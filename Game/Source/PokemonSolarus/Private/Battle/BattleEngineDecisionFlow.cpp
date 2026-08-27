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

namespace BattleEngineDecisionFlowPrivate
{
	using namespace BattleEngineCheckpointStatePrivate;
	using namespace BattleEngineCommonPrivate;
	using namespace BattleEngineEventsPrivate;
	using namespace BattleEngineQueueBoundaryPrivate;
	using namespace BattleEngineSwitchPipelinePrivate;
	using namespace BattleEngineTriggerRuntimePrivate;

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

using namespace BattleEngineDecisionFlowPrivate;

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
