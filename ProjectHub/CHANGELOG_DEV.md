
## TB03 TEST13 — Dashboard Grip Bypass
- Preserve TEST12 Dashboard native Mouse click arbitration.
- Add Knuckles Grip ButtonTouch/ButtonUntouch event mirror for Dashboard focus fallback.
- Allow wrist-dot Grip relocation using the current SteamVR native pointer controller while Dashboard is visible.
- Reuse the same fallback for expanded-keyboard Grip grabs.
- No Action Manifest introduced.
# Development Changelog

此檔按照「新對話為主、舊對話補背景」整理。

## Earlier history

V3.9.x 系列逐步加入：
- 控制中心
- Updater
- build / release automation
- VR Overlay / keyboard 功能

歷史細節留待需要時從「舊」對話補入。

## V3.9.9

重要定位：

**目前被選為 low-latency loop 基準。**

這代表後續版本雖然增加功能，
但在 latency / loop 行為上，V3.9.9 被視為值得回歸的版本。

## V3.9.10

加入後續功能。

目前問題不是單純把整版丟掉，
而是希望保留它的功能，同時找回 V3.9.9 的低延遲 loop。

## V4.0.1

目前保留價值：

- 效能診斷面板
- CSV 報告輸出

這些 diagnostics 被整合進目前 Test Build。

## Alpha V3.9.10 Test Build 01

組合：

- V3.9.9 low-latency loop
- V3.9.10 features
- V4.0.1 diagnostics / CSV

目的：

Regression test。

確認低延遲、功能與診斷工具能否同時成立。

## Alpha V3.9.10 Test Build 02

延續 Test Build 01，加入手腕圓點輸入仲裁修正：

- 手腕待機 Overlay Input Method：`Mouse` → `None`
- `DriveWristOverlayInteraction()` 成為唯一輸入來源
- 保留自訂控制器雷射與近距離觸碰
- 手腕待機時忽略排隊中的原生 MouseMove / MouseButtonDown / MouseButtonUp
- 完整鍵盤模式繼續使用 SteamVR 原生 Mouse

目的：消除兩套輸入同時寫入 ImGui 所造成的重複點擊、雙擊誤判、長按不穩定與游標跳動。

整合核對：原統整包與 `VRFullKeyboard_wristfix.zip` 內的 `src/main.cpp` 位元完全相同；本次主要完成版本識別與 ProjectHub 交接更新。尚待 Windows 建置及 VR 實機回歸。

## Current — Alpha V3.9.10 Test Build 03

在 TB02 基準上加入 overlay interaction layer：

- 一般／版面配置模式
- Grip 指向抓取及與 Trigger handle 分離的 release ownership
- thumbstick 推拉與縮放
- 獨立位置／旋轉阻尼，預設 0
- auto-face／Face Me
- 完整鍵盤 curvature、VR pointer、hover haptics
- circular wrist hit mask 及離開命中區的明確釋放
- Windows extended key／NumPad Enter 修正
- 只涵蓋 D3D + texture submit 的 optional active／idle scheduler，預設關閉

正式 SemVer 維持 3.9.10。來源已整合；Windows build 與 SteamVR 實測待完成。

## Superseded interpretation

先前 Project Hub 曾把 v4.1.2 當成最新主線。

此判斷已被撤銷。

目前專案主線以：

**Alpha V3.9.10 Test Build 03**

為準。
