<!-- STATUS -->
Epic: Battle System
Feature: Behavior-preserving Battle Engine structural split before C10A
Task: Atomic-test Wave T1 complete; production Waves P0-P2 await approval
<!-- /STATUS -->

# Active Project State — 2026-08-27

## Current work

- B00 through C09 package delivery is complete under focused validation. C10
  and C11 are the only remaining roadmap packages.
- ADR-0002 is Accepted and its bounded implementation gate is **PASS** at
  `b5db3e440d7c6eb5ba6ddbcc01a92a3c9b8756c0`.
- The stale accepted Bag-cancellation blocker is closed. The live path now
  prepares its state delta, queue boundary, replacement requests, events, and
  resolution before the final identity recheck and one commit. Six focused ADR
  tests cover Bag and Capture cancellation routes, recoverable preparation
  failures, mandatory replacement, and final stale identity.
- The bounded post-ADR structural delta review and Guide Wave G1B are complete.
  G1B refreshed this file, `docs/battle-engine-structural-split-handoff.md`, and
  a dated ADR-0004 prerequisite amendment.
- Atomic-test Wave T1 is complete. The 84 exact ADR-0002 test paths moved from
  `BattleAtomicCheckpointTests.cpp` into eight focused test sources and four
  private support pairs. The monolith was removed only after an exact path-set
  comparison. T1 changed no production C++, public contract, Bag test, or test
  harness.
- Production Waves P0, P1, and P2 remain unapproved. Do not infer production
  approval from G1B, T1, or the task-specific T1 commit and push authorization.
- C10A Required Canonical Rows remains the next roadmap package after the
  separately approved structural work. Preserve the order
  `C10A -> C10B -> C11A -> C11B`.
- Cry for Help, wild reinforcement, and `CallReinforcement` remain **Freeze
  until call by user**. Existing related setup, state, snapshot,
  encounter-policy, replay, and test code remains unchanged.
- Battle HUD visuals and Blueprint assets remain user-owned. Do not change
  layout, styling, art, materials, textures, composition, or motion appearance.

## Verified evidence

- The pre-T1 baseline HEAD and `origin/main` were
  `3259405d73be5a634ce855a7d382f838eeb36ae6`. Live production source still
  matches the accepted production checkout
  `b5db3e440d7c6eb5ba6ddbcc01a92a3c9b8756c0`; T1 changes only test translation
  units and current structural guides.
- The final evidence root is
  `Game/Saved/AutomationReports/ADR0002-StaleBag-Final-20260827-164919`.
  Its 22 exported `index.json` files report 606 successes in total, with zero
  succeeded-with-warnings, failures, not-run, or in-process tests.
- The 21 ADR/affected-filter reports before the full suite total 286 successes:
  110 ADR-0002 tests and 176 affected package/runtime tests. The full
  `PokemonSolarus.Battle` report passed 320 tests.
- The forced-Unity editor build passed with `-ForceUnity`,
  `-DisableAdaptiveUnity`, `-BytesPerUnityCPP=1`, and `-NoUBA`. Every report
  discovered tests, contained only exact-prefix unique paths, and had zero
  per-test warnings or errors.
- The gate report is
  `production/gate-checks/2026-08-27-adr-0002-implementation-pass.md`. The
  earlier `implementation-fail.md` remains a truthful historical record for
  the superseded checkout and 594-success baseline.
- T1's forced-Unity editor build passed with `-ForceUnity`,
  `-DisableAdaptiveUnity`, `-BytesPerUnityCPP=1`, and `-NoUBA`. The fresh
  evidence root is
  `Game/Saved/AutomationReports/BattleStructural-T1-20260827-185852`.
  `PokemonSolarus.Battle.ADR0002` passed 110 tests and full
  `PokemonSolarus.Battle` passed 320 tests. All exported aggregate and per-test
  warning/error counters are zero.
- The full sorted test-path set exactly matches the accepted 320-test report.
  Its SHA-256 is
  `693e6aff39767ae1e8c771ba9b896836fd360db1f127128e24e82ded2c58e25d`.

## Working-tree scope to preserve

- Preserve the pre-existing modification to
  `docs/registry/architecture.yaml`.
- Preserve the pre-existing untracked ADR-0003 document and the architecture
  registry. ADR-0004 is also pre-existing untracked work; G1B authorizes only
  the dated prerequisite amendment recorded in that document. Preserve all of
  its other content.
- The exact G1B write set is this file,
  `docs/battle-engine-structural-split-handoff.md`, and
  `docs/architecture/adr-0004-production-action-orchestration-and-observer-safe-resolution-projection.md`.
- T1's exact code layout and validation evidence are recorded in
  `docs/battle-engine-structural-split-handoff.md`. The later documentation and
  Git approval covers only the T1 source changes, this file, and that handoff;
  it excludes the architecture registry, ADR-0003, and ADR-0004.
- Do not change production source, tests, visual assets, Blueprints, maps,
  configuration, `.uproject` data, module rules, or C10 content data without a
  new task-specific approval.
- Unreal validation may update its normal generated files under
  `Game/Saved/Config/**` and `Game/Saved/Logs/**`; final exported evidence must
  still use a fresh unique `Game/Saved/AutomationReports/**` root.
- Preserve replay schema `6`, existing enum ordinals, and frozen
  Cry/reinforcement behavior.
- Do not commit, stage, push, create branches, or rewrite Git history unless the
  user explicitly asks.

## Next

1. Treat T1 as complete; do not continue test-split cleanup or behavior changes
   under its approval.
2. Obtain separate approval before P0. If structural work continues, preserve
   the order `P0 -> P1 -> P2 -> G2` and use a fresh bounded session per wave.
3. After the approved structural work closes, resume roadmap order at
   `C10A -> C10B -> C11A -> C11B`.
