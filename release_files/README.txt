VR Full Keyboard

一般使用者版（免建置）

1. 直接執行 VRFullKeyboardControl.exe。
2. 使用「啟動 VR 鍵盤」、「桌面預覽」或「編輯預覽」。
3. 「更新與版本」可直接從官方 GitHub Release 檢查並安裝新版。
4. 不需要 Visual Studio、CMake、Git，也不需要自行編譯。

自動更新會先驗證 SHA256，再由獨立更新器替換檔案。
VRFullKeyboard.ini 不會被更新包覆蓋，個人設定會保留。

分享版啟動後會安靜地背景檢查新版；若有更新，「更新與版本」會直接顯示可用版本。
更新器只保留最近一次備份，若新版控制中心啟動驗證失敗會自動還原。
