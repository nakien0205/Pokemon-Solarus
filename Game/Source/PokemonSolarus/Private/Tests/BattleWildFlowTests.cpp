#if WITH_DEV_AUTOMATION_TESTS

#include "Battle/BattleAbility.h"
#include "Battle/BattleEngine.h"
#include "Battle/BattleState.h"
#include "Battle/BattleWildFlow.h"
#include "BattleTestFactories.h"
#include "BattleTestRandom.h"
#include "Misc/AutomationTest.h"

class FBattleC09BWildFlowEngineFixture
{
public:
	static FBattleEngineState& GetMutableState(FBattleEngine& Engine)
	{
		check(Engine.State.IsValid());
		return *Engine.State;
	}

	static const FBattleEngineState& GetState(const FBattleEngine& Engine)
	{
		check(Engine.State.IsValid());
		return *Engine.State;
	}

	static int32 GetRemainingActions(
		const FBattleEngine& Engine,
		const FTrainerId TrainerId)
	{
		const FBattleTrainerState* Trainer = GetState(Engine).FindTrainer(TrainerId);
		return Trainer != nullptr ? Trainer->ActionAllowance.RemainingActions : INDEX_NONE;
	}

	static bool ApplySpeedStage(
		FBattleEngine& Engine,
		const FBattlerId BattlerId,
		const int32 Delta)
	{
		FBattleBattlerState* Battler = GetMutableState(Engine).FindMutableBattler(BattlerId);
		return Battler != nullptr
			&& Battler->Stages.ApplyChange(EBattleStat::Speed, Delta).Outcome
				== EBattleStatStageChangeOutcome::Applied;
	}

	static bool SetPermanentSpeed(
		FBattleEngine& Engine,
		const FBattlerId BattlerId,
		const int32 Speed)
	{
		FBattleBattlerState* Battler = GetMutableState(Engine).FindMutableBattler(BattlerId);
		if (Battler == nullptr)
		{
			return false;
		}
		Battler->PermanentStats.Speed = Speed;
		return true;
	}

	static bool IsActive(const FBattleEngine& Engine, const FBattlerId BattlerId)
	{
		return GetState(Engine).ActivePositions.ContainsByPredicate(
			[BattlerId](const FBattleActivePositionState& Position)
			{
				return Position.BattlerId == BattlerId;
			});
	}

	static bool IsRemoved(const FBattleEngine& Engine, const FBattlerId BattlerId)
	{
		const FBattleBattlerState* Battler = GetState(Engine).FindBattler(BattlerId);
		return Battler != nullptr && Battler->bRemoved;
	}

	static int32 GetMovePP(const FBattleEngine& Engine, const FBattlerId BattlerId)
	{
		const FBattleBattlerState* Battler = GetState(Engine).FindBattler(BattlerId);
		return Battler != nullptr && !Battler->Moves.IsEmpty()
			? Battler->Moves[0].CurrentPP
			: INDEX_NONE;
	}

	static TConstArrayView<FBattleWildFleePolicyState> GetWildFleePolicies(
		const FBattleEngine& Engine)
	{
		return GetState(Engine).WildFleePolicies;
	}
};

namespace
{
	using BattleTest::MakeActiveSlotId;
	using BattleTest::MakeDefinitionId;
	using BattleTest::MakeNumericId;
	using BattleTest::MakePartySlotId;
	using BattleTest::FSequenceBattleRandom;

	constexpr uint64 PlayerTrainerValue = 1;
	constexpr uint64 OpponentTrainerValue = 2;
	constexpr uint64 PlayerLeftValue = 11;
	constexpr uint64 PlayerRightValue = 12;
	constexpr uint64 OpponentLeftValue = 21;
	constexpr uint64 OpponentRightValue = 22;

	const TCHAR* PlayerSpeciesName = TEXT("Species.C09B.WildFlow.Player");
	const TCHAR* WildSpeciesName = TEXT("Species.C09B.WildFlow.Wild");
	const TCHAR* ProbeMoveName = TEXT("Move.C09B.WildFlow.Probe");

	struct FWildFlowScenario
	{
		EBattleEncounterKind EncounterKind = EBattleEncounterKind::Wild;
		EBattleFormat Format = EBattleFormat::Single;
		int32 PlayerLeftSpeed = 50;
		int32 PlayerRightSpeed = 73;
		int32 OpponentLeftSpeed = 100;
		int32 OpponentRightSpeed = 4;
		bool bPlayerReserve = false;
		bool bRunAllowed = true;
		EBattleWildFleeMode WildFleeMode = EBattleWildFleeMode::Disabled;
		uint32 WildFleeNumerator = 0;
		uint32 WildFleeDenominator = 0;
	};

	TArray<FBattleTypeChartEntry> MakeNeutralTypeChart()
	{
		TArray<FBattleTypeChartEntry> Entries;
		Entries.Reserve(FBattleTypeChart::EntryCount);
		for (int32 Attack = 0; Attack < FBattleTypeChart::TypeCount; ++Attack)
		{
			for (int32 Defense = 0; Defense < FBattleTypeChart::TypeCount; ++Defense)
			{
				Entries.Add({
					static_cast<EPokemonType>(Attack),
					static_cast<EPokemonType>(Defense),
					1,
					1});
			}
		}
		return Entries;
	}

	FBattleMoveDefinition MakeProbeMove()
	{
		FBattleMoveDefinition Move;
		Move.Id = MakeDefinitionId<FMoveId>(ProbeMoveName);
		Move.Type = EPokemonType::Normal;
		Move.Category = EBattleMoveCategory::Status;
		Move.bAlwaysHits = true;
		Move.BasePP = 20;
		Move.TargetClass = EBattleTargetClass::Self;
		FBattleMoveEffectDescriptor Effect;
		Effect.Kind = EBattleMoveEffectKind::ModifyStatStage;
		Effect.Target = EBattleEffectTarget::User;
		Effect.Stat = EBattleStat::Attack;
		Effect.MagnitudeNumerator = 1;
		Move.Effects.Add(Effect);
		return Move;
	}

	FBattleSpeciesFormDefinition MakeSpecies(const TCHAR* Name)
	{
		FBattleSpeciesFormDefinition Species;
		Species.Id = MakeDefinitionId<FSpeciesFormId>(Name);
		Species.PrimaryType = EPokemonType::Normal;
		Species.BaseStats = {80, 80, 80, 80, 80, 80};
		Species.CatchRate = 45;
		Species.AbilityChoices.Add(FBattleAbilityRules::GetBlazeId());
		return Species;
	}

	FBattleDefinitionCatalog MakeCatalog()
	{
		FBattleDefinitionCatalogInput Input;
		Input.TypeChartEntries = MakeNeutralTypeChart();
		Input.Moves.Add(MakeProbeMove());
		Input.Abilities.Add({FBattleAbilityRules::GetBlazeId()});
		Input.SpeciesForms.Add(MakeSpecies(PlayerSpeciesName));
		Input.SpeciesForms.Add(MakeSpecies(WildSpeciesName));

		FBattleDefinitionCatalog Catalog;
		TArray<FBattleCatalogDiagnostic> Diagnostics;
		const bool bCreated = FBattleDefinitionCatalog::TryCreate(Input, Catalog, Diagnostics);
		check(bCreated);
		return Catalog;
	}

	FBattleTrainerSetup MakeTrainer(
		const uint64 TrainerValue,
		const EBattleSide Side,
		const EBattleTrainerRole Role,
		const EBattleDecisionController Controller)
	{
		FBattleTrainerSetup Trainer;
		Trainer.TrainerId = MakeNumericId<FTrainerId>(TrainerValue);
		Trainer.Side = Side;
		Trainer.Role = Role;
		Trainer.Controller = Controller;
		Trainer.SelectorProfileId = MakeDefinitionId<FDefinitionId>(
			Role == EBattleTrainerRole::Player
				? TEXT("Selector.C09B.WildFlow.Player")
				: TEXT("Selector.C09B.WildFlow.Opponent"));
		return Trainer;
	}

	FBattlePartyEntrySetup MakePartyEntry(
		const uint64 TrainerValue,
		const uint64 BattlerValue,
		const int32 PartyIndex,
		const TCHAR* SpeciesName,
		const int32 Speed)
	{
		FBattlePartyEntrySetup Entry;
		Entry.TrainerId = MakeNumericId<FTrainerId>(TrainerValue);
		Entry.BattlerId = MakeNumericId<FBattlerId>(BattlerValue);
		Entry.SourcePokemonId = MakeNumericId<FSourcePokemonId>(1000 + BattlerValue);
		Entry.PartySlotId = MakePartySlotId(PartyIndex);
		Entry.SpeciesFormId = MakeDefinitionId<FSpeciesFormId>(SpeciesName);
		Entry.Level = 50;
		Entry.Stats = {200, 100, 100, 100, 100, Speed};
		Entry.CurrentHP = 200;
		Entry.AbilityId = FBattleAbilityRules::GetBlazeId();
		Entry.CaptureClassification = EBattleCaptureSpeciesClassification::Normal;
		Entry.Moves.Add({0, MakeDefinitionId<FMoveId>(ProbeMoveName), 20, 20});
		return Entry;
	}

	FBattleActiveAssignment MakeActive(
		const EBattleSide Side,
		const EBattlePosition Position,
		const uint64 TrainerValue,
		const uint64 BattlerValue)
	{
		return {
			MakeActiveSlotId(Side, Position),
			MakeNumericId<FTrainerId>(TrainerValue),
			MakeNumericId<FBattlerId>(BattlerValue)};
	}

	FBattleSetupInput MakeSetupInput(const FWildFlowScenario& Scenario)
	{
		FBattleSetupInput Input;
		Input.BattleId = MakeNumericId<FBattleId>(9093);
		Input.SettingsReference = {
			MakeDefinitionId<FDefinitionId>(TEXT("Settings.C09B.WildFlow")),
			1};
		Input.CatalogReference = {
			MakeDefinitionId<FDefinitionId>(TEXT("Catalog.C09B.WildFlow")),
			1};
		Input.EncounterKind = Scenario.EncounterKind;
		Input.Format = Scenario.Format;
		Input.Policies.bBagAllowed = false;
		Input.Policies.bCaptureAllowed = false;
		Input.Policies.bRunAllowed = Scenario.bRunAllowed;
		Input.Policies.bShiftPromptEligible = false;
		Input.Policies.WildFleeMode = Scenario.WildFleeMode;
		Input.Policies.WildFleeNumerator = Scenario.WildFleeNumerator;
		Input.Policies.WildFleeDenominator = Scenario.WildFleeDenominator;

		Input.Trainers.Add(MakeTrainer(
			PlayerTrainerValue,
			EBattleSide::Player,
			EBattleTrainerRole::Player,
			EBattleDecisionController::Human));
		Input.Trainers.Add(MakeTrainer(
			OpponentTrainerValue,
			EBattleSide::Opponent,
			EBattleTrainerRole::Opponent,
			EBattleDecisionController::EnemyAI));

		Input.PartyEntries.Add(MakePartyEntry(
			PlayerTrainerValue,
			PlayerLeftValue,
			0,
			PlayerSpeciesName,
			Scenario.PlayerLeftSpeed));
		Input.PartyEntries.Add(MakePartyEntry(
			OpponentTrainerValue,
			OpponentLeftValue,
			0,
			WildSpeciesName,
			Scenario.OpponentLeftSpeed));
		Input.StartingActive.Add(MakeActive(
			EBattleSide::Player,
			EBattlePosition::Left,
			PlayerTrainerValue,
			PlayerLeftValue));
		Input.StartingActive.Add(MakeActive(
			EBattleSide::Opponent,
			EBattlePosition::Left,
			OpponentTrainerValue,
			OpponentLeftValue));

		if (Scenario.Format == EBattleFormat::Double)
		{
			Input.PartyEntries.Add(MakePartyEntry(
				PlayerTrainerValue,
				PlayerRightValue,
				1,
				PlayerSpeciesName,
				Scenario.PlayerRightSpeed));
			Input.PartyEntries.Add(MakePartyEntry(
				OpponentTrainerValue,
				OpponentRightValue,
				1,
				WildSpeciesName,
				Scenario.OpponentRightSpeed));
			Input.StartingActive.Add(MakeActive(
				EBattleSide::Player,
				EBattlePosition::Right,
				PlayerTrainerValue,
				PlayerRightValue));
			Input.StartingActive.Add(MakeActive(
				EBattleSide::Opponent,
				EBattlePosition::Right,
				OpponentTrainerValue,
				OpponentRightValue));
		}
		else if (Scenario.bPlayerReserve)
		{
			Input.PartyEntries.Add(MakePartyEntry(
				PlayerTrainerValue,
				PlayerRightValue,
				1,
				PlayerSpeciesName,
				Scenario.PlayerRightSpeed));
		}
		return Input;
	}

	bool TryMakeEngine(
		const FWildFlowScenario& Scenario,
		TArray<uint32> RandomResults,
		TUniquePtr<FBattleEngine>& OutEngine)
	{
		FBattleSetup Setup;
		EBattleSetupValidationError SetupError = EBattleSetupValidationError::None;
		if (!FBattleSetup::TryCreate(MakeSetupInput(Scenario), Setup, SetupError))
		{
			return false;
		}
		TUniquePtr<IBattleRandom> Random =
			MakeUnique<FSequenceBattleRandom>(MoveTemp(RandomResults));
		FBattleRejection Rejection;
		return FBattleEngine::TryCreate(
			Setup,
			MakeCatalog(),
			MoveTemp(Random),
			OutEngine,
			Rejection);
	}

	FBattleRandomContext MakeRandomContext()
	{
		FBattleRandomContext Context;
		Context.BattleId = MakeNumericId<FBattleId>(9094);
		Context.TurnId = MakeNumericId<FTurnId>(1);
		Context.ActionId = MakeNumericId<FActionId>(1);
		Context.ResolutionId = MakeNumericId<FResolutionId>(1);
		return Context;
	}

	FBattleDecision MakeDecision(
		const FBattleDecisionRequest& Request,
		const EBattleActionKind ActionKind)
	{
		FBattleDecision Decision;
		bool bCreated = false;
		switch (ActionKind)
		{
		case EBattleActionKind::Fight:
			bCreated = FBattleDecision::TryCreateAutomaticallyTargetedFight(
				Request.GetStateVersion(),
				Request.GetDecisionOwnerTrainerId(),
				Request.GetActingBattlerId(),
				MakeDefinitionId<FMoveId>(ProbeMoveName),
				Decision);
			break;
		case EBattleActionKind::Run:
		case EBattleActionKind::WildFlee:
			bCreated = FBattleDecision::TryCreateSimpleAction(
				Request.GetStateVersion(),
				Request.GetRequestKind(),
				Request.GetDecisionOwnerTrainerId(),
				Request.GetActingBattlerId(),
				ActionKind,
				Decision);
			break;
		case EBattleActionKind::Switch:
			check(!Request.GetLegalSwitchPartySlots().IsEmpty()
				&& !Request.GetLegalActiveTargets().IsEmpty());
			bCreated = FBattleDecision::TryCreateSwitch(
				Request.GetStateVersion(),
				Request.GetRequestKind(),
				Request.GetDecisionOwnerTrainerId(),
				Request.GetActingBattlerId(),
				Request.GetLegalSwitchPartySlots()[0],
				Request.GetLegalActiveTargets()[0],
				Decision);
			break;
		default:
			break;
		}
		check(bCreated);
		return Decision;
	}

	FBattleDecisionBatch MakeBatch(
		const TArray<FBattleDecisionRequest>& Requests,
		TArray<FBattleDecision> Decisions)
	{
		check(!Requests.IsEmpty());
		FBattleDecisionBatchSpec Spec;
		Spec.StateVersion = Requests[0].GetStateVersion();
		Spec.RequestKind = Requests[0].GetRequestKind();
		Spec.DecisionOwnerTrainerId = Requests[0].GetDecisionOwnerTrainerId();
		Spec.Decisions = MoveTemp(Decisions);
		FBattleDecisionBatch Batch;
		FBattleRejection Rejection;
		const bool bCreated = FBattleDecisionBatch::TryCreate(Spec, Batch, Rejection);
		check(bCreated);
		return Batch;
	}

	bool SubmitCurrentRequests(
		FBattleEngine& Engine,
		const uint64 SpecialBattlerValue = 0,
		const EBattleActionKind SpecialAction = EBattleActionKind::Fight)
	{
		const TArray<FBattleDecisionRequest> Requests = Engine.GetPendingDecisionRequests();
		if (Requests.IsEmpty())
		{
			return false;
		}
		TArray<FBattleDecision> Decisions;
		for (const FBattleDecisionRequest& Request : Requests)
		{
			const EBattleActionKind Choice = SpecialBattlerValue != 0
				&& Request.GetActingBattlerId()
					== MakeNumericId<FBattlerId>(SpecialBattlerValue)
				? SpecialAction
				: EBattleActionKind::Fight;
			Decisions.Add(MakeDecision(Request, Choice));
		}
		return Engine.SubmitDecisionBatch(
			MakeBatch(Requests, MoveTemp(Decisions))).WasAccepted();
	}

	bool LockTurn(
		FBattleEngine& Engine,
		const uint64 SpecialBattlerValue = 0,
		const EBattleActionKind SpecialAction = EBattleActionKind::Fight)
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
			if (!SubmitCurrentRequests(Engine, SpecialBattlerValue, SpecialAction))
			{
				return false;
			}
		}
		return Guard < 4 && Engine.GetSnapshot().GetPhase() == EBattlePhase::Locked;
	}

	bool ExecuteQueueToBoundary(FBattleEngine& Engine)
	{
		int32 Guard = 0;
		while ((Engine.GetSnapshot().GetPhase() == EBattlePhase::Locked
				|| Engine.GetSnapshot().GetPhase() == EBattlePhase::Resolving)
			&& Guard++ < 16)
		{
			if (!Engine.BeginNextLockedAction().WasAccepted())
			{
				return false;
			}
			const TOptional<FBattleLockedAction> Current = Engine.GetCurrentLockedAction();
			if (!Current.IsSet())
			{
				continue;
			}

			switch (Current->Decision.GetActionKind())
			{
			case EBattleActionKind::Run:
			case EBattleActionKind::WildFlee:
				if (!Engine.ExecuteCurrentWildAction().WasAccepted())
				{
					return false;
				}
				break;
			case EBattleActionKind::Switch:
				if (!Engine.ExecuteCurrentSwitch().WasAccepted())
				{
					return false;
				}
				break;
			case EBattleActionKind::Fight:
				if (!Engine.CommitCurrentMoveAfterPreMoveGates().WasAccepted())
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
				break;
			default:
				return false;
			}
		}
		const EBattlePhase Phase = Engine.GetSnapshot().GetPhase();
		return Guard < 16
			&& (Phase == EBattlePhase::EndOfTurn || Phase == EBattlePhase::Terminal);
	}

	bool AdvanceFromEndTurn(FBattleEngine& Engine)
	{
		return Engine.GetSnapshot().GetPhase() == EBattlePhase::EndOfTurn
			&& Engine.ResolveEndTurn().WasAccepted()
			&& Engine.GetSnapshot().GetPhase() == EBattlePhase::Selecting;
	}

	bool StartAndResolveWildAction(
		FBattleEngine& Engine,
		const EBattleActionKind ExpectedKind,
		FBattleResolution& OutResolution)
	{
		if (!Engine.BeginNextLockedAction().WasAccepted())
		{
			return false;
		}
		const TOptional<FBattleLockedAction> Current = Engine.GetCurrentLockedAction();
		if (!Current.IsSet() || Current->Decision.GetActionKind() != ExpectedKind)
		{
			return false;
		}
		OutResolution = Engine.ExecuteCurrentWildAction();
		return OutResolution.WasAccepted();
	}

	bool HasEvent(const FBattleResolution& Resolution, const EBattleEventType Type)
	{
		return Resolution.GetEvents().ContainsByPredicate(
			[Type](const FBattleEvent& Event)
			{
				return Event.GetType() == Type;
			});
	}

	int32 FindEventIndex(const FBattleResolution& Resolution, const EBattleEventType Type)
	{
		for (int32 Index = 0; Index < Resolution.GetEvents().Num(); ++Index)
		{
			if (Resolution.GetEvents()[Index].GetType() == Type)
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

	bool DriveFailedThenSuccessfulRun(FBattleEngine& Engine)
	{
		FBattleResolution Failed;
		if (!LockTurn(Engine, PlayerLeftValue, EBattleActionKind::Run)
			|| !StartAndResolveWildAction(Engine, EBattleActionKind::Run, Failed)
			|| HasEvent(Failed, EBattleEventType::Escaped)
			|| !ExecuteQueueToBoundary(Engine)
			|| !AdvanceFromEndTurn(Engine))
		{
			return false;
		}

		FBattleResolution Succeeded;
		return LockTurn(Engine, PlayerLeftValue, EBattleActionKind::Run)
			&& StartAndResolveWildAction(Engine, EBattleActionKind::Run, Succeeded)
			&& HasEvent(Succeeded, EBattleEventType::Escaped)
			&& Engine.GetSnapshot().GetPhase() == EBattlePhase::Terminal;
	}

	bool SerializeReplay(const FBattleEngine& Engine, TArray<uint8>& OutBytes)
	{
		FBattleRejection Rejection;
		return FBattleReplaySerializer::TrySerializeCanonical(
			Engine.ExportReplayRecord(),
			OutBytes,
			Rejection);
	}

	FBattleWildFleePolicySpec MakeWildFleePolicy(const EBattleWildFleeMode Mode)
	{
		FBattleWildFleePolicySpec Policy;
		Policy.TriggerId = FBattleWildFleeRules::GetActionSelectionTriggerId();
		Policy.EligibilityId = FBattleWildFleeRules::GetActiveLivingWildEligibilityId();
		Policy.ProbabilityMode = Mode;
		return Policy;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC09BRunLegalityFormulaTest,
	"PokemonSolarus.Battle.C09B.Run.LegalityFormulaAndPermanentSpeed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC09BRunLegalityFormulaTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FWildFlowScenario TrainerScenario;
	TrainerScenario.EncounterKind = EBattleEncounterKind::Trainer;
	TrainerScenario.bRunAllowed = false;
	TUniquePtr<FBattleEngine> TrainerEngine;
	TestTrue(TEXT("Trainer engine is created"), TryMakeEngine(TrainerScenario, {}, TrainerEngine));
	FBattleRejection BeginRejection;
	TestTrue(TEXT("Trainer action selection begins"),
		TrainerEngine->TryBeginActionDecisionSequence(BeginRejection));
	const FBattleDecisionRequest TrainerRequest = TrainerEngine->GetPendingDecision().GetValue();
	TestFalse(TEXT("Trainer encounter does not offer Run"),
		TrainerRequest.GetLegalActionKinds().Contains(EBattleActionKind::Run));
	const int32 TrainerActionsBefore = FBattleC09BWildFlowEngineFixture::GetRemainingActions(
		*TrainerEngine,
		MakeNumericId<FTrainerId>(PlayerTrainerValue));
	const FBattleResolution ForgedTrainerRun = TrainerEngine->SubmitDecision(
		MakeDecision(TrainerRequest, EBattleActionKind::Run));
	TestFalse(TEXT("Forged trainer Run is rejected"), ForgedTrainerRun.WasAccepted());
	TestEqual(TEXT("Trainer rejection consumes no action"),
		FBattleC09BWildFlowEngineFixture::GetRemainingActions(
			*TrainerEngine,
			MakeNumericId<FTrainerId>(PlayerTrainerValue)),
		TrainerActionsBefore);
	TestEqual(TEXT("Trainer rejection consumes no RNG"),
		TrainerEngine->ExportRandomTrace().Num(), 0);

	FWildFlowScenario BlockedScenario;
	BlockedScenario.PlayerLeftSpeed = 3;
	TUniquePtr<FBattleEngine> BlockedEngine;
	TestTrue(TEXT("Low-Speed wild engine is created"),
		TryMakeEngine(BlockedScenario, {}, BlockedEngine));
	BeginRejection = FBattleRejection();
	TestTrue(TEXT("Low-Speed action selection begins"),
		BlockedEngine->TryBeginActionDecisionSequence(BeginRejection));
	const FBattleDecisionRequest BlockedRequest = BlockedEngine->GetPendingDecision().GetValue();
	TestFalse(TEXT("Speed below four does not offer Run"),
		BlockedRequest.GetLegalActionKinds().Contains(EBattleActionKind::Run));
	const FBattleResolution ForgedBlockedRun = BlockedEngine->SubmitDecision(
		MakeDecision(BlockedRequest, EBattleActionKind::Run));
	TestFalse(TEXT("Forged low-Speed Run is rejected"), ForgedBlockedRun.WasAccepted());
	TestEqual(TEXT("Blocked Run leaves the one-based counter unchanged"),
		BlockedEngine->GetSnapshot().GetEscapeAttemptCount(), 1U);
	TestEqual(TEXT("Blocked Run consumes no RNG"), BlockedEngine->ExportRandomTrace().Num(), 0);

	FWildFlowScenario LeftmostScenario;
	LeftmostScenario.Format = EBattleFormat::Double;
	LeftmostScenario.PlayerLeftSpeed = 50;
	LeftmostScenario.OpponentLeftSpeed = 100;
	LeftmostScenario.OpponentRightSpeed = 4;
	TUniquePtr<FBattleEngine> LeftmostEngine;
	TestTrue(TEXT("Double wild engine is created"),
		TryMakeEngine(LeftmostScenario, {94}, LeftmostEngine));
	TestTrue(TEXT("A transient Speed boost is applied"),
		FBattleC09BWildFlowEngineFixture::ApplySpeedStage(
			*LeftmostEngine,
			MakeNumericId<FBattlerId>(PlayerLeftValue),
			6));
	TestTrue(TEXT("Run turn locks"),
		LockTurn(*LeftmostEngine, PlayerLeftValue, EBattleActionKind::Run));
	FBattleResolution LeftmostRun;
	TestTrue(TEXT("Run resolves"),
		StartAndResolveWildAction(*LeftmostEngine, EBattleActionKind::Run, LeftmostRun));
	TestFalse(TEXT("R equal to the left wild threshold fails"),
		HasEvent(LeftmostRun, EBattleEventType::Escaped));
	TestEqual(TEXT("Legal failure increments the counter"),
		LeftmostEngine->GetSnapshot().GetEscapeAttemptCount(), 2U);
	const TArray<FBattleRandomDraw> LeftmostTrace = LeftmostEngine->ExportRandomTrace();
	TestEqual(TEXT("Permanent Speed and leftmost wild require one draw"), LeftmostTrace.Num(), 1);
	if (!LeftmostTrace.IsEmpty())
	{
		TestEqual(TEXT("Run draws over U[0,255]"), LeftmostTrace[0].InclusiveMaximum, 255U);
		TestTrue(TEXT("Run draw has the typed purpose"),
			LeftmostTrace[0].RulePurpose == FBattleRunRules::GetRandomCheckPurpose());
	}

	FSequenceBattleRandom GuaranteedRandom({});
	FBattleRunCalculationInput GuaranteedInput;
	GuaranteedInput.PlayerPermanentSpeed = 400;
	GuaranteedInput.WildPermanentSpeed = 200;
	GuaranteedInput.EscapeAttemptCount = 1;
	FBattleRunCalculationResult GuaranteedResult;
	TestTrue(TEXT("The guaranteed Run formula resolves"),
		FBattleRunRules::TryResolve(GuaranteedInput, GuaranteedRandom, GuaranteedResult));
	TestEqual(TEXT("The exact guaranteed threshold is 286"),
		GuaranteedResult.EscapeThreshold, 286LL);
	TestTrue(TEXT("F greater than 255 succeeds"), GuaranteedResult.bSucceeded);
	TestEqual(TEXT("F greater than 255 consumes no RNG"),
		GuaranteedRandom.GetTrace().Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC09BRunBoundaryCounterTest,
	"PokemonSolarus.Battle.C09B.Run.BoundariesCounterAndPersistence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC09BRunBoundaryCounterTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FBattleRunCalculationInput BoundaryInput;
	BoundaryInput.PlayerPermanentSpeed = 50;
	BoundaryInput.WildPermanentSpeed = 100;
	BoundaryInput.EscapeAttemptCount = 1;
	BoundaryInput.RandomContext = MakeRandomContext();
	FSequenceBattleRandom BelowRandom({93});
	FBattleRunCalculationResult BelowResult;
	TestTrue(TEXT("R below F resolves"),
		FBattleRunRules::TryResolve(BoundaryInput, BelowRandom, BelowResult));
	TestEqual(TEXT("Boundary threshold is 94"), BelowResult.EscapeThreshold, 94LL);
	TestTrue(TEXT("R below F succeeds"), BelowResult.bSucceeded);
	FSequenceBattleRandom EqualRandom({94});
	FBattleRunCalculationResult EqualResult;
	TestTrue(TEXT("R equal to F resolves"),
		FBattleRunRules::TryResolve(BoundaryInput, EqualRandom, EqualResult));
	TestFalse(TEXT("R equal to F fails"), EqualResult.bSucceeded);

	FWildFlowScenario PersistenceScenario;
	PersistenceScenario.bPlayerReserve = true;
	PersistenceScenario.PlayerRightSpeed = 61;
	TUniquePtr<FBattleEngine> PersistenceEngine;
	TestTrue(TEXT("Persistence engine is created"),
		TryMakeEngine(PersistenceScenario, {94}, PersistenceEngine));
	FBattleResolution FailedRun;
	TestTrue(TEXT("First Run turn locks"),
		LockTurn(*PersistenceEngine, PlayerLeftValue, EBattleActionKind::Run));
	TestTrue(TEXT("First Run fails legally"),
		StartAndResolveWildAction(*PersistenceEngine, EBattleActionKind::Run, FailedRun)
			&& !HasEvent(FailedRun, EBattleEventType::Escaped));
	TestEqual(TEXT("Failed Run advances C to two"),
		PersistenceEngine->GetSnapshot().GetEscapeAttemptCount(), 2U);
	TestTrue(TEXT("The rest of the first turn resolves"),
		ExecuteQueueToBoundary(*PersistenceEngine));
	TestTrue(TEXT("The second turn begins"), AdvanceFromEndTurn(*PersistenceEngine));

	TestTrue(TEXT("An ordinary Fight turn locks"), LockTurn(*PersistenceEngine));
	TestTrue(TEXT("The ordinary Fight turn resolves"),
		ExecuteQueueToBoundary(*PersistenceEngine));
	TestEqual(TEXT("Other actions do not reset C"),
		PersistenceEngine->GetSnapshot().GetEscapeAttemptCount(), 2U);
	TestTrue(TEXT("The third turn begins"), AdvanceFromEndTurn(*PersistenceEngine));

	TestTrue(TEXT("A voluntary Switch turn locks"),
		LockTurn(*PersistenceEngine, PlayerLeftValue, EBattleActionKind::Switch));
	TestTrue(TEXT("The voluntary Switch turn resolves"),
		ExecuteQueueToBoundary(*PersistenceEngine));
	TestEqual(TEXT("Switching does not reset C"),
		PersistenceEngine->GetSnapshot().GetEscapeAttemptCount(), 2U);
	TestTrue(TEXT("The switched-in battler can be made too slow before selection"),
		FBattleC09BWildFlowEngineFixture::SetPermanentSpeed(
			*PersistenceEngine,
			MakeNumericId<FBattlerId>(PlayerRightValue),
			3));
	TestTrue(TEXT("The fourth turn begins"), AdvanceFromEndTurn(*PersistenceEngine));
	const FBattleDecisionRequest BlockedRequest = PersistenceEngine->GetPendingDecision().GetValue();
	TestFalse(TEXT("The now-blocked actor is not offered Run"),
		BlockedRequest.GetLegalActionKinds().Contains(EBattleActionKind::Run));
	const FBattleResolution BlockedRun = PersistenceEngine->SubmitDecision(
		MakeDecision(BlockedRequest, EBattleActionKind::Run));
	TestFalse(TEXT("The forged blocked attempt is rejected"), BlockedRun.WasAccepted());
	TestEqual(TEXT("The blocked attempt does not increment C"),
		PersistenceEngine->GetSnapshot().GetEscapeAttemptCount(), 2U);
	TestEqual(TEXT("Only the legal failed Run drew RNG"),
		PersistenceEngine->ExportRandomTrace().Num(), 1);

	FWildFlowScenario ReplayScenario;
	TUniquePtr<FBattleEngine> First;
	TUniquePtr<FBattleEngine> Second;
	TestTrue(TEXT("First replay engine is created"),
		TryMakeEngine(ReplayScenario, {94, 0}, First));
	TestTrue(TEXT("Second replay engine is created"),
		TryMakeEngine(ReplayScenario, {94, 0}, Second));
	TestTrue(TEXT("First failed-then-successful Run sequence resolves"),
		DriveFailedThenSuccessfulRun(*First));
	TestTrue(TEXT("Second failed-then-successful Run sequence resolves"),
		DriveFailedThenSuccessfulRun(*Second));
	const FBattleSnapshot Final = First->GetSnapshot();
	TestEqual(TEXT("Successful Run ends in Escape"), Final.GetOutcome(), EBattleOutcome::Escape);
	TestEqual(TEXT("Player Run uses the ordinary outcome cause"),
		Final.GetOutcomeCause(), EBattleOutcomeCause::Ordinary);
	TestEqual(TEXT("The failed-attempt counter persists through success"),
		Final.GetEscapeAttemptCount(), 2U);
	TArray<uint8> FirstBytes;
	TArray<uint8> SecondBytes;
	TestTrue(TEXT("First Run replay serializes"), SerializeReplay(*First, FirstBytes));
	TestTrue(TEXT("Second Run replay serializes"), SerializeReplay(*Second, SecondBytes));
	TestTrue(TEXT("Identical Run histories replay equally"),
		!FirstBytes.IsEmpty() && FirstBytes == SecondBytes);
	TestEqual(TEXT("Run preserves replay schema 6"),
		First->ExportReplayRecord().GetSchemaVersion(), 6U);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC09BWildFleePolicyTest,
	"PokemonSolarus.Battle.C09B.WildFlee.PolicyProbabilityRemovalAndOutcome",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC09BWildFleePolicyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FBattleWildFleeCalculationInput PolicyInput;
	PolicyInput.RandomContext = MakeRandomContext();
	FSequenceBattleRandom DisabledRandom({});
	PolicyInput.Policy = MakeWildFleePolicy(EBattleWildFleeMode::Disabled);
	FBattleWildFleeCalculationResult PolicyResult;
	TestFalse(TEXT("Disabled is not an executable policy"),
		FBattleWildFleeRules::TryResolve(PolicyInput, DisabledRandom, PolicyResult));
	TestEqual(TEXT("Disabled consumes no RNG"), DisabledRandom.GetTrace().Num(), 0);

	FSequenceBattleRandom NeverRandom({});
	PolicyInput.Policy = MakeWildFleePolicy(EBattleWildFleeMode::Never);
	TestTrue(TEXT("Never resolves as a legal attempt"),
		FBattleWildFleeRules::TryResolve(PolicyInput, NeverRandom, PolicyResult));
	TestFalse(TEXT("Never does not flee"), PolicyResult.bSucceeded);
	TestEqual(TEXT("Never consumes no RNG"), NeverRandom.GetTrace().Num(), 0);

	FSequenceBattleRandom AlwaysRandom({});
	PolicyInput.Policy = MakeWildFleePolicy(EBattleWildFleeMode::Always);
	TestTrue(TEXT("Always resolves"),
		FBattleWildFleeRules::TryResolve(PolicyInput, AlwaysRandom, PolicyResult));
	TestTrue(TEXT("Always flees"), PolicyResult.bSucceeded);
	TestEqual(TEXT("Always consumes no RNG"), AlwaysRandom.GetTrace().Num(), 0);

	PolicyInput.Policy = MakeWildFleePolicy(EBattleWildFleeMode::Chance);
	PolicyInput.Policy.Numerator = 2;
	PolicyInput.Policy.Denominator = 5;
	FSequenceBattleRandom ChanceSuccessRandom({1});
	TestTrue(TEXT("Chance success boundary resolves"),
		FBattleWildFleeRules::TryResolve(
			PolicyInput,
			ChanceSuccessRandom,
			PolicyResult));
	TestTrue(TEXT("Numerator minus one succeeds"), PolicyResult.bSucceeded);
	FSequenceBattleRandom ChanceFailureRandom({2});
	TestTrue(TEXT("Chance failure boundary resolves"),
		FBattleWildFleeRules::TryResolve(
			PolicyInput,
			ChanceFailureRandom,
			PolicyResult));
	TestFalse(TEXT("A draw equal to numerator fails"), PolicyResult.bSucceeded);
	TestEqual(TEXT("Chance draws U[0, denominator - 1]"),
		ChanceFailureRandom.GetTrace()[0].InclusiveMaximum, 4U);
	TestTrue(TEXT("Chance draw has the typed purpose"),
		ChanceFailureRandom.GetTrace()[0].RulePurpose
			== FBattleWildFleeRules::GetRandomCheckPurpose());

	FWildFlowScenario DisabledScenario;
	TUniquePtr<FBattleEngine> DisabledEngine;
	TestTrue(TEXT("Disabled engine is created"),
		TryMakeEngine(DisabledScenario, {}, DisabledEngine));
	TestEqual(TEXT("Disabled creates no runtime WildFlee policy"),
		FBattleC09BWildFlowEngineFixture::GetWildFleePolicies(*DisabledEngine).Num(), 0);
	FBattleRejection BeginRejection;
	TestTrue(TEXT("Disabled action selection begins"),
		DisabledEngine->TryBeginActionDecisionSequence(BeginRejection));
	TestTrue(TEXT("Player choice advances to the opponent"),
		SubmitCurrentRequests(*DisabledEngine));
	const FBattleDecisionRequest DisabledOpponentRequest =
		DisabledEngine->GetPendingDecision().GetValue();
	TestFalse(TEXT("Disabled does not generate WildFlee"),
		DisabledOpponentRequest.GetLegalActionKinds().Contains(EBattleActionKind::WildFlee));

	FWildFlowScenario NeverScenario;
	NeverScenario.WildFleeMode = EBattleWildFleeMode::Never;
	TUniquePtr<FBattleEngine> NeverEngine;
	TestTrue(TEXT("Never engine is created"), TryMakeEngine(NeverScenario, {}, NeverEngine));
	const TConstArrayView<FBattleWildFleePolicyState> NeverPolicies =
		FBattleC09BWildFlowEngineFixture::GetWildFleePolicies(*NeverEngine);
	TestEqual(TEXT("Explicit mode creates one encounter-wide policy"), NeverPolicies.Num(), 1);
	if (!NeverPolicies.IsEmpty())
	{
		TestFalse(TEXT("Encounter-wide policy has no species restriction"),
			NeverPolicies[0].SpeciesFormId.IsValid());
		TestTrue(TEXT("Policy preserves the action-selection trigger"),
			NeverPolicies[0].TriggerId
				== FBattleWildFleeRules::GetActionSelectionTriggerId());
		TestTrue(TEXT("Policy preserves the living-wild eligibility"),
			NeverPolicies[0].EligibilityId
				== FBattleWildFleeRules::GetActiveLivingWildEligibilityId());
	}
	TestTrue(TEXT("Never WildFlee turn locks"),
		LockTurn(*NeverEngine, OpponentLeftValue, EBattleActionKind::WildFlee));
	const int32 WildActionsBefore = FBattleC09BWildFlowEngineFixture::GetRemainingActions(
		*NeverEngine,
		MakeNumericId<FTrainerId>(OpponentTrainerValue));
	const int32 NeverPPBefore = FBattleC09BWildFlowEngineFixture::GetMovePP(
		*NeverEngine,
		MakeNumericId<FBattlerId>(OpponentLeftValue));
	FBattleResolution NeverAttempt;
	TestTrue(TEXT("Never attempt resolves legally"),
		StartAndResolveWildAction(*NeverEngine, EBattleActionKind::WildFlee, NeverAttempt));
	TestEqual(TEXT("The legal failed attempt consumes the wild action"),
		FBattleC09BWildFlowEngineFixture::GetRemainingActions(
			*NeverEngine,
			MakeNumericId<FTrainerId>(OpponentTrainerValue)),
		WildActionsBefore - 1);
	TestFalse(TEXT("Never emits no Escaped event"),
		HasEvent(NeverAttempt, EBattleEventType::Escaped));
	TestTrue(TEXT("Never leaves the wild battler active"),
		FBattleC09BWildFlowEngineFixture::IsActive(
			*NeverEngine,
			MakeNumericId<FBattlerId>(OpponentLeftValue)));
	TestEqual(TEXT("A WildFlee attempt spends no PP"),
		FBattleC09BWildFlowEngineFixture::GetMovePP(
			*NeverEngine,
			MakeNumericId<FBattlerId>(OpponentLeftValue)),
		NeverPPBefore);
	TestEqual(TEXT("Never consumes no RNG"), NeverEngine->ExportRandomTrace().Num(), 0);

	FWildFlowScenario AlwaysScenario;
	AlwaysScenario.Format = EBattleFormat::Double;
	AlwaysScenario.WildFleeMode = EBattleWildFleeMode::Always;
	TUniquePtr<FBattleEngine> AlwaysEngine;
	TestTrue(TEXT("Always double engine is created"),
		TryMakeEngine(AlwaysScenario, {}, AlwaysEngine));
	const int32 AlwaysPPBefore = FBattleC09BWildFlowEngineFixture::GetMovePP(
		*AlwaysEngine,
		MakeNumericId<FBattlerId>(OpponentLeftValue));
	TestTrue(TEXT("Always WildFlee turn locks"),
		LockTurn(*AlwaysEngine, OpponentLeftValue, EBattleActionKind::WildFlee));
	FBattleResolution AlwaysAttempt;
	TestTrue(TEXT("Always WildFlee resolves"),
		StartAndResolveWildAction(*AlwaysEngine, EBattleActionKind::WildFlee, AlwaysAttempt));
	TestTrue(TEXT("Always emits Escaped"), HasEvent(AlwaysAttempt, EBattleEventType::Escaped));
	TestTrue(TEXT("Always emits Removed"), HasEvent(AlwaysAttempt, EBattleEventType::Removed));
	TestTrue(TEXT("The fleeing battler is removed"),
		FBattleC09BWildFlowEngineFixture::IsRemoved(
			*AlwaysEngine,
			MakeNumericId<FBattlerId>(OpponentLeftValue)));
	TestFalse(TEXT("The fleeing battler leaves its active slot"),
		FBattleC09BWildFlowEngineFixture::IsActive(
			*AlwaysEngine,
			MakeNumericId<FBattlerId>(OpponentLeftValue)));
	TestTrue(TEXT("The other wild opponent remains active"),
		FBattleC09BWildFlowEngineFixture::IsActive(
			*AlwaysEngine,
			MakeNumericId<FBattlerId>(OpponentRightValue)));
	TestEqual(TEXT("Multi-wild battle remains in progress"),
		AlwaysEngine->GetSnapshot().GetOutcome(), EBattleOutcome::InProgress);
	TestEqual(TEXT("Successful WildFlee spends no PP"),
		FBattleC09BWildFlowEngineFixture::GetMovePP(
			*AlwaysEngine,
			MakeNumericId<FBattlerId>(OpponentLeftValue)),
		AlwaysPPBefore);
	TestEqual(TEXT("Always consumes no RNG"), AlwaysEngine->ExportRandomTrace().Num(), 0);

	FWildFlowScenario ChanceScenario;
	ChanceScenario.WildFleeMode = EBattleWildFleeMode::Chance;
	ChanceScenario.WildFleeNumerator = 1;
	ChanceScenario.WildFleeDenominator = 2;
	TUniquePtr<FBattleEngine> FirstChance;
	TUniquePtr<FBattleEngine> SecondChance;
	TestTrue(TEXT("First Chance engine is created"),
		TryMakeEngine(ChanceScenario, {0}, FirstChance));
	TestTrue(TEXT("Second Chance engine is created"),
		TryMakeEngine(ChanceScenario, {0}, SecondChance));
	FBattleResolution FirstChanceAttempt;
	FBattleResolution SecondChanceAttempt;
	TestTrue(TEXT("First Chance turn locks"),
		LockTurn(*FirstChance, OpponentLeftValue, EBattleActionKind::WildFlee));
	TestTrue(TEXT("First Chance succeeds"),
		StartAndResolveWildAction(
			*FirstChance,
			EBattleActionKind::WildFlee,
			FirstChanceAttempt));
	TestTrue(TEXT("Second Chance turn locks"),
		LockTurn(*SecondChance, OpponentLeftValue, EBattleActionKind::WildFlee));
	TestTrue(TEXT("Second Chance succeeds"),
		StartAndResolveWildAction(
			*SecondChance,
			EBattleActionKind::WildFlee,
			SecondChanceAttempt));
	const FBattleSnapshot ChanceFinal = FirstChance->GetSnapshot();
	TestEqual(TEXT("Last wild flee ends in Escape"),
		ChanceFinal.GetOutcome(), EBattleOutcome::Escape);
	TestEqual(TEXT("Last wild flee records OpponentFled"),
		ChanceFinal.GetOutcomeCause(), EBattleOutcomeCause::OpponentFled);
	TestEqual(TEXT("Last wild flee enters Terminal"),
		ChanceFinal.GetPhase(), EBattlePhase::Terminal);
	const int32 AttemptedIndex = FindEventIndex(
		FirstChanceAttempt,
		EBattleEventType::RunAttempted);
	const int32 EscapedIndex = FindEventIndex(
		FirstChanceAttempt,
		EBattleEventType::Escaped);
	const int32 RemovedIndex = FindEventIndex(
		FirstChanceAttempt,
		EBattleEventType::Removed);
	const int32 CompletedIndex = FindEventIndex(
		FirstChanceAttempt,
		EBattleEventType::ActionCompleted);
	const int32 EndedIndex = FindEventIndex(
		FirstChanceAttempt,
		EBattleEventType::BattleEnded);
	TestTrue(TEXT("WildFlee terminal facts retain causal order"),
		AttemptedIndex != INDEX_NONE
			&& EscapedIndex > AttemptedIndex
			&& RemovedIndex > EscapedIndex
			&& CompletedIndex > RemovedIndex
			&& EndedIndex > CompletedIndex);
	TestEqual(TEXT("Chance consumes exactly one draw"),
		FirstChance->ExportRandomTrace().Num(), 1);
	TArray<uint8> FirstChanceBytes;
	TArray<uint8> SecondChanceBytes;
	TestTrue(TEXT("First WildFlee replay serializes"),
		SerializeReplay(*FirstChance, FirstChanceBytes));
	TestTrue(TEXT("Second WildFlee replay serializes"),
		SerializeReplay(*SecondChance, SecondChanceBytes));
	TestTrue(TEXT("Identical WildFlee histories replay equally"),
		!FirstChanceBytes.IsEmpty() && FirstChanceBytes == SecondChanceBytes);
	TestEqual(TEXT("WildFlee preserves replay schema 6"),
		FirstChance->ExportReplayRecord().GetSchemaVersion(), 6U);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
