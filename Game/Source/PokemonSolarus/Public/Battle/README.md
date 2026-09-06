# Public Battle Contracts

This folder contains the public contracts and reusable types for the Battle system.

Use it to answer:

> Which Battle contract or type should I understand before changing this subsystem?

Do not read every header in this folder. Start with the smallest contract group that owns the behavior, then inspect its implementation and matching tests.

## Agent routing rule

For a Battle change:

```text
Public/Battle contract
    ↓
Private/Battle implementation
    ↓
Private/Tests matching subsystem tests
```

If intended behavior is unclear, consult the matching package under `plan/battle_mechanics/` after identifying the owning subsystem.

## Major responsibility groups

### Core engine, setup, state, and decisions

Start here for battle lifecycle, mutable-state ownership, setup, snapshots, decisions, or public events.

* `BattleEngine.h` — main mutable Battle-core owner and public operation boundary.
* `BattleSetup.h`
* `BattleSetupTypes.h`
* `BattleSnapshot.h`
* `BattleDecision.h`
* `BattleEvent.h`
* `BattleIdentifiers.h`

Use `BattleEngine.h` when changing how a Battle operation is entered or exposed. Use the more specific setup/snapshot/decision/event contracts when changing the shape of data crossing that boundary.

### Definitions, catalogs, and runtime data

Start here for authored Battle definitions, DataTable loading, catalogs, or runtime-source construction.

* `BattleDefinitions.h`
* `BattleDefinitionCatalog.h`
* `BattleDataTableAdapter.h`
* `BattleDataTableRows.h`
* `BattleRuntimeDataTableRows.h`
* `BattleRuntimeSource.h`
* `BattleDisplayNameResolver.h`

For Battle source-data authoring or importer problems, route first to `Game/SourceData/Battle/` rather than treating these headers as the data source.

### Actions, ordering, and targeting

Start here for action selection, queue ordering, or legal target calculation.

* `BattleActionQueue.h`
* `BattleActionSelector.h`
* `BattleTargeting.h`

Do not begin in damage or effect execution when the problem is actually action legality, ordering, or target selection.

### Hit, damage, stats, and types

Start here for move hit rules, damage math, stat calculation, type effectiveness, or related move-rule inputs.

* `BattleHitResolver.h`
* `BattleDamageCalculator.h`
* `BattleFinalDamageCalculator.h`
* `BattleStatCalculator.h`
* `BattleStats.h`
* `BattleStatStages.h`
* `BattleTypeChart.h`
* `BattleMoveCategory.h`
* `BattleMoveHitRules.h`
* `BattleMoveWeatherRules.h`

Pick the narrowest contract first. For example, a type-effectiveness change should normally begin with `BattleTypeChart.h`, not the entire damage stack.

### Status, volatile state, field/side conditions, and triggers

Start here for persistent Battle conditions and their lifecycle integration.

* `BattleMajorStatus.h`
* `BattleVolatile.h`
* `BattleFieldSideConditions.h`
* `BattleTriggerFramework.h`

Use `BattleTriggerFramework.h` when the change concerns when or how a condition, Ability, or item participates in Battle lifecycle phases rather than the condition's isolated rule itself.

### Abilities and items

Start here for Ability behavior, held-item interaction, or Bag-item rules.

* `BattleAbility.h`
* `BattleAbilityItemContracts.h`
* `BattleItem.h`
* `BattleBagItem.h`

Then route to the matching private implementation family and focused tests.

### Switching, encounters, capture, and wild flow

Start here for party switching, replacement-related contracts, encounter restrictions, capture, or wild Battle flow.

* `BattleSwitching.h`
* `BattleCapture.h`
* `BattleWildFlow.h`
* `BattleEncounterPolicy.h`
* `BattleEncounterPolicyTypes.h`

### RNG and replay

Start here when deterministic randomness, transactional RNG, replay inputs, replay records, or canonical serialization are part of the change.

* `BattleRandom.h`
* `BattleReplay.h`

Ordinary deterministic rule changes do not require reading the entire RNG/replay infrastructure unless they alter RNG ownership, transactional behavior, trace semantics, or replay-visible state.

## Related directories

* `Game/Source/PokemonSolarus/Private/Battle/` — implementations of these Battle contracts.
* `Game/Source/PokemonSolarus/Private/Tests/` — focused unit, contract, atomic, integration, and presentation tests.
* `Game/SourceData/Battle/` — canonical editable Battle source data and import/validation tooling.
* `plan/battle_mechanics/` — detailed Battle requirements and package boundaries.
* `Game/Source/PokemonSolarus/Public/UI/` — presentation contracts that consume Battle state; UI is not Battle-mechanics authority.

## Do not read by default

Do not automatically read:

* every header in this directory;
* every file under `Private/Battle/`;
* the complete Battle test suite;
* all Battle plan packages;
* Unreal DataTable assets;
* UI or presentation code for a mechanics-only change.

Identify the owning subsystem first, read its public contract, then follow only the directly relevant implementation and tests.
