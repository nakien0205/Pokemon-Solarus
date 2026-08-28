#include "BattleEffectExecutorContext.h"

#include "Battle/BattleAbility.h"
#include "BattleEntryHazardPrevention.h"
#include "Battle/BattleFieldSideConditions.h"
#include "Battle/BattleItem.h"
#include "Battle/BattleMajorStatus.h"
#include "Battle/BattleState.h"
#include "Battle/BattleVolatile.h"
#include "Math/NumericLimits.h"

namespace BattleEffectExecutorPrivate
{
	bool FStateExecutionContext::TryCleanupItemHooks(
		const FBattleBattlerState& Battler,
		const FItemId& ItemId,
		const EBattleTriggerCleanupReason Reason)
	{
		if (!FBattleItemRules::IsCanonical(ItemId))
		{
			return true;
		}
		FBattleTriggerSubject Owner;
		FBattleTriggerSourceDefinition SourceDefinition;
		FBattleTriggerOperationContext Operation;
		if (!FBattleTriggerSubject::TryCreateBattler(Battler.BattlerId, Owner)
			|| !FBattleTriggerSourceDefinition::TryCreateItem(ItemId, SourceDefinition)
			|| !TryTakeTriggerContext(Operation))
		{
			return false;
		}
		FBattleTriggerCleanupRequest Cleanup;
		Cleanup.Reason = Reason;
		Cleanup.AffectedOwners.Add(Owner);
		Cleanup.SourceDefinitionFilter = SourceDefinition;
		Cleanup.Context = Operation;
		EBattleTriggerError Error = EBattleTriggerError::None;
		if (!TriggerFramework.TryApplyCleanup(Cleanup, Error))
		{
			return false;
		}
		DrainTriggerOutputs();
		return true;
	}

	bool FStateExecutionContext::TryRegisterItemHooks(
		const FBattleBattlerState& Battler,
		const FBattleActivePositionState& Active)
	{
		const FItemId ItemId = Battler.HeldItem.CurrentItemId;
		if (!FBattleItemRules::IsCanonical(ItemId))
		{
			return true;
		}
		if (Battler.HeldItem.bConsumed || Battler.HeldItem.bTemporarilyRemoved)
		{
			return true;
		}
		if (!Active.bAvailable
			|| Active.BattlerId != Battler.BattlerId
			|| Active.TrainerId != Battler.TrainerId
			|| !TryCleanupItemHooks(
				Battler,
				ItemId,
				EBattleTriggerCleanupReason::Removal))
		{
			return false;
		}
		FBattleTriggerSubject Owner;
		FBattleTriggerSubject Source;
		if (!FBattleTriggerSubject::TryCreateBattler(Battler.BattlerId, Owner)
			|| !FBattleTriggerSubject::TryCreateBattler(Battler.BattlerId, Source))
		{
			return false;
		}
		FBattleItemRegistrationFacts Facts;
		Facts.ItemId = ItemId;
		Facts.Owner = Owner;
		Facts.Source = Source;
		Facts.Targets.Add(Owner);
		Facts.bSuppressed = Battler.HeldItem.bSuppressed;
		EBattleAbilityItemHookError Error = EBattleAbilityItemHookError::None;
		if (!FBattleItemRules::TryRegisterHooks(TriggerFramework, Facts, Error))
		{
			return false;
		}
		DrainTriggerOutputs();
		return true;
	}

	bool FStateExecutionContext::TryDispatchItemPhase(
		const FBattleBattlerState& Battler,
		const EBattleTriggerPhase Phase,
		TArray<FBattleTriggerEffectRequest>& OutRequests)
	{
		OutRequests.Reset();
		const FItemId ItemId = Battler.HeldItem.CurrentItemId;
		if (!FBattleItemRules::IsCanonical(ItemId)
			|| Battler.HeldItem.bConsumed
			|| Battler.HeldItem.bTemporarilyRemoved)
		{
			return false;
		}
		FBattleTriggerSubject Owner;
		FBattleTriggerSourceDefinition SourceDefinition;
		if (!FBattleTriggerSubject::TryCreateBattler(Battler.BattlerId, Owner)
			|| !FBattleTriggerSourceDefinition::TryCreateItem(ItemId, SourceDefinition))
		{
			return false;
		}
		FBattleTriggerDispatchSpec Dispatch;
		Dispatch.Phase = Phase;
		const FBattleActivePositionState* Active = FindActiveForBattler(
			Battler.BattlerId);
		for (const FBattleTriggerRegistrationState& Registration :
			TriggerFramework.GetActiveRegistrations())
		{
			if (Registration.Spec.Owner == Owner
				&& Registration.Spec.SourceDefinition == SourceDefinition
				&& Registration.Spec.Rule.Phase == Phase)
			{
				FBattleTriggerDispatchParticipant& Participant =
					Dispatch.Participants.AddDefaulted_GetRef();
				Participant.RegistrationId = Registration.RegistrationId;
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
		FBattleTriggerDispatchResult Result;
		EBattleTriggerError Error = EBattleTriggerError::None;
		if (!TryTakeTriggerContext(Operation))
		{
			return false;
		}
		Dispatch.ReentrancyToken = Operation.ReentrancyToken;
		if (!TriggerFramework.TryEnqueueDispatch(Dispatch, Error)
			|| !TriggerFramework.TryResolveNextDispatch(Result, Error))
		{
			return false;
		}
		TArray<FBattleTriggerLifecycleFact> Facts;
		TriggerFramework.DrainEffectRequests(OutRequests);
		TriggerFramework.DrainLifecycleFacts(Facts);
		if (Result.bQueuedExpiryDispatch)
		{
			FBattleTriggerDispatchResult ExpiryResult;
			if (!TriggerFramework.TryResolveNextDispatch(ExpiryResult, Error))
			{
				return false;
			}
			TArray<FBattleTriggerEffectRequest> ExpiryRequests;
			TArray<FBattleTriggerLifecycleFact> ExpiryFacts;
			TriggerFramework.DrainEffectRequests(ExpiryRequests);
			TriggerFramework.DrainLifecycleFacts(ExpiryFacts);
			OutRequests.Append(MoveTemp(ExpiryRequests));
		}
		return true;
	}

	bool FStateExecutionContext::TryCleanupAbilityHooks(
		const FBattleBattlerState& Battler,
		const EBattleTriggerCleanupReason Reason)
	{
		if (!FBattleAbilityRules::IsCanonical(Battler.AbilityId))
		{
			return true;
		}

		FBattleTriggerSubject Owner;
		FBattleTriggerSourceDefinition SourceDefinition;
		FBattleTriggerOperationContext Operation;
		if (!FBattleTriggerSubject::TryCreateBattler(Battler.BattlerId, Owner)
			|| !FBattleTriggerSourceDefinition::TryCreateAbility(
				Battler.AbilityId,
				SourceDefinition)
			|| !TryTakeTriggerContext(Operation))
		{
			return false;
		}

		FBattleTriggerCleanupRequest Cleanup;
		Cleanup.Reason = Reason;
		Cleanup.AffectedOwners.Add(Owner);
		Cleanup.SourceDefinitionFilter = SourceDefinition;
		Cleanup.Context = Operation;
		EBattleTriggerError Error = EBattleTriggerError::None;
		if (!TriggerFramework.TryApplyCleanup(Cleanup, Error))
		{
			return false;
		}
		DrainTriggerOutputs();
		return true;
	}

	bool FStateExecutionContext::TryRegisterAbilityHooks(
		const FBattleBattlerState& Battler,
		const FBattleActivePositionState& Active)
	{
		if (!FBattleAbilityRules::IsCanonical(Battler.AbilityId))
		{
			return true;
		}
		if (!Active.bAvailable
			|| Active.BattlerId != Battler.BattlerId
			|| Active.TrainerId != Battler.TrainerId
			|| !TryCleanupAbilityHooks(
				Battler,
				EBattleTriggerCleanupReason::Removal))
		{
			return false;
		}

		FBattleTriggerSubject Owner;
		FBattleTriggerSubject Source;
		if (!FBattleTriggerSubject::TryCreateBattler(Battler.BattlerId, Owner)
			|| !FBattleTriggerSubject::TryCreateBattler(Battler.BattlerId, Source))
		{
			return false;
		}

		FBattleAbilityRegistrationFacts Facts;
		Facts.AbilityId = Battler.AbilityId;
		Facts.Owner = Owner;
		Facts.Source = Source;
		Facts.bSuppressed = Battler.bAbilitySuppressed;
		if (Battler.AbilityId == FBattleAbilityRules::GetIntimidateId())
		{
			TArray<const FBattleActivePositionState*> OpposingPositions;
			for (const FBattleActivePositionState& Candidate : ActivePositions)
			{
				if (Candidate.bAvailable
					&& Candidate.ActiveSlotId.GetSide()
						!= Active.ActiveSlotId.GetSide())
				{
					OpposingPositions.Add(&Candidate);
				}
			}
			OpposingPositions.Sort(
				[](const FBattleActivePositionState& Left,
					const FBattleActivePositionState& Right)
				{
					if (Left.ActiveSlotId.GetSide() != Right.ActiveSlotId.GetSide())
					{
						return static_cast<uint8>(Left.ActiveSlotId.GetSide())
							< static_cast<uint8>(Right.ActiveSlotId.GetSide());
					}
					return static_cast<uint8>(Left.ActiveSlotId.GetPosition())
						< static_cast<uint8>(Right.ActiveSlotId.GetPosition());
				});
			for (const FBattleActivePositionState* Position : OpposingPositions)
			{
				const FBattleBattlerState* TargetBattler = Position != nullptr
					? FindBattler(Position->BattlerId)
					: nullptr;
				FBattleTriggerSubject Target;
				if (TargetBattler == nullptr
					|| TargetBattler->CurrentHP <= 0
					|| TargetBattler->bFainted
					|| TargetBattler->bCaptured
					|| TargetBattler->bRemoved)
				{
					continue;
				}
				if (!FBattleTriggerSubject::TryCreateBattler(
						TargetBattler->BattlerId,
						Target))
				{
					return false;
				}
				Facts.Targets.Add(MoveTemp(Target));
			}
		}
		else
		{
			Facts.Targets.Add(Owner);
		}

		EBattleAbilityItemHookError Error = EBattleAbilityItemHookError::None;
		if (!FBattleAbilityRules::TryRegisterHooks(
				TriggerFramework,
				Facts,
				Error))
		{
			return false;
		}
		DrainTriggerOutputs();
		return true;
	}

	bool FStateExecutionContext::TryDispatchAbilityPhase(
		const FBattleBattlerState& Battler,
		const EBattleTriggerPhase Phase,
		TArray<FBattleTriggerEffectRequest>& OutRequests)
	{
		OutRequests.Reset();
		if (!FBattleAbilityRules::IsCanonical(Battler.AbilityId))
		{
			return false;
		}

		FBattleTriggerSubject Owner;
		FBattleTriggerSourceDefinition SourceDefinition;
		if (!FBattleTriggerSubject::TryCreateBattler(Battler.BattlerId, Owner)
			|| !FBattleTriggerSourceDefinition::TryCreateAbility(
				Battler.AbilityId,
				SourceDefinition))
		{
			return false;
		}

		FBattleTriggerDispatchSpec Dispatch;
		Dispatch.Phase = Phase;
		const FBattleActivePositionState* Active = FindActiveForBattler(
			Battler.BattlerId);
		for (const FBattleTriggerRegistrationState& Registration :
			TriggerFramework.GetActiveRegistrations())
		{
			if (Registration.Spec.Owner == Owner
				&& Registration.Spec.SourceDefinition == SourceDefinition
				&& Registration.Spec.Rule.Phase == Phase)
			{
				FBattleTriggerDispatchParticipant& Participant =
					Dispatch.Participants.AddDefaulted_GetRef();
				Participant.RegistrationId = Registration.RegistrationId;
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
		EBattleTriggerError Error = EBattleTriggerError::None;
		FBattleTriggerDispatchResult Result;
		if (!TryTakeTriggerContext(Operation))
		{
			return false;
		}
		Dispatch.ReentrancyToken = Operation.ReentrancyToken;
		if (!TriggerFramework.TryEnqueueDispatch(Dispatch, Error)
			|| !TriggerFramework.TryResolveNextDispatch(Result, Error))
		{
			return false;
		}
		TArray<FBattleTriggerLifecycleFact> Facts;
		TriggerFramework.DrainEffectRequests(OutRequests);
		TriggerFramework.DrainLifecycleFacts(Facts);
		if (Result.bQueuedExpiryDispatch)
		{
			FBattleTriggerDispatchResult ExpiryResult;
			if (!TriggerFramework.TryResolveNextDispatch(ExpiryResult, Error))
			{
				return false;
			}
			TArray<FBattleTriggerEffectRequest> ExpiryRequests;
			TArray<FBattleTriggerLifecycleFact> ExpiryFacts;
			TriggerFramework.DrainEffectRequests(ExpiryRequests);
			TriggerFramework.DrainLifecycleFacts(ExpiryFacts);
			OutRequests.Append(MoveTemp(ExpiryRequests));
		}
		return true;
	}

	bool FStateExecutionContext::TryBuildFieldSideOwner(
		const FConditionId& ConditionId,
		const TOptional<EBattleSide>& Side,
		FBattleTriggerSubject& OutOwner) const
	{
		if (FBattleFieldSideConditionRules::IsFieldOwned(ConditionId))
		{
			OutOwner = FBattleTriggerSubject::CreateField();
			return OutOwner.IsValid() && !Side.IsSet();
		}
		return FBattleFieldSideConditionRules::IsSideOwned(ConditionId)
			&& Side.IsSet()
			&& FBattleTriggerSubject::TryCreateSide(Side.GetValue(), OutOwner);
	}

	const FBattleConditionState* FStateExecutionContext::FindFieldSideConditionState(
		const FBattleTriggerSubject& Owner,
		const FConditionId& ConditionId) const
	{
		if (!FBattleFieldSideConditionRules::IsCanonical(ConditionId))
		{
			return nullptr;
		}
		if (Owner.Kind == EBattleTriggerSubjectKind::Field)
		{
			if (Field.Weather.IsSet()
				&& Field.Weather.GetValue().ConditionId == ConditionId)
			{
				return &Field.Weather.GetValue();
			}
			if (Field.Terrain.IsSet()
				&& Field.Terrain.GetValue().ConditionId == ConditionId)
			{
				return &Field.Terrain.GetValue();
			}
			return Field.Rooms.FindByPredicate(
				[&ConditionId](const FBattleConditionState& Condition)
				{
					return Condition.ConditionId == ConditionId;
				});
		}
		if (Owner.Kind != EBattleTriggerSubjectKind::Side || !Owner.bHasSide)
		{
			return nullptr;
		}
		const FBattleSideState* Side = FindSide(Owner.Side);
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

	bool FStateExecutionContext::TryDispatchFieldSidePhase(
		const FBattleTriggerSubject& Owner,
		const EBattleTriggerPhase Phase,
		const FConditionId& FilterConditionId,
		const TOptional<FActiveSlotId>& ActiveSlotId,
		TArray<FBattleTriggerEffectRequest>& OutRequests)
	{
		OutRequests.Reset();
		if (!Owner.IsValid()
			|| (Owner.Kind != EBattleTriggerSubjectKind::Field
				&& Owner.Kind != EBattleTriggerSubjectKind::Side))
		{
			return false;
		}
		FBattleTriggerDispatchSpec Dispatch;
		Dispatch.Phase = Phase;
		for (const FBattleTriggerRegistrationState& Registration :
			TriggerFramework.GetActiveRegistrations())
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
				|| FindFieldSideConditionState(Owner, ConditionId) == nullptr)
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
		if (!TryTakeTriggerContext(Operation))
		{
			return false;
		}
		Dispatch.ReentrancyToken = Operation.ReentrancyToken;
		if (!TriggerFramework.TryEnqueueDispatch(Dispatch, Error)
			|| !TriggerFramework.TryResolveNextDispatch(Result, Error))
		{
			return false;
		}
		TArray<FBattleTriggerLifecycleFact> Facts;
		TriggerFramework.DrainEffectRequests(OutRequests);
		TriggerFramework.DrainLifecycleFacts(Facts);
		if (Result.bQueuedExpiryDispatch)
		{
			FBattleTriggerDispatchResult ExpiryResult;
			if (!TriggerFramework.TryResolveNextDispatch(ExpiryResult, Error))
			{
				return false;
			}
			TArray<FBattleTriggerEffectRequest> ExpiryRequests;
			TArray<FBattleTriggerLifecycleFact> ExpiryFacts;
			TriggerFramework.DrainEffectRequests(ExpiryRequests);
			TriggerFramework.DrainLifecycleFacts(ExpiryFacts);
			OutRequests.Append(MoveTemp(ExpiryRequests));
		}
		return true;
	}

	bool FStateExecutionContext::TryIsFieldSideConditionActiveForPhase(
		const FConditionId& ConditionId,
		const TOptional<EBattleSide>& Side,
		const EBattleTriggerPhase Phase,
		const TOptional<FActiveSlotId>& ActiveSlotId,
		bool& bOutActive)
	{
		bOutActive = false;
		FBattleTriggerSubject Owner;
		if (!TryBuildFieldSideOwner(ConditionId, Side, Owner))
		{
			return false;
		}
		TArray<FBattleTriggerEffectRequest> Requests;
		if (!TryDispatchFieldSidePhase(
				Owner,
				Phase,
				ConditionId,
				ActiveSlotId,
				Requests))
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

	bool FStateExecutionContext::TryRegisterFieldSideCondition(
		const FConditionId& ConditionId,
		const TOptional<EBattleSide>& Side,
		const TOptional<int32>& RemainingTurns,
		const int32 Layers)
	{
		FBattleTriggerSubject Owner;
		FBattleTriggerSubject Source;
		if (!TryBuildFieldSideOwner(ConditionId, Side, Owner)
			|| !FBattleTriggerSubject::TryCreateBattler(Request.UserBattlerId, Source))
		{
			return false;
		}
		FBattleFieldSideTriggerRegistrationFacts Facts;
		Facts.ConditionId = ConditionId;
		Facts.PayloadId = ConditionId.GetDefinitionId();
		Facts.Owner = Owner;
		Facts.Source = Source;
		Facts.RemainingTurns = RemainingTurns;
		Facts.Layers = Layers;
		EBattleTriggerError Error = EBattleTriggerError::None;
		if (!FBattleFieldSideConditionRules::TryRegisterTriggers(
			TriggerFramework,
			Facts,
			Error))
		{
			return false;
		}
		DrainTriggerOutputs();
		return true;
	}

	bool FStateExecutionContext::TryCleanupFieldSideCondition(
		const FConditionId& ConditionId,
		const TOptional<EBattleSide>& Side,
		const EBattleTriggerCleanupReason Reason)
	{
		FBattleTriggerSubject Owner;
		FBattleTriggerOperationContext Operation;
		EBattleTriggerError Error = EBattleTriggerError::None;
		if (!TryBuildFieldSideOwner(ConditionId, Side, Owner)
			|| !TryTakeTriggerContext(Operation)
			|| !FBattleFieldSideConditionRules::TryCleanupTriggers(
				TriggerFramework,
				ConditionId,
				Owner,
				Reason,
				Operation,
				Error))
		{
			return false;
		}
		DrainTriggerOutputs();
		return true;
	}

	bool FStateExecutionContext::TryUpdateFieldSideLayers(
		const FConditionId& ConditionId,
		const EBattleSide Side,
		const int32 Layers)
	{
		FBattleTriggerSubject Owner;
		FBattleTriggerOperationContext Operation;
		EBattleTriggerError Error = EBattleTriggerError::None;
		if (!FBattleTriggerSubject::TryCreateSide(Side, Owner)
			|| !TryTakeTriggerContext(Operation)
			|| !FBattleFieldSideConditionRules::TryUpdateTriggerLayers(
				TriggerFramework,
				ConditionId,
				Owner,
				Layers,
				Operation,
				Error))
		{
			return false;
		}
		DrainTriggerOutputs();
		return true;
	}

	bool FStateExecutionContext::IsVolatileActiveForPhase(
		const FBattlerId BattlerId,
		const FConditionId& VolatileId,
		const EBattleTriggerPhase Phase) const
	{
		const FBattleBattlerState* Battler = FindBattler(BattlerId);
		if (Battler == nullptr || !HasVolatile(*Battler, VolatileId))
		{
			return false;
		}
		for (const FBattleTriggerRegistrationState& Registration :
			TriggerFramework.GetActiveRegistrations())
		{
			if (!Registration.bSuppressed
				&& Registration.Spec.Owner.Kind == EBattleTriggerSubjectKind::Battler
				&& Registration.Spec.Owner.BattlerId == BattlerId
				&& Registration.Spec.SourceDefinition.Kind
					== EBattleTriggerSourceDefinitionKind::Condition
				&& Registration.Spec.SourceDefinition.ConditionId == VolatileId
				&& Registration.Spec.Rule.Phase == Phase)
			{
				return true;
			}
		}
		return false;
	}

	bool FStateExecutionContext::TryGetVolatilePayloadMoveId(
		const FBattlerId BattlerId,
		const FConditionId& VolatileId,
		FMoveId& OutMoveId) const
	{
		OutMoveId = FMoveId();
		for (const FBattleTriggerRegistrationState& Registration :
			TriggerFramework.GetActiveRegistrations())
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

	bool FStateExecutionContext::TryRegisterVolatile(
		FBattleBattlerState& Battler,
		const FConditionId& VolatileId,
		const FDefinitionId& PayloadId,
		const FBattleTriggerSubject& Source,
		const TArray<FBattleTriggerSubject>& Targets,
		const TOptional<int32>& RemainingTurns,
		const int32 Layers,
		const bool bSuppressed)
	{
		FBattleTriggerSubject Owner;
		FBattleVolatileTriggerRegistrationFacts Facts;
		Facts.VolatileId = VolatileId;
		Facts.PayloadId = PayloadId;
		Facts.Source = Source;
		Facts.Targets = Targets;
		Facts.RemainingTurns = RemainingTurns;
		Facts.Layers = Layers;
		Facts.bSuppressed = bSuppressed;
		EBattleTriggerError Error = EBattleTriggerError::None;
		if (!FBattleTriggerSubject::TryCreateBattler(Battler.BattlerId, Owner))
		{
			return false;
		}
		Facts.Owner = Owner;
		if (!FBattleVolatileRules::TryRegisterTriggers(
			TriggerFramework,
			Facts,
			Error))
		{
			return false;
		}
		DrainTriggerOutputs();

		FBattleConditionState Condition;
		Condition.ConditionId = VolatileId;
		Condition.RemainingTurns = RemainingTurns;
		Condition.LayerCount = Layers;
		Condition.CreationOrdinal = NextConditionCreationOrdinal++;
		Condition.SourceBattlerId = Request.UserBattlerId;
		Battler.Volatiles.Add(MoveTemp(Condition));
		return true;
	}

	bool FStateExecutionContext::TryCleanupVolatile(
		const FBattleBattlerState& Battler,
		const FConditionId& VolatileId,
		const EBattleTriggerCleanupReason Reason)
	{
		FBattleTriggerSubject Owner;
		FBattleTriggerOperationContext Operation;
		EBattleTriggerError Error = EBattleTriggerError::None;
		if (!FBattleTriggerSubject::TryCreateBattler(Battler.BattlerId, Owner)
			|| !TryTakeTriggerContext(Operation)
			|| !FBattleVolatileRules::TryCleanupTriggers(
				TriggerFramework,
				VolatileId,
				Owner,
				Reason,
				Operation,
				Error))
		{
			return false;
		}
		DrainTriggerOutputs();
		return true;
	}

	bool FStateExecutionContext::TryCleanupAllOwnedVolatiles(
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
			if (!TryCleanupVolatile(Battler, Id, Reason))
			{
				return false;
			}
		}
		return true;
	}

	bool FStateExecutionContext::TryCleanupSourceDependentVolatiles(
		const FBattlerId SourceBattlerId,
		const EBattleTriggerCleanupReason Reason)
	{
		for (FBattleBattlerState& Candidate : Battlers)
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
				if (!TryCleanupVolatile(Candidate, Id, Reason))
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

	bool FStateExecutionContext::TrySetVolatileLayers(
		const FBattlerId BattlerId,
		const FConditionId& VolatileId,
		const int32 Layers)
	{
		FBattleTriggerOperationContext Operation;
		if (Layers <= 0 || !TryTakeTriggerContext(Operation))
		{
			return false;
		}
		EBattleTriggerError Error = EBattleTriggerError::None;
		const TArray<FBattleTriggerRegistrationState> Registrations =
			TriggerFramework.GetActiveRegistrations();
		for (const FBattleTriggerRegistrationState& Registration : Registrations)
		{
			if (Registration.Spec.Owner.Kind == EBattleTriggerSubjectKind::Battler
				&& Registration.Spec.Owner.BattlerId == BattlerId
				&& Registration.Spec.SourceDefinition.Kind
					== EBattleTriggerSourceDefinitionKind::Condition
				&& Registration.Spec.SourceDefinition.ConditionId == VolatileId
				&& !TriggerFramework.TryUpdateLayers(
					Registration.RegistrationId,
					Layers,
					Operation,
					Error))
			{
				return false;
			}
		}
		DrainTriggerOutputs();
		return true;
	}

	bool FStateExecutionContext::TrySetVolatileSuppressed(
		const FBattlerId BattlerId,
		const FConditionId& VolatileId,
		const bool bSuppressed)
	{
		FBattleTriggerOperationContext Operation;
		if (!TryTakeTriggerContext(Operation))
		{
			return false;
		}
		EBattleTriggerError Error = EBattleTriggerError::None;
		const TArray<FBattleTriggerRegistrationState> Registrations =
			TriggerFramework.GetActiveRegistrations();
		for (const FBattleTriggerRegistrationState& Registration : Registrations)
		{
			if (Registration.Spec.Owner.Kind == EBattleTriggerSubjectKind::Battler
				&& Registration.Spec.Owner.BattlerId == BattlerId
				&& Registration.Spec.SourceDefinition.Kind
					== EBattleTriggerSourceDefinitionKind::Condition
				&& Registration.Spec.SourceDefinition.ConditionId == VolatileId
				&& !TriggerFramework.TrySetSuppressed(
					Registration.RegistrationId,
					bSuppressed,
					Operation,
					Error))
			{
				return false;
			}
		}
		DrainTriggerOutputs();
		return true;
	}

	bool FStateExecutionContext::TrySetVolatilePhaseSuppressed(
		const FBattlerId BattlerId,
		const FConditionId& VolatileId,
		const EBattleTriggerPhase Phase,
		const bool bSuppressed)
	{
		FBattleTriggerOperationContext Operation;
		if (!TryTakeTriggerContext(Operation))
		{
			return false;
		}
		bool bUpdated = false;
		EBattleTriggerError Error = EBattleTriggerError::None;
		const TArray<FBattleTriggerRegistrationState> Registrations =
			TriggerFramework.GetActiveRegistrations();
		for (const FBattleTriggerRegistrationState& Registration : Registrations)
		{
			if (Registration.Spec.Owner.Kind == EBattleTriggerSubjectKind::Battler
				&& Registration.Spec.Owner.BattlerId == BattlerId
				&& Registration.Spec.SourceDefinition.Kind
					== EBattleTriggerSourceDefinitionKind::Condition
				&& Registration.Spec.SourceDefinition.ConditionId == VolatileId
				&& Registration.Spec.Rule.Phase == Phase)
			{
				if (!TriggerFramework.TrySetSuppressed(
						Registration.RegistrationId,
						bSuppressed,
						Operation,
						Error))
				{
					return false;
				}
				bUpdated = true;
			}
		}
		DrainTriggerOutputs();
		return bUpdated;
	}

	bool FStateExecutionContext::TryTakeTriggerContext(FBattleTriggerOperationContext& OutContext)
	{
		OutContext = FBattleTriggerOperationContext();
		if (NextTriggerReentrancyToken == 0
			|| NextTriggerReentrancyToken == TNumericLimits<uint64>::Max()
			|| !FBattleTriggerReentrancyToken::TryCreate(
				NextTriggerReentrancyToken,
				OutContext.ReentrancyToken))
		{
			return false;
		}
		++NextTriggerReentrancyToken;
		return true;
	}

	bool FStateExecutionContext::TryDispatchStatusPhase(
		const FBattleBattlerState& Battler,
		const EBattleTriggerPhase Phase,
		bool& bOutEmitted)
	{
		bOutEmitted = false;
		FBattleTriggerSubject Owner;
		FBattleTriggerSourceDefinition SourceDefinition;
		FBattleTriggerOperationContext Operation;
		if (!FBattleTriggerSubject::TryCreateBattler(Battler.BattlerId, Owner)
			|| !FBattleTriggerSourceDefinition::TryCreateCondition(
				Battler.MajorStatusId,
				SourceDefinition)
			|| !TryTakeTriggerContext(Operation))
		{
			return false;
		}
		FBattleTriggerDispatchSpec Dispatch;
		Dispatch.Phase = Phase;
		Dispatch.ReentrancyToken = Operation.ReentrancyToken;
		for (const FBattleTriggerRegistrationState& Registration :
			TriggerFramework.GetActiveRegistrations())
		{
			if (Registration.Spec.SourceDefinition == SourceDefinition
				&& Registration.Spec.Owner == Owner
				&& Registration.Spec.Rule.Phase == Phase)
			{
				FBattleTriggerDispatchParticipant& Participant =
					Dispatch.Participants.AddDefaulted_GetRef();
				Participant.RegistrationId = Registration.RegistrationId;
			}
		}
		if (Dispatch.Participants.IsEmpty())
		{
			return true;
		}

		EBattleTriggerError Error = EBattleTriggerError::None;
		FBattleTriggerDispatchResult Result;
		if (!TriggerFramework.TryEnqueueDispatch(Dispatch, Error)
			|| !TriggerFramework.TryResolveNextDispatch(Result, Error))
		{
			return false;
		}
		TArray<FBattleTriggerEffectRequest> Requests;
		TArray<FBattleTriggerLifecycleFact> Facts;
		TriggerFramework.DrainEffectRequests(Requests);
		TriggerFramework.DrainLifecycleFacts(Facts);
		bOutEmitted = !Requests.IsEmpty();
		return true;
	}

	void FStateExecutionContext::DrainTriggerOutputs()
	{
		TArray<FBattleTriggerEffectRequest> IgnoredRequests;
		TArray<FBattleTriggerLifecycleFact> IgnoredFacts;
		TriggerFramework.DrainEffectRequests(IgnoredRequests);
		TriggerFramework.DrainLifecycleFacts(IgnoredFacts);
	}

	bool FStateExecutionContext::TryCleanupCanonicalStatus(const FBattleBattlerState& Battler)
	{
		if (!FBattleMajorStatusRules::IsCanonical(Battler.MajorStatusId))
		{
			return true;
		}
		FBattleTriggerSubject Owner;
		FBattleTriggerOperationContext Operation;
		EBattleTriggerError Error = EBattleTriggerError::None;
		if (!FBattleTriggerSubject::TryCreateBattler(Battler.BattlerId, Owner)
			|| !TryTakeTriggerContext(Operation)
			|| !FBattleMajorStatusRules::TryCleanupTriggers(
				TriggerFramework,
				Battler.MajorStatusId,
				Owner,
				EBattleTriggerCleanupReason::Removal,
				Operation,
				Error))
		{
			return false;
		}
		DrainTriggerOutputs();
		return true;
	}
}
