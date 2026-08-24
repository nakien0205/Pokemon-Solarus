<!-- STATUS -->
Epic: Battle System
Feature: Encounters, Capture, Escape, and Partner Battles
Task: C09 complete; C10A Required Canonical Rows is dependency-clear and next
<!-- /STATUS -->

# Active Project State — 2026-08-24

## Current work

- The user explicitly verified and accepted the production runtime/HUD slice on
  2026-08-24. That supersedes the prior manual HUD gate for battle-mechanics
  sequencing.
- C09A, C09B, and C09C are complete under their focused filters. C09C began
  from clean committed baseline
  `8d52dfca58c879cf4d015a3f4cf35b0296232ee5` and required one session with no
  subagents because its changes shared one engine/state/event/replay boundary.
- C09C freezes separate player and partner Trainer ownership, parties, Bags,
  switches, action allowances, selector assignments, and resolved Human or
  PartnerAI control. Partner observations may see the player's command; enemy
  observations may not. Allied support remains legal without weakening
  owner-party item and switch restrictions.
- Partner capture is unavailable, an exhausted partner slot stays empty, and a
  full player-party wipe continues while the partner can battle.
- `PartnerTeamVictory` restores the first valid player party entry to 1 HP,
  guarantees its major status is clear, and emits typed
  `PartnerTeamVictoryRecovery` event ordinal `52` before `BattleEnded`.
- Core-authority final snapshots expose typed NPC-partner facts marking
  persistent EXP and EV eligibility false. The core performs no reward
  calculation or persistent write. Canonical replay schema is now `6`.
- Cry for Help, wild reinforcement, and `CallReinforcement` remain **Freeze
  until call by user**. Existing related setup, state, snapshot,
  encounter-policy, replay, and test code remains unchanged.
- The Battle HUD visuals and Blueprint assets remain user-owned. C09C modified
  no presentation, assets, configuration, module rules, or `.uproject` data.

## Verified evidence

- The final C09C `PokemonSolarusEditor Win64 Development` build succeeded with
  `-ForceUnity -DisableAdaptiveUnity -BytesPerUnityCPP=1 -NoUBA`:
  `Game/Saved/Automation/C09C-Partner-Final-20260824T143500Z/build.log`.
- `Game/Saved/Automation/C09C-Partner-Final-20260824T143500Z/report/index.json`:
  the exact `PokemonSolarus.Battle.C09C` filter passed 6/6, with 0 succeeded
  with warnings, 0 failed, 0 not run, and 0 in process. Every reported path is
  under C09C and has 0 warnings/errors; process exit code is `0`, and report
  SHA-256 is
  `46eda7e469474a8078b43e5b2172aa0ee3bd3d1ba8c23d9de227c12eb8728fa7`.
- The first C09C-only diagnostic run passed 5/6 and exposed only test-fixture
  assumptions. A later completion audit added explicit starting-status cure
  proof; its first build exposed a test-only projection-type compile error.
  Both were corrected, and the final replay proof excludes the test-only status
  mutation. Neither earlier diagnostic is acceptance evidence.
- No older package filter, full Battle suite, project-wide suite, Cry test,
  C10 test, or other Automation filter was run during C09C.

## Working-tree scope to preserve

- Preserve all accepted C09A encounter-policy/selector work, C09B Capture and
  WildFlow behavior, and C09C PartnerDouble ownership/outcome/progression work.
- Preserve replay schema `6`, all pre-existing enum ordinals, and appended
  `PartnerTeamVictoryRecovery` ordinal `52`.
- Preserve all Cry/reinforcement-related code unchanged while the mechanic is
  frozen.
- Preserve all user-owned visual work and every unrelated change. Do not
  commit, stage, push, branch, or rewrite Git history unless the user explicitly
  asks.

## Next

1. Start C10A Required Canonical Rows in a fresh session, reading the live
   authorities and this handoff.
2. Treat Cry for Help and wild reinforcement as **Freeze until call by user**;
   do not alter their existing code or proof content unless explicitly reopened.
3. Keep C10A separate from C09C and do not expand into UI/assets, persistence,
   rewards, deployment, or Git writes without explicit approval.
