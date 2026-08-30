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
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {
constexpr int TEX_W = 1920;
constexpr int TEX_H = 660;
constexpr float DEFAULT_OVERLAY_WIDTH_M = 1.08f;
constexpr float DEFAULT_OVERLAY_DISTANCE_M = 0.92f;
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
    bool hoverHapticsEnabled = true;
    float hoverHapticScale = 0.18f;
    bool showInteractionPointer = true;
    float pointerScale = 1.0f;
    bool repeatEnabled = true;
    float repeatDelay = 0.38f;
    float repeatRate = 0.070f;
    bool autoFadeEnabled = true;
    float autoFadeSeconds = 8.0f;
    float autoFadeAlpha = 0.22f;

    // V3.4.3 wrist-dot controls.
    bool wristUseLeftHand = true;
    bool startInWristStandby = true;
    float wristWidthMeters = 0.30f; // Legacy V3.4 setting; kept for INI compatibility.
    // Small launcher overlay. Size is the transparent square overlay width;
    // the visible blue/white dot occupies only part of that square.
    float wristDotSizeMeters = 0.045f;
    float wristDotAlpha = 0.88f;
    // Controller-local calibration offsets. Defaults stay on the tracked grip
    // origin instead of guessing a Quest/Index/Pico-specific wrist offset.
    float wristDotOffsetX = 0.0f;
    float wristDotOffsetY = 0.0f;
    float wristDotOffsetZ = 0.0f;
    // Logical circular hit mask, independent from the visible dot.  0.85
    // means the clickable diameter uses 85% of the transparent square.
    float wristHitScale = 0.85f;
    float wristDoubleClickSeconds = 0.30f;
    float wristLongPressSeconds = 0.75f;

    // Reference-overlay-inspired window handling. Zero damping keeps the TB02
    // controller-relative, one-to-one path.  Non-zero values opt into
    // independent position / rotation smoothing while grabbing.
    bool layoutGripGrabEnabled = true;
    bool autoFaceOnRelease = true;
    float grabPositionDamping = 0.0f;
    float grabRotationDamping = 0.0f;
    float pushPullSpeed = 0.35f;
    float scaleSpeed = 0.40f;

    // OpenVR compositor curvature for the expanded keyboard only.
    bool curvedOverlay = true;
    float overlayCurvature = 0.12f;

    // Texture updates may be capped independently from the uncapped input /
    // pose loop.  Off preserves the exact TB02 low-latency baseline.
    bool adaptiveTextureUpdates = false;
    float activeTextureFps = 120.0f;
    float idleTextureFps = 30.0f;

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
float g_widthMeters = DEFAULT_OVERLAY_WIDTH_M;
float g_distance = DEFAULT_OVERLAY_DISTANCE_M;
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
bool g_forceTextureSubmit = true;
std::chrono::steady_clock::time_point g_lastTextureSubmit{};
bool g_pointerVisible = false;
ImGuiID g_hoverHapticItem = 0;
bool g_hoverHapticSeenThisFrame = false;

// Alpha V3.9.10 Test Build diagnostics. These timers observe the original
// immediate V3.9.9-style pose/input loop without adding sleeps, pose caches or
// WaitFrameSync. TB03's optional scheduler is texture-only and defaults off.
struct PerfFrameSample {
    float trackingMs = 0.0f;
    float eventsMs = 0.0f;
    float logicMs = 0.0f;
    float imguiMs = 0.0f;
    float d3dMs = 0.0f;
    float submitMs = 0.0f;
    float totalMs = 0.0f;
};

struct PerfSummary {
    float avgTrackingMs = 0.0f, maxTrackingMs = 0.0f;
    float avgEventsMs = 0.0f, maxEventsMs = 0.0f;
    float avgLogicMs = 0.0f, maxLogicMs = 0.0f;
    float avgImGuiMs = 0.0f, maxImGuiMs = 0.0f;
    float avgD3DMs = 0.0f, maxD3DMs = 0.0f;
    float avgSubmitMs = 0.0f, maxSubmitMs = 0.0f;
    float avgTotalMs = 0.0f, maxTotalMs = 0.0f;
    float cpuPercent = 0.0f;
    float loopsPerSec = 0.0f;
    size_t sampleCount = 0;
    vr::EVROverlayError lastSubmitError = vr::VROverlayError_None;
};

std::array<PerfFrameSample, 240> g_perfFrames{};
size_t g_perfFrameWrite = 0;
size_t g_perfFrameCount = 0;
PerfSummary g_perfSummary{};
uint64_t g_perfLoopsSinceSummary = 0;
std::chrono::steady_clock::time_point g_perfLastSummary{};

constexpr wchar_t kControlExitEventName[] = L"Local\\VRFullKeyboard.Exit.AlphaV3_9_10";
HANDLE g_controlExitEvent = nullptr;

static double FileTimeSeconds(const FILETIME& ft) {
    ULARGE_INTEGER u{};
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return static_cast<double>(u.QuadPart) / 10000000.0;
}

static std::filesystem::path PerfLogDirectory() {
    wchar_t localAppData[MAX_PATH]{};
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);
    std::filesystem::path dir;
    if (n > 0 && n < MAX_PATH) dir = std::filesystem::path(localAppData) / L"VRFullKeyboard" / L"logs";
    else dir = std::filesystem::temp_directory_path() / L"VRFullKeyboard";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

static std::filesystem::path PerfLogPath() {
    return PerfLogDirectory() / L"perf_alpha_v3_9_10.csv";
}

static std::filesystem::path PerfLivePath() {
    return PerfLogDirectory() / L"perf_live_alpha_v3_9_10.ini";
}

static float SampleProcessCpuPercent() {
    static bool initialized = false;
    static double prevCpuSec = 0.0;
    static auto prevWall = std::chrono::steady_clock::now();
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user)) return 0.0f;
    const double cpuSec = FileTimeSeconds(kernel) + FileTimeSeconds(user);
    const auto now = std::chrono::steady_clock::now();
    if (!initialized) {
        initialized = true;
        prevCpuSec = cpuSec;
        prevWall = now;
        return 0.0f;
    }
    const double wallSec = std::chrono::duration<double>(now - prevWall).count();
    const double cpuDelta = cpuSec - prevCpuSec;
    prevCpuSec = cpuSec;
    prevWall = now;
    if (wallSec <= 0.0) return 0.0f;
    DWORD cores = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    if (cores == 0) cores = 1;
    return static_cast<float>(std::clamp(cpuDelta / wallSec / static_cast<double>(cores) * 100.0, 0.0, 100.0));
}

static void PushPerfSample(const PerfFrameSample& sample) {
    g_perfFrames[g_perfFrameWrite] = sample;
    g_perfFrameWrite = (g_perfFrameWrite + 1) % g_perfFrames.size();
    g_perfFrameCount = (std::min)(g_perfFrameCount + 1, g_perfFrames.size());
    ++g_perfLoopsSinceSummary;
}

static void WritePerfLiveSnapshot(const PerfSummary& out) {
    const auto live = PerfLivePath();
    auto temp = live;
    temp += L".tmp";
    std::ofstream file(temp, std::ios::binary | std::ios::trunc);
    if (!file) return;
    file << std::fixed << std::setprecision(4)
         << "display_version=" << VRFK_DISPLAY_VERSION << " " << VRFK_TEST_BUILD_LABEL << '\n'
         << "pid=" << GetCurrentProcessId() << '\n'
         << "cpu_percent=" << out.cpuPercent << '\n'
         << "loops_per_sec=" << out.loopsPerSec << '\n'
         << "samples=" << out.sampleCount << '\n'
         << "avg_tracking_ms=" << out.avgTrackingMs << '\n'
         << "max_tracking_ms=" << out.maxTrackingMs << '\n'
         << "avg_events_ms=" << out.avgEventsMs << '\n'
         << "max_events_ms=" << out.maxEventsMs << '\n'
         << "avg_logic_ms=" << out.avgLogicMs << '\n'
         << "max_logic_ms=" << out.maxLogicMs << '\n'
         << "avg_imgui_ms=" << out.avgImGuiMs << '\n'
         << "max_imgui_ms=" << out.maxImGuiMs << '\n'
         << "avg_d3d_ms=" << out.avgD3DMs << '\n'
         << "max_d3d_ms=" << out.maxD3DMs << '\n'
         << "avg_submit_ms=" << out.avgSubmitMs << '\n'
         << "max_submit_ms=" << out.maxSubmitMs << '\n'
         << "avg_total_ms=" << out.avgTotalMs << '\n'
         << "max_total_ms=" << out.maxTotalMs << '\n'
         << "submit_error=" << static_cast<int>(out.lastSubmitError) << '\n';
    file.close();
    MoveFileExW(temp.wstring().c_str(), live.wstring().c_str(), MOVEFILE_REPLACE_EXISTING);
}

static void UpdatePerfSummary(vr::EVROverlayError lastSubmitError) {
    using Clock = std::chrono::steady_clock;
    const auto now = Clock::now();
    if (g_perfLastSummary.time_since_epoch().count() != 0 &&
        now - g_perfLastSummary < std::chrono::milliseconds(500)) return;
    const double windowSec = g_perfLastSummary.time_since_epoch().count() == 0
        ? 0.5 : (std::max)(0.001, std::chrono::duration<double>(now - g_perfLastSummary).count());
    g_perfLastSummary = now;

    PerfSummary out{};
    out.sampleCount = g_perfFrameCount;
    out.lastSubmitError = lastSubmitError;
    auto add = [](float& avg, float& mx, float value) {
        avg += value;
        mx = (std::max)(mx, value);
    };
    for (size_t i = 0; i < g_perfFrameCount; ++i) {
        const auto& f = g_perfFrames[i];
        add(out.avgTrackingMs, out.maxTrackingMs, f.trackingMs);
        add(out.avgEventsMs, out.maxEventsMs, f.eventsMs);
        add(out.avgLogicMs, out.maxLogicMs, f.logicMs);
        add(out.avgImGuiMs, out.maxImGuiMs, f.imguiMs);
        add(out.avgD3DMs, out.maxD3DMs, f.d3dMs);
        add(out.avgSubmitMs, out.maxSubmitMs, f.submitMs);
        add(out.avgTotalMs, out.maxTotalMs, f.totalMs);
    }
    if (g_perfFrameCount > 0) {
        const float inv = 1.0f / static_cast<float>(g_perfFrameCount);
        out.avgTrackingMs *= inv;
        out.avgEventsMs *= inv;
        out.avgLogicMs *= inv;
        out.avgImGuiMs *= inv;
        out.avgD3DMs *= inv;
        out.avgSubmitMs *= inv;
        out.avgTotalMs *= inv;
    }
    out.cpuPercent = SampleProcessCpuPercent();
    out.loopsPerSec = static_cast<float>(g_perfLoopsSinceSummary / windowSec);
    g_perfLoopsSinceSummary = 0;
    g_perfSummary = out;

    const auto logPath = PerfLogPath();
    std::error_code ec;
    const bool needHeader = !std::filesystem::exists(logPath, ec) || std::filesystem::file_size(logPath, ec) == 0;
    std::ofstream log(logPath, std::ios::app);
    if (log) {
        if (needHeader) {
            log << "timestamp_ms,version,cpu_percent,loops_per_sec,samples,avg_tracking_ms,max_tracking_ms,avg_events_ms,max_events_ms,avg_logic_ms,max_logic_ms,avg_imgui_ms,max_imgui_ms,avg_d3d_ms,max_d3d_ms,avg_submit_ms,max_submit_ms,avg_total_ms,max_total_ms,submit_error\n";
        }
        const auto systemMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        log << systemMs << ',' << VRFK_DISPLAY_VERSION << '-' << VRFK_TEST_BUILD_LABEL << ','
            << out.cpuPercent << ',' << out.loopsPerSec << ',' << out.sampleCount << ','
            << out.avgTrackingMs << ',' << out.maxTrackingMs << ','
            << out.avgEventsMs << ',' << out.maxEventsMs << ','
            << out.avgLogicMs << ',' << out.maxLogicMs << ','
            << out.avgImGuiMs << ',' << out.maxImGuiMs << ','
            << out.avgD3DMs << ',' << out.maxD3DMs << ','
            << out.avgSubmitMs << ',' << out.maxSubmitMs << ','
            << out.avgTotalMs << ',' << out.maxTotalMs << ','
            << static_cast<int>(out.lastSubmitError) << '\n';
    }
    WritePerfLiveSnapshot(out);
}
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

enum class InteractionMode { Normal, Layout };
enum class GrabInputSource { None, TriggerHandle, GripLayout };

// SteamVR controller grab state. The grab handle moves the whole overlay,
// and releasing it converts the overlay back to a world-fixed transform.
// Layout mode additionally supports grip-anywhere grabbing plus thumbstick
// push/pull and scale without changing the normal keyboard interaction path.
InteractionMode g_interactionMode = InteractionMode::Normal;
bool g_vrGrabActive = false;
vr::TrackedDeviceIndex_t g_vrGrabDevice = vr::k_unTrackedDeviceIndexInvalid;
vr::TrackedDeviceIndex_t g_lastPointerDevice = vr::k_unTrackedDeviceIndexInvalid;
vr::HmdMatrix34_t g_vrGrabRelative{};
vr::HmdMatrix34_t g_vrGrabSmoothedAbsolute{};
GrabInputSource g_vrGrabInputSource = GrabInputSource::None;
std::array<bool, vr::k_unMaxTrackedDeviceCount> g_layoutGripWasDown{};
std::chrono::steady_clock::time_point g_vrGrabLastUpdate{};

// TB03 grip compatibility state.  Do not install a SteamVR Action Manifest:
// that previously stole Dashboard/system UI input on the user's runtime.
// Instead combine legacy button events, the classic controller-state bit,
// and (for Touch/OpenXR-style mappings) a second analog Trigger-type axis.
std::array<bool, vr::k_unMaxTrackedDeviceCount> g_gripEventKnown{};
std::array<bool, vr::k_unMaxTrackedDeviceCount> g_gripEventDown{};
// Knuckles' validated gesture is based on capacitive Grip touch rather than
// squeeze pressure. Keep an event-backed mirror because SteamVR Dashboard can
// own legacy controller focus while still forwarding touch transitions.
std::array<bool, vr::k_unMaxTrackedDeviceCount> g_gripTouchEventKnown{};
std::array<bool, vr::k_unMaxTrackedDeviceCount> g_gripTouchEventDown{};
std::array<bool, vr::k_unMaxTrackedDeviceCount> g_gripAnalogDown{};

// Valve Index / Knuckles compatibility: squeeze force is not exposed through the
// legacy controller-state API on the user's runtime. It does, however, expose
// k_EButton_Grip in ulButtonTouched. Use that capacitive contact as a safe
// fallback without installing a SteamVR Action Manifest. The gesture is
// intentionally edge/latch based: the user must fully release the grip contact
// once before closing the hand to start a grab, which avoids grabbing
// immediately just because the controller was already being held at startup.
std::array<bool, vr::k_unMaxTrackedDeviceCount> g_knucklesGripInitialized{};
std::array<bool, vr::k_unMaxTrackedDeviceCount> g_knucklesGripRawTouch{};
std::array<bool, vr::k_unMaxTrackedDeviceCount> g_knucklesGripStableTouch{};
std::array<bool, vr::k_unMaxTrackedDeviceCount> g_knucklesGripArmed{};
std::array<bool, vr::k_unMaxTrackedDeviceCount> g_knucklesGripActive{};
std::array<std::chrono::steady_clock::time_point, vr::k_unMaxTrackedDeviceCount> g_knucklesGripRawChanged{};

std::array<int, vr::k_unMaxTrackedDeviceCount> g_gripAnalogAxis = [] {
    std::array<int, vr::k_unMaxTrackedDeviceCount> result{};
    result.fill(-2); // -2 unknown, -1 unsupported
    return result;
}();

// Wrist-dot calibration grab is separate from the full keyboard grab.  The
// dot remains wrist-bound; dragging only rewrites wristDotOffset X/Y/Z.
bool g_wristGripGrabActive = false;
vr::TrackedDeviceIndex_t g_wristGripGrabDevice = vr::k_unTrackedDeviceIndexInvalid;
vr::HmdMatrix34_t g_wristGripGrabRelative{};
std::array<bool, vr::k_unMaxTrackedDeviceCount> g_wristGripWasDown{};

// V3.4 wrist standby / summon state. These positions are session-only because
// SteamVR Standing-space coordinates may change between room setup sessions.
bool g_wristStandby = false;
// Current device selected for the wrist launcher. Unlike the old role-only
// binding, V3.9.8 can also recover a hand by its physical left/right position.
vr::TrackedDeviceIndex_t g_wristTrackedDevice = vr::k_unTrackedDeviceIndexInvalid;
bool g_wristUsingHmdFallback = false;
vr::HmdMatrix34_t g_wristAbsoluteTransform{};
bool g_wristAbsoluteTransformValid = false;
// The launcher quad is billboarded toward the HMD, so OpenVR backface culling
// cannot tell whether the *physical* back of the hand is facing the user.
// Derive that visibility from the calibrated wrist-local offset instead.
bool g_wristDotFacingViewer = true;
// Outside the SteamVR Dashboard the validated wrist path owns pointer/Trigger
// input. While the Dashboard is visible, SteamVR must own mouse routing or its
// system UI input focus prevents the raw/manual Trigger path from clicking.
bool g_wristDashboardNativeInput = false;

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

bool IsExtendedVirtualKey(WORD vk) {
    switch (vk) {
        case VK_RMENU:
        case VK_RCONTROL:
        case VK_INSERT:
        case VK_DELETE:
        case VK_HOME:
        case VK_END:
        case VK_PRIOR:
        case VK_NEXT:
        case VK_LEFT:
        case VK_UP:
        case VK_RIGHT:
        case VK_DOWN:
        case VK_NUMLOCK:
        case VK_CANCEL:
        case VK_SNAPSHOT:
        case VK_DIVIDE:
        case VK_LWIN:
        case VK_RWIN:
        case VK_APPS:
            return true;
        default:
            return false;
    }
}

void SendVk(WORD vk, bool down, bool forceExtended = false) {
    if (g_previewMode) return;
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = vk;
    input.ki.dwFlags = (down ? 0 : KEYEVENTF_KEYUP) |
                       ((forceExtended || IsExtendedVirtualKey(vk)) ? KEYEVENTF_EXTENDEDKEY : 0);
    SendInput(1, &input, sizeof(INPUT));
}

void SendTapRaw(WORD vk, bool forceExtended = false) {
    SendVk(vk, true, forceExtended);
    SendVk(vk, false, forceExtended);
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

void SendKey(WORD vk, bool forceExtended = false) {
    PressActiveModifiers();
    SendTapRaw(vk, forceExtended);
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

void TrackHoverHaptic(ImGuiID itemId, bool hovered) {
    if (!hovered) return;
    g_hoverHapticSeenThisFrame = true;
    if (g_hoverHapticItem == itemId) return;
    g_hoverHapticItem = itemId;
    if (g_ui.hoverHapticsEnabled) PulseHaptic(g_ui.hoverHapticScale);
}

void FinishHoverHapticsFrame() {
    if (!g_hoverHapticSeenThisFrame) g_hoverHapticItem = 0;
    g_hoverHapticSeenThisFrame = false;
}

bool SelectedButton(const char* label, bool selected, ImVec2 size) {
    if (selected) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.48f, 0.78f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.56f, 0.90f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.14f, 0.40f, 0.68f, 1.0f));
    }
    const ImGuiID itemId = ImGui::GetID(label);
    const bool clicked = ImGui::Button(label, size);
    TrackHoverHaptic(itemId, ImGui::IsItemHovered());
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
    const ImGuiID itemId = ImGui::GetID("##editor_centered_button");
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
    TrackHoverHaptic(itemId, hovered);
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
    TrackHoverHaptic(itemId, hovered && !activated);
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
    Key("backspace", "BACKSPACE", VK_BACK, 2.05f, KeyZone::Blue);

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
    Key("del", "DELETE", VK_DELETE, 1.0f, KeyZone::Blue); SameKey();
    Key("end", "END", VK_END, 1.0f, KeyZone::Blue); SameKey();
    Key("pgdn", "PAGE\nDOWN", VK_NEXT, 1.0f, KeyZone::Blue, "", "", true);

    ImGui::Dummy(ImVec2(1, 8 * KeyboardScale()));
    ImGui::Indent((W(1.0f) + g_ui.gap) * KeyboardScale());
    Key("up", "↑", VK_UP, 1.0f, KeyZone::Blue);
    ImGui::Unindent((W(1.0f) + g_ui.gap) * KeyboardScale());
    Key("left", "←", VK_LEFT, 1.0f, KeyZone::Blue); SameKey();
    Key("down", "↓", VK_DOWN, 1.0f, KeyZone::Blue); SameKey();
    Key("right", "→", VK_RIGHT, 1.0f, KeyZone::Blue);
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
    // Numpad Enter shares VK_RETURN with the main Enter key but is identified
    // by the Windows extended-key bit. Keep the two physical keys distinct.
    if (PhysicalKeyButton("numenter", "ENTER", "", "", ImVec2(W(1.0f), g_ui.keyHeight * 2.0f + g_ui.gap), KeyZone::Purple, false, true)) SendKey(VK_RETURN, true);
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
    g_widthMeters = DEFAULT_OVERLAY_WIDTH_M;
    g_distance = DEFAULT_OVERLAY_DISTANCE_M;
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
    out << "hoverHapticsEnabled=" << (g_ui.hoverHapticsEnabled ? 1 : 0) << '\n';
    out << "hoverHapticScale=" << g_ui.hoverHapticScale << '\n';
    out << "showInteractionPointer=" << (g_ui.showInteractionPointer ? 1 : 0) << '\n';
    out << "pointerScale=" << g_ui.pointerScale << '\n';
    out << "repeatEnabled=" << (g_ui.repeatEnabled ? 1 : 0) << '\n';
    out << "repeatDelay=" << g_ui.repeatDelay << '\n';
    out << "repeatRate=" << g_ui.repeatRate << '\n';
    out << "autoFadeEnabled=" << (g_ui.autoFadeEnabled ? 1 : 0) << '\n';
    out << "autoFadeSeconds=" << g_ui.autoFadeSeconds << '\n';
    out << "autoFadeAlpha=" << g_ui.autoFadeAlpha << '\n';
    out << "wristUseLeftHand=" << (g_ui.wristUseLeftHand ? 1 : 0) << '\n';
    out << "startInWristStandby=" << (g_ui.startInWristStandby ? 1 : 0) << '\n';
    out << "overlayWidthMeters=" << g_widthMeters << '\n';
    out << "overlayDistanceMeters=" << g_distance << '\n';
    out << "wristWidthMeters=" << g_ui.wristWidthMeters << '\n';
    out << "wristDotSizeMeters=" << g_ui.wristDotSizeMeters << '\n';
    out << "wristDotAlpha=" << g_ui.wristDotAlpha << '\n';
    out << "wristDotOffsetX=" << g_ui.wristDotOffsetX << '\n';
    out << "wristDotOffsetY=" << g_ui.wristDotOffsetY << '\n';
    out << "wristDotOffsetZ=" << g_ui.wristDotOffsetZ << '\n';
    out << "wristHitScale=" << g_ui.wristHitScale << '\n';
    out << "wristDoubleClickSeconds=" << g_ui.wristDoubleClickSeconds << '\n';
    out << "wristLongPressSeconds=" << g_ui.wristLongPressSeconds << '\n';
    out << "layoutGripGrabEnabled=" << (g_ui.layoutGripGrabEnabled ? 1 : 0) << '\n';
    out << "autoFaceOnRelease=" << (g_ui.autoFaceOnRelease ? 1 : 0) << '\n';
    out << "grabPositionDamping=" << g_ui.grabPositionDamping << '\n';
    out << "grabRotationDamping=" << g_ui.grabRotationDamping << '\n';
    out << "pushPullSpeed=" << g_ui.pushPullSpeed << '\n';
    out << "scaleSpeed=" << g_ui.scaleSpeed << '\n';
    out << "curvedOverlay=" << (g_ui.curvedOverlay ? 1 : 0) << '\n';
    out << "overlayCurvature=" << g_ui.overlayCurvature << '\n';
    out << "adaptiveTextureUpdates=" << (g_ui.adaptiveTextureUpdates ? 1 : 0) << '\n';
    out << "activeTextureFps=" << g_ui.activeTextureFps << '\n';
    out << "idleTextureFps=" << g_ui.idleTextureFps << '\n';
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
    // V3.9.9 migration marker: older INIs have wristDotSizeMeters but no
    // calibration keys. That lets us safely replace the oversized legacy
    // launcher default once without touching later user calibration.
    bool hasWristDotCalibrationKeys = false;
    bool loadedLegacyWristDotSize = false;
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
        else if (key == "hoverHapticsEnabled") g_ui.hoverHapticsEnabled = value > 0.5f;
        else if (key == "hoverHapticScale") g_ui.hoverHapticScale = value;
        else if (key == "showInteractionPointer") g_ui.showInteractionPointer = value > 0.5f;
        else if (key == "pointerScale") g_ui.pointerScale = value;
        else if (key == "repeatEnabled") g_ui.repeatEnabled = value > 0.5f;
        else if (key == "repeatDelay") g_ui.repeatDelay = value;
        else if (key == "repeatRate") g_ui.repeatRate = value;
        else if (key == "autoFadeEnabled") g_ui.autoFadeEnabled = value > 0.5f;
        else if (key == "autoFadeSeconds") g_ui.autoFadeSeconds = value;
        else if (key == "autoFadeAlpha") g_ui.autoFadeAlpha = value;
        else if (key == "wristUseLeftHand") g_ui.wristUseLeftHand = value > 0.5f;
        else if (key == "startInWristStandby") g_ui.startInWristStandby = value > 0.5f;
        else if (key == "overlayWidthMeters") g_widthMeters = value;
        else if (key == "overlayDistanceMeters") g_distance = value;
        else if (key == "wristWidthMeters") g_ui.wristWidthMeters = value;
        else if (key == "wristDotSizeMeters") { g_ui.wristDotSizeMeters = value; loadedLegacyWristDotSize = true; }
        else if (key == "wristDotAlpha") g_ui.wristDotAlpha = value;
        else if (key == "wristDotOffsetX") { g_ui.wristDotOffsetX = value; hasWristDotCalibrationKeys = true; }
        else if (key == "wristDotOffsetY") { g_ui.wristDotOffsetY = value; hasWristDotCalibrationKeys = true; }
        else if (key == "wristDotOffsetZ") { g_ui.wristDotOffsetZ = value; hasWristDotCalibrationKeys = true; }
        else if (key == "wristHitScale") g_ui.wristHitScale = value;
        else if (key == "wristDoubleClickSeconds") g_ui.wristDoubleClickSeconds = value;
        else if (key == "wristLongPressSeconds") g_ui.wristLongPressSeconds = value;
        else if (key == "layoutGripGrabEnabled") g_ui.layoutGripGrabEnabled = value > 0.5f;
        else if (key == "autoFaceOnRelease") g_ui.autoFaceOnRelease = value > 0.5f;
        else if (key == "grabPositionDamping") g_ui.grabPositionDamping = value;
        else if (key == "grabRotationDamping") g_ui.grabRotationDamping = value;
        else if (key == "pushPullSpeed") g_ui.pushPullSpeed = value;
        else if (key == "scaleSpeed") g_ui.scaleSpeed = value;
        else if (key == "curvedOverlay") g_ui.curvedOverlay = value > 0.5f;
        else if (key == "overlayCurvature") g_ui.overlayCurvature = value;
        else if (key == "adaptiveTextureUpdates") g_ui.adaptiveTextureUpdates = value > 0.5f;
        else if (key == "activeTextureFps") g_ui.activeTextureFps = value;
        else if (key == "idleTextureFps") g_ui.idleTextureFps = value;
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
    // Existing 3.9.7/3.9.8 users commonly carry the old 0.085 m value.
    // No calibration keys means this is an old-format INI, so migrate it to
    // the new compact launcher. A 3.9.9+ INI keeps the user's chosen size.
    if (loadedLegacyWristDotSize && !hasWristDotCalibrationKeys) {
        g_ui.wristDotSizeMeters = 0.045f;
        g_ui.wristDotOffsetX = 0.0f;
        g_ui.wristDotOffsetY = 0.0f;
        g_ui.wristDotOffsetZ = 0.0f;
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
    g_ui.hoverHapticScale = std::clamp(g_ui.hoverHapticScale, 0.05f, 0.50f);
    g_ui.pointerScale = std::clamp(g_ui.pointerScale, 0.50f, 2.50f);
    g_ui.repeatDelay = std::clamp(g_ui.repeatDelay, 0.20f, 0.80f);
    g_ui.repeatRate = std::clamp(g_ui.repeatRate, 0.035f, 0.20f);
    g_ui.autoFadeSeconds = std::clamp(g_ui.autoFadeSeconds, 2.0f, 60.0f);
    g_ui.autoFadeAlpha = std::clamp(g_ui.autoFadeAlpha, 0.08f, 0.80f);
    g_widthMeters = std::clamp(g_widthMeters, 0.80f, 2.50f);
    g_distance = std::clamp(g_distance, 0.45f, 1.50f);
    g_ui.wristWidthMeters = std::clamp(g_ui.wristWidthMeters, 0.18f, 0.48f);
    g_ui.wristDotSizeMeters = std::clamp(g_ui.wristDotSizeMeters, 0.025f, 0.080f);
    g_ui.wristDotAlpha = std::clamp(g_ui.wristDotAlpha, 0.30f, 1.00f);
    g_ui.wristDotOffsetX = std::clamp(g_ui.wristDotOffsetX, -0.15f, 0.15f);
    g_ui.wristDotOffsetY = std::clamp(g_ui.wristDotOffsetY, -0.15f, 0.15f);
    g_ui.wristDotOffsetZ = std::clamp(g_ui.wristDotOffsetZ, -0.15f, 0.15f);
    g_ui.wristHitScale = std::clamp(g_ui.wristHitScale, 0.45f, 1.00f);
    g_ui.wristDoubleClickSeconds = std::clamp(g_ui.wristDoubleClickSeconds, 0.18f, 0.55f);
    g_ui.wristLongPressSeconds = std::clamp(g_ui.wristLongPressSeconds, 0.40f, 1.50f);
    g_ui.grabPositionDamping = std::clamp(g_ui.grabPositionDamping, 0.0f, 0.95f);
    g_ui.grabRotationDamping = std::clamp(g_ui.grabRotationDamping, 0.0f, 0.95f);
    g_ui.pushPullSpeed = std::clamp(g_ui.pushPullSpeed, 0.05f, 1.00f);
    g_ui.scaleSpeed = std::clamp(g_ui.scaleSpeed, 0.05f, 1.00f);
    g_ui.overlayCurvature = std::clamp(g_ui.overlayCurvature, 0.0f, 0.45f);
    g_ui.activeTextureFps = std::clamp(g_ui.activeTextureFps, 45.0f, 144.0f);
    g_ui.idleTextureFps = std::clamp(g_ui.idleTextureFps, 10.0f, 60.0f);
    g_ui.idleTextureFps = (std::min)(g_ui.idleTextureFps, g_ui.activeTextureFps);
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
bool g_running = true;

static void EnsureControlExitEvent() {
    if (g_controlExitEvent) return;
    g_controlExitEvent = CreateEventW(nullptr, TRUE, FALSE, kControlExitEventName);
    if (g_controlExitEvent) {
        ResetEvent(g_controlExitEvent);
        std::error_code ec;
        std::filesystem::remove(PerfLivePath(), ec);
    }
}

static bool ControlCenterRequestedExit() {
    return g_controlExitEvent && WaitForSingleObject(g_controlExitEvent, 0) == WAIT_OBJECT_0;
}

static void CloseControlExitEvent() {
    if (g_controlExitEvent) {
        CloseHandle(g_controlExitEvent);
        g_controlExitEvent = nullptr;
    }
}

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

float DampingBlend(float damping, float dt) {
    if (damping <= 0.0001f) return 1.0f;
    const float retainedPer90HzFrame = std::clamp(damping, 0.0f, 0.95f);
    return 1.0f - std::pow(retainedPer90HzFrame, std::clamp(dt, 0.0f, 0.05f) * 90.0f);
}

vr::HmdMatrix34_t BlendRigid34(const vr::HmdMatrix34_t& current,
                               const vr::HmdMatrix34_t& target,
                               float positionBlend,
                               float rotationBlend) {
    vr::HmdMatrix34_t out = current;
    for (int row = 0; row < 3; ++row) {
        out.m[row][3] += (target.m[row][3] - current.m[row][3]) * positionBlend;
        for (int column = 0; column < 3; ++column) {
            out.m[row][column] += (target.m[row][column] - current.m[row][column]) * rotationBlend;
        }
    }

    // Re-orthonormalize the blended basis so the compositor always receives
    // a rigid transform even after independent rotation damping.
    auto normalize = [](float& x, float& y, float& z) {
        const float len = std::sqrt(x * x + y * y + z * z);
        if (len < 0.00001f) return false;
        x /= len; y /= len; z /= len;
        return true;
    };

    float zx = out.m[0][2], zy = out.m[1][2], zz = out.m[2][2];
    if (!normalize(zx, zy, zz)) {
        zx = target.m[0][2]; zy = target.m[1][2]; zz = target.m[2][2];
        normalize(zx, zy, zz);
    }

    float xx = out.m[0][0], xy = out.m[1][0], xz = out.m[2][0];
    const float xDotZ = xx * zx + xy * zy + xz * zz;
    xx -= xDotZ * zx; xy -= xDotZ * zy; xz -= xDotZ * zz;
    if (!normalize(xx, xy, xz)) {
        xx = target.m[0][0]; xy = target.m[1][0]; xz = target.m[2][0];
        const float fallbackDot = xx * zx + xy * zy + xz * zz;
        xx -= fallbackDot * zx; xy -= fallbackDot * zy; xz -= fallbackDot * zz;
        normalize(xx, xy, xz);
    }

    const float yx = zy * xz - zz * xy;
    const float yy = zz * xx - zx * xz;
    const float yz = zx * xy - zy * xx;
    out.m[0][0] = xx; out.m[0][1] = yx; out.m[0][2] = zx;
    out.m[1][0] = xy; out.m[1][1] = yy; out.m[1][2] = zy;
    out.m[2][0] = xz; out.m[2][1] = yz; out.m[2][2] = zz;
    return out;
}

bool GetAbsoluteDevicePose(vr::TrackedDeviceIndex_t device, vr::HmdMatrix34_t& out) {
    auto* sys = vr::VRSystem();
    if (!sys || device == vr::k_unTrackedDeviceIndexInvalid || device >= vr::k_unMaxTrackedDeviceCount) return false;

    // Latency regression fix: query SteamVR at the moment the pose is needed.
    // V3.9.10 cached one snapshot at frame start; together with frame sleeping,
    // that made pointer/overlay transforms visibly trail the controller.
    vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount]{};
    sys->GetDeviceToAbsoluteTrackingPose(
        vr::TrackingUniverseStanding,
        0.0f,
        poses,
        vr::k_unMaxTrackedDeviceCount);
    if (!poses[device].bPoseIsValid) return false;
    out = poses[device].mDeviceToAbsoluteTracking;
    return true;
}

bool FaceTransformTowardHmd(vr::HmdMatrix34_t& transform) {
    vr::HmdMatrix34_t hmdPose{};
    if (!GetAbsoluteDevicePose(vr::k_unTrackedDeviceIndex_Hmd, hmdPose)) return false;

    const float px = transform.m[0][3];
    const float py = transform.m[1][3];
    const float pz = transform.m[2][3];
    float zx = hmdPose.m[0][3] - px;
    float zy = hmdPose.m[1][3] - py;
    float zz = hmdPose.m[2][3] - pz;
    const float zLength = std::sqrt(zx * zx + zy * zy + zz * zz);
    if (zLength < 0.0001f) return false;
    zx /= zLength; zy /= zLength; zz /= zLength;

    // Prefer world-up so releasing a grab also levels the keyboard. Near the
    // vertical singularity, fall back to the HMD's current right axis.
    float xx = zz;
    float xy = 0.0f;
    float xz = -zx;
    float xLength = std::sqrt(xx * xx + xz * xz);
    if (xLength < 0.0001f) {
        xx = hmdPose.m[0][0];
        xy = hmdPose.m[1][0];
        xz = hmdPose.m[2][0];
        xLength = std::sqrt(xx * xx + xy * xy + xz * xz);
    }
    if (xLength < 0.0001f) return false;
    xx /= xLength; xy /= xLength; xz /= xLength;

    float yx = zy * xz - zz * xy;
    float yy = zz * xx - zx * xz;
    float yz = zx * xy - zy * xx;
    const float yLength = std::sqrt(yx * yx + yy * yy + yz * yz);
    if (yLength < 0.0001f) return false;
    yx /= yLength; yy /= yLength; yz /= yLength;

    transform.m[0][0] = xx; transform.m[0][1] = yx; transform.m[0][2] = zx;
    transform.m[1][0] = xy; transform.m[1][1] = yy; transform.m[1][2] = zy;
    transform.m[2][0] = xz; transform.m[2][1] = yz; transform.m[2][2] = zz;
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

bool ResolveWristAbsolute(vr::TrackedDeviceIndex_t& device, vr::HmdMatrix34_t& absolute, bool& usingHmdFallback) {
    auto* sys = vr::VRSystem();
    if (!sys) return false;

    vr::HmdMatrix34_t hmdPose{};
    if (!GetAbsoluteDevicePose(vr::k_unTrackedDeviceIndex_Hmd, hmdPose)) return false;

    const bool wantLeft = g_ui.wristUseLeftHand;
    const vr::ETrackedControllerRole desiredRole = wantLeft
        ? vr::TrackedControllerRole_LeftHand
        : vr::TrackedControllerRole_RightHand;

    vr::TrackedDeviceIndex_t selected = sys->GetTrackedDeviceIndexForControllerRole(desiredRole);
    vr::HmdMatrix34_t controllerPose{};
    bool selectedValid = selected != vr::k_unTrackedDeviceIndexInvalid &&
                         sys->GetTrackedDeviceClass(selected) == vr::TrackedDeviceClass_Controller &&
                         GetAbsoluteDevicePose(selected, controllerPose);

    if (!selectedValid) {
        // Some OpenXR->SteamVR controller stacks do not publish controller roles
        // reliably while a scene application (for example VRChat) owns input.
        // Recover the requested hand by comparing every tracked controller's
        // physical position against the HMD's local right axis.
        bool found = false;
        float bestScore = wantLeft ? FLT_MAX : -FLT_MAX;
        vr::TrackedDeviceIndex_t bestDevice = vr::k_unTrackedDeviceIndexInvalid;
        vr::HmdMatrix34_t bestPose{};

        const float hx = hmdPose.m[0][3];
        const float hy = hmdPose.m[1][3];
        const float hz = hmdPose.m[2][3];
        // Column 0 is the HMD local +X/right axis in Standing space.
        const float rx = hmdPose.m[0][0];
        const float ry = hmdPose.m[1][0];
        const float rz = hmdPose.m[2][0];

        for (vr::TrackedDeviceIndex_t i = 0; i < vr::k_unMaxTrackedDeviceCount; ++i) {
            if (sys->GetTrackedDeviceClass(i) != vr::TrackedDeviceClass_Controller) continue;
            vr::HmdMatrix34_t pose{};
            if (!GetAbsoluteDevicePose(i, pose)) continue;

            const float dx = pose.m[0][3] - hx;
            const float dy = pose.m[1][3] - hy;
            const float dz = pose.m[2][3] - hz;
            const float sideScore = dx * rx + dy * ry + dz * rz;

            if (!found || (wantLeft ? sideScore < bestScore : sideScore > bestScore)) {
                found = true;
                bestScore = sideScore;
                bestDevice = i;
                bestPose = pose;
            }
        }

        if (found) {
            selected = bestDevice;
            controllerPose = bestPose;
            selectedValid = true;
        }
    }

    if (!selectedValid) {
        // Keep a discoverable fallback while SteamVR is still establishing
        // controller tracking. As soon as a real hand pose appears this
        // function switches back automatically on the next frame.
        device = vr::k_unTrackedDeviceIndex_Hmd;
        usingHmdFallback = true;
        g_wristDotFacingViewer = true;
        absolute = Multiply34(hmdPose, MakeTransform(wantLeft ? -0.24f : 0.24f, -0.22f, -0.48f, -8.0f));
        return true;
    }

    device = selected;
    usingHmdFallback = false;

    // Start exactly at the runtime's tracked grip origin, then apply the
    // user's small controller-local calibration. Different runtimes expose
    // noticeably different grip origins, so these offsets are intentionally
    // configurable instead of hard-coded.
    const float localX = g_ui.wristDotOffsetX;
    const float localY = g_ui.wristDotOffsetY;
    const float localZ = g_ui.wristDotOffsetZ;
    const float px = controllerPose.m[0][3] +
                     controllerPose.m[0][0] * localX +
                     controllerPose.m[0][1] * localY +
                     controllerPose.m[0][2] * localZ;
    const float py = controllerPose.m[1][3] +
                     controllerPose.m[1][0] * localX +
                     controllerPose.m[1][1] * localY +
                     controllerPose.m[1][2] * localZ;
    const float pz = controllerPose.m[2][3] +
                     controllerPose.m[2][0] * localX +
                     controllerPose.m[2][1] * localY +
                     controllerPose.m[2][2] * localZ;

    // The visible quad always faces the HMD for readability, therefore
    // NoBackside cannot express the natural wrist-launcher rule: visible when
    // the calibrated hand surface faces the user, hidden when the hand flips
    // away. The saved local offset doubles as that surface normal. This also
    // adapts to users who place the dot at different points on Index/Touch/etc.
    // Tiny/unconfigured offsets keep the legacy always-visible behavior.
    const float offsetLen = std::sqrt(localX * localX + localY * localY + localZ * localZ);
    if (offsetLen > 0.012f && !g_wristGripGrabActive) {
        const float lx = localX / offsetLen;
        const float ly = localY / offsetLen;
        const float lz = localZ / offsetLen;
        const float nx = controllerPose.m[0][0] * lx + controllerPose.m[0][1] * ly + controllerPose.m[0][2] * lz;
        const float ny = controllerPose.m[1][0] * lx + controllerPose.m[1][1] * ly + controllerPose.m[1][2] * lz;
        const float nz = controllerPose.m[2][0] * lx + controllerPose.m[2][1] * ly + controllerPose.m[2][2] * lz;
        float vx = hmdPose.m[0][3] - px;
        float vy = hmdPose.m[1][3] - py;
        float vz = hmdPose.m[2][3] - pz;
        const float vLen = std::sqrt(vx * vx + vy * vy + vz * vz);
        if (vLen > 0.0001f) {
            vx /= vLen; vy /= vLen; vz /= vLen;
            // Small positive dead-zone avoids flicker when the wrist is almost
            // exactly edge-on to the viewer.
            g_wristDotFacingViewer = (nx * vx + ny * vy + nz * vz) > 0.05f;
        } else {
            g_wristDotFacingViewer = true;
        }
    } else {
        // Keep the dot visible while Grip-calibrating so it cannot disappear
        // under the user's hand halfway through a reposition gesture.
        g_wristDotFacingViewer = true;
    }

    // Face the small launcher toward the user's HMD every frame. Controller
    // local rotations vary noticeably between Index/Quest/Pico/WMR runtimes,
    // which was the main reason the old relative transform could look "off hand".
    float zx = hmdPose.m[0][3] - px;
    float zy = hmdPose.m[1][3] - py;
    float zz = hmdPose.m[2][3] - pz;
    float zLen = std::sqrt(zx * zx + zy * zy + zz * zz);
    if (zLen < 0.0001f) return false;
    zx /= zLen; zy /= zLen; zz /= zLen;

    // right = worldUp x forwardToViewer
    float xx = zz;
    float xy = 0.0f;
    float xz = -zx;
    float xLen = std::sqrt(xx * xx + xz * xz);
    if (xLen < 0.0001f) {
        xx = 1.0f; xy = 0.0f; xz = 0.0f;
    } else {
        xx /= xLen; xz /= xLen;
    }

    // up = forwardToViewer x right
    float yx = zy * xz - zz * xy;
    float yy = zz * xx - zx * xz;
    float yz = zx * xy - zy * xx;
    float yLen = std::sqrt(yx * yx + yy * yy + yz * yz);
    if (yLen < 0.0001f) {
        yx = 0.0f; yy = 1.0f; yz = 0.0f;
    } else {
        yx /= yLen; yy /= yLen; yz /= yLen;
    }

    vr::HmdMatrix34_t m{};
    // Matrix columns are the overlay local X/Y/Z axes in Standing space.
    m.m[0][0] = xx; m.m[0][1] = yx; m.m[0][2] = zx; m.m[0][3] = px;
    m.m[1][0] = xy; m.m[1][1] = yy; m.m[1][2] = zy; m.m[1][3] = py;
    m.m[2][0] = xz; m.m[2][1] = yz; m.m[2][2] = zz; m.m[2][3] = pz;
    absolute = m;
    return true;
}

bool GetCurrentOverlayAbsolute(vr::HmdMatrix34_t& out) {
    if (g_wristStandby) {
        vr::TrackedDeviceIndex_t wristDevice = vr::k_unTrackedDeviceIndexInvalid;
        bool hmdFallback = false;
        return ResolveWristAbsolute(wristDevice, out, hmdFallback);
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

void MarkVrInteraction(bool forceTextureSubmit = false);

bool BeginVrGrab(vr::VROverlayHandle_t overlay,
                 GrabInputSource inputSource = GrabInputSource::TriggerHandle,
                 vr::TrackedDeviceIndex_t preferredController = vr::k_unTrackedDeviceIndexInvalid) {
    if (g_previewMode || g_wristStandby || overlay == vr::k_ulOverlayHandleInvalid || g_vrGrabActive) return false;
    auto* ov = vr::VROverlay();
    auto* sys = vr::VRSystem();
    if (!ov || !sys) return false;

    const bool preferredValid = preferredController != vr::k_unTrackedDeviceIndexInvalid &&
                                preferredController < vr::k_unMaxTrackedDeviceCount &&
                                sys->GetTrackedDeviceClass(preferredController) == vr::TrackedDeviceClass_Controller;
    const vr::TrackedDeviceIndex_t controller = preferredValid ? preferredController : ChooseGrabController();
    if (controller == vr::k_unTrackedDeviceIndexInvalid) return false;

    vr::HmdMatrix34_t controllerAbs{};
    vr::HmdMatrix34_t overlayAbs{};
    if (!GetAbsoluteDevicePose(controller, controllerAbs) || !GetCurrentOverlayAbsolute(overlayAbs)) return false;

    g_vrGrabDevice = controller;
    g_lastPointerDevice = controller;
    g_vrGrabRelative = Multiply34(InverseRigid34(controllerAbs), overlayAbs);
    g_vrGrabSmoothedAbsolute = overlayAbs;
    g_vrGrabInputSource = inputSource;
    g_vrGrabLastUpdate = std::chrono::steady_clock::now();
    g_vrGrabActive = true;
    if (g_ui.grabPositionDamping <= 0.0001f && g_ui.grabRotationDamping <= 0.0001f) {
        // Preserve TB02's direct compositor-tracked grab when damping is off.
        ov->SetOverlayTransformTrackedDeviceRelative(overlay, controller, &g_vrGrabRelative);
    } else {
        ov->SetOverlayTransformAbsolute(overlay, vr::TrackingUniverseStanding, &g_vrGrabSmoothedAbsolute);
    }
    MarkVrInteraction(true);
    PulseHaptic(0.55f);
    return true;
}

float ApplyAxisDeadzone(float value, float deadzone = 0.18f) {
    const float magnitude = std::fabs(value);
    if (magnitude <= deadzone) return 0.0f;
    const float normalized = (magnitude - deadzone) / (1.0f - deadzone);
    return std::copysign(normalized * normalized, value);
}

int FindPrimaryControllerAxis(vr::TrackedDeviceIndex_t device) {
    static std::array<int, vr::k_unMaxTrackedDeviceCount> cached = [] {
        std::array<int, vr::k_unMaxTrackedDeviceCount> result{};
        result.fill(-2);
        return result;
    }();
    if (device >= vr::k_unMaxTrackedDeviceCount) return -1;
    int& cachedAxis = cached[device];
    if (cachedAxis != -2) return cachedAxis;

    cachedAxis = -1;
    auto* sys = vr::VRSystem();
    if (!sys) return -1;
    int trackpadFallback = -1;
    for (int axis = 0; axis < vr::k_unControllerStateAxisCount; ++axis) {
        const auto property = static_cast<vr::ETrackedDeviceProperty>(
            static_cast<int>(vr::Prop_Axis0Type_Int32) + axis);
        const int32_t axisType = sys->GetInt32TrackedDeviceProperty(device, property);
        if (axisType == vr::k_eControllerAxis_Joystick) {
            cachedAxis = axis;
            return cachedAxis;
        }
        if (axisType == vr::k_eControllerAxis_TrackPad && trackpadFallback < 0)
            trackpadFallback = axis;
    }
    // Do not guess axis 0: on some runtimes it is a Trigger, which would turn
    // a grab squeeze into unintended scaling. Unsupported mappings simply
    // keep grab movement available without thumbstick manipulation.
    cachedAxis = trackpadFallback;
    return cachedAxis;
}

void UpdateVrGrabTransform(vr::VROverlayHandle_t overlay) {
    if (!g_vrGrabActive || g_previewMode || overlay == vr::k_ulOverlayHandleInvalid) return;
    auto* ov = vr::VROverlay();
    auto* sys = vr::VRSystem();
    if (!ov || !sys) return;

    const auto now = std::chrono::steady_clock::now();
    const float dt = std::clamp(
        std::chrono::duration<float>(now - g_vrGrabLastUpdate).count(), 0.0f, 0.05f);
    g_vrGrabLastUpdate = now;

    bool controlsChanged = false;
    if (g_interactionMode == InteractionMode::Layout) {
        vr::VRControllerState_t state{};
        if (sys->GetControllerState(g_vrGrabDevice, &state, sizeof(state))) {
            const int axis = FindPrimaryControllerAxis(g_vrGrabDevice);
            if (axis >= 0 && axis < vr::k_unControllerStateAxisCount) {
                const float scaleAxis = ApplyAxisDeadzone(state.rAxis[axis].x);
                const float distanceAxis = ApplyAxisDeadzone(state.rAxis[axis].y);
                if (std::fabs(distanceAxis) > 0.0001f) {
                    g_vrGrabRelative.m[2][3] = std::clamp(
                        g_vrGrabRelative.m[2][3] - distanceAxis * g_ui.pushPullSpeed * dt,
                        -2.50f, -0.15f);
                    controlsChanged = true;
                }
                if (std::fabs(scaleAxis) > 0.0001f) {
                    g_widthMeters = std::clamp(
                        g_widthMeters + scaleAxis * g_ui.scaleSpeed * dt,
                        0.80f, 2.50f);
                    controlsChanged = true;
                }
            }
        }
    }

    const bool useDamping = g_ui.grabPositionDamping > 0.0001f ||
                            g_ui.grabRotationDamping > 0.0001f;
    if (!useDamping) {
        if (controlsChanged) {
            ov->SetOverlayTransformTrackedDeviceRelative(overlay, g_vrGrabDevice, &g_vrGrabRelative);
            ov->SetOverlayWidthInMeters(overlay, g_widthMeters);
            MarkVrInteraction();
        }
        return;
    }

    vr::HmdMatrix34_t controllerAbsolute{};
    if (!GetAbsoluteDevicePose(g_vrGrabDevice, controllerAbsolute)) return;
    const vr::HmdMatrix34_t target = Multiply34(controllerAbsolute, g_vrGrabRelative);
    g_vrGrabSmoothedAbsolute = BlendRigid34(
        g_vrGrabSmoothedAbsolute,
        target,
        DampingBlend(g_ui.grabPositionDamping, dt),
        DampingBlend(g_ui.grabRotationDamping, dt));
    ov->SetOverlayTransformAbsolute(overlay, vr::TrackingUniverseStanding, &g_vrGrabSmoothedAbsolute);
    if (controlsChanged) {
        ov->SetOverlayWidthInMeters(overlay, g_widthMeters);
        MarkVrInteraction();
    }
}

void EndVrGrab(vr::VROverlayHandle_t overlay) {
    if (!g_vrGrabActive) return;
    auto* ov = vr::VROverlay();

    vr::HmdMatrix34_t controllerAbs{};
    if (ov && GetAbsoluteDevicePose(g_vrGrabDevice, controllerAbs)) {
        const bool useDamping = g_ui.grabPositionDamping > 0.0001f ||
                                g_ui.grabRotationDamping > 0.0001f;
        g_worldTransform = useDamping
            ? g_vrGrabSmoothedAbsolute
            : Multiply34(controllerAbs, g_vrGrabRelative);
        if (g_ui.autoFaceOnRelease) FaceTransformTowardHmd(g_worldTransform);
        g_worldTransformValid = true;
        g_anchor = AnchorMode::WorldFixed;
        ov->SetOverlayTransformAbsolute(overlay, vr::TrackingUniverseStanding, &g_worldTransform);
        ov->SetOverlayWidthInMeters(overlay, g_widthMeters);
    }

    g_vrGrabActive = false;
    g_vrGrabDevice = vr::k_unTrackedDeviceIndexInvalid;
    g_vrGrabInputSource = GrabInputSource::None;
    MarkVrInteraction(true);
    PulseHaptic(0.35f);
    if (g_autoSave) SaveSettings();
}

void CancelVrGrabState() {
    g_vrGrabActive = false;
    g_vrGrabDevice = vr::k_unTrackedDeviceIndexInvalid;
    g_vrGrabInputSource = GrabInputSource::None;
    g_layoutGripWasDown.fill(false);
    g_gripEventKnown.fill(false);
    g_gripEventDown.fill(false);
    g_gripTouchEventKnown.fill(false);
    g_gripTouchEventDown.fill(false);
    g_knucklesGripInitialized.fill(false);
    g_knucklesGripRawTouch.fill(false);
    g_knucklesGripStableTouch.fill(false);
    g_knucklesGripArmed.fill(false);
    g_knucklesGripActive.fill(false);
    g_knucklesGripRawChanged.fill({});
    g_wristGripGrabActive = false;
    g_wristGripGrabDevice = vr::k_unTrackedDeviceIndexInvalid;
    g_wristGripWasDown.fill(false);
}

bool FaceOverlayTowardHmd(vr::VROverlayHandle_t overlay) {
    if (g_previewMode) {
        g_vrPlacementStatus = "桌面模擬：已執行面向我";
        return true;
    }
    if (g_vrGrabActive || overlay == vr::k_ulOverlayHandleInvalid) return false;
    vr::HmdMatrix34_t current{};
    if (!GetCurrentOverlayAbsolute(current) || !FaceTransformTowardHmd(current)) {
        g_vrPlacementStatus = "面向我失敗：無法取得 HMD 或鍵盤位置";
        return false;
    }
    auto* ov = vr::VROverlay();
    if (!ov) return false;
    g_worldTransform = current;
    g_worldTransformValid = true;
    g_anchor = AnchorMode::WorldFixed;
    ov->SetOverlayTransformAbsolute(overlay, vr::TrackingUniverseStanding, &g_worldTransform);
    ov->SetOverlayWidthInMeters(overlay, g_widthMeters);
    g_vrPlacementStatus = "鍵盤已轉向並固定面向目前位置";
    MarkVrInteraction(true);
    return true;
}

std::string GetTrackedDeviceString(vr::TrackedDeviceIndex_t device, vr::ETrackedDeviceProperty prop) {
    auto* sys = vr::VRSystem();
    if (!sys) return {};
    vr::ETrackedPropertyError err = vr::TrackedProp_Success;
    const uint32_t needed = sys->GetStringTrackedDeviceProperty(device, prop, nullptr, 0, &err);
    if (needed <= 1 || (err != vr::TrackedProp_Success && err != vr::TrackedProp_BufferTooSmall)) return {};
    std::string value(needed, '\0');
    err = vr::TrackedProp_Success;
    sys->GetStringTrackedDeviceProperty(device, prop, value.data(), needed, &err);
    if (err != vr::TrackedProp_Success) return {};
    if (!value.empty() && value.back() == '\0') value.pop_back();
    return value;
}

bool LooksLikeKnucklesController(vr::TrackedDeviceIndex_t device) {
    std::string id = GetTrackedDeviceString(device, vr::Prop_ControllerType_String);
    id += " ";
    id += GetTrackedDeviceString(device, vr::Prop_ModelNumber_String);
    id += " ";
    id += GetTrackedDeviceString(device, vr::Prop_InputProfilePath_String);
    std::transform(id.begin(), id.end(), id.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return id.find("knuckles") != std::string::npos ||
           id.find("index controller") != std::string::npos ||
           id.find("valve index") != std::string::npos;
}

bool LooksLikeTouchController(vr::TrackedDeviceIndex_t device) {
    std::string id = GetTrackedDeviceString(device, vr::Prop_ControllerType_String);
    id += " ";
    id += GetTrackedDeviceString(device, vr::Prop_ModelNumber_String);
    id += " ";
    id += GetTrackedDeviceString(device, vr::Prop_InputProfilePath_String);
    std::transform(id.begin(), id.end(), id.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return id.find("oculus") != std::string::npos ||
           id.find("quest") != std::string::npos ||
           id.find("touch") != std::string::npos ||
           id.find("rift") != std::string::npos;
}

int FindGripAnalogAxis(vr::TrackedDeviceIndex_t device) {
    if (device >= vr::k_unMaxTrackedDeviceCount) return -1;
    int& cached = g_gripAnalogAxis[device];
    if (cached >= -1) return cached;

    auto* sys = vr::VRSystem();
    if (!sys) return -1;

    // Standard legacy Touch mapping advertises index trigger + hand squeeze as
    // two Trigger-type axes. Prefer the second Trigger-type axis.
    int triggerCount = 0;
    for (int axis = 0; axis < vr::k_unControllerStateAxisCount; ++axis) {
        const auto prop = static_cast<vr::ETrackedDeviceProperty>(
            static_cast<int>(vr::Prop_Axis0Type_Int32) + axis);
        const int32_t axisType = sys->GetInt32TrackedDeviceProperty(device, prop);
        if (axisType != vr::k_eControllerAxis_Trigger) continue;
        ++triggerCount;
        if (triggerCount >= 2) {
            cached = axis;
            return cached;
        }
    }

    // Some Steam Link / compatibility drivers expose the classic Oculus Touch
    // layout but omit the AxisNType metadata. Valve's legacy Oculus layout uses
    // axis 2 for squeeze, so keep that as a controller-identity fallback.
    if (LooksLikeTouchController(device)) {
        cached = 2;
        return cached;
    }

    // Leave as unknown rather than permanently unsupported so compatible legacy scalar axes can still be discovered
    // an untyped scalar axis while the user squeezes Grip.
    return -1;
}

bool ReadControllerGripDown(vr::TrackedDeviceIndex_t device, bool& down) {
    down = false;
    auto* sys = vr::VRSystem();
    if (!sys || device >= vr::k_unMaxTrackedDeviceCount ||
        sys->GetTrackedDeviceClass(device) != vr::TrackedDeviceClass_Controller) return false;

    vr::VRControllerState_t state{};
    const bool haveState = sys->GetControllerState(device, &state, sizeof(state));
    bool digitalDown = false;
    bool analogDown = g_gripAnalogDown[device];
    if (haveState) {
        const uint64_t gripMask = vr::ButtonMaskFromId(vr::k_EButton_Grip);
        digitalDown = (state.ulButtonPressed & gripMask) != 0;

        int axis = FindGripAnalogAxis(device);
        auto scalarDown = [&](int a) {
            if (a < 0 || a >= vr::k_unControllerStateAxisCount) return false;
            const float value = state.rAxis[a].x;
            const float pressThreshold = g_gripAnalogDown[device] ? 0.35f : 0.60f;
            return value >= pressThreshold;
        };

        if (axis >= 0) {
            analogDown = scalarDown(axis);
        } else {
            // Last-resort legacy discovery. Only consider axes 2..4 whose type
            // is Trigger or None and whose Y component behaves like a scalar.
            // Joystick/trackpad axes are explicitly excluded, so moving a stick
            // cannot accidentally grab the keyboard.
            for (int candidate = 2; candidate < vr::k_unControllerStateAxisCount; ++candidate) {
                const auto prop = static_cast<vr::ETrackedDeviceProperty>(
                    static_cast<int>(vr::Prop_Axis0Type_Int32) + candidate);
                const int32_t axisType = sys->GetInt32TrackedDeviceProperty(device, prop);
                if (axisType == vr::k_eControllerAxis_Joystick ||
                    axisType == vr::k_eControllerAxis_TrackPad) continue;
                if (std::fabs(state.rAxis[candidate].y) > 0.15f) continue;
                if (state.rAxis[candidate].x >= 0.60f) {
                    g_gripAnalogAxis[device] = candidate;
                    axis = candidate;
                    analogDown = true;
                    break;
                }
            }
            if (axis < 0) analogDown = false;
        }
        g_gripAnalogDown[device] = analogDown;
    }

    bool knucklesTouchDown = false;
    bool knucklesTouchEventUsable = false;
    if (LooksLikeKnucklesController(device)) {
        const uint64_t gripMask = vr::ButtonMaskFromId(vr::k_EButton_Grip);
        bool rawTouch = haveState && (state.ulButtonTouched & gripMask) != 0;

        // TEST13: SteamVR Dashboard can take legacy controller input focus.
        // Preserve the validated capacitive Knuckles gesture by OR-ing the
        // event-backed touch state while Dashboard is visible. Outside the
        // Dashboard the direct controller state remains authoritative.
        bool dashboardVisible = false;
        if (auto* ov = vr::VROverlay()) dashboardVisible = ov->IsDashboardVisible();
        knucklesTouchEventUsable = dashboardVisible && g_gripTouchEventKnown[device];
        if (knucklesTouchEventUsable)
            rawTouch = rawTouch || g_gripTouchEventDown[device];

        const bool haveTouchSource = haveState || knucklesTouchEventUsable;
        if (haveTouchSource) {
            const auto now = std::chrono::steady_clock::now();

            if (!g_knucklesGripInitialized[device]) {
                g_knucklesGripInitialized[device] = true;
                g_knucklesGripRawTouch[device] = rawTouch;
                g_knucklesGripStableTouch[device] = rawTouch;
                g_knucklesGripRawChanged[device] = now;
                // Never start grabbed when the application launches while the
                // controller is already held. Observe a genuine release first.
                g_knucklesGripArmed[device] = !rawTouch;
                g_knucklesGripActive[device] = false;
            } else {
                if (rawTouch != g_knucklesGripRawTouch[device]) {
                    g_knucklesGripRawTouch[device] = rawTouch;
                    g_knucklesGripRawChanged[device] = now;
                }

                // Capacitive Grip on Knuckles can chatter while fingers roll on
                // the handle. Require a stable contact/release before changing the
                // logical grab state.
                const auto stableFor = now - g_knucklesGripRawChanged[device];
                const auto required = rawTouch ? std::chrono::milliseconds(70)
                                               : std::chrono::milliseconds(100);
                if (stableFor >= required && rawTouch != g_knucklesGripStableTouch[device]) {
                    g_knucklesGripStableTouch[device] = rawTouch;
                    if (!rawTouch) {
                        g_knucklesGripActive[device] = false;
                        g_knucklesGripArmed[device] = true;
                    } else if (g_knucklesGripArmed[device]) {
                        g_knucklesGripActive[device] = true;
                        g_knucklesGripArmed[device] = false;
                    }
                }
            }
            knucklesTouchDown = g_knucklesGripActive[device];
        }
    }

    const bool eventDown = g_gripEventKnown[device] && g_gripEventDown[device];
    down = digitalDown || analogDown || eventDown || knucklesTouchDown;
    return haveState || g_gripEventKnown[device] || knucklesTouchEventUsable;
}

bool ControllerRayIntersectsOverlay(vr::VROverlayHandle_t overlay,
                                    vr::TrackedDeviceIndex_t device) {
    auto* ov = vr::VROverlay();
    if (!ov || overlay == vr::k_ulOverlayHandleInvalid) return false;
    vr::HmdMatrix34_t pose{};
    if (!GetAbsoluteDevicePose(device, pose)) return false;

    vr::VROverlayIntersectionParams_t params{};
    params.vSource.v[0] = pose.m[0][3];
    params.vSource.v[1] = pose.m[1][3];
    params.vSource.v[2] = pose.m[2][3];
    params.vDirection.v[0] = -pose.m[0][2];
    params.vDirection.v[1] = -pose.m[1][2];
    params.vDirection.v[2] = -pose.m[2][2];
    params.eOrigin = vr::TrackingUniverseStanding;
    vr::VROverlayIntersectionResults_t hit{};
    return ov->ComputeOverlayIntersection(overlay, &params, &hit);
}

void DriveLayoutAndGrabInteraction(vr::VROverlayHandle_t overlay) {
    if (g_previewMode || overlay == vr::k_ulOverlayHandleInvalid) return;
    auto* sys = vr::VRSystem();
    if (!sys) return;

    // Free Grip grab is available during normal keyboard use.  Layout mode
    // only adds thumbstick push/pull + scaling inside UpdateVrGrabTransform().
    const bool gripModeAvailable = !g_wristStandby && g_ui.layoutGripGrabEnabled;
    if (!gripModeAvailable) {
        g_layoutGripWasDown.fill(false);
        if (g_vrGrabActive && g_vrGrabInputSource == GrabInputSource::GripLayout)
            EndVrGrab(overlay);
        UpdateVrGrabTransform(overlay);
        return;
    }

    bool activeGrabDeviceSeen = false;
    for (vr::TrackedDeviceIndex_t device = 0; device < vr::k_unMaxTrackedDeviceCount; ++device) {
        if (sys->GetTrackedDeviceClass(device) != vr::TrackedDeviceClass_Controller) {
            g_layoutGripWasDown[device] = false;
            continue;
        }

        bool gripDown = false;
        if (!ReadControllerGripDown(device, gripDown)) {
            g_layoutGripWasDown[device] = false;
            continue;
        }
        const bool gripPressed = gripDown && !g_layoutGripWasDown[device];
        const bool gripReleased = !gripDown && g_layoutGripWasDown[device];
        g_layoutGripWasDown[device] = gripDown;

        if (g_vrGrabActive && g_vrGrabInputSource == GrabInputSource::GripLayout &&
            g_vrGrabDevice == device) {
            activeGrabDeviceSeen = true;
            if (gripReleased) EndVrGrab(overlay);
            continue;
        }

        // Prefer SteamVR's own overlay pointer identity: that path already
        // accounts for the controller-specific laser/tip pose.  Fall back to
        // the legacy raw-ray intersection if no native hover is available.
        const bool nativePointerHit = g_pointerVisible && g_lastPointerDevice == device;
        if (!g_vrGrabActive && gripPressed &&
            (nativePointerHit || ControllerRayIntersectsOverlay(overlay, device))) {
            BeginVrGrab(overlay, GrabInputSource::GripLayout, device);
            activeGrabDeviceSeen = g_vrGrabActive;
        }
    }

    if (g_vrGrabActive && g_vrGrabInputSource == GrabInputSource::GripLayout && !activeGrabDeviceSeen)
        EndVrGrab(overlay);
    UpdateVrGrabTransform(overlay);
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
    ov->SetOverlayFlag(overlay, vr::VROverlayFlags_NoBackside, true);
    ov->SetOverlayFlag(overlay, vr::VROverlayFlags_EnableClickStabilization, false);
    ov->SetOverlayInputMethod(overlay, vr::VROverlayInputMethod_Mouse);
    g_wristDashboardNativeInput = false;
    vr::VRTextureBounds_t bounds{0.0f, 0.0f, 1.0f, 1.0f};
    ov->SetOverlayTextureBounds(overlay, &bounds);
    vr::HmdVector2_t mouseScale{{(float)TEX_W, (float)TEX_H}};
    ov->SetOverlayMouseScale(overlay, &mouseScale);
    ov->SetOverlayWidthInMeters(overlay, g_widthMeters);
    ov->SetOverlayCurvature(overlay, g_ui.curvedOverlay ? g_ui.overlayCurvature : 0.0f);
    g_forceTextureSubmit = true;
}

void SetWristDotVisualBounds(vr::VROverlayHandle_t overlay) {
    if (g_previewMode || overlay == vr::k_ulOverlayHandleInvalid) return;
    auto* ov = vr::VROverlay();
    if (!ov) return;
    // Crop the left TEX_H x TEX_H square from the wide render texture. This
    // makes the physical OpenVR overlay square while keeping a single shared
    // render target for both the full keyboard and wrist dot.
    // The launcher is a symmetric circle, so render it from either side.
    // This avoids controller-coordinate differences hiding the overlay on some runtimes.
    ov->SetOverlayFlag(overlay, vr::VROverlayFlags_NoBackside, false);
    // Click stabilization only affects SteamVR's own native mouse/laser path,
    // which we deliberately disable below. Left off here so it does not
    // silently mask timing issues in our own ray/near-touch path.
    ov->SetOverlayFlag(overlay, vr::VROverlayFlags_EnableClickStabilization, false);
    // IMPORTANT: default wrist input is None. DriveWristOverlayInteraction()
    // owns ray/near-touch + Trigger input during normal scene use. Running the
    // native Mouse path at the same time previously caused double-click, stuck
    // long-press and cursor-jump races. UpdateWristDashboardInputMode() is the
    // only exception: while SteamVR Dashboard is visible it temporarily enables
    // Mouse and suspends the manual path, then restores None when Dashboard
    // closes. The two owners therefore remain mutually exclusive.
    ov->SetOverlayInputMethod(overlay, vr::VROverlayInputMethod_None);
    g_wristDashboardNativeInput = false;
    const float uMax = (float)TEX_H / (float)TEX_W;
    vr::VRTextureBounds_t bounds{0.0f, 0.0f, uMax, 1.0f};
    ov->SetOverlayTextureBounds(overlay, &bounds);
    vr::HmdVector2_t mouseScale{{(float)TEX_H, (float)TEX_H}};
    ov->SetOverlayMouseScale(overlay, &mouseScale);
    // The wrist launcher must stay geometrically flat; curving a tiny circular
    // hit target makes near-touch and ray coordinates harder to predict.
    ov->SetOverlayCurvature(overlay, 0.0f);
    g_forceTextureSubmit = true;
}

void UpdateWristDashboardInputMode(vr::VROverlayHandle_t overlay) {
    if (g_previewMode || !g_wristStandby || overlay == vr::k_ulOverlayHandleInvalid) return;
    auto* ov = vr::VROverlay();
    if (!ov) return;

    // When SteamVR Dashboard is open it owns controller/system input focus.
    // Temporarily let SteamVR translate its own controller pointer into mouse
    // events for this overlay. Outside Dashboard, return to the TB03 validated
    // manual ray/near-touch + Trigger path so two input sources never race.
    const bool wantNativeMouse = ov->IsDashboardVisible() && g_wristDotFacingViewer;
    if (wantNativeMouse == g_wristDashboardNativeInput) return;

    ov->SetOverlayInputMethod(overlay, wantNativeMouse
        ? vr::VROverlayInputMethod_Mouse
        : vr::VROverlayInputMethod_None);
    g_wristDashboardNativeInput = wantNativeMouse;
    g_pointerVisible = false;
    g_forceTextureSubmit = true;
}

void ApplyWristStandbyTransform(vr::VROverlayHandle_t overlay) {
    if (g_previewMode || overlay == vr::k_ulOverlayHandleInvalid) return;
    auto* ov = vr::VROverlay();
    if (!ov) return;

    vr::TrackedDeviceIndex_t device = vr::k_unTrackedDeviceIndexInvalid;
    vr::HmdMatrix34_t absolute{};
    bool hmdFallback = false;
    if (!ResolveWristAbsolute(device, absolute, hmdFallback)) return;

    g_wristTrackedDevice = device;
    g_wristUsingHmdFallback = hmdFallback;
    g_wristAbsoluteTransform = absolute;
    g_wristAbsoluteTransformValid = true;
    ov->SetOverlayTransformAbsolute(overlay, vr::TrackingUniverseStanding, &absolute);
    UpdateWristDashboardInputMode(overlay);
}

void EnterWristStandby(vr::VROverlayHandle_t overlay) {
    if (!g_wristStandby) SaveReturnPosition(overlay);
    CancelVrGrabState();
    g_wristStandby = true;
    g_wristPendingSingle = false;
    g_wristLongPressTriggered = false;
    g_wristLastClickTime = -10.0;
    g_vrPlacementStatus = g_returnWorldTransformValid
        ? "手腕圓點待機中｜返回位置已保留"
        : "手腕圓點待機中";

    // Static visual/input properties only need to be sent when entering
    // standby. The per-frame path below only updates the transform.
    if (!g_previewMode && overlay != vr::k_ulOverlayHandleInvalid) {
        if (auto* ov = vr::VROverlay()) {
            SetWristDotVisualBounds(overlay);
            ov->SetOverlayWidthInMeters(overlay, g_ui.wristDotSizeMeters);
            ov->SetOverlayAlpha(overlay, 1.0f);
        }
    }
    ApplyWristStandbyTransform(overlay);
}

void SummonKeyboard(vr::VROverlayHandle_t overlay, bool preserveExistingReturn = false) {
    if (!preserveExistingReturn) SaveReturnPosition(overlay);
    g_wristStandby = false;
    g_wristTrackedDevice = vr::k_unTrackedDeviceIndexInvalid;
    g_wristUsingHmdFallback = false;
    g_wristAbsoluteTransformValid = false;
    g_wristPendingSingle = false;
    CancelVrGrabState();
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
    g_wristTrackedDevice = vr::k_unTrackedDeviceIndexInvalid;
    g_wristUsingHmdFallback = false;
    g_wristAbsoluteTransformValid = false;
    g_wristPendingSingle = false;
    CancelVrGrabState();
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
    g_wristTrackedDevice = vr::k_unTrackedDeviceIndexInvalid;
    g_wristUsingHmdFallback = false;
    g_wristAbsoluteTransformValid = false;
    g_wristPendingSingle = false;
    CancelVrGrabState();
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

void MarkVrInteraction(bool forceTextureSubmit) {
    g_lastVrInteraction = std::chrono::steady_clock::now();
    if (forceTextureSubmit) g_forceTextureSubmit = true;
}

bool TextureUpdateDue(std::chrono::steady_clock::time_point now) {
    if (!g_ui.adaptiveTextureUpdates || g_forceTextureSubmit ||
        g_lastTextureSubmit.time_since_epoch().count() == 0) return true;

    const ImGuiIO& io = ImGui::GetIO();
    const bool recentInteraction = now - g_lastVrInteraction < std::chrono::milliseconds(450);
    const bool active = recentInteraction || g_pointerVisible || g_vrGrabActive ||
                        io.MouseDown[0] || io.MouseDown[1] ||
                        g_wristPendingSingle || g_macroRunning;
    const float activeFps = std::clamp(g_ui.activeTextureFps, 45.0f, 144.0f);
    const float idleFps = (std::min)(std::clamp(g_ui.idleTextureFps, 10.0f, 60.0f), activeFps);
    const float fps = active ? activeFps : idleFps;
    return std::chrono::duration<float>(now - g_lastTextureSubmit).count() >= 1.0f / fps;
}

void DrawInteractionPointer() {
    if (g_previewMode || g_wristStandby || !g_ui.showInteractionPointer || !g_pointerVisible) return;
    const ImVec2 position = ImGui::GetIO().MousePos;
    if (!std::isfinite(position.x) || !std::isfinite(position.y) ||
        position.x < 0.0f || position.y < 0.0f ||
        position.x > (float)TEX_W || position.y > (float)TEX_H) return;

    const float scale = std::clamp(g_ui.pointerScale, 0.50f, 2.50f);
    const float radius = 7.0f * scale;
    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    drawList->AddCircleFilled(position, radius + 3.0f * scale, IM_COL32(8, 16, 28, 205), 32);
    drawList->AddCircleFilled(position, radius, IM_COL32(106, 205, 255, 245), 32);
    drawList->AddCircle(position, radius, IM_COL32(235, 250, 255, 255), 32, 1.5f * scale);
    drawList->AddCircleFilled(position, 1.8f * scale, IM_COL32(255, 255, 255, 255), 16);
}

void UpdateAutoFade(vr::VROverlayHandle_t overlay) {
    if (g_previewMode || overlay == vr::k_ulOverlayHandleInvalid) return;
    auto* ov = vr::VROverlay();
    if (!ov) return;
    const float idle = std::chrono::duration<float>(std::chrono::steady_clock::now() - g_lastVrInteraction).count();
    // Wrist launcher visibility follows the physical calibrated hand surface.
    // Switch immediately when the wrist flips; do not smooth through a nearly
    // invisible input-catching state. Full keyboard auto-fade remains smooth.
    if (g_wristStandby) {
        g_overlayAlpha = g_wristDotFacingViewer ? 1.0f : 0.0f;
        ov->SetOverlayAlpha(overlay, g_overlayAlpha);
        return;
    }
    const float target = (g_ui.autoFadeEnabled && idle >= g_ui.autoFadeSeconds) ? g_ui.autoFadeAlpha : 1.0f;
    const float dt = std::clamp(ImGui::GetIO().DeltaTime, 0.0f, 0.10f);
    const float speed = (target > g_overlayAlpha) ? 12.0f : 5.0f;
    g_overlayAlpha += (target - g_overlayAlpha) * std::clamp(dt * speed, 0.0f, 1.0f);
    if (std::fabs(target - g_overlayAlpha) < 0.002f) g_overlayAlpha = target;
    ov->SetOverlayAlpha(overlay, std::clamp(g_overlayAlpha, 0.05f, 1.0f));
}

void DrawMoveHandle(vr::VROverlayHandle_t overlay) {
    if (g_previewMode && !g_editorMode) return;

    const char* label = g_previewMode ? "拖曳鍵盤" : (g_vrGrabActive ? "移動中..." : "抓住移動");
    const std::string stableLabel = std::string(label) + "###vr_move_handle";
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.26f, 0.38f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.36f, 0.54f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.12f, 0.48f, 0.68f, 1.0f));
    ImGui::Button(stableLabel.c_str(), ImVec2(112, 48));
    const bool moveHandleActivated = ImGui::IsItemActivated();
    TrackHoverHaptic(ImGui::GetID("###vr_move_handle"), ImGui::IsItemHovered() && !moveHandleActivated);
    ImGui::PopStyleColor(3);

    if (g_previewMode) {
        if (moveHandleActivated) {
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
    } else if (moveHandleActivated) {
        BeginVrGrab(overlay, GrabInputSource::TriggerHandle);
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
        CancelVrGrabState();
    };

    // Row 1: day-to-day controls. Placement controls are deliberately hidden
    // in Normal mode so ordinary typing cannot accidentally move the overlay.
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
    if (SelectedButton("一般模式", g_interactionMode == InteractionMode::Normal, ImVec2(96,44))) {
        if (g_vrGrabActive && g_vrGrabInputSource == GrabInputSource::GripLayout) EndVrGrab(overlay);
        g_interactionMode = InteractionMode::Normal;
        g_vrPlacementStatus = "一般模式：指向鍵盤按 Grip 可自由抓取";
    }
    ImGui::SameLine();
    if (SelectedButton("版面配置", g_interactionMode == InteractionMode::Layout, ImVec2(104,44))) {
        g_interactionMode = InteractionMode::Layout;
        g_vrPlacementStatus = "版面配置：可抓取、推拉與縮放";
    }
    ImGui::SameLine(0, 18.0f);
    if (SelectedButton("手腕圓點", g_wristStandby, ImVec2(112,44))) EnterWristStandby(overlay);
    ImGui::SameLine();
    if (ImGui::Button("召喚", ImVec2(82,44))) SummonKeyboard(overlay, g_wristStandby);
    ImGui::SameLine();
    if (!g_returnWorldTransformValid) ImGui::BeginDisabled();
    if (ImGui::Button("返回原位", ImVec2(108,44))) ReturnKeyboard(overlay);
    if (!g_returnWorldTransformValid) ImGui::EndDisabled();
    ImGui::SameLine(0, 18.0f);
    if (ImGui::Button("清除組合鍵", ImVec2(126,42))) g_mods.clear();
    if (g_previewMode) {
        ImGui::SameLine(0, 18.0f);
        if (SelectedButton("編輯預覽", g_editorMode, ImVec2(108,42))) g_editorMode = !g_editorMode;
    }
    ImGui::SameLine(0, 18.0f);
    if (ImGui::Button("關閉", ImVec2(74,42))) g_running = false;

    if (g_interactionMode == InteractionMode::Layout) {
        // Row 2 exists only in Layout mode.
        if (SelectedButton("世界固定", !g_wristStandby && g_anchor == AnchorMode::WorldFixed, ImVec2(112,42))) {
            cancelWrist(); g_anchor = AnchorMode::WorldFixed; apply(true);
        }
        ImGui::SameLine();
        if (SelectedButton("固定視野", !g_wristStandby && g_anchor == AnchorMode::HeadLocked, ImVec2(108,42))) {
            cancelWrist(); g_anchor = AnchorMode::HeadLocked; apply();
        }
        ImGui::SameLine();
        if (SelectedButton("左手", !g_wristStandby && g_anchor == AnchorMode::LeftHand, ImVec2(78,42))) {
            cancelWrist(); g_anchor = AnchorMode::LeftHand; apply();
        }
        ImGui::SameLine();
        if (SelectedButton("右手", !g_wristStandby && g_anchor == AnchorMode::RightHand, ImVec2(78,42))) {
            cancelWrist(); g_anchor = AnchorMode::RightHand; apply();
        }
        ImGui::SameLine(0, 18.0f);
        if (!g_previewMode || g_editorMode) DrawMoveHandle(overlay);
        if (!g_previewMode || g_editorMode) ImGui::SameLine();
        if (ImGui::Button("縮小", ImVec2(78,42))) {
            g_widthMeters = (std::max)(0.80f, g_widthMeters - 0.10f); apply();
            if (g_autoSave) SaveSettings();
        }
        ImGui::SameLine();
        if (ImGui::Button("放大", ImVec2(78,42))) {
            g_widthMeters = (std::min)(2.50f, g_widthMeters + 0.10f); apply();
            if (g_autoSave) SaveSettings();
        }
        ImGui::SameLine();
        if (ImGui::Button("靠近", ImVec2(78,42))) {
            g_distance = (std::max)(0.45f, g_distance - 0.08f); apply();
            if (g_autoSave) SaveSettings();
        }
        ImGui::SameLine();
        if (ImGui::Button("遠離", ImVec2(78,42))) {
            g_distance = (std::min)(1.50f, g_distance + 0.08f); apply();
            if (g_autoSave) SaveSettings();
        }
        ImGui::SameLine(0, 18.0f);
        if (ImGui::Button("面向我", ImVec2(90,42))) FaceOverlayTowardHmd(overlay);
        ImGui::SameLine(0, 18.0f);
        ImGui::TextDisabled("%s", g_vrPlacementStatus.c_str());
    }
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

    ImGui::Text("VR Full Keyboard　目前版本：%s · %s", VRFK_DISPLAY_VERSION, VRFK_TEST_BUILD_LABEL);
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
    ImGui::Text("編輯預覽 %s · %s", VRFK_DISPLAY_VERSION, VRFK_TEST_BUILD_LABEL);
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
            changed |= ImGui::Checkbox("指到新按鍵時輕震", &g_ui.hoverHapticsEnabled);
            changed |= ImGui::SliderFloat("懸停震動比例", &g_ui.hoverHapticScale, 0.05f, 0.50f, "%.2f");
            changed |= ImGui::Checkbox("顯示 VR 互動游標", &g_ui.showInteractionPointer);
            changed |= ImGui::SliderFloat("游標大小", &g_ui.pointerScale, 0.50f, 2.50f, "%.2f×");

            ImGui::SeparatorText("長按連發");
            changed |= ImGui::Checkbox("啟用長按連發", &g_ui.repeatEnabled);
            changed |= ImGui::SliderFloat("第一次等待", &g_ui.repeatDelay, 0.20f, 0.80f, "%.2f 秒");
            changed |= ImGui::SliderFloat("連發間隔", &g_ui.repeatRate, 0.035f, 0.20f, "%.3f 秒");
            WrappedText("Backspace、Delete 與方向鍵固定單次觸發，避免 VR 短按被誤判成連發；PageUp / PageDown 仍支援長按。", true);

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
            changed |= ImGui::Checkbox("啟動 VR 時先顯示手腕圓點", &g_ui.startInWristStandby);
            changed |= ImGui::Checkbox("待機使用左手腕", &g_ui.wristUseLeftHand);
            changed |= ImGui::SliderFloat("圓點大小", &g_ui.wristDotSizeMeters, 0.025f, 0.080f, "%.3f m");
            changed |= ImGui::SliderFloat("圓點透明度", &g_ui.wristDotAlpha, 0.30f, 1.00f, "%.2f");
            changed |= ImGui::SliderFloat("圓形命中範圍", &g_ui.wristHitScale, 0.45f, 1.00f, "%.2f×");
            ImGui::TextDisabled("命中範圍與可見圓點分離；圓形外的透明角落不會觸發。 ");
            ImGui::SeparatorText("手腕圓點位置校準");
            WrappedText("以目前控制器追蹤原點為中心微調；正負方向依控制器 Runtime 而異。可一次只調一個軸，VR 中確認位置後再繼續。");
            changed |= ImGui::SliderFloat("位置 X", &g_ui.wristDotOffsetX, -0.15f, 0.15f, "%+.3f m");
            changed |= ImGui::SliderFloat("位置 Y", &g_ui.wristDotOffsetY, -0.15f, 0.15f, "%+.3f m");
            changed |= ImGui::SliderFloat("位置 Z", &g_ui.wristDotOffsetZ, -0.15f, 0.15f, "%+.3f m");
            if (EditorCenteredButton("重設圓點大小與位置", ImVec2(-1.0f, 36.0f))) {
                g_ui.wristDotSizeMeters = 0.045f;
                g_ui.wristDotOffsetX = 0.0f;
                g_ui.wristDotOffsetY = 0.0f;
                g_ui.wristDotOffsetZ = 0.0f;
                g_ui.wristHitScale = 0.85f;
                changed = true;
            }
            changed |= ImGui::SliderFloat("雙擊間隔", &g_ui.wristDoubleClickSeconds, 0.18f, 0.55f, "%.2f s");
            changed |= ImGui::SliderFloat("長按時間", &g_ui.wristLongPressSeconds, 0.40f, 1.50f, "%.2f s");
            ImGui::TextDisabled("單擊：展開｜雙擊：召喚｜長按：返回原位");
            ImGui::Text("狀態：%s", g_vrPlacementStatus.c_str());
            ImGui::TextDisabled("返回位置只保存於本次執行，不寫入 INI。");

            ImGui::SeparatorText("VR 初始尺寸");
            changed |= ImGui::SliderFloat("鍵盤實體寬度", &g_widthMeters, 0.80f, 1.60f, "%.2f m");
            changed |= ImGui::SliderFloat("固定視野距離", &g_distance, 0.55f, 1.30f, "%.2f m");
            ImGui::TextDisabled("新版預設：1.08 m / 0.92 m；調整後會保存到 INI。");

            if (g_previewMode) {
                ImGui::SeparatorText("桌面模擬");
                if (EditorCenteredSelectedButton("模擬手腕圓點", g_wristStandby, ImVec2(-1.0f, 38.0f))) EnterWristStandby(vr::k_ulOverlayHandleInvalid);
                if (EditorCenteredButton("模擬召喚到眼前", ImVec2(-1.0f, 36.0f))) SummonKeyboard(vr::k_ulOverlayHandleInvalid, g_wristStandby);
                if (!g_returnWorldTransformValid) ImGui::BeginDisabled();
                if (EditorCenteredButton("模擬返回原位", ImVec2(-1.0f, 36.0f))) ReturnKeyboard(vr::k_ulOverlayHandleInvalid);
                if (!g_returnWorldTransformValid) ImGui::EndDisabled();
            }

            ImGui::SeparatorText("VR Grip 抓取");
            WrappedText("一般模式即可指向鍵盤後按住 Grip 自由抓取；版面配置模式下抓取時額外支援搖桿推拉與縮放。此功能不使用 SteamVR Action Manifest。", true);
            changed |= ImGui::Checkbox("允許 Grip 指向抓取", &g_ui.layoutGripGrabEnabled);
            changed |= ImGui::Checkbox("放開後自動面向我", &g_ui.autoFaceOnRelease);
            changed |= ImGui::SliderFloat("位置阻尼", &g_ui.grabPositionDamping, 0.0f, 0.95f, "%.2f");
            changed |= ImGui::SliderFloat("旋轉阻尼", &g_ui.grabRotationDamping, 0.0f, 0.95f, "%.2f");
            changed |= ImGui::SliderFloat("推拉速度", &g_ui.pushPullSpeed, 0.05f, 1.00f, "%.2f m/s");
            changed |= ImGui::SliderFloat("縮放速度", &g_ui.scaleSpeed, 0.05f, 1.00f, "%.2f m/s");
            ImGui::TextDisabled("阻尼 0＝TB02 原始一對一低延遲抓取。 ");
            ImGui::TextDisabled("Valve Index / Knuckles：張開抓握手指後再握回即可抓取；再次張開即放開。 ");

            ImGui::SeparatorText("曲面顯示");
            changed |= ImGui::Checkbox("完整鍵盤使用曲面", &g_ui.curvedOverlay);
            changed |= ImGui::SliderFloat("曲率", &g_ui.overlayCurvature, 0.0f, 0.45f, "%.2f");
            ImGui::TextDisabled("只套用到完整鍵盤；手腕圓點固定保持平面。 ");

            ImGui::SeparatorText("材質更新率");
            changed |= ImGui::Checkbox("啟用自適應材質更新", &g_ui.adaptiveTextureUpdates);
            changed |= ImGui::SliderFloat("互動更新率", &g_ui.activeTextureFps, 45.0f, 144.0f, "%.0f FPS");
            changed |= ImGui::SliderFloat("閒置更新率", &g_ui.idleTextureFps, 10.0f, 60.0f, "%.0f FPS");
            WrappedText("只限制 D3D 繪製與 SetOverlayTexture；姿勢、事件與輸入迴圈仍不 Sleep、不 WaitFrameSync。預設關閉以保留 TB02 基準。", true);

            ImGui::SeparatorText("固定方式");
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
    ImGuiWindowFlags wristChildFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (!g_previewMode) wristChildFlags |= ImGuiWindowFlags_NoBackground;
    ImGui::BeginChild("wrist_dot_panel", ImVec2(logicalW, logicalH), ImGuiChildFlags_None, wristChildFlags);

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();

    // Keep the visible launcher small, but give it a much larger invisible
    // hit target. The physical OpenVR overlay is only a few centimetres wide,
    // so this does not create a large VR obstruction; it simply makes the
    // transparent area around the dot forgiving to laser/touch input.
    const float visualDiameter = g_previewMode
        ? std::clamp((std::min)(avail.x, avail.y) * 0.18f, 54.0f, 120.0f)
        : 260.0f;
    const float hitDiameter = (std::min)(avail.x, avail.y) *
                              std::clamp(g_ui.wristHitScale, 0.45f, 1.00f);

    const ImVec2 hitTopLeft(
        origin.x + (avail.x - hitDiameter) * 0.5f,
        origin.y + (avail.y - hitDiameter) * 0.5f);
    ImGui::SetCursorScreenPos(hitTopLeft);

    const bool releasedClick = ImGui::InvisibleButton("##wrist_dot", ImVec2(hitDiameter, hitDiameter));
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    const bool activated = ImGui::IsItemActivated();
    const bool clicked = releasedClick && !g_wristLongPressTriggered;
    TrackHoverHaptic(ImGui::GetID("##wrist_dot"), hovered && !activated);
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
    const ImVec2 center(
        origin.x + avail.x * 0.5f,
        origin.y + avail.y * 0.5f);
    const float radius = visualDiameter * 0.46f;
    const float alpha = std::clamp(g_ui.wristDotAlpha, 0.30f, 1.0f);
    const ImU32 fill = ImGui::GetColorU32(ImVec4(
        hovered ? 0.18f : 0.10f,
        hovered ? 0.48f : 0.33f,
        hovered ? 0.88f : 0.68f,
        alpha));
    const ImU32 border = ImGui::GetColorU32(ImVec4(0.46f, 0.76f, 1.0f, (std::min)(1.0f, alpha + 0.10f)));
    dl->AddCircleFilled(center, radius, fill, 64);
    dl->AddCircle(center, radius, border, 64, (std::max)(2.0f, visualDiameter * 0.018f));

    // Long-press progress ring. It only appears while holding, so the standby
    // state remains a clean single circle when idle.
    if (active && !g_wristLongPressTriggered) {
        constexpr float kPi = 3.14159265358979323846f;
        const float a0 = -kPi * 0.5f;
        const float a1 = a0 + kPi * 2.0f * holdProgress;
        dl->PathArcTo(center, radius * 0.82f, a0, a1, 48);
        dl->PathStroke(ImGui::GetColorU32(ImVec4(1.0f, 0.86f, 0.34f, 1.0f)), 0, (std::max)(4.0f, visualDiameter * 0.026f));
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
    ImGuiWindowFlags mainFlags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
    // The VR wrist launcher is drawn onto a texture cleared to alpha 0.  Do
    // not let the normal ImGui window background refill that texture, or the
    // cropped wrist overlay becomes an opaque square.
    if (!g_previewMode && g_wristStandby) mainFlags |= ImGuiWindowFlags_NoBackground;
    ImGui::Begin("VRFullKeyboard", nullptr, mainFlags);

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

    DrawInteractionPointer();
    ImGui::End();
}


bool BeginWristGripGrab(vr::TrackedDeviceIndex_t grabDevice) {
    if (!g_wristStandby || g_wristUsingHmdFallback || !g_wristAbsoluteTransformValid ||
        g_wristTrackedDevice == vr::k_unTrackedDeviceIndexInvalid ||
        g_wristTrackedDevice == vr::k_unTrackedDeviceIndex_Hmd ||
        grabDevice == vr::k_unTrackedDeviceIndexInvalid || grabDevice == g_wristTrackedDevice) return false;

    vr::HmdMatrix34_t grabAbsolute{};
    if (!GetAbsoluteDevicePose(grabDevice, grabAbsolute)) return false;

    // Preserve the point under the grab hand so the dot never snaps when the
    // drag begins.  The resulting world point is converted back to wrist-local
    // XYZ each frame, keeping the launcher attached to the wrist after release.
    g_wristGripGrabRelative = Multiply34(InverseRigid34(grabAbsolute), g_wristAbsoluteTransform);
    g_wristGripGrabDevice = grabDevice;
    g_wristGripGrabActive = true;
    g_lastPointerDevice = grabDevice;
    g_vrPlacementStatus = "手腕圓點位置調整中...";
    MarkVrInteraction(true);
    PulseHaptic(0.45f);
    return true;
}

void EndWristGripGrab() {
    if (!g_wristGripGrabActive) return;
    g_wristGripGrabActive = false;
    g_wristGripGrabDevice = vr::k_unTrackedDeviceIndexInvalid;
    g_vrPlacementStatus = "手腕圓點位置已更新";
    if (g_autoSave) SaveSettings();
    PulseHaptic(0.30f);
}

void UpdateWristGripGrab(vr::VROverlayHandle_t overlay) {
    if (!g_wristGripGrabActive || !g_wristStandby ||
        g_wristGripGrabDevice == vr::k_unTrackedDeviceIndexInvalid) return;

    bool gripDown = false;
    if (!ReadControllerGripDown(g_wristGripGrabDevice, gripDown) || !gripDown) {
        EndWristGripGrab();
        return;
    }
    g_wristGripWasDown[g_wristGripGrabDevice] = gripDown;

    vr::HmdMatrix34_t grabAbsolute{};
    vr::HmdMatrix34_t wristControllerAbsolute{};
    if (!GetAbsoluteDevicePose(g_wristGripGrabDevice, grabAbsolute) ||
        !GetAbsoluteDevicePose(g_wristTrackedDevice, wristControllerAbsolute)) return;

    const vr::HmdMatrix34_t targetAbsolute = Multiply34(grabAbsolute, g_wristGripGrabRelative);
    const vr::HmdMatrix34_t invWrist = InverseRigid34(wristControllerAbsolute);
    const float wx = targetAbsolute.m[0][3];
    const float wy = targetAbsolute.m[1][3];
    const float wz = targetAbsolute.m[2][3];

    g_ui.wristDotOffsetX = std::clamp(
        invWrist.m[0][0] * wx + invWrist.m[0][1] * wy + invWrist.m[0][2] * wz + invWrist.m[0][3],
        -0.15f, 0.15f);
    g_ui.wristDotOffsetY = std::clamp(
        invWrist.m[1][0] * wx + invWrist.m[1][1] * wy + invWrist.m[1][2] * wz + invWrist.m[1][3],
        -0.15f, 0.15f);
    g_ui.wristDotOffsetZ = std::clamp(
        invWrist.m[2][0] * wx + invWrist.m[2][1] * wy + invWrist.m[2][2] * wz + invWrist.m[2][3],
        -0.15f, 0.15f);

    ApplyWristStandbyTransform(overlay);
    MarkVrInteraction(true);
}

void DriveWristOverlayInteraction(vr::VROverlayHandle_t overlay) {
    if (g_previewMode || !g_wristStandby || overlay == vr::k_ulOverlayHandleInvalid) return;
    auto* sys = vr::VRSystem();
    auto* ov = vr::VROverlay();
    if (!sys || !ov) return;

    // Outside SteamVR Dashboard this function is the sole input source for the
    // wrist dot (input method None). While Dashboard is visible we temporarily
    // switch to SteamVR native Mouse routing and consume those overlay events
    // in ProcessOverlayEvents(). Manual mode has two compatible paths:
    //   1) near-touch -> bring the opposite controller close to the wrist dot
    //      and press Trigger. This path does not depend on controller ray axes.
    //   2) controller ray -> ComputeOverlayIntersection
    // (A third path relying on SteamVR's own native overlay-mouse system used
    // to run in parallel here; it was removed because it independently wrote
    // the same ImGui mouse state every frame and raced with the two paths
    // below, causing spurious double-clicks and unreliable long-press.)
    static std::array<bool, vr::k_unMaxTrackedDeviceCount> triggerWasDown{};
    static std::array<bool, vr::k_unMaxTrackedDeviceCount> triggerDeliveredDown{};
    static std::array<bool, vr::k_unMaxTrackedDeviceCount> triggerNeedsRelease{};
    static std::array<int, vr::k_unMaxTrackedDeviceCount> triggerAxisIndex = [] {
        std::array<int, vr::k_unMaxTrackedDeviceCount> values{};
        values.fill(-2); // -2 unknown, -1 no advertised trigger axis
        return values;
    }();

    ImGuiIO& io = ImGui::GetIO();
    g_pointerVisible = false;

    if (g_wristGripGrabActive) {
        io.AddMouseButtonEvent(0, false);
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
        UpdateWristGripGrab(overlay);
        return;
    }

    std::array<vr::TrackedDeviceIndex_t, vr::k_unMaxTrackedDeviceCount> controllers{};
    size_t count = 0;
    for (vr::TrackedDeviceIndex_t i = 0; i < vr::k_unMaxTrackedDeviceCount; ++i) {
        if (sys->GetTrackedDeviceClass(i) != vr::TrackedDeviceClass_Controller) continue;
        vr::HmdMatrix34_t pose{};
        if (!GetAbsoluteDevicePose(i, pose)) continue;
        controllers[count++] = i;
    }

    auto readTrigger = [&](vr::TrackedDeviceIndex_t d, bool& down) {
        vr::VRControllerState_t state{};
        if (!sys->GetControllerState(d, &state, sizeof(state))) return false;

        const uint64_t triggerMask = vr::ButtonMaskFromId(vr::k_EButton_SteamVR_Trigger);
        const bool digitalPressed = (state.ulButtonPressed & triggerMask) != 0;

        int& axisIndex = triggerAxisIndex[d];
        if (axisIndex == -2) {
            axisIndex = -1;
            for (int axis = 0; axis < vr::k_unControllerStateAxisCount; ++axis) {
                const auto prop = static_cast<vr::ETrackedDeviceProperty>(
                    static_cast<int>(vr::Prop_Axis0Type_Int32) + axis);
                const int32_t axisType = sys->GetInt32TrackedDeviceProperty(d, prop);
                if (axisType == vr::k_eControllerAxis_Trigger) {
                    axisIndex = axis;
                    break;
                }
            }
        }

        // Some OpenXR->SteamVR runtimes expose Trigger only as an analog axis,
        // not as k_EButton_SteamVR_Trigger in ulButtonPressed. Read both.
        float analog = 0.0f;
        if (axisIndex >= 0 && axisIndex < vr::k_unControllerStateAxisCount) {
            analog = state.rAxis[axisIndex].x;
        } else if (vr::k_unControllerStateAxisCount > 1) {
            // Common legacy fallback used by Quest/Pico-style mappings.
            analog = state.rAxis[1].x;
        }

        // Small hysteresis prevents noisy analog values from creating rapid
        // down/up transitions around the threshold.
        const float threshold = triggerWasDown[d] ? 0.35f : 0.55f;
        down = digitalPressed || analog >= threshold;
        return true;
    };

    // SteamVR Dashboard temporarily owns controller/system input focus. In
    // that state SetOverlayInputMethod(Mouse) is the authoritative path. Keep
    // the manual and native sources mutually exclusive so TEST11's stable
    // single/double/long-press behavior cannot regress.
    static bool dashboardNativeWasActive = false;
    if (g_wristDashboardNativeInput) {
        if (!dashboardNativeWasActive) {
            for (vr::TrackedDeviceIndex_t d = 0; d < vr::k_unMaxTrackedDeviceCount; ++d) {
                if (triggerDeliveredDown[d]) io.AddMouseButtonEvent(0, false);
                triggerDeliveredDown[d] = false;
                bool down = false;
                if (readTrigger(d, down)) {
                    triggerNeedsRelease[d] = down;
                    triggerWasDown[d] = down;
                }
            }
            io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
        }
        dashboardNativeWasActive = true;

        // TEST13: native Dashboard Mouse remains the sole Trigger/click owner,
        // but Grip is allowed through as a completely separate grab-only path.
        // This avoids the TEST11 double-input regression while restoring wrist
        // dot relocation in SteamVR's system UI.
        if (g_wristGripGrabActive) {
            UpdateWristGripGrab(overlay);
            return;
        }

        const vr::TrackedDeviceIndex_t pointerDevice = g_lastPointerDevice;
        for (size_t i = 0; i < count; ++i) {
            const vr::TrackedDeviceIndex_t d = controllers[i];
            bool gripDown = false;
            if (!ReadControllerGripDown(d, gripDown)) {
                g_wristGripWasDown[d] = false;
                continue;
            }
            const bool gripPressed = gripDown && !g_wristGripWasDown[d];
            g_wristGripWasDown[d] = gripDown;

            // Only the controller currently owning SteamVR's native pointer may
            // start the grab, and never the wrist-attached controller itself.
            if (d == pointerDevice && g_pointerVisible && d != g_wristTrackedDevice &&
                gripPressed && BeginWristGripGrab(d)) {
                // If Trigger happened to be held when Grip starts, make sure
                // ImGui cannot carry a native Dashboard click into the drag.
                io.AddMouseButtonEvent(0, false);
                return;
            }
        }
        return;
    }

    // If Dashboard closes while its native mouse was held, explicitly release
    // ImGui once before returning to the manual Trigger path.
    if (dashboardNativeWasActive) {
        dashboardNativeWasActive = false;
        io.AddMouseButtonEvent(0, false);
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    }

    // The billboard quad itself always faces the viewer. Use the calibrated
    // physical hand-surface normal to hide *and* disable input when the back
    // of the hand turns away, preventing an invisible hit blocker.
    if (!g_wristDotFacingViewer) {
        for (vr::TrackedDeviceIndex_t d = 0; d < vr::k_unMaxTrackedDeviceCount; ++d) {
            if (triggerDeliveredDown[d]) io.AddMouseButtonEvent(0, false);
            triggerDeliveredDown[d] = false;
            bool down = false;
            if (readTrigger(d, down)) {
                triggerNeedsRelease[d] = down;
                triggerWasDown[d] = down;
            }
        }
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
        return;
    }

    auto submitTrigger = [&](vr::TrackedDeviceIndex_t d) {
        bool down = false;
        if (!readTrigger(d, down)) return;
        triggerWasDown[d] = down;
        if (!down) triggerNeedsRelease[d] = false;
        if (triggerNeedsRelease[d]) return;
        if (down != triggerDeliveredDown[d]) {
            io.AddMouseButtonEvent(0, down);
            if (down) {
                g_vrClickEventTime = std::chrono::steady_clock::now();
                g_vrClickPending = true;
                MarkVrInteraction(true);
            }
            triggerDeliveredDown[d] = down;
        }
    };

    auto releaseIfNeeded = [&](vr::TrackedDeviceIndex_t d) {
        bool down = false;
        if (!readTrigger(d, down)) return;
        if (triggerDeliveredDown[d]) io.AddMouseButtonEvent(0, false);
        triggerDeliveredDown[d] = false;
        triggerNeedsRelease[d] = down;
        triggerWasDown[d] = down;
    };

    auto beginGripIfPressed = [&](vr::TrackedDeviceIndex_t d) {
        bool gripDown = false;
        if (!ReadControllerGripDown(d, gripDown)) return false;
        const bool gripPressed = gripDown && !g_wristGripWasDown[d];
        g_wristGripWasDown[d] = gripDown;
        return gripPressed && BeginWristGripGrab(d);
    };

    auto tryNearTouch = [&](vr::TrackedDeviceIndex_t d) {
        if (!g_wristAbsoluteTransformValid || d == g_wristTrackedDevice) return false;

        vr::HmdMatrix34_t pose{};
        if (!GetAbsoluteDevicePose(d, pose)) return false;

        // Approximate the controller's physical tip slightly along its -Z
        // forward axis. Also accept the grip origin itself; taking the nearer
        // of the two works across controllers whose grip origin differs.
        const float wx = g_wristAbsoluteTransform.m[0][3];
        const float wy = g_wristAbsoluteTransform.m[1][3];
        const float wz = g_wristAbsoluteTransform.m[2][3];

        const float gx = pose.m[0][3];
        const float gy = pose.m[1][3];
        const float gz = pose.m[2][3];
        const float tx = gx - pose.m[0][2] * 0.075f;
        const float ty = gy - pose.m[1][2] * 0.075f;
        const float tz = gz - pose.m[2][2] * 0.075f;

        auto distSq = [&](float x, float y, float z) {
            const float dx = x - wx, dy = y - wy, dz = z - wz;
            return dx * dx + dy * dy + dz * dz;
        };

        const float nearestSq = (std::min)(distSq(gx, gy, gz), distSq(tx, ty, tz));
        const float touchRadius = (std::max)(0.060f, g_ui.wristDotSizeMeters * 1.35f);
        if (nearestSq > touchRadius * touchRadius) {
            releaseIfNeeded(d);
            return false;
        }

        // Put ImGui's pointer in the exact centre of the large invisible hit
        // target. The visible dot remains small and transparent.
        io.AddMousePosEvent((float)TEX_H * 0.5f, (float)TEX_H * 0.5f);
        g_lastPointerDevice = d;
        g_pointerVisible = true;
        MarkVrInteraction();
        if (beginGripIfPressed(d)) {
            releaseIfNeeded(d);
            return true;
        }
        submitTrigger(d);
        return true;
    };

    auto tryRay = [&](vr::TrackedDeviceIndex_t d) {
        vr::HmdMatrix34_t pose{};
        if (!GetAbsoluteDevicePose(d, pose)) return false;

        vr::VROverlayIntersectionParams_t params{};
        params.vSource.v[0] = pose.m[0][3];
        params.vSource.v[1] = pose.m[1][3];
        params.vSource.v[2] = pose.m[2][3];
        params.vDirection.v[0] = -pose.m[0][2];
        params.vDirection.v[1] = -pose.m[1][2];
        params.vDirection.v[2] = -pose.m[2][2];
        params.eOrigin = vr::TrackingUniverseStanding;

        vr::VROverlayIntersectionResults_t hit{};
        if (!ov->ComputeOverlayIntersection(overlay, &params, &hit)) {
            releaseIfNeeded(d);
            return false;
        }

        const float u = std::clamp(hit.vUVs.v[0], 0.0f, 1.0f);
        const float v = std::clamp(hit.vUVs.v[1], 0.0f, 1.0f);
        const float dx = u - 0.5f;
        const float dy = v - 0.5f;
        const float hitRadiusUv = std::clamp(g_ui.wristHitScale, 0.45f, 1.00f) * 0.5f;
        if (dx * dx + dy * dy > hitRadiusUv * hitRadiusUv) {
            releaseIfNeeded(d);
            return false;
        }
        io.AddMousePosEvent(u * (float)TEX_H, (1.0f - v) * (float)TEX_H);
        g_lastPointerDevice = d;
        g_pointerVisible = true;
        MarkVrInteraction();
        if (beginGripIfPressed(d)) {
            releaseIfNeeded(d);
            return true;
        }
        submitTrigger(d);
        return true;
    };

    // Prefer the opposite hand. Near-touch is checked before ray input because
    // it is independent of controller model-specific ray orientation.
    for (size_t i = 0; i < count; ++i) {
        const vr::TrackedDeviceIndex_t d = controllers[i];
        if (d == g_wristTrackedDevice) continue;
        if (tryNearTouch(d)) return;
    }
    for (size_t i = 0; i < count; ++i) {
        const vr::TrackedDeviceIndex_t d = controllers[i];
        if (d == g_wristTrackedDevice) continue;
        if (tryRay(d)) return;
    }

    // If nothing is pointing at / touching the launcher, allow any manual
    // fallback state to release cleanly so ImGui never gets a stuck mouse-down.
    for (size_t i = 0; i < count; ++i) {
        releaseIfNeeded(controllers[i]);
        bool gripDown = false;
        if (ReadControllerGripDown(controllers[i], gripDown))
            g_wristGripWasDown[controllers[i]] = gripDown;
    }
    io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
}

void ProcessOverlayEvents(vr::VROverlayHandle_t overlay) {
    ImGuiIO& io = ImGui::GetIO();
    vr::VREvent_t e{};
    while (vr::VROverlay()->PollNextOverlayEvent(overlay, &e, sizeof(e))) {
        // In normal wrist mode DriveWristOverlayInteraction() is the sole input
        // source, so stale native mouse events must be dropped. During SteamVR
        // Dashboard, however, UpdateWristDashboardInputMode() intentionally
        // switches the dot to Mouse so the Dashboard-owned pointer can click it.
        const bool isMouseEvent = e.eventType == vr::VREvent_FocusEnter ||
                                   e.eventType == vr::VREvent_FocusLeave ||
                                   e.eventType == vr::VREvent_MouseMove ||
                                   e.eventType == vr::VREvent_MouseButtonDown ||
                                   e.eventType == vr::VREvent_MouseButtonUp;
        if (g_wristStandby && !g_wristDashboardNativeInput && isMouseEvent) continue;
        if (g_wristGripGrabActive &&
            (e.eventType == vr::VREvent_MouseButtonDown ||
             e.eventType == vr::VREvent_MouseButtonUp)) continue;

        // Some runtimes route controller button/touch transitions to the
        // focused overlay while Dashboard owns system input. Mirror Grip here
        // as well as in PollLegacySystemEvents() so either delivery route can
        // sustain the validated Knuckles release-to-arm/touch-to-grab gesture.
        if ((e.eventType == vr::VREvent_ButtonPress || e.eventType == vr::VREvent_ButtonUnpress ||
             e.eventType == vr::VREvent_ButtonTouch || e.eventType == vr::VREvent_ButtonUntouch) &&
            e.trackedDeviceIndex < vr::k_unMaxTrackedDeviceCount &&
            e.data.controller.button == vr::k_EButton_Grip) {
            if (e.eventType == vr::VREvent_ButtonPress || e.eventType == vr::VREvent_ButtonUnpress) {
                g_gripEventKnown[e.trackedDeviceIndex] = true;
                g_gripEventDown[e.trackedDeviceIndex] = e.eventType == vr::VREvent_ButtonPress;
            } else {
                g_gripTouchEventKnown[e.trackedDeviceIndex] = true;
                g_gripTouchEventDown[e.trackedDeviceIndex] = e.eventType == vr::VREvent_ButtonTouch;
            }
        }

        switch (e.eventType) {
            case vr::VREvent_FocusEnter:
                g_pointerVisible = true;
                MarkVrInteraction(true);
                break;
            case vr::VREvent_FocusLeave:
                g_pointerVisible = false;
                io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
                break;
            case vr::VREvent_MouseMove:
                MarkVrInteraction();
                g_pointerVisible = true;
                if (e.trackedDeviceIndex != vr::k_unTrackedDeviceIndexInvalid)
                    g_lastPointerDevice = e.trackedDeviceIndex;
                io.AddMousePosEvent(e.data.mouse.x, (float)TEX_H - e.data.mouse.y);
                break;
            case vr::VREvent_MouseButtonDown:
                MarkVrInteraction(true);
                g_pointerVisible = true;
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
                    if (g_vrGrabActive && g_vrGrabInputSource == GrabInputSource::TriggerHandle)
                        EndVrGrab(overlay);
                }
                if (e.data.mouse.button & vr::VRMouseButton_Right) io.AddMouseButtonEvent(1, false);
                break;
            case vr::VREvent_ScrollDiscrete:
            case vr::VREvent_ScrollSmooth:
                MarkVrInteraction(true);
                io.AddMouseWheelEvent(e.data.scroll.xdelta, e.data.scroll.ydelta);
                break;
            default:
                break;
        }
    }
}

void PollLegacySystemEvents() {
    auto* sys = vr::VRSystem();
    if (!sys) return;
    vr::VREvent_t e{};
    while (sys->PollNextEvent(&e, sizeof(e))) {
        if (e.eventType == vr::VREvent_Quit) {
            g_running = false;
            continue;
        }
        if ((e.eventType == vr::VREvent_ButtonPress || e.eventType == vr::VREvent_ButtonUnpress ||
             e.eventType == vr::VREvent_ButtonTouch || e.eventType == vr::VREvent_ButtonUntouch) &&
            e.trackedDeviceIndex < vr::k_unMaxTrackedDeviceCount &&
            e.data.controller.button == vr::k_EButton_Grip) {
            if (e.eventType == vr::VREvent_ButtonPress || e.eventType == vr::VREvent_ButtonUnpress) {
                g_gripEventKnown[e.trackedDeviceIndex] = true;
                g_gripEventDown[e.trackedDeviceIndex] = e.eventType == vr::VREvent_ButtonPress;
            } else {
                g_gripTouchEventKnown[e.trackedDeviceIndex] = true;
                g_gripTouchEventDown[e.trackedDeviceIndex] = e.eventType == vr::VREvent_ButtonTouch;
            }
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
    EnsureControlExitEvent();

    while (g_running) {
        if (ControlCenterRequestedExit()) { g_running = false; break; }
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
        FinishHoverHapticsFrame();
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
    CloseControlExitEvent();
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
    if (g_ui.startInWristStandby) EnterWristStandby(overlay);
    g_lastVrInteraction = std::chrono::steady_clock::now();
    g_overlayAlpha = 1.0f;
    ov->ShowOverlay(overlay);

    vr::Texture_t vrTexture{dx.texture, vr::TextureType_DirectX, vr::ColorSpace_Auto};
    auto last = std::chrono::steady_clock::now();
    vr::EVROverlayError lastSubmitError = vr::VROverlayError_None;
    EnsureControlExitEvent();

    while (g_running && vr::VRSystem()) {
        using PerfClock = std::chrono::steady_clock;
        PerfFrameSample perf{};
        const auto perfLoopStart = PerfClock::now();

        if (ControlCenterRequestedExit()) {
            g_running = false;
            break;
        }

        // Consume legacy button events before evaluating Grip edges so a
        // press can start a grab in the same loop rather than after tracking.
        const auto systemEventsStart = PerfClock::now();
        PollLegacySystemEvents();
        const auto systemEventsEnd = PerfClock::now();
        if (!g_running) break;

        const auto trackingStart = systemEventsEnd;
        // Preserve the V3.9.9-style immediate tracking path. No pose cache and
        // no artificial frame pacing is introduced by the interaction path.
        if (g_wristStandby && !g_vrGrabActive) ApplyWristStandbyTransform(overlay);
        else if (g_anchor == AnchorMode::HeadLocked && !g_vrGrabActive) ApplyAnchor(overlay);
        DriveWristOverlayInteraction(overlay);
        DriveLayoutAndGrabInteraction(overlay);
        const auto trackingEnd = PerfClock::now();
        perf.trackingMs = std::chrono::duration<float, std::milli>(trackingEnd - trackingStart).count();

        // Keep the exact low-latency baseline call order while timing each group.
        const auto overlayEventsStart = trackingEnd;
        ProcessOverlayEvents(overlay);
        const auto overlayEventsEnd = PerfClock::now();

        const auto logicStart = overlayEventsEnd;
        UpdateClipboardHistory();
        const auto logicEnd = PerfClock::now();
        perf.logicMs = std::chrono::duration<float, std::milli>(logicEnd - logicStart).count();

        perf.eventsMs =
            std::chrono::duration<float, std::milli>(overlayEventsEnd - overlayEventsStart).count() +
            std::chrono::duration<float, std::milli>(systemEventsEnd - systemEventsStart).count();

        const auto now = std::chrono::steady_clock::now();
        io.DeltaTime = (std::max)(0.001f, std::chrono::duration<float>(now - last).count());
        last = now;
        io.DisplaySize = ImVec2((float)TEX_W, (float)TEX_H);

        const auto imguiStart = PerfClock::now();
        ImGui_ImplDX11_NewFrame();
        ImGui::NewFrame();
        ProcessMacroQueue();
        DrawUI(overlay);
        UpdateAutoFade(overlay);
        FinishHoverHapticsFrame();
        ImGui::Render();
        const auto imguiEnd = PerfClock::now();
        perf.imguiMs = std::chrono::duration<float, std::milli>(imguiEnd - imguiStart).count();

        const float clear[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        const auto d3dStart = PerfClock::now();
        const bool submitTexture = TextureUpdateDue(d3dStart);
        if (submitTexture) {
            dx.context->OMSetRenderTargets(1, &dx.rtv, nullptr);
            dx.context->ClearRenderTargetView(dx.rtv, clear);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        }
        const auto d3dEnd = PerfClock::now();
        perf.d3dMs = std::chrono::duration<float, std::milli>(d3dEnd - d3dStart).count();

        const auto submitStart = PerfClock::now();
        if (submitTexture) {
            lastSubmitError = ov->SetOverlayTexture(overlay, &vrTexture);
            g_lastTextureSubmit = std::chrono::steady_clock::now();
            g_forceTextureSubmit = false;
        }
        const auto submitEnd = PerfClock::now();
        perf.submitMs = std::chrono::duration<float, std::milli>(submitEnd - submitStart).count();

        const auto perfLoopEnd = PerfClock::now();
        perf.totalMs = std::chrono::duration<float, std::milli>(perfLoopEnd - perfLoopStart).count();
        PushPerfSample(perf);
        UpdatePerfSummary(lastSubmitError);

        // Latency baseline: intentionally no Sleep/WaitFrameSync/FPS cap here.
        std::this_thread::yield();
    }
    CloseControlExitEvent();

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
    if (editorRequested) {
        g_editorMode = true;
        g_interactionMode = InteractionMode::Layout;
    }
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
