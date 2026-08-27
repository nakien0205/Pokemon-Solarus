#if WITH_DEV_AUTOMATION_TESTS

#include "BattleAtomicCheckpointTestCommon.h"
#include "BattleAtomicCheckpointTestFaults.h"
#include "BattleAtomicSwitchTestSupport.h"
#include "BattleAtomicMoveCheckpointTestSupport.h"

namespace BattleAtomicMoveTargetTestsPrivate
{
	using namespace BattleAtomicCheckpointTestCommonPrivate;
	using namespace BattleAtomicCheckpointTestFaultsPrivate;
	using namespace BattleAtomicSwitchTestSupportPrivate;
	using namespace BattleAtomicMoveCheckpointTestSupportPrivate;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E5DeterministicResolvedAtomicTest,
	"PokemonSolarus.Battle.ADR0002.3E5.Targets.Success.DeterministicResolvedAtomic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E5DeterministicResolvedAtomicTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	const FMoveId MoveId = MakeDefinitionId<FMoveId>(TargetProbeMoveName);
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	TUniquePtr<FBattleEngine> Engine;
	FStrictBattleRandom* Random = nullptr;
	if (!TestTrue(TEXT("Deterministic target engine is created"),
			TryMakeStrictEngine(MakePreMoveScenario(), {}, Engine, Random))
		|| !TestNotNull(TEXT("Deterministic strict RNG is retained"), Random)
		|| !TestTrue(TEXT("Deterministic target checkpoint follows committed PP"),
			TryPrepareTargetCheckpoint(*Engine, MoveId)))
	{
		return false;
	}

	const FTargetCheckpointObservation Before = ObserveTargetCheckpoint(*Engine);
	const int32 PPBefore = GetPreMovePP(*Engine, ActorId, MoveId);
	const FBattleResolution Resolved = Engine->ResolveCurrentMoveTargets();
	const FBattleEngineState& State =
		FBattleC09BWildFlowEngineFixture::GetState(*Engine);
	const FBattleLockedActionState& Action =
		State.LockedActions[Before.Action.ActionIndex];
	bool bValid = TestTrue(TEXT("Deterministic target checkpoint is accepted"),
		Resolved.WasAccepted());
	bValid &= TestTrue(TEXT("Deterministic target checkpoint uses no RNG"),
		Random->IsExact() && Random->GetTrace().IsEmpty());
	bValid &= TestEqual(TEXT("3E4 PP remains spent after target resolution"),
		GetPreMovePP(*Engine, ActorId, MoveId), PPBefore);
	bValid &= TestTrue(TEXT("Target and TargetsResolved event commit together"),
		Action.TargetResolution.IsSet()
			&& Action.TargetResolution.GetValue().Outcome
				== EBattleTargetResolutionOutcome::Resolved
			&& Action.TargetResolution.GetValue().Targets.Num() == 1
			&& Resolved.GetEvents().Num() == 1
			&& Resolved.GetEvents()[0].GetType()
				== EBattleEventType::TargetsResolved
			&& Resolved.GetEvents()[0].GetTargets().Num() == 1);
	bValid &= TestTrue(TEXT("Resolved action remains current and ready for effects"),
		State.CurrentLockedActionIndex == Before.Action.ActionIndex
			&& !Action.bFinished
			&& Action.EffectExecutionState
				== EBattleLockedEffectExecutionState::Pending);
	bValid &= TestEqual(TEXT("Accepted target checkpoint advances version once"),
		State.StateVersion, Before.Action.StateVersion + 1);
	bValid &= TestTrue(TEXT("Accepted target resolution is published exactly once"),
		IsReturnedResolutionAppendedExactlyOnce(*Engine, Resolved));
	bValid &= TestEqual(TEXT("Accepted target replay remains schema 6"),
		Engine->ExportReplayRecord().GetSchemaVersion(), static_cast<uint32>(6));
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E5RandomSuccessTest,
	"PokemonSolarus.Battle.ADR0002.3E5.Targets.Success.RandomTwoAndOneCandidateTransactional",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E5RandomSuccessTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FMoveId MoveId = MakeDefinitionId<FMoveId>(RandomTargetProbeMoveName);
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	auto RunCase = [&](const EBattleFormat Format,
		const uint32 Maximum,
		const uint32 Result,
		const FBattlerId ExpectedTarget) -> bool
	{
		FAtomicWildScenario Scenario = MakePreMoveScenario(MoveId);
		Scenario.Format = Format;
		TUniquePtr<FBattleEngine> Engine;
		FStrictBattleRandom* Random = nullptr;
		if (!TestTrue(TEXT("Random target engine is created"),
				TryMakeStrictEngine(
					Scenario,
					{MakeTargetExpectedDraw(Maximum, Result)},
					Engine,
					Random))
			|| !TestNotNull(TEXT("Random strict RNG is retained"), Random)
			|| !TestTrue(TEXT("Random target checkpoint follows committed PP"),
				TryPrepareTargetCheckpoint(*Engine, MoveId)))
		{
			return false;
		}
		const int32 PPBefore = GetPreMovePP(*Engine, ActorId, MoveId);
		const FBattleResolution Resolution = Engine->ResolveCurrentMoveTargets();
		const TOptional<FBattleLockedAction> Current = Engine->GetCurrentLockedAction();
		bool bCase = TestTrue(TEXT("Random target checkpoint is accepted"),
			Resolution.WasAccepted());
		bCase &= TestTrue(TEXT("Random target draw contract is exact"),
			Random->IsExact()
				&& Random->GetTrace().Num() == 1
				&& Random->GetTrace()[0].InclusiveMinimum == 0
				&& Random->GetTrace()[0].InclusiveMaximum == Maximum
				&& Random->GetTrace()[0].RulePurpose
					== FBattleTargetResolver::GetRandomLegalOpponentRulePurpose());
		bCase &= TestTrue(TEXT("Committed random trace and frozen target agree"),
			Current.IsSet()
				&& Current->TargetResolution.IsSet()
				&& Current->TargetResolution.GetValue().Targets.Num() == 1
				&& Current->TargetResolution.GetValue().Targets[0].GetBattler().BattlerId
					== ExpectedTarget);
		bCase &= TestEqual(TEXT("Random targeting does not charge PP again"),
			GetPreMovePP(*Engine, ActorId, MoveId), PPBefore);
		bCase &= TestTrue(TEXT("Random target resolution publishes exactly once"),
			IsReturnedResolutionAppendedExactlyOnce(*Engine, Resolution));
		return bCase;
	};

	bool bValid = RunCase(
		EBattleFormat::Double,
		1,
		1,
		MakeNumericId<FBattlerId>(OpponentRightValue));
	bValid &= RunCase(
		EBattleFormat::Single,
		0,
		0,
		MakeNumericId<FBattlerId>(OpponentLeftValue));
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E5EmptyRandomTest,
	"PokemonSolarus.Battle.ADR0002.3E5.Targets.Success.EmptyRandomNoDrawNoTargetExactOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E5EmptyRandomTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FMoveId MoveId = MakeDefinitionId<FMoveId>(RandomTargetProbeMoveName);
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	const FBattlerId TargetId = MakeNumericId<FBattlerId>(OpponentLeftValue);
	TUniquePtr<FBattleEngine> Engine;
	FFaultBattleRandom* Random = nullptr;
	if (!TestTrue(TEXT("Empty-random engine is created"),
			TryMakeStrictFaultEngine(
				MakePreMoveScenario(MoveId),
				{},
				EFaultRandomMode::PassThrough,
				Engine,
				Random))
		|| !TestNotNull(TEXT("Empty-random transaction source is retained"), Random)
		|| !TestTrue(TEXT("Empty-random target checkpoint follows committed PP"),
			TryPrepareTargetCheckpoint(*Engine, MoveId))
		|| !TestTrue(TEXT("Empty-random legal set is removed"),
			TryMarkTargetFainted(*Engine, TargetId)))
	{
		return false;
	}

	const FTargetCheckpointObservation Before = ObserveTargetCheckpoint(*Engine);
	const int32 PPBefore = GetPreMovePP(*Engine, ActorId, MoveId);
	const FBattleResolution NoTarget = Engine->ResolveCurrentMoveTargets();
	const FBattleEngineState& State =
		FBattleC09BWildFlowEngineFixture::GetState(*Engine);
	const FBattleLockedActionState& Action =
		State.LockedActions[Before.Action.ActionIndex];
	const TArray<EBattleEventType> ExpectedOrder = {
		EBattleEventType::TargetsResolved,
		EBattleEventType::ActionCanceled,
		EBattleEventType::ActionCompleted};
	bool bValid = TestTrue(TEXT("Empty random set is accepted as no target"),
		NoTarget.WasAccepted());
	bValid &= TestTrue(TEXT("Empty random set creates and commits an empty transaction"),
		Random->IsExact()
			&& Random->GetCounters().TransactionCreateAttempts == 1
			&& Random->GetCounters().DrawAttempts == 0
			&& Random->GetCounters().CommitAttempts == 1
			&& Random->GetTrace().IsEmpty());
	bValid &= TestTrue(TEXT("No-target action completes and advances exactly once"),
		Action.TargetResolution.IsSet()
			&& Action.TargetResolution.GetValue().Outcome
				== EBattleTargetResolutionOutcome::NoLegalTarget
			&& Action.bFinished
			&& State.CurrentLockedActionIndex == Before.Action.ActionIndex + 1);
	bValid &= TestTrue(TEXT("No-target event order is exact"),
		HasExactTargetEventOrder(NoTarget, ExpectedOrder));
	bValid &= TestEqual(TEXT("Empty random no-target retains committed PP"),
		GetPreMovePP(*Engine, ActorId, MoveId), PPBefore);
	bValid &= TestTrue(TEXT("Empty random no-target publishes exactly once"),
		IsReturnedResolutionAppendedExactlyOnce(*Engine, NoTarget));
	bValid &= TestEqual(TEXT("Empty random replay remains schema 6"),
		Engine->ExportReplayRecord().GetSchemaVersion(), static_cast<uint32>(6));
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E5FaintedFallbackTest,
	"PokemonSolarus.Battle.ADR0002.3E5.Targets.Success.FaintedFallbackNoDraw",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E5FaintedFallbackTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FMoveId MoveId = MakeDefinitionId<FMoveId>(TargetProbeMoveName);
	TUniquePtr<FBattleEngine> Engine;
	FStrictBattleRandom* Random = nullptr;
	FAtomicWildScenario Scenario = MakePreMoveScenario();
	Scenario.Format = EBattleFormat::Double;
	if (!TestTrue(TEXT("Fainted-fallback engine is created"),
			TryMakeStrictEngine(Scenario, {}, Engine, Random))
		|| !TestNotNull(TEXT("Fainted-fallback strict RNG is retained"), Random)
		|| !TestTrue(TEXT("Fainted-fallback target checkpoint is prepared"),
			TryPrepareTargetCheckpoint(*Engine, MoveId))
		|| !TestTrue(TEXT("Originally selected opponent is fainted"),
			TryMarkTargetFainted(
				*Engine,
				MakeNumericId<FBattlerId>(OpponentLeftValue))))
	{
		return false;
	}

	const FBattleResolution Resolution = Engine->ResolveCurrentMoveTargets();
	const TOptional<FBattleLockedAction> Current = Engine->GetCurrentLockedAction();
	bool bValid = TestTrue(TEXT("Fainted-target fallback is accepted"),
		Resolution.WasAccepted());
	bValid &= TestTrue(TEXT("Fainted-target fallback consumes no RNG"),
		Random->IsExact() && Random->GetTrace().IsEmpty());
	bValid &= TestTrue(TEXT("Fainted-target fallback freezes the other living opponent"),
		Current.IsSet()
			&& Current->TargetResolution.IsSet()
			&& Current->TargetResolution.GetValue().Outcome
				== EBattleTargetResolutionOutcome::Resolved
			&& Current->TargetResolution.GetValue().bWasRedirected
			&& Current->TargetResolution.GetValue().bUsedFaintedTargetFallback
			&& Current->TargetResolution.GetValue().Targets.Num() == 1
			&& Current->TargetResolution.GetValue().Targets[0].GetBattler().BattlerId
				== MakeNumericId<FBattlerId>(OpponentRightValue));
	bValid &= TestTrue(TEXT("Fainted fallback publishes target and event exactly once"),
		Resolution.GetEvents().Num() == 1
			&& Resolution.GetEvents()[0].GetType()
				== EBattleEventType::TargetsResolved
			&& IsReturnedResolutionAppendedExactlyOnce(*Engine, Resolution));
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E5ChargedNoTargetTest,
	"PokemonSolarus.Battle.ADR0002.3E5.Targets.Success.ChargedReleaseNoTargetCleanupAndPP",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E5ChargedNoTargetTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FMoveId MoveId = MakeDefinitionId<FMoveId>(ChargeProbeMoveName);
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	TUniquePtr<FBattleEngine> Engine;
	FStrictBattleRandom* Random = nullptr;
	if (!TestTrue(TEXT("Charged no-target engine is created"),
			TryMakeStrictEngine(MakePreMoveScenario(MoveId), {}, Engine, Random))
		|| !TestNotNull(TEXT("Charged no-target strict RNG is retained"), Random)
		|| !TestTrue(TEXT("Charged release reaches target checkpoint"),
			TrySeedChargedReleaseTargetCheckpoint(*Engine, MoveId))
		|| !TestTrue(TEXT("Charged release loses its legal target"),
			TryMarkTargetFainted(
				*Engine,
				MakeNumericId<FBattlerId>(OpponentLeftValue))))
	{
		return false;
	}

	const int32 PPBefore = GetPreMovePP(*Engine, ActorId, MoveId);
	const FBattleResolution NoTarget = Engine->ResolveCurrentMoveTargets();
	const FBattleEngineState& State =
		FBattleC09BWildFlowEngineFixture::GetState(*Engine);
	bool bValid = TestTrue(TEXT("Charged release no-target is accepted"),
		NoTarget.WasAccepted());
	bValid &= TestEqual(TEXT("Charged release no-target has no second PP cost"),
		GetPreMovePP(*Engine, ActorId, MoveId), PPBefore);
	bValid &= TestFalse(TEXT("Charged release no-target emits no PP event"),
		HasEvent(NoTarget, EBattleEventType::PPConsumed));
	bValid &= TestFalse(TEXT("Charged release no-target clears Charging"),
		FindPreMoveVolatile(
			*Engine,
			ActorId,
			FBattleVolatileRules::GetChargingId()) != nullptr);
	bValid &= TestFalse(TEXT("Charged release no-target clears semi-invulnerability"),
		FindPreMoveVolatile(
			*Engine,
			ActorId,
			FBattleVolatileRules::GetFlySemiInvulnerableId()) != nullptr);
	bValid &= TestEqual(TEXT("Charging trigger registrations are cleaned"),
		CountActionStartTriggerRegistrations(
			State,
			FBattleVolatileRules::GetChargingId().GetDefinitionId()), 0);
	bValid &= TestEqual(TEXT("Semi-invulnerability trigger registrations are cleaned"),
		CountActionStartTriggerRegistrations(
			State,
			FBattleVolatileRules::GetFlySemiInvulnerableId().GetDefinitionId()), 0);
	bValid &= TestTrue(TEXT("Charged no-target consumes no targeting RNG"),
		Random->IsExact() && Random->GetTrace().IsEmpty());
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E5NoTargetBoundaryTest,
	"PokemonSolarus.Battle.ADR0002.3E5.Targets.Success.NoTargetBoundaryCursorEventOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E5NoTargetBoundaryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FMoveId MoveId = MakeDefinitionId<FMoveId>(TargetProbeMoveName);
	const FBattlerId OpponentLeft = MakeNumericId<FBattlerId>(OpponentLeftValue);

	FAtomicWildScenario EndScenario = MakePreMoveScenario();
	EndScenario.PlayerLeftSpeed = 1;
	TUniquePtr<FBattleEngine> EndEngine;
	FStrictBattleRandom* EndRandom = nullptr;
	if (!TestTrue(TEXT("End-of-turn boundary engine is created"),
			TryMakeStrictEngine(EndScenario, {}, EndEngine, EndRandom))
		|| !TestTrue(TEXT("Last action reaches target checkpoint"),
			TryPrepareLastTargetCheckpoint(*EndEngine, MoveId))
		|| !TestTrue(TEXT("Last action loses its legal target"),
			TryMarkTargetFainted(*EndEngine, OpponentLeft)))
	{
		return false;
	}
	const FTargetCheckpointObservation EndBefore = ObserveTargetCheckpoint(*EndEngine);
	const FBattleResolution EndResolution = EndEngine->ResolveCurrentMoveTargets();
	const FBattleEngineState& EndState =
		FBattleC09BWildFlowEngineFixture::GetState(*EndEngine);
	const TArray<EBattleEventType> EndOrder = {
		EBattleEventType::TargetsResolved,
		EBattleEventType::ActionCanceled,
		EBattleEventType::ActionCompleted};
	bool bValid = TestTrue(TEXT("Last no-target action is accepted"),
		EndResolution.WasAccepted());
	bValid &= TestTrue(TEXT("Last no-target action enters EndOfTurn exactly once"),
		EndState.Phase == EBattlePhase::EndOfTurn
			&& EndState.CurrentLockedActionIndex == EndState.LockedActions.Num()
			&& EndState.CurrentLockedActionIndex == EndBefore.Action.ActionIndex + 1);
	bValid &= TestTrue(TEXT("End-of-turn no-target event order is exact"),
		HasExactTargetEventOrder(EndResolution, EndOrder));

	FAtomicWildScenario ReplacementScenario = MakePreMoveScenario();
	ReplacementScenario.Format = EBattleFormat::Double;
	ReplacementScenario.bVoluntarySwitchFlow = true;
	ReplacementScenario.PlayerLeftSpeed = 1;
	TUniquePtr<FBattleEngine> ReplacementEngine;
	FStrictBattleRandom* ReplacementRandom = nullptr;
	if (!TestTrue(TEXT("Replacement-boundary engine is created"),
			TryMakeStrictEngine(
				ReplacementScenario,
				{},
				ReplacementEngine,
				ReplacementRandom))
		|| !TestTrue(TEXT("Replacement last action reaches target checkpoint"),
			TryPrepareLastTargetCheckpoint(*ReplacementEngine, MoveId))
		|| !TestTrue(TEXT("Replacement case faints opponent Left"),
			TryMarkTargetFainted(*ReplacementEngine, OpponentLeft))
		|| !TestTrue(TEXT("Replacement case faints opponent Right"),
			TryMarkTargetFainted(
				*ReplacementEngine,
				MakeNumericId<FBattlerId>(OpponentRightValue)))
		|| !TestTrue(TEXT("Replacement case faints player Right"),
			TryMarkTargetFainted(
				*ReplacementEngine,
				MakeNumericId<FBattlerId>(PlayerRightValue)))
		|| !TestTrue(TEXT("Replacement case opens player Right slot"),
			TryClearTargetActivePosition(
				*ReplacementEngine,
				MakeActiveSlotId(EBattleSide::Player, EBattlePosition::Right))))
	{
		return false;
	}
	const FBattleResolution Replacement =
		ReplacementEngine->ResolveCurrentMoveTargets();
	const FBattleEngineState& ReplacementState =
		FBattleC09BWildFlowEngineFixture::GetState(*ReplacementEngine);
	const TArray<EBattleEventType> ReplacementOrder = {
		EBattleEventType::TargetsResolved,
		EBattleEventType::ActionCanceled,
		EBattleEventType::ActionCompleted,
		EBattleEventType::ReplacementRequired};
	bValid &= TestTrue(TEXT("No-target replacement boundary is accepted"),
		Replacement.WasAccepted());
	bValid &= TestTrue(TEXT("Replacement boundary stages pending request facts"),
		ReplacementState.Phase == EBattlePhase::MandatoryReplacement
			&& ReplacementState.PendingReplacements.Num() == 1
			&& ReplacementState.PendingDecisionRequests.Num() == 1
			&& ReplacementState.PendingDecision.IsSet());
	bValid &= TestTrue(TEXT("Boundary events follow target cancellation and completion"),
		HasExactTargetEventOrder(Replacement, ReplacementOrder));
	bValid &= TestTrue(TEXT("Both deterministic boundary cases consume no RNG"),
		EndRandom != nullptr
			&& ReplacementRandom != nullptr
			&& EndRandom->IsExact()
			&& ReplacementRandom->IsExact());
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E5TargetPreparationFailureTest,
	"PokemonSolarus.Battle.ADR0002.3E5.Targets.Failure.TargetSpecAndResolverPreparation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E5TargetPreparationFailureTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	const FMoveId MoveId = MakeDefinitionId<FMoveId>(TargetProbeMoveName);
	TUniquePtr<FBattleEngine> Engine;
	FStrictBattleRandom* Random = nullptr;
	if (!TestTrue(TEXT("Target-preparation failure engine is created"),
			TryMakeStrictEngine(MakePreMoveScenario(), {}, Engine, Random))
		|| !TestTrue(TEXT("Target-preparation failure follows committed PP"),
			TryPrepareTargetCheckpoint(*Engine, MoveId)))
	{
		return false;
	}
	FBattleEngineState& MutableState =
		FBattleC09BWildFlowEngineFixture::GetMutableState(*Engine);
	if (!MutableState.LockedActions.IsValidIndex(MutableState.CurrentLockedActionIndex))
	{
		return false;
	}
	MutableState.LockedActions[MutableState.CurrentLockedActionIndex].TargetClass =
		static_cast<EBattleTargetClass>(255);
	const FTargetCheckpointObservation Before = ObserveTargetCheckpoint(*Engine);
	const FBattleResolution Rejected = Engine->ResolveCurrentMoveTargets();
	bool bValid = VerifyRejectedTargetCheckpoint(
		*this,
		*Engine,
		Before,
		EBattleRejectionReason::CheckpointPreparationFailed,
		Rejected);
	bValid &= TestTrue(TEXT("Rejected invalid target spec consumes no RNG"),
		Random != nullptr && Random->IsExact() && Random->GetTrace().IsEmpty());
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E5ChargeCleanupFailureTest,
	"PokemonSolarus.Battle.ADR0002.3E5.Targets.Failure.ChargeAndTriggerCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E5ChargeCleanupFailureTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	const FMoveId MoveId = MakeDefinitionId<FMoveId>(ChargeProbeMoveName);
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	TUniquePtr<FBattleEngine> Engine;
	FStrictBattleRandom* Random = nullptr;
	if (!TestTrue(TEXT("Charge-cleanup failure engine is created"),
			TryMakeStrictEngine(MakePreMoveScenario(MoveId), {}, Engine, Random))
		|| !TestTrue(TEXT("Charge-cleanup failure reaches target checkpoint"),
			TrySeedChargedReleaseTargetCheckpoint(*Engine, MoveId))
		|| !TestTrue(TEXT("Charge-cleanup failure has no legal target"),
			TryMarkTargetFainted(
				*Engine,
				MakeNumericId<FBattlerId>(OpponentLeftValue))))
	{
		return false;
	}
	FBattleC09BWildFlowEngineFixture::GetMutableState(*Engine)
		.NextTriggerReentrancyToken = TNumericLimits<uint64>::Max();
	const FTargetCheckpointObservation Before = ObserveTargetCheckpoint(*Engine);
	const int32 PPBefore = GetPreMovePP(*Engine, ActorId, MoveId);
	const FBattleResolution Rejected = Engine->ResolveCurrentMoveTargets();
	bool bValid = VerifyRejectedTargetCheckpoint(
		*this,
		*Engine,
		Before,
		EBattleRejectionReason::CheckpointPreparationFailed,
		Rejected);
	bValid &= TestEqual(TEXT("Failed charge cleanup retains committed PP"),
		GetPreMovePP(*Engine, ActorId, MoveId), PPBefore);
	bValid &= TestTrue(TEXT("Failed charge cleanup retains Charging"),
		FindPreMoveVolatile(
			*Engine,
			ActorId,
			FBattleVolatileRules::GetChargingId()) != nullptr);
	bValid &= TestTrue(TEXT("Failed charge cleanup retains semi-invulnerability"),
		FindPreMoveVolatile(
			*Engine,
			ActorId,
			FBattleVolatileRules::GetFlySemiInvulnerableId()) != nullptr);
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E5BoundaryPlanFailureTest,
	"PokemonSolarus.Battle.ADR0002.3E5.Targets.Failure.BoundaryAndPlanStaging",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E5BoundaryPlanFailureTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	const FMoveId MoveId = MakeDefinitionId<FMoveId>(TargetProbeMoveName);

	TUniquePtr<FBattleEngine> PlanEngine;
	FStrictBattleRandom* PlanRandom = nullptr;
	if (!TestTrue(TEXT("Plan-staging failure engine is created"),
			TryMakeStrictEngine(MakePreMoveScenario(), {}, PlanEngine, PlanRandom))
		|| !TestTrue(TEXT("Plan-staging failure follows committed PP"),
			TryPrepareTargetCheckpoint(*PlanEngine, MoveId)))
	{
		return false;
	}
	FBattleC09BWildFlowEngineFixture::GetMutableState(*PlanEngine).StateVersion =
		TNumericLimits<uint64>::Max();
	const FTargetCheckpointObservation PlanBefore =
		ObserveTargetCheckpoint(*PlanEngine);
	const FBattleResolution PlanRejected =
		PlanEngine->ResolveCurrentMoveTargets();
	bool bValid = VerifyRejectedTargetCheckpoint(
		*this,
		*PlanEngine,
		PlanBefore,
		EBattleRejectionReason::CheckpointPreparationFailed,
		PlanRejected);

	FAtomicWildScenario BoundaryScenario = MakePreMoveScenario();
	BoundaryScenario.Format = EBattleFormat::Double;
	BoundaryScenario.bVoluntarySwitchFlow = true;
	BoundaryScenario.PlayerLeftSpeed = 1;
	TUniquePtr<FBattleEngine> BoundaryEngine;
	FStrictBattleRandom* BoundaryRandom = nullptr;
	if (!TestTrue(TEXT("Boundary-preparation failure engine is created"),
			TryMakeStrictEngine(
				BoundaryScenario,
				{},
				BoundaryEngine,
				BoundaryRandom))
		|| !TestTrue(TEXT("Boundary-preparation failure reaches last target checkpoint"),
			TryPrepareLastTargetCheckpoint(*BoundaryEngine, MoveId))
		|| !TestTrue(TEXT("Boundary failure faints opponent Left"),
			TryMarkTargetFainted(
				*BoundaryEngine,
				MakeNumericId<FBattlerId>(OpponentLeftValue)))
		|| !TestTrue(TEXT("Boundary failure faints opponent Right"),
			TryMarkTargetFainted(
				*BoundaryEngine,
				MakeNumericId<FBattlerId>(OpponentRightValue)))
		|| !TestTrue(TEXT("Boundary failure faints player Right"),
			TryMarkTargetFainted(
				*BoundaryEngine,
				MakeNumericId<FBattlerId>(PlayerRightValue)))
		|| !TestTrue(TEXT("Boundary failure opens replacement slot"),
			TryClearTargetActivePosition(
				*BoundaryEngine,
				MakeActiveSlotId(EBattleSide::Player, EBattlePosition::Right))))
	{
		return false;
	}
	FBattleC09BWildFlowEngineFixture::GetMutableState(*BoundaryEngine).StateVersion =
		TNumericLimits<uint64>::Max();
	const FTargetCheckpointObservation BoundaryBefore =
		ObserveTargetCheckpoint(*BoundaryEngine);
	const FBattleResolution BoundaryRejected =
		BoundaryEngine->ResolveCurrentMoveTargets();
	bValid &= VerifyRejectedTargetCheckpoint(
		*this,
		*BoundaryEngine,
		BoundaryBefore,
		EBattleRejectionReason::CheckpointPreparationFailed,
		BoundaryRejected);
	bValid &= TestTrue(TEXT("Both plan and boundary failures consume no RNG"),
		PlanRandom != nullptr
			&& BoundaryRandom != nullptr
			&& PlanRandom->IsExact()
			&& BoundaryRandom->IsExact());
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E5RandomFailureTest,
	"PokemonSolarus.Battle.ADR0002.3E5.Targets.Failure.RandomTransactionCreateDrawCommit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E5RandomFailureTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FMoveId MoveId = MakeDefinitionId<FMoveId>(RandomTargetProbeMoveName);
	auto RunFault = [&](const EFaultRandomMode Mode,
		const EBattleRejectionReason ExpectedReason) -> bool
	{
		TUniquePtr<FBattleEngine> Engine;
		FFaultBattleRandom* Random = nullptr;
		if (!TestTrue(TEXT("Target RNG-fault engine is created"),
				TryMakeStrictFaultEngine(
					MakePreMoveScenario(MoveId),
					{MakeTargetExpectedDraw(0, 0)},
					Mode,
					Engine,
					Random))
			|| !TestNotNull(TEXT("Target RNG-fault source is retained"), Random)
			|| !TestTrue(TEXT("Target RNG-fault follows committed PP"),
				TryPrepareTargetCheckpoint(*Engine, MoveId)))
		{
			return false;
		}
		const FTargetCheckpointObservation Before = ObserveTargetCheckpoint(*Engine);
		const FBattleResolution Rejected = Engine->ResolveCurrentMoveTargets();
		bool bCase = VerifyRejectedTargetCheckpoint(
			*this,
			*Engine,
			Before,
			ExpectedReason,
			Rejected);
		const FFaultRandomCounters& Counters = Random->GetCounters();
		bCase &= TestEqual(TEXT("Target RNG failure creates at most one transaction"),
			Counters.TransactionCreateAttempts, 1);
		if (Mode == EFaultRandomMode::CreateTransaction)
		{
			bCase &= TestTrue(TEXT("Creation failure performs no draw or commit"),
				Counters.DrawAttempts == 0 && Counters.CommitAttempts == 0);
		}
		else if (Mode == EFaultRandomMode::Draw)
		{
			bCase &= TestTrue(TEXT("Staged-draw failure never reaches commit"),
				Counters.DrawAttempts == 1
					&& Counters.SuccessfulDraws == 0
					&& Counters.CommitAttempts == 0);
		}
		else
		{
			bCase &= TestTrue(TEXT("Commit failure follows one successful staged draw"),
				Counters.DrawAttempts == 1
					&& Counters.SuccessfulDraws == 1
					&& Counters.CommitAttempts == 1);
		}
		return bCase;
	};

	bool bValid = RunFault(
		EFaultRandomMode::CreateTransaction,
		EBattleRejectionReason::CheckpointRandomStageFailed);
	bValid &= RunFault(
		EFaultRandomMode::Draw,
		EBattleRejectionReason::CheckpointRandomStageFailed);
	bValid &= RunFault(
		EFaultRandomMode::Commit,
		EBattleRejectionReason::RandomTransactionCommitFailed);
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E5StaleIdentityTest,
	"PokemonSolarus.Battle.ADR0002.3E5.Targets.Failure.StaleExactActionActorAndTargetPosition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E5StaleIdentityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FMoveId MoveId = MakeDefinitionId<FMoveId>(RandomTargetProbeMoveName);
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	const FActiveSlotId TargetSlot =
		MakeActiveSlotId(EBattleSide::Opponent, EBattlePosition::Right);
	enum class EStaleFact : uint8
	{
		Action,
		Actor,
		TargetPosition
	};
	auto RunStale = [&](const EStaleFact Fact) -> bool
	{
		FAtomicWildScenario Scenario = MakePreMoveScenario(MoveId);
		Scenario.Format = EBattleFormat::Double;
		TUniquePtr<FBattleEngine> Engine;
		FFaultBattleRandom* Random = nullptr;
		if (!TestTrue(TEXT("Stale target engine is created"),
				TryMakeStrictFaultEngine(
					Scenario,
					{MakeTargetExpectedDraw(1, 0)},
					EFaultRandomMode::StaleAfterDraw,
					Engine,
					Random))
			|| !TestNotNull(TEXT("Stale target RNG seam is retained"), Random)
			|| !TestTrue(TEXT("Stale target case follows committed PP"),
				TryPrepareTargetCheckpoint(*Engine, MoveId)))
		{
			return false;
		}
		const FTargetCheckpointObservation Before = ObserveTargetCheckpoint(*Engine);
		FTargetCheckpointObservation ExpectedAfter = Before;
		Random->SetAfterDraw([EnginePtr = Engine.Get(), Fact, ActorId, TargetSlot]()
		{
			FBattleEngineState& State =
				FBattleC09BWildFlowEngineFixture::GetMutableState(*EnginePtr);
			switch (Fact)
			{
			case EStaleFact::Action:
				if (State.LockedActions.IsValidIndex(State.CurrentLockedActionIndex))
				{
					++State.LockedActions[State.CurrentLockedActionIndex].QueueOrdinal;
				}
				break;
			case EStaleFact::Actor:
				if (FBattleBattlerState* Actor = State.FindMutableBattler(ActorId))
				{
					--Actor->CurrentHP;
				}
				break;
			case EStaleFact::TargetPosition:
				if (FBattleActivePositionState* Position =
					State.ActivePositions.FindByPredicate(
						[TargetSlot](const FBattleActivePositionState& Candidate)
						{
							return Candidate.ActiveSlotId == TargetSlot;
						}))
				{
					Position->bAvailable = false;
				}
				break;
			}
		});

		if (Fact == EStaleFact::Action)
		{
			++ExpectedAfter.CurrentAction.QueueOrdinal;
		}
		else if (Fact == EStaleFact::Actor)
		{
			for (FTargetCheckpointBattlerObservation& Battler : ExpectedAfter.Battlers)
			{
				if (Battler.Facts.BattlerId == ActorId)
				{
					--Battler.Facts.CurrentHP;
				}
			}
			--ExpectedAfter.Mechanics.Outgoing.CurrentHP;
		}
		else
		{
			const int32 PositionIndex = ExpectedAfter.Mechanics.ActiveSlotIds.Find(TargetSlot);
			if (ExpectedAfter.Mechanics.ActiveAvailability.IsValidIndex(PositionIndex))
			{
				ExpectedAfter.Mechanics.ActiveAvailability[PositionIndex] = 0;
			}
		}

		const FBattleResolution Rejected = Engine->ResolveCurrentMoveTargets();
		bool bCase = VerifyRejectedTargetEnvelope(
			*this,
			*Engine,
			Before,
			EBattleRejectionReason::StaleCheckpointIdentity,
			Rejected);
		bCase &= TestTrue(TEXT("Stale identity follows one staged draw without parent commit"),
			Random->GetCounters().SuccessfulDraws == 1
				&& Random->GetCounters().CommitAttempts == 0
				&& Random->GetTrace().IsEmpty());
		bCase &= TestTrue(TEXT("Only the injected concurrent stale fact survives rejection"),
			AreTargetCheckpointGameplayFactsIdentical(
				ObserveTargetCheckpoint(*Engine),
				ExpectedAfter));
		return bCase;
	};

	bool bValid = RunStale(EStaleFact::Action);
	bValid &= RunStale(EStaleFact::Actor);
	bValid &= RunStale(EStaleFact::TargetPosition);
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E5RejectionPreservationTest,
	"PokemonSolarus.Battle.ADR0002.3E5.Targets.Failure.RejectionExactOncePreservesStateAndReplayFacts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E5RejectionPreservationTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	const FMoveId MoveId = MakeDefinitionId<FMoveId>(TargetProbeMoveName);
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	const FBattlerId TargetId = MakeNumericId<FBattlerId>(OpponentLeftValue);
	TUniquePtr<FBattleEngine> Engine;
	FStrictBattleRandom* Random = nullptr;
	if (!TestTrue(TEXT("Rich rejection-preservation engine is created"),
			TryMakeStrictEngine(MakePreMoveScenario(), {}, Engine, Random))
		|| !TestTrue(TEXT("Rich rejection follows committed PP"),
			TryPrepareTargetCheckpoint(*Engine, MoveId))
		|| !TestTrue(TEXT("Rich rejection seeds Charging"),
			TrySeedActionStartVolatile(
				*Engine,
				ActorId,
				FBattleVolatileRules::GetChargingId(),
				MoveId.GetDefinitionId()))
		|| !TestTrue(TEXT("Rich rejection seeds semi-invulnerability"),
			TrySeedActionStartVolatile(
				*Engine,
				ActorId,
				FBattleVolatileRules::GetFlySemiInvulnerableId()))
		|| !TestTrue(TEXT("Rich rejection seeds target status"),
			TrySeedPreMoveMajorStatus(
				*Engine,
				TargetId,
				FBattleMajorStatusRules::GetParalysisId())))
	{
		return false;
	}
	FBattleEngineState& MutableState =
		FBattleC09BWildFlowEngineFixture::GetMutableState(*Engine);
	FBattleBattlerState* Target = MutableState.FindMutableBattler(TargetId);
	if (!TestNotNull(TEXT("Rich rejection target exists"), Target)
		|| !MutableState.LockedActions.IsValidIndex(MutableState.CurrentLockedActionIndex))
	{
		return false;
	}
	Target->CurrentHP = 137;
	MutableState.LockedActions[MutableState.CurrentLockedActionIndex].TargetClass =
		static_cast<EBattleTargetClass>(255);
	const int32 PPBefore = GetPreMovePP(*Engine, ActorId, MoveId);
	const FTargetCheckpointObservation Before = ObserveTargetCheckpoint(*Engine);
	const FBattleResolution Rejected = Engine->ResolveCurrentMoveTargets();
	bool bValid = VerifyRejectedTargetCheckpoint(
		*this,
		*Engine,
		Before,
		EBattleRejectionReason::CheckpointPreparationFailed,
		Rejected);
	bValid &= TestEqual(TEXT("Rich rejection retains PP already committed by 3E4"),
		GetPreMovePP(*Engine, ActorId, MoveId), PPBefore);
	bValid &= TestTrue(TEXT("Rich rejection retains charged-release state"),
		FindPreMoveVolatile(
			*Engine,
			ActorId,
			FBattleVolatileRules::GetChargingId()) != nullptr
			&& FindPreMoveVolatile(
				*Engine,
				ActorId,
				FBattleVolatileRules::GetFlySemiInvulnerableId()) != nullptr);
	bValid &= TestTrue(TEXT("Rich rejection consumes no targeting RNG"),
		Random != nullptr && Random->IsExact() && Random->GetTrace().IsEmpty());
	return bValid;
}

} // namespace BattleAtomicMoveTargetTestsPrivate

#endif
