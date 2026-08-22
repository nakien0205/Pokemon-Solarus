#include "Battle/BattleMajorStatus.h"

namespace BattleMajorStatusPrivate
{
	template <typename IdType>
	IdType MakeDefinitionId(const TCHAR* Name)
	{
		IdType Result;
		const bool bCreated = IdType::TryCreate(FName(Name), Result);
		check(bCreated);
		return Result;
	}

	FDefinitionId MakePayloadId(const FConditionId& StatusId)
	{
		return StatusId.GetDefinitionId();
	}

	bool HasType(
		const FBattleMajorStatusApplicationFacts& Facts,
		const EPokemonType Type)
	{
		return Facts.PrimaryType == Type || Facts.SecondaryType == Type;
	}

	bool IsValidTypePair(const EPokemonType Primary, const EPokemonType Secondary)
	{
		return FBattleTypeChart::IsKnownType(Primary)
			&& (Secondary == EPokemonType::Invalid
				|| (FBattleTypeChart::IsKnownType(Secondary) && Secondary != Primary));
	}

	FBattleTriggerEffectId MakeEffectId(
		const EBattleMajorStatusKind Kind,
		const EBattleTriggerPhase Phase)
	{
		const TCHAR* KindName = TEXT("Invalid");
		switch (Kind)
		{
		case EBattleMajorStatusKind::Burn: KindName = TEXT("Burn"); break;
		case EBattleMajorStatusKind::Paralysis: KindName = TEXT("Paralysis"); break;
		case EBattleMajorStatusKind::Sleep: KindName = TEXT("Sleep"); break;
		case EBattleMajorStatusKind::Freeze: KindName = TEXT("Freeze"); break;
		case EBattleMajorStatusKind::Poison: KindName = TEXT("Poison"); break;
		case EBattleMajorStatusKind::Toxic: KindName = TEXT("Toxic"); break;
		default: break;
		}

		const TCHAR* PhaseName = TEXT("Invalid");
		switch (Phase)
		{
		case EBattleTriggerPhase::ActionOrderCalculation:
			PhaseName = TEXT("ActionOrderCalculation");
			break;
		case EBattleTriggerPhase::BeforeAction: PhaseName = TEXT("BeforeAction"); break;
		case EBattleTriggerPhase::BeforeDamage: PhaseName = TEXT("BeforeDamage"); break;
		case EBattleTriggerPhase::SwitchOut: PhaseName = TEXT("SwitchOut"); break;
		case EBattleTriggerPhase::EndTurn: PhaseName = TEXT("EndTurn"); break;
		default: break;
		}
		return MakeDefinitionId<FBattleTriggerEffectId>(
			*FString::Printf(TEXT("Trigger.MajorStatus.%s.%s"), KindName, PhaseName));
	}

	FBattleTriggerRegistrationSpec MakeTriggerSpec(
		const FConditionId& StatusId,
		const EBattleMajorStatusKind Kind,
		const EBattleTriggerPhase Phase,
		const FBattleTriggerSubject& Owner,
		const int32 Order)
	{
		FBattleTriggerRegistrationSpec Spec;
		Spec.Rule.Phase = Phase;
		Spec.Rule.EffectId = MakeEffectId(Kind, Phase);
		Spec.Rule.PayloadId = MakePayloadId(StatusId);
		Spec.Rule.Order = Order;
		const bool bSourceCreated = FBattleTriggerSourceDefinition::TryCreateCondition(
			StatusId,
			Spec.SourceDefinition);
		check(bSourceCreated);
		Spec.Owner = Owner;
		Spec.Source = Owner;
		Spec.Targets.Add(Owner);
		Spec.DurationOwner = Owner;
		Spec.Layers = 1;
		Spec.Visibility = FBattleTriggerVisibility::CreateCoreOnly();
		Spec.CleanupPolicy = EBattleTriggerCleanupPolicy::OnFaint
			| EBattleTriggerCleanupPolicy::OnCapture
			| EBattleTriggerCleanupPolicy::OnBattleEnd
			| EBattleTriggerCleanupPolicy::OnRemoval;
		return Spec;
	}

	FBattleRandomContext WithPurpose(
		const FBattleRandomContext& BaseContext,
		const FDefinitionId& Purpose)
	{
		FBattleRandomContext Context = BaseContext;
		Context.RulePurpose = Purpose;
		return Context;
	}
}

FConditionId FBattleMajorStatusRules::GetBurnId()
{
	return BattleMajorStatusPrivate::MakeDefinitionId<FConditionId>(TEXT("Condition.Burn"));
}

FConditionId FBattleMajorStatusRules::GetParalysisId()
{
	return BattleMajorStatusPrivate::MakeDefinitionId<FConditionId>(TEXT("Condition.Paralysis"));
}

FConditionId FBattleMajorStatusRules::GetSleepId()
{
	return BattleMajorStatusPrivate::MakeDefinitionId<FConditionId>(TEXT("Condition.Sleep"));
}

FConditionId FBattleMajorStatusRules::GetFreezeId()
{
	return BattleMajorStatusPrivate::MakeDefinitionId<FConditionId>(TEXT("Condition.Freeze"));
}

FConditionId FBattleMajorStatusRules::GetPoisonId()
{
	return BattleMajorStatusPrivate::MakeDefinitionId<FConditionId>(TEXT("Condition.Poison"));
}

FConditionId FBattleMajorStatusRules::GetToxicId()
{
	return BattleMajorStatusPrivate::MakeDefinitionId<FConditionId>(TEXT("Condition.Toxic"));
}

TArray<FConditionId> FBattleMajorStatusRules::GetCanonicalIds()
{
	return {
		GetBurnId(),
		GetParalysisId(),
		GetSleepId(),
		GetFreezeId(),
		GetPoisonId(),
		GetToxicId()
	};
}

EBattleMajorStatusKind FBattleMajorStatusRules::GetKind(const FConditionId& StatusId)
{
	if (!StatusId.IsValid()) return EBattleMajorStatusKind::None;
	if (StatusId == GetBurnId()) return EBattleMajorStatusKind::Burn;
	if (StatusId == GetParalysisId()) return EBattleMajorStatusKind::Paralysis;
	if (StatusId == GetSleepId()) return EBattleMajorStatusKind::Sleep;
	if (StatusId == GetFreezeId()) return EBattleMajorStatusKind::Freeze;
	if (StatusId == GetPoisonId()) return EBattleMajorStatusKind::Poison;
	if (StatusId == GetToxicId()) return EBattleMajorStatusKind::Toxic;
	return EBattleMajorStatusKind::Invalid;
}

bool FBattleMajorStatusRules::IsCanonical(const FConditionId& StatusId)
{
	const EBattleMajorStatusKind Kind = GetKind(StatusId);
	return Kind != EBattleMajorStatusKind::None && Kind != EBattleMajorStatusKind::Invalid;
}

bool FBattleMajorStatusRules::TryEvaluateApplication(
	const FBattleMajorStatusApplicationFacts& Facts,
	FBattleMajorStatusApplicationResult& OutResult)
{
	OutResult = FBattleMajorStatusApplicationResult();
	const EBattleMajorStatusKind Kind = GetKind(Facts.RequestedStatusId);
	if (Kind == EBattleMajorStatusKind::None
		|| Kind == EBattleMajorStatusKind::Invalid
		|| !BattleMajorStatusPrivate::IsValidTypePair(Facts.PrimaryType, Facts.SecondaryType))
	{
		return false;
	}

	OutResult.bValid = true;
	OutResult.Kind = Kind;
	OutResult.Outcome = EBattleMajorStatusApplicationOutcome::CanApply;
	OutResult.PreventionReason = EBattleMajorStatusPreventionReason::None;
	if (Facts.ExistingMajorStatusId.IsValid())
	{
		OutResult.Outcome = EBattleMajorStatusApplicationOutcome::AlreadyHasMajorStatus;
		OutResult.PreventionReason = EBattleMajorStatusPreventionReason::ExistingMajorStatus;
		return true;
	}

	const bool bTypeImmune = (Kind == EBattleMajorStatusKind::Burn
			&& BattleMajorStatusPrivate::HasType(Facts, EPokemonType::Fire))
		|| (Kind == EBattleMajorStatusKind::Paralysis
			&& BattleMajorStatusPrivate::HasType(Facts, EPokemonType::Electric))
		|| (Kind == EBattleMajorStatusKind::Freeze
			&& BattleMajorStatusPrivate::HasType(Facts, EPokemonType::Ice))
		|| ((Kind == EBattleMajorStatusKind::Poison || Kind == EBattleMajorStatusKind::Toxic)
			&& (BattleMajorStatusPrivate::HasType(Facts, EPokemonType::Poison)
				|| BattleMajorStatusPrivate::HasType(Facts, EPokemonType::Steel)));
	if (bTypeImmune)
	{
		OutResult.Outcome = EBattleMajorStatusApplicationOutcome::TypeImmune;
		OutResult.PreventionReason = EBattleMajorStatusPreventionReason::TypeImmunity;
		return true;
	}

	if (Kind == EBattleMajorStatusKind::Freeze && Facts.Prevention.bSunActive)
	{
		OutResult.Outcome = EBattleMajorStatusApplicationOutcome::Prevented;
		OutResult.PreventionReason = EBattleMajorStatusPreventionReason::Sun;
	}
	else if (Facts.Prevention.bTerrainPrevents)
	{
		OutResult.Outcome = EBattleMajorStatusApplicationOutcome::Prevented;
		OutResult.PreventionReason = EBattleMajorStatusPreventionReason::Terrain;
	}
	else if (Facts.Prevention.bSafeguardPrevents)
	{
		OutResult.Outcome = EBattleMajorStatusApplicationOutcome::Prevented;
		OutResult.PreventionReason = EBattleMajorStatusPreventionReason::Safeguard;
	}
	else if (Facts.Prevention.bAbilityPrevents)
	{
		OutResult.Outcome = EBattleMajorStatusApplicationOutcome::Prevented;
		OutResult.PreventionReason = EBattleMajorStatusPreventionReason::Ability;
	}
	else if (Facts.Prevention.bItemPrevents)
	{
		OutResult.Outcome = EBattleMajorStatusApplicationOutcome::Prevented;
		OutResult.PreventionReason = EBattleMajorStatusPreventionReason::Item;
	}
	return true;
}

bool FBattleMajorStatusRules::TryRollSleepDuration(
	const FBattleRandomContext& BaseContext,
	IBattleRandom& Random,
	FBattleSleepDurationResult& OutResult)
{
	OutResult = FBattleSleepDurationResult();
	const FBattleRandomContext Context = BattleMajorStatusPrivate::WithPurpose(
		BaseContext,
		GetSleepDurationPurpose());
	if (!Context.IsValid() || !Random.TryDrawUniform(2, 4, Context, OutResult.Draw))
	{
		OutResult = FBattleSleepDurationResult();
		return false;
	}
	OutResult.bValid = true;
	OutResult.Turns = static_cast<int32>(OutResult.Draw.Result);
	return true;
}

bool FBattleMajorStatusRules::TryResolveBeforeAction(
	const FBattleMajorStatusActionFacts& Facts,
	const FBattleRandomContext& BaseContext,
	IBattleRandom& Random,
	FBattleMajorStatusActionResult& OutResult)
{
	OutResult = FBattleMajorStatusActionResult();
	const EBattleMajorStatusKind Kind = GetKind(Facts.StatusId);
	if (Kind == EBattleMajorStatusKind::None || Kind == EBattleMajorStatusKind::Invalid)
	{
		return false;
	}

	OutResult.bValid = true;
	OutResult.Outcome = EBattleMajorStatusActionOutcome::Allowed;
	if (Kind == EBattleMajorStatusKind::Sleep)
	{
		if (!Facts.RemainingSleepTurns.IsSet()
			|| Facts.RemainingSleepTurns.GetValue() <= 0)
		{
			OutResult = FBattleMajorStatusActionResult();
			return false;
		}
		const int32 Remaining = Facts.RemainingSleepTurns.GetValue() - 1;
		OutResult.RemainingSleepTurns = Remaining;
		if (Remaining == 0)
		{
			OutResult.Outcome = EBattleMajorStatusActionOutcome::CuredAndAllowed;
			OutResult.bCureStatus = true;
		}
		else if (!Facts.bMoveUsableWhileAsleep)
		{
			OutResult.Outcome = EBattleMajorStatusActionOutcome::Denied;
		}
		return true;
	}

	if (Kind == EBattleMajorStatusKind::Freeze)
	{
		if (Facts.bMoveThawsUser)
		{
			OutResult.Outcome = EBattleMajorStatusActionOutcome::CuredAndAllowed;
			OutResult.bCureStatus = true;
			return true;
		}
		const FBattleRandomContext Context = BattleMajorStatusPrivate::WithPurpose(
			BaseContext,
			GetFreezeNaturalThawPurpose());
		if (!Context.IsValid() || !Random.TryDrawUniform(0, 4, Context, OutResult.Draw))
		{
			OutResult = FBattleMajorStatusActionResult();
			return false;
		}
		OutResult.bDrawConsumed = true;
		if (OutResult.Draw.Result == 0)
		{
			OutResult.Outcome = EBattleMajorStatusActionOutcome::CuredAndAllowed;
			OutResult.bCureStatus = true;
		}
		else
		{
			OutResult.Outcome = EBattleMajorStatusActionOutcome::Denied;
		}
		return true;
	}

	if (Kind == EBattleMajorStatusKind::Paralysis)
	{
		const FBattleRandomContext Context = BattleMajorStatusPrivate::WithPurpose(
			BaseContext,
			GetParalysisActionGatePurpose());
		if (!Context.IsValid() || !Random.TryDrawUniform(0, 3, Context, OutResult.Draw))
		{
			OutResult = FBattleMajorStatusActionResult();
			return false;
		}
		OutResult.bDrawConsumed = true;
		if (OutResult.Draw.Result == 0)
		{
			OutResult.Outcome = EBattleMajorStatusActionOutcome::Denied;
		}
	}
	return true;
}

bool FBattleMajorStatusRules::TryApplySpeedModifier(
	const FConditionId& StatusId,
	const int32 StageAdjustedSpeed,
	int32& OutEffectiveSpeed)
{
	OutEffectiveSpeed = 0;
	if (StageAdjustedSpeed < 0)
	{
		return false;
	}
	OutEffectiveSpeed = StatusId == GetParalysisId()
		? StageAdjustedSpeed / 2
		: StageAdjustedSpeed;
	return true;
}

bool FBattleMajorStatusRules::ShouldApplyBurnPhysicalPenalty(
	const FConditionId& StatusId,
	const EBattleMoveCategory MoveCategory,
	const bool bBypassBurnPenalty)
{
	return StatusId == GetBurnId()
		&& MoveCategory == EBattleMoveCategory::Physical
		&& !bBypassBurnPenalty;
}

bool FBattleMajorStatusRules::TryResolveResidual(
	const FBattleMajorStatusResidualFacts& Facts,
	FBattleMajorStatusResidualResult& OutResult)
{
	OutResult = FBattleMajorStatusResidualResult();
	const EBattleMajorStatusKind Kind = GetKind(Facts.StatusId);
	if (Facts.BaseMaximumHP <= 0
		|| (Kind != EBattleMajorStatusKind::Burn
			&& Kind != EBattleMajorStatusKind::Poison
			&& Kind != EBattleMajorStatusKind::Toxic))
	{
		return false;
	}
	if (Kind == EBattleMajorStatusKind::Toxic
		&& (Facts.ToxicLayerEncoding < 1 || Facts.ToxicLayerEncoding > 16))
	{
		return false;
	}

	OutResult.bValid = true;
	OutResult.bAppliesDamage = true;
	if (Kind == EBattleMajorStatusKind::Burn)
	{
		OutResult.Damage = FMath::Max(1, Facts.BaseMaximumHP / 16);
	}
	else if (Kind == EBattleMajorStatusKind::Poison)
	{
		OutResult.Damage = FMath::Max(1, Facts.BaseMaximumHP / 8);
	}
	else
	{
		OutResult.PreviousToxicStage = Facts.ToxicLayerEncoding - 1;
		OutResult.ToxicStage = FMath::Min(15, OutResult.PreviousToxicStage + 1);
		OutResult.ToxicLayerEncoding = OutResult.ToxicStage + 1;
		OutResult.Damage = FMath::Max(1, Facts.BaseMaximumHP / 16)
			* OutResult.ToxicStage;
	}
	return true;
}

bool FBattleMajorStatusRules::ShouldThawReachedTarget(
	const FConditionId& TargetStatusId,
	const EPokemonType MoveType,
	const bool bDamagingMove,
	const bool bMoveThawsTarget,
	const bool bReachedTarget)
{
	return bReachedTarget
		&& TargetStatusId == GetFreezeId()
		&& (bMoveThawsTarget || (bDamagingMove && MoveType == EPokemonType::Fire));
}

bool FBattleMajorStatusRules::TryBuildTriggerRegistrationSpecs(
	const FConditionId& StatusId,
	const FBattleTriggerSubject& Owner,
	const TOptional<int32>& SleepTurns,
	TArray<FBattleTriggerRegistrationSpec>& OutSpecs)
{
	OutSpecs.Reset();
	const EBattleMajorStatusKind Kind = GetKind(StatusId);
	if (!Owner.IsValid()
		|| Kind == EBattleMajorStatusKind::None
		|| Kind == EBattleMajorStatusKind::Invalid
		|| (Kind == EBattleMajorStatusKind::Sleep
			&& (!SleepTurns.IsSet()
				|| SleepTurns.GetValue() < 2
				|| SleepTurns.GetValue() > 4))
		|| (Kind != EBattleMajorStatusKind::Sleep && SleepTurns.IsSet()))
	{
		return false;
	}

	auto Add = [&OutSpecs, &StatusId, Kind, &Owner](
		const EBattleTriggerPhase Phase,
		const int32 Order) -> FBattleTriggerRegistrationSpec&
	{
		return OutSpecs.Add_GetRef(BattleMajorStatusPrivate::MakeTriggerSpec(
			StatusId,
			Kind,
			Phase,
			Owner,
			Order));
	};

	switch (Kind)
	{
	case EBattleMajorStatusKind::Burn:
		Add(EBattleTriggerPhase::BeforeDamage, 0);
		Add(EBattleTriggerPhase::EndTurn, 10);
		break;
	case EBattleMajorStatusKind::Paralysis:
		Add(EBattleTriggerPhase::ActionOrderCalculation, 0);
		Add(EBattleTriggerPhase::BeforeAction, 0);
		break;
	case EBattleMajorStatusKind::Sleep:
	{
		FBattleTriggerRegistrationSpec& Spec = Add(EBattleTriggerPhase::BeforeAction, 0);
		Spec.RemainingTurns = SleepTurns;
		Spec.Rule.bDecrementDurationBeforeEffect = true;
		break;
	}
	case EBattleMajorStatusKind::Freeze:
		Add(EBattleTriggerPhase::BeforeAction, 0);
		break;
	case EBattleMajorStatusKind::Poison:
		Add(EBattleTriggerPhase::EndTurn, 9);
		break;
	case EBattleMajorStatusKind::Toxic:
		Add(EBattleTriggerPhase::EndTurn, 9);
		Add(EBattleTriggerPhase::SwitchOut, 0);
		break;
	default:
		return false;
	}
	return true;
}

bool FBattleMajorStatusRules::TryRegisterTriggers(
	FBattleTriggerFramework& Framework,
	const FConditionId& StatusId,
	const FBattleTriggerSubject& Owner,
	const TOptional<int32>& SleepTurns,
	EBattleTriggerError& OutError)
{
	OutError = EBattleTriggerError::None;
	TArray<FBattleTriggerRegistrationSpec> Specs;
	if (!TryBuildTriggerRegistrationSpecs(StatusId, Owner, SleepTurns, Specs))
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

bool FBattleMajorStatusRules::TryCleanupTriggers(
	FBattleTriggerFramework& Framework,
	const FConditionId& StatusId,
	const FBattleTriggerSubject& Owner,
	const EBattleTriggerCleanupReason Reason,
	const FBattleTriggerOperationContext& Context,
	EBattleTriggerError& OutError)
{
	OutError = EBattleTriggerError::None;
	if (!IsCanonical(StatusId) || !Owner.IsValid())
	{
		OutError = EBattleTriggerError::InvalidDefinition;
		return false;
	}
	FBattleTriggerSourceDefinition SourceDefinition;
	if (!FBattleTriggerSourceDefinition::TryCreateCondition(StatusId, SourceDefinition))
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

FDefinitionId FBattleMajorStatusRules::GetSleepDurationPurpose()
{
	return BattleMajorStatusPrivate::MakeDefinitionId<FDefinitionId>(
		TEXT("Rule.MajorStatus.Sleep.Duration"));
}

FDefinitionId FBattleMajorStatusRules::GetParalysisActionGatePurpose()
{
	return BattleMajorStatusPrivate::MakeDefinitionId<FDefinitionId>(
		TEXT("Rule.MajorStatus.Paralysis.ActionGate"));
}

FDefinitionId FBattleMajorStatusRules::GetFreezeNaturalThawPurpose()
{
	return BattleMajorStatusPrivate::MakeDefinitionId<FDefinitionId>(
		TEXT("Rule.MajorStatus.Freeze.NaturalThaw"));
}
