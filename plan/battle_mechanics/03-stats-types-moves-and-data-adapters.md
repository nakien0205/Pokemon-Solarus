# C02 — Stats, Types, Moves, and Data Adapters

Priority: P0  
Status: C02 complete under user-limited focused validation  
Hard dependency: C01B complete  
Session shape: C02A and C02B are logically parallel

## Objective

Provide validated immutable rule data and pure calculations needed by mutable
battle state. Keep permanent calculated stats separate from temporary stages
and keep Unreal Data Tables outside the core rules.

## Parallel Ownership

C02A owns only stat inputs, resolved nature-modifier application, stat
calculation, effective-stat queries, and their tests. C02B alone owns nature
IDs/rows, types, move/definition records, catalog validation, Unreal adapters,
and their tests. C01 freezes the small `FNatureStatModifier` value shared by the
lanes. Neither lane edits `FBattleEngine`, battle state, the action resolver, or
the existing base-damage implementation.

In the current no-Git workspace, run the sessions sequentially despite their
logical independence.

## C02A — Permanent Stats and Temporary Stage Math

Status: Complete on 2026-08-20

Define:

- `FPokemonStatInputs`: level, six base stats, six IVs, six EVs, and an already
  resolved `FNatureStatModifier`.
- `FPokemonBattleStats`: permanent calculated Max HP, Attack, Defense, Special
  Attack, Special Defense, and Speed.
- `FBattleStatStages`: Attack, Defense, Special Attack, Special Defense, Speed,
  Accuracy, and Evasion stages.
- `FBattleStatCalculator` and pure effective-stat query functions.

Validation:

- Level `1..100`.
- Each base stat is positive.
- IV `0..31` for each stat.
- EV `0..252` for each stat and total EV at most `510`.
- The supplied nature modifier has exactly one valid boost/reduction pair or is
  neutral.

Behavior:

- Use B00B's exact modern HP and non-HP formulas and rounding.
- Apply neutral/boosting/reducing nature modifiers supplied through C01's frozen
  value contract. Do not own nature IDs or rows.
- Apply stage multipliers for every value from `-6` through `+6`.
- Accuracy/evasion use their own canonical table.
- Cap stage-changing requests at `-6/+6` and report applied versus blocked
  changes without mutating permanent stats.
- Effective Speed can later accept typed transient modifiers, but the permanent
  Speed remains unchanged.

## C02B — Types, Moves, Definitions, and Unreal Adapter

Status: Complete under user-limited focused validation (2026-08-20)

Define immutable records for:

- Species/forms: typing, base stats, catch rate, Ability choices, and stable IDs.
- Natures.
- Moves: type, Physical/Special/Status category, power, accuracy, PP, priority,
  target class, flags, and an ordered list of effect descriptors.
- Abilities, held items, battle items, capture items, and conditions. Later
  packages populate their rule payloads without changing definition identity.
- A complete 18x18 type chart.

Move effect descriptors must express, without bespoke move code:

- Damage, status/stage application, healing, drain, recoil, multi-hit count,
  secondary chance, field/side changes, switching, item changes, charging,
  recharge, Protect, and semi-invulnerability.
- Effect order, target selector, probability, magnitude, duration, and flags.

Create `FBattleDefinitionCatalog` as an immutable validated collection. Catalog
construction fails atomically on:

- Duplicate or invalid IDs.
- Missing referenced definitions.
- Invalid stats, PP, power, accuracy, priority, duration, chance, or layers.
- An incomplete/duplicate type chart.
- An effect incompatible with the move category or target class.

### Unreal boundary

- Unreal row `USTRUCT`s inherit `FTableRowBase` and live only in the adapter
  layer.
- `FBattleDataTableAdapter::BuildCatalog` reads rows, copies them into plain
  definitions, validates all cross-references, and returns the frozen catalog
  or typed diagnostics.
- Never cache a `UDataTable` row pointer. Reimport may invalidate it.
- Store source JSON under `Game/SourceData/Battle`.
- Store imported Data Table assets under `Game/Content/Data/Battle`.
- Prefer JSON over CSV for ordered/nested effect arrays.
- Pure core tests construct definitions directly and do not require Unreal
  assets or an Editor process.

## Tests

C02A:

- Levels 1, 50, and 100; neutral, boosting, and reducing natures.
- IV/EV minimums, maximums, per-stat rejection, and total-510 rejection.
- HP versus non-HP rounding golden cases.
- Every stage from `-6` through `+6`; cap/floor attempts.
- Accuracy/evasion multipliers.
- Permanent stats unchanged after every effective-stat query.

C02B:

- All 324 type-chart entries, immunity, neutral, resistant, super-effective,
  and dual-type products.
- Physical, Special, Status, always-hit, spread, ally, and field move records.
- Duplicate IDs, missing references, bad effect order, invalid range, and
  incomplete-chart rejection.
- JSON/Data Table row copy and proof that later source-row mutation cannot alter
  the frozen catalog.
- Catalog construction produces deterministic definition order and diagnostics.

## Acceptance

- Existing four base-damage tests remain unchanged and pass.
- All stat/type/move golden fixtures match B00B.
- Core stat and definition headers have no `UObject` or `UDataTable` dependency.
- Invalid content cannot produce a partially usable catalog.
- C03 can consume permanent stats, stages, IDs, moves, and a frozen catalog
  without changing C02 public contracts.

## C02A Completion Handoff

Status date: 2026-08-20  
Successful focused run ID: `C02A-Stats-20260820T134951Z`  
Successful build run ID: `C02A-EditorBuild-20260820T134917Z`  
C02A status: Complete  
Next package at this historical handoff: C02B; later completion recorded below

### Owned files

C02A modified or added only these runtime and focused-test files:

- `Game/Source/PokemonSolarus/Public/Battle/BattleStats.h`
- `Game/Source/PokemonSolarus/Public/Battle/BattleStatStages.h`
- `Game/Source/PokemonSolarus/Public/Battle/BattleStatCalculator.h`
- `Game/Source/PokemonSolarus/Private/Battle/BattleStatStages.cpp`
- `Game/Source/PokemonSolarus/Private/Battle/BattleStatCalculator.cpp`
- `Game/Source/PokemonSolarus/Private/Tests/BattleStatCalculatorTests.cpp`
- `Game/Source/PokemonSolarus/Private/Tests/BattleStatStagesTests.cpp`

This handoff and `00-roadmap-index.md` are the only status documents updated.
C02A did not edit C01, C02B definitions/adapters, `FBattleEngine`, battle state,
the base-damage implementation/tests, module rules, `.uproject`, Unreal config,
content/assets, Git metadata, or unrelated sprite/importer work.

### Frozen C02A contracts

- `FPokemonStatValues` is the common six-value source block.
  `FPokemonStatInputs` owns Level, base stats, IVs, EVs, and C01's resolved
  `FNatureStatModifier`. It owns no nature ID or authored row.
- `FBattleStatCalculator::TryCalculatePermanentStats` validates Level `1..100`,
  positive base stats, IVs `0..31`, EVs `0..252`, total EV at most `510`, the
  resolved nature shape, and `int32` result representability. Rejection is
  atomic, resets the output, and returns `EBattleStatCalculationError`.
- Permanent HP and non-HP values use the exact B00B integer formulas. Base HP
  exactly `1` produces Max HP `1`. Nature multiplication uses the frozen
  `11/10`, `9/10`, or `10/10` rational and floors only at the final nature
  step.
- `FBattleStatStages` privately owns Attack, Defense, Special Attack, Special
  Defense, Speed, Accuracy, and Evasion. Every stage starts at zero and all
  changes clamp to `-6..+6`. A result reports Applied, Blocked, or Invalid plus
  previous, requested, actual, final, and clamped values.
- Effective non-HP stat queries apply the canonical stage ratio without
  changing permanent stats. Accuracy uses
  `clamp(attacker Accuracy - defender Evasion, -6, +6)`, its separate table,
  integer floor, and no clamp to 100.
- The current queries consume no transient Ability, item, status, weather, or
  field modifier. Later packages may supply typed transient modifiers without
  rewriting these permanent facts or stage state.
- C02A public headers and runtime code contain no `UObject`, `UDataTable`,
  Actor, World, presentation, asset-loading, or random-number dependency.

### Validation evidence

- `PokemonSolarusEditor Win64 Development` succeeded with 0 compiler warning
  lines and 0 compiler error lines. The build compiled both new runtime units,
  both new focused test units, and the module units affected by the expanded
  `BattleStats.h`. Log:
  `Game/Saved/Logs/C02A-EditorBuild-20260820T134917Z.log`, SHA-256
  `b96babe86220d42e57eac3cb781d5a426babb212e6fbebfc18067de549b36f1f`.
- `PokemonSolarus.Battle.C02A` performed exactly seven tests: 7 succeeded, 0
  with warnings, 0 failed, and 0 not run; process exit code 0. Report:
  `Game/Saved/Automation/C02A-Stats-20260820T134951Z/index.json`, SHA-256
  `e21f7173c4a1d69883b8481b434bd86e326f22a36c521dbd71a399f5e6b735a8`.
  Log: `Game/Saved/Logs/C02A-Stats-20260820T134951Z.log`, SHA-256
  `5d424edd8469bcd3aae3d0c0326ab19240023eb2ab4a951ffc9781ccda4293fa`.
- The focused tests cover Levels 1, 50, and 100; neutral, boosted, and reduced
  natures; HP/non-HP rounding; base HP one; every base/IV/EV field boundary;
  total EV 510/511; unrepresentable-output rejection; all thirteen battle-stat
  and accuracy ratios; cap/floor outcomes; combined-stage clamping; and proof
  that effective queries leave permanent stats unchanged.
- Per the user's instruction to run only tests related to this implementation,
  the protected `PokemonSolarus.Battle.DamageCalculator` filter and the full
  `PokemonSolarus.Battle` suite were not run. Their source/test hashes remained
  unchanged. This handoff does not present their earlier results as fresh C02A
  runtime evidence.
- `BattleSetupTypes.h`, calculator source/tests, module rules, `.uproject`, and
  `DefaultEngine.ini` matched their pre-run hashes. The config remained at
  SHA-256
  `cc089de5c5094ab055af54e81f4b518e799afa9adaebf542e0d45bea46ba2920`;
  no generated Android File Server edit appeared.
- The JSON report contains no warning or failure. Separate startup noise was
  optional profiler/GPU-capture/tablet DLL messages, unavailable EOS
  anti-cheat, and Unreal's own `UE::UnifiedErrorTest` diagnostics. No C02A or
  Automation issue line was found.

Final C02A source/test hashes:

| File | SHA-256 |
|---|---|
| `Game/Source/PokemonSolarus/Public/Battle/BattleStats.h` | `cf071c134ac9255e2906c993e7e469340e0af0c2ed3ad2c91c15091b6dfcc0ad` |
| `Game/Source/PokemonSolarus/Public/Battle/BattleStatStages.h` | `14a6e396d1ccfd41d6b8733d44405696011027db05c6784464801e630a5142cc` |
| `Game/Source/PokemonSolarus/Public/Battle/BattleStatCalculator.h` | `21a997db2ce8b8cf52d131fdc64e4bf28ad51aa32f221847f77c2eb76648496f` |
| `Game/Source/PokemonSolarus/Private/Battle/BattleStatStages.cpp` | `7d0a186a11b3abc66ab135b64aa442e8b2493a444aa3e5c0a9d0d1b3e9f09b08` |
| `Game/Source/PokemonSolarus/Private/Battle/BattleStatCalculator.cpp` | `48bb03ffb3fe5637474ef31bb54c7222e833e3acb4106879f8e96decd28c702e` |
| `Game/Source/PokemonSolarus/Private/Tests/BattleStatCalculatorTests.cpp` | `ce3a249a23fec886412e763afaefb190e16969486037176ac084911ad390573d` |
| `Game/Source/PokemonSolarus/Private/Tests/BattleStatStagesTests.cpp` | `5e6b7b92b92e1fcf81b26c2ee86d8160f8eb44c5189fd18dd899a6fad510d45d` |

## C02B Completion Handoff

Status date: 2026-08-20  
Successful focused run ID: `C02B-Definitions-20260820T143547Z`  
Successful build run ID: `C02B-EditorBuild-20260820T143528Z`  
C02B status: Complete under user-limited focused validation  
C02 status: Complete under user-limited focused validation  
Next package: C03A in a new session

### Owned files

C02B added only these runtime, adapter, and focused-test files:

- `Game/Source/PokemonSolarus/Public/Battle/BattleDefinitions.h`
- `Game/Source/PokemonSolarus/Public/Battle/BattleTypeChart.h`
- `Game/Source/PokemonSolarus/Private/Battle/BattleTypeChart.cpp`
- `Game/Source/PokemonSolarus/Public/Battle/BattleDefinitionCatalog.h`
- `Game/Source/PokemonSolarus/Private/Battle/BattleDefinitionCatalog.cpp`
- `Game/Source/PokemonSolarus/Public/Battle/BattleDataTableRows.h`
- `Game/Source/PokemonSolarus/Public/Battle/BattleDataTableAdapter.h`
- `Game/Source/PokemonSolarus/Private/Battle/BattleDataTableAdapter.cpp`
- `Game/Source/PokemonSolarus/Private/Tests/BattleTypeChartTests.cpp`
- `Game/Source/PokemonSolarus/Private/Tests/BattleDefinitionCatalogTests.cpp`
- `Game/Source/PokemonSolarus/Private/Tests/BattleDataTableAdapterTests.cpp`

This handoff and `00-roadmap-index.md` are the only status documents updated.
C02B did not edit C01, C02A, `FBattleEngine`, battle state, the base-damage
implementation/tests, module rules, `.uproject`, Unreal config, content/assets,
Git metadata, or unrelated sprite/importer work. No production JSON or Data
Table asset was added: C02B freezes the row/adapter contract, while authored
proof content remains a later package.

### Frozen C02B contracts

- `EPokemonType` is the modern 18-type set in B00B order. `FBattleTypeChart`
  atomically accepts exactly one valid entry for each of the 324 attacking and
  defending pairs. It stores the four legal single-type values exactly and
  multiplies ordered dual types without intermediate rounding; duplicate
  defending types are rejected.
- `FNatureId` and the plain definition records cover species/forms, natures,
  moves, Abilities, held/battle/capture items, and conditions. Move records own
  type, category, power, literal always-hit state or numeric accuracy, PP,
  priority, target class, flags, and an ordered reusable effect list.
- `FBattleMoveEffectDescriptor` represents damage, condition/stage changes,
  healing, drain, recoil, multi-hit count, field/side changes, switching, item
  changes, charging, recharge, Protect, and semi-invulnerability through typed
  kinds plus target, chance, magnitude, duration, count/layer, and flag fields.
- `FBattleDefinitionCatalog::TryCreate` copies and lexically canonicalizes each
  definition family, validates identities, cross-references, ranges, effect
  order/compatibility, and the complete type chart, then publishes only a
  fully valid immutable-by-interface snapshot. Failure resets the output and
  returns sorted typed diagnostics, with no partial definitions exposed.
- Core definition and catalog headers contain no `UObject`, `UDataTable`,
  `FTableRowBase`, asset-loading, presentation, or random-number dependency.
- Unreal-facing row `USTRUCT`s live only in `BattleDataTableRows.h`, and every
  top-level table row inherits `FTableRowBase`. The Data Table row Name is the
  stable definition ID. `FBattleDataTableAdapter::BuildCatalog` visits copied
  row names in lexical order, uses each `FindRow<T>` pointer only inside the
  current call, copies nested arrays into plain definitions, and retains no
  table or row pointer.

### Validation evidence

- `PokemonSolarusEditor Win64 Development` succeeded with no compiler warning
  or error line. Log:
  `Game/Saved/Logs/C02B-EditorBuild-20260820T143528Z.log`, SHA-256
  `36031833f8a0ef0e7b7395407bbc63a7d2d622d01e43f273357c9bf19c52fff5`.
- The exact filter `PokemonSolarus.Battle.C02B` discovered and performed seven
  tests: 7 succeeded, 0 with warnings, 0 failed, and 0 not run; process exit
  code 0. Report:
  `Game/Saved/Automation/C02B-Definitions-20260820T143547Z/index.json`,
  SHA-256
  `14cb472bdf3af3e963bf608f07d596275d9b39fefd240f53d5e4eb63d7d15716`.
  Log: `Game/Saved/Logs/C02B-Definitions-20260820T143547Z.log`, SHA-256
  `001bd33c42cb675c8038c15b510ff7bb04823f30583a1d8fc2a8a01c6b456ace`.
- The focused tests verify all 324 B00B chart cells; immunity, resistance,
  neutrality, super-effectiveness, dual products, and chart rejection; the
  required move/category/target shapes; catalog identity, reference, range,
  effect-order, incompatibility, and atomic-failure behavior; deterministic
  definition/diagnostic ordering; reflected row/table diagnostics; JSON
  import; deep row copying; and source-mutation isolation.
- Per the user's explicit instruction, no C01, C02A, base-damage, or full
  `PokemonSolarus.Battle` test filter was run. Their relevant source/test hashes
  remained unchanged, but this handoff makes no fresh runtime regression claim
  for those suites. This is the approved exception to the roadmap's usual full
  battle-suite completion step.
- `CLAUDE.md`, the Solarus handoff, B00B snapshot, C01/C02A/base-damage source
  and tests, module rules, `.uproject`, and `DefaultEngine.ini` all matched
  their pre-run hashes after Unreal. The config remained at SHA-256
  `cc089de5c5094ab055af54e81f4b518e799afa9adaebf542e0d45bea46ba2920`;
  the existing Android File Server block was unchanged.
- The successful JSON report contains no test warning or error. Separate
  startup noise was limited to missing optional profiler/GPU-capture/tablet
  DLLs, unavailable EOS anti-cheat, and Unreal's own
  `UE::UnifiedErrorTest` diagnostics; no C02B or Automation issue line was
  present in the successful run.

Two earlier correction attempts are retained as non-passing evidence rather
than hidden: `C02B-EditorBuild-20260820T143205Z` failed because the adapter
ignored one `[[nodiscard]]` result, and
`C02B-Definitions-20260820T143414Z` exited 1 after two successful tests because
a test fixture appended an element directly from the same `TArray`. Both fixes
stayed inside C02B files. The successful build and seven-test run above are the
acceptance evidence.

Final C02B source/test hashes:

| File | SHA-256 |
|---|---|
| `Game/Source/PokemonSolarus/Public/Battle/BattleDefinitions.h` | `8dde81699b643ce1bed256ee6785d915eaa3617eced2f018320f6d8c8c1ec742` |
| `Game/Source/PokemonSolarus/Public/Battle/BattleTypeChart.h` | `e45eb82aebe5d8f0da3fba970c97ec2334695691b9ab73140e994787ba39bfb2` |
| `Game/Source/PokemonSolarus/Private/Battle/BattleTypeChart.cpp` | `a23fc7dc1ab3649da9b12ec81ae7ca76648a96cd566d06eb01fc23f6d3c95e56` |
| `Game/Source/PokemonSolarus/Public/Battle/BattleDefinitionCatalog.h` | `d1b19327d845405b88b90f85d242cd4d2f8290a688aaf580f729f35de004ce34` |
| `Game/Source/PokemonSolarus/Private/Battle/BattleDefinitionCatalog.cpp` | `b5f48a7742297b36c6c4002efe4fe6b72785810efb7c3a8e124d94f22326d4e6` |
| `Game/Source/PokemonSolarus/Public/Battle/BattleDataTableRows.h` | `8ef550b089d65e53dfa5c874c42c8a3b3a55cb364878144c22152a288abf1d60` |
| `Game/Source/PokemonSolarus/Public/Battle/BattleDataTableAdapter.h` | `80300a74e13bd1e017db64527b4bc2e59201708a65731ca18ec983f1ddca5e5b` |
| `Game/Source/PokemonSolarus/Private/Battle/BattleDataTableAdapter.cpp` | `80a48bfb4454e6b22d6c3e71ff7c35499d73a1438755c99e3b169c5941c31b15` |
| `Game/Source/PokemonSolarus/Private/Tests/BattleTypeChartTests.cpp` | `04a368411083d71698234a813b36a64daf3c50687f07fe15c60f5ee8c61a1720` |
| `Game/Source/PokemonSolarus/Private/Tests/BattleDefinitionCatalogTests.cpp` | `2e414a126dfbd58f643c5446259e69eeb77bae337dd686c5d87ec498d3a755f9` |
| `Game/Source/PokemonSolarus/Private/Tests/BattleDataTableAdapterTests.cpp` | `60626a10c88f5c833809261130a74c6b70c2a6a15eefd1b01db78c0d4d486b17` |
