# TB03 TEST13 — Dashboard Grip Bypass

Base checkpoint: `TB03_WristVisibility_DashboardInput_TEST12`, itself based on `TB03_KnucklesGrip_VALIDATED`.

## Confirmed TEST12 hardware result

- Wrist dot back-of-hand visibility: PASS (hide/show with hand facing).
- Wrist dot Grip relocation in VRChat: PASS.
- SteamVR Dashboard UI operation: PASS.
- Wrist dot Trigger click while Dashboard is open: PASS.
- Returning to VRChat/manual wrist click path: PASS.
- Remaining failure: while SteamVR Dashboard is visible, Grip cannot grab either the wrist dot or the expanded keyboard.

## TEST13 scope

TEST13 does not replace TEST12's successful Dashboard Mouse handoff. It splits Dashboard interaction into two independent owners:

1. Trigger / laser click remains owned exclusively by SteamVR native Overlay Mouse.
2. Grip remains a VRFullKeyboard grab-only side channel. Grip never injects ImGui mouse clicks.

### Knuckles fallback

The validated Knuckles gesture normally reads `VRControllerState_t::ulButtonTouched`. TEST13 additionally mirrors `VREvent_ButtonTouch` / `VREvent_ButtonUntouch` for `k_EButton_Grip`. While Dashboard is visible, this event state can backstop direct controller state if SteamVR has shifted legacy input focus.

Grip events are mirrored from both:

- `IVRSystem::PollNextEvent()`
- `IVROverlay::PollNextOverlayEvent()` when the runtime routes focused-controller events there

No SteamVR Action Manifest or binding JSON is introduced.

### Wrist dot

During Dashboard native Mouse mode, the controller currently owning SteamVR's pointer may still start `BeginWristGripGrab()`. Once Grip grab starts, Trigger mouse-button events are ignored until release, while pointer focus/move events continue updating normally.

### Expanded keyboard

The existing `DriveLayoutAndGrabInteraction()` path is retained. It gains the same Knuckles touch-event fallback through `ReadControllerGripDown()`, so Dashboard mode does not require a second keyboard-grab implementation.

## Required real-VR validation

1. Open SteamVR Dashboard and confirm normal Dashboard Trigger clicking still works.
2. With wrist dot visible, point at it, fully release Grip, then touch Grip again: dot should begin relocation.
3. Release Grip: dot must stay at the new wrist-local position.
4. Expand/summon the full keyboard while Dashboard remains open.
5. Point at the keyboard, fully release Grip, then touch Grip again: keyboard should freely grab.
6. Release Grip: keyboard must remain at the new position.
7. While Dashboard remains open, Trigger-click the wrist dot / keyboard normally to confirm no double click or stuck press.
8. Close Dashboard and recheck the original Knuckles `release -> arm -> touch -> grab` behavior in VRChat.

Status: source/static validation only; SteamVR hardware validation pending.
