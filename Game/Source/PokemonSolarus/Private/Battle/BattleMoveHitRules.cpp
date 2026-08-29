#include "Battle/BattleMoveHitRules.h"

namespace BattleMoveHitRulesPrivate
{
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

	bool AreTypesValid(
		const EPokemonType PrimaryType,
		const EPokemonType SecondaryType)
	{
		return FBattleTypeChart::IsKnownType(PrimaryType)
			&& (SecondaryType == EPokemonType::Invalid
				|| (FBattleTypeChart::IsKnownType(SecondaryType)
					&& SecondaryType != PrimaryType));
	}
}

bool FBattleMoveHitRules::TryValidateMoveDefinition(
	const FBattleMoveDefinition& Move,
	EBattleMoveHitRuleValidationError& OutError)
{
	OutError = EBattleMoveHitRuleValidationError::None;
	const bool bRespectsTypeImmunity = EnumHasAllFlags(
		Move.Flags,
		EBattleMoveFlags::RespectsTypeImmunity);
	const bool bPowder = EnumHasAllFlags(
		Move.Flags,
		EBattleMoveFlags::Powder);
	const bool bPoisonUserBypass = EnumHasAllFlags(
		Move.Flags,
		EBattleMoveFlags::PoisonTypeUserBypassesSemiInvulnerabilityAndAccuracy);
	if (!bRespectsTypeImmunity && !bPowder && !bPoisonUserBypass)
	{
		return true;
	}
	if (!BattleMoveHitRulesPrivate::IsBattlerTargetClass(Move.TargetClass))
	{
		OutError = EBattleMoveHitRuleValidationError::RequiresBattlerTarget;
		return false;
	}
	if (bRespectsTypeImmunity && Move.Category != EBattleMoveCategory::Status)
	{
		OutError =
			EBattleMoveHitRuleValidationError::StatusTypeImmunityRequiresStatusMove;
		return false;
	}
	if (bPoisonUserBypass
		&& (Move.Category != EBattleMoveCategory::Status
			|| Move.Type != EPokemonType::Poison))
	{
		OutError = EBattleMoveHitRuleValidationError::PoisonUserBypassRequiresPoisonStatusMove;
		return false;
	}
	if (bPoisonUserBypass
		&& (Move.bAlwaysHits || Move.Accuracy < 1 || Move.Accuracy > 100))
	{
		OutError = EBattleMoveHitRuleValidationError::PoisonUserBypassRequiresNumericAccuracy;
		return false;
	}
	return true;
}

bool FBattleMoveHitRules::TryResolveUserHitQualifiers(
	const FBattleMoveDefinition& Move,
	const EPokemonType UserPrimaryType,
	const EPokemonType UserSecondaryType,
	FBattleMoveUserHitQualifiers& OutQualifiers)
{
	OutQualifiers = FBattleMoveUserHitQualifiers();
	EBattleMoveHitRuleValidationError ValidationError =
		EBattleMoveHitRuleValidationError::None;
	if (!TryValidateMoveDefinition(Move, ValidationError)
		|| !BattleMoveHitRulesPrivate::AreTypesValid(
			UserPrimaryType,
			UserSecondaryType))
	{
		return false;
	}
	if (!EnumHasAllFlags(
			Move.Flags,
			EBattleMoveFlags::PoisonTypeUserBypassesSemiInvulnerabilityAndAccuracy))
	{
		return true;
	}
	const bool bPoisonUser = UserPrimaryType == EPokemonType::Poison
		|| UserSecondaryType == EPokemonType::Poison;
	OutQualifiers.bBypassSemiInvulnerability = bPoisonUser;
	OutQualifiers.bBypassAccuracy = bPoisonUser;
	return true;
}

bool FBattleMoveHitRules::TryResolveMoveImmunity(
	const FBattleMoveDefinition& Move,
	const EPokemonType TargetPrimaryType,
	const EPokemonType TargetSecondaryType,
	const FBattleTypeChart& TypeChart,
	FBattleMoveHitImmunityResult& OutResult)
{
	OutResult = FBattleMoveHitImmunityResult();
	EBattleMoveHitRuleValidationError ValidationError =
		EBattleMoveHitRuleValidationError::None;
	const bool bRespectsTypeImmunity = EnumHasAllFlags(
		Move.Flags,
		EBattleMoveFlags::RespectsTypeImmunity);
	if (!TryValidateMoveDefinition(Move, ValidationError)
		|| (bRespectsTypeImmunity && !TypeChart.IsValid())
		|| !BattleMoveHitRulesPrivate::AreTypesValid(
			TargetPrimaryType,
			TargetSecondaryType))
	{
		return false;
	}

	if (bRespectsTypeImmunity)
	{
		FBattleTypeEffectiveness Effectiveness;
		const bool bResolved = TargetSecondaryType == EPokemonType::Invalid
			? TypeChart.TryGetEffectiveness(
				Move.Type,
				TargetPrimaryType,
				Effectiveness)
			: TypeChart.TryGetDualEffectiveness(
				Move.Type,
				TargetPrimaryType,
				TargetSecondaryType,
				Effectiveness);
		if (!bResolved)
		{
			return false;
		}
		if (Effectiveness.IsImmune())
		{
			OutResult.Reason = EBattleMoveHitImmunityReason::TypeChart;
			return true;
		}
	}

	if (EnumHasAllFlags(Move.Flags, EBattleMoveFlags::Powder)
		&& (TargetPrimaryType == EPokemonType::Grass
			|| TargetSecondaryType == EPokemonType::Grass))
	{
		OutResult.Reason = EBattleMoveHitImmunityReason::Powder;
		return true;
	}

	OutResult.Reason = EBattleMoveHitImmunityReason::None;
	return true;
}
