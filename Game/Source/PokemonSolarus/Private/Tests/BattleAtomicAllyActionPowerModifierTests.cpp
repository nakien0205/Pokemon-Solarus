#if WITH_DEV_AUTOMATION_TESTS

#include "Battle/BattleAllyActionPowerModifier.h"
#include "Battle/BattleEffectExecutor.h"
#include "Battle/BattleFieldSideConditions.h"
#include "Battle/BattleState.h"
#include "Battle/BattleEffectExecutorContext.h"
#include "BattleAtomicCheckpointTestFaults.h"
#include "BattleAtomicMoveCheckpointTestSupport.h"
#include "BattleAtomicSwitchTestSupport.h"
#include "Math/NumericLimits.h"

namespace BattleAtomicAllyActionPowerModifierTestsPrivate
{
	using namespace BattleAtomicCheckpointTestCommonPrivate;
	using namespace BattleAtomicCheckpointTestFaultsPrivate;
	using namespace BattleAtomicMoveCheckpointTestSupportPrivate;
	using namespace BattleAtomicSwitchTestSupportPrivate;

	const TCHAR* const R3AtomicSourceMoveName =
		TEXT("Move.C10R3.Atomic.Source");

	FBattleBattlerTarget FindR3AtomicTarget(
		const FBattleEngineState& State,
		const FBattlerId BattlerId)
	{
		const FBattleActivePositionState* Active =
			State.ActivePositions.FindByPredicate(
				[BattlerId](const FBattleActivePositionState& Candidate)
				{
					return Candidate.bAvailable
						&& Candidate.BattlerId == BattlerId;
				});
		return Active != nullptr
			? FBattleBattlerTarget{Active->ActiveSlotId, BattlerId}
			: FBattleBattlerTarget();
	}

	bool TrySeedR3AtomicBoundRegistration(
		FBattleEngine& Engine,
		const uint64 TargetBattlerValue,
		const uint64 SourceActionValue)
	{
		FBattleEngineState& State =
			FBattleC09BWildFlowEngineFixture::GetMutableState(Engine);
		const FBattlerId TargetBattlerId =
			MakeNumericId<FBattlerId>(TargetBattlerValue);
		const FBattleBattlerTarget Target =
			FindR3AtomicTarget(State, TargetBattlerId);
		const FBattleLockedActionState* TargetAction =
			State.LockedActions.FindByPredicate(
				[TargetBattlerId](const FBattleLockedActionState& Action)
				{
					return Action.Decision.GetActingBattlerId()
							== TargetBattlerId
						&& Action.Decision.GetActionKind()
							== EBattleActionKind::Fight
						&& !Action.bFinished;
				});
		if (!Target.IsValid() || TargetAction == nullptr)
		{
			return false;
		}
		FBattleAllyActionPowerModifierRegistration& Registration =
			State.AllyActionPowerModifierRegistrations.AddDefaulted_GetRef();
		Registration.TurnId = State.TurnId;
		Registration.SourceActionId =
			MakeNumericId<FActionId>(SourceActionValue);
		Registration.SourceMoveId = MakeDefinitionId<FMoveId>(
			R3AtomicSourceMoveName);
		Registration.TargetActionId = TargetAction->ActionId;
		Registration.Target = Target;
		Registration.MagnitudeNumerator = 3;
		Registration.MagnitudeDenominator = 2;
		return FBattleAllyActionPowerModifier::IsRegistrationCollectionValid(
			State.Format,
			State.TurnId,
			State.AllyActionPowerModifierRegistrations,
			State.Battlers,
			State.ActivePositions,
			State.LockedActions);
	}

	bool TrySeedR3AtomicQueuedNewEntryRegistration(
		FBattleEngine& Engine,
		const uint64 TargetBattlerValue,
		const uint64 SourceActionValue,
		const EBattleActionKind TargetActionKind)
	{
		FBattleEngineState& State =
			FBattleC09BWildFlowEngineFixture::GetMutableState(Engine);
		const FBattlerId TargetBattlerId =
			MakeNumericId<FBattlerId>(TargetBattlerValue);
		FBattleBattlerState* TargetBattler =
			State.FindMutableBattler(TargetBattlerId);
		const FBattleBattlerTarget Target =
			FindR3AtomicTarget(State, TargetBattlerId);
		const FBattleLockedActionState* TargetAction =
			State.LockedActions.FindByPredicate(
				[TargetBattlerId, TargetActionKind](
					const FBattleLockedActionState& Action)
				{
					return Action.Decision.GetActingBattlerId() == TargetBattlerId
						&& Action.Decision.GetActionKind() == TargetActionKind
						&& !Action.bStarted && !Action.bFinished;
				});
		if (TargetBattler == nullptr || !Target.IsValid() || TargetAction == nullptr)
		{
			return false;
		}
		TargetBattler->EnteredActiveOnTurnId = State.TurnId;
		FBattleAllyActionPowerModifierRegistration& Registration =
			State.AllyActionPowerModifierRegistrations.AddDefaulted_GetRef();
		Registration.TurnId = State.TurnId;
		Registration.SourceActionId = MakeNumericId<FActionId>(SourceActionValue);
		Registration.SourceMoveId =
			MakeDefinitionId<FMoveId>(R3AtomicSourceMoveName);
		Registration.Target = Target;
		Registration.MagnitudeNumerator = 3;
		Registration.MagnitudeDenominator = 2;
		return FBattleAllyActionPowerModifier::IsRegistrationCollectionValid(
			State.Format, State.TurnId,
			State.AllyActionPowerModifierRegistrations, State.Battlers,
			State.ActivePositions, State.LockedActions);
	}

	TArray<FBattleAllyActionPowerModifierRegistration>
	CopyR3AtomicRegistrations(const FBattleEngine& Engine)
	{
		return FBattleC09BWildFlowEngineFixture::GetState(Engine)
			.AllyActionPowerModifierRegistrations;
	}

	bool R3AtomicRegistrationsMatch(
		const FBattleEngine& Engine,
		const TConstArrayView<FBattleAllyActionPowerModifierRegistration>
			Expected)
	{
		return FBattleAllyActionPowerModifier::AreRegistrationsIdentical(
			FBattleC09BWildFlowEngineFixture::GetState(Engine)
				.AllyActionPowerModifierRegistrations,
			Expected);
	}

	bool TryPrepareR3AtomicEffectsCheckpoint(
		FBattleEngine& Engine,
		const FMoveId MoveId)
	{
		return TryPrepareTargetCheckpoint(Engine, MoveId)
			&& Engine.ResolveCurrentMoveTargets().WasAccepted();
	}

	bool TryMakeR3AtomicOpponentReserveEngine(
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

	bool TryBuildR3AtomicCurrentExecutionRequest(
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
		if (Action == nullptr
			|| Move == nullptr
			|| !Action->TargetResolution.IsSet())
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

	bool TrySeedR3AtomicElectricTerrain(FBattleEngine& Engine)
	{
		FBattleEngineState& State =
			FBattleC09BWildFlowEngineFixture::GetMutableState(Engine);
		const FConditionId TerrainId =
			FBattleFieldSideConditionRules::GetElectricTerrainId();
		if (State.Field.Terrain.IsSet())
		{
			return false;
		}
		FBattleTriggerSubject Owner = FBattleTriggerSubject::CreateField();
		FBattleTriggerSubject Source;
		const FBattlerId SourceId =
			MakeNumericId<FBattlerId>(PlayerLeftValue);
		if (!FBattleTriggerSubject::TryCreateBattler(SourceId, Source))
		{
			return false;
		}
		FBattleFieldSideTriggerRegistrationFacts Facts;
		Facts.ConditionId = TerrainId;
		Facts.PayloadId = TerrainId.GetDefinitionId();
		Facts.Owner = Owner;
		Facts.Source = Source;
		Facts.Targets.Add(Owner);
		Facts.RemainingTurns = 5;
		EBattleTriggerError TriggerError = EBattleTriggerError::None;
		if (!FBattleFieldSideConditionRules::TryRegisterTriggers(
				State.TriggerFramework, Facts, TriggerError))
		{
			return false;
		}
		FBattleConditionState Terrain;
		Terrain.ConditionId = TerrainId;
		Terrain.RemainingTurns = 5;
		Terrain.CreationOrdinal = State.NextConditionCreationOrdinal++;
		Terrain.SourceBattlerId = SourceId;
		State.Field.Terrain = MoveTemp(Terrain);
		TArray<FBattleTriggerLifecycleFact> Ignored;
		State.TriggerFramework.DrainLifecycleFacts(Ignored);
		return true;
	}

	bool TryLockR3AtomicDoubleVoluntarySwitch(FBattleEngine& Engine)
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
							MakeActiveSlotId(EBattleSide::Player,
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
		return Guard < 4
			&& Engine.GetSnapshot().GetPhase() == EBattlePhase::Locked;
	}
}

using namespace BattleAtomicAllyActionPowerModifierTestsPrivate;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC10R3ExecutorAndDamageRollbackTest,
	"PokemonSolarus.Battle.C05B.C10ActionModifiers.Atomic.ExecutorAndDamageRollback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC10R3ExecutorAndDamageRollbackTest::RunTest(
	const FString& Parameters)
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
	if (!TestTrue(TEXT("Executor rollback engine is created"),
			TryMakeR3AtomicOpponentReserveEngine(
				ExecutorScenario, {}, ExecutorEngine, ParentRandom))
		|| !TestTrue(TEXT("Forced-switch request reaches committed targets"),
			TryPrepareR3AtomicEffectsCheckpoint(
				*ExecutorEngine, ForcedMoveId))
		|| !TestTrue(TEXT("Executor rollback binding is seeded"),
			TrySeedR3AtomicBoundRegistration(
				*ExecutorEngine, OpponentLeftValue, 8201)))
	{
		return false;
	}
	FBattleEngineState& ExecutorState =
		FBattleC09BWildFlowEngineFixture::GetMutableState(*ExecutorEngine);
	ExecutorState.NextTriggerReentrancyToken =
		TNumericLimits<uint64>::Max() - 1;
	FBattleEffectExecutionRequest Request;
	if (!TestTrue(TEXT("The exact executor request is constructed"),
		TryBuildR3AtomicCurrentExecutionRequest(ExecutorState, Request)))
	{
		return false;
	}
	const TArray<FBattleAllyActionPowerModifierRegistration> ExecutorBefore =
		CopyR3AtomicRegistrations(*ExecutorEngine);
	FStrictBattleRandom ExecutionRandom({{
		0, 0, 0, FBattleSwitchResolver::GetForcedSelectionRulePurpose()}});
	FBattleEffectExecutionPlan Plan;
	EBattleEffectExecutorError Error = EBattleEffectExecutorError::None;
	const bool bPrepared = FBattleEffectExecutor::TryPrepareAgainstState(
		Request, ExecutorState, ExecutionRandom, Plan, Error);
	bool bValid = TestFalse(TEXT("Late executor preparation fails"), bPrepared);
	bValid &= TestTrue(TEXT("Failed executor preparation exposes no partial array"),
		!Plan.Result.bValid
			&& Plan.AllyActionPowerModifierRegistrations.IsEmpty());
	bValid &= TestTrue(TEXT("Executor rollback preserves the ordered live array"),
		R3AtomicRegistrationsMatch(*ExecutorEngine, ExecutorBefore));
	bValid &= TestTrue(TEXT("Executor rollback leaves parent RNG untouched"),
		ParentRandom != nullptr && ParentRandom->IsExact());

	const FMoveId DamageMoveId =
		MakeDefinitionId<FMoveId>(RandomExecutionProbeMoveName);
	FAtomicWildScenario DamageScenario = MakePreMoveScenario(DamageMoveId);
	DamageScenario.Format = EBattleFormat::Double;
	DamageScenario.TargetCurrentHP = 1;
	TUniquePtr<FBattleEngine> DamageEngine;
	FStrictBattleRandom* DamageRandom = nullptr;
	const TArray<FBattleExpectedRandomDraw> DamageDraws =
	{
		{0, 99, 0, FBattleEffectExecutor::GetAccuracyRulePurpose()},
		{0, 15, 0, FBattleEffectExecutor::GetDamageRandomRulePurpose()}
	};
	if (!TestTrue(TEXT("Damage rollback engine is created"),
			TryMakeStrictEngine(DamageScenario, DamageDraws,
				DamageEngine, DamageRandom))
		|| !TestTrue(TEXT("Lethal damage reaches effects"),
			TryPrepareR3AtomicEffectsCheckpoint(*DamageEngine, DamageMoveId))
		|| !TestTrue(TEXT("Matching current-action binding is seeded"),
			TrySeedR3AtomicBoundRegistration(
				*DamageEngine, PlayerLeftValue, 8202))
		|| !TestTrue(TEXT("Faint cleanup work is seeded"),
			TrySeedPreMoveMajorStatus(
				*DamageEngine,
				MakeNumericId<FBattlerId>(OpponentLeftValue),
				FBattleMajorStatusRules::GetBurnId())))
	{
		return false;
	}
	FBattleEngineState& DamageState =
		FBattleC09BWildFlowEngineFixture::GetMutableState(*DamageEngine);
	DamageState.NextTriggerReentrancyToken =
		TNumericLimits<uint64>::Max() - 1;
	const TArray<FBattleAllyActionPowerModifierRegistration> DamageBefore =
		CopyR3AtomicRegistrations(*DamageEngine);
	const FBattleBattlerState* BeforeTarget = DamageState.FindBattler(
		MakeNumericId<FBattlerId>(OpponentLeftValue));
	const int32 BeforeHP = BeforeTarget != nullptr
		? BeforeTarget->CurrentHP
		: INDEX_NONE;
	const uint64 BeforeVersion = DamageState.StateVersion;
	const int32 BeforeCursor = DamageState.CurrentLockedActionIndex;
	const FBattleResolution DamageRejected =
		DamageEngine->ExecuteCurrentMoveEffects();
	const FBattleEngineState& DamageAfter =
		FBattleC09BWildFlowEngineFixture::GetState(*DamageEngine);
	const FBattleBattlerState* AfterTarget = DamageAfter.FindBattler(
		MakeNumericId<FBattlerId>(OpponentLeftValue));
	bValid &= TestTrue(TEXT("Late damage/faint preparation is rejected"),
		!DamageRejected.WasAccepted()
			&& DamageRejected.GetRejection().Reason
				== EBattleRejectionReason::CheckpointPreparationFailed);
	bValid &= TestTrue(TEXT("Damage rollback preserves private and gameplay state"),
		R3AtomicRegistrationsMatch(*DamageEngine, DamageBefore)
			&& DamageAfter.StateVersion == BeforeVersion
			&& DamageAfter.CurrentLockedActionIndex == BeforeCursor
			&& AfterTarget != nullptr
			&& AfterTarget->CurrentHP == BeforeHP
			&& DamageAfter.LockedActions.IsValidIndex(BeforeCursor)
			&& !DamageAfter.LockedActions[BeforeCursor].bFinished);
	bValid &= TestTrue(TEXT("Damage rollback leaves parent draws uncommitted"),
		DamageRandom != nullptr && DamageRandom->GetTrace().IsEmpty());

	FAtomicWildScenario ContextScenario = MakePreMoveScenario(DamageMoveId);
	ContextScenario.Format = EBattleFormat::Double;
	TUniquePtr<FBattleEngine> ContextEngine;
	FStrictBattleRandom* ContextParentRandom = nullptr;
	if (!TestTrue(TEXT("Real damage-context engine is created"),
			TryMakeStrictEngine(ContextScenario, {},
				ContextEngine, ContextParentRandom))
		|| !TestTrue(TEXT("Real damage context reaches resolved targets"),
			TryPrepareR3AtomicEffectsCheckpoint(
				*ContextEngine, DamageMoveId))
		|| !TestTrue(TEXT("First real damage-context binding is seeded"),
			TrySeedR3AtomicBoundRegistration(
				*ContextEngine, PlayerLeftValue, 8203))
		|| !TestTrue(TEXT("Second real damage-context binding is seeded"),
			TrySeedR3AtomicBoundRegistration(
				*ContextEngine, PlayerLeftValue, 8204))
		|| !TestTrue(TEXT("Electric Terrain is seeded"),
			TrySeedR3AtomicElectricTerrain(*ContextEngine)))
	{
		return false;
	}
	FBattleEngineState& ContextState =
		FBattleC09BWildFlowEngineFixture::GetMutableState(*ContextEngine);
	FBattleEffectExecutionRequest ContextRequest;
	if (!TestTrue(TEXT("Real damage-context request is constructed"),
		TryBuildR3AtomicCurrentExecutionRequest(
			ContextState, ContextRequest)))
	{
		return false;
	}
	FBattleMoveDefinition ElectricMove = *ContextRequest.Move;
	ElectricMove.Type = EPokemonType::Electric;
	ContextRequest.Move = &ElectricMove;
	FStrictBattleRandom ContextRandom({});
	BattleEffectExecutorPrivate::FStateExecutionContext Context(
		ContextRequest, ContextState, ContextRandom);
	FBattleFinalDamageInput ProbeInput;
	FBattleFinalDamageInput ActualInput;
	const bool bProbeBuilt = Context.TryBuildDamageInput(
		ElectricMove, ContextRequest.Targets[0], false, ProbeInput);
	const bool bActualBuilt = Context.TryBuildDamageInput(
		ElectricMove, ContextRequest.Targets[0], false, ActualInput);
	const FDefinitionId TerrainRule =
		FBattleFieldSideConditionRules::GetElectricTerrainId().GetDefinitionId();
	const FDefinitionId RegistrationRule =
		MakeDefinitionId<FMoveId>(R3AtomicSourceMoveName).GetDefinitionId();
	bValid &= TestTrue(TEXT("The pre-accuracy probe excludes ally modifiers"),
		bProbeBuilt
			&& ProbeInput.PowerModifiers.Num() == 1
			&& ProbeInput.PowerModifiers[0].RuleId == TerrainRule);
	bValid &= TestTrue(
		TEXT("The actual BeforeDamage build appends both ally modifiers before terrain"),
		bActualBuilt
			&& ActualInput.PowerModifiers.Num() == 3
			&& ActualInput.PowerModifiers[0].RuleId == RegistrationRule
			&& ActualInput.PowerModifiers[0].ModifierQ12 == 6144
			&& ActualInput.PowerModifiers[1].RuleId == RegistrationRule
			&& ActualInput.PowerModifiers[1].ModifierQ12 == 6144
			&& ActualInput.PowerModifiers[2].RuleId == TerrainRule
			&& ContextRandom.IsExact()
			&& ContextParentRandom != nullptr
			&& ContextParentRandom->IsExact());

	FBattleAllyActionPowerModifierRegistration Corrupt = DamageBefore[0];
	Corrupt.MagnitudeNumerator = 1;
	Corrupt.MagnitudeDenominator = 3;
	FBattleDamageModifier Sentinel;
	Sentinel.RuleId = MakeDefinitionId<FDefinitionId>(
		TEXT("Rule.C10R3.Atomic.Sentinel"));
	TArray<FBattleDamageModifier> DamageModifiers = {Sentinel};
	bValid &= TestFalse(TEXT("A corrupt matching rational rejects damage staging"),
		FBattleAllyActionPowerModifier::AppendMatchingPowerModifiers(
			Corrupt.TurnId,
			Corrupt.TargetActionId,
			Corrupt.Target,
			true,
			{Corrupt},
			DamageModifiers));
	bValid &= TestTrue(TEXT("Rejected damage staging appends no partial modifier"),
		DamageModifiers.Num() == 1
			&& DamageModifiers[0].RuleId == Sentinel.RuleId);
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC10R3ActionCancellationRollbackTest,
	"PokemonSolarus.Battle.C05B.C10ActionModifiers.Atomic.ActionCancellationRollback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC10R3ActionCancellationRollbackTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	bool bValid = true;
	const FBattlerId PlayerLeft = MakeNumericId<FBattlerId>(PlayerLeftValue);

	FAtomicWildScenario IdentityScenario = MakePreMoveScenario();
	IdentityScenario.Format = EBattleFormat::Double;
	TUniquePtr<FBattleEngine> IdentityEngine;
	FActionStartStaleRandom* IdentityRandom = nullptr;
	if (!TestTrue(TEXT("Action-start identity engine is created"),
			TryMakeActionStartStaleEngine(
				IdentityScenario, IdentityEngine, IdentityRandom))
		|| !TestNotNull(TEXT("Action-start identity seam is retained"),
			IdentityRandom)
		|| !TestTrue(TEXT("Action-start identity turn locks"),
			LockTurn(*IdentityEngine, PlayerLeftValue,
				EBattleActionKind::Fight))
		|| !TestTrue(TEXT("Action-start identity binding is seeded"),
			TrySeedR3AtomicBoundRegistration(
				*IdentityEngine, PlayerLeftValue, 8300)))
	{
		return false;
	}
	TArray<FBattleAllyActionPowerModifierRegistration> IdentityExpected =
		CopyR3AtomicRegistrations(*IdentityEngine);
	const FActionId ConcurrentStartAction = MakeNumericId<FActionId>(8398);
	IdentityExpected[0].SourceActionId = ConcurrentStartAction;
	bool bIdentityMutationSucceeded = false;
	IdentityRandom->ArmAfterTraceRead(
		7,
		[EnginePtr = IdentityEngine.Get(), ConcurrentStartAction,
			&bIdentityMutationSucceeded]()
		{
			FBattleEngineState& State =
				FBattleC09BWildFlowEngineFixture::GetMutableState(*EnginePtr);
			if (!State.AllyActionPowerModifierRegistrations.IsEmpty())
			{
				State.AllyActionPowerModifierRegistrations[0].SourceActionId =
					ConcurrentStartAction;
				bIdentityMutationSucceeded = true;
			}
		});
	const FBattleResolution IdentityRejected =
		IdentityEngine->BeginNextLockedAction();
	const int32 IdentityTraceReads = IdentityRandom->GetReadsSinceArm();
	const bool bIdentityInjected = IdentityRandom->WasInjected();
	IdentityRandom->Disarm();
	bValid &= TestTrue(TEXT("Same-version action-start drift reaches final recheck"),
		bIdentityInjected && bIdentityMutationSucceeded
			&& IdentityTraceReads == 7);
	bValid &= TestTrue(TEXT("ActionStart compares the ordered registration identity"),
		!IdentityRejected.WasAccepted()
			&& IdentityRejected.GetRejection().Reason
				== EBattleRejectionReason::StaleCheckpointIdentity
			&& R3AtomicRegistrationsMatch(*IdentityEngine, IdentityExpected));

	FAtomicWildScenario StartScenario = MakePreMoveScenario();
	StartScenario.Format = EBattleFormat::Double;
	TUniquePtr<FBattleEngine> StartEngine;
	FStrictBattleRandom* StartRandom = nullptr;
	if (!TestTrue(TEXT("Action-start rollback engine is created"),
			TryMakeStrictEngine(StartScenario, {}, StartEngine, StartRandom))
		|| !TestTrue(TEXT("Action-start rollback turn locks"),
			LockTurn(*StartEngine, PlayerLeftValue, EBattleActionKind::Fight))
		|| !TestTrue(TEXT("Action-start rollback binding is seeded"),
			TrySeedR3AtomicBoundRegistration(
				*StartEngine, PlayerLeftValue, 8301))
		|| !TestTrue(TEXT("Recharge cancellation is seeded"),
			TrySeedActionStartVolatile(*StartEngine, PlayerLeft,
				FBattleVolatileRules::GetRechargeId())))
	{
		return false;
	}
	FBattleC09BWildFlowEngineFixture::GetMutableState(*StartEngine).StateVersion =
		TNumericLimits<uint64>::Max();
	const TArray<FBattleAllyActionPowerModifierRegistration> StartBefore =
		CopyR3AtomicRegistrations(*StartEngine);
	const FBattleResolution StartRejected = StartEngine->BeginNextLockedAction();
	bValid &= TestTrue(TEXT("Failed action-start cancellation rolls back the array"),
		!StartRejected.WasAccepted()
			&& StartRejected.GetRejection().Reason
				== EBattleRejectionReason::CheckpointPreparationFailed
			&& R3AtomicRegistrationsMatch(*StartEngine, StartBefore)
			&& StartRandom != nullptr
			&& StartRandom->IsExact());

	FAtomicWildScenario PreMoveScenario = MakePreMoveScenario();
	PreMoveScenario.Format = EBattleFormat::Double;
	TUniquePtr<FBattleEngine> PreMoveEngine;
	FStrictBattleRandom* PreMoveRandom = nullptr;
	if (!TestTrue(TEXT("Pre-move rollback engine is created"),
			TryMakeStrictEngine(PreMoveScenario, {},
				PreMoveEngine, PreMoveRandom))
		|| !TestTrue(TEXT("Pre-move action starts"),
			TryLockAndBeginPreMove(*PreMoveEngine))
		|| !TestTrue(TEXT("Flinch cancellation is seeded"),
			TrySeedActionStartVolatile(*PreMoveEngine, PlayerLeft,
				FBattleVolatileRules::GetFlinchId()))
		|| !TestTrue(TEXT("Pre-move rollback binding is seeded"),
			TrySeedR3AtomicBoundRegistration(
				*PreMoveEngine, PlayerLeftValue, 8302)))
	{
		return false;
	}
	FBattleC09BWildFlowEngineFixture::GetMutableState(*PreMoveEngine)
		.NextTriggerReentrancyToken = TNumericLimits<uint64>::Max() - 1;
	const TArray<FBattleAllyActionPowerModifierRegistration> PreMoveBefore =
		CopyR3AtomicRegistrations(*PreMoveEngine);
	const FBattleResolution PreMoveRejected =
		PreMoveEngine->CommitCurrentMoveAfterPreMoveGates();
	bValid &= TestTrue(TEXT("Failed pre-move cancellation rolls back the array"),
		!PreMoveRejected.WasAccepted()
			&& PreMoveRejected.GetRejection().Reason
				== EBattleRejectionReason::CheckpointPreparationFailed
			&& R3AtomicRegistrationsMatch(*PreMoveEngine, PreMoveBefore)
			&& PreMoveRandom != nullptr
			&& PreMoveRandom->IsExact());

	const FMoveId TargetMoveId =
		MakeDefinitionId<FMoveId>(TargetProbeMoveName);
	FAtomicWildScenario TargetScenario = MakePreMoveScenario();
	TargetScenario.Format = EBattleFormat::Double;
	TUniquePtr<FBattleEngine> TargetEngine;
	FStrictBattleRandom* TargetRandom = nullptr;
	if (!TestTrue(TEXT("Target-cancellation rollback engine is created"),
			TryMakeStrictEngine(TargetScenario, {}, TargetEngine, TargetRandom))
		|| !TestTrue(TEXT("Target cancellation reaches target checkpoint"),
			TryPrepareTargetCheckpoint(*TargetEngine, TargetMoveId))
		|| !TestTrue(TEXT("Target-cancellation binding is seeded"),
			TrySeedR3AtomicBoundRegistration(
				*TargetEngine, PlayerLeftValue, 8303))
		|| !TestTrue(TEXT("Opponent Left is removed from targeting"),
			TryMarkTargetFainted(*TargetEngine,
				MakeNumericId<FBattlerId>(OpponentLeftValue)))
		|| !TestTrue(TEXT("Opponent Right is removed from targeting"),
			TryMarkTargetFainted(*TargetEngine,
				MakeNumericId<FBattlerId>(OpponentRightValue))))
	{
		return false;
	}
	FBattleC09BWildFlowEngineFixture::GetMutableState(*TargetEngine).StateVersion =
		TNumericLimits<uint64>::Max();
	const TArray<FBattleAllyActionPowerModifierRegistration> TargetBefore =
		CopyR3AtomicRegistrations(*TargetEngine);
	const FBattleResolution TargetRejected =
		TargetEngine->ResolveCurrentMoveTargets();
	bValid &= TestTrue(TEXT("Failed target cancellation rolls back the array"),
		!TargetRejected.WasAccepted()
			&& TargetRejected.GetRejection().Reason
				== EBattleRejectionReason::CheckpointPreparationFailed
			&& R3AtomicRegistrationsMatch(*TargetEngine, TargetBefore)
			&& TargetRandom != nullptr
			&& TargetRandom->IsExact());
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC10R3SwitchFaintCaptureWildStaleRollbackTest,
	"PokemonSolarus.Battle.C05B.C10ActionModifiers.Atomic.SwitchFaintCaptureWildAndStaleIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC10R3SwitchFaintCaptureWildStaleRollbackTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	bool bValid = true;
	FAtomicWildScenario SwitchScenario = MakeAtomicVoluntarySwitchScenario(
		FItemId(), FBattleAbilityRules::GetIntimidateId());
	SwitchScenario.Format = EBattleFormat::Double;
	TUniquePtr<FBattleEngine> SwitchEngine;
	if (!TestTrue(TEXT("Switch rollback engine is created"),
			TryMakeSequenceEngine(SwitchScenario, {}, SwitchEngine))
		|| !TestTrue(TEXT("Double voluntary switch turn locks"),
			TryLockR3AtomicDoubleVoluntarySwitch(*SwitchEngine))
		|| !TestTrue(TEXT("Outgoing transient hooks are seeded"),
			TrySeedAtomicSwitchOutgoingTransients(*SwitchEngine))
		|| !TestTrue(TEXT("Voluntary switch action starts"),
			BeginExpectedWildAction(*SwitchEngine, PlayerLeftValue,
				EBattleActionKind::Switch))
		|| !TestTrue(TEXT("Unrelated pending Fight binding is seeded"),
			TrySeedR3AtomicBoundRegistration(
				*SwitchEngine, OpponentLeftValue, 8401)))
	{
		return false;
	}
	FBattleC09BWildFlowEngineFixture::SetNextTriggerReentrancyToken(
		*SwitchEngine, TNumericLimits<uint64>::Max() - 1);
	const TArray<FBattleAllyActionPowerModifierRegistration> SwitchBefore =
		CopyR3AtomicRegistrations(*SwitchEngine);
	const FBattleResolution SwitchRejected = SwitchEngine->ExecuteCurrentSwitch();
	bValid &= TestTrue(TEXT("Switch rollback preserves the ordered array"),
		!SwitchRejected.WasAccepted()
			&& SwitchRejected.GetRejection().Reason
				== EBattleRejectionReason::CheckpointPreparationFailed
			&& R3AtomicRegistrationsMatch(*SwitchEngine, SwitchBefore));

	TUniquePtr<FBattleEngine> SwitchSuccessEngine;
	if (!TestTrue(TEXT("Successful Switch cleanup engine is created"),
			TryMakeSequenceEngine(SwitchScenario, {}, SwitchSuccessEngine))
		|| !TestTrue(TEXT("Successful Switch cleanup turn locks"),
			TryLockR3AtomicDoubleVoluntarySwitch(*SwitchSuccessEngine))
		|| !TestTrue(TEXT("The outgoing new-entry binding is seeded"),
			TrySeedR3AtomicQueuedNewEntryRegistration(
				*SwitchSuccessEngine, PlayerLeftValue, 8402,
				EBattleActionKind::Switch))
		|| !TestTrue(TEXT("Successful Switch cleanup action starts"),
			BeginExpectedWildAction(*SwitchSuccessEngine, PlayerLeftValue,
				EBattleActionKind::Switch)))
	{
		return false;
	}
	const FBattleResolution SwitchAccepted =
		SwitchSuccessEngine->ExecuteCurrentSwitch();
	bValid &= TestTrue(TEXT("Successful Switch removes the outgoing binding"),
		SwitchAccepted.WasAccepted()
			&& CopyR3AtomicRegistrations(*SwitchSuccessEngine).IsEmpty());

	const FMoveId DamageMoveId =
		MakeDefinitionId<FMoveId>(RandomExecutionProbeMoveName);
	FAtomicWildScenario FaintScenario = MakePreMoveScenario(DamageMoveId);
	FaintScenario.Format = EBattleFormat::Double;
	FaintScenario.TargetCurrentHP = 1;
	TUniquePtr<FBattleEngine> FaintEngine;
	FStrictBattleRandom* FaintRandom = nullptr;
	const TArray<FBattleExpectedRandomDraw> FaintDraws =
	{
		{0, 99, 0, FBattleEffectExecutor::GetAccuracyRulePurpose()},
		{0, 15, 0, FBattleEffectExecutor::GetDamageRandomRulePurpose()}
	};
	if (!TestTrue(TEXT("Faint rollback engine is created"),
			TryMakeStrictEngine(FaintScenario, FaintDraws,
				FaintEngine, FaintRandom))
		|| !TestTrue(TEXT("Faint rollback reaches effects"),
			TryPrepareR3AtomicEffectsCheckpoint(*FaintEngine, DamageMoveId))
		|| !TestTrue(TEXT("Faint rollback target binding is seeded"),
			TrySeedR3AtomicBoundRegistration(
				*FaintEngine, OpponentLeftValue, 8402))
		|| !TestTrue(TEXT("Faint cleanup status is seeded"),
			TrySeedPreMoveMajorStatus(
				*FaintEngine,
				MakeNumericId<FBattlerId>(OpponentLeftValue),
				FBattleMajorStatusRules::GetBurnId())))
	{
		return false;
	}
	FBattleC09BWildFlowEngineFixture::SetNextTriggerReentrancyToken(
		*FaintEngine, TNumericLimits<uint64>::Max() - 1);
	const TArray<FBattleAllyActionPowerModifierRegistration> FaintBefore =
		CopyR3AtomicRegistrations(*FaintEngine);
	const FBattleResolution FaintRejected =
		FaintEngine->ExecuteCurrentMoveEffects();
	bValid &= TestTrue(TEXT("Faint rollback preserves the ordered array and parent RNG"),
		!FaintRejected.WasAccepted()
			&& FaintRejected.GetRejection().Reason
				== EBattleRejectionReason::CheckpointPreparationFailed
			&& R3AtomicRegistrationsMatch(*FaintEngine, FaintBefore)
			&& FaintRandom != nullptr
			&& FaintRandom->GetTrace().IsEmpty());

	FAtomicWildScenario CaptureScenario = MakeAtomicCaptureScenario();
	CaptureScenario.Format = EBattleFormat::Double;
	TUniquePtr<FBattleEngine> CaptureEngine;
	FFaultBattleRandom* CaptureRandom = nullptr;
	if (!TestTrue(TEXT("Capture rollback engine is created"),
			TryMakeFaultEngine(CaptureScenario, {0, 0, 0, 0},
				EFaultRandomMode::Commit, CaptureEngine, CaptureRandom))
		|| !TestTrue(TEXT("Capture rollback turn locks"),
			LockTurn(*CaptureEngine, PlayerLeftValue, EBattleActionKind::Bag))
		|| !TestTrue(TEXT("Capture rollback action starts"),
			BeginExpectedWildAction(*CaptureEngine, PlayerLeftValue,
				EBattleActionKind::Bag))
		|| !TestTrue(TEXT("Capture rollback target binding is seeded"),
			TrySeedR3AtomicBoundRegistration(
				*CaptureEngine, OpponentLeftValue, 8403)))
	{
		return false;
	}
	const TArray<FBattleAllyActionPowerModifierRegistration> CaptureBefore =
		CopyR3AtomicRegistrations(*CaptureEngine);
	const FBattleResolution CaptureRejected =
		CaptureEngine->ExecuteCurrentBagItem();
	bValid &= TestTrue(TEXT("Capture commit failure preserves array and parent RNG"),
		!CaptureRejected.WasAccepted()
			&& R3AtomicRegistrationsMatch(*CaptureEngine, CaptureBefore)
			&& CaptureRandom != nullptr
			&& CaptureRandom->GetTrace().IsEmpty());

	FAtomicWildScenario FleeScenario;
	FleeScenario.Format = EBattleFormat::Double;
	FleeScenario.WildFleeMode = EBattleWildFleeMode::Always;
	TUniquePtr<FBattleEngine> FleeEngine;
	if (!TestTrue(TEXT("Wild rollback engine is created"),
			TryMakeSequenceEngine(FleeScenario, {}, FleeEngine))
		|| !TestTrue(TEXT("Wild rollback turn locks"),
			LockTurn(*FleeEngine, OpponentLeftValue,
				EBattleActionKind::WildFlee))
		|| !TestTrue(TEXT("Wild rollback action starts"),
			BeginExpectedWildAction(*FleeEngine, OpponentLeftValue,
				EBattleActionKind::WildFlee))
		|| !TestTrue(TEXT("Wild rollback preserves a pending ally Fight binding"),
			TrySeedR3AtomicBoundRegistration(
				*FleeEngine, PlayerLeftValue, 8404)))
	{
		return false;
	}
	FBattleC09BWildFlowEngineFixture::SetNextTriggerReentrancyToken(
		*FleeEngine, TNumericLimits<uint64>::Max());
	const TArray<FBattleAllyActionPowerModifierRegistration> FleeBefore =
		CopyR3AtomicRegistrations(*FleeEngine);
	const FBattleResolution FleeRejected = FleeEngine->ExecuteCurrentWildAction();
	bValid &= TestTrue(TEXT("Wild cleanup failure preserves the ordered array"),
		!FleeRejected.WasAccepted()
			&& FleeRejected.GetRejection().Reason
				== EBattleRejectionReason::CheckpointPreparationFailed
			&& R3AtomicRegistrationsMatch(*FleeEngine, FleeBefore));

	TUniquePtr<FBattleEngine> FleeSuccessEngine;
	if (!TestTrue(TEXT("Successful WildFlee cleanup engine is created"),
			TryMakeSequenceEngine(FleeScenario, {}, FleeSuccessEngine))
		|| !TestTrue(TEXT("Successful WildFlee cleanup turn locks"),
			LockTurn(*FleeSuccessEngine, OpponentLeftValue,
				EBattleActionKind::WildFlee))
		|| !TestTrue(TEXT("The fleeing new-entry binding is seeded"),
			TrySeedR3AtomicQueuedNewEntryRegistration(
				*FleeSuccessEngine, OpponentLeftValue, 8406,
				EBattleActionKind::WildFlee))
		|| !TestTrue(TEXT("Successful WildFlee cleanup action starts"),
			BeginExpectedWildAction(*FleeSuccessEngine, OpponentLeftValue,
				EBattleActionKind::WildFlee)))
	{
		return false;
	}
	const FBattleResolution FleeAccepted =
		FleeSuccessEngine->ExecuteCurrentWildAction();
	bValid &= TestTrue(TEXT("Successful WildFlee removes the fleeing binding"),
		FleeAccepted.WasAccepted()
			&& FBattleC09BWildFlowEngineFixture::IsRemoved(
				*FleeSuccessEngine,
				MakeNumericId<FBattlerId>(OpponentLeftValue))
			&& CopyR3AtomicRegistrations(*FleeSuccessEngine).IsEmpty());

	TUniquePtr<FBattleEngine> StaleEngine;
	FActionStartStaleRandom* StaleRandom = nullptr;
	if (!TestTrue(TEXT("Stale switch engine is created"),
			TryMakeActionStartStaleEngine(
				SwitchScenario, StaleEngine, StaleRandom))
		|| !TestNotNull(TEXT("Stale random seam is retained"), StaleRandom)
		|| !TestTrue(TEXT("Stale voluntary switch turn locks"),
			TryLockR3AtomicDoubleVoluntarySwitch(*StaleEngine))
		|| !TestTrue(TEXT("Stale voluntary switch action starts"),
			BeginExpectedWildAction(*StaleEngine, PlayerLeftValue,
				EBattleActionKind::Switch))
		|| !TestTrue(TEXT("Stale identity binding is seeded"),
			TrySeedR3AtomicBoundRegistration(
				*StaleEngine, OpponentLeftValue, 8405)))
	{
		return false;
	}
	TArray<FBattleAllyActionPowerModifierRegistration> StaleExpected =
		CopyR3AtomicRegistrations(*StaleEngine);
	const FActionId ConcurrentActionId = MakeNumericId<FActionId>(8499);
	StaleExpected[0].SourceActionId = ConcurrentActionId;
	bool bMutationSucceeded = false;
	check(StaleRandom != nullptr);
	StaleRandom->ArmAfterTraceRead(
		2,
		[EnginePtr = StaleEngine.Get(), ConcurrentActionId,
			&bMutationSucceeded]()
		{
			FBattleEngineState& State =
				FBattleC09BWildFlowEngineFixture::GetMutableState(*EnginePtr);
			if (!State.AllyActionPowerModifierRegistrations.IsEmpty())
			{
				State.AllyActionPowerModifierRegistrations[0].SourceActionId =
					ConcurrentActionId;
				bMutationSucceeded = true;
			}
		});
	const FBattleResolution StaleRejected = StaleEngine->ExecuteCurrentSwitch();
	const int32 TraceReads = StaleRandom->GetReadsSinceArm();
	const bool bInjected = StaleRandom->WasInjected();
	StaleRandom->Disarm();
	bValid &= TestTrue(TEXT("Same-version registration drift reaches final recheck"),
		bInjected && bMutationSucceeded && TraceReads == 2);
	bValid &= TestTrue(TEXT("Same-version ordered-array drift is rejected as stale"),
		!StaleRejected.WasAccepted()
			&& StaleRejected.GetRejection().Reason
				== EBattleRejectionReason::StaleCheckpointIdentity
			&& R3AtomicRegistrationsMatch(*StaleEngine, StaleExpected));

	const TArray<FBattleAllyActionPowerModifierRegistration> IdentityBaseline =
		StaleExpected;
	auto RejectIdentityDifference =
		[this, &IdentityBaseline](const TCHAR* Label, auto Mutate)
		{
			TArray<FBattleAllyActionPowerModifierRegistration> Candidate =
				IdentityBaseline;
			Mutate(Candidate[0]);
			return TestFalse(Label,
				FBattleAllyActionPowerModifier::AreRegistrationsIdentical(
					IdentityBaseline, Candidate));
		};
	bValid &= RejectIdentityDifference(TEXT("Identity compares TurnId"),
		[](auto& R) { R.TurnId = MakeNumericId<FTurnId>(99); });
	bValid &= RejectIdentityDifference(TEXT("Identity compares source action"),
		[](auto& R) { R.SourceActionId = MakeNumericId<FActionId>(99); });
	bValid &= RejectIdentityDifference(TEXT("Identity compares source move"),
		[](auto& R) { R.SourceMoveId = MakeDefinitionId<FMoveId>(
			TEXT("Move.C10R3.Atomic.Other")); });
	bValid &= RejectIdentityDifference(TEXT("Identity compares target action"),
		[](auto& R) { R.TargetActionId = MakeNumericId<FActionId>(99); });
	bValid &= RejectIdentityDifference(TEXT("Identity compares target slot"),
		[](auto& R) { R.Target.ActiveSlotId = MakeActiveSlotId(
			EBattleSide::Opponent, EBattlePosition::Right); });
	bValid &= RejectIdentityDifference(TEXT("Identity compares target battler"),
		[](auto& R) { R.Target.BattlerId = MakeNumericId<FBattlerId>(99); });
	bValid &= RejectIdentityDifference(TEXT("Identity compares numerator"),
		[](auto& R) { R.MagnitudeNumerator = 4; });
	bValid &= RejectIdentityDifference(TEXT("Identity compares denominator"),
		[](auto& R) { R.MagnitudeDenominator = 4; });
	TArray<FBattleAllyActionPowerModifierRegistration> Reordered =
		IdentityBaseline;
	const FBattleAllyActionPowerModifierRegistration ReorderedSecond =
		Reordered[0];
	Reordered.Add(ReorderedSecond);
	Reordered[1].SourceActionId = MakeNumericId<FActionId>(100);
	TArray<FBattleAllyActionPowerModifierRegistration> Reversed = Reordered;
	Reversed.Swap(0, 1);
	bValid &= TestFalse(TEXT("Identity preserves registration order"),
		FBattleAllyActionPowerModifier::AreRegistrationsIdentical(
			Reordered, Reversed));
	return bValid;
}

#endif
