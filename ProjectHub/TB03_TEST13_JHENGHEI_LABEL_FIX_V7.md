# TB03 TEST13 JhengHei Label Fix V7

- Keep Microsoft JhengHei as the primary UI font.
- Fix custom editor buttons so ImGui ID suffixes (`##` / `###`) are never rendered as visible text.
- Measure and draw only the visible label range.
- Remove the manual -2px label Y offset.
- Disable horizontal pixel snapping for JhengHei to avoid uneven CJK advance spacing.
- No SteamVR / Dashboard Grip / Knuckles interaction logic changed.
