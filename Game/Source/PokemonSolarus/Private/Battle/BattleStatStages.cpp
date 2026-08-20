#include "Battle/BattleStatStages.h"

bool FBattleStatStages::TryGetStage(const EBattleStat Stat, int32& OutStage) const
{
	OutStage = 0;
	switch (Stat)
	{
	case EBattleStat::Attack:
		OutStage = Attack;
		return true;
	case EBattleStat::Defense:
		OutStage = Defense;
		return true;
	case EBattleStat::SpecialAttack:
		OutStage = SpecialAttack;
		return true;
	case EBattleStat::SpecialDefense:
		OutStage = SpecialDefense;
		return true;
	case EBattleStat::Speed:
		OutStage = Speed;
		return true;
	case EBattleStat::Accuracy:
		OutStage = Accuracy;
		return true;
	case EBattleStat::Evasion:
		OutStage = Evasion;
		return true;
	default:
		return false;
	}
}

FBattleStatStageChangeResult FBattleStatStages::ApplyChange(
	const EBattleStat Stat,
	const int32 RequestedDelta)
{
	FBattleStatStageChangeResult Result;
	Result.RequestedDelta = RequestedDelta;

	if (!TryGetStage(Stat, Result.PreviousStage))
	{
		return Result;
	}

	const int64 RequestedStage =
		static_cast<int64>(Result.PreviousStage) + static_cast<int64>(RequestedDelta);
	const int64 ClampedStage = FMath::Clamp<int64>(
		RequestedStage,
		static_cast<int64>(MinimumStage),
		static_cast<int64>(MaximumStage));
	Result.NewStage = static_cast<int32>(ClampedStage);
	Result.AppliedDelta = Result.NewStage - Result.PreviousStage;
	Result.bClamped = ClampedStage != RequestedStage;
	Result.Outcome = Result.AppliedDelta == 0
		? EBattleStatStageChangeOutcome::Blocked
		: EBattleStatStageChangeOutcome::Applied;

	if (!TrySetStage(Stat, static_cast<int8>(Result.NewStage)))
	{
		return FBattleStatStageChangeResult();
	}
	return Result;
}

bool FBattleStatStages::TrySetStage(const EBattleStat Stat, const int8 NewStage)
{
	if (NewStage < MinimumStage || NewStage > MaximumStage)
	{
		return false;
	}

	switch (Stat)
	{
	case EBattleStat::Attack:
		Attack = NewStage;
		return true;
	case EBattleStat::Defense:
		Defense = NewStage;
		return true;
	case EBattleStat::SpecialAttack:
		SpecialAttack = NewStage;
		return true;
	case EBattleStat::SpecialDefense:
		SpecialDefense = NewStage;
		return true;
	case EBattleStat::Speed:
		Speed = NewStage;
		return true;
	case EBattleStat::Accuracy:
		Accuracy = NewStage;
		return true;
	case EBattleStat::Evasion:
		Evasion = NewStage;
		return true;
	default:
		return false;
	}
}
