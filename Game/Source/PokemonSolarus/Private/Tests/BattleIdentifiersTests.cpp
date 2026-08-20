#if WITH_DEV_AUTOMATION_TESTS

#include "Battle/BattleIdentifiers.h"
#include "Misc/AutomationTest.h"

#include <type_traits>

static_assert(!std::is_same_v<FBattleId, FTurnId>, "Battle and turn IDs must remain distinct types.");
static_assert(!std::is_same_v<FTrainerId, FBattlerId>, "Trainer and battler IDs must remain distinct types.");
static_assert(!std::is_same_v<FMoveId, FItemId>, "Move and item IDs must remain distinct types.");

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleNumericIdentifiersTest,
	"PokemonSolarus.Battle.CoreContracts.Identifiers.NumericIds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBattleNumericIdentifiersTest::RunTest(const FString& Parameters)
{
	FBattleId BattleId;
	TestFalse(TEXT("A default battle ID is invalid"), BattleId.IsValid());
	TestFalse(TEXT("Zero is rejected as a battle ID"), FBattleId::TryCreate(0, BattleId));
	TestFalse(TEXT("A rejected value leaves the battle ID invalid"), BattleId.IsValid());
	TestTrue(TEXT("A positive battle ID is accepted"), FBattleId::TryCreate(42, BattleId));
	TestTrue(TEXT("The accepted battle ID is valid"), BattleId.IsValid());
	TestEqual(TEXT("The accepted battle ID preserves its value"), BattleId.GetValue(), 42ULL);

	FBattleId SameBattleId;
	FBattleId OtherBattleId;
	TestTrue(TEXT("The same battle value can be created"), FBattleId::TryCreate(42, SameBattleId));
	TestTrue(TEXT("A different battle value can be created"), FBattleId::TryCreate(43, OtherBattleId));
	TestTrue(TEXT("Equal battle values compare equal"), BattleId == SameBattleId);
	TestTrue(TEXT("Different battle values compare unequal"), BattleId != OtherBattleId);

	FSourcePokemonId SourcePokemonId;
	TestTrue(
		TEXT("An opaque source Pokemon ID accepts a positive stable value"),
		FSourcePokemonId::TryCreate(9001, SourcePokemonId));
	TestEqual(TEXT("The opaque source Pokemon ID preserves its value"), SourcePokemonId.GetValue(), 9001ULL);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleDefinitionIdentifiersTest,
	"PokemonSolarus.Battle.CoreContracts.Identifiers.DefinitionIds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBattleDefinitionIdentifiersTest::RunTest(const FString& Parameters)
{
	FDefinitionId DefinitionId;
	TestFalse(TEXT("A default definition ID is invalid"), DefinitionId.IsValid());
	TestFalse(
		TEXT("NAME_None is rejected as a definition ID"),
		FDefinitionId::TryCreate(NAME_None, DefinitionId));
	TestFalse(TEXT("A rejected definition ID remains invalid"), DefinitionId.IsValid());

	const FName FlamethrowerName(TEXT("Move.Flamethrower"));
	TestTrue(
		TEXT("A named definition ID is accepted"),
		FDefinitionId::TryCreate(FlamethrowerName, DefinitionId));
	TestEqual(TEXT("The definition ID preserves its name"), DefinitionId.GetName(), FlamethrowerName);

	FMoveId MoveId;
	FItemId ItemId;
	TestTrue(TEXT("The generic ID can become a move ID"), FMoveId::TryCreate(DefinitionId, MoveId));
	TestTrue(TEXT("The same generic ID can become an item ID"), FItemId::TryCreate(DefinitionId, ItemId));
	TestTrue(TEXT("The typed move ID is valid"), MoveId.IsValid());
	TestTrue(
		TEXT("The typed move ID retains the generic definition"),
		MoveId.GetDefinitionId() == DefinitionId);

	FDefinitionId LaterDefinitionId;
	TestTrue(
		TEXT("A second definition ID is accepted"),
		FDefinitionId::TryCreate(FName(TEXT("Move.VineWhip")), LaterDefinitionId));
	TestTrue(
		TEXT("Definition IDs expose deterministic lexical ordering"),
		DefinitionId.LexicalLess(LaterDefinitionId));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleSlotIdentityTest,
	"PokemonSolarus.Battle.CoreContracts.Identifiers.SlotIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBattleSlotIdentityTest::RunTest(const FString& Parameters)
{
	FPartySlotId PartySlot;
	TestFalse(TEXT("A default party slot is invalid"), PartySlot.IsValid());
	TestFalse(TEXT("Party slot -1 is rejected"), FPartySlotId::TryCreate(-1, PartySlot));
	TestTrue(TEXT("Party slot 0 is accepted"), FPartySlotId::TryCreate(0, PartySlot));
	TestEqual(TEXT("Party slot 0 preserves its index"), PartySlot.GetIndex(), static_cast<uint8>(0));

	FPartySlotId LastPartySlot;
	TestTrue(TEXT("Party slot 5 is accepted"), FPartySlotId::TryCreate(5, LastPartySlot));
	TestFalse(TEXT("Party slot 6 is rejected"), FPartySlotId::TryCreate(6, LastPartySlot));
	TestFalse(TEXT("A rejected party slot resets the output"), LastPartySlot.IsValid());

	FPartySlotId PartySlotCopy = PartySlot;
	TestTrue(TEXT("Party-slot identity is value based"), PartySlotCopy == PartySlot);
	TestTrue(TEXT("Equal party slots do not require the same address"), &PartySlotCopy != &PartySlot);

	FActiveSlotId PlayerLeft;
	TestFalse(TEXT("A default active slot is invalid"), PlayerLeft.IsValid());
	TestTrue(
		TEXT("Player Left is a valid active slot"),
		FActiveSlotId::TryCreate(EBattleSide::Player, EBattlePosition::Left, PlayerLeft));

	FActiveSlotId PlayerRight;
	FActiveSlotId OpponentLeft;
	TestTrue(
		TEXT("Player Right is a valid active slot"),
		FActiveSlotId::TryCreate(EBattleSide::Player, EBattlePosition::Right, PlayerRight));
	TestTrue(
		TEXT("Opponent Left is a valid active slot"),
		FActiveSlotId::TryCreate(EBattleSide::Opponent, EBattlePosition::Left, OpponentLeft));
	TestTrue(TEXT("Left and Right positions have distinct identity"), PlayerLeft != PlayerRight);
	TestTrue(TEXT("Player and opponent sides have distinct identity"), PlayerLeft != OpponentLeft);
	TestEqual(TEXT("Player Left preserves its side"), PlayerLeft.GetSide(), EBattleSide::Player);
	TestEqual(TEXT("Player Left preserves its position"), PlayerLeft.GetPosition(), EBattlePosition::Left);

	FActiveSlotId InvalidSlot = PlayerLeft;
	TestFalse(
		TEXT("An unknown side is rejected"),
		FActiveSlotId::TryCreate(
			static_cast<EBattleSide>(255),
			EBattlePosition::Left,
			InvalidSlot));
	TestFalse(TEXT("A rejected active slot resets the output"), InvalidSlot.IsValid());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
