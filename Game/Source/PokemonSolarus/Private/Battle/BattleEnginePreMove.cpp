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

namespace BattleEnginePreMovePrivate
{
	using namespace BattleEngineCheckpointStatePrivate;
	using namespace BattleEngineCommonPrivate;
	using namespace BattleEngineEventsPrivate;
	using namespace BattleEngineQueueBoundaryPrivate;
	using namespace BattleEngineSwitchPipelinePrivate;
	using namespace BattleEngineTriggerRuntimePrivate;

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
}

using namespace BattleEnginePreMovePrivate;

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
