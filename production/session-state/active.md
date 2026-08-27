<!-- STATUS -->
Epic: Battle System
Feature: Behavior-preserving Battle Engine structural split before C10A
Task: Production Wave P1 complete; P2 approved but not started; G2 awaits approval
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
- Private-production-seam Wave P0 is approved and complete. Its exact six
  private support pairs now own the shared helper families while all 28
  `FBattleEngine` member definitions remain in `BattleEngine.cpp`.
- Non-checkpoint member-relocation Wave P1 is approved and complete. The five
  focused production sources now own snapshots, decision flow, end turn,
  between-actions stat refresh, and replay export. P2 is separately approved for
  a later fresh bounded implementation session but has not started. G2 remains
  unapproved.
- C10A Required Canonical Rows remains the next roadmap package after the
  separately approved structural work. Preserve the order
  `C10A -> C10B -> C11A -> C11B`.
- Cry for Help, wild reinforcement, and `CallReinforcement` remain **Freeze
  until call by user**. Existing related setup, state, snapshot,
  encounter-policy, replay, and test code remains unchanged.
- Battle HUD visuals and Blueprint assets remain user-owned. Do not change
  layout, styling, art, materials, textures, composition, or motion appearance.

## Verified evidence

- The pre-P0 baseline HEAD and `origin/main` were
  `89663923ef0d101868aa3e016847901c69db4924`. That checkout contains the
  validated T1 test split. P0 changed production only by relocating shared
  private helpers out of `BattleEngine.cpp`; it does not change the public
  facade, member-method locations, or behavior contracts.
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
- P0's corrected forced-Unity editor build passed with all four required flags.
  Its build log is
  `Game/Saved/Logs/BattleStructural-P0-20260827-200221-ForcedUnityBuild.log`.
- P0's exact serial 22-filter evidence root is
  `Game/Saved/AutomationReports/BattleStructural-P0-20260827-200412`. The
  exported reports contain 606 successes in aggregate, including 320 for full
  `PokemonSolarus.Battle`; every aggregate and per-test issue counter is zero.
- `BattleEngine.cpp` is 13,035 lines after P0. The 12 exact private support
  files exist, no `.cpp` includes another `.cpp`, and no P1 or P2 production
  source has been created.
- P1's corrected forced-Unity editor build passed with `-ForceUnity`,
  `-DisableAdaptiveUnity`, `-BytesPerUnityCPP=1`, and `-NoUBA`. Its build log is
  `Game/Saved/Logs/BattleStructural-P1-20260827-212714-ForcedUnityBuild.log`.
- P1's exact serial 22-filter evidence root is
  `Game/Saved/AutomationReports/BattleStructural-P1-20260827-212839`. The 22
  readable exported reports contain 606 successes in aggregate, including 320
  for full `PokemonSolarus.Battle`; all aggregate and per-test issue counters
  are zero. Every report contains exact-prefix unique paths, and every path set
  exactly matches P0's accepted matrix.
- `BattleEngine.cpp` is 8,990 lines after P1 and retains 12 member definitions:
  construction, creation, test-fixture creation, and the seven P2 checkpoint
  methods. The five P1 sources contain the other 16 definitions. All 28 P0
  definitions were reconstructed exactly once without changing their logical
  source lines. No `.cpp` includes another `.cpp`, and no P2 source exists.

## Working-tree scope to preserve

- The canonical ADR and traceability-registry directory is now
  `docs/registry/architecture/`. References under current guides, registries,
  examples, and package documents use that path. The old `docs/architecture/`
  path remains only in dated historical gate reports.
- Preserve the pre-existing modification to
  `docs/registry/architecture.yaml`.
- Preserve the pre-existing untracked ADR-0003 document and the architecture
  registry. ADR-0004 is also pre-existing untracked work; G1B authorizes only
  the dated prerequisite amendment recorded in that document. Preserve all of
  its other content.
- The exact G1B write set is this file,
  `docs/battle-engine-structural-split-handoff.md`, and
  `docs/registry/architecture/adr-0004-production-action-orchestration-and-observer-safe-resolution-projection.md`.
- T1's exact code layout and validation evidence are recorded in
  `docs/battle-engine-structural-split-handoff.md`. The later documentation and
  Git approval covers only the T1 source changes, this file, and that handoff;
  it excludes the architecture registry, ADR-0003, and ADR-0004.
- The current P0 commit write set is `BattleEngine.cpp`, the exact six private
  support `.h`/`.cpp` pairs, this file, and
  `docs/battle-engine-structural-split-handoff.md`. The user separately approved
  one commit and push for only that set. It excludes the architecture registry,
  ADR-0003, and ADR-0004.
- P1's exact hand-edited write set is `BattleEngine.cpp`,
  `BattleEngineSnapshots.cpp`, `BattleEngineDecisionFlow.cpp`,
  `BattleEngineEndTurn.cpp`, `BattleEngineBetweenActions.cpp`,
  `BattleEngineReplay.cpp`, this file, and
  `docs/battle-engine-structural-split-handoff.md`. Its only other writes are
  ordinary generated build and Automation output under `Game/Saved`.
- P2 is approved only for a later fresh bounded implementation session using
  the exact seven-file map, exclusions, forced-Unity build, and serial
  22-filter matrix in `docs/battle-engine-structural-split-handoff.md`. This
  approval-sync and commit task must not create or modify P2 production source.
- Do not change any other production source, tests, visual assets, Blueprints,
  maps, configuration, `.uproject` data, module rules, or C10 content data
  without a new task-specific approval.
- Unreal validation may update its normal generated files under
  `Game/Saved/Config/**` and `Game/Saved/Logs/**`; final exported evidence must
  still use a fresh unique `Game/Saved/AutomationReports/**` root.
- Preserve replay schema `6`, existing enum ordinals, and frozen
  Cry/reinforcement behavior.
- The current task authorizes one commit and push containing only P1's exact
  source files plus this file and
  `docs/battle-engine-structural-split-handoff.md`. It excludes the architecture
  registry, ADR-0003, ADR-0004, P2 source, generated output, branches, and Git
  history rewrites.

## Next

1. Treat P1 as complete; do not continue relocation cleanup or behavior changes
   under its approval.
2. P2 is approved but not started. Do not implement it in this task; begin it
   only in a fresh bounded implementation session. G2 remains separately
   unapproved. Preserve the remaining order `P2 -> G2`.
3. After the approved structural work closes, resume roadmap order at
   `C10A -> C10B -> C11A -> C11B`.
