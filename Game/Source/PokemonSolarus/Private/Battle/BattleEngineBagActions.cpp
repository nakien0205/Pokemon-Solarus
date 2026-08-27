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

namespace BattleEngineBagActionsPrivate
{
	using namespace BattleEngineCheckpointStatePrivate;
	using namespace BattleEngineCommonPrivate;
	using namespace BattleEngineEventsPrivate;
	using namespace BattleEngineQueueBoundaryPrivate;
	using namespace BattleEngineSwitchPipelinePrivate;
	using namespace BattleEngineTriggerRuntimePrivate;

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
}

using namespace BattleEngineBagActionsPrivate;

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
