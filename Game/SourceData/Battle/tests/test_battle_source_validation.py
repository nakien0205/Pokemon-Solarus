from __future__ import annotations

import copy
import json
import sys
import unittest
from pathlib import Path


BATTLE_SOURCE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(BATTLE_SOURCE))

import battle_source_validation as validation  # noqa: E402


DISPLAY_NAMES = {
    "Species.Charizard": "Charizard",
    "Species.Venusaur": "Venusaur",
    "Species.Gyarados": "Gyarados",
    "Species.Rotom": "Rotom",
    "Species.Pelipper": "Pelipper",
    "Species.Espathra": "Espathra",
    "Species.Clefable": "Clefable",
    "Species.Excadrill": "Excadrill",
}


class BattleSourceValidationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        documents, raw_documents, diagnostics = validation.load_source_documents(
            BATTLE_SOURCE / "Initial"
        )
        if diagnostics:
            raise AssertionError(diagnostics)
        documents["display_names"] = [
            {"Name": name, "DisplayName": display_name}
            for name, display_name in DISPLAY_NAMES.items()
        ]
        raw_documents["display_names"] = json.dumps(
            documents["display_names"], ensure_ascii=False
        ).encode("utf-8")
        cls.valid_documents = documents
        cls.valid_raw_documents = raw_documents

    def validate(
        self,
        mutate=None,
        *,
        raw_mutate=None,
        families=validation.MUTABLE_FAMILIES,
        manifest=validation.ACCEPTED_PINNED_MANIFEST_SHA256,
    ):
        documents = copy.deepcopy(self.valid_documents)
        raw_documents = dict(self.valid_raw_documents)
        if mutate:
            mutate(documents)
        if raw_mutate:
            raw_mutate(raw_documents)
        before = copy.deepcopy(documents)
        result = validation.validate_documents(
            documents,
            raw_documents,
            families,
            pinned_manifest_sha256=manifest,
        )
        self.assertEqual(before, documents, "validation mutated an input document")
        return result

    @staticmethod
    def codes(result):
        return {item.code for item in result.diagnostics}

    def assert_diagnostic(
        self, result, *, family, row, field, code
    ):
        matches = [
            item
            for item in result.diagnostics
            if item.family == family
            and item.row == row
            and item.field == field
            and item.code == code
        ]
        self.assertEqual(
            1,
            len(matches),
            f"Missing precise diagnostic in {result.diagnostics}",
        )
        self.assertTrue(matches[0].message)
        self.assertEqual(tuple(sorted(result.diagnostics)), result.diagnostics)

    @staticmethod
    def move(docs, name):
        return next(row for row in docs["moves"] if row["Name"] == name)

    def test_complete_bundle_succeeds_without_mutation(self):
        result = self.validate()
        self.assertTrue(result.is_valid, result.diagnostics)
        self.assertEqual(
            324,
            sum(
                len(row["Entries"])
                for row in self.valid_documents["type_chart"]
            ),
        )
        self.assertEqual(validation.EXPECTED_COUNTS, result.counts)

    def test_case_folded_duplicate_is_rejected(self):
        result = self.validate(
            lambda docs: docs["abilities"].__setitem__(
                1, {"Name": docs["abilities"][0]["Name"].swapcase()}
            )
        )
        duplicate = self.valid_documents["abilities"][0]["Name"].swapcase()
        self.assert_diagnostic(
            result,
            family="abilities",
            row=duplicate,
            field="Name",
            code="identity.duplicate",
        )

    def test_missing_and_extra_fields_are_rejected(self):
        def mutate(docs):
            docs["items"][0].pop("Kind")
            docs["items"][0]["Unexpected"] = 1

        result = self.validate(mutate)
        row = self.valid_documents["items"][0]["Name"]
        self.assert_diagnostic(
            result, family="items", row=row, field="Kind", code="field.missing"
        )
        self.assert_diagnostic(
            result, family="items", row=row, field="Unexpected", code="field.extra"
        )

    def test_wrong_identity_family_is_rejected(self):
        result = self.validate(
            lambda docs: docs["species_forms"][0].__setitem__(
                "Name", "Move.Charizard"
            )
        )
        self.assert_diagnostic(
            result,
            family="species_forms",
            row="Move.Charizard",
            field="Name",
            code="family.mismatch",
        )

    def test_missing_reference_is_rejected(self):
        result = self.validate(
            lambda docs: docs["species_forms"][0].__setitem__(
                "AbilityIds", ["Ability.Missing"]
            )
        )
        row = self.valid_documents["species_forms"][0]["Name"]
        self.assert_diagnostic(
            result,
            family="species_forms",
            row=row,
            field="AbilityIds[0]",
            code="reference.missing",
        )

    def test_unknown_enum_and_flag_are_rejected(self):
        def mutate(docs):
            docs["moves"][0]["Type"] = "Light"
            docs["moves"][0]["Flags"].append("NotAFlag")

        result = self.validate(mutate)
        row = self.valid_documents["moves"][0]["Name"]
        flag_index = len(self.valid_documents["moves"][0]["Flags"])
        self.assert_diagnostic(
            result, family="moves", row=row, field="Type", code="enum.unknown"
        )
        self.assert_diagnostic(
            result,
            family="moves",
            row=row,
            field=f"Flags[{flag_index}]",
            code="flag.unknown",
        )

    def test_bad_numeric_range_is_rejected(self):
        result = self.validate(
            lambda docs: docs["moves"][0].__setitem__("Accuracy", 101)
        )
        self.assert_diagnostic(
            result,
            family="moves",
            row=self.valid_documents["moves"][0]["Name"],
            field="Accuracy",
            code="range.invalid",
        )

    def test_bad_target_is_rejected(self):
        result = self.validate(
            lambda docs: docs["moves"][0]["Effects"][0].__setitem__(
                "Target", "User"
            )
        )
        self.assert_diagnostic(
            result,
            family="moves",
            row=self.valid_documents["moves"][0]["Name"],
            field="Effects[0].Target",
            code="target.incompatible",
        )

    def test_malformed_effect_and_order_are_rejected(self):
        def mutate(docs):
            effects = docs["moves"][0]["Effects"]
            effects[0].pop("ChanceDenominator")
            effects[1]["Order"] = effects[0]["Order"]

        result = self.validate(mutate)
        row = self.valid_documents["moves"][0]["Name"]
        self.assert_diagnostic(
            result,
            family="moves",
            row=row,
            field="Effects[0].ChanceDenominator",
            code="field.missing",
        )
        self.assert_diagnostic(
            result,
            family="moves",
            row=row,
            field="Effects.Order",
            code="effect.order",
        )

    def test_wrong_document_and_type_pair_counts_are_rejected(self):
        def mutate(docs):
            docs["species_forms"].pop()
            docs["type_chart"][0]["Entries"].pop()

        result = self.validate(mutate)
        self.assert_diagnostic(
            result,
            family="species_forms",
            row="document",
            field="count",
            code="count.mismatch",
        )
        self.assert_diagnostic(
            result,
            family="type_chart",
            row="document",
            field="Entries",
            code="count.mismatch",
        )

    def test_accepted_hash_and_manifest_identity_are_enforced(self):
        result = self.validate(
            raw_mutate=lambda raws: raws.__setitem__(
                "moves", raws["moves"] + b" "
            ),
            manifest="0" * 64,
        )
        self.assert_diagnostic(
            result,
            family="moves",
            row="document",
            field="sha256",
            code="hash.mismatch",
        )
        self.assert_diagnostic(
            result,
            family="manifest",
            row="pinned",
            field="sha256",
            code="manifest.mismatch",
        )

    def test_allowlist_rejects_unknown_duplicate_validate_only_and_incomplete(self):
        result = self.validate(
            families=(
                "species_forms",
                "species_forms",
                "type_chart",
                "unknown",
            )
        )
        codes = self.codes(result)
        self.assertIn("allowlist.duplicate", codes)
        self.assertIn("allowlist.validate_only", codes)
        self.assertIn("allowlist.unknown", codes)
        self.assertIn("allowlist.incomplete", codes)
        self.assert_diagnostic(
            result,
            family="import",
            row="1",
            field="family",
            code="allowlist.duplicate",
        )
        self.assert_diagnostic(
            result,
            family="import",
            row="2",
            field="family",
            code="allowlist.validate_only",
        )
        self.assert_diagnostic(
            result,
            family="import",
            row="allowlist",
            field="families",
            code="allowlist.incomplete",
        )

    def test_catalog_semantic_rules_fail_before_import(self):
        cases = (
            (
                "per-hit damage",
                lambda docs: self.move(docs, "Move.Flamethrower")["Effects"][0]["Flags"].append("PerHit"),
                "Move.Flamethrower",
                "Effects[0].Flags",
            ),
            (
                "removal target family",
                lambda docs: self.move(docs, "Move.RapidSpin")["Effects"][1].__setitem__("Target", "Field"),
                "Move.RapidSpin",
                "Effects[1].Target",
            ),
            (
                "field target class",
                lambda docs: self.move(docs, "Move.SunnyDay").__setitem__("TargetClass", "SelectedOpponent"),
                "Move.SunnyDay",
                "Effects[0].Target",
            ),
            (
                "held-item operation shape",
                lambda docs: self.move(docs, "Move.KnockOff")["Effects"][-1].__setitem__("HeldItemOperation", "ExchangeCurrent"),
                "Move.KnockOff",
                "Effects.HeldItemOperation",
            ),
            (
                "hit rule flag",
                lambda docs: self.move(docs, "Move.Flamethrower")["Flags"].append("RespectsTypeImmunity"),
                "Move.Flamethrower",
                "Flags",
            ),
            (
                "weather accuracy flag",
                lambda docs: self.move(docs, "Move.Flamethrower")["Flags"].append("RainAlwaysHitsSunAccuracyFifty"),
                "Move.Flamethrower",
                "Flags",
            ),
            (
                "registration payload",
                lambda docs: self.move(docs, "Move.FollowMe")["Effects"][0].__setitem__("MagnitudeNumerator", 1),
                "Move.FollowMe",
                "Effects.RegisterTargetRedirection",
            ),
            (
                "charge primary chance",
                lambda docs: self.move(docs, "Move.SolarBeam")["Effects"][0].__setitem__("ChanceNumerator", 50),
                "Move.SolarBeam",
                "Effects.Charge",
            ),
        )
        for label, mutate, row, field in cases:
            with self.subTest(label=label):
                result = self.validate(mutate)
                self.assert_diagnostic(
                    result,
                    family="moves",
                    row=row,
                    field=field,
                    code=(
                        "target.incompatible"
                        if "target" in label
                        else "effect.incompatible"
                    ),
                )


if __name__ == "__main__":
    unittest.main()
