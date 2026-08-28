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
	bool FStateExecutionContext::TryApplyPostMoveLifeOrbRecoil(
		FBattleEffectExecutionResult& Result,
		EBattleEffectExecutorError& OutError)
	{
		FBattleBattlerState* User = FindMutableBattler(Request.UserBattlerId);
		if (User == nullptr)
		{
			OutError = EBattleEffectExecutorError::InvalidTarget;
			return false;
		}
		const FItemId ItemId = User->HeldItem.CurrentItemId;
		if (User->CurrentHP <= 0
			|| User->bFainted
			|| User->bCaptured
			|| User->bRemoved
			|| User->HeldItem.bConsumed
			|| User->HeldItem.bTemporarilyRemoved
			|| ItemId != FBattleItemRules::GetLifeOrbId())
		{
			return true;
		}

		FBattleLifeOrbRecoilFacts Facts;
		Facts.ItemId = ItemId;
		Facts.BaseMaximumHP = User->PermanentStats.MaxHP;
		Facts.bDamagingMove = Request.Move->Category == EBattleMoveCategory::Physical
			|| Request.Move->Category == EBattleMoveCategory::Special;
		Facts.bMoveAffectedTarget = bMoveAffectedDifferentBattler;
		Facts.bSourceAndTargetDiffer = bMoveAffectedDifferentBattler;
		Facts.bForcedSwitchSuppressesRecoil = Result.SwitchIntents.ContainsByPredicate(
			[](const FBattleSwitchEffectIntent& Intent)
			{
				return Intent.Kind == EBattleSwitchKind::Forced && Intent.bApplied;
			});
		Facts.bSuppressed = User->HeldItem.bSuppressed;
		FBattleLifeOrbRecoilResult Recoil;
		if (!FBattleItemRules::TryEvaluateLifeOrbRecoil(Facts, Recoil)
			|| !Recoil.bValid)
		{
			OutError = EBattleEffectExecutorError::InvalidHookResult;
			return false;
		}
		if (!Recoil.bApplies)
		{
			return true;
		}

		if (FBattleAbilityRules::ShouldMagicGuardPreventDamage(
				User->AbilityId,
				EBattleHPChangeSourceKind::Item,
				User->bAbilitySuppressed))
		{
			FBattleAbilityItemEffectRequest AbilityRequest;
			if (!TryGetAbilityEffectRequest(
					*User,
					EBattleTriggerPhase::AfterDamage,
					EBattleAbilityItemHookPoint::AfterDamage,
					AbilityRequest)
				|| !TryRecordAbilityActivation(
					AbilityRequest,
					EBattleAbilityItemActivationOutcome::Applied,
					*User))
			{
				OutError = EBattleEffectExecutorError::InvalidHookResult;
				return false;
			}
			return true;
		}

		FBattleAbilityItemEffectRequest ItemRequest;
		if (!TryGetItemEffectRequest(
				*User,
				EBattleTriggerPhase::AfterAction,
				EBattleAbilityItemHookPoint::AfterDamage,
				ItemRequest)
			|| !TryRecordItemActivation(
				ItemRequest,
				Recoil.Outcome,
				*User,
				ItemId))
		{
			OutError = EBattleEffectExecutorError::InvalidHookResult;
			return false;
		}

		const int32 PreviousHP = User->CurrentHP;
		const int32 AppliedDamage = FMath::Min(PreviousHP, Recoil.RecoilDamage);
		User->CurrentHP -= AppliedDamage;
		if (User->CurrentHP == 0)
		{
			User->bFainted = true;
			User->bFaintTransitionPending = true;
		}
		if (!TryAppendItemMutationEvent(
				EBattleEventType::Damage,
				ItemId,
				*User,
				PreviousHP,
				User->CurrentHP,
				-AppliedDamage)
			|| !TryAppendItemMutationEvent(
				EBattleEventType::HPChanged,
				ItemId,
				*User,
				PreviousHP,
				User->CurrentHP,
				-AppliedDamage))
		{
			OutError = EBattleEffectExecutorError::InvalidHookResult;
			return false;
		}
		return true;
	}

	FBattleEffectHookResult FStateExecutionContext::CheckAbilityImmunity(
		const FBattleMoveDefinition& Move,
		const FBattleResolvedTarget& Target)
	{
		if (Target.GetKind() != EBattleResolvedTargetKind::Battler
			|| Move.Type != EPokemonType::Ground
			|| EnumHasAllFlags(Move.Flags, EBattleMoveFlags::TypelessDamage))
		{
			return Applied();
		}

		const FBattleBattlerState* User = FindBattler(Request.UserBattlerId);
		const FBattleBattlerState* Defender = FindBattler(
			Target.GetBattler().BattlerId);
		if (User == nullptr || Defender == nullptr)
		{
			return Outcome(EBattleEffectExecutionOutcome::Failed);
		}
		if (Defender->AbilityId != FBattleAbilityRules::GetLevitateId())
		{
			return Applied();
		}

		TArray<FBattleAbilityItemHookDefinition> DefenderHooks;
		if (!FBattleAbilityRules::TryGetHookDefinitionsForPhase(
				Defender->AbilityId,
				EBattleTriggerPhase::BeforeHit,
				DefenderHooks)
			|| DefenderHooks.Num() != 1
			|| DefenderHooks[0].HookPoint
				!= EBattleAbilityItemHookPoint::TypeImmunity)
		{
			return Outcome(EBattleEffectExecutionOutcome::Failed);
		}

		const bool bIgnoredForMove =
			FBattleAbilityRules::ShouldMoldBreakerIgnoreDefenderHookForMove(
				User->AbilityId,
				User->bAbilitySuppressed,
				Defender->AbilityId,
				DefenderHooks[0]);
		if (!FBattleAbilityRules::ShouldLevitatePreventMove(
				Defender->AbilityId,
				Move.Type,
				Defender->bAbilitySuppressed,
				bIgnoredForMove))
		{
			if (bIgnoredForMove)
			{
				FBattleAbilityItemEffectRequest IgnoredRequest;
				if (!TryGetAbilityEffectRequest(
						*Defender,
						EBattleTriggerPhase::BeforeHit,
						EBattleAbilityItemHookPoint::TypeImmunity,
						IgnoredRequest)
					|| !TryRecordAbilityActivation(
						IgnoredRequest,
						EBattleAbilityItemActivationOutcome::Ignored,
						*Defender))
				{
					return Outcome(EBattleEffectExecutionOutcome::Failed);
				}
			}
			return Applied();
		}

		FBattleAbilityItemEffectRequest AbilityRequest;
		if (!TryGetAbilityEffectRequest(
				*Defender,
				EBattleTriggerPhase::BeforeHit,
				EBattleAbilityItemHookPoint::TypeImmunity,
				AbilityRequest)
			|| !TryRecordAbilityActivation(
				AbilityRequest,
				EBattleAbilityItemActivationOutcome::Applied,
				*Defender))
		{
			return Outcome(EBattleEffectExecutionOutcome::Failed);
		}

		FBattleEffectHookResult Result = Outcome(
			EBattleEffectExecutionOutcome::Immune);
		Result.RuleId = Defender->AbilityId.GetDefinitionId();
		return Result;
	}

	FBattleEffectHookResult FStateExecutionContext::CheckItemImmunity(
		const FBattleMoveDefinition& Move,
		const FBattleResolvedTarget& Target)
	{
		if (Target.GetKind() != EBattleResolvedTargetKind::Battler
			|| EnumHasAllFlags(Move.Flags, EBattleMoveFlags::TypelessDamage))
		{
			return Applied();
		}
		FBattleBattlerState* Defender = FindMutableBattler(
			Target.GetBattler().BattlerId);
		if (Defender == nullptr)
		{
			return Outcome(EBattleEffectExecutionOutcome::Failed);
		}
		const FItemId ItemId = Defender->HeldItem.CurrentItemId;
		if (Defender->HeldItem.bConsumed
			|| Defender->HeldItem.bTemporarilyRemoved
			|| !FBattleItemRules::ShouldAirBalloonPreventMove(
				ItemId,
				Move.Type,
				Defender->HeldItem.bSuppressed))
		{
			return Applied();
		}
		FBattleAbilityItemEffectRequest ItemRequest;
		if (!TryGetItemEffectRequest(
				*Defender,
				EBattleTriggerPhase::BeforeHit,
				EBattleAbilityItemHookPoint::TypeImmunity,
				ItemRequest)
			|| !TryRecordItemActivation(
				ItemRequest,
				EBattleAbilityItemActivationOutcome::Applied,
				*Defender,
				ItemId))
		{
			return Outcome(EBattleEffectExecutionOutcome::Failed);
		}
		FBattleEffectHookResult Result = Outcome(
			EBattleEffectExecutionOutcome::Immune);
		Result.RuleId = ItemId.GetDefinitionId();
		return Result;
	}

	void FStateExecutionContext::RunImmediateUpdate(const FBattleResolvedTarget& Target)
	{
		if (!bRuntimeValid)
		{
			return;
		}
		if (Target.GetKind() == EBattleResolvedTargetKind::Battler)
		{
			const FBattlerId TargetId = Target.GetBattler().BattlerId;
			const bool bOriginalReachedTarget = Request.Targets.ContainsByPredicate(
				[TargetId](const FBattleResolvedTarget& Candidate)
				{
					return Candidate.GetKind() == EBattleResolvedTargetKind::Battler
						&& Candidate.GetBattler().BattlerId == TargetId;
				});
			FBattleBattlerState* Battler = FindMutableBattler(TargetId);
			const bool bDamagingMove = Request.Move->Category
					== EBattleMoveCategory::Physical
				|| Request.Move->Category == EBattleMoveCategory::Special;
			if (ExecutionResult != nullptr
				&& Battler != nullptr
				&& Battler->CurrentHP > 0
				&& !Battler->bFainted
				&& !Battler->bCaptured
				&& !Battler->bRemoved
				&& FBattleMajorStatusRules::ShouldThawReachedTarget(
					Battler->MajorStatusId,
					Request.Move->Type,
					bDamagingMove,
					EnumHasAllFlags(
						Request.Move->Flags,
						EBattleMoveFlags::ThawsTarget),
					bOriginalReachedTarget)
				&& TryCleanupCanonicalStatus(*Battler))
			{
				FBattleEventTarget EventTarget;
				EventTarget.TrainerId = Battler->TrainerId;
				EventTarget.BattlerId = Battler->BattlerId;
				EventTarget.ActiveSlotId = Target.GetBattler().ActiveSlotId;
				Battler->MajorStatusId = FConditionId();
				FBattleEffectExecutionEvent& Event =
					ExecutionResult->Events.AddDefaulted_GetRef();
				Event.Type = EBattleEventType::StatusChanged;
				Event.Cause = EBattleEventCause::Rule;
				Event.Outcome = EBattleEffectExecutionOutcome::Applied;
				Event.Targets.Add(MoveTemp(EventTarget));
				Event.NumericBefore = 1;
				Event.NumericAfter = 0;
				Event.NumericDelta = -1;
			}
			PendingImmediateItemUpdates.Remove(TargetId);
			if (Battler != nullptr && !TryRunImmediateHeldItemUpdate(*Battler))
			{
				bRuntimeValid = false;
				return;
			}
		}

		TArray<FBattlerId> PendingUpdates = PendingImmediateItemUpdates.Array();
		PendingUpdates.Sort(
			[this](const FBattlerId LeftId, const FBattlerId RightId)
			{
				const FBattleActivePositionState* Left = FindActiveForBattler(LeftId);
				const FBattleActivePositionState* Right = FindActiveForBattler(RightId);
				if (Left == nullptr || Right == nullptr)
				{
					return Left != nullptr || (Right == nullptr && LeftId < RightId);
				}
				if (Left->ActiveSlotId.GetSide() != Right->ActiveSlotId.GetSide())
				{
					return static_cast<uint8>(Left->ActiveSlotId.GetSide())
						< static_cast<uint8>(Right->ActiveSlotId.GetSide());
				}
				return static_cast<uint8>(Left->ActiveSlotId.GetPosition())
					< static_cast<uint8>(Right->ActiveSlotId.GetPosition());
			});
		PendingImmediateItemUpdates.Reset();
		for (const FBattlerId BattlerId : PendingUpdates)
		{
			FBattleBattlerState* Battler = FindMutableBattler(BattlerId);
			if (Battler != nullptr && !TryRunImmediateHeldItemUpdate(*Battler))
			{
				bRuntimeValid = false;
				return;
			}
		}
	}

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

	bool FStateExecutionContext::TryGetItemEffectRequest(
		const FBattleBattlerState& Battler,
		const EBattleTriggerPhase Phase,
		const EBattleAbilityItemHookPoint HookPoint,
		FBattleAbilityItemEffectRequest& OutRequest)
	{
		OutRequest = FBattleAbilityItemEffectRequest();
		TArray<FBattleTriggerEffectRequest> Requests;
		if (!TryDispatchItemPhase(Battler, Phase, Requests))
		{
			return false;
		}
		bool bFound = false;
		for (const FBattleTriggerEffectRequest& TriggerRequest : Requests)
		{
			FBattleAbilityItemEffectRequest TypedRequest;
			EBattleAbilityItemHookError Error = EBattleAbilityItemHookError::None;
			if (!FBattleItemRules::TryCreateTypedEffectRequest(
					TriggerRequest,
					TypedRequest,
					Error))
			{
				return false;
			}
			if (TypedRequest.HookPoint == HookPoint)
			{
				if (bFound)
				{
					return false;
				}
				OutRequest = MoveTemp(TypedRequest);
				bFound = true;
			}
		}
		return bFound;
	}

	bool FStateExecutionContext::TryBuildItemEventIdentity(
		const FBattleBattlerState& Battler,
		const FItemId& ItemId,
		FBattleEventSource& OutSource,
		FBattleEventTarget& OutTarget) const
	{
		OutSource = FBattleEventSource();
		OutTarget = FBattleEventTarget();
		const FBattleActivePositionState* Active = FindActiveForBattler(
			Battler.BattlerId);
		if (!ItemId.IsValid() || Active == nullptr || !Active->bAvailable)
		{
			return false;
		}
		OutSource.TrainerId = Battler.TrainerId;
		OutSource.BattlerId = Battler.BattlerId;
		OutSource.ActiveSlotId = Active->ActiveSlotId;
		OutSource.DefinitionId = ItemId.GetDefinitionId();
		OutTarget.TrainerId = Battler.TrainerId;
		OutTarget.BattlerId = Battler.BattlerId;
		OutTarget.ActiveSlotId = Active->ActiveSlotId;
		return true;
	}

	bool FStateExecutionContext::TryRecordItemActivation(
		const FBattleAbilityItemEffectRequest& RequestToRecord,
		const EBattleAbilityItemActivationOutcome Outcome,
		FBattleBattlerState& SourceBattler,
		const FItemId& ItemId)
	{
		TOptional<FBattleAbilityItemActivationFact> Fact;
		EBattleAbilityItemHookError Error = EBattleAbilityItemHookError::None;
		if (!AbilityItemRevealTracker.TryRecordActivation(
				RequestToRecord,
				Outcome,
				Fact,
				Error))
		{
			return false;
		}
		if (!Fact.IsSet() || !Fact.GetValue().RevealedSourceDefinition.IsSet())
		{
			return true;
		}
		const FBattleTriggerSourceDefinition& Revealed =
			Fact.GetValue().RevealedSourceDefinition.GetValue();
		if (Revealed.Kind != EBattleTriggerSourceDefinitionKind::Item
			|| Revealed.ItemId != ItemId
			|| SourceBattler.HeldItem.CurrentItemId != ItemId
			|| SourceBattler.HeldItem.bConsumed
			|| SourceBattler.HeldItem.bTemporarilyRemoved)
		{
			return false;
		}
		if (!SourceBattler.HeldItem.bRevealed)
		{
			FBattleHeldItemOperationFact RevealFact;
			if (!TryApplyHeldItemOperation(
					SourceBattler,
					EBattleHeldItemOperationKind::Reveal,
					false,
					RevealFact))
			{
				return false;
			}
		}
		if (ExecutionResult == nullptr)
		{
			return false;
		}
		FBattleEventSource Source;
		FBattleEventTarget Target;
		if (!TryBuildItemEventIdentity(SourceBattler, ItemId, Source, Target))
		{
			return false;
		}
		FBattleEffectExecutionEvent& Event =
			ExecutionResult->Events.AddDefaulted_GetRef();
		Event.Type = EBattleEventType::ItemActivated;
		Event.Cause = EBattleEventCause::Item;
		Event.Outcome = Outcome == EBattleAbilityItemActivationOutcome::Applied
			? EBattleEffectExecutionOutcome::Applied
			: EBattleEffectExecutionOutcome::Prevented;
		Event.SourceOverride = MoveTemp(Source);
		Event.Targets.Add(MoveTemp(Target));
		Event.NumericBefore = Fact.GetValue().bFirstPublicReveal ? 0 : 1;
		Event.NumericAfter = 1;
		Event.NumericDelta = Fact.GetValue().bFirstPublicReveal ? 1 : 0;
		return true;
	}

	bool FStateExecutionContext::TryAppendItemMutationEvent(
		const EBattleEventType Type,
		const FItemId& ItemId,
		const FBattleBattlerState& Battler,
		const int64 Before,
		const int64 After,
		const int64 Delta)
	{
		if (ExecutionResult == nullptr)
		{
			return false;
		}
		FBattleEventSource Source;
		FBattleEventTarget Target;
		if (!TryBuildItemEventIdentity(Battler, ItemId, Source, Target))
		{
			return false;
		}
		FBattleEffectExecutionEvent& Event =
			ExecutionResult->Events.AddDefaulted_GetRef();
		Event.Type = Type;
		Event.Cause = EBattleEventCause::Item;
		Event.Outcome = EBattleEffectExecutionOutcome::Applied;
		Event.SourceOverride = MoveTemp(Source);
		Event.Targets.Add(MoveTemp(Target));
		Event.NumericBefore = Before;
		Event.NumericAfter = After;
		Event.NumericDelta = Delta;
		return true;
	}

	bool FStateExecutionContext::TryConsumeHeldItem(FBattleBattlerState& Battler, const FItemId& ItemId)
	{
		if (Battler.HeldItem.CurrentItemId != ItemId
			|| Battler.HeldItem.bConsumed
			|| Battler.HeldItem.bTemporarilyRemoved
			|| !TryCleanupItemHooks(
				Battler,
				ItemId,
				EBattleTriggerCleanupReason::Removal))
		{
			return false;
		}
		FBattleHeldItemOperationFact ConsumeFact;
		return TryApplyHeldItemOperation(
			Battler,
			EBattleHeldItemOperationKind::Consume,
			false,
			ConsumeFact);
	}

	bool FStateExecutionContext::TryResolveHeldItemSwitchIn(
		FBattleBattlerState& Battler,
		const FBattleActivePositionState& Active)
	{
		if (!TryRegisterItemHooks(Battler, Active))
		{
			return false;
		}
		const bool bAirBalloonActive = !Battler.HeldItem.bConsumed
			&& !Battler.HeldItem.bTemporarilyRemoved
			&& !Battler.HeldItem.bSuppressed
			&& Battler.HeldItem.CurrentItemId == FBattleItemRules::GetAirBalloonId();
		if (bAirBalloonActive)
		{
			const FItemId ItemId = Battler.HeldItem.CurrentItemId;
			FBattleAbilityItemEffectRequest ItemRequest;
			if (!TryGetItemEffectRequest(
					Battler,
					EBattleTriggerPhase::SwitchIn,
					EBattleAbilityItemHookPoint::SwitchIn,
					ItemRequest)
				|| !TryRecordItemActivation(
					ItemRequest,
					EBattleAbilityItemActivationOutcome::Applied,
					Battler,
					ItemId))
			{
				return false;
			}
		}
		return TryRunImmediateHeldItemUpdate(Battler);
	}

	bool FStateExecutionContext::TryRunImmediateHeldItemUpdate(FBattleBattlerState& Battler)
	{
		const bool bDamagingHitConnected =
			PendingDamagingHitConnections.Remove(Battler.BattlerId) > 0;
		const FItemId ItemId = Battler.HeldItem.CurrentItemId;
		if (!FBattleItemRules::IsCanonical(ItemId)
			|| Battler.HeldItem.bConsumed
			|| Battler.HeldItem.bTemporarilyRemoved)
		{
			return true;
		}

		if (ItemId == FBattleItemRules::GetAirBalloonId())
		{
			if (!FBattleItemRules::ShouldPopAirBalloon(
					ItemId,
					bDamagingHitConnected,
					Battler.HeldItem.bSuppressed))
			{
				return true;
			}
			FBattleAbilityItemEffectRequest ItemRequest;
			if (!TryGetItemEffectRequest(
					Battler,
					EBattleTriggerPhase::AfterDamage,
					EBattleAbilityItemHookPoint::AfterDamage,
					ItemRequest)
				|| !TryRecordItemActivation(
					ItemRequest,
					EBattleAbilityItemActivationOutcome::Applied,
					Battler,
					ItemId)
				|| !TryCleanupItemHooks(
					Battler,
					ItemId,
					EBattleTriggerCleanupReason::Removal))
			{
				return false;
			}
			FBattleHeldItemOperationFact RemoveFact;
			if (!TryApplyHeldItemOperation(
					Battler,
					EBattleHeldItemOperationKind::Remove,
					false,
					RemoveFact)
				|| !TryAppendItemMutationEvent(
					EBattleEventType::ItemRemoved,
					ItemId,
					Battler,
					1,
					0,
					-1))
			{
				return false;
			}
			return true;
		}

		if (ItemId == FBattleItemRules::GetSitrusBerryId())
		{
			FBattleItemRecoveryFacts Facts;
			Facts.ItemId = ItemId;
			Facts.CurrentHP = Battler.CurrentHP;
			Facts.BaseMaximumHP = Battler.PermanentStats.MaxHP;
			Facts.bHealingPermitted = Battler.CurrentHP > 0 && !Battler.bFainted;
			Facts.bSuppressed = Battler.HeldItem.bSuppressed;
			FBattleItemRecoveryResult Recovery;
			if (!FBattleItemRules::TryEvaluateRecovery(Facts, Recovery)
				|| !Recovery.bValid)
			{
				return false;
			}
			if (!Recovery.bApplies)
			{
				return true;
			}
			FBattleAbilityItemEffectRequest ItemRequest;
			if (!Recovery.bConsumesItem
				|| !TryGetItemEffectRequest(
					Battler,
					EBattleTriggerPhase::AfterDamage,
					EBattleAbilityItemHookPoint::AfterDamage,
					ItemRequest)
				|| !TryRecordItemActivation(
					ItemRequest,
					Recovery.Outcome,
					Battler,
					ItemId)
				|| !TryConsumeHeldItem(Battler, ItemId)
				|| !TryAppendItemMutationEvent(
					EBattleEventType::ItemConsumed,
					ItemId,
					Battler,
					1,
					0,
					-1))
			{
				return false;
			}
			const int32 PreviousHP = Battler.CurrentHP;
			Battler.CurrentHP = FMath::Min(
				Battler.PermanentStats.MaxHP,
				Battler.CurrentHP + Recovery.HealAmount);
			const int32 AppliedHeal = Battler.CurrentHP - PreviousHP;
			return AppliedHeal > 0
				&& TryAppendItemMutationEvent(
					EBattleEventType::Healing,
					ItemId,
					Battler,
					PreviousHP,
					Battler.CurrentHP,
					AppliedHeal)
				&& TryAppendItemMutationEvent(
					EBattleEventType::HPChanged,
					ItemId,
					Battler,
					PreviousHP,
					Battler.CurrentHP,
					AppliedHeal);
		}

		if (ItemId == FBattleItemRules::GetLumBerryId())
		{
			const bool bHasMajorStatus = Battler.MajorStatusId.IsValid();
			const bool bHasConfusion = HasVolatile(
				Battler,
				FBattleVolatileRules::GetConfusionId());
			FBattleLumBerryFacts Facts;
			Facts.ItemId = ItemId;
			Facts.bHolderAbleToBattle = Battler.CurrentHP > 0
				&& !Battler.bFainted
				&& !Battler.bCaptured
				&& !Battler.bRemoved;
			Facts.bHasMajorStatus = bHasMajorStatus;
			Facts.bHasConfusion = bHasConfusion;
			Facts.bSuppressed = Battler.HeldItem.bSuppressed;
			FBattleLumBerryResult Cure;
			if (!FBattleItemRules::TryEvaluateLumBerry(Facts, Cure) || !Cure.bValid)
			{
				return false;
			}
			if (!Cure.bApplies)
			{
				return true;
			}
			FBattleAbilityItemEffectRequest ItemRequest;
			if (!Cure.bConsumesItem
				|| !TryGetItemEffectRequest(
					Battler,
					EBattleTriggerPhase::AfterHit,
					EBattleAbilityItemHookPoint::EffectApplication,
					ItemRequest)
				|| !TryRecordItemActivation(
					ItemRequest,
					Cure.Outcome,
					Battler,
					ItemId)
				|| !TryConsumeHeldItem(Battler, ItemId)
				|| !TryAppendItemMutationEvent(
					EBattleEventType::ItemConsumed,
					ItemId,
					Battler,
					1,
					0,
					-1))
			{
				return false;
			}
			if (Cure.bCuresMajorStatus)
			{
				if (!TryCleanupCanonicalStatus(Battler))
				{
					return false;
				}
				Battler.MajorStatusId = FConditionId();
			}
			if (Cure.bCuresConfusion)
			{
				if (!TryCleanupVolatile(
						Battler,
						FBattleVolatileRules::GetConfusionId(),
						EBattleTriggerCleanupReason::Removal))
				{
					return false;
				}
				Battler.Volatiles.RemoveAll(
					[](const FBattleConditionState& Condition)
					{
						return Condition.ConditionId
							== FBattleVolatileRules::GetConfusionId();
					});
			}
			const int32 CuredCount = (Cure.bCuresMajorStatus ? 1 : 0)
				+ (Cure.bCuresConfusion ? 1 : 0);
			return CuredCount > 0
				&& TryAppendItemMutationEvent(
					EBattleEventType::StatusChanged,
					ItemId,
					Battler,
					CuredCount,
					0,
					-CuredCount);
		}

		return true;
	}

	bool FStateExecutionContext::TryGetAbilityEffectRequest(
		const FBattleBattlerState& Battler,
		const EBattleTriggerPhase Phase,
		const EBattleAbilityItemHookPoint HookPoint,
		FBattleAbilityItemEffectRequest& OutRequest)
	{
		OutRequest = FBattleAbilityItemEffectRequest();
		TArray<FBattleTriggerEffectRequest> Requests;
		if (!TryDispatchAbilityPhase(Battler, Phase, Requests))
		{
			return false;
		}

		bool bFound = false;
		for (const FBattleTriggerEffectRequest& TriggerRequest : Requests)
		{
			FBattleAbilityItemEffectRequest TypedRequest;
			EBattleAbilityItemHookError Error = EBattleAbilityItemHookError::None;
			if (!FBattleAbilityRules::TryCreateTypedEffectRequest(
					TriggerRequest,
					TypedRequest,
					Error))
			{
				return false;
			}
			if (TypedRequest.HookPoint == HookPoint)
			{
				if (bFound)
				{
					return false;
				}
				OutRequest = MoveTemp(TypedRequest);
				bFound = true;
			}
		}
		return bFound;
	}

	bool FStateExecutionContext::TryRecordAbilityActivation(
		const FBattleAbilityItemEffectRequest& RequestToRecord,
		const EBattleAbilityItemActivationOutcome Outcome,
		const FBattleBattlerState& SourceBattler)
	{
		TOptional<FBattleAbilityItemActivationFact> Fact;
		EBattleAbilityItemHookError Error = EBattleAbilityItemHookError::None;
		if (!AbilityItemRevealTracker.TryRecordActivation(
				RequestToRecord,
				Outcome,
				Fact,
				Error))
		{
			return false;
		}
		if (!Fact.IsSet())
		{
			return true;
		}
		if (ExecutionResult == nullptr
			|| !Fact.GetValue().RevealedSourceDefinition.IsSet()
			|| Fact.GetValue().RevealedSourceDefinition.GetValue().Kind
				!= EBattleTriggerSourceDefinitionKind::Ability
			|| Fact.GetValue().RevealedSourceDefinition.GetValue().AbilityId
				!= SourceBattler.AbilityId)
		{
			return false;
		}
		const FBattleActivePositionState* Active = FindActiveForBattler(
			SourceBattler.BattlerId);
		if (Active == nullptr || !Active->bAvailable)
		{
			return false;
		}

		FBattleEventSource Source;
		Source.TrainerId = SourceBattler.TrainerId;
		Source.BattlerId = SourceBattler.BattlerId;
		Source.ActiveSlotId = Active->ActiveSlotId;
		Source.DefinitionId = SourceBattler.AbilityId.GetDefinitionId();
		FBattleEventTarget Target;
		Target.TrainerId = SourceBattler.TrainerId;
		Target.BattlerId = SourceBattler.BattlerId;
		Target.ActiveSlotId = Active->ActiveSlotId;

		FBattleEffectExecutionEvent& Event =
			ExecutionResult->Events.AddDefaulted_GetRef();
		Event.Type = EBattleEventType::AbilityActivated;
		Event.Cause = EBattleEventCause::Rule;
		Event.Outcome = Outcome == EBattleAbilityItemActivationOutcome::Applied
			? EBattleEffectExecutionOutcome::Applied
			: EBattleEffectExecutionOutcome::Prevented;
		Event.SourceOverride = MoveTemp(Source);
		Event.Targets.Add(MoveTemp(Target));
		Event.NumericBefore = Fact.GetValue().bFirstPublicReveal ? 0 : 1;
		Event.NumericAfter = 1;
		Event.NumericDelta = Fact.GetValue().bFirstPublicReveal ? 1 : 0;
		return true;
	}

	bool FStateExecutionContext::TryRecordLevitateGroundedActivation(
		const FBattleBattlerState& Battler,
		const EBattleTriggerPhase Phase,
		const EBattleAbilityItemHookPoint HookPoint)
	{
		FBattleAbilityItemEffectRequest AbilityRequest;
		return Battler.AbilityId == FBattleAbilityRules::GetLevitateId()
			&& TryGetAbilityEffectRequest(
				Battler,
				Phase,
				HookPoint,
				AbilityRequest)
			&& TryRecordAbilityActivation(
				AbilityRequest,
				EBattleAbilityItemActivationOutcome::Applied,
				Battler);
	}

	bool FStateExecutionContext::TrySetMagicRoomSuppression(const bool bSuppressed)
	{
		for (FBattleBattlerState& Battler : Battlers)
		{
			const bool bPresent = Battler.HeldItem.CurrentItemId.IsValid()
				&& !Battler.HeldItem.bConsumed
				&& !Battler.HeldItem.bTemporarilyRemoved;
			const bool bDesiredSuppression = bSuppressed && bPresent;
			if (!bPresent || Battler.HeldItem.bSuppressed == bDesiredSuppression)
			{
				continue;
			}
			const FItemId ItemId = Battler.HeldItem.CurrentItemId;
			if (!TryCleanupItemHooks(
					Battler,
					ItemId,
					EBattleTriggerCleanupReason::Removal))
			{
				return false;
			}
			FBattleHeldItemOperationFact SuppressFact;
			if (!TryApplyHeldItemOperation(
					Battler,
					EBattleHeldItemOperationKind::Suppress,
					bDesiredSuppression,
					SuppressFact))
			{
				return false;
			}
			const FBattleActivePositionState* Active = FindActiveForBattler(
				Battler.BattlerId);
			if (Active != nullptr && !TryRegisterItemHooks(Battler, *Active))
			{
				return false;
			}
			if (!bDesiredSuppression && Active != nullptr)
			{
				PendingImmediateItemUpdates.Add(Battler.BattlerId);
			}
		}
		return true;
	}
}
