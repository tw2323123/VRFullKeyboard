# Alpha V3.9.10 TB03 — Knuckles Grip VALIDATED

## SteamVR 實測通過
- Valve Index / Knuckles：一般模式可直接以抓握手勢自由抓取鍵盤。
- 放開抓握後，鍵盤固定在新的世界位置。
- 手腕圓點可由另一手以同樣抓握手勢重新校準位置，並保存 wrist-local X/Y/Z offset。
- Backspace、Delete、方向鍵改為單次觸發；PageUp / PageDown 保留長按連發。
- SteamVR Dashboard 保持原生操作，不使用 SteamVR Action Manifest、不註冊 binding JSON。
- 鍵盤初始尺寸、距離、角度、曲面、Pointer 命中、按鍵發光同步與整體低延遲體感均已通過本輪 SteamVR 實測。
- 已移除 TEST8～TEST10 的 Grip 診斷 UI、即時顯示與 CSV 記錄，只保留正式互動邏輯。

## Knuckles 抓取手勢
- 張開抓握手指離開 Grip 感測區，系統進入 armed。
- 再握回控制器並穩定接觸約 70 ms，開始抓取。
- 再次張開並離開 Grip 感測區約 100 ms，結束抓取。
- 此路徑使用 legacy OpenVR `ulButtonTouched` 的 Grip bit，不會接管 SteamVR Dashboard 系統輸入。

# Alpha V3.9.10 - Test Build 03

## VR 懸浮視窗互動整合
- 新增一般／版面配置模式，移動與固定控制只在版面配置模式顯示。
- 新增 Grip 指向抓取、搖桿推拉／縮放、放開自動面向 HMD 與「面向我」動作。
- 新增可分離的位置／旋轉阻尼；阻尼 0 保留 TB02 直接 controller-relative 路徑。
- 完整鍵盤可套用 OpenVR 曲率，手腕圓點維持平面。
- 新增可設定的 VR 游標與懸停觸覺回饋。
- 手腕圓點採獨立圓形 hit mask，離開命中區時明確釋放 ImGui 按鍵狀態。
- 修正 Windows 延伸鍵與 NumPad Enter 映射。
- 新增可選 active／idle 材質更新率；只節流 D3D 與 overlay texture submit，預設關閉且不更動輸入／姿勢 loop。
- 新增 `TEST_BUILD` 建置識別；CMake 顯示標籤與控制中心 bootstrap cache 同步使用，SemVer 不變時也不會誤開舊前端。

## Test Build 02 基準（保留）

## 手腕圓點輸入修正
- 手腕待機時將 Overlay Input Method 改為 `None`，不再同時接收 SteamVR 原生 Mouse 與自訂輸入。
- `DriveWristOverlayInteraction()` 成為手腕圓點唯一輸入來源，保留控制器雷射與近距離觸碰兩條路徑。
- 手腕待機時忽略切換前殘留的 MouseMove / MouseButtonDown / MouseButtonUp 事件。
- 完整鍵盤模式不受影響，仍使用 SteamVR 原生 Mouse 輸入。
- 目標是修正單擊／雙擊偶發重複、長按不穩定與游標跳動；仍需 Windows 建置與 VR 實機回歸確認。

## 測試基準
- 版本顯示回退並固定為 **Alpha V3.9.10**，之後測試用 Test Build 編號區分，不再快速消耗正式 SemVer。
- 延續上一個 regression fix：保留 V3.9.9 式即時主迴圈，不加入 `sleep_until`、`WaitFrameSync`、FPS cap、Pose cache 或 Dirty Render。

## 控制中心
- 新增「關閉鍵盤」按鈕：透過命名 Event 要求 VRFullKeyboard 優雅結束，方便反覆測試，不需要開工作管理員。
- 新增「即時效能監測」面板，每 0.5 秒顯示 CPU、Loop/s、Tracking、Events、Logic、ImGui、D3D、SetOverlayTexture、Total 的平均/最大耗時。
- 新增「效能報告 CSV」按鈕，可直接定位報告。

## 自動效能報告
- 核心每 0.5 秒寫入 `%LOCALAPPDATA%\VRFullKeyboard\logs\perf_alpha_v3_9_10.csv`。
- 同時寫入輕量 live snapshot，供控制中心顯示。
- 偵測器只做計時與低頻檔案寫入，不改變 VR 主迴圈節奏。
