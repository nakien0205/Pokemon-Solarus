#include "BattleEffectExecutorContext.h"

#include "Battle/BattleAbility.h"
#include "BattleEntryHazardPrevention.h"
#include "Battle/BattleFieldSideConditions.h"
#include "Battle/BattleItem.h"
#include "Battle/BattleMajorStatus.h"
#include "Battle/BattleState.h"
#include "Battle/BattleVolatile.h"
#include "BattleMoveRedirection.h"
#include "Math/NumericLimits.h"

namespace BattleEffectExecutorPrivate
{
	bool TryAddRandomEvent(
		IBattleEffectExecutionContext& Context,
		FBattleEffectExecutionResult& Result,
		const FBattleResolvedTarget& Target,
		const FBattleRandomDraw& Draw,
		EBattleEffectExecutionOutcome Outcome,
		const TOptional<uint16>& HitIndex);

	FBattleEffectHookResult FStateExecutionContext::CheckReachability(
		const FBattleMoveDefinition& Move,
		const FBattleResolvedTarget& Target)
	{
		if (Target.GetKind() != EBattleResolvedTargetKind::Battler)
		{
			return Applied();
		}
		const FBattleBattlerTarget& BattlerTarget = Target.GetBattler();
		const FBattleActivePositionState* Position = State.FindActivePosition(
			BattlerTarget.ActiveSlotId);
		const FBattleBattlerState* Battler = FindBattler(BattlerTarget.BattlerId);
		if (Position == nullptr
			|| !Position->bAvailable
			|| Position->BattlerId != BattlerTarget.BattlerId
			|| Battler == nullptr
			|| Battler->CurrentHP <= 0
			|| Battler->bFainted
			|| Battler->bCaptured
			|| Battler->bRemoved)
		{
			return Outcome(EBattleEffectExecutionOutcome::Unreachable);
		}
		FBattleFlyReachabilityFacts Facts;
		Facts.bTargetFlySemiInvulnerable = IsVolatileActiveForPhase(
			Battler->BattlerId,
			FBattleVolatileRules::GetFlySemiInvulnerableId(),
			EBattleTriggerPhase::BeforeHit);
		Facts.bMoveReachesFlyTarget = EnumHasAllFlags(
			Move.Flags,
			EBattleMoveFlags::ReachesAirborneSemiInvulnerableTarget);
		Facts.bMoveDoublesPowerAgainstFlyTarget = EnumHasAllFlags(
			Move.Flags,
			EBattleMoveFlags::DoublesPowerAgainstAirborneSemiInvulnerableTarget);
		FBattleFlyReachabilityResult Reachability;
		if (!FBattleVolatileRules::TryResolveFlyReachability(Facts, Reachability))
		{
			return Outcome(EBattleEffectExecutionOutcome::Failed);
		}
		if (!Reachability.bReachable)
		{
			return Outcome(EBattleEffectExecutionOutcome::Unreachable);
		}
		return Applied();
	}

	FBattleEffectHookResult FStateExecutionContext::CheckProtection(
		const FBattleMoveDefinition& Move,
		const FBattleResolvedTarget& Target)
	{
		if (Target.GetKind() == EBattleResolvedTargetKind::Battler
			&& FBattleVolatileRules::ShouldProtectBlockEffect(
				IsVolatileActiveForPhase(
					Target.GetBattler().BattlerId,
					FBattleVolatileRules::GetProtectId(),
					EBattleTriggerPhase::BeforeHit),
				EnumHasAllFlags(Move.Flags, EBattleMoveFlags::BlockedByProtect),
				EnumHasAllFlags(Move.Flags, EBattleMoveFlags::BypassesProtect)
					|| EnumHasAllFlags(Move.Flags, EBattleMoveFlags::BreaksProtection)))
		{
			return Outcome(EBattleEffectExecutionOutcome::Protected);
		}
		return Applied();
	}

	FBattleEffectHookResult FStateExecutionContext::ApplyProtectionBreaking(
		const FBattleMoveDefinition& Move,
		const FBattleResolvedTarget& Target)
	{
		if (!EnumHasAllFlags(Move.Flags, EBattleMoveFlags::BreaksProtection)
			|| Target.GetKind() != EBattleResolvedTargetKind::Battler)
		{
			return Applied();
		}
		FBattleBattlerState* Battler = FindMutableBattler(
			Target.GetBattler().BattlerId);
		if (Battler == nullptr
			|| !HasVolatile(*Battler, FBattleVolatileRules::GetProtectId()))
		{
			return Applied();
		}
		if (!TrySetVolatilePhaseSuppressed(
				Battler->BattlerId,
				FBattleVolatileRules::GetProtectId(),
				EBattleTriggerPhase::BeforeHit,
				true))
		{
			return Outcome(EBattleEffectExecutionOutcome::Failed);
		}
		if (ExecutionResult == nullptr)
		{
			return Outcome(EBattleEffectExecutionOutcome::Failed);
		}
		FBattleEventTarget EventTarget;
		if (!TryBuildEventTarget(Target, EventTarget))
		{
			return Outcome(EBattleEffectExecutionOutcome::Failed);
		}
		FBattleEffectExecutionEvent& Event =
			ExecutionResult->Events.AddDefaulted_GetRef();
		Event.Type = EBattleEventType::StatusChanged;
		Event.Cause = EBattleEventCause::Move;
		Event.Outcome = EBattleEffectExecutionOutcome::Applied;
		Event.Targets.Add(MoveTemp(EventTarget));
		Event.NumericBefore = 1;
		Event.NumericAfter = 0;
		Event.NumericDelta = -1;
		FBattleEffectHookResult Result = Applied();
		Result.bStateMutated = true;
		Result.NumericBefore = 1;
		Result.NumericAfter = 0;
		Result.NumericDelta = -1;
		return Result;
	}

	bool FStateExecutionContext::ShouldSkipEffectDescriptor(
		const FBattleMoveEffectDescriptor& Effect) const
	{
		if (Effect.Kind != EBattleMoveEffectKind::Charge
			&& Effect.Kind != EBattleMoveEffectKind::SemiInvulnerability)
		{
			return false;
		}
		FMoveId LockedMoveId;
		return TryGetVolatilePayloadMoveId(
			Request.UserBattlerId,
			FBattleVolatileRules::GetChargingId(),
			LockedMoveId)
			&& LockedMoveId == Request.Move->Id;
	}

	FBattleEffectHookResult FStateExecutionContext::CheckEffectEligibility(
		const FBattleMoveEffectDescriptor& Effect,
		const FBattleResolvedTarget& Target)
	{
		if (!Target.IsValid())
		{
			return Outcome(EBattleEffectExecutionOutcome::Failed);
		}
		if (Target.GetKind() == EBattleResolvedTargetKind::Battler)
		{
			const FBattleBattlerState* Battler = FindBattler(Target.GetBattler().BattlerId);
			if (Battler == nullptr
				|| Battler->CurrentHP <= 0
				|| Battler->bFainted
				|| Battler->bCaptured
				|| Battler->bRemoved)
			{
				return Outcome(EBattleEffectExecutionOutcome::Prevented);
			}
			const bool bEffectFromOpponent = Target.GetBattler().ActiveSlotId.GetSide()
				!= Request.UserSlotId.GetSide();
			const bool bBypassesSubstitute = EnumHasAllFlags(
				Request.Move->Flags,
				EBattleMoveFlags::BypassesSubstitute)
				|| EnumHasAllFlags(
					Effect.Flags,
					EBattleMoveEffectFlags::BypassesSubstitute);
			if (FBattleVolatileRules::ShouldSubstituteBlockEffect(
					HasVolatile(*Battler, FBattleVolatileRules::GetSubstituteId())
						|| SubstituteProtectedTargets.Contains(Battler->BattlerId),
					bEffectFromOpponent,
					bBypassesSubstitute))
			{
				return Outcome(EBattleEffectExecutionOutcome::Blocked);
			}
			if ((Effect.Kind == EBattleMoveEffectKind::Heal
					|| Effect.Kind == EBattleMoveEffectKind::Drain)
				&& Battler->CurrentHP >= Battler->PermanentStats.MaxHP)
			{
				FBattleEffectHookResult Result = Outcome(EBattleEffectExecutionOutcome::Capped);
				Result.NumericBefore = Battler->CurrentHP;
				Result.NumericAfter = Battler->CurrentHP;
				Result.NumericDelta = 0;
				Result.bCapped = true;
				return Result;
			}
			if (Effect.Kind == EBattleMoveEffectKind::ApplyCondition)
			{
				const FBattleConditionDefinition* Definition = State.Catalog.FindCondition(
					Effect.ConditionId);
				if (Definition == nullptr)
				{
					return Outcome(EBattleEffectExecutionOutcome::Failed);
				}
				if (Definition->Kind == EBattleConditionKind::MajorStatus
					&& FBattleMajorStatusRules::IsCanonical(Effect.ConditionId))
				{
					const FBattleSpeciesFormDefinition* Species = State.Catalog.FindSpeciesForm(
						Battler->SpeciesFormId);
					if (Species == nullptr)
					{
						return Outcome(EBattleEffectExecutionOutcome::Failed);
					}
					FBattleMajorStatusApplicationFacts Facts;
					Facts.RequestedStatusId = Effect.ConditionId;
					Facts.ExistingMajorStatusId = Battler->MajorStatusId;
					Facts.PrimaryType = Species->PrimaryType;
					Facts.SecondaryType = Species->SecondaryType;
					bool bTargetGrounded = false;
					bool bLevitateMadeAirborne = false;
					if (!TryIsGrounded(
						*Battler,
						bTargetGrounded,
						ShouldIgnoreLevitateForCurrentMove(*Battler),
						&bLevitateMadeAirborne))
					{
						return Outcome(EBattleEffectExecutionOutcome::Failed);
					}
					const bool bAppliedByOpponent =
						Target.GetBattler().ActiveSlotId.GetSide()
						!= Request.UserSlotId.GetSide();
					const FConditionId WeatherId = GetWeatherId();
					bool bWeatherTriggerActive = false;
					if (FBattleFieldSideConditionRules::IsCanonical(WeatherId)
						&& !TryIsFieldSideConditionActiveForPhase(
							WeatherId,
							TOptional<EBattleSide>(),
							EBattleTriggerPhase::BeforeHit,
							Target.GetBattler().ActiveSlotId,
							bWeatherTriggerActive))
					{
						return Outcome(EBattleEffectExecutionOutcome::Failed);
					}
					Facts.Prevention.bSunActive =
						bWeatherTriggerActive
						&& FBattleFieldSideConditionRules::ShouldSunPreventFreeze(WeatherId);
					const FConditionId TerrainId = GetTerrainId();
					bool bTerrainTriggerActive = false;
					if (FBattleFieldSideConditionRules::IsCanonical(TerrainId)
						&& !TryIsFieldSideConditionActiveForPhase(
							TerrainId,
							TOptional<EBattleSide>(),
							EBattleTriggerPhase::BeforeHit,
							Target.GetBattler().ActiveSlotId,
							bTerrainTriggerActive))
					{
						return Outcome(EBattleEffectExecutionOutcome::Failed);
					}
					Facts.Prevention.bTerrainPrevents =
						bTerrainTriggerActive
						&& FBattleFieldSideConditionRules::ShouldTerrainPreventMajorStatus(
							TerrainId,
							Effect.ConditionId,
							bTargetGrounded);
					bool bSafeguardTriggerActive = false;
					if (!TryIsFieldSideConditionActiveForPhase(
							FBattleFieldSideConditionRules::GetSafeguardId(),
							Target.GetBattler().ActiveSlotId.GetSide(),
							EBattleTriggerPhase::BeforeHit,
							Target.GetBattler().ActiveSlotId,
							bSafeguardTriggerActive))
					{
						return Outcome(EBattleEffectExecutionOutcome::Failed);
					}
					Facts.Prevention.bSafeguardPrevents =
						FBattleFieldSideConditionRules::ShouldSafeguardPrevent(
							bSafeguardTriggerActive,
							bAppliedByOpponent,
							EnumHasAllFlags(
								Request.Move->Flags,
								EBattleMoveFlags::BypassesSideProtection));
					FBattleMajorStatusApplicationResult Application;
					if (!FBattleMajorStatusRules::TryEvaluateApplication(Facts, Application))
					{
						return Outcome(EBattleEffectExecutionOutcome::Failed);
					}
					if (Application.Outcome == EBattleMajorStatusApplicationOutcome::TypeImmune)
					{
						return Outcome(EBattleEffectExecutionOutcome::Immune);
					}
					if (Application.Outcome == EBattleMajorStatusApplicationOutcome::Prevented)
					{
						return Outcome(EBattleEffectExecutionOutcome::Prevented);
					}
					if (Application.Outcome
						!= EBattleMajorStatusApplicationOutcome::CanApply)
					{
						return Outcome(EBattleEffectExecutionOutcome::Failed);
					}
					if (bLevitateMadeAirborne
						&& bTerrainTriggerActive
						&& FBattleFieldSideConditionRules::ShouldTerrainPreventMajorStatus(
							TerrainId,
							Effect.ConditionId,
							true)
						&& !TryRecordLevitateGroundedActivation(
							*Battler,
							EBattleTriggerPhase::BeforeHit,
							EBattleAbilityItemHookPoint::TypeImmunity))
					{
						return Outcome(EBattleEffectExecutionOutcome::Failed);
					}
				}
				if ((Definition->Kind == EBattleConditionKind::MajorStatus
						&& Battler->MajorStatusId.IsValid())
					|| (Definition->Kind == EBattleConditionKind::Volatile
						&& Battler->Volatiles.ContainsByPredicate(
							[&Effect](const FBattleConditionState& Existing)
							{
								return Existing.ConditionId == Effect.ConditionId;
							})))
				{
					return Outcome(EBattleEffectExecutionOutcome::Failed);
				}
			}
			if (Effect.Kind == EBattleMoveEffectKind::ModifyStatStage)
			{
				const bool bAppliedByOpponent =
					Target.GetBattler().ActiveSlotId.GetSide()
					!= Request.UserSlotId.GetSide();
				bool bMistTriggerActive = false;
				if (!TryIsFieldSideConditionActiveForPhase(
						FBattleFieldSideConditionRules::GetMistId(),
						Target.GetBattler().ActiveSlotId.GetSide(),
						EBattleTriggerPhase::BeforeHit,
						Target.GetBattler().ActiveSlotId,
						bMistTriggerActive))
				{
					return Outcome(EBattleEffectExecutionOutcome::Failed);
				}
				if (FBattleFieldSideConditionRules::ShouldMistPreventStatDrop(
						bMistTriggerActive,
						bAppliedByOpponent,
						EnumHasAllFlags(
							Request.Move->Flags,
							EBattleMoveFlags::BypassesSideProtection),
						Effect.MagnitudeNumerator))
				{
					return Outcome(EBattleEffectExecutionOutcome::Prevented);
				}
				int32 Stage = 0;
				if (!Battler->Stages.TryGetStage(Effect.Stat, Stage))
				{
					return Outcome(EBattleEffectExecutionOutcome::Failed);
				}
				if ((Effect.MagnitudeNumerator > 0 && Stage >= FBattleStatStages::MaximumStage)
					|| (Effect.MagnitudeNumerator < 0 && Stage <= FBattleStatStages::MinimumStage))
				{
					FBattleEffectHookResult Result = Outcome(EBattleEffectExecutionOutcome::Capped);
					Result.NumericBefore = Stage;
					Result.NumericAfter = Stage;
					Result.NumericDelta = 0;
					Result.bCapped = true;
					return Result;
				}
			}
			if (Effect.Kind == EBattleMoveEffectKind::RemoveCondition)
			{
				const FBattleConditionDefinition* Definition = State.Catalog.FindCondition(
					Effect.ConditionId);
				const bool bPresent = Definition != nullptr
					&& ((Definition->Kind == EBattleConditionKind::MajorStatus
							&& Battler->MajorStatusId == Effect.ConditionId)
						|| (Definition->Kind == EBattleConditionKind::Volatile
							&& Battler->Volatiles.ContainsByPredicate(
								[&Effect](const FBattleConditionState& Existing)
								{
									return Existing.ConditionId == Effect.ConditionId;
								})));
				if (!bPresent)
				{
					return Outcome(EBattleEffectExecutionOutcome::Failed);
				}
			}
			return Applied();
		}

		if (Target.GetKind() == EBattleResolvedTargetKind::Side)
		{
			const FBattleSideState* Side = Sides.FindByPredicate(
				[&Target](const FBattleSideState& Entry)
				{
					return Entry.Side == Target.GetSide();
				});
			if (Side == nullptr)
			{
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}
			if (Effect.Kind == EBattleMoveEffectKind::SetSideCondition
				&& FBattleFieldSideConditionRules::IsCanonical(Effect.ConditionId))
			{
				const FBattleConditionDefinition* Definition = State.Catalog.FindCondition(
					Effect.ConditionId);
				if (Definition == nullptr
					|| Definition->Kind
						!= FBattleFieldSideConditionRules::GetConditionFamily(Effect.ConditionId))
				{
					return Outcome(EBattleEffectExecutionOutcome::Failed);
				}
				const TArray<FBattleConditionState>& Collection =
					Definition->Kind == EBattleConditionKind::Hazard
					? Side->Hazards
					: Side->Conditions;
				const FBattleConditionState* Existing = Collection.FindByPredicate(
					[&Effect](const FBattleConditionState& Condition)
					{
						return Condition.ConditionId == Effect.ConditionId;
					});
				FBattleFieldSideApplicationFacts Facts;
				Facts.RequestedConditionId = Effect.ConditionId;
				Facts.bRequestedAlreadyActive = Existing != nullptr;
				Facts.ExistingLayers = Existing != nullptr ? Existing->LayerCount : 0;
				Facts.bSnowActive = GetWeatherId()
					== FBattleFieldSideConditionRules::GetSnowId();
				Facts.bDurationExtensionActive = Effect.DurationTurns == 8;
				FBattleFieldSideApplicationResult Application;
				if (!FBattleFieldSideConditionRules::TryEvaluateApplication(Facts, Application))
				{
					return Outcome(EBattleEffectExecutionOutcome::Failed);
				}
				return Application.Outcome == EBattleFieldSideApplicationOutcome::Create
					|| Application.Outcome == EBattleFieldSideApplicationOutcome::AddLayer
					? Applied()
					: Outcome(EBattleEffectExecutionOutcome::Failed);
			}
			const bool bPresent = Side->Conditions.ContainsByPredicate(
				[&Effect](const FBattleConditionState& Existing)
				{
					return Existing.ConditionId == Effect.ConditionId;
				}) || Side->Hazards.ContainsByPredicate(
				[&Effect](const FBattleConditionState& Existing)
				{
					return Existing.ConditionId == Effect.ConditionId;
				});
			if ((Effect.Kind == EBattleMoveEffectKind::SetSideCondition && bPresent)
				|| (Effect.Kind == EBattleMoveEffectKind::RemoveCondition && !bPresent))
			{
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}
			return Applied();
		}

		if (Target.GetKind() == EBattleResolvedTargetKind::Field)
		{
			if (Effect.Kind == EBattleMoveEffectKind::SetFieldCondition
				&& FBattleFieldSideConditionRules::IsCanonical(Effect.ConditionId))
			{
				const FBattleConditionDefinition* Definition = State.Catalog.FindCondition(
					Effect.ConditionId);
				const EBattleConditionKind Family =
					FBattleFieldSideConditionRules::GetConditionFamily(Effect.ConditionId);
				if (Definition == nullptr || Definition->Kind != Family)
				{
					return Outcome(EBattleEffectExecutionOutcome::Failed);
				}
				FBattleFieldSideApplicationFacts Facts;
				Facts.RequestedConditionId = Effect.ConditionId;
				if (Family == EBattleConditionKind::Weather && Field.Weather.IsSet())
				{
					Facts.ExistingExclusiveConditionId = Field.Weather.GetValue().ConditionId;
				}
				else if (Family == EBattleConditionKind::Terrain && Field.Terrain.IsSet())
				{
					Facts.ExistingExclusiveConditionId = Field.Terrain.GetValue().ConditionId;
				}
				Facts.bRequestedAlreadyActive =
					(Family == EBattleConditionKind::Weather
						&& Field.Weather.IsSet()
						&& Field.Weather.GetValue().ConditionId == Effect.ConditionId)
					|| (Family == EBattleConditionKind::Terrain
						&& Field.Terrain.IsSet()
						&& Field.Terrain.GetValue().ConditionId == Effect.ConditionId)
					|| (Family == EBattleConditionKind::Room && HasRoom(Effect.ConditionId));
				Facts.bSnowActive = GetWeatherId()
					== FBattleFieldSideConditionRules::GetSnowId();
				Facts.bDurationExtensionActive = Effect.DurationTurns == 8;
				FBattleFieldSideApplicationResult Application;
				if (!FBattleFieldSideConditionRules::TryEvaluateApplication(Facts, Application))
				{
					return Outcome(EBattleEffectExecutionOutcome::Failed);
				}
				return Application.Outcome == EBattleFieldSideApplicationOutcome::Create
					|| Application.Outcome == EBattleFieldSideApplicationOutcome::ReplaceExclusive
					|| Application.Outcome == EBattleFieldSideApplicationOutcome::ToggleOff
					? Applied()
					: Outcome(EBattleEffectExecutionOutcome::Failed);
			}
			const bool bPresent = (Field.Weather.IsSet()
					&& Field.Weather.GetValue().ConditionId == Effect.ConditionId)
				|| (Field.Terrain.IsSet()
					&& Field.Terrain.GetValue().ConditionId == Effect.ConditionId)
				|| Field.Rooms.ContainsByPredicate(
					[&Effect](const FBattleConditionState& Existing)
					{
						return Existing.ConditionId == Effect.ConditionId;
					})
				|| Field.Effects.ContainsByPredicate(
					[&Effect](const FBattleConditionState& Existing)
					{
						return Existing.ConditionId == Effect.ConditionId;
					});
			if ((Effect.Kind == EBattleMoveEffectKind::SetFieldCondition && bPresent)
				|| (Effect.Kind == EBattleMoveEffectKind::RemoveCondition && !bPresent))
			{
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}
			return Applied();
		}
		return Outcome(EBattleEffectExecutionOutcome::Failed);
	}

	FBattleEffectHookResult FStateExecutionContext::ApplyNonHpEffect(
		const FBattleMoveEffectDescriptor& Effect,
		const FBattleResolvedTarget& Target)
	{
		switch (Effect.Kind)
		{
		case EBattleMoveEffectKind::ApplyCondition:
			return ApplyCondition(Effect, Target);
		case EBattleMoveEffectKind::ModifyStatStage:
			return ApplyStatStage(Effect, Target);
		case EBattleMoveEffectKind::SetFieldCondition:
			return SetFieldCondition(Effect, Target);
		case EBattleMoveEffectKind::SetSideCondition:
			return SetSideCondition(Effect, Target);
		case EBattleMoveEffectKind::RemoveCondition:
			return RemoveCondition(Effect, Target);
		case EBattleMoveEffectKind::RegisterTargetRedirection:
			return ApplyTargetRedirectionRegistration(Target);
		case EBattleMoveEffectKind::Charge:
			return ApplyCharge(Effect, Target);
		case EBattleMoveEffectKind::Recharge:
			return ApplySimpleSpecialVolatile(
				Effect,
				Target,
				FBattleVolatileRules::GetRechargeId());
		case EBattleMoveEffectKind::Protect:
			return ApplyProtect(Effect, Target);
		case EBattleMoveEffectKind::SemiInvulnerability:
			return ApplySimpleSpecialVolatile(
				Effect,
				Target,
				FBattleVolatileRules::GetFlySemiInvulnerableId());
		case EBattleMoveEffectKind::Switch:
		case EBattleMoveEffectKind::ChangeItem:
			return Outcome(EBattleEffectExecutionOutcome::Deferred);
		default:
			return Outcome(EBattleEffectExecutionOutcome::Failed);
		}
	}

	FBattleEffectHookResult FStateExecutionContext::ApplyTargetRedirectionRegistration(
		const FBattleResolvedTarget& Target)
	{
		if (Target.GetKind() != EBattleResolvedTargetKind::Battler
			|| Target.GetBattler().ActiveSlotId != Request.UserSlotId
			|| Target.GetBattler().BattlerId != Request.UserBattlerId)
		{
			SetRuntimeFailure(EBattleEffectExecutorError::InvalidTarget);
			return Outcome(EBattleEffectExecutionOutcome::Failed);
		}

		const EBattleMoveRedirectionRegistrationOutcome RegistrationOutcome =
			FBattleMoveRedirection::TryRegister(
				State.Format,
				Request.TurnId,
				Request.ActionId,
				Target.GetBattler(),
				Battlers,
				ActivePositions,
				MoveRedirectionRegistrations);
		if (RegistrationOutcome
			== EBattleMoveRedirectionRegistrationOutcome::IneligibleFormat)
		{
			return Outcome(EBattleEffectExecutionOutcome::Failed);
		}
		if (RegistrationOutcome
			!= EBattleMoveRedirectionRegistrationOutcome::Registered)
		{
			SetRuntimeFailure(EBattleEffectExecutorError::InvalidHookResult);
			return Outcome(EBattleEffectExecutionOutcome::Failed);
		}

		FBattleEffectHookResult Result = Applied();
		Result.bStateMutated = true;
		return Result;
	}

	bool FStateExecutionContext::HasRoom(const FConditionId& RoomId) const
	{
		return Field.Rooms.ContainsByPredicate(
			[&RoomId](const FBattleConditionState& Condition)
			{
				return Condition.ConditionId == RoomId;
			});
	}

	bool FStateExecutionContext::HasSideCondition(const EBattleSide Side, const FConditionId& ConditionId) const
	{
		const FBattleSideState* SideState = FindSide(Side);
		return SideState != nullptr && SideState->Conditions.ContainsByPredicate(
			[&ConditionId](const FBattleConditionState& Condition)
			{
				return Condition.ConditionId == ConditionId;
			});
	}

	TArray<FConditionId> FStateExecutionContext::GetSideConditionIds(const EBattleSide Side) const
	{
		TArray<FConditionId> Result;
		const FBattleSideState* SideState = FindSide(Side);
		if (SideState != nullptr)
		{
			for (const FBattleConditionState& Condition : SideState->Conditions)
			{
				Result.Add(Condition.ConditionId);
			}
		}
		return Result;
	}

	FBattleConditionState FStateExecutionContext::MakeCanonicalConditionState(
		const FConditionId& ConditionId,
		const TOptional<int32>& RemainingTurns,
		const int32 Layers)
	{
		FBattleConditionState Condition;
		Condition.ConditionId = ConditionId;
		Condition.RemainingTurns = RemainingTurns;
		Condition.LayerCount = Layers;
		Condition.CreationOrdinal = NextConditionCreationOrdinal++;
		Condition.SourceBattlerId = Request.UserBattlerId;
		return Condition;
	}

	FBattleConditionState FStateExecutionContext::MakeConditionState(const FBattleMoveEffectDescriptor& Effect)
	{
		FBattleConditionState Condition;
		Condition.ConditionId = Effect.ConditionId;
		if (Effect.DurationTurns > 0)
		{
			Condition.RemainingTurns = Effect.DurationTurns;
		}
		Condition.LayerCount = Effect.LayerCount;
		Condition.CreationOrdinal = NextConditionCreationOrdinal++;
		Condition.SourceBattlerId = Request.UserBattlerId;
		return Condition;
	}

	const FBattleConditionState* FStateExecutionContext::FindVolatile(
		const FBattleBattlerState& Battler,
		const FConditionId& VolatileId)
	{
		return Battler.Volatiles.FindByPredicate(
			[&VolatileId](const FBattleConditionState& Condition)
			{
				return Condition.ConditionId == VolatileId;
			});
	}

	FBattleConditionState* FStateExecutionContext::FindMutableVolatile(
		FBattleBattlerState& Battler,
		const FConditionId& VolatileId)
	{
		return Battler.Volatiles.FindByPredicate(
			[&VolatileId](const FBattleConditionState& Condition)
			{
				return Condition.ConditionId == VolatileId;
			});
	}

	bool FStateExecutionContext::HasVolatile(
		const FBattleBattlerState& Battler,
		const FConditionId& VolatileId)
	{
		return FindVolatile(Battler, VolatileId) != nullptr;
	}

	bool FStateExecutionContext::TryAppendRandomDraw(
		const FBattleResolvedTarget& Target,
		const FBattleRandomDraw& Draw)
	{
		return ExecutionResult != nullptr
			&& TryAddRandomEvent(
				*this,
				*ExecutionResult,
					Target,
					Draw,
					EBattleEffectExecutionOutcome::Applied,
					TOptional<uint16>());
	}

	bool FStateExecutionContext::TryBuildVolatileSource(
		const FConditionId& VolatileId,
		FBattleTriggerSubject& OutSource) const
	{
		if (VolatileId == FBattleVolatileRules::GetLeechSeedId())
		{
			return FBattleTriggerSubject::TryCreateActiveSlot(
				Request.UserSlotId,
				OutSource);
		}
		return FBattleTriggerSubject::TryCreateBattler(
			Request.UserBattlerId,
			OutSource);
	}

	bool FStateExecutionContext::HasActedThisTurn(const FBattlerId BattlerId) const
	{
		return State.LockedActions.ContainsByPredicate(
			[BattlerId](const FBattleLockedActionState& Action)
			{
				return Action.Decision.GetActingBattlerId() == BattlerId
					&& (Action.bStarted || Action.bFinished);
			});
	}

	int32 FStateExecutionContext::GetCurrentPP(const FBattleBattlerState& Battler, const FMoveId MoveId) const
	{
		if (!MoveId.IsValid())
		{
			return 0;
		}
		if (MoveId == FBattleBuiltInMoveDefinitions::GetStruggleMoveId())
		{
			return 1;
		}
		const FBattleMoveSlotState* Slot = Battler.Moves.FindByPredicate(
			[MoveId](const FBattleMoveSlotState& Candidate)
			{
				return Candidate.MoveId == MoveId;
			});
		return Slot != nullptr ? Slot->CurrentPP : 0;
	}

	FBattleEffectHookResult FStateExecutionContext::ApplyCanonicalVolatile(
		const FBattleMoveEffectDescriptor& Effect,
		const FBattleResolvedTarget& Target,
		FBattleBattlerState& Battler)
	{
		const FBattleSpeciesFormDefinition* Species = State.Catalog.FindSpeciesForm(
			Battler.SpeciesFormId);
		if (Species == nullptr)
		{
			return Outcome(EBattleEffectExecutionOutcome::Failed);
		}

		const EBattleVolatileKind Kind = FBattleVolatileRules::GetKind(Effect.ConditionId);
		FBattleVolatileApplicationFacts Facts;
		Facts.RequestedVolatileId = Effect.ConditionId;
		Facts.bAlreadyPresent = HasVolatile(Battler, Effect.ConditionId);
		Facts.PrimaryType = Species->PrimaryType;
		Facts.SecondaryType = Species->SecondaryType;
		bool bLevitateMadeAirborne = false;
		if (!TryIsGrounded(
				Battler,
				Facts.bTargetGrounded,
				ShouldIgnoreLevitateForCurrentMove(Battler),
				&bLevitateMadeAirborne))
		{
			return Outcome(EBattleEffectExecutionOutcome::Failed);
		}
		const FConditionId TerrainId = GetTerrainId();
		bool bTerrainTriggerActive = false;
		if (FBattleFieldSideConditionRules::IsCanonical(TerrainId)
			&& !TryIsFieldSideConditionActiveForPhase(
				TerrainId,
				TOptional<EBattleSide>(),
				EBattleTriggerPhase::BeforeHit,
				Target.GetBattler().ActiveSlotId,
				bTerrainTriggerActive))
		{
			return Outcome(EBattleEffectExecutionOutcome::Failed);
		}
		Facts.bMistyTerrainActive = bTerrainTriggerActive
			&& FBattleFieldSideConditionRules::ShouldTerrainPreventConfusion(
				TerrainId,
				Facts.bTargetGrounded);
		Facts.bAppliedByOpponent = Target.GetBattler().ActiveSlotId.GetSide()
			!= Request.UserSlotId.GetSide();
		if (!TryIsFieldSideConditionActiveForPhase(
				FBattleFieldSideConditionRules::GetSafeguardId(),
				Target.GetBattler().ActiveSlotId.GetSide(),
				EBattleTriggerPhase::BeforeHit,
				Target.GetBattler().ActiveSlotId,
				Facts.bSafeguardActive))
		{
			return Outcome(EBattleEffectExecutionOutcome::Failed);
		}
		Facts.bBypassesSafeguard = EnumHasAllFlags(
			Request.Move->Flags,
			EBattleMoveFlags::BypassesSideProtection);
		Facts.bTargetAlreadyActed = HasActedThisTurn(Battler.BattlerId);
		Facts.LastMoveId = Battler.LastMoveId;
		Facts.LastMoveCurrentPP = GetCurrentPP(Battler, Battler.LastMoveId);
		Facts.bLastMoveIsStruggle = Battler.LastMoveId
			== FBattleBuiltInMoveDefinitions::GetStruggleMoveId();
		const FBattleMoveDefinition* LastMove = Facts.bLastMoveIsStruggle
			? &FBattleBuiltInMoveDefinitions::GetStruggle()
			: State.Catalog.FindMove(Battler.LastMoveId);
		Facts.bLastMoveUnencoreable = LastMove != nullptr
			&& EnumHasAllFlags(LastMove->Flags, EBattleMoveFlags::Unencoreable);
		Facts.BaseMaximumHP = Battler.PermanentStats.MaxHP;
		Facts.CurrentHP = Battler.CurrentHP;

		FBattleVolatileApplicationResult Application;
		if (!FBattleVolatileRules::TryEvaluateApplication(Facts, Application))
		{
			return Outcome(EBattleEffectExecutionOutcome::Failed);
		}
		if (Application.Outcome == EBattleVolatileApplicationOutcome::TypeImmune)
		{
			return Outcome(EBattleEffectExecutionOutcome::Immune);
		}
		if (Application.Outcome == EBattleVolatileApplicationOutcome::PreventedByTerrain
			|| Application.Outcome == EBattleVolatileApplicationOutcome::PreventedBySafeguard)
		{
			return Outcome(EBattleEffectExecutionOutcome::Prevented);
		}
		if (Application.Outcome != EBattleVolatileApplicationOutcome::CanApply)
		{
			return Outcome(EBattleEffectExecutionOutcome::Failed);
		}
		if (Kind == EBattleVolatileKind::Confusion
			&& bLevitateMadeAirborne
			&& bTerrainTriggerActive
			&& FBattleFieldSideConditionRules::ShouldTerrainPreventConfusion(
				TerrainId,
				true)
			&& !TryRecordLevitateGroundedActivation(
				Battler,
				EBattleTriggerPhase::BeforeHit,
				EBattleAbilityItemHookPoint::TypeImmunity))
		{
			return Outcome(EBattleEffectExecutionOutcome::Failed);
		}

		TOptional<int32> RemainingTurns;
		int32 Layers = 1;
		TOptional<FBattleRandomDraw> DurationDraw;
		FBattleRandomContext RandomContext;
		RandomContext.BattleId = Request.BattleId;
		RandomContext.TurnId = Request.TurnId;
		RandomContext.ActionId = Request.ActionId;
		RandomContext.ResolutionId = Request.ResolutionId;
		RandomContext.RulePurpose = Effect.ConditionId.GetDefinitionId();
		if (Kind == EBattleVolatileKind::Confusion)
		{
			FBattleVolatileDurationResult Duration;
			if (!FBattleVolatileRules::TryRollConfusionDuration(
					RandomContext,
					Random,
					Duration))
			{
				SetRuntimeFailure(EBattleEffectExecutorError::RandomFailure);
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}
			RemainingTurns = Duration.Turns;
			DurationDraw = Duration.Draw;
		}
		else if (Kind == EBattleVolatileKind::PartialTrap)
		{
			FBattleVolatileDurationResult Duration;
			if (!FBattleVolatileRules::TryRollPartialTrapDuration(
					RandomContext,
					Random,
					Duration))
			{
				SetRuntimeFailure(EBattleEffectExecutorError::RandomFailure);
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}
			RemainingTurns = Duration.Turns;
			DurationDraw = Duration.Draw;
		}
		else if (Kind == EBattleVolatileKind::Taunt)
		{
			RemainingTurns = FBattleVolatileRules::GetTauntDuration(
				Facts.bTargetAlreadyActed);
		}
		else if (Kind == EBattleVolatileKind::Encore)
		{
			RemainingTurns = FBattleVolatileRules::GetEncoreDuration();
		}
		else if (Kind == EBattleVolatileKind::Disable)
		{
			RemainingTurns = FBattleVolatileRules::GetDisableDuration();
		}
		else if (Kind == EBattleVolatileKind::Substitute)
		{
			FBattleSubstituteCreationResult Creation;
			if (!FBattleVolatileRules::TryResolveSubstituteCreation(
					Battler.PermanentStats.MaxHP,
					Battler.CurrentHP,
					Creation)
				|| !Creation.bCanCreate)
			{
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}
			Layers = Creation.SubstituteHP;
		}

		FDefinitionId PayloadId = Effect.ConditionId.GetDefinitionId();
		if (Kind == EBattleVolatileKind::Encore || Kind == EBattleVolatileKind::Disable)
		{
			PayloadId = Battler.LastMoveId.GetDefinitionId();
		}
		FBattleTriggerSubject Source;
		TArray<FBattleTriggerSubject> Targets;
		if (!TryBuildVolatileSource(Effect.ConditionId, Source)
			|| !TryRegisterVolatile(
				Battler,
				Effect.ConditionId,
				PayloadId,
				Source,
				Targets,
				RemainingTurns,
				Layers))
		{
			return Outcome(EBattleEffectExecutionOutcome::Failed);
		}

		if (DurationDraw.IsSet() && !TryAppendRandomDraw(Target, DurationDraw.GetValue()))
		{
			SetRuntimeFailure(EBattleEffectExecutorError::InvalidHookResult);
			return Outcome(EBattleEffectExecutionOutcome::Failed);
		}
		if (Kind == EBattleVolatileKind::Substitute)
		{
			const int32 PreviousHP = Battler.CurrentHP;
			Battler.CurrentHP -= Layers;
			if (ExecutionResult == nullptr)
			{
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}
			FBattleEffectExecutionEvent& CostEvent =
				ExecutionResult->Events.AddDefaulted_GetRef();
			CostEvent.Type = EBattleEventType::HPChanged;
			CostEvent.Cause = EBattleEventCause::Move;
			CostEvent.Outcome = EBattleEffectExecutionOutcome::Applied;
			FBattleEventTarget EventTarget;
			if (!TryBuildEventTarget(Target, EventTarget))
			{
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}
			CostEvent.Targets.Add(MoveTemp(EventTarget));
			CostEvent.NumericBefore = PreviousHP;
			CostEvent.NumericAfter = Battler.CurrentHP;
			CostEvent.NumericDelta = -Layers;
		}

		FBattleEffectHookResult Result = Applied();
		Result.NumericBefore = 0;
		Result.NumericAfter = 1;
		Result.NumericDelta = 1;
		Result.bStateMutated = true;
		return Result;
	}

	FBattleEffectHookResult FStateExecutionContext::ApplySimpleSpecialVolatile(
		const FBattleMoveEffectDescriptor& Effect,
		const FBattleResolvedTarget& Target,
		const FConditionId& ExpectedId)
	{
		if (Effect.ConditionId != ExpectedId
			|| Target.GetKind() != EBattleResolvedTargetKind::Battler
			|| Target.GetBattler().BattlerId != Request.UserBattlerId)
		{
			return Outcome(EBattleEffectExecutionOutcome::Failed);
		}
		FBattleBattlerState* Battler = FindMutableBattler(Request.UserBattlerId);
		const FBattleConditionDefinition* Definition = State.Catalog.FindCondition(ExpectedId);
		if (Battler == nullptr
			|| Definition == nullptr
			|| Definition->Kind != EBattleConditionKind::Volatile
			|| HasVolatile(*Battler, ExpectedId))
		{
			return Outcome(EBattleEffectExecutionOutcome::Failed);
		}
		FBattleTriggerSubject Source;
		TArray<FBattleTriggerSubject> Targets;
		if (!FBattleTriggerSubject::TryCreateBattler(Request.UserBattlerId, Source)
			|| !TryRegisterVolatile(
				*Battler,
				ExpectedId,
				ExpectedId.GetDefinitionId(),
				Source,
				Targets,
				TOptional<int32>(),
				1))
		{
			return Outcome(EBattleEffectExecutionOutcome::Failed);
		}
		FBattleEffectHookResult Result = Applied();
		Result.NumericBefore = 0;
		Result.NumericAfter = 1;
		Result.NumericDelta = 1;
		Result.bStateMutated = true;
		return Result;
	}

	FBattleEffectHookResult FStateExecutionContext::ApplyCharge(
		const FBattleMoveEffectDescriptor& Effect,
		const FBattleResolvedTarget& Target)
	{
		if (Effect.ConditionId != FBattleVolatileRules::GetChargingId()
			|| Target.GetKind() != EBattleResolvedTargetKind::Battler
			|| Target.GetBattler().BattlerId != Request.UserBattlerId)
		{
			return Outcome(EBattleEffectExecutionOutcome::Failed);
		}
		FBattleBattlerState* Battler = FindMutableBattler(Request.UserBattlerId);
		const FBattleConditionDefinition* ChargeDefinition = State.Catalog.FindCondition(
			FBattleVolatileRules::GetChargingId());
		if (Battler == nullptr
			|| ChargeDefinition == nullptr
			|| ChargeDefinition->Kind != EBattleConditionKind::Volatile
			|| HasVolatile(*Battler, FBattleVolatileRules::GetChargingId()))
		{
			return Outcome(EBattleEffectExecutionOutcome::Failed);
		}

		const FBattleMoveEffectDescriptor* FlyDescriptor = Request.Move->Effects.FindByPredicate(
			[](const FBattleMoveEffectDescriptor& Candidate)
			{
				return Candidate.Kind == EBattleMoveEffectKind::SemiInvulnerability;
			});
		if (FlyDescriptor != nullptr)
		{
			const FBattleConditionDefinition* FlyDefinition = State.Catalog.FindCondition(
				FlyDescriptor->ConditionId);
			if (FlyDescriptor->ConditionId != FBattleVolatileRules::GetFlySemiInvulnerableId()
				|| FlyDefinition == nullptr
				|| FlyDefinition->Kind != EBattleConditionKind::Volatile
				|| HasVolatile(*Battler, FlyDescriptor->ConditionId))
			{
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}
		}

		FBattleTriggerSubject Source;
		TArray<FBattleTriggerSubject> LockedTargets;
		if (!FBattleTriggerSubject::TryCreateBattler(Request.UserBattlerId, Source))
		{
			return Outcome(EBattleEffectExecutionOutcome::Failed);
		}
		for (const FBattleResolvedTarget& Resolved : Request.Targets)
		{
			if (Resolved.GetKind() == EBattleResolvedTargetKind::Battler)
			{
				FBattleTriggerSubject TargetSlotSubject;
				FBattleTriggerSubject TargetBattlerSubject;
				if (!FBattleTriggerSubject::TryCreateActiveSlot(
						Resolved.GetBattler().ActiveSlotId,
						TargetSlotSubject)
					|| !FBattleTriggerSubject::TryCreateBattler(
						Resolved.GetBattler().BattlerId,
						TargetBattlerSubject))
				{
					return Outcome(EBattleEffectExecutionOutcome::Failed);
				}
				LockedTargets.Add(MoveTemp(TargetSlotSubject));
				LockedTargets.Add(MoveTemp(TargetBattlerSubject));
				break;
			}
		}

		const FBattleTriggerFramework FrameworkBefore = TriggerFramework;
		const TArray<FBattleConditionState> VolatilesBefore = Battler->Volatiles;
		const uint64 OrdinalBefore = NextConditionCreationOrdinal;
		if (!TryRegisterVolatile(
				*Battler,
				FBattleVolatileRules::GetChargingId(),
				Request.Move->Id.GetDefinitionId(),
				Source,
				LockedTargets,
				TOptional<int32>(),
				1)
			|| (FlyDescriptor != nullptr
				&& !TryRegisterVolatile(
					*Battler,
					FlyDescriptor->ConditionId,
					FlyDescriptor->ConditionId.GetDefinitionId(),
					Source,
					TArray<FBattleTriggerSubject>(),
					TOptional<int32>(),
					1)))
		{
			TriggerFramework = FrameworkBefore;
			Battler->Volatiles = VolatilesBefore;
			NextConditionCreationOrdinal = OrdinalBefore;
			return Outcome(EBattleEffectExecutionOutcome::Failed);
		}

		FBattleEffectHookResult Result = Applied();
		Result.NumericBefore = 0;
		Result.NumericAfter = 1;
		Result.NumericDelta = 1;
		Result.bStateMutated = true;
		Result.bDefersMove = true;
		return Result;
	}

	FBattleEffectHookResult FStateExecutionContext::ApplyProtect(
		const FBattleMoveEffectDescriptor& Effect,
		const FBattleResolvedTarget& Target)
	{
		if (Effect.ConditionId != FBattleVolatileRules::GetProtectId()
			|| Target.GetKind() != EBattleResolvedTargetKind::Battler
			|| Target.GetBattler().BattlerId != Request.UserBattlerId)
		{
			return Outcome(EBattleEffectExecutionOutcome::Failed);
		}
		FBattleBattlerState* Battler = FindMutableBattler(Request.UserBattlerId);
		const FBattleConditionDefinition* Definition = State.Catalog.FindCondition(
			Effect.ConditionId);
		if (Battler == nullptr
			|| Definition == nullptr
			|| Definition->Kind != EBattleConditionKind::Volatile)
		{
			return Outcome(EBattleEffectExecutionOutcome::Failed);
		}
		FBattleConditionState* Existing = FindMutableVolatile(*Battler, Effect.ConditionId);
		FBattleProtectAttemptFacts Facts;
		Facts.bHasQueuedAction = true;
		Facts.bConsecutiveEligibleUse = Existing != nullptr;
		Facts.ChainCounter = Existing != nullptr ? Existing->LayerCount : 0;
		FBattleRandomContext RandomContext;
		RandomContext.BattleId = Request.BattleId;
		RandomContext.TurnId = Request.TurnId;
		RandomContext.ActionId = Request.ActionId;
		RandomContext.ResolutionId = Request.ResolutionId;
		RandomContext.RulePurpose = FBattleVolatileRules::GetProtectConsecutiveUsePurpose();
		FBattleProtectAttemptResult Attempt;
		if (!FBattleVolatileRules::TryResolveProtectAttempt(
				Facts,
				RandomContext,
				Random,
				Attempt))
		{
			SetRuntimeFailure(EBattleEffectExecutorError::RandomFailure);
			return Outcome(EBattleEffectExecutionOutcome::Failed);
		}
		if (Attempt.bDrawConsumed && !TryAppendRandomDraw(Target, Attempt.Draw))
		{
			SetRuntimeFailure(EBattleEffectExecutorError::InvalidHookResult);
			return Outcome(EBattleEffectExecutionOutcome::Failed);
		}

		if (!Attempt.bSucceeded)
		{
			if (Existing != nullptr)
			{
				if (!TryCleanupVolatile(
						*Battler,
						Effect.ConditionId,
						EBattleTriggerCleanupReason::Removal))
				{
					return Outcome(EBattleEffectExecutionOutcome::Failed);
				}
				Battler->Volatiles.RemoveAll(
					[&Effect](const FBattleConditionState& Condition)
					{
						return Condition.ConditionId == Effect.ConditionId;
					});
			}
			return Outcome(EBattleEffectExecutionOutcome::Failed);
		}

		if (Existing == nullptr)
		{
			FBattleTriggerSubject Source;
			if (!FBattleTriggerSubject::TryCreateBattler(Request.UserBattlerId, Source)
				|| !TryRegisterVolatile(
					*Battler,
					Effect.ConditionId,
					Effect.ConditionId.GetDefinitionId(),
					Source,
					TArray<FBattleTriggerSubject>(),
					TOptional<int32>(),
					Attempt.NextChainCounter))
			{
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}
		}
		else
		{
			if (!TrySetVolatileLayers(
					Battler->BattlerId,
					Effect.ConditionId,
					Attempt.NextChainCounter)
				|| !TrySetVolatileSuppressed(
					Battler->BattlerId,
					Effect.ConditionId,
					false))
			{
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}
			Existing->LayerCount = Attempt.NextChainCounter;
		}

		FBattleEffectHookResult Result = Applied();
		Result.NumericBefore = Facts.bConsecutiveEligibleUse ? Facts.ChainCounter : 0;
		Result.NumericAfter = Attempt.NextChainCounter;
		Result.NumericDelta = Attempt.NextChainCounter - Result.NumericBefore.GetValue();
		Result.bStateMutated = true;
		return Result;
	}

	FBattleEffectHookResult FStateExecutionContext::ApplyCondition(
		const FBattleMoveEffectDescriptor& Effect,
		const FBattleResolvedTarget& Target)
	{
		if (Target.GetKind() != EBattleResolvedTargetKind::Battler)
		{
			return Outcome(EBattleEffectExecutionOutcome::Failed);
		}
		FBattleBattlerState* Battler = FindMutableBattler(Target.GetBattler().BattlerId);
		const FBattleConditionDefinition* Definition = State.Catalog.FindCondition(Effect.ConditionId);
		if (Battler == nullptr || Definition == nullptr)
		{
			return Outcome(EBattleEffectExecutionOutcome::Failed);
		}
		FBattleEffectHookResult Result = Applied();
		Result.NumericBefore = 0;
		Result.NumericAfter = 1;
		Result.NumericDelta = 1;
		Result.bStateMutated = true;
		if (Definition->Kind == EBattleConditionKind::MajorStatus)
		{
			if (Battler->MajorStatusId.IsValid())
			{
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}
			if (FBattleMajorStatusRules::IsCanonical(Effect.ConditionId))
			{
				TOptional<int32> SleepTurns;
				if (Effect.ConditionId == FBattleMajorStatusRules::GetSleepId())
				{
					FBattleRandomContext BaseContext;
					BaseContext.BattleId = Request.BattleId;
					BaseContext.TurnId = Request.TurnId;
					BaseContext.ActionId = Request.ActionId;
					BaseContext.ResolutionId = Request.ResolutionId;
					BaseContext.RulePurpose = FBattleMajorStatusRules::GetSleepDurationPurpose();
					FBattleSleepDurationResult Duration;
					if (!FBattleMajorStatusRules::TryRollSleepDuration(
							BaseContext,
							Random,
							Duration))
					{
						SetRuntimeFailure(EBattleEffectExecutorError::RandomFailure);
						return Outcome(EBattleEffectExecutionOutcome::Failed);
					}
					SleepTurns = Duration.Turns;
				}
				FBattleTriggerSubject Owner;
				EBattleTriggerError TriggerError = EBattleTriggerError::None;
				if (!FBattleTriggerSubject::TryCreateBattler(Battler->BattlerId, Owner)
					|| !FBattleMajorStatusRules::TryRegisterTriggers(
						TriggerFramework,
						Effect.ConditionId,
						Owner,
						SleepTurns,
						TriggerError))
				{
					return Outcome(EBattleEffectExecutionOutcome::Failed);
				}
				DrainTriggerOutputs();
			}
			Battler->MajorStatusId = Effect.ConditionId;
			return Result;
		}
		if (Definition->Kind == EBattleConditionKind::Volatile)
		{
			if (FBattleVolatileRules::IsCanonical(Effect.ConditionId))
			{
				return ApplyCanonicalVolatile(Effect, Target, *Battler);
			}
			if (Battler->Volatiles.ContainsByPredicate(
				[&Effect](const FBattleConditionState& Condition)
				{
					return Condition.ConditionId == Effect.ConditionId;
				}))
			{
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}
			Battler->Volatiles.Add(MakeConditionState(Effect));
			return Result;
		}
		return Outcome(EBattleEffectExecutionOutcome::Failed);
	}

	FBattleEffectHookResult FStateExecutionContext::ApplyStatStage(
		const FBattleMoveEffectDescriptor& Effect,
		const FBattleResolvedTarget& Target)
	{
		if (Target.GetKind() != EBattleResolvedTargetKind::Battler)
		{
			return Outcome(EBattleEffectExecutionOutcome::Failed);
		}
		FBattleBattlerState* Battler = FindMutableBattler(Target.GetBattler().BattlerId);
		if (Battler == nullptr)
		{
			return Outcome(EBattleEffectExecutionOutcome::Failed);
		}
		const FBattleStatStageChangeResult Change = Battler->Stages.ApplyChange(
			Effect.Stat,
			Effect.MagnitudeNumerator);
		FBattleEffectHookResult Result;
		Result.NumericBefore = Change.PreviousStage;
		Result.NumericAfter = Change.NewStage;
		Result.NumericDelta = Change.AppliedDelta;
		if (Change.Outcome == EBattleStatStageChangeOutcome::Blocked)
		{
			Result.Outcome = EBattleEffectExecutionOutcome::Capped;
			Result.bCapped = true;
			return Result;
		}
		if (Change.Outcome != EBattleStatStageChangeOutcome::Applied)
		{
			Result.Outcome = EBattleEffectExecutionOutcome::Failed;
			return Result;
		}
		Result.Outcome = EBattleEffectExecutionOutcome::Applied;
		Result.bStateMutated = true;
		Result.bCapped = Change.bClamped;
		return Result;
	}

	FBattleEffectHookResult FStateExecutionContext::SetFieldCondition(
		const FBattleMoveEffectDescriptor& Effect,
		const FBattleResolvedTarget& Target)
	{
		if (Target.GetKind() != EBattleResolvedTargetKind::Field)
		{
			return Outcome(EBattleEffectExecutionOutcome::Failed);
		}
		const FBattleConditionDefinition* Definition = State.Catalog.FindCondition(Effect.ConditionId);
		if (Definition == nullptr)
		{
			return Outcome(EBattleEffectExecutionOutcome::Failed);
		}
		if (FBattleFieldSideConditionRules::IsCanonical(Effect.ConditionId))
		{
			const EBattleConditionKind Family =
				FBattleFieldSideConditionRules::GetConditionFamily(Effect.ConditionId);
			if (Definition->Kind != Family
				|| (Family != EBattleConditionKind::Weather
					&& Family != EBattleConditionKind::Terrain
					&& Family != EBattleConditionKind::Room))
			{
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}
			FBattleFieldSideApplicationFacts Facts;
			Facts.RequestedConditionId = Effect.ConditionId;
			if (Family == EBattleConditionKind::Weather && Field.Weather.IsSet())
			{
				Facts.ExistingExclusiveConditionId = Field.Weather.GetValue().ConditionId;
			}
			else if (Family == EBattleConditionKind::Terrain && Field.Terrain.IsSet())
			{
				Facts.ExistingExclusiveConditionId = Field.Terrain.GetValue().ConditionId;
			}
			Facts.bRequestedAlreadyActive =
				(Family == EBattleConditionKind::Weather
					&& Field.Weather.IsSet()
					&& Field.Weather.GetValue().ConditionId == Effect.ConditionId)
				|| (Family == EBattleConditionKind::Terrain
					&& Field.Terrain.IsSet()
					&& Field.Terrain.GetValue().ConditionId == Effect.ConditionId)
				|| (Family == EBattleConditionKind::Room && HasRoom(Effect.ConditionId));
			Facts.bSnowActive = GetWeatherId()
				== FBattleFieldSideConditionRules::GetSnowId();
			Facts.bDurationExtensionActive = Effect.DurationTurns == 8;
			FBattleFieldSideApplicationResult Application;
			if (!FBattleFieldSideConditionRules::TryEvaluateApplication(Facts, Application))
			{
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}

			if (Application.Outcome == EBattleFieldSideApplicationOutcome::ToggleOff)
			{
				if (!TryCleanupFieldSideCondition(
						Effect.ConditionId,
						TOptional<EBattleSide>(),
						EBattleTriggerCleanupReason::Removal))
				{
					return Outcome(EBattleEffectExecutionOutcome::Failed);
				}
				Field.Rooms.RemoveAll(
					[&Effect](const FBattleConditionState& Condition)
					{
						return Condition.ConditionId == Effect.ConditionId;
					});
				if (Effect.ConditionId == FBattleFieldSideConditionRules::GetMagicRoomId())
				{
					if (!TrySetMagicRoomSuppression(false))
					{
						return Outcome(EBattleEffectExecutionOutcome::Failed);
					}
				}
				FBattleEffectHookResult Result = Applied();
				Result.NumericBefore = 1;
				Result.NumericAfter = 0;
				Result.NumericDelta = -1;
				Result.bStateMutated = true;
				return Result;
			}
			if (Application.Outcome != EBattleFieldSideApplicationOutcome::Create
				&& Application.Outcome
					!= EBattleFieldSideApplicationOutcome::ReplaceExclusive)
			{
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}

			if (Application.Outcome == EBattleFieldSideApplicationOutcome::ReplaceExclusive)
			{
				const FConditionId ExistingId = Family == EBattleConditionKind::Weather
					? Field.Weather.GetValue().ConditionId
					: Field.Terrain.GetValue().ConditionId;
				if (FBattleFieldSideConditionRules::IsCanonical(ExistingId)
					&& !TryCleanupFieldSideCondition(
						ExistingId,
						TOptional<EBattleSide>(),
						EBattleTriggerCleanupReason::Removal))
				{
					return Outcome(EBattleEffectExecutionOutcome::Failed);
				}
			}
			if (!TryRegisterFieldSideCondition(
					Effect.ConditionId,
					TOptional<EBattleSide>(),
					Application.DurationTurns,
					Application.Layers))
			{
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}
			FBattleConditionState NewCondition = MakeCanonicalConditionState(
				Effect.ConditionId,
				Application.DurationTurns,
				Application.Layers);
			if (Family == EBattleConditionKind::Weather)
			{
				Field.Weather = MoveTemp(NewCondition);
			}
			else if (Family == EBattleConditionKind::Terrain)
			{
				Field.Terrain = MoveTemp(NewCondition);
			}
			else
			{
				Field.Rooms.Add(MoveTemp(NewCondition));
			}
			if (Effect.ConditionId == FBattleFieldSideConditionRules::GetMagicRoomId())
			{
				if (!TrySetMagicRoomSuppression(true))
				{
					return Outcome(EBattleEffectExecutionOutcome::Failed);
				}
			}
			FBattleEffectHookResult Result = Applied();
			Result.NumericBefore = Application.Outcome
				== EBattleFieldSideApplicationOutcome::ReplaceExclusive ? 1 : 0;
			Result.NumericAfter = 1;
			Result.NumericDelta = Application.Outcome
				== EBattleFieldSideApplicationOutcome::ReplaceExclusive ? 0 : 1;
			Result.bStateMutated = true;
			return Result;
		}
		if (Definition->Kind == EBattleConditionKind::Weather)
		{
			if (Field.Weather.IsSet()
				&& Field.Weather.GetValue().ConditionId == Effect.ConditionId)
			{
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}
			if (Field.Weather.IsSet()
				&& FBattleFieldSideConditionRules::IsCanonical(
					Field.Weather.GetValue().ConditionId)
				&& !TryCleanupFieldSideCondition(
					Field.Weather.GetValue().ConditionId,
					TOptional<EBattleSide>(),
					EBattleTriggerCleanupReason::Removal))
			{
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}
			Field.Weather = MakeConditionState(Effect);
		}
		else if (Definition->Kind == EBattleConditionKind::Terrain)
		{
			if (Field.Terrain.IsSet()
				&& Field.Terrain.GetValue().ConditionId == Effect.ConditionId)
			{
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}
			if (Field.Terrain.IsSet()
				&& FBattleFieldSideConditionRules::IsCanonical(
					Field.Terrain.GetValue().ConditionId)
				&& !TryCleanupFieldSideCondition(
					Field.Terrain.GetValue().ConditionId,
					TOptional<EBattleSide>(),
					EBattleTriggerCleanupReason::Removal))
			{
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}
			Field.Terrain = MakeConditionState(Effect);
		}
		else if (Definition->Kind == EBattleConditionKind::Room)
		{
			if (Field.Rooms.ContainsByPredicate(
				[&Effect](const FBattleConditionState& Existing)
				{
					return Existing.ConditionId == Effect.ConditionId;
				}))
			{
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}
			Field.Rooms.Add(MakeConditionState(Effect));
		}
		else
		{
			return Outcome(EBattleEffectExecutionOutcome::Failed);
		}
		FBattleEffectHookResult Result = Applied();
		Result.NumericBefore = 0;
		Result.NumericAfter = 1;
		Result.NumericDelta = 1;
		Result.bStateMutated = true;
		return Result;
	}

	FBattleEffectHookResult FStateExecutionContext::SetSideCondition(
		const FBattleMoveEffectDescriptor& Effect,
		const FBattleResolvedTarget& Target)
	{
		if (Target.GetKind() != EBattleResolvedTargetKind::Side)
		{
			return Outcome(EBattleEffectExecutionOutcome::Failed);
		}
		FBattleSideState* Side = FindMutableSide(Target.GetSide());
		const FBattleConditionDefinition* Definition = State.Catalog.FindCondition(Effect.ConditionId);
		if (Side == nullptr || Definition == nullptr)
		{
			return Outcome(EBattleEffectExecutionOutcome::Failed);
		}
		TArray<FBattleConditionState>* Collection = Definition->Kind == EBattleConditionKind::Hazard
			? &Side->Hazards
			: &Side->Conditions;
		if (FBattleFieldSideConditionRules::IsCanonical(Effect.ConditionId))
		{
			if (Definition->Kind
				!= FBattleFieldSideConditionRules::GetConditionFamily(Effect.ConditionId))
			{
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}
			FBattleConditionState* Existing = Collection->FindByPredicate(
				[&Effect](const FBattleConditionState& Condition)
				{
					return Condition.ConditionId == Effect.ConditionId;
				});
			FBattleFieldSideApplicationFacts Facts;
			Facts.RequestedConditionId = Effect.ConditionId;
			Facts.bRequestedAlreadyActive = Existing != nullptr;
			Facts.ExistingLayers = Existing != nullptr ? Existing->LayerCount : 0;
			Facts.bSnowActive = GetWeatherId()
				== FBattleFieldSideConditionRules::GetSnowId();
			Facts.bDurationExtensionActive = Effect.DurationTurns == 8;
			FBattleFieldSideApplicationResult Application;
			if (!FBattleFieldSideConditionRules::TryEvaluateApplication(Facts, Application))
			{
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}
			if (Application.Outcome == EBattleFieldSideApplicationOutcome::AddLayer)
			{
				if (Existing == nullptr
					|| !TryUpdateFieldSideLayers(
						Effect.ConditionId,
						Target.GetSide(),
						Application.Layers))
				{
					return Outcome(EBattleEffectExecutionOutcome::Failed);
				}
				const int32 PreviousLayers = Existing->LayerCount;
				Existing->LayerCount = Application.Layers;
				FBattleEffectHookResult Result = Applied();
				Result.NumericBefore = PreviousLayers;
				Result.NumericAfter = Application.Layers;
				Result.NumericDelta = Application.Layers - PreviousLayers;
				Result.bStateMutated = true;
				return Result;
			}
			if (Application.Outcome != EBattleFieldSideApplicationOutcome::Create
				|| !TryRegisterFieldSideCondition(
					Effect.ConditionId,
					Target.GetSide(),
					Application.DurationTurns,
					Application.Layers))
			{
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}
			Collection->Add(MakeCanonicalConditionState(
				Effect.ConditionId,
				Application.DurationTurns,
				Application.Layers));
			FBattleEffectHookResult Result = Applied();
			Result.NumericBefore = 0;
			Result.NumericAfter = Application.Layers;
			Result.NumericDelta = Application.Layers;
			Result.bStateMutated = true;
			return Result;
		}
		if (Collection->ContainsByPredicate(
			[&Effect](const FBattleConditionState& Existing)
			{
				return Existing.ConditionId == Effect.ConditionId;
			}))
		{
			return Outcome(EBattleEffectExecutionOutcome::Failed);
		}
		Collection->Add(MakeConditionState(Effect));
		FBattleEffectHookResult Result = Applied();
		Result.NumericBefore = 0;
		Result.NumericAfter = 1;
		Result.NumericDelta = 1;
		Result.bStateMutated = true;
		return Result;
	}

	FBattleEffectHookResult FStateExecutionContext::RemoveCondition(
		const FBattleMoveEffectDescriptor& Effect,
		const FBattleResolvedTarget& Target)
	{
		const FBattleConditionDefinition* Definition = State.Catalog.FindCondition(Effect.ConditionId);
		if (Definition == nullptr)
		{
			return Outcome(EBattleEffectExecutionOutcome::Failed);
		}
		if (FBattleFieldSideConditionRules::IsCanonical(Effect.ConditionId))
		{
			TOptional<EBattleSide> SideOwner;
			if (Target.GetKind() == EBattleResolvedTargetKind::Side)
			{
				SideOwner = Target.GetSide();
			}
			if (!TryCleanupFieldSideCondition(
					Effect.ConditionId,
					SideOwner,
					EBattleTriggerCleanupReason::Removal))
			{
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}
		}
		bool bRemoved = false;
		if (Target.GetKind() == EBattleResolvedTargetKind::Battler)
		{
			FBattleBattlerState* Battler = FindMutableBattler(Target.GetBattler().BattlerId);
			if (Battler == nullptr)
			{
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}
			if (Definition->Kind == EBattleConditionKind::MajorStatus
				&& Battler->MajorStatusId == Effect.ConditionId)
			{
				if (FBattleMajorStatusRules::IsCanonical(Effect.ConditionId)
					&& !TryCleanupCanonicalStatus(*Battler))
				{
					return Outcome(EBattleEffectExecutionOutcome::Failed);
				}
				Battler->MajorStatusId = FConditionId();
				bRemoved = true;
			}
			else if (Definition->Kind == EBattleConditionKind::Volatile)
			{
				if (FBattleVolatileRules::IsCanonical(Effect.ConditionId)
					&& HasVolatile(*Battler, Effect.ConditionId)
					&& !TryCleanupVolatile(
						*Battler,
						Effect.ConditionId,
						EBattleTriggerCleanupReason::Removal))
				{
					return Outcome(EBattleEffectExecutionOutcome::Failed);
				}
				const int32 Removed = Battler->Volatiles.RemoveAll(
					[&Effect](const FBattleConditionState& Existing)
					{
						return Existing.ConditionId == Effect.ConditionId;
					});
				bRemoved = Removed > 0;
			}
		}
		else if (Target.GetKind() == EBattleResolvedTargetKind::Side)
		{
			FBattleSideState* Side = FindMutableSide(Target.GetSide());
			if (Side == nullptr)
			{
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}
			const int32 Removed = Side->Conditions.RemoveAll(
				[&Effect](const FBattleConditionState& Existing)
				{
					return Existing.ConditionId == Effect.ConditionId;
				}) + Side->Hazards.RemoveAll(
				[&Effect](const FBattleConditionState& Existing)
				{
					return Existing.ConditionId == Effect.ConditionId;
				});
			bRemoved = Removed > 0;
		}
		else if (Target.GetKind() == EBattleResolvedTargetKind::Field)
		{
			if (Field.Weather.IsSet()
				&& Field.Weather.GetValue().ConditionId == Effect.ConditionId)
			{
				Field.Weather.Reset();
				bRemoved = true;
			}
			if (Field.Terrain.IsSet()
				&& Field.Terrain.GetValue().ConditionId == Effect.ConditionId)
			{
				Field.Terrain.Reset();
				bRemoved = true;
			}
			bRemoved = Field.Rooms.RemoveAll(
				[&Effect](const FBattleConditionState& Existing)
				{
					return Existing.ConditionId == Effect.ConditionId;
				}) > 0 || bRemoved;
			bRemoved = Field.Effects.RemoveAll(
				[&Effect](const FBattleConditionState& Existing)
				{
					return Existing.ConditionId == Effect.ConditionId;
				}) > 0 || bRemoved;
		}

		if (!bRemoved)
		{
			return Outcome(EBattleEffectExecutionOutcome::Failed);
		}
		if (Effect.ConditionId == FBattleFieldSideConditionRules::GetMagicRoomId())
		{
			if (!TrySetMagicRoomSuppression(false))
			{
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}
		}
		FBattleEffectHookResult Result = Applied();
		Result.NumericBefore = 1;
		Result.NumericAfter = 0;
		Result.NumericDelta = -1;
		Result.bStateMutated = true;
		return Result;
	}
}
