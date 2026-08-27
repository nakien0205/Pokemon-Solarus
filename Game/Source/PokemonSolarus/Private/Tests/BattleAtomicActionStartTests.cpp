#if WITH_DEV_AUTOMATION_TESTS

#include "BattleAtomicCheckpointTestCommon.h"
#include "BattleAtomicCheckpointTestFaults.h"
#include "BattleAtomicMoveCheckpointTestSupport.h"

namespace BattleAtomicActionStartTestsPrivate
{
	using namespace BattleAtomicCheckpointTestCommonPrivate;
	using namespace BattleAtomicCheckpointTestFaultsPrivate;
	using namespace BattleAtomicMoveCheckpointTestSupportPrivate;

bool LockCaptureThenTargetTurn(FBattleEngine& Engine)
	{
		if (Engine.GetSnapshot().GetPhase() == EBattlePhase::Setup)
		{
			FBattleRejection Rejection;
			if (!Engine.TryBeginActionDecisionSequence(Rejection))
			{
				return false;
			}
		}

		int32 Guard = 0;
		while (!Engine.GetPendingDecisionRequests().IsEmpty() && Guard++ < 4)
		{
			const TArray<FBattleDecisionRequest> Requests =
				Engine.GetPendingDecisionRequests();
			TArray<FBattleDecision> Decisions;
			for (const FBattleDecisionRequest& Request : Requests)
			{
				if (Request.GetActingBattlerId()
					== MakeNumericId<FBattlerId>(PlayerLeftValue))
				{
					Decisions.Add(MakeDecision(Request, EBattleActionKind::Bag));
				}
				else if (Request.GetActingBattlerId()
					== MakeNumericId<FBattlerId>(PlayerRightValue))
				{
					Decisions.Add(MakeDecision(
						Request,
						EBattleActionKind::Fight,
						MakeDefinitionId<FMoveId>(TargetProbeMoveName)));
				}
				else
				{
					Decisions.Add(MakeDecision(Request, EBattleActionKind::Fight));
				}
			}
			if (!Engine.SubmitDecisionBatch(
					MakeBatch(Requests, MoveTemp(Decisions))).WasAccepted())
			{
				return false;
			}
		}
		return Guard < 4 && Engine.GetSnapshot().GetPhase() == EBattlePhase::Locked;
	}

bool TrySeedActionStartMagicRoom(
		FBattleEngine& Engine,
		const FBattlerId SourceBattlerId)
	{
		FBattleEngineState& State =
			FBattleC09BWildFlowEngineFixture::GetMutableState(Engine);
		const FConditionId MagicRoomId =
			FBattleFieldSideConditionRules::GetMagicRoomId();
		if (State.Field.Rooms.ContainsByPredicate(
			[MagicRoomId](const FBattleConditionState& Condition)
			{
				return Condition.ConditionId == MagicRoomId;
			}))
		{
			return false;
		}

		FBattleTriggerSubject Source;
		if (!FBattleTriggerSubject::TryCreateBattler(SourceBattlerId, Source))
		{
			return false;
		}
		FBattleFieldSideTriggerRegistrationFacts Facts;
		Facts.ConditionId = MagicRoomId;
		Facts.PayloadId = MagicRoomId.GetDefinitionId();
		Facts.Owner = FBattleTriggerSubject::CreateField();
		Facts.Source = Source;
		Facts.Targets.Add(Facts.Owner);
		Facts.RemainingTurns = 5;
		Facts.Layers = 1;
		EBattleTriggerError TriggerError = EBattleTriggerError::None;
		if (!FBattleFieldSideConditionRules::TryRegisterTriggers(
				State.TriggerFramework,
				Facts,
				TriggerError))
		{
			return false;
		}

		FBattleConditionState MagicRoom;
		MagicRoom.ConditionId = MagicRoomId;
		MagicRoom.RemainingTurns = Facts.RemainingTurns;
		MagicRoom.LayerCount = Facts.Layers;
		MagicRoom.CreationOrdinal = State.NextConditionCreationOrdinal++;
		MagicRoom.SourceBattlerId = SourceBattlerId;
		State.Field.Rooms.Add(MoveTemp(MagicRoom));
		TArray<FBattleTriggerLifecycleFact> IgnoredFacts;
		State.TriggerFramework.DrainLifecycleFacts(IgnoredFacts);
		return true;
	}

bool HasActionStartVolatile(
		const FBattleEngineState& State,
		const FBattlerId BattlerId,
		const FConditionId VolatileId)
	{
		const FBattleBattlerState* Battler = State.FindBattler(BattlerId);
		return Battler != nullptr
			&& Battler->Volatiles.ContainsByPredicate(
				[VolatileId](const FBattleConditionState& Condition)
				{
					return Condition.ConditionId == VolatileId;
				});
	}

bool TryClearActionStartActiveSlot(
		FBattleEngine& Engine,
		const FActiveSlotId ActiveSlotId)
	{
		FBattleEngineState& State =
			FBattleC09BWildFlowEngineFixture::GetMutableState(Engine);
		FBattleActivePositionState* Active = State.FindMutableActivePosition(ActiveSlotId);
		if (Active == nullptr)
		{
			return false;
		}
		Active->TrainerId = FTrainerId();
		Active->BattlerId = FBattlerId();
		return true;
	}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E1ActionStartProceedTest,
	"PokemonSolarus.Battle.ADR0002.3E1.ActionStart.Success.ProceedSuppressionAndObedience",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E1ActionStartProceedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FAtomicWildScenario Scenario;
	Scenario.PlayerLeftSpeed = 150;
	Scenario.bPlayerHasCanonicalHeldItem = true;
	Scenario.bPlayerSubjectToObedience = true;
	Scenario.PlayerReferenceLevel = 20;
	Scenario.PlayerBadgeCount = 0;
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	const FTrainerId TrainerId = MakeNumericId<FTrainerId>(PlayerTrainerValue);

	TUniquePtr<FBattleEngine> Engine;
	if (!TestTrue(TEXT("Proceed engine is created"),
		TryMakeSequenceEngine(Scenario, {}, Engine))
		|| !TestTrue(TEXT("Proceed action is locked first"),
			LockTurn(*Engine, PlayerLeftValue, EBattleActionKind::Fight))
		|| !TestTrue(TEXT("Magic Room is seeded before action start"),
			TrySeedActionStartMagicRoom(*Engine, ActorId)))
	{
		return false;
	}
	const FActionStartCheckpointObservation Before =
		ObserveActionStartCheckpoint(*Engine, ActorId, TrainerId);
	const FBattleResolution Resolution = Engine->BeginNextLockedAction();
	const FBattleEngineState& State =
		FBattleC09BWildFlowEngineFixture::GetState(*Engine);
	const FBattleTrainerState* Trainer = State.FindTrainer(TrainerId);
	const FBattleBattlerState* Actor = State.FindBattler(ActorId);
	const FBattleHeldItemInstanceState* LedgerItem = Actor != nullptr
		? State.HeldItemLedger.FindState(Actor->HeldItem.InstanceId)
		: nullptr;

	TestTrue(TEXT("Proceed action start is accepted"), Resolution.WasAccepted());
	TestTrue(TEXT("Proceed keeps exact action-start event order"),
		HasExactEventOrder(Resolution, {
			EBattleEventType::ActionStarted,
			EBattleEventType::ObedienceConfirmed}));
	TestTrue(TEXT("Proceed obedience fact keeps canonical numeric metadata"),
		Resolution.GetEvents().Num() == 2
			&& Resolution.GetEvents()[1].GetNumericBefore()
				== TOptional<int64>(20)
			&& Resolution.GetEvents()[1].GetNumericAfter()
				== TOptional<int64>(20)
			&& Resolution.GetEvents()[1].GetNumericDelta()
				== TOptional<int64>(0));
	TestTrue(TEXT("Obedience confirmation remains CoreOnly"),
		Resolution.GetEvents().Num() == 2
			&& Resolution.GetEvents()[0].GetVisibility().Level
				== EBattleVisibilityLevel::Public
			&& Resolution.GetEvents()[1].GetVisibility().Level
				== EBattleVisibilityLevel::CoreOnly);
	TestTrue(TEXT("Proceed returns the exact single append"),
		IsReturnedResolutionAppendedExactlyOnce(*Engine, Resolution));
	TestEqual(TEXT("Proceed increments state version once"),
		State.StateVersion, Before.StateVersion + 1);
	TestEqual(TEXT("Proceed resolution reports the exact version pair"),
		Resolution.GetBeforeStateVersion(), Before.StateVersion);
	TestEqual(TEXT("Proceed resolution reports one after-version"),
		Resolution.GetAfterStateVersion(), Before.StateVersion + 1);
	TestEqual(TEXT("Proceed keeps the current action cursor"),
		State.CurrentLockedActionIndex, Before.ActionIndex);
	TestEqual(TEXT("Proceed enters Resolving"), State.Phase, EBattlePhase::Resolving);
	TestTrue(TEXT("Proceed starts without finishing or committing the action"),
		State.LockedActions.IsValidIndex(Before.ActionIndex)
			&& State.LockedActions[Before.ActionIndex].bStarted
			&& !State.LockedActions[Before.ActionIndex].bFinished
			&& !State.LockedActions[Before.ActionIndex].bMoveCommitted
			&& !State.LockedActions[Before.ActionIndex].TargetResolution.IsSet()
			&& State.LockedActions[Before.ActionIndex].EffectExecutionState
				== EBattleLockedEffectExecutionState::Pending);
	TestEqual(TEXT("Proceed consumes one Trainer action"),
		Trainer != nullptr ? Trainer->ActionAllowance.RemainingActions : INDEX_NONE,
		Before.RemainingActions - 1);
	TestTrue(TEXT("Proceed suppresses both battler and ledger held-item facts"),
		Actor != nullptr
			&& Actor->HeldItem.CurrentItemId == FBattleItemRules::GetLeftoversId()
			&& Actor->HeldItem.bSuppressed
			&& !Actor->HeldItem.bConsumed
			&& LedgerItem != nullptr
			&& LedgerItem->bSuppressed
			&& !LedgerItem->bConsumed);
	TestTrue(TEXT("Proceed preserves Magic Room and re-registers suppressed item hooks"),
		CountActionStartTriggerRegistrations(
			State,
			FBattleFieldSideConditionRules::GetMagicRoomId().GetDefinitionId()) > 0
			&& CountActionStartTriggerRegistrations(
				State,
				FBattleItemRules::GetLeftoversId().GetDefinitionId()) > 0
			&& State.TriggerFramework.GetActiveRegistrations().ContainsByPredicate(
				[](const FBattleTriggerRegistrationState& Registration)
				{
					return Registration.Spec.SourceDefinition.Kind
							== EBattleTriggerSourceDefinitionKind::Item
						&& Registration.Spec.SourceDefinition.ItemId
							== FBattleItemRules::GetLeftoversId()
						&& Registration.bSuppressed;
				}));
	TestEqual(TEXT("Proceed commits the staged Magic Room and item-cleanup tokens"),
		State.NextTriggerReentrancyToken, Before.NextTriggerToken + 2);
	TestEqual(TEXT("Proceed consumes no PP"),
		ObserveActionStartCheckpoint(*Engine, ActorId, TrainerId).TotalMovePP,
		Before.TotalMovePP);
	TestEqual(TEXT("Proceed consumes no RNG"),
		State.Random->GetTrace().Num(), Before.RandomTraceCount);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E1ActionStartRechargeTest,
	"PokemonSolarus.Battle.ADR0002.3E1.ActionStart.Success.RechargeDenial",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E1ActionStartRechargeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FAtomicWildScenario Scenario;
	Scenario.PlayerLeftSpeed = 150;
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	const FTrainerId TrainerId = MakeNumericId<FTrainerId>(PlayerTrainerValue);
	const FMoveId MoveId = MakeDefinitionId<FMoveId>(ProbeMoveName);
	TUniquePtr<FBattleEngine> Engine;
	if (!TestTrue(TEXT("Recharge engine is created"),
		TryMakeSequenceEngine(Scenario, {}, Engine))
		|| !TestTrue(TEXT("Recharge action is locked first"),
			LockTurn(*Engine, PlayerLeftValue, EBattleActionKind::Fight))
		|| !TestTrue(TEXT("Charging is seeded"),
			TrySeedActionStartVolatile(
				*Engine,
				ActorId,
				FBattleVolatileRules::GetChargingId(),
				MoveId.GetDefinitionId()))
		|| !TestTrue(TEXT("Fly semi-invulnerability is seeded"),
			TrySeedActionStartVolatile(
				*Engine,
				ActorId,
				FBattleVolatileRules::GetFlySemiInvulnerableId()))
		|| !TestTrue(TEXT("Recharge is seeded"),
			TrySeedActionStartVolatile(
				*Engine,
				ActorId,
				FBattleVolatileRules::GetRechargeId())))
	{
		return false;
	}
	const FActionStartCheckpointObservation Before =
		ObserveActionStartCheckpoint(*Engine, ActorId, TrainerId);
	const FBattleResolution Resolution = Engine->BeginNextLockedAction();
	const FBattleEngineState& State =
		FBattleC09BWildFlowEngineFixture::GetState(*Engine);
	const FBattleTrainerState* Trainer = State.FindTrainer(TrainerId);

	TestTrue(TEXT("Recharge denial is an accepted consumed action"),
		Resolution.WasAccepted());
	TestTrue(TEXT("Recharge denial keeps exact event order"),
		HasExactEventOrder(Resolution, {
			EBattleEventType::ActionStarted,
			EBattleEventType::StatusChanged,
			EBattleEventType::EffectPrevented,
			EBattleEventType::ActionCanceled,
			EBattleEventType::ActionCompleted}));
	TestTrue(TEXT("Recharge status removal keeps exact numeric metadata"),
		Resolution.GetEvents().Num() == 5
			&& Resolution.GetEvents()[1].GetNumericBefore()
				== TOptional<int64>(1)
			&& Resolution.GetEvents()[1].GetNumericAfter()
				== TOptional<int64>(0)
			&& Resolution.GetEvents()[1].GetNumericDelta()
				== TOptional<int64>(-1));
	TestTrue(TEXT("Recharge denial returns the exact single append"),
		IsReturnedResolutionAppendedExactlyOnce(*Engine, Resolution));
	TestEqual(TEXT("Recharge denial increments state version once"),
		State.StateVersion, Before.StateVersion + 1);
	TestEqual(TEXT("Recharge denial advances exactly one queue action"),
		State.CurrentLockedActionIndex, Before.ActionIndex + 1);
	TestEqual(TEXT("Recharge denial remains Resolving while another action waits"),
		State.Phase, EBattlePhase::Resolving);
	TestTrue(TEXT("Recharge denial marks the consumed action started and finished"),
		State.LockedActions.IsValidIndex(Before.ActionIndex)
			&& State.LockedActions[Before.ActionIndex].bStarted
			&& State.LockedActions[Before.ActionIndex].bFinished
			&& !State.LockedActions[Before.ActionIndex].bMoveCommitted);
	TestEqual(TEXT("Recharge denial consumes one Trainer action"),
		Trainer != nullptr ? Trainer->ActionAllowance.RemainingActions : INDEX_NONE,
		Before.RemainingActions - 1);
	TestFalse(TEXT("Recharge denial removes Recharge"),
		HasActionStartVolatile(State, ActorId, FBattleVolatileRules::GetRechargeId()));
	TestFalse(TEXT("Recharge denial clears Charging"),
		HasActionStartVolatile(State, ActorId, FBattleVolatileRules::GetChargingId()));
	TestFalse(TEXT("Recharge denial clears Fly semi-invulnerability"),
		HasActionStartVolatile(
			State,
			ActorId,
			FBattleVolatileRules::GetFlySemiInvulnerableId()));
	TestTrue(TEXT("Recharge and charge trigger registrations are cleaned"),
		CountActionStartTriggerRegistrations(
			State,
			FBattleVolatileRules::GetRechargeId().GetDefinitionId()) == 0
			&& CountActionStartTriggerRegistrations(
				State,
				FBattleVolatileRules::GetChargingId().GetDefinitionId()) == 0
			&& CountActionStartTriggerRegistrations(
				State,
				FBattleVolatileRules::GetFlySemiInvulnerableId().GetDefinitionId()) == 0);
	TestEqual(TEXT("Recharge denial commits dispatch plus three cleanup tokens"),
		State.NextTriggerReentrancyToken, Before.NextTriggerToken + 4);
	TestEqual(TEXT("Recharge denial consumes no PP"),
		ObserveActionStartCheckpoint(*Engine, ActorId, TrainerId).TotalMovePP,
		Before.TotalMovePP);
	TestEqual(TEXT("Recharge denial consumes no RNG"),
		State.Random->GetTrace().Num(), Before.RandomTraceCount);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E1ActionStartObedienceRefusalTest,
	"PokemonSolarus.Battle.ADR0002.3E1.ActionStart.Success.ObedienceRefusalAndChargeCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E1ActionStartObedienceRefusalTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	FAtomicWildScenario Scenario;
	Scenario.PlayerLeftSpeed = 150;
	Scenario.bPlayerSubjectToObedience = true;
	Scenario.PlayerReferenceLevel = 21;
	Scenario.PlayerBadgeCount = 0;
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	const FTrainerId TrainerId = MakeNumericId<FTrainerId>(PlayerTrainerValue);
	const FMoveId MoveId = MakeDefinitionId<FMoveId>(ProbeMoveName);
	TUniquePtr<FBattleEngine> Engine;
	if (!TestTrue(TEXT("Obedience-refusal engine is created"),
		TryMakeSequenceEngine(Scenario, {}, Engine))
		|| !TestTrue(TEXT("Obedience-refusal action is locked first"),
			LockTurn(*Engine, PlayerLeftValue, EBattleActionKind::Fight))
		|| !TestTrue(TEXT("Refused action Charging is seeded"),
			TrySeedActionStartVolatile(
				*Engine,
				ActorId,
				FBattleVolatileRules::GetChargingId(),
				MoveId.GetDefinitionId()))
		|| !TestTrue(TEXT("Refused action Fly state is seeded"),
			TrySeedActionStartVolatile(
				*Engine,
				ActorId,
				FBattleVolatileRules::GetFlySemiInvulnerableId())))
	{
		return false;
	}
	const FActionStartCheckpointObservation Before =
		ObserveActionStartCheckpoint(*Engine, ActorId, TrainerId);
	const FBattleResolution Resolution = Engine->BeginNextLockedAction();
	const FBattleEngineState& State =
		FBattleC09BWildFlowEngineFixture::GetState(*Engine);
	const FBattleTrainerState* Trainer = State.FindTrainer(TrainerId);

	TestTrue(TEXT("Obedience refusal is an accepted consumed action"),
		Resolution.WasAccepted());
	TestTrue(TEXT("Obedience refusal keeps exact event order"),
		HasExactEventOrder(Resolution, {
			EBattleEventType::ActionStarted,
			EBattleEventType::ObedienceRefused,
			EBattleEventType::ActionCompleted}));
	TestTrue(TEXT("Obedience refusal keeps exact public numeric metadata"),
		Resolution.GetEvents().Num() == 3
			&& Resolution.GetEvents()[1].GetVisibility().Level
				== EBattleVisibilityLevel::Public
			&& Resolution.GetEvents()[1].GetNumericBefore()
				== TOptional<int64>(21)
			&& Resolution.GetEvents()[1].GetNumericAfter()
				== TOptional<int64>(20)
			&& Resolution.GetEvents()[1].GetNumericDelta()
				== TOptional<int64>(1));
	TestTrue(TEXT("Obedience refusal returns the exact single append"),
		IsReturnedResolutionAppendedExactlyOnce(*Engine, Resolution));
	TestEqual(TEXT("Obedience refusal increments state version once"),
		State.StateVersion, Before.StateVersion + 1);
	TestEqual(TEXT("Obedience refusal advances one queue action"),
		State.CurrentLockedActionIndex, Before.ActionIndex + 1);
	TestTrue(TEXT("Obedience refusal starts and finishes the action"),
		State.LockedActions.IsValidIndex(Before.ActionIndex)
			&& State.LockedActions[Before.ActionIndex].bStarted
			&& State.LockedActions[Before.ActionIndex].bFinished
			&& !State.LockedActions[Before.ActionIndex].bMoveCommitted);
	TestEqual(TEXT("Obedience refusal consumes one Trainer action"),
		Trainer != nullptr ? Trainer->ActionAllowance.RemainingActions : INDEX_NONE,
		Before.RemainingActions - 1);
	TestFalse(TEXT("Obedience refusal clears Charging"),
		HasActionStartVolatile(State, ActorId, FBattleVolatileRules::GetChargingId()));
	TestFalse(TEXT("Obedience refusal clears Fly semi-invulnerability"),
		HasActionStartVolatile(
			State,
			ActorId,
			FBattleVolatileRules::GetFlySemiInvulnerableId()));
	TestEqual(TEXT("Obedience refusal commits both charge-cleanup tokens"),
		State.NextTriggerReentrancyToken, Before.NextTriggerToken + 2);
	TestEqual(TEXT("Obedience refusal consumes no PP"),
		ObserveActionStartCheckpoint(*Engine, ActorId, TrainerId).TotalMovePP,
		Before.TotalMovePP);
	TestEqual(TEXT("Obedience refusal consumes no RNG"),
		State.Random->GetTrace().Num(), Before.RandomTraceCount);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E1ActionStartActorInvalidationTest,
	"PokemonSolarus.Battle.ADR0002.3E1.ActionStart.Success.ActorInvalidationReplacementBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E1ActionStartActorInvalidationTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	FAtomicWildScenario Scenario;
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	const FTrainerId TrainerId = MakeNumericId<FTrainerId>(PlayerTrainerValue);
	const FActiveSlotId ActorSlot = MakeActiveSlotId(
		EBattleSide::Player,
		EBattlePosition::Left);
	TUniquePtr<FBattleEngine> Engine;
	if (!TestTrue(TEXT("Actor-invalidation engine is created"),
		TryMakeSequenceEngine(Scenario, {}, Engine))
		|| !TestTrue(TEXT("Actor-invalidation turn is locked"),
			LockTurn(*Engine, PlayerLeftValue, EBattleActionKind::Fight))
		|| !TestTrue(TEXT("Player action is selected as the last queued start"),
			TryPrepareLastLockedAction(*Engine, ActorId))
		|| !TestTrue(TEXT("The last action actor is invalidated from its active slot"),
			TryClearActionStartActiveSlot(*Engine, ActorSlot)))
	{
		return false;
	}
	const FActionStartCheckpointObservation Before =
		ObserveActionStartCheckpoint(*Engine, ActorId, TrainerId);
	const FBattleResolution Resolution = Engine->BeginNextLockedAction();
	const FBattleEngineState& State =
		FBattleC09BWildFlowEngineFixture::GetState(*Engine);
	const FBattleTrainerState* Trainer = State.FindTrainer(TrainerId);

	TestTrue(TEXT("Actor invalidation is an accepted cancellation"),
		Resolution.WasAccepted());
	TestTrue(TEXT("Actor invalidation and replacement keep exact event order"),
		HasExactEventOrder(Resolution, {
			EBattleEventType::ActionCanceled,
			EBattleEventType::ActionCompleted,
			EBattleEventType::ReplacementRequired}));
	TestTrue(TEXT("Actor invalidation cancellation stays public and action-caused"),
		Resolution.GetEvents().Num() == 3
			&& Resolution.GetEvents()[0].GetCause()
				== EBattleEventCause::Action
			&& Resolution.GetEvents()[0].GetVisibility().Level
				== EBattleVisibilityLevel::Public);
	TestTrue(TEXT("Actor invalidation returns the exact single append"),
		IsReturnedResolutionAppendedExactlyOnce(*Engine, Resolution));
	TestEqual(TEXT("Actor invalidation increments state version once"),
		State.StateVersion, Before.StateVersion + 1);
	TestEqual(TEXT("Actor invalidation exhausts the queue"),
		State.CurrentLockedActionIndex, State.LockedActions.Num());
	TestTrue(TEXT("Actor invalidation finishes without starting the action"),
		State.LockedActions.IsValidIndex(Before.ActionIndex)
			&& !State.LockedActions[Before.ActionIndex].bStarted
			&& State.LockedActions[Before.ActionIndex].bFinished
			&& !State.LockedActions[Before.ActionIndex].bMoveCommitted);
	TestEqual(TEXT("Actor invalidation consumes one Trainer action"),
		Trainer != nullptr ? Trainer->ActionAllowance.RemainingActions : INDEX_NONE,
		Before.RemainingActions - 1);
	TestEqual(TEXT("Actor invalidation stages MandatoryReplacement"),
		State.Phase, EBattlePhase::MandatoryReplacement);
	TestTrue(TEXT("Actor invalidation stages one canonical replacement request"),
		State.PendingReplacements.Num() == 1
			&& State.PendingReplacements[0].TrainerId == TrainerId
			&& State.PendingReplacements[0].ActiveSlotId == ActorSlot
			&& State.PendingDecision.IsSet()
			&& State.PendingDecisionRequests.Num() == 1
			&& State.PendingDecisionRequests[0].GetRequestKind()
				== EBattleDecisionRequestKind::MandatoryReplacement
			&& State.PendingDecisionRequests[0].GetStateVersion()
				== State.StateVersion);
	TestTrue(TEXT("ReplacementRequired targets the exact empty slot"),
		Resolution.GetEvents().Num() == 3
			&& Resolution.GetEvents()[2].GetTargets().Num() == 1
			&& Resolution.GetEvents()[2].GetTargets()[0].TrainerId == TrainerId
			&& Resolution.GetEvents()[2].GetTargets()[0].ActiveSlotId == ActorSlot);
	TestEqual(TEXT("Actor invalidation consumes no PP"),
		ObserveActionStartCheckpoint(*Engine, ActorId, TrainerId).TotalMovePP,
		Before.TotalMovePP);
	TestEqual(TEXT("Actor invalidation consumes no RNG"),
		State.Random->GetTrace().Num(), Before.RandomTraceCount);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E1ActionStartCapturedTargetTest,
	"PokemonSolarus.Battle.ADR0002.3E1.ActionStart.Success.CapturedTargetCancellation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E1ActionStartCapturedTargetTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	FAtomicWildScenario Scenario = MakeAtomicCaptureScenario();
	Scenario.Format = EBattleFormat::Double;
	Scenario.PlayerLeftSpeed = 160;
	Scenario.PlayerRightSpeed = 150;
	Scenario.OpponentLeftSpeed = 100;
	Scenario.OpponentRightSpeed = 4;
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerRightValue);
	const FTrainerId TrainerId = MakeNumericId<FTrainerId>(PlayerTrainerValue);
	const FBattlerId TargetId = MakeNumericId<FBattlerId>(OpponentLeftValue);
	TUniquePtr<FBattleEngine> Engine;
	if (!TestTrue(TEXT("Captured-target engine is created"),
		TryMakeSequenceEngine(Scenario, {0, 0, 0, 0}, Engine))
		|| !TestTrue(TEXT("Capture-before-selected-target turn is locked"),
			LockCaptureThenTargetTurn(*Engine))
		|| !TestTrue(TEXT("The faster Capture action starts first"),
			BeginExpectedWildAction(
				*Engine,
				PlayerLeftValue,
				EBattleActionKind::Bag)))
	{
		return false;
	}
	const FBattleResolution Capture = Engine->ExecuteCurrentBagItem();
	const FBattleEngineState& CapturedState =
		FBattleC09BWildFlowEngineFixture::GetState(*Engine);
	const FBattleBattlerState* CapturedBeforeStart = CapturedState.FindBattler(TargetId);
	if (!TestTrue(TEXT("The first action successfully captures its target"),
		Capture.WasAccepted()
			&& HasEvent(Capture, EBattleEventType::Captured)
			&& CapturedBeforeStart != nullptr
			&& CapturedBeforeStart->bCaptured
			&& CapturedBeforeStart->bRemoved
			&& !FBattleC09BWildFlowEngineFixture::IsActive(*Engine, TargetId))
		|| !TestTrue(TEXT("The next locked action retains the captured selected target"),
			CapturedState.LockedActions.IsValidIndex(
				CapturedState.CurrentLockedActionIndex)
				&& CapturedState.LockedActions[CapturedState.CurrentLockedActionIndex]
					.Decision.GetActingBattlerId() == ActorId
				&& CapturedState.LockedActions[CapturedState.CurrentLockedActionIndex]
					.SelectedTargetBattlerId == TargetId))
	{
		return false;
	}
	const FActionStartCheckpointObservation Before =
		ObserveActionStartCheckpoint(*Engine, ActorId, TrainerId);
	const FBattleResolution Resolution = Engine->BeginNextLockedAction();
	const FBattleEngineState& State =
		FBattleC09BWildFlowEngineFixture::GetState(*Engine);
	const FBattleTrainerState* Trainer = State.FindTrainer(TrainerId);
	const FBattleBattlerState* CapturedTarget = State.FindBattler(TargetId);

	TestTrue(TEXT("Captured target produces an accepted cancellation"),
		Resolution.WasAccepted());
	TestTrue(TEXT("Captured-target cancellation keeps exact event order"),
		HasExactEventOrder(Resolution, {
			EBattleEventType::ActionCanceled,
			EBattleEventType::ActionCompleted}));
	TestTrue(TEXT("Captured-target cancellation returns the exact single append"),
		IsReturnedResolutionAppendedExactlyOnce(*Engine, Resolution));
	TestEqual(TEXT("Captured-target cancellation increments state version once"),
		State.StateVersion, Before.StateVersion + 1);
	TestEqual(TEXT("Captured-target cancellation advances one queue action"),
		State.CurrentLockedActionIndex, Before.ActionIndex + 1);
	TestEqual(TEXT("Captured-target cancellation remains Resolving"),
		State.Phase, EBattlePhase::Resolving);
	TestTrue(TEXT("Captured-target cancellation finishes without starting"),
		State.LockedActions.IsValidIndex(Before.ActionIndex)
			&& !State.LockedActions[Before.ActionIndex].bStarted
			&& State.LockedActions[Before.ActionIndex].bFinished
			&& !State.LockedActions[Before.ActionIndex].bMoveCommitted);
	TestEqual(TEXT("Captured-target cancellation consumes one Trainer action"),
		Trainer != nullptr ? Trainer->ActionAllowance.RemainingActions : INDEX_NONE,
		Before.RemainingActions - 1);
	TestTrue(TEXT("Captured-target facts remain captured, removed, and inactive"),
		CapturedTarget != nullptr
			&& CapturedTarget->bCaptured
			&& CapturedTarget->bRemoved
			&& !FBattleC09BWildFlowEngineFixture::IsActive(*Engine, TargetId));
	TestEqual(TEXT("Captured-target cancellation consumes no PP"),
		ObserveActionStartCheckpoint(*Engine, ActorId, TrainerId).TotalMovePP,
		Before.TotalMovePP);
	TestEqual(TEXT("Captured-target cancellation consumes no RNG"),
		State.Random->GetTrace().Num(), Before.RandomTraceCount);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E1ActionStartPreparationFailureTest,
	"PokemonSolarus.Battle.ADR0002.3E1.ActionStart.Failure.PreparationBeforePublication",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E1ActionStartPreparationFailureTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	FAtomicWildScenario Scenario;
	Scenario.PlayerLeftSpeed = 150;
	Scenario.bPlayerHasCanonicalHeldItem = true;
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	const FTrainerId TrainerId = MakeNumericId<FTrainerId>(PlayerTrainerValue);
	TUniquePtr<FBattleEngine> Engine;
	if (!TestTrue(TEXT("Preparation-failure engine is created"),
		TryMakeSequenceEngine(Scenario, {}, Engine))
		|| !TestTrue(TEXT("Preparation-failure action is locked first"),
			LockTurn(*Engine, PlayerLeftValue, EBattleActionKind::Fight))
		|| !TestTrue(TEXT("Preparation-failure Magic Room is seeded"),
			TrySeedActionStartMagicRoom(*Engine, ActorId)))
	{
		return false;
	}
	FBattleEngineState& MutableState =
		FBattleC09BWildFlowEngineFixture::GetMutableState(*Engine);
	MutableState.NextTriggerReentrancyToken = TNumericLimits<uint64>::Max() - 1;
	const FActionStartCheckpointObservation Before =
		ObserveActionStartCheckpoint(*Engine, ActorId, TrainerId);
	const FBattleResolution Rejected = Engine->BeginNextLockedAction();
	const bool bRejectedWithoutDelta = VerifyRejectedActionStartCheckpoint(
		*this,
		*Engine,
		ActorId,
		TrainerId,
		Before,
		Before.StateVersion,
		EBattleRejectionReason::CheckpointPreparationFailed,
		Rejected);
	const FBattleEngineState& State =
		FBattleC09BWildFlowEngineFixture::GetState(*Engine);
	const FBattleBattlerState* Actor = State.FindBattler(ActorId);
	TestTrue(TEXT("Preparation failure occurs after staged Magic Room dispatch"),
		Before.NextTriggerToken == TNumericLimits<uint64>::Max() - 1
			&& CountActionStartTriggerRegistrations(
				State,
				FBattleFieldSideConditionRules::GetMagicRoomId().GetDefinitionId()) > 0);
	TestTrue(TEXT("Preparation failure publishes no held-item suppression"),
		Actor != nullptr
			&& Actor->HeldItem.CurrentItemId == FBattleItemRules::GetLeftoversId()
			&& !Actor->HeldItem.bSuppressed
			&& !Actor->HeldItem.bConsumed);
	return bRejectedWithoutDelta;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E1ActionStartStaleIdentityTest,
	"PokemonSolarus.Battle.ADR0002.3E1.ActionStart.Failure.StaleIdentityAfterPreparation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E1ActionStartStaleIdentityTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	FAtomicWildScenario Scenario;
	Scenario.PlayerLeftSpeed = 150;
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	const FTrainerId TrainerId = MakeNumericId<FTrainerId>(PlayerTrainerValue);
	TUniquePtr<FBattleEngine> Engine;
	FActionStartStaleRandom* Random = nullptr;
	if (!TestTrue(TEXT("Stale-identity engine is created"),
		TryMakeActionStartStaleEngine(Scenario, Engine, Random))
		|| !TestNotNull(TEXT("Stale-identity random seam is retained"), Random)
		|| !TestTrue(TEXT("Stale-identity action is locked first"),
			LockTurn(*Engine, PlayerLeftValue, EBattleActionKind::Fight)))
	{
		return false;
	}
	const FActionStartCheckpointObservation Before =
		ObserveActionStartCheckpoint(*Engine, ActorId, TrainerId);
	Random->ArmAfterTraceRead(
		7,
		[EnginePtr = Engine.Get()]()
		{
			FBattleC09BWildFlowEngineFixture::AdvanceStateVersion(*EnginePtr);
		});
	const FBattleResolution Rejected = Engine->BeginNextLockedAction();
	const int32 TraceReads = Random->GetReadsSinceArm();
	const bool bInjected = Random->WasInjected();
	Random->Disarm();
	TestTrue(TEXT("Stale identity is injected at the final post-plan recheck"),
		bInjected && TraceReads == 7);
	return VerifyRejectedActionStartCheckpoint(
		*this,
		*Engine,
		ActorId,
		TrainerId,
		Before,
		Before.StateVersion + 1,
		EBattleRejectionReason::StaleCheckpointIdentity,
		Rejected);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E1ActionStartPlanFailureTest,
	"PokemonSolarus.Battle.ADR0002.3E1.ActionStart.Failure.ResolutionPlanStaging",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E1ActionStartPlanFailureTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	FAtomicWildScenario Scenario;
	Scenario.PlayerLeftSpeed = 150;
	Scenario.bPlayerSubjectToObedience = true;
	Scenario.PlayerReferenceLevel = 21;
	Scenario.PlayerBadgeCount = 0;
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	const FTrainerId TrainerId = MakeNumericId<FTrainerId>(PlayerTrainerValue);
	TUniquePtr<FBattleEngine> Engine;
	if (!TestTrue(TEXT("Plan-failure engine is created"),
		TryMakeSequenceEngine(Scenario, {}, Engine))
		|| !TestTrue(TEXT("Plan-failure refusal action is locked first"),
			LockTurn(*Engine, PlayerLeftValue, EBattleActionKind::Fight)))
	{
		return false;
	}
	FBattleEngineState& MutableState =
		FBattleC09BWildFlowEngineFixture::GetMutableState(*Engine);
	MutableState.NextEventOrdinal = TNumericLimits<uint64>::Max() - 2;
	const FActionStartCheckpointObservation Before =
		ObserveActionStartCheckpoint(*Engine, ActorId, TrainerId);
	const FBattleResolution Rejected = Engine->BeginNextLockedAction();
	TestEqual(TEXT("Plan failure starts at the bounded near-overflow ordinal"),
		Before.NextEventOrdinal, TNumericLimits<uint64>::Max() - 2);
	return VerifyRejectedActionStartCheckpoint(
		*this,
		*Engine,
		ActorId,
		TrainerId,
		Before,
		Before.StateVersion,
		EBattleRejectionReason::CheckpointPreparationFailed,
		Rejected);
}

} // namespace BattleAtomicActionStartTestsPrivate

#endif
