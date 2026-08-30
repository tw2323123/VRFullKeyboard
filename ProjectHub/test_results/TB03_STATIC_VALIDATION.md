# TB03 Static Validation

Date: 2026-08-25

## Passed

- `python3 tests/static_integration_checks.py`
  - balanced C++ delimiters
  - all new INI settings have Save / Load counterparts
  - Test Build identity and Control Center cache key
  - wrist single-input and circular-mask contracts
  - Grip / Trigger release ownership contracts
  - curvature flat-reset contract
  - extended-key / NumPad Enter mapping contract
  - texture scheduling remains downstream of immediate input paths
  - no `Sleep`, `sleep_until` or `WaitFrameSync` call in the VR loop
- `git diff --check`
- clean comparison against a fresh extraction of the uploaded TB02 archive
  - no unintended change to the existing Control Center C++ source
  - no unintended change to updater, workflow or SemVer contents
  - expected changes limited to core integration, build identity, tests and documentation

## Environment limitation

This validation workspace provides CMake, but it is not a Windows/MSVC environment and does not provide the Windows SDK, SteamVR runtime or tracked controllers. A Windows MSVC compile and the complete hands-on matrix in `CURRENT_TEST_BUILD.md` remain required.
