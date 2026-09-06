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

For source-data authoring or importer problems, route first to `Game/SourceData/` rather than treating these headers as the data source.

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

* all 42 headers in this directory;
* every file under `Private/Battle/`;
* the complete Battle test suite;
* all Battle plan packages;
* Unreal DataTable assets;
* UI or presentation code for a mechanics-only change.

Identify the owning subsystem first, read its public contract, then follow only the directly relevant implementation and tests.

---

# `Game/Source/PokemonSolarus/Private/Tests/README.md`

# Battle Tests

This folder contains Battle unit tests, contract tests, atomic/checkpoint tests, canonical integration tests, presentation tests, and shared test infrastructure.

It is intentionally broad. Do not treat the directory as one test suite that must be read whenever Battle code changes.

## Agent routing rule

> Search by subsystem first. Read the smallest relevant unit or contract tests, then add affected integration or atomic tests only when the change crosses those boundaries.

Typical routing:

```text
changed Battle subsystem
    ↓
smallest matching unit/contract tests
    ↓
affected canonical integration tests, if applicable
    ↓
atomic/checkpoint tests, only if transactional boundaries are involved
```

## Test families

### Unit and contract tests

These are usually the first stop for an isolated Battle-rule or public-contract change.

Examples include:

* `BattleAbilityTests.cpp`
* `BattleActionQueueTests.cpp`
* `BattleBagItemTests.cpp`
* `BattleCaptureTests.cpp`
* `BattleDamageCalculatorTests.cpp`
* `BattleDefinitionCatalogTests.cpp`
* `BattleEngineContractTests.cpp`
* `BattleHitDamageTests.cpp`
* `BattleMajorStatusTests.cpp`
* `BattleSnapshotDecisionTests.cpp`
* `BattleStatCalculatorTests.cpp`
* `BattleSwitchingTests.cpp`
* `BattleTargetingTests.cpp`
* `BattleTriggerFrameworkTests.cpp`
* `BattleTypeChartTests.cpp`
* `BattleVolatileTests.cpp`
* `BattleWildFlowTests.cpp`

Search for the subsystem name instead of browsing this list manually.

Other focused families cover setup, identifiers, items, field/side conditions, move rules, replay, replacement, partner flow, redirection, and related mechanics.

### Atomic and checkpoint tests

Files beginning with `BattleAtomic*` verify Battle operations that must preserve transactional, rollback, checkpoint, stale-identity, and exact-publication behavior.

Shared infrastructure includes:

* `BattleAtomicCheckpointTestCommon.*`
* `BattleAtomicCheckpointTestFaults.*`
* `BattleAtomicCheckpointTestHarness.h`
* `BattleAtomicMoveCheckpointTestSupport.*`
* `BattleAtomicSwitchTestSupport.*`

Read these when a change affects an atomic Battle operation, checkpoint preparation/commit, rollback behavior, staged RNG or events, or exact-once publication.

Do **not** load them automatically for an isolated calculator, type-chart, or ordinary rule change.

### Canonical integration tests

Files beginning with `BattleCanonical*` prove behavior across larger Battle-system boundaries.

Current groups include:

* condition integration;
* canonical content/catalog coverage;
* Doubles behavior;
* modifiers;
* partner behavior;
* replay invariants;
* Singles core/effects;
* wild Battle behavior.

Shared support is in:

* `BattleCanonicalIntegrationTestSupport.*`

Use these after focused tests when a change affects one of the canonical end-to-end scenarios or crosses subsystem boundaries.

### UI and presentation tests

Presentation-related Battle tests include:

* `BattleCommandUITests.cpp`
* `BattleHUDProductionLifecycleTests.cpp`
* `BattlePresentationAdapterTests.cpp`
* `BattleRuntimePresentationTests.cpp`

Use these for functional UI code-behind, presentation adaptation, HUD lifecycle, or Battle runtime-to-UI integration.

They do not make UI code authoritative for Battle mechanics.

### Cross-cutting lifecycle and replay tests

Some mechanics have focused lifecycle/replay coverage in addition to their ordinary unit tests, for example:

* ally action-power modifiers;
* held-item move effects;
* move redirection;
* replay contracts.

Read these when changing that exact feature or when the change alters replay-visible/lifecycle behavior.

### Shared test infrastructure

Common helpers include:

* `BattleTestFactories.h`
* `BattleTestRandom.h`
* `BattleScriptedActionSelector.h`

Read a helper when a relevant test uses it or when test setup itself is the problem. Do not inspect shared infrastructure merely because it exists.

## Choosing the first tests to inspect

For most changes:

1. Search this folder for the subsystem or public type being changed.
2. Open the smallest matching unit/contract test file.
3. Add directly affected lifecycle or canonical integration tests if behavior crosses those seams.
4. Add `BattleAtomic*` coverage only when transactional/checkpoint behavior is affected.
5. Use shared support files only as required by those tests.

## Related directories

* `../Battle/` — Battle implementation under test.
* `../../Public/Battle/` — public contracts the tests exercise.
* `../UI/` and `../../Public/UI/` — functional Battle presentation code.
* `Game/SourceData/Battle/tests/` — Python tests for Battle source validation/import tooling.

## Do not read or run by default

Do not automatically:

* read all ~80 test/support files;
* run every Battle test because one rule changed;
* load every `BattleAtomic*` test for a non-atomic change;
* load every canonical integration test before identifying the affected scenario.

Start narrow and expand only when the changed behavior crosses a tested boundary.
