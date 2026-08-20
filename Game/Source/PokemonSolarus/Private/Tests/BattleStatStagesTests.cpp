#if WITH_DEV_AUTOMATION_TESTS

#include "Battle/BattleStatCalculator.h"
#include "Math/NumericLimits.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC02AStatStagesAllBattleRatiosTest,
	"PokemonSolarus.Battle.C02A.StatStages.AllBattleRatios",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBattleC02AStatStagesAllBattleRatiosTest::RunTest(const FString& Parameters)
{
	const int32 Stages[] = {-6, -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5, 6};
	const int32 ExpectedValues[] = {25, 28, 33, 40, 50, 67, 101, 151, 202, 252, 303, 353, 404};
	FPokemonBattleStats PermanentStats = {101, 101, 101, 101, 101, 101};

	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Stages); ++Index)
	{
		FBattleStatStages StatStages;
		StatStages.ApplyChange(EBattleStat::Speed, Stages[Index]);

		int32 EffectiveSpeed = 0;
		TestTrue(
			FString::Printf(TEXT("Speed stage %d can be queried"), Stages[Index]),
			FBattleStatCalculator::TryCalculateEffectiveStat(
				PermanentStats,
				StatStages,
				EBattleStat::Speed,
				EffectiveSpeed));
		TestEqual(
			FString::Printf(TEXT("Speed stage %d uses the canonical ratio"), Stages[Index]),
			EffectiveSpeed,
			ExpectedValues[Index]);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC02AStatStagesChangeCapsAndBlockingTest,
	"PokemonSolarus.Battle.C02A.StatStages.ChangeCapsAndBlocking",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBattleC02AStatStagesChangeCapsAndBlockingTest::RunTest(const FString& Parameters)
{
	const EBattleStat AllStats[] =
	{
		EBattleStat::Attack,
		EBattleStat::Defense,
		EBattleStat::SpecialAttack,
		EBattleStat::SpecialDefense,
		EBattleStat::Speed,
		EBattleStat::Accuracy,
		EBattleStat::Evasion
	};

	FBattleStatStages Stages;
	for (const EBattleStat Stat : AllStats)
	{
		int32 Stage = 99;
		TestTrue(TEXT("Every known stage can be queried"), Stages.TryGetStage(Stat, Stage));
		TestEqual(TEXT("Every stage begins at zero"), Stage, 0);
	}

	FBattleStatStageChangeResult Result = Stages.ApplyChange(EBattleStat::Attack, 5);
	TestEqual(TEXT("An in-range increase is applied"), Result.Outcome, EBattleStatStageChangeOutcome::Applied);
	TestEqual(TEXT("The full in-range increase is reported"), Result.AppliedDelta, 5);
	TestFalse(TEXT("An in-range increase is not clamped"), Result.bClamped);

	Result = Stages.ApplyChange(EBattleStat::Attack, 2);
	TestEqual(TEXT("A partial increase is still applied"), Result.Outcome, EBattleStatStageChangeOutcome::Applied);
	TestEqual(TEXT("The previous stage is reported"), Result.PreviousStage, 5);
	TestEqual(TEXT("Only one stage is applied at the cap"), Result.AppliedDelta, 1);
	TestEqual(TEXT("The new stage is capped at six"), Result.NewStage, 6);
	TestTrue(TEXT("The capped request reports clamping"), Result.bClamped);

	Result = Stages.ApplyChange(EBattleStat::Attack, 1);
	TestEqual(TEXT("An outward request at the cap is blocked"), Result.Outcome, EBattleStatStageChangeOutcome::Blocked);
	TestEqual(TEXT("A blocked request applies no stages"), Result.AppliedDelta, 0);
	TestEqual(TEXT("A blocked request leaves the cap unchanged"), Result.NewStage, 6);

	Result = Stages.ApplyChange(EBattleStat::Attack, -20);
	TestEqual(TEXT("A cross-range reduction is applied"), Result.Outcome, EBattleStatStageChangeOutcome::Applied);
	TestEqual(TEXT("The cross-range reduction reaches minus six"), Result.NewStage, -6);
	TestEqual(TEXT("The actual cross-range delta is reported"), Result.AppliedDelta, -12);
	TestTrue(TEXT("The cross-range reduction reports clamping"), Result.bClamped);

	Result = Stages.ApplyChange(EBattleStat::Attack, -1);
	TestEqual(TEXT("An outward request at the floor is blocked"), Result.Outcome, EBattleStatStageChangeOutcome::Blocked);
	TestEqual(TEXT("The floor remains minus six"), Result.NewStage, -6);

	Result = Stages.ApplyChange(EBattleStat::Defense, 0);
	TestEqual(TEXT("A zero change is blocked"), Result.Outcome, EBattleStatStageChangeOutcome::Blocked);
	TestFalse(TEXT("A zero change is not a clamp"), Result.bClamped);

	Stages.ApplyChange(EBattleStat::Accuracy, 2);
	Stages.ApplyChange(EBattleStat::Evasion, -3);
	int32 Accuracy = 0;
	int32 Evasion = 0;
	TestTrue(TEXT("Accuracy remains independently queryable"), Stages.TryGetStage(EBattleStat::Accuracy, Accuracy));
	TestTrue(TEXT("Evasion remains independently queryable"), Stages.TryGetStage(EBattleStat::Evasion, Evasion));
	TestEqual(TEXT("Accuracy stores its own stage"), Accuracy, 2);
	TestEqual(TEXT("Evasion stores its own stage"), Evasion, -3);

	int32 InvalidStage = 99;
	TestFalse(
		TEXT("An unknown stat cannot be queried"),
		Stages.TryGetStage(static_cast<EBattleStat>(255), InvalidStage));
	TestEqual(TEXT("A rejected stage query resets its output"), InvalidStage, 0);

	Result = Stages.ApplyChange(static_cast<EBattleStat>(255), 1);
	TestEqual(TEXT("An unknown stat change is invalid"), Result.Outcome, EBattleStatStageChangeOutcome::Invalid);
	TestEqual(TEXT("An invalid stat change applies nothing"), Result.AppliedDelta, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC02AAccuracyEvasionAllCombinedRatiosTest,
	"PokemonSolarus.Battle.C02A.AccuracyEvasion.AllCombinedRatios",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBattleC02AAccuracyEvasionAllCombinedRatiosTest::RunTest(const FString& Parameters)
{
	const int32 CombinedStages[] = {-6, -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5, 6};
	const int32 ExpectedAccuracy[] = {33, 37, 42, 50, 60, 75, 100, 133, 166, 200, 233, 266, 300};

	for (int32 Index = 0; Index < UE_ARRAY_COUNT(CombinedStages); ++Index)
	{
		FBattleStatStages AttackerStages;
		FBattleStatStages DefenderStages;
		AttackerStages.ApplyChange(EBattleStat::Accuracy, CombinedStages[Index]);

		int32 EffectiveAccuracy = 0;
		TestTrue(
			FString::Printf(TEXT("Combined accuracy stage %d can be queried"), CombinedStages[Index]),
			FBattleStatCalculator::TryCalculateEffectiveAccuracy(
				100,
				AttackerStages,
				DefenderStages,
				EffectiveAccuracy));
		TestEqual(
			FString::Printf(TEXT("Combined accuracy stage %d uses the canonical ratio"), CombinedStages[Index]),
			EffectiveAccuracy,
			ExpectedAccuracy[Index]);
	}

	FBattleStatStages MaximumAttacker;
	FBattleStatStages MinimumDefender;
	MaximumAttacker.ApplyChange(EBattleStat::Accuracy, 6);
	MinimumDefender.ApplyChange(EBattleStat::Evasion, -6);
	int32 EffectiveAccuracy = 0;
	TestTrue(
		TEXT("A combined stage above six is accepted"),
		FBattleStatCalculator::TryCalculateEffectiveAccuracy(
			100,
			MaximumAttacker,
			MinimumDefender,
			EffectiveAccuracy));
	TestEqual(TEXT("A combined stage above six clamps to six"), EffectiveAccuracy, 300);

	FBattleStatStages MinimumAttacker;
	FBattleStatStages MaximumDefender;
	MinimumAttacker.ApplyChange(EBattleStat::Accuracy, -6);
	MaximumDefender.ApplyChange(EBattleStat::Evasion, 6);
	TestTrue(
		TEXT("A combined stage below minus six is accepted"),
		FBattleStatCalculator::TryCalculateEffectiveAccuracy(
			100,
			MinimumAttacker,
			MaximumDefender,
			EffectiveAccuracy));
	TestEqual(TEXT("A combined stage below minus six clamps to minus six"), EffectiveAccuracy, 33);

	EffectiveAccuracy = 77;
	TestFalse(
		TEXT("Zero base accuracy is rejected"),
		FBattleStatCalculator::TryCalculateEffectiveAccuracy(
			0,
			MaximumAttacker,
			MinimumDefender,
			EffectiveAccuracy));
	TestEqual(TEXT("A rejected accuracy query resets its output"), EffectiveAccuracy, 0);

	TestFalse(
		TEXT("An unrepresentable effective accuracy is rejected"),
		FBattleStatCalculator::TryCalculateEffectiveAccuracy(
			TNumericLimits<int32>::Max(),
			MaximumAttacker,
			MinimumDefender,
			EffectiveAccuracy));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC02AEffectiveQueriesPreservePermanentStatsTest,
	"PokemonSolarus.Battle.C02A.EffectiveQueries.PreservePermanentStats",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBattleC02AEffectiveQueriesPreservePermanentStatsTest::RunTest(const FString& Parameters)
{
	FPokemonBattleStats PermanentStats = {197, 181, 163, 149, 137, 127};
	const FPokemonBattleStats OriginalStats = PermanentStats;
	FBattleStatStages Stages;
	Stages.ApplyChange(EBattleStat::Attack, 1);
	Stages.ApplyChange(EBattleStat::Defense, -1);
	Stages.ApplyChange(EBattleStat::SpecialAttack, 2);
	Stages.ApplyChange(EBattleStat::SpecialDefense, -2);
	Stages.ApplyChange(EBattleStat::Speed, 3);
	Stages.ApplyChange(EBattleStat::Accuracy, 4);
	Stages.ApplyChange(EBattleStat::Evasion, -4);

	const EBattleStat EffectiveStats[] =
	{
		EBattleStat::Attack,
		EBattleStat::Defense,
		EBattleStat::SpecialAttack,
		EBattleStat::SpecialDefense,
		EBattleStat::Speed
	};
	for (const EBattleStat Stat : EffectiveStats)
	{
		int32 EffectiveValue = 0;
		TestTrue(
			TEXT("Every non-HP battle stat has a pure effective query"),
			FBattleStatCalculator::TryCalculateEffectiveStat(
				PermanentStats,
				Stages,
				Stat,
				EffectiveValue));
	}

	int32 EffectiveAccuracy = 0;
	TestTrue(
		TEXT("The accuracy/evasion query is pure"),
		FBattleStatCalculator::TryCalculateEffectiveAccuracy(
			100,
			Stages,
			Stages,
			EffectiveAccuracy));

	TestEqual(TEXT("Max HP remains permanent"), PermanentStats.MaxHP, OriginalStats.MaxHP);
	TestEqual(TEXT("Attack remains permanent"), PermanentStats.Attack, OriginalStats.Attack);
	TestEqual(TEXT("Defense remains permanent"), PermanentStats.Defense, OriginalStats.Defense);
	TestEqual(TEXT("Special Attack remains permanent"), PermanentStats.SpecialAttack, OriginalStats.SpecialAttack);
	TestEqual(TEXT("Special Defense remains permanent"), PermanentStats.SpecialDefense, OriginalStats.SpecialDefense);
	TestEqual(TEXT("Speed remains permanent"), PermanentStats.Speed, OriginalStats.Speed);

	int32 InvalidValue = 77;
	TestFalse(
		TEXT("Accuracy is not a permanent battle-stat query"),
		FBattleStatCalculator::TryCalculateEffectiveStat(
			PermanentStats,
			Stages,
			EBattleStat::Accuracy,
			InvalidValue));
	TestEqual(TEXT("A rejected effective-stat query resets its output"), InvalidValue, 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
