# TB03 TEST13 — Preview Font Clipping Fix V3

## Reason
V1/V2 changed font size, font family, padding and button height, but the desktop preview top toolbar still used Dear ImGui built-in `Button()` for most labels. Built-in button text is clipped to the item rectangle, so Traditional Chinese glyph extents can still be visibly cut even when the button itself is taller.

## Fix
- Keep the unified Microsoft JhengHei font path from V2.
- Route every desktop preview top-toolbar button through the custom centered draw-list renderer.
- The custom renderer uses `InvisibleButton()` only for interaction and draws the label separately with the window draw list, so the glyph is no longer clipped by ImGui's button-label clip rectangle.
- Apply the same renderer to the keyboard move/drag handle.
- No TEST14 and no changes to TEST13 SteamVR / Knuckles interaction logic.

## Validation
Run `tests/static_integration_checks.py` and then verify the desktop preview toolbar visually, especially: 版面配置、手腕圓點、返回原位、清除組合鍵、世界固定、固定視野、左手、右手、縮小、放大、靠近、遠離、面向我.
