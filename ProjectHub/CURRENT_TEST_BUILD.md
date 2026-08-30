# Current Test Build

## Name

**VRFullKeyboard Alpha V3.9.10 Test Build 03 — Dashboard Grip Bypass TEST13**

正式 SemVer 維持 **3.9.10**，Test Build 維持 **03**。本基準由 TB03 Overlay Interaction Integration 延伸，已完成 Windows/MSVC 建置與 Valve Index / Knuckles SteamVR 實測。

版本來源：`VERSION=3.9.10`、`TEST_BUILD=03`。

## Validated baseline

- V3.9.9-style immediate pose/input loop
- TB02 wrist standby single-input ownership
- 手腕待機：`VROverlayInputMethod_None`
- 完整鍵盤：SteamVR native Overlay Mouse
- 一般模式直接 Grip 自由抓取鍵盤
- Valve Index / Knuckles 使用 legacy OpenVR `ulButtonTouched` Grip bit 的 release-to-arm / touch-to-grab 手勢
- 手腕圓點可由另一手 Grip 拖動，保存 wrist-local X/Y/Z offset
- Backspace / Delete / 方向鍵單次觸發
- PageUp / PageDown 保留長按 repeat
- SteamVR Dashboard 不使用、不依賴 Action Manifest，系統 UI 可正常操作
- 預設不使用 Sleep、WaitFrameSync、pose cache 或 texture cap

## SteamVR hands-on result

- [x] Windows + MSVC Release build succeeds
- [x] 初始鍵盤尺寸合適
- [x] 初始距離舒服
- [x] 初始高度與角度自然
- [x] 曲面程度正常，左右邊緣可正常瞄準
- [x] Pointer 命中與按鍵發光瞬間同步
- [x] 實際輸入與亮起按鍵一致
- [x] 未再觀察到「晚一拍」
- [x] Knuckles 一般模式可自由抓取鍵盤
- [x] 放手後鍵盤固定在新位置
- [x] 手腕圓點可 Grip 調位
- [x] Delete / Backspace / 方向鍵不再單擊連發
- [x] SteamVR Dashboard 可正常操作

## Knuckles gesture contract

1. 張開抓握手指離開 Grip 感測區，進入 armed。
2. 再握回並穩定接觸約 70 ms，開始抓取。
3. 再次張開離開 Grip 感測區約 100 ms，結束抓取。

此路徑刻意**不使用 SteamVR Action Manifest**。先前 action-based 測試會與 SteamVR Dashboard 系統輸入衝突，已淘汰。

## TEST13 hardware validation — PASS

TEST13 已完成 Valve Index / Knuckles 實機驗證：SteamVR Dashboard 顯示期間，Grip 可正常抓取手腕圓點與完整鍵盤；Dashboard 關閉後，手腕 Trigger、一般 Grip 抓取、圓點調位、Pointer 命中、按鍵發光同步、Backspace / Delete / 方向鍵單次輸入與 VRChat 一般模式皆未觀察到 regression。

## Remaining validation boundary

Valve Index / Knuckles 已實測通過；其他控制器（Quest / Touch、Vive、WMR、Pico 等）的 Grip 映射仍需要各自實機覆蓋，不能由 Knuckles 結果直接推定。
