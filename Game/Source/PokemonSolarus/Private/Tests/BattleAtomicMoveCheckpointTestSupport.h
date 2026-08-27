#pragma once

#if WITH_DEV_AUTOMATION_TESTS

#include "BattleAtomicCheckpointTestCommon.h"
#include "BattleAtomicSwitchTestSupport.h"

namespace BattleAtomicMoveCheckpointTestSupportPrivate
{
	using namespace BattleAtomicCheckpointTestCommonPrivate;
	using namespace BattleAtomicSwitchTestSupportPrivate;

struct FActionStartCheckpointObservation
	{
		uint64 StateVersion = 0;
		uint64 NextResolutionId = 0;
		uint64 NextEventOrdinal = 0;
		uint64 NextTriggerToken = 0;
		uint64 NextConditionCreationOrdinal = 0;
		int32 ActionIndex = INDEX_NONE;
		int32 ResolutionCount = 0;
		int32 EventCount = 0;
		int32 RandomTraceCount = 0;
		int32 TotalMovePP = 0;
		int32 MaximumActions = INDEX_NONE;
		int32 RemainingActions = INDEX_NONE;
		bool bBagActionAvailable = false;
		EBattlePhase Phase = EBattlePhase::Setup;
		EBattleOutcome Outcome = EBattleOutcome::InProgress;
		EBattleOutcomeCause OutcomeCause = EBattleOutcomeCause::None;
		bool bActionStarted = false;
		bool bMoveCommitted = false;
		bool bTargetResolutionSet = false;
		EBattleLockedEffectExecutionState EffectExecutionState =
			EBattleLockedEffectExecutionState::Pending;
		bool bActionFinished = false;
		bool bPendingDecisionSet = false;
		int32 PendingDecisionRequestCount = 0;
		int32 PendingReplacementCount = 0;
		int32 RoomCount = 0;
		bool bActorActive = false;
		bool bHasHeldItem = false;
		FBattleHeldItemState HeldItem;
		TArray<FBattleHeldItemInstanceState> LedgerStates;
		TArray<FConditionId> ActorVolatileIds;
		TArray<FBattleTriggerRegistrationId> TriggerRegistrationIds;
		TArray<uint64> TriggerCreationOrdinals;
		TArray<FBattleTriggerSourceDefinition> TriggerSources;
		TArray<uint8> TriggerSuppression;
		int32 PendingTriggerDispatchCount = 0;
		int32 PendingTriggerEffectCount = 0;
		int32 PendingTriggerLifecycleCount = 0;
	};

struct FTargetCheckpointBattlerObservation
	{
		FAtomicSwitchBattlerObservation Facts;
		bool bEgg = false;
		TArray<FMoveId> MoveIds;
		TArray<int32> CurrentPP;
		TArray<int32> MaximumPP;
	};

struct FTargetCheckpointObservation
	{
		FActionStartCheckpointObservation Action;
		FAtomicSwitchCheckpointObservation Mechanics;
		bool bHasCurrentAction = false;
		FBattleLockedActionState CurrentAction;
		TArray<FTargetCheckpointBattlerObservation> Battlers;
		TArray<FBattleRandomDraw> RandomTrace;
		TOptional<FBattleDecisionRequest> PendingDecision;
		TArray<FBattleDecisionRequest> PendingRequests;
		TArray<FBattlePendingReplacementState> PendingReplacements;
		int32 TargetSuccessEventCount = 0;
	};

bool TrySeedActionStartVolatile(
		FBattleEngine& Engine,
		const FBattlerId BattlerId,
		const FConditionId VolatileId,
		const FDefinitionId PayloadId = FDefinitionId(),
		const TOptional<int32> RemainingTurns = TOptional<int32>());

bool TrySeedPreMoveMajorStatus(
		FBattleEngine& Engine,
		const FBattlerId BattlerId,
		const FConditionId StatusId,
		const TOptional<int32> SleepTurns = TOptional<int32>());

int32 CountActionStartTriggerRegistrations(
		const FBattleEngineState& State,
		const FDefinitionId DefinitionId);

bool TryPrepareLastLockedAction(
		FBattleEngine& Engine,
		const FBattlerId ExpectedActorId);

FActionStartCheckpointObservation ObserveActionStartCheckpoint(
		const FBattleEngine& Engine,
		const FBattlerId ActorId,
		const FTrainerId TrainerId);

bool VerifyRejectedActionStartCheckpoint(
		FAutomationTestBase& Test,
		const FBattleEngine& Engine,
		const FBattlerId ActorId,
		const FTrainerId TrainerId,
		const FActionStartCheckpointObservation& Before,
		const uint64 ExpectedStateVersion,
		const EBattleRejectionReason ExpectedReason,
		const FBattleResolution& Returned);

FAtomicWildScenario MakePreMoveScenario(
		const FMoveId ExtraMoveId = FMoveId(),
		const int32 PlayerCurrentHP = 200,
		const FAbilityId AbilityId = FAbilityId(),
		const FItemId HeldItemId = FItemId());

bool TryLockAndBeginPreMove(
		FBattleEngine& Engine,
		const FMoveId MoveId = FMoveId());

int32 GetPreMovePP(
		const FBattleEngine& Engine,
		const FBattlerId BattlerId,
		const FMoveId MoveId);

const FBattleConditionState* FindPreMoveVolatile(
		const FBattleEngine& Engine,
		const FBattlerId BattlerId,
		const FConditionId VolatileId);

FBattleExpectedRandomDraw MakeTargetExpectedDraw(
		const uint32 Maximum,
		const uint32 Result);

bool TryPrepareTargetCheckpoint(
		FBattleEngine& Engine,
		const FMoveId MoveId);

bool TryPrepareLastTargetCheckpoint(
		FBattleEngine& Engine,
		const FMoveId MoveId);

bool TryMarkTargetFainted(
		FBattleEngine& Engine,
		const FBattlerId BattlerId);

bool TryClearTargetActivePosition(
		FBattleEngine& Engine,
		const FActiveSlotId ActiveSlotId);

bool TrySeedChargedReleaseTargetCheckpoint(
		FBattleEngine& Engine,
		const FMoveId ChargeMoveId);

bool IsTargetCheckpointSuccessEvent(const FBattleEvent& Event);

bool HasExactTargetEventOrder(
		const FBattleResolution& Resolution,
		const TConstArrayView<EBattleEventType> Expected);

FTargetCheckpointBattlerObservation ObserveTargetCheckpointBattler(
		const FBattleEngineState& State,
		const FBattleBattlerState& Battler);

bool AreTargetCheckpointBattlersIdentical(
		const FTargetCheckpointBattlerObservation& Left,
		const FTargetCheckpointBattlerObservation& Right);

FTargetCheckpointObservation ObserveTargetCheckpoint(
		const FBattleEngine& Engine);

bool AreTargetPendingRequestsIdentical(
		const TConstArrayView<FBattleDecisionRequest> Left,
		const TConstArrayView<FBattleDecisionRequest> Right);

bool AreTargetPendingReplacementsIdentical(
		const TConstArrayView<FBattlePendingReplacementState> Left,
		const TConstArrayView<FBattlePendingReplacementState> Right);

bool AreTargetCheckpointGameplayFactsIdentical(
		const FTargetCheckpointObservation& Left,
		const FTargetCheckpointObservation& Right);

bool VerifyRejectedTargetEnvelope(
		FAutomationTestBase& Test,
		const FBattleEngine& Engine,
		const FTargetCheckpointObservation& Before,
		const EBattleRejectionReason ExpectedReason,
		const FBattleResolution& Returned);

bool VerifyRejectedTargetCheckpoint(
		FAutomationTestBase& Test,
		const FBattleEngine& Engine,
		const FTargetCheckpointObservation& Before,
		const EBattleRejectionReason ExpectedReason,
		const FBattleResolution& Returned);
}

#endif
