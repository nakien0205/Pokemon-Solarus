#if WITH_DEV_AUTOMATION_TESTS

#include "Battle/BattleRandom.h"
#include "Misc/AutomationTest.h"

namespace
{
	FBattleRandomContext MakeRandomContext()
	{
		FBattleRandomContext Context;
		const bool bBattleIdCreated = FBattleId::TryCreate(10, Context.BattleId);
		const bool bTurnIdCreated = FTurnId::TryCreate(2, Context.TurnId);
		const bool bActionIdCreated = FActionId::TryCreate(7, Context.ActionId);
		const bool bResolutionIdCreated = FResolutionId::TryCreate(9, Context.ResolutionId);
		const bool bPurposeCreated = FDefinitionId::TryCreate(FName(TEXT("RNG.Test")), Context.RulePurpose);
		check(
			bBattleIdCreated
			&& bTurnIdCreated
			&& bActionIdCreated
			&& bResolutionIdCreated
			&& bPurposeCreated);
		return Context;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleRandomBoundsAndTraceTest,
	"PokemonSolarus.Battle.CoreContracts.Random.BoundsAndTrace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBattleRandomBoundsAndTraceTest::RunTest(const FString& Parameters)
{
	FSeededBattleRandom Random(1);
	FBattleRandomDraw Draw;
	Draw.CallOrdinal = 999;

	const FBattleRandomContext ValidContext = MakeRandomContext();
	TestFalse(
		TEXT("An inverted inclusive range is rejected"),
		Random.TryDrawUniform(5, 4, ValidContext, Draw));
	TestEqual(TEXT("A rejected range resets the output"), Draw.CallOrdinal, 0ULL);
	TestEqual(TEXT("A rejected range consumes no trace entry"), Random.GetTrace().Num(), 0);

	const FBattleRandomContext InvalidContext;
	TestFalse(
		TEXT("An invalid RNG context is rejected"),
		Random.TryDrawUniform(0, 99, InvalidContext, Draw));
	TestEqual(TEXT("An invalid context consumes no trace entry"), Random.GetTrace().Num(), 0);

	TestTrue(
		TEXT("A one-value range still consumes one draw"),
		Random.TryDrawUniform(0, 0, ValidContext, Draw));
	TestEqual(TEXT("The first call ordinal is one"), Draw.CallOrdinal, 1ULL);
	TestEqual(TEXT("The one-value range has bound one"), Draw.Bound, 1ULL);
	TestEqual(TEXT("The one-value result is zero"), Draw.Result, static_cast<uint32>(0));
	TestEqual(TEXT("The first raw SplitMix64 value is frozen"), Draw.RawValue, 0x910A2DEC89025CC1ULL);
	TestTrue(TEXT("The draw retains its battle ID"), Draw.BattleId == ValidContext.BattleId);
	TestTrue(TEXT("The draw retains its turn ID"), Draw.TurnId == ValidContext.TurnId);
	TestTrue(TEXT("The draw retains its action ID"), Draw.ActionId == ValidContext.ActionId);
	TestTrue(TEXT("The draw retains its resolution ID"), Draw.ResolutionId == ValidContext.ResolutionId);
	TestTrue(TEXT("The draw retains its rule purpose"), Draw.RulePurpose == ValidContext.RulePurpose);

	TestTrue(
		TEXT("A non-zero inclusive range is accepted"),
		Random.TryDrawUniform(2, 4, ValidContext, Draw));
	TestEqual(TEXT("The second call ordinal is two"), Draw.CallOrdinal, 2ULL);
	TestEqual(TEXT("The inclusive 2..4 range has bound three"), Draw.Bound, 3ULL);
	TestEqual(TEXT("The second raw value maps deterministically into 2..4"), Draw.Result, static_cast<uint32>(3));
	TestEqual(TEXT("The second raw SplitMix64 value is frozen"), Draw.RawValue, 0xBEEB8DA1658EEC67ULL);
	TestEqual(TEXT("Two valid calls produce two trace entries"), Random.GetTrace().Num(), 2);
	TestTrue(TEXT("The returned second draw matches the stored trace"), Random.GetTrace()[1] == Draw);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleRandomReplayEquivalenceTest,
	"PokemonSolarus.Battle.CoreContracts.Random.ReplayEquivalence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBattleRandomReplayEquivalenceTest::RunTest(const FString& Parameters)
{
	FSeededBattleRandom First(987654321ULL);
	FSeededBattleRandom Second(987654321ULL);
	const FBattleRandomContext Context = MakeRandomContext();
	const TArray<TPair<uint32, uint32>> Ranges = {
		{0, 0},
		{0, 99},
		{2, 4},
		{0, 65535}
	};

	for (const TPair<uint32, uint32>& Range : Ranges)
	{
		FBattleRandomDraw FirstDraw;
		FBattleRandomDraw SecondDraw;
		TestTrue(
			TEXT("The first replay stream accepts the range"),
			First.TryDrawUniform(Range.Key, Range.Value, Context, FirstDraw));
		TestTrue(
			TEXT("The second replay stream accepts the range"),
			Second.TryDrawUniform(Range.Key, Range.Value, Context, SecondDraw));
		TestTrue(TEXT("Equal seed and requests produce equal draw records"), FirstDraw == SecondDraw);
		TestTrue(
			TEXT("Every mapped result remains inside its inclusive range"),
			FirstDraw.Result >= Range.Key && FirstDraw.Result <= Range.Value);
	}

	TestEqual(TEXT("Replay traces contain the same number of calls"), First.GetTrace().Num(), Second.GetTrace().Num());
	for (int32 Index = 0; Index < First.GetTrace().Num(); ++Index)
	{
		TestTrue(
			FString::Printf(TEXT("Replay trace entry %d is identical"), Index),
			First.GetTrace()[Index] == Second.GetTrace()[Index]);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
