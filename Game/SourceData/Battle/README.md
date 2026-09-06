# Source Data

`Game/SourceData/` contains hand-authored source data and the tooling that validates and converts it into runtime Unreal assets.

For Battle data, the editable source of truth is under:

```text
Game/SourceData/Battle/Initial/
```

Do not treat imported `.uasset` DataTables as the authoring source when corresponding source JSON exists.

## Battle data pipeline

```text
Battle/Initial/*.json
        ↓
battle_source_validation.py
        ↓
import_initial_battle_data.py
        ↓
Game/Content/Data/Battle/Initial/*.uasset
        ↓
Battle DataTable adapter / definition catalog / runtime source
        ↓
Battle runtime
```

Validation runs before live Unreal asset mutation.

## Authoritative Battle source

`Battle/Initial/` currently contains:

* `species_forms.json`
* `natures.json`
* `moves.json`
* `abilities.json`
* `items.json`
* `conditions.json`
* `type_chart.json`
* `display_names.json`
* `runtime_scenario.json`

For a Battle content change, begin with the relevant JSON family.

Do not manually edit the corresponding production DataTable to bypass this source pipeline.

## Validation and import tooling

### `Battle/battle_source_validation.py`

Pure deterministic validation of the source bundle.

Start here for:

* invalid or missing fields;
* bad IDs or references;
* enum/flag errors;
* range or compatibility failures;
* source-count/hash/manifest validation;
* failures that occur before Unreal asset work begins.

It has no Unreal dependency and is also exercised by host-side Python tests.

### `Battle/import_initial_battle_data.py`

Coordinates validated source data with Unreal DataTables.

Start here for:

* transient DataTable conversion failures;
* reflected row-struct problems;
* production-table loading;
* import-setting problems;
* save/rollback behavior;
* source-to-DataTable discrepancies.

The current importer has two important groups:

```text
Imported/mutable:
species_forms
natures
moves
abilities
items
conditions
display_names

Validate-only in this importer:
type_chart
runtime_scenario
```

Do not assume all nine source families are written by the same import operation.

### Tooling tests

* `Battle/tests/test_battle_source_validation.py`
* `Battle/tests/test_import_initial_battle_data.py`

Use these when changing validation or import behavior. They are separate from Unreal Battle Automation tests under `Private/Tests/`.

## Routing by problem

### Changing Battle content

Start with:

```text
Battle/Initial/<relevant-family>.json
```

Then run or inspect the validation/import path required by the owning Battle package.

### Validation failure

Start with:

```text
Battle/battle_source_validation.py
```

and the reported source family.

Do not inspect runtime Battle code unless the source bundle validates successfully and the problem remains.

### Importer failure

Start with:

```text
Battle/import_initial_battle_data.py
Battle/tests/test_import_initial_battle_data.py
```

Then inspect only the DataTable family involved.

### Schema or data-shape problem

Inspect:

```text
source JSON
        ↓
battle_source_validation.py
        ↓
Public/Battle/BattleDataTableRows.h
or BattleRuntimeDataTableRows.h
```

depending on which family crosses into Unreal.

Do not turn this README into the schema specification; the validator and reflected row contracts are the authority.

### Runtime data discrepancy

Trace the pipeline in order:

```text
source JSON
→ validation
→ imported DataTable
→ BattleDataTableAdapter / runtime source
→ BattleDefinitionCatalog
→ BattleEngine
```

Do not jump directly to changing Battle mechanics until the data pipeline is known to be correct.

## Related directories

* `Game/Content/Data/Battle/Initial/` — imported production Battle DataTables.
* `Game/Source/PokemonSolarus/Public/Battle/` — reflected/public data and runtime contracts.
* `Game/Source/PokemonSolarus/Private/Battle/` — DataTable/runtime-source implementations.
* `Game/Source/PokemonSolarus/Private/Tests/` — runtime/catalog Battle tests.

## Do not read by default

Do not automatically:

* open every JSON source file;
* inspect binary `.uasset` DataTables;
* manually change generated/imported DataTables instead of source JSON;
* read BattleEngine implementation for a source-validation failure;
* read unrelated SourceData families.

Follow the affected data family through the pipeline only as far as necessary.

---

# `Game/Source/PokemonSolarus/Private/UI/README.md`

# Battle UI Implementation

This folder contains the native implementation of the Battle presentation layer and its runtime composition/input wiring.

The presentation boundary is:

```text
BattleEngine owns mechanics and authoritative state
        ↓
observer-safe Battle snapshot / decision request
        ↓
BattlePresentationAdapter
        ↓
display-ready UI state
        ↓
HUD / command / health widgets
```

UI code must not become a second Battle-rules engine.

## Agent routing rule

Start with the implementation file that owns the presentation responsibility.

Do not read all UI files for every Battle UI issue, and do not move Battle legality, damage, targeting, availability, or other mechanics into UI code.

## Responsibility groups

### Battle state → display state

Start with:

* `BattlePresentationAdapter.cpp`

Use it when converting observer-safe Battle facts into display-ready command or HUD state, including typed unavailable reasons and display validation.

The adapter consumes Battle facts. It should not independently calculate Battle mechanics.

Public contract:

* `../../Public/UI/BattlePresentationAdapter.h`
* `../../Public/UI/BattleHUDDisplayState.h`

### Runtime composition and Battle advancement

Start with:

* `BattleGameMode.cpp`

This is the Battle runtime composition/orchestration boundary. It initializes the runtime source and Battle engine, connects the local player/controller, refreshes presentation, and coordinates Battle progression.

Use it for runtime wiring or the handoff between accepted UI requests and Battle operations.

Battle-rule authority remains in `Private/Battle/` / `Public/Battle/`, not in the GameMode.

### Local input and HUD ownership

Start with:

* `BattlePlayerController.cpp`

Use it for:

* Enhanced Input wiring;
* Battle navigation/Confirm/Cancel routing;
* HUD creation and viewport ownership;
* local presentation readiness;
* forwarding input to the active HUD.

Input routing may select or request an action, but legality remains Battle-owned.

### Top-level command menu

Start with:

* `BattleCommandWidget.cpp`

This owns local Fight / Bag / Pokémon / Run menu behavior such as focus, cardinal navigation, Confirm handling, and emitted local command requests.

It intentionally does not read or mutate Battle-core state.

### Root Battle HUD

Start with:

* `BattleHUDWidget.cpp`

Use it for:

* root HUD lifecycle;
* structural child binding;
* applying complete display state;
* command-widget facade behavior;
* health-panel coordination;
* presentation/input gating.

Do not place Battle mechanics here.

### Pokémon health presentation

Start with:

* `BattlePokemonHealthPanel.cpp`

Use it for health-panel display behavior and visual HP interpolation.

The panel presents authoritative HP values; visual animation must not mutate or redefine Battle HP.

## Related directories

* `Game/Source/PokemonSolarus/Public/UI/` — public contracts for these implementations.
* `Game/Source/PokemonSolarus/Public/Battle/` — authoritative Battle state/decision contracts consumed by presentation.
* `Game/Source/PokemonSolarus/Private/Battle/` — Battle mechanics/runtime implementation.
* `Game/Source/PokemonSolarus/Private/Tests/` — command UI, presentation-adapter, HUD lifecycle, and runtime-presentation tests.
* `Game/Content/UI/Battle/` — authored Battle UMG/content assets.

Current native code references assets such as:

* `WBP_BattleHUD.uasset`
* `WBP_BattleCommandUI.uasset`
* `WBP_BattlePokemonHealthPanel.uasset`

Inspect those assets only when the task actually concerns structural Blueprint bindings or user-requested presentation work.

## Presentation boundary

A useful ownership test is:

```text
Does this value answer "what is true in the Battle?"
    → BattleEngine / Battle contracts own it.

Does this value answer "how should an already-authoritative fact be exposed to the UI?"
    → presentation adapter / UI may own it.

Does this concern local focus, widget lifecycle, animation, or input forwarding?
    → UI may own it.
```

Do not infer mechanics from what happens to be displayed.

## Do not read by default

Do not automatically inspect:

* every file in `Private/Battle/`;
* Widget Blueprint visuals or art assets;
* UI reference art;
* unrelated input assets;
* all presentation tests.

For functional UI work, identify the native presentation seam first and inspect Blueprint/content assets only when the native contract requires it.
