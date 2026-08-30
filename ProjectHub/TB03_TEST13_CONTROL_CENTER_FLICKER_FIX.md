# TB03 TEST13 Control Center Flicker Fix

Status: source patch prepared; requires Windows/MSVC build and visual verification.

## Symptom
The developer control center visibly flashes at a regular cadence even while idle.

## Root cause
`TIMER_PERF_REFRESH` fires every 500 ms and `RefreshPerfSnapshot()` invalidated the entire top-level window on every tick. The control center contains multiple owner-drawn cards and buttons, so the full-window repaint was exposed as repeated flashing.

## Fix
- Cache the performance section repaint rectangle during layout.
- The 500 ms timer now invalidates only the performance section, without background erase.
- The Stop button is invalidated only when the core running state changes.
- Parent `WM_PAINT` is double-buffered to prevent intermediate custom-draw frames from reaching the screen.

## Scope
Control-center rendering only. No SteamVR / Knuckles / overlay interaction logic was changed. TEST13 remains the current test build.
