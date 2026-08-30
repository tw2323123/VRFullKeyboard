# Performance / Latency Notes

## Current baseline

**Alpha V3.9.10 Test Build 03**

TB03 preserves the TB02 default timing behavior:

- no VR-loop Sleep
- no WaitFrameSync
- no pose cache
- no input or pose cap
- grab position / rotation damping default to 0
- adaptive texture updates default to off

## Optional experiment: texture-only scheduling

When explicitly enabled, the scheduler caps only D3D rendering and `SetOverlayTexture`:

- active default: 120 FPS
- idle default: 30 FPS
- input, pose, event, ImGui logic and transform paths remain immediate
- clicks, grab transitions and overlay-mode changes can force a prompt submit

This option must be evaluated against the default-off TB02-compatible run. It is not yet accepted as the production default.

## Required comparison

### Run A — baseline

- damping: 0 / 0
- adaptive texture updates: off
- repeat the TB02 drag and wrist tests

### Run B — scheduler only

- damping: 0 / 0
- adaptive texture updates: on, 120 / 30
- repeat the exact same motion and duration

### Run C — damping experiments

- scheduler state fixed to the preferred result from A/B
- position damping only
- rotation damping only
- both damping values
- change only one value per run

Compare CSV averages/maxima, Loop/s, D3D, submit cost, CPU and subjective controller-to-overlay latency.

## Naming

Suggested returned filenames:

- `perf_Alpha_V3_9_10_TB03_baseline_<date>.csv`
- `perf_Alpha_V3_9_10_TB03_scheduler_<date>.csv`
- `perf_Alpha_V3_9_10_TB03_damping_<date>.csv`

The application still writes `perf_alpha_v3_9_10.csv`; rename copies before the next run.

