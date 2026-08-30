# Build / Release State

## Local project

`C:\Users\user\Desktop\VRFullKeyboard`

## Known build outputs

`build\Release`

- `VRFullKeyboard.exe`
- `VRFullKeyboardUpdater.exe`

## Confirmed build history

可確認紀錄：
- CMake 設定可成功
- OpenVR / ImGui 可完成建置
- 主程式可產出
- Updater 可產出
- 控制中心首次建置曾無 warning

## Updater verified history

已有通過紀錄：
- backup
- restore
- cleanup newly added files
- preserve INI

## Version separation

### Current development
Alpha V3.9.10 Test Build 03

Sources of truth:
- `VERSION` → SemVer (`3.9.10`)
- `TEST_BUILD` → development build identity (`03`)
- Control Center bootstrap cache key → `<VERSION>-TB<TEST_BUILD>`

### Public-release history
目前可取得紀錄曾出現：
- v3.9.6 commit / tag pushed to GitHub
- 更早 updater log 顯示 v3.9.1 為最新版

這些不等於目前 development build。

## Recommended build metadata

每個 development build 建議顯示：

- Version
- Test Build number
- Git commit short SHA
- Build timestamp

例如：

`Alpha V3.9.10 Test Build 03 (abc1234, 2026-08-25)`
