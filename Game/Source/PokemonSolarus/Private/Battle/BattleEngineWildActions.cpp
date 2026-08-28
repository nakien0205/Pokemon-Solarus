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

namespace BattleEngineWildActionsPrivate
{
	using namespace BattleEngineCheckpointStatePrivate;
	using namespace BattleEngineCommonPrivate;
	using namespace BattleEngineEventsPrivate;
	using namespace BattleEngineQueueBoundaryPrivate;
	using namespace BattleEngineSwitchPipelinePrivate;
	using namespace BattleEngineTriggerRuntimePrivate;

	/** Rejects any unexpected draw while deterministic WildFlee modes are prepared. */
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
		TArray<FBattleMoveRedirectionRegistration> MoveRedirectionRegistrations;
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
		OutDelta.MoveRedirectionRegistrations =
			State.MoveRedirectionRegistrations;
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
		State.MoveRedirectionRegistrations =
			Delta.MoveRedirectionRegistrations;

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
}

using namespace BattleEngineWildActionsPrivate;

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
	FBattleMoveRedirection::RemoveForOccupant(
		Delta.MoveRedirectionRegistrations,
		{Active->ActiveSlotId, ActingBattler->BattlerId});

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
