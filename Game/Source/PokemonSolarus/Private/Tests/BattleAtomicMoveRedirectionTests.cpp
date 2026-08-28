#if WITH_DEV_AUTOMATION_TESTS

#include "Battle/BattleMoveRedirection.h"
#include "BattleAtomicCheckpointTestFaults.h"
#include "BattleAtomicMoveCheckpointTestSupport.h"
#include "BattleAtomicSwitchTestSupport.h"

namespace BattleAtomicMoveRedirectionTestsPrivate
{
	using namespace BattleAtomicCheckpointTestCommonPrivate;
	using namespace BattleAtomicCheckpointTestFaultsPrivate;
	using namespace BattleAtomicMoveCheckpointTestSupportPrivate;
	using namespace BattleAtomicSwitchTestSupportPrivate;

	bool TrySeedRegistration(
		FBattleEngine& Engine,
		const uint64 BattlerValue,
		const uint64 ActionValue)
	{
		FBattleEngineState& State =
			FBattleC09BWildFlowEngineFixture::GetMutableState(Engine);
		const FBattlerId BattlerId = MakeNumericId<FBattlerId>(BattlerValue);
		const FBattleActivePositionState* Active = State.ActivePositions.FindByPredicate(
			[BattlerId](const FBattleActivePositionState& Candidate)
			{
				return Candidate.BattlerId == BattlerId;
			});
		return Active != nullptr
			&& FBattleMoveRedirection::TryRegister(
				State.Format,
				State.TurnId,
				MakeNumericId<FActionId>(ActionValue),
				{Active->ActiveSlotId, BattlerId},
				State.Battlers,
				State.ActivePositions,
				State.MoveRedirectionRegistrations)
				== EBattleMoveRedirectionRegistrationOutcome::Registered;
	}

	TArray<FBattleMoveRedirectionRegistration> CopyRegistrations(
		const FBattleEngine& Engine)
	{
		return FBattleC09BWildFlowEngineFixture::GetState(Engine)
			.MoveRedirectionRegistrations;
	}

	bool RegistrationsMatch(
		const FBattleEngine& Engine,
		const TConstArrayView<FBattleMoveRedirectionRegistration> Expected)
	{
		return FBattleMoveRedirection::AreRegistrationsIdentical(
			FBattleC09BWildFlowEngineFixture::GetState(Engine)
				.MoveRedirectionRegistrations,
			Expected);
	}

	bool TryPrepareEffectsCheckpoint(FBattleEngine& Engine, const FMoveId MoveId)
	{
		return TryPrepareTargetCheckpoint(Engine, MoveId)
			&& Engine.ResolveCurrentMoveTargets().WasAccepted();
	}

	bool TryMakeDoubleOpponentReserveStrictEngine(
		const FAtomicWildScenario& Scenario,
		TArray<FBattleExpectedRandomDraw> ExpectedDraws,
		TUniquePtr<FBattleEngine>& OutEngine,
		FStrictBattleRandom*& OutRandom)
	{
		FBattleSetupInput Input = MakeSetupInput(Scenario);
		FBattlePartyEntrySetup* Reserve = Input.PartyEntries.FindByPredicate(
			[](const FBattlePartyEntrySetup& Entry)
			{
				return Entry.BattlerId
					== MakeNumericId<FBattlerId>(OpponentReserveValue);
			});
		if (Reserve == nullptr)
		{
			return false;
		}
		Reserve->PartySlotId = MakePartySlotId(2);
		FBattleSetup Setup;
		EBattleSetupValidationError SetupError = EBattleSetupValidationError::None;
		if (!FBattleSetup::TryCreate(Input, Setup, SetupError))
		{
			return false;
		}
		TUniquePtr<FStrictBattleRandom> Strict =
			MakeUnique<FStrictBattleRandom>(MoveTemp(ExpectedDraws));
		OutRandom = Strict.Get();
		TUniquePtr<IBattleRandom> Random = MoveTemp(Strict);
		FBattleRejection Rejection;
		return FBattleEngine::TryCreate(
			Setup,
			MakeCatalog(Scenario),
			MoveTemp(Random),
			OutEngine,
			Rejection);
	}

	bool TryBuildCurrentExecutionRequest(
		const FBattleEngineState& State,
		FBattleEffectExecutionRequest& OutRequest)
	{
		OutRequest = FBattleEffectExecutionRequest();
		const FBattleLockedActionState* Action =
			State.LockedActions.IsValidIndex(State.CurrentLockedActionIndex)
				? &State.LockedActions[State.CurrentLockedActionIndex]
				: nullptr;
		const FBattleMoveDefinition* Move = Action != nullptr
			? State.Catalog.FindMove(Action->Decision.GetMoveId())
			: nullptr;
		if (Action == nullptr || Move == nullptr || !Action->TargetResolution.IsSet())
		{
			return false;
		}
		OutRequest.BattleId = State.Setup.GetBattleId();
		OutRequest.TurnId = State.TurnId;
		OutRequest.ActionId = Action->ActionId;
		OutRequest.ResolutionId = MakeNumericId<FResolutionId>(8101);
		OutRequest.UserBattlerId = Action->Decision.GetActingBattlerId();
		OutRequest.UserSlotId = Action->OrderKey.ActingSlotId;
		OutRequest.Move = Move;
		OutRequest.Targets = Action->TargetResolution.GetValue().Targets;
		return true;
	}

	bool TryLockDoubleVoluntarySwitch(FBattleEngine& Engine)
	{
		FBattleRejection Rejection;
		if (!Engine.TryBeginActionDecisionSequence(Rejection))
		{
			return false;
		}
		int32 Guard = 0;
		while (!Engine.GetPendingDecisionRequests().IsEmpty() && Guard++ < 4)
		{
			const TArray<FBattleDecisionRequest> Requests =
				Engine.GetPendingDecisionRequests();
			TArray<FBattleDecision> Decisions;
			for (const FBattleDecisionRequest& Request : Requests)
			{
				FBattleDecision Decision;
				if (Request.GetActingBattlerId()
					== MakeNumericId<FBattlerId>(PlayerLeftValue))
				{
					if (!FBattleDecision::TryCreateSwitch(
							Request.GetStateVersion(),
							EBattleDecisionRequestKind::Action,
							Request.GetDecisionOwnerTrainerId(),
							Request.GetActingBattlerId(),
							MakePartySlotId(2),
							MakeActiveSlotId(
								EBattleSide::Player,
								EBattlePosition::Left),
							Decision))
					{
						return false;
					}
				}
				else
				{
					Decision = MakeDecision(Request, EBattleActionKind::Fight);
				}
				Decisions.Add(MoveTemp(Decision));
			}
			if (!Engine.SubmitDecisionBatch(
					MakeBatch(Requests, MoveTemp(Decisions))).WasAccepted())
			{
				return false;
			}
		}
		return Guard < 4 && Engine.GetSnapshot().GetPhase() == EBattlePhase::Locked;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC10R2ExecutorTargetRollbackTest,
		"PokemonSolarus.Battle.C04B.C10Redirection.Atomic.ExecutorAndTargetRollback",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FBattleC10R2ExecutorTargetRollbackTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		const FMoveId ForcedMoveId =
			MakeDefinitionId<FMoveId>(ForcedEntryProbeMoveName);
		FAtomicWildScenario ExecutorScenario = MakePreMoveScenario(ForcedMoveId);
		ExecutorScenario.Format = EBattleFormat::Double;
		ExecutorScenario.bTrainerEncounter = true;
		ExecutorScenario.bOpponentSwitchReserve = true;
		TUniquePtr<FBattleEngine> ExecutorEngine;
		FStrictBattleRandom* ParentRandom = nullptr;
		if (!TestTrue(TEXT("The executor rollback engine is created"),
				TryMakeDoubleOpponentReserveStrictEngine(
					ExecutorScenario,
					{},
					ExecutorEngine,
					ParentRandom))
			|| !TestTrue(TEXT("The forced-switch request reaches committed targets"),
				TryPrepareEffectsCheckpoint(*ExecutorEngine, ForcedMoveId))
			|| !TestTrue(TEXT("The executor rollback registration is seeded"),
				TrySeedRegistration(*ExecutorEngine, OpponentLeftValue, 8201)))
		{
			return false;
		}
		FBattleEngineState& ExecutorState =
			FBattleC09BWildFlowEngineFixture::GetMutableState(*ExecutorEngine);
		ExecutorState.NextTriggerReentrancyToken =
			TNumericLimits<uint64>::Max() - 1;
		FBattleEffectExecutionRequest Request;
		if (!TestTrue(TEXT("The exact executor request is constructed"),
				TryBuildCurrentExecutionRequest(ExecutorState, Request)))
		{
			return false;
		}
		const TArray<FBattleMoveRedirectionRegistration> ExecutorBefore =
			CopyRegistrations(*ExecutorEngine);
		FStrictBattleRandom ExecutionRandom({{
			0,
			0,
			0,
			FBattleSwitchResolver::GetForcedSelectionRulePurpose()}});
		FBattleEffectExecutionPlan Plan;
		EBattleEffectExecutorError Error = EBattleEffectExecutorError::None;
		const bool bPrepared = FBattleEffectExecutor::TryPrepareAgainstState(
			Request,
			ExecutorState,
			ExecutionRandom,
			Plan,
			Error);
		bool bValid = TestFalse(TEXT("Late forced-switch executor preparation fails"),
			bPrepared);
		bValid &= TestTrue(TEXT("Failed executor preparation exposes no partial plan"),
			!Plan.Result.bValid && Plan.MoveRedirectionRegistrations.IsEmpty());
		bValid &= TestTrue(TEXT("Failed executor preparation preserves the complete array"),
			RegistrationsMatch(*ExecutorEngine, ExecutorBefore));

		FAtomicWildScenario TargetScenario = MakePreMoveScenario();
		TargetScenario.Format = EBattleFormat::Double;
		TUniquePtr<FBattleEngine> TargetEngine;
		FStrictBattleRandom* TargetRandom = nullptr;
		const FMoveId TargetMoveId = MakeDefinitionId<FMoveId>(TargetProbeMoveName);
		if (!TestTrue(TEXT("The target rollback engine is created"),
				TryMakeStrictEngine(TargetScenario, {}, TargetEngine, TargetRandom))
			|| !TestTrue(TEXT("The target checkpoint follows committed PP"),
				TryPrepareTargetCheckpoint(*TargetEngine, TargetMoveId))
			|| !TestTrue(TEXT("The target rollback registration is seeded"),
				TrySeedRegistration(*TargetEngine, OpponentLeftValue, 8202)))
		{
			return false;
		}
		FBattleEngineState& TargetState =
			FBattleC09BWildFlowEngineFixture::GetMutableState(*TargetEngine);
		TargetState.LockedActions[TargetState.CurrentLockedActionIndex].TargetClass =
			static_cast<EBattleTargetClass>(255);
		const TArray<FBattleMoveRedirectionRegistration> TargetBefore =
			CopyRegistrations(*TargetEngine);
		const FBattleResolution TargetRejected =
			TargetEngine->ResolveCurrentMoveTargets();
		bValid &= TestTrue(TEXT("Invalid target preparation is rejected with the checkpoint reason"),
			!TargetRejected.WasAccepted()
				&& TargetRejected.GetRejection().Reason
					== EBattleRejectionReason::CheckpointPreparationFailed);
		bValid &= TestTrue(TEXT("Target rollback preserves the complete registration array"),
			RegistrationsMatch(*TargetEngine, TargetBefore));
		bValid &= TestTrue(TEXT("Target rollback consumes no RNG"),
			TargetRandom != nullptr && TargetRandom->IsExact());
		return bValid;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC10R2SwitchRollbackTest,
		"PokemonSolarus.Battle.C04B.C10Redirection.Atomic.SwitchRollback",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FBattleC10R2SwitchRollbackTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		FAtomicWildScenario Scenario = MakeAtomicVoluntarySwitchScenario(
			FItemId(),
			FBattleAbilityRules::GetIntimidateId());
		Scenario.Format = EBattleFormat::Double;
		TUniquePtr<FBattleEngine> Engine;
		if (!TestTrue(TEXT("The switch rollback engine is created"),
				TryMakeSequenceEngine(Scenario, {}, Engine))
			|| !TestTrue(TEXT("The Double voluntary switch turn locks"),
				TryLockDoubleVoluntarySwitch(*Engine))
			|| !TestTrue(TEXT("Outgoing transient hooks are seeded"),
				TrySeedAtomicSwitchOutgoingTransients(*Engine))
			|| !TestTrue(TEXT("The voluntary switch action starts"),
				BeginExpectedWildAction(
					*Engine,
					PlayerLeftValue,
					EBattleActionKind::Switch))
			|| !TestTrue(TEXT("The outgoing switch registration is seeded"),
				TrySeedRegistration(*Engine, PlayerLeftValue, 8301))
			|| !TestTrue(TEXT("An unrelated switch registration is seeded"),
				TrySeedRegistration(*Engine, OpponentLeftValue, 8302)))
		{
			return false;
		}
		FBattleC09BWildFlowEngineFixture::SetNextTriggerReentrancyToken(
			*Engine,
			TNumericLimits<uint64>::Max() - 1);
		const TArray<FBattleMoveRedirectionRegistration> Before =
			CopyRegistrations(*Engine);
		const FBattleResolution Rejected = Engine->ExecuteCurrentSwitch();
		bool bValid = TestTrue(TEXT("Late switch preparation is rejected"),
			!Rejected.WasAccepted()
				&& Rejected.GetRejection().Reason
					== EBattleRejectionReason::CheckpointPreparationFailed);
		bValid &= TestTrue(TEXT("Switch rollback preserves every registration in order"),
			RegistrationsMatch(*Engine, Before));

		TUniquePtr<FBattleEngine> StaleEngine;
		FActionStartStaleRandom* StaleRandom = nullptr;
		if (!TestTrue(TEXT("The stale switch engine is created"),
				TryMakeActionStartStaleEngine(Scenario, StaleEngine, StaleRandom))
			|| !TestNotNull(TEXT("The stale switch random seam is retained"), StaleRandom)
			|| !TestTrue(TEXT("The stale Double voluntary switch turn locks"),
				TryLockDoubleVoluntarySwitch(*StaleEngine))
			|| !TestTrue(TEXT("The stale voluntary switch action starts"),
				BeginExpectedWildAction(
					*StaleEngine,
					PlayerLeftValue,
					EBattleActionKind::Switch))
			|| !TestTrue(TEXT("The stale outgoing registration is seeded"),
				TrySeedRegistration(*StaleEngine, PlayerLeftValue, 8303))
			|| !TestTrue(TEXT("The stale unrelated registration is seeded"),
				TrySeedRegistration(*StaleEngine, OpponentLeftValue, 8304)))
		{
			return false;
		}
		TArray<FBattleMoveRedirectionRegistration> StaleExpected =
			CopyRegistrations(*StaleEngine);
		const FActionId ConcurrentActionId = MakeNumericId<FActionId>(8399);
		StaleExpected[0].SourceActionId = ConcurrentActionId;
		bool bMutationSucceeded = false;
		check(StaleRandom != nullptr);
		StaleRandom->ArmAfterTraceRead(
			2,
			[EnginePtr = StaleEngine.Get(), ConcurrentActionId, &bMutationSucceeded]()
			{
				FBattleEngineState& State =
					FBattleC09BWildFlowEngineFixture::GetMutableState(*EnginePtr);
				if (!State.MoveRedirectionRegistrations.IsEmpty())
				{
					State.MoveRedirectionRegistrations[0].SourceActionId = ConcurrentActionId;
					bMutationSucceeded = true;
				}
			});
		const FBattleResolution StaleRejected = StaleEngine->ExecuteCurrentSwitch();
		const int32 TraceReads = StaleRandom->GetReadsSinceArm();
		const bool bInjected = StaleRandom->WasInjected();
		StaleRandom->Disarm();
		bValid &= TestTrue(TEXT("Registration identity changes only at the final switch recheck"),
			bInjected && bMutationSucceeded && TraceReads == 2);
		bValid &= TestTrue(TEXT("Same-version registration drift is rejected as stale"),
			!StaleRejected.WasAccepted()
				&& StaleRejected.GetRejection().Reason
					== EBattleRejectionReason::StaleCheckpointIdentity);
		bValid &= TestTrue(TEXT("Concurrent registration drift remains authoritative"),
			RegistrationsMatch(*StaleEngine, StaleExpected));
		return bValid;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC10R2PreMoveEffectRollbackTest,
		"PokemonSolarus.Battle.C04B.C10Redirection.Atomic.PreMoveAndEffectRollback",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FBattleC10R2PreMoveEffectRollbackTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		FAtomicWildScenario PreMoveScenario = MakePreMoveScenario();
		PreMoveScenario.Format = EBattleFormat::Double;
		TUniquePtr<FBattleEngine> PreMoveEngine;
		FStrictBattleRandom* PreMoveRandom = nullptr;
		if (!TestTrue(TEXT("The pre-move rollback engine is created"),
				TryMakeStrictEngine(PreMoveScenario, {}, PreMoveEngine, PreMoveRandom))
			|| !TestTrue(TEXT("The pre-move action starts"),
				TryLockAndBeginPreMove(*PreMoveEngine))
			|| !TestTrue(TEXT("Flinch is seeded for late cleanup failure"),
				TrySeedActionStartVolatile(
					*PreMoveEngine,
					MakeNumericId<FBattlerId>(PlayerLeftValue),
					FBattleVolatileRules::GetFlinchId()))
			|| !TestTrue(TEXT("The pre-move rollback registration is seeded"),
				TrySeedRegistration(*PreMoveEngine, OpponentLeftValue, 8401)))
		{
			return false;
		}
		FBattleC09BWildFlowEngineFixture::SetNextTriggerReentrancyToken(
			*PreMoveEngine,
			TNumericLimits<uint64>::Max() - 1);
		const TArray<FBattleMoveRedirectionRegistration> PreMoveBefore =
			CopyRegistrations(*PreMoveEngine);
		const FBattleResolution PreMoveRejected =
			PreMoveEngine->CommitCurrentMoveAfterPreMoveGates();
		bool bValid = TestTrue(TEXT("Late pre-move cleanup is rejected"),
			!PreMoveRejected.WasAccepted()
				&& PreMoveRejected.GetRejection().Reason
					== EBattleRejectionReason::CheckpointPreparationFailed);
		bValid &= TestTrue(TEXT("Pre-move rollback preserves the complete registration array"),
			RegistrationsMatch(*PreMoveEngine, PreMoveBefore));

		const FMoveId DamageMoveId =
			MakeDefinitionId<FMoveId>(RandomExecutionProbeMoveName);
		FAtomicWildScenario EffectScenario = MakePreMoveScenario(DamageMoveId);
		EffectScenario.Format = EBattleFormat::Double;
		EffectScenario.TargetCurrentHP = 1;
		TUniquePtr<FBattleEngine> EffectEngine;
		FStrictBattleRandom* EffectRandom = nullptr;
		const TArray<FBattleExpectedRandomDraw> Expected =
		{
			{0, 99, 0, FBattleEffectExecutor::GetAccuracyRulePurpose()},
			{0, 15, 0, FBattleEffectExecutor::GetDamageRandomRulePurpose()}
		};
		if (!TestTrue(TEXT("The effect rollback engine is created"),
				TryMakeStrictEngine(EffectScenario, Expected, EffectEngine, EffectRandom))
			|| !TestTrue(TEXT("The lethal action reaches effects"),
				TryPrepareEffectsCheckpoint(*EffectEngine, DamageMoveId))
			|| !TestTrue(TEXT("The lethal target has a cleanup hook"),
				TrySeedPreMoveMajorStatus(
					*EffectEngine,
					MakeNumericId<FBattlerId>(OpponentLeftValue),
					FBattleMajorStatusRules::GetBurnId()))
			|| !TestTrue(TEXT("The effect rollback registration is seeded"),
				TrySeedRegistration(*EffectEngine, OpponentLeftValue, 8402)))
		{
			return false;
		}
		FBattleC09BWildFlowEngineFixture::SetNextTriggerReentrancyToken(
			*EffectEngine,
			TNumericLimits<uint64>::Max() - 1);
		const TArray<FBattleMoveRedirectionRegistration> EffectBefore =
			CopyRegistrations(*EffectEngine);
		const FBattleResolution EffectRejected =
			EffectEngine->ExecuteCurrentMoveEffects();
		bValid &= TestTrue(TEXT("Late effect and faint cleanup is rejected"),
			!EffectRejected.WasAccepted()
				&& EffectRejected.GetRejection().Reason
					== EBattleRejectionReason::CheckpointPreparationFailed);
		bValid &= TestTrue(TEXT("Effect rollback restores the complete registration array"),
			RegistrationsMatch(*EffectEngine, EffectBefore));
		bValid &= TestTrue(TEXT("Effect rollback leaves lethal draws uncommitted"),
			EffectRandom != nullptr && EffectRandom->GetTrace().IsEmpty());
		return bValid;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC10R2CaptureWildRollbackTest,
		"PokemonSolarus.Battle.C04B.C10Redirection.Atomic.CaptureAndWildRollback",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FBattleC10R2CaptureWildRollbackTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		FAtomicWildScenario CaptureScenario = MakeAtomicCaptureScenario();
		CaptureScenario.Format = EBattleFormat::Double;
		TUniquePtr<FBattleEngine> CaptureEngine;
		FFaultBattleRandom* CaptureRandom = nullptr;
		if (!TestTrue(TEXT("The capture rollback engine is created"),
				TryMakeFaultEngine(
					CaptureScenario,
					{0, 0, 0, 0},
					EFaultRandomMode::Commit,
					CaptureEngine,
					CaptureRandom))
			|| !TestTrue(TEXT("The capture rollback turn locks"),
				LockTurn(*CaptureEngine, PlayerLeftValue, EBattleActionKind::Bag))
			|| !TestTrue(TEXT("The capture rollback action starts"),
				BeginExpectedWildAction(
					*CaptureEngine,
					PlayerLeftValue,
					EBattleActionKind::Bag))
			|| !TestTrue(TEXT("The capture rollback registration is seeded"),
				TrySeedRegistration(*CaptureEngine, OpponentLeftValue, 8501)))
		{
			return false;
		}
		const TArray<FBattleMoveRedirectionRegistration> CaptureBefore =
			CopyRegistrations(*CaptureEngine);
		const FBattleResolution CaptureRejected =
			CaptureEngine->ExecuteCurrentBagItem();
		bool bValid = TestFalse(TEXT("Capture random commit failure is rejected"),
			CaptureRejected.WasAccepted());
		bValid &= TestTrue(TEXT("Capture rollback preserves the complete registration array"),
			RegistrationsMatch(*CaptureEngine, CaptureBefore));
		bValid &= TestTrue(TEXT("Capture rollback leaves the parent random trace empty"),
			CaptureRandom != nullptr && CaptureRandom->GetTrace().IsEmpty());

		FAtomicWildScenario FleeScenario;
		FleeScenario.Format = EBattleFormat::Double;
		FleeScenario.WildFleeMode = EBattleWildFleeMode::Always;
		TUniquePtr<FBattleEngine> FleeEngine;
		if (!TestTrue(TEXT("The wild rollback engine is created"),
				TryMakeSequenceEngine(FleeScenario, {}, FleeEngine))
			|| !TestTrue(TEXT("The wild rollback turn locks"),
				LockTurn(*FleeEngine, OpponentLeftValue, EBattleActionKind::WildFlee))
			|| !TestTrue(TEXT("The wild rollback action starts"),
				BeginExpectedWildAction(
					*FleeEngine,
					OpponentLeftValue,
					EBattleActionKind::WildFlee))
			|| !TestTrue(TEXT("The wild rollback registration is seeded"),
				TrySeedRegistration(*FleeEngine, OpponentLeftValue, 8502))
			|| !TestTrue(TEXT("An unrelated wild rollback registration is seeded"),
				TrySeedRegistration(*FleeEngine, PlayerLeftValue, 8503)))
		{
			return false;
		}
		FBattleC09BWildFlowEngineFixture::SetNextTriggerReentrancyToken(
			*FleeEngine,
			TNumericLimits<uint64>::Max());
		const TArray<FBattleMoveRedirectionRegistration> FleeBefore =
			CopyRegistrations(*FleeEngine);
		const FBattleResolution FleeRejected =
			FleeEngine->ExecuteCurrentWildAction();
		bValid &= TestTrue(TEXT("Late wild removal cleanup is rejected"),
			!FleeRejected.WasAccepted()
				&& FleeRejected.GetRejection().Reason
					== EBattleRejectionReason::CheckpointPreparationFailed);
		bValid &= TestTrue(TEXT("Wild rollback preserves every registration in order"),
			RegistrationsMatch(*FleeEngine, FleeBefore));
		return bValid;
	}
}

#endif
