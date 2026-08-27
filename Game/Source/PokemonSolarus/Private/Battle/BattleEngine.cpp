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

namespace
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
