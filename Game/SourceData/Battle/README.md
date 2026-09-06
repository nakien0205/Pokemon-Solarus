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

Then run or inspect the validation/import path required by the current accepted authority and affected data family.

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
