#include "Battle/BattleFinalDamageCalculator.h"

#include "Battle/BattleDamageCalculator.h"
#include "Battle/BattleStatCalculator.h"
#include "Math/NumericLimits.h"

namespace BattleFinalDamageCalculatorPrivate
{
	constexpr uint64 UInt32Modulus = 1ULL << 32;
	constexpr uint64 UInt16Modulus = 1ULL << 16;
	constexpr uint64 PokeRoundOffset = 2047;
	constexpr uint64 ChainRoundOffset = 2048;

	void AddTrace(
		FDamageTrace& Trace,
		const EBattleDamageTraceStep Step,
		const int64 Value,
		const FDefinitionId& RuleId = FDefinitionId())
	{
		FBattleDamageTraceEntry& Entry = Trace.Entries.AddDefaulted_GetRef();
		Entry.Step = Step;
		Entry.Value = Value;
		Entry.RuleId = RuleId;
	}

	bool TryMultiply(const uint64 Left, const uint64 Right, uint64& OutProduct)
	{
		OutProduct = 0;
		if (Right != 0 && Left > TNumericLimits<uint64>::Max() / Right)
		{
			return false;
		}
		OutProduct = Left * Right;
		return true;
	}

	bool TryAdd(const uint64 Left, const uint64 Right, uint64& OutSum)
	{
		OutSum = 0;
		if (Left > TNumericLimits<uint64>::Max() - Right)
		{
			return false;
		}
		OutSum = Left + Right;
		return true;
	}

	uint32 ApplyOF32(const uint64 Value)
	{
		return static_cast<uint32>(Value % UInt32Modulus);
	}

	uint16 ApplyOF16(const uint64 Value)
	{
		return static_cast<uint16>(Value % UInt16Modulus);
	}

	bool IsKnownDamageCategory(const EBattleMoveCategory Category)
	{
		return Category == EBattleMoveCategory::Physical
			|| Category == EBattleMoveCategory::Special;
	}

	bool IsKnownWeatherModifier(const int32 ModifierQ12)
	{
		return ModifierQ12 == 2048
			|| ModifierQ12 == FBattleFinalDamageCalculator::Q12Neutral
			|| ModifierQ12 == 6144;
	}

	bool IsKnownStabModifier(const int32 ModifierQ12)
	{
		return ModifierQ12 == FBattleFinalDamageCalculator::Q12Neutral
			|| ModifierQ12 == 6144;
	}

	bool IsKnownTypeEffectiveness(const FBattleTypeEffectiveness& Effectiveness)
	{
		return (Effectiveness.Numerator == 0 && Effectiveness.Denominator == 1)
			|| (Effectiveness.Numerator == 1 && Effectiveness.Denominator == 4)
			|| (Effectiveness.Numerator == 1 && Effectiveness.Denominator == 2)
			|| (Effectiveness.Numerator == 1 && Effectiveness.Denominator == 1)
			|| (Effectiveness.Numerator == 2 && Effectiveness.Denominator == 1)
			|| (Effectiveness.Numerator == 4 && Effectiveness.Denominator == 1);
	}

	bool IsValidModifier(
		const FBattleDamageModifier& Modifier,
		const bool bAllowCriticalIgnore)
	{
		return Modifier.RuleId.IsValid()
			&& Modifier.ModifierQ12 > 0
			&& Modifier.ModifierQ12 <= FBattleFinalDamageCalculator::MaximumFinalModifierQ12
			&& (bAllowCriticalIgnore || !Modifier.bIgnoredByCritical);
	}

	bool AreModifiersValid(
		const TArray<FBattleDamageModifier>& Modifiers,
		const bool bAllowCriticalIgnore)
	{
		for (const FBattleDamageModifier& Modifier : Modifiers)
		{
			if (!IsValidModifier(Modifier, bAllowCriticalIgnore))
			{
				return false;
			}
		}
		return true;
	}

	bool TryBuildModifierChain(
		const TArray<FBattleDamageModifier>& Modifiers,
		const EBattleDamageTraceStep TraceStep,
		const bool bCritical,
		const bool bCanIgnoreForCritical,
		FDamageTrace& Trace,
		uint64& OutChain)
	{
		OutChain = FBattleFinalDamageCalculator::Q12Neutral;
		for (const FBattleDamageModifier& Modifier : Modifiers)
		{
			if (bCanIgnoreForCritical && bCritical && Modifier.bIgnoredByCritical)
			{
				AddTrace(
					Trace,
					EBattleDamageTraceStep::FinalModifierIgnored,
					Modifier.ModifierQ12,
					Modifier.RuleId);
				continue;
			}

			uint64 Product = 0;
			uint64 RoundedProduct = 0;
			if (!TryMultiply(OutChain, static_cast<uint64>(Modifier.ModifierQ12), Product)
				|| !TryAdd(Product, ChainRoundOffset, RoundedProduct))
			{
				return false;
			}
			OutChain = RoundedProduct / FBattleFinalDamageCalculator::Q12Neutral;
			if (OutChain > static_cast<uint64>(TNumericLimits<int64>::Max()))
			{
				return false;
			}
			AddTrace(Trace, TraceStep, static_cast<int64>(OutChain), Modifier.RuleId);
		}
		return true;
	}

	bool TryApplyPokeRoundQ12(
		const uint64 Value,
		const uint64 ModifierQ12,
		const uint64 MaximumResult,
		uint64& OutValue)
	{
		OutValue = 0;
		uint64 Product = 0;
		uint64 RoundedProduct = 0;
		if (!TryMultiply(Value, ModifierQ12, Product)
			|| !TryAdd(Product, PokeRoundOffset, RoundedProduct))
		{
			return false;
		}
		OutValue = RoundedProduct / FBattleFinalDamageCalculator::Q12Neutral;
		return OutValue <= MaximumResult;
	}

	bool TryApplyDamagePokeRoundQ12(
		const uint64 Value,
		const uint64 ModifierQ12,
		uint64& OutValue)
	{
		OutValue = 0;
		uint64 Product = 0;
		if (!TryMultiply(Value, ModifierQ12, Product))
		{
			return false;
		}
		const uint64 ReducedProduct = ApplyOF32(Product);
		OutValue = (ReducedProduct + PokeRoundOffset)
			/ FBattleFinalDamageCalculator::Q12Neutral;
		return true;
	}

	bool TryResolveStagedStat(
		const FPokemonBattleStats& PermanentStats,
		const FBattleStatStages& InputStages,
		const EBattleStat Stat,
		const bool bIgnoreNegative,
		const bool bIgnorePositive,
		const EBattleDamageTraceStep InputTraceStep,
		const EBattleDamageTraceStep UsedTraceStep,
		const EBattleDamageTraceStep ValueTraceStep,
		FDamageTrace& Trace,
		int32& OutValue)
	{
		OutValue = 0;
		int32 InputStage = 0;
		if (!InputStages.TryGetStage(Stat, InputStage))
		{
			return false;
		}

		int32 UsedStage = InputStage;
		if ((bIgnoreNegative && UsedStage < 0) || (bIgnorePositive && UsedStage > 0))
		{
			UsedStage = 0;
		}

		AddTrace(Trace, InputTraceStep, InputStage);
		AddTrace(Trace, UsedTraceStep, UsedStage);
		FBattleStatStages UsedStages = InputStages;
		if (UsedStage != InputStage)
		{
			const FBattleStatStageChangeResult Change = UsedStages.ApplyChange(
				Stat,
				UsedStage - InputStage);
			if (Change.Outcome != EBattleStatStageChangeOutcome::Applied
				|| Change.NewStage != UsedStage)
			{
				return false;
			}
		}

		if (!FBattleStatCalculator::TryCalculateEffectiveStat(
			PermanentStats,
			UsedStages,
			Stat,
			OutValue))
		{
			return false;
		}
		AddTrace(Trace, ValueTraceStep, OutValue);
		return true;
	}

	bool TryResolveNoEffect(
		const FBattleFinalDamageInput& Input,
		bool& bOutNoEffect,
		FBattleFinalDamageResult& OutResult,
		EBattleDamageCalculationError& OutError)
	{
		bOutNoEffect = false;
		OutResult = FBattleFinalDamageResult();
		OutError = EBattleDamageCalculationError::None;
		if (!IsKnownTypeEffectiveness(Input.TypeEffectiveness))
		{
			OutError = EBattleDamageCalculationError::InvalidTypeEffectiveness;
			return false;
		}

		if (Input.TypeEffectiveness.IsImmune() && !Input.bBypassTypeImmunity)
		{
			bOutNoEffect = true;
			OutResult.Outcome = EBattleDamageOutcome::NoEffect;
			OutResult.NoEffectReason = EBattleDamageNoEffectReason::TypeImmunity;
			AddTrace(
				OutResult.Trace,
				EBattleDamageTraceStep::NoEffect,
				static_cast<int64>(EBattleDamageNoEffectReason::TypeImmunity));
			return true;
		}

		if (Input.BlockingRuleId.IsValid())
		{
			bOutNoEffect = true;
			OutResult.Outcome = EBattleDamageOutcome::NoEffect;
			OutResult.NoEffectReason = EBattleDamageNoEffectReason::RuleHook;
			OutResult.NoEffectRuleId = Input.BlockingRuleId;
			AddTrace(
				OutResult.Trace,
				EBattleDamageTraceStep::NoEffect,
				static_cast<int64>(EBattleDamageNoEffectReason::RuleHook),
				Input.BlockingRuleId);
		}
		return true;
	}

	bool IsActionRandomContextValid(const FBattleRandomContext& Context)
	{
		return Context.IsValid() && Context.ActionId.IsValid();
	}
}

bool FBattleFinalDamageCalculator::TryResolvePreAccuracyNoEffect(
	const FBattleFinalDamageInput& Input,
	bool& bOutNoEffect,
	FBattleFinalDamageResult& OutResult,
	EBattleDamageCalculationError& OutError)
{
	return BattleFinalDamageCalculatorPrivate::TryResolveNoEffect(
		Input,
		bOutNoEffect,
		OutResult,
		OutError);
}

bool FBattleFinalDamageCalculator::TryCalculateFinalDamage(
	const FBattleFinalDamageInput& Input,
	IBattleRandom& Random,
	FBattleFinalDamageResult& OutResult,
	EBattleDamageCalculationError& OutError)
{
	using namespace BattleFinalDamageCalculatorPrivate;

	OutResult = FBattleFinalDamageResult();
	OutError = EBattleDamageCalculationError::None;
	bool bNoEffect = false;
	if (!TryResolveNoEffect(Input, bNoEffect, OutResult, OutError))
	{
		return false;
	}
	if (bNoEffect)
	{
		return true;
	}

	auto Fail = [&OutResult, &OutError](const EBattleDamageCalculationError Error)
	{
		OutResult = FBattleFinalDamageResult();
		OutError = Error;
		return false;
	};

	if (Input.AttackerLevel < 1
		|| Input.AttackerLevel > 100
		|| Input.MovePower <= 0
		|| !IsKnownDamageCategory(Input.MoveCategory)
		|| !IsKnownWeatherModifier(Input.WeatherModifierQ12)
		|| !IsKnownStabModifier(Input.StabModifierQ12))
	{
		return Fail(EBattleDamageCalculationError::InvalidInput);
	}
	if (!AreModifiersValid(Input.PowerModifiers, false)
		|| !AreModifiersValid(Input.OffensiveStatModifiers, false)
		|| !AreModifiersValid(Input.DirectDefensiveStatModifiers, false)
		|| !AreModifiersValid(Input.DefensiveStatModifiers, false)
		|| !AreModifiersValid(Input.FinalModifiers, true))
	{
		return Fail(EBattleDamageCalculationError::InvalidModifier);
	}

	// Validate the deterministic final hook chain before the random-damage stage.
	FDamageTrace FinalModifierValidationTrace;
	uint64 ValidatedFinalModifierChain = Q12Neutral;
	if (!TryBuildModifierChain(
		Input.FinalModifiers,
		EBattleDamageTraceStep::FinalModifierChain,
		Input.bCritical,
		true,
		FinalModifierValidationTrace,
		ValidatedFinalModifierChain))
	{
		return Fail(EBattleDamageCalculationError::ArithmeticOverflow);
	}

	AddTrace(OutResult.Trace, EBattleDamageTraceStep::InputPower, Input.MovePower);
	uint64 PowerModifierChain = Q12Neutral;
	uint64 EffectivePower = 0;
	if (!TryBuildModifierChain(
		Input.PowerModifiers,
		EBattleDamageTraceStep::PowerModifierChain,
		Input.bCritical,
		false,
		OutResult.Trace,
		PowerModifierChain)
		|| !TryApplyPokeRoundQ12(
			static_cast<uint64>(Input.MovePower),
			PowerModifierChain,
			static_cast<uint64>(TNumericLimits<int32>::Max()),
			EffectivePower)
		|| EffectivePower == 0)
	{
		return Fail(EBattleDamageCalculationError::ArithmeticOverflow);
	}
	AddTrace(OutResult.Trace, EBattleDamageTraceStep::EffectivePower, static_cast<int64>(EffectivePower));

	const EBattleStat OffensiveStat = Input.MoveCategory == EBattleMoveCategory::Physical
		? EBattleStat::Attack
		: EBattleStat::SpecialAttack;
	const EBattleStat DefensiveStat = Input.MoveCategory == EBattleMoveCategory::Physical
		? EBattleStat::Defense
		: EBattleStat::SpecialDefense;
	int32 StagedOffensiveStat = 0;
	int32 StagedDefensiveStat = 0;
	if (!TryResolveStagedStat(
		Input.AttackerStats,
		Input.AttackerStages,
		OffensiveStat,
		Input.bCritical,
		false,
		EBattleDamageTraceStep::OffensiveStageInput,
		EBattleDamageTraceStep::OffensiveStageUsed,
		EBattleDamageTraceStep::StagedOffensiveStat,
		OutResult.Trace,
		StagedOffensiveStat)
		|| !TryResolveStagedStat(
			Input.DefenderStats,
			Input.DefenderStages,
			DefensiveStat,
			false,
			Input.bCritical,
			EBattleDamageTraceStep::DefensiveStageInput,
			EBattleDamageTraceStep::DefensiveStageUsed,
			EBattleDamageTraceStep::StagedDefensiveStat,
			OutResult.Trace,
			StagedDefensiveStat))
	{
		return Fail(EBattleDamageCalculationError::InvalidInput);
	}

	uint64 DirectDefensiveStat = static_cast<uint64>(StagedDefensiveStat);
	for (const FBattleDamageModifier& Modifier : Input.DirectDefensiveStatModifiers)
	{
		uint64 ModifiedDefense = 0;
		if (!TryApplyPokeRoundQ12(
			DirectDefensiveStat,
			static_cast<uint64>(Modifier.ModifierQ12),
			static_cast<uint64>(TNumericLimits<int32>::Max()),
			ModifiedDefense)
			|| ModifiedDefense == 0)
		{
			return Fail(EBattleDamageCalculationError::ArithmeticOverflow);
		}
		DirectDefensiveStat = ModifiedDefense;
		AddTrace(
			OutResult.Trace,
			EBattleDamageTraceStep::DirectDefensiveStat,
			static_cast<int64>(DirectDefensiveStat),
			Modifier.RuleId);
	}

	uint64 OffensiveModifierChain = Q12Neutral;
	uint64 DefensiveModifierChain = Q12Neutral;
	uint64 EffectiveOffensiveStat = 0;
	uint64 EffectiveDefensiveStat = 0;
	if (!TryBuildModifierChain(
		Input.OffensiveStatModifiers,
		EBattleDamageTraceStep::OffensiveModifierChain,
		Input.bCritical,
		false,
		OutResult.Trace,
		OffensiveModifierChain)
		|| !TryBuildModifierChain(
			Input.DefensiveStatModifiers,
			EBattleDamageTraceStep::DefensiveModifierChain,
			Input.bCritical,
			false,
			OutResult.Trace,
			DefensiveModifierChain)
		|| !TryApplyPokeRoundQ12(
			static_cast<uint64>(StagedOffensiveStat),
			OffensiveModifierChain,
			static_cast<uint64>(TNumericLimits<int32>::Max()),
			EffectiveOffensiveStat)
		|| !TryApplyPokeRoundQ12(
			DirectDefensiveStat,
			DefensiveModifierChain,
			static_cast<uint64>(TNumericLimits<int32>::Max()),
			EffectiveDefensiveStat)
		|| EffectiveOffensiveStat == 0
		|| EffectiveDefensiveStat == 0)
	{
		return Fail(EBattleDamageCalculationError::ArithmeticOverflow);
	}
	AddTrace(OutResult.Trace, EBattleDamageTraceStep::EffectiveOffensiveStat, static_cast<int64>(EffectiveOffensiveStat));
	AddTrace(OutResult.Trace, EBattleDamageTraceStep::EffectiveDefensiveStat, static_cast<int64>(EffectiveDefensiveStat));

	FPokemonBattleStats EffectiveAttackerStats = Input.AttackerStats;
	FPokemonBattleStats EffectiveDefenderStats = Input.DefenderStats;
	if (Input.MoveCategory == EBattleMoveCategory::Physical)
	{
		EffectiveAttackerStats.Attack = static_cast<int32>(EffectiveOffensiveStat);
		EffectiveDefenderStats.Defense = static_cast<int32>(EffectiveDefensiveStat);
	}
	else
	{
		EffectiveAttackerStats.SpecialAttack = static_cast<int32>(EffectiveOffensiveStat);
		EffectiveDefenderStats.SpecialDefense = static_cast<int32>(EffectiveDefensiveStat);
	}

	const uint64 LevelTerm = (2ULL * static_cast<uint64>(Input.AttackerLevel)) / 5ULL + 2ULL;
	uint64 PowerTerm = 0;
	uint64 AttackTerm = 0;
	if (!TryMultiply(LevelTerm, EffectivePower, PowerTerm)
		|| !TryMultiply(PowerTerm, EffectiveOffensiveStat, AttackTerm)
		|| PowerTerm > static_cast<uint64>(TNumericLimits<int64>::Max())
		|| AttackTerm > static_cast<uint64>(TNumericLimits<int64>::Max()))
	{
		return Fail(EBattleDamageCalculationError::ArithmeticOverflow);
	}
	const uint64 Quotient = AttackTerm / EffectiveDefensiveStat;
	const uint64 TracedBaseDamage = Quotient / 50ULL + 2ULL;
	if (TracedBaseDamage > static_cast<uint64>(TNumericLimits<int32>::Max()))
	{
		return Fail(EBattleDamageCalculationError::ArithmeticOverflow);
	}
	AddTrace(OutResult.Trace, EBattleDamageTraceStep::LevelTerm, static_cast<int64>(LevelTerm));
	AddTrace(OutResult.Trace, EBattleDamageTraceStep::PowerTerm, static_cast<int64>(PowerTerm));
	AddTrace(OutResult.Trace, EBattleDamageTraceStep::AttackTerm, static_cast<int64>(AttackTerm));
	AddTrace(OutResult.Trace, EBattleDamageTraceStep::Quotient, static_cast<int64>(Quotient));

	int32 BaseDamage = 0;
	if (!FBattleDamageCalculator::TryCalculateDamage(
		Input.AttackerLevel,
		EffectiveAttackerStats,
		EffectiveDefenderStats,
		Input.MoveCategory,
		static_cast<int32>(EffectivePower),
		BaseDamage))
	{
		return Fail(EBattleDamageCalculationError::BaseCalculationFailed);
	}
	if (BaseDamage != static_cast<int32>(TracedBaseDamage))
	{
		return Fail(EBattleDamageCalculationError::BaseCalculationFailed);
	}
	AddTrace(OutResult.Trace, EBattleDamageTraceStep::BaseDamage, BaseDamage);

	uint64 Damage = static_cast<uint64>(BaseDamage);
	if (Input.bSpreadAcrossMultipleTargets)
	{
		if (!TryApplyDamagePokeRoundQ12(Damage, 3072, Damage))
		{
			return Fail(EBattleDamageCalculationError::ArithmeticOverflow);
		}
	}
	AddTrace(OutResult.Trace, EBattleDamageTraceStep::SpreadDamage, static_cast<int64>(Damage));

	if (Input.WeatherModifierQ12 != Q12Neutral
		&& !TryApplyDamagePokeRoundQ12(
			Damage,
			static_cast<uint64>(Input.WeatherModifierQ12),
			Damage))
	{
		return Fail(EBattleDamageCalculationError::ArithmeticOverflow);
	}
	AddTrace(OutResult.Trace, EBattleDamageTraceStep::WeatherDamage, static_cast<int64>(Damage));

	if (Input.bCritical)
	{
		uint64 CriticalProduct = 0;
		if (!TryMultiply(Damage, 3ULL, CriticalProduct))
		{
			return Fail(EBattleDamageCalculationError::ArithmeticOverflow);
		}
		Damage = ApplyOF32(CriticalProduct / 2ULL);
	}
	AddTrace(OutResult.Trace, EBattleDamageTraceStep::CriticalDamage, static_cast<int64>(Damage));

	if (!IsActionRandomContextValid(Input.RandomContext))
	{
		return Fail(EBattleDamageCalculationError::InvalidRandomContext);
	}
	if (!Random.TryDrawUniform(0, 15, Input.RandomContext, OutResult.RandomDraw))
	{
		return Fail(EBattleDamageCalculationError::RandomFailure);
	}
	OutResult.bRandomDrawConsumed = true;
	const uint64 RandomFactor = 100ULL - static_cast<uint64>(OutResult.RandomDraw.Result);
	uint64 RandomProduct = 0;
	if (!TryMultiply(Damage, RandomFactor, RandomProduct))
	{
		return Fail(EBattleDamageCalculationError::ArithmeticOverflow);
	}
	Damage = static_cast<uint64>(ApplyOF32(RandomProduct)) / 100ULL;
	AddTrace(OutResult.Trace, EBattleDamageTraceStep::RandomRoll, OutResult.RandomDraw.Result);
	AddTrace(OutResult.Trace, EBattleDamageTraceStep::RandomFactor, static_cast<int64>(RandomFactor));
	AddTrace(OutResult.Trace, EBattleDamageTraceStep::RandomDamage, static_cast<int64>(Damage));

	if (Input.StabModifierQ12 != Q12Neutral
		&& !TryApplyDamagePokeRoundQ12(
			Damage,
			static_cast<uint64>(Input.StabModifierQ12),
			Damage))
	{
		return Fail(EBattleDamageCalculationError::ArithmeticOverflow);
	}
	AddTrace(OutResult.Trace, EBattleDamageTraceStep::StabDamage, static_cast<int64>(Damage));

	const int32 EffectivenessNumerator = Input.bBypassTypeImmunity
		&& Input.TypeEffectiveness.IsImmune()
		? 1
		: Input.TypeEffectiveness.Numerator;
	const int32 EffectivenessDenominator = Input.bBypassTypeImmunity
		&& Input.TypeEffectiveness.IsImmune()
		? 1
		: Input.TypeEffectiveness.Denominator;
	AddTrace(OutResult.Trace, EBattleDamageTraceStep::TypeEffectivenessNumerator, EffectivenessNumerator);
	AddTrace(OutResult.Trace, EBattleDamageTraceStep::TypeEffectivenessDenominator, EffectivenessDenominator);
	uint64 TypeProduct = 0;
	if (!TryMultiply(
		static_cast<uint64>(ApplyOF32(Damage)),
		static_cast<uint64>(EffectivenessNumerator),
		TypeProduct))
	{
		return Fail(EBattleDamageCalculationError::ArithmeticOverflow);
	}
	Damage = TypeProduct / static_cast<uint64>(EffectivenessDenominator);
	if (Damage > static_cast<uint64>(TNumericLimits<int64>::Max()))
	{
		return Fail(EBattleDamageCalculationError::ArithmeticOverflow);
	}
	AddTrace(OutResult.Trace, EBattleDamageTraceStep::TypeDamage, static_cast<int64>(Damage));

	if (Input.bAttackerBurned
		&& !Input.bBypassBurnPenalty
		&& Input.MoveCategory == EBattleMoveCategory::Physical)
	{
		Damage /= 2ULL;
	}
	AddTrace(OutResult.Trace, EBattleDamageTraceStep::BurnDamage, static_cast<int64>(Damage));

	uint64 FinalModifierChain = Q12Neutral;
	if (!TryBuildModifierChain(
		Input.FinalModifiers,
		EBattleDamageTraceStep::FinalModifierChain,
		Input.bCritical,
		true,
		OutResult.Trace,
		FinalModifierChain))
	{
		return Fail(EBattleDamageCalculationError::ArithmeticOverflow);
	}
	FinalModifierChain = FMath::Clamp<uint64>(
		FinalModifierChain,
		MinimumFinalModifierQ12,
		MaximumFinalModifierQ12);
	AddTrace(
		OutResult.Trace,
		EBattleDamageTraceStep::FinalModifierClamped,
		static_cast<int64>(FinalModifierChain));

	uint64 FinalProduct = 0;
	if (!TryMultiply(Damage, FinalModifierChain, FinalProduct))
	{
		return Fail(EBattleDamageCalculationError::ArithmeticOverflow);
	}
	const uint64 ReducedFinalProduct = ApplyOF32(FinalProduct);
	const uint64 RoundedFinalDamage = (ReducedFinalProduct + PokeRoundOffset) / Q12Neutral;
	const uint64 MinimumAppliedDamage = FMath::Max<uint64>(1ULL, RoundedFinalDamage);
	const uint16 FinalDamage = ApplyOF16(MinimumAppliedDamage);

	OutResult.Outcome = EBattleDamageOutcome::Damage;
	OutResult.Damage = static_cast<int32>(FinalDamage);
	AddTrace(OutResult.Trace, EBattleDamageTraceStep::FinalDamage, OutResult.Damage);
	return true;
}
