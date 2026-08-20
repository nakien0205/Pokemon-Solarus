#pragma once

#include "CoreMinimal.h"
#include "Battle/BattleDefinitions.h"

/** Typed reason a complete type chart was rejected. */
enum class EBattleTypeChartValidationError : uint8
{
	None = 0,
	InvalidType = 1,
	InvalidMultiplier = 2,
	DuplicateEntry = 3,
	IncompleteChart = 4
};

/** One authored attacking/defending type-chart cell using an exact rational value. */
struct POKEMONSOLARUS_API FBattleTypeChartEntry
{
	EPokemonType AttackingType = EPokemonType::Invalid;
	EPokemonType DefendingType = EPokemonType::Invalid;
	int32 Numerator = 0;
	int32 Denominator = 1;
};

/** Exact single- or dual-type effectiveness result. */
struct POKEMONSOLARUS_API FBattleTypeEffectiveness
{
	int32 Numerator = 0;
	int32 Denominator = 1;

	/** Returns whether an immunity made the exact product zero. */
	[[nodiscard]] bool IsImmune() const
	{
		return Numerator == 0;
	}
};

/**
 * Validated immutable complete 18x18 chart.
 * Storage uses exact quarter units, where four represents neutral effectiveness.
 */
class POKEMONSOLARUS_API FBattleTypeChart
{
public:
	static constexpr int32 TypeCount = 18;
	static constexpr int32 EntryCount = TypeCount * TypeCount;

	/** Creates an invalid empty chart. */
	FBattleTypeChart() = default;

	/** Validates all 324 cells and atomically constructs a complete chart. */
	[[nodiscard]] static bool TryCreate(
		TConstArrayView<FBattleTypeChartEntry> Entries,
		FBattleTypeChart& OutChart,
		EBattleTypeChartValidationError& OutError);

	/** Returns whether construction validated one complete chart. */
	[[nodiscard]] bool IsValid() const
	{
		return bValid;
	}

	/** Returns whether the enum value names one of the supported 18 types. */
	[[nodiscard]] static bool IsKnownType(EPokemonType Type);

	/** Reads one exact attacking-versus-defending multiplier. */
	[[nodiscard]] bool TryGetEffectiveness(
		EPokemonType AttackingType,
		EPokemonType DefendingType,
		FBattleTypeEffectiveness& OutEffectiveness) const;

	/**
	 * Multiplies the first stored defending type and then the second without rounding.
	 * Duplicate defending types and invalid enum values are rejected.
	 */
	[[nodiscard]] bool TryGetDualEffectiveness(
		EPokemonType AttackingType,
		EPokemonType FirstDefendingType,
		EPokemonType SecondDefendingType,
		FBattleTypeEffectiveness& OutEffectiveness) const;

private:
	[[nodiscard]] static bool TryEncodeQuarterUnits(
		int32 Numerator,
		int32 Denominator,
		uint8& OutQuarterUnits);
	[[nodiscard]] static FBattleTypeEffectiveness DecodeQuarterUnits(uint8 QuarterUnits);
	[[nodiscard]] static int32 ToIndex(EPokemonType AttackingType, EPokemonType DefendingType);

	bool bValid = false;
	TStaticArray<uint8, EntryCount> QuarterUnits{};
};
