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
