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
	bool AreTargetResolutionRequestsIdentical(
		TConstArrayView<FBattleDecisionRequest> Left,
		TConstArrayView<FBattleDecisionRequest> Right);
	bool AreTargetResolutionPendingDecisionIdentical(
		const TOptional<FBattleDecisionRequest>& Left,
		const TOptional<FBattleDecisionRequest>& Right);
	bool AreTargetResolutionPendingReplacementsIdentical(
		TConstArrayView<FBattlePendingReplacementState> Left,
		TConstArrayView<FBattlePendingReplacementState> Right);
}

namespace BattleEngineMoveEffectsPrivate
{
	using namespace BattleEngineCheckpointStatePrivate;
	using namespace BattleEngineCommonPrivate;
	using namespace BattleEngineEventsPrivate;
	using namespace BattleEngineQueueBoundaryPrivate;
	using namespace BattleEngineSwitchPipelinePrivate;
	using namespace BattleEngineTriggerRuntimePrivate;
	using namespace BattleEnginePreMovePrivate;
	using namespace BattleEngineMoveTargetsPrivate;

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
			Common.MoveRedirectionRegistrations =
				MoveTemp(EffectPlan.MoveRedirectionRegistrations);
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
}

using namespace BattleEngineMoveEffectsPrivate;

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
				Projection.MoveRedirectionRegistrations,
				Projection.CompiledEncounterPolicies,
				FaintPlan)
			|| !FBattleFaintOutcomeResolver::TryApplyActionPlan(
				Projection.Battlers,
				Projection.ActivePositions,
				Projection.MoveRedirectionRegistrations,
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
