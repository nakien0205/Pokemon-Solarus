# Battle Implementation

This folder contains the private implementation of the Battle system.

Use it to answer:

> Which Battle implementation file or file family owns this behavior?

Do not read every Battle source file. Start from the narrowest owning contract or behavior family. Use `Public/Battle/` first when a relevant public contract exists; otherwise begin with the private owner named below. Then identify the smallest matching private implementation family and focused tests.

## Agent routing rule

For a Battle change:

```text
narrowest owning contract or behavior family
(Public/Battle when applicable)
    ↓
smallest matching Private/Battle implementation family
    ↓
Private/Tests matching subsystem tests
```

If intended behavior is unclear, use `production/session-state/active.md` to identify the current accepted authority. Consult `plan/battle_mechanics/` only when the current task specifically depends on the completed Battle mechanics packages.

Do not load replay, checkpoint, atomic, or broad BattleEngine infrastructure unless the task actually crosses those boundaries.

## Major responsibility groups

### BattleEngine orchestration

`BattleEngine.cpp` is the main engine construction/facade implementation, but Battle operations are split across focused `BattleEngine*` files.

Do not assume all Battle behavior belongs in `BattleEngine.cpp`.

Important focused engine files include:

* `BattleEngineMoveTargets.cpp` — engine-side target-resolution checkpoint.
* `BattleEngineMoveEffects.cpp` — move-effect resolution orchestration.
* `BattleEngineVoluntarySwitch.cpp` — voluntary-switch execution/checkpoint.
* `BattleEngineBagActions.cpp` — Bag-item and Capture action execution.
* `BattleEngineReplay.cpp` — BattleEngine replay export.
* `BattleEngineCheckpointState.cpp` — checkpoint/stale-identity support.
* `BattleResolutionCommit.cpp` — atomic resolution commit support.

Search `BattleEngine*` for the operation being changed instead of opening the whole family.

### Effect execution

Start with the `BattleEffectExecutor*` family when the task concerns applying move effects or effect-driven state changes.

The implementation is intentionally split.

Important files include:

* `BattleEffectExecutor.cpp` — core executor behavior and coordination.
* `BattleEffectExecutorState.cpp` — staged execution-state/context behavior.
* `BattleEffectExecutorDamage.cpp` — hit/damage and damage-rule integration.
* `BattleEffectExecutorConditions.cpp` — condition-related effect execution.
* `BattleEffectExecutorAbilityItems.cpp` — Ability/item behavior during effect execution.
* `BattleEffectExecutorTriggers.cpp` — executor-side trigger registration, dispatch, and cleanup.
* `BattleEffectExecutorItemMoves.cpp` — held-item move operations.
* `BattleEffectExecutorSwitching.cpp` — forced-switch and switch-related effect execution.
* `BattleEffectExecutorActionModifiers.cpp` — action-power modifier execution.

Do not read every executor file unless the effect crosses those responsibilities.

### Actions, ordering, targeting, and redirection

Start here when the problem concerns action selection, queue ordering, target legality, or redirection:

* `BattleActionQueue.cpp`
* `BattleActionSelector.cpp`
* `BattleTargeting.cpp`
* `BattleMoveRedirection.cpp`

For engine integration of move target resolution, also inspect:

* `BattleEngineMoveTargets.cpp`

`BattleTargeting.cpp` owns reusable targeting rules; `BattleEngineMoveTargets.cpp` owns their BattleEngine checkpoint integration.

### Hit, damage, stats, and types

Start with the narrowest matching implementation:

* `BattleHitResolver.cpp`
* `BattleDamageCalculator.cpp`
* `BattleFinalDamageCalculator.cpp`
* `BattleStatCalculator.cpp`
* `BattleStatStages.cpp`
* `BattleTypeChart.cpp`
* `BattleMoveHitRules.cpp`
* `BattleMoveWeatherRules.cpp`

For a basic damage-formula bug, do not begin in `BattleEngine*`.

For final damage modifiers, inspect `BattleFinalDamageCalculator.cpp` rather than assuming `BattleDamageCalculator.cpp` owns the full damage pipeline.

### Conditions and triggers

Start with the matching condition implementation for isolated rules.

Relevant families include:

* `BattleMajorStatus*`
* `BattleVolatile*`
* `BattleFieldSideConditions*`
* `BattleTriggerFramework.cpp`

Use `BattleEngineTriggerRuntime.*` when the issue concerns how BattleEngine integrates trigger registration, lifecycle, cleanup, or dispatch.

Do not load trigger runtime for an isolated condition-rule change unless lifecycle integration is involved.

### Abilities and items

Start with the matching behavior family:

* `BattleAbility*`
* `BattleAbilityItemContracts*`
* `BattleItem*`
* `BattleBagItem*`
* `BattleHeldItemMoveEffects*`

Then follow into `BattleEffectExecutor*` or `BattleEngine*` only when the behavior requires lifecycle/effect/action integration.

For example:

```text
Ability definition/rule
    → BattleAbility*
    → trigger integration if needed
    → matching executor/engine seam if needed
```

Do not treat every Ability or item change as a BattleEngine change.

### Switching, fainting, and partner behavior

These responsibilities are separate.

Start with:

* `BattleSwitching*` — reusable switching legality/resolution rules.
* `BattleEngineVoluntarySwitch.cpp` — voluntary-switch engine checkpoint.
* `BattleEngineSwitchPipeline.cpp` — shared switch-application pipeline.
* `BattleFaintOutcomeResolver.cpp` — faint transitions, replacements, and outcome resolution.
* `BattlePartnerFlow.cpp` — partner-specific Battle flow.

Use the smallest owner matching the problem instead of opening the entire switching/fainting stack.

### Encounters, Capture, and wild flow

Start with:

* `BattleEncounterPolicy.cpp` — compiled encounter-policy behavior.
* `BattleCapture.cpp` — Capture calculations and rules.
* `BattleWildFlow.cpp` — reusable Run and configured WildFlee rules.
* `BattleEngineWildActions.cpp` — BattleEngine execution/checkpoint for Run and configured WildFlee actions.

Capture action execution also crosses:

* `BattleEngineBagActions.cpp`

Use `BattleWildFlow.cpp` for reusable Run/WildFlee rules; use `BattleEngineWildActions.cpp` when the problem concerns execution, checkpointing, cleanup, or BattleEngine integration.

### RNG, replay, checkpoints, and atomicity

Start here only when the task actually involves deterministic RNG, replay-visible behavior, stale checkpoints, transactional mutation, or publication ordering.

Relevant files include:

* `BattleRandom.cpp`
* `BattleReplay.cpp`
* `BattleEngineReplay.cpp`
* `BattleResolutionCommit.cpp`
* `BattleEngineCheckpointState.cpp`

Also route through the relevant `BattleEngine*` checkpoint being changed.

Do not read this infrastructure for an ordinary deterministic rule bug.

If the change affects state/RNG/event publication atomicity, stale-plan behavior, replay semantics, or resolution commit guarantees, consult:

`docs/registry/architecture/adr-0002-battle-encounter-runtime-authority-and-atomic-resolution-commit.md`

### Definitions, catalogs, and runtime data

Start with:

* `BattleDefinitionCatalog.cpp` — validated runtime definition catalog.
* `BattleDataTableAdapter.cpp` — reflected DataTable rows → Battle definitions.
* `BattleDataTableRuntimeSource.cpp` — production DataTable-backed runtime source.

For editable Battle content, do **not** begin here.

Route instead to:

`Game/SourceData/Battle/`

The editable source data is authored there and imported into Unreal DataTables.

## Tests

Matching Battle tests are under:

`Game/Source/PokemonSolarus/Private/Tests/`

Use `../Tests/README.md` to select the smallest relevant test family.

Typical routing:

```text
private implementation being changed
    ↓
matching focused unit/contract test
    ↓
lifecycle/canonical test only if the boundary is affected
    ↓
BattleAtomic* only if transactional/checkpoint behavior is affected
```

Do not read or run the complete Battle test suite by default.

## Related directories

* `Game/Source/PokemonSolarus/Public/Battle/` — public Battle contracts.
* `Game/Source/PokemonSolarus/Private/Tests/` — focused Battle tests.
* `Game/SourceData/Battle/` — editable Battle source data and import/validation tooling.
* `plan/battle_mechanics/` — completed Battle mechanics implementation packages; consult only when the current task depends on them.
* `docs/registry/architecture.yaml` — compact architecture registry.
* `Game/Source/PokemonSolarus/Private/UI/` — presentation implementation; not Battle-mechanics authority.

## Do not read by default

Do not automatically read:

* every `BattleEngine*` file;
* every `BattleEffectExecutor*` file;
* every Battle implementation file;
* the complete Battle test suite;
* every Battle plan package;
* replay/atomic infrastructure for an ordinary rule change;
* UI/presentation code for a mechanics-only change;
* imported Unreal DataTables when editable source data is the actual concern.

Identify the owning public contract, choose the smallest private implementation family, inspect its focused tests, and expand context only when the behavior crosses another boundary.
