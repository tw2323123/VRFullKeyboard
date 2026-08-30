# Codex Handoff — Current

## Mainline

**Alpha V3.9.10 Test Build 03 — Overlay Interaction Integration**

Do not treat v4.x as the active branch. Do not reapply the TB02 wristfix: it is already part of the source.

## What TB03 changes

TB03 reimplements selected interaction patterns observed in a strong VR overlay reference while retaining this project's native architecture:

- Normal / Layout mode split
- Grip-point grab plus trigger-handle grab
- Thumbstick push/pull and scale
- Independent position / rotation damping
- Auto-face and manual Face Me
- Expanded-overlay curvature
- Pointer and hover haptics
- Circular wrist hit mask
- Windows extended-key correctness
- Optional D3D / texture-submit scheduler

No Unity code, binary, asset, brand string, identifier or configuration file from the reference is copied into the product.

## Non-negotiable latency decisions

- Position damping default: 0
- Rotation damping default: 0
- Adaptive texture updates default: off
- No Sleep in the VR loop
- No WaitFrameSync
- No pose cache
- Pose, input, event and ImGui logic remain uncapped
- TB02 direct controller-relative grab remains the default path

## Immediate next task

Build on Windows with MSVC, then execute `CURRENT_TEST_BUILD.md` in order:

1. Identity / Control Center
2. TB02 wrist regression and circular-mask edge cases
3. Layout grip vs trigger ownership
4. Thumbstick manipulation and face-to-HMD
5. Pointer, haptics and curvature
6. Extended-key verification
7. Texture scheduler off/on comparison

Record subjective feel and `perf_alpha_v3_9_10.csv`. Do not tune damping and scheduler in the same run.

## Validation status

Source integration and static package checks are complete in the current workspace. Windows compilation and SteamVR hands-on results are still missing and must remain explicitly marked pending.

