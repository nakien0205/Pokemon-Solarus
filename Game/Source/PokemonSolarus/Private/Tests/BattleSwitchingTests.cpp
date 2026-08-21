#if WITH_DEV_AUTOMATION_TESTS

#include "Battle/BattleEngine.h"
#include "Battle/BattleState.h"
#include "Battle/BattleSwitching.h"
#include "BattleTestFactories.h"
#include "Misc/AutomationTest.h"

namespace BattleSwitchingTests
{
	using BattleTest::MakeActiveSlotId;
	using BattleTest::MakeDefinitionId;
	using BattleTest::MakeNumericId;
	using BattleTest::MakePartySlotId;

	constexpr uint64 PlayerTrainerValue = 1;
	constexpr uint64 OpponentTrainerValue = 2;
	constexpr uint64 PlayerLeftValue = 11;
	constexpr uint64 PlayerRightValue = 12;
	constexpr uint64 PlayerReserveAValue = 13;
	constexpr uint64 PlayerReserveBValue = 14;
	constexpr uint64 OpponentLeftValue = 21;
	constexpr uint64 OpponentRightValue = 22;
	constexpr uint64 OpponentReserveAValue = 23;
	constexpr uint64 OpponentReserveBValue = 24;

	const TCHAR* NeutralMoveName = TEXT("Move.C06A.Neutral");
	const TCHAR* ForcedMoveName = TEXT("Move.C06A.ForcedSwitch");
	const TCHAR* PivotMoveName = TEXT("Move.C06A.PivotSwitch");
	const TCHAR* PivotRecoilMoveName = TEXT("Move.C06A.PivotRecoil");
	const TCHAR* SpeciesName = TEXT("Species.C06A.Common");
	const TCHAR* AbilityName = TEXT("Ability.C06A.Persistent");
	const TCHAR* HeldItemName = TEXT("Item.C06A.Persistent");
	const TCHAR* MajorStatusName = TEXT("Condition.C06A.Major");
	const TCHAR* VolatileName = TEXT("Condition.C06A.Volatile");

	struct FC06AScenario
	{
		EBattleEncounterKind EncounterKind = EBattleEncounterKind::Trainer;
		EBattleFormat Format = EBattleFormat::Single;
		const TCHAR* PlayerLeftMove = NeutralMoveName;
		const TCHAR* OpponentLeftMove = NeutralMoveName;
		int32 PlayerReserveCount = 2;
		int32 OpponentReserveCount = 1;
		int32 PlayerLeftHP = 200;
	};

	TArray<FBattleTypeChartEntry> MakeTypeChart()
	{
		TArray<FBattleTypeChartEntry> Entries;
		Entries.Reserve(FBattleTypeChart::EntryCount);
		for (int32 Attacking = 0; Attacking < FBattleTypeChart::TypeCount; ++Attacking)
		{
			for (int32 Defending = 0; Defending < FBattleTypeChart::TypeCount; ++Defending)
			{
				Entries.Add(
					{
						static_cast<EPokemonType>(Attacking),
						static_cast<EPokemonType>(Defending),
						1,
						1
					});
			}
		}
		return Entries;
	}

	FBattleMoveDefinition MakeNeutralMove()
	{
		FBattleMoveDefinition Move;
		Move.Id = MakeDefinitionId<FMoveId>(NeutralMoveName);
		Move.Type = EPokemonType::Normal;
		Move.Category = EBattleMoveCategory::Physical;
		Move.Power = 40;
		Move.bAlwaysHits = true;
		Move.Accuracy = 0;
		Move.BasePP = 20;
		Move.TargetClass = EBattleTargetClass::SelectedOpponent;
		FBattleMoveEffectDescriptor Damage;
		Damage.Order = 0;
		Damage.Kind = EBattleMoveEffectKind::Damage;
		Damage.Target = EBattleEffectTarget::ResolvedTarget;
		Move.Effects.Add(Damage);
		return Move;
	}

	FBattleMoveDefinition MakeSwitchMove(
		const TCHAR* Name,
		const EBattleEffectTarget Target)
	{
		FBattleMoveDefinition Move;
		Move.Id = MakeDefinitionId<FMoveId>(Name);
		Move.Type = EPokemonType::Normal;
		Move.Category = EBattleMoveCategory::Status;
		Move.Power = 0;
		Move.bAlwaysHits = true;
		Move.Accuracy = 0;
		Move.BasePP = 20;
		Move.TargetClass = EBattleTargetClass::SelectedOpponent;
		FBattleMoveEffectDescriptor Switch;
		Switch.Order = 0;
		Switch.Kind = EBattleMoveEffectKind::Switch;
		Switch.Target = Target;
		Move.Effects.Add(Switch);
		return Move;
	}

	FBattleMoveDefinition MakePivotRecoilMove()
	{
		FBattleMoveDefinition Move = MakeNeutralMove();
		Move.Id = MakeDefinitionId<FMoveId>(PivotRecoilMoveName);

		FBattleMoveEffectDescriptor Switch;
		Switch.Order = 1;
		Switch.Kind = EBattleMoveEffectKind::Switch;
		Switch.Target = EBattleEffectTarget::User;
		Move.Effects.Add(Switch);

		FBattleMoveEffectDescriptor Recoil;
		Recoil.Order = 2;
		Recoil.Kind = EBattleMoveEffectKind::Recoil;
		Recoil.Target = EBattleEffectTarget::User;
		Recoil.MagnitudeNumerator = 1;
		Recoil.MagnitudeDenominator = 1;
		Recoil.Flags = EBattleMoveEffectFlags::UsesActualDamage;
		Move.Effects.Add(Recoil);
		return Move;
	}

	FBattleDefinitionCatalog MakeCatalog()
	{
		FBattleDefinitionCatalogInput Input;
		Input.TypeChartEntries = MakeTypeChart();
		Input.Moves.Add(MakeNeutralMove());
		Input.Moves.Add(MakeSwitchMove(ForcedMoveName, EBattleEffectTarget::ResolvedTarget));
		Input.Moves.Add(MakeSwitchMove(PivotMoveName, EBattleEffectTarget::User));
		Input.Moves.Add(MakePivotRecoilMove());
		Input.Abilities.Add({MakeDefinitionId<FAbilityId>(AbilityName)});
		Input.Items.Add({MakeDefinitionId<FItemId>(HeldItemName), EBattleItemKind::Held});
		Input.Conditions.Add(
			{MakeDefinitionId<FConditionId>(MajorStatusName), EBattleConditionKind::MajorStatus});
		Input.Conditions.Add(
			{MakeDefinitionId<FConditionId>(VolatileName), EBattleConditionKind::Volatile});

		FBattleSpeciesFormDefinition Species;
		Species.Id = MakeDefinitionId<FSpeciesFormId>(SpeciesName);
		Species.PrimaryType = EPokemonType::Normal;
		Species.BaseStats = {80, 80, 80, 80, 80, 80};
		Species.CatchRate = 45;
		Species.AbilityChoices.Add(MakeDefinitionId<FAbilityId>(AbilityName));
		Input.SpeciesForms.Add(Species);

		FBattleDefinitionCatalog Catalog;
		TArray<FBattleCatalogDiagnostic> Diagnostics;
		const bool bCreated = FBattleDefinitionCatalog::TryCreate(Input, Catalog, Diagnostics);
		check(bCreated);
		return Catalog;
	}

	FBattleTrainerSetup MakeTrainer(
		const uint64 Value,
		const EBattleSide Side,
		const EBattleTrainerRole Role,
		const EBattleDecisionController Controller)
	{
		FBattleTrainerSetup Trainer;
		Trainer.TrainerId = MakeNumericId<FTrainerId>(Value);
		Trainer.Side = Side;
		Trainer.Role = Role;
		Trainer.Controller = Controller;
		Trainer.SelectorProfileId = MakeDefinitionId<FDefinitionId>(
			Role == EBattleTrainerRole::Player
				? TEXT("Selector.C06A.Player")
				: TEXT("Selector.C06A.Opponent"));
		return Trainer;
	}

	FBattlePartyEntrySetup MakePartyEntry(
		const uint64 TrainerValue,
		const uint64 BattlerValue,
		const int32 PartyIndex,
		const TCHAR* MoveName,
		const int32 Speed,
		const int32 CurrentHP = 200)
	{
		FBattlePartyEntrySetup Entry;
		Entry.TrainerId = MakeNumericId<FTrainerId>(TrainerValue);
		Entry.BattlerId = MakeNumericId<FBattlerId>(BattlerValue);
		Entry.SourcePokemonId = MakeNumericId<FSourcePokemonId>(1000 + BattlerValue);
		Entry.PartySlotId = MakePartySlotId(PartyIndex);
		Entry.SpeciesFormId = MakeDefinitionId<FSpeciesFormId>(SpeciesName);
		Entry.Level = 50;
		Entry.Stats = {200, 100, 100, 100, 100, Speed};
		Entry.CurrentHP = CurrentHP;
		Entry.AbilityId = MakeDefinitionId<FAbilityId>(AbilityName);
		Entry.OriginalHeldItemId = MakeDefinitionId<FItemId>(HeldItemName);
		Entry.CurrentHeldItemId = Entry.OriginalHeldItemId;
		Entry.Moves.Add({0, MakeDefinitionId<FMoveId>(MoveName), 10, 20});
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
			MakeNumericId<FBattlerId>(BattlerValue)
		};
	}

	FBattleSetupInput MakeSetupInput(const FC06AScenario& Scenario)
	{
		FBattleSetupInput Input;
		Input.BattleId = MakeNumericId<FBattleId>(606);
		Input.SettingsReference =
			{MakeDefinitionId<FDefinitionId>(TEXT("Settings.C06A")), 1};
		Input.CatalogReference =
			{MakeDefinitionId<FDefinitionId>(TEXT("Catalog.C06A")), 1};
		Input.EncounterKind = Scenario.EncounterKind;
		Input.Format = Scenario.Format;
		Input.CaptureCapacity = {0, 100};
		Input.Policies.bBagAllowed = false;
		Input.Policies.bRunAllowed = Scenario.EncounterKind == EBattleEncounterKind::Wild;
		Input.Policies.bCaptureAllowed = false;

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
			Scenario.PlayerLeftMove,
			120,
			Scenario.PlayerLeftHP));
		Input.PartyEntries.Add(MakePartyEntry(
			OpponentTrainerValue,
			OpponentLeftValue,
			0,
			Scenario.OpponentLeftMove,
			80));
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

		int32 PlayerReserveStart = 1;
		int32 OpponentReserveStart = 1;
		if (Scenario.Format == EBattleFormat::Double)
		{
			Input.PartyEntries.Add(MakePartyEntry(
				PlayerTrainerValue,
				PlayerRightValue,
				1,
				NeutralMoveName,
				110));
			Input.PartyEntries.Add(MakePartyEntry(
				OpponentTrainerValue,
				OpponentRightValue,
				1,
				NeutralMoveName,
				70));
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
			PlayerReserveStart = 2;
			OpponentReserveStart = 2;
		}

		for (int32 Index = 0; Index < Scenario.PlayerReserveCount; ++Index)
		{
			Input.PartyEntries.Add(MakePartyEntry(
				PlayerTrainerValue,
				Index == 0 ? PlayerReserveAValue : PlayerReserveBValue,
				PlayerReserveStart + Index,
				NeutralMoveName,
				90 - Index));
		}
		for (int32 Index = 0; Index < Scenario.OpponentReserveCount; ++Index)
		{
			Input.PartyEntries.Add(MakePartyEntry(
				OpponentTrainerValue,
				Index == 0 ? OpponentReserveAValue : OpponentReserveBValue,
				OpponentReserveStart + Index,
				NeutralMoveName,
				60 - Index));
		}
		return Input;
	}

	FBattleSetup MakeSetup(const FC06AScenario& Scenario)
	{
		FBattleSetup Setup;
		EBattleSetupValidationError Error = EBattleSetupValidationError::None;
		const bool bCreated = FBattleSetup::TryCreate(MakeSetupInput(Scenario), Setup, Error);
		check(bCreated);
		return Setup;
	}

	TUniquePtr<FBattleEngine> MakeEngine(
		const FC06AScenario& Scenario,
		const uint64 Seed = 606)
	{
		TUniquePtr<FBattleEngine> Engine;
		FBattleRejection Rejection;
		const bool bCreated = FBattleEngine::TryCreate(
			MakeSetup(Scenario),
			MakeCatalog(),
			MakeUnique<FSeededBattleRandom>(Seed),
			Engine,
			Rejection);
		check(bCreated);
		return Engine;
	}

	FBattleDecision MakeFightDecision(const FBattleDecisionRequest& Request)
	{
		check(Request.GetLegalMoveIds().Num() == 1);
		const FMoveId MoveId = Request.GetLegalMoveIds()[0];
		const FBattleMoveTargetOption* Pair = Request.GetLegalMoveTargets().FindByPredicate(
			[MoveId](const FBattleMoveTargetOption& Candidate)
			{
				return Candidate.MoveId == MoveId;
			});
		check(Pair != nullptr);
		FBattleDecision Decision;
		const bool bCreated = FBattleDecision::TryCreateFight(
			Request.GetStateVersion(),
			Request.GetDecisionOwnerTrainerId(),
			Request.GetActingBattlerId(),
			MoveId,
			Pair->ActiveSlotId,
			Decision);
		check(bCreated);
		return Decision;
	}

	FBattleDecision MakeSwitchDecision(
		const FBattleDecisionRequest& Request,
		const FPartySlotId PartySlotId,
		const uint64 StateVersionOverride = 0,
		const FTrainerId TrainerOverride = FTrainerId())
	{
		FBattleDecision Decision;
		const bool bCreated = FBattleDecision::TryCreateSwitch(
			StateVersionOverride == 0 ? Request.GetStateVersion() : StateVersionOverride,
			Request.GetRequestKind(),
			TrainerOverride.IsValid() ? TrainerOverride : Request.GetDecisionOwnerTrainerId(),
			Request.GetActingBattlerId(),
			PartySlotId,
			Request.GetActingSlotId(),
			Decision);
		check(bCreated);
		return Decision;
	}

	FBattleDecisionBatch MakeBatch(const TArray<FBattleDecision>& Decisions)
	{
		check(!Decisions.IsEmpty());
		FBattleDecisionBatchSpec Spec;
		Spec.StateVersion = Decisions[0].GetStateVersion();
		Spec.RequestKind = Decisions[0].GetRequestKind();
		Spec.DecisionOwnerTrainerId = Decisions[0].GetDecisionOwnerTrainerId();
		Spec.Decisions = Decisions;
		FBattleDecisionBatch Batch;
		FBattleRejection Rejection;
		const bool bCreated = FBattleDecisionBatch::TryCreate(Spec, Batch, Rejection);
		check(bCreated);
		return Batch;
	}

	bool HasUnavailableAction(
		const FBattleDecisionRequest& Request,
		const EBattleActionKind ActionKind,
		const EBattleOptionUnavailableReason Reason)
	{
		return Request.GetUnavailableOptions().ContainsByPredicate(
			[ActionKind, Reason](const FBattleUnavailableDecisionOption& Option)
			{
				return Option.Kind == EBattleDecisionOptionKind::Action
					&& Option.ActionKind == ActionKind
					&& Option.Reason == Reason;
			});
	}

	TArray<EBattleEventType> EventTypes(const FBattleResolution& Resolution)
	{
		TArray<EBattleEventType> Types;
		for (const FBattleEvent& Event : Resolution.GetEvents())
		{
			Types.Add(Event.GetType());
		}
		return Types;
	}

	bool HasEvent(const FBattleResolution& Resolution, const EBattleEventType Type)
	{
		return Resolution.GetEvents().ContainsByPredicate(
			[Type](const FBattleEvent& Event)
			{
				return Event.GetType() == Type;
			});
	}

	void LockSingleTurn(
		FBattleEngine& Engine,
		const FBattleDecision& PlayerDecision)
	{
		check(Engine.SubmitDecision(PlayerDecision).WasAccepted());
		const TArray<FBattleDecisionRequest> OpponentRequests = Engine.GetPendingDecisionRequests();
		check(OpponentRequests.Num() == 1);
		check(Engine.SubmitDecision(MakeFightDecision(OpponentRequests[0])).WasAccepted());
		check(Engine.GetSnapshot().GetPhase() == EBattlePhase::Locked);
	}

	void AdvanceFightToEffects(FBattleEngine& Engine)
	{
		check(Engine.BeginNextLockedAction().WasAccepted());
		check(Engine.CommitCurrentMoveAfterPreMoveGates().WasAccepted());
		check(Engine.ResolveCurrentMoveTargets().WasAccepted());
	}

	FBattleSwitchCandidateFacts MakeCandidate(
		const int32 PartyIndex,
		const uint64 TrainerValue,
		const uint64 BattlerValue)
	{
		FBattleSwitchCandidateFacts Candidate;
		Candidate.PartySlotId = MakePartySlotId(PartyIndex);
		Candidate.bOccupied = BattlerValue != 0;
		if (Candidate.bOccupied)
		{
			Candidate.TrainerId = MakeNumericId<FTrainerId>(TrainerValue);
			Candidate.BattlerId = MakeNumericId<FBattlerId>(BattlerValue);
		}
		return Candidate;
	}

	FBattleSwitchLegalitySpec MakeLegalitySpec(const EBattleSwitchKind Kind)
	{
		FBattleSwitchLegalitySpec Spec;
		Spec.Kind = Kind;
		Spec.ActingTrainerId = MakeNumericId<FTrainerId>(PlayerTrainerValue);
		Spec.ActingBattlerId = MakeNumericId<FBattlerId>(PlayerLeftValue);
		Spec.ActiveSlotId = MakeActiveSlotId(EBattleSide::Player, EBattlePosition::Left);
		Spec.TransferPolicy = EBattleSwitchStateTransferPolicy::ClearTransient;
		Spec.Candidates.Add(MakeCandidate(0, PlayerTrainerValue, PlayerLeftValue));
		Spec.Candidates.Last().bAlreadyActive = true;
		Spec.Candidates.Add(MakeCandidate(1, PlayerTrainerValue, PlayerReserveAValue));
		return Spec;
	}
}

class FBattleC06AEngineFixture
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

	static void SeedPersistentAndTransientFacts(FBattleEngine& Engine)
	{
		using namespace BattleSwitchingTests;
		FBattleEngineState& State = GetMutableState(Engine);
		FBattleBattlerState* Battler = State.FindMutableBattler(
			MakeNumericId<FBattlerId>(PlayerLeftValue));
		check(Battler != nullptr);
		const FBattleStatStageChangeResult Change = Battler->Stages.ApplyChange(
			EBattleStat::Attack,
			2);
		check(Change.Outcome == EBattleStatStageChangeOutcome::Applied);
		Battler->MajorStatusId = MakeDefinitionId<FConditionId>(MajorStatusName);
		FBattleConditionState Volatile;
		Volatile.ConditionId = MakeDefinitionId<FConditionId>(VolatileName);
		Volatile.RemainingTurns = 3;
		Volatile.CreationOrdinal = State.NextConditionCreationOrdinal++;
		Volatile.SourceBattlerId = Battler->BattlerId;
		Battler->Volatiles.Add(Volatile);
		Battler->HeldItem.bSuppressed = true;
	}

	static const FBattleBattlerState& GetBattler(
		const FBattleEngine& Engine,
		const uint64 BattlerValue)
	{
		const FBattleBattlerState* Battler = GetState(Engine).FindBattler(
			BattleSwitchingTests::MakeNumericId<FBattlerId>(BattlerValue));
		check(Battler != nullptr);
		return *Battler;
	}

	static FBattlerId GetOccupant(
		const FBattleEngine& Engine,
		const EBattleSide Side,
		const EBattlePosition Position)
	{
		const FBattleActivePositionState* Active = GetState(Engine).FindActivePosition(
			BattleSwitchingTests::MakeActiveSlotId(Side, Position));
		check(Active != nullptr);
		return Active->BattlerId;
	}

	static int32 GetRemainingActions(
		const FBattleEngine& Engine,
		const uint64 TrainerValue)
	{
		const FBattleTrainerState* Trainer = GetState(Engine).FindTrainer(
			BattleSwitchingTests::MakeNumericId<FTrainerId>(TrainerValue));
		check(Trainer != nullptr);
		return Trainer->ActionAllowance.RemainingActions;
	}
};

namespace BattleSwitchingTests
{
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FC06APartyReserveRulesTest,
		"PokemonSolarus.Battle.C06A.Legality.PartyReserveRules",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FC06APartyReserveRulesTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		FBattleSwitchLegalitySpec Spec = MakeLegalitySpec(EBattleSwitchKind::Voluntary);
		Spec.Candidates.Add(MakeCandidate(2, PlayerTrainerValue, 15));
		Spec.Candidates.Last().bFainted = true;
		Spec.Candidates.Add(MakeCandidate(3, PlayerTrainerValue, 16));
		Spec.Candidates.Last().bEgg = true;
		Spec.Candidates.Add(MakeCandidate(4, PlayerTrainerValue, 17));
		Spec.Candidates.Last().bCaptured = true;
		Spec.Candidates.Add(MakeCandidate(5, PlayerTrainerValue, 18));
		Spec.Candidates.Last().bRemoved = true;

		FBattleSwitchLegalityResult Legality;
		TestTrue(TEXT("A six-slot party legality request is valid"),
			FBattleSwitchResolver::TryBuildLegality(Spec, Legality));
		TestEqual(TEXT("Only the living reserve is legal"), Legality.GetLegalPartySlots().Num(), 1);
		TestEqual(TEXT("The legal reserve keeps party-slot order"),
			Legality.GetLegalPartySlots()[0].GetIndex(), 1);
		TestEqual(TEXT("The active party member is rejected as already active"),
			Legality.GetCandidates()[0].Reason, EBattleSwitchBlockReason::AlreadyActive);
		TestEqual(TEXT("A fainted reserve is rejected as fainted"),
			Legality.GetCandidates()[2].Reason, EBattleSwitchBlockReason::Fainted);
		TestEqual(TEXT("An Egg reserve is rejected as an Egg"),
			Legality.GetCandidates()[3].Reason, EBattleSwitchBlockReason::Egg);
		TestEqual(TEXT("A captured reserve is rejected as captured"),
			Legality.GetCandidates()[4].Reason, EBattleSwitchBlockReason::Captured);
		TestEqual(TEXT("A removed reserve is rejected as removed"),
			Legality.GetCandidates()[5].Reason, EBattleSwitchBlockReason::Removed);

		FBattleSwitchLegalitySpec EmptySpec = MakeLegalitySpec(EBattleSwitchKind::Voluntary);
		EmptySpec.Candidates[1] = MakeCandidate(1, 0, 0);
		TestTrue(TEXT("An empty party slot is a valid described candidate"),
			FBattleSwitchResolver::TryBuildLegality(EmptySpec, Legality));
		TestEqual(TEXT("The empty slot is unavailable"), Legality.GetLegalPartySlots().Num(), 0);

		FBattleSwitchLegalitySpec WrongOwnerSpec = MakeLegalitySpec(EBattleSwitchKind::Voluntary);
		WrongOwnerSpec.Candidates[1] = MakeCandidate(1, OpponentTrainerValue, PlayerReserveAValue);
		TestTrue(TEXT("Wrong ownership is described without invalidating the request"),
			FBattleSwitchResolver::TryBuildLegality(WrongOwnerSpec, Legality));
		TestEqual(TEXT("The wrong-owner reserve is unavailable"), Legality.GetLegalPartySlots().Num(), 0);

		FBattleSwitchLegalitySpec ReservedSpec = MakeLegalitySpec(EBattleSwitchKind::Voluntary);
		ReservedSpec.Candidates[1].bAlreadyReserved = true;
		TestTrue(TEXT("An already-reserved slot is described deterministically"),
			FBattleSwitchResolver::TryBuildLegality(ReservedSpec, Legality));
		TestEqual(TEXT("The reserved slot is unavailable"), Legality.GetLegalPartySlots().Num(), 0);

		FBattleSwitchLegalitySpec OversizedSpec = Spec;
		OversizedSpec.Candidates.Add(MakeCandidate(5, PlayerTrainerValue, 19));
		TestFalse(TEXT("More than six candidate slots is rejected"),
			FBattleSwitchResolver::TryBuildLegality(OversizedSpec, Legality));

		FBattleSwitchLegalitySpec BatonSpec = MakeLegalitySpec(EBattleSwitchKind::Voluntary);
		BatonSpec.TransferPolicy = EBattleSwitchStateTransferPolicy::BatonPassLike;
		TestTrue(TEXT("The Baton-style extension policy remains a typed request"),
			FBattleSwitchResolver::TryBuildLegality(BatonSpec, Legality));
		TestTrue(TEXT("The unpopulated Baton-style policy is blocked"), Legality.IsBlocked());
		TestEqual(TEXT("The Baton-style block is typed"),
			Legality.GetBlockReason(), EBattleSwitchBlockReason::UnsupportedTransferPolicy);

		FSeededBattleRandom Random(1);
		FBattleSwitchSelectionSpec SelectionSpec;
		SelectionSpec.RequestedPartySlotId = MakePartySlotId(2);
		FBattleSwitchResolution Resolution;
		FBattleSwitchLegalityResult ValidLegality;
		const bool bBuiltValidLegality = FBattleSwitchResolver::TryBuildLegality(
			Spec,
			ValidLegality);
		TestTrue(TEXT("The legal baseline can be rebuilt for selection"), bBuiltValidLegality);
		TestTrue(TEXT("An illegal requested slot resolves as a typed no-selection result"),
			FBattleSwitchResolver::TryResolve(
				ValidLegality,
				SelectionSpec,
				Random,
				Resolution));
		TestFalse(TEXT("The illegal requested slot selects nothing"), Resolution.HasSelection());
		TestEqual(TEXT("Legality rejection consumes no RNG"), Random.GetTrace().Num(), 0);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FC06AVoluntaryCleanupTest,
		"PokemonSolarus.Battle.C06A.Voluntary.CleanupEventsAndActionCost",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FC06AVoluntaryCleanupTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		TUniquePtr<FBattleEngine> Engine = MakeEngine(FC06AScenario());
		FBattleC06AEngineFixture::SeedPersistentAndTransientFacts(*Engine);

		FBattleRejection Rejection;
		TestTrue(TEXT("Action selection begins"), Engine->TryBeginActionDecisionSequence(Rejection));
		const FBattleDecisionRequest PlayerRequest = Engine->GetPendingDecisionRequests()[0];
		const FBattleDecision Switch = MakeSwitchDecision(PlayerRequest, MakePartySlotId(1));
		LockSingleTurn(*Engine, Switch);

		const FBattleResolution Started = Engine->BeginNextLockedAction();
		TestTrue(TEXT("The voluntary switch action starts"), Started.WasAccepted());
		TestEqual(TEXT("Action start emits exactly one event"), Started.GetEvents().Num(), 1);
		TestEqual(TEXT("The first event is ActionStarted"),
			Started.GetEvents()[0].GetType(), EBattleEventType::ActionStarted);
		TestEqual(TEXT("The switch consumes the acting Trainer's one action"),
			FBattleC06AEngineFixture::GetRemainingActions(*Engine, PlayerTrainerValue), 0);

		const FBattleResolution Switched = Engine->ExecuteCurrentSwitch();
		TestTrue(TEXT("The voluntary switch executes"), Switched.WasAccepted());
		const TArray<EBattleEventType> Expected = {
			EBattleEventType::LeftActiveSlot,
			EBattleEventType::SwitchTransientStateCleared,
			EBattleEventType::EnteredActiveSlot,
			EBattleEventType::Switched,
			EBattleEventType::ActionCompleted
		};
		TestEqual(TEXT("The switch emits the frozen number of transition events"),
			Switched.GetEvents().Num(), Expected.Num());
		for (int32 Index = 0; Index < FMath::Min(Switched.GetEvents().Num(), Expected.Num()); ++Index)
		{
			TestEqual(FString::Printf(TEXT("Switch event %d is in frozen order"), Index),
				Switched.GetEvents()[Index].GetType(), Expected[Index]);
		}
		TestEqual(TEXT("The structural player-left slot now contains the reserve"),
			FBattleC06AEngineFixture::GetOccupant(
				*Engine,
				EBattleSide::Player,
				EBattlePosition::Left),
			MakeNumericId<FBattlerId>(PlayerReserveAValue));

		const FBattleBattlerState& Outgoing = FBattleC06AEngineFixture::GetBattler(
			*Engine,
			PlayerLeftValue);
		TestEqual(TEXT("Current HP persists"), Outgoing.CurrentHP, 200);
		TestEqual(TEXT("Stable party-slot identity persists"),
			Outgoing.PartySlotId, MakePartySlotId(0));
		TestEqual(TEXT("Stable source-Pokemon identity persists"),
			Outgoing.SourcePokemonId,
			MakeNumericId<FSourcePokemonId>(1000 + PlayerLeftValue));
		int32 AttackStage = 99;
		TestTrue(TEXT("Outgoing Attack stage remains queryable"),
			Outgoing.Stages.TryGetStage(EBattleStat::Attack, AttackStage));
		TestEqual(TEXT("Ordinary switching clears stat stages"), AttackStage, 0);
		TestEqual(TEXT("Ordinary switching clears ordinary volatiles"), Outgoing.Volatiles.Num(), 0);
		TestEqual(TEXT("Major status persists"), Outgoing.MajorStatusId,
			MakeDefinitionId<FConditionId>(MajorStatusName));
		TestEqual(TEXT("Ability ownership persists"), Outgoing.AbilityId,
			MakeDefinitionId<FAbilityId>(AbilityName));
		TestEqual(TEXT("Original held item persists"), Outgoing.HeldItem.OriginalItemId,
			MakeDefinitionId<FItemId>(HeldItemName));
		TestEqual(TEXT("Current held item persists"), Outgoing.HeldItem.CurrentItemId,
			MakeDefinitionId<FItemId>(HeldItemName));
		TestTrue(TEXT("Held-item suppression persists"), Outgoing.HeldItem.bSuppressed);
		TestEqual(TEXT("Move PP persists"), Outgoing.Moves[0].CurrentPP, 10);
		TestEqual(TEXT("Voluntary switching consumes no RNG"), Engine->ExportRandomTrace().Num(), 0);

		TUniquePtr<FBattleEngine> StaleEngine = MakeEngine(FC06AScenario());
		TestTrue(TEXT("Stale-target selection begins"),
			StaleEngine->TryBeginActionDecisionSequence(Rejection));
		const FBattleDecisionRequest StalePlayerRequest =
			StaleEngine->GetPendingDecisionRequests()[0];
		LockSingleTurn(
			*StaleEngine,
			MakeSwitchDecision(StalePlayerRequest, MakePartySlotId(1)));
		TestTrue(TEXT("The stale-target switch starts"),
			StaleEngine->BeginNextLockedAction().WasAccepted());
		FBattleBattlerState* StaleReserve =
			FBattleC06AEngineFixture::GetMutableState(*StaleEngine).FindMutableBattler(
				MakeNumericId<FBattlerId>(PlayerReserveAValue));
		TestNotNull(TEXT("The selected reserve exists before revalidation"), StaleReserve);
		if (StaleReserve != nullptr)
		{
			StaleReserve->bEgg = true;
		}
		const FBattleResolution Canceled = StaleEngine->ExecuteCurrentSwitch();
		TestTrue(TEXT("A newly illegal committed switch cancels safely"), Canceled.WasAccepted());
		TestTrue(TEXT("The stale switch emits ActionCanceled"),
			HasEvent(Canceled, EBattleEventType::ActionCanceled));
		TestTrue(TEXT("The stale switch still completes its committed action slot"),
			HasEvent(Canceled, EBattleEventType::ActionCompleted));
		TestEqual(TEXT("The stale switch leaves the outgoing battler active"),
			FBattleC06AEngineFixture::GetOccupant(
				*StaleEngine,
				EBattleSide::Player,
				EBattlePosition::Left),
			MakeNumericId<FBattlerId>(PlayerLeftValue));
		TestEqual(TEXT("Execution-time switch rejection consumes no RNG"),
			StaleEngine->ExportRandomTrace().Num(), 0);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FC06ADoubleSwitchTest,
		"PokemonSolarus.Battle.C06A.Voluntary.DoubleDistinctReservesAndOrder",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FC06ADoubleSwitchTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		FC06AScenario Scenario;
		Scenario.Format = EBattleFormat::Double;
		Scenario.PlayerReserveCount = 2;
		Scenario.OpponentReserveCount = 0;

		TUniquePtr<FBattleEngine> DuplicateEngine = MakeEngine(Scenario);
		FBattleRejection Rejection;
		TestTrue(TEXT("Duplicate scenario selection begins"),
			DuplicateEngine->TryBeginActionDecisionSequence(Rejection));
		const TArray<FBattleDecisionRequest> DuplicateRequests =
			DuplicateEngine->GetPendingDecisionRequests();
		TArray<FBattleDecision> DuplicateDecisions = {
			MakeSwitchDecision(DuplicateRequests[0], MakePartySlotId(2)),
			MakeSwitchDecision(DuplicateRequests[1], MakePartySlotId(2))
		};
		const uint64 DuplicateVersion = DuplicateEngine->GetSnapshot().GetStateVersion();
		const FBattleResolution Duplicate = DuplicateEngine->SubmitDecisionBatch(
			MakeBatch(DuplicateDecisions));
		TestFalse(TEXT("Two allies cannot reserve the same switch target"), Duplicate.WasAccepted());
		TestEqual(TEXT("Duplicate rejection is typed IllegalSwitch"),
			Duplicate.GetRejection().Reason, EBattleRejectionReason::IllegalSwitch);
		TestEqual(TEXT("Atomic duplicate rejection does not advance state"),
			DuplicateEngine->GetSnapshot().GetStateVersion(), DuplicateVersion);

		TUniquePtr<FBattleEngine> Engine = MakeEngine(Scenario);
		TestTrue(TEXT("Distinct scenario selection begins"),
			Engine->TryBeginActionDecisionSequence(Rejection));
		const TArray<FBattleDecisionRequest> PlayerRequests = Engine->GetPendingDecisionRequests();
		TArray<FBattleDecision> PlayerDecisions = {
			MakeSwitchDecision(PlayerRequests[0], MakePartySlotId(2)),
			MakeSwitchDecision(PlayerRequests[1], MakePartySlotId(3))
		};
		TestTrue(TEXT("Distinct allied reserves are accepted atomically"),
			Engine->SubmitDecisionBatch(MakeBatch(PlayerDecisions)).WasAccepted());

		const TArray<FBattleDecisionRequest> OpponentRequests = Engine->GetPendingDecisionRequests();
		TArray<FBattleDecision> OpponentDecisions;
		for (const FBattleDecisionRequest& Request : OpponentRequests)
		{
			OpponentDecisions.Add(MakeFightDecision(Request));
		}
		TestTrue(TEXT("Opponent choices lock the queue"),
			Engine->SubmitDecisionBatch(MakeBatch(OpponentDecisions)).WasAccepted());
		const TArray<FBattleLockedAction> Queue = Engine->GetLockedActions();
		TestEqual(TEXT("Both allied switches lead the queue"),
			Queue[0].Decision.GetActionKind(), EBattleActionKind::Switch);
		TestEqual(TEXT("The second allied action is also a switch"),
			Queue[1].Decision.GetActionKind(), EBattleActionKind::Switch);
		TestEqual(TEXT("Higher-Speed player-left switch is first"),
			Queue[0].Decision.GetActingBattlerId(), MakeNumericId<FBattlerId>(PlayerLeftValue));

		TestTrue(TEXT("First allied switch starts"), Engine->BeginNextLockedAction().WasAccepted());
		TestTrue(TEXT("First allied switch executes"), Engine->ExecuteCurrentSwitch().WasAccepted());
		TestTrue(TEXT("Second allied switch starts"), Engine->BeginNextLockedAction().WasAccepted());
		TestTrue(TEXT("Second allied switch executes"), Engine->ExecuteCurrentSwitch().WasAccepted());
		TestEqual(TEXT("Left slot receives reserve A"),
			FBattleC06AEngineFixture::GetOccupant(*Engine, EBattleSide::Player, EBattlePosition::Left),
			MakeNumericId<FBattlerId>(PlayerReserveAValue));
		TestEqual(TEXT("Right slot receives reserve B"),
			FBattleC06AEngineFixture::GetOccupant(*Engine, EBattleSide::Player, EBattlePosition::Right),
			MakeNumericId<FBattlerId>(PlayerReserveBValue));
		TestEqual(TEXT("Distinct switches consume no RNG"), Engine->ExportRandomTrace().Num(), 0);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FC06ASwitchPreventionTest,
		"PokemonSolarus.Battle.C06A.Prevention.TrappingAndEncounterPolicy",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FC06ASwitchPreventionTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		FBattleSwitchLegalitySpec TrappedSpec = MakeLegalitySpec(EBattleSwitchKind::Voluntary);
		TrappedSpec.Blockers.bTrapped = true;
		TrappedSpec.Blockers.TrappingRuleId = MakeDefinitionId<FDefinitionId>(
			TEXT("Rule.C06A.Trapped"));
		FBattleSwitchLegalityResult Legality;
		TestTrue(TEXT("Typed trapping facts are accepted"),
			FBattleSwitchResolver::TryBuildLegality(TrappedSpec, Legality));
		TestTrue(TEXT("Trapping blocks an ordinary voluntary switch"), Legality.IsBlocked());
		TestEqual(TEXT("Trapping exposes a typed blocker"),
			Legality.GetBlockReason(), EBattleSwitchBlockReason::Trapped);

		FBattleSwitchLegalitySpec PivotSpec = TrappedSpec;
		PivotSpec.Kind = EBattleSwitchKind::Pivot;
		TestTrue(TEXT("The same hook facts can be evaluated for a pivot"),
			FBattleSwitchResolver::TryBuildLegality(PivotSpec, Legality));
		TestFalse(TEXT("Ordinary trapping does not block the typed pivot seam"), Legality.IsBlocked());
		TestEqual(TEXT("The pivot retains its legal reserve"), Legality.GetLegalPartySlots().Num(), 1);

		FBattleSwitchLegalitySpec ForcedSpec = TrappedSpec;
		ForcedSpec.Kind = EBattleSwitchKind::Forced;
		ForcedSpec.Blockers.bEncounterPolicyAllows = false;
		ForcedSpec.Blockers.EncounterPolicyRuleId = MakeDefinitionId<FDefinitionId>(
			TEXT("Rule.C06A.EncounterRestricted"));
		TestTrue(TEXT("Forced switching evaluates through the same seam"),
			FBattleSwitchResolver::TryBuildLegality(ForcedSpec, Legality));
		TestFalse(TEXT("Ordinary encounter and trap blockers do not block forced switching"),
			Legality.IsBlocked());

		FC06AScenario WildScenario;
		WildScenario.EncounterKind = EBattleEncounterKind::Wild;
		TUniquePtr<FBattleEngine> WildEngine = MakeEngine(WildScenario);
		FBattleRejection Rejection;
		TestTrue(TEXT("Wild action selection begins"),
			WildEngine->TryBeginActionDecisionSequence(Rejection));
		const FBattleDecisionRequest PlayerRequest = WildEngine->GetPendingDecisionRequests()[0];
		TestTrue(TEXT("The player may switch in a wild battle"),
			PlayerRequest.GetLegalActionKinds().Contains(EBattleActionKind::Switch));
		TestTrue(TEXT("The player fight advances to the wild opponent request"),
			WildEngine->SubmitDecision(MakeFightDecision(PlayerRequest)).WasAccepted());
		const FBattleDecisionRequest WildOpponentRequest = WildEngine->GetPendingDecisionRequests()[0];
		TestFalse(TEXT("A wild opponent has no ordinary party switch action"),
			WildOpponentRequest.GetLegalActionKinds().Contains(EBattleActionKind::Switch));
		TestTrue(TEXT("Wild ordinary switching exposes a typed restricted reason"),
			HasUnavailableAction(
				WildOpponentRequest,
				EBattleActionKind::Switch,
				EBattleOptionUnavailableReason::SwitchRestricted));

		TUniquePtr<FBattleEngine> TrainerEngine = MakeEngine(FC06AScenario());
		TestTrue(TEXT("Trainer action selection begins"),
			TrainerEngine->TryBeginActionDecisionSequence(Rejection));
		const FBattleDecisionRequest TrainerPlayerRequest =
			TrainerEngine->GetPendingDecisionRequests()[0];
		TestTrue(TEXT("Player fight advances to the Trainer opponent request"),
			TrainerEngine->SubmitDecision(MakeFightDecision(TrainerPlayerRequest)).WasAccepted());
		const FBattleDecisionRequest TrainerOpponentRequest =
			TrainerEngine->GetPendingDecisionRequests()[0];
		TestTrue(TEXT("A Trainer opponent may switch to its own reserve"),
			TrainerOpponentRequest.GetLegalActionKinds().Contains(EBattleActionKind::Switch));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FC06AForcedSwitchTest,
		"PokemonSolarus.Battle.C06A.Forced.RandomReserveAndNoReserve",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FC06AForcedSwitchTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		for (int32 ReserveCount = 0; ReserveCount <= 2; ++ReserveCount)
		{
			FC06AScenario Scenario;
			Scenario.PlayerLeftMove = ForcedMoveName;
			Scenario.OpponentReserveCount = ReserveCount;
			TUniquePtr<FBattleEngine> Engine = MakeEngine(Scenario, 700 + ReserveCount);
			FBattleRejection Rejection;
			TestTrue(FString::Printf(TEXT("Forced scenario %d selection begins"), ReserveCount),
				Engine->TryBeginActionDecisionSequence(Rejection));
			const FBattleDecisionRequest PlayerRequest = Engine->GetPendingDecisionRequests()[0];
			LockSingleTurn(*Engine, MakeFightDecision(PlayerRequest));
			AdvanceFightToEffects(*Engine);
			const FBattleResolution Effects = Engine->ExecuteCurrentMoveEffects();
			TestTrue(FString::Printf(TEXT("Forced scenario %d resolves"), ReserveCount),
				Effects.WasAccepted());

			const TArray<FBattleRandomDraw> Trace = Engine->ExportRandomTrace();
			if (ReserveCount == 0)
			{
				TestEqual(TEXT("No forced reserve means no RNG draw"), Trace.Num(), 0);
				TestEqual(TEXT("No-reserve forced switching leaves the occupant in place"),
					FBattleC06AEngineFixture::GetOccupant(
						*Engine,
						EBattleSide::Opponent,
						EBattlePosition::Left),
					MakeNumericId<FBattlerId>(OpponentLeftValue));
				TestTrue(TEXT("No-reserve forced switching emits EffectFailed"),
					HasEvent(Effects, EBattleEventType::EffectFailed));
				TestFalse(TEXT("No-reserve forced switching emits no deferred placeholder"),
					HasEvent(Effects, EBattleEventType::EffectDeferred));
				continue;
			}

			TestEqual(TEXT("Every non-empty forced reserve list consumes one draw"), Trace.Num(), 1);
			TestEqual(TEXT("The forced draw starts at zero"), Trace[0].InclusiveMinimum, 0U);
			TestEqual(TEXT("The forced draw covers the party-ordered reserve list"),
				Trace[0].InclusiveMaximum, static_cast<uint32>(ReserveCount - 1));
			TestEqual(TEXT("The forced draw uses the frozen rule purpose"),
				Trace[0].RulePurpose, FBattleSwitchResolver::GetForcedSelectionRulePurpose());
			const uint64 ExpectedIncoming = Trace[0].Result == 0
				? OpponentReserveAValue
				: OpponentReserveBValue;
			TestEqual(TEXT("The draw index selects the party-ordered reserve"),
				FBattleC06AEngineFixture::GetOccupant(
					*Engine,
					EBattleSide::Opponent,
					EBattlePosition::Left),
				MakeNumericId<FBattlerId>(ExpectedIncoming));
			TestTrue(TEXT("Forced switching emits LeftActiveSlot"),
				HasEvent(Effects, EBattleEventType::LeftActiveSlot));
			TestTrue(TEXT("Forced switching emits transient cleanup"),
				HasEvent(Effects, EBattleEventType::SwitchTransientStateCleared));
			TestTrue(TEXT("Forced switching emits EnteredActiveSlot"),
				HasEvent(Effects, EBattleEventType::EnteredActiveSlot));
			TestTrue(TEXT("Forced switching emits one entry trigger"),
				HasEvent(Effects, EBattleEventType::Switched));
			TestFalse(TEXT("A concrete forced switch emits no deferred placeholder"),
				HasEvent(Effects, EBattleEventType::EffectDeferred));
			const TArray<EBattleEventType> Expected = {
				EBattleEventType::LeftActiveSlot,
				EBattleEventType::SwitchTransientStateCleared,
				EBattleEventType::EnteredActiveSlot,
				EBattleEventType::Switched,
				EBattleEventType::ActionCompleted
			};
			const int32 TransitionStart = Effects.GetEvents().IndexOfByPredicate(
				[](const FBattleEvent& Event)
				{
					return Event.GetType() == EBattleEventType::LeftActiveSlot;
				});
			TestTrue(TEXT("A concrete forced switch reaches its transition sequence"),
				TransitionStart != INDEX_NONE);
			TestEqual(TEXT("The frozen forced-switch sequence reaches action completion"),
				Effects.GetEvents().Num() - TransitionStart, Expected.Num());
			for (int32 Index = 0;
				TransitionStart != INDEX_NONE
					&& Index < FMath::Min(Effects.GetEvents().Num() - TransitionStart, Expected.Num());
				++Index)
			{
				TestEqual(FString::Printf(TEXT("Forced switch event %d is frozen"), Index),
					Effects.GetEvents()[TransitionStart + Index].GetType(), Expected[Index]);
			}
		}
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FC06APivotSwitchTest,
		"PokemonSolarus.Battle.C06A.Pivot.PostMoveDecisionAndNoExtraAction",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FC06APivotSwitchTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		FC06AScenario Scenario;
		Scenario.PlayerLeftMove = PivotMoveName;
		TUniquePtr<FBattleEngine> Engine = MakeEngine(Scenario);
		FBattleRejection Rejection;
		TestTrue(TEXT("Pivot action selection begins"), Engine->TryBeginActionDecisionSequence(Rejection));
		const FBattleDecisionRequest PlayerRequest = Engine->GetPendingDecisionRequests()[0];
		LockSingleTurn(*Engine, MakeFightDecision(PlayerRequest));
		AdvanceFightToEffects(*Engine);

		const FBattleResolution Effects = Engine->ExecuteCurrentMoveEffects();
		TestTrue(TEXT("Reached pivot effects resolve to a decision checkpoint"), Effects.WasAccepted());
		TestTrue(TEXT("The waiting pivot retains a truthful EffectDeferred event"),
			HasEvent(Effects, EBattleEventType::EffectDeferred));
		TestFalse(TEXT("The original action is not completed before the pivot choice"),
			HasEvent(Effects, EBattleEventType::ActionCompleted));
		const TOptional<FBattleDecisionRequest> Pending = Engine->GetPendingDecision();
		TestTrue(TEXT("A pivot request is published"), Pending.IsSet());
		if (!Pending.IsSet())
		{
			return false;
		}
		const FBattleDecisionRequest PivotRequest = Pending.GetValue();
		TestEqual(TEXT("The request is typed PivotSwitch"),
			PivotRequest.GetRequestKind(), EBattleDecisionRequestKind::PivotSwitch);
		TestEqual(TEXT("Both living reserves are legal pivot destinations"),
			PivotRequest.GetLegalSwitchPartySlots().Num(), 2);
		TestEqual(TEXT("The original Fight already consumed the action"),
			FBattleC06AEngineFixture::GetRemainingActions(*Engine, PlayerTrainerValue), 0);

		const uint64 WaitingVersion = Engine->GetSnapshot().GetStateVersion();
		const int32 WaitingDraws = Engine->ExportRandomTrace().Num();
		const FBattleDecision Stale = MakeSwitchDecision(
			PivotRequest,
			MakePartySlotId(1),
			PivotRequest.GetStateVersion() - 1);
		const FBattleResolution StaleResult = Engine->SubmitDecision(Stale);
		TestFalse(TEXT("A stale pivot response is rejected"), StaleResult.WasAccepted());
		TestEqual(TEXT("Stale rejection is typed"),
			StaleResult.GetRejection().Reason, EBattleRejectionReason::StaleStateVersion);
		TestEqual(TEXT("Stale pivot rejection leaves state unchanged"),
			Engine->GetSnapshot().GetStateVersion(), WaitingVersion);

		const FBattleDecision WrongOwner = MakeSwitchDecision(
			PivotRequest,
			MakePartySlotId(1),
			0,
			MakeNumericId<FTrainerId>(OpponentTrainerValue));
		const FBattleResolution WrongOwnerResult = Engine->SubmitDecision(WrongOwner);
		TestFalse(TEXT("A wrong-owner pivot response is rejected"), WrongOwnerResult.WasAccepted());
		TestEqual(TEXT("Wrong-owner rejection is typed"),
			WrongOwnerResult.GetRejection().Reason, EBattleRejectionReason::WrongDecisionOwner);
		TestTrue(TEXT("Rejected responses leave the pivot request active"),
			Engine->GetPendingDecision().IsSet());
		TestEqual(TEXT("Rejected pivot responses consume no RNG"),
			Engine->ExportRandomTrace().Num(), WaitingDraws);

		FBattleBattlerState* NewlyIllegalReserve =
			FBattleC06AEngineFixture::GetMutableState(*Engine).FindMutableBattler(
				MakeNumericId<FBattlerId>(PlayerReserveAValue));
		TestNotNull(TEXT("The requested pivot reserve exists before revalidation"), NewlyIllegalReserve);
		if (NewlyIllegalReserve != nullptr)
		{
			NewlyIllegalReserve->bEgg = true;
		}
		const FBattleResolution NewlyIllegalResult = Engine->SubmitDecision(
			MakeSwitchDecision(PivotRequest, MakePartySlotId(1)));
		TestFalse(TEXT("A newly illegal pivot target is rejected"),
			NewlyIllegalResult.WasAccepted());
		TestEqual(TEXT("The newly illegal pivot rejection is typed"),
			NewlyIllegalResult.GetRejection().Reason, EBattleRejectionReason::IllegalSwitch);
		TestEqual(TEXT("Pivot target revalidation leaves state unchanged"),
			Engine->GetSnapshot().GetStateVersion(), WaitingVersion);
		TestTrue(TEXT("Pivot target revalidation leaves the request active"),
			Engine->GetPendingDecision().IsSet());
		TestEqual(TEXT("Pivot target revalidation consumes no RNG"),
			Engine->ExportRandomTrace().Num(), WaitingDraws);

		const FBattleDecision Valid = MakeSwitchDecision(PivotRequest, MakePartySlotId(2));
		const FBattleResolution Completed = Engine->SubmitDecision(Valid);
		TestTrue(TEXT("A valid pivot response completes"), Completed.WasAccepted());
		const TArray<EBattleEventType> Expected = {
			EBattleEventType::LeftActiveSlot,
			EBattleEventType::SwitchTransientStateCleared,
			EBattleEventType::EnteredActiveSlot,
			EBattleEventType::Switched,
			EBattleEventType::ActionCompleted
		};
		TestEqual(TEXT("Pivot completion emits the frozen transition sequence length"),
			Completed.GetEvents().Num(), Expected.Num());
		for (int32 Index = 0; Index < FMath::Min(Completed.GetEvents().Num(), Expected.Num()); ++Index)
		{
			TestEqual(FString::Printf(TEXT("Pivot completion event %d is frozen"), Index),
				Completed.GetEvents()[Index].GetType(), Expected[Index]);
		}
		TestFalse(TEXT("The completed pivot clears its request"), Engine->GetPendingDecision().IsSet());
		TestEqual(TEXT("Pivot switching consumes no second action"),
			FBattleC06AEngineFixture::GetRemainingActions(*Engine, PlayerTrainerValue), 0);

		FC06AScenario NoReserveScenario;
		NoReserveScenario.PlayerLeftMove = PivotMoveName;
		NoReserveScenario.PlayerReserveCount = 0;
		TUniquePtr<FBattleEngine> NoReserveEngine = MakeEngine(NoReserveScenario);
		TestTrue(TEXT("No-reserve pivot selection begins"),
			NoReserveEngine->TryBeginActionDecisionSequence(Rejection));
		LockSingleTurn(
			*NoReserveEngine,
			MakeFightDecision(NoReserveEngine->GetPendingDecisionRequests()[0]));
		AdvanceFightToEffects(*NoReserveEngine);
		const FBattleResolution NoReserveEffects = NoReserveEngine->ExecuteCurrentMoveEffects();
		TestTrue(TEXT("A no-reserve pivot finishes without a selector request"),
			NoReserveEffects.WasAccepted());
		TestFalse(TEXT("No-reserve pivot publishes no request"),
			NoReserveEngine->GetPendingDecision().IsSet());
		TestTrue(TEXT("No-reserve pivot completes the action"),
			HasEvent(NoReserveEffects, EBattleEventType::ActionCompleted));
		TestTrue(TEXT("No-reserve pivot reports EffectFailed"),
			HasEvent(NoReserveEffects, EBattleEventType::EffectFailed));

		FC06AScenario GoneScenario;
		GoneScenario.PlayerLeftMove = PivotRecoilMoveName;
		GoneScenario.PlayerLeftHP = 1;
		TUniquePtr<FBattleEngine> GoneEngine = MakeEngine(GoneScenario, 909);
		TestTrue(TEXT("Source-gone pivot selection begins"),
			GoneEngine->TryBeginActionDecisionSequence(Rejection));
		LockSingleTurn(
			*GoneEngine,
			MakeFightDecision(GoneEngine->GetPendingDecisionRequests()[0]));
		AdvanceFightToEffects(*GoneEngine);
		const FBattleResolution GoneEffects = GoneEngine->ExecuteCurrentMoveEffects();
		TestTrue(TEXT("A pivot source that faints to reached recoil resolves"), GoneEffects.WasAccepted());
		TestFalse(TEXT("A removed pivot source publishes no request"),
			GoneEngine->GetPendingDecision().IsSet());
		TestTrue(TEXT("The source-gone pivot completes its action"),
			HasEvent(GoneEffects, EBattleEventType::ActionCompleted));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FC06AIncomingOccupantTargetTest,
		"PokemonSolarus.Battle.C06A.Continuity.IncomingOccupantReceivesQueuedTarget",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FC06AIncomingOccupantTargetTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		TUniquePtr<FBattleEngine> Engine = MakeEngine(FC06AScenario(), 808);
		FBattleRejection Rejection;
		TestTrue(TEXT("Continuity action selection begins"),
			Engine->TryBeginActionDecisionSequence(Rejection));
		const FBattleDecisionRequest PlayerRequest = Engine->GetPendingDecisionRequests()[0];
		LockSingleTurn(
			*Engine,
			MakeSwitchDecision(PlayerRequest, MakePartySlotId(1)));

		TestTrue(TEXT("Earlier switch starts"), Engine->BeginNextLockedAction().WasAccepted());
		const FBattleResolution SwitchResolution = Engine->ExecuteCurrentSwitch();
		TestTrue(TEXT("Earlier switch executes"), SwitchResolution.WasAccepted());
		int32 EntryTriggerCount = 0;
		for (const FBattleEvent& Event : SwitchResolution.GetEvents())
		{
			EntryTriggerCount += Event.GetType() == EBattleEventType::Switched ? 1 : 0;
		}
		TestEqual(TEXT("The incoming battler receives exactly one entry trigger"),
			EntryTriggerCount, 1);

		TestTrue(TEXT("The later queued attack starts"), Engine->BeginNextLockedAction().WasAccepted());
		TestTrue(TEXT("The later queued attack commits PP"),
			Engine->CommitCurrentMoveAfterPreMoveGates().WasAccepted());
		const FBattleResolution Targets = Engine->ResolveCurrentMoveTargets();
		TestTrue(TEXT("The later queued attack resolves its target"), Targets.WasAccepted());
		const FBattleEvent* TargetsResolved = Targets.GetEvents().FindByPredicate(
			[](const FBattleEvent& Event)
			{
				return Event.GetType() == EBattleEventType::TargetsResolved;
			});
		TestNotNull(TEXT("TargetsResolved is emitted"), TargetsResolved);
		if (TargetsResolved != nullptr)
		{
			TestEqual(TEXT("The structural target now names the incoming battler"),
				TargetsResolved->GetTargets()[0].BattlerId,
				MakeNumericId<FBattlerId>(PlayerReserveAValue));
		}

		const int32 OutgoingBefore = FBattleC06AEngineFixture::GetBattler(
			*Engine,
			PlayerLeftValue).CurrentHP;
		const int32 IncomingBefore = FBattleC06AEngineFixture::GetBattler(
			*Engine,
			PlayerReserveAValue).CurrentHP;
		const FBattleResolution Damage = Engine->ExecuteCurrentMoveEffects();
		TestTrue(TEXT("The later queued attack executes"), Damage.WasAccepted());
		int32 IncomingDamageEventCount = 0;
		for (const FBattleEvent& Event : Damage.GetEvents())
		{
			if (Event.GetType() == EBattleEventType::Damage
				&& Event.GetTargets().Num() == 1
				&& Event.GetTargets()[0].BattlerId
					== MakeNumericId<FBattlerId>(PlayerReserveAValue))
			{
				++IncomingDamageEventCount;
			}
		}
		TestEqual(TEXT("The queued attack hits the incoming occupant exactly once"),
			IncomingDamageEventCount, 1);
		const FBattleEvent* DamageEvent = Damage.GetEvents().FindByPredicate(
			[](const FBattleEvent& Event)
			{
				return Event.GetType() == EBattleEventType::Damage;
			});
		TestNotNull(TEXT("Damage is emitted"), DamageEvent);
		if (DamageEvent != nullptr)
		{
			TestEqual(TEXT("Damage targets the incoming battler"),
				DamageEvent->GetTargets()[0].BattlerId,
				MakeNumericId<FBattlerId>(PlayerReserveAValue));
		}
		TestEqual(TEXT("The switched-out battler takes no queued damage"),
			FBattleC06AEngineFixture::GetBattler(*Engine, PlayerLeftValue).CurrentHP,
			OutgoingBefore);
		TestTrue(TEXT("The incoming battler takes the queued damage"),
			FBattleC06AEngineFixture::GetBattler(*Engine, PlayerReserveAValue).CurrentHP
				< IncomingBefore);
		return true;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
