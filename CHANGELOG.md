# Changelog

## 3.9.6

- 開發版控制中心新增「發布 GitHub 新版本」。
- 發布視窗可直接輸入版本號並在送出前二次確認。
- 發布流程內建安全同步：暫存未提交修改 → pull --rebase → 還原修改 → 更新 VERSION。
- 自動 Commit、Push main、建立 annotated Tag 並 Push Tag，之後交由 GitHub Actions 建立 ZIP、SHA256 與 Release。
- 發布前檢查 main 分支、Git、.git Repository、版本格式與重複 Tag。
- Tag Push 失敗時會移除本機新建 Tag，讓同一版本可安全重試。
- 「建立分享版」說明改為明確表示會自動建置最新版再產生 ZIP / SHA256。
- 發布功能完全整合在 control_center.cpp，不需要額外 PowerShell / CMD 發布腳本。

## 3.9.5

- 修正編輯預覽中文字按鈕在部分 DPI / 微軟正黑體下視覺基線偏低的問題。
- 編輯器全寬按鈕改為自訂垂直置中文字繪製，不再依賴 ImGui 中文字型 baseline。
- 不影響 VR 鍵盤本體鍵帽，並保留 Hover、Active、Click、Border 與圓角行為。
- 保留 V3.9.3 的裁切修正、UI 間距解耦與自動儲存 I/O 優化。

## 3.9.3
- Fixed editor sidebar controls being clipped near the bottom of scrollable pages.
- Removed the obsolete "防裁切版" development label.
- Decoupled editor spacing from keyboard key-gap settings.
- Added explicit shortcut editor row sizing and cell padding for reliable text-field layout.
- Added bottom scroll breathing room for Appearance and Shortcut pages.
- Reduced unnecessary INI writes by deferring editor auto-save until the active control is released.

## 3.9.2
- 建置前會檢查 `build/CMakeCache.txt` 綁定的 Source 路徑；若專案曾搬家、改名或由舊版資料夾複製而造成路徑不一致，控制中心會自動清除舊快取並重新 Configure。
- 分享版啟動後約 1.6 秒會在背景檢查 GitHub Latest Release，不主動跳出視窗。
- 發現新版時，「更新與版本」按鈕與頂部狀態徽章會直接顯示可用版本。
- 更新下載期間在按鈕上顯示下載百分比、SHA256 驗證、解壓與準備安裝等階段。
- 更新成功重啟後會顯示「舊版本 → 新版本」完成提示。
- 更新備份改為只保留最近一份 `backup_previous`，避免長期累積。
- 啟動新版控制中心後加入短暫健康檢查；若新版立即退出，Updater 會自動 Rollback 並重新啟動舊版。
- 開發版新增「測試更新回復」工具，使用隔離暫存資料驗證備份、還原、新增檔案清理與 `VRFullKeyboard.ini` 保護。
- 更新前會清理舊的暫存下載目錄，但保留最近一次版本備份。

## 3.9.1
- 自動更新端到端驗證版本。
- 不變更 VR 鍵盤核心操作與介面配置，專門測試 3.9.0 → 3.9.1 更新流程。
- 更新完成後可直接由控制中心版本徽章確認已升級至 3.9.1。
- GitHub Actions runner 固定使用已驗證成功的 `windows-2022`。
- 保留 ZIP + SHA256 驗證、Updater 備份/替換/重啟與 `VRFullKeyboard.ini` 保護機制。

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
