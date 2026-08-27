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
#include "BattleResolutionCommit.h"
#include "Math/NumericLimits.h"

namespace
{
	template <typename TState>
	const FBattleActivePositionState* FindActiveForBattler(
		const TState& State,
		FBattlerId BattlerId);
	template <typename TState>
	bool TryCalculateEffectiveSpeedForOrdering(
		TState& State,
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

	template <typename TState>
	bool TryTakeTriggerOperationContext(
		TState& State,
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

	template <typename TState>
	void DrainTriggerOutputs(TState& State)
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

	template <typename TState>
	bool TryDispatchBattlerStatusPhase(
		TState& State,
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

	template <typename TState>
	bool TryCleanupMajorStatusTriggers(
		TState& State,
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

	template <typename TState>
	bool TryCleanupAbilityTriggers(
		TState& State,
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

	template <typename TState>
	bool TryRegisterAbilityTriggers(
		TState& State,
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

	template <typename TState>
	bool TryCleanupItemTriggers(
		TState& State,
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

	template <typename TState>
	bool TryRegisterItemTriggers(
		TState& State,
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

	template <typename TState>
	bool TryApplyHeldItemLedgerOperation(
		TState& State,
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

	template <typename TState>
	bool TryRevealHeldItem(TState& State, const FBattlerId BattlerId)
	{
		const FBattleBattlerState* Battler = State.FindBattler(BattlerId);
		return Battler != nullptr
			&& (Battler->HeldItem.bRevealed
				|| TryApplyHeldItemLedgerOperation(
					State,
					BattlerId,
					EBattleHeldItemOperationKind::Reveal));
	}

	template <typename TState>
	bool TryConsumeHeldItem(TState& State, const FBattlerId BattlerId)
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

	template <typename TState>
	bool TryRemoveHeldItem(TState& State, const FBattlerId BattlerId)
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

	template <typename TState>
	bool TrySetHeldItemSuppressed(
		TState& State,
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

	template <typename TState>
	bool TrySetAllHeldItemsSuppressed(
		TState& State,
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

	template <typename TState>
	bool TryDispatchAbilityPhase(
		TState& State,
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

	template <typename TState>
	bool TryDispatchItemPhase(
		TState& State,
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

	template <typename TState>
	bool TryRecordAbilityActivation(
		TState& State,
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

	template <typename TState>
	bool TryRecordItemActivation(
		TState& State,
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

	template <typename TState>
	bool HasFieldRoom(
		const TState& State,
		const FConditionId& RoomId)
	{
		return State.Field.Rooms.ContainsByPredicate(
			[&RoomId](const FBattleConditionState& Condition)
			{
				return Condition.ConditionId == RoomId;
			});
	}

	template <typename TState>
	const FBattleSideState* FindSide(
		const TState& State,
		const EBattleSide Side)
	{
		return State.Sides.FindByPredicate(
			[Side](const FBattleSideState& Candidate)
			{
				return Candidate.Side == Side;
			});
	}

	template <typename TState>
	FBattleSideState* FindMutableSide(
		TState& State,
		const EBattleSide Side)
	{
		return State.Sides.FindByPredicate(
			[Side](const FBattleSideState& Candidate)
			{
				return Candidate.Side == Side;
			});
	}

	template <typename TState>
	bool HasSideCondition(
		const TState& State,
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

	template <typename TState>
	const FBattleConditionState* FindFieldSideCondition(
		const TState& State,
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

	template <typename TState>
	bool TryDispatchFieldSidePhase(
		TState& State,
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

	template <typename TState>
	bool TryIsFieldSideConditionActiveForPhase(
		TState& State,
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

	template <typename TState>
	bool TryCalculateEffectiveSpeedForOrdering(
		TState& State,
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

	template <typename TState>
	bool TryResolveGrounded(
		const TState& State,
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

	template <typename TState>
	bool TryCleanupFieldSideTriggers(
		TState& State,
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

	template <typename TState>
	bool TryCleanupVolatileTriggers(
		TState& State,
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

	template <typename TState>
	bool TryCleanupAllOwnedVolatileTriggers(
		TState& State,
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

	template <typename TState>
	bool TryCleanupSourceDependentVolatiles(
		TState& State,
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

	template <typename TState>
	bool TryGetVolatilePayloadMoveId(
		const TState& State,
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

	template <typename TState>
	bool TryClearChargeState(
		TState& State,
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

	template <typename TState>
	bool TryDispatchBattlerVolatilePhase(
		TState& State,
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

	template <typename TState>
	bool TryCleanupBattleEndTriggers(TState& State)
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

	template <typename TState>
	bool TrySetToxicLayers(
		TState& State,
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

	template <typename TState>
	bool TryRunToxicSwitchOut(
		TState& State,
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

	template <typename TState>
	FBattleEventSource FindFallbackSource(const TState& State)
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

	template <typename TState>
	FBattleEventSource SourceFromLockedAction(
		const TState& State,
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

	template <typename TState>
	FBattleEvent MakeEvent(
		TState& State,
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

	template <typename TState>
	FBattleEvent MakeActionDetailEvent(
		TState& State,
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

	template <typename TState>
	FBattleEvent MakeBattleEffectEvent(
		TState& State,
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

	template <typename TState>
	FBattleEvent MakeTargetedActionEvent(
		TState& State,
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

	template <typename TState>
	FBattleEvent MakeTargetedActionlessEvent(
		TState& State,
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

	template <typename TState>
	FBattleEvent MakeRuleMutationEvent(
		TState& State,
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

	template <typename TState>
	FBattleEvent MakeAbilityActivationEvent(
		TState& State,
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

	template <typename TState>
	FBattleEvent MakeItemActivationEvent(
		TState& State,
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

	template <typename TState>
	bool TryAppendAbilityActivationForPhase(
		TState& State,
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

	template <typename TState>
	bool TryAppendItemActivationForPhase(
		TState& State,
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

	template <typename TState>
	FBattleEvent MakeHeldItemMutationEvent(
		TState& State,
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

	template <typename TState>
	bool TryFindItemRequestForPhase(
		TState& State,
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

	template <typename TState>
	bool TryResolveImmediateHeldItem(
		TState& State,
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

	template <typename TState>
	bool TryRevealAirBalloonOnEntry(
		TState& State,
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

	template <typename TState>
	bool TryResolveAbilityEntries(
		TState& State,
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

	template <typename TState>
	void AppendSwitchTransitionEvents(
		TState& State,
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

	template <typename TState>
	FBattleEvent MakeBattleEndedEvent(
		TState& State,
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

	template <typename TState>
	void AppendPartnerTeamVictoryRecoveryEvent(
		TState& State,
		const FResolutionId ResolutionId,
		const FActionId ActionId,
		const EBattleActionKind ActionKind,
		const FBattleEventSource& Source,
		const FBattleFaintOutcomeResolution& FaintResolution,
		TArray<FBattleEvent>& Events)
	{
		if (!FaintResolution.PartnerTeamVictoryRecovery.IsSet())
		{
			return;
		}

		const FBattlePartnerTeamVictoryRecovery& Recovery =
			FaintResolution.PartnerTeamVictoryRecovery.GetValue();
		FBattleEventSpec Spec;
		Spec.EventOrdinal = State.NextEventOrdinal;
		Spec.BattleId = State.Setup.GetBattleId();
		Spec.TurnId = State.TurnId;
		Spec.ActionId = ActionId;
		Spec.ResolutionId = ResolutionId;
		Spec.Type = EBattleEventType::PartnerTeamVictoryRecovery;
		Spec.Cause = EBattleEventCause::Outcome;
		Spec.CauseActionKind = ActionKind;
		Spec.OutcomeCause = EBattleOutcomeCause::PartnerTeamVictory;
		Spec.Source = Source;
		Spec.Targets.Add(Recovery.Target);
		Spec.NumericBefore = Recovery.PreviousHP;
		Spec.NumericAfter = Recovery.NewHP;
		Spec.NumericDelta = Recovery.NewHP - Recovery.PreviousHP;
		Spec.Visibility.Level = EBattleVisibilityLevel::Public;

		FBattleEvent Event;
		const bool bCreated = FBattleEvent::TryCreate(Spec, Event);
		check(bCreated && Recovery.bMajorStatusCured);
		++State.NextEventOrdinal;
		Events.Add(MoveTemp(Event));
	}

	template <typename TState>
	bool TryResolveEntryHazards(
		TState& State,
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
				FBattleFaintOutcomePlan FaintPlan;
				if (!FBattleFaintOutcomeResolver::TryResolveAction(
						FaintInput,
						EBattleTargetClass::SelectedOpponent,
						ResolutionId,
						State.Battlers,
						State.ActivePositions,
						State.CompiledEncounterPolicies,
						FaintPlan)
					|| !FBattleFaintOutcomeResolver::TryApplyActionPlan(
						State.Battlers,
						State.ActivePositions,
						State.Phase,
						State.Outcome,
						State.OutcomeCause,
						State.PendingDecision,
						State.PendingDecisionRequests,
						FaintPlan))
				{
					return false;
				}
				const FBattleFaintOutcomeResolution& FaintResolution =
					FaintPlan.Resolution;
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
					AppendPartnerTeamVictoryRecoveryEvent(
						State,
						ResolutionId,
						FActionId(),
						EBattleActionKind::Switch,
						Source,
						FaintResolution,
						Events);
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

	template <typename TState>
	bool TryBuildReplacementCheckpointRequests(
		const TState& State,
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

	template <typename TState>
	const FBattleActivePositionState* FindActiveForBattler(
		const TState& State,
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

	template <typename TState>
	const FBattleTrainerEncounterPolicy* FindTrainerEncounterPolicy(
		const TState& State,
		const FTrainerId TrainerId)
	{
		return State.CompiledEncounterPolicies.FindTrainerPolicy(TrainerId);
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

	FDefinitionId GetWildOpponentSwitchRestrictionRuleId()
	{
		FDefinitionId RuleId;
		const bool bCreated = FDefinitionId::TryCreate(
			FName(TEXT("Battle.Switch.NoOrdinaryWildOpponent")),
			RuleId);
		check(bCreated);
		return RuleId;
	}

	template <typename TState>
	bool TryBuildSwitchLegality(
		const TState& State,
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
		const FBattleTrainerEncounterPolicy* TrainerPolicy =
			FindTrainerEncounterPolicy(State, Trainer->TrainerId);
		if (TrainerPolicy == nullptr)
		{
			return false;
		}
		Spec.Blockers.bEncounterPolicyAllows = Kind != EBattleSwitchKind::Voluntary
			|| TrainerPolicy->bMayVoluntarilySwitch;
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

	template <typename TState>
	bool TryApplySwitchSelection(
		TState& State,
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

	template <typename TState>
	bool TryBuildReplacementDecisionRequest(
		const TState& State,
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

	template <typename TState>
	bool TryBuildMandatoryReplacementRequests(
		const TState& State,
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

	template <typename TState>
	bool TryBuildShiftDecisionRequest(
		const TState& State,
		const uint64 StateVersion,
		FBattleDecisionRequest& OutRequest)
	{
		OutRequest = FBattleDecisionRequest();
		if (State.CompiledEncounterPolicies.GetBattleStyle() != EBattleStylePolicy::Shift
			|| State.CompiledEncounterPolicies.GetFormat() != EBattleFormat::Single
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

		const FBattleTrainerEncounterPolicy* PlayerPolicy =
			State.CompiledEncounterPolicies.GetTrainerPolicies().FindByPredicate(
			[](const FBattleTrainerEncounterPolicy& Policy)
			{
				return Policy.Side == EBattleSide::Player
					&& Policy.Role == EBattleTrainerRole::Player;
			});
		const FBattleTrainerState* PlayerTrainer = PlayerPolicy != nullptr
			? State.FindTrainer(PlayerPolicy->TrainerId)
			: nullptr;
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

	template <typename TState>
	bool TryBuildReplacementCheckpointRequests(
		const TState& State,
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

	template <typename TState>
	bool TryResolveVolatileMoveGate(
		const TState& State,
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
		const FBattleTrainerEncounterPolicy* TrainerPolicy =
			FindTrainerEncounterPolicy(State, ActingTrainer.TrainerId);
		if (TrainerPolicy == nullptr)
		{
			return false;
		}
		OutFacts.bActingTrainerMayUseBag = TrainerPolicy->bMayUseBag;
		OutFacts.bActingTrainerMayCapture = TrainerPolicy->bMayCapture;
		OutFacts.bActingTrainerMayUseRevive = TrainerPolicy->bMayUseRevive;
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
		const FBattleTrainerEncounterPolicy* TrainerPolicy = Trainer != nullptr
			? FindTrainerEncounterPolicy(State, Trainer->TrainerId)
			: nullptr;
		if (!State.bHasCatalog
			|| ActingPosition == nullptr
			|| Battler == nullptr
			|| Trainer == nullptr
			|| TrainerPolicy == nullptr
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
		if (!TrainerPolicy->bMayUseBag
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
					&& !TrainerPolicy->bMayCapture)
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

	int32 GetDecisionSequenceBand(const FBattleTrainerEncounterPolicy& Trainer)
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
		for (const FBattleTrainerEncounterPolicy& TrainerPolicy :
			State.CompiledEncounterPolicies.GetTrainerPolicies())
		{
			const FBattleTrainerState* Trainer = State.FindTrainer(TrainerPolicy.TrainerId);
			check(Trainer != nullptr);
			FBattleDecisionOwnerState Owner;
			Owner.TrainerId = TrainerPolicy.TrainerId;
			Owner.Controller = TrainerPolicy.Controller;
			for (const FBattleActivePositionState& Position : State.ActivePositions)
			{
				if (Position.TrainerId == TrainerPolicy.TrainerId
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
				const FBattleTrainerEncounterPolicy* LeftTrainer =
					FindTrainerEncounterPolicy(State, Left.TrainerId);
				const FBattleTrainerEncounterPolicy* RightTrainer =
					FindTrainerEncounterPolicy(State, Right.TrainerId);
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

	/** Rejects any unexpected draw while deterministic WildFlee modes are prepared. */
	class FNoDrawBattleRandom final : public IBattleRandom
	{
	public:
		virtual bool TryDrawUniform(
			uint32 InclusiveMinimum,
			uint32 InclusiveMaximum,
			const FBattleRandomContext& Context,
			FBattleRandomDraw& OutDraw) override
		{
			(void)InclusiveMinimum;
			(void)InclusiveMaximum;
			(void)Context;
			OutDraw = FBattleRandomDraw();
			return false;
		}

		virtual TConstArrayView<FBattleRandomDraw> GetTrace() const override
		{
			return Trace;
		}

		virtual bool TryCreateTransaction(
			FResolutionId ResolutionId,
			FActionId OwningActionId,
			TUniquePtr<IBattleRandomTransaction>& OutTransaction) override
		{
			(void)ResolutionId;
			(void)OwningActionId;
			OutTransaction.Reset();
			return false;
		}

	private:
		TArray<FBattleRandomDraw> Trace;
	};

	struct FWildActionStagedVolatiles
	{
		FBattlerId BattlerId;
		TArray<FBattleConditionState> Volatiles;
	};

	/** Bounded trigger/volatile projection used only by one Wild-action invocation. */
	struct FWildActionCleanupStage
	{
		FBattleTriggerFramework TriggerFramework;
		uint64 NextTriggerReentrancyToken = 0;
		TArray<FWildActionStagedVolatiles> BattlerVolatiles;

		void Capture(const FBattleEngineState& State)
		{
			TriggerFramework = State.TriggerFramework;
			NextTriggerReentrancyToken = State.NextTriggerReentrancyToken;
			BattlerVolatiles.Reset();
			BattlerVolatiles.Reserve(State.Battlers.Num());
			for (const FBattleBattlerState& Battler : State.Battlers)
			{
				FWildActionStagedVolatiles& Staged = BattlerVolatiles.AddDefaulted_GetRef();
				Staged.BattlerId = Battler.BattlerId;
				Staged.Volatiles = Battler.Volatiles;
			}
		}

		FWildActionStagedVolatiles* FindMutableVolatiles(const FBattlerId BattlerId)
		{
			return BattlerVolatiles.FindByPredicate(
				[BattlerId](const FWildActionStagedVolatiles& Candidate)
				{
					return Candidate.BattlerId == BattlerId;
				});
		}
	};

	bool TryTakeStagedTriggerOperationContext(
		FWildActionCleanupStage& Stage,
		FBattleTriggerOperationContext& OutContext)
	{
		OutContext = FBattleTriggerOperationContext();
		if (Stage.NextTriggerReentrancyToken == 0
			|| Stage.NextTriggerReentrancyToken == TNumericLimits<uint64>::Max()
			|| !FBattleTriggerReentrancyToken::TryCreate(
				Stage.NextTriggerReentrancyToken,
				OutContext.ReentrancyToken))
		{
			return false;
		}
		++Stage.NextTriggerReentrancyToken;
		return true;
	}

	void DrainStagedTriggerOutputs(FWildActionCleanupStage& Stage)
	{
		TArray<FBattleTriggerEffectRequest> IgnoredRequests;
		TArray<FBattleTriggerLifecycleFact> IgnoredFacts;
		Stage.TriggerFramework.DrainEffectRequests(IgnoredRequests);
		Stage.TriggerFramework.DrainLifecycleFacts(IgnoredFacts);
	}

	bool TryStageMajorStatusCleanup(
		FWildActionCleanupStage& Stage,
		const FConditionId& StatusId,
		const FBattlerId BattlerId,
		const EBattleTriggerCleanupReason Reason = EBattleTriggerCleanupReason::Removal)
	{
		if (!FBattleMajorStatusRules::IsCanonical(StatusId))
		{
			return true;
		}
		FBattleTriggerSubject Owner;
		FBattleTriggerOperationContext Operation;
		EBattleTriggerError Error = EBattleTriggerError::None;
		if (!TryMakeBattlerTriggerSubject(BattlerId, Owner)
			|| !TryTakeStagedTriggerOperationContext(Stage, Operation)
			|| !FBattleMajorStatusRules::TryCleanupTriggers(
				Stage.TriggerFramework,
				StatusId,
				Owner,
				Reason,
				Operation,
				Error))
		{
			return false;
		}
		DrainStagedTriggerOutputs(Stage);
		return true;
	}

	bool TryStageAbilityCleanup(
		FWildActionCleanupStage& Stage,
		const FAbilityId& AbilityId,
		const FBattlerId BattlerId,
		const EBattleTriggerCleanupReason Reason = EBattleTriggerCleanupReason::Removal)
	{
		if (!FBattleAbilityRules::IsCanonical(AbilityId))
		{
			return true;
		}
		FBattleTriggerSubject Owner;
		FBattleTriggerSourceDefinition SourceDefinition;
		FBattleTriggerOperationContext Operation;
		if (!TryMakeBattlerTriggerSubject(BattlerId, Owner)
			|| !FBattleTriggerSourceDefinition::TryCreateAbility(AbilityId, SourceDefinition)
			|| !TryTakeStagedTriggerOperationContext(Stage, Operation))
		{
			return false;
		}

		FBattleTriggerCleanupRequest Cleanup;
		Cleanup.Reason = Reason;
		Cleanup.AffectedOwners.Add(Owner);
		Cleanup.SourceDefinitionFilter = SourceDefinition;
		Cleanup.Context = Operation;
		EBattleTriggerError Error = EBattleTriggerError::None;
		if (!Stage.TriggerFramework.TryApplyCleanup(Cleanup, Error))
		{
			return false;
		}
		DrainStagedTriggerOutputs(Stage);
		return true;
	}

	bool TryStageItemCleanup(
		FWildActionCleanupStage& Stage,
		const FItemId& ItemId,
		const FBattlerId BattlerId,
		const EBattleTriggerCleanupReason Reason = EBattleTriggerCleanupReason::Removal)
	{
		if (!FBattleItemRules::IsCanonical(ItemId))
		{
			return true;
		}
		FBattleTriggerSubject Owner;
		FBattleTriggerSourceDefinition SourceDefinition;
		FBattleTriggerOperationContext Operation;
		if (!TryMakeBattlerTriggerSubject(BattlerId, Owner)
			|| !FBattleTriggerSourceDefinition::TryCreateItem(ItemId, SourceDefinition)
			|| !TryTakeStagedTriggerOperationContext(Stage, Operation))
		{
			return false;
		}

		FBattleTriggerCleanupRequest Cleanup;
		Cleanup.Reason = Reason;
		Cleanup.AffectedOwners.Add(Owner);
		Cleanup.SourceDefinitionFilter = SourceDefinition;
		Cleanup.Context = Operation;
		EBattleTriggerError Error = EBattleTriggerError::None;
		if (!Stage.TriggerFramework.TryApplyCleanup(Cleanup, Error))
		{
			return false;
		}
		DrainStagedTriggerOutputs(Stage);
		return true;
	}

	bool TryStageVolatileCleanup(
		FWildActionCleanupStage& Stage,
		const FConditionId& VolatileId,
		const FBattlerId BattlerId,
		const EBattleTriggerCleanupReason Reason = EBattleTriggerCleanupReason::Removal)
	{
		if (!FBattleVolatileRules::IsCanonical(VolatileId))
		{
			return true;
		}
		FBattleTriggerSubject Owner;
		FBattleTriggerOperationContext Operation;
		EBattleTriggerError Error = EBattleTriggerError::None;
		if (!TryMakeBattlerTriggerSubject(BattlerId, Owner)
			|| !TryTakeStagedTriggerOperationContext(Stage, Operation)
			|| !FBattleVolatileRules::TryCleanupTriggers(
				Stage.TriggerFramework,
				VolatileId,
				Owner,
				Reason,
				Operation,
				Error))
		{
			return false;
		}
		DrainStagedTriggerOutputs(Stage);
		return true;
	}

	bool TryStageSourceDependentVolatileCleanup(
		FWildActionCleanupStage& Stage,
		const FBattlerId SourceBattlerId,
		const EBattleTriggerCleanupReason Reason = EBattleTriggerCleanupReason::Removal)
	{
		for (FWildActionStagedVolatiles& Candidate : Stage.BattlerVolatiles)
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
				if (!TryStageVolatileCleanup(Stage, Id, Candidate.BattlerId, Reason))
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

	bool TryStageAllOwnedVolatileCleanup(
		FWildActionCleanupStage& Stage,
		const FBattlerId BattlerId,
		const EBattleTriggerCleanupReason Reason = EBattleTriggerCleanupReason::Removal)
	{
		FWildActionStagedVolatiles* Battler = Stage.FindMutableVolatiles(BattlerId);
		if (Battler == nullptr)
		{
			return false;
		}
		TArray<FConditionId> Ids;
		for (const FBattleConditionState& Condition : Battler->Volatiles)
		{
			if (FBattleVolatileRules::IsCanonical(Condition.ConditionId))
			{
				Ids.Add(Condition.ConditionId);
			}
		}
		for (const FConditionId& Id : Ids)
		{
			if (!TryStageVolatileCleanup(Stage, Id, BattlerId, Reason))
			{
				return false;
			}
		}
		return true;
	}

	bool TryStageBattleEndCleanup(FWildActionCleanupStage& Stage)
	{
		FBattleTriggerOperationContext Operation;
		if (!TryTakeStagedTriggerOperationContext(Stage, Operation))
		{
			return false;
		}
		FBattleTriggerCleanupRequest Cleanup;
		Cleanup.Reason = EBattleTriggerCleanupReason::BattleEnd;
		Cleanup.Context = Operation;
		EBattleTriggerError Error = EBattleTriggerError::None;
		if (!Stage.TriggerFramework.TryApplyCleanup(Cleanup, Error))
		{
			return false;
		}
		DrainStagedTriggerOutputs(Stage);
		return true;
	}

	struct FWildActionStateDelta
	{
		int32 NextLockedActionIndex = INDEX_NONE;
		uint32 EscapeAttemptCount = 0;
		EBattlePhase Phase = EBattlePhase::Resolving;
		EBattleOutcome Outcome = EBattleOutcome::InProgress;
		EBattleOutcomeCause OutcomeCause = EBattleOutcomeCause::None;
		TOptional<FBattleDecisionRequest> PendingDecision;
		TArray<FBattleDecisionRequest> PendingDecisionRequests;
		TArray<FBattlePendingReplacementState> PendingReplacements;
		bool bApplyCleanupStage = false;
		FWildActionCleanupStage CleanupStage;
		bool bRemoveBattler = false;
		FBattlerId RemovedBattlerId;
		FActiveSlotId ClearedActiveSlotId;
		TOptional<uint64> OpponentRemovalCheckpointOrdinal;
	};

	void InitializeWildActionDelta(
		const FBattleEngineState& State,
		FWildActionStateDelta& OutDelta)
	{
		OutDelta = FWildActionStateDelta();
		OutDelta.NextLockedActionIndex = State.CurrentLockedActionIndex + 1;
		OutDelta.EscapeAttemptCount = State.EscapeAttemptCount;
		OutDelta.Phase = State.Phase;
		OutDelta.Outcome = State.Outcome;
		OutDelta.OutcomeCause = State.OutcomeCause;
		OutDelta.PendingDecision = State.PendingDecision;
		OutDelta.PendingDecisionRequests = State.PendingDecisionRequests;
		OutDelta.PendingReplacements = State.PendingReplacements;
	}

	bool IsProjectedActiveBattler(
		const FBattleEngineState& State,
		const FWildActionStateDelta& Delta,
		const FBattlerId BattlerId)
	{
		return State.ActivePositions.ContainsByPredicate(
			[&Delta, BattlerId](const FBattleActivePositionState& Position)
			{
				return (!Delta.bRemoveBattler
						|| Position.ActiveSlotId != Delta.ClearedActiveSlotId)
					&& Position.BattlerId == BattlerId;
			});
	}

	FTrainerId FindWildActionInitialSlotOwner(
		const FBattleEngineState& State,
		const FActiveSlotId ActiveSlotId)
	{
		for (const FBattleActiveAssignment& Assignment : State.Setup.GetStartingActive())
		{
			if (Assignment.ActiveSlotId == ActiveSlotId)
			{
				return Assignment.TrainerId;
			}
		}
		return FTrainerId();
	}

	bool HasProjectedReplacementRequirement(
		const FBattleEngineState& State,
		const FWildActionStateDelta& Delta)
	{
		for (const FBattleActivePositionState& Position : State.ActivePositions)
		{
			const bool bProjectedEmpty = Position.bAvailable
				&& (!Position.BattlerId.IsValid()
					|| (Delta.bRemoveBattler
						&& Position.ActiveSlotId == Delta.ClearedActiveSlotId));
			if (!bProjectedEmpty)
			{
				continue;
			}

			const FTrainerId InitialOwner = FindWildActionInitialSlotOwner(
				State,
				Position.ActiveSlotId);
			if (!InitialOwner.IsValid())
			{
				continue;
			}

			for (const FBattleBattlerState& Battler : State.Battlers)
			{
				if (Battler.TrainerId == InitialOwner
					&& Battler.BattlerId != Delta.RemovedBattlerId
					&& IsLivingSelectableBattler(&Battler)
					&& !IsProjectedActiveBattler(State, Delta, Battler.BattlerId))
				{
					return true;
				}
			}
		}
		return false;
	}

	bool TryPrepareWildActionBoundary(
		const FBattleEngineState& State,
		const bool bTerminal,
		FWildActionStateDelta& Delta)
	{
		if (bTerminal)
		{
			Delta.PendingDecision.Reset();
			Delta.PendingDecisionRequests.Reset();
			Delta.PendingReplacements.Reset();
			return true;
		}
		if (Delta.NextLockedActionIndex < State.LockedActions.Num())
		{
			Delta.Phase = EBattlePhase::Resolving;
			return true;
		}
		if (HasProjectedReplacementRequirement(State, Delta))
		{
			return false;
		}

		Delta.Phase = EBattlePhase::EndOfTurn;
		Delta.PendingDecision.Reset();
		Delta.PendingDecisionRequests.Reset();
		Delta.PendingReplacements.Reset();
		return true;
	}

	FBattleEventSpec MakeStagedWildActionEventSpec(
		const FBattleEngineState& State,
		const FResolutionId ResolutionId,
		const FActionId ActionId,
		const EBattleActionKind ActionKind,
		const FBattleEventSource& Source,
		const EBattleEventType Type,
		const EBattleEventCause Cause,
		const EBattleOutcomeCause OutcomeCause = EBattleOutcomeCause::None,
		const FBattleEventTarget* Target = nullptr)
	{
		FBattleEventSpec Spec;
		Spec.BattleId = State.Setup.GetBattleId();
		Spec.TurnId = State.TurnId;
		Spec.ActionId = ActionId;
		Spec.ResolutionId = ResolutionId;
		Spec.Type = Type;
		Spec.Cause = Cause;
		Spec.CauseActionKind = ActionKind;
		Spec.OutcomeCause = OutcomeCause;
		Spec.Source = Source;
		if (Target != nullptr)
		{
			Spec.Targets.Add(*Target);
		}
		Spec.Visibility.Level = EBattleVisibilityLevel::Public;
		return Spec;
	}

	FBattleResolution PublishWildActionCheckpointRejection(
		FBattleEngineState& State,
		const FResolutionId ResolutionId,
		const FActionId ActionId,
		const EBattleRejectionReason Reason,
		const FTrainerId TrainerId,
		const FBattlerId BattlerId,
		const EBattleActionKind ActionKind,
		const FBattleEventSource& Source)
	{
		FBattleResolutionCommitPlan RejectedPlan;
		const bool bPrepared = FBattleResolutionCommit::TryBuildRejectedPlan(
			State,
			ResolutionId,
			ActionId,
			Reason,
			TrainerId,
			BattlerId,
			ActionKind,
			Source,
			RejectedPlan);
		check(bPrepared);
		const FBattleResolution Resolution = FBattleResolutionCommit::PublishPrepared(
			State,
			RejectedPlan);
		return Resolution;
	}

	void ApplyWildActionDelta(
		FBattleEngineState& State,
		const FBattleResolutionCommitIdentity& Identity,
		const FWildActionStateDelta& Delta)
	{
		check(State.LockedActions.IsValidIndex(Identity.ExpectedLockedActionIndex));
		FBattleLockedActionState& Action =
			State.LockedActions[Identity.ExpectedLockedActionIndex];
		check(Action.ActionId == Identity.OwningActionId && Action.bStarted && !Action.bFinished);
		Action.bFinished = true;

		if (Delta.bApplyCleanupStage)
		{
			State.TriggerFramework = Delta.CleanupStage.TriggerFramework;
			State.NextTriggerReentrancyToken =
				Delta.CleanupStage.NextTriggerReentrancyToken;
			for (const FWildActionStagedVolatiles& Staged :
				Delta.CleanupStage.BattlerVolatiles)
			{
				FBattleBattlerState* Battler = State.FindMutableBattler(Staged.BattlerId);
				check(Battler != nullptr);
				Battler->Volatiles = Staged.Volatiles;
			}
		}

		if (Delta.bRemoveBattler)
		{
			FBattleBattlerState* Battler = State.FindMutableBattler(Delta.RemovedBattlerId);
			FBattleActivePositionState* Active = State.FindMutableActivePosition(
				Delta.ClearedActiveSlotId);
			check(Battler != nullptr
				&& Active != nullptr
				&& Active->BattlerId == Delta.RemovedBattlerId);
			Battler->MajorStatusId = FConditionId();
			Battler->Stages = FBattleStatStages();
			Battler->Volatiles.Reset();
			Battler->LastMoveId = FMoveId();
			Battler->bAbilitySuppressed = false;
			Battler->HeldItem.ChoiceLockedMoveId = FMoveId();
			Battler->EnteredActiveOnTurnId = FTurnId();
			Battler->bRemoved = true;
			Battler->bFaintTransitionPending = false;
			Active->TrainerId = FTrainerId();
			Active->BattlerId = FBattlerId();
		}

		State.EscapeAttemptCount = Delta.EscapeAttemptCount;
		State.Phase = Delta.Phase;
		State.Outcome = Delta.Outcome;
		State.OutcomeCause = Delta.OutcomeCause;
		State.PendingDecision = Delta.PendingDecision;
		State.PendingDecisionRequests = Delta.PendingDecisionRequests;
		State.PendingReplacements = Delta.PendingReplacements;
		State.CurrentLockedActionIndex = Delta.NextLockedActionIndex;
		if (Delta.OpponentRemovalCheckpointOrdinal.IsSet())
		{
			State.AvailableOpponentRemovalCheckpoints.Add(
				Delta.OpponentRemovalCheckpointOrdinal.GetValue());
		}
	}

	/** Bounded trigger projection used only by one non-Capture Bag invocation. */
	struct FBagItemCleanupStage
	{
		FBattleTriggerFramework TriggerFramework;
		uint64 NextTriggerReentrancyToken = 0;

		void Capture(const FBattleEngineState& State)
		{
			TriggerFramework = State.TriggerFramework;
			NextTriggerReentrancyToken = State.NextTriggerReentrancyToken;
		}
	};

	bool TryTakeBagItemTriggerOperationContext(
		FBagItemCleanupStage& Stage,
		FBattleTriggerOperationContext& OutContext)
	{
		OutContext = FBattleTriggerOperationContext();
		if (Stage.NextTriggerReentrancyToken == 0
			|| Stage.NextTriggerReentrancyToken == TNumericLimits<uint64>::Max()
			|| !FBattleTriggerReentrancyToken::TryCreate(
				Stage.NextTriggerReentrancyToken,
				OutContext.ReentrancyToken))
		{
			return false;
		}
		++Stage.NextTriggerReentrancyToken;
		return true;
	}

	void DrainBagItemTriggerOutputs(FBagItemCleanupStage& Stage)
	{
		TArray<FBattleTriggerEffectRequest> IgnoredRequests;
		TArray<FBattleTriggerLifecycleFact> IgnoredFacts;
		Stage.TriggerFramework.DrainEffectRequests(IgnoredRequests);
		Stage.TriggerFramework.DrainLifecycleFacts(IgnoredFacts);
	}

	bool TryStageBagItemMajorStatusCleanup(
		FBagItemCleanupStage& Stage,
		const FConditionId& StatusId,
		const FBattlerId BattlerId)
	{
		if (!FBattleMajorStatusRules::IsCanonical(StatusId))
		{
			return true;
		}
		FBattleTriggerSubject Owner;
		FBattleTriggerOperationContext Operation;
		EBattleTriggerError Error = EBattleTriggerError::None;
		if (!TryMakeBattlerTriggerSubject(BattlerId, Owner)
			|| !TryTakeBagItemTriggerOperationContext(Stage, Operation)
			|| !FBattleMajorStatusRules::TryCleanupTriggers(
				Stage.TriggerFramework,
				StatusId,
				Owner,
				EBattleTriggerCleanupReason::Removal,
				Operation,
				Error))
		{
			return false;
		}
		DrainBagItemTriggerOutputs(Stage);
		return true;
	}

	bool TryStageBagItemVolatileCleanup(
		FBagItemCleanupStage& Stage,
		const FConditionId& VolatileId,
		const FBattlerId BattlerId)
	{
		if (!FBattleVolatileRules::IsCanonical(VolatileId))
		{
			return true;
		}
		FBattleTriggerSubject Owner;
		FBattleTriggerOperationContext Operation;
		EBattleTriggerError Error = EBattleTriggerError::None;
		if (!TryMakeBattlerTriggerSubject(BattlerId, Owner)
			|| !TryTakeBagItemTriggerOperationContext(Stage, Operation)
			|| !FBattleVolatileRules::TryCleanupTriggers(
				Stage.TriggerFramework,
				VolatileId,
				Owner,
				EBattleTriggerCleanupReason::Removal,
				Operation,
				Error))
		{
			return false;
		}
		DrainBagItemTriggerOutputs(Stage);
		return true;
	}

	struct FBagItemStateDelta
	{
		FTrainerId ActingTrainerId;
		TArray<FBattleBagItemCount> Bag;
		bool bBagActionAvailable = false;
		FBattlerId TargetBattlerId;
		FBattleBattlerState TargetBattler;
		bool bApplyCleanupStage = false;
		FBagItemCleanupStage CleanupStage;
		int32 NextLockedActionIndex = INDEX_NONE;
		EBattlePhase Phase = EBattlePhase::Resolving;
		TOptional<FBattleDecisionRequest> PendingDecision;
		TArray<FBattleDecisionRequest> PendingDecisionRequests;
		TArray<FBattlePendingReplacementState> PendingReplacements;
	};

	/** Complete private transaction for one accepted stale Bag cancellation. */
	struct FAcceptedBagCancellationDelta
	{
		FBattleResolutionCommitIdentity ActionIdentity;
		int32 NextLockedActionIndex = INDEX_NONE;
		EBattlePhase Phase = EBattlePhase::Resolving;
		TArray<FBattlePendingReplacementState> PendingReplacements;
		TOptional<FBattleDecisionRequest> PendingDecision;
		TArray<FBattleDecisionRequest> PendingDecisionRequests;
		FBattleResolutionCommitPlan ResolutionPlan;
	};

	/**
	 * Call-scoped read-only adapter over unchanged Battle authority and the
	 * accepted-cancellation fields that have already been staged.
	 */
	struct FAcceptedBagCancellationStateView
	{
		const FBattleDefinitionCatalog& Catalog;
		const TArray<FBattleTrainerState>& Trainers;
		const TArray<FBattleBattlerState>& Battlers;
		const TArray<FBattleActivePositionState>& ActivePositions;
		const FBattleCompiledEncounterPolicies& CompiledEncounterPolicies;
		const EBattlePhase& Phase;
		const TArray<FBattlePendingReplacementState>& PendingReplacements;

		FAcceptedBagCancellationStateView(
			const FBattleEngineState& State,
			const FAcceptedBagCancellationDelta& Delta)
			: Catalog(State.Catalog)
			, Trainers(State.Trainers)
			, Battlers(State.Battlers)
			, ActivePositions(State.ActivePositions)
			, CompiledEncounterPolicies(State.CompiledEncounterPolicies)
			, Phase(Delta.Phase)
			, PendingReplacements(Delta.PendingReplacements)
		{
		}

		[[nodiscard]] const FBattleTrainerState* FindTrainer(
			const FTrainerId TrainerId) const
		{
			return Trainers.FindByPredicate(
				[TrainerId](const FBattleTrainerState& Candidate)
				{
					return Candidate.TrainerId == TrainerId;
				});
		}

		[[nodiscard]] const FBattleBattlerState* FindBattler(
			const FBattlerId BattlerId) const
		{
			return Battlers.FindByPredicate(
				[BattlerId](const FBattleBattlerState& Candidate)
				{
					return Candidate.BattlerId == BattlerId;
				});
		}

		[[nodiscard]] const FBattleActivePositionState* FindActivePosition(
			const FActiveSlotId ActiveSlotId) const
		{
			return ActivePositions.FindByPredicate(
				[ActiveSlotId](const FBattleActivePositionState& Candidate)
				{
					return Candidate.ActiveSlotId == ActiveSlotId;
				});
		}
	};

	bool TryPrepareBagItemBoundary(
		const FBattleEngineState& State,
		const uint64 RequestStateVersion,
		FBagItemStateDelta& Delta,
		TArray<FBattleReplacementRequirement>& OutRequirements)
	{
		OutRequirements.Reset();
		if (RequestStateVersion == 0 || !Delta.TargetBattlerId.IsValid())
		{
			return false;
		}

		Delta.NextLockedActionIndex = State.CurrentLockedActionIndex + 1;
		Delta.Phase = State.Phase;
		Delta.PendingDecision = State.PendingDecision;
		Delta.PendingDecisionRequests = State.PendingDecisionRequests;
		Delta.PendingReplacements = State.PendingReplacements;

		FBattleEngineState Projection;
		Projection.Setup = State.Setup;
		Projection.Catalog = State.Catalog;
		Projection.bHasCatalog = State.bHasCatalog;
		Projection.StateVersion = State.StateVersion;
		Projection.TurnId = State.TurnId;
		Projection.EncounterKind = State.EncounterKind;
		Projection.Format = State.Format;
		Projection.Phase = State.Phase;
		Projection.Outcome = State.Outcome;
		Projection.OutcomeCause = State.OutcomeCause;
		Projection.Trainers = State.Trainers;
		Projection.Battlers = State.Battlers;
		Projection.ActivePositions = State.ActivePositions;
		Projection.CompiledEncounterPolicies = State.CompiledEncounterPolicies;
		Projection.LockedActions = State.LockedActions;
		Projection.CurrentLockedActionIndex = Delta.NextLockedActionIndex;
		Projection.PendingDecision = State.PendingDecision;
		Projection.PendingDecisionRequests = State.PendingDecisionRequests;
		Projection.PendingReplacements = State.PendingReplacements;

		FBattleBattlerState* ProjectedTarget = Projection.FindMutableBattler(
			Delta.TargetBattlerId);
		if (ProjectedTarget == nullptr)
		{
			return false;
		}
		*ProjectedTarget = Delta.TargetBattler;

		FBattleFaintOutcomeResolver::ResolveQueueBoundary(
			Projection,
			OutRequirements);
		Delta.Phase = Projection.Phase;
		if (Projection.Phase == EBattlePhase::MandatoryReplacement)
		{
			Projection.PendingReplacements.Reset();
			for (const FBattleReplacementRequirement& Requirement : OutRequirements)
			{
				FBattlePendingReplacementState& Pending =
					Projection.PendingReplacements.AddDefaulted_GetRef();
				Pending.TrainerId = Requirement.Target.TrainerId;
				Pending.ActiveSlotId = Requirement.Target.ActiveSlotId;
			}
			TArray<FBattleDecisionRequest> Requests;
			if (!TryBuildReplacementCheckpointRequests(
					Projection,
					RequestStateVersion,
					true,
					Requests)
				|| Requests.IsEmpty())
			{
				return false;
			}
			Delta.PendingReplacements = Projection.PendingReplacements;
			Delta.PendingDecisionRequests = MoveTemp(Requests);
			Delta.PendingDecision = Delta.PendingDecisionRequests[0];
		}
		else if (Projection.Phase == EBattlePhase::EndOfTurn)
		{
			Delta.PendingReplacements.Reset();
			Delta.PendingDecisionRequests.Reset();
			Delta.PendingDecision.Reset();
		}
		return true;
	}

	FBattleEventSpec MakeStagedBagItemEventSpec(
		const FBattleEngineState& State,
		const FResolutionId ResolutionId,
		const FActionId ActionId,
		const FBattleEventSource& Source,
		const EBattleEventType Type,
		const EBattleEventCause Cause,
		const FBattleEventTarget* Target = nullptr,
		const TOptional<int64> NumericBefore = TOptional<int64>(),
		const TOptional<int64> NumericAfter = TOptional<int64>(),
		const TOptional<int64> NumericDelta = TOptional<int64>())
	{
		FBattleEventSpec Spec;
		Spec.BattleId = State.Setup.GetBattleId();
		Spec.TurnId = State.TurnId;
		Spec.ActionId = ActionId;
		Spec.ResolutionId = ResolutionId;
		Spec.Type = Type;
		Spec.Cause = Cause;
		Spec.CauseActionKind = EBattleActionKind::Bag;
		Spec.Source = Source;
		if (Target != nullptr)
		{
			Spec.Targets.Add(*Target);
		}
		Spec.NumericBefore = NumericBefore;
		Spec.NumericAfter = NumericAfter;
		Spec.NumericDelta = NumericDelta;
		Spec.Visibility.Level = EBattleVisibilityLevel::Public;
		Spec.Visibility.bRevealSourceDefinition =
			Type == EBattleEventType::ItemUsed
			|| Type == EBattleEventType::ItemConsumed;
		return Spec;
	}

	FBattleResolution PublishBagItemCheckpointRejection(
		FBattleEngineState& State,
		const FResolutionId ResolutionId,
		const FActionId ActionId,
		const EBattleRejectionReason Reason,
		const FTrainerId TrainerId,
		const FBattlerId BattlerId,
		const FBattleEventSource& Source)
	{
		FBattleResolutionCommitPlan RejectedPlan;
		const bool bPrepared = FBattleResolutionCommit::TryBuildRejectedPlan(
			State,
			ResolutionId,
			ActionId,
			Reason,
			TrainerId,
			BattlerId,
			EBattleActionKind::Bag,
			Source,
			RejectedPlan);
		check(bPrepared);
		const FBattleResolution Resolution = FBattleResolutionCommit::PublishPrepared(
			State,
			RejectedPlan);
		EBattleStateValidationError StateError = EBattleStateValidationError::None;
		const bool bStateValid = State.ValidateInvariants(StateError);
		check(bStateValid);
		return Resolution;
	}

	void ApplyBagItemDelta(
		FBattleEngineState& State,
		const FBattleResolutionCommitIdentity& Identity,
		const FBagItemStateDelta& Delta)
	{
		check(State.LockedActions.IsValidIndex(Identity.ExpectedLockedActionIndex));
		FBattleLockedActionState& Action =
			State.LockedActions[Identity.ExpectedLockedActionIndex];
		FBattleTrainerState* ActingTrainer = State.FindMutableTrainer(
			Delta.ActingTrainerId);
		FBattleBattlerState* TargetBattler = State.FindMutableBattler(
			Delta.TargetBattlerId);
		check(Action.ActionId == Identity.OwningActionId
			&& Action.bStarted
			&& !Action.bFinished
			&& ActingTrainer != nullptr
			&& TargetBattler != nullptr
			&& Delta.TargetBattler.BattlerId == Delta.TargetBattlerId);

		ActingTrainer->Bag = Delta.Bag;
		ActingTrainer->ActionAllowance.bBagActionAvailable =
			Delta.bBagActionAvailable;
		if (Delta.bApplyCleanupStage)
		{
			State.TriggerFramework = Delta.CleanupStage.TriggerFramework;
			State.NextTriggerReentrancyToken =
				Delta.CleanupStage.NextTriggerReentrancyToken;
		}
		*TargetBattler = Delta.TargetBattler;
		Action.bFinished = true;
		State.CurrentLockedActionIndex = Delta.NextLockedActionIndex;
		State.Phase = Delta.Phase;
		State.PendingDecision = Delta.PendingDecision;
		State.PendingDecisionRequests = Delta.PendingDecisionRequests;
		State.PendingReplacements = Delta.PendingReplacements;
	}

	struct FCaptureQueuedCancellationFact
	{
		FActionId ActionId;
		bool bCapturedActor = false;
		bool bCapturedTarget = false;
	};

	/** Complete private mutation set for one legal Capture attempt. */
	struct FCaptureStateDelta
	{
		FTrainerId ActingTrainerId;
		TArray<FBattleBagItemCount> Bag;
		bool bBagActionAvailable = false;
		bool bSucceeded = false;
		FBattlerId TargetBattlerId;
		FBattleBattlerState TargetBattler;
		FActiveSlotId ClearedActiveSlotId;
		TOptional<FBattlePendingCaptureRecord> PendingCapture;
		TArray<FCaptureQueuedCancellationFact> QueuedCancellations;
		bool bApplyCleanupStage = false;
		FWildActionCleanupStage CleanupStage;
		int32 NextLockedActionIndex = INDEX_NONE;
		EBattlePhase Phase = EBattlePhase::Resolving;
		EBattleOutcome Outcome = EBattleOutcome::InProgress;
		EBattleOutcomeCause OutcomeCause = EBattleOutcomeCause::None;
		TOptional<FBattleDecisionRequest> PendingDecision;
		TArray<FBattleDecisionRequest> PendingDecisionRequests;
		TArray<FBattlePendingReplacementState> PendingReplacements;
		TOptional<uint64> OpponentRemovalCheckpointOrdinal;
	};

	void InitializeCaptureDelta(
		const FBattleEngineState& State,
		const FBattleTrainerBagState& AppliedBag,
		const FTrainerId ActingTrainerId,
		const FBattlerId TargetBattlerId,
		FCaptureStateDelta& OutDelta)
	{
		OutDelta = FCaptureStateDelta();
		OutDelta.ActingTrainerId = ActingTrainerId;
		OutDelta.Bag = AppliedBag.Items;
		OutDelta.bBagActionAvailable = AppliedBag.bBagActionAvailable;
		OutDelta.TargetBattlerId = TargetBattlerId;
		OutDelta.NextLockedActionIndex = State.CurrentLockedActionIndex + 1;
		OutDelta.Phase = State.Phase;
		OutDelta.Outcome = State.Outcome;
		OutDelta.OutcomeCause = State.OutcomeCause;
		OutDelta.PendingDecision = State.PendingDecision;
		OutDelta.PendingDecisionRequests = State.PendingDecisionRequests;
		OutDelta.PendingReplacements = State.PendingReplacements;
	}

	bool TryBuildPendingCaptureRecord(
		const FBattleEngineState& State,
		const FBattleBattlerState& TargetBattler,
		FBattlePendingCaptureRecord& OutRecord)
	{
		OutRecord = FBattlePendingCaptureRecord();
		OutRecord.CaptureOrdinal =
			static_cast<uint64>(State.PendingCaptures.Num()) + 1ULL;
		OutRecord.Destination = State.PendingCaptures.Num()
			< State.CaptureCapacity.PartySlotsRemaining
			? EBattlePendingCaptureDestination::Party
			: EBattlePendingCaptureDestination::Storage;
		OutRecord.OriginalTrainerId = TargetBattler.TrainerId;
		OutRecord.BattlerId = TargetBattler.BattlerId;
		OutRecord.SourcePokemonId = TargetBattler.SourcePokemonId;
		OutRecord.SpeciesFormId = TargetBattler.SpeciesFormId;
		OutRecord.SpeciesClassification = TargetBattler.CaptureClassification;
		OutRecord.Level = TargetBattler.Level;
		OutRecord.CurrentHP = TargetBattler.CurrentHP;
		OutRecord.MaxHP = TargetBattler.PermanentStats.MaxHP;
		OutRecord.MajorStatusId = TargetBattler.MajorStatusId;
		for (const FBattleMoveSlotState& Move : TargetBattler.Moves)
		{
			FBattleCapturedMoveFact& MoveFact = OutRecord.Moves.AddDefaulted_GetRef();
			MoveFact.SlotIndex = Move.SlotIndex;
			MoveFact.MoveId = Move.MoveId;
			MoveFact.CurrentPP = Move.CurrentPP;
			MoveFact.MaxPP = Move.MaxPP;
		}
		OutRecord.HeldItem.OriginalItemId = TargetBattler.HeldItem.OriginalItemId;
		OutRecord.HeldItem.CurrentItemId = TargetBattler.HeldItem.CurrentItemId;
		OutRecord.HeldItem.bConsumed = TargetBattler.HeldItem.bConsumed;
		OutRecord.HeldItem.bSuppressed = TargetBattler.HeldItem.bSuppressed;
		OutRecord.HeldItem.bRevealed = TargetBattler.HeldItem.bRevealed;
		OutRecord.HeldItem.bTemporarilyRemoved =
			TargetBattler.HeldItem.bTemporarilyRemoved;
		OutRecord.HeldItem.ChoiceLockedMoveId =
			TargetBattler.HeldItem.ChoiceLockedMoveId;
		return OutRecord.IsValid();
	}

	void StageCaptureQueueCancellationFacts(
		const FBattleEngineState& State,
		const FBattlerId TargetBattlerId,
		FCaptureStateDelta& Delta)
	{
		Delta.QueuedCancellations.Reset();
		for (int32 ActionIndex = State.CurrentLockedActionIndex + 1;
			ActionIndex < State.LockedActions.Num();
			++ActionIndex)
		{
			const FBattleLockedActionState& Candidate = State.LockedActions[ActionIndex];
			FCaptureQueuedCancellationFact Fact;
			Fact.ActionId = Candidate.ActionId;
			Fact.bCapturedActor =
				Candidate.Decision.GetActingBattlerId() == TargetBattlerId;
			Fact.bCapturedTarget =
				Candidate.Decision.GetActionKind() == EBattleActionKind::Fight
				&& Candidate.SelectedTargetBattlerId == TargetBattlerId;
			if (Fact.bCapturedActor || Fact.bCapturedTarget)
			{
				Delta.QueuedCancellations.Add(Fact);
			}
		}
	}

	bool DoesLivingOpponentRemainAfterCapture(
		const FBattleEngineState& State,
		const FBattlerId CapturedBattlerId)
	{
		for (const FBattleBattlerState& Battler : State.Battlers)
		{
			if (Battler.BattlerId == CapturedBattlerId)
			{
				continue;
			}
			const FBattleTrainerState* Trainer = State.FindTrainer(Battler.TrainerId);
			if (Trainer != nullptr
				&& Trainer->Side == EBattleSide::Opponent
				&& IsLivingSelectableBattler(&Battler))
			{
				return true;
			}
		}
		return false;
	}

	bool TryPrepareCaptureBoundary(
		const FBattleEngineState& State,
		const uint64 RequestStateVersion,
		FCaptureStateDelta& Delta,
		TArray<FBattleReplacementRequirement>& OutRequirements)
	{
		OutRequirements.Reset();
		if (RequestStateVersion == 0
			|| !Delta.TargetBattlerId.IsValid()
			|| Delta.NextLockedActionIndex <= State.CurrentLockedActionIndex)
		{
			return false;
		}

		if (Delta.Phase == EBattlePhase::Terminal)
		{
			Delta.PendingDecision.Reset();
			Delta.PendingDecisionRequests.Reset();
			Delta.PendingReplacements.Reset();
			return true;
		}

		FBattleEngineState Projection;
		Projection.Setup = State.Setup;
		Projection.Catalog = State.Catalog;
		Projection.bHasCatalog = State.bHasCatalog;
		Projection.StateVersion = State.StateVersion;
		Projection.TurnId = State.TurnId;
		Projection.EncounterKind = State.EncounterKind;
		Projection.Format = State.Format;
		Projection.Phase = Delta.Phase;
		Projection.Outcome = Delta.Outcome;
		Projection.OutcomeCause = Delta.OutcomeCause;
		Projection.Trainers = State.Trainers;
		Projection.Battlers = State.Battlers;
		Projection.ActivePositions = State.ActivePositions;
		Projection.CompiledEncounterPolicies = State.CompiledEncounterPolicies;
		Projection.LockedActions = State.LockedActions;
		Projection.CurrentLockedActionIndex = Delta.NextLockedActionIndex;
		Projection.PendingDecision = Delta.PendingDecision;
		Projection.PendingDecisionRequests = Delta.PendingDecisionRequests;
		Projection.PendingReplacements = Delta.PendingReplacements;

		if (Delta.bSucceeded)
		{
			FBattleBattlerState* ProjectedTarget = Projection.FindMutableBattler(
				Delta.TargetBattlerId);
			FBattleActivePositionState* ProjectedActive =
				Projection.FindMutableActivePosition(Delta.ClearedActiveSlotId);
			if (ProjectedTarget == nullptr
				|| ProjectedActive == nullptr
				|| ProjectedActive->BattlerId != Delta.TargetBattlerId)
			{
				return false;
			}
			*ProjectedTarget = Delta.TargetBattler;
			ProjectedActive->TrainerId = FTrainerId();
			ProjectedActive->BattlerId = FBattlerId();
		}

		FBattleFaintOutcomeResolver::ResolveQueueBoundary(
			Projection,
			OutRequirements);
		Delta.Phase = Projection.Phase;
		if (Projection.Phase == EBattlePhase::MandatoryReplacement)
		{
			Projection.PendingReplacements.Reset();
			for (const FBattleReplacementRequirement& Requirement : OutRequirements)
			{
				FBattlePendingReplacementState& Pending =
					Projection.PendingReplacements.AddDefaulted_GetRef();
				Pending.TrainerId = Requirement.Target.TrainerId;
				Pending.ActiveSlotId = Requirement.Target.ActiveSlotId;
			}
			TArray<FBattleDecisionRequest> Requests;
			if (!TryBuildReplacementCheckpointRequests(
					Projection,
					RequestStateVersion,
					true,
					Requests)
				|| Requests.IsEmpty())
			{
				return false;
			}
			Delta.PendingReplacements = Projection.PendingReplacements;
			Delta.PendingDecisionRequests = MoveTemp(Requests);
			Delta.PendingDecision = Delta.PendingDecisionRequests[0];
		}
		else if (Projection.Phase == EBattlePhase::EndOfTurn)
		{
			Delta.PendingReplacements.Reset();
			Delta.PendingDecisionRequests.Reset();
			Delta.PendingDecision.Reset();
		}
		return true;
	}

	FBattleEventSpec MakeStagedCaptureEventSpec(
		const FBattleEngineState& State,
		const FResolutionId ResolutionId,
		const FActionId ActionId,
		const FBattleEventSource& Source,
		const EBattleEventType Type,
		const EBattleEventCause Cause,
		const EBattleOutcomeCause OutcomeCause = EBattleOutcomeCause::None,
		const FBattleEventTarget* Target = nullptr,
		const FBattleCaptureEventMetadata* Capture = nullptr)
	{
		FBattleEventSpec Spec;
		Spec.BattleId = State.Setup.GetBattleId();
		Spec.TurnId = State.TurnId;
		Spec.ActionId = ActionId;
		Spec.ResolutionId = ResolutionId;
		Spec.Type = Type;
		Spec.Cause = Cause;
		Spec.CauseActionKind = EBattleActionKind::Bag;
		Spec.OutcomeCause = OutcomeCause;
		Spec.Source = Source;
		if (Target != nullptr)
		{
			Spec.Targets.Add(*Target);
		}
		if (Capture != nullptr)
		{
			Spec.Capture = *Capture;
		}
		Spec.Visibility.Level = EBattleVisibilityLevel::Public;
		Spec.Visibility.bRevealSourceDefinition =
			Type == EBattleEventType::ItemUsed
			|| Type == EBattleEventType::ItemConsumed
			|| Type == EBattleEventType::CaptureAttempted
			|| Type == EBattleEventType::Captured;
		return Spec;
	}

	void ApplyCaptureDelta(
		FBattleEngineState& State,
		const FBattleResolutionCommitIdentity& Identity,
		const FCaptureStateDelta& Delta)
	{
		check(State.LockedActions.IsValidIndex(Identity.ExpectedLockedActionIndex));
		FBattleLockedActionState& Action =
			State.LockedActions[Identity.ExpectedLockedActionIndex];
		FBattleTrainerState* ActingTrainer = State.FindMutableTrainer(
			Delta.ActingTrainerId);
		check(Action.ActionId == Identity.OwningActionId
			&& Action.bStarted
			&& !Action.bFinished
			&& ActingTrainer != nullptr);

		ActingTrainer->Bag = Delta.Bag;
		ActingTrainer->ActionAllowance.bBagActionAvailable =
			Delta.bBagActionAvailable;
		if (Delta.bApplyCleanupStage)
		{
			State.TriggerFramework = Delta.CleanupStage.TriggerFramework;
			State.NextTriggerReentrancyToken =
				Delta.CleanupStage.NextTriggerReentrancyToken;
			for (const FWildActionStagedVolatiles& Staged :
				Delta.CleanupStage.BattlerVolatiles)
			{
				FBattleBattlerState* Battler = State.FindMutableBattler(Staged.BattlerId);
				check(Battler != nullptr);
				Battler->Volatiles = Staged.Volatiles;
			}
		}

		if (Delta.bSucceeded)
		{
			FBattleBattlerState* TargetBattler = State.FindMutableBattler(
				Delta.TargetBattlerId);
			FBattleActivePositionState* TargetActive = State.FindMutableActivePosition(
				Delta.ClearedActiveSlotId);
			check(TargetBattler != nullptr
				&& TargetActive != nullptr
				&& TargetActive->BattlerId == Delta.TargetBattlerId
				&& Delta.TargetBattler.BattlerId == Delta.TargetBattlerId
				&& Delta.PendingCapture.IsSet()
				&& Delta.PendingCapture.GetValue().CaptureOrdinal
					== static_cast<uint64>(State.PendingCaptures.Num()) + 1ULL);
			*TargetBattler = Delta.TargetBattler;
			TargetActive->TrainerId = FTrainerId();
			TargetActive->BattlerId = FBattlerId();
			State.PendingCaptures.Add(Delta.PendingCapture.GetValue());

			for (const FCaptureQueuedCancellationFact& Fact : Delta.QueuedCancellations)
			{
				const FBattleLockedActionState* Queued = State.LockedActions.FindByPredicate(
					[&Fact](const FBattleLockedActionState& Candidate)
					{
						return Candidate.ActionId == Fact.ActionId;
					});
				check(Queued != nullptr
					&& ((!Fact.bCapturedActor
							|| Queued->Decision.GetActingBattlerId() == Delta.TargetBattlerId)
						&& (!Fact.bCapturedTarget
							|| (Queued->Decision.GetActionKind() == EBattleActionKind::Fight
								&& Queued->SelectedTargetBattlerId == Delta.TargetBattlerId))));
			}
		}

		Action.bFinished = true;
		State.CurrentLockedActionIndex = Delta.NextLockedActionIndex;
		State.Phase = Delta.Phase;
		State.Outcome = Delta.Outcome;
		State.OutcomeCause = Delta.OutcomeCause;
		State.PendingDecision = Delta.PendingDecision;
		State.PendingDecisionRequests = Delta.PendingDecisionRequests;
		State.PendingReplacements = Delta.PendingReplacements;
		if (Delta.OpponentRemovalCheckpointOrdinal.IsSet())
		{
			State.AvailableOpponentRemovalCheckpoints.Add(
				Delta.OpponentRemovalCheckpointOrdinal.GetValue());
		}
	}

	/** Exact immutable identity for one not-yet-started locked action. */
	struct FActionStartCheckpointIdentity
	{
		FBattleResolutionCommitIdentity CommitIdentity;
		EBattlePhase ExpectedPhase = EBattlePhase::Setup;
		int32 ExpectedLockedActionCount = 0;
		int32 ExpectedEventCount = 0;
		uint64 ExpectedQueueOrdinal = 0;
		FTrainerId ExpectedDecisionOwnerTrainerId;
		FBattlerId ExpectedActorId;
		FActiveSlotId ExpectedActingSlotId;
		EBattleActionKind ExpectedActionKind = EBattleActionKind::Fight;
		FMoveId ExpectedMoveId;
		FPartySlotId ExpectedSwitchPartySlotId;
		FItemId ExpectedItemId;
		FPartySlotId ExpectedItemPartyTargetId;
		FActiveSlotId ExpectedActiveTargetId;
		EBattleTargetClass ExpectedTargetClass = EBattleTargetClass::SelectedOpponent;
		FBattlerId ExpectedSelectedTargetBattlerId;
		bool bExpectedStarted = false;
		bool bExpectedMoveCommitted = false;
		bool bExpectedTargetResolution = false;
		EBattleLockedEffectExecutionState ExpectedEffectExecutionState =
			EBattleLockedEffectExecutionState::Pending;
		bool bExpectedFinished = false;
	};

	bool TryCaptureActionStartCheckpointIdentity(
		const FBattleEngineState& State,
		const FResolutionId ResolutionId,
		const FBattleLockedActionState& Action,
		FActionStartCheckpointIdentity& OutIdentity)
	{
		OutIdentity = FActionStartCheckpointIdentity();
		if (!ResolutionId.IsValid()
			|| !Action.ActionId.IsValid()
			|| State.StateVersion == 0
			|| (State.Phase != EBattlePhase::Locked
				&& State.Phase != EBattlePhase::Resolving)
			|| !State.LockedActions.IsValidIndex(State.CurrentLockedActionIndex)
			|| &State.LockedActions[State.CurrentLockedActionIndex] != &Action
			|| Action.bStarted
			|| Action.bMoveCommitted
			|| Action.TargetResolution.IsSet()
			|| Action.EffectExecutionState != EBattleLockedEffectExecutionState::Pending
			|| Action.bFinished
			|| !State.Random.IsValid()
			|| State.NextResolutionId == 0
			|| State.NextEventOrdinal == 0)
		{
			return false;
		}

		OutIdentity.CommitIdentity.ResolutionId = ResolutionId;
		OutIdentity.CommitIdentity.OwningActionId = Action.ActionId;
		OutIdentity.CommitIdentity.ExpectedStateVersion = State.StateVersion;
		OutIdentity.CommitIdentity.ExpectedLockedActionIndex =
			State.CurrentLockedActionIndex;
		OutIdentity.CommitIdentity.ExpectedNextResolutionId = State.NextResolutionId;
		OutIdentity.CommitIdentity.ExpectedEventOrdinal = State.NextEventOrdinal;
		OutIdentity.CommitIdentity.ExpectedResolutionCount = State.Resolutions.Num();
		OutIdentity.CommitIdentity.ExpectedRandomTraceCount = State.Random->GetTrace().Num();
		OutIdentity.ExpectedPhase = State.Phase;
		OutIdentity.ExpectedLockedActionCount = State.LockedActions.Num();
		OutIdentity.ExpectedEventCount = State.OrderedEvents.Num();
		OutIdentity.ExpectedQueueOrdinal = Action.QueueOrdinal;
		OutIdentity.ExpectedDecisionOwnerTrainerId =
			Action.Decision.GetDecisionOwnerTrainerId();
		OutIdentity.ExpectedActorId = Action.Decision.GetActingBattlerId();
		OutIdentity.ExpectedActingSlotId = Action.OrderKey.ActingSlotId;
		OutIdentity.ExpectedActionKind = Action.Decision.GetActionKind();
		OutIdentity.ExpectedMoveId = Action.Decision.GetMoveId();
		OutIdentity.ExpectedSwitchPartySlotId =
			Action.Decision.GetSwitchPartySlotId();
		OutIdentity.ExpectedItemId = Action.Decision.GetItemId();
		OutIdentity.ExpectedItemPartyTargetId =
			Action.Decision.GetItemPartyTargetId();
		OutIdentity.ExpectedActiveTargetId = Action.Decision.GetActiveTargetId();
		OutIdentity.ExpectedTargetClass = Action.TargetClass;
		OutIdentity.ExpectedSelectedTargetBattlerId =
			Action.SelectedTargetBattlerId;
		OutIdentity.bExpectedStarted = Action.bStarted;
		OutIdentity.bExpectedMoveCommitted = Action.bMoveCommitted;
		OutIdentity.bExpectedTargetResolution = Action.TargetResolution.IsSet();
		OutIdentity.ExpectedEffectExecutionState = Action.EffectExecutionState;
		OutIdentity.bExpectedFinished = Action.bFinished;
		return true;
	}

	bool IsActionStartCheckpointIdentityCurrent(
		const FBattleEngineState& State,
		const FActionStartCheckpointIdentity& Identity)
	{
		// Read the trace before comparing state so a test double can deterministically
		// inject a stale caller-serialized identity without consuming randomness.
		const int32 RandomTraceCount = State.Random.IsValid()
			? State.Random->GetTrace().Num()
			: INDEX_NONE;
		const FBattleResolutionCommitIdentity& Commit = Identity.CommitIdentity;
		if (!Commit.ResolutionId.IsValid()
			|| !Commit.OwningActionId.IsValid()
			|| State.Phase != Identity.ExpectedPhase
			|| State.StateVersion != Commit.ExpectedStateVersion
			|| State.CurrentLockedActionIndex != Commit.ExpectedLockedActionIndex
			|| State.LockedActions.Num() != Identity.ExpectedLockedActionCount
			|| State.NextResolutionId != Commit.ExpectedNextResolutionId
			|| State.NextEventOrdinal != Commit.ExpectedEventOrdinal
			|| State.Resolutions.Num() != Commit.ExpectedResolutionCount
			|| State.OrderedEvents.Num() != Identity.ExpectedEventCount
			|| RandomTraceCount != Commit.ExpectedRandomTraceCount
			|| !State.LockedActions.IsValidIndex(Commit.ExpectedLockedActionIndex))
		{
			return false;
		}

		const FBattleLockedActionState& Action =
			State.LockedActions[Commit.ExpectedLockedActionIndex];
		return Action.ActionId == Commit.OwningActionId
			&& Action.QueueOrdinal == Identity.ExpectedQueueOrdinal
			&& Action.Decision.GetDecisionOwnerTrainerId()
				== Identity.ExpectedDecisionOwnerTrainerId
			&& Action.Decision.GetActingBattlerId() == Identity.ExpectedActorId
			&& Action.OrderKey.ActingSlotId == Identity.ExpectedActingSlotId
			&& Action.Decision.GetActionKind() == Identity.ExpectedActionKind
			&& Action.Decision.GetMoveId() == Identity.ExpectedMoveId
			&& Action.Decision.GetSwitchPartySlotId()
				== Identity.ExpectedSwitchPartySlotId
			&& Action.Decision.GetItemId() == Identity.ExpectedItemId
			&& Action.Decision.GetItemPartyTargetId()
				== Identity.ExpectedItemPartyTargetId
			&& Action.Decision.GetActiveTargetId() == Identity.ExpectedActiveTargetId
			&& Action.TargetClass == Identity.ExpectedTargetClass
			&& Action.SelectedTargetBattlerId
				== Identity.ExpectedSelectedTargetBattlerId
			&& Action.bStarted == Identity.bExpectedStarted
			&& Action.bMoveCommitted == Identity.bExpectedMoveCommitted
			&& Action.TargetResolution.IsSet()
				== Identity.bExpectedTargetResolution
			&& Action.EffectExecutionState == Identity.ExpectedEffectExecutionState
			&& Action.bFinished == Identity.bExpectedFinished;
	}

	struct FActionStartStagedHeldItem
	{
		FBattlerId BattlerId;
		FBattleHeldItemState HeldItem;
	};

	/** Small action-start-only projection of the fallible trigger/item/volatile work. */
	struct FActionStartMechanicsStage
	{
		FBattleTriggerFramework TriggerFramework;
		uint64 NextTriggerReentrancyToken = 0;
		FBattleHeldItemLedger HeldItemLedger;
		TArray<FActionStartStagedHeldItem> HeldItems;
		FBattlerId ActorId;
		TArray<FBattleConditionState> ActorVolatiles;

		void Capture(
			const FBattleEngineState& State,
			const FBattlerId InActorId)
		{
			TriggerFramework = State.TriggerFramework;
			NextTriggerReentrancyToken = State.NextTriggerReentrancyToken;
			HeldItemLedger = State.HeldItemLedger;
			HeldItems.Reset();
			for (const FBattleBattlerState& Battler : State.Battlers)
			{
				if (!IsHeldItemActive(Battler))
				{
					continue;
				}
				FActionStartStagedHeldItem& Staged = HeldItems.AddDefaulted_GetRef();
				Staged.BattlerId = Battler.BattlerId;
				Staged.HeldItem = Battler.HeldItem;
			}
			ActorId = InActorId;
			const FBattleBattlerState* Actor = State.FindBattler(ActorId);
			ActorVolatiles = Actor != nullptr
				? Actor->Volatiles
				: TArray<FBattleConditionState>();
		}

		FActionStartStagedHeldItem* FindMutableHeldItem(const FBattlerId BattlerId)
		{
			return HeldItems.FindByPredicate(
				[BattlerId](const FActionStartStagedHeldItem& Candidate)
				{
					return Candidate.BattlerId == BattlerId;
				});
		}

		bool HasActorVolatile(const FConditionId& VolatileId) const
		{
			return ActorVolatiles.ContainsByPredicate(
				[&VolatileId](const FBattleConditionState& Condition)
				{
					return Condition.ConditionId == VolatileId;
				});
		}
	};

	bool TryTakeActionStartTriggerOperationContext(
		FActionStartMechanicsStage& Stage,
		FBattleTriggerOperationContext& OutContext)
	{
		OutContext = FBattleTriggerOperationContext();
		if (Stage.NextTriggerReentrancyToken == 0
			|| Stage.NextTriggerReentrancyToken == TNumericLimits<uint64>::Max()
			|| !FBattleTriggerReentrancyToken::TryCreate(
				Stage.NextTriggerReentrancyToken,
				OutContext.ReentrancyToken))
		{
			return false;
		}
		++Stage.NextTriggerReentrancyToken;
		return true;
	}

	void DrainActionStartTriggerOutputs(FActionStartMechanicsStage& Stage)
	{
		TArray<FBattleTriggerEffectRequest> IgnoredRequests;
		TArray<FBattleTriggerLifecycleFact> IgnoredFacts;
		Stage.TriggerFramework.DrainEffectRequests(IgnoredRequests);
		Stage.TriggerFramework.DrainLifecycleFacts(IgnoredFacts);
	}

	bool TryStageActionStartFieldConditionDispatch(
		const FBattleEngineState& State,
		FActionStartMechanicsStage& Stage,
		const FConditionId& ConditionId,
		const EBattleTriggerPhase Phase,
		const TOptional<FActiveSlotId>& ActiveSlotId,
		bool& bOutActive)
	{
		bOutActive = false;
		if (!FBattleFieldSideConditionRules::IsFieldOwned(ConditionId))
		{
			return false;
		}
		const FBattleTriggerSubject Owner = FBattleTriggerSubject::CreateField();
		FBattleTriggerDispatchSpec Dispatch;
		Dispatch.Phase = Phase;
		for (const FBattleTriggerRegistrationState& Registration :
			Stage.TriggerFramework.GetActiveRegistrations())
		{
			if (Registration.Spec.Owner != Owner
				|| Registration.Spec.Rule.Phase != Phase
				|| Registration.Spec.SourceDefinition.Kind
					!= EBattleTriggerSourceDefinitionKind::Condition
				|| Registration.Spec.SourceDefinition.ConditionId != ConditionId
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
		if (!TryTakeActionStartTriggerOperationContext(Stage, Operation))
		{
			return false;
		}
		Dispatch.ReentrancyToken = Operation.ReentrancyToken;
		if (!Stage.TriggerFramework.TryEnqueueDispatch(Dispatch, Error)
			|| !Stage.TriggerFramework.TryResolveNextDispatch(Result, Error))
		{
			return false;
		}
		TArray<FBattleTriggerEffectRequest> Requests;
		TArray<FBattleTriggerLifecycleFact> Facts;
		Stage.TriggerFramework.DrainEffectRequests(Requests);
		Stage.TriggerFramework.DrainLifecycleFacts(Facts);
		if (Result.bQueuedExpiryDispatch)
		{
			FBattleTriggerDispatchResult ExpiryResult;
			if (!Stage.TriggerFramework.TryResolveNextDispatch(ExpiryResult, Error))
			{
				return false;
			}
			TArray<FBattleTriggerEffectRequest> ExpiryRequests;
			TArray<FBattleTriggerLifecycleFact> ExpiryFacts;
			Stage.TriggerFramework.DrainEffectRequests(ExpiryRequests);
			Stage.TriggerFramework.DrainLifecycleFacts(ExpiryFacts);
			Requests.Append(MoveTemp(ExpiryRequests));
			Facts.Append(MoveTemp(ExpiryFacts));
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

	bool TryStageActionStartItemCleanup(
		FActionStartMechanicsStage& Stage,
		const FItemId& ItemId,
		const FBattlerId BattlerId)
	{
		if (!FBattleItemRules::IsCanonical(ItemId))
		{
			return true;
		}
		FBattleTriggerSubject Owner;
		FBattleTriggerSourceDefinition SourceDefinition;
		FBattleTriggerOperationContext Operation;
		if (!TryMakeBattlerTriggerSubject(BattlerId, Owner)
			|| !FBattleTriggerSourceDefinition::TryCreateItem(ItemId, SourceDefinition)
			|| !TryTakeActionStartTriggerOperationContext(Stage, Operation))
		{
			return false;
		}

		FBattleTriggerCleanupRequest Cleanup;
		Cleanup.Reason = EBattleTriggerCleanupReason::Removal;
		Cleanup.AffectedOwners.Add(Owner);
		Cleanup.SourceDefinitionFilter = SourceDefinition;
		Cleanup.Context = Operation;
		EBattleTriggerError Error = EBattleTriggerError::None;
		if (!Stage.TriggerFramework.TryApplyCleanup(Cleanup, Error))
		{
			return false;
		}
		DrainActionStartTriggerOutputs(Stage);
		return true;
	}

	bool TryStageActionStartItemRegistration(
		const FBattleEngineState& State,
		FActionStartMechanicsStage& Stage,
		const FActionStartStagedHeldItem& Staged)
	{
		if (!FBattleItemRules::IsCanonical(Staged.HeldItem.CurrentItemId))
		{
			return true;
		}
		const bool bAlreadyRegistered =
			Stage.TriggerFramework.GetActiveRegistrations().ContainsByPredicate(
				[&Staged](const FBattleTriggerRegistrationState& Registration)
				{
					return Registration.Spec.Owner.Kind
							== EBattleTriggerSubjectKind::Battler
						&& Registration.Spec.Owner.BattlerId == Staged.BattlerId
						&& Registration.Spec.SourceDefinition.Kind
							== EBattleTriggerSourceDefinitionKind::Item
						&& Registration.Spec.SourceDefinition.ItemId
							== Staged.HeldItem.CurrentItemId;
				});
		if (bAlreadyRegistered
			&& !TryStageActionStartItemCleanup(
				Stage,
				Staged.HeldItem.CurrentItemId,
				Staged.BattlerId))
		{
			return false;
		}

		FBattleTriggerSubject Owner;
		if (FindActiveForBattler(State, Staged.BattlerId) == nullptr
			|| !TryMakeBattlerTriggerSubject(Staged.BattlerId, Owner))
		{
			return false;
		}
		FBattleItemRegistrationFacts Facts;
		Facts.ItemId = Staged.HeldItem.CurrentItemId;
		Facts.Owner = Owner;
		Facts.Source = Owner;
		Facts.Targets.Add(Owner);
		Facts.bSuppressed = Staged.HeldItem.bSuppressed;
		EBattleAbilityItemHookError Error = EBattleAbilityItemHookError::None;
		if (!FBattleItemRules::TryRegisterHooks(Stage.TriggerFramework, Facts, Error))
		{
			return false;
		}
		DrainActionStartTriggerOutputs(Stage);
		return true;
	}

	bool TryStageAllActionStartHeldItemsSuppressed(
		const FBattleEngineState& State,
		FActionStartMechanicsStage& Stage,
		const bool bSuppressed)
	{
		for (FActionStartStagedHeldItem& Staged : Stage.HeldItems)
		{
			const bool bActive = FindActiveForBattler(State, Staged.BattlerId) != nullptr;
			if (Staged.HeldItem.bSuppressed == bSuppressed)
			{
				continue;
			}
			if (bActive
				&& !TryStageActionStartItemCleanup(
					Stage,
					Staged.HeldItem.CurrentItemId,
					Staged.BattlerId))
			{
				return false;
			}

			FBattleHeldItemOperationRequest Request;
			Request.Kind = EBattleHeldItemOperationKind::Suppress;
			Request.PrimaryInstanceId = Staged.HeldItem.InstanceId;
			Request.bSuppressed = bSuppressed;
			FBattleHeldItemOperationFact Fact;
			EBattleHeldItemContractError Error = EBattleHeldItemContractError::None;
			if (!Stage.HeldItemLedger.TryApplyOperation(Request, Fact, Error))
			{
				return false;
			}
			Staged.HeldItem.CurrentItemId = Fact.PrimaryAfter.CurrentItemId;
			Staged.HeldItem.bConsumed = Fact.PrimaryAfter.bConsumed;
			Staged.HeldItem.bSuppressed = Fact.PrimaryAfter.bSuppressed;
			Staged.HeldItem.bRevealed = Fact.PrimaryAfter.bRevealed;
			Staged.HeldItem.bTemporarilyRemoved =
				Fact.PrimaryAfter.bTemporarilyRemoved;
			if (bSuppressed)
			{
				Staged.HeldItem.ChoiceLockedMoveId = FMoveId();
			}
			if (bActive
				&& !TryStageActionStartItemRegistration(State, Stage, Staged))
			{
				return false;
			}
		}
		return true;
	}

	bool TryStageActionStartVolatileCleanup(
		FActionStartMechanicsStage& Stage,
		const FConditionId& VolatileId)
	{
		if (!FBattleVolatileRules::IsCanonical(VolatileId))
		{
			return true;
		}
		FBattleTriggerSubject Owner;
		FBattleTriggerOperationContext Operation;
		EBattleTriggerError Error = EBattleTriggerError::None;
		if (!TryMakeBattlerTriggerSubject(Stage.ActorId, Owner)
			|| !TryTakeActionStartTriggerOperationContext(Stage, Operation)
			|| !FBattleVolatileRules::TryCleanupTriggers(
				Stage.TriggerFramework,
				VolatileId,
				Owner,
				EBattleTriggerCleanupReason::Removal,
				Operation,
				Error))
		{
			return false;
		}
		DrainActionStartTriggerOutputs(Stage);
		return true;
	}

	bool TryStageRechargeDenial(
		const FBattleEngineState& State,
		FActionStartMechanicsStage& Stage)
	{
		const FConditionId RechargeId = FBattleVolatileRules::GetRechargeId();
		if (!Stage.HasActorVolatile(RechargeId))
		{
			return false;
		}
		FBattleTriggerSubject Owner;
		if (!TryMakeBattlerTriggerSubject(Stage.ActorId, Owner))
		{
			return false;
		}

		FBattleTriggerDispatchSpec Dispatch;
		Dispatch.Phase = EBattleTriggerPhase::BeforeAction;
		for (const FBattleTriggerRegistrationState& Registration :
			Stage.TriggerFramework.GetActiveRegistrations())
		{
			if (Registration.Spec.Owner != Owner
				|| Registration.Spec.Rule.Phase != EBattleTriggerPhase::BeforeAction
				|| Registration.Spec.SourceDefinition.Kind
					!= EBattleTriggerSourceDefinitionKind::Condition
				|| Registration.Spec.SourceDefinition.ConditionId != RechargeId)
			{
				continue;
			}
			FBattleTriggerDispatchParticipant& Participant =
				Dispatch.Participants.AddDefaulted_GetRef();
			Participant.RegistrationId = Registration.RegistrationId;
			const FBattleActivePositionState* Active =
				FindActiveForBattler(State, Stage.ActorId);
			if (Active != nullptr)
			{
				Participant.ActiveSlotId = Active->ActiveSlotId;
			}
		}
		if (Dispatch.Participants.IsEmpty())
		{
			return false;
		}

		FBattleTriggerOperationContext Operation;
		FBattleTriggerDispatchResult DispatchResult;
		EBattleTriggerError TriggerError = EBattleTriggerError::None;
		if (!TryTakeActionStartTriggerOperationContext(Stage, Operation))
		{
			return false;
		}
		Dispatch.ReentrancyToken = Operation.ReentrancyToken;
		if (!Stage.TriggerFramework.TryEnqueueDispatch(Dispatch, TriggerError)
			|| !Stage.TriggerFramework.TryResolveNextDispatch(
				DispatchResult,
				TriggerError))
		{
			return false;
		}
		TArray<FBattleTriggerEffectRequest> Requests;
		TArray<FBattleTriggerLifecycleFact> Facts;
		Stage.TriggerFramework.DrainEffectRequests(Requests);
		Stage.TriggerFramework.DrainLifecycleFacts(Facts);
		if (DispatchResult.bQueuedExpiryDispatch)
		{
			FBattleTriggerDispatchResult ExpiryResult;
			if (!Stage.TriggerFramework.TryResolveNextDispatch(
					ExpiryResult,
					TriggerError))
			{
				return false;
			}
			TArray<FBattleTriggerEffectRequest> ExpiryRequests;
			TArray<FBattleTriggerLifecycleFact> ExpiryFacts;
			Stage.TriggerFramework.DrainEffectRequests(ExpiryRequests);
			Stage.TriggerFramework.DrainLifecycleFacts(ExpiryFacts);
			Requests.Append(MoveTemp(ExpiryRequests));
			Facts.Append(MoveTemp(ExpiryFacts));
		}

		FBattleVolatileActionResult Gate;
		if (Requests.Num() != 1
			|| !Facts.IsEmpty()
			|| Requests[0].SourceDefinition.Kind
				!= EBattleTriggerSourceDefinitionKind::Condition
			|| Requests[0].SourceDefinition.ConditionId != RechargeId
			|| !FBattleVolatileRules::TryResolveSimpleBeforeAction(RechargeId, Gate)
			|| Gate.Outcome != EBattleVolatileActionOutcome::Denied
			|| !Gate.bRemoveVolatile
			|| !TryStageActionStartVolatileCleanup(Stage, RechargeId)
			|| Stage.ActorVolatiles.RemoveAll(
				[RechargeId](const FBattleConditionState& Condition)
				{
					return Condition.ConditionId == RechargeId;
				}) != 1)
		{
			return false;
		}
		return true;
	}

	bool TryStageClearActionStartChargeState(FActionStartMechanicsStage& Stage)
	{
		for (const FConditionId& Id : {
			FBattleVolatileRules::GetChargingId(),
			FBattleVolatileRules::GetFlySemiInvulnerableId()})
		{
			if (!Stage.HasActorVolatile(Id))
			{
				continue;
			}
			if (!TryStageActionStartVolatileCleanup(Stage, Id))
			{
				return false;
			}
			Stage.ActorVolatiles.RemoveAll(
				[&Id](const FBattleConditionState& Condition)
				{
					return Condition.ConditionId == Id;
				});
		}
		return true;
	}

	struct FActionStartStateDelta
	{
		FTrainerId ActingTrainerId;
		FBattleTrainerActionAllowance TrainerAllowance;
		bool bApplyMechanicsStage = false;
		FActionStartMechanicsStage MechanicsStage;
		bool bStarted = false;
		bool bMoveCommitted = false;
		TOptional<FBattleTargetResolutionResult> TargetResolution;
		EBattleLockedEffectExecutionState EffectExecutionState =
			EBattleLockedEffectExecutionState::Pending;
		bool bFinished = false;
		int32 NextLockedActionIndex = INDEX_NONE;
		EBattlePhase Phase = EBattlePhase::Resolving;
		EBattleOutcome Outcome = EBattleOutcome::InProgress;
		EBattleOutcomeCause OutcomeCause = EBattleOutcomeCause::None;
		TOptional<FBattleDecisionRequest> PendingDecision;
		TArray<FBattleDecisionRequest> PendingDecisionRequests;
		TArray<FBattlePendingReplacementState> PendingReplacements;
	};

	void InitializeActionStartDelta(
		const FBattleEngineState& State,
		const FBattleLockedActionState& Action,
		const FBattleTrainerState& Trainer,
		FActionStartStateDelta& OutDelta)
	{
		OutDelta = FActionStartStateDelta();
		OutDelta.ActingTrainerId = Trainer.TrainerId;
		OutDelta.TrainerAllowance = Trainer.ActionAllowance;
		OutDelta.bStarted = Action.bStarted;
		OutDelta.bMoveCommitted = Action.bMoveCommitted;
		OutDelta.TargetResolution = Action.TargetResolution;
		OutDelta.EffectExecutionState = Action.EffectExecutionState;
		OutDelta.bFinished = Action.bFinished;
		OutDelta.NextLockedActionIndex = State.CurrentLockedActionIndex;
		OutDelta.Phase = EBattlePhase::Resolving;
		OutDelta.Outcome = State.Outcome;
		OutDelta.OutcomeCause = State.OutcomeCause;
		OutDelta.PendingDecision = State.PendingDecision;
		OutDelta.PendingDecisionRequests = State.PendingDecisionRequests;
		OutDelta.PendingReplacements = State.PendingReplacements;
	}

	bool TryPrepareActionStartBoundary(
		const FBattleEngineState& State,
		const uint64 RequestStateVersion,
		FActionStartStateDelta& Delta,
		TArray<FBattleReplacementRequirement>& OutRequirements)
	{
		OutRequirements.Reset();
		if (RequestStateVersion == 0
			|| Delta.NextLockedActionIndex <= State.CurrentLockedActionIndex
			|| Delta.NextLockedActionIndex > State.LockedActions.Num())
		{
			return false;
		}

		FBattleEngineState Projection;
		Projection.Setup = State.Setup;
		Projection.Catalog = State.Catalog;
		Projection.bHasCatalog = State.bHasCatalog;
		Projection.StateVersion = State.StateVersion;
		Projection.TurnId = State.TurnId;
		Projection.EncounterKind = State.EncounterKind;
		Projection.Format = State.Format;
		Projection.Phase = EBattlePhase::Resolving;
		Projection.Outcome = Delta.Outcome;
		Projection.OutcomeCause = Delta.OutcomeCause;
		Projection.Trainers = State.Trainers;
		Projection.Battlers = State.Battlers;
		Projection.ActivePositions = State.ActivePositions;
		Projection.CompiledEncounterPolicies = State.CompiledEncounterPolicies;
		Projection.LockedActions = State.LockedActions;
		Projection.CurrentLockedActionIndex = Delta.NextLockedActionIndex;
		Projection.PendingDecision = Delta.PendingDecision;
		Projection.PendingDecisionRequests = Delta.PendingDecisionRequests;
		Projection.PendingReplacements = Delta.PendingReplacements;

		FBattleFaintOutcomeResolver::ResolveQueueBoundary(
			Projection,
			OutRequirements);
		Delta.Phase = Projection.Phase;
		Delta.Outcome = Projection.Outcome;
		Delta.OutcomeCause = Projection.OutcomeCause;
		if (Projection.Phase == EBattlePhase::MandatoryReplacement)
		{
			Projection.PendingReplacements.Reset();
			for (const FBattleReplacementRequirement& Requirement : OutRequirements)
			{
				FBattlePendingReplacementState& Pending =
					Projection.PendingReplacements.AddDefaulted_GetRef();
				Pending.TrainerId = Requirement.Target.TrainerId;
				Pending.ActiveSlotId = Requirement.Target.ActiveSlotId;
			}
			TArray<FBattleDecisionRequest> Requests;
			if (!TryBuildReplacementCheckpointRequests(
					Projection,
					RequestStateVersion,
					true,
					Requests)
				|| Requests.IsEmpty())
			{
				return false;
			}
			Delta.PendingReplacements = Projection.PendingReplacements;
			Delta.PendingDecisionRequests = MoveTemp(Requests);
			Delta.PendingDecision = Delta.PendingDecisionRequests[0];
		}
		else if (Projection.Phase == EBattlePhase::EndOfTurn)
		{
			Delta.PendingReplacements.Reset();
			Delta.PendingDecisionRequests.Reset();
			Delta.PendingDecision.Reset();
		}
		else if (Projection.Phase != EBattlePhase::Resolving)
		{
			return false;
		}
		return true;
	}

	FBattleEventSpec MakeStagedActionStartEventSpec(
		const FBattleEngineState& State,
		const FResolutionId ResolutionId,
		const FActionId ActionId,
		const EBattleActionKind ActionKind,
		const FBattleEventSource& Source,
		const EBattleEventType Type,
		const EBattleEventCause Cause,
		const TOptional<int64> NumericBefore = TOptional<int64>(),
		const TOptional<int64> NumericAfter = TOptional<int64>(),
		const TOptional<int64> NumericDelta = TOptional<int64>(),
		const EBattleVisibilityLevel Visibility = EBattleVisibilityLevel::Public,
		const FBattleEventTarget* Target = nullptr)
	{
		FBattleEventSpec Spec;
		Spec.BattleId = State.Setup.GetBattleId();
		Spec.TurnId = State.TurnId;
		Spec.ActionId = ActionId;
		Spec.ResolutionId = ResolutionId;
		Spec.Type = Type;
		Spec.Cause = Cause;
		Spec.CauseActionKind = ActionKind;
		Spec.Source = Source;
		Spec.NumericBefore = NumericBefore;
		Spec.NumericAfter = NumericAfter;
		Spec.NumericDelta = NumericDelta;
		Spec.Visibility.Level = Visibility;
		if (Target != nullptr)
		{
			Spec.Targets.Add(*Target);
		}
		return Spec;
	}

	FBattleResolution PublishActionStartCheckpointRejection(
		FBattleEngineState& State,
		const FResolutionId ResolutionId,
		const FActionId ActionId,
		const EBattleRejectionReason Reason,
		const FTrainerId TrainerId,
		const FBattlerId BattlerId,
		const EBattleActionKind ActionKind,
		const FBattleEventSource& Source)
	{
		FBattleResolutionCommitPlan RejectedPlan;
		const bool bPrepared = FBattleResolutionCommit::TryBuildRejectedPlan(
			State,
			ResolutionId,
			ActionId,
			Reason,
			TrainerId,
			BattlerId,
			ActionKind,
			Source,
			RejectedPlan);
		check(bPrepared);
		const FBattleResolution Resolution = FBattleResolutionCommit::PublishPrepared(
			State,
			RejectedPlan);
		EBattleStateValidationError StateError = EBattleStateValidationError::None;
		const bool bStateValid = State.ValidateInvariants(StateError);
		check(bStateValid);
		return Resolution;
	}

	void ApplyActionStartDelta(
		FBattleEngineState& State,
		const FActionStartCheckpointIdentity& Identity,
		const FActionStartStateDelta& Delta)
	{
		const int32 ActionIndex = Identity.CommitIdentity.ExpectedLockedActionIndex;
		check(State.LockedActions.IsValidIndex(ActionIndex));
		FBattleLockedActionState& Action = State.LockedActions[ActionIndex];
		FBattleTrainerState* Trainer = State.FindMutableTrainer(Delta.ActingTrainerId);
		FBattleBattlerState* Actor = Delta.bApplyMechanicsStage
			? State.FindMutableBattler(Delta.MechanicsStage.ActorId)
			: nullptr;
		check(Action.ActionId == Identity.CommitIdentity.OwningActionId
			&& Trainer != nullptr
			&& (!Delta.bApplyMechanicsStage || Actor != nullptr));
		if (Delta.bApplyMechanicsStage)
		{
			for (const FActionStartStagedHeldItem& Staged :
				Delta.MechanicsStage.HeldItems)
			{
				const FBattleBattlerState* Battler = State.FindBattler(Staged.BattlerId);
				check(Battler != nullptr
					&& Battler->HeldItem.InstanceId == Staged.HeldItem.InstanceId);
			}
		}

		Trainer->ActionAllowance = Delta.TrainerAllowance;
		if (Delta.bApplyMechanicsStage)
		{
			State.TriggerFramework = Delta.MechanicsStage.TriggerFramework;
			State.NextTriggerReentrancyToken =
				Delta.MechanicsStage.NextTriggerReentrancyToken;
			State.HeldItemLedger = Delta.MechanicsStage.HeldItemLedger;
			for (const FActionStartStagedHeldItem& Staged :
				Delta.MechanicsStage.HeldItems)
			{
				FBattleBattlerState* Battler = State.FindMutableBattler(Staged.BattlerId);
				check(Battler != nullptr);
				Battler->HeldItem = Staged.HeldItem;
			}
			check(Actor != nullptr);
			Actor->Volatiles = Delta.MechanicsStage.ActorVolatiles;
		}
		Action.bStarted = Delta.bStarted;
		Action.bMoveCommitted = Delta.bMoveCommitted;
		Action.TargetResolution = Delta.TargetResolution;
		Action.EffectExecutionState = Delta.EffectExecutionState;
		Action.bFinished = Delta.bFinished;
		State.CurrentLockedActionIndex = Delta.NextLockedActionIndex;
		State.Phase = Delta.Phase;
		State.Outcome = Delta.Outcome;
		State.OutcomeCause = Delta.OutcomeCause;
		State.PendingDecision = Delta.PendingDecision;
		State.PendingDecisionRequests = Delta.PendingDecisionRequests;
		State.PendingReplacements = Delta.PendingReplacements;
	}

	struct FVoluntarySwitchBattlerIdentity
	{
		FTrainerId TrainerId;
		FBattlerId BattlerId;
		FSourcePokemonId SourcePokemonId;
		FPartySlotId PartySlotId;
		FBattleHeldItemInstanceId HeldItemInstanceId;
		FItemId CurrentHeldItemId;
		FAbilityId AbilityId;
		FConditionId MajorStatusId;
		FMoveId LastMoveId;
		FTurnId EnteredActiveOnTurnId;
		int32 CurrentHP = 0;
		int32 VolatileCount = 0;
		bool bFainted = false;
		bool bCaptured = false;
		bool bRemoved = false;
		bool bFaintTransitionPending = false;
		bool bEgg = false;
		bool bAbilitySuppressed = false;
	};

	struct FVoluntarySwitchActiveIdentity
	{
		FActiveSlotId ActiveSlotId;
		FTrainerId TrainerId;
		FBattlerId BattlerId;
		bool bAvailable = false;
	};

	/** Exact caller-serialized identity for one already-started voluntary Switch action. */
	struct FVoluntarySwitchCheckpointIdentity
	{
		FBattleResolutionCommitIdentity CommitIdentity;
		int32 ExpectedLockedActionCount = 0;
		int32 ExpectedEventCount = 0;
		int32 ExpectedTrainerCount = 0;
		int32 ExpectedPendingDecisionRequestCount = 0;
		int32 ExpectedPendingReplacementCount = 0;
		int32 ExpectedOpponentRemovalCheckpointCount = 0;
		int32 ExpectedPendingTriggerDispatchCount = 0;
		int32 ExpectedPendingTriggerEffectCount = 0;
		int32 ExpectedPendingTriggerLifecycleCount = 0;
		uint64 ExpectedNextConditionCreationOrdinal = 0;
		uint64 ExpectedNextTriggerReentrancyToken = 0;
		uint64 ExpectedQueueOrdinal = 0;
		FTrainerId ExpectedDecisionOwnerTrainerId;
		FBattlerId ExpectedOutgoingBattlerId;
		FBattlerId ExpectedIncomingBattlerId;
		FPartySlotId ExpectedIncomingPartySlotId;
		FActiveSlotId ExpectedActingSlotId;
		FMoveId ExpectedMoveId;
		FItemId ExpectedItemId;
		FPartySlotId ExpectedItemPartyTargetId;
		FActiveSlotId ExpectedActiveTargetId;
		EBattleTargetClass ExpectedTargetClass = EBattleTargetClass::SelectedOpponent;
		FBattlerId ExpectedSelectedTargetBattlerId;
		bool bExpectedMoveCommitted = false;
		bool bExpectedTargetResolution = false;
		EBattleLockedEffectExecutionState ExpectedEffectExecutionState =
			EBattleLockedEffectExecutionState::Pending;
		TArray<FVoluntarySwitchBattlerIdentity> Battlers;
		TArray<FVoluntarySwitchActiveIdentity> ActivePositions;
		TArray<FBattleHeldItemInstanceId> HeldItemInstances;
		TArray<FBattleTriggerRegistrationId> TriggerRegistrations;
	};

	FVoluntarySwitchBattlerIdentity MakeVoluntarySwitchBattlerIdentity(
		const FBattleBattlerState& Battler)
	{
		FVoluntarySwitchBattlerIdentity Identity;
		Identity.TrainerId = Battler.TrainerId;
		Identity.BattlerId = Battler.BattlerId;
		Identity.SourcePokemonId = Battler.SourcePokemonId;
		Identity.PartySlotId = Battler.PartySlotId;
		Identity.HeldItemInstanceId = Battler.HeldItem.InstanceId;
		Identity.CurrentHeldItemId = Battler.HeldItem.CurrentItemId;
		Identity.AbilityId = Battler.AbilityId;
		Identity.MajorStatusId = Battler.MajorStatusId;
		Identity.LastMoveId = Battler.LastMoveId;
		Identity.EnteredActiveOnTurnId = Battler.EnteredActiveOnTurnId;
		Identity.CurrentHP = Battler.CurrentHP;
		Identity.VolatileCount = Battler.Volatiles.Num();
		Identity.bFainted = Battler.bFainted;
		Identity.bCaptured = Battler.bCaptured;
		Identity.bRemoved = Battler.bRemoved;
		Identity.bFaintTransitionPending = Battler.bFaintTransitionPending;
		Identity.bEgg = Battler.bEgg;
		Identity.bAbilitySuppressed = Battler.bAbilitySuppressed;
		return Identity;
	}

	bool MatchesVoluntarySwitchBattlerIdentity(
		const FBattleBattlerState& Battler,
		const FVoluntarySwitchBattlerIdentity& Identity)
	{
		return Battler.TrainerId == Identity.TrainerId
			&& Battler.BattlerId == Identity.BattlerId
			&& Battler.SourcePokemonId == Identity.SourcePokemonId
			&& Battler.PartySlotId == Identity.PartySlotId
			&& Battler.HeldItem.InstanceId == Identity.HeldItemInstanceId
			&& Battler.HeldItem.CurrentItemId == Identity.CurrentHeldItemId
			&& Battler.AbilityId == Identity.AbilityId
			&& Battler.MajorStatusId == Identity.MajorStatusId
			&& Battler.LastMoveId == Identity.LastMoveId
			&& Battler.EnteredActiveOnTurnId == Identity.EnteredActiveOnTurnId
			&& Battler.CurrentHP == Identity.CurrentHP
			&& Battler.Volatiles.Num() == Identity.VolatileCount
			&& Battler.bFainted == Identity.bFainted
			&& Battler.bCaptured == Identity.bCaptured
			&& Battler.bRemoved == Identity.bRemoved
			&& Battler.bFaintTransitionPending == Identity.bFaintTransitionPending
			&& Battler.bEgg == Identity.bEgg
			&& Battler.bAbilitySuppressed == Identity.bAbilitySuppressed;
	}

	bool TryCaptureVoluntarySwitchCheckpointIdentity(
		const FBattleEngineState& State,
		const FResolutionId ResolutionId,
		const FBattleLockedActionState& Action,
		FVoluntarySwitchCheckpointIdentity& OutIdentity)
	{
		OutIdentity = FVoluntarySwitchCheckpointIdentity();
		FBattleResolutionCommitIdentity CommitIdentity;
		if (Action.Decision.GetActionKind() != EBattleActionKind::Switch
			|| !Action.bStarted
			|| Action.bFinished
			|| !FBattleResolutionCommit::TryCaptureIdentity(
				State,
				ResolutionId,
				Action.ActionId,
				CommitIdentity))
		{
			return false;
		}

		const FTrainerId TrainerId = Action.Decision.GetDecisionOwnerTrainerId();
		const FBattlerId OutgoingBattlerId = Action.Decision.GetActingBattlerId();
		const FPartySlotId IncomingPartySlotId = Action.Decision.GetSwitchPartySlotId();
		const FBattleTrainerState* Trainer = State.FindTrainer(TrainerId);
		const FBattleBattlerState* Outgoing = State.FindBattler(OutgoingBattlerId);
		const FBattleActivePositionState* Active = State.FindActivePosition(
			Action.OrderKey.ActingSlotId);
		const FBattlePartySlotState* IncomingPartySlot = Trainer != nullptr
			? Trainer->PartySlots.FindByPredicate(
				[IncomingPartySlotId](const FBattlePartySlotState& Candidate)
				{
					return Candidate.PartySlotId == IncomingPartySlotId;
				})
			: nullptr;
		const FBattleBattlerState* Incoming = IncomingPartySlot != nullptr
			? State.FindBattler(IncomingPartySlot->BattlerId)
			: nullptr;
		if (Trainer == nullptr
			|| Outgoing == nullptr
			|| Active == nullptr
			|| Incoming == nullptr
			|| !Active->bAvailable
			|| Active->TrainerId != TrainerId
			|| Active->BattlerId != OutgoingBattlerId
			|| Outgoing->TrainerId != TrainerId
			|| Incoming->TrainerId != TrainerId
			|| Incoming->PartySlotId != IncomingPartySlotId)
		{
			return false;
		}

		OutIdentity.CommitIdentity = CommitIdentity;
		OutIdentity.ExpectedLockedActionCount = State.LockedActions.Num();
		OutIdentity.ExpectedEventCount = State.OrderedEvents.Num();
		OutIdentity.ExpectedTrainerCount = State.Trainers.Num();
		OutIdentity.ExpectedPendingDecisionRequestCount = State.PendingDecisionRequests.Num();
		OutIdentity.ExpectedPendingReplacementCount = State.PendingReplacements.Num();
		OutIdentity.ExpectedOpponentRemovalCheckpointCount =
			State.AvailableOpponentRemovalCheckpoints.Num();
		OutIdentity.ExpectedPendingTriggerDispatchCount =
			State.TriggerFramework.GetPendingDispatchCount();
		OutIdentity.ExpectedPendingTriggerEffectCount =
			State.TriggerFramework.GetPendingEffectRequestCount();
		OutIdentity.ExpectedPendingTriggerLifecycleCount =
			State.TriggerFramework.GetPendingLifecycleFactCount();
		OutIdentity.ExpectedNextConditionCreationOrdinal =
			State.NextConditionCreationOrdinal;
		OutIdentity.ExpectedNextTriggerReentrancyToken =
			State.NextTriggerReentrancyToken;
		OutIdentity.ExpectedQueueOrdinal = Action.QueueOrdinal;
		OutIdentity.ExpectedDecisionOwnerTrainerId = TrainerId;
		OutIdentity.ExpectedOutgoingBattlerId = OutgoingBattlerId;
		OutIdentity.ExpectedIncomingBattlerId = Incoming->BattlerId;
		OutIdentity.ExpectedIncomingPartySlotId = IncomingPartySlotId;
		OutIdentity.ExpectedActingSlotId = Action.OrderKey.ActingSlotId;
		OutIdentity.ExpectedMoveId = Action.Decision.GetMoveId();
		OutIdentity.ExpectedItemId = Action.Decision.GetItemId();
		OutIdentity.ExpectedItemPartyTargetId = Action.Decision.GetItemPartyTargetId();
		OutIdentity.ExpectedActiveTargetId = Action.Decision.GetActiveTargetId();
		OutIdentity.ExpectedTargetClass = Action.TargetClass;
		OutIdentity.ExpectedSelectedTargetBattlerId = Action.SelectedTargetBattlerId;
		OutIdentity.bExpectedMoveCommitted = Action.bMoveCommitted;
		OutIdentity.bExpectedTargetResolution = Action.TargetResolution.IsSet();
		OutIdentity.ExpectedEffectExecutionState = Action.EffectExecutionState;

		OutIdentity.Battlers.Reserve(State.Battlers.Num());
		for (const FBattleBattlerState& Battler : State.Battlers)
		{
			OutIdentity.Battlers.Add(MakeVoluntarySwitchBattlerIdentity(Battler));
		}
		OutIdentity.ActivePositions.Reserve(State.ActivePositions.Num());
		for (const FBattleActivePositionState& Position : State.ActivePositions)
		{
			FVoluntarySwitchActiveIdentity& Identity =
				OutIdentity.ActivePositions.AddDefaulted_GetRef();
			Identity.ActiveSlotId = Position.ActiveSlotId;
			Identity.TrainerId = Position.TrainerId;
			Identity.BattlerId = Position.BattlerId;
			Identity.bAvailable = Position.bAvailable;
		}
		for (const FBattleHeldItemInstanceState& Item : State.HeldItemLedger.GetStates())
		{
			OutIdentity.HeldItemInstances.Add(Item.InstanceId);
		}
		for (const FBattleTriggerRegistrationState& Registration :
			State.TriggerFramework.GetActiveRegistrations())
		{
			OutIdentity.TriggerRegistrations.Add(Registration.RegistrationId);
		}
		return true;
	}

	bool IsVoluntarySwitchCheckpointIdentityCurrent(
		const FBattleEngineState& State,
		const FVoluntarySwitchCheckpointIdentity& Identity)
	{
		// Read first so a caller-serialized identity seam cannot mutate state after
		// the state-version comparison has already passed.
		const int32 RandomTraceCount = State.Random.IsValid()
			? State.Random->GetTrace().Num()
			: INDEX_NONE;
		const FBattleResolutionCommitIdentity& Commit = Identity.CommitIdentity;
		if (!Commit.ResolutionId.IsValid()
			|| !Commit.OwningActionId.IsValid()
			|| State.Phase != EBattlePhase::Resolving
			|| State.StateVersion != Commit.ExpectedStateVersion
			|| State.CurrentLockedActionIndex != Commit.ExpectedLockedActionIndex
			|| State.LockedActions.Num() != Identity.ExpectedLockedActionCount
			|| State.OrderedEvents.Num() != Identity.ExpectedEventCount
			|| State.Trainers.Num() != Identity.ExpectedTrainerCount
			|| State.NextResolutionId != Commit.ExpectedNextResolutionId
			|| State.NextEventOrdinal != Commit.ExpectedEventOrdinal
			|| State.NextConditionCreationOrdinal
				!= Identity.ExpectedNextConditionCreationOrdinal
			|| State.NextTriggerReentrancyToken
				!= Identity.ExpectedNextTriggerReentrancyToken
			|| State.Resolutions.Num() != Commit.ExpectedResolutionCount
			|| State.PendingDecisionRequests.Num()
				!= Identity.ExpectedPendingDecisionRequestCount
			|| State.PendingReplacements.Num() != Identity.ExpectedPendingReplacementCount
			|| State.AvailableOpponentRemovalCheckpoints.Num()
				!= Identity.ExpectedOpponentRemovalCheckpointCount
			|| State.TriggerFramework.GetPendingDispatchCount()
				!= Identity.ExpectedPendingTriggerDispatchCount
			|| State.TriggerFramework.GetPendingEffectRequestCount()
				!= Identity.ExpectedPendingTriggerEffectCount
			|| State.TriggerFramework.GetPendingLifecycleFactCount()
				!= Identity.ExpectedPendingTriggerLifecycleCount
			|| RandomTraceCount != Commit.ExpectedRandomTraceCount
			|| State.Battlers.Num() != Identity.Battlers.Num()
			|| State.ActivePositions.Num() != Identity.ActivePositions.Num()
			|| State.HeldItemLedger.GetStates().Num() != Identity.HeldItemInstances.Num()
			|| State.TriggerFramework.GetActiveRegistrations().Num()
				!= Identity.TriggerRegistrations.Num()
			|| !State.LockedActions.IsValidIndex(Commit.ExpectedLockedActionIndex))
		{
			return false;
		}

		const FBattleLockedActionState& Action =
			State.LockedActions[Commit.ExpectedLockedActionIndex];
		if (Action.ActionId != Commit.OwningActionId
			|| Action.QueueOrdinal != Identity.ExpectedQueueOrdinal
			|| Action.Decision.GetDecisionOwnerTrainerId()
				!= Identity.ExpectedDecisionOwnerTrainerId
			|| Action.Decision.GetActingBattlerId()
				!= Identity.ExpectedOutgoingBattlerId
			|| Action.Decision.GetSwitchPartySlotId()
				!= Identity.ExpectedIncomingPartySlotId
			|| Action.Decision.GetActionKind() != EBattleActionKind::Switch
			|| Action.Decision.GetMoveId() != Identity.ExpectedMoveId
			|| Action.Decision.GetItemId() != Identity.ExpectedItemId
			|| Action.Decision.GetItemPartyTargetId()
				!= Identity.ExpectedItemPartyTargetId
			|| Action.Decision.GetActiveTargetId() != Identity.ExpectedActiveTargetId
			|| Action.OrderKey.ActingSlotId != Identity.ExpectedActingSlotId
			|| Action.TargetClass != Identity.ExpectedTargetClass
			|| Action.SelectedTargetBattlerId
				!= Identity.ExpectedSelectedTargetBattlerId
			|| !Action.bStarted
			|| Action.bMoveCommitted != Identity.bExpectedMoveCommitted
			|| Action.TargetResolution.IsSet() != Identity.bExpectedTargetResolution
			|| Action.EffectExecutionState != Identity.ExpectedEffectExecutionState
			|| Action.bFinished)
		{
			return false;
		}

		for (const FVoluntarySwitchBattlerIdentity& Expected : Identity.Battlers)
		{
			const FBattleBattlerState* Battler = State.FindBattler(Expected.BattlerId);
			if (Battler == nullptr || !MatchesVoluntarySwitchBattlerIdentity(*Battler, Expected))
			{
				return false;
			}
		}
		for (const FVoluntarySwitchActiveIdentity& Expected : Identity.ActivePositions)
		{
			const FBattleActivePositionState* Position =
				State.FindActivePosition(Expected.ActiveSlotId);
			if (Position == nullptr
				|| Position->bAvailable != Expected.bAvailable
				|| Position->TrainerId != Expected.TrainerId
				|| Position->BattlerId != Expected.BattlerId)
			{
				return false;
			}
		}
		for (const FBattleHeldItemInstanceId InstanceId : Identity.HeldItemInstances)
		{
			if (State.HeldItemLedger.FindState(InstanceId) == nullptr)
			{
				return false;
			}
		}
		for (const FBattleTriggerRegistrationId RegistrationId : Identity.TriggerRegistrations)
		{
			if (!State.TriggerFramework.GetActiveRegistrations().ContainsByPredicate(
					[RegistrationId](const FBattleTriggerRegistrationState& Candidate)
					{
						return Candidate.RegistrationId == RegistrationId;
					}))
			{
				return false;
			}
		}

		const FBattleActivePositionState* Active =
			State.FindActivePosition(Identity.ExpectedActingSlotId);
		const FBattleBattlerState* Outgoing =
			State.FindBattler(Identity.ExpectedOutgoingBattlerId);
		const FBattleBattlerState* Incoming =
			State.FindBattler(Identity.ExpectedIncomingBattlerId);
		return Active != nullptr
			&& Outgoing != nullptr
			&& Incoming != nullptr
			&& Active->TrainerId == Identity.ExpectedDecisionOwnerTrainerId
			&& Active->BattlerId == Identity.ExpectedOutgoingBattlerId
			&& Outgoing->TrainerId == Identity.ExpectedDecisionOwnerTrainerId
			&& Incoming->TrainerId == Identity.ExpectedDecisionOwnerTrainerId
			&& Incoming->PartySlotId == Identity.ExpectedIncomingPartySlotId;
	}

	template <typename ElementType, typename EqualType>
	bool AreOrderedPivotIdentityValuesEqual(
		const TConstArrayView<ElementType> Left,
		const TConstArrayView<ElementType> Right,
		EqualType Equal)
	{
		if (Left.Num() != Right.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < Left.Num(); ++Index)
		{
			if (!Equal(Left[Index], Right[Index]))
			{
				return false;
			}
		}
		return true;
	}

	bool ArePivotDecisionsIdentical(
		const FBattleDecision& Left,
		const FBattleDecision& Right)
	{
		return Left.IsValid() == Right.IsValid()
			&& Left.GetStateVersion() == Right.GetStateVersion()
			&& Left.GetRequestKind() == Right.GetRequestKind()
			&& Left.GetDecisionOwnerTrainerId() == Right.GetDecisionOwnerTrainerId()
			&& Left.GetActingBattlerId() == Right.GetActingBattlerId()
			&& Left.GetActionKind() == Right.GetActionKind()
			&& Left.GetMoveId() == Right.GetMoveId()
			&& Left.GetSwitchPartySlotId() == Right.GetSwitchPartySlotId()
			&& Left.GetItemId() == Right.GetItemId()
			&& Left.GetItemPartyTargetId() == Right.GetItemPartyTargetId()
			&& Left.GetActiveTargetId() == Right.GetActiveTargetId();
	}

	bool ArePivotDecisionRequestsIdentical(
		const FBattleDecisionRequest& Left,
		const FBattleDecisionRequest& Right)
	{
		return Left.IsValid() == Right.IsValid()
			&& Left.GetStateVersion() == Right.GetStateVersion()
			&& Left.GetRequestKind() == Right.GetRequestKind()
			&& Left.GetDecisionOwnerTrainerId() == Right.GetDecisionOwnerTrainerId()
			&& Left.GetActingBattlerId() == Right.GetActingBattlerId()
			&& Left.GetActingSlotId() == Right.GetActingSlotId()
			&& AreOrderedPivotIdentityValuesEqual(
				Left.GetLegalActionKinds(),
				Right.GetLegalActionKinds(),
				[](const EBattleActionKind L, const EBattleActionKind R)
				{
					return L == R;
				})
			&& AreOrderedPivotIdentityValuesEqual(
				Left.GetLegalMoveIds(),
				Right.GetLegalMoveIds(),
				[](const FMoveId& L, const FMoveId& R)
				{
					return L == R;
				})
			&& AreOrderedPivotIdentityValuesEqual(
				Left.GetAutomaticallyTargetedMoveIds(),
				Right.GetAutomaticallyTargetedMoveIds(),
				[](const FMoveId& L, const FMoveId& R)
				{
					return L == R;
				})
			&& AreOrderedPivotIdentityValuesEqual(
				Left.GetLegalSwitchPartySlots(),
				Right.GetLegalSwitchPartySlots(),
				[](const FPartySlotId L, const FPartySlotId R)
				{
					return L == R;
				})
			&& AreOrderedPivotIdentityValuesEqual(
				Left.GetLegalItemIds(),
				Right.GetLegalItemIds(),
				[](const FItemId& L, const FItemId& R)
				{
					return L == R;
				})
			&& AreOrderedPivotIdentityValuesEqual(
				Left.GetLegalActiveTargets(),
				Right.GetLegalActiveTargets(),
				[](const FActiveSlotId L, const FActiveSlotId R)
				{
					return L == R;
				})
			&& AreOrderedPivotIdentityValuesEqual(
				Left.GetLegalPartyTargets(),
				Right.GetLegalPartyTargets(),
				[](const FPartySlotId L, const FPartySlotId R)
				{
					return L == R;
				})
			&& AreOrderedPivotIdentityValuesEqual(
				Left.GetLegalMoveTargets(),
				Right.GetLegalMoveTargets(),
				[](const FBattleMoveTargetOption& L, const FBattleMoveTargetOption& R)
				{
					return L.MoveId == R.MoveId && L.ActiveSlotId == R.ActiveSlotId;
				})
			&& AreOrderedPivotIdentityValuesEqual(
				Left.GetLegalItemPartyTargets(),
				Right.GetLegalItemPartyTargets(),
				[](const FBattleItemPartyTargetOption& L,
					const FBattleItemPartyTargetOption& R)
				{
					return L.ItemId == R.ItemId && L.PartySlotId == R.PartySlotId;
				})
			&& AreOrderedPivotIdentityValuesEqual(
				Left.GetLegalItemActiveTargets(),
				Right.GetLegalItemActiveTargets(),
				[](const FBattleItemActiveTargetOption& L,
					const FBattleItemActiveTargetOption& R)
				{
					return L.ItemId == R.ItemId && L.ActiveSlotId == R.ActiveSlotId;
				})
			&& AreOrderedPivotIdentityValuesEqual(
				Left.GetUnavailableOptions(),
				Right.GetUnavailableOptions(),
				[](const FBattleUnavailableDecisionOption& L,
					const FBattleUnavailableDecisionOption& R)
				{
					return L.Kind == R.Kind
						&& L.Reason == R.Reason
						&& L.ActionKind == R.ActionKind
						&& L.MoveId == R.MoveId
						&& L.PartySlotId == R.PartySlotId
						&& L.ItemId == R.ItemId
						&& L.ActiveSlotId == R.ActiveSlotId;
				});
	}

	bool IsAcceptedBagCancellationDeltaValid(
		const FBattleEngineState& State,
		const FAcceptedBagCancellationDelta& Delta,
		const FBattleQueueBoundaryPlan& BoundaryPlan)
	{
		const FBattleResolutionCommitIdentity& Identity = Delta.ActionIdentity;
		const FBattleResolutionCommitPlan& Plan = Delta.ResolutionPlan;
		const int32 ReplacementCount = BoundaryPlan.Requirements.Num();
		if (!Identity.ResolutionId.IsValid()
			|| !Identity.OwningActionId.IsValid()
			|| Identity.ExpectedStateVersion == 0
			|| Identity.ExpectedStateVersion == TNumericLimits<uint64>::Max()
			|| Identity.ExpectedLockedActionIndex < 0
			|| Identity.ExpectedLockedActionIndex == TNumericLimits<int32>::Max()
			|| Delta.NextLockedActionIndex
				!= Identity.ExpectedLockedActionIndex + 1
			|| Delta.NextLockedActionIndex > State.LockedActions.Num()
			|| Delta.Phase != BoundaryPlan.PhaseAfter
			|| Plan.Identity.ResolutionId != Identity.ResolutionId
			|| Plan.Identity.OwningActionId != Identity.OwningActionId
			|| Plan.Identity.ExpectedStateVersion != Identity.ExpectedStateVersion
			|| Plan.Identity.ExpectedLockedActionIndex
				!= Identity.ExpectedLockedActionIndex
			|| Plan.StartingEventOrdinal != Identity.ExpectedEventOrdinal
			|| Plan.Events.Num() != 2 + ReplacementCount
			|| Plan.NextEventOrdinal
				!= Identity.ExpectedEventOrdinal
					+ static_cast<uint64>(Plan.Events.Num())
			|| !Plan.Resolution.IsValid()
			|| !Plan.Resolution.WasAccepted()
			|| Plan.Resolution.GetResolutionId() != Identity.ResolutionId
			|| Plan.Resolution.GetBeforeStateVersion()
				!= Identity.ExpectedStateVersion
			|| Plan.Resolution.GetAfterStateVersion()
				!= Identity.ExpectedStateVersion + 1
			|| Plan.Resolution.GetEvents().Num() != Plan.Events.Num())
		{
			return false;
		}

		auto IsExpectedActionEvent =
			[&Identity](
				const FBattleEvent& Event,
				const EBattleEventType Type,
				const EBattleEventCause Cause)
			{
				return Event.GetActionId() == Identity.OwningActionId
					&& Event.GetResolutionId() == Identity.ResolutionId
					&& Event.GetType() == Type
					&& Event.GetCause() == Cause
					&& Event.GetCauseActionKind() == EBattleActionKind::Bag
					&& Event.GetTargets().IsEmpty()
					&& Event.GetVisibility().Level
						== EBattleVisibilityLevel::Public;
			};
		if (!IsExpectedActionEvent(
				Plan.Events[0],
				EBattleEventType::ActionCanceled,
				EBattleEventCause::Item)
			|| !IsExpectedActionEvent(
				Plan.Events[1],
				EBattleEventType::ActionCompleted,
				EBattleEventCause::Action))
		{
			return false;
		}

		if (Delta.Phase == EBattlePhase::MandatoryReplacement)
		{
			if (ReplacementCount <= 0
				|| Delta.PendingReplacements.Num() != ReplacementCount
				|| !Delta.PendingDecision.IsSet()
				|| Delta.PendingDecisionRequests.IsEmpty()
				|| !ArePivotDecisionRequestsIdentical(
					Delta.PendingDecision.GetValue(),
					Delta.PendingDecisionRequests[0]))
			{
				return false;
			}

			const EBattleDecisionRequestKind RequestKind =
				Delta.PendingDecisionRequests[0].GetRequestKind();
			if ((RequestKind == EBattleDecisionRequestKind::ShiftResponse
					&& Delta.PendingDecisionRequests.Num() != 1)
				|| (RequestKind != EBattleDecisionRequestKind::ShiftResponse
					&& RequestKind
						!= EBattleDecisionRequestKind::MandatoryReplacement))
			{
				return false;
			}
			for (const FBattleDecisionRequest& Request :
				Delta.PendingDecisionRequests)
			{
				if (!Request.IsValid()
					|| Request.GetStateVersion()
						!= Identity.ExpectedStateVersion + 1
					|| (RequestKind
							== EBattleDecisionRequestKind::MandatoryReplacement
						&& Request.GetRequestKind() != RequestKind))
				{
					return false;
				}
			}
		}
		else if ((Delta.Phase != EBattlePhase::Resolving
				&& Delta.Phase != EBattlePhase::EndOfTurn)
			|| ReplacementCount != 0
			|| !Delta.PendingReplacements.IsEmpty()
			|| Delta.PendingDecision.IsSet()
			|| !Delta.PendingDecisionRequests.IsEmpty())
		{
			return false;
		}

		for (int32 Index = 0; Index < ReplacementCount; ++Index)
		{
			const FBattleReplacementRequirement& Requirement =
				BoundaryPlan.Requirements[Index];
			const FBattlePendingReplacementState& Pending =
				Delta.PendingReplacements[Index];
			const FBattleEvent& Event = Plan.Events[Index + 2];
			if (Pending.TrainerId != Requirement.Target.TrainerId
				|| Pending.ActiveSlotId != Requirement.Target.ActiveSlotId
				|| Event.GetActionId() != Identity.OwningActionId
				|| Event.GetResolutionId() != Identity.ResolutionId
				|| Event.GetType() != EBattleEventType::ReplacementRequired
				|| Event.GetCause() != EBattleEventCause::Rule
				|| Event.GetCauseActionKind() != EBattleActionKind::Bag
				|| Event.GetTargets().Num() != 1
				|| Event.GetTargets()[0].TrainerId
					!= Requirement.Target.TrainerId
				|| Event.GetTargets()[0].BattlerId
					!= Requirement.Target.BattlerId
				|| Event.GetTargets()[0].ActiveSlotId
					!= Requirement.Target.ActiveSlotId)
			{
				return false;
			}
		}
		return true;
	}

	bool ArePivotTargetResolutionsIdentical(
		const TOptional<FBattleTargetResolutionResult>& Left,
		const TOptional<FBattleTargetResolutionResult>& Right)
	{
		if (Left.IsSet() != Right.IsSet())
		{
			return false;
		}
		if (!Left.IsSet())
		{
			return true;
		}
		const FBattleTargetResolutionResult& L = Left.GetValue();
		const FBattleTargetResolutionResult& R = Right.GetValue();
		return L.TargetClass == R.TargetClass
			&& L.Outcome == R.Outcome
			&& L.bWasRedirected == R.bWasRedirected
			&& L.bUsedFaintedTargetFallback == R.bUsedFaintedTargetFallback
			&& AreOrderedPivotIdentityValuesEqual(
				TConstArrayView<FBattleResolvedTarget>(L.Targets),
				TConstArrayView<FBattleResolvedTarget>(R.Targets),
				[](const FBattleResolvedTarget& LTarget,
					const FBattleResolvedTarget& RTarget)
				{
					return LTarget == RTarget;
				});
	}

	bool ArePivotLockedActionsIdentical(
		const FBattleLockedActionState& Left,
		const FBattleLockedActionState& Right)
	{
		return Left.ActionId == Right.ActionId
			&& Left.QueueOrdinal == Right.QueueOrdinal
			&& ArePivotDecisionsIdentical(Left.Decision, Right.Decision)
			&& Left.OrderKey.CommandBand == Right.OrderKey.CommandBand
			&& Left.OrderKey.MovePriority == Right.OrderKey.MovePriority
			&& Left.OrderKey.FractionalPriorityTenths
				== Right.OrderKey.FractionalPriorityTenths
			&& Left.OrderKey.EffectiveSpeed == Right.OrderKey.EffectiveSpeed
			&& Left.OrderKey.ActingSlotId == Right.OrderKey.ActingSlotId
			&& Left.TargetClass == Right.TargetClass
			&& Left.SelectedTargetBattlerId == Right.SelectedTargetBattlerId
			&& Left.bStarted == Right.bStarted
			&& Left.bMoveCommitted == Right.bMoveCommitted
			&& ArePivotTargetResolutionsIdentical(
				Left.TargetResolution,
				Right.TargetResolution)
			&& Left.EffectExecutionState == Right.EffectExecutionState
			&& Left.bFinished == Right.bFinished;
	}

	/** Exact caller-serialized identity for a Pivot response continuing one Fight action. */
	struct FPivotSwitchCheckpointIdentity
	{
		FBattleResolutionCommitIdentity CommitIdentity;
		int32 ExpectedLockedActionCount = 0;
		int32 ExpectedEventCount = 0;
		int32 ExpectedTrainerCount = 0;
		int32 ExpectedPendingReplacementCount = 0;
		int32 ExpectedOpponentRemovalCheckpointCount = 0;
		int32 ExpectedPendingTriggerDispatchCount = 0;
		int32 ExpectedPendingTriggerEffectCount = 0;
		int32 ExpectedPendingTriggerLifecycleCount = 0;
		int32 ExpectedSubmittedDecisionCount = 0;
		uint64 ExpectedNextConditionCreationOrdinal = 0;
		uint64 ExpectedNextTriggerReentrancyToken = 0;
		FTrainerId ExpectedDecisionOwnerTrainerId;
		FBattlerId ExpectedOutgoingBattlerId;
		FBattlerId ExpectedIncomingBattlerId;
		FPartySlotId ExpectedIncomingPartySlotId;
		FActiveSlotId ExpectedActingSlotId;
		FBattleLockedActionState ExpectedAction;
		FBattleDecisionRequest ExpectedRequest;
		FBattleDecision ExpectedSubmittedResponse;
		TArray<FVoluntarySwitchBattlerIdentity> Battlers;
		TArray<FVoluntarySwitchActiveIdentity> ActivePositions;
		TArray<FBattleHeldItemInstanceId> HeldItemInstances;
		TArray<FBattleTriggerRegistrationId> TriggerRegistrations;
	};

	bool TryCapturePivotSwitchCheckpointIdentity(
		const FBattleEngineState& State,
		const FResolutionId ResolutionId,
		const FBattleLockedActionState& Action,
		const FBattleDecisionRequest& Request,
		const FBattleDecision& Response,
		FPivotSwitchCheckpointIdentity& OutIdentity)
	{
		OutIdentity = FPivotSwitchCheckpointIdentity();
		FBattleRejection RequestRejection;
		FBattleResolutionCommitIdentity CommitIdentity;
		if (Action.Decision.GetActionKind() != EBattleActionKind::Fight
			|| !Action.bStarted
			|| !Action.bMoveCommitted
			|| !Action.TargetResolution.IsSet()
			|| Action.EffectExecutionState
				!= EBattleLockedEffectExecutionState::AwaitingPivot
			|| Action.bFinished
			|| !Request.IsValid()
			|| Request.GetRequestKind() != EBattleDecisionRequestKind::PivotSwitch
			|| Request.GetStateVersion() != State.StateVersion
			|| !Response.IsValid()
			|| Response.GetRequestKind() != EBattleDecisionRequestKind::PivotSwitch
			|| Response.GetActionKind() != EBattleActionKind::Switch
			|| !Request.Allows(Response, RequestRejection)
			|| !State.PendingDecision.IsSet()
			|| State.PendingDecisionRequests.Num() != 1
			|| !ArePivotDecisionRequestsIdentical(
				State.PendingDecision.GetValue(),
				Request)
			|| !ArePivotDecisionRequestsIdentical(
				State.PendingDecisionRequests[0],
				Request)
			|| State.SubmittedDecisions.IsEmpty()
			|| !ArePivotDecisionsIdentical(State.SubmittedDecisions.Last(), Response)
			|| !FBattleResolutionCommit::TryCaptureIdentity(
				State,
				ResolutionId,
				Action.ActionId,
				CommitIdentity))
		{
			return false;
		}

		const FTrainerId TrainerId = Response.GetDecisionOwnerTrainerId();
		const FBattlerId OutgoingBattlerId = Response.GetActingBattlerId();
		const FPartySlotId IncomingPartySlotId = Response.GetSwitchPartySlotId();
		const FActiveSlotId ActingSlotId = Response.GetActiveTargetId();
		const FBattleTrainerState* Trainer = State.FindTrainer(TrainerId);
		const FBattleBattlerState* Outgoing = State.FindBattler(OutgoingBattlerId);
		const FBattleActivePositionState* Active = State.FindActivePosition(ActingSlotId);
		const FBattlePartySlotState* IncomingPartySlot = Trainer != nullptr
			? Trainer->PartySlots.FindByPredicate(
				[IncomingPartySlotId](const FBattlePartySlotState& Candidate)
				{
					return Candidate.PartySlotId == IncomingPartySlotId;
				})
			: nullptr;
		const FBattleBattlerState* Incoming = IncomingPartySlot != nullptr
			? State.FindBattler(IncomingPartySlot->BattlerId)
			: nullptr;
		if (Trainer == nullptr
			|| Outgoing == nullptr
			|| Active == nullptr
			|| Incoming == nullptr
			|| !Active->bAvailable
			|| Action.Decision.GetDecisionOwnerTrainerId() != TrainerId
			|| Action.Decision.GetActingBattlerId() != OutgoingBattlerId
			|| Action.OrderKey.ActingSlotId != ActingSlotId
			|| Request.GetDecisionOwnerTrainerId() != TrainerId
			|| Request.GetActingBattlerId() != OutgoingBattlerId
			|| Request.GetActingSlotId() != ActingSlotId
			|| Active->TrainerId != TrainerId
			|| Active->BattlerId != OutgoingBattlerId
			|| Outgoing->TrainerId != TrainerId
			|| Incoming->TrainerId != TrainerId
			|| Incoming->PartySlotId != IncomingPartySlotId)
		{
			return false;
		}

		OutIdentity.CommitIdentity = CommitIdentity;
		OutIdentity.ExpectedLockedActionCount = State.LockedActions.Num();
		OutIdentity.ExpectedEventCount = State.OrderedEvents.Num();
		OutIdentity.ExpectedTrainerCount = State.Trainers.Num();
		OutIdentity.ExpectedPendingReplacementCount = State.PendingReplacements.Num();
		OutIdentity.ExpectedOpponentRemovalCheckpointCount =
			State.AvailableOpponentRemovalCheckpoints.Num();
		OutIdentity.ExpectedPendingTriggerDispatchCount =
			State.TriggerFramework.GetPendingDispatchCount();
		OutIdentity.ExpectedPendingTriggerEffectCount =
			State.TriggerFramework.GetPendingEffectRequestCount();
		OutIdentity.ExpectedPendingTriggerLifecycleCount =
			State.TriggerFramework.GetPendingLifecycleFactCount();
		OutIdentity.ExpectedSubmittedDecisionCount = State.SubmittedDecisions.Num();
		OutIdentity.ExpectedNextConditionCreationOrdinal =
			State.NextConditionCreationOrdinal;
		OutIdentity.ExpectedNextTriggerReentrancyToken =
			State.NextTriggerReentrancyToken;
		OutIdentity.ExpectedDecisionOwnerTrainerId = TrainerId;
		OutIdentity.ExpectedOutgoingBattlerId = OutgoingBattlerId;
		OutIdentity.ExpectedIncomingBattlerId = Incoming->BattlerId;
		OutIdentity.ExpectedIncomingPartySlotId = IncomingPartySlotId;
		OutIdentity.ExpectedActingSlotId = ActingSlotId;
		OutIdentity.ExpectedAction = Action;
		OutIdentity.ExpectedRequest = Request;
		OutIdentity.ExpectedSubmittedResponse = Response;
		OutIdentity.Battlers.Reserve(State.Battlers.Num());
		for (const FBattleBattlerState& Battler : State.Battlers)
		{
			OutIdentity.Battlers.Add(MakeVoluntarySwitchBattlerIdentity(Battler));
		}
		OutIdentity.ActivePositions.Reserve(State.ActivePositions.Num());
		for (const FBattleActivePositionState& Position : State.ActivePositions)
		{
			FVoluntarySwitchActiveIdentity& Identity =
				OutIdentity.ActivePositions.AddDefaulted_GetRef();
			Identity.ActiveSlotId = Position.ActiveSlotId;
			Identity.TrainerId = Position.TrainerId;
			Identity.BattlerId = Position.BattlerId;
			Identity.bAvailable = Position.bAvailable;
		}
		for (const FBattleHeldItemInstanceState& Item : State.HeldItemLedger.GetStates())
		{
			OutIdentity.HeldItemInstances.Add(Item.InstanceId);
		}
		for (const FBattleTriggerRegistrationState& Registration :
			State.TriggerFramework.GetActiveRegistrations())
		{
			OutIdentity.TriggerRegistrations.Add(Registration.RegistrationId);
		}
		return true;
	}

	bool IsPivotSwitchCheckpointIdentityCurrent(
		const FBattleEngineState& State,
		const FPivotSwitchCheckpointIdentity& Identity)
	{
		const FBattleResolutionCommitIdentity& Commit = Identity.CommitIdentity;
		if (!FBattleResolutionCommit::IsIdentityCurrent(State, Commit)
			|| State.LockedActions.Num() != Identity.ExpectedLockedActionCount
			|| State.OrderedEvents.Num() != Identity.ExpectedEventCount
			|| State.Trainers.Num() != Identity.ExpectedTrainerCount
			|| State.PendingReplacements.Num()
				!= Identity.ExpectedPendingReplacementCount
			|| State.AvailableOpponentRemovalCheckpoints.Num()
				!= Identity.ExpectedOpponentRemovalCheckpointCount
			|| State.TriggerFramework.GetPendingDispatchCount()
				!= Identity.ExpectedPendingTriggerDispatchCount
			|| State.TriggerFramework.GetPendingEffectRequestCount()
				!= Identity.ExpectedPendingTriggerEffectCount
			|| State.TriggerFramework.GetPendingLifecycleFactCount()
				!= Identity.ExpectedPendingTriggerLifecycleCount
			|| State.SubmittedDecisions.Num()
				!= Identity.ExpectedSubmittedDecisionCount
			|| State.NextConditionCreationOrdinal
				!= Identity.ExpectedNextConditionCreationOrdinal
			|| State.NextTriggerReentrancyToken
				!= Identity.ExpectedNextTriggerReentrancyToken
			|| !State.PendingDecision.IsSet()
			|| State.PendingDecisionRequests.Num() != 1
			|| !ArePivotDecisionRequestsIdentical(
				State.PendingDecision.GetValue(),
				Identity.ExpectedRequest)
			|| !ArePivotDecisionRequestsIdentical(
				State.PendingDecisionRequests[0],
				Identity.ExpectedRequest)
			|| State.SubmittedDecisions.IsEmpty()
			|| !ArePivotDecisionsIdentical(
				State.SubmittedDecisions.Last(),
				Identity.ExpectedSubmittedResponse)
			|| State.Battlers.Num() != Identity.Battlers.Num()
			|| State.ActivePositions.Num() != Identity.ActivePositions.Num()
			|| State.HeldItemLedger.GetStates().Num()
				!= Identity.HeldItemInstances.Num()
			|| State.TriggerFramework.GetActiveRegistrations().Num()
				!= Identity.TriggerRegistrations.Num()
			|| !State.LockedActions.IsValidIndex(Commit.ExpectedLockedActionIndex)
			|| !ArePivotLockedActionsIdentical(
				State.LockedActions[Commit.ExpectedLockedActionIndex],
				Identity.ExpectedAction))
		{
			return false;
		}

		for (const FVoluntarySwitchBattlerIdentity& Expected : Identity.Battlers)
		{
			const FBattleBattlerState* Battler = State.FindBattler(Expected.BattlerId);
			if (Battler == nullptr || !MatchesVoluntarySwitchBattlerIdentity(*Battler, Expected))
			{
				return false;
			}
		}
		for (const FVoluntarySwitchActiveIdentity& Expected : Identity.ActivePositions)
		{
			const FBattleActivePositionState* Position =
				State.FindActivePosition(Expected.ActiveSlotId);
			if (Position == nullptr
				|| Position->bAvailable != Expected.bAvailable
				|| Position->TrainerId != Expected.TrainerId
				|| Position->BattlerId != Expected.BattlerId)
			{
				return false;
			}
		}
		for (const FBattleHeldItemInstanceId InstanceId : Identity.HeldItemInstances)
		{
			if (State.HeldItemLedger.FindState(InstanceId) == nullptr)
			{
				return false;
			}
		}
		for (const FBattleTriggerRegistrationId RegistrationId :
			Identity.TriggerRegistrations)
		{
			if (!State.TriggerFramework.GetActiveRegistrations().ContainsByPredicate(
					[RegistrationId](const FBattleTriggerRegistrationState& Candidate)
					{
						return Candidate.RegistrationId == RegistrationId;
					}))
			{
				return false;
			}
		}

		const FBattleActivePositionState* Active =
			State.FindActivePosition(Identity.ExpectedActingSlotId);
		const FBattleBattlerState* Outgoing =
			State.FindBattler(Identity.ExpectedOutgoingBattlerId);
		const FBattleBattlerState* Incoming =
			State.FindBattler(Identity.ExpectedIncomingBattlerId);
		return Active != nullptr
			&& Outgoing != nullptr
			&& Incoming != nullptr
			&& Active->TrainerId == Identity.ExpectedDecisionOwnerTrainerId
			&& Active->BattlerId == Identity.ExpectedOutgoingBattlerId
			&& Outgoing->TrainerId == Identity.ExpectedDecisionOwnerTrainerId
			&& Incoming->TrainerId == Identity.ExpectedDecisionOwnerTrainerId
			&& Incoming->PartySlotId == Identity.ExpectedIncomingPartySlotId;
	}

	/** Mutable record families shared by the switch, pre-move, and effect preparation plans. */
	struct FAtomicCheckpointCommonPreparation
	{
		TArray<FBattleBattlerState> Battlers;
		TArray<FBattleActivePositionState> ActivePositions;
		FBattleTriggerFramework TriggerFramework;
		FBattleAbilityItemRevealTracker AbilityItemRevealTracker;
		FBattleHeldItemLedger HeldItemLedger;
		uint64 NextConditionCreationOrdinal = 0;
		uint64 NextTriggerReentrancyToken = 0;
		uint64 NextEventOrdinal = 0;
		int32 CurrentLockedActionIndex = INDEX_NONE;
		EBattlePhase Phase = EBattlePhase::Resolving;
		EBattleOutcome Outcome = EBattleOutcome::InProgress;
		EBattleOutcomeCause OutcomeCause = EBattleOutcomeCause::None;
		TOptional<FBattleDecisionRequest> PendingDecision;
		TArray<FBattleDecisionRequest> PendingDecisionRequests;
		TArray<FBattlePendingReplacementState> PendingReplacements;
		TArray<uint64> AvailableOpponentRemovalCheckpoints;

		void Capture(const FBattleEngineState& State)
		{
			Battlers = State.Battlers;
			ActivePositions = State.ActivePositions;
			TriggerFramework = State.TriggerFramework;
			AbilityItemRevealTracker = State.AbilityItemRevealTracker;
			HeldItemLedger = State.HeldItemLedger;
			NextConditionCreationOrdinal = State.NextConditionCreationOrdinal;
			NextTriggerReentrancyToken = State.NextTriggerReentrancyToken;
			NextEventOrdinal = State.NextEventOrdinal;
			CurrentLockedActionIndex = State.CurrentLockedActionIndex;
			Phase = State.Phase;
			Outcome = State.Outcome;
			OutcomeCause = State.OutcomeCause;
			PendingDecision = State.PendingDecision;
			PendingDecisionRequests = State.PendingDecisionRequests;
			PendingReplacements = State.PendingReplacements;
			AvailableOpponentRemovalCheckpoints =
				State.AvailableOpponentRemovalCheckpoints;
		}
	};

	/** Complete switch-only preparation; immutable authority remains outside the plan. */
	struct FSwitchCheckpointPreparation
	{
		FAtomicCheckpointCommonPreparation Common;
		FBattleFieldState Field;
		TArray<FBattleSideState> Sides;
		FBattleLockedActionState Action;

		bool Capture(const FBattleEngineState& State, const FActionId ActionId)
		{
			const FBattleLockedActionState* CurrentAction = State.LockedActions.FindByPredicate(
				[ActionId](const FBattleLockedActionState& Candidate)
				{
					return Candidate.ActionId == ActionId;
				});
			if (CurrentAction == nullptr)
			{
				return false;
			}
			Common.Capture(State);
			Field = State.Field;
			Sides = State.Sides;
			Action = *CurrentAction;
			return true;
		}
	};

	/** Pre-move preparation owns only the mutable common families and its action. */
	struct FPreMoveCheckpointPreparation
	{
		FAtomicCheckpointCommonPreparation Common;
		FBattleLockedActionState Action;

		bool Capture(const FBattleEngineState& State, const FActionId ActionId)
		{
			const FBattleLockedActionState* CurrentAction =
				State.LockedActions.FindByPredicate(
					[ActionId](const FBattleLockedActionState& Candidate)
					{
						return Candidate.ActionId == ActionId;
					});
			if (CurrentAction == nullptr)
			{
				return false;
			}
			Common.Capture(State);
			Action = *CurrentAction;
			return true;
		}
	};

	/**
	 * Call-scoped adapter over immutable engine authority and one owned preparation plan.
	 * It contains references only and is never retained by a plan or commit delta.
	 */
	template <typename TFieldState, typename TSideStates>
	struct TAtomicCheckpointStateView
	{
		const FBattleSetup& Setup;
		const FBattleDefinitionCatalog& Catalog;
		const bool& bHasCatalog;
		const uint64& StateVersion;
		const FTurnId& TurnId;
		const EBattleEncounterKind& EncounterKind;
		const EBattleFormat& Format;
		const TArray<FBattleTrainerState>& Trainers;
		TArray<FBattleBattlerState>& Battlers;
		TArray<FBattleActivePositionState>& ActivePositions;
		TFieldState& Field;
		TSideStates& Sides;
		const FBattleCompiledEncounterPolicies& CompiledEncounterPolicies;
		const TArray<FBattleLockedActionState>& LockedActions;
		int32& CurrentLockedActionIndex;
		EBattlePhase& Phase;
		EBattleOutcome& Outcome;
		EBattleOutcomeCause& OutcomeCause;
		TOptional<FBattleDecisionRequest>& PendingDecision;
		TArray<FBattleDecisionRequest>& PendingDecisionRequests;
		TArray<FBattlePendingReplacementState>& PendingReplacements;
		FBattleTriggerFramework& TriggerFramework;
		FBattleAbilityItemRevealTracker& AbilityItemRevealTracker;
		FBattleHeldItemLedger& HeldItemLedger;
		uint64& NextEventOrdinal;
		uint64& NextConditionCreationOrdinal;
		uint64& NextTriggerReentrancyToken;
		TArray<uint64>& AvailableOpponentRemovalCheckpoints;

		TAtomicCheckpointStateView(
			const FBattleEngineState& Authority,
			FAtomicCheckpointCommonPreparation& Preparation,
			TFieldState& InField,
			TSideStates& InSides)
			: Setup(Authority.Setup)
			, Catalog(Authority.Catalog)
			, bHasCatalog(Authority.bHasCatalog)
			, StateVersion(Authority.StateVersion)
			, TurnId(Authority.TurnId)
			, EncounterKind(Authority.EncounterKind)
			, Format(Authority.Format)
			, Trainers(Authority.Trainers)
			, Battlers(Preparation.Battlers)
			, ActivePositions(Preparation.ActivePositions)
			, Field(InField)
			, Sides(InSides)
			, CompiledEncounterPolicies(Authority.CompiledEncounterPolicies)
			, LockedActions(Authority.LockedActions)
			, CurrentLockedActionIndex(Preparation.CurrentLockedActionIndex)
			, Phase(Preparation.Phase)
			, Outcome(Preparation.Outcome)
			, OutcomeCause(Preparation.OutcomeCause)
			, PendingDecision(Preparation.PendingDecision)
			, PendingDecisionRequests(Preparation.PendingDecisionRequests)
			, PendingReplacements(Preparation.PendingReplacements)
			, TriggerFramework(Preparation.TriggerFramework)
			, AbilityItemRevealTracker(Preparation.AbilityItemRevealTracker)
			, HeldItemLedger(Preparation.HeldItemLedger)
			, NextEventOrdinal(Preparation.NextEventOrdinal)
			, NextConditionCreationOrdinal(Preparation.NextConditionCreationOrdinal)
			, NextTriggerReentrancyToken(Preparation.NextTriggerReentrancyToken)
			, AvailableOpponentRemovalCheckpoints(
				Preparation.AvailableOpponentRemovalCheckpoints)
		{
		}

		[[nodiscard]] const FBattleTrainerState* FindTrainer(const FTrainerId TrainerId) const
		{
			return Trainers.FindByPredicate(
				[TrainerId](const FBattleTrainerState& Candidate)
				{
					return Candidate.TrainerId == TrainerId;
				});
		}

		[[nodiscard]] const FBattleBattlerState* FindBattler(const FBattlerId BattlerId) const
		{
			return Battlers.FindByPredicate(
				[BattlerId](const FBattleBattlerState& Candidate)
				{
					return Candidate.BattlerId == BattlerId;
				});
		}

		[[nodiscard]] FBattleBattlerState* FindMutableBattler(const FBattlerId BattlerId)
		{
			return Battlers.FindByPredicate(
				[BattlerId](const FBattleBattlerState& Candidate)
				{
					return Candidate.BattlerId == BattlerId;
				});
		}

		[[nodiscard]] const FBattleActivePositionState* FindActivePosition(
			const FActiveSlotId ActiveSlotId) const
		{
			return ActivePositions.FindByPredicate(
				[ActiveSlotId](const FBattleActivePositionState& Candidate)
				{
					return Candidate.ActiveSlotId == ActiveSlotId;
				});
		}

		[[nodiscard]] FBattleActivePositionState* FindMutableActivePosition(
			const FActiveSlotId ActiveSlotId)
		{
			return ActivePositions.FindByPredicate(
				[ActiveSlotId](const FBattleActivePositionState& Candidate)
				{
					return Candidate.ActiveSlotId == ActiveSlotId;
				});
		}
	};

	using FMutableFieldSideCheckpointView = TAtomicCheckpointStateView<
		FBattleFieldState,
		TArray<FBattleSideState>>;
	using FReadOnlyFieldSideCheckpointView = TAtomicCheckpointStateView<
		const FBattleFieldState,
		const TArray<FBattleSideState>>;

	template <typename TState>
	bool TryAppendAtomicSwitchBoundaryEvents(
		TState& StateView,
		const FResolutionId ResolutionId,
		const FBattleLockedActionState& Action,
		TArray<FBattleEvent>& Events)
	{
		FBattleQueueBoundaryPlan BoundaryPlan;
		if (!FBattleFaintOutcomeResolver::ResolveQueueBoundary(
				StateView.Phase,
				StateView.Outcome,
				StateView.CurrentLockedActionIndex,
				StateView.LockedActions.Num(),
				StateView.Setup.GetStartingActive(),
				StateView.Battlers,
				StateView.ActivePositions,
				BoundaryPlan)
			|| !FBattleFaintOutcomeResolver::TryApplyQueueBoundaryPlan(
				StateView.Phase,
				BoundaryPlan))
		{
			return false;
		}
		const TArray<FBattleReplacementRequirement>& Requirements =
			BoundaryPlan.Requirements;
		if (StateView.Phase == EBattlePhase::MandatoryReplacement)
		{
			if (Requirements.IsEmpty()
				|| StateView.StateVersion == TNumericLimits<uint64>::Max())
			{
				return false;
			}
			StateView.PendingReplacements.Reset();
			for (const FBattleReplacementRequirement& Requirement : Requirements)
			{
				FBattlePendingReplacementState& Pending =
					StateView.PendingReplacements.AddDefaulted_GetRef();
				Pending.TrainerId = Requirement.Target.TrainerId;
				Pending.ActiveSlotId = Requirement.Target.ActiveSlotId;
			}
			TArray<FBattleDecisionRequest> Requests;
			if (!TryBuildReplacementCheckpointRequests(
					StateView,
					StateView.StateVersion + 1,
					true,
					Requests)
				|| Requests.IsEmpty())
			{
				return false;
			}
			StateView.PendingDecisionRequests = MoveTemp(Requests);
			StateView.PendingDecision = StateView.PendingDecisionRequests[0];
		}
		else if (StateView.Phase == EBattlePhase::EndOfTurn)
		{
			StateView.PendingReplacements.Reset();
			StateView.PendingDecisionRequests.Reset();
			StateView.PendingDecision.Reset();
		}
		else if (StateView.Phase != EBattlePhase::Resolving
			&& StateView.Phase != EBattlePhase::Terminal)
		{
			return false;
		}

		for (const FBattleReplacementRequirement& Requirement : Requirements)
		{
			Events.Add(MakeTargetedActionEvent(
				StateView,
				ResolutionId,
				Action,
				EBattleEventType::ReplacementRequired,
				EBattleEventCause::Rule,
				Requirement.Target));
		}
		return true;
	}

	FBattleEventSpec MakeAtomicSwitchStagedEventSpec(const FBattleEvent& Event)
	{
		FBattleEventSpec Spec;
		Spec.BattleId = Event.GetBattleId();
		Spec.TurnId = Event.GetTurnId();
		Spec.ActionId = Event.GetActionId();
		Spec.ResolutionId = Event.GetResolutionId();
		Spec.Type = Event.GetType();
		Spec.Cause = Event.GetCause();
		Spec.CauseActionKind = Event.GetCauseActionKind();
		Spec.OutcomeCause = Event.GetOutcomeCause();
		Spec.Source = Event.GetSource();
		for (const FBattleEventTarget& Target : Event.GetTargets())
		{
			Spec.Targets.Add(Target);
		}
		Spec.NumericBefore = Event.GetNumericBefore();
		Spec.NumericAfter = Event.GetNumericAfter();
		Spec.NumericDelta = Event.GetNumericDelta();
		Spec.SimultaneousGroupId = Event.GetSimultaneousGroupId();
		Spec.HitIndex = Event.GetHitIndex();
		Spec.HitCount = Event.GetHitCount();
		Spec.ActionOrder = Event.GetActionOrder();
		Spec.TargetResolution = Event.GetTargetResolution();
		Spec.Capture = Event.GetCapture();
		Spec.Visibility = Event.GetVisibility();
		return Spec;
	}

	struct FAtomicBattlerRecordDelta
	{
		FBattlerId BattlerId;
		FBattleBattlerState After;
	};

	struct FAtomicActivePositionRecordDelta
	{
		FActiveSlotId ActiveSlotId;
		FBattleActivePositionState After;
	};

	struct FAtomicSideRecordDelta
	{
		EBattleSide Side = EBattleSide::Player;
		FBattleSideState After;
	};

	struct FAtomicCheckpointCommonDelta
	{
		TArray<FAtomicBattlerRecordDelta> Battlers;
		TArray<FAtomicActivePositionRecordDelta> ActivePositions;
		FBattleTriggerFramework TriggerFramework;
		FBattleAbilityItemRevealTracker AbilityItemRevealTracker;
		FBattleHeldItemLedger HeldItemLedger;
		uint64 NextConditionCreationOrdinal = 0;
		uint64 NextTriggerReentrancyToken = 0;
		int32 NextLockedActionIndex = INDEX_NONE;
		EBattlePhase Phase = EBattlePhase::Resolving;
		EBattleOutcome Outcome = EBattleOutcome::InProgress;
		EBattleOutcomeCause OutcomeCause = EBattleOutcomeCause::None;
		TOptional<FBattleDecisionRequest> PendingDecision;
		TArray<FBattleDecisionRequest> PendingDecisionRequests;
		TArray<FBattlePendingReplacementState> PendingReplacements;
		TArray<uint64> AvailableOpponentRemovalCheckpoints;
	};

	struct FAtomicSwitchStateDelta : FAtomicCheckpointCommonDelta
	{
		FBattleFieldState Field;
		TArray<FAtomicSideRecordDelta> Sides;
	};

	bool TryCaptureAtomicCheckpointCommonDelta(
		const FAtomicCheckpointCommonPreparation& Preparation,
		FAtomicCheckpointCommonDelta& OutDelta)
	{
		OutDelta = FAtomicCheckpointCommonDelta();
		TSet<FBattlerId> BattlerIds;
		for (const FBattleBattlerState& Battler : Preparation.Battlers)
		{
			if (!Battler.BattlerId.IsValid() || BattlerIds.Contains(Battler.BattlerId))
			{
				return false;
			}
			BattlerIds.Add(Battler.BattlerId);
			FAtomicBattlerRecordDelta& Record = OutDelta.Battlers.AddDefaulted_GetRef();
			Record.BattlerId = Battler.BattlerId;
			Record.After = Battler;
		}

		TSet<FActiveSlotId> ActiveSlotIds;
		for (const FBattleActivePositionState& Position : Preparation.ActivePositions)
		{
			if (!Position.ActiveSlotId.IsValid()
				|| ActiveSlotIds.Contains(Position.ActiveSlotId))
			{
				return false;
			}
			ActiveSlotIds.Add(Position.ActiveSlotId);
			FAtomicActivePositionRecordDelta& Record =
				OutDelta.ActivePositions.AddDefaulted_GetRef();
			Record.ActiveSlotId = Position.ActiveSlotId;
			Record.After = Position;
		}

		OutDelta.TriggerFramework = Preparation.TriggerFramework;
		OutDelta.AbilityItemRevealTracker = Preparation.AbilityItemRevealTracker;
		OutDelta.HeldItemLedger = Preparation.HeldItemLedger;
		OutDelta.NextConditionCreationOrdinal = Preparation.NextConditionCreationOrdinal;
		OutDelta.NextTriggerReentrancyToken = Preparation.NextTriggerReentrancyToken;
		OutDelta.NextLockedActionIndex = Preparation.CurrentLockedActionIndex;
		OutDelta.Phase = Preparation.Phase;
		OutDelta.Outcome = Preparation.Outcome;
		OutDelta.OutcomeCause = Preparation.OutcomeCause;
		OutDelta.PendingDecision = Preparation.PendingDecision;
		OutDelta.PendingDecisionRequests = Preparation.PendingDecisionRequests;
		OutDelta.PendingReplacements = Preparation.PendingReplacements;
		OutDelta.AvailableOpponentRemovalCheckpoints =
			Preparation.AvailableOpponentRemovalCheckpoints;
		return true;
	}

	bool TryCaptureAtomicFieldSideDelta(
		const FAtomicCheckpointCommonPreparation& Common,
		const FBattleFieldState& Field,
		const TConstArrayView<FBattleSideState> Sides,
		FAtomicSwitchStateDelta& OutDelta)
	{
		OutDelta = FAtomicSwitchStateDelta();
		if (!TryCaptureAtomicCheckpointCommonDelta(Common, OutDelta))
		{
			return false;
		}
		OutDelta.Field = Field;
		TSet<EBattleSide> SeenSides;
		for (const FBattleSideState& Side : Sides)
		{
			if (SeenSides.Contains(Side.Side))
			{
				return false;
			}
			SeenSides.Add(Side.Side);
			FAtomicSideRecordDelta& Record = OutDelta.Sides.AddDefaulted_GetRef();
			Record.Side = Side.Side;
			Record.After = Side;
		}
		return true;
	}

	bool TryCaptureAtomicSwitchDelta(
		const FSwitchCheckpointPreparation& Preparation,
		FAtomicSwitchStateDelta& OutDelta)
	{
		return TryCaptureAtomicFieldSideDelta(
			Preparation.Common,
			Preparation.Field,
			Preparation.Sides,
			OutDelta);
	}

	bool AreAtomicCheckpointCommonDeltaRecordsValid(
		const TConstArrayView<FVoluntarySwitchBattlerIdentity> ExpectedBattlers,
		const TConstArrayView<FVoluntarySwitchActiveIdentity> ExpectedActivePositions,
		const FAtomicCheckpointCommonDelta& Delta)
	{
		if (Delta.Battlers.Num() != ExpectedBattlers.Num()
			|| Delta.ActivePositions.Num() != ExpectedActivePositions.Num())
		{
			return false;
		}
		for (const FVoluntarySwitchBattlerIdentity& Expected : ExpectedBattlers)
		{
			const FAtomicBattlerRecordDelta* Staged = Delta.Battlers.FindByPredicate(
				[&Expected](const FAtomicBattlerRecordDelta& Candidate)
				{
					return Candidate.BattlerId == Expected.BattlerId;
				});
			if (Staged == nullptr
				|| Staged->After.TrainerId != Expected.TrainerId
				|| Staged->After.SourcePokemonId != Expected.SourcePokemonId
				|| Staged->After.PartySlotId != Expected.PartySlotId)
			{
				return false;
			}
		}
		for (const FVoluntarySwitchActiveIdentity& Expected : ExpectedActivePositions)
		{
			if (!Delta.ActivePositions.ContainsByPredicate(
					[&Expected](const FAtomicActivePositionRecordDelta& Candidate)
					{
						return Candidate.ActiveSlotId == Expected.ActiveSlotId
							&& Candidate.After.bAvailable == Expected.bAvailable;
					}))
			{
				return false;
			}
		}
		for (const FBattleHeldItemInstanceState& Item : Delta.HeldItemLedger.GetStates())
		{
			if (!Item.InstanceId.IsValid())
			{
				return false;
			}
		}
		for (const FBattleTriggerRegistrationState& Registration :
			Delta.TriggerFramework.GetActiveRegistrations())
		{
			if (!Registration.RegistrationId.IsValid())
			{
				return false;
			}
		}
		return true;
	}

	void ApplyAtomicCheckpointCommonDelta(
		FBattleEngineState& State,
		const FAtomicCheckpointCommonDelta& Delta)
	{
		for (const FAtomicBattlerRecordDelta& Record : Delta.Battlers)
		{
			FBattleBattlerState* Battler = State.FindMutableBattler(Record.BattlerId);
			check(Battler != nullptr);
			*Battler = Record.After;
		}
		for (const FAtomicActivePositionRecordDelta& Record : Delta.ActivePositions)
		{
			FBattleActivePositionState* Position =
				State.FindMutableActivePosition(Record.ActiveSlotId);
			check(Position != nullptr);
			*Position = Record.After;
		}
		State.TriggerFramework = Delta.TriggerFramework;
		State.AbilityItemRevealTracker = Delta.AbilityItemRevealTracker;
		State.HeldItemLedger = Delta.HeldItemLedger;
		State.NextConditionCreationOrdinal = Delta.NextConditionCreationOrdinal;
		State.NextTriggerReentrancyToken = Delta.NextTriggerReentrancyToken;
		State.CurrentLockedActionIndex = Delta.NextLockedActionIndex;
		State.Phase = Delta.Phase;
		State.Outcome = Delta.Outcome;
		State.OutcomeCause = Delta.OutcomeCause;
		State.PendingDecision = Delta.PendingDecision;
		State.PendingDecisionRequests = Delta.PendingDecisionRequests;
		State.PendingReplacements = Delta.PendingReplacements;
		State.AvailableOpponentRemovalCheckpoints =
			Delta.AvailableOpponentRemovalCheckpoints;
	}

	void ApplyAtomicSwitchStateDelta(
		FBattleEngineState& State,
		const FAtomicSwitchStateDelta& Delta)
	{
		ApplyAtomicCheckpointCommonDelta(State, Delta);
		State.Field = Delta.Field;
		for (const FAtomicSideRecordDelta& Record : Delta.Sides)
		{
			FBattleSideState* Side = State.Sides.FindByPredicate(
				[&Record](const FBattleSideState& Candidate)
				{
					return Candidate.Side == Record.Side;
				});
			check(Side != nullptr);
			*Side = Record.After;
		}
	}

	void ApplyVoluntarySwitchDelta(
		FBattleEngineState& State,
		const FVoluntarySwitchCheckpointIdentity& Identity,
		const FAtomicSwitchStateDelta& Delta)
	{
		FBattleLockedActionState* Action = State.LockedActions.FindByPredicate(
			[&Identity](const FBattleLockedActionState& Candidate)
			{
				return Candidate.ActionId == Identity.CommitIdentity.OwningActionId;
			});
		check(Action != nullptr);
		ApplyAtomicSwitchStateDelta(State, Delta);
		Action->bFinished = true;
	}

	void ApplyPivotSwitchDelta(
		FBattleEngineState& State,
		const FPivotSwitchCheckpointIdentity& Identity,
		const FAtomicSwitchStateDelta& Delta)
	{
		FBattleLockedActionState* Action = State.LockedActions.FindByPredicate(
			[&Identity](const FBattleLockedActionState& Candidate)
			{
				return Candidate.ActionId == Identity.CommitIdentity.OwningActionId;
			});
		check(Action != nullptr);
		ApplyAtomicSwitchStateDelta(State, Delta);
		Action->EffectExecutionState = EBattleLockedEffectExecutionState::Completed;
		Action->bFinished = true;
	}

	/** Exact caller-serialized identity for one started, uncommitted Fight action. */
	bool ArePreMoveConditionsIdentical(
		const TArray<FBattleConditionState>& Left,
		const TArray<FBattleConditionState>& Right)
	{
		return AreOrderedPivotIdentityValuesEqual(
			TConstArrayView<FBattleConditionState>(Left),
			TConstArrayView<FBattleConditionState>(Right),
			[](const FBattleConditionState& L, const FBattleConditionState& R)
			{
				return L.ConditionId == R.ConditionId
					&& L.RemainingTurns == R.RemainingTurns
					&& L.LayerCount == R.LayerCount
					&& L.CreationOrdinal == R.CreationOrdinal
					&& L.SourceBattlerId == R.SourceBattlerId;
			});
	}

	bool ArePreMoveBattlersIdentical(
		const FBattleBattlerState& Left,
		const FBattleBattlerState& Right)
	{
		if (Left.TrainerId != Right.TrainerId
			|| Left.BattlerId != Right.BattlerId
			|| Left.SourcePokemonId != Right.SourcePokemonId
			|| Left.PartySlotId != Right.PartySlotId
			|| Left.SpeciesFormId != Right.SpeciesFormId
			|| Left.CaptureClassification != Right.CaptureClassification
			|| Left.Level != Right.Level
			|| Left.PermanentStats.MaxHP != Right.PermanentStats.MaxHP
			|| Left.PermanentStats.Attack != Right.PermanentStats.Attack
			|| Left.PermanentStats.Defense != Right.PermanentStats.Defense
			|| Left.PermanentStats.SpecialAttack != Right.PermanentStats.SpecialAttack
			|| Left.PermanentStats.SpecialDefense != Right.PermanentStats.SpecialDefense
			|| Left.PermanentStats.Speed != Right.PermanentStats.Speed
			|| Left.CurrentHP != Right.CurrentHP
			|| Left.bFainted != Right.bFainted
			|| Left.bCaptured != Right.bCaptured
			|| Left.bRemoved != Right.bRemoved
			|| Left.bFaintTransitionPending != Right.bFaintTransitionPending
			|| Left.bEgg != Right.bEgg
			|| Left.MajorStatusId != Right.MajorStatusId
			|| Left.AbilityId != Right.AbilityId
			|| Left.bAbilitySuppressed != Right.bAbilitySuppressed
			|| Left.EnteredActiveOnTurnId != Right.EnteredActiveOnTurnId
			|| Left.HeldItem.InstanceId != Right.HeldItem.InstanceId
			|| Left.HeldItem.OriginalItemId != Right.HeldItem.OriginalItemId
			|| Left.HeldItem.CurrentItemId != Right.HeldItem.CurrentItemId
			|| Left.HeldItem.bConsumed != Right.HeldItem.bConsumed
			|| Left.HeldItem.bSuppressed != Right.HeldItem.bSuppressed
			|| Left.HeldItem.bRevealed != Right.HeldItem.bRevealed
			|| Left.HeldItem.bTemporarilyRemoved
				!= Right.HeldItem.bTemporarilyRemoved
			|| Left.HeldItem.ChoiceLockedMoveId
				!= Right.HeldItem.ChoiceLockedMoveId
			|| Left.LastMoveId != Right.LastMoveId
			|| Left.Obedience.bHasSnapshot != Right.Obedience.bHasSnapshot
			|| Left.Obedience.bSubjectToPlayerCap
				!= Right.Obedience.bSubjectToPlayerCap
			|| Left.Obedience.ReferenceLevel != Right.Obedience.ReferenceLevel
			|| Left.Obedience.BadgeCount != Right.Obedience.BadgeCount
			|| !ArePreMoveConditionsIdentical(Left.Volatiles, Right.Volatiles)
			|| !AreOrderedPivotIdentityValuesEqual(
				TConstArrayView<FBattleMoveSlotState>(Left.Moves),
				TConstArrayView<FBattleMoveSlotState>(Right.Moves),
				[](const FBattleMoveSlotState& L, const FBattleMoveSlotState& R)
				{
					return L.SlotIndex == R.SlotIndex
						&& L.MoveId == R.MoveId
						&& L.CurrentPP == R.CurrentPP
						&& L.MaxPP == R.MaxPP;
				}))
		{
			return false;
		}

		for (int32 StatIndex = static_cast<int32>(EBattleStat::Attack);
			StatIndex <= static_cast<int32>(EBattleStat::Evasion);
			++StatIndex)
		{
			int32 LeftStage = 0;
			int32 RightStage = 0;
			if (!Left.Stages.TryGetStage(
					static_cast<EBattleStat>(StatIndex),
					LeftStage)
				|| !Right.Stages.TryGetStage(
					static_cast<EBattleStat>(StatIndex),
					RightStage)
				|| LeftStage != RightStage)
			{
				return false;
			}
		}
		return true;
	}

	bool ArePreMoveTriggerRegistrationsIdentical(
		const FBattleTriggerRegistrationState& Left,
		const FBattleTriggerRegistrationState& Right)
	{
		const FBattleTriggerRegistrationSpec& L = Left.Spec;
		const FBattleTriggerRegistrationSpec& R = Right.Spec;
		return Left.RegistrationId == Right.RegistrationId
			&& Left.CreationOrdinal == Right.CreationOrdinal
			&& Left.RemainingTurns == Right.RemainingTurns
			&& Left.Layers == Right.Layers
			&& Left.bSuppressed == Right.bSuppressed
			&& L.Rule.Phase == R.Rule.Phase
			&& L.Rule.EffectId == R.Rule.EffectId
			&& L.Rule.PayloadId == R.Rule.PayloadId
			&& L.Rule.Order == R.Rule.Order
			&& L.Rule.Priority == R.Rule.Priority
			&& L.Rule.Suborder == R.Rule.Suborder
			&& L.Rule.bRepeatable == R.Rule.bRepeatable
			&& L.Rule.bDecrementDurationBeforeEffect
				== R.Rule.bDecrementDurationBeforeEffect
			&& L.SourceDefinition == R.SourceDefinition
			&& L.Owner == R.Owner
			&& L.Source == R.Source
			&& AreOrderedPivotIdentityValuesEqual(
				TConstArrayView<FBattleTriggerSubject>(L.Targets),
				TConstArrayView<FBattleTriggerSubject>(R.Targets),
				[](const FBattleTriggerSubject& LTarget,
					const FBattleTriggerSubject& RTarget)
				{
					return LTarget == RTarget;
				})
			&& L.DurationOwner == R.DurationOwner
			&& L.RemainingTurns == R.RemainingTurns
			&& L.Layers == R.Layers
			&& L.Visibility.Level == R.Visibility.Level
			&& L.Visibility.OwningTrainerId == R.Visibility.OwningTrainerId
			&& L.Visibility.OwningSide == R.Visibility.OwningSide
			&& L.Visibility.bHasOwningSide == R.Visibility.bHasOwningSide
			&& L.CleanupPolicy == R.CleanupPolicy
			&& L.bSuppressed == R.bSuppressed;
	}

	struct FPreMoveCheckpointIdentity
	{
		FBattleResolutionCommitIdentity CommitIdentity;
		int32 ExpectedLockedActionCount = 0;
		int32 ExpectedEventCount = 0;
		int32 ExpectedTrainerCount = 0;
		int32 ExpectedPendingDecisionRequestCount = 0;
		int32 ExpectedPendingReplacementCount = 0;
		int32 ExpectedOpponentRemovalCheckpointCount = 0;
		int32 ExpectedPendingTriggerDispatchCount = 0;
		int32 ExpectedPendingTriggerEffectCount = 0;
		int32 ExpectedPendingTriggerLifecycleCount = 0;
		uint64 ExpectedNextConditionCreationOrdinal = 0;
		uint64 ExpectedNextTriggerReentrancyToken = 0;
		FBattleLockedActionState ExpectedAction;
		FBattlerId ExpectedActorId;
		FBattleBattlerState ExpectedActor;
		uint8 ExpectedMoveSlotNumber = 255;
		int32 ExpectedCurrentPP = 0;
		int32 ExpectedMaximumPP = 0;
		TArray<FVoluntarySwitchBattlerIdentity> Battlers;
		TArray<FBattleBattlerState> ExactBattlers;
		TArray<uint8> AbilityRevealFacts;
		TArray<uint8> ItemRevealFacts;
		TArray<FVoluntarySwitchActiveIdentity> ActivePositions;
		TArray<FBattleHeldItemInstanceState> HeldItemStates;
		TArray<FBattleTriggerRegistrationState> TriggerRegistrations;
	};

	bool TryCapturePreMoveCheckpointIdentity(
		const FBattleEngineState& State,
		const FResolutionId ResolutionId,
		const FBattleLockedActionState& Action,
		FPreMoveCheckpointIdentity& OutIdentity)
	{
		OutIdentity = FPreMoveCheckpointIdentity();
		FBattleResolutionCommitIdentity CommitIdentity;
		if (Action.Decision.GetActionKind() != EBattleActionKind::Fight
			|| !Action.bStarted
			|| Action.bMoveCommitted
			|| Action.TargetResolution.IsSet()
			|| Action.EffectExecutionState != EBattleLockedEffectExecutionState::Pending
			|| Action.bFinished
			|| !FBattleResolutionCommit::TryCaptureIdentity(
				State,
				ResolutionId,
				Action.ActionId,
				CommitIdentity))
		{
			return false;
		}

		const FBattleBattlerState* Actor = State.FindBattler(
			Action.Decision.GetActingBattlerId());
		const FBattleActivePositionState* Active = State.FindActivePosition(
			Action.OrderKey.ActingSlotId);
		if (Actor == nullptr
			|| Active == nullptr
			|| !Active->bAvailable
			|| Active->TrainerId != Action.Decision.GetDecisionOwnerTrainerId()
			|| Active->BattlerId != Actor->BattlerId
			|| Actor->TrainerId != Action.Decision.GetDecisionOwnerTrainerId())
		{
			return false;
		}

		const bool bStruggle = Action.Decision.GetMoveId()
			== FBattleBuiltInMoveDefinitions::GetStruggleMoveId();
		const FBattleMoveSlotState* MoveSlot = nullptr;
		if (!bStruggle)
		{
			MoveSlot = Actor->Moves.FindByPredicate(
				[&Action](const FBattleMoveSlotState& Candidate)
				{
					return Candidate.MoveId == Action.Decision.GetMoveId();
				});
			if (MoveSlot == nullptr)
			{
				return false;
			}
		}

		OutIdentity.CommitIdentity = CommitIdentity;
		OutIdentity.ExpectedLockedActionCount = State.LockedActions.Num();
		OutIdentity.ExpectedEventCount = State.OrderedEvents.Num();
		OutIdentity.ExpectedTrainerCount = State.Trainers.Num();
		OutIdentity.ExpectedPendingDecisionRequestCount =
			State.PendingDecisionRequests.Num();
		OutIdentity.ExpectedPendingReplacementCount = State.PendingReplacements.Num();
		OutIdentity.ExpectedOpponentRemovalCheckpointCount =
			State.AvailableOpponentRemovalCheckpoints.Num();
		OutIdentity.ExpectedPendingTriggerDispatchCount =
			State.TriggerFramework.GetPendingDispatchCount();
		OutIdentity.ExpectedPendingTriggerEffectCount =
			State.TriggerFramework.GetPendingEffectRequestCount();
		OutIdentity.ExpectedPendingTriggerLifecycleCount =
			State.TriggerFramework.GetPendingLifecycleFactCount();
		OutIdentity.ExpectedNextConditionCreationOrdinal =
			State.NextConditionCreationOrdinal;
		OutIdentity.ExpectedNextTriggerReentrancyToken =
			State.NextTriggerReentrancyToken;
		OutIdentity.ExpectedAction = Action;
		OutIdentity.ExpectedActorId = Actor->BattlerId;
		if (MoveSlot != nullptr)
		{
			OutIdentity.ExpectedMoveSlotNumber = MoveSlot->SlotIndex;
			OutIdentity.ExpectedCurrentPP = MoveSlot->CurrentPP;
			OutIdentity.ExpectedMaximumPP = MoveSlot->MaxPP;
		}

		OutIdentity.Battlers.Reserve(State.Battlers.Num());
		for (const FBattleBattlerState& Battler : State.Battlers)
		{
			OutIdentity.Battlers.Add(MakeVoluntarySwitchBattlerIdentity(Battler));
			OutIdentity.ExactBattlers.Add(Battler);
			FBattleTriggerSubject Owner;
			const bool bOwnerValid = FBattleTriggerSubject::TryCreateBattler(
				Battler.BattlerId,
				Owner);
			FBattleTriggerSourceDefinition AbilitySource;
			const bool bAbilitySourceValid = bOwnerValid
				&& FBattleTriggerSourceDefinition::TryCreateAbility(
					Battler.AbilityId,
					AbilitySource);
			OutIdentity.AbilityRevealFacts.Add(
				bAbilitySourceValid
					&& State.AbilityItemRevealTracker.HasBeenRevealed(
						AbilitySource,
						Owner)
					? 1
					: 0);
			FBattleTriggerSourceDefinition ItemSource;
			const bool bItemSourceValid = bOwnerValid
				&& Battler.HeldItem.CurrentItemId.IsValid()
				&& FBattleTriggerSourceDefinition::TryCreateItem(
					Battler.HeldItem.CurrentItemId,
					ItemSource);
			OutIdentity.ItemRevealFacts.Add(
				bItemSourceValid
					&& State.AbilityItemRevealTracker.HasBeenRevealed(
						ItemSource,
						Owner)
					? 1
					: 0);
		}
		OutIdentity.ActivePositions.Reserve(State.ActivePositions.Num());
		for (const FBattleActivePositionState& Position : State.ActivePositions)
		{
			FVoluntarySwitchActiveIdentity& Identity =
				OutIdentity.ActivePositions.AddDefaulted_GetRef();
			Identity.ActiveSlotId = Position.ActiveSlotId;
			Identity.TrainerId = Position.TrainerId;
			Identity.BattlerId = Position.BattlerId;
			Identity.bAvailable = Position.bAvailable;
		}
		for (const FBattleHeldItemInstanceState& Item : State.HeldItemLedger.GetStates())
		{
			OutIdentity.HeldItemStates.Add(Item);
		}
		for (const FBattleTriggerRegistrationState& Registration :
			State.TriggerFramework.GetActiveRegistrations())
		{
			OutIdentity.TriggerRegistrations.Add(Registration);
		}
		return true;
	}

	bool IsPreMoveCheckpointIdentityCurrent(
		const FBattleEngineState& State,
		const FPreMoveCheckpointIdentity& Identity)
	{
		const FBattleResolutionCommitIdentity& Commit = Identity.CommitIdentity;
		if (!FBattleResolutionCommit::IsIdentityCurrent(State, Commit)
			|| State.LockedActions.Num() != Identity.ExpectedLockedActionCount
			|| State.OrderedEvents.Num() != Identity.ExpectedEventCount
			|| State.Trainers.Num() != Identity.ExpectedTrainerCount
			|| State.PendingDecisionRequests.Num()
				!= Identity.ExpectedPendingDecisionRequestCount
			|| State.PendingReplacements.Num() != Identity.ExpectedPendingReplacementCount
			|| State.AvailableOpponentRemovalCheckpoints.Num()
				!= Identity.ExpectedOpponentRemovalCheckpointCount
			|| State.TriggerFramework.GetPendingDispatchCount()
				!= Identity.ExpectedPendingTriggerDispatchCount
			|| State.TriggerFramework.GetPendingEffectRequestCount()
				!= Identity.ExpectedPendingTriggerEffectCount
			|| State.TriggerFramework.GetPendingLifecycleFactCount()
				!= Identity.ExpectedPendingTriggerLifecycleCount
			|| State.NextConditionCreationOrdinal
				!= Identity.ExpectedNextConditionCreationOrdinal
			|| State.NextTriggerReentrancyToken
				!= Identity.ExpectedNextTriggerReentrancyToken
			|| State.Battlers.Num() != Identity.Battlers.Num()
			|| State.Battlers.Num() != Identity.ExactBattlers.Num()
			|| State.Battlers.Num() != Identity.AbilityRevealFacts.Num()
			|| State.Battlers.Num() != Identity.ItemRevealFacts.Num()
			|| State.ActivePositions.Num() != Identity.ActivePositions.Num()
			|| State.HeldItemLedger.GetStates().Num()
				!= Identity.HeldItemStates.Num()
			|| State.TriggerFramework.GetActiveRegistrations().Num()
				!= Identity.TriggerRegistrations.Num()
			|| !State.LockedActions.IsValidIndex(Commit.ExpectedLockedActionIndex)
			|| !ArePivotLockedActionsIdentical(
				State.LockedActions[Commit.ExpectedLockedActionIndex],
				Identity.ExpectedAction))
		{
			return false;
		}

		for (const FVoluntarySwitchBattlerIdentity& Expected : Identity.Battlers)
		{
			const FBattleBattlerState* Battler = State.FindBattler(Expected.BattlerId);
			if (Battler == nullptr || !MatchesVoluntarySwitchBattlerIdentity(*Battler, Expected))
			{
				return false;
			}
		}
		for (int32 BattlerIndex = 0;
			BattlerIndex < Identity.ExactBattlers.Num();
			++BattlerIndex)
		{
			if (!ArePreMoveBattlersIdentical(
					State.Battlers[BattlerIndex],
					Identity.ExactBattlers[BattlerIndex]))
			{
				return false;
			}
			const FBattleBattlerState& Battler = State.Battlers[BattlerIndex];
			FBattleTriggerSubject Owner;
			if (!FBattleTriggerSubject::TryCreateBattler(Battler.BattlerId, Owner))
			{
				return false;
			}
			FBattleTriggerSourceDefinition AbilitySource;
			const bool bAbilityRevealed =
				FBattleTriggerSourceDefinition::TryCreateAbility(
					Battler.AbilityId,
					AbilitySource)
				&& State.AbilityItemRevealTracker.HasBeenRevealed(
					AbilitySource,
					Owner);
			FBattleTriggerSourceDefinition ItemSource;
			const bool bItemRevealed = Battler.HeldItem.CurrentItemId.IsValid()
				&& FBattleTriggerSourceDefinition::TryCreateItem(
					Battler.HeldItem.CurrentItemId,
					ItemSource)
				&& State.AbilityItemRevealTracker.HasBeenRevealed(
					ItemSource,
					Owner);
			if ((bAbilityRevealed ? 1 : 0)
					!= Identity.AbilityRevealFacts[BattlerIndex]
				|| (bItemRevealed ? 1 : 0)
					!= Identity.ItemRevealFacts[BattlerIndex])
			{
				return false;
			}
		}
		for (const FVoluntarySwitchActiveIdentity& Expected : Identity.ActivePositions)
		{
			const FBattleActivePositionState* Position =
				State.FindActivePosition(Expected.ActiveSlotId);
			if (Position == nullptr
				|| Position->bAvailable != Expected.bAvailable
				|| Position->TrainerId != Expected.TrainerId
				|| Position->BattlerId != Expected.BattlerId)
			{
				return false;
			}
		}
		for (int32 ItemIndex = 0;
			ItemIndex < Identity.HeldItemStates.Num();
			++ItemIndex)
		{
			if (!(State.HeldItemLedger.GetStates()[ItemIndex]
				== Identity.HeldItemStates[ItemIndex]))
			{
				return false;
			}
		}
		const TArray<FBattleTriggerRegistrationState> CurrentRegistrations =
			State.TriggerFramework.GetActiveRegistrations();
		for (int32 RegistrationIndex = 0;
			RegistrationIndex < Identity.TriggerRegistrations.Num();
			++RegistrationIndex)
		{
			if (!ArePreMoveTriggerRegistrationsIdentical(
					CurrentRegistrations[RegistrationIndex],
					Identity.TriggerRegistrations[RegistrationIndex]))
			{
				return false;
			}
		}

		const FBattleBattlerState* Actor = State.FindBattler(Identity.ExpectedActorId);
		if (Actor == nullptr
			|| Actor->BattlerId
				!= Identity.ExpectedAction.Decision.GetActingBattlerId())
		{
			return false;
		}
		if (Identity.ExpectedMoveSlotNumber == 255)
		{
			return Identity.ExpectedAction.Decision.GetMoveId()
				== FBattleBuiltInMoveDefinitions::GetStruggleMoveId();
		}
		const FBattleMoveSlotState* MoveSlot = Actor->Moves.FindByPredicate(
			[&Identity](const FBattleMoveSlotState& Candidate)
			{
				return Candidate.SlotIndex == Identity.ExpectedMoveSlotNumber;
			});
		return MoveSlot != nullptr
			&& MoveSlot->MoveId == Identity.ExpectedAction.Decision.GetMoveId()
			&& MoveSlot->CurrentPP == Identity.ExpectedCurrentPP
			&& MoveSlot->MaxPP == Identity.ExpectedMaximumPP;
	}

	struct FPreMoveCheckpointDelta
	{
		FAtomicCheckpointCommonDelta State;
		FBattleLockedActionState Action;
	};

	bool TryCapturePreMoveCheckpointDelta(
		const FPreMoveCheckpointPreparation& Preparation,
		const FPreMoveCheckpointIdentity& Identity,
		FPreMoveCheckpointDelta& OutDelta)
	{
		OutDelta = FPreMoveCheckpointDelta();
		if (Preparation.Action.ActionId
			!= Identity.CommitIdentity.OwningActionId
			|| !TryCaptureAtomicCheckpointCommonDelta(
				Preparation.Common,
				OutDelta.State))
		{
			return false;
		}
		OutDelta.Action = Preparation.Action;
		return AreAtomicCheckpointCommonDeltaRecordsValid(
			Identity.Battlers,
			Identity.ActivePositions,
			OutDelta.State);
	}

	void ApplyPreMoveCheckpointDelta(
		FBattleEngineState& State,
		const FPreMoveCheckpointIdentity& Identity,
		const FPreMoveCheckpointDelta& Delta)
	{
		FBattleLockedActionState* Action = State.LockedActions.FindByPredicate(
			[&Identity](const FBattleLockedActionState& Candidate)
			{
				return Candidate.ActionId
					== Identity.CommitIdentity.OwningActionId;
			});
		check(Action != nullptr);
		ApplyAtomicCheckpointCommonDelta(State, Delta.State);
		*Action = Delta.Action;
	}

	bool TryPublishPreMoveCheckpointRejection(
		FBattleEngineState& State,
		const FResolutionId ResolutionId,
		const FActionId ActionId,
		const EBattleRejectionReason Reason,
		const FTrainerId TrainerId,
		const FBattlerId BattlerId,
		const FBattleEventSource& Source,
		FBattleResolution& OutResolution)
	{
		OutResolution = FBattleResolution();
		FBattleResolutionCommitPlan RejectedPlan;
		if (!FBattleResolutionCommit::TryBuildRejectedPlan(
				State,
				ResolutionId,
				ActionId,
				Reason,
				TrainerId,
				BattlerId,
				EBattleActionKind::Fight,
				Source,
				RejectedPlan))
		{
			return false;
		}
		OutResolution = FBattleResolutionCommit::PublishPrepared(State, RejectedPlan);
		return true;
	}

	/** Minimal copied battler facts read by target-spec, target-event, and queue-boundary preparation. */
	struct FTargetResolutionBattlerIdentity
	{
		FTrainerId TrainerId;
		FBattlerId BattlerId;
		FPartySlotId PartySlotId;
		bool bEgg = false;
		bool bFainted = false;
		bool bCaptured = false;
		bool bRemoved = false;
	};

	/** Exact caller-serialized identity for one committed Fight target checkpoint. */
	struct FTargetResolutionCheckpointIdentity
	{
		FBattleResolutionCommitIdentity CommitIdentity;
		int32 ExpectedLockedActionCount = 0;
		int32 ExpectedEventCount = 0;
		int32 ExpectedTrainerCount = 0;
		int32 ExpectedPendingTriggerDispatchCount = 0;
		int32 ExpectedPendingTriggerEffectCount = 0;
		int32 ExpectedPendingTriggerLifecycleCount = 0;
		uint64 ExpectedNextTriggerReentrancyToken = 0;
		EBattleOutcome ExpectedOutcome = EBattleOutcome::InProgress;
		EBattleOutcomeCause ExpectedOutcomeCause = EBattleOutcomeCause::None;
		FBattleLockedActionState ExpectedAction;
		FTrainerId ExpectedOwnerId;
		FBattlerId ExpectedActorId;
		FActiveSlotId ExpectedActingSlotId;
		FBattleBattlerState ExpectedActor;
		uint8 ExpectedMoveSlotNumber = 255;
		int32 ExpectedCurrentPP = 0;
		int32 ExpectedMaximumPP = 0;
		bool bExpectedReleasingCharge = false;
		TArray<FBattleConditionState> ExpectedActorVolatiles;
		TArray<FTargetResolutionBattlerIdentity> Battlers;
		TArray<FVoluntarySwitchActiveIdentity> ActivePositions;
		TArray<FBattleTriggerRegistrationState> TriggerRegistrations;
		TArray<FBattleRandomDraw> ExpectedRandomTrace;
		TOptional<FBattleDecisionRequest> ExpectedPendingDecision;
		TArray<FBattleDecisionRequest> ExpectedPendingDecisionRequests;
		TArray<FBattlePendingReplacementState> ExpectedPendingReplacements;
		FBattleTargetResolutionSpec PreparedTargetSpec;
	};

	/** Target preparation owns only the action, actor cleanup, and boundary fields. */
	struct FTargetResolutionCheckpointPreparation
	{
		FBattleLockedActionState Action;
		FBattlerId ActorId;
		TArray<FBattleConditionState> ActorVolatiles;
		FBattleTriggerFramework TriggerFramework;
		uint64 NextTriggerReentrancyToken = 0;
		uint64 NextEventOrdinal = 0;
		int32 CurrentLockedActionIndex = INDEX_NONE;
		EBattlePhase Phase = EBattlePhase::Resolving;
		TOptional<FBattleDecisionRequest> PendingDecision;
		TArray<FBattleDecisionRequest> PendingDecisionRequests;
		TArray<FBattlePendingReplacementState> PendingReplacements;

		bool Capture(
			const FBattleEngineState& State,
			const FActionId ActionId,
			const FBattlerId InActorId)
		{
			const FBattleLockedActionState* CurrentAction =
				State.LockedActions.FindByPredicate(
					[ActionId](const FBattleLockedActionState& Candidate)
					{
						return Candidate.ActionId == ActionId;
					});
			const FBattleBattlerState* CurrentActor = State.FindBattler(InActorId);
			if (CurrentAction == nullptr || CurrentActor == nullptr)
			{
				return false;
			}
			Action = *CurrentAction;
			ActorId = CurrentActor->BattlerId;
			ActorVolatiles = CurrentActor->Volatiles;
			TriggerFramework = State.TriggerFramework;
			NextTriggerReentrancyToken = State.NextTriggerReentrancyToken;
			NextEventOrdinal = State.NextEventOrdinal;
			CurrentLockedActionIndex = State.CurrentLockedActionIndex;
			Phase = State.Phase;
			PendingDecision = State.PendingDecision;
			PendingDecisionRequests = State.PendingDecisionRequests;
			PendingReplacements = State.PendingReplacements;
			return true;
		}
	};

	/** Reference-only adapter for target cleanup and queue-boundary helpers. */
	struct FTargetResolutionCheckpointView
	{
		const FBattleEngineState& Authority;
		const FBattleSetup& Setup;
		const FBattleDefinitionCatalog& Catalog;
		const uint64& StateVersion;
		const FTurnId& TurnId;
		const EBattleEncounterKind& EncounterKind;
		const EBattleFormat& Format;
		const TArray<FBattleTrainerState>& Trainers;
		const TArray<FBattleBattlerState>& Battlers;
		const TArray<FBattleActivePositionState>& ActivePositions;
		const FBattleCompiledEncounterPolicies& CompiledEncounterPolicies;
		const TArray<FBattleLockedActionState>& LockedActions;
		const EBattleOutcome& Outcome;
		int32& CurrentLockedActionIndex;
		EBattlePhase& Phase;
		TOptional<FBattleDecisionRequest>& PendingDecision;
		TArray<FBattleDecisionRequest>& PendingDecisionRequests;
		TArray<FBattlePendingReplacementState>& PendingReplacements;
		FBattleTriggerFramework& TriggerFramework;
		uint64& NextTriggerReentrancyToken;
		uint64& NextEventOrdinal;

		FTargetResolutionCheckpointView(
			const FBattleEngineState& InAuthority,
			FTargetResolutionCheckpointPreparation& Preparation)
			: Authority(InAuthority)
			, Setup(InAuthority.Setup)
			, Catalog(InAuthority.Catalog)
			, StateVersion(InAuthority.StateVersion)
			, TurnId(InAuthority.TurnId)
			, EncounterKind(InAuthority.EncounterKind)
			, Format(InAuthority.Format)
			, Trainers(InAuthority.Trainers)
			, Battlers(InAuthority.Battlers)
			, ActivePositions(InAuthority.ActivePositions)
			, CompiledEncounterPolicies(InAuthority.CompiledEncounterPolicies)
			, LockedActions(InAuthority.LockedActions)
			, Outcome(InAuthority.Outcome)
			, CurrentLockedActionIndex(Preparation.CurrentLockedActionIndex)
			, Phase(Preparation.Phase)
			, PendingDecision(Preparation.PendingDecision)
			, PendingDecisionRequests(Preparation.PendingDecisionRequests)
			, PendingReplacements(Preparation.PendingReplacements)
			, TriggerFramework(Preparation.TriggerFramework)
			, NextTriggerReentrancyToken(Preparation.NextTriggerReentrancyToken)
			, NextEventOrdinal(Preparation.NextEventOrdinal)
		{
		}

		[[nodiscard]] const FBattleTrainerState* FindTrainer(
			const FTrainerId TrainerId) const
		{
			return Authority.FindTrainer(TrainerId);
		}

		[[nodiscard]] const FBattleBattlerState* FindBattler(
			const FBattlerId BattlerId) const
		{
			return Authority.FindBattler(BattlerId);
		}

		[[nodiscard]] const FBattleActivePositionState* FindActivePosition(
			const FActiveSlotId ActiveSlotId) const
		{
			return Authority.FindActivePosition(ActiveSlotId);
		}
	};

	struct FTargetResolutionTriggerCleanupView
	{
		FBattleTriggerFramework& TriggerFramework;
		uint64& NextTriggerReentrancyToken;
	};

	bool TryClearTargetResolutionChargeState(
		FTargetResolutionCheckpointPreparation& Preparation,
		const EBattleTriggerCleanupReason Reason)
	{
		FTargetResolutionTriggerCleanupView CleanupView{
			Preparation.TriggerFramework,
			Preparation.NextTriggerReentrancyToken};
		for (const FConditionId& Id : {
			FBattleVolatileRules::GetChargingId(),
			FBattleVolatileRules::GetFlySemiInvulnerableId()})
		{
			if (!Preparation.ActorVolatiles.ContainsByPredicate(
					[&Id](const FBattleConditionState& Condition)
					{
						return Condition.ConditionId == Id;
					}))
			{
				continue;
			}
			if (!TryCleanupVolatileTriggers(
					CleanupView,
					Id,
					Preparation.ActorId,
					Reason))
			{
				return false;
			}
			Preparation.ActorVolatiles.RemoveAll(
				[&Id](const FBattleConditionState& Condition)
				{
					return Condition.ConditionId == Id;
				});
		}
		return true;
	}

	/** Narrow, fully prepared state assignment owned only by target resolution. */
	struct FTargetResolutionCheckpointDelta
	{
		FBattleLockedActionState Action;
		FBattlerId ActorId;
		TArray<FBattleConditionState> ActorVolatiles;
		FBattleTriggerFramework TriggerFramework;
		uint64 NextTriggerReentrancyToken = 0;
		int32 NextLockedActionIndex = INDEX_NONE;
		EBattlePhase Phase = EBattlePhase::Resolving;
		TOptional<FBattleDecisionRequest> PendingDecision;
		TArray<FBattleDecisionRequest> PendingDecisionRequests;
		TArray<FBattlePendingReplacementState> PendingReplacements;
	};

	bool AreTargetResolutionBattlerIdentitiesIdentical(
		const FTargetResolutionBattlerIdentity& Left,
		const FTargetResolutionBattlerIdentity& Right)
	{
		return Left.TrainerId == Right.TrainerId
			&& Left.BattlerId == Right.BattlerId
			&& Left.PartySlotId == Right.PartySlotId
			&& Left.bEgg == Right.bEgg
			&& Left.bFainted == Right.bFainted
			&& Left.bCaptured == Right.bCaptured
			&& Left.bRemoved == Right.bRemoved;
	}

	bool AreTargetResolutionActiveIdentitiesIdentical(
		const FVoluntarySwitchActiveIdentity& Left,
		const FVoluntarySwitchActiveIdentity& Right)
	{
		return Left.ActiveSlotId == Right.ActiveSlotId
			&& Left.TrainerId == Right.TrainerId
			&& Left.BattlerId == Right.BattlerId
			&& Left.bAvailable == Right.bAvailable;
	}

	bool AreTargetResolutionPositionFactsIdentical(
		const FBattleTargetPositionFacts& Left,
		const FBattleTargetPositionFacts& Right)
	{
		return Left.ActiveSlotId == Right.ActiveSlotId
			&& Left.BattlerId == Right.BattlerId
			&& Left.State == Right.State
			&& Left.bSemiInvulnerable == Right.bSemiInvulnerable;
	}

	bool AreTargetResolutionSpecsIdentical(
		const FBattleTargetResolutionSpec& Left,
		const FBattleTargetResolutionSpec& Right)
	{
		if (Left.TargetClass != Right.TargetClass
			|| Left.UserSlotId != Right.UserSlotId
			|| Left.UserBattlerId != Right.UserBattlerId
			|| Left.ExplicitTarget != Right.ExplicitTarget
			|| Left.Positions.Num() != Right.Positions.Num()
			|| Left.RedirectionProposals.Num() != Right.RedirectionProposals.Num()
			|| Left.RandomContext.BattleId != Right.RandomContext.BattleId
			|| Left.RandomContext.TurnId != Right.RandomContext.TurnId
			|| Left.RandomContext.ActionId != Right.RandomContext.ActionId
			|| Left.RandomContext.ResolutionId != Right.RandomContext.ResolutionId
			|| Left.RandomContext.RulePurpose != Right.RandomContext.RulePurpose)
		{
			return false;
		}
		for (int32 Index = 0; Index < Left.Positions.Num(); ++Index)
		{
			if (!AreTargetResolutionPositionFactsIdentical(
					Left.Positions[Index],
					Right.Positions[Index]))
			{
				return false;
			}
		}
		for (int32 Index = 0; Index < Left.RedirectionProposals.Num(); ++Index)
		{
			if (Left.RedirectionProposals[Index].ProposedTarget
				!= Right.RedirectionProposals[Index].ProposedTarget)
			{
				return false;
			}
		}
		return true;
	}

	bool AreTargetResolutionConditionsIdentical(
		const TConstArrayView<FBattleConditionState> Left,
		const TConstArrayView<FBattleConditionState> Right)
	{
		if (Left.Num() != Right.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < Left.Num(); ++Index)
		{
			if (Left[Index].ConditionId != Right[Index].ConditionId
				|| Left[Index].RemainingTurns != Right[Index].RemainingTurns
				|| Left[Index].LayerCount != Right[Index].LayerCount
				|| Left[Index].CreationOrdinal != Right[Index].CreationOrdinal
				|| Left[Index].SourceBattlerId != Right[Index].SourceBattlerId)
			{
				return false;
			}
		}
		return true;
	}

	bool AreTargetResolutionRequestsIdentical(
		const TConstArrayView<FBattleDecisionRequest> Left,
		const TConstArrayView<FBattleDecisionRequest> Right)
	{
		if (Left.Num() != Right.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < Left.Num(); ++Index)
		{
			if (!ArePivotDecisionRequestsIdentical(Left[Index], Right[Index]))
			{
				return false;
			}
		}
		return true;
	}

	bool AreTargetResolutionPendingDecisionIdentical(
		const TOptional<FBattleDecisionRequest>& Left,
		const TOptional<FBattleDecisionRequest>& Right)
	{
		return Left.IsSet() == Right.IsSet()
			&& (!Left.IsSet()
				|| ArePivotDecisionRequestsIdentical(
					Left.GetValue(),
					Right.GetValue()));
	}

	bool AreTargetResolutionPendingReplacementsIdentical(
		const TConstArrayView<FBattlePendingReplacementState> Left,
		const TConstArrayView<FBattlePendingReplacementState> Right)
	{
		if (Left.Num() != Right.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < Left.Num(); ++Index)
		{
			if (Left[Index].TrainerId != Right[Index].TrainerId
				|| Left[Index].ActiveSlotId != Right[Index].ActiveSlotId)
			{
				return false;
			}
		}
		return true;
	}

	bool TryBuildTargetResolutionCheckpointSpec(
		const FBattleEngineState& State,
		const FResolutionId ResolutionId,
		const FBattleLockedActionState& Action,
		FBattleTargetResolutionSpec& OutSpec,
		TArray<FTargetResolutionBattlerIdentity>& OutBattlerFacts,
		TArray<FVoluntarySwitchActiveIdentity>& OutActivePositions)
	{
		OutSpec = FBattleTargetResolutionSpec();
		OutBattlerFacts.Reset();
		OutActivePositions.Reset();
		const FBattleBattlerState* User = State.FindBattler(
			Action.Decision.GetActingBattlerId());
		const FBattleActivePositionState* UserPosition = State.FindActivePosition(
			Action.OrderKey.ActingSlotId);
		if (!ResolutionId.IsValid()
			|| Action.Decision.GetActionKind() != EBattleActionKind::Fight
			|| !Action.bStarted
			|| !Action.bMoveCommitted
			|| Action.TargetResolution.IsSet()
			|| Action.EffectExecutionState != EBattleLockedEffectExecutionState::Pending
			|| Action.bFinished
			|| User == nullptr
			|| UserPosition == nullptr
			|| !UserPosition->bAvailable
			|| UserPosition->TrainerId != Action.Decision.GetDecisionOwnerTrainerId()
			|| UserPosition->BattlerId != User->BattlerId
			|| User->TrainerId != Action.Decision.GetDecisionOwnerTrainerId()
			|| !IsLivingSelectableBattler(User))
		{
			return false;
		}

		OutBattlerFacts.Reserve(State.Battlers.Num());
		for (const FBattleBattlerState& Battler : State.Battlers)
		{
			FTargetResolutionBattlerIdentity& Identity =
				OutBattlerFacts.AddDefaulted_GetRef();
			Identity.TrainerId = Battler.TrainerId;
			Identity.BattlerId = Battler.BattlerId;
			Identity.PartySlotId = Battler.PartySlotId;
			Identity.bEgg = Battler.bEgg;
			Identity.bFainted = Battler.bFainted;
			Identity.bCaptured = Battler.bCaptured;
			Identity.bRemoved = Battler.bRemoved;
		}

		OutActivePositions.Reserve(State.ActivePositions.Num());
		for (const FBattleActivePositionState& Position : State.ActivePositions)
		{
			FVoluntarySwitchActiveIdentity& Identity =
				OutActivePositions.AddDefaulted_GetRef();
			Identity.ActiveSlotId = Position.ActiveSlotId;
			Identity.TrainerId = Position.TrainerId;
			Identity.BattlerId = Position.BattlerId;
			Identity.bAvailable = Position.bAvailable;
		}

		OutSpec.TargetClass = Action.TargetClass;
		OutSpec.UserSlotId = UserPosition->ActiveSlotId;
		OutSpec.UserBattlerId = User->BattlerId;
		OutSpec.Positions = BuildBattleEngineTargetPositions(State);
		if (IsBattleEngineExplicitTargetClass(Action.TargetClass))
		{
			OutSpec.ExplicitTarget.ActiveSlotId = Action.Decision.GetActiveTargetId();
			const FBattleActivePositionState* CurrentTargetPosition =
				State.FindActivePosition(OutSpec.ExplicitTarget.ActiveSlotId);
			if (CurrentTargetPosition != nullptr
				&& CurrentTargetPosition->BattlerId.IsValid())
			{
				OutSpec.ExplicitTarget.BattlerId = CurrentTargetPosition->BattlerId;
			}
			else
			{
				OutSpec.ExplicitTarget.BattlerId = Action.SelectedTargetBattlerId;
				const FBattleBattlerState* OriginalTarget = State.FindBattler(
					Action.SelectedTargetBattlerId);
				FBattleTargetPositionFacts* EmptySelectedPosition =
					OutSpec.Positions.FindByPredicate(
						[&OutSpec](const FBattleTargetPositionFacts& Position)
						{
							return Position.ActiveSlotId
								== OutSpec.ExplicitTarget.ActiveSlotId;
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
		if (Action.TargetClass == EBattleTargetClass::RandomLegalOpponent)
		{
			OutSpec.RandomContext.BattleId = State.Setup.GetBattleId();
			OutSpec.RandomContext.TurnId = State.TurnId;
			OutSpec.RandomContext.ActionId = Action.ActionId;
			OutSpec.RandomContext.ResolutionId = ResolutionId;
			OutSpec.RandomContext.RulePurpose =
				FBattleTargetResolver::GetRandomLegalOpponentRulePurpose();
		}
		return true;
	}

	bool TryCaptureTargetResolutionCheckpointIdentity(
		const FBattleEngineState& State,
		const FResolutionId ResolutionId,
		const FBattleLockedActionState& Action,
		FTargetResolutionCheckpointIdentity& OutIdentity)
	{
		OutIdentity = FTargetResolutionCheckpointIdentity();
		FBattleResolutionCommitIdentity CommitIdentity;
		FBattleTargetResolutionSpec PreparedSpec;
		TArray<FTargetResolutionBattlerIdentity> Battlers;
		TArray<FVoluntarySwitchActiveIdentity> ActivePositions;
		if (!FBattleResolutionCommit::TryCaptureIdentity(
				State,
				ResolutionId,
				Action.ActionId,
				CommitIdentity)
			|| !TryBuildTargetResolutionCheckpointSpec(
				State,
				ResolutionId,
				Action,
				PreparedSpec,
				Battlers,
				ActivePositions))
		{
			return false;
		}

		const FBattleBattlerState* Actor = State.FindBattler(
			Action.Decision.GetActingBattlerId());
		if (Actor == nullptr)
		{
			return false;
		}
		const bool bStruggle = Action.Decision.GetMoveId()
			== FBattleBuiltInMoveDefinitions::GetStruggleMoveId();
		const FBattleMoveSlotState* MoveSlot = bStruggle
			? nullptr
			: Actor->Moves.FindByPredicate(
				[&Action](const FBattleMoveSlotState& Candidate)
				{
					return Candidate.MoveId == Action.Decision.GetMoveId();
				});
		if (!bStruggle && MoveSlot == nullptr)
		{
			return false;
		}

		OutIdentity.CommitIdentity = CommitIdentity;
		OutIdentity.ExpectedLockedActionCount = State.LockedActions.Num();
		OutIdentity.ExpectedEventCount = State.OrderedEvents.Num();
		OutIdentity.ExpectedTrainerCount = State.Trainers.Num();
		OutIdentity.ExpectedPendingTriggerDispatchCount =
			State.TriggerFramework.GetPendingDispatchCount();
		OutIdentity.ExpectedPendingTriggerEffectCount =
			State.TriggerFramework.GetPendingEffectRequestCount();
		OutIdentity.ExpectedPendingTriggerLifecycleCount =
			State.TriggerFramework.GetPendingLifecycleFactCount();
		OutIdentity.ExpectedNextTriggerReentrancyToken =
			State.NextTriggerReentrancyToken;
		OutIdentity.ExpectedOutcome = State.Outcome;
		OutIdentity.ExpectedOutcomeCause = State.OutcomeCause;
		OutIdentity.ExpectedAction = Action;
		OutIdentity.ExpectedOwnerId = Action.Decision.GetDecisionOwnerTrainerId();
		OutIdentity.ExpectedActorId = Action.Decision.GetActingBattlerId();
		OutIdentity.ExpectedActingSlotId = Action.OrderKey.ActingSlotId;
		OutIdentity.ExpectedActor = *Actor;
		if (MoveSlot != nullptr)
		{
			OutIdentity.ExpectedMoveSlotNumber = MoveSlot->SlotIndex;
			OutIdentity.ExpectedCurrentPP = MoveSlot->CurrentPP;
			OutIdentity.ExpectedMaximumPP = MoveSlot->MaxPP;
		}
		OutIdentity.bExpectedReleasingCharge = IsReleasingCharge(
			State,
			*Actor,
			Action.Decision.GetMoveId());
		OutIdentity.ExpectedActorVolatiles = Actor->Volatiles;
		OutIdentity.Battlers = MoveTemp(Battlers);
		OutIdentity.ActivePositions = MoveTemp(ActivePositions);
		for (const FBattleRandomDraw& Draw : State.Random->GetTrace())
		{
			OutIdentity.ExpectedRandomTrace.Add(Draw);
		}
		for (const FBattleTriggerRegistrationState& Registration :
			State.TriggerFramework.GetActiveRegistrations())
		{
			OutIdentity.TriggerRegistrations.Add(Registration);
		}
		OutIdentity.ExpectedPendingDecision = State.PendingDecision;
		OutIdentity.ExpectedPendingDecisionRequests = State.PendingDecisionRequests;
		OutIdentity.ExpectedPendingReplacements = State.PendingReplacements;
		OutIdentity.PreparedTargetSpec = MoveTemp(PreparedSpec);
		return true;
	}

	bool IsTargetResolutionCheckpointIdentityCurrent(
		const FBattleEngineState& State,
		const FTargetResolutionCheckpointIdentity& Identity)
	{
		const FBattleResolutionCommitIdentity& Commit = Identity.CommitIdentity;
		if (!FBattleResolutionCommit::IsIdentityCurrent(State, Commit)
			|| State.LockedActions.Num() != Identity.ExpectedLockedActionCount
			|| State.OrderedEvents.Num() != Identity.ExpectedEventCount
			|| State.Trainers.Num() != Identity.ExpectedTrainerCount
			|| State.Outcome != Identity.ExpectedOutcome
			|| State.OutcomeCause != Identity.ExpectedOutcomeCause
			|| State.TriggerFramework.GetPendingDispatchCount()
				!= Identity.ExpectedPendingTriggerDispatchCount
			|| State.TriggerFramework.GetPendingEffectRequestCount()
				!= Identity.ExpectedPendingTriggerEffectCount
			|| State.TriggerFramework.GetPendingLifecycleFactCount()
				!= Identity.ExpectedPendingTriggerLifecycleCount
			|| State.NextTriggerReentrancyToken
				!= Identity.ExpectedNextTriggerReentrancyToken
			|| State.Battlers.Num() != Identity.Battlers.Num()
			|| State.ActivePositions.Num() != Identity.ActivePositions.Num()
			|| State.TriggerFramework.GetActiveRegistrations().Num()
				!= Identity.TriggerRegistrations.Num()
			|| State.Random->GetTrace().Num()
				!= Identity.ExpectedRandomTrace.Num()
			|| !State.LockedActions.IsValidIndex(Commit.ExpectedLockedActionIndex)
			|| !ArePivotLockedActionsIdentical(
				State.LockedActions[Commit.ExpectedLockedActionIndex],
				Identity.ExpectedAction)
			|| !AreTargetResolutionPendingDecisionIdentical(
				State.PendingDecision,
				Identity.ExpectedPendingDecision)
			|| !AreTargetResolutionRequestsIdentical(
				State.PendingDecisionRequests,
				Identity.ExpectedPendingDecisionRequests)
			|| !AreTargetResolutionPendingReplacementsIdentical(
				State.PendingReplacements,
				Identity.ExpectedPendingReplacements))
		{
			return false;
		}
		for (int32 Index = 0; Index < Identity.ExpectedRandomTrace.Num(); ++Index)
		{
			if (State.Random->GetTrace()[Index]
				!= Identity.ExpectedRandomTrace[Index])
			{
				return false;
			}
		}

		FBattleTargetResolutionSpec CurrentSpec;
		TArray<FTargetResolutionBattlerIdentity> CurrentBattlers;
		TArray<FVoluntarySwitchActiveIdentity> CurrentActivePositions;
		if (!TryBuildTargetResolutionCheckpointSpec(
				State,
				Commit.ResolutionId,
				State.LockedActions[Commit.ExpectedLockedActionIndex],
				CurrentSpec,
				CurrentBattlers,
				CurrentActivePositions)
			|| !AreTargetResolutionSpecsIdentical(
				CurrentSpec,
				Identity.PreparedTargetSpec))
		{
			return false;
		}
		for (int32 Index = 0; Index < Identity.Battlers.Num(); ++Index)
		{
			if (!AreTargetResolutionBattlerIdentitiesIdentical(
					CurrentBattlers[Index],
					Identity.Battlers[Index]))
			{
				return false;
			}
		}
		for (int32 Index = 0; Index < Identity.ActivePositions.Num(); ++Index)
		{
			if (!AreTargetResolutionActiveIdentitiesIdentical(
					CurrentActivePositions[Index],
					Identity.ActivePositions[Index]))
			{
				return false;
			}
		}

		const FBattleBattlerState* Actor = State.FindBattler(Identity.ExpectedActorId);
		if (Actor == nullptr
			|| !ArePreMoveBattlersIdentical(*Actor, Identity.ExpectedActor)
			|| Actor->TrainerId != Identity.ExpectedOwnerId
			|| !AreTargetResolutionConditionsIdentical(
				Actor->Volatiles,
				Identity.ExpectedActorVolatiles)
			|| IsReleasingCharge(
				State,
				*Actor,
				Identity.ExpectedAction.Decision.GetMoveId())
				!= Identity.bExpectedReleasingCharge)
		{
			return false;
		}
		if (Identity.ExpectedMoveSlotNumber == 255)
		{
			if (Identity.ExpectedAction.Decision.GetMoveId()
				!= FBattleBuiltInMoveDefinitions::GetStruggleMoveId())
			{
				return false;
			}
		}
		else
		{
			const FBattleMoveSlotState* MoveSlot = Actor->Moves.FindByPredicate(
				[&Identity](const FBattleMoveSlotState& Candidate)
				{
					return Candidate.SlotIndex == Identity.ExpectedMoveSlotNumber;
				});
			if (MoveSlot == nullptr
				|| MoveSlot->MoveId != Identity.ExpectedAction.Decision.GetMoveId()
				|| MoveSlot->CurrentPP != Identity.ExpectedCurrentPP
				|| MoveSlot->MaxPP != Identity.ExpectedMaximumPP)
			{
				return false;
			}
		}

		const TArray<FBattleTriggerRegistrationState> CurrentRegistrations =
			State.TriggerFramework.GetActiveRegistrations();
		for (int32 Index = 0; Index < Identity.TriggerRegistrations.Num(); ++Index)
		{
			if (!ArePreMoveTriggerRegistrationsIdentical(
					CurrentRegistrations[Index],
					Identity.TriggerRegistrations[Index]))
			{
				return false;
			}
		}
		return true;
	}

	template <typename TState>
	bool TryMakeTargetResolutionEventSpec(
		const TState& Projection,
		const FResolutionId ResolutionId,
		const FBattleLockedActionState& Action,
		const FBattleTargetResolutionResult& TargetResolution,
		FBattleEventSpec& OutSpec)
	{
		OutSpec = FBattleEventSpec();
		if (!ResolutionId.IsValid()
			|| !Action.ActionId.IsValid()
			|| (TargetResolution.Outcome != EBattleTargetResolutionOutcome::Resolved
				&& TargetResolution.Outcome
					!= EBattleTargetResolutionOutcome::NoLegalTarget))
		{
			return false;
		}
		OutSpec.BattleId = Projection.Setup.GetBattleId();
		OutSpec.TurnId = Projection.TurnId;
		OutSpec.ActionId = Action.ActionId;
		OutSpec.ResolutionId = ResolutionId;
		OutSpec.Type = EBattleEventType::TargetsResolved;
		OutSpec.Cause = EBattleEventCause::Targeting;
		OutSpec.CauseActionKind = EBattleActionKind::Fight;
		OutSpec.Source = SourceFromLockedAction(Projection, Action);
		OutSpec.TargetResolution = FBattleTargetResolutionMetadata{
			TargetResolution.TargetClass,
			TargetResolution.bWasRedirected,
			TargetResolution.bUsedFaintedTargetFallback};
		OutSpec.Visibility.Level = EBattleVisibilityLevel::Public;
		for (const FBattleResolvedTarget& Target : TargetResolution.Targets)
		{
			FBattleEventTarget EventTarget;
			switch (Target.GetKind())
			{
			case EBattleResolvedTargetKind::Battler:
			{
				const FBattleBattlerTarget& BattlerTarget = Target.GetBattler();
				const FBattleBattlerState* Battler = Projection.FindBattler(
					BattlerTarget.BattlerId);
				if (Battler == nullptr
					|| !BattlerTarget.ActiveSlotId.IsValid()
					|| !BattlerTarget.BattlerId.IsValid())
				{
					return false;
				}
				EventTarget.TrainerId = Battler->TrainerId;
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
				return false;
			}
			OutSpec.Targets.Add(MoveTemp(EventTarget));
		}
		return true;
	}

	template <typename TState>
	bool TryMakeTargetResolutionActionEventSpec(
		const TState& Projection,
		const FResolutionId ResolutionId,
		const FBattleLockedActionState& Action,
		const EBattleEventType Type,
		const EBattleEventCause Cause,
		FBattleEventSpec& OutSpec)
	{
		OutSpec = FBattleEventSpec();
		if (!ResolutionId.IsValid()
			|| !Action.ActionId.IsValid()
			|| (Type != EBattleEventType::ActionCanceled
				&& Type != EBattleEventType::ActionCompleted))
		{
			return false;
		}
		OutSpec.BattleId = Projection.Setup.GetBattleId();
		OutSpec.TurnId = Projection.TurnId;
		OutSpec.ActionId = Action.ActionId;
		OutSpec.ResolutionId = ResolutionId;
		OutSpec.Type = Type;
		OutSpec.Cause = Cause;
		OutSpec.CauseActionKind = EBattleActionKind::Fight;
		OutSpec.Source = SourceFromLockedAction(Projection, Action);
		OutSpec.Visibility.Level = EBattleVisibilityLevel::Public;
		return true;
	}

	bool TryCaptureTargetResolutionCheckpointDelta(
		const FTargetResolutionCheckpointPreparation& Preparation,
		const FTargetResolutionCheckpointIdentity& Identity,
		FTargetResolutionCheckpointDelta& OutDelta)
	{
		OutDelta = FTargetResolutionCheckpointDelta();
		if (Preparation.Action.ActionId
				!= Identity.CommitIdentity.OwningActionId
			|| Preparation.ActorId != Identity.ExpectedActorId
			|| !Preparation.Action.TargetResolution.IsSet())
		{
			return false;
		}
		const FBattleLockedActionState& Action = Preparation.Action;
		const bool bNoTarget = Action.TargetResolution.GetValue().Outcome
			== EBattleTargetResolutionOutcome::NoLegalTarget;
		if ((bNoTarget
				&& (!Action.bFinished
					|| Preparation.CurrentLockedActionIndex
						!= Identity.CommitIdentity.ExpectedLockedActionIndex + 1))
			|| (!bNoTarget
				&& (Action.bFinished
					|| Preparation.CurrentLockedActionIndex
						!= Identity.CommitIdentity.ExpectedLockedActionIndex)))
		{
			return false;
		}
		for (const FBattleTriggerRegistrationState& Registration :
			Preparation.TriggerFramework.GetActiveRegistrations())
		{
			if (!Registration.RegistrationId.IsValid())
			{
				return false;
			}
		}

		OutDelta.Action = Action;
		OutDelta.ActorId = Preparation.ActorId;
		OutDelta.ActorVolatiles = Preparation.ActorVolatiles;
		OutDelta.TriggerFramework = Preparation.TriggerFramework;
		OutDelta.NextTriggerReentrancyToken =
			Preparation.NextTriggerReentrancyToken;
		OutDelta.NextLockedActionIndex = Preparation.CurrentLockedActionIndex;
		OutDelta.Phase = Preparation.Phase;
		OutDelta.PendingDecision = Preparation.PendingDecision;
		OutDelta.PendingDecisionRequests = Preparation.PendingDecisionRequests;
		OutDelta.PendingReplacements = Preparation.PendingReplacements;
		return true;
	}

	void ApplyTargetResolutionCheckpointDelta(
		FBattleEngineState& State,
		const FTargetResolutionCheckpointIdentity& Identity,
		const FTargetResolutionCheckpointDelta& Delta)
	{
		FBattleLockedActionState* Action = State.LockedActions.FindByPredicate(
			[&Identity](const FBattleLockedActionState& Candidate)
			{
				return Candidate.ActionId
					== Identity.CommitIdentity.OwningActionId;
			});
		FBattleBattlerState* Actor = State.FindMutableBattler(Delta.ActorId);
		check(Action != nullptr && Actor != nullptr);
		*Action = Delta.Action;
		Actor->Volatiles = Delta.ActorVolatiles;
		State.TriggerFramework = Delta.TriggerFramework;
		State.NextTriggerReentrancyToken = Delta.NextTriggerReentrancyToken;
		State.CurrentLockedActionIndex = Delta.NextLockedActionIndex;
		State.Phase = Delta.Phase;
		State.PendingDecision = Delta.PendingDecision;
		State.PendingDecisionRequests = Delta.PendingDecisionRequests;
		State.PendingReplacements = Delta.PendingReplacements;
	}

	bool TryPublishTargetResolutionCheckpointRejection(
		FBattleEngineState& State,
		const FResolutionId ResolutionId,
		const FActionId ActionId,
		const EBattleRejectionReason Reason,
		const FTrainerId TrainerId,
		const FBattlerId BattlerId,
		const FBattleEventSource& Source,
		FBattleResolution& OutResolution)
	{
		OutResolution = FBattleResolution();
		FBattleResolutionCommitPlan RejectedPlan;
		if (!FBattleResolutionCommit::TryBuildRejectedPlan(
				State,
				ResolutionId,
				ActionId,
				Reason,
				TrainerId,
				BattlerId,
				EBattleActionKind::Fight,
				Source,
				RejectedPlan))
		{
			return false;
		}
		OutResolution = FBattleResolutionCommit::PublishPrepared(State, RejectedPlan);
		return true;
	}

	bool AreMoveEffectsDescriptorsIdentical(
		const FBattleMoveEffectDescriptor& Left,
		const FBattleMoveEffectDescriptor& Right)
	{
		return Left.Order == Right.Order
			&& Left.Kind == Right.Kind
			&& Left.Target == Right.Target
			&& Left.ConditionId == Right.ConditionId
			&& Left.ItemId == Right.ItemId
			&& Left.Stat == Right.Stat
			&& Left.ChanceNumerator == Right.ChanceNumerator
			&& Left.ChanceDenominator == Right.ChanceDenominator
			&& Left.MagnitudeNumerator == Right.MagnitudeNumerator
			&& Left.MagnitudeDenominator == Right.MagnitudeDenominator
			&& Left.MinimumCount == Right.MinimumCount
			&& Left.MaximumCount == Right.MaximumCount
			&& Left.DurationTurns == Right.DurationTurns
			&& Left.LayerCount == Right.LayerCount
			&& Left.Flags == Right.Flags;
	}

	bool AreMoveEffectsDefinitionsIdentical(
		const FBattleMoveDefinition& Left,
		const FBattleMoveDefinition& Right)
	{
		if (Left.Id != Right.Id
			|| Left.Type != Right.Type
			|| Left.Category != Right.Category
			|| Left.Power != Right.Power
			|| Left.bAlwaysHits != Right.bAlwaysHits
			|| Left.Accuracy != Right.Accuracy
			|| Left.bUsesPP != Right.bUsesPP
			|| Left.BasePP != Right.BasePP
			|| Left.bAllowsPPBoosts != Right.bAllowsPPBoosts
			|| Left.Priority != Right.Priority
			|| Left.TargetClass != Right.TargetClass
			|| Left.Flags != Right.Flags
			|| Left.Effects.Num() != Right.Effects.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < Left.Effects.Num(); ++Index)
		{
			if (!AreMoveEffectsDescriptorsIdentical(Left.Effects[Index], Right.Effects[Index]))
			{
				return false;
			}
		}
		return true;
	}

	bool AreMoveEffectsFieldsIdentical(
		const FBattleFieldState& Left,
		const FBattleFieldState& Right)
	{
		const auto OptionalConditionEqual = [](const TOptional<FBattleConditionState>& L,
			const TOptional<FBattleConditionState>& R)
		{
			return L.IsSet() == R.IsSet()
				&& (!L.IsSet()
					|| ArePreMoveConditionsIdentical(
						TArray<FBattleConditionState>{L.GetValue()},
						TArray<FBattleConditionState>{R.GetValue()}));
		};
		return OptionalConditionEqual(Left.Weather, Right.Weather)
			&& OptionalConditionEqual(Left.Terrain, Right.Terrain)
			&& ArePreMoveConditionsIdentical(Left.Rooms, Right.Rooms)
			&& ArePreMoveConditionsIdentical(Left.Effects, Right.Effects);
	}

	bool AreMoveEffectsSidesIdentical(
		const TConstArrayView<FBattleSideState> Left,
		const TConstArrayView<FBattleSideState> Right)
	{
		return AreOrderedPivotIdentityValuesEqual(
			Left,
			Right,
			[](const FBattleSideState& L, const FBattleSideState& R)
			{
				return L.Side == R.Side
					&& ArePreMoveConditionsIdentical(L.Conditions, R.Conditions)
					&& ArePreMoveConditionsIdentical(L.Hazards, R.Hazards);
			});
	}

	bool AreMoveEffectsPoliciesIdentical(
		const FBattleCompiledEncounterPolicies& Left,
		const FBattleCompiledEncounterPolicies& Right)
	{
		if (Left.IsValid() != Right.IsValid()
			|| Left.GetEncounterKind() != Right.GetEncounterKind()
			|| Left.GetFormat() != Right.GetFormat()
			|| Left.GetMaximumActiveBattlersPerSide()
				!= Right.GetMaximumActiveBattlersPerSide()
			|| Left.GetMaximumPartySize() != Right.GetMaximumPartySize()
			|| Left.IsRunAllowed() != Right.IsRunAllowed()
			|| Left.IsCaptureAllowed() != Right.IsCaptureAllowed()
			|| Left.IsBagAllowed() != Right.IsBagAllowed()
			|| Left.GetBattleStyle() != Right.GetBattleStyle()
			|| Left.GetReinforcementPolicy() != Right.GetReinforcementPolicy()
			|| Left.IsWildFleeConfigured() != Right.IsWildFleeConfigured()
			|| Left.GetWildFleeMode() != Right.GetWildFleeMode()
			|| Left.GetWildFleeNumerator() != Right.GetWildFleeNumerator()
			|| Left.GetWildFleeDenominator() != Right.GetWildFleeDenominator()
			|| Left.IsScriptedEndingAllowed() != Right.IsScriptedEndingAllowed()
			|| Left.HasSeparatePartnerOwnership() != Right.HasSeparatePartnerOwnership()
			|| Left.GetTrainerPolicies().Num() != Right.GetTrainerPolicies().Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < Left.GetTrainerPolicies().Num(); ++Index)
		{
			const FBattleTrainerEncounterPolicy& L = Left.GetTrainerPolicies()[Index];
			const FBattleTrainerEncounterPolicy& R = Right.GetTrainerPolicies()[Index];
			if (L.TrainerId != R.TrainerId
				|| L.Side != R.Side
				|| L.Role != R.Role
				|| L.Controller != R.Controller
				|| L.SelectorProfileId != R.SelectorProfileId
				|| L.SelectorProfileTag != R.SelectorProfileTag
				|| L.bMayUseBag != R.bMayUseBag
				|| L.bMayUseRevive != R.bMayUseRevive
				|| L.bMayRun != R.bMayRun
				|| L.bMayCapture != R.bMayCapture
				|| L.bMayVoluntarilySwitch != R.bMayVoluntarilySwitch
				|| L.bPartnerOwnsSeparatePartyAndBag
					!= R.bPartnerOwnsSeparatePartyAndBag)
			{
				return false;
			}
		}
		return true;
	}

	/** Bounded Trainer facts read by Pivot and replacement legality preparation. */
	struct FMoveEffectsTrainerIdentity
	{
		FTrainerId TrainerId;
		EBattleSide Side = EBattleSide::Player;
		TArray<FBattlePartySlotState> PartySlots;
	};

	bool MatchesMoveEffectsTrainerIdentity(
		const FBattleTrainerState& Trainer,
		const FMoveEffectsTrainerIdentity& Identity)
	{
		if (Trainer.TrainerId != Identity.TrainerId
			|| Trainer.Side != Identity.Side
			|| Trainer.PartySlots.Num() != Identity.PartySlots.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < Trainer.PartySlots.Num(); ++Index)
		{
			if (Trainer.PartySlots[Index].PartySlotId
					!= Identity.PartySlots[Index].PartySlotId
				|| Trainer.PartySlots[Index].BattlerId
					!= Identity.PartySlots[Index].BattlerId)
			{
				return false;
			}
		}
		return true;
	}

	/** Bounded locked-action facts read by the executor's acted-this-turn query. */
	struct FMoveEffectsLockedActionIdentity
	{
		FBattlerId ActingBattlerId;
		bool bStarted = false;
		bool bFinished = false;
	};

	/** One action plus bounded preparation facts; append-only histories are scalar identities. */
	struct FMoveEffectsCheckpointIdentity
	{
		FBattleResolutionCommitIdentity CommitIdentity;
		int32 ExpectedLockedActionCount = 0;
		int32 ExpectedEventCount = 0;
		FBattleId ExpectedBattleId;
		FTurnId ExpectedTurnId;
		EBattleEncounterKind ExpectedEncounterKind = EBattleEncounterKind::Wild;
		EBattleFormat ExpectedFormat = EBattleFormat::Single;
		bool bExpectedHasCatalog = false;
		FBattleLockedActionState ExpectedAction;
		TArray<FMoveEffectsLockedActionIdentity> LockedActionIdentities;
		FBattleMoveDefinition ExpectedMove;
		FTrainerId ExpectedOwnerId;
		FBattlerId ExpectedActorId;
		FActiveSlotId ExpectedActingSlotId;
		TArray<FMoveEffectsTrainerIdentity> TrainerIdentities;
		TArray<FBattleBattlerState> ExpectedBattlers;
		FBattleFieldState ExpectedField;
		TArray<FBattleSideState> ExpectedSides;
		FBattleCompiledEncounterPolicies ExpectedPolicies;
		TArray<FBattleActiveAssignment> ExpectedStartingActive;
		EBattleOutcome ExpectedOutcome = EBattleOutcome::InProgress;
		EBattleOutcomeCause ExpectedOutcomeCause = EBattleOutcomeCause::None;
		TOptional<FBattleDecisionRequest> ExpectedPendingDecision;
		TArray<FBattleDecisionRequest> ExpectedPendingDecisionRequests;
		TArray<FBattlePendingReplacementState> ExpectedPendingReplacements;
		TArray<FBattleHeldItemInstanceState> ExpectedHeldItemStates;
		TArray<FBattleTriggerRegistrationState> ExpectedTriggerRegistrations;
		TArray<uint8> ExpectedAbilityRevealFacts;
		TArray<uint8> ExpectedItemRevealFacts;
		int32 ExpectedPendingTriggerDispatchCount = 0;
		int32 ExpectedPendingTriggerEffectCount = 0;
		int32 ExpectedPendingTriggerLifecycleCount = 0;
		uint64 ExpectedNextConditionCreationOrdinal = 0;
		uint64 ExpectedNextTriggerReentrancyToken = 0;
		TArray<uint64> ExpectedOpponentRemovalCheckpoints;
		TArray<FVoluntarySwitchBattlerIdentity> BattlerIdentities;
		TArray<FVoluntarySwitchActiveIdentity> ActiveIdentities;
	};

	bool TryCaptureMoveEffectsCheckpointIdentity(
		const FBattleEngineState& State,
		const FResolutionId ResolutionId,
		const FBattleLockedActionState& Action,
		FMoveEffectsCheckpointIdentity& OutIdentity)
	{
		OutIdentity = FMoveEffectsCheckpointIdentity();
		if (Action.Decision.GetActionKind() != EBattleActionKind::Fight
			|| !Action.bStarted
			|| !Action.bMoveCommitted
			|| !Action.TargetResolution.IsSet()
			|| Action.TargetResolution.GetValue().Outcome
				!= EBattleTargetResolutionOutcome::Resolved
			|| Action.EffectExecutionState != EBattleLockedEffectExecutionState::Pending
			|| Action.bFinished
			|| !State.Random.IsValid())
		{
			return false;
		}

		const FBattleBattlerState* Actor = State.FindBattler(
			Action.Decision.GetActingBattlerId());
		const FBattleActivePositionState* Active = State.FindActivePosition(
			Action.OrderKey.ActingSlotId);
		if (Actor == nullptr
			|| Active == nullptr
			|| !Active->bAvailable
			|| Active->TrainerId != Action.Decision.GetDecisionOwnerTrainerId()
			|| Active->BattlerId != Action.Decision.GetActingBattlerId())
		{
			return false;
		}

		const FMoveId MoveId = Action.Decision.GetMoveId();
		const FBattleMoveDefinition* Move =
			MoveId == FBattleBuiltInMoveDefinitions::GetStruggleMoveId()
				? &FBattleBuiltInMoveDefinitions::GetStruggle()
				: State.Catalog.FindMove(MoveId);
		if (Move == nullptr
			|| Action.TargetClass != Move->TargetClass
			|| Action.TargetResolution.GetValue().TargetClass != Action.TargetClass)
		{
			return false;
		}

		// Preserve the final stale-test seam without retaining the trace itself. This
		// probe and TryCaptureIdentity must be the only parent trace reads before staging.
		const int32 RandomTraceCount = State.Random->GetTrace().Num();
		FBattleResolutionCommitIdentity CommitIdentity;
		if (!FBattleResolutionCommit::TryCaptureIdentity(
				State,
				ResolutionId,
				Action.ActionId,
				CommitIdentity)
			|| RandomTraceCount != CommitIdentity.ExpectedRandomTraceCount)
		{
			return false;
		}

		OutIdentity.CommitIdentity = CommitIdentity;
		OutIdentity.ExpectedLockedActionCount = State.LockedActions.Num();
		OutIdentity.ExpectedEventCount = State.OrderedEvents.Num();
		OutIdentity.ExpectedAction = Action;
		OutIdentity.LockedActionIdentities.Reserve(State.LockedActions.Num());
		for (const FBattleLockedActionState& LockedAction : State.LockedActions)
		{
			FMoveEffectsLockedActionIdentity& Identity =
				OutIdentity.LockedActionIdentities.AddDefaulted_GetRef();
			Identity.ActingBattlerId = LockedAction.Decision.GetActingBattlerId();
			Identity.bStarted = LockedAction.bStarted;
			Identity.bFinished = LockedAction.bFinished;
		}
		OutIdentity.ExpectedBattleId = State.Setup.GetBattleId();
		OutIdentity.ExpectedTurnId = State.TurnId;
		OutIdentity.ExpectedEncounterKind = State.EncounterKind;
		OutIdentity.ExpectedFormat = State.Format;
		OutIdentity.bExpectedHasCatalog = State.bHasCatalog;
		OutIdentity.ExpectedMove = *Move;
		OutIdentity.ExpectedOwnerId = Action.Decision.GetDecisionOwnerTrainerId();
		OutIdentity.ExpectedActorId = Action.Decision.GetActingBattlerId();
		OutIdentity.ExpectedActingSlotId = Action.OrderKey.ActingSlotId;
		OutIdentity.TrainerIdentities.Reserve(State.Trainers.Num());
		for (const FBattleTrainerState& Trainer : State.Trainers)
		{
			FMoveEffectsTrainerIdentity& Identity =
				OutIdentity.TrainerIdentities.AddDefaulted_GetRef();
			Identity.TrainerId = Trainer.TrainerId;
			Identity.Side = Trainer.Side;
			Identity.PartySlots = Trainer.PartySlots;
		}
		OutIdentity.ExpectedBattlers = State.Battlers;
		OutIdentity.ExpectedField = State.Field;
		OutIdentity.ExpectedSides = State.Sides;
		OutIdentity.ExpectedPolicies = State.CompiledEncounterPolicies;
		for (const FBattleActiveAssignment& Assignment : State.Setup.GetStartingActive())
		{
			OutIdentity.ExpectedStartingActive.Add(Assignment);
		}
		OutIdentity.ExpectedOutcome = State.Outcome;
		OutIdentity.ExpectedOutcomeCause = State.OutcomeCause;
		OutIdentity.ExpectedPendingDecision = State.PendingDecision;
		OutIdentity.ExpectedPendingDecisionRequests = State.PendingDecisionRequests;
		OutIdentity.ExpectedPendingReplacements = State.PendingReplacements;
		OutIdentity.ExpectedPendingTriggerDispatchCount =
			State.TriggerFramework.GetPendingDispatchCount();
		OutIdentity.ExpectedPendingTriggerEffectCount =
			State.TriggerFramework.GetPendingEffectRequestCount();
		OutIdentity.ExpectedPendingTriggerLifecycleCount =
			State.TriggerFramework.GetPendingLifecycleFactCount();
		OutIdentity.ExpectedNextConditionCreationOrdinal =
			State.NextConditionCreationOrdinal;
		OutIdentity.ExpectedNextTriggerReentrancyToken =
			State.NextTriggerReentrancyToken;
		OutIdentity.ExpectedOpponentRemovalCheckpoints =
			State.AvailableOpponentRemovalCheckpoints;
		for (const FBattleHeldItemInstanceState& Item : State.HeldItemLedger.GetStates())
		{
			OutIdentity.ExpectedHeldItemStates.Add(Item);
		}
		for (const FBattleTriggerRegistrationState& Registration :
			State.TriggerFramework.GetActiveRegistrations())
		{
			OutIdentity.ExpectedTriggerRegistrations.Add(Registration);
		}
		for (const FBattleBattlerState& Battler : State.Battlers)
		{
			OutIdentity.BattlerIdentities.Add(MakeVoluntarySwitchBattlerIdentity(Battler));
			FBattleTriggerSubject Owner;
			const bool bOwnerValid = FBattleTriggerSubject::TryCreateBattler(
				Battler.BattlerId,
				Owner);
			FBattleTriggerSourceDefinition AbilitySource;
			const bool bAbilityRevealed = bOwnerValid
				&& FBattleTriggerSourceDefinition::TryCreateAbility(
					Battler.AbilityId,
					AbilitySource)
				&& State.AbilityItemRevealTracker.HasBeenRevealed(AbilitySource, Owner);
			FBattleTriggerSourceDefinition ItemSource;
			const bool bItemRevealed = bOwnerValid
				&& Battler.HeldItem.CurrentItemId.IsValid()
				&& FBattleTriggerSourceDefinition::TryCreateItem(
					Battler.HeldItem.CurrentItemId,
					ItemSource)
				&& State.AbilityItemRevealTracker.HasBeenRevealed(ItemSource, Owner);
			OutIdentity.ExpectedAbilityRevealFacts.Add(bAbilityRevealed ? 1 : 0);
			OutIdentity.ExpectedItemRevealFacts.Add(bItemRevealed ? 1 : 0);
		}
		for (const FBattleActivePositionState& Position : State.ActivePositions)
		{
			FVoluntarySwitchActiveIdentity& Identity =
				OutIdentity.ActiveIdentities.AddDefaulted_GetRef();
			Identity.ActiveSlotId = Position.ActiveSlotId;
			Identity.TrainerId = Position.TrainerId;
			Identity.BattlerId = Position.BattlerId;
			Identity.bAvailable = Position.bAvailable;
		}
		return true;
	}

	bool IsMoveEffectsCheckpointIdentityCurrent(
		const FBattleEngineState& State,
		const FMoveEffectsCheckpointIdentity& Identity)
	{
		const FBattleResolutionCommitIdentity& Commit = Identity.CommitIdentity;
		// Keep the commit identity check first: it is the only parent trace read after
		// staging and closes the stale-checkpoint seam before exact fact comparison.
		if (!FBattleResolutionCommit::IsIdentityCurrent(State, Commit)
			|| !State.LockedActions.IsValidIndex(Commit.ExpectedLockedActionIndex)
			|| State.LockedActions.Num() != Identity.ExpectedLockedActionCount
			|| State.LockedActions.Num() != Identity.LockedActionIdentities.Num()
			|| State.OrderedEvents.Num() != Identity.ExpectedEventCount
			|| State.Setup.GetBattleId() != Identity.ExpectedBattleId
			|| State.TurnId != Identity.ExpectedTurnId
			|| State.EncounterKind != Identity.ExpectedEncounterKind
			|| State.Format != Identity.ExpectedFormat
			|| State.bHasCatalog != Identity.bExpectedHasCatalog
			|| State.Outcome != Identity.ExpectedOutcome
			|| State.OutcomeCause != Identity.ExpectedOutcomeCause
			|| State.NextConditionCreationOrdinal
				!= Identity.ExpectedNextConditionCreationOrdinal
			|| State.NextTriggerReentrancyToken
				!= Identity.ExpectedNextTriggerReentrancyToken
			|| !ArePivotLockedActionsIdentical(
				State.LockedActions[Commit.ExpectedLockedActionIndex],
				Identity.ExpectedAction)
			|| State.Trainers.Num() != Identity.TrainerIdentities.Num()
			|| State.Battlers.Num() != Identity.ExpectedBattlers.Num()
			|| State.Battlers.Num() != Identity.BattlerIdentities.Num()
			|| State.Battlers.Num() != Identity.ExpectedAbilityRevealFacts.Num()
			|| State.Battlers.Num() != Identity.ExpectedItemRevealFacts.Num()
			|| State.ActivePositions.Num() != Identity.ActiveIdentities.Num()
			|| !AreMoveEffectsFieldsIdentical(State.Field, Identity.ExpectedField)
			|| !AreMoveEffectsSidesIdentical(State.Sides, Identity.ExpectedSides)
			|| !AreMoveEffectsPoliciesIdentical(
				State.CompiledEncounterPolicies,
				Identity.ExpectedPolicies)
			|| !AreTargetResolutionPendingDecisionIdentical(
				State.PendingDecision,
				Identity.ExpectedPendingDecision)
			|| !AreTargetResolutionRequestsIdentical(
				State.PendingDecisionRequests,
				Identity.ExpectedPendingDecisionRequests)
			|| !AreTargetResolutionPendingReplacementsIdentical(
				State.PendingReplacements,
				Identity.ExpectedPendingReplacements)
			|| State.AvailableOpponentRemovalCheckpoints
				!= Identity.ExpectedOpponentRemovalCheckpoints
			|| State.TriggerFramework.GetPendingDispatchCount()
				!= Identity.ExpectedPendingTriggerDispatchCount
			|| State.TriggerFramework.GetPendingEffectRequestCount()
				!= Identity.ExpectedPendingTriggerEffectCount
			|| State.TriggerFramework.GetPendingLifecycleFactCount()
				!= Identity.ExpectedPendingTriggerLifecycleCount
			|| State.TriggerFramework.GetActiveRegistrations().Num()
				!= Identity.ExpectedTriggerRegistrations.Num()
			|| State.HeldItemLedger.GetStates().Num()
				!= Identity.ExpectedHeldItemStates.Num()
			|| State.Setup.GetStartingActive().Num()
				!= Identity.ExpectedStartingActive.Num())
		{
			return false;
		}

		for (int32 Index = 0; Index < State.LockedActions.Num(); ++Index)
		{
			const FBattleLockedActionState& Current = State.LockedActions[Index];
			const FMoveEffectsLockedActionIdentity& Expected =
				Identity.LockedActionIdentities[Index];
			if (Current.Decision.GetActingBattlerId() != Expected.ActingBattlerId
				|| Current.bStarted != Expected.bStarted
				|| Current.bFinished != Expected.bFinished)
			{
				return false;
			}
		}
		for (const FMoveEffectsTrainerIdentity& Expected : Identity.TrainerIdentities)
		{
			const FBattleTrainerState* Trainer = State.FindTrainer(Expected.TrainerId);
			if (Trainer == nullptr || !MatchesMoveEffectsTrainerIdentity(*Trainer, Expected))
			{
				return false;
			}
		}
		for (int32 Index = 0; Index < State.Battlers.Num(); ++Index)
		{
			if (!ArePreMoveBattlersIdentical(
					State.Battlers[Index],
					Identity.ExpectedBattlers[Index]))
			{
				return false;
			}
			const FBattleBattlerState& Battler = State.Battlers[Index];
			FBattleTriggerSubject Owner;
			if (!FBattleTriggerSubject::TryCreateBattler(Battler.BattlerId, Owner))
			{
				return false;
			}
			FBattleTriggerSourceDefinition AbilitySource;
			const bool bAbilityRevealed =
				FBattleTriggerSourceDefinition::TryCreateAbility(
					Battler.AbilityId,
					AbilitySource)
				&& State.AbilityItemRevealTracker.HasBeenRevealed(AbilitySource, Owner);
			FBattleTriggerSourceDefinition ItemSource;
			const bool bItemRevealed = Battler.HeldItem.CurrentItemId.IsValid()
				&& FBattleTriggerSourceDefinition::TryCreateItem(
					Battler.HeldItem.CurrentItemId,
					ItemSource)
				&& State.AbilityItemRevealTracker.HasBeenRevealed(ItemSource, Owner);
			if ((bAbilityRevealed ? 1 : 0) != Identity.ExpectedAbilityRevealFacts[Index]
				|| (bItemRevealed ? 1 : 0) != Identity.ExpectedItemRevealFacts[Index])
			{
				return false;
			}
		}
		for (const FVoluntarySwitchActiveIdentity& Expected : Identity.ActiveIdentities)
		{
			const FBattleActivePositionState* Position =
				State.FindActivePosition(Expected.ActiveSlotId);
			if (Position == nullptr
				|| Position->bAvailable != Expected.bAvailable
				|| Position->TrainerId != Expected.TrainerId
				|| Position->BattlerId != Expected.BattlerId)
			{
				return false;
			}
		}
		for (int32 Index = 0; Index < State.TriggerFramework.GetActiveRegistrations().Num(); ++Index)
		{
			if (!ArePreMoveTriggerRegistrationsIdentical(
					State.TriggerFramework.GetActiveRegistrations()[Index],
					Identity.ExpectedTriggerRegistrations[Index]))
			{
				return false;
			}
		}
		for (int32 Index = 0; Index < State.HeldItemLedger.GetStates().Num(); ++Index)
		{
			if (!(State.HeldItemLedger.GetStates()[Index]
				== Identity.ExpectedHeldItemStates[Index]))
			{
				return false;
			}
		}
		for (int32 Index = 0; Index < State.Setup.GetStartingActive().Num(); ++Index)
		{
			const FBattleActiveAssignment& L = State.Setup.GetStartingActive()[Index];
			const FBattleActiveAssignment& R = Identity.ExpectedStartingActive[Index];
			if (L.TrainerId != R.TrainerId
				|| L.BattlerId != R.BattlerId
				|| L.ActiveSlotId != R.ActiveSlotId)
			{
				return false;
			}
		}

		// Re-find the selected move by stable ID immediately before commit; no catalog
		// snapshot or catalog-wide comparison is retained.
		const FBattleMoveDefinition* CurrentMove =
			Identity.ExpectedMove.Id == FBattleBuiltInMoveDefinitions::GetStruggleMoveId()
				? &FBattleBuiltInMoveDefinitions::GetStruggle()
				: State.Catalog.FindMove(Identity.ExpectedMove.Id);
		const FBattleActivePositionState* CurrentActive =
			State.FindActivePosition(Identity.ExpectedActingSlotId);
		const FBattleBattlerState* CurrentActor =
			State.FindBattler(Identity.ExpectedActorId);
		return CurrentMove != nullptr
			&& CurrentActor != nullptr
			&& AreMoveEffectsDefinitionsIdentical(*CurrentMove, Identity.ExpectedMove)
			&& CurrentActor->TrainerId == Identity.ExpectedOwnerId
			&& CurrentActive != nullptr
			&& CurrentActive->bAvailable
			&& CurrentActive->TrainerId == Identity.ExpectedOwnerId
			&& CurrentActive->BattlerId == Identity.ExpectedActorId;
	}

	/** Move-effects preparation adopts the executor's bounded plan, then stages finalization. */
	struct FMoveEffectsCheckpointPreparation
	{
		FAtomicCheckpointCommonPreparation Common;
		FBattleFieldState Field;
		TArray<FBattleSideState> Sides;
		FBattleLockedActionState Action;

		bool ImportPreparedEffects(
			const FBattleEngineState& State,
			const FActionId ActionId,
			FBattleEffectExecutionPlan&& EffectPlan)
		{
			const FBattleLockedActionState* CurrentAction =
				State.LockedActions.FindByPredicate(
					[ActionId](const FBattleLockedActionState& Candidate)
					{
						return Candidate.ActionId == ActionId;
					});
			if (CurrentAction == nullptr)
			{
				return false;
			}

			Common.Capture(State);
			Common.Battlers = MoveTemp(EffectPlan.Battlers);
			Common.ActivePositions = MoveTemp(EffectPlan.ActivePositions);
			Common.TriggerFramework = MoveTemp(EffectPlan.TriggerFramework);
			Common.AbilityItemRevealTracker =
				MoveTemp(EffectPlan.AbilityItemRevealTracker);
			Common.HeldItemLedger = MoveTemp(EffectPlan.HeldItemLedger);
			Common.NextConditionCreationOrdinal =
				EffectPlan.NextConditionCreationOrdinal;
			Common.NextTriggerReentrancyToken =
				EffectPlan.NextTriggerReentrancyToken;
			Field = MoveTemp(EffectPlan.Field);
			Sides = MoveTemp(EffectPlan.Sides);
			Action = *CurrentAction;
			return true;
		}
	};

	struct FMoveEffectsCheckpointDelta
	{
		FAtomicSwitchStateDelta State;
		FBattleLockedActionState Action;
	};

	bool TryCaptureMoveEffectsCheckpointDelta(
		const FMoveEffectsCheckpointPreparation& Preparation,
		const FMoveEffectsCheckpointIdentity& Identity,
		FMoveEffectsCheckpointDelta& OutDelta)
	{
		OutDelta = FMoveEffectsCheckpointDelta();
		const int32 ActionIndex = Identity.CommitIdentity.ExpectedLockedActionIndex;
		if (Preparation.Action.ActionId
			!= Identity.CommitIdentity.OwningActionId)
		{
			return false;
		}
		const FBattleLockedActionState& Action = Preparation.Action;
		if ((Action.EffectExecutionState == EBattleLockedEffectExecutionState::Completed
				&& (!Action.bFinished
					|| Preparation.Common.CurrentLockedActionIndex != ActionIndex + 1))
			|| (Action.EffectExecutionState
					== EBattleLockedEffectExecutionState::AwaitingPivot
				&& (Action.bFinished
					|| Preparation.Common.CurrentLockedActionIndex != ActionIndex
					|| !Preparation.Common.PendingDecision.IsSet()
					|| Preparation.Common.PendingDecisionRequests.Num() != 1
					|| Preparation.Common.PendingDecisionRequests[0].GetRequestKind()
						!= EBattleDecisionRequestKind::PivotSwitch))
			|| (Action.EffectExecutionState != EBattleLockedEffectExecutionState::Completed
				&& Action.EffectExecutionState
					!= EBattleLockedEffectExecutionState::AwaitingPivot))
		{
			return false;
		}
		if (!TryCaptureAtomicFieldSideDelta(
				Preparation.Common,
				Preparation.Field,
				Preparation.Sides,
				OutDelta.State))
		{
			return false;
		}
		OutDelta.Action = Action;
		return AreAtomicCheckpointCommonDeltaRecordsValid(
			Identity.BattlerIdentities,
			Identity.ActiveIdentities,
			OutDelta.State);
	}

	void ApplyMoveEffectsCheckpointDelta(
		FBattleEngineState& State,
		const FMoveEffectsCheckpointIdentity& Identity,
		const FMoveEffectsCheckpointDelta& Delta)
	{
		FBattleLockedActionState* Action = State.LockedActions.FindByPredicate(
			[&Identity](const FBattleLockedActionState& Candidate)
			{
				return Candidate.ActionId
					== Identity.CommitIdentity.OwningActionId;
			});
		check(Action != nullptr);
		ApplyAtomicSwitchStateDelta(State, Delta.State);
		*Action = Delta.Action;
	}

	template <typename TState>
	bool TryPrepareMoveEffectsPivotRequest(
		const TState& State,
		const FBattleLockedActionState& Action,
		const uint64 StateVersion,
		bool& OutHasLegalReserve,
		TOptional<FBattleDecisionRequest>& OutRequest)
	{
		OutHasLegalReserve = false;
		OutRequest.Reset();
		FBattleSwitchLegalityResult Legality;
		if (!TryBuildSwitchLegality(
				State,
				EBattleSwitchKind::Pivot,
				Action.Decision.GetDecisionOwnerTrainerId(),
				Action.Decision.GetActingBattlerId(),
				Action.OrderKey.ActingSlotId,
				TConstArrayView<FPartySlotId>(),
				Legality))
		{
			return false;
		}
		if (Legality.GetLegalPartySlots().IsEmpty())
		{
			return true;
		}
		OutHasLegalReserve = true;
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
		FBattleDecisionRequest Request;
		FBattleRejection Rejection;
		if (!FBattleDecisionRequest::TryCreate(Spec, Request, Rejection))
		{
			return false;
		}
		OutRequest = MoveTemp(Request);
		return true;
	}

	template <typename TState>
	bool TryAppendMoveEffectsPartnerRecoveryEvent(
		TState& Projection,
		const FResolutionId ResolutionId,
		const FActionId ActionId,
		const EBattleActionKind ActionKind,
		const FBattleEventSource& Source,
		const FBattleFaintOutcomeResolution& FaintResolution,
		TArray<FBattleEvent>& Events)
	{
		if (!FaintResolution.PartnerTeamVictoryRecovery.IsSet())
		{
			return true;
		}
		const FBattlePartnerTeamVictoryRecovery& Recovery =
			FaintResolution.PartnerTeamVictoryRecovery.GetValue();
		if (!Recovery.bMajorStatusCured
			|| Projection.NextEventOrdinal == 0
			|| Projection.NextEventOrdinal == TNumericLimits<uint64>::Max())
		{
			return false;
		}
		FBattleEventSpec Spec;
		Spec.EventOrdinal = Projection.NextEventOrdinal;
		Spec.BattleId = Projection.Setup.GetBattleId();
		Spec.TurnId = Projection.TurnId;
		Spec.ActionId = ActionId;
		Spec.ResolutionId = ResolutionId;
		Spec.Type = EBattleEventType::PartnerTeamVictoryRecovery;
		Spec.Cause = EBattleEventCause::Outcome;
		Spec.CauseActionKind = ActionKind;
		Spec.OutcomeCause = EBattleOutcomeCause::PartnerTeamVictory;
		Spec.Source = Source;
		Spec.Targets.Add(Recovery.Target);
		Spec.NumericBefore = Recovery.PreviousHP;
		Spec.NumericAfter = Recovery.NewHP;
		Spec.NumericDelta = Recovery.NewHP - Recovery.PreviousHP;
		Spec.Visibility.Level = EBattleVisibilityLevel::Public;
		FBattleEvent Event;
		if (!FBattleEvent::TryCreate(Spec, Event))
		{
			return false;
		}
		++Projection.NextEventOrdinal;
		Events.Add(MoveTemp(Event));
		return true;
	}

	bool TryPublishMoveEffectsCheckpointRejection(
		FBattleEngineState& State,
		const FResolutionId ResolutionId,
		const FActionId ActionId,
		const EBattleRejectionReason Reason,
		const FTrainerId TrainerId,
		const FBattlerId BattlerId,
		const FBattleEventSource& Source,
		FBattleResolution& OutResolution)
	{
		OutResolution = FBattleResolution();
		FBattleResolutionCommitPlan RejectedPlan;
		if (!FBattleResolutionCommit::TryBuildRejectedPlan(
				State,
				ResolutionId,
				ActionId,
				Reason,
				TrainerId,
				BattlerId,
				EBattleActionKind::Fight,
				Source,
				RejectedPlan))
		{
			return false;
		}
		OutResolution = FBattleResolutionCommit::PublishPrepared(State, RejectedPlan);
		return true;
	}

	FBattleResolution PublishVoluntarySwitchCheckpointRejection(
		FBattleEngineState& State,
		const FResolutionId ResolutionId,
		const FActionId ActionId,
		const EBattleRejectionReason Reason,
		const FTrainerId TrainerId,
		const FBattlerId BattlerId,
		const FBattleEventSource& Source)
	{
		FBattleResolutionCommitPlan RejectedPlan;
		const bool bPrepared = FBattleResolutionCommit::TryBuildRejectedPlan(
			State,
			ResolutionId,
			ActionId,
			Reason,
			TrainerId,
			BattlerId,
			EBattleActionKind::Switch,
			Source,
			RejectedPlan);
		check(bPrepared);
		return FBattleResolutionCommit::PublishPrepared(State, RejectedPlan);
	}

	FBattleResolution PublishPivotSwitchCheckpointRejection(
		FBattleEngineState& State,
		const FResolutionId ResolutionId,
		const FActionId ActionId,
		const EBattleRejectionReason Reason,
		const FTrainerId TrainerId,
		const FBattlerId BattlerId,
		const FBattleEventSource& Source)
	{
		FBattleResolutionCommitPlan RejectedPlan;
		const bool bPrepared = FBattleResolutionCommit::TryBuildRejectedPlan(
			State,
			ResolutionId,
			ActionId,
			Reason,
			TrainerId,
			BattlerId,
			EBattleActionKind::Fight,
			Source,
			RejectedPlan);
		check(bPrepared);
		return FBattleResolutionCommit::PublishPrepared(State, RejectedPlan);
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
	Snapshot.EncounterKind = State->CompiledEncounterPolicies.GetEncounterKind();
	Snapshot.Format = State->CompiledEncounterPolicies.GetFormat();
	Snapshot.Phase = State->Phase;
	Snapshot.Outcome = State->Outcome;
	Snapshot.OutcomeCause = State->OutcomeCause;
	Snapshot.SettingsReference = State->Setup.GetSettingsReference();
	Snapshot.CatalogReference = State->Setup.GetCatalogReference();
	Snapshot.EscapeAttemptCount = State->EscapeAttemptCount;
	Snapshot.bReinforcementSucceeded = State->bReinforcementSucceeded;
	const FBattleTrainerEncounterPolicy* ObserverPolicy = bFiltered
		? FindTrainerEncounterPolicy(*State, Observer->TrainerId)
		: nullptr;
	Snapshot.bCaptureStateVisible = !bFiltered
		|| (ObserverPolicy != nullptr
			&& ObserverPolicy->Role == EBattleTrainerRole::Player);
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
		for (const FBattleBattlerState& Battler : State->Battlers)
		{
			const FBattleTrainerEncounterPolicy* TrainerPolicy =
				FindTrainerEncounterPolicy(*State, Battler.TrainerId);
			if (TrainerPolicy == nullptr
				|| !TrainerPolicy->bPartnerOwnsSeparatePartyAndBag)
			{
				continue;
			}
			FBattlePersistentProgressionEligibilityFact& Fact =
				Snapshot.PersistentProgressionEligibilityFacts.AddDefaulted_GetRef();
			Fact.TrainerId = Battler.TrainerId;
			Fact.BattlerId = Battler.BattlerId;
			Fact.SourcePokemonId = Battler.SourcePokemonId;
			Fact.bExperienceEligible = false;
			Fact.bEffortValueEligible = false;
			Fact.Restriction = EBattlePersistentProgressionRestriction::NpcPartner;
			check(Fact.IsValid());
		}
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

const FBattleCompiledEncounterPolicies& FBattleEngine::GetCompiledEncounterPolicies() const
{
	check(State.IsValid());
	return State->CompiledEncounterPolicies;
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
	const FActionId ActionId = ExistingAction->ActionId;
	const FTrainerId ActingTrainerId = Trainer->TrainerId;
	const FBattlerId ActingBattlerId = Battler->BattlerId;
	const FBattleEventSource Source = SourceFromLockedAction(*State, *ExistingAction);
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

	FActionStartCheckpointIdentity CheckpointIdentity;
	if (!TryCaptureActionStartCheckpointIdentity(
			*State,
			ResolutionId,
			*ExistingAction,
			CheckpointIdentity))
	{
		return PublishActionStartCheckpointRejection(
			*State,
			ResolutionId,
			ActionId,
			EBattleRejectionReason::CheckpointPreparationFailed,
			ActingTrainerId,
			ActingBattlerId,
			ActionKind,
			Source);
	}

	auto RejectPreparedCheckpoint = [&](const EBattleRejectionReason Reason)
	{
		return PublishActionStartCheckpointRejection(
			*State,
			ResolutionId,
			ActionId,
			Reason,
			ActingTrainerId,
			ActingBattlerId,
			ActionKind,
			Source);
	};

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
	if (!IsActionStartCheckpointIdentityCurrent(*State, CheckpointIdentity))
	{
		return RejectPreparedCheckpoint(
			EBattleRejectionReason::StaleCheckpointIdentity);
	}

	FActionStartStateDelta Delta;
	InitializeActionStartDelta(*State, *ExistingAction, *Trainer, Delta);
	if (Delta.TrainerAllowance.RemainingActions <= 0)
	{
		return RejectPreparedCheckpoint(
			EBattleRejectionReason::CheckpointPreparationFailed);
	}
	--Delta.TrainerAllowance.RemainingActions;
	Delta.MechanicsStage.Capture(*State, ActingBattlerId);

	bool bRechargeDeniedAction = false;
	if (StartResult.Outcome == EBattleActionStartOutcome::Proceed)
	{
		Delta.bApplyMechanicsStage = true;
		bool bMagicRoomTriggerActive = false;
		if (!TryStageActionStartFieldConditionDispatch(
				*State,
				Delta.MechanicsStage,
				FBattleFieldSideConditionRules::GetMagicRoomId(),
				EBattleTriggerPhase::BeforeAction,
				Active != nullptr
					? TOptional<FActiveSlotId>(Active->ActiveSlotId)
					: TOptional<FActiveSlotId>(),
				bMagicRoomTriggerActive))
		{
			return RejectPreparedCheckpoint(
				EBattleRejectionReason::CheckpointPreparationFailed);
		}
		if (!IsActionStartCheckpointIdentityCurrent(*State, CheckpointIdentity))
		{
			return RejectPreparedCheckpoint(
				EBattleRejectionReason::StaleCheckpointIdentity);
		}
		if (!TryStageAllActionStartHeldItemsSuppressed(
				*State,
				Delta.MechanicsStage,
				bMagicRoomTriggerActive))
		{
			return RejectPreparedCheckpoint(
				EBattleRejectionReason::CheckpointPreparationFailed);
		}
		if (!IsActionStartCheckpointIdentityCurrent(*State, CheckpointIdentity))
		{
			return RejectPreparedCheckpoint(
				EBattleRejectionReason::StaleCheckpointIdentity);
		}
		if (HasVolatile(*Battler, FBattleVolatileRules::GetRechargeId()))
		{
			if (!TryStageRechargeDenial(*State, Delta.MechanicsStage))
			{
				return RejectPreparedCheckpoint(
					EBattleRejectionReason::CheckpointPreparationFailed);
			}
			if (!IsActionStartCheckpointIdentityCurrent(*State, CheckpointIdentity))
			{
				return RejectPreparedCheckpoint(
					EBattleRejectionReason::StaleCheckpointIdentity);
			}
			bRechargeDeniedAction = true;
		}
	}

	const bool bNeedsChargeCleanup = bChargedFight
		&& (bRechargeDeniedAction
			|| StartResult.Outcome != EBattleActionStartOutcome::Proceed);
	if (bNeedsChargeCleanup)
	{
		Delta.bApplyMechanicsStage = true;
		if (!TryStageClearActionStartChargeState(Delta.MechanicsStage))
		{
			return RejectPreparedCheckpoint(
				EBattleRejectionReason::CheckpointPreparationFailed);
		}
		if (!IsActionStartCheckpointIdentityCurrent(*State, CheckpointIdentity))
		{
			return RejectPreparedCheckpoint(
				EBattleRejectionReason::StaleCheckpointIdentity);
		}
	}

	bool bCompletesAction = false;
	if (bRechargeDeniedAction)
	{
		Delta.bStarted = true;
		Delta.bFinished = true;
		bCompletesAction = true;
	}
	else if (StartResult.Outcome == EBattleActionStartOutcome::Proceed)
	{
		Delta.bStarted = true;
	}
	else if (StartResult.Outcome == EBattleActionStartOutcome::ObedienceRefused)
	{
		Delta.bStarted = true;
		Delta.bFinished = true;
		bCompletesAction = true;
	}
	else
	{
		Delta.bFinished = true;
		bCompletesAction = true;
	}

	TArray<FBattleReplacementRequirement> ReplacementRequirements;
	if (bCompletesAction)
	{
		Delta.NextLockedActionIndex = State->CurrentLockedActionIndex + 1;
		if (CheckpointIdentity.CommitIdentity.ExpectedStateVersion
				== TNumericLimits<uint64>::Max()
			|| !TryPrepareActionStartBoundary(
				*State,
				CheckpointIdentity.CommitIdentity.ExpectedStateVersion + 1,
				Delta,
				ReplacementRequirements))
		{
			return RejectPreparedCheckpoint(
				EBattleRejectionReason::CheckpointPreparationFailed);
		}
		if (!IsActionStartCheckpointIdentityCurrent(*State, CheckpointIdentity))
		{
			return RejectPreparedCheckpoint(
				EBattleRejectionReason::StaleCheckpointIdentity);
		}
	}

	FBattleResolutionCommitPlan CommitPlan;
	if (!FBattleResolutionCommit::TryBeginAcceptedPlan(
			CheckpointIdentity.CommitIdentity,
			CommitPlan))
	{
		return RejectPreparedCheckpoint(
			EBattleRejectionReason::CheckpointPreparationFailed);
	}
	if (!IsActionStartCheckpointIdentityCurrent(*State, CheckpointIdentity))
	{
		return RejectPreparedCheckpoint(
			EBattleRejectionReason::StaleCheckpointIdentity);
	}

	EBattleRejectionReason StageFailureReason = EBattleRejectionReason::None;
	auto StageEvent = [&](const EBattleEventType Type,
		const EBattleEventCause Cause,
		const TOptional<int64> NumericBefore = TOptional<int64>(),
		const TOptional<int64> NumericAfter = TOptional<int64>(),
		const TOptional<int64> NumericDelta = TOptional<int64>(),
		const EBattleVisibilityLevel Visibility = EBattleVisibilityLevel::Public,
		const FBattleEventTarget* Target = nullptr)
	{
		if (!FBattleResolutionCommit::TryStageEvent(
				CommitPlan,
				MakeStagedActionStartEventSpec(
					*State,
					ResolutionId,
					ActionId,
					ActionKind,
					Source,
					Type,
					Cause,
					NumericBefore,
					NumericAfter,
					NumericDelta,
					Visibility,
					Target)))
		{
			StageFailureReason = EBattleRejectionReason::CheckpointPreparationFailed;
			return false;
		}
		if (!IsActionStartCheckpointIdentityCurrent(*State, CheckpointIdentity))
		{
			StageFailureReason = EBattleRejectionReason::StaleCheckpointIdentity;
			return false;
		}
		return true;
	};

	bool bEventsStaged = true;
	if (bRechargeDeniedAction)
	{
		bEventsStaged = StageEvent(
				EBattleEventType::ActionStarted,
				EBattleEventCause::Action)
			&& StageEvent(
				EBattleEventType::StatusChanged,
				EBattleEventCause::Rule,
				static_cast<int64>(1),
				static_cast<int64>(0),
				static_cast<int64>(-1))
			&& StageEvent(
				EBattleEventType::EffectPrevented,
				EBattleEventCause::Rule)
			&& StageEvent(
				EBattleEventType::ActionCanceled,
				EBattleEventCause::Rule)
			&& StageEvent(
				EBattleEventType::ActionCompleted,
				EBattleEventCause::Action);
	}
	else if (StartResult.Outcome == EBattleActionStartOutcome::Proceed)
	{
		bEventsStaged = StageEvent(
			EBattleEventType::ActionStarted,
			EBattleEventCause::Action);
		if (bEventsStaged && StartResult.ObedienceCap.IsSet())
		{
			bEventsStaged = StageEvent(
				EBattleEventType::ObedienceConfirmed,
				EBattleEventCause::Rule,
				static_cast<int64>(Facts.ObedienceReferenceLevel),
				static_cast<int64>(StartResult.ObedienceCap.GetValue()),
				static_cast<int64>(Facts.ObedienceReferenceLevel)
					- static_cast<int64>(StartResult.ObedienceCap.GetValue()),
				EBattleVisibilityLevel::CoreOnly);
		}
	}
	else if (StartResult.Outcome == EBattleActionStartOutcome::ObedienceRefused)
	{
		bEventsStaged = StageEvent(
				EBattleEventType::ActionStarted,
				EBattleEventCause::Action)
			&& StageEvent(
				EBattleEventType::ObedienceRefused,
				EBattleEventCause::Rule,
				static_cast<int64>(Facts.ObedienceReferenceLevel),
				static_cast<int64>(StartResult.ObedienceCap.GetValue()),
				static_cast<int64>(Facts.ObedienceReferenceLevel)
					- static_cast<int64>(StartResult.ObedienceCap.GetValue()))
			&& StageEvent(
				EBattleEventType::ActionCompleted,
				EBattleEventCause::Action);
	}
	else
	{
		bEventsStaged = StageEvent(
				EBattleEventType::ActionCanceled,
				EBattleEventCause::Action)
			&& StageEvent(
				EBattleEventType::ActionCompleted,
				EBattleEventCause::Action);
	}
	for (const FBattleReplacementRequirement& Requirement : ReplacementRequirements)
	{
		if (!bEventsStaged)
		{
			break;
		}
		bEventsStaged = StageEvent(
			EBattleEventType::ReplacementRequired,
			EBattleEventCause::Rule,
			TOptional<int64>(),
			TOptional<int64>(),
			TOptional<int64>(),
			EBattleVisibilityLevel::Public,
			&Requirement.Target);
	}
	if (!bEventsStaged)
	{
		check(StageFailureReason != EBattleRejectionReason::None);
		return RejectPreparedCheckpoint(StageFailureReason);
	}
	if (!FBattleResolutionCommit::TryFinishAcceptedPlan(CommitPlan))
	{
		return RejectPreparedCheckpoint(
			EBattleRejectionReason::CheckpointPreparationFailed);
	}
	if (!IsActionStartCheckpointIdentityCurrent(*State, CheckpointIdentity))
	{
		return RejectPreparedCheckpoint(
			EBattleRejectionReason::StaleCheckpointIdentity);
	}

	ApplyActionStartDelta(*State, CheckpointIdentity, Delta);
	const FBattleResolution Resolution = FBattleResolutionCommit::PublishPrepared(
		*State,
		CommitPlan);
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
	const FActionId ActionId = Action->ActionId;
	const FTrainerId DecisionOwnerTrainerId =
		Action->Decision.GetDecisionOwnerTrainerId();
	const FBattlerId OutgoingBattlerId = Action->Decision.GetActingBattlerId();
	const FBattleEventSource Source = SourceFromLockedAction(*State, *Action);
	FVoluntarySwitchCheckpointIdentity CheckpointIdentity;
	if (!TryCaptureVoluntarySwitchCheckpointIdentity(
			*State,
			ResolutionId,
			*Action,
			CheckpointIdentity))
	{
		return PublishVoluntarySwitchCheckpointRejection(
			*State,
			ResolutionId,
			ActionId,
			EBattleRejectionReason::CheckpointPreparationFailed,
			DecisionOwnerTrainerId,
			OutgoingBattlerId,
			Source);
	}

	FBattleResolutionCommitPlan CommitPlan;
	if (!FBattleResolutionCommit::TryBeginAcceptedPlan(
			CheckpointIdentity.CommitIdentity,
			CommitPlan))
	{
		return PublishVoluntarySwitchCheckpointRejection(
			*State,
			ResolutionId,
			ActionId,
			EBattleRejectionReason::CheckpointPreparationFailed,
			DecisionOwnerTrainerId,
			OutgoingBattlerId,
			Source);
	}
	auto RejectPreparedCheckpoint = [&](const EBattleRejectionReason Reason)
	{
		return PublishVoluntarySwitchCheckpointRejection(
			*State,
			ResolutionId,
			ActionId,
			Reason,
			DecisionOwnerTrainerId,
			OutgoingBattlerId,
			Source);
	};

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
	FNoDrawBattleRandom NoDrawRandom;
	const bool bResolved = bLegalityBuilt
		&& FBattleSwitchResolver::TryResolve(
			Legality,
			SelectionSpec,
			NoDrawRandom,
			SwitchResolution);
	if (!bResolved || !NoDrawRandom.GetTrace().IsEmpty())
	{
		return RejectPreparedCheckpoint(
			EBattleRejectionReason::CheckpointPreparationFailed);
	}

	FSwitchCheckpointPreparation Preparation;
	if (!Preparation.Capture(
			*State,
			CheckpointIdentity.CommitIdentity.OwningActionId))
	{
		return RejectPreparedCheckpoint(
			EBattleRejectionReason::CheckpointPreparationFailed);
	}
	FMutableFieldSideCheckpointView Projection(
		*State,
		Preparation.Common,
		Preparation.Field,
		Preparation.Sides);
	FBattleLockedActionState& ProjectedAction = Preparation.Action;
	TArray<FBattleEvent> Events;
	FBattleEventTarget OutgoingTarget;
	FBattleEventTarget IncomingTarget;
	if (SwitchResolution.HasSelection())
	{
		if (!TryApplySwitchSelection(
				Projection,
				DecisionOwnerTrainerId,
				OutgoingBattlerId,
				ProjectedAction.OrderKey.ActingSlotId,
				SwitchResolution,
				OutgoingTarget,
				IncomingTarget))
		{
			return RejectPreparedCheckpoint(
				EBattleRejectionReason::CheckpointPreparationFailed);
		}
		AppendSwitchTransitionEvents(
			Projection,
			ResolutionId,
			ProjectedAction,
			OutgoingTarget,
			IncomingTarget,
			Events);
		if (!TryRevealAirBalloonOnEntry(
				Projection,
				IncomingTarget.BattlerId,
				ResolutionId,
				EBattleActionKind::Switch,
				Events))
		{
			return RejectPreparedCheckpoint(
				EBattleRejectionReason::CheckpointPreparationFailed);
		}
		if (!TryResolveEntryHazards(
				Projection,
				IncomingTarget.BattlerId,
				IncomingTarget.ActiveSlotId,
				ResolutionId,
				Events))
		{
			return RejectPreparedCheckpoint(
				EBattleRejectionReason::CheckpointPreparationFailed);
		}
		if (!TryResolveImmediateHeldItem(
				Projection,
				IncomingTarget.BattlerId,
				ResolutionId,
				ActionId,
				EBattleActionKind::Switch,
				Events))
		{
			return RejectPreparedCheckpoint(
				EBattleRejectionReason::CheckpointPreparationFailed);
		}
		const TArray<FBattlerId> AbilityEntrants{IncomingTarget.BattlerId};
		if (!TryResolveAbilityEntries(
				Projection,
				AbilityEntrants,
				ResolutionId,
				EBattleActionKind::Switch,
				Events))
		{
			return RejectPreparedCheckpoint(
				EBattleRejectionReason::CheckpointPreparationFailed);
		}
	}
	else
	{
		Events.Add(MakeActionDetailEvent(
			Projection,
			ResolutionId,
			ProjectedAction,
			EBattleEventType::ActionCanceled,
			EBattleEventCause::Switch));
	}

	ProjectedAction.bFinished = true;
	Events.Add(MakeActionDetailEvent(
		Projection,
		ResolutionId,
		ProjectedAction,
		EBattleEventType::ActionCompleted,
		EBattleEventCause::Action));
	++Projection.CurrentLockedActionIndex;
	if (!TryAppendAtomicSwitchBoundaryEvents(
			Projection,
			ResolutionId,
			ProjectedAction,
			Events))
	{
		return RejectPreparedCheckpoint(
			EBattleRejectionReason::CheckpointPreparationFailed);
	}

	for (const FBattleEvent& Event : Events)
	{
		if (!FBattleResolutionCommit::TryStageEvent(
				CommitPlan,
				MakeAtomicSwitchStagedEventSpec(Event)))
		{
			return RejectPreparedCheckpoint(
				EBattleRejectionReason::CheckpointPreparationFailed);
		}
	}
	FAtomicSwitchStateDelta Delta;
	if (!TryCaptureAtomicSwitchDelta(Preparation, Delta))
	{
		return RejectPreparedCheckpoint(
			EBattleRejectionReason::CheckpointPreparationFailed);
	}
	if (!FBattleResolutionCommit::TryFinishAcceptedPlan(CommitPlan))
	{
		return RejectPreparedCheckpoint(
			EBattleRejectionReason::CheckpointPreparationFailed);
	}
	if (!IsVoluntarySwitchCheckpointIdentityCurrent(*State, CheckpointIdentity)
		|| !AreAtomicCheckpointCommonDeltaRecordsValid(
			CheckpointIdentity.Battlers,
			CheckpointIdentity.ActivePositions,
			Delta))
	{
		return RejectPreparedCheckpoint(
			EBattleRejectionReason::StaleCheckpointIdentity);
	}

	ApplyVoluntarySwitchDelta(*State, CheckpointIdentity, Delta);
	const FBattleResolution Resolution = FBattleResolutionCommit::PublishPrepared(
		*State,
		CommitPlan);
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
	const FActionId ActionId = Action->ActionId;
	const FTrainerId DecisionOwnerTrainerId = Action->Decision.GetDecisionOwnerTrainerId();
	const FBattlerId ActingBattlerId = Action->Decision.GetActingBattlerId();
	const FBattleEventSource Source = SourceFromLockedAction(*State, *Action);

	FBattleResolutionCommitIdentity CommitIdentity;
	if (!FBattleResolutionCommit::TryCaptureIdentity(
			*State,
			ResolutionId,
			ActionId,
			CommitIdentity))
	{
		return PublishWildActionCheckpointRejection(
			*State,
			ResolutionId,
			ActionId,
			EBattleRejectionReason::CheckpointPreparationFailed,
			DecisionOwnerTrainerId,
			ActingBattlerId,
			ActionKind,
			Source);
	}

	FWildActionStateDelta Delta;
	InitializeWildActionDelta(*State, Delta);
	FBattleResolutionCommitPlan CommitPlan;
	if (!FBattleResolutionCommit::TryBeginAcceptedPlan(CommitIdentity, CommitPlan))
	{
		return PublishWildActionCheckpointRejection(
			*State,
			ResolutionId,
			ActionId,
			EBattleRejectionReason::CheckpointPreparationFailed,
			DecisionOwnerTrainerId,
			ActingBattlerId,
			ActionKind,
			Source);
	}

	TUniquePtr<IBattleRandomTransaction> RandomTransaction;
	bool bCommitRandomTransaction = false;
	auto RejectPreparedCheckpoint = [&](const EBattleRejectionReason Reason)
	{
		if (RandomTransaction.IsValid())
		{
			RandomTransaction->Rollback();
			RandomTransaction.Reset();
		}
		return PublishWildActionCheckpointRejection(
			*State,
			ResolutionId,
			ActionId,
			Reason,
			DecisionOwnerTrainerId,
			ActingBattlerId,
			ActionKind,
			Source);
	};

	auto StageEvent = [&](const EBattleEventType Type,
		const EBattleEventCause Cause,
		const EBattleOutcomeCause OutcomeCause,
		const FBattleEventTarget* Target)
	{
		return FBattleResolutionCommit::TryStageEvent(
			CommitPlan,
			MakeStagedWildActionEventSpec(
				*State,
				ResolutionId,
				ActionId,
				ActionKind,
				Source,
				Type,
				Cause,
				OutcomeCause,
				Target));
	};

	auto TryFinishPreparedAction = [&](const TOptional<EBattleOutcomeCause>& TerminalCause)
	{
		if (!StageEvent(
				EBattleEventType::ActionCompleted,
				EBattleEventCause::Action,
				EBattleOutcomeCause::None,
				nullptr)
			|| !TryPrepareWildActionBoundary(*State, TerminalCause.IsSet(), Delta))
		{
			return false;
		}
		if (TerminalCause.IsSet()
			&& !StageEvent(
				EBattleEventType::BattleEnded,
				EBattleEventCause::Outcome,
				TerminalCause.GetValue(),
				nullptr))
		{
			return false;
		}
		return FBattleResolutionCommit::TryFinishAcceptedPlan(CommitPlan);
	};

	auto CommitPreparedAction = [&]()
	{
		if (!FBattleResolutionCommit::IsIdentityCurrent(*State, CommitIdentity))
		{
			return RejectPreparedCheckpoint(EBattleRejectionReason::StaleCheckpointIdentity);
		}
		if (bCommitRandomTransaction)
		{
			check(RandomTransaction.IsValid());
			EBattleRandomTransactionCommitError RandomError =
				EBattleRandomTransactionCommitError::None;
			if (!RandomTransaction->TryCommit(
					*State->Random,
					ResolutionId,
					ActionId,
					RandomError))
			{
				return RejectPreparedCheckpoint(
					EBattleRejectionReason::RandomTransactionCommitFailed);
			}
			RandomTransaction.Reset();
		}
		else if (RandomTransaction.IsValid())
		{
			RandomTransaction->Rollback();
			RandomTransaction.Reset();
		}

		ApplyWildActionDelta(*State, CommitIdentity, Delta);
		const FBattleResolution Resolution = FBattleResolutionCommit::PublishPrepared(
			*State,
			CommitPlan);
		EBattleStateValidationError StateError = EBattleStateValidationError::None;
		const bool bStateValid = State->ValidateInvariants(StateError);
		check(bStateValid);
		return Resolution;
	};

	const FBattleBattlerState* ActingBattler = State->FindBattler(ActingBattlerId);
	const FBattleTrainerState* ActingTrainer = ActingBattler != nullptr
		? State->FindTrainer(ActingBattler->TrainerId)
		: nullptr;
	auto PrepareAcceptedCancellation = [&]()
	{
		return StageEvent(
				EBattleEventType::ActionCanceled,
				EBattleEventCause::Run,
				EBattleOutcomeCause::None,
				nullptr)
			&& TryFinishPreparedAction(TOptional<EBattleOutcomeCause>());
	};

	if (ActingBattler == nullptr || ActingTrainer == nullptr)
	{
		if (!PrepareAcceptedCancellation())
		{
			return RejectPreparedCheckpoint(
				EBattleRejectionReason::CheckpointPreparationFailed);
		}
		return CommitPreparedAction();
	}

	if (ActionKind == EBattleActionKind::Run)
	{
		const FBattleBattlerState* WildOpponent = FindLeftmostLivingWildOpponent(*State);
		if (!CanOfferRunAction(*State, *ActingTrainer, *ActingBattler)
			|| WildOpponent == nullptr)
		{
			if (!PrepareAcceptedCancellation())
			{
				return RejectPreparedCheckpoint(
					EBattleRejectionReason::CheckpointPreparationFailed);
			}
			return CommitPreparedAction();
		}

		FBattleRunCalculationInput Input;
		Input.PlayerPermanentSpeed = ActingBattler->PermanentStats.Speed;
		Input.WildPermanentSpeed = WildOpponent->PermanentStats.Speed;
		Input.EscapeAttemptCount = State->EscapeAttemptCount;
		Input.RandomContext.BattleId = State->Setup.GetBattleId();
		Input.RandomContext.TurnId = State->TurnId;
		Input.RandomContext.ActionId = ActionId;
		Input.RandomContext.ResolutionId = ResolutionId;
		if (!State->Random->TryCreateTransaction(
				ResolutionId,
				ActionId,
				RandomTransaction))
		{
			return RejectPreparedCheckpoint(
				EBattleRejectionReason::CheckpointRandomStageFailed);
		}

		FBattleRunCalculationResult Result;
		if (!FBattleRunRules::TryResolve(Input, *RandomTransaction, Result))
		{
			return RejectPreparedCheckpoint(
				EBattleRejectionReason::CheckpointRandomStageFailed);
		}
		bCommitRandomTransaction = Result.RandomDraw.IsSet();
		if (!bCommitRandomTransaction)
		{
			RandomTransaction->Rollback();
			RandomTransaction.Reset();
		}

		if (!StageEvent(
				EBattleEventType::RunAttempted,
				EBattleEventCause::Run,
				EBattleOutcomeCause::None,
				nullptr))
		{
			return RejectPreparedCheckpoint(
				EBattleRejectionReason::CheckpointPreparationFailed);
		}
		if (!Result.bSucceeded)
		{
			if (Delta.EscapeAttemptCount == TNumericLimits<uint32>::Max())
			{
				return RejectPreparedCheckpoint(
					EBattleRejectionReason::CheckpointPreparationFailed);
			}
			++Delta.EscapeAttemptCount;
			if (!TryFinishPreparedAction(TOptional<EBattleOutcomeCause>()))
			{
				return RejectPreparedCheckpoint(
					EBattleRejectionReason::CheckpointPreparationFailed);
			}
			return CommitPreparedAction();
		}

		Delta.CleanupStage.Capture(*State);
		if (!TryStageBattleEndCleanup(Delta.CleanupStage))
		{
			return RejectPreparedCheckpoint(
				EBattleRejectionReason::CheckpointPreparationFailed);
		}
		Delta.bApplyCleanupStage = true;
		Delta.Phase = EBattlePhase::Terminal;
		Delta.Outcome = EBattleOutcome::Escape;
		Delta.OutcomeCause = EBattleOutcomeCause::Ordinary;
		if (!StageEvent(
				EBattleEventType::Escaped,
				EBattleEventCause::Run,
				EBattleOutcomeCause::None,
				nullptr)
			|| !TryFinishPreparedAction(
				TOptional<EBattleOutcomeCause>(EBattleOutcomeCause::Ordinary)))
		{
			return RejectPreparedCheckpoint(
				EBattleRejectionReason::CheckpointPreparationFailed);
		}
		return CommitPreparedAction();
	}

	const FBattleWildFleePolicyState* Policy = FindWildFleePolicy(*State, *ActingBattler);
	const FBattleActivePositionState* Active = FindActiveForBattler(
		*State,
		ActingBattler->BattlerId);
	if (!CanOfferWildFleeAction(*State, *ActingTrainer, *ActingBattler)
		|| Policy == nullptr
		|| Active == nullptr)
	{
		if (!PrepareAcceptedCancellation())
		{
			return RejectPreparedCheckpoint(
				EBattleRejectionReason::CheckpointPreparationFailed);
		}
		return CommitPreparedAction();
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
	Input.RandomContext.ActionId = ActionId;
	Input.RandomContext.ResolutionId = ResolutionId;

	FBattleWildFleeCalculationResult Result;
	if (Policy->ProbabilityMode == EBattleWildFleeMode::Chance)
	{
		if (!State->Random->TryCreateTransaction(
				ResolutionId,
				ActionId,
				RandomTransaction)
			|| !FBattleWildFleeRules::TryResolve(Input, *RandomTransaction, Result)
			|| !Result.RandomDraw.IsSet())
		{
			return RejectPreparedCheckpoint(
				EBattleRejectionReason::CheckpointRandomStageFailed);
		}
		bCommitRandomTransaction = true;
	}
	else
	{
		FNoDrawBattleRandom NoDrawRandom;
		if (!FBattleWildFleeRules::TryResolve(Input, NoDrawRandom, Result)
			|| Result.RandomDraw.IsSet())
		{
			return RejectPreparedCheckpoint(
				EBattleRejectionReason::CheckpointPreparationFailed);
		}
	}

	if (!StageEvent(
			EBattleEventType::RunAttempted,
			EBattleEventCause::Run,
			EBattleOutcomeCause::None,
			nullptr))
	{
		return RejectPreparedCheckpoint(
			EBattleRejectionReason::CheckpointPreparationFailed);
	}
	if (!Result.bSucceeded)
	{
		if (!TryFinishPreparedAction(TOptional<EBattleOutcomeCause>()))
		{
			return RejectPreparedCheckpoint(
				EBattleRejectionReason::CheckpointPreparationFailed);
		}
		return CommitPreparedAction();
	}

	Delta.CleanupStage.Capture(*State);
	if (!TryStageSourceDependentVolatileCleanup(
			Delta.CleanupStage,
			ActingBattler->BattlerId)
		|| !TryStageAbilityCleanup(
			Delta.CleanupStage,
			ActingBattler->AbilityId,
			ActingBattler->BattlerId)
		|| !TryStageItemCleanup(
			Delta.CleanupStage,
			ActingBattler->HeldItem.CurrentItemId,
			ActingBattler->BattlerId)
		|| !TryStageMajorStatusCleanup(
			Delta.CleanupStage,
			ActingBattler->MajorStatusId,
			ActingBattler->BattlerId)
		|| !TryStageAllOwnedVolatileCleanup(
			Delta.CleanupStage,
			ActingBattler->BattlerId))
	{
		return RejectPreparedCheckpoint(
			EBattleRejectionReason::CheckpointPreparationFailed);
	}
	Delta.bApplyCleanupStage = true;
	Delta.bRemoveBattler = true;
	Delta.RemovedBattlerId = ActingBattler->BattlerId;
	Delta.ClearedActiveSlotId = Active->ActiveSlotId;

	FBattleEventTarget FleeingTarget;
	FleeingTarget.TrainerId = ActingBattler->TrainerId;
	FleeingTarget.BattlerId = ActingBattler->BattlerId;
	FleeingTarget.ActiveSlotId = Active->ActiveSlotId;
	if (!StageEvent(
			EBattleEventType::Escaped,
			EBattleEventCause::Run,
			EBattleOutcomeCause::None,
			&FleeingTarget)
		|| !StageEvent(
			EBattleEventType::LeftActiveSlot,
			EBattleEventCause::Run,
			EBattleOutcomeCause::None,
			&FleeingTarget)
		|| !StageEvent(
			EBattleEventType::Removed,
			EBattleEventCause::Run,
			EBattleOutcomeCause::None,
			&FleeingTarget)
		|| !StageEvent(
			EBattleEventType::OpponentRemovalCheckpoint,
			EBattleEventCause::Rule,
			EBattleOutcomeCause::None,
			&FleeingTarget))
	{
		return RejectPreparedCheckpoint(
			EBattleRejectionReason::CheckpointPreparationFailed);
	}
	Delta.OpponentRemovalCheckpointOrdinal =
		CommitPlan.Events.Last().GetEventOrdinal();

	bool bLivingOpponentRemains = false;
	for (const FBattleBattlerState& Battler : State->Battlers)
	{
		const FBattleTrainerState* Trainer = State->FindTrainer(Battler.TrainerId);
		if (Battler.BattlerId != ActingBattler->BattlerId
			&& Trainer != nullptr
			&& Trainer->Side == EBattleSide::Opponent
			&& IsLivingSelectableBattler(&Battler))
		{
			bLivingOpponentRemains = true;
			break;
		}
	}

	TOptional<EBattleOutcomeCause> TerminalCause;
	if (!bLivingOpponentRemains)
	{
		if (!TryStageBattleEndCleanup(Delta.CleanupStage))
		{
			return RejectPreparedCheckpoint(
				EBattleRejectionReason::CheckpointPreparationFailed);
		}
		Delta.Phase = EBattlePhase::Terminal;
		Delta.Outcome = EBattleOutcome::Escape;
		Delta.OutcomeCause = EBattleOutcomeCause::OpponentFled;
		TerminalCause = EBattleOutcomeCause::OpponentFled;
	}
	if (!TryFinishPreparedAction(TerminalCause))
	{
		return RejectPreparedCheckpoint(
			EBattleRejectionReason::CheckpointPreparationFailed);
	}
	return CommitPreparedAction();
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

	const FActionId AcceptedCancellationActionId = Action->ActionId;
	const FTrainerId AcceptedCancellationTrainerId =
		Action->Decision.GetDecisionOwnerTrainerId();
	const FBattlerId AcceptedCancellationBattlerId =
		Action->Decision.GetActingBattlerId();
	const FBattleEventSource AcceptedCancellationSource =
		SourceFromLockedAction(*State, *Action);
	auto CancelStaleUse =
		[this,
		 ResolutionId,
		 AcceptedCancellationActionId,
		 AcceptedCancellationTrainerId,
		 AcceptedCancellationBattlerId,
		 AcceptedCancellationSource]()
	{
		FAcceptedBagCancellationDelta Delta;
		if (!FBattleResolutionCommit::TryCaptureIdentity(
				*State,
				ResolutionId,
				AcceptedCancellationActionId,
				Delta.ActionIdentity))
		{
			return PublishBagItemCheckpointRejection(
				*State,
				ResolutionId,
				AcceptedCancellationActionId,
				EBattleRejectionReason::CheckpointPreparationFailed,
				AcceptedCancellationTrainerId,
				AcceptedCancellationBattlerId,
				AcceptedCancellationSource);
		}

		auto RejectPreparation =
			[this,
			 ResolutionId,
			 AcceptedCancellationActionId,
			 AcceptedCancellationTrainerId,
			 AcceptedCancellationBattlerId,
			 AcceptedCancellationSource](
				const EBattleRejectionReason Reason)
			{
				return PublishBagItemCheckpointRejection(
					*State,
					ResolutionId,
					AcceptedCancellationActionId,
					Reason,
					AcceptedCancellationTrainerId,
					AcceptedCancellationBattlerId,
					AcceptedCancellationSource);
			};
		if (!FBattleResolutionCommit::TryBeginAcceptedPlan(
				Delta.ActionIdentity,
				Delta.ResolutionPlan))
		{
			return RejectPreparation(
				EBattleRejectionReason::CheckpointPreparationFailed);
		}

		auto StageEvent =
			[this,
			 ResolutionId,
			 AcceptedCancellationActionId,
			 AcceptedCancellationSource,
			 &Delta](
				const EBattleEventType Type,
				const EBattleEventCause Cause,
				const FBattleEventTarget* Target = nullptr)
			{
				return FBattleResolutionCommit::TryStageEvent(
					Delta.ResolutionPlan,
					MakeStagedBagItemEventSpec(
						*State,
						ResolutionId,
						AcceptedCancellationActionId,
						AcceptedCancellationSource,
						Type,
						Cause,
						Target));
			};
		if (!StageEvent(
				EBattleEventType::ActionCanceled,
				EBattleEventCause::Item)
			|| !StageEvent(
				EBattleEventType::ActionCompleted,
				EBattleEventCause::Action))
		{
			return RejectPreparation(
				EBattleRejectionReason::CheckpointPreparationFailed);
		}

		if (Delta.ActionIdentity.ExpectedStateVersion
				== TNumericLimits<uint64>::Max()
			|| Delta.ActionIdentity.ExpectedLockedActionIndex
				== TNumericLimits<int32>::Max())
		{
			return RejectPreparation(
				EBattleRejectionReason::CheckpointPreparationFailed);
		}
		Delta.NextLockedActionIndex =
			Delta.ActionIdentity.ExpectedLockedActionIndex + 1;
		if (Delta.NextLockedActionIndex > State->LockedActions.Num())
		{
			return RejectPreparation(
				EBattleRejectionReason::CheckpointPreparationFailed);
		}

		Delta.Phase = State->Phase;
		Delta.PendingReplacements = State->PendingReplacements;
		Delta.PendingDecision = State->PendingDecision;
		Delta.PendingDecisionRequests = State->PendingDecisionRequests;

		FBattleQueueBoundaryPlan BoundaryPlan;
		if (!FBattleFaintOutcomeResolver::ResolveQueueBoundary(
				Delta.Phase,
				State->Outcome,
				Delta.NextLockedActionIndex,
				State->LockedActions.Num(),
				State->Setup.GetStartingActive(),
				State->Battlers,
				State->ActivePositions,
				BoundaryPlan)
			|| !FBattleFaintOutcomeResolver::TryApplyQueueBoundaryPlan(
				Delta.Phase,
				BoundaryPlan))
		{
			return RejectPreparation(
				EBattleRejectionReason::CheckpointPreparationFailed);
		}

		if (Delta.Phase == EBattlePhase::MandatoryReplacement)
		{
			if (BoundaryPlan.Requirements.IsEmpty())
			{
				return RejectPreparation(
					EBattleRejectionReason::CheckpointPreparationFailed);
			}
			Delta.PendingReplacements.Reset();
			Delta.PendingDecision.Reset();
			Delta.PendingDecisionRequests.Reset();
			for (const FBattleReplacementRequirement& Requirement :
				BoundaryPlan.Requirements)
			{
				FBattlePendingReplacementState& Pending =
					Delta.PendingReplacements.AddDefaulted_GetRef();
				Pending.TrainerId = Requirement.Target.TrainerId;
				Pending.ActiveSlotId = Requirement.Target.ActiveSlotId;
			}

			const FAcceptedBagCancellationStateView StagedState(*State, Delta);
			if (!TryBuildReplacementCheckpointRequests(
					StagedState,
					Delta.ActionIdentity.ExpectedStateVersion + 1,
					true,
					Delta.PendingDecisionRequests)
				|| Delta.PendingDecisionRequests.IsEmpty())
			{
				return RejectPreparation(
					EBattleRejectionReason::CheckpointPreparationFailed);
			}
			Delta.PendingDecision = Delta.PendingDecisionRequests[0];
		}
		else if (Delta.Phase == EBattlePhase::EndOfTurn)
		{
			Delta.PendingReplacements.Reset();
			Delta.PendingDecision.Reset();
			Delta.PendingDecisionRequests.Reset();
		}
		else if (Delta.Phase != EBattlePhase::Resolving)
		{
			return RejectPreparation(
				EBattleRejectionReason::CheckpointPreparationFailed);
		}

		for (const FBattleReplacementRequirement& Requirement :
			BoundaryPlan.Requirements)
		{
			if (!StageEvent(
					EBattleEventType::ReplacementRequired,
					EBattleEventCause::Rule,
					&Requirement.Target))
			{
				return RejectPreparation(
					EBattleRejectionReason::CheckpointPreparationFailed);
			}
		}
		if (!FBattleResolutionCommit::TryFinishAcceptedPlan(
				Delta.ResolutionPlan)
			|| !IsAcceptedBagCancellationDeltaValid(
				*State,
				Delta,
				BoundaryPlan))
		{
			return RejectPreparation(
				EBattleRejectionReason::CheckpointPreparationFailed);
		}

		if (!FBattleResolutionCommit::IsIdentityCurrent(
				*State,
				Delta.ActionIdentity))
		{
			return RejectPreparation(
				EBattleRejectionReason::StaleCheckpointIdentity);
		}
		if (!State->LockedActions.IsValidIndex(
				Delta.ActionIdentity.ExpectedLockedActionIndex)
			|| State->LockedActions[
					Delta.ActionIdentity.ExpectedLockedActionIndex].ActionId
				!= Delta.ActionIdentity.OwningActionId)
		{
			return RejectPreparation(
				EBattleRejectionReason::StaleCheckpointIdentity);
		}

		FBattleLockedActionState& CommittedAction =
			State->LockedActions[
				Delta.ActionIdentity.ExpectedLockedActionIndex];
		CommittedAction.bFinished = true;
		State->CurrentLockedActionIndex = Delta.NextLockedActionIndex;
		State->Phase = Delta.Phase;
		State->PendingReplacements = Delta.PendingReplacements;
		State->PendingDecision = Delta.PendingDecision;
		State->PendingDecisionRequests = Delta.PendingDecisionRequests;
		return FBattleResolutionCommit::PublishPrepared(
			*State,
			Delta.ResolutionPlan);
	};
	const FBattleTrainerEncounterPolicy* ActingTrainerPolicy = ActingTrainer != nullptr
		? FindTrainerEncounterPolicy(*State, ActingTrainer->TrainerId)
		: nullptr;
	if (TargetBattler == nullptr
		|| ActingTrainerPolicy == nullptr
		|| !ActingTrainerPolicy->bMayUseBag)
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

	FBattleResolutionCommitIdentity BagCommitIdentity;
	FBattleResolutionCommitPlan BagCommitPlan;
	FBagItemStateDelta BagDelta;
	bool bBagCheckpointPrepared = false;
	if (!bCaptureUse)
	{
		const FActionId ActionId = Action->ActionId;
		const FTrainerId ActingTrainerId = ActingTrainer->TrainerId;
		const FBattlerId ActingBattlerId = ActingBattler->BattlerId;
		const FBattleEventSource Source = SourceFromLockedAction(*State, *Action);
		if (!FBattleResolutionCommit::TryCaptureIdentity(
				*State,
				ResolutionId,
				ActionId,
				BagCommitIdentity))
		{
			return PublishBagItemCheckpointRejection(
				*State,
				ResolutionId,
				ActionId,
				EBattleRejectionReason::CheckpointPreparationFailed,
				ActingTrainerId,
				ActingBattlerId,
				Source);
		}

		auto RejectPreparedCheckpoint = [&](const EBattleRejectionReason Reason)
		{
			return PublishBagItemCheckpointRejection(
				*State,
				ResolutionId,
				ActionId,
				Reason,
				ActingTrainerId,
				ActingBattlerId,
				Source);
		};
		if (!FBattleResolutionCommit::TryBeginAcceptedPlan(
				BagCommitIdentity,
				BagCommitPlan))
		{
			return RejectPreparedCheckpoint(
				EBattleRejectionReason::CheckpointPreparationFailed);
		}

		BagDelta.ActingTrainerId = ActingTrainerId;
		BagDelta.Bag = AppliedBag->Items;
		BagDelta.bBagActionAvailable = AppliedBag->bBagActionAvailable;
		BagDelta.TargetBattlerId = TargetBattler->BattlerId;
		BagDelta.TargetBattler = *TargetBattler;

		FBattleEventTarget EventTarget;
		EventTarget.TrainerId = TargetBattler->TrainerId;
		EventTarget.BattlerId = TargetBattler->BattlerId;
		if (TargetActive != nullptr
			&& TargetActive->BattlerId == TargetBattler->BattlerId)
		{
			EventTarget.ActiveSlotId = TargetActive->ActiveSlotId;
		}

		if (UseResult.bCuresMajorStatus || UseResult.bCuresConfusion)
		{
			BagDelta.CleanupStage.Capture(*State);
			if ((UseResult.bCuresMajorStatus
					&& !TryStageBagItemMajorStatusCleanup(
						BagDelta.CleanupStage,
						TargetBattler->MajorStatusId,
						TargetBattler->BattlerId))
				|| (UseResult.bCuresConfusion
					&& !TryStageBagItemVolatileCleanup(
						BagDelta.CleanupStage,
						FBattleVolatileRules::GetConfusionId(),
						TargetBattler->BattlerId)))
			{
				return RejectPreparedCheckpoint(
					EBattleRejectionReason::CheckpointPreparationFailed);
			}
			BagDelta.bApplyCleanupStage = true;
		}

		TArray<EBattleEventType> EffectEventTypes;
		TOptional<int64> EffectNumericBefore;
		TOptional<int64> EffectNumericAfter;
		TOptional<int64> EffectNumericDelta;
		switch (UseResult.Kind)
		{
		case EBattleBagItemRuleKind::HyperPotion:
		{
			const int32 PreviousHP = BagDelta.TargetBattler.CurrentHP;
			if (UseResult.HealAmount <= 0
				|| PreviousHP > BagDelta.TargetBattler.PermanentStats.MaxHP
					- UseResult.HealAmount)
			{
				return RejectPreparedCheckpoint(
					EBattleRejectionReason::CheckpointPreparationFailed);
			}
			BagDelta.TargetBattler.CurrentHP += UseResult.HealAmount;
			EffectEventTypes = {
				EBattleEventType::Healing,
				EBattleEventType::HPChanged};
			EffectNumericBefore = static_cast<int64>(PreviousHP);
			EffectNumericAfter = static_cast<int64>(
				BagDelta.TargetBattler.CurrentHP);
			EffectNumericDelta = static_cast<int64>(UseResult.HealAmount);
			break;
		}
		case EBattleBagItemRuleKind::Revive:
		{
			const int32 PreviousHP = BagDelta.TargetBattler.CurrentHP;
			if (UseResult.HealAmount <= 0
				|| UseResult.HealAmount
					> BagDelta.TargetBattler.PermanentStats.MaxHP)
			{
				return RejectPreparedCheckpoint(
					EBattleRejectionReason::CheckpointPreparationFailed);
			}
			BagDelta.TargetBattler.CurrentHP = UseResult.HealAmount;
			BagDelta.TargetBattler.bFainted = false;
			BagDelta.TargetBattler.bFaintTransitionPending = false;
			BagDelta.TargetBattler.bRemoved = false;
			BagDelta.TargetBattler.MajorStatusId = FConditionId();
			BagDelta.TargetBattler.Stages = FBattleStatStages();
			BagDelta.TargetBattler.Volatiles.Reset();
			BagDelta.TargetBattler.LastMoveId = FMoveId();
			BagDelta.TargetBattler.bAbilitySuppressed = false;
			BagDelta.TargetBattler.HeldItem.ChoiceLockedMoveId = FMoveId();
			BagDelta.TargetBattler.EnteredActiveOnTurnId = FTurnId();
			EffectEventTypes = {
				EBattleEventType::Healing,
				EBattleEventType::HPChanged};
			EffectNumericBefore = static_cast<int64>(PreviousHP);
			EffectNumericAfter = static_cast<int64>(
				BagDelta.TargetBattler.CurrentHP);
			EffectNumericDelta = static_cast<int64>(UseResult.HealAmount);
			break;
		}
		case EBattleBagItemRuleKind::FullHeal:
		{
			const int32 CuredCount = (UseResult.bCuresMajorStatus ? 1 : 0)
				+ (UseResult.bCuresConfusion ? 1 : 0);
			if (CuredCount <= 0)
			{
				return RejectPreparedCheckpoint(
					EBattleRejectionReason::CheckpointPreparationFailed);
			}
			if (UseResult.bCuresMajorStatus)
			{
				BagDelta.TargetBattler.MajorStatusId = FConditionId();
			}
			if (UseResult.bCuresConfusion)
			{
				BagDelta.TargetBattler.Volatiles.RemoveAll(
					[](const FBattleConditionState& Condition)
					{
						return Condition.ConditionId
							== FBattleVolatileRules::GetConfusionId();
					});
			}
			EffectEventTypes = {EBattleEventType::StatusChanged};
			EffectNumericBefore = static_cast<int64>(CuredCount);
			EffectNumericAfter = static_cast<int64>(0);
			EffectNumericDelta = static_cast<int64>(-CuredCount);
			break;
		}
		case EBattleBagItemRuleKind::XAttack:
		{
			const int32 PreviousStage = UseFacts.AttackStage;
			const FBattleStatStageChangeResult StageChange =
				BagDelta.TargetBattler.Stages.ApplyChange(
					EBattleStat::Attack,
					UseResult.RequestedAttackStageDelta);
			if (StageChange.Outcome != EBattleStatStageChangeOutcome::Applied
				|| StageChange.PreviousStage != PreviousStage
				|| StageChange.AppliedDelta != UseResult.AppliedAttackStageDelta
				|| StageChange.NewStage != UseResult.ResultingAttackStage)
			{
				return RejectPreparedCheckpoint(
					EBattleRejectionReason::CheckpointPreparationFailed);
			}
			EffectEventTypes = {EBattleEventType::StatStageChanged};
			EffectNumericBefore = static_cast<int64>(StageChange.PreviousStage);
			EffectNumericAfter = static_cast<int64>(StageChange.NewStage);
			EffectNumericDelta = static_cast<int64>(StageChange.AppliedDelta);
			break;
		}
		default:
			return RejectPreparedCheckpoint(
				EBattleRejectionReason::CheckpointPreparationFailed);
		}

		TArray<FBattleReplacementRequirement> ReplacementRequirements;
		if (BagCommitIdentity.ExpectedStateVersion == TNumericLimits<uint64>::Max()
			|| !TryPrepareBagItemBoundary(
				*State,
				BagCommitIdentity.ExpectedStateVersion + 1,
				BagDelta,
				ReplacementRequirements))
		{
			return RejectPreparedCheckpoint(
				EBattleRejectionReason::CheckpointPreparationFailed);
		}

		auto StageEvent = [&](const EBattleEventType Type,
			const EBattleEventCause Cause,
			const FBattleEventTarget* Target,
			const TOptional<int64> NumericBefore = TOptional<int64>(),
			const TOptional<int64> NumericAfter = TOptional<int64>(),
			const TOptional<int64> NumericDelta = TOptional<int64>())
		{
			return FBattleResolutionCommit::TryStageEvent(
				BagCommitPlan,
				MakeStagedBagItemEventSpec(
					*State,
					ResolutionId,
					ActionId,
					Source,
					Type,
					Cause,
					Target,
					NumericBefore,
					NumericAfter,
					NumericDelta));
		};

		if (!StageEvent(
				EBattleEventType::ItemUsed,
				EBattleEventCause::Item,
				&EventTarget)
			|| !StageEvent(
				EBattleEventType::ItemConsumed,
				EBattleEventCause::Item,
				&EventTarget))
		{
			return RejectPreparedCheckpoint(
				EBattleRejectionReason::CheckpointPreparationFailed);
		}
		for (const EBattleEventType EffectEventType : EffectEventTypes)
		{
			if (!StageEvent(
					EffectEventType,
					EBattleEventCause::Item,
					&EventTarget,
					EffectNumericBefore,
					EffectNumericAfter,
					EffectNumericDelta))
			{
				return RejectPreparedCheckpoint(
					EBattleRejectionReason::CheckpointPreparationFailed);
			}
		}
		if (!StageEvent(
				EBattleEventType::ActionCompleted,
				EBattleEventCause::Action,
				nullptr))
		{
			return RejectPreparedCheckpoint(
				EBattleRejectionReason::CheckpointPreparationFailed);
		}
		for (const FBattleReplacementRequirement& Requirement :
			ReplacementRequirements)
		{
			if (!StageEvent(
					EBattleEventType::ReplacementRequired,
					EBattleEventCause::Rule,
					&Requirement.Target))
			{
				return RejectPreparedCheckpoint(
					EBattleRejectionReason::CheckpointPreparationFailed);
			}
		}
		if (!FBattleResolutionCommit::TryFinishAcceptedPlan(BagCommitPlan))
		{
			return RejectPreparedCheckpoint(
				EBattleRejectionReason::CheckpointPreparationFailed);
		}
		if (!FBattleResolutionCommit::IsIdentityCurrent(
				*State,
				BagCommitIdentity))
		{
			return RejectPreparedCheckpoint(
				EBattleRejectionReason::StaleCheckpointIdentity);
		}
		bBagCheckpointPrepared = true;
	}
	if (!bCaptureUse)
	{
		ActingTrainer->Bag = AppliedBag->Items;
		ActingTrainer->ActionAllowance.bBagActionAvailable =
			AppliedBag->bBagActionAvailable;
	}

	if (bCaptureUse)
	{
		const FActionId ActionId = Action->ActionId;
		const FTrainerId ActingTrainerId = ActingTrainer->TrainerId;
		const FBattlerId ActingBattlerId = ActingBattler->BattlerId;
		const FBattlerId TargetBattlerId = TargetBattler->BattlerId;
		const FBattleEventSource Source = SourceFromLockedAction(*State, *Action);
		FBattleResolutionCommitIdentity CommitIdentity;
		if (!FBattleResolutionCommit::TryCaptureIdentity(
				*State,
				ResolutionId,
				ActionId,
				CommitIdentity))
		{
			return PublishBagItemCheckpointRejection(
				*State,
				ResolutionId,
				ActionId,
				EBattleRejectionReason::CheckpointPreparationFailed,
				ActingTrainerId,
				ActingBattlerId,
				Source);
		}

		FBattleResolutionCommitPlan CommitPlan;
		TUniquePtr<IBattleRandomTransaction> RandomTransaction;
		auto RejectPreparedCheckpoint = [&](const EBattleRejectionReason Reason)
		{
			if (RandomTransaction.IsValid())
			{
				RandomTransaction->Rollback();
				RandomTransaction.Reset();
			}
			return PublishBagItemCheckpointRejection(
				*State,
				ResolutionId,
				ActionId,
				Reason,
				ActingTrainerId,
				ActingBattlerId,
				Source);
		};
		if (!FBattleResolutionCommit::TryBeginAcceptedPlan(
				CommitIdentity,
				CommitPlan))
		{
			return RejectPreparedCheckpoint(
				EBattleRejectionReason::CheckpointPreparationFailed);
		}

		FBattleCapturePreparation CapturePreparation;
		if (!FBattleCaptureCalculator::TryPrepare(
				CaptureInput,
				CapturePreparation))
		{
			return RejectPreparedCheckpoint(
				EBattleRejectionReason::CheckpointPreparationFailed);
		}

		FCaptureStateDelta CaptureDelta;
		InitializeCaptureDelta(
			*State,
			*AppliedBag,
			ActingTrainerId,
			TargetBattlerId,
			CaptureDelta);

		FBattleCaptureCalculationResult CaptureResult;
		if (CapturePreparation.bRequiresRandomResolution)
		{
			if (!State->Random->TryCreateTransaction(
					ResolutionId,
					ActionId,
					RandomTransaction)
				|| !RandomTransaction.IsValid()
				|| !FBattleCaptureCalculator::TryResolveRandom(
					CapturePreparation,
					*RandomTransaction,
					CaptureResult)
				|| RandomTransaction->GetTrace().IsEmpty())
			{
				return RejectPreparedCheckpoint(
					EBattleRejectionReason::CheckpointRandomStageFailed);
			}
		}
		else
		{
			CaptureResult = CapturePreparation.PreparedResult;
			if (!CaptureResult.bValid)
			{
				return RejectPreparedCheckpoint(
					EBattleRejectionReason::CheckpointPreparationFailed);
			}
		}

		FBattleEventTarget CaptureTarget;
		CaptureTarget.TrainerId = TargetBattler->TrainerId;
		CaptureTarget.BattlerId = TargetBattler->BattlerId;
		CaptureTarget.ActiveSlotId = TargetActive->ActiveSlotId;
		FBattleCaptureEventMetadata CaptureMetadata =
			FBattleCaptureCalculator::MakeEventMetadata(CaptureResult);
		TArray<FBattleReplacementRequirement> ReplacementRequirements;
		const bool bTerminalCapture = CaptureResult.bSucceeded
			&& !DoesLivingOpponentRemainAfterCapture(*State, TargetBattlerId);
		if (CaptureResult.bSucceeded)
		{
			FBattlePendingCaptureRecord PendingCapture;
			if (!TryBuildPendingCaptureRecord(
					*State,
					*TargetBattler,
					PendingCapture))
			{
				return RejectPreparedCheckpoint(
					EBattleRejectionReason::CheckpointPreparationFailed);
			}
			CaptureDelta.bSucceeded = true;
			CaptureDelta.PendingCapture = PendingCapture;
			CaptureDelta.ClearedActiveSlotId = TargetActive->ActiveSlotId;
			CaptureDelta.TargetBattler = *TargetBattler;
			CaptureDelta.TargetBattler.MajorStatusId = FConditionId();
			CaptureDelta.TargetBattler.Stages = FBattleStatStages();
			CaptureDelta.TargetBattler.Volatiles.Reset();
			CaptureDelta.TargetBattler.LastMoveId = FMoveId();
			CaptureDelta.TargetBattler.bAbilitySuppressed = false;
			CaptureDelta.TargetBattler.HeldItem.ChoiceLockedMoveId = FMoveId();
			CaptureDelta.TargetBattler.EnteredActiveOnTurnId = FTurnId();
			CaptureDelta.TargetBattler.bCaptured = true;
			CaptureDelta.TargetBattler.bRemoved = true;
			CaptureDelta.TargetBattler.bFaintTransitionPending = false;
			StageCaptureQueueCancellationFacts(
				*State,
				TargetBattlerId,
				CaptureDelta);

			CaptureDelta.CleanupStage.Capture(*State);
			if (!TryStageSourceDependentVolatileCleanup(
					CaptureDelta.CleanupStage,
					TargetBattlerId,
					EBattleTriggerCleanupReason::Capture)
				|| !TryStageAbilityCleanup(
					CaptureDelta.CleanupStage,
					TargetBattler->AbilityId,
					TargetBattlerId,
					EBattleTriggerCleanupReason::Capture)
				|| !TryStageItemCleanup(
					CaptureDelta.CleanupStage,
					TargetBattler->HeldItem.CurrentItemId,
					TargetBattlerId,
					EBattleTriggerCleanupReason::Capture)
				|| !TryStageMajorStatusCleanup(
					CaptureDelta.CleanupStage,
					TargetBattler->MajorStatusId,
					TargetBattlerId,
					EBattleTriggerCleanupReason::Capture)
				|| !TryStageAllOwnedVolatileCleanup(
					CaptureDelta.CleanupStage,
					TargetBattlerId,
					EBattleTriggerCleanupReason::Capture))
			{
				return RejectPreparedCheckpoint(
					EBattleRejectionReason::CheckpointPreparationFailed);
			}
			CaptureDelta.bApplyCleanupStage = true;
			if (bTerminalCapture)
			{
				CaptureDelta.Phase = EBattlePhase::Terminal;
				CaptureDelta.Outcome = EBattleOutcome::Victory;
				CaptureDelta.OutcomeCause = EBattleOutcomeCause::Capture;
				if (!TryStageBattleEndCleanup(CaptureDelta.CleanupStage))
				{
					return RejectPreparedCheckpoint(
						EBattleRejectionReason::CheckpointPreparationFailed);
				}
			}
		}

		if (CommitIdentity.ExpectedStateVersion == TNumericLimits<uint64>::Max()
			|| !TryPrepareCaptureBoundary(
				*State,
				CommitIdentity.ExpectedStateVersion + 1,
				CaptureDelta,
				ReplacementRequirements))
		{
			return RejectPreparedCheckpoint(
				EBattleRejectionReason::CheckpointPreparationFailed);
		}

		auto StageEvent = [&](const EBattleEventType Type,
			const EBattleEventCause Cause,
			const EBattleOutcomeCause OutcomeCause,
			const FBattleEventTarget* Target,
			const FBattleCaptureEventMetadata* Metadata)
		{
			return FBattleResolutionCommit::TryStageEvent(
				CommitPlan,
				MakeStagedCaptureEventSpec(
					*State,
					ResolutionId,
					ActionId,
					Source,
					Type,
					Cause,
					OutcomeCause,
					Target,
					Metadata));
		};

		if (!StageEvent(
				EBattleEventType::ItemUsed,
				EBattleEventCause::Item,
				EBattleOutcomeCause::None,
				&CaptureTarget,
				nullptr)
			|| !StageEvent(
				EBattleEventType::ItemConsumed,
				EBattleEventCause::Item,
				EBattleOutcomeCause::None,
				&CaptureTarget,
				nullptr)
			|| !StageEvent(
				EBattleEventType::CaptureAttempted,
				EBattleEventCause::Capture,
				EBattleOutcomeCause::None,
				&CaptureTarget,
				&CaptureMetadata))
		{
			return RejectPreparedCheckpoint(
				EBattleRejectionReason::CheckpointPreparationFailed);
		}

		if (CaptureResult.bSucceeded)
		{
			const FBattlePendingCaptureRecord& PendingCapture =
				CaptureDelta.PendingCapture.GetValue();
			CaptureMetadata.bHasPendingDestination = true;
			CaptureMetadata.PendingCaptureOrdinal = PendingCapture.CaptureOrdinal;
			CaptureMetadata.PendingDestination = PendingCapture.Destination;
			if (!StageEvent(
					EBattleEventType::Captured,
					EBattleEventCause::Capture,
					EBattleOutcomeCause::None,
					&CaptureTarget,
					&CaptureMetadata)
				|| !StageEvent(
					EBattleEventType::LeftActiveSlot,
					EBattleEventCause::Capture,
					EBattleOutcomeCause::None,
					&CaptureTarget,
					nullptr)
				|| !StageEvent(
					EBattleEventType::Removed,
					EBattleEventCause::Capture,
					EBattleOutcomeCause::None,
					&CaptureTarget,
					nullptr)
				|| !StageEvent(
					EBattleEventType::OpponentRemovalCheckpoint,
					EBattleEventCause::Rule,
					EBattleOutcomeCause::None,
					&CaptureTarget,
					nullptr))
			{
				return RejectPreparedCheckpoint(
					EBattleRejectionReason::CheckpointPreparationFailed);
			}
			CaptureDelta.OpponentRemovalCheckpointOrdinal =
				CommitPlan.Events.Last().GetEventOrdinal();
		}

		if (!StageEvent(
				EBattleEventType::ActionCompleted,
				EBattleEventCause::Action,
				EBattleOutcomeCause::None,
				nullptr,
				nullptr))
		{
			return RejectPreparedCheckpoint(
				EBattleRejectionReason::CheckpointPreparationFailed);
		}
		for (const FBattleReplacementRequirement& Requirement : ReplacementRequirements)
		{
			if (!StageEvent(
					EBattleEventType::ReplacementRequired,
					EBattleEventCause::Rule,
					EBattleOutcomeCause::None,
					&Requirement.Target,
					nullptr))
			{
				return RejectPreparedCheckpoint(
					EBattleRejectionReason::CheckpointPreparationFailed);
			}
		}
		if ((bTerminalCapture
				&& !StageEvent(
					EBattleEventType::BattleEnded,
					EBattleEventCause::Outcome,
					EBattleOutcomeCause::Capture,
					nullptr,
					nullptr))
			|| !FBattleResolutionCommit::TryFinishAcceptedPlan(CommitPlan))
		{
			return RejectPreparedCheckpoint(
				EBattleRejectionReason::CheckpointPreparationFailed);
		}

		if (!FBattleResolutionCommit::IsIdentityCurrent(*State, CommitIdentity))
		{
			return RejectPreparedCheckpoint(
				EBattleRejectionReason::StaleCheckpointIdentity);
		}
		if (RandomTransaction.IsValid())
		{
			EBattleRandomTransactionCommitError RandomError =
				EBattleRandomTransactionCommitError::None;
			if (!RandomTransaction->TryCommit(
					*State->Random,
					ResolutionId,
					ActionId,
					RandomError))
			{
				return RejectPreparedCheckpoint(
					EBattleRejectionReason::RandomTransactionCommitFailed);
			}
			RandomTransaction.Reset();
		}

		ApplyCaptureDelta(*State, CommitIdentity, CaptureDelta);
		const FBattleResolution Resolution = FBattleResolutionCommit::PublishPrepared(
			*State,
			CommitPlan);
		EBattleStateValidationError StateError = EBattleStateValidationError::None;
		const bool bStateValid = State->ValidateInvariants(StateError);
		check(bStateValid);
		return Resolution;
	}

	check(bBagCheckpointPrepared);
	ApplyBagItemDelta(*State, BagCommitIdentity, BagDelta);
	const FBattleResolution Resolution = FBattleResolutionCommit::PublishPrepared(
		*State,
		BagCommitPlan);
	EBattleStateValidationError StateError = EBattleStateValidationError::None;
	const bool bStateValid = State->ValidateInvariants(StateError);
	check(bStateValid);
	return Resolution;
}

FBattleResolution FBattleEngine::CommitCurrentMoveAfterPreMoveGates()
{
	check(State.IsValid());
	const FResolutionId ResolutionId = TakeResolutionId(*State);

	FActionId ActionId;
	FTrainerId TrainerId;
	FBattlerId ActorId;
	FActiveSlotId ActingSlotId;
	FBattleEventSource FallbackSource;
	TOptional<FBattleMoveDefinition> PreparedMove;
	bool bStruggle = false;
	bool bReleasingCharge = false;
	FPreMoveCheckpointIdentity CheckpointIdentity;
	{
		const FBattleLockedActionState* Action = State->LockedActions.IsValidIndex(
			State->CurrentLockedActionIndex)
			? &State->LockedActions[State->CurrentLockedActionIndex]
			: nullptr;
		FallbackSource = Action != nullptr
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

		const FBattleBattlerState* Battler = Action != nullptr
			? State->FindBattler(Action->Decision.GetActingBattlerId())
			: nullptr;
		const FBattleMoveDefinition* Move = nullptr;
		bStruggle = Action != nullptr
			&& Action->Decision.GetMoveId()
				== FBattleBuiltInMoveDefinitions::GetStruggleMoveId();
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

		ActionId = Action->ActionId;
		TrainerId = Action->Decision.GetDecisionOwnerTrainerId();
		ActorId = Action->Decision.GetActingBattlerId();
		ActingSlotId = Action->OrderKey.ActingSlotId;
		PreparedMove = *Move;
		bReleasingCharge = IsReleasingCharge(*State, *Battler, Move->Id);
		if (!TryCapturePreMoveCheckpointIdentity(
				*State,
				ResolutionId,
				*Action,
				CheckpointIdentity))
		{
			FBattleResolution Rejected;
			if (TryPublishPreMoveCheckpointRejection(
					*State,
					ResolutionId,
					ActionId,
					EBattleRejectionReason::CheckpointPreparationFailed,
					TrainerId,
					ActorId,
					FallbackSource,
					Rejected))
			{
				return Rejected;
			}
			return FBattleResolution();
		}
	}

	TUniquePtr<IBattleRandomTransaction> RandomTransaction;
	auto RejectCheckpoint =
		[&](const EBattleRejectionReason Reason) -> FBattleResolution
		{
			if (RandomTransaction.IsValid())
			{
				RandomTransaction->Rollback();
			}
			FBattleResolution Rejected;
			if (TryPublishPreMoveCheckpointRejection(
					*State,
					ResolutionId,
					ActionId,
					Reason,
					TrainerId,
					ActorId,
					FallbackSource,
					Rejected))
			{
				return Rejected;
			}
			return FBattleResolution();
		};
	auto EnsureRandomTransaction = [&]() -> bool
	{
		return RandomTransaction.IsValid()
			|| (State->Random.IsValid()
				&& State->Random->TryCreateTransaction(
					ResolutionId,
					ActionId,
					RandomTransaction)
				&& RandomTransaction.IsValid());
	};

	FBattleResolutionCommitPlan CommitPlan;
	if (!PreparedMove.IsSet()
		|| !FBattleResolutionCommit::TryBeginAcceptedPlan(
			CheckpointIdentity.CommitIdentity,
			CommitPlan))
	{
		return RejectCheckpoint(EBattleRejectionReason::CheckpointPreparationFailed);
	}

	FPreMoveCheckpointPreparation Preparation;
	if (!Preparation.Capture(*State, ActionId))
	{
		return RejectCheckpoint(EBattleRejectionReason::CheckpointPreparationFailed);
	}
	FReadOnlyFieldSideCheckpointView Projection(
		*State,
		Preparation.Common,
		State->Field,
		State->Sides);
	FBattleBattlerState* PreparedActor = Projection.FindMutableBattler(ActorId);
	if (PreparedActor == nullptr)
	{
		return RejectCheckpoint(EBattleRejectionReason::CheckpointPreparationFailed);
	}

	TArray<FBattleEvent> Events;
	FBattleFaintOutcomeResolution ConfusionFaintResolution;
	{
		FBattleLockedActionState& Action = Preparation.Action;
		FBattleBattlerState& Battler = *PreparedActor;
		const FBattleMoveDefinition& Move = PreparedMove.GetValue();
		FBattleMoveSlotState* MoveSlot =
			CheckpointIdentity.ExpectedMoveSlotNumber != 255
			? Battler.Moves.FindByPredicate(
				[&CheckpointIdentity](const FBattleMoveSlotState& Candidate)
				{
					return Candidate.SlotIndex
						== CheckpointIdentity.ExpectedMoveSlotNumber;
				})
			: nullptr;
		if (Action.ActionId != ActionId
			|| Battler.BattlerId != ActorId
			|| (!bStruggle && MoveSlot == nullptr))
		{
			return RejectCheckpoint(EBattleRejectionReason::CheckpointPreparationFailed);
		}

		bool bStatusDeniedAction = false;
		bool bStatusCured = false;
		FBattleMajorStatusActionResult StatusAction;
		if (Battler.MajorStatusId == FBattleMajorStatusRules::GetSleepId()
			|| Battler.MajorStatusId == FBattleMajorStatusRules::GetFreezeId()
			|| Battler.MajorStatusId == FBattleMajorStatusRules::GetParalysisId())
		{
			const FConditionId StatusBeforeGate = Battler.MajorStatusId;
			TArray<FBattleTriggerEffectRequest> TriggerRequests;
			TArray<FBattleTriggerLifecycleFact> TriggerFacts;
			const bool bSleep =
				StatusBeforeGate == FBattleMajorStatusRules::GetSleepId();
			if (!TryDispatchBattlerStatusPhase(
					Projection,
					Battler,
					EBattleTriggerPhase::BeforeAction,
					bSleep,
					TOptional<int32>(),
					TriggerRequests,
					TriggerFacts))
			{
				return RejectCheckpoint(
					EBattleRejectionReason::CheckpointPreparationFailed);
			}

			if (bSleep)
			{
				const bool bExpired = TriggerFacts.ContainsByPredicate(
					[](const FBattleTriggerLifecycleFact& Fact)
					{
						return Fact.Kind == EBattleTriggerLifecycleFactKind::Ended
							&& Fact.EndReason.IsSet()
							&& Fact.EndReason.GetValue()
								== EBattleTriggerEndReason::Expired;
					});
				if (bExpired)
				{
					bStatusCured = true;
					Battler.MajorStatusId = FConditionId();
				}
				else if (TriggerRequests.Num() == 1
					&& TriggerRequests[0].RemainingTurns.IsSet()
					&& TriggerRequests[0].RemainingTurns.GetValue() > 0)
				{
					bStatusDeniedAction = true;
				}
				else
				{
					return RejectCheckpoint(
						EBattleRejectionReason::CheckpointPreparationFailed);
				}
			}
			else
			{
				FBattleMajorStatusActionFacts Facts;
				Facts.StatusId = StatusBeforeGate;
				Facts.bMoveThawsUser = EnumHasAllFlags(
					Move.Flags,
					EBattleMoveFlags::ThawsUser);
				FBattleRandomContext RandomContext;
				RandomContext.BattleId = Projection.Setup.GetBattleId();
				RandomContext.TurnId = Projection.TurnId;
				RandomContext.ActionId = ActionId;
				RandomContext.ResolutionId = ResolutionId;
				RandomContext.RulePurpose = StatusBeforeGate.GetDefinitionId();

				bool bResolved = false;
				if (StatusBeforeGate == FBattleMajorStatusRules::GetFreezeId()
					&& Facts.bMoveThawsUser)
				{
					FNoDrawBattleRandom NoDrawRandom;
					bResolved = FBattleMajorStatusRules::TryResolveBeforeAction(
						Facts,
						RandomContext,
						NoDrawRandom,
						StatusAction)
						&& NoDrawRandom.GetTrace().IsEmpty();
				}
				else
				{
					if (!EnsureRandomTransaction())
					{
						return RejectCheckpoint(
							EBattleRejectionReason::CheckpointRandomStageFailed);
					}
					bResolved = FBattleMajorStatusRules::TryResolveBeforeAction(
						Facts,
						RandomContext,
						*RandomTransaction,
						StatusAction);
				}
				if (!bResolved || TriggerRequests.Num() != 1)
				{
					return RejectCheckpoint(
						RandomTransaction.IsValid()
							? EBattleRejectionReason::CheckpointRandomStageFailed
							: EBattleRejectionReason::CheckpointPreparationFailed);
				}

				bStatusDeniedAction = StatusAction.Outcome
					== EBattleMajorStatusActionOutcome::Denied;
				if (StatusAction.bCureStatus)
				{
					if (!TryCleanupMajorStatusTriggers(
							Projection,
							StatusBeforeGate,
							ActorId,
							EBattleTriggerCleanupReason::Removal))
					{
						return RejectCheckpoint(
							EBattleRejectionReason::CheckpointPreparationFailed);
					}
					bStatusCured = true;
					Battler.MajorStatusId = FConditionId();
				}
			}
		}

		if (StatusAction.bDrawConsumed)
		{
			Events.Add(MakeActionDetailEvent(
				Projection,
				ResolutionId,
				Action,
				EBattleEventType::RandomCheck,
				EBattleEventCause::Rule,
				static_cast<int64>(StatusAction.Draw.InclusiveMinimum),
				static_cast<int64>(StatusAction.Draw.Result),
				static_cast<int64>(StatusAction.Draw.InclusiveMaximum)));
		}
		if (bStatusCured)
		{
			Events.Add(MakeActionDetailEvent(
				Projection,
				ResolutionId,
				Action,
				EBattleEventType::StatusChanged,
				EBattleEventCause::Rule,
				static_cast<int64>(1),
				static_cast<int64>(0),
				static_cast<int64>(-1)));
		}

		if (bStatusDeniedAction)
		{
			if (bReleasingCharge
				&& !TryClearChargeState(
					Projection,
					ActorId,
					EBattleTriggerCleanupReason::Removal))
			{
				return RejectCheckpoint(
					EBattleRejectionReason::CheckpointPreparationFailed);
			}
			Action.bFinished = true;
			Events.Add(MakeActionDetailEvent(
				Projection,
				ResolutionId,
				Action,
				EBattleEventType::EffectPrevented,
				EBattleEventCause::Rule));
			Events.Add(MakeActionDetailEvent(
				Projection,
				ResolutionId,
				Action,
				EBattleEventType::ActionCanceled,
				EBattleEventCause::Rule));
			Events.Add(MakeActionDetailEvent(
				Projection,
				ResolutionId,
				Action,
				EBattleEventType::ActionCompleted,
				EBattleEventCause::Action));
			++Projection.CurrentLockedActionIndex;
			if (!TryAppendAtomicSwitchBoundaryEvents(
					Projection,
					ResolutionId,
					Action,
					Events))
			{
				return RejectCheckpoint(
					EBattleRejectionReason::CheckpointPreparationFailed);
			}
		}
		else
		{
			TArray<FBattleTriggerEffectRequest> VolatileRequests;
			TArray<FBattleTriggerLifecycleFact> VolatileFacts;
			if (!TryDispatchBattlerVolatilePhase(
					Projection,
					Battler,
					EBattleTriggerPhase::BeforeAction,
					true,
					VolatileRequests,
					VolatileFacts))
			{
				return RejectCheckpoint(
					EBattleRejectionReason::CheckpointPreparationFailed);
			}
			for (const FBattleConditionState& Condition : Battler.Volatiles)
			{
				const FConditionId VolatileId = Condition.ConditionId;
				const bool bPreMoveGateVolatile =
					VolatileId == FBattleVolatileRules::GetConfusionId()
					|| VolatileId == FBattleVolatileRules::GetFlinchId()
					|| VolatileId == FBattleVolatileRules::GetRechargeId()
					|| VolatileId == FBattleVolatileRules::GetTauntId()
					|| VolatileId == FBattleVolatileRules::GetEncoreId()
					|| VolatileId == FBattleVolatileRules::GetDisableId();
				if (!bPreMoveGateVolatile)
				{
					continue;
				}
				const bool bHasRequest = VolatileRequests.ContainsByPredicate(
					[VolatileId](const FBattleTriggerEffectRequest& Request)
					{
						return Request.SourceDefinition.Kind
								== EBattleTriggerSourceDefinitionKind::Condition
							&& Request.SourceDefinition.ConditionId == VolatileId;
					});
				const bool bExpired = VolatileFacts.ContainsByPredicate(
					[VolatileId](const FBattleTriggerLifecycleFact& Fact)
					{
						return Fact.Kind == EBattleTriggerLifecycleFactKind::Ended
							&& Fact.EndReason.IsSet()
							&& Fact.EndReason.GetValue()
								== EBattleTriggerEndReason::Expired
							&& Fact.SourceDefinition.Kind
								== EBattleTriggerSourceDefinitionKind::Condition
							&& Fact.SourceDefinition.ConditionId == VolatileId;
					});
				if (!bHasRequest && !bExpired)
				{
					return RejectCheckpoint(
						EBattleRejectionReason::CheckpointPreparationFailed);
				}
			}

			bool bVolatileDeniedAction = false;
			bool bConfusionSelfHit = false;
			bool bVolatileRemoved = false;
			auto RemoveVolatile = [&](const FConditionId& VolatileId) -> bool
			{
				if (!TryCleanupVolatileTriggers(
						Projection,
						VolatileId,
						ActorId,
						EBattleTriggerCleanupReason::Removal))
				{
					return false;
				}
				Battler.Volatiles.RemoveAll(
					[&VolatileId](const FBattleConditionState& Condition)
					{
						return Condition.ConditionId == VolatileId;
					});
				bVolatileRemoved = true;
				return true;
			};

			for (const FBattleTriggerLifecycleFact& Fact : VolatileFacts)
			{
				if (Fact.Kind == EBattleTriggerLifecycleFactKind::Ended
					&& Fact.EndReason.IsSet()
					&& Fact.EndReason.GetValue()
						== EBattleTriggerEndReason::Expired
					&& Fact.SourceDefinition.Kind
						== EBattleTriggerSourceDefinitionKind::Condition
					&& Fact.SourceDefinition.ConditionId
						== FBattleVolatileRules::GetConfusionId())
				{
					if (!RemoveVolatile(FBattleVolatileRules::GetConfusionId()))
					{
						return RejectCheckpoint(
							EBattleRejectionReason::CheckpointPreparationFailed);
					}
					break;
				}
			}

			for (const FBattleTriggerEffectRequest& Request : VolatileRequests)
			{
				if (bVolatileDeniedAction)
				{
					break;
				}
				if (Request.SourceDefinition.Kind
					!= EBattleTriggerSourceDefinitionKind::Condition)
				{
					return RejectCheckpoint(
						EBattleRejectionReason::CheckpointPreparationFailed);
				}
				const FConditionId VolatileId =
					Request.SourceDefinition.ConditionId;
				if (VolatileId == FBattleVolatileRules::GetConfusionId())
				{
					FBattleConditionState* Confusion =
						FindMutableVolatile(Battler, VolatileId);
					if (Confusion == nullptr
						|| !Confusion->RemainingTurns.IsSet())
					{
						return RejectCheckpoint(
							EBattleRejectionReason::CheckpointPreparationFailed);
					}
					FBattleRandomContext RandomContext;
					RandomContext.BattleId = Projection.Setup.GetBattleId();
					RandomContext.TurnId = Projection.TurnId;
					RandomContext.ActionId = ActionId;
					RandomContext.ResolutionId = ResolutionId;
					RandomContext.RulePurpose =
						FBattleVolatileRules::GetConfusionActionGatePurpose();
					FBattleVolatileActionResult Gate;
					bool bResolved = false;
					if (Confusion->RemainingTurns.GetValue() == 1)
					{
						FNoDrawBattleRandom NoDrawRandom;
						bResolved =
							FBattleVolatileRules::TryResolveConfusionBeforeAction(
								Confusion->RemainingTurns.GetValue(),
								RandomContext,
								NoDrawRandom,
								Gate)
							&& NoDrawRandom.GetTrace().IsEmpty();
					}
					else
					{
						if (!EnsureRandomTransaction())
						{
							return RejectCheckpoint(
								EBattleRejectionReason::CheckpointRandomStageFailed);
						}
						bResolved =
							FBattleVolatileRules::TryResolveConfusionBeforeAction(
								Confusion->RemainingTurns.GetValue(),
								RandomContext,
								*RandomTransaction,
								Gate);
					}
					if (!bResolved)
					{
						return RejectCheckpoint(
							RandomTransaction.IsValid()
								? EBattleRejectionReason::CheckpointRandomStageFailed
								: EBattleRejectionReason::CheckpointPreparationFailed);
					}
					if (!Request.RemainingTurns.IsSet()
						|| !Gate.RemainingTurns.IsSet()
						|| Request.RemainingTurns.GetValue()
							!= Gate.RemainingTurns.GetValue())
					{
						return RejectCheckpoint(
							EBattleRejectionReason::CheckpointPreparationFailed);
					}
					Confusion->RemainingTurns = Gate.RemainingTurns;
					if (Gate.bDrawConsumed)
					{
						Events.Add(MakeActionDetailEvent(
							Projection,
							ResolutionId,
							Action,
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
					if (!FBattleVolatileRules::TryResolveSimpleBeforeAction(
							VolatileId,
							Gate)
						|| !Gate.bRemoveVolatile
						|| !RemoveVolatile(VolatileId))
					{
						return RejectCheckpoint(
							EBattleRejectionReason::CheckpointPreparationFailed);
					}
					bVolatileDeniedAction = true;
				}
				else if (VolatileId == FBattleVolatileRules::GetTauntId()
					|| VolatileId == FBattleVolatileRules::GetEncoreId()
					|| VolatileId == FBattleVolatileRules::GetDisableId())
				{
					FBattleVolatileMoveGateResult Gate;
					if (!TryResolveVolatileMoveGate(
							Projection,
							Battler,
							Move,
							bStruggle,
							Gate))
					{
						return RejectCheckpoint(
							EBattleRejectionReason::CheckpointPreparationFailed);
					}
					if (Gate.bEndEncore
						&& HasVolatile(
							Battler,
							FBattleVolatileRules::GetEncoreId())
						&& !RemoveVolatile(
							FBattleVolatileRules::GetEncoreId()))
					{
						return RejectCheckpoint(
							EBattleRejectionReason::CheckpointPreparationFailed);
					}
					if (Gate.bEndDisable
						&& HasVolatile(
							Battler,
							FBattleVolatileRules::GetDisableId())
						&& !RemoveVolatile(
							FBattleVolatileRules::GetDisableId()))
					{
						return RejectCheckpoint(
							EBattleRejectionReason::CheckpointPreparationFailed);
					}
					bVolatileDeniedAction =
						Gate.Outcome != EBattleVolatileMoveGateOutcome::Allowed;
				}
			}

			if (bVolatileRemoved)
			{
				Events.Add(MakeActionDetailEvent(
					Projection,
					ResolutionId,
					Action,
					EBattleEventType::StatusChanged,
					EBattleEventCause::Rule,
					static_cast<int64>(1),
					static_cast<int64>(0),
					static_cast<int64>(-1)));
			}

			if (bConfusionSelfHit)
			{
				FBattleEventTarget SelfTarget;
				SelfTarget.TrainerId = Battler.TrainerId;
				SelfTarget.BattlerId = ActorId;
				SelfTarget.ActiveSlotId = ActingSlotId;
				if (FBattleAbilityRules::ShouldMagicGuardPreventDamage(
						Battler.AbilityId,
						EBattleHPChangeSourceKind::Volatile,
						Battler.bAbilitySuppressed))
				{
					if (!TryAppendAbilityActivationForPhase(
							Projection,
							ActorId,
							EBattleTriggerPhase::BeforeAction,
							EBattleAbilityItemActivationOutcome::Applied,
							ResolutionId,
							ActionId,
							EBattleActionKind::Fight,
							Events,
							&SelfTarget))
					{
						return RejectCheckpoint(
							EBattleRejectionReason::CheckpointPreparationFailed);
					}
				}
				else
				{
					if (!RandomTransaction.IsValid())
					{
						return RejectCheckpoint(
							EBattleRejectionReason::CheckpointRandomStageFailed);
					}
					FBattleFinalDamageInput DamageInput;
					DamageInput.AttackerLevel = Battler.Level;
					DamageInput.AttackerStats = Battler.PermanentStats;
					DamageInput.DefenderStats = Battler.PermanentStats;
					DamageInput.AttackerStages = Battler.Stages;
					DamageInput.DefenderStages = Battler.Stages;
					DamageInput.MoveCategory = EBattleMoveCategory::Physical;
					DamageInput.MovePower =
						FBattleVolatileRules::GetConfusionSelfHitBasePower();
					DamageInput.bAttackerBurned =
						FBattleMajorStatusRules::ShouldApplyBurnPhysicalPenalty(
							Battler.MajorStatusId,
							EBattleMoveCategory::Physical,
							false);
					DamageInput.bBypassTypeImmunity = true;
					DamageInput.WeatherModifierQ12 =
						FBattleFinalDamageCalculator::Q12Neutral;
					DamageInput.StabModifierQ12 =
						FBattleFinalDamageCalculator::Q12Neutral;
					DamageInput.TypeEffectiveness = {1, 1};
					DamageInput.RandomContext.BattleId =
						Projection.Setup.GetBattleId();
					DamageInput.RandomContext.TurnId = Projection.TurnId;
					DamageInput.RandomContext.ActionId = ActionId;
					DamageInput.RandomContext.ResolutionId = ResolutionId;
					DamageInput.RandomContext.RulePurpose =
						FBattleVolatileRules::GetConfusionSelfHitDamagePurpose();
					FBattleFinalDamageResult DamageResult;
					EBattleDamageCalculationError DamageError =
						EBattleDamageCalculationError::None;
					if (!FBattleFinalDamageCalculator::TryCalculateFinalDamage(
							DamageInput,
							*RandomTransaction,
							DamageResult,
							DamageError)
						|| DamageResult.Outcome != EBattleDamageOutcome::Damage)
					{
						return RejectCheckpoint(
							EBattleRejectionReason::CheckpointRandomStageFailed);
					}
					if (DamageResult.bRandomDrawConsumed)
					{
						Events.Add(MakeActionDetailEvent(
							Projection,
							ResolutionId,
							Action,
							EBattleEventType::RandomCheck,
							EBattleEventCause::Rule,
							static_cast<int64>(
								DamageResult.RandomDraw.InclusiveMinimum),
							static_cast<int64>(DamageResult.RandomDraw.Result),
							static_cast<int64>(
								DamageResult.RandomDraw.InclusiveMaximum)));
					}

					const int32 PreviousHP = Battler.CurrentHP;
					const int32 AppliedDamage =
						FMath::Min(PreviousHP, DamageResult.Damage);
					Battler.CurrentHP -= AppliedDamage;
					if (Battler.CurrentHP == 0)
					{
						Battler.bFainted = true;
						Battler.bFaintTransitionPending = true;
					}
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
						Record.Targets.Add(SelfTarget);
						Record.NumericBefore = PreviousHP;
						Record.NumericAfter = Battler.CurrentHP;
						Record.NumericDelta = -AppliedDamage;
						Events.Add(MakeBattleEffectEvent(
							Projection,
							ResolutionId,
							Action,
							Record,
							TOptional<uint64>()));
					}
					if (!TryResolveImmediateHeldItem(
							Projection,
							ActorId,
							ResolutionId,
							ActionId,
							EBattleActionKind::Fight,
							Events))
					{
						return RejectCheckpoint(
							EBattleRejectionReason::CheckpointPreparationFailed);
					}

					if (Battler.bFaintTransitionPending)
					{
						const FConditionId PendingStatus = Battler.MajorStatusId;
						TArray<FConditionId> PendingVolatiles;
						for (const FBattleConditionState& Condition :
							Battler.Volatiles)
						{
							if (FBattleVolatileRules::IsCanonical(
									Condition.ConditionId))
							{
								PendingVolatiles.Add(Condition.ConditionId);
							}
						}
						Battler.LastMoveId = FMoveId();
						if (!TryCleanupSourceDependentVolatiles(
								Projection,
								ActorId,
								EBattleTriggerCleanupReason::Removal))
						{
							return RejectCheckpoint(
								EBattleRejectionReason::CheckpointPreparationFailed);
						}

						FBattleFaintOutcomePlan FaintPlan;
						if (!FBattleFaintOutcomeResolver::TryResolveAction(
								EffectResult,
								EBattleTargetClass::Self,
								ResolutionId,
								Projection.Battlers,
								Projection.ActivePositions,
								Projection.CompiledEncounterPolicies,
								FaintPlan)
							|| !FBattleFaintOutcomeResolver::TryApplyActionPlan(
								Projection.Battlers,
								Projection.ActivePositions,
								Projection.Phase,
								Projection.Outcome,
								Projection.OutcomeCause,
								Projection.PendingDecision,
								Projection.PendingDecisionRequests,
								FaintPlan))
						{
							return RejectCheckpoint(
								EBattleRejectionReason::CheckpointPreparationFailed);
						}
						ConfusionFaintResolution = FaintPlan.Resolution;

						if (!TryCleanupAbilityTriggers(
								Projection,
								Battler.AbilityId,
								ActorId,
								EBattleTriggerCleanupReason::Faint)
							|| !TryCleanupItemTriggers(
								Projection,
								Battler.HeldItem.CurrentItemId,
								ActorId,
								EBattleTriggerCleanupReason::Faint))
						{
							return RejectCheckpoint(
								EBattleRejectionReason::CheckpointPreparationFailed);
						}
						Battler.bAbilitySuppressed = false;
						Battler.EnteredActiveOnTurnId = FTurnId();
						if (FBattleMajorStatusRules::IsCanonical(PendingStatus)
							&& !TryCleanupMajorStatusTriggers(
								Projection,
								PendingStatus,
								ActorId,
								EBattleTriggerCleanupReason::Faint))
						{
							return RejectCheckpoint(
								EBattleRejectionReason::CheckpointPreparationFailed);
						}
						for (const FConditionId& VolatileId : PendingVolatiles)
						{
							if (!TryCleanupVolatileTriggers(
									Projection,
									VolatileId,
									ActorId,
									EBattleTriggerCleanupReason::Faint))
							{
								return RejectCheckpoint(
									EBattleRejectionReason::CheckpointPreparationFailed);
							}
						}
						for (const FBattleFaintTransitionRecord& Faint :
							ConfusionFaintResolution.Faints)
						{
							Events.Add(MakeTargetedActionEvent(
								Projection,
								ResolutionId,
								Action,
								EBattleEventType::Fainted,
								EBattleEventCause::Rule,
								Faint.Target));
						}
						for (const FBattleFaintTransitionRecord& Removal :
							ConfusionFaintResolution.Removals)
						{
							Events.Add(MakeTargetedActionEvent(
								Projection,
								ResolutionId,
								Action,
								EBattleEventType::LeftActiveSlot,
								EBattleEventCause::Rule,
								Removal.Target));
							Events.Add(MakeTargetedActionEvent(
								Projection,
								ResolutionId,
								Action,
								EBattleEventType::Removed,
								EBattleEventCause::Rule,
								Removal.Target));
						}
						if (ConfusionFaintResolution.bBattleEnded
							&& !TryCleanupBattleEndTriggers(Projection))
						{
							return RejectCheckpoint(
								EBattleRejectionReason::CheckpointPreparationFailed);
						}
					}
				}
			}

			if (bVolatileDeniedAction)
			{
				if (bReleasingCharge
					&& !TryClearChargeState(
						Projection,
						ActorId,
						EBattleTriggerCleanupReason::Removal))
				{
					return RejectCheckpoint(
						EBattleRejectionReason::CheckpointPreparationFailed);
				}
				Action.bFinished = true;
				Events.Add(MakeActionDetailEvent(
					Projection,
					ResolutionId,
					Action,
					EBattleEventType::EffectPrevented,
					EBattleEventCause::Rule));
				Events.Add(MakeActionDetailEvent(
					Projection,
					ResolutionId,
					Action,
					EBattleEventType::ActionCanceled,
					EBattleEventCause::Rule));
				Events.Add(MakeActionDetailEvent(
					Projection,
					ResolutionId,
					Action,
					EBattleEventType::ActionCompleted,
					EBattleEventCause::Action));
				++Projection.CurrentLockedActionIndex;
				if (ConfusionFaintResolution.bBattleEnded)
				{
					AppendPartnerTeamVictoryRecoveryEvent(
						Projection,
						ResolutionId,
						ActionId,
						EBattleActionKind::Fight,
						SourceFromLockedAction(Projection, Action),
						ConfusionFaintResolution,
						Events);
					Events.Add(MakeBattleEndedEvent(
						Projection,
						ResolutionId,
						Action,
						ConfusionFaintResolution.OutcomeCause));
				}
				else if (!TryAppendAtomicSwitchBoundaryEvents(
						Projection,
						ResolutionId,
						Action,
						Events))
				{
					return RejectCheckpoint(
						EBattleRejectionReason::CheckpointPreparationFailed);
				}
			}
			else
			{
				FBattleChoiceBandMoveResult ChoiceCommitResult;
				ChoiceCommitResult.bValid = true;
				ChoiceCommitResult.bMoveAllowed = true;
				ChoiceCommitResult.Outcome =
					EBattleAbilityItemActivationOutcome::Ineligible;
				const bool bChoiceBandActive = IsHeldItemActive(Battler)
					&& Battler.HeldItem.CurrentItemId
						== FBattleItemRules::GetChoiceBandId();
				if (bChoiceBandActive)
				{
					FBattleChoiceBandMoveFacts ChoiceFacts;
					ChoiceFacts.ItemId = Battler.HeldItem.CurrentItemId;
					ChoiceFacts.SelectedMoveId = Move.Id;
					ChoiceFacts.LockedMoveId =
						Battler.HeldItem.ChoiceLockedMoveId;
					ChoiceFacts.bSelectedMoveIsStruggle = bStruggle;
					ChoiceFacts.bSuppressed = Battler.HeldItem.bSuppressed;
					if (!FBattleItemRules::TryEvaluateChoiceBandMove(
							ChoiceFacts,
							ChoiceCommitResult))
					{
						return RejectCheckpoint(
							EBattleRejectionReason::CheckpointPreparationFailed);
					}
				}

				const bool bChoiceBandDenied = !ChoiceCommitResult.bMoveAllowed;
				const bool bNoPP = !bStruggle
					&& !bReleasingCharge
					&& MoveSlot->CurrentPP <= 0;

				if (bChoiceBandDenied || bNoPP)
				{
					if (bReleasingCharge
						&& !TryClearChargeState(
							Projection,
							ActorId,
							EBattleTriggerCleanupReason::Removal))
					{
						return RejectCheckpoint(
							EBattleRejectionReason::CheckpointPreparationFailed);
					}
					Action.bFinished = true;
					if (bChoiceBandDenied)
					{
						Events.Add(MakeActionDetailEvent(
							Projection,
							ResolutionId,
							Action,
							EBattleEventType::EffectPrevented,
							EBattleEventCause::Rule));
					}
					Events.Add(MakeActionDetailEvent(
						Projection,
						ResolutionId,
						Action,
						EBattleEventType::ActionCanceled,
						bChoiceBandDenied
							? EBattleEventCause::Rule
							: EBattleEventCause::Action));
					Events.Add(MakeActionDetailEvent(
						Projection,
						ResolutionId,
						Action,
						EBattleEventType::ActionCompleted,
						EBattleEventCause::Action));
					++Projection.CurrentLockedActionIndex;
					if (!TryAppendAtomicSwitchBoundaryEvents(
							Projection,
							ResolutionId,
							Action,
							Events))
					{
						return RejectCheckpoint(
							EBattleRejectionReason::CheckpointPreparationFailed);
					}
				}
				else
				{
					if (ChoiceCommitResult.bShouldEstablishLock)
					{
						Battler.HeldItem.ChoiceLockedMoveId =
							ChoiceCommitResult.LockMoveId;
					}
					if (MoveSlot != nullptr && !bReleasingCharge)
					{
						const int32 PreviousPP = MoveSlot->CurrentPP;
						--MoveSlot->CurrentPP;
						Events.Add(MakeActionDetailEvent(
							Projection,
							ResolutionId,
							Action,
							EBattleEventType::PPConsumed,
							EBattleEventCause::Move,
							static_cast<int64>(PreviousPP),
							static_cast<int64>(MoveSlot->CurrentPP),
							static_cast<int64>(-1)));
					}
					Events.Add(MakeActionDetailEvent(
						Projection,
						ResolutionId,
						Action,
						EBattleEventType::MoveUsed,
						EBattleEventCause::Move));
					Battler.LastMoveId = Move.Id;
					Action.bMoveCommitted = true;
				}
			}
		}
	}

	for (const FBattleEvent& Event : Events)
	{
		if (!FBattleResolutionCommit::TryStageEvent(
				CommitPlan,
				MakeAtomicSwitchStagedEventSpec(Event)))
		{
			return RejectCheckpoint(
				EBattleRejectionReason::CheckpointPreparationFailed);
		}
	}
	if (!FBattleResolutionCommit::TryFinishAcceptedPlan(CommitPlan))
	{
		return RejectCheckpoint(EBattleRejectionReason::CheckpointPreparationFailed);
	}

	FPreMoveCheckpointDelta Delta;
	if (!TryCapturePreMoveCheckpointDelta(
			Preparation,
			CheckpointIdentity,
			Delta))
	{
		return RejectCheckpoint(EBattleRejectionReason::CheckpointPreparationFailed);
	}
	if (!IsPreMoveCheckpointIdentityCurrent(*State, CheckpointIdentity))
	{
		return RejectCheckpoint(EBattleRejectionReason::StaleCheckpointIdentity);
	}

	if (RandomTransaction.IsValid())
	{
		EBattleRandomTransactionCommitError CommitError =
			EBattleRandomTransactionCommitError::None;
		if (!RandomTransaction->TryCommit(
				*State->Random,
				ResolutionId,
				ActionId,
				CommitError))
		{
			return RejectCheckpoint(
				EBattleRejectionReason::RandomTransactionCommitFailed);
		}
	}

	ApplyPreMoveCheckpointDelta(*State, CheckpointIdentity, Delta);
	return FBattleResolutionCommit::PublishPrepared(*State, CommitPlan);
}
FBattleResolution FBattleEngine::ResolveCurrentMoveTargets()
{
	check(State.IsValid());
	const FResolutionId ResolutionId = TakeResolutionId(*State);

	FActionId ActionId;
	FTrainerId TrainerId;
	FBattlerId ActorId;
	FBattleEventSource FallbackSource;
	FTargetResolutionCheckpointIdentity CheckpointIdentity;
	{
		const FBattleLockedActionState* Action = State->LockedActions.IsValidIndex(
			State->CurrentLockedActionIndex)
			? &State->LockedActions[State->CurrentLockedActionIndex]
			: nullptr;
		FallbackSource = Action != nullptr
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
			|| Action->EffectExecutionState
				!= EBattleLockedEffectExecutionState::Pending
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
				|| !UserPosition->bAvailable
				|| UserPosition->TrainerId
					!= Action->Decision.GetDecisionOwnerTrainerId()
				|| UserPosition->BattlerId != User->BattlerId
				|| User->TrainerId != Action->Decision.GetDecisionOwnerTrainerId()
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

		ActionId = Action->ActionId;
		TrainerId = Action->Decision.GetDecisionOwnerTrainerId();
		ActorId = Action->Decision.GetActingBattlerId();
		if (!TryCaptureTargetResolutionCheckpointIdentity(
				*State,
				ResolutionId,
				*Action,
				CheckpointIdentity))
		{
			FBattleResolution Rejected;
			if (TryPublishTargetResolutionCheckpointRejection(
					*State,
					ResolutionId,
					ActionId,
					EBattleRejectionReason::CheckpointPreparationFailed,
					TrainerId,
					ActorId,
					FallbackSource,
					Rejected))
			{
				return Rejected;
			}
			return FBattleResolution();
		}
	}

	TUniquePtr<IBattleRandomTransaction> RandomTransaction;
	auto RejectCheckpoint =
		[&](const EBattleRejectionReason Reason) -> FBattleResolution
		{
			if (RandomTransaction.IsValid())
			{
				RandomTransaction->Rollback();
			}
			FBattleResolution Rejected;
			if (TryPublishTargetResolutionCheckpointRejection(
					*State,
					ResolutionId,
					ActionId,
					Reason,
					TrainerId,
					ActorId,
					FallbackSource,
					Rejected))
			{
				return Rejected;
			}
			return FBattleResolution();
		};

	FBattleResolutionCommitPlan CommitPlan;
	if (!FBattleResolutionCommit::TryBeginAcceptedPlan(
			CheckpointIdentity.CommitIdentity,
			CommitPlan))
	{
		return RejectCheckpoint(EBattleRejectionReason::CheckpointPreparationFailed);
	}

	FTargetResolutionCheckpointPreparation Preparation;
	if (!Preparation.Capture(*State, ActionId, ActorId))
	{
		return RejectCheckpoint(EBattleRejectionReason::CheckpointPreparationFailed);
	}
	FTargetResolutionCheckpointView Projection(*State, Preparation);
	FBattleLockedActionState& ProjectedAction = Preparation.Action;

	FNoDrawBattleRandom NoDrawRandom;
	IBattleRandom* TargetRandom = &NoDrawRandom;
	if (CheckpointIdentity.ExpectedAction.TargetClass
		== EBattleTargetClass::RandomLegalOpponent)
	{
		if (!State->Random.IsValid()
			|| !State->Random->TryCreateTransaction(
				ResolutionId,
				ActionId,
				RandomTransaction)
			|| !RandomTransaction.IsValid())
		{
			return RejectCheckpoint(
				EBattleRejectionReason::CheckpointRandomStageFailed);
		}
		TargetRandom = RandomTransaction.Get();
	}

	FBattleTargetResolutionResult TargetResolution;
	EBattleTargetingError TargetError = EBattleTargetingError::None;
	if (!FBattleTargetResolver::TryResolve(
			CheckpointIdentity.PreparedTargetSpec,
			*TargetRandom,
			TargetResolution,
			TargetError))
	{
		return RejectCheckpoint(
			TargetError == EBattleTargetingError::RandomFailure
				? EBattleRejectionReason::CheckpointRandomStageFailed
				: EBattleRejectionReason::CheckpointPreparationFailed);
	}
	if (TargetResolution.Outcome
			== EBattleTargetResolutionOutcome::CapturedTargetCanceled
		|| TargetResolution.Outcome == EBattleTargetResolutionOutcome::Invalid)
	{
		return RejectCheckpoint(EBattleRejectionReason::InvalidDecision);
	}

	ProjectedAction.TargetResolution = TargetResolution;
	FBattleEventSpec EventSpec;
	if (!TryMakeTargetResolutionEventSpec(
			Projection,
			ResolutionId,
			ProjectedAction,
			TargetResolution,
			EventSpec)
		|| !FBattleResolutionCommit::TryStageEvent(
			CommitPlan,
			MoveTemp(EventSpec)))
	{
		return RejectCheckpoint(EBattleRejectionReason::CheckpointPreparationFailed);
	}

	if (TargetResolution.Outcome == EBattleTargetResolutionOutcome::NoLegalTarget)
	{
		if (CheckpointIdentity.bExpectedReleasingCharge
			&& !TryClearTargetResolutionChargeState(
				Preparation,
				EBattleTriggerCleanupReason::Removal))
		{
			return RejectCheckpoint(
				EBattleRejectionReason::CheckpointPreparationFailed);
		}
		ProjectedAction.bFinished = true;
		if (!TryMakeTargetResolutionActionEventSpec(
				Projection,
				ResolutionId,
				ProjectedAction,
				EBattleEventType::ActionCanceled,
				EBattleEventCause::Targeting,
				EventSpec)
			|| !FBattleResolutionCommit::TryStageEvent(
				CommitPlan,
				MoveTemp(EventSpec))
			|| !TryMakeTargetResolutionActionEventSpec(
				Projection,
				ResolutionId,
				ProjectedAction,
				EBattleEventType::ActionCompleted,
				EBattleEventCause::Action,
				EventSpec)
			|| !FBattleResolutionCommit::TryStageEvent(
				CommitPlan,
				MoveTemp(EventSpec)))
		{
			return RejectCheckpoint(
				EBattleRejectionReason::CheckpointPreparationFailed);
		}

		++Projection.CurrentLockedActionIndex;
		TArray<FBattleEvent> BoundaryEvents;
		if (!TryAppendAtomicSwitchBoundaryEvents(
				Projection,
				ResolutionId,
				ProjectedAction,
				BoundaryEvents))
		{
			return RejectCheckpoint(
				EBattleRejectionReason::CheckpointPreparationFailed);
		}
		for (const FBattleEvent& BoundaryEvent : BoundaryEvents)
		{
			if (!FBattleResolutionCommit::TryStageEvent(
					CommitPlan,
					MakeAtomicSwitchStagedEventSpec(BoundaryEvent)))
			{
				return RejectCheckpoint(
					EBattleRejectionReason::CheckpointPreparationFailed);
			}
		}
	}

	if (!FBattleResolutionCommit::TryFinishAcceptedPlan(CommitPlan))
	{
		return RejectCheckpoint(EBattleRejectionReason::CheckpointPreparationFailed);
	}

	FTargetResolutionCheckpointDelta Delta;
	if (!TryCaptureTargetResolutionCheckpointDelta(
			Preparation,
			CheckpointIdentity,
			Delta))
	{
		return RejectCheckpoint(EBattleRejectionReason::CheckpointPreparationFailed);
	}
	if (!IsTargetResolutionCheckpointIdentityCurrent(*State, CheckpointIdentity))
	{
		return RejectCheckpoint(EBattleRejectionReason::StaleCheckpointIdentity);
	}

	if (RandomTransaction.IsValid())
	{
		EBattleRandomTransactionCommitError CommitError =
			EBattleRandomTransactionCommitError::None;
		if (!RandomTransaction->TryCommit(
				*State->Random,
				ResolutionId,
				ActionId,
				CommitError))
		{
			return RejectCheckpoint(
				EBattleRejectionReason::RandomTransactionCommitFailed);
		}
	}

	ApplyTargetResolutionCheckpointDelta(*State, CheckpointIdentity, Delta);
	return FBattleResolutionCommit::PublishPrepared(*State, CommitPlan);
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

	{
		FMoveEffectsCheckpointIdentity CheckpointIdentity;
		if (!TryCaptureMoveEffectsCheckpointIdentity(
				*State,
				ResolutionId,
				*Action,
				CheckpointIdentity))
		{
			FBattleResolution Rejected;
			TryPublishMoveEffectsCheckpointRejection(
				*State,
				ResolutionId,
				Action->ActionId,
				EBattleRejectionReason::CheckpointPreparationFailed,
				Action->Decision.GetDecisionOwnerTrainerId(),
				Action->Decision.GetActingBattlerId(),
				FallbackSource,
				Rejected);
			return Rejected;
		}

		const FActionId ActionId = CheckpointIdentity.CommitIdentity.OwningActionId;
		const FTrainerId OwnerId = CheckpointIdentity.ExpectedOwnerId;
		const FBattlerId ActorId = CheckpointIdentity.ExpectedActorId;
		const FBattleEventSource CheckpointSource = FallbackSource;
		TUniquePtr<IBattleRandomTransaction> RandomTransaction;
		auto RejectCheckpoint =
			[&](const EBattleRejectionReason Reason)
			{
				if (RandomTransaction.IsValid())
				{
					RandomTransaction->Rollback();
				}
				FBattleResolution Rejected;
				TryPublishMoveEffectsCheckpointRejection(
					*State,
					ResolutionId,
					ActionId,
					Reason,
					OwnerId,
					ActorId,
					CheckpointSource,
					Rejected);
				return Rejected;
			};

		if (!State->Random.IsValid()
			|| !State->Random->TryCreateTransaction(
				ResolutionId,
				ActionId,
				RandomTransaction)
			|| !RandomTransaction.IsValid())
		{
			return RejectCheckpoint(EBattleRejectionReason::CheckpointRandomStageFailed);
		}

		FBattleResolutionCommitPlan CommitPlan;
		if (!FBattleResolutionCommit::TryBeginAcceptedPlan(
				CheckpointIdentity.CommitIdentity,
				CommitPlan))
		{
			return RejectCheckpoint(EBattleRejectionReason::CheckpointPreparationFailed);
		}

		const FBattleBattlerState* ExpectedActor =
			CheckpointIdentity.ExpectedBattlers.FindByPredicate(
				[ActorId](const FBattleBattlerState& Candidate)
				{
					return Candidate.BattlerId == ActorId;
				});
		if (ExpectedActor == nullptr)
		{
			return RejectCheckpoint(EBattleRejectionReason::CheckpointPreparationFailed);
		}

		FMoveId StoredChargeMoveId;
		const bool bWasChargedRelease = HasVolatile(
				*ExpectedActor,
				FBattleVolatileRules::GetChargingId())
			&& TryGetVolatilePayloadMoveId(
				*State,
				ExpectedActor->BattlerId,
				FBattleVolatileRules::GetChargingId(),
				StoredChargeMoveId)
			&& StoredChargeMoveId == CheckpointIdentity.ExpectedMove.Id;

		FBattleEffectExecutionRequest Request;
		Request.BattleId = CheckpointIdentity.ExpectedBattleId;
		Request.TurnId = CheckpointIdentity.ExpectedTurnId;
		Request.ActionId = ActionId;
		Request.ResolutionId = ResolutionId;
		Request.UserBattlerId = ActorId;
		Request.UserSlotId = CheckpointIdentity.ExpectedActingSlotId;
		Request.Move = &CheckpointIdentity.ExpectedMove;
		Request.Targets = CheckpointIdentity.ExpectedAction.TargetResolution.GetValue().Targets;

		FBattleEffectExecutionPlan EffectPlan;
		EBattleEffectExecutorError EffectError = EBattleEffectExecutorError::None;
		if (!FBattleEffectExecutor::TryPrepareAgainstState(
				Request,
				*State,
				*RandomTransaction,
				EffectPlan,
				EffectError))
		{
			return RejectCheckpoint(
				EffectError == EBattleEffectExecutorError::RandomFailure
					? EBattleRejectionReason::CheckpointRandomStageFailed
					: EBattleRejectionReason::CheckpointPreparationFailed);
		}
		FBattleEffectExecutionResult EffectResult = MoveTemp(EffectPlan.Result);
		FMoveEffectsCheckpointPreparation Preparation;
		if (!Preparation.ImportPreparedEffects(
				*State,
				ActionId,
				MoveTemp(EffectPlan)))
		{
			return RejectCheckpoint(EBattleRejectionReason::CheckpointPreparationFailed);
		}
		FMutableFieldSideCheckpointView Projection(
			*State,
			Preparation.Common,
			Preparation.Field,
			Preparation.Sides);
		FBattleLockedActionState& ProjectedAction = Preparation.Action;
		ProjectedAction.EffectExecutionState =
			EBattleLockedEffectExecutionState::Executing;
		if (bWasChargedRelease
			&& !TryClearChargeState(
				Projection,
				ActorId,
				EBattleTriggerCleanupReason::Removal))
		{
			return RejectCheckpoint(EBattleRejectionReason::CheckpointPreparationFailed);
		}

		TMap<FBattlerId, FConditionId> PendingFaintStatuses;
		TMap<FBattlerId, TArray<FConditionId>> PendingFaintVolatiles;
		for (const FBattleBattlerState& Candidate : Projection.Battlers)
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
		for (const TPair<FBattlerId, TArray<FConditionId>>& Pending :
			PendingFaintVolatiles)
		{
			FBattleBattlerState* PendingBattler = Projection.FindMutableBattler(Pending.Key);
			if (PendingBattler == nullptr)
			{
				return RejectCheckpoint(EBattleRejectionReason::CheckpointPreparationFailed);
			}
			PendingBattler->LastMoveId = FMoveId();
			if (!TryCleanupSourceDependentVolatiles(
					Projection,
					Pending.Key,
					EBattleTriggerCleanupReason::Removal))
			{
				return RejectCheckpoint(EBattleRejectionReason::CheckpointPreparationFailed);
			}
		}

		FBattleFaintOutcomePlan FaintPlan;
		if (!FBattleFaintOutcomeResolver::TryResolveAction(
				EffectResult,
				ProjectedAction.TargetClass,
				ResolutionId,
				Projection.Battlers,
				Projection.ActivePositions,
				Projection.CompiledEncounterPolicies,
				FaintPlan)
			|| !FBattleFaintOutcomeResolver::TryApplyActionPlan(
				Projection.Battlers,
				Projection.ActivePositions,
				Projection.Phase,
				Projection.Outcome,
				Projection.OutcomeCause,
				Projection.PendingDecision,
				Projection.PendingDecisionRequests,
				FaintPlan))
		{
			return RejectCheckpoint(EBattleRejectionReason::CheckpointPreparationFailed);
		}
		const FBattleFaintOutcomeResolution& FaintResolution = FaintPlan.Resolution;

		for (const FBattleFaintTransitionRecord& Removal : FaintResolution.Removals)
		{
			FBattleBattlerState* RemovedBattler = Projection.FindMutableBattler(
				Removal.Target.BattlerId);
			if (RemovedBattler == nullptr
				|| !TryCleanupAbilityTriggers(
					Projection,
					RemovedBattler->AbilityId,
					RemovedBattler->BattlerId,
					EBattleTriggerCleanupReason::Faint)
				|| !TryCleanupItemTriggers(
					Projection,
					RemovedBattler->HeldItem.CurrentItemId,
					RemovedBattler->BattlerId,
					EBattleTriggerCleanupReason::Faint))
			{
				return RejectCheckpoint(EBattleRejectionReason::CheckpointPreparationFailed);
			}
			RemovedBattler->bAbilitySuppressed = false;
			RemovedBattler->EnteredActiveOnTurnId = FTurnId();

			const FConditionId* StatusId = PendingFaintStatuses.Find(
				Removal.Target.BattlerId);
			if (StatusId != nullptr
				&& !TryCleanupMajorStatusTriggers(
					Projection,
					*StatusId,
					Removal.Target.BattlerId,
					EBattleTriggerCleanupReason::Faint))
			{
				return RejectCheckpoint(EBattleRejectionReason::CheckpointPreparationFailed);
			}
			if (const TArray<FConditionId>* VolatileIds = PendingFaintVolatiles.Find(
				Removal.Target.BattlerId))
			{
				for (const FConditionId& VolatileId : *VolatileIds)
				{
					if (!TryCleanupVolatileTriggers(
							Projection,
							VolatileId,
							Removal.Target.BattlerId,
							EBattleTriggerCleanupReason::Faint))
					{
						return RejectCheckpoint(
							EBattleRejectionReason::CheckpointPreparationFailed);
					}
				}
			}
		}
		if (FaintResolution.bBattleEnded && !TryCleanupBattleEndTriggers(Projection))
		{
			return RejectCheckpoint(EBattleRejectionReason::CheckpointPreparationFailed);
		}

		if (CheckpointIdentity.CommitIdentity.ExpectedStateVersion
			== TNumericLimits<uint64>::Max())
		{
			return RejectCheckpoint(EBattleRejectionReason::CheckpointPreparationFailed);
		}
		const uint64 AfterStateVersion =
			CheckpointIdentity.CommitIdentity.ExpectedStateVersion + 1;
		TOptional<FBattleDecisionRequest> PivotRequest;
		if (!FaintResolution.bBattleEnded)
		{
			for (FBattleSwitchEffectIntent& Intent : EffectResult.SwitchIntents)
			{
				if (Intent.Kind != EBattleSwitchKind::Pivot)
				{
					continue;
				}
				if (PivotRequest.IsSet())
				{
					Intent.BlockReason = EBattleSwitchBlockReason::NoLegalReserve;
					continue;
				}
				const bool bActingBattlerRemoved =
					FaintResolution.Removals.ContainsByPredicate(
						[ActorId](const FBattleFaintTransitionRecord& Removal)
						{
							return Removal.Target.BattlerId == ActorId;
						});
				if (bActingBattlerRemoved)
				{
					Intent.BlockReason =
						EBattleSwitchBlockReason::ActingBattlerUnavailable;
					continue;
				}
				bool bHasLegalReserve = false;
				TOptional<FBattleDecisionRequest> CandidateRequest;
				if (!TryPrepareMoveEffectsPivotRequest(
						Projection,
						ProjectedAction,
						AfterStateVersion,
						bHasLegalReserve,
						CandidateRequest))
				{
					return RejectCheckpoint(
						EBattleRejectionReason::CheckpointPreparationFailed);
				}
				if (bHasLegalReserve && CandidateRequest.IsSet())
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

		Projection.NextEventOrdinal = 1;
		TArray<FBattleEvent> Events;
		Events.Reserve(
			EffectResult.Events.Num()
			+ EffectResult.SwitchIntents.Num() * 4
			+ FaintResolution.Faints.Num()
			+ FaintResolution.Removals.Num() * 3
			+ 4);
		TArray<FBattlerId> ForcedAbilityEntrants;
		for (int32 EventIndex = 0; EventIndex < EffectResult.Events.Num(); ++EventIndex)
		{
			FBattleEffectExecutionEvent Record = EffectResult.Events[EventIndex];
			TOptional<uint64> SimultaneousGroupId;
			if (const uint64* GroupId =
				FaintResolution.SimultaneousGroupsByEffectEvent.Find(EventIndex))
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
					Projection,
					ResolutionId,
					ProjectedAction,
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
					Projection,
					ResolutionId,
					ProjectedAction,
					Record,
					SimultaneousGroupId));
			}

			const FBattleFaintTransitionRecord* Faint =
				FaintResolution.Faints.FindByPredicate(
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
					Projection,
					ResolutionId,
					ProjectedAction,
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
		if (!TryResolveAbilityEntries(
				Projection,
				ForcedAbilityEntrants,
				ResolutionId,
				EBattleActionKind::Fight,
				Events))
		{
			return RejectCheckpoint(EBattleRejectionReason::CheckpointPreparationFailed);
		}

		TArray<int32> OpponentCheckpointEventIndexes;
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
				Projection,
				ResolutionId,
				ProjectedAction,
				EBattleEventType::LeftActiveSlot,
				EBattleEventCause::Rule,
				Removal.Target,
				EBattleOutcomeCause::None,
				Removal.SimultaneousGroupId,
				TOptional<uint16>(),
				TOptional<uint16>(),
				RemovalSource));
			Events.Add(MakeTargetedActionEvent(
				Projection,
				ResolutionId,
				ProjectedAction,
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
				OpponentCheckpointEventIndexes.Add(Events.Num());
				Events.Add(MakeTargetedActionEvent(
					Projection,
					ResolutionId,
					ProjectedAction,
					EBattleEventType::OpponentRemovalCheckpoint,
					EBattleEventCause::Rule,
					Removal.Target,
					EBattleOutcomeCause::None,
					Removal.SimultaneousGroupId,
					TOptional<uint16>(),
					TOptional<uint16>(),
					RemovalSource));
			}
		}

		if (PivotRequest.IsSet())
		{
			ProjectedAction.EffectExecutionState =
				EBattleLockedEffectExecutionState::AwaitingPivot;
			Projection.PendingDecision = PivotRequest.GetValue();
			Projection.PendingDecisionRequests.Reset();
			Projection.PendingDecisionRequests.Add(PivotRequest.GetValue());
		}
		else
		{
			ProjectedAction.EffectExecutionState =
				EBattleLockedEffectExecutionState::Completed;
			ProjectedAction.bFinished = true;
			Events.Add(MakeActionDetailEvent(
				Projection,
				ResolutionId,
				ProjectedAction,
				EBattleEventType::ActionCompleted,
				EBattleEventCause::Action));
			++Projection.CurrentLockedActionIndex;
			if (FaintResolution.bBattleEnded)
			{
				const FBattleEventSource* BattleEndSource = nullptr;
				for (const FBattleFaintTransitionRecord& Faint : FaintResolution.Faints)
				{
					if (EffectResult.Events.IsValidIndex(Faint.EffectEventIndex)
						&& EffectResult.Events[Faint.EffectEventIndex]
							.SourceOverride.IsSet())
					{
						BattleEndSource = &EffectResult.Events[Faint.EffectEventIndex]
							.SourceOverride.GetValue();
						break;
					}
				}
				const FBattleEventSource FinalSource = BattleEndSource != nullptr
					? *BattleEndSource
					: SourceFromLockedAction(Projection, ProjectedAction);
				if (!TryAppendMoveEffectsPartnerRecoveryEvent(
						Projection,
						ResolutionId,
						ProjectedAction.ActionId,
						ProjectedAction.Decision.GetActionKind(),
						FinalSource,
						FaintResolution,
						Events))
				{
					return RejectCheckpoint(
						EBattleRejectionReason::CheckpointPreparationFailed);
				}
				Events.Add(MakeBattleEndedEvent(
					Projection,
					ResolutionId,
					ProjectedAction,
					FaintResolution.OutcomeCause,
					BattleEndSource));
			}
			else if (!TryAppendAtomicSwitchBoundaryEvents(
				Projection,
				ResolutionId,
				ProjectedAction,
				Events))
			{
				return RejectCheckpoint(EBattleRejectionReason::CheckpointPreparationFailed);
			}
		}

		int32 ActionCompletedCount = 0;
		int32 BattleEndedIndex = INDEX_NONE;
		int32 PartnerRecoveryIndex = INDEX_NONE;
		for (int32 EventIndex = 0; EventIndex < Events.Num(); ++EventIndex)
		{
			const EBattleEventType Type = Events[EventIndex].GetType();
			ActionCompletedCount += Type == EBattleEventType::ActionCompleted ? 1 : 0;
			if (Type == EBattleEventType::BattleEnded)
			{
				BattleEndedIndex = EventIndex;
			}
			else if (Type == EBattleEventType::PartnerTeamVictoryRecovery)
			{
				PartnerRecoveryIndex = EventIndex;
			}
			if (!FBattleResolutionCommit::TryStageEvent(
					CommitPlan,
					MakeAtomicSwitchStagedEventSpec(Events[EventIndex])))
			{
				return RejectCheckpoint(EBattleRejectionReason::CheckpointPreparationFailed);
			}
		}
		if ((PivotRequest.IsSet() && ActionCompletedCount != 0)
			|| (!PivotRequest.IsSet() && ActionCompletedCount != 1)
			|| (PartnerRecoveryIndex != INDEX_NONE
				&& (BattleEndedIndex == INDEX_NONE
					|| PartnerRecoveryIndex >= BattleEndedIndex))
			|| !FBattleResolutionCommit::TryFinishAcceptedPlan(CommitPlan))
		{
			return RejectCheckpoint(EBattleRejectionReason::CheckpointPreparationFailed);
		}

		for (const int32 EventIndex : OpponentCheckpointEventIndexes)
		{
			if (!CommitPlan.Events.IsValidIndex(EventIndex)
				|| CommitPlan.Events[EventIndex].GetType()
					!= EBattleEventType::OpponentRemovalCheckpoint)
			{
				return RejectCheckpoint(EBattleRejectionReason::CheckpointPreparationFailed);
			}
			Projection.AvailableOpponentRemovalCheckpoints.Add(
				CommitPlan.Events[EventIndex].GetEventOrdinal());
		}

		FMoveEffectsCheckpointDelta Delta;
		if (!TryCaptureMoveEffectsCheckpointDelta(
				Preparation,
				CheckpointIdentity,
				Delta))
		{
			return RejectCheckpoint(EBattleRejectionReason::CheckpointPreparationFailed);
		}
		if (!IsMoveEffectsCheckpointIdentityCurrent(*State, CheckpointIdentity))
		{
			return RejectCheckpoint(EBattleRejectionReason::StaleCheckpointIdentity);
		}

		EBattleRandomTransactionCommitError RandomCommitError =
			EBattleRandomTransactionCommitError::None;
		if (!RandomTransaction->TryCommit(
				*State->Random,
				ResolutionId,
				ActionId,
				RandomCommitError))
		{
			return RejectCheckpoint(
				EBattleRejectionReason::RandomTransactionCommitFailed);
		}

		ApplyMoveEffectsCheckpointDelta(
			*State,
			CheckpointIdentity,
			Delta);
		return FBattleResolutionCommit::PublishPrepared(*State, CommitPlan);
	}
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
				AppendPartnerTeamVictoryRecoveryEvent(
					*State,
					ResolutionId,
					FActionId(),
					EBattleActionKind::Residual,
					Source,
					FaintResolution,
					Events);
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
				AppendPartnerTeamVictoryRecoveryEvent(
					*State,
					ResolutionId,
					FActionId(),
					EBattleActionKind::Residual,
					Source,
					FaintResolution,
					Events);
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
		const FBattleDecisionRequest PivotRequest = *Request;
		const FBattleDecision PivotResponse = Decision;
		FBattleLockedActionState* Action = State->LockedActions.IsValidIndex(
			State->CurrentLockedActionIndex)
			? &State->LockedActions[State->CurrentLockedActionIndex]
			: nullptr;
		if (State->Phase != EBattlePhase::Resolving
			|| State->PendingDecisionRequests.Num() != 1
			|| Action == nullptr
			|| Action->Decision.GetActionKind() != EBattleActionKind::Fight
			|| !Action->bStarted
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

		const FActionId ActionId = Action->ActionId;
		const FTrainerId DecisionOwnerTrainerId =
			PivotResponse.GetDecisionOwnerTrainerId();
		const FBattlerId OutgoingBattlerId = PivotResponse.GetActingBattlerId();
		const FActiveSlotId ActingSlotId = PivotResponse.GetActiveTargetId();
		const FBattleEventSource PivotSource = SourceFromLockedAction(*State, *Action);
		FPivotSwitchCheckpointIdentity CheckpointIdentity;
		if (!TryCapturePivotSwitchCheckpointIdentity(
				*State,
				ResolutionId,
				*Action,
				PivotRequest,
				PivotResponse,
				CheckpointIdentity))
		{
			return PublishPivotSwitchCheckpointRejection(
				*State,
				ResolutionId,
				ActionId,
				EBattleRejectionReason::CheckpointPreparationFailed,
				DecisionOwnerTrainerId,
				OutgoingBattlerId,
				PivotSource);
		}
		auto RejectPreparedCheckpoint = [&](const EBattleRejectionReason Reason)
		{
			return PublishPivotSwitchCheckpointRejection(
				*State,
				ResolutionId,
				ActionId,
				Reason,
				DecisionOwnerTrainerId,
				OutgoingBattlerId,
				PivotSource);
		};

		FBattleSwitchLegalityResult Legality;
		if (!TryBuildSwitchLegality(
				*State,
				EBattleSwitchKind::Pivot,
				DecisionOwnerTrainerId,
				OutgoingBattlerId,
				ActingSlotId,
				TConstArrayView<FPartySlotId>(),
				Legality))
		{
			return RejectPreparedCheckpoint(
				EBattleRejectionReason::CheckpointPreparationFailed);
		}
		FBattleSwitchSelectionSpec SelectionSpec;
		SelectionSpec.RequestedPartySlotId = PivotResponse.GetSwitchPartySlotId();
		FBattleSwitchResolution SwitchResolution;
		FNoDrawBattleRandom NoDrawRandom;
		if (!FBattleSwitchResolver::TryResolve(
				Legality,
				SelectionSpec,
				NoDrawRandom,
				SwitchResolution)
			|| !NoDrawRandom.GetTrace().IsEmpty())
		{
			return RejectPreparedCheckpoint(
				EBattleRejectionReason::CheckpointPreparationFailed);
		}
		if (!SwitchResolution.HasSelection())
		{
			Rejection.Reason = EBattleRejectionReason::IllegalSwitch;
			Rejection.PartySlotId = PivotResponse.GetSwitchPartySlotId();
			return MakeRejectedResolution(
				*State,
				ResolutionId,
				Rejection,
				EBattleEventType::DecisionRejected,
				EBattleEventCause::Decision,
				ActionKind,
				Source);
		}

		FBattleResolutionCommitPlan CommitPlan;
		if (!FBattleResolutionCommit::TryBeginAcceptedPlan(
				CheckpointIdentity.CommitIdentity,
				CommitPlan))
		{
			return RejectPreparedCheckpoint(
				EBattleRejectionReason::CheckpointPreparationFailed);
		}

		FSwitchCheckpointPreparation Preparation;
		if (!Preparation.Capture(
				*State,
				CheckpointIdentity.CommitIdentity.OwningActionId))
		{
			return RejectPreparedCheckpoint(
				EBattleRejectionReason::CheckpointPreparationFailed);
		}
		FMutableFieldSideCheckpointView Projection(
			*State,
			Preparation.Common,
			Preparation.Field,
			Preparation.Sides);
		FBattleLockedActionState& ProjectedAction = Preparation.Action;
		TArray<FBattleEvent> Events;
		FBattleEventTarget OutgoingTarget;
		FBattleEventTarget IncomingTarget;
		if (!TryApplySwitchSelection(
				Projection,
				DecisionOwnerTrainerId,
				OutgoingBattlerId,
				ActingSlotId,
				SwitchResolution,
				OutgoingTarget,
				IncomingTarget))
		{
			return RejectPreparedCheckpoint(
				EBattleRejectionReason::CheckpointPreparationFailed);
		}
		AppendSwitchTransitionEvents(
			Projection,
			ResolutionId,
			ProjectedAction,
			OutgoingTarget,
			IncomingTarget,
			Events);
		if (!TryRevealAirBalloonOnEntry(
				Projection,
				IncomingTarget.BattlerId,
				ResolutionId,
				EBattleActionKind::Fight,
				Events))
		{
			return RejectPreparedCheckpoint(
				EBattleRejectionReason::CheckpointPreparationFailed);
		}
		if (!TryResolveEntryHazards(
				Projection,
				IncomingTarget.BattlerId,
				IncomingTarget.ActiveSlotId,
				ResolutionId,
				Events))
		{
			return RejectPreparedCheckpoint(
				EBattleRejectionReason::CheckpointPreparationFailed);
		}
		if (!TryResolveImmediateHeldItem(
				Projection,
				IncomingTarget.BattlerId,
				ResolutionId,
				ActionId,
				EBattleActionKind::Fight,
				Events))
		{
			return RejectPreparedCheckpoint(
				EBattleRejectionReason::CheckpointPreparationFailed);
		}
		const TArray<FBattlerId> AbilityEntrants{IncomingTarget.BattlerId};
		if (!TryResolveAbilityEntries(
				Projection,
				AbilityEntrants,
				ResolutionId,
				EBattleActionKind::Fight,
				Events))
		{
			return RejectPreparedCheckpoint(
				EBattleRejectionReason::CheckpointPreparationFailed);
		}
		ProjectedAction.EffectExecutionState = EBattleLockedEffectExecutionState::Completed;
		ProjectedAction.bFinished = true;
		Events.Add(MakeActionDetailEvent(
			Projection,
			ResolutionId,
			ProjectedAction,
			EBattleEventType::ActionCompleted,
			EBattleEventCause::Action));
		++Projection.CurrentLockedActionIndex;
		Projection.PendingDecision.Reset();
		Projection.PendingDecisionRequests.Reset();
		if (!TryAppendAtomicSwitchBoundaryEvents(
				Projection,
				ResolutionId,
				ProjectedAction,
				Events))
		{
			return RejectPreparedCheckpoint(
				EBattleRejectionReason::CheckpointPreparationFailed);
		}

		for (const FBattleEvent& Event : Events)
		{
			if (!FBattleResolutionCommit::TryStageEvent(
					CommitPlan,
					MakeAtomicSwitchStagedEventSpec(Event)))
			{
				return RejectPreparedCheckpoint(
					EBattleRejectionReason::CheckpointPreparationFailed);
			}
		}
		FAtomicSwitchStateDelta Delta;
		if (!TryCaptureAtomicSwitchDelta(Preparation, Delta))
		{
			return RejectPreparedCheckpoint(
				EBattleRejectionReason::CheckpointPreparationFailed);
		}
		if (!FBattleResolutionCommit::TryFinishAcceptedPlan(CommitPlan))
		{
			return RejectPreparedCheckpoint(
				EBattleRejectionReason::CheckpointPreparationFailed);
		}
		if (!IsPivotSwitchCheckpointIdentityCurrent(*State, CheckpointIdentity)
			|| !AreAtomicCheckpointCommonDeltaRecordsValid(
				CheckpointIdentity.Battlers,
				CheckpointIdentity.ActivePositions,
				Delta))
		{
			return RejectPreparedCheckpoint(
				EBattleRejectionReason::StaleCheckpointIdentity);
		}

		ApplyPivotSwitchDelta(*State, CheckpointIdentity, Delta);
		const FBattleResolution Resolution = FBattleResolutionCommit::PublishPrepared(
			*State,
			CommitPlan);
		return Resolution;
	}
	if ((ActionKind == EBattleActionKind::ScriptedEnd
			&& !State->CompiledEncounterPolicies.IsScriptedEndingAllowed())
		|| (ActionKind != EBattleActionKind::ScriptedEnd
			&& ActionKind != EBattleActionKind::Abandon))
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
