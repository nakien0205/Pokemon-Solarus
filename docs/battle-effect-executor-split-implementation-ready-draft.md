# BattleEffectExecutor Split — Temporary Implementation-Ready Guide

**State:** `IMPLEMENTED`
**Approved by:** User
**Approval date:** 2026-08-28
**Implementation date:** 2026-08-28
**Validation:** `PASS`
**Purpose:** Preserved contract and evidence for the completed bounded implementation
**Authority:** Records the approved structural draft; it does not replace the
live authorities listed below

This guide stores the executor-split draft that was approved in the preceding
read-only review and subsequently implemented in a fresh session. The
pre-implementation hashes and line locations below remain the truthful baseline
for that approved relocation; they are not claims about the current split
layout. Final layout and validation evidence are recorded at the end.

Do not implement the split in the review session that created this guide. A
fresh implementation session must recheck the live baseline before editing. If
an in-scope authority or executor source differs from this guide, stop and show
the conflict. Do not infer a revised plan.

This implementation approval did not authorize a commit, stage, push, branch,
Git-history change, C10A work, cleanup, deduplication, or a mechanics change.
After implementation and validation passed, the user separately authorized one
bounded commit and push of the completed task paths.

## Workspace and required read order

Work in `D:\Python\Projects\Pokemon Solarus`.

Read completely and in this order:

1. `production/session-state/active.md` — current accepted state and scope
   boundaries.
2. `docs/battle-engine-structural-split-handoff.md` — governing structural
   plan, especially `BattleEffectExecutor follow-up`.
3. `docs/registry/architecture/adr-0002-battle-encounter-runtime-authority-and-atomic-resolution-commit.md`
   — accepted atomicity contract.
4. `docs/battle-effect-executor-split-implementation-ready-draft.md` — this
   approved implementation guide.
5. `Game/Source/PokemonSolarus/Private/Battle/BattleEffectExecutor.h` — private
   executor contract that must remain unchanged.
6. `Game/Source/PokemonSolarus/Private/Battle/BattleEffectExecutor.cpp` — live
   implementation and relocation source.
7. `Game/Source/PokemonSolarus/Private/Battle/BattleEngineMoveEffects.cpp` —
   read-only caller evidence for the outer atomic commit and publication path.

## Single objective

Perform a behavior-preserving structural split of `BattleEffectExecutor.cpp`
using one private context declaration and six focused implementation sources.
Preserve exactly one staged `FStateExecutionContext`, one staged state, and one
final outer commit/publication path.

Do not begin C10A or any later work.

## Verified pre-implementation baseline (historical)

The approved review verified this baseline on 2026-08-28:

- HEAD: `48ca5501410bd6ec9f9aa87f17b8455aa006f0d9`.
- `BattleEffectExecutor.h`: 256 physical lines; SHA-256
  `0dadca0d3da930f2f5cf2a8548016e20ec70a1252f090bd452dbf16dc258e989`.
- `BattleEffectExecutor.cpp`: 7,642 physical lines; SHA-256
  `3c00415ed195242316377e72cbfaf3f6b602cde5bfd6128d3becc529b5ca31fb`.
- `FStateExecutionContext` starts at line 819 and ends at line 6,282. It
  contains 102 methods.
- The complete ordered `FBattleEffectExecutor::TryExecute` coordinator starts
  at line 6,789 and ends at line 7,582.
- `TryExecuteAgainstState`, `TryPrepareAgainstState`, and `ApplyPreparedPlan`
  start at lines 7,584, 7,607, and 7,629.
- The file contains exactly one context declaration, one context construction,
  one `TryExecute` definition, one `TryPrepareAgainstState` definition, and one
  `ApplyPreparedPlan` definition.
- No `.cpp` includes another `.cpp`.
- The only pre-existing working-tree entries were these two untracked,
  excluded files:
  - `docs/registry/architecture/adr-0003-pokemon-essentials-move-animation-conversion-and-battle-presentation.md`
  - `docs/registry/architecture/adr-0004-production-action-orchestration-and-observer-safe-resolution-projection.md`

The temporary guide itself is an additional user-authorized documentation
write made after that baseline. It is reference input only and is not part of
the implementation write set below. Do not modify or delete it without a
separate instruction.

At implementation startup, recheck the two executor hashes, live locations,
HEAD, working-tree inventory, and guide references. A different HEAD alone is
not automatically a conflict, but any changed in-scope authority, executor
content, relevant caller contract, or unexpected overlap is a stop condition.

## Confirmed atomic path

The approved review found no need for a broader redesign:

1. One `FStateExecutionContext` copies the staged collections once.
2. `TryPrepareAgainstState` constructs that context once and moves its
   completed state into one plan.
3. `BattleEngineMoveEffects.cpp` imports that one plan into the existing outer
   checkpoint preparation.
4. The outer checkpoint rechecks identity, commits transactional RNG, applies
   one state delta, and publishes once.

The saved P2 build and Automation reports are planning baselines only. They are
not fresh executor-split acceptance evidence and do not prove byte-for-byte
source identity.

## Contracts that must remain unchanged

- Keep `BattleEffectExecutor.h` byte-for-byte unchanged.
- Keep the complete ordered `FBattleEffectExecutor::TryExecute` coordinator.
- Keep exactly one staged `FStateExecutionContext`, one staged state, and one
  final commit.
- Every focused source must define methods on that same context. No focused
  source may create or own an independent state/context copy or commit.
- Preserve the public API, module boundary, `FBattleEngine` state ownership,
  event order, replay schema `6`, enum ordinals, RNG behavior, rejection
  behavior, commit timing, and exact-once publication.
- Do not include a `.cpp` file from another `.cpp` file.
- Give every focused source self-contained includes and safe unique named
  private namespaces under forced Unity.
- Keep the change structural. Do not review or change unrelated mechanics or
  bugs during relocation.

## Approved structural split

Create one private context declaration and six focused implementation files.
Every focused source defines methods on
`BattleEffectExecutorPrivate::FStateExecutionContext`.

| Proposed file | Current methods | Current body lines | Responsibility |
|---|---:|---:|---|
| `BattleEffectExecutorState.cpp` | 22 | 411 | Context construction, validation, shared lookup, plan materialization, direct-state commit |
| `BattleEffectExecutorDamage.cpp` | 8 | 794 | Hit gates, accuracy, critical, damage input, HP mutation |
| `BattleEffectExecutorConditions.cpp` | 27 | 1,704 | Statuses, stat stages, volatiles, field and side conditions |
| `BattleEffectExecutorAbilityItems.cpp` | 16 | 949 | Ability/item evaluation, activation, reveal, ledger mutation, immediate updates |
| `BattleEffectExecutorSwitching.cpp` | 3 | 610 | Forced switching, switch-out status handling, entry hazards |
| `BattleEffectExecutorTriggers.cpp` | 26 | 992 | Trigger registration, dispatch, suppression, update, and cleanup |

After the mechanical move, `BattleEffectExecutor.cpp` retains approximately
2,119 current lines:

- all pure request/move validation and event-building helpers currently
  outside the context;
- `TryApplyChance`, `TryApplyOrdinaryDescriptor`, and
  `TryApplyLinkedDescriptor`;
- all five rule-purpose getters; and
- the complete ordered `FBattleEffectExecutor::TryExecute`.

The approximate retained line count is planning data, not an acceptance target.
Record actual final line counts only after implementation and validation.

## Exact context-method ownership

### `BattleEffectExecutorState.cpp`

Own the context constructor, `MovePreparedState`, `BindExecutionResult`,
`PrevalidateRequest`, `IsRuntimeValid`, `GetRuntimeError`,
`IsSourceAbleToContinue`, `IsTargetAbleToContinue`, `TryBuildEventTarget`,
`Applied`, `Outcome`, all battler/active/side lookups, `GetWeatherId`,
`GetTerrainId`, `TryIsGrounded`, `ShouldIgnoreLevitateForCurrentMove`, and
`SetRuntimeFailure`.

Also move the public definitions of `TryExecuteAgainstState`,
`TryPrepareAgainstState`, and `ApplyPreparedPlan` to this source. These methods
must keep the existing one-context preparation and one direct plan-application
semantics.

### `BattleEffectExecutorDamage.cpp`

Own `CheckTryHit`, `CheckMoveImmunity`, `TryBuildAccuracyInput`,
`TryBuildCriticalInput`, `TryBuildDamageInput`, `SetDirectMoveDamageHit`,
`TryGetHp`, and `ApplyHpDelta`.

### `BattleEffectExecutorConditions.cpp`

Own `CheckReachability`, `CheckProtection`, `ApplyProtectionBreaking`,
`ShouldSkipEffectDescriptor`, `CheckEffectEligibility`, `ApplyNonHpEffect`,
`HasRoom`, `HasSideCondition`, `GetSideConditionIds`, both condition-state
constructors, volatile lookups, `TryAppendRandomDraw`,
`TryBuildVolatileSource`, `HasActedThisTurn`, `GetCurrentPP`, all applicable
`Apply*Condition/Volatile/Charge/Protect/StatStage` methods,
`SetFieldCondition`, `SetSideCondition`, and `RemoveCondition`.

### `BattleEffectExecutorAbilityItems.cpp`

Own `TryApplyPostMoveLifeOrbRecoil`, `CheckAbilityImmunity`,
`CheckItemImmunity`, `RunImmediateUpdate`, held-item ledger operations, typed
ability/item request and activation methods, item event methods, consumption,
switch-in and immediate updates, Levitate activation recording, and Magic Room
suppression.

### `BattleEffectExecutorSwitching.cpp`

Own `TryResolveForcedSwitches`, `TryApplyEntryHazards`, and
`TryRunSwitchOutStatus`.

### `BattleEffectExecutorTriggers.cpp`

Own item and Ability hook registration/dispatch/cleanup; field/side owner,
state, dispatch, registration, cleanup, and layer methods; volatile
registration, dispatch-state queries, suppression, layer, and cleanup methods;
`TryTakeTriggerContext`, `TryDispatchStatusPhase`, `DrainTriggerOutputs`, and
`TryCleanupCanonicalStatus`.

This approved partition accounts for all 102 context methods exactly once,
with zero missing or duplicate ownership. Before moving bodies, reconstruct the
live 102-method inventory and compare it with these ownership rules. Stop if the
inventory no longer partitions exactly once.

## Private context header decision

Create
`Game/Source/PokemonSolarus/Private/Battle/BattleEffectExecutorContext.h`.

It contains only:

- the single declaration of
  `BattleEffectExecutorPrivate::FStateExecutionContext`;
- constructor and staging-method declarations;
- every `IBattleEffectExecutionContext` override;
- private member-function declarations required by the six focused sources;
  and
- the existing data members in their current order, including the one
  request/state/random reference set, staged collections, transient sets/maps,
  result pointer, runtime flags, and ordinals.

It must not contain:

- a second context or state object;
- public executor types already owned by `BattleEffectExecutor.h`;
- commit logic;
- independent random ownership;
- new generic abstractions; or
- unrelated inline helpers.

Coordinator-only free helpers remain local in `BattleEffectExecutor.cpp`. The
first mechanical pass creates no new shared free-helper seam. Any unavoidable
file-local helper must use a unique named namespace such as
`BattleEffectExecutorDamagePrivate`; do not use anonymous namespaces or repeat
generic private helper names that can collide under forced Unity.

## Approved hand-edited write set (implemented)

Modify only:

- `Game/Source/PokemonSolarus/Private/Battle/BattleEffectExecutor.cpp`
- `docs/battle-engine-structural-split-handoff.md`
- `production/session-state/active.md`

Create only:

- `Game/Source/PokemonSolarus/Private/Battle/BattleEffectExecutorContext.h`
- `Game/Source/PokemonSolarus/Private/Battle/BattleEffectExecutorState.cpp`
- `Game/Source/PokemonSolarus/Private/Battle/BattleEffectExecutorDamage.cpp`
- `Game/Source/PokemonSolarus/Private/Battle/BattleEffectExecutorConditions.cpp`
- `Game/Source/PokemonSolarus/Private/Battle/BattleEffectExecutorAbilityItems.cpp`
- `Game/Source/PokemonSolarus/Private/Battle/BattleEffectExecutorSwitching.cpp`
- `Game/Source/PokemonSolarus/Private/Battle/BattleEffectExecutorTriggers.cpp`

If validation is reached in that fresh implementation session, its only
additional writes may be ordinary fresh generated output under:

- `Game/Binaries/**`
- `Game/Intermediate/**`
- uniquely named `Game/Saved/**`

Do not hand-edit generated output or overwrite existing reports and logs.

## Exact exclusions

Exclude:

- this temporary guide unless the user gives a separate instruction;
- `Game/Source/PokemonSolarus/Private/Battle/BattleEffectExecutor.h`;
- all public headers and every test source;
- `BattleEngineMoveEffects.cpp`, `BattleState.*`, `BattleResolutionCommit.*`,
  `BattleFaintOutcomeResolver.*`, and `BattlePartnerFlow.*`;
- every other production source, module rule, configuration, asset, Blueprint,
  map, `.uproject`, and visual file;
- ADR-0002, ADR-0003, ADR-0004, and
  `docs/registry/architecture.yaml`;
- C10A data and work;
- Cry for Help, reinforcement, and `CallReinforcement` behavior;
- behavior cleanup, deduplication, generic redesign, or mechanics changes;
- existing generated reports and logs;
- the two pre-existing untracked ADR files and every unrelated dirty path; and
- every Git action, including stage, commit, push, branch creation, or history
  alteration.

## Guide inventory and approved migration

The read-only review found only four guides with the old filename or
executor/context symbols:

| Guide | Classification | Approved future action |
|---|---|---|
| `docs/battle-engine-structural-split-handoff.md` | Live structural authority | Replace the follow-up proposal with the approved file map, then record only fresh implementation evidence |
| `production/session-state/active.md` | Live current-state authority | Record the separately approved executor wave while keeping C10A afterward |
| `plan/battle_mechanics/06-hit-damage-effects-and-outcomes.md` | Historical C05B/C05C hashes and evidence | Preserve unchanged |
| `plan/battle_mechanics/07-parties-switching-and-replacements.md` | Historical C06A hashes and evidence | Preserve unchanged |

Responsibility-only matches in ADR-0002, roadmap files `00`, `03`, `05`, `08`,
and `12`, the modern-rules snapshot, battle-system interview, reusable-damage
quick spec, battle-HUD guide, ADR-0003, ADR-0004, and the historical ADR-0002
fail report are path-neutral or historical. Preserve them unchanged.

No stale executor-location assumption was found in `AGENTS.md`, `CLAUDE.md`,
`UE.md`, or `.codex/skills/`. Preserve generated evidence unchanged.

### Old-to-new mapping

- Monolithic `BattleEffectExecutor.cpp` becomes coordinator-only
  `BattleEffectExecutor.cpp` plus the six focused sources.
- The inline `FStateExecutionContext` declaration moves to
  `BattleEffectExecutorContext.h`.
- Context method bodies move according to the exact six-file ownership map
  above.
- `FBattleEffectExecutor::TryExecute` remains in `BattleEffectExecutor.cpp`.
- State preparation and direct plan-application methods move to
  `BattleEffectExecutorState.cpp`.

Historical guide text and generated reports are evidence, not migration
targets. Do not rewrite them to describe the new layout.

## Mechanical implementation order

1. Recheck both executor hashes, live source locations, caller contract,
   working-tree status, and the guide-reference inventory. Stop on any in-scope
   conflict or unexpected overlap.
2. Update the two live guides with approved-but-not-yet-complete wording while
   preserving their historical sections.
3. Create `BattleEffectExecutorContext.h` with unchanged signatures, access,
   and data-member order.
4. Create focused sources in dependency order:
   `State -> Triggers -> AbilityItems -> Conditions -> Damage -> Switching`.
5. Move method bodies mechanically, changing only required class qualification
   and self-contained includes.
6. Remove the inline context from `BattleEffectExecutor.cpp` and move the three
   state-level public definitions to `BattleEffectExecutorState.cpp`.
7. Prove all 102 context methods and all nine `FBattleEffectExecutor`
   definitions occur exactly once.
8. Run read-only scope, diff, `.cpp`-include, namespace, and stale-guide scans.
9. Run the fresh validation matrix below.
10. Only after clean fresh evidence, finalize the two live guides with actual
    line counts and fresh report locations.

Do not combine this mechanical relocation with cleanup or deduplication.

## Approved validation matrix (completed)

Do not reuse the saved P2 result as acceptance evidence.

First run a fresh `PokemonSolarusEditor Win64 Development` build with all four
forced-Unity flags:

```text
-ForceUnity -DisableAdaptiveUnity -BytesPerUnityCPP=1 -NoUBA
```

Then run these filters serially, stopping on the first build failure or
exported JSON gate failure:

| Filter | Expected successes |
|---|---:|
| `PokemonSolarus.Battle.ADR0002` | 110 |
| `PokemonSolarus.Battle.C03A` | 6 |
| `PokemonSolarus.Battle.C03B` | 6 |
| `PokemonSolarus.Battle.C04A` | 7 |
| `PokemonSolarus.Battle.C04B` | 9 |
| `PokemonSolarus.Battle.C05A` | 8 |
| `PokemonSolarus.Battle.C05B` | 9 |
| `PokemonSolarus.Battle.C05C` | 7 |
| `PokemonSolarus.Battle.C06A` | 7 |
| `PokemonSolarus.Battle.C06B` | 8 |
| `PokemonSolarus.Battle.C07A` | 7 |
| `PokemonSolarus.Battle.C07B` | 9 |
| `PokemonSolarus.Battle.C07C` | 8 |
| `PokemonSolarus.Battle.C07D` | 9 |
| `PokemonSolarus.Battle.C08A` | 7 |
| `PokemonSolarus.Battle.C08B` | 20 |
| `PokemonSolarus.Battle.C08C` | 27 |
| `PokemonSolarus.Battle.C09A` | 6 |
| `PokemonSolarus.Battle.C09B` | 7 |
| `PokemonSolarus.Battle.C09C` | 6 |
| `PokemonSolarus.Battle.Runtime` | 3 |
| `PokemonSolarus.Battle` | 320 |

The 21 focused filters contain 286 expected successes. Including full Battle,
the matrix contains 606 expected successes.

The approved review found 320 unique live Battle paths with sorted path-set
SHA-256:

`693e6aff39767ae1e8c771ba9b896836fd360db1f127128e24e82ded2c58e25d`

Judge every Automation run through its fresh exported `index.json`, not the
process exit code. Acceptance requires:

- the exact expected success count;
- zero succeeded-with-warnings, failures, not-run, and in-process entries;
- zero per-test warnings and errors;
- every test state equal to `Success`;
- unique exact-prefix test paths; and
- the full sorted path set matching the approved 320-path hash.

If the live registered test set changes before implementation, stop rather than
silently changing expected counts or the hash.

## Completion conditions

The implementation wave is complete only when:

- the exact approved file map exists and only the approved files were
  hand-edited;
- `BattleEffectExecutor.h` is byte-for-byte unchanged;
- the complete ordered `TryExecute` coordinator remains intact;
- exactly one context and one staged state feed the existing single final
  commit/publication path;
- all 102 context methods and all nine executor definitions occur exactly once;
- no `.cpp` includes another `.cpp`;
- focused sources have self-contained includes and forced-Unity-safe unique
  named namespaces;
- both live guides record truthful final layout and fresh evidence while all
  historical evidence remains unchanged;
- the forced-Unity build passes; and
- all 22 serial Automation filters pass the exported `index.json` gates.

The implementation session stopped after reporting the implementation and fresh
evidence. It did not commit or begin C10A. The later bounded commit and push are
separately authorized by the user.

## Implementation result — 2026-08-28

**PASS.** The implementation session rechecked the recorded hashes, live
authorities, caller contract, guide references, and working-tree inventory
before editing. No conflict was found.

The final physical layout is:

| File | Lines | Definitions |
|---|---:|---:|
| `BattleEffectExecutor.cpp` | 2,118 | Five rule-purpose getters and the ordered `TryExecute` coordinator |
| `BattleEffectExecutorContext.h` | 429 | One private context declaration |
| `BattleEffectExecutorState.cpp` | 471 | 22 context definitions and three state-level executor definitions |
| `BattleEffectExecutorDamage.cpp` | 809 | 8 context definitions |
| `BattleEffectExecutorConditions.cpp` | 1,726 | 27 context definitions |
| `BattleEffectExecutorAbilityItems.cpp` | 962 | 16 context definitions |
| `BattleEffectExecutorSwitching.cpp` | 623 | 3 context definitions |
| `BattleEffectExecutorTriggers.cpp` | 1,005 | 26 context definitions |

The final source audit found all 102 context definitions and all nine executor
definitions exactly once. `BattleEffectExecutor.h` remains byte-for-byte
unchanged at SHA-256
`0dadca0d3da930f2f5cf2a8548016e20ec70a1252f090bd452dbf16dc258e989`.
The ordered `TryExecute` coordinator matches the approved baseline, and
`BattleEngineMoveEffects.cpp` remains unchanged. One context construction and
one staged state still feed the existing outer identity recheck,
transactional-RNG commit, state-delta application, and successful publication
path. No `.cpp` includes another `.cpp`, and no focused source adds an anonymous
namespace or file-local helper definition.

Fresh validation evidence:

- forced-Unity build log:
  `Game/Saved/Logs/BattleEffectExecutorSplit-20260828-091241-ForcedUnityBuild.log`;
- serial 22-filter evidence root:
  `Game/Saved/AutomationReports/BattleEffectExecutorSplit-20260828-091837`;
- 286 focused successes plus 320 full-Battle successes, for 606 total;
- zero aggregate warnings, failures, not-run, or in-process tests and zero
  per-test warnings or errors; and
- full sorted 320-path SHA-256
  `693e6aff39767ae1e8c771ba9b896836fd360db1f127128e24e82ded2c58e25d`.

C10A, cleanup, deduplication, mechanics changes, tests, public contracts,
unrelated production sources, ADR-0003, and ADR-0004 remain outside this
completed task.
