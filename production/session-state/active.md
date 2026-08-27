<!-- STATUS -->
Epic: Battle System
Feature: ADR-0002 atomic resolution closeout before canonical proof content
Task: B00-C09 complete; remediate stale Bag cancellation before C10A
<!-- /STATUS -->

# Active Project State — 2026-08-27

## Current work

- B00 through C09 package delivery is complete under focused validation. C10
  and C11 are the only remaining roadmap packages.
- ADR-0002 remains an Accepted design, but its implementation gate is **FAIL**
  at `f48146f4f439930ed06f5f7feaf957514bcc4408`.
- The blocker is the accepted stale Bag-cancellation path in
  `Game/Source/PokemonSolarus/Private/Battle/BattleEngine.cpp`. Its
  `FinishAcceptedAction` helper finishes the action, advances the locked-action
  cursor, runs the still-fallible post-action boundary path, and increments the
  state version before invariant validation and resolution creation complete.
  This is live-first behavior, not ADR-0002's prepare/stage/commit rule.
- C10A Required Canonical Rows is the next roadmap package only after that
  implementation defect is repaired and the ADR-0002 gate passes. Preserve the
  order `C10A -> C10B -> C11A -> C11B`.
- Cry for Help, wild reinforcement, and `CallReinforcement` remain **Freeze
  until call by user**. Existing related setup, state, snapshot,
  encounter-policy, replay, and test code remains unchanged.
- Battle HUD visuals and Blueprint assets remain user-owned. Do not change
  layout, styling, art, materials, textures, composition, or motion appearance.

## Verified evidence

- Current HEAD is `f48146f4f439930ed06f5f7feaf957514bcc4408`.
- The final evidence root is
  `Game/Saved/AutomationReports/ADR0002-Task5-Final-20260827-111636`.
  Its 22 exported `index.json` files report 594 successes in total, with zero
  succeeded-with-warnings, failures, not-run, or in-process tests.
- The 21 ADR/affected-filter reports before the full suite total 280 successes:
  104 ADR-0002 tests and 176 affected package/runtime tests. The full
  `PokemonSolarus.Battle` report passed 314 tests.
- These reports are a strong baseline, but they do not prove the omitted
  failure branch. The stale Capture cancellation test in
  `BattleAtomicCheckpointTests.cpp` and the stale Bag tests in
  `BattleBagItemTests.cpp` prove nominal cancellation only; they do not inject
  post-action boundary, invariant, or resolution-preparation failure after the
  action has begun.
- The gate report is
  `production/gate-checks/2026-08-27-adr-0002-implementation-fail.md`.

## Working-tree scope to preserve

- Preserve the pre-existing modification to
  `docs/registry/architecture.yaml`.
- Preserve the pre-existing untracked ADR-0003, ADR-0004, and structural-split
  documents. The approved documentation cleanup may make only the recorded
  surgical corrections to ADR-0004 and the structural-split handoff; ADR-0003
  and the architecture registry remain excluded.
- Do not change production source, tests, visual assets, Blueprints, maps,
  configuration, `.uproject` data, module rules, C10 content data, or generated
  Unreal output as part of the documentation cleanup.
- Preserve replay schema `6`, existing enum ordinals, and frozen
  Cry/reinforcement behavior.
- Do not commit, stage, push, create branches, or rewrite Git history unless the
  user explicitly asks.

## Next

1. Start a fresh, bounded implementation session for the stale accepted Bag
   cancellation. Limit the proposed code write set to `BattleEngine.cpp`,
   `BattleBagItemTests.cpp`, and `BattleAtomicCheckpointTests.cpp`; obtain
   approval before editing.
2. Stage every fallible boundary/request/resolution fact before changing live
   action state, then commit once. Add fault-injection proof that every failure
   preserves resources, Bag quota, RNG, state, cursor, events, action progress,
   and resolution history.
3. Run the forced-Unity build and the exact 22-report ADR/affected/full-Battle
   matrix. Judge every exported `index.json`, then rerun the implementation
   gate.
4. After a PASS, proceed in order: `C10A -> C10B -> C11A -> C11B`.
