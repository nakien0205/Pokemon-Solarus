# C02 — Stats, Types, Moves, and Data Adapters

Priority: P0  
Status: C02A complete under user-limited focused validation; C02B not started  
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

Status: Not started

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
Next package: C02B in a new session; C03 remains blocked

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
