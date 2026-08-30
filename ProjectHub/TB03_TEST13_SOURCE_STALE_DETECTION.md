# TB03 TEST13 — Source / Core EXE stale detection

Status: implementation ready for user validation.

## Changes
- Preview/VR UI default font restored to Microsoft JhengHei (`msjh.ttc`).
- Developer Control Center compares `build/Release/VRFullKeyboard.exe` timestamp against core build inputs:
  - `src/main.cpp`
  - `CMakeLists.txt`
  - `VERSION`
  - `TEST_BUILD`
  - `cmake/VRFullKeyboardVersion.h.in`
- When Source is newer, the header status shows `Core 過期`.
- Launching VR / Preview / Editor while Core is stale prompts to rebuild, and continues the requested launch automatically after a successful build.
- The Control Center bootstrap script also compares its own Source timestamps, so replacing a TEST13 source package no longer silently reuses an older `.frontend/VRFullKeyboardControl.exe` just because SemVer/Test Build are unchanged.

No SteamVR Dashboard / Knuckles / wrist-dot interaction logic was modified.
