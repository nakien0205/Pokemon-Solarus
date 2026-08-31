from __future__ import annotations

import json
import shutil
import sys
import tempfile
import unittest
from pathlib import Path


BATTLE_SOURCE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(BATTLE_SOURCE))

import import_initial_battle_data as importer  # noqa: E402


DISPLAY_NAMES = [
    {"Name": f"Species.{name}", "DisplayName": name}
    for name in (
        "Charizard",
        "Venusaur",
        "Gyarados",
        "Rotom",
        "Pelipper",
        "Espathra",
        "Clefable",
        "Excadrill",
    )
]


class FakeTable:
    def __init__(self, *, production=False, family="", row_struct=None):
        self.production = production
        self.family = family
        self.row_struct = row_struct
        self.rows = [{"Name": f"Baseline.{family}"}]
        self.settings = (
            {
                "import_key_field": f"Original.{family}",
                "ignore_extra_fields": True,
                "ignore_missing_fields": True,
                "preserve_existing_values": True,
            }
            if production
            else {}
        )


class FakeTableService:
    def __init__(
        self,
        *,
        transient_failure=None,
        live_failure=None,
        save_failure=False,
        post_save_corruption=None,
    ):
        self.transient_failure = transient_failure
        self.live_failure = live_failure
        self.live_failure_used = False
        self.save_failure = save_failure
        self.post_save_corruption = post_save_corruption
        self.production_loads = []
        self.live_fills = []
        self.restore_fills = []
        self.save_calls = []
        self.tables = {}
        for family in importer.APPROVED_IMPORT_FAMILIES:
            spec = importer.TABLE_SPECS[family]
            self.tables[spec.asset_path] = FakeTable(
                production=True,
                family=family,
                row_struct=spec.struct_path,
            )
        self.original_rows = {
            path: list(table.rows) for path, table in self.tables.items()
        }
        self.original_settings = {
            path: dict(table.settings)
            for path, table in self.tables.items()
        }

    def resolve_row_struct(self, object_path):
        return object_path

    def create_transient_table(self):
        return FakeTable()

    def load_production_table(self, asset_path):
        self.production_loads.append(asset_path)
        return self.tables[asset_path]

    def fill_from_json(self, table, text, row_struct):
        rows = json.loads(text)
        family = next(
            (
                family
                for family, spec in importer.TABLE_SPECS.items()
                if spec.struct_path == row_struct
            ),
            "unknown",
        )
        if not table.production and family == self.transient_failure:
            return False
        is_restore = (
            table.production
            and rows
            and str(rows[0].get("Name", "")).startswith("Baseline.")
        )
        if is_restore:
            self.restore_fills.append(table.family)
        elif table.production:
            self.live_fills.append(table.family)
        if (
            table.production
            and family == self.live_failure
            and not self.live_failure_used
            and not is_restore
        ):
            self.live_failure_used = True
            return False
        table.row_struct = row_struct
        table.rows = rows
        return True

    def export_to_json(self, table):
        return json.dumps(table.rows)

    def get_row_names(self, table):
        return {str(row["Name"]).casefold() for row in table.rows}

    def get_row_struct(self, table):
        return table.row_struct

    def get_import_settings(self, table):
        return dict(table.settings)

    def set_import_settings(self, table, settings):
        table.settings = dict(settings)

    def save_loaded_assets(self, tables):
        self.save_calls.append(list(tables))
        if self.save_failure:
            return False
        for table in tables:
            if table.family == self.post_save_corruption:
                table.rows = [{"Name": f"Corrupted.{table.family}"}]
                break
        return True


class ImportInitialBattleDataTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.source_directory = Path(self.temporary.name)
        for spec in importer.TABLE_SPECS.values():
            shutil.copy2(
                BATTLE_SOURCE / "Initial" / spec.file_name,
                self.source_directory / spec.file_name,
            )
        (self.source_directory / "display_names.json").write_text(
            json.dumps(DISPLAY_NAMES, indent=2) + "\n",
            encoding="utf-8",
        )

    def tearDown(self):
        self.temporary.cleanup()

    def run_import(self, service):
        return importer.import_approved_tables(
            service,
            self.source_directory,
            importer.APPROVED_IMPORT_FAMILIES,
        )

    def test_preflight_failure_performs_no_unreal_work(self):
        moves_path = self.source_directory / "moves.json"
        moves = json.loads(moves_path.read_text(encoding="utf-8"))
        moves[0]["UnknownField"] = True
        moves_path.write_text(json.dumps(moves), encoding="utf-8")
        service = FakeTableService()
        with self.assertRaises(importer.ImportFailure) as raised:
            self.run_import(service)
        self.assertIn("preflight", str(raised.exception))
        self.assertEqual("moves", raised.exception.family)
        self.assertEqual(
            "DT_InitialBattleMoves", raised.exception.table
        )
        self.assertIn("field.extra", str(raised.exception))
        self.assertEqual([], service.production_loads)
        self.assertEqual([], service.live_fills)
        self.assertEqual([], service.save_calls)

    def test_transient_failure_performs_no_live_mutation(self):
        service = FakeTableService(transient_failure="moves")
        with self.assertRaises(importer.ImportFailure) as raised:
            self.run_import(service)
        self.assertIn("family=moves", str(raised.exception))
        self.assertIn("DT_InitialBattleMoves", str(raised.exception))
        self.assertEqual([], service.production_loads)
        self.assertEqual([], service.live_fills)
        self.assertEqual([], service.save_calls)

    def test_allowlist_is_exactly_seven_and_excludes_validate_only(self):
        self.assertEqual(
            (
                "species_forms",
                "natures",
                "moves",
                "abilities",
                "items",
                "conditions",
                "display_names",
            ),
            importer.APPROVED_IMPORT_FAMILIES,
        )
        self.assertEqual(
            ("type_chart", "runtime_scenario"),
            importer.VALIDATE_ONLY_FAMILIES,
        )
        self.assertTrue(
            set(importer.APPROVED_IMPORT_FAMILIES).isdisjoint(
                importer.VALIDATE_ONLY_FAMILIES
            )
        )

    def test_live_fill_failure_restores_all_tables_and_saves_nothing(self):
        service = FakeTableService(live_failure="abilities")
        with self.assertRaises(importer.ImportFailure) as raised:
            self.run_import(service)
        self.assertIn("family=abilities", str(raised.exception))
        self.assertIn("DT_InitialBattleAbilities", str(raised.exception))
        self.assertEqual(
            list(importer.APPROVED_IMPORT_FAMILIES),
            service.restore_fills,
        )
        self.assertEqual([], service.save_calls)
        for path, rows in service.original_rows.items():
            self.assertEqual(rows, service.tables[path].rows)
            self.assertEqual(
                service.original_settings[path],
                service.tables[path].settings,
            )

    def test_save_failure_is_typed_and_saves_only_the_seven_tables(self):
        service = FakeTableService(save_failure=True)
        with self.assertRaises(importer.SaveFailure) as raised:
            self.run_import(service)
        self.assertEqual("bundle", raised.exception.family)
        self.assertEqual("seven-approved-tables", raised.exception.table)
        self.assertEqual(1, len(service.save_calls))
        self.assertEqual(
            list(importer.APPROVED_IMPORT_FAMILIES),
            [table.family for table in service.save_calls[0]],
        )

    def test_post_save_mismatch_is_typed_and_identifies_the_table(self):
        service = FakeTableService(post_save_corruption="conditions")
        with self.assertRaises(importer.SaveFailure) as raised:
            self.run_import(service)
        self.assertEqual("conditions", raised.exception.family)
        self.assertEqual(
            "DT_InitialBattleConditions",
            raised.exception.table,
        )
        self.assertIn("post-save-verify", str(raised.exception))
        self.assertEqual(1, len(service.save_calls))
        self.assertEqual(
            list(importer.APPROVED_IMPORT_FAMILIES),
            [table.family for table in service.save_calls[0]],
        )

    def test_binary_restore_verifies_then_restores_exactly_seven_files(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            backup_directory = root / "asset-backups"
            backup_directory.mkdir()
            expected = {}
            targets = {}
            for index, family in enumerate(
                importer.APPROVED_IMPORT_FAMILIES
            ):
                target = root / f"{index}-{family}.uasset"
                backup = backup_directory / target.name
                baseline = f"baseline:{family}".encode("utf-8")
                target.write_bytes(baseline)
                backup.write_bytes(baseline)
                expected[family] = {"sha256": importer._sha256(backup)}
                targets[family] = target
                target.write_bytes(f"changed:{family}".encode("utf-8"))

            validate_only = root / "validate-only.uasset"
            validate_only.write_bytes(b"unchanged")
            importer._restore_verified_binary_files(
                backup_directory,
                expected,
                targets,
                importer.APPROVED_IMPORT_FAMILIES,
            )

            for family, target in targets.items():
                self.assertEqual(
                    expected[family]["sha256"],
                    importer._sha256(target),
                )
            self.assertEqual(b"unchanged", validate_only.read_bytes())

    def test_success_saves_exactly_the_seven_approved_tables(self):
        service = FakeTableService()
        outcome = self.run_import(service)
        self.assertEqual(
            list(importer.APPROVED_IMPORT_FAMILIES),
            [item.spec.family for item in outcome.prepared_tables],
        )
        self.assertTrue(outcome.validation_result.is_valid)
        self.assertEqual(1, len(service.save_calls))
        saved = service.save_calls[0]
        self.assertEqual(7, len(saved))
        self.assertEqual(
            list(importer.APPROVED_IMPORT_FAMILIES),
            [table.family for table in saved],
        )
        self.assertNotIn("type_chart", service.live_fills)
        self.assertNotIn("runtime_scenario", service.live_fills)


if __name__ == "__main__":
    unittest.main()
