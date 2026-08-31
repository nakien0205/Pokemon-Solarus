<!-- STATUS -->
Epic: Battle System
Feature: C11 full integration and release gate
Task: Prepare a fresh C11A implementation-approval handoff
<!-- /STATUS -->

# Active Project State — 2026-08-31

## Current state

- B00 through C10 are complete under their bounded validation. C10A source-row
  authoring and C10B import/catalog acceptance are complete. The required
  continuation is `C11A -> C11B`.
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
- C10B was accepted on 2026-08-31. Exactly seven approved production Data
  Tables changed after pure validation and seven transient conversions. The
  type-chart and runtime-scenario assets remained byte-identical. C11A is next
  and has not been implemented or approved.

## Accepted C10B result

C10B converted the accepted source rows into production Data Tables and proved
that source and reflected rows build the same immutable catalog. It added no
battle mechanic or production C++ owner.

The accepted hand-authored paths are:

- new `Game/SourceData/Battle/battle_source_validation.py`;
- new
  `Game/SourceData/Battle/tests/test_battle_source_validation.py`;
- new
  `Game/SourceData/Battle/tests/test_import_initial_battle_data.py`;
- `Game/SourceData/Battle/import_initial_battle_data.py`;
- `Game/SourceData/Battle/Initial/display_names.json`, adding only Gyarados,
  Rotom, Pelipper, Espathra, Clefable, and Excadrill;
- new
  `Game/Source/PokemonSolarus/Private/Tests/BattleCanonicalContentTests.cpp`;
  and
- `Game/Source/PokemonSolarus/Private/Tests/BattleRuntimeDataSourceTests.cpp`.
- `Game/PokemonSolarus.uproject`, converted from UTF-16 LE with BOM to
  semantically identical UTF-8 without BOM so UE 5.8.1 UnrealBuildTool can
  parse the project during a clean rebuild.

After complete preflight and transient conversion, the accepted asset write
allowlist was:

- `DT_InitialBattleSpeciesForms.uasset`;
- `DT_InitialBattleNatures.uasset`;
- `DT_InitialBattleMoves.uasset`;
- `DT_InitialBattleAbilities.uasset`;
- `DT_InitialBattleItems.uasset`;
- `DT_InitialBattleConditions.uasset`; and
- `DT_InitialBattleDisplayNames.uasset`.

`DT_InitialBattleTypeChart.uasset` and `DT_BattleRuntimeScenario.uasset` remained
validate-only and byte-identical. The existing reflected rows,
`FBattleDataTableAdapter`, `FBattleDefinitionCatalog`, and runtime loader remain
the production owners; C10B added validation/import coordination and tests only.
C10B acceptance does not authorize C11A or C11B implementation.

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
- Accepted C10B seven-table implementation/import evidence:
  `Game/Saved/AutomationReports/C10B-ImportAndCatalog-20260831-090312`.
  It records the pure preflight, seven transient conversions, exact seven-asset
  import, binary backups, and unchanged validate-only assets.
- Accepted project-encoding evidence:
  `Game/Saved/AutomationReports/C10B-UProjectUtf8Normalization-20260831-095635`.
  The decoded project text and JSON remained identical while the file changed
  from UTF-16 LE SHA-256
  `D67F6C440219884BA3EA65623A9DCB30F5245B25F07379A7AD1D17080FD59518`
  to UTF-8-without-BOM SHA-256
  `1F8CD7D128EDE4F1FA2B6D3D4E17DC0C748A7AC8C7C5B467234DF0C441BCCB17`.
- Final accepted C10B remediation evidence:
  `Game/Saved/AutomationReports/C10B-ReviewRemediationFinal-20260831-110350`.
  Python passed 21/21, the clean forced-Unity rebuild completed 150/150 actions
  with zero compiler warnings or errors, and the 12 serial Automation reports
  passed 187/187 with zero warning, failure, not-run, in-process, or per-test
  issue counters. The battle-owned catalog isolation proof passed, final
  `code-review` was APPROVED, and `test-evidence-review` was
  ADEQUATE/COMPLETE. Source and two independent production catalog summaries
  retained SHA-256
  `94CDB260DD1129C61E80CF4087389F1DC0265E3DCEDF95E0E254EBBC6A7F3CBA`.
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

1. Start a fresh C11A implementation-approval task against the live C11 plan.
2. Implement and accept C11A before starting C11B.
3. Preserve the required order `C11A -> C11B`.
