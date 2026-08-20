#if WITH_DEV_AUTOMATION_TESTS

#include "Battle/BattleTypeChart.h"
#include "Misc/AutomationTest.h"

namespace
{
	constexpr int32 TypeCount = FBattleTypeChart::TypeCount;

	constexpr int32 ExpectedQuarterUnits[TypeCount][TypeCount] =
	{
		{4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 2, 0, 4, 4, 2, 4},
		{4, 2, 2, 4, 8, 8, 4, 4, 4, 4, 4, 8, 2, 4, 2, 4, 8, 4},
		{4, 8, 2, 4, 2, 4, 4, 4, 8, 4, 4, 4, 8, 4, 2, 4, 4, 4},
		{4, 4, 8, 2, 2, 4, 4, 4, 0, 8, 4, 4, 4, 4, 2, 4, 4, 4},
		{4, 2, 8, 4, 2, 4, 4, 2, 8, 2, 4, 2, 8, 4, 2, 4, 2, 4},
		{4, 2, 2, 4, 8, 2, 4, 4, 8, 8, 4, 4, 4, 4, 8, 4, 2, 4},
		{8, 4, 4, 4, 4, 8, 4, 2, 4, 2, 2, 2, 8, 0, 4, 8, 8, 2},
		{4, 4, 4, 4, 8, 4, 4, 2, 2, 4, 4, 4, 2, 2, 4, 4, 0, 8},
		{4, 8, 4, 8, 2, 4, 4, 8, 4, 0, 4, 2, 8, 4, 4, 4, 8, 4},
		{4, 4, 4, 2, 8, 4, 8, 4, 4, 4, 4, 8, 2, 4, 4, 4, 2, 4},
		{4, 4, 4, 4, 4, 4, 8, 8, 4, 4, 2, 4, 4, 4, 4, 0, 2, 4},
		{4, 2, 4, 4, 8, 4, 2, 2, 4, 2, 8, 4, 4, 2, 4, 8, 2, 2},
		{4, 8, 4, 4, 4, 8, 2, 4, 2, 8, 4, 8, 4, 4, 4, 4, 2, 4},
		{0, 4, 4, 4, 4, 4, 4, 4, 4, 4, 8, 4, 4, 8, 4, 2, 4, 4},
		{4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 8, 4, 2, 0},
		{4, 4, 4, 4, 4, 4, 2, 4, 4, 4, 8, 4, 4, 8, 4, 2, 4, 2},
		{4, 2, 2, 2, 4, 8, 4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 2, 8},
		{4, 2, 4, 4, 4, 4, 8, 2, 4, 4, 4, 4, 4, 4, 8, 8, 2, 4}
	};

	FBattleTypeEffectiveness EffectivenessFromQuarterUnits(const int32 QuarterUnits)
	{
		switch (QuarterUnits)
		{
		case 0:
			return {0, 1};
		case 2:
			return {1, 2};
		case 4:
			return {1, 1};
		case 8:
			return {2, 1};
		default:
			return {};
		}
	}

	TArray<FBattleTypeChartEntry> MakeModernTypeChartEntries()
	{
		TArray<FBattleTypeChartEntry> Entries;
		Entries.Reserve(FBattleTypeChart::EntryCount);
		for (int32 AttackingIndex = 0; AttackingIndex < TypeCount; ++AttackingIndex)
		{
			for (int32 DefendingIndex = 0; DefendingIndex < TypeCount; ++DefendingIndex)
			{
				const FBattleTypeEffectiveness Expected =
					EffectivenessFromQuarterUnits(ExpectedQuarterUnits[AttackingIndex][DefendingIndex]);
				Entries.Add(
					{
						static_cast<EPokemonType>(AttackingIndex),
						static_cast<EPokemonType>(DefendingIndex),
						Expected.Numerator,
						Expected.Denominator
					});
			}
		}
		return Entries;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC02BAllTypeChartEntriesTest,
	"PokemonSolarus.Battle.C02B.TypeChart.All324Entries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBattleC02BAllTypeChartEntriesTest::RunTest(const FString& Parameters)
{
	const TArray<FBattleTypeChartEntry> Entries = MakeModernTypeChartEntries();
	TestEqual(TEXT("The modern chart fixture contains exactly 324 entries"), Entries.Num(), 324);

	FBattleTypeChart Chart;
	EBattleTypeChartValidationError Error = EBattleTypeChartValidationError::None;
	TestTrue(TEXT("The complete modern chart is accepted"), FBattleTypeChart::TryCreate(Entries, Chart, Error));
	TestEqual(TEXT("Successful chart construction reports no error"), Error, EBattleTypeChartValidationError::None);
	TestTrue(TEXT("The successfully constructed chart is valid"), Chart.IsValid());

	for (int32 AttackingIndex = 0; AttackingIndex < TypeCount; ++AttackingIndex)
	{
		for (int32 DefendingIndex = 0; DefendingIndex < TypeCount; ++DefendingIndex)
		{
			FBattleTypeEffectiveness Actual;
			const EPokemonType AttackingType = static_cast<EPokemonType>(AttackingIndex);
			const EPokemonType DefendingType = static_cast<EPokemonType>(DefendingIndex);
			TestTrue(
				FString::Printf(TEXT("Chart entry %d,%d can be queried"), AttackingIndex, DefendingIndex),
				Chart.TryGetEffectiveness(AttackingType, DefendingType, Actual));

			const FBattleTypeEffectiveness Expected =
				EffectivenessFromQuarterUnits(ExpectedQuarterUnits[AttackingIndex][DefendingIndex]);
			TestEqual(
				FString::Printf(TEXT("Chart entry %d,%d has the expected numerator"), AttackingIndex, DefendingIndex),
				Actual.Numerator,
				Expected.Numerator);
			TestEqual(
				FString::Printf(TEXT("Chart entry %d,%d has the expected denominator"), AttackingIndex, DefendingIndex),
				Actual.Denominator,
				Expected.Denominator);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC02BTypeChartProductsAndValidationTest,
	"PokemonSolarus.Battle.C02B.TypeChart.ProductsAndValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBattleC02BTypeChartProductsAndValidationTest::RunTest(const FString& Parameters)
{
	TArray<FBattleTypeChartEntry> Entries = MakeModernTypeChartEntries();
	FBattleTypeChart Chart;
	EBattleTypeChartValidationError Error = EBattleTypeChartValidationError::None;
	TestTrue(TEXT("The valid chart fixture is accepted"), FBattleTypeChart::TryCreate(Entries, Chart, Error));

	FBattleTypeEffectiveness Effectiveness;
	TestTrue(
		TEXT("Fire against Grass can be queried"),
		Chart.TryGetEffectiveness(EPokemonType::Fire, EPokemonType::Grass, Effectiveness));
	TestEqual(TEXT("Fire against Grass is super-effective"), Effectiveness.Numerator, 2);
	TestEqual(TEXT("Super-effective denominator is one"), Effectiveness.Denominator, 1);

	TestTrue(
		TEXT("Electric against Ground can be queried"),
		Chart.TryGetEffectiveness(EPokemonType::Electric, EPokemonType::Ground, Effectiveness));
	TestTrue(TEXT("Electric against Ground is immune"), Effectiveness.IsImmune());

	TestTrue(
		TEXT("Ice against Dragon and Flying can be queried as an ordered dual type"),
		Chart.TryGetDualEffectiveness(
			EPokemonType::Ice,
			EPokemonType::Dragon,
			EPokemonType::Flying,
			Effectiveness));
	TestEqual(TEXT("Two super-effective factors produce four times"), Effectiveness.Numerator, 4);
	TestEqual(TEXT("The four-times product is integral"), Effectiveness.Denominator, 1);

	TestTrue(
		TEXT("Fire against Water and Dragon can be queried"),
		Chart.TryGetDualEffectiveness(
			EPokemonType::Fire,
			EPokemonType::Water,
			EPokemonType::Dragon,
			Effectiveness));
	TestEqual(TEXT("Two resistances produce one quarter"), Effectiveness.Numerator, 1);
	TestEqual(TEXT("Two resistances preserve the exact denominator"), Effectiveness.Denominator, 4);

	TestTrue(
		TEXT("Fire against Water and Flying can be queried"),
		Chart.TryGetDualEffectiveness(
			EPokemonType::Fire,
			EPokemonType::Water,
			EPokemonType::Flying,
			Effectiveness));
	TestEqual(TEXT("Resistance and neutral produce one half"), Effectiveness.Numerator, 1);
	TestEqual(TEXT("One-half product preserves denominator two"), Effectiveness.Denominator, 2);

	TestTrue(
		TEXT("Fire against Water and Grass can be queried"),
		Chart.TryGetDualEffectiveness(
			EPokemonType::Fire,
			EPokemonType::Water,
			EPokemonType::Grass,
			Effectiveness));
	TestEqual(TEXT("Resistance and weakness cancel to neutral"), Effectiveness.Numerator, 1);
	TestEqual(TEXT("Neutral product has denominator one"), Effectiveness.Denominator, 1);

	TestTrue(
		TEXT("Fire against Grass and Flying can be queried"),
		Chart.TryGetDualEffectiveness(
			EPokemonType::Fire,
			EPokemonType::Grass,
			EPokemonType::Flying,
			Effectiveness));
	TestEqual(TEXT("Weakness and neutral produce two times"), Effectiveness.Numerator, 2);
	TestEqual(TEXT("Two-times product has denominator one"), Effectiveness.Denominator, 1);

	TestTrue(
		TEXT("Normal against Rock and Ghost can be queried"),
		Chart.TryGetDualEffectiveness(
			EPokemonType::Normal,
			EPokemonType::Rock,
			EPokemonType::Ghost,
			Effectiveness));
	TestTrue(TEXT("Any immune dual-type factor produces zero"), Effectiveness.IsImmune());

	TestFalse(
		TEXT("Duplicate stored defending types are rejected by the dual query"),
		Chart.TryGetDualEffectiveness(
			EPokemonType::Fire,
			EPokemonType::Grass,
			EPokemonType::Grass,
			Effectiveness));
	TestEqual(TEXT("A rejected query resets the numerator"), Effectiveness.Numerator, 0);
	TestEqual(TEXT("A rejected query resets the denominator"), Effectiveness.Denominator, 1);

	TArray<FBattleTypeChartEntry> Incomplete = Entries;
	Incomplete.Pop(EAllowShrinking::No);
	FBattleTypeChart RejectedChart = Chart;
	TestFalse(
		TEXT("An incomplete chart is rejected"),
		FBattleTypeChart::TryCreate(Incomplete, RejectedChart, Error));
	TestEqual(TEXT("Incomplete chart rejection is typed"), Error, EBattleTypeChartValidationError::IncompleteChart);
	TestFalse(TEXT("Failure atomically resets the output chart"), RejectedChart.IsValid());

	TArray<FBattleTypeChartEntry> Duplicate = Entries;
	Duplicate.Last() = Duplicate[0];
	TestFalse(
		TEXT("A duplicate attacking-defending pair is rejected"),
		FBattleTypeChart::TryCreate(Duplicate, RejectedChart, Error));
	TestEqual(TEXT("Duplicate chart rejection is typed"), Error, EBattleTypeChartValidationError::DuplicateEntry);

	TArray<FBattleTypeChartEntry> InvalidMultiplier = Entries;
	InvalidMultiplier[0].Numerator = 3;
	InvalidMultiplier[0].Denominator = 2;
	TestFalse(
		TEXT("A non-canonical multiplier is rejected"),
		FBattleTypeChart::TryCreate(InvalidMultiplier, RejectedChart, Error));
	TestEqual(
		TEXT("Invalid multiplier rejection is typed"),
		Error,
		EBattleTypeChartValidationError::InvalidMultiplier);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
