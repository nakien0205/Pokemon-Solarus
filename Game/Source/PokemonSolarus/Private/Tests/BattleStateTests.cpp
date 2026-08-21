#if WITH_DEV_AUTOMATION_TESTS

#include "Battle/BattleEngine.h"
#include "Battle/BattleState.h"
#include "BattleTestFactories.h"
#include "Misc/AutomationTest.h"

namespace
{
	using BattleTest::MakeActiveSlotId;
	using BattleTest::MakeDefinitionId;
	using BattleTest::MakeNumericId;
	using BattleTest::MakePartySlotId;

	constexpr uint64 PlayerTrainerValue = 1;
	constexpr uint64 OpponentTrainerValue = 2;
	constexpr uint64 PartnerTrainerValue = 3;

	TArray<FBattleTypeChartEntry> MakeCompleteNeutralChart()
	{
		TArray<FBattleTypeChartEntry> Entries;
		Entries.Reserve(FBattleTypeChart::EntryCount);
		for (int32 AttackingIndex = 0; AttackingIndex < FBattleTypeChart::TypeCount; ++AttackingIndex)
		{
			for (int32 DefendingIndex = 0; DefendingIndex < FBattleTypeChart::TypeCount; ++DefendingIndex)
			{
				Entries.Add(
					{
						static_cast<EPokemonType>(AttackingIndex),
						static_cast<EPokemonType>(DefendingIndex),
						1,
						1
					});
			}
		}
		return Entries;
	}

	FBattleMoveDefinition MakeMove(const TCHAR* Name)
	{
		FBattleMoveDefinition Move;
		Move.Id = MakeDefinitionId<FMoveId>(Name);
		Move.Type = EPokemonType::Normal;
		Move.Category = EBattleMoveCategory::Physical;
		Move.Power = 40;
		Move.Accuracy = 100;
		Move.BasePP = 35;
		Move.TargetClass = EBattleTargetClass::SelectedOpponent;

		FBattleMoveEffectDescriptor Damage;
		Damage.Order = 0;
		Damage.Kind = EBattleMoveEffectKind::Damage;
		Damage.Target = EBattleEffectTarget::ResolvedTarget;
		Move.Effects.Add(Damage);
		return Move;
	}

	FBattleSpeciesFormDefinition MakeSpecies(
		const TCHAR* Name,
		const FAbilityId AbilityId)
	{
		FBattleSpeciesFormDefinition Species;
		Species.Id = MakeDefinitionId<FSpeciesFormId>(Name);
		Species.PrimaryType = EPokemonType::Normal;
		Species.BaseStats = {80, 80, 80, 80, 80, 80};
		Species.CatchRate = 45;
		Species.AbilityChoices.Add(AbilityId);
		return Species;
	}

	FBattleDefinitionCatalogInput MakeCatalogInput()
	{
		FBattleDefinitionCatalogInput Input;
		Input.TypeChartEntries = MakeCompleteNeutralChart();

		const FAbilityId AbilityId = MakeDefinitionId<FAbilityId>(TEXT("Ability.C03A"));
		Input.Abilities.Add({AbilityId});
		Input.Items.Add(
			{MakeDefinitionId<FItemId>(TEXT("Item.C03A.Potion")), EBattleItemKind::Battle});
		Input.Items.Add(
			{MakeDefinitionId<FItemId>(TEXT("Item.C03A.Leftovers")), EBattleItemKind::Held});
		Input.Moves.Add(MakeMove(TEXT("Move.C03A")));

		Input.SpeciesForms.Add(MakeSpecies(TEXT("Species.PlayerLeft"), AbilityId));
		Input.SpeciesForms.Add(MakeSpecies(TEXT("Species.PlayerRight"), AbilityId));
		Input.SpeciesForms.Add(MakeSpecies(TEXT("Species.Partner"), AbilityId));
		Input.SpeciesForms.Add(MakeSpecies(TEXT("Species.OpponentLeft"), AbilityId));
		Input.SpeciesForms.Add(MakeSpecies(TEXT("Species.OpponentRight"), AbilityId));
		return Input;
	}

	FBattleDefinitionCatalog MakeCatalog()
	{
		FBattleDefinitionCatalog Catalog;
		TArray<FBattleCatalogDiagnostic> Diagnostics;
		check(FBattleDefinitionCatalog::TryCreate(MakeCatalogInput(), Catalog, Diagnostics));
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
			Role == EBattleTrainerRole::Player ? TEXT("Selector.Player")
			: (Role == EBattleTrainerRole::Partner ? TEXT("Selector.Partner") : TEXT("Selector.Enemy")));
		if (Role == EBattleTrainerRole::Player)
		{
			Trainer.Bag.Add(
				{MakeDefinitionId<FItemId>(TEXT("Item.C03A.Potion")), 2});
		}
		return Trainer;
	}

	FBattlePartyEntrySetup MakePartyEntry(
		const uint64 TrainerValue,
		const uint64 BattlerValue,
		const uint64 SourceValue,
		const int32 PartyIndex,
		const TCHAR* SpeciesName,
		const bool bWithHeldItem = false)
	{
		FBattlePartyEntrySetup Entry;
		Entry.TrainerId = MakeNumericId<FTrainerId>(TrainerValue);
		Entry.BattlerId = MakeNumericId<FBattlerId>(BattlerValue);
		Entry.SourcePokemonId = MakeNumericId<FSourcePokemonId>(SourceValue);
		Entry.PartySlotId = MakePartySlotId(PartyIndex);
		Entry.SpeciesFormId = MakeDefinitionId<FSpeciesFormId>(SpeciesName);
		Entry.Level = 50;
		Entry.Stats = {200, 100, 100, 100, 100, 100};
		Entry.CurrentHP = 200;
		Entry.AbilityId = MakeDefinitionId<FAbilityId>(TEXT("Ability.C03A"));
		if (bWithHeldItem)
		{
			Entry.OriginalHeldItemId = MakeDefinitionId<FItemId>(TEXT("Item.C03A.Leftovers"));
			Entry.CurrentHeldItemId = Entry.OriginalHeldItemId;
		}

		FBattleMoveSlotSetup Move;
		Move.SlotIndex = 0;
		Move.MoveId = MakeDefinitionId<FMoveId>(TEXT("Move.C03A"));
		Move.CurrentPP = 35;
		Move.MaxPP = 35;
		Entry.Moves.Add(Move);
		return Entry;
	}

	FBattleActiveAssignment MakeActive(
		const EBattleSide Side,
		const EBattlePosition Position,
		const uint64 TrainerValue,
		const uint64 BattlerValue)
	{
		FBattleActiveAssignment Assignment;
		Assignment.ActiveSlotId = MakeActiveSlotId(Side, Position);
		Assignment.TrainerId = MakeNumericId<FTrainerId>(TrainerValue);
		Assignment.BattlerId = MakeNumericId<FBattlerId>(BattlerValue);
		return Assignment;
	}

	FBattleSetupInput MakeSetupInput(const EBattleFormat Format)
	{
		FBattleSetupInput Input;
		Input.BattleId = MakeNumericId<FBattleId>(300);
		Input.SettingsReference =
			{MakeDefinitionId<FDefinitionId>(TEXT("Settings.C03A")), 1};
		Input.CatalogReference =
			{MakeDefinitionId<FDefinitionId>(TEXT("Catalog.C03A")), 1};
		Input.EncounterKind = EBattleEncounterKind::Trainer;
		Input.Format = Format;
		Input.CaptureCapacity = {4, 100};
		Input.Policies.WildFleeMode = EBattleWildFleeMode::Disabled;

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
			11,
			111,
			0,
			TEXT("Species.PlayerLeft"),
			true));
		Input.PartyEntries.Add(MakePartyEntry(
			OpponentTrainerValue,
			21,
			211,
			0,
			TEXT("Species.OpponentLeft")));
		Input.StartingActive.Add(MakeActive(
			EBattleSide::Player,
			EBattlePosition::Left,
			PlayerTrainerValue,
			11));
		Input.StartingActive.Add(MakeActive(
			EBattleSide::Opponent,
			EBattlePosition::Left,
			OpponentTrainerValue,
			21));

		if (Format == EBattleFormat::Double)
		{
			Input.PartyEntries.Add(MakePartyEntry(
				PlayerTrainerValue,
				12,
				112,
				1,
				TEXT("Species.PlayerRight")));
			Input.PartyEntries.Add(MakePartyEntry(
				OpponentTrainerValue,
				22,
				212,
				1,
				TEXT("Species.OpponentRight")));
			Input.StartingActive.Add(MakeActive(
				EBattleSide::Player,
				EBattlePosition::Right,
				PlayerTrainerValue,
				12));
			Input.StartingActive.Add(MakeActive(
				EBattleSide::Opponent,
				EBattlePosition::Right,
				OpponentTrainerValue,
				22));
		}
		else if (Format == EBattleFormat::PartnerDouble)
		{
			Input.Trainers.Add(MakeTrainer(
				PartnerTrainerValue,
				EBattleSide::Player,
				EBattleTrainerRole::Partner,
				EBattleDecisionController::PartnerAI));
			Input.PartyEntries.Add(MakePartyEntry(
				PartnerTrainerValue,
				31,
				311,
				0,
				TEXT("Species.Partner")));
			Input.PartyEntries.Add(MakePartyEntry(
				OpponentTrainerValue,
				22,
				212,
				1,
				TEXT("Species.OpponentRight")));
			Input.StartingActive.Add(MakeActive(
				EBattleSide::Player,
				EBattlePosition::Right,
				PartnerTrainerValue,
				31));
			Input.StartingActive.Add(MakeActive(
				EBattleSide::Opponent,
				EBattlePosition::Right,
				OpponentTrainerValue,
				22));
		}

		for (const FBattlePartyEntrySetup& Entry : Input.PartyEntries)
		{
			if (Entry.TrainerId == MakeNumericId<FTrainerId>(PlayerTrainerValue))
			{
				Input.ObedienceInputs.Add({Entry.BattlerId, true, 20, 0});
			}
		}
		return Input;
	}

	FBattleSetup MakeSetup(const EBattleFormat Format)
	{
		FBattleSetup Setup;
		EBattleSetupValidationError Error = EBattleSetupValidationError::None;
		check(FBattleSetup::TryCreate(MakeSetupInput(Format), Setup, Error));
		return Setup;
	}

	TUniquePtr<FBattleEngine> MakeEngine(const EBattleFormat Format)
	{
		TUniquePtr<FBattleEngine> Engine;
		FBattleRejection Rejection;
		check(FBattleEngine::TryCreate(
			MakeSetup(Format),
			MakeCatalog(),
			MakeUnique<FSeededBattleRandom>(77),
			Engine,
			Rejection));
		return Engine;
	}

	const FBattleActivePositionState* FindPosition(
		const FBattleEngineState& State,
		const EBattleSide Side,
		const EBattlePosition Position)
	{
		return State.FindActivePosition(MakeActiveSlotId(Side, Position));
	}
}

class FBattleStateTestFixture
{
public:
	static const FBattleEngineState& GetState(const FBattleEngine& Engine)
	{
		check(Engine.State.IsValid());
		return *Engine.State;
	}

	static FBattleEngineState& GetMutableState(FBattleEngine& Engine)
	{
		check(Engine.State.IsValid());
		return *Engine.State;
	}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC03ASingleStateTest,
	"PokemonSolarus.Battle.C03A.State.Single",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBattleC03ASingleStateTest::RunTest(const FString& Parameters)
{
	const TUniquePtr<FBattleEngine> Engine = MakeEngine(EBattleFormat::Single);
	const FBattleEngineState& State = FBattleStateTestFixture::GetState(*Engine);

	TestEqual(TEXT("Single stores two Trainers"), State.GetTrainers().Num(), 2);
	TestEqual(TEXT("Single stores two battlers"), State.GetBattlers().Num(), 2);
	TestEqual(TEXT("Every Trainer has six structural party slots"), State.GetTrainers()[0].PartySlots.Num(), FPartySlotId::PartySize);
	TestEqual(TEXT("The state always stores four structural active positions"), State.GetActivePositions().Num(), 4);
	TestTrue(TEXT("Player Left is available"), FindPosition(State, EBattleSide::Player, EBattlePosition::Left)->bAvailable);
	TestFalse(TEXT("Player Right is structurally unavailable"), FindPosition(State, EBattleSide::Player, EBattlePosition::Right)->bAvailable);
	TestFalse(TEXT("Unavailable Player Right is empty"), FindPosition(State, EBattleSide::Player, EBattlePosition::Right)->BattlerId.IsValid());
	TestEqual(TEXT("The current turn starts at one"), State.GetTurnId().GetValue(), 1ULL);
	TestEqual(TEXT("The initial phase is Setup"), State.GetPhase(), EBattlePhase::Setup);
	TestEqual(TEXT("The initial outcome is in progress"), State.GetOutcome(), EBattleOutcome::InProgress);
	TestEqual(TEXT("Escape attempt count starts at one"), State.GetEscapeAttemptCount(), 1U);
	TestEqual(TEXT("There are no pending captures"), State.GetPendingCaptures().Num(), 0);
	TestFalse(TEXT("No reinforcement has succeeded"), State.HasSuccessfulReinforcement());
	TestTrue(TEXT("The frozen catalog is retained"), State.HasCatalog());

	EBattleStateValidationError Error = EBattleStateValidationError::None;
	TestTrue(TEXT("The constructed Single state satisfies every invariant"), State.ValidateInvariants(Error));
	TestEqual(TEXT("A valid state reports no invariant error"), Error, EBattleStateValidationError::None);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC03ADoubleStateTest,
	"PokemonSolarus.Battle.C03A.State.Double",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBattleC03ADoubleStateTest::RunTest(const FString& Parameters)
{
	const TUniquePtr<FBattleEngine> Engine = MakeEngine(EBattleFormat::Double);
	const FBattleEngineState& State = FBattleStateTestFixture::GetState(*Engine);
	const FBattleTrainerState* Player = State.FindTrainer(MakeNumericId<FTrainerId>(PlayerTrainerValue));
	const FBattleTrainerState* Opponent = State.FindTrainer(MakeNumericId<FTrainerId>(OpponentTrainerValue));

	TestEqual(TEXT("Double stores four battlers"), State.GetBattlers().Num(), 4);
	TestTrue(TEXT("Player Right is available"), FindPosition(State, EBattleSide::Player, EBattlePosition::Right)->bAvailable);
	TestTrue(TEXT("Opponent Right is available"), FindPosition(State, EBattleSide::Opponent, EBattlePosition::Right)->bAvailable);
	TestTrue(TEXT("Player Right is occupied"), FindPosition(State, EBattleSide::Player, EBattlePosition::Right)->BattlerId.IsValid());
	TestNotNull(TEXT("The player Trainer exists"), Player);
	TestNotNull(TEXT("The opponent Trainer exists"), Opponent);
	if (Player != nullptr && Opponent != nullptr)
	{
		TestEqual(TEXT("The player receives two action allowances"), Player->ActionAllowance.MaximumActions, 2);
		TestEqual(TEXT("The opponent receives two action allowances"), Opponent->ActionAllowance.MaximumActions, 2);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC03APartnerDoubleStateTest,
	"PokemonSolarus.Battle.C03A.State.PartnerDouble",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBattleC03APartnerDoubleStateTest::RunTest(const FString& Parameters)
{
	const TUniquePtr<FBattleEngine> Engine = MakeEngine(EBattleFormat::PartnerDouble);
	const FBattleEngineState& State = FBattleStateTestFixture::GetState(*Engine);
	const FBattleTrainerState* Player = State.FindTrainer(MakeNumericId<FTrainerId>(PlayerTrainerValue));
	const FBattleTrainerState* Partner = State.FindTrainer(MakeNumericId<FTrainerId>(PartnerTrainerValue));
	const FBattleActivePositionState* PlayerRight = FindPosition(State, EBattleSide::Player, EBattlePosition::Right);

	TestEqual(TEXT("Partner Double stores three separate Trainers"), State.GetTrainers().Num(), 3);
	TestNotNull(TEXT("The player Trainer exists"), Player);
	TestNotNull(TEXT("The partner Trainer exists"), Partner);
	TestNotNull(TEXT("Player Right exists structurally"), PlayerRight);
	if (Player != nullptr && Partner != nullptr && PlayerRight != nullptr)
	{
		TestEqual(TEXT("The player receives one action allowance"), Player->ActionAllowance.MaximumActions, 1);
		TestEqual(TEXT("The partner receives its own action allowance"), Partner->ActionAllowance.MaximumActions, 1);
		TestTrue(TEXT("Player Right remains owned by the partner Trainer"), PlayerRight->TrainerId == Partner->TrainerId);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC03ASetupRejectionTest,
	"PokemonSolarus.Battle.C03A.Validation.SetupAtomicity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBattleC03ASetupRejectionTest::RunTest(const FString& Parameters)
{
	FBattleSetup Setup = MakeSetup(EBattleFormat::Single);
	EBattleSetupValidationError Error = EBattleSetupValidationError::None;

	FBattleSetupInput Duplicate = MakeSetupInput(EBattleFormat::Single);
	const FBattlePartyEntrySetup DuplicateEntry = Duplicate.PartyEntries[0];
	Duplicate.PartyEntries.Add(DuplicateEntry);
	TestFalse(TEXT("A duplicate battler is rejected"), FBattleSetup::TryCreate(Duplicate, Setup, Error));
	TestEqual(TEXT("Duplicate identity has a typed error"), Error, EBattleSetupValidationError::DuplicateIdentity);
	TestFalse(TEXT("Rejected construction resets the output atomically"), Setup.IsValid());

	FBattleSetupInput Oversized = MakeSetupInput(EBattleFormat::Single);
	for (int32 PartyIndex = 1; PartyIndex < 7; ++PartyIndex)
	{
		Oversized.PartyEntries.Add(MakePartyEntry(
			PlayerTrainerValue,
			static_cast<uint64>(11 + PartyIndex),
			static_cast<uint64>(111 + PartyIndex),
			PartyIndex < FPartySlotId::PartySize ? PartyIndex : 5,
			TEXT("Species.PlayerRight")));
	}
	TestFalse(TEXT("More than six entries for one Trainer are rejected"), FBattleSetup::TryCreate(Oversized, Setup, Error));
	TestTrue(
		TEXT("Oversized input reports a typed identity or party-shape error"),
		Error == EBattleSetupValidationError::DuplicateIdentity || Error == EBattleSetupValidationError::PartyShape);

	FBattleSetupInput TooManyActive = MakeSetupInput(EBattleFormat::Single);
	TooManyActive.StartingActive.Add(MakeActive(
		EBattleSide::Player,
		EBattlePosition::Right,
		PlayerTrainerValue,
		11));
	TestFalse(TEXT("Single rejects more than one active per side"), FBattleSetup::TryCreate(TooManyActive, Setup, Error));
	TestEqual(TEXT("The active-count error is typed"), Error, EBattleSetupValidationError::ActiveSlotShape);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC03AInvariantTest,
	"PokemonSolarus.Battle.C03A.Validation.RuntimeInvariants",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBattleC03AInvariantTest::RunTest(const FString& Parameters)
{
	TUniquePtr<FBattleEngine> Engine = MakeEngine(EBattleFormat::Single);
	FBattleEngineState& State = FBattleStateTestFixture::GetMutableState(*Engine);
	EBattleStateValidationError Error = EBattleStateValidationError::None;
	FBattleBattlerState* Battler = State.FindMutableBattler(MakeNumericId<FBattlerId>(11));
	FBattleTrainerState* Trainer = State.FindMutableTrainer(MakeNumericId<FTrainerId>(PlayerTrainerValue));
	check(Battler != nullptr && Trainer != nullptr);

	const int32 OriginalHP = Battler->CurrentHP;
	Battler->CurrentHP = Battler->PermanentStats.MaxHP + 1;
	TestFalse(TEXT("HP above Max HP violates state invariants"), State.ValidateInvariants(Error));
	TestEqual(TEXT("HP violations are typed"), Error, EBattleStateValidationError::InvalidHP);
	Battler->CurrentHP = OriginalHP;

	const int32 OriginalPP = Battler->Moves[0].CurrentPP;
	Battler->Moves[0].CurrentPP = Battler->Moves[0].MaxPP + 1;
	TestFalse(TEXT("PP above Max PP violates state invariants"), State.ValidateInvariants(Error));
	TestEqual(TEXT("PP violations are typed"), Error, EBattleStateValidationError::InvalidPP);
	Battler->Moves[0].CurrentPP = OriginalPP;

	const int32 OriginalItemCount = Trainer->Bag[0].Count;
	Trainer->Bag[0].Count = -1;
	TestFalse(TEXT("A negative Trainer resource violates state invariants"), State.ValidateInvariants(Error));
	TestEqual(TEXT("Resource violations are typed"), Error, EBattleStateValidationError::InvalidResource);
	Trainer->Bag[0].Count = OriginalItemCount;

	State.Outcome = EBattleOutcome::Victory;
	TestFalse(TEXT("A terminal outcome cannot coexist with Setup phase"), State.ValidateInvariants(Error));
	TestEqual(TEXT("Lifecycle violations are typed"), Error, EBattleStateValidationError::InvalidLifecycle);
	State.Outcome = EBattleOutcome::InProgress;

	Battler->Stages.ApplyChange(EBattleStat::Attack, 100);
	int32 AttackStage = 0;
	TestTrue(TEXT("The Attack stage remains readable"), Battler->Stages.TryGetStage(EBattleStat::Attack, AttackStage));
	TestEqual(TEXT("Stage mutation clamps at +6"), AttackStage, FBattleStatStages::MaximumStage);
	TestTrue(TEXT("Clamped stages retain every state invariant"), State.ValidateInvariants(Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC03ACatalogRejectionTest,
	"PokemonSolarus.Battle.C03A.Validation.CatalogReferences",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBattleC03ACatalogRejectionTest::RunTest(const FString& Parameters)
{
	FBattleDefinitionCatalogInput CatalogInput = MakeCatalogInput();
	CatalogInput.Moves.Reset();
	FBattleDefinitionCatalog Catalog;
	TArray<FBattleCatalogDiagnostic> Diagnostics;
	check(FBattleDefinitionCatalog::TryCreate(CatalogInput, Catalog, Diagnostics));

	TUniquePtr<FBattleEngineState> State;
	EBattleStateValidationError StateError = EBattleStateValidationError::None;
	TestFalse(
		TEXT("A setup move missing from the frozen catalog is rejected"),
		FBattleEngineState::TryCreate(
			MakeSetup(EBattleFormat::Single),
			&Catalog,
			MakeUnique<FSeededBattleRandom>(9),
			State,
			StateError));
	TestEqual(TEXT("Missing catalog references are typed"), StateError, EBattleStateValidationError::MissingCatalogReference);
	TestFalse(TEXT("Failed state construction is atomic"), State.IsValid());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
