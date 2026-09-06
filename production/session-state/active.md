<!-- STATUS -->
Epic: Battle System
Feature: Fast reusable Battle roadmap through strategic opponent AI
Task: Four-Move Battle Selection GDD accepted; required architecture decisions are next
<!-- /STATUS -->

# Active Project State — 2026-09-03

## Current roadmap

- **Task:** Four-Move Battle Selection GDD.
- **Status:** Independent design review completed and accepted by the project
  owner on 2026-09-03. That review produced no separate review artifact.
- **File:** `design/gdd/four-move-battle-selection.md`.
- **Scope:** Only the ordered playable Battle steps from four-move selection
  through strategic opponent AI. Full-game systems remain outside this index.
- **Next:** Create and approve the separate opponent-policy and presentation-
  composition ADRs required by the accepted GDD. Acceptance of the GDD does
  not authorize implementation.

## Current state

- The reusable one-Pokemon playable battle prototype is accepted complete as of
  2026-09-02. A normal `PokemonSolarusEditor Win64 Development` build
  succeeded (target already up to date), and the project owner reported one
  complete FoundationMap PIE battle. This closes only the bounded prototype;
  it does not close C11A or change C11B.

- B00 through C10 are complete under their bounded validation. C10A source-row
  authoring and C10B import/catalog acceptance are complete. C11A's test-only
  deterministic integration remediation is validated, but C11A is explicitly
  **not fully complete**: its status is `INCOMPLETE_CATALOG_DEFERRED` because six
  accepted-catalog branches cannot yet be exercised.
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
  type-chart and runtime-scenario assets remained byte-identical.
- C11B's normal-Unity compatibility remediation, build/full-suite evidence
  gate, and independent Final Review are complete and accepted as **PASS**.
  Full C11 remains open because `C11A-DATA-001` through `C11A-DATA-006` are
  still deferred.

## Current C11A result

C11A added exactly ten test-only files and preserved exactly 25
`PokemonSolarus.Battle.C11A` Automation identities. It changed no production
C++, source JSON, Data Table, configuration, `.uproject`, UI, visual asset, or
runtime ownership seam.

The accepted remediation evidence is rooted at
`Game/Saved/AutomationReports/C11A-ReviewRemediation-20260831-221346`. A fresh
forced-Unity build succeeded and produced linked DLL SHA-256
`9432EE8A929878E27BC0A6A027A2116ED810EEAC2A540694102ACEC3FBBB2272`.
The six serial filters passed exact success counts `4/10/4/6/5/25`; every
warning, failure, not-run, in-process, and per-test issue counter was zero.
Protected hashes matched `251/251` with zero mismatches. Final `code-review`
was `APPROVED_WITH_SUGGESTIONS`; `test-evidence-review` was `ADEQUATE` with zero
blocking items for the approved remediation scope.

C11A remains `INCOMPLETE_CATALOG_DEFERRED`. The six declared gaps are:

1. Snow's physical Defense boost for Ice defenders: no accepted Ice species.
2. Sandstorm's Special Defense boost for Rock defenders: no accepted Rock
   species.
3. Sun/Rain modification of Water damage: no accepted damaging Water move.
4. Misty Terrain's Dragon reduction: no accepted damaging Dragon move.
5. Screen and Safeguard bypass branches: no accepted damaging or major-status
   bypass move.
6. Eight-turn field/side duration extension: no accepted qualifying
   set-condition effect.

Each gap is logged with its production symbol, missing data, current partial
coverage, data-arrival guard, and required later retest in
`deferred-catalog-gaps.json` under the evidence root. The guards fail when the
required data appears so coverage cannot remain silently deferred.

The user approved a special sequencing exception on 2026-08-31: C11B may be
implemented while these six gaps remain declared. This does not mark C11A fully
complete and does not waive the deferred retests. C11B used that exception and
is now complete; the exception does not permit a full C11 completion claim.

## Accepted C11B result

C11B is independently accepted as **PASS**. Its canonical evidence is rooted at
`Game/Saved/AutomationReports/C11B-NormalUnityRemediation-20260901-081133`.
The approved remediation changed exactly 14 tracked C++ files to contain
file-private helpers and test bodies in unique named namespaces. It changed no
battle behavior, public API, Automation identity, data, asset, configuration,
RNG ownership, event order, or replay schema.

The normal Editor build and the exact forced-Unity build with
`-ForceUnity -DisableAdaptiveUnity -BytesPerUnityCPP=1 -NoUBA` both produced
fresh DLL links. The final accepted DLL SHA-256 is
`906EA5B045F31677DE8015AB1FAB5D15B59D255B5CBA22F4C787A24A6092AA56`.
Source and rebuilt-binary discovery matched exactly at `418/418` with zero
duplicates. All 29 serial reports were accepted: `836/836` aggregate and
`418/418` for the complete `PokemonSolarus.Battle` prefix, with every exported
test issue counter zero.

Fresh C10B digest proof retained
`94CDB260DD1129C61E80CF4087389F1DC0265E3DCEDF95E0E254EBBC6A7F3CBA`.
Fresh C11A proof passed all 25 identities, the six declared gap messages, and
seven data-arrival guards while preserving `INCOMPLETE_CATALOG_DEFERRED`.
Protected hashes matched `303/303` with exactly the approved 14 changes and no
unapproved changes. The canonical evidence index contains 86 verified files.
The fresh independent `code-review` and `test-evidence-review` found no
unresolved Critical or High findings and returned final C11B verdict **PASS**.

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
C10B acceptance by itself did not authorize C11A or C11B implementation.

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
- C11A deterministic integration remediation evidence:
  `Game/Saved/AutomationReports/C11A-ReviewRemediation-20260831-221346`.
  The consolidated result is `completion-audit.json`; the six explicit data
  gaps and later retests are in `deferred-catalog-gaps.json`.
- Accepted C11B normal-Unity remediation and release-gate evidence:
  `Game/Saved/AutomationReports/C11B-NormalUnityRemediation-20260901-081133`.
  The canonical inventory is `evidence-index.json`; `completion-audit.json`
  records the pre-review validation closeout, and the subsequent independent
  read-only Final Review accepted C11B as **PASS**.
- Live C10 and C11 contracts:
  `plan/battle_mechanics/11-canonical-proof-content.md` and
  `plan/battle_mechanics/12-integration-and-release-gate.md`.

## Invariants and protected scope

- Preserve unrelated dirty documents byte for byte. Do not stage, commit, or
  otherwise change them without separate approval.
- No Git stage, unstage, commit, push, branch, reset, checkout, or history
  change is authorized by this state document; Git publication requires a
  separate explicit user request.
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

1. Create and approve the separate opponent-policy and presentation-composition
   ADRs required by the accepted Four-Move Battle Selection GDD. Do not begin
   implementation from the accepted GDD alone.
2. Keep C11A marked `INCOMPLETE_CATALOG_DEFERRED` and full C11 open; run the
   six deferred retests only when their required accepted catalog data arrives.
3. Do not begin Cry for Help/reinforcement or later presentation lanes without
   a separate user call and their own approval workflow.
