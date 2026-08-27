#pragma once

#include "Battle/BattleAbility.h"
#include "Battle/BattleFieldSideConditions.h"
#include "Battle/BattleItem.h"
#include "Battle/BattleMajorStatus.h"
#include "Battle/BattleStatCalculator.h"
#include "Battle/BattleVolatile.h"
#include "BattleEngineCommon.h"
#include "Math/NumericLimits.h"

namespace BattleEngineTriggerRuntimePrivate
{
	using namespace BattleEngineCommonPrivate;

	template <typename TState>
	bool TryCalculateEffectiveSpeedForOrdering(
		TState& State,
		const FBattleBattlerState& Battler,
		FActiveSlotId ActiveSlotId,
		int32& OutEffectiveSpeed);

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
		FBattleTriggerSubject& OutOwner);

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
		const int32 Layers);

	bool TrySetVolatileSuppressed(
		FBattleEngineState& State,
		const FBattlerId BattlerId,
		const FConditionId& VolatileId,
		const bool bSuppressed);

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
		FBattleTriggerOperationContext& OutContext);

	void DrainStagedTriggerOutputs(FWildActionCleanupStage& Stage);

	bool TryStageMajorStatusCleanup(
		FWildActionCleanupStage& Stage,
		const FConditionId& StatusId,
		const FBattlerId BattlerId,
		const EBattleTriggerCleanupReason Reason = EBattleTriggerCleanupReason::Removal);

	bool TryStageAbilityCleanup(
		FWildActionCleanupStage& Stage,
		const FAbilityId& AbilityId,
		const FBattlerId BattlerId,
		const EBattleTriggerCleanupReason Reason = EBattleTriggerCleanupReason::Removal);

	bool TryStageItemCleanup(
		FWildActionCleanupStage& Stage,
		const FItemId& ItemId,
		const FBattlerId BattlerId,
		const EBattleTriggerCleanupReason Reason = EBattleTriggerCleanupReason::Removal);

	bool TryStageVolatileCleanup(
		FWildActionCleanupStage& Stage,
		const FConditionId& VolatileId,
		const FBattlerId BattlerId,
		const EBattleTriggerCleanupReason Reason = EBattleTriggerCleanupReason::Removal);

	bool TryStageSourceDependentVolatileCleanup(
		FWildActionCleanupStage& Stage,
		const FBattlerId SourceBattlerId,
		const EBattleTriggerCleanupReason Reason = EBattleTriggerCleanupReason::Removal);

	bool TryStageAllOwnedVolatileCleanup(
		FWildActionCleanupStage& Stage,
		const FBattlerId BattlerId,
		const EBattleTriggerCleanupReason Reason = EBattleTriggerCleanupReason::Removal);

	bool TryStageBattleEndCleanup(FWildActionCleanupStage& Stage);
}
