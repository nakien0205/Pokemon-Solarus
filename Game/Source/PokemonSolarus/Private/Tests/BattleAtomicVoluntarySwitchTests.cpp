#if WITH_DEV_AUTOMATION_TESTS

#include "BattleAtomicCheckpointTestCommon.h"
#include "BattleAtomicCheckpointTestFaults.h"
#include "BattleAtomicSwitchTestSupport.h"

namespace BattleAtomicVoluntarySwitchTestsPrivate
{
	using namespace BattleAtomicCheckpointTestCommonPrivate;
	using namespace BattleAtomicCheckpointTestFaultsPrivate;
	using namespace BattleAtomicSwitchTestSupportPrivate;

bool TryPrepareAtomicVoluntarySwitch(FBattleEngine& Engine)
	{
		FBattleEngineState& State =
			FBattleC09BWildFlowEngineFixture::GetMutableState(Engine);
		const FBattlerId OutgoingBattlerId =
			MakeNumericId<FBattlerId>(PlayerLeftValue);
		const FBattleBattlerState* Outgoing = State.FindBattler(OutgoingBattlerId);
		const FBattleActivePositionState* Active = State.ActivePositions.FindByPredicate(
			[OutgoingBattlerId](const FBattleActivePositionState& Candidate)
			{
				return Candidate.BattlerId == OutgoingBattlerId;
			});
		FBattleTrainerState* Trainer = Outgoing != nullptr
			? State.FindMutableTrainer(Outgoing->TrainerId)
			: nullptr;
		if (Outgoing == nullptr || Active == nullptr || Trainer == nullptr)
		{
			return false;
		}

		FBattleDecision Decision;
		if (!FBattleDecision::TryCreateSwitch(
				State.StateVersion,
				EBattleDecisionRequestKind::Action,
				Outgoing->TrainerId,
				OutgoingBattlerId,
				MakePartySlotId(1),
				Active->ActiveSlotId,
				Decision))
		{
			return false;
		}
		FBattleLockedActionState Action;
		Action.ActionId = MakeNumericId<FActionId>(3003201);
		Action.QueueOrdinal = 1;
		Action.Decision = MoveTemp(Decision);
		Action.OrderKey.CommandBand = EBattleActionCommandBand::VoluntarySwitch;
		Action.OrderKey.EffectiveSpeed = Outgoing->PermanentStats.Speed;
		Action.OrderKey.ActingSlotId = Active->ActiveSlotId;
		State.LockedActions = {MoveTemp(Action)};
		State.CurrentLockedActionIndex = 0;
		State.PendingDecision.Reset();
		State.PendingDecisionRequests.Reset();
		State.PendingReplacements.Reset();
		State.DecisionOwnerSequence.Reset();
		State.AcceptedSelections.Reset();
		State.Phase = EBattlePhase::Locked;
		Trainer->ActionAllowance.MaximumActions = 1;
		Trainer->ActionAllowance.RemainingActions = 1;
		EBattleStateValidationError Validation = EBattleStateValidationError::None;
		return State.ValidateInvariants(Validation);
	}

bool TryBeginAtomicVoluntarySwitch(FBattleEngine& Engine)
	{
		if (!Engine.BeginNextLockedAction().WasAccepted())
		{
			return false;
		}
		const TOptional<FBattleLockedAction> Current = Engine.GetCurrentLockedAction();
		return Current.IsSet()
			&& Current->Decision.GetActionKind() == EBattleActionKind::Switch
			&& Current->Decision.GetActingBattlerId()
				== MakeNumericId<FBattlerId>(PlayerLeftValue)
			&& Current->Decision.GetSwitchPartySlotId() == MakePartySlotId(1);
	}

bool VerifyRejectedAtomicVoluntarySwitch(
		FAutomationTestBase& Test,
		const FBattleEngine& Engine,
		const FAtomicSwitchCheckpointObservation& Before,
		const uint64 ExpectedStateVersion,
		const EBattleRejectionReason ExpectedReason,
		const FBattleResolution& Returned)
	{
		const FAtomicSwitchCheckpointObservation After =
			ObserveAtomicSwitchCheckpoint(Engine);
		bool bValid = true;
		bValid &= Test.TestFalse(TEXT("Voluntary Switch checkpoint failure is rejected"),
			Returned.WasAccepted());
		bValid &= Test.TestEqual(TEXT("Voluntary Switch rejection is typed"),
			Returned.GetRejection().Reason,
			ExpectedReason);
		bValid &= Test.TestTrue(TEXT("Rejected Switch is appended exactly once"),
			IsReturnedResolutionAppendedExactlyOnce(Engine, Returned));
		bValid &= Test.TestTrue(TEXT("Rejected Switch publishes cancellation only"),
			Returned.GetEvents().Num() == 1
				&& Returned.GetEvents()[0].GetType() == EBattleEventType::ActionCanceled
				&& Returned.GetEvents()[0].GetCause() == EBattleEventCause::Rule);
		bValid &= Test.TestEqual(TEXT("Rejection appends one resolution"),
			After.ResolutionCount, Before.ResolutionCount + 1);
		bValid &= Test.TestEqual(TEXT("Rejection appends one event"),
			After.EventCount, Before.EventCount + 1);
		bValid &= Test.TestEqual(TEXT("Rejection consumes one resolution id"),
			After.NextResolutionId, Before.NextResolutionId + 1);
		bValid &= Test.TestEqual(TEXT("Rejection consumes one event ordinal"),
			After.NextEventOrdinal, Before.NextEventOrdinal + 1);
		bValid &= Test.TestEqual(TEXT("Rejection has no accepted version delta"),
			After.StateVersion, ExpectedStateVersion);
		bValid &= Test.TestTrue(TEXT("Rejection preserves every staged gameplay domain"),
			AreAtomicSwitchMechanicsIdentical(After, Before));
		const FBattleReplayRecord Replay = Engine.ExportReplayRecord();
		bValid &= Test.TestEqual(TEXT("Rejected Switch replay uses schema 6"),
			Replay.GetSchemaVersion(), FBattleReplayRecord::CurrentSchemaVersion);
		bValid &= Test.TestTrue(TEXT("Rejected Switch replay contains the same fact"),
			!Replay.GetResolutions().IsEmpty()
				&& Replay.GetResolutions().Last().GetResolutionId()
					== Returned.GetResolutionId()
				&& Replay.GetResolutions().Last().GetRejection().Reason
					== Returned.GetRejection().Reason);
		return bValid;
	}

bool RunAtomicVoluntarySwitchFailureFamily(
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
		if (!Test.TestTrue(TEXT("Failure-family Switch engine is created"),
				TryMakeSequenceEngine(
					MakeAtomicVoluntarySwitchScenario(
						IncomingItemId,
						IncomingAbilityId,
						Family == EAtomicSwitchFailureFamily::ImmediateHeldItem ? 90 : 200),
					{},
					Engine))
			|| !Test.TestTrue(TEXT("Failure-family Switch is locked"),
				TryPrepareAtomicVoluntarySwitch(*Engine))
			|| !Test.TestTrue(TEXT("Outgoing transient state is seeded"),
				TrySeedAtomicSwitchOutgoingTransients(*Engine))
			|| !Test.TestTrue(TEXT("Failure-family Switch is started"),
				TryBeginAtomicVoluntarySwitch(*Engine)))
		{
			return false;
		}
		if (Family == EAtomicSwitchFailureFamily::EntryHazard
			&& !Test.TestTrue(TEXT("Entry hazard is seeded"),
				TrySeedAtomicSwitchHazard(
					*Engine,
					FBattleFieldSideConditionRules::GetSpikesId())))
		{
			return false;
		}
		FBattleEngineState& MutableState =
			FBattleC09BWildFlowEngineFixture::GetMutableState(*Engine);
		MutableState.NextTriggerReentrancyToken =
			TNumericLimits<uint64>::Max() - 1;
		const FAtomicSwitchCheckpointObservation Before =
			ObserveAtomicSwitchCheckpoint(*Engine);
		const FBattleResolution Rejected = Engine->ExecuteCurrentSwitch();
		return VerifyRejectedAtomicVoluntarySwitch(
			Test,
			*Engine,
			Before,
			Before.StateVersion,
			EBattleRejectionReason::CheckpointPreparationFailed,
			Rejected);
	}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E2VoluntarySwitchFullEntryChainTest,
	"PokemonSolarus.Battle.ADR0002.3E2.VoluntarySwitch.Success.FullEntryChain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E2VoluntarySwitchFullEntryChainTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	TUniquePtr<FBattleEngine> Engine;
	if (!TestTrue(TEXT("Full-chain Switch engine is created"),
			TryMakeSequenceEngine(
				MakeAtomicVoluntarySwitchScenario(
					FBattleItemRules::GetSitrusBerryId(),
					FBattleAbilityRules::GetIntimidateId(),
					110),
				{},
				Engine))
		|| !TestTrue(TEXT("Full-chain Switch is locked"),
			TryPrepareAtomicVoluntarySwitch(*Engine))
		|| !TestTrue(TEXT("Full-chain outgoing transients are seeded"),
			TrySeedAtomicSwitchOutgoingTransients(*Engine))
		|| !TestTrue(TEXT("Full-chain Switch is started"),
			TryBeginAtomicVoluntarySwitch(*Engine))
		|| !TestTrue(TEXT("Full-chain Stealth Rock is seeded"),
			TrySeedAtomicSwitchHazard(
				*Engine,
				FBattleFieldSideConditionRules::GetStealthRockId())))
	{
		return false;
	}

	const FAtomicSwitchCheckpointObservation Before =
		ObserveAtomicSwitchCheckpoint(*Engine);
	const FBattleResolution Resolution = Engine->ExecuteCurrentSwitch();
	const FBattleEngineState& State =
		FBattleC09BWildFlowEngineFixture::GetState(*Engine);
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

	TestTrue(TEXT("Full entry chain is accepted"), Resolution.WasAccepted());
	TestTrue(TEXT("Full entry chain has exact event order"),
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
	TestTrue(TEXT("Full entry chain is appended exactly once"),
		IsReturnedResolutionAppendedExactlyOnce(*Engine, Resolution));
	TestEqual(TEXT("Full entry chain advances one version"),
		State.StateVersion, Before.StateVersion + 1);
	TestEqual(TEXT("Full entry chain consumes no gameplay RNG"),
		State.Random->GetTrace().Num(), Before.RandomTraceCount);
	TestTrue(TEXT("Outgoing battler is inactive and fully cleaned"),
		Outgoing != nullptr
			&& PlayerActive != nullptr
			&& PlayerActive->BattlerId == MakeNumericId<FBattlerId>(PlayerReserveValue)
			&& Outgoing->Volatiles.IsEmpty()
			&& !Outgoing->LastMoveId.IsValid()
			&& !Outgoing->bAbilitySuppressed
			&& !Outgoing->EnteredActiveOnTurnId.IsValid());
	int32 OutgoingAttackStage = 1;
	TestTrue(TEXT("Outgoing stages reset at commit"),
		Outgoing != nullptr
			&& Outgoing->Stages.TryGetStage(EBattleStat::Attack, OutgoingAttackStage)
			&& OutgoingAttackStage == 0);
	TestTrue(TEXT("Incoming hazard, item, and entry Ability all commit"),
		Incoming != nullptr
			&& Incoming->CurrentHP == 135
			&& Incoming->HeldItem.bConsumed
			&& !Incoming->HeldItem.CurrentItemId.IsValid()
			&& Incoming->EnteredActiveOnTurnId == State.TurnId
			&& bOpponentStageRead
			&& OpponentAttackStage == -1);
	TestTrue(TEXT("Incoming Sitrus and Intimidate reveal facts commit"),
		IsAtomicSwitchDefinitionRevealed(
			State,
			MakeNumericId<FBattlerId>(PlayerReserveValue),
			true)
			&& Incoming != nullptr
			&& Incoming->HeldItem.bRevealed);
	TestEqual(TEXT("Full entry chain reaches EndOfTurn"),
		State.Phase, EBattlePhase::EndOfTurn);
	TestEqual(TEXT("Full entry chain advances the action cursor"),
		State.CurrentLockedActionIndex, 1);
	TestTrue(TEXT("Full entry chain marks its exact action finished"),
		State.LockedActions.Num() == 1 && State.LockedActions[0].bFinished);
	const FBattleReplayRecord Replay = Engine->ExportReplayRecord();
	TestEqual(TEXT("Full entry chain replay keeps schema 6"),
		Replay.GetSchemaVersion(), FBattleReplayRecord::CurrentSchemaVersion);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E2VoluntarySwitchAirBalloonTest,
	"PokemonSolarus.Battle.ADR0002.3E2.VoluntarySwitch.Success.AirBalloonReveal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E2VoluntarySwitchAirBalloonTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	TUniquePtr<FBattleEngine> Engine;
	if (!TestTrue(TEXT("Air Balloon Switch engine is created"),
			TryMakeSequenceEngine(
				MakeAtomicVoluntarySwitchScenario(FBattleItemRules::GetAirBalloonId()),
				{},
				Engine))
		|| !TestTrue(TEXT("Air Balloon Switch is locked"),
			TryPrepareAtomicVoluntarySwitch(*Engine))
		|| !TestTrue(TEXT("Air Balloon Switch is started"),
			TryBeginAtomicVoluntarySwitch(*Engine)))
	{
		return false;
	}
	const FAtomicSwitchCheckpointObservation Before =
		ObserveAtomicSwitchCheckpoint(*Engine);
	const FBattleResolution Resolution = Engine->ExecuteCurrentSwitch();
	const FBattleEngineState& State =
		FBattleC09BWildFlowEngineFixture::GetState(*Engine);
	const FBattleBattlerState* Incoming = State.FindBattler(
		MakeNumericId<FBattlerId>(PlayerReserveValue));
	TestTrue(TEXT("Air Balloon Switch is accepted"), Resolution.WasAccepted());
	TestTrue(TEXT("Air Balloon Switch has exact event order"),
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
	TestTrue(TEXT("Air Balloon Switch is appended exactly once"),
		IsReturnedResolutionAppendedExactlyOnce(*Engine, Resolution));
	TestTrue(TEXT("Air Balloon reveal commits to mirror and tracker"),
		Incoming != nullptr
			&& Incoming->HeldItem.CurrentItemId == FBattleItemRules::GetAirBalloonId()
			&& Incoming->HeldItem.bRevealed
			&& IsAtomicSwitchDefinitionRevealed(
				State,
				Incoming->BattlerId,
				false));
	TestEqual(TEXT("Air Balloon Switch advances one version"),
		State.StateVersion, Before.StateVersion + 1);
	TestEqual(TEXT("Air Balloon Switch consumes no RNG"),
		State.Random->GetTrace().Num(), Before.RandomTraceCount);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E2VoluntarySwitchLethalHazardTest,
	"PokemonSolarus.Battle.ADR0002.3E2.VoluntarySwitch.Success.LethalHazardReplacementBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E2VoluntarySwitchLethalHazardTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	TUniquePtr<FBattleEngine> Engine;
	if (!TestTrue(TEXT("Lethal-hazard Switch engine is created"),
			TryMakeSequenceEngine(MakeAtomicVoluntarySwitchScenario(FItemId(),
				FBattleAbilityRules::GetBlazeId(), 10), {}, Engine))
		|| !TestTrue(TEXT("Lethal-hazard Switch is locked"),
			TryPrepareAtomicVoluntarySwitch(*Engine))
		|| !TestTrue(TEXT("Lethal-hazard Switch is started"),
			TryBeginAtomicVoluntarySwitch(*Engine))
		|| !TestTrue(TEXT("Lethal Stealth Rock is seeded"),
			TrySeedAtomicSwitchHazard(
				*Engine,
				FBattleFieldSideConditionRules::GetStealthRockId())))
	{
		return false;
	}
	const FAtomicSwitchCheckpointObservation Before =
		ObserveAtomicSwitchCheckpoint(*Engine);
	const FBattleResolution Resolution = Engine->ExecuteCurrentSwitch();
	const FBattleEngineState& State =
		FBattleC09BWildFlowEngineFixture::GetState(*Engine);
	const FBattleBattlerState* Incoming = State.FindBattler(
		MakeNumericId<FBattlerId>(PlayerReserveValue));
	const FBattleActivePositionState* PlayerActive = State.FindActivePosition(
		MakeActiveSlotId(EBattleSide::Player, EBattlePosition::Left));
	TestTrue(TEXT("Lethal-hazard Switch is accepted"), Resolution.WasAccepted());
	TestTrue(TEXT("Lethal-hazard Switch has exact event and boundary order"),
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
	TestTrue(TEXT("Lethal-hazard Switch is appended exactly once"),
		IsReturnedResolutionAppendedExactlyOnce(*Engine, Resolution));
	TestTrue(TEXT("Lethal incoming battler fully faints and leaves its slot"),
		Incoming != nullptr
			&& Incoming->CurrentHP == 0
			&& Incoming->bFainted
			&& Incoming->bRemoved
			&& PlayerActive != nullptr
			&& !PlayerActive->BattlerId.IsValid()
			&& !PlayerActive->TrainerId.IsValid());
	TestEqual(TEXT("Lethal hazard reaches mandatory replacement"),
		State.Phase, EBattlePhase::MandatoryReplacement);
	TestTrue(TEXT("Lethal hazard prepares one complete replacement checkpoint"),
		State.PendingReplacements.Num() == 1
			&& State.PendingDecisionRequests.Num() == 1
			&& State.PendingDecision.IsSet()
			&& State.PendingReplacements[0].TrainerId
				== MakeNumericId<FTrainerId>(PlayerTrainerValue));
	TestEqual(TEXT("Lethal hazard advances one version"),
		State.StateVersion, Before.StateVersion + 1);
	TestEqual(TEXT("Lethal hazard consumes no RNG"),
		State.Random->GetTrace().Num(), Before.RandomTraceCount);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E2VoluntarySwitchEntryItemFailureTest,
	"PokemonSolarus.Battle.ADR0002.3E2.VoluntarySwitch.Failure.EntryItemReveal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E2VoluntarySwitchEntryItemFailureTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	return RunAtomicVoluntarySwitchFailureFamily(
		*this,
		EAtomicSwitchFailureFamily::EntryItemReveal);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E2VoluntarySwitchEntryHazardFailureTest,
	"PokemonSolarus.Battle.ADR0002.3E2.VoluntarySwitch.Failure.EntryHazard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E2VoluntarySwitchEntryHazardFailureTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	return RunAtomicVoluntarySwitchFailureFamily(
		*this,
		EAtomicSwitchFailureFamily::EntryHazard);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E2VoluntarySwitchImmediateItemFailureTest,
	"PokemonSolarus.Battle.ADR0002.3E2.VoluntarySwitch.Failure.ImmediateHeldItem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E2VoluntarySwitchImmediateItemFailureTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	return RunAtomicVoluntarySwitchFailureFamily(
		*this,
		EAtomicSwitchFailureFamily::ImmediateHeldItem);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E2VoluntarySwitchEntryAbilityFailureTest,
	"PokemonSolarus.Battle.ADR0002.3E2.VoluntarySwitch.Failure.EntryAbility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E2VoluntarySwitchEntryAbilityFailureTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	return RunAtomicVoluntarySwitchFailureFamily(
		*this,
		EAtomicSwitchFailureFamily::EntryAbility);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E2VoluntarySwitchStaleIdentityTest,
	"PokemonSolarus.Battle.ADR0002.3E2.VoluntarySwitch.Failure.StaleIdentityBeforeCommit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E2VoluntarySwitchStaleIdentityTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	TUniquePtr<FBattleEngine> Engine;
	FActionStartStaleRandom* Random = nullptr;
	if (!TestTrue(TEXT("Stale Switch engine is created"),
			TryMakeActionStartStaleEngine(
				MakeAtomicVoluntarySwitchScenario(),
				Engine,
				Random))
		|| !TestNotNull(TEXT("Stale Switch random seam is retained"), Random)
		|| !TestTrue(TEXT("Stale Switch is locked"),
			TryPrepareAtomicVoluntarySwitch(*Engine))
		|| !TestTrue(TEXT("Stale Switch outgoing transients are seeded"),
			TrySeedAtomicSwitchOutgoingTransients(*Engine))
		|| !TestTrue(TEXT("Stale Switch is started"),
			TryBeginAtomicVoluntarySwitch(*Engine)))
	{
		return false;
	}
	const FAtomicSwitchCheckpointObservation Before =
		ObserveAtomicSwitchCheckpoint(*Engine);
	check(Random != nullptr);
	Random->ArmAfterTraceRead(
		2,
		[EnginePtr = Engine.Get()]()
		{
			FBattleC09BWildFlowEngineFixture::AdvanceStateVersion(*EnginePtr);
		});
	const FBattleResolution Rejected = Engine->ExecuteCurrentSwitch();
	const int32 TraceReads = Random->GetReadsSinceArm();
	const bool bInjected = Random->WasInjected();
	Random->Disarm();
	TestTrue(TEXT("Stale identity is injected only at the final recheck"),
		bInjected && TraceReads == 2);
	return VerifyRejectedAtomicVoluntarySwitch(
		*this,
		*Engine,
		Before,
		Before.StateVersion + 1,
		EBattleRejectionReason::StaleCheckpointIdentity,
		Rejected);
}

} // namespace BattleAtomicVoluntarySwitchTestsPrivate

#endif
