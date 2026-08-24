#include <windows.h>
#include <shellapi.h>
#include <winhttp.h>
#include <imm.h>
#include <d3d11.h>
#include <dxgi.h>

#include <openvr.h>
#include <imgui.h>
#include <backends/imgui_impl_dx11.h>
#include <backends/imgui_impl_win32.h>
#include "VRFullKeyboardVersion.h"

// Dear ImGui intentionally does not expose this Win32 callback declaration
// from imgui_impl_win32.h in some versions, so applications declare it.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cfloat>
#include <cstdint>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {
constexpr int TEX_W = 1920;
constexpr int TEX_H = 660;
constexpr float DEFAULT_OVERLAY_WIDTH_M = 1.68f;
struct UiSettings {
    float keyHeight = 70.0f;
    float keyUnit = 74.0f;
    float gap = 6.0f;
    float mainFont = 27.0f;
    float shiftedFont = 15.0f;
    float zhuyinFont = 19.0f;
    float compactFont = 19.0f;
    float backgroundAlpha = 0.985f;
    float rgbIntensity = 1.0f;
    float keyboardScale = 1.0f;
    float keyboardOffsetX = 0.0f;
    float keyboardOffsetY = 0.0f;

    // V3.4 interaction controls.
    bool hapticEnabled = true;
    float hapticStrength = 0.35f;
    bool repeatEnabled = true;
    float repeatDelay = 0.38f;
    float repeatRate = 0.070f;
    bool autoFadeEnabled = true;
    float autoFadeSeconds = 8.0f;
    float autoFadeAlpha = 0.22f;

    // V3.4.3 wrist-dot controls.
    bool wristUseLeftHand = true;
    float wristWidthMeters = 0.30f; // Legacy V3.4 setting; kept for INI compatibility.
    float wristDotSizeMeters = 0.085f;
    float wristDotAlpha = 0.88f;
    float wristDoubleClickSeconds = 0.30f;
    float wristLongPressSeconds = 0.75f;

    // Font rendering mode. 0 = Clear (Segoe UI + Microsoft JhengHei), 1 = Standard JhengHei.
    int fontStyle = 0;

    // V3.7 input layout mode (introduced in V3.6). Q9 keeps real VK_NUMPAD key codes and only changes the VR UI emphasis.
    bool q9Mode = false;

    // V3.8 update checker. Download/install is intentionally deferred to V3.9.
    bool autoCheckUpdates = true;

    // V3 appearance controls.
    float keyCornerRadius = 8.0f;
    float keyBorderThickness = 2.0f;
    std::array<float, 3> backgroundColor{0.025f, 0.030f, 0.042f};
    std::array<float, 3> keyColor{0.122f, 0.141f, 0.188f};
    std::array<float, 3> mainTextColor{0.957f, 0.969f, 0.988f};
    std::array<float, 3> shiftedTextColor{0.753f, 0.804f, 0.882f};
    std::array<float, 3> zhuyinTextColor{1.000f, 0.906f, 0.678f};
    std::array<float, 3> zoneRed{1.000f, 0.282f, 0.329f};
    std::array<float, 3> zoneOrange{1.000f, 0.569f, 0.227f};
    std::array<float, 3> zoneGreen{0.349f, 0.863f, 0.478f};
    std::array<float, 3> zoneBlue{0.357f, 0.635f, 1.000f};
    std::array<float, 3> zonePurple{0.722f, 0.412f, 1.000f};
};

UiSettings g_ui;
bool g_editorMode = false;
bool g_autoSave = true;
ImVec2 g_previewKeyboardArea(0.0f, 0.0f);
float g_lastVrClickLatencyMs = 0.0f;
bool g_fontLoaded = false;
std::string g_fontStatus = "Not loaded";
bool g_vrClickPending = false;
std::chrono::steady_clock::time_point g_vrClickEventTime{};
std::chrono::steady_clock::time_point g_lastVrInteraction = std::chrono::steady_clock::now();
float g_overlayAlpha = 1.0f;
ImGuiID g_repeatActiveId = 0;
double g_repeatNextTime = 0.0;

enum class PageMode { Keyboard, Shortcuts, Clipboard, Update };
PageMode g_page = PageMode::Keyboard;

struct ShortcutSlot {
    char label[48];
    char action[256];
};

struct ShortcutBank {
    char name[32];
    std::array<ShortcutSlot, 8> slots;
};

std::array<ShortcutBank, 3> MakeDefaultShortcutBanks() {
    std::array<ShortcutBank, 3> banks{};
    auto setName = [&](size_t bank, const char* name) {
        strncpy_s(banks[bank].name, sizeof(banks[bank].name), name, _TRUNCATE);
    };
    auto setSlot = [&](size_t bank, size_t slot, const char* label, const char* action) {
        strncpy_s(banks[bank].slots[slot].label, sizeof(banks[bank].slots[slot].label), label, _TRUNCATE);
        strncpy_s(banks[bank].slots[slot].action, sizeof(banks[bank].slots[slot].action), action, _TRUNCATE);
    };

    setName(0, "通用");
    setSlot(0, 0, "複製", "CTRL+C");
    setSlot(0, 1, "貼上", "CTRL+V");
    setSlot(0, 2, "復原", "CTRL+Z");
    setSlot(0, 3, "截圖", "WIN+SHIFT+S");
    setSlot(0, 4, "顯示桌面", "WIN+D");
    setSlot(0, 5, "工作管理員", "CTRL+SHIFT+ESC");
    setSlot(0, 6, "常用文字 1", "TEXT:等我一下");
    setSlot(0, 7, "常用文字 2", "TEXT:我在 VR");

    setName(1, "VRChat");
    setSlot(1, 0, "選單 / Esc", "ESC");
    setSlot(1, 1, "快速文字 1", "TEXT:等我一下");
    setSlot(1, 2, "快速文字 2", "TEXT:我在 VR");
    setSlot(1, 3, "截圖", "WIN+SHIFT+S");
    setSlot(1, 4, "切換視窗", "ALT+TAB");
    setSlot(1, 5, "自訂 6", "");
    setSlot(1, 6, "自訂 7", "");
    setSlot(1, 7, "自訂 8", "");

    setName(2, "工作");
    setSlot(2, 0, "儲存", "CTRL+S");
    setSlot(2, 1, "另存新檔", "CTRL+SHIFT+S");
    setSlot(2, 2, "復原", "CTRL+Z");
    setSlot(2, 3, "重做", "CTRL+Y");
    setSlot(2, 4, "複製", "CTRL+C");
    setSlot(2, 5, "貼上", "CTRL+V");
    setSlot(2, 6, "切換視窗", "ALT+TAB");
    setSlot(2, 7, "截圖", "WIN+SHIFT+S");
    return banks;
}

const std::array<ShortcutBank, 3> kDefaultShortcutBanks = MakeDefaultShortcutBanks();
std::array<ShortcutBank, 3> g_shortcutBanks = kDefaultShortcutBanks;
size_t g_shortcutBankIndex = 0;

ShortcutBank& CurrentShortcutBank() {
    g_shortcutBankIndex = (std::min)(g_shortcutBankIndex, g_shortcutBanks.size() - 1);
    return g_shortcutBanks[g_shortcutBankIndex];
}

std::array<ShortcutSlot, 8>& CurrentShortcutSlots() {
    return CurrentShortcutBank().slots;
}

std::deque<std::wstring> g_clipboardHistory;
DWORD g_lastClipboardSequence = 0;
std::chrono::steady_clock::time_point g_lastClipboardPoll{};

enum class MacroStepType { Chord, Text, Wait };
struct MacroStep {
    MacroStepType type = MacroStepType::Chord;
    std::vector<WORD> keys;
    std::wstring text;
    int waitMs = 0;
};
std::deque<MacroStep> g_macroQueue;
std::chrono::steady_clock::time_point g_macroReadyTime{};
bool g_macroRunning = false;
std::string g_macroStatus = "閒置";

// Desktop editor drag state.
bool g_desktopKeyboardDragging = false;
ImVec2 g_desktopDragStartMouse(0.0f, 0.0f);
float g_desktopDragStartOffsetX = 0.0f;
float g_desktopDragStartOffsetY = 0.0f;

// SteamVR controller grab state. The grab handle moves the whole overlay,
// and releasing it converts the overlay back to a world-fixed transform.
bool g_vrGrabActive = false;
vr::TrackedDeviceIndex_t g_vrGrabDevice = vr::k_unTrackedDeviceIndexInvalid;
vr::TrackedDeviceIndex_t g_lastPointerDevice = vr::k_unTrackedDeviceIndexInvalid;
vr::HmdMatrix34_t g_vrGrabRelative{};

// V3.4 wrist standby / summon state. These positions are session-only because
// SteamVR Standing-space coordinates may change between room setup sessions.
bool g_wristStandby = false;
vr::HmdMatrix34_t g_returnWorldTransform{};
bool g_returnWorldTransformValid = false;
float g_returnWidthMeters = DEFAULT_OVERLAY_WIDTH_M;
std::string g_vrPlacementStatus = "尚未儲存返回位置";

// V3.4.3 wrist-dot gesture state. A single click is deliberately deferred
// until the double-click window expires, so double-click never triggers the
// single-click action first.
bool g_wristPendingSingle = false;
double g_wristPendingSingleDue = 0.0;
double g_wristLastClickTime = -10.0;
double g_wristPressStartTime = 0.0;
bool g_wristLongPressTriggered = false;
bool g_fontRestartRequired = false;

// GitHub Release update-check state. The VR overlay can check and display
// release information; installation is handled by the native Control Center updater.
enum class UpdateCheckState { Idle, Checking, UpToDate, Available, Error, NotConfigured };
struct UpdateCheckResult {
    UpdateCheckState state = UpdateCheckState::Idle;
    std::string latestVersion;
    std::string releaseName;
    std::string releaseNotes;
    std::string releaseUrl;
    std::string message = "尚未檢查更新";
};

std::mutex g_updateMutex;
UpdateCheckResult g_updateResult;
std::thread g_updateThread;
std::atomic_bool g_updateChecking{false};
bool g_updateAutoStarted = false;

struct DxState {
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    ID3D11Texture2D* texture = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
};

void SafeRelease(IUnknown*& p) {
    if (p) { p->Release(); p = nullptr; }
}

template <typename T>
void SafeReleaseT(T*& p) {
    if (p) { p->Release(); p = nullptr; }
}

bool InitD3D(DxState& dx) {
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    const D3D_FEATURE_LEVEL requested[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };
    D3D_FEATURE_LEVEL got{};

    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
        requested, static_cast<UINT>(std::size(requested)),
        D3D11_SDK_VERSION, &dx.device, &got, &dx.context);

    if (FAILED(hr)) {
        // Some systems do not have the debug layer installed.
        flags &= ~D3D11_CREATE_DEVICE_DEBUG;
        hr = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
            requested, static_cast<UINT>(std::size(requested)),
            D3D11_SDK_VERSION, &dx.device, &got, &dx.context);
    }
    if (FAILED(hr)) return false;

    D3D11_TEXTURE2D_DESC td{};
    td.Width = TEX_W;
    td.Height = TEX_H;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    hr = dx.device->CreateTexture2D(&td, nullptr, &dx.texture);
    if (FAILED(hr)) return false;
    hr = dx.device->CreateRenderTargetView(dx.texture, nullptr, &dx.rtv);
    return SUCCEEDED(hr);
}

void ShutdownD3D(DxState& dx) {
    SafeReleaseT(dx.rtv);
    SafeReleaseT(dx.texture);
    SafeReleaseT(dx.context);
    SafeReleaseT(dx.device);
}

struct ModifierState {
    bool ctrl = false;
    bool shift = false;
    bool alt = false;
    bool win = false;

    void clear() { ctrl = shift = alt = win = false; }
};

ModifierState g_mods;
bool g_previewMode = false;

void SendVk(WORD vk, bool down) {
    if (g_previewMode) return;
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = vk;
    input.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    SendInput(1, &input, sizeof(INPUT));
}

void SendTapRaw(WORD vk) {
    SendVk(vk, true);
    SendVk(vk, false);
}

void PressActiveModifiers() {
    if (g_mods.ctrl)  SendVk(VK_CONTROL, true);
    if (g_mods.shift) SendVk(VK_SHIFT, true);
    if (g_mods.alt)   SendVk(VK_MENU, true);
    if (g_mods.win)   SendVk(VK_LWIN, true);
}

void ReleaseActiveModifiers() {
    if (g_mods.win)   SendVk(VK_LWIN, false);
    if (g_mods.alt)   SendVk(VK_MENU, false);
    if (g_mods.shift) SendVk(VK_SHIFT, false);
    if (g_mods.ctrl)  SendVk(VK_CONTROL, false);
}

void SendKey(WORD vk) {
    PressActiveModifiers();
    SendTapRaw(vk);
    ReleaseActiveModifiers();
    g_mods.clear();
}

void SendChord(std::initializer_list<WORD> keys) {
    std::vector<WORD> v(keys);
    for (WORD k : v) SendVk(k, true);
    for (auto it = v.rbegin(); it != v.rend(); ++it) SendVk(*it, false);
    g_mods.clear();
}

void SendChordVector(const std::vector<WORD>& keys) {
    for (WORD k : keys) SendVk(k, true);
    for (auto it = keys.rbegin(); it != keys.rend(); ++it) SendVk(*it, false);
    g_mods.clear();
}

std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
    if (count <= 0) return {};
    std::wstring result(static_cast<size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), result.data(), count);
    return result;
}

std::string WideToUtf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int count = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (count <= 0) return {};
    std::string result(static_cast<size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), result.data(), count, nullptr, nullptr);
    return result;
}

void SendUnicodeText(const std::wstring& text) {
    if (g_previewMode || text.empty()) return;
    for (wchar_t ch : text) {
        INPUT down{};
        down.type = INPUT_KEYBOARD;
        down.ki.wScan = static_cast<WORD>(ch);
        down.ki.dwFlags = KEYEVENTF_UNICODE;
        INPUT up = down;
        up.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
        INPUT pair[2]{down, up};
        SendInput(2, pair, sizeof(INPUT));
    }
}

std::string TrimAscii(std::string v) {
    auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    v.erase(v.begin(), std::find_if(v.begin(), v.end(), notSpace));
    v.erase(std::find_if(v.rbegin(), v.rend(), notSpace).base(), v.end());
    return v;
}


void AppendUtf8Codepoint(std::string& out, unsigned int cp) {
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
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

bool ExtractJsonString(const std::string& json, const char* key, std::string& out) {
    const std::string needle = std::string("\"") + key + "\"";
    size_t pos = json.find(needle);
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
        if (c != '\\') {
            out.push_back(c);
            continue;
        }
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

bool ParseSemVer(const std::string& raw, std::array<int, 3>& out) {
    std::string v = TrimAscii(raw);
    if (!v.empty() && (v[0] == 'v' || v[0] == 'V')) v.erase(v.begin());
    const size_t suffix = v.find_first_of("-+");
    if (suffix != std::string::npos) v.resize(suffix);

    size_t start = 0;
    for (int i = 0; i < 3; ++i) {
        const size_t dot = v.find('.', start);
        const size_t end = (i == 2) ? v.size() : dot;
        if (end == std::string::npos || end <= start) return false;
        const std::string part = v.substr(start, end - start);
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

UpdateCheckResult GetUpdateSnapshot() {
    std::lock_guard<std::mutex> lock(g_updateMutex);
    return g_updateResult;
}

void SetUpdateResult(UpdateCheckResult result) {
    std::lock_guard<std::mutex> lock(g_updateMutex);
    g_updateResult = std::move(result);
}

bool UpdateRepoConfigured() {
    return VRFK_GITHUB_OWNER[0] != '\0' && VRFK_GITHUB_REPO[0] != '\0';
}

bool HttpGetLatestRelease(std::string& response, DWORD& statusCode, std::string& error) {
    response.clear();
    statusCode = 0;
    error.clear();

    const std::wstring userAgent = Utf8ToWide(std::string("VRFullKeyboard/") + VRFK_VERSION_STRING);
    HINTERNET session = WinHttpOpen(userAgent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        error = "WinHTTP 初始化失敗：" + std::to_string(GetLastError());
        return false;
    }
    WinHttpSetTimeouts(session, 5000, 5000, 8000, 8000);

    HINTERNET connection = WinHttpConnect(session, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connection) {
        error = "無法連線 GitHub：" + std::to_string(GetLastError());
        WinHttpCloseHandle(session);
        return false;
    }

    const std::string apiPathUtf8 = std::string("/repos/") + VRFK_GITHUB_OWNER + "/" + VRFK_GITHUB_REPO + "/releases/latest";
    const std::wstring apiPath = Utf8ToWide(apiPathUtf8);
    HINTERNET request = WinHttpOpenRequest(connection, L"GET", apiPath.c_str(), nullptr,
                                           WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           WINHTTP_FLAG_SECURE);
    if (!request) {
        error = "建立 GitHub 請求失敗：" + std::to_string(GetLastError());
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return false;
    }

    static constexpr wchar_t headers[] =
        L"Accept: application/vnd.github+json\r\n"
        L"X-GitHub-Api-Version: 2026-03-10\r\n";
    WinHttpAddRequestHeaders(request, headers, -1L, WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);

    const bool sent = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                         WINHTTP_NO_REQUEST_DATA, 0, 0, 0) != FALSE;
    const bool received = sent && WinHttpReceiveResponse(request, nullptr) != FALSE;
    if (!received) {
        error = "GitHub 請求失敗：" + std::to_string(GetLastError());
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return false;
    }

    DWORD statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);

    while (response.size() < 2 * 1024 * 1024) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available)) {
            error = "讀取 GitHub 回應失敗：" + std::to_string(GetLastError());
            break;
        }
        if (available == 0) break;
        const size_t oldSize = response.size();
        const size_t room = 2 * 1024 * 1024 - oldSize;
        const DWORD toRead = static_cast<DWORD>((std::min)(room, static_cast<size_t>(available)));
        response.resize(oldSize + toRead);
        DWORD read = 0;
        if (!WinHttpReadData(request, response.data() + oldSize, toRead, &read)) {
            response.resize(oldSize);
            error = "下載 GitHub 回應失敗：" + std::to_string(GetLastError());
            break;
        }
        response.resize(oldSize + read);
        if (read == 0) break;
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return error.empty();
}

void RunUpdateCheckWorker() {
    UpdateCheckResult result;
    result.state = UpdateCheckState::Checking;
    result.message = "正在檢查 GitHub Release...";
    SetUpdateResult(result);

    std::string json;
    std::string error;
    DWORD statusCode = 0;
    if (!HttpGetLatestRelease(json, statusCode, error)) {
        result.state = UpdateCheckState::Error;
        result.message = error.empty() ? "無法檢查更新" : error;
        SetUpdateResult(result);
        g_updateChecking = false;
        return;
    }
    if (statusCode != 200) {
        result.state = UpdateCheckState::Error;
        result.message = "GitHub API 回傳 HTTP " + std::to_string(statusCode);
        if (statusCode == 404) result.message += "（請確認倉庫已公開且已有正式 Release）";
        SetUpdateResult(result);
        g_updateChecking = false;
        return;
    }

    std::string tag;
    if (!ExtractJsonString(json, "tag_name", tag)) {
        result.state = UpdateCheckState::Error;
        result.message = "GitHub Release 缺少 tag_name";
        SetUpdateResult(result);
        g_updateChecking = false;
        return;
    }

    std::array<int, 3> parsed{};
    if (!ParseSemVer(tag, parsed)) {
        result.state = UpdateCheckState::Error;
        result.message = "Release 版本格式不是 vMAJOR.MINOR.PATCH：" + tag;
        SetUpdateResult(result);
        g_updateChecking = false;
        return;
    }

    result.latestVersion = tag;
    ExtractJsonString(json, "name", result.releaseName);
    ExtractJsonString(json, "body", result.releaseNotes);
    ExtractJsonString(json, "html_url", result.releaseUrl);
    if (result.releaseNotes.size() > 3500) {
        result.releaseNotes.resize(3500);
        result.releaseNotes += "\n...";
    }

    if (CompareSemVer(tag, VRFK_VERSION_STRING) > 0) {
        result.state = UpdateCheckState::Available;
        result.message = "發現新版本";
    } else {
        result.state = UpdateCheckState::UpToDate;
        result.message = "目前已是最新版";
    }
    SetUpdateResult(result);
    g_updateChecking = false;
}

void StartUpdateCheck() {
    if (g_updateChecking.load()) return;
    if (!UpdateRepoConfigured()) {
        UpdateCheckResult result;
        result.state = UpdateCheckState::NotConfigured;
        result.message = "尚未綁定 GitHub Repository；建立正式倉庫後再填入更新來源。";
        SetUpdateResult(result);
        return;
    }

    if (g_updateThread.joinable()) g_updateThread.join();
    g_updateChecking = true;
    g_updateThread = std::thread(RunUpdateCheckWorker);
}

void MaybeStartAutoUpdateCheck() {
    if (g_updateAutoStarted) return;
    g_updateAutoStarted = true;
    if (g_ui.autoCheckUpdates) StartUpdateCheck();
}

void ShutdownUpdateChecker() {
    if (g_updateThread.joinable()) g_updateThread.join();
}

void OpenLatestReleasePage() {
    const UpdateCheckResult result = GetUpdateSnapshot();
    if (result.releaseUrl.empty()) return;
    const std::wstring url = Utf8ToWide(result.releaseUrl);
    ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

std::string UpperAscii(std::string v) {
    for (char& c : v) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return v;
}

WORD VkFromToken(const std::string& raw) {
    const std::string t = UpperAscii(TrimAscii(raw));
    if (t.size() == 1 && ((t[0] >= 'A' && t[0] <= 'Z') || (t[0] >= '0' && t[0] <= '9'))) return static_cast<WORD>(t[0]);
    if (t == "CTRL" || t == "CONTROL") return VK_CONTROL;
    if (t == "SHIFT") return VK_SHIFT;
    if (t == "ALT") return VK_MENU;
    if (t == "WIN" || t == "WINDOWS") return VK_LWIN;
    if (t == "TAB") return VK_TAB;
    if (t == "ESC" || t == "ESCAPE") return VK_ESCAPE;
    if (t == "ENTER" || t == "RETURN") return VK_RETURN;
    if (t == "SPACE") return VK_SPACE;
    if (t == "BACKSPACE") return VK_BACK;
    if (t == "DELETE" || t == "DEL") return VK_DELETE;
    if (t == "INSERT" || t == "INS") return VK_INSERT;
    if (t == "HOME") return VK_HOME;
    if (t == "END") return VK_END;
    if (t == "PGUP" || t == "PAGEUP") return VK_PRIOR;
    if (t == "PGDN" || t == "PAGEDOWN") return VK_NEXT;
    if (t == "UP") return VK_UP;
    if (t == "DOWN") return VK_DOWN;
    if (t == "LEFT") return VK_LEFT;
    if (t == "RIGHT") return VK_RIGHT;
    if (t == "PRTSC" || t == "PRINTSCREEN") return VK_SNAPSHOT;
    if (t.rfind("F", 0) == 0 && t.size() <= 3) {
        try {
            const int n = std::stoi(t.substr(1));
            if (n >= 1 && n <= 12) return static_cast<WORD>(VK_F1 + (n - 1));
        } catch (...) {}
    }
    return 0;
}

bool ParseChordSpec(const std::string& action, std::vector<WORD>& keys) {
    keys.clear();
    size_t start = 0;
    while (start <= action.size()) {
        const size_t plus = action.find('+', start);
        const std::string token = action.substr(start, plus == std::string::npos ? std::string::npos : plus - start);
        const WORD vk = VkFromToken(token);
        if (!vk) return false;
        keys.push_back(vk);
        if (plus == std::string::npos) break;
        start = plus + 1;
    }
    return !keys.empty();
}

bool ParseMacroSteps(const std::string& body, std::deque<MacroStep>& outSteps) {
    outSteps.clear();
    size_t start = 0;
    while (start <= body.size()) {
        const size_t semi = body.find(';', start);
        std::string part = TrimAscii(body.substr(start, semi == std::string::npos ? std::string::npos : semi - start));
        if (!part.empty()) {
            const std::string upper = UpperAscii(part);
            MacroStep step;
            if (upper.rfind("WAIT:", 0) == 0) {
                try {
                    int ms = std::stoi(TrimAscii(part.substr(5)));
                    step.type = MacroStepType::Wait;
                    step.waitMs = std::clamp(ms, 0, 5000);
                } catch (...) {
                    return false;
                }
            } else if (upper.rfind("TEXT:", 0) == 0) {
                step.type = MacroStepType::Text;
                step.text = Utf8ToWide(part.substr(5));
            } else {
                step.type = MacroStepType::Chord;
                if (!ParseChordSpec(part, step.keys)) return false;
            }
            outSteps.push_back(std::move(step));
        }
        if (semi == std::string::npos) break;
        start = semi + 1;
    }
    return !outSteps.empty();
}

bool IsActionSpecValid(const char* spec) {
    if (!spec) return false;
    const std::string action = TrimAscii(spec);
    if (action.empty()) return false;
    const std::string upper = UpperAscii(action);
    if (upper.rfind("TEXT:", 0) == 0) return true;
    if (upper.rfind("MACRO:", 0) == 0) {
        std::deque<MacroStep> steps;
        return ParseMacroSteps(action.substr(6), steps);
    }
    std::vector<WORD> keys;
    return ParseChordSpec(action, keys);
}

void StartMacro(const std::string& body) {
    std::deque<MacroStep> steps;
    if (!ParseMacroSteps(body, steps)) {
        g_macroQueue.clear();
        g_macroRunning = false;
        g_macroStatus = "格式錯誤";
        return;
    }
    g_macroQueue = std::move(steps);
    g_macroReadyTime = std::chrono::steady_clock::now();
    g_macroRunning = true;
    g_macroStatus = "執行中";
}

void ProcessMacroQueue() {
    if (!g_macroRunning) return;
    const auto now = std::chrono::steady_clock::now();
    if (now < g_macroReadyTime) return;

    if (g_macroQueue.empty()) {
        g_macroRunning = false;
        g_macroStatus = "完成";
        return;
    }

    MacroStep step = std::move(g_macroQueue.front());
    g_macroQueue.pop_front();
    if (step.type == MacroStepType::Wait) {
        g_macroReadyTime = now + std::chrono::milliseconds(step.waitMs);
    } else if (step.type == MacroStepType::Text) {
        SendUnicodeText(step.text);
        g_macroReadyTime = now;
    } else {
        SendChordVector(step.keys);
        g_macroReadyTime = now;
    }

    if (g_macroQueue.empty() && step.type != MacroStepType::Wait) {
        g_macroRunning = false;
        g_macroStatus = "完成";
    }
}

bool ExecuteActionSpec(const char* spec) {
    if (!spec) return false;
    std::string action = TrimAscii(spec);
    if (action.empty()) return false;
    const std::string upper = UpperAscii(action);
    if (upper.rfind("TEXT:", 0) == 0) {
        SendUnicodeText(Utf8ToWide(action.substr(5)));
        return true;
    }
    if (upper.rfind("MACRO:", 0) == 0) {
        StartMacro(action.substr(6));
        return g_macroRunning;
    }

    std::vector<WORD> keys;
    if (!ParseChordSpec(action, keys)) return false;
    SendChordVector(keys);
    return true;
}

void PulseHaptic(float scale = 1.0f) {
    if (g_previewMode || !g_ui.hapticEnabled) return;
    auto* sys = vr::VRSystem();
    if (!sys || g_lastPointerDevice == vr::k_unTrackedDeviceIndexInvalid) return;
    if (g_lastPointerDevice >= vr::k_unMaxTrackedDeviceCount) return;
    if (sys->GetTrackedDeviceClass(g_lastPointerDevice) != vr::TrackedDeviceClass_Controller) return;
    const float strength = std::clamp(g_ui.hapticStrength * scale, 0.05f, 1.0f);
    const unsigned short duration = static_cast<unsigned short>(200.0f + 1500.0f * strength);
    sys->TriggerHapticPulse(g_lastPointerDevice, 0, duration);
}

bool SelectedButton(const char* label, bool selected, ImVec2 size) {
    if (selected) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.48f, 0.78f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.56f, 0.90f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.14f, 0.40f, 0.68f, 1.0f));
    }
    const bool clicked = ImGui::Button(label, size);
    if (selected) ImGui::PopStyleColor(3);
    return clicked;
}

// Editor-only button renderer.  Traditional Chinese glyphs in Microsoft JhengHei
// have a visual baseline that sits lower than ImGui's nominal font metrics.
// Drawing the label ourselves lets us center the visible text independently of
// ImGui::Button's baseline calculation while retaining normal ImGui interaction.
bool EditorCenteredButton(const char* label, ImVec2 size, float visualYOffset = -3.5f) {
    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (size.x < 0.0f) size.x = (std::max)(1.0f, avail.x + size.x + 1.0f);
    if (size.y <= 0.0f) size.y = ImGui::GetFrameHeight();

    ImGui::PushID(label);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton("##editor_centered_button", size);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();

    const ImGuiStyle& style = ImGui::GetStyle();
    const ImU32 bg = ImGui::GetColorU32(active ? ImGuiCol_ButtonActive :
                                       hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button);
    const ImU32 border = ImGui::GetColorU32(ImGuiCol_Border);
    const ImU32 textColor = ImGui::GetColorU32(ImGuiCol_Text);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p1(p0.x + size.x, p0.y + size.y);
    dl->AddRectFilled(p0, p1, bg, style.FrameRounding);
    if (style.FrameBorderSize > 0.0f)
        dl->AddRect(p0, p1, border, style.FrameRounding, 0, style.FrameBorderSize);

    const ImVec2 textSize = ImGui::CalcTextSize(label, nullptr, true);
    const ImVec2 textPos(
        p0.x + (size.x - textSize.x) * 0.5f,
        p0.y + (size.y - textSize.y) * 0.5f + visualYOffset);
    dl->AddText(textPos, textColor, label);
    ImGui::PopID();
    return clicked;
}

bool EditorCenteredSelectedButton(const char* label, bool selected, ImVec2 size) {
    if (selected) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.48f, 0.78f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.56f, 0.90f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.14f, 0.40f, 0.68f, 1.0f));
    }
    const bool clicked = EditorCenteredButton(label, size);
    if (selected) ImGui::PopStyleColor(3);
    return clicked;
}

float KeyboardScale() {
    return std::clamp(g_ui.keyboardScale, 0.55f, 1.35f);
}

float W(float units) {
    return g_ui.keyUnit * units + g_ui.gap * (units - 1.0f);
}

enum class KeyZone { Red, Orange, Green, Blue, Purple, Neutral };

ImU32 ZoneColor(KeyZone z) {
    const std::array<float, 3>* c = nullptr;
    int a = 230;
    switch (z) {
        case KeyZone::Red:    c = &g_ui.zoneRed; break;
        case KeyZone::Orange: c = &g_ui.zoneOrange; break;
        case KeyZone::Green:  c = &g_ui.zoneGreen; break;
        case KeyZone::Blue:   c = &g_ui.zoneBlue; a = 235; break;
        case KeyZone::Purple: c = &g_ui.zonePurple; a = 235; break;
        default: break;
    }
    if (!c) return IM_COL32(125, 135, 155,
        std::clamp(static_cast<int>(210 * g_ui.rgbIntensity), 25, 255));
    const int r = static_cast<int>(std::clamp((*c)[0], 0.0f, 1.0f) * 255.0f);
    const int g = static_cast<int>(std::clamp((*c)[1], 0.0f, 1.0f) * 255.0f);
    const int b = static_cast<int>(std::clamp((*c)[2], 0.0f, 1.0f) * 255.0f);
    a = (std::max)(25, (std::min)(255, static_cast<int>(a * g_ui.rgbIntensity)));
    return IM_COL32(r, g, b, a);
}

void DrawTextCentered(ImDrawList* dl, const ImVec2& pos, const ImVec2& size,
                      const char* text, float fontSize, ImU32 color) {
    if (!text || !*text) return;
    ImFont* font = ImGui::GetFont();
    const ImVec2 ts = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text);
    dl->AddText(font, fontSize,
                ImVec2(pos.x + (size.x - ts.x) * 0.5f, pos.y + (size.y - ts.y) * 0.5f),
                color, text);
}

bool PhysicalKeyButton(const char* id,
                       const char* primary,
                       const char* shifted,
                       const char* zhuyin,
                       ImVec2 size,
                       KeyZone zone,
                       bool selected = false,
                       bool compactText = false,
                       bool repeatable = false) {
    ImGui::PushID(id);
    const float ks = KeyboardScale();
    size.x *= ks;
    size.y *= ks;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("key", size);
    // Trigger on button-down instead of waiting for release. In VR this removes
    // the perceptible click/release delay caused by controller ray interaction.
    const bool activated = ImGui::IsItemActivated();
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    const ImGuiID itemId = ImGui::GetID("key");
    const double nowTime = ImGui::GetTime();
    if (activated && repeatable) {
        g_repeatActiveId = itemId;
        g_repeatNextTime = nowTime + static_cast<double>(g_ui.repeatDelay);
    }
    bool repeated = false;
    if (repeatable && g_ui.repeatEnabled && held && !activated && g_repeatActiveId == itemId && nowTime >= g_repeatNextTime) {
        repeated = true;
        g_repeatNextTime += static_cast<double>(g_ui.repeatRate);
        if (g_repeatNextTime < nowTime - static_cast<double>(g_ui.repeatRate) * 3.0)
            g_repeatNextTime = nowTime + static_cast<double>(g_ui.repeatRate);
    }
    if (ImGui::IsItemDeactivated() && g_repeatActiveId == itemId) g_repeatActiveId = 0;
    const bool triggered = activated || repeated;
    if (triggered && !g_previewMode && g_vrClickPending) {
        g_lastVrClickLatencyMs = std::chrono::duration<float, std::milli>(
            std::chrono::steady_clock::now() - g_vrClickEventTime).count();
        g_vrClickPending = false;
    }
    if (triggered) PulseHaptic(repeated ? 0.32f : 1.0f);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    auto toU32 = [](const std::array<float, 3>& c, float alpha = 1.0f) {
        return IM_COL32(
            static_cast<int>(std::clamp(c[0], 0.0f, 1.0f) * 255.0f),
            static_cast<int>(std::clamp(c[1], 0.0f, 1.0f) * 255.0f),
            static_cast<int>(std::clamp(c[2], 0.0f, 1.0f) * 255.0f),
            static_cast<int>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f));
    };
    auto brighten = [](const std::array<float, 3>& c, float mul) {
        std::array<float, 3> r = c;
        for (float& v : r) v = std::clamp(v * mul, 0.0f, 1.0f);
        return r;
    };

    ImU32 fill = toU32(g_ui.keyColor, 0.99f);
    if (selected) fill = IM_COL32(42, 104, 164, 255);
    else if (held) fill = toU32(brighten(g_ui.keyColor, 1.65f));
    else if (hovered) fill = toU32(brighten(g_ui.keyColor, 1.30f));

    const ImU32 border = selected ? IM_COL32(105, 205, 255, 255) : ZoneColor(zone);
    const float radius = g_ui.keyCornerRadius * ks;
    const float borderThickness = (selected ? (g_ui.keyBorderThickness + 1.0f) : g_ui.keyBorderThickness) * ks;
    const float pressOffset = held ? 2.5f * ks : 0.0f;
    const ImVec2 drawPos(pos.x, pos.y + pressOffset);
    dl->AddRectFilled(drawPos, ImVec2(drawPos.x + size.x, drawPos.y + size.y), fill, radius);
    dl->AddRect(drawPos, ImVec2(drawPos.x + size.x, drawPos.y + size.y), border, radius, 0, borderThickness);
    dl->AddRect(ImVec2(drawPos.x + 3, drawPos.y + 3), ImVec2(drawPos.x + size.x - 3, drawPos.y + size.y - 3),
                IM_COL32(255, 255, 255, held ? 8 : 18), (std::max)(1.0f, radius - 2.0f), 0, 1.0f);

    const ImU32 mainColor = toU32(g_ui.mainTextColor);
    const ImU32 subColor = toU32(g_ui.shiftedTextColor);
    const ImU32 bopomofoColor = toU32(g_ui.zhuyinTextColor);

    if (compactText) {
        DrawTextCentered(dl, drawPos, size, primary, g_ui.compactFont * ks, mainColor);
    } else {
        if (primary && *primary) {
            ImFont* font = ImGui::GetFont();
            const float mainSize = ((std::strlen(primary) <= 2) ? g_ui.mainFont : (g_ui.mainFont * 0.67f)) * ks;
            const ImVec2 ts = font->CalcTextSizeA(mainSize, FLT_MAX, 0.0f, primary);
            dl->AddText(font, mainSize,
                        ImVec2(drawPos.x + (size.x - ts.x) * 0.5f, drawPos.y + (size.y - ts.y) * 0.43f),
                        mainColor, primary);
        }
        if (shifted && *shifted) {
            dl->AddText(ImGui::GetFont(), g_ui.shiftedFont * ks, ImVec2(drawPos.x + 8.0f * ks, drawPos.y + 5.0f * ks), subColor, shifted);
        }
        if (zhuyin && *zhuyin) {
            ImFont* font = ImGui::GetFont();
            const float zSize = g_ui.zhuyinFont * ks;
            const ImVec2 zt = font->CalcTextSizeA(zSize, FLT_MAX, 0.0f, zhuyin);
            dl->AddText(font, zSize,
                        ImVec2(drawPos.x + size.x - zt.x - 8.0f * ks, drawPos.y + size.y - zt.y - 5.0f * ks),
                        bopomofoColor, zhuyin);
        }
    }

    ImGui::PopID();
    return triggered;
}

void Key(const char* id, const char* primary, WORD vk, float units, KeyZone zone,
         const char* shifted = "", const char* zhuyin = "", bool repeatable = false) {
    if (PhysicalKeyButton(id, primary, shifted, zhuyin, ImVec2(W(units), g_ui.keyHeight), zone, false, false, repeatable)) SendKey(vk);
}

void ModKey(const char* id, const char* label, bool& state, float units, KeyZone zone) {
    if (PhysicalKeyButton(id, label, "", "", ImVec2(W(units), g_ui.keyHeight), zone, state, true)) state = !state;
}

void SameKey(float spacing = -1.0f) { ImGui::SameLine(0, (spacing < 0.0f ? g_ui.gap : spacing) * KeyboardScale()); }

void DrawAlphaKeyboard() {
    // Standard Taiwan Zhuyin (Da-Qian) legends, arranged to match the user's full-size physical keyboard.
    Key("grave", "`", VK_OEM_3, 1.0f, KeyZone::Red, "~"); SameKey();
    Key("1", "1", '1', 1.0f, KeyZone::Red, "!", "ㄅ"); SameKey();
    Key("2", "2", '2', 1.0f, KeyZone::Red, "@", "ㄉ"); SameKey();
    Key("3", "3", '3', 1.0f, KeyZone::Orange, "#", "ˇ"); SameKey();
    Key("4", "4", '4', 1.0f, KeyZone::Orange, "$", "ˋ"); SameKey();
    Key("5", "5", '5', 1.0f, KeyZone::Orange, "%", "ㄓ"); SameKey();
    Key("6", "6", '6', 1.0f, KeyZone::Orange, "^", "ˊ"); SameKey();
    Key("7", "7", '7', 1.0f, KeyZone::Green, "&", "˙"); SameKey();
    Key("8", "8", '8', 1.0f, KeyZone::Green, "*", "ㄚ"); SameKey();
    Key("9", "9", '9', 1.0f, KeyZone::Green, "(", "ㄞ"); SameKey();
    Key("0", "0", '0', 1.0f, KeyZone::Green, ")", "ㄢ"); SameKey();
    Key("minus", "-", VK_OEM_MINUS, 1.0f, KeyZone::Blue, "_", "ㄦ"); SameKey();
    Key("equals", "=", VK_OEM_PLUS, 1.0f, KeyZone::Blue, "+"); SameKey();
    Key("backspace", "BACKSPACE", VK_BACK, 2.05f, KeyZone::Blue, "", "", true);

    Key("tab", "TAB", VK_TAB, 1.55f, KeyZone::Red); SameKey();
    Key("Q", "Q", 'Q', 1.0f, KeyZone::Red, "", "ㄆ"); SameKey();
    Key("W", "W", 'W', 1.0f, KeyZone::Red, "", "ㄊ"); SameKey();
    Key("E", "E", 'E', 1.0f, KeyZone::Orange, "", "ㄍ"); SameKey();
    Key("R", "R", 'R', 1.0f, KeyZone::Orange, "", "ㄐ"); SameKey();
    Key("T", "T", 'T', 1.0f, KeyZone::Orange, "", "ㄔ"); SameKey();
    Key("Y", "Y", 'Y', 1.0f, KeyZone::Orange, "", "ㄗ"); SameKey();
    Key("U", "U", 'U', 1.0f, KeyZone::Green, "", "ㄧ"); SameKey();
    Key("I", "I", 'I', 1.0f, KeyZone::Green, "", "ㄛ"); SameKey();
    Key("O", "O", 'O', 1.0f, KeyZone::Green, "", "ㄟ"); SameKey();
    Key("P", "P", 'P', 1.0f, KeyZone::Green, "", "ㄣ"); SameKey();
    Key("lbracket", "[", VK_OEM_4, 1.0f, KeyZone::Blue, "{"); SameKey();
    Key("rbracket", "]", VK_OEM_6, 1.0f, KeyZone::Blue, "}"); SameKey();
    Key("backslash", "\\", VK_OEM_5, 1.50f, KeyZone::Blue, "|");

    if (PhysicalKeyButton("caps", "CAPS", "", "", ImVec2(W(1.80f), g_ui.keyHeight), KeyZone::Red,
                          (GetKeyState(VK_CAPITAL) & 0x0001) != 0, true)) SendKey(VK_CAPITAL); SameKey();
    Key("A", "A", 'A', 1.0f, KeyZone::Red, "", "ㄇ"); SameKey();
    Key("S", "S", 'S', 1.0f, KeyZone::Red, "", "ㄋ"); SameKey();
    Key("D", "D", 'D', 1.0f, KeyZone::Orange, "", "ㄎ"); SameKey();
    Key("F", "F", 'F', 1.0f, KeyZone::Orange, "", "ㄑ"); SameKey();
    Key("G", "G", 'G', 1.0f, KeyZone::Orange, "", "ㄕ"); SameKey();
    Key("H", "H", 'H', 1.0f, KeyZone::Orange, "", "ㄘ"); SameKey();
    Key("J", "J", 'J', 1.0f, KeyZone::Green, "", "ㄨ"); SameKey();
    Key("K", "K", 'K', 1.0f, KeyZone::Green, "", "ㄜ"); SameKey();
    Key("L", "L", 'L', 1.0f, KeyZone::Green, "", "ㄠ"); SameKey();
    Key("semicolon", ";", VK_OEM_1, 1.0f, KeyZone::Blue, ":", "ㄤ"); SameKey();
    Key("quote", "'", VK_OEM_7, 1.0f, KeyZone::Blue, "\""); SameKey();
    Key("enter", "ENTER", VK_RETURN, 2.18f, KeyZone::Blue);

    ModKey("shiftL", "SHIFT", g_mods.shift, 2.35f, KeyZone::Red); SameKey();
    Key("Z", "Z", 'Z', 1.0f, KeyZone::Red, "", "ㄈ"); SameKey();
    Key("X", "X", 'X', 1.0f, KeyZone::Red, "", "ㄌ"); SameKey();
    Key("C", "C", 'C', 1.0f, KeyZone::Orange, "", "ㄏ"); SameKey();
    Key("V", "V", 'V', 1.0f, KeyZone::Orange, "", "ㄒ"); SameKey();
    Key("B", "B", 'B', 1.0f, KeyZone::Orange, "", "ㄖ"); SameKey();
    Key("N", "N", 'N', 1.0f, KeyZone::Green, "", "ㄙ"); SameKey();
    Key("M", "M", 'M', 1.0f, KeyZone::Green, "", "ㄩ"); SameKey();
    Key("comma", ",", VK_OEM_COMMA, 1.0f, KeyZone::Green, "<", "ㄝ"); SameKey();
    Key("period", ".", VK_OEM_PERIOD, 1.0f, KeyZone::Blue, ">", "ㄡ"); SameKey();
    Key("slash", "/", VK_OEM_2, 1.0f, KeyZone::Blue, "?", "ㄥ"); SameKey();
    ModKey("shiftR", "SHIFT", g_mods.shift, 2.43f, KeyZone::Blue);

    ModKey("ctrlL", "CTRL", g_mods.ctrl, 1.55f, KeyZone::Red); SameKey();
    ModKey("winL", "WIN", g_mods.win, 1.35f, KeyZone::Red); SameKey();
    ModKey("altL", "ALT", g_mods.alt, 1.35f, KeyZone::Orange); SameKey();
    Key("space", "SPACE", VK_SPACE, 6.30f, KeyZone::Green); SameKey();
    ModKey("altR", "ALT", g_mods.alt, 1.35f, KeyZone::Green); SameKey();
    PhysicalKeyButton("fn", "FN", "", "", ImVec2(W(1.15f), g_ui.keyHeight), KeyZone::Blue, false, true); SameKey();
    Key("menu", "MENU", VK_APPS, 1.25f, KeyZone::Blue); SameKey();
    ModKey("ctrlR", "CTRL", g_mods.ctrl, 1.50f, KeyZone::Blue);
}

void DrawNavigation() {
    Key("ins", "INSERT", VK_INSERT, 1.0f, KeyZone::Blue); SameKey();
    Key("home", "HOME", VK_HOME, 1.0f, KeyZone::Blue); SameKey();
    Key("pgup", "PAGE\nUP", VK_PRIOR, 1.0f, KeyZone::Blue, "", "", true);
    Key("del", "DELETE", VK_DELETE, 1.0f, KeyZone::Blue, "", "", true); SameKey();
    Key("end", "END", VK_END, 1.0f, KeyZone::Blue); SameKey();
    Key("pgdn", "PAGE\nDOWN", VK_NEXT, 1.0f, KeyZone::Blue, "", "", true);

    ImGui::Dummy(ImVec2(1, 8 * KeyboardScale()));
    ImGui::Indent((W(1.0f) + g_ui.gap) * KeyboardScale());
    Key("up", "↑", VK_UP, 1.0f, KeyZone::Blue, "", "", true);
    ImGui::Unindent((W(1.0f) + g_ui.gap) * KeyboardScale());
    Key("left", "←", VK_LEFT, 1.0f, KeyZone::Blue, "", "", true); SameKey();
    Key("down", "↓", VK_DOWN, 1.0f, KeyZone::Blue, "", "", true); SameKey();
    Key("right", "→", VK_RIGHT, 1.0f, KeyZone::Blue, "", "", true);
}

void Q9NumpadKey(const char* id, const char* primary, WORD vk, float units, const char* normalShifted = "") {
    // Q9 is intentionally presentation-only: always send the real numeric keypad VK code.
    // The installed Q9 IME remains responsible for radicals, candidates and composition.
    const char* q9Corner = g_ui.q9Mode ? "九方" : "";
    if (PhysicalKeyButton(id, primary, normalShifted, q9Corner, ImVec2(W(units), g_ui.keyHeight),
                          KeyZone::Purple, g_ui.q9Mode, false)) SendKey(vk);
}

void DrawNumpad() {
    const bool numLockOn = (GetKeyState(VK_NUMLOCK) & 0x0001) != 0;
    if (PhysicalKeyButton("numlock", "NUM\nLOCK", "", "", ImVec2(W(1.0f), g_ui.keyHeight), KeyZone::Purple,
                          numLockOn, true)) SendKey(VK_NUMLOCK); SameKey();
    Key("numdiv", "/", VK_DIVIDE, 1.0f, KeyZone::Purple); SameKey();
    Key("nummul", "*", VK_MULTIPLY, 1.0f, KeyZone::Purple); SameKey();
    Key("numsub", "-", VK_SUBTRACT, 1.0f, KeyZone::Purple);

    ImGui::BeginGroup();
    Q9NumpadKey("num7", "7", VK_NUMPAD7, 1.0f, "HOME"); SameKey();
    Q9NumpadKey("num8", "8", VK_NUMPAD8, 1.0f, "↑"); SameKey();
    Q9NumpadKey("num9", "9", VK_NUMPAD9, 1.0f, "PGUP");
    Q9NumpadKey("num4", "4", VK_NUMPAD4, 1.0f, "←"); SameKey();
    Q9NumpadKey("num5", "5", VK_NUMPAD5, 1.0f); SameKey();
    Q9NumpadKey("num6", "6", VK_NUMPAD6, 1.0f, "→");
    ImGui::EndGroup();
    SameKey();
    if (PhysicalKeyButton("numadd", "+", "", "", ImVec2(W(1.0f), g_ui.keyHeight * 2.0f + g_ui.gap), KeyZone::Purple)) SendKey(VK_ADD);

    ImGui::BeginGroup();
    Q9NumpadKey("num1", "1", VK_NUMPAD1, 1.0f, "END"); SameKey();
    Q9NumpadKey("num2", "2", VK_NUMPAD2, 1.0f, "↓"); SameKey();
    Q9NumpadKey("num3", "3", VK_NUMPAD3, 1.0f, "PGDN");
    Q9NumpadKey("num0", "0", VK_NUMPAD0, 2.07f, "INS"); SameKey();
    Key("numdot", ".", VK_DECIMAL, 1.0f, KeyZone::Purple, "DEL");
    ImGui::EndGroup();
    SameKey();
    if (PhysicalKeyButton("numenter", "ENTER", "", "", ImVec2(W(1.0f), g_ui.keyHeight * 2.0f + g_ui.gap), KeyZone::Purple, false, true)) SendKey(VK_RETURN);
}


std::filesystem::path SettingsPath() {
    wchar_t exePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::filesystem::path p(exePath);
    p.replace_filename(L"VRFullKeyboard.ini");
    return p;
}

void ResetUiSettings() {
    g_ui = UiSettings{};
    g_shortcutBanks = kDefaultShortcutBanks;
    g_shortcutBankIndex = 0;
    g_macroQueue.clear();
    g_macroRunning = false;
    g_macroStatus = "閒置";
}

void ResetAppearanceSettings() {
    const UiSettings d{};
    g_ui.keyCornerRadius = d.keyCornerRadius;
    g_ui.keyBorderThickness = d.keyBorderThickness;
    g_ui.backgroundColor = d.backgroundColor;
    g_ui.keyColor = d.keyColor;
    g_ui.mainTextColor = d.mainTextColor;
    g_ui.shiftedTextColor = d.shiftedTextColor;
    g_ui.zhuyinTextColor = d.zhuyinTextColor;
    g_ui.zoneRed = d.zoneRed;
    g_ui.zoneOrange = d.zoneOrange;
    g_ui.zoneGreen = d.zoneGreen;
    g_ui.zoneBlue = d.zoneBlue;
    g_ui.zonePurple = d.zonePurple;
    g_ui.backgroundAlpha = d.backgroundAlpha;
    g_ui.rgbIntensity = d.rgbIntensity;
}

void SaveSettings() {
    std::ofstream out(SettingsPath());
    if (!out) return;
    out << "keyHeight=" << g_ui.keyHeight << '\n';
    out << "keyUnit=" << g_ui.keyUnit << '\n';
    out << "gap=" << g_ui.gap << '\n';
    out << "mainFont=" << g_ui.mainFont << '\n';
    out << "shiftedFont=" << g_ui.shiftedFont << '\n';
    out << "zhuyinFont=" << g_ui.zhuyinFont << '\n';
    out << "compactFont=" << g_ui.compactFont << '\n';
    out << "backgroundAlpha=" << g_ui.backgroundAlpha << '\n';
    out << "rgbIntensity=" << g_ui.rgbIntensity << '\n';
    out << "keyboardScale=" << g_ui.keyboardScale << '\n';
    out << "keyboardOffsetX=" << g_ui.keyboardOffsetX << '\n';
    out << "keyboardOffsetY=" << g_ui.keyboardOffsetY << '\n';
    out << "keyCornerRadius=" << g_ui.keyCornerRadius << '\n';
    out << "keyBorderThickness=" << g_ui.keyBorderThickness << '\n';
    out << "hapticEnabled=" << (g_ui.hapticEnabled ? 1 : 0) << '\n';
    out << "hapticStrength=" << g_ui.hapticStrength << '\n';
    out << "repeatEnabled=" << (g_ui.repeatEnabled ? 1 : 0) << '\n';
    out << "repeatDelay=" << g_ui.repeatDelay << '\n';
    out << "repeatRate=" << g_ui.repeatRate << '\n';
    out << "autoFadeEnabled=" << (g_ui.autoFadeEnabled ? 1 : 0) << '\n';
    out << "autoFadeSeconds=" << g_ui.autoFadeSeconds << '\n';
    out << "autoFadeAlpha=" << g_ui.autoFadeAlpha << '\n';
    out << "wristUseLeftHand=" << (g_ui.wristUseLeftHand ? 1 : 0) << '\n';
    out << "wristWidthMeters=" << g_ui.wristWidthMeters << '\n';
    out << "wristDotSizeMeters=" << g_ui.wristDotSizeMeters << '\n';
    out << "wristDotAlpha=" << g_ui.wristDotAlpha << '\n';
    out << "wristDoubleClickSeconds=" << g_ui.wristDoubleClickSeconds << '\n';
    out << "wristLongPressSeconds=" << g_ui.wristLongPressSeconds << '\n';
    out << "fontStyle=" << g_ui.fontStyle << '\n';
    out << "q9Mode=" << (g_ui.q9Mode ? 1 : 0) << '\n';
    out << "autoCheckUpdates=" << (g_ui.autoCheckUpdates ? 1 : 0) << '\n';
    out << "shortcutBankIndex=" << g_shortcutBankIndex << '\n';
    for (size_t b = 0; b < g_shortcutBanks.size(); ++b) {
        out << "bank" << b << "Name=" << g_shortcutBanks[b].name << '\n';
        for (size_t i = 0; i < g_shortcutBanks[b].slots.size(); ++i) {
            out << "bank" << b << "Shortcut" << i << "Label=" << g_shortcutBanks[b].slots[i].label << '\n';
            out << "bank" << b << "Shortcut" << i << "Action=" << g_shortcutBanks[b].slots[i].action << '\n';
        }
    }

    auto saveColor = [&](const char* name, const std::array<float, 3>& c) {
        out << name << "R=" << c[0] << '\n';
        out << name << "G=" << c[1] << '\n';
        out << name << "B=" << c[2] << '\n';
    };
    saveColor("backgroundColor", g_ui.backgroundColor);
    saveColor("keyColor", g_ui.keyColor);
    saveColor("mainTextColor", g_ui.mainTextColor);
    saveColor("shiftedTextColor", g_ui.shiftedTextColor);
    saveColor("zhuyinTextColor", g_ui.zhuyinTextColor);
    saveColor("zoneRed", g_ui.zoneRed);
    saveColor("zoneOrange", g_ui.zoneOrange);
    saveColor("zoneGreen", g_ui.zoneGreen);
    saveColor("zoneBlue", g_ui.zoneBlue);
    saveColor("zonePurple", g_ui.zonePurple);
}

void LoadSettings() {
    std::ifstream in(SettingsPath());
    if (!in) return;
    std::string line;
    while (std::getline(in, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        const std::string rawValue = line.substr(eq + 1);
        bool handledString = false;

        // V3.3 multi-bank shortcut settings.
        for (size_t b = 0; b < g_shortcutBanks.size() && !handledString; ++b) {
            const std::string bankPrefix = "bank" + std::to_string(b);
            if (key == bankPrefix + "Name") {
                strncpy_s(g_shortcutBanks[b].name, sizeof(g_shortcutBanks[b].name), rawValue.c_str(), _TRUNCATE);
                handledString = true;
                break;
            }
            for (size_t i = 0; i < g_shortcutBanks[b].slots.size(); ++i) {
                const std::string prefix = bankPrefix + "Shortcut" + std::to_string(i);
                if (key == prefix + "Label") {
                    strncpy_s(g_shortcutBanks[b].slots[i].label, sizeof(g_shortcutBanks[b].slots[i].label), rawValue.c_str(), _TRUNCATE);
                    handledString = true;
                    break;
                }
                if (key == prefix + "Action") {
                    strncpy_s(g_shortcutBanks[b].slots[i].action, sizeof(g_shortcutBanks[b].slots[i].action), rawValue.c_str(), _TRUNCATE);
                    handledString = true;
                    break;
                }
            }
        }

        // Backward compatibility with V3.2: old shortcutN keys become bank 0.
        if (!handledString) {
            for (size_t i = 0; i < g_shortcutBanks[0].slots.size(); ++i) {
                const std::string prefix = "shortcut" + std::to_string(i);
                if (key == prefix + "Label") {
                    strncpy_s(g_shortcutBanks[0].slots[i].label, sizeof(g_shortcutBanks[0].slots[i].label), rawValue.c_str(), _TRUNCATE);
                    handledString = true;
                    break;
                }
                if (key == prefix + "Action") {
                    strncpy_s(g_shortcutBanks[0].slots[i].action, sizeof(g_shortcutBanks[0].slots[i].action), rawValue.c_str(), _TRUNCATE);
                    handledString = true;
                    break;
                }
            }
        }
        if (handledString) continue;

        float value = 0.0f;
        try { value = std::stof(rawValue); } catch (...) { continue; }
        if (key == "shortcutBankIndex") g_shortcutBankIndex = static_cast<size_t>((std::max)(0.0f, value));
        else if (key == "keyHeight") g_ui.keyHeight = value;
        else if (key == "keyUnit") g_ui.keyUnit = value;
        else if (key == "gap") g_ui.gap = value;
        else if (key == "mainFont") g_ui.mainFont = value;
        else if (key == "shiftedFont") g_ui.shiftedFont = value;
        else if (key == "zhuyinFont") g_ui.zhuyinFont = value;
        else if (key == "compactFont") g_ui.compactFont = value;
        else if (key == "backgroundAlpha") g_ui.backgroundAlpha = value;
        else if (key == "rgbIntensity") g_ui.rgbIntensity = value;
        else if (key == "keyboardScale") g_ui.keyboardScale = value;
        else if (key == "keyboardOffsetX") g_ui.keyboardOffsetX = value;
        else if (key == "keyboardOffsetY") g_ui.keyboardOffsetY = value;
        else if (key == "keyCornerRadius") g_ui.keyCornerRadius = value;
        else if (key == "keyBorderThickness") g_ui.keyBorderThickness = value;
        else if (key == "hapticEnabled") g_ui.hapticEnabled = value > 0.5f;
        else if (key == "hapticStrength") g_ui.hapticStrength = value;
        else if (key == "repeatEnabled") g_ui.repeatEnabled = value > 0.5f;
        else if (key == "repeatDelay") g_ui.repeatDelay = value;
        else if (key == "repeatRate") g_ui.repeatRate = value;
        else if (key == "autoFadeEnabled") g_ui.autoFadeEnabled = value > 0.5f;
        else if (key == "autoFadeSeconds") g_ui.autoFadeSeconds = value;
        else if (key == "autoFadeAlpha") g_ui.autoFadeAlpha = value;
        else if (key == "wristUseLeftHand") g_ui.wristUseLeftHand = value > 0.5f;
        else if (key == "wristWidthMeters") g_ui.wristWidthMeters = value;
        else if (key == "wristDotSizeMeters") g_ui.wristDotSizeMeters = value;
        else if (key == "wristDotAlpha") g_ui.wristDotAlpha = value;
        else if (key == "wristDoubleClickSeconds") g_ui.wristDoubleClickSeconds = value;
        else if (key == "wristLongPressSeconds") g_ui.wristLongPressSeconds = value;
        else if (key == "fontStyle") g_ui.fontStyle = static_cast<int>(value);
        else if (key == "q9Mode") g_ui.q9Mode = value > 0.5f;
        else if (key == "autoCheckUpdates") g_ui.autoCheckUpdates = value > 0.5f;
        else if (key == "backgroundColorR") g_ui.backgroundColor[0] = value;
        else if (key == "backgroundColorG") g_ui.backgroundColor[1] = value;
        else if (key == "backgroundColorB") g_ui.backgroundColor[2] = value;
        else if (key == "keyColorR") g_ui.keyColor[0] = value;
        else if (key == "keyColorG") g_ui.keyColor[1] = value;
        else if (key == "keyColorB") g_ui.keyColor[2] = value;
        else if (key == "mainTextColorR") g_ui.mainTextColor[0] = value;
        else if (key == "mainTextColorG") g_ui.mainTextColor[1] = value;
        else if (key == "mainTextColorB") g_ui.mainTextColor[2] = value;
        else if (key == "shiftedTextColorR") g_ui.shiftedTextColor[0] = value;
        else if (key == "shiftedTextColorG") g_ui.shiftedTextColor[1] = value;
        else if (key == "shiftedTextColorB") g_ui.shiftedTextColor[2] = value;
        else if (key == "zhuyinTextColorR") g_ui.zhuyinTextColor[0] = value;
        else if (key == "zhuyinTextColorG") g_ui.zhuyinTextColor[1] = value;
        else if (key == "zhuyinTextColorB") g_ui.zhuyinTextColor[2] = value;
        else if (key == "zoneRedR") g_ui.zoneRed[0] = value;
        else if (key == "zoneRedG") g_ui.zoneRed[1] = value;
        else if (key == "zoneRedB") g_ui.zoneRed[2] = value;
        else if (key == "zoneOrangeR") g_ui.zoneOrange[0] = value;
        else if (key == "zoneOrangeG") g_ui.zoneOrange[1] = value;
        else if (key == "zoneOrangeB") g_ui.zoneOrange[2] = value;
        else if (key == "zoneGreenR") g_ui.zoneGreen[0] = value;
        else if (key == "zoneGreenG") g_ui.zoneGreen[1] = value;
        else if (key == "zoneGreenB") g_ui.zoneGreen[2] = value;
        else if (key == "zoneBlueR") g_ui.zoneBlue[0] = value;
        else if (key == "zoneBlueG") g_ui.zoneBlue[1] = value;
        else if (key == "zoneBlueB") g_ui.zoneBlue[2] = value;
        else if (key == "zonePurpleR") g_ui.zonePurple[0] = value;
        else if (key == "zonePurpleG") g_ui.zonePurple[1] = value;
        else if (key == "zonePurpleB") g_ui.zonePurple[2] = value;
    }
    g_ui.keyHeight = std::clamp(g_ui.keyHeight, 58.0f, 82.0f);
    g_ui.keyUnit = std::clamp(g_ui.keyUnit, 64.0f, 82.0f);
    g_ui.gap = std::clamp(g_ui.gap, 2.0f, 12.0f);
    g_ui.mainFont = std::clamp(g_ui.mainFont, 20.0f, 34.0f);
    g_ui.shiftedFont = std::clamp(g_ui.shiftedFont, 11.0f, 20.0f);
    g_ui.zhuyinFont = std::clamp(g_ui.zhuyinFont, 14.0f, 26.0f);
    g_ui.compactFont = std::clamp(g_ui.compactFont, 14.0f, 24.0f);
    g_ui.backgroundAlpha = std::clamp(g_ui.backgroundAlpha, 0.25f, 1.0f);
    g_ui.rgbIntensity = std::clamp(g_ui.rgbIntensity, 0.10f, 1.50f);
    g_ui.keyboardScale = std::clamp(g_ui.keyboardScale, 0.55f, 1.35f);
    g_ui.keyboardOffsetX = std::clamp(g_ui.keyboardOffsetX, -420.0f, 420.0f);
    g_ui.keyboardOffsetY = std::clamp(g_ui.keyboardOffsetY, -180.0f, 260.0f);
    g_ui.keyCornerRadius = std::clamp(g_ui.keyCornerRadius, 0.0f, 18.0f);
    g_ui.keyBorderThickness = std::clamp(g_ui.keyBorderThickness, 0.5f, 5.0f);
    g_ui.hapticStrength = std::clamp(g_ui.hapticStrength, 0.05f, 1.0f);
    g_ui.repeatDelay = std::clamp(g_ui.repeatDelay, 0.20f, 0.80f);
    g_ui.repeatRate = std::clamp(g_ui.repeatRate, 0.035f, 0.20f);
    g_ui.autoFadeSeconds = std::clamp(g_ui.autoFadeSeconds, 2.0f, 60.0f);
    g_ui.autoFadeAlpha = std::clamp(g_ui.autoFadeAlpha, 0.08f, 0.80f);
    g_ui.wristWidthMeters = std::clamp(g_ui.wristWidthMeters, 0.18f, 0.48f);
    g_ui.wristDotSizeMeters = std::clamp(g_ui.wristDotSizeMeters, 0.05f, 0.14f);
    g_ui.wristDotAlpha = std::clamp(g_ui.wristDotAlpha, 0.30f, 1.00f);
    g_ui.wristDoubleClickSeconds = std::clamp(g_ui.wristDoubleClickSeconds, 0.18f, 0.55f);
    g_ui.wristLongPressSeconds = std::clamp(g_ui.wristLongPressSeconds, 0.40f, 1.50f);
    g_ui.fontStyle = std::clamp(g_ui.fontStyle, 0, 1);
    g_shortcutBankIndex = (std::min)(g_shortcutBankIndex, g_shortcutBanks.size() - 1);
    auto clampColor = [](std::array<float, 3>& c) {
        for (float& v : c) v = std::clamp(v, 0.0f, 1.0f);
    };
    clampColor(g_ui.backgroundColor);
    clampColor(g_ui.keyColor);
    clampColor(g_ui.mainTextColor);
    clampColor(g_ui.shiftedTextColor);
    clampColor(g_ui.zhuyinTextColor);
    clampColor(g_ui.zoneRed);
    clampColor(g_ui.zoneOrange);
    clampColor(g_ui.zoneGreen);
    clampColor(g_ui.zoneBlue);
    clampColor(g_ui.zonePurple);
}

enum class AnchorMode { WorldFixed, HeadLocked, LeftHand, RightHand };
AnchorMode g_anchor = AnchorMode::WorldFixed;
vr::HmdMatrix34_t g_worldTransform{};
bool g_worldTransformValid = false;
float g_widthMeters = DEFAULT_OVERLAY_WIDTH_M;
float g_distance = 0.82f;
bool g_running = true;

vr::HmdMatrix34_t MakeTransform(float x, float y, float z, float pitchDegrees = 0.0f) {
    const float p = pitchDegrees * 3.1415926535f / 180.0f;
    const float c = std::cos(p), s = std::sin(p);
    vr::HmdMatrix34_t m{};
    m.m[0][0] = 1.0f; m.m[0][1] = 0.0f; m.m[0][2] = 0.0f; m.m[0][3] = x;
    m.m[1][0] = 0.0f; m.m[1][1] = c;    m.m[1][2] = -s;   m.m[1][3] = y;
    m.m[2][0] = 0.0f; m.m[2][1] = s;    m.m[2][2] = c;    m.m[2][3] = z;
    return m;
}

vr::HmdMatrix34_t Multiply34(const vr::HmdMatrix34_t& a, const vr::HmdMatrix34_t& b) {
    vr::HmdMatrix34_t r{};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j];
        }
        r.m[i][3] = a.m[i][0] * b.m[0][3] + a.m[i][1] * b.m[1][3] + a.m[i][2] * b.m[2][3] + a.m[i][3];
    }
    return r;
}

vr::HmdMatrix34_t InverseRigid34(const vr::HmdMatrix34_t& m) {
    vr::HmdMatrix34_t r{};
    // Rotation inverse = transpose for rigid transforms.
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            r.m[i][j] = m.m[j][i];

    const float tx = m.m[0][3], ty = m.m[1][3], tz = m.m[2][3];
    r.m[0][3] = -(r.m[0][0] * tx + r.m[0][1] * ty + r.m[0][2] * tz);
    r.m[1][3] = -(r.m[1][0] * tx + r.m[1][1] * ty + r.m[1][2] * tz);
    r.m[2][3] = -(r.m[2][0] * tx + r.m[2][1] * ty + r.m[2][2] * tz);
    return r;
}

bool GetAbsoluteDevicePose(vr::TrackedDeviceIndex_t device, vr::HmdMatrix34_t& out) {
    auto* sys = vr::VRSystem();
    if (!sys || device == vr::k_unTrackedDeviceIndexInvalid || device >= vr::k_unMaxTrackedDeviceCount) return false;
    vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount]{};
    sys->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding, 0.0f, poses, vr::k_unMaxTrackedDeviceCount);
    if (!poses[device].bPoseIsValid) return false;
    out = poses[device].mDeviceToAbsoluteTracking;
    return true;
}

bool ResolveAnchorRelative(AnchorMode mode, vr::TrackedDeviceIndex_t& device, vr::HmdMatrix34_t& relative) {
    auto* sys = vr::VRSystem();
    if (!sys) return false;

    if (mode == AnchorMode::HeadLocked) {
        device = vr::k_unTrackedDeviceIndex_Hmd;
        relative = MakeTransform(0.0f, -0.32f, -g_distance, -8.0f);
        return true;
    }

    if (mode == AnchorMode::LeftHand || mode == AnchorMode::RightHand) {
        const vr::ETrackedControllerRole role = (mode == AnchorMode::LeftHand)
            ? vr::TrackedControllerRole_LeftHand
            : vr::TrackedControllerRole_RightHand;
        device = sys->GetTrackedDeviceIndexForControllerRole(role);
        if (device == vr::k_unTrackedDeviceIndexInvalid) return false;
        relative = MakeTransform(0.0f, 0.08f, -0.28f, -35.0f);
        return true;
    }

    return false;
}

bool ResolveWristRelative(vr::TrackedDeviceIndex_t& device, vr::HmdMatrix34_t& relative) {
    auto* sys = vr::VRSystem();
    if (!sys) return false;

    const vr::ETrackedControllerRole primaryRole = g_ui.wristUseLeftHand
        ? vr::TrackedControllerRole_LeftHand
        : vr::TrackedControllerRole_RightHand;
    const vr::ETrackedControllerRole fallbackRole = g_ui.wristUseLeftHand
        ? vr::TrackedControllerRole_RightHand
        : vr::TrackedControllerRole_LeftHand;

    device = sys->GetTrackedDeviceIndexForControllerRole(primaryRole);
    if (device == vr::k_unTrackedDeviceIndexInvalid)
        device = sys->GetTrackedDeviceIndexForControllerRole(fallbackRole);

    if (device == vr::k_unTrackedDeviceIndexInvalid) {
        // Desktop-like fallback in VR if neither controller is currently tracked.
        device = vr::k_unTrackedDeviceIndex_Hmd;
        relative = MakeTransform(-0.22f, -0.24f, -0.46f, -10.0f);
        return true;
    }

    // Compact panel slightly above/in front of the controller. This is intentionally
    // conservative and can be tuned after the final VR hardware test.
    relative = MakeTransform(0.0f, 0.055f, -0.14f, -55.0f);
    return true;
}

bool GetCurrentOverlayAbsolute(vr::HmdMatrix34_t& out) {
    if (g_wristStandby) {
        vr::TrackedDeviceIndex_t wristDevice = vr::k_unTrackedDeviceIndexInvalid;
        vr::HmdMatrix34_t wristRelative{};
        vr::HmdMatrix34_t wristAbs{};
        if (!ResolveWristRelative(wristDevice, wristRelative) || !GetAbsoluteDevicePose(wristDevice, wristAbs)) return false;
        out = Multiply34(wristAbs, wristRelative);
        return true;
    }

    if (g_anchor == AnchorMode::WorldFixed && g_worldTransformValid) {
        out = g_worldTransform;
        return true;
    }

    vr::TrackedDeviceIndex_t device = vr::k_unTrackedDeviceIndexInvalid;
    vr::HmdMatrix34_t relative{};
    if (!ResolveAnchorRelative(g_anchor, device, relative)) {
        // If the selected hand is not tracked, use the head-locked fallback
        // because ApplyAnchor() uses the same fallback behavior.
        device = vr::k_unTrackedDeviceIndex_Hmd;
        relative = MakeTransform(0.0f, -0.32f, -g_distance, -8.0f);
    }

    vr::HmdMatrix34_t deviceAbs{};
    if (!GetAbsoluteDevicePose(device, deviceAbs)) return false;
    out = Multiply34(deviceAbs, relative);
    return true;
}

vr::TrackedDeviceIndex_t ChooseGrabController() {
    auto* sys = vr::VRSystem();
    if (!sys) return vr::k_unTrackedDeviceIndexInvalid;

    auto validController = [&](vr::TrackedDeviceIndex_t d) {
        return d != vr::k_unTrackedDeviceIndexInvalid &&
               d < vr::k_unMaxTrackedDeviceCount &&
               sys->GetTrackedDeviceClass(d) == vr::TrackedDeviceClass_Controller;
    };

    if (validController(g_lastPointerDevice)) return g_lastPointerDevice;

    const vr::TrackedDeviceIndex_t right = sys->GetTrackedDeviceIndexForControllerRole(vr::TrackedControllerRole_RightHand);
    if (validController(right)) return right;
    const vr::TrackedDeviceIndex_t left = sys->GetTrackedDeviceIndexForControllerRole(vr::TrackedControllerRole_LeftHand);
    if (validController(left)) return left;
    return vr::k_unTrackedDeviceIndexInvalid;
}

bool BeginVrGrab(vr::VROverlayHandle_t overlay) {
    if (g_previewMode || g_wristStandby || overlay == vr::k_ulOverlayHandleInvalid || g_vrGrabActive) return false;
    auto* ov = vr::VROverlay();
    if (!ov) return false;

    const vr::TrackedDeviceIndex_t controller = ChooseGrabController();
    if (controller == vr::k_unTrackedDeviceIndexInvalid) return false;

    vr::HmdMatrix34_t controllerAbs{};
    vr::HmdMatrix34_t overlayAbs{};
    if (!GetAbsoluteDevicePose(controller, controllerAbs) || !GetCurrentOverlayAbsolute(overlayAbs)) return false;

    g_vrGrabDevice = controller;
    g_vrGrabRelative = Multiply34(InverseRigid34(controllerAbs), overlayAbs);
    g_vrGrabActive = true;
    ov->SetOverlayTransformTrackedDeviceRelative(overlay, controller, &g_vrGrabRelative);
    return true;
}

void EndVrGrab(vr::VROverlayHandle_t overlay) {
    if (!g_vrGrabActive) return;
    auto* ov = vr::VROverlay();

    vr::HmdMatrix34_t controllerAbs{};
    if (ov && GetAbsoluteDevicePose(g_vrGrabDevice, controllerAbs)) {
        g_worldTransform = Multiply34(controllerAbs, g_vrGrabRelative);
        g_worldTransformValid = true;
        g_anchor = AnchorMode::WorldFixed;
        ov->SetOverlayTransformAbsolute(overlay, vr::TrackingUniverseStanding, &g_worldTransform);
        ov->SetOverlayWidthInMeters(overlay, g_widthMeters);
    }

    g_vrGrabActive = false;
    g_vrGrabDevice = vr::k_unTrackedDeviceIndexInvalid;
}

bool GetHmdFrontAbsolute(vr::HmdMatrix34_t& out) {
    vr::HmdMatrix34_t hmdAbs{};
    if (!GetAbsoluteDevicePose(vr::k_unTrackedDeviceIndex_Hmd, hmdAbs)) return false;
    const vr::HmdMatrix34_t relative = MakeTransform(0.0f, -0.32f, -g_distance, -8.0f);
    out = Multiply34(hmdAbs, relative);
    return true;
}

bool SaveReturnPosition(vr::VROverlayHandle_t overlay) {
    g_returnWidthMeters = g_widthMeters;
    if (g_previewMode) {
        g_returnWorldTransformValid = true;
        g_vrPlacementStatus = "桌面模擬：已記住返回位置";
        return true;
    }

    vr::HmdMatrix34_t current{};
    if (!GetCurrentOverlayAbsolute(current)) {
        g_vrPlacementStatus = "無法取得目前位置";
        return false;
    }
    g_returnWorldTransform = current;
    g_returnWorldTransformValid = true;
    g_vrPlacementStatus = "已記住返回位置";
    return true;
}

void SetFullOverlayVisualBounds(vr::VROverlayHandle_t overlay) {
    if (g_previewMode || overlay == vr::k_ulOverlayHandleInvalid) return;
    auto* ov = vr::VROverlay();
    if (!ov) return;
    vr::VRTextureBounds_t bounds{0.0f, 0.0f, 1.0f, 1.0f};
    ov->SetOverlayTextureBounds(overlay, &bounds);
    vr::HmdVector2_t mouseScale{{(float)TEX_W, (float)TEX_H}};
    ov->SetOverlayMouseScale(overlay, &mouseScale);
    ov->SetOverlayWidthInMeters(overlay, g_widthMeters);
}

void SetWristDotVisualBounds(vr::VROverlayHandle_t overlay) {
    if (g_previewMode || overlay == vr::k_ulOverlayHandleInvalid) return;
    auto* ov = vr::VROverlay();
    if (!ov) return;
    // Crop the left TEX_H x TEX_H square from the wide render texture. This
    // makes the physical OpenVR overlay square while keeping a single shared
    // render target for both the full keyboard and wrist dot.
    const float uMax = (float)TEX_H / (float)TEX_W;
    vr::VRTextureBounds_t bounds{0.0f, 0.0f, uMax, 1.0f};
    ov->SetOverlayTextureBounds(overlay, &bounds);
    vr::HmdVector2_t mouseScale{{(float)TEX_H, (float)TEX_H}};
    ov->SetOverlayMouseScale(overlay, &mouseScale);
}

void ApplyWristStandbyTransform(vr::VROverlayHandle_t overlay) {
    if (g_previewMode || overlay == vr::k_ulOverlayHandleInvalid) return;
    auto* ov = vr::VROverlay();
    if (!ov) return;

    vr::TrackedDeviceIndex_t device = vr::k_unTrackedDeviceIndexInvalid;
    vr::HmdMatrix34_t relative{};
    if (!ResolveWristRelative(device, relative)) return;
    ov->SetOverlayTransformTrackedDeviceRelative(overlay, device, &relative);
    SetWristDotVisualBounds(overlay);
    ov->SetOverlayWidthInMeters(overlay, g_ui.wristDotSizeMeters);
    ov->SetOverlayAlpha(overlay, 1.0f);
}

void EnterWristStandby(vr::VROverlayHandle_t overlay) {
    if (!g_wristStandby) SaveReturnPosition(overlay);
    g_vrGrabActive = false;
    g_wristStandby = true;
    g_wristPendingSingle = false;
    g_wristLongPressTriggered = false;
    g_wristLastClickTime = -10.0;
    g_vrPlacementStatus = g_returnWorldTransformValid
        ? "手腕圓點待機中｜返回位置已保留"
        : "手腕圓點待機中";
    ApplyWristStandbyTransform(overlay);
}

void SummonKeyboard(vr::VROverlayHandle_t overlay, bool preserveExistingReturn = false) {
    if (!preserveExistingReturn) SaveReturnPosition(overlay);
    g_wristStandby = false;
    g_wristPendingSingle = false;
    g_vrGrabActive = false;
    g_anchor = AnchorMode::WorldFixed;
    SetFullOverlayVisualBounds(overlay);

    if (g_previewMode) {
        g_vrPlacementStatus = "桌面模擬：鍵盤已召喚到眼前";
        return;
    }

    vr::HmdMatrix34_t summoned{};
    if (!GetHmdFrontAbsolute(summoned)) {
        g_vrPlacementStatus = "召喚失敗：HMD 位置無效";
        return;
    }
    g_worldTransform = summoned;
    g_worldTransformValid = true;
    if (auto* ov = vr::VROverlay()) {
        ov->SetOverlayTransformAbsolute(overlay, vr::TrackingUniverseStanding, &g_worldTransform);
        ov->SetOverlayWidthInMeters(overlay, g_widthMeters);
        ov->SetOverlayAlpha(overlay, 1.0f);
    }
    g_vrPlacementStatus = "已召喚到眼前｜可按返回原位";
}

void ReturnKeyboard(vr::VROverlayHandle_t overlay) {
    if (!g_returnWorldTransformValid) {
        g_vrPlacementStatus = "目前沒有可返回的位置";
        return;
    }

    g_wristStandby = false;
    g_wristPendingSingle = false;
    g_vrGrabActive = false;
    g_anchor = AnchorMode::WorldFixed;
    g_widthMeters = g_returnWidthMeters;
    SetFullOverlayVisualBounds(overlay);

    if (!g_previewMode) {
        g_worldTransform = g_returnWorldTransform;
        g_worldTransformValid = true;
        if (auto* ov = vr::VROverlay()) {
            ov->SetOverlayTransformAbsolute(overlay, vr::TrackingUniverseStanding, &g_worldTransform);
            ov->SetOverlayWidthInMeters(overlay, g_widthMeters);
            ov->SetOverlayAlpha(overlay, 1.0f);
        }
    }
    g_vrPlacementStatus = "已返回原本世界位置";
}

void ExpandWristKeyboard(vr::VROverlayHandle_t overlay) {
    g_wristStandby = false;
    g_wristPendingSingle = false;
    g_vrGrabActive = false;
    g_anchor = g_ui.wristUseLeftHand ? AnchorMode::LeftHand : AnchorMode::RightHand;
    SetFullOverlayVisualBounds(overlay);

    if (g_previewMode) {
        g_vrPlacementStatus = "桌面模擬：單擊圓點，已展開完整鍵盤";
        return;
    }

    auto* sys = vr::VRSystem();
    auto* ov = vr::VROverlay();
    if (!sys || !ov) return;
    const vr::ETrackedControllerRole role = g_ui.wristUseLeftHand
        ? vr::TrackedControllerRole_LeftHand
        : vr::TrackedControllerRole_RightHand;
    vr::TrackedDeviceIndex_t device = sys->GetTrackedDeviceIndexForControllerRole(role);
    if (device == vr::k_unTrackedDeviceIndexInvalid) {
        g_anchor = AnchorMode::HeadLocked;
        const vr::HmdMatrix34_t relative = MakeTransform(0.0f, -0.32f, -g_distance, -8.0f);
        ov->SetOverlayTransformTrackedDeviceRelative(overlay, vr::k_unTrackedDeviceIndex_Hmd, &relative);
        ov->SetOverlayWidthInMeters(overlay, g_returnWorldTransformValid ? g_returnWidthMeters : g_widthMeters);
        ov->SetOverlayAlpha(overlay, 1.0f);
        g_vrPlacementStatus = "手腕控制器未追蹤，完整鍵盤改為固定視野";
        return;
    }

    // Expand the full keyboard in front of the configured wrist hand, far
    // enough away to keep the normal full-size width usable.
    const vr::HmdMatrix34_t relative = MakeTransform(0.0f, -0.10f, -0.78f, -18.0f);
    ov->SetOverlayTransformTrackedDeviceRelative(overlay, device, &relative);
    ov->SetOverlayWidthInMeters(overlay, g_returnWorldTransformValid ? g_returnWidthMeters : g_widthMeters);
    ov->SetOverlayAlpha(overlay, 1.0f);
    g_vrPlacementStatus = "手腕圓點單擊：已展開完整鍵盤";
}

bool CaptureWorldTransform(vr::VROverlayHandle_t overlay) {
    auto* ov = vr::VROverlay();
    if (!ov) return false;

    vr::HmdMatrix34_t front{};
    if (!GetHmdFrontAbsolute(front)) return false;
    g_worldTransform = front;
    g_worldTransformValid = true;
    ov->SetOverlayTransformAbsolute(overlay, vr::TrackingUniverseStanding, &g_worldTransform);
    return true;
}

void ApplyAnchor(vr::VROverlayHandle_t overlay, bool recaptureWorld = false) {
    if (g_vrGrabActive) return;
    if (g_wristStandby) { ApplyWristStandbyTransform(overlay); return; }
    auto* sys = vr::VRSystem();
    auto* ov = vr::VROverlay();
    if (!sys || !ov) return;

    if (g_anchor == AnchorMode::WorldFixed) {
        if (recaptureWorld || !g_worldTransformValid) CaptureWorldTransform(overlay);
        else ov->SetOverlayTransformAbsolute(overlay, vr::TrackingUniverseStanding, &g_worldTransform);
        ov->SetOverlayWidthInMeters(overlay, g_widthMeters);
        return;
    }

    vr::TrackedDeviceIndex_t device = vr::k_unTrackedDeviceIndex_Hmd;
    vr::HmdMatrix34_t t{};

    if (g_anchor == AnchorMode::HeadLocked) {
        t = MakeTransform(0.0f, -0.32f, -g_distance, -8.0f);
    } else {
        const vr::ETrackedControllerRole role = (g_anchor == AnchorMode::LeftHand)
            ? vr::TrackedControllerRole_LeftHand
            : vr::TrackedControllerRole_RightHand;
        device = sys->GetTrackedDeviceIndexForControllerRole(role);
        if (device == vr::k_unTrackedDeviceIndexInvalid) {
            g_anchor = AnchorMode::HeadLocked;
            device = vr::k_unTrackedDeviceIndex_Hmd;
            t = MakeTransform(0.0f, -0.32f, -g_distance, -8.0f);
        } else {
            t = MakeTransform(0.0f, 0.08f, -0.28f, -35.0f);
        }
    }

    ov->SetOverlayTransformTrackedDeviceRelative(overlay, device, &t);
    ov->SetOverlayWidthInMeters(overlay, g_widthMeters);
}

const char* InputModeLabel() {
    HWND fg = GetForegroundWindow();
    if (!fg) return "EN";
    const DWORD tid = GetWindowThreadProcessId(fg, nullptr);
    const HKL hkl = GetKeyboardLayout(tid);
    const LANGID lang = LOWORD(reinterpret_cast<ULONG_PTR>(hkl));
    const WORD primary = PRIMARYLANGID(lang);
    if (primary == LANG_CHINESE) {
        HIMC imc = ImmGetContext(fg);
        if (imc) {
            const bool open = ImmGetOpenStatus(imc) != FALSE;
            ImmReleaseContext(fg, imc);
            return open ? "中" : "EN";
        }
        return "中";
    }
    if (primary == LANG_JAPANESE) return "日";
    if (primary == LANG_KOREAN) return "韓";
    return "EN";
}

std::wstring ReadClipboardUnicodeText() {
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) return {};
    if (!OpenClipboard(nullptr)) return {};
    std::wstring result;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (h) {
        const wchar_t* p = static_cast<const wchar_t*>(GlobalLock(h));
        if (p) {
            result = p;
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    return result;
}

void UpdateClipboardHistory(bool force = false) {
    const auto now = std::chrono::steady_clock::now();
    if (!force && g_lastClipboardPoll.time_since_epoch().count() != 0 &&
        std::chrono::duration<float>(now - g_lastClipboardPoll).count() < 0.35f) return;
    g_lastClipboardPoll = now;

    const DWORD seq = GetClipboardSequenceNumber();
    if (!force && seq != 0 && seq == g_lastClipboardSequence) return;
    g_lastClipboardSequence = seq;

    std::wstring text = ReadClipboardUnicodeText();
    if (text.empty()) return;
    if (!g_clipboardHistory.empty() && g_clipboardHistory.front() == text) return;
    g_clipboardHistory.erase(std::remove(g_clipboardHistory.begin(), g_clipboardHistory.end(), text), g_clipboardHistory.end());
    g_clipboardHistory.push_front(std::move(text));
    while (g_clipboardHistory.size() > 8) g_clipboardHistory.pop_back();
}

std::string ClipboardPreviewLabel(size_t index, const std::wstring& text) {
    size_t visibleChars = 0;
    bool asciiOnly = true;
    std::wstring firstLine;
    for (wchar_t ch : text) {
        if (ch == L'\r' || ch == L'\n') break;
        if (visibleChars >= 22) break;
        firstLine.push_back(ch);
        if (ch > 0x7F) asciiOnly = false;
        ++visibleChars;
    }
    std::ostringstream oss;
    oss << "剪貼簿 " << (index + 1) << "  (" << text.size() << " 字)";
    if (asciiOnly && !firstLine.empty()) oss << "  " << WideToUtf8(firstLine);
    return oss.str();
}

void MarkVrInteraction() {
    g_lastVrInteraction = std::chrono::steady_clock::now();
}

void UpdateAutoFade(vr::VROverlayHandle_t overlay) {
    if (g_previewMode || overlay == vr::k_ulOverlayHandleInvalid) return;
    auto* ov = vr::VROverlay();
    if (!ov) return;
    const float idle = std::chrono::duration<float>(std::chrono::steady_clock::now() - g_lastVrInteraction).count();
    // Keep the tiny wrist launcher visible; auto-fade only applies to the full keyboard.
    const float target = g_wristStandby ? 1.0f
        : ((g_ui.autoFadeEnabled && idle >= g_ui.autoFadeSeconds) ? g_ui.autoFadeAlpha : 1.0f);
    const float dt = std::clamp(ImGui::GetIO().DeltaTime, 0.0f, 0.10f);
    const float speed = (target > g_overlayAlpha) ? 12.0f : 5.0f;
    g_overlayAlpha += (target - g_overlayAlpha) * std::clamp(dt * speed, 0.0f, 1.0f);
    if (std::fabs(target - g_overlayAlpha) < 0.002f) g_overlayAlpha = target;
    ov->SetOverlayAlpha(overlay, std::clamp(g_overlayAlpha, 0.05f, 1.0f));
}

void DrawMoveHandle(vr::VROverlayHandle_t overlay) {
    if (g_previewMode && !g_editorMode) return;

    const char* label = g_previewMode ? "拖曳鍵盤" : (g_vrGrabActive ? "移動中..." : "抓住移動");
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.26f, 0.38f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.36f, 0.54f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.12f, 0.48f, 0.68f, 1.0f));
    ImGui::Button(label, ImVec2(112, 48));
    ImGui::PopStyleColor(3);

    if (g_previewMode) {
        if (ImGui::IsItemActivated()) {
            g_desktopKeyboardDragging = true;
            g_desktopDragStartMouse = ImGui::GetIO().MousePos;
            g_desktopDragStartOffsetX = g_ui.keyboardOffsetX;
            g_desktopDragStartOffsetY = g_ui.keyboardOffsetY;
        }
        if (g_desktopKeyboardDragging && ImGui::IsItemActive()) {
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            g_ui.keyboardOffsetX = std::clamp(g_desktopDragStartOffsetX + mouse.x - g_desktopDragStartMouse.x, -420.0f, 420.0f);
            g_ui.keyboardOffsetY = std::clamp(g_desktopDragStartOffsetY + mouse.y - g_desktopDragStartMouse.y, -180.0f, 260.0f);
        }
        if (g_desktopKeyboardDragging && ImGui::IsItemDeactivated()) {
            g_desktopKeyboardDragging = false;
            if (g_autoSave) SaveSettings();
        }
    } else if (ImGui::IsItemActivated()) {
        BeginVrGrab(overlay);
    }

    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(g_previewMode
            ? "按住滑鼠左鍵拖曳，可直接調整鍵盤 X / Y 位置。"
            : "用控制器指向此按鈕並按住扳機移動；放開後自動切換為世界固定。");
    }
}

void DrawTopBar(vr::VROverlayHandle_t overlay) {
    auto apply = [&](bool recapture = false) {
        if (!g_previewMode) ApplyAnchor(overlay, recapture);
    };
    auto cancelWrist = [&]() {
        if (g_wristStandby) SetFullOverlayVisualBounds(overlay);
        g_wristStandby = false;
        g_wristPendingSingle = false;
        g_vrGrabActive = false;
    };

    // Row 1: page + placement. Splitting the toolbar into two rows keeps V3.4
    // readable even after adding wrist / summon controls.
    if (SelectedButton("鍵盤", g_page == PageMode::Keyboard && !g_ui.q9Mode, ImVec2(82, 44))) {
        g_page = PageMode::Keyboard;
        g_ui.q9Mode = false;
        if (g_autoSave) SaveSettings();
    }
    ImGui::SameLine();
    if (SelectedButton("九方模式", g_page == PageMode::Keyboard && g_ui.q9Mode, ImVec2(108, 44))) {
        g_page = PageMode::Keyboard;
        g_ui.q9Mode = !g_ui.q9Mode;
        if (g_autoSave) SaveSettings();
    }
    ImGui::SameLine();
    if (SelectedButton("快捷鍵", g_page == PageMode::Shortcuts, ImVec2(92, 44))) g_page = PageMode::Shortcuts;
    ImGui::SameLine();
    if (SelectedButton("剪貼簿", g_page == PageMode::Clipboard, ImVec2(92, 44))) {
        g_page = PageMode::Clipboard;
        UpdateClipboardHistory(true);
    }
    ImGui::SameLine();
    const UpdateCheckResult updateSnapshot = GetUpdateSnapshot();
    const char* updateLabel = updateSnapshot.state == UpdateCheckState::Available ? "更新!" : "更新";
    if (SelectedButton(updateLabel, g_page == PageMode::Update, ImVec2(82, 44))) g_page = PageMode::Update;
    ImGui::SameLine(0, 18.0f);
    if (SelectedButton("世界固定", !g_wristStandby && g_anchor == AnchorMode::WorldFixed, ImVec2(112,44))) {
        cancelWrist(); g_anchor = AnchorMode::WorldFixed; apply(true);
    }
    ImGui::SameLine();
    if (SelectedButton("固定視野", !g_wristStandby && g_anchor == AnchorMode::HeadLocked, ImVec2(108,44))) {
        cancelWrist(); g_anchor = AnchorMode::HeadLocked; apply();
    }
    ImGui::SameLine();
    if (SelectedButton("左手", !g_wristStandby && g_anchor == AnchorMode::LeftHand, ImVec2(78,44))) {
        cancelWrist(); g_anchor = AnchorMode::LeftHand; apply();
    }
    ImGui::SameLine();
    if (SelectedButton("右手", !g_wristStandby && g_anchor == AnchorMode::RightHand, ImVec2(78,44))) {
        cancelWrist(); g_anchor = AnchorMode::RightHand; apply();
    }
    ImGui::SameLine(0, 18.0f);
    if (SelectedButton("手腕圓點", g_wristStandby, ImVec2(112,44))) EnterWristStandby(overlay);
    ImGui::SameLine();
    if (ImGui::Button("召喚", ImVec2(82,44))) SummonKeyboard(overlay, g_wristStandby);
    ImGui::SameLine();
    if (!g_returnWorldTransformValid) ImGui::BeginDisabled();
    if (ImGui::Button("返回原位", ImVec2(108,44))) ReturnKeyboard(overlay);
    if (!g_returnWorldTransformValid) ImGui::EndDisabled();

    // Row 2: manipulation + utility.
    if (!g_previewMode || g_editorMode) DrawMoveHandle(overlay);
    if (!g_previewMode || g_editorMode) ImGui::SameLine();
    if (ImGui::Button("縮小", ImVec2(78,42))) {
        g_widthMeters = (std::max)(0.80f, g_widthMeters - 0.10f); apply();
    }
    ImGui::SameLine();
    if (ImGui::Button("放大", ImVec2(78,42))) {
        g_widthMeters = (std::min)(2.50f, g_widthMeters + 0.10f); apply();
    }
    ImGui::SameLine();
    if (ImGui::Button("靠近", ImVec2(78,42))) {
        g_distance = (std::max)(0.45f, g_distance - 0.08f); apply();
    }
    ImGui::SameLine();
    if (ImGui::Button("遠離", ImVec2(78,42))) {
        g_distance = (std::min)(1.50f, g_distance + 0.08f); apply();
    }
    ImGui::SameLine(0, 18.0f);
    if (ImGui::Button("清除組合鍵", ImVec2(126,42))) g_mods.clear();
    ImGui::SameLine(0, 18.0f);
    ImGui::TextDisabled("%s", g_vrPlacementStatus.c_str());
    if (g_previewMode) {
        ImGui::SameLine(0, 18.0f);
        if (SelectedButton("編輯預覽", g_editorMode, ImVec2(108,42))) g_editorMode = !g_editorMode;
    }
    ImGui::SameLine();
    if (ImGui::Button("關閉", ImVec2(74,42))) g_running = false;
}

void DrawShortcuts() {
    ImGui::TextUnformatted("常用快捷鍵");
    ImGui::Spacing();
    const ImVec2 s(115, 50);
    if (PhysicalKeyButton("sc_copy", "Ctrl+C", "", "", s, KeyZone::Red, false, true)) SendChord({VK_CONTROL, 'C'}); ImGui::SameLine(0,g_ui.gap);
    if (PhysicalKeyButton("sc_paste", "Ctrl+V", "", "", s, KeyZone::Red, false, true)) SendChord({VK_CONTROL, 'V'}); ImGui::SameLine(0,g_ui.gap);
    if (PhysicalKeyButton("sc_cut", "Ctrl+X", "", "", s, KeyZone::Orange, false, true)) SendChord({VK_CONTROL, 'X'}); ImGui::SameLine(0,g_ui.gap);
    if (PhysicalKeyButton("sc_undo", "Ctrl+Z", "", "", s, KeyZone::Orange, false, true)) SendChord({VK_CONTROL, 'Z'}); ImGui::SameLine(0,g_ui.gap);
    if (PhysicalKeyButton("sc_redo", "Ctrl+Y", "", "", s, KeyZone::Green, false, true)) SendChord({VK_CONTROL, 'Y'}); ImGui::SameLine(0,g_ui.gap);
    if (PhysicalKeyButton("sc_all", "Ctrl+A", "", "", s, KeyZone::Green, false, true)) SendChord({VK_CONTROL, 'A'}); ImGui::SameLine(0,g_ui.gap);
    if (PhysicalKeyButton("sc_alttab", "Alt+Tab", "", "", s, KeyZone::Blue, false, true)) SendChord({VK_MENU, VK_TAB}); ImGui::SameLine(0,g_ui.gap);
    if (PhysicalKeyButton("sc_wind", "Win+D", "", "", s, KeyZone::Blue, false, true)) SendChord({VK_LWIN, 'D'});

    ImGui::Spacing();
    if (PhysicalKeyButton("sc_lang", "Win+Space", "", "", ImVec2(135,50), KeyZone::Green, false, true)) SendChord({VK_LWIN, VK_SPACE}); ImGui::SameLine(0,g_ui.gap);
    if (PhysicalKeyButton("sc_shot", "Win+Shift+S", "", "", ImVec2(155,50), KeyZone::Blue, false, true)) SendChord({VK_LWIN, VK_SHIFT, 'S'}); ImGui::SameLine(0,g_ui.gap);
    if (PhysicalKeyButton("sc_task", "工作管理員", "", "", ImVec2(145,50), KeyZone::Purple, false, true)) SendChord({VK_CONTROL, VK_SHIFT, VK_ESCAPE}); ImGui::SameLine(0,g_ui.gap);
    if (PhysicalKeyButton("sc_mute", "靜音", "", "", ImVec2(90,50), KeyZone::Purple, false, true)) SendTapRaw(VK_VOLUME_MUTE); ImGui::SameLine(0,g_ui.gap);
    if (PhysicalKeyButton("sc_voldown", "音量-", "", "", ImVec2(90,50), KeyZone::Purple, false, true, true)) SendTapRaw(VK_VOLUME_DOWN); ImGui::SameLine(0,g_ui.gap);
    if (PhysicalKeyButton("sc_volup", "音量+", "", "", ImVec2(90,50), KeyZone::Purple, false, true, true)) SendTapRaw(VK_VOLUME_UP);

    ImGui::Spacing();
    ImGui::SeparatorText("自訂快捷頁 / 巨集");
    for (size_t b = 0; b < g_shortcutBanks.size(); ++b) {
        ImGui::PushID(static_cast<int>(1000 + b));
        const char* bankName = g_shortcutBanks[b].name[0] ? g_shortcutBanks[b].name : "未命名頁";
        if (SelectedButton(bankName, g_shortcutBankIndex == b, ImVec2(145, 40))) {
            g_shortcutBankIndex = b;
            if (g_autoSave) SaveSettings();
        }
        if (b + 1 < g_shortcutBanks.size()) ImGui::SameLine(0, 8.0f);
        ImGui::PopID();
    }
    ImGui::SameLine(0, 18.0f);
    ImGui::TextDisabled("巨集：%s", g_macroStatus.c_str());

    ImGui::Spacing();
    auto& slots = CurrentShortcutSlots();
    for (size_t i = 0; i < slots.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        const KeyZone zone = static_cast<KeyZone>(static_cast<int>(i) % 5);
        const char* label = slots[i].label[0] ? slots[i].label : "未命名";
        if (PhysicalKeyButton("custom", label, "", "", ImVec2(205,58), zone, false, true)) {
            ExecuteActionSpec(slots[i].action);
        }
        if ((i % 4) != 3) ImGui::SameLine(0, g_ui.gap);
        ImGui::PopID();
    }
    ImGui::TextDisabled("可在桌面編輯器切換三個快捷頁。支援 CTRL+C、TEXT:文字、MACRO:CTRL+C;WAIT:200;CTRL+V。");
}

void DrawClipboardPage() {
    UpdateClipboardHistory();
    ImGui::TextUnformatted("剪貼簿歷史｜點一下直接輸入完整內容");
    ImGui::SameLine(0, 18.0f);
    if (ImGui::Button("重新讀取", ImVec2(105, 38))) UpdateClipboardHistory(true);
    ImGui::SameLine();
    if (ImGui::Button("清除歷史", ImVec2(105, 38))) g_clipboardHistory.clear();
    ImGui::Separator();

    if (g_clipboardHistory.empty()) {
        ImGui::TextDisabled("目前沒有可用的文字剪貼簿內容。複製一段文字後再打開此頁即可。");
        return;
    }

    for (size_t i = 0; i < g_clipboardHistory.size(); ++i) {
        const std::string label = ClipboardPreviewLabel(i, g_clipboardHistory[i]);
        ImGui::PushID(static_cast<int>(i));
        if (PhysicalKeyButton("clip", label.c_str(), "", "", ImVec2(560, 58), KeyZone::Blue, false, true)) {
            SendUnicodeText(g_clipboardHistory[i]);
        }
        ImGui::SameLine(0, 12.0f);
        ImGui::TextDisabled("點擊輸入");
        ImGui::PopID();
    }
    ImGui::Spacing();
    ImGui::TextWrapped("為了維持中文字型 Atlas 輕量，含大量未預載中文字的內容只顯示「剪貼簿 N（字數）」；點擊時仍會輸入完整原文。");
}

void DrawUpdatePage() {
    const UpdateCheckResult result = GetUpdateSnapshot();

    ImGui::Text("VR Full Keyboard　目前版本：%s", VRFK_VERSION_STRING);
    ImGui::Spacing();
    ImGui::SeparatorText("GitHub 更新");

    if (UpdateRepoConfigured()) {
        ImGui::Text("更新來源：%s / %s", VRFK_GITHUB_OWNER, VRFK_GITHUB_REPO);
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.28f, 1.0f), "尚未綁定 GitHub Repository");
        ImGui::TextWrapped("此建置版本沒有設定官方 GitHub 更新來源。");
    }

    bool autoCheck = g_ui.autoCheckUpdates;
    if (ImGui::Checkbox("啟動時自動檢查更新", &autoCheck)) {
        g_ui.autoCheckUpdates = autoCheck;
        if (g_autoSave) SaveSettings();
    }

    ImGui::Spacing();
    if (g_updateChecking.load() || !UpdateRepoConfigured()) ImGui::BeginDisabled();
    if (ImGui::Button("檢查更新", ImVec2(150, 44))) StartUpdateCheck();
    if (g_updateChecking.load() || !UpdateRepoConfigured()) ImGui::EndDisabled();

    ImGui::SameLine(0, 16.0f);
    switch (result.state) {
        case UpdateCheckState::Checking:
            ImGui::TextColored(ImVec4(0.45f, 0.72f, 1.0f, 1.0f), "正在檢查...");
            break;
        case UpdateCheckState::Available:
            ImGui::TextColored(ImVec4(0.38f, 0.92f, 0.55f, 1.0f), "%s：%s", result.message.c_str(), result.latestVersion.c_str());
            break;
        case UpdateCheckState::UpToDate:
            ImGui::TextColored(ImVec4(0.38f, 0.92f, 0.55f, 1.0f), "%s", result.message.c_str());
            break;
        case UpdateCheckState::Error:
            ImGui::TextColored(ImVec4(1.0f, 0.42f, 0.42f, 1.0f), "%s", result.message.c_str());
            break;
        case UpdateCheckState::NotConfigured:
            ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.28f, 1.0f), "%s", result.message.c_str());
            break;
        default:
            ImGui::TextDisabled("%s", result.message.c_str());
            break;
    }

    if (!result.latestVersion.empty()) {
        ImGui::SeparatorText("最新 Release");
        ImGui::Text("版本：%s", result.latestVersion.c_str());
        if (!result.releaseName.empty()) ImGui::TextWrapped("名稱：%s", result.releaseName.c_str());
        if (!result.releaseNotes.empty()) {
            ImGui::TextUnformatted("更新內容：");
            ImGui::BeginChild("update_release_notes", ImVec2(-1.0f, 190.0f), ImGuiChildFlags_Borders,
                              ImGuiWindowFlags_AlwaysVerticalScrollbar);
            ImGui::TextWrapped("%s", result.releaseNotes.c_str());
            ImGui::EndChild();
        }
        if (!result.releaseUrl.empty()) {
            if (ImGui::Button("開啟 GitHub Release", ImVec2(190, 42))) OpenLatestReleasePage();
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("VR 畫面內可檢查 Release 資訊；下載與安裝請使用控制中心的「更新與版本」。");
    ImGui::TextDisabled("控制中心會驗證 SHA256、備份舊版並以獨立更新器安全替換檔案。");
}

void DrawFunctionRow() {
    const float h = 58.0f;
    auto F = [h](const char* id, const char* label, WORD vk, KeyZone zone, float width = 70.0f) {
        if (PhysicalKeyButton(id, label, "", "", ImVec2(width, h), zone, false, true)) SendKey(vk);
    };
    auto gap = [](float px) { ImGui::SameLine(0, px * KeyboardScale()); };

    F("esc", "ESC", VK_ESCAPE, KeyZone::Red, 76); gap(30);
    F("f1", "F1", VK_F1, KeyZone::Red); gap(g_ui.gap);
    F("f2", "F2", VK_F2, KeyZone::Red); gap(g_ui.gap);
    F("f3", "F3", VK_F3, KeyZone::Orange); gap(g_ui.gap);
    F("f4", "F4", VK_F4, KeyZone::Orange); gap(24);
    F("f5", "F5", VK_F5, KeyZone::Orange); gap(g_ui.gap);
    F("f6", "F6", VK_F6, KeyZone::Green); gap(g_ui.gap);
    F("f7", "F7", VK_F7, KeyZone::Green); gap(g_ui.gap);
    F("f8", "F8", VK_F8, KeyZone::Green); gap(24);
    F("f9", "F9", VK_F9, KeyZone::Green); gap(g_ui.gap);
    F("f10", "F10", VK_F10, KeyZone::Blue); gap(g_ui.gap);
    F("f11", "F11", VK_F11, KeyZone::Blue); gap(g_ui.gap);
    F("f12", "F12", VK_F12, KeyZone::Blue); gap(20);
    F("prtsc", "截圖\nPrtSc", VK_SNAPSHOT, KeyZone::Blue, 76); gap(g_ui.gap);
    if (PhysicalKeyButton("scroll", "捲動\nScroll", "", "", ImVec2(76, h), KeyZone::Blue,
                          (GetKeyState(VK_SCROLL) & 0x0001) != 0, true)) SendKey(VK_SCROLL); gap(g_ui.gap);
    F("pause", "暫停\nPause", VK_PAUSE, KeyZone::Blue, 76); gap(20);
    F("mute", "靜音", VK_VOLUME_MUTE, KeyZone::Purple, 72); gap(g_ui.gap);
    F("voldown", "音量-", VK_VOLUME_DOWN, KeyZone::Purple, 72); gap(g_ui.gap);
    F("volup", "音量+", VK_VOLUME_UP, KeyZone::Purple, 72); gap(g_ui.gap);
    F("calc", "計算機", VK_LAUNCH_APP2, KeyZone::Purple, 72);
}


void ApplyLiveStyleSettings() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.ItemSpacing = ImVec2(g_ui.gap, g_ui.gap);
    s.Colors[ImGuiCol_WindowBg] = ImVec4(
        g_ui.backgroundColor[0], g_ui.backgroundColor[1], g_ui.backgroundColor[2], g_ui.backgroundAlpha);
}

void FitKeyboardToArea(const ImVec2& area) {
    // Approximate natural full-keyboard bounds before keyboardScale is applied.
    constexpr float baseW = 1885.0f;
    constexpr float baseH = 505.0f;
    const float sx = (std::max)(400.0f, area.x - 28.0f) / baseW;
    const float sy = (std::max)(260.0f, area.y - 34.0f) / baseH;
    g_ui.keyboardScale = std::clamp((std::min)(sx, sy), 0.55f, 1.20f);
    g_ui.keyboardOffsetX = 0.0f;
    g_ui.keyboardOffsetY = 0.0f;
    if (g_autoSave) SaveSettings();
}

void DrawEditorSidebar() {
    ImGui::Text("編輯預覽 V%s", VRFK_VERSION_STRING);
    ImGui::Separator();

    bool changed = false;
    auto WrappedText = [](const char* text, bool disabled = false) {
        const float wrapX = ImGui::GetCursorPosX() + (std::max)(80.0f, ImGui::GetContentRegionAvail().x - 18.0f);
        ImGui::PushTextWrapPos(wrapX);
        if (disabled) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextUnformatted(text);
        if (disabled) ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();
    };

    // The editor sidebar must not inherit keyboard key spacing.  Keeping a
    // dedicated editor spacing prevents large key-gap settings from squeezing
    // the footer or clipping the last row of controls.
    const float editorSpacing = 8.0f;
    const float footerReserve = ImGui::GetFrameHeight() * 3.0f + editorSpacing * 5.0f + 18.0f;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(editorSpacing, editorSpacing));
    ImGui::BeginChild("editor_tab_content", ImVec2(0.0f, -footerReserve), ImGuiChildFlags_None,
                      ImGuiWindowFlags_AlwaysVerticalScrollbar);

    if (ImGui::BeginTabBar("editor_tabs", ImGuiTabBarFlags_FittingPolicyResizeDown)) {
        if (ImGui::BeginTabItem("基本")) {
            ImGuiIO& io = ImGui::GetIO();
            ImGui::Text("FPS %.0f　畫面 %.2f ms", io.Framerate, io.Framerate > 0.1f ? 1000.0f / io.Framerate : 0.0f);
            ImGui::Text("VR 點擊 %.2f ms", g_lastVrClickLatencyMs);
            ImGui::Text("中文字型：%s", g_fontStatus.c_str());
            WrappedText("VR 點擊值只會在 SteamVR 模式更新。上方「拖曳鍵盤」可直接移動左側預覽。", true);

            ImGui::SeparatorText("位置與尺寸");
            changed |= ImGui::SliderFloat("整體縮放", &g_ui.keyboardScale, 0.55f, 1.35f, "%.2fx");
            changed |= ImGui::SliderFloat("X 位置", &g_ui.keyboardOffsetX, -420.0f, 420.0f, "%.0f px");
            changed |= ImGui::SliderFloat("Y 位置", &g_ui.keyboardOffsetY, -180.0f, 260.0f, "%.0f px");
            if (EditorCenteredButton("一鍵適應畫面", ImVec2(-1.0f, 36.0f))) FitKeyboardToArea(g_previewKeyboardArea);

            ImGui::SeparatorText("鍵帽");
            changed |= ImGui::SliderFloat("鍵帽高度", &g_ui.keyHeight, 58.0f, 82.0f, "%.0f px");
            changed |= ImGui::SliderFloat("單鍵寬度", &g_ui.keyUnit, 64.0f, 82.0f, "%.0f px");
            changed |= ImGui::SliderFloat("按鍵間距", &g_ui.gap, 2.0f, 12.0f, "%.1f px");
            changed |= ImGui::SliderFloat("鍵帽圓角", &g_ui.keyCornerRadius, 0.0f, 18.0f, "%.1f px");
            changed |= ImGui::SliderFloat("外框粗細", &g_ui.keyBorderThickness, 0.5f, 5.0f, "%.1f px");

            ImGui::SeparatorText("文字");
            changed |= ImGui::SliderFloat("英文字", &g_ui.mainFont, 20.0f, 34.0f, "%.0f px");
            changed |= ImGui::SliderFloat("符號字", &g_ui.shiftedFont, 11.0f, 20.0f, "%.0f px");
            changed |= ImGui::SliderFloat("注音字", &g_ui.zhuyinFont, 14.0f, 26.0f, "%.0f px");
            changed |= ImGui::SliderFloat("功能鍵字", &g_ui.compactFont, 14.0f, 24.0f, "%.0f px");
            ImGui::SeparatorText("字體清晰度");
            int oldFontStyle = g_ui.fontStyle;
            const char* fontModes[] = { "清晰：Segoe UI + 微軟正黑體", "標準：微軟正黑體" };
            changed |= ImGui::Combo("字體樣式", &g_ui.fontStyle, fontModes, IM_ARRAYSIZE(fontModes));
            if (g_ui.fontStyle != oldFontStyle) g_fontRestartRequired = true;
            if (g_fontRestartRequired) ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.30f, 1.0f), "重新啟動預覽 / VR 後套用新字體");
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("外觀")) {
            WrappedText("此頁只處理配色與透明度，不再和尺寸設定混在一起。", true);
            changed |= ImGui::SliderFloat("背景不透明度", &g_ui.backgroundAlpha, 0.25f, 1.0f, "%.2f");
            changed |= ImGui::SliderFloat("RGB 外框強度", &g_ui.rgbIntensity, 0.10f, 1.50f, "%.2f");

            const ImGuiColorEditFlags colorFlags = ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel;
            ImGui::SeparatorText("主色");
            changed |= ImGui::ColorEdit3("背景", g_ui.backgroundColor.data(), colorFlags);
            changed |= ImGui::ColorEdit3("鍵帽", g_ui.keyColor.data(), colorFlags);
            changed |= ImGui::ColorEdit3("英文", g_ui.mainTextColor.data(), colorFlags);
            changed |= ImGui::ColorEdit3("符號", g_ui.shiftedTextColor.data(), colorFlags);
            changed |= ImGui::ColorEdit3("注音", g_ui.zhuyinTextColor.data(), colorFlags);

            ImGui::SeparatorText("RGB 分區");
            changed |= ImGui::ColorEdit3("紅區", g_ui.zoneRed.data(), colorFlags);
            changed |= ImGui::ColorEdit3("橘區", g_ui.zoneOrange.data(), colorFlags);
            changed |= ImGui::ColorEdit3("綠區", g_ui.zoneGreen.data(), colorFlags);
            changed |= ImGui::ColorEdit3("藍區", g_ui.zoneBlue.data(), colorFlags);
            changed |= ImGui::ColorEdit3("紫區", g_ui.zonePurple.data(), colorFlags);
            if (EditorCenteredButton("恢復預設彩虹外觀", ImVec2(-1.0f, 36.0f))) {
                ResetAppearanceSettings();
                changed = true;
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("輸入")) {
            ImGui::SeparatorText("NumPad / 九方");
            changed |= ImGui::Checkbox("啟用九方介面", &g_ui.q9Mode);
            WrappedText("九方模式只改變右側數字鍵盤的顯示與強調效果；0～9 仍送出真正的 VK_NUMPAD 鍵碼，交由電腦上已安裝的九方輸入法處理。", true);
            if (g_ui.q9Mode && (GetKeyState(VK_NUMLOCK) & 0x0001) == 0)
                ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.28f, 1.0f), "目前 Num Lock 關閉，使用九方前請先開啟。 ");

            ImGui::SeparatorText("VR 回饋");
            changed |= ImGui::Checkbox("控制器震動", &g_ui.hapticEnabled);
            changed |= ImGui::SliderFloat("震動強度", &g_ui.hapticStrength, 0.05f, 1.0f, "%.2f");

            ImGui::SeparatorText("長按連發");
            changed |= ImGui::Checkbox("啟用長按連發", &g_ui.repeatEnabled);
            changed |= ImGui::SliderFloat("第一次等待", &g_ui.repeatDelay, 0.20f, 0.80f, "%.2f 秒");
            changed |= ImGui::SliderFloat("連發間隔", &g_ui.repeatRate, 0.035f, 0.20f, "%.3f 秒");
            WrappedText("Backspace、Delete、方向鍵、PageUp / PageDown 等支援長按連發。", true);

            ImGui::SeparatorText("閒置淡化");
            changed |= ImGui::Checkbox("啟用自動淡化", &g_ui.autoFadeEnabled);
            changed |= ImGui::SliderFloat("淡化等待", &g_ui.autoFadeSeconds, 2.0f, 60.0f, "%.0f 秒");
            changed |= ImGui::SliderFloat("淡化透明度", &g_ui.autoFadeAlpha, 0.08f, 0.80f, "%.2f");
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("快捷")) {
            ImGui::SeparatorText("動作格式");
            ImGui::BulletText("快捷鍵：CTRL+C");
            ImGui::BulletText("文字：TEXT:等我一下");
            ImGui::BulletText("巨集：MACRO:CTRL+C;WAIT:200;CTRL+V");
            WrappedText("WAIT 單位為毫秒（0～5000）。巨集採非阻塞執行，不會用 Sleep 卡住 VR 畫面。", true);

            ImGui::SeparatorText("快捷頁");
            for (size_t b = 0; b < g_shortcutBanks.size(); ++b) {
                ImGui::PushID(static_cast<int>(2000 + b));
                const char* name = g_shortcutBanks[b].name[0] ? g_shortcutBanks[b].name : "未命名頁";
                if (EditorCenteredSelectedButton(name, g_shortcutBankIndex == b, ImVec2(112, 36))) {
                    g_shortcutBankIndex = b;
                    changed = true;
                }
                if (b + 1 < g_shortcutBanks.size()) ImGui::SameLine(0, 6.0f);
                ImGui::PopID();
            }

            ShortcutBank& bank = CurrentShortcutBank();
            ImGui::SetNextItemWidth(-1.0f);
            changed |= ImGui::InputText("頁面名稱", bank.name, sizeof(bank.name));
            ImGui::Text("巨集狀態：%s", g_macroStatus.c_str());
            if (g_macroRunning) {
                ImGui::SameLine();
                if (ImGui::SmallButton("停止")) {
                    g_macroQueue.clear();
                    g_macroRunning = false;
                    g_macroStatus = "已停止";
                }
            }

            ImGui::SeparatorText("8 顆按鈕");
            ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(8.0f, 7.0f));
            if (ImGui::BeginTable("shortcut_editor_grid", 2,
                                  ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersInnerV |
                                  ImGuiTableFlags_PadOuterX)) {
                const float shortcutRowMinHeight = ImGui::GetTextLineHeightWithSpacing() * 2.0f +
                                                   ImGui::GetFrameHeight() * 2.0f + 12.0f;
                for (size_t i = 0; i < bank.slots.size(); ++i) {
                    if ((i % 2) == 0) ImGui::TableNextRow(ImGuiTableRowFlags_None, shortcutRowMinHeight);
                    ImGui::TableSetColumnIndex(static_cast<int>(i % 2));
                    ImGui::PushID(static_cast<int>(i));
                    ImGui::Text("#%d", static_cast<int>(i + 1));
                    ImGui::SetNextItemWidth(-1.0f);
                    changed |= ImGui::InputText("##label", bank.slots[i].label, sizeof(bank.slots[i].label));
                    ImGui::SetNextItemWidth(-1.0f);
                    changed |= ImGui::InputText("##action", bank.slots[i].action, sizeof(bank.slots[i].action));
                    const bool valid = IsActionSpecValid(bank.slots[i].action);
                    ImGui::TextColored(valid ? ImVec4(0.35f, 0.90f, 0.52f, 1.0f) : ImVec4(1.0f, 0.42f, 0.42f, 1.0f),
                                       valid ? "格式 OK" : (bank.slots[i].action[0] ? "格式錯誤" : "未設定"));
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            ImGui::PopStyleVar();
            if (EditorCenteredButton("還原目前快捷頁", ImVec2(-1.0f, 34.0f))) {
                g_shortcutBanks[g_shortcutBankIndex] = kDefaultShortcutBanks[g_shortcutBankIndex];
                changed = true;
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("VR")) {
            ImGui::SeparatorText("手腕圓點 / 召喚");
            WrappedText("待機時只顯示一個小圓點：單擊展開完整鍵盤、雙擊召喚到眼前、長按返回進入待機前的世界位置。");
            changed |= ImGui::Checkbox("待機使用左手腕", &g_ui.wristUseLeftHand);
            changed |= ImGui::SliderFloat("圓點大小", &g_ui.wristDotSizeMeters, 0.05f, 0.14f, "%.3f m");
            changed |= ImGui::SliderFloat("圓點透明度", &g_ui.wristDotAlpha, 0.30f, 1.00f, "%.2f");
            changed |= ImGui::SliderFloat("雙擊間隔", &g_ui.wristDoubleClickSeconds, 0.18f, 0.55f, "%.2f s");
            changed |= ImGui::SliderFloat("長按時間", &g_ui.wristLongPressSeconds, 0.40f, 1.50f, "%.2f s");
            ImGui::TextDisabled("單擊：展開｜雙擊：召喚｜長按：返回原位");
            ImGui::Text("狀態：%s", g_vrPlacementStatus.c_str());
            ImGui::TextDisabled("返回位置只保存於本次執行，不寫入 INI。");

            if (g_previewMode) {
                ImGui::SeparatorText("桌面模擬");
                if (EditorCenteredSelectedButton("模擬手腕圓點", g_wristStandby, ImVec2(-1.0f, 38.0f))) EnterWristStandby(vr::k_ulOverlayHandleInvalid);
                if (EditorCenteredButton("模擬召喚到眼前", ImVec2(-1.0f, 36.0f))) SummonKeyboard(vr::k_ulOverlayHandleInvalid, g_wristStandby);
                if (!g_returnWorldTransformValid) ImGui::BeginDisabled();
                if (EditorCenteredButton("模擬返回原位", ImVec2(-1.0f, 36.0f))) ReturnKeyboard(vr::k_ulOverlayHandleInvalid);
                if (!g_returnWorldTransformValid) ImGui::EndDisabled();
            }

            ImGui::SeparatorText("抓取與固定");
            WrappedText("SteamVR 中用控制器指向上方「抓住移動」，按住扳機即可拖曳與旋轉；放開後會自動轉成世界固定。", true);
            ImGui::BulletText("世界固定：鍵盤留在房間座標");
            ImGui::BulletText("固定視野：跟隨 HMD");
            ImGui::BulletText("左手 / 右手：完整鍵盤跟隨控制器");
            ImGui::BulletText("手腕圓點：單擊展開、雙擊召喚、長按返回");
            ImGui::BulletText("召喚 / 返回：暫時移到眼前，再回原位");
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    // Extra scroll padding keeps the final button/input fully visible instead
    // of landing exactly on the child clipping boundary.
    ImGui::Dummy(ImVec2(0.0f, ImGui::GetFrameHeight() * 0.65f));
    ImGui::EndChild();
    ImGui::PopStyleVar(2);

    ImGui::Separator();
    const bool autoBefore = g_autoSave;
    ImGui::Checkbox("自動儲存", &g_autoSave);
    if (autoBefore != g_autoSave && g_autoSave) SaveSettings();
    if (EditorCenteredButton("立即儲存", ImVec2(-1.0f, 34.0f))) SaveSettings();
    if (EditorCenteredButton("全部還原預設", ImVec2(-1.0f, 34.0f))) {
        ResetUiSettings();
        SaveSettings();
    }
    // Do not rewrite the INI every frame while a slider/input is being dragged.
    // Queue one save and flush it once the user releases the active control.
    static bool editorAutoSavePending = false;
    if (changed && g_autoSave) editorAutoSavePending = true;
    if (editorAutoSavePending && g_autoSave && !ImGui::IsAnyItemActive()) {
        SaveSettings();
        editorAutoSavePending = false;
    }
}


void DrawWristStandbyContent(vr::VROverlayHandle_t overlay, const ImVec2& areaSize) {
    // In SteamVR the overlay is cropped to a square TEX_H x TEX_H region.
    // Desktop preview keeps the normal child size and centers the same dot.
    const float logicalW = g_previewMode ? areaSize.x : (float)TEX_H;
    const float logicalH = g_previewMode ? areaSize.y : (float)TEX_H;
    ImGui::BeginChild("wrist_dot_panel", ImVec2(logicalW, logicalH), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float diameter = g_previewMode
        ? std::clamp((std::min)(avail.x, avail.y) * 0.30f, 92.0f, 210.0f)
        : 430.0f;
    const ImVec2 topLeft(
        origin.x + (avail.x - diameter) * 0.5f,
        origin.y + (avail.y - diameter) * 0.5f);
    ImGui::SetCursorScreenPos(topLeft);

    const bool releasedClick = ImGui::InvisibleButton("##wrist_dot", ImVec2(diameter, diameter));
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    const bool activated = ImGui::IsItemActivated();
    const bool clicked = releasedClick && !g_wristLongPressTriggered;
    const double now = ImGui::GetTime();

    if (activated) {
        g_wristPressStartTime = now;
        g_wristLongPressTriggered = false;
        MarkVrInteraction();
    }

    float holdProgress = 0.0f;
    if (active) {
        holdProgress = std::clamp((float)((now - g_wristPressStartTime) / (double)g_ui.wristLongPressSeconds), 0.0f, 1.0f);
        if (!g_wristLongPressTriggered && holdProgress >= 1.0f) {
            g_wristLongPressTriggered = true;
            g_wristPendingSingle = false;
            PulseHaptic();
            ReturnKeyboard(overlay);
        }
    }

    if (clicked) {
        const double sinceLast = now - g_wristLastClickTime;
        if (g_wristPendingSingle && sinceLast <= (double)g_ui.wristDoubleClickSeconds) {
            g_wristPendingSingle = false;
            g_wristLastClickTime = -10.0;
            PulseHaptic();
            SummonKeyboard(overlay, true);
        } else {
            g_wristLastClickTime = now;
            g_wristPendingSingle = true;
            g_wristPendingSingleDue = now + (double)g_ui.wristDoubleClickSeconds;
        }
    }

    if (g_wristPendingSingle && !active && now >= g_wristPendingSingleDue) {
        g_wristPendingSingle = false;
        PulseHaptic();
        ExpandWristKeyboard(overlay);
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 center(topLeft.x + diameter * 0.5f, topLeft.y + diameter * 0.5f);
    const float radius = diameter * 0.46f;
    const float alpha = std::clamp(g_ui.wristDotAlpha, 0.30f, 1.0f);
    const ImU32 fill = ImGui::GetColorU32(ImVec4(
        hovered ? 0.18f : 0.10f,
        hovered ? 0.48f : 0.33f,
        hovered ? 0.88f : 0.68f,
        alpha));
    const ImU32 border = ImGui::GetColorU32(ImVec4(0.46f, 0.76f, 1.0f, (std::min)(1.0f, alpha + 0.10f)));
    dl->AddCircleFilled(center, radius, fill, 64);
    dl->AddCircle(center, radius, border, 64, (std::max)(2.0f, diameter * 0.018f));

    // Long-press progress ring. It only appears while holding, so the standby
    // state remains a clean single circle when idle.
    if (active && !g_wristLongPressTriggered) {
        constexpr float kPi = 3.14159265358979323846f;
        const float a0 = -kPi * 0.5f;
        const float a1 = a0 + kPi * 2.0f * holdProgress;
        dl->PathArcTo(center, radius * 0.82f, a0, a1, 48);
        dl->PathStroke(ImGui::GetColorU32(ImVec4(1.0f, 0.86f, 0.34f, 1.0f)), 0, (std::max)(4.0f, diameter * 0.026f));
    }

    // Minimal center mark, deliberately not a text label/icon panel.
    dl->AddCircleFilled(center, radius * 0.13f, ImGui::GetColorU32(ImVec4(0.95f, 0.98f, 1.0f, 0.95f)), 32);

    if (g_previewMode) {
        ImGui::SetCursorScreenPos(ImVec2(origin.x + 16.0f, origin.y + 14.0f));
        ImGui::TextDisabled("手腕圓點模擬｜單擊展開・雙擊召喚・長按返回");
    }
    ImGui::EndChild();
}

void DrawKeyboardPreviewArea(vr::VROverlayHandle_t overlay, const ImVec2& areaSize) {
    g_previewKeyboardArea = areaSize;
    ImGui::BeginChild("keyboard_preview_area", areaSize, ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    if (g_wristStandby) {
        // Nested wrist panel preview. End the outer child immediately after it.
        DrawWristStandbyContent(overlay, ImGui::GetContentRegionAvail());
        ImGui::EndChild();
        return;
    }

    if (g_page == PageMode::Shortcuts) {
        DrawShortcuts();
        ImGui::EndChild();
        return;
    }
    if (g_page == PageMode::Clipboard) {
        DrawClipboardPage();
        ImGui::EndChild();
        return;
    }
    if (g_page == PageMode::Update) {
        DrawUpdatePage();
        ImGui::EndChild();
        return;
    }

    ImGui::TextUnformatted(g_ui.q9Mode ? "實體鍵盤對照｜九方 NumPad 模式｜世界固定＝鍵盤留在房間原位" : "實體鍵盤對照｜英文＋台灣注音｜世界固定＝鍵盤留在房間原位");
    ImGui::SameLine(0, 22);
    const bool capsOn = (GetKeyState(VK_CAPITAL) & 0x0001) != 0;
    const bool numOn = (GetKeyState(VK_NUMLOCK) & 0x0001) != 0;
    const bool scrollOn = (GetKeyState(VK_SCROLL) & 0x0001) != 0;
    ImGui::Text("輸入 %s　大寫 %s　數字鍵 %s　捲動鎖 %s　NumPad %s",
                InputModeLabel(), capsOn ? "開" : "關", numOn ? "開" : "關", scrollOn ? "開" : "關",
                g_ui.q9Mode ? "九方" : "標準");
    if (g_ui.q9Mode && !numOn) {
        ImGui::SameLine(0, 18.0f);
        ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.28f, 1.0f), "九方：請開啟 Num Lock");
    }
    ImGui::Separator();

    ImVec2 start = ImGui::GetCursorPos();
    ImGui::SetCursorPos(ImVec2(start.x + g_ui.keyboardOffsetX, start.y + g_ui.keyboardOffsetY));

    DrawFunctionRow();
    ImGui::Spacing();
    ImGui::Spacing();

    if (ImGui::BeginTable("keyboard_columns", 3, ImGuiTableFlags_SizingFixedFit)) {
        const float ks = KeyboardScale();
        ImGui::TableSetupColumn("alpha", ImGuiTableColumnFlags_WidthFixed, 1280.0f * ks);
        ImGui::TableSetupColumn("nav", ImGuiTableColumnFlags_WidthFixed, 255.0f * ks);
        ImGui::TableSetupColumn("num", ImGuiTableColumnFlags_WidthFixed, 325.0f * ks);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); DrawAlphaKeyboard();
        ImGui::TableSetColumnIndex(1); DrawNavigation();
        ImGui::TableSetColumnIndex(2); DrawNumpad();
        ImGui::EndTable();
    }
    ImGui::EndChild();
}

void DrawUI(vr::VROverlayHandle_t overlay) {
    ApplyLiveStyleSettings();
    MaybeStartAutoUpdateCheck();
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 canvas = g_previewMode ? io.DisplaySize : ImVec2((float)TEX_W, (float)TEX_H);
    ImGui::SetNextWindowPos(ImVec2(0,0));
    ImGui::SetNextWindowSize(canvas);
    ImGui::Begin("VRFullKeyboard", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

    if (!g_previewMode && g_wristStandby) {
        DrawWristStandbyContent(overlay, ImGui::GetContentRegionAvail());
        ImGui::End();
        return;
    }

    if (g_previewMode) {
        ImGui::TextUnformatted("桌面預覽｜安全模式：點擊按鍵不會向 Windows 傳送輸入");
        ImGui::Separator();
    }
    DrawTopBar(overlay);
    ImGui::Separator();

    if (g_previewMode && g_editorMode) {
        const float totalW = ImGui::GetContentRegionAvail().x;
        const float sideW = std::clamp(totalW * 0.33f, 500.0f, 570.0f);
        const float h = ImGui::GetContentRegionAvail().y;
        const float leftW = (std::max)(520.0f, totalW - sideW - 10.0f);
        DrawKeyboardPreviewArea(overlay, ImVec2(leftW, h));
        ImGui::SameLine(0, 10.0f);
        ImGui::BeginChild("editor_sidebar", ImVec2(sideW, h), ImGuiChildFlags_Borders);
        DrawEditorSidebar();
        ImGui::EndChild();
    } else {
        DrawKeyboardPreviewArea(overlay, ImGui::GetContentRegionAvail());
    }

    ImGui::End();
}

void ProcessOverlayEvents(vr::VROverlayHandle_t overlay) {
    ImGuiIO& io = ImGui::GetIO();
    vr::VREvent_t e{};
    while (vr::VROverlay()->PollNextOverlayEvent(overlay, &e, sizeof(e))) {
        switch (e.eventType) {
            case vr::VREvent_MouseMove:
                MarkVrInteraction();
                if (e.trackedDeviceIndex != vr::k_unTrackedDeviceIndexInvalid)
                    g_lastPointerDevice = e.trackedDeviceIndex;
                io.AddMousePosEvent(e.data.mouse.x, (float)TEX_H - e.data.mouse.y);
                break;
            case vr::VREvent_MouseButtonDown:
                MarkVrInteraction();
                if (e.trackedDeviceIndex != vr::k_unTrackedDeviceIndexInvalid)
                    g_lastPointerDevice = e.trackedDeviceIndex;
                if (e.data.mouse.button & vr::VRMouseButton_Left) {
                    g_vrClickEventTime = std::chrono::steady_clock::now();
                    g_vrClickPending = true;
                    io.AddMouseButtonEvent(0, true);
                }
                if (e.data.mouse.button & vr::VRMouseButton_Right) io.AddMouseButtonEvent(1, true);
                break;
            case vr::VREvent_MouseButtonUp:
                if (e.trackedDeviceIndex != vr::k_unTrackedDeviceIndexInvalid)
                    g_lastPointerDevice = e.trackedDeviceIndex;
                if (e.data.mouse.button & vr::VRMouseButton_Left) {
                    io.AddMouseButtonEvent(0, false);
                    if (g_vrGrabActive) EndVrGrab(overlay);
                }
                if (e.data.mouse.button & vr::VRMouseButton_Right) io.AddMouseButtonEvent(1, false);
                break;
            case vr::VREvent_ScrollDiscrete:
            case vr::VREvent_ScrollSmooth:
                MarkVrInteraction();
                io.AddMouseWheelEvent(e.data.scroll.xdelta, e.data.scroll.ydelta);
                break;
            default:
                break;
        }
    }
}

void SetupStyle() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 22.0f;
    s.FrameRounding = 9.0f;
    s.WindowPadding = ImVec2(14, 10);
    s.ItemSpacing = ImVec2(g_ui.gap, g_ui.gap);
    s.FramePadding = ImVec2(10, 8);

    s.Colors[ImGuiCol_WindowBg] = ImVec4(
        g_ui.backgroundColor[0], g_ui.backgroundColor[1], g_ui.backgroundColor[2], g_ui.backgroundAlpha);
    s.Colors[ImGuiCol_Text] = ImVec4(0.94f, 0.96f, 0.99f, 1.0f);
    s.Colors[ImGuiCol_Button] = ImVec4(0.105f, 0.120f, 0.155f, 1.0f);
    s.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.175f, 0.205f, 0.260f, 1.0f);
    s.Colors[ImGuiCol_ButtonActive] = ImVec4(0.230f, 0.280f, 0.350f, 1.0f);
    s.Colors[ImGuiCol_Separator] = ImVec4(0.28f, 0.32f, 0.40f, 0.55f);
}

void LoadFont() {
    ImGuiIO& io = ImGui::GetIO();

    // Build a compact extra-glyph range instead of the entire ChineseFull range.
    // This keeps the atlas small while covering every Traditional Chinese / Bopomofo
    // string used by the app.
    static ImVector<ImWchar> extraRanges;
    if (extraRanges.empty()) {
        ImFontGlyphRangesBuilder builder;
        static const ImWchar extra[] = {
            0x02C0, 0x02FF, // tone marks
            0x2000, 0x206F, // punctuation
            0x2190, 0x21FF, // arrows
            0x3100, 0x312F, // Bopomofo
            0xFF00, 0xFFEF, // full-width punctuation
            0,
        };
        builder.AddRanges(extra);
        builder.AddText("一三上下不世並中主也了仍以位住作使供保修個值做停側傳儲先入內全共再分切到制前剪功加動化區卡即原取另只召可台史右合名向含命和員啟喚單器回固圓圖在型執基塞外大失套字存安完定容實寫寬寸對小尚尺工左巨已常帽底度座建式強彩待後得從復快恢感態應成我或截房所手打扳把抓拖持指按捲捷採接控提換援擊操擬支改放效敗整數文新方於旋日明時景暫曳更會有未本板格框桌標模橘機檔次止此歷段毫沒法注淡混清測滑灣為無照版狀理用界留畫發的盤目直眼示秒移程稱窗立符第等算管簡簿籤粗精紅紫細組綠維編縮置背能腕自與般色英著藍處號虹行裁製複見視覽觀角觸言訂計記設試語誤說調讀貼起距跟載輕輯輸轉近返送透這通速連進遠適選還部配重野量鈕錯鍵長閉開閒間關防阻附除階隔際隨集離震靜非靠面韓音頁預顆顏顯饋驗體高點鼠清晰標準微軟正黑體重新啟動後套用雙擊間隔長按時間圓點大小透明度單擊展開收起完整鍵盤召喚到眼前返回原位手腕模式");
        builder.AddText("更新檢查版本最新發現倉庫來源正在名稱內容下載覆蓋程式檔案自動驗證備份綁定建立正式位址填入固定啟用回傳網路連線請求失敗目前已是最新版開啟資訊階段加入任何");
        builder.BuildRanges(&extraRanges);
    }

    static const ImWchar* latinRanges = io.Fonts->GetGlyphRangesDefault();
    const char* jhengHei = "C:\\Windows\\Fonts\\msjh.ttc";
    const wchar_t* jhengHeiW = L"C:\\Windows\\Fonts\\msjh.ttc";
    const char* segoe = "C:\\Windows\\Fonts\\segoeui.ttf";
    const wchar_t* segoeW = L"C:\\Windows\\Fonts\\segoeui.ttf";

    g_fontLoaded = false;

    if (g_ui.fontStyle == 0 && GetFileAttributesW(segoeW) != INVALID_FILE_ATTRIBUTES &&
        GetFileAttributesW(jhengHeiW) != INVALID_FILE_ATTRIBUTES) {
        // Clear mode: use Segoe UI for Latin/UI glyphs, then merge JhengHei only
        // for Traditional Chinese/Bopomofo. Pixel snapping improves small legends.
        ImFontConfig latinCfg{};
        latinCfg.OversampleH = 3;
        latinCfg.OversampleV = 2;
        latinCfg.PixelSnapH = true;
        ImFont* base = io.Fonts->AddFontFromFileTTF(segoe, 30.0f, &latinCfg, latinRanges);
        if (base) {
            ImFontConfig zhCfg{};
            zhCfg.MergeMode = true;
            zhCfg.PixelSnapH = true;
            zhCfg.OversampleH = 3;
            zhCfg.OversampleV = 2;
            if (io.Fonts->AddFontFromFileTTF(jhengHei, 30.0f, &zhCfg, extraRanges.Data)) {
                g_fontLoaded = true;
                g_fontStatus = "OK: Segoe UI + Microsoft JhengHei (Clear)";
            }
        }
    }

    if (!g_fontLoaded) {
        // If clear mode partially added Segoe UI but the Chinese merge failed,
        // remove that partial atlas so the fallback font becomes the real default.
        io.Fonts->Clear();
        // Standard / fallback mode: single JhengHei font with a compact merged range.
        static ImVector<ImWchar> allRanges;
        if (allRanges.empty()) {
            ImFontGlyphRangesBuilder allBuilder;
            allBuilder.AddRanges(io.Fonts->GetGlyphRangesDefault());
            allBuilder.AddRanges(extraRanges.Data);
            allBuilder.BuildRanges(&allRanges);
        }

        const wchar_t* candidatesW[] = {
            L"C:\\Windows\\Fonts\\msjh.ttc",
            L"C:\\Windows\\Fonts\\msjhbd.ttc",
            L"C:\\Windows\\Fonts\\mingliu.ttc",
            L"C:\\Windows\\Fonts\\kaiu.ttf"
        };
        const char* candidates[] = {
            "C:\\Windows\\Fonts\\msjh.ttc",
            "C:\\Windows\\Fonts\\msjhbd.ttc",
            "C:\\Windows\\Fonts\\mingliu.ttc",
            "C:\\Windows\\Fonts\\kaiu.ttf"
        };
        const char* names[] = { "Microsoft JhengHei", "Microsoft JhengHei Bold", "MingLiU", "DFKai-SB" };

        for (int i = 0; i < 4; ++i) {
            if (GetFileAttributesW(candidatesW[i]) == INVALID_FILE_ATTRIBUTES) continue;
            ImFontConfig cfg{};
            cfg.OversampleH = 3;
            cfg.OversampleV = 2;
            cfg.PixelSnapH = true;
            if (io.Fonts->AddFontFromFileTTF(candidates[i], 29.0f, &cfg, allRanges.Data)) {
                g_fontLoaded = true;
                g_fontStatus = std::string("OK: ") + names[i] + " (Standard)";
                break;
            }
        }
    }

    if (!g_fontLoaded) {
        io.Fonts->AddFontDefault();
        g_fontStatus = "ERROR: Chinese font not found";
    }
}


struct PreviewDxState {
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    IDXGISwapChain* swapChain = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
};

PreviewDxState* g_previewDxForResize = nullptr;

void CreatePreviewRenderTarget(PreviewDxState& dx) {
    ID3D11Texture2D* backBuffer = nullptr;
    if (dx.swapChain && SUCCEEDED(dx.swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)))) {
        dx.device->CreateRenderTargetView(backBuffer, nullptr, &dx.rtv);
        backBuffer->Release();
    }
}

void CleanupPreviewRenderTarget(PreviewDxState& dx) {
    SafeReleaseT(dx.rtv);
}

bool InitPreviewD3D(HWND hwnd, PreviewDxState& dx) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    const D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_0
    };
    D3D_FEATURE_LEVEL got{};
    const HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
        levels, static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION,
        &sd, &dx.swapChain, &dx.device, &got, &dx.context);
    if (FAILED(hr)) return false;

    CreatePreviewRenderTarget(dx);
    return dx.rtv != nullptr;
}

void ShutdownPreviewD3D(PreviewDxState& dx) {
    CleanupPreviewRenderTarget(dx);
    SafeReleaseT(dx.swapChain);
    SafeReleaseT(dx.context);
    SafeReleaseT(dx.device);
}

LRESULT WINAPI PreviewWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) return true;

    switch (msg) {
        case WM_SIZE:
            if (g_previewDxForResize && g_previewDxForResize->device && wParam != SIZE_MINIMIZED) {
                CleanupPreviewRenderTarget(*g_previewDxForResize);
                g_previewDxForResize->swapChain->ResizeBuffers(
                    0, static_cast<UINT>(LOWORD(lParam)), static_cast<UINT>(HIWORD(lParam)),
                    DXGI_FORMAT_UNKNOWN, 0);
                CreatePreviewRenderTarget(*g_previewDxForResize);
            }
            return 0;
        case WM_SYSCOMMAND:
            if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
            break;
        case WM_CLOSE:
            g_running = false;
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int RunDesktopPreview(HINSTANCE instance) {
    LoadSettings();
    g_previewMode = true;
    g_running = true;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = PreviewWndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"VRFullKeyboardPreviewWindow";
    RegisterClassExW(&wc);

    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int workW = work.right - work.left;
    const int workH = work.bottom - work.top;
    const int wantedW = (std::min)(1820, (std::max)(1200, workW - 80));
    const int wantedH = (std::min)(740, (std::max)(640, workH - 220));
    RECT wr{0, 0, wantedW, wantedH};
    AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowW(
        wc.lpszClassName,
        L"VR 完整鍵盤 - 桌面預覽",
        WS_OVERLAPPEDWINDOW,
        work.left + (workW - (wr.right - wr.left)) / 2,
        work.top + (workH - (wr.bottom - wr.top)) / 2,
        wr.right - wr.left, wr.bottom - wr.top,
        nullptr, nullptr, instance, nullptr);
    if (!hwnd) {
        UnregisterClassW(wc.lpszClassName, instance);
        return 10;
    }

    PreviewDxState dx;
    if (!InitPreviewD3D(hwnd, dx)) {
        DestroyWindow(hwnd);
        UnregisterClassW(wc.lpszClassName, instance);
        MessageBoxW(nullptr, L"無法建立 DirectX 11 桌面預覽視窗。", L"VR Full Keyboard", MB_OK | MB_ICONERROR);
        return 11;
    }
    g_previewDxForResize = &dx;

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    LoadFont();
    SetupStyle();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(dx.device, dx.context);

    while (g_running) {
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) g_running = false;
        }
        if (!g_running) break;

        if (IsIconic(hwnd)) {
            Sleep(30);
            continue;
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        ProcessMacroQueue();
        DrawUI(vr::k_ulOverlayHandleInvalid);
        ImGui::Render();

        const float clear[4] = {0.012f, 0.015f, 0.022f, 1.0f};
        dx.context->OMSetRenderTargets(1, &dx.rtv, nullptr);
        dx.context->ClearRenderTargetView(dx.rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        dx.swapChain->Present(1, 0);
    }

    SaveSettings();
    ShutdownUpdateChecker();
    g_previewDxForResize = nullptr;
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    ShutdownPreviewD3D(dx);
    if (IsWindow(hwnd)) DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, instance);
    return 0;
}

int Run() {
    LoadSettings();
    vr::EVRInitError initError = vr::VRInitError_None;
    vr::IVRSystem* system = vr::VR_Init(&initError, vr::VRApplication_Overlay);
    if (initError != vr::VRInitError_None || !system) {
        MessageBoxA(nullptr, "SteamVR/OpenVR is not available. Start SteamVR first, then run VRFullKeyboard again.",
                    "VR Full Keyboard", MB_OK | MB_ICONERROR);
        return 2;
    }

    auto* ov = vr::VROverlay();
    if (!ov) {
        vr::VR_Shutdown();
        return 3;
    }

    vr::VROverlayHandle_t overlay = vr::k_ulOverlayHandleInvalid;
    vr::EVROverlayError oe = ov->CreateOverlay("fly.vrfullkeyboard.overlay", "VR Full Keyboard", &overlay);
    if (oe != vr::VROverlayError_None) {
        MessageBoxA(nullptr, ov->GetOverlayErrorNameFromEnum(oe), "Unable to create SteamVR overlay", MB_OK | MB_ICONERROR);
        vr::VR_Shutdown();
        return 4;
    }

    DxState dx;
    if (!InitD3D(dx)) {
        ov->DestroyOverlay(overlay);
        vr::VR_Shutdown();
        MessageBoxA(nullptr, "Failed to create a DirectX 11 off-screen texture.", "VR Full Keyboard", MB_OK | MB_ICONERROR);
        return 5;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)TEX_W, (float)TEX_H);
    io.IniFilename = nullptr;
    LoadFont();
    SetupStyle();
    ImGui_ImplDX11_Init(dx.device, dx.context);

    vr::HmdVector2_t mouseScale{{(float)TEX_W, (float)TEX_H}};
    ov->SetOverlayInputMethod(overlay, vr::VROverlayInputMethod_Mouse);
    ov->SetOverlayMouseScale(overlay, &mouseScale);
    ov->SetOverlayWidthInMeters(overlay, g_widthMeters);
    ov->SetOverlayAlpha(overlay, 1.0f);
    ov->SetOverlayFlag(overlay, vr::VROverlayFlags_NoBackside, true);
    ov->SetOverlayFlag(overlay, vr::VROverlayFlags_EnableClickStabilization, false);
    SetFullOverlayVisualBounds(overlay);
    ApplyAnchor(overlay, g_anchor == AnchorMode::WorldFixed);
    g_lastVrInteraction = std::chrono::steady_clock::now();
    g_overlayAlpha = 1.0f;
    ov->ShowOverlay(overlay);

    vr::Texture_t vrTexture{dx.texture, vr::TextureType_DirectX, vr::ColorSpace_Auto};
    auto last = std::chrono::steady_clock::now();

    while (g_running && vr::VRSystem()) {
        // World-fixed overlays intentionally do not follow the HMD. Their absolute
        // Standing-space transform is captured once when World fixed is selected.
        if (g_anchor == AnchorMode::HeadLocked && !g_vrGrabActive) ApplyAnchor(overlay);

        ProcessOverlayEvents(overlay);
        UpdateClipboardHistory();

        vr::VREvent_t sysEvent{};
        while (vr::VRSystem()->PollNextEvent(&sysEvent, sizeof(sysEvent))) {
            if (sysEvent.eventType == vr::VREvent_Quit) {
                g_running = false;
                break;
            }
        }

        const auto now = std::chrono::steady_clock::now();
        io.DeltaTime = (std::max)(0.001f, std::chrono::duration<float>(now - last).count());
        last = now;
        io.DisplaySize = ImVec2((float)TEX_W, (float)TEX_H);

        ImGui_ImplDX11_NewFrame();
        ImGui::NewFrame();
        ProcessMacroQueue();
        DrawUI(overlay);
        UpdateAutoFade(overlay);
        ImGui::Render();

        const float clear[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        dx.context->OMSetRenderTargets(1, &dx.rtv, nullptr);
        dx.context->ClearRenderTargetView(dx.rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        ov->SetOverlayTexture(overlay, &vrTexture);
        // No forced Flush/Sleep here: both can add avoidable input-to-display latency.
        std::this_thread::yield();
    }

    SaveSettings();
    ShutdownUpdateChecker();
    ImGui_ImplDX11_Shutdown();
    ImGui::DestroyContext();
    ov->DestroyOverlay(overlay);
    ShutdownD3D(dx);
    vr::VR_Shutdown();
    return 0;
}
} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR cmdLine, int) {
    const std::wstring args = cmdLine ? cmdLine : L"";
    const bool editorRequested = args.find(L"--editor") != std::wstring::npos || args.find(L"/editor") != std::wstring::npos;
    const bool updateRequested = args.find(L"--update") != std::wstring::npos || args.find(L"/update") != std::wstring::npos;
    if (editorRequested) g_editorMode = true;
    if (updateRequested) g_page = PageMode::Update;
    if (args.find(L"--preview") != std::wstring::npos || args.find(L"/preview") != std::wstring::npos || editorRequested || updateRequested) {
        return RunDesktopPreview(hInstance);
    }

    // When SteamVR is unavailable, fall back to the desktop preview automatically.
    if (!vr::VR_IsRuntimeInstalled() || !vr::VR_IsHmdPresent()) {
        return RunDesktopPreview(hInstance);
    }
    return Run();
}
