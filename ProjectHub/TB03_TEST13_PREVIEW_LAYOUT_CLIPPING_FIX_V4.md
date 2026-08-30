# TEST13 Desktop Preview Layout Clipping Fix V4

## Root cause
The remaining clipping visible in the V3 screenshot is no longer ImGui text/glyph clipping. The top-toolbar self-drawn labels are intact. The full keyboard itself exceeds the desktop preview child width, which cuts the right-most NumPad column and any text inside that overflow region.

## Fix
- Keep V3 self-drawn toolbar labels.
- Reset JhengHei GlyphOffset to 0 to avoid artificial vertical displacement.
- Add a desktop-preview-only runtime scale cap based on the actual available preview width.
- Use 2130 px as the natural complete keyboard width (keys + navigation + NumPad + spacing/padding), replacing the old underestimated 1885 px fit constant.
- Do not write this runtime cap back to `g_ui.keyboardScale`; VR size/settings are unchanged.

## Regression boundary
No SteamVR, Dashboard, Knuckles Grip, wrist dot, pointer, key dispatch, repeat, or overlay transform logic changed.
