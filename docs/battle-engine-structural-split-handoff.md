# Battle Engine Structural Split Record

## Status

Guide Wave G1B, atomic-test Wave T1, private-support Wave P0, member-relocation
Waves P1 and P2, and the G2 documentation closeout are complete. The user
accepted the Battle Engine structural split on 2026-08-28.

The separate `BattleEffectExecutor` split also completed on 2026-08-28 and was
published as `504f036858bae310de8ad03ae450903ebedc2779`. This document records
the completed structural work and its preserved evidence. It does not authorize
future production, test, documentation, or Git changes.

Live source, the active roadmap package, the worktree, and fresh exported Unreal
Automation reports override this historical record.

## Structural invariants

- `FBattleEngine` remains the public facade and sole owner of the authoritative
  `FBattleEngineState`.
- `BattleEngine.h` remains a compact public declaration file.
- `BattleResolutionCommit.h/.cpp` remains the common atomic commit seam unless
  live evidence establishes a concrete need to change it.
- Each checkpoint preserves explicit identity, preparation, staging, validation,
  and commit ordering.
- Replay, event ordering, stale-identity checks, RNG ownership, and exact-once
  publication must not change as a side effect of structural work.
- `BattleEffectExecutor` keeps one staged context, one staged state, and the
  existing outer commit and publication path.
- Sources use self-contained includes and unique named private namespaces; no
  `.cpp` file includes another `.cpp` file.
- Do not introduce a new Unreal module merely to reduce file length.

## Final `BattleEngine` source map

| File | Responsibility |
|---|---|
| `BattleEngine.cpp` | Constructor, destructor, `TryCreate`, and test-fixture creation |
| `BattleEngineSnapshots.cpp` | Snapshot projection, filtering, and read-only getters |
| `BattleEngineDecisionFlow.cpp` | Decision startup, requests, batches, and all of `SubmitDecision`, including Pivot continuation |
| `BattleEngineActionStart.cpp` | Atomic action-start staging and `BeginNextLockedAction` |
| `BattleEngineWildActions.cpp` | Atomic Run and WildFlee execution |
| `BattleEngineBagActions.cpp` | Complete `ExecuteCurrentBagItem`, including ordinary Bag, stale cancellation, and Capture |
| `BattleEngineVoluntarySwitch.cpp` | Voluntary-switch identity, staging, and execution |
| `BattleEnginePreMove.cpp` | Pre-move identity, gates, PP staging, and commit |
| `BattleEngineMoveTargets.cpp` | Atomic target resolution |
| `BattleEngineMoveEffects.cpp` | Atomic move-effect resolution coordinator |
| `BattleEngineEndTurn.cpp` | End-turn resolution |
| `BattleEngineBetweenActions.cpp` | `ApplyBetweenActionsStatRefresh` |
| `BattleEngineReplay.cpp` | Replay export methods |

Bag and Capture remain branches of `ExecuteCurrentBagItem`; Pivot continuation
remains part of `SubmitDecision`. Splitting either further requires a separately
reviewed internal seam, not mechanical relocation alone.

## Shared BattleEngine support map

Helpers are promoted to shared support only when at least two focused
translation units need them; otherwise they remain local to their owning source.

| Private support pair | Responsibility |
|---|---|
| `BattleEngineCommon.*` | No-draw random, stable identifiers, common state lookups, sources, and rejection facts |
| `BattleEngineCheckpointState.*` | Shared exact identities and staged projection/delta support |
| `BattleEngineQueueBoundary.*` | Pure queue-boundary and replacement planning |
| `BattleEngineEvents.*` | Common event construction |
| `BattleEngineTriggerRuntime.*` | Trigger dispatch and cleanup |
| `BattleEngineSwitchPipeline.*` | Switch application, entry hazards, immediate held items, and entry abilities |

## Atomic test split record

T1 replaced `BattleAtomicCheckpointTests.cpp` with eight focused checkpoint test
sources while preserving all 84 exact Automation paths. Four private support
pairs provide common checkpoint, fault, switch, and move-checkpoint helpers.
The 3D2 Bag checkpoint tests remain in `BattleBagItemTests.cpp`; no separate
Bag-action test source was created.

## BattleEffectExecutor follow-up

The completed executor split uses `BattleEffectExecutorContext.h` and focused
state, damage, conditions, ability/item, switching, and trigger sources. All
context methods remain on the same single context; none introduces another state
copy, RNG owner, commit seam, or publication path.

One non-runtime dependency-clarity concern remains: three focused sources
manually redeclare six helpers whose definitions remain in
`BattleEffectExecutor.cpp`. The declarations matched and the build linked during
the accepted split validation. A separately approved source change should either
restore local ownership or introduce one explicit private declaration header.

Split-era header hashes, line counts, and unchanged-header constraints are
historical evidence only. Later approved mechanics work changed established
executor owners without adding a second execution context, state, identity
recheck, RNG owner, commit, or publication path.

## Accepted evidence

All listed Automation reports have clean aggregate counters and per-test warning
and error counters. Every recorded test state is `Success`. The 320-path full
Battle set has SHA-256
`693e6aff39767ae1e8c771ba9b896836fd360db1f127128e24e82ded2c58e25d`.

| Wave | Build or evidence root | Result |
|---|---|---|
| T1 | `Game/Saved/AutomationReports/BattleStructural-T1-20260827-185852` | 110 ADR-0002 and 320 full-Battle successes; exact pre/post 84-path test-split comparison |
| P0 | `Game/Saved/AutomationReports/BattleStructural-P0-20260827-200412` | Forced-Unity build and 22-filter matrix; 606 overlapping executions, including 320 full-Battle successes |
| P1 | `Game/Saved/AutomationReports/BattleStructural-P1-20260827-212839` | Forced-Unity build and 22-filter matrix; 606 overlapping executions, with path sets equal to P0 |
| P2 | `Game/Saved/AutomationReports/BattleStructural-P2-20260827-225953` | Forced-Unity build and 22-filter matrix; 606 overlapping executions, with path sets equal to P1 |
| G2 | Preserved T1/P0/P1/P2 reports and P2 build log | Read-only closeout confirmed the final 25-file layout, 28 `FBattleEngine` definitions exactly once, and no `.cpp` includes |
| Executor split | `Game/Saved/AutomationReports/BattleEffectExecutorSplit-20260828-091837` | Forced-Unity build and 22-filter matrix; 606 overlapping executions, including 320 full-Battle successes |

The forced-Unity builds used `-ForceUnity`, `-DisableAdaptiveUnity`,
`-BytesPerUnityCPP=1`, and `-NoUBA`. Process exit status alone is not Automation
acceptance evidence; use exported `index.json` counters and path identity.

## Future changes

The structural split is complete. Any later cleanup, deduplication, new internal
seam, public-contract change, or executor declaration-seam remediation requires
a separately reviewed and approved task. Preserve the invariants and validate
the affected scope against fresh exported evidence.
