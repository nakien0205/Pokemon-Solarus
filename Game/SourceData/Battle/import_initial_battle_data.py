"""Transactional seven-table importer for the approved C10B Battle bundle.

The module is importable by ordinary CPython because Unreal is loaded only by
the command entry point. Host-side tests exercise the coordinator with a fake
service; the live commandlet uses :class:`UnrealTableService`.
"""

from __future__ import annotations

import hashlib
import json
import os
import shutil
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable, Mapping, Protocol, Sequence

MODULE_DIRECTORY = Path(__file__).resolve().parent
if str(MODULE_DIRECTORY) not in sys.path:
    sys.path.insert(0, str(MODULE_DIRECTORY))

import battle_source_validation as validation


SOURCE_DIRECTORY = Path(__file__).resolve().parent / "Initial"
GAME_DIRECTORY = Path(__file__).resolve().parents[2]
ASSET_DIRECTORY = "/Game/Data/Battle/Initial"
EVIDENCE_PARENT = GAME_DIRECTORY / "Saved" / "AutomationReports"


@dataclass(frozen=True)
class TableSpec:
    family: str
    file_name: str
    asset_name: str
    struct_path: str

    @property
    def asset_path(self) -> str:
        return f"{ASSET_DIRECTORY}/{self.asset_name}"

    @property
    def asset_file(self) -> Path:
        return (
            GAME_DIRECTORY
            / "Content"
            / "Data"
            / "Battle"
            / "Initial"
            / f"{self.asset_name}.uasset"
        )


TABLE_SPECS = {
    spec.family: spec
    for spec in (
        TableSpec("species_forms", "species_forms.json", "DT_InitialBattleSpeciesForms", "/Script/PokemonSolarus.BattleSpeciesFormTableRow"),
        TableSpec("natures", "natures.json", "DT_InitialBattleNatures", "/Script/PokemonSolarus.BattleNatureTableRow"),
        TableSpec("moves", "moves.json", "DT_InitialBattleMoves", "/Script/PokemonSolarus.BattleMoveTableRow"),
        TableSpec("abilities", "abilities.json", "DT_InitialBattleAbilities", "/Script/PokemonSolarus.BattleAbilityTableRow"),
        TableSpec("items", "items.json", "DT_InitialBattleItems", "/Script/PokemonSolarus.BattleItemTableRow"),
        TableSpec("conditions", "conditions.json", "DT_InitialBattleConditions", "/Script/PokemonSolarus.BattleConditionTableRow"),
        TableSpec("type_chart", "type_chart.json", "DT_InitialBattleTypeChart", "/Script/PokemonSolarus.BattleTypeChartTableRow"),
        TableSpec("display_names", "display_names.json", "DT_InitialBattleDisplayNames", "/Script/PokemonSolarus.BattleDisplayNameTableRow"),
        TableSpec("runtime_scenario", "runtime_scenario.json", "DT_BattleRuntimeScenario", "/Script/PokemonSolarus.BattleRuntimeScenarioTableRow"),
    )
}
APPROVED_IMPORT_FAMILIES = validation.MUTABLE_FAMILIES
VALIDATE_ONLY_FAMILIES = validation.VALIDATE_ONLY_FAMILIES
IMPORT_SETTINGS = {
    "import_key_field": "Name",
    "ignore_extra_fields": False,
    "ignore_missing_fields": False,
    "preserve_existing_values": False,
}


class TableService(Protocol):
    def resolve_row_struct(self, object_path: str) -> Any: ...
    def create_transient_table(self) -> Any: ...
    def load_production_table(self, asset_path: str) -> Any: ...
    def fill_from_json(self, table: Any, text: str, row_struct: Any) -> bool: ...
    def export_to_json(self, table: Any) -> str: ...
    def get_row_names(self, table: Any) -> set[str]: ...
    def get_row_struct(self, table: Any) -> Any: ...
    def get_import_settings(self, table: Any) -> Mapping[str, Any]: ...
    def set_import_settings(self, table: Any, settings: Mapping[str, Any]) -> None: ...
    def save_loaded_assets(self, tables: Sequence[Any]) -> bool: ...


class ImportFailure(RuntimeError):
    def __init__(self, family: str, table: str, phase: str, message: str):
        self.family = family
        self.table = table
        self.phase = phase
        super().__init__(f"[{phase}] family={family} table={table}: {message}")


class SaveFailure(ImportFailure):
    """The Unreal process must stop; restore binary backups after it exits."""


@dataclass(frozen=True)
class PreparedTable:
    spec: TableSpec
    text: str
    expected_names: frozenset[str]
    row_struct: Any


@dataclass(frozen=True)
class LiveSnapshot:
    prepared: PreparedTable
    table: Any
    json_text: str
    import_settings: Mapping[str, Any]


@dataclass(frozen=True)
class ImportOutcome:
    validation_result: validation.ValidationResult
    prepared_tables: tuple[PreparedTable, ...]


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def _expected_names(document: object) -> frozenset[str]:
    if not isinstance(document, list):
        return frozenset()
    return frozenset(
        str(row["Name"]).casefold()
        for row in document
        if isinstance(row, dict) and isinstance(row.get("Name"), str)
    )


def _load_validated_bundle(
    source_directory: Path,
    import_families: Sequence[str],
) -> tuple[validation.ValidationResult, dict[str, object], dict[str, bytes]]:
    documents, raw_documents, load_diagnostics = validation.load_source_documents(
        source_directory
    )
    result = validation.validate_documents(
        documents, raw_documents, import_families
    )
    diagnostics = tuple(sorted(set(result.diagnostics + load_diagnostics)))
    if diagnostics:
        first = diagnostics[0]
        table = (
            TABLE_SPECS[first.family].asset_name
            if first.family in TABLE_SPECS
            else "none"
        )
        summary = "; ".join(
            f"{item.family}:{item.row}:{item.field}:{item.code}:{item.message}"
            for item in diagnostics
        )
        raise ImportFailure(first.family, table, "preflight", summary)
    for family, digest in result.source_sha256.items():
        current = hashlib.sha256(
            (source_directory / validation.SOURCE_FILES[family]).read_bytes()
        ).hexdigest().upper()
        if current != digest:
            raise ImportFailure(
                family,
                TABLE_SPECS[family].asset_name,
                "preflight",
                "source bytes changed after validation",
            )
    return result, documents, raw_documents


def stage_transient_tables(
    service: TableService,
    documents: Mapping[str, object],
    raw_documents: Mapping[str, bytes],
    import_families: Sequence[str],
) -> list[PreparedTable]:
    prepared: list[PreparedTable] = []
    for family in import_families:
        spec = TABLE_SPECS[family]
        try:
            row_struct = service.resolve_row_struct(spec.struct_path)
            if row_struct is None:
                raise RuntimeError("reflected row struct did not resolve")
            table = service.create_transient_table()
            service.set_import_settings(table, IMPORT_SETTINGS)
            text = raw_documents[family].decode("utf-8")
            if not service.fill_from_json(table, text, row_struct):
                raise RuntimeError(
                    "transient fill_from_json_string returned false"
                )
            expected_names = _expected_names(documents[family])
            actual_names = service.get_row_names(table)
            if actual_names != expected_names:
                raise RuntimeError(
                    "row-name mismatch; "
                    f"expected {sorted(expected_names)}, got {sorted(actual_names)}"
                )
            if service.get_row_struct(table) != row_struct:
                raise RuntimeError("transient table has the wrong row struct")
        except Exception as error:
            if isinstance(error, ImportFailure):
                raise
            raise ImportFailure(
                family, spec.asset_name, "transient", str(error)
            ) from error
        prepared.append(
            PreparedTable(spec, text, expected_names, row_struct)
        )
    return prepared


def _snapshot_live_tables(
    service: TableService,
    prepared_tables: Sequence[PreparedTable],
) -> list[LiveSnapshot]:
    snapshots: list[LiveSnapshot] = []
    for prepared in prepared_tables:
        spec = prepared.spec
        try:
            table = service.load_production_table(spec.asset_path)
            if table is None:
                raise RuntimeError(
                    "approved production Data Table does not exist"
                )
            if service.get_row_struct(table) != prepared.row_struct:
                raise RuntimeError(
                    "production Data Table has the wrong row struct"
                )
            snapshots.append(
                LiveSnapshot(
                    prepared,
                    table,
                    service.export_to_json(table),
                    dict(service.get_import_settings(table)),
                )
            )
        except Exception as error:
            raise ImportFailure(
                spec.family, spec.asset_name, "snapshot", str(error)
            ) from error
    return snapshots


def _restore_live_snapshots(
    service: TableService,
    snapshots: Sequence[LiveSnapshot],
) -> list[str]:
    errors: list[str] = []
    for snapshot in snapshots:
        try:
            service.set_import_settings(
                snapshot.table, snapshot.import_settings
            )
            if not service.fill_from_json(
                snapshot.table,
                snapshot.json_text,
                snapshot.prepared.row_struct,
            ):
                raise RuntimeError("snapshot fill returned false")
            if (
                service.get_row_struct(snapshot.table)
                != snapshot.prepared.row_struct
            ):
                raise RuntimeError(
                    "restored table has the wrong row struct"
                )
        except Exception as error:
            errors.append(
                f"{snapshot.prepared.spec.family}/"
                f"{snapshot.prepared.spec.asset_name}: {error}"
            )
    return errors


def import_approved_tables(
    service: TableService,
    source_directory: Path,
    import_families: Sequence[str],
    *,
    before_live_load: Callable[
        [Sequence[PreparedTable], validation.ValidationResult], None
    ]
    | None = None,
    after_live_snapshot: Callable[[Sequence[LiveSnapshot]], None] | None = None,
) -> ImportOutcome:
    """Validate, transient-stage, mutate, and save an explicit allowlist."""
    result, documents, raw_documents = _load_validated_bundle(
        source_directory, import_families
    )
    prepared = stage_transient_tables(
        service, documents, raw_documents, import_families
    )
    if before_live_load:
        before_live_load(prepared, result)
    snapshots = _snapshot_live_tables(service, prepared)
    if after_live_snapshot:
        after_live_snapshot(snapshots)
    try:
        for snapshot in snapshots:
            service.set_import_settings(snapshot.table, IMPORT_SETTINGS)
            if not service.fill_from_json(
                snapshot.table,
                snapshot.prepared.text,
                snapshot.prepared.row_struct,
            ):
                raise ImportFailure(
                    snapshot.prepared.spec.family,
                    snapshot.prepared.spec.asset_name,
                    "live-fill",
                    "fill_from_json_string returned false",
                )
            if (
                service.get_row_names(snapshot.table)
                != snapshot.prepared.expected_names
            ):
                raise ImportFailure(
                    snapshot.prepared.spec.family,
                    snapshot.prepared.spec.asset_name,
                    "live-verify",
                    "row-name verification failed",
                )
            if (
                service.get_row_struct(snapshot.table)
                != snapshot.prepared.row_struct
            ):
                raise ImportFailure(
                    snapshot.prepared.spec.family,
                    snapshot.prepared.spec.asset_name,
                    "live-verify",
                    "row struct changed during import",
                )
    except Exception as error:
        restore_errors = _restore_live_snapshots(service, snapshots)
        if restore_errors:
            raise ImportFailure(
                getattr(error, "family", "bundle"),
                getattr(error, "table", "multiple"),
                "rollback",
                f"{error}; restore failures: {'; '.join(restore_errors)}",
            ) from error
        if isinstance(error, ImportFailure):
            raise
        raise ImportFailure(
            "bundle", "multiple", "live-fill", str(error)
        ) from error

    tables = [snapshot.table for snapshot in snapshots]
    if not service.save_loaded_assets(tables):
        raise SaveFailure(
            "bundle",
            "seven-approved-tables",
            "save",
            "save_loaded_assets returned false; stop Unreal and restore "
            "binary backups",
        )
    for snapshot in snapshots:
        if (
            service.get_row_names(snapshot.table)
            != snapshot.prepared.expected_names
            or service.get_row_struct(snapshot.table)
            != snapshot.prepared.row_struct
        ):
            raise SaveFailure(
                snapshot.prepared.spec.family,
                snapshot.prepared.spec.asset_name,
                "post-save-verify",
                "saved table no longer matches the staged rows; stop "
                "Unreal and restore binary backups",
            )
    return ImportOutcome(result, tuple(prepared))


def _assert_evidence_root(evidence_root: Path) -> Path:
    resolved = evidence_root.resolve()
    parent = EVIDENCE_PARENT.resolve()
    if resolved == parent or parent not in resolved.parents:
        raise RuntimeError(
            f"Evidence root must be a unique child of {parent}"
        )
    resolved.mkdir(parents=True, exist_ok=True)
    return resolved


def capture_binary_backups(
    evidence_root: Path,
    source_directory: Path,
    prepared_tables: Sequence[PreparedTable],
    expected_source_sha256: Mapping[str, str],
) -> dict[str, Any]:
    root = _assert_evidence_root(evidence_root)
    backup_directory = root / "asset-backups"
    backup_directory.mkdir(exist_ok=False)
    manifest: dict[str, Any] = {
        "json": [],
        "assets": [],
        "mutableFamilies": [
            item.spec.family for item in prepared_tables
        ],
        "validateOnlyFamilies": list(VALIDATE_ONLY_FAMILIES),
    }
    for family, spec in TABLE_SPECS.items():
        source_path = source_directory / spec.file_name
        asset_path = spec.asset_file
        if not source_path.is_file() or not asset_path.is_file():
            raise RuntimeError(f"Missing source or asset for {family}")
        source_sha256 = _sha256(source_path)
        if source_sha256 != expected_source_sha256.get(family):
            raise RuntimeError(
                f"Source bytes changed before backup for {family}"
            )
        manifest["json"].append(
            {
                "family": family,
                "path": str(source_path),
                "sha256": source_sha256,
            }
        )
        manifest["assets"].append(
            {
                "family": family,
                "path": str(asset_path),
                "sha256": _sha256(asset_path),
            }
        )
    for prepared in prepared_tables:
        source = prepared.spec.asset_file
        destination = backup_directory / source.name
        shutil.copy2(source, destination)
        if _sha256(source) != _sha256(destination):
            raise RuntimeError(
                f"Backup hash mismatch for {prepared.spec.family}"
            )
    (root / "import-baseline-and-backups.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return manifest


def write_live_snapshots(
    evidence_root: Path,
    snapshots: Sequence[LiveSnapshot],
) -> None:
    root = _assert_evidence_root(evidence_root)
    payload = []
    for snapshot in snapshots:
        payload.append(
            {
                "family": snapshot.prepared.spec.family,
                "asset": snapshot.prepared.spec.asset_name,
                "rowStruct": snapshot.prepared.spec.struct_path,
                "importSettings": dict(snapshot.import_settings),
                "json": json.loads(snapshot.json_text),
            }
        )
    (root / "live-table-snapshots.json").write_text(
        json.dumps(
            payload, ensure_ascii=False, indent=2, sort_keys=True
        )
        + "\n",
        encoding="utf-8",
    )


def _restore_verified_binary_files(
    backup_directory: Path,
    expected: Mapping[str, Mapping[str, Any]],
    target_files: Mapping[str, Path],
    families: Sequence[str],
) -> None:
    """Validate every backup before restoring the exact requested targets."""
    if set(expected) != set(families) or set(target_files) != set(families):
        raise RuntimeError(
            "Backup inputs do not cover the exact requested families"
        )
    for family in families:
        backup = backup_directory / target_files[family].name
        if (
            not backup.is_file()
            or _sha256(backup) != expected[family]["sha256"]
        ):
            raise RuntimeError(f"Invalid backup for {family}")
    for family in families:
        target = target_files[family]
        shutil.copy2(backup_directory / target.name, target)
        if _sha256(target) != expected[family]["sha256"]:
            raise RuntimeError(f"Restored hash mismatch for {family}")


def restore_binary_backups(evidence_root: Path) -> None:
    """Restore seven baseline files after the Unreal process has stopped."""
    root = _assert_evidence_root(evidence_root)
    manifest = json.loads(
        (root / "import-baseline-and-backups.json").read_text(
            encoding="utf-8"
        )
    )
    expected = {
        entry["family"]: entry
        for entry in manifest["assets"]
        if entry["family"] in APPROVED_IMPORT_FAMILIES
    }
    targets = {
        family: TABLE_SPECS[family].asset_file
        for family in APPROVED_IMPORT_FAMILIES
    }
    _restore_verified_binary_files(
        root / "asset-backups",
        expected,
        targets,
        APPROVED_IMPORT_FAMILIES,
    )


class UnrealTableService:
    def __init__(self, unreal_module: Any):
        self.unreal = unreal_module

    def resolve_row_struct(self, object_path: str) -> Any:
        value = self.unreal.load_object(None, object_path)
        if value is None or not isinstance(value, self.unreal.ScriptStruct):
            raise RuntimeError(
                f"Could not resolve ScriptStruct {object_path}"
            )
        return value

    def create_transient_table(self) -> Any:
        table = self.unreal.new_object(type=self.unreal.DataTable)
        if table is None:
            raise RuntimeError(
                "unreal.new_object(type=unreal.DataTable) returned None"
            )
        return table

    def load_production_table(self, asset_path: str) -> Any:
        if not self.unreal.EditorAssetLibrary.does_asset_exist(asset_path):
            raise RuntimeError(
                f"Approved Data Table is missing: {asset_path}"
            )
        table = self.unreal.EditorAssetLibrary.load_asset(asset_path)
        if not isinstance(table, self.unreal.DataTable):
            raise RuntimeError(
                f"Approved asset is not a Data Table: {asset_path}"
            )
        return table

    def fill_from_json(self, table: Any, text: str, row_struct: Any) -> bool:
        return bool(table.fill_from_json_string(text, row_struct))

    def export_to_json(self, table: Any) -> str:
        return str(table.export_to_json_string())

    def get_row_names(self, table: Any) -> set[str]:
        return {
            str(name).casefold() for name in table.get_row_names()
        }

    def get_row_struct(self, table: Any) -> Any:
        return table.get_row_struct()

    def get_import_settings(self, table: Any) -> Mapping[str, Any]:
        return {
            key: table.get_editor_property(key)
            for key in IMPORT_SETTINGS
        }

    def set_import_settings(
        self, table: Any, settings: Mapping[str, Any]
    ) -> None:
        for key, value in settings.items():
            table.set_editor_property(key, value)

    def save_loaded_assets(self, tables: Sequence[Any]) -> bool:
        return bool(
            self.unreal.EditorAssetLibrary.save_loaded_assets(
                list(tables), only_if_is_dirty=False
            )
        )


def main() -> None:
    mode = os.environ.get("C10B_IMPORT_MODE", "").casefold()
    evidence_value = os.environ.get("C10B_EVIDENCE_ROOT")
    if mode == "restore":
        if not evidence_value:
            raise RuntimeError(
                "C10B_EVIDENCE_ROOT is required for restore mode"
            )
        restore_binary_backups(Path(evidence_value))
        return
    if mode not in {"probe", "import"}:
        raise RuntimeError(
            "C10B_IMPORT_MODE must be 'probe', 'import', or 'restore'"
        )

    import unreal

    service = UnrealTableService(unreal)
    if mode == "probe":
        _, documents, raws = _load_validated_bundle(
            SOURCE_DIRECTORY, APPROVED_IMPORT_FAMILIES
        )
        prepared = stage_transient_tables(
            service, documents, raws, APPROVED_IMPORT_FAMILIES
        )
        unreal.log(
            "C10B transient probe passed: "
            + ", ".join(item.spec.family for item in prepared)
        )
        return
    if not evidence_value:
        raise RuntimeError(
            "C10B_EVIDENCE_ROOT is required for import mode"
        )
    evidence_root = Path(evidence_value)
    outcome = import_approved_tables(
        service,
        SOURCE_DIRECTORY,
        APPROVED_IMPORT_FAMILIES,
        before_live_load=lambda tables, result: capture_binary_backups(
            evidence_root,
            SOURCE_DIRECTORY,
            tables,
            result.source_sha256,
        ),
        after_live_snapshot=lambda snapshots: write_live_snapshots(
            evidence_root, snapshots
        ),
    )
    (evidence_root / "import-result.json").write_text(
        json.dumps(
            {
                "status": "success",
                "families": [
                    item.spec.family for item in outcome.prepared_tables
                ],
                "tables": [
                    item.spec.asset_name for item in outcome.prepared_tables
                ],
                "sourceSha256": dict(
                    outcome.validation_result.source_sha256
                ),
            },
            indent=2,
            sort_keys=True,
        )
        + "\n",
        encoding="utf-8",
    )
    for item in outcome.prepared_tables:
        unreal.log(f"Imported {item.spec.asset_path}")


if __name__ == "__main__":
    main()
