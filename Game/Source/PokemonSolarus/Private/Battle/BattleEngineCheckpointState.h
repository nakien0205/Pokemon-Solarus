#pragma once

#include "Battle/BattleTargeting.h"
#include "BattleEngineCommon.h"
#include "BattleResolutionCommit.h"
#include "Math/NumericLimits.h"

namespace BattleEngineCheckpointStatePrivate
{
	using namespace BattleEngineCommonPrivate;

	struct FVoluntarySwitchBattlerIdentity
	{
		FTrainerId TrainerId;
		FBattlerId BattlerId;
		FSourcePokemonId SourcePokemonId;
		FPartySlotId PartySlotId;
		FBattleHeldItemInstanceId HeldItemInstanceId;
		FItemId CurrentHeldItemId;
		FAbilityId AbilityId;
		FConditionId MajorStatusId;
		FMoveId LastMoveId;
		FTurnId EnteredActiveOnTurnId;
		int32 CurrentHP = 0;
		int32 VolatileCount = 0;
		bool bFainted = false;
		bool bCaptured = false;
		bool bRemoved = false;
		bool bFaintTransitionPending = false;
		bool bEgg = false;
		bool bAbilitySuppressed = false;
	};

	struct FVoluntarySwitchActiveIdentity
	{
		FActiveSlotId ActiveSlotId;
		FTrainerId TrainerId;
		FBattlerId BattlerId;
		bool bAvailable = false;
	};

	/** Exact caller-serialized identity for one already-started voluntary Switch action. */

	FVoluntarySwitchBattlerIdentity MakeVoluntarySwitchBattlerIdentity(
		const FBattleBattlerState& Battler);

	bool MatchesVoluntarySwitchBattlerIdentity(
		const FBattleBattlerState& Battler,
		const FVoluntarySwitchBattlerIdentity& Identity);

	template <typename ElementType, typename EqualType>
	bool AreOrderedPivotIdentityValuesEqual(
		const TConstArrayView<ElementType> Left,
		const TConstArrayView<ElementType> Right,
		EqualType Equal)
	{
		if (Left.Num() != Right.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < Left.Num(); ++Index)
		{
			if (!Equal(Left[Index], Right[Index]))
			{
				return false;
			}
		}
		return true;
	}

	bool ArePivotDecisionsIdentical(
		const FBattleDecision& Left,
		const FBattleDecision& Right);

	bool ArePivotDecisionRequestsIdentical(
		const FBattleDecisionRequest& Left,
		const FBattleDecisionRequest& Right);

	bool ArePivotTargetResolutionsIdentical(
		const TOptional<FBattleTargetResolutionResult>& Left,
		const TOptional<FBattleTargetResolutionResult>& Right);

	bool ArePivotLockedActionsIdentical(
		const FBattleLockedActionState& Left,
		const FBattleLockedActionState& Right);

	struct FAtomicCheckpointCommonPreparation
	{
		TArray<FBattleBattlerState> Battlers;
		TArray<FBattleActivePositionState> ActivePositions;
		FBattleTriggerFramework TriggerFramework;
		FBattleAbilityItemRevealTracker AbilityItemRevealTracker;
		FBattleHeldItemLedger HeldItemLedger;
		uint64 NextConditionCreationOrdinal = 0;
		uint64 NextTriggerReentrancyToken = 0;
		uint64 NextEventOrdinal = 0;
		int32 CurrentLockedActionIndex = INDEX_NONE;
		EBattlePhase Phase = EBattlePhase::Resolving;
		EBattleOutcome Outcome = EBattleOutcome::InProgress;
		EBattleOutcomeCause OutcomeCause = EBattleOutcomeCause::None;
		TOptional<FBattleDecisionRequest> PendingDecision;
		TArray<FBattleDecisionRequest> PendingDecisionRequests;
		TArray<FBattlePendingReplacementState> PendingReplacements;
		TArray<uint64> AvailableOpponentRemovalCheckpoints;

		void Capture(const FBattleEngineState& State)
		{
			Battlers = State.Battlers;
			ActivePositions = State.ActivePositions;
			TriggerFramework = State.TriggerFramework;
			AbilityItemRevealTracker = State.AbilityItemRevealTracker;
			HeldItemLedger = State.HeldItemLedger;
			NextConditionCreationOrdinal = State.NextConditionCreationOrdinal;
			NextTriggerReentrancyToken = State.NextTriggerReentrancyToken;
			NextEventOrdinal = State.NextEventOrdinal;
			CurrentLockedActionIndex = State.CurrentLockedActionIndex;
			Phase = State.Phase;
			Outcome = State.Outcome;
			OutcomeCause = State.OutcomeCause;
			PendingDecision = State.PendingDecision;
			PendingDecisionRequests = State.PendingDecisionRequests;
			PendingReplacements = State.PendingReplacements;
			AvailableOpponentRemovalCheckpoints =
				State.AvailableOpponentRemovalCheckpoints;
		}
	};

	/** Complete switch-only preparation; immutable authority remains outside the plan. */

	struct FSwitchCheckpointPreparation
	{
		FAtomicCheckpointCommonPreparation Common;
		FBattleFieldState Field;
		TArray<FBattleSideState> Sides;
		FBattleLockedActionState Action;

		bool Capture(const FBattleEngineState& State, const FActionId ActionId)
		{
			const FBattleLockedActionState* CurrentAction = State.LockedActions.FindByPredicate(
				[ActionId](const FBattleLockedActionState& Candidate)
				{
					return Candidate.ActionId == ActionId;
				});
			if (CurrentAction == nullptr)
			{
				return false;
			}
			Common.Capture(State);
			Field = State.Field;
			Sides = State.Sides;
			Action = *CurrentAction;
			return true;
		}
	};

	/** Pre-move preparation owns only the mutable common families and its action. */

	struct FPreMoveCheckpointPreparation
	{
		FAtomicCheckpointCommonPreparation Common;
		FBattleLockedActionState Action;

		bool Capture(const FBattleEngineState& State, const FActionId ActionId)
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
			Action = *CurrentAction;
			return true;
		}
	};

	/**
	 * Call-scoped adapter over immutable engine authority and one owned preparation plan.
	 * It contains references only and is never retained by a plan or commit delta.
	 */
	template <typename TFieldState, typename TSideStates>
	struct TAtomicCheckpointStateView
	{
		const FBattleSetup& Setup;
		const FBattleDefinitionCatalog& Catalog;
		const bool& bHasCatalog;
		const uint64& StateVersion;
		const FTurnId& TurnId;
		const EBattleEncounterKind& EncounterKind;
		const EBattleFormat& Format;
		const TArray<FBattleTrainerState>& Trainers;
		TArray<FBattleBattlerState>& Battlers;
		TArray<FBattleActivePositionState>& ActivePositions;
		TFieldState& Field;
		TSideStates& Sides;
		const FBattleCompiledEncounterPolicies& CompiledEncounterPolicies;
		const TArray<FBattleLockedActionState>& LockedActions;
		int32& CurrentLockedActionIndex;
		EBattlePhase& Phase;
		EBattleOutcome& Outcome;
		EBattleOutcomeCause& OutcomeCause;
		TOptional<FBattleDecisionRequest>& PendingDecision;
		TArray<FBattleDecisionRequest>& PendingDecisionRequests;
		TArray<FBattlePendingReplacementState>& PendingReplacements;
		FBattleTriggerFramework& TriggerFramework;
		FBattleAbilityItemRevealTracker& AbilityItemRevealTracker;
		FBattleHeldItemLedger& HeldItemLedger;
		uint64& NextEventOrdinal;
		uint64& NextConditionCreationOrdinal;
		uint64& NextTriggerReentrancyToken;
		TArray<uint64>& AvailableOpponentRemovalCheckpoints;

		TAtomicCheckpointStateView(
			const FBattleEngineState& Authority,
			FAtomicCheckpointCommonPreparation& Preparation,
			TFieldState& InField,
			TSideStates& InSides)
			: Setup(Authority.Setup)
			, Catalog(Authority.Catalog)
			, bHasCatalog(Authority.bHasCatalog)
			, StateVersion(Authority.StateVersion)
			, TurnId(Authority.TurnId)
			, EncounterKind(Authority.EncounterKind)
			, Format(Authority.Format)
			, Trainers(Authority.Trainers)
			, Battlers(Preparation.Battlers)
			, ActivePositions(Preparation.ActivePositions)
			, Field(InField)
			, Sides(InSides)
			, CompiledEncounterPolicies(Authority.CompiledEncounterPolicies)
			, LockedActions(Authority.LockedActions)
			, CurrentLockedActionIndex(Preparation.CurrentLockedActionIndex)
			, Phase(Preparation.Phase)
			, Outcome(Preparation.Outcome)
			, OutcomeCause(Preparation.OutcomeCause)
			, PendingDecision(Preparation.PendingDecision)
			, PendingDecisionRequests(Preparation.PendingDecisionRequests)
			, PendingReplacements(Preparation.PendingReplacements)
			, TriggerFramework(Preparation.TriggerFramework)
			, AbilityItemRevealTracker(Preparation.AbilityItemRevealTracker)
			, HeldItemLedger(Preparation.HeldItemLedger)
			, NextEventOrdinal(Preparation.NextEventOrdinal)
			, NextConditionCreationOrdinal(Preparation.NextConditionCreationOrdinal)
			, NextTriggerReentrancyToken(Preparation.NextTriggerReentrancyToken)
			, AvailableOpponentRemovalCheckpoints(
				Preparation.AvailableOpponentRemovalCheckpoints)
		{
		}

		[[nodiscard]] const FBattleTrainerState* FindTrainer(const FTrainerId TrainerId) const
		{
			return Trainers.FindByPredicate(
				[TrainerId](const FBattleTrainerState& Candidate)
				{
					return Candidate.TrainerId == TrainerId;
				});
		}

		[[nodiscard]] const FBattleBattlerState* FindBattler(const FBattlerId BattlerId) const
		{
			return Battlers.FindByPredicate(
				[BattlerId](const FBattleBattlerState& Candidate)
				{
					return Candidate.BattlerId == BattlerId;
				});
		}

		[[nodiscard]] FBattleBattlerState* FindMutableBattler(const FBattlerId BattlerId)
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

		[[nodiscard]] FBattleActivePositionState* FindMutableActivePosition(
			const FActiveSlotId ActiveSlotId)
		{
			return ActivePositions.FindByPredicate(
				[ActiveSlotId](const FBattleActivePositionState& Candidate)
				{
					return Candidate.ActiveSlotId == ActiveSlotId;
				});
		}
	};

	using FMutableFieldSideCheckpointView = TAtomicCheckpointStateView<
		FBattleFieldState,
		TArray<FBattleSideState>>;

	using FReadOnlyFieldSideCheckpointView = TAtomicCheckpointStateView<
		const FBattleFieldState,
		const TArray<FBattleSideState>>;

	struct FAtomicBattlerRecordDelta
	{
		FBattlerId BattlerId;
		FBattleBattlerState After;
	};

	struct FAtomicActivePositionRecordDelta
	{
		FActiveSlotId ActiveSlotId;
		FBattleActivePositionState After;
	};

	struct FAtomicSideRecordDelta
	{
		EBattleSide Side = EBattleSide::Player;
		FBattleSideState After;
	};

	struct FAtomicCheckpointCommonDelta
	{
		TArray<FAtomicBattlerRecordDelta> Battlers;
		TArray<FAtomicActivePositionRecordDelta> ActivePositions;
		FBattleTriggerFramework TriggerFramework;
		FBattleAbilityItemRevealTracker AbilityItemRevealTracker;
		FBattleHeldItemLedger HeldItemLedger;
		uint64 NextConditionCreationOrdinal = 0;
		uint64 NextTriggerReentrancyToken = 0;
		int32 NextLockedActionIndex = INDEX_NONE;
		EBattlePhase Phase = EBattlePhase::Resolving;
		EBattleOutcome Outcome = EBattleOutcome::InProgress;
		EBattleOutcomeCause OutcomeCause = EBattleOutcomeCause::None;
		TOptional<FBattleDecisionRequest> PendingDecision;
		TArray<FBattleDecisionRequest> PendingDecisionRequests;
		TArray<FBattlePendingReplacementState> PendingReplacements;
		TArray<uint64> AvailableOpponentRemovalCheckpoints;
	};

	struct FAtomicSwitchStateDelta : FAtomicCheckpointCommonDelta
	{
		FBattleFieldState Field;
		TArray<FAtomicSideRecordDelta> Sides;
	};

	bool TryCaptureAtomicCheckpointCommonDelta(
		const FAtomicCheckpointCommonPreparation& Preparation,
		FAtomicCheckpointCommonDelta& OutDelta);

	bool TryCaptureAtomicFieldSideDelta(
		const FAtomicCheckpointCommonPreparation& Common,
		const FBattleFieldState& Field,
		const TConstArrayView<FBattleSideState> Sides,
		FAtomicSwitchStateDelta& OutDelta);

	bool TryCaptureAtomicSwitchDelta(
		const FSwitchCheckpointPreparation& Preparation,
		FAtomicSwitchStateDelta& OutDelta);

	bool AreAtomicCheckpointCommonDeltaRecordsValid(
		const TConstArrayView<FVoluntarySwitchBattlerIdentity> ExpectedBattlers,
		const TConstArrayView<FVoluntarySwitchActiveIdentity> ExpectedActivePositions,
		const FAtomicCheckpointCommonDelta& Delta);

	void ApplyAtomicCheckpointCommonDelta(
		FBattleEngineState& State,
		const FAtomicCheckpointCommonDelta& Delta);

	void ApplyAtomicSwitchStateDelta(
		FBattleEngineState& State,
		const FAtomicSwitchStateDelta& Delta);
}
