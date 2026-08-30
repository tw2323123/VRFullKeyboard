# Alpha V3.9.11 · Test Build 03

本測試分支以 V3.9.9 的即時手感為基準，保留 Test Build 02 的手腕單一輸入修正，並在既有原生 C++／OpenVR 架構內加入更完整的 VR 懸浮視窗互動。正式 SemVer 已推進至 3.9.11。

## TB03 已驗證基準

Valve Index / Knuckles 已完成 SteamVR 實測：一般模式可直接抓取鍵盤、放手固定、手腕圓點可 Grip 調位、Delete / Backspace / 方向鍵不再誤連發，SteamVR Dashboard 仍可正常操作。Knuckles 抓取手勢為「先張開抓握手指，再握回開始抓取；再次張開即放開」。本基準不使用 SteamVR Action Manifest。

# VR Full Keyboard

SteamVR virtual full-size keyboard for Windows, with Traditional Chinese / Bopomofo support, Q9/Numpad mode, macros, wrist summon controls, desktop preview, editor preview, and a native Control Center.

Current version: **3.9.11**

Development test identity is read from **TEST_BUILD**. The launcher includes it in the Control Center cache key, so advancing a Test Build without changing SemVer still forces the correct native front end to rebuild.

## Test Build 03：VR 懸浮視窗互動整合

- 新增「一般模式／版面配置」分流；一般模式隱藏移動控制，降低打字時誤移風險。
- 版面配置支援指向 overlay 後按住 Grip 抓取，抓取中可用搖桿上下推拉、左右縮放。
- 位置與旋轉阻尼可分開設定；預設皆為 0，保留 TB02 的一對一低延遲抓取路徑。
- 可在放開後自動面向 HMD，也可按「面向我」立即校正方向。
- 完整鍵盤支援 OpenVR 曲率；手腕圓點固定保持平面。
- 新增 VR 互動游標、懸停輕震與大小／強度設定。
- 手腕圓點的可見圖形與邏輯圓形命中遮罩分離，透明方形角落不再觸發。
- Home、End、Insert、Delete、方向鍵、PageUp／PageDown 等改送 Windows 延伸鍵；NumPad Enter 與主 Enter 正確區分。
- 可選自適應材質更新率只限制 D3D 繪製與 `SetOverlayTexture`，不節流姿勢與輸入迴圈；預設關閉。

## V3.9.8 wrist interaction compatibility

The wrist launcher now performs its own controller-ray intersection and Trigger handling with OpenVR 2.15.6 `ComputeOverlayIntersection` + `GetControllerState`, so it does not depend on the removed legacy overlay-mouse helper or scene-application focus. Wrist placement is recovered by physical left/right position if controller roles are unavailable, stays close to the grip instead of floating in front of it, and automatically faces the HMD for consistent placement across controller runtimes.

## Source / developer version

Open `啟動控制中心.cmd`.

The first launch builds only the native Control Center. It requires Visual Studio 2022 C++ tools, Windows SDK, and CMake. Git is only required when building the VR keyboard core because OpenVR and Dear ImGui are fetched for source builds.

The developer Control Center can launch previews, build the core, create the prebuilt share package, publish GitHub versions, inspect logs, and manage build outputs.

If the project folder is moved or renamed, the Control Center automatically detects a stale CMake cache that points to the old source path, clears only the generated `build` directory, and configures the project again.

## First-run VR behavior

The VR keyboard now starts in the wrist-dot standby launcher by default. The full keyboard uses a more conservative default physical width of 1.08 m at a 0.92 m view distance, and both values are persisted in `VRFullKeyboard.ini`. The wrist launcher validates live controller tracking, falls back near the HMD while controllers are not yet tracked, and automatically returns to the configured wrist when tracking becomes valid.

## Prebuilt share version

Use **建立分享版** in the developer Control Center. It creates:

- `dist/VRFullKeyboard_Windows_x64.zip`
- `dist/VRFullKeyboard_Windows_x64.sha256`

End users only extract the ZIP and launch `VRFullKeyboardControl.exe`. They do **not** need Visual Studio, CMake, Git, source code, or any build step.

## Automatic updates

The Control Center is bound to the official public repository:

`tw2323123/VRFullKeyboard`

**更新與版本** checks the latest GitHub Release, downloads the fixed Windows ZIP and SHA256 assets, verifies the package, extracts it to a staging directory, then launches `VRFullKeyboardUpdater.exe` to replace application files safely. Share builds also perform a quiet background check shortly after launch and surface an available version directly on the update card.

`VRFullKeyboard.ini` is never included in release packages and is explicitly protected by the updater, so personal settings are preserved. The updater keeps only one `backup_previous` snapshot, validates that the relaunched Control Center remains alive briefly, and rolls back automatically if the new Control Center exits immediately.

**Upgrade note:** v3.9.0 is the first release that contains the bound repository and native updater. Existing v3.8.6 users must install v3.9.0 manually once; later releases can be installed from the Control Center.

## Release automation

Developer builds include **發布 GitHub 新版本** directly in the Control Center. Enter a `MAJOR.MINOR.PATCH` version and confirm once; the Control Center safely stashes local edits, syncs `main`, restores the edits, updates `VERSION`, commits all source changes, pushes `main`, creates the matching annotated tag, and pushes it. GitHub Actions then builds the Windows package and creates the GitHub Release with ZIP + SHA256 assets.

The publisher requires Git for Windows and must be run from the real Git repository on the `main` branch. It refuses duplicate tags and stops on merge/stash conflicts instead of overwriting local source.

## Update reliability test

Developer builds include **測試更新回復**, an isolated self-test for backup/restore behavior. It does not modify the real installation.

## V3.9.10 手腕圓點觸碰與輸入仲裁

手腕圓點支援自訂控制器雷射，以及「另一隻控制器靠近圓點後按 Trigger」的近距離觸碰操作。Trigger 會同時讀取數位按鍵與類比 Axis，以相容更多 OpenXR → SteamVR 控制器。

Test Build 02 在手腕待機模式停用 SteamVR 原生 Overlay Mouse 輸入，讓 `DriveWristOverlayInteraction()` 成為圓點唯一輸入來源，並丟棄切換瞬間可能殘留的原生滑鼠事件。完整鍵盤模式仍保留 SteamVR 原生 Mouse 輸入。

## V3.9.9 手腕圓點校準

手腕圓點現在預設直接放在 SteamVR 回報的控制器 grip tracking origin，並可在「編輯預覽 → VR」調整圓點大小與 X / Y / Z 位置。背景為真正透明，只保留藍色圓點與白色中心。這些設定會寫入 `VRFullKeyboard.ini`。
