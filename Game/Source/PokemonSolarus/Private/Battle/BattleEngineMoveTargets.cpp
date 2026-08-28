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

namespace BattleEnginePreMovePrivate
{
	bool ArePreMoveConditionsIdentical(
		const TArray<FBattleConditionState>& Left,
		const TArray<FBattleConditionState>& Right);
	bool ArePreMoveBattlersIdentical(
		const FBattleBattlerState& Left,
		const FBattleBattlerState& Right);
	bool ArePreMoveTriggerRegistrationsIdentical(
		const FBattleTriggerRegistrationState& Left,
		const FBattleTriggerRegistrationState& Right);
}

namespace BattleEngineMoveTargetsPrivate
{
	using namespace BattleEngineCheckpointStatePrivate;
	using namespace BattleEngineCommonPrivate;
	using namespace BattleEngineEventsPrivate;
	using namespace BattleEngineQueueBoundaryPrivate;
	using namespace BattleEngineSwitchPipelinePrivate;
	using namespace BattleEngineTriggerRuntimePrivate;
	using namespace BattleEnginePreMovePrivate;

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

		FAtomicCheckpointCommonPreparation SpeedPreparation;
		SpeedPreparation.Capture(State);
		FBattleFieldState SpeedField = State.Field;
		TArray<FBattleSideState> SpeedSides = State.Sides;
		FMutableFieldSideCheckpointView SpeedProjection(
			State,
			SpeedPreparation,
			SpeedField,
			SpeedSides);
		const FBattleBattlerTarget UserTarget{
			UserPosition->ActiveSlotId,
			User->BattlerId};
		if (!FBattleMoveRedirection::TrySelectWinningProposal(
				State.Format,
				State.TurnId,
				Action.TargetClass,
				UserTarget,
				SpeedProjection.MoveRedirectionRegistrations,
				SpeedProjection.Battlers,
				SpeedProjection.ActivePositions,
				[&SpeedProjection](
					const FBattleBattlerTarget& Target,
					int32& OutEffectiveSpeed)
				{
					const FBattleBattlerState* Battler =
						SpeedProjection.FindBattler(Target.BattlerId);
					const FBattleActivePositionState* Active =
						SpeedProjection.FindActivePosition(Target.ActiveSlotId);
					return Battler != nullptr
						&& Active != nullptr
						&& Active->BattlerId == Target.BattlerId
						&& TryCalculateEffectiveSpeedForOrdering(
							SpeedProjection,
							*Battler,
							Target.ActiveSlotId,
							OutEffectiveSpeed);
				},
				OutSpec.RedirectionProposals))
		{
			return false;
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
}

using namespace BattleEngineMoveTargetsPrivate;

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
