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
#include "Math/NumericLimits.h"

namespace BattleEngineVoluntarySwitchPrivate
{
	using namespace BattleEngineCheckpointStatePrivate;
	using namespace BattleEngineCommonPrivate;
	using namespace BattleEngineEventsPrivate;
	using namespace BattleEngineQueueBoundaryPrivate;
	using namespace BattleEngineSwitchPipelinePrivate;
	using namespace BattleEngineTriggerRuntimePrivate;

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

	/** Mutable record families shared by the switch, pre-move, and effect preparation plans. */
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
}

using namespace BattleEngineVoluntarySwitchPrivate;

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
