# VR Full Keyboard

SteamVR virtual full-size keyboard for Windows, with Traditional Chinese / Bopomofo support, Q9/Numpad mode, macros, wrist summon controls, desktop preview, editor preview, and a native Control Center.

Current version: **3.9.6**

## Source / developer version

Open `啟動控制中心.cmd`.

The first launch builds only the native Control Center. It requires Visual Studio 2022 C++ tools, Windows SDK, and CMake. Git is only required when building the VR keyboard core because OpenVR and Dear ImGui are fetched for source builds.

The developer Control Center can launch previews, build the core, create the prebuilt share package, publish GitHub versions, inspect logs, and manage build outputs.

If the project folder is moved or renamed, the Control Center automatically detects a stale CMake cache that points to the old source path, clears only the generated `build` directory, and configures the project again.

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
