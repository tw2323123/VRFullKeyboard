# Changelog

## 3.8.6
- 分享版控制中心精簡為 4 個啟動選項 + 建立桌面捷徑。
- 分享版隱藏開發工具、資料夾工具與作業記錄區。
- 分享版使用較小的預設視窗與最小尺寸。
- 開發版完整控制中心介面維持不變。

## 3.8.5

- Fix Control Center UI compilation on MSVC caused by mixing Win32 `LONG` values with `int` in `std::max`.
- Normalize log panel geometry to explicit integer dimensions before calling `MoveWindow`.
- Keep the V3.8.4 dark UI redesign and all V3.8.3 build/share behavior unchanged.

## 3.8.4

- Rebuilt the Control Center visual layer with a dark SteamVR-oriented interface.
- Added clear Launch / Development & Release / Tools / Activity sections.
- Added colored status badges for build state, version, package type, CMake, and Git.
- Added primary/secondary/utility button hierarchy with hover, pressed, focus, and disabled states.
- Added a collapsible dark activity log panel.
- Added Clear Log and Copy Log actions while keeping the persistent build log unchanged.
- Long-running build/package operations automatically expand the activity log.
- Added per-monitor DPI awareness and DPI-responsive layout/font scaling.
- Added Windows dark title-bar integration where supported.
- Kept the Control Center dependency-free from OpenVR and Dear ImGui.
- Kept the prebuilt share package workflow unchanged: end users do not need Visual Studio, CMake, Git, or a build step.

## 3.8.3

- Decoupled Control Center bootstrap build from OpenVR/ImGui fetching.
- Fixed bootstrap compiler warnings.
- Clarified source/developer build versus prebuilt share package behavior.
