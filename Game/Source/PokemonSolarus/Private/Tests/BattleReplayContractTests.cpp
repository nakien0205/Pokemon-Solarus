#if WITH_DEV_AUTOMATION_TESTS

#include "Battle/BattleEngine.h"
#include "Misc/AutomationTest.h"

namespace
{
	template <typename IdType>
	IdType MakeReplayNumericId(const uint64 Value)
	{
		IdType Id;
		check(IdType::TryCreate(Value, Id));
		return Id;
	}

	template <typename IdType>
	IdType MakeReplayDefinitionId(const TCHAR* Value)
	{
		IdType Id;
		check(IdType::TryCreate(FName(Value), Id));
		return Id;
	}

	FBattleSetupInput MakeReplayInput(const bool bReverseInsertion)
	{
		FBattleSetupInput Input;
		Input.BattleId = MakeReplayNumericId<FBattleId>(900);
		Input.SettingsReference = {MakeReplayDefinitionId<FDefinitionId>(TEXT("Settings.Casual")), 1};
		Input.CatalogReference = {MakeReplayDefinitionId<FDefinitionId>(TEXT("Catalog.Replay")), 3};
		Input.EncounterKind = EBattleEncounterKind::Trainer;
		Input.Format = EBattleFormat::Single;
		Input.CaptureCapacity = {4, 50};
		Input.Policies.WildFleeMode = EBattleWildFleeMode::Disabled;

		FBattleTrainerSetup Player;
		Player.TrainerId = MakeReplayNumericId<FTrainerId>(1);
		Player.Side = EBattleSide::Player;
		Player.Role = EBattleTrainerRole::Player;
		Player.Controller = EBattleDecisionController::Human;
		Player.SelectorProfileId = MakeReplayDefinitionId<FDefinitionId>(TEXT("Selector.Player"));

		FBattleTrainerSetup Opponent;
		Opponent.TrainerId = MakeReplayNumericId<FTrainerId>(2);
		Opponent.Side = EBattleSide::Opponent;
		Opponent.Role = EBattleTrainerRole::Opponent;
		Opponent.Controller = EBattleDecisionController::EnemyAI;
		Opponent.SelectorProfileId = MakeReplayDefinitionId<FDefinitionId>(TEXT("Selector.Opponent"));

		FBattlePartyEntrySetup PlayerParty;
		PlayerParty.TrainerId = Player.TrainerId;
		PlayerParty.BattlerId = MakeReplayNumericId<FBattlerId>(11);
		PlayerParty.SourcePokemonId = MakeReplayNumericId<FSourcePokemonId>(111);
		check(FPartySlotId::TryCreate(0, PlayerParty.PartySlotId));
		PlayerParty.SpeciesFormId = MakeReplayDefinitionId<FSpeciesFormId>(TEXT("Species.Charizard"));
		PlayerParty.Level = 50;
		PlayerParty.Stats = {200, 100, 100, 100, 100, 100};
		PlayerParty.CurrentHP = 200;
		PlayerParty.AbilityId = MakeReplayDefinitionId<FAbilityId>(TEXT("Ability.Blaze"));

		FBattlePartyEntrySetup OpponentParty = PlayerParty;
		OpponentParty.TrainerId = Opponent.TrainerId;
		OpponentParty.BattlerId = MakeReplayNumericId<FBattlerId>(21);
		OpponentParty.SourcePokemonId = MakeReplayNumericId<FSourcePokemonId>(211);
		OpponentParty.SpeciesFormId = MakeReplayDefinitionId<FSpeciesFormId>(TEXT("Species.Venusaur"));
		OpponentParty.AbilityId = MakeReplayDefinitionId<FAbilityId>(TEXT("Ability.Overgrow"));

		FBattleActiveAssignment PlayerActive;
		check(FActiveSlotId::TryCreate(EBattleSide::Player, EBattlePosition::Left, PlayerActive.ActiveSlotId));
		PlayerActive.TrainerId = Player.TrainerId;
		PlayerActive.BattlerId = PlayerParty.BattlerId;

		FBattleActiveAssignment OpponentActive;
		check(FActiveSlotId::TryCreate(EBattleSide::Opponent, EBattlePosition::Left, OpponentActive.ActiveSlotId));
		OpponentActive.TrainerId = Opponent.TrainerId;
		OpponentActive.BattlerId = OpponentParty.BattlerId;

		if (bReverseInsertion)
		{
			Input.Trainers = {Opponent, Player};
			Input.PartyEntries = {OpponentParty, PlayerParty};
			Input.StartingActive = {OpponentActive, PlayerActive};
		}
		else
		{
			Input.Trainers = {Player, Opponent};
			Input.PartyEntries = {PlayerParty, OpponentParty};
			Input.StartingActive = {PlayerActive, OpponentActive};
		}
		return Input;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC01BCanonicalReplayTest,
	"PokemonSolarus.Battle.C01B.Replay.CanonicalSerialization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBattleC01BCanonicalReplayTest::RunTest(const FString& Parameters)
{
	FBattleSetup FirstSetup;
	FBattleSetup SecondSetup;
	EBattleSetupValidationError SetupError;
	TestTrue(TEXT("The first semantic setup is accepted"), FBattleSetup::TryCreate(MakeReplayInput(false), FirstSetup, SetupError));
	TestTrue(TEXT("The reverse-inserted semantic setup is accepted"), FBattleSetup::TryCreate(MakeReplayInput(true), SecondSetup, SetupError));

	TUniquePtr<FBattleEngine> FirstEngine;
	TUniquePtr<FBattleEngine> SecondEngine;
	FBattleRejection Rejection;
	TestTrue(TEXT("The first replay engine is created"), FBattleEngine::TryCreate(FirstSetup, MakeUnique<FSeededBattleRandom>(5), FirstEngine, Rejection));
	TestTrue(TEXT("The second replay engine is created"), FBattleEngine::TryCreate(SecondSetup, MakeUnique<FSeededBattleRandom>(5), SecondEngine, Rejection));

	TArray<uint8> FirstBytes;
	TArray<uint8> SecondBytes;
	TestTrue(
		TEXT("The first replay record serializes canonically"),
		FBattleReplaySerializer::TrySerializeCanonical(FirstEngine->ExportReplayRecord(), FirstBytes, Rejection));
	TestTrue(
		TEXT("The second replay record serializes canonically"),
		FBattleReplaySerializer::TrySerializeCanonical(SecondEngine->ExportReplayRecord(), SecondBytes, Rejection));
	TestTrue(TEXT("Semantic equality produces byte-identical replay data"), FirstBytes == SecondBytes);
	TestTrue(TEXT("The canonical replay is not empty"), FirstBytes.Num() > 16);
	TestEqual(TEXT("The replay starts with P"), FirstBytes[0], static_cast<uint8>('P'));
	TestEqual(TEXT("The replay starts with S"), FirstBytes[1], static_cast<uint8>('S'));
	TestEqual(TEXT("The replay starts with B"), FirstBytes[2], static_cast<uint8>('B'));
	TestEqual(TEXT("The replay starts with R"), FirstBytes[3], static_cast<uint8>('R'));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
