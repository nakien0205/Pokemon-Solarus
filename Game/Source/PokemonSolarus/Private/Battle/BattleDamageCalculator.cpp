#include "Battle/BattleDamageCalculator.h"

#include "Math/NumericLimits.h"

bool FBattleDamageCalculator::TryCalculateDamage(
	const int32 AttackerLevel,
	const FPokemonBattleStats& AttackerStats,
	const FPokemonBattleStats& DefenderStats,
	const EBattleMoveCategory MoveCategory,
	const int32 MovePower,
	int32& OutDamage)
{
	OutDamage = 0;

	if (AttackerLevel < 1 || AttackerLevel > 100 || MovePower <= 0)
	{
		return false;
	}

	int32 OffensiveStat = 0;
	int32 DefensiveStat = 0;
	switch (MoveCategory)
	{
	case EBattleMoveCategory::Physical:
		OffensiveStat = AttackerStats.Attack;
		DefensiveStat = DefenderStats.Defense;
		break;
	case EBattleMoveCategory::Special:
		OffensiveStat = AttackerStats.SpecialAttack;
		DefensiveStat = DefenderStats.SpecialDefense;
		break;
	case EBattleMoveCategory::Status:
	default:
		return false;
	}

	if (OffensiveStat <= 0 || DefensiveStat <= 0)
	{
		return false;
	}

	const int64 LevelFactor = (2LL * static_cast<int64>(AttackerLevel)) / 5LL + 2LL;
	const int64 MovePower64 = static_cast<int64>(MovePower);
	const int64 OffensiveStat64 = static_cast<int64>(OffensiveStat);
	const int64 MaxIntermediate = TNumericLimits<int64>::Max();

	if (LevelFactor > MaxIntermediate / MovePower64)
	{
		return false;
	}

	const int64 LevelPower = LevelFactor * MovePower64;
	if (LevelPower > MaxIntermediate / OffensiveStat64)
	{
		return false;
	}

	const int64 ScaledDamage =
		(LevelPower * OffensiveStat64) / static_cast<int64>(DefensiveStat);
	const int64 BaseDamage = FMath::Max<int64>(1LL, ScaledDamage / 50LL + 2LL);
	if (BaseDamage > static_cast<int64>(TNumericLimits<int32>::Max()))
	{
		return false;
	}

	OutDamage = static_cast<int32>(BaseDamage);
	return true;
}
