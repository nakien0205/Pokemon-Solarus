<!-- STATUS -->
Epic: Battle System
Feature: Encounters, Capture, Escape, and Partner Battles
Task: C09B complete; C09C Partner Double Battles is dependency-clear and next
<!-- /STATUS -->

# Active Project State — 2026-08-24

## Current work

- The user explicitly verified and accepted the production runtime/HUD slice on
  2026-08-24. That supersedes the prior manual HUD gate for battle-mechanics
  sequencing.
- C09A and C09B session 1 were committed before this session at baseline
  `7965042a3730869fb5adc983f2551321186c7758`.
- C09B session 1 provides exact Scarlet/Violet capture math/RNG, Poke Ball
  selection/execution, multiple-capture removal/cancellation, ordered pending
  destinations with retained facts, public capture metadata, and replay schema
  `5`.
- C09B session 2 adds exact Run legality/formula/counter behavior and explicit
  configured WildFlee generation, probability, per-actor removal, continuation,
  terminal outcome, and replay equality. Replay schema remains `5`.
- Cry for Help, wild reinforcement, and `CallReinforcement` are **Freeze until
  call by user**. Existing related setup, state, snapshot, encounter-policy,
  replay, and test code remains unchanged. This frozen mechanic is not part of
  C09B acceptance and does not block C09C.
- The Battle HUD visuals and Blueprint assets remain user-owned. C09B modified
  no presentation, assets, configuration, module rules, or `.uproject` data.
- C09B is complete. C09C is dependency-clear and not started.

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
- `Game/Saved/Automation/C09B-Capture-20260824T125438Z/report-final/index.json`:
  the session-1 C09B filter passed the four Capture tests with 0 warnings or
  failures.
- The C09B session-2 `PokemonSolarusEditor Win64 Development` build succeeded
  with `-ForceUnity -DisableAdaptiveUnity -BytesPerUnityCPP=1 -NoUBA`; its log is
  `Game/Saved/Automation/C09B-WildFlow-Final-20260824T135514Z/build-final.log`.
- `Game/Saved/Automation/C09B-WildFlow-Final-20260824T135514Z/report-final/index.json`:
  the exact `PokemonSolarus.Battle.C09B` filter passed 7/7, with 0 succeeded with
  warnings, 0 failed, 0 not run, and 0 in process. Every reported path is under
  `PokemonSolarus.Battle.C09B`, every entry has 0 warnings/errors, and report
  SHA-256 is
  `8620e75256508cb157e13fa295c785ba540a450d6dd352dddabe0c2a1986874c`.

## Working-tree scope to preserve

- Preserve all accepted C09A policy/selector work and C09B Capture runtime,
  schema, and four Capture tests.
- Preserve C09B Run/WildFlee pure rules, engine integration, and three focused
  tests.
- Preserve replay schema `5`, action/band/outcome ordinals, and all Cry/
  reinforcement-related code unchanged while the mechanic is frozen.
- Preserve all user-owned visual work and every unrelated change. Do not commit,
  stage, push, branch, or rewrite Git history unless the user explicitly asks.

## Next

1. Start C09C Partner Double Battles in a fresh session, reading the live
   authorities and this handoff.
2. Treat Cry for Help and wild reinforcement as **Freeze until call by user**;
   do not alter their existing code unless the user explicitly reopens them.
3. Keep C09C separate from C09B and do not expand into UI/assets, persistence,
   rewards, deployment, or Git writes without explicit approval.
