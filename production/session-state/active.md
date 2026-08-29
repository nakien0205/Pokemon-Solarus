<!-- STATUS -->
Epic: Battle System
Feature: C10A reusable typed-remediation lanes
Task: R1-R4A complete; R4B is next
<!-- /STATUS -->

# Active Project State — 2026-08-29

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
  between-actions stat refresh, and replay export.
- Checkpoint member-relocation Wave P2 is approved and complete. The seven
  focused production sources now own action start, Wild actions, Bag and
  Capture, voluntary Switch, pre-move, target resolution, and move effects.
  G2's documentation-only evidence capture and guide updates are complete. The
  user approved the final diff and evidence on 2026-08-28, so the structural
  split is accepted.
- The separately approved `BattleEffectExecutor` structural split is
  implemented and freshly validated. Its completed map uses one private context
  header plus six focused sources while preserving one staged context, one
  staged state, the ordered `TryExecute` coordinator, and the existing outer
  commit/publication path. The user separately authorized one bounded commit and
  push, and the implementation was published to `origin/main` as
  `504f036858bae310de8ad03ae450903ebedc2779`.
- An independent post-migration review found no changed battle logic, public
  contract, state ownership, RNG behavior, event ordering, commit/publication
  order, compile result, or link result. It found one non-runtime executor
  dependency concern: three focused sources manually redeclare six helpers whose
  definitions remain in `BattleEffectExecutor.cpp`. The declarations currently
  match and link. This documentation-only reconciliation records that concern;
  it does not claim the source seam has been remediated.
- The documentation reconciliation updated this file, both structural guides,
  the global roadmap, C10, C11, and 76 canonical architecture-directory
  references across 20 local project skills. Only
  `.codex/skills/architecture-decision/SKILL.md` and
  `.codex/skills/create-architecture/SKILL.md` are tracked; the other 18 skill
  files are intentionally ignored local tooling. Dated package evidence and
  ADR-0002 gate reports remain unchanged.
- C10A remediation lanes R1 through R4A are complete. R1 added the two typed
  target classes, R2 added private action-scoped redirection, R3 added the
  private typed ally action power modifier, and R4A added reusable authored hit
  qualifiers for status-move type immunity, Powder immunity, and Poison-type
  user reachability and accuracy behavior. Swift, Fly, Follow Me, Helping Hand,
  Toxic, and Powder moves are now expressible, but their C10A rows remain
  unauthored.
- R4B is next. Preserve the order
  `R4B -> R5 -> R6 -> R7 -> C10A -> C10B -> C11A -> C11B`.
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
  Its 22 exported `index.json` files report 606 test executions in total, with
  zero succeeded-with-warnings, failures, not-run, or in-process tests.
- The 21 ADR/affected-filter reports before the full suite total 286 executions:
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
  exported reports contain 606 executions in aggregate, including 320 for full
  `PokemonSolarus.Battle`; every aggregate and per-test issue counter is zero.
- `BattleEngine.cpp` is 13,035 lines after P0. The 12 exact private support
  files exist, no `.cpp` includes another `.cpp`, and no P1 or P2 production
  source has been created.
- P1's corrected forced-Unity editor build passed with `-ForceUnity`,
  `-DisableAdaptiveUnity`, `-BytesPerUnityCPP=1`, and `-NoUBA`. Its build log is
  `Game/Saved/Logs/BattleStructural-P1-20260827-212714-ForcedUnityBuild.log`.
- P1's exact serial 22-filter evidence root is
  `Game/Saved/AutomationReports/BattleStructural-P1-20260827-212839`. The 22
  readable exported reports contain 606 executions in aggregate, including 320
  for full `PokemonSolarus.Battle`; all aggregate and per-test issue counters
  are zero. Every report contains exact-prefix unique paths, and every path set
  exactly matches P0's accepted matrix.
- `BattleEngine.cpp` is 8,990 lines after P1 and retains 12 member definitions:
  construction, creation, test-fixture creation, and the seven P2 checkpoint
  methods. The five P1 sources contain the other 16 definitions. All 28 P0
  definitions were reconstructed exactly once without changing their logical
  source lines. No `.cpp` includes another `.cpp`, and no P2 source exists.
- P2's forced-Unity editor build passed on its first run with `-ForceUnity`,
  `-DisableAdaptiveUnity`, `-BytesPerUnityCPP=1`, and `-NoUBA`. Its build log is
  `Game/Saved/Logs/BattleStructural-P2-20260827-225012-ForcedUnityBuild.log`.
- P2's exact serial 22-filter evidence root is
  `Game/Saved/AutomationReports/BattleStructural-P2-20260827-225953`. The 22
  readable exported reports contain 606 executions in aggregate, including 320
  for full `PokemonSolarus.Battle`; all aggregate and per-test issue counters
  are zero. Every report contains exact-prefix unique paths, and every path set
  exactly matches P1's accepted matrix. The full sorted path-set SHA-256 remains
  `693e6aff39767ae1e8c771ba9b896836fd360db1f127128e24e82ded2c58e25d`.
- `BattleEngine.cpp` is 129 lines after P2 and retains only five construction
  and creation definitions. The seven P2 sources own one checkpoint definition
  each, while the five P1 sources retain their 16 definitions. All 28 P0
  definitions still exist exactly once. Every P2 source has self-contained
  includes and a unique named private namespace, and no `.cpp` includes another
  `.cpp`.
- G2 re-read the final live structural layout on 2026-08-28. All 25 expected
  files from P0, P1, and P2 are present with the recorded line counts. The 13
  facade-member sources still contain all 28 `FBattleEngine` definitions
  exactly once: five in `BattleEngine.cpp`, 16 across P1, and seven across P2.
  A fresh source scan found no `.cpp` include of another `.cpp`. This was a
  direct filesystem inspection; G2 ran no Git status or diff and makes no
  clean-worktree claim.
- G2 re-opened all 68 preserved exported `index.json` files under
  `Game/Saved/AutomationReports/BattleStructural-T1-20260827-185852`,
  `BattleStructural-P0-20260827-200412`,
  `BattleStructural-P1-20260827-212839`, and
  `BattleStructural-P2-20260827-225953`. T1's two reports contain 430
  executions. P0, P1, and P2 each contain 22 reports and 606 executions,
  including 320 in full `PokemonSolarus.Battle`. Every aggregate issue counter
  and per-test warning/error counter is zero, every test state is `Success`,
  and no report has a duplicate path. All 22 P2 path sets exactly match P1.
- G2 also re-read the P2 build log. It records all four required forced-Unity
  flags and `Result: Succeeded`. No new build or Automation run was required or
  authorized for this documentation-only closeout. This VERIFY is not a fresh
  build or test run and does not prove byte-for-byte source identity.
- The `BattleEffectExecutor` split's fresh forced-Unity editor build passed with
  `-ForceUnity`, `-DisableAdaptiveUnity`, `-BytesPerUnityCPP=1`, and `-NoUBA`.
  Its build log is
  `Game/Saved/Logs/BattleEffectExecutorSplit-20260828-091241-ForcedUnityBuild.log`.
- Its fresh serial 22-filter evidence root is
  `Game/Saved/AutomationReports/BattleEffectExecutorSplit-20260828-091837`.
  The 22 exported reports contain 606 executions: 286 focused and 320 full
  Battle, representing 320 unique full-Battle paths because the focused filters
  overlap the full suite. All aggregate issue counters and per-test warning or
  error counters are zero, all states are `Success`, every report has unique
  exact-prefix paths, and the full sorted path-set SHA-256 remains
  `693e6aff39767ae1e8c771ba9b896836fd360db1f127128e24e82ded2c58e25d`.
- The final executor source audit found the 102 context definitions exactly once
  in the approved `22 / 8 / 27 / 16 / 3 / 26` file split and all nine executor
  definitions exactly once. `BattleEffectExecutor.h` is byte-for-byte unchanged
  at its recorded SHA-256, and `BattleEngineMoveEffects.cpp` remains unchanged.
  The implementation retains one context construction, one staged state, the
  ordered coordinator, and the existing single outer commit/publication path.
- A later independent post-migration search found that the global roadmap, C10,
  C11, and one current-source paragraph in the structural handoff had not been
  refreshed after the accepted ADR-0002 and executor work. It also found local
  project skills still using the removed `docs/architecture/` directory. This
  documentation reconciliation corrected those live authorities and skill
  paths while preserving dated package hashes, completion snapshots, and gate
  reports as historical evidence.
- R4A's post-review forced-Unity editor build passed with `-ForceUnity`,
  `-DisableAdaptiveUnity`, `-BytesPerUnityCPP=1`, and `-NoUBA`. Its build log is
  `Game/Saved/Logs/R4A-HitRules-PostReview-20260829-160909-Build.log`.
- R4A's fresh serial evidence root is
  `Game/Saved/AutomationReports/R4A-HitRules-PostReview-20260829-160909`.
  The focused `PokemonSolarus.Battle.C05B.C10HitRules` report passed exactly
  seven tests. The eleven required affected filters also passed, for 152/152
  total executions with zero aggregate or per-test warnings, errors, failures,
  not-run, or in-process results. Both final reviews returned PASS with no
  remaining validated finding.

## Working-tree scope to preserve

- The canonical architecture-document directory is now
  `docs/registry/architecture/`. References under current guides, registries,
  examples, package documents, and local project skills use that path. The old
  `docs/architecture/` path remains only in dated historical gate reports and
  in current statements that explicitly identify it as the retired path.
- Do not modify `docs/registry/architecture.yaml`; it is outside this task.
- Preserve the pre-existing ADR-0003 and ADR-0004 documents. Neither is in
  G2's write set.
- ADR-0003 and ADR-0004 remain untracked local documents and therefore are not
  present in the published `origin/main` checkout. The tracked registry links
  to those local paths. This documentation pass does not authorize staging,
  committing, or changing either ADR or the registry.
- The exact G1B write set is this file,
  `docs/battle-engine-structural-split-handoff.md`, and
  `docs/registry/architecture/adr-0004-production-action-orchestration-and-observer-safe-resolution-projection.md`.
- T1's exact code layout and validation evidence are recorded in
  `docs/battle-engine-structural-split-handoff.md`. Its later task-specific
  documentation and Git approval covered only the T1 source changes, this file,
  and that handoff; it excluded the architecture registry, ADR-0003, and
  ADR-0004.
- P0's task-specific commit write set was `BattleEngine.cpp`, the exact six
  private support `.h`/`.cpp` pairs, this file, and
  `docs/battle-engine-structural-split-handoff.md`. It excluded the architecture
  registry, ADR-0003, and ADR-0004.
- P1's exact hand-edited write set is `BattleEngine.cpp`,
  `BattleEngineSnapshots.cpp`, `BattleEngineDecisionFlow.cpp`,
  `BattleEngineEndTurn.cpp`, `BattleEngineBetweenActions.cpp`,
  `BattleEngineReplay.cpp`, this file, and
  `docs/battle-engine-structural-split-handoff.md`. Its only other writes are
  ordinary generated build and Automation output under `Game/Saved`.
- P2 completed the exact seven-file map, exclusions, forced-Unity build, and
  serial 22-filter matrix in `docs/battle-engine-structural-split-handoff.md`.
  Its hand-edited implementation set is `BattleEngine.cpp`, the seven named P2
  sources, and that handoff. Its only other writes are ordinary generated build
  and Automation output under `Game/Saved`.
- G2's exact hand-edited write set is this file and
  `docs/battle-engine-structural-split-handoff.md`. It authorizes no production
  source, test, generated-output, C10A, or cleanup change.
- The `BattleEffectExecutor` implementation's exact hand-edited write set is
  `BattleEffectExecutor.cpp`, `BattleEffectExecutorContext.h`, the six focused
  `BattleEffectExecutor*.cpp` sources recorded in the handoff, this file, and
  `docs/battle-engine-structural-split-handoff.md`. Its only other writes are
  ordinary generated build and Automation output under `Game/Saved`. The
  implementation approval itself authorized no Git action.
- After the implementation and validation report, the user separately
  authorized one commit and push containing that exact code set plus this file,
  `docs/battle-engine-structural-split-handoff.md`, and
  `docs/battle-effect-executor-split-implementation-ready-draft.md`. Preserve
  the unrelated untracked ADR-0003 and ADR-0004 files and every other exclusion.
- Do not change any other production source, tests, visual assets, Blueprints,
  maps, configuration, `.uproject` data, module rules, or C10 content data
  without a new task-specific approval.
- Unreal validation may update its normal generated files under
  `Game/Saved/Config/**` and `Game/Saved/Logs/**`; final exported evidence must
  still use a fresh unique `Game/Saved/AutomationReports/**` root.
- Preserve replay schema `6`, existing enum ordinals, and frozen
  Cry/reinforcement behavior.
- After approving the final G2 diff, the user authorized one task-specific
  commit and push containing exactly this file and
  `docs/battle-engine-structural-split-handoff.md`. No branch or Git-history
  rewrite is authorized.
- R4A's exact hand-authored implementation set is the new
  `BattleMoveHitRules.h`, `BattleMoveHitRules.cpp`, and
  `BattleMoveHitRuleTests.cpp`; the modified `BattleDefinitions.h`,
  `BattleDataTableAdapter.cpp`, `BattleDefinitionCatalog.cpp`,
  `BattleEffectExecutor.cpp`, `BattleEffectExecutorContext.h`,
  `BattleEffectExecutorConditions.cpp`, and `BattleEffectExecutorDamage.cpp`;
  this file; and `docs/c10a-canonical-proof-content-implementation-ready-draft.md`.
  The user approved one bounded commit and push for that exact set. Preserve the
  unrelated untracked ADR-0003 and ADR-0004 files and all generated evidence.

## Next

1. Treat the `BattleEffectExecutor` structural split as implemented, freshly
   validated, and published at `504f036858bae310de8ad03ae450903ebedc2779`.
2. Keep the six-helper cross-translation-unit declaration seam recorded as a
   non-runtime source follow-up. Do not claim it has been remediated without a
   separately approved source change and validation.
3. Treat R1 through R4A as complete. R4A passed its forced-Unity build, all
   seven focused tests, eleven affected regression filters, and both required
   independent reviews.
4. R4B is the next separately approved remediation lane. Do not author C10A
   source rows until R4B-R6 and independent R7 pass. Preserve roadmap order
   `R4B -> R5 -> R6 -> R7 -> C10A -> C10B -> C11A -> C11B`.
