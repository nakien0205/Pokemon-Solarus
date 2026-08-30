<!-- STATUS -->
Epic: Battle System
Feature: C10 canonical proof content
Task: Obtain exact C10B implementation approval
<!-- /STATUS -->

# Active Project State — 2026-08-30

## Current state

- B00 through C09 are complete under focused validation. C10A source-row
  authoring is complete. The required continuation is
  `C10B -> C11A -> C11B`.
- ADR-0002 is Accepted and its bounded implementation gate is **PASS** at
  `b5db3e440d7c6eb5ba6ddbcc01a92a3c9b8756c0`.
- The BattleEngine and BattleEffectExecutor structural splits are complete.
  `docs/battle-engine-structural-split-handoff.md` records their current source
  map, invariants, accepted evidence, and one remaining non-runtime concern:
  three focused executor sources manually redeclare six helpers whose
  definitions remain in `BattleEffectExecutor.cpp`.
- R1 through R6 supplied the reusable typed mechanics required by C10A. The
  independent R7 gate passed against live source and exported evidence.
- The completed C10A source slice contains exactly 8 species/forms, 25 natures,
  62 moves, 8 Abilities, 14 items, and 40 conditions. Static validation passed
  with zero source-fact, reference, or descriptor errors. C10A changed no Unreal
  asset and ran no Automation.
- C10B is planned but is not implemented or approved. C11 remains blocked until
  C10B is accepted.

## Current C10B boundary

C10B converts the accepted source rows into production Data Tables and proves
that the reflected rows build the same immutable catalog. It must not add a new
battle mechanic.

The planned hand-authored paths are:

- new `Game/SourceData/Battle/battle_source_validation.py`;
- `Game/SourceData/Battle/import_initial_battle_data.py`;
- `Game/SourceData/Battle/Initial/display_names.json`, adding only Gyarados,
  Rotom, Pelipper, Espathra, Clefable, and Excadrill;
- new
  `Game/Source/PokemonSolarus/Private/Tests/BattleCanonicalContentTests.cpp`;
  and
- `Game/Source/PokemonSolarus/Private/Tests/BattleRuntimeDataSourceTests.cpp`.

After complete preflight, the planned asset write allowlist is:

- `DT_InitialBattleSpeciesForms.uasset`;
- `DT_InitialBattleNatures.uasset`;
- `DT_InitialBattleMoves.uasset`;
- `DT_InitialBattleAbilities.uasset`;
- `DT_InitialBattleItems.uasset`;
- `DT_InitialBattleConditions.uasset`; and
- `DT_InitialBattleDisplayNames.uasset`.

`DT_InitialBattleTypeChart.uasset` and `DT_BattleRuntimeScenario.uasset` remain
validate-only. No production C++ change is expected. If the existing source,
reflection, adapter, catalog, or runtime path cannot support the approved rows,
C10B must stop and return to the owning package under a new implementation
draft. A C10B approval does not authorize C11A or C11B.

## Current evidence

- ADR-0002 implementation gate:
  `production/gate-checks/2026-08-27-adr-0002-implementation-pass.md`.
- Structural record:
  `docs/battle-engine-structural-split-handoff.md`.
- Executor split evidence:
  `Game/Saved/AutomationReports/BattleEffectExecutorSplit-20260828-091837`.
- Independent R7 evidence:
  `Game/Saved/AutomationReports/R7-IndependentGate-20260830-164042`.
- Reproducible C10A source evidence:
  `Game/Saved/AutomationReports/C10A-SourceContent-20260830-170739`.
- C10A implementation evidence:
  `Game/Saved/AutomationReports/C10A-Implementation-20260830-172323`.
- Live C10 and C11 contracts:
  `plan/battle_mechanics/11-canonical-proof-content.md` and
  `plan/battle_mechanics/12-integration-and-release-gate.md`.

## Invariants and protected scope

- Preserve the pre-existing untracked ADR-0003 and ADR-0004 documents byte for
  byte. Do not stage, commit, or otherwise change them without separate
  approval.
- No Git stage, unstage, commit, push, branch, reset, checkout, or history
  change is authorized by this state document.
- Preserve one authoritative battle state, transactional RNG ownership, event
  and replay ordering, stale-identity checks, and exact-once publication.
- Do not claim the executor six-helper declaration seam is remediated without a
  separately approved source change and fresh validation.
- Cry for Help, wild reinforcement, and `CallReinforcement` remain **Freeze
  until call by user**.
- Battle HUD visuals and Blueprint assets remain user-owned. Do not change
  layout, styling, art, materials, textures, composition, or motion appearance.
- Do not modify production source, tests, source data, assets, Blueprints,
  configuration, `.uproject` data, modules, or generated Unreal output without
  a new task-specific approval.

## Next

1. Obtain exact C10B implementation approval against the live C10 plan.
2. Complete and independently accept C10B before starting C11A.
3. Preserve the required order `C10B -> C11A -> C11B`.
