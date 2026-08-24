# VR Full Keyboard

SteamVR virtual full-size keyboard for Windows, with Traditional Chinese / Bopomofo support, Q9/Numpad mode, macros, wrist summon controls, desktop preview, editor preview, and a native Control Center.

Current version: **3.9.1**

## Source / developer version

Open `啟動控制中心.cmd`.

The first launch builds only the native Control Center. It requires Visual Studio 2022 C++ tools, Windows SDK, and CMake. Git is only required when building the VR keyboard core because OpenVR and Dear ImGui are fetched for source builds.

The developer Control Center can launch previews, build the core, create the prebuilt share package, inspect logs, and manage build outputs.

## Prebuilt share version

Use **建立分享版** in the developer Control Center. It creates:

- `dist/VRFullKeyboard_Windows_x64.zip`
- `dist/VRFullKeyboard_Windows_x64.sha256`

End users only extract the ZIP and launch `VRFullKeyboardControl.exe`. They do **not** need Visual Studio, CMake, Git, source code, or any build step.

## Automatic updates

The Control Center is bound to the official public repository:

`tw2323123/VRFullKeyboard`

**更新與版本** checks the latest GitHub Release, downloads the fixed Windows ZIP and SHA256 assets, verifies the package, extracts it to a staging directory, then launches `VRFullKeyboardUpdater.exe` to replace application files safely.

`VRFullKeyboard.ini` is never included in release packages and is explicitly protected by the updater, so personal settings are preserved.

**Upgrade note:** v3.9.0 is the first release that contains the bound repository and native updater. Existing v3.8.6 users must install v3.9.0 manually once; later releases can be installed from the Control Center.

## Release automation

GitHub Actions builds the Windows package on pushes and pull requests. Pushing a tag such as `v3.9.1` whose value matches `VERSION` also creates/updates the GitHub Release and uploads the ZIP + SHA256 assets automatically.
