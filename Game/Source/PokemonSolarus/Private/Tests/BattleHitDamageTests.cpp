#if WITH_DEV_AUTOMATION_TESTS

#include "Battle/BattleFinalDamageCalculator.h"
#include "Battle/BattleHitResolver.h"
#include "BattleTestFactories.h"
#include "BattleTestRandom.h"
#include "Math/NumericLimits.h"
#include "Misc/AutomationTest.h"

namespace BattleC05AHitDamageTests
{
	using BattleTest::MakeDefinitionId;
	using BattleTest::MakeNumericId;
	using BattleTest::FSequenceBattleRandom;

	FBattleRandomContext MakeRandomContext(
		const TCHAR* RulePurpose,
		const uint64 ResolutionValue = 1)
	{
		FBattleRandomContext Context;
		Context.BattleId = MakeNumericId<FBattleId>(1);
		Context.TurnId = MakeNumericId<FTurnId>(1);
		Context.ActionId = MakeNumericId<FActionId>(1);
		Context.ResolutionId = MakeNumericId<FResolutionId>(ResolutionValue);
		Context.RulePurpose = MakeDefinitionId<FDefinitionId>(RulePurpose);
		return Context;
	}

	FPokemonBattleStats MakeDamageStats()
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

	FBattleFinalDamageInput MakeNeutralDamageInput()
	{
		FBattleFinalDamageInput Input;
		Input.AttackerLevel = 50;
		Input.AttackerStats = MakeDamageStats();
		Input.DefenderStats = MakeDamageStats();
		Input.MoveCategory = EBattleMoveCategory::Physical;
		Input.MovePower = 100;
		Input.WeatherModifierQ12 = FBattleFinalDamageCalculator::Q12Neutral;
		Input.StabModifierQ12 = FBattleFinalDamageCalculator::Q12Neutral;
		Input.TypeEffectiveness.Numerator = 1;
		Input.TypeEffectiveness.Denominator = 1;
		Input.RandomContext = MakeRandomContext(TEXT("Rule.C05A.DamageRandom"));
		return Input;
	}

	FBattleDamageModifier MakeModifier(
		const TCHAR* RuleName,
		const int32 ModifierQ12,
		const bool bIgnoredByCritical = false)
	{
		FBattleDamageModifier Modifier;
		Modifier.RuleId = MakeDefinitionId<FDefinitionId>(RuleName);
		Modifier.ModifierQ12 = ModifierQ12;
		Modifier.bIgnoredByCritical = bIgnoredByCritical;
		return Modifier;
	}

	TArray<FBattleTypeChartEntry> MakeFocusedTypeChartEntries()
	{
		TArray<FBattleTypeChartEntry> Entries;
		Entries.Reserve(FBattleTypeChart::EntryCount);
		for (int32 AttackingIndex = 0; AttackingIndex < FBattleTypeChart::TypeCount; ++AttackingIndex)
		{
			for (int32 DefendingIndex = 0; DefendingIndex < FBattleTypeChart::TypeCount; ++DefendingIndex)
			{
				FBattleTypeChartEntry Entry;
				Entry.AttackingType = static_cast<EPokemonType>(AttackingIndex);
				Entry.DefendingType = static_cast<EPokemonType>(DefendingIndex);
				Entry.Numerator = 1;
				Entry.Denominator = 1;
				if (Entry.AttackingType == EPokemonType::Fire
					&& (Entry.DefendingType == EPokemonType::Water
						|| Entry.DefendingType == EPokemonType::Dragon))
				{
					Entry.Denominator = 2;
				}
				else if (Entry.AttackingType == EPokemonType::Fire
					&& (Entry.DefendingType == EPokemonType::Grass
						|| Entry.DefendingType == EPokemonType::Steel))
				{
					Entry.Numerator = 2;
				}
				Entries.Add(Entry);
			}
		}
		return Entries;
	}

	const FBattleDamageTraceEntry* FindTraceEntry(
		const FDamageTrace& Trace,
		const EBattleDamageTraceStep Step,
		const FDefinitionId* RuleId = nullptr)
	{
		return Trace.Entries.FindByPredicate(
			[Step, RuleId](const FBattleDamageTraceEntry& Entry)
			{
				return Entry.Step == Step
					&& (RuleId == nullptr || Entry.RuleId == *RuleId);
			});
	}

	bool TestTraceValue(
		FAutomationTestBase& Test,
		const TCHAR* What,
		const FDamageTrace& Trace,
		const EBattleDamageTraceStep Step,
		const int64 ExpectedValue,
		const FDefinitionId* RuleId = nullptr)
	{
		const FBattleDamageTraceEntry* Entry = FindTraceEntry(Trace, Step, RuleId);
		if (!Test.TestNotNull(What, Entry))
		{
			return false;
		}
		return Test.TestEqual(What, Entry->Value, ExpectedValue);
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC05AAccuracyTest,
		"PokemonSolarus.Battle.C05A.Accuracy",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

	bool FBattleC05AAccuracyTest::RunTest(const FString& Parameters)
	{
		FSequenceBattleRandom AlwaysHitRandom({});
		FBattleAccuracyCheckInput AlwaysHitInput;
		AlwaysHitInput.bAlwaysHits = true;
		FBattleAccuracyCheckResult AccuracyResult;
		EBattleHitResolverError Error = EBattleHitResolverError::None;
		TestTrue(
			TEXT("Literal always-hit succeeds without a numeric accuracy or context"),
			FBattleHitResolver::TryResolveAccuracy(
				AlwaysHitInput,
				AlwaysHitRandom,
				AccuracyResult,
				Error));
		TestEqual(
			TEXT("Literal always-hit reports Hit"),
			AccuracyResult.Outcome,
			EBattleAccuracyCheckOutcome::Hit);
		TestFalse(TEXT("Literal always-hit consumes no draw"), AccuracyResult.bDrawConsumed);
		TestTrue(TEXT("Literal always-hit leaves the RNG trace empty"), AlwaysHitRandom.GetTrace().IsEmpty());

		FSequenceBattleRandom NumericHundredRandom({99});
		FBattleAccuracyCheckInput NumericHundredInput;
		NumericHundredInput.BaseAccuracy = 100;
		NumericHundredInput.RandomContext = MakeRandomContext(TEXT("Rule.C05A.Accuracy.Numeric100"));
		TestTrue(
			TEXT("Numeric accuracy 100 resolves"),
			FBattleHitResolver::TryResolveAccuracy(
				NumericHundredInput,
				NumericHundredRandom,
				AccuracyResult,
				Error));
		TestEqual(TEXT("Numeric 100 remains effective accuracy 100"), AccuracyResult.EffectiveAccuracy, 100);
		TestEqual(TEXT("Numeric 100 hits on roll 99"), AccuracyResult.Outcome, EBattleAccuracyCheckOutcome::Hit);
		TestTrue(TEXT("Numeric 100 still consumes a draw"), AccuracyResult.bDrawConsumed);
		TestEqual(TEXT("Numeric accuracy uses U[0,99]"), NumericHundredRandom.GetTrace()[0].InclusiveMaximum, 99U);

		FBattleAccuracyCheckInput ExtremeInput;
		ExtremeInput.BaseAccuracy = 100;
		ExtremeInput.AttackerStages.ApplyChange(EBattleStat::Accuracy, -6);
		ExtremeInput.DefenderStages.ApplyChange(EBattleStat::Evasion, 6);
		ExtremeInput.RandomContext = MakeRandomContext(TEXT("Rule.C05A.Accuracy.Extreme"));
		FSequenceBattleRandom ExtremeRandom({32, 33});
		TestTrue(
			TEXT("The clamped negative accuracy extreme resolves"),
			FBattleHitResolver::TryResolveAccuracy(
				ExtremeInput,
				ExtremeRandom,
				AccuracyResult,
				Error));
		TestEqual(TEXT("Combined -12 clamps to -6 and produces 33"), AccuracyResult.EffectiveAccuracy, 33);
		TestEqual(TEXT("Roll 32 hits effective accuracy 33"), AccuracyResult.Outcome, EBattleAccuracyCheckOutcome::Hit);
		TestTrue(
			TEXT("The same extreme can resolve again"),
			FBattleHitResolver::TryResolveAccuracy(
				ExtremeInput,
				ExtremeRandom,
				AccuracyResult,
				Error));
		TestEqual(TEXT("Roll 33 misses effective accuracy 33"), AccuracyResult.Outcome, EBattleAccuracyCheckOutcome::Miss);
		TestEqual(TEXT("Two numeric checks consume exactly two draws"), ExtremeRandom.GetTrace().Num(), 2);

		FBattleAccuracyCheckInput PositiveExtremeInput;
		PositiveExtremeInput.BaseAccuracy = 100;
		PositiveExtremeInput.AttackerStages.ApplyChange(EBattleStat::Accuracy, 6);
		PositiveExtremeInput.DefenderStages.ApplyChange(EBattleStat::Evasion, -6);
		PositiveExtremeInput.RandomContext = MakeRandomContext(TEXT("Rule.C05A.Accuracy.PositiveExtreme"));
		FSequenceBattleRandom PositiveExtremeRandom({99});
		TestTrue(
			TEXT("The clamped positive accuracy extreme resolves"),
			FBattleHitResolver::TryResolveAccuracy(
				PositiveExtremeInput,
				PositiveExtremeRandom,
				AccuracyResult,
				Error));
		TestEqual(TEXT("Combined +12 clamps to +6 and produces 300"), AccuracyResult.EffectiveAccuracy, 300);
		TestTrue(TEXT("Effective accuracy above 100 still consumes a draw"), AccuracyResult.bDrawConsumed);

		FBattleAccuracyCheckInput InvalidContextInput;
		InvalidContextInput.BaseAccuracy = 50;
		InvalidContextInput.RandomContext = MakeRandomContext(TEXT("Rule.C05A.Accuracy.InvalidAction"));
		InvalidContextInput.RandomContext.ActionId = FActionId();
		TestFalse(
			TEXT("A numeric check requires an action-scoped context"),
			FBattleHitResolver::TryResolveAccuracy(
				InvalidContextInput,
				AlwaysHitRandom,
				AccuracyResult,
				Error));
		TestEqual(TEXT("Invalid action context is typed"), Error, EBattleHitResolverError::InvalidRandomContext);

		FBattleAccuracyCheckInput InvalidInput;
		InvalidInput.BaseAccuracy = 0;
		AccuracyResult.Outcome = EBattleAccuracyCheckOutcome::Hit;
		TestFalse(
			TEXT("Numeric accuracy zero is rejected"),
			FBattleHitResolver::TryResolveAccuracy(
				InvalidInput,
				AlwaysHitRandom,
				AccuracyResult,
				Error));
		TestEqual(TEXT("Invalid accuracy reports a typed error"), Error, EBattleHitResolverError::InvalidAccuracy);
		TestEqual(TEXT("A rejected result resets to Invalid"), AccuracyResult.Outcome, EBattleAccuracyCheckOutcome::Invalid);

		InvalidInput.BaseAccuracy = 101;
		TestFalse(
			TEXT("Authored numeric accuracy above 100 is rejected"),
			FBattleHitResolver::TryResolveAccuracy(
				InvalidInput,
				AlwaysHitRandom,
				AccuracyResult,
				Error));
		InvalidInput.bAlwaysHits = true;
		TestFalse(
			TEXT("Literal always-hit cannot also carry numeric accuracy"),
			FBattleHitResolver::TryResolveAccuracy(
				InvalidInput,
				AlwaysHitRandom,
				AccuracyResult,
				Error));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC05ACriticalTest,
		"PokemonSolarus.Battle.C05A.Critical",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

	bool FBattleC05ACriticalTest::RunTest(const FString& Parameters)
	{
		FSequenceBattleRandom Random({0, 7, 1, 0, 0});
		FBattleCriticalCheckInput Input;
		Input.RandomContext = MakeRandomContext(TEXT("Rule.C05A.Critical"));
		FBattleCriticalCheckResult Result;
		EBattleHitResolverError Error = EBattleHitResolverError::None;

		Input.BaseStage = 0;
		TestTrue(TEXT("Critical stage zero resolves"), FBattleHitResolver::TryResolveCritical(Input, Random, Result, Error));
		TestEqual(TEXT("Stage zero cannot critical"), Result.Outcome, EBattleCriticalCheckOutcome::NotCritical);
		TestFalse(TEXT("Stage zero consumes no draw"), Result.bDrawConsumed);

		Input.BaseStage = 1;
		TestTrue(TEXT("Critical stage one resolves"), FBattleHitResolver::TryResolveCritical(Input, Random, Result, Error));
		TestEqual(TEXT("Stage one uses U[0,23]"), Result.Draw.InclusiveMaximum, 23U);
		TestEqual(TEXT("Stage one roll zero criticals"), Result.Outcome, EBattleCriticalCheckOutcome::Critical);
		TestTrue(TEXT("A critical ignores negative offense stages"), Result.bIgnoreNegativeOffensiveStage);
		TestTrue(TEXT("A critical ignores positive defense stages"), Result.bIgnorePositiveDefensiveStage);
		TestTrue(TEXT("A critical ignores screens"), Result.bIgnoreScreens);

		Input.BaseStage = 2;
		TestTrue(TEXT("Critical stage two resolves"), FBattleHitResolver::TryResolveCritical(Input, Random, Result, Error));
		TestEqual(TEXT("Stage two uses U[0,7]"), Result.Draw.InclusiveMaximum, 7U);
		TestEqual(TEXT("Stage two roll seven is not critical"), Result.Outcome, EBattleCriticalCheckOutcome::NotCritical);

		Input.BaseStage = 3;
		TestTrue(TEXT("Critical stage three resolves"), FBattleHitResolver::TryResolveCritical(Input, Random, Result, Error));
		TestEqual(TEXT("Stage three uses U[0,1]"), Result.Draw.InclusiveMaximum, 1U);
		TestEqual(TEXT("Stage three roll one is not critical"), Result.Outcome, EBattleCriticalCheckOutcome::NotCritical);

		Input.BaseStage = 5;
		TestTrue(TEXT("Critical stages clamp to four"), FBattleHitResolver::TryResolveCritical(Input, Random, Result, Error));
		TestEqual(TEXT("The clamped critical stage is four"), Result.ResolvedStage, 4);
		TestEqual(TEXT("Stage four still consumes U[0,0]"), Result.Draw.InclusiveMaximum, 0U);
		TestEqual(TEXT("Stage four roll zero criticals"), Result.Outcome, EBattleCriticalCheckOutcome::Critical);

		Input.BaseStage = 1;
		Input.bDefenderBlocksCritical = true;
		TestTrue(TEXT("A blocked critical still resolves"), FBattleHitResolver::TryResolveCritical(Input, Random, Result, Error));
		TestTrue(TEXT("The critical candidate was reached"), Result.bCriticalCandidate);
		TestEqual(TEXT("The blocker cancels the critical"), Result.Outcome, EBattleCriticalCheckOutcome::Blocked);
		TestTrue(TEXT("The blocked critical retains its draw"), Result.bDrawConsumed);

		Input.BaseStage = -4;
		Input.bDefenderBlocksCritical = false;
		TestTrue(TEXT("Negative critical stage resolves"), FBattleHitResolver::TryResolveCritical(Input, Random, Result, Error));
		TestEqual(TEXT("Negative critical stage clamps to zero"), Result.ResolvedStage, 0);
		TestFalse(TEXT("Clamped stage zero consumes no draw"), Result.bDrawConsumed);

		FSequenceBattleRandom FixedModeRandom({});
		Input = FBattleCriticalCheckInput();
		Input.Mode = EBattleCriticalCheckMode::Always;
		TestTrue(TEXT("Always-critical resolves"), FBattleHitResolver::TryResolveCritical(Input, FixedModeRandom, Result, Error));
		TestEqual(TEXT("Always-critical succeeds"), Result.Outcome, EBattleCriticalCheckOutcome::Critical);
		Input.Mode = EBattleCriticalCheckMode::Never;
		TestTrue(TEXT("Never-critical resolves"), FBattleHitResolver::TryResolveCritical(Input, FixedModeRandom, Result, Error));
		TestEqual(TEXT("Never-critical cannot critical"), Result.Outcome, EBattleCriticalCheckOutcome::NotCritical);
		TestTrue(TEXT("Fixed critical modes consume no draw"), FixedModeRandom.GetTrace().IsEmpty());

		Input.Mode = EBattleCriticalCheckMode::Invalid;
		TestFalse(TEXT("An invalid critical mode is rejected"), FBattleHitResolver::TryResolveCritical(Input, FixedModeRandom, Result, Error));
		TestEqual(TEXT("Invalid mode has a typed error"), Error, EBattleHitResolverError::InvalidCriticalMode);

		Input = FBattleCriticalCheckInput();
		Input.RandomContext = MakeRandomContext(TEXT("Rule.C05A.Critical.InvalidAction"));
		Input.RandomContext.ActionId = FActionId();
		TestFalse(
			TEXT("A random critical check requires an action-scoped context"),
			FBattleHitResolver::TryResolveCritical(Input, FixedModeRandom, Result, Error));
		TestEqual(TEXT("Invalid critical context is typed"), Error, EBattleHitResolverError::InvalidRandomContext);
		TestTrue(TEXT("Invalid critical context consumes no draw"), FixedModeRandom.GetTrace().IsEmpty());
		TestEqual(TEXT("Only five standard checks consumed draws"), Random.GetTrace().Num(), 5);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC05ABaseFixturesAndTraceTest,
		"PokemonSolarus.Battle.C05A.BaseFixturesAndTrace",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

	bool FBattleC05ABaseFixturesAndTraceTest::RunTest(const FString& Parameters)
	{
		FSequenceBattleRandom Random({0, 0, 0, 0});
		FBattleFinalDamageResult Result;
		EBattleDamageCalculationError Error = EBattleDamageCalculationError::None;

		FBattleFinalDamageInput Input = MakeNeutralDamageInput();
		Input.AttackerStats.Attack = 200;
		Input.AttackerStats.SpecialAttack = 150;
		Input.DefenderStats.Defense = 100;
		Input.DefenderStats.SpecialDefense = 300;
		TestTrue(
			TEXT("The arbitrary physical fixture resolves"),
			FBattleFinalDamageCalculator::TryCalculateFinalDamage(Input, Random, Result, Error));
		TestEqual(TEXT("The existing physical base fixture remains 90"), Result.Damage, 90);
		TestTraceValue(*this, TEXT("The trace records LevelTerm 22"), Result.Trace, EBattleDamageTraceStep::LevelTerm, 22);
		TestTraceValue(*this, TEXT("The trace records PowerTerm 2200"), Result.Trace, EBattleDamageTraceStep::PowerTerm, 2200);
		TestTraceValue(*this, TEXT("The trace records AttackTerm 440000"), Result.Trace, EBattleDamageTraceStep::AttackTerm, 440000);
		TestTraceValue(*this, TEXT("The trace records Quotient 4400"), Result.Trace, EBattleDamageTraceStep::Quotient, 4400);
		TestTraceValue(*this, TEXT("The trace records base damage 90"), Result.Trace, EBattleDamageTraceStep::BaseDamage, 90);
		TestTraceValue(*this, TEXT("The trace records final damage 90"), Result.Trace, EBattleDamageTraceStep::FinalDamage, 90);

		Input.MoveCategory = EBattleMoveCategory::Special;
		Input.RandomContext = MakeRandomContext(TEXT("Rule.C05A.DamageRandom"), 2);
		TestTrue(
			TEXT("The arbitrary special fixture resolves"),
			FBattleFinalDamageCalculator::TryCalculateFinalDamage(Input, Random, Result, Error));
		TestEqual(TEXT("The existing special base fixture remains 24"), Result.Damage, 24);

		Input = MakeNeutralDamageInput();
		Input.AttackerStats.MaxHP = 153;
		Input.AttackerStats.Attack = 104;
		Input.AttackerStats.Defense = 98;
		Input.AttackerStats.SpecialAttack = 129;
		Input.AttackerStats.SpecialDefense = 105;
		Input.AttackerStats.Speed = 120;
		Input.DefenderStats.MaxHP = 155;
		Input.DefenderStats.Attack = 102;
		Input.DefenderStats.Defense = 103;
		Input.DefenderStats.SpecialAttack = 120;
		Input.DefenderStats.SpecialDefense = 120;
		Input.DefenderStats.Speed = 100;
		Input.MoveCategory = EBattleMoveCategory::Special;
		Input.MovePower = 90;
		Input.RandomContext = MakeRandomContext(TEXT("Rule.C05A.DamageRandom"), 3);
		TestTrue(TEXT("The known special fixture resolves"), FBattleFinalDamageCalculator::TryCalculateFinalDamage(Input, Random, Result, Error));
		TestEqual(TEXT("The existing known special fixture remains 44"), Result.Damage, 44);

		Swap(Input.AttackerStats, Input.DefenderStats);
		Input.MoveCategory = EBattleMoveCategory::Physical;
		Input.MovePower = 45;
		Input.RandomContext = MakeRandomContext(TEXT("Rule.C05A.DamageRandom"), 4);
		TestTrue(TEXT("The known physical fixture resolves"), FBattleFinalDamageCalculator::TryCalculateFinalDamage(Input, Random, Result, Error));
		TestEqual(TEXT("The existing known physical fixture remains 22"), Result.Damage, 22);
		TestEqual(TEXT("Each final damage fixture consumes one random roll"), Random.GetTrace().Num(), 4);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC05ARandomDamageVectorsTest,
		"PokemonSolarus.Battle.C05A.RandomDamageVectors",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

	bool FBattleC05ARandomDamageVectorsTest::RunTest(const FString& Parameters)
	{
		TArray<uint32> Rolls;
		for (uint32 Roll = 0; Roll < 16; ++Roll)
		{
			Rolls.Add(Roll);
		}
		FSequenceBattleRandom Random(MoveTemp(Rolls));
		// Golden mapping: smogon/damage-calc@8380780 enumerates factors 85..100;
		// accepted B00B freezes the replay-facing raw mapping as Factor = 100 - Roll.
		const int32 ExpectedDamage[] =
		{
			90, 89, 88, 87, 86, 85, 84, 83,
			82, 81, 81, 80, 79, 78, 77, 76
		};

		for (int32 Roll = 0; Roll < UE_ARRAY_COUNT(ExpectedDamage); ++Roll)
		{
			FBattleFinalDamageInput Input = MakeNeutralDamageInput();
			Input.AttackerStats.Attack = 200;
			FBattleFinalDamageResult Result;
			EBattleDamageCalculationError Error = EBattleDamageCalculationError::None;
			TestTrue(
				FString::Printf(TEXT("Raw damage roll %d resolves"), Roll),
				FBattleFinalDamageCalculator::TryCalculateFinalDamage(Input, Random, Result, Error));
			TestEqual(
				FString::Printf(TEXT("Raw roll %d maps to factor %d"), Roll, 100 - Roll),
				Result.Damage,
				ExpectedDamage[Roll]);
			TestTraceValue(
				*this,
				*FString::Printf(TEXT("Trace records raw roll %d"), Roll),
				Result.Trace,
				EBattleDamageTraceStep::RandomRoll,
				Roll);
			TestTraceValue(
				*this,
				*FString::Printf(TEXT("Trace records factor %d"), 100 - Roll),
				Result.Trace,
				EBattleDamageTraceStep::RandomFactor,
				100 - Roll);
		}

		TestEqual(TEXT("All 16 vectors consume exactly 16 draws"), Random.GetTrace().Num(), 16);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC05AModifierOrderAndHooksTest,
		"PokemonSolarus.Battle.C05A.ModifierOrderAndHooks",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

	bool FBattleC05AModifierOrderAndHooksTest::RunTest(const FString& Parameters)
	{
		FSequenceBattleRandom Random({15, 0, 0, 0, 0, 0});
		FBattleTypeChart TypeChart;
		EBattleTypeChartValidationError TypeChartError = EBattleTypeChartValidationError::None;
		const TArray<FBattleTypeChartEntry> TypeChartEntries = MakeFocusedTypeChartEntries();
		TestTrue(
			TEXT("The complete focused type chart fixture is accepted"),
			FBattleTypeChart::TryCreate(TypeChartEntries, TypeChart, TypeChartError));
		FBattleTypeEffectiveness QuarterEffectiveness;
		TestTrue(
			TEXT("Fire into Water and Dragon resolves through the type chart"),
			TypeChart.TryGetDualEffectiveness(
				EPokemonType::Fire,
				EPokemonType::Water,
				EPokemonType::Dragon,
				QuarterEffectiveness));

		FBattleFinalDamageInput Input = MakeNeutralDamageInput();
		Input.AttackerStats.Attack = 200;
		Input.bSpreadAcrossMultipleTargets = true;
		Input.WeatherModifierQ12 = 6144;
		Input.bCritical = true;
		Input.StabModifierQ12 = 6144;
		Input.TypeEffectiveness = QuarterEffectiveness;
		Input.bAttackerBurned = true;
		const FBattleDamageModifier Screen = MakeModifier(TEXT("Condition.C05A.Reflect"), 2048, true);
		const FBattleDamageModifier LifeOrb = MakeModifier(TEXT("Item.C05A.LifeOrb"), 5324);
		Input.FinalModifiers = {Screen, LifeOrb};

		FBattleFinalDamageResult Result;
		EBattleDamageCalculationError Error = EBattleDamageCalculationError::None;
		// Golden sequence follows the exact staged algorithm pinned in B00B to
		// smogon/damage-calc@83807801012f0af3e2dbb543d6fd40b483b3ebab.
		TestTrue(
			TEXT("The compound pinned modifier vector resolves"),
			FBattleFinalDamageCalculator::TryCalculateFinalDamage(Input, Random, Result, Error));
		TestEqual(TEXT("The exact compound vector deals 30 damage"), Result.Damage, 30);
		TestTraceValue(*this, TEXT("Base remains 90"), Result.Trace, EBattleDamageTraceStep::BaseDamage, 90);
		TestTraceValue(*this, TEXT("Spread tie rounds down to 67"), Result.Trace, EBattleDamageTraceStep::SpreadDamage, 67);
		TestTraceValue(*this, TEXT("Weather tie rounds down to 100"), Result.Trace, EBattleDamageTraceStep::WeatherDamage, 100);
		TestTraceValue(*this, TEXT("Critical floor produces 150"), Result.Trace, EBattleDamageTraceStep::CriticalDamage, 150);
		TestTraceValue(*this, TEXT("Raw 15 produces random damage 127"), Result.Trace, EBattleDamageTraceStep::RandomDamage, 127);
		TestTraceValue(*this, TEXT("STAB tie rounds down to 190"), Result.Trace, EBattleDamageTraceStep::StabDamage, 190);
		TestTraceValue(*this, TEXT("Dual half effectiveness floors to 47"), Result.Trace, EBattleDamageTraceStep::TypeDamage, 47);
		TestTraceValue(*this, TEXT("Burn floors 47 to 23"), Result.Trace, EBattleDamageTraceStep::BurnDamage, 23);
		TestTraceValue(*this, TEXT("Life Orb final damage is 30"), Result.Trace, EBattleDamageTraceStep::FinalDamage, 30);
		TestTraceValue(*this, TEXT("Critical ignores the screen modifier"), Result.Trace, EBattleDamageTraceStep::FinalModifierIgnored, 2048, &Screen.RuleId);
		TestTraceValue(*this, TEXT("Life Orb is the retained final chain"), Result.Trace, EBattleDamageTraceStep::FinalModifierChain, 5324, &LifeOrb.RuleId);

		Input = MakeNeutralDamageInput();
		Input.AttackerStats.Attack = 200;
		const FBattleDamageModifier Terrain = MakeModifier(TEXT("Condition.C05A.Terrain"), 5325);
		const FBattleDamageModifier Ability = MakeModifier(TEXT("Ability.C05A.Blaze"), 6144);
		const FBattleDamageModifier SnowDefense = MakeModifier(TEXT("Condition.C05A.SnowDefense"), 6144);
		Input.PowerModifiers = {Terrain};
		Input.OffensiveStatModifiers = {Ability};
		Input.DirectDefensiveStatModifiers = {SnowDefense};
		TestTrue(
			TEXT("Terrain, Ability, and direct weather-defense hooks resolve"),
			FBattleFinalDamageCalculator::TryCalculateFinalDamage(Input, Random, Result, Error));
		TestEqual(TEXT("The hook vector produces base and final damage 116"), Result.Damage, 116);
		TestTraceValue(*this, TEXT("Terrain changes power to 130"), Result.Trace, EBattleDamageTraceStep::EffectivePower, 130);
		TestTraceValue(*this, TEXT("Blaze changes offense to 300"), Result.Trace, EBattleDamageTraceStep::EffectiveOffensiveStat, 300);
		TestTraceValue(*this, TEXT("Snow changes defense to 150"), Result.Trace, EBattleDamageTraceStep::DirectDefensiveStat, 150, &SnowDefense.RuleId);

		Input = MakeNeutralDamageInput();
		Input.MovePower = 2048;
		Input.PowerModifiers = {Terrain};
		TestTrue(
			TEXT("The pinned base-power tie vector resolves"),
			FBattleFinalDamageCalculator::TryCalculateFinalDamage(Input, Random, Result, Error));
		TestTraceValue(
			*this,
			TEXT("PokeRound rounds exact 2662.5 down to 2662 power"),
			Result.Trace,
			EBattleDamageTraceStep::EffectivePower,
			2662);

		Input = MakeNeutralDamageInput();
		Input.AttackerStats.Attack = 200;
		FBattleTypeEffectiveness FourTimesEffectiveness;
		TestTrue(
			TEXT("Fire into Grass and Steel resolves through the type chart"),
			TypeChart.TryGetDualEffectiveness(
				EPokemonType::Fire,
				EPokemonType::Grass,
				EPokemonType::Steel,
				FourTimesEffectiveness));
		Input.TypeEffectiveness = FourTimesEffectiveness;
		TestTrue(TEXT("A dual-type four-times result resolves"), FBattleFinalDamageCalculator::TryCalculateFinalDamage(Input, Random, Result, Error));
		TestEqual(TEXT("Four-times effectiveness applies after random damage"), Result.Damage, 360);

		Input = MakeNeutralDamageInput();
		Input.AttackerStats.Attack = 200;
		Input.FinalModifiers = {MakeModifier(TEXT("Condition.C05A.DoubleScreen"), 2732)};
		TestTrue(TEXT("The Doubles screen hook resolves"), FBattleFinalDamageCalculator::TryCalculateFinalDamage(Input, Random, Result, Error));
		TestEqual(TEXT("The Doubles screen Q12 value reduces 90 to 60"), Result.Damage, 60);

		Input = MakeNeutralDamageInput();
		Input.AttackerStats.Attack = 200;
		const FBattleDamageModifier SingleScreen = MakeModifier(TEXT("Condition.C05A.SingleScreen"), 2048);
		Input.FinalModifiers = {SingleScreen, LifeOrb};
		TestTrue(TEXT("Ordered screen and Life Orb hooks resolve"), FBattleFinalDamageCalculator::TryCalculateFinalDamage(Input, Random, Result, Error));
		TestEqual(TEXT("The ordered final-modifier chain reduces 90 to 58"), Result.Damage, 58);
		TestTraceValue(*this, TEXT("Final modifier chaining rounds to 2662"), Result.Trace, EBattleDamageTraceStep::FinalModifierChain, 2662, &LifeOrb.RuleId);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC05ACriticalImmunityAndMinimumTest,
		"PokemonSolarus.Battle.C05A.CriticalImmunityAndMinimum",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

	bool FBattleC05ACriticalImmunityAndMinimumTest::RunTest(const FString& Parameters)
	{
		FSequenceBattleRandom Random({0, 0, 0, 15, 15, 0, 0});
		FBattleFinalDamageInput Input = MakeNeutralDamageInput();
		Input.AttackerStats.Attack = 200;
		Input.AttackerStages.ApplyChange(EBattleStat::Attack, -6);
		Input.DefenderStages.ApplyChange(EBattleStat::Defense, 6);
		Input.bCritical = true;
		Input.FinalModifiers = {MakeModifier(TEXT("Condition.C05A.Screen"), 2048, true)};
		FBattleFinalDamageResult Result;
		EBattleDamageCalculationError Error = EBattleDamageCalculationError::None;
		TestTrue(TEXT("Critical stage ignoring resolves"), FBattleFinalDamageCalculator::TryCalculateFinalDamage(Input, Random, Result, Error));
		TestEqual(TEXT("Critical ignores adverse stages and the screen"), Result.Damage, 135);
		TestTraceValue(*this, TEXT("Critical uses neutral offense stage"), Result.Trace, EBattleDamageTraceStep::OffensiveStageUsed, 0);
		TestTraceValue(*this, TEXT("Critical uses neutral defense stage"), Result.Trace, EBattleDamageTraceStep::DefensiveStageUsed, 0);

		Input.bCritical = false;
		TestTrue(TEXT("The non-critical comparison resolves"), FBattleFinalDamageCalculator::TryCalculateFinalDamage(Input, Random, Result, Error));
		TestEqual(TEXT("Non-critical keeps both adverse stages and the screen"), Result.Damage, 3);

		Input = MakeNeutralDamageInput();
		Input.AttackerStages.ApplyChange(EBattleStat::Attack, 2);
		Input.DefenderStages.ApplyChange(EBattleStat::Defense, -2);
		Input.bCritical = true;
		TestTrue(TEXT("A critical with favorable stages resolves"), FBattleFinalDamageCalculator::TryCalculateFinalDamage(Input, Random, Result, Error));
		TestEqual(TEXT("Critical preserves favorable offense and defense stages"), Result.Damage, 267);

		const int32 DrawsBeforeImmunity = Random.GetTrace().Num();
		Input = MakeNeutralDamageInput();
		Input.TypeEffectiveness.Numerator = 0;
		Input.RandomContext = FBattleRandomContext();
		bool bPreAccuracyNoEffect = false;
		TestTrue(
			TEXT("The pre-accuracy immunity seam resolves"),
			FBattleFinalDamageCalculator::TryResolvePreAccuracyNoEffect(
				Input,
				bPreAccuracyNoEffect,
				Result,
				Error));
		TestTrue(TEXT("Type immunity stops before accuracy"), bPreAccuracyNoEffect);
		TestTrue(TEXT("Type immunity is a successful no-effect result"), FBattleFinalDamageCalculator::TryCalculateFinalDamage(Input, Random, Result, Error));
		TestEqual(TEXT("Type immunity reports NoEffect"), Result.Outcome, EBattleDamageOutcome::NoEffect);
		TestEqual(TEXT("Type immunity is typed"), Result.NoEffectReason, EBattleDamageNoEffectReason::TypeImmunity);
		TestEqual(TEXT("Type immunity deals zero"), Result.Damage, 0);
		TestEqual(TEXT("Type immunity consumes no random draw"), Random.GetTrace().Num(), DrawsBeforeImmunity);

		Input = MakeNeutralDamageInput();
		Input.BlockingRuleId = MakeDefinitionId<FDefinitionId>(TEXT("Ability.C05A.Immunity"));
		Input.RandomContext = FBattleRandomContext();
		TestTrue(TEXT("A rule hook can produce typed no-effect"), FBattleFinalDamageCalculator::TryCalculateFinalDamage(Input, Random, Result, Error));
		TestEqual(TEXT("The hook no-effect reason is typed"), Result.NoEffectReason, EBattleDamageNoEffectReason::RuleHook);
		TestTrue(TEXT("The blocking rule identity is retained"), Result.NoEffectRuleId == Input.BlockingRuleId);
		TestEqual(TEXT("Rule-hook immunity also consumes no draw"), Random.GetTrace().Num(), DrawsBeforeImmunity);

		Input = MakeNeutralDamageInput();
		Input.MovePower = 50;
		Input.TypeEffectiveness.Numerator = 0;
		Input.bBypassTypeImmunity = true;
		TestTrue(TEXT("Typeless damage bypasses ordinary type immunity"), FBattleFinalDamageCalculator::TryCalculateFinalDamage(Input, Random, Result, Error));
		TestEqual(TEXT("Typeless Power 50 reaches neutral damage"), Result.Damage, 20);
		TestEqual(TEXT("Typeless bypass reaches the random stage"), Random.GetTrace().Num(), DrawsBeforeImmunity + 1);

		Input = MakeNeutralDamageInput();
		Input.AttackerLevel = 1;
		Input.MovePower = 1;
		Input.AttackerStats.Attack = 1;
		Input.DefenderStats.Defense = TNumericLimits<int32>::Max();
		Input.bAttackerBurned = true;
		TestTrue(TEXT("The minimum-damage vector resolves"), FBattleFinalDamageCalculator::TryCalculateFinalDamage(Input, Random, Result, Error));
		TestEqual(TEXT("A reached non-immune hit retains minimum one"), Result.Damage, 1);

		Input = MakeNeutralDamageInput();
		Input.MovePower = 17;
		Input.bAttackerBurned = true;
		TestTrue(TEXT("Ordinary physical burn resolves"), FBattleFinalDamageCalculator::TryCalculateFinalDamage(Input, Random, Result, Error));
		TestEqual(TEXT("Burn floors base damage nine to four"), Result.Damage, 4);
		Input.bBypassBurnPenalty = true;
		TestTrue(TEXT("The explicit burn exception resolves"), FBattleFinalDamageCalculator::TryCalculateFinalDamage(Input, Random, Result, Error));
		TestEqual(TEXT("The burn exception preserves base damage nine"), Result.Damage, 9);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC05AOverflowAndRejectionTest,
		"PokemonSolarus.Battle.C05A.OverflowAndRejection",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

	bool FBattleC05AOverflowAndRejectionTest::RunTest(const FString& Parameters)
	{
		FSequenceBattleRandom Random({0, 0, 0});
		FBattleFinalDamageResult Result;
		EBattleDamageCalculationError Error = EBattleDamageCalculationError::None;

		FBattleFinalDamageInput Input = MakeNeutralDamageInput();
		Input.TypeEffectiveness.Denominator = 0;
		TestFalse(TEXT("A zero effectiveness denominator is rejected"), FBattleFinalDamageCalculator::TryCalculateFinalDamage(Input, Random, Result, Error));
		TestEqual(TEXT("Invalid effectiveness is typed"), Error, EBattleDamageCalculationError::InvalidTypeEffectiveness);
		TestTrue(TEXT("Invalid effectiveness consumes no draw"), Random.GetTrace().IsEmpty());

		Input = MakeNeutralDamageInput();
		FBattleDamageModifier InvalidModifier;
		InvalidModifier.ModifierQ12 = 6144;
		Input.PowerModifiers = {InvalidModifier};
		TestFalse(TEXT("An unnamed modifier is rejected"), FBattleFinalDamageCalculator::TryCalculateFinalDamage(Input, Random, Result, Error));
		TestEqual(TEXT("Invalid modifier is typed"), Error, EBattleDamageCalculationError::InvalidModifier);
		TestTrue(TEXT("Invalid modifiers consume no draw"), Random.GetTrace().IsEmpty());

		Input = MakeNeutralDamageInput();
		for (int32 ModifierIndex = 0; ModifierIndex < 11; ++ModifierIndex)
		{
			Input.FinalModifiers.Add(MakeModifier(
				*FString::Printf(TEXT("Rule.C05A.OverflowingFinalChain.%d"), ModifierIndex),
				131072));
		}
		TestFalse(
			TEXT("A host-overflowing final modifier chain is rejected"),
			FBattleFinalDamageCalculator::TryCalculateFinalDamage(Input, Random, Result, Error));
		TestEqual(TEXT("Final-chain host overflow is typed"), Error, EBattleDamageCalculationError::ArithmeticOverflow);
		TestTrue(TEXT("Invalid final chains consume no draw"), Random.GetTrace().IsEmpty());

		Input = MakeNeutralDamageInput();
		Input.AttackerLevel = 100;
		Input.MovePower = TNumericLimits<int32>::Max();
		Input.AttackerStats.Attack = 2;
		Input.DefenderStats.Defense = 1;
		TestFalse(
			TEXT("A live-base result above int32 is rejected"),
			FBattleFinalDamageCalculator::TryCalculateFinalDamage(Input, Random, Result, Error));
		TestEqual(TEXT("Base output overflow is typed"), Error, EBattleDamageCalculationError::ArithmeticOverflow);
		TestTrue(TEXT("Base output overflow consumes no draw"), Random.GetTrace().IsEmpty());

		Input = MakeNeutralDamageInput();
		Input.AttackerLevel = 100;
		Input.MovePower = TNumericLimits<int32>::Max();
		Input.AttackerStats.Attack = 102261126;
		Input.DefenderStats.Defense = TNumericLimits<int32>::Max();
		TestTrue(TEXT("The last fitting live-base multiplication resolves"), FBattleFinalDamageCalculator::TryCalculateFinalDamage(Input, Random, Result, Error));
		TestTraceValue(*this, TEXT("The fitting live base is 85899347"), Result.Trace, EBattleDamageTraceStep::BaseDamage, 85899347);
		TestEqual(TEXT("The documented random OF32 reduction is retained"), Result.Damage, 1);

		Input.AttackerStats.Attack = 102261127;
		TestFalse(TEXT("The adjacent int64 base multiplication overflow is rejected"), FBattleFinalDamageCalculator::TryCalculateFinalDamage(Input, Random, Result, Error));
		TestEqual(TEXT("Host arithmetic overflow is typed"), Error, EBattleDamageCalculationError::ArithmeticOverflow);
		TestEqual(TEXT("Base overflow happens before another random draw"), Random.GetTrace().Num(), 1);

		Input = MakeNeutralDamageInput();
		Input.AttackerLevel = 100;
		Input.MovePower = 100;
		Input.AttackerStats.Attack = 357;
		Input.DefenderStats.Defense = 1;
		Input.FinalModifiers = {MakeModifier(TEXT("Rule.C05A.MaximumFinalModifier"), 131072)};
		TestTrue(TEXT("The documented final OF16 reduction resolves"), FBattleFinalDamageCalculator::TryCalculateFinalDamage(Input, Random, Result, Error));
		TestEqual(TEXT("OF16 reduces 959680 to 42176"), Result.Damage, 42176);
		TestEqual(TEXT("The final reduction consumes its reached random draw"), Random.GetTrace().Num(), 2);

		Input = MakeNeutralDamageInput();
		Input.AttackerLevel = 1;
		Input.MovePower = 1;
		Input.AttackerStats.Attack = 51150;
		Input.DefenderStats.Defense = 1;
		Input.FinalModifiers = {MakeModifier(TEXT("Rule.C05A.MaximumFinalModifier.ZeroBoundary"), 131072)};
		TestTrue(TEXT("The exact OF16 zero boundary resolves"), FBattleFinalDamageCalculator::TryCalculateFinalDamage(Input, Random, Result, Error));
		TestEqual(TEXT("The documented minimum is applied before OF16 returns zero"), Result.Damage, 0);
		TestEqual(TEXT("The OF16 zero boundary consumes its reached random draw"), Random.GetTrace().Num(), 3);

		Input = MakeNeutralDamageInput();
		Input.RandomContext.ActionId = FActionId();
		TestFalse(
			TEXT("Final damage requires an action-scoped random context"),
			FBattleFinalDamageCalculator::TryCalculateFinalDamage(Input, Random, Result, Error));
		TestEqual(TEXT("Invalid damage context is typed"), Error, EBattleDamageCalculationError::InvalidRandomContext);
		TestEqual(TEXT("Invalid damage context consumes no draw"), Random.GetTrace().Num(), 3);

		FSequenceBattleRandom EmptyRandom({});
		Input = MakeNeutralDamageInput();
		TestFalse(TEXT("An RNG failure rejects final damage"), FBattleFinalDamageCalculator::TryCalculateFinalDamage(Input, EmptyRandom, Result, Error));
		TestEqual(TEXT("RNG failure is typed"), Error, EBattleDamageCalculationError::RandomFailure);
		TestTrue(TEXT("A failed RNG call adds no trace"), EmptyRandom.GetTrace().IsEmpty());
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC05ARngOrderTest,
		"PokemonSolarus.Battle.C05A.RngOrder",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

	bool FBattleC05ARngOrderTest::RunTest(const FString& Parameters)
	{
		FSequenceBattleRandom Random({99, 0, 15});
		FBattleAccuracyCheckInput AccuracyInput;
		AccuracyInput.BaseAccuracy = 100;
		AccuracyInput.RandomContext = MakeRandomContext(TEXT("Rule.C05A.Order.Accuracy"));
		FBattleAccuracyCheckResult AccuracyResult;
		EBattleHitResolverError HitError = EBattleHitResolverError::None;
		TestTrue(TEXT("Ordered accuracy resolves"), FBattleHitResolver::TryResolveAccuracy(AccuracyInput, Random, AccuracyResult, HitError));
		TestEqual(TEXT("Ordered accuracy hits"), AccuracyResult.Outcome, EBattleAccuracyCheckOutcome::Hit);

		FBattleCriticalCheckInput CriticalInput;
		CriticalInput.BaseStage = 1;
		CriticalInput.RandomContext = MakeRandomContext(TEXT("Rule.C05A.Order.Critical"));
		FBattleCriticalCheckResult CriticalResult;
		TestTrue(TEXT("Ordered critical resolves"), FBattleHitResolver::TryResolveCritical(CriticalInput, Random, CriticalResult, HitError));
		TestEqual(TEXT("Ordered critical succeeds"), CriticalResult.Outcome, EBattleCriticalCheckOutcome::Critical);

		FBattleFinalDamageInput DamageInput = MakeNeutralDamageInput();
		DamageInput.AttackerStats.Attack = 200;
		DamageInput.RandomContext = MakeRandomContext(TEXT("Rule.C05A.Order.Damage"));
		FBattleFinalDamageResult DamageResult;
		EBattleDamageCalculationError DamageError = EBattleDamageCalculationError::None;
		TestTrue(TEXT("Ordered damage resolves"), FBattleFinalDamageCalculator::TryCalculateFinalDamage(DamageInput, Random, DamageResult, DamageError));
		TestEqual(TEXT("Raw 15 produces 76 from base 90"), DamageResult.Damage, 76);

		TestEqual(TEXT("Hit, critical, and damage consume exactly three draws"), Random.GetTrace().Num(), 3);
		TestEqual(TEXT("Accuracy is first and uses U[0,99]"), Random.GetTrace()[0].InclusiveMaximum, 99U);
		TestEqual(TEXT("Critical is second and uses U[0,23]"), Random.GetTrace()[1].InclusiveMaximum, 23U);
		TestEqual(TEXT("Damage is third and uses U[0,15]"), Random.GetTrace()[2].InclusiveMaximum, 15U);
		TestTrue(TEXT("Accuracy purpose remains first"), Random.GetTrace()[0].RulePurpose == AccuracyInput.RandomContext.RulePurpose);
		TestTrue(TEXT("Critical purpose remains second"), Random.GetTrace()[1].RulePurpose == CriticalInput.RandomContext.RulePurpose);
		TestTrue(TEXT("Damage purpose remains third"), Random.GetTrace()[2].RulePurpose == DamageInput.RandomContext.RulePurpose);

		FSequenceBattleRandom MissRandom({50});
		AccuracyInput.BaseAccuracy = 50;
		TestTrue(TEXT("A miss check resolves"), FBattleHitResolver::TryResolveAccuracy(AccuracyInput, MissRandom, AccuracyResult, HitError));
		TestEqual(TEXT("Roll 50 misses accuracy 50"), AccuracyResult.Outcome, EBattleAccuracyCheckOutcome::Miss);
		TestEqual(TEXT("Stopping after the miss leaves only one draw"), MissRandom.GetTrace().Num(), 1);
		return true;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
