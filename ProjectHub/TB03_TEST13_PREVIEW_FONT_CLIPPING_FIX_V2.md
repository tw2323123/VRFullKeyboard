# TEST13 Preview Font Clipping Fix V2

Status: ready for user validation.

Changes:
- Replaced Segoe UI + merged Microsoft JhengHei atlas with a single Microsoft JhengHei font for both Latin and Traditional Chinese in the default clear mode.
- Reduced UI font size to 26 px and shifted glyphs upward by 1 px to keep Traditional Chinese glyph extents inside ImGui line boxes.
- Increased top toolbar button heights from 42/44 px to 46/48 px to add vertical safety margin.
- Added a small top inset before the desktop preview safety-status line.
- No changes to TEST13 SteamVR Dashboard Grip bypass, overlay grab, wrist-dot, pointer, or key input logic.

Validation target:
- No clipped Chinese glyphs in the desktop preview title/status line.
- No clipped text in top toolbar buttons, especially 版面配置 / 手腕圓點 / 返回原位 / 清除組合鍵 and row-2 placement controls.
