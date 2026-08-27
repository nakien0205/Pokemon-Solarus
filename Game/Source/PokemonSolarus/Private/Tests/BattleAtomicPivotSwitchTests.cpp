#if WITH_DEV_AUTOMATION_TESTS

#include "BattleAtomicCheckpointTestCommon.h"
#include "BattleAtomicCheckpointTestFaults.h"
#include "BattleAtomicSwitchTestSupport.h"

namespace BattleAtomicPivotSwitchTestsPrivate
{
	using namespace BattleAtomicCheckpointTestCommonPrivate;
	using namespace BattleAtomicCheckpointTestFaultsPrivate;
	using namespace BattleAtomicSwitchTestSupportPrivate;

bool TryMakePivotSwitchDecision(
		const FBattleDecisionRequest& Request,
		const FPartySlotId PartySlotId,
		FBattleDecision& OutDecision)
	{
		return FBattleDecision::TryCreateSwitch(
			Request.GetStateVersion(),
			EBattleDecisionRequestKind::PivotSwitch,
			Request.GetDecisionOwnerTrainerId(),
			Request.GetActingBattlerId(),
			PartySlotId,
			Request.GetActingSlotId(),
			OutDecision);
	}

bool TryPrepareAtomicPivotSwitch(
		FBattleEngine& Engine,
		FBattleDecisionRequest& OutRequest)
	{
		if (!LockTurn(
				Engine,
				PlayerLeftValue,
				EBattleActionKind::Fight,
				MakeDefinitionId<FMoveId>(PivotProbeMoveName)))
		{
			return false;
		}
		int32 Guard = 0;
		while (Guard++ < 4)
		{
			if (!Engine.BeginNextLockedAction().WasAccepted())
			{
				return false;
			}
			const TOptional<FBattleLockedAction> Current = Engine.GetCurrentLockedAction();
			if (!Current.IsSet()
				|| Current->Decision.GetActionKind() != EBattleActionKind::Fight)
			{
				return false;
			}
			if (Current->Decision.GetActingBattlerId()
					== MakeNumericId<FBattlerId>(PlayerLeftValue)
				&& Current->Decision.GetMoveId()
					== MakeDefinitionId<FMoveId>(PivotProbeMoveName))
			{
				break;
			}
			if (!Engine.CommitCurrentMoveAfterPreMoveGates().WasAccepted()
				|| !Engine.ResolveCurrentMoveTargets().WasAccepted()
				|| !Engine.ExecuteCurrentMoveEffects().WasAccepted())
			{
				return false;
			}
		}
		const TOptional<FBattleLockedAction> PivotAction = Engine.GetCurrentLockedAction();
		if (Guard > 4
			|| !PivotAction.IsSet()
			|| PivotAction->Decision.GetMoveId()
				!= MakeDefinitionId<FMoveId>(PivotProbeMoveName)
			|| !Engine.CommitCurrentMoveAfterPreMoveGates().WasAccepted()
			|| !Engine.ResolveCurrentMoveTargets().WasAccepted())
		{
			return false;
		}
		const FBattleResolution Effects = Engine.ExecuteCurrentMoveEffects();
		const FBattleEngineState& State =
			FBattleC09BWildFlowEngineFixture::GetState(Engine);
		if (!Effects.WasAccepted()
			|| Effects.GetEvents().ContainsByPredicate(
				[](const FBattleEvent& Event)
				{
					return Event.GetType() == EBattleEventType::ActionCompleted;
				})
			|| State.Phase != EBattlePhase::Resolving
			|| !State.LockedActions.IsValidIndex(State.CurrentLockedActionIndex)
			|| State.PendingDecisionRequests.Num() != 1
			|| !State.PendingDecision.IsSet())
		{
			return false;
		}
		const FBattleLockedActionState& Action =
			State.LockedActions[State.CurrentLockedActionIndex];
		const FBattleDecisionRequest& Request = State.PendingDecisionRequests[0];
		if (Action.Decision.GetActionKind() != EBattleActionKind::Fight
			|| Action.Decision.GetMoveId() != MakeDefinitionId<FMoveId>(PivotProbeMoveName)
			|| !Action.bStarted
			|| !Action.bMoveCommitted
			|| !Action.TargetResolution.IsSet()
			|| Action.EffectExecutionState
				!= EBattleLockedEffectExecutionState::AwaitingPivot
			|| Action.bFinished
			|| Request.GetRequestKind() != EBattleDecisionRequestKind::PivotSwitch
			|| Request.GetActingBattlerId()
				!= MakeNumericId<FBattlerId>(PlayerLeftValue)
			|| Request.GetActingSlotId()
				!= MakeActiveSlotId(EBattleSide::Player, EBattlePosition::Left))
		{
			return false;
		}
		OutRequest = Request;
		return true;
	}

bool TryCopyPivotRequestWithSwitchSlot(
		const FBattleDecisionRequest& Source,
		const FPartySlotId PartySlotId,
		FBattleDecisionRequest& OutRequest)
	{
		FBattleDecisionRequestSpec Spec;
		Spec.StateVersion = Source.GetStateVersion();
		Spec.RequestKind = Source.GetRequestKind();
		Spec.DecisionOwnerTrainerId = Source.GetDecisionOwnerTrainerId();
		Spec.ActingBattlerId = Source.GetActingBattlerId();
		Spec.ActingSlotId = Source.GetActingSlotId();
		Spec.LegalActionKinds.Append(Source.GetLegalActionKinds());
		Spec.LegalMoveIds.Append(Source.GetLegalMoveIds());
		Spec.AutomaticallyTargetedMoveIds.Append(Source.GetAutomaticallyTargetedMoveIds());
		Spec.LegalSwitchPartySlots.Add(PartySlotId);
		Spec.LegalItemIds.Append(Source.GetLegalItemIds());
		Spec.LegalActiveTargets.Append(Source.GetLegalActiveTargets());
		Spec.LegalPartyTargets.Append(Source.GetLegalPartyTargets());
		Spec.LegalMoveTargets.Append(Source.GetLegalMoveTargets());
		Spec.LegalItemPartyTargets.Append(Source.GetLegalItemPartyTargets());
		Spec.LegalItemActiveTargets.Append(Source.GetLegalItemActiveTargets());
		Spec.UnavailableOptions.Append(Source.GetUnavailableOptions());
		FBattleRejection Rejection;
		return FBattleDecisionRequest::TryCreate(Spec, OutRequest, Rejection);
	}

struct FAtomicPivotSwitchObservation
	{
		FAtomicSwitchCheckpointObservation Mechanics;
		int32 LockedActionCount = 0;
		int32 SubmittedDecisionCount = 0;
		int32 RemainingActions = INDEX_NONE;
		int32 ActionStartedEventCount = 0;
		bool bHasCurrentAction = false;
		bool bHasPendingDecision = false;
		bool bHasPendingRequest = false;
		bool bHasLastSubmittedDecision = false;
		FBattleLockedActionState CurrentAction;
		FBattleDecisionRequest PendingDecision;
		FBattleDecisionRequest PendingRequest;
		FBattleDecision LastSubmittedDecision;
	};

FAtomicPivotSwitchObservation ObserveAtomicPivotSwitchCheckpoint(
		const FBattleEngine& Engine)
	{
		const FBattleEngineState& State =
			FBattleC09BWildFlowEngineFixture::GetState(Engine);
		FAtomicPivotSwitchObservation Observation;
		Observation.Mechanics = ObserveAtomicSwitchCheckpoint(Engine);
		Observation.LockedActionCount = State.LockedActions.Num();
		Observation.SubmittedDecisionCount = State.SubmittedDecisions.Num();
		if (State.LockedActions.IsValidIndex(State.CurrentLockedActionIndex))
		{
			Observation.bHasCurrentAction = true;
			Observation.CurrentAction = State.LockedActions[State.CurrentLockedActionIndex];
		}
		if (State.PendingDecision.IsSet())
		{
			Observation.bHasPendingDecision = true;
			Observation.PendingDecision = State.PendingDecision.GetValue();
		}
		if (!State.PendingDecisionRequests.IsEmpty())
		{
			Observation.bHasPendingRequest = true;
			Observation.PendingRequest = State.PendingDecisionRequests[0];
		}
		if (!State.SubmittedDecisions.IsEmpty())
		{
			Observation.bHasLastSubmittedDecision = true;
			Observation.LastSubmittedDecision = State.SubmittedDecisions.Last();
		}
		const FBattleTrainerState* Trainer = State.FindTrainer(
			MakeNumericId<FTrainerId>(PlayerTrainerValue));
		if (Trainer != nullptr)
		{
			Observation.RemainingActions = Trainer->ActionAllowance.RemainingActions;
		}
		for (const FBattleEvent& Event : State.OrderedEvents)
		{
			Observation.ActionStartedEventCount +=
				Event.GetType() == EBattleEventType::ActionStarted ? 1 : 0;
		}
		return Observation;
	}

bool AreAtomicPivotSwitchGameplayFactsIdentical(
		const FAtomicPivotSwitchObservation& Left,
		const FAtomicPivotSwitchObservation& Right)
	{
		return AreAtomicSwitchMechanicsIdentical(Left.Mechanics, Right.Mechanics)
			&& Left.LockedActionCount == Right.LockedActionCount
			&& Left.RemainingActions == Right.RemainingActions
			&& Left.ActionStartedEventCount == Right.ActionStartedEventCount
			&& Left.bHasCurrentAction == Right.bHasCurrentAction
			&& (!Left.bHasCurrentAction
				|| ArePivotTestLockedActionsIdentical(
					Left.CurrentAction,
					Right.CurrentAction))
			&& Left.bHasPendingDecision == Right.bHasPendingDecision
			&& (!Left.bHasPendingDecision
				|| ArePivotTestRequestsIdentical(
					Left.PendingDecision,
					Right.PendingDecision))
			&& Left.bHasPendingRequest == Right.bHasPendingRequest
			&& (!Left.bHasPendingRequest
				|| ArePivotTestRequestsIdentical(
					Left.PendingRequest,
					Right.PendingRequest));
	}

bool VerifyRejectedAtomicPivotSwitch(
		FAutomationTestBase& Test,
		const FBattleEngine& Engine,
		const FAtomicPivotSwitchObservation& Before,
		const FAtomicPivotSwitchObservation& ExpectedGameplay,
		const FBattleDecision& ExpectedSubmittedResponse,
		const EBattleRejectionReason ExpectedReason,
		const FBattleResolution& Returned)
	{
		const FAtomicPivotSwitchObservation After =
			ObserveAtomicPivotSwitchCheckpoint(Engine);
		bool bValid = true;
		bValid &= Test.TestFalse(TEXT("Pivot checkpoint failure is rejected"),
			Returned.WasAccepted());
		bValid &= Test.TestEqual(TEXT("Pivot rejection is typed"),
			Returned.GetRejection().Reason,
			ExpectedReason);
		bValid &= Test.TestTrue(TEXT("Rejected Pivot is returned and appended exactly once"),
			IsReturnedResolutionAppendedExactlyOnce(Engine, Returned));
		bValid &= Test.TestTrue(TEXT("Rejected Pivot publishes one Fight cancellation only"),
			Returned.GetEvents().Num() == 1
				&& Returned.GetEvents()[0].GetType() == EBattleEventType::ActionCanceled
				&& Returned.GetEvents()[0].GetCause() == EBattleEventCause::Rule
				&& Returned.GetEvents()[0].GetCauseActionKind()
					== EBattleActionKind::Fight);
		bValid &= Test.TestEqual(TEXT("Rejected Pivot appends one resolution"),
			After.Mechanics.ResolutionCount,
			Before.Mechanics.ResolutionCount + 1);
		bValid &= Test.TestEqual(TEXT("Rejected Pivot appends one event"),
			After.Mechanics.EventCount,
			Before.Mechanics.EventCount + 1);
		bValid &= Test.TestEqual(TEXT("Rejected Pivot consumes one resolution id"),
			After.Mechanics.NextResolutionId,
			Before.Mechanics.NextResolutionId + 1);
		bValid &= Test.TestEqual(TEXT("Rejected Pivot consumes one event ordinal"),
			After.Mechanics.NextEventOrdinal,
			Before.Mechanics.NextEventOrdinal + 1);
		bValid &= Test.TestEqual(TEXT("Rejected Pivot has no accepted version delta"),
			After.Mechanics.StateVersion,
			ExpectedGameplay.Mechanics.StateVersion);
		bValid &= Test.TestTrue(TEXT("Rejected Pivot preserves every expected gameplay fact"),
			AreAtomicPivotSwitchGameplayFactsIdentical(After, ExpectedGameplay));
		bValid &= Test.TestTrue(TEXT("Rejected Pivot preserves AwaitingPivot Fight continuation"),
			After.bHasCurrentAction
				&& After.CurrentAction.Decision.GetActionKind() == EBattleActionKind::Fight
				&& After.CurrentAction.EffectExecutionState
					== EBattleLockedEffectExecutionState::AwaitingPivot
				&& !After.CurrentAction.bFinished
				&& After.Mechanics.ActionIndex == ExpectedGameplay.Mechanics.ActionIndex
				&& After.bHasPendingDecision
				&& After.bHasPendingRequest
				&& ArePivotTestRequestsIdentical(
					After.PendingDecision,
					ExpectedGameplay.PendingDecision)
				&& ArePivotTestRequestsIdentical(
					After.PendingRequest,
					ExpectedGameplay.PendingRequest));
		bValid &= Test.TestEqual(TEXT("Submitted Pivot response remains replay input"),
			After.SubmittedDecisionCount,
			Before.SubmittedDecisionCount + 1);
		bValid &= Test.TestTrue(TEXT("Submitted Pivot response remains exact"),
			After.bHasLastSubmittedDecision
				&& ArePivotTestDecisionsIdentical(
					After.LastSubmittedDecision,
					ExpectedSubmittedResponse));
		bValid &= Test.TestEqual(TEXT("Rejected Pivot consumes no gameplay RNG"),
			After.Mechanics.RandomTraceCount,
			ExpectedGameplay.Mechanics.RandomTraceCount);
		const FBattleReplayRecord Replay = Engine.ExportReplayRecord();
		bValid &= Test.TestEqual(TEXT("Rejected Pivot replay schema is exactly 6"),
			Replay.GetSchemaVersion(),
			static_cast<uint32>(6));
		bValid &= Test.TestTrue(TEXT("Rejected Pivot replay keeps the submitted response input"),
			Replay.GetInputs().Decisions.Num() == After.SubmittedDecisionCount
				&& !Replay.GetInputs().Decisions.IsEmpty()
				&& ArePivotTestDecisionsIdentical(
					Replay.GetInputs().Decisions.Last(),
					ExpectedSubmittedResponse));
		bValid &= Test.TestTrue(TEXT("Rejected Pivot replay contains no checkpoint success fact"),
			!Replay.GetResolutions().IsEmpty()
				&& Replay.GetResolutions().Last().GetResolutionId()
					== Returned.GetResolutionId()
				&& !Replay.GetResolutions().Last().WasAccepted()
				&& Replay.GetResolutions().Last().GetRejection().Reason == ExpectedReason
				&& Replay.GetResolutions().Last().GetEvents().Num() == 1
				&& Replay.GetResolutions().Last().GetEvents()[0].GetType()
					== EBattleEventType::ActionCanceled);
		return bValid;
	}

bool RunAtomicPivotSwitchFailureFamily(
		FAutomationTestBase& Test,
		const EAtomicSwitchFailureFamily Family)
	{
		FItemId IncomingItemId;
		FAbilityId IncomingAbilityId = FBattleAbilityRules::GetBlazeId();
		if (Family == EAtomicSwitchFailureFamily::EntryItemReveal)
		{
			IncomingItemId = FBattleItemRules::GetAirBalloonId();
		}
		else if (Family == EAtomicSwitchFailureFamily::ImmediateHeldItem)
		{
			IncomingItemId = FBattleItemRules::GetSitrusBerryId();
		}
		else if (Family == EAtomicSwitchFailureFamily::EntryAbility)
		{
			IncomingAbilityId = FBattleAbilityRules::GetIntimidateId();
		}
		TUniquePtr<FBattleEngine> Engine;
		if (!Test.TestTrue(TEXT("Failure-family Pivot engine is created"),
				TryMakeSequenceEngine(
					MakeAtomicPivotSwitchScenario(
						IncomingItemId,
						IncomingAbilityId,
						Family == EAtomicSwitchFailureFamily::ImmediateHeldItem ? 90 : 200),
					{},
					Engine))
			|| !Test.TestTrue(TEXT("Failure-family Pivot outgoing transients are seeded"),
				TrySeedAtomicSwitchOutgoingTransients(*Engine)))
		{
			return false;
		}
		if (Family == EAtomicSwitchFailureFamily::EntryHazard
			&& !Test.TestTrue(TEXT("Failure-family Pivot hazard is seeded"),
				TrySeedAtomicSwitchHazard(
					*Engine,
					FBattleFieldSideConditionRules::GetSpikesId())))
		{
			return false;
		}
		FBattleDecisionRequest Request;
		FBattleDecision Response;
		if (!Test.TestTrue(TEXT("Failure-family Pivot reaches AwaitingPivot"),
				TryPrepareAtomicPivotSwitch(*Engine, Request))
			|| !Test.TestTrue(TEXT("Failure-family Pivot response is created"),
				TryMakePivotSwitchDecision(Request, MakePartySlotId(1), Response)))
		{
			return false;
		}
		FBattleEngineState& MutableState =
			FBattleC09BWildFlowEngineFixture::GetMutableState(*Engine);
		MutableState.NextTriggerReentrancyToken = TNumericLimits<uint64>::Max() - 1;
		const FAtomicPivotSwitchObservation Before =
			ObserveAtomicPivotSwitchCheckpoint(*Engine);
		const FBattleResolution Rejected = Engine->SubmitDecision(Response);
		return VerifyRejectedAtomicPivotSwitch(
			Test,
			*Engine,
			Before,
			Before,
			Response,
			EBattleRejectionReason::CheckpointPreparationFailed,
			Rejected);
	}

bool TryReplaceAtomicPivotPendingRequest(
		FBattleEngine& Engine,
		const FPartySlotId PartySlotId)
	{
		FBattleEngineState& State =
			FBattleC09BWildFlowEngineFixture::GetMutableState(Engine);
		if (!State.PendingDecision.IsSet() || State.PendingDecisionRequests.Num() != 1)
		{
			return false;
		}
		FBattleDecisionRequest Replacement;
		if (!TryCopyPivotRequestWithSwitchSlot(
				State.PendingDecision.GetValue(),
				PartySlotId,
				Replacement))
		{
			return false;
		}
		State.PendingDecision = Replacement;
		State.PendingDecisionRequests[0] = MoveTemp(Replacement);
		return true;
	}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E3PivotSwitchFullEntryChainTest,
	"PokemonSolarus.Battle.ADR0002.3E3.PivotSwitch.Success.FullEntryChain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E3PivotSwitchFullEntryChainTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	TUniquePtr<FBattleEngine> Engine;
	if (!TestTrue(TEXT("Full-chain Pivot engine is created"),
			TryMakeSequenceEngine(
				MakeAtomicPivotSwitchScenario(
					FBattleItemRules::GetSitrusBerryId(),
					FBattleAbilityRules::GetIntimidateId(),
					110),
				{},
				Engine))
		|| !TestTrue(TEXT("Full-chain Pivot outgoing transients are seeded"),
			TrySeedAtomicSwitchOutgoingTransients(*Engine))
		|| !TestTrue(TEXT("Full-chain Pivot Stealth Rock is seeded"),
			TrySeedAtomicSwitchHazard(
				*Engine,
				FBattleFieldSideConditionRules::GetStealthRockId())))
	{
		return false;
	}
	FBattleDecisionRequest Request;
	FBattleDecision Response;
	if (!TestTrue(TEXT("Full-chain Fight reaches AwaitingPivot"),
			TryPrepareAtomicPivotSwitch(*Engine, Request))
		|| !TestTrue(TEXT("Full-chain Pivot response is created"),
			TryMakePivotSwitchDecision(Request, MakePartySlotId(1), Response)))
	{
		return false;
	}
	const FAtomicPivotSwitchObservation Before =
		ObserveAtomicPivotSwitchCheckpoint(*Engine);
	const FBattleResolution Resolution = Engine->SubmitDecision(Response);
	const FBattleEngineState& State =
		FBattleC09BWildFlowEngineFixture::GetState(*Engine);
	const FAtomicPivotSwitchObservation After =
		ObserveAtomicPivotSwitchCheckpoint(*Engine);
	const FBattleBattlerState* Outgoing = State.FindBattler(
		MakeNumericId<FBattlerId>(PlayerLeftValue));
	const FBattleBattlerState* Incoming = State.FindBattler(
		MakeNumericId<FBattlerId>(PlayerReserveValue));
	const FBattleBattlerState* Opponent = State.FindBattler(
		MakeNumericId<FBattlerId>(OpponentLeftValue));
	const FBattleActivePositionState* PlayerActive = State.FindActivePosition(
		MakeActiveSlotId(EBattleSide::Player, EBattlePosition::Left));
	int32 OpponentAttackStage = 0;
	const bool bOpponentStageRead = Opponent != nullptr
		&& Opponent->Stages.TryGetStage(EBattleStat::Attack, OpponentAttackStage);
	int32 OutgoingAttackStage = 1;

	TestTrue(TEXT("Full entry-chain Pivot is accepted"), Resolution.WasAccepted());
	TestTrue(TEXT("Full entry-chain Pivot keeps exact event order"),
		HasExactEventOrder(
			Resolution,
			{
				EBattleEventType::LeftActiveSlot,
				EBattleEventType::SwitchTransientStateCleared,
				EBattleEventType::EnteredActiveSlot,
				EBattleEventType::Switched,
				EBattleEventType::Damage,
				EBattleEventType::HPChanged,
				EBattleEventType::ItemActivated,
				EBattleEventType::ItemConsumed,
				EBattleEventType::Healing,
				EBattleEventType::HPChanged,
				EBattleEventType::AbilityActivated,
				EBattleEventType::StatStageChanged,
				EBattleEventType::ActionCompleted
			}));
	TestTrue(TEXT("Full entry-chain Pivot is appended exactly once"),
		IsReturnedResolutionAppendedExactlyOnce(*Engine, Resolution));
	TestEqual(TEXT("Full entry-chain Pivot advances one version"),
		State.StateVersion, Before.Mechanics.StateVersion + 1);
	TestEqual(TEXT("Full entry-chain Pivot consumes no RNG"),
		State.Random->GetTrace().Num(), Before.Mechanics.RandomTraceCount);
	TestTrue(TEXT("Full entry-chain Pivot cleans and deactivates the outgoing battler"),
		Outgoing != nullptr
			&& PlayerActive != nullptr
			&& PlayerActive->BattlerId == MakeNumericId<FBattlerId>(PlayerReserveValue)
			&& Outgoing->Volatiles.IsEmpty()
			&& !Outgoing->LastMoveId.IsValid()
			&& !Outgoing->bAbilitySuppressed
			&& !Outgoing->EnteredActiveOnTurnId.IsValid()
			&& Outgoing->Stages.TryGetStage(EBattleStat::Attack, OutgoingAttackStage)
			&& OutgoingAttackStage == 0);
	TestTrue(TEXT("Full entry-chain Pivot commits hazard, item, and Ability effects"),
		Incoming != nullptr
			&& Incoming->CurrentHP == 135
			&& Incoming->HeldItem.bConsumed
			&& !Incoming->HeldItem.CurrentItemId.IsValid()
			&& Incoming->HeldItem.bRevealed
			&& Incoming->EnteredActiveOnTurnId == State.TurnId
			&& bOpponentStageRead
			&& OpponentAttackStage == -1
			&& IsAtomicSwitchDefinitionRevealed(
				State,
				MakeNumericId<FBattlerId>(PlayerReserveValue),
				true));
	TestTrue(TEXT("Pivot completes the same Fight action without another start or cost"),
		Before.bHasCurrentAction
			&& State.LockedActions.IsValidIndex(Before.Mechanics.ActionIndex)
			&& State.LockedActions[Before.Mechanics.ActionIndex].ActionId
				== Before.CurrentAction.ActionId
			&& State.LockedActions[Before.Mechanics.ActionIndex].Decision.GetActionKind()
				== EBattleActionKind::Fight
			&& State.LockedActions[Before.Mechanics.ActionIndex].EffectExecutionState
				== EBattleLockedEffectExecutionState::Completed
			&& State.LockedActions[Before.Mechanics.ActionIndex].bFinished
			&& After.ActionStartedEventCount == Before.ActionStartedEventCount
			&& After.RemainingActions == Before.RemainingActions);
	TestTrue(TEXT("Full entry-chain Pivot clears its request and advances its cursor"),
		State.Phase == EBattlePhase::Resolving
			&& State.CurrentLockedActionIndex == Before.Mechanics.ActionIndex + 1
			&& !State.PendingDecision.IsSet()
			&& State.PendingDecisionRequests.IsEmpty());
	TestTrue(TEXT("Successful Pivot response is retained as exact replay input"),
		After.SubmittedDecisionCount == Before.SubmittedDecisionCount + 1
			&& After.bHasLastSubmittedDecision
			&& ArePivotTestDecisionsIdentical(After.LastSubmittedDecision, Response));
	const FBattleReplayRecord Replay = Engine->ExportReplayRecord();
	TestTrue(TEXT("Successful Pivot replay keeps schema 6 and exact response"),
		Replay.GetSchemaVersion() == 6
			&& Replay.GetInputs().Decisions.Num() == After.SubmittedDecisionCount
			&& !Replay.GetInputs().Decisions.IsEmpty()
			&& ArePivotTestDecisionsIdentical(
				Replay.GetInputs().Decisions.Last(),
				Response));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E3PivotSwitchAirBalloonTest,
	"PokemonSolarus.Battle.ADR0002.3E3.PivotSwitch.Success.AirBalloonReveal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E3PivotSwitchAirBalloonTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	TUniquePtr<FBattleEngine> Engine;
	if (!TestTrue(TEXT("Air Balloon Pivot engine is created"),
			TryMakeSequenceEngine(
				MakeAtomicPivotSwitchScenario(FBattleItemRules::GetAirBalloonId()),
				{},
				Engine)))
	{
		return false;
	}
	FBattleDecisionRequest Request;
	FBattleDecision Response;
	if (!TestTrue(TEXT("Air Balloon Fight reaches AwaitingPivot"),
			TryPrepareAtomicPivotSwitch(*Engine, Request))
		|| !TestTrue(TEXT("Air Balloon Pivot response is created"),
			TryMakePivotSwitchDecision(Request, MakePartySlotId(1), Response)))
	{
		return false;
	}
	const FAtomicPivotSwitchObservation Before =
		ObserveAtomicPivotSwitchCheckpoint(*Engine);
	const FBattleResolution Resolution = Engine->SubmitDecision(Response);
	const FBattleEngineState& State =
		FBattleC09BWildFlowEngineFixture::GetState(*Engine);
	const FAtomicPivotSwitchObservation After =
		ObserveAtomicPivotSwitchCheckpoint(*Engine);
	const FBattleBattlerState* Incoming = State.FindBattler(
		MakeNumericId<FBattlerId>(PlayerReserveValue));
	TestTrue(TEXT("Air Balloon Pivot is accepted"), Resolution.WasAccepted());
	TestTrue(TEXT("Air Balloon Pivot keeps exact event order"),
		HasExactEventOrder(
			Resolution,
			{
				EBattleEventType::LeftActiveSlot,
				EBattleEventType::SwitchTransientStateCleared,
				EBattleEventType::EnteredActiveSlot,
				EBattleEventType::Switched,
				EBattleEventType::ItemActivated,
				EBattleEventType::ActionCompleted
			}));
	TestTrue(TEXT("Air Balloon Pivot is appended exactly once"),
		IsReturnedResolutionAppendedExactlyOnce(*Engine, Resolution));
	TestTrue(TEXT("Air Balloon Pivot commits mirror and reveal tracker"),
		Incoming != nullptr
			&& Incoming->HeldItem.CurrentItemId == FBattleItemRules::GetAirBalloonId()
			&& Incoming->HeldItem.bRevealed
			&& IsAtomicSwitchDefinitionRevealed(State, Incoming->BattlerId, false));
	TestEqual(TEXT("Air Balloon Pivot advances one version"),
		State.StateVersion, Before.Mechanics.StateVersion + 1);
	TestEqual(TEXT("Air Balloon Pivot consumes no RNG"),
		State.Random->GetTrace().Num(), Before.Mechanics.RandomTraceCount);
	TestTrue(TEXT("Air Balloon Pivot resumes and completes the same Fight action"),
		State.LockedActions.IsValidIndex(Before.Mechanics.ActionIndex)
			&& State.LockedActions[Before.Mechanics.ActionIndex].ActionId
				== Before.CurrentAction.ActionId
			&& State.LockedActions[Before.Mechanics.ActionIndex].bFinished
			&& State.LockedActions[Before.Mechanics.ActionIndex].EffectExecutionState
				== EBattleLockedEffectExecutionState::Completed
			&& After.ActionStartedEventCount == Before.ActionStartedEventCount
			&& After.RemainingActions == Before.RemainingActions
			&& After.SubmittedDecisionCount == Before.SubmittedDecisionCount + 1
			&& ArePivotTestDecisionsIdentical(After.LastSubmittedDecision, Response));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E3PivotSwitchLethalHazardTest,
	"PokemonSolarus.Battle.ADR0002.3E3.PivotSwitch.Success.LethalHazardReplacementBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E3PivotSwitchLethalHazardTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	FAtomicWildScenario Scenario = MakeAtomicPivotSwitchScenario(
		FItemId(),
		FBattleAbilityRules::GetBlazeId(),
		10);
	Scenario.PlayerLeftSpeed = 50;
	Scenario.OpponentLeftSpeed = 150;
	TUniquePtr<FBattleEngine> Engine;
	if (!TestTrue(TEXT("Lethal-hazard Pivot engine is created"),
			TryMakeSequenceEngine(
				Scenario,
				{},
				Engine))
		|| !TestTrue(TEXT("Lethal-hazard Pivot Stealth Rock is seeded"),
			TrySeedAtomicSwitchHazard(
				*Engine,
				FBattleFieldSideConditionRules::GetStealthRockId())))
	{
		return false;
	}
	FBattleDecisionRequest Request;
	FBattleDecision Response;
	if (!TestTrue(TEXT("Lethal-hazard Fight reaches AwaitingPivot"),
			TryPrepareAtomicPivotSwitch(*Engine, Request))
		|| !TestTrue(TEXT("Lethal-hazard Pivot response is created"),
			TryMakePivotSwitchDecision(Request, MakePartySlotId(1), Response)))
	{
		return false;
	}
	const FAtomicPivotSwitchObservation Before =
		ObserveAtomicPivotSwitchCheckpoint(*Engine);
	const FBattleResolution Resolution = Engine->SubmitDecision(Response);
	const FBattleEngineState& State =
		FBattleC09BWildFlowEngineFixture::GetState(*Engine);
	const FAtomicPivotSwitchObservation After =
		ObserveAtomicPivotSwitchCheckpoint(*Engine);
	const FBattleBattlerState* Incoming = State.FindBattler(
		MakeNumericId<FBattlerId>(PlayerReserveValue));
	const FBattleActivePositionState* PlayerActive = State.FindActivePosition(
		MakeActiveSlotId(EBattleSide::Player, EBattlePosition::Left));
	TestTrue(TEXT("Lethal-hazard Pivot is accepted"), Resolution.WasAccepted());
	TestTrue(TEXT("Lethal-hazard Pivot keeps exact event and boundary order"),
		HasExactEventOrder(
			Resolution,
			{
				EBattleEventType::LeftActiveSlot,
				EBattleEventType::SwitchTransientStateCleared,
				EBattleEventType::EnteredActiveSlot,
				EBattleEventType::Switched,
				EBattleEventType::Damage,
				EBattleEventType::HPChanged,
				EBattleEventType::Fainted,
				EBattleEventType::LeftActiveSlot,
				EBattleEventType::Removed,
				EBattleEventType::ActionCompleted,
				EBattleEventType::ReplacementRequired
			}));
	TestTrue(TEXT("Lethal-hazard Pivot is appended exactly once"),
		IsReturnedResolutionAppendedExactlyOnce(*Engine, Resolution));
	TestTrue(TEXT("Lethal-hazard Pivot commits faint and empty-slot facts"),
		Incoming != nullptr
			&& Incoming->CurrentHP == 0
			&& Incoming->bFainted
			&& Incoming->bRemoved
			&& PlayerActive != nullptr
			&& !PlayerActive->BattlerId.IsValid()
			&& !PlayerActive->TrainerId.IsValid());
	TestTrue(TEXT("Lethal-hazard Pivot stages one complete replacement boundary"),
		State.Phase == EBattlePhase::MandatoryReplacement
			&& State.PendingReplacements.Num() == 1
			&& State.PendingDecisionRequests.Num() == 1
			&& State.PendingDecision.IsSet()
			&& State.PendingReplacements[0].TrainerId
				== MakeNumericId<FTrainerId>(PlayerTrainerValue));
	TestTrue(TEXT("Lethal-hazard Pivot completes the same Fight action once"),
		State.LockedActions.IsValidIndex(Before.Mechanics.ActionIndex)
			&& State.LockedActions[Before.Mechanics.ActionIndex].ActionId
				== Before.CurrentAction.ActionId
			&& State.LockedActions[Before.Mechanics.ActionIndex].bFinished
			&& State.LockedActions[Before.Mechanics.ActionIndex].EffectExecutionState
				== EBattleLockedEffectExecutionState::Completed
			&& State.CurrentLockedActionIndex == Before.Mechanics.ActionIndex + 1
			&& After.ActionStartedEventCount == Before.ActionStartedEventCount
			&& After.RemainingActions == Before.RemainingActions);
	TestEqual(TEXT("Lethal-hazard Pivot advances one version"),
		State.StateVersion, Before.Mechanics.StateVersion + 1);
	TestEqual(TEXT("Lethal-hazard Pivot consumes no RNG"),
		State.Random->GetTrace().Num(), Before.Mechanics.RandomTraceCount);
	TestTrue(TEXT("Lethal-hazard Pivot retains its exact response input"),
		After.SubmittedDecisionCount == Before.SubmittedDecisionCount + 1
			&& ArePivotTestDecisionsIdentical(After.LastSubmittedDecision, Response));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E3PivotSwitchEntryItemFailureTest,
	"PokemonSolarus.Battle.ADR0002.3E3.PivotSwitch.Failure.EntryItemReveal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E3PivotSwitchEntryItemFailureTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	return RunAtomicPivotSwitchFailureFamily(
		*this,
		EAtomicSwitchFailureFamily::EntryItemReveal);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E3PivotSwitchEntryHazardFailureTest,
	"PokemonSolarus.Battle.ADR0002.3E3.PivotSwitch.Failure.EntryHazard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E3PivotSwitchEntryHazardFailureTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	return RunAtomicPivotSwitchFailureFamily(
		*this,
		EAtomicSwitchFailureFamily::EntryHazard);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E3PivotSwitchImmediateItemFailureTest,
	"PokemonSolarus.Battle.ADR0002.3E3.PivotSwitch.Failure.ImmediateHeldItem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E3PivotSwitchImmediateItemFailureTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	return RunAtomicPivotSwitchFailureFamily(
		*this,
		EAtomicSwitchFailureFamily::ImmediateHeldItem);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E3PivotSwitchEntryAbilityFailureTest,
	"PokemonSolarus.Battle.ADR0002.3E3.PivotSwitch.Failure.EntryAbility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E3PivotSwitchEntryAbilityFailureTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	return RunAtomicPivotSwitchFailureFamily(
		*this,
		EAtomicSwitchFailureFamily::EntryAbility);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E3PivotSwitchStaleFightActionTest,
	"PokemonSolarus.Battle.ADR0002.3E3.PivotSwitch.Failure.StaleFightActionIdentityBeforeCommit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E3PivotSwitchStaleFightActionTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	TUniquePtr<FBattleEngine> Engine;
	FActionStartStaleRandom* Random = nullptr;
	if (!TestTrue(TEXT("Stale-Fight Pivot engine is created"),
			TryMakeActionStartStaleEngine(
				MakeAtomicPivotSwitchScenario(),
				Engine,
				Random))
		|| !TestNotNull(TEXT("Stale-Fight Pivot random seam is retained"), Random))
	{
		return false;
	}
	FBattleDecisionRequest Request;
	FBattleDecision Response;
	if (!TestTrue(TEXT("Stale-Fight test reaches AwaitingPivot"),
			TryPrepareAtomicPivotSwitch(*Engine, Request))
		|| !TestTrue(TEXT("Stale-Fight response is created"),
			TryMakePivotSwitchDecision(Request, MakePartySlotId(1), Response)))
	{
		return false;
	}
	const FAtomicPivotSwitchObservation Before =
		ObserveAtomicPivotSwitchCheckpoint(*Engine);
	FAtomicPivotSwitchObservation Expected = Before;
	++Expected.CurrentAction.QueueOrdinal;
	bool bMutationSucceeded = false;
	check(Random != nullptr);
	Random->ArmAfterTraceRead(
		2,
		[EnginePtr = Engine.Get(), &bMutationSucceeded]()
		{
			FBattleEngineState& State =
				FBattleC09BWildFlowEngineFixture::GetMutableState(*EnginePtr);
			if (State.LockedActions.IsValidIndex(State.CurrentLockedActionIndex))
			{
				++State.LockedActions[State.CurrentLockedActionIndex].QueueOrdinal;
				bMutationSucceeded = true;
			}
		});
	const FBattleResolution Rejected = Engine->SubmitDecision(Response);
	const int32 TraceReads = Random->GetReadsSinceArm();
	const bool bInjected = Random->WasInjected();
	Random->Disarm();
	TestTrue(TEXT("Exact Fight identity changes only at final Pivot recheck"),
		bInjected && bMutationSucceeded && TraceReads == 2);
	return VerifyRejectedAtomicPivotSwitch(
		*this,
		*Engine,
		Before,
		Expected,
		Response,
		EBattleRejectionReason::StaleCheckpointIdentity,
		Rejected);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E3PivotSwitchStaleRequestTest,
	"PokemonSolarus.Battle.ADR0002.3E3.PivotSwitch.Failure.StalePendingRequestIdentityBeforeCommit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E3PivotSwitchStaleRequestTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	TUniquePtr<FBattleEngine> Engine;
	FActionStartStaleRandom* Random = nullptr;
	if (!TestTrue(TEXT("Stale-request Pivot engine is created"),
			TryMakeActionStartStaleEngine(
				MakeAtomicPivotSwitchScenario(
					FItemId(),
					FBattleAbilityRules::GetBlazeId(),
					200,
					true),
				Engine,
				Random))
		|| !TestNotNull(TEXT("Stale-request Pivot random seam is retained"), Random))
	{
		return false;
	}
	FBattleDecisionRequest Request;
	FBattleDecision Response;
	if (!TestTrue(TEXT("Stale-request test reaches AwaitingPivot"),
			TryPrepareAtomicPivotSwitch(*Engine, Request))
		|| !TestTrue(TEXT("Stale-request response is created"),
			TryMakePivotSwitchDecision(Request, MakePartySlotId(1), Response)))
	{
		return false;
	}
	const FAtomicPivotSwitchObservation Before =
		ObserveAtomicPivotSwitchCheckpoint(*Engine);
	FAtomicPivotSwitchObservation Expected = Before;
	FBattleDecisionRequest Replacement;
	if (!TestTrue(TEXT("Different canonical Pivot request is created"),
			TryCopyPivotRequestWithSwitchSlot(
				Before.PendingRequest,
				MakePartySlotId(2),
				Replacement)))
	{
		return false;
	}
	Expected.PendingDecision = Replacement;
	Expected.PendingRequest = Replacement;
	bool bMutationSucceeded = false;
	check(Random != nullptr);
	Random->ArmAfterTraceRead(
		2,
		[EnginePtr = Engine.Get(), &bMutationSucceeded]()
		{
			bMutationSucceeded = TryReplaceAtomicPivotPendingRequest(
				*EnginePtr,
				MakePartySlotId(2));
		});
	const FBattleResolution Rejected = Engine->SubmitDecision(Response);
	const int32 TraceReads = Random->GetReadsSinceArm();
	const bool bInjected = Random->WasInjected();
	Random->Disarm();
	TestTrue(TEXT("Exact pending Pivot request changes only at final recheck"),
		bInjected && bMutationSucceeded && TraceReads == 2);
	return VerifyRejectedAtomicPivotSwitch(
		*this,
		*Engine,
		Before,
		Expected,
		Response,
		EBattleRejectionReason::StaleCheckpointIdentity,
		Rejected);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E3PivotSwitchStaleSubmittedResponseTest,
	"PokemonSolarus.Battle.ADR0002.3E3.PivotSwitch.Failure.StaleSubmittedResponseIdentityBeforeCommit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E3PivotSwitchStaleSubmittedResponseTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	TUniquePtr<FBattleEngine> Engine;
	FActionStartStaleRandom* Random = nullptr;
	if (!TestTrue(TEXT("Stale-response Pivot engine is created"),
			TryMakeActionStartStaleEngine(
				MakeAtomicPivotSwitchScenario(
					FItemId(),
					FBattleAbilityRules::GetBlazeId(),
					200,
					true),
				Engine,
				Random))
		|| !TestNotNull(TEXT("Stale-response Pivot random seam is retained"), Random))
	{
		return false;
	}
	FBattleDecisionRequest Request;
	FBattleDecision Response;
	FBattleDecision ReplacementResponse;
	if (!TestTrue(TEXT("Stale-response test reaches AwaitingPivot"),
			TryPrepareAtomicPivotSwitch(*Engine, Request))
		|| !TestTrue(TEXT("Original Pivot response is created"),
			TryMakePivotSwitchDecision(Request, MakePartySlotId(1), Response))
		|| !TestTrue(TEXT("Different Pivot response is created"),
			TryMakePivotSwitchDecision(
				Request,
				MakePartySlotId(2),
				ReplacementResponse)))
	{
		return false;
	}
	const FAtomicPivotSwitchObservation Before =
		ObserveAtomicPivotSwitchCheckpoint(*Engine);
	bool bMutationSucceeded = false;
	check(Random != nullptr);
	Random->ArmAfterTraceRead(
		2,
		[EnginePtr = Engine.Get(), ReplacementResponse, &bMutationSucceeded]()
		{
			FBattleEngineState& State =
				FBattleC09BWildFlowEngineFixture::GetMutableState(*EnginePtr);
			if (!State.SubmittedDecisions.IsEmpty())
			{
				State.SubmittedDecisions.Last() = ReplacementResponse;
				bMutationSucceeded = true;
			}
		});
	const FBattleResolution Rejected = Engine->SubmitDecision(Response);
	const int32 TraceReads = Random->GetReadsSinceArm();
	const bool bInjected = Random->WasInjected();
	Random->Disarm();
	TestTrue(TEXT("Exact submitted Pivot response changes only at final recheck"),
		bInjected && bMutationSucceeded && TraceReads == 2);
	return VerifyRejectedAtomicPivotSwitch(
		*this,
		*Engine,
		Before,
		Before,
		ReplacementResponse,
		EBattleRejectionReason::StaleCheckpointIdentity,
		Rejected);
}

} // namespace BattleAtomicPivotSwitchTestsPrivate

#endif
