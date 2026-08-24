"""Import the approved initial Battle JSON into reflected Unreal Data Tables.

Run this script from the Unreal Editor after the PokemonSolarus module, including
BattleRuntimeDataTableRows.h, has compiled successfully.
"""

from __future__ import annotations

import json
from pathlib import Path

import unreal


SOURCE_DIRECTORY = Path(__file__).resolve().parent / "Initial"
ASSET_DIRECTORY = "/Game/Data/Battle/Initial"

TABLES = (
    (
        "abilities.json",
        "DT_InitialBattleAbilities",
        "/Script/PokemonSolarus.BattleAbilityTableRow",
    ),
    (
        "items.json",
        "DT_InitialBattleItems",
        "/Script/PokemonSolarus.BattleItemTableRow",
    ),
    (
        "conditions.json",
        "DT_InitialBattleConditions",
        "/Script/PokemonSolarus.BattleConditionTableRow",
    ),
    (
        "species_forms.json",
        "DT_InitialBattleSpeciesForms",
        "/Script/PokemonSolarus.BattleSpeciesFormTableRow",
    ),
    (
        "natures.json",
        "DT_InitialBattleNatures",
        "/Script/PokemonSolarus.BattleNatureTableRow",
    ),
    (
        "moves.json",
        "DT_InitialBattleMoves",
        "/Script/PokemonSolarus.BattleMoveTableRow",
    ),
    (
        "type_chart.json",
        "DT_InitialBattleTypeChart",
        "/Script/PokemonSolarus.BattleTypeChartTableRow",
    ),
    (
        "display_names.json",
        "DT_InitialBattleDisplayNames",
        "/Script/PokemonSolarus.BattleDisplayNameTableRow",
    ),
    (
        "runtime_scenario.json",
        "DT_BattleRuntimeScenario",
        "/Script/PokemonSolarus.BattleRuntimeScenarioTableRow",
    ),
)


def _load_and_validate_json(source_path: Path) -> tuple[str, set[str]]:
    text = source_path.read_text(encoding="utf-8")
    rows = json.loads(text)
    if not isinstance(rows, list):
        raise RuntimeError(f"{source_path.name} must contain a JSON array")

    names: set[str] = set()
    for index, row in enumerate(rows):
        if not isinstance(row, dict):
            raise RuntimeError(f"{source_path.name} row {index} must be an object")
        name = row.get("Name")
        if not isinstance(name, str) or not name:
            raise RuntimeError(f"{source_path.name} row {index} has no non-empty Name")
        normalized_name = name.casefold()
        if normalized_name in names:
            raise RuntimeError(f"{source_path.name} contains duplicate Name '{name}'")
        names.add(normalized_name)
    return text, names


def _resolve_row_struct(object_path: str) -> unreal.ScriptStruct:
    row_struct = unreal.load_object(None, object_path)
    if row_struct is None:
        raise RuntimeError(
            f"Could not resolve {object_path}; compile the PokemonSolarus module first"
        )
    if not isinstance(row_struct, unreal.ScriptStruct):
        raise RuntimeError(f"{object_path} resolved to a non-ScriptStruct object")
    return row_struct


def _load_or_create_table(
    asset_tools: unreal.AssetTools,
    asset_name: str,
    row_struct: unreal.ScriptStruct,
) -> unreal.DataTable:
    asset_path = f"{ASSET_DIRECTORY}/{asset_name}"
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        table = unreal.EditorAssetLibrary.load_asset(asset_path)
        if not isinstance(table, unreal.DataTable):
            raise RuntimeError(f"{asset_path} exists but is not a Data Table")
        if table.get_row_struct() != row_struct:
            raise RuntimeError(f"{asset_path} has the wrong row struct")
        return table

    factory = unreal.DataTableFactory()
    factory.set_editor_property("struct", row_struct)
    table = asset_tools.create_asset(
        asset_name,
        ASSET_DIRECTORY,
        unreal.DataTable,
        factory,
    )
    if table is None:
        raise RuntimeError(f"Could not create {asset_path}")
    return table


def _replace_rows(
    table: unreal.DataTable,
    text: str,
    expected_names: set[str],
    row_struct: unreal.ScriptStruct,
) -> None:
    if expected_names:
        if not table.fill_from_json_string(text, row_struct):
            raise RuntimeError(
                f"JSON import failed for {table.get_name()}; inspect the Unreal log"
            )
        return

    for row_name in table.get_row_names():
        unreal.DataTableFunctionLibrary.remove_data_table_row(table, row_name)


def main() -> None:
    prepared = []
    for file_name, asset_name, struct_path in TABLES:
        source_path = SOURCE_DIRECTORY / file_name
        text, expected_names = _load_and_validate_json(source_path)
        prepared.append((asset_name, struct_path, text, expected_names))

    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    imported_tables = []
    for asset_name, struct_path, text, expected_names in prepared:
        row_struct = _resolve_row_struct(struct_path)
        table = _load_or_create_table(asset_tools, asset_name, row_struct)
        table.set_editor_property("import_key_field", "Name")
        table.set_editor_property("ignore_extra_fields", False)
        table.set_editor_property("ignore_missing_fields", False)
        table.set_editor_property("preserve_existing_values", False)
        _replace_rows(table, text, expected_names, row_struct)

        actual_names = {str(name).casefold() for name in table.get_row_names()}
        if actual_names != expected_names:
            raise RuntimeError(
                f"{asset_name} row verification failed: "
                f"expected {sorted(expected_names)}, got {sorted(actual_names)}"
            )
        if table.get_row_struct() != row_struct:
            raise RuntimeError(f"{asset_name} changed to the wrong row struct during import")
        imported_tables.append(table)

    if not unreal.EditorAssetLibrary.save_loaded_assets(
        imported_tables,
        only_if_is_dirty=False,
    ):
        raise RuntimeError("Could not save every initial Battle Data Table")
    for table in imported_tables:
        unreal.log(f"Imported {table.get_path_name()}")


if __name__ == "__main__":
    main()
