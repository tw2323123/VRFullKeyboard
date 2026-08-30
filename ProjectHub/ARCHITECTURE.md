# Architecture

## Current lineage

```text
V3.9.9 immediate pose/input loop
+ V3.9.10 feature set
+ V4.0.1 diagnostics / CSV
+ TB02 wrist single-input arbitration
+ TB03 overlay interaction layer
= Alpha V3.9.10 Test Build 03
```

## Stack

- Native C++20
- CMake / MSVC
- OpenVR / SteamVR
- Dear ImGui
- Direct3D 11
- Win32

## Interaction ownership

### Wrist standby

- Overlay input method: `None`
- Sole owner: `DriveWristOverlayInteraction()`
- Sources inside that owner: controller ray and near-touch
- Visible dot and circular logical hit mask are independent
- Any delivered ImGui mouse-down is released when the pointer leaves the mask

### Expanded keyboard

- Overlay input method: SteamVR native Mouse
- Rendered pointer mirrors ImGui mouse position
- Physical keys own click and hover haptics
- Windows input layer distinguishes extended navigation keys and NumPad Enter

### Placement

- Normal mode hides mutation controls
- Layout mode enables trigger-handle and Grip-point grabs
- `GrabInputSource` prevents one source from releasing the other
- Damping 0 uses compositor tracked-device-relative transform
- Non-zero damping uses per-loop absolute rigid-transform blending
- Release converts to world-fixed space and may face the HMD

## Render scheduling boundary

The optional scheduler wraps only:

1. D3D render-target clear + ImGui draw
2. `SetOverlayTexture`

The following stay outside and execute every loop:

- live OpenVR pose reads
- wrist interaction
- layout / grab interaction
- overlay and system events
- clipboard / macro logic
- ImGui frame and interaction state
- overlay transform updates
- diagnostics

This boundary is intentional: visual texture rate may be reduced without reintroducing input/pose latency.

