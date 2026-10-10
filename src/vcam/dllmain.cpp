#include "MediaSource.h"

#include "../shared/FrameProtocol.h"

#include <new>
#include <strsafe.h>

namespace {

HMODULE g_module = nullptr;
long g_locks = 0;

void Log(const wchar_t* message) {
    OutputDebugStringW(message);
    OutputDebugStringW(L"\n");
    CreateDirectoryW(L"C:\\ProgramData\\Callayers", nullptr);
    HANDLE file = CreateFileW(
        L"C:\\ProgramData\\Callayers\\vcam.log",
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    DWORD written = 0;
    char buffer[1024]{};
    WideCharToMultiByte(CP_UTF8, 0, message, -1, buffer, sizeof(buffer) - 2, nullptr, nullptr);
    StringCchCatA(buffer, sizeof(buffer), "\r\n");
    WriteFile(file, buffer, static_cast<DWORD>(strlen(buffer)), &written, nullptr);
    CloseHandle(file);
}

HRESULT RegisterAt(HKEY root, const wchar_t* keyPath) {
    wchar_t path[MAX_PATH]{};
    if (!GetModuleFileNameW(g_module, path, MAX_PATH)) {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    HKEY clsid = nullptr;
    LONG status = RegCreateKeyExW(root, keyPath, 0, nullptr, 0, KEY_WRITE, nullptr, &clsid, nullptr);
    if (status != ERROR_SUCCESS) {
        return HRESULT_FROM_WIN32(status);
    }
    const wchar_t* name = L"Callayers Virtual Camera";
    RegSetValueExW(clsid, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE*>(name), static_cast<DWORD>((wcslen(name) + 1) * sizeof(wchar_t)));
    HKEY inproc = nullptr;
    status = RegCreateKeyExW(clsid, L"InprocServer32", 0, nullptr, 0, KEY_WRITE, nullptr, &inproc, nullptr);
    RegCloseKey(clsid);
    if (status != ERROR_SUCCESS) {
        return HRESULT_FROM_WIN32(status);
    }
    status = RegSetValueExW(inproc, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE*>(path), static_cast<DWORD>((wcslen(path) + 1) * sizeof(wchar_t)));
    if (status == ERROR_SUCCESS) {
        const wchar_t* threading = L"Both";
        status = RegSetValueExW(inproc, L"ThreadingModel", 0, REG_SZ, reinterpret_cast<const BYTE*>(threading), static_cast<DWORD>((wcslen(threading) + 1) * sizeof(wchar_t)));
    }
    RegCloseKey(inproc);
    return HRESULT_FROM_WIN32(status == ERROR_SUCCESS ? ERROR_SUCCESS : status);
}

} // namespace

long* CallayersServerLock() {
    return &g_locks;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = module;
        DisableThreadLibraryCalls(module);
        Log(L"CallayersVCam loaded");
    }
    return TRUE;
}

STDAPI DllCanUnloadNow() {
    return g_locks == 0 ? S_OK : S_FALSE;
}

STDAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void** object) {
    if (!object) {
        return E_POINTER;
    }
    *object = nullptr;
    if (clsid != callayers::kActivateClsid) {
        Log(L"DllGetClassObject unknown clsid");
        return CLASS_E_CLASSNOTAVAILABLE;
    }
    Log(L"DllGetClassObject CallayersActivate");
    auto* factory = new (std::nothrow) CallayersClassFactory();
    if (!factory) {
        return E_OUTOFMEMORY;
    }
    const HRESULT hr = factory->QueryInterface(riid, object);
    factory->Release();
    return hr;
}

STDAPI DllRegisterServer() {
    wchar_t keyPath[160]{};
    StringCchPrintfW(keyPath, 160, L"Software\\Classes\\CLSID\\%s", callayers::kActivateClsidString);
    const HRESULT user = RegisterAt(HKEY_CURRENT_USER, keyPath);
    StringCchPrintfW(keyPath, 160, L"SOFTWARE\\Classes\\CLSID\\%s", callayers::kActivateClsidString);
    const HRESULT machine = RegisterAt(HKEY_LOCAL_MACHINE, keyPath);
    if (SUCCEEDED(machine)) {
        Log(L"Registered Callayers virtual camera for this machine");
        return S_OK;
    }
    Log(L"Machine registration failed; current-user registration was still attempted");
    // Frame Server is Local System and only reads HKLM. HKCU is enough for the
    // in-process checker, not for Windows Camera. The caller elevates once.
    return FAILED(user) ? user : machine;
}

STDAPI DllUnregisterServer() {
    wchar_t keyPath[180]{};
    StringCchPrintfW(keyPath, 180, L"Software\\Classes\\CLSID\\%s\\InprocServer32", callayers::kActivateClsidString);
    RegDeleteKeyW(HKEY_CURRENT_USER, keyPath);
    StringCchPrintfW(keyPath, 180, L"Software\\Classes\\CLSID\\%s", callayers::kActivateClsidString);
    RegDeleteKeyW(HKEY_CURRENT_USER, keyPath);
    StringCchPrintfW(keyPath, 180, L"SOFTWARE\\Classes\\CLSID\\%s\\InprocServer32", callayers::kActivateClsidString);
    RegDeleteKeyW(HKEY_LOCAL_MACHINE, keyPath);
    StringCchPrintfW(keyPath, 180, L"SOFTWARE\\Classes\\CLSID\\%s", callayers::kActivateClsidString);
    const LONG status = RegDeleteKeyW(HKEY_LOCAL_MACHINE, keyPath);
    return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND ? S_OK : HRESULT_FROM_WIN32(status);
}
