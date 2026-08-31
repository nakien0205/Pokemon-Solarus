"""Pure, deterministic validation for the canonical Battle source bundle.

This module deliberately has no Unreal dependency.  The importer and host-side
tests both use the same validation entry point before any package is loaded.
"""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
import re
from dataclasses import asdict, dataclass
from pathlib import Path
from types import MappingProxyType
from typing import Any, Mapping, Sequence


SOURCE_FILES = MappingProxyType(
    {
        "species_forms": "species_forms.json",
        "natures": "natures.json",
        "moves": "moves.json",
        "abilities": "abilities.json",
        "items": "items.json",
        "conditions": "conditions.json",
        "type_chart": "type_chart.json",
        "display_names": "display_names.json",
        "runtime_scenario": "runtime_scenario.json",
    }
)
MUTABLE_FAMILIES = (
    "species_forms",
    "natures",
    "moves",
    "abilities",
    "items",
    "conditions",
    "display_names",
)
VALIDATE_ONLY_FAMILIES = ("type_chart", "runtime_scenario")
EXPECTED_COUNTS = MappingProxyType(
    {
        "species_forms": 8,
        "natures": 25,
        "moves": 62,
        "abilities": 8,
        "items": 14,
        "conditions": 40,
        "type_chart": 18,
        "display_names": 8,
        "runtime_scenario": 1,
    }
)
ACCEPTED_C10A_HASHES = MappingProxyType(
    {
        "species_forms": "74A8C58DE106511748055F66C6E3E7D8FB881B04EBF844B2F7528817B08FC9DC",
        "natures": "92DEC15A9727809B496F0DA932052AA5F4AE5704D9954F9A49681257871BB319",
        "moves": "4AA41851A6F031155E492A60157EF60ECA29C742A388C87230F154B5D42B2299",
        "abilities": "5319A7F4BDEA20E5DFBE61544A57927D1C83D29D8893EDC65776F43BA9EACD6F",
        "items": "78980395765FB8A465BDEA600B92A394C381433A38598E233C1EB395B760E378",
        "conditions": "2119768D8A4D43762DD90D7872EFA5AED4558DACD3B5611ECD69C9E28BD3AB27",
    }
)
ACCEPTED_PINNED_MANIFEST_SHA256 = (
    "721EE44EF5D0EF6B9522D4BA1A94546D7032D1F17E1CAE812D52C4EC8D986629"
)

TYPES = (
    "Normal", "Fire", "Water", "Electric", "Grass", "Ice", "Fighting",
    "Poison", "Ground", "Flying", "Psychic", "Bug", "Rock", "Ghost",
    "Dragon", "Dark", "Steel", "Fairy",
)
NATURE_STATS = {"None", "Attack", "Defense", "SpecialAttack", "SpecialDefense", "Speed"}
BATTLE_STATS = {"None", "Attack", "Defense", "SpecialAttack", "SpecialDefense", "Speed", "Accuracy", "Evasion"}
MOVE_CATEGORIES = {"Physical", "Special", "Status"}
TARGET_CLASSES = {
    "Self", "SelectedAlly", "SelectedOpponent", "AnySelectedBattler",
    "RandomLegalOpponent", "UserSide", "OpponentSide", "BothSides", "Field",
    "FixedSpreadSet", "SelectedOtherBattler", "FixedOpponentSpreadSet",
}
BATTLER_TARGET_CLASSES = {
    "Self", "SelectedAlly", "SelectedOpponent", "AnySelectedBattler",
    "RandomLegalOpponent", "FixedSpreadSet", "SelectedOtherBattler",
    "FixedOpponentSpreadSet",
}
RESOLVED_OTHER_TARGET_CLASSES = {
    "SelectedOpponent", "RandomLegalOpponent", "SelectedOtherBattler",
}
EFFECT_KINDS = {
    "Damage", "ApplyCondition", "ModifyStatStage", "Heal", "Drain", "Recoil",
    "MultiHit", "SetFieldCondition", "SetSideCondition", "Switch", "ChangeItem",
    "Charge", "Recharge", "Protect", "SemiInvulnerability", "RemoveCondition",
    "RegisterTargetRedirection", "RegisterAllyActionPowerModifier",
}
EFFECT_TARGETS = {"User", "ResolvedTarget", "AllResolvedTargets", "UserSide", "TargetSide", "BothSides", "Field"}
MOVE_FLAGS = {
    "MakesContact", "BlockedByProtect", "BypassesProtect", "BypassesSubstitute",
    "ThawsUser", "ThawsTarget", "Unencoreable", "AlwaysCritical", "NeverCritical",
    "UsesPerHitAccuracy", "TypelessDamage", "ReachesAirborneSemiInvulnerableTarget",
    "DoublesPowerAgainstAirborneSemiInvulnerableTarget", "BreaksProtection",
    "BypassesSideProtection", "ReducedByGrassyTerrain", "RespectsTypeImmunity",
    "Powder", "PoisonTypeUserBypassesSemiInvulnerabilityAndAccuracy",
    "SkipsChargeInSun", "HalvesPowerInRainSandstormOrSnow",
    "RainAlwaysHitsSunAccuracyFifty",
}
EFFECT_FLAGS = {"BypassesSubstitute", "UsesActualDamage", "MinimumOne", "StopOnFaint", "PerHit", "OptionalIfAbsent"}
HELD_OPERATIONS = {"None", "RemoveCurrent", "ExchangeCurrent", "TransferCurrent", "RestoreLastConsumed"}
ITEM_KINDS = {"Held", "Battle", "Capture"}
CONDITION_KINDS = {"MajorStatus", "Volatile", "Weather", "Terrain", "Hazard", "Screen", "Room", "SideCondition"}
PREFIXES = {
    "species_forms": "Species.", "natures": "Nature.", "moves": "Move.",
    "abilities": "Ability.", "items": "Item.", "conditions": "Condition.",
    "display_names": "Species.", "runtime_scenario": "",
}
ROW_FIELDS = {
    "species_forms": {"Name", "PrimaryType", "SecondaryType", "BaseHP", "BaseAttack", "BaseDefense", "BaseSpecialAttack", "BaseSpecialDefense", "BaseSpeed", "CatchRate", "AbilityIds"},
    "natures": {"Name", "BoostedStat", "ReducedStat"},
    "moves": {"Name", "Type", "Category", "Power", "bAlwaysHits", "Accuracy", "bUsesPP", "BasePP", "bAllowsPPBoosts", "Priority", "TargetClass", "Flags", "Effects"},
    "abilities": {"Name"},
    "items": {"Name", "Kind", "bCanBeTakenByMove"},
    "conditions": {"Name", "Kind"},
    "type_chart": {"Name", "Entries"},
    "display_names": {"Name", "DisplayName"},
}
EFFECT_FIELDS = {"Order", "Kind", "Target", "ConditionId", "ItemId", "HeldItemOperation", "Stat", "ChanceNumerator", "ChanceDenominator", "MagnitudeNumerator", "MagnitudeDenominator", "MinimumCount", "MaximumCount", "DurationTurns", "LayerCount", "Flags"}
ID_PATTERN = re.compile(r"^[A-Za-z][A-Za-z0-9]*\.[A-Za-z][A-Za-z0-9]*$")


@dataclass(frozen=True, order=True)
class Diagnostic:
    family: str
    row: str
    field: str
    code: str
    message: str


@dataclass(frozen=True)
class ValidationResult:
    diagnostics: tuple[Diagnostic, ...]
    counts: Mapping[str, int]
    source_sha256: Mapping[str, str]
    pinned_manifest_sha256: str
    import_families: tuple[str, ...]

    @property
    def is_valid(self) -> bool:
        return not self.diagnostics

    def to_dict(self) -> dict[str, Any]:
        return {
            "valid": self.is_valid,
            "counts": dict(sorted(self.counts.items())),
            "sourceSha256": dict(sorted(self.source_sha256.items())),
            "pinnedManifestSha256": self.pinned_manifest_sha256,
            "importFamilies": list(self.import_families),
            "diagnostics": [asdict(item) for item in self.diagnostics],
        }


def _diag(items: list[Diagnostic], family: str, row: object, field: str, code: str, message: str) -> None:
    items.append(Diagnostic(family, str(row), field, code, message))


def _is_int(value: object) -> bool:
    return type(value) is int


def _check_fields(items: list[Diagnostic], family: str, row_name: object, value: object, expected: set[str], field: str = "row") -> bool:
    if not isinstance(value, dict):
        _diag(items, family, row_name, field, "field.type", "Expected an object.")
        return False
    prefix = "" if field == "row" else f"{field}."
    for missing in sorted(expected - set(value)):
        _diag(items, family, row_name, f"{prefix}{missing}", "field.missing", "Required field is missing.")
    for extra in sorted(set(value) - expected):
        _diag(items, family, row_name, f"{prefix}{extra}", "field.extra", "Unknown field is not allowed.")
    return set(value) == expected


def _check_enum(items: list[Diagnostic], family: str, row: object, field: str, value: object, allowed: set[str] | Sequence[str]) -> bool:
    if not isinstance(value, str):
        _diag(items, family, row, field, "field.type", "Expected a string enum name.")
        return False
    if value not in allowed:
        _diag(items, family, row, field, "enum.unknown", f"Unknown enum name '{value}'.")
        return False
    return True


def _check_int(items: list[Diagnostic], family: str, row: object, field: str, value: object, minimum: int, maximum: int) -> bool:
    if not _is_int(value):
        _diag(items, family, row, field, "field.type", "Expected an integer.")
        return False
    if value < minimum or value > maximum:
        _diag(items, family, row, field, "range.invalid", f"Expected {minimum} through {maximum}.")
        return False
    return True


def _check_bool(items: list[Diagnostic], family: str, row: object, field: str, value: object) -> bool:
    if type(value) is not bool:
        _diag(items, family, row, field, "field.type", "Expected a boolean.")
        return False
    return True


def _check_id(items: list[Diagnostic], family: str, row: object, field: str, value: object, prefix: str, allow_none: bool = False) -> bool:
    if allow_none and value == "None":
        return True
    if not isinstance(value, str) or not ID_PATTERN.fullmatch(value):
        _diag(items, family, row, field, "identity.invalid", "Expected a stable dotted identifier.")
        return False
    if not value.startswith(prefix):
        _diag(items, family, row, field, "family.mismatch", f"Expected the '{prefix}' family.")
        return False
    return True


def validate_import_allowlist(families: Sequence[str]) -> tuple[Diagnostic, ...]:
    items: list[Diagnostic] = []
    seen: set[str] = set()
    for index, family in enumerate(families):
        if family in seen:
            _diag(items, "import", index, "family", "allowlist.duplicate", f"Duplicate family '{family}'.")
        seen.add(family)
        if family in VALIDATE_ONLY_FAMILIES:
            _diag(items, "import", index, "family", "allowlist.validate_only", f"'{family}' is validate-only.")
        elif family not in MUTABLE_FAMILIES:
            _diag(items, "import", index, "family", "allowlist.unknown", f"Unknown mutable family '{family}'.")
    if tuple(families) != MUTABLE_FAMILIES:
        _diag(items, "import", "allowlist", "families", "allowlist.incomplete", "The allowlist must contain the exact seven approved families in canonical order.")
    return tuple(sorted(items))


def _rows(items: list[Diagnostic], family: str, document: object) -> list[dict[str, Any]]:
    if not isinstance(document, list):
        _diag(items, family, "document", "document", "document.type", "Expected a JSON array.")
        return []
    if len(document) != EXPECTED_COUNTS[family]:
        _diag(items, family, "document", "count", "count.mismatch", f"Expected exactly {EXPECTED_COUNTS[family]} rows, got {len(document)}.")
    result: list[dict[str, Any]] = []
    for index, row in enumerate(document):
        if isinstance(row, dict):
            result.append(row)
        else:
            _diag(items, family, index, "row", "field.type", "Expected an object row.")
    return result


def _index_names(items: list[Diagnostic], family: str, rows: list[dict[str, Any]]) -> dict[str, dict[str, Any]]:
    result: dict[str, dict[str, Any]] = {}
    for index, row in enumerate(rows):
        name = row.get("Name")
        if not isinstance(name, str) or not name:
            _diag(items, family, index, "Name", "identity.invalid", "Name must be a non-empty string.")
            continue
        if family not in {"type_chart", "runtime_scenario"}:
            _check_id(items, family, name, "Name", name, PREFIXES[family])
        key = name.casefold()
        if key in result:
            _diag(items, family, name, "Name", "identity.duplicate", "Name is not unique under case folding.")
        else:
            result[key] = row
    return result


def _validate_simple_families(items: list[Diagnostic], docs: Mapping[str, object]) -> dict[str, dict[str, dict[str, Any]]]:
    indexes: dict[str, dict[str, dict[str, Any]]] = {}
    for family in SOURCE_FILES:
        rows = _rows(items, family, docs.get(family))
        indexes[family] = _index_names(items, family, rows)
        if family != "runtime_scenario":
            for index, row in enumerate(rows):
                _check_fields(items, family, row.get("Name", index), row, ROW_FIELDS[family])
    return indexes


def _validate_species(items: list[Diagnostic], rows: list[dict[str, Any]], abilities: set[str]) -> None:
    for index, row in enumerate(rows):
        name = row.get("Name", index)
        _check_enum(items, "species_forms", name, "PrimaryType", row.get("PrimaryType"), set(TYPES))
        secondary = row.get("SecondaryType")
        if secondary != "None":
            _check_enum(items, "species_forms", name, "SecondaryType", secondary, set(TYPES))
            if secondary == row.get("PrimaryType"):
                _diag(items, "species_forms", name, "SecondaryType", "range.invalid", "Secondary type must differ from primary type.")
        for field in ("BaseHP", "BaseAttack", "BaseDefense", "BaseSpecialAttack", "BaseSpecialDefense", "BaseSpeed"):
            _check_int(items, "species_forms", name, field, row.get(field), 1, 255)
        _check_int(items, "species_forms", name, "CatchRate", row.get("CatchRate"), 1, 255)
        choices = row.get("AbilityIds")
        if not isinstance(choices, list) or not 1 <= len(choices) <= 3:
            _diag(items, "species_forms", name, "AbilityIds", "range.invalid", "Expected one through three Ability IDs.")
            continue
        seen: set[str] = set()
        for choice_index, choice in enumerate(choices):
            if _check_id(items, "species_forms", name, f"AbilityIds[{choice_index}]", choice, "Ability."):
                folded = choice.casefold()
                if folded in seen:
                    _diag(items, "species_forms", name, f"AbilityIds[{choice_index}]", "identity.duplicate", "Ability choice is duplicated.")
                elif folded not in abilities:
                    _diag(items, "species_forms", name, f"AbilityIds[{choice_index}]", "reference.missing", f"Unknown Ability '{choice}'.")
                seen.add(folded)


def _validate_natures(items: list[Diagnostic], rows: list[dict[str, Any]]) -> None:
    for index, row in enumerate(rows):
        name = row.get("Name", index)
        boosted = row.get("BoostedStat")
        reduced = row.get("ReducedStat")
        valid_boost = _check_enum(items, "natures", name, "BoostedStat", boosted, NATURE_STATS)
        valid_reduce = _check_enum(items, "natures", name, "ReducedStat", reduced, NATURE_STATS)
        if valid_boost and valid_reduce and not ((boosted == reduced == "None") or (boosted != "None" and reduced != "None" and boosted != reduced)):
            _diag(items, "natures", name, "BoostedStat/ReducedStat", "range.invalid", "Nature must be neutral or use two distinct non-None stats.")


def _validate_flags(items: list[Diagnostic], family: str, row: object, field: str, value: object, allowed: set[str]) -> set[str]:
    if not isinstance(value, list):
        _diag(items, family, row, field, "field.type", "Expected an array of flag names.")
        return set()
    seen: set[str] = set()
    for index, flag in enumerate(value):
        if not isinstance(flag, str) or flag not in allowed:
            _diag(items, family, row, f"{field}[{index}]", "flag.unknown", f"Unknown flag '{flag}'.")
        elif flag in seen:
            _diag(items, family, row, f"{field}[{index}]", "flag.duplicate", f"Duplicate flag '{flag}'.")
        seen.add(flag) if isinstance(flag, str) else None
    return seen


def _condition_compatible(kind: str, condition_kind: str) -> bool:
    allowed = {
        "ApplyCondition": {"MajorStatus", "Volatile"},
        "SetFieldCondition": {"Weather", "Terrain", "Room"},
        "SetSideCondition": {"Hazard", "Screen", "SideCondition"},
        "Charge": {"Volatile"}, "Recharge": {"Volatile"},
        "Protect": {"Volatile"}, "SemiInvulnerability": {"Volatile"},
        "RemoveCondition": CONDITION_KINDS,
    }
    return condition_kind in allowed.get(kind, set())


def _primary_chance(effect: Mapping[str, Any]) -> bool:
    return (
        effect.get("ChanceNumerator") == 1
        and effect.get("ChanceDenominator") == 1
    )


def _removal_target_compatible(target: object, condition_kind: object) -> bool:
    if condition_kind in {"MajorStatus", "Volatile"}:
        return target in {"User", "ResolvedTarget", "AllResolvedTargets"}
    if condition_kind in {"Weather", "Terrain", "Room"}:
        return target == "Field"
    if condition_kind in {"Hazard", "Screen", "SideCondition"}:
        return target in {"UserSide", "TargetSide", "BothSides"}
    return False


def _registration_effect_is_canonical(
    effect: Mapping[str, Any], kind: str
) -> bool:
    target = (
        "User"
        if kind == "RegisterTargetRedirection"
        else "ResolvedTarget"
    )
    common = (
        effect.get("Order") == 0
        and effect.get("Kind") == kind
        and effect.get("Target") == target
        and _primary_chance(effect)
        and effect.get("ConditionId") == "None"
        and effect.get("ItemId") == "None"
        and effect.get("HeldItemOperation") == "None"
        and effect.get("Stat") == "None"
        and effect.get("MinimumCount") == 0
        and effect.get("MaximumCount") == 0
        and effect.get("DurationTurns") == 0
        and effect.get("LayerCount") == 0
        and effect.get("Flags") == []
    )
    if not common:
        return False
    numerator = effect.get("MagnitudeNumerator")
    denominator = effect.get("MagnitudeDenominator")
    if kind == "RegisterTargetRedirection":
        return numerator == 0 and denominator == 1
    if not _is_int(numerator) or not _is_int(denominator):
        return False
    product = numerator * 4096
    return (
        numerator > 0
        and denominator > 0
        and product % denominator == 0
        and 0 < product // denominator <= 131072
    )


def _validate_move_rule_flags(
    items: list[Diagnostic], move: Mapping[str, Any], flags: set[str]
) -> None:
    name = move.get("Name", "unknown")
    target_class = move.get("TargetClass")
    category = move.get("Category")
    damaging = category in {"Physical", "Special"}
    hit_flags = {
        "RespectsTypeImmunity",
        "Powder",
        "PoisonTypeUserBypassesSemiInvulnerabilityAndAccuracy",
    }
    if flags & hit_flags and target_class not in BATTLER_TARGET_CLASSES:
        _diag(items, "moves", name, "Flags", "effect.incompatible", "Hit-rule flags require a battler target class.")
    if "RespectsTypeImmunity" in flags and category != "Status":
        _diag(items, "moves", name, "Flags", "effect.incompatible", "RespectsTypeImmunity is valid only on status moves.")
    if "PoisonTypeUserBypassesSemiInvulnerabilityAndAccuracy" in flags:
        if category != "Status" or move.get("Type") != "Poison":
            _diag(items, "moves", name, "Flags", "effect.incompatible", "The Poison-user bypass requires a Poison status move.")
        if move.get("bAlwaysHits") is not False or not (
            _is_int(move.get("Accuracy"))
            and 1 <= move["Accuracy"] <= 100
        ):
            _diag(items, "moves", name, "Flags", "effect.incompatible", "The Poison-user bypass requires ordinary numeric accuracy.")

    effects = [
        effect for effect in move.get("Effects", [])
        if isinstance(effect, dict)
    ]
    charge = [effect for effect in effects if effect.get("Kind") == "Charge"]
    damage = [effect for effect in effects if effect.get("Kind") == "Damage"]
    canonical_charge = (
        len(charge) == 1
        and len(damage) == 1
        and _is_int(charge[0].get("Order"))
        and _is_int(damage[0].get("Order"))
        and 0 <= charge[0]["Order"] < damage[0]["Order"]
        and charge[0].get("Target") == "User"
        and _primary_chance(charge[0])
        and charge[0].get("ConditionId") == "Condition.Charging"
    )
    if "SkipsChargeInSun" in flags and (
        not damaging or not canonical_charge
    ):
        _diag(items, "moves", name, "Flags", "effect.incompatible", "SkipsChargeInSun requires one canonical primary charge before damage.")
    if "HalvesPowerInRainSandstormOrSnow" in flags and not damaging:
        _diag(items, "moves", name, "Flags", "effect.incompatible", "The weather power modifier requires a damaging move.")
    if "RainAlwaysHitsSunAccuracyFifty" in flags and (
        not damaging
        or target_class not in BATTLER_TARGET_CLASSES
        or move.get("Accuracy") != 70
        or move.get("bAlwaysHits") is not False
        or "UsesPerHitAccuracy" in flags
    ):
        _diag(items, "moves", name, "Flags", "effect.incompatible", "The weather accuracy rule requires an ordinary 70-accuracy battler-targeting damaging move.")


def _validate_held_item_operations(
    items: list[Diagnostic], move: Mapping[str, Any], effects: Sequence[object]
) -> None:
    name = move.get("Name", "unknown")
    operation_count = 0
    has_earlier_damage = False
    valid = True
    for index, effect in enumerate(effects):
        if not isinstance(effect, dict):
            continue
        kind = effect.get("Kind")
        operation = effect.get("HeldItemOperation")
        item_id = effect.get("ItemId")
        if kind == "ChangeItem":
            legacy = operation == "None" and item_id != "None"
            if operation != "None":
                operation_count += 1
                base = (
                    operation in HELD_OPERATIONS
                    and item_id == "None"
                    and "PerHit" not in effect.get("Flags", [])
                    and index == len(effects) - 1
                )
                if operation in {"RemoveCurrent", "TransferCurrent"}:
                    shape = (
                        move.get("Category") in {"Physical", "Special"}
                        and has_earlier_damage
                        and move.get("TargetClass")
                        in RESOLVED_OTHER_TARGET_CLASSES
                        and effect.get("Target") == "ResolvedTarget"
                    )
                elif operation == "ExchangeCurrent":
                    shape = (
                        move.get("Category") == "Status"
                        and not has_earlier_damage
                        and move.get("TargetClass")
                        in RESOLVED_OTHER_TARGET_CLASSES
                        and effect.get("Target") == "ResolvedTarget"
                    )
                elif operation == "RestoreLastConsumed":
                    shape = (
                        move.get("Category") == "Status"
                        and not has_earlier_damage
                        and move.get("TargetClass") == "Self"
                        and effect.get("Target") == "User"
                    )
                else:
                    shape = False
                valid = valid and base and shape
            elif not legacy:
                valid = False
        elif item_id != "None" or operation != "None":
            valid = False
        if kind == "Damage":
            has_earlier_damage = True
    if not valid or operation_count > 1:
        _diag(items, "moves", name, "Effects.HeldItemOperation", "effect.incompatible", "Held-item descriptors do not match an approved legacy or operation-specific shape.")


def _validate_effect(items: list[Diagnostic], move: dict[str, Any], effect: object, effect_index: int, conditions: Mapping[str, dict[str, Any]], item_ids: set[str]) -> None:
    name = move.get("Name", "unknown")
    field = f"Effects[{effect_index}]"
    if not _check_fields(items, "moves", name, effect, EFFECT_FIELDS, field):
        if not isinstance(effect, dict):
            return
    assert isinstance(effect, dict)
    kind = effect.get("Kind")
    target = effect.get("Target")
    _check_int(items, "moves", name, f"{field}.Order", effect.get("Order"), 0, 255)
    kind_valid = _check_enum(items, "moves", name, f"{field}.Kind", kind, EFFECT_KINDS)
    target_valid = _check_enum(items, "moves", name, f"{field}.Target", target, EFFECT_TARGETS)
    _check_enum(items, "moves", name, f"{field}.HeldItemOperation", effect.get("HeldItemOperation"), HELD_OPERATIONS)
    _check_enum(items, "moves", name, f"{field}.Stat", effect.get("Stat"), BATTLE_STATS)
    effect_flags = _validate_flags(items, "moves", name, f"{field}.Flags", effect.get("Flags"), EFFECT_FLAGS)
    for key, low, high in (("ChanceNumerator", 1, 100), ("ChanceDenominator", 1, 100), ("MagnitudeNumerator", -1000, 1000), ("MagnitudeDenominator", 1, 1000), ("MinimumCount", 0, 255), ("MaximumCount", 0, 255), ("DurationTurns", 0, 255), ("LayerCount", 0, 3)):
        _check_int(items, "moves", name, f"{field}.{key}", effect.get(key), low, high)
    chance = (effect.get("ChanceNumerator"), effect.get("ChanceDenominator"))
    if all(_is_int(v) for v in chance) and chance != (1, 1) and not (chance[1] == 100 and 1 <= chance[0] <= 100):
        _diag(items, "moves", name, f"{field}.Chance", "range.invalid", "Chance must be 1/1 or an independent percentage over 100.")
    condition_id = effect.get("ConditionId")
    item_id = effect.get("ItemId")
    requires_condition = kind in {"ApplyCondition", "SetFieldCondition", "SetSideCondition", "Charge", "Recharge", "Protect", "SemiInvulnerability", "RemoveCondition"}
    condition = None
    if requires_condition:
        if _check_id(items, "moves", name, f"{field}.ConditionId", condition_id, "Condition."):
            condition = conditions.get(str(condition_id).casefold())
            if condition is None:
                _diag(items, "moves", name, f"{field}.ConditionId", "reference.missing", f"Unknown Condition '{condition_id}'.")
            elif kind_valid and not _condition_compatible(str(kind), str(condition.get("Kind"))):
                _diag(items, "moves", name, f"{field}.ConditionId", "effect.incompatible", "Condition family is incompatible with the effect kind.")
            elif kind == "RemoveCondition" and not _removal_target_compatible(
                target, condition.get("Kind")
            ):
                _diag(items, "moves", name, f"{field}.Target", "target.incompatible", "The removal target is incompatible with the condition family.")
    elif condition_id != "None":
        _diag(items, "moves", name, f"{field}.ConditionId", "effect.incompatible", "This effect kind cannot carry a Condition ID.")
    if kind == "ChangeItem":
        if _check_id(items, "moves", name, f"{field}.ItemId", item_id, "Item.", allow_none=True) and item_id != "None" and str(item_id).casefold() not in item_ids:
            _diag(items, "moves", name, f"{field}.ItemId", "reference.missing", f"Unknown Item '{item_id}'.")
    elif item_id != "None":
        _diag(items, "moves", name, f"{field}.ItemId", "effect.incompatible", "This effect kind cannot carry an Item ID.")
    battler_target = move.get("TargetClass") in BATTLER_TARGET_CLASSES
    if kind == "Damage" and (target not in {"ResolvedTarget", "AllResolvedTargets"} or not battler_target or chance != (1, 1)):
        _diag(items, "moves", name, f"{field}.Target", "target.incompatible", "Damage requires a resolved battler target and primary chance.")
    if kind in {"ApplyCondition", "ModifyStatStage", "Heal", "Switch", "ChangeItem"} and not (target == "User" or (target in {"ResolvedTarget", "AllResolvedTargets"} and battler_target)):
        _diag(items, "moves", name, f"{field}.Target", "target.incompatible", "Effect target is incompatible with the move target class.")
    required_targets = {
        "Drain": "User", "Recoil": "User", "Charge": "User", "Recharge": "User",
        "Protect": "User", "SemiInvulnerability": "User",
        "RegisterTargetRedirection": "User", "RegisterAllyActionPowerModifier": "ResolvedTarget",
        "SetFieldCondition": "Field",
    }
    if kind in required_targets and target_valid and target != required_targets[kind]:
        _diag(items, "moves", name, f"{field}.Target", "target.incompatible", f"{kind} requires target {required_targets[kind]}.")
    if kind == "SetFieldCondition" and move.get("TargetClass") != "Field":
        _diag(items, "moves", name, f"{field}.Target", "target.incompatible", "SetFieldCondition requires the Field move target class.")
    if kind == "SetSideCondition" and target not in {"UserSide", "TargetSide", "BothSides"}:
        _diag(items, "moves", name, f"{field}.Target", "target.incompatible", "Side conditions require a side target.")
    if (
        kind in {"SetSideCondition", "RemoveCondition"}
        and target == "TargetSide"
        and move.get("TargetClass") == "Field"
    ):
        _diag(items, "moves", name, f"{field}.Target", "target.incompatible", "A Field-targeting move cannot resolve TargetSide.")
    if (
        kind == "RemoveCondition"
        and condition is not None
        and condition.get("Kind") in {"MajorStatus", "Volatile"}
        and target in {"ResolvedTarget", "AllResolvedTargets"}
        and not battler_target
    ):
        _diag(items, "moves", name, f"{field}.Target", "target.incompatible", "Battler condition removal requires a battler target class.")
    if kind == "ModifyStatStage":
        if effect.get("Stat") == "None" or effect.get("MagnitudeNumerator") == 0 or effect.get("MagnitudeDenominator") != 1:
            _diag(items, "moves", name, f"{field}.Stat", "range.invalid", "Stat changes require a known stat and non-zero integer magnitude.")
    elif effect.get("Stat") != "None":
        _diag(items, "moves", name, f"{field}.Stat", "effect.incompatible", "Only stat-stage effects may specify Stat.")
    if kind in {"Heal", "Drain", "Recoil"} and (_is_int(effect.get("MagnitudeNumerator")) and _is_int(effect.get("MagnitudeDenominator"))) and (effect["MagnitudeNumerator"] <= 0 or (effect["MagnitudeDenominator"] > 1 and effect["MagnitudeNumerator"] > effect["MagnitudeDenominator"])):
        _diag(items, "moves", name, f"{field}.Magnitude", "range.invalid", "Fractional magnitude must be positive and at most one.")
    if kind == "MultiHit" and (effect.get("MinimumCount"), effect.get("MaximumCount")) not in {(2, 2), (3, 3), (4, 4), (5, 5), (2, 5)}:
        _diag(items, "moves", name, f"{field}.Count", "range.invalid", "Multi-hit count must be fixed 2-5 or range 2-5.")
    if "OptionalIfAbsent" in effect_flags and kind != "RemoveCondition":
        _diag(items, "moves", name, f"{field}.Flags", "effect.incompatible", "OptionalIfAbsent is valid only for RemoveCondition.")
    if "PerHit" in effect_flags and (
        move.get("Category") == "Status"
        or chance == (1, 1)
        or kind in {"Damage", "MultiHit", "Drain", "Recoil"}
    ):
        _diag(items, "moves", name, f"{field}.Flags", "effect.incompatible", "PerHit requires a non-primary secondary effect on a damaging move.")


def _validate_moves(items: list[Diagnostic], rows: list[dict[str, Any]], conditions: Mapping[str, dict[str, Any]], item_ids: set[str]) -> None:
    for index, move in enumerate(rows):
        name = move.get("Name", index)
        _check_enum(items, "moves", name, "Type", move.get("Type"), set(TYPES))
        category_valid = _check_enum(items, "moves", name, "Category", move.get("Category"), MOVE_CATEGORIES)
        _check_enum(items, "moves", name, "TargetClass", move.get("TargetClass"), TARGET_CLASSES)
        flags = _validate_flags(items, "moves", name, "Flags", move.get("Flags"), MOVE_FLAGS)
        for field in ("bAlwaysHits", "bUsesPP", "bAllowsPPBoosts"):
            _check_bool(items, "moves", name, field, move.get(field))
        power_valid = _check_int(items, "moves", name, "Power", move.get("Power"), 0, 1000)
        accuracy_valid = _check_int(items, "moves", name, "Accuracy", move.get("Accuracy"), 0, 100)
        base_pp_valid = _check_int(items, "moves", name, "BasePP", move.get("BasePP"), 0, 64)
        _check_int(items, "moves", name, "Priority", move.get("Priority"), -7, 5)
        if category_valid and power_valid and ((move.get("Category") == "Status" and move.get("Power") != 0) or (move.get("Category") in {"Physical", "Special"} and move["Power"] < 1)):
            _diag(items, "moves", name, "Power", "range.invalid", "Status power must be zero; damaging power must be positive.")
        if type(move.get("bAlwaysHits")) is bool and accuracy_valid and ((move["bAlwaysHits"] and move["Accuracy"] != 0) or (not move["bAlwaysHits"] and not 1 <= move["Accuracy"] <= 100)):
            _diag(items, "moves", name, "Accuracy", "range.invalid", "Always-hit moves use 0; other moves use 1 through 100.")
        if type(move.get("bUsesPP")) is bool and base_pp_valid and ((move["bUsesPP"] and not 1 <= move["BasePP"] <= 64) or (not move["bUsesPP"] and (move["BasePP"] != 0 or move.get("bAllowsPPBoosts") is not False))):
            _diag(items, "moves", name, "BasePP", "range.invalid", "PP fields are inconsistent.")
        if "AlwaysCritical" in flags and "NeverCritical" in flags:
            _diag(items, "moves", name, "Flags", "effect.incompatible", "AlwaysCritical and NeverCritical conflict.")
        if "TypelessDamage" in flags:
            _diag(items, "moves", name, "Flags", "effect.incompatible", "TypelessDamage is not approved for canonical content.")
        effects = move.get("Effects")
        if not isinstance(effects, list) or not effects:
            _diag(items, "moves", name, "Effects", "range.invalid", "At least one effect is required.")
            continue
        for effect_index, effect in enumerate(effects):
            _validate_effect(items, move, effect, effect_index, conditions, item_ids)
        _validate_move_rule_flags(items, move, flags)
        _validate_held_item_operations(items, move, effects)
        orders = [effect.get("Order") for effect in effects if isinstance(effect, dict)]
        if any(not _is_int(order) for order in orders) or orders != sorted(orders) or len(set(orders)) != len(orders):
            _diag(items, "moves", name, "Effects.Order", "effect.order", "Effect orders must be unique and strictly increasing.")
        damage = [effect for effect in effects if isinstance(effect, dict) and effect.get("Kind") == "Damage"]
        if (move.get("Category") == "Status" and damage) or (move.get("Category") in {"Physical", "Special"} and len(damage) != 1):
            _diag(items, "moves", name, "Category", "effect.incompatible", "Damage descriptor count does not match move category.")
        for special, expected_class in (("RegisterTargetRedirection", "Self"), ("RegisterAllyActionPowerModifier", "SelectedAlly")):
            special_effects = [effect for effect in effects if isinstance(effect, dict) and effect.get("Kind") == special]
            if special_effects and (
                len(effects) != 1
                or move.get("Category") != "Status"
                or move.get("Power") != 0
                or move.get("bAlwaysHits") is not True
                or move.get("Accuracy") != 0
                or move.get("TargetClass") != expected_class
                or not _registration_effect_is_canonical(
                    special_effects[0], special
                )
            ):
                _diag(items, "moves", name, f"Effects.{special}", "effect.incompatible", f"{special} move shape is not canonical.")
        multi = [effect for effect in effects if isinstance(effect, dict) and effect.get("Kind") == "MultiHit"]
        charge = [effect for effect in effects if isinstance(effect, dict) and effect.get("Kind") == "Charge"]
        semi = [effect for effect in effects if isinstance(effect, dict) and effect.get("Kind") == "SemiInvulnerability"]
        if len(multi) > 1 or len(charge) > 1 or len(semi) > 1:
            _diag(items, "moves", name, "Effects", "effect.incompatible", "At most one MultiHit, Charge, and SemiInvulnerability descriptor is allowed.")
        if multi and (not damage or not _primary_chance(multi[0]) or multi[0].get("Order", 999) >= damage[0].get("Order", -1) or multi[0].get("Target") != damage[0].get("Target") or move.get("TargetClass") in {"FixedSpreadSet", "FixedOpponentSpreadSet"}):
            _diag(items, "moves", name, "Effects.MultiHit", "effect.incompatible", "MultiHit must precede matching non-spread damage.")
        if charge and (not damage or not _primary_chance(charge[0]) or charge[0].get("Order", 999) >= damage[0].get("Order", -1)):
            _diag(items, "moves", name, "Effects.Charge", "effect.incompatible", "Charge must precede damage.")
        if semi and (not charge or not damage or not _primary_chance(semi[0]) or not charge[0].get("Order", 999) < semi[0].get("Order", -1) < damage[0].get("Order", -1)):
            _diag(items, "moves", name, "Effects.SemiInvulnerability", "effect.incompatible", "SemiInvulnerability must be between charge and damage.")


def _validate_catalog_rows(items: list[Diagnostic], docs: Mapping[str, object], indexes: Mapping[str, Mapping[str, dict[str, Any]]]) -> None:
    ability_ids = set(indexes["abilities"])
    item_ids = set(indexes["items"])
    _validate_species(items, _rows_no_diag(docs.get("species_forms")), ability_ids)
    _validate_natures(items, _rows_no_diag(docs.get("natures")))
    for row in _rows_no_diag(docs.get("items")):
        name = row.get("Name", "unknown")
        _check_enum(items, "items", name, "Kind", row.get("Kind"), ITEM_KINDS)
        _check_bool(items, "items", name, "bCanBeTakenByMove", row.get("bCanBeTakenByMove"))
    for row in _rows_no_diag(docs.get("conditions")):
        _check_enum(items, "conditions", row.get("Name", "unknown"), "Kind", row.get("Kind"), CONDITION_KINDS)
    for row in _rows_no_diag(docs.get("display_names")):
        name = row.get("Name", "unknown")
        if not isinstance(row.get("DisplayName"), str) or not row.get("DisplayName"):
            _diag(items, "display_names", name, "DisplayName", "field.type", "DisplayName must be non-empty text.")
    if set(indexes["display_names"]) != set(indexes["species_forms"]):
        _diag(items, "display_names", "document", "Name", "reference.missing", "Display-name IDs must exactly equal species IDs.")
    _validate_moves(items, _rows_no_diag(docs.get("moves")), indexes["conditions"], item_ids)


def _rows_no_diag(value: object) -> list[dict[str, Any]]:
    return [row for row in value if isinstance(row, dict)] if isinstance(value, list) else []


def _validate_type_chart(items: list[Diagnostic], rows: list[dict[str, Any]]) -> None:
    attacking = [row.get("Name") for row in rows]
    if set(attacking) != set(TYPES) or len(attacking) != len(set(attacking)):
        _diag(items, "type_chart", "document", "Name", "count.mismatch", "Attacking types must be the exact modern 18-type set.")
    pair_count = 0
    for index, row in enumerate(rows):
        name = row.get("Name", index)
        _check_enum(items, "type_chart", name, "Name", name, set(TYPES))
        entries = row.get("Entries")
        if not isinstance(entries, list):
            _diag(items, "type_chart", name, "Entries", "field.type", "Expected an array.")
            continue
        pair_count += len(entries)
        defending: list[object] = []
        for entry_index, entry in enumerate(entries):
            field = f"Entries[{entry_index}]"
            if not _check_fields(items, "type_chart", name, entry, {"DefendingType", "Numerator", "Denominator"}, field):
                if not isinstance(entry, dict):
                    continue
            assert isinstance(entry, dict)
            defending.append(entry.get("DefendingType"))
            _check_enum(items, "type_chart", name, f"{field}.DefendingType", entry.get("DefendingType"), set(TYPES))
            numerator = entry.get("Numerator")
            denominator = entry.get("Denominator")
            if not _is_int(numerator) or not _is_int(denominator):
                _diag(items, "type_chart", name, field, "field.type", "Effectiveness ratio must use integers.")
            elif (numerator, denominator) not in {(0, 1), (1, 2), (1, 1), (2, 1)}:
                _diag(items, "type_chart", name, field, "range.invalid", "Effectiveness must be 0, 1/2, 1, or 2.")
        if set(defending) != set(TYPES) or len(defending) != len(set(defending)):
            _diag(items, "type_chart", name, "Entries", "count.mismatch", "Defending entries must be the exact 18-type set.")
    if pair_count != 324:
        _diag(items, "type_chart", "document", "Entries", "count.mismatch", f"Expected exactly 324 type pairs, got {pair_count}.")


def _scenario_fields() -> set[str]:
    return {"Name", "SpeciesForms", "Natures", "Moves", "Abilities", "Items", "Conditions", "TypeChart", "DisplayNames", "Seed", "BattleId", "LocalTrainerId", "SettingsSnapshotId", "SettingsSchemaVersion", "CatalogSnapshotId", "CatalogSchemaVersion", "EncounterKind", "Format", "PartySlotsRemaining", "StorageSlotsRemaining", "Policies", "Trainers", "PartyEntries", "StartingActive", "ObedienceInputs"}


def _validate_scenario(items: list[Diagnostic], rows: list[dict[str, Any]], indexes: Mapping[str, Mapping[str, dict[str, Any]]]) -> None:
    if not rows:
        return
    row = rows[0]
    name = row.get("Name", 0)
    _check_fields(items, "runtime_scenario", name, row, _scenario_fields())
    expected_paths = {
        "SpeciesForms": "/Game/Data/Battle/Initial/DT_InitialBattleSpeciesForms.DT_InitialBattleSpeciesForms",
        "Natures": "/Game/Data/Battle/Initial/DT_InitialBattleNatures.DT_InitialBattleNatures",
        "Moves": "/Game/Data/Battle/Initial/DT_InitialBattleMoves.DT_InitialBattleMoves",
        "Abilities": "/Game/Data/Battle/Initial/DT_InitialBattleAbilities.DT_InitialBattleAbilities",
        "Items": "/Game/Data/Battle/Initial/DT_InitialBattleItems.DT_InitialBattleItems",
        "Conditions": "/Game/Data/Battle/Initial/DT_InitialBattleConditions.DT_InitialBattleConditions",
        "TypeChart": "/Game/Data/Battle/Initial/DT_InitialBattleTypeChart.DT_InitialBattleTypeChart",
        "DisplayNames": "/Game/Data/Battle/Initial/DT_InitialBattleDisplayNames.DT_InitialBattleDisplayNames",
    }
    for field, expected in expected_paths.items():
        if row.get(field) != expected:
            _diag(items, "runtime_scenario", name, field, "reference.invalid_asset", f"Expected '{expected}'.")
    for field in ("Seed", "BattleId", "LocalTrainerId"):
        value = row.get(field)
        if not isinstance(value, str) or not value.isdecimal() or int(value) <= 0:
            _diag(items, "runtime_scenario", name, field, "range.invalid", "Expected a positive decimal identity string.")
    for field, prefix in (("SettingsSnapshotId", "Settings."), ("CatalogSnapshotId", "Catalog.")):
        _check_id(items, "runtime_scenario", name, field, row.get(field), prefix)
    for field in ("SettingsSchemaVersion", "CatalogSchemaVersion"):
        _check_int(items, "runtime_scenario", name, field, row.get(field), 1, 2_147_483_647)
    _check_enum(items, "runtime_scenario", name, "EncounterKind", row.get("EncounterKind"), {"Wild", "Trainer", "Rival", "BossGym", "TutorialScripted"})
    _check_enum(items, "runtime_scenario", name, "Format", row.get("Format"), {"Single", "Double", "PartnerDouble"})
    _check_int(items, "runtime_scenario", name, "PartySlotsRemaining", row.get("PartySlotsRemaining"), 0, 6)
    _check_int(items, "runtime_scenario", name, "StorageSlotsRemaining", row.get("StorageSlotsRemaining"), 0, 2_147_483_647)
    policies = row.get("Policies")
    if _check_fields(items, "runtime_scenario", name, policies, {"bRunAllowed", "bCaptureAllowed", "bBagAllowed", "bShiftPromptEligible", "WildFleeMode", "WildFleeNumerator", "WildFleeDenominator"}, "Policies"):
        assert isinstance(policies, dict)
        for field in ("bRunAllowed", "bCaptureAllowed", "bBagAllowed", "bShiftPromptEligible"):
            _check_bool(items, "runtime_scenario", name, f"Policies.{field}", policies.get(field))
        _check_enum(items, "runtime_scenario", name, "Policies.WildFleeMode", policies.get("WildFleeMode"), {"Disabled", "Never", "Always", "Chance"})
        _check_int(items, "runtime_scenario", name, "Policies.WildFleeNumerator", policies.get("WildFleeNumerator"), 0, 2_147_483_647)
        _check_int(items, "runtime_scenario", name, "Policies.WildFleeDenominator", policies.get("WildFleeDenominator"), 0, 2_147_483_647)
        if policies.get("WildFleeMode") == "Chance" and (not _is_int(policies.get("WildFleeDenominator")) or policies["WildFleeDenominator"] <= 0 or policies.get("WildFleeNumerator", 0) > policies["WildFleeDenominator"]):
            _diag(items, "runtime_scenario", name, "Policies.WildFlee", "range.invalid", "Chance mode requires 0 <= numerator <= positive denominator.")
    trainer_fields = {"TrainerId", "Side", "Role", "Controller", "SelectorProfileId", "Bag"}
    bag_fields = {"ItemId", "Count"}
    party_fields = {"TrainerId", "BattlerId", "SourcePokemonId", "PartySlotIndex", "SpeciesFormId", "Level", "NatureId", "IndividualValues", "EffortValues", "bEgg", "AbilityId", "OriginalHeldItemId", "CurrentHeldItemId", "Moves"}
    stat_fields = {"HP", "Attack", "Defense", "SpecialAttack", "SpecialDefense", "Speed"}
    move_fields = {"SlotIndex", "MoveId", "PPUps"}
    trainers = row.get("Trainers")
    trainer_ids: set[str] = set()
    if not isinstance(trainers, list) or not trainers:
        _diag(items, "runtime_scenario", name, "Trainers", "range.invalid", "At least one Trainer is required.")
    else:
        for index, trainer in enumerate(trainers):
            key = f"Trainers[{index}]"
            if not _check_fields(items, "runtime_scenario", name, trainer, trainer_fields, key):
                if not isinstance(trainer, dict):
                    continue
            assert isinstance(trainer, dict)
            trainer_id = trainer.get("TrainerId")
            if not isinstance(trainer_id, str) or not trainer_id.isdecimal() or int(trainer_id) <= 0:
                _diag(items, "runtime_scenario", name, f"{key}.TrainerId", "identity.invalid", "Expected a positive decimal Trainer ID.")
            elif trainer_id in trainer_ids:
                _diag(items, "runtime_scenario", name, f"{key}.TrainerId", "identity.duplicate", "Trainer ID is duplicated.")
            trainer_ids.add(str(trainer_id))
            _check_enum(items, "runtime_scenario", name, f"{key}.Side", trainer.get("Side"), {"Player", "Opponent"})
            _check_enum(items, "runtime_scenario", name, f"{key}.Role", trainer.get("Role"), {"Player", "Partner", "Opponent"})
            _check_enum(items, "runtime_scenario", name, f"{key}.Controller", trainer.get("Controller"), {"Human", "PartnerAI", "EnemyAI", "Scripted"})
            _check_id(items, "runtime_scenario", name, f"{key}.SelectorProfileId", trainer.get("SelectorProfileId"), "Selector.")
            bag = trainer.get("Bag")
            if not isinstance(bag, list):
                _diag(items, "runtime_scenario", name, f"{key}.Bag", "field.type", "Expected an array.")
            else:
                for bag_index, entry in enumerate(bag):
                    entry_field = f"{key}.Bag[{bag_index}]"
                    if _check_fields(items, "runtime_scenario", name, entry, bag_fields, entry_field):
                        assert isinstance(entry, dict)
                        if _check_id(items, "runtime_scenario", name, f"{entry_field}.ItemId", entry.get("ItemId"), "Item.") and str(entry.get("ItemId")).casefold() not in indexes["items"]:
                            _diag(items, "runtime_scenario", name, f"{entry_field}.ItemId", "reference.missing", "Bag Item is not in the catalog.")
                        _check_int(items, "runtime_scenario", name, f"{entry_field}.Count", entry.get("Count"), 1, 2_147_483_647)
    party = row.get("PartyEntries")
    battler_ids: set[str] = set()
    if not isinstance(party, list) or not party:
        _diag(items, "runtime_scenario", name, "PartyEntries", "range.invalid", "At least one party entry is required.")
    else:
        for index, entry in enumerate(party):
            key = f"PartyEntries[{index}]"
            if not _check_fields(items, "runtime_scenario", name, entry, party_fields, key):
                if not isinstance(entry, dict):
                    continue
            assert isinstance(entry, dict)
            for field in ("TrainerId", "BattlerId", "SourcePokemonId"):
                value = entry.get(field)
                if not isinstance(value, str) or not value.isdecimal() or int(value) <= 0:
                    _diag(items, "runtime_scenario", name, f"{key}.{field}", "identity.invalid", "Expected a positive decimal identity string.")
            if str(entry.get("TrainerId")) not in trainer_ids:
                _diag(items, "runtime_scenario", name, f"{key}.TrainerId", "reference.missing", "Party Trainer is not declared.")
            battler_id = str(entry.get("BattlerId"))
            if battler_id in battler_ids:
                _diag(items, "runtime_scenario", name, f"{key}.BattlerId", "identity.duplicate", "Battler ID is duplicated.")
            battler_ids.add(battler_id)
            for field, family, prefix in (("SpeciesFormId", "species_forms", "Species."), ("NatureId", "natures", "Nature."), ("AbilityId", "abilities", "Ability.")):
                value = entry.get(field)
                if _check_id(items, "runtime_scenario", name, f"{key}.{field}", value, prefix) and str(value).casefold() not in indexes[family]:
                    _diag(items, "runtime_scenario", name, f"{key}.{field}", "reference.missing", f"Referenced {family} row is missing.")
            species = indexes["species_forms"].get(str(entry.get("SpeciesFormId")).casefold())
            if species and str(entry.get("AbilityId")).casefold() not in {str(value).casefold() for value in species.get("AbilityIds", [])}:
                _diag(items, "runtime_scenario", name, f"{key}.AbilityId", "reference.missing", "Ability is not an allowed species choice.")
            for field in ("OriginalHeldItemId", "CurrentHeldItemId"):
                value = entry.get(field)
                if _check_id(items, "runtime_scenario", name, f"{key}.{field}", value, "Item.", allow_none=True) and value != "None" and str(value).casefold() not in indexes["items"]:
                    _diag(items, "runtime_scenario", name, f"{key}.{field}", "reference.missing", "Held Item is not in the catalog.")
            _check_int(items, "runtime_scenario", name, f"{key}.PartySlotIndex", entry.get("PartySlotIndex"), 0, 5)
            _check_int(items, "runtime_scenario", name, f"{key}.Level", entry.get("Level"), 1, 100)
            _check_bool(items, "runtime_scenario", name, f"{key}.bEgg", entry.get("bEgg"))
            for stat_block, maximum in (("IndividualValues", 31), ("EffortValues", 252)):
                values = entry.get(stat_block)
                if _check_fields(items, "runtime_scenario", name, values, stat_fields, f"{key}.{stat_block}"):
                    assert isinstance(values, dict)
                    for stat in stat_fields:
                        _check_int(items, "runtime_scenario", name, f"{key}.{stat_block}.{stat}", values.get(stat), 0, maximum)
            moves = entry.get("Moves")
            if not isinstance(moves, list) or not 1 <= len(moves) <= 4:
                _diag(items, "runtime_scenario", name, f"{key}.Moves", "range.invalid", "Expected one through four moves.")
            else:
                slots: set[int] = set()
                for move_index, move in enumerate(moves):
                    move_field = f"{key}.Moves[{move_index}]"
                    if _check_fields(items, "runtime_scenario", name, move, move_fields, move_field):
                        assert isinstance(move, dict)
                        slot = move.get("SlotIndex")
                        _check_int(items, "runtime_scenario", name, f"{move_field}.SlotIndex", slot, 0, 3)
                        if _is_int(slot) and slot in slots:
                            _diag(items, "runtime_scenario", name, f"{move_field}.SlotIndex", "identity.duplicate", "Move slot is duplicated.")
                        slots.add(slot) if _is_int(slot) else None
                        move_id = move.get("MoveId")
                        if _check_id(items, "runtime_scenario", name, f"{move_field}.MoveId", move_id, "Move.") and str(move_id).casefold() not in indexes["moves"]:
                            _diag(items, "runtime_scenario", name, f"{move_field}.MoveId", "reference.missing", "Move is not in the catalog.")
                        _check_int(items, "runtime_scenario", name, f"{move_field}.PPUps", move.get("PPUps"), 0, 3)
    active = row.get("StartingActive")
    if not isinstance(active, list) or not active:
        _diag(items, "runtime_scenario", name, "StartingActive", "range.invalid", "Starting-active assignments are required.")
    else:
        for index, assignment in enumerate(active):
            key = f"StartingActive[{index}]"
            if _check_fields(items, "runtime_scenario", name, assignment, {"Side", "Position", "TrainerId", "BattlerId"}, key):
                assert isinstance(assignment, dict)
                _check_enum(items, "runtime_scenario", name, f"{key}.Side", assignment.get("Side"), {"Player", "Opponent"})
                _check_enum(items, "runtime_scenario", name, f"{key}.Position", assignment.get("Position"), {"Left", "Right"})
                if str(assignment.get("TrainerId")) not in trainer_ids:
                    _diag(items, "runtime_scenario", name, f"{key}.TrainerId", "reference.missing", "Active Trainer is not declared.")
                if str(assignment.get("BattlerId")) not in battler_ids:
                    _diag(items, "runtime_scenario", name, f"{key}.BattlerId", "reference.missing", "Active Battler is not declared.")
    obedience = row.get("ObedienceInputs")
    if not isinstance(obedience, list):
        _diag(items, "runtime_scenario", name, "ObedienceInputs", "field.type", "Expected an array.")
    else:
        for index, entry in enumerate(obedience):
            key = f"ObedienceInputs[{index}]"
            if _check_fields(items, "runtime_scenario", name, entry, {"BattlerId", "bSubjectToPlayerCap", "ReferenceLevel", "BadgeCount"}, key):
                assert isinstance(entry, dict)
                if str(entry.get("BattlerId")) not in battler_ids:
                    _diag(items, "runtime_scenario", name, f"{key}.BattlerId", "reference.missing", "Obedience Battler is not declared.")
                _check_bool(items, "runtime_scenario", name, f"{key}.bSubjectToPlayerCap", entry.get("bSubjectToPlayerCap"))
                _check_int(items, "runtime_scenario", name, f"{key}.ReferenceLevel", entry.get("ReferenceLevel"), 1, 100)
                _check_int(items, "runtime_scenario", name, f"{key}.BadgeCount", entry.get("BadgeCount"), 0, 8)


def validate_documents(
    documents: Mapping[str, object],
    raw_documents: Mapping[str, bytes],
    import_families: Sequence[str] = MUTABLE_FAMILIES,
    *,
    accepted_hashes: Mapping[str, str] = ACCEPTED_C10A_HASHES,
    pinned_manifest_sha256: str = ACCEPTED_PINNED_MANIFEST_SHA256,
) -> ValidationResult:
    """Validate a complete in-memory bundle without changing any caller input."""
    docs = copy.deepcopy(dict(documents))
    raws = dict(raw_documents)
    items = list(validate_import_allowlist(import_families))
    for family in SOURCE_FILES:
        if family not in docs:
            _diag(items, family, "document", "document", "document.missing", "Source document is missing.")
        if family not in raws:
            _diag(items, family, "document", "document", "hash.input_missing", "Raw source bytes are missing.")
    hashes = {family: hashlib.sha256(raw).hexdigest().upper() for family, raw in raws.items() if isinstance(raw, bytes)}
    for family, expected in accepted_hashes.items():
        if hashes.get(family) != expected.upper():
            _diag(items, family, "document", "sha256", "hash.mismatch", f"Expected accepted SHA-256 {expected.upper()}, got {hashes.get(family, 'missing')}.")
    if pinned_manifest_sha256.upper() != ACCEPTED_PINNED_MANIFEST_SHA256:
        _diag(items, "manifest", "pinned", "sha256", "manifest.mismatch", f"Expected pinned manifest SHA-256 {ACCEPTED_PINNED_MANIFEST_SHA256}.")
    indexes = _validate_simple_families(items, docs)
    _validate_catalog_rows(items, docs, indexes)
    _validate_type_chart(items, _rows_no_diag(docs.get("type_chart")))
    _validate_scenario(items, _rows_no_diag(docs.get("runtime_scenario")), indexes)
    counts = {family: len(value) if isinstance(value, list) else 0 for family, value in docs.items() if family in SOURCE_FILES}
    return ValidationResult(tuple(sorted(set(items))), MappingProxyType(dict(counts)), MappingProxyType(dict(hashes)), pinned_manifest_sha256.upper(), tuple(import_families))


def load_source_documents(source_directory: Path) -> tuple[dict[str, object], dict[str, bytes], tuple[Diagnostic, ...]]:
    documents: dict[str, object] = {}
    raw_documents: dict[str, bytes] = {}
    items: list[Diagnostic] = []
    for family, file_name in SOURCE_FILES.items():
        path = source_directory / file_name
        try:
            raw = path.read_bytes()
        except OSError as error:
            _diag(items, family, "document", "file", "file.read", str(error))
            continue
        raw_documents[family] = raw
        try:
            documents[family] = json.loads(raw.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as error:
            _diag(items, family, "document", "json", "json.decode", str(error))
    return documents, raw_documents, tuple(sorted(items))


def validate_source_bundle(
    source_directory: Path,
    import_families: Sequence[str] = MUTABLE_FAMILIES,
    *,
    pinned_manifest_sha256: str = ACCEPTED_PINNED_MANIFEST_SHA256,
) -> ValidationResult:
    documents, raws, load_diagnostics = load_source_documents(source_directory)
    result = validate_documents(documents, raws, import_families, pinned_manifest_sha256=pinned_manifest_sha256)
    if not load_diagnostics:
        return result
    return ValidationResult(tuple(sorted(set(result.diagnostics + load_diagnostics))), result.counts, result.source_sha256, result.pinned_manifest_sha256, result.import_families)


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, default=Path(__file__).resolve().parent / "Initial")
    parser.add_argument("--family", action="append", dest="families")
    parser.add_argument("--pinned-manifest-sha256", default=ACCEPTED_PINNED_MANIFEST_SHA256)
    args = parser.parse_args(argv)
    result = validate_source_bundle(args.source_dir, tuple(args.families) if args.families else MUTABLE_FAMILIES, pinned_manifest_sha256=args.pinned_manifest_sha256)
    print(json.dumps(result.to_dict(), ensure_ascii=False, indent=2, sort_keys=True))
    return 0 if result.is_valid else 1


if __name__ == "__main__":
    raise SystemExit(main())
