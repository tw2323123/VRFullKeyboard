# Changelog

## 3.9.0
- 綁定官方 GitHub Repository：`tw2323123/VRFullKeyboard`。
- 3.9.0 是第一個具備完整更新器的版本；3.8.6 → 3.9.0 需手動更新一次，之後版本可由控制中心更新。
- 控制中心「更新與版本」可直接檢查 GitHub Latest Release。
- 新增自動下載 Windows 分享版 ZIP 與 SHA256。
- 下載完成後會先驗證 SHA256，驗證失敗不修改現有程式。
- 新增獨立 `VRFullKeyboardUpdater.exe`，可在控制中心關閉後安全替換 EXE / DLL。
- 更新前自動備份被覆蓋檔案；套用失敗時嘗試還原。
- 更新流程明確保護 `VRFullKeyboard.ini`，保留個人設定。
- 分享版加入 Updater，但仍不需要 Visual Studio / CMake / Git。
- GitHub Actions 改用現代 FetchContent 寫法並加入 Tag → Release 自動封裝流程。

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
