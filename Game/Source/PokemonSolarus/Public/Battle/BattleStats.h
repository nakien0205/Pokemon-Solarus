#pragma once

#include "CoreMinimal.h"
#include "Battle/BattleSetupTypes.h"

/** One complete six-stat source block used for base stats, IVs, or EVs. */
struct POKEMONSOLARUS_API FPokemonStatValues
{
	int32 HP = 0;
	int32 Attack = 0;
	int32 Defense = 0;
	int32 SpecialAttack = 0;
	int32 SpecialDefense = 0;
	int32 Speed = 0;
};

/** Validated inputs from which immutable battle-entry stats are calculated. */
struct POKEMONSOLARUS_API FPokemonStatInputs
{
	int32 Level = 0;
	FPokemonStatValues BaseStats;
	FPokemonStatValues IndividualValues;
	FPokemonStatValues EffortValues;
	FNatureStatModifier NatureModifier;
};

/** Calculated numeric stats consumed by battle rules for one Pokemon. */
struct POKEMONSOLARUS_API FPokemonBattleStats
{
	int32 MaxHP = 0;
	int32 Attack = 0;
	int32 Defense = 0;
	int32 SpecialAttack = 0;
	int32 SpecialDefense = 0;
	int32 Speed = 0;
};
