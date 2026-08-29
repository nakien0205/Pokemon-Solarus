# Battle Engine Structural Split Handoff

## Status

**Guide Wave G1B, atomic-test Wave T1, private-production-seam Wave P0,
non-checkpoint member-relocation Wave P1, and checkpoint member-relocation Wave
P2 were completed on 2026-08-27. The post-ADR structural delta review is
complete. G2's documentation-only closeout evidence was recorded on 2026-08-28
and accepted by the user on 2026-08-28. The Battle Engine structural split is
complete. The separately approved `BattleEffectExecutor` structural split was
implemented and passed its fresh forced-Unity build and serial 22-filter matrix
on 2026-08-28. The user separately authorized its bounded commit and push, and
the implementation was published to `origin/main` as
`504f036858bae310de8ad03ae450903ebedc2779`. A later independent migration
review found no behavior, state, RNG, ordering, public-API, compile, or link
regression. It identified one non-runtime executor helper-declaration concern,
which is documented in the executor follow-up below.**

This document records the completed read-only delta review and the live
approval boundaries. G1B authorized only this file,
`production/session-state/active.md`, and a dated prerequisite amendment in
ADR-0004. T1 was separately approved and was limited to the documented atomic
test split, this handoff, and its approved generated validation output. T1 did
not itself approve production C++, P0, C10A, or Git actions. The user later
authorized one task-specific T1 documentation sync, commit, and push. P0 was
later approved separately and was limited to the exact six private support
pairs, `BattleEngine.cpp`, this handoff, and ordinary generated validation
output. At P0 completion, every `FBattleEngine` member definition remained in
`BattleEngine.cpp`.
P0 did not itself approve P1, P2, G2, C10A, or Git actions. The user later
approved one task-specific commit and push for the exact P0 source plus this
handoff and `production/session-state/active.md`. P1 then completed under its
exact source, guide, generated-output, build, and Automation boundaries. P2 was
separately approved and has now completed in a fresh bounded implementation
session under its exact seven-source map, this handoff, and ordinary generated
validation output. P2 changed no public contract, behavior rule, test source,
module boundary, or visual asset. Its implementation session changed no Git
state. The user later authorized one task-specific P2 guide sync, commit, and
push. G2's separately approved evidence capture and two-guide update are now
implemented. The user approved the final G2 diff and evidence, then authorized
one task-specific commit and push of the exact two-guide set. C10A remains
unapproved and will come later when called by the user.

The review was intentionally limited to file size, responsibility boundaries,
and safe translation-unit decomposition. It was not an in-depth mechanics or
bug review.

## Source of truth and continuation rule

The original measurements below describe commit `c514c86` on 2026-08-26. The
completed delta review compared them with accepted production source/test
checkout `b5db3e440d7c6eb5ba6ddbcc01a92a3c9b8756c0`. The pre-T1 baseline HEAD and
`origin/main` were `3259405d73be5a634ce855a7d382f838eeb36ae6`; that later baseline
commit changes only ADR-0002 closeout documents. The pre-P0 baseline HEAD and
`origin/main` were `89663923ef0d101868aa3e016847901c69db4924`, including the
validated T1 split. Live production source now differs from the accepted
production checkout only by P0's behavior-preserving private-helper relocation,
P1's behavior-preserving non-checkpoint member relocation, and P2's
behavior-preserving checkpoint member relocation, plus the separately approved
and published `BattleEffectExecutor` structural split. Live tests differ only
by T1's validated translation-unit split.

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
   contains 22 readable exported `index.json` files and 606 executions across
   overlapping filters.
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
- At that pre-T1 review, `BattleEffectExecutor` still used one staged
  `FStateExecutionContext` and one owned `FBattleEffectExecutionPlan`, and its
  split remained later work. The completed split is recorded below.
- The worktree paths to preserve are modified
  `docs/registry/architecture.yaml`, untracked ADR-0003, and the pre-existing
  untracked ADR-0004 except for its explicitly approved G1B amendment.

Do not repeat the completed logic review or rerun old exploratory suites merely
to rediscover this plan.

## Mandatory guide-reference migration

As of 2026-08-27, the canonical architecture-document directory is
`docs/registry/architecture/`. Current guides, examples, and project-local
skills use that path. The two dated ADR-0002 gate reports retain
`docs/architecture/` only as historical evidence of their reviewed checkout.

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
| `docs/registry/architecture/adr-0004-production-action-orchestration-and-observer-safe-resolution-projection.md` | Historical authoring context says stale Bag cancellation remains open | Preserve the paragraph and add a dated accepted-gate amendment | Amend in G1B |
| `docs/registry/architecture/adr-0002-battle-encounter-runtime-authority-and-atomic-resolution-commit.md` | Method-name responsibility and commit contracts | Method names and contracts remain unchanged | Preserve accepted decision |
| `plan/battle_mechanics/00-roadmap-index.md` | Current gate text still described ADR-0002 as blocking C10A; historical paths and package evidence also remain | Record ADR-0002 and the structural work as complete, with C10A next; preserve dated package evidence | Correct current status in post-migration reconciliation |
| `plan/battle_mechanics/11-canonical-proof-content.md` and `12-integration-and-release-gate.md` | Current status still listed the completed ADR-0002 closeout as a blocker | Mark the ADR-0002 gate satisfied; retain only the real C10 dependency chain | Correct current status in post-migration reconciliation |
| `plan/battle_mechanics/02-core-contracts-events-and-rng.md`, `04-battle-state-snapshots-and-decisions.md`, `05-actions-order-and-targeting.md`, `06-hit-damage-effects-and-outcomes.md`, `07-parties-switching-and-replacements.md`, and `10-encounters-capture-escape-and-partner.md` | Historical paths, hashes, and package ownership | Historical accepted checkouts | Preserve completion records |
| `production/gate-checks/2026-08-27-adr-0002-implementation-fail.md` and `2026-08-27-adr-0002-implementation-pass.md` | Historical paths, line locations, and report evidence | Historical reviewed checkouts | Preserve evidence |
| ADR-0003, `docs/battle-system-interview-handoff.md`, the reusable-damage quick spec, `design/ux/battle-hud.md`, roadmap `03-stats-types-moves-and-data-adapters.md`, and `plan/battle_mechanics/reference/modern-rules-snapshot.md` | Responsibility-only or gameplay references without current file-location assumptions | Existing path-neutral wording | Preserve; no migration needed |
| `Game/Saved/AutomationReports/**`, logs, and generated Unreal output | Generated evidence | Generated evidence only | Never hand-edit |

No stale Battle source-location assumption was found in `AGENTS.md`,
`CLAUDE.md`, `UE.md`, or `.codex/skills/`. The later post-migration review did
find that 20 local skill guides still used the removed
`docs/architecture/` directory; the documentation reconciliation replaced
those paths with `docs/registry/architecture/`.

The post-T1 discovery pass repeated the filename and responsibility search.
References to the removed monolith now remain only in this handoff's historical
measurements and completed-T1 record, plus the two historical ADR-0002 gate
reports. Those gate reports remain unchanged as historical evidence. No other
active guide assumes that the monolith still exists, and all method-name
references outside this handoff remain path-neutral. After a separate explicit
documentation approval, `production/session-state/active.md` was refreshed to
record T1's completed layout and validation while keeping P0, P1, and P2
unapproved. The historical gate reports remain unchanged.

The post-P0 discovery pass repeated the required filename and responsibility
search across the root instructions, `.codex/skills/`, `docs/`, `production/`,
`plan/`, `design/`, and the other repository text files. It found the same 13
guide/evidence files classified above. The six new private support names occur
only in this handoff. Existing member-method references remain path-neutral or
historical and remain accurate because P0 moved no member definition out of
`BattleEngine.cpp`. No stale file-location assumption was found.

The later approval-sync pass updates `production/session-state/active.md` to
record P0 as complete and P1 as approved but not started. The repeated search
still finds only the same 13 classified guide/evidence files. This handoff and
the active session-state guide are the only live structural authorities that
need status changes. The accepted ADR, roadmap completion records, and gate
reports remain accurate historical or path-neutral evidence and must not be
rewritten. No new P1 planning document should be created: this handoff already
owns the exact file map, exclusions, validation matrix, and continuation rule.

The post-P1 discovery pass repeated the required filename and responsibility
search across the root instructions, `.codex/skills/`, `docs/`, `production/`,
`plan/`, `design/`, and the other repository text files. It found the same 13
classified guide/evidence files. The five P1 source names occur only in this
handoff and the active session-state guide. The accepted ADRs remain
path-neutral, while roadmap completion records and gate reports remain truthful
historical evidence. No stale live file-location assumption was found, so only
this handoff and `production/session-state/active.md` received P1 status updates.

The P2 approval-sync pass repeated that search before the P1 commit. It still
finds the same 13 classified guide/evidence files. Accepted ADR method contracts
remain path-neutral, and roadmap hashes plus gate-report paths remain historical
evidence. Only this handoff and `production/session-state/active.md` require the
live P2 approval status. No P2 source exists, so no guide may describe a P2 file
as live.

The post-P2 discovery pass repeated the required filename and responsibility
search across the root instructions, `.codex/skills/`, `docs/`, `production/`,
`plan/`, `design/`, and the other repository text files. It found the same 13
classified guide/evidence files. The seven P2 source names occur only in this
handoff, and they now describe live files. Accepted ADR method contracts remain
path-neutral, while roadmap completion records and gate reports remain truthful
historical evidence. The later P2 commit approval-sync updated
`production/session-state/active.md` to record P2 as complete and G2 as approved
but not started. That sync did not perform the G2 closeout review or mark G2
complete.

The G2 closeout repeated the same search on 2026-08-28, excluding generated
Unreal output. It again found exactly the 13 guide/evidence files classified in
the table above. This handoff and `production/session-state/active.md` are the
only current structural authorities. The accepted ADR-0002 method references
remain path-neutral. ADR-0004's older stale-Bag paragraph remains explicitly
historical beneath its dated prerequisite amendment. The seven roadmap files
and two ADR-0002 gate reports remain truthful historical evidence. No match was
found in `AGENTS.md`, `CLAUDE.md`, `UE.md`, or `.codex/skills/`, and no stale
live file-location assumption remains.

That G2 conclusion was accurate for the searched Battle source locations, but
it was too broad as a statement about all current documentation. The later
independent migration review also checked roadmap gate state and the canonical
architecture-document directory. It found and corrected the stale current
status in the global roadmap, C10, and C11; the omitted executor split in this
document's source-of-truth paragraph; and 76 retired architecture-directory
references across 20 local project skills. This post-migration result supersedes
the broader no-stale-document interpretation without rewriting the truthful G2
file-location history.

### Post-migration documentation reconciliation — 2026-08-28

The reconciliation updated the six live authorities that could otherwise
misdirect continuation: this handoff, `production/session-state/active.md`, the
implemented executor guide, the global roadmap, C10, and C11. It also replaced
76 retired architecture-directory references across 20 local project skills.
Only `.codex/skills/architecture-decision/SKILL.md` and
`.codex/skills/create-architecture/SKILL.md` are tracked; the other 18 are
intentionally ignored local tooling. The reconciliation changed no production
source, test, generated evidence, ADR content, registry content, or Git state.
Historical package hashes, completion snapshots, and the two dated ADR-0002
gate reports remain unchanged.

The exact approved G1B guide write set is:

- `docs/battle-engine-structural-split-handoff.md`;
- `production/session-state/active.md`; and
- only the dated prerequisite amendment in the pre-existing untracked
  `docs/registry/architecture/adr-0004-production-action-orchestration-and-observer-safe-resolution-projection.md`.

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

## Staged `BattleEngine` translation units

The approved structural plan uses behavior-preserving relocation. Each boundary
becomes live only in its separately approved wave:

| File | Responsibility |
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

P1 makes `BattleEngineSnapshots.cpp`, `BattleEngineDecisionFlow.cpp`,
`BattleEngineEndTurn.cpp`, `BattleEngineBetweenActions.cpp`, and
`BattleEngineReplay.cpp` live. P2 makes the seven checkpoint sources listed
above live. `BattleEngine.cpp` now owns only construction, creation, and
test-fixture creation.

Do not create `BattleEngineCaptureCheckpoint.cpp` in the first mechanical pass:
Bag and Capture are branches of the same public method, and Capture currently
reuses the Wild cleanup staging family. Do not create
`BattleEnginePivotSwitch.cpp` in that pass either: Pivot continuation is inside
`SubmitDecision` and moves with that complete method. Separating either branch
requires a later approved internal seam, not relocation disguised as a split.

A class method can be defined in any of these `.cpp` files by including
`BattleEngine.h`; `BattleEngine.cpp` does not import implementations from them.

## P0 private shared support (completed 2026-08-27)

Only promote a helper when at least two focused translation units need it.
Otherwise keep it local to its checkpoint file.

The completed P0 relocation uses exactly these shared seams:

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

The completed line counts are:

| Private support pair | Header | Source |
|---|---:|---:|
| `BattleEngineCommon` | 392 | 429 |
| `BattleEngineCheckpointState` | 379 | 418 |
| `BattleEngineQueueBoundary` | 465 | 1,252 |
| `BattleEngineEvents` | 721 | 338 |
| `BattleEngineTriggerRuntime` | 1,404 | 312 |
| `BattleEngineSwitchPipeline` | 1,348 | 61 |

`BattleEngine.cpp` is 13,035 lines after P0. All 28 `FBattleEngine` member
definitions remain there. No P1 member-relocation source exists, and no `.cpp`
file includes another `.cpp` file.

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

**2026-08-29 live-surface note:** the split-era header hash, line counts, and
"byte-for-byte unchanged" constraint below are preserved as 2026-08-28
historical split evidence. Separately approved R4A and R4B mechanics work later
changed the existing executor header, context, condition, and damage owners
within their established responsibilities. R4B also replaced the private
boolean charge hook with
`TryShouldSkipEffectDescriptor(Effect, OutShouldSkip)` and added the focused
`BattleMoveWeatherRules.h/.cpp` seam. It did not add another context, state,
identity recheck, RNG owner, commit, or publication path. Current source and the
C10A blocker register override the split-era measurements below.

The user approved the bounded implementation guide at
`docs/battle-effect-executor-split-implementation-ready-draft.md` on
2026-08-28. The approved split is implemented and freshly validated. This
records implementation completion and the later independent migration-review
result. The user's later commit-and-push instruction was task-specific and does
not authorize any broader Git action.

Keep `BattleEffectExecutor.h` byte-for-byte unchanged, keep the complete
ordered `FBattleEffectExecutor::TryExecute` coordinator in
`BattleEffectExecutor.cpp`, and keep exactly one staged
`BattleEffectExecutorPrivate::FStateExecutionContext`, one staged state, and
the existing outer commit/publication path.

`BattleEffectExecutor.h` remains at 256 physical lines and retains its recorded
SHA-256
`0dadca0d3da930f2f5cf2a8548016e20ec70a1252f090bd452dbf16dc258e989`.
The retained coordinator source is 2,118 physical lines. The completed focused
file map and actual line counts are:

| File | Lines | Responsibility |
|---|---:|---|
| `BattleEffectExecutorContext.h` | 429 | The single private context declaration, unchanged method declarations, and existing data members in their current order |
| `BattleEffectExecutorState.cpp` | 471 | Context construction, validation, shared lookup, plan materialization, and direct-state plan application |
| `BattleEffectExecutorDamage.cpp` | 809 | Hit gates, accuracy, critical, damage input, and HP mutation |
| `BattleEffectExecutorConditions.cpp` | 1,726 | Statuses, stat stages, volatiles, field conditions, and side conditions |
| `BattleEffectExecutorAbilityItems.cpp` | 962 | Ability/item evaluation, activation, reveal, ledger mutation, and immediate updates |
| `BattleEffectExecutorSwitching.cpp` | 623 | Forced switching, switch-out status handling, and entry hazards |
| `BattleEffectExecutorTriggers.cpp` | 1,005 | Trigger registration, dispatch, suppression, update, and cleanup |

All six focused sources define methods on the same context. None may create an
independent state copy, RNG owner, commit seam, or publication path. The
approved wave excludes mechanics changes, cleanup, C10A, public headers, tests,
other production sources, and Git actions.

The final source audit found all 102 context definitions exactly once in the
approved `22 / 8 / 27 / 16 / 3 / 26` ownership split and all nine executor
definitions exactly once. It also found one context construction, no independent
state/context copy or commit seam, no `.cpp` include, no anonymous namespace,
and no new file-local helper definition. Each focused source is self-contained,
and the forced-Unity build passed. `BattleEngineMoveEffects.cpp` remains
unchanged, preserving the existing identity recheck, transactional-RNG commit,
one delta application, and one successful `PublishPrepared` path.

The later migration review confirmed that behavior-preserving result and found
one structural deviation from the approved helper-ownership plan. Three focused
sources manually redeclare six helpers whose definitions remain in
`BattleEffectExecutor.cpp`: four declarations in
`BattleEffectExecutorState.cpp`, `MakeRuleId` in
`BattleEffectExecutorDamage.cpp`, and `TryAddRandomEvent` in
`BattleEffectExecutorConditions.cpp`. Their signatures match and the build
links successfully, so no present runtime or ODR failure was found. The manual
declarations remain a documented dependency-clarity concern until a separately
approved source change restores local ownership or introduces one explicit
private declaration header.

The executor build used `-BytesPerUnityCPP=1`, so each generated Unity wrapper
contained one project source. It proves independent translation-unit compilation
and linking, not ordinary multi-source Unity co-location. The source audit found
no concrete anonymous-namespace or duplicate-helper collision.

Fresh validation passed:

- forced-Unity build log:
  `Game/Saved/Logs/BattleEffectExecutorSplit-20260828-091241-ForcedUnityBuild.log`;
- serial 22-filter evidence root:
  `Game/Saved/AutomationReports/BattleEffectExecutorSplit-20260828-091837`;
- 286 focused executions plus 320 full-Battle executions, for 606 executions
  across overlapping filters and 320 unique full-Battle paths;
- zero aggregate warnings, failures, not-run, or in-process tests and zero
  per-test warnings or errors; and
- full sorted 320-path SHA-256
  `693e6aff39767ae1e8c771ba9b896836fd360db1f127128e24e82ded2c58e25d`.

## Approval-bounded write sets and sequence

Use separate changes rather than one broad refactor. G1B, T1, P0, P1, and P2
have been approved and completed. G2's evidence capture and guide updates are
implemented and accepted.

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

### P0 - private production seams (approved and completed 2026-08-27)

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

P0 completed exactly this implementation write set. It changed no public
header, member-method location, test source, behavior contract, module boundary,
or visual asset. Before the later task-specific approval, it changed no Git
state. Its only additional writes are ordinary generated build and Automation
outputs under `Game/Saved`; the later approval sync adds
`production/session-state/active.md` to the exact commit set.

### P1 - non-checkpoint member relocation (approved and completed 2026-08-27)

P1 modified `BattleEngine.cpp`, this handoff, and
`production/session-state/active.md`. It created:

- `BattleEngineSnapshots.cpp`;
- `BattleEngineDecisionFlow.cpp`;
- `BattleEngineEndTurn.cpp`;
- `BattleEngineBetweenActions.cpp`; and
- `BattleEngineReplay.cpp`.

Construction, destruction, `TryCreate`, and test-fixture creation remain in
`BattleEngine.cpp`. `SubmitDecision` moves whole, including Pivot continuation.
The seven checkpoint members reserved for P2 also remain there. The final
physical line counts are `BattleEngine.cpp=8,990`,
`BattleEngineSnapshots.cpp=600`, `BattleEngineDecisionFlow.cpp=1,743`,
`BattleEngineEndTurn.cpp=1,642`, `BattleEngineBetweenActions.cpp=157`, and
`BattleEngineReplay.cpp=79`. The main file owns 12 member definitions and the
five P1 files own 16, preserving all 28 P0 definitions exactly once.

P1 completed this exact mechanical relocation, the two active-guide updates,
ordinary generated validation output, and the required build/matrix. It changed
no public header, test source, behavior contract, module boundary, or visual
asset. P1 did not itself authorize P2, behavior cleanup, public-contract changes,
C10A, or Git actions. P2 was later approved and completed separately; it remains
excluded from the earlier P1 commit.

### P2 - checkpoint member relocation (approved and completed 2026-08-27)

Modify `BattleEngine.cpp` and this handoff. Create:

- `BattleEngineActionStart.cpp`;
- `BattleEngineWildActions.cpp`;
- `BattleEngineBagActions.cpp`;
- `BattleEngineVoluntarySwitch.cpp`;
- `BattleEnginePreMove.cpp`;
- `BattleEngineMoveTargets.cpp`; and
- `BattleEngineMoveEffects.cpp`.

P2 modified `BattleEngine.cpp` and this handoff, created the seven files above,
and wrote only ordinary generated build and Automation output outside that
hand-edited set. The final physical line counts are
`BattleEngine.cpp=129`, `BattleEngineActionStart.cpp=1,303`,
`BattleEngineWildActions.cpp=750`, `BattleEngineBagActions.cpp=2,041`,
`BattleEngineVoluntarySwitch.cpp=633`, `BattleEnginePreMove.cpp=1,652`,
`BattleEngineMoveTargets.cpp=1,234`, and
`BattleEngineMoveEffects.cpp=1,521`.

The main file owns five construction/creation definitions, the five P1 files
own 16 definitions, and the seven P2 files own one checkpoint definition each.
All 28 P0 definitions still exist exactly once. Shared comparison helpers use
explicit private namespace declarations between the approved P2 sources; P2
created no header or new support seam. Every source has self-contained includes
and a unique named private namespace, and no `.cpp` includes another `.cpp`.

`ExecuteCurrentBagItem` moves whole, including ordinary Bag, stale
cancellation, and Capture. Do not create separate Capture or Pivot source files
in this wave.

### G2 - structural closeout (implemented and accepted 2026-08-28)

G2 changed only this handoff and `production/session-state/active.md`. It made
no production-source, test, generated-output, visual-asset, configuration, or
Git change.

The read-only live-layout pass found all 25 expected P0, P1, and P2 structural
files with the exact recorded line counts. The 13 facade-member sources contain
all 28 `FBattleEngine` definitions exactly once: five in `BattleEngine.cpp`, 16
across the five P1 sources, and seven across the seven P2 sources. No `.cpp`
file includes another `.cpp` file. This was a direct filesystem inspection;
G2 ran no Git status or diff and makes no clean-worktree claim.

The preserved generated evidence remains readable:

- `Game/Saved/AutomationReports/BattleStructural-T1-20260827-185852` contains
  two exported reports and 430 executions: 110 ADR-0002 plus 320 full Battle;
- `Game/Saved/AutomationReports/BattleStructural-P0-20260827-200412`,
  `BattleStructural-P1-20260827-212839`, and
  `BattleStructural-P2-20260827-225953` each contain 22 exported reports and
  606 executions across overlapping filters, including 320 full Battle; and
- across all 68 `index.json` files, every aggregate issue counter and per-test
  warning/error counter is zero, every test state is `Success`, and no report
  contains a duplicate path. All 22 P2 path sets exactly match P1.

The existing P2 build log at
`Game/Saved/Logs/BattleStructural-P2-20260827-225012-ForcedUnityBuild.log`
records `-ForceUnity`, `-DisableAdaptiveUnity`, `-BytesPerUnityCPP=1`, and
`-NoUBA`, followed by `Result: Succeeded`. G2 did not rerun the build or
Automation because its approved scope is evidence inspection and guide
closeout only. This VERIFY is not a fresh build or test run and does not prove
byte-for-byte source identity.

The fresh guide search is recorded in the mandatory migration section above.
The user reviewed and approved the final G2 diff and evidence on 2026-08-28.
That approval completes the structural split. Cleanup, C10A work, and the later
`BattleEffectExecutor` split remain outside G2.

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

The accepted per-filter counts must remain unchanged, including 606 executions
in aggregate across overlapping filters and 320 unique paths for full Battle.
Every exported `index.json` must have
zero succeeded-with-warnings, failed, not-run, and in-process counters; every
test entry must have zero warnings and errors. Use a fresh unique generated
root under `Game/Saved/AutomationReports/BattleStructural-<wave>-<timestamp>`.
Process exit code alone is not acceptance evidence.

P0 passes the required build and matrix. The initial forced-Unity build exposed
only private seam compilation defects: two missing private type includes, one
malformed extracted optional-parameter signature, and one missing private
namespace import. They were corrected without moving a member method or changing
behavior, and no Automation ran before the corrected build passed. Evidence is:

- the corrected forced-Unity editor build passed with all four required flags;
- its build log is
  `Game/Saved/Logs/BattleStructural-P0-20260827-200221-ForcedUnityBuild.log`;
- the 22 filters ran one at a time in the required order under
  `Game/Saved/AutomationReports/BattleStructural-P0-20260827-200412`;
- the exact per-filter successes were `ADR0002=110`, `C03A=6`, `C03B=6`,
  `C04A=7`, `C04B=9`, `C05A=8`, `C05B=9`, `C05C=7`, `C06A=7`, `C06B=8`,
  `C07A=7`, `C07B=9`, `C07C=8`, `C07D=9`, `C08A=7`, `C08B=20`, `C08C=27`,
  `C09A=6`, `C09B=7`, `C09C=6`, `Runtime=3`, and full `Battle=320`;
- the 22 reports contain 606 executions in aggregate; and
- every exported aggregate warning, failure, not-run, and in-process counter is
  zero, every test entry has zero warnings and errors, and every test state is
  `Success`.

P1 passes the required build and matrix. The initial forced-Unity build exposed
one mechanical ownership defect: `FWildActionStateDelta` had moved with the
decision helpers while its Wild-action helpers remained in `BattleEngine.cpp`.
The declaration was restored beside those helpers. The 88 compiler diagnostics
were one cascade from that missing type, and no P1 source reported a separate
error. No Automation ran before the corrected build passed. Evidence is:

- the corrected forced-Unity editor build passed with all four required flags;
- its build log is
  `Game/Saved/Logs/BattleStructural-P1-20260827-212714-ForcedUnityBuild.log`;
- the 22 filters ran one at a time in the required order under
  `Game/Saved/AutomationReports/BattleStructural-P1-20260827-212839`;
- the exact per-filter successes were `ADR0002=110`, `C03A=6`, `C03B=6`,
  `C04A=7`, `C04B=9`, `C05A=8`, `C05B=9`, `C05C=7`, `C06A=7`, `C06B=8`,
  `C07A=7`, `C07B=9`, `C07C=8`, `C07D=9`, `C08A=7`, `C08B=20`, `C08C=27`,
  `C09A=6`, `C09B=7`, `C09C=6`, `Runtime=3`, and full `Battle=320`;
- the 22 reports contain 606 executions in aggregate; and
- every exported aggregate issue counter is zero, every test entry has zero
  warnings and errors and state `Success`, every report contains exact-prefix
  unique paths, and every report path set exactly matches P0's accepted matrix.

P2 passes the required build and matrix without a correction wave. Evidence is:

- the forced-Unity editor build passed on its first run with all four required
  flags;
- its build log is
  `Game/Saved/Logs/BattleStructural-P2-20260827-225012-ForcedUnityBuild.log`;
- the 22 filters ran one at a time in the required order under
  `Game/Saved/AutomationReports/BattleStructural-P2-20260827-225953`;
- the exact per-filter successes were `ADR0002=110`, `C03A=6`, `C03B=6`,
  `C04A=7`, `C04B=9`, `C05A=8`, `C05B=9`, `C05C=7`, `C06A=7`, `C06B=8`,
  `C07A=7`, `C07B=9`, `C07C=8`, `C07D=9`, `C08A=7`, `C08B=20`, `C08C=27`,
  `C09A=6`, `C09B=7`, `C09C=6`, `Runtime=3`, and full `Battle=320`;
- the 22 reports contain 606 executions in aggregate; and
- every exported aggregate issue counter is zero, every test entry has zero
  warnings and errors and state `Success`, every report contains exact-prefix
  unique paths, and every report path set exactly matches P1's accepted matrix.
  The full sorted path-set SHA-256 remains
  `693e6aff39767ae1e8c771ba9b896836fd360db1f127128e24e82ded2c58e25d`.

G2 reran the guide-reference search and verified that active guidance reflects
current versus proposed files accurately. No Blueprint lifecycle or actual-size
PIE claim is part of this plain-C++ structural task.

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
- `docs/registry/architecture.yaml`, ADR-0003, and every part of ADR-0004 other
  than the dated G1B amendment; and
- staging, committing, pushing, branching, or rewriting Git history, except for
  the one task-specific G2 commit and push authorized after final review. That
  exception contains exactly this handoff and
  `production/session-state/active.md`.

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

All Battle Engine criteria above were satisfied when the user approved G2 on
2026-08-28. The Battle Engine split and the mechanically behavior-preserving
executor relocation are complete. The executor helper-declaration seam remains
an explicitly documented non-runtime follow-up; this document does not claim
that source concern has been remediated.
