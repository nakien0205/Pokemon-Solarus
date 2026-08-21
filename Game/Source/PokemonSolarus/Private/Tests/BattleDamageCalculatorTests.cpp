#if WITH_DEV_AUTOMATION_TESTS

#include "Battle/BattleDamageCalculator.h"
#include "Math/NumericLimits.h"
#include "Misc/AutomationTest.h"

namespace BattleDamageCalculatorTests
{
	FPokemonBattleStats MakeValidDamageStats()
	{
		FPokemonBattleStats Stats;
		Stats.MaxHP = 100;
		Stats.Attack = 100;
		Stats.Defense = 100;
		Stats.SpecialAttack = 100;
		Stats.SpecialDefense = 100;
		Stats.Speed = 100;
		return Stats;
	}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleDamageCalculatorArbitraryFixtureAndCategoryStatSelectionTest,
	"PokemonSolarus.Battle.DamageCalculator.ArbitraryFixtureAndCategoryStatSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBattleDamageCalculatorArbitraryFixtureAndCategoryStatSelectionTest::RunTest(
	const FString& Parameters)
{
	FPokemonBattleStats AttackerStats = MakeValidDamageStats();
	AttackerStats.Attack = 200;
	AttackerStats.SpecialAttack = 150;

	FPokemonBattleStats DefenderStats = MakeValidDamageStats();
	DefenderStats.Defense = 100;
	DefenderStats.SpecialDefense = 300;

	int32 PhysicalDamage = 0;
	int32 SpecialDamage = 0;
	TestTrue(
		TEXT("An arbitrary physical fixture calculates successfully"),
		FBattleDamageCalculator::TryCalculateDamage(
			50,
			AttackerStats,
			DefenderStats,
			EBattleMoveCategory::Physical,
			100,
			PhysicalDamage));
	TestTrue(
		TEXT("The same arbitrary fixture calculates successfully as special damage"),
		FBattleDamageCalculator::TryCalculateDamage(
			50,
			AttackerStats,
			DefenderStats,
			EBattleMoveCategory::Special,
			100,
			SpecialDamage));

	TestEqual(TEXT("Physical selects Attack and Defense"), PhysicalDamage, 90);
	TestEqual(TEXT("Special selects Special Attack and Special Defense"), SpecialDamage, 24);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleDamageCalculatorKnownFixturesTest,
	"PokemonSolarus.Battle.DamageCalculator.KnownFixtures",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBattleDamageCalculatorKnownFixturesTest::RunTest(const FString& Parameters)
{
	FPokemonBattleStats FirstStats;
	FirstStats.MaxHP = 153;
	FirstStats.Attack = 104;
	FirstStats.Defense = 98;
	FirstStats.SpecialAttack = 129;
	FirstStats.SpecialDefense = 105;
	FirstStats.Speed = 120;

	FPokemonBattleStats SecondStats;
	SecondStats.MaxHP = 155;
	SecondStats.Attack = 102;
	SecondStats.Defense = 103;
	SecondStats.SpecialAttack = 120;
	SecondStats.SpecialDefense = 120;
	SecondStats.Speed = 100;

	int32 SpecialDamage = 0;
	int32 PhysicalDamage = 0;
	TestTrue(
		TEXT("The known special fixture calculates successfully"),
		FBattleDamageCalculator::TryCalculateDamage(
			50,
			FirstStats,
			SecondStats,
			EBattleMoveCategory::Special,
			90,
			SpecialDamage));
	TestTrue(
		TEXT("The known physical fixture calculates successfully"),
		FBattleDamageCalculator::TryCalculateDamage(
			50,
			SecondStats,
			FirstStats,
			EBattleMoveCategory::Physical,
			45,
			PhysicalDamage));

	TestEqual(TEXT("The known special fixture deals 44 base damage"), SpecialDamage, 44);
	TestEqual(TEXT("The known physical fixture deals 22 base damage"), PhysicalDamage, 22);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleDamageCalculatorInvalidInputsTest,
	"PokemonSolarus.Battle.DamageCalculator.InvalidInputs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBattleDamageCalculatorInvalidInputsTest::RunTest(const FString& Parameters)
{
	const FPokemonBattleStats ValidAttackerStats = MakeValidDamageStats();
	const FPokemonBattleStats ValidDefenderStats = MakeValidDamageStats();

	auto TestInvalid = [this](
		const TCHAR* CaseName,
		const int32 AttackerLevel,
		const FPokemonBattleStats& AttackerStats,
		const FPokemonBattleStats& DefenderStats,
		const EBattleMoveCategory MoveCategory,
		const int32 MovePower)
	{
		int32 Damage = 777;
		const bool bCalculated = FBattleDamageCalculator::TryCalculateDamage(
			AttackerLevel,
			AttackerStats,
			DefenderStats,
			MoveCategory,
			MovePower,
			Damage);
		TestFalse(FString::Printf(TEXT("%s returns false"), CaseName), bCalculated);
		TestEqual(FString::Printf(TEXT("%s resets output damage"), CaseName), Damage, 0);
	};

	TestInvalid(
		TEXT("Level zero"),
		0,
		ValidAttackerStats,
		ValidDefenderStats,
		EBattleMoveCategory::Physical,
		50);
	TestInvalid(
		TEXT("Level 101"),
		101,
		ValidAttackerStats,
		ValidDefenderStats,
		EBattleMoveCategory::Physical,
		50);
	TestInvalid(
		TEXT("Zero power"),
		50,
		ValidAttackerStats,
		ValidDefenderStats,
		EBattleMoveCategory::Physical,
		0);
	TestInvalid(
		TEXT("Negative power"),
		50,
		ValidAttackerStats,
		ValidDefenderStats,
		EBattleMoveCategory::Physical,
		-1);
	TestInvalid(
		TEXT("Status category"),
		50,
		ValidAttackerStats,
		ValidDefenderStats,
		EBattleMoveCategory::Status,
		50);
	TestInvalid(
		TEXT("Unknown category"),
		50,
		ValidAttackerStats,
		ValidDefenderStats,
		static_cast<EBattleMoveCategory>(255),
		50);

	FPokemonBattleStats InvalidStats = ValidAttackerStats;
	InvalidStats.Attack = 0;
	TestInvalid(
		TEXT("Zero selected offense"),
		50,
		InvalidStats,
		ValidDefenderStats,
		EBattleMoveCategory::Physical,
		50);
	InvalidStats.Attack = -1;
	TestInvalid(
		TEXT("Negative selected offense"),
		50,
		InvalidStats,
		ValidDefenderStats,
		EBattleMoveCategory::Physical,
		50);

	InvalidStats = ValidDefenderStats;
	InvalidStats.Defense = 0;
	TestInvalid(
		TEXT("Zero selected defense"),
		50,
		ValidAttackerStats,
		InvalidStats,
		EBattleMoveCategory::Physical,
		50);
	InvalidStats.Defense = -1;
	TestInvalid(
		TEXT("Negative selected defense"),
		50,
		ValidAttackerStats,
		InvalidStats,
		EBattleMoveCategory::Physical,
		50);

	InvalidStats = ValidAttackerStats;
	InvalidStats.SpecialAttack = 0;
	TestInvalid(
		TEXT("Zero selected special offense"),
		50,
		InvalidStats,
		ValidDefenderStats,
		EBattleMoveCategory::Special,
		50);
	InvalidStats = ValidDefenderStats;
	InvalidStats.SpecialDefense = 0;
	TestInvalid(
		TEXT("Zero selected special defense"),
		50,
		ValidAttackerStats,
		InvalidStats,
		EBattleMoveCategory::Special,
		50);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleDamageCalculatorLevelBoundariesAndMinimumTest,
	"PokemonSolarus.Battle.DamageCalculator.LevelBoundariesAndMinimum",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBattleDamageCalculatorLevelBoundariesAndMinimumTest::RunTest(const FString& Parameters)
{
	FPokemonBattleStats AttackerStats = MakeValidDamageStats();
	FPokemonBattleStats DefenderStats = MakeValidDamageStats();
	AttackerStats.Attack = 1;
	DefenderStats.Defense = TNumericLimits<int32>::Max();

	int32 LevelOneDamage = 0;
	int32 LevelOneHundredDamage = 0;
	TestTrue(
		TEXT("Level one is accepted"),
		FBattleDamageCalculator::TryCalculateDamage(
			1,
			AttackerStats,
			DefenderStats,
			EBattleMoveCategory::Physical,
			1,
			LevelOneDamage));
	TestTrue(
		TEXT("Level 100 is accepted"),
		FBattleDamageCalculator::TryCalculateDamage(
			100,
			AttackerStats,
			DefenderStats,
			EBattleMoveCategory::Physical,
			1,
			LevelOneHundredDamage));

	TestTrue(TEXT("Successful level-one damage is at least one"), LevelOneDamage >= 1);
	TestTrue(TEXT("Successful level-100 damage is at least one"), LevelOneHundredDamage >= 1);
	return true;
}

}

#endif // WITH_DEV_AUTOMATION_TESTS
