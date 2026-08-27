# Battle Engine Structural Split Handoff

## Status

**Deferred until the remaining ADR-0002 stale accepted Bag-cancellation remediation is implemented and validated.**

This document records a read-only structural review. It is not approval to
change production code, tests, guides, Git state, or generated Unreal output.
The future split still requires an exact write set, exclusions, validation
scope, draft, and user approval.

The review was intentionally limited to file size, responsibility boundaries,
and safe translation-unit decomposition. It was not an in-depth mechanics or
bug review.

## Source of truth and continuation rule

The measurements below describe commit `c514c86` on 2026-08-26. They are a
snapshot, not a permanent contract. Live source, the current ADR-0002 gate,
the worktree, the current roadmap package, and exported Unreal Automation
reports override this document.

Do not begin the split directly from this snapshot. First perform the bounded
delta review described below. A new full structural review is unnecessary
unless the remaining ADR-0002 work changes the ownership or transaction
boundaries materially.

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
reviewed snapshot. It is not a fresh logic-correctness verdict and does not
prove the later final ADR-0002 gate.

At current HEAD `f48146f4f439930ed06f5f7feaf957514bcc4408`, target resolution
and move-effect checkpoint remediation are present, and the final 22-report
matrix is green. The implementation gate still fails because accepted stale
Bag cancellation mutates live action state before fallible post-action boundary
and resolution preparation completes. The current gate report, not the older
snapshot list above, controls continuation.

## ADR-0002 completion gate

Do not start structural changes until all of the following are true:

1. Target-resolution and move-effect checkpoint remediation remains complete in
   live source.
2. Accepted stale Bag cancellation is staged through fallible boundary,
   invariant, and resolution preparation and has explicit failure proof.
3. Required affected package filters pass.
4. The full `PokemonSolarus.Battle` filter passes when required by the live
   ADR and approved validation scope.
5. Acceptance is judged from exported `index.json` counters, not process exit
   code alone.
6. The final accepted commit or checkout and all dirty paths are recorded.

## Required delta review after ADR-0002

Run one narrow, read-only structural delta review before drafting edits. It
must:

1. Compare the accepted ADR-0002 checkout with `c514c86` for the files and
   symbols named in this document.
2. Recount the main production and atomic-test file sizes.
3. Remap helper ownership added by target resolution, move effects, faint
   continuation, or final action completion.
4. Check whether any proposed file would create a circular private dependency
   or split one atomic transaction across independent state copies.
5. Inspect the final exported ADR-0002 reports and record their counters.
6. Recheck the worktree and preserve all unrelated changes.
7. Report whether this handoff is still usable, needs a small revision, or
   requires a new structural review.

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

Before any C++ edit, produce an old-to-new reference table containing:

| Guide file | Old reference or assumption | Proposed new reference | Action |
|---|---|---|---|
| `<path>` | `<old file, line, or responsibility>` | `<new file or path-neutral wording>` | Update, preserve, or add an amendment |

Classify every match:

- **Active or normative guide:** update it to the new split paths or use
  path-neutral responsibility wording.
- **Historical decision, accepted report, or evidence record:** do not rewrite
  history as though the new files existed at that time. Preserve it, or add a
  clearly dated amendment only when readers could otherwise treat it as
  current guidance.
- **Generated output, log, or Unreal report:** do not hand-edit it.
- **Unrelated mention:** record why no update is required.

The exact guide write set and exceptions must be shown to the user and approved
with the source write set. Draft the guide rewrites before production edits.
Apply approved active-guide updates in the same bounded structural change as
the corresponding file moves, so the final worktree never leaves current
guides pointing at removed code. Do not commit an intermediate state in which
guides describe files that do not yet exist.

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
| `BattleEngine.cpp` | Constructor, destructor, and `TryCreate` only |
| `BattleEngineSnapshots.cpp` | Snapshot projection, filtering, and read-only getters |
| `BattleEngineDecisionFlow.cpp` | Decision requests, batches, and ordinary submission flow |
| `BattleEngineActionStart.cpp` | Atomic action-start staging and `BeginNextLockedAction` |
| `BattleEngineWildActions.cpp` | Atomic Run and WildFlee execution |
| `BattleEngineBagActions.cpp` | Shared Bag preflight and non-Capture Bag checkpoint |
| `BattleEngineCaptureCheckpoint.cpp` | Capture staging, delta, and Capture branch |
| `BattleEngineVoluntarySwitch.cpp` | Voluntary-switch identity, staging, and execution |
| `BattleEnginePivotSwitch.cpp` | Pivot identity and continuation currently embedded in decision submission |
| `BattleEnginePreMove.cpp` | Pre-move identity, gates, PP staging, and commit |
| `BattleEngineMoveTargets.cpp` | Atomic target resolution |
| `BattleEngineMoveEffects.cpp` | Atomic move-effect resolution coordinator |
| `BattleEngineEndTurn.cpp` | End-turn resolution |
| `BattleEngineReplay.cpp` | Replay export methods |

The final names and exact boundaries must be confirmed by the post-ADR delta
review. A class method can be defined in any of these `.cpp` files by including
`BattleEngine.h`; `BattleEngine.cpp` does not import implementations from them.

## Proposed private shared support

Only promote a helper when at least two focused translation units need it.
Otherwise keep it local to its checkpoint file.

Likely shared seams after the delta review are:

- `BattleEngineCheckpointState.*` for shared exact identities and staged
  projection/delta support;
- `BattleEngineQueueBoundary.*` for pure queue-boundary and replacement
  planning;
- `BattleEngineEvents.*` for common event construction;
- `BattleEngineTriggerRuntime.*` for trigger dispatch and cleanup; and
- `BattleEngineSwitchPipeline.*` for switch application, entry hazards,
  immediate held items, and entry abilities.

Do not replace all checkpoints with one generic base class. Their stale
identities and owned deltas differ, and the explicit code is part of their
auditability.

## Atomic test split

Split `BattleAtomicCheckpointTests.cpp` as a separate approved task, not mixed
with production relocation:

- `BattleAtomicWildActionTests.cpp`;
- `BattleAtomicBagActionTests.cpp`;
- `BattleAtomicCaptureTests.cpp`;
- `BattleAtomicActionStartTests.cpp`;
- `BattleAtomicVoluntarySwitchTests.cpp`;
- `BattleAtomicPivotSwitchTests.cpp`; and
- `BattleAtomicPreMoveTests.cpp`.

Partition the now-existing target-resolution and move-effect checkpoint tests
according to the accepted ADR-0002 implementation. Include the stale accepted
Bag-cancellation failure proof in the Bag or checkpoint partition. Keep generic
setup, catalog, event observation, and fault-injection support in a small
private test support seam. Do not replace one large test source with one large
support header.

## `BattleEffectExecutor` follow-up

`BattleEffectExecutor.cpp` is the second production priority. Its atomic
move-effect and outcome staging must remain stable through the final ADR-0002
gate before it is split.

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

## Implementation sequence

Use separate, approval-bounded changes rather than one broad refactor:

1. Complete the remaining ADR-0002 implementation remediation and pass its
   final gate; the ADR design is already Accepted.
2. Run the narrow structural delta review.
3. Complete the mandatory guide-reference inventory and old-to-new mapping.
4. Present exact production, test, guide, and support-file write sets, plus
   exclusions and validation, and wait for approval.
5. Split the atomic test source as its own mechanical task if the user chooses
   that first.
6. Relocate `BattleEngine` code without behavioral cleanup, deduplication, or
   contract changes.
7. Validate the relocation and all active guide references.
8. In a later task, reduce proven duplication such as repeated queue-boundary
   planning, rejection publication, trigger cleanup, and shared switch
   projection.
9. Split `BattleEffectExecutor` only after its atomic boundary is stable.

The first production relocation reduces individual file size, not total code
size. Actual line-count reduction belongs to a later, separately reviewed
cleanup.

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
