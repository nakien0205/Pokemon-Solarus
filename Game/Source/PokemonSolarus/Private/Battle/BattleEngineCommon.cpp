#include "BattleEngineCommon.h"

#include "BattleEngineEvents.h"

namespace BattleEngineCommonPrivate
{
	using namespace BattleEngineEventsPrivate;

	FResolutionId TakeResolutionId(FBattleEngineState& State)
	{
		FResolutionId Id;
		const bool bCreated = State.NextResolutionId > 0
			&& FResolutionId::TryCreate(State.NextResolutionId, Id);
		check(bCreated);
		++State.NextResolutionId;
		return Id;
	}

	FActionId TakeActionId(FBattleEngineState& State)
	{
		FActionId Id;
		const bool bCreated = State.NextActionId > 0
			&& FActionId::TryCreate(State.NextActionId, Id);
		check(bCreated);
		++State.NextActionId;
		return Id;
	}

	bool IsHeldItemActive(const FBattleBattlerState& Battler)
	{
		return Battler.HeldItem.CurrentItemId.IsValid()
			&& !Battler.HeldItem.bConsumed
			&& !Battler.HeldItem.bTemporarilyRemoved;
	}

	const FBattleConditionState* FindVolatile(
		const FBattleBattlerState& Battler,
		const FConditionId& VolatileId)
	{
		return Battler.Volatiles.FindByPredicate(
			[&VolatileId](const FBattleConditionState& Condition)
			{
				return Condition.ConditionId == VolatileId;
			});
	}

	FBattleConditionState* FindMutableVolatile(
		FBattleBattlerState& Battler,
		const FConditionId& VolatileId)
	{
		return Battler.Volatiles.FindByPredicate(
			[&VolatileId](const FBattleConditionState& Condition)
			{
				return Condition.ConditionId == VolatileId;
			});
	}

	bool HasVolatile(
		const FBattleBattlerState& Battler,
		const FConditionId& VolatileId)
	{
		return FindVolatile(Battler, VolatileId) != nullptr;
	}

	FBattleConditionState* FindMutableFieldSideCondition(
		FBattleEngineState& State,
		const FBattleTriggerSubject& Owner,
		const FConditionId& ConditionId)
	{
		if (!FBattleFieldSideConditionRules::IsCanonical(ConditionId))
		{
			return nullptr;
		}
		if (Owner.Kind == EBattleTriggerSubjectKind::Field)
		{
			if (State.Field.Weather.IsSet()
				&& State.Field.Weather.GetValue().ConditionId == ConditionId)
			{
				return &State.Field.Weather.GetValue();
			}
			if (State.Field.Terrain.IsSet()
				&& State.Field.Terrain.GetValue().ConditionId == ConditionId)
			{
				return &State.Field.Terrain.GetValue();
			}
			return State.Field.Rooms.FindByPredicate(
				[&ConditionId](const FBattleConditionState& Condition)
				{
					return Condition.ConditionId == ConditionId;
				});
		}
		if (Owner.Kind != EBattleTriggerSubjectKind::Side || !Owner.bHasSide)
		{
			return nullptr;
		}
		FBattleSideState* Side = FindMutableSide(State, Owner.Side);
		if (Side == nullptr)
		{
			return nullptr;
		}
		FBattleConditionState* Condition = Side->Conditions.FindByPredicate(
			[&ConditionId](const FBattleConditionState& Candidate)
			{
				return Candidate.ConditionId == ConditionId;
			});
		return Condition != nullptr
			? Condition
			: Side->Hazards.FindByPredicate(
				[&ConditionId](const FBattleConditionState& Candidate)
				{
					return Candidate.ConditionId == ConditionId;
				});
	}

	bool RemoveFieldSideConditionState(
		FBattleEngineState& State,
		const FBattleTriggerSubject& Owner,
		const FConditionId& ConditionId)
	{
		if (Owner.Kind == EBattleTriggerSubjectKind::Field)
		{
			if (State.Field.Weather.IsSet()
				&& State.Field.Weather.GetValue().ConditionId == ConditionId)
			{
				State.Field.Weather.Reset();
				return true;
			}
			if (State.Field.Terrain.IsSet()
				&& State.Field.Terrain.GetValue().ConditionId == ConditionId)
			{
				State.Field.Terrain.Reset();
				return true;
			}
			return State.Field.Rooms.RemoveAll(
				[&ConditionId](const FBattleConditionState& Condition)
				{
					return Condition.ConditionId == ConditionId;
				}) == 1;
		}
		if (Owner.Kind != EBattleTriggerSubjectKind::Side || !Owner.bHasSide)
		{
			return false;
		}
		FBattleSideState* Side = FindMutableSide(State, Owner.Side);
		if (Side == nullptr)
		{
			return false;
		}
		const int32 RemovedConditions = Side->Conditions.RemoveAll(
			[&ConditionId](const FBattleConditionState& Condition)
			{
				return Condition.ConditionId == ConditionId;
			});
		const int32 RemovedHazards = Side->Hazards.RemoveAll(
			[&ConditionId](const FBattleConditionState& Condition)
			{
				return Condition.ConditionId == ConditionId;
			});
		return RemovedConditions + RemovedHazards == 1;
	}

	bool IsVolatileSuppressed(
		const FBattleEngineState& State,
		const FBattlerId BattlerId,
		const FConditionId& VolatileId)
	{
		bool bFound = false;
		for (const FBattleTriggerRegistrationState& Registration :
			State.TriggerFramework.GetActiveRegistrations())
		{
			if (Registration.Spec.Owner.Kind == EBattleTriggerSubjectKind::Battler
				&& Registration.Spec.Owner.BattlerId == BattlerId
				&& Registration.Spec.SourceDefinition.Kind
					== EBattleTriggerSourceDefinitionKind::Condition
				&& Registration.Spec.SourceDefinition.ConditionId == VolatileId)
			{
				bFound = true;
				if (!Registration.bSuppressed)
				{
					return false;
				}
			}
		}
		return bFound;
	}

	bool TryGetChargingTargetSlot(
		const FBattleEngineState& State,
		const FBattlerId BattlerId,
		FActiveSlotId& OutTargetSlotId)
	{
		OutTargetSlotId = FActiveSlotId();
		for (const FBattleTriggerRegistrationState& Registration :
			State.TriggerFramework.GetActiveRegistrations())
		{
			if (Registration.Spec.Owner.Kind == EBattleTriggerSubjectKind::Battler
				&& Registration.Spec.Owner.BattlerId == BattlerId
				&& Registration.Spec.SourceDefinition.Kind
					== EBattleTriggerSourceDefinitionKind::Condition
				&& Registration.Spec.SourceDefinition.ConditionId
					== FBattleVolatileRules::GetChargingId())
			{
				const FBattleTriggerSubject* Target = Registration.Spec.Targets.FindByPredicate(
					[](const FBattleTriggerSubject& Subject)
					{
						return Subject.Kind == EBattleTriggerSubjectKind::ActiveSlot;
					});
				if (Target != nullptr)
				{
					OutTargetSlotId = Target->ActiveSlotId;
					return OutTargetSlotId.IsValid();
				}
			}
		}
		return false;
	}

	bool TryGetChargingTargetBattler(
		const FBattleEngineState& State,
		const FBattlerId BattlerId,
		FBattlerId& OutTargetBattlerId)
	{
		OutTargetBattlerId = FBattlerId();
		for (const FBattleTriggerRegistrationState& Registration :
			State.TriggerFramework.GetActiveRegistrations())
		{
			if (Registration.Spec.Owner.Kind == EBattleTriggerSubjectKind::Battler
				&& Registration.Spec.Owner.BattlerId == BattlerId
				&& Registration.Spec.SourceDefinition.Kind
					== EBattleTriggerSourceDefinitionKind::Condition
				&& Registration.Spec.SourceDefinition.ConditionId
					== FBattleVolatileRules::GetChargingId())
			{
				const FBattleTriggerSubject* Target = Registration.Spec.Targets.FindByPredicate(
					[](const FBattleTriggerSubject& Subject)
					{
						return Subject.Kind == EBattleTriggerSubjectKind::Battler;
					});
				if (Target != nullptr)
				{
					OutTargetBattlerId = Target->BattlerId;
					return OutTargetBattlerId.IsValid();
				}
			}
		}
		return false;
	}

	bool IsReleasingCharge(
		const FBattleEngineState& State,
		const FBattleBattlerState& Battler,
		const FMoveId MoveId)
	{
		FMoveId StoredMoveId;
		return HasVolatile(Battler, FBattleVolatileRules::GetChargingId())
			&& TryGetVolatilePayloadMoveId(
				State,
				Battler.BattlerId,
				FBattleVolatileRules::GetChargingId(),
				StoredMoveId)
			&& StoredMoveId == MoveId;
	}

	FBattleEventSource SourceFromRequest(
		const FBattleEngineState& State,
		const FBattleDecisionRequest* Request,
		const FBattleDecision* Decision)
	{
		FBattleEventSource Source = FindFallbackSource(State);
		if (Request != nullptr && Request->IsValid())
		{
			Source.TrainerId = Request->GetDecisionOwnerTrainerId();
			Source.BattlerId = Request->GetActingBattlerId();
			Source.ActiveSlotId = Request->GetActingSlotId();
		}
		else if (Decision != nullptr && Decision->IsValid())
		{
			Source.TrainerId = Decision->GetDecisionOwnerTrainerId();
			Source.BattlerId = Decision->GetActingBattlerId();
		}
		return Source;
	}

	FBattleResolution MakeRejectedResolution(
		FBattleEngineState& State,
		const FResolutionId ResolutionId,
		const FBattleRejection& Rejection,
		const EBattleEventType EventType,
		const EBattleEventCause Cause,
		const EBattleActionKind ActionKind,
		const FBattleEventSource& Source)
	{
		FBattleResolutionSpec Spec;
		Spec.ResolutionId = ResolutionId;
		Spec.BeforeStateVersion = State.StateVersion;
		Spec.AfterStateVersion = State.StateVersion;
		Spec.bAccepted = false;
		Spec.Rejection = Rejection;
		Spec.Events.Add(MakeEvent(
			State,
			ResolutionId,
			FActionId(),
			EventType,
			Cause,
			ActionKind,
			EBattleOutcomeCause::None,
			Source));

		FBattleResolution Resolution;
		const bool bCreated = FBattleResolution::TryCreate(Spec, Resolution);
		check(bCreated);
		State.AppendResolution(Resolution);
		return Resolution;
	}

	bool ActiveSlotLess(const FActiveSlotId& Left, const FActiveSlotId& Right)
	{
		if (Left.GetSide() != Right.GetSide())
		{
			return static_cast<uint8>(Left.GetSide()) < static_cast<uint8>(Right.GetSide());
		}
		return static_cast<uint8>(Left.GetPosition()) < static_cast<uint8>(Right.GetPosition());
	}

	bool IsLivingSelectableBattler(const FBattleBattlerState* Battler)
	{
		return Battler != nullptr
			&& !Battler->bEgg
			&& !Battler->bFainted
			&& !Battler->bCaptured
			&& !Battler->bRemoved;
	}

	const FBattleBattlerState* FindLeftmostLivingWildOpponent(
		const FBattleEngineState& State)
	{
		for (const EBattlePosition Position : {EBattlePosition::Left, EBattlePosition::Right})
		{
			FActiveSlotId SlotId;
			const bool bSlotCreated = FActiveSlotId::TryCreate(
				EBattleSide::Opponent,
				Position,
				SlotId);
			check(bSlotCreated);
			const FBattleActivePositionState* Active = State.FindActivePosition(SlotId);
			const FBattleBattlerState* Battler = Active != nullptr
				? State.FindBattler(Active->BattlerId)
				: nullptr;
			const FBattleTrainerState* Trainer = Battler != nullptr
				? State.FindTrainer(Battler->TrainerId)
				: nullptr;
			if (Active != nullptr
				&& Active->bAvailable
				&& Trainer != nullptr
				&& Trainer->Side == EBattleSide::Opponent
				&& Trainer->Role == EBattleTrainerRole::Opponent
				&& IsLivingSelectableBattler(Battler))
			{
				return Battler;
			}
		}
		return nullptr;
	}

	const FBattleWildFleePolicyState* FindWildFleePolicy(
		const FBattleEngineState& State,
		const FBattleBattlerState& Battler)
	{
		const FDefinitionId TriggerId = FBattleWildFleeRules::GetActionSelectionTriggerId();
		const FDefinitionId EligibilityId =
			FBattleWildFleeRules::GetActiveLivingWildEligibilityId();
		const auto IsSupportedPolicy = [TriggerId, EligibilityId](
			const FBattleWildFleePolicyState& Policy)
		{
			return Policy.TriggerId == TriggerId
				&& Policy.EligibilityId == EligibilityId;
		};

		const FBattleWildFleePolicyState* Exact = State.WildFleePolicies.FindByPredicate(
			[&Battler, &IsSupportedPolicy](const FBattleWildFleePolicyState& Policy)
			{
				return Policy.SpeciesFormId == Battler.SpeciesFormId
					&& IsSupportedPolicy(Policy);
			});
		if (Exact != nullptr)
		{
			return Exact;
		}
		return State.WildFleePolicies.FindByPredicate(
			[&IsSupportedPolicy](const FBattleWildFleePolicyState& Policy)
			{
				return !Policy.SpeciesFormId.IsValid() && IsSupportedPolicy(Policy);
			});
	}

	bool CanOfferRunAction(
		const FBattleEngineState& State,
		const FBattleTrainerState& Trainer,
		const FBattleBattlerState& Battler)
	{
		const FBattleBattlerState* WildOpponent = FindLeftmostLivingWildOpponent(State);
		const FBattleTrainerEncounterPolicy* TrainerPolicy =
			FindTrainerEncounterPolicy(State, Trainer.TrainerId);
		return TrainerPolicy != nullptr
			&& TrainerPolicy->bMayRun
			&& IsLivingSelectableBattler(&Battler)
			&& FindActiveForBattler(State, Battler.BattlerId) != nullptr
			&& WildOpponent != nullptr
			&& FBattleRunRules::IsSpeedPairLegal(
				Battler.PermanentStats.Speed,
				WildOpponent->PermanentStats.Speed);
	}

	bool CanOfferWildFleeAction(
		const FBattleEngineState& State,
		const FBattleTrainerState& Trainer,
		const FBattleBattlerState& Battler)
	{
		const FBattleTrainerEncounterPolicy* TrainerPolicy =
			FindTrainerEncounterPolicy(State, Trainer.TrainerId);
		return State.CompiledEncounterPolicies.IsWildFleeConfigured()
			&& TrainerPolicy != nullptr
			&& TrainerPolicy->Side == EBattleSide::Opponent
			&& TrainerPolicy->Role == EBattleTrainerRole::Opponent
			&& IsLivingSelectableBattler(&Battler)
			&& FindActiveForBattler(State, Battler.BattlerId) != nullptr
			&& FindWildFleePolicy(State, Battler) != nullptr;
	}
}
