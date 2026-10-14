#include <windows.h>
#include <string>
#include <filesystem>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    wchar_t executable[32768]{}, system[32768]{};
    GetModuleFileNameW(nullptr, executable, 32768);
    GetSystemDirectoryW(system, 32768);
    auto root = std::filesystem::path(executable).parent_path();
    auto powershell = std::filesystem::path(system) / L"WindowsPowerShell/v1.0/powershell.exe";
    std::wstring command = L"\"" + powershell.wstring() + L"\" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"" +
        (root / L"update.ps1").wstring() + L"\" -Mode ApplyAndLaunch";
    STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION process{};
    if (!CreateProcessW(powershell.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                        nullptr, root.c_str(), &startup, &process)) {
        MessageBoxW(nullptr, L"Callayers couldn't start. Run the installer again; your saved cards will be kept.", L"Callayers", MB_OK | MB_ICONERROR);
        return 1;
    }
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    return 0;
}
