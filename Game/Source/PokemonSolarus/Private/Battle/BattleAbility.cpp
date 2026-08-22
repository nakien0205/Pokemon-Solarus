#include "Battle/BattleAbility.h"

#include "Battle/BattleFieldSideConditions.h"
#include "Battle/BattleTypeChart.h"

namespace BattleAbilityPrivate
{
	template <typename IdType>
	IdType MakeNamedId(const TCHAR* Name)
	{
		IdType Result;
		const bool bCreated = IdType::TryCreate(FName(Name), Result);
		check(bCreated);
		return Result;
	}

	const TCHAR* GetKindName(const EBattleAbilityKind Kind)
	{
		switch (Kind)
		{
		case EBattleAbilityKind::Blaze: return TEXT("Blaze");
		case EBattleAbilityKind::Overgrow: return TEXT("Overgrow");
		case EBattleAbilityKind::Intimidate: return TEXT("Intimidate");
		case EBattleAbilityKind::Levitate: return TEXT("Levitate");
		case EBattleAbilityKind::Drizzle: return TEXT("Drizzle");
		case EBattleAbilityKind::SpeedBoost: return TEXT("SpeedBoost");
		case EBattleAbilityKind::MagicGuard: return TEXT("MagicGuard");
		case EBattleAbilityKind::MoldBreaker: return TEXT("MoldBreaker");
		default: return TEXT("Invalid");
		}
	}

	FBattleAbilityItemHookDefinition MakeHookDefinition(
		const FAbilityId& AbilityId,
		const EBattleAbilityKind Kind,
		const TCHAR* HookName,
		const EBattleAbilityItemHookPoint HookPoint,
		const EBattleAbilityItemEffectKind EffectKind,
		const EBattleTriggerPhase Phase,
		const EBattleAbilityItemRevealPolicy RevealPolicy,
		const bool bBreakable,
		const int32 Order = 0,
		const int32 Suborder = 0,
		const bool bRepeatable = false)
	{
		const TCHAR* KindName = GetKindName(Kind);
		FBattleAbilityItemHookDefinition Definition;
		Definition.HookId = MakeNamedId<FDefinitionId>(
			*FString::Printf(TEXT("Hook.Ability.%s.%s"), KindName, HookName));
		Definition.HookPoint = HookPoint;
		Definition.EffectKind = EffectKind;
		Definition.TriggerRule.Phase = Phase;
		Definition.TriggerRule.EffectId = MakeNamedId<FBattleTriggerEffectId>(
			*FString::Printf(TEXT("HookEffect.Ability.%s.%s"), KindName, HookName));
		Definition.TriggerRule.PayloadId = AbilityId.GetDefinitionId();
		Definition.TriggerRule.Order = Order;
		Definition.TriggerRule.Suborder = Suborder;
		Definition.TriggerRule.bRepeatable = bRepeatable;
		Definition.RevealPolicy = RevealPolicy;
		Definition.bBreakable = bBreakable;
		return Definition;
	}

	bool IsKnownPhase(const EBattleTriggerPhase Phase)
	{
		return static_cast<uint8>(Phase) <= static_cast<uint8>(EBattleTriggerPhase::Expiry);
	}

	bool AreSameHook(
		const FBattleAbilityItemHookDefinition& Left,
		const FBattleAbilityItemHookDefinition& Right)
	{
		return Left.HookId == Right.HookId
			&& Left.HookPoint == Right.HookPoint
			&& Left.EffectKind == Right.EffectKind
			&& Left.TriggerRule.Phase == Right.TriggerRule.Phase
			&& Left.TriggerRule.EffectId == Right.TriggerRule.EffectId
			&& Left.TriggerRule.PayloadId == Right.TriggerRule.PayloadId
			&& Left.TriggerRule.Order == Right.TriggerRule.Order
			&& Left.TriggerRule.Priority == Right.TriggerRule.Priority
			&& Left.TriggerRule.Suborder == Right.TriggerRule.Suborder
			&& Left.TriggerRule.bRepeatable == Right.TriggerRule.bRepeatable
			&& Left.TriggerRule.bDecrementDurationBeforeEffect
				== Right.TriggerRule.bDecrementDurationBeforeEffect
			&& Left.RevealPolicy == Right.RevealPolicy
			&& Left.bBreakable == Right.bBreakable;
	}
}

FAbilityId FBattleAbilityRules::GetBlazeId()
{
	return BattleAbilityPrivate::MakeNamedId<FAbilityId>(TEXT("Ability.Blaze"));
}

FAbilityId FBattleAbilityRules::GetOvergrowId()
{
	return BattleAbilityPrivate::MakeNamedId<FAbilityId>(TEXT("Ability.Overgrow"));
}

FAbilityId FBattleAbilityRules::GetIntimidateId()
{
	return BattleAbilityPrivate::MakeNamedId<FAbilityId>(TEXT("Ability.Intimidate"));
}

FAbilityId FBattleAbilityRules::GetLevitateId()
{
	return BattleAbilityPrivate::MakeNamedId<FAbilityId>(TEXT("Ability.Levitate"));
}

FAbilityId FBattleAbilityRules::GetDrizzleId()
{
	return BattleAbilityPrivate::MakeNamedId<FAbilityId>(TEXT("Ability.Drizzle"));
}

FAbilityId FBattleAbilityRules::GetSpeedBoostId()
{
	return BattleAbilityPrivate::MakeNamedId<FAbilityId>(TEXT("Ability.SpeedBoost"));
}

FAbilityId FBattleAbilityRules::GetMagicGuardId()
{
	return BattleAbilityPrivate::MakeNamedId<FAbilityId>(TEXT("Ability.MagicGuard"));
}

FAbilityId FBattleAbilityRules::GetMoldBreakerId()
{
	return BattleAbilityPrivate::MakeNamedId<FAbilityId>(TEXT("Ability.MoldBreaker"));
}

TArray<FAbilityId> FBattleAbilityRules::GetCanonicalIds()
{
	return {
		GetBlazeId(),
		GetOvergrowId(),
		GetIntimidateId(),
		GetLevitateId(),
		GetDrizzleId(),
		GetSpeedBoostId(),
		GetMagicGuardId(),
		GetMoldBreakerId()
	};
}

EBattleAbilityKind FBattleAbilityRules::GetKind(const FAbilityId& AbilityId)
{
	if (!AbilityId.IsValid()) return EBattleAbilityKind::None;
	if (AbilityId == GetBlazeId()) return EBattleAbilityKind::Blaze;
	if (AbilityId == GetOvergrowId()) return EBattleAbilityKind::Overgrow;
	if (AbilityId == GetIntimidateId()) return EBattleAbilityKind::Intimidate;
	if (AbilityId == GetLevitateId()) return EBattleAbilityKind::Levitate;
	if (AbilityId == GetDrizzleId()) return EBattleAbilityKind::Drizzle;
	if (AbilityId == GetSpeedBoostId()) return EBattleAbilityKind::SpeedBoost;
	if (AbilityId == GetMagicGuardId()) return EBattleAbilityKind::MagicGuard;
	if (AbilityId == GetMoldBreakerId()) return EBattleAbilityKind::MoldBreaker;
	return EBattleAbilityKind::Invalid;
}

bool FBattleAbilityRules::IsCanonical(const FAbilityId& AbilityId)
{
	const EBattleAbilityKind Kind = GetKind(AbilityId);
	return Kind != EBattleAbilityKind::None && Kind != EBattleAbilityKind::Invalid;
}

bool FBattleAbilityRules::TryBuildHookDefinitions(
	const FAbilityId& AbilityId,
	TArray<FBattleAbilityItemHookDefinition>& OutDefinitions)
{
	OutDefinitions.Reset();
	const EBattleAbilityKind Kind = GetKind(AbilityId);
	using EHook = EBattleAbilityItemHookPoint;
	using EEffect = EBattleAbilityItemEffectKind;
	using EReveal = EBattleAbilityItemRevealPolicy;

	auto Add = [&OutDefinitions, &AbilityId, Kind](
		const TCHAR* HookName,
		const EHook HookPoint,
		const EEffect EffectKind,
		const EBattleTriggerPhase Phase,
		const EReveal RevealPolicy,
		const bool bBreakable = false,
		const int32 Order = 0,
		const int32 Suborder = 0,
		const bool bRepeatable = false)
	{
		OutDefinitions.Add(BattleAbilityPrivate::MakeHookDefinition(
			AbilityId,
			Kind,
			HookName,
			HookPoint,
			EffectKind,
			Phase,
			RevealPolicy,
			bBreakable,
			Order,
			Suborder,
			bRepeatable));
	};

	switch (Kind)
	{
	case EBattleAbilityKind::Blaze:
		Add(TEXT("OffensiveStat"), EHook::OffensiveStat, EEffect::Modify,
			EBattleTriggerPhase::BeforeDamage, EReveal::OnAppliedEffect,
			false, 0, 0, true);
		break;
	case EBattleAbilityKind::Overgrow:
		Add(TEXT("OffensiveStat"), EHook::OffensiveStat, EEffect::Modify,
			EBattleTriggerPhase::BeforeDamage, EReveal::OnAppliedEffect,
			false, 0, 0, true);
		break;
	case EBattleAbilityKind::Intimidate:
		Add(TEXT("SwitchIn"), EHook::SwitchIn, EEffect::Modify,
			EBattleTriggerPhase::SwitchIn, EReveal::OnPublicAttempt);
		break;
	case EBattleAbilityKind::Levitate:
		Add(TEXT("GroundedSwitchIn"), EHook::SwitchIn, EEffect::Prevent,
			EBattleTriggerPhase::SwitchIn, EReveal::OnAppliedEffect,
			true, 0, 0, true);
		Add(TEXT("TypeImmunity"), EHook::TypeImmunity, EEffect::Prevent,
			EBattleTriggerPhase::BeforeHit, EReveal::OnAppliedEffect,
			true, 0, 0, true);
		Add(TEXT("GroundedEndTurn"), EHook::EndTurn, EEffect::Prevent,
			EBattleTriggerPhase::EndTurn, EReveal::OnAppliedEffect,
			true, 0, 0, true);
		break;
	case EBattleAbilityKind::Drizzle:
		Add(TEXT("CreateRain"), EHook::FieldCreation, EEffect::CreateField,
			EBattleTriggerPhase::SwitchIn, EReveal::OnAppliedEffect);
		break;
	case EBattleAbilityKind::SpeedBoost:
		Add(TEXT("EndTurn"), EHook::EndTurn, EEffect::Modify,
			EBattleTriggerPhase::EndTurn, EReveal::OnAppliedEffect,
			false, GetSpeedBoostResidualOrder(), GetSpeedBoostResidualSuborder());
		break;
	case EBattleAbilityKind::MagicGuard:
		Add(TEXT("SwitchInDamage"), EHook::FinalDamage, EEffect::Prevent,
			EBattleTriggerPhase::SwitchIn, EReveal::OnAppliedEffect,
			false, 0, 0, true);
		Add(TEXT("BeforeActionDamage"), EHook::FinalDamage, EEffect::Prevent,
			EBattleTriggerPhase::BeforeAction, EReveal::OnAppliedEffect,
			false, 0, 0, true);
		Add(TEXT("AfterDamage"), EHook::AfterDamage, EEffect::Prevent,
			EBattleTriggerPhase::AfterDamage, EReveal::OnAppliedEffect,
			false, 0, 0, true);
		Add(TEXT("EndTurnDamage"), EHook::FinalDamage, EEffect::Prevent,
			EBattleTriggerPhase::EndTurn, EReveal::OnAppliedEffect,
			false, 0, 0, true);
		break;
	case EBattleAbilityKind::MoldBreaker:
		Add(TEXT("EntryReveal"), EHook::SwitchIn, EEffect::Reveal,
			EBattleTriggerPhase::SwitchIn, EReveal::OnAppliedEffect);
		Add(TEXT("IgnoreBreakableDefenderHook"), EHook::EffectApplication,
			EEffect::Ignore, EBattleTriggerPhase::BeforeHit,
			EReveal::OnAppliedEffect, false, 0, 0, true);
		break;
	default:
		return false;
	}

	for (const FBattleAbilityItemHookDefinition& Definition : OutDefinitions)
	{
		if (!FBattleAbilityItemHookContracts::IsDefinitionValid(Definition))
		{
			OutDefinitions.Reset();
			return false;
		}
	}
	return true;
}

bool FBattleAbilityRules::TryGetHookDefinition(
	const FAbilityId& AbilityId,
	const FDefinitionId& HookId,
	FBattleAbilityItemHookDefinition& OutDefinition)
{
	OutDefinition = FBattleAbilityItemHookDefinition();
	if (!HookId.IsValid())
	{
		return false;
	}
	TArray<FBattleAbilityItemHookDefinition> Definitions;
	if (!TryBuildHookDefinitions(AbilityId, Definitions))
	{
		return false;
	}
	const FBattleAbilityItemHookDefinition* Match = Definitions.FindByPredicate(
		[&HookId](const FBattleAbilityItemHookDefinition& Definition)
		{
			return Definition.HookId == HookId;
		});
	if (Match == nullptr)
	{
		return false;
	}
	OutDefinition = *Match;
	return true;
}

bool FBattleAbilityRules::TryGetHookDefinitionsForPhase(
	const FAbilityId& AbilityId,
	const EBattleTriggerPhase Phase,
	TArray<FBattleAbilityItemHookDefinition>& OutDefinitions)
{
	OutDefinitions.Reset();
	if (!BattleAbilityPrivate::IsKnownPhase(Phase))
	{
		return false;
	}
	TArray<FBattleAbilityItemHookDefinition> AllDefinitions;
	if (!TryBuildHookDefinitions(AbilityId, AllDefinitions))
	{
		return false;
	}
	for (const FBattleAbilityItemHookDefinition& Definition : AllDefinitions)
	{
		if (Definition.TriggerRule.Phase == Phase)
		{
			OutDefinitions.Add(Definition);
		}
	}
	return !OutDefinitions.IsEmpty();
}

bool FBattleAbilityRules::TryCreateTypedEffectRequest(
	const FBattleTriggerEffectRequest& TriggerRequest,
	FBattleAbilityItemEffectRequest& OutRequest,
	EBattleAbilityItemHookError& OutError)
{
	OutRequest = FBattleAbilityItemEffectRequest();
	if (!TriggerRequest.SourceDefinition.IsValid()
		|| TriggerRequest.SourceDefinition.Kind
			!= EBattleTriggerSourceDefinitionKind::Ability)
	{
		OutError = EBattleAbilityItemHookError::InvalidSourceDefinition;
		return false;
	}

	TArray<FBattleAbilityItemHookDefinition> Definitions;
	if (!TryBuildHookDefinitions(
			TriggerRequest.SourceDefinition.AbilityId,
			Definitions))
	{
		OutError = EBattleAbilityItemHookError::InvalidDefinition;
		return false;
	}
	const FBattleAbilityItemHookDefinition* Match = Definitions.FindByPredicate(
		[&TriggerRequest](const FBattleAbilityItemHookDefinition& Definition)
		{
			return Definition.TriggerRule.Phase == TriggerRequest.Phase
				&& Definition.TriggerRule.EffectId == TriggerRequest.EffectId
				&& Definition.TriggerRule.PayloadId == TriggerRequest.PayloadId;
		});
	if (Match == nullptr)
	{
		OutError = EBattleAbilityItemHookError::MismatchedTriggerRequest;
		return false;
	}
	return FBattleAbilityItemHookContracts::TryCreateTypedEffectRequest(
		*Match,
		TriggerRequest,
		OutRequest,
		OutError);
}

bool FBattleAbilityRules::TryBuildHookRegistrationFacts(
	const FBattleAbilityRegistrationFacts& Facts,
	TArray<FBattleAbilityItemHookRegistrationFacts>& OutHookFacts,
	EBattleAbilityItemHookError& OutError)
{
	OutHookFacts.Reset();
	TArray<FBattleAbilityItemHookDefinition> Definitions;
	if (!TryBuildHookDefinitions(Facts.AbilityId, Definitions))
	{
		OutError = EBattleAbilityItemHookError::InvalidDefinition;
		return false;
	}

	FBattleTriggerSourceDefinition SourceDefinition;
	if (!FBattleTriggerSourceDefinition::TryCreateAbility(
			Facts.AbilityId,
			SourceDefinition))
	{
		OutError = EBattleAbilityItemHookError::InvalidSourceDefinition;
		return false;
	}

	for (const FBattleAbilityItemHookDefinition& Definition : Definitions)
	{
		FBattleAbilityItemHookRegistrationFacts HookFacts;
		HookFacts.Definition = Definition;
		HookFacts.SourceDefinition = SourceDefinition;
		HookFacts.Owner = Facts.Owner;
		HookFacts.Source = Facts.Source;
		HookFacts.Targets = Facts.Targets;
		HookFacts.DurationOwner = Facts.Owner;
		HookFacts.Layers = 1;
		HookFacts.Visibility = FBattleTriggerVisibility::CreatePublic();
		HookFacts.CleanupPolicy = EBattleTriggerCleanupPolicy::OnSwitch
			| EBattleTriggerCleanupPolicy::OnFaint
			| EBattleTriggerCleanupPolicy::OnCapture
			| EBattleTriggerCleanupPolicy::OnBattleEnd
			| EBattleTriggerCleanupPolicy::OnRemoval;
		HookFacts.bSuppressed = Facts.bSuppressed;

		FBattleTriggerRegistrationSpec IgnoredRegistration;
		if (!FBattleAbilityItemHookContracts::TryBuildTriggerRegistration(
				HookFacts,
				IgnoredRegistration,
				OutError))
		{
			OutHookFacts.Reset();
			return false;
		}
		OutHookFacts.Add(MoveTemp(HookFacts));
	}
	OutError = EBattleAbilityItemHookError::None;
	return true;
}

bool FBattleAbilityRules::TryRegisterHooks(
	FBattleTriggerFramework& Framework,
	const FBattleAbilityRegistrationFacts& Facts,
	EBattleAbilityItemHookError& OutError)
{
	TArray<FBattleAbilityItemHookRegistrationFacts> HookFacts;
	if (!TryBuildHookRegistrationFacts(Facts, HookFacts, OutError))
	{
		return false;
	}

	FBattleTriggerFramework Staged = Framework;
	for (const FBattleAbilityItemHookRegistrationFacts& Hook : HookFacts)
	{
		FBattleTriggerRegistrationId IgnoredRegistrationId;
		if (!FBattleAbilityItemHookContracts::TryRegisterHook(
				Staged,
				Hook,
				IgnoredRegistrationId,
				OutError))
		{
			return false;
		}
	}
	Framework = MoveTemp(Staged);
	OutError = EBattleAbilityItemHookError::None;
	return true;
}

bool FBattleAbilityRules::TryEvaluateOffensiveStatModifier(
	const FBattleAbilityOffensiveStatFacts& Facts,
	FBattleAbilityOffensiveStatResult& OutResult)
{
	OutResult = FBattleAbilityOffensiveStatResult();
	const EBattleAbilityKind Kind = GetKind(Facts.AbilityId);
	if ((Kind != EBattleAbilityKind::Blaze && Kind != EBattleAbilityKind::Overgrow)
		|| !FBattleTypeChart::IsKnownType(Facts.MoveType)
		|| Facts.BaseMaximumHP <= 0
		|| Facts.CurrentHP < 0
		|| Facts.CurrentHP > Facts.BaseMaximumHP)
	{
		return false;
	}

	OutResult.bValid = true;
	OutResult.ModifierQ12 = GetNeutralModifierQ12();
	if (Facts.bSuppressed)
	{
		OutResult.Outcome = EBattleAbilityItemActivationOutcome::Suppressed;
		return true;
	}
	const EPokemonType RequiredType = Kind == EBattleAbilityKind::Blaze
		? EPokemonType::Fire
		: EPokemonType::Grass;
	const bool bAtOrBelowThreshold = Facts.CurrentHP > 0
		&& static_cast<int64>(Facts.CurrentHP) * 3
			<= static_cast<int64>(Facts.BaseMaximumHP);
	if (Facts.MoveType != RequiredType || !bAtOrBelowThreshold)
	{
		OutResult.Outcome = EBattleAbilityItemActivationOutcome::Ineligible;
		return true;
	}

	OutResult.bApplies = true;
	OutResult.ModifierQ12 = GetLowHPBoostModifierQ12();
	OutResult.Outcome = EBattleAbilityItemActivationOutcome::Applied;
	return true;
}

bool FBattleAbilityRules::TryEvaluateIntimidateTarget(
	const FBattleIntimidateTargetFacts& Facts,
	FBattleIntimidateTargetResult& OutResult)
{
	OutResult = FBattleIntimidateTargetResult();
	if (Facts.CurrentAttackStage < -6 || Facts.CurrentAttackStage > 6)
	{
		return false;
	}
	OutResult.bValid = true;
	if (!Facts.bAdjacentOpponent || !Facts.bTargetAbleToBattle)
	{
		OutResult.Outcome = EBattleAbilityItemActivationOutcome::Ineligible;
		return true;
	}
	if (Facts.bSuppressed)
	{
		OutResult.Outcome = EBattleAbilityItemActivationOutcome::Suppressed;
		return true;
	}
	if (Facts.bSubstituteActive
		|| Facts.bStatStageDropPrevented
		|| Facts.CurrentAttackStage == -6)
	{
		OutResult.Outcome = EBattleAbilityItemActivationOutcome::AttemptedButPrevented;
		return true;
	}
	OutResult.AttackStageDelta = -1;
	OutResult.Outcome = EBattleAbilityItemActivationOutcome::Applied;
	return true;
}

bool FBattleAbilityRules::IsLevitateAirborne(
	const FAbilityId& AbilityId,
	const bool bSuppressed,
	const bool bIgnoredForMove)
{
	return AbilityId == GetLevitateId() && !bSuppressed && !bIgnoredForMove;
}

bool FBattleAbilityRules::ShouldLevitatePreventMove(
	const FAbilityId& AbilityId,
	const EPokemonType MoveType,
	const bool bSuppressed,
	const bool bIgnoredForMove)
{
	return MoveType == EPokemonType::Ground
		&& IsLevitateAirborne(AbilityId, bSuppressed, bIgnoredForMove);
}

bool FBattleAbilityRules::TryEvaluateDrizzleEntry(
	const FAbilityId& AbilityId,
	const FConditionId& ExistingWeatherId,
	const bool bSuppressed,
	FBattleDrizzleEntryResult& OutResult)
{
	OutResult = FBattleDrizzleEntryResult();
	if (AbilityId != GetDrizzleId())
	{
		return false;
	}

	OutResult.bValid = true;
	OutResult.RainId = FBattleFieldSideConditionRules::GetRainId();
	OutResult.DurationTurns = GetDrizzleDurationTurns();
	if (bSuppressed)
	{
		OutResult.Outcome = EBattleAbilityItemActivationOutcome::Suppressed;
		return true;
	}
	if (ExistingWeatherId == OutResult.RainId)
	{
		OutResult.Outcome = EBattleAbilityItemActivationOutcome::Ineligible;
		return true;
	}
	OutResult.bReplacesExistingWeather = ExistingWeatherId.IsValid();
	OutResult.Outcome = EBattleAbilityItemActivationOutcome::Applied;
	return true;
}

bool FBattleAbilityRules::ShouldApplySpeedBoost(
	const FAbilityId& AbilityId,
	const uint32 ActiveTurns,
	const int32 CurrentSpeedStage,
	const bool bSuppressed)
{
	return AbilityId == GetSpeedBoostId()
		&& !bSuppressed
		&& ActiveTurns >= 1
		&& CurrentSpeedStage >= -6
		&& CurrentSpeedStage < 6;
}

bool FBattleAbilityRules::ShouldMagicGuardPreventDamage(
	const FAbilityId& AbilityId,
	const EBattleHPChangeSourceKind SourceKind,
	const bool bSuppressed)
{
	if (AbilityId != GetMagicGuardId() || bSuppressed)
	{
		return false;
	}
	switch (SourceKind)
	{
	case EBattleHPChangeSourceKind::Condition:
	case EBattleHPChangeSourceKind::Field:
	case EBattleHPChangeSourceKind::Volatile:
	case EBattleHPChangeSourceKind::Ability:
	case EBattleHPChangeSourceKind::Item:
	case EBattleHPChangeSourceKind::OtherIndirect:
		return true;
	case EBattleHPChangeSourceKind::Move:
	case EBattleHPChangeSourceKind::Cost:
	case EBattleHPChangeSourceKind::Invalid:
	default:
		return false;
	}
}

bool FBattleAbilityRules::ShouldMoldBreakerIgnoreDefenderHookForMove(
	const FAbilityId& AttackerAbilityId,
	const bool bAttackerAbilitySuppressed,
	const FAbilityId& DefenderAbilityId,
	const FBattleAbilityItemHookDefinition& DefenderHook)
{
	if (AttackerAbilityId != GetMoldBreakerId()
		|| bAttackerAbilitySuppressed
		|| !DefenderHook.bBreakable)
	{
		return false;
	}

	TArray<FBattleAbilityItemHookDefinition> DefenderHooks;
	if (!TryBuildHookDefinitions(DefenderAbilityId, DefenderHooks))
	{
		return false;
	}
	return DefenderHooks.ContainsByPredicate(
		[&DefenderHook](const FBattleAbilityItemHookDefinition& CanonicalHook)
		{
			return CanonicalHook.bBreakable
				&& BattleAbilityPrivate::AreSameHook(CanonicalHook, DefenderHook);
		});
}
