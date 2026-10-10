#include "pch.h"
#include "VirtualCamera.h"

#include "../shared/FrameProtocol.h"

#include <mfapi.h>
#include <mferror.h>
#include <mfvirtualcamera.h>
#include <sddl.h>
#include <shellapi.h>
#include <shlwapi.h>

#include <cwchar>

#pragma comment(lib, "mfsensorgroup.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shlwapi.lib")

namespace callayers {
namespace {

std::wstring DllPath() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    PathRemoveFileSpecW(path);
    PathAppendW(path, L"CallayersVCam.dll");
    return path;
}

bool IsWindows11() {
    using RtlGetVersionPtr = LONG(WINAPI*)(OSVERSIONINFOEXW*);
    auto rtl = reinterpret_cast<RtlGetVersionPtr>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
    OSVERSIONINFOEXW os{};
    os.dwOSVersionInfoSize = sizeof(os);
    if (!rtl || rtl(&os) != 0) {
        return false;
    }
    return os.dwBuildNumber >= 22000;
}

std::wstring Hex(HRESULT hr) {
    wchar_t text[16]{};
    swprintf_s(text, L"0x%08X", static_cast<unsigned>(hr));
    return text;
}

std::wstring SharedDllPath() {
    return L"C:\\ProgramData\\Callayers\\CallayersVCam.dll";
}

// Frame Server runs as Local System and Frame Server Monitor as Local Service.
// Both have to read the DLL, so the registered copy lives in ProgramData.
std::wstring InstallSharedDll(const std::wstring& source) {
    CreateDirectoryW(L"C:\\ProgramData\\Callayers", nullptr);
    const std::wstring dest = SharedDllPath();
    if (!CopyFileW(source.c_str(), dest.c_str(), FALSE) &&
        GetFileAttributesW(dest.c_str()) == INVALID_FILE_ATTRIBUTES) {
        return {};
    }
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:P(A;;GRGX;;;SY)(A;;GRGX;;;LS)(A;;GRGX;;;BA)(A;;GRGX;;;BU)(A;;GRGX;;;IU)",
            SDDL_REVISION_1,
            &descriptor,
            nullptr)) {
        SetFileSecurityW(dest.c_str(), DACL_SECURITY_INFORMATION, descriptor);
        LocalFree(descriptor);
    }
    return dest;
}

bool IsMachineRegistered() {
    wchar_t keyPath[220]{};
    swprintf_s(keyPath, L"SOFTWARE\\Classes\\CLSID\\%s\\InprocServer32", kActivateClsidString);
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, keyPath, 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return false;
    }
    wchar_t path[MAX_PATH]{};
    DWORD bytes = sizeof(path);
    DWORD type = 0;
    const LONG status = RegQueryValueExW(key, nullptr, nullptr, &type, reinterpret_cast<BYTE*>(path), &bytes);
    RegCloseKey(key);
    return status == ERROR_SUCCESS && type == REG_SZ && GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

bool RequestMachineRegistration() {
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS;
    info.lpVerb = L"runas";
    info.lpFile = exe;
    info.lpParameters = L"--register-vcam";
    info.nShow = SW_HIDE;
    if (!ShellExecuteExW(&info)) {
        return false;
    }
    if (info.hProcess) {
        WaitForSingleObject(info.hProcess, 120000);
        CloseHandle(info.hProcess);
    }
    return IsMachineRegistered();
}

} // namespace

HRESULT RegisterInstalledDll() {
    const std::wstring source = DllPath();
    const std::wstring shared = InstallSharedDll(source);
    const std::wstring& path = shared.empty() ? source : shared;
    HMODULE library = LoadLibraryW(path.c_str());
    if (!library) {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    auto registerServer = reinterpret_cast<HRESULT(STDAPICALLTYPE*)()>(GetProcAddress(library, "DllRegisterServer"));
    if (!registerServer) {
        FreeLibrary(library);
        return E_FAIL;
    }
    const HRESULT hr = registerServer();
    FreeLibrary(library);
    return hr;
}

std::wstring EnsureVirtualCamera() {
    if (!IsWindows11()) {
        return L"This camera needs Windows 11.";
    }
    if (!IsMachineRegistered()) {
        const HRESULT registered = RegisterInstalledDll();
        if (!IsMachineRegistered()) {
            if (registered != HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED)) {
                return L"Could not load CallayersVCam.dll " + Hex(registered) + L". The preview is still running.";
            }
            if (!RequestMachineRegistration()) {
                return L"The preview is running. Windows needs a one-time admin approval before other apps can open the Callayers camera.";
            }
        }
    }

    const HRESULT started = MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);
    if (FAILED(started)) {
        return L"Media Foundation failed to start " + Hex(started) + L".";
    }

    auto create = [](winrt::com_ptr<IMFVirtualCamera>& camera) {
        camera = nullptr;
        return MFCreateVirtualCamera(
            MFVirtualCameraType_SoftwareCameraSource,
            MFVirtualCameraLifetime_System,
            MFVirtualCameraAccess_CurrentUser,
            kCameraName,
            kActivateClsidString,
            nullptr,
            0,
            camera.put());
    };

    winrt::com_ptr<IMFVirtualCamera> camera;
    HRESULT hr = create(camera);
    if (FAILED(hr)) {
        return L"Could not create the Callayers camera " + Hex(hr) + L".";
    }
    hr = camera->Start(nullptr);
    if (hr == MF_E_SHUTDOWN) {
        // A previous process left this system camera shut down. Remove it and
        // register a new one with the same name.
        camera->Remove();
        hr = create(camera);
        if (SUCCEEDED(hr)) {
            hr = camera->Start(nullptr);
        }
    }
    if (FAILED(hr)) {
        return L"Could not start the Callayers camera " + Hex(hr) + L".";
    }

    // Keep the control object alive for this process. System lifetime keeps the
    // device after we exit, and the media source then sends the idle frame.
    camera.detach();
    return L"Callayers is a camera. The moving bar is what Windows Camera and Chrome should show.";
}

} // namespace callayers
