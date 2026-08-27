#if WITH_DEV_AUTOMATION_TESTS

#include "BattleAtomicCheckpointTestCommon.h"
#include "BattleAtomicCheckpointTestFaults.h"

namespace BattleAtomicWildActionTestsPrivate
{
	using namespace BattleAtomicCheckpointTestCommonPrivate;
	using namespace BattleAtomicCheckpointTestFaultsPrivate;

bool ExecuteRemainingQueue(FBattleEngine& Engine)
	{
		int32 Guard = 0;
		while (Engine.GetSnapshot().GetPhase() == EBattlePhase::Resolving
			&& Guard++ < 12)
		{
			if (!Engine.BeginNextLockedAction().WasAccepted())
			{
				return false;
			}
			const TOptional<FBattleLockedAction> Current = Engine.GetCurrentLockedAction();
			if (!Current.IsSet()
				|| Current->Decision.GetActionKind() != EBattleActionKind::Fight
				|| !Engine.CommitCurrentMoveAfterPreMoveGates().WasAccepted())
			{
				return false;
			}
			if (Engine.GetCurrentLockedAction().IsSet()
				&& !Engine.ResolveCurrentMoveTargets().WasAccepted())
			{
				return false;
			}
			if (Engine.GetCurrentLockedAction().IsSet()
				&& !Engine.ExecuteCurrentMoveEffects().WasAccepted())
			{
				return false;
			}
		}
		return Guard < 12 && Engine.GetSnapshot().GetPhase() == EBattlePhase::EndOfTurn;
	}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023D1RunAtomicCommitTest,
	"PokemonSolarus.Battle.ADR0002.3D1.WildActions.Run.SuccessFailureAndNoDraw",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023D1RunAtomicCommitTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FAtomicWildScenario Scenario;

	TUniquePtr<FBattleEngine> Failed;
	if (!TestTrue(TEXT("Failed-Run engine is created"),
		TryMakeSequenceEngine(Scenario, {200}, Failed))
		|| !TestTrue(TEXT("Failed Run turn locks"),
			LockTurn(*Failed, PlayerLeftValue, EBattleActionKind::Run))
		|| !TestTrue(TEXT("Failed Run starts"),
			BeginExpectedWildAction(*Failed, PlayerLeftValue, EBattleActionKind::Run)))
	{
		return false;
	}
	const int32 FailedResolutionCountBefore =
		FBattleC09BWildFlowEngineFixture::GetState(*Failed).Resolutions.Num();
	const FBattleResolution FailedResolution = Failed->ExecuteCurrentWildAction();
	TestTrue(TEXT("Legal failed Run is accepted"), FailedResolution.WasAccepted());
	TestEqual(TEXT("Failed Run increments the escape counter"),
		Failed->GetSnapshot().GetEscapeAttemptCount(), 2U);
	TestEqual(TEXT("Failed Run commits one parent draw"),
		FBattleC09BWildFlowEngineFixture::GetState(*Failed).Random->GetTrace().Num(), 1);
	TestTrue(TEXT("Failed Run preserves exact events"),
		HasExactEventOrder(FailedResolution, {
			EBattleEventType::RunAttempted,
			EBattleEventType::ActionCompleted}));
	TestEqual(TEXT("Failed Run appends one resolution"),
		FBattleC09BWildFlowEngineFixture::GetState(*Failed).Resolutions.Num(),
		FailedResolutionCountBefore + 1);
	TestTrue(TEXT("Failed Run returns the appended resolution"),
		IsReturnedResolutionAppendedExactlyOnce(*Failed, FailedResolution));

	TUniquePtr<FBattleEngine> Succeeded;
	if (!TestTrue(TEXT("Successful-Run engine is created"),
		TryMakeSequenceEngine(Scenario, {0}, Succeeded))
		|| !TestTrue(TEXT("Successful Run turn locks"),
			LockTurn(*Succeeded, PlayerLeftValue, EBattleActionKind::Run))
		|| !TestTrue(TEXT("Successful Run starts"),
			BeginExpectedWildAction(*Succeeded, PlayerLeftValue, EBattleActionKind::Run)))
	{
		return false;
	}
	const FBattleResolution SuccessResolution = Succeeded->ExecuteCurrentWildAction();
	TestTrue(TEXT("Legal successful Run is accepted"), SuccessResolution.WasAccepted());
	TestEqual(TEXT("Successful Run reaches terminal Escape"),
		Succeeded->GetSnapshot().GetOutcome(), EBattleOutcome::Escape);
	TestEqual(TEXT("Successful Run keeps ordinary cause"),
		Succeeded->GetSnapshot().GetOutcomeCause(), EBattleOutcomeCause::Ordinary);
	TestTrue(TEXT("Successful Run preserves exact events"),
		HasExactEventOrder(SuccessResolution, {
			EBattleEventType::RunAttempted,
			EBattleEventType::Escaped,
			EBattleEventType::ActionCompleted,
			EBattleEventType::BattleEnded}));
	TestTrue(TEXT("Successful Run returns the appended resolution"),
		IsReturnedResolutionAppendedExactlyOnce(*Succeeded, SuccessResolution));

	FAtomicWildScenario GuaranteedScenario;
	GuaranteedScenario.PlayerLeftSpeed = 100;
	GuaranteedScenario.OpponentLeftSpeed = 4;
	TUniquePtr<FBattleEngine> Guaranteed;
	if (!TestTrue(TEXT("Guaranteed-Run engine is created"),
		TryMakeSequenceEngine(GuaranteedScenario, {}, Guaranteed))
		|| !TestTrue(TEXT("Guaranteed Run turn locks"),
			LockTurn(*Guaranteed, PlayerLeftValue, EBattleActionKind::Run))
		|| !TestTrue(TEXT("Guaranteed Run starts"),
			BeginExpectedWildAction(*Guaranteed, PlayerLeftValue, EBattleActionKind::Run)))
	{
		return false;
	}
	const FBattleResolution GuaranteedResolution = Guaranteed->ExecuteCurrentWildAction();
	TestTrue(TEXT("F greater than 255 succeeds"), GuaranteedResolution.WasAccepted());
	TestEqual(TEXT("Guaranteed Run publishes no parent draw"),
		FBattleC09BWildFlowEngineFixture::GetState(*Guaranteed).Random->GetTrace().Num(), 0);
	TestEqual(TEXT("Guaranteed Run remains ordinary Escape"),
		Guaranteed->GetSnapshot().GetOutcomeCause(), EBattleOutcomeCause::Ordinary);
	TestEqual(TEXT("Run replay schema remains 6"),
		Guaranteed->ExportReplayRecord().GetSchemaVersion(), 6U);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023D1WildFleeModesAtomicCommitTest,
	"PokemonSolarus.Battle.ADR0002.3D1.WildActions.WildFlee.DeterministicAndChance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023D1WildFleeModesAtomicCommitTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	for (const EBattleWildFleeMode Mode : {
		EBattleWildFleeMode::Never,
		EBattleWildFleeMode::Always})
	{
		FAtomicWildScenario Scenario;
		Scenario.WildFleeMode = Mode;
		TUniquePtr<FBattleEngine> Engine;
		if (!TestTrue(TEXT("Deterministic WildFlee engine is created"),
			TryMakeSequenceEngine(Scenario, {}, Engine))
			|| !TestTrue(TEXT("Deterministic WildFlee turn locks"),
				LockTurn(*Engine, OpponentLeftValue, EBattleActionKind::WildFlee))
			|| !TestTrue(TEXT("Deterministic WildFlee starts"),
				BeginExpectedWildAction(*Engine, OpponentLeftValue, EBattleActionKind::WildFlee)))
		{
			return false;
		}
		const FBattleResolution Resolution = Engine->ExecuteCurrentWildAction();
		TestTrue(TEXT("Deterministic WildFlee checkpoint is accepted"), Resolution.WasAccepted());
		TestEqual(TEXT("Deterministic WildFlee publishes no parent draw"),
			FBattleC09BWildFlowEngineFixture::GetState(*Engine).Random->GetTrace().Num(), 0);
		TestEqual(TEXT("Never leaves the actor active; Always removes it"),
			FBattleC09BWildFlowEngineFixture::IsRemoved(
				*Engine,
				MakeNumericId<FBattlerId>(OpponentLeftValue)),
			Mode == EBattleWildFleeMode::Always);
		TestTrue(TEXT("Deterministic result returns the appended resolution"),
			IsReturnedResolutionAppendedExactlyOnce(*Engine, Resolution));
	}

	for (const uint32 Draw : {1U, 0U})
	{
		FAtomicWildScenario Scenario;
		Scenario.WildFleeMode = EBattleWildFleeMode::Chance;
		Scenario.WildFleeNumerator = 1;
		Scenario.WildFleeDenominator = 2;
		TUniquePtr<FBattleEngine> Engine;
		if (!TestTrue(TEXT("Chance WildFlee engine is created"),
			TryMakeSequenceEngine(Scenario, {Draw}, Engine))
			|| !TestTrue(TEXT("Chance WildFlee turn locks"),
				LockTurn(*Engine, OpponentLeftValue, EBattleActionKind::WildFlee))
			|| !TestTrue(TEXT("Chance WildFlee starts"),
				BeginExpectedWildAction(*Engine, OpponentLeftValue, EBattleActionKind::WildFlee)))
		{
			return false;
		}
		const FBattleResolution Resolution = Engine->ExecuteCurrentWildAction();
		TestTrue(TEXT("Chance WildFlee checkpoint is accepted"), Resolution.WasAccepted());
		TestEqual(TEXT("Chance WildFlee commits exactly one parent draw"),
			FBattleC09BWildFlowEngineFixture::GetState(*Engine).Random->GetTrace().Num(), 1);
		TestEqual(TEXT("Chance outcome follows draw below numerator"),
			FBattleC09BWildFlowEngineFixture::IsRemoved(
				*Engine,
				MakeNumericId<FBattlerId>(OpponentLeftValue)),
			Draw == 0);
		TestTrue(TEXT("Chance result returns the appended resolution"),
			IsReturnedResolutionAppendedExactlyOnce(*Engine, Resolution));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023D1MultiWildQueueContinuationTest,
	"PokemonSolarus.Battle.ADR0002.3D1.WildActions.WildFlee.MultiActiveQueueContinuation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023D1MultiWildQueueContinuationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FAtomicWildScenario Scenario;
	Scenario.Format = EBattleFormat::Double;
	Scenario.WildFleeMode = EBattleWildFleeMode::Always;
	TUniquePtr<FBattleEngine> Engine;
	if (!TestTrue(TEXT("Multi-Wild engine is created"),
		TryMakeSequenceEngine(Scenario, {}, Engine))
		|| !TestTrue(TEXT("Multi-Wild turn locks"),
			LockTurn(*Engine, OpponentLeftValue, EBattleActionKind::WildFlee))
		|| !TestTrue(TEXT("WildFlee is the first locked action"),
			BeginExpectedWildAction(*Engine, OpponentLeftValue, EBattleActionKind::WildFlee)))
	{
		return false;
	}

	const FBattleResolution Resolution = Engine->ExecuteCurrentWildAction();
	TestTrue(TEXT("One active Wild may flee"), Resolution.WasAccepted());
	TestTrue(TEXT("Only the fleeing Wild is removed"),
		FBattleC09BWildFlowEngineFixture::IsRemoved(
			*Engine,
			MakeNumericId<FBattlerId>(OpponentLeftValue)));
	TestFalse(TEXT("The remaining active Wild is not removed"),
		FBattleC09BWildFlowEngineFixture::IsRemoved(
			*Engine,
			MakeNumericId<FBattlerId>(OpponentRightValue)));
	TestTrue(TEXT("The remaining Wild stays active"),
		FBattleC09BWildFlowEngineFixture::IsActive(
			*Engine,
			MakeNumericId<FBattlerId>(OpponentRightValue)));
	TestEqual(TEXT("Battle remains in progress"),
		Engine->GetSnapshot().GetOutcome(), EBattleOutcome::InProgress);
	TestTrue(TEXT("Multi-Wild flee preserves exact event order"),
		HasExactEventOrder(Resolution, {
			EBattleEventType::RunAttempted,
			EBattleEventType::Escaped,
			EBattleEventType::LeftActiveSlot,
			EBattleEventType::Removed,
			EBattleEventType::OpponentRemovalCheckpoint,
			EBattleEventType::ActionCompleted}));
	TestTrue(TEXT("Remaining locked actions execute to queue boundary"),
		ExecuteRemainingQueue(*Engine));
	TestEqual(TEXT("Queue reaches EndOfTurn without Wild replacement"),
		Engine->GetSnapshot().GetPhase(), EBattlePhase::EndOfTurn);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023D1PreparationFailureTest,
	"PokemonSolarus.Battle.ADR0002.3D1.WildActions.Failure.Preparation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023D1PreparationFailureTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FAtomicWildScenario RunScenario;
	RunScenario.PlayerLeftSpeed = 100;
	RunScenario.OpponentLeftSpeed = 4;
	TUniquePtr<FBattleEngine> RunEngine;
	if (!TestTrue(TEXT("Run cleanup-failure engine is created"),
		TryMakeSequenceEngine(RunScenario, {}, RunEngine))
		|| !TestTrue(TEXT("Run cleanup-failure turn locks"),
			LockTurn(*RunEngine, PlayerLeftValue, EBattleActionKind::Run))
		|| !TestTrue(TEXT("Run cleanup-failure action starts"),
			BeginExpectedWildAction(*RunEngine, PlayerLeftValue, EBattleActionKind::Run)))
	{
		return false;
	}
	FBattleC09BWildFlowEngineFixture::SetNextTriggerReentrancyToken(
		*RunEngine,
		TNumericLimits<uint64>::Max());
	const FCheckpointObservation RunBefore = ObserveCheckpoint(
		*RunEngine,
		MakeNumericId<FBattlerId>(PlayerLeftValue));
	const FBattleResolution RunRejected = RunEngine->ExecuteCurrentWildAction();
	VerifyRejectedCheckpoint(
		*this,
		*RunEngine,
		MakeNumericId<FBattlerId>(PlayerLeftValue),
		RunBefore,
		RunBefore.StateVersion,
		EBattleRejectionReason::CheckpointPreparationFailed,
		RunRejected);

	FAtomicWildScenario FleeScenario;
	FleeScenario.WildFleeMode = EBattleWildFleeMode::Always;
	TUniquePtr<FBattleEngine> FleeEngine;
	if (!TestTrue(TEXT("WildFlee cleanup-failure engine is created"),
		TryMakeSequenceEngine(FleeScenario, {}, FleeEngine))
		|| !TestTrue(TEXT("WildFlee cleanup-failure turn locks"),
			LockTurn(*FleeEngine, OpponentLeftValue, EBattleActionKind::WildFlee))
		|| !TestTrue(TEXT("WildFlee cleanup-failure action starts"),
			BeginExpectedWildAction(*FleeEngine, OpponentLeftValue, EBattleActionKind::WildFlee)))
	{
		return false;
	}
	FBattleC09BWildFlowEngineFixture::SetNextTriggerReentrancyToken(
		*FleeEngine,
		TNumericLimits<uint64>::Max());
	const FCheckpointObservation FleeBefore = ObserveCheckpoint(
		*FleeEngine,
		MakeNumericId<FBattlerId>(OpponentLeftValue));
	const FBattleResolution FleeRejected = FleeEngine->ExecuteCurrentWildAction();
	VerifyRejectedCheckpoint(
		*this,
		*FleeEngine,
		MakeNumericId<FBattlerId>(OpponentLeftValue),
		FleeBefore,
		FleeBefore.StateVersion,
		EBattleRejectionReason::CheckpointPreparationFailed,
		FleeRejected);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023D1RandomStageFailureTest,
	"PokemonSolarus.Battle.ADR0002.3D1.WildActions.Failure.RandomStage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023D1RandomStageFailureTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FAtomicWildScenario RunScenario;
	TUniquePtr<FBattleEngine> CreateEngine;
	FFaultBattleRandom* CreateRandom = nullptr;
	if (!TestTrue(TEXT("Transaction-create failure engine is created"),
		TryMakeFaultEngine(
			RunScenario,
			{0},
			EFaultRandomMode::CreateTransaction,
			CreateEngine,
			CreateRandom))
		|| !TestTrue(TEXT("Transaction-create failure turn locks"),
			LockTurn(*CreateEngine, PlayerLeftValue, EBattleActionKind::Run))
		|| !TestTrue(TEXT("Transaction-create failure action starts"),
			BeginExpectedWildAction(*CreateEngine, PlayerLeftValue, EBattleActionKind::Run)))
	{
		return false;
	}
	check(CreateRandom != nullptr);
	const FCheckpointObservation CreateBefore = ObserveCheckpoint(
		*CreateEngine,
		MakeNumericId<FBattlerId>(PlayerLeftValue));
	const FBattleResolution CreateRejected = CreateEngine->ExecuteCurrentWildAction();
	VerifyRejectedCheckpoint(
		*this,
		*CreateEngine,
		MakeNumericId<FBattlerId>(PlayerLeftValue),
		CreateBefore,
		CreateBefore.StateVersion,
		EBattleRejectionReason::CheckpointRandomStageFailed,
		CreateRejected);

	FAtomicWildScenario ChanceScenario;
	ChanceScenario.WildFleeMode = EBattleWildFleeMode::Chance;
	ChanceScenario.WildFleeNumerator = 1;
	ChanceScenario.WildFleeDenominator = 2;
	TUniquePtr<FBattleEngine> DrawEngine;
	FFaultBattleRandom* DrawRandom = nullptr;
	if (!TestTrue(TEXT("Staged-draw failure engine is created"),
		TryMakeFaultEngine(
			ChanceScenario,
			{0},
			EFaultRandomMode::Draw,
			DrawEngine,
			DrawRandom))
		|| !TestTrue(TEXT("Staged-draw failure turn locks"),
			LockTurn(*DrawEngine, OpponentLeftValue, EBattleActionKind::WildFlee))
		|| !TestTrue(TEXT("Staged-draw failure action starts"),
			BeginExpectedWildAction(*DrawEngine, OpponentLeftValue, EBattleActionKind::WildFlee)))
	{
		return false;
	}
	check(DrawRandom != nullptr);
	const FCheckpointObservation DrawBefore = ObserveCheckpoint(
		*DrawEngine,
		MakeNumericId<FBattlerId>(OpponentLeftValue));
	const FBattleResolution DrawRejected = DrawEngine->ExecuteCurrentWildAction();
	VerifyRejectedCheckpoint(
		*this,
		*DrawEngine,
		MakeNumericId<FBattlerId>(OpponentLeftValue),
		DrawBefore,
		DrawBefore.StateVersion,
		EBattleRejectionReason::CheckpointRandomStageFailed,
		DrawRejected);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023D1StaleIdentityFailureTest,
	"PokemonSolarus.Battle.ADR0002.3D1.WildActions.Failure.StaleIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023D1StaleIdentityFailureTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FAtomicWildScenario Scenario;
	TUniquePtr<FBattleEngine> Engine;
	FFaultBattleRandom* Random = nullptr;
	if (!TestTrue(TEXT("Stale-identity engine is created"),
		TryMakeFaultEngine(
			Scenario,
			{0},
			EFaultRandomMode::StaleAfterDraw,
			Engine,
			Random))
		|| !TestTrue(TEXT("Stale-identity turn locks"),
			LockTurn(*Engine, PlayerLeftValue, EBattleActionKind::Run))
		|| !TestTrue(TEXT("Stale-identity action starts"),
			BeginExpectedWildAction(*Engine, PlayerLeftValue, EBattleActionKind::Run)))
	{
		return false;
	}
	check(Random != nullptr);
	Random->SetAfterDraw([EnginePtr = Engine.Get()]()
	{
		FBattleC09BWildFlowEngineFixture::AdvanceStateVersion(*EnginePtr);
	});
	const FCheckpointObservation Before = ObserveCheckpoint(
		*Engine,
		MakeNumericId<FBattlerId>(PlayerLeftValue));
	const FBattleResolution Rejected = Engine->ExecuteCurrentWildAction();
	VerifyRejectedCheckpoint(
		*this,
		*Engine,
		MakeNumericId<FBattlerId>(PlayerLeftValue),
		Before,
		Before.StateVersion + 1,
		EBattleRejectionReason::StaleCheckpointIdentity,
		Rejected);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023D1RandomCommitFailureTest,
	"PokemonSolarus.Battle.ADR0002.3D1.WildActions.Failure.RandomCommit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023D1RandomCommitFailureTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FAtomicWildScenario Scenario;
	TUniquePtr<FBattleEngine> Engine;
	FFaultBattleRandom* Random = nullptr;
	if (!TestTrue(TEXT("Random-commit failure engine is created"),
		TryMakeFaultEngine(
			Scenario,
			{0},
			EFaultRandomMode::Commit,
			Engine,
			Random))
		|| !TestTrue(TEXT("Random-commit failure turn locks"),
			LockTurn(*Engine, PlayerLeftValue, EBattleActionKind::Run))
		|| !TestTrue(TEXT("Random-commit failure action starts"),
			BeginExpectedWildAction(*Engine, PlayerLeftValue, EBattleActionKind::Run)))
	{
		return false;
	}
	check(Random != nullptr);
	const FCheckpointObservation Before = ObserveCheckpoint(
		*Engine,
		MakeNumericId<FBattlerId>(PlayerLeftValue));
	const FBattleResolution Rejected = Engine->ExecuteCurrentWildAction();
	VerifyRejectedCheckpoint(
		*this,
		*Engine,
		MakeNumericId<FBattlerId>(PlayerLeftValue),
		Before,
		Before.StateVersion,
		EBattleRejectionReason::RandomTransactionCommitFailed,
		Rejected);
	return true;
}

} // namespace BattleAtomicWildActionTestsPrivate

#endif
