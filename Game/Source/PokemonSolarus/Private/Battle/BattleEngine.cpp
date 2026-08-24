#include "Battle/BattleEngine.h"
#include "Battle/BattleAbility.h"
#include "Battle/BattleBagItem.h"
#include "Battle/BattleCapture.h"
#include "Battle/BattleEffectExecutor.h"
#include "BattleEntryHazardPrevention.h"
#include "Battle/BattleFieldSideConditions.h"
#include "Battle/BattleFaintOutcomeResolver.h"
#include "Battle/BattleItem.h"
#include "Battle/BattleMajorStatus.h"
#include "Battle/BattleState.h"
#include "Battle/BattleStatCalculator.h"
#include "Battle/BattleSwitching.h"
#include "Battle/BattleVolatile.h"
#include "Battle/BattleWildFlow.h"
#include "Math/NumericLimits.h"

namespace
{
	const FBattleActivePositionState* FindActiveForBattler(
		const FBattleEngineState& State,
		FBattlerId BattlerId);
	bool TryCalculateEffectiveSpeedForOrdering(
		FBattleEngineState& State,
		const FBattleBattlerState& Battler,
		FActiveSlotId ActiveSlotId,
		int32& OutEffectiveSpeed);

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

	bool TryCleanupAbilityTriggers(
		FBattleEngineState& State,
		const FAbilityId& AbilityId,
		const FBattlerId BattlerId,
		const EBattleTriggerCleanupReason Reason)
	{
		if (!FBattleAbilityRules::IsCanonical(AbilityId))
		{
			return true;
		}
		FBattleTriggerSubject Owner;
		FBattleTriggerSourceDefinition SourceDefinition;
		FBattleTriggerOperationContext Operation;
		if (!TryMakeBattlerTriggerSubject(BattlerId, Owner)
			|| !FBattleTriggerSourceDefinition::TryCreateAbility(
				AbilityId,
				SourceDefinition)
			|| !TryTakeTriggerOperationContext(State, Operation))
		{
			return false;
		}

		FBattleTriggerCleanupRequest Cleanup;
		Cleanup.Reason = Reason;
		Cleanup.AffectedOwners.Add(Owner);
		Cleanup.SourceDefinitionFilter = SourceDefinition;
		Cleanup.Context = Operation;
		EBattleTriggerError Error = EBattleTriggerError::None;
		if (!State.TriggerFramework.TryApplyCleanup(Cleanup, Error))
		{
			return false;
		}
		DrainTriggerOutputs(State);
		return true;
	}

	bool TryRegisterAbilityTriggers(
		FBattleEngineState& State,
		const FBattlerId BattlerId)
	{
		const FBattleBattlerState* Battler = State.FindBattler(BattlerId);
		const FBattleActivePositionState* Active = Battler != nullptr
			? FindActiveForBattler(State, BattlerId)
			: nullptr;
		if (Battler == nullptr || Active == nullptr)
		{
			return false;
		}
		if (!FBattleAbilityRules::IsCanonical(Battler->AbilityId))
		{
			return true;
		}

		bool bAlreadyRegistered = false;
		for (const FBattleTriggerRegistrationState& Registration :
			State.TriggerFramework.GetActiveRegistrations())
		{
			if (Registration.Spec.Owner.Kind == EBattleTriggerSubjectKind::Battler
				&& Registration.Spec.Owner.BattlerId == BattlerId
				&& Registration.Spec.SourceDefinition.Kind
					== EBattleTriggerSourceDefinitionKind::Ability
				&& Registration.Spec.SourceDefinition.AbilityId == Battler->AbilityId)
			{
				bAlreadyRegistered = true;
				break;
			}
		}
		if (bAlreadyRegistered
			&& !TryCleanupAbilityTriggers(
				State,
				Battler->AbilityId,
				BattlerId,
				EBattleTriggerCleanupReason::Removal))
		{
			return false;
		}

		FBattleTriggerSubject Owner;
		if (!TryMakeBattlerTriggerSubject(BattlerId, Owner))
		{
			return false;
		}
		FBattleAbilityRegistrationFacts Facts;
		Facts.AbilityId = Battler->AbilityId;
		Facts.Owner = Owner;
		Facts.Source = Owner;
		Facts.bSuppressed = Battler->bAbilitySuppressed;
		if (Battler->AbilityId == FBattleAbilityRules::GetIntimidateId())
		{
			TArray<const FBattleActivePositionState*> Targets;
			for (const FBattleActivePositionState& Position : State.ActivePositions)
			{
				const FBattleBattlerState* Target = State.FindBattler(Position.BattlerId);
				if (Position.bAvailable
					&& Position.ActiveSlotId.GetSide() != Active->ActiveSlotId.GetSide()
					&& Target != nullptr
					&& Target->CurrentHP > 0
					&& !Target->bFainted
					&& !Target->bCaptured
					&& !Target->bRemoved)
				{
					Targets.Add(&Position);
				}
			}
			Targets.Sort(
				[](const FBattleActivePositionState& Left,
					const FBattleActivePositionState& Right)
				{
					const uint8 LeftSide = static_cast<uint8>(Left.ActiveSlotId.GetSide());
					const uint8 RightSide = static_cast<uint8>(Right.ActiveSlotId.GetSide());
					return LeftSide != RightSide
						? LeftSide < RightSide
						: static_cast<uint8>(Left.ActiveSlotId.GetPosition())
							< static_cast<uint8>(Right.ActiveSlotId.GetPosition());
				});
			for (const FBattleActivePositionState* Target : Targets)
			{
				FBattleTriggerSubject TargetSubject;
				if (Target == nullptr
					|| !TryMakeBattlerTriggerSubject(Target->BattlerId, TargetSubject))
				{
					return false;
				}
				Facts.Targets.Add(TargetSubject);
			}
		}
		else
		{
			Facts.Targets.Add(Owner);
		}

		EBattleAbilityItemHookError Error = EBattleAbilityItemHookError::None;
		if (!FBattleAbilityRules::TryRegisterHooks(
				State.TriggerFramework,
				Facts,
				Error))
		{
			return false;
		}
		DrainTriggerOutputs(State);
		return true;
	}

	bool IsHeldItemActive(const FBattleBattlerState& Battler)
	{
		return Battler.HeldItem.CurrentItemId.IsValid()
			&& !Battler.HeldItem.bConsumed
			&& !Battler.HeldItem.bTemporarilyRemoved;
	}

	bool TryCleanupItemTriggers(
		FBattleEngineState& State,
		const FItemId& ItemId,
		const FBattlerId BattlerId,
		const EBattleTriggerCleanupReason Reason)
	{
		if (!FBattleItemRules::IsCanonical(ItemId))
		{
			if (Reason == EBattleTriggerCleanupReason::Faint)
			{
				FBattleBattlerState* Battler = State.FindMutableBattler(BattlerId);
				if (Battler != nullptr)
				{
					Battler->HeldItem.ChoiceLockedMoveId = FMoveId();
				}
			}
			return true;
		}
		FBattleTriggerSubject Owner;
		FBattleTriggerSourceDefinition SourceDefinition;
		FBattleTriggerOperationContext Operation;
		if (!TryMakeBattlerTriggerSubject(BattlerId, Owner)
			|| !FBattleTriggerSourceDefinition::TryCreateItem(ItemId, SourceDefinition)
			|| !TryTakeTriggerOperationContext(State, Operation))
		{
			return false;
		}

		FBattleTriggerCleanupRequest Cleanup;
		Cleanup.Reason = Reason;
		Cleanup.AffectedOwners.Add(Owner);
		Cleanup.SourceDefinitionFilter = SourceDefinition;
		Cleanup.Context = Operation;
		EBattleTriggerError Error = EBattleTriggerError::None;
		if (!State.TriggerFramework.TryApplyCleanup(Cleanup, Error))
		{
			return false;
		}
		DrainTriggerOutputs(State);
		if (Reason == EBattleTriggerCleanupReason::Faint)
		{
			FBattleBattlerState* Battler = State.FindMutableBattler(BattlerId);
			if (Battler == nullptr)
			{
				return false;
			}
			Battler->HeldItem.ChoiceLockedMoveId = FMoveId();
		}
		return true;
	}

	bool TryRegisterItemTriggers(
		FBattleEngineState& State,
		const FBattlerId BattlerId)
	{
		const FBattleBattlerState* Battler = State.FindBattler(BattlerId);
		const FBattleActivePositionState* Active = Battler != nullptr
			? FindActiveForBattler(State, BattlerId)
			: nullptr;
		if (Battler == nullptr || Active == nullptr)
		{
			return false;
		}
		if (!IsHeldItemActive(*Battler)
			|| !FBattleItemRules::IsCanonical(Battler->HeldItem.CurrentItemId))
		{
			return true;
		}

		const FItemId ItemId = Battler->HeldItem.CurrentItemId;
		const bool bAlreadyRegistered = State.TriggerFramework.GetActiveRegistrations().ContainsByPredicate(
			[BattlerId, &ItemId](const FBattleTriggerRegistrationState& Registration)
			{
				return Registration.Spec.Owner.Kind == EBattleTriggerSubjectKind::Battler
					&& Registration.Spec.Owner.BattlerId == BattlerId
					&& Registration.Spec.SourceDefinition.Kind
						== EBattleTriggerSourceDefinitionKind::Item
					&& Registration.Spec.SourceDefinition.ItemId == ItemId;
			});
		if (bAlreadyRegistered
			&& !TryCleanupItemTriggers(
				State,
				ItemId,
				BattlerId,
				EBattleTriggerCleanupReason::Removal))
		{
			return false;
		}

		FBattleTriggerSubject Owner;
		if (!TryMakeBattlerTriggerSubject(BattlerId, Owner))
		{
			return false;
		}
		FBattleItemRegistrationFacts Facts;
		Facts.ItemId = ItemId;
		Facts.Owner = Owner;
		Facts.Source = Owner;
		Facts.Targets.Add(Owner);
		Facts.bSuppressed = Battler->HeldItem.bSuppressed;
		EBattleAbilityItemHookError Error = EBattleAbilityItemHookError::None;
		if (!FBattleItemRules::TryRegisterHooks(State.TriggerFramework, Facts, Error))
		{
			return false;
		}
		DrainTriggerOutputs(State);
		return true;
	}

	bool TryApplyHeldItemLedgerOperation(
		FBattleEngineState& State,
		const FBattlerId BattlerId,
		const EBattleHeldItemOperationKind Kind,
		const bool bSuppressed = false)
	{
		FBattleBattlerState* Battler = State.FindMutableBattler(BattlerId);
		if (Battler == nullptr || !Battler->HeldItem.InstanceId.IsValid())
		{
			return false;
		}
		FBattleHeldItemOperationRequest Request;
		Request.Kind = Kind;
		Request.PrimaryInstanceId = Battler->HeldItem.InstanceId;
		Request.bSuppressed = bSuppressed;
		FBattleHeldItemOperationFact Fact;
		EBattleHeldItemContractError Error = EBattleHeldItemContractError::None;
		if (!State.HeldItemLedger.TryApplyOperation(Request, Fact, Error))
		{
			return false;
		}
		Battler->HeldItem.CurrentItemId = Fact.PrimaryAfter.CurrentItemId;
		Battler->HeldItem.bConsumed = Fact.PrimaryAfter.bConsumed;
		Battler->HeldItem.bSuppressed = Fact.PrimaryAfter.bSuppressed;
		Battler->HeldItem.bRevealed = Fact.PrimaryAfter.bRevealed;
		Battler->HeldItem.bTemporarilyRemoved = Fact.PrimaryAfter.bTemporarilyRemoved;
		if (Kind == EBattleHeldItemOperationKind::Consume
			|| Kind == EBattleHeldItemOperationKind::Remove
			|| (Kind == EBattleHeldItemOperationKind::Suppress && bSuppressed))
		{
			Battler->HeldItem.ChoiceLockedMoveId = FMoveId();
		}
		return true;
	}

	bool TryRevealHeldItem(FBattleEngineState& State, const FBattlerId BattlerId)
	{
		const FBattleBattlerState* Battler = State.FindBattler(BattlerId);
		return Battler != nullptr
			&& (Battler->HeldItem.bRevealed
				|| TryApplyHeldItemLedgerOperation(
					State,
					BattlerId,
					EBattleHeldItemOperationKind::Reveal));
	}

	bool TryConsumeHeldItem(FBattleEngineState& State, const FBattlerId BattlerId)
	{
		const FBattleBattlerState* Battler = State.FindBattler(BattlerId);
		if (Battler == nullptr || !IsHeldItemActive(*Battler))
		{
			return false;
		}
		const FItemId ItemId = Battler->HeldItem.CurrentItemId;
		return TryCleanupItemTriggers(
				State,
				ItemId,
				BattlerId,
				EBattleTriggerCleanupReason::Removal)
			&& TryApplyHeldItemLedgerOperation(
				State,
				BattlerId,
				EBattleHeldItemOperationKind::Consume);
	}

	bool TryRemoveHeldItem(FBattleEngineState& State, const FBattlerId BattlerId)
	{
		const FBattleBattlerState* Battler = State.FindBattler(BattlerId);
		if (Battler == nullptr || !IsHeldItemActive(*Battler))
		{
			return false;
		}
		const FItemId ItemId = Battler->HeldItem.CurrentItemId;
		return TryCleanupItemTriggers(
				State,
				ItemId,
				BattlerId,
				EBattleTriggerCleanupReason::Removal)
			&& TryApplyHeldItemLedgerOperation(
				State,
				BattlerId,
				EBattleHeldItemOperationKind::Remove);
	}

	bool TrySetHeldItemSuppressed(
		FBattleEngineState& State,
		const FBattlerId BattlerId,
		const bool bSuppressed)
	{
		const FBattleBattlerState* Battler = State.FindBattler(BattlerId);
		if (Battler == nullptr)
		{
			return false;
		}
		if (!IsHeldItemActive(*Battler))
		{
			return !Battler->HeldItem.bSuppressed;
		}
		if (Battler->HeldItem.bSuppressed == bSuppressed)
		{
			return true;
		}
		const FItemId ItemId = Battler->HeldItem.CurrentItemId;
		const bool bActive = FindActiveForBattler(State, BattlerId) != nullptr;
		if (bActive
			&& !TryCleanupItemTriggers(
				State,
				ItemId,
				BattlerId,
				EBattleTriggerCleanupReason::Removal))
		{
			return false;
		}
		if (!TryApplyHeldItemLedgerOperation(
				State,
				BattlerId,
				EBattleHeldItemOperationKind::Suppress,
				bSuppressed))
		{
			return false;
		}
		return !bActive || TryRegisterItemTriggers(State, BattlerId);
	}

	bool TrySetAllHeldItemsSuppressed(
		FBattleEngineState& State,
		const bool bSuppressed)
	{
		TArray<FBattlerId> BattlerIds;
		for (const FBattleBattlerState& Battler : State.Battlers)
		{
			if (IsHeldItemActive(Battler))
			{
				BattlerIds.Add(Battler.BattlerId);
			}
		}
		for (const FBattlerId BattlerId : BattlerIds)
		{
			if (!TrySetHeldItemSuppressed(State, BattlerId, bSuppressed))
			{
				return false;
			}
		}
		return true;
	}

	bool TryDispatchAbilityPhase(
		FBattleEngineState& State,
		const TConstArrayView<FBattlerId> Owners,
		const EBattleTriggerPhase Phase,
		TArray<FBattleTriggerEffectRequest>& OutRequests)
	{
		OutRequests.Reset();
		if (Owners.IsEmpty())
		{
			return true;
		}
		FBattleTriggerDispatchSpec Dispatch;
		Dispatch.Phase = Phase;
		Dispatch.OrderPolicy.bUseEffectiveSpeed = true;
		for (const FBattleTriggerRegistrationState& Registration :
			State.TriggerFramework.GetActiveRegistrations())
		{
			if (Registration.Spec.Rule.Phase != Phase
				|| Registration.Spec.SourceDefinition.Kind
					!= EBattleTriggerSourceDefinitionKind::Ability
				|| Registration.Spec.Owner.Kind != EBattleTriggerSubjectKind::Battler
				|| !Owners.Contains(Registration.Spec.Owner.BattlerId))
			{
				continue;
			}
			const FBattleBattlerState* Battler = State.FindBattler(
				Registration.Spec.Owner.BattlerId);
			const FBattleActivePositionState* Active = Battler != nullptr
				? FindActiveForBattler(State, Battler->BattlerId)
				: nullptr;
			if (Battler == nullptr
				|| Active == nullptr
				|| Battler->CurrentHP <= 0
				|| Battler->bFainted
				|| Battler->bCaptured
				|| Battler->bRemoved
				|| Battler->AbilityId != Registration.Spec.SourceDefinition.AbilityId)
			{
				continue;
			}
			int32 EffectiveSpeed = 0;
			if (!TryCalculateEffectiveSpeedForOrdering(
					State,
					*Battler,
					Active->ActiveSlotId,
					EffectiveSpeed))
			{
				return false;
			}
			FBattleTriggerDispatchParticipant& Participant =
				Dispatch.Participants.AddDefaulted_GetRef();
			Participant.RegistrationId = Registration.RegistrationId;
			Participant.EffectiveSpeed = EffectiveSpeed;
			Participant.ActiveSlotId = Active->ActiveSlotId;
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
		TArray<FBattleTriggerLifecycleFact> IgnoredFacts;
		State.TriggerFramework.DrainLifecycleFacts(IgnoredFacts);
		return !Result.bQueuedExpiryDispatch;
	}

	bool TryDispatchItemPhase(
		FBattleEngineState& State,
		const TConstArrayView<FBattlerId> Owners,
		const EBattleTriggerPhase Phase,
		TArray<FBattleTriggerEffectRequest>& OutRequests)
	{
		OutRequests.Reset();
		if (Owners.IsEmpty())
		{
			return true;
		}
		FBattleTriggerDispatchSpec Dispatch;
		Dispatch.Phase = Phase;
		Dispatch.OrderPolicy.bUseEffectiveSpeed = true;
		for (const FBattleTriggerRegistrationState& Registration :
			State.TriggerFramework.GetActiveRegistrations())
		{
			if (Registration.Spec.Rule.Phase != Phase
				|| Registration.Spec.SourceDefinition.Kind
					!= EBattleTriggerSourceDefinitionKind::Item
				|| Registration.Spec.Owner.Kind != EBattleTriggerSubjectKind::Battler
				|| !Owners.Contains(Registration.Spec.Owner.BattlerId))
			{
				continue;
			}
			const FBattleBattlerState* Battler = State.FindBattler(
				Registration.Spec.Owner.BattlerId);
			const FBattleActivePositionState* Active = Battler != nullptr
				? FindActiveForBattler(State, Battler->BattlerId)
				: nullptr;
			if (Battler == nullptr
				|| Active == nullptr
				|| Battler->CurrentHP <= 0
				|| Battler->bFainted
				|| Battler->bCaptured
				|| Battler->bRemoved
				|| !IsHeldItemActive(*Battler)
				|| Battler->HeldItem.CurrentItemId
					!= Registration.Spec.SourceDefinition.ItemId)
			{
				continue;
			}
			int32 EffectiveSpeed = 0;
			if (!TryCalculateEffectiveSpeedForOrdering(
					State,
					*Battler,
					Active->ActiveSlotId,
					EffectiveSpeed))
			{
				return false;
			}
			FBattleTriggerDispatchParticipant& Participant =
				Dispatch.Participants.AddDefaulted_GetRef();
			Participant.RegistrationId = Registration.RegistrationId;
			Participant.EffectiveSpeed = EffectiveSpeed;
			Participant.ActiveSlotId = Active->ActiveSlotId;
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
		TArray<FBattleTriggerLifecycleFact> IgnoredFacts;
		State.TriggerFramework.DrainLifecycleFacts(IgnoredFacts);
		return !Result.bQueuedExpiryDispatch;
	}

	bool TryRecordAbilityActivation(
		FBattleEngineState& State,
		const FBattleTriggerEffectRequest& TriggerRequest,
		const EBattleAbilityItemActivationOutcome Outcome,
		TOptional<FBattleAbilityItemActivationFact>& OutFact)
	{
		OutFact.Reset();
		FBattleAbilityItemEffectRequest Request;
		EBattleAbilityItemHookError Error = EBattleAbilityItemHookError::None;
		return FBattleAbilityRules::TryCreateTypedEffectRequest(
				TriggerRequest,
				Request,
				Error)
			&& State.AbilityItemRevealTracker.TryRecordActivation(
				Request,
				Outcome,
				OutFact,
				Error);
	}

	bool TryRecordItemActivation(
		FBattleEngineState& State,
		const FBattleTriggerEffectRequest& TriggerRequest,
		const EBattleAbilityItemActivationOutcome Outcome,
		TOptional<FBattleAbilityItemActivationFact>& OutFact)
	{
		OutFact.Reset();
		FBattleAbilityItemEffectRequest Request;
		EBattleAbilityItemHookError Error = EBattleAbilityItemHookError::None;
		if (!FBattleItemRules::TryCreateTypedEffectRequest(
				TriggerRequest,
				Request,
				Error)
			|| !State.AbilityItemRevealTracker.TryRecordActivation(
				Request,
				Outcome,
				OutFact,
				Error))
		{
			return false;
		}
		return !OutFact.IsSet()
			|| !OutFact.GetValue().RevealedSourceDefinition.IsSet()
			|| (TriggerRequest.Owner.Kind == EBattleTriggerSubjectKind::Battler
				&& TryRevealHeldItem(State, TriggerRequest.Owner.BattlerId));
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

	bool TryCalculateEffectiveSpeedForOrdering(
		FBattleEngineState& State,
		const FBattleBattlerState& Battler,
		const FActiveSlotId ActiveSlotId,
		int32& OutEffectiveSpeed)
	{
		OutEffectiveSpeed = 0;
		if (!ActiveSlotId.IsValid()
			|| !FBattleStatCalculator::TryCalculateEffectiveStat(
				Battler.PermanentStats,
				Battler.Stages,
				EBattleStat::Speed,
				OutEffectiveSpeed))
		{
			return false;
		}
		if (Battler.MajorStatusId == FBattleMajorStatusRules::GetParalysisId())
		{
			TArray<FBattleTriggerEffectRequest> Requests;
			TArray<FBattleTriggerLifecycleFact> Facts;
			if (!TryDispatchBattlerStatusPhase(
					State,
					Battler,
					EBattleTriggerPhase::ActionOrderCalculation,
					false,
					OutEffectiveSpeed,
					Requests,
					Facts)
				|| Requests.Num() != 1
				|| !FBattleMajorStatusRules::TryApplySpeedModifier(
					Battler.MajorStatusId,
					OutEffectiveSpeed,
					OutEffectiveSpeed))
			{
				return false;
			}
		}

		bool bTailwindTriggerActive = false;
		return TryIsFieldSideConditionActiveForPhase(
				State,
				FBattleFieldSideConditionRules::GetTailwindId(),
				ActiveSlotId.GetSide(),
				EBattleTriggerPhase::ActionOrderCalculation,
				ActiveSlotId,
				bTailwindTriggerActive)
			&& FBattleFieldSideConditionRules::TryApplyTailwindSpeed(
				bTailwindTriggerActive,
				OutEffectiveSpeed,
				OutEffectiveSpeed);
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
		bool& bOutGrounded,
		bool* bOutLevitateMadeAirborne = nullptr)
	{
		if (bOutLevitateMadeAirborne != nullptr)
		{
			*bOutLevitateMadeAirborne = false;
		}
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
		Facts.bAbilityMakesAirborne = FBattleAbilityRules::IsLevitateAirborne(
			Battler.AbilityId,
			Battler.bAbilitySuppressed);
		Facts.bAbilitySuppressed = Battler.bAbilitySuppressed;
		Facts.bItemMakesAirborne = IsHeldItemActive(Battler)
			&& FBattleItemRules::IsAirBalloonAirborne(
				Battler.HeldItem.CurrentItemId,
				Battler.HeldItem.bSuppressed);
		Facts.bItemSuppressed = Battler.HeldItem.bSuppressed;
		Facts.bAirborneSemiInvulnerable = HasVolatile(
			Battler,
			FBattleVolatileRules::GetFlySemiInvulnerableId());
		if (!FBattleFieldSideConditionRules::TryResolveGrounded(Facts, bOutGrounded))
		{
			return false;
		}
		if (bOutLevitateMadeAirborne != nullptr && Facts.bAbilityMakesAirborne)
		{
			FBattleGroundedFacts WithoutLevitate = Facts;
			WithoutLevitate.bAbilityMakesAirborne = false;
			bool bGroundedWithoutLevitate = false;
			if (!FBattleFieldSideConditionRules::TryResolveGrounded(
					WithoutLevitate,
					bGroundedWithoutLevitate))
			{
				return false;
			}
			*bOutLevitateMadeAirborne = bGroundedWithoutLevitate && !bOutGrounded;
		}
		return true;
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

	FBattleEvent ReissueEventWithNextOrdinal(
		FBattleEngineState& State,
		const FBattleEvent& Existing)
	{
		FBattleEventSpec Spec;
		Spec.EventOrdinal = State.NextEventOrdinal;
		Spec.BattleId = Existing.GetBattleId();
		Spec.TurnId = Existing.GetTurnId();
		Spec.ActionId = Existing.GetActionId();
		Spec.ResolutionId = Existing.GetResolutionId();
		Spec.Type = Existing.GetType();
		Spec.Cause = Existing.GetCause();
		Spec.CauseActionKind = Existing.GetCauseActionKind();
		Spec.OutcomeCause = Existing.GetOutcomeCause();
		Spec.Source = Existing.GetSource();
		for (const FBattleEventTarget& Target : Existing.GetTargets())
		{
			Spec.Targets.Add(Target);
		}
		Spec.NumericBefore = Existing.GetNumericBefore();
		Spec.NumericAfter = Existing.GetNumericAfter();
		Spec.NumericDelta = Existing.GetNumericDelta();
		Spec.SimultaneousGroupId = Existing.GetSimultaneousGroupId();
		Spec.HitIndex = Existing.GetHitIndex();
		Spec.HitCount = Existing.GetHitCount();
		Spec.ActionOrder = Existing.GetActionOrder();
		Spec.TargetResolution = Existing.GetTargetResolution();
		Spec.Capture = Existing.GetCapture();
		Spec.Visibility = Existing.GetVisibility();

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
		Spec.Visibility.bRevealSourceDefinition =
			Record.Type == EBattleEventType::AbilityActivated
			|| Record.Type == EBattleEventType::ItemActivated
			|| Record.Type == EBattleEventType::ItemRemoved;

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

	FBattleEvent MakeBagItemMutationEvent(
		FBattleEngineState& State,
		const FResolutionId ResolutionId,
		const FBattleLockedActionState& Action,
		const EBattleEventType Type,
		const FBattleEventTarget& Target,
		const TOptional<int64> NumericBefore = TOptional<int64>(),
		const TOptional<int64> NumericAfter = TOptional<int64>(),
		const TOptional<int64> NumericDelta = TOptional<int64>())
	{
		FBattleEventSpec Spec;
		Spec.EventOrdinal = State.NextEventOrdinal;
		Spec.BattleId = State.Setup.GetBattleId();
		Spec.TurnId = State.TurnId;
		Spec.ActionId = Action.ActionId;
		Spec.ResolutionId = ResolutionId;
		Spec.Type = Type;
		Spec.Cause = EBattleEventCause::Item;
		Spec.CauseActionKind = EBattleActionKind::Bag;
		Spec.Source = SourceFromLockedAction(State, Action);
		Spec.Targets.Add(Target);
		Spec.NumericBefore = NumericBefore;
		Spec.NumericAfter = NumericAfter;
		Spec.NumericDelta = NumericDelta;
		Spec.Visibility.Level = EBattleVisibilityLevel::Public;
		Spec.Visibility.bRevealSourceDefinition =
			Type == EBattleEventType::ItemUsed
			|| Type == EBattleEventType::ItemConsumed;

		FBattleEvent Event;
		const bool bCreated = FBattleEvent::TryCreate(Spec, Event);
		check(bCreated);
		++State.NextEventOrdinal;
		return Event;
	}

	FBattleEvent MakeCaptureEvent(
		FBattleEngineState& State,
		const FResolutionId ResolutionId,
		const FBattleLockedActionState& Action,
		const EBattleEventType Type,
		const FBattleEventTarget& Target,
		const FBattleCaptureEventMetadata& Capture)
	{
		FBattleEventSpec Spec;
		Spec.EventOrdinal = State.NextEventOrdinal;
		Spec.BattleId = State.Setup.GetBattleId();
		Spec.TurnId = State.TurnId;
		Spec.ActionId = Action.ActionId;
		Spec.ResolutionId = ResolutionId;
		Spec.Type = Type;
		Spec.Cause = EBattleEventCause::Capture;
		Spec.CauseActionKind = EBattleActionKind::Bag;
		Spec.Source = SourceFromLockedAction(State, Action);
		Spec.Targets.Add(Target);
		Spec.Capture = Capture;
		Spec.Visibility.Level = EBattleVisibilityLevel::Public;
		Spec.Visibility.bRevealSourceDefinition = true;

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

	FBattleEvent MakeAbilityActivationEvent(
		FBattleEngineState& State,
		const FResolutionId ResolutionId,
		const FActionId ActionId,
		const EBattleActionKind ActionKind,
		const FBattleTriggerEffectRequest& Request,
		const FBattleAbilityItemActivationFact& Fact,
		const FBattleEventTarget* ExplicitTarget = nullptr)
	{
		FBattleEventSpec Spec;
		Spec.EventOrdinal = State.NextEventOrdinal;
		Spec.BattleId = State.Setup.GetBattleId();
		Spec.TurnId = State.TurnId;
		Spec.ActionId = ActionId;
		Spec.ResolutionId = ResolutionId;
		Spec.Type = EBattleEventType::AbilityActivated;
		Spec.Cause = EBattleEventCause::Rule;
		Spec.CauseActionKind = ActionKind;
		const FBattleBattlerState* SourceBattler =
			Request.Source.Kind == EBattleTriggerSubjectKind::Battler
				? State.FindBattler(Request.Source.BattlerId)
				: nullptr;
		if (SourceBattler != nullptr)
		{
			Spec.Source.TrainerId = SourceBattler->TrainerId;
			Spec.Source.BattlerId = SourceBattler->BattlerId;
			const FBattleActivePositionState* SourceActive = FindActiveForBattler(
				State,
				SourceBattler->BattlerId);
			if (SourceActive != nullptr)
			{
				Spec.Source.ActiveSlotId = SourceActive->ActiveSlotId;
			}
		}
		Spec.Source.DefinitionId = Request.SourceDefinition.AbilityId.GetDefinitionId();
		if (ExplicitTarget != nullptr)
		{
			Spec.Targets.Add(*ExplicitTarget);
		}
		else
		{
			for (const FBattleTriggerSubject& Subject : Request.Targets)
			{
				FBattleEventTarget Target;
				if (Subject.Kind == EBattleTriggerSubjectKind::Battler)
				{
					const FBattleBattlerState* TargetBattler = State.FindBattler(
						Subject.BattlerId);
					if (TargetBattler == nullptr)
					{
						continue;
					}
					Target.TrainerId = TargetBattler->TrainerId;
					Target.BattlerId = TargetBattler->BattlerId;
					const FBattleActivePositionState* TargetActive = FindActiveForBattler(
						State,
						TargetBattler->BattlerId);
					if (TargetActive != nullptr)
					{
						Target.ActiveSlotId = TargetActive->ActiveSlotId;
					}
				}
				else if (Subject.Kind == EBattleTriggerSubjectKind::ActiveSlot)
				{
					Target.ActiveSlotId = Subject.ActiveSlotId;
					const FBattleActivePositionState* TargetActive = State.FindActivePosition(
						Subject.ActiveSlotId);
					if (TargetActive != nullptr)
					{
						Target.TrainerId = TargetActive->TrainerId;
						Target.BattlerId = TargetActive->BattlerId;
					}
				}
				else if (Subject.Kind == EBattleTriggerSubjectKind::Side)
				{
					Target.Side = Subject.Side;
					Target.bHasSide = true;
				}
				else if (Subject.Kind == EBattleTriggerSubjectKind::Field)
				{
					Target.bField = true;
				}
				else
				{
					continue;
				}
				Spec.Targets.Add(MoveTemp(Target));
			}
		}
		Spec.NumericBefore = Fact.bFirstPublicReveal ? 0 : 1;
		Spec.NumericAfter = 1;
		Spec.NumericDelta = Fact.bFirstPublicReveal ? 1 : 0;
		Spec.Visibility.Level = EBattleVisibilityLevel::Public;
		Spec.Visibility.bRevealSourceDefinition = Fact.RevealedSourceDefinition.IsSet();

		FBattleEvent Event;
		const bool bCreated = FBattleEvent::TryCreate(Spec, Event);
		check(bCreated);
		++State.NextEventOrdinal;
		return Event;
	}

	FBattleEvent MakeItemActivationEvent(
		FBattleEngineState& State,
		const FResolutionId ResolutionId,
		const FActionId ActionId,
		const EBattleActionKind ActionKind,
		const FBattleTriggerEffectRequest& Request,
		const FBattleAbilityItemActivationFact& Fact,
		const FBattleEventTarget* ExplicitTarget = nullptr)
	{
		FBattleEventSpec Spec;
		Spec.EventOrdinal = State.NextEventOrdinal;
		Spec.BattleId = State.Setup.GetBattleId();
		Spec.TurnId = State.TurnId;
		Spec.ActionId = ActionId;
		Spec.ResolutionId = ResolutionId;
		Spec.Type = EBattleEventType::ItemActivated;
		Spec.Cause = EBattleEventCause::Item;
		Spec.CauseActionKind = ActionKind;
		const FBattleBattlerState* SourceBattler =
			Request.Source.Kind == EBattleTriggerSubjectKind::Battler
				? State.FindBattler(Request.Source.BattlerId)
				: nullptr;
		if (SourceBattler != nullptr)
		{
			Spec.Source.TrainerId = SourceBattler->TrainerId;
			Spec.Source.BattlerId = SourceBattler->BattlerId;
			const FBattleActivePositionState* SourceActive = FindActiveForBattler(
				State,
				SourceBattler->BattlerId);
			if (SourceActive != nullptr)
			{
				Spec.Source.ActiveSlotId = SourceActive->ActiveSlotId;
			}
		}
		Spec.Source.DefinitionId = Request.SourceDefinition.ItemId.GetDefinitionId();
		if (ExplicitTarget != nullptr)
		{
			Spec.Targets.Add(*ExplicitTarget);
		}
		else
		{
			for (const FBattleTriggerSubject& Subject : Request.Targets)
			{
				if (Subject.Kind != EBattleTriggerSubjectKind::Battler)
				{
					continue;
				}
				const FBattleBattlerState* TargetBattler = State.FindBattler(Subject.BattlerId);
				if (TargetBattler == nullptr)
				{
					continue;
				}
				FBattleEventTarget Target;
				Target.TrainerId = TargetBattler->TrainerId;
				Target.BattlerId = TargetBattler->BattlerId;
				const FBattleActivePositionState* TargetActive = FindActiveForBattler(
					State,
					TargetBattler->BattlerId);
				if (TargetActive != nullptr)
				{
					Target.ActiveSlotId = TargetActive->ActiveSlotId;
				}
				Spec.Targets.Add(MoveTemp(Target));
			}
		}
		Spec.NumericBefore = Fact.bFirstPublicReveal ? 0 : 1;
		Spec.NumericAfter = 1;
		Spec.NumericDelta = Fact.bFirstPublicReveal ? 1 : 0;
		Spec.Visibility.Level = EBattleVisibilityLevel::Public;
		Spec.Visibility.bRevealSourceDefinition = Fact.RevealedSourceDefinition.IsSet();

		FBattleEvent Event;
		const bool bCreated = FBattleEvent::TryCreate(Spec, Event);
		check(bCreated);
		++State.NextEventOrdinal;
		return Event;
	}

	bool TryAppendAbilityActivationForPhase(
		FBattleEngineState& State,
		const FBattlerId BattlerId,
		const EBattleTriggerPhase Phase,
		const EBattleAbilityItemActivationOutcome Outcome,
		const FResolutionId ResolutionId,
		const FActionId ActionId,
		const EBattleActionKind ActionKind,
		TArray<FBattleEvent>& Events,
		const FBattleEventTarget* ExplicitTarget = nullptr)
	{
		TArray<FBattleTriggerEffectRequest> Requests;
		const TArray<FBattlerId> Owners{BattlerId};
		if (!TryDispatchAbilityPhase(State, Owners, Phase, Requests))
		{
			return false;
		}
		const FBattleBattlerState* Battler = State.FindBattler(BattlerId);
		const FBattleTriggerEffectRequest* Request = Battler != nullptr
			? Requests.FindByPredicate(
				[Battler](const FBattleTriggerEffectRequest& Candidate)
				{
					return Candidate.SourceDefinition.Kind
						== EBattleTriggerSourceDefinitionKind::Ability
						&& Candidate.SourceDefinition.AbilityId == Battler->AbilityId;
				})
			: nullptr;
		if (Request == nullptr)
		{
			return Outcome != EBattleAbilityItemActivationOutcome::Applied
				&& Outcome != EBattleAbilityItemActivationOutcome::AttemptedButPrevented;
		}
		TOptional<FBattleAbilityItemActivationFact> Fact;
		if (!TryRecordAbilityActivation(State, *Request, Outcome, Fact))
		{
			return false;
		}
		if (Fact.IsSet())
		{
			Events.Add(MakeAbilityActivationEvent(
				State,
				ResolutionId,
				ActionId,
				ActionKind,
				*Request,
				Fact.GetValue(),
				ExplicitTarget));
		}
		return true;
	}

	bool TryAppendItemActivationForPhase(
		FBattleEngineState& State,
		const FBattlerId BattlerId,
		const EBattleTriggerPhase Phase,
		const EBattleAbilityItemActivationOutcome Outcome,
		const FResolutionId ResolutionId,
		const FActionId ActionId,
		const EBattleActionKind ActionKind,
		TArray<FBattleEvent>& Events,
		const FBattleEventTarget* ExplicitTarget = nullptr)
	{
		TArray<FBattleTriggerEffectRequest> Requests;
		const TArray<FBattlerId> Owners{BattlerId};
		if (!TryDispatchItemPhase(State, Owners, Phase, Requests))
		{
			return false;
		}
		const FBattleBattlerState* Battler = State.FindBattler(BattlerId);
		const FBattleTriggerEffectRequest* Request = Battler != nullptr
			? Requests.FindByPredicate(
				[Battler](const FBattleTriggerEffectRequest& Candidate)
				{
					return Candidate.SourceDefinition.Kind
						== EBattleTriggerSourceDefinitionKind::Item
						&& Candidate.SourceDefinition.ItemId
							== Battler->HeldItem.CurrentItemId;
				})
			: nullptr;
		if (Request == nullptr)
		{
			return Outcome != EBattleAbilityItemActivationOutcome::Applied
				&& Outcome != EBattleAbilityItemActivationOutcome::AttemptedButPrevented;
		}
		TOptional<FBattleAbilityItemActivationFact> Fact;
		if (!TryRecordItemActivation(State, *Request, Outcome, Fact))
		{
			return false;
		}
		if (Fact.IsSet())
		{
			Events.Add(MakeItemActivationEvent(
				State,
				ResolutionId,
				ActionId,
				ActionKind,
				*Request,
				Fact.GetValue(),
				ExplicitTarget));
		}
		return true;
	}

	FBattleEvent MakeHeldItemMutationEvent(
		FBattleEngineState& State,
		const FResolutionId ResolutionId,
		const FActionId ActionId,
		const EBattleActionKind ActionKind,
		const EBattleEventType Type,
		const FBattlerId BattlerId,
		const FActiveSlotId ActiveSlotId,
		const FItemId& ItemId,
		const int64 NumericBefore,
		const int64 NumericAfter,
		const int64 NumericDelta)
	{
		const FBattleBattlerState* Battler = State.FindBattler(BattlerId);
		check(Battler != nullptr);
		FBattleEventSpec Spec;
		Spec.EventOrdinal = State.NextEventOrdinal;
		Spec.BattleId = State.Setup.GetBattleId();
		Spec.TurnId = State.TurnId;
		Spec.ActionId = ActionId;
		Spec.ResolutionId = ResolutionId;
		Spec.Type = Type;
		Spec.Cause = EBattleEventCause::Item;
		Spec.CauseActionKind = ActionKind;
		Spec.Source.TrainerId = Battler->TrainerId;
		Spec.Source.BattlerId = BattlerId;
		Spec.Source.ActiveSlotId = ActiveSlotId;
		Spec.Source.DefinitionId = ItemId.GetDefinitionId();
		FBattleEventTarget Target;
		Target.TrainerId = Battler->TrainerId;
		Target.BattlerId = BattlerId;
		Target.ActiveSlotId = ActiveSlotId;
		Spec.Targets.Add(Target);
		Spec.NumericBefore = NumericBefore;
		Spec.NumericAfter = NumericAfter;
		Spec.NumericDelta = NumericDelta;
		Spec.Visibility.Level = EBattleVisibilityLevel::Public;
		Spec.Visibility.bRevealSourceDefinition = true;
		FBattleEvent Event;
		const bool bCreated = FBattleEvent::TryCreate(Spec, Event);
		check(bCreated);
		++State.NextEventOrdinal;
		return Event;
	}

	bool TryFindItemRequestForPhase(
		FBattleEngineState& State,
		const FBattlerId BattlerId,
		const FItemId& ItemId,
		const EBattleTriggerPhase Phase,
		FBattleTriggerEffectRequest& OutRequest)
	{
		OutRequest = FBattleTriggerEffectRequest();
		TArray<FBattleTriggerEffectRequest> Requests;
		const TArray<FBattlerId> Owners{BattlerId};
		if (!TryDispatchItemPhase(State, Owners, Phase, Requests))
		{
			return false;
		}
		const FBattleTriggerEffectRequest* Request = Requests.FindByPredicate(
			[&ItemId](const FBattleTriggerEffectRequest& Candidate)
			{
				return Candidate.SourceDefinition.Kind
					== EBattleTriggerSourceDefinitionKind::Item
					&& Candidate.SourceDefinition.ItemId == ItemId;
			});
		if (Request == nullptr)
		{
			return false;
		}
		OutRequest = *Request;
		return true;
	}

	bool TryResolveImmediateHeldItem(
		FBattleEngineState& State,
		const FBattlerId BattlerId,
		const FResolutionId ResolutionId,
		const FActionId ActionId,
		const EBattleActionKind ActionKind,
		TArray<FBattleEvent>& Events)
	{
		FBattleBattlerState* Battler = State.FindMutableBattler(BattlerId);
		const FBattleActivePositionState* Active = Battler != nullptr
			? FindActiveForBattler(State, BattlerId)
			: nullptr;
		if (Battler == nullptr)
		{
			return false;
		}
		if (Active == nullptr)
		{
			return true;
		}
		if (!IsHeldItemActive(*Battler))
		{
			return true;
		}
		const FItemId ItemId = Battler->HeldItem.CurrentItemId;
		if (ItemId == FBattleItemRules::GetSitrusBerryId())
		{
			FBattleItemRecoveryFacts Facts;
			Facts.ItemId = ItemId;
			Facts.CurrentHP = Battler->CurrentHP;
			Facts.BaseMaximumHP = Battler->PermanentStats.MaxHP;
			Facts.bHealingPermitted = Battler->CurrentHP > 0
				&& !Battler->bFainted
				&& !Battler->bCaptured
				&& !Battler->bRemoved;
			Facts.bSuppressed = Battler->HeldItem.bSuppressed;
			FBattleItemRecoveryResult Result;
			if (!FBattleItemRules::TryEvaluateRecovery(Facts, Result))
			{
				return false;
			}
			if (!Result.bApplies)
			{
				return true;
			}
			FBattleTriggerEffectRequest Request;
			TOptional<FBattleAbilityItemActivationFact> Activation;
			if (!TryFindItemRequestForPhase(
					State,
					BattlerId,
					ItemId,
					EBattleTriggerPhase::AfterDamage,
					Request)
				|| !TryRecordItemActivation(
					State,
					Request,
					EBattleAbilityItemActivationOutcome::Applied,
					Activation)
				|| !Activation.IsSet())
			{
				return false;
			}
			const int32 PreviousHP = Battler->CurrentHP;
			const FActiveSlotId ActiveSlotId = Active->ActiveSlotId;
			if (!TryConsumeHeldItem(State, BattlerId))
			{
				return false;
			}
			Battler->CurrentHP += Result.HealAmount;
			Events.Add(MakeItemActivationEvent(
				State,
				ResolutionId,
				ActionId,
				ActionKind,
				Request,
				Activation.GetValue()));
			Events.Add(MakeHeldItemMutationEvent(
				State, ResolutionId, ActionId, ActionKind,
				EBattleEventType::ItemConsumed, BattlerId, ActiveSlotId, ItemId,
				1, 0, -1));
			for (const EBattleEventType Type : {
				EBattleEventType::Healing,
				EBattleEventType::HPChanged})
			{
				Events.Add(MakeHeldItemMutationEvent(
					State, ResolutionId, ActionId, ActionKind,
					Type, BattlerId, ActiveSlotId, ItemId,
					PreviousHP, Battler->CurrentHP, Result.HealAmount));
			}
			return true;
		}

		if (ItemId != FBattleItemRules::GetLumBerryId())
		{
			return true;
		}
		const bool bHasMajorStatus = FBattleMajorStatusRules::IsCanonical(
			Battler->MajorStatusId);
		const bool bHasConfusion = HasVolatile(
			*Battler,
			FBattleVolatileRules::GetConfusionId());
		FBattleLumBerryFacts Facts;
		Facts.ItemId = ItemId;
		Facts.bHolderAbleToBattle = Battler->CurrentHP > 0
			&& !Battler->bFainted
			&& !Battler->bCaptured
			&& !Battler->bRemoved;
		Facts.bHasMajorStatus = bHasMajorStatus;
		Facts.bHasConfusion = bHasConfusion;
		Facts.bSuppressed = Battler->HeldItem.bSuppressed;
		FBattleLumBerryResult Result;
		if (!FBattleItemRules::TryEvaluateLumBerry(Facts, Result))
		{
			return false;
		}
		if (!Result.bApplies)
		{
			return true;
		}
		FBattleTriggerEffectRequest Request;
		TOptional<FBattleAbilityItemActivationFact> Activation;
		if (!TryFindItemRequestForPhase(
				State,
				BattlerId,
				ItemId,
				EBattleTriggerPhase::AfterHit,
				Request)
			|| !TryRecordItemActivation(
				State,
				Request,
				EBattleAbilityItemActivationOutcome::Applied,
				Activation)
			|| !Activation.IsSet())
		{
			return false;
		}
		const FActiveSlotId ActiveSlotId = Active->ActiveSlotId;
		const int32 CuredCount = (bHasMajorStatus ? 1 : 0) + (bHasConfusion ? 1 : 0);
		const FConditionId MajorStatusId = Battler->MajorStatusId;
		if (!TryConsumeHeldItem(State, BattlerId)
			|| (bHasMajorStatus
				&& !TryCleanupMajorStatusTriggers(
					State,
					MajorStatusId,
					BattlerId,
					EBattleTriggerCleanupReason::Removal))
			|| (bHasConfusion
				&& !TryCleanupVolatileTriggers(
					State,
					FBattleVolatileRules::GetConfusionId(),
					BattlerId,
					EBattleTriggerCleanupReason::Removal)))
		{
			return false;
		}
		if (bHasMajorStatus)
		{
			Battler->MajorStatusId = FConditionId();
		}
		if (bHasConfusion)
		{
			Battler->Volatiles.RemoveAll(
				[](const FBattleConditionState& Condition)
				{
					return Condition.ConditionId == FBattleVolatileRules::GetConfusionId();
				});
		}
		Events.Add(MakeItemActivationEvent(
			State,
			ResolutionId,
			ActionId,
			ActionKind,
			Request,
			Activation.GetValue()));
		Events.Add(MakeHeldItemMutationEvent(
			State, ResolutionId, ActionId, ActionKind,
			EBattleEventType::ItemConsumed, BattlerId, ActiveSlotId, ItemId,
			1, 0, -1));
		Events.Add(MakeHeldItemMutationEvent(
			State, ResolutionId, ActionId, ActionKind,
			EBattleEventType::StatusChanged, BattlerId, ActiveSlotId, ItemId,
			CuredCount, 0, -CuredCount));
		return true;
	}

	bool TryRevealAirBalloonOnEntry(
		FBattleEngineState& State,
		const FBattlerId BattlerId,
		const FResolutionId ResolutionId,
		const EBattleActionKind ActionKind,
		TArray<FBattleEvent>& Events)
	{
		const FBattleBattlerState* Battler = State.FindBattler(BattlerId);
		const FBattleActivePositionState* Active = Battler != nullptr
			? FindActiveForBattler(State, BattlerId)
			: nullptr;
		if (Battler == nullptr || Active == nullptr)
		{
			return false;
		}
		if (!IsHeldItemActive(*Battler)
			|| Battler->HeldItem.CurrentItemId != FBattleItemRules::GetAirBalloonId()
			|| Battler->HeldItem.bSuppressed)
		{
			return true;
		}
		FBattleTriggerEffectRequest Request;
		TOptional<FBattleAbilityItemActivationFact> Activation;
		if (!TryFindItemRequestForPhase(
				State,
				BattlerId,
				Battler->HeldItem.CurrentItemId,
				EBattleTriggerPhase::SwitchIn,
				Request)
			|| !TryRecordItemActivation(
				State,
				Request,
				EBattleAbilityItemActivationOutcome::Applied,
				Activation)
			|| !Activation.IsSet())
		{
			return false;
		}
		FBattleEventTarget Target;
		Target.TrainerId = Battler->TrainerId;
		Target.BattlerId = BattlerId;
		Target.ActiveSlotId = Active->ActiveSlotId;
		Events.Add(MakeItemActivationEvent(
			State,
			ResolutionId,
			FActionId(),
			ActionKind,
			Request,
			Activation.GetValue(),
			&Target));
		return true;
	}

	bool TryResolveAbilityEntries(
		FBattleEngineState& State,
		const TConstArrayView<FBattlerId> Entrants,
		const FResolutionId ResolutionId,
		const EBattleActionKind ActionKind,
		TArray<FBattleEvent>& Events)
	{
		TArray<FBattleTriggerEffectRequest> Requests;
		if (!TryDispatchAbilityPhase(
				State,
				Entrants,
				EBattleTriggerPhase::SwitchIn,
				Requests))
		{
			return false;
		}
		for (const FBattleTriggerEffectRequest& Request : Requests)
		{
			if (Request.SourceDefinition.Kind
				!= EBattleTriggerSourceDefinitionKind::Ability
				|| Request.Owner.Kind != EBattleTriggerSubjectKind::Battler)
			{
				continue;
			}
			FBattleBattlerState* Owner = State.FindMutableBattler(
				Request.Owner.BattlerId);
			const FBattleActivePositionState* OwnerActive = Owner != nullptr
				? FindActiveForBattler(State, Owner->BattlerId)
				: nullptr;
			if (Owner == nullptr
				|| OwnerActive == nullptr
				|| Owner->CurrentHP <= 0
				|| Owner->bFainted
				|| Owner->bCaptured
				|| Owner->bRemoved
				|| Owner->AbilityId != Request.SourceDefinition.AbilityId)
			{
				continue;
			}

			const EBattleAbilityKind Kind = FBattleAbilityRules::GetKind(Owner->AbilityId);
			if (Kind == EBattleAbilityKind::Intimidate)
			{
				struct FPendingIntimidateMutation
				{
					FBattleEventTarget Target;
					FBattleStatStageChangeResult Change;
				};
				EBattleAbilityItemActivationOutcome OverallOutcome =
					EBattleAbilityItemActivationOutcome::Ineligible;
				TArray<FPendingIntimidateMutation> Mutations;
				for (const FBattleTriggerSubject& TargetSubject : Request.Targets)
				{
					FBattleBattlerState* Target =
						TargetSubject.Kind == EBattleTriggerSubjectKind::Battler
							? State.FindMutableBattler(TargetSubject.BattlerId)
							: nullptr;
					const FBattleActivePositionState* TargetActive = Target != nullptr
						? FindActiveForBattler(State, Target->BattlerId)
						: nullptr;
					if (Target == nullptr || TargetActive == nullptr)
					{
						continue;
					}
					bool bMistActive = false;
					if (!TryIsFieldSideConditionActiveForPhase(
							State,
							FBattleFieldSideConditionRules::GetMistId(),
							TargetActive->ActiveSlotId.GetSide(),
							EBattleTriggerPhase::BeforeHit,
							TargetActive->ActiveSlotId,
							bMistActive))
					{
						return false;
					}
					FBattleIntimidateTargetFacts Facts;
					Facts.bAdjacentOpponent = TargetActive->ActiveSlotId.GetSide()
						!= OwnerActive->ActiveSlotId.GetSide();
					Facts.bTargetAbleToBattle = Target->CurrentHP > 0
						&& !Target->bFainted
						&& !Target->bCaptured
						&& !Target->bRemoved;
					Facts.bSubstituteActive = HasVolatile(
						*Target,
						FBattleVolatileRules::GetSubstituteId());
					Facts.bStatStageDropPrevented =
						FBattleFieldSideConditionRules::ShouldMistPreventStatDrop(
							bMistActive,
							true,
							false,
							-1);
					if (!Target->Stages.TryGetStage(
							EBattleStat::Attack,
							Facts.CurrentAttackStage))
					{
						return false;
					}
					Facts.bSuppressed = Owner->bAbilitySuppressed;
					FBattleIntimidateTargetResult Result;
					if (!FBattleAbilityRules::TryEvaluateIntimidateTarget(Facts, Result))
					{
						return false;
					}
					if (Result.Outcome == EBattleAbilityItemActivationOutcome::Applied)
					{
						OverallOutcome = EBattleAbilityItemActivationOutcome::Applied;
						const FBattleStatStageChangeResult Change = Target->Stages.ApplyChange(
							EBattleStat::Attack,
							Result.AttackStageDelta);
						if (Change.Outcome != EBattleStatStageChangeOutcome::Applied)
						{
							return false;
						}
						FPendingIntimidateMutation& Mutation =
							Mutations.AddDefaulted_GetRef();
						Mutation.Target.TrainerId = Target->TrainerId;
						Mutation.Target.BattlerId = Target->BattlerId;
						Mutation.Target.ActiveSlotId = TargetActive->ActiveSlotId;
						Mutation.Change = Change;
					}
					else if (Result.Outcome
						== EBattleAbilityItemActivationOutcome::AttemptedButPrevented
						&& OverallOutcome != EBattleAbilityItemActivationOutcome::Applied)
					{
						OverallOutcome = Result.Outcome;
					}
				}
				TOptional<FBattleAbilityItemActivationFact> Fact;
				if (!TryRecordAbilityActivation(State, Request, OverallOutcome, Fact))
				{
					return false;
				}
				if (Fact.IsSet())
				{
					Events.Add(MakeAbilityActivationEvent(
						State,
						ResolutionId,
						FActionId(),
						ActionKind,
						Request,
						Fact.GetValue()));
				}
				FBattleEventSource Source;
				Source.TrainerId = Owner->TrainerId;
				Source.BattlerId = Owner->BattlerId;
				Source.ActiveSlotId = OwnerActive->ActiveSlotId;
				Source.DefinitionId = Owner->AbilityId.GetDefinitionId();
				for (const FPendingIntimidateMutation& Mutation : Mutations)
				{
					Events.Add(MakeRuleMutationEvent(
						State,
						ResolutionId,
						EBattleEventType::StatStageChanged,
						ActionKind,
						Source,
						Mutation.Target,
						Mutation.Change.PreviousStage,
						Mutation.Change.NewStage,
						Mutation.Change.AppliedDelta));
				}
				continue;
			}

			if (Kind == EBattleAbilityKind::Drizzle)
			{
				const FConditionId ExistingWeather = State.Field.Weather.IsSet()
					? State.Field.Weather.GetValue().ConditionId
					: FConditionId();
				FBattleDrizzleEntryResult Drizzle;
				if (!FBattleAbilityRules::TryEvaluateDrizzleEntry(
						Owner->AbilityId,
						ExistingWeather,
						Owner->bAbilitySuppressed,
						Drizzle))
				{
					return false;
				}
				if (Drizzle.Outcome != EBattleAbilityItemActivationOutcome::Applied)
				{
					TOptional<FBattleAbilityItemActivationFact> IgnoredFact;
					if (!TryRecordAbilityActivation(
							State,
							Request,
							Drizzle.Outcome,
							IgnoredFact))
					{
						return false;
					}
					continue;
				}

				FBattleFieldSideApplicationFacts ApplicationFacts;
				ApplicationFacts.RequestedConditionId = Drizzle.RainId;
				if (ExistingWeather.IsValid())
				{
					ApplicationFacts.ExistingExclusiveConditionId = ExistingWeather;
				}
				ApplicationFacts.bRequestedAlreadyActive = ExistingWeather == Drizzle.RainId;
				FBattleFieldSideApplicationResult Application;
				if (!FBattleFieldSideConditionRules::TryEvaluateApplication(
						ApplicationFacts,
						Application)
					|| (Application.Outcome != EBattleFieldSideApplicationOutcome::Create
						&& Application.Outcome
							!= EBattleFieldSideApplicationOutcome::ReplaceExclusive))
				{
					return false;
				}
				if (ExistingWeather.IsValid()
					&& FBattleFieldSideConditionRules::IsCanonical(ExistingWeather)
					&& !TryCleanupFieldSideTriggers(
						State,
						ExistingWeather,
						TOptional<EBattleSide>(),
						EBattleTriggerCleanupReason::Removal))
				{
					return false;
				}
				FBattleTriggerSubject SourceSubject;
				if (!TryMakeBattlerTriggerSubject(Owner->BattlerId, SourceSubject))
				{
					return false;
				}
				FBattleFieldSideTriggerRegistrationFacts TriggerFacts;
				TriggerFacts.ConditionId = Drizzle.RainId;
				TriggerFacts.PayloadId = Drizzle.RainId.GetDefinitionId();
				TriggerFacts.Owner = FBattleTriggerSubject::CreateField();
				TriggerFacts.Source = SourceSubject;
				TriggerFacts.RemainingTurns = Drizzle.DurationTurns;
				TriggerFacts.Layers = 1;
				EBattleTriggerError TriggerError = EBattleTriggerError::None;
				if (!FBattleFieldSideConditionRules::TryRegisterTriggers(
						State.TriggerFramework,
						TriggerFacts,
						TriggerError))
				{
					return false;
				}
				DrainTriggerOutputs(State);
				FBattleConditionState Rain;
				Rain.ConditionId = Drizzle.RainId;
				Rain.RemainingTurns = Drizzle.DurationTurns;
				Rain.LayerCount = 1;
				Rain.CreationOrdinal = State.NextConditionCreationOrdinal++;
				Rain.SourceBattlerId = Owner->BattlerId;
				State.Field.Weather = Rain;

				TOptional<FBattleAbilityItemActivationFact> Fact;
				if (!TryRecordAbilityActivation(
						State,
						Request,
						EBattleAbilityItemActivationOutcome::Applied,
						Fact)
					|| !Fact.IsSet())
				{
					return false;
				}
				Events.Add(MakeAbilityActivationEvent(
					State,
					ResolutionId,
					FActionId(),
					ActionKind,
					Request,
					Fact.GetValue()));
				FBattleEventSource Source;
				Source.TrainerId = Owner->TrainerId;
				Source.BattlerId = Owner->BattlerId;
				Source.ActiveSlotId = OwnerActive->ActiveSlotId;
				Source.DefinitionId = Owner->AbilityId.GetDefinitionId();
				FBattleEventTarget FieldTarget;
				FieldTarget.bField = true;
				Events.Add(MakeRuleMutationEvent(
					State,
					ResolutionId,
					EBattleEventType::FieldEffectChanged,
					ActionKind,
					Source,
					FieldTarget,
					ExistingWeather.IsValid() ? 1 : 0,
					1,
					ExistingWeather.IsValid() ? 0 : 1));
				continue;
			}

			if (Kind == EBattleAbilityKind::MoldBreaker)
			{
				TOptional<FBattleAbilityItemActivationFact> Fact;
				const EBattleAbilityItemActivationOutcome Outcome = Owner->bAbilitySuppressed
					? EBattleAbilityItemActivationOutcome::Suppressed
					: EBattleAbilityItemActivationOutcome::Applied;
				if (!TryRecordAbilityActivation(State, Request, Outcome, Fact))
				{
					return false;
				}
				if (Fact.IsSet())
				{
					Events.Add(MakeAbilityActivationEvent(
						State,
						ResolutionId,
						FActionId(),
						ActionKind,
						Request,
						Fact.GetValue()));
				}
			}
		}
		return true;
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
			bool bLevitateMadeAirborne = false;
			if (!TryResolveGrounded(
					State,
					*Incoming,
					bGrounded,
					&bLevitateMadeAirborne))
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
			const bool bBootsBypassActive = IsHeldItemActive(*Incoming)
				&& FBattleItemRules::ShouldBypassEntryHazards(
					Incoming->HeldItem.CurrentItemId,
					Incoming->HeldItem.bSuppressed);
			const bool bDamagingHazardWouldApply =
				(HazardId == FBattleFieldSideConditionRules::GetSpikesId()
					&& bGrounded)
				|| (HazardId == FBattleFieldSideConditionRules::GetStealthRockId()
					&& !RockEffectiveness.IsImmune());
			const bool bMagicGuardWouldPreventDamage = bDamagingHazardWouldApply
				&& FBattleAbilityRules::ShouldMagicGuardPreventDamage(
					Incoming->AbilityId,
					EBattleHPChangeSourceKind::Condition,
					Incoming->bAbilitySuppressed);
			const BattleEntryHazardPrevention::FResult Prevention =
				BattleEntryHazardPrevention::Resolve(
					bBootsBypassActive,
					bMagicGuardWouldPreventDamage);
			Facts.bBypassesEntryHazards = Prevention.bBypassesEntryHazards;
			Facts.bIndirectDamagePrevented = Prevention.bIndirectDamagePrevented;
			FBattleHazardSwitchInResult HazardResult;
			if (!FBattleFieldSideConditionRules::TryResolveHazardSwitchIn(Facts, HazardResult))
			{
				return false;
			}
			const bool bMagicGuardPreventedDamage = Facts.bIndirectDamagePrevented;
			bool bLevitatePreventedHazard = false;
			if (bLevitateMadeAirborne)
			{
				FBattleHazardSwitchInFacts GroundedFacts = Facts;
				GroundedFacts.bGrounded = true;
				FBattleHazardSwitchInResult GroundedResult;
				if (!FBattleFieldSideConditionRules::TryResolveHazardSwitchIn(
						GroundedFacts,
						GroundedResult))
				{
					return false;
				}
				bLevitatePreventedHazard =
					HazardResult.EffectKind == EBattleHazardSwitchInEffectKind::None
					&& GroundedResult.EffectKind != EBattleHazardSwitchInEffectKind::None;
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
			if (bLevitatePreventedHazard
				&& !TryAppendAbilityActivationForPhase(
					State,
					Incoming->BattlerId,
					EBattleTriggerPhase::SwitchIn,
					EBattleAbilityItemActivationOutcome::Applied,
					ResolutionId,
					FActionId(),
					EBattleActionKind::Switch,
					Events,
					&Target))
			{
				return false;
			}
			if (bMagicGuardPreventedDamage
				&& !TryAppendAbilityActivationForPhase(
					State,
					Incoming->BattlerId,
					EBattleTriggerPhase::SwitchIn,
					EBattleAbilityItemActivationOutcome::Applied,
					ResolutionId,
					FActionId(),
					EBattleActionKind::Switch,
					Events,
					&Target))
			{
				return false;
			}

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

			if (!TryResolveImmediateHeldItem(
					State,
					Incoming->BattlerId,
					ResolutionId,
					FActionId(),
					EBattleActionKind::Switch,
					Events))
			{
				return false;
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
					if (!TryCleanupAbilityTriggers(
							State,
							Incoming->AbilityId,
							IncomingBattlerId,
							EBattleTriggerCleanupReason::Faint)
						|| !TryCleanupItemTriggers(
							State,
							Incoming->HeldItem.CurrentItemId,
							IncomingBattlerId,
							EBattleTriggerCleanupReason::Faint)
						|| (FBattleMajorStatusRules::IsCanonical(PendingStatus)
						&& !TryCleanupMajorStatusTriggers(
							State,
							PendingStatus,
							IncomingBattlerId,
							EBattleTriggerCleanupReason::Faint)))
					{
						return false;
					}
					Incoming->bAbilitySuppressed = false;
					Incoming->EnteredActiveOnTurnId = FTurnId();
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
		const TConstArrayView<FBattlePendingReplacementState> AlreadyAnnouncedRequirements,
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
			const bool bAlreadyAnnounced = AlreadyAnnouncedRequirements.ContainsByPredicate(
				[&Requirement](const FBattlePendingReplacementState& Pending)
				{
					return Pending.TrainerId == Requirement.Target.TrainerId
						&& Pending.ActiveSlotId == Requirement.Target.ActiveSlotId;
				});
			if (bAlreadyAnnounced)
			{
				continue;
			}
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
		return State.EncounterKind == EBattleEncounterKind::Wild
			&& State.EncounterPolicies.bRunAllowed
			&& Trainer.Side == EBattleSide::Player
			&& Trainer.Role == EBattleTrainerRole::Player
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
		return State.EncounterKind == EBattleEncounterKind::Wild
			&& Trainer.Side == EBattleSide::Opponent
			&& Trainer.Role == EBattleTrainerRole::Opponent
			&& IsLivingSelectableBattler(&Battler)
			&& FindActiveForBattler(State, Battler.BattlerId) != nullptr
			&& FindWildFleePolicy(State, Battler) != nullptr;
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
		if (!TryCleanupAbilityTriggers(
				State,
				Outgoing->AbilityId,
				Outgoing->BattlerId,
				EBattleTriggerCleanupReason::Switch)
			|| !TryCleanupItemTriggers(
				State,
				Outgoing->HeldItem.CurrentItemId,
				Outgoing->BattlerId,
				EBattleTriggerCleanupReason::Switch)
			|| !TryCleanupAllOwnedVolatileTriggers(
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
		Outgoing->bAbilitySuppressed = false;
		Outgoing->HeldItem.ChoiceLockedMoveId = FMoveId();
		Outgoing->EnteredActiveOnTurnId = FTurnId();
		Active->BattlerId = Incoming->BattlerId;
		Incoming->bAbilitySuppressed = false;
		Incoming->EnteredActiveOnTurnId = State.TurnId;
		return TryRegisterAbilityTriggers(State, Incoming->BattlerId)
			&& TryRegisterItemTriggers(State, Incoming->BattlerId);
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
		Incoming->bAbilitySuppressed = false;
		Incoming->EnteredActiveOnTurnId = State.TurnId;
		OutIncomingTarget.TrainerId = Incoming->TrainerId;
		OutIncomingTarget.BattlerId = Incoming->BattlerId;
		OutIncomingTarget.ActiveSlotId = Active->ActiveSlotId;
		return TryRegisterAbilityTriggers(State, Incoming->BattlerId)
			&& TryRegisterItemTriggers(State, Incoming->BattlerId);
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
		case EBattleActionKind::WildFlee:
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
		TArray<FBattleEvent>& OutPreLockEvents,
		bool& bOutReverseSpeed)
	{
		OutActions.Reset();
		OutPreLockEvents.Reset();
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
				|| !TryCalculateEffectiveSpeedForOrdering(
					State,
					*Battler,
					Active->ActiveSlotId,
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
			else if (Decision.GetActionKind() == EBattleActionKind::Bag)
			{
				if (Decision.GetItemPartyTargetId().IsValid()
					&& !Decision.GetActiveTargetId().IsValid())
				{
					const FBattlePartySlotState* PartySlot = Trainer->PartySlots.FindByPredicate(
						[&Decision](const FBattlePartySlotState& CandidateSlot)
						{
							return CandidateSlot.PartySlotId
								== Decision.GetItemPartyTargetId();
						});
					if (PartySlot == nullptr || !PartySlot->BattlerId.IsValid())
					{
						return false;
					}
					Candidate.SelectedTargetBattlerId = PartySlot->BattlerId;
				}
				else if (Decision.GetActiveTargetId().IsValid()
					&& !Decision.GetItemPartyTargetId().IsValid())
				{
					const FBattleActivePositionState* Target = State.FindActivePosition(
						Decision.GetActiveTargetId());
					if (Target == nullptr || !Target->BattlerId.IsValid())
					{
						return false;
					}
					Candidate.SelectedTargetBattlerId = Target->BattlerId;
				}
				else
				{
					return false;
				}
			}
			LockSpec.Candidates.Add(MoveTemp(Candidate));
		}

		struct FQuickClawDrawRecord
		{
			FActionId ActionId;
			FBattlerId BattlerId;
			FBattleRandomDraw Draw;
			bool bActivated = false;
		};
		TArray<int32> QuickClawCandidateIndices;
		for (int32 CandidateIndex = 0;
			CandidateIndex < LockSpec.Candidates.Num();
			++CandidateIndex)
		{
			const FBattleActionOrderCandidate& Candidate = LockSpec.Candidates[CandidateIndex];
			if (Candidate.Decision.GetActionKind() != EBattleActionKind::Fight)
			{
				continue;
			}
			const FBattleBattlerState* Battler = State.FindBattler(
				Candidate.Decision.GetActingBattlerId());
			if (Battler == nullptr || !IsHeldItemActive(*Battler))
			{
				continue;
			}
			FBattleQuickClawFacts Facts;
			Facts.ItemId = Battler->HeldItem.CurrentItemId;
			Facts.MovePriority = Candidate.OrderKey.MovePriority;
			Facts.bSelectedMoveEligible = true;
			Facts.bSuppressed = Battler->HeldItem.bSuppressed;
			FBattleQuickClawEligibilityResult Eligibility;
			if (!FBattleItemRules::TryEvaluateQuickClawEligibility(Facts, Eligibility))
			{
				if (Battler->HeldItem.CurrentItemId == FBattleItemRules::GetQuickClawId())
				{
					return false;
				}
				continue;
			}
			if (Eligibility.bEligible && Eligibility.bConsumesRandomDraw)
			{
				QuickClawCandidateIndices.Add(CandidateIndex);
			}
		}
		QuickClawCandidateIndices.Sort(
			[&LockSpec](const int32 LeftIndex, const int32 RightIndex)
			{
				const FActiveSlotId Left =
					LockSpec.Candidates[LeftIndex].OrderKey.ActingSlotId;
				const FActiveSlotId Right =
					LockSpec.Candidates[RightIndex].OrderKey.ActingSlotId;
				const uint8 LeftSide = static_cast<uint8>(Left.GetSide());
				const uint8 RightSide = static_cast<uint8>(Right.GetSide());
				return LeftSide != RightSide
					? LeftSide < RightSide
					: static_cast<uint8>(Left.GetPosition())
						< static_cast<uint8>(Right.GetPosition());
			});
		TArray<FQuickClawDrawRecord> QuickClawDraws;
		for (const int32 CandidateIndex : QuickClawCandidateIndices)
		{
			FBattleActionOrderCandidate& Candidate = LockSpec.Candidates[CandidateIndex];
			const FBattleBattlerState* Battler = State.FindBattler(
				Candidate.Decision.GetActingBattlerId());
			check(Battler != nullptr);
			FBattleRandomContext RandomContext;
			RandomContext.BattleId = State.Setup.GetBattleId();
			RandomContext.TurnId = State.TurnId;
			RandomContext.ActionId = Candidate.ActionId;
			RandomContext.ResolutionId = ResolutionId;
			RandomContext.RulePurpose = FBattleItemRules::GetQuickClawActivationPurpose();
			FBattleRandomDraw Draw;
			if (!State.Random->TryDrawUniform(
					0,
					FBattleItemRules::GetQuickClawRollMaxInclusive(),
					RandomContext,
					Draw))
			{
				return false;
			}
			FBattleQuickClawFacts Facts;
			Facts.ItemId = Battler->HeldItem.CurrentItemId;
			Facts.MovePriority = Candidate.OrderKey.MovePriority;
			Facts.bSelectedMoveEligible = true;
			Facts.bSuppressed = Battler->HeldItem.bSuppressed;
			FBattleQuickClawDrawResult DrawResult;
			if (!FBattleItemRules::TryResolveQuickClawDraw(
					Facts,
					Draw.Result,
					DrawResult))
			{
				return false;
			}
			Candidate.OrderKey.FractionalPriorityTenths =
				DrawResult.FractionalPriorityTenths;
			FQuickClawDrawRecord& Record = QuickClawDraws.AddDefaulted_GetRef();
			Record.ActionId = Candidate.ActionId;
			Record.BattlerId = Battler->BattlerId;
			Record.Draw = Draw;
			Record.bActivated = DrawResult.bApplies;
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
		for (const FQuickClawDrawRecord& DrawRecord : QuickClawDraws)
		{
			const FBattleLockedActionState* LockedAction = OutActions.FindByPredicate(
				[&DrawRecord](const FBattleLockedActionState& Candidate)
				{
					return Candidate.ActionId == DrawRecord.ActionId;
				});
			if (LockedAction == nullptr)
			{
				return false;
			}
			OutPreLockEvents.Add(MakeActionDetailEvent(
				State,
				ResolutionId,
				*LockedAction,
				EBattleEventType::RandomCheck,
				EBattleEventCause::Rule,
				static_cast<int64>(DrawRecord.Draw.InclusiveMinimum),
				static_cast<int64>(DrawRecord.Draw.Result),
				static_cast<int64>(DrawRecord.Draw.InclusiveMaximum),
				EBattleVisibilityLevel::CoreOnly));
			if (DrawRecord.bActivated
				&& !TryAppendItemActivationForPhase(
					State,
					DrawRecord.BattlerId,
					EBattleTriggerPhase::ActionOrderCalculation,
					EBattleAbilityItemActivationOutcome::Applied,
					ResolutionId,
					DrawRecord.ActionId,
					EBattleActionKind::Fight,
					OutPreLockEvents))
			{
				return false;
			}
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

	bool TryBuildBagItemUseFacts(
		const FBattleEngineState& State,
		const FBattleTrainerState& ActingTrainer,
		const FBattleBattlerState& ActingBattler,
		const FBattleItemDefinition& ItemDefinition,
		const EBattleBagItemTargetKind TargetKind,
		const FBattleBattlerState& TargetBattler,
		const FBattleActivePositionState* TargetActive,
		FBattleBagItemUseFacts& OutFacts)
	{
		OutFacts = FBattleBagItemUseFacts();
		int32 AttackStage = 0;
		if (!TargetBattler.Stages.TryGetStage(EBattleStat::Attack, AttackStage))
		{
			return false;
		}

		OutFacts.ItemId = ItemDefinition.Id;
		OutFacts.DefinitionKind = ItemDefinition.Kind;
		OutFacts.TargetKind = TargetKind;
		OutFacts.ActingTrainerRole = ActingTrainer.Role;
		OutFacts.EncounterKind = State.EncounterKind;
		OutFacts.bCaptureAllowed = State.EncounterPolicies.bCaptureAllowed;
		OutFacts.bTargetOwnedByActingTrainer =
			TargetBattler.TrainerId == ActingTrainer.TrainerId;
		OutFacts.bTargetIsActingBattler =
			TargetBattler.BattlerId == ActingBattler.BattlerId;
		OutFacts.bTargetIsOpposingActive = TargetActive != nullptr
			&& TargetActive->bAvailable
			&& TargetActive->BattlerId == TargetBattler.BattlerId
			&& TargetActive->ActiveSlotId.GetSide() != ActingTrainer.Side;
		OutFacts.bTargetEgg = TargetBattler.bEgg;
		OutFacts.bTargetCaptured = TargetBattler.bCaptured;
		OutFacts.bTargetRemoved = TargetBattler.bRemoved;
		OutFacts.bTargetFainted = TargetBattler.bFainted;
		OutFacts.bTargetFaintTransitionPending = TargetBattler.bFaintTransitionPending;
		OutFacts.CurrentHP = TargetBattler.CurrentHP;
		OutFacts.MaximumHP = TargetBattler.PermanentStats.MaxHP;
		OutFacts.bHasCanonicalMajorStatus =
			FBattleMajorStatusRules::IsCanonical(TargetBattler.MajorStatusId);
		OutFacts.bHasConfusion = HasVolatile(
			TargetBattler,
			FBattleVolatileRules::GetConfusionId());
		OutFacts.AttackStage = AttackStage;
		return true;
	}

	bool IsExactBagDecisionTargetShape(
		const FBattleDecisionRequest& Request,
		const FBattleDecision& Decision)
	{
		if (Decision.GetActionKind() != EBattleActionKind::Bag)
		{
			return true;
		}
		const bool bPartyPair = Decision.GetItemPartyTargetId().IsValid()
			&& !Decision.GetActiveTargetId().IsValid()
			&& Request.GetLegalItemPartyTargets().ContainsByPredicate(
				[&Decision](const FBattleItemPartyTargetOption& Option)
				{
					return Option.ItemId == Decision.GetItemId()
						&& Option.PartySlotId == Decision.GetItemPartyTargetId();
				});
		const bool bActivePair = Decision.GetActiveTargetId().IsValid()
			&& !Decision.GetItemPartyTargetId().IsValid()
			&& Request.GetLegalItemActiveTargets().ContainsByPredicate(
				[&Decision](const FBattleItemActiveTargetOption& Option)
				{
					return Option.ItemId == Decision.GetItemId()
						&& Option.ActiveSlotId == Decision.GetActiveTargetId();
				});
		return bPartyPair != bActivePair;
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
			if (IsHeldItemActive(*Battler)
				&& Battler->HeldItem.CurrentItemId == FBattleItemRules::GetChoiceBandId())
			{
				FBattleChoiceBandMoveFacts ChoiceFacts;
				ChoiceFacts.ItemId = Battler->HeldItem.CurrentItemId;
				ChoiceFacts.SelectedMoveId = Move.MoveId;
				ChoiceFacts.LockedMoveId = Battler->HeldItem.ChoiceLockedMoveId;
				ChoiceFacts.bSuppressed = Battler->HeldItem.bSuppressed;
				FBattleChoiceBandMoveResult ChoiceResult;
				if (!FBattleItemRules::TryEvaluateChoiceBandMove(
						ChoiceFacts,
						ChoiceResult))
				{
					OutRejection = FBattleRejection();
					OutRejection.Reason = EBattleRejectionReason::InvalidSetup;
					return false;
				}
				if (!ChoiceResult.bMoveAllowed)
				{
					AddUnavailableMove(
						Spec,
						Move.MoveId,
						EBattleOptionUnavailableReason::ChoiceLocked);
					continue;
				}
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

		const auto HasTrainerBagSelection = [Trainer](
			const TConstArrayView<FBattleDecision> Decisions)
		{
			for (const FBattleDecision& Decision : Decisions)
			{
				if (Decision.GetDecisionOwnerTrainerId() == Trainer->TrainerId
					&& Decision.GetActionKind() == EBattleActionKind::Bag)
				{
					return true;
				}
			}
			return false;
		};
		const bool bBagAlreadySelected = HasTrainerBagSelection(State.AcceptedSelections)
			|| HasTrainerBagSelection(AdditionalSelections);
		if (!State.EncounterPolicies.bBagAllowed
			|| !Trainer->ActionAllowance.bBagActionAvailable
			|| bBagAlreadySelected)
		{
			AddUnavailableAction(Spec, EBattleActionKind::Bag, EBattleOptionUnavailableReason::BagRestricted);
		}
		else
		{
			bool bAnyRemainingCanonicalItem = false;
			bool bCaptureCapacityBlocked = false;
			const int64 TotalCaptureCapacity =
				static_cast<int64>(State.CaptureCapacity.PartySlotsRemaining)
				+ static_cast<int64>(State.CaptureCapacity.StorageSlotsRemaining);
			const bool bCaptureCapacityFull =
				static_cast<int64>(State.PendingCaptures.Num()) >= TotalCaptureCapacity;
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
				const EBattleBagItemRuleKind RuleKind =
					FBattleBagItemRules::GetKind(ItemCount.ItemId);
				if (RuleKind == EBattleBagItemRuleKind::None
					|| RuleKind == EBattleBagItemRuleKind::Invalid
					|| Item->Kind
						!= FBattleBagItemRules::GetExpectedDefinitionKind(RuleKind))
				{
					AddUnavailableItem(Spec, ItemCount.ItemId, EBattleOptionUnavailableReason::WrongItemKind);
					continue;
				}
				bAnyRemainingCanonicalItem = true;
				if (RuleKind == EBattleBagItemRuleKind::PokeBall
					&& (State.EncounterKind != EBattleEncounterKind::Wild
						|| !State.EncounterPolicies.bCaptureAllowed
						|| Trainer->Role != EBattleTrainerRole::Player))
				{
					AddUnavailableItem(
						Spec,
						ItemCount.ItemId,
						EBattleOptionUnavailableReason::CaptureRestricted);
					continue;
				}
				if (RuleKind == EBattleBagItemRuleKind::PokeBall
					&& bCaptureCapacityFull)
				{
					bCaptureCapacityBlocked = true;
					AddUnavailableItem(
						Spec,
						ItemCount.ItemId,
						EBattleOptionUnavailableReason::CaptureCapacityFull);
					continue;
				}

				const int32 PartyPairStart = Spec.LegalItemPartyTargets.Num();
				const int32 ActivePairStart = Spec.LegalItemActiveTargets.Num();
				if (FBattleBagItemRules::GetTargetKind(RuleKind)
					== EBattleBagItemTargetKind::Party)
				{
					for (const FBattlePartySlotState& PartySlot : Trainer->PartySlots)
					{
						const FBattleBattlerState* Target = State.FindBattler(PartySlot.BattlerId);
						if (Target == nullptr)
						{
							continue;
						}
						const FBattleActivePositionState* TargetActive =
							FindActiveForBattler(State, Target->BattlerId);
						FBattleBagItemUseFacts Facts;
						FBattleBagItemUseResult Result;
						if (!TryBuildBagItemUseFacts(
								State,
								*Trainer,
								*Battler,
								*Item,
								EBattleBagItemTargetKind::Party,
								*Target,
								TargetActive,
								Facts)
							|| !FBattleBagItemRules::TryEvaluateUse(Facts, Result))
						{
							OutRejection = FBattleRejection();
							OutRejection.Reason = EBattleRejectionReason::InvalidSetup;
							return false;
						}
						if (Result.bLegal)
						{
							AddUnique(Spec.LegalPartyTargets, PartySlot.PartySlotId);
							Spec.LegalItemPartyTargets.Add({ItemCount.ItemId, PartySlot.PartySlotId});
						}
					}
				}
				else
				{
					for (const FBattleActivePositionState& Position : State.ActivePositions)
					{
						const FBattleBattlerState* Target = State.FindBattler(Position.BattlerId);
						if (Target == nullptr
							|| (RuleKind == EBattleBagItemRuleKind::PokeBall
								&& Target->CaptureClassification
									!= EBattleCaptureSpeciesClassification::Normal))
						{
							continue;
						}
						FBattleBagItemUseFacts Facts;
						FBattleBagItemUseResult Result;
						if (!TryBuildBagItemUseFacts(
								State,
								*Trainer,
								*Battler,
								*Item,
								EBattleBagItemTargetKind::Active,
								*Target,
								&Position,
								Facts)
							|| !FBattleBagItemRules::TryEvaluateUse(Facts, Result))
						{
							OutRejection = FBattleRejection();
							OutRejection.Reason = EBattleRejectionReason::InvalidSetup;
							return false;
						}
						if (Result.bLegal)
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
				AddUnavailableAction(
					Spec,
					EBattleActionKind::Bag,
					bCaptureCapacityBlocked
						? EBattleOptionUnavailableReason::CaptureCapacityFull
						: bAnyRemainingCanonicalItem
						? EBattleOptionUnavailableReason::NoLegalTarget
						: EBattleOptionUnavailableReason::NoItemRemaining);
			}
		}

		if (CanOfferRunAction(State, *Trainer, *Battler))
		{
			Spec.LegalActionKinds.Add(EBattleActionKind::Run);
		}
		else
		{
			AddUnavailableAction(Spec, EBattleActionKind::Run, EBattleOptionUnavailableReason::RunRestricted);
		}

		if (CanOfferWildFleeAction(State, *Trainer, *Battler))
		{
			Spec.LegalActionKinds.Add(EBattleActionKind::WildFlee);
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

		TArray<FBattlePendingReplacementState> AlreadyAnnouncedRequirements;
		for (const FBattlePendingReplacementState& Pending : State.PendingReplacements)
		{
			const bool bSatisfiedByBatch = Batch.GetDecisions().ContainsByPredicate(
				[&Pending](const FBattleDecision& Decision)
				{
					return Decision.GetDecisionOwnerTrainerId() == Pending.TrainerId
						&& Decision.GetActiveTargetId() == Pending.ActiveSlotId;
				});
			if (!bSatisfiedByBatch)
			{
				AlreadyAnnouncedRequirements.Add(Pending);
			}
		}

		TArray<FBattleEvent> Events;
		TArray<FBattlerId> AbilityEntrants;
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
			const bool bItemEntryRevealed = TryRevealAirBalloonOnEntry(
				State,
				IncomingTarget.BattlerId,
				ResolutionId,
				EBattleActionKind::Replacement,
				Events);
			if (!bItemEntryRevealed)
			{
				UE_LOG(LogTemp, Fatal, TEXT("C08C replacement held-item entry could not be resolved."));
			}
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
			const bool bImmediateItemsResolved = TryResolveImmediateHeldItem(
				State,
				IncomingTarget.BattlerId,
				ResolutionId,
				FActionId(),
				EBattleActionKind::Replacement,
				Events);
			if (!bImmediateItemsResolved)
			{
				UE_LOG(LogTemp, Fatal, TEXT("C08C replacement immediate held items could not be resolved."));
			}
			AbilityEntrants.Add(IncomingTarget.BattlerId);
		}
		const bool bAbilitiesResolved = TryResolveAbilityEntries(
			State,
			AbilityEntrants,
			ResolutionId,
			EBattleActionKind::Replacement,
			Events);
		if (!bAbilitiesResolved)
		{
			UE_LOG(LogTemp, Fatal, TEXT("C08B replacement entry Abilities could not be resolved."));
		}

		if (!TryRebuildReplacementCheckpointAfterEntryHazards(
				State,
				AfterStateVersion,
				ResolutionId,
				EBattleActionKind::Replacement,
				FallbackSource,
				AlreadyAnnouncedRequirements,
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

		const TArray<FBattlePendingReplacementState> AlreadyAnnouncedRequirements =
			State.PendingReplacements;
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
			const bool bItemEntryRevealed = TryRevealAirBalloonOnEntry(
				State,
				IncomingTarget.BattlerId,
				ResolutionId,
				EBattleActionKind::Switch,
				Events);
			if (!bItemEntryRevealed)
			{
				UE_LOG(LogTemp, Fatal, TEXT("C08C Shift held-item entry could not be resolved."));
			}
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
			const bool bImmediateItemsResolved = TryResolveImmediateHeldItem(
				State,
				IncomingTarget.BattlerId,
				ResolutionId,
				FActionId(),
				EBattleActionKind::Switch,
				Events);
			if (!bImmediateItemsResolved)
			{
				UE_LOG(LogTemp, Fatal, TEXT("C08C Shift immediate held items could not be resolved."));
			}
			const TArray<FBattlerId> AbilityEntrants{IncomingTarget.BattlerId};
			const bool bAbilitiesResolved = TryResolveAbilityEntries(
				State,
				AbilityEntrants,
				ResolutionId,
				EBattleActionKind::Switch,
				Events);
			if (!bAbilitiesResolved)
			{
				UE_LOG(LogTemp, Fatal, TEXT("C08B Shift entry Abilities could not be resolved."));
			}
		}

		if (!TryRebuildReplacementCheckpointAfterEntryHazards(
				State,
				AfterStateVersion,
				ResolutionId,
				EBattleActionKind::Switch,
				Source,
				AlreadyAnnouncedRequirements,
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

	const uint64 BeforeStateVersion = State->StateVersion;
	TArray<FBattlerId> AbilityEntrants;
	TArray<FBattlerId> ItemEntrants;
	for (const FBattleActivePositionState& Active : State->ActivePositions)
	{
		const FBattleBattlerState* Battler = Active.bAvailable
			? State->FindBattler(Active.BattlerId)
			: nullptr;
		if (Battler != nullptr
			&& Battler->CurrentHP > 0
			&& !Battler->bFainted
			&& !Battler->bCaptured
			&& !Battler->bRemoved
			&& FBattleAbilityRules::IsCanonical(Battler->AbilityId))
		{
			if (!TryRegisterAbilityTriggers(*State, Battler->BattlerId))
			{
				OutRejection.Reason = EBattleRejectionReason::InvalidSetup;
				return false;
			}
			AbilityEntrants.Add(Battler->BattlerId);
		}
		if (Battler != nullptr
			&& Battler->CurrentHP > 0
			&& !Battler->bFainted
			&& !Battler->bCaptured
			&& !Battler->bRemoved
			&& IsHeldItemActive(*Battler)
			&& FBattleItemRules::IsCanonical(Battler->HeldItem.CurrentItemId))
		{
			if (!TryRegisterItemTriggers(*State, Battler->BattlerId))
			{
				OutRejection.Reason = EBattleRejectionReason::InvalidSetup;
				return false;
			}
			ItemEntrants.Add(Battler->BattlerId);
		}
	}
	FResolutionId AbilityResolutionId;
	TArray<FBattleEvent> AbilityEvents;
	if (!AbilityEntrants.IsEmpty() || !ItemEntrants.IsEmpty())
	{
		AbilityResolutionId = TakeResolutionId(*State);
		for (const FBattlerId ItemEntrant : ItemEntrants)
		{
			if (!TryRevealAirBalloonOnEntry(
					*State,
					ItemEntrant,
					AbilityResolutionId,
					EBattleActionKind::Switch,
					AbilityEvents)
				|| !TryResolveImmediateHeldItem(
					*State,
					ItemEntrant,
					AbilityResolutionId,
					FActionId(),
					EBattleActionKind::Switch,
					AbilityEvents))
			{
				OutRejection.Reason = EBattleRejectionReason::InvalidSetup;
				return false;
			}
		}
		if (!TryResolveAbilityEntries(
				*State,
				AbilityEntrants,
				AbilityResolutionId,
				EBattleActionKind::Switch,
				AbilityEvents))
		{
			OutRejection.Reason = EBattleRejectionReason::InvalidSetup;
			return false;
		}
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
	if (AbilityResolutionId.IsValid() && !AbilityEvents.IsEmpty())
	{
		FBattleResolutionSpec ResolutionSpec;
		ResolutionSpec.ResolutionId = AbilityResolutionId;
		ResolutionSpec.BeforeStateVersion = BeforeStateVersion;
		ResolutionSpec.AfterStateVersion = State->StateVersion;
		ResolutionSpec.bAccepted = true;
		ResolutionSpec.Events = MoveTemp(AbilityEvents);
		FBattleResolution Resolution;
		const bool bCreated = FBattleResolution::TryCreate(ResolutionSpec, Resolution);
		check(bCreated);
		State->AppendResolution(Resolution);
	}
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
	Snapshot.EscapeAttemptCount = State->EscapeAttemptCount;
	Snapshot.bReinforcementSucceeded = State->bReinforcementSucceeded;
	Snapshot.bCaptureStateVisible = !bFiltered
		|| Observer->Role == EBattleTrainerRole::Player;
	if (Snapshot.bCaptureStateVisible)
	{
		Snapshot.CaptureCapacity = State->CaptureCapacity;
		Snapshot.CaptureProgression = State->Setup.GetCaptureProgression();
		Snapshot.PendingCaptures = State->PendingCaptures;
	}
	if (!bFiltered)
	{
		Snapshot.ConfiguredReinforcementBattlerId =
			State->Setup.GetConfiguredReinforcementBattlerId();
	}
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

		const bool bHeldItemPresent = IsHeldItemActive(Battler);
		const bool bHeldItemDefinitionKnown = bFiltered
			&& (Battler.HeldItem.bRevealed
				|| (Battler.HeldItem.CurrentItemId.IsValid()
					&& IsDefinitionKnown(
						*State,
						*ObserverTrainerId,
						Battler.BattlerId,
						EBattleKnowledgeKind::ItemRevealed,
						Battler.HeldItem.CurrentItemId.GetDefinitionId())));
		Projection.bHeldItemKnown = !bFiltered
			|| bOwned
			|| bHeldItemDefinitionKnown;
		if (Projection.bHeldItemKnown)
		{
			Projection.HeldItemId = bHeldItemPresent
				? Battler.HeldItem.CurrentItemId
				: FItemId();
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
		if (!TrySetAllHeldItemsSuppressed(*State, bMagicRoomTriggerActive))
		{
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
		const bool bItemEntryRevealed = TryRevealAirBalloonOnEntry(
			*State,
			IncomingTarget.BattlerId,
			ResolutionId,
			EBattleActionKind::Switch,
			Events);
		if (!bItemEntryRevealed)
		{
			UE_LOG(LogTemp, Fatal, TEXT("C08C voluntary-switch held-item entry could not be resolved."));
		}
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
		const bool bImmediateItemsResolved = TryResolveImmediateHeldItem(
			*State,
			IncomingTarget.BattlerId,
			ResolutionId,
			Action->ActionId,
			EBattleActionKind::Switch,
			Events);
		if (!bImmediateItemsResolved)
		{
			UE_LOG(LogTemp, Fatal, TEXT("C08C voluntary-switch immediate held items could not be resolved."));
		}
		const TArray<FBattlerId> AbilityEntrants{IncomingTarget.BattlerId};
		const bool bAbilitiesResolved = TryResolveAbilityEntries(
			*State,
			AbilityEntrants,
			ResolutionId,
			EBattleActionKind::Switch,
			Events);
		if (!bAbilitiesResolved)
		{
			UE_LOG(LogTemp, Fatal, TEXT("C08B voluntary-switch entry Abilities could not be resolved."));
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

FBattleResolution FBattleEngine::ExecuteCurrentWildAction()
{
	check(State.IsValid());
	FBattleLockedActionState* Action = State->LockedActions.IsValidIndex(
		State->CurrentLockedActionIndex)
		? &State->LockedActions[State->CurrentLockedActionIndex]
		: nullptr;
	const EBattleActionKind ActionKind = Action != nullptr
		? Action->Decision.GetActionKind()
		: EBattleActionKind::Run;
	const FResolutionId ResolutionId = TakeResolutionId(*State);
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
		|| (ActionKind != EBattleActionKind::Run
			&& ActionKind != EBattleActionKind::WildFlee))
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
			EBattleEventCause::Run,
			ActionKind,
			FallbackSource);
	}

	check(Action != nullptr);
	const uint64 BeforeStateVersion = State->StateVersion;
	auto FinishAcceptedAction = [this, Action, ResolutionId, BeforeStateVersion](
		TArray<FBattleEvent>& Events,
		const TOptional<EBattleOutcomeCause>& TerminalOutcomeCause)
	{
		Action->bFinished = true;
		Events.Add(MakeActionDetailEvent(
			*State,
			ResolutionId,
			*Action,
			EBattleEventType::ActionCompleted,
			EBattleEventCause::Action));
		++State->CurrentLockedActionIndex;
		if (TerminalOutcomeCause.IsSet())
		{
			Events.Add(MakeBattleEndedEvent(
				*State,
				ResolutionId,
				*Action,
				TerminalOutcomeCause.GetValue()));
		}
		else
		{
			AppendPostActionBoundaryEvents(*State, ResolutionId, *Action, Events);
		}
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
	};

	const FBattleBattlerState* ActingBattler = State->FindBattler(
		Action->Decision.GetActingBattlerId());
	const FBattleTrainerState* ActingTrainer = ActingBattler != nullptr
		? State->FindTrainer(ActingBattler->TrainerId)
		: nullptr;
	TArray<FBattleEvent> Events;
	auto CancelStaleAttempt = [&]()
	{
		Events.Add(MakeActionDetailEvent(
			*State,
			ResolutionId,
			*Action,
			EBattleEventType::ActionCanceled,
			EBattleEventCause::Run));
		return FinishAcceptedAction(Events, TOptional<EBattleOutcomeCause>());
	};

	if (ActingBattler == nullptr || ActingTrainer == nullptr)
	{
		return CancelStaleAttempt();
	}

	if (ActionKind == EBattleActionKind::Run)
	{
		const FBattleBattlerState* WildOpponent = FindLeftmostLivingWildOpponent(*State);
		if (!CanOfferRunAction(*State, *ActingTrainer, *ActingBattler)
			|| WildOpponent == nullptr)
		{
			return CancelStaleAttempt();
		}

		FBattleRunCalculationInput Input;
		Input.PlayerPermanentSpeed = ActingBattler->PermanentStats.Speed;
		Input.WildPermanentSpeed = WildOpponent->PermanentStats.Speed;
		Input.EscapeAttemptCount = State->EscapeAttemptCount;
		Input.RandomContext.BattleId = State->Setup.GetBattleId();
		Input.RandomContext.TurnId = State->TurnId;
		Input.RandomContext.ActionId = Action->ActionId;
		Input.RandomContext.ResolutionId = ResolutionId;
		FBattleRunCalculationResult Result;
		if (!FBattleRunRules::TryResolve(Input, *State->Random, Result))
		{
			UE_LOG(LogTemp, Fatal, TEXT("Validated C09B Run calculation or RNG could not be resolved."));
		}

		Events.Add(MakeActionDetailEvent(
			*State,
			ResolutionId,
			*Action,
			EBattleEventType::RunAttempted,
			EBattleEventCause::Run));
		if (!Result.bSucceeded)
		{
			check(State->EscapeAttemptCount < TNumericLimits<uint32>::Max());
			++State->EscapeAttemptCount;
			return FinishAcceptedAction(Events, TOptional<EBattleOutcomeCause>());
		}

		State->Phase = EBattlePhase::Terminal;
		State->Outcome = EBattleOutcome::Escape;
		State->OutcomeCause = EBattleOutcomeCause::Ordinary;
		State->PendingDecision.Reset();
		State->PendingDecisionRequests.Reset();
		State->PendingReplacements.Reset();
		const bool bBattleEndCleaned = TryCleanupBattleEndTriggers(*State);
		check(bBattleEndCleaned);
		Events.Add(MakeActionDetailEvent(
			*State,
			ResolutionId,
			*Action,
			EBattleEventType::Escaped,
			EBattleEventCause::Run));
		return FinishAcceptedAction(
			Events,
			TOptional<EBattleOutcomeCause>(EBattleOutcomeCause::Ordinary));
	}

	const FBattleWildFleePolicyState* Policy = FindWildFleePolicy(
		*State,
		*ActingBattler);
	const FBattleActivePositionState* Active = FindActiveForBattler(
		*State,
		ActingBattler->BattlerId);
	if (!CanOfferWildFleeAction(*State, *ActingTrainer, *ActingBattler)
		|| Policy == nullptr
		|| Active == nullptr)
	{
		return CancelStaleAttempt();
	}

	FBattleWildFleeCalculationInput Input;
	Input.Policy.SpeciesFormId = Policy->SpeciesFormId;
	Input.Policy.TriggerId = Policy->TriggerId;
	Input.Policy.EligibilityId = Policy->EligibilityId;
	Input.Policy.ProbabilityMode = Policy->ProbabilityMode;
	Input.Policy.Numerator = Policy->Numerator;
	Input.Policy.Denominator = Policy->Denominator;
	Input.RandomContext.BattleId = State->Setup.GetBattleId();
	Input.RandomContext.TurnId = State->TurnId;
	Input.RandomContext.ActionId = Action->ActionId;
	Input.RandomContext.ResolutionId = ResolutionId;
	FBattleWildFleeCalculationResult Result;
	if (!FBattleWildFleeRules::TryResolve(Input, *State->Random, Result))
	{
		UE_LOG(LogTemp, Fatal, TEXT("Validated C09B WildFlee policy or RNG could not be resolved."));
	}

	Events.Add(MakeActionDetailEvent(
		*State,
		ResolutionId,
		*Action,
		EBattleEventType::RunAttempted,
		EBattleEventCause::Run));
	if (!Result.bSucceeded)
	{
		return FinishAcceptedAction(Events, TOptional<EBattleOutcomeCause>());
	}

	FBattleEventTarget FleeingTarget;
	FleeingTarget.TrainerId = ActingBattler->TrainerId;
	FleeingTarget.BattlerId = ActingBattler->BattlerId;
	FleeingTarget.ActiveSlotId = Active->ActiveSlotId;
	const bool bTriggersCleaned =
		TryCleanupSourceDependentVolatiles(
			*State,
			ActingBattler->BattlerId,
			EBattleTriggerCleanupReason::Removal)
		&& TryCleanupAbilityTriggers(
			*State,
			ActingBattler->AbilityId,
			ActingBattler->BattlerId,
			EBattleTriggerCleanupReason::Removal)
		&& TryCleanupItemTriggers(
			*State,
			ActingBattler->HeldItem.CurrentItemId,
			ActingBattler->BattlerId,
			EBattleTriggerCleanupReason::Removal)
		&& (!FBattleMajorStatusRules::IsCanonical(ActingBattler->MajorStatusId)
			|| TryCleanupMajorStatusTriggers(
				*State,
				ActingBattler->MajorStatusId,
				ActingBattler->BattlerId,
				EBattleTriggerCleanupReason::Removal))
		&& TryCleanupAllOwnedVolatileTriggers(
			*State,
			*ActingBattler,
			EBattleTriggerCleanupReason::Removal);
	check(bTriggersCleaned);

	FBattleBattlerState* MutableBattler = State->FindMutableBattler(ActingBattler->BattlerId);
	FBattleActivePositionState* MutableActive = State->FindMutableActivePosition(
		Active->ActiveSlotId);
	check(MutableBattler != nullptr
		&& MutableActive != nullptr
		&& MutableActive->BattlerId == ActingBattler->BattlerId);
	MutableBattler->MajorStatusId = FConditionId();
	MutableBattler->Stages = FBattleStatStages();
	MutableBattler->Volatiles.Reset();
	MutableBattler->LastMoveId = FMoveId();
	MutableBattler->bAbilitySuppressed = false;
	MutableBattler->HeldItem.ChoiceLockedMoveId = FMoveId();
	MutableBattler->EnteredActiveOnTurnId = FTurnId();
	MutableBattler->bRemoved = true;
	MutableBattler->bFaintTransitionPending = false;
	MutableActive->TrainerId = FTrainerId();
	MutableActive->BattlerId = FBattlerId();

	Events.Add(MakeTargetedActionEvent(
		*State,
		ResolutionId,
		*Action,
		EBattleEventType::Escaped,
		EBattleEventCause::Run,
		FleeingTarget));
	Events.Add(MakeTargetedActionEvent(
		*State,
		ResolutionId,
		*Action,
		EBattleEventType::LeftActiveSlot,
		EBattleEventCause::Run,
		FleeingTarget));
	Events.Add(MakeTargetedActionEvent(
		*State,
		ResolutionId,
		*Action,
		EBattleEventType::Removed,
		EBattleEventCause::Run,
		FleeingTarget));
	FBattleEvent RemovalCheckpoint = MakeTargetedActionEvent(
		*State,
		ResolutionId,
		*Action,
		EBattleEventType::OpponentRemovalCheckpoint,
		EBattleEventCause::Rule,
		FleeingTarget);
	State->AvailableOpponentRemovalCheckpoints.Add(
		RemovalCheckpoint.GetEventOrdinal());
	Events.Add(MoveTemp(RemovalCheckpoint));

	bool bLivingOpponentRemains = false;
	for (const FBattleBattlerState& Battler : State->Battlers)
	{
		const FBattleTrainerState* Trainer = State->FindTrainer(Battler.TrainerId);
		if (Trainer != nullptr
			&& Trainer->Side == EBattleSide::Opponent
			&& IsLivingSelectableBattler(&Battler))
		{
			bLivingOpponentRemains = true;
			break;
		}
	}
	if (!bLivingOpponentRemains)
	{
		State->Phase = EBattlePhase::Terminal;
		State->Outcome = EBattleOutcome::Escape;
		State->OutcomeCause = EBattleOutcomeCause::OpponentFled;
		State->PendingDecision.Reset();
		State->PendingDecisionRequests.Reset();
		State->PendingReplacements.Reset();
		const bool bBattleEndCleaned = TryCleanupBattleEndTriggers(*State);
		check(bBattleEndCleaned);
		return FinishAcceptedAction(
			Events,
			TOptional<EBattleOutcomeCause>(EBattleOutcomeCause::OpponentFled));
	}

	return FinishAcceptedAction(Events, TOptional<EBattleOutcomeCause>());
}

FBattleResolution FBattleEngine::ExecuteCurrentBagItem()
{
	check(State.IsValid());
	FBattleLockedActionState* Action = State->LockedActions.IsValidIndex(
		State->CurrentLockedActionIndex)
		? &State->LockedActions[State->CurrentLockedActionIndex]
		: nullptr;
	const FBattleItemDefinition* ItemDefinition = Action != nullptr
		? State->Catalog.FindItem(Action->Decision.GetItemId())
		: nullptr;
	const EBattleBagItemRuleKind RuleKind = Action != nullptr
		? FBattleBagItemRules::GetKind(Action->Decision.GetItemId())
		: EBattleBagItemRuleKind::Invalid;

	const FResolutionId ResolutionId = TakeResolutionId(*State);
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
		|| Action->Decision.GetActionKind() != EBattleActionKind::Bag)
	{
		Rejection.Reason = EBattleRejectionReason::IllegalAction;
		if (Action != nullptr)
		{
			Rejection.ActionId = Action->ActionId;
		}
	}

	if (!Rejection.IsRejected()
		&& (ItemDefinition == nullptr
			|| !FBattleBagItemRules::IsCanonical(Action->Decision.GetItemId())
			|| ItemDefinition->Kind
				!= FBattleBagItemRules::GetExpectedDefinitionKind(RuleKind)))
	{
		Rejection.Reason = EBattleRejectionReason::IllegalItem;
		Rejection.ActionId = Action->ActionId;
		Rejection.ItemId = Action->Decision.GetItemId();
	}
	if (Rejection.IsRejected())
	{
		return MakeRejectedResolution(
			*State,
			ResolutionId,
			Rejection,
			EBattleEventType::ActionCanceled,
			EBattleEventCause::Item,
			EBattleActionKind::Bag,
			FallbackSource);
	}

	check(Action != nullptr && ItemDefinition != nullptr);
	const uint64 BeforeStateVersion = State->StateVersion;
	auto FinishAcceptedAction = [this, Action, ResolutionId, BeforeStateVersion](
		TArray<FBattleEvent>& Events,
		const TOptional<EBattleOutcomeCause>& TerminalOutcomeCause)
	{
		Action->bFinished = true;
		Events.Add(MakeActionDetailEvent(
			*State,
			ResolutionId,
			*Action,
			EBattleEventType::ActionCompleted,
			EBattleEventCause::Action));
		++State->CurrentLockedActionIndex;
		if (TerminalOutcomeCause.IsSet())
		{
			Events.Add(MakeBattleEndedEvent(
				*State,
				ResolutionId,
				*Action,
				TerminalOutcomeCause.GetValue()));
		}
		else
		{
			AppendPostActionBoundaryEvents(*State, ResolutionId, *Action, Events);
		}
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
	};

	FBattleTrainerState* ActingTrainer = State->FindMutableTrainer(
		Action->Decision.GetDecisionOwnerTrainerId());
	const FBattleBattlerState* ActingBattler = State->FindBattler(
		Action->Decision.GetActingBattlerId());
	FBattleBattlerState* TargetBattler = nullptr;
	const FBattleActivePositionState* TargetActive = nullptr;
	EBattleBagItemTargetKind TargetKind = EBattleBagItemTargetKind::Invalid;
	bool bTargetShapeValid = false;
	if (ActingTrainer != nullptr
		&& Action->Decision.GetItemPartyTargetId().IsValid()
		&& !Action->Decision.GetActiveTargetId().IsValid())
	{
		TargetKind = EBattleBagItemTargetKind::Party;
		const FBattlePartySlotState* PartySlot = ActingTrainer->PartySlots.FindByPredicate(
			[Action](const FBattlePartySlotState& Candidate)
			{
				return Candidate.PartySlotId
					== Action->Decision.GetItemPartyTargetId();
			});
		if (PartySlot != nullptr
			&& PartySlot->BattlerId == Action->SelectedTargetBattlerId)
		{
			TargetBattler = State->FindMutableBattler(PartySlot->BattlerId);
			TargetActive = TargetBattler != nullptr
				? FindActiveForBattler(*State, TargetBattler->BattlerId)
				: nullptr;
		}
		bTargetShapeValid = true;
	}
	else if (ActingTrainer != nullptr
		&& Action->Decision.GetActiveTargetId().IsValid()
		&& !Action->Decision.GetItemPartyTargetId().IsValid())
	{
		TargetKind = EBattleBagItemTargetKind::Active;
		TargetActive = State->FindActivePosition(Action->Decision.GetActiveTargetId());
		if (TargetActive != nullptr
			&& TargetActive->BattlerId == Action->SelectedTargetBattlerId)
		{
			TargetBattler = State->FindMutableBattler(TargetActive->BattlerId);
		}
		bTargetShapeValid = true;
	}
	if (!bTargetShapeValid || ActingTrainer == nullptr || ActingBattler == nullptr)
	{
		Rejection.Reason = EBattleRejectionReason::InvalidDecision;
		Rejection.ActionId = Action->ActionId;
		Rejection.ItemId = Action->Decision.GetItemId();
		return MakeRejectedResolution(
			*State,
			ResolutionId,
			Rejection,
			EBattleEventType::ActionCanceled,
			EBattleEventCause::Item,
			EBattleActionKind::Bag,
			FallbackSource);
	}

	TArray<FBattleEvent> Events;
	auto CancelStaleUse = [&]()
	{
		Events.Add(MakeActionDetailEvent(
			*State,
			ResolutionId,
			*Action,
			EBattleEventType::ActionCanceled,
			EBattleEventCause::Item));
		return FinishAcceptedAction(Events, TOptional<EBattleOutcomeCause>());
	};
	if (TargetBattler == nullptr || !State->EncounterPolicies.bBagAllowed)
	{
		return CancelStaleUse();
	}

	FBattleBagItemUseFacts UseFacts;
	FBattleBagItemUseResult UseResult;
	if (!TryBuildBagItemUseFacts(
			*State,
			*ActingTrainer,
			*ActingBattler,
			*ItemDefinition,
			TargetKind,
			*TargetBattler,
			TargetActive,
			UseFacts)
		|| !FBattleBagItemRules::TryEvaluateUse(UseFacts, UseResult)
		|| !UseResult.bValid)
	{
		Rejection.Reason = EBattleRejectionReason::InvalidDecision;
		Rejection.ActionId = Action->ActionId;
		Rejection.ItemId = Action->Decision.GetItemId();
		return MakeRejectedResolution(
			*State,
			ResolutionId,
			Rejection,
			EBattleEventType::ActionCanceled,
			EBattleEventCause::Item,
			EBattleActionKind::Bag,
			FallbackSource);
	}
	if (!UseResult.bLegal
		|| UseResult.bCaptureHandoff
			!= (RuleKind == EBattleBagItemRuleKind::PokeBall))
	{
		return CancelStaleUse();
	}

	FBattleCaptureCalculationInput CaptureInput;
	const bool bCaptureUse = UseResult.bCaptureHandoff;
	if (bCaptureUse)
	{
		const int64 TotalCaptureCapacity =
			static_cast<int64>(State->CaptureCapacity.PartySlotsRemaining)
			+ static_cast<int64>(State->CaptureCapacity.StorageSlotsRemaining);
		const FBattleSpeciesFormDefinition* Species = State->Catalog.FindSpeciesForm(
			TargetBattler->SpeciesFormId);
		CaptureInput.BallItemId = Action->Decision.GetItemId();
		CaptureInput.BallMultiplierQ12 = FBattleCaptureCalculator::Q12Neutral;
		CaptureInput.SpeciesClassification = TargetBattler->CaptureClassification;
		CaptureInput.SpeciesCatchRate = Species != nullptr ? Species->CatchRate : 0;
		CaptureInput.CurrentHP = TargetBattler->CurrentHP;
		CaptureInput.MaximumHP = TargetBattler->PermanentStats.MaxHP;
		CaptureInput.TargetLevel = TargetBattler->Level;
		CaptureInput.PlayerLevel = ActingBattler->Level;
		CaptureInput.MajorStatus = FBattleMajorStatusRules::GetKind(
			TargetBattler->MajorStatusId);
		CaptureInput.Progression = State->Setup.GetCaptureProgression();
		CaptureInput.RandomContext.BattleId = State->Setup.GetBattleId();
		CaptureInput.RandomContext.TurnId = State->TurnId;
		CaptureInput.RandomContext.ActionId = Action->ActionId;
		CaptureInput.RandomContext.ResolutionId = ResolutionId;
		CaptureInput.RandomContext.RulePurpose =
			FBattleCaptureCalculator::GetShakeCheckPurpose();
		if (TargetKind != EBattleBagItemTargetKind::Active
			|| TargetActive == nullptr
			|| static_cast<int64>(State->PendingCaptures.Num()) >= TotalCaptureCapacity
			|| !FBattleCaptureCalculator::IsInputValid(CaptureInput))
		{
			return CancelStaleUse();
		}
	}

	TArray<FBattleTrainerBagState> BagStates;
	BagStates.Reserve(State->Trainers.Num());
	for (const FBattleTrainerState& Trainer : State->Trainers)
	{
		FBattleTrainerBagState& BagState = BagStates.AddDefaulted_GetRef();
		BagState.TrainerId = Trainer.TrainerId;
		BagState.Items = Trainer.Bag;
		BagState.bBagActionAvailable = Trainer.ActionAllowance.bBagActionAvailable;
	}
	FBattleBagOwnershipContract BagContract;
	EBattleBagContractError BagError = EBattleBagContractError::InvalidState;
	if (!FBattleBagOwnershipContract::TryCreate(
			BagStates,
			BagContract,
			BagError))
	{
		Rejection.Reason = EBattleRejectionReason::InvalidDecision;
		Rejection.ActionId = Action->ActionId;
		return MakeRejectedResolution(
			*State,
			ResolutionId,
			Rejection,
			EBattleEventType::ActionCanceled,
			EBattleEventCause::Item,
			EBattleActionKind::Bag,
			FallbackSource);
	}

	FBattleBagUseRequest BagRequest;
	BagRequest.ActingTrainerId = ActingTrainer->TrainerId;
	BagRequest.ItemId = Action->Decision.GetItemId();
	BagRequest.TargetOwnerTrainerId = TargetBattler->TrainerId;
	BagRequest.bItemExplicitlyAllowsOtherTrainerTarget = bCaptureUse;
	BagRequest.bItemSpecificTargetLegal = true;
	FBattleBagUseResult BagResult;
	if (!BagContract.TryApplyUse(BagRequest, BagResult, BagError))
	{
		Rejection.Reason = EBattleRejectionReason::InvalidDecision;
		Rejection.ActionId = Action->ActionId;
		return MakeRejectedResolution(
			*State,
			ResolutionId,
			Rejection,
			EBattleEventType::ActionCanceled,
			EBattleEventCause::Item,
			EBattleActionKind::Bag,
			FallbackSource);
	}
	if (BagResult.Outcome == EBattleBagUseOutcome::PreUseRejected)
	{
		return CancelStaleUse();
	}
	if (BagResult.Outcome != EBattleBagUseOutcome::Applied
		|| !BagResult.bItemConsumed
		|| !BagResult.bActionConsumed)
	{
		Rejection.Reason = EBattleRejectionReason::InvalidDecision;
		Rejection.ActionId = Action->ActionId;
		return MakeRejectedResolution(
			*State,
			ResolutionId,
			Rejection,
			EBattleEventType::ActionCanceled,
			EBattleEventCause::Item,
			EBattleActionKind::Bag,
			FallbackSource);
	}

	const TArray<FBattleBagItemCount> BagBeforeUse = ActingTrainer->Bag;
	const bool bBagQuotaBeforeUse = ActingTrainer->ActionAllowance.bBagActionAvailable;
	const FBattleTriggerFramework FrameworkBeforeUse = State->TriggerFramework;
	const uint64 TriggerTokenBeforeUse = State->NextTriggerReentrancyToken;
	const FBattleTrainerBagState* AppliedBag = BagContract.FindTrainerState(
		ActingTrainer->TrainerId);
	if (AppliedBag == nullptr)
	{
		Rejection.Reason = EBattleRejectionReason::InvalidDecision;
		return MakeRejectedResolution(
			*State,
			ResolutionId,
			Rejection,
			EBattleEventType::ActionCanceled,
			EBattleEventCause::Item,
			EBattleActionKind::Bag,
			FallbackSource);
	}
	ActingTrainer->Bag = AppliedBag->Items;
	ActingTrainer->ActionAllowance.bBagActionAvailable =
		AppliedBag->bBagActionAvailable;

	if (bCaptureUse)
	{
		FBattleCaptureCalculationResult CaptureResult;
		if (!FBattleCaptureCalculator::TryResolve(
				CaptureInput,
				*State->Random,
				CaptureResult))
		{
			UE_LOG(
				LogTemp,
				Fatal,
				TEXT("Validated C09B capture calculation or RNG could not be resolved."));
		}

		FBattleEventTarget CaptureTarget;
		CaptureTarget.TrainerId = TargetBattler->TrainerId;
		CaptureTarget.BattlerId = TargetBattler->BattlerId;
		CaptureTarget.ActiveSlotId = TargetActive->ActiveSlotId;
		Events.Add(MakeBagItemMutationEvent(
			*State,
			ResolutionId,
			*Action,
			EBattleEventType::ItemUsed,
			CaptureTarget));
		Events.Add(MakeBagItemMutationEvent(
			*State,
			ResolutionId,
			*Action,
			EBattleEventType::ItemConsumed,
			CaptureTarget));

		FBattleCaptureEventMetadata CaptureMetadata =
			FBattleCaptureCalculator::MakeEventMetadata(CaptureResult);
		Events.Add(MakeCaptureEvent(
			*State,
			ResolutionId,
			*Action,
			EBattleEventType::CaptureAttempted,
			CaptureTarget,
			CaptureMetadata));
		if (!CaptureResult.bSucceeded)
		{
			return FinishAcceptedAction(
				Events,
				TOptional<EBattleOutcomeCause>());
		}

		FBattlePendingCaptureRecord PendingCapture;
		PendingCapture.CaptureOrdinal =
			static_cast<uint64>(State->PendingCaptures.Num()) + 1ULL;
		PendingCapture.Destination = State->PendingCaptures.Num()
			< State->CaptureCapacity.PartySlotsRemaining
			? EBattlePendingCaptureDestination::Party
			: EBattlePendingCaptureDestination::Storage;
		PendingCapture.OriginalTrainerId = TargetBattler->TrainerId;
		PendingCapture.BattlerId = TargetBattler->BattlerId;
		PendingCapture.SourcePokemonId = TargetBattler->SourcePokemonId;
		PendingCapture.SpeciesFormId = TargetBattler->SpeciesFormId;
		PendingCapture.SpeciesClassification = TargetBattler->CaptureClassification;
		PendingCapture.Level = TargetBattler->Level;
		PendingCapture.CurrentHP = TargetBattler->CurrentHP;
		PendingCapture.MaxHP = TargetBattler->PermanentStats.MaxHP;
		PendingCapture.MajorStatusId = TargetBattler->MajorStatusId;
		for (const FBattleMoveSlotState& Move : TargetBattler->Moves)
		{
			FBattleCapturedMoveFact& MoveFact =
				PendingCapture.Moves.AddDefaulted_GetRef();
			MoveFact.SlotIndex = Move.SlotIndex;
			MoveFact.MoveId = Move.MoveId;
			MoveFact.CurrentPP = Move.CurrentPP;
			MoveFact.MaxPP = Move.MaxPP;
		}
		PendingCapture.HeldItem.OriginalItemId =
			TargetBattler->HeldItem.OriginalItemId;
		PendingCapture.HeldItem.CurrentItemId =
			TargetBattler->HeldItem.CurrentItemId;
		PendingCapture.HeldItem.bConsumed = TargetBattler->HeldItem.bConsumed;
		PendingCapture.HeldItem.bSuppressed = TargetBattler->HeldItem.bSuppressed;
		PendingCapture.HeldItem.bRevealed = TargetBattler->HeldItem.bRevealed;
		PendingCapture.HeldItem.bTemporarilyRemoved =
			TargetBattler->HeldItem.bTemporarilyRemoved;
		PendingCapture.HeldItem.ChoiceLockedMoveId =
			TargetBattler->HeldItem.ChoiceLockedMoveId;
		check(PendingCapture.IsValid());

		const bool bCaptureTriggersCleaned =
			TryCleanupSourceDependentVolatiles(
				*State,
				TargetBattler->BattlerId,
				EBattleTriggerCleanupReason::Capture)
			&& TryCleanupAbilityTriggers(
				*State,
				TargetBattler->AbilityId,
				TargetBattler->BattlerId,
				EBattleTriggerCleanupReason::Capture)
			&& TryCleanupItemTriggers(
				*State,
				TargetBattler->HeldItem.CurrentItemId,
				TargetBattler->BattlerId,
				EBattleTriggerCleanupReason::Capture)
			&& (!FBattleMajorStatusRules::IsCanonical(TargetBattler->MajorStatusId)
				|| TryCleanupMajorStatusTriggers(
					*State,
					TargetBattler->MajorStatusId,
					TargetBattler->BattlerId,
					EBattleTriggerCleanupReason::Capture))
			&& TryCleanupAllOwnedVolatileTriggers(
				*State,
				*TargetBattler,
				EBattleTriggerCleanupReason::Capture);
		check(bCaptureTriggersCleaned);

		FBattleActivePositionState* MutableTargetActive =
			State->FindMutableActivePosition(CaptureTarget.ActiveSlotId);
		check(MutableTargetActive != nullptr
			&& MutableTargetActive->BattlerId == TargetBattler->BattlerId);
		TargetBattler->MajorStatusId = FConditionId();
		TargetBattler->Stages = FBattleStatStages();
		TargetBattler->Volatiles.Reset();
		TargetBattler->LastMoveId = FMoveId();
		TargetBattler->bAbilitySuppressed = false;
		TargetBattler->HeldItem.ChoiceLockedMoveId = FMoveId();
		TargetBattler->EnteredActiveOnTurnId = FTurnId();
		TargetBattler->bCaptured = true;
		TargetBattler->bRemoved = true;
		TargetBattler->bFaintTransitionPending = false;
		MutableTargetActive->TrainerId = FTrainerId();
		MutableTargetActive->BattlerId = FBattlerId();
		State->PendingCaptures.Add(PendingCapture);

		CaptureMetadata.bHasPendingDestination = true;
		CaptureMetadata.PendingCaptureOrdinal = PendingCapture.CaptureOrdinal;
		CaptureMetadata.PendingDestination = PendingCapture.Destination;
		Events.Add(MakeCaptureEvent(
			*State,
			ResolutionId,
			*Action,
			EBattleEventType::Captured,
			CaptureTarget,
			CaptureMetadata));
		Events.Add(MakeTargetedActionEvent(
			*State,
			ResolutionId,
			*Action,
			EBattleEventType::LeftActiveSlot,
			EBattleEventCause::Capture,
			CaptureTarget));
		Events.Add(MakeTargetedActionEvent(
			*State,
			ResolutionId,
			*Action,
			EBattleEventType::Removed,
			EBattleEventCause::Capture,
			CaptureTarget));
		FBattleEvent RemovalCheckpoint = MakeTargetedActionEvent(
			*State,
			ResolutionId,
			*Action,
			EBattleEventType::OpponentRemovalCheckpoint,
			EBattleEventCause::Rule,
			CaptureTarget);
		State->AvailableOpponentRemovalCheckpoints.Add(
			RemovalCheckpoint.GetEventOrdinal());
		Events.Add(MoveTemp(RemovalCheckpoint));

		bool bLivingOpponentRemains = false;
		for (const FBattleBattlerState& Battler : State->Battlers)
		{
			const FBattleTrainerState* Trainer = State->FindTrainer(Battler.TrainerId);
			if (Trainer != nullptr
				&& Trainer->Side == EBattleSide::Opponent
				&& IsLivingSelectableBattler(&Battler))
			{
				bLivingOpponentRemains = true;
				break;
			}
		}
		if (!bLivingOpponentRemains)
		{
			State->Phase = EBattlePhase::Terminal;
			State->Outcome = EBattleOutcome::Victory;
			State->OutcomeCause = EBattleOutcomeCause::Capture;
			State->PendingDecision.Reset();
			State->PendingDecisionRequests.Reset();
			State->PendingReplacements.Reset();
			const bool bBattleEndCleaned = TryCleanupBattleEndTriggers(*State);
			check(bBattleEndCleaned);
			return FinishAcceptedAction(
				Events,
				TOptional<EBattleOutcomeCause>(EBattleOutcomeCause::Capture));
		}

		return FinishAcceptedAction(
			Events,
			TOptional<EBattleOutcomeCause>());
	}

	if ((UseResult.bCuresMajorStatus
			&& !TryCleanupMajorStatusTriggers(
				*State,
				TargetBattler->MajorStatusId,
				TargetBattler->BattlerId,
				EBattleTriggerCleanupReason::Removal))
		|| (UseResult.bCuresConfusion
			&& !TryCleanupVolatileTriggers(
				*State,
				FBattleVolatileRules::GetConfusionId(),
				TargetBattler->BattlerId,
				EBattleTriggerCleanupReason::Removal)))
	{
		ActingTrainer->Bag = BagBeforeUse;
		ActingTrainer->ActionAllowance.bBagActionAvailable = bBagQuotaBeforeUse;
		State->TriggerFramework = FrameworkBeforeUse;
		State->NextTriggerReentrancyToken = TriggerTokenBeforeUse;
		Rejection.Reason = EBattleRejectionReason::InvalidDecision;
		Rejection.ActionId = Action->ActionId;
		return MakeRejectedResolution(
			*State,
			ResolutionId,
			Rejection,
			EBattleEventType::ActionCanceled,
			EBattleEventCause::Item,
			EBattleActionKind::Bag,
			FallbackSource);
	}

	FBattleEventTarget EventTarget;
	EventTarget.TrainerId = TargetBattler->TrainerId;
	EventTarget.BattlerId = TargetBattler->BattlerId;
	if (TargetActive != nullptr
		&& TargetActive->BattlerId == TargetBattler->BattlerId)
	{
		EventTarget.ActiveSlotId = TargetActive->ActiveSlotId;
	}
	Events.Add(MakeBagItemMutationEvent(
		*State,
		ResolutionId,
		*Action,
		EBattleEventType::ItemUsed,
		EventTarget));
	Events.Add(MakeBagItemMutationEvent(
		*State,
		ResolutionId,
		*Action,
		EBattleEventType::ItemConsumed,
		EventTarget));

	switch (UseResult.Kind)
	{
	case EBattleBagItemRuleKind::HyperPotion:
	{
		const int32 PreviousHP = TargetBattler->CurrentHP;
		TargetBattler->CurrentHP += UseResult.HealAmount;
		for (const EBattleEventType Type : {
			EBattleEventType::Healing,
			EBattleEventType::HPChanged})
		{
			Events.Add(MakeBagItemMutationEvent(
				*State,
				ResolutionId,
				*Action,
				Type,
				EventTarget,
				static_cast<int64>(PreviousHP),
				static_cast<int64>(TargetBattler->CurrentHP),
				static_cast<int64>(UseResult.HealAmount)));
		}
		break;
	}
	case EBattleBagItemRuleKind::Revive:
	{
		const int32 PreviousHP = TargetBattler->CurrentHP;
		TargetBattler->CurrentHP = UseResult.HealAmount;
		TargetBattler->bFainted = false;
		TargetBattler->bFaintTransitionPending = false;
		TargetBattler->bRemoved = false;
		TargetBattler->MajorStatusId = FConditionId();
		TargetBattler->Stages = FBattleStatStages();
		TargetBattler->Volatiles.Reset();
		TargetBattler->LastMoveId = FMoveId();
		TargetBattler->bAbilitySuppressed = false;
		TargetBattler->HeldItem.ChoiceLockedMoveId = FMoveId();
		TargetBattler->EnteredActiveOnTurnId = FTurnId();
		for (const EBattleEventType Type : {
			EBattleEventType::Healing,
			EBattleEventType::HPChanged})
		{
			Events.Add(MakeBagItemMutationEvent(
				*State,
				ResolutionId,
				*Action,
				Type,
				EventTarget,
				static_cast<int64>(PreviousHP),
				static_cast<int64>(TargetBattler->CurrentHP),
				static_cast<int64>(UseResult.HealAmount)));
		}
		break;
	}
	case EBattleBagItemRuleKind::FullHeal:
	{
		const int32 CuredCount = (UseResult.bCuresMajorStatus ? 1 : 0)
			+ (UseResult.bCuresConfusion ? 1 : 0);
		if (UseResult.bCuresMajorStatus)
		{
			TargetBattler->MajorStatusId = FConditionId();
		}
		if (UseResult.bCuresConfusion)
		{
			TargetBattler->Volatiles.RemoveAll(
				[](const FBattleConditionState& Condition)
				{
					return Condition.ConditionId
						== FBattleVolatileRules::GetConfusionId();
				});
		}
		Events.Add(MakeBagItemMutationEvent(
			*State,
			ResolutionId,
			*Action,
			EBattleEventType::StatusChanged,
			EventTarget,
			static_cast<int64>(CuredCount),
			static_cast<int64>(0),
			static_cast<int64>(-CuredCount)));
		break;
	}
	case EBattleBagItemRuleKind::XAttack:
	{
		const int32 PreviousStage = UseFacts.AttackStage;
		const FBattleStatStageChangeResult StageChange = TargetBattler->Stages.ApplyChange(
			EBattleStat::Attack,
			UseResult.RequestedAttackStageDelta);
		check(StageChange.Outcome == EBattleStatStageChangeOutcome::Applied
			&& StageChange.PreviousStage == PreviousStage
			&& StageChange.AppliedDelta == UseResult.AppliedAttackStageDelta
			&& StageChange.NewStage == UseResult.ResultingAttackStage);
		Events.Add(MakeBagItemMutationEvent(
			*State,
			ResolutionId,
			*Action,
			EBattleEventType::StatStageChanged,
			EventTarget,
			static_cast<int64>(StageChange.PreviousStage),
			static_cast<int64>(StageChange.NewStage),
			static_cast<int64>(StageChange.AppliedDelta)));
		break;
	}
	default:
		checkNoEntry();
		break;
	}

	return FinishAcceptedAction(Events, TOptional<EBattleOutcomeCause>());
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
		FBattleEventTarget SelfTarget;
		SelfTarget.TrainerId = Battler->TrainerId;
		SelfTarget.BattlerId = Battler->BattlerId;
		SelfTarget.ActiveSlotId = Action->OrderKey.ActingSlotId;
		if (FBattleAbilityRules::ShouldMagicGuardPreventDamage(
				Battler->AbilityId,
				EBattleHPChangeSourceKind::Volatile,
				Battler->bAbilitySuppressed))
		{
			const bool bRecorded = TryAppendAbilityActivationForPhase(
				*State,
				Battler->BattlerId,
				EBattleTriggerPhase::BeforeAction,
				EBattleAbilityItemActivationOutcome::Applied,
				ResolutionId,
				Action->ActionId,
				EBattleActionKind::Fight,
				Events,
				&SelfTarget);
			check(bRecorded);
		}
		else
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
		if (!TryResolveImmediateHeldItem(
				*State,
				Battler->BattlerId,
				ResolutionId,
				Action->ActionId,
				EBattleActionKind::Fight,
				Events))
		{
			Rejection.Reason = EBattleRejectionReason::InvalidDecision;
			return MakeRejectedResolution(
				*State,
				ResolutionId,
				Rejection,
				EBattleEventType::ActionCanceled,
				EBattleEventCause::Item,
				EBattleActionKind::Fight,
				FallbackSource);
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
			const bool bAbilityCleaned = TryCleanupAbilityTriggers(
				*State,
				Battler->AbilityId,
				Battler->BattlerId,
				EBattleTriggerCleanupReason::Faint);
			const bool bItemCleaned = TryCleanupItemTriggers(
				*State,
				Battler->HeldItem.CurrentItemId,
				Battler->BattlerId,
				EBattleTriggerCleanupReason::Faint);
			check(bAbilityCleaned && bItemCleaned);
			Battler->bAbilitySuppressed = false;
			Battler->EnteredActiveOnTurnId = FTurnId();
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
	FBattleChoiceBandMoveResult ChoiceCommitResult;
	if (IsHeldItemActive(*Battler)
		&& Battler->HeldItem.CurrentItemId == FBattleItemRules::GetChoiceBandId())
	{
		FBattleChoiceBandMoveFacts ChoiceFacts;
		ChoiceFacts.ItemId = Battler->HeldItem.CurrentItemId;
		ChoiceFacts.SelectedMoveId = Move->Id;
		ChoiceFacts.LockedMoveId = Battler->HeldItem.ChoiceLockedMoveId;
		ChoiceFacts.bSelectedMoveIsStruggle = bStruggle;
		ChoiceFacts.bSuppressed = Battler->HeldItem.bSuppressed;
		if (!FBattleItemRules::TryEvaluateChoiceBandMove(
				ChoiceFacts,
				ChoiceCommitResult))
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
		if (!ChoiceCommitResult.bMoveAllowed)
		{
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

			FBattleResolutionSpec ChoiceResolutionSpec;
			ChoiceResolutionSpec.ResolutionId = ResolutionId;
			ChoiceResolutionSpec.BeforeStateVersion = BeforeStateVersion;
			ChoiceResolutionSpec.AfterStateVersion = State->StateVersion;
			ChoiceResolutionSpec.bAccepted = true;
			ChoiceResolutionSpec.Events = MoveTemp(Events);
			FBattleResolution ChoiceResolution;
			const bool bChoiceResolutionCreated = FBattleResolution::TryCreate(
				ChoiceResolutionSpec,
				ChoiceResolution);
			check(bChoiceResolutionCreated);
			State->AppendResolution(ChoiceResolution);
			return ChoiceResolution;
		}
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
	if (ChoiceCommitResult.bShouldEstablishLock)
	{
		Battler->HeldItem.ChoiceLockedMoveId = ChoiceCommitResult.LockMoveId;
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
		FBattleBattlerState* RemovedBattler = State->FindMutableBattler(
			Removal.Target.BattlerId);
		if (RemovedBattler != nullptr)
		{
			const bool bAbilityCleaned = TryCleanupAbilityTriggers(
				*State,
				RemovedBattler->AbilityId,
				RemovedBattler->BattlerId,
				EBattleTriggerCleanupReason::Faint);
			const bool bItemCleaned = TryCleanupItemTriggers(
				*State,
				RemovedBattler->HeldItem.CurrentItemId,
				RemovedBattler->BattlerId,
				EBattleTriggerCleanupReason::Faint);
			check(bAbilityCleaned && bItemCleaned);
			RemovedBattler->bAbilitySuppressed = false;
			RemovedBattler->EnteredActiveOnTurnId = FTurnId();
		}
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
	TArray<FBattlerId> ForcedAbilityEntrants;
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
			if (SwitchIntent->Kind == EBattleSwitchKind::Forced)
			{
				ForcedAbilityEntrants.Add(SwitchIntent->IncomingTarget.BattlerId);
			}
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
	const bool bForcedAbilitiesResolved = TryResolveAbilityEntries(
		*State,
		ForcedAbilityEntrants,
		ResolutionId,
		EBattleActionKind::Fight,
		Events);
	check(bForcedAbilitiesResolved);

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
			if (Registration.Spec.Rule.Phase != EBattleTriggerPhase::EndTurn)
			{
				continue;
			}
			if (Registration.Spec.SourceDefinition.Kind
				== EBattleTriggerSourceDefinitionKind::Ability)
			{
				if (Registration.Spec.Owner.Kind != EBattleTriggerSubjectKind::Battler)
				{
					continue;
				}
				const FBattleBattlerState* AbilityOwner = State->FindBattler(
					Registration.Spec.Owner.BattlerId);
				const FBattleActivePositionState* AbilityActive = AbilityOwner != nullptr
					? FindActiveForBattler(*State, AbilityOwner->BattlerId)
					: nullptr;
				if (AbilityOwner == nullptr
					|| AbilityActive == nullptr
					|| AbilityOwner->CurrentHP <= 0
					|| AbilityOwner->bFainted
					|| AbilityOwner->bCaptured
					|| AbilityOwner->bRemoved
					|| AbilityOwner->AbilityId
						!= Registration.Spec.SourceDefinition.AbilityId)
				{
					continue;
				}
				FBattleTriggerDispatchParticipant& Participant =
					Dispatch.Participants.AddDefaulted_GetRef();
				Participant.RegistrationId = Registration.RegistrationId;
				Participant.ActiveSlotId = AbilityActive->ActiveSlotId;
				continue;
			}
			if (Registration.Spec.SourceDefinition.Kind
				== EBattleTriggerSourceDefinitionKind::Item)
			{
				if (Registration.Spec.Owner.Kind != EBattleTriggerSubjectKind::Battler)
				{
					continue;
				}
				const FBattleBattlerState* ItemOwner = State->FindBattler(
					Registration.Spec.Owner.BattlerId);
				const FBattleActivePositionState* ItemActive = ItemOwner != nullptr
					? FindActiveForBattler(*State, ItemOwner->BattlerId)
					: nullptr;
				if (ItemOwner == nullptr
					|| ItemActive == nullptr
					|| ItemOwner->CurrentHP <= 0
					|| ItemOwner->bFainted
					|| ItemOwner->bCaptured
					|| ItemOwner->bRemoved
					|| !IsHeldItemActive(*ItemOwner)
					|| ItemOwner->HeldItem.CurrentItemId
						!= Registration.Spec.SourceDefinition.ItemId)
				{
					continue;
				}
				FBattleTriggerDispatchParticipant& Participant =
					Dispatch.Participants.AddDefaulted_GetRef();
				Participant.RegistrationId = Registration.RegistrationId;
				Participant.ActiveSlotId = ItemActive->ActiveSlotId;
				continue;
			}
			if (Registration.Spec.SourceDefinition.Kind
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
			FBattleEventTarget Target;
			Target.TrainerId = TargetBattler.TrainerId;
			Target.BattlerId = TargetBattler.BattlerId;
			Target.ActiveSlotId = TargetActive.ActiveSlotId;
			if (Damage > 0
				&& FBattleAbilityRules::ShouldMagicGuardPreventDamage(
					TargetBattler.AbilityId,
					FBattleVolatileRules::IsCanonical(ConditionId)
						? EBattleHPChangeSourceKind::Volatile
						: EBattleHPChangeSourceKind::Condition,
					TargetBattler.bAbilitySuppressed))
			{
				const bool bRecorded = TryAppendAbilityActivationForPhase(
					*State,
					TargetBattler.BattlerId,
					EBattleTriggerPhase::EndTurn,
					EBattleAbilityItemActivationOutcome::Applied,
					ResolutionId,
					FActionId(),
					EBattleActionKind::Residual,
					Events,
					&Target);
				check(bRecorded);
				return true;
			}
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
			const bool bImmediateItemResolved = TryResolveImmediateHeldItem(
				*State,
				TargetBattler.BattlerId,
				ResolutionId,
				FActionId(),
				EBattleActionKind::Residual,
				Events);
			check(bImmediateItemResolved);

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
				const bool bAbilityCleaned = TryCleanupAbilityTriggers(
					*State,
					TargetBattler.AbilityId,
					Target.BattlerId,
					EBattleTriggerCleanupReason::Faint);
				const bool bItemCleaned = TryCleanupItemTriggers(
					*State,
					TargetBattler.HeldItem.CurrentItemId,
					Target.BattlerId,
					EBattleTriggerCleanupReason::Faint);
				check(bAbilityCleaned && bItemCleaned);
				TargetBattler.bAbilitySuppressed = false;
				TargetBattler.EnteredActiveOnTurnId = FTurnId();
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
				== EBattleTriggerSourceDefinitionKind::Ability)
			{
				if (Request.Owner.Kind != EBattleTriggerSubjectKind::Battler)
				{
					continue;
				}
				FBattleBattlerState* AbilityOwner = State->FindMutableBattler(
					Request.Owner.BattlerId);
				const FBattleActivePositionState* AbilityActive = AbilityOwner != nullptr
					? FindActiveForBattler(*State, AbilityOwner->BattlerId)
					: nullptr;
				if (AbilityOwner == nullptr
					|| AbilityActive == nullptr
					|| AbilityOwner->CurrentHP <= 0
					|| AbilityOwner->bFainted
					|| AbilityOwner->bCaptured
					|| AbilityOwner->bRemoved
					|| AbilityOwner->AbilityId != Request.SourceDefinition.AbilityId
					|| AbilityOwner->AbilityId != FBattleAbilityRules::GetSpeedBoostId())
				{
					continue;
				}
				int32 CurrentSpeedStage = 0;
				if (!AbilityOwner->Stages.TryGetStage(
						EBattleStat::Speed,
						CurrentSpeedStage))
				{
					continue;
				}
				const uint32 ActiveTurns =
					!AbilityOwner->EnteredActiveOnTurnId.IsValid()
						|| AbilityOwner->EnteredActiveOnTurnId < State->TurnId
						? 1u
						: 0u;
				const bool bApplies = FBattleAbilityRules::ShouldApplySpeedBoost(
					AbilityOwner->AbilityId,
					ActiveTurns,
					CurrentSpeedStage,
					AbilityOwner->bAbilitySuppressed);
				const EBattleAbilityItemActivationOutcome Outcome = bApplies
					? EBattleAbilityItemActivationOutcome::Applied
					: (AbilityOwner->bAbilitySuppressed
						? EBattleAbilityItemActivationOutcome::Suppressed
						: EBattleAbilityItemActivationOutcome::Ineligible);
				TOptional<FBattleAbilityItemActivationFact> Fact;
				if (!TryRecordAbilityActivation(*State, Request, Outcome, Fact))
				{
					continue;
				}
				if (!bApplies)
				{
					continue;
				}
				const FBattleStatStageChangeResult Change = AbilityOwner->Stages.ApplyChange(
					EBattleStat::Speed,
					1);
				if (Change.Outcome != EBattleStatStageChangeOutcome::Applied
					|| !Fact.IsSet())
				{
					continue;
				}
				FBattleEventTarget Target;
				Target.TrainerId = AbilityOwner->TrainerId;
				Target.BattlerId = AbilityOwner->BattlerId;
				Target.ActiveSlotId = AbilityActive->ActiveSlotId;
				Events.Add(MakeAbilityActivationEvent(
					*State,
					ResolutionId,
					FActionId(),
					EBattleActionKind::Residual,
					Request,
					Fact.GetValue(),
					&Target));
				FBattleEventSource Source;
				Source.TrainerId = AbilityOwner->TrainerId;
				Source.BattlerId = AbilityOwner->BattlerId;
				Source.ActiveSlotId = AbilityActive->ActiveSlotId;
				Source.DefinitionId = AbilityOwner->AbilityId.GetDefinitionId();
				Events.Add(MakeResidualMutationEvent(
					*State,
					ResolutionId,
					EBattleEventType::StatStageChanged,
					Source,
					Target,
					Change.PreviousStage,
					Change.NewStage,
					Change.AppliedDelta));
				continue;
			}
			if (Request.SourceDefinition.Kind
				== EBattleTriggerSourceDefinitionKind::Item)
			{
				FBattleBattlerState* ItemOwner =
					Request.Owner.Kind == EBattleTriggerSubjectKind::Battler
						? State->FindMutableBattler(Request.Owner.BattlerId)
						: nullptr;
				const FBattleActivePositionState* ItemActive = ItemOwner != nullptr
					? FindActiveForBattler(*State, ItemOwner->BattlerId)
					: nullptr;
				if (ItemOwner == nullptr
					|| ItemActive == nullptr
					|| !IsHeldItemActive(*ItemOwner)
					|| ItemOwner->HeldItem.CurrentItemId
						!= FBattleItemRules::GetLeftoversId()
					|| Request.SourceDefinition.ItemId
						!= ItemOwner->HeldItem.CurrentItemId)
				{
					continue;
				}
				FBattleItemRecoveryFacts RecoveryFacts;
				RecoveryFacts.ItemId = ItemOwner->HeldItem.CurrentItemId;
				RecoveryFacts.CurrentHP = ItemOwner->CurrentHP;
				RecoveryFacts.BaseMaximumHP = ItemOwner->PermanentStats.MaxHP;
				RecoveryFacts.bHealingPermitted = ItemOwner->CurrentHP > 0
					&& !ItemOwner->bFainted
					&& !ItemOwner->bCaptured
					&& !ItemOwner->bRemoved;
				RecoveryFacts.bSuppressed = ItemOwner->HeldItem.bSuppressed;
				FBattleItemRecoveryResult Recovery;
				if (!FBattleItemRules::TryEvaluateRecovery(RecoveryFacts, Recovery)
					|| !Recovery.bValid)
				{
					continue;
				}
				TOptional<FBattleAbilityItemActivationFact> Activation;
				if (!TryRecordItemActivation(*State, Request, Recovery.Outcome, Activation))
				{
					continue;
				}
				if (!Recovery.bApplies || !Activation.IsSet())
				{
					continue;
				}
				const int32 PreviousHP = ItemOwner->CurrentHP;
				ItemOwner->CurrentHP += Recovery.HealAmount;
				FBattleEventTarget Target;
				Target.TrainerId = ItemOwner->TrainerId;
				Target.BattlerId = ItemOwner->BattlerId;
				Target.ActiveSlotId = ItemActive->ActiveSlotId;
				Events.Add(MakeItemActivationEvent(
					*State,
					ResolutionId,
					FActionId(),
					EBattleActionKind::Residual,
					Request,
					Activation.GetValue(),
					&Target));
				for (const EBattleEventType Type : {
					EBattleEventType::Healing,
					EBattleEventType::HPChanged})
				{
					Events.Add(MakeHeldItemMutationEvent(
						*State,
						ResolutionId,
						FActionId(),
						EBattleActionKind::Residual,
						Type,
						ItemOwner->BattlerId,
						ItemActive->ActiveSlotId,
						ItemOwner->HeldItem.CurrentItemId,
						PreviousHP,
						ItemOwner->CurrentHP,
						Recovery.HealAmount));
				}
				continue;
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
					bool bLevitateMadeAirborne = false;
					if (!TryResolveGrounded(
							*State,
							*FieldBattler,
							bGrounded,
							&bLevitateMadeAirborne))
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
					if (bLevitateMadeAirborne)
					{
						FBattleFieldResidualFacts GroundedFacts = Facts;
						GroundedFacts.bGrounded = true;
						FBattleFieldResidualResult GroundedResult;
						if (!FBattleFieldSideConditionRules::TryResolveFieldResidual(
								GroundedFacts,
								GroundedResult))
						{
							continue;
						}
						if (Result.EffectKind == EBattleFieldResidualEffectKind::None
							&& GroundedResult.EffectKind
								== EBattleFieldResidualEffectKind::Heal
							&& GroundedResult.Amount > 0)
						{
							FBattleEventTarget Target;
							Target.TrainerId = FieldBattler->TrainerId;
							Target.BattlerId = FieldBattler->BattlerId;
							Target.ActiveSlotId = FieldActive->ActiveSlotId;
							const bool bRecorded = TryAppendAbilityActivationForPhase(
								*State,
								FieldBattler->BattlerId,
								EBattleTriggerPhase::EndTurn,
								EBattleAbilityItemActivationOutcome::Applied,
								ResolutionId,
								FActionId(),
								EBattleActionKind::Residual,
								Events,
								&Target);
							check(bRecorded);
							continue;
						}
					}
					const bool bMagicGuardPreventedDamage =
						Result.EffectKind == EBattleFieldResidualEffectKind::Damage
						&& Result.Amount > 0
						&& FBattleAbilityRules::ShouldMagicGuardPreventDamage(
							FieldBattler->AbilityId,
							EBattleHPChangeSourceKind::Field,
							FieldBattler->bAbilitySuppressed);
					if (bMagicGuardPreventedDamage)
					{
						Facts.bIndirectDamagePrevented = true;
						if (!FBattleFieldSideConditionRules::TryResolveFieldResidual(
								Facts,
								Result))
						{
							continue;
						}
						FBattleEventTarget Target;
						Target.TrainerId = FieldBattler->TrainerId;
						Target.BattlerId = FieldBattler->BattlerId;
						Target.ActiveSlotId = FieldActive->ActiveSlotId;
						const bool bRecorded = TryAppendAbilityActivationForPhase(
							*State,
							FieldBattler->BattlerId,
							EBattleTriggerPhase::EndTurn,
							EBattleAbilityItemActivationOutcome::Applied,
							ResolutionId,
							FActionId(),
							EBattleActionKind::Residual,
							Events,
							&Target);
						check(bRecorded);
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
			FBattleEventTarget Target;
			Target.TrainerId = Battler->TrainerId;
			Target.BattlerId = Battler->BattlerId;
			Target.ActiveSlotId = Active->ActiveSlotId;
			if (FBattleAbilityRules::ShouldMagicGuardPreventDamage(
					Battler->AbilityId,
					EBattleHPChangeSourceKind::Condition,
					Battler->bAbilitySuppressed))
			{
				const bool bRecorded = TryAppendAbilityActivationForPhase(
					*State,
					Battler->BattlerId,
					EBattleTriggerPhase::EndTurn,
					EBattleAbilityItemActivationOutcome::Applied,
					ResolutionId,
					FActionId(),
					EBattleActionKind::Residual,
					Events,
					&Target);
				check(bRecorded);
				continue;
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
			const bool bImmediateItemResolved = TryResolveImmediateHeldItem(
				*State,
				Battler->BattlerId,
				ResolutionId,
				FActionId(),
				EBattleActionKind::Residual,
				Events);
			check(bImmediateItemResolved);
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
				const bool bAbilityCleaned = TryCleanupAbilityTriggers(
					*State,
					Battler->AbilityId,
					Target.BattlerId,
					EBattleTriggerCleanupReason::Faint);
				const bool bItemCleaned = TryCleanupItemTriggers(
					*State,
					Battler->HeldItem.CurrentItemId,
					Target.BattlerId,
					EBattleTriggerCleanupReason::Faint);
				check(bAbilityCleaned && bItemCleaned);
				Battler->bAbilitySuppressed = false;
				Battler->EnteredActiveOnTurnId = FTurnId();
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
					const bool bItemsUnsuppressed = TrySetAllHeldItemsSuppressed(
						*State,
						false);
					check(bItemsUnsuppressed);
					TArray<FBattlerId> ActiveBattlerIds;
					for (const FBattleActivePositionState& Active : State->ActivePositions)
					{
						if (Active.bAvailable && Active.BattlerId.IsValid())
						{
							ActiveBattlerIds.Add(Active.BattlerId);
						}
					}
					for (const FBattlerId ActiveBattlerId : ActiveBattlerIds)
					{
						const bool bImmediateResolved = TryResolveImmediateHeldItem(
							*State,
							ActiveBattlerId,
							ResolutionId,
							FActionId(),
							EBattleActionKind::Residual,
							Events);
						check(bImmediateResolved);
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
			TArray<FBattleEvent> PreLockEvents;
			bool bReverseSpeed = false;
			const bool bLocked = TryBuildLockedActions(
				*State,
				State->AcceptedSelections,
				ResolutionId,
				NewLockedActions,
				PreLockEvents,
				bReverseSpeed);
			check(bLocked && !NewLockedActions.IsEmpty());
			State->LockedActions = MoveTemp(NewLockedActions);
			State->bLockedOrderReversesSpeed = bReverseSpeed;
			State->NextActionId += static_cast<uint64>(State->LockedActions.Num());
			State->Phase = EBattlePhase::Locked;
			Events.Append(MoveTemp(PreLockEvents));
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
			if (!IsExactBagDecisionTargetShape(Request, Decision))
			{
				Rejection.Reason = EBattleRejectionReason::IllegalTarget;
				Rejection.ItemId = Decision.GetItemId();
				Rejection.PartySlotId = Decision.GetItemPartyTargetId();
				Rejection.ActiveSlotId = Decision.GetActiveTargetId();
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
	const uint64 EventOrdinalBeforeQueueLock = State->NextEventOrdinal;
	TArray<FBattleLockedActionState> NewLockedActions;
	TArray<FBattleEvent> PreLockEvents;
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
				PreLockEvents,
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
		State->NextEventOrdinal = EventOrdinalBeforeQueueLock;
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
		for (const FBattleEvent& PreLockEvent : PreLockEvents)
		{
			Events.Add(ReissueEventWithNextOrdinal(*State, PreLockEvent));
		}
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
	if (!IsExactBagDecisionTargetShape(*Request, Decision))
	{
		Rejection.Reason = EBattleRejectionReason::IllegalTarget;
		Rejection.ItemId = Decision.GetItemId();
		Rejection.PartySlotId = Decision.GetItemPartyTargetId();
		Rejection.ActiveSlotId = Decision.GetActiveTargetId();
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
		const bool bItemEntryRevealed = TryRevealAirBalloonOnEntry(
			*State,
			IncomingTarget.BattlerId,
			ResolutionId,
			EBattleActionKind::Fight,
			Events);
		if (!bItemEntryRevealed)
		{
			UE_LOG(LogTemp, Fatal, TEXT("C08C pivot held-item entry could not be resolved."));
		}
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
		const bool bImmediateItemsResolved = TryResolveImmediateHeldItem(
			*State,
			IncomingTarget.BattlerId,
			ResolutionId,
			Action->ActionId,
			EBattleActionKind::Fight,
			Events);
		if (!bImmediateItemsResolved)
		{
			UE_LOG(LogTemp, Fatal, TEXT("C08C pivot immediate held items could not be resolved."));
		}
		const TArray<FBattlerId> AbilityEntrants{IncomingTarget.BattlerId};
		const bool bAbilitiesResolved = TryResolveAbilityEntries(
			*State,
			AbilityEntrants,
			ResolutionId,
			EBattleActionKind::Fight,
			Events);
		if (!bAbilitiesResolved)
		{
			UE_LOG(LogTemp, Fatal, TEXT("C08B pivot entry Abilities could not be resolved."));
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
	TArray<FBattleEvent> Events;
	Events.Add(MoveTemp(Event));
	const bool bImmediateItemResolved = TryResolveImmediateHeldItem(
		*State,
		Refresh.BattlerId,
		ResolutionId,
		FActionId(),
		EBattleActionKind::Fight,
		Events);
	check(bImmediateItemResolved);

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
