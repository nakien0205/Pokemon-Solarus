#include "BattleEffectExecutorContext.h"

#include "BattleHeldItemMoveEffects.h"
#include "Battle/BattleFieldSideConditions.h"
#include "Battle/BattleItem.h"
#include "Battle/BattleState.h"

namespace BattleEffectExecutorItemMovesPrivate
{
	int32 GetStableActiveSlotOrder(const FActiveSlotId ActiveSlotId)
	{
		const int32 SideOffset = ActiveSlotId.GetSide() == EBattleSide::Player ? 0 : 2;
		const int32 PositionOffset =
			ActiveSlotId.GetPosition() == EBattlePosition::Left ? 0 : 1;
		return SideOffset + PositionOffset;
	}
}

namespace BattleEffectExecutorPrivate
{
	bool FStateExecutionContext::TryApplyHeldItemOperation(
		FBattleBattlerState& Battler,
		const EBattleHeldItemOperationKind Kind,
		const bool bSuppressed,
		FBattleHeldItemOperationFact& OutFact)
	{
		if (!Battler.HeldItem.InstanceId.IsValid())
		{
			return false;
		}
		FBattleHeldItemOperationRequest Operation;
		Operation.Kind = Kind;
		Operation.PrimaryInstanceId = Battler.HeldItem.InstanceId;
		Operation.bSuppressed = bSuppressed;
		EBattleHeldItemContractError Error = EBattleHeldItemContractError::None;
		if (!HeldItemLedger.TryApplyOperation(Operation, OutFact, Error))
		{
			return false;
		}
		Battler.HeldItem.CurrentItemId = OutFact.PrimaryAfter.CurrentItemId;
		Battler.HeldItem.bConsumed = OutFact.PrimaryAfter.bConsumed;
		Battler.HeldItem.bSuppressed = OutFact.PrimaryAfter.bSuppressed;
		Battler.HeldItem.bRevealed = OutFact.PrimaryAfter.bRevealed;
		Battler.HeldItem.bTemporarilyRemoved =
			OutFact.PrimaryAfter.bTemporarilyRemoved;
		const bool bItemLost = !Battler.HeldItem.CurrentItemId.IsValid()
			|| Battler.HeldItem.bConsumed
			|| Battler.HeldItem.bTemporarilyRemoved;
		if (FBattleItemRules::ShouldClearChoiceBandMoveLock(
			false,
			bItemLost,
			Battler.HeldItem.bSuppressed))
		{
			Battler.HeldItem.ChoiceLockedMoveId = FMoveId();
		}
		return true;
	}

	bool FStateExecutionContext::TryResolveHeldItemMoveIntents(
		FBattleEffectExecutionResult& Result,
		EBattleEffectExecutorError& OutError)
	{
		if (Result.HeldItemMoveIntents.IsEmpty())
		{
			return true;
		}
		if (Result.HeldItemMoveIntents.Num() != 1)
		{
			OutError = EBattleEffectExecutorError::InvalidHookResult;
			return false;
		}

		const FBattleHeldItemMoveEffectIntent Intent = Result.HeldItemMoveIntents[0];
		if (!FBattleHeldItemMoveEffects::IsKnownOperation(Intent.Operation)
			|| Intent.Operation == EBattleMoveHeldItemOperation::None
			|| Intent.Operation == EBattleMoveHeldItemOperation::Invalid
			|| Intent.EffectEventIndex < 0
			|| Intent.EffectEventIndex >= Result.Events.Num()
			|| Intent.Target.GetKind() != EBattleResolvedTargetKind::Battler)
		{
			OutError = EBattleEffectExecutorError::InvalidHookResult;
			return false;
		}
		FBattleEffectExecutionEvent& DeferredEvent =
			Result.Events[Intent.EffectEventIndex];
		if (DeferredEvent.Type != EBattleEventType::EffectDeferred
			|| DeferredEvent.Cause != EBattleEventCause::Move
			|| DeferredEvent.Outcome != EBattleEffectExecutionOutcome::Deferred
			|| DeferredEvent.Targets.Num() != 1)
		{
			OutError = EBattleEffectExecutorError::InvalidHookResult;
			return false;
		}

		FBattleBattlerState* User = FindMutableBattler(Request.UserBattlerId);
		FBattleBattlerState* Target = FindMutableBattler(
			Intent.Target.GetBattler().BattlerId);
		const FBattleActivePositionState* UserActive = User != nullptr
			? FindActiveForBattler(User->BattlerId)
			: nullptr;
		const FBattleActivePositionState* TargetActive = Target != nullptr
			? FindActiveForBattler(Target->BattlerId)
			: nullptr;
		if (User == nullptr
			|| Target == nullptr
			|| UserActive == nullptr
			|| TargetActive == nullptr
			|| !UserActive->bAvailable
			|| !TargetActive->bAvailable
			|| UserActive->BattlerId != User->BattlerId
			|| TargetActive->BattlerId != Target->BattlerId
			|| UserActive->TrainerId != User->TrainerId
			|| TargetActive->TrainerId != Target->TrainerId)
		{
			OutError = EBattleEffectExecutorError::InvalidTarget;
			return false;
		}
		if (Intent.Target.GetBattler().ActiveSlotId != TargetActive->ActiveSlotId
			|| DeferredEvent.Targets[0].TrainerId != Target->TrainerId
			|| DeferredEvent.Targets[0].BattlerId != Target->BattlerId
			|| DeferredEvent.Targets[0].ActiveSlotId != TargetActive->ActiveSlotId)
		{
			OutError = EBattleEffectExecutorError::InvalidHookResult;
			return false;
		}

		auto FailInternal = [&OutError]()
		{
			OutError = EBattleEffectExecutorError::InvalidHookResult;
			return false;
		};
		auto ResolveGameplayFailure = [&DeferredEvent](
			const EBattleEffectExecutionOutcome Outcome)
		{
			DeferredEvent.Type = Outcome == EBattleEffectExecutionOutcome::Prevented
				? EBattleEventType::EffectPrevented
				: EBattleEventType::EffectFailed;
			DeferredEvent.Outcome = Outcome;
			DeferredEvent.SourceOverride.Reset();
			DeferredEvent.NumericBefore.Reset();
			DeferredEvent.NumericAfter.Reset();
			DeferredEvent.NumericDelta.Reset();
			return true;
		};
		auto ResolvePresentItem = [this](
			const FBattleBattlerState& Battler,
			FBattleHeldItemInstanceState& OutState,
			const FBattleItemDefinition*& OutDefinition,
			bool& bOutPresent)
		{
			OutState = FBattleHeldItemInstanceState();
			OutDefinition = nullptr;
			bOutPresent = Battler.HeldItem.CurrentItemId.IsValid()
				&& !Battler.HeldItem.bConsumed
				&& !Battler.HeldItem.bTemporarilyRemoved;
			if (!bOutPresent)
			{
				if (!Battler.HeldItem.InstanceId.IsValid())
				{
					return !Battler.HeldItem.CurrentItemId.IsValid()
						&& !Battler.HeldItem.bConsumed
						&& !Battler.HeldItem.bSuppressed
						&& !Battler.HeldItem.bRevealed
						&& !Battler.HeldItem.bTemporarilyRemoved;
				}
				const FBattleHeldItemInstanceState* EmptyState =
					HeldItemLedger.FindState(Battler.HeldItem.InstanceId);
				return EmptyState != nullptr
					&& EmptyState->CurrentItemId == Battler.HeldItem.CurrentItemId
					&& EmptyState->bConsumed == Battler.HeldItem.bConsumed
					&& EmptyState->bSuppressed == Battler.HeldItem.bSuppressed
					&& EmptyState->bRevealed == Battler.HeldItem.bRevealed
					&& EmptyState->bTemporarilyRemoved
						== Battler.HeldItem.bTemporarilyRemoved
					&& !EmptyState->CurrentHolderTrainerId.IsValid()
					&& !EmptyState->CurrentHolderBattlerId.IsValid();
			}
			if (!Battler.HeldItem.InstanceId.IsValid())
			{
				return false;
			}
			const FBattleHeldItemInstanceState* State = HeldItemLedger.FindState(
				Battler.HeldItem.InstanceId);
			OutDefinition = State != nullptr
				? this->State.Catalog.FindItem(State->CurrentItemId)
				: nullptr;
			if (State == nullptr
				|| OutDefinition == nullptr
				|| State->CurrentItemId != Battler.HeldItem.CurrentItemId
				|| State->CurrentHolderTrainerId != Battler.TrainerId
				|| State->CurrentHolderBattlerId != Battler.BattlerId
				|| State->bConsumed
				|| State->bTemporarilyRemoved)
			{
				return false;
			}
			OutState = *State;
			return true;
		};
		auto IsTakeable = [](const FBattleHeldItemInstanceState& Item,
			const FBattleItemDefinition* Definition)
		{
			return FBattleHeldItemMoveEffects::IsHeldItemTakeable(
				Item.CurrentItemId.IsValid(),
				Item.bConsumed,
				Item.bTemporarilyRemoved,
				Definition);
		};
		auto ClearHeldItemMirror = [](FBattleBattlerState& Battler)
		{
			Battler.HeldItem.InstanceId = FBattleHeldItemInstanceId();
			Battler.HeldItem.CurrentItemId = FItemId();
			Battler.HeldItem.bConsumed = false;
			Battler.HeldItem.bSuppressed = false;
			Battler.HeldItem.bRevealed = false;
			Battler.HeldItem.bTemporarilyRemoved = false;
			Battler.HeldItem.ChoiceLockedMoveId = FMoveId();
		};
		auto ApplyHeldItemMirror = [](FBattleBattlerState& Battler,
			const FBattleHeldItemInstanceState& Item)
		{
			Battler.HeldItem.InstanceId = Item.InstanceId;
			Battler.HeldItem.CurrentItemId = Item.CurrentItemId;
			Battler.HeldItem.bConsumed = Item.bConsumed;
			Battler.HeldItem.bSuppressed = Item.bSuppressed;
			Battler.HeldItem.bRevealed = Item.bRevealed;
			Battler.HeldItem.bTemporarilyRemoved = Item.bTemporarilyRemoved;
			Battler.HeldItem.ChoiceLockedMoveId = FMoveId();
		};
		auto RecordPublicReveal = [this](
			const FItemId& ItemId,
			const FBattleBattlerState& Owner)
		{
			FBattleTriggerSourceDefinition SourceDefinition;
			FBattleTriggerSubject Subject;
			bool bFirstPublicReveal = false;
			EBattleAbilityItemHookError Error = EBattleAbilityItemHookError::None;
			return FBattleTriggerSourceDefinition::TryCreateItem(
					ItemId,
					SourceDefinition)
				&& FBattleTriggerSubject::TryCreateBattler(
					Owner.BattlerId,
					Subject)
				&& AbilityItemRevealTracker.TryRecordPublicReveal(
					SourceDefinition,
					Subject,
					bFirstPublicReveal,
					Error);
		};
		auto MakeMutationEvent = [this](
			const EBattleEventType Type,
			const FItemId& ItemId,
			const FBattleBattlerState& SourceBattler,
			const FBattleBattlerState& TargetBattler,
			const int64 Before,
			const int64 After,
			const int64 Delta,
			FBattleEffectExecutionEvent& OutEvent)
		{
			const FBattleActivePositionState* SourceActive = FindActiveForBattler(
				SourceBattler.BattlerId);
			const FBattleActivePositionState* DestinationActive = FindActiveForBattler(
				TargetBattler.BattlerId);
			if (!ItemId.IsValid()
				|| SourceActive == nullptr
				|| DestinationActive == nullptr
				|| !SourceActive->bAvailable
				|| !DestinationActive->bAvailable)
			{
				return false;
			}
			OutEvent = FBattleEffectExecutionEvent();
			OutEvent.Type = Type;
			OutEvent.Cause = EBattleEventCause::Item;
			OutEvent.Outcome = EBattleEffectExecutionOutcome::Applied;
			FBattleEventSource Source;
			Source.TrainerId = SourceBattler.TrainerId;
			Source.BattlerId = SourceBattler.BattlerId;
			Source.ActiveSlotId = SourceActive->ActiveSlotId;
			Source.DefinitionId = ItemId.GetDefinitionId();
			OutEvent.SourceOverride = MoveTemp(Source);
			FBattleEventTarget& EventTarget = OutEvent.Targets.AddDefaulted_GetRef();
			EventTarget.TrainerId = TargetBattler.TrainerId;
			EventTarget.BattlerId = TargetBattler.BattlerId;
			EventTarget.ActiveSlotId = DestinationActive->ActiveSlotId;
			OutEvent.NumericBefore = Before;
			OutEvent.NumericAfter = After;
			OutEvent.NumericDelta = Delta;
			return true;
		};
		auto ReplaceMutationEvents = [&Result, &Intent](
			TArray<FBattleEffectExecutionEvent>&& Events)
		{
			if (Events.IsEmpty())
			{
				return false;
			}
			Result.Events[Intent.EffectEventIndex] = MoveTemp(Events[0]);
			for (int32 Index = 1; Index < Events.Num(); ++Index)
			{
				Result.Events.Insert(
					MoveTemp(Events[Index]),
					Intent.EffectEventIndex + Index);
			}
			return true;
		};
		auto RegisterNewHolderHooks = [this](FBattleBattlerState& Holder)
		{
			const FBattleActivePositionState* Active = FindActiveForBattler(
				Holder.BattlerId);
			return Active != nullptr && TryRegisterItemHooks(Holder, *Active);
		};
		auto RunStableImmediateUpdates = [this](TArray<FBattlerId> HolderIds)
		{
			HolderIds.Sort(
				[this](const FBattlerId LeftId, const FBattlerId RightId)
				{
					const FBattleActivePositionState* Left = FindActiveForBattler(LeftId);
					const FBattleActivePositionState* Right = FindActiveForBattler(RightId);
					if (Left == nullptr || Right == nullptr)
					{
						return Left != nullptr || (Right == nullptr && LeftId < RightId);
					}
					return BattleEffectExecutorItemMovesPrivate::GetStableActiveSlotOrder(
						Left->ActiveSlotId)
						< BattleEffectExecutorItemMovesPrivate::GetStableActiveSlotOrder(
							Right->ActiveSlotId);
				});
			for (const FBattlerId HolderId : HolderIds)
			{
				FBattleBattlerState* Holder = FindMutableBattler(HolderId);
				if (Holder == nullptr || !TryRunImmediateHeldItemUpdate(*Holder))
				{
					return false;
				}
			}
			return true;
		};

		FBattleHeldItemInstanceState UserItem;
		FBattleHeldItemInstanceState TargetItem;
		const FBattleItemDefinition* UserItemDefinition = nullptr;
		const FBattleItemDefinition* TargetItemDefinition = nullptr;
		bool bUserHasItem = false;
		bool bTargetHasItem = false;
		if (!ResolvePresentItem(
				*User,
				UserItem,
				UserItemDefinition,
				bUserHasItem)
			|| !ResolvePresentItem(
				*Target,
				TargetItem,
				TargetItemDefinition,
				bTargetHasItem))
		{
			return FailInternal();
		}

		TArray<FBattleEffectExecutionEvent> MutationEvents;
		TArray<FBattlerId> ImmediateHolderIds;
		switch (Intent.Operation)
		{
		case EBattleMoveHeldItemOperation::RemoveCurrent:
		{
			if (!bTargetHasItem || !IsTakeable(TargetItem, TargetItemDefinition))
			{
				return ResolveGameplayFailure(
					EBattleEffectExecutionOutcome::Prevented);
			}
			const FItemId ItemId = TargetItem.CurrentItemId;
			if (!TryCleanupItemHooks(
					*Target,
					ItemId,
					EBattleTriggerCleanupReason::Removal))
			{
				return FailInternal();
			}
			FBattleHeldItemOperationFact RemoveFact;
			if (!TryApplyHeldItemOperation(
					*Target,
					EBattleHeldItemOperationKind::Remove,
					false,
					RemoveFact)
				|| !RecordPublicReveal(ItemId, *Target))
			{
				return FailInternal();
			}
			FBattleEffectExecutionEvent Event;
			if (!MakeMutationEvent(
					EBattleEventType::ItemRemoved,
					ItemId,
					*Target,
					*Target,
					1,
					0,
					-1,
					Event))
			{
				return FailInternal();
			}
			MutationEvents.Add(MoveTemp(Event));
			break;
		}
		case EBattleMoveHeldItemOperation::TransferCurrent:
		{
			if (bUserHasItem
				|| !bTargetHasItem
				|| !IsTakeable(TargetItem, TargetItemDefinition))
			{
				return ResolveGameplayFailure(
					EBattleEffectExecutionOutcome::Prevented);
			}
			const FItemId ItemId = TargetItem.CurrentItemId;
			if (!TryCleanupItemHooks(
					*Target,
					ItemId,
					EBattleTriggerCleanupReason::Removal))
			{
				return FailInternal();
			}
			FBattleHeldItemOperationRequest Operation;
			Operation.Kind = EBattleHeldItemOperationKind::TemporarilySteal;
			Operation.PrimaryInstanceId = TargetItem.InstanceId;
			Operation.TargetHolderTrainerId = User->TrainerId;
			Operation.TargetHolderBattlerId = User->BattlerId;
			FBattleHeldItemOperationFact TransferFact;
			EBattleHeldItemContractError Error = EBattleHeldItemContractError::None;
			if (!HeldItemLedger.TryApplyOperation(Operation, TransferFact, Error))
			{
				return FailInternal();
			}
			ClearHeldItemMirror(*Target);
			ApplyHeldItemMirror(*User, TransferFact.PrimaryAfter);
			if (!RegisterNewHolderHooks(*User)
				|| !RecordPublicReveal(ItemId, *Target)
				|| !RecordPublicReveal(ItemId, *User))
			{
				return FailInternal();
			}
			FBattleEffectExecutionEvent Event;
			if (!MakeMutationEvent(
					EBattleEventType::ItemTransferred,
					ItemId,
					*Target,
					*User,
					1,
					1,
					0,
					Event))
			{
				return FailInternal();
			}
			MutationEvents.Add(MoveTemp(Event));
			ImmediateHolderIds.Add(User->BattlerId);
			break;
		}
		case EBattleMoveHeldItemOperation::ExchangeCurrent:
		{
			if (User->BattlerId == Target->BattlerId
				|| (!bUserHasItem && !bTargetHasItem)
				|| (bUserHasItem && !IsTakeable(UserItem, UserItemDefinition))
				|| (bTargetHasItem && !IsTakeable(TargetItem, TargetItemDefinition)))
			{
				return ResolveGameplayFailure(EBattleEffectExecutionOutcome::Failed);
			}
			if ((bUserHasItem
					&& !TryCleanupItemHooks(
						*User,
						UserItem.CurrentItemId,
						EBattleTriggerCleanupReason::Removal))
				|| (bTargetHasItem
					&& !TryCleanupItemHooks(
						*Target,
						TargetItem.CurrentItemId,
						EBattleTriggerCleanupReason::Removal)))
			{
				return FailInternal();
			}

			FBattleHeldItemOperationRequest Operation;
			FBattleHeldItemOperationFact ExchangeFact;
			EBattleHeldItemContractError Error = EBattleHeldItemContractError::None;
			if (bUserHasItem && bTargetHasItem)
			{
				Operation.Kind = EBattleHeldItemOperationKind::Swap;
				Operation.PrimaryInstanceId = UserItem.InstanceId;
				Operation.SecondaryInstanceId = TargetItem.InstanceId;
				if (!HeldItemLedger.TryApplyOperation(Operation, ExchangeFact, Error)
					|| !ExchangeFact.SecondaryAfter.IsSet())
				{
					return FailInternal();
				}
				ApplyHeldItemMirror(*Target, ExchangeFact.PrimaryAfter);
				ApplyHeldItemMirror(*User, ExchangeFact.SecondaryAfter.GetValue());
			}
			else
			{
				const FBattleHeldItemInstanceState& MovedItem = bUserHasItem
					? UserItem
					: TargetItem;
				FBattleBattlerState& NewHolder = bUserHasItem ? *Target : *User;
				Operation.Kind = EBattleHeldItemOperationKind::TemporarilySteal;
				Operation.PrimaryInstanceId = MovedItem.InstanceId;
				Operation.TargetHolderTrainerId = NewHolder.TrainerId;
				Operation.TargetHolderBattlerId = NewHolder.BattlerId;
				if (!HeldItemLedger.TryApplyOperation(Operation, ExchangeFact, Error))
				{
					return FailInternal();
				}
				if (bUserHasItem)
				{
					ClearHeldItemMirror(*User);
					ApplyHeldItemMirror(*Target, ExchangeFact.PrimaryAfter);
				}
				else
				{
					ClearHeldItemMirror(*Target);
					ApplyHeldItemMirror(*User, ExchangeFact.PrimaryAfter);
				}
			}

			if ((bTargetHasItem && !RegisterNewHolderHooks(*User))
				|| (bUserHasItem && !RegisterNewHolderHooks(*Target)))
			{
				return FailInternal();
			}
			if (bUserHasItem)
			{
				if (!RecordPublicReveal(UserItem.CurrentItemId, *User)
					|| !RecordPublicReveal(UserItem.CurrentItemId, *Target))
				{
					return FailInternal();
				}
				FBattleEffectExecutionEvent Event;
				if (!MakeMutationEvent(
						EBattleEventType::ItemTransferred,
						UserItem.CurrentItemId,
						*User,
						*Target,
						1,
						1,
						0,
						Event))
				{
					return FailInternal();
				}
				MutationEvents.Add(MoveTemp(Event));
				ImmediateHolderIds.Add(Target->BattlerId);
			}
			if (bTargetHasItem)
			{
				if (!RecordPublicReveal(TargetItem.CurrentItemId, *Target)
					|| !RecordPublicReveal(TargetItem.CurrentItemId, *User))
				{
					return FailInternal();
				}
				FBattleEffectExecutionEvent Event;
				if (!MakeMutationEvent(
						EBattleEventType::ItemTransferred,
						TargetItem.CurrentItemId,
						*Target,
						*User,
						1,
						1,
						0,
						Event))
				{
					return FailInternal();
				}
				MutationEvents.Add(MoveTemp(Event));
				ImmediateHolderIds.Add(User->BattlerId);
			}
			break;
		}
		case EBattleMoveHeldItemOperation::RestoreLastConsumed:
		{
			if (User->BattlerId != Target->BattlerId || bUserHasItem)
			{
				return ResolveGameplayFailure(EBattleEffectExecutionOutcome::Failed);
			}
			const FBattleHeldItemInstanceState* Consumed =
				HeldItemLedger.FindMostRecentlyConsumedBy(
					User->TrainerId,
					User->BattlerId);
			if (Consumed == nullptr)
			{
				return ResolveGameplayFailure(EBattleEffectExecutionOutcome::Failed);
			}
			const FBattleHeldItemInstanceState ConsumedSnapshot = *Consumed;
			const FBattleItemDefinition* RestoredDefinition =
				State.Catalog.FindItem(ConsumedSnapshot.DefinitionItemId);
			if (!ConsumedSnapshot.bConsumed || RestoredDefinition == nullptr)
			{
				return FailInternal();
			}
			FBattleHeldItemOperationRequest Operation;
			Operation.Kind = EBattleHeldItemOperationKind::Restore;
			Operation.PrimaryInstanceId = ConsumedSnapshot.InstanceId;
			Operation.TargetHolderTrainerId = User->TrainerId;
			Operation.TargetHolderBattlerId = User->BattlerId;
			Operation.bSuppressed = HasRoom(
				FBattleFieldSideConditionRules::GetMagicRoomId());
			FBattleHeldItemOperationFact RestoreFact;
			EBattleHeldItemContractError Error = EBattleHeldItemContractError::None;
			if (!HeldItemLedger.TryApplyOperation(Operation, RestoreFact, Error))
			{
				return FailInternal();
			}
			ApplyHeldItemMirror(*User, RestoreFact.PrimaryAfter);
			const FItemId ItemId = User->HeldItem.CurrentItemId;
			if (!RegisterNewHolderHooks(*User)
				|| !RecordPublicReveal(ItemId, *User))
			{
				return FailInternal();
			}
			FBattleEffectExecutionEvent Event;
			if (!MakeMutationEvent(
					EBattleEventType::ItemRestored,
					ItemId,
					*User,
					*User,
					0,
					1,
					1,
					Event))
			{
				return FailInternal();
			}
			MutationEvents.Add(MoveTemp(Event));
			ImmediateHolderIds.Add(User->BattlerId);
			break;
		}
		default:
			return FailInternal();
		}

		if (!ReplaceMutationEvents(MoveTemp(MutationEvents)))
		{
			return FailInternal();
		}
		ImmediateHolderIds.Sort();
		for (int32 Index = ImmediateHolderIds.Num() - 1; Index > 0; --Index)
		{
			if (ImmediateHolderIds[Index] == ImmediateHolderIds[Index - 1])
			{
				ImmediateHolderIds.RemoveAt(Index);
			}
		}
		if (!RunStableImmediateUpdates(MoveTemp(ImmediateHolderIds)))
		{
			return FailInternal();
		}
		return true;
	}
}
