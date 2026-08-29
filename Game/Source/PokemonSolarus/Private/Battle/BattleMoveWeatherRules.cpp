#include "Battle/BattleMoveWeatherRules.h"

#include "Battle/BattleVolatile.h"

namespace BattleMoveWeatherRulesPrivate
{
	constexpr int32 NeutralModifierQ12 = 4096;
	constexpr int32 HalfPowerModifierQ12 = 2048;

	bool IsDamagingMove(const FBattleMoveDefinition& Move)
	{
		return Move.Category == EBattleMoveCategory::Physical
			|| Move.Category == EBattleMoveCategory::Special;
	}

	bool IsBattlerTargetClass(const EBattleTargetClass TargetClass)
	{
		switch (TargetClass)
		{
		case EBattleTargetClass::Self:
		case EBattleTargetClass::SelectedAlly:
		case EBattleTargetClass::SelectedOpponent:
		case EBattleTargetClass::AnySelectedBattler:
		case EBattleTargetClass::RandomLegalOpponent:
		case EBattleTargetClass::FixedSpreadSet:
		case EBattleTargetClass::SelectedOtherBattler:
		case EBattleTargetClass::FixedOpponentSpreadSet:
			return true;
		default:
			return false;
		}
	}

	bool HasOneCanonicalPrimaryChargeBeforeDamage(const FBattleMoveDefinition& Move)
	{
		const FBattleMoveEffectDescriptor* Charge = nullptr;
		const FBattleMoveEffectDescriptor* Damage = nullptr;
		int32 ChargeCount = 0;
		int32 DamageCount = 0;
		for (const FBattleMoveEffectDescriptor& Effect : Move.Effects)
		{
			if (Effect.Kind == EBattleMoveEffectKind::Charge)
			{
				++ChargeCount;
				Charge = &Effect;
			}
			else if (Effect.Kind == EBattleMoveEffectKind::Damage)
			{
				++DamageCount;
				Damage = &Effect;
			}
		}

		return ChargeCount == 1
			&& DamageCount == 1
			&& Charge != nullptr
			&& Damage != nullptr
			&& Charge->Order >= 0
			&& Charge->Order < Damage->Order
			&& Charge->Target == EBattleEffectTarget::User
			&& Charge->ChanceNumerator == 1
			&& Charge->ChanceDenominator == 1
			&& Charge->ConditionId == FBattleVolatileRules::GetChargingId();
	}

	FDefinitionId MakeHalfPowerRuleId()
	{
		FDefinitionId Result;
		const bool bCreated = FDefinitionId::TryCreate(
			FName(TEXT("Rule.C10WeatherMoveRules.HalfPower")),
			Result);
		check(bCreated);
		return Result;
	}
}

bool FBattleMoveWeatherRules::TryValidateMoveDefinition(
	const FBattleMoveDefinition& Move,
	EBattleMoveWeatherRuleValidationError& OutError)
{
	OutError = EBattleMoveWeatherRuleValidationError::None;
	const bool bSkipsChargeInSun = EnumHasAllFlags(
		Move.Flags,
		EBattleMoveFlags::SkipsChargeInSun);
	const bool bHalvesPower = EnumHasAllFlags(
		Move.Flags,
		EBattleMoveFlags::HalvesPowerInRainSandstormOrSnow);
	const bool bWeatherAccuracy = EnumHasAllFlags(
		Move.Flags,
		EBattleMoveFlags::RainAlwaysHitsSunAccuracyFifty);
	if (!bSkipsChargeInSun && !bHalvesPower && !bWeatherAccuracy)
	{
		return true;
	}

	const bool bDamaging = BattleMoveWeatherRulesPrivate::IsDamagingMove(Move);
	if (bSkipsChargeInSun && !bDamaging)
	{
		OutError = EBattleMoveWeatherRuleValidationError::ChargeSkipRequiresDamagingMove;
		return false;
	}
	if (bSkipsChargeInSun
		&& !BattleMoveWeatherRulesPrivate::HasOneCanonicalPrimaryChargeBeforeDamage(Move))
	{
		OutError = EBattleMoveWeatherRuleValidationError::ChargeSkipRequiresPrimaryChargeBeforeDamage;
		return false;
	}
	if (bHalvesPower && !bDamaging)
	{
		OutError = EBattleMoveWeatherRuleValidationError::PowerModifierRequiresDamagingMove;
		return false;
	}
	if (bWeatherAccuracy && !bDamaging)
	{
		OutError = EBattleMoveWeatherRuleValidationError::WeatherAccuracyRequiresDamagingMove;
		return false;
	}
	if (bWeatherAccuracy
		&& !BattleMoveWeatherRulesPrivate::IsBattlerTargetClass(Move.TargetClass))
	{
		OutError = EBattleMoveWeatherRuleValidationError::WeatherAccuracyRequiresBattlerTarget;
		return false;
	}
	if (bWeatherAccuracy && Move.Accuracy != 70)
	{
		OutError = EBattleMoveWeatherRuleValidationError::WeatherAccuracyRequiresSeventyAccuracy;
		return false;
	}
	if (bWeatherAccuracy && Move.bAlwaysHits)
	{
		OutError = EBattleMoveWeatherRuleValidationError::WeatherAccuracyRequiresOrdinaryAccuracy;
		return false;
	}
	if (bWeatherAccuracy
		&& EnumHasAllFlags(Move.Flags, EBattleMoveFlags::UsesPerHitAccuracy))
	{
		OutError = EBattleMoveWeatherRuleValidationError::WeatherAccuracyDisallowsPerHitAccuracy;
		return false;
	}

	return true;
}

bool FBattleMoveWeatherRules::TryResolveChargeSkip(
	const FBattleMoveDefinition& Move,
	const EBattleFieldSideConditionKind WeatherKind,
	FBattleMoveWeatherChargeSkipResult& OutResult)
{
	OutResult = FBattleMoveWeatherChargeSkipResult();
	EBattleMoveWeatherRuleValidationError ValidationError =
		EBattleMoveWeatherRuleValidationError::None;
	if (!TryValidateMoveDefinition(Move, ValidationError))
	{
		return false;
	}

	OutResult.bShouldSkipCharge = EnumHasAllFlags(
		Move.Flags,
		EBattleMoveFlags::SkipsChargeInSun)
		&& WeatherKind == EBattleFieldSideConditionKind::Sun;
	return true;
}

bool FBattleMoveWeatherRules::TryResolveAccuracy(
	const FBattleMoveDefinition& Move,
	const EBattleFieldSideConditionKind WeatherKind,
	FBattleMoveWeatherAccuracyResult& OutResult)
{
	OutResult = FBattleMoveWeatherAccuracyResult();
	EBattleMoveWeatherRuleValidationError ValidationError =
		EBattleMoveWeatherRuleValidationError::None;
	if (!TryValidateMoveDefinition(Move, ValidationError))
	{
		return false;
	}

	OutResult.bAlwaysHits = Move.bAlwaysHits;
	OutResult.BaseAccuracy = Move.Accuracy;
	if (!EnumHasAllFlags(
		Move.Flags,
		EBattleMoveFlags::RainAlwaysHitsSunAccuracyFifty))
	{
		return true;
	}

	if (WeatherKind == EBattleFieldSideConditionKind::Rain)
	{
		OutResult.bAlwaysHits = true;
		OutResult.BaseAccuracy = 0;
	}
	else if (WeatherKind == EBattleFieldSideConditionKind::Sun)
	{
		OutResult.bAlwaysHits = false;
		OutResult.BaseAccuracy = 50;
	}
	return true;
}

bool FBattleMoveWeatherRules::TryResolvePowerModifier(
	const FBattleMoveDefinition& Move,
	const EBattleFieldSideConditionKind WeatherKind,
	FBattleMoveWeatherPowerModifierResult& OutResult)
{
	OutResult = FBattleMoveWeatherPowerModifierResult();
	OutResult.ModifierQ12 = BattleMoveWeatherRulesPrivate::NeutralModifierQ12;
	EBattleMoveWeatherRuleValidationError ValidationError =
		EBattleMoveWeatherRuleValidationError::None;
	if (!TryValidateMoveDefinition(Move, ValidationError))
	{
		return false;
	}

	const bool bSupportedWeather = WeatherKind == EBattleFieldSideConditionKind::Rain
		|| WeatherKind == EBattleFieldSideConditionKind::Sandstorm
		|| WeatherKind == EBattleFieldSideConditionKind::Snow;
	if (!EnumHasAllFlags(
			Move.Flags,
			EBattleMoveFlags::HalvesPowerInRainSandstormOrSnow)
		|| !bSupportedWeather)
	{
		return true;
	}

	OutResult.bApplies = true;
	OutResult.RuleId = BattleMoveWeatherRulesPrivate::MakeHalfPowerRuleId();
	OutResult.ModifierQ12 = BattleMoveWeatherRulesPrivate::HalfPowerModifierQ12;
	return true;
}
