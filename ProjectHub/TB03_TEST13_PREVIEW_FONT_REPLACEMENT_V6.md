# TB03 TEST13 Preview Font Replacement V6

Status: awaiting user validation

## Change
- Replaced the default preview/UI Traditional Chinese face with Windows PMingLiU (新細明體), loaded from `C:\Windows\Fonts\mingliu.ttc`, face index 1.
- Kept Microsoft JhengHei as the selectable/automatic fallback.
- Updated the font-style selector labels accordingly.
- No TEST14 branch was created.
- No SteamVR Dashboard Grip, Knuckles, wrist-dot, pointer, or key-input behavior was changed.

## Reason
Repeated V2-V5 layout and clipping fixes showed that the remaining compact-toolbar label issue persisted with Microsoft JhengHei. V6 changes the actual font metrics instead of continuing to compensate with button sizing.

## Static validation
- balanced C++ delimiters: PASS
- settings round-trip: PASS
- TB03 interaction contracts: PASS
