#pragma once

#include "CoreMinimal.h"
#include "Battle/BattleDefinitions.h"
#include "Battle/BattleTypeChart.h"

/** Stable definition family used to sort and interpret catalog diagnostics. */
enum class EBattleDefinitionFamily : uint8
{
	TypeChart = 0,
	SpeciesForm = 1,
	Nature = 2,
	Move = 3,
	Ability = 4,
	Item = 5,
	Condition = 6,
	Adapter = 7
};

/** Typed content or adapter failure; it is never player-facing text. */
enum class EBattleCatalogDiagnosticCode : uint8
{
	InvalidIdentity = 0,
	DuplicateIdentity = 1,
	MissingReference = 2,
	InvalidEnum = 3,
	InvalidRange = 4,
	InvalidEffectOrder = 5,
	IncompatibleEffect = 6,
	InvalidTypeChartEntry = 7,
	DuplicateTypeChartEntry = 8,
	IncompleteTypeChart = 9,
	MissingTable = 10,
	WrongRowType = 11,
	MissingRow = 12,
	InvalidAuthoredValue = 13
};

/** One deterministic typed catalog diagnostic with stable definition and field context. */
struct POKEMONSOLARUS_API FBattleCatalogDiagnostic
{
	EBattleCatalogDiagnosticCode Code = EBattleCatalogDiagnosticCode::InvalidAuthoredValue;
	EBattleDefinitionFamily Family = EBattleDefinitionFamily::Adapter;
	FDefinitionId DefinitionId;
	FName Field = NAME_None;
	int32 EntryIndex = INDEX_NONE;

	/** Provides canonical diagnostic ordering independent of input or map iteration order. */
	[[nodiscard]] static bool Less(
		const FBattleCatalogDiagnostic& Left,
		const FBattleCatalogDiagnostic& Right);

	friend bool operator==(const FBattleCatalogDiagnostic& Left, const FBattleCatalogDiagnostic& Right)
	{
		return Left.Code == Right.Code
			&& Left.Family == Right.Family
			&& Left.DefinitionId == Right.DefinitionId
			&& Left.Field == Right.Field
			&& Left.EntryIndex == Right.EntryIndex;
	}

	friend bool operator!=(const FBattleCatalogDiagnostic& Left, const FBattleCatalogDiagnostic& Right)
	{
		return !(Left == Right);
	}
};

/** Mutable construction input copied and canonicalized into an immutable catalog. */
struct POKEMONSOLARUS_API FBattleDefinitionCatalogInput
{
	TArray<FBattleTypeChartEntry> TypeChartEntries;
	TArray<FBattleSpeciesFormDefinition> SpeciesForms;
	TArray<FBattleNatureDefinition> Natures;
	TArray<FBattleMoveDefinition> Moves;
	TArray<FBattleAbilityDefinition> Abilities;
	TArray<FBattleItemDefinition> Items;
	TArray<FBattleConditionDefinition> Conditions;
};

/**
 * Validated immutable-by-interface battle definition snapshot.
 * Construction is atomic and canonicalizes every definition family by stable ID.
 */
class POKEMONSOLARUS_API FBattleDefinitionCatalog
{
public:
	/** Creates an invalid empty catalog. */
	FBattleDefinitionCatalog() = default;

	/** Validates every record and cross-reference, returning deterministic diagnostics on failure. */
	[[nodiscard]] static bool TryCreate(
		const FBattleDefinitionCatalogInput& Input,
		FBattleDefinitionCatalog& OutCatalog,
		TArray<FBattleCatalogDiagnostic>& OutDiagnostics);

	/** Returns whether construction completed without any diagnostic. */
	[[nodiscard]] bool IsValid() const
	{
		return bValid;
	}

	/** Returns the complete immutable type chart. */
	[[nodiscard]] const FBattleTypeChart& GetTypeChart() const
	{
		return TypeChart;
	}

	/** Returns species/form definitions in canonical lexical ID order. */
	[[nodiscard]] TConstArrayView<FBattleSpeciesFormDefinition> GetSpeciesForms() const
	{
		return SpeciesForms;
	}

	/** Returns nature definitions in canonical lexical ID order. */
	[[nodiscard]] TConstArrayView<FBattleNatureDefinition> GetNatures() const
	{
		return Natures;
	}

	/** Returns move definitions in canonical lexical ID order. */
	[[nodiscard]] TConstArrayView<FBattleMoveDefinition> GetMoves() const
	{
		return Moves;
	}

	/** Returns Ability definitions in canonical lexical ID order. */
	[[nodiscard]] TConstArrayView<FBattleAbilityDefinition> GetAbilities() const
	{
		return Abilities;
	}

	/** Returns item definitions in canonical lexical ID order. */
	[[nodiscard]] TConstArrayView<FBattleItemDefinition> GetItems() const
	{
		return Items;
	}

	/** Returns condition definitions in canonical lexical ID order. */
	[[nodiscard]] TConstArrayView<FBattleConditionDefinition> GetConditions() const
	{
		return Conditions;
	}

	/** Finds a species/form by stable ID, or returns null. */
	[[nodiscard]] const FBattleSpeciesFormDefinition* FindSpeciesForm(FSpeciesFormId Id) const;

	/** Finds a nature by stable ID, or returns null. */
	[[nodiscard]] const FBattleNatureDefinition* FindNature(FNatureId Id) const;

	/** Finds a move by stable ID, or returns null. */
	[[nodiscard]] const FBattleMoveDefinition* FindMove(FMoveId Id) const;

	/** Finds an Ability by stable ID, or returns null. */
	[[nodiscard]] const FBattleAbilityDefinition* FindAbility(FAbilityId Id) const;

	/** Finds an item by stable ID, or returns null. */
	[[nodiscard]] const FBattleItemDefinition* FindItem(FItemId Id) const;

	/** Finds a condition by stable ID, or returns null. */
	[[nodiscard]] const FBattleConditionDefinition* FindCondition(FConditionId Id) const;

private:
	bool bValid = false;
	FBattleTypeChart TypeChart;
	TArray<FBattleSpeciesFormDefinition> SpeciesForms;
	TArray<FBattleNatureDefinition> Natures;
	TArray<FBattleMoveDefinition> Moves;
	TArray<FBattleAbilityDefinition> Abilities;
	TArray<FBattleItemDefinition> Items;
	TArray<FBattleConditionDefinition> Conditions;
};
