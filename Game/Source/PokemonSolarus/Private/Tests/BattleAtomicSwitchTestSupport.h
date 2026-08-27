#pragma once

#if WITH_DEV_AUTOMATION_TESTS

#include "BattleAtomicCheckpointTestCommon.h"

namespace BattleAtomicSwitchTestSupportPrivate
{
	using namespace BattleAtomicCheckpointTestCommonPrivate;

struct FAtomicSwitchConditionObservation
	{
		FConditionId ConditionId;
		int32 RemainingTurns = 0;
		int32 LayerCount = 0;
		uint64 CreationOrdinal = 0;
		FBattlerId SourceBattlerId;
		bool bHasRemainingTurns = false;
	};

struct FAtomicSwitchBattlerObservation
	{
		FTrainerId TrainerId;
		FBattlerId BattlerId;
		FPartySlotId PartySlotId;
		int32 CurrentHP = 0;
		TArray<int32> Stages;
		TArray<FAtomicSwitchConditionObservation> Volatiles;
		FConditionId MajorStatusId;
		FAbilityId AbilityId;
		FBattleHeldItemState HeldItem;
		FMoveId LastMoveId;
		FTurnId EnteredActiveOnTurnId;
		bool bFainted = false;
		bool bCaptured = false;
		bool bRemoved = false;
		bool bFaintTransitionPending = false;
		bool bAbilitySuppressed = false;
	};

struct FAtomicSwitchCheckpointObservation
	{
		uint64 StateVersion = 0;
		uint64 NextResolutionId = 0;
		uint64 NextEventOrdinal = 0;
		uint64 NextConditionCreationOrdinal = 0;
		uint64 NextTriggerToken = 0;
		int32 ActionIndex = INDEX_NONE;
		int32 ResolutionCount = 0;
		int32 EventCount = 0;
		int32 RandomTraceCount = 0;
		int32 PendingDecisionRequestCount = 0;
		int32 PendingReplacementCount = 0;
		int32 OpponentRemovalCheckpointCount = 0;
		int32 PendingTriggerDispatchCount = 0;
		int32 PendingTriggerEffectCount = 0;
		int32 PendingTriggerLifecycleCount = 0;
		EBattlePhase Phase = EBattlePhase::Setup;
		EBattleOutcome Outcome = EBattleOutcome::InProgress;
		EBattleOutcomeCause OutcomeCause = EBattleOutcomeCause::None;
		bool bPendingDecisionSet = false;
		bool bActionStarted = false;
		bool bActionFinished = false;
		bool bIncomingAbilityRevealed = false;
		bool bIncomingItemRevealed = false;
		FAtomicSwitchBattlerObservation Outgoing;
		FAtomicSwitchBattlerObservation Incoming;
		FAtomicSwitchBattlerObservation Opponent;
		TArray<FActiveSlotId> ActiveSlotIds;
		TArray<FTrainerId> ActiveTrainerIds;
		TArray<FBattlerId> ActiveBattlerIds;
		TArray<uint8> ActiveAvailability;
		TArray<FAtomicSwitchConditionObservation> PlayerHazards;
		TArray<FBattleHeldItemInstanceState> LedgerStates;
		TArray<FBattleTriggerRegistrationId> TriggerRegistrationIds;
		TArray<uint64> TriggerCreationOrdinals;
		TArray<FBattleTriggerSourceDefinition> TriggerSources;
		TArray<uint8> TriggerSuppression;
	};

enum class EAtomicSwitchFailureFamily : uint8
	{
		EntryItemReveal,
		EntryHazard,
		ImmediateHeldItem,
		EntryAbility
	};

template <typename ElementType, typename EqualType>
	bool AreOrderedPivotTestValuesEqual(
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

FAtomicWildScenario MakeAtomicVoluntarySwitchScenario(
		const FItemId IncomingItemId = FItemId(),
		const FAbilityId IncomingAbilityId = FBattleAbilityRules::GetBlazeId(),
		const int32 IncomingCurrentHP = 200);

FAtomicWildScenario MakeAtomicPivotSwitchScenario(
		const FItemId IncomingItemId = FItemId(),
		const FAbilityId IncomingAbilityId = FBattleAbilityRules::GetBlazeId(),
		const int32 IncomingCurrentHP = 200,
		const bool bSecondReserve = false);

bool TrySeedAtomicSwitchHazard(
		FBattleEngine& Engine,
		const FConditionId HazardId,
		const int32 Layers = 1,
		const EBattleSide AffectedSide = EBattleSide::Player);

bool TrySeedAtomicSwitchOutgoingTransients(FBattleEngine& Engine);

FAtomicSwitchConditionObservation ObserveAtomicSwitchCondition(
		const FBattleConditionState& Condition);

bool AreAtomicSwitchConditionsIdentical(
		const TArray<FAtomicSwitchConditionObservation>& Left,
		const TArray<FAtomicSwitchConditionObservation>& Right);

FAtomicSwitchBattlerObservation ObserveAtomicSwitchBattler(
		const FBattleEngineState& State,
		const FBattlerId BattlerId);

bool AreAtomicSwitchBattlersIdentical(
		const FAtomicSwitchBattlerObservation& Left,
		const FAtomicSwitchBattlerObservation& Right);

bool IsAtomicSwitchDefinitionRevealed(
		const FBattleEngineState& State,
		const FBattlerId BattlerId,
		const bool bAbility);

FAtomicSwitchCheckpointObservation ObserveAtomicSwitchCheckpoint(
		const FBattleEngine& Engine);

bool AreAtomicSwitchMechanicsIdentical(
		const FAtomicSwitchCheckpointObservation& Left,
		const FAtomicSwitchCheckpointObservation& Right);

bool ArePivotTestDecisionsIdentical(
		const FBattleDecision& Left,
		const FBattleDecision& Right);

bool ArePivotTestRequestsIdentical(
		const FBattleDecisionRequest& Left,
		const FBattleDecisionRequest& Right);

bool ArePivotTestTargetResolutionsIdentical(
		const TOptional<FBattleTargetResolutionResult>& Left,
		const TOptional<FBattleTargetResolutionResult>& Right);

bool ArePivotTestLockedActionsIdentical(
		const FBattleLockedActionState& Left,
		const FBattleLockedActionState& Right);
}

#endif
