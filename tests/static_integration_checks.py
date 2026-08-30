#!/usr/bin/env python3
"""Source-level guardrails for the TB03 overlay interaction integration."""

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "src" / "main.cpp").read_text(encoding="utf-8")
VERSION_HEADER = (ROOT / "cmake" / "VRFullKeyboardVersion.h.in").read_text(encoding="utf-8")
TEST_BUILD = (ROOT / "TEST_BUILD").read_text(encoding="utf-8").strip()
LAUNCHER = (ROOT / "啟動控制中心.cmd").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def strip_comments_and_literals(text: str) -> str:
    result = []
    i = 0
    state = "code"
    while i < len(text):
        ch = text[i]
        nxt = text[i + 1] if i + 1 < len(text) else ""
        if state == "code":
            if ch == "/" and nxt == "/":
                state = "line_comment"
                result.extend("  ")
                i += 2
                continue
            if ch == "/" and nxt == "*":
                state = "block_comment"
                result.extend("  ")
                i += 2
                continue
            if ch == '"':
                state = "string"
                result.append(" ")
                i += 1
                continue
            if ch == "'":
                state = "char"
                result.append(" ")
                i += 1
                continue
            result.append(ch)
        elif state == "line_comment":
            if ch == "\n":
                state = "code"
                result.append("\n")
            else:
                result.append(" ")
        elif state == "block_comment":
            if ch == "*" and nxt == "/":
                state = "code"
                result.extend("  ")
                i += 2
                continue
            result.append("\n" if ch == "\n" else " ")
        else:
            if ch == "\\":
                result.extend("  ")
                i += 2
                continue
            if (state == "string" and ch == '"') or (state == "char" and ch == "'"):
                state = "code"
            result.append("\n" if ch == "\n" else " ")
        i += 1
    require(state in {"code", "line_comment"}, f"unterminated C++ token state: {state}")
    return "".join(result)


def check_balanced_delimiters() -> None:
    clean = strip_comments_and_literals(SOURCE)
    pairs = {"}": "{", ")": "(", "]": "["}
    stack = []
    for offset, ch in enumerate(clean):
        if ch in "{([":
            stack.append((ch, offset))
        elif ch in "})]":
            require(stack and stack[-1][0] == pairs[ch], f"unbalanced delimiter near byte {offset}")
            stack.pop()
    if stack:
        raise AssertionError(f"unclosed delimiter {stack[-1][0]} near byte {stack[-1][1]}")


def check_settings_round_trip() -> None:
    setting_keys = [
        "hoverHapticsEnabled", "hoverHapticScale", "showInteractionPointer", "pointerScale",
        "wristHitScale", "layoutGripGrabEnabled", "autoFaceOnRelease",
        "grabPositionDamping", "grabRotationDamping", "pushPullSpeed", "scaleSpeed",
        "curvedOverlay", "overlayCurvature", "adaptiveTextureUpdates",
        "activeTextureFps", "idleTextureFps",
    ]
    for key in setting_keys:
        require(f'out << "{key}="' in SOURCE, f"missing SaveSettings entry: {key}")
        require(f'key == "{key}"' in SOURCE, f"missing LoadSettings entry: {key}")


def check_contracts() -> None:
    require(TEST_BUILD == "03", "wrong TEST_BUILD identity")
    require('VRFK_TEST_BUILD_LABEL "Test Build @VRFK_TEST_BUILD@"' in VERSION_HEADER,
            "CMake test-build label is not sourced from TEST_BUILD")
    require("PROJECT_BUILD_ID=%PROJECT_VERSION%-TB%PROJECT_TEST_BUILD%" in LAUNCHER,
            "Control Center cache key does not include Test Build")
    require("bool adaptiveTextureUpdates = false;" in SOURCE, "texture scheduler must default off")
    require("float grabPositionDamping = 0.0f;" in SOURCE, "position damping must default to zero")
    require("float grabRotationDamping = 0.0f;" in SOURCE, "rotation damping must default to zero")

    require("VROverlayInputMethod_None" in SOURCE, "wrist input method is not None")
    require("VROverlayInputMethod_Mouse" in SOURCE, "expanded overlay Mouse input missing")
    require("dx * dx + dy * dy > hitRadiusUv * hitRadiusUv" in SOURCE, "circular wrist ray mask missing")
    require("triggerDeliveredDown" in SOURCE, "delivered button ownership state missing")
    require("g_wristDotFacingViewer" in SOURCE, "wrist physical-facing visibility state missing")
    require("offsetLen > 0.012f" in SOURCE, "wrist calibrated surface-normal gate missing")
    require("IsDashboardVisible()" in SOURCE, "SteamVR Dashboard visibility handoff missing")
    require("g_wristDashboardNativeInput" in SOURCE, "Dashboard/native wrist input arbitration missing")
    require("g_wristStandby && !g_wristDashboardNativeInput && isMouseEvent" in SOURCE,
            "native wrist mouse events are not gated by Dashboard ownership")
    require("g_gripTouchEventKnown" in SOURCE and "g_gripTouchEventDown" in SOURCE,
            "Dashboard Grip touch-event fallback missing")
    require("knucklesTouchEventUsable" in SOURCE,
            "Knuckles Dashboard touch source arbitration missing")
    require("native Dashboard Mouse remains the sole Trigger/click owner" in SOURCE,
            "Dashboard Grip-only wrist bypass missing")
    require("d == pointerDevice && g_pointerVisible" in SOURCE and "BeginWristGripGrab(d)" in SOURCE,
            "Dashboard wrist Grip must be gated by SteamVR native pointer ownership")

    require("GrabInputSource::GripLayout" in SOURCE, "Grip grab ownership missing")
    require("ButtonMaskFromId(vr::k_EButton_Grip)" in SOURCE, "Grip controller input missing")
    require("ControllerRayIntersectsOverlay" in SOURCE, "point-before-grab gate missing")
    require("g_vrGrabInputSource == GrabInputSource::TriggerHandle" in SOURCE,
            "trigger-handle release ownership missing")

    require("SetOverlayCurvature(overlay, g_ui.curvedOverlay" in SOURCE, "expanded curvature missing")
    require("SetOverlayCurvature(overlay, 0.0f)" in SOURCE, "wrist flat reset missing")
    require("TextureUpdateDue" in SOURCE and SOURCE.count("if (submitTexture)") >= 2,
            "texture-only scheduling boundary missing")

    for vk in ["VK_INSERT", "VK_DELETE", "VK_HOME", "VK_END", "VK_PRIOR", "VK_NEXT",
               "VK_LEFT", "VK_UP", "VK_RIGHT", "VK_DOWN"]:
        require(re.search(rf"case\s+{vk}\s*:", SOURCE) is not None, f"extended-key mapping missing: {vk}")
    require('SendKey(VK_RETURN, true)' in SOURCE, "NumPad Enter is not explicitly extended")

    loop_start = SOURCE.index("while (g_running && vr::VRSystem())")
    loop_end = SOURCE.index("CloseControlExitEvent();", loop_start)
    vr_loop = strip_comments_and_literals(SOURCE[loop_start:loop_end])
    for forbidden in ["Sleep(", "sleep_until(", "WaitFrameSync("]:
        require(forbidden not in vr_loop, f"forbidden VR-loop pacing call found: {forbidden}")
    require("DriveWristOverlayInteraction(overlay);" in vr_loop, "wrist input left immediate loop")
    require("DriveLayoutAndGrabInteraction(overlay);" in vr_loop, "grab input left immediate loop")
    require(vr_loop.index("DriveWristOverlayInteraction(overlay);") < vr_loop.index("TextureUpdateDue"),
            "texture scheduler incorrectly precedes input path")


def main() -> int:
    checks = [
        ("balanced C++ delimiters", check_balanced_delimiters),
        ("new settings round-trip", check_settings_round_trip),
        ("TB03 interaction contracts", check_contracts),
    ]
    for label, check in checks:
        check()
        print(f"PASS: {label}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, ValueError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
