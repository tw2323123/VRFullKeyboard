# VR Full Keyboard

SteamVR virtual full-size keyboard for Windows, with Traditional Chinese / Bopomofo support, macro tools, wrist summon controls, Q9/Numpad interface mode, desktop preview, editor preview, and a native Control Center.

Current version: **3.8.6**

## Source / developer version

Open `啟動控制中心.cmd`.

The first launch builds only the native Control Center. It requires Visual Studio 2022 C++ tools, Windows SDK, and CMake. The Control Center itself does not require Git, OpenVR, Dear ImGui, SteamVR, or network access.

From the Control Center you can:

- Launch the VR keyboard
- Open desktop preview
- Open editor preview
- Build the latest core
- Create the prebuilt share package
- Clear build cache
- Open output/project locations
- View build logs
- Create a desktop shortcut

Git is needed only when building the VR keyboard core because OpenVR and Dear ImGui are retrieved for the source build.

## Prebuilt share version

Use **建立分享版** in the developer Control Center.

It creates `dist/VRFullKeyboard_Windows_x64.zip` plus a SHA256 file. Friends/end users only extract the ZIP and launch `VRFullKeyboardControl.exe`.

They do **not** need Visual Studio, CMake, Git, source code, or any build step.

## User settings

`VRFullKeyboard.ini` is intentionally excluded from release packages and is not meant to be overwritten by future updates.

## 3.8.6 Control Center

The Control Center uses a custom-drawn dark Win32 interface with status badges, clear action hierarchy, DPI scaling, and a collapsible activity log. It remains a lightweight native executable with no OpenVR/ImGui dependency.


### 分享版控制中心
分享版只顯示「啟動 VR 鍵盤、桌面預覽、編輯預覽、更新與版本、建立桌面捷徑」，不顯示任何開發或建置工具。
