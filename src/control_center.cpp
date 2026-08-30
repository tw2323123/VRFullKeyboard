#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <bcrypt.h>
#include <dwmapi.h>
#include <winhttp.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cwctype>
#include <cstring>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <map>
#include <iterator>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <unordered_set>
#include <vector>

#include "VRFullKeyboardVersion.h"

namespace fs = std::filesystem;

namespace {
constexpr UINT WM_APP_LOG = WM_APP + 1;
constexpr UINT WM_APP_TASK_DONE = WM_APP + 2;
constexpr UINT WM_APP_UPDATE_CHECK_DONE = WM_APP + 3;
constexpr UINT WM_APP_UPDATE_EXIT = WM_APP + 4;
constexpr UINT WM_APP_UPDATE_PROGRESS = WM_APP + 5;
constexpr UINT_PTR TIMER_BACKGROUND_UPDATE = 1;
constexpr UINT_PTR TIMER_PERF_REFRESH = 2;

constexpr int IDC_STATUS = 1001;
constexpr int IDC_ENV = 1002;
constexpr int IDC_RUN_VR = 1101;
constexpr int IDC_PREVIEW = 1102;
constexpr int IDC_EDITOR = 1103;
constexpr int IDC_UPDATE = 1104;
constexpr int IDC_STOP_VR = 1105;
constexpr int IDC_BUILD = 1201;
constexpr int IDC_PACKAGE = 1202;
constexpr int IDC_CLEAN = 1203;
constexpr int IDC_OPEN_OUTPUT = 1204;
constexpr int IDC_PUBLISH_GITHUB = 1205;
constexpr int IDC_OPEN_ROOT = 1301;
constexpr int IDC_SHORTCUT = 1302;
constexpr int IDC_OPEN_LOG = 1303;
constexpr int IDC_ROLLBACK_TEST = 1304;
constexpr int IDC_OPEN_PERF_REPORT = 1305;
constexpr int IDC_LOG = 1401;
constexpr int IDC_LOG_TOGGLE = 1402;
constexpr int IDC_LOG_CLEAR = 1403;
constexpr int IDC_LOG_COPY = 1404;

constexpr int IDC_RELEASE_VERSION = 2101;
constexpr int IDC_RELEASE_CONFIRM = 2102;
constexpr int IDC_RELEASE_CANCEL = 2103;
constexpr wchar_t kReleaseDialogClass[] = L"VRFullKeyboardReleaseDialogClass";

struct TaskDone {
    bool success = false;
    int pendingLaunch = 0;
    bool offerActions = false;
    std::wstring summary;
};

struct UpdateReleaseInfo {
    bool success = false;
    bool updateAvailable = false;
    bool silent = false;
    std::string tag;
    std::string name;
    std::string notes;
    std::string pageUrl;
    std::string zipUrl;
    std::string shaUrl;
    std::wstring error;
};

enum class PendingLaunch : int { None = 0, Vr = 1, Preview = 2, Editor = 3, Update = 4 };

HINSTANCE g_instance = nullptr;
HWND g_hwnd = nullptr;
HWND g_log = nullptr;
HFONT g_font = nullptr;
HFONT g_smallFont = nullptr;
HFONT g_titleFont = nullptr;
HFONT g_logFont = nullptr;
HBRUSH g_logBrush = nullptr;
HBRUSH g_releaseEditBrush = nullptr;
HWND g_releaseDialog = nullptr;
std::atomic_bool g_busy{false};
std::atomic_bool g_updateCheckBusy{false};
fs::path g_root;
fs::path g_persistentLogPath;
bool g_developerMode = false;
bool g_coreReady = false;
bool g_coreOutdated = false;
bool g_cmakeReady = false;
bool g_gitReady = false;
bool g_logExpanded = false;
bool g_updateAvailableHint = false;
std::wstring g_updateAvailableTag;
std::wstring g_updateButtonSubtitle = L"檢查 GitHub、驗證並安裝新版";
constexpr wchar_t kControlExitEventName[] = L"Local\\VRFullKeyboard.Exit.AlphaV3_9_11";
bool g_coreRunning = false;
RECT g_perfPaintRect{0, 0, 0, 0};
struct PerfSnapshot {
    bool valid = false;
    float cpuPercent = 0.0f;
    float loopsPerSec = 0.0f;
    float avgTrackingMs = 0.0f, maxTrackingMs = 0.0f;
    float avgEventsMs = 0.0f, maxEventsMs = 0.0f;
    float avgLogicMs = 0.0f, maxLogicMs = 0.0f;
    float avgImGuiMs = 0.0f, maxImGuiMs = 0.0f;
    float avgD3DMs = 0.0f, maxD3DMs = 0.0f;
    float avgSubmitMs = 0.0f, maxSubmitMs = 0.0f;
    float avgTotalMs = 0.0f, maxTotalMs = 0.0f;
    int submitError = 0;
};
PerfSnapshot g_perfSnapshot{};
std::vector<HWND> g_taskButtons;
std::vector<HWND> g_allButtons;
std::unordered_set<HWND> g_hoverButtons;

void SetLogExpanded(bool expanded);
void Layout(HWND hwnd);
void OpenFolder(const fs::path& path);
void StartPublishTask(const std::wstring& version);
void ShowReleaseDialog();

std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), (int)text.size(), nullptr, 0);
    UINT cp = CP_UTF8;
    DWORD flags = MB_ERR_INVALID_CHARS;
    if (len <= 0) {
        cp = CP_ACP;
        flags = 0;
        len = MultiByteToWideChar(cp, flags, text.data(), (int)text.size(), nullptr, 0);
    }
    if (len <= 0) return L"";
    std::wstring out((size_t)len, L'\0');
    MultiByteToWideChar(cp, flags, text.data(), (int)text.size(), out.data(), len);
    return out;
}

std::string WideToUtf8(const std::wstring& text) {
    if (text.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string out((size_t)len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), out.data(), len, nullptr, nullptr);
    return out;
}

std::wstring TrimWide(std::wstring value) {
    auto notSpace = [](wchar_t ch) { return !iswspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    return value;
}

bool IsSemVer(const std::wstring& value) {
    if (value.empty()) return false;
    int partCount = 0;
    size_t start = 0;
    while (start <= value.size()) {
        const size_t dot = value.find(L'.', start);
        const size_t end = (dot == std::wstring::npos) ? value.size() : dot;
        if (end == start) return false;
        for (size_t i = start; i < end; ++i) {
            if (!iswdigit(value[i])) return false;
        }
        ++partCount;
        if (dot == std::wstring::npos) break;
        start = dot + 1;
    }
    return partCount == 3;
}

bool WriteVersionFile(const std::wstring& version) {
    std::ofstream out(g_root / L"VERSION", std::ios::binary | std::ios::trunc);
    if (!out) return false;
    const std::string utf8 = WideToUtf8(version + L"\n");
    out.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
    return out.good();
}

std::wstring Quote(const std::wstring& value) {
    std::wstring out = L"\"";
    for (wchar_t ch : value) {
        if (ch == L'\"') out += L"\\\"";
        else out += ch;
    }
    out += L"\"";
    return out;
}

fs::path ModulePath() {
    std::vector<wchar_t> buffer(32768);
    DWORD len = GetModuleFileNameW(nullptr, buffer.data(), (DWORD)buffer.size());
    return len ? fs::path(std::wstring(buffer.data(), len)) : fs::path();
}

fs::path DetectRoot() {
    const fs::path exeDir = ModulePath().parent_path();
    if (fs::exists(exeDir / L"CMakeLists.txt")) return exeDir;
    if (exeDir.filename() == L".frontend" && fs::exists(exeDir.parent_path() / L"CMakeLists.txt")) {
        return exeDir.parent_path();
    }
    return exeDir;
}

fs::path CoreExePath() {
    if (g_developerMode) return g_root / L"build" / L"Release" / L"VRFullKeyboard.exe";
    return g_root / L"VRFullKeyboard.exe";
}

bool IsCoreBuildOutdated(std::wstring* newerSource = nullptr) {
    if (!g_developerMode) return false;
    const fs::path exe = CoreExePath();
    std::error_code ec;
    if (!fs::exists(exe, ec)) return false;
    const auto exeTime = fs::last_write_time(exe, ec);
    if (ec) return false;

    const fs::path inputs[] = {
        g_root / L"src" / L"main.cpp",
        g_root / L"CMakeLists.txt",
        g_root / L"VERSION",
        g_root / L"TEST_BUILD",
        g_root / L"cmake" / L"VRFullKeyboardVersion.h.in",
    };
    for (const auto& input : inputs) {
        ec.clear();
        if (!fs::exists(input, ec) || ec) continue;
        const auto inputTime = fs::last_write_time(input, ec);
        if (!ec && inputTime > exeTime) {
            if (newerSource) *newerSource = input.lexically_relative(g_root).wstring();
            return true;
        }
    }
    return false;
}

fs::path OpenVrDllPath() {
    if (g_developerMode) return g_root / L"build" / L"Release" / L"openvr_api.dll";
    return g_root / L"openvr_api.dll";
}

fs::path UpdaterExePath() {
    if (g_developerMode) return g_root / L"build" / L"Release" / L"VRFullKeyboardUpdater.exe";
    return g_root / L"VRFullKeyboardUpdater.exe";
}

std::wstring EnvValue(const wchar_t* name) {
    DWORD len = GetEnvironmentVariableW(name, nullptr, 0);
    if (!len) return {};
    std::wstring value(len, L'\0');
    GetEnvironmentVariableW(name, value.data(), len);
    if (!value.empty() && value.back() == L'\0') value.pop_back();
    return value;
}

fs::path PersistentLogPath() {
    const std::wstring local = EnvValue(L"LOCALAPPDATA");
    fs::path base = local.empty() ? g_root : fs::path(local);
    return base / L"VRFullKeyboard" / L"logs" / L"control_center.log";
}

void PreparePersistentLog() {
    g_persistentLogPath = PersistentLogPath();
    std::error_code ec;
    fs::create_directories(g_persistentLogPath.parent_path(), ec);
    if (!ec && fs::exists(g_persistentLogPath, ec) && fs::file_size(g_persistentLogPath, ec) > 2 * 1024 * 1024) {
        fs::path old = g_persistentLogPath;
        old += L".old";
        fs::remove(old, ec);
        ec.clear();
        fs::rename(g_persistentLogPath, old, ec);
    }
}

void AppendPersistentLog(const std::wstring& text) {
    if (g_persistentLogPath.empty()) return;
    std::ofstream out(g_persistentLogPath, std::ios::binary | std::ios::app);
    if (!out) return;
    const std::string utf8 = WideToUtf8(text);
    out.write(utf8.data(), (std::streamsize)utf8.size());
}

std::wstring ReadTextFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return Utf8ToWide(data);
}

bool ContainsBuildWarning(std::wstring text) {
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t ch) { return (wchar_t)towlower(ch); });
    return text.find(L"warning") != std::wstring::npos ||
           text.find(L"deprecated") != std::wstring::npos ||
           text.find(L"警告") != std::wstring::npos;
}

fs::path SearchProgram(const wchar_t* exe) {
    wchar_t buffer[32768]{};
    DWORD len = SearchPathW(nullptr, exe, nullptr, (DWORD)std::size(buffer), buffer, nullptr);
    if (len > 0 && len < std::size(buffer)) return fs::path(buffer);
    return {};
}

fs::path FindCMake() {
    fs::path found = SearchProgram(L"cmake.exe");
    if (!found.empty()) return found;

    const std::wstring pf = EnvValue(L"ProgramFiles");
    if (!pf.empty()) {
        const std::vector<fs::path> candidates = {
            fs::path(pf) / L"CMake/bin/cmake.exe",
            fs::path(pf) / L"Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe",
            fs::path(pf) / L"Microsoft Visual Studio/2022/Professional/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe",
            fs::path(pf) / L"Microsoft Visual Studio/2022/Enterprise/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe",
            fs::path(pf) / L"Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
        };
        for (const auto& p : candidates) if (fs::exists(p)) return p;
    }
    return {};
}

fs::path FindGit() {
    fs::path found = SearchProgram(L"git.exe");
    if (!found.empty()) return found;
    const std::wstring pf = EnvValue(L"ProgramFiles");
    if (!pf.empty()) {
        fs::path p = fs::path(pf) / L"Git/cmd/git.exe";
        if (fs::exists(p)) return p;
    }
    return {};
}

void PostLog(const std::wstring& text) {
    if (!g_hwnd || !IsWindow(g_hwnd)) return;
    auto* heapText = new std::wstring(text);
    PostMessageW(g_hwnd, WM_APP_LOG, 0, reinterpret_cast<LPARAM>(heapText));
}

void PostUpdateProgress(const std::wstring& text) {
    if (!g_hwnd || !IsWindow(g_hwnd)) return;
    auto* heapText = new std::wstring(text);
    PostMessageW(g_hwnd, WM_APP_UPDATE_PROGRESS, 0, reinterpret_cast<LPARAM>(heapText));
}

void AppendLog(const std::wstring& text) {
    AppendPersistentLog(text);
    if (!g_log) return;
    int len = GetWindowTextLengthW(g_log);
    SendMessageW(g_log, EM_SETSEL, len, len);
    SendMessageW(g_log, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(text.c_str()));
    SendMessageW(g_log, EM_SCROLLCARET, 0, 0);
}

fs::path PerfReportPath() {
    const std::wstring local = EnvValue(L"LOCALAPPDATA");
    fs::path base = local.empty() ? g_root : fs::path(local);
    return base / L"VRFullKeyboard" / L"logs" / L"perf_alpha_v3_9_11.csv";
}

fs::path PerfLivePath() {
    const std::wstring local = EnvValue(L"LOCALAPPDATA");
    fs::path base = local.empty() ? g_root : fs::path(local);
    return base / L"VRFullKeyboard" / L"logs" / L"perf_live_alpha_v3_9_11.ini";
}

bool IsCoreRunning() {
    HANDLE eventHandle = OpenEventW(SYNCHRONIZE, FALSE, kControlExitEventName);
    if (!eventHandle) return false;
    CloseHandle(eventHandle);
    return true;
}

bool RequestCoreShutdown() {
    HANDLE eventHandle = OpenEventW(EVENT_MODIFY_STATE, FALSE, kControlExitEventName);
    if (!eventHandle) return false;
    const BOOL ok = SetEvent(eventHandle);
    CloseHandle(eventHandle);
    return ok == TRUE;
}

float ParseFloatOr(const std::map<std::wstring, std::wstring>& values, const wchar_t* key, float fallback = 0.0f) {
    auto it = values.find(key);
    if (it == values.end()) return fallback;
    try { return std::stof(it->second); } catch (...) { return fallback; }
}

int ParseIntOr(const std::map<std::wstring, std::wstring>& values, const wchar_t* key, int fallback = 0) {
    auto it = values.find(key);
    if (it == values.end()) return fallback;
    try { return std::stoi(it->second); } catch (...) { return fallback; }
}

void RefreshPerfSnapshot() {
    const bool wasRunning = g_coreRunning;
    g_coreRunning = IsCoreRunning();
    PerfSnapshot next{};
    if (g_coreRunning) {
        std::wifstream in(PerfLivePath());
        std::map<std::wstring, std::wstring> values;
        std::wstring line;
        while (std::getline(in, line)) {
            const size_t eq = line.find(L'=');
            if (eq == std::wstring::npos) continue;
            values[TrimWide(line.substr(0, eq))] = TrimWide(line.substr(eq + 1));
        }
        if (!values.empty()) {
            next.valid = true;
            next.cpuPercent = ParseFloatOr(values, L"cpu_percent");
            next.loopsPerSec = ParseFloatOr(values, L"loops_per_sec");
            next.avgTrackingMs = ParseFloatOr(values, L"avg_tracking_ms");
            next.maxTrackingMs = ParseFloatOr(values, L"max_tracking_ms");
            next.avgEventsMs = ParseFloatOr(values, L"avg_events_ms");
            next.maxEventsMs = ParseFloatOr(values, L"max_events_ms");
            next.avgLogicMs = ParseFloatOr(values, L"avg_logic_ms");
            next.maxLogicMs = ParseFloatOr(values, L"max_logic_ms");
            next.avgImGuiMs = ParseFloatOr(values, L"avg_imgui_ms");
            next.maxImGuiMs = ParseFloatOr(values, L"max_imgui_ms");
            next.avgD3DMs = ParseFloatOr(values, L"avg_d3d_ms");
            next.maxD3DMs = ParseFloatOr(values, L"max_d3d_ms");
            next.avgSubmitMs = ParseFloatOr(values, L"avg_submit_ms");
            next.maxSubmitMs = ParseFloatOr(values, L"max_submit_ms");
            next.avgTotalMs = ParseFloatOr(values, L"avg_total_ms");
            next.maxTotalMs = ParseFloatOr(values, L"max_total_ms");
            next.submitError = ParseIntOr(values, L"submit_error");
        }
    }
    g_perfSnapshot = next;
    if (g_hwnd) {
        // The performance timer runs every 500 ms. Repainting the entire top-level
        // window here made every owner-drawn card/button visibly flash even though
        // only the performance card had changed. Keep the refresh local.
        if (wasRunning != g_coreRunning) {
            if (HWND stop = GetDlgItem(g_hwnd, IDC_STOP_VR))
                InvalidateRect(stop, nullptr, FALSE);
        }
        if (g_perfPaintRect.right > g_perfPaintRect.left &&
            g_perfPaintRect.bottom > g_perfPaintRect.top) {
            InvalidateRect(g_hwnd, &g_perfPaintRect, FALSE);
        }
    }
}

void OpenPerfReport() {
    const fs::path report = PerfReportPath();
    if (fs::exists(report)) {
        std::wstring arg = L"/select," + Quote(report.wstring());
        ShellExecuteW(g_hwnd, L"open", L"explorer.exe", arg.c_str(), nullptr, SW_SHOWNORMAL);
        return;
    }
    const fs::path dir = report.parent_path();
    std::error_code ec;
    fs::create_directories(dir, ec);
    OpenFolder(dir);
    MessageBoxW(g_hwnd, L"目前還沒有效能報告。啟動 VR 鍵盤約 1 秒後就會開始產生 CSV。", L"效能報告", MB_ICONINFORMATION);
}

void SetBusy(bool busy) {
    g_busy = busy;
    for (HWND button : g_taskButtons) {
        EnableWindow(button, busy ? FALSE : TRUE);
        InvalidateRect(button, nullptr, TRUE);
    }
    if (g_hwnd) {
        if (HWND update = GetDlgItem(g_hwnd, IDC_UPDATE)) {
            EnableWindow(update, busy ? FALSE : TRUE);
            InvalidateRect(update, nullptr, TRUE);
        }
    }
}

void RefreshStatus() {
    g_coreOutdated = IsCoreBuildOutdated();
    g_coreReady = fs::exists(CoreExePath()) && fs::exists(OpenVrDllPath()) && !g_coreOutdated;
    g_cmakeReady = !FindCMake().empty();
    g_gitReady = !FindGit().empty();
    RefreshPerfSnapshot();
    if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE);
}

int RunProcessCapture(const fs::path& exe, const std::vector<std::wstring>& args, const fs::path& cwd,
                      std::wstring* captured = nullptr, bool echoOutput = true) {
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) return (int)GetLastError();
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    std::wstring command = Quote(exe.wstring());
    for (const auto& arg : args) {
        command += L" ";
        command += Quote(arg);
    }
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = writePipe;
    si.hStdError = writePipe;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};

    BOOL ok = CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW, nullptr, cwd.wstring().c_str(), &si, &pi);
    CloseHandle(writePipe);
    if (!ok) {
        CloseHandle(readPipe);
        PostLog(L"[錯誤] 無法啟動程序，錯誤碼 " + std::to_wstring(GetLastError()) + L"\r\n");
        return (int)GetLastError();
    }

    char buffer[4096];
    DWORD read = 0;
    while (ReadFile(readPipe, buffer, sizeof(buffer), &read, nullptr) && read > 0) {
        const std::wstring chunk = Utf8ToWide(std::string(buffer, buffer + read));
        if (captured) *captured += chunk;
        if (echoOutput) PostLog(chunk);
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(readPipe);
    return (int)exitCode;
}

bool BootstrapGitRepository(const fs::path& git, std::wstring* error = nullptr) {
    if (fs::exists(g_root / L".git")) return true;

    auto fail = [&](const std::wstring& message) {
        if (error) *error = message;
        PostLog(L"[Git 修復失敗] " + message + L"\r\n");
        return false;
    };
    auto run = [&](const std::vector<std::wstring>& args, std::wstring* output = nullptr, bool echo = true) {
        if (output) output->clear();
        return RunProcessCapture(git, args, g_root, output, echo);
    };

    const std::wstring remote = L"https://github.com/" + Utf8ToWide(VRFK_GITHUB_OWNER) +
                                L"/" + Utf8ToWide(VRFK_GITHUB_REPO) + L".git";
    PostLog(L"[Git 修復] 找不到 .git，開始重建 Repository metadata。\r\n");
    PostLog(L"[Git 修復] 不會覆蓋目前 Source；只重建 Git metadata 並對齊 origin/main。\r\n");

    if (run({L"init"}) != 0) return fail(L"git init 失敗。");
    if (run({L"remote", L"add", L"origin", remote}) != 0) {
        std::wstring currentRemote;
        if (run({L"remote", L"get-url", L"origin"}, &currentRemote, false) != 0) return fail(L"無法建立 origin remote。");
        currentRemote = TrimWide(currentRemote);
        if (_wcsicmp(currentRemote.c_str(), remote.c_str()) != 0 &&
            run({L"remote", L"set-url", L"origin", remote}) != 0) return fail(L"無法修正 origin remote URL。");
    }

    if (run({L"fetch", L"origin", L"main", L"--tags"}) != 0) return fail(L"無法從 GitHub 取得 origin/main。請確認網路與 GitHub 權限。");
    if (run({L"update-ref", L"refs/heads/main", L"refs/remotes/origin/main"}) != 0) return fail(L"無法建立本機 main 分支。");
    if (run({L"symbolic-ref", L"HEAD", L"refs/heads/main"}) != 0) return fail(L"無法將 HEAD 指向 main。");
    if (run({L"reset", L"--mixed", L"HEAD"}) != 0) return fail(L"無法將 Git index 對齊 origin/main。");
    run({L"branch", L"--set-upstream-to=origin/main", L"main"}, nullptr, false);

    PostLog(L"[Git 修復完成] Repository metadata 已重建；目前 Source 完整保留。\r\n");
    return true;
}

std::wstring NormalizePathForCompare(const fs::path& path) {
    std::error_code ec;
    fs::path normalized = fs::weakly_canonical(path, ec);
    if (ec) {
        ec.clear();
        normalized = fs::absolute(path, ec);
        if (ec) normalized = path;
    }
    std::wstring text = normalized.lexically_normal().wstring();
    std::replace(text.begin(), text.end(), L'/', L'\\');
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t ch) { return (wchar_t)towlower(ch); });
    while (text.size() > 3 && (text.back() == L'\\' || text.back() == L'/')) text.pop_back();
    return text;
}

bool RemoveStaleBuildCacheIfNeeded() {
    const fs::path buildDir = g_root / L"build";
    const fs::path cachePath = buildDir / L"CMakeCache.txt";
    if (!fs::exists(cachePath)) return true;

    const std::wstring cache = ReadTextFile(cachePath);
    const std::wstring key = L"CMAKE_HOME_DIRECTORY:INTERNAL=";
    const size_t pos = cache.find(key);
    if (pos == std::wstring::npos) return true;

    const size_t valueStart = pos + key.size();
    size_t valueEnd = cache.find_first_of(L"\r\n", valueStart);
    if (valueEnd == std::wstring::npos) valueEnd = cache.size();
    const std::wstring cachedSource = cache.substr(valueStart, valueEnd - valueStart);
    if (cachedSource.empty()) return true;

    if (NormalizePathForCompare(fs::path(cachedSource)) == NormalizePathForCompare(g_root)) return true;

    PostLog(L"[自動修復] 偵測到 build/CMakeCache.txt 屬於其他專案路徑：\r\n");
    PostLog(L"  舊路徑：" + cachedSource + L"\r\n");
    PostLog(L"  目前路徑：" + g_root.wstring() + L"\r\n");
    PostLog(L"[自動修復] 正在清除舊建置快取並重新設定...\r\n");

    std::error_code ec;
    fs::remove_all(buildDir, ec);
    if (ec) {
        PostLog(L"[錯誤] 無法清除舊 build 資料夾，請關閉可能正在使用它的程式後再試一次。\r\n");
        return false;
    }
    return true;
}

bool ConfigureProject(const fs::path& cmake) {
    PostLog(L"\r\n[1/2] 設定 CMake 專案...\r\n");
    if (!RemoveStaleBuildCacheIfNeeded()) return false;
    return RunProcessCapture(cmake, {
        L"-S", g_root.wstring(),
        L"-B", (g_root / L"build").wstring(),
        L"-G", L"Visual Studio 17 2022",
        L"-A", L"x64",
        L"-DVRFK_BUILD_CORE=ON"
    }, g_root) == 0;
}

bool BuildCore(const fs::path& cmake) {
    if (!ConfigureProject(cmake)) return false;
    PostLog(L"\r\n[2/2] 建置 VR Full Keyboard...\r\n");
    const int result = RunProcessCapture(cmake, {
        L"--build", (g_root / L"build").wstring(),
        L"--config", L"Release",
        L"--target", L"VRFullKeyboard", L"VRFullKeyboardUpdater",
        L"--parallel"
    }, g_root);
    if (result == 0) {
        SetFileAttributesW((g_root / L"build").wstring().c_str(), FILE_ATTRIBUTE_HIDDEN);
    }
    return result == 0 && fs::exists(CoreExePath()) && fs::exists(OpenVrDllPath());
}

std::wstring Sha256File(const fs::path& path) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objLen = 0, cb = 0, hashLen = 0;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) return {};
    if (BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objLen), sizeof(objLen), &cb, 0) != 0 ||
        BCryptGetProperty(alg, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hashLen), sizeof(hashLen), &cb, 0) != 0) {
        BCryptCloseAlgorithmProvider(alg, 0);
        return {};
    }
    std::vector<UCHAR> object(objLen);
    std::vector<UCHAR> digest(hashLen);
    if (BCryptCreateHash(alg, &hash, object.data(), objLen, nullptr, 0, 0) != 0) {
        BCryptCloseAlgorithmProvider(alg, 0);
        return {};
    }
    std::ifstream input(path, std::ios::binary);
    std::vector<char> buffer(1024 * 1024);
    while (input) {
        input.read(buffer.data(), (std::streamsize)buffer.size());
        std::streamsize count = input.gcount();
        if (count > 0) BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()), (ULONG)count, 0);
    }
    const NTSTATUS status = BCryptFinishHash(hash, digest.data(), hashLen, 0);
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(alg, 0);
    if (status != 0) return {};
    std::wostringstream out;
    out << std::hex << std::setfill(L'0');
    for (UCHAR byte : digest) out << std::setw(2) << (unsigned int)byte;
    return out.str();
}


std::string TrimAscii(std::string value) {
    auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    return value;
}

void AppendUtf8Codepoint(std::string& out, unsigned int cp) {
    if (cp <= 0x7F) out.push_back(static_cast<char>(cp));
    else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0x10FFFF) {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

int HexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

bool ExtractJsonStringAt(const std::string& json, const char* key, size_t start, std::string& out) {
    const std::string needle = std::string("\"") + key + "\"";
    size_t pos = json.find(needle, start);
    if (pos == std::string::npos) return false;
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return false;
    ++pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
    if (pos >= json.size() || json[pos] != '"') return false;
    ++pos;
    out.clear();
    while (pos < json.size()) {
        char c = json[pos++];
        if (c == '"') return true;
        if (c != '\\') { out.push_back(c); continue; }
        if (pos >= json.size()) return false;
        const char esc = json[pos++];
        switch (esc) {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'u': {
                if (pos + 4 > json.size()) return false;
                unsigned int cp = 0;
                for (int i = 0; i < 4; ++i) {
                    const int h = HexNibble(json[pos + i]);
                    if (h < 0) return false;
                    cp = (cp << 4) | static_cast<unsigned int>(h);
                }
                pos += 4;
                AppendUtf8Codepoint(out, cp);
                break;
            }
            default: return false;
        }
    }
    return false;
}

bool ExtractJsonString(const std::string& json, const char* key, std::string& out) {
    return ExtractJsonStringAt(json, key, 0, out);
}

bool ParseSemVer(const std::string& raw, std::array<int, 3>& out) {
    std::string value = TrimAscii(raw);
    if (!value.empty() && (value[0] == 'v' || value[0] == 'V')) value.erase(value.begin());
    const size_t suffix = value.find_first_of("-+");
    if (suffix != std::string::npos) value.resize(suffix);
    size_t start = 0;
    for (int i = 0; i < 3; ++i) {
        const size_t dot = value.find('.', start);
        const size_t end = (i == 2) ? value.size() : dot;
        if (end == std::string::npos || end <= start) return false;
        const std::string part = value.substr(start, end - start);
        if (!std::all_of(part.begin(), part.end(), [](unsigned char c) { return std::isdigit(c) != 0; })) return false;
        try { out[static_cast<size_t>(i)] = std::stoi(part); } catch (...) { return false; }
        if (i < 2) {
            if (dot == std::string::npos) return false;
            start = dot + 1;
        }
    }
    return true;
}

int CompareSemVer(const std::string& lhs, const std::string& rhs) {
    std::array<int, 3> a{}, b{};
    if (!ParseSemVer(lhs, a) || !ParseSemVer(rhs, b)) return 0;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i] < b[i]) return -1;
        if (a[i] > b[i]) return 1;
    }
    return 0;
}

bool HttpGetUrl(const std::wstring& url, std::vector<char>& data, DWORD& statusCode,
                std::wstring& error, bool githubApi = false,
                const std::function<void(size_t, size_t)>& progress = {}) {
    data.clear();
    statusCode = 0;
    error.clear();

    URL_COMPONENTSW parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwSchemeLength = static_cast<DWORD>(-1);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts)) {
        error = L"無法解析下載網址。";
        return false;
    }
    if (parts.nScheme != INTERNET_SCHEME_HTTPS) {
        error = L"更新器只允許 HTTPS 下載。";
        return false;
    }

    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.dwExtraInfoLength) path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    if (path.empty()) path = L"/";

    const std::wstring userAgent = L"VRFullKeyboard/" + Utf8ToWide(VRFK_VERSION_STRING);
    HINTERNET session = WinHttpOpen(userAgent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) { error = L"WinHTTP 初始化失敗。"; return false; }
    WinHttpSetTimeouts(session, 6000, 6000, 15000, 15000);

    HINTERNET connection = WinHttpConnect(session, host.c_str(), parts.nPort, 0);
    if (!connection) {
        error = L"無法連線更新伺服器。";
        WinHttpCloseHandle(session);
        return false;
    }
    HINTERNET request = WinHttpOpenRequest(connection, L"GET", path.c_str(), nullptr,
                                           WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           WINHTTP_FLAG_SECURE);
    if (!request) {
        error = L"無法建立更新請求。";
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return false;
    }
    if (githubApi) {
        static constexpr wchar_t headers[] =
            L"Accept: application/vnd.github+json\r\n"
            L"X-GitHub-Api-Version: 2026-03-10\r\n";
        WinHttpAddRequestHeaders(request, headers, -1L, WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
    }

    const bool sent = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                         WINHTTP_NO_REQUEST_DATA, 0, 0, 0) != FALSE;
    const bool received = sent && WinHttpReceiveResponse(request, nullptr) != FALSE;
    if (!received) {
        error = L"下載請求失敗，錯誤碼 " + std::to_wstring(GetLastError());
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return false;
    }

    DWORD statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);
    if (statusCode < 200 || statusCode >= 300) {
        error = L"伺服器回傳 HTTP " + std::to_wstring(statusCode);
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return false;
    }

    size_t contentLength = 0;
    wchar_t contentLengthText[64]{};
    DWORD contentLengthSize = sizeof(contentLengthText);
    if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX,
                            contentLengthText, &contentLengthSize, WINHTTP_NO_HEADER_INDEX)) {
        contentLength = static_cast<size_t>(_wcstoui64(contentLengthText, nullptr, 10));
    }
    if (progress) progress(0, contentLength);

    constexpr size_t kMaxDownload = 256ull * 1024ull * 1024ull;
    while (data.size() < kMaxDownload) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available)) {
            error = L"讀取下載資料失敗。";
            break;
        }
        if (!available) break;
        const size_t room = kMaxDownload - data.size();
        const DWORD want = static_cast<DWORD>((std::min)(room, static_cast<size_t>(available)));
        const size_t old = data.size();
        data.resize(old + want);
        DWORD read = 0;
        if (!WinHttpReadData(request, data.data() + old, want, &read)) {
            data.resize(old);
            error = L"下載資料失敗。";
            break;
        }
        data.resize(old + read);
        if (progress) progress(data.size(), contentLength);
        if (!read) break;
    }
    if (data.size() >= kMaxDownload && error.empty()) error = L"下載檔案超過安全大小限制。";

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return error.empty();
}

bool DownloadToFile(const std::string& urlUtf8, const fs::path& destination, std::wstring& error,
                    const std::function<void(size_t, size_t)>& progress = {}) {
    std::vector<char> data;
    DWORD status = 0;
    if (!HttpGetUrl(Utf8ToWide(urlUtf8), data, status, error, false, progress)) return false;
    std::error_code ec;
    fs::create_directories(destination.parent_path(), ec);
    std::ofstream out(destination, std::ios::binary | std::ios::trunc);
    if (!out) { error = L"無法建立下載檔案。"; return false; }
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    if (!out) { error = L"寫入下載檔案失敗。"; return false; }
    return true;
}

bool FindAssetUrl(const std::string& json, const std::string& assetName, std::string& url) {
    size_t pos = 0;
    while ((pos = json.find("\"browser_download_url\"", pos)) != std::string::npos) {
        std::string candidate;
        if (!ExtractJsonStringAt(json, "browser_download_url", pos, candidate)) return false;
        if (candidate.size() >= assetName.size() &&
            candidate.compare(candidate.size() - assetName.size(), assetName.size(), assetName) == 0) {
            url = std::move(candidate);
            return true;
        }
        ++pos;
    }
    return false;
}

UpdateReleaseInfo FetchLatestRelease() {
    UpdateReleaseInfo info;
    const std::wstring api = L"https://api.github.com/repos/" + Utf8ToWide(VRFK_GITHUB_OWNER) +
                             L"/" + Utf8ToWide(VRFK_GITHUB_REPO) + L"/releases/latest";
    std::vector<char> bytes;
    DWORD status = 0;
    if (!HttpGetUrl(api, bytes, status, info.error, true)) return info;
    const std::string json(bytes.begin(), bytes.end());

    if (!ExtractJsonString(json, "tag_name", info.tag)) {
        info.error = L"GitHub Release 缺少版本標籤。";
        return info;
    }
    std::array<int, 3> parsed{};
    if (!ParseSemVer(info.tag, parsed)) {
        info.error = L"GitHub Release 版本格式不是 vMAJOR.MINOR.PATCH。";
        return info;
    }
    ExtractJsonString(json, "name", info.name);
    ExtractJsonString(json, "body", info.notes);
    ExtractJsonString(json, "html_url", info.pageUrl);
    if (!FindAssetUrl(json, "VRFullKeyboard_Windows_x64.zip", info.zipUrl) ||
        !FindAssetUrl(json, "VRFullKeyboard_Windows_x64.sha256", info.shaUrl)) {
        info.error = L"最新 Release 缺少自動更新需要的 ZIP 或 SHA256 資產。";
        return info;
    }
    if (info.notes.size() > 2200) {
        info.notes.resize(2200);
        info.notes += "\n...";
    }
    info.updateAvailable = CompareSemVer(info.tag, VRFK_VERSION_STRING) > 0;
    info.success = true;
    return info;
}

std::wstring ExpectedSha256(const fs::path& shaFile) {
    std::ifstream in(shaFile, std::ios::binary);
    if (!in) return {};
    std::string token;
    in >> token;
    if (token.size() != 64) return {};
    if (!std::all_of(token.begin(), token.end(), [](unsigned char c) { return std::isxdigit(c) != 0; })) return {};
    std::transform(token.begin(), token.end(), token.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return Utf8ToWide(token);
}

std::wstring EscapePowerShellSingleQuoted(const std::wstring& value) {
    std::wstring out;
    out.reserve(value.size() + 8);
    for (wchar_t ch : value) {
        if (ch == L'\'') out += L"''";
        else out += ch;
    }
    return out;
}


fs::path UpdateBasePath() {
    const std::wstring local = EnvValue(L"LOCALAPPDATA");
    fs::path base = local.empty() ? g_root : fs::path(local);
    return base / L"VRFullKeyboard" / L"updates";
}

void CleanupStaleUpdateDirectories(const fs::path& keep) {
    const fs::path base = UpdateBasePath();
    std::error_code ec;
    fs::create_directories(base, ec);
    ec.clear();
    for (fs::directory_iterator it(base, ec), end; !ec && it != end; it.increment(ec)) {
        const fs::path p = it->path();
        if (p == keep || p.filename() == L"backup_previous") continue;
        if (it->is_directory(ec)) fs::remove_all(p, ec);
        else fs::remove(p, ec);
        ec.clear();
    }
}

bool LaunchUpdaterProcess(const fs::path& updater, const fs::path& installDir,
                          const fs::path& stageDir, const fs::path& backupDir,
                          const std::wstring& fromVersion, const std::wstring& toVersion) {
    const DWORD parentPid = GetCurrentProcessId();
    std::wstring command = Quote(updater.wstring()) +
        L" --install-dir " + Quote(installDir.wstring()) +
        L" --stage-dir " + Quote(stageDir.wstring()) +
        L" --backup-dir " + Quote(backupDir.wstring()) +
        L" --parent-pid " + std::to_wstring(parentPid) +
        L" --relaunch " + Quote(L"VRFullKeyboardControl.exe") +
        L" --from-version " + Quote(fromVersion) +
        L" --to-version " + Quote(toVersion);
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    const BOOL ok = CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE,
                                   0, nullptr, stageDir.wstring().c_str(), &si, &pi);
    if (ok) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    return ok != FALSE;
}

bool DownloadAndStartUpdate(const UpdateReleaseInfo& info, std::wstring& error) {
    const fs::path updateRoot = UpdateBasePath() / Utf8ToWide(info.tag);
    const fs::path zip = updateRoot / L"VRFullKeyboard_Windows_x64.zip";
    const fs::path sha = updateRoot / L"VRFullKeyboard_Windows_x64.sha256";
    const fs::path stage = updateRoot / L"stage";
    const fs::path backup = UpdateBasePath() / L"backup_previous";

    CleanupStaleUpdateDirectories(updateRoot);
    std::error_code ec;
    fs::remove_all(updateRoot, ec);
    ec.clear();
    fs::create_directories(updateRoot, ec);
    if (ec) { error = L"無法建立更新暫存資料夾。"; return false; }

    PostLog(L"[更新] 下載 " + Utf8ToWide(info.tag) + L"...\r\n");
    PostUpdateProgress(L"下載更新檔 0%");
    int lastPercent = -5;
    if (!DownloadToFile(info.zipUrl, zip, error, [&](size_t current, size_t total) {
            if (total == 0) return;
            const int percent = static_cast<int>((current * 100ull) / total);
            if (percent >= lastPercent + 5 || percent == 100) {
                lastPercent = percent;
                PostUpdateProgress(L"下載更新檔 " + std::to_wstring(percent) + L"%");
            }
        })) return false;
    PostUpdateProgress(L"下載驗證檔…");
    if (!DownloadToFile(info.shaUrl, sha, error)) return false;

    PostUpdateProgress(L"驗證 SHA256…");
    const std::wstring expected = ExpectedSha256(sha);
    const std::wstring actual = Sha256File(zip);
    if (expected.empty() || actual.empty() || _wcsicmp(expected.c_str(), actual.c_str()) != 0) {
        error = L"SHA256 驗證失敗。更新已中止，現有版本不會被修改。";
        return false;
    }
    PostLog(L"[更新] SHA256 驗證成功。\r\n");
    PostUpdateProgress(L"解壓更新檔…");

    const fs::path powershell = SearchProgram(L"powershell.exe");
    if (powershell.empty()) { error = L"找不到 Windows PowerShell，無法解壓更新。"; return false; }
    fs::remove_all(stage, ec);
    ec.clear();
    fs::create_directories(stage, ec);
    const std::wstring command =
        L"Expand-Archive -LiteralPath '" + EscapePowerShellSingleQuoted(zip.wstring()) +
        L"' -DestinationPath '" + EscapePowerShellSingleQuoted(stage.wstring()) + L"' -Force";
    if (RunProcessCapture(powershell, {L"-NoProfile", L"-ExecutionPolicy", L"Bypass", L"-Command", command}, g_root) != 0) {
        error = L"解壓更新失敗。";
        return false;
    }

    const std::vector<fs::path> required = {
        stage / L"VRFullKeyboardControl.exe",
        stage / L"VRFullKeyboard.exe",
        stage / L"VRFullKeyboardUpdater.exe",
        stage / L"openvr_api.dll",
        stage / L"VERSION.txt"
    };
    for (const auto& file : required) {
        if (!fs::exists(file)) {
            error = L"更新包內容不完整：" + file.filename().wstring();
            return false;
        }
    }
    const std::string stagedVersion = TrimAscii(WideToUtf8(ReadTextFile(stage / L"VERSION.txt")));
    if (stagedVersion.empty() || CompareSemVer(stagedVersion, info.tag) != 0) {
        error = L"更新包版本與 GitHub Release 標籤不一致。";
        return false;
    }

    PostUpdateProgress(L"準備安裝並重新啟動…");
    if (!LaunchUpdaterProcess(stage / L"VRFullKeyboardUpdater.exe", g_root, stage, backup,
                              Utf8ToWide(VRFK_VERSION_STRING), Utf8ToWide(info.tag))) {
        error = L"無法啟動更新器。";
        return false;
    }
    return true;
}

void StartUpdateInstallTask(UpdateReleaseInfo info) {
    if (g_busy.exchange(true)) return;
    if (g_developerMode) SetLogExpanded(true);
    SetBusy(true);
    AppendLog(L"\r\n==============================\r\n下載並安裝更新\r\n==============================\r\n");
    std::thread([info = std::move(info)]() {
        std::wstring error;
        const bool ok = DownloadAndStartUpdate(info, error);
        if (ok) {
            if (IsWindow(g_hwnd)) PostMessageW(g_hwnd, WM_APP_UPDATE_EXIT, 0, 0);
        } else {
            auto* done = new TaskDone();
            done->success = false;
            done->summary = error.empty() ? L"更新失敗。" : error;
            if (IsWindow(g_hwnd)) PostMessageW(g_hwnd, WM_APP_TASK_DONE, 0, reinterpret_cast<LPARAM>(done));
            else delete done;
        }
    }).detach();
}

void StartUpdateCheckTask(bool silent = false) {
    if (g_updateCheckBusy.exchange(true)) return;
    if (!silent) {
        if (g_busy.exchange(true)) {
            g_updateCheckBusy = false;
            return;
        }
        if (g_developerMode) SetLogExpanded(true);
        SetBusy(true);
        AppendLog(L"\r\n[更新] 正在檢查 GitHub Release...\r\n");
        g_updateButtonSubtitle = L"正在檢查 GitHub…";
        if (HWND update = GetDlgItem(g_hwnd, IDC_UPDATE)) InvalidateRect(update, nullptr, TRUE);
    }
    std::thread([silent]() {
        UpdateReleaseInfo info = FetchLatestRelease();
        info.silent = silent;
        auto* heap = new UpdateReleaseInfo(std::move(info));
        if (IsWindow(g_hwnd)) PostMessageW(g_hwnd, WM_APP_UPDATE_CHECK_DONE, 0, reinterpret_cast<LPARAM>(heap));
        else delete heap;
    }).detach();
}

void StartRollbackSelfTest() {
    if (g_busy.exchange(true)) return;
    const fs::path updater = UpdaterExePath();
    if (!fs::exists(updater)) {
        g_busy = false;
        MessageBoxW(g_hwnd, L"找不到 VRFullKeyboardUpdater.exe。\n請先按「建置最新版」。",
                    L"更新可靠性測試", MB_ICONWARNING | MB_OK);
        return;
    }
    SetBusy(true);
    if (g_developerMode) SetLogExpanded(true);
    AppendLog(L"\r\n==============================\r\n更新回復機制自我測試\r\n==============================\r\n");
    const fs::path testRoot = UpdateBasePath() / L"rollback_self_test";
    std::thread([updater, testRoot]() {
        const int code = RunProcessCapture(updater, {L"--self-test-dir", testRoot.wstring()}, g_root);
        auto* done = new TaskDone();
        done->success = (code == 0);
        done->summary = done->success
            ? L"Rollback 自我測試通過：備份、還原、新增檔案清理與 INI 保護皆正常。"
            : L"Rollback 自我測試失敗，請查看完整建置記錄。";
        if (IsWindow(g_hwnd)) PostMessageW(g_hwnd, WM_APP_TASK_DONE, 0, reinterpret_cast<LPARAM>(done));
        else delete done;
    }).detach();
}

bool CreateReleasePackage(const fs::path& cmake) {
    if (!BuildCore(cmake)) return false;

    const fs::path distRoot = g_root / L"dist";
    const fs::path stage = distRoot / L"VRFullKeyboard_Windows_x64";
    const fs::path zip = distRoot / L"VRFullKeyboard_Windows_x64.zip";
    const fs::path hashFile = distRoot / L"VRFullKeyboard_Windows_x64.sha256";

    std::error_code ec;
    fs::remove_all(stage, ec);
    fs::create_directories(stage, ec);
    fs::remove(zip, ec);
    fs::remove(hashFile, ec);

    auto copy = [&](const fs::path& from, const fs::path& to) -> bool {
        std::error_code copyEc;
        fs::copy_file(from, to, fs::copy_options::overwrite_existing, copyEc);
        if (copyEc) {
            PostLog(L"[錯誤] 無法複製：" + from.wstring() + L"\r\n");
            return false;
        }
        return true;
    };

    if (!copy(CoreExePath(), stage / L"VRFullKeyboard.exe")) return false;
    if (!copy(OpenVrDllPath(), stage / L"openvr_api.dll")) return false;
    if (!copy(ModulePath(), stage / L"VRFullKeyboardControl.exe")) return false;
    if (!copy(UpdaterExePath(), stage / L"VRFullKeyboardUpdater.exe")) return false;
    if (!copy(g_root / L"release_files" / L"README.txt", stage / L"README.txt")) return false;
    if (!copy(g_root / L"VERSION", stage / L"VERSION.txt")) return false;

    // User settings are deliberately not copied into release packages.
    PostLog(L"\r\n[封裝] 建立一般使用者 ZIP...\r\n");
    const fs::path powershell = SearchProgram(L"powershell.exe");
    if (powershell.empty()) {
        PostLog(L"[錯誤] 找不到 Windows PowerShell。\r\n");
        return false;
    }
    const std::wstring command =
        L"Compress-Archive -Path '" + EscapePowerShellSingleQuoted((stage / L"*").wstring()) +
        L"' -DestinationPath '" + EscapePowerShellSingleQuoted(zip.wstring()) + L"' -Force";
    if (RunProcessCapture(powershell, {L"-NoProfile", L"-ExecutionPolicy", L"Bypass", L"-Command", command}, g_root) != 0) return false;

    const std::wstring hash = Sha256File(zip);
    if (hash.empty()) {
        PostLog(L"[錯誤] SHA256 計算失敗。\r\n");
        return false;
    }
    std::ofstream out(hashFile, std::ios::binary);
    const std::string line = WideToUtf8(hash) + "  VRFullKeyboard_Windows_x64.zip\r\n";
    out.write(line.data(), (std::streamsize)line.size());
    out.close();
    PostLog(L"[完成] " + zip.wstring() + L"\r\n");
    PostLog(L"[完成] SHA256 已建立。\r\n");
    return true;
}

void LaunchCore(PendingLaunch mode) {
    const fs::path exe = CoreExePath();
    if (!fs::exists(exe)) {
        if (g_developerMode && !g_busy) {
            if (MessageBoxW(g_hwnd, L"主程式尚未建置。\n\n要現在自動建置嗎？", L"VR Full Keyboard 控制中心", MB_ICONQUESTION | MB_YESNO) == IDYES) {
                PostMessageW(g_hwnd, WM_COMMAND, MAKEWPARAM(IDC_BUILD, BN_CLICKED), static_cast<LPARAM>((int)mode));
            }
        } else {
            MessageBoxW(g_hwnd, L"找不到 VRFullKeyboard.exe。", L"無法啟動", MB_ICONERROR);
        }
        return;
    }
    if (g_developerMode && !g_busy) {
        std::wstring newerSource;
        if (IsCoreBuildOutdated(&newerSource)) {
            std::wstring message = L"偵測到 Source 已更新，但目前 Core EXE 仍是較舊的建置。\n\n";
            if (!newerSource.empty()) message += L"較新的檔案：" + newerSource + L"\n\n";
            message += L"要現在重新建置，完成後自動開啟嗎？";
            if (MessageBoxW(g_hwnd, message.c_str(), L"Core EXE 已過期", MB_ICONWARNING | MB_YESNO) == IDYES) {
                AppendLog(L"[建置] 偵測到 Source 新於 Core EXE，自動要求重新建置。\r\n");
                PostMessageW(g_hwnd, WM_COMMAND, MAKEWPARAM(IDC_BUILD, BN_CLICKED), static_cast<LPARAM>((int)mode));
            }
            return;
        }
    }
    std::wstring params;
    if (mode == PendingLaunch::Preview) params = L"--preview";
    else if (mode == PendingLaunch::Editor) params = L"--preview --editor";
    else if (mode == PendingLaunch::Update) params = L"--preview --update";
    HINSTANCE result = ShellExecuteW(g_hwnd, L"open", exe.wstring().c_str(), params.empty() ? nullptr : params.c_str(), exe.parent_path().wstring().c_str(), SW_SHOWNORMAL);
    if ((INT_PTR)result <= 32) MessageBoxW(g_hwnd, L"啟動失敗。", L"VR Full Keyboard", MB_ICONERROR);
}

void StartBuildTask(PendingLaunch pending = PendingLaunch::None) {
    if (g_busy.exchange(true)) return;
    SetLogExpanded(true);
    SetBusy(true);
    AppendLog(L"\r\n==============================\r\n開始建置\r\n==============================\r\n");
    std::thread([pending]() {
        TaskDone done;
        done.pendingLaunch = (int)pending;
        const fs::path cmake = FindCMake();
        if (cmake.empty()) {
            done.summary = L"找不到 CMake。請先在 Visual Studio Installer 安裝 C++ CMake tools for Windows。";
        } else if (FindGit().empty()) {
            done.summary = L"找不到 Git。控制中心本身不需要 Git，但建置 VR 鍵盤核心需要 Git 取得 OpenVR / Dear ImGui。";
        } else {
            done.success = BuildCore(cmake);
            done.summary = done.success ? L"建置完成。" : L"建置失敗，請查看下方記錄。";
        }
        if (IsWindow(g_hwnd)) PostMessageW(g_hwnd, WM_APP_TASK_DONE, 0, reinterpret_cast<LPARAM>(new TaskDone(done)));
    }).detach();
}

void StartPackageTask() {
    if (g_busy.exchange(true)) return;
    SetLogExpanded(true);
    SetBusy(true);
    AppendLog(L"\r\n==============================\r\n建立分享版（一般使用者免建置）\r\n==============================\r\n");
    std::thread([]() {
        TaskDone done;
        const fs::path cmake = FindCMake();
        if (cmake.empty()) {
            done.summary = L"找不到 CMake。";
        } else if (FindGit().empty()) {
            done.summary = L"找不到 Git。建立分享版前需要先建置 VR 鍵盤核心。";
        } else {
            done.success = CreateReleasePackage(cmake);
            done.summary = done.success ? L"免建置分享版與 SHA256 已建立，可直接提供給一般使用者。" : L"建立分享版失敗，請查看下方記錄。";
        }
        if (IsWindow(g_hwnd)) PostMessageW(g_hwnd, WM_APP_TASK_DONE, 0, reinterpret_cast<LPARAM>(new TaskDone(done)));
    }).detach();
}

void StartPublishTask(const std::wstring& version) {
    if (!IsSemVer(version)) {
        MessageBoxW(g_hwnd, L"版本格式不正確。", L"無法發布", MB_ICONWARNING | MB_OK);
        return;
    }
    if (g_busy.exchange(true)) return;

    SetLogExpanded(true);
    SetBusy(true);
    const std::wstring tag = L"v" + version;
    AppendLog(L"\r\n==============================\r\n發布 GitHub 新版本 " + tag +
              L"\r\n==============================\r\n");

    std::thread([version, tag]() {
        TaskDone done;
        const fs::path git = FindGit();
        if (git.empty()) {
            done.summary = L"找不到 Git for Windows。";
            if (IsWindow(g_hwnd)) PostMessageW(g_hwnd, WM_APP_TASK_DONE, 0, reinterpret_cast<LPARAM>(new TaskDone(done)));
            return;
        }
        if (!fs::exists(g_root / L".git")) {
            std::wstring repairError;
            if (!BootstrapGitRepository(git, &repairError)) {
                done.summary = L"Git Repository metadata 自動修復失敗。\n\n" + repairError;
                if (IsWindow(g_hwnd)) PostMessageW(g_hwnd, WM_APP_TASK_DONE, 0, reinterpret_cast<LPARAM>(new TaskDone(done)));
                return;
            }
        }

        auto runGit = [&](const std::vector<std::wstring>& args, std::wstring* output = nullptr, bool echo = true) -> int {
            if (output) output->clear();
            return RunProcessCapture(git, args, g_root, output, echo);
        };
        auto finish = [&](bool success, const std::wstring& summary, bool offerActions = false) {
            done.success = success;
            done.offerActions = offerActions;
            done.summary = summary;
            if (IsWindow(g_hwnd)) PostMessageW(g_hwnd, WM_APP_TASK_DONE, 0, reinterpret_cast<LPARAM>(new TaskDone(done)));
        };

        PostLog(L"[1/7] 檢查 main 分支與 Tag...\r\n");
        std::wstring branch;
        if (runGit({L"branch", L"--show-current"}, &branch, false) != 0) {
            finish(false, L"無法讀取目前 Git 分支。");
            return;
        }
        branch = TrimWide(branch);
        if (branch != L"main") {
            finish(false, L"目前分支是「" + branch + L"」，請切換到 main 後再發布。");
            return;
        }
        if (runGit({L"fetch", L"origin", L"--tags"}) != 0) {
            finish(false, L"無法從 GitHub 同步 Tag。");
            return;
        }
        if (runGit({L"rev-parse", L"-q", L"--verify", L"refs/tags/" + tag}, nullptr, false) == 0) {
            finish(false, L"Tag " + tag + L" 已存在，請改用新的版本號。");
            return;
        }

        PostLog(L"\r\n[2/7] 暫存本機尚未提交的修改...\r\n");
        std::wstring status;
        if (runGit({L"status", L"--porcelain"}, &status, false) != 0) {
            finish(false, L"無法讀取 Git 工作目錄狀態。");
            return;
        }
        bool stashed = !TrimWide(status).empty();
        const std::wstring stashName = L"VRFK_RELEASE_TEMP_" + std::to_wstring(GetTickCount64());
        if (stashed) {
            PostLog(L"偵測到本機 Source 修改：\r\n" + status);
            if (runGit({L"stash", L"push", L"-u", L"-m", stashName}) != 0) {
                finish(false, L"暫存本機修改失敗；Source 沒有被發布。 ");
                return;
            }
            PostLog(L"本機修改已安全暫存。\r\n");
        } else {
            PostLog(L"目前沒有未提交修改。\r\n");
        }

        PostLog(L"\r\n[3/7] 同步 GitHub main...\r\n");
        if (runGit({L"pull", L"--rebase", L"origin", L"main"}) != 0) {
            if (stashed) {
                PostLog(L"同步失敗，正在還原暫存內容...\r\n");
                runGit({L"stash", L"pop"});
            }
            finish(false, L"git pull --rebase 失敗；已停止發布。請查看作業記錄。");
            return;
        }

        if (stashed) {
            PostLog(L"正在還原本機開發內容...\r\n");
            if (runGit({L"stash", L"pop"}) != 0) {
                finish(false, L"GitHub 最新內容與本機修改發生衝突。修改與 stash 仍由 Git 保留，請先解決衝突後再發布。");
                return;
            }
            stashed = false;
        }

        PostLog(L"\r\n[4/7] 設定 VERSION = " + version + L"...\r\n");
        if (!WriteVersionFile(version)) {
            finish(false, L"無法寫入 VERSION 檔案。");
            return;
        }

        status.clear();
        if (runGit({L"status", L"--porcelain"}, &status, false) != 0) {
            finish(false, L"無法取得發布前 Git 狀態。");
            return;
        }
        if (!TrimWide(status).empty()) {
            PostLog(L"即將提交：\r\n" + status);
            if (runGit({L"add", L"-A"}) != 0) {
                finish(false, L"git add 失敗；尚未建立 Release Commit。");
                return;
            }
            if (runGit({L"commit", L"-m", L"Release " + tag}) != 0) {
                finish(false, L"建立 Release Commit 失敗，請查看作業記錄。");
                return;
            }
        } else {
            PostLog(L"Source 沒有新的檔案差異，將目前 HEAD 直接發布為 " + tag + L"。\r\n");
        }

        std::wstring headVersion;
        if (runGit({L"show", L"HEAD:VERSION"}, &headVersion, false) != 0 || TrimWide(headVersion) != version) {
            finish(false, L"安全檢查失敗：HEAD 中的 VERSION 與要發布的版本不一致。");
            return;
        }

        PostLog(L"\r\n[5/7] Push main...\r\n");
        if (runGit({L"push", L"origin", L"main"}) != 0) {
            finish(false, L"Push main 失敗。Release Commit 仍保留在本機，可修正連線後重新發布。");
            return;
        }

        PostLog(L"\r\n[6/7] 建立並 Push " + tag + L"...\r\n");
        if (runGit({L"tag", L"-a", tag, L"-m", L"VR Full Keyboard " + tag}) != 0) {
            finish(false, L"建立 Tag 失敗。");
            return;
        }
        if (runGit({L"push", L"origin", tag}) != 0) {
            PostLog(L"Tag Push 失敗，移除本機 Tag 以便安全重試...\r\n");
            runGit({L"tag", L"-d", tag}, nullptr, false);
            finish(false, L"Push Tag 失敗；main 已推送，但本機 Tag 已移除，可直接重新發布同一版本。");
            return;
        }

        PostLog(L"\r\n[7/7] 完成。GitHub Actions 已接手建置 Release。\r\n");
        finish(true,
               tag + L" 已推送至 GitHub。\n\nGitHub Actions 接下來會自動建置 Windows 分享版、ZIP、SHA256 與 Release。\n控制中心下次啟動時會依新的 VERSION 自動重建。",
               true);
    }).detach();
}

void OpenFolder(const fs::path& path) {
    fs::path target = path;
    if (!fs::exists(target)) target = g_root;
    ShellExecuteW(g_hwnd, L"open", L"explorer.exe", Quote(target.wstring()).c_str(), nullptr, SW_SHOWNORMAL);
}

void OpenBuildLog() {
    const fs::path bootstrap = g_root / L".frontend" / L"bootstrap_build.log";
    if (g_developerMode && fs::exists(bootstrap)) {
        const std::wstring bootstrapText = ReadTextFile(bootstrap);
        if (ContainsBuildWarning(bootstrapText)) {
            ShellExecuteW(g_hwnd, L"open", L"notepad.exe", Quote(bootstrap.wstring()).c_str(), nullptr, SW_SHOWNORMAL);
            return;
        }
    }
    if (fs::exists(g_persistentLogPath)) {
        ShellExecuteW(g_hwnd, L"open", L"notepad.exe", Quote(g_persistentLogPath.wstring()).c_str(), nullptr, SW_SHOWNORMAL);
        return;
    }
    if (g_developerMode && fs::exists(bootstrap)) {
        ShellExecuteW(g_hwnd, L"open", L"notepad.exe", Quote(bootstrap.wstring()).c_str(), nullptr, SW_SHOWNORMAL);
        return;
    }
    MessageBoxW(g_hwnd, L"目前還沒有可查看的建置記錄。", L"VR Full Keyboard", MB_ICONINFORMATION);
}

void OpenOutput() {
    const fs::path exe = CoreExePath();
    if (fs::exists(exe)) {
        std::wstring arg = L"/select," + Quote(exe.wstring());
        ShellExecuteW(g_hwnd, L"open", L"explorer.exe", arg.c_str(), nullptr, SW_SHOWNORMAL);
    } else {
        OpenFolder(g_developerMode ? g_root / L"build" : g_root);
    }
}

void CleanBuildCache() {
    if (!g_developerMode) return;
    if (MessageBoxW(g_hwnd,
                    L"這會刪除 build 建置快取。\n\n不會刪除原始碼、設定檔或 dist 發行包。\n下次建置會重新下載／設定必要依賴。",
                    L"清除建置快取", MB_ICONWARNING | MB_YESNO) != IDYES) return;
    std::error_code ec;
    fs::remove_all(g_root / L"build", ec);
    if (ec) MessageBoxW(g_hwnd, L"部分建置檔案無法刪除，可能仍被其他程式使用。", L"清除未完成", MB_ICONWARNING);
    else AppendLog(L"\r\n[完成] build 建置快取已清除。\r\n");
    RefreshStatus();
}

bool CreateDesktopShortcut() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool uninit = SUCCEEDED(hr);
    IShellLinkW* link = nullptr;
    hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link));
    if (FAILED(hr) || !link) {
        if (uninit) CoUninitialize();
        return false;
    }
    const fs::path exe = ModulePath();
    link->SetPath(exe.wstring().c_str());
    link->SetWorkingDirectory(g_root.wstring().c_str());
    link->SetDescription(L"VR Full Keyboard 控制中心");

    PWSTR desktopRaw = nullptr;
    hr = SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &desktopRaw);
    if (SUCCEEDED(hr) && desktopRaw) {
        fs::path shortcut = fs::path(desktopRaw) / L"VR Full Keyboard 控制中心.lnk";
        IPersistFile* persist = nullptr;
        hr = link->QueryInterface(IID_PPV_ARGS(&persist));
        if (SUCCEEDED(hr) && persist) {
            hr = persist->Save(shortcut.wstring().c_str(), TRUE);
            persist->Release();
        }
        CoTaskMemFree(desktopRaw);
    }
    link->Release();
    if (uninit) CoUninitialize();
    return SUCCEEDED(hr);
}

enum class ButtonTone { Primary, Accent, Secondary, Utility, Danger };

struct ButtonVisual {
    const wchar_t* title;
    const wchar_t* subtitle;
    ButtonTone tone;
};

constexpr COLORREF kBg = RGB(12, 16, 24);
constexpr COLORREF kPanel = RGB(18, 24, 35);
constexpr COLORREF kCard = RGB(23, 30, 43);
constexpr COLORREF kCardHover = RGB(29, 38, 54);
constexpr COLORREF kBorder = RGB(48, 60, 79);
constexpr COLORREF kText = RGB(238, 243, 249);
constexpr COLORREF kMuted = RGB(151, 166, 188);
constexpr COLORREF kAccent = RGB(58, 139, 253);
constexpr COLORREF kAccentHover = RGB(76, 151, 255);
constexpr COLORREF kAccentPressed = RGB(42, 112, 214);
constexpr COLORREF kCyan = RGB(45, 191, 214);
constexpr COLORREF kGreen = RGB(79, 202, 126);
constexpr COLORREF kAmber = RGB(242, 177, 67);
constexpr COLORREF kRed = RGB(224, 92, 104);
constexpr COLORREF kLogBg = RGB(9, 13, 20);

int ScaleForDpi(int value, UINT dpi) {
    return MulDiv(value, (int)dpi, 96);
}

UINT WindowDpi(HWND hwnd) {
    using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
    static auto fn = reinterpret_cast<GetDpiForWindowFn>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    return fn ? fn(hwnd) : 96;
}

void RecreateFonts(HWND hwnd) {
    const UINT dpi = WindowDpi(hwnd);
    auto makeFont = [dpi](int points, int weight, const wchar_t* face) -> HFONT {
        return CreateFontW(-MulDiv(points, (int)dpi, 72), 0, 0, 0, weight, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH | FF_DONTCARE, face);
    };
    if (g_font) DeleteObject(g_font);
    if (g_smallFont) DeleteObject(g_smallFont);
    if (g_titleFont) DeleteObject(g_titleFont);
    if (g_logFont) DeleteObject(g_logFont);
    g_font = makeFont(11, FW_NORMAL, L"Microsoft JhengHei UI");
    g_smallFont = makeFont(9, FW_NORMAL, L"Microsoft JhengHei UI");
    g_titleFont = makeFont(24, FW_SEMIBOLD, L"Microsoft JhengHei UI");
    g_logFont = makeFont(10, FW_NORMAL, L"Cascadia Mono");
    if (!g_logFont) g_logFont = makeFont(10, FW_NORMAL, L"Consolas");
    for (HWND button : g_allButtons) SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
    if (g_log) SendMessageW(g_log, WM_SETFONT, reinterpret_cast<WPARAM>(g_logFont), TRUE);
}

ButtonVisual GetButtonVisual(int id) {
    switch (id) {
    case IDC_RUN_VR: return {L"啟動 VR 鍵盤", L"連接 SteamVR 並開啟鍵盤 Overlay", ButtonTone::Primary};
    case IDC_STOP_VR: return {L"關閉鍵盤", g_coreRunning ? L"立即結束目前測試中的鍵盤" : L"目前沒有執行中的鍵盤", ButtonTone::Danger};
    case IDC_PREVIEW: return {L"桌面預覽", L"不戴頭顯也能檢查鍵盤畫面", ButtonTone::Secondary};
    case IDC_EDITOR: return {L"編輯預覽", L"調整外觀、位置、快捷鍵與九方模式", ButtonTone::Secondary};
    case IDC_UPDATE: return {L"更新與版本", g_updateButtonSubtitle.c_str(), g_updateAvailableHint ? ButtonTone::Primary : ButtonTone::Accent};
    case IDC_BUILD: return {L"建置最新版", L"編譯 VRFullKeyboard.exe", ButtonTone::Secondary};
    case IDC_PACKAGE: return {L"建立分享版", L"自動建置並產生 ZIP 與 SHA256", ButtonTone::Primary};
    case IDC_PUBLISH_GITHUB: return {L"發布 GitHub 新版本", L"提交、推送、Tag，自動建立 Release", ButtonTone::Accent};
    case IDC_CLEAN: return {L"清除建置快取", L"刪除 build，不動設定與發行包", ButtonTone::Danger};
    case IDC_OPEN_OUTPUT: return {L"開啟程式位置", L"定位目前建置完成的核心程式", ButtonTone::Utility};
    case IDC_OPEN_ROOT: return {g_developerMode ? L"專案資料夾" : L"程式資料夾", L"在檔案總管開啟目前資料夾", ButtonTone::Utility};
    case IDC_OPEN_LOG: return {L"完整建置記錄", L"使用記事本查看完整 Log", ButtonTone::Utility};
    case IDC_ROLLBACK_TEST: return {L"測試更新回復", L"在隔離暫存區驗證備份與 Rollback", ButtonTone::Utility};
    case IDC_OPEN_PERF_REPORT: return {L"效能報告 CSV", L"開啟可直接回傳分析的效能記錄", ButtonTone::Utility};
    case IDC_SHORTCUT: return {L"建立桌面捷徑", L"從桌面直接開啟控制中心", ButtonTone::Utility};
    case IDC_LOG_TOGGLE: return {g_logExpanded ? L"收合記錄" : L"展開記錄", L"", ButtonTone::Utility};
    case IDC_LOG_CLEAR: return {L"清空畫面", L"", ButtonTone::Utility};
    case IDC_LOG_COPY: return {L"複製內容", L"", ButtonTone::Utility};
    default: return {L"", L"", ButtonTone::Secondary};
    }
}

COLORREF Blend(COLORREF a, COLORREF b, int percentB) {
    const int p = std::clamp(percentB, 0, 100);
    const int q = 100 - p;
    return RGB((GetRValue(a) * q + GetRValue(b) * p) / 100,
               (GetGValue(a) * q + GetGValue(b) * p) / 100,
               (GetBValue(a) * q + GetBValue(b) * p) / 100);
}

void FillRounded(HDC dc, const RECT& rc, int radius, COLORREF color, COLORREF border, int borderWidth = 1) {
    HBRUSH brush = CreateSolidBrush(color);
    HPEN pen = CreatePen(PS_SOLID, borderWidth, border);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, radius, radius);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

void DrawTextSimple(HDC dc, const std::wstring& text, RECT rc, HFONT font, COLORREF color, UINT format) {
    HGDIOBJ old = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, text.c_str(), (int)text.size(), &rc, format);
    SelectObject(dc, old);
}

void DrawPill(HDC dc, int& x, int y, const std::wstring& text, COLORREF accent, UINT dpi) {
    HGDIOBJ old = SelectObject(dc, g_smallFont);
    SIZE size{};
    GetTextExtentPoint32W(dc, text.c_str(), (int)text.size(), &size);
    SelectObject(dc, old);
    const int px = ScaleForDpi(10, dpi);
    const int py = ScaleForDpi(5, dpi);
    RECT r{x, y, x + size.cx + px * 2, y + size.cy + py * 2};
    FillRounded(dc, r, ScaleForDpi(14, dpi), Blend(kCard, accent, 16), Blend(kBorder, accent, 42));
    RECT tr{r.left + px, r.top + py - 1, r.right - px, r.bottom - py};
    DrawTextSimple(dc, text, tr, g_smallFont, accent, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
    x = r.right + ScaleForDpi(8, dpi);
}

LRESULT CALLBACK ButtonSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                    UINT_PTR, DWORD_PTR) {
    switch (msg) {
    case WM_MOUSEMOVE:
        if (!g_hoverButtons.contains(hwnd)) {
            g_hoverButtons.insert(hwnd);
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tme);
            InvalidateRect(hwnd, nullptr, TRUE);
        }
        break;
    case WM_MOUSELEAVE:
        g_hoverButtons.erase(hwnd);
        InvalidateRect(hwnd, nullptr, TRUE);
        break;
    case WM_SETCURSOR:
        SetCursor(LoadCursorW(nullptr, IDC_HAND));
        return TRUE;
    case WM_NCDESTROY:
        g_hoverButtons.erase(hwnd);
        RemoveWindowSubclass(hwnd, ButtonSubclassProc, 1);
        break;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

HWND MakeButton(HWND parent, int id, const wchar_t* text = L"") {
    const ButtonVisual visual = GetButtonVisual(id);
    const wchar_t* caption = (text && *text) ? text : visual.title;
    HWND h = CreateWindowExW(0, L"BUTTON", caption,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0, 0, 100, 40, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g_instance, nullptr);
    SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
    SetWindowSubclass(h, ButtonSubclassProc, 1, 0);
    g_allButtons.push_back(h);
    return h;
}

void CloseReleaseDialog(HWND hwnd) {
    HWND owner = GetWindow(hwnd, GW_OWNER);
    g_releaseDialog = nullptr;
    DestroyWindow(hwnd);
    if (owner && IsWindow(owner)) {
        EnableWindow(owner, TRUE);
        SetForegroundWindow(owner);
    }
}

LRESULT CALLBACK ReleaseDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        const UINT dpi = WindowDpi(g_hwnd ? g_hwnd : hwnd);
        BOOL dark = TRUE;
        constexpr DWORD kImmersiveDarkMode = 20;
        DwmSetWindowAttribute(hwnd, kImmersiveDarkMode, &dark, sizeof(dark));

        const int left = ScaleForDpi(24, dpi);
        const int width = ScaleForDpi(472, dpi);
        const int labelH = ScaleForDpi(24, dpi);

        HWND title = CreateWindowExW(0, L"STATIC", L"發布 GitHub 新版本",
            WS_CHILD | WS_VISIBLE, left, ScaleForDpi(22, dpi), width, labelH,
            hwnd, nullptr, g_instance, nullptr);
        SendMessageW(title, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);

        std::wstring current = TrimWide(ReadTextFile(g_root / L"VERSION"));
        if (current.empty()) current = Utf8ToWide(VRFK_VERSION_STRING);
        std::wstring currentText = L"目前 Source VERSION：" + current;
        HWND currentLabel = CreateWindowExW(0, L"STATIC", currentText.c_str(),
            WS_CHILD | WS_VISIBLE, left, ScaleForDpi(52, dpi), width, labelH,
            hwnd, nullptr, g_instance, nullptr);
        SendMessageW(currentLabel, WM_SETFONT, reinterpret_cast<WPARAM>(g_smallFont), TRUE);

        HWND versionLabel = CreateWindowExW(0, L"STATIC", L"要發布的版本號",
            WS_CHILD | WS_VISIBLE, left, ScaleForDpi(84, dpi), width, labelH,
            hwnd, nullptr, g_instance, nullptr);
        SendMessageW(versionLabel, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);

        HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", current.c_str(),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            left, ScaleForDpi(112, dpi), width, ScaleForDpi(34, dpi),
            hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_RELEASE_VERSION)), g_instance, nullptr);
        SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
        SendMessageW(edit, EM_SETSEL, 0, -1);

        HWND info = CreateWindowExW(0, L"STATIC",
            L"會自動暫存未提交修改、同步 GitHub main、還原修改、更新 VERSION、提交全部 Source、Push main、建立 Tag，再交給 GitHub Actions 建立 Release。",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            left, ScaleForDpi(158, dpi), width, ScaleForDpi(58, dpi),
            hwnd, nullptr, g_instance, nullptr);
        SendMessageW(info, WM_SETFONT, reinterpret_cast<WPARAM>(g_smallFont), TRUE);

        HWND confirm = CreateWindowExW(0, L"BUTTON", L"開始發布",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            left + ScaleForDpi(250, dpi), ScaleForDpi(232, dpi), ScaleForDpi(106, dpi), ScaleForDpi(36, dpi),
            hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_RELEASE_CONFIRM)), g_instance, nullptr);
        HWND cancel = CreateWindowExW(0, L"BUTTON", L"取消",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            left + ScaleForDpi(366, dpi), ScaleForDpi(232, dpi), ScaleForDpi(106, dpi), ScaleForDpi(36, dpi),
            hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_RELEASE_CANCEL)), g_instance, nullptr);
        SendMessageW(confirm, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
        SendMessageW(cancel, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);

        SetFocus(edit);
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_RELEASE_CONFIRM: {
            wchar_t buffer[64]{};
            GetDlgItemTextW(hwnd, IDC_RELEASE_VERSION, buffer, static_cast<int>(std::size(buffer)));
            const std::wstring version = TrimWide(buffer);
            if (!IsSemVer(version)) {
                MessageBoxW(hwnd, L"版本格式必須是 MAJOR.MINOR.PATCH，例如 3.9.8。",
                            L"版本格式不正確", MB_ICONWARNING | MB_OK);
                return 0;
            }
            const std::wstring tag = L"v" + version;
            std::wstring message = L"確定要將目前所有未提交的 Source 變更發布為 " + tag + L"？\n\n"
                                   L"控制中心會自動同步 main、Commit、Push、建立 Tag，接著由 GitHub Actions 建立 ZIP、SHA256 與 Release。";
            if (MessageBoxW(hwnd, message.c_str(), L"確認發布", MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON2) != IDYES) {
                return 0;
            }
            CloseReleaseDialog(hwnd);
            StartPublishTask(version);
            return 0;
        }
        case IDC_RELEASE_CANCEL:
            CloseReleaseDialog(hwnd);
            return 0;
        }
        break;
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, kText);
        SetBkMode(dc, TRANSPARENT);
        return reinterpret_cast<INT_PTR>(GetStockObject(NULL_BRUSH));
    }
    case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, kText);
        SetBkColor(dc, kCard);
        return reinterpret_cast<INT_PTR>(g_releaseEditBrush ? g_releaseEditBrush : GetStockObject(BLACK_BRUSH));
    }
    case WM_ERASEBKGND: {
        RECT rc{};
        GetClientRect(hwnd, &rc);
        HBRUSH brush = CreateSolidBrush(kBg);
        FillRect(reinterpret_cast<HDC>(wParam), &rc, brush);
        DeleteObject(brush);
        return 1;
    }
    case WM_CLOSE:
        CloseReleaseDialog(hwnd);
        return 0;
    case WM_DESTROY:
        if (g_releaseDialog == hwnd) g_releaseDialog = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void ShowReleaseDialog() {
    if (!g_developerMode) return;
    if (g_busy) {
        MessageBoxW(g_hwnd, L"目前已有其他作業正在執行，請完成後再發布。",
                    L"VR Full Keyboard", MB_ICONINFORMATION | MB_OK);
        return;
    }
    if (FindGit().empty()) {
        MessageBoxW(g_hwnd, L"找不到 Git。發布 GitHub 版本需要 Git for Windows。",
                    L"無法發布", MB_ICONWARNING | MB_OK);
        return;
    }
    if (!fs::exists(g_root / L".git")) {
        const int answer = MessageBoxW(
            g_hwnd,
            L"目前專案資料夾缺少 .git。這通常發生在從 ZIP 接手開發版後。\n\n"
            L"是否現在自動從官方 GitHub Repository 重建 Git metadata？\n\n"
            L"此操作不會覆蓋目前 Source；TEST13 的本機修改會保留為待提交差異。",
            L"修復 Git Repository",
            MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON1);
        if (answer != IDYES) return;

        const fs::path git = FindGit();
        std::wstring repairError;
        if (!BootstrapGitRepository(git, &repairError)) {
            MessageBoxW(g_hwnd, (L"無法重建 Git Repository。\n\n" + repairError).c_str(),
                        L"Git 修復失敗", MB_ICONERROR | MB_OK);
            return;
        }
        MessageBoxW(g_hwnd,
                    L"Git Repository metadata 已重建完成。\n\n現在可以繼續發布；目前 Source 沒有被覆蓋。",
                    L"Git 修復完成", MB_ICONINFORMATION | MB_OK);
    }
    if (g_releaseDialog && IsWindow(g_releaseDialog)) {
        SetForegroundWindow(g_releaseDialog);
        return;
    }

    const UINT dpi = WindowDpi(g_hwnd);
    const int width = ScaleForDpi(520, dpi);
    const int height = ScaleForDpi(316, dpi);
    RECT owner{};
    GetWindowRect(g_hwnd, &owner);
    const int x = owner.left + ((owner.right - owner.left) - width) / 2;
    const int y = owner.top + ((owner.bottom - owner.top) - height) / 2;

    HWND dialog = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
        kReleaseDialogClass, L"發布 GitHub 新版本",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        x, y, width, height, g_hwnd, nullptr, g_instance, nullptr);
    if (!dialog) {
        MessageBoxW(g_hwnd, L"無法建立發布視窗。", L"VR Full Keyboard", MB_ICONERROR | MB_OK);
        return;
    }
    g_releaseDialog = dialog;
    EnableWindow(g_hwnd, FALSE);
    ShowWindow(dialog, SW_SHOW);
    UpdateWindow(dialog);
}

void SetLogExpanded(bool expanded) {
    g_logExpanded = expanded;
    if (g_hwnd) {
        HWND toggle = GetDlgItem(g_hwnd, IDC_LOG_TOGGLE);
        if (toggle) InvalidateRect(toggle, nullptr, TRUE);
        Layout(g_hwnd);
        InvalidateRect(g_hwnd, nullptr, TRUE);
    }
}

void ClearVisibleLog() {
    if (g_log) SetWindowTextW(g_log, L"");
}

bool CopyVisibleLog() {
    if (!g_log) return false;
    const int len = GetWindowTextLengthW(g_log);
    std::wstring text((size_t)len + 1, L'\0');
    if (len > 0) GetWindowTextW(g_log, text.data(), len + 1);
    text.resize((size_t)len);
    if (!OpenClipboard(g_hwnd)) return false;
    EmptyClipboard();
    const SIZE_T bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!mem) { CloseClipboard(); return false; }
    void* ptr = GlobalLock(mem);
    memcpy(ptr, text.c_str(), bytes);
    GlobalUnlock(mem);
    SetClipboardData(CF_UNICODETEXT, mem);
    CloseClipboard();
    return true;
}

void DrawOwnerButton(const DRAWITEMSTRUCT* dis) {
    if (!dis || dis->CtlType != ODT_BUTTON) return;
    const int id = (int)dis->CtlID;
    const ButtonVisual visual = GetButtonVisual(id);
    const bool disabled = (dis->itemState & ODS_DISABLED) != 0;
    const bool pressed = (dis->itemState & ODS_SELECTED) != 0;
    const bool hovered = g_hoverButtons.contains(dis->hwndItem);
    const bool focused = (dis->itemState & ODS_FOCUS) != 0;
    const UINT dpi = WindowDpi(dis->hwndItem);

    COLORREF fill = kCard;
    COLORREF border = kBorder;
    COLORREF titleColor = kText;
    COLORREF subColor = kMuted;
    switch (visual.tone) {
    case ButtonTone::Primary:
        fill = kAccent;
        border = Blend(kAccent, RGB(255,255,255), 16);
        titleColor = RGB(255,255,255);
        subColor = RGB(220,235,255);
        break;
    case ButtonTone::Accent:
        fill = Blend(kCard, kCyan, 12);
        border = Blend(kBorder, kCyan, 60);
        titleColor = RGB(221, 249, 255);
        subColor = RGB(158, 205, 215);
        break;
    case ButtonTone::Danger:
        fill = Blend(kCard, kRed, 8);
        border = Blend(kBorder, kRed, 42);
        titleColor = RGB(246, 208, 213);
        break;
    case ButtonTone::Utility:
        fill = RGB(20, 26, 37);
        border = RGB(42, 53, 70);
        break;
    default:
        break;
    }
    if (hovered && !disabled) {
        fill = visual.tone == ButtonTone::Primary ? kAccentHover : Blend(fill, RGB(255,255,255), 7);
        border = Blend(border, RGB(255,255,255), 14);
    }
    if (pressed && !disabled) fill = visual.tone == ButtonTone::Primary ? kAccentPressed : Blend(fill, RGB(0,0,0), 14);
    if (disabled) {
        fill = Blend(kBg, kCard, 45);
        border = Blend(kBg, kBorder, 40);
        titleColor = RGB(95, 106, 124);
        subColor = RGB(77, 88, 104);
    }

    RECT rc = dis->rcItem;
    const int radius = ScaleForDpi(10, dpi);
    FillRounded(dis->hDC, rc, radius, fill, focused ? kAccent : border, focused ? 2 : 1);

    const int pad = ScaleForDpi(14, dpi);
    RECT tr{rc.left + pad, rc.top + ScaleForDpi(8, dpi), rc.right - pad, rc.bottom - ScaleForDpi(7, dpi)};
    const bool compact = (rc.bottom - rc.top) < ScaleForDpi(54, dpi) || visual.subtitle[0] == L'\0';
    if (compact) {
        DrawTextSimple(dis->hDC, visual.title, tr, g_font, titleColor, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_END_ELLIPSIS);
    } else {
        RECT titleRc = tr;
        titleRc.bottom = titleRc.top + ScaleForDpi(24, dpi);
        DrawTextSimple(dis->hDC, visual.title, titleRc, g_font, titleColor, DT_SINGLELINE | DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);
        RECT subRc{tr.left, titleRc.bottom + ScaleForDpi(2, dpi), tr.right, tr.bottom};
        DrawTextSimple(dis->hDC, visual.subtitle, subRc, g_smallFont, subColor, DT_SINGLELINE | DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);
    }
}

struct LayoutMetrics {
    RECT header{};
    RECT launchCard{};
    RECT perfCard{};
    RECT devCard{};
    RECT toolsCard{};
    RECT logCard{};
    int launchLabelY = 0;
    int perfLabelY = 0;
    int devLabelY = 0;
    int toolsLabelY = 0;
    int logLabelY = 0;
};

LayoutMetrics ComputeLayout(HWND hwnd) {
    RECT rc{}; GetClientRect(hwnd, &rc);
    const UINT dpi = WindowDpi(hwnd);
    const int w = rc.right;
    const int h = rc.bottom;
    const int m = ScaleForDpi(22, dpi);
    const int gap = ScaleForDpi(12, dpi);
    const int headerH = ScaleForDpi(126, dpi);
    const int sectionTitleH = ScaleForDpi(27, dpi);
    const int cardPad = ScaleForDpi(14, dpi);
    const int mainButtonH = ScaleForDpi(70, dpi);
    const int toolButtonH = ScaleForDpi(58, dpi);
    LayoutMetrics lm{};
    lm.header = RECT{m, m, w - m, m + headerH};

    int y = lm.header.bottom + ScaleForDpi(18, dpi);
    lm.launchLabelY = y;
    y += sectionTitleH;
    lm.launchCard = RECT{m, y, w - m, y + mainButtonH + cardPad * 2};
    y = lm.launchCard.bottom + ScaleForDpi(16, dpi);

    lm.perfLabelY = y;
    y += sectionTitleH;
    lm.perfCard = RECT{m, y, w - m, y + ScaleForDpi(104, dpi)};
    y = lm.perfCard.bottom + ScaleForDpi(16, dpi);

    if (g_developerMode) {
        lm.devLabelY = y;
        y += sectionTitleH;
        lm.devCard = RECT{m, y, w - m, y + toolButtonH + cardPad * 2};
        y = lm.devCard.bottom + ScaleForDpi(16, dpi);
    }

    lm.toolsLabelY = y;
    y += sectionTitleH;
    lm.toolsCard = RECT{m, y, w - m, y + ScaleForDpi(50, dpi) + cardPad * 2};
    y = lm.toolsCard.bottom + ScaleForDpi(16, dpi);

    // The public share build is intentionally a compact launcher: four launch
    // actions plus one desktop-shortcut action. Developer-only diagnostics
    // and logs stay out of the user-facing window.
    if (g_developerMode) {
        lm.logLabelY = y;
        y += sectionTitleH;
        const int collapsed = ScaleForDpi(86, dpi);
        const int expandedMin = ScaleForDpi(170, dpi);
        int logH = g_logExpanded ? std::max(expandedMin, h - y - m) : collapsed;
        if (y + logH + m > h) logH = std::max(ScaleForDpi(64, dpi), h - y - m);
        lm.logCard = RECT{m, y, w - m, y + logH};
    }
    return lm;
}

void Layout(HWND hwnd) {
    RECT rc{}; GetClientRect(hwnd, &rc);
    const UINT dpi = WindowDpi(hwnd);
    const int gap = ScaleForDpi(10, dpi);
    const int pad = ScaleForDpi(14, dpi);
    const LayoutMetrics lm = ComputeLayout(hwnd);
    // Cache the only parent-window area that changes on the 500 ms performance timer.
    // Include the section label and a small margin around the card for antialiasing.
    g_perfPaintRect = lm.perfCard;
    g_perfPaintRect.top = lm.perfLabelY;
    InflateRect(&g_perfPaintRect, ScaleForDpi(2, dpi), ScaleForDpi(2, dpi));

    auto placeRow = [&](const RECT& card, const std::vector<int>& ids, int height) {
        const int count = (int)ids.size();
        if (!count) return;
        const int innerW = (card.right - card.left) - pad * 2;
        const int bw = (innerW - gap * (count - 1)) / count;
        const int y = card.top + pad;
        for (int i = 0; i < count; ++i) {
            HWND h = GetDlgItem(hwnd, ids[i]);
            if (h) MoveWindow(h, card.left + pad + i * (bw + gap), y, bw, height, TRUE);
        }
    };

    placeRow(lm.launchCard, {IDC_RUN_VR, IDC_STOP_VR, IDC_PREVIEW, IDC_EDITOR, IDC_UPDATE}, ScaleForDpi(70, dpi));
    if (g_developerMode) {
        placeRow(lm.devCard, {IDC_BUILD, IDC_PACKAGE, IDC_PUBLISH_GITHUB, IDC_CLEAN, IDC_OPEN_OUTPUT}, ScaleForDpi(58, dpi));
        placeRow(lm.toolsCard, {IDC_OPEN_ROOT, IDC_OPEN_LOG, IDC_OPEN_PERF_REPORT, IDC_ROLLBACK_TEST, IDC_SHORTCUT}, ScaleForDpi(50, dpi));

        const int actionW = ScaleForDpi(104, dpi);
        const int actionH = ScaleForDpi(32, dpi);
        const int logGap = ScaleForDpi(8, dpi);
        int ax = lm.logCard.right - pad - actionW;
        MoveWindow(GetDlgItem(hwnd, IDC_LOG_COPY), ax, lm.logLabelY - ScaleForDpi(2, dpi), actionW, actionH, TRUE); ax -= actionW + logGap;
        MoveWindow(GetDlgItem(hwnd, IDC_LOG_CLEAR), ax, lm.logLabelY - ScaleForDpi(2, dpi), actionW, actionH, TRUE); ax -= actionW + logGap;
        MoveWindow(GetDlgItem(hwnd, IDC_LOG_TOGGLE), ax, lm.logLabelY - ScaleForDpi(2, dpi), actionW, actionH, TRUE);
    } else {
        placeRow(lm.toolsCard, {IDC_SHORTCUT}, ScaleForDpi(50, dpi));
    }

    if (g_developerMode && g_log) {
        const int inset = ScaleForDpi(12, dpi);
        const int logX = static_cast<int>(lm.logCard.left) + inset;
        const int logY = static_cast<int>(lm.logCard.top) + inset;
        const int logWidth = std::max(ScaleForDpi(36, dpi),
            static_cast<int>(lm.logCard.right - lm.logCard.left) - inset * 2);
        const int logHeight = std::max(ScaleForDpi(36, dpi),
            static_cast<int>(lm.logCard.bottom - lm.logCard.top) - inset * 2);
        MoveWindow(g_log, logX, logY, logWidth, logHeight, TRUE);
    }
}

void DrawSectionTitle(HDC dc, const wchar_t* text, int y, HWND hwnd) {
    const UINT dpi = WindowDpi(hwnd);
    RECT rc{ScaleForDpi(26, dpi), y, 6000, y + ScaleForDpi(24, dpi)};
    DrawTextSimple(dc, text, rc, g_font, RGB(190, 202, 220), DT_SINGLELINE | DT_LEFT | DT_VCENTER);
}

void PaintWindow(HWND hwnd, HDC dc) {
    RECT client{}; GetClientRect(hwnd, &client);
    HBRUSH bg = CreateSolidBrush(kBg); FillRect(dc, &client, bg); DeleteObject(bg);
    const UINT dpi = WindowDpi(hwnd);
    const LayoutMetrics lm = ComputeLayout(hwnd);

    FillRounded(dc, lm.header, ScaleForDpi(14, dpi), kPanel, kBorder);
    const int hx = lm.header.left + ScaleForDpi(22, dpi);
    RECT titleRc{hx, lm.header.top + ScaleForDpi(16, dpi), lm.header.right - ScaleForDpi(20, dpi), lm.header.top + ScaleForDpi(54, dpi)};
    DrawTextSimple(dc, L"VR Full Keyboard", titleRc, g_titleFont, kText, DT_SINGLELINE | DT_LEFT | DT_VCENTER);
    RECT subRc{hx, lm.header.top + ScaleForDpi(52, dpi), lm.header.right - ScaleForDpi(20, dpi), lm.header.top + ScaleForDpi(78, dpi)};
    DrawTextSimple(dc, L"控制中心  ·  SteamVR 虛擬全尺寸鍵盤", subRc, g_font, kMuted, DT_SINGLELINE | DT_LEFT | DT_VCENTER);

    int pillX = hx;
    const int pillY = lm.header.top + ScaleForDpi(85, dpi);
    const wchar_t* coreStatus = g_coreOutdated ? L"Core 過期" : (g_coreReady ? L"已建置" : (g_developerMode ? L"尚未建置" : L"檔案不完整"));
    DrawPill(dc, pillX, pillY, coreStatus, g_coreReady ? kGreen : kAmber, dpi);
    DrawPill(dc, pillX, pillY, Utf8ToWide(VRFK_DISPLAY_VERSION) + L" · " + Utf8ToWide(VRFK_TEST_BUILD_LABEL), kAccent, dpi);
    DrawPill(dc, pillX, pillY, g_developerMode ? L"開發版" : L"分享版", kCyan, dpi);
    if (g_developerMode) {
        DrawPill(dc, pillX, pillY, g_cmakeReady ? L"CMake OK" : L"CMake 未找到", g_cmakeReady ? kGreen : kRed, dpi);
        DrawPill(dc, pillX, pillY, g_gitReady ? L"Git OK" : L"Git 未找到", g_gitReady ? kGreen : kRed, dpi);
    }
    if (!g_developerMode && g_updateAvailableHint && !g_updateAvailableTag.empty()) {
        DrawPill(dc, pillX, pillY, L"有新版 " + g_updateAvailableTag, kAmber, dpi);
    }

    DrawSectionTitle(dc, L"啟動", lm.launchLabelY, hwnd);
    FillRounded(dc, lm.launchCard, ScaleForDpi(12, dpi), kPanel, RGB(34, 44, 59));

    DrawSectionTitle(dc, L"即時效能監測", lm.perfLabelY, hwnd);
    FillRounded(dc, lm.perfCard, ScaleForDpi(12, dpi), kPanel, RGB(34, 44, 59));
    const int px = lm.perfCard.left + ScaleForDpi(18, dpi);
    const int py = lm.perfCard.top + ScaleForDpi(12, dpi);
    RECT statusRc{px, py, lm.perfCard.right - ScaleForDpi(18, dpi), py + ScaleForDpi(22, dpi)};
    std::wstring perfStatus = g_coreRunning ? L"● 鍵盤執行中" : L"○ 鍵盤未執行";
    if (g_coreRunning && !g_perfSnapshot.valid) perfStatus += L" · 等待第一筆資料…";
    DrawTextSimple(dc, perfStatus, statusRc, g_font, g_coreRunning ? kGreen : kMuted, DT_SINGLELINE | DT_LEFT | DT_VCENTER);
    if (g_perfSnapshot.valid) {
        auto fmt = [](const wchar_t* label, float a, float b) {
            wchar_t buf[160]{};
            swprintf_s(buf, std::size(buf), L"%s %.3f / %.3f ms", label, a, b);
            return std::wstring(buf);
        };
        wchar_t top[256]{};
        swprintf_s(top, std::size(top), L"CPU %.2f%%    Loop %.0f/s    Total %.3f / %.3f ms    Submit Error %d",
                   g_perfSnapshot.cpuPercent, g_perfSnapshot.loopsPerSec,
                   g_perfSnapshot.avgTotalMs, g_perfSnapshot.maxTotalMs, g_perfSnapshot.submitError);
        RECT topRc{px, py + ScaleForDpi(24, dpi), lm.perfCard.right - ScaleForDpi(18, dpi), py + ScaleForDpi(45, dpi)};
        DrawTextSimple(dc, top, topRc, g_font, kText, DT_SINGLELINE | DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);
        const std::wstring left = fmt(L"Tracking", g_perfSnapshot.avgTrackingMs, g_perfSnapshot.maxTrackingMs) +
                                  L"    " + fmt(L"Events", g_perfSnapshot.avgEventsMs, g_perfSnapshot.maxEventsMs) +
                                  L"    " + fmt(L"Logic", g_perfSnapshot.avgLogicMs, g_perfSnapshot.maxLogicMs);
        RECT leftRc{px, py + ScaleForDpi(47, dpi), lm.perfCard.right - ScaleForDpi(18, dpi), py + ScaleForDpi(67, dpi)};
        DrawTextSimple(dc, left, leftRc, g_smallFont, kMuted, DT_SINGLELINE | DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);
        const std::wstring right = fmt(L"ImGui", g_perfSnapshot.avgImGuiMs, g_perfSnapshot.maxImGuiMs) +
                                   L"    " + fmt(L"D3D", g_perfSnapshot.avgD3DMs, g_perfSnapshot.maxD3DMs) +
                                   L"    " + fmt(L"Submit", g_perfSnapshot.avgSubmitMs, g_perfSnapshot.maxSubmitMs);
        RECT rightRc{px, py + ScaleForDpi(68, dpi), lm.perfCard.right - ScaleForDpi(18, dpi), py + ScaleForDpi(88, dpi)};
        DrawTextSimple(dc, right, rightRc, g_smallFont, kMuted, DT_SINGLELINE | DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);
    } else if (!g_coreRunning) {
        RECT hintRc{px, py + ScaleForDpi(30, dpi), lm.perfCard.right - ScaleForDpi(18, dpi), py + ScaleForDpi(78, dpi)};
        DrawTextSimple(dc, L"啟動 VR 鍵盤後，每 0.5 秒更新 CPU、主迴圈、Tracking、Events、Logic、ImGui、D3D、Submit 與 Total；同時自動寫入 CSV。",
                       hintRc, g_smallFont, kMuted, DT_WORDBREAK | DT_LEFT | DT_VCENTER);
    }

    if (g_developerMode) {
        DrawSectionTitle(dc, L"開發與發行", lm.devLabelY, hwnd);
        FillRounded(dc, lm.devCard, ScaleForDpi(12, dpi), kPanel, RGB(34, 44, 59));
    }
    DrawSectionTitle(dc, g_developerMode ? L"工具" : L"桌面捷徑", lm.toolsLabelY, hwnd);
    FillRounded(dc, lm.toolsCard, ScaleForDpi(12, dpi), kPanel, RGB(34, 44, 59));
    if (g_developerMode) {
        DrawSectionTitle(dc, L"作業記錄", lm.logLabelY, hwnd);
        FillRounded(dc, lm.logCard, ScaleForDpi(12, dpi), kPanel, RGB(34, 44, 59));
    }
}

void EnableDarkTitleBar(HWND hwnd) {
    BOOL enabled = TRUE;
    constexpr DWORD kImmersiveDarkMode = 20;
    DwmSetWindowAttribute(hwnd, kImmersiveDarkMode, &enabled, sizeof(enabled));
    COLORREF caption = kBg;
    COLORREF text = kText;
    constexpr DWORD kCaptionColor = 35;
    constexpr DWORD kTextColor = 36;
    DwmSetWindowAttribute(hwnd, kCaptionColor, &caption, sizeof(caption));
    DwmSetWindowAttribute(hwnd, kTextColor, &text, sizeof(text));
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        EnableDarkTitleBar(hwnd);
        g_logExpanded = false;

        MakeButton(hwnd, IDC_RUN_VR);
        MakeButton(hwnd, IDC_STOP_VR);
        MakeButton(hwnd, IDC_PREVIEW);
        MakeButton(hwnd, IDC_EDITOR);
        MakeButton(hwnd, IDC_UPDATE);

        if (g_developerMode) {
            g_taskButtons.push_back(MakeButton(hwnd, IDC_BUILD));
            g_taskButtons.push_back(MakeButton(hwnd, IDC_PACKAGE));
            g_taskButtons.push_back(MakeButton(hwnd, IDC_PUBLISH_GITHUB));
            g_taskButtons.push_back(MakeButton(hwnd, IDC_CLEAN));
            g_taskButtons.push_back(MakeButton(hwnd, IDC_OPEN_OUTPUT));
        }
        if (g_developerMode) {
            MakeButton(hwnd, IDC_OPEN_ROOT);
            MakeButton(hwnd, IDC_OPEN_LOG);
            MakeButton(hwnd, IDC_OPEN_PERF_REPORT);
            g_taskButtons.push_back(MakeButton(hwnd, IDC_ROLLBACK_TEST));
            MakeButton(hwnd, IDC_LOG_TOGGLE);
            MakeButton(hwnd, IDC_LOG_CLEAR);
            MakeButton(hwnd, IDC_LOG_COPY);

            g_log = CreateWindowExW(0, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
                0, 0, 100, 100, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LOG)), g_instance, nullptr);
            SendMessageW(g_log, WM_SETFONT, reinterpret_cast<WPARAM>(g_logFont), TRUE);
            SendMessageW(g_log, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                         MAKELPARAM(ScaleForDpi(8, WindowDpi(hwnd)), ScaleForDpi(8, WindowDpi(hwnd))));
            AppendLog(L"控制中心已就緒。\r\n");
        }
        MakeButton(hwnd, IDC_SHORTCUT);

        if (g_developerMode) {
            const fs::path bootstrap = g_root / L".frontend" / L"bootstrap_build.log";
            const std::wstring bootstrapText = ReadTextFile(bootstrap);
            if (!bootstrapText.empty()) {
                if (ContainsBuildWarning(bootstrapText)) {
                    AppendLog(L"[提示] 控制中心首次建置成功，但建置工具曾輸出警告。可按「完整建置記錄」查看。\r\n");
                } else {
                    AppendLog(L"[完成] 控制中心首次建置正常，未偵測到 warning。\r\n");
                }
            }
        }
        RefreshStatus();
        Layout(hwnd);
        if (!g_developerMode) SetTimer(hwnd, TIMER_BACKGROUND_UPDATE, 1600, nullptr);
        SetTimer(hwnd, TIMER_PERF_REFRESH, 500, nullptr);
        return 0;
    }
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        const UINT dpi = WindowDpi(hwnd);
        info->ptMinTrackSize.x = ScaleForDpi(g_developerMode ? 900 : 820, dpi);
        info->ptMinTrackSize.y = ScaleForDpi(g_developerMode ? 830 : 590, dpi);
        return 0;
    }
    case WM_DPICHANGED: {
        auto* suggested = reinterpret_cast<RECT*>(lParam);
        SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left, suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        RecreateFonts(hwnd);
        Layout(hwnd);
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    }
    case WM_TIMER:
        if (wParam == TIMER_BACKGROUND_UPDATE) {
            KillTimer(hwnd, TIMER_BACKGROUND_UPDATE);
            if (!g_developerMode) StartUpdateCheckTask(true);
            return 0;
        }
        if (wParam == TIMER_PERF_REFRESH) {
            RefreshPerfSnapshot();
            return 0;
        }
        break;
    case WM_SIZE:
        Layout(hwnd);
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);

        // Double-buffer the parent surface. The UI uses many custom-drawn panels;
        // drawing them directly to the screen can expose intermediate frames during
        // timer refreshes/resizes and looks like a full-window flash.
        RECT client{};
        GetClientRect(hwnd, &client);
        const int width = client.right - client.left;
        const int height = client.bottom - client.top;
        HDC memDc = CreateCompatibleDC(dc);
        HBITMAP bitmap = memDc ? CreateCompatibleBitmap(dc, std::max(1, width), std::max(1, height)) : nullptr;
        HGDIOBJ oldBitmap = (memDc && bitmap) ? SelectObject(memDc, bitmap) : nullptr;
        if (memDc && bitmap) {
            PaintWindow(hwnd, memDc);
            BitBlt(dc, ps.rcPaint.left, ps.rcPaint.top,
                   ps.rcPaint.right - ps.rcPaint.left, ps.rcPaint.bottom - ps.rcPaint.top,
                   memDc, ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);
            SelectObject(memDc, oldBitmap);
            DeleteObject(bitmap);
            DeleteDC(memDc);
        } else {
            if (bitmap) DeleteObject(bitmap);
            if (memDc) DeleteDC(memDc);
            PaintWindow(hwnd, dc);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DRAWITEM:
        DrawOwnerButton(reinterpret_cast<DRAWITEMSTRUCT*>(lParam));
        return TRUE;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        HWND ctl = reinterpret_cast<HWND>(lParam);
        if (ctl == g_log) {
            SetTextColor(dc, RGB(205, 216, 232));
            SetBkColor(dc, kLogBg);
            return reinterpret_cast<INT_PTR>(g_logBrush);
        }
        SetTextColor(dc, kText);
        SetBkColor(dc, kBg);
        return reinterpret_cast<INT_PTR>(GetStockObject(NULL_BRUSH));
    }
    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        switch (id) {
        case IDC_RUN_VR: LaunchCore(PendingLaunch::Vr); break;
        case IDC_STOP_VR:
            if (RequestCoreShutdown()) {
                AppendLog(L"[測試] 已送出關閉鍵盤要求。\r\n");
            } else {
                MessageBoxW(hwnd, L"目前沒有可關閉的 VR Full Keyboard 測試程序。", L"關閉鍵盤", MB_ICONINFORMATION);
            }
            RefreshPerfSnapshot();
            break;
        case IDC_PREVIEW: LaunchCore(PendingLaunch::Preview); break;
        case IDC_EDITOR: LaunchCore(PendingLaunch::Editor); break;
        case IDC_UPDATE: StartUpdateCheckTask(false); break;
        case IDC_BUILD: {
            PendingLaunch pending = PendingLaunch::None;
            if (lParam >= (LPARAM)PendingLaunch::Vr && lParam <= (LPARAM)PendingLaunch::Update) pending = (PendingLaunch)lParam;
            StartBuildTask(pending);
            break;
        }
        case IDC_PACKAGE: StartPackageTask(); break;
        case IDC_PUBLISH_GITHUB: ShowReleaseDialog(); break;
        case IDC_CLEAN: CleanBuildCache(); break;
        case IDC_OPEN_OUTPUT: OpenOutput(); break;
        case IDC_OPEN_ROOT: OpenFolder(g_root); break;
        case IDC_OPEN_LOG: OpenBuildLog(); break;
        case IDC_OPEN_PERF_REPORT: OpenPerfReport(); break;
        case IDC_ROLLBACK_TEST: StartRollbackSelfTest(); break;
        case IDC_SHORTCUT: {
            const bool ok = CreateDesktopShortcut();
            MessageBoxW(hwnd, ok ? L"桌面捷徑已建立。" : L"建立桌面捷徑失敗。",
                        L"VR Full Keyboard", ok ? MB_ICONINFORMATION : MB_ICONERROR);
            break;
        }
        case IDC_LOG_TOGGLE: SetLogExpanded(!g_logExpanded); break;
        case IDC_LOG_CLEAR: ClearVisibleLog(); break;
        case IDC_LOG_COPY:
            if (!CopyVisibleLog()) MessageBoxW(hwnd, L"無法複製作業記錄。", L"VR Full Keyboard", MB_ICONWARNING);
            break;
        }
        return 0;
    }
    case WM_APP_LOG: {
        std::unique_ptr<std::wstring> text(reinterpret_cast<std::wstring*>(lParam));
        if (text) AppendLog(*text);
        return 0;
    }
    case WM_APP_UPDATE_PROGRESS: {
        std::unique_ptr<std::wstring> progress(reinterpret_cast<std::wstring*>(lParam));
        if (progress) {
            g_updateButtonSubtitle = *progress;
            if (HWND update = GetDlgItem(hwnd, IDC_UPDATE)) InvalidateRect(update, nullptr, TRUE);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_APP_UPDATE_CHECK_DONE: {
        std::unique_ptr<UpdateReleaseInfo> info(reinterpret_cast<UpdateReleaseInfo*>(lParam));
        g_updateCheckBusy = false;
        if (!info) return 0;
        if (!info->silent) SetBusy(false);
        if (info->silent) {
            if (info->success && info->updateAvailable) {
                g_updateAvailableHint = true;
                g_updateAvailableTag = Utf8ToWide(info->tag);
                g_updateButtonSubtitle = L"有新版 " + g_updateAvailableTag + L" 可安裝";
                AppendPersistentLog(L"[更新] 背景檢查發現新版：" + g_updateAvailableTag + L"\r\n");
            } else if (info->success) {
                g_updateAvailableHint = false;
                g_updateAvailableTag.clear();
                g_updateButtonSubtitle = L"目前已是最新版";
            } else {
                AppendPersistentLog(L"[更新] 背景檢查失敗：" + info->error + L"\r\n");
            }
            if (HWND update = GetDlgItem(hwnd, IDC_UPDATE)) InvalidateRect(update, nullptr, TRUE);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (!info->success) {
            const std::wstring message = info->error.empty() ? L"無法檢查更新。" : info->error;
            AppendLog(L"[更新] " + message + L"\r\n");
            g_updateButtonSubtitle = L"檢查失敗，點此重試";
            if (HWND update = GetDlgItem(hwnd, IDC_UPDATE)) InvalidateRect(update, nullptr, TRUE);
            MessageBoxW(hwnd, message.c_str(), L"檢查更新失敗", MB_ICONWARNING | MB_OK);
            return 0;
        }
        if (!info->updateAvailable) {
            const std::wstring message = L"目前版本：" + Utf8ToWide(VRFK_DISPLAY_VERSION) +
                                         L"\nGitHub 最新版本：" + Utf8ToWide(info->tag) +
                                         L"\n\n目前已是最新版。";
            AppendLog(L"[更新] 已是最新版：" + Utf8ToWide(info->tag) + L"\r\n");
            g_updateAvailableHint = false;
            g_updateAvailableTag.clear();
            g_updateButtonSubtitle = L"目前已是最新版";
            if (HWND update = GetDlgItem(hwnd, IDC_UPDATE)) InvalidateRect(update, nullptr, TRUE);
            MessageBoxW(hwnd, message.c_str(), L"VR Full Keyboard 更新", MB_ICONINFORMATION | MB_OK);
            return 0;
        }

        g_updateAvailableHint = true;
        g_updateAvailableTag = Utf8ToWide(info->tag);
        g_updateButtonSubtitle = L"有新版 " + g_updateAvailableTag + L" 可安裝";
        if (HWND update = GetDlgItem(hwnd, IDC_UPDATE)) InvalidateRect(update, nullptr, TRUE);
        InvalidateRect(hwnd, nullptr, FALSE);

        std::wstring notes = Utf8ToWide(info->notes);
        if (notes.size() > 1400) notes = notes.substr(0, 1400) + L"\n...";
        if (g_developerMode) {
            std::wstring message = L"GitHub 有新版本：" + Utf8ToWide(info->tag) +
                                   L"\n目前 Source 版本：" + Utf8ToWide(VRFK_DISPLAY_VERSION) +
                                   L"\n\n開發版不會用自動更新器覆蓋 Source 專案。\n要開啟 GitHub Release 嗎？";
            if (!notes.empty()) message += L"\n\n更新內容：\n" + notes;
            if (MessageBoxW(hwnd, message.c_str(), L"VR Full Keyboard 更新", MB_ICONINFORMATION | MB_YESNO) == IDYES && !info->pageUrl.empty()) {
                const std::wstring url = Utf8ToWide(info->pageUrl);
                ShellExecuteW(hwnd, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            }
            return 0;
        }
        std::wstring message = L"發現新版本：" + Utf8ToWide(info->tag) + L"\n目前版本：" +
                               Utf8ToWide(VRFK_DISPLAY_VERSION);
        if (!notes.empty()) message += L"\n\n更新內容：\n" + notes;
        message += L"\n\n是：下載、驗證並安裝\n否：開啟 GitHub Release\n取消：稍後再說";
        const int choice = MessageBoxW(hwnd, message.c_str(), L"VR Full Keyboard 有新版本", MB_ICONINFORMATION | MB_YESNOCANCEL);
        if (choice == IDYES) {
            StartUpdateInstallTask(*info);
        } else if (choice == IDNO && !info->pageUrl.empty()) {
            const std::wstring url = Utf8ToWide(info->pageUrl);
            ShellExecuteW(hwnd, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        return 0;
    }
    case WM_APP_UPDATE_EXIT:
        g_busy = false;
        DestroyWindow(hwnd);
        return 0;
    case WM_APP_TASK_DONE: {
        std::unique_ptr<TaskDone> done(reinterpret_cast<TaskDone*>(lParam));
        SetBusy(false);
        RefreshStatus();
        if (done) {
            AppendLog(L"\r\n" + done->summary + L"\r\n");
            if (!done->success && done->summary.find(L"更新") != std::wstring::npos) {
                g_updateButtonSubtitle = L"更新失敗，可點此重試";
                if (HWND update = GetDlgItem(hwnd, IDC_UPDATE)) InvalidateRect(update, nullptr, TRUE);
            }
            if (!done->success) {
                MessageBoxW(hwnd, done->summary.c_str(), L"作業未完成", MB_ICONWARNING);
            } else if (done->pendingLaunch != 0) {
                LaunchCore((PendingLaunch)done->pendingLaunch);
            } else if (done->offerActions) {
                std::wstring message = done->summary + L"\n\n要開啟 GitHub Actions 頁面嗎？";
                if (MessageBoxW(hwnd, message.c_str(), L"GitHub 發布已送出", MB_ICONINFORMATION | MB_YESNO) == IDYES) {
                    ShellExecuteW(hwnd, L"open", L"https://github.com/tw2323123/VRFullKeyboard/actions", nullptr, nullptr, SW_SHOWNORMAL);
                }
            } else {
                MessageBoxW(hwnd, done->summary.c_str(), L"完成", MB_ICONINFORMATION);
            }
        }
        return 0;
    }
    case WM_CLOSE:
        if (g_busy) {
            if (MessageBoxW(hwnd, L"目前仍有建置／封裝／發布／更新作業正在執行。\n確定要關閉控制中心嗎？", L"作業進行中", MB_ICONWARNING | MB_YESNO) != IDYES) return 0;
        }
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
std::wstring CommandLineOption(const wchar_t* name) {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return {};
    std::wstring value;
    for (int i = 1; i + 1 < argc; ++i) {
        if (_wcsicmp(argv[i], name) == 0) {
            value = argv[i + 1];
            break;
        }
    }
    LocalFree(argv);
    return value;
}

std::wstring CleanVersionForDisplay(std::wstring value) {
    if (!value.empty() && (value.front() == L'v' || value.front() == L'V')) value.erase(value.begin());
    return value;
}

void ShowStartupUpdateResult() {
    if (g_developerMode) return;
    const std::wstring result = CommandLineOption(L"--update-result");
    if (result.empty()) return;
    const std::wstring from = CleanVersionForDisplay(CommandLineOption(L"--from-version"));
    const std::wstring to = CleanVersionForDisplay(CommandLineOption(L"--to-version"));
    if (_wcsicmp(result.c_str(), L"success") == 0) {
        std::wstring message = L"更新完成。";
        if (!from.empty() && !to.empty()) message += L"\n\n" + from + L"  →  " + to;
        message += L"\n\n個人設定已保留，上一版備份只保留最近一份。";
        MessageBoxW(g_hwnd, message.c_str(), L"VR Full Keyboard 更新成功", MB_ICONINFORMATION | MB_OK);
    } else if (_wcsicmp(result.c_str(), L"rollback") == 0) {
        std::wstring message = L"新版啟動驗證失敗，已自動還原上一個可用版本。";
        if (!from.empty() && !to.empty()) message += L"\n\n原版本：" + from + L"\n嘗試版本：" + to;
        MessageBoxW(g_hwnd, message.c_str(), L"VR Full Keyboard 已自動還原", MB_ICONWARNING | MB_OK);
    }
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    g_instance = hInstance;
    g_root = DetectRoot();
    g_developerMode = fs::exists(g_root / L"CMakeLists.txt") && fs::exists(g_root / L"src" / L"main.cpp");
    PreparePersistentLog();

    using SetDpiAwarenessFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
    if (auto fn = reinterpret_cast<SetDpiAwarenessFn>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext"))) {
        fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }

    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    g_logBrush = CreateSolidBrush(kLogBg);
    g_releaseEditBrush = CreateSolidBrush(kCard);

    const wchar_t* cls = L"VRFullKeyboardControlCenterClass";
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = cls;
    RegisterClassExW(&wc);

    WNDCLASSEXW releaseWc{sizeof(releaseWc)};
    releaseWc.lpfnWndProc = ReleaseDialogProc;
    releaseWc.hInstance = hInstance;
    releaseWc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    releaseWc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    releaseWc.hbrBackground = nullptr;
    releaseWc.lpszClassName = kReleaseDialogClass;
    RegisterClassExW(&releaseWc);

    const int initialWidth = g_developerMode ? 1120 : 980;
    const int initialHeight = g_developerMode ? 900 : 630;
    g_hwnd = CreateWindowExW(0, cls, L"VR Full Keyboard 控制中心",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, initialWidth, initialHeight,
        nullptr, nullptr, hInstance, nullptr);
    if (!g_hwnd) return 1;

    RecreateFonts(g_hwnd);
    // WM_CREATE runs before RecreateFonts; refresh control fonts now that DPI is known.
    for (HWND button : g_allButtons) SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
    if (g_log) SendMessageW(g_log, WM_SETFONT, reinterpret_cast<WPARAM>(g_logFont), TRUE);
    Layout(g_hwnd);

    ShowWindow(g_hwnd, nCmdShow);
    UpdateWindow(g_hwnd);
    ShowStartupUpdateResult();

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (g_releaseDialog && IsWindow(g_releaseDialog) && IsDialogMessageW(g_releaseDialog, &msg)) continue;
        if (!IsDialogMessageW(g_hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (g_logBrush) DeleteObject(g_logBrush);
    if (g_releaseEditBrush) DeleteObject(g_releaseEditBrush);
    if (g_font) DeleteObject(g_font);
    if (g_smallFont) DeleteObject(g_smallFont);
    if (g_titleFont) DeleteObject(g_titleFont);
    if (g_logFont) DeleteObject(g_logFont);
    return (int)msg.wParam;
}
