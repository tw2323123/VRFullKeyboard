# TB03 TEST13 Preview Button Label Clipping Fix V5

## Root cause
Toolbar labels were no longer clipped vertically by ImGui Button text rendering, but several hard-coded button widths were physically narrower than the rendered Traditional-Chinese label. Example: `一般模式` used a 96 px button while the 26 px CJK font needs roughly 104 px before padding.

## Fix
- `EditorCenteredButton()` now treats requested width/height as minimums.
- Actual button width is expanded to `CalcTextSize(label).x + 24 px`.
- Height is also guaranteed to be at least text height + 16 px.
- This fixes all editor-toolbar labels generically instead of patching individual buttons.
- No VR interaction / TEST13 Dashboard Grip logic changed.
