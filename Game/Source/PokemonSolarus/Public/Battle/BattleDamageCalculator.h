#pragma once

#include "CoreMinimal.h"
#include "Battle/BattleStats.h"

/** Identifies which calculated offensive and defensive stats a move uses. */
enum class EBattleMoveCategory : uint8
{
	Physical,
	Special,
	Status
};

/** Calculates deterministic base damage from supplied level, stats, category, and power. */
class POKEMONSOLARUS_API FBattleDamageCalculator
{
public:
	/**
	 * Attempts the reusable base-damage calculation.
	 * Invalid input returns false and resets OutDamage to zero.
	 */
	static bool TryCalculateDamage(
		int32 AttackerLevel,
		const FPokemonBattleStats& AttackerStats,
		const FPokemonBattleStats& DefenderStats,
		EBattleMoveCategory MoveCategory,
		int32 MovePower,
		int32& OutDamage);
};
