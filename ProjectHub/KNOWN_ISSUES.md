# Known Issues

## P1 — Non-Knuckles Grip mappings are not yet validated

Valve Index / Knuckles has passed hands-on SteamVR testing using the legacy `ulButtonTouched` Grip bit. Quest / Touch, Vive, WMR, Pico and other controller families still need real-device validation before claiming universal Grip support.

## P1 — Do not reintroduce SteamVR Action Manifest for this TB03 baseline

Action-manifest experiments successfully exposed Knuckles squeeze, but also interfered with SteamVR Dashboard/system UI input on the tested runtime. The validated TB03 baseline deliberately stays on the legacy Dashboard-safe path.

## P1 — Optional texture scheduler remains experimental

Adaptive texture updates are opt-in. The validated low-latency baseline keeps them disabled by default.

## Version identity

Current development identity: **Alpha V3.9.10 Test Build 03 — Knuckles Grip VALIDATED**.
