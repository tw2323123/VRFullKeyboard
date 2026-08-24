#include <windows.h>
#include <tlhelp32.h>
#include <shellapi.h>

#include <cwchar>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {
std::wstring Quote(const std::wstring& value) {
    std::wstring out = L"\"";
    for (wchar_t ch : value) {
        if (ch == L'\"') out += L"\\\"";
        else out += ch;
    }
    out += L"\"";
    return out;
}

std::wstring ArgValue(int argc, wchar_t** argv, const wchar_t* name) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (_wcsicmp(argv[i], name) == 0) return argv[i + 1];
    }
    return {};
}

DWORD ArgDword(int argc, wchar_t** argv, const wchar_t* name) {
    const std::wstring value = ArgValue(argc, argv, name);
    if (value.empty()) return 0;
    return static_cast<DWORD>(_wcstoui64(value.c_str(), nullptr, 10));
}

void WaitForProcessId(DWORD pid, DWORD timeoutMs) {
    if (!pid) return;
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!process) return;
    WaitForSingleObject(process, timeoutMs);
    CloseHandle(process);
}

std::vector<DWORD> FindProcessesByName(const wchar_t* exeName) {
    std::vector<DWORD> result;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return result;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snap, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, exeName) == 0 && entry.th32ProcessID != GetCurrentProcessId()) {
                result.push_back(entry.th32ProcessID);
            }
        } while (Process32NextW(snap, &entry));
    }
    CloseHandle(snap);
    return result;
}

struct CloseWindowsContext { DWORD pid = 0; };
BOOL CALLBACK CloseWindowsProc(HWND hwnd, LPARAM param) {
    auto* ctx = reinterpret_cast<CloseWindowsContext*>(param);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == ctx->pid) PostMessageW(hwnd, WM_CLOSE, 0, 0);
    return TRUE;
}

void StopOwnProcess(const wchar_t* exeName) {
    for (DWORD pid : FindProcessesByName(exeName)) {
        CloseWindowsContext ctx{pid};
        EnumWindows(CloseWindowsProc, reinterpret_cast<LPARAM>(&ctx));
        HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE, FALSE, pid);
        if (!process) continue;
        if (WaitForSingleObject(process, 5000) == WAIT_TIMEOUT) {
            TerminateProcess(process, 0);
            WaitForSingleObject(process, 2000);
        }
        CloseHandle(process);
    }
}

bool CopyFileSafe(const fs::path& from, const fs::path& to) {
    std::error_code ec;
    fs::create_directories(to.parent_path(), ec);
    ec.clear();
    fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
    return !ec;
}

bool WriteText(const fs::path& path, const std::string& text) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    return !!out;
}

std::string ReadText(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

bool RestoreBackup(const fs::path& backupDir, const fs::path& installDir,
                   const std::vector<fs::path>& newlyCreated) {
    std::error_code ec;
    for (const auto& rel : newlyCreated) {
        fs::remove(installDir / rel, ec);
        ec.clear();
    }
    if (!fs::exists(backupDir)) return true;
    for (fs::recursive_directory_iterator it(backupDir, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file()) continue;
        const fs::path rel = fs::relative(it->path(), backupDir, ec);
        if (ec) return false;
        if (!CopyFileSafe(it->path(), installDir / rel)) return false;
    }
    return true;
}

bool ApplyUpdate(const fs::path& stageDir, const fs::path& installDir, const fs::path& backupDir,
                 std::vector<fs::path>& newlyCreated) {
    std::error_code ec;
    fs::remove_all(backupDir, ec);
    ec.clear();
    fs::create_directories(backupDir, ec);
    if (ec) return false;

    newlyCreated.clear();
    for (fs::recursive_directory_iterator it(stageDir, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file()) continue;
        const fs::path rel = fs::relative(it->path(), stageDir, ec);
        if (ec) return false;

        // User settings are never updated by the package, even if a future archive accidentally contains them.
        if (_wcsicmp(rel.filename().c_str(), L"VRFullKeyboard.ini") == 0) continue;

        const fs::path dest = installDir / rel;
        if (fs::exists(dest, ec)) {
            ec.clear();
            if (!CopyFileSafe(dest, backupDir / rel)) {
                RestoreBackup(backupDir, installDir, newlyCreated);
                return false;
            }
        } else {
            newlyCreated.push_back(rel);
        }

        if (!CopyFileSafe(it->path(), dest)) {
            RestoreBackup(backupDir, installDir, newlyCreated);
            return false;
        }
    }
    if (ec) {
        RestoreBackup(backupDir, installDir, newlyCreated);
        return false;
    }
    return true;
}

bool LaunchProgramChecked(const fs::path& exe, const fs::path& cwd,
                          const std::vector<std::wstring>& args, DWORD healthWaitMs) {
    std::wstring command = Quote(exe.wstring());
    for (const auto& arg : args) command += L" " + Quote(arg);
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    const BOOL ok = CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE,
                                   0, nullptr, cwd.wstring().c_str(), &si, &pi);
    if (!ok) return false;
    CloseHandle(pi.hThread);
    if (healthWaitMs > 0) {
        const DWORD wait = WaitForSingleObject(pi.hProcess, healthWaitMs);
        if (wait == WAIT_OBJECT_0) {
            CloseHandle(pi.hProcess);
            return false;
        }
    }
    CloseHandle(pi.hProcess);
    return true;
}

bool RunRollbackSelfTest(const fs::path& root) {
    std::error_code ec;
    fs::remove_all(root, ec);
    const fs::path install = root / L"install";
    const fs::path stage = root / L"stage";
    const fs::path backup = root / L"backup";
    fs::create_directories(install, ec);
    fs::create_directories(stage, ec);
    if (ec) return false;

    if (!WriteText(install / L"core.bin", "old-core") ||
        !WriteText(install / L"VRFullKeyboard.ini", "user-settings") ||
        !WriteText(stage / L"core.bin", "new-core") ||
        !WriteText(stage / L"new-file.bin", "new-file") ||
        !WriteText(stage / L"VRFullKeyboard.ini", "must-not-overwrite")) return false;

    std::vector<fs::path> newlyCreated;
    if (!ApplyUpdate(stage, install, backup, newlyCreated)) return false;
    const bool applied = ReadText(install / L"core.bin") == "new-core" &&
                         ReadText(install / L"VRFullKeyboard.ini") == "user-settings" &&
                         fs::exists(install / L"new-file.bin");
    const bool restored = RestoreBackup(backup, install, newlyCreated) &&
                          ReadText(install / L"core.bin") == "old-core" &&
                          ReadText(install / L"VRFullKeyboard.ini") == "user-settings" &&
                          !fs::exists(install / L"new-file.bin");
    fs::remove_all(root, ec);
    return applied && restored;
}

void ScheduleCleanup(const fs::path& updateRoot) {
    const std::wstring command = L"/d /c timeout /t 2 /nobreak >nul & rmdir /s /q " + Quote(updateRoot.wstring());
    ShellExecuteW(nullptr, L"open", L"cmd.exe", command.c_str(), nullptr, SW_HIDE);
}
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return 2;

    const fs::path selfTestDir = ArgValue(argc, argv, L"--self-test-dir");
    if (!selfTestDir.empty()) {
        LocalFree(argv);
        return RunRollbackSelfTest(selfTestDir) ? 0 : 20;
    }

    const fs::path installDir = ArgValue(argc, argv, L"--install-dir");
    const fs::path stageDir = ArgValue(argc, argv, L"--stage-dir");
    const fs::path backupDir = ArgValue(argc, argv, L"--backup-dir");
    const fs::path relaunch = ArgValue(argc, argv, L"--relaunch");
    const std::wstring fromVersion = ArgValue(argc, argv, L"--from-version");
    const std::wstring toVersion = ArgValue(argc, argv, L"--to-version");
    const DWORD parentPid = ArgDword(argc, argv, L"--parent-pid");
    LocalFree(argv);

    if (installDir.empty() || stageDir.empty() || backupDir.empty() || relaunch.empty()) return 3;
    if (!fs::exists(stageDir / L"VRFullKeyboardControl.exe") ||
        !fs::exists(stageDir / L"VRFullKeyboard.exe") ||
        !fs::exists(stageDir / L"openvr_api.dll")) return 4;

    WaitForProcessId(parentPid, 15000);
    StopOwnProcess(L"VRFullKeyboard.exe");
    StopOwnProcess(L"VRFullKeyboardControl.exe");

    std::vector<fs::path> newlyCreated;
    if (!ApplyUpdate(stageDir, installDir, backupDir, newlyCreated)) {
        const fs::path oldControl = installDir / relaunch;
        LaunchProgramChecked(oldControl, installDir,
            {L"--update-result", L"rollback", L"--from-version", fromVersion, L"--to-version", toVersion}, 0);
        return 5;
    }

    const fs::path launchPath = installDir / relaunch;
    if (!LaunchProgramChecked(launchPath, installDir,
            {L"--update-result", L"success", L"--from-version", fromVersion, L"--to-version", toVersion}, 2500)) {
        StopOwnProcess(L"VRFullKeyboardControl.exe");
        const bool restored = RestoreBackup(backupDir, installDir, newlyCreated);
        if (restored) {
            LaunchProgramChecked(installDir / relaunch, installDir,
                {L"--update-result", L"rollback", L"--from-version", fromVersion, L"--to-version", toVersion}, 0);
        } else {
            MessageBoxW(nullptr,
                        L"新版控制中心啟動失敗，而且自動還原未完整成功。\n\n請從 GitHub Release 重新下載分享版。",
                        L"VR Full Keyboard 更新失敗", MB_ICONERROR | MB_OK);
        }
        return 6;
    }

    ScheduleCleanup(stageDir.parent_path());
    return 0;
}
