#include "Battle/BattleVolatile.h"

#include "Battle/BattleTypeChart.h"

namespace BattleVolatilePrivate
{
	template <typename IdType>
	IdType MakeDefinitionId(const TCHAR* Name)
	{
		IdType Result;
		const bool bCreated = IdType::TryCreate(FName(Name), Result);
		check(bCreated);
		return Result;
	}

	bool HasType(
		const EPokemonType Primary,
		const EPokemonType Secondary,
		const EPokemonType Type)
	{
		return Primary == Type || Secondary == Type;
	}

	bool IsValidTypePair(const EPokemonType Primary, const EPokemonType Secondary)
	{
		return FBattleTypeChart::IsKnownType(Primary)
			&& (Secondary == EPokemonType::Invalid
				|| (FBattleTypeChart::IsKnownType(Secondary) && Secondary != Primary));
	}

	bool IsKnownMoveCategory(const EBattleMoveCategory Category)
	{
		return Category == EBattleMoveCategory::Physical
			|| Category == EBattleMoveCategory::Special
			|| Category == EBattleMoveCategory::Status;
	}

	const TCHAR* KindName(const EBattleVolatileKind Kind)
	{
		switch (Kind)
		{
		case EBattleVolatileKind::Confusion: return TEXT("Confusion");
		case EBattleVolatileKind::Flinch: return TEXT("Flinch");
		case EBattleVolatileKind::Protect: return TEXT("Protect");
		case EBattleVolatileKind::LeechSeed: return TEXT("LeechSeed");
		case EBattleVolatileKind::PartialTrap: return TEXT("PartialTrap");
		case EBattleVolatileKind::Trap: return TEXT("Trap");
		case EBattleVolatileKind::Taunt: return TEXT("Taunt");
		case EBattleVolatileKind::Encore: return TEXT("Encore");
		case EBattleVolatileKind::Disable: return TEXT("Disable");
		case EBattleVolatileKind::Substitute: return TEXT("Substitute");
		case EBattleVolatileKind::Charging: return TEXT("Charging");
		case EBattleVolatileKind::Recharge: return TEXT("Recharge");
		case EBattleVolatileKind::FlySemiInvulnerable: return TEXT("FlySemiInvulnerable");
		default: return TEXT("Invalid");
		}
	}

	const TCHAR* PhaseName(const EBattleTriggerPhase Phase)
	{
		switch (Phase)
		{
		case EBattleTriggerPhase::SelectionEligibility: return TEXT("SelectionEligibility");
		case EBattleTriggerPhase::BeforeAction: return TEXT("BeforeAction");
		case EBattleTriggerPhase::BeforeHit: return TEXT("BeforeHit");
		case EBattleTriggerPhase::BeforeDamage: return TEXT("BeforeDamage");
		case EBattleTriggerPhase::EndTurn: return TEXT("EndTurn");
		default: return TEXT("Invalid");
		}
	}

	bool IsSupportedPhase(const EBattleVolatileKind Kind, const EBattleTriggerPhase Phase)
	{
		switch (Kind)
		{
		case EBattleVolatileKind::Confusion:
			return Phase == EBattleTriggerPhase::BeforeAction;
		case EBattleVolatileKind::Flinch:
			return Phase == EBattleTriggerPhase::BeforeAction
				|| Phase == EBattleTriggerPhase::EndTurn;
		case EBattleVolatileKind::Protect:
			return Phase == EBattleTriggerPhase::BeforeHit
				|| Phase == EBattleTriggerPhase::EndTurn;
		case EBattleVolatileKind::LeechSeed:
			return Phase == EBattleTriggerPhase::EndTurn;
		case EBattleVolatileKind::PartialTrap:
			return Phase == EBattleTriggerPhase::SelectionEligibility
				|| Phase == EBattleTriggerPhase::EndTurn;
		case EBattleVolatileKind::Trap:
			return Phase == EBattleTriggerPhase::SelectionEligibility;
		case EBattleVolatileKind::Taunt:
		case EBattleVolatileKind::Encore:
		case EBattleVolatileKind::Disable:
			return Phase == EBattleTriggerPhase::SelectionEligibility
				|| Phase == EBattleTriggerPhase::BeforeAction
				|| Phase == EBattleTriggerPhase::EndTurn;
		case EBattleVolatileKind::Substitute:
			return Phase == EBattleTriggerPhase::BeforeDamage;
		case EBattleVolatileKind::Charging:
		case EBattleVolatileKind::Recharge:
			return Phase == EBattleTriggerPhase::BeforeAction;
		case EBattleVolatileKind::FlySemiInvulnerable:
			return Phase == EBattleTriggerPhase::BeforeHit;
		default:
			return false;
		}
	}

	FBattleTriggerEffectId MakeEffectId(
		const EBattleVolatileKind Kind,
		const EBattleTriggerPhase Phase)
	{
		return MakeDefinitionId<FBattleTriggerEffectId>(
			*FString::Printf(
				TEXT("Trigger.Volatile.%s.%s"),
				KindName(Kind),
				PhaseName(Phase)));
	}

	FBattleRandomContext WithPurpose(
		const FBattleRandomContext& BaseContext,
		const FDefinitionId& Purpose)
	{
		FBattleRandomContext Context = BaseContext;
		Context.RulePurpose = Purpose;
		return Context;
	}

	bool IsValidProtectCounter(const int32 Counter)
	{
		return Counter == 3
			|| Counter == 9
			|| Counter == 27
			|| Counter == 81
			|| Counter == 243
			|| Counter == 729;
	}

	bool IsValidDuration(
		const EBattleVolatileKind Kind,
		const TOptional<int32>& RemainingTurns)
	{
		switch (Kind)
		{
		case EBattleVolatileKind::Confusion:
			return RemainingTurns.IsSet()
				&& RemainingTurns.GetValue() >= 2
				&& RemainingTurns.GetValue() <= 5;
		case EBattleVolatileKind::PartialTrap:
			return RemainingTurns.IsSet()
				&& RemainingTurns.GetValue() >= 5
				&& RemainingTurns.GetValue() <= 6;
		case EBattleVolatileKind::Taunt:
			return RemainingTurns.IsSet()
				&& RemainingTurns.GetValue() >= 3
				&& RemainingTurns.GetValue() <= 4;
		case EBattleVolatileKind::Encore:
			return RemainingTurns.IsSet() && RemainingTurns.GetValue() == 3;
		case EBattleVolatileKind::Disable:
			return RemainingTurns.IsSet() && RemainingTurns.GetValue() == 5;
		default:
			return !RemainingTurns.IsSet();
		}
	}

	FBattleTriggerRegistrationSpec MakeTriggerSpec(
		const FBattleVolatileTriggerRegistrationFacts& Facts,
		const EBattleVolatileKind Kind,
		const EBattleTriggerPhase Phase,
		const int32 Order,
		const int32 Priority)
	{
		FBattleTriggerRegistrationSpec Spec;
		Spec.Rule.Phase = Phase;
		Spec.Rule.EffectId = MakeEffectId(Kind, Phase);
		Spec.Rule.PayloadId = Facts.PayloadId;
		Spec.Rule.Order = Order;
		Spec.Rule.Priority = Priority;
		const bool bSourceCreated = FBattleTriggerSourceDefinition::TryCreateCondition(
			Facts.VolatileId,
			Spec.SourceDefinition);
		check(bSourceCreated);
		Spec.Owner = Facts.Owner;
		Spec.Source = Facts.Source;
		Spec.Targets = Facts.Targets;
		Spec.DurationOwner = Facts.Owner;
		Spec.Layers = Facts.Layers;
		Spec.Visibility = FBattleTriggerVisibility::CreateCoreOnly();
		Spec.bSuppressed = Facts.bSuppressed;
		Spec.CleanupPolicy = EBattleTriggerCleanupPolicy::OnSwitch
			| EBattleTriggerCleanupPolicy::OnFaint
			| EBattleTriggerCleanupPolicy::OnCapture
			| EBattleTriggerCleanupPolicy::OnBattleEnd
			| EBattleTriggerCleanupPolicy::OnRemoval;
		return Spec;
	}
}

FConditionId FBattleVolatileRules::GetConfusionId()
{
	return BattleVolatilePrivate::MakeDefinitionId<FConditionId>(TEXT("Condition.Confusion"));
}

FConditionId FBattleVolatileRules::GetFlinchId()
{
	return BattleVolatilePrivate::MakeDefinitionId<FConditionId>(TEXT("Condition.Flinch"));
}

FConditionId FBattleVolatileRules::GetProtectId()
{
	return BattleVolatilePrivate::MakeDefinitionId<FConditionId>(TEXT("Condition.Protect"));
}

FConditionId FBattleVolatileRules::GetLeechSeedId()
{
	return BattleVolatilePrivate::MakeDefinitionId<FConditionId>(TEXT("Condition.LeechSeed"));
}

FConditionId FBattleVolatileRules::GetPartialTrapId()
{
	return BattleVolatilePrivate::MakeDefinitionId<FConditionId>(TEXT("Condition.PartialTrap"));
}

FConditionId FBattleVolatileRules::GetTrapId()
{
	return BattleVolatilePrivate::MakeDefinitionId<FConditionId>(TEXT("Condition.Trap"));
}

FConditionId FBattleVolatileRules::GetTauntId()
{
	return BattleVolatilePrivate::MakeDefinitionId<FConditionId>(TEXT("Condition.Taunt"));
}

FConditionId FBattleVolatileRules::GetEncoreId()
{
	return BattleVolatilePrivate::MakeDefinitionId<FConditionId>(TEXT("Condition.Encore"));
}

FConditionId FBattleVolatileRules::GetDisableId()
{
	return BattleVolatilePrivate::MakeDefinitionId<FConditionId>(TEXT("Condition.Disable"));
}

FConditionId FBattleVolatileRules::GetSubstituteId()
{
	return BattleVolatilePrivate::MakeDefinitionId<FConditionId>(TEXT("Condition.Substitute"));
}

FConditionId FBattleVolatileRules::GetChargingId()
{
	return BattleVolatilePrivate::MakeDefinitionId<FConditionId>(TEXT("Condition.Charging"));
}

FConditionId FBattleVolatileRules::GetRechargeId()
{
	return BattleVolatilePrivate::MakeDefinitionId<FConditionId>(TEXT("Condition.Recharge"));
}

FConditionId FBattleVolatileRules::GetFlySemiInvulnerableId()
{
	return BattleVolatilePrivate::MakeDefinitionId<FConditionId>(
		TEXT("Condition.FlySemiInvulnerable"));
}

TArray<FConditionId> FBattleVolatileRules::GetCanonicalIds()
{
	return {
		GetConfusionId(),
		GetFlinchId(),
		GetProtectId(),
		GetLeechSeedId(),
		GetPartialTrapId(),
		GetTrapId(),
		GetTauntId(),
		GetEncoreId(),
		GetDisableId(),
		GetSubstituteId(),
		GetChargingId(),
		GetRechargeId(),
		GetFlySemiInvulnerableId()
	};
}

EBattleVolatileKind FBattleVolatileRules::GetKind(const FConditionId& VolatileId)
{
	if (!VolatileId.IsValid()) return EBattleVolatileKind::None;
	if (VolatileId == GetConfusionId()) return EBattleVolatileKind::Confusion;
	if (VolatileId == GetFlinchId()) return EBattleVolatileKind::Flinch;
	if (VolatileId == GetProtectId()) return EBattleVolatileKind::Protect;
	if (VolatileId == GetLeechSeedId()) return EBattleVolatileKind::LeechSeed;
	if (VolatileId == GetPartialTrapId()) return EBattleVolatileKind::PartialTrap;
	if (VolatileId == GetTrapId()) return EBattleVolatileKind::Trap;
	if (VolatileId == GetTauntId()) return EBattleVolatileKind::Taunt;
	if (VolatileId == GetEncoreId()) return EBattleVolatileKind::Encore;
	if (VolatileId == GetDisableId()) return EBattleVolatileKind::Disable;
	if (VolatileId == GetSubstituteId()) return EBattleVolatileKind::Substitute;
	if (VolatileId == GetChargingId()) return EBattleVolatileKind::Charging;
	if (VolatileId == GetRechargeId()) return EBattleVolatileKind::Recharge;
	if (VolatileId == GetFlySemiInvulnerableId())
	{
		return EBattleVolatileKind::FlySemiInvulnerable;
	}
	return EBattleVolatileKind::Invalid;
}

bool FBattleVolatileRules::IsCanonical(const FConditionId& VolatileId)
{
	const EBattleVolatileKind Kind = GetKind(VolatileId);
	return Kind != EBattleVolatileKind::None && Kind != EBattleVolatileKind::Invalid;
}

bool FBattleVolatileRules::TryEvaluateApplication(
	const FBattleVolatileApplicationFacts& Facts,
	FBattleVolatileApplicationResult& OutResult)
{
	OutResult = FBattleVolatileApplicationResult();
	const EBattleVolatileKind Kind = GetKind(Facts.RequestedVolatileId);
	if (Kind == EBattleVolatileKind::None || Kind == EBattleVolatileKind::Invalid)
	{
		return false;
	}

	OutResult.bValid = true;
	OutResult.Kind = Kind;
	OutResult.Outcome = EBattleVolatileApplicationOutcome::CanApply;
	if (Facts.bAlreadyPresent)
	{
		OutResult.Outcome = EBattleVolatileApplicationOutcome::AlreadyPresent;
		return true;
	}

	if (Kind == EBattleVolatileKind::Confusion)
	{
		if (Facts.bTargetGrounded && Facts.bMistyTerrainActive)
		{
			OutResult.Outcome = EBattleVolatileApplicationOutcome::PreventedByTerrain;
		}
		else if (Facts.bSafeguardActive
			&& Facts.bAppliedByOpponent
			&& !Facts.bBypassesSafeguard)
		{
			OutResult.Outcome = EBattleVolatileApplicationOutcome::PreventedBySafeguard;
		}
		return true;
	}

	if (Kind == EBattleVolatileKind::Flinch)
	{
		if (Facts.bTargetAlreadyActed)
		{
			OutResult.Outcome = EBattleVolatileApplicationOutcome::TargetAlreadyActed;
		}
		return true;
	}

	if (Kind == EBattleVolatileKind::LeechSeed || Kind == EBattleVolatileKind::Trap)
	{
		if (!BattleVolatilePrivate::IsValidTypePair(Facts.PrimaryType, Facts.SecondaryType))
		{
			OutResult = FBattleVolatileApplicationResult();
			return false;
		}
		const EPokemonType ImmuneType = Kind == EBattleVolatileKind::LeechSeed
			? EPokemonType::Grass
			: EPokemonType::Ghost;
		if (BattleVolatilePrivate::HasType(
			Facts.PrimaryType,
			Facts.SecondaryType,
			ImmuneType))
		{
			OutResult.Outcome = EBattleVolatileApplicationOutcome::TypeImmune;
		}
		return true;
	}

	if (Kind == EBattleVolatileKind::Encore || Kind == EBattleVolatileKind::Disable)
	{
		if (Facts.LastMoveCurrentPP < 0)
		{
			OutResult = FBattleVolatileApplicationResult();
			return false;
		}
		if (!Facts.LastMoveId.IsValid())
		{
			OutResult.Outcome = EBattleVolatileApplicationOutcome::InvalidLastMove;
		}
		else if (Facts.LastMoveCurrentPP == 0)
		{
			OutResult.Outcome = EBattleVolatileApplicationOutcome::LastMoveHasNoPP;
		}
		else if (Kind == EBattleVolatileKind::Encore && Facts.bLastMoveUnencoreable)
		{
			OutResult.Outcome = EBattleVolatileApplicationOutcome::LastMoveUnencoreable;
		}
		else if (Kind == EBattleVolatileKind::Disable && Facts.bLastMoveIsStruggle)
		{
			OutResult.Outcome = EBattleVolatileApplicationOutcome::LastMoveIsStruggle;
		}
		return true;
	}

	if (Kind == EBattleVolatileKind::Substitute)
	{
		FBattleSubstituteCreationResult Creation;
		if (!TryResolveSubstituteCreation(
			Facts.BaseMaximumHP,
			Facts.CurrentHP,
			Creation))
		{
			OutResult = FBattleVolatileApplicationResult();
			return false;
		}
		if (!Creation.bCanCreate)
		{
			OutResult.Outcome = EBattleVolatileApplicationOutcome::InsufficientHP;
		}
	}
	return true;
}

bool FBattleVolatileRules::TryRollConfusionDuration(
	const FBattleRandomContext& BaseContext,
	IBattleRandom& Random,
	FBattleVolatileDurationResult& OutResult)
{
	OutResult = FBattleVolatileDurationResult();
	const FBattleRandomContext Context = BattleVolatilePrivate::WithPurpose(
		BaseContext,
		GetConfusionDurationPurpose());
	if (!Context.IsValid() || !Random.TryDrawUniform(2, 5, Context, OutResult.Draw))
	{
		OutResult = FBattleVolatileDurationResult();
		return false;
	}
	OutResult.bValid = true;
	OutResult.Turns = static_cast<int32>(OutResult.Draw.Result);
	return true;
}

bool FBattleVolatileRules::TryRollPartialTrapDuration(
	const FBattleRandomContext& BaseContext,
	IBattleRandom& Random,
	FBattleVolatileDurationResult& OutResult)
{
	OutResult = FBattleVolatileDurationResult();
	const FBattleRandomContext Context = BattleVolatilePrivate::WithPurpose(
		BaseContext,
		GetPartialTrapDurationPurpose());
	if (!Context.IsValid() || !Random.TryDrawUniform(5, 6, Context, OutResult.Draw))
	{
		OutResult = FBattleVolatileDurationResult();
		return false;
	}
	OutResult.bValid = true;
	OutResult.Turns = static_cast<int32>(OutResult.Draw.Result);
	return true;
}

bool FBattleVolatileRules::TryResolveConfusionBeforeAction(
	const int32 RemainingTurns,
	const FBattleRandomContext& BaseContext,
	IBattleRandom& Random,
	FBattleVolatileActionResult& OutResult)
{
	OutResult = FBattleVolatileActionResult();
	if (RemainingTurns < 1 || RemainingTurns > 5)
	{
		return false;
	}

	OutResult.bValid = true;
	OutResult.RemainingTurns = RemainingTurns - 1;
	if (OutResult.RemainingTurns.GetValue() == 0)
	{
		OutResult.Outcome = EBattleVolatileActionOutcome::CuredAndAllowed;
		OutResult.bRemoveVolatile = true;
		return true;
	}

	const FBattleRandomContext Context = BattleVolatilePrivate::WithPurpose(
		BaseContext,
		GetConfusionActionGatePurpose());
	if (!Context.IsValid() || !Random.TryDrawUniform(0, 99, Context, OutResult.Draw))
	{
		OutResult = FBattleVolatileActionResult();
		return false;
	}
	OutResult.bDrawConsumed = true;
	OutResult.Outcome = OutResult.Draw.Result < 33
		? EBattleVolatileActionOutcome::ConfusionSelfHit
		: EBattleVolatileActionOutcome::Allowed;
	return true;
}

bool FBattleVolatileRules::TryResolveSimpleBeforeAction(
	const FConditionId& VolatileId,
	FBattleVolatileActionResult& OutResult)
{
	OutResult = FBattleVolatileActionResult();
	const EBattleVolatileKind Kind = GetKind(VolatileId);
	if (Kind != EBattleVolatileKind::Flinch && Kind != EBattleVolatileKind::Recharge)
	{
		return false;
	}
	OutResult.bValid = true;
	OutResult.Outcome = EBattleVolatileActionOutcome::Denied;
	OutResult.bRemoveVolatile = true;
	return true;
}

bool FBattleVolatileRules::TryResolveProtectAttempt(
	const FBattleProtectAttemptFacts& Facts,
	const FBattleRandomContext& BaseContext,
	IBattleRandom& Random,
	FBattleProtectAttemptResult& OutResult)
{
	OutResult = FBattleProtectAttemptResult();
	if (Facts.ChainCounter < 0)
	{
		return false;
	}

	OutResult.bValid = true;
	if (!Facts.bHasQueuedAction)
	{
		return true;
	}
	if (!Facts.bConsecutiveEligibleUse)
	{
		OutResult.bSucceeded = true;
		OutResult.NextChainCounter = GetProtectInitialChainCounter();
		return true;
	}
	if (!BattleVolatilePrivate::IsValidProtectCounter(Facts.ChainCounter))
	{
		OutResult = FBattleProtectAttemptResult();
		return false;
	}

	const FBattleRandomContext Context = BattleVolatilePrivate::WithPurpose(
		BaseContext,
		GetProtectConsecutiveUsePurpose());
	if (!Context.IsValid()
		|| !Random.TryDrawUniform(
			0,
			static_cast<uint32>(Facts.ChainCounter - 1),
			Context,
			OutResult.Draw))
	{
		OutResult = FBattleProtectAttemptResult();
		return false;
	}
	OutResult.bDrawConsumed = true;
	OutResult.bSucceeded = OutResult.Draw.Result == 0;
	OutResult.NextChainCounter = OutResult.bSucceeded
		? FMath::Min(Facts.ChainCounter * 3, GetProtectMaximumChainCounter())
		: GetClearedProtectChainCounter();
	return true;
}

bool FBattleVolatileRules::ShouldProtectBlockEffect(
	const bool bProtectActive,
	const bool bMoveBlockedByProtect,
	const bool bMoveBypassesProtect)
{
	return bProtectActive && bMoveBlockedByProtect && !bMoveBypassesProtect;
}

bool FBattleVolatileRules::TryResolveLeechSeedResidual(
	const FBattleLeechSeedResidualFacts& Facts,
	FBattleLeechSeedResidualResult& OutResult)
{
	OutResult = FBattleLeechSeedResidualResult();
	if (Facts.TargetBaseMaximumHP <= 0
		|| Facts.TargetCurrentHP < 0
		|| Facts.TargetCurrentHP > Facts.TargetBaseMaximumHP
		|| Facts.RecipientMissingHP < 0)
	{
		return false;
	}

	OutResult.bValid = true;
	if (!Facts.bSourceSlotHasLivingRecipient || Facts.TargetCurrentHP == 0)
	{
		return true;
	}
	OutResult.bApplies = true;
	OutResult.RequestedDamage = FMath::Max(1, Facts.TargetBaseMaximumHP / 8);
	OutResult.ActualDamage = FMath::Min(OutResult.RequestedDamage, Facts.TargetCurrentHP);
	OutResult.Heal = FMath::Min(OutResult.ActualDamage, Facts.RecipientMissingHP);
	return true;
}

bool FBattleVolatileRules::TryResolvePartialTrapResidual(
	const FBattlePartialTrapResidualFacts& Facts,
	FBattlePartialTrapResidualResult& OutResult)
{
	OutResult = FBattlePartialTrapResidualResult();
	if (Facts.TargetBaseMaximumHP <= 0
		|| Facts.TargetCurrentHP < 0
		|| Facts.TargetCurrentHP > Facts.TargetBaseMaximumHP)
	{
		return false;
	}

	OutResult.bValid = true;
	if (!Facts.bBindingSourceActiveAndLiving)
	{
		OutResult.bEndsEarly = true;
		return true;
	}
	if (Facts.TargetCurrentHP == 0)
	{
		return true;
	}
	OutResult.bAppliesDamage = true;
	OutResult.RequestedDamage = FMath::Max(1, Facts.TargetBaseMaximumHP / 8);
	OutResult.ActualDamage = FMath::Min(OutResult.RequestedDamage, Facts.TargetCurrentHP);
	return true;
}

bool FBattleVolatileRules::ShouldBlockVoluntarySwitch(
	const FConditionId& VolatileId,
	const EPokemonType PrimaryType,
	const EPokemonType SecondaryType,
	const bool bSourceActiveAndLiving,
	const bool bEligibilityRemains)
{
	if (!BattleVolatilePrivate::IsValidTypePair(PrimaryType, SecondaryType))
	{
		return false;
	}
	const EBattleVolatileKind Kind = GetKind(VolatileId);
	if (Kind == EBattleVolatileKind::PartialTrap)
	{
		return bSourceActiveAndLiving;
	}
	if (Kind == EBattleVolatileKind::Trap)
	{
		return bSourceActiveAndLiving
			&& bEligibilityRemains
			&& !BattleVolatilePrivate::HasType(
				PrimaryType,
				SecondaryType,
				EPokemonType::Ghost);
	}
	return false;
}

int32 FBattleVolatileRules::GetTauntDuration(const bool bTargetAlreadyActed)
{
	return bTargetAlreadyActed ? 4 : 3;
}

bool FBattleVolatileRules::TryResolveMoveGate(
	const FBattleVolatileMoveGateFacts& Facts,
	FBattleVolatileMoveGateResult& OutResult)
{
	OutResult = FBattleVolatileMoveGateResult();
	if (!Facts.SelectedMoveId.IsValid()
		|| !BattleVolatilePrivate::IsKnownMoveCategory(Facts.SelectedMoveCategory)
		|| Facts.EncoreMoveCurrentPP < 0
		|| Facts.DisabledMoveCurrentPP < 0
		|| (Facts.EncoreMoveId.IsSet() && !Facts.EncoreMoveId.GetValue().IsValid())
		|| (Facts.DisabledMoveId.IsSet() && !Facts.DisabledMoveId.GetValue().IsValid()))
	{
		return false;
	}

	OutResult.bValid = true;
	OutResult.Outcome = EBattleVolatileMoveGateOutcome::Allowed;
	OutResult.bEndEncore = Facts.EncoreMoveId.IsSet()
		&& (!Facts.bEncoreMoveStillValid || Facts.EncoreMoveCurrentPP == 0);
	OutResult.bEndDisable = Facts.DisabledMoveId.IsSet()
		&& (!Facts.bDisabledMoveStillValid || Facts.DisabledMoveCurrentPP == 0);

	if (Facts.bSelectedMoveIsStruggle && Facts.bNoUsableOrdinaryMove)
	{
		return true;
	}
	if (Facts.bTauntActive && Facts.SelectedMoveCategory == EBattleMoveCategory::Status)
	{
		OutResult.Outcome = EBattleVolatileMoveGateOutcome::Taunted;
	}
	else if (Facts.EncoreMoveId.IsSet()
		&& !OutResult.bEndEncore
		&& Facts.SelectedMoveId != Facts.EncoreMoveId.GetValue())
	{
		OutResult.Outcome = EBattleVolatileMoveGateOutcome::EncoreLocked;
	}
	else if (Facts.DisabledMoveId.IsSet()
		&& !OutResult.bEndDisable
		&& !Facts.bSelectedMoveIsStruggle
		&& Facts.SelectedMoveId == Facts.DisabledMoveId.GetValue())
	{
		OutResult.Outcome = EBattleVolatileMoveGateOutcome::Disabled;
	}
	return true;
}

bool FBattleVolatileRules::TryResolveSubstituteCreation(
	const int32 BaseMaximumHP,
	const int32 CurrentHP,
	FBattleSubstituteCreationResult& OutResult)
{
	OutResult = FBattleSubstituteCreationResult();
	if (BaseMaximumHP <= 0 || CurrentHP < 0 || CurrentHP > BaseMaximumHP)
	{
		return false;
	}
	OutResult.bValid = true;
	OutResult.HPCost = BaseMaximumHP / 4;
	OutResult.bCanCreate = BaseMaximumHP != 1 && CurrentHP > OutResult.HPCost;
	OutResult.SubstituteHP = OutResult.bCanCreate ? OutResult.HPCost : 0;
	return true;
}

bool FBattleVolatileRules::TryResolveSubstituteDamage(
	const FBattleSubstituteDamageFacts& Facts,
	FBattleSubstituteDamageResult& OutResult)
{
	OutResult = FBattleSubstituteDamageResult();
	if (Facts.SubstituteHP <= 0 || Facts.OwnerCurrentHP < 0 || Facts.IncomingDamage < 0)
	{
		return false;
	}

	OutResult.bValid = true;
	OutResult.RemainingSubstituteHP = Facts.SubstituteHP;
	if (Facts.bBypassesSubstitute)
	{
		OutResult.DamageToOwner = FMath::Min(Facts.IncomingDamage, Facts.OwnerCurrentHP);
		OutResult.ActualDamageForDrainOrRecoil = OutResult.DamageToOwner;
		return true;
	}

	OutResult.DamageToSubstitute = FMath::Min(Facts.IncomingDamage, Facts.SubstituteHP);
	OutResult.RemainingSubstituteHP = Facts.SubstituteHP - OutResult.DamageToSubstitute;
	OutResult.ActualDamageForDrainOrRecoil = OutResult.DamageToSubstitute;
	OutResult.bBrokeSubstitute = Facts.IncomingDamage > 0
		&& OutResult.RemainingSubstituteHP == 0;
	return true;
}

bool FBattleVolatileRules::ShouldSubstituteBlockEffect(
	const bool bSubstituteActive,
	const bool bEffectFromOpponent,
	const bool bBypassesSubstitute)
{
	return bSubstituteActive && bEffectFromOpponent && !bBypassesSubstitute;
}

bool FBattleVolatileRules::TryResolveChargeAction(
	const FBattleChargeActionFacts& Facts,
	FBattleChargeActionResult& OutResult)
{
	OutResult = FBattleChargeActionResult();
	if (Facts.bExplicitlyCancelled)
	{
		if (!Facts.bChargeActive)
		{
			return false;
		}
		OutResult.bValid = true;
		OutResult.Outcome = EBattleChargeActionOutcome::CancelCharge;
		OutResult.bRemoveCharge = true;
		return true;
	}

	if (Facts.bChargeActive)
	{
		if (!Facts.LockedMoveId.IsValid())
		{
			return false;
		}
		OutResult.bValid = true;
		OutResult.Outcome = EBattleChargeActionOutcome::ExecuteChargedMove;
		OutResult.MoveId = Facts.LockedMoveId;
		OutResult.TargetBattlerId = Facts.LockedTargetBattlerId;
		OutResult.bRemoveCharge = true;
		return true;
	}

	if (!Facts.SelectedMoveId.IsValid())
	{
		return false;
	}
	OutResult.bValid = true;
	OutResult.Outcome = EBattleChargeActionOutcome::BeginCharge;
	OutResult.MoveId = Facts.SelectedMoveId;
	OutResult.TargetBattlerId = Facts.SelectedTargetBattlerId;
	OutResult.bPayPP = true;
	OutResult.bAddCharge = true;
	return true;
}

bool FBattleVolatileRules::TryResolveFlyReachability(
	const FBattleFlyReachabilityFacts& Facts,
	FBattleFlyReachabilityResult& OutResult)
{
	OutResult = FBattleFlyReachabilityResult();
	if (Facts.bMoveDoublesPowerAgainstFlyTarget && !Facts.bMoveReachesFlyTarget)
	{
		return false;
	}
	OutResult.bValid = true;
	OutResult.bReachable = !Facts.bTargetFlySemiInvulnerable
		|| Facts.bMoveReachesFlyTarget;
	if (Facts.bTargetFlySemiInvulnerable
		&& Facts.bMoveReachesFlyTarget
		&& Facts.bMoveDoublesPowerAgainstFlyTarget)
	{
		OutResult.PowerMultiplierNumerator = 2;
	}
	return true;
}

bool FBattleVolatileRules::TryGetTriggerEffectId(
	const FConditionId& VolatileId,
	const EBattleTriggerPhase Phase,
	FBattleTriggerEffectId& OutEffectId)
{
	OutEffectId = FBattleTriggerEffectId();
	const EBattleVolatileKind Kind = GetKind(VolatileId);
	if (!BattleVolatilePrivate::IsSupportedPhase(Kind, Phase))
	{
		return false;
	}
	OutEffectId = BattleVolatilePrivate::MakeEffectId(Kind, Phase);
	return true;
}

bool FBattleVolatileRules::TryBuildTriggerRegistrationSpecs(
	const FBattleVolatileTriggerRegistrationFacts& Facts,
	TArray<FBattleTriggerRegistrationSpec>& OutSpecs)
{
	OutSpecs.Reset();
	const EBattleVolatileKind Kind = GetKind(Facts.VolatileId);
	if (!Facts.Owner.IsValid()
		|| !Facts.Source.IsValid()
		|| !Facts.PayloadId.IsValid()
		|| Kind == EBattleVolatileKind::None
		|| Kind == EBattleVolatileKind::Invalid
		|| !BattleVolatilePrivate::IsValidDuration(Kind, Facts.RemainingTurns)
		|| Facts.Layers <= 0
		|| (Kind == EBattleVolatileKind::Protect
			? !BattleVolatilePrivate::IsValidProtectCounter(Facts.Layers)
			: Kind != EBattleVolatileKind::Substitute && Facts.Layers != 1))
	{
		return false;
	}
	for (const FBattleTriggerSubject& Target : Facts.Targets)
	{
		if (!Target.IsValid())
		{
			return false;
		}
	}

	auto Add = [&OutSpecs, &Facts, Kind](
		const EBattleTriggerPhase Phase,
		const int32 Order = 0,
		const int32 Priority = 0) -> FBattleTriggerRegistrationSpec&
	{
		return OutSpecs.Add_GetRef(BattleVolatilePrivate::MakeTriggerSpec(
			Facts,
			Kind,
			Phase,
			Order,
			Priority));
	};
	const int32 DurationOnlyOrder = TNumericLimits<int32>::Max();

	switch (Kind)
	{
	case EBattleVolatileKind::Confusion:
	{
		FBattleTriggerRegistrationSpec& Spec = Add(EBattleTriggerPhase::BeforeAction);
		Spec.RemainingTurns = Facts.RemainingTurns;
		Spec.Rule.bDecrementDurationBeforeEffect = true;
		break;
	}
	case EBattleVolatileKind::Flinch:
		Add(EBattleTriggerPhase::BeforeAction);
		Add(EBattleTriggerPhase::EndTurn, DurationOnlyOrder);
		break;
	case EBattleVolatileKind::Protect:
		Add(EBattleTriggerPhase::BeforeHit);
		Add(EBattleTriggerPhase::EndTurn, DurationOnlyOrder);
		break;
	case EBattleVolatileKind::LeechSeed:
		Add(EBattleTriggerPhase::EndTurn, 8);
		break;
	case EBattleVolatileKind::PartialTrap:
	{
		Add(EBattleTriggerPhase::SelectionEligibility);
		FBattleTriggerRegistrationSpec& Spec = Add(EBattleTriggerPhase::EndTurn, 13);
		Spec.RemainingTurns = Facts.RemainingTurns;
		Spec.Rule.bDecrementDurationBeforeEffect = true;
		break;
	}
	case EBattleVolatileKind::Trap:
		Add(EBattleTriggerPhase::SelectionEligibility);
		break;
	case EBattleVolatileKind::Taunt:
	{
		Add(EBattleTriggerPhase::SelectionEligibility);
		Add(EBattleTriggerPhase::BeforeAction);
		FBattleTriggerRegistrationSpec& Spec = Add(
			EBattleTriggerPhase::EndTurn,
			DurationOnlyOrder);
		Spec.RemainingTurns = Facts.RemainingTurns;
		Spec.Rule.bDecrementDurationBeforeEffect = true;
		break;
	}
	case EBattleVolatileKind::Encore:
	{
		Add(EBattleTriggerPhase::SelectionEligibility);
		Add(EBattleTriggerPhase::BeforeAction);
		FBattleTriggerRegistrationSpec& Spec = Add(
			EBattleTriggerPhase::EndTurn,
			DurationOnlyOrder);
		Spec.RemainingTurns = Facts.RemainingTurns;
		Spec.Rule.bDecrementDurationBeforeEffect = true;
		break;
	}
	case EBattleVolatileKind::Disable:
	{
		Add(EBattleTriggerPhase::SelectionEligibility);
		Add(EBattleTriggerPhase::BeforeAction, 0, 7);
		FBattleTriggerRegistrationSpec& Spec = Add(
			EBattleTriggerPhase::EndTurn,
			DurationOnlyOrder);
		Spec.RemainingTurns = Facts.RemainingTurns;
		Spec.Rule.bDecrementDurationBeforeEffect = true;
		break;
	}
	case EBattleVolatileKind::Substitute:
		Add(EBattleTriggerPhase::BeforeDamage);
		break;
	case EBattleVolatileKind::Charging:
		Add(EBattleTriggerPhase::BeforeAction);
		break;
	case EBattleVolatileKind::Recharge:
		Add(EBattleTriggerPhase::BeforeAction);
		break;
	case EBattleVolatileKind::FlySemiInvulnerable:
		Add(EBattleTriggerPhase::BeforeHit, -1);
		break;
	default:
		return false;
	}
	return true;
}

bool FBattleVolatileRules::TryRegisterTriggers(
	FBattleTriggerFramework& Framework,
	const FBattleVolatileTriggerRegistrationFacts& Facts,
	EBattleTriggerError& OutError)
{
	OutError = EBattleTriggerError::None;
	TArray<FBattleTriggerRegistrationSpec> Specs;
	if (!TryBuildTriggerRegistrationSpecs(Facts, Specs))
	{
		OutError = EBattleTriggerError::InvalidDefinition;
		return false;
	}

	FBattleTriggerFramework Staged = Framework;
	for (const FBattleTriggerRegistrationSpec& Spec : Specs)
	{
		FBattleTriggerRegistrationId RegistrationId;
		if (!Staged.TryRegister(Spec, RegistrationId, OutError))
		{
			return false;
		}
	}
	Framework = MoveTemp(Staged);
	return true;
}

bool FBattleVolatileRules::TryCleanupTriggers(
	FBattleTriggerFramework& Framework,
	const FConditionId& VolatileId,
	const FBattleTriggerSubject& Owner,
	const EBattleTriggerCleanupReason Reason,
	const FBattleTriggerOperationContext& Context,
	EBattleTriggerError& OutError)
{
	OutError = EBattleTriggerError::None;
	if (!IsCanonical(VolatileId) || !Owner.IsValid())
	{
		OutError = EBattleTriggerError::InvalidDefinition;
		return false;
	}
	FBattleTriggerSourceDefinition SourceDefinition;
	if (!FBattleTriggerSourceDefinition::TryCreateCondition(VolatileId, SourceDefinition))
	{
		OutError = EBattleTriggerError::InvalidDefinition;
		return false;
	}
	FBattleTriggerCleanupRequest Request;
	Request.Reason = Reason;
	Request.AffectedOwners.Add(Owner);
	Request.SourceDefinitionFilter = SourceDefinition;
	Request.Context = Context;
	return Framework.TryApplyCleanup(Request, OutError);
}

FDefinitionId FBattleVolatileRules::GetConfusionDurationPurpose()
{
	return BattleVolatilePrivate::MakeDefinitionId<FDefinitionId>(
		TEXT("Rule.Volatile.Confusion.Duration"));
}

FDefinitionId FBattleVolatileRules::GetConfusionActionGatePurpose()
{
	return BattleVolatilePrivate::MakeDefinitionId<FDefinitionId>(
		TEXT("Rule.Volatile.Confusion.ActionGate"));
}

FDefinitionId FBattleVolatileRules::GetConfusionSelfHitDamagePurpose()
{
	return BattleVolatilePrivate::MakeDefinitionId<FDefinitionId>(
		TEXT("Rule.Volatile.Confusion.SelfHitDamage"));
}

FDefinitionId FBattleVolatileRules::GetProtectConsecutiveUsePurpose()
{
	return BattleVolatilePrivate::MakeDefinitionId<FDefinitionId>(
		TEXT("Rule.Volatile.Protect.ConsecutiveUse"));
}

FDefinitionId FBattleVolatileRules::GetPartialTrapDurationPurpose()
{
	return BattleVolatilePrivate::MakeDefinitionId<FDefinitionId>(
		TEXT("Rule.Volatile.PartialTrap.Duration"));
}
