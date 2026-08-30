# VRFullKeyboard — Current Project Status

Last organized: 2026-08-26

## Current Development Mainline

**Alpha V3.9.10 Test Build 03 — Knuckles Grip VALIDATED**

正式版本號仍是 **3.9.10**；Test Build 維持 **03**。

## Validated decisions

- 主 loop 延續 V3.9.9-style immediate pose/input behavior。
- TB02 wrist mode 維持 `VROverlayInputMethod_None`，唯一輸入 owner 是 `DriveWristOverlayInteraction()`。
- 完整鍵盤維持 SteamVR native Mouse。
- Valve Index / Knuckles 不使用 SteamVR Action Manifest；Grip 以 legacy `ulButtonTouched` 的 Grip bit 做 edge/latch 抓取。
- 一般模式可直接抓取鍵盤；版面配置只額外提供推拉、縮放等配置操作。
- 手腕圓點 Grip 拖動只修改 wrist-local offset。
- Backspace、Delete、方向鍵為單次觸發；PageUp / PageDown 保留 repeat。
- 抓取阻尼預設 0，自適應材質更新預設關閉。
- 不加入 Sleep、WaitFrameSync 或 pose cache。

## Validation status

- [x] Source integration
- [x] INI save/load/clamp
- [x] Windows/MSVC Release build
- [x] SteamVR Valve Index / Knuckles hands-on pass
- [x] Initial size / distance / angle / curvature pass
- [x] Pointer hit / highlight synchronization pass
- [x] No observed one-frame-late interaction regression
- [x] Normal-mode keyboard Grip grab + release pass
- [x] Wrist-dot Grip reposition pass
- [x] Special-key single-fire pass
- [x] SteamVR Dashboard coexistence pass

## Remaining scope

- Other controller families still require device-specific Grip validation.
- Optional texture scheduler remains opt-in; no default change is authorized from this validation alone.

## Source-of-truth priority

1. `PROJECT_STATUS.md`
2. `CURRENT_TEST_BUILD.md`
3. `TB03_VALIDATION_RESULTS.md`
4. `CODEX_HANDOFF.md`
