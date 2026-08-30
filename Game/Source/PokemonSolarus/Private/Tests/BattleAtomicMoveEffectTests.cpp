#if WITH_DEV_AUTOMATION_TESTS

#include "BattleAtomicCheckpointTestCommon.h"
#include "BattleAtomicCheckpointTestFaults.h"
#include "BattleAtomicSwitchTestSupport.h"
#include "BattleAtomicMoveCheckpointTestSupport.h"

namespace BattleAtomicMoveEffectTestsPrivate
{
	using namespace BattleAtomicCheckpointTestCommonPrivate;
	using namespace BattleAtomicCheckpointTestFaultsPrivate;
	using namespace BattleAtomicSwitchTestSupportPrivate;
	using namespace BattleAtomicMoveCheckpointTestSupportPrivate;

	bool TryPrepareEffectsCheckpoint(FBattleEngine& Engine, const FMoveId MoveId)
	{
		if (!TryPrepareTargetCheckpoint(Engine, MoveId)
			|| !Engine.ResolveCurrentMoveTargets().WasAccepted())
		{
			return false;
		}
		const FBattleEngineState& State =
			FBattleC09BWildFlowEngineFixture::GetState(Engine);
		const FBattleLockedActionState* Action =
			State.LockedActions.IsValidIndex(State.CurrentLockedActionIndex)
				? &State.LockedActions[State.CurrentLockedActionIndex]
				: nullptr;
		return Action != nullptr
			&& Action->bStarted
			&& Action->bMoveCommitted
			&& Action->TargetResolution.IsSet()
			&& Action->TargetResolution.GetValue().Outcome
				== EBattleTargetResolutionOutcome::Resolved
			&& Action->EffectExecutionState == EBattleLockedEffectExecutionState::Pending
			&& !Action->bFinished;
	}

	bool TryPrepareLastEffectsCheckpoint(FBattleEngine& Engine, const FMoveId MoveId)
	{
		return TryPrepareLastTargetCheckpoint(Engine, MoveId)
			&& Engine.ResolveCurrentMoveTargets().WasAccepted();
	}

	bool IsMoveEffectsSuccessEvent(const FBattleEvent& Event)
	{
		return Event.GetType() != EBattleEventType::ActionCanceled;
	}

	const TCHAR* const C10RemovalAtomicMoveName =
		TEXT("Move.C05B.C10Removal.AtomicLateFailure");

	FBattleMoveDefinition MakeC10RemovalAtomicMove()
	{
		FBattleMoveDefinition Move;
		Move.Id = MakeDefinitionId<FMoveId>(C10RemovalAtomicMoveName);
		Move.Type = EPokemonType::Normal;
		Move.Category = EBattleMoveCategory::Status;
		Move.bAlwaysHits = true;
		Move.BasePP = 20;
		Move.TargetClass = EBattleTargetClass::SelectedOpponent;

		FBattleMoveEffectDescriptor OptionalRemoval;
		OptionalRemoval.Order = 0;
		OptionalRemoval.Kind = EBattleMoveEffectKind::RemoveCondition;
		OptionalRemoval.Target = EBattleEffectTarget::ResolvedTarget;
		OptionalRemoval.ConditionId = FBattleVolatileRules::GetLeechSeedId();
		OptionalRemoval.ChanceNumerator = 100;
		OptionalRemoval.ChanceDenominator = 100;
		OptionalRemoval.Flags = EBattleMoveEffectFlags::OptionalIfAbsent;
		Move.Effects.Add(OptionalRemoval);

		FBattleMoveEffectDescriptor LaterStatMutation;
		LaterStatMutation.Order = 1;
		LaterStatMutation.Kind = EBattleMoveEffectKind::ModifyStatStage;
		LaterStatMutation.Target = EBattleEffectTarget::User;
		LaterStatMutation.Stat = EBattleStat::Speed;
		LaterStatMutation.MagnitudeNumerator = 1;
		LaterStatMutation.MagnitudeDenominator = 1;
		Move.Effects.Add(LaterStatMutation);
		return Move;
	}

	FBattleDefinitionCatalog MakeC10RemovalAtomicCatalog(
		const FAtomicWildScenario& Scenario)
	{
		FBattleDefinitionCatalogInput Input;
		Input.TypeChartEntries = MakeNeutralTypeChart();
		Input.Moves = {
			MakeProbeMove(),
			MakeTargetProbeMove(),
			MakePivotProbeMove(),
			MakeThawProbeMove(),
			MakeChargeProbeMove(),
			MakeRandomTargetProbeMove(),
			MakeRandomExecutionProbeMove(),
			MakeForcedEntryProbeMove(),
			MakeC10RemovalAtomicMove()};
		Input.Abilities = {
			{FBattleAbilityRules::GetBlazeId()},
			{FBattleAbilityRules::GetIntimidateId()},
			{FBattleAbilityRules::GetMagicGuardId()}};
		Input.Items = {
			{FBattleBagItemRules::GetPokeBallId(), EBattleItemKind::Capture},
			{FBattleItemRules::GetLeftoversId(), EBattleItemKind::Held},
			{FBattleItemRules::GetSitrusBerryId(), EBattleItemKind::Held},
			{FBattleItemRules::GetAirBalloonId(), EBattleItemKind::Held},
			{FBattleItemRules::GetLumBerryId(), EBattleItemKind::Held},
			{FBattleItemRules::GetChoiceBandId(), EBattleItemKind::Held},
			{FBattleItemRules::GetHeavyDutyBootsId(), EBattleItemKind::Held},
			{MakeDefinitionId<FItemId>(CaptureHeldItemName), EBattleItemKind::Held}};
		Input.Conditions = {
			{FBattleFieldSideConditionRules::GetMagicRoomId(), EBattleConditionKind::Room},
			{FBattleFieldSideConditionRules::GetSpikesId(), EBattleConditionKind::Hazard},
			{FBattleFieldSideConditionRules::GetStealthRockId(), EBattleConditionKind::Hazard}};
		for (const FConditionId& VolatileId : FBattleVolatileRules::GetCanonicalIds())
		{
			Input.Conditions.Add({VolatileId, EBattleConditionKind::Volatile});
		}
		for (const FConditionId& StatusId : FBattleMajorStatusRules::GetCanonicalIds())
		{
			Input.Conditions.Add({StatusId, EBattleConditionKind::MajorStatus});
		}
		Input.SpeciesForms = {
			MakeSpecies(PlayerSpeciesName),
			MakeSpecies(WildSpeciesName, Scenario.CatchRate)};

		FBattleDefinitionCatalog Catalog;
		TArray<FBattleCatalogDiagnostic> Diagnostics;
		const bool bCreated = FBattleDefinitionCatalog::TryCreate(
			Input,
			Catalog,
			Diagnostics);
		check(bCreated);
		return Catalog;
	}

	bool TryMakeC10RemovalAtomicEngine(
		const FAtomicWildScenario& Scenario,
		TArray<FBattleExpectedRandomDraw> ExpectedDraws,
		TUniquePtr<FBattleEngine>& OutEngine,
		FStrictBattleRandom*& OutRandom)
	{
		FBattleSetup Setup;
		EBattleSetupValidationError SetupError = EBattleSetupValidationError::None;
		if (!FBattleSetup::TryCreate(MakeSetupInput(Scenario), Setup, SetupError))
		{
			return false;
		}
		TUniquePtr<FStrictBattleRandom> Strict =
			MakeUnique<FStrictBattleRandom>(MoveTemp(ExpectedDraws));
		OutRandom = Strict.Get();
		FBattleRejection Rejection;
		return FBattleEngine::TryCreate(
			Setup,
			MakeC10RemovalAtomicCatalog(Scenario),
			MoveTemp(Strict),
			OutEngine,
			Rejection);
	}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E6DeterministicCompletedAtomicTest,
	"PokemonSolarus.Battle.ADR0002.3E6.Effects.Success.DeterministicCompletedAtomic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E6DeterministicCompletedAtomicTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	const FMoveId MoveId = MakeDefinitionId<FMoveId>(TargetProbeMoveName);
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	TUniquePtr<FBattleEngine> Engine;
	FStrictBattleRandom* Random = nullptr;
	if (!TestTrue(TEXT("Deterministic 3E6 engine is created"),
			TryMakeStrictEngine(MakePreMoveScenario(), {}, Engine, Random))
		|| !TestTrue(TEXT("PP and targets are committed before 3E6"),
			TryPrepareEffectsCheckpoint(*Engine, MoveId)))
	{
		return false;
	}
	const FBattleEngineState& Before =
		FBattleC09BWildFlowEngineFixture::GetState(*Engine);
	const int32 ActionIndex = Before.CurrentLockedActionIndex;
	const uint64 StateVersion = Before.StateVersion;
	const int32 PP = GetPreMovePP(*Engine, ActorId, MoveId);
	const FBattleTargetResolutionResult Targets =
		Before.LockedActions[ActionIndex].TargetResolution.GetValue();
	const FBattleResolution Resolution = Engine->ExecuteCurrentMoveEffects();
	const FBattleEngineState& After =
		FBattleC09BWildFlowEngineFixture::GetState(*Engine);
	bool bValid = TestTrue(TEXT("Deterministic effect checkpoint is accepted"),
		Resolution.WasAccepted());
	const int32 StatEventIndex = Resolution.GetEvents().IndexOfByPredicate(
		[](const FBattleEvent& Event)
		{
			return Event.GetType() == EBattleEventType::StatStageChanged;
		});
	const int32 CompletionEventIndex = Resolution.GetEvents().IndexOfByPredicate(
		[](const FBattleEvent& Event)
		{
			return Event.GetType() == EBattleEventType::ActionCompleted;
		});
	bValid &= TestTrue(TEXT("State and events publish together in canonical order"),
		StatEventIndex != INDEX_NONE
			&& CompletionEventIndex != INDEX_NONE
			&& StatEventIndex < CompletionEventIndex);
	bValid &= TestTrue(TEXT("The action completes and advances exactly once"),
		After.CurrentLockedActionIndex == ActionIndex + 1
			&& After.LockedActions[ActionIndex].EffectExecutionState
				== EBattleLockedEffectExecutionState::Completed
			&& After.LockedActions[ActionIndex].bFinished
			&& After.StateVersion == StateVersion + 1);
	bValid &= TestTrue(TEXT("Earlier PP and exact targets remain committed"),
		GetPreMovePP(*Engine, ActorId, MoveId) == PP
			&& ArePivotTestTargetResolutionsIdentical(
				After.LockedActions[ActionIndex].TargetResolution,
				TOptional<FBattleTargetResolutionResult>(Targets)));
	bValid &= TestTrue(TEXT("Deterministic checkpoint consumes no RNG"),
		Random != nullptr && Random->IsExact() && Random->GetTrace().IsEmpty());
	bValid &= TestTrue(TEXT("Accepted resolution is appended exactly once"),
		IsReturnedResolutionAppendedExactlyOnce(*Engine, Resolution));
	bValid &= TestEqual(TEXT("Replay schema remains 6"),
		Engine->ExportReplayRecord().GetSchemaVersion(), static_cast<uint32>(6));
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E6TransactionalRandomTraceTest,
	"PokemonSolarus.Battle.ADR0002.3E6.Effects.Success.TransactionalTopLevelAndNestedRng",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E6TransactionalRandomTraceTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	const FMoveId MoveId = MakeDefinitionId<FMoveId>(RandomExecutionProbeMoveName);
	TUniquePtr<FBattleEngine> Engine;
	FStrictBattleRandom* Random = nullptr;
	const TArray<FBattleExpectedRandomDraw> Expected =
	{
		{0, 99, 0, FBattleEffectExecutor::GetAccuracyRulePurpose()},
		{0, 15, 0, FBattleEffectExecutor::GetDamageRandomRulePurpose()},
		{2, 4, 2, FBattleMajorStatusRules::GetSleepDurationPurpose()}
	};
	if (!TestTrue(TEXT("Transactional-random 3E6 engine is created"),
			TryMakeStrictEngine(MakePreMoveScenario(MoveId), Expected, Engine, Random))
		|| !TestTrue(TEXT("Random move reaches the effects checkpoint"),
			TryPrepareEffectsCheckpoint(*Engine, MoveId)))
	{
		return false;
	}
	const FBattleResolution Resolution = Engine->ExecuteCurrentMoveEffects();
	const TArray<FBattleRandomDraw> Trace = Engine->ExportRandomTrace();
	bool bValid = TestTrue(TEXT("Random effect checkpoint is accepted"),
		Resolution.WasAccepted());
	bValid &= TestTrue(TEXT("Strict transaction consumes the complete script exactly"),
		Random != nullptr && Random->IsExact());
	bValid &= TestEqual(TEXT("Accuracy, damage, and Sleep duration commit in one trace"),
		Trace.Num(), 3);
	if (Trace.Num() == 3)
	{
		bValid &= TestTrue(TEXT("Committed ranges are exact and ordered"),
			Trace[0].InclusiveMinimum == 0 && Trace[0].InclusiveMaximum == 99
				&& Trace[0].RulePurpose == FBattleEffectExecutor::GetAccuracyRulePurpose()
				&& Trace[1].InclusiveMinimum == 0 && Trace[1].InclusiveMaximum == 15
				&& Trace[1].RulePurpose
					== FBattleEffectExecutor::GetDamageRandomRulePurpose()
				&& Trace[2].InclusiveMinimum == 2 && Trace[2].InclusiveMaximum == 4
				&& Trace[2].RulePurpose
					== FBattleMajorStatusRules::GetSleepDurationPurpose());
	}
	bValid &= TestTrue(TEXT("Random state and events commit in the same resolution"),
		HasEvent(Resolution, EBattleEventType::AccuracyChecked)
			&& HasEvent(Resolution, EBattleEventType::Damage)
			&& HasEvent(Resolution, EBattleEventType::StatusChanged)
			&& HasEvent(Resolution, EBattleEventType::ActionCompleted));
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E6ForcedEntryAtomicTest,
	"PokemonSolarus.Battle.ADR0002.3E6.Effects.Success.ForcedSwitchItemHazardAbilityCanonicalOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E6ForcedEntryAtomicTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FMoveId MoveId = MakeDefinitionId<FMoveId>(ForcedEntryProbeMoveName);
	FAtomicWildScenario Scenario = MakePreMoveScenario(MoveId);
	Scenario.bTrainerEncounter = true;
	Scenario.bOpponentSwitchReserve = true;
	Scenario.OpponentReserveAbilityId = FBattleAbilityRules::GetIntimidateId();
	Scenario.OpponentReserveHeldItemId = FBattleItemRules::GetSitrusBerryId();
	Scenario.OpponentReserveCurrentHP = 110;
	TUniquePtr<FBattleEngine> Engine;
	FStrictBattleRandom* Random = nullptr;
	if (!TestTrue(TEXT("The full forced-entry engine is created"),
			TryMakeStrictEngine(
				Scenario,
				{{0, 0, 0, FBattleSwitchResolver::GetForcedSelectionRulePurpose()}},
				Engine,
				Random))
		|| !TestTrue(TEXT("Opponent Stealth Rock is staged"),
			TrySeedAtomicSwitchHazard(
				*Engine,
				FBattleFieldSideConditionRules::GetStealthRockId(),
				1,
				EBattleSide::Opponent))
		|| !TestTrue(TEXT("The forced-entry move reaches effects"),
			TryPrepareEffectsCheckpoint(*Engine, MoveId)))
	{
		return false;
	}

	const FBattleResolution Resolution = Engine->ExecuteCurrentMoveEffects();
	const FBattleEngineState& State =
		FBattleC09BWildFlowEngineFixture::GetState(*Engine);
	const FBattleBattlerState* Incoming = State.FindBattler(
		MakeNumericId<FBattlerId>(OpponentReserveValue));
	const FBattleBattlerState* Player = State.FindBattler(
		MakeNumericId<FBattlerId>(PlayerLeftValue));
	const FBattleActivePositionState* Active = State.FindActivePosition(
		MakeActiveSlotId(EBattleSide::Opponent, EBattlePosition::Left));
	int32 PlayerAttackStage = INDEX_NONE;
	const bool bHasPlayerAttackStage = Player != nullptr
		&& Player->Stages.TryGetStage(EBattleStat::Attack, PlayerAttackStage);
	auto FindEvent = [&Resolution](const EBattleEventType Type)
	{
		return Resolution.GetEvents().IndexOfByPredicate(
			[Type](const FBattleEvent& Event)
			{
				return Event.GetType() == Type;
			});
	};
	const int32 Left = FindEvent(EBattleEventType::LeftActiveSlot);
	const int32 Entered = FindEvent(EBattleEventType::EnteredActiveSlot);
	const int32 Switched = FindEvent(EBattleEventType::Switched);
	const int32 Item = FindEvent(EBattleEventType::ItemActivated);
	const int32 Ability = FindEvent(EBattleEventType::AbilityActivated);
	const int32 Stage = FindEvent(EBattleEventType::StatStageChanged);
	const int32 Completed = FindEvent(EBattleEventType::ActionCompleted);
	bool bValid = TestTrue(TEXT("The forced-entry checkpoint is accepted"),
		Resolution.WasAccepted());
	bValid &= TestTrue(TEXT("The exact opponent reserve is active"),
		Active != nullptr
			&& Active->BattlerId
				== MakeNumericId<FBattlerId>(OpponentReserveValue));
	bValid &= TestTrue(TEXT("Hazard, Sitrus, and Intimidate state commit together"),
		Incoming != nullptr
			&& Incoming->CurrentHP == 135
			&& Incoming->HeldItem.bConsumed
			&& !Incoming->HeldItem.CurrentItemId.IsValid()
			&& Incoming->HeldItem.bRevealed
			&& bHasPlayerAttackStage
			&& PlayerAttackStage == -1);
	bValid &= TestTrue(TEXT("Switch, item, Ability, and completion keep canonical order"),
		Left != INDEX_NONE
			&& Entered > Left
			&& Switched > Entered
			&& Item > Switched
			&& Ability > Item
			&& Stage > Ability
			&& Completed > Stage);
	bValid &= TestTrue(TEXT("The one forced-selection draw commits exactly"),
		Random != nullptr && Random->IsExact());
	const TArray<FBattleRandomDraw> Trace = Engine->ExportRandomTrace();
	bValid &= TestTrue(TEXT("Forced-selection range and purpose are exact"),
		Trace.Num() == 1
			&& Trace[0].InclusiveMinimum == 0
			&& Trace[0].InclusiveMaximum == 0
			&& Trace[0].RulePurpose
				== FBattleSwitchResolver::GetForcedSelectionRulePurpose());
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E6ForcedEntryFailureTest,
	"PokemonSolarus.Battle.ADR0002.3E6.Effects.Failure.ForcedEntryConsequenceRollback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E6ForcedEntryFailureTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FMoveId MoveId = MakeDefinitionId<FMoveId>(ForcedEntryProbeMoveName);
	FAtomicWildScenario Scenario = MakePreMoveScenario(MoveId);
	Scenario.bTrainerEncounter = true;
	Scenario.bOpponentSwitchReserve = true;
	Scenario.OpponentReserveAbilityId = FBattleAbilityRules::GetIntimidateId();
	Scenario.OpponentReserveHeldItemId = FBattleItemRules::GetSitrusBerryId();
	Scenario.OpponentReserveCurrentHP = 110;
	TUniquePtr<FBattleEngine> Engine;
	FStrictBattleRandom* Random = nullptr;
	if (!TestTrue(TEXT("The forced-entry failure engine is created"),
			TryMakeStrictEngine(
				Scenario,
				{{0, 0, 0, FBattleSwitchResolver::GetForcedSelectionRulePurpose()}},
				Engine,
				Random))
		|| !TestTrue(TEXT("The forced-entry failure hazard is staged"),
			TrySeedAtomicSwitchHazard(
				*Engine,
				FBattleFieldSideConditionRules::GetStealthRockId(),
				1,
				EBattleSide::Opponent))
		|| !TestTrue(TEXT("The forced-entry failure case reaches effects"),
			TryPrepareEffectsCheckpoint(*Engine, MoveId)))
	{
		return false;
	}
	FBattleC09BWildFlowEngineFixture::GetMutableState(*Engine)
		.NextTriggerReentrancyToken = TNumericLimits<uint64>::Max() - 1;
	const FTargetCheckpointObservation Before = ObserveTargetCheckpoint(*Engine);
	const FBattleResolution Rejected = Engine->ExecuteCurrentMoveEffects();
	bool bValid = VerifyRejectedTargetCheckpoint(
		*this,
		*Engine,
		Before,
		EBattleRejectionReason::CheckpointPreparationFailed,
		Rejected);
	bValid &= TestTrue(TEXT("The forced-selection draw rolls back"),
		Random != nullptr && Random->GetTrace().IsEmpty());
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E6AwaitingPivotAtomicTest,
	"PokemonSolarus.Battle.ADR0002.3E6.Effects.Success.PivotAwaitingAtomic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E6AwaitingPivotAtomicTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FMoveId MoveId = MakeDefinitionId<FMoveId>(PivotProbeMoveName);
	TUniquePtr<FBattleEngine> Engine;
	FStrictBattleRandom* Random = nullptr;
	if (!TestTrue(TEXT("Pivot 3E6 engine is created"),
			TryMakeStrictEngine(MakeAtomicPivotSwitchScenario(), {}, Engine, Random))
		|| !TestTrue(TEXT("Pivot move reaches the effects checkpoint"),
			TryPrepareEffectsCheckpoint(*Engine, MoveId)))
	{
		return false;
	}
	const FBattleEngineState& Before =
		FBattleC09BWildFlowEngineFixture::GetState(*Engine);
	const int32 ActionIndex = Before.CurrentLockedActionIndex;
	const FBattleResolution Resolution = Engine->ExecuteCurrentMoveEffects();
	const FBattleEngineState& After =
		FBattleC09BWildFlowEngineFixture::GetState(*Engine);
	bool bValid = TestTrue(TEXT("Pivot effect checkpoint is accepted"),
		Resolution.WasAccepted());
	bValid &= TestTrue(TEXT("Exactly one PivotSwitch request is published"),
		After.PendingDecision.IsSet()
			&& After.PendingDecisionRequests.Num() == 1
			&& After.PendingDecision.GetValue().GetRequestKind()
				== EBattleDecisionRequestKind::PivotSwitch
			&& ArePivotTestRequestsIdentical(
				After.PendingDecision.GetValue(),
				After.PendingDecisionRequests[0]));
	bValid &= TestTrue(TEXT("AwaitingPivot keeps the exact action current"),
		After.CurrentLockedActionIndex == ActionIndex
			&& After.LockedActions[ActionIndex].EffectExecutionState
				== EBattleLockedEffectExecutionState::AwaitingPivot
			&& !After.LockedActions[ActionIndex].bFinished);
	bValid &= TestFalse(TEXT("AwaitingPivot publishes no completion fact"),
		HasEvent(Resolution, EBattleEventType::ActionCompleted));
	bValid &= TestTrue(TEXT("Pivot preparation consumes no RNG"),
		Random != nullptr && Random->IsExact() && Random->GetTrace().IsEmpty());
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E6CompletedBoundaryAtomicTest,
	"PokemonSolarus.Battle.ADR0002.3E6.Effects.Success.CompletedBoundaryWithoutReplacementExecution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E6CompletedBoundaryAtomicTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	const FMoveId MoveId = MakeDefinitionId<FMoveId>(TargetProbeMoveName);
	TUniquePtr<FBattleEngine> Engine;
	FStrictBattleRandom* Random = nullptr;
	FAtomicWildScenario Scenario = MakePreMoveScenario();
	Scenario.PlayerLeftSpeed = 1;
	if (!TestTrue(TEXT("Boundary 3E6 engine is created"),
			TryMakeStrictEngine(Scenario, {}, Engine, Random))
		|| !TestTrue(TEXT("Last queued action reaches effects"),
			TryPrepareLastEffectsCheckpoint(*Engine, MoveId)))
	{
		return false;
	}
	const FBattleResolution Resolution = Engine->ExecuteCurrentMoveEffects();
	const FBattleEngineState& State =
		FBattleC09BWildFlowEngineFixture::GetState(*Engine);
	bool bValid = TestTrue(TEXT("Last ordinary action is accepted"),
		Resolution.WasAccepted());
	bValid &= TestTrue(TEXT("Completion and EndOfTurn boundary publish together"),
		State.Phase == EBattlePhase::EndOfTurn
			&& State.CurrentLockedActionIndex == State.LockedActions.Num()
			&& State.PendingReplacements.IsEmpty()
			&& State.PendingDecisionRequests.IsEmpty()
			&& !State.PendingDecision.IsSet());
	bValid &= TestTrue(TEXT("Boundary checkpoint does not execute replacement selection"),
		!HasEvent(Resolution, EBattleEventType::ReplacementRequired));
	bValid &= TestTrue(TEXT("Boundary path consumes no RNG"),
		Random != nullptr && Random->IsExact());
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E6PrevalidationFailureTest,
	"PokemonSolarus.Battle.ADR0002.3E6.Effects.Failure.EffectPrevalidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E6PrevalidationFailureTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FMoveId MoveId = MakeDefinitionId<FMoveId>(TargetProbeMoveName);
	TUniquePtr<FBattleEngine> Engine;
	FStrictBattleRandom* Random = nullptr;
	if (!TestTrue(TEXT("Prevalidation-failure engine is created"),
			TryMakeStrictEngine(MakePreMoveScenario(), {}, Engine, Random))
		|| !TestTrue(TEXT("Prevalidation case reaches effects"),
			TryPrepareEffectsCheckpoint(*Engine, MoveId)))
	{
		return false;
	}
	FBattleEngineState& Mutable =
		FBattleC09BWildFlowEngineFixture::GetMutableState(*Engine);
	Mutable.LockedActions[Mutable.CurrentLockedActionIndex].TargetClass =
		static_cast<EBattleTargetClass>(255);
	const FTargetCheckpointObservation Before = ObserveTargetCheckpoint(*Engine);
	const FBattleResolution Rejected = Engine->ExecuteCurrentMoveEffects();
	bool bValid = VerifyRejectedTargetCheckpoint(
		*this,
		*Engine,
		Before,
		EBattleRejectionReason::CheckpointPreparationFailed,
		Rejected);
	bValid &= TestTrue(TEXT("Prevalidation rejection preserves parent RNG"),
		Random != nullptr && Random->IsExact() && Random->GetTrace().IsEmpty());
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E6EventPlanFailureTest,
	"PokemonSolarus.Battle.ADR0002.3E6.Effects.Failure.EventAndResolutionPlanStaging",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E6EventPlanFailureTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FMoveId MoveId = MakeDefinitionId<FMoveId>(TargetProbeMoveName);
	TUniquePtr<FBattleEngine> Engine;
	FStrictBattleRandom* Random = nullptr;
	if (!TestTrue(TEXT("Event-plan failure engine is created"),
			TryMakeStrictEngine(MakePreMoveScenario(), {}, Engine, Random))
		|| !TestTrue(TEXT("Event-plan case reaches effects"),
			TryPrepareEffectsCheckpoint(*Engine, MoveId)))
	{
		return false;
	}
	FBattleC09BWildFlowEngineFixture::GetMutableState(*Engine).NextEventOrdinal =
		TNumericLimits<uint64>::Max() - 1;
	const FTargetCheckpointObservation Before = ObserveTargetCheckpoint(*Engine);
	const FBattleResolution Rejected = Engine->ExecuteCurrentMoveEffects();
	bool bValid = VerifyRejectedTargetCheckpoint(
		*this,
		*Engine,
		Before,
		EBattleRejectionReason::CheckpointPreparationFailed,
		Rejected);
	bValid &= TestTrue(TEXT("Private event staging publishes no partial event"),
		Rejected.GetEvents().Num() == 1
			&& !Rejected.GetEvents().ContainsByPredicate(IsMoveEffectsSuccessEvent));
	bValid &= TestTrue(TEXT("Event-plan failure preserves parent RNG"),
		Random != nullptr && Random->IsExact());

	const FMoveId RandomMoveId =
		MakeDefinitionId<FMoveId>(RandomExecutionProbeMoveName);
	FAtomicWildScenario BoundaryScenario = MakePreMoveScenario(RandomMoveId);
	BoundaryScenario.PlayerLeftSpeed = 1;
	BoundaryScenario.TargetCurrentHP = 1;
	BoundaryScenario.bTrainerEncounter = true;
	BoundaryScenario.bOpponentSwitchReserve = true;
	TUniquePtr<FBattleEngine> BoundaryEngine;
	FStrictBattleRandom* BoundaryRandom = nullptr;
	const TArray<FBattleExpectedRandomDraw> LethalDraws =
	{
		{0, 99, 0, FBattleEffectExecutor::GetAccuracyRulePurpose()},
		{0, 15, 0, FBattleEffectExecutor::GetDamageRandomRulePurpose()}
	};
	if (!TestTrue(TEXT("Boundary-preparation failure engine is created"),
			TryMakeStrictEngine(
				BoundaryScenario,
				LethalDraws,
				BoundaryEngine,
				BoundaryRandom))
		|| !TestTrue(TEXT("Boundary-preparation case reaches last effects"),
			TryPrepareLastEffectsCheckpoint(*BoundaryEngine, RandomMoveId)))
	{
		return false;
	}
	FBattleTrainerState* OpponentTrainer =
		FBattleC09BWildFlowEngineFixture::GetMutableState(*BoundaryEngine)
			.FindMutableTrainer(MakeNumericId<FTrainerId>(OpponentTrainerValue));
	if (!TestNotNull(TEXT("The opponent Trainer owns a reserve slot"), OpponentTrainer)
		|| !TestTrue(TEXT("The opponent reserve slot is present"),
			OpponentTrainer->PartySlots.Num() > 1))
	{
		return false;
	}
	OpponentTrainer->PartySlots[1].BattlerId = MakeNumericId<FBattlerId>(999);
	const FTargetCheckpointObservation BoundaryBefore =
		ObserveTargetCheckpoint(*BoundaryEngine);
	const FBattleResolution BoundaryRejected =
		BoundaryEngine->ExecuteCurrentMoveEffects();
	bValid &= VerifyRejectedTargetCheckpoint(
		*this,
		*BoundaryEngine,
		BoundaryBefore,
		EBattleRejectionReason::CheckpointPreparationFailed,
		BoundaryRejected);
	bValid &= TestTrue(TEXT("Boundary failure rolls back lethal draws"),
		BoundaryRandom != nullptr && BoundaryRandom->GetTrace().IsEmpty());

	const FMoveId PivotMoveId = MakeDefinitionId<FMoveId>(PivotProbeMoveName);
	TUniquePtr<FBattleEngine> PivotEngine;
	FStrictBattleRandom* PivotRandom = nullptr;
	if (!TestTrue(TEXT("Pivot-request failure engine is created"),
			TryMakeStrictEngine(
				MakeAtomicPivotSwitchScenario(),
				{},
				PivotEngine,
				PivotRandom))
		|| !TestTrue(TEXT("Pivot-request failure reaches effects"),
			TryPrepareEffectsCheckpoint(*PivotEngine, PivotMoveId)))
	{
		return false;
	}
	FBattleTrainerState* PlayerTrainer =
		FBattleC09BWildFlowEngineFixture::GetMutableState(*PivotEngine)
			.FindMutableTrainer(MakeNumericId<FTrainerId>(PlayerTrainerValue));
	if (!TestNotNull(TEXT("The pivot owner has a reserve slot"), PlayerTrainer)
		|| !TestTrue(TEXT("The pivot reserve slot is present"),
			PlayerTrainer->PartySlots.Num() > 1))
	{
		return false;
	}
	PlayerTrainer->PartySlots[1].BattlerId = MakeNumericId<FBattlerId>(999);
	const FTargetCheckpointObservation PivotBefore =
		ObserveTargetCheckpoint(*PivotEngine);
	const FBattleResolution PivotRejected =
		PivotEngine->ExecuteCurrentMoveEffects();
	bValid &= VerifyRejectedTargetCheckpoint(
		*this,
		*PivotEngine,
		PivotBefore,
		EBattleRejectionReason::CheckpointPreparationFailed,
		PivotRejected);
	bValid &= TestTrue(TEXT("Pivot-request failure preserves no-draw RNG"),
		PivotRandom != nullptr
			&& PivotRandom->IsExact()
			&& PivotRandom->GetTrace().IsEmpty());
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E6RandomFailureFamilyTest,
	"PokemonSolarus.Battle.ADR0002.3E6.Effects.Failure.RandomTransactionCreateDrawCommit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E6RandomFailureFamilyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	auto RunCase = [this](
		const EFaultRandomMode Mode,
		const FMoveId MoveId,
		const FAtomicWildScenario& Scenario,
		const TArray<FBattleExpectedRandomDraw>& Draws,
		const int32 SuccessfulBeforeFailure,
		const EBattleRejectionReason ExpectedReason)
	{
		TUniquePtr<FBattleEngine> Engine;
		FFaultBattleRandom* Random = nullptr;
		if (!TestTrue(TEXT("RNG failure-family engine is created"),
				TryMakeStrictFaultEngine(
					Scenario,
					Draws,
					Mode,
					Engine,
					Random,
					SuccessfulBeforeFailure))
			|| !TestTrue(TEXT("RNG failure-family case reaches effects"),
				TryPrepareEffectsCheckpoint(*Engine, MoveId)))
		{
			return false;
		}
		const FTargetCheckpointObservation Before = ObserveTargetCheckpoint(*Engine);
		const FBattleResolution Rejected = Engine->ExecuteCurrentMoveEffects();
		bool bCase = VerifyRejectedTargetCheckpoint(
			*this,
			*Engine,
			Before,
			ExpectedReason,
			Rejected);
		bCase &= TestTrue(TEXT("Every RNG failure preserves the parent trace"),
			Random != nullptr && Random->GetTrace().IsEmpty());
		return bCase;
	};

	const FMoveId DeterministicMove = MakeDefinitionId<FMoveId>(TargetProbeMoveName);
	const FMoveId RandomMove = MakeDefinitionId<FMoveId>(RandomExecutionProbeMoveName);
	bool bValid = RunCase(
		EFaultRandomMode::CreateTransaction,
		DeterministicMove,
		MakePreMoveScenario(),
		{},
		0,
		EBattleRejectionReason::CheckpointRandomStageFailed);
	bValid &= RunCase(
		EFaultRandomMode::Draw,
		RandomMove,
		MakePreMoveScenario(RandomMove),
		{{0, 99, 0, FBattleEffectExecutor::GetAccuracyRulePurpose()}},
		1,
		EBattleRejectionReason::CheckpointRandomStageFailed);
	bValid &= RunCase(
		EFaultRandomMode::Commit,
		DeterministicMove,
		MakePreMoveScenario(),
		{},
		0,
		EBattleRejectionReason::RandomTransactionCommitFailed);
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E6TriggerCleanupFailureTest,
	"PokemonSolarus.Battle.ADR0002.3E6.Effects.Failure.FaintConditionAbilityItemTriggerCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E6TriggerCleanupFailureTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FMoveId MoveId = MakeDefinitionId<FMoveId>(RandomExecutionProbeMoveName);
	FAtomicWildScenario Scenario = MakePreMoveScenario(MoveId);
	Scenario.TargetCurrentHP = 1;
	TUniquePtr<FBattleEngine> Engine;
	FStrictBattleRandom* Random = nullptr;
	const TArray<FBattleExpectedRandomDraw> Expected =
	{
		{0, 99, 0, FBattleEffectExecutor::GetAccuracyRulePurpose()},
		{0, 15, 0, FBattleEffectExecutor::GetDamageRandomRulePurpose()}
	};
	if (!TestTrue(TEXT("Cleanup-failure engine is created"),
			TryMakeStrictEngine(Scenario, Expected, Engine, Random))
		|| !TestTrue(TEXT("Lethal case reaches effects"),
			TryPrepareEffectsCheckpoint(*Engine, MoveId))
		|| !TestTrue(TEXT("The faint target has a live major-status hook"),
			TrySeedPreMoveMajorStatus(
				*Engine,
				MakeNumericId<FBattlerId>(OpponentLeftValue),
				FBattleMajorStatusRules::GetBurnId()))
		|| !TestTrue(TEXT("The faint target has a live volatile hook"),
			TrySeedActionStartVolatile(
				*Engine,
				MakeNumericId<FBattlerId>(OpponentLeftValue),
				FBattleVolatileRules::GetConfusionId(),
				FDefinitionId(),
				TOptional<int32>(2))))
	{
		return false;
	}
	FBattleC09BWildFlowEngineFixture::GetMutableState(*Engine)
		.NextTriggerReentrancyToken = TNumericLimits<uint64>::Max() - 1;
	const FTargetCheckpointObservation Before = ObserveTargetCheckpoint(*Engine);
	const FBattleResolution Rejected = Engine->ExecuteCurrentMoveEffects();
	bool bValid = VerifyRejectedTargetCheckpoint(
		*this,
		*Engine,
		Before,
		EBattleRejectionReason::CheckpointPreparationFailed,
		Rejected);
	bValid &= TestTrue(TEXT("Staged lethal draws roll back with cleanup failure"),
		Random != nullptr && Random->GetTrace().IsEmpty());

	FAtomicWildScenario FaintPlanScenario = MakePreMoveScenario(MoveId);
	FaintPlanScenario.TargetCurrentHP = 1;
	FaintPlanScenario.bTrainerEncounter = true;
	FaintPlanScenario.bOpponentSwitchReserve = true;
	TUniquePtr<FBattleEngine> FaintPlanEngine;
	FStrictBattleRandom* FaintPlanRandom = nullptr;
	if (!TestTrue(TEXT("Faint-plan failure engine is created"),
			TryMakeStrictEngine(
				FaintPlanScenario,
				Expected,
				FaintPlanEngine,
				FaintPlanRandom))
		|| !TestTrue(TEXT("Faint-plan failure reaches effects"),
			TryPrepareEffectsCheckpoint(*FaintPlanEngine, MoveId)))
	{
		return false;
	}
	FBattleBattlerState* UnrelatedReserve =
		FBattleC09BWildFlowEngineFixture::GetMutableState(*FaintPlanEngine)
			.FindMutableBattler(MakeNumericId<FBattlerId>(OpponentReserveValue));
	if (!TestNotNull(TEXT("The unrelated reserve exists"), UnrelatedReserve))
	{
		return false;
	}
	UnrelatedReserve->bFaintTransitionPending = true;
	const FTargetCheckpointObservation FaintPlanBefore =
		ObserveTargetCheckpoint(*FaintPlanEngine);
	const FBattleResolution FaintPlanRejected =
		FaintPlanEngine->ExecuteCurrentMoveEffects();
	bValid &= VerifyRejectedTargetCheckpoint(
		*this,
		*FaintPlanEngine,
		FaintPlanBefore,
		EBattleRejectionReason::CheckpointPreparationFailed,
		FaintPlanRejected);
	bValid &= TestTrue(TEXT("Faint-plan failure rolls back staged draws"),
		FaintPlanRandom != nullptr && FaintPlanRandom->GetTrace().IsEmpty());
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E6StaleIdentityFamilyTest,
	"PokemonSolarus.Battle.ADR0002.3E6.Effects.Failure.StaleExactActionActorTargetFieldSideItemAbilityQueuePendingRequest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E6StaleIdentityFamilyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	enum class EStaleFact : uint8
	{
		Action,
		Actor,
		Target,
		Field,
		Side,
		Item,
		Ability,
		Queue,
		PendingRequest
	};
	const TArray<EStaleFact> Facts =
	{
		EStaleFact::Action,
		EStaleFact::Actor,
		EStaleFact::Target,
		EStaleFact::Field,
		EStaleFact::Side,
		EStaleFact::Item,
		EStaleFact::Ability,
		EStaleFact::Queue,
		EStaleFact::PendingRequest
	};
	bool bValid = true;
	for (const EStaleFact Fact : Facts)
	{
		FAtomicWildScenario Scenario = MakePreMoveScenario(
			FMoveId(),
			200,
			FBattleAbilityRules::GetBlazeId(),
			FBattleItemRules::GetChoiceBandId());
		TUniquePtr<FBattleEngine> Engine;
		FActionStartStaleRandom* Random = nullptr;
		if (!TestTrue(TEXT("Stale-family engine is created"),
				TryMakeActionStartStaleEngine(Scenario, Engine, Random))
			|| !TestNotNull(TEXT("Stale-family random seam is retained"), Random)
			|| !TestTrue(TEXT("Stale-family case reaches effects"),
				TryPrepareEffectsCheckpoint(
					*Engine,
					MakeDefinitionId<FMoveId>(TargetProbeMoveName))))
		{
			return false;
		}
		FTargetCheckpointObservation AfterInjection;
		bool bObservedAfterInjection = false;
		bool bInjected = false;
		Random->ArmAfterTraceRead(
			3,
			[EnginePtr = Engine.Get(), Random, Fact, &AfterInjection,
				&bObservedAfterInjection, &bInjected]()
			{
				FBattleEngineState& State =
					FBattleC09BWildFlowEngineFixture::GetMutableState(*EnginePtr);
				switch (Fact)
				{
				case EStaleFact::Action:
					++State.LockedActions[State.CurrentLockedActionIndex].QueueOrdinal;
					break;
				case EStaleFact::Actor:
					--State.FindMutableBattler(
						MakeNumericId<FBattlerId>(PlayerLeftValue))->CurrentHP;
					break;
				case EStaleFact::Target:
					--State.FindMutableBattler(
						MakeNumericId<FBattlerId>(OpponentLeftValue))->CurrentHP;
					break;
				case EStaleFact::Field:
				{
					FBattleConditionState Condition;
					Condition.ConditionId = FBattleFieldSideConditionRules::GetMagicRoomId();
					Condition.LayerCount = 1;
					Condition.CreationOrdinal = State.NextConditionCreationOrdinal++;
					State.Field.Rooms.Add(Condition);
					break;
				}
				case EStaleFact::Side:
				{
					FBattleConditionState Condition;
					Condition.ConditionId = FBattleFieldSideConditionRules::GetSpikesId();
					Condition.LayerCount = 1;
					Condition.CreationOrdinal = State.NextConditionCreationOrdinal++;
					State.Sides[0].Hazards.Add(Condition);
					break;
				}
				case EStaleFact::Item:
					State.FindMutableBattler(
						MakeNumericId<FBattlerId>(PlayerLeftValue))->HeldItem.bSuppressed = true;
					break;
				case EStaleFact::Ability:
					State.FindMutableBattler(
						MakeNumericId<FBattlerId>(PlayerLeftValue))->AbilityId =
						FBattleAbilityRules::GetIntimidateId();
					break;
				case EStaleFact::Queue:
					State.PendingReplacements.Add({
						MakeNumericId<FTrainerId>(PlayerTrainerValue),
						MakeActiveSlotId(EBattleSide::Player, EBattlePosition::Left)});
					break;
				case EStaleFact::PendingRequest:
					State.PendingDecisionRequests.Add(FBattleDecisionRequest());
					break;
				}
				Random->DisableFurtherTraceInjection();
				AfterInjection = ObserveTargetCheckpoint(*EnginePtr);
				--AfterInjection.Action.NextResolutionId;
				bObservedAfterInjection = true;
				bInjected = true;
			});
		const FBattleResolution Rejected = Engine->ExecuteCurrentMoveEffects();
		const int32 TraceReads = Random->GetReadsSinceArm();
		const bool bRandomInjected = Random->WasInjected();
		Random->Disarm();
		bool bCase = TestTrue(TEXT("Stale mutation is observed before rejection"),
			bObservedAfterInjection);
		bCase &= VerifyRejectedTargetCheckpoint(
			*this,
			*Engine,
			AfterInjection,
			EBattleRejectionReason::StaleCheckpointIdentity,
			Rejected);
		bCase &= TestTrue(TEXT("Stale fact is injected at the final exact recheck"),
			bInjected && bRandomInjected && TraceReads == 3);
		bCase &= TestTrue(TEXT("Stale rejection commits no parent RNG"),
			Engine->ExportRandomTrace().IsEmpty());
		bValid &= bCase;
	}
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC10RemovalAtomicLateFailureRollbackTest,
	"PokemonSolarus.Battle.C05B.C10Removal.Atomic.LateFailureRollback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC10RemovalAtomicLateFailureRollbackTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	const FMoveId MoveId = MakeDefinitionId<FMoveId>(C10RemovalAtomicMoveName);
	const FBattlerId TargetId = MakeNumericId<FBattlerId>(OpponentLeftValue);
	const FBattlerId UserId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	FAtomicWildScenario Scenario = MakePreMoveScenario(MoveId);
	Scenario.PlayerExtraMoveId = MoveId;
	TUniquePtr<FBattleEngine> Engine;
	FStrictBattleRandom* Random = nullptr;
	if (!TestTrue(TEXT("C10 removal atomic engine is created"),
			TryMakeC10RemovalAtomicEngine(
				Scenario,
				{{0, 99, 0, FBattleEffectExecutor::GetSecondaryChanceRulePurpose()}},
				Engine,
				Random))
		|| !TestTrue(TEXT("C10 removal move reaches effects"),
			TryPrepareEffectsCheckpoint(*Engine, MoveId))
		|| !TestTrue(TEXT("Leech Seed is present before optional removal"),
			TrySeedActionStartVolatile(
				*Engine,
				TargetId,
				FBattleVolatileRules::GetLeechSeedId())))
	{
		return false;
	}

	FBattleEngineState& Mutable =
		FBattleC09BWildFlowEngineFixture::GetMutableState(*Engine);
	Mutable.NextEventOrdinal = TNumericLimits<uint64>::Max() - 3;
	const FTargetCheckpointObservation Before = ObserveTargetCheckpoint(*Engine);
	const FBattleResolution Rejected = Engine->ExecuteCurrentMoveEffects();
	bool bValid = VerifyRejectedTargetCheckpoint(
		*this,
		*Engine,
		Before,
		EBattleRejectionReason::CheckpointPreparationFailed,
		Rejected);
	const FBattleEngineState& After =
		FBattleC09BWildFlowEngineFixture::GetState(*Engine);
	const FBattleBattlerState* Target = After.FindBattler(TargetId);
	const FBattleBattlerState* User = After.FindBattler(UserId);
	int32 UserSpeedStage = INDEX_NONE;
	bValid &= TestTrue(TEXT("Late plan failure restores the removed volatile"),
		Target != nullptr && Target->Volatiles.ContainsByPredicate(
			[](const FBattleConditionState& Condition)
			{
				return Condition.ConditionId == FBattleVolatileRules::GetLeechSeedId();
			}));
	bValid &= TestTrue(TEXT("Late plan failure restores the later stat mutation"),
		User != nullptr
			&& User->Stages.TryGetStage(EBattleStat::Speed, UserSpeedStage)
			&& UserSpeedStage == 0);
	bValid &= TestTrue(TEXT("Optional-removal chance draw remains transaction-local"),
		Random != nullptr && Random->GetTrace().IsEmpty());
	bValid &= TestEqual(TEXT("Rollback keeps replay schema 6"),
		Engine->ExportReplayRecord().GetSchemaVersion(), static_cast<uint32>(6));
	return bValid;
}

} // namespace BattleAtomicMoveEffectTestsPrivate

#endif
