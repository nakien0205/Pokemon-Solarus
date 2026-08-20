#pragma once

#include "CoreMinimal.h"
#include "Battle/BattleStatStages.h"
#include "Battle/BattleStats.h"

/** Typed reason a permanent-stat calculation was rejected. */
enum class EBattleStatCalculationError : uint8
{
	None = 0,
	InvalidLevel = 1,
	InvalidBaseStat = 2,
	InvalidIndividualValue = 3,
	InvalidEffortValue = 4,
	EffortValueTotalExceeded = 5,
	InvalidNatureModifier = 6,
	ArithmeticOverflow = 7
};

/** Pure modern-stat, temporary-stage, and accuracy/evasion calculations. */
class POKEMONSOLARUS_API FBattleStatCalculator
{
public:
	/**
	 * Validates all inputs and calculates one immutable battle-entry stat block.
	 * Failure resets OutStats and returns a typed error.
	 */
	[[nodiscard]] static bool TryCalculatePermanentStats(
		const FPokemonStatInputs& Inputs,
		FPokemonBattleStats& OutStats,
		EBattleStatCalculationError& OutError);

	/**
	 * Applies the matching temporary stage to one permanent non-HP stat.
	 * Accuracy, Evasion, invalid values, and unrepresentable results are rejected.
	 */
	[[nodiscard]] static bool TryCalculateEffectiveStat(
		const FPokemonBattleStats& PermanentStats,
		const FBattleStatStages& Stages,
		EBattleStat Stat,
		int32& OutEffectiveStat);

	/**
	 * Applies the clamped attacker-Accuracy minus defender-Evasion stage ratio.
	 * The numeric result is deliberately not clamped to 100.
	 */
	[[nodiscard]] static bool TryCalculateEffectiveAccuracy(
		int32 BaseAccuracy,
		const FBattleStatStages& AttackerStages,
		const FBattleStatStages& DefenderStages,
		int32& OutEffectiveAccuracy);
};
