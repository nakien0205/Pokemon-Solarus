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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023B1SetupShapeTest,
	"PokemonSolarus.Battle.ADR0002.3B1.SetupPolicy.TypedShapesAndAtomicPublication",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBattleADR00023B1SetupShapeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FBattleSetup Setup;
	EBattleSetupValidationError Error = EBattleSetupValidationError::None;
	TestTrue(
		TEXT("A valid Partner Double setup is published"),
		FBattleSetup::TryCreate(MakeSetupInput(EBattleFormat::PartnerDouble), Setup, Error));
	TestTrue(
		TEXT("A published setup owns its successful compiled policy"),
		Setup.GetCompiledEncounterPolicies().IsValid());

	FBattleSetupInput InvalidPartner = MakeSetupInput(EBattleFormat::PartnerDouble);
	for (FBattleTrainerSetup& Trainer : InvalidPartner.Trainers)
	{
		if (Trainer.Role == EBattleTrainerRole::Partner)
		{
			Trainer.Controller = EBattleDecisionController::EnemyAI;
		}
	}
	TestFalse(
		TEXT("A Partner controlled by Enemy AI is rejected"),
		FBattleSetup::TryCreate(InvalidPartner, Setup, Error));
	TestEqual(
		TEXT("The Partner controller failure is typed"),
		Error,
		EBattleSetupValidationError::InvalidPartnerController);
	TestFalse(TEXT("A failed compile publishes no setup"), Setup.IsValid());
	TestFalse(
		TEXT("A failed compile publishes no compiled policy"),
		Setup.GetCompiledEncounterPolicies().IsValid());

	FBattleSetupInput WildWithReserve = MakeSetupInput(EBattleFormat::Single);
	WildWithReserve.EncounterKind = EBattleEncounterKind::Wild;
	WildWithReserve.PartyEntries.Add(MakePartyEntry(
		2,
		22,
		212,
		1,
		TEXT("Species.WildReserve"),
		TEXT("Move.WildReserve")));
	TestFalse(
		TEXT("An ordinary living Wild opponent reserve is rejected"),
		FBattleSetup::TryCreate(WildWithReserve, Setup, Error));
	TestEqual(
		TEXT("The ordinary Wild reserve failure is typed"),
		Error,
		EBattleSetupValidationError::InvalidWildReserve);

	FBattleSetupInput FaintedWildReserve = WildWithReserve;
	FaintedWildReserve.PartyEntries.Last().CurrentHP = 0;
	TestTrue(
		TEXT("A fainted Wild reserve does not violate the living-reserve rule"),
		FBattleSetup::TryCreate(FaintedWildReserve, Setup, Error));

	FBattleSetupInput EggWildReserve = WildWithReserve;
	EggWildReserve.PartyEntries.Last().bEgg = true;
	TestTrue(
		TEXT("An Egg Wild reserve does not violate the ordinary living-reserve rule"),
		FBattleSetup::TryCreate(EggWildReserve, Setup, Error));

	FBattleSetupInput ConfiguredReinforcement = MakeSetupInput(EBattleFormat::Double);
	ConfiguredReinforcement.EncounterKind = EBattleEncounterKind::Wild;
	ConfiguredReinforcement.PartyEntries.Add(MakePartyEntry(
		2,
		23,
		213,
		2,
		TEXT("Species.ConfiguredReinforcement"),
		TEXT("Move.ConfiguredReinforcement")));
	ConfiguredReinforcement.ConfiguredReinforcementBattlerId = MakeNumericId<FBattlerId>(23);
	TestTrue(
		TEXT("The exact frozen configured-reinforcement identity remains an accepted setup shape"),
		FBattleSetup::TryCreate(ConfiguredReinforcement, Setup, Error));
	ConfiguredReinforcement.PartyEntries.Add(MakePartyEntry(
		2,
		24,
		214,
		3,
		TEXT("Species.OrdinaryWildReserve"),
		TEXT("Move.OrdinaryWildReserve")));
	TestFalse(
		TEXT("A second ordinary living reserve is not covered by the configured identity"),
		FBattleSetup::TryCreate(ConfiguredReinforcement, Setup, Error));
	TestEqual(
		TEXT("The non-configured Wild reserve remains typed"),
		Error,
		EBattleSetupValidationError::InvalidWildReserve);

	FBattleSetupInput InvalidObedience = MakeSetupInput(EBattleFormat::Single);
	InvalidObedience.ObedienceInputs[0].BadgeCount = 9;
	TestFalse(
		TEXT("Obedience badge validation remains capped at eight"),
		FBattleSetup::TryCreate(InvalidObedience, Setup, Error));
	TestEqual(
		TEXT("Obedience overflow retains its dedicated error"),
		Error,
		EBattleSetupValidationError::InvalidObedience);
	return true;
}

}

#endif // WITH_DEV_AUTOMATION_TESTS
