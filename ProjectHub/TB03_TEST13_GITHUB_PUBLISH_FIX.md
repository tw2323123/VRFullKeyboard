# TB03 TEST13 — GitHub Publish Repository Bootstrap Fix

TEST13 VR hardware validation is PASS. This hotfix does not change VR input behavior and does not create TEST14.

## Problem
Developer handoff ZIPs do not contain `.git`, so Control Center previously blocked **Publish GitHub Release** immediately.

## Fix
When `.git` is missing, Control Center now offers to rebuild Git metadata from the configured official repository, without overwriting current Source. It performs `git init`, attaches `origin`, fetches `origin/main` + tags, recreates local `main`, points HEAD to it, and uses `git reset --mixed HEAD` so TEST13 files remain as working-tree differences. The existing stash / pull / restore / commit / push flow then continues.

## Validation boundary
Source/static validation completed here. Final Windows validation should click **發布 GitHub 新版** from a ZIP-derived project folder and confirm the repair prompt, metadata rebuild, and release dialog continuation.
