# VRFullKeyboard Project Hub

此版本以 **「接續VR鍵盤專案(新)」** 為主要資料源建立。

資料優先序：

1. 接續VR鍵盤專案(新) — 最新進度，最高優先
2. 接續VR鍵盤專案(舊) — 補足前期背景
3. 舊資料若與新資料衝突，一律以新資料為準
4. 無法由目前可取得紀錄確認的項目，會標成「待核對」，不把舊狀態當成現況

## 目前最新開發主線

**Alpha V3.9.11 Test Build 03 — TEST13 Validated Mainline**

這個測試版：
- 以 V3.9.9 的低延遲迴圈為基礎
- 保留 V3.9.10 的功能
- 保留 V4.0.1 效能診斷面板與 CSV
- 已整合手腕圓點「雙重輸入來源」修正
- 新增安全版面配置、Grip 抓取、推拉／縮放、阻尼、曲面、游標、觸覺與按鍵映射整合
- 目前目的為回歸測試，確認新互動、手腕操作與低延遲表現是否可以同時保留

## 已確認目前工作

- 控制中心可啟動 / 關閉鍵盤
- 已整合 V4.0.1 效能診斷面板
- 已有 CSV 效能報告輸出
- 主程式與 Updater 建置鏈仍正常
- Updater 的備份 / 還原 / 新增檔案清理 / INI 保護已有通過紀錄
- `VRFullKeyboard_wristfix.zip` 的 `src/main.cpp` 已確認與統整包內容位元完全相同，不需再次覆蓋
- Test Build 03 已完成來源整合，尚待 Windows 重新建置與 VR 實機驗證

## 建議閱讀順序

1. PROJECT_STATUS.md
2. CURRENT_TEST_BUILD.md
3. KNOWN_ISSUES.md
4. PERFORMANCE_NOTES.md
5. ARCHITECTURE.md
6. BUILD_AND_RELEASE.md
7. CHANGELOG_DEV.md
8. CODEX_HANDOFF.md
9. WORKSPACE_RULES.md
