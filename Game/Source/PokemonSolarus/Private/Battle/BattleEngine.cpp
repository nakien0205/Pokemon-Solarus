#include "Battle/BattleEngine.h"
#include "Battle/BattleEffectExecutor.h"
#include "Battle/BattleFieldSideConditions.h"
#include "Battle/BattleFaintOutcomeResolver.h"
#include "Battle/BattleMajorStatus.h"
#include "Battle/BattleState.h"
#include "Battle/BattleStatCalculator.h"
#include "Battle/BattleSwitching.h"
#include "Battle/BattleVolatile.h"
#include "Math/NumericLimits.h"

namespace
{
	const FBattleActivePositionState* FindActiveForBattler(
		const FBattleEngineState& State,
		FBattlerId BattlerId);

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

	bool TryTakeTriggerOperationContext(
		FBattleEngineState& State,
		FBattleTriggerOperationContext& OutContext)
	{
		OutContext = FBattleTriggerOperationContext();
		if (State.NextTriggerReentrancyToken == 0
			|| State.NextTriggerReentrancyToken == TNumericLimits<uint64>::Max()
			|| !FBattleTriggerReentrancyToken::TryCreate(
				State.NextTriggerReentrancyToken,
				OutContext.ReentrancyToken))
		{
			return false;
		}
		++State.NextTriggerReentrancyToken;
		return true;
	}

	void DrainTriggerOutputs(FBattleEngineState& State)
	{
		TArray<FBattleTriggerEffectRequest> IgnoredRequests;
		TArray<FBattleTriggerLifecycleFact> IgnoredFacts;
		State.TriggerFramework.DrainEffectRequests(IgnoredRequests);
		State.TriggerFramework.DrainLifecycleFacts(IgnoredFacts);
	}

	bool TryMakeBattlerTriggerSubject(
		const FBattlerId BattlerId,
		FBattleTriggerSubject& OutOwner)
	{
		return FBattleTriggerSubject::TryCreateBattler(BattlerId, OutOwner);
	}

	bool TryDispatchBattlerStatusPhase(
		FBattleEngineState& State,
		const FBattleBattlerState& Battler,
		const EBattleTriggerPhase Phase,
		const bool bTickDuration,
		const TOptional<int32>& EffectiveSpeed,
		TArray<FBattleTriggerEffectRequest>& OutRequests,
		TArray<FBattleTriggerLifecycleFact>& OutFacts)
	{
		OutRequests.Reset();
		OutFacts.Reset();
		if (!FBattleMajorStatusRules::IsCanonical(Battler.MajorStatusId))
		{
			return true;
		}
		FBattleTriggerSubject Owner;
		FBattleTriggerSourceDefinition SourceDefinition;
		FBattleTriggerOperationContext Operation;
		if (!TryMakeBattlerTriggerSubject(Battler.BattlerId, Owner)
			|| !FBattleTriggerSourceDefinition::TryCreateCondition(
				Battler.MajorStatusId,
				SourceDefinition)
			|| !TryTakeTriggerOperationContext(State, Operation))
		{
			return false;
		}

		FBattleTriggerDispatchSpec Dispatch;
		Dispatch.Phase = Phase;
		Dispatch.ReentrancyToken = Operation.ReentrancyToken;
		Dispatch.OrderPolicy.bUseEffectiveSpeed = EffectiveSpeed.IsSet();
		if (bTickDuration)
		{
			Dispatch.DurationTickOwners.Add(Owner);
		}
		for (const FBattleTriggerRegistrationState& Registration :
			State.TriggerFramework.GetActiveRegistrations())
		{
			if (Registration.Spec.SourceDefinition == SourceDefinition
				&& Registration.Spec.Owner == Owner
				&& Registration.Spec.Rule.Phase == Phase)
			{
				FBattleTriggerDispatchParticipant& Participant =
					Dispatch.Participants.AddDefaulted_GetRef();
				Participant.RegistrationId = Registration.RegistrationId;
				Participant.EffectiveSpeed = EffectiveSpeed;
				const FBattleActivePositionState* Active = State.ActivePositions.FindByPredicate(
					[&Battler](const FBattleActivePositionState& Candidate)
					{
						return Candidate.BattlerId == Battler.BattlerId;
					});
				if (Active != nullptr)
				{
					Participant.ActiveSlotId = Active->ActiveSlotId;
				}
			}
		}
		if (Dispatch.Participants.IsEmpty())
		{
			return true;
		}

		EBattleTriggerError Error = EBattleTriggerError::None;
		FBattleTriggerDispatchResult Result;
		if (!State.TriggerFramework.TryEnqueueDispatch(Dispatch, Error)
			|| !State.TriggerFramework.TryResolveNextDispatch(Result, Error))
		{
			return false;
		}
		State.TriggerFramework.DrainEffectRequests(OutRequests);
		State.TriggerFramework.DrainLifecycleFacts(OutFacts);
		if (Result.bQueuedExpiryDispatch)
		{
			FBattleTriggerDispatchResult ExpiryResult;
			if (!State.TriggerFramework.TryResolveNextDispatch(ExpiryResult, Error))
			{
				return false;
			}
			TArray<FBattleTriggerEffectRequest> ExpiryRequests;
			TArray<FBattleTriggerLifecycleFact> ExpiryFacts;
			State.TriggerFramework.DrainEffectRequests(ExpiryRequests);
			State.TriggerFramework.DrainLifecycleFacts(ExpiryFacts);
			OutRequests.Append(MoveTemp(ExpiryRequests));
			OutFacts.Append(MoveTemp(ExpiryFacts));
		}
		return true;
	}

	bool TryCleanupMajorStatusTriggers(
		FBattleEngineState& State,
		const FConditionId& StatusId,
		const FBattlerId BattlerId,
		const EBattleTriggerCleanupReason Reason)
	{
		if (!FBattleMajorStatusRules::IsCanonical(StatusId))
		{
			return true;
		}
		FBattleTriggerSubject Owner;
		FBattleTriggerOperationContext Operation;
		EBattleTriggerError Error = EBattleTriggerError::None;
		if (!TryMakeBattlerTriggerSubject(BattlerId, Owner)
			|| !TryTakeTriggerOperationContext(State, Operation)
			|| !FBattleMajorStatusRules::TryCleanupTriggers(
				State.TriggerFramework,
				StatusId,
				Owner,
				Reason,
				Operation,
				Error))
		{
			return false;
		}
		DrainTriggerOutputs(State);
		return true;
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

	bool HasFieldRoom(
		const FBattleEngineState& State,
		const FConditionId& RoomId)
	{
		return State.Field.Rooms.ContainsByPredicate(
			[&RoomId](const FBattleConditionState& Condition)
			{
				return Condition.ConditionId == RoomId;
			});
	}

	const FBattleSideState* FindSide(
		const FBattleEngineState& State,
		const EBattleSide Side)
	{
		return State.Sides.FindByPredicate(
			[Side](const FBattleSideState& Candidate)
			{
				return Candidate.Side == Side;
			});
	}

	FBattleSideState* FindMutableSide(
		FBattleEngineState& State,
		const EBattleSide Side)
	{
		return State.Sides.FindByPredicate(
			[Side](const FBattleSideState& Candidate)
			{
				return Candidate.Side == Side;
			});
	}

	bool HasSideCondition(
		const FBattleEngineState& State,
		const EBattleSide Side,
		const FConditionId& ConditionId)
	{
		const FBattleSideState* SideState = FindSide(State, Side);
		return SideState != nullptr && SideState->Conditions.ContainsByPredicate(
			[&ConditionId](const FBattleConditionState& Condition)
			{
				return Condition.ConditionId == ConditionId;
			});
	}

	const FBattleConditionState* FindFieldSideCondition(
		const FBattleEngineState& State,
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
		const FBattleSideState* Side = FindSide(State, Owner.Side);
		if (Side == nullptr)
		{
			return nullptr;
		}
		const FBattleConditionState* Condition = Side->Conditions.FindByPredicate(
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

	bool TryDispatchFieldSidePhase(
		FBattleEngineState& State,
		const FBattleTriggerSubject& Owner,
		const EBattleTriggerPhase Phase,
		const FConditionId& FilterConditionId,
		const TOptional<FActiveSlotId>& ActiveSlotId,
		TArray<FBattleTriggerEffectRequest>& OutRequests,
		TArray<FBattleTriggerLifecycleFact>& OutFacts)
	{
		OutRequests.Reset();
		OutFacts.Reset();
		if (!Owner.IsValid()
			|| (Owner.Kind != EBattleTriggerSubjectKind::Field
				&& Owner.Kind != EBattleTriggerSubjectKind::Side))
		{
			return false;
		}

		FBattleTriggerDispatchSpec Dispatch;
		Dispatch.Phase = Phase;
		for (const FBattleTriggerRegistrationState& Registration :
			State.TriggerFramework.GetActiveRegistrations())
		{
			if (Registration.Spec.Owner != Owner
				|| Registration.Spec.Rule.Phase != Phase
				|| Registration.Spec.SourceDefinition.Kind
					!= EBattleTriggerSourceDefinitionKind::Condition)
			{
				continue;
			}
			const FConditionId& ConditionId =
				Registration.Spec.SourceDefinition.ConditionId;
			if (!FBattleFieldSideConditionRules::IsCanonical(ConditionId)
				|| (FilterConditionId.IsValid() && ConditionId != FilterConditionId)
				|| FindFieldSideCondition(State, Owner, ConditionId) == nullptr)
			{
				continue;
			}
			FBattleTriggerDispatchParticipant& Participant =
				Dispatch.Participants.AddDefaulted_GetRef();
			Participant.RegistrationId = Registration.RegistrationId;
			Participant.ActiveSlotId = ActiveSlotId;
		}
		if (Dispatch.Participants.IsEmpty())
		{
			return true;
		}

		FBattleTriggerOperationContext Operation;
		EBattleTriggerError Error = EBattleTriggerError::None;
		FBattleTriggerDispatchResult Result;
		if (!TryTakeTriggerOperationContext(State, Operation))
		{
			return false;
		}
		Dispatch.ReentrancyToken = Operation.ReentrancyToken;
		if (!State.TriggerFramework.TryEnqueueDispatch(Dispatch, Error)
			|| !State.TriggerFramework.TryResolveNextDispatch(Result, Error))
		{
			return false;
		}
		State.TriggerFramework.DrainEffectRequests(OutRequests);
		State.TriggerFramework.DrainLifecycleFacts(OutFacts);
		if (Result.bQueuedExpiryDispatch)
		{
			FBattleTriggerDispatchResult ExpiryResult;
			if (!State.TriggerFramework.TryResolveNextDispatch(ExpiryResult, Error))
			{
				return false;
			}
			TArray<FBattleTriggerEffectRequest> ExpiryRequests;
			TArray<FBattleTriggerLifecycleFact> ExpiryFacts;
			State.TriggerFramework.DrainEffectRequests(ExpiryRequests);
			State.TriggerFramework.DrainLifecycleFacts(ExpiryFacts);
			OutRequests.Append(MoveTemp(ExpiryRequests));
			OutFacts.Append(MoveTemp(ExpiryFacts));
		}
		return true;
	}

	bool TryIsFieldSideConditionActiveForPhase(
		FBattleEngineState& State,
		const FConditionId& ConditionId,
		const TOptional<EBattleSide>& Side,
		const EBattleTriggerPhase Phase,
		const TOptional<FActiveSlotId>& ActiveSlotId,
		bool& bOutActive)
	{
		bOutActive = false;
		FBattleTriggerSubject Owner;
		if (FBattleFieldSideConditionRules::IsFieldOwned(ConditionId))
		{
			if (Side.IsSet())
			{
				return false;
			}
			Owner = FBattleTriggerSubject::CreateField();
		}
		else if (!Side.IsSet()
			|| !FBattleTriggerSubject::TryCreateSide(Side.GetValue(), Owner))
		{
			return false;
		}
		TArray<FBattleTriggerEffectRequest> Requests;
		TArray<FBattleTriggerLifecycleFact> Facts;
		if (!TryDispatchFieldSidePhase(
				State,
				Owner,
				Phase,
				ConditionId,
				ActiveSlotId,
				Requests,
				Facts))
		{
			return false;
		}
		bOutActive = Requests.ContainsByPredicate(
			[&ConditionId, Phase](const FBattleTriggerEffectRequest& Request)
			{
				return Request.Phase == Phase
					&& Request.SourceDefinition.Kind
						== EBattleTriggerSourceDefinitionKind::Condition
					&& Request.SourceDefinition.ConditionId == ConditionId;
			});
		return true;
	}

	FBattleEventSource BuildFieldSideConditionSource(
		const FBattleEngineState& State,
		const FBattleTriggerSubject& Owner,
		const FConditionId& ConditionId)
	{
		FBattleEventSource Source;
		Source.DefinitionId = ConditionId.GetDefinitionId();
		const FBattleConditionState* Condition = FindFieldSideCondition(
			State,
			Owner,
			ConditionId);
		const FBattleBattlerState* SourceBattler = Condition != nullptr
			? State.FindBattler(Condition->SourceBattlerId)
			: nullptr;
		if (SourceBattler == nullptr)
		{
			return Source;
		}

		Source.TrainerId = SourceBattler->TrainerId;
		Source.BattlerId = SourceBattler->BattlerId;
		const FBattleActivePositionState* SourceActive =
			State.ActivePositions.FindByPredicate(
				[SourceBattler](const FBattleActivePositionState& Position)
				{
					return Position.BattlerId == SourceBattler->BattlerId;
				});
		if (SourceActive != nullptr)
		{
			Source.ActiveSlotId = SourceActive->ActiveSlotId;
		}
		return Source;
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

	bool TryResolveGrounded(
		const FBattleEngineState& State,
		const FBattleBattlerState& Battler,
		bool& bOutGrounded)
	{
		const FBattleSpeciesFormDefinition* Species = State.Catalog.FindSpeciesForm(
			Battler.SpeciesFormId);
		if (Species == nullptr)
		{
			bOutGrounded = false;
			return false;
		}
		FBattleGroundedFacts Facts;
		Facts.PrimaryType = Species->PrimaryType;
		Facts.SecondaryType = Species->SecondaryType;
		Facts.bAirborneSemiInvulnerable = HasVolatile(
			Battler,
			FBattleVolatileRules::GetFlySemiInvulnerableId());
		return FBattleFieldSideConditionRules::TryResolveGrounded(Facts, bOutGrounded);
	}

	bool TryCleanupFieldSideTriggers(
		FBattleEngineState& State,
		const FConditionId& ConditionId,
		const TOptional<EBattleSide>& Side,
		const EBattleTriggerCleanupReason Reason)
	{
		if (!FBattleFieldSideConditionRules::IsCanonical(ConditionId))
		{
			return true;
		}
		FBattleTriggerSubject Owner;
		if (FBattleFieldSideConditionRules::IsFieldOwned(ConditionId))
		{
			if (Side.IsSet())
			{
				return false;
			}
			Owner = FBattleTriggerSubject::CreateField();
		}
		else if (!Side.IsSet()
			|| !FBattleTriggerSubject::TryCreateSide(Side.GetValue(), Owner))
		{
			return false;
		}
		FBattleTriggerOperationContext Operation;
		EBattleTriggerError Error = EBattleTriggerError::None;
		if (!Owner.IsValid()
			|| !TryTakeTriggerOperationContext(State, Operation)
			|| !FBattleFieldSideConditionRules::TryCleanupTriggers(
				State.TriggerFramework,
				ConditionId,
				Owner,
				Reason,
				Operation,
				Error))
		{
			return false;
		}
		DrainTriggerOutputs(State);
		return true;
	}

	bool TryCleanupVolatileTriggers(
		FBattleEngineState& State,
		const FConditionId& VolatileId,
		const FBattlerId BattlerId,
		const EBattleTriggerCleanupReason Reason)
	{
		if (!FBattleVolatileRules::IsCanonical(VolatileId))
		{
			return true;
		}
		FBattleTriggerSubject Owner;
		FBattleTriggerOperationContext Operation;
		EBattleTriggerError Error = EBattleTriggerError::None;
		if (!TryMakeBattlerTriggerSubject(BattlerId, Owner)
			|| !TryTakeTriggerOperationContext(State, Operation)
			|| !FBattleVolatileRules::TryCleanupTriggers(
				State.TriggerFramework,
				VolatileId,
				Owner,
				Reason,
				Operation,
				Error))
		{
			return false;
		}
		DrainTriggerOutputs(State);
		return true;
	}

	bool TryCleanupAllOwnedVolatileTriggers(
		FBattleEngineState& State,
		const FBattleBattlerState& Battler,
		const EBattleTriggerCleanupReason Reason)
	{
		TArray<FConditionId> Ids;
		for (const FBattleConditionState& Condition : Battler.Volatiles)
		{
			if (FBattleVolatileRules::IsCanonical(Condition.ConditionId))
			{
				Ids.Add(Condition.ConditionId);
			}
		}
		for (const FConditionId& Id : Ids)
		{
			if (!TryCleanupVolatileTriggers(State, Id, Battler.BattlerId, Reason))
			{
				return false;
			}
		}
		return true;
	}

	bool TryCleanupSourceDependentVolatiles(
		FBattleEngineState& State,
		const FBattlerId SourceBattlerId,
		const EBattleTriggerCleanupReason Reason)
	{
		for (FBattleBattlerState& Candidate : State.Battlers)
		{
			TArray<FConditionId> ToRemove;
			for (const FBattleConditionState& Condition : Candidate.Volatiles)
			{
				if (Condition.SourceBattlerId == SourceBattlerId
					&& (Condition.ConditionId == FBattleVolatileRules::GetPartialTrapId()
						|| Condition.ConditionId == FBattleVolatileRules::GetTrapId()))
				{
					ToRemove.Add(Condition.ConditionId);
				}
			}
			for (const FConditionId& Id : ToRemove)
			{
				if (!TryCleanupVolatileTriggers(State, Id, Candidate.BattlerId, Reason))
				{
					return false;
				}
				Candidate.Volatiles.RemoveAll(
					[&Id](const FBattleConditionState& Condition)
					{
						return Condition.ConditionId == Id;
					});
			}
		}
		return true;
	}

	bool TrySetVolatileLayers(
		FBattleEngineState& State,
		const FBattlerId BattlerId,
		const FConditionId& VolatileId,
		const int32 Layers)
	{
		FBattleTriggerOperationContext Operation;
		if (Layers <= 0 || !TryTakeTriggerOperationContext(State, Operation))
		{
			return false;
		}
		EBattleTriggerError Error = EBattleTriggerError::None;
		const TArray<FBattleTriggerRegistrationState> Registrations =
			State.TriggerFramework.GetActiveRegistrations();
		for (const FBattleTriggerRegistrationState& Registration : Registrations)
		{
			if (Registration.Spec.Owner.Kind == EBattleTriggerSubjectKind::Battler
				&& Registration.Spec.Owner.BattlerId == BattlerId
				&& Registration.Spec.SourceDefinition.Kind
					== EBattleTriggerSourceDefinitionKind::Condition
				&& Registration.Spec.SourceDefinition.ConditionId == VolatileId
				&& !State.TriggerFramework.TryUpdateLayers(
					Registration.RegistrationId,
					Layers,
					Operation,
					Error))
			{
				return false;
			}
		}
		DrainTriggerOutputs(State);
		return true;
	}

	bool TrySetVolatileSuppressed(
		FBattleEngineState& State,
		const FBattlerId BattlerId,
		const FConditionId& VolatileId,
		const bool bSuppressed)
	{
		FBattleTriggerOperationContext Operation;
		if (!TryTakeTriggerOperationContext(State, Operation))
		{
			return false;
		}
		EBattleTriggerError Error = EBattleTriggerError::None;
		const TArray<FBattleTriggerRegistrationState> Registrations =
			State.TriggerFramework.GetActiveRegistrations();
		for (const FBattleTriggerRegistrationState& Registration : Registrations)
		{
			if (Registration.Spec.Owner.Kind == EBattleTriggerSubjectKind::Battler
				&& Registration.Spec.Owner.BattlerId == BattlerId
				&& Registration.Spec.SourceDefinition.Kind
					== EBattleTriggerSourceDefinitionKind::Condition
				&& Registration.Spec.SourceDefinition.ConditionId == VolatileId
				&& !State.TriggerFramework.TrySetSuppressed(
					Registration.RegistrationId,
					bSuppressed,
					Operation,
					Error))
			{
				return false;
			}
		}
		DrainTriggerOutputs(State);
		return true;
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

	bool TryGetVolatilePayloadMoveId(
		const FBattleEngineState& State,
		const FBattlerId BattlerId,
		const FConditionId& VolatileId,
		FMoveId& OutMoveId)
	{
		OutMoveId = FMoveId();
		for (const FBattleTriggerRegistrationState& Registration :
			State.TriggerFramework.GetActiveRegistrations())
		{
			if (Registration.Spec.Owner.Kind == EBattleTriggerSubjectKind::Battler
				&& Registration.Spec.Owner.BattlerId == BattlerId
				&& Registration.Spec.SourceDefinition.Kind
					== EBattleTriggerSourceDefinitionKind::Condition
				&& Registration.Spec.SourceDefinition.ConditionId == VolatileId)
			{
				return FMoveId::TryCreate(
					Registration.Spec.Rule.PayloadId,
					OutMoveId);
			}
		}
		return false;
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

	bool TryClearChargeState(
		FBattleEngineState& State,
		const FBattlerId BattlerId,
		const EBattleTriggerCleanupReason Reason)
	{
		FBattleBattlerState* Battler = State.FindMutableBattler(BattlerId);
		if (Battler == nullptr)
		{
			return false;
		}
		for (const FConditionId& Id : {
			FBattleVolatileRules::GetChargingId(),
			FBattleVolatileRules::GetFlySemiInvulnerableId()})
		{
			if (!HasVolatile(*Battler, Id))
			{
				continue;
			}
			if (!TryCleanupVolatileTriggers(State, Id, BattlerId, Reason))
			{
				return false;
			}
			Battler->Volatiles.RemoveAll(
				[&Id](const FBattleConditionState& Condition)
				{
					return Condition.ConditionId == Id;
				});
		}
		return true;
	}

	bool TryDispatchBattlerVolatilePhase(
		FBattleEngineState& State,
		const FBattleBattlerState& Battler,
		const EBattleTriggerPhase Phase,
		const bool bTickDuration,
		TArray<FBattleTriggerEffectRequest>& OutRequests,
		TArray<FBattleTriggerLifecycleFact>& OutFacts,
		const FConditionId FilterVolatileId = FConditionId())
	{
		OutRequests.Reset();
		OutFacts.Reset();
		FBattleTriggerSubject Owner;
		if (!TryMakeBattlerTriggerSubject(Battler.BattlerId, Owner))
		{
			return false;
		}
		FBattleTriggerDispatchSpec Dispatch;
		Dispatch.Phase = Phase;
		if (bTickDuration)
		{
			Dispatch.DurationTickOwners.Add(Owner);
		}
		for (const FBattleTriggerRegistrationState& Registration :
			State.TriggerFramework.GetActiveRegistrations())
		{
			if (Registration.Spec.Owner == Owner
				&& Registration.Spec.Rule.Phase == Phase
				&& Registration.Spec.SourceDefinition.Kind
					== EBattleTriggerSourceDefinitionKind::Condition
				&& FBattleVolatileRules::IsCanonical(
					Registration.Spec.SourceDefinition.ConditionId)
				&& (!FilterVolatileId.IsValid()
					|| Registration.Spec.SourceDefinition.ConditionId == FilterVolatileId)
				&& HasVolatile(
					Battler,
					Registration.Spec.SourceDefinition.ConditionId))
			{
				FBattleTriggerDispatchParticipant& Participant =
					Dispatch.Participants.AddDefaulted_GetRef();
				Participant.RegistrationId = Registration.RegistrationId;
				const FBattleActivePositionState* Active = State.ActivePositions.FindByPredicate(
					[&Battler](const FBattleActivePositionState& Position)
					{
						return Position.BattlerId == Battler.BattlerId;
					});
				if (Active != nullptr)
				{
					Participant.ActiveSlotId = Active->ActiveSlotId;
				}
			}
		}
		if (Dispatch.Participants.IsEmpty())
		{
			return true;
		}
		FBattleTriggerOperationContext Operation;
		if (!TryTakeTriggerOperationContext(State, Operation))
		{
			return false;
		}
		Dispatch.ReentrancyToken = Operation.ReentrancyToken;
		EBattleTriggerError Error = EBattleTriggerError::None;
		FBattleTriggerDispatchResult Result;
		if (!State.TriggerFramework.TryEnqueueDispatch(Dispatch, Error)
			|| !State.TriggerFramework.TryResolveNextDispatch(Result, Error))
		{
			return false;
		}
		State.TriggerFramework.DrainEffectRequests(OutRequests);
		State.TriggerFramework.DrainLifecycleFacts(OutFacts);
		if (Result.bQueuedExpiryDispatch)
		{
			FBattleTriggerDispatchResult ExpiryResult;
			if (!State.TriggerFramework.TryResolveNextDispatch(ExpiryResult, Error))
			{
				return false;
			}
			TArray<FBattleTriggerEffectRequest> ExpiryRequests;
			TArray<FBattleTriggerLifecycleFact> ExpiryFacts;
			State.TriggerFramework.DrainEffectRequests(ExpiryRequests);
			State.TriggerFramework.DrainLifecycleFacts(ExpiryFacts);
			OutRequests.Append(MoveTemp(ExpiryRequests));
			OutFacts.Append(MoveTemp(ExpiryFacts));
		}
		return true;
	}

	bool TryCleanupBattleEndTriggers(FBattleEngineState& State)
	{
		FBattleTriggerOperationContext Operation;
		if (!TryTakeTriggerOperationContext(State, Operation))
		{
			return false;
		}
		FBattleTriggerCleanupRequest Cleanup;
		Cleanup.Reason = EBattleTriggerCleanupReason::BattleEnd;
		Cleanup.Context = Operation;
		EBattleTriggerError Error = EBattleTriggerError::None;
		if (!State.TriggerFramework.TryApplyCleanup(Cleanup, Error))
		{
			return false;
		}
		DrainTriggerOutputs(State);
		return true;
	}

	bool TrySetToxicLayers(
		FBattleEngineState& State,
		const FBattlerId BattlerId,
		const int32 NewLayerEncoding,
		const FBattleTriggerOperationContext& Operation)
	{
		FBattleTriggerSubject Owner;
		FBattleTriggerSourceDefinition SourceDefinition;
		if (!TryMakeBattlerTriggerSubject(BattlerId, Owner)
			|| !FBattleTriggerSourceDefinition::TryCreateCondition(
				FBattleMajorStatusRules::GetToxicId(),
				SourceDefinition))
		{
			return false;
		}
		EBattleTriggerError Error = EBattleTriggerError::None;
		for (const FBattleTriggerRegistrationState& Registration :
			State.TriggerFramework.GetActiveRegistrations())
		{
			if (Registration.Spec.SourceDefinition == SourceDefinition
				&& Registration.Spec.Owner == Owner
				&& !State.TriggerFramework.TryUpdateLayers(
					Registration.RegistrationId,
					NewLayerEncoding,
					Operation,
					Error))
			{
				return false;
			}
		}
		return true;
	}

	bool TryRunToxicSwitchOut(
		FBattleEngineState& State,
		const FBattleBattlerState& Battler)
	{
		if (Battler.MajorStatusId != FBattleMajorStatusRules::GetToxicId())
		{
			return true;
		}
		TArray<FBattleTriggerEffectRequest> Requests;
		TArray<FBattleTriggerLifecycleFact> Facts;
		if (!TryDispatchBattlerStatusPhase(
			State,
			Battler,
			EBattleTriggerPhase::SwitchOut,
			false,
			TOptional<int32>(),
			Requests,
			Facts))
		{
			return false;
		}
		FBattleTriggerOperationContext Operation;
		if (!TryTakeTriggerOperationContext(State, Operation)
			|| !TrySetToxicLayers(
				State,
				Battler.BattlerId,
				FBattleMajorStatusRules::GetResetToxicLayerEncoding(),
				Operation))
		{
			return false;
		}
		DrainTriggerOutputs(State);
		return true;
	}

	FBattleEventSource FindFallbackSource(const FBattleEngineState& State)
	{
		FBattleEventSource Source;
		const FBattleTrainerState* PlayerTrainer = State.Trainers.FindByPredicate(
			[](const FBattleTrainerState& Trainer)
			{
				return Trainer.Role == EBattleTrainerRole::Player;
			});
		if (PlayerTrainer != nullptr)
		{
			Source.TrainerId = PlayerTrainer->TrainerId;
		}
		const FBattleActivePositionState* PlayerLeft = State.ActivePositions.FindByPredicate(
			[](const FBattleActivePositionState& Position)
			{
				return Position.ActiveSlotId.GetSide() == EBattleSide::Player
					&& Position.ActiveSlotId.GetPosition() == EBattlePosition::Left;
			});
		if (PlayerLeft != nullptr)
		{
			Source.TrainerId = PlayerLeft->TrainerId;
			Source.BattlerId = PlayerLeft->BattlerId;
			Source.ActiveSlotId = PlayerLeft->ActiveSlotId;
		}
		return Source;
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

	FBattleEventSource SourceFromLockedAction(
		const FBattleEngineState& State,
		const FBattleLockedActionState& Action)
	{
		FBattleEventSource Source = FindFallbackSource(State);
		Source.TrainerId = Action.Decision.GetDecisionOwnerTrainerId();
		Source.BattlerId = Action.Decision.GetActingBattlerId();
		Source.ActiveSlotId = Action.OrderKey.ActingSlotId;
		if (Action.Decision.GetActionKind() == EBattleActionKind::Fight)
		{
			Source.DefinitionId = Action.Decision.GetMoveId().GetDefinitionId();
		}
		else if (Action.Decision.GetActionKind() == EBattleActionKind::Bag)
		{
			Source.DefinitionId = Action.Decision.GetItemId().GetDefinitionId();
		}
		return Source;
	}

	FBattleEvent MakeEvent(
		FBattleEngineState& State,
		const FResolutionId ResolutionId,
		const FActionId ActionId,
		const EBattleEventType Type,
		const EBattleEventCause Cause,
		const EBattleActionKind ActionKind,
		const EBattleOutcomeCause OutcomeCause,
		const FBattleEventSource& Source)
	{
		FBattleEventSpec Spec;
		Spec.EventOrdinal = State.NextEventOrdinal;
		Spec.BattleId = State.Setup.GetBattleId();
		Spec.TurnId = State.TurnId;
		Spec.ActionId = ActionId;
		Spec.ResolutionId = ResolutionId;
		Spec.Type = Type;
		Spec.Cause = Cause;
		Spec.CauseActionKind = ActionKind;
		Spec.OutcomeCause = OutcomeCause;
		Spec.Source = Source;
		Spec.Visibility.Level = EBattleVisibilityLevel::Public;

		FBattleEvent Event;
		const bool bCreated = FBattleEvent::TryCreate(Spec, Event);
		check(bCreated);
		++State.NextEventOrdinal;
		return Event;
	}

	FBattleEvent MakeActionOrderLockedEvent(
		FBattleEngineState& State,
		const FResolutionId ResolutionId,
		const FBattleLockedActionState& Action)
	{
		FBattleEventSpec Spec;
		Spec.EventOrdinal = State.NextEventOrdinal;
		Spec.BattleId = State.Setup.GetBattleId();
		Spec.TurnId = State.TurnId;
		Spec.ActionId = Action.ActionId;
		Spec.ResolutionId = ResolutionId;
		Spec.Type = EBattleEventType::ActionOrderLocked;
		Spec.Cause = EBattleEventCause::Action;
		Spec.CauseActionKind = Action.Decision.GetActionKind();
		Spec.Source = SourceFromLockedAction(State, Action);
		Spec.ActionOrder = FBattleActionOrderMetadata{
			Action.QueueOrdinal,
			Action.OrderKey,
			State.bLockedOrderReversesSpeed
		};
		Spec.Visibility.Level = EBattleVisibilityLevel::CoreOnly;

		FBattleEvent Event;
		const bool bCreated = FBattleEvent::TryCreate(Spec, Event);
		check(bCreated);
		++State.NextEventOrdinal;
		return Event;
	}

	FBattleEvent MakeActionDetailEvent(
		FBattleEngineState& State,
		const FResolutionId ResolutionId,
		const FBattleLockedActionState& Action,
		const EBattleEventType Type,
		const EBattleEventCause Cause,
		const TOptional<int64> NumericBefore = TOptional<int64>(),
		const TOptional<int64> NumericAfter = TOptional<int64>(),
		const TOptional<int64> NumericDelta = TOptional<int64>(),
		const EBattleVisibilityLevel Visibility = EBattleVisibilityLevel::Public)
	{
		FBattleEventSpec Spec;
		Spec.EventOrdinal = State.NextEventOrdinal;
		Spec.BattleId = State.Setup.GetBattleId();
		Spec.TurnId = State.TurnId;
		Spec.ActionId = Action.ActionId;
		Spec.ResolutionId = ResolutionId;
		Spec.Type = Type;
		Spec.Cause = Cause;
		Spec.CauseActionKind = Action.Decision.GetActionKind();
		Spec.Source = SourceFromLockedAction(State, Action);
		Spec.NumericBefore = NumericBefore;
		Spec.NumericAfter = NumericAfter;
		Spec.NumericDelta = NumericDelta;
		Spec.Visibility.Level = Visibility;

		FBattleEvent Event;
		const bool bCreated = FBattleEvent::TryCreate(Spec, Event);
		check(bCreated);
		++State.NextEventOrdinal;
		return Event;
	}

	FBattleEvent MakeBattleEngineTargetsResolvedEvent(
		FBattleEngineState& State,
		const FResolutionId ResolutionId,
		const FBattleLockedActionState& Action,
		const FBattleTargetResolutionResult& TargetResolution)
	{
		FBattleEventSpec Spec;
		Spec.EventOrdinal = State.NextEventOrdinal;
		Spec.BattleId = State.Setup.GetBattleId();
		Spec.TurnId = State.TurnId;
		Spec.ActionId = Action.ActionId;
		Spec.ResolutionId = ResolutionId;
		Spec.Type = EBattleEventType::TargetsResolved;
		Spec.Cause = EBattleEventCause::Targeting;
		Spec.CauseActionKind = EBattleActionKind::Fight;
		Spec.Source = SourceFromLockedAction(State, Action);
		Spec.TargetResolution = FBattleTargetResolutionMetadata{
			TargetResolution.TargetClass,
			TargetResolution.bWasRedirected,
			TargetResolution.bUsedFaintedTargetFallback
		};
		Spec.Visibility.Level = EBattleVisibilityLevel::Public;

		for (const FBattleResolvedTarget& Target : TargetResolution.Targets)
		{
			FBattleEventTarget EventTarget;
			switch (Target.GetKind())
			{
			case EBattleResolvedTargetKind::Battler:
			{
				const FBattleBattlerTarget& BattlerTarget = Target.GetBattler();
				const FBattleBattlerState* Battler = State.FindBattler(BattlerTarget.BattlerId);
				check(Battler != nullptr);
				if (Battler != nullptr)
				{
					EventTarget.TrainerId = Battler->TrainerId;
				}
				EventTarget.BattlerId = BattlerTarget.BattlerId;
				EventTarget.ActiveSlotId = BattlerTarget.ActiveSlotId;
				break;
			}
			case EBattleResolvedTargetKind::Side:
				EventTarget.Side = Target.GetSide();
				EventTarget.bHasSide = true;
				break;
			case EBattleResolvedTargetKind::Field:
				EventTarget.bField = true;
				break;
			default:
				checkNoEntry();
				break;
			}
			Spec.Targets.Add(MoveTemp(EventTarget));
		}

		FBattleEvent Event;
		const bool bCreated = FBattleEvent::TryCreate(Spec, Event);
		check(bCreated);
		++State.NextEventOrdinal;
		return Event;
	}

	FBattleEvent MakeBattleEffectEvent(
		FBattleEngineState& State,
		const FResolutionId ResolutionId,
		const FBattleLockedActionState& Action,
		const FBattleEffectExecutionEvent& Record,
		const TOptional<uint64> SimultaneousGroupId = TOptional<uint64>())
	{
		FBattleEventSpec Spec;
		Spec.EventOrdinal = State.NextEventOrdinal;
		Spec.BattleId = State.Setup.GetBattleId();
		Spec.TurnId = State.TurnId;
		Spec.ActionId = Action.ActionId;
		Spec.ResolutionId = ResolutionId;
		Spec.Type = Record.Type;
		Spec.Cause = Record.Cause;
		Spec.CauseActionKind = EBattleActionKind::Fight;
		Spec.Source = Record.SourceOverride.IsSet()
			? Record.SourceOverride.GetValue()
			: SourceFromLockedAction(State, Action);
		Spec.Targets = Record.Targets;
		Spec.NumericBefore = Record.NumericBefore;
		Spec.NumericAfter = Record.NumericAfter;
		Spec.NumericDelta = Record.NumericDelta;
		Spec.SimultaneousGroupId = SimultaneousGroupId;
		Spec.HitIndex = Record.HitIndex;
		Spec.HitCount = Record.HitCount;
		Spec.Visibility.Level = EBattleVisibilityLevel::Public;

		FBattleEvent Event;
		const bool bCreated = FBattleEvent::TryCreate(Spec, Event);
		check(bCreated);
		++State.NextEventOrdinal;
		return Event;
	}

	FBattleEvent MakeTargetedActionEvent(
		FBattleEngineState& State,
		const FResolutionId ResolutionId,
		const FBattleLockedActionState& Action,
		const EBattleEventType Type,
		const EBattleEventCause Cause,
		const FBattleEventTarget& Target,
		const EBattleOutcomeCause OutcomeCause = EBattleOutcomeCause::None,
		const TOptional<uint64> SimultaneousGroupId = TOptional<uint64>(),
		const TOptional<uint16> HitIndex = TOptional<uint16>(),
		const TOptional<uint16> HitCount = TOptional<uint16>(),
		const FBattleEventSource* SourceOverride = nullptr)
	{
		FBattleEventSpec Spec;
		Spec.EventOrdinal = State.NextEventOrdinal;
		Spec.BattleId = State.Setup.GetBattleId();
		Spec.TurnId = State.TurnId;
		Spec.ActionId = Action.ActionId;
		Spec.ResolutionId = ResolutionId;
		Spec.Type = Type;
		Spec.Cause = Cause;
		Spec.CauseActionKind = Action.Decision.GetActionKind();
		Spec.OutcomeCause = OutcomeCause;
		Spec.Source = SourceOverride != nullptr
			? *SourceOverride
			: SourceFromLockedAction(State, Action);
		Spec.Targets.Add(Target);
		Spec.SimultaneousGroupId = SimultaneousGroupId;
		Spec.HitIndex = HitIndex;
		Spec.HitCount = HitCount;
		Spec.Visibility.Level = EBattleVisibilityLevel::Public;

		FBattleEvent Event;
		const bool bCreated = FBattleEvent::TryCreate(Spec, Event);
		check(bCreated);
		++State.NextEventOrdinal;
		return Event;
	}

	FBattleEvent MakeTargetedActionlessEvent(
		FBattleEngineState& State,
		const FResolutionId ResolutionId,
		const EBattleEventType Type,
		const EBattleEventCause Cause,
		const EBattleActionKind ActionKind,
		const FBattleEventSource& Source,
		const FBattleEventTarget& Target)
	{
		FBattleEventSpec Spec;
		Spec.EventOrdinal = State.NextEventOrdinal;
		Spec.BattleId = State.Setup.GetBattleId();
		Spec.TurnId = State.TurnId;
		Spec.ResolutionId = ResolutionId;
		Spec.Type = Type;
		Spec.Cause = Cause;
		Spec.CauseActionKind = ActionKind;
		Spec.Source = Source;
		Spec.Targets.Add(Target);
		Spec.Visibility.Level = EBattleVisibilityLevel::Public;

		FBattleEvent Event;
		const bool bCreated = FBattleEvent::TryCreate(Spec, Event);
		check(bCreated);
		++State.NextEventOrdinal;
		return Event;
	}

	FBattleEvent MakeResidualMutationEvent(
		FBattleEngineState& State,
		const FResolutionId ResolutionId,
		const EBattleEventType Type,
		const FBattleEventSource& Source,
		const FBattleEventTarget& Target,
		const int64 NumericBefore,
		const int64 NumericAfter,
		const int64 NumericDelta)
	{
		FBattleEventSpec Spec;
		Spec.EventOrdinal = State.NextEventOrdinal;
		Spec.BattleId = State.Setup.GetBattleId();
		Spec.TurnId = State.TurnId;
		Spec.ResolutionId = ResolutionId;
		Spec.Type = Type;
		Spec.Cause = EBattleEventCause::Rule;
		Spec.CauseActionKind = EBattleActionKind::Residual;
		Spec.Source = Source;
		Spec.Targets.Add(Target);
		Spec.NumericBefore = NumericBefore;
		Spec.NumericAfter = NumericAfter;
		Spec.NumericDelta = NumericDelta;
		Spec.Visibility.Level = EBattleVisibilityLevel::Public;

		FBattleEvent Event;
		const bool bCreated = FBattleEvent::TryCreate(Spec, Event);
		check(bCreated);
		++State.NextEventOrdinal;
		return Event;
	}

	FBattleEvent MakeRuleMutationEvent(
		FBattleEngineState& State,
		const FResolutionId ResolutionId,
		const EBattleEventType Type,
		const EBattleActionKind ActionKind,
		const FBattleEventSource& Source,
		const FBattleEventTarget& Target,
		const int64 NumericBefore,
		const int64 NumericAfter,
		const int64 NumericDelta)
	{
		FBattleEventSpec Spec;
		Spec.EventOrdinal = State.NextEventOrdinal;
		Spec.BattleId = State.Setup.GetBattleId();
		Spec.TurnId = State.TurnId;
		Spec.ResolutionId = ResolutionId;
		Spec.Type = Type;
		Spec.Cause = EBattleEventCause::Rule;
		Spec.CauseActionKind = ActionKind;
		Spec.Source = Source;
		Spec.Targets.Add(Target);
		Spec.NumericBefore = NumericBefore;
		Spec.NumericAfter = NumericAfter;
		Spec.NumericDelta = NumericDelta;
		Spec.Visibility.Level = EBattleVisibilityLevel::Public;
		FBattleEvent Event;
		const bool bCreated = FBattleEvent::TryCreate(Spec, Event);
		check(bCreated);
		++State.NextEventOrdinal;
		return Event;
	}

	void AppendActionlessSwitchTransitionEvents(
		FBattleEngineState& State,
		const FResolutionId ResolutionId,
		const FBattleEventSource& Source,
		const FBattleEventTarget& Outgoing,
		const FBattleEventTarget& Incoming,
		TArray<FBattleEvent>& Events)
	{
		Events.Add(MakeTargetedActionlessEvent(
			State,
			ResolutionId,
			EBattleEventType::LeftActiveSlot,
			EBattleEventCause::Switch,
			EBattleActionKind::Switch,
			Source,
			Outgoing));
		Events.Add(MakeTargetedActionlessEvent(
			State,
			ResolutionId,
			EBattleEventType::SwitchTransientStateCleared,
			EBattleEventCause::Rule,
			EBattleActionKind::Switch,
			Source,
			Outgoing));
		Events.Add(MakeTargetedActionlessEvent(
			State,
			ResolutionId,
			EBattleEventType::EnteredActiveSlot,
			EBattleEventCause::Switch,
			EBattleActionKind::Switch,
			Source,
			Incoming));
		Events.Add(MakeTargetedActionlessEvent(
			State,
			ResolutionId,
			EBattleEventType::Switched,
			EBattleEventCause::Rule,
			EBattleActionKind::Switch,
			Source,
			Incoming));
	}

	void AppendSwitchTransitionEvents(
		FBattleEngineState& State,
		const FResolutionId ResolutionId,
		const FBattleLockedActionState& Action,
		const FBattleEventTarget& Outgoing,
		const FBattleEventTarget& Incoming,
		TArray<FBattleEvent>& Events)
	{
		Events.Add(MakeTargetedActionEvent(
			State,
			ResolutionId,
			Action,
			EBattleEventType::LeftActiveSlot,
			EBattleEventCause::Switch,
			Outgoing));
		Events.Add(MakeTargetedActionEvent(
			State,
			ResolutionId,
			Action,
			EBattleEventType::SwitchTransientStateCleared,
			EBattleEventCause::Rule,
			Outgoing));
		Events.Add(MakeTargetedActionEvent(
			State,
			ResolutionId,
			Action,
			EBattleEventType::EnteredActiveSlot,
			EBattleEventCause::Switch,
			Incoming));
		Events.Add(MakeTargetedActionEvent(
			State,
			ResolutionId,
			Action,
			EBattleEventType::Switched,
			EBattleEventCause::Rule,
			Incoming));
	}

	FBattleEvent MakeBattleEndedEvent(
		FBattleEngineState& State,
		const FResolutionId ResolutionId,
		const FBattleLockedActionState& Action,
		const EBattleOutcomeCause OutcomeCause,
		const FBattleEventSource* SourceOverride = nullptr)
	{
		FBattleEventSpec Spec;
		Spec.EventOrdinal = State.NextEventOrdinal;
		Spec.BattleId = State.Setup.GetBattleId();
		Spec.TurnId = State.TurnId;
		Spec.ActionId = Action.ActionId;
		Spec.ResolutionId = ResolutionId;
		Spec.Type = EBattleEventType::BattleEnded;
		Spec.Cause = EBattleEventCause::Outcome;
		Spec.CauseActionKind = Action.Decision.GetActionKind();
		Spec.OutcomeCause = OutcomeCause;
		Spec.Source = SourceOverride != nullptr
			? *SourceOverride
			: SourceFromLockedAction(State, Action);
		Spec.Visibility.Level = EBattleVisibilityLevel::Public;

		FBattleEvent Event;
		const bool bCreated = FBattleEvent::TryCreate(Spec, Event);
		check(bCreated);
		++State.NextEventOrdinal;
		return Event;
	}

	bool TryResolveEntryHazards(
		FBattleEngineState& State,
		const FBattlerId IncomingBattlerId,
		const FActiveSlotId ActiveSlotId,
		const FResolutionId ResolutionId,
		TArray<FBattleEvent>& Events)
	{
		FBattleBattlerState* Incoming = State.FindMutableBattler(IncomingBattlerId);
		const FBattleActivePositionState* Active = State.FindActivePosition(ActiveSlotId);
		FBattleSideState* Side = FindMutableSide(State, ActiveSlotId.GetSide());
		const FBattleSpeciesFormDefinition* Species = Incoming != nullptr
			? State.Catalog.FindSpeciesForm(Incoming->SpeciesFormId)
			: nullptr;
		if (Incoming == nullptr
			|| Active == nullptr
			|| Active->BattlerId != IncomingBattlerId
			|| Side == nullptr
			|| Species == nullptr)
		{
			return false;
		}

		FBattleTriggerSubject SideOwner;
		TArray<FBattleTriggerEffectRequest> HazardRequests;
		TArray<FBattleTriggerLifecycleFact> HazardFacts;
		if (!FBattleTriggerSubject::TryCreateSide(ActiveSlotId.GetSide(), SideOwner)
			|| !TryDispatchFieldSidePhase(
				State,
				SideOwner,
				EBattleTriggerPhase::SwitchIn,
				FConditionId(),
				ActiveSlotId,
				HazardRequests,
				HazardFacts))
		{
			return false;
		}
		for (const FBattleTriggerEffectRequest& HazardRequest : HazardRequests)
		{
			if (Incoming->CurrentHP <= 0 || Incoming->bFainted || State.Phase == EBattlePhase::Terminal)
			{
				break;
			}
			if (HazardRequest.Phase != EBattleTriggerPhase::SwitchIn
				|| HazardRequest.SourceDefinition.Kind
					!= EBattleTriggerSourceDefinitionKind::Condition)
			{
				return false;
			}
			const FConditionId HazardId = HazardRequest.SourceDefinition.ConditionId;
			const FBattleConditionState* Hazard = Side->Hazards.FindByPredicate(
				[&HazardId](const FBattleConditionState& Condition)
				{
					return Condition.ConditionId == HazardId;
				});
			if (Hazard == nullptr
				|| !FBattleFieldSideConditionRules::IsCanonical(HazardId))
			{
				continue;
			}

			bool bGrounded = false;
			if (!TryResolveGrounded(State, *Incoming, bGrounded))
			{
				return false;
			}
			FBattleTypeEffectiveness RockEffectiveness{1, 1};
			if (HazardId == FBattleFieldSideConditionRules::GetStealthRockId())
			{
				const bool bTypeFound = Species->SecondaryType == EPokemonType::Invalid
					? State.Catalog.GetTypeChart().TryGetEffectiveness(
						EPokemonType::Rock,
						Species->PrimaryType,
						RockEffectiveness)
					: State.Catalog.GetTypeChart().TryGetDualEffectiveness(
						EPokemonType::Rock,
						Species->PrimaryType,
						Species->SecondaryType,
						RockEffectiveness);
				if (!bTypeFound)
				{
					return false;
				}
			}

			const FConditionId HazardStatusId = HazardRequest.Layers >= 2
				? FBattleMajorStatusRules::GetToxicId()
				: FBattleMajorStatusRules::GetPoisonId();
			const FBattleBattlerState* HazardSourceBattler =
				State.FindBattler(HazardRequest.Source.BattlerId);
			const FBattleTrainerState* HazardSourceTrainer = HazardSourceBattler != nullptr
				? State.FindTrainer(HazardSourceBattler->TrainerId)
				: nullptr;
			const bool bHazardAppliedByOpponent = HazardSourceTrainer == nullptr
				|| HazardSourceTrainer->Side != ActiveSlotId.GetSide();
			FBattleMajorStatusApplicationFacts StatusFacts;
			StatusFacts.RequestedStatusId = HazardStatusId;
			StatusFacts.ExistingMajorStatusId = Incoming->MajorStatusId;
			StatusFacts.PrimaryType = Species->PrimaryType;
			StatusFacts.SecondaryType = Species->SecondaryType;
			const FConditionId TerrainId = State.Field.Terrain.IsSet()
				? State.Field.Terrain.GetValue().ConditionId
				: FConditionId();
			bool bTerrainTriggerActive = false;
			if (FBattleFieldSideConditionRules::IsCanonical(TerrainId)
				&& !TryIsFieldSideConditionActiveForPhase(
						State,
						TerrainId,
						TOptional<EBattleSide>(),
						EBattleTriggerPhase::BeforeHit,
						ActiveSlotId,
						bTerrainTriggerActive))
			{
				return false;
			}
			StatusFacts.Prevention.bTerrainPrevents =
				bTerrainTriggerActive
				&& FBattleFieldSideConditionRules::ShouldTerrainPreventMajorStatus(
					TerrainId,
					HazardStatusId,
					bGrounded);
			bool bSafeguardTriggerActive = false;
			if (!TryIsFieldSideConditionActiveForPhase(
					State,
					FBattleFieldSideConditionRules::GetSafeguardId(),
					ActiveSlotId.GetSide(),
					EBattleTriggerPhase::BeforeHit,
					ActiveSlotId,
					bSafeguardTriggerActive))
			{
				return false;
			}
			StatusFacts.Prevention.bSafeguardPrevents =
				FBattleFieldSideConditionRules::ShouldSafeguardPrevent(
					bSafeguardTriggerActive,
					bHazardAppliedByOpponent,
					false);
			FBattleMajorStatusApplicationResult StatusApplication;
			if (!FBattleMajorStatusRules::TryEvaluateApplication(StatusFacts, StatusApplication))
			{
				return false;
			}

			FBattleHazardSwitchInFacts Facts;
			Facts.HazardId = HazardId;
			Facts.Layers = HazardRequest.Layers;
			Facts.BaseMaximumHP = Incoming->PermanentStats.MaxHP;
			Facts.CurrentHP = Incoming->CurrentHP;
			Facts.PrimaryType = Species->PrimaryType;
			Facts.SecondaryType = Species->SecondaryType;
			Facts.bGrounded = bGrounded;
			Facts.bMajorStatusPrevented = StatusApplication.Outcome
				!= EBattleMajorStatusApplicationOutcome::CanApply;
			bool bMistTriggerActive = false;
			if (!TryIsFieldSideConditionActiveForPhase(
					State,
					FBattleFieldSideConditionRules::GetMistId(),
					ActiveSlotId.GetSide(),
					EBattleTriggerPhase::BeforeHit,
					ActiveSlotId,
					bMistTriggerActive))
			{
				return false;
			}
			Facts.bStatStageDropPrevented =
				FBattleFieldSideConditionRules::ShouldMistPreventStatDrop(
					bMistTriggerActive,
					bHazardAppliedByOpponent,
					false,
					-1);
			Facts.RockEffectiveness = RockEffectiveness;
			FBattleHazardSwitchInResult HazardResult;
			if (!FBattleFieldSideConditionRules::TryResolveHazardSwitchIn(Facts, HazardResult))
			{
				return false;
			}

			FBattleEventSource Source;
			Source.DefinitionId = HazardId.GetDefinitionId();
			if (HazardSourceBattler != nullptr)
			{
				Source.TrainerId = HazardSourceBattler->TrainerId;
				Source.BattlerId = HazardSourceBattler->BattlerId;
				const FBattleActivePositionState* SourceActive = FindActiveForBattler(
					State,
					HazardSourceBattler->BattlerId);
				if (SourceActive != nullptr)
				{
					Source.ActiveSlotId = SourceActive->ActiveSlotId;
				}
			}
			FBattleEventTarget Target;
			Target.TrainerId = Incoming->TrainerId;
			Target.BattlerId = Incoming->BattlerId;
			Target.ActiveSlotId = ActiveSlotId;

			FBattleEffectExecutionResult FaintInput;
			FaintInput.bValid = true;
			if (HazardResult.EffectKind == EBattleHazardSwitchInEffectKind::Damage)
			{
				const int32 PreviousHP = Incoming->CurrentHP;
				const int32 AppliedDamage = FMath::Min(PreviousHP, HazardResult.Damage);
				Incoming->CurrentHP -= AppliedDamage;
				if (Incoming->CurrentHP == 0)
				{
					Incoming->bFainted = true;
					Incoming->bFaintTransitionPending = true;
				}
				for (const EBattleEventType Type : {EBattleEventType::Damage, EBattleEventType::HPChanged})
				{
					Events.Add(MakeRuleMutationEvent(
						State,
						ResolutionId,
						Type,
						EBattleActionKind::Switch,
						Source,
						Target,
						PreviousHP,
						Incoming->CurrentHP,
						-AppliedDamage));
					FBattleEffectExecutionEvent& Record = FaintInput.Events.AddDefaulted_GetRef();
					Record.Type = Type;
					Record.Cause = EBattleEventCause::Rule;
					Record.Outcome = EBattleEffectExecutionOutcome::Applied;
					Record.Targets.Add(Target);
					Record.NumericBefore = PreviousHP;
					Record.NumericAfter = Incoming->CurrentHP;
					Record.NumericDelta = -AppliedDamage;
				}
			}
			else if (HazardResult.EffectKind
				== EBattleHazardSwitchInEffectKind::ApplyMajorStatus)
			{
				FBattleTriggerSubject Owner;
				EBattleTriggerError Error = EBattleTriggerError::None;
				if (!FBattleTriggerSubject::TryCreateBattler(Incoming->BattlerId, Owner)
					|| !FBattleMajorStatusRules::TryRegisterTriggers(
						State.TriggerFramework,
						HazardResult.MajorStatusId,
						Owner,
						TOptional<int32>(),
						Error))
				{
					return false;
				}
				DrainTriggerOutputs(State);
				Incoming->MajorStatusId = HazardResult.MajorStatusId;
				Events.Add(MakeRuleMutationEvent(
					State,
					ResolutionId,
					EBattleEventType::StatusChanged,
					EBattleActionKind::Switch,
					Source,
					Target,
					0,
					1,
					1));
			}
			else if (HazardResult.EffectKind
				== EBattleHazardSwitchInEffectKind::ModifyStatStage)
			{
				const FBattleStatStageChangeResult Change = Incoming->Stages.ApplyChange(
					HazardResult.Stat,
					HazardResult.StatStageDelta);
				if (Change.Outcome == EBattleStatStageChangeOutcome::Applied)
				{
					Events.Add(MakeRuleMutationEvent(
						State,
						ResolutionId,
						EBattleEventType::StatStageChanged,
						EBattleActionKind::Switch,
						Source,
						Target,
						Change.PreviousStage,
						Change.NewStage,
						Change.AppliedDelta));
				}
			}
			else if (HazardResult.EffectKind
				== EBattleHazardSwitchInEffectKind::RemoveHazard)
			{
				const int32 PreviousLayers = HazardRequest.Layers;
				if (!TryCleanupFieldSideTriggers(
						State,
						HazardId,
						ActiveSlotId.GetSide(),
						EBattleTriggerCleanupReason::Removal))
				{
					return false;
				}
				Side->Hazards.RemoveAll(
					[&HazardId](const FBattleConditionState& Condition)
					{
						return Condition.ConditionId == HazardId;
					});
				FBattleEventTarget SideTarget;
				SideTarget.Side = ActiveSlotId.GetSide();
				SideTarget.bHasSide = true;
				Events.Add(MakeRuleMutationEvent(
					State,
					ResolutionId,
					EBattleEventType::FieldEffectChanged,
					EBattleActionKind::Switch,
					Source,
					SideTarget,
					PreviousLayers,
					0,
					-PreviousLayers));
			}

			if (Incoming->bFaintTransitionPending)
			{
				const FConditionId PendingStatus = Incoming->MajorStatusId;
				TArray<FConditionId> PendingVolatiles;
				for (const FBattleConditionState& Condition : Incoming->Volatiles)
				{
					if (FBattleVolatileRules::IsCanonical(Condition.ConditionId))
					{
						PendingVolatiles.Add(Condition.ConditionId);
					}
				}
				Incoming->LastMoveId = FMoveId();
				if (!TryCleanupSourceDependentVolatiles(
						State,
						Incoming->BattlerId,
						EBattleTriggerCleanupReason::Removal))
				{
					return false;
				}
				FBattleFaintOutcomeResolution FaintResolution;
				if (!FBattleFaintOutcomeResolver::TryResolveAction(
						FaintInput,
						EBattleTargetClass::SelectedOpponent,
						ResolutionId,
						State,
						FaintResolution))
				{
					return false;
				}
				if (!FaintResolution.Removals.IsEmpty())
				{
					if (FBattleMajorStatusRules::IsCanonical(PendingStatus)
						&& !TryCleanupMajorStatusTriggers(
							State,
							PendingStatus,
							IncomingBattlerId,
							EBattleTriggerCleanupReason::Faint))
					{
						return false;
					}
					for (const FConditionId& VolatileId : PendingVolatiles)
					{
						if (!TryCleanupVolatileTriggers(
							State,
							VolatileId,
							IncomingBattlerId,
							EBattleTriggerCleanupReason::Faint))
						{
							return false;
						}
					}
				}
				for (const FBattleFaintTransitionRecord& Faint : FaintResolution.Faints)
				{
					Events.Add(MakeTargetedActionlessEvent(
						State,
						ResolutionId,
						EBattleEventType::Fainted,
						EBattleEventCause::Rule,
						EBattleActionKind::Switch,
						Source,
						Faint.Target));
				}
				for (const FBattleFaintTransitionRecord& Removal : FaintResolution.Removals)
				{
					Events.Add(MakeTargetedActionlessEvent(
						State,
						ResolutionId,
						EBattleEventType::LeftActiveSlot,
						EBattleEventCause::Rule,
						EBattleActionKind::Switch,
						Source,
						Removal.Target));
					Events.Add(MakeTargetedActionlessEvent(
						State,
						ResolutionId,
						EBattleEventType::Removed,
						EBattleEventCause::Rule,
						EBattleActionKind::Switch,
						Source,
						Removal.Target));
					if (Removal.Target.ActiveSlotId.GetSide() == EBattleSide::Opponent)
					{
						FBattleEvent Checkpoint = MakeTargetedActionlessEvent(
							State,
							ResolutionId,
							EBattleEventType::OpponentRemovalCheckpoint,
							EBattleEventCause::Rule,
							EBattleActionKind::Switch,
							Source,
							Removal.Target);
						State.AvailableOpponentRemovalCheckpoints.Add(
							Checkpoint.GetEventOrdinal());
						Events.Add(MoveTemp(Checkpoint));
					}
				}
				if (FaintResolution.bBattleEnded)
				{
					Events.Add(MakeEvent(
						State,
						ResolutionId,
						FActionId(),
						EBattleEventType::BattleEnded,
						EBattleEventCause::Outcome,
						EBattleActionKind::Switch,
						FaintResolution.OutcomeCause,
						Source));
				}
			}
		}
		return true;
	}

	bool TryBuildReplacementCheckpointRequests(
		const FBattleEngineState& State,
		uint64 StateVersion,
		bool bAllowShiftPrompt,
		TArray<FBattleDecisionRequest>& OutRequests);

	bool TryRebuildReplacementCheckpointAfterEntryHazards(
		FBattleEngineState& State,
		const uint64 RequestStateVersion,
		const FResolutionId ResolutionId,
		const EBattleActionKind ActionKind,
		const FBattleEventSource& Source,
		TArray<FBattleEvent>& Events)
	{
		if (RequestStateVersion == 0 || !ResolutionId.IsValid())
		{
			return false;
		}

		State.PendingReplacements.Reset();
		State.PendingDecision.Reset();
		State.PendingDecisionRequests.Reset();
		if (State.Phase == EBattlePhase::Terminal)
		{
			return true;
		}

		State.Phase = EBattlePhase::Resolving;
		State.CurrentLockedActionIndex = State.LockedActions.Num();
		TArray<FBattleReplacementRequirement> Requirements;
		FBattleFaintOutcomeResolver::ResolveQueueBoundary(State, Requirements);
		if (State.Phase == EBattlePhase::MandatoryReplacement)
		{
			if (Requirements.IsEmpty())
			{
				return false;
			}
			for (const FBattleReplacementRequirement& Requirement : Requirements)
			{
				FBattlePendingReplacementState& Pending =
					State.PendingReplacements.AddDefaulted_GetRef();
				Pending.TrainerId = Requirement.Target.TrainerId;
				Pending.ActiveSlotId = Requirement.Target.ActiveSlotId;
			}

			TArray<FBattleDecisionRequest> Requests;
			if (!TryBuildReplacementCheckpointRequests(
					State,
					RequestStateVersion,
					false,
					Requests)
				|| Requests.IsEmpty())
			{
				return false;
			}
			State.PendingDecisionRequests = MoveTemp(Requests);
			State.PendingDecision = State.PendingDecisionRequests[0];
		}
		else if (State.Phase != EBattlePhase::EndOfTurn)
		{
			return false;
		}

		for (const FBattleReplacementRequirement& Requirement : Requirements)
		{
			Events.Add(MakeTargetedActionlessEvent(
				State,
				ResolutionId,
				EBattleEventType::ReplacementRequired,
				EBattleEventCause::Rule,
				ActionKind,
				Source,
				Requirement.Target));
		}
		return true;
	}

	void AppendPostActionBoundaryEvents(
		FBattleEngineState& State,
		const FResolutionId ResolutionId,
		const FBattleLockedActionState& Action,
		TArray<FBattleEvent>& Events)
	{
		TArray<FBattleReplacementRequirement> Requirements;
		FBattleFaintOutcomeResolver::ResolveQueueBoundary(State, Requirements);
		if (State.Phase == EBattlePhase::MandatoryReplacement)
		{
			State.PendingReplacements.Reset();
			for (const FBattleReplacementRequirement& Requirement : Requirements)
			{
				FBattlePendingReplacementState& Pending =
					State.PendingReplacements.AddDefaulted_GetRef();
				Pending.TrainerId = Requirement.Target.TrainerId;
				Pending.ActiveSlotId = Requirement.Target.ActiveSlotId;
			}

			TArray<FBattleDecisionRequest> Requests;
			const uint64 RequestStateVersion = State.StateVersion + 1;
			const bool bRequestsBuilt = RequestStateVersion != 0
				&& TryBuildReplacementCheckpointRequests(
					State,
					RequestStateVersion,
					true,
					Requests);
			check(bRequestsBuilt && !Requests.IsEmpty());
			State.PendingDecisionRequests = MoveTemp(Requests);
			State.PendingDecision = State.PendingDecisionRequests[0];
		}
		else if (State.Phase == EBattlePhase::EndOfTurn)
		{
			State.PendingReplacements.Reset();
			State.PendingDecisionRequests.Reset();
			State.PendingDecision.Reset();
		}
		for (const FBattleReplacementRequirement& Requirement : Requirements)
		{
			Events.Add(MakeTargetedActionEvent(
				State,
				ResolutionId,
				Action,
				EBattleEventType::ReplacementRequired,
				EBattleEventCause::Rule,
				Requirement.Target));
		}
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

	template <typename ElementType>
	void AddUnique(TArray<ElementType>& Values, const ElementType& Value)
	{
		if (!Values.Contains(Value))
		{
			Values.Add(Value);
		}
	}

	const FBattleActivePositionState* FindActiveForBattler(
		const FBattleEngineState& State,
		const FBattlerId BattlerId)
	{
		return State.ActivePositions.FindByPredicate(
			[BattlerId](const FBattleActivePositionState& Position)
			{
				return Position.BattlerId == BattlerId;
			});
	}

	bool IsLivingSelectableBattler(const FBattleBattlerState* Battler)
	{
		return Battler != nullptr
			&& !Battler->bEgg
			&& !Battler->bFainted
			&& !Battler->bCaptured
			&& !Battler->bRemoved;
	}

	FDefinitionId GetWildOpponentSwitchRestrictionRuleId()
	{
		FDefinitionId RuleId;
		const bool bCreated = FDefinitionId::TryCreate(
			FName(TEXT("Battle.Switch.NoOrdinaryWildOpponent")),
			RuleId);
		check(bCreated);
		return RuleId;
	}

	bool TryBuildSwitchLegality(
		const FBattleEngineState& State,
		const EBattleSwitchKind Kind,
		const FTrainerId TrainerId,
		const FBattlerId BattlerId,
		const FActiveSlotId ActiveSlotId,
		const TConstArrayView<FPartySlotId> ReservedPartySlots,
		FBattleSwitchLegalityResult& OutLegality)
	{
		const FBattleTrainerState* Trainer = State.FindTrainer(TrainerId);
		const FBattleActivePositionState* Active = State.FindActivePosition(ActiveSlotId);
		const bool bReplacement = Kind == EBattleSwitchKind::Replacement;
		const FBattleBattlerState* Battler = bReplacement
			? nullptr
			: State.FindBattler(BattlerId);
		if (Trainer == nullptr || Active == nullptr)
		{
			return false;
		}
		if (bReplacement)
		{
			if (BattlerId.IsValid()
				|| !Active->bAvailable
				|| Active->ActiveSlotId.GetSide() != Trainer->Side
				|| Active->TrainerId.IsValid()
				|| Active->BattlerId.IsValid())
			{
				return false;
			}
		}
		else if (Battler == nullptr
			|| Battler->TrainerId != Trainer->TrainerId
			|| Active->TrainerId != Trainer->TrainerId
			|| Active->BattlerId != Battler->BattlerId)
		{
			return false;
		}

		FBattleSwitchLegalitySpec Spec;
		Spec.Kind = Kind;
		Spec.ActingTrainerId = Trainer->TrainerId;
		Spec.ActingBattlerId = bReplacement ? FBattlerId() : Battler->BattlerId;
		Spec.ActiveSlotId = Active->ActiveSlotId;
		Spec.TransferPolicy = EBattleSwitchStateTransferPolicy::ClearTransient;
		Spec.Blockers.bEncounterPolicyAllows = Kind != EBattleSwitchKind::Voluntary
			|| !(State.EncounterKind == EBattleEncounterKind::Wild
				&& Trainer->Role == EBattleTrainerRole::Opponent);
		if (!Spec.Blockers.bEncounterPolicyAllows)
		{
			Spec.Blockers.EncounterPolicyRuleId = GetWildOpponentSwitchRestrictionRuleId();
		}
		if (Kind == EBattleSwitchKind::Voluntary && Battler != nullptr)
		{
			const FBattleSpeciesFormDefinition* Species = State.Catalog.FindSpeciesForm(
				Battler->SpeciesFormId);
			if (Species == nullptr)
			{
				return false;
			}
			for (const FBattleConditionState& Condition : Battler->Volatiles)
			{
				if (Condition.ConditionId != FBattleVolatileRules::GetPartialTrapId()
					&& Condition.ConditionId != FBattleVolatileRules::GetTrapId())
				{
					continue;
				}
				const FBattleBattlerState* Source = State.FindBattler(
					Condition.SourceBattlerId);
				const FBattleActivePositionState* SourceActive = Source != nullptr
					? State.ActivePositions.FindByPredicate(
						[Source](const FBattleActivePositionState& Position)
						{
							return Position.BattlerId == Source->BattlerId;
						})
					: nullptr;
				const bool bSourceActiveAndLiving = Source != nullptr
					&& SourceActive != nullptr
					&& Source->CurrentHP > 0
					&& !Source->bFainted
					&& !Source->bCaptured
					&& !Source->bRemoved;
				if (FBattleVolatileRules::ShouldBlockVoluntarySwitch(
						Condition.ConditionId,
						Species->PrimaryType,
						Species->SecondaryType,
						bSourceActiveAndLiving,
						true))
				{
					Spec.Blockers.bTrapped = true;
					Spec.Blockers.TrappingRuleId = Condition.ConditionId.GetDefinitionId();
					break;
				}
			}
		}

		Spec.Candidates.Reserve(Trainer->PartySlots.Num());
		for (const FBattlePartySlotState& PartySlot : Trainer->PartySlots)
		{
			FBattleSwitchCandidateFacts Candidate;
			Candidate.PartySlotId = PartySlot.PartySlotId;
			Candidate.bOccupied = PartySlot.BattlerId.IsValid();
			if (Candidate.bOccupied)
			{
				const FBattleBattlerState* CandidateBattler = State.FindBattler(PartySlot.BattlerId);
				if (CandidateBattler == nullptr)
				{
					return false;
				}
				Candidate.TrainerId = CandidateBattler->TrainerId;
				Candidate.BattlerId = CandidateBattler->BattlerId;
				Candidate.bAlreadyActive = FindActiveForBattler(State, CandidateBattler->BattlerId) != nullptr;
				Candidate.bFainted = CandidateBattler->CurrentHP <= 0 || CandidateBattler->bFainted;
				Candidate.bEgg = CandidateBattler->bEgg;
				Candidate.bCaptured = CandidateBattler->bCaptured;
				Candidate.bRemoved = CandidateBattler->bRemoved;
				Candidate.bAlreadyReserved = ReservedPartySlots.Contains(PartySlot.PartySlotId);
			}
			Spec.Candidates.Add(MoveTemp(Candidate));
		}
		return FBattleSwitchResolver::TryBuildLegality(Spec, OutLegality);
	}

	EBattleOptionUnavailableReason ToUnavailableReason(const EBattleSwitchBlockReason Reason)
	{
		switch (Reason)
		{
		case EBattleSwitchBlockReason::EmptyPartySlot:
			return EBattleOptionUnavailableReason::EmptyPartySlot;
		case EBattleSwitchBlockReason::AlreadyActive:
			return EBattleOptionUnavailableReason::AlreadyActive;
		case EBattleSwitchBlockReason::Fainted:
			return EBattleOptionUnavailableReason::Fainted;
		case EBattleSwitchBlockReason::Egg:
			return EBattleOptionUnavailableReason::Egg;
		case EBattleSwitchBlockReason::Captured:
			return EBattleOptionUnavailableReason::Captured;
		case EBattleSwitchBlockReason::WrongOwner:
			return EBattleOptionUnavailableReason::WrongOwner;
		case EBattleSwitchBlockReason::AlreadyReserved:
			return EBattleOptionUnavailableReason::AlreadyReserved;
		case EBattleSwitchBlockReason::Trapped:
			return EBattleOptionUnavailableReason::Trapped;
		case EBattleSwitchBlockReason::EncounterPolicy:
			return EBattleOptionUnavailableReason::SwitchRestricted;
		case EBattleSwitchBlockReason::Removed:
		default:
			return EBattleOptionUnavailableReason::Removed;
		}
	}

	bool TryApplySwitchSelection(
		FBattleEngineState& State,
		const FTrainerId TrainerId,
		const FBattlerId OutgoingBattlerId,
		const FActiveSlotId ActiveSlotId,
		const FBattleSwitchResolution& Resolution,
		FBattleEventTarget& OutOutgoingTarget,
		FBattleEventTarget& OutIncomingTarget)
	{
		OutOutgoingTarget = FBattleEventTarget();
		OutIncomingTarget = FBattleEventTarget();
		if (!Resolution.IsValid() || !Resolution.HasSelection())
		{
			return false;
		}
		FBattleActivePositionState* Active = State.FindMutableActivePosition(ActiveSlotId);
		FBattleBattlerState* Outgoing = State.FindMutableBattler(OutgoingBattlerId);
		FBattleBattlerState* Incoming = State.FindMutableBattler(Resolution.GetSelectedBattlerId());
		const FBattleTrainerState* Trainer = State.FindTrainer(TrainerId);
		if (Active == nullptr
			|| Outgoing == nullptr
			|| Incoming == nullptr
			|| Trainer == nullptr
			|| !Active->bAvailable
			|| Active->TrainerId != Trainer->TrainerId
			|| Active->BattlerId != Outgoing->BattlerId
			|| Outgoing->TrainerId != Trainer->TrainerId
			|| Incoming->TrainerId != Trainer->TrainerId
			|| Incoming->PartySlotId != Resolution.GetSelectedPartySlotId())
		{
			return false;
		}
		if (!TryRunToxicSwitchOut(State, *Outgoing))
		{
			return false;
		}
		if (!TryCleanupAllOwnedVolatileTriggers(
				State,
				*Outgoing,
				EBattleTriggerCleanupReason::Switch)
			|| !TryCleanupSourceDependentVolatiles(
				State,
				Outgoing->BattlerId,
				EBattleTriggerCleanupReason::Removal))
		{
			return false;
		}

		OutOutgoingTarget.TrainerId = Outgoing->TrainerId;
		OutOutgoingTarget.BattlerId = Outgoing->BattlerId;
		OutOutgoingTarget.ActiveSlotId = Active->ActiveSlotId;
		OutIncomingTarget.TrainerId = Incoming->TrainerId;
		OutIncomingTarget.BattlerId = Incoming->BattlerId;
		OutIncomingTarget.ActiveSlotId = Active->ActiveSlotId;
		Outgoing->Stages = FBattleStatStages();
		Outgoing->Volatiles.Reset();
		Outgoing->LastMoveId = FMoveId();
		Active->BattlerId = Incoming->BattlerId;
		return true;
	}

	bool TryApplyReplacementSelection(
		FBattleEngineState& State,
		const FTrainerId TrainerId,
		const FActiveSlotId ActiveSlotId,
		const FBattleSwitchResolution& Resolution,
		FBattleEventTarget& OutIncomingTarget)
	{
		OutIncomingTarget = FBattleEventTarget();
		if (!Resolution.IsValid() || !Resolution.HasSelection())
		{
			return false;
		}

		FBattleActivePositionState* Active = State.FindMutableActivePosition(ActiveSlotId);
		FBattleBattlerState* Incoming = State.FindMutableBattler(
			Resolution.GetSelectedBattlerId());
		const FBattleTrainerState* Trainer = State.FindTrainer(TrainerId);
		if (Active == nullptr
			|| Incoming == nullptr
			|| Trainer == nullptr
			|| !Active->bAvailable
			|| Active->ActiveSlotId.GetSide() != Trainer->Side
			|| Active->TrainerId.IsValid()
			|| Active->BattlerId.IsValid()
			|| Incoming->TrainerId != Trainer->TrainerId
			|| Incoming->PartySlotId != Resolution.GetSelectedPartySlotId()
			|| !IsLivingSelectableBattler(Incoming)
			|| FindActiveForBattler(State, Incoming->BattlerId) != nullptr)
		{
			return false;
		}

		Active->TrainerId = Trainer->TrainerId;
		Active->BattlerId = Incoming->BattlerId;
		OutIncomingTarget.TrainerId = Incoming->TrainerId;
		OutIncomingTarget.BattlerId = Incoming->BattlerId;
		OutIncomingTarget.ActiveSlotId = Active->ActiveSlotId;
		return true;
	}

	bool IsBattleEngineExplicitTargetClass(const EBattleTargetClass TargetClass)
	{
		return TargetClass == EBattleTargetClass::SelectedAlly
			|| TargetClass == EBattleTargetClass::SelectedOpponent
			|| TargetClass == EBattleTargetClass::AnySelectedBattler;
	}

	TArray<FBattleTargetPositionFacts> BuildBattleEngineTargetPositions(
		const FBattleEngineState& State)
	{
		TArray<FBattleTargetPositionFacts> Positions;
		Positions.Reserve(State.ActivePositions.Num());
		for (const FBattleActivePositionState& Position : State.ActivePositions)
		{
			FBattleTargetPositionFacts Facts;
			Facts.ActiveSlotId = Position.ActiveSlotId;
			const FBattleBattlerState* Battler = Position.BattlerId.IsValid()
				? State.FindBattler(Position.BattlerId)
				: nullptr;
			if (!Position.bAvailable || Battler == nullptr)
			{
				Facts.State = EBattleTargetPositionState::Empty;
			}
			else
			{
				Facts.BattlerId = Battler->BattlerId;
				if (Battler->bCaptured)
				{
					Facts.State = EBattleTargetPositionState::Captured;
				}
				else if (Battler->bRemoved)
				{
					Facts.State = EBattleTargetPositionState::Removed;
				}
				else if (Battler->bFainted)
				{
					Facts.State = EBattleTargetPositionState::Fainted;
				}
				else
				{
					Facts.State = EBattleTargetPositionState::Living;
				}
			}
			Positions.Add(MoveTemp(Facts));
		}
		return Positions;
	}

	bool TryGetCommandBand(
		const EBattleActionKind ActionKind,
		EBattleActionCommandBand& OutBand)
	{
		OutBand = EBattleActionCommandBand::Move;
		switch (ActionKind)
		{
		case EBattleActionKind::Fight:
			OutBand = EBattleActionCommandBand::Move;
			return true;
		case EBattleActionKind::Bag:
			OutBand = EBattleActionCommandBand::Bag;
			return true;
		case EBattleActionKind::Switch:
			OutBand = EBattleActionCommandBand::VoluntarySwitch;
			return true;
		case EBattleActionKind::Run:
			OutBand = EBattleActionCommandBand::Run;
			return true;
		default:
			return false;
		}
	}

	bool TryBuildLockedActions(
		FBattleEngineState& State,
		const TArray<FBattleDecision>& Selections,
		const FResolutionId ResolutionId,
		TArray<FBattleLockedActionState>& OutActions,
		bool& bOutReverseSpeed)
	{
		OutActions.Reset();
		bOutReverseSpeed = false;
		const uint64 NextActionIdAfterLock = State.NextActionId + static_cast<uint64>(Selections.Num());
		if (Selections.IsEmpty()
			|| Selections.Num() > 4
			|| !State.Random.IsValid()
			|| NextActionIdAfterLock <= State.NextActionId)
		{
			return false;
		}

		FBattleActionQueueLockSpec LockSpec;
		LockSpec.BattleId = State.Setup.GetBattleId();
		LockSpec.TurnId = State.TurnId;
		LockSpec.ResolutionId = ResolutionId;
		bool bTrickRoomTriggerActive = false;
		if (!TryIsFieldSideConditionActiveForPhase(
				State,
				FBattleFieldSideConditionRules::GetTrickRoomId(),
				TOptional<EBattleSide>(),
				EBattleTriggerPhase::ActionOrderCalculation,
				TOptional<FActiveSlotId>(),
				bTrickRoomTriggerActive))
		{
			return false;
		}
		LockSpec.bReverseSpeed = FBattleFieldSideConditionRules::ShouldReverseSpeedOrder(
			bTrickRoomTriggerActive);
		bOutReverseSpeed = LockSpec.bReverseSpeed;
		LockSpec.Candidates.Reserve(Selections.Num());

		for (int32 Index = 0; Index < Selections.Num(); ++Index)
		{
			const FBattleDecision& Decision = Selections[Index];
			const FBattleBattlerState* Battler = State.FindBattler(Decision.GetActingBattlerId());
			const FBattleActivePositionState* Active = Battler != nullptr
				? FindActiveForBattler(State, Battler->BattlerId)
				: nullptr;
			const FBattleTrainerState* Trainer = Battler != nullptr
				? State.FindTrainer(Battler->TrainerId)
				: nullptr;
			if (Battler == nullptr
				|| Active == nullptr
				|| Trainer == nullptr
				|| Trainer->TrainerId != Decision.GetDecisionOwnerTrainerId()
				|| !IsLivingSelectableBattler(Battler))
			{
				return false;
			}

			const uint64 RawActionId = State.NextActionId + static_cast<uint64>(Index);
			FActionId ActionId;
			if (RawActionId < State.NextActionId
				|| !FActionId::TryCreate(RawActionId, ActionId))
			{
				return false;
			}

			FBattleActionOrderCandidate Candidate;
			Candidate.ActionId = ActionId;
			Candidate.Decision = Decision;
			Candidate.OrderKey.ActingSlotId = Active->ActiveSlotId;
			if (!TryGetCommandBand(Decision.GetActionKind(), Candidate.OrderKey.CommandBand)
				|| !FBattleStatCalculator::TryCalculateEffectiveStat(
					Battler->PermanentStats,
					Battler->Stages,
					EBattleStat::Speed,
					Candidate.OrderKey.EffectiveSpeed))
			{
				return false;
			}
			if (Battler->MajorStatusId == FBattleMajorStatusRules::GetParalysisId())
			{
				TArray<FBattleTriggerEffectRequest> Requests;
				TArray<FBattleTriggerLifecycleFact> Facts;
				if (!TryDispatchBattlerStatusPhase(
					State,
					*Battler,
					EBattleTriggerPhase::ActionOrderCalculation,
					false,
					Candidate.OrderKey.EffectiveSpeed,
					Requests,
					Facts)
					|| Requests.Num() != 1
					|| !FBattleMajorStatusRules::TryApplySpeedModifier(
						Battler->MajorStatusId,
						Candidate.OrderKey.EffectiveSpeed,
						Candidate.OrderKey.EffectiveSpeed))
				{
					return false;
				}
			}
			bool bTailwindTriggerActive = false;
			if (!TryIsFieldSideConditionActiveForPhase(
					State,
					FBattleFieldSideConditionRules::GetTailwindId(),
					Active->ActiveSlotId.GetSide(),
					EBattleTriggerPhase::ActionOrderCalculation,
					Active->ActiveSlotId,
					bTailwindTriggerActive)
				|| !FBattleFieldSideConditionRules::TryApplyTailwindSpeed(
					bTailwindTriggerActive,
					Candidate.OrderKey.EffectiveSpeed,
					Candidate.OrderKey.EffectiveSpeed))
			{
				return false;
			}

			if (Decision.GetActionKind() == EBattleActionKind::Fight)
			{
				const FMoveId MoveId = Decision.GetMoveId();
				const FBattleMoveDefinition* Move = MoveId == FBattleBuiltInMoveDefinitions::GetStruggleMoveId()
					? &FBattleBuiltInMoveDefinitions::GetStruggle()
					: State.Catalog.FindMove(MoveId);
				if (Move == nullptr)
				{
					return false;
				}
				Candidate.OrderKey.MovePriority = Move->Priority;
				Candidate.TargetClass = Move->TargetClass;

				if (IsBattleEngineExplicitTargetClass(Candidate.TargetClass))
				{
					const FBattleActivePositionState* Target = State.FindActivePosition(
						Decision.GetActiveTargetId());
					if (Target == nullptr)
					{
						return false;
					}
					if (Target->BattlerId.IsValid())
					{
						Candidate.SelectedTargetBattlerId = Target->BattlerId;
					}
					else
					{
						FMoveId StoredChargeMoveId;
						FBattlerId StoredTargetBattlerId;
						if (!HasVolatile(
								*Battler,
								FBattleVolatileRules::GetChargingId())
							|| !TryGetVolatilePayloadMoveId(
								State,
								Battler->BattlerId,
								FBattleVolatileRules::GetChargingId(),
								StoredChargeMoveId)
							|| StoredChargeMoveId != MoveId
							|| !TryGetChargingTargetBattler(
								State,
								Battler->BattlerId,
								StoredTargetBattlerId))
						{
							return false;
						}
						Candidate.SelectedTargetBattlerId = StoredTargetBattlerId;
					}
				}
			}
			LockSpec.Candidates.Add(MoveTemp(Candidate));
		}

		TArray<FBattleLockedAction> Locked;
		EBattleActionQueueError QueueError = EBattleActionQueueError::None;
		if (!FBattleActionQueueResolver::TryLock(
			LockSpec,
			*State.Random,
			Locked,
			QueueError))
		{
			return false;
		}

		OutActions.Reserve(Locked.Num());
		for (const FBattleLockedAction& Action : Locked)
		{
			FBattleLockedActionState StateAction;
			StateAction.ActionId = Action.ActionId;
			StateAction.QueueOrdinal = Action.QueueOrdinal;
			StateAction.Decision = Action.Decision;
			StateAction.OrderKey = Action.OrderKey;
			StateAction.TargetClass = Action.TargetClass;
			StateAction.SelectedTargetBattlerId = Action.SelectedTargetBattlerId;
			OutActions.Add(MoveTemp(StateAction));
		}
		return true;
	}

	void AddUnavailableAction(
		FBattleDecisionRequestSpec& Spec,
		const EBattleActionKind ActionKind,
		const EBattleOptionUnavailableReason Reason)
	{
		FBattleUnavailableDecisionOption Option;
		Option.Kind = EBattleDecisionOptionKind::Action;
		Option.Reason = Reason;
		Option.ActionKind = ActionKind;
		Spec.UnavailableOptions.Add(Option);
	}

	void AddUnavailableMove(
		FBattleDecisionRequestSpec& Spec,
		const FMoveId MoveId,
		const EBattleOptionUnavailableReason Reason)
	{
		FBattleUnavailableDecisionOption Option;
		Option.Kind = EBattleDecisionOptionKind::Move;
		Option.Reason = Reason;
		Option.MoveId = MoveId;
		Spec.UnavailableOptions.Add(Option);
	}

	void AddUnavailableSwitch(
		FBattleDecisionRequestSpec& Spec,
		const FPartySlotId PartySlotId,
		const EBattleOptionUnavailableReason Reason)
	{
		FBattleUnavailableDecisionOption Option;
		Option.Kind = EBattleDecisionOptionKind::SwitchPartySlot;
		Option.Reason = Reason;
		Option.PartySlotId = PartySlotId;
		Spec.UnavailableOptions.Add(Option);
	}

	void AddUnavailableItem(
		FBattleDecisionRequestSpec& Spec,
		const FItemId ItemId,
		const EBattleOptionUnavailableReason Reason)
	{
		FBattleUnavailableDecisionOption Option;
		Option.Kind = EBattleDecisionOptionKind::Item;
		Option.Reason = Reason;
		Option.ItemId = ItemId;
		Spec.UnavailableOptions.Add(Option);
	}

	bool TryBuildReplacementDecisionRequest(
		const FBattleEngineState& State,
		const FBattlePendingReplacementState& Pending,
		const uint64 StateVersion,
		FBattleDecisionRequest& OutRequest)
	{
		OutRequest = FBattleDecisionRequest();
		FBattleSwitchLegalityResult Legality;
		if (!TryBuildSwitchLegality(
			State,
			EBattleSwitchKind::Replacement,
			Pending.TrainerId,
			FBattlerId(),
			Pending.ActiveSlotId,
			TConstArrayView<FPartySlotId>(),
			Legality)
			|| Legality.GetLegalPartySlots().IsEmpty())
		{
			return false;
		}

		FBattleDecisionRequestSpec Spec;
		Spec.StateVersion = StateVersion;
		Spec.RequestKind = EBattleDecisionRequestKind::MandatoryReplacement;
		Spec.DecisionOwnerTrainerId = Pending.TrainerId;
		Spec.ActingSlotId = Pending.ActiveSlotId;
		Spec.LegalActionKinds.Add(EBattleActionKind::Replacement);
		Spec.LegalActiveTargets.Add(Pending.ActiveSlotId);
		for (const FBattleSwitchCandidateResult& Candidate : Legality.GetCandidates())
		{
			if (Candidate.bLegal)
			{
				Spec.LegalSwitchPartySlots.Add(Candidate.PartySlotId);
			}
			else
			{
				AddUnavailableSwitch(
					Spec,
					Candidate.PartySlotId,
					ToUnavailableReason(Candidate.Reason));
			}
		}

		FBattleRejection Rejection;
		return FBattleDecisionRequest::TryCreate(Spec, OutRequest, Rejection);
	}

	bool TryBuildMandatoryReplacementRequests(
		const FBattleEngineState& State,
		const uint64 StateVersion,
		TArray<FBattleDecisionRequest>& OutRequests)
	{
		OutRequests.Reset();
		if (State.PendingReplacements.IsEmpty())
		{
			return false;
		}

		const FTrainerId OwnerTrainerId = State.PendingReplacements[0].TrainerId;
		for (const FBattlePendingReplacementState& Pending : State.PendingReplacements)
		{
			if (Pending.TrainerId != OwnerTrainerId)
			{
				continue;
			}

			FBattleDecisionRequest Request;
			if (!TryBuildReplacementDecisionRequest(
				State,
				Pending,
				StateVersion,
				Request))
			{
				OutRequests.Reset();
				return false;
			}
			OutRequests.Add(MoveTemp(Request));
		}
		return !OutRequests.IsEmpty() && OutRequests.Num() <= 2;
	}

	bool TryBuildShiftDecisionRequest(
		const FBattleEngineState& State,
		const uint64 StateVersion,
		FBattleDecisionRequest& OutRequest)
	{
		OutRequest = FBattleDecisionRequest();
		if (!State.EncounterPolicies.bShiftPromptEligible
			|| State.Format != EBattleFormat::Single
			|| State.EncounterKind == EBattleEncounterKind::Wild
			|| State.PendingReplacements.IsEmpty()
			|| State.PendingReplacements.ContainsByPredicate(
				[](const FBattlePendingReplacementState& Pending)
				{
					return Pending.ActiveSlotId.GetSide() == EBattleSide::Player;
				})
			|| !State.PendingReplacements.ContainsByPredicate(
				[](const FBattlePendingReplacementState& Pending)
				{
					return Pending.ActiveSlotId.GetSide() == EBattleSide::Opponent;
				}))
		{
			return false;
		}

		const FBattleTrainerState* PlayerTrainer = State.Trainers.FindByPredicate(
			[](const FBattleTrainerState& Trainer)
			{
				return Trainer.Side == EBattleSide::Player
					&& Trainer.Role == EBattleTrainerRole::Player;
			});
		const FBattleActivePositionState* PlayerActive = State.ActivePositions.FindByPredicate(
			[](const FBattleActivePositionState& Position)
			{
				return Position.ActiveSlotId.GetSide() == EBattleSide::Player
					&& Position.ActiveSlotId.GetPosition() == EBattlePosition::Left;
			});
		const FBattleBattlerState* PlayerBattler = PlayerActive != nullptr
			? State.FindBattler(PlayerActive->BattlerId)
			: nullptr;
		if (PlayerTrainer == nullptr
			|| PlayerActive == nullptr
			|| PlayerActive->TrainerId != PlayerTrainer->TrainerId
			|| !IsLivingSelectableBattler(PlayerBattler))
		{
			return false;
		}

		FBattleSwitchLegalityResult Legality;
		if (!TryBuildSwitchLegality(
			State,
			EBattleSwitchKind::Voluntary,
			PlayerTrainer->TrainerId,
			PlayerBattler->BattlerId,
			PlayerActive->ActiveSlotId,
			TConstArrayView<FPartySlotId>(),
			Legality)
			|| Legality.GetLegalPartySlots().IsEmpty())
		{
			return false;
		}

		FBattleDecisionRequestSpec Spec;
		Spec.StateVersion = StateVersion;
		Spec.RequestKind = EBattleDecisionRequestKind::ShiftResponse;
		Spec.DecisionOwnerTrainerId = PlayerTrainer->TrainerId;
		Spec.ActingBattlerId = PlayerBattler->BattlerId;
		Spec.ActingSlotId = PlayerActive->ActiveSlotId;
		Spec.LegalActionKinds.Add(EBattleActionKind::Switch);
		Spec.LegalActiveTargets.Add(PlayerActive->ActiveSlotId);
		for (const FBattleSwitchCandidateResult& Candidate : Legality.GetCandidates())
		{
			if (Candidate.bLegal)
			{
				Spec.LegalSwitchPartySlots.Add(Candidate.PartySlotId);
			}
			else
			{
				AddUnavailableSwitch(
					Spec,
					Candidate.PartySlotId,
					ToUnavailableReason(Candidate.Reason));
			}
		}

		FBattleRejection Rejection;
		return FBattleDecisionRequest::TryCreate(Spec, OutRequest, Rejection);
	}

	bool TryBuildReplacementCheckpointRequests(
		const FBattleEngineState& State,
		const uint64 StateVersion,
		const bool bAllowShiftPrompt,
		TArray<FBattleDecisionRequest>& OutRequests)
	{
		OutRequests.Reset();
		if (State.Phase != EBattlePhase::MandatoryReplacement
			|| StateVersion == 0
			|| State.PendingReplacements.IsEmpty())
		{
			return false;
		}

		if (bAllowShiftPrompt)
		{
			FBattleDecisionRequest ShiftRequest;
			if (TryBuildShiftDecisionRequest(State, StateVersion, ShiftRequest))
			{
				OutRequests.Add(MoveTemp(ShiftRequest));
				return true;
			}
		}

		return TryBuildMandatoryReplacementRequests(State, StateVersion, OutRequests);
	}

	bool TryBuildPivotDecisionRequest(
		const FBattleEngineState& State,
		const FBattleLockedActionState& Action,
		const uint64 StateVersion,
		FBattleDecisionRequest& OutRequest)
	{
		OutRequest = FBattleDecisionRequest();
		FBattleSwitchLegalityResult Legality;
		if (!TryBuildSwitchLegality(
			State,
			EBattleSwitchKind::Pivot,
			Action.Decision.GetDecisionOwnerTrainerId(),
			Action.Decision.GetActingBattlerId(),
			Action.OrderKey.ActingSlotId,
			TConstArrayView<FPartySlotId>(),
			Legality)
			|| Legality.GetLegalPartySlots().IsEmpty())
		{
			return false;
		}

		FBattleDecisionRequestSpec Spec;
		Spec.StateVersion = StateVersion;
		Spec.RequestKind = EBattleDecisionRequestKind::PivotSwitch;
		Spec.DecisionOwnerTrainerId = Action.Decision.GetDecisionOwnerTrainerId();
		Spec.ActingBattlerId = Action.Decision.GetActingBattlerId();
		Spec.ActingSlotId = Action.OrderKey.ActingSlotId;
		Spec.LegalActionKinds.Add(EBattleActionKind::Switch);
		for (const FPartySlotId PartySlotId : Legality.GetLegalPartySlots())
		{
			Spec.LegalSwitchPartySlots.Add(PartySlotId);
		}
		Spec.LegalActiveTargets.Add(Action.OrderKey.ActingSlotId);
		FBattleRejection Rejection;
		return FBattleDecisionRequest::TryCreate(Spec, OutRequest, Rejection);
	}

	bool TryAddBattleEngineMoveSelection(
		const FBattleActivePositionState& ActingPosition,
		const TArray<FBattleTargetPositionFacts>& Positions,
		const FMoveId MoveId,
		const EBattleTargetClass TargetClass,
		FBattleDecisionRequestSpec& InOutSpec,
		bool& OutHasLegalTarget)
	{
		OutHasLegalTarget = false;
		FBattleTargetSelectionSpec TargetSpec;
		TargetSpec.TargetClass = TargetClass;
		TargetSpec.UserSlotId = ActingPosition.ActiveSlotId;
		TargetSpec.UserBattlerId = ActingPosition.BattlerId;
		TargetSpec.Positions = Positions;

		FBattleTargetSelectionResult TargetSelection;
		EBattleTargetingError TargetError = EBattleTargetingError::None;
		if (!FBattleTargetResolver::TryBuildSelection(
			TargetSpec,
			TargetSelection,
			TargetError))
		{
			return false;
		}
		if (!TargetSelection.bHasLegalTarget)
		{
			return true;
		}

		OutHasLegalTarget = true;
		InOutSpec.LegalMoveIds.Add(MoveId);
		if (!TargetSelection.bRequiresExplicitChoice)
		{
			InOutSpec.AutomaticallyTargetedMoveIds.Add(MoveId);
		}
		for (const FBattleBattlerTarget& Target : TargetSelection.BattlerCandidates)
		{
			AddUnique(InOutSpec.LegalActiveTargets, Target.ActiveSlotId);
			InOutSpec.LegalMoveTargets.Add({MoveId, Target.ActiveSlotId});
		}
		return true;
	}

	int32 GetMoveCurrentPP(
		const FBattleBattlerState& Battler,
		const FMoveId MoveId)
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

	bool TryResolveVolatileMoveGate(
		const FBattleEngineState& State,
		const FBattleBattlerState& Battler,
		const FBattleMoveDefinition& SelectedMove,
		const bool bNoUsableOrdinaryMove,
		FBattleVolatileMoveGateResult& OutResult)
	{
		FBattleVolatileMoveGateFacts Facts;
		Facts.SelectedMoveId = SelectedMove.Id;
		Facts.SelectedMoveCategory = SelectedMove.Category;
		Facts.bSelectedMoveIsStruggle = SelectedMove.Id
			== FBattleBuiltInMoveDefinitions::GetStruggleMoveId();
		Facts.bNoUsableOrdinaryMove = bNoUsableOrdinaryMove;
		Facts.bTauntActive = HasVolatile(Battler, FBattleVolatileRules::GetTauntId());

		FMoveId EncoreMoveId;
		if (HasVolatile(Battler, FBattleVolatileRules::GetEncoreId())
			&& TryGetVolatilePayloadMoveId(
				State,
				Battler.BattlerId,
				FBattleVolatileRules::GetEncoreId(),
				EncoreMoveId))
		{
			Facts.EncoreMoveId = EncoreMoveId;
			Facts.bEncoreMoveStillValid = State.Catalog.FindMove(EncoreMoveId) != nullptr
				&& Battler.Moves.ContainsByPredicate(
					[EncoreMoveId](const FBattleMoveSlotState& Slot)
					{
						return Slot.MoveId == EncoreMoveId;
					});
			Facts.EncoreMoveCurrentPP = GetMoveCurrentPP(Battler, EncoreMoveId);
		}

		FMoveId DisabledMoveId;
		if (HasVolatile(Battler, FBattleVolatileRules::GetDisableId())
			&& TryGetVolatilePayloadMoveId(
				State,
				Battler.BattlerId,
				FBattleVolatileRules::GetDisableId(),
				DisabledMoveId))
		{
			Facts.DisabledMoveId = DisabledMoveId;
			Facts.bDisabledMoveStillValid = State.Catalog.FindMove(DisabledMoveId) != nullptr
				&& Battler.Moves.ContainsByPredicate(
					[DisabledMoveId](const FBattleMoveSlotState& Slot)
					{
						return Slot.MoveId == DisabledMoveId;
					});
			Facts.DisabledMoveCurrentPP = GetMoveCurrentPP(Battler, DisabledMoveId);
		}
		return FBattleVolatileRules::TryResolveMoveGate(Facts, OutResult);
	}

	EBattleOptionUnavailableReason ToUnavailableReason(
		const EBattleVolatileMoveGateOutcome Outcome)
	{
		switch (Outcome)
		{
		case EBattleVolatileMoveGateOutcome::Taunted:
			return EBattleOptionUnavailableReason::Taunted;
		case EBattleVolatileMoveGateOutcome::EncoreLocked:
			return EBattleOptionUnavailableReason::Encored;
		case EBattleVolatileMoveGateOutcome::Disabled:
			return EBattleOptionUnavailableReason::Disabled;
		default:
			return EBattleOptionUnavailableReason::Removed;
		}
	}

	bool TryBuildDecisionRequest(
		const FBattleEngineState& State,
		const FBattleDecisionActorState& Actor,
		const uint64 StateVersion,
		const TConstArrayView<FBattleDecision> AdditionalSelections,
		FBattleDecisionRequest& OutRequest,
		FBattleRejection& OutRejection)
	{
		const FBattleActivePositionState* ActingPosition = State.FindActivePosition(Actor.ActiveSlotId);
		const FBattleBattlerState* Battler = State.FindBattler(Actor.BattlerId);
		const FBattleTrainerState* Trainer = Battler != nullptr ? State.FindTrainer(Battler->TrainerId) : nullptr;
		if (!State.bHasCatalog
			|| ActingPosition == nullptr
			|| Battler == nullptr
			|| Trainer == nullptr
			|| ActingPosition->BattlerId != Battler->BattlerId
			|| ActingPosition->TrainerId != Trainer->TrainerId
			|| !IsLivingSelectableBattler(Battler)
			|| Trainer->ActionAllowance.RemainingActions <= 0)
		{
			OutRejection = FBattleRejection();
			OutRejection.Reason = EBattleRejectionReason::InvalidSetup;
			return false;
		}

		FBattleDecisionRequestSpec Spec;
		Spec.StateVersion = StateVersion;
		Spec.RequestKind = EBattleDecisionRequestKind::Action;
		Spec.DecisionOwnerTrainerId = Trainer->TrainerId;
		Spec.ActingBattlerId = Battler->BattlerId;
		Spec.ActingSlotId = ActingPosition->ActiveSlotId;
		const TArray<FBattleTargetPositionFacts> TargetPositions =
			BuildBattleEngineTargetPositions(State);

		bool bMoveRejectedForNoTarget = false;
		for (const FBattleMoveSlotState& Move : Battler->Moves)
		{
			const FBattleMoveDefinition* Definition = State.Catalog.FindMove(Move.MoveId);
			if (Definition == nullptr)
			{
				AddUnavailableMove(Spec, Move.MoveId, EBattleOptionUnavailableReason::MissingCatalogReference);
				continue;
			}
			if (Move.CurrentPP <= 0)
			{
				AddUnavailableMove(Spec, Move.MoveId, EBattleOptionUnavailableReason::NoPP);
				continue;
			}
			FBattleVolatileMoveGateResult MoveGate;
			if (!TryResolveVolatileMoveGate(
					State,
					*Battler,
					*Definition,
					false,
					MoveGate))
			{
				OutRejection = FBattleRejection();
				OutRejection.Reason = EBattleRejectionReason::InvalidSetup;
				return false;
			}
			if (MoveGate.Outcome != EBattleVolatileMoveGateOutcome::Allowed)
			{
				AddUnavailableMove(
					Spec,
					Move.MoveId,
					ToUnavailableReason(MoveGate.Outcome));
				continue;
			}
			bool bHasLegalTarget = false;
			if (!TryAddBattleEngineMoveSelection(
				*ActingPosition,
				TargetPositions,
				Move.MoveId,
				Definition->TargetClass,
				Spec,
				bHasLegalTarget))
			{
				OutRejection = FBattleRejection();
				OutRejection.Reason = EBattleRejectionReason::InvalidSetup;
				return false;
			}
			if (!bHasLegalTarget)
			{
				bMoveRejectedForNoTarget = true;
				AddUnavailableMove(Spec, Move.MoveId, EBattleOptionUnavailableReason::NoLegalTarget);
			}
		}
		if (Spec.LegalMoveIds.IsEmpty())
		{
			const FBattleMoveDefinition& Struggle = FBattleBuiltInMoveDefinitions::GetStruggle();
			bool bHasLegalTarget = false;
			if (!TryAddBattleEngineMoveSelection(
				*ActingPosition,
				TargetPositions,
				Struggle.Id,
				Struggle.TargetClass,
				Spec,
				bHasLegalTarget))
			{
				OutRejection = FBattleRejection();
				OutRejection.Reason = EBattleRejectionReason::InvalidSetup;
				return false;
			}
			if (!bHasLegalTarget)
			{
				bMoveRejectedForNoTarget = true;
			}
		}

		if (!Spec.LegalMoveIds.IsEmpty())
		{
			Spec.LegalActionKinds.Add(EBattleActionKind::Fight);
		}
		else
		{
			AddUnavailableAction(
				Spec,
				EBattleActionKind::Fight,
				bMoveRejectedForNoTarget ? EBattleOptionUnavailableReason::NoLegalTarget : EBattleOptionUnavailableReason::NoPP);
		}

		TArray<FPartySlotId> ReservedPartySlots;
		auto AddReservedSlots = [&ReservedPartySlots, Trainer](
			const TConstArrayView<FBattleDecision> Decisions)
		{
			for (const FBattleDecision& Decision : Decisions)
			{
				if (Decision.GetDecisionOwnerTrainerId() == Trainer->TrainerId
					&& Decision.GetActionKind() == EBattleActionKind::Switch
					&& Decision.GetSwitchPartySlotId().IsValid())
				{
					AddUnique(ReservedPartySlots, Decision.GetSwitchPartySlotId());
				}
			}
		};
		AddReservedSlots(State.AcceptedSelections);
		AddReservedSlots(AdditionalSelections);

		FBattleSwitchLegalityResult SwitchLegality;
		if (!TryBuildSwitchLegality(
			State,
			EBattleSwitchKind::Voluntary,
			Trainer->TrainerId,
			Battler->BattlerId,
			ActingPosition->ActiveSlotId,
			ReservedPartySlots,
			SwitchLegality))
		{
			OutRejection = FBattleRejection();
			OutRejection.Reason = EBattleRejectionReason::InvalidSetup;
			return false;
		}
		for (const FBattleSwitchCandidateResult& Candidate : SwitchLegality.GetCandidates())
		{
			if (Candidate.bLegal)
			{
				Spec.LegalSwitchPartySlots.Add(Candidate.PartySlotId);
			}
			else
			{
				AddUnavailableSwitch(
					Spec,
					Candidate.PartySlotId,
					ToUnavailableReason(Candidate.Reason));
			}
		}
		if (!Spec.LegalSwitchPartySlots.IsEmpty())
		{
			Spec.LegalActionKinds.Add(EBattleActionKind::Switch);
			AddUnique(Spec.LegalActiveTargets, ActingPosition->ActiveSlotId);
		}
		else
		{
			AddUnavailableAction(
				Spec,
				EBattleActionKind::Switch,
				SwitchLegality.IsBlocked()
					? ToUnavailableReason(SwitchLegality.GetBlockReason())
					: EBattleOptionUnavailableReason::NoLegalTarget);
		}

		if (!State.EncounterPolicies.bBagAllowed || !Trainer->ActionAllowance.bBagActionAvailable)
		{
			AddUnavailableAction(Spec, EBattleActionKind::Bag, EBattleOptionUnavailableReason::BagRestricted);
		}
		else
		{
			for (const FBattleBagItemCount& ItemCount : Trainer->Bag)
			{
				const FBattleItemDefinition* Item = State.Catalog.FindItem(ItemCount.ItemId);
				if (Item == nullptr)
				{
					AddUnavailableItem(Spec, ItemCount.ItemId, EBattleOptionUnavailableReason::MissingCatalogReference);
					continue;
				}
				if (ItemCount.Count <= 0)
				{
					AddUnavailableItem(Spec, ItemCount.ItemId, EBattleOptionUnavailableReason::NoItemRemaining);
					continue;
				}
				if (Item->Kind == EBattleItemKind::Held)
				{
					AddUnavailableItem(Spec, ItemCount.ItemId, EBattleOptionUnavailableReason::WrongItemKind);
					continue;
				}
				if (Item->Kind == EBattleItemKind::Capture
					&& (!State.EncounterPolicies.bCaptureAllowed || Trainer->Role != EBattleTrainerRole::Player))
				{
					AddUnavailableItem(Spec, ItemCount.ItemId, EBattleOptionUnavailableReason::CaptureRestricted);
					continue;
				}

				const int32 PartyPairStart = Spec.LegalItemPartyTargets.Num();
				const int32 ActivePairStart = Spec.LegalItemActiveTargets.Num();
				if (Item->Kind == EBattleItemKind::Battle)
				{
					for (const FBattlePartySlotState& PartySlot : Trainer->PartySlots)
					{
						const FBattleBattlerState* Target = State.FindBattler(PartySlot.BattlerId);
						if (Target != nullptr && !Target->bEgg && !Target->bCaptured && !Target->bRemoved)
						{
							AddUnique(Spec.LegalPartyTargets, PartySlot.PartySlotId);
							Spec.LegalItemPartyTargets.Add({ItemCount.ItemId, PartySlot.PartySlotId});
						}
					}
					for (const FBattleActivePositionState& Position : State.ActivePositions)
					{
						if (Position.TrainerId == Trainer->TrainerId && Position.BattlerId.IsValid())
						{
							AddUnique(Spec.LegalActiveTargets, Position.ActiveSlotId);
							Spec.LegalItemActiveTargets.Add({ItemCount.ItemId, Position.ActiveSlotId});
						}
					}
				}
				else if (Item->Kind == EBattleItemKind::Capture)
				{
					for (const FBattleActivePositionState& Position : State.ActivePositions)
					{
						if (Position.ActiveSlotId.GetSide() != Trainer->Side
							&& IsLivingSelectableBattler(State.FindBattler(Position.BattlerId)))
						{
							AddUnique(Spec.LegalActiveTargets, Position.ActiveSlotId);
							Spec.LegalItemActiveTargets.Add({ItemCount.ItemId, Position.ActiveSlotId});
						}
					}
				}

				if (Spec.LegalItemPartyTargets.Num() == PartyPairStart
					&& Spec.LegalItemActiveTargets.Num() == ActivePairStart)
				{
					AddUnavailableItem(Spec, ItemCount.ItemId, EBattleOptionUnavailableReason::NoLegalTarget);
				}
				else
				{
					Spec.LegalItemIds.Add(ItemCount.ItemId);
				}
			}

			if (!Spec.LegalItemIds.IsEmpty())
			{
				Spec.LegalActionKinds.Add(EBattleActionKind::Bag);
			}
			else
			{
				AddUnavailableAction(Spec, EBattleActionKind::Bag, EBattleOptionUnavailableReason::NoItemRemaining);
			}
		}

		if (State.EncounterKind == EBattleEncounterKind::Wild
			&& State.EncounterPolicies.bRunAllowed
			&& Trainer->Role == EBattleTrainerRole::Player)
		{
			Spec.LegalActionKinds.Add(EBattleActionKind::Run);
		}
		else
		{
			AddUnavailableAction(Spec, EBattleActionKind::Run, EBattleOptionUnavailableReason::RunRestricted);
		}

		return FBattleDecisionRequest::TryCreate(Spec, OutRequest, OutRejection);
	}

	int32 GetDecisionSequenceBand(const FBattleTrainerState& Trainer)
	{
		if (Trainer.Side == EBattleSide::Player
			&& Trainer.Role == EBattleTrainerRole::Player
			&& Trainer.Controller == EBattleDecisionController::Human)
		{
			return 0;
		}
		if (Trainer.Side == EBattleSide::Player
			&& Trainer.Role == EBattleTrainerRole::Partner
			&& Trainer.Controller == EBattleDecisionController::Human)
		{
			return 1;
		}
		if (Trainer.Controller == EBattleDecisionController::PartnerAI)
		{
			return 2;
		}
		if (Trainer.Controller == EBattleDecisionController::EnemyAI)
		{
			return 3;
		}
		return 4;
	}

	TArray<FBattleDecisionOwnerState> BuildDecisionOwnerSequence(const FBattleEngineState& State)
	{
		TArray<FBattleDecisionOwnerState> Sequence;
		for (const FBattleTrainerState& Trainer : State.Trainers)
		{
			FBattleDecisionOwnerState Owner;
			Owner.TrainerId = Trainer.TrainerId;
			Owner.Controller = Trainer.Controller;
			for (const FBattleActivePositionState& Position : State.ActivePositions)
			{
				if (Position.TrainerId == Trainer.TrainerId
					&& IsLivingSelectableBattler(State.FindBattler(Position.BattlerId)))
				{
					Owner.Actors.Add({Position.BattlerId, Position.ActiveSlotId});
				}
			}
			Owner.Actors.Sort(
				[](const FBattleDecisionActorState& Left, const FBattleDecisionActorState& Right)
				{
					return ActiveSlotLess(Left.ActiveSlotId, Right.ActiveSlotId);
				});
			if (!Owner.Actors.IsEmpty())
			{
				Sequence.Add(MoveTemp(Owner));
			}
		}

		Sequence.Sort(
			[&State](const FBattleDecisionOwnerState& Left, const FBattleDecisionOwnerState& Right)
			{
				const FBattleTrainerState* LeftTrainer = State.FindTrainer(Left.TrainerId);
				const FBattleTrainerState* RightTrainer = State.FindTrainer(Right.TrainerId);
				check(LeftTrainer != nullptr && RightTrainer != nullptr);
				const int32 LeftBand = GetDecisionSequenceBand(*LeftTrainer);
				const int32 RightBand = GetDecisionSequenceBand(*RightTrainer);
				return LeftBand == RightBand
					? Left.TrainerId < Right.TrainerId
					: LeftBand < RightBand;
			});
		return Sequence;
	}

	bool TryBuildPendingRequests(
		const FBattleEngineState& State,
		const TArray<FBattleDecisionOwnerState>& Sequence,
		const int32 OwnerIndex,
		const int32 ActorOffset,
		const uint64 StateVersion,
		const TConstArrayView<FBattleDecision> AdditionalSelections,
		TArray<FBattleDecisionRequest>& OutRequests,
		FBattleRejection& OutRejection)
	{
		OutRequests.Reset();
		OutRejection = FBattleRejection();
		if (!Sequence.IsValidIndex(OwnerIndex)
			|| ActorOffset < 0
			|| ActorOffset >= Sequence[OwnerIndex].Actors.Num())
		{
			OutRejection.Reason = EBattleRejectionReason::InvalidSetup;
			return false;
		}

		for (int32 ActorIndex = ActorOffset; ActorIndex < Sequence[OwnerIndex].Actors.Num(); ++ActorIndex)
		{
			FBattleDecisionRequest Request;
			if (!TryBuildDecisionRequest(
				State,
				Sequence[OwnerIndex].Actors[ActorIndex],
				StateVersion,
				AdditionalSelections,
				Request,
				OutRejection))
			{
				OutRequests.Reset();
				return false;
			}
			OutRequests.Add(MoveTemp(Request));
		}
		return !OutRequests.IsEmpty();
	}

	bool ValidateCombinedSelections(
		const FBattleEngineState& State,
		const TConstArrayView<FBattleDecision> NewDecisions,
		FBattleRejection& OutRejection)
	{
		TArray<FBattleDecision> Combined = State.AcceptedSelections;
		for (const FBattleDecision& Decision : NewDecisions)
		{
			Combined.Add(Decision);
		}

		for (int32 LeftIndex = 0; LeftIndex < Combined.Num(); ++LeftIndex)
		{
			const FBattleDecision& Left = Combined[LeftIndex];
			const FBattleTrainerState* Trainer = State.FindTrainer(Left.GetDecisionOwnerTrainerId());
			if (Trainer == nullptr)
			{
				OutRejection.Reason = EBattleRejectionReason::WrongDecisionOwner;
				OutRejection.TrainerId = Left.GetDecisionOwnerTrainerId();
				return false;
			}

			int32 TrainerActionCount = 0;
			for (const FBattleDecision& Candidate : Combined)
			{
				if (Candidate.GetDecisionOwnerTrainerId() == Trainer->TrainerId)
				{
					++TrainerActionCount;
				}
			}
			if (TrainerActionCount > Trainer->ActionAllowance.RemainingActions)
			{
				OutRejection.Reason = EBattleRejectionReason::IllegalAction;
				OutRejection.TrainerId = Trainer->TrainerId;
				return false;
			}

			for (int32 RightIndex = LeftIndex + 1; RightIndex < Combined.Num(); ++RightIndex)
			{
				const FBattleDecision& Right = Combined[RightIndex];
				if (Left.GetDecisionOwnerTrainerId() != Right.GetDecisionOwnerTrainerId())
				{
					continue;
				}
				if (Left.GetActingBattlerId() == Right.GetActingBattlerId())
				{
					OutRejection.Reason = EBattleRejectionReason::IllegalAction;
					OutRejection.BattlerId = Right.GetActingBattlerId();
					return false;
				}
				if (Left.GetActionKind() == EBattleActionKind::Bag
					&& Right.GetActionKind() == EBattleActionKind::Bag)
				{
					OutRejection.Reason = EBattleRejectionReason::IllegalAction;
					OutRejection.TrainerId = Trainer->TrainerId;
					return false;
				}
				if (Left.GetActionKind() == EBattleActionKind::Switch
					&& Right.GetActionKind() == EBattleActionKind::Switch
					&& Left.GetSwitchPartySlotId() == Right.GetSwitchPartySlotId())
				{
					OutRejection.Reason = EBattleRejectionReason::IllegalSwitch;
					OutRejection.PartySlotId = Right.GetSwitchPartySlotId();
					return false;
				}
			}
		}
		return true;
	}

	bool HasPendingReplacement(
		const FBattleEngineState& State,
		const FTrainerId TrainerId,
		const FActiveSlotId ActiveSlotId)
	{
		return State.PendingReplacements.ContainsByPredicate(
			[TrainerId, ActiveSlotId](const FBattlePendingReplacementState& Pending)
			{
				return Pending.TrainerId == TrainerId
					&& Pending.ActiveSlotId == ActiveSlotId;
			});
	}

	FBattleResolution ResolveReplacementDecisionBatch(
		FBattleEngineState& State,
		const FBattleDecisionBatch& Batch,
		const FResolutionId ResolutionId,
		const FBattleEventSource& FallbackSource)
	{
		const FBattleDecision* FirstDecision = Batch.IsValid() && !Batch.GetDecisions().IsEmpty()
			? &Batch.GetDecisions()[0]
			: nullptr;
		FBattleRejection Rejection;
		if (!Batch.IsValid())
		{
			Rejection.Reason = EBattleRejectionReason::InvalidDecisionBatch;
		}
		else if (State.PendingDecisionRequests.IsEmpty()
			|| State.PendingReplacements.IsEmpty())
		{
			Rejection.Reason = EBattleRejectionReason::NoPendingDecision;
		}
		else if (Batch.GetStateVersion() != State.StateVersion)
		{
			Rejection.Reason = EBattleRejectionReason::StaleStateVersion;
		}
		else if (Batch.GetRequestKind()
			!= EBattleDecisionRequestKind::MandatoryReplacement)
		{
			Rejection.Reason = EBattleRejectionReason::WrongRequestKind;
		}
		else if (Batch.GetDecisionOwnerTrainerId()
			!= State.PendingDecisionRequests[0].GetDecisionOwnerTrainerId())
		{
			Rejection.Reason = EBattleRejectionReason::WrongDecisionOwner;
			Rejection.TrainerId = Batch.GetDecisionOwnerTrainerId();
		}
		else if (Batch.GetDecisions().Num() != State.PendingDecisionRequests.Num())
		{
			Rejection.Reason = EBattleRejectionReason::WrongDecisionCount;
		}

		TArray<FBattleSwitchResolution> SwitchResolutions;
		TArray<FPartySlotId> ReservedPartySlots;
		if (!Rejection.IsRejected())
		{
			SwitchResolutions.Reserve(Batch.GetDecisions().Num());
			for (int32 DecisionIndex = 0;
				DecisionIndex < Batch.GetDecisions().Num();
				++DecisionIndex)
			{
				const FBattleDecision& Decision = Batch.GetDecisions()[DecisionIndex];
				const FBattleDecisionRequest& Request =
					State.PendingDecisionRequests[DecisionIndex];
				if (Request.GetRequestKind()
						!= EBattleDecisionRequestKind::MandatoryReplacement
					|| Decision.GetActiveTargetId() != Request.GetActingSlotId())
				{
					Rejection.Reason = EBattleRejectionReason::WrongDecisionOrder;
					Rejection.ActiveSlotId = Decision.GetActiveTargetId();
					break;
				}
				if (!Request.Allows(Decision, Rejection))
				{
					break;
				}
				if (!HasPendingReplacement(
					State,
					Decision.GetDecisionOwnerTrainerId(),
					Decision.GetActiveTargetId()))
				{
					Rejection.Reason = EBattleRejectionReason::IllegalTarget;
					Rejection.ActiveSlotId = Decision.GetActiveTargetId();
					break;
				}

				FBattleSwitchLegalityResult Legality;
				FBattleSwitchSelectionSpec SelectionSpec;
				SelectionSpec.RequestedPartySlotId = Decision.GetSwitchPartySlotId();
				FBattleSwitchResolution SwitchResolution;
				if (!TryBuildSwitchLegality(
						State,
						EBattleSwitchKind::Replacement,
						Decision.GetDecisionOwnerTrainerId(),
						FBattlerId(),
						Decision.GetActiveTargetId(),
						ReservedPartySlots,
						Legality)
					|| !FBattleSwitchResolver::TryResolve(
						Legality,
						SelectionSpec,
						*State.Random,
						SwitchResolution)
					|| !SwitchResolution.HasSelection())
				{
					Rejection.Reason = EBattleRejectionReason::IllegalSwitch;
					Rejection.PartySlotId = Decision.GetSwitchPartySlotId();
					break;
				}

				ReservedPartySlots.Add(Decision.GetSwitchPartySlotId());
				SwitchResolutions.Add(MoveTemp(SwitchResolution));
			}
		}

		if (Rejection.IsRejected())
		{
			return MakeRejectedResolution(
				State,
				ResolutionId,
				Rejection,
				EBattleEventType::DecisionRejected,
				EBattleEventCause::Decision,
				FirstDecision != nullptr
					? FirstDecision->GetActionKind()
					: EBattleActionKind::Replacement,
				FallbackSource);
		}

		const uint64 BeforeStateVersion = State.StateVersion;
		const uint64 AfterStateVersion = BeforeStateVersion + 1;
		if (AfterStateVersion == 0)
		{
			Rejection.Reason = EBattleRejectionReason::InvalidDecision;
			return MakeRejectedResolution(
				State,
				ResolutionId,
				Rejection,
				EBattleEventType::DecisionRejected,
				EBattleEventCause::Decision,
				EBattleActionKind::Replacement,
				FallbackSource);
		}

		TArray<FBattleEvent> Events;
		for (int32 DecisionIndex = 0;
			DecisionIndex < Batch.GetDecisions().Num();
			++DecisionIndex)
		{
			const FBattleDecision& Decision = Batch.GetDecisions()[DecisionIndex];
			const FBattleDecisionRequest& Request =
				State.PendingDecisionRequests[DecisionIndex];
			Events.Add(MakeEvent(
				State,
				ResolutionId,
				FActionId(),
				EBattleEventType::DecisionAccepted,
				EBattleEventCause::Decision,
				EBattleActionKind::Replacement,
				EBattleOutcomeCause::None,
				SourceFromRequest(State, &Request, &Decision)));
		}

		for (int32 DecisionIndex = 0;
			DecisionIndex < Batch.GetDecisions().Num();
			++DecisionIndex)
		{
			const FBattleDecision& Decision = Batch.GetDecisions()[DecisionIndex];
			const FBattleDecisionRequest& Request =
				State.PendingDecisionRequests[DecisionIndex];
			FBattleEventTarget IncomingTarget;
			const bool bApplied = TryApplyReplacementSelection(
				State,
				Decision.GetDecisionOwnerTrainerId(),
				Decision.GetActiveTargetId(),
				SwitchResolutions[DecisionIndex],
				IncomingTarget);
			if (!bApplied)
			{
				UE_LOG(
					LogTemp,
					Fatal,
					TEXT("Validated C06B replacement could not be applied atomically."));
			}
			const FBattleEventSource Source = SourceFromRequest(State, &Request, &Decision);
			Events.Add(MakeTargetedActionlessEvent(
				State,
				ResolutionId,
				EBattleEventType::EnteredActiveSlot,
				EBattleEventCause::Switch,
				EBattleActionKind::Replacement,
				Source,
				IncomingTarget));
			Events.Add(MakeTargetedActionlessEvent(
				State,
				ResolutionId,
				EBattleEventType::Switched,
				EBattleEventCause::Rule,
				EBattleActionKind::Replacement,
				Source,
				IncomingTarget));
			const bool bHazardsResolved = TryResolveEntryHazards(
				State,
				IncomingTarget.BattlerId,
				IncomingTarget.ActiveSlotId,
				ResolutionId,
				Events);
			if (!bHazardsResolved)
			{
				UE_LOG(
					LogTemp,
					Fatal,
					TEXT("Validated C07D replacement entry hazards could not be resolved."));
			}
		}

		if (!TryRebuildReplacementCheckpointAfterEntryHazards(
				State,
				AfterStateVersion,
				ResolutionId,
				EBattleActionKind::Replacement,
				FallbackSource,
				Events))
		{
			UE_LOG(
				LogTemp,
				Fatal,
				TEXT("C07D could not rebuild replacement requests after entry hazards."));
		}
		State.StateVersion = AfterStateVersion;

		EBattleStateValidationError StateError = EBattleStateValidationError::None;
		const bool bStateValid = State.ValidateInvariants(StateError);
		check(bStateValid);

		FBattleResolutionSpec ResolutionSpec;
		ResolutionSpec.ResolutionId = ResolutionId;
		ResolutionSpec.BeforeStateVersion = BeforeStateVersion;
		ResolutionSpec.AfterStateVersion = State.StateVersion;
		ResolutionSpec.bAccepted = true;
		ResolutionSpec.Events = MoveTemp(Events);
		FBattleResolution Resolution;
		const bool bResolutionCreated = FBattleResolution::TryCreate(
			ResolutionSpec,
			Resolution);
		check(bResolutionCreated);
		State.AppendResolution(Resolution);
		return Resolution;
	}

	FBattleResolution ResolveShiftResponseDecision(
		FBattleEngineState& State,
		const FBattleDecision& Decision,
		const FBattleDecisionRequest& Request,
		const FResolutionId ResolutionId,
		const FBattleEventSource& Source)
	{
		check(Request.GetRequestKind() == EBattleDecisionRequestKind::ShiftResponse);
		const bool bDeclined = !Decision.GetSwitchPartySlotId().IsValid()
			&& !Decision.GetActiveTargetId().IsValid();
		FBattleSwitchResolution SwitchResolution;
		FBattleRejection Rejection;
		if (!bDeclined)
		{
			FBattleSwitchLegalityResult Legality;
			FBattleSwitchSelectionSpec SelectionSpec;
			SelectionSpec.RequestedPartySlotId = Decision.GetSwitchPartySlotId();
			if (!TryBuildSwitchLegality(
					State,
					EBattleSwitchKind::Voluntary,
					Decision.GetDecisionOwnerTrainerId(),
					Decision.GetActingBattlerId(),
					Decision.GetActiveTargetId(),
					TConstArrayView<FPartySlotId>(),
					Legality)
				|| !FBattleSwitchResolver::TryResolve(
					Legality,
					SelectionSpec,
					*State.Random,
					SwitchResolution)
				|| !SwitchResolution.HasSelection())
			{
				Rejection.Reason = EBattleRejectionReason::IllegalSwitch;
				Rejection.PartySlotId = Decision.GetSwitchPartySlotId();
			}
		}

		if (Rejection.IsRejected())
		{
			return MakeRejectedResolution(
				State,
				ResolutionId,
				Rejection,
				EBattleEventType::DecisionRejected,
				EBattleEventCause::Decision,
				EBattleActionKind::Switch,
				Source);
		}

		const uint64 BeforeStateVersion = State.StateVersion;
		const uint64 AfterStateVersion = BeforeStateVersion + 1;
		if (AfterStateVersion == 0)
		{
			Rejection.Reason = EBattleRejectionReason::InvalidDecision;
			return MakeRejectedResolution(
				State,
				ResolutionId,
				Rejection,
				EBattleEventType::DecisionRejected,
				EBattleEventCause::Decision,
				EBattleActionKind::Switch,
				Source);
		}

		TArray<FBattleEvent> Events;
		Events.Add(MakeEvent(
			State,
			ResolutionId,
			FActionId(),
			EBattleEventType::DecisionAccepted,
			EBattleEventCause::Decision,
			EBattleActionKind::Switch,
			EBattleOutcomeCause::None,
			Source));
		if (!bDeclined)
		{
			FBattleEventTarget OutgoingTarget;
			FBattleEventTarget IncomingTarget;
			const bool bApplied = TryApplySwitchSelection(
				State,
				Decision.GetDecisionOwnerTrainerId(),
				Decision.GetActingBattlerId(),
				Decision.GetActiveTargetId(),
				SwitchResolution,
				OutgoingTarget,
				IncomingTarget);
			if (!bApplied)
			{
				UE_LOG(
					LogTemp,
					Fatal,
					TEXT("Validated C06B Shift response could not be applied."));
			}
			AppendActionlessSwitchTransitionEvents(
				State,
				ResolutionId,
				Source,
				OutgoingTarget,
				IncomingTarget,
				Events);
			const bool bHazardsResolved = TryResolveEntryHazards(
				State,
				IncomingTarget.BattlerId,
				IncomingTarget.ActiveSlotId,
				ResolutionId,
				Events);
			if (!bHazardsResolved)
			{
				UE_LOG(
					LogTemp,
					Fatal,
					TEXT("Validated C07D Shift entry hazards could not be resolved."));
			}
		}

		if (!TryRebuildReplacementCheckpointAfterEntryHazards(
				State,
				AfterStateVersion,
				ResolutionId,
				EBattleActionKind::Switch,
				Source,
				Events))
		{
			UE_LOG(
				LogTemp,
				Fatal,
				TEXT("C07D could not rebuild Shift replacement requests after entry hazards."));
		}
		State.StateVersion = AfterStateVersion;

		EBattleStateValidationError StateError = EBattleStateValidationError::None;
		const bool bStateValid = State.ValidateInvariants(StateError);
		check(bStateValid);

		FBattleResolutionSpec ResolutionSpec;
		ResolutionSpec.ResolutionId = ResolutionId;
		ResolutionSpec.BeforeStateVersion = BeforeStateVersion;
		ResolutionSpec.AfterStateVersion = State.StateVersion;
		ResolutionSpec.bAccepted = true;
		ResolutionSpec.Events = MoveTemp(Events);
		FBattleResolution Resolution;
		const bool bResolutionCreated = FBattleResolution::TryCreate(
			ResolutionSpec,
			Resolution);
		check(bResolutionCreated);
		State.AppendResolution(Resolution);
		return Resolution;
	}

}

FBattleEngine::FBattleEngine(TUniquePtr<FBattleEngineState>&& InState)
	: State(MoveTemp(InState))
{
}

FBattleEngine::~FBattleEngine() = default;

bool FBattleEngine::TryCreate(
	const FBattleSetup& Setup,
	const FBattleDefinitionCatalog& Catalog,
	TUniquePtr<IBattleRandom>&& Random,
	TUniquePtr<FBattleEngine>& OutEngine,
	FBattleRejection& OutRejection)
{
	OutEngine.Reset();
	OutRejection = FBattleRejection();
	TUniquePtr<FBattleEngineState> NewState;
	EBattleStateValidationError StateError = EBattleStateValidationError::None;
	if (!FBattleEngineState::TryCreate(
		Setup,
		&Catalog,
		MoveTemp(Random),
		NewState,
		StateError))
	{
		OutRejection.Reason = EBattleRejectionReason::InvalidSetup;
		return false;
	}

	OutEngine = TUniquePtr<FBattleEngine>(new FBattleEngine(MoveTemp(NewState)));
	return true;
}

bool FBattleEngine::TryCreate(
	const FBattleSetup& Setup,
	TUniquePtr<IBattleRandom>&& Random,
	TUniquePtr<FBattleEngine>& OutEngine,
	FBattleRejection& OutRejection)
{
	OutEngine.Reset();
	OutRejection = FBattleRejection();
	TUniquePtr<FBattleEngineState> NewState;
	EBattleStateValidationError StateError = EBattleStateValidationError::None;
	if (!FBattleEngineState::TryCreate(
		Setup,
		nullptr,
		MoveTemp(Random),
		NewState,
		StateError))
	{
		OutRejection.Reason = EBattleRejectionReason::InvalidSetup;
		return false;
	}

	OutEngine = TUniquePtr<FBattleEngine>(new FBattleEngine(MoveTemp(NewState)));
	return true;
}

#if WITH_DEV_AUTOMATION_TESTS
bool FBattleEngine::TryCreateForContractFixture(
	const FBattleSetup& Setup,
	TUniquePtr<IBattleRandom>&& Random,
	const FBattleDecisionRequest& PendingRequest,
	const bool bSeedOpponentRemovalCheckpoint,
	TUniquePtr<FBattleEngine>& OutEngine,
	FBattleRejection& OutRejection)
{
	if (!PendingRequest.IsValid()
		|| PendingRequest.GetStateVersion() != 1
		|| !TryCreate(Setup, MoveTemp(Random), OutEngine, OutRejection))
	{
		if (!OutRejection.IsRejected())
		{
			OutRejection.Reason = EBattleRejectionReason::InvalidDecision;
		}
		OutEngine.Reset();
		return false;
	}

	const FBattlePartyEntrySetup* Battler = Setup.FindBattler(PendingRequest.GetActingBattlerId());
	const FBattleTrainerSetup* Trainer = Setup.FindTrainer(PendingRequest.GetDecisionOwnerTrainerId());
	const FBattleActiveAssignment* Active = Setup.FindActive(PendingRequest.GetActingSlotId());
	if (Battler == nullptr
		|| Trainer == nullptr
		|| Active == nullptr
		|| Battler->TrainerId != Trainer->TrainerId
		|| Active->BattlerId != Battler->BattlerId)
	{
		OutRejection.Reason = EBattleRejectionReason::InvalidDecision;
		OutEngine.Reset();
		return false;
	}

	OutEngine->State->PendingDecision = PendingRequest;
	OutEngine->State->Phase = bSeedOpponentRemovalCheckpoint
		? EBattlePhase::Resolving
		: EBattlePhase::Selecting;
	if (bSeedOpponentRemovalCheckpoint)
	{
		OutEngine->State->AvailableOpponentRemovalCheckpoints.Add(1);
		OutEngine->State->NextEventOrdinal = 2;
	}
	return true;
}
#endif // WITH_DEV_AUTOMATION_TESTS

bool FBattleEngine::TryBeginActionDecisionSequence(FBattleRejection& OutRejection)
{
	OutRejection = FBattleRejection();
	if (!State.IsValid() || !State->bHasCatalog)
	{
		OutRejection.Reason = EBattleRejectionReason::InvalidSetup;
		return false;
	}
	if (State->Phase == EBattlePhase::Terminal)
	{
		OutRejection.Reason = EBattleRejectionReason::TerminalState;
		return false;
	}
	if (State->Phase != EBattlePhase::Setup
		|| State->PendingDecision.IsSet()
		|| !State->PendingDecisionRequests.IsEmpty()
		|| !State->DecisionOwnerSequence.IsEmpty())
	{
		OutRejection.Reason = EBattleRejectionReason::DecisionSequenceNotStarted;
		return false;
	}

	TArray<FBattleDecisionOwnerState> Sequence = BuildDecisionOwnerSequence(*State);
	TArray<FBattleDecisionRequest> Requests;
	const uint64 NewStateVersion = State->StateVersion + 1;
	if (Sequence.IsEmpty()
		|| NewStateVersion == 0
		|| !TryBuildPendingRequests(
			*State,
			Sequence,
			0,
			0,
			NewStateVersion,
			TConstArrayView<FBattleDecision>(),
			Requests,
			OutRejection))
	{
		if (!OutRejection.IsRejected())
		{
			OutRejection.Reason = EBattleRejectionReason::InvalidSetup;
		}
		return false;
	}

	State->DecisionOwnerSequence = MoveTemp(Sequence);
	State->CurrentDecisionOwnerIndex = 0;
	State->CurrentDecisionActorOffset = 0;
	State->PendingDecisionRequests = MoveTemp(Requests);
	State->PendingDecision = State->PendingDecisionRequests[0];
	State->Phase = EBattlePhase::Selecting;
	State->StateVersion = NewStateVersion;
	return true;
}

namespace
{
	bool IsEventVisibleToTrainer(
		const FBattleEngineState& State,
		const FBattleEventVisibility& Visibility,
		const FTrainerId ObserverTrainerId)
	{
		if (Visibility.Level == EBattleVisibilityLevel::Public)
		{
			return true;
		}
		if (Visibility.Level == EBattleVisibilityLevel::OwningTrainer)
		{
			return Visibility.OwningTrainerId == ObserverTrainerId;
		}
		if (Visibility.Level == EBattleVisibilityLevel::OwningSide && Visibility.bHasOwningSide)
		{
			const FBattleTrainerState* Observer = State.FindTrainer(ObserverTrainerId);
			return Observer != nullptr && Observer->Side == Visibility.OwningSide;
		}
		return false;
	}

	bool IsDefinitionKnown(
		const FBattleEngineState& State,
		const FTrainerId ObserverTrainerId,
		const FBattlerId SubjectBattlerId,
		const EBattleKnowledgeKind Kind,
		const FDefinitionId& DefinitionId)
	{
		const FBattleBattlerState* Subject = State.FindBattler(SubjectBattlerId);
		if (Subject != nullptr && Subject->TrainerId == ObserverTrainerId)
		{
			return true;
		}
		if (State.Setup.GetKnowledgeFacts().ContainsByPredicate(
			[ObserverTrainerId, SubjectBattlerId, Kind, &DefinitionId](const FBattleKnowledgeFact& Fact)
			{
				return Fact.ObserverTrainerId == ObserverTrainerId
					&& Fact.Visibility != EBattleVisibilityLevel::CoreOnly
					&& Fact.SubjectBattlerId == SubjectBattlerId
					&& Fact.Kind == Kind
					&& Fact.DefinitionId == DefinitionId;
			}))
		{
			return true;
		}
		if (Kind == EBattleKnowledgeKind::SpeciesFormKnown
			&& State.Setup.GetKnowledgeFacts().ContainsByPredicate(
				[ObserverTrainerId, Kind, &DefinitionId](const FBattleKnowledgeFact& Fact)
				{
					return Fact.ObserverTrainerId == ObserverTrainerId
						&& Fact.Visibility != EBattleVisibilityLevel::CoreOnly
						&& Fact.Kind == Kind
						&& Fact.DefinitionId == DefinitionId;
				}))
		{
			return true;
		}

		if (Kind == EBattleKnowledgeKind::SpeciesFormKnown)
		{
			return false;
		}

		return State.OrderedEvents.ContainsByPredicate(
			[&State, ObserverTrainerId, SubjectBattlerId, Kind, &DefinitionId](const FBattleEvent& Event)
			{
				const bool bMatchingDefinitionFamily =
					(Kind == EBattleKnowledgeKind::MoveRevealed
						&& Event.GetCause() == EBattleEventCause::Move)
					|| (Kind == EBattleKnowledgeKind::ItemRevealed
						&& Event.GetCause() == EBattleEventCause::Item)
					|| (Kind == EBattleKnowledgeKind::AbilityRevealed
						&& Event.GetCause() == EBattleEventCause::Rule);
				return Event.GetVisibility().bRevealSourceDefinition
					&& bMatchingDefinitionFamily
					&& Event.GetSource().BattlerId == SubjectBattlerId
					&& Event.GetSource().DefinitionId == DefinitionId
					&& IsEventVisibleToTrainer(State, Event.GetVisibility(), ObserverTrainerId);
			});
	}

	FBattleObservedCondition ProjectCondition(const FBattleConditionState& Condition)
	{
		FBattleObservedCondition Projection;
		Projection.ConditionId = Condition.ConditionId;
		Projection.RemainingTurns = Condition.RemainingTurns;
		Projection.LayerCount = Condition.LayerCount;
		Projection.CreationOrdinal = Condition.CreationOrdinal;
		Projection.SourceBattlerId = Condition.SourceBattlerId;
		return Projection;
	}

	EBattleEffectivenessKnowledge ToKnowledge(const FBattleTypeEffectiveness& Effectiveness)
	{
		if (Effectiveness.IsImmune())
		{
			return EBattleEffectivenessKnowledge::Immune;
		}
		if (Effectiveness.Numerator == Effectiveness.Denominator)
		{
			return EBattleEffectivenessKnowledge::Neutral;
		}
		return Effectiveness.Numerator < Effectiveness.Denominator
			? EBattleEffectivenessKnowledge::NotVeryEffective
			: EBattleEffectivenessKnowledge::SuperEffective;
	}

	EBattleEffectivenessKnowledge CalculateEffectivenessKnowledge(
		const FBattleEngineState& State,
		const FTrainerId ObserverTrainerId,
		const FMoveId MoveId,
		const FActiveSlotId TargetSlotId)
	{
		if (MoveId == FBattleBuiltInMoveDefinitions::GetStruggleMoveId())
		{
			const FBattleActivePositionState* Position = State.FindActivePosition(TargetSlotId);
			const FBattleBattlerState* Target = Position != nullptr
				? State.FindBattler(Position->BattlerId)
				: nullptr;
			return Target != nullptr
				&& IsDefinitionKnown(
					State,
					ObserverTrainerId,
					Target->BattlerId,
					EBattleKnowledgeKind::SpeciesFormKnown,
					Target->SpeciesFormId.GetDefinitionId())
				? EBattleEffectivenessKnowledge::Neutral
				: EBattleEffectivenessKnowledge::Unknown;
		}

		const FBattleMoveDefinition* Move = State.Catalog.FindMove(MoveId);
		if (Move == nullptr)
		{
			return EBattleEffectivenessKnowledge::Unknown;
		}
		if (Move->Category == EBattleMoveCategory::Status)
		{
			return EBattleEffectivenessKnowledge::NotApplicable;
		}

		const FBattleActivePositionState* Position = State.FindActivePosition(TargetSlotId);
		const FBattleBattlerState* Target = Position != nullptr ? State.FindBattler(Position->BattlerId) : nullptr;
		if (Target == nullptr
			|| !IsDefinitionKnown(
				State,
				ObserverTrainerId,
				Target->BattlerId,
				EBattleKnowledgeKind::SpeciesFormKnown,
				Target->SpeciesFormId.GetDefinitionId()))
		{
			return EBattleEffectivenessKnowledge::Unknown;
		}

		const FBattleSpeciesFormDefinition* Species = State.Catalog.FindSpeciesForm(Target->SpeciesFormId);
		if (Species == nullptr)
		{
			return EBattleEffectivenessKnowledge::Unknown;
		}

		FBattleTypeEffectiveness Effectiveness;
		const bool bFound = Species->SecondaryType == EPokemonType::Invalid
			? State.Catalog.GetTypeChart().TryGetEffectiveness(
				Move->Type,
				Species->PrimaryType,
				Effectiveness)
			: State.Catalog.GetTypeChart().TryGetDualEffectiveness(
				Move->Type,
				Species->PrimaryType,
				Species->SecondaryType,
				Effectiveness);
		return bFound ? ToKnowledge(Effectiveness) : EBattleEffectivenessKnowledge::Unknown;
	}

	EBattleEffectivenessKnowledge SummarizeEffectiveness(
		const TArray<EBattleEffectivenessKnowledge>& Values)
	{
		if (Values.IsEmpty())
		{
			return EBattleEffectivenessKnowledge::Unknown;
		}
		for (int32 Index = 1; Index < Values.Num(); ++Index)
		{
			if (Values[Index] != Values[0])
			{
				return EBattleEffectivenessKnowledge::Varies;
			}
		}
		return Values[0];
	}
}

FBattleSnapshot FBattleEngine::BuildSnapshot(const FTrainerId* ObserverTrainerId) const
{
	FBattleSnapshot Snapshot;
	if (!State.IsValid())
	{
		return Snapshot;
	}

	const bool bFiltered = ObserverTrainerId != nullptr;
	const FBattleTrainerState* Observer = bFiltered ? State->FindTrainer(*ObserverTrainerId) : nullptr;
	if (bFiltered && Observer == nullptr)
	{
		return Snapshot;
	}

	Snapshot.bValid = true;
	Snapshot.StateVersion = State->StateVersion;
	Snapshot.BattleId = State->Setup.GetBattleId();
	Snapshot.TurnId = State->TurnId;
	Snapshot.EncounterKind = State->EncounterKind;
	Snapshot.Format = State->Format;
	Snapshot.Phase = State->Phase;
	Snapshot.Outcome = State->Outcome;
	Snapshot.OutcomeCause = State->OutcomeCause;
	Snapshot.SettingsReference = State->Setup.GetSettingsReference();
	Snapshot.CatalogReference = State->Setup.GetCatalogReference();
	Snapshot.bObserverFiltered = bFiltered;
	if (bFiltered)
	{
		Snapshot.ObserverTrainerId = *ObserverTrainerId;
	}
	else
	{
		Snapshot.Trainers = State->BuildTrainerProjection();
		Snapshot.PartyEntries = State->BuildPartyProjection();
		Snapshot.ActiveAssignments = State->BuildActiveProjection();
	}

	for (const FBattleTrainerState& Trainer : State->Trainers)
	{
		FBattleObservedTrainer Projection;
		Projection.TrainerId = Trainer.TrainerId;
		Projection.Side = Trainer.Side;
		Projection.Role = Trainer.Role;
		Projection.Controller = Trainer.Controller;
		Projection.bBagVisible = !bFiltered || Trainer.TrainerId == *ObserverTrainerId;
		if (Projection.bBagVisible)
		{
			Projection.Bag = Trainer.Bag;
		}
		Snapshot.ObservedTrainers.Add(MoveTemp(Projection));
	}

	for (const FBattleActivePositionState& Position : State->ActivePositions)
	{
		Snapshot.ObservedActiveSlots.Add(
			{Position.ActiveSlotId, Position.bAvailable, Position.TrainerId, Position.BattlerId});
	}

	for (const FBattleBattlerState& Battler : State->Battlers)
	{
		const bool bOwned = bFiltered && Battler.TrainerId == *ObserverTrainerId;
		const bool bActive = FindActiveForBattler(*State, Battler.BattlerId) != nullptr;
		if (bFiltered && !bOwned && !bActive)
		{
			continue;
		}

		FBattleObservedBattler Projection;
		Projection.TrainerId = Battler.TrainerId;
		Projection.BattlerId = Battler.BattlerId;
		Projection.bPartySlotVisible = !bFiltered || bOwned;
		if (Projection.bPartySlotVisible)
		{
			Projection.PartySlotId = Battler.PartySlotId;
		}
		Projection.SpeciesFormId = Battler.SpeciesFormId;
		Projection.Level = Battler.Level;
		Projection.CurrentHP = Battler.CurrentHP;
		Projection.MaxHP = Battler.PermanentStats.MaxHP;
		Projection.bFainted = Battler.bFainted;
		Projection.MajorStatusId = Battler.MajorStatusId;
		Projection.StatStages = Battler.Stages;

		Projection.bAbilityKnown = !bFiltered
			|| bOwned
			|| IsDefinitionKnown(
				*State,
				*ObserverTrainerId,
				Battler.BattlerId,
				EBattleKnowledgeKind::AbilityRevealed,
				Battler.AbilityId.GetDefinitionId());
		if (Projection.bAbilityKnown)
		{
			Projection.AbilityId = Battler.AbilityId;
		}

		Projection.bHeldItemKnown = !bFiltered
			|| bOwned
			|| (Battler.HeldItem.CurrentItemId.IsValid()
				&& IsDefinitionKnown(
					*State,
					*ObserverTrainerId,
					Battler.BattlerId,
					EBattleKnowledgeKind::ItemRevealed,
					Battler.HeldItem.CurrentItemId.GetDefinitionId()));
		if (Projection.bHeldItemKnown)
		{
			Projection.HeldItemId = Battler.HeldItem.CurrentItemId;
		}

		for (const FBattleMoveSlotState& Move : Battler.Moves)
		{
			const bool bMoveKnown = !bFiltered
				|| bOwned
				|| IsDefinitionKnown(
					*State,
					*ObserverTrainerId,
					Battler.BattlerId,
					EBattleKnowledgeKind::MoveRevealed,
					Move.MoveId.GetDefinitionId());
			if (!bMoveKnown)
			{
				continue;
			}
			FBattleObservedMove MoveProjection;
			MoveProjection.SlotIndex = Move.SlotIndex;
			MoveProjection.MoveId = Move.MoveId;
			MoveProjection.bPPVisible = !bFiltered || bOwned;
			if (MoveProjection.bPPVisible)
			{
				MoveProjection.CurrentPP = Move.CurrentPP;
				MoveProjection.MaxPP = Move.MaxPP;
			}
			Projection.Moves.Add(MoveProjection);
		}
		Snapshot.ObservedBattlers.Add(MoveTemp(Projection));
	}

	if (State->Field.Weather.IsSet())
	{
		Snapshot.Weather = ProjectCondition(State->Field.Weather.GetValue());
	}
	if (State->Field.Terrain.IsSet())
	{
		Snapshot.Terrain = ProjectCondition(State->Field.Terrain.GetValue());
	}
	for (const FBattleConditionState& Room : State->Field.Rooms)
	{
		Snapshot.Rooms.Add(ProjectCondition(Room));
	}
	for (const FBattleConditionState& Effect : State->Field.Effects)
	{
		Snapshot.FieldEffects.Add(ProjectCondition(Effect));
	}
	for (const FBattleSideState& Side : State->Sides)
	{
		FBattleObservedSide SideProjection;
		SideProjection.Side = Side.Side;
		for (const FBattleConditionState& Condition : Side.Conditions)
		{
			SideProjection.Conditions.Add(ProjectCondition(Condition));
		}
		for (const FBattleConditionState& Hazard : Side.Hazards)
		{
			SideProjection.Hazards.Add(ProjectCondition(Hazard));
		}
		Snapshot.ObservedSides.Add(MoveTemp(SideProjection));
	}

	if (!bFiltered
		|| (!State->PendingDecisionRequests.IsEmpty()
			&& State->PendingDecisionRequests[0].GetDecisionOwnerTrainerId() == *ObserverTrainerId))
	{
		Snapshot.PendingDecisionRequests = State->PendingDecisionRequests;
		if (!Snapshot.PendingDecisionRequests.IsEmpty())
		{
			Snapshot.PendingDecision = Snapshot.PendingDecisionRequests[0];
		}
	}
	if (Snapshot.PendingDecisionRequests.IsEmpty()
		&& State->PendingDecision.IsSet()
		&& (!bFiltered
			|| State->PendingDecision.GetValue().GetDecisionOwnerTrainerId() == *ObserverTrainerId))
	{
		Snapshot.PendingDecision = State->PendingDecision;
		Snapshot.PendingDecisionRequests.Add(State->PendingDecision.GetValue());
	}

	for (const FBattleDecision& Decision : State->AcceptedSelections)
	{
		bool bVisible = !bFiltered || Decision.GetDecisionOwnerTrainerId() == *ObserverTrainerId;
		if (bFiltered && !bVisible && Observer->Role == EBattleTrainerRole::Partner)
		{
			const FBattleTrainerState* DecisionTrainer = State->FindTrainer(Decision.GetDecisionOwnerTrainerId());
			bVisible = DecisionTrainer != nullptr
				&& DecisionTrainer->Role == EBattleTrainerRole::Player
				&& DecisionTrainer->Side == Observer->Side;
		}
		if (bVisible)
		{
			Snapshot.VisibleSelections.Add(Decision);
		}
	}

	if (bFiltered)
	{
		for (const FBattleDecisionRequest& Request : Snapshot.PendingDecisionRequests)
		{
			for (const FMoveId& MoveId : Request.GetLegalMoveIds())
			{
				TArray<EBattleEffectivenessKnowledge> TargetValues;
				for (const FBattleMoveTargetOption& Pair : Request.GetLegalMoveTargets())
				{
					if (Pair.MoveId != MoveId)
					{
						continue;
					}
					const EBattleEffectivenessKnowledge Value = CalculateEffectivenessKnowledge(
						*State,
						*ObserverTrainerId,
						MoveId,
						Pair.ActiveSlotId);
					TargetValues.Add(Value);
					Snapshot.TargetEffectivenessKnowledge.Add({MoveId, Pair.ActiveSlotId, Value});
				}
				const FBattleMoveDefinition* Move = State->Catalog.FindMove(MoveId);
				const EBattleEffectivenessKnowledge Summary = Move != nullptr
					&& Move->Category == EBattleMoveCategory::Status
					? EBattleEffectivenessKnowledge::NotApplicable
					: SummarizeEffectiveness(TargetValues);
				Snapshot.MoveEffectivenessKnowledge.Add({MoveId, Summary});
			}
		}
	}
	return Snapshot;
}

FBattleSnapshot FBattleEngine::GetSnapshot() const
{
	return BuildSnapshot(nullptr);
}

FBattleSnapshot FBattleEngine::GetSnapshotForObserver(const FTrainerId ObserverTrainerId) const
{
	return BuildSnapshot(&ObserverTrainerId);
}

TOptional<FBattleDecisionRequest> FBattleEngine::GetPendingDecision() const
{
	return State.IsValid() ? State->PendingDecision : TOptional<FBattleDecisionRequest>();
}

TArray<FBattleDecisionRequest> FBattleEngine::GetPendingDecisionRequests() const
{
	return State.IsValid() ? State->PendingDecisionRequests : TArray<FBattleDecisionRequest>();
}

TArray<FBattleLockedAction> FBattleEngine::GetLockedActions() const
{
	TArray<FBattleLockedAction> Actions;
	if (!State.IsValid())
	{
		return Actions;
	}

	Actions.Reserve(State->LockedActions.Num());
	for (const FBattleLockedActionState& StateAction : State->LockedActions)
	{
		FBattleLockedAction Action;
		Action.ActionId = StateAction.ActionId;
		Action.QueueOrdinal = StateAction.QueueOrdinal;
		Action.Decision = StateAction.Decision;
		Action.OrderKey = StateAction.OrderKey;
		Action.TargetClass = StateAction.TargetClass;
		Action.SelectedTargetBattlerId = StateAction.SelectedTargetBattlerId;
		Action.TargetResolution = StateAction.TargetResolution;
		Actions.Add(MoveTemp(Action));
	}
	return Actions;
}

TOptional<FBattleLockedAction> FBattleEngine::GetCurrentLockedAction() const
{
	if (!State.IsValid()
		|| !State->LockedActions.IsValidIndex(State->CurrentLockedActionIndex))
	{
		return TOptional<FBattleLockedAction>();
	}

	const FBattleLockedActionState& StateAction = State->LockedActions[State->CurrentLockedActionIndex];
	if (!StateAction.bStarted || StateAction.bFinished)
	{
		return TOptional<FBattleLockedAction>();
	}

	FBattleLockedAction Action;
	Action.ActionId = StateAction.ActionId;
	Action.QueueOrdinal = StateAction.QueueOrdinal;
	Action.Decision = StateAction.Decision;
	Action.OrderKey = StateAction.OrderKey;
	Action.TargetClass = StateAction.TargetClass;
	Action.SelectedTargetBattlerId = StateAction.SelectedTargetBattlerId;
	Action.TargetResolution = StateAction.TargetResolution;
	return Action;
}

FBattleResolution FBattleEngine::BeginNextLockedAction()
{
	check(State.IsValid());
	const FResolutionId ResolutionId = TakeResolutionId(*State);
	const FBattleLockedActionState* ExistingAction = State->LockedActions.IsValidIndex(
		State->CurrentLockedActionIndex)
		? &State->LockedActions[State->CurrentLockedActionIndex]
		: nullptr;
	const FBattleEventSource FallbackSource = ExistingAction != nullptr
		? SourceFromLockedAction(*State, *ExistingAction)
		: FindFallbackSource(*State);
	const EBattleActionKind ActionKind = ExistingAction != nullptr
		? ExistingAction->Decision.GetActionKind()
		: EBattleActionKind::Fight;

	FBattleRejection Rejection;
	if (State->Phase == EBattlePhase::Terminal)
	{
		Rejection.Reason = EBattleRejectionReason::TerminalState;
	}
	else if (State->Battlers.ContainsByPredicate(
		[](const FBattleBattlerState& Candidate)
		{
			return Candidate.bFaintTransitionPending;
		}))
	{
		// C05B leaves zero-HP battlers pending for C05C. Do not allow a later
		// locked action to bypass that checkpoint before C05C owns the transition.
		Rejection.Reason = EBattleRejectionReason::IllegalAction;
	}
	else if ((State->Phase != EBattlePhase::Locked && State->Phase != EBattlePhase::Resolving)
		|| ExistingAction == nullptr
		|| ExistingAction->bFinished)
	{
		Rejection.Reason = EBattleRejectionReason::IllegalAction;
	}
	else if (ExistingAction->bStarted)
	{
		Rejection.Reason = EBattleRejectionReason::IllegalAction;
		Rejection.ActionId = ExistingAction->ActionId;
	}

	const FBattleBattlerState* Battler = ExistingAction != nullptr
		? State->FindBattler(ExistingAction->Decision.GetActingBattlerId())
		: nullptr;
	const FBattleTrainerState* Trainer = Battler != nullptr
		? State->FindTrainer(Battler->TrainerId)
		: nullptr;
	if (!Rejection.IsRejected()
		&& (Trainer == nullptr || Trainer->ActionAllowance.RemainingActions <= 0))
	{
		Rejection.Reason = EBattleRejectionReason::IllegalAction;
		if (Trainer != nullptr)
		{
			Rejection.TrainerId = Trainer->TrainerId;
		}
	}

	if (Rejection.IsRejected())
	{
		return MakeRejectedResolution(
			*State,
			ResolutionId,
			Rejection,
			EBattleEventType::ActionCanceled,
			EBattleEventCause::Action,
			ActionKind,
			FallbackSource);
	}

	check(ExistingAction != nullptr && Battler != nullptr && Trainer != nullptr);
	const bool bChargedFight = ExistingAction->Decision.GetActionKind()
			== EBattleActionKind::Fight
		&& IsReleasingCharge(
			*State,
			*Battler,
			ExistingAction->Decision.GetMoveId());
	const FBattleActivePositionState* Active = State->FindActivePosition(
		ExistingAction->OrderKey.ActingSlotId);
	const FBattleBattlerState* SelectedTarget = ExistingAction->SelectedTargetBattlerId.IsValid()
		? State->FindBattler(ExistingAction->SelectedTargetBattlerId)
		: nullptr;

	FBattleActionStartFacts Facts;
	Facts.ActionKind = ExistingAction->Decision.GetActionKind();
	Facts.bActorActive = Active != nullptr
		&& Active->BattlerId == Battler->BattlerId;
	Facts.bActorLiving = IsLivingSelectableBattler(Battler);
	Facts.bSelectedTargetCaptured = Facts.ActionKind == EBattleActionKind::Fight
		&& SelectedTarget != nullptr
		&& SelectedTarget->bCaptured;
	Facts.bSubjectToPlayerObedience = Facts.ActionKind == EBattleActionKind::Fight
		&& Trainer->Role == EBattleTrainerRole::Player
		&& Trainer->Controller == EBattleDecisionController::Human
		&& Battler->Obedience.bHasSnapshot
		&& Battler->Obedience.bSubjectToPlayerCap;
	if (Facts.bSubjectToPlayerObedience)
	{
		Facts.ObedienceReferenceLevel = Battler->Obedience.ReferenceLevel;
		Facts.BadgeCount = Battler->Obedience.BadgeCount;
	}

	FBattleActionStartResult StartResult;
	if (!FBattleActionStartRules::TryEvaluate(Facts, StartResult))
	{
		Rejection.Reason = EBattleRejectionReason::InvalidDecision;
		return MakeRejectedResolution(
			*State,
			ResolutionId,
			Rejection,
			EBattleEventType::ActionCanceled,
			EBattleEventCause::Action,
			ActionKind,
			FallbackSource);
	}
	if (StartResult.Outcome == EBattleActionStartOutcome::Proceed)
	{
		bool bMagicRoomTriggerActive = false;
		if (!TryIsFieldSideConditionActiveForPhase(
				*State,
				FBattleFieldSideConditionRules::GetMagicRoomId(),
				TOptional<EBattleSide>(),
				EBattleTriggerPhase::BeforeAction,
				Active != nullptr
					? TOptional<FActiveSlotId>(Active->ActiveSlotId)
					: TOptional<FActiveSlotId>(),
				bMagicRoomTriggerActive))
		{
			Rejection.Reason = EBattleRejectionReason::InvalidDecision;
			return MakeRejectedResolution(
				*State,
				ResolutionId,
				Rejection,
				EBattleEventType::ActionCanceled,
				EBattleEventCause::Action,
				ActionKind,
				FallbackSource);
		}
		for (FBattleBattlerState& Candidate : State->Battlers)
		{
			Candidate.HeldItem.bSuppressed = bMagicRoomTriggerActive
				&& Candidate.HeldItem.CurrentItemId.IsValid()
				&& !Candidate.HeldItem.bConsumed;
		}
	}

	bool bRechargeDeniedAction = false;
	if (StartResult.Outcome == EBattleActionStartOutcome::Proceed
		&& HasVolatile(*Battler, FBattleVolatileRules::GetRechargeId()))
	{
		const FBattleTriggerFramework FrameworkBeforeGate = State->TriggerFramework;
		const uint64 TriggerTokenBeforeGate = State->NextTriggerReentrancyToken;
		const TArray<FBattleConditionState> VolatilesBeforeGate = Battler->Volatiles;
		TArray<FBattleTriggerEffectRequest> Requests;
		TArray<FBattleTriggerLifecycleFact> FactsAtGate;
		FBattleVolatileActionResult Gate;
		bool bGateValid = TryDispatchBattlerVolatilePhase(
			*State,
			*Battler,
			EBattleTriggerPhase::BeforeAction,
			false,
			Requests,
			FactsAtGate,
			FBattleVolatileRules::GetRechargeId())
			&& Requests.Num() == 1
			&& FactsAtGate.IsEmpty()
			&& Requests[0].SourceDefinition.Kind
				== EBattleTriggerSourceDefinitionKind::Condition
			&& Requests[0].SourceDefinition.ConditionId
				== FBattleVolatileRules::GetRechargeId()
			&& FBattleVolatileRules::TryResolveSimpleBeforeAction(
				FBattleVolatileRules::GetRechargeId(),
				Gate)
			&& Gate.Outcome == EBattleVolatileActionOutcome::Denied
			&& Gate.bRemoveVolatile;
		FBattleBattlerState* MutableBattler = State->FindMutableBattler(Battler->BattlerId);
		bGateValid = bGateValid
			&& MutableBattler != nullptr
			&& TryCleanupVolatileTriggers(
				*State,
				FBattleVolatileRules::GetRechargeId(),
				Battler->BattlerId,
				EBattleTriggerCleanupReason::Removal);
		if (bGateValid)
		{
			bGateValid = MutableBattler->Volatiles.RemoveAll(
				[](const FBattleConditionState& Condition)
				{
					return Condition.ConditionId == FBattleVolatileRules::GetRechargeId();
				}) == 1;
		}
		if (!bGateValid)
		{
			State->TriggerFramework = FrameworkBeforeGate;
			State->NextTriggerReentrancyToken = TriggerTokenBeforeGate;
			if (MutableBattler != nullptr)
			{
				MutableBattler->Volatiles = VolatilesBeforeGate;
			}
			Rejection.Reason = EBattleRejectionReason::InvalidDecision;
			return MakeRejectedResolution(
				*State,
				ResolutionId,
				Rejection,
				EBattleEventType::ActionCanceled,
				EBattleEventCause::Rule,
				ActionKind,
				FallbackSource);
		}
		bRechargeDeniedAction = true;
	}

	const uint64 BeforeStateVersion = State->StateVersion;
	FBattleLockedActionState& Action = State->LockedActions[State->CurrentLockedActionIndex];
	FBattleTrainerState* MutableTrainer = State->FindMutableTrainer(Trainer->TrainerId);
	check(MutableTrainer != nullptr && MutableTrainer->ActionAllowance.RemainingActions > 0);
	--MutableTrainer->ActionAllowance.RemainingActions;

	TArray<FBattleEvent> Events;
	if (bRechargeDeniedAction)
	{
		Action.bStarted = true;
		Action.bFinished = true;
		State->Phase = EBattlePhase::Resolving;
		if (bChargedFight)
		{
			const bool bChargeCleared = TryClearChargeState(
				*State,
				Battler->BattlerId,
				EBattleTriggerCleanupReason::Removal);
			check(bChargeCleared);
		}
		Events.Add(MakeActionDetailEvent(
			*State,
			ResolutionId,
			Action,
			EBattleEventType::ActionStarted,
			EBattleEventCause::Action));
		Events.Add(MakeActionDetailEvent(
			*State,
			ResolutionId,
			Action,
			EBattleEventType::StatusChanged,
			EBattleEventCause::Rule,
			static_cast<int64>(1),
			static_cast<int64>(0),
			static_cast<int64>(-1)));
		Events.Add(MakeActionDetailEvent(
			*State,
			ResolutionId,
			Action,
			EBattleEventType::EffectPrevented,
			EBattleEventCause::Rule));
		Events.Add(MakeActionDetailEvent(
			*State,
			ResolutionId,
			Action,
			EBattleEventType::ActionCanceled,
			EBattleEventCause::Rule));
		Events.Add(MakeActionDetailEvent(
			*State,
			ResolutionId,
			Action,
			EBattleEventType::ActionCompleted,
			EBattleEventCause::Action));
		++State->CurrentLockedActionIndex;
		AppendPostActionBoundaryEvents(*State, ResolutionId, Action, Events);
	}
	else if (StartResult.Outcome == EBattleActionStartOutcome::Proceed)
	{
		Action.bStarted = true;
		State->Phase = EBattlePhase::Resolving;
		Events.Add(MakeActionDetailEvent(
			*State,
			ResolutionId,
			Action,
			EBattleEventType::ActionStarted,
			EBattleEventCause::Action));
		if (StartResult.ObedienceCap.IsSet())
		{
			Events.Add(MakeActionDetailEvent(
				*State,
				ResolutionId,
				Action,
				EBattleEventType::ObedienceConfirmed,
				EBattleEventCause::Rule,
				static_cast<int64>(Facts.ObedienceReferenceLevel),
				static_cast<int64>(StartResult.ObedienceCap.GetValue()),
				static_cast<int64>(Facts.ObedienceReferenceLevel)
					- static_cast<int64>(StartResult.ObedienceCap.GetValue()),
				EBattleVisibilityLevel::CoreOnly));
		}
	}
	else if (StartResult.Outcome == EBattleActionStartOutcome::ObedienceRefused)
	{
		Action.bStarted = true;
		Action.bFinished = true;
		State->Phase = EBattlePhase::Resolving;
		Events.Add(MakeActionDetailEvent(
			*State,
			ResolutionId,
			Action,
			EBattleEventType::ActionStarted,
			EBattleEventCause::Action));
		Events.Add(MakeActionDetailEvent(
			*State,
			ResolutionId,
			Action,
			EBattleEventType::ObedienceRefused,
			EBattleEventCause::Rule,
			static_cast<int64>(Facts.ObedienceReferenceLevel),
			static_cast<int64>(StartResult.ObedienceCap.GetValue()),
			static_cast<int64>(Facts.ObedienceReferenceLevel)
				- static_cast<int64>(StartResult.ObedienceCap.GetValue())));
		Events.Add(MakeActionDetailEvent(
			*State,
			ResolutionId,
			Action,
			EBattleEventType::ActionCompleted,
			EBattleEventCause::Action));
		++State->CurrentLockedActionIndex;
		AppendPostActionBoundaryEvents(*State, ResolutionId, Action, Events);
	}
	else
	{
		Action.bFinished = true;
		State->Phase = EBattlePhase::Resolving;
		Events.Add(MakeActionDetailEvent(
			*State,
			ResolutionId,
			Action,
			EBattleEventType::ActionCanceled,
			EBattleEventCause::Action));
		Events.Add(MakeActionDetailEvent(
			*State,
			ResolutionId,
			Action,
			EBattleEventType::ActionCompleted,
			EBattleEventCause::Action));
		++State->CurrentLockedActionIndex;
		AppendPostActionBoundaryEvents(*State, ResolutionId, Action, Events);
	}
	if (StartResult.Outcome != EBattleActionStartOutcome::Proceed && bChargedFight)
	{
		const bool bChargeCleared = TryClearChargeState(
			*State,
			Battler->BattlerId,
			EBattleTriggerCleanupReason::Removal);
		check(bChargeCleared);
	}

	++State->StateVersion;
	FBattleResolutionSpec ResolutionSpec;
	ResolutionSpec.ResolutionId = ResolutionId;
	ResolutionSpec.BeforeStateVersion = BeforeStateVersion;
	ResolutionSpec.AfterStateVersion = State->StateVersion;
	ResolutionSpec.bAccepted = true;
	ResolutionSpec.Events = MoveTemp(Events);
	FBattleResolution Resolution;
	const bool bResolutionCreated = FBattleResolution::TryCreate(ResolutionSpec, Resolution);
	check(bResolutionCreated);
	State->AppendResolution(Resolution);
	return Resolution;
}

FBattleResolution FBattleEngine::ExecuteCurrentSwitch()
{
	check(State.IsValid());
	const FResolutionId ResolutionId = TakeResolutionId(*State);
	FBattleLockedActionState* Action = State->LockedActions.IsValidIndex(
		State->CurrentLockedActionIndex)
		? &State->LockedActions[State->CurrentLockedActionIndex]
		: nullptr;
	const FBattleEventSource FallbackSource = Action != nullptr
		? SourceFromLockedAction(*State, *Action)
		: FindFallbackSource(*State);

	FBattleRejection Rejection;
	if (State->Phase == EBattlePhase::Terminal)
	{
		Rejection.Reason = EBattleRejectionReason::TerminalState;
	}
	else if (State->Phase != EBattlePhase::Resolving
		|| Action == nullptr
		|| !Action->bStarted
		|| Action->bFinished
		|| Action->Decision.GetActionKind() != EBattleActionKind::Switch)
	{
		Rejection.Reason = EBattleRejectionReason::IllegalAction;
		if (Action != nullptr)
		{
			Rejection.ActionId = Action->ActionId;
		}
	}
	if (Rejection.IsRejected())
	{
		return MakeRejectedResolution(
			*State,
			ResolutionId,
			Rejection,
			EBattleEventType::ActionCanceled,
			EBattleEventCause::Switch,
			EBattleActionKind::Switch,
			FallbackSource);
	}

	check(Action != nullptr);
	const uint64 BeforeStateVersion = State->StateVersion;
	TArray<FPartySlotId> ReservedPartySlots;
	for (int32 Index = 0; Index < State->LockedActions.Num(); ++Index)
	{
		if (Index == State->CurrentLockedActionIndex)
		{
			continue;
		}
		const FBattleLockedActionState& Other = State->LockedActions[Index];
		if (!Other.bFinished
			&& Other.Decision.GetActionKind() == EBattleActionKind::Switch
			&& Other.Decision.GetDecisionOwnerTrainerId()
				== Action->Decision.GetDecisionOwnerTrainerId())
		{
			AddUnique(ReservedPartySlots, Other.Decision.GetSwitchPartySlotId());
		}
	}

	FBattleSwitchLegalityResult Legality;
	const bool bLegalityBuilt = TryBuildSwitchLegality(
		*State,
		EBattleSwitchKind::Voluntary,
		Action->Decision.GetDecisionOwnerTrainerId(),
		Action->Decision.GetActingBattlerId(),
		Action->OrderKey.ActingSlotId,
		ReservedPartySlots,
		Legality);
	FBattleSwitchSelectionSpec SelectionSpec;
	SelectionSpec.RequestedPartySlotId = Action->Decision.GetSwitchPartySlotId();
	FBattleSwitchResolution SwitchResolution;
	const bool bResolved = bLegalityBuilt
		&& FBattleSwitchResolver::TryResolve(
			Legality,
			SelectionSpec,
			*State->Random,
			SwitchResolution);

	TArray<FBattleEvent> Events;
	FBattleEventTarget OutgoingTarget;
	FBattleEventTarget IncomingTarget;
	const bool bApplied = bResolved
		&& SwitchResolution.HasSelection()
		&& TryApplySwitchSelection(
			*State,
			Action->Decision.GetDecisionOwnerTrainerId(),
			Action->Decision.GetActingBattlerId(),
			Action->OrderKey.ActingSlotId,
			SwitchResolution,
			OutgoingTarget,
			IncomingTarget);
	if (bApplied)
	{
		AppendSwitchTransitionEvents(
			*State,
			ResolutionId,
			*Action,
			OutgoingTarget,
			IncomingTarget,
			Events);
		const bool bHazardsResolved = TryResolveEntryHazards(
			*State,
			IncomingTarget.BattlerId,
			IncomingTarget.ActiveSlotId,
			ResolutionId,
			Events);
		if (!bHazardsResolved)
		{
			UE_LOG(
				LogTemp,
				Fatal,
				TEXT("Validated C07D voluntary-switch entry hazards could not be resolved."));
		}
	}
	else
	{
		Events.Add(MakeActionDetailEvent(
			*State,
			ResolutionId,
			*Action,
			EBattleEventType::ActionCanceled,
			EBattleEventCause::Switch));
	}

	Action->bFinished = true;
	Events.Add(MakeActionDetailEvent(
		*State,
		ResolutionId,
		*Action,
		EBattleEventType::ActionCompleted,
		EBattleEventCause::Action));
	++State->CurrentLockedActionIndex;
	AppendPostActionBoundaryEvents(*State, ResolutionId, *Action, Events);
	++State->StateVersion;

	EBattleStateValidationError StateError = EBattleStateValidationError::None;
	const bool bStateValid = State->ValidateInvariants(StateError);
	check(bStateValid);

	FBattleResolutionSpec ResolutionSpec;
	ResolutionSpec.ResolutionId = ResolutionId;
	ResolutionSpec.BeforeStateVersion = BeforeStateVersion;
	ResolutionSpec.AfterStateVersion = State->StateVersion;
	ResolutionSpec.bAccepted = true;
	ResolutionSpec.Events = MoveTemp(Events);
	FBattleResolution Resolution;
	const bool bResolutionCreated = FBattleResolution::TryCreate(ResolutionSpec, Resolution);
	check(bResolutionCreated);
	State->AppendResolution(Resolution);
	return Resolution;
}

FBattleResolution FBattleEngine::CommitCurrentMoveAfterPreMoveGates()
{
	check(State.IsValid());
	const FResolutionId ResolutionId = TakeResolutionId(*State);
	FBattleLockedActionState* Action = State->LockedActions.IsValidIndex(
		State->CurrentLockedActionIndex)
		? &State->LockedActions[State->CurrentLockedActionIndex]
		: nullptr;
	const FBattleEventSource FallbackSource = Action != nullptr
		? SourceFromLockedAction(*State, *Action)
		: FindFallbackSource(*State);

	FBattleRejection Rejection;
	if (State->Phase == EBattlePhase::Terminal)
	{
		Rejection.Reason = EBattleRejectionReason::TerminalState;
	}
	else if (State->Phase != EBattlePhase::Resolving
		|| Action == nullptr
		|| !Action->bStarted
		|| Action->bFinished
		|| Action->bMoveCommitted
		|| Action->Decision.GetActionKind() != EBattleActionKind::Fight)
	{
		Rejection.Reason = EBattleRejectionReason::IllegalAction;
	}

	FBattleBattlerState* Battler = Action != nullptr
		? State->FindMutableBattler(Action->Decision.GetActingBattlerId())
		: nullptr;
	FBattleMoveSlotState* MoveSlot = nullptr;
	const bool bStruggle = Action != nullptr
		&& Action->Decision.GetMoveId() == FBattleBuiltInMoveDefinitions::GetStruggleMoveId();
	const FBattleMoveDefinition* Move = nullptr;
	if (!Rejection.IsRejected() && Battler == nullptr)
	{
		Rejection.Reason = EBattleRejectionReason::WrongActingBattler;
	}
	if (!Rejection.IsRejected())
	{
		Move = bStruggle
			? &FBattleBuiltInMoveDefinitions::GetStruggle()
			: State->Catalog.FindMove(Action->Decision.GetMoveId());
		if (Move == nullptr)
		{
			Rejection.Reason = EBattleRejectionReason::IllegalMove;
		}
	}
	if (!Rejection.IsRejected() && !bStruggle)
	{
		MoveSlot = Battler->Moves.FindByPredicate(
			[Action](const FBattleMoveSlotState& Candidate)
			{
				return Candidate.MoveId == Action->Decision.GetMoveId();
			});
	}

	if (Rejection.IsRejected())
	{
		return MakeRejectedResolution(
			*State,
			ResolutionId,
			Rejection,
			EBattleEventType::ActionCanceled,
			EBattleEventCause::Action,
			EBattleActionKind::Fight,
			FallbackSource);
	}

	check(Action != nullptr && Battler != nullptr && Move != nullptr);
	const bool bReleasingCharge = IsReleasingCharge(
		*State,
		*Battler,
		Move->Id);
	const uint64 BeforeStateVersion = State->StateVersion;
	TArray<FBattleEvent> Events;
	bool bStatusDeniedAction = false;
	bool bStatusCured = false;
	FBattleMajorStatusActionResult StatusAction;
	if (Battler->MajorStatusId == FBattleMajorStatusRules::GetSleepId()
		|| Battler->MajorStatusId == FBattleMajorStatusRules::GetFreezeId()
		|| Battler->MajorStatusId == FBattleMajorStatusRules::GetParalysisId())
	{
		const FConditionId StatusBeforeGate = Battler->MajorStatusId;
		const FBattleTriggerFramework FrameworkBeforeGate = State->TriggerFramework;
		const uint64 TriggerTokenBeforeGate = State->NextTriggerReentrancyToken;
		TArray<FBattleTriggerEffectRequest> TriggerRequests;
		TArray<FBattleTriggerLifecycleFact> TriggerFacts;
		const bool bSleep = StatusBeforeGate == FBattleMajorStatusRules::GetSleepId();
		const bool bDispatched = TryDispatchBattlerStatusPhase(
			*State,
			*Battler,
			EBattleTriggerPhase::BeforeAction,
			bSleep,
			TOptional<int32>(),
			TriggerRequests,
			TriggerFacts);
		bool bGateValid = bDispatched;
		if (bSleep)
		{
			const bool bExpired = TriggerFacts.ContainsByPredicate(
				[](const FBattleTriggerLifecycleFact& Fact)
				{
					return Fact.Kind == EBattleTriggerLifecycleFactKind::Ended
						&& Fact.EndReason.IsSet()
						&& Fact.EndReason.GetValue() == EBattleTriggerEndReason::Expired;
				});
			if (bExpired)
			{
				bStatusCured = true;
				Battler->MajorStatusId = FConditionId();
			}
			else if (TriggerRequests.Num() == 1
				&& TriggerRequests[0].RemainingTurns.IsSet()
				&& TriggerRequests[0].RemainingTurns.GetValue() > 0)
			{
				// The asleep-usable move hook is explicit in the C07B rule input and
				// remains neutral until later move content supplies it.
				bStatusDeniedAction = true;
			}
			else
			{
				bGateValid = false;
			}
		}
		else
		{
			FBattleMajorStatusActionFacts Facts;
			Facts.StatusId = StatusBeforeGate;
			Facts.bMoveThawsUser = EnumHasAllFlags(
				Move->Flags,
				EBattleMoveFlags::ThawsUser);
			FBattleRandomContext RandomContext;
			RandomContext.BattleId = State->Setup.GetBattleId();
			RandomContext.TurnId = State->TurnId;
			RandomContext.ActionId = Action->ActionId;
			RandomContext.ResolutionId = ResolutionId;
			RandomContext.RulePurpose = StatusBeforeGate.GetDefinitionId();
			bGateValid = bGateValid
				&& TriggerRequests.Num() == 1
				&& FBattleMajorStatusRules::TryResolveBeforeAction(
					Facts,
					RandomContext,
					*State->Random,
					StatusAction);
			if (bGateValid)
			{
				bStatusDeniedAction = StatusAction.Outcome
					== EBattleMajorStatusActionOutcome::Denied;
				if (StatusAction.bCureStatus)
				{
					bGateValid = TryCleanupMajorStatusTriggers(
						*State,
						StatusBeforeGate,
						Battler->BattlerId,
						EBattleTriggerCleanupReason::Removal);
					if (bGateValid)
					{
						bStatusCured = true;
						Battler->MajorStatusId = FConditionId();
					}
				}
			}
		}

		if (!bGateValid)
		{
			State->TriggerFramework = FrameworkBeforeGate;
			State->NextTriggerReentrancyToken = TriggerTokenBeforeGate;
			Battler->MajorStatusId = StatusBeforeGate;
			Rejection.Reason = EBattleRejectionReason::InvalidDecision;
			return MakeRejectedResolution(
				*State,
				ResolutionId,
				Rejection,
				EBattleEventType::ActionCanceled,
				EBattleEventCause::Rule,
				EBattleActionKind::Fight,
				FallbackSource);
		}
	}

	if (StatusAction.bDrawConsumed)
	{
		Events.Add(MakeActionDetailEvent(
			*State,
			ResolutionId,
			*Action,
			EBattleEventType::RandomCheck,
			EBattleEventCause::Rule,
			static_cast<int64>(StatusAction.Draw.InclusiveMinimum),
			static_cast<int64>(StatusAction.Draw.Result),
			static_cast<int64>(StatusAction.Draw.InclusiveMaximum)));
	}
	if (bStatusCured)
	{
		Events.Add(MakeActionDetailEvent(
			*State,
			ResolutionId,
			*Action,
			EBattleEventType::StatusChanged,
			EBattleEventCause::Rule,
			static_cast<int64>(1),
			static_cast<int64>(0),
			static_cast<int64>(-1)));
	}
	if (bStatusDeniedAction)
	{
		if (bReleasingCharge)
		{
			const bool bChargeCleared = TryClearChargeState(
				*State,
				Battler->BattlerId,
				EBattleTriggerCleanupReason::Removal);
			check(bChargeCleared);
		}
		Action->bFinished = true;
		Events.Add(MakeActionDetailEvent(
			*State,
			ResolutionId,
			*Action,
			EBattleEventType::EffectPrevented,
			EBattleEventCause::Rule));
		Events.Add(MakeActionDetailEvent(
			*State,
			ResolutionId,
			*Action,
			EBattleEventType::ActionCanceled,
			EBattleEventCause::Rule));
		Events.Add(MakeActionDetailEvent(
			*State,
			ResolutionId,
			*Action,
			EBattleEventType::ActionCompleted,
			EBattleEventCause::Action));
		++State->CurrentLockedActionIndex;
		AppendPostActionBoundaryEvents(*State, ResolutionId, *Action, Events);
		++State->StateVersion;

		FBattleResolutionSpec ResolutionSpec;
		ResolutionSpec.ResolutionId = ResolutionId;
		ResolutionSpec.BeforeStateVersion = BeforeStateVersion;
		ResolutionSpec.AfterStateVersion = State->StateVersion;
		ResolutionSpec.bAccepted = true;
		ResolutionSpec.Events = MoveTemp(Events);
		FBattleResolution Resolution;
		const bool bResolutionCreated = FBattleResolution::TryCreate(
			ResolutionSpec,
			Resolution);
		check(bResolutionCreated);
		State->AppendResolution(Resolution);
		return Resolution;
	}

	const FBattleTriggerFramework VolatileFrameworkBeforeGate = State->TriggerFramework;
	const uint64 VolatileTokenBeforeGate = State->NextTriggerReentrancyToken;
	const TArray<FBattleConditionState> VolatilesBeforeGate = Battler->Volatiles;
	TArray<FBattleTriggerEffectRequest> VolatileRequests;
	TArray<FBattleTriggerLifecycleFact> VolatileFacts;
	bool bVolatileGateValid = TryDispatchBattlerVolatilePhase(
		*State,
		*Battler,
		EBattleTriggerPhase::BeforeAction,
		true,
		VolatileRequests,
		VolatileFacts);
	bool bVolatileDeniedAction = false;
	bool bConfusionSelfHit = false;
	bool bVolatileRemoved = false;

	auto RemoveVolatile = [&](const FConditionId& VolatileId)
	{
		if (!TryCleanupVolatileTriggers(
				*State,
				VolatileId,
				Battler->BattlerId,
				EBattleTriggerCleanupReason::Removal))
		{
			return false;
		}
		Battler->Volatiles.RemoveAll(
			[&VolatileId](const FBattleConditionState& Condition)
			{
				return Condition.ConditionId == VolatileId;
			});
		bVolatileRemoved = true;
		return true;
	};

	if (bVolatileGateValid)
	{
		for (const FBattleTriggerLifecycleFact& Fact : VolatileFacts)
		{
			if (Fact.Kind == EBattleTriggerLifecycleFactKind::Ended
				&& Fact.EndReason.IsSet()
				&& Fact.EndReason.GetValue() == EBattleTriggerEndReason::Expired
				&& Fact.SourceDefinition.Kind
					== EBattleTriggerSourceDefinitionKind::Condition
				&& Fact.SourceDefinition.ConditionId
					== FBattleVolatileRules::GetConfusionId())
			{
				bVolatileGateValid = RemoveVolatile(
					FBattleVolatileRules::GetConfusionId());
				break;
			}
		}
	}

	for (const FBattleTriggerEffectRequest& Request : VolatileRequests)
	{
		if (!bVolatileGateValid || bVolatileDeniedAction)
		{
			break;
		}
		if (Request.SourceDefinition.Kind
			!= EBattleTriggerSourceDefinitionKind::Condition)
		{
			bVolatileGateValid = false;
			break;
		}
		const FConditionId VolatileId = Request.SourceDefinition.ConditionId;
		if (VolatileId == FBattleVolatileRules::GetConfusionId())
		{
			FBattleConditionState* Confusion = FindMutableVolatile(*Battler, VolatileId);
			if (Confusion == nullptr || !Confusion->RemainingTurns.IsSet())
			{
				bVolatileGateValid = false;
				break;
			}
			FBattleRandomContext RandomContext;
			RandomContext.BattleId = State->Setup.GetBattleId();
			RandomContext.TurnId = State->TurnId;
			RandomContext.ActionId = Action->ActionId;
			RandomContext.ResolutionId = ResolutionId;
			RandomContext.RulePurpose = FBattleVolatileRules::GetConfusionActionGatePurpose();
			FBattleVolatileActionResult Gate;
			bVolatileGateValid = FBattleVolatileRules::TryResolveConfusionBeforeAction(
				Confusion->RemainingTurns.GetValue(),
				RandomContext,
				*State->Random,
				Gate)
				&& Request.RemainingTurns.IsSet()
				&& Gate.RemainingTurns.IsSet()
				&& Request.RemainingTurns.GetValue() == Gate.RemainingTurns.GetValue();
			if (!bVolatileGateValid)
			{
				break;
			}
			Confusion->RemainingTurns = Gate.RemainingTurns;
			if (Gate.bDrawConsumed)
			{
				Events.Add(MakeActionDetailEvent(
					*State,
					ResolutionId,
					*Action,
					EBattleEventType::RandomCheck,
					EBattleEventCause::Rule,
					static_cast<int64>(Gate.Draw.InclusiveMinimum),
					static_cast<int64>(Gate.Draw.Result),
					static_cast<int64>(Gate.Draw.InclusiveMaximum)));
			}
			bConfusionSelfHit = Gate.Outcome
				== EBattleVolatileActionOutcome::ConfusionSelfHit;
			bVolatileDeniedAction = bConfusionSelfHit;
		}
		else if (VolatileId == FBattleVolatileRules::GetFlinchId()
			|| VolatileId == FBattleVolatileRules::GetRechargeId())
		{
			FBattleVolatileActionResult Gate;
			bVolatileGateValid = FBattleVolatileRules::TryResolveSimpleBeforeAction(
				VolatileId,
				Gate)
				&& Gate.bRemoveVolatile
				&& RemoveVolatile(VolatileId);
			bVolatileDeniedAction = bVolatileGateValid;
		}
		else if (VolatileId == FBattleVolatileRules::GetTauntId()
			|| VolatileId == FBattleVolatileRules::GetEncoreId()
			|| VolatileId == FBattleVolatileRules::GetDisableId())
		{
			FBattleVolatileMoveGateResult Gate;
			bVolatileGateValid = TryResolveVolatileMoveGate(
				*State,
				*Battler,
				*Move,
				bStruggle,
				Gate);
			if (!bVolatileGateValid)
			{
				break;
			}
			if (Gate.bEndEncore
				&& HasVolatile(*Battler, FBattleVolatileRules::GetEncoreId()))
			{
				bVolatileGateValid = RemoveVolatile(
					FBattleVolatileRules::GetEncoreId());
			}
			if (bVolatileGateValid
				&& Gate.bEndDisable
				&& HasVolatile(*Battler, FBattleVolatileRules::GetDisableId()))
			{
				bVolatileGateValid = RemoveVolatile(
					FBattleVolatileRules::GetDisableId());
			}
			bVolatileDeniedAction = bVolatileGateValid
				&& Gate.Outcome != EBattleVolatileMoveGateOutcome::Allowed;
		}
	}

	if (!bVolatileGateValid)
	{
		State->TriggerFramework = VolatileFrameworkBeforeGate;
		State->NextTriggerReentrancyToken = VolatileTokenBeforeGate;
		Battler->Volatiles = VolatilesBeforeGate;
		Rejection.Reason = EBattleRejectionReason::InvalidDecision;
		return MakeRejectedResolution(
			*State,
			ResolutionId,
			Rejection,
			EBattleEventType::ActionCanceled,
			EBattleEventCause::Rule,
			EBattleActionKind::Fight,
			FallbackSource);
	}
	if (bVolatileRemoved)
	{
		Events.Add(MakeActionDetailEvent(
			*State,
			ResolutionId,
			*Action,
			EBattleEventType::StatusChanged,
			EBattleEventCause::Rule,
			static_cast<int64>(1),
			static_cast<int64>(0),
			static_cast<int64>(-1)));
	}

	FBattleFaintOutcomeResolution ConfusionFaintResolution;
	if (bConfusionSelfHit)
	{
		FBattleFinalDamageInput DamageInput;
		DamageInput.AttackerLevel = Battler->Level;
		DamageInput.AttackerStats = Battler->PermanentStats;
		DamageInput.DefenderStats = Battler->PermanentStats;
		DamageInput.AttackerStages = Battler->Stages;
		DamageInput.DefenderStages = Battler->Stages;
		DamageInput.MoveCategory = EBattleMoveCategory::Physical;
		DamageInput.MovePower = FBattleVolatileRules::GetConfusionSelfHitBasePower();
		DamageInput.bAttackerBurned = FBattleMajorStatusRules::ShouldApplyBurnPhysicalPenalty(
			Battler->MajorStatusId,
			EBattleMoveCategory::Physical,
			false);
		DamageInput.bBypassTypeImmunity = true;
		DamageInput.WeatherModifierQ12 = FBattleFinalDamageCalculator::Q12Neutral;
		DamageInput.StabModifierQ12 = FBattleFinalDamageCalculator::Q12Neutral;
		DamageInput.TypeEffectiveness = {1, 1};
		DamageInput.RandomContext.BattleId = State->Setup.GetBattleId();
		DamageInput.RandomContext.TurnId = State->TurnId;
		DamageInput.RandomContext.ActionId = Action->ActionId;
		DamageInput.RandomContext.ResolutionId = ResolutionId;
		DamageInput.RandomContext.RulePurpose =
			FBattleVolatileRules::GetConfusionSelfHitDamagePurpose();
		FBattleFinalDamageResult DamageResult;
		EBattleDamageCalculationError DamageError = EBattleDamageCalculationError::None;
		if (!FBattleFinalDamageCalculator::TryCalculateFinalDamage(
				DamageInput,
				*State->Random,
				DamageResult,
				DamageError)
			|| DamageResult.Outcome != EBattleDamageOutcome::Damage)
		{
			Rejection.Reason = EBattleRejectionReason::InvalidDecision;
			return MakeRejectedResolution(
				*State,
				ResolutionId,
				Rejection,
				EBattleEventType::ActionCanceled,
				EBattleEventCause::Rule,
				EBattleActionKind::Fight,
				FallbackSource);
		}
		if (DamageResult.bRandomDrawConsumed)
		{
			Events.Add(MakeActionDetailEvent(
				*State,
				ResolutionId,
				*Action,
				EBattleEventType::RandomCheck,
				EBattleEventCause::Rule,
				static_cast<int64>(DamageResult.RandomDraw.InclusiveMinimum),
				static_cast<int64>(DamageResult.RandomDraw.Result),
				static_cast<int64>(DamageResult.RandomDraw.InclusiveMaximum)));
		}
		const int32 PreviousHP = Battler->CurrentHP;
		const int32 AppliedDamage = FMath::Min(PreviousHP, DamageResult.Damage);
		Battler->CurrentHP -= AppliedDamage;
		if (Battler->CurrentHP == 0)
		{
			Battler->bFainted = true;
			Battler->bFaintTransitionPending = true;
		}
		FBattleEventTarget SelfTarget;
		SelfTarget.TrainerId = Battler->TrainerId;
		SelfTarget.BattlerId = Battler->BattlerId;
		SelfTarget.ActiveSlotId = Action->OrderKey.ActingSlotId;
		FBattleEffectExecutionResult EffectResult;
		EffectResult.bValid = true;
		for (const EBattleEventType Type : {EBattleEventType::Damage, EBattleEventType::HPChanged})
		{
			FBattleEffectExecutionEvent& Record = EffectResult.Events.AddDefaulted_GetRef();
			Record.Type = Type;
			Record.Cause = EBattleEventCause::Rule;
			Record.Outcome = EBattleEffectExecutionOutcome::Applied;
			Record.Targets.Add(SelfTarget);
			Record.NumericBefore = PreviousHP;
			Record.NumericAfter = Battler->CurrentHP;
			Record.NumericDelta = -AppliedDamage;
			Events.Add(MakeBattleEffectEvent(
				*State,
				ResolutionId,
				*Action,
				Record,
				TOptional<uint64>()));
		}

		if (Battler->bFaintTransitionPending)
		{
			const FConditionId PendingStatus = Battler->MajorStatusId;
			TArray<FConditionId> PendingVolatiles;
			for (const FBattleConditionState& Condition : Battler->Volatiles)
			{
				if (FBattleVolatileRules::IsCanonical(Condition.ConditionId))
				{
					PendingVolatiles.Add(Condition.ConditionId);
				}
			}
			Battler->LastMoveId = FMoveId();
			const bool bSourceEffectsCleaned = TryCleanupSourceDependentVolatiles(
				*State,
				Battler->BattlerId,
				EBattleTriggerCleanupReason::Removal);
			check(bSourceEffectsCleaned);
			const bool bFaintsResolved = FBattleFaintOutcomeResolver::TryResolveAction(
				EffectResult,
				EBattleTargetClass::Self,
				ResolutionId,
				*State,
				ConfusionFaintResolution);
			check(bFaintsResolved);
			if (FBattleMajorStatusRules::IsCanonical(PendingStatus))
			{
				const bool bCleaned = TryCleanupMajorStatusTriggers(
					*State,
					PendingStatus,
					SelfTarget.BattlerId,
					EBattleTriggerCleanupReason::Faint);
				check(bCleaned);
			}
			for (const FConditionId& VolatileId : PendingVolatiles)
			{
				const bool bCleaned = TryCleanupVolatileTriggers(
					*State,
					VolatileId,
					SelfTarget.BattlerId,
					EBattleTriggerCleanupReason::Faint);
				check(bCleaned);
			}
			for (const FBattleFaintTransitionRecord& Faint : ConfusionFaintResolution.Faints)
			{
				Events.Add(MakeTargetedActionEvent(
					*State,
					ResolutionId,
					*Action,
					EBattleEventType::Fainted,
					EBattleEventCause::Rule,
					Faint.Target));
			}
			for (const FBattleFaintTransitionRecord& Removal : ConfusionFaintResolution.Removals)
			{
				Events.Add(MakeTargetedActionEvent(
					*State,
					ResolutionId,
					*Action,
					EBattleEventType::LeftActiveSlot,
					EBattleEventCause::Rule,
					Removal.Target));
				Events.Add(MakeTargetedActionEvent(
					*State,
					ResolutionId,
					*Action,
					EBattleEventType::Removed,
					EBattleEventCause::Rule,
					Removal.Target));
			}
			if (ConfusionFaintResolution.bBattleEnded)
			{
				const bool bBattleEndCleaned = TryCleanupBattleEndTriggers(*State);
				check(bBattleEndCleaned);
			}
		}
	}

	if (bVolatileDeniedAction)
	{
		if (bReleasingCharge)
		{
			const bool bChargeCleared = TryClearChargeState(
				*State,
				Battler->BattlerId,
				EBattleTriggerCleanupReason::Removal);
			check(bChargeCleared);
		}
		Action->bFinished = true;
		Events.Add(MakeActionDetailEvent(
			*State,
			ResolutionId,
			*Action,
			EBattleEventType::EffectPrevented,
			EBattleEventCause::Rule));
		Events.Add(MakeActionDetailEvent(
			*State,
			ResolutionId,
			*Action,
			EBattleEventType::ActionCanceled,
			EBattleEventCause::Rule));
		Events.Add(MakeActionDetailEvent(
			*State,
			ResolutionId,
			*Action,
			EBattleEventType::ActionCompleted,
			EBattleEventCause::Action));
		++State->CurrentLockedActionIndex;
		if (ConfusionFaintResolution.bBattleEnded)
		{
			Events.Add(MakeBattleEndedEvent(
				*State,
				ResolutionId,
				*Action,
				ConfusionFaintResolution.OutcomeCause));
		}
		else
		{
			AppendPostActionBoundaryEvents(*State, ResolutionId, *Action, Events);
		}
		++State->StateVersion;

		FBattleResolutionSpec ResolutionSpec;
		ResolutionSpec.ResolutionId = ResolutionId;
		ResolutionSpec.BeforeStateVersion = BeforeStateVersion;
		ResolutionSpec.AfterStateVersion = State->StateVersion;
		ResolutionSpec.bAccepted = true;
		ResolutionSpec.Events = MoveTemp(Events);
		FBattleResolution Resolution;
		const bool bResolutionCreated = FBattleResolution::TryCreate(
			ResolutionSpec,
			Resolution);
		check(bResolutionCreated);
		State->AppendResolution(Resolution);
		return Resolution;
	}
	if (!bStruggle
		&& !bReleasingCharge
		&& (MoveSlot == nullptr || MoveSlot->CurrentPP <= 0))
	{
		Action->bFinished = true;
		Events.Add(MakeActionDetailEvent(
			*State,
			ResolutionId,
			*Action,
			EBattleEventType::ActionCanceled,
			EBattleEventCause::Action));
		Events.Add(MakeActionDetailEvent(
			*State,
			ResolutionId,
			*Action,
			EBattleEventType::ActionCompleted,
			EBattleEventCause::Action));
		++State->CurrentLockedActionIndex;
		AppendPostActionBoundaryEvents(*State, ResolutionId, *Action, Events);
		++State->StateVersion;

		FBattleResolutionSpec ResolutionSpec;
		ResolutionSpec.ResolutionId = ResolutionId;
		ResolutionSpec.BeforeStateVersion = BeforeStateVersion;
		ResolutionSpec.AfterStateVersion = State->StateVersion;
		ResolutionSpec.bAccepted = true;
		ResolutionSpec.Events = MoveTemp(Events);
		FBattleResolution Resolution;
		const bool bResolutionCreated = FBattleResolution::TryCreate(ResolutionSpec, Resolution);
		check(bResolutionCreated);
		State->AppendResolution(Resolution);
		return Resolution;
	}

	if (MoveSlot != nullptr && !bReleasingCharge)
	{
		const int32 PreviousPP = MoveSlot->CurrentPP;
		--MoveSlot->CurrentPP;
		Events.Add(MakeActionDetailEvent(
			*State,
			ResolutionId,
			*Action,
			EBattleEventType::PPConsumed,
			EBattleEventCause::Move,
			static_cast<int64>(PreviousPP),
			static_cast<int64>(MoveSlot->CurrentPP),
			static_cast<int64>(-1)));
	}
	Events.Add(MakeActionDetailEvent(
		*State,
		ResolutionId,
		*Action,
		EBattleEventType::MoveUsed,
		EBattleEventCause::Move));
	Battler->LastMoveId = Move->Id;
	Action->bMoveCommitted = true;
	++State->StateVersion;

	FBattleResolutionSpec ResolutionSpec;
	ResolutionSpec.ResolutionId = ResolutionId;
	ResolutionSpec.BeforeStateVersion = BeforeStateVersion;
	ResolutionSpec.AfterStateVersion = State->StateVersion;
	ResolutionSpec.bAccepted = true;
	ResolutionSpec.Events = MoveTemp(Events);
	FBattleResolution Resolution;
	const bool bResolutionCreated = FBattleResolution::TryCreate(ResolutionSpec, Resolution);
	check(bResolutionCreated);
	State->AppendResolution(Resolution);
	return Resolution;
}

FBattleResolution FBattleEngine::ResolveCurrentMoveTargets()
{
	check(State.IsValid());
	const FResolutionId ResolutionId = TakeResolutionId(*State);
	FBattleLockedActionState* Action = State->LockedActions.IsValidIndex(
		State->CurrentLockedActionIndex)
		? &State->LockedActions[State->CurrentLockedActionIndex]
		: nullptr;
	const FBattleEventSource FallbackSource = Action != nullptr
		? SourceFromLockedAction(*State, *Action)
		: FindFallbackSource(*State);

	FBattleRejection Rejection;
	if (State->Phase == EBattlePhase::Terminal)
	{
		Rejection.Reason = EBattleRejectionReason::TerminalState;
	}
	else if (State->Phase != EBattlePhase::Resolving
		|| Action == nullptr
		|| !Action->bStarted
		|| Action->bFinished
		|| !Action->bMoveCommitted
		|| Action->TargetResolution.IsSet()
		|| Action->Decision.GetActionKind() != EBattleActionKind::Fight)
	{
		Rejection.Reason = EBattleRejectionReason::IllegalAction;
	}

	const FBattleBattlerState* User = Action != nullptr
		? State->FindBattler(Action->Decision.GetActingBattlerId())
		: nullptr;
	const FBattleActivePositionState* UserPosition = Action != nullptr
		? State->FindActivePosition(Action->OrderKey.ActingSlotId)
		: nullptr;
	if (!Rejection.IsRejected()
		&& (User == nullptr
			|| UserPosition == nullptr
			|| UserPosition->BattlerId != User->BattlerId
			|| !IsLivingSelectableBattler(User)))
	{
		Rejection.Reason = EBattleRejectionReason::WrongActingBattler;
		if (Action != nullptr)
		{
			Rejection.BattlerId = Action->Decision.GetActingBattlerId();
		}
	}

	if (Rejection.IsRejected())
	{
		return MakeRejectedResolution(
			*State,
			ResolutionId,
			Rejection,
			EBattleEventType::ActionCanceled,
			EBattleEventCause::Targeting,
			EBattleActionKind::Fight,
			FallbackSource);
	}

	check(Action != nullptr && User != nullptr && UserPosition != nullptr);
	FBattleTargetResolutionSpec TargetSpec;
	TargetSpec.TargetClass = Action->TargetClass;
	TargetSpec.UserSlotId = UserPosition->ActiveSlotId;
	TargetSpec.UserBattlerId = User->BattlerId;
	TargetSpec.Positions = BuildBattleEngineTargetPositions(*State);
	if (IsBattleEngineExplicitTargetClass(Action->TargetClass))
	{
		TargetSpec.ExplicitTarget.ActiveSlotId = Action->Decision.GetActiveTargetId();
		const FBattleActivePositionState* CurrentTargetPosition = State->FindActivePosition(
			TargetSpec.ExplicitTarget.ActiveSlotId);
		if (CurrentTargetPosition != nullptr && CurrentTargetPosition->BattlerId.IsValid())
		{
			TargetSpec.ExplicitTarget.BattlerId = CurrentTargetPosition->BattlerId;
		}
		else
		{
			// The selector chose a structural position. A later switch is hit through
			// the current occupant above; this historical identity is retained only
			// to preserve fainted-target fallback when the position is now empty.
			TargetSpec.ExplicitTarget.BattlerId = Action->SelectedTargetBattlerId;
			const FBattleBattlerState* OriginalTarget = State->FindBattler(
				Action->SelectedTargetBattlerId);
			FBattleTargetPositionFacts* EmptySelectedPosition = TargetSpec.Positions.FindByPredicate(
				[&TargetSpec](const FBattleTargetPositionFacts& Position)
				{
					return Position.ActiveSlotId == TargetSpec.ExplicitTarget.ActiveSlotId;
				});
			if (OriginalTarget != nullptr && EmptySelectedPosition != nullptr)
			{
				EmptySelectedPosition->BattlerId = OriginalTarget->BattlerId;
				EmptySelectedPosition->State = OriginalTarget->bCaptured
					? EBattleTargetPositionState::Captured
					: OriginalTarget->bFainted
						? EBattleTargetPositionState::Fainted
						: EBattleTargetPositionState::Removed;
			}
		}
	}
	if (Action->TargetClass == EBattleTargetClass::RandomLegalOpponent)
	{
		TargetSpec.RandomContext.BattleId = State->Setup.GetBattleId();
		TargetSpec.RandomContext.TurnId = State->TurnId;
		TargetSpec.RandomContext.ActionId = Action->ActionId;
		TargetSpec.RandomContext.ResolutionId = ResolutionId;
		TargetSpec.RandomContext.RulePurpose =
			FBattleTargetResolver::GetRandomLegalOpponentRulePurpose();
	}

	FBattleTargetResolutionResult TargetResolution;
	EBattleTargetingError TargetError = EBattleTargetingError::None;
	if (!FBattleTargetResolver::TryResolve(
		TargetSpec,
		*State->Random,
		TargetResolution,
		TargetError)
		|| TargetResolution.Outcome == EBattleTargetResolutionOutcome::CapturedTargetCanceled
		|| TargetResolution.Outcome == EBattleTargetResolutionOutcome::Invalid)
	{
		Rejection.Reason = EBattleRejectionReason::InvalidDecision;
		return MakeRejectedResolution(
			*State,
			ResolutionId,
			Rejection,
			EBattleEventType::ActionCanceled,
			EBattleEventCause::Targeting,
			EBattleActionKind::Fight,
			FallbackSource);
	}

	const uint64 BeforeStateVersion = State->StateVersion;
	Action->TargetResolution = TargetResolution;
	TArray<FBattleEvent> Events;
	Events.Add(MakeBattleEngineTargetsResolvedEvent(
		*State,
		ResolutionId,
		*Action,
		TargetResolution));
	if (TargetResolution.Outcome == EBattleTargetResolutionOutcome::NoLegalTarget)
	{
		if (IsReleasingCharge(
				*State,
				*User,
				Action->Decision.GetMoveId()))
		{
			const bool bChargeCleared = TryClearChargeState(
				*State,
				User->BattlerId,
				EBattleTriggerCleanupReason::Removal);
			check(bChargeCleared);
		}
		Action->bFinished = true;
		Events.Add(MakeActionDetailEvent(
			*State,
			ResolutionId,
			*Action,
			EBattleEventType::ActionCanceled,
			EBattleEventCause::Targeting));
		Events.Add(MakeActionDetailEvent(
			*State,
			ResolutionId,
			*Action,
			EBattleEventType::ActionCompleted,
			EBattleEventCause::Action));
		++State->CurrentLockedActionIndex;
		AppendPostActionBoundaryEvents(*State, ResolutionId, *Action, Events);
	}

	++State->StateVersion;
	FBattleResolutionSpec ResolutionSpec;
	ResolutionSpec.ResolutionId = ResolutionId;
	ResolutionSpec.BeforeStateVersion = BeforeStateVersion;
	ResolutionSpec.AfterStateVersion = State->StateVersion;
	ResolutionSpec.bAccepted = true;
	ResolutionSpec.Events = MoveTemp(Events);
	FBattleResolution Resolution;
	const bool bResolutionCreated = FBattleResolution::TryCreate(ResolutionSpec, Resolution);
	check(bResolutionCreated);
	State->AppendResolution(Resolution);
	return Resolution;
}

FBattleResolution FBattleEngine::ExecuteCurrentMoveEffects()
{
	check(State.IsValid());
	const FResolutionId ResolutionId = TakeResolutionId(*State);
	FBattleLockedActionState* Action = State->LockedActions.IsValidIndex(
		State->CurrentLockedActionIndex)
		? &State->LockedActions[State->CurrentLockedActionIndex]
		: nullptr;
	const FBattleEventSource FallbackSource = Action != nullptr
		? SourceFromLockedAction(*State, *Action)
		: FindFallbackSource(*State);

	FBattleRejection Rejection;
	if (State->Phase == EBattlePhase::Terminal)
	{
		Rejection.Reason = EBattleRejectionReason::TerminalState;
	}
	else if (State->Phase != EBattlePhase::Resolving
		|| Action == nullptr
		|| !Action->bStarted
		|| Action->bFinished
		|| !Action->bMoveCommitted
		|| !Action->TargetResolution.IsSet()
		|| Action->TargetResolution.GetValue().Outcome
			!= EBattleTargetResolutionOutcome::Resolved
		|| Action->Decision.GetActionKind() != EBattleActionKind::Fight
		|| Action->EffectExecutionState != EBattleLockedEffectExecutionState::Pending)
	{
		Rejection.Reason = EBattleRejectionReason::IllegalAction;
		if (Action != nullptr)
		{
			Rejection.ActionId = Action->ActionId;
		}
	}

	const FBattleBattlerState* User = Action != nullptr
		? State->FindBattler(Action->Decision.GetActingBattlerId())
		: nullptr;
	const FBattleMoveDefinition* Move = nullptr;
	if (!Rejection.IsRejected() && User == nullptr)
	{
		Rejection.Reason = EBattleRejectionReason::WrongActingBattler;
		Rejection.BattlerId = Action->Decision.GetActingBattlerId();
	}
	if (!Rejection.IsRejected())
	{
		const FMoveId MoveId = Action->Decision.GetMoveId();
		Move = MoveId == FBattleBuiltInMoveDefinitions::GetStruggleMoveId()
			? &FBattleBuiltInMoveDefinitions::GetStruggle()
			: State->Catalog.FindMove(MoveId);
		if (Move == nullptr)
		{
			Rejection.Reason = EBattleRejectionReason::IllegalMove;
		}
	}

	if (Rejection.IsRejected())
	{
		return MakeRejectedResolution(
			*State,
			ResolutionId,
			Rejection,
			EBattleEventType::ActionCanceled,
			EBattleEventCause::Move,
			EBattleActionKind::Fight,
			FallbackSource);
	}

	check(Action != nullptr && User != nullptr && Move != nullptr);
	FMoveId StoredChargeMoveId;
	const bool bWasChargedRelease = HasVolatile(
			*User,
			FBattleVolatileRules::GetChargingId())
		&& TryGetVolatilePayloadMoveId(
			*State,
			User->BattlerId,
			FBattleVolatileRules::GetChargingId(),
			StoredChargeMoveId)
		&& StoredChargeMoveId == Move->Id;
	Action->EffectExecutionState = EBattleLockedEffectExecutionState::Executing;

	FBattleEffectExecutionRequest Request;
	Request.BattleId = State->Setup.GetBattleId();
	Request.TurnId = State->TurnId;
	Request.ActionId = Action->ActionId;
	Request.ResolutionId = ResolutionId;
	Request.UserBattlerId = User->BattlerId;
	Request.UserSlotId = Action->OrderKey.ActingSlotId;
	Request.Move = Move;
	Request.Targets = Action->TargetResolution.GetValue().Targets;

	FBattleEffectExecutionResult EffectResult;
	EBattleEffectExecutorError EffectError = EBattleEffectExecutorError::None;
	if (!FBattleEffectExecutor::TryExecuteAgainstState(
		Request,
		*State,
		EffectResult,
		EffectError))
	{
		Action->EffectExecutionState = EBattleLockedEffectExecutionState::Pending;
		Rejection.Reason = EffectError == EBattleEffectExecutorError::InvalidMoveDefinition
			? EBattleRejectionReason::IllegalMove
			: EBattleRejectionReason::InvalidDecision;
		Rejection.ActionId = Action->ActionId;
		return MakeRejectedResolution(
			*State,
			ResolutionId,
			Rejection,
			EBattleEventType::ActionCanceled,
			EBattleEventCause::Move,
			EBattleActionKind::Fight,
			FallbackSource);
	}
	if (bWasChargedRelease)
	{
		const bool bChargeCleared = TryClearChargeState(
			*State,
			User->BattlerId,
			EBattleTriggerCleanupReason::Removal);
		check(bChargeCleared);
	}

	const uint64 BeforeStateVersion = State->StateVersion;
	TMap<FBattlerId, FConditionId> PendingFaintStatuses;
	TMap<FBattlerId, TArray<FConditionId>> PendingFaintVolatiles;
	for (const FBattleBattlerState& Candidate : State->Battlers)
	{
		if (!Candidate.bFaintTransitionPending)
		{
			continue;
		}
		if (FBattleMajorStatusRules::IsCanonical(Candidate.MajorStatusId))
		{
			PendingFaintStatuses.Add(Candidate.BattlerId, Candidate.MajorStatusId);
		}
		TArray<FConditionId>& VolatileIds = PendingFaintVolatiles.FindOrAdd(
			Candidate.BattlerId);
		for (const FBattleConditionState& Condition : Candidate.Volatiles)
		{
			if (FBattleVolatileRules::IsCanonical(Condition.ConditionId))
			{
				VolatileIds.Add(Condition.ConditionId);
			}
		}
	}
	for (const TPair<FBattlerId, TArray<FConditionId>>& Pending : PendingFaintVolatiles)
	{
		FBattleBattlerState* PendingBattler = State->FindMutableBattler(Pending.Key);
		if (PendingBattler != nullptr)
		{
			PendingBattler->LastMoveId = FMoveId();
		}
		const bool bSourceEffectsCleaned = TryCleanupSourceDependentVolatiles(
			*State,
			Pending.Key,
			EBattleTriggerCleanupReason::Removal);
		check(bSourceEffectsCleaned);
	}
	FBattleFaintOutcomeResolution FaintResolution;
	const bool bFaintsResolved = FBattleFaintOutcomeResolver::TryResolveAction(
		EffectResult,
		Action->TargetClass,
		ResolutionId,
		*State,
		FaintResolution);
	if (!bFaintsResolved)
	{
		UE_LOG(
			LogTemp,
			Fatal,
			TEXT("C05C faint resolution disagreed with already committed effect state."));
	}
	for (const FBattleFaintTransitionRecord& Removal : FaintResolution.Removals)
	{
		const FConditionId* StatusId = PendingFaintStatuses.Find(Removal.Target.BattlerId);
		if (StatusId != nullptr)
		{
			const bool bCleaned = TryCleanupMajorStatusTriggers(
				*State,
				*StatusId,
				Removal.Target.BattlerId,
				EBattleTriggerCleanupReason::Faint);
			check(bCleaned);
		}
		if (const TArray<FConditionId>* VolatileIds = PendingFaintVolatiles.Find(
			Removal.Target.BattlerId))
		{
			for (const FConditionId& VolatileId : *VolatileIds)
			{
				const bool bCleaned = TryCleanupVolatileTriggers(
					*State,
					VolatileId,
					Removal.Target.BattlerId,
					EBattleTriggerCleanupReason::Faint);
				check(bCleaned);
			}
		}
	}
	if (FaintResolution.bBattleEnded)
	{
		const bool bBattleEndCleaned = TryCleanupBattleEndTriggers(*State);
		check(bBattleEndCleaned);
	}
	const uint64 AfterStateVersion = State->StateVersion + 1;
	check(AfterStateVersion != 0);
	TOptional<FBattleDecisionRequest> PivotRequest;
	if (!FaintResolution.bBattleEnded)
	{
		for (FBattleSwitchEffectIntent& Intent : EffectResult.SwitchIntents)
		{
			if (Intent.Kind != EBattleSwitchKind::Pivot)
			{
				continue;
			}
			FBattleDecisionRequest CandidateRequest;
			if (!PivotRequest.IsSet()
				&& TryBuildPivotDecisionRequest(
					*State,
					*Action,
					AfterStateVersion,
					CandidateRequest))
			{
				PivotRequest = MoveTemp(CandidateRequest);
			}
			else
			{
				Intent.BlockReason = EBattleSwitchBlockReason::NoLegalReserve;
			}
		}
	}
	else
	{
		for (FBattleSwitchEffectIntent& Intent : EffectResult.SwitchIntents)
		{
			if (Intent.Kind == EBattleSwitchKind::Pivot)
			{
				Intent.BlockReason = EBattleSwitchBlockReason::ActingBattlerUnavailable;
			}
		}
	}

	TArray<FBattleEvent> Events;
	Events.Reserve(
		EffectResult.Events.Num()
		+ EffectResult.SwitchIntents.Num() * 3
		+ FaintResolution.Faints.Num()
		+ FaintResolution.Removals.Num() * 3
		+ 3);
	for (int32 EventIndex = 0; EventIndex < EffectResult.Events.Num(); ++EventIndex)
	{
		FBattleEffectExecutionEvent Record = EffectResult.Events[EventIndex];
		TOptional<uint64> SimultaneousGroupId;
		if (const uint64* GroupId = FaintResolution.SimultaneousGroupsByEffectEvent.Find(EventIndex))
		{
			SimultaneousGroupId = *GroupId;
		}
		const FBattleSwitchEffectIntent* SwitchIntent =
			EffectResult.SwitchIntents.FindByPredicate(
				[EventIndex](const FBattleSwitchEffectIntent& Candidate)
				{
					return Candidate.EffectEventIndex == EventIndex;
				});
		if (SwitchIntent != nullptr && SwitchIntent->bApplied)
		{
			AppendSwitchTransitionEvents(
				*State,
				ResolutionId,
				*Action,
				SwitchIntent->OutgoingTarget,
				SwitchIntent->IncomingTarget,
				Events);
		}
		else
		{
			if (SwitchIntent != nullptr
				&& SwitchIntent->BlockReason != EBattleSwitchBlockReason::None)
			{
				Record.Type = EBattleEventType::EffectFailed;
				Record.Outcome = EBattleEffectExecutionOutcome::Failed;
			}
			Events.Add(MakeBattleEffectEvent(
				*State,
				ResolutionId,
				*Action,
				Record,
				SimultaneousGroupId));
		}

		const FBattleFaintTransitionRecord* Faint = FaintResolution.Faints.FindByPredicate(
			[EventIndex](const FBattleFaintTransitionRecord& Candidate)
			{
				return Candidate.EffectEventIndex == EventIndex;
			});
		if (Faint != nullptr)
		{
			const FBattleEventSource* FaintSource = Record.SourceOverride.IsSet()
				? &Record.SourceOverride.GetValue()
				: nullptr;
			Events.Add(MakeTargetedActionEvent(
				*State,
				ResolutionId,
				*Action,
				EBattleEventType::Fainted,
				Record.Cause,
				Faint->Target,
				EBattleOutcomeCause::None,
				Faint->SimultaneousGroupId,
				Faint->HitIndex,
				Faint->HitCount,
				FaintSource));
		}
	}

	for (const FBattleFaintTransitionRecord& Removal : FaintResolution.Removals)
	{
		const FBattleEffectExecutionEvent* RemovalRecord =
			EffectResult.Events.IsValidIndex(Removal.EffectEventIndex)
				? &EffectResult.Events[Removal.EffectEventIndex]
				: nullptr;
		const FBattleEventSource* RemovalSource = RemovalRecord != nullptr
			&& RemovalRecord->SourceOverride.IsSet()
				? &RemovalRecord->SourceOverride.GetValue()
				: nullptr;
		Events.Add(MakeTargetedActionEvent(
			*State,
			ResolutionId,
			*Action,
			EBattleEventType::LeftActiveSlot,
			EBattleEventCause::Rule,
			Removal.Target,
			EBattleOutcomeCause::None,
			Removal.SimultaneousGroupId,
			TOptional<uint16>(),
			TOptional<uint16>(),
			RemovalSource));
		Events.Add(MakeTargetedActionEvent(
			*State,
			ResolutionId,
			*Action,
			EBattleEventType::Removed,
			EBattleEventCause::Rule,
			Removal.Target,
			EBattleOutcomeCause::None,
			Removal.SimultaneousGroupId,
			TOptional<uint16>(),
			TOptional<uint16>(),
			RemovalSource));
		if (Removal.Target.ActiveSlotId.GetSide() == EBattleSide::Opponent)
		{
			FBattleEvent Checkpoint = MakeTargetedActionEvent(
				*State,
				ResolutionId,
				*Action,
				EBattleEventType::OpponentRemovalCheckpoint,
				EBattleEventCause::Rule,
				Removal.Target,
				EBattleOutcomeCause::None,
				Removal.SimultaneousGroupId,
				TOptional<uint16>(),
				TOptional<uint16>(),
				RemovalSource);
			State->AvailableOpponentRemovalCheckpoints.Add(Checkpoint.GetEventOrdinal());
			Events.Add(MoveTemp(Checkpoint));
		}
	}

	if (PivotRequest.IsSet())
	{
		Action->EffectExecutionState = EBattleLockedEffectExecutionState::AwaitingPivot;
		State->PendingDecision = PivotRequest.GetValue();
		State->PendingDecisionRequests.Reset();
		State->PendingDecisionRequests.Add(PivotRequest.GetValue());
	}
	else
	{
		Action->EffectExecutionState = EBattleLockedEffectExecutionState::Completed;
		Action->bFinished = true;
		Events.Add(MakeActionDetailEvent(
			*State,
			ResolutionId,
			*Action,
			EBattleEventType::ActionCompleted,
			EBattleEventCause::Action));
		++State->CurrentLockedActionIndex;
		if (FaintResolution.bBattleEnded)
		{
			const FBattleEventSource* BattleEndSource = nullptr;
			for (const FBattleFaintTransitionRecord& Faint : FaintResolution.Faints)
			{
				if (EffectResult.Events.IsValidIndex(Faint.EffectEventIndex)
					&& EffectResult.Events[Faint.EffectEventIndex].SourceOverride.IsSet())
				{
					BattleEndSource = &EffectResult.Events[Faint.EffectEventIndex]
						.SourceOverride.GetValue();
					break;
				}
			}
			Events.Add(MakeBattleEndedEvent(
				*State,
				ResolutionId,
				*Action,
				FaintResolution.OutcomeCause,
				BattleEndSource));
		}
		else
		{
			AppendPostActionBoundaryEvents(*State, ResolutionId, *Action, Events);
		}
	}
	State->StateVersion = AfterStateVersion;

	EBattleStateValidationError StateError = EBattleStateValidationError::None;
	const bool bStateValid = State->ValidateInvariants(StateError);
	check(bStateValid);

	FBattleResolutionSpec ResolutionSpec;
	ResolutionSpec.ResolutionId = ResolutionId;
	ResolutionSpec.BeforeStateVersion = BeforeStateVersion;
	ResolutionSpec.AfterStateVersion = State->StateVersion;
	ResolutionSpec.bAccepted = true;
	ResolutionSpec.Events = MoveTemp(Events);
	FBattleResolution Resolution;
	const bool bResolutionCreated = FBattleResolution::TryCreate(ResolutionSpec, Resolution);
	check(bResolutionCreated);
	State->AppendResolution(Resolution);
	return Resolution;
}

FBattleResolution FBattleEngine::ResolveEndTurn()
{
	check(State.IsValid());
	const FResolutionId ResolutionId = TakeResolutionId(*State);
	const FBattleEventSource FallbackSource = FindFallbackSource(*State);
	FBattleRejection Rejection;
	if (State->Phase == EBattlePhase::Terminal)
	{
		Rejection.Reason = EBattleRejectionReason::TerminalState;
	}
	else if (State->Phase != EBattlePhase::EndOfTurn)
	{
		Rejection.Reason = EBattleRejectionReason::IllegalAction;
	}
	if (Rejection.IsRejected())
	{
		return MakeRejectedResolution(
			*State,
			ResolutionId,
			Rejection,
			EBattleEventType::ActionCanceled,
			EBattleEventCause::Rule,
			EBattleActionKind::Residual,
			FallbackSource);
	}

	const uint64 BeforeStateVersion = State->StateVersion;
	const uint64 AfterStateVersion = BeforeStateVersion + 1;
	check(AfterStateVersion != 0);
	TArray<FBattleEvent> Events;

	if (!State->bEndTurnTriggerPassComplete)
	{
		TSet<FBattlerId> SuppressedProtectAtStart;
		for (const FBattleBattlerState& Battler : State->Battlers)
		{
			if (HasVolatile(Battler, FBattleVolatileRules::GetProtectId())
				&& IsVolatileSuppressed(
					*State,
					Battler.BattlerId,
					FBattleVolatileRules::GetProtectId()))
			{
				SuppressedProtectAtStart.Add(Battler.BattlerId);
			}
		}
		FBattleTriggerDispatchSpec Dispatch;
		Dispatch.Phase = EBattleTriggerPhase::EndTurn;
		for (const FBattleTriggerRegistrationState& Registration :
			State->TriggerFramework.GetActiveRegistrations())
		{
			if (Registration.Spec.Rule.Phase != EBattleTriggerPhase::EndTurn
				|| Registration.Spec.SourceDefinition.Kind
					!= EBattleTriggerSourceDefinitionKind::Condition)
			{
				continue;
			}
			const FConditionId ConditionId =
				Registration.Spec.SourceDefinition.ConditionId;
			const bool bFieldSideOwner =
				Registration.Spec.Owner.Kind == EBattleTriggerSubjectKind::Field
				|| Registration.Spec.Owner.Kind == EBattleTriggerSubjectKind::Side;
			if (bFieldSideOwner)
			{
				if (!FBattleFieldSideConditionRules::IsCanonical(ConditionId)
					|| FindFieldSideCondition(
						*State,
						Registration.Spec.Owner,
						ConditionId) == nullptr)
				{
					continue;
				}
				FBattleTriggerDispatchParticipant& Participant =
					Dispatch.Participants.AddDefaulted_GetRef();
				Participant.RegistrationId = Registration.RegistrationId;
			}
			else
			{
				if (Registration.Spec.Owner.Kind != EBattleTriggerSubjectKind::Battler)
				{
					continue;
				}
				const FBattleBattlerState* Battler = State->FindBattler(
					Registration.Spec.Owner.BattlerId);
				const FBattleActivePositionState* Active = Battler != nullptr
					? FindActiveForBattler(*State, Battler->BattlerId)
					: nullptr;
				const bool bSourceMatchesBattler = Battler != nullptr
					&& (ConditionId == Battler->MajorStatusId
						|| HasVolatile(*Battler, ConditionId));
				if (Battler == nullptr
					|| Active == nullptr
					|| Battler->CurrentHP <= 0
					|| Battler->bFainted
					|| Battler->bCaptured
					|| Battler->bRemoved
					|| !bSourceMatchesBattler)
				{
					continue;
				}
				FBattleTriggerDispatchParticipant& Participant =
					Dispatch.Participants.AddDefaulted_GetRef();
				Participant.RegistrationId = Registration.RegistrationId;
				Participant.ActiveSlotId = Active->ActiveSlotId;
			}
			if (!Dispatch.DurationTickOwners.Contains(Registration.Spec.Owner))
			{
				Dispatch.DurationTickOwners.Add(Registration.Spec.Owner);
			}
		}

		TArray<FBattleTriggerEffectRequest> ResidualRequests;
		TArray<FBattleTriggerLifecycleFact> ResidualLifecycleFacts;
		FBattleTriggerOperationContext ResidualOperation;
		if (!Dispatch.Participants.IsEmpty())
		{
			const bool bTokenCreated = TryTakeTriggerOperationContext(
				*State,
				ResidualOperation);
			check(bTokenCreated);
			Dispatch.ReentrancyToken = ResidualOperation.ReentrancyToken;
			EBattleTriggerError TriggerError = EBattleTriggerError::None;
			FBattleTriggerDispatchResult DispatchResult;
			const bool bDispatched = State->TriggerFramework.TryEnqueueDispatch(
				Dispatch,
				TriggerError)
				&& State->TriggerFramework.TryResolveNextDispatch(
					DispatchResult,
					TriggerError);
			check(bDispatched);
			State->TriggerFramework.DrainEffectRequests(ResidualRequests);
			State->TriggerFramework.DrainLifecycleFacts(ResidualLifecycleFacts);
			if (DispatchResult.bQueuedExpiryDispatch)
			{
				FBattleTriggerDispatchResult ExpiryResult;
				const bool bExpiryResolved = State->TriggerFramework.TryResolveNextDispatch(
					ExpiryResult,
					TriggerError);
				check(bExpiryResolved);
				TArray<FBattleTriggerEffectRequest> ExpiryRequests;
				TArray<FBattleTriggerLifecycleFact> ExpiryFacts;
				State->TriggerFramework.DrainEffectRequests(ExpiryRequests);
				State->TriggerFramework.DrainLifecycleFacts(ExpiryFacts);
				ResidualRequests.Append(MoveTemp(ExpiryRequests));
				ResidualLifecycleFacts.Append(MoveTemp(ExpiryFacts));
			}
		}

		auto ApplyVolatileResidual = [
			this,
			&Events,
			&ResolutionId](
			FBattleBattlerState& TargetBattler,
			const FBattleActivePositionState& TargetActive,
			const FConditionId& ConditionId,
			const int32 Damage,
			FBattleBattlerState* HealRecipient,
			const FBattleActivePositionState* HealActive,
			const int32 HealAmount,
			const FBattleEventSource* SourceOverride)
		{
			const int32 PreviousHP = TargetBattler.CurrentHP;
			const int32 AppliedDamage = FMath::Min(PreviousHP, Damage);
			TargetBattler.CurrentHP -= AppliedDamage;
			if (TargetBattler.CurrentHP == 0)
			{
				TargetBattler.bFainted = true;
				TargetBattler.bFaintTransitionPending = true;
			}

			FBattleEventSource Source = SourceOverride != nullptr
				? *SourceOverride
				: FBattleEventSource();
			if (SourceOverride == nullptr)
			{
				Source.TrainerId = TargetBattler.TrainerId;
				Source.BattlerId = TargetBattler.BattlerId;
				Source.ActiveSlotId = TargetActive.ActiveSlotId;
			}
			Source.DefinitionId = ConditionId.GetDefinitionId();
			FBattleEventTarget Target;
			Target.TrainerId = TargetBattler.TrainerId;
			Target.BattlerId = TargetBattler.BattlerId;
			Target.ActiveSlotId = TargetActive.ActiveSlotId;
			Events.Add(MakeResidualMutationEvent(
				*State,
				ResolutionId,
				EBattleEventType::Damage,
				Source,
				Target,
				PreviousHP,
				TargetBattler.CurrentHP,
				-AppliedDamage));
			Events.Add(MakeResidualMutationEvent(
				*State,
				ResolutionId,
				EBattleEventType::HPChanged,
				Source,
				Target,
				PreviousHP,
				TargetBattler.CurrentHP,
				-AppliedDamage));

			FBattleEffectExecutionResult EffectResult;
			EffectResult.bValid = true;
			for (const EBattleEventType Type : {
				EBattleEventType::Damage,
				EBattleEventType::HPChanged})
			{
				FBattleEffectExecutionEvent& Record =
					EffectResult.Events.AddDefaulted_GetRef();
				Record.Type = Type;
				Record.Cause = EBattleEventCause::Rule;
				Record.Outcome = EBattleEffectExecutionOutcome::Applied;
				Record.Targets.Add(Target);
				Record.NumericBefore = PreviousHP;
				Record.NumericAfter = TargetBattler.CurrentHP;
				Record.NumericDelta = -AppliedDamage;
			}

			const FConditionId PendingStatus = TargetBattler.MajorStatusId;
			TArray<FConditionId> PendingVolatiles;
			for (const FBattleConditionState& Condition : TargetBattler.Volatiles)
			{
				if (FBattleVolatileRules::IsCanonical(Condition.ConditionId))
				{
					PendingVolatiles.Add(Condition.ConditionId);
				}
			}
			if (TargetBattler.bFaintTransitionPending)
			{
				TargetBattler.LastMoveId = FMoveId();
				const bool bSourceEffectsCleaned = TryCleanupSourceDependentVolatiles(
					*State,
					TargetBattler.BattlerId,
					EBattleTriggerCleanupReason::Removal);
				check(bSourceEffectsCleaned);
			}

			FBattleFaintOutcomeResolution FaintResolution;
			const bool bFaintsResolved = FBattleFaintOutcomeResolver::TryResolveAction(
				EffectResult,
				EBattleTargetClass::SelectedOpponent,
				ResolutionId,
				*State,
				FaintResolution);
			check(bFaintsResolved);
			if (!FaintResolution.Removals.IsEmpty())
			{
				if (FBattleMajorStatusRules::IsCanonical(PendingStatus))
				{
					const bool bCleaned = TryCleanupMajorStatusTriggers(
						*State,
						PendingStatus,
						Target.BattlerId,
						EBattleTriggerCleanupReason::Faint);
					check(bCleaned);
				}
				for (const FConditionId& VolatileId : PendingVolatiles)
				{
					const bool bCleaned = TryCleanupVolatileTriggers(
						*State,
						VolatileId,
						Target.BattlerId,
						EBattleTriggerCleanupReason::Faint);
					check(bCleaned);
				}
			}
			for (const FBattleFaintTransitionRecord& Faint : FaintResolution.Faints)
			{
				Events.Add(MakeTargetedActionlessEvent(
					*State,
					ResolutionId,
					EBattleEventType::Fainted,
					EBattleEventCause::Rule,
					EBattleActionKind::Residual,
					Source,
					Faint.Target));
			}
			for (const FBattleFaintTransitionRecord& Removal : FaintResolution.Removals)
			{
				Events.Add(MakeTargetedActionlessEvent(
					*State,
					ResolutionId,
					EBattleEventType::LeftActiveSlot,
					EBattleEventCause::Rule,
					EBattleActionKind::Residual,
					Source,
					Removal.Target));
				Events.Add(MakeTargetedActionlessEvent(
					*State,
					ResolutionId,
					EBattleEventType::Removed,
					EBattleEventCause::Rule,
					EBattleActionKind::Residual,
					Source,
					Removal.Target));
				if (Removal.Target.ActiveSlotId.GetSide() == EBattleSide::Opponent)
				{
					FBattleEvent Checkpoint = MakeTargetedActionlessEvent(
						*State,
						ResolutionId,
						EBattleEventType::OpponentRemovalCheckpoint,
						EBattleEventCause::Rule,
						EBattleActionKind::Residual,
						Source,
						Removal.Target);
					State->AvailableOpponentRemovalCheckpoints.Add(
						Checkpoint.GetEventOrdinal());
					Events.Add(MoveTemp(Checkpoint));
				}
			}
			if (HealRecipient != nullptr && HealActive != nullptr && HealAmount > 0)
			{
				const int32 PreviousRecipientHP = HealRecipient->CurrentHP;
				const int32 AppliedHeal = FMath::Min(
					HealAmount,
					HealRecipient->PermanentStats.MaxHP - HealRecipient->CurrentHP);
				HealRecipient->CurrentHP += AppliedHeal;
				FBattleEventTarget RecipientTarget;
				RecipientTarget.TrainerId = HealRecipient->TrainerId;
				RecipientTarget.BattlerId = HealRecipient->BattlerId;
				RecipientTarget.ActiveSlotId = HealActive->ActiveSlotId;
				Events.Add(MakeResidualMutationEvent(
					*State,
					ResolutionId,
					EBattleEventType::Healing,
					Source,
					RecipientTarget,
					PreviousRecipientHP,
					HealRecipient->CurrentHP,
					AppliedHeal));
				Events.Add(MakeResidualMutationEvent(
					*State,
					ResolutionId,
					EBattleEventType::HPChanged,
					Source,
					RecipientTarget,
					PreviousRecipientHP,
					HealRecipient->CurrentHP,
					AppliedHeal));
			}
			if (FaintResolution.bBattleEnded)
			{
				Events.Add(MakeEvent(
					*State,
					ResolutionId,
					FActionId(),
					EBattleEventType::BattleEnded,
					EBattleEventCause::Outcome,
					EBattleActionKind::Residual,
					FaintResolution.OutcomeCause,
					Source));
				return false;
			}
			return true;
		};

		auto ApplyFieldHealing = [
			this,
			&Events,
			&ResolutionId](
			FBattleBattlerState& TargetBattler,
			const FBattleActivePositionState& TargetActive,
			const FConditionId& ConditionId,
			const int32 HealAmount)
		{
			const int32 PreviousHP = TargetBattler.CurrentHP;
			const int32 AppliedHeal = FMath::Min(
				HealAmount,
				TargetBattler.PermanentStats.MaxHP - PreviousHP);
			if (AppliedHeal <= 0)
			{
				return;
			}
			TargetBattler.CurrentHP += AppliedHeal;

			FBattleTriggerSubject FieldOwner = FBattleTriggerSubject::CreateField();
			const FBattleEventSource Source = BuildFieldSideConditionSource(
				*State,
				FieldOwner,
				ConditionId);

			FBattleEventTarget Target;
			Target.TrainerId = TargetBattler.TrainerId;
			Target.BattlerId = TargetBattler.BattlerId;
			Target.ActiveSlotId = TargetActive.ActiveSlotId;
			for (const EBattleEventType Type : {
				EBattleEventType::Healing,
				EBattleEventType::HPChanged})
			{
				Events.Add(MakeResidualMutationEvent(
					*State,
					ResolutionId,
					Type,
					Source,
					Target,
					PreviousHP,
					TargetBattler.CurrentHP,
					AppliedHeal));
			}
		};

		for (const FBattleTriggerEffectRequest& Request : ResidualRequests)
		{
			if (State->Phase == EBattlePhase::Terminal)
			{
				break;
			}
			if (Request.SourceDefinition.Kind
				!= EBattleTriggerSourceDefinitionKind::Condition)
			{
				continue;
			}
			const FConditionId RequestConditionId =
				Request.SourceDefinition.ConditionId;
			const bool bFieldSideOwner =
				Request.Owner.Kind == EBattleTriggerSubjectKind::Field
				|| Request.Owner.Kind == EBattleTriggerSubjectKind::Side;
			if (bFieldSideOwner)
			{
				FBattleConditionState* Condition = FindMutableFieldSideCondition(
					*State,
					Request.Owner,
					RequestConditionId);
				if (Condition == nullptr)
				{
					continue;
				}
				if (Request.RemainingTurns.IsSet())
				{
					Condition->RemainingTurns = Request.RemainingTurns;
				}

				FBattleTriggerEffectId ResidualEffectId;
				if (!FBattleFieldSideConditionRules::TryGetTriggerEffectId(
						RequestConditionId,
						EBattleTriggerPhase::EndTurn,
						ResidualEffectId)
					|| Request.EffectId != ResidualEffectId)
				{
					continue;
				}
				const FBattleEventSource FieldSource = BuildFieldSideConditionSource(
					*State,
					Request.Owner,
					RequestConditionId);

				TArray<FActiveSlotId> ActiveSlotIds;
				for (const FBattleActivePositionState& Position : State->ActivePositions)
				{
					if (Position.bAvailable && Position.BattlerId.IsValid())
					{
						ActiveSlotIds.Add(Position.ActiveSlotId);
					}
				}
				ActiveSlotIds.Sort(
					[](const FActiveSlotId& Left, const FActiveSlotId& Right)
					{
						return ActiveSlotLess(Left, Right);
					});
				for (const FActiveSlotId ActiveSlotId : ActiveSlotIds)
				{
					const FBattleActivePositionState* FieldActive =
						State->FindActivePosition(ActiveSlotId);
					FBattleBattlerState* FieldBattler = FieldActive != nullptr
						? State->FindMutableBattler(FieldActive->BattlerId)
						: nullptr;
					const FBattleSpeciesFormDefinition* Species = FieldBattler != nullptr
						? State->Catalog.FindSpeciesForm(FieldBattler->SpeciesFormId)
						: nullptr;
					if (FieldActive == nullptr
						|| FieldBattler == nullptr
						|| Species == nullptr
						|| FieldBattler->CurrentHP <= 0
						|| FieldBattler->bFainted
						|| FieldBattler->bCaptured
						|| FieldBattler->bRemoved)
					{
						continue;
					}

					bool bGrounded = false;
					if (!TryResolveGrounded(*State, *FieldBattler, bGrounded))
					{
						continue;
					}
					FBattleFieldResidualFacts Facts;
					Facts.ConditionId = RequestConditionId;
					Facts.BaseMaximumHP = FieldBattler->PermanentStats.MaxHP;
					Facts.CurrentHP = FieldBattler->CurrentHP;
					Facts.PrimaryType = Species->PrimaryType;
					Facts.SecondaryType = Species->SecondaryType;
					Facts.bGrounded = bGrounded;
					FBattleFieldResidualResult Result;
					if (!FBattleFieldSideConditionRules::TryResolveFieldResidual(
						Facts,
						Result))
					{
						continue;
					}
					if (Result.EffectKind == EBattleFieldResidualEffectKind::Damage)
					{
						if (!ApplyVolatileResidual(
							*FieldBattler,
							*FieldActive,
							RequestConditionId,
							Result.Amount,
							nullptr,
							nullptr,
							0,
							&FieldSource))
						{
							break;
						}
					}
					else if (Result.EffectKind == EBattleFieldResidualEffectKind::Heal)
					{
						ApplyFieldHealing(
							*FieldBattler,
							*FieldActive,
							RequestConditionId,
							Result.Amount);
					}
				}
				continue;
			}
			if (Request.Owner.Kind != EBattleTriggerSubjectKind::Battler)
			{
				continue;
			}
			FBattleBattlerState* Battler = State->FindMutableBattler(Request.Owner.BattlerId);
			const FBattleActivePositionState* Active = Battler != nullptr
				? FindActiveForBattler(*State, Battler->BattlerId)
				: nullptr;
			const FConditionId StatusId = RequestConditionId;
			const bool bMajorStatusRequest = Battler != nullptr
				&& Battler->MajorStatusId == StatusId
				&& FBattleMajorStatusRules::IsCanonical(StatusId);
			const bool bVolatileRequest = Battler != nullptr
				&& HasVolatile(*Battler, StatusId)
				&& FBattleVolatileRules::IsCanonical(StatusId);
			if (Battler == nullptr
				|| Active == nullptr
				|| (!bMajorStatusRequest && !bVolatileRequest)
				|| Battler->CurrentHP <= 0
				|| Battler->bFainted
				|| Battler->bCaptured
				|| Battler->bRemoved)
			{
				continue;
			}
			if (bVolatileRequest)
			{
				FBattleConditionState* Condition = FindMutableVolatile(*Battler, StatusId);
				check(Condition != nullptr);
				if (Request.RemainingTurns.IsSet())
				{
					Condition->RemainingTurns = Request.RemainingTurns;
				}

				auto RemoveCurrentVolatile = [&]()
				{
					const bool bCleaned = TryCleanupVolatileTriggers(
						*State,
						StatusId,
						Battler->BattlerId,
						EBattleTriggerCleanupReason::Removal);
					check(bCleaned);
					Battler->Volatiles.RemoveAll(
						[&StatusId](const FBattleConditionState& Candidate)
						{
							return Candidate.ConditionId == StatusId;
						});
				};

				if (StatusId == FBattleVolatileRules::GetLeechSeedId())
				{
					const FBattleActivePositionState* SourceActive =
						Request.Source.Kind == EBattleTriggerSubjectKind::ActiveSlot
						? State->FindActivePosition(Request.Source.ActiveSlotId)
						: nullptr;
					FBattleBattlerState* Recipient = SourceActive != nullptr
						? State->FindMutableBattler(SourceActive->BattlerId)
						: nullptr;
					const bool bLivingRecipient = Recipient != nullptr
						&& Recipient->CurrentHP > 0
						&& !Recipient->bFainted
						&& !Recipient->bCaptured
						&& !Recipient->bRemoved;
					FBattleLeechSeedResidualFacts Facts;
					Facts.TargetBaseMaximumHP = Battler->PermanentStats.MaxHP;
					Facts.TargetCurrentHP = Battler->CurrentHP;
					Facts.bSourceSlotHasLivingRecipient = bLivingRecipient;
					Facts.RecipientMissingHP = bLivingRecipient
						? Recipient->PermanentStats.MaxHP - Recipient->CurrentHP
						: 0;
					FBattleLeechSeedResidualResult Residual;
					const bool bResolved = FBattleVolatileRules::TryResolveLeechSeedResidual(
						Facts,
						Residual);
					check(bResolved);
					if (Residual.bApplies
						&& !ApplyVolatileResidual(
							*Battler,
							*Active,
							StatusId,
							Residual.RequestedDamage,
							bLivingRecipient ? Recipient : nullptr,
							bLivingRecipient ? SourceActive : nullptr,
							Residual.Heal,
							nullptr))
					{
						break;
					}
				}
				else if (StatusId == FBattleVolatileRules::GetPartialTrapId())
				{
					const FBattleBattlerState* SourceBattler =
						Request.Source.Kind == EBattleTriggerSubjectKind::Battler
						? State->FindBattler(Request.Source.BattlerId)
						: nullptr;
					const FBattleActivePositionState* SourceActive = SourceBattler != nullptr
						? FindActiveForBattler(*State, SourceBattler->BattlerId)
						: nullptr;
					FBattlePartialTrapResidualFacts Facts;
					Facts.TargetBaseMaximumHP = Battler->PermanentStats.MaxHP;
					Facts.TargetCurrentHP = Battler->CurrentHP;
					Facts.bBindingSourceActiveAndLiving = SourceBattler != nullptr
						&& SourceActive != nullptr
						&& SourceBattler->CurrentHP > 0
						&& !SourceBattler->bFainted
						&& !SourceBattler->bCaptured
						&& !SourceBattler->bRemoved;
					FBattlePartialTrapResidualResult Residual;
					const bool bResolved = FBattleVolatileRules::TryResolvePartialTrapResidual(
						Facts,
						Residual);
					check(bResolved);
					if (Residual.bEndsEarly)
					{
						RemoveCurrentVolatile();
					}
					else if (Residual.bAppliesDamage
						&& !ApplyVolatileResidual(
							*Battler,
							*Active,
							StatusId,
							Residual.RequestedDamage,
							nullptr,
							nullptr,
							0,
							nullptr))
					{
						break;
					}
				}
				else if (StatusId == FBattleVolatileRules::GetFlinchId())
				{
					RemoveCurrentVolatile();
				}
				else if (StatusId == FBattleVolatileRules::GetProtectId())
				{
					const bool bSuppressed = TrySetVolatileSuppressed(
						*State,
						Battler->BattlerId,
						StatusId,
						true);
					check(bSuppressed);
				}
				else if (StatusId == FBattleVolatileRules::GetEncoreId()
					|| StatusId == FBattleVolatileRules::GetDisableId())
				{
					FMoveId LockedMoveId;
					const bool bPayloadValid = FMoveId::TryCreate(
						Request.PayloadId,
						LockedMoveId)
						&& State->Catalog.FindMove(LockedMoveId) != nullptr
						&& Battler->Moves.ContainsByPredicate(
							[LockedMoveId](const FBattleMoveSlotState& Slot)
							{
								return Slot.MoveId == LockedMoveId
									&& Slot.CurrentPP > 0;
							});
					if (!bPayloadValid)
					{
						RemoveCurrentVolatile();
					}
				}
				continue;
			}

			FBattleMajorStatusResidualFacts ResidualFacts;
			ResidualFacts.StatusId = StatusId;
			ResidualFacts.BaseMaximumHP = Battler->PermanentStats.MaxHP;
			ResidualFacts.ToxicLayerEncoding = Request.Layers;
			FBattleMajorStatusResidualResult Residual;
			const bool bResidualResolved = FBattleMajorStatusRules::TryResolveResidual(
				ResidualFacts,
				Residual);
			check(bResidualResolved && Residual.bAppliesDamage);
			if (StatusId == FBattleMajorStatusRules::GetToxicId())
			{
				const bool bLayersUpdated = TrySetToxicLayers(
					*State,
					Battler->BattlerId,
					Residual.ToxicLayerEncoding,
					ResidualOperation);
				check(bLayersUpdated);
			}

			const int32 PreviousHP = Battler->CurrentHP;
			const int32 AppliedDamage = FMath::Min(PreviousHP, Residual.Damage);
			Battler->CurrentHP -= AppliedDamage;
			if (Battler->CurrentHP == 0)
			{
				Battler->bFainted = true;
				Battler->bFaintTransitionPending = true;
			}

			FBattleEventSource Source;
			Source.TrainerId = Battler->TrainerId;
			Source.BattlerId = Battler->BattlerId;
			Source.ActiveSlotId = Active->ActiveSlotId;
			Source.DefinitionId = StatusId.GetDefinitionId();
			FBattleEventTarget Target;
			Target.TrainerId = Battler->TrainerId;
			Target.BattlerId = Battler->BattlerId;
			Target.ActiveSlotId = Active->ActiveSlotId;
			Events.Add(MakeResidualMutationEvent(
				*State,
				ResolutionId,
				EBattleEventType::Damage,
				Source,
				Target,
				PreviousHP,
				Battler->CurrentHP,
				-AppliedDamage));
			Events.Add(MakeResidualMutationEvent(
				*State,
				ResolutionId,
				EBattleEventType::HPChanged,
				Source,
				Target,
				PreviousHP,
				Battler->CurrentHP,
				-AppliedDamage));

			FBattleEffectExecutionResult EffectResult;
			EffectResult.bValid = true;
			FBattleEffectExecutionEvent& DamageRecord =
				EffectResult.Events.AddDefaulted_GetRef();
			DamageRecord.Type = EBattleEventType::Damage;
			DamageRecord.Cause = EBattleEventCause::Rule;
			DamageRecord.Outcome = EBattleEffectExecutionOutcome::Applied;
			DamageRecord.Targets.Add(Target);
			DamageRecord.NumericBefore = PreviousHP;
			DamageRecord.NumericAfter = Battler->CurrentHP;
			DamageRecord.NumericDelta = -AppliedDamage;
			FBattleEffectExecutionEvent& HpRecord =
				EffectResult.Events.AddDefaulted_GetRef();
			HpRecord.Type = EBattleEventType::HPChanged;
			HpRecord.Cause = EBattleEventCause::Rule;
			HpRecord.Outcome = EBattleEffectExecutionOutcome::Applied;
			HpRecord.Targets.Add(Target);
			HpRecord.NumericBefore = PreviousHP;
			HpRecord.NumericAfter = Battler->CurrentHP;
			HpRecord.NumericDelta = -AppliedDamage;
			TArray<FConditionId> PendingVolatileIds;
			if (Battler->bFaintTransitionPending)
			{
				for (const FBattleConditionState& Condition : Battler->Volatiles)
				{
					if (FBattleVolatileRules::IsCanonical(Condition.ConditionId))
					{
						PendingVolatileIds.Add(Condition.ConditionId);
					}
				}
				Battler->LastMoveId = FMoveId();
				const bool bSourceEffectsCleaned = TryCleanupSourceDependentVolatiles(
					*State,
					Battler->BattlerId,
					EBattleTriggerCleanupReason::Removal);
				check(bSourceEffectsCleaned);
			}

			FBattleFaintOutcomeResolution FaintResolution;
			const bool bFaintsResolved = FBattleFaintOutcomeResolver::TryResolveAction(
				EffectResult,
				EBattleTargetClass::SelectedOpponent,
				ResolutionId,
				*State,
				FaintResolution);
			check(bFaintsResolved);
			if (!FaintResolution.Removals.IsEmpty())
			{
				const bool bCleaned = TryCleanupMajorStatusTriggers(
					*State,
					StatusId,
					Target.BattlerId,
					EBattleTriggerCleanupReason::Faint);
				check(bCleaned);
				for (const FConditionId& VolatileId : PendingVolatileIds)
				{
					const bool bVolatileCleaned = TryCleanupVolatileTriggers(
						*State,
						VolatileId,
						Target.BattlerId,
						EBattleTriggerCleanupReason::Faint);
					check(bVolatileCleaned);
				}
			}
			for (const FBattleFaintTransitionRecord& Faint : FaintResolution.Faints)
			{
				Events.Add(MakeTargetedActionlessEvent(
					*State,
					ResolutionId,
					EBattleEventType::Fainted,
					EBattleEventCause::Rule,
					EBattleActionKind::Residual,
					Source,
					Faint.Target));
			}
			for (const FBattleFaintTransitionRecord& Removal : FaintResolution.Removals)
			{
				Events.Add(MakeTargetedActionlessEvent(
					*State,
					ResolutionId,
					EBattleEventType::LeftActiveSlot,
					EBattleEventCause::Rule,
					EBattleActionKind::Residual,
					Source,
					Removal.Target));
				Events.Add(MakeTargetedActionlessEvent(
					*State,
					ResolutionId,
					EBattleEventType::Removed,
					EBattleEventCause::Rule,
					EBattleActionKind::Residual,
					Source,
					Removal.Target));
				if (Removal.Target.ActiveSlotId.GetSide() == EBattleSide::Opponent)
				{
					FBattleEvent Checkpoint = MakeTargetedActionlessEvent(
						*State,
						ResolutionId,
						EBattleEventType::OpponentRemovalCheckpoint,
						EBattleEventCause::Rule,
						EBattleActionKind::Residual,
						Source,
						Removal.Target);
					State->AvailableOpponentRemovalCheckpoints.Add(
						Checkpoint.GetEventOrdinal());
					Events.Add(MoveTemp(Checkpoint));
				}
			}
			if (FaintResolution.bBattleEnded)
			{
				Events.Add(MakeEvent(
					*State,
					ResolutionId,
					FActionId(),
					EBattleEventType::BattleEnded,
					EBattleEventCause::Outcome,
					EBattleActionKind::Residual,
					FaintResolution.OutcomeCause,
					Source));
				break;
			}
		}
		for (const FBattleTriggerLifecycleFact& Fact : ResidualLifecycleFacts)
		{
			if (Fact.SourceDefinition.Kind
				!= EBattleTriggerSourceDefinitionKind::Condition)
			{
				continue;
			}
			const FConditionId ConditionId = Fact.SourceDefinition.ConditionId;
			const bool bFieldSideOwner = Fact.Owner.Kind == EBattleTriggerSubjectKind::Field
				|| Fact.Owner.Kind == EBattleTriggerSubjectKind::Side;
			if (bFieldSideOwner
				&& FBattleFieldSideConditionRules::IsCanonical(ConditionId))
			{
				FBattleConditionState* Condition = FindMutableFieldSideCondition(
					*State,
					Fact.Owner,
					ConditionId);
				if (Condition != nullptr
					&& Fact.Kind == EBattleTriggerLifecycleFactKind::DurationChanged
					&& Fact.RemainingTurns.IsSet())
				{
					Condition->RemainingTurns = Fact.RemainingTurns;
				}
				const bool bExpired = Fact.Kind == EBattleTriggerLifecycleFactKind::Ended
					&& Fact.EndReason.IsSet()
					&& Fact.EndReason.GetValue() == EBattleTriggerEndReason::Expired;
				if (!bExpired || Condition == nullptr)
				{
					continue;
				}
				const TOptional<EBattleSide> Side =
					Fact.Owner.Kind == EBattleTriggerSubjectKind::Side
						? TOptional<EBattleSide>(Fact.Owner.Side)
						: TOptional<EBattleSide>();
				const bool bCleaned = TryCleanupFieldSideTriggers(
					*State,
					ConditionId,
					Side,
					EBattleTriggerCleanupReason::Removal);
				check(bCleaned);
				if (ConditionId == FBattleFieldSideConditionRules::GetMagicRoomId())
				{
					for (FBattleBattlerState& Battler : State->Battlers)
					{
						Battler.HeldItem.bSuppressed = false;
					}
				}
				const bool bRemoved = RemoveFieldSideConditionState(
					*State,
					Fact.Owner,
					ConditionId);
				check(bRemoved);
				continue;
			}
			if (Fact.Kind != EBattleTriggerLifecycleFactKind::Ended
				|| !Fact.EndReason.IsSet()
				|| Fact.EndReason.GetValue() != EBattleTriggerEndReason::Expired
				|| Fact.Owner.Kind != EBattleTriggerSubjectKind::Battler
				|| !FBattleVolatileRules::IsCanonical(ConditionId))
			{
				continue;
			}
			FBattleBattlerState* Owner = State->FindMutableBattler(Fact.Owner.BattlerId);
			if (Owner == nullptr
				|| !HasVolatile(*Owner, ConditionId))
			{
				continue;
			}
			const bool bCleaned = TryCleanupVolatileTriggers(
				*State,
				ConditionId,
				Owner->BattlerId,
				EBattleTriggerCleanupReason::Removal);
			check(bCleaned);
			Owner->Volatiles.RemoveAll(
				[&Fact](const FBattleConditionState& Condition)
				{
					return Condition.ConditionId == Fact.SourceDefinition.ConditionId;
				});
		}
		for (const FBattlerId BattlerId : SuppressedProtectAtStart)
		{
			FBattleBattlerState* Owner = State->FindMutableBattler(BattlerId);
			if (Owner == nullptr
				|| !HasVolatile(*Owner, FBattleVolatileRules::GetProtectId()))
			{
				continue;
			}
			const bool bCleaned = TryCleanupVolatileTriggers(
				*State,
				FBattleVolatileRules::GetProtectId(),
				BattlerId,
				EBattleTriggerCleanupReason::Removal);
			check(bCleaned);
			Owner->Volatiles.RemoveAll(
				[](const FBattleConditionState& Condition)
				{
					return Condition.ConditionId == FBattleVolatileRules::GetProtectId();
				});
		}
		DrainTriggerOutputs(*State);
		State->bEndTurnTriggerPassComplete = true;

		if (State->Phase == EBattlePhase::Terminal)
		{
			const bool bCleaned = TryCleanupBattleEndTriggers(*State);
			check(bCleaned);
		}
		else
		{
			State->Phase = EBattlePhase::Resolving;
			State->CurrentLockedActionIndex = State->LockedActions.Num();
			TArray<FBattleReplacementRequirement> Requirements;
			FBattleFaintOutcomeResolver::ResolveQueueBoundary(*State, Requirements);
			if (State->Phase == EBattlePhase::MandatoryReplacement)
			{
				State->PendingReplacements.Reset();
				for (const FBattleReplacementRequirement& Requirement : Requirements)
				{
					FBattlePendingReplacementState& Pending =
						State->PendingReplacements.AddDefaulted_GetRef();
					Pending.TrainerId = Requirement.Target.TrainerId;
					Pending.ActiveSlotId = Requirement.Target.ActiveSlotId;
				}
				TArray<FBattleDecisionRequest> Requests;
				const bool bRequestsBuilt = TryBuildReplacementCheckpointRequests(
					*State,
					AfterStateVersion,
					true,
					Requests);
				check(bRequestsBuilt && !Requests.IsEmpty());
				State->PendingDecisionRequests = MoveTemp(Requests);
				State->PendingDecision = State->PendingDecisionRequests[0];
			}
			for (const FBattleReplacementRequirement& Requirement : Requirements)
			{
				Events.Add(MakeTargetedActionlessEvent(
					*State,
					ResolutionId,
					EBattleEventType::ReplacementRequired,
					EBattleEventCause::Rule,
					EBattleActionKind::Residual,
					FallbackSource,
					Requirement.Target));
			}
		}
	}

	if (State->Phase == EBattlePhase::EndOfTurn)
	{
		const uint64 NextTurnValue = State->TurnId.GetValue() + 1;
		FTurnId NextTurnId;
		const bool bTurnCreated = NextTurnValue > State->TurnId.GetValue()
			&& FTurnId::TryCreate(NextTurnValue, NextTurnId);
		check(bTurnCreated);
		State->TurnId = NextTurnId;
		for (FBattleTrainerState& Trainer : State->Trainers)
		{
			Trainer.ActionAllowance.MaximumActions = 0;
			Trainer.ActionAllowance.RemainingActions = 0;
			Trainer.ActionAllowance.bBagActionAvailable = true;
		}
		for (const FBattleActivePositionState& Active : State->ActivePositions)
		{
			const FBattleBattlerState* Battler = State->FindBattler(Active.BattlerId);
			FBattleTrainerState* Trainer = State->FindMutableTrainer(Active.TrainerId);
			if (Active.bAvailable
				&& Battler != nullptr
				&& Trainer != nullptr
				&& IsLivingSelectableBattler(Battler))
			{
				++Trainer->ActionAllowance.MaximumActions;
				++Trainer->ActionAllowance.RemainingActions;
			}
		}

		State->LockedActions.Reset();
		State->bLockedOrderReversesSpeed = false;
		State->CurrentLockedActionIndex = 0;
		State->AcceptedSelections.Reset();
		State->DecisionOwnerSequence.Reset();
		State->CurrentDecisionOwnerIndex = INDEX_NONE;
		State->CurrentDecisionActorOffset = 0;
		State->PendingDecision.Reset();
		State->PendingDecisionRequests.Reset();
		State->PendingReplacements.Reset();
		TArray<FBattleDecisionOwnerState> Sequence = BuildDecisionOwnerSequence(*State);
		for (int32 OwnerIndex = Sequence.Num() - 1; OwnerIndex >= 0; --OwnerIndex)
		{
			FBattleDecisionOwnerState& Owner = Sequence[OwnerIndex];
			for (int32 ActorIndex = Owner.Actors.Num() - 1; ActorIndex >= 0; --ActorIndex)
			{
				const FBattleDecisionActorState Actor = Owner.Actors[ActorIndex];
				const FBattleBattlerState* Battler = State->FindBattler(Actor.BattlerId);
				FMoveId ChargedMoveId;
				if (Battler == nullptr
					|| !HasVolatile(*Battler, FBattleVolatileRules::GetChargingId())
					|| !TryGetVolatilePayloadMoveId(
						*State,
						Battler->BattlerId,
						FBattleVolatileRules::GetChargingId(),
						ChargedMoveId))
				{
					continue;
				}
				const FBattleMoveDefinition* ChargedMove = State->Catalog.FindMove(
					ChargedMoveId);
				FBattleDecision ForcedDecision;
				bool bDecisionCreated = false;
				if (ChargedMove != nullptr
					&& IsBattleEngineExplicitTargetClass(ChargedMove->TargetClass))
				{
					FActiveSlotId TargetSlotId;
					bDecisionCreated = TryGetChargingTargetSlot(
						*State,
						Battler->BattlerId,
						TargetSlotId)
						&& FBattleDecision::TryCreateFight(
							AfterStateVersion,
							Battler->TrainerId,
							Battler->BattlerId,
							ChargedMoveId,
							TargetSlotId,
							ForcedDecision);
				}
				else if (ChargedMove != nullptr)
				{
					bDecisionCreated = FBattleDecision::TryCreateAutomaticallyTargetedFight(
						AfterStateVersion,
						Battler->TrainerId,
						Battler->BattlerId,
						ChargedMoveId,
						ForcedDecision);
				}
				check(bDecisionCreated);
				State->AcceptedSelections.Add(MoveTemp(ForcedDecision));
				Owner.Actors.RemoveAt(ActorIndex);
			}
			if (Owner.Actors.IsEmpty())
			{
				Sequence.RemoveAt(OwnerIndex);
			}
		}

		if (Sequence.IsEmpty())
		{
			TArray<FBattleLockedActionState> NewLockedActions;
			bool bReverseSpeed = false;
			const bool bLocked = TryBuildLockedActions(
				*State,
				State->AcceptedSelections,
				ResolutionId,
				NewLockedActions,
				bReverseSpeed);
			check(bLocked && !NewLockedActions.IsEmpty());
			State->LockedActions = MoveTemp(NewLockedActions);
			State->bLockedOrderReversesSpeed = bReverseSpeed;
			State->NextActionId += static_cast<uint64>(State->LockedActions.Num());
			State->Phase = EBattlePhase::Locked;
			for (const FBattleLockedActionState& Action : State->LockedActions)
			{
				Events.Add(MakeActionOrderLockedEvent(*State, ResolutionId, Action));
			}
		}
		else
		{
			TArray<FBattleDecisionRequest> Requests;
			FBattleRejection BuildRejection;
			const bool bRequestsBuilt = TryBuildPendingRequests(
				*State,
				Sequence,
				0,
				0,
				AfterStateVersion,
				TConstArrayView<FBattleDecision>(),
				Requests,
				BuildRejection);
			check(bRequestsBuilt && !Requests.IsEmpty());
			State->DecisionOwnerSequence = MoveTemp(Sequence);
			State->CurrentDecisionOwnerIndex = 0;
			State->CurrentDecisionActorOffset = 0;
			State->PendingDecisionRequests = MoveTemp(Requests);
			State->PendingDecision = State->PendingDecisionRequests[0];
			State->Phase = EBattlePhase::Selecting;
		}
		State->bEndTurnTriggerPassComplete = false;
	}

	State->StateVersion = AfterStateVersion;
	if (Events.IsEmpty())
	{
		Events.Add(MakeEvent(
			*State,
			ResolutionId,
			FActionId(),
			EBattleEventType::ActionCompleted,
			EBattleEventCause::Rule,
			EBattleActionKind::Residual,
			EBattleOutcomeCause::None,
			FallbackSource));
	}
	EBattleStateValidationError StateError = EBattleStateValidationError::None;
	const bool bStateValid = State->ValidateInvariants(StateError);
	check(bStateValid);

	FBattleResolutionSpec ResolutionSpec;
	ResolutionSpec.ResolutionId = ResolutionId;
	ResolutionSpec.BeforeStateVersion = BeforeStateVersion;
	ResolutionSpec.AfterStateVersion = State->StateVersion;
	ResolutionSpec.bAccepted = true;
	ResolutionSpec.Events = MoveTemp(Events);
	FBattleResolution Resolution;
	const bool bResolutionCreated = FBattleResolution::TryCreate(ResolutionSpec, Resolution);
	check(bResolutionCreated);
	State->AppendResolution(Resolution);
	return Resolution;
}

FBattleResolution FBattleEngine::SubmitDecisionBatch(const FBattleDecisionBatch& Batch)
{
	check(State.IsValid());
	if (Batch.IsValid())
	{
		for (const FBattleDecision& Decision : Batch.GetDecisions())
		{
			State->SubmittedDecisions.Add(Decision);
		}
	}

	const FResolutionId ResolutionId = TakeResolutionId(*State);
	const FBattleDecisionRequest* FirstRequest = State->PendingDecisionRequests.IsEmpty()
		? nullptr
		: &State->PendingDecisionRequests[0];
	const FBattleDecision* FirstDecision = Batch.IsValid() && !Batch.GetDecisions().IsEmpty()
		? &Batch.GetDecisions()[0]
		: nullptr;
	const FBattleEventSource FallbackSource = SourceFromRequest(*State, FirstRequest, FirstDecision);
	if (State->Phase == EBattlePhase::MandatoryReplacement)
	{
		return ResolveReplacementDecisionBatch(
			*State,
			Batch,
			ResolutionId,
			FallbackSource);
	}

	FBattleRejection Rejection;
	if (State->Phase == EBattlePhase::Terminal)
	{
		Rejection.Reason = EBattleRejectionReason::TerminalState;
	}
	else if (!Batch.IsValid())
	{
		Rejection.Reason = EBattleRejectionReason::InvalidDecisionBatch;
	}
	else if (State->Phase != EBattlePhase::Selecting || State->PendingDecisionRequests.IsEmpty())
	{
		Rejection.Reason = EBattleRejectionReason::DecisionSequenceNotStarted;
	}
	else if (Batch.GetStateVersion() != State->StateVersion)
	{
		Rejection.Reason = EBattleRejectionReason::StaleStateVersion;
	}
	else if (Batch.GetRequestKind() != EBattleDecisionRequestKind::Action)
	{
		Rejection.Reason = EBattleRejectionReason::WrongRequestKind;
	}
	else if (Batch.GetDecisionOwnerTrainerId()
		!= State->PendingDecisionRequests[0].GetDecisionOwnerTrainerId())
	{
		Rejection.Reason = EBattleRejectionReason::WrongDecisionOwner;
		Rejection.TrainerId = Batch.GetDecisionOwnerTrainerId();
	}
	else if (Batch.GetDecisions().IsEmpty()
		|| Batch.GetDecisions().Num() > State->PendingDecisionRequests.Num())
	{
		Rejection.Reason = EBattleRejectionReason::WrongDecisionCount;
	}
	else
	{
		for (int32 DecisionIndex = 0; DecisionIndex < Batch.GetDecisions().Num(); ++DecisionIndex)
		{
			const FBattleDecision& Decision = Batch.GetDecisions()[DecisionIndex];
			const FBattleDecisionRequest& Request = State->PendingDecisionRequests[DecisionIndex];
			if (Decision.GetActingBattlerId() != Request.GetActingBattlerId())
			{
				Rejection.Reason = EBattleRejectionReason::WrongDecisionOrder;
				Rejection.BattlerId = Decision.GetActingBattlerId();
				break;
			}
			if (!Request.Allows(Decision, Rejection))
			{
				break;
			}
		}
		if (!Rejection.IsRejected())
		{
			ValidateCombinedSelections(*State, Batch.GetDecisions(), Rejection);
		}
	}

	if (Rejection.IsRejected())
	{
		return MakeRejectedResolution(
			*State,
			ResolutionId,
			Rejection,
			EBattleEventType::DecisionRejected,
			EBattleEventCause::Decision,
			FirstDecision != nullptr ? FirstDecision->GetActionKind() : EBattleActionKind::Fight,
			FallbackSource);
	}

	const uint64 BeforeStateVersion = State->StateVersion;
	const uint64 AfterStateVersion = BeforeStateVersion + 1;
	if (AfterStateVersion == 0)
	{
		Rejection.Reason = EBattleRejectionReason::InvalidDecision;
		return MakeRejectedResolution(
			*State,
			ResolutionId,
			Rejection,
			EBattleEventType::DecisionRejected,
			EBattleEventCause::Decision,
			FirstDecision->GetActionKind(),
			FallbackSource);
	}

	int32 NextOwnerIndex = State->CurrentDecisionOwnerIndex;
	int32 NextActorOffset = State->CurrentDecisionActorOffset + Batch.GetDecisions().Num();
	if (!State->DecisionOwnerSequence.IsValidIndex(NextOwnerIndex))
	{
		Rejection.Reason = EBattleRejectionReason::InvalidDecision;
		return MakeRejectedResolution(
			*State,
			ResolutionId,
			Rejection,
			EBattleEventType::DecisionRejected,
			EBattleEventCause::Decision,
			FirstDecision->GetActionKind(),
			FallbackSource);
	}
	if (NextActorOffset >= State->DecisionOwnerSequence[NextOwnerIndex].Actors.Num())
	{
		++NextOwnerIndex;
		NextActorOffset = 0;
	}

	TArray<FBattleDecisionRequest> NextRequests;
	if (State->DecisionOwnerSequence.IsValidIndex(NextOwnerIndex)
		&& !TryBuildPendingRequests(
			*State,
			State->DecisionOwnerSequence,
			NextOwnerIndex,
			NextActorOffset,
			AfterStateVersion,
			Batch.GetDecisions(),
			NextRequests,
			Rejection))
	{
		if (!Rejection.IsRejected())
		{
			Rejection.Reason = EBattleRejectionReason::InvalidDecision;
		}
		return MakeRejectedResolution(
			*State,
			ResolutionId,
			Rejection,
			EBattleEventType::DecisionRejected,
			EBattleEventCause::Decision,
			FirstDecision->GetActionKind(),
			FallbackSource);
	}

	const bool bLocksQueue = NextRequests.IsEmpty();
	TArray<FBattleLockedActionState> NewLockedActions;
	bool bReverseSpeed = false;
	if (bLocksQueue)
	{
		TArray<FBattleDecision> AllSelections = State->AcceptedSelections;
		for (const FBattleDecision& Decision : Batch.GetDecisions())
		{
			AllSelections.Add(Decision);
		}
		if (!TryBuildLockedActions(
				*State,
				AllSelections,
				ResolutionId,
				NewLockedActions,
				bReverseSpeed))
		{
			Rejection.Reason = EBattleRejectionReason::InvalidDecision;
			return MakeRejectedResolution(
				*State,
				ResolutionId,
				Rejection,
				EBattleEventType::DecisionRejected,
				EBattleEventCause::Decision,
				FirstDecision->GetActionKind(),
				FallbackSource);
		}
	}

	TArray<FBattleEvent> Events;
	for (int32 DecisionIndex = 0; DecisionIndex < Batch.GetDecisions().Num(); ++DecisionIndex)
	{
		const FBattleDecision& Decision = Batch.GetDecisions()[DecisionIndex];
		const FBattleDecisionRequest& Request = State->PendingDecisionRequests[DecisionIndex];
		State->AcceptedSelections.Add(Decision);
		Events.Add(MakeEvent(
			*State,
			ResolutionId,
			FActionId(),
			EBattleEventType::DecisionAccepted,
			EBattleEventCause::Decision,
			Decision.GetActionKind(),
			EBattleOutcomeCause::None,
			SourceFromRequest(*State, &Request, &Decision)));
	}

	State->StateVersion = AfterStateVersion;
	State->CurrentDecisionOwnerIndex = NextOwnerIndex;
	State->CurrentDecisionActorOffset = NextActorOffset;
	State->PendingDecisionRequests = MoveTemp(NextRequests);
	if (State->PendingDecisionRequests.IsEmpty())
	{
		State->PendingDecision.Reset();
		State->LockedActions = MoveTemp(NewLockedActions);
		State->bLockedOrderReversesSpeed = bReverseSpeed;
		State->CurrentLockedActionIndex = 0;
		State->NextActionId += static_cast<uint64>(State->LockedActions.Num());
		State->Phase = EBattlePhase::Locked;
		for (const FBattleLockedActionState& Action : State->LockedActions)
		{
			Events.Add(MakeActionOrderLockedEvent(*State, ResolutionId, Action));
		}
	}
	else
	{
		State->PendingDecision = State->PendingDecisionRequests[0];
	}

	FBattleResolutionSpec ResolutionSpec;
	ResolutionSpec.ResolutionId = ResolutionId;
	ResolutionSpec.BeforeStateVersion = BeforeStateVersion;
	ResolutionSpec.AfterStateVersion = State->StateVersion;
	ResolutionSpec.bAccepted = true;
	ResolutionSpec.Events = MoveTemp(Events);
	FBattleResolution Resolution;
	const bool bResolutionCreated = FBattleResolution::TryCreate(ResolutionSpec, Resolution);
	check(bResolutionCreated);
	State->AppendResolution(Resolution);
	return Resolution;
}

FBattleResolution FBattleEngine::SubmitDecision(const FBattleDecision& Decision)
{
	check(State.IsValid());
	if ((State->Phase == EBattlePhase::Selecting
			|| (State->Phase == EBattlePhase::MandatoryReplacement
				&& Decision.GetRequestKind()
					== EBattleDecisionRequestKind::MandatoryReplacement))
		&& !State->PendingDecisionRequests.IsEmpty())
	{
		FBattleDecisionBatchSpec BatchSpec;
		BatchSpec.StateVersion = Decision.GetStateVersion();
		BatchSpec.RequestKind = Decision.GetRequestKind();
		BatchSpec.DecisionOwnerTrainerId = Decision.GetDecisionOwnerTrainerId();
		BatchSpec.Decisions.Add(Decision);
		FBattleDecisionBatch Batch;
		FBattleRejection BatchRejection;
		if (FBattleDecisionBatch::TryCreate(BatchSpec, Batch, BatchRejection))
		{
			return SubmitDecisionBatch(Batch);
		}
	}
	if (Decision.IsValid())
	{
		State->SubmittedDecisions.Add(Decision);
	}

	const FResolutionId ResolutionId = TakeResolutionId(*State);
	const FBattleDecisionRequest* Request = State->PendingDecision.IsSet()
		? &State->PendingDecision.GetValue()
		: nullptr;
	const FBattleEventSource Source = SourceFromRequest(*State, Request, &Decision);
	const EBattleActionKind ActionKind = Decision.IsValid()
		? Decision.GetActionKind()
		: EBattleActionKind::Fight;

	FBattleRejection Rejection;
	if (State->Phase == EBattlePhase::Terminal)
	{
		Rejection.Reason = EBattleRejectionReason::TerminalState;
		return MakeRejectedResolution(
			*State,
			ResolutionId,
			Rejection,
			EBattleEventType::DecisionRejected,
			EBattleEventCause::Decision,
			ActionKind,
			Source);
	}
	if (!Decision.IsValid())
	{
		Rejection.Reason = EBattleRejectionReason::InvalidDecision;
		return MakeRejectedResolution(
			*State,
			ResolutionId,
			Rejection,
			EBattleEventType::DecisionRejected,
			EBattleEventCause::Decision,
			ActionKind,
			Source);
	}
	if (Request == nullptr)
	{
		Rejection.Reason = EBattleRejectionReason::NoPendingDecision;
		return MakeRejectedResolution(
			*State,
			ResolutionId,
			Rejection,
			EBattleEventType::DecisionRejected,
			EBattleEventCause::Decision,
			ActionKind,
			Source);
	}
	if (!Request->Allows(Decision, Rejection))
	{
		return MakeRejectedResolution(
			*State,
			ResolutionId,
			Rejection,
			EBattleEventType::DecisionRejected,
			EBattleEventCause::Decision,
			ActionKind,
			Source);
	}
	if (Request->GetRequestKind() == EBattleDecisionRequestKind::ShiftResponse)
	{
		if (State->Phase != EBattlePhase::MandatoryReplacement
			|| State->PendingDecisionRequests.Num() != 1
			|| State->PendingReplacements.IsEmpty()
			|| State->PendingDecisionRequests[0].GetRequestKind()
				!= EBattleDecisionRequestKind::ShiftResponse)
		{
			Rejection.Reason = EBattleRejectionReason::IllegalAction;
			return MakeRejectedResolution(
				*State,
				ResolutionId,
				Rejection,
				EBattleEventType::DecisionRejected,
				EBattleEventCause::Decision,
				ActionKind,
				Source);
		}
		return ResolveShiftResponseDecision(
			*State,
			Decision,
			*Request,
			ResolutionId,
			Source);
	}
	if (Request->GetRequestKind() == EBattleDecisionRequestKind::PivotSwitch)
	{
		FBattleLockedActionState* Action = State->LockedActions.IsValidIndex(
			State->CurrentLockedActionIndex)
			? &State->LockedActions[State->CurrentLockedActionIndex]
			: nullptr;
		if (State->Phase != EBattlePhase::Resolving
			|| State->PendingDecisionRequests.Num() != 1
			|| Action == nullptr
			|| Action->EffectExecutionState
				!= EBattleLockedEffectExecutionState::AwaitingPivot
			|| Action->bFinished
			|| Action->Decision.GetActingBattlerId() != Decision.GetActingBattlerId()
			|| Decision.GetActionKind() != EBattleActionKind::Switch)
		{
			Rejection.Reason = EBattleRejectionReason::IllegalAction;
			return MakeRejectedResolution(
				*State,
				ResolutionId,
				Rejection,
				EBattleEventType::DecisionRejected,
				EBattleEventCause::Decision,
				ActionKind,
				Source);
		}

		FBattleSwitchLegalityResult Legality;
		FBattleSwitchSelectionSpec SelectionSpec;
		SelectionSpec.RequestedPartySlotId = Decision.GetSwitchPartySlotId();
		FBattleSwitchResolution SwitchResolution;
		const bool bResolved = TryBuildSwitchLegality(
			*State,
			EBattleSwitchKind::Pivot,
			Decision.GetDecisionOwnerTrainerId(),
			Decision.GetActingBattlerId(),
			Decision.GetActiveTargetId(),
			TConstArrayView<FPartySlotId>(),
			Legality)
			&& FBattleSwitchResolver::TryResolve(
				Legality,
				SelectionSpec,
				*State->Random,
				SwitchResolution);
		FBattleEventTarget OutgoingTarget;
		FBattleEventTarget IncomingTarget;
		if (!bResolved
			|| !SwitchResolution.HasSelection()
			|| !TryApplySwitchSelection(
				*State,
				Decision.GetDecisionOwnerTrainerId(),
				Decision.GetActingBattlerId(),
				Decision.GetActiveTargetId(),
				SwitchResolution,
				OutgoingTarget,
				IncomingTarget))
		{
			Rejection.Reason = EBattleRejectionReason::IllegalSwitch;
			Rejection.PartySlotId = Decision.GetSwitchPartySlotId();
			return MakeRejectedResolution(
				*State,
				ResolutionId,
				Rejection,
				EBattleEventType::DecisionRejected,
				EBattleEventCause::Decision,
				ActionKind,
				Source);
		}

		const uint64 BeforeStateVersion = State->StateVersion;
		TArray<FBattleEvent> Events;
		AppendSwitchTransitionEvents(
			*State,
			ResolutionId,
			*Action,
			OutgoingTarget,
			IncomingTarget,
			Events);
		const bool bHazardsResolved = TryResolveEntryHazards(
			*State,
			IncomingTarget.BattlerId,
			IncomingTarget.ActiveSlotId,
			ResolutionId,
			Events);
		if (!bHazardsResolved)
		{
			UE_LOG(
				LogTemp,
				Fatal,
				TEXT("Validated C07D pivot entry hazards could not be resolved."));
		}
		Action->EffectExecutionState = EBattleLockedEffectExecutionState::Completed;
		Action->bFinished = true;
		Events.Add(MakeActionDetailEvent(
			*State,
			ResolutionId,
			*Action,
			EBattleEventType::ActionCompleted,
			EBattleEventCause::Action));
		++State->CurrentLockedActionIndex;
		State->PendingDecision.Reset();
		State->PendingDecisionRequests.Reset();
		AppendPostActionBoundaryEvents(*State, ResolutionId, *Action, Events);
		++State->StateVersion;

		EBattleStateValidationError StateError = EBattleStateValidationError::None;
		const bool bStateValid = State->ValidateInvariants(StateError);
		check(bStateValid);

		FBattleResolutionSpec ResolutionSpec;
		ResolutionSpec.ResolutionId = ResolutionId;
		ResolutionSpec.BeforeStateVersion = BeforeStateVersion;
		ResolutionSpec.AfterStateVersion = State->StateVersion;
		ResolutionSpec.bAccepted = true;
		ResolutionSpec.Events = MoveTemp(Events);
		FBattleResolution Resolution;
		const bool bResolutionCreated = FBattleResolution::TryCreate(
			ResolutionSpec,
			Resolution);
		check(bResolutionCreated);
		State->AppendResolution(Resolution);
		return Resolution;
	}
	if (ActionKind != EBattleActionKind::ScriptedEnd && ActionKind != EBattleActionKind::Abandon)
	{
		Rejection.Reason = EBattleRejectionReason::IllegalAction;
		return MakeRejectedResolution(
			*State,
			ResolutionId,
			Rejection,
			EBattleEventType::DecisionRejected,
			EBattleEventCause::Decision,
			ActionKind,
			Source);
	}

	const uint64 BeforeStateVersion = State->StateVersion;
	const FActionId ActionId = TakeActionId(*State);
	TArray<FBattleEvent> Events;
	Events.Add(MakeEvent(*State, ResolutionId, ActionId, EBattleEventType::DecisionAccepted, EBattleEventCause::Decision, ActionKind, EBattleOutcomeCause::None, Source));
	Events.Add(MakeEvent(*State, ResolutionId, ActionId, EBattleEventType::ActionLocked, EBattleEventCause::Action, ActionKind, EBattleOutcomeCause::None, Source));
	Events.Add(MakeEvent(*State, ResolutionId, ActionId, EBattleEventType::ActionStarted, EBattleEventCause::Action, ActionKind, EBattleOutcomeCause::None, Source));
	Events.Add(MakeEvent(*State, ResolutionId, ActionId, EBattleEventType::ScriptedAction, EBattleEventCause::Scripted, ActionKind, EBattleOutcomeCause::None, Source));
	Events.Add(MakeEvent(*State, ResolutionId, ActionId, EBattleEventType::ActionCompleted, EBattleEventCause::Action, ActionKind, EBattleOutcomeCause::None, Source));

	State->Phase = EBattlePhase::Terminal;
	State->Outcome = ActionKind == EBattleActionKind::Abandon
		? EBattleOutcome::Abandoned
		: EBattleOutcome::ScriptedEnd;
	State->OutcomeCause = EBattleOutcomeCause::Ordinary;
	State->PendingDecision.Reset();
	const bool bBattleEndCleaned = TryCleanupBattleEndTriggers(*State);
	check(bBattleEndCleaned);
	++State->StateVersion;
	Events.Add(MakeEvent(*State, ResolutionId, ActionId, EBattleEventType::BattleEnded, EBattleEventCause::Outcome, ActionKind, State->OutcomeCause, Source));

	FBattleResolutionSpec ResolutionSpec;
	ResolutionSpec.ResolutionId = ResolutionId;
	ResolutionSpec.BeforeStateVersion = BeforeStateVersion;
	ResolutionSpec.AfterStateVersion = State->StateVersion;
	ResolutionSpec.bAccepted = true;
	ResolutionSpec.Events = MoveTemp(Events);
	FBattleResolution Resolution;
	const bool bResolutionCreated = FBattleResolution::TryCreate(ResolutionSpec, Resolution);
	check(bResolutionCreated);
	State->AppendResolution(Resolution);
	return Resolution;
}

FBattleResolution FBattleEngine::ApplyBetweenActionsStatRefresh(
	const FBattleBetweenActionsStatRefresh& Refresh)
{
	check(State.IsValid());
	if (Refresh.IsValid())
	{
		State->SubmittedStatRefreshes.Add(Refresh);
	}

	const FResolutionId ResolutionId = TakeResolutionId(*State);
	FBattleEventSource Source = FindFallbackSource(*State);
	Source.BattlerId = Refresh.BattlerId;
	const FBattleBattlerState* Existing = State->FindBattler(Refresh.BattlerId);
	if (Existing != nullptr)
	{
		Source.TrainerId = Existing->TrainerId;
	}

	FBattleRejection Rejection;
	if (State->Phase == EBattlePhase::Terminal)
	{
		Rejection.Reason = EBattleRejectionReason::TerminalState;
	}
	else if (!Refresh.IsValid())
	{
		Rejection.Reason = EBattleRejectionReason::InvalidDecision;
	}
	else if (Refresh.StateVersion != State->StateVersion)
	{
		Rejection.Reason = EBattleRejectionReason::StaleStateVersion;
	}
	else if (State->Phase != EBattlePhase::Resolving)
	{
		Rejection.Reason = EBattleRejectionReason::RefreshNotAllowed;
	}
	else if (State->PendingDecision.IsSet()
		&& State->PendingDecision.GetValue().GetRequestKind()
			== EBattleDecisionRequestKind::PivotSwitch)
	{
		Rejection.Reason = EBattleRejectionReason::RefreshNotAllowed;
	}
	else if (!State->AvailableOpponentRemovalCheckpoints.Contains(Refresh.OpponentRemovalCheckpointEventOrdinal))
	{
		Rejection.Reason = EBattleRejectionReason::InvalidCheckpoint;
	}
	else if (Existing == nullptr)
	{
		Rejection.Reason = EBattleRejectionReason::WrongActingBattler;
		Rejection.BattlerId = Refresh.BattlerId;
	}

	if (Rejection.IsRejected())
	{
		return MakeRejectedResolution(
			*State,
			ResolutionId,
			Rejection,
			EBattleEventType::StatRefreshRejected,
			EBattleEventCause::StatRefresh,
			EBattleActionKind::Fight,
			Source);
	}

	const uint64 BeforeStateVersion = State->StateVersion;
	FBattleBattlerState* MutableEntry = State->FindMutableBattler(Refresh.BattlerId);
	check(MutableEntry != nullptr);
	const int32 PreviousLevel = MutableEntry->Level;
	MutableEntry->Level = Refresh.NewLevel;
	MutableEntry->PermanentStats = Refresh.NewStats;
	MutableEntry->CurrentHP = Refresh.NewCurrentHP;
	MutableEntry->bFainted = Refresh.NewCurrentHP == 0;
	State->AvailableOpponentRemovalCheckpoints.RemoveSingle(Refresh.OpponentRemovalCheckpointEventOrdinal);
	++State->StateVersion;

	FBattleEvent Event = MakeEvent(
		*State,
		ResolutionId,
		FActionId(),
		EBattleEventType::StatRefreshApplied,
		EBattleEventCause::StatRefresh,
		EBattleActionKind::Fight,
		EBattleOutcomeCause::None,
		Source);
	FBattleEventSpec EventSpec;
	EventSpec.EventOrdinal = Event.GetEventOrdinal();
	EventSpec.BattleId = Event.GetBattleId();
	EventSpec.TurnId = Event.GetTurnId();
	EventSpec.ResolutionId = Event.GetResolutionId();
	EventSpec.Type = Event.GetType();
	EventSpec.Cause = Event.GetCause();
	EventSpec.CauseActionKind = Event.GetCauseActionKind();
	EventSpec.Source = Event.GetSource();
	EventSpec.NumericBefore = PreviousLevel;
	EventSpec.NumericAfter = Refresh.NewLevel;
	EventSpec.NumericDelta = Refresh.NewLevel - PreviousLevel;
	EventSpec.Visibility = Event.GetVisibility();
	const bool bEventCreated = FBattleEvent::TryCreate(EventSpec, Event);
	check(bEventCreated);

	FBattleResolutionSpec ResolutionSpec;
	ResolutionSpec.ResolutionId = ResolutionId;
	ResolutionSpec.BeforeStateVersion = BeforeStateVersion;
	ResolutionSpec.AfterStateVersion = State->StateVersion;
	ResolutionSpec.bAccepted = true;
	ResolutionSpec.Events.Add(Event);
	FBattleResolution Resolution;
	const bool bResolutionCreated = FBattleResolution::TryCreate(ResolutionSpec, Resolution);
	check(bResolutionCreated);
	State->AppendResolution(Resolution);
	return Resolution;
}

FBattleReplayInputs FBattleEngine::ExportReplayInputs() const
{
	FBattleReplayInputs Inputs;
	if (State.IsValid())
	{
		Inputs.Setup = State->Setup;
		Inputs.Decisions = State->SubmittedDecisions;
		Inputs.StatRefreshes = State->SubmittedStatRefreshes;
	}
	return Inputs;
}

TArray<FBattleRandomDraw> FBattleEngine::ExportRandomTrace() const
{
	TArray<FBattleRandomDraw> Trace;
	if (State.IsValid() && State->Random.IsValid())
	{
		for (const FBattleRandomDraw& Draw : State->Random->GetTrace())
		{
			Trace.Add(Draw);
		}
	}
	return Trace;
}

FBattleReplayRecord FBattleEngine::ExportReplayRecord() const
{
	FBattleReplayRecord Record;
	if (!State.IsValid())
	{
		return Record;
	}
	const TArray<FBattleRandomDraw> Trace = ExportRandomTrace();
	const bool bCreated = FBattleReplayRecord::TryCreate(
		FBattleReplayRecord::CurrentSchemaVersion,
		ExportReplayInputs(),
		State->Resolutions,
		Trace,
		GetSnapshot(),
		Record);
	ensure(bCreated);
	return Record;
}

const FBattlePartyEntrySetup* FBattleSnapshot::FindBattler(const FBattlerId BattlerId) const
{
	return PartyEntries.FindByPredicate(
		[BattlerId](const FBattlePartyEntrySetup& Entry)
		{
			return Entry.BattlerId == BattlerId;
		});
}

const FBattleObservedBattler* FBattleSnapshot::FindObservedBattler(const FBattlerId BattlerId) const
{
	return ObservedBattlers.FindByPredicate(
		[BattlerId](const FBattleObservedBattler& Entry)
		{
			return Entry.BattlerId == BattlerId;
		});
}
