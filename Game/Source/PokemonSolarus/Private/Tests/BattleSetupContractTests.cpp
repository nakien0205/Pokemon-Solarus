#if WITH_DEV_AUTOMATION_TESTS

#include "Battle/BattleSetup.h"
#include "BattleTestFactories.h"
#include "Misc/AutomationTest.h"

namespace BattleSetupContractTests
{
	using BattleTest::MakeActiveSlotId;
	using BattleTest::MakeDefinitionId;
	using BattleTest::MakeNumericId;
	using BattleTest::MakePartySlotId;

	FBattleSnapshotReference MakeReference(const TCHAR* Value)
	{
		FBattleSnapshotReference Reference;
		Reference.SnapshotId = MakeDefinitionId<FDefinitionId>(Value);
		Reference.SchemaVersion = 1;
		return Reference;
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
			: (Role == EBattleTrainerRole::Partner ? TEXT("Selector.Partner") : TEXT("Selector.Opponent")));
		return Trainer;
	}

	FBattlePartyEntrySetup MakePartyEntry(
		const uint64 TrainerValue,
		const uint64 BattlerValue,
		const uint64 SourceValue,
		const int32 PartyIndex,
		const TCHAR* SpeciesDefinitionName,
		const TCHAR* MoveName)
	{
		FBattlePartyEntrySetup Entry;
		Entry.TrainerId = MakeNumericId<FTrainerId>(TrainerValue);
		Entry.BattlerId = MakeNumericId<FBattlerId>(BattlerValue);
		Entry.SourcePokemonId = MakeNumericId<FSourcePokemonId>(SourceValue);
		Entry.PartySlotId = MakePartySlotId(PartyIndex);
		Entry.SpeciesFormId = MakeDefinitionId<FSpeciesFormId>(SpeciesDefinitionName);
		Entry.Level = 50;
		Entry.Stats = {200, 100, 100, 100, 100, 100};
		Entry.CurrentHP = 200;
		Entry.AbilityId = MakeDefinitionId<FAbilityId>(TEXT("Ability.Test"));

		FBattleMoveSlotSetup Move;
		Move.SlotIndex = 0;
		Move.MoveId = MakeDefinitionId<FMoveId>(MoveName);
		Move.CurrentPP = 10;
		Move.MaxPP = 10;
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
		Input.BattleId = MakeNumericId<FBattleId>(100);
		Input.SettingsReference = MakeReference(TEXT("Settings.Casual"));
		Input.CatalogReference = MakeReference(TEXT("Catalog.CoreProof"));
		Input.EncounterKind = EBattleEncounterKind::Trainer;
		Input.Format = Format;
		Input.CaptureCapacity.PartySlotsRemaining = 4;
		Input.CaptureCapacity.StorageSlotsRemaining = 100;
		Input.Policies.WildFleeMode = EBattleWildFleeMode::Disabled;

		Input.Trainers.Add(MakeTrainer(1, EBattleSide::Player, EBattleTrainerRole::Player, EBattleDecisionController::Human));
		Input.Trainers.Add(MakeTrainer(2, EBattleSide::Opponent, EBattleTrainerRole::Opponent, EBattleDecisionController::EnemyAI));
		Input.PartyEntries.Add(MakePartyEntry(1, 11, 111, 0, TEXT("Species.Charizard"), TEXT("Move.Flamethrower")));
		Input.PartyEntries.Add(MakePartyEntry(2, 21, 211, 0, TEXT("Species.Venusaur"), TEXT("Move.VineWhip")));
		Input.StartingActive.Add(MakeActive(EBattleSide::Player, EBattlePosition::Left, 1, 11));
		Input.StartingActive.Add(MakeActive(EBattleSide::Opponent, EBattlePosition::Left, 2, 21));

		if (Format == EBattleFormat::Double)
		{
			Input.PartyEntries.Add(MakePartyEntry(1, 12, 112, 1, TEXT("Species.Blastoise"), TEXT("Move.WaterGun")));
			Input.PartyEntries.Add(MakePartyEntry(2, 22, 212, 1, TEXT("Species.Torterra"), TEXT("Move.RazorLeaf")));
			Input.StartingActive.Add(MakeActive(EBattleSide::Player, EBattlePosition::Right, 1, 12));
			Input.StartingActive.Add(MakeActive(EBattleSide::Opponent, EBattlePosition::Right, 2, 22));
		}
		else if (Format == EBattleFormat::PartnerDouble)
		{
			Input.Trainers.Add(MakeTrainer(3, EBattleSide::Player, EBattleTrainerRole::Partner, EBattleDecisionController::PartnerAI));
			Input.PartyEntries.Add(MakePartyEntry(3, 31, 311, 0, TEXT("Species.Blastoise"), TEXT("Move.WaterGun")));
			Input.PartyEntries.Add(MakePartyEntry(2, 22, 212, 1, TEXT("Species.Torterra"), TEXT("Move.RazorLeaf")));
			Input.StartingActive.Add(MakeActive(EBattleSide::Player, EBattlePosition::Right, 3, 31));
			Input.StartingActive.Add(MakeActive(EBattleSide::Opponent, EBattlePosition::Right, 2, 22));
		}

		FBattleObedienceInput Obedience;
		Obedience.BattlerId = MakeNumericId<FBattlerId>(11);
		Obedience.bSubjectToPlayerCap = true;
		Obedience.ReferenceLevel = 20;
		Obedience.BadgeCount = 0;
		Input.ObedienceInputs.Add(Obedience);
		return Input;
	}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC01BSingleSetupTest,
	"PokemonSolarus.Battle.C01B.Setup.Single",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBattleC01BSingleSetupTest::RunTest(const FString& Parameters)
{
	FBattleSetup Setup;
	EBattleSetupValidationError Error = EBattleSetupValidationError::None;
	TestTrue(TEXT("A valid Single setup is accepted"), FBattleSetup::TryCreate(MakeSetupInput(EBattleFormat::Single), Setup, Error));
	TestTrue(TEXT("The accepted Single setup is valid"), Setup.IsValid());
	TestEqual(TEXT("Single has two starting active assignments"), Setup.GetStartingActive().Num(), 2);

	FBattleSetupInput Invalid = MakeSetupInput(EBattleFormat::Single);
	Invalid.StartingActive.Add(MakeActive(EBattleSide::Player, EBattlePosition::Right, 1, 11));
	TestFalse(TEXT("Single rejects a right active slot"), FBattleSetup::TryCreate(Invalid, Setup, Error));
	TestEqual(TEXT("The shape error is typed"), Error, EBattleSetupValidationError::ActiveSlotShape);
	TestFalse(TEXT("A rejected setup resets the output"), Setup.IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC01BDoubleSetupTest,
	"PokemonSolarus.Battle.C01B.Setup.Double",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBattleC01BDoubleSetupTest::RunTest(const FString& Parameters)
{
	FBattleSetup Setup;
	EBattleSetupValidationError Error = EBattleSetupValidationError::None;
	TestTrue(TEXT("A valid Double setup is accepted"), FBattleSetup::TryCreate(MakeSetupInput(EBattleFormat::Double), Setup, Error));
	TestEqual(TEXT("Double has four starting active assignments"), Setup.GetStartingActive().Num(), 4);

	FBattleSetupInput Invalid = MakeSetupInput(EBattleFormat::Double);
	Invalid.StartingActive.Pop();
	TestFalse(TEXT("Double rejects a missing active slot"), FBattleSetup::TryCreate(Invalid, Setup, Error));
	TestEqual(TEXT("The missing slot reports the shape error"), Error, EBattleSetupValidationError::ActiveSlotShape);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC01BPartnerDoubleSetupTest,
	"PokemonSolarus.Battle.C01B.Setup.PartnerDouble",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBattleC01BPartnerDoubleSetupTest::RunTest(const FString& Parameters)
{
	FBattleSetup Setup;
	EBattleSetupValidationError Error = EBattleSetupValidationError::None;
	TestTrue(
		TEXT("A valid Partner Double setup is accepted"),
		FBattleSetup::TryCreate(MakeSetupInput(EBattleFormat::PartnerDouble), Setup, Error));
	TestEqual(TEXT("Partner Double has three Trainers"), Setup.GetTrainers().Num(), 3);

	FBattleSetupInput Invalid = MakeSetupInput(EBattleFormat::PartnerDouble);
	for (FBattleActiveAssignment& Assignment : Invalid.StartingActive)
	{
		if (Assignment.ActiveSlotId.GetSide() == EBattleSide::Player
			&& Assignment.ActiveSlotId.GetPosition() == EBattlePosition::Right)
		{
			Assignment.TrainerId = MakeNumericId<FTrainerId>(1);
		}
	}
	TestFalse(
		TEXT("Partner Double rejects the player Trainer owning the partner slot"),
		FBattleSetup::TryCreate(Invalid, Setup, Error));
	TestEqual(TEXT("The ownership error is typed"), Error, EBattleSetupValidationError::TrainerOwnership);
	return true;
}

}

#endif // WITH_DEV_AUTOMATION_TESTS
