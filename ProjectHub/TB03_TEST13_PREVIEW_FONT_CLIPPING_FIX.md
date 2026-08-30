# TB03 TEST13 — Desktop Preview Font Clipping Fix

Status: ready for user validation.

## Symptom
Desktop preview / layout configuration buttons could clip Traditional Chinese glyphs vertically.

## Cause
The preview used a 30 px UI font together with fixed-height 42–44 px buttons and 8 px vertical frame padding. The nominal ImGui frame requirement could exceed the explicit button height, causing glyphs to be clipped.

## Fix
- Keep the existing system-font strategy (Segoe UI + Microsoft JhengHei), avoiding redistribution of font files.
- Reduce preview/core UI atlas size from 30/29 px to 27 px.
- Reduce vertical frame padding from 8 px to 6 px.
- Retune editor custom button visual offset from -3.5 px to -2.0 px.

No SteamVR Dashboard / Knuckles interaction behavior was changed. TEST13 remains the active validated branch.
