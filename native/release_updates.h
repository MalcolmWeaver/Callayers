#pragma once
#include <windows.h>
#include <filesystem>
#include <string>

namespace callayers {
inline std::filesystem::path ReleaseRoot() {
#ifdef CALLAYERS_RELEASE
    wchar_t executable[32768]{}; GetModuleFileNameW(nullptr, executable, 32768);
    auto version = std::filesystem::path(executable).parent_path();
    if(version.parent_path().filename()==L"versions")return version.parent_path().parent_path();
#endif
    return {};
}
inline bool StartReleaseUpdate(const wchar_t* mode, DWORD waitForPid = 0, HANDLE* running = nullptr) {
#ifdef CALLAYERS_RELEASE
    wchar_t executable[32768]{}, system[32768]{};
    GetModuleFileNameW(nullptr, executable, 32768); GetSystemDirectoryW(system, 32768);
    auto version = std::filesystem::path(executable).parent_path();
    // Only installed, versioned releases run the updater; build outputs stay isolated.
    if (version.parent_path().filename() != L"versions") return false;
    auto root = version.parent_path().parent_path();
    if (!std::filesystem::exists(root / L"update.ps1")) return false;
    auto powershell = std::filesystem::path(system) / L"WindowsPowerShell/v1.0/powershell.exe";
    std::wstring command = L"\"" + powershell.wstring() + L"\" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"" +
        (root / L"update.ps1").wstring() + L"\" -Mode " + mode + L" -WaitForPid " + std::to_wstring(waitForPid);
    STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION process{};
    if (CreateProcessW(powershell.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                       nullptr, root.c_str(), &startup, &process)) { CloseHandle(process.hThread); if(running)*running=process.hProcess;else CloseHandle(process.hProcess);return true; }
#else
    (void)mode; (void)waitForPid; (void)running;
#endif
    return false;
}
}
