#if WITH_DEV_AUTOMATION_TESTS

#include "Battle/BattleDataTableRuntimeSource.h"

#include "Battle/BattleDataTableAdapter.h"
#include "Battle/BattleReplay.h"
#include "Battle/BattleRuntimeDataTableRows.h"
#include "Engine/DataTable.h"
#include "Misc/AutomationTest.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UObjectGlobals.h"

namespace BattleRuntimeDataSourceTests
{
	const TCHAR* RuntimeTablePath =
		TEXT("/Game/Data/Battle/Initial/DT_BattleRuntimeScenario.DT_BattleRuntimeScenario");
	const TCHAR* WrongRuntimeTablePath =
		TEXT("/Game/Data/Battle/Initial/DT_InitialBattleSpeciesForms.DT_InitialBattleSpeciesForms");
	constexpr uint64 ExpectedFirstRandomRawValue = 15634621701698845082ULL;

	template <typename IdType>
	IdType MakeNumericId(const uint64 Value)
	{
		IdType Id;
		check(IdType::TryCreate(Value, Id));
		return Id;
	}

	template <typename IdType>
	IdType MakeDefinitionId(const TCHAR* Value)
	{
		IdType Id;
		check(IdType::TryCreate(FName(Value), Id));
		return Id;
	}

	FBattleDataTableRuntimeSource MakeProductionSource()
	{
		return FBattleDataTableRuntimeSource(
			TSoftObjectPtr<UDataTable>(FSoftObjectPath(RuntimeTablePath)));
	}

	bool TryLoadProductionCatalog(
		FBattleDefinitionCatalog& OutCatalog,
		FString& OutError)
	{
		OutCatalog = FBattleDefinitionCatalog();
		OutError.Reset();
		UDataTable* RuntimeTable = LoadObject<UDataTable>(nullptr, RuntimeTablePath);
		if (RuntimeTable == nullptr
			|| RuntimeTable->GetRowStruct() != FBattleRuntimeScenarioTableRow::StaticStruct())
		{
			OutError = TEXT("The production runtime table is missing or has the wrong row type.");
			return false;
		}
		const FBattleRuntimeScenarioTableRow* Scenario =
			RuntimeTable->FindRow<FBattleRuntimeScenarioTableRow>(
				FName(TEXT("InitialBattle")),
				TEXT("Battle runtime catalog equivalence test"),
				false);
		if (Scenario == nullptr)
		{
			OutError = TEXT("The production InitialBattle row is missing.");
			return false;
		}

		FBattleDataTableSet Tables;
		Tables.SpeciesForms = Scenario->SpeciesForms.LoadSynchronous();
		Tables.Natures = Scenario->Natures.LoadSynchronous();
		Tables.Moves = Scenario->Moves.LoadSynchronous();
		Tables.Abilities = Scenario->Abilities.LoadSynchronous();
		Tables.Items = Scenario->Items.LoadSynchronous();
		Tables.Conditions = Scenario->Conditions.LoadSynchronous();
		Tables.TypeChart = Scenario->TypeChart.LoadSynchronous();
		TArray<FBattleCatalogDiagnostic> Diagnostics;
		if (!FBattleDataTableAdapter::BuildCatalog(Tables, OutCatalog, Diagnostics))
		{
			OutError = FString::Printf(
				TEXT("The production catalog produced %d diagnostic(s)."),
				Diagnostics.Num());
			return false;
		}
		return true;
	}

	bool TryMakeFirstFightDecision(
		const FBattleDecisionRequest& Request,
		FBattleDecision& OutDecision)
	{
		const TConstArrayView<FBattleMoveTargetOption> Options = Request.GetLegalMoveTargets();
		if (Options.IsEmpty())
		{
			return false;
		}
		return FBattleDecision::TryCreateFight(
			Request.GetStateVersion(),
			Request.GetDecisionOwnerTrainerId(),
			Request.GetActingBattlerId(),
			Options[0].MoveId,
			Options[0].ActiveSlotId,
			OutDecision);
	}

	bool TryLockInitialFightTurn(FBattleEngine& Engine)
	{
		FBattleRejection Rejection;
		if (!Engine.TryBeginActionDecisionSequence(Rejection))
		{
			return false;
		}
		for (int32 DecisionIndex = 0; DecisionIndex < 2; ++DecisionIndex)
		{
			const TArray<FBattleDecisionRequest> Requests = Engine.GetPendingDecisionRequests();
			FBattleDecision Decision;
			if (Requests.Num() != 1
				|| !TryMakeFirstFightDecision(Requests[0], Decision)
				|| !Engine.SubmitDecision(Decision).WasAccepted())
			{
				return false;
			}
		}
		return Engine.GetSnapshot().GetPhase() == EBattlePhase::Locked;
	}

	bool TryConsumeFirstMoveRandomness(FBattleEngine& Engine)
	{
		return TryLockInitialFightTurn(Engine)
			&& Engine.BeginNextLockedAction().WasAccepted()
			&& Engine.CommitCurrentMoveAfterPreMoveGates().WasAccepted()
			&& Engine.ResolveCurrentMoveTargets().WasAccepted()
			&& Engine.ExecuteCurrentMoveEffects().WasAccepted();
	}

	void TestPermanentStats(
		FAutomationTestBase& Test,
		const FString& Prefix,
		const FBattlePartyEntrySetup& Entry,
		const FPokemonBattleStats& Expected)
	{
		Test.TestEqual(Prefix + TEXT(" MaxHP"), Entry.Stats.MaxHP, Expected.MaxHP);
		Test.TestEqual(Prefix + TEXT(" Attack"), Entry.Stats.Attack, Expected.Attack);
		Test.TestEqual(Prefix + TEXT(" Defense"), Entry.Stats.Defense, Expected.Defense);
		Test.TestEqual(
			Prefix + TEXT(" SpecialAttack"),
			Entry.Stats.SpecialAttack,
			Expected.SpecialAttack);
		Test.TestEqual(
			Prefix + TEXT(" SpecialDefense"),
			Entry.Stats.SpecialDefense,
			Expected.SpecialDefense);
		Test.TestEqual(Prefix + TEXT(" Speed"), Entry.Stats.Speed, Expected.Speed);
		Test.TestEqual(Prefix + TEXT(" starts at full HP"), Entry.CurrentHP, Expected.MaxHP);
	}

	void TestMove(
		FAutomationTestBase& Test,
		const FString& Prefix,
		const FBattlePartyEntrySetup& Entry,
		const FMoveId ExpectedMoveId,
		const int32 ExpectedPP)
	{
		Test.TestEqual(Prefix + TEXT(" has one move"), Entry.Moves.Num(), 1);
		if (Entry.Moves.Num() != 1)
		{
			return;
		}
		Test.TestEqual(Prefix + TEXT(" move identity"), Entry.Moves[0].MoveId, ExpectedMoveId);
		Test.TestEqual(Prefix + TEXT(" full current PP"), Entry.Moves[0].CurrentPP, ExpectedPP);
		Test.TestEqual(Prefix + TEXT(" catalog maximum PP"), Entry.Moves[0].MaxPP, ExpectedPP);
	}

	void TestPolicies(FAutomationTestBase& Test, const FBattleSetup& Setup)
	{
		const FBattleEncounterPolicies& Policies = Setup.GetPolicies();
		Test.TestFalse(TEXT("Trainer battle disallows Run"), Policies.bRunAllowed);
		Test.TestFalse(TEXT("Trainer battle disallows capture"), Policies.bCaptureAllowed);
		Test.TestTrue(TEXT("Trainer battle allows the Bag policy"), Policies.bBagAllowed);
		Test.TestFalse(TEXT("Initial battle disables Shift prompts"), Policies.bShiftPromptEligible);
		Test.TestEqual(
			TEXT("Initial battle disables wild fleeing"),
			Policies.WildFleeMode,
			EBattleWildFleeMode::Disabled);
		Test.TestEqual(TEXT("Disabled flee numerator is zero"), Policies.WildFleeNumerator, 0U);
		Test.TestEqual(TEXT("Disabled flee denominator is zero"), Policies.WildFleeDenominator, 0U);
	}

	void TestBaseStats(
		FAutomationTestBase& Test,
		const FString& Prefix,
		const FPokemonStatValues& Actual,
		const FPokemonStatValues& Expected)
	{
		Test.TestEqual(Prefix + TEXT(" base HP"), Actual.HP, Expected.HP);
		Test.TestEqual(Prefix + TEXT(" base Attack"), Actual.Attack, Expected.Attack);
		Test.TestEqual(Prefix + TEXT(" base Defense"), Actual.Defense, Expected.Defense);
		Test.TestEqual(Prefix + TEXT(" base Special Attack"),
			Actual.SpecialAttack, Expected.SpecialAttack);
		Test.TestEqual(Prefix + TEXT(" base Special Defense"),
			Actual.SpecialDefense, Expected.SpecialDefense);
		Test.TestEqual(Prefix + TEXT(" base Speed"), Actual.Speed, Expected.Speed);
	}

	void TestSpecies(
		FAutomationTestBase& Test,
		const FBattleDefinitionCatalog& Catalog,
		const TCHAR* SpeciesName,
		const EPokemonType PrimaryType,
		const EPokemonType SecondaryType,
		const FPokemonStatValues& ExpectedStats,
		const TCHAR* AbilityName)
	{
		const FString Prefix(SpeciesName);
		const FBattleSpeciesFormDefinition* Species = Catalog.FindSpeciesForm(
			MakeDefinitionId<FSpeciesFormId>(SpeciesName));
		if (!Test.TestNotNull(Prefix + TEXT(" definition exists"), Species))
		{
			return;
		}
		Test.TestEqual(Prefix + TEXT(" primary type"), Species->PrimaryType, PrimaryType);
		Test.TestEqual(Prefix + TEXT(" secondary type"), Species->SecondaryType, SecondaryType);
		TestBaseStats(Test, Prefix, Species->BaseStats, ExpectedStats);
		Test.TestEqual(Prefix + TEXT(" catch rate"), Species->CatchRate, 45);
		Test.TestEqual(Prefix + TEXT(" has one Ability choice"), Species->AbilityChoices.Num(), 1);
		if (Species->AbilityChoices.Num() == 1)
		{
			Test.TestEqual(Prefix + TEXT(" Ability choice"),
				Species->AbilityChoices[0], MakeDefinitionId<FAbilityId>(AbilityName));
		}
	}

	void TestAbilities(FAutomationTestBase& Test, const FBattleDefinitionCatalog& Catalog)
	{
		Test.TestEqual(TEXT("The approved catalog has two Abilities"), Catalog.GetAbilities().Num(), 2);
		Test.TestNotNull(TEXT("Blaze exists"),
			Catalog.FindAbility(MakeDefinitionId<FAbilityId>(TEXT("Ability.Blaze"))));
		Test.TestNotNull(TEXT("Overgrow exists"),
			Catalog.FindAbility(MakeDefinitionId<FAbilityId>(TEXT("Ability.Overgrow"))));
	}

	void TestMoveCore(
		FAutomationTestBase& Test,
		const FString& Prefix,
		const FBattleMoveDefinition& Move,
		const EPokemonType Type,
		const EBattleMoveCategory Category,
		const int32 Power,
		const int32 BasePP,
		const EBattleMoveFlags Flags)
	{
		Test.TestEqual(Prefix + TEXT(" type"), Move.Type, Type);
		Test.TestEqual(Prefix + TEXT(" category"), Move.Category, Category);
		Test.TestEqual(Prefix + TEXT(" power"), Move.Power, Power);
		Test.TestFalse(Prefix + TEXT(" uses ordinary accuracy"), Move.bAlwaysHits);
		Test.TestEqual(Prefix + TEXT(" accuracy"), Move.Accuracy, 100);
		Test.TestTrue(Prefix + TEXT(" uses PP"), Move.bUsesPP);
		Test.TestEqual(Prefix + TEXT(" base PP"), Move.BasePP, BasePP);
		Test.TestTrue(Prefix + TEXT(" permits catalog PP boosts"), Move.bAllowsPPBoosts);
		Test.TestEqual(Prefix + TEXT(" priority"), Move.Priority, 0);
		Test.TestEqual(Prefix + TEXT(" target class"),
			Move.TargetClass, EBattleTargetClass::SelectedOpponent);
		Test.TestTrue(Prefix + TEXT(" flags"), Move.Flags == Flags);
	}

	void TestFlamethrower(FAutomationTestBase& Test, const FBattleDefinitionCatalog& Catalog)
	{
		const FBattleMoveDefinition* Move =
			Catalog.FindMove(MakeDefinitionId<FMoveId>(TEXT("Move.Flamethrower")));
		if (!Test.TestNotNull(TEXT("Flamethrower definition exists"), Move))
		{
			return;
		}
		TestMoveCore(Test, TEXT("Flamethrower"), *Move, EPokemonType::Fire,
			EBattleMoveCategory::Special, 90, 15, EBattleMoveFlags::BlockedByProtect);
		Test.TestEqual(TEXT("Flamethrower has two ordered effects"), Move->Effects.Num(), 2);
		if (Move->Effects.Num() != 2)
		{
			return;
		}
		Test.TestEqual(TEXT("Flamethrower effect 0 order"), Move->Effects[0].Order, 0);
		Test.TestEqual(TEXT("Flamethrower effect 0 kind"),
			Move->Effects[0].Kind, EBattleMoveEffectKind::Damage);
		Test.TestEqual(TEXT("Flamethrower effect 0 target"),
			Move->Effects[0].Target, EBattleEffectTarget::ResolvedTarget);
		Test.TestEqual(TEXT("Flamethrower damage numerator"),
			Move->Effects[0].ChanceNumerator, 1);
		Test.TestEqual(TEXT("Flamethrower damage denominator"),
			Move->Effects[0].ChanceDenominator, 1);
		Test.TestEqual(TEXT("Flamethrower effect 1 order"), Move->Effects[1].Order, 1);
		Test.TestEqual(TEXT("Flamethrower effect 1 kind"),
			Move->Effects[1].Kind, EBattleMoveEffectKind::ApplyCondition);
		Test.TestEqual(TEXT("Flamethrower effect 1 target"),
			Move->Effects[1].Target, EBattleEffectTarget::ResolvedTarget);
		Test.TestEqual(TEXT("Flamethrower Burn identity"), Move->Effects[1].ConditionId,
			MakeDefinitionId<FConditionId>(TEXT("Condition.Burn")));
		Test.TestEqual(TEXT("Flamethrower Burn numerator"), Move->Effects[1].ChanceNumerator, 10);
		Test.TestEqual(TEXT("Flamethrower Burn denominator"), Move->Effects[1].ChanceDenominator, 100);
	}

	void TestVineWhip(FAutomationTestBase& Test, const FBattleDefinitionCatalog& Catalog)
	{
		const FBattleMoveDefinition* Move =
			Catalog.FindMove(MakeDefinitionId<FMoveId>(TEXT("Move.VineWhip")));
		if (!Test.TestNotNull(TEXT("Vine Whip definition exists"), Move))
		{
			return;
		}
		const EBattleMoveFlags Flags =
			EBattleMoveFlags::MakesContact | EBattleMoveFlags::BlockedByProtect;
		TestMoveCore(Test, TEXT("Vine Whip"), *Move, EPokemonType::Grass,
			EBattleMoveCategory::Physical, 45, 25, Flags);
		Test.TestEqual(TEXT("Vine Whip has one effect"), Move->Effects.Num(), 1);
		if (Move->Effects.Num() == 1)
		{
			Test.TestEqual(TEXT("Vine Whip damage order"), Move->Effects[0].Order, 0);
			Test.TestEqual(TEXT("Vine Whip damage kind"),
				Move->Effects[0].Kind, EBattleMoveEffectKind::Damage);
			Test.TestEqual(TEXT("Vine Whip damage target"),
				Move->Effects[0].Target, EBattleEffectTarget::ResolvedTarget);
			Test.TestEqual(TEXT("Vine Whip damage numerator"),
				Move->Effects[0].ChanceNumerator, 1);
			Test.TestEqual(TEXT("Vine Whip damage denominator"),
				Move->Effects[0].ChanceDenominator, 1);
		}
	}

	void TestEffectiveness(
		FAutomationTestBase& Test,
		const FBattleTypeChart& Chart,
		const TCHAR* Label,
		const EPokemonType Attacking,
		const EPokemonType Defending,
		const int32 Numerator,
		const int32 Denominator)
	{
		FBattleTypeEffectiveness Value;
		if (Test.TestTrue(Label, Chart.TryGetEffectiveness(Attacking, Defending, Value)))
		{
			Test.TestEqual(FString(Label) + TEXT(" numerator"), Value.Numerator, Numerator);
			Test.TestEqual(FString(Label) + TEXT(" denominator"), Value.Denominator, Denominator);
		}
	}

	void TestDualEffectiveness(
		FAutomationTestBase& Test,
		const FBattleTypeChart& Chart,
		const TCHAR* Label,
		const EPokemonType Attacking,
		const EPokemonType FirstDefending,
		const EPokemonType SecondDefending,
		const int32 Numerator,
		const int32 Denominator)
	{
		FBattleTypeEffectiveness Value;
		if (Test.TestTrue(Label, Chart.TryGetDualEffectiveness(
			Attacking, FirstDefending, SecondDefending, Value)))
		{
			Test.TestEqual(FString(Label) + TEXT(" numerator"), Value.Numerator, Numerator);
			Test.TestEqual(FString(Label) + TEXT(" denominator"), Value.Denominator, Denominator);
		}
	}

	void TestTypeChart(FAutomationTestBase& Test, const FBattleTypeChart& Chart)
	{
		Test.TestTrue(TEXT("The imported 18x18 type chart is valid"), Chart.IsValid());
		TestEffectiveness(Test, Chart, TEXT("Fire is super effective against Grass"),
			EPokemonType::Fire, EPokemonType::Grass, 2, 1);
		TestEffectiveness(Test, Chart, TEXT("Fire is neutral against Poison"),
			EPokemonType::Fire, EPokemonType::Poison, 1, 1);
		TestDualEffectiveness(Test, Chart, TEXT("Fire versus Venusaur is 2x"),
			EPokemonType::Fire, EPokemonType::Grass, EPokemonType::Poison, 2, 1);
		TestEffectiveness(Test, Chart, TEXT("Grass is resisted by Fire"),
			EPokemonType::Grass, EPokemonType::Fire, 1, 2);
		TestEffectiveness(Test, Chart, TEXT("Grass is resisted by Flying"),
			EPokemonType::Grass, EPokemonType::Flying, 1, 2);
		TestDualEffectiveness(Test, Chart, TEXT("Grass versus Charizard is 0.25x"),
			EPokemonType::Grass, EPokemonType::Fire, EPokemonType::Flying, 1, 4);
		TestEffectiveness(Test, Chart, TEXT("Normal cannot hit Ghost"),
			EPokemonType::Normal, EPokemonType::Ghost, 0, 1);
		TestEffectiveness(Test, Chart, TEXT("Electric cannot hit Ground"),
			EPokemonType::Electric, EPokemonType::Ground, 0, 1);
		TestEffectiveness(Test, Chart, TEXT("Fairy is super effective against Dragon"),
			EPokemonType::Fairy, EPokemonType::Dragon, 2, 1);
	}
}

using namespace BattleRuntimeDataSourceTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleRuntimeProductionDataSourceTest,
	"PokemonSolarus.Battle.Runtime.DataSource.ProductionAssets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleRuntimeProductionDataSourceTest::RunTest(const FString& Parameters)
{
	FBattleRuntimeBundle Bundle;
	FString Error;
	FBattleDataTableRuntimeSource Source = MakeProductionSource();
	if (!TestTrue(TEXT("Production Data Tables create the runtime bundle"),
		Source.TryCreateInitialBattle(Bundle, Error)))
	{
		AddError(Error);
		return false;
	}

	TestTrue(TEXT("The returned bundle is complete"), Bundle.IsValid());
	TestEqual(TEXT("The local Trainer identity is data-driven"),
		Bundle.LocalTrainerId, MakeNumericId<FTrainerId>(1));
	const FBattleSnapshot Snapshot = Bundle.Engine->GetSnapshot();
	TestTrue(TEXT("The engine exposes a valid setup snapshot"), Snapshot.IsValid());
	TestEqual(TEXT("The Battle identity is data-driven"),
		Snapshot.GetBattleId(), MakeNumericId<FBattleId>(1001));
	TestEqual(TEXT("The encounter is a Trainer battle"),
		Snapshot.GetEncounterKind(), EBattleEncounterKind::Trainer);
	TestEqual(TEXT("The format is Single"), Snapshot.GetFormat(), EBattleFormat::Single);

	const FBattlePartyEntrySetup* Charizard =
		Snapshot.FindBattler(MakeNumericId<FBattlerId>(11));
	const FBattlePartyEntrySetup* Venusaur =
		Snapshot.FindBattler(MakeNumericId<FBattlerId>(21));
	if (!TestNotNull(TEXT("Charizard is present"), Charizard)
		|| !TestNotNull(TEXT("Venusaur is present"), Venusaur))
	{
		return false;
	}
	TestPermanentStats(*this, TEXT("Charizard"), *Charizard, {153, 104, 98, 129, 105, 120});
	TestPermanentStats(*this, TEXT("Venusaur"), *Venusaur, {155, 102, 103, 120, 120, 100});
	TestMove(*this, TEXT("Charizard"), *Charizard,
		MakeDefinitionId<FMoveId>(TEXT("Move.Flamethrower")), 15);
	TestMove(*this, TEXT("Venusaur"), *Venusaur,
		MakeDefinitionId<FMoveId>(TEXT("Move.VineWhip")), 25);

	FText DisplayName;
	TestTrue(TEXT("Charizard display name resolves"),
		Bundle.DisplayNames->TryResolveSpeciesName(
			MakeDefinitionId<FSpeciesFormId>(TEXT("Species.Charizard")), DisplayName));
	TestEqual(TEXT("Charizard display text is copied"), DisplayName.ToString(), FString(TEXT("Charizard")));
	TestTrue(TEXT("Venusaur display name resolves"),
		Bundle.DisplayNames->TryResolveSpeciesName(
			MakeDefinitionId<FSpeciesFormId>(TEXT("Species.Venusaur")), DisplayName));
	TestEqual(TEXT("Venusaur display text is copied"), DisplayName.ToString(), FString(TEXT("Venusaur")));

	TestPolicies(*this, Bundle.Engine->ExportReplayInputs().Setup);
	TestTrue(TEXT("The first move consumes deterministic seeded randomness"),
		TryConsumeFirstMoveRandomness(*Bundle.Engine));
	const TArray<FBattleRandomDraw> Trace = Bundle.Engine->ExportRandomTrace();
	TestTrue(TEXT("The seeded engine records at least one random draw"), !Trace.IsEmpty());
	if (!Trace.IsEmpty())
	{
		TestEqual(TEXT("The decimal-authored seed drives SplitMix64"),
			Trace[0].RawValue, ExpectedFirstRandomRawValue);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleRuntimeProductionCatalogEquivalenceTest,
	"PokemonSolarus.Battle.Runtime.DataSource.ProductionCatalogEquivalence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleRuntimeProductionCatalogEquivalenceTest::RunTest(const FString& Parameters)
{
	FBattleDefinitionCatalog Catalog;
	FString Error;
	if (!TestTrue(TEXT("The scenario references build the production catalog"),
		TryLoadProductionCatalog(Catalog, Error)))
	{
		AddError(Error);
		return false;
	}

	TestTrue(TEXT("The imported catalog is valid"), Catalog.IsValid());
	TestEqual(TEXT("The approved catalog has two species"), Catalog.GetSpeciesForms().Num(), 2);
	TestEqual(TEXT("The approved catalog has two moves"), Catalog.GetMoves().Num(), 2);
	TestEqual(TEXT("The approved catalog has one neutral nature"), Catalog.GetNatures().Num(), 1);
	TestEqual(TEXT("The approved catalog has no item definitions"), Catalog.GetItems().Num(), 0);
	TestEqual(TEXT("The approved catalog has one condition"), Catalog.GetConditions().Num(), 1);
	TestAbilities(*this, Catalog);
	TestSpecies(*this, Catalog, TEXT("Species.Charizard"),
		EPokemonType::Fire, EPokemonType::Flying, {78, 84, 78, 109, 85, 100},
		TEXT("Ability.Blaze"));
	TestSpecies(*this, Catalog, TEXT("Species.Venusaur"),
		EPokemonType::Grass, EPokemonType::Poison, {80, 82, 83, 100, 100, 80},
		TEXT("Ability.Overgrow"));

	const FBattleNatureDefinition* Hardy =
		Catalog.FindNature(MakeDefinitionId<FNatureId>(TEXT("Nature.Hardy")));
	if (TestNotNull(TEXT("Hardy nature exists"), Hardy))
	{
		TestTrue(TEXT("Hardy is neutral"), Hardy->Modifier.IsNeutral());
	}
	const FBattleConditionDefinition* Burn =
		Catalog.FindCondition(MakeDefinitionId<FConditionId>(TEXT("Condition.Burn")));
	if (TestNotNull(TEXT("Burn condition exists"), Burn))
	{
		TestEqual(TEXT("Burn is a major status"), Burn->Kind, EBattleConditionKind::MajorStatus);
	}
	TestFlamethrower(*this, Catalog);
	TestVineWhip(*this, Catalog);
	TestTypeChart(*this, Catalog.GetTypeChart());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleRuntimeDataSourceFailClosedTest,
	"PokemonSolarus.Battle.Runtime.DataSource.MissingOrWrongTableFailsClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleRuntimeDataSourceFailClosedTest::RunTest(const FString& Parameters)
{
	FBattleRuntimeBundle Bundle;
	FString Error;
	FBattleDataTableRuntimeSource ProductionSource = MakeProductionSource();
	if (!TestTrue(TEXT("The reset fixture first creates a valid bundle"),
		ProductionSource.TryCreateInitialBattle(Bundle, Error)))
	{
		AddError(Error);
		return false;
	}

	FBattleDataTableRuntimeSource MissingSource{TSoftObjectPtr<UDataTable>()};
	TestFalse(TEXT("An empty runtime table reference is rejected"),
		MissingSource.TryCreateInitialBattle(Bundle, Error));
	TestFalse(TEXT("Failure clears a previously valid bundle"), Bundle.IsValid());
	TestTrue(TEXT("Missing-table failure returns a diagnostic"), !Error.IsEmpty());

	UDataTable* WrongTable = LoadObject<UDataTable>(nullptr, WrongRuntimeTablePath);
	if (!TestNotNull(TEXT("The wrong-row-type fixture table loads"), WrongTable))
	{
		return false;
	}
	FBattleDataTableRuntimeSource WrongSource{TSoftObjectPtr<UDataTable>(WrongTable)};
	Error.Reset();
	TestFalse(TEXT("A table with the wrong row struct is rejected"),
		WrongSource.TryCreateInitialBattle(Bundle, Error));
	TestFalse(TEXT("Wrong-row failure leaves no partial bundle"), Bundle.IsValid());
	TestTrue(TEXT("Wrong-row failure returns a diagnostic"), !Error.IsEmpty());
	return true;
}

#endif
