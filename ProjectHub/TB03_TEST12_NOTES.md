# TB03 TEST12 — Wrist Visibility + Dashboard Input

Base checkpoint: `TB03_KnucklesGrip_VALIDATED`

## Scope

This test changes only the wrist launcher presentation/input arbitration needed for two reported regressions. The validated Valve Index / Knuckles Grip grab path is intentionally retained.

### 1. Hand-back visibility

- A calibrated wrist-dot offset is treated as the physical hand-surface normal.
- The dot is visible while that surface faces the HMD and hidden when it turns away.
- During wrist-dot Grip repositioning, visibility is forced on until release.
- Hidden wrist dots do not accept the custom pointer path.

### 2. SteamVR Dashboard coexistence

- Normal scene / VRChat wrist mode keeps the validated custom Trigger/ray path and `VROverlayInputMethod_None`.
- When `IVROverlay::IsDashboardVisible()` becomes true, the wrist overlay temporarily switches to `VROverlayInputMethod_Mouse` and the custom path yields ownership.
- Native overlay mouse events are accepted only during that Dashboard handoff.
- Closing Dashboard immediately restores the custom path and ignores stale native mouse events.
- No SteamVR Action Manifest or controller binding JSON is introduced.

## Static validation

`python tests/static_integration_checks.py`

Expected:
- PASS: balanced C++ delimiters
- PASS: new settings round-trip
- PASS: TB03 interaction contracts

## Required real-VR validation

1. Put the wrist dot on the back of the hand. Face the HMD: visible. Turn away: hidden. Turn back: visible.
2. Grip-reposition the dot across the facing threshold. It must stay visible until release.
3. Open SteamVR Dashboard and verify its own UI remains clickable.
4. Keep Dashboard open and click the wrist dot with Trigger. The keyboard must expand/summon.
5. Close Dashboard and click the wrist dot again in VRChat/scene mode.
6. Recheck normal-mode Knuckles `release -> arm -> touch -> grab` and placement retention.

Status: source/static validation complete; SteamVR hardware validation pending.
