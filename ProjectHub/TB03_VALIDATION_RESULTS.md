# TB03 Validation Results

Validated on SteamVR with Valve Index / Knuckles during Alpha V3.9.10 Test Build 03 hands-on testing.

| Area | Result | Notes |
|---|---|---|
| Windows/MSVC Release build | PASS | CMake 3.28.3, VS2022/MSVC 19.40, Windows SDK 10.0.22621.0 |
| Initial keyboard size | PASS | Comfortable at summon |
| Initial distance | PASS | Comfortable |
| Initial height / angle | PASS | Natural |
| Curvature / edge readability | PASS | No edge aiming problem |
| Pointer hit | PASS | Immediate target switching |
| Key highlight synchronization | PASS | No stale previous-key highlight |
| Input vs highlighted key | PASS | Matched |
| One-frame-late symptom | PASS | Not observed in final interaction path |
| Normal-mode keyboard Grip grab | PASS | Knuckles release-to-arm / re-grip gesture |
| Grab release / position retention | PASS | Stops at new location |
| Wrist-dot Grip reposition | PASS | Wrist-local calibration drag works |
| Delete / Backspace / arrows | PASS | Single-fire behavior |
| SteamVR Dashboard coexistence | PASS | Dashboard remains normally clickable |

## Knuckles implementation note

On the tested runtime, legacy `GetControllerState()` did not expose Knuckles squeeze force as a pressed Grip button or useful analog axis. The reliable legacy signal was `ulButtonTouched` with the Grip bit. The final TB03 baseline therefore uses an armed edge/latch gesture rather than SteamVR Action Manifest.

## Removed diagnostic scaffolding

TEST8～TEST10 diagnostic panels and `grip_diagnostics_*.csv` logging were removed from the validated package. Their observations informed the final Knuckles mapping but are not part of the production interaction path.
