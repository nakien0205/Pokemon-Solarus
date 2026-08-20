#pragma once

#include "CoreMinimal.h"

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
