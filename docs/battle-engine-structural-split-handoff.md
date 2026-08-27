# Battle Engine Structural Split Handoff

## Status

**Guide Wave G1B and atomic-test Wave T1 were completed on 2026-08-27. The
post-ADR structural delta review is complete. Production Waves P0, P1, and P2
remain unapproved.**

This document records the completed read-only delta review and the live
approval boundaries. G1B authorized only this file,
`production/session-state/active.md`, and a dated prerequisite amendment in
ADR-0004. T1 was separately approved and was limited to the documented atomic
test split, this handoff, and its approved generated validation output. T1 did
not itself approve production C++, P0, C10A, or Git actions. The user later
authorized one task-specific T1 documentation sync, commit, and push; that does
not authorize any production wave. Each later wave still requires separate
user approval.

The review was intentionally limited to file size, responsibility boundaries,
and safe translation-unit decomposition. It was not an in-depth mechanics or
bug review.

## Source of truth and continuation rule

The original measurements below describe commit `c514c86` on 2026-08-26. The
completed delta review compared them with accepted production source/test
checkout `b5db3e440d7c6eb5ba6ddbcc01a92a3c9b8756c0`. The pre-T1 baseline HEAD and
`origin/main` were `3259405d73be5a634ce855a7d382f838eeb36ae6`; that later baseline
commit changes only ADR-0002 closeout documents. Live production source still
matches the accepted checkout. Live tests now differ only by the validated,
behavior-preserving T1 translation-unit split recorded below.

Live source, the accepted ADR-0002 gate, the worktree, the current roadmap
package, and exported Unreal Automation reports override all historical
measurements. A new full structural review is unnecessary unless those live
ownership or transaction boundaries change materially before implementation.

## Verified review snapshot

At `c514c86`:

- `Game/Source/PokemonSolarus/Private/Battle/BattleEngine.cpp` was 17,296
  physical lines and 16,639 nonblank lines.
- `Game/Source/PokemonSolarus/Private/Battle/BattleEffectExecutor.cpp` was
  7,534 physical lines and 7,285 nonblank lines.
- Those two files contained approximately 55.2 percent of production Battle
  `.cpp` code.
- `Game/Source/PokemonSolarus/Private/Tests/BattleAtomicCheckpointTests.cpp`
  was 7,090 physical lines and contained 57 tests.
- `Game/Source/PokemonSolarus/Private/Battle/BattleState.cpp` was 1,641
  physical lines.
- The public `BattleEngine.h` facade was compact.
- `BattleResolutionCommit.h/.cpp` were already focused at 77 and 207 physical
  lines.

The current source contained the following implemented atomic checkpoint
families, with clean focused exported reports inspected during the review:

- Run and WildFlee;
- non-Capture Bag cleanup;
- Capture;
- action start;
- voluntary Switch;
- PivotSwitch continuation; and
- pre-move gates and PP commit.

That evidence proves the implementations and focused reports existed at the
reviewed snapshot. The later accepted gate below supersedes it.

## ADR-0002 completion gate

The ADR-0002 prerequisite is satisfied at accepted production source/test
checkout `b5db3e440d7c6eb5ba6ddbcc01a92a3c9b8756c0`:

1. Target-resolution and move-effect checkpoint remediation is present.
2. Accepted stale Bag and Capture cancellation is staged through fallible
   boundary, request, event, and resolution preparation with explicit failure
   proof.
3. The forced-Unity editor build passed with `-ForceUnity`,
   `-DisableAdaptiveUnity`, `-BytesPerUnityCPP=1`, and `-NoUBA`.
4. The accepted evidence root
   `Game/Saved/AutomationReports/ADR0002-StaleBag-Final-20260827-164919`
   contains 22 readable exported `index.json` files and 606 successes.
5. The full `PokemonSolarus.Battle` filter has 320 successes. Every warning,
   failure, not-run, in-process, per-test warning, and per-test error counter is
   zero.
6. The final gate report is
   `production/gate-checks/2026-08-27-adr-0002-implementation-pass.md`.

## Required delta review after ADR-0002

The required narrow review is complete. Its verdict is **small revision**; the
handoff remains usable and no new structural review is needed.

| Hotspot | `c514c86` | Pre-T1 accepted checkout | Delta |
|---|---:|---:|---:|
| `BattleEngine.cpp` | 17,296 physical lines | 20,043 | +2,747 |
| `BattleEffectExecutor.cpp` | 7,534 physical lines | 7,642 | +108 |
| `BattleAtomicCheckpointTests.cpp` | 7,090 lines / 57 tests | 10,987 / 84 | +3,897 / +27 tests |

Additional verified facts:

- `BattleEngine.cpp` plus `BattleEffectExecutor.cpp` now contain 27,685 of
  46,336 production Battle `.cpp` lines, or 59.7 percent.
- Before T1, `BattleAtomicCheckpointTests.cpp` contained 20.1 percent of Battle
  test `.cpp` lines. T1 replaced it with eight focused test sources totaling
  8,172 physical lines and eight support files totaling 3,370 physical lines;
  the largest focused source is the 2,133-line Capture family.
- `BattleState.*` and `BattleResolutionCommit.*` are unchanged between
  `c514c86` and the accepted checkout. The public `BattleEngine.h` remains a
  compact 168-line facade and `FBattleEngine` still owns one authoritative
  `FBattleEngineState`.
- Target-resolution 3E5 and move-effect 3E6 add checkpoint-local identities,
  owned preparations, and deltas around the existing state/commit boundary.
  They do not create another state owner or commit seam.
- `BattleEffectExecutor` still uses one staged `FStateExecutionContext` and one
  owned `FBattleEffectExecutionPlan`; its split remains a later task.
- The worktree paths to preserve are modified
  `docs/registry/architecture.yaml`, untracked ADR-0003, and the pre-existing
  untracked ADR-0004 except for its explicitly approved G1B amendment.

Do not repeat the completed logic review or rerun old exploratory suites merely
to rediscover this plan.

## Mandatory guide-reference migration

Guide discovery is a prerequisite, not cleanup work to be remembered later.
Before modifying production C++ or tests, the split task must find every
repository guide that refers to the old monolithic files, old line locations,
or responsibilities that will move.

The discovery pass must search at least:

- root instruction and contributor files such as `AGENTS.md` and `CLAUDE.md`;
- `.codex/skills/`;
- `docs/`;
- `production/`;
- roadmap, package, architecture, validation, and handoff Markdown or YAML;
  and
- other tracked text files containing the old paths or named symbols.

Search by both file names and responsibilities. At minimum, include:

- `BattleEngine.cpp`;
- `BattleEffectExecutor.cpp`;
- `BattleAtomicCheckpointTests.cpp`;
- `BeginNextLockedAction`;
- `ExecuteCurrentWildAction`;
- `ExecuteCurrentBagItem`;
- `ExecuteCurrentSwitch`;
- `CommitCurrentMoveAfterPreMoveGates`;
- `ResolveCurrentMoveTargets`;
- `ExecuteCurrentMoveEffects`;
- `ResolveEndTurn`;
- `SubmitDecision`; and
- `FStateExecutionContext`.

The G1B discovery pass searched the required root instructions, skills, `docs/`,
`production/`, roadmap files, and other text by both filename and
responsibility. Its old-to-new classification is:

| Guide file | Old reference or assumption | Proposed new reference | Action |
|---|---|---|---|
| `docs/battle-engine-structural-split-handoff.md` | ADR-0002 still blocked; preliminary source and test partitions | Completed delta evidence and corrected partition below | Update in G1B |
| `production/session-state/active.md` | C10A immediately next; prior accepted checkout described as current HEAD | Bounded structural work is current; C10A remains next afterward; distinguish live HEAD from accepted source checkout | Update in G1B |
| `docs/architecture/adr-0004-production-action-orchestration-and-observer-safe-resolution-projection.md` | Historical authoring context says stale Bag cancellation remains open | Preserve the paragraph and add a dated accepted-gate amendment | Amend in G1B |
| `docs/architecture/adr-0002-battle-encounter-runtime-authority-and-atomic-resolution-commit.md` | Method-name responsibility and commit contracts | Method names and contracts remain unchanged | Preserve accepted decision |
| `plan/battle_mechanics/00-roadmap-index.md`, `02-core-contracts-events-and-rng.md`, `04-battle-state-snapshots-and-decisions.md`, `05-actions-order-and-targeting.md`, `06-hit-damage-effects-and-outcomes.md`, `07-parties-switching-and-replacements.md`, and `10-encounters-capture-escape-and-partner.md` | Historical paths, hashes, and package ownership | Historical accepted checkouts | Preserve completion records |
| `production/gate-checks/2026-08-27-adr-0002-implementation-fail.md` and `2026-08-27-adr-0002-implementation-pass.md` | Historical paths, line locations, and report evidence | Historical reviewed checkouts | Preserve evidence |
| ADR-0003, `docs/battle-system-interview-handoff.md`, the reusable-damage quick spec, `design/ux/battle-hud.md`, roadmap `03-stats-types-moves-and-data-adapters.md`, and `plan/battle_mechanics/reference/modern-rules-snapshot.md` | Responsibility-only or gameplay references without current file-location assumptions | Existing path-neutral wording | Preserve; no migration needed |
| `Game/Saved/AutomationReports/**`, logs, and generated Unreal output | Generated evidence | Generated evidence only | Never hand-edit |

No stale structural path assumption was found in `AGENTS.md`, `CLAUDE.md`,
`UE.md`, or `.codex/skills/`.

The post-T1 discovery pass repeated the filename and responsibility search.
References to the removed monolith now remain only in this handoff's historical
measurements and completed-T1 record, plus the two historical ADR-0002 gate
reports. Those gate reports remain unchanged as historical evidence. No other
active guide assumes that the monolith still exists, and all method-name
references outside this handoff remain path-neutral. After a separate explicit
documentation approval, `production/session-state/active.md` was refreshed to
record T1's completed layout and validation while keeping P0, P1, and P2
unapproved. The historical gate reports remain unchanged.

The exact approved G1B guide write set is:

- `docs/battle-engine-structural-split-handoff.md`;
- `production/session-state/active.md`; and
- only the dated prerequisite amendment in the pre-existing untracked
  `docs/architecture/adr-0004-production-action-orchestration-and-observer-safe-resolution-projection.md`.

All other matched guides and dirty paths are excluded. Later production waves
must update this handoff in the same bounded change as the corresponding file
moves so it never claims a proposed file already exists. Do not commit an
intermediate state in which active guides point at removed code.

After every split wave, repeat the reference search and prove that no stale
reference remains in an active guide. The structural change is not complete
until its guide migration is complete.

## What must remain centralized

Keep these contracts and ownership rules:

- `FBattleEngine` remains the public facade.
- `FBattleEngine` remains the sole owner of one authoritative
  `FBattleEngineState`.
- `BattleEngine.h` remains a compact public declaration file.
- `BattleResolutionCommit.h/.cpp` remain the common atomic commit seam unless
  live evidence reveals a concrete problem.
- Each checkpoint keeps an explicit identity -> preparation -> staging ->
  validation -> commit sequence.
- Effect execution keeps one staged context, one staged state, and one final
  commit boundary.
- Existing replay, event-order, stale-identity, RNG, and exact-once publication
  contracts must not change as a side effect of moving code.

Do not introduce a new Unreal module merely to reduce file length. Do not
include `.cpp` files from other `.cpp` files.

## Proposed `BattleEngine` translation units

The first production pass should be a behavior-preserving relocation. The
proposed boundaries are:

| Proposed file | Responsibility |
|---|---|
| `BattleEngine.cpp` | Constructor, destructor, `TryCreate`, and test-fixture creation only |
| `BattleEngineSnapshots.cpp` | Snapshot projection, filtering, and read-only getters |
| `BattleEngineDecisionFlow.cpp` | Decision startup, requests, batches, and all of `SubmitDecision`, including the embedded Pivot continuation |
| `BattleEngineActionStart.cpp` | Atomic action-start staging and `BeginNextLockedAction` |
| `BattleEngineWildActions.cpp` | Atomic Run and WildFlee execution |
| `BattleEngineBagActions.cpp` | The complete `ExecuteCurrentBagItem` method, including ordinary Bag, stale cancellation, and Capture helpers/branches |
| `BattleEngineVoluntarySwitch.cpp` | Voluntary-switch identity, staging, and execution |
| `BattleEnginePreMove.cpp` | Pre-move identity, gates, PP staging, and commit |
| `BattleEngineMoveTargets.cpp` | Atomic target resolution |
| `BattleEngineMoveEffects.cpp` | Atomic move-effect resolution coordinator |
| `BattleEngineEndTurn.cpp` | End-turn resolution |
| `BattleEngineBetweenActions.cpp` | `ApplyBetweenActionsStatRefresh` |
| `BattleEngineReplay.cpp` | Replay export methods |

Do not create `BattleEngineCaptureCheckpoint.cpp` in the first mechanical pass:
Bag and Capture are branches of the same public method, and Capture currently
reuses the Wild cleanup staging family. Do not create
`BattleEnginePivotSwitch.cpp` in that pass either: Pivot continuation is inside
`SubmitDecision` and moves with that complete method. Separating either branch
requires a later approved internal seam, not relocation disguised as a split.

A class method can be defined in any of these `.cpp` files by including
`BattleEngine.h`; `BattleEngine.cpp` does not import implementations from them.

## Proposed private shared support

Only promote a helper when at least two focused translation units need it.
Otherwise keep it local to its checkpoint file.

The completed delta review confirms these shared seams:

- `BattleEngineCommon.*` for no-draw random, stable identifiers, common state
  lookups, sources, and rejection facts used across checkpoint families;
- `BattleEngineCheckpointState.*` for shared exact identities and staged
  projection/delta support;
- `BattleEngineQueueBoundary.*` for pure queue-boundary and replacement
  planning;
- `BattleEngineEvents.*` for common event construction;
- `BattleEngineTriggerRuntime.*` for trigger dispatch and cleanup; and
- `BattleEngineSwitchPipeline.*` for switch application, entry hazards,
  immediate held items, and entry abilities.

Each name above means one private `.h`/`.cpp` pair. Headers contain only the
declarations and template definitions that callers require; non-template
definitions remain in the corresponding `.cpp`. Each file uses self-contained
includes and a unique named private namespace. A helper stays local when only
one focused translation unit needs it.

Do not replace all checkpoints with one generic base class. Their stale
identities and owned deltas differ, and the explicit code is part of their
auditability.

## Atomic test split

T1 is complete. It was executed separately from production relocation and
preserved all 84 exact Automation paths:

- `BattleAtomicWildActionTests.cpp`: 7 tests;
- `BattleAtomicCaptureTests.cpp`: 9 tests;
- `BattleAtomicActionStartTests.cpp`: 8 tests;
- `BattleAtomicVoluntarySwitchTests.cpp`: 8 tests;
- `BattleAtomicPivotSwitchTests.cpp`: 10 tests;
- `BattleAtomicPreMoveTests.cpp`: 19 tests;
- `BattleAtomicMoveTargetTests.cpp`: 12 tests; and
- `BattleAtomicMoveEffectTests.cpp`: 11 tests.

The four 3D2 Bag checkpoint tests remain in `BattleBagItemTests.cpp`.
`BattleAtomicBagActionTests.cpp` was not created, and neither
`BattleBagItemTests.cpp` nor `BattleAtomicCheckpointTestHarness.h` was modified.

The completed split uses these focused private support pairs:

- `BattleAtomicCheckpointTestCommon.h/.cpp` for generic scenario, catalog,
  setup, decision, event, and replay helpers;
- `BattleAtomicCheckpointTestFaults.h/.cpp` for reusable random and transaction
  failure seams;
- `BattleAtomicSwitchTestSupport.h/.cpp` for helpers shared by voluntary and
  Pivot switch tests; and
- `BattleAtomicMoveCheckpointTestSupport.h/.cpp` for helpers shared by
  pre-move, target, and effect checkpoints.

Family-specific helpers remain in their family test `.cpp`. Support code is
wrapped in `WITH_DEV_AUTOMATION_TESTS`, every test source has a unique named
namespace, and no `.cpp` includes another `.cpp`. The exact pre-delete path-set
comparison proved 84 unique old paths and 84 identical unique new paths before
`BattleAtomicCheckpointTests.cpp` was removed.

## `BattleEffectExecutor` follow-up

`BattleEffectExecutor.cpp` is the second production priority. The accepted
ADR-0002 gate confirms that its atomic move-effect and outcome staging is
stable, but it is excluded from the `BattleEngine` split and still requires a
later read-only delta review, exact write set, and separate approval.

Keep:

- the current private header contract;
- the ordered `FBattleEffectExecutor::TryExecute` coordinator; and
- exactly one staged `FStateExecutionContext` and commit.

Then consider moving the context declaration to a private header and defining
its methods across focused files for:

- common state-context construction, validation, lookup, and commit;
- damage and HP changes;
- statuses, volatiles, field, and side conditions;
- abilities and held items;
- switching and entry hazards; and
- trigger registration, dispatch, and cleanup.

These files must operate on the same staged context. They must not create
independent state copies or commits.

## Approval-bounded write sets and sequence

Use separate changes rather than one broad refactor. G1B and T1 have been
approved and completed. No production wave has been approved.

### G1B - active guide migration (approved)

Write only the three guide paths listed in the migration section. Run no build
or Automation and write no generated Unreal output.

### T1 - atomic tests (approved and completed 2026-08-27)

Under `Game/Source/PokemonSolarus/Private/Tests/`, the eight focused test
sources and four support `.h`/`.cpp` pairs listed above now replace
`BattleAtomicCheckpointTests.cpp`. All 84 paths moved before deletion. This
handoff was the only hand-edited guide in the original T1 wave. A later
explicit documentation approval refreshed `production/session-state/active.md`
before the T1 commit. The harness and Bag tests remain outside the write set.

### P0 - private production seams (proposed, not approved)

Modify:

- `Game/Source/PokemonSolarus/Private/Battle/BattleEngine.cpp`; and
- this handoff.

Create the exact six private support `.h`/`.cpp` pairs:

- `BattleEngineCommon.h/.cpp`;
- `BattleEngineCheckpointState.h/.cpp`;
- `BattleEngineQueueBoundary.h/.cpp`;
- `BattleEngineEvents.h/.cpp`;
- `BattleEngineTriggerRuntime.h/.cpp`; and
- `BattleEngineSwitchPipeline.h/.cpp`.

P0 moves only shared helper families. Every `FBattleEngine` member definition
remains in `BattleEngine.cpp` so helper linkage and include boundaries can be
validated independently.

### P1 - non-checkpoint member relocation (proposed, not approved)

Modify `BattleEngine.cpp` and this handoff. Create:

- `BattleEngineSnapshots.cpp`;
- `BattleEngineDecisionFlow.cpp`;
- `BattleEngineEndTurn.cpp`;
- `BattleEngineBetweenActions.cpp`; and
- `BattleEngineReplay.cpp`.

Construction, destruction, `TryCreate`, and test-fixture creation remain in
`BattleEngine.cpp`. `SubmitDecision` moves whole, including Pivot continuation.

### P2 - checkpoint member relocation (proposed, not approved)

Modify `BattleEngine.cpp` and this handoff. Create:

- `BattleEngineActionStart.cpp`;
- `BattleEngineWildActions.cpp`;
- `BattleEngineBagActions.cpp`;
- `BattleEngineVoluntarySwitch.cpp`;
- `BattleEnginePreMove.cpp`;
- `BattleEngineMoveTargets.cpp`; and
- `BattleEngineMoveEffects.cpp`.

`ExecuteCurrentBagItem` moves whole, including ordinary Bag, stale
cancellation, and Capture. Do not create separate Capture or Pivot source files
in this wave.

### G2 - structural closeout (proposed, not approved)

Update this handoff and `production/session-state/active.md` with the final live
worktree, generated report roots, exported counters, and current guide search.
Then stop for user diff review. Do not commit.

After G2, separately review any behavior cleanup or duplication reduction. A
later `BattleEffectExecutor` split is another independent task.

The first production relocation reduces individual file size, not total code
size. Actual line-count reduction belongs to a later, separately reviewed
cleanup.

## Validation scope and evidence

G1B requires only a read-only reference and diff verification.

T1 requires:

1. The forced-Unity editor build with `-ForceUnity`,
   `-DisableAdaptiveUnity`, `-BytesPerUnityCPP=1`, and `-NoUBA`.
2. `PokemonSolarus.Battle.ADR0002` with exactly 110 successes.
3. Full `PokemonSolarus.Battle` with exactly 320 successes.
4. An exact full-test-path set comparison with the accepted reports so equal
   counts cannot hide a missing or duplicate registration.

T1 passes all four requirements. The first compile exposed one support
dependency: the generic held-item comparator was needed by switch support. It
was moved from move-checkpoint support to common support, and no Automation was
run until the corrected final build passed. Final evidence is:

- the forced-Unity editor build passed with all four required flags;
- `Game/Saved/AutomationReports/BattleStructural-T1-20260827-185852/01-ADR0002/report/index.json`
  reports exactly 110 successes and zero succeeded-with-warnings, failures,
  not-run, or in-process tests;
- `Game/Saved/AutomationReports/BattleStructural-T1-20260827-185852/02-Battle/report/index.json`
  reports exactly 320 successes and the same zero counters;
- every entry in both reports has zero warnings and errors, every path uses the
  exact intended prefix, and neither report contains a duplicate path; and
- the full sorted path-set SHA-256 is
  `693e6aff39767ae1e8c771ba9b896836fd360db1f127128e24e82ded2c58e25d`,
  identical to the accepted 320-test report. Direct set comparison also found
  zero missing and zero extra paths.

P0, P1, and P2 each move shared `BattleEngine` code and therefore each require
the forced-Unity build plus this exact serial 22-filter matrix:

`ADR0002`, `C03A`, `C03B`, `C04A`, `C04B`, `C05A`, `C05B`, `C05C`,
`C06A`, `C06B`, `C07A`, `C07B`, `C07C`, `C07D`, `C08A`, `C08B`,
`C08C`, `C09A`, `C09B`, `C09C`, `Runtime`, and full `Battle`, all under
the `PokemonSolarus.Battle` prefix.

The accepted per-filter counts must remain unchanged, including 606 successes
in aggregate and 320 for full Battle. Every exported `index.json` must have
zero succeeded-with-warnings, failed, not-run, and in-process counters; every
test entry must have zero warnings and errors. Use a fresh unique generated
root under `Game/Saved/AutomationReports/BattleStructural-<wave>-<timestamp>`.
Process exit code alone is not acceptance evidence.

After T1, P0, P1, P2, and G2, rerun the guide-reference search and verify that
active guidance reflects current versus proposed files accurately. No
Blueprint lifecycle or actual-size PIE claim is part of this plain-C++
structural task.

## Exact exclusions

Unless a later approval explicitly changes this list, exclude:

- C10A and every later roadmap implementation package;
- `BattleEffectExecutor.*`, `BattleState.*`, `BattleResolutionCommit.*`,
  `BattleFaintOutcomeResolver.*`, and `BattlePartnerFlow.*` from the first
  `BattleEngine` relocation;
- public `BattleEngine.h` or external-call contract changes;
- behavior cleanup, deduplication, generic checkpoint base classes, and new
  gameplay seams created only to make a file boundary easier;
- event order, rejection behavior, replay schema `6`, enum ordinals, RNG draws
  or commit timing, action cursor/progress ownership, and exact-once
  publication changes;
- Cry for Help, reinforcement, `CallReinforcement`, UI, Blueprint appearance,
  assets, maps, configuration, `.uproject`, module rules, and new modules;
- hand edits under `Game/Saved`, `Game/Intermediate`, or `Game/Binaries`;
  approved build/Automation tools may write their ordinary generated output;
- `docs/registry/architecture.yaml`, ADR-0003, and every part of the untracked
  ADR-0004 other than the dated G1B amendment; and
- staging, committing, pushing, branching, or rewriting Git history.

## Mechanical and validation constraints

- Preserve the current module and public API.
- Give each new source file self-contained includes; do not rely on Unity
  include bleed.
- Use unique named private namespaces such as
  `BattleEngineActionStartPrivate`. Forced Unity builds can combine multiple
  `.cpp` files and make repeated anonymous or static names collide.
- Move complete checkpoint helper families together.
- Do not change event order, replay schema, RNG consumption, state ownership,
  rejection behavior, or commit timing during relocation.
- Preserve unrelated dirty files and user-owned visual assets.
- Do not stage, commit, push, branch, or rewrite Git history without explicit
  permission.
- Run only the approved validation scope. For shared `BattleEngine` movement,
  the plan should include the relevant affected filters and the full Battle
  gate when required.
- Judge Unreal Automation using exported `index.json` counters.
- Re-run the active-guide reference search after each wave.

## Completion criteria for the structural split

The structural work is complete only when:

- the approved focused translation units own the intended responsibilities;
- `BattleEngine.h` and external callers retain their public contract;
- one authoritative state and the atomic commit boundaries are preserved;
- no `.cpp` file includes another `.cpp` file;
- forced Unity compilation succeeds under the approved configuration;
- approved Automation filters have clean exported counters;
- every active guide points to the new structure or uses accurate path-neutral
  wording;
- historical evidence remains historically accurate;
- no unrelated or generated files were hand-edited; and
- the user has reviewed the final diff and validation evidence.
