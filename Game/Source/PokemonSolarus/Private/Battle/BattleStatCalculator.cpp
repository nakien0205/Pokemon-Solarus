#include "Battle/BattleStatCalculator.h"

#include "Math/NumericLimits.h"

namespace
{
	bool IsKnownBattleStatNature(const ENatureStat Stat)
	{
		return Stat == ENatureStat::None
			|| Stat == ENatureStat::Attack
			|| Stat == ENatureStat::Defense
			|| Stat == ENatureStat::SpecialAttack
			|| Stat == ENatureStat::SpecialDefense
			|| Stat == ENatureStat::Speed;
	}

	bool IsValidBattleStatNatureModifier(const FNatureStatModifier& Modifier)
	{
		const ENatureStat BoostedStat = Modifier.GetBoostedStat();
		const ENatureStat ReducedStat = Modifier.GetReducedStat();
		if (!IsKnownBattleStatNature(BoostedStat) || !IsKnownBattleStatNature(ReducedStat))
		{
			return false;
		}

		const bool bBoostIsNone = BoostedStat == ENatureStat::None;
		const bool bReductionIsNone = ReducedStat == ENatureStat::None;
		return bBoostIsNone == bReductionIsNone
			&& (bBoostIsNone || BoostedStat != ReducedStat);
	}

	bool TryStoreCalculatedStat(const int64 Value, int32& OutValue)
	{
		OutValue = 0;
		if (Value < 0 || Value > static_cast<int64>(TNumericLimits<int32>::Max()))
		{
			return false;
		}
		OutValue = static_cast<int32>(Value);
		return true;
	}

	bool TryCalculateHP(
		const int32 Level,
		const int32 BaseStat,
		const int32 IndividualValue,
		const int32 EffortValue,
		int32& OutValue)
	{
		if (BaseStat == 1)
		{
			OutValue = 1;
			return true;
		}

		const int64 EffortTerm = static_cast<int64>(EffortValue / 4);
		const int64 Inner = 2LL * static_cast<int64>(BaseStat)
			+ static_cast<int64>(IndividualValue)
			+ EffortTerm;
		const int64 Value = (Inner * static_cast<int64>(Level)) / 100LL
			+ static_cast<int64>(Level)
			+ 10LL;
		return TryStoreCalculatedStat(Value, OutValue);
	}

	bool TryCalculateNonHPStat(
		const int32 Level,
		const int32 BaseStat,
		const int32 IndividualValue,
		const int32 EffortValue,
		const ENatureStat NatureStat,
		const FNatureStatModifier& NatureModifier,
		int32& OutValue)
	{
		int32 Numerator = 0;
		int32 Denominator = 0;
		if (!NatureModifier.TryGetMultiplier(NatureStat, Numerator, Denominator)
			|| Denominator <= 0)
		{
			OutValue = 0;
			return false;
		}

		const int64 EffortTerm = static_cast<int64>(EffortValue / 4);
		const int64 Inner = 2LL * static_cast<int64>(BaseStat)
			+ static_cast<int64>(IndividualValue)
			+ EffortTerm;
		const int64 BeforeNature =
			(Inner * static_cast<int64>(Level)) / 100LL + 5LL;
		const int64 Value =
			(BeforeNature * static_cast<int64>(Numerator)) / static_cast<int64>(Denominator);
		return TryStoreCalculatedStat(Value, OutValue);
	}

	void GetBattleStageRatio(const int32 Stage, int32& OutNumerator, int32& OutDenominator)
	{
		if (Stage < 0)
		{
			OutNumerator = 2;
			OutDenominator = 2 - Stage;
			return;
		}
		if (Stage > 0)
		{
			OutNumerator = 2 + Stage;
			OutDenominator = 2;
			return;
		}
		OutNumerator = 1;
		OutDenominator = 1;
	}

	void GetAccuracyStageRatio(const int32 Stage, int32& OutNumerator, int32& OutDenominator)
	{
		if (Stage < 0)
		{
			OutNumerator = 3;
			OutDenominator = 3 - Stage;
			return;
		}
		if (Stage > 0)
		{
			OutNumerator = 3 + Stage;
			OutDenominator = 3;
			return;
		}
		OutNumerator = 3;
		OutDenominator = 3;
	}
}

bool FBattleStatCalculator::TryCalculatePermanentStats(
	const FPokemonStatInputs& Inputs,
	FPokemonBattleStats& OutStats,
	EBattleStatCalculationError& OutError)
{
	OutStats = FPokemonBattleStats();
	OutError = EBattleStatCalculationError::None;

	auto Fail = [&OutError](const EBattleStatCalculationError Error)
	{
		OutError = Error;
		return false;
	};

	if (Inputs.Level < 1 || Inputs.Level > 100)
	{
		return Fail(EBattleStatCalculationError::InvalidLevel);
	}

	const int32 BaseStats[] =
	{
		Inputs.BaseStats.HP,
		Inputs.BaseStats.Attack,
		Inputs.BaseStats.Defense,
		Inputs.BaseStats.SpecialAttack,
		Inputs.BaseStats.SpecialDefense,
		Inputs.BaseStats.Speed
	};
	const int32 IndividualValues[] =
	{
		Inputs.IndividualValues.HP,
		Inputs.IndividualValues.Attack,
		Inputs.IndividualValues.Defense,
		Inputs.IndividualValues.SpecialAttack,
		Inputs.IndividualValues.SpecialDefense,
		Inputs.IndividualValues.Speed
	};
	const int32 EffortValues[] =
	{
		Inputs.EffortValues.HP,
		Inputs.EffortValues.Attack,
		Inputs.EffortValues.Defense,
		Inputs.EffortValues.SpecialAttack,
		Inputs.EffortValues.SpecialDefense,
		Inputs.EffortValues.Speed
	};

	int64 TotalEffortValues = 0;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(BaseStats); ++Index)
	{
		if (BaseStats[Index] <= 0)
		{
			return Fail(EBattleStatCalculationError::InvalidBaseStat);
		}
		if (IndividualValues[Index] < 0 || IndividualValues[Index] > 31)
		{
			return Fail(EBattleStatCalculationError::InvalidIndividualValue);
		}
		if (EffortValues[Index] < 0 || EffortValues[Index] > 252)
		{
			return Fail(EBattleStatCalculationError::InvalidEffortValue);
		}
		TotalEffortValues += static_cast<int64>(EffortValues[Index]);
	}
	if (TotalEffortValues > 510)
	{
		return Fail(EBattleStatCalculationError::EffortValueTotalExceeded);
	}
	if (!IsValidBattleStatNatureModifier(Inputs.NatureModifier))
	{
		return Fail(EBattleStatCalculationError::InvalidNatureModifier);
	}

	FPokemonBattleStats CalculatedStats;
	if (!TryCalculateHP(
			Inputs.Level,
			Inputs.BaseStats.HP,
			Inputs.IndividualValues.HP,
			Inputs.EffortValues.HP,
			CalculatedStats.MaxHP)
		|| !TryCalculateNonHPStat(
			Inputs.Level,
			Inputs.BaseStats.Attack,
			Inputs.IndividualValues.Attack,
			Inputs.EffortValues.Attack,
			ENatureStat::Attack,
			Inputs.NatureModifier,
			CalculatedStats.Attack)
		|| !TryCalculateNonHPStat(
			Inputs.Level,
			Inputs.BaseStats.Defense,
			Inputs.IndividualValues.Defense,
			Inputs.EffortValues.Defense,
			ENatureStat::Defense,
			Inputs.NatureModifier,
			CalculatedStats.Defense)
		|| !TryCalculateNonHPStat(
			Inputs.Level,
			Inputs.BaseStats.SpecialAttack,
			Inputs.IndividualValues.SpecialAttack,
			Inputs.EffortValues.SpecialAttack,
			ENatureStat::SpecialAttack,
			Inputs.NatureModifier,
			CalculatedStats.SpecialAttack)
		|| !TryCalculateNonHPStat(
			Inputs.Level,
			Inputs.BaseStats.SpecialDefense,
			Inputs.IndividualValues.SpecialDefense,
			Inputs.EffortValues.SpecialDefense,
			ENatureStat::SpecialDefense,
			Inputs.NatureModifier,
			CalculatedStats.SpecialDefense)
		|| !TryCalculateNonHPStat(
			Inputs.Level,
			Inputs.BaseStats.Speed,
			Inputs.IndividualValues.Speed,
			Inputs.EffortValues.Speed,
			ENatureStat::Speed,
			Inputs.NatureModifier,
			CalculatedStats.Speed))
	{
		return Fail(EBattleStatCalculationError::ArithmeticOverflow);
	}

	OutStats = CalculatedStats;
	return true;
}

bool FBattleStatCalculator::TryCalculateEffectiveStat(
	const FPokemonBattleStats& PermanentStats,
	const FBattleStatStages& Stages,
	const EBattleStat Stat,
	int32& OutEffectiveStat)
{
	OutEffectiveStat = 0;

	int32 PermanentValue = 0;
	switch (Stat)
	{
	case EBattleStat::Attack:
		PermanentValue = PermanentStats.Attack;
		break;
	case EBattleStat::Defense:
		PermanentValue = PermanentStats.Defense;
		break;
	case EBattleStat::SpecialAttack:
		PermanentValue = PermanentStats.SpecialAttack;
		break;
	case EBattleStat::SpecialDefense:
		PermanentValue = PermanentStats.SpecialDefense;
		break;
	case EBattleStat::Speed:
		PermanentValue = PermanentStats.Speed;
		break;
	case EBattleStat::Accuracy:
	case EBattleStat::Evasion:
	default:
		return false;
	}
	if (PermanentValue <= 0)
	{
		return false;
	}

	int32 Stage = 0;
	if (!Stages.TryGetStage(Stat, Stage))
	{
		return false;
	}
	int32 Numerator = 0;
	int32 Denominator = 0;
	GetBattleStageRatio(Stage, Numerator, Denominator);
	const int64 EffectiveValue =
		(static_cast<int64>(PermanentValue) * static_cast<int64>(Numerator))
		/ static_cast<int64>(Denominator);
	return TryStoreCalculatedStat(EffectiveValue, OutEffectiveStat);
}

bool FBattleStatCalculator::TryCalculateEffectiveAccuracy(
	const int32 BaseAccuracy,
	const FBattleStatStages& AttackerStages,
	const FBattleStatStages& DefenderStages,
	int32& OutEffectiveAccuracy)
{
	OutEffectiveAccuracy = 0;
	if (BaseAccuracy <= 0)
	{
		return false;
	}

	int32 AccuracyStage = 0;
	int32 EvasionStage = 0;
	if (!AttackerStages.TryGetStage(EBattleStat::Accuracy, AccuracyStage)
		|| !DefenderStages.TryGetStage(EBattleStat::Evasion, EvasionStage))
	{
		return false;
	}

	const int32 CombinedStage = FMath::Clamp(
		AccuracyStage - EvasionStage,
		FBattleStatStages::MinimumStage,
		FBattleStatStages::MaximumStage);
	int32 Numerator = 0;
	int32 Denominator = 0;
	GetAccuracyStageRatio(CombinedStage, Numerator, Denominator);
	const int64 EffectiveAccuracy =
		(static_cast<int64>(BaseAccuracy) * static_cast<int64>(Numerator))
		/ static_cast<int64>(Denominator);
	return TryStoreCalculatedStat(EffectiveAccuracy, OutEffectiveAccuracy);
}
