# Workspace Rules

## Conversation source priority

固定：

1. `接續VR鍵盤專案(新)`
2. `接續VR鍵盤專案(舊)`

「新」是目前進度。
「舊」只補歷史。

衝突時永遠以「新」為準。

## Chat organization

建議對話：

- 📌 專案總控
- 🧪 Alpha V3.9.10 TB02｜手腕圓點修正實測
- 🐛 VR拖曳延遲調查
- ⚡ 效能 / CSV 分析
- ⌨️ 主鍵盤
- 🔢 小鍵盤
- 🎨 UI / Overlay
- 🔄 Updater
- 🔨 Build / GitHub
- 🚀 Release

## Source of truth

現在狀態：
`PROJECT_STATUS.md`

當前測試版：
`CURRENT_TEST_BUILD.md`

目前問題：
`KNOWN_ISSUES.md`

效能：
`PERFORMANCE_NOTES.md`

開發歷史：
`CHANGELOG_DEV.md`

Codex：
`CODEX_HANDOFF.md`

## End-of-session update

每次開發完成後至少更新：

- PROJECT_STATUS.md
- CURRENT_TEST_BUILD.md
- KNOWN_ISSUES.md
- CHANGELOG_DEV.md

有 perf test 時：
- PERFORMANCE_NOTES.md

## No stale-state rule

任何從舊聊天整理出來的資訊，
如果沒有在新對話被重新確認，
不得直接標成「目前仍存在」。
只能寫成：
- Historical
- Needs verification
- Superseded
