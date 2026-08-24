<!-- STATUS -->
Epic: Battle System
Feature: Encounters, Capture, Escape, Reinforcement, and Partner Battles
Task: C09B session 1 complete; C09B session 2 is next
<!-- /STATUS -->

# Active Project State — 2026-08-24

## Current work

- The user explicitly verified and accepted the production runtime/HUD slice on
  2026-08-24. That supersedes the prior manual HUD gate for battle-mechanics
  sequencing.
- C09A remains preserved as accepted uncommitted work in the current worktree.
- C09B session 1 is complete. It adds exact Scarlet/Violet capture math/RNG,
  Poke Ball selection/execution, multiple-capture removal/cancellation behavior,
  ordered pending destinations with retained facts, public capture metadata,
  replay schema 5, and the complete shared C09B setup/snapshot/replay schema.
- The Battle HUD visuals and Blueprint assets remain user-owned. C09B session 1
  did not modify presentation, assets, configuration, module rules, or the
  `.uproject`.
- C09B as a whole is not complete. Run, Cry for Help, configured WildFlee, and
  `CallReinforcement` execution remain unimplemented; C09C remains blocked.

## Verified evidence

- `Game/Saved/Automation/ADR0001-DataSource-20260824-1000/report/index.json`: 2 succeeded, 0 failed, 0 not run, 0 in process.
- `Game/Saved/Automation/ADR0001-BattleUI-Rerun-20260824-1004/report/index.json`: 20 succeeded, 0 failed, 0 not run, 0 in process. This supersedes the earlier 18/20 run.
- `Game/Saved/Automation/ADR0001-C02BAdapter-20260824-1005/report/index.json`: 2 succeeded, 0 failed, 0 not run, 0 in process.
- `Game/Saved/Packaging/ADR0001-Win64-20260824-1013/PackagedSmoke.log`: the staged Windows build mounted its containers, loaded `FoundationMap` with `BattleGameMode`, and exited with status 0 without fatal/error entries.
- Runtime/HUD manual acceptance is supplied by the user's explicit verification;
  the automated reports above do not independently prove visual dimensions.
- `Game/Saved/Automation/C09A-PoliciesSelectors-Final-20260824T095839Z/build.log`:
  `PokemonSolarusEditor Win64 Development` succeeded with exit code 0.
- `Game/Saved/Automation/C09A-PoliciesSelectors-Final-20260824T095839Z/report-final/index.json`:
  the exact `PokemonSolarus.Battle.C09A` filter passed 6/6, with 0 warnings, 0
  failures, 0 not run, and 0 in process. Every reported path is C09A and report
  SHA-256 is
  `c72f3b076d8f7fcec3af65ce3d4600a579d97221c7a2be18ad8f762e5fb3cd11`.
- The final C09B session-1 `PokemonSolarusEditor Win64 Development` build
  succeeded with `-ForceUnity -DisableAdaptiveUnity -BytesPerUnityCPP=1
  -NoUBA`; its durable log is
  `Game/Saved/Automation/C09B-Capture-20260824T125438Z/build-final.log`.
- `Game/Saved/Automation/C09B-Capture-20260824T125438Z/report-final/index.json`:
  the exact `PokemonSolarus.Battle.C09B` filter passed 4/4, with 0 warnings, 0
  failures, 0 not run, and 0 in process. Every reported path is under
  `PokemonSolarus.Battle.C09B.Capture`, every entry has 0 warnings and 0 errors,
  process exit code is `0`, and report SHA-256 is
  `9269891e673157050a0d5b4ad920760318b0e64f494b19c5697c4fe0bfd5f7ea`.

## Working-tree scope to preserve

- C09A runtime contracts: `BattleEncounterPolicy` and `BattleActionSelector`.
- C09A integration: the narrow setup validation and Boss/Gym Revive rule changes.
- C09A tests: `BattleEncounterPolicySelectorTests.cpp` and the test-only scripted
  selector.
- C09A status records: the live battle roadmap, this package file, and this
  active-session handoff.
- C09B session-1 capture runtime/schema sources, four focused capture tests, and
  the mechanical replay-schema assertions must be preserved for session 2.
- Preserve all user-owned visual work and every unrelated change. Do not commit,
  stage, push, branch, or rewrite Git history unless the user explicitly asks.

## Next

1. C09B session 1 complete; session 2 Run, Cry for Help, and WildFlee next.
2. Start C09B session 2 in a fresh session, reading the live authorities and
   this handoff. Reuse replay schema 5; do not make another schema-format change.
3. Implement only Run, Cry for Help, configured WildFlee, and the required
   `CallReinforcement` execution/tests for the session-2 contract.
4. Do not begin C09C until all of C09B is complete.
