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
#include "BattleEngineCheckpointState.h"
#include "BattleEngineCommon.h"
#include "BattleEngineEvents.h"
#include "BattleEngineQueueBoundary.h"
#include "BattleEngineSwitchPipeline.h"
#include "BattleEngineTriggerRuntime.h"
#include "BattleResolutionCommit.h"
#include "BattleMoveRedirection.h"
#include "Math/NumericLimits.h"

namespace BattleEngineEndTurnPrivate
{
	using namespace BattleEngineCheckpointStatePrivate;
	using namespace BattleEngineCommonPrivate;
	using namespace BattleEngineEventsPrivate;
	using namespace BattleEngineQueueBoundaryPrivate;
	using namespace BattleEngineSwitchPipelinePrivate;
	using namespace BattleEngineTriggerRuntimePrivate;
}

using namespace BattleEngineEndTurnPrivate;

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
		FBattleMoveRedirection::Clear(State->MoveRedirectionRegistrations);
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
