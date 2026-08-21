#if WITH_DEV_AUTOMATION_TESTS

#include "Battle/BattleStatCalculator.h"
#include "Math/NumericLimits.h"
#include "Misc/AutomationTest.h"

namespace BattleStatCalculatorTests
{
	FPokemonStatInputs MakeUniformInputs(const int32 BaseStat)
	{
		FPokemonStatInputs Inputs;
		Inputs.Level = 50;
		Inputs.BaseStats = {BaseStat, BaseStat, BaseStat, BaseStat, BaseStat, BaseStat};
		Inputs.IndividualValues = {0, 0, 0, 0, 0, 0};
		Inputs.EffortValues = {0, 0, 0, 0, 0, 0};
		return Inputs;
	}

	struct FNamedStatMember
	{
		const TCHAR* Name;
		int32 FPokemonStatValues::* Member;
	};

	const FNamedStatMember StatMembers[] =
	{
		{TEXT("HP"), &FPokemonStatValues::HP},
		{TEXT("Attack"), &FPokemonStatValues::Attack},
		{TEXT("Defense"), &FPokemonStatValues::Defense},
		{TEXT("Special Attack"), &FPokemonStatValues::SpecialAttack},
		{TEXT("Special Defense"), &FPokemonStatValues::SpecialDefense},
		{TEXT("Speed"), &FPokemonStatValues::Speed}
	};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC02APermanentStatsLevelAndNatureGoldensTest,
	"PokemonSolarus.Battle.C02A.PermanentStats.LevelAndNatureGoldens",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBattleC02APermanentStatsLevelAndNatureGoldensTest::RunTest(const FString& Parameters)
{
	EBattleStatCalculationError Error = EBattleStatCalculationError::None;
	FPokemonBattleStats Stats;

	FPokemonStatInputs LevelOneInputs = MakeUniformInputs(2);
	LevelOneInputs.Level = 1;
	TestTrue(
		TEXT("A level-one minimum fixture is accepted"),
		FBattleStatCalculator::TryCalculatePermanentStats(LevelOneInputs, Stats, Error));
	TestEqual(TEXT("Level one HP uses the modern formula"), Stats.MaxHP, 11);
	TestEqual(TEXT("Level one Attack uses the modern formula"), Stats.Attack, 5);
	TestEqual(TEXT("Level one Defense uses the modern formula"), Stats.Defense, 5);
	TestEqual(TEXT("Level one Special Attack uses the modern formula"), Stats.SpecialAttack, 5);
	TestEqual(TEXT("Level one Special Defense uses the modern formula"), Stats.SpecialDefense, 5);
	TestEqual(TEXT("Level one Speed uses the modern formula"), Stats.Speed, 5);

	FPokemonStatInputs LevelFiftyInputs;
	LevelFiftyInputs.Level = 50;
	LevelFiftyInputs.BaseStats = {78, 84, 78, 109, 85, 100};
	LevelFiftyInputs.IndividualValues = {31, 31, 31, 31, 31, 31};
	LevelFiftyInputs.EffortValues = {0, 0, 0, 0, 0, 0};
	TestTrue(
		TEXT("The neutral level-50 Charizard fixture is accepted"),
		FBattleStatCalculator::TryCalculatePermanentStats(LevelFiftyInputs, Stats, Error));
	TestEqual(TEXT("The level-50 fixture has 153 HP"), Stats.MaxHP, 153);
	TestEqual(TEXT("The level-50 fixture has 104 Attack"), Stats.Attack, 104);
	TestEqual(TEXT("The level-50 fixture has 98 Defense"), Stats.Defense, 98);
	TestEqual(TEXT("The level-50 fixture has 129 Special Attack"), Stats.SpecialAttack, 129);
	TestEqual(TEXT("The level-50 fixture has 105 Special Defense"), Stats.SpecialDefense, 105);
	TestEqual(TEXT("The level-50 fixture has 120 Speed"), Stats.Speed, 120);

	FPokemonStatInputs LevelOneHundredInputs = MakeUniformInputs(255);
	LevelOneHundredInputs.Level = 100;
	LevelOneHundredInputs.IndividualValues = {31, 31, 31, 31, 31, 31};
	LevelOneHundredInputs.EffortValues = {252, 252, 0, 6, 0, 0};
	TestTrue(
		TEXT("A valid boosted and reduced nature can be created"),
		FNatureStatModifier::TryCreate(
			ENatureStat::Attack,
			ENatureStat::Defense,
			LevelOneHundredInputs.NatureModifier));
	TestTrue(
		TEXT("The level-100 total-510 fixture is accepted"),
		FBattleStatCalculator::TryCalculatePermanentStats(LevelOneHundredInputs, Stats, Error));
	TestEqual(TEXT("The level-100 fixture has 714 HP"), Stats.MaxHP, 714);
	TestEqual(TEXT("The boosted level-100 Attack floors to 669"), Stats.Attack, 669);
	TestEqual(TEXT("The reduced level-100 Defense floors to 491"), Stats.Defense, 491);
	TestEqual(TEXT("EV division occurs before level scaling"), Stats.SpecialAttack, 547);
	TestEqual(TEXT("The unaffected level-100 Special Defense is 546"), Stats.SpecialDefense, 546);
	TestEqual(TEXT("The unaffected level-100 Speed is 546"), Stats.Speed, 546);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC02APermanentStatsValidationBoundariesTest,
	"PokemonSolarus.Battle.C02A.PermanentStats.ValidationBoundaries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBattleC02APermanentStatsValidationBoundariesTest::RunTest(const FString& Parameters)
{
	auto TestInvalid = [this](
		const FString& CaseName,
		const FPokemonStatInputs& Inputs,
		const EBattleStatCalculationError ExpectedError)
	{
		FPokemonBattleStats Output = {99, 99, 99, 99, 99, 99};
		EBattleStatCalculationError Error = EBattleStatCalculationError::None;
		TestFalse(
			CaseName + TEXT(" is rejected"),
			FBattleStatCalculator::TryCalculatePermanentStats(Inputs, Output, Error));
		TestEqual(CaseName + TEXT(" reports the expected error"), Error, ExpectedError);
		TestEqual(CaseName + TEXT(" resets Max HP"), Output.MaxHP, 0);
		TestEqual(CaseName + TEXT(" resets Speed"), Output.Speed, 0);
	};

	FPokemonStatInputs Inputs = MakeUniformInputs(1);
	Inputs.Level = 1;
	FPokemonBattleStats Stats;
	EBattleStatCalculationError Error = EBattleStatCalculationError::None;
	TestTrue(
		TEXT("Level, base-stat, IV, and EV minimums are accepted"),
		FBattleStatCalculator::TryCalculatePermanentStats(Inputs, Stats, Error));

	FPokemonStatInputs Invalid = Inputs;
	Invalid.Level = 0;
	TestInvalid(TEXT("Level zero"), Invalid, EBattleStatCalculationError::InvalidLevel);
	Invalid = Inputs;
	Invalid.Level = 101;
	TestInvalid(TEXT("Level 101"), Invalid, EBattleStatCalculationError::InvalidLevel);

	for (const FNamedStatMember& Field : StatMembers)
	{
		FPokemonStatInputs BaseInvalid = Inputs;
		BaseInvalid.BaseStats.*Field.Member = 0;
		TestInvalid(
			FString::Printf(TEXT("Zero %s base stat"), Field.Name),
			BaseInvalid,
			EBattleStatCalculationError::InvalidBaseStat);

		FPokemonStatInputs IVMaximum = Inputs;
		IVMaximum.IndividualValues.*Field.Member = 31;
		TestTrue(
			FString::Printf(TEXT("%s IV 31 is accepted"), Field.Name),
			FBattleStatCalculator::TryCalculatePermanentStats(IVMaximum, Stats, Error));

		FPokemonStatInputs IVBelow = Inputs;
		IVBelow.IndividualValues.*Field.Member = -1;
		TestInvalid(
			FString::Printf(TEXT("%s IV -1"), Field.Name),
			IVBelow,
			EBattleStatCalculationError::InvalidIndividualValue);

		FPokemonStatInputs IVAbove = Inputs;
		IVAbove.IndividualValues.*Field.Member = 32;
		TestInvalid(
			FString::Printf(TEXT("%s IV 32"), Field.Name),
			IVAbove,
			EBattleStatCalculationError::InvalidIndividualValue);

		FPokemonStatInputs EVMaximum = Inputs;
		EVMaximum.EffortValues.*Field.Member = 252;
		TestTrue(
			FString::Printf(TEXT("%s EV 252 is accepted"), Field.Name),
			FBattleStatCalculator::TryCalculatePermanentStats(EVMaximum, Stats, Error));

		FPokemonStatInputs EVBelow = Inputs;
		EVBelow.EffortValues.*Field.Member = -1;
		TestInvalid(
			FString::Printf(TEXT("%s EV -1"), Field.Name),
			EVBelow,
			EBattleStatCalculationError::InvalidEffortValue);

		FPokemonStatInputs EVAbove = Inputs;
		EVAbove.EffortValues.*Field.Member = 253;
		TestInvalid(
			FString::Printf(TEXT("%s EV 253"), Field.Name),
			EVAbove,
			EBattleStatCalculationError::InvalidEffortValue);
	}

	FPokemonStatInputs TotalFiveHundredTen = Inputs;
	TotalFiveHundredTen.EffortValues = {252, 252, 6, 0, 0, 0};
	TestTrue(
		TEXT("An EV total of exactly 510 is accepted"),
		FBattleStatCalculator::TryCalculatePermanentStats(TotalFiveHundredTen, Stats, Error));

	FPokemonStatInputs TotalFiveHundredEleven = Inputs;
	TotalFiveHundredEleven.EffortValues = {252, 252, 7, 0, 0, 0};
	TestInvalid(
		TEXT("An EV total of 511"),
		TotalFiveHundredEleven,
		EBattleStatCalculationError::EffortValueTotalExceeded);

	FPokemonStatInputs Overflow = Inputs;
	Overflow.Level = 100;
	Overflow.BaseStats.HP = TNumericLimits<int32>::Max();
	TestInvalid(
		TEXT("An unrepresentable calculated stat"),
		Overflow,
		EBattleStatCalculationError::ArithmeticOverflow);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC02APermanentStatsRoundingAndBaseOneTest,
	"PokemonSolarus.Battle.C02A.PermanentStats.RoundingAndBaseOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBattleC02APermanentStatsRoundingAndBaseOneTest::RunTest(const FString& Parameters)
{
	FPokemonStatInputs Inputs = MakeUniformInputs(85);
	Inputs.IndividualValues = {31, 31, 31, 31, 31, 31};
	TestTrue(
		TEXT("A rounding-boundary nature can be created"),
		FNatureStatModifier::TryCreate(
			ENatureStat::Attack,
			ENatureStat::Defense,
			Inputs.NatureModifier));

	FPokemonBattleStats Stats;
	EBattleStatCalculationError Error = EBattleStatCalculationError::None;
	TestTrue(
		TEXT("The rounding-boundary fixture is accepted"),
		FBattleStatCalculator::TryCalculatePermanentStats(Inputs, Stats, Error));
	TestEqual(TEXT("HP uses its separate formula"), Stats.MaxHP, 160);
	TestEqual(TEXT("An exact boosted .5 result rounds down"), Stats.Attack, 115);
	TestEqual(TEXT("An exact reduced .5 result rounds down"), Stats.Defense, 94);
	TestEqual(TEXT("An unaffected non-HP stat remains 105"), Stats.SpecialAttack, 105);

	Inputs.Level = 100;
	Inputs.BaseStats.HP = 1;
	Inputs.EffortValues.HP = 252;
	TestTrue(
		TEXT("The base-HP-one fixture is accepted"),
		FBattleStatCalculator::TryCalculatePermanentStats(Inputs, Stats, Error));
	TestEqual(TEXT("Base HP exactly one always produces Max HP one"), Stats.MaxHP, 1);
	return true;
}

}

#endif // WITH_DEV_AUTOMATION_TESTS
