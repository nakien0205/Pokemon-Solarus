#if WITH_DEV_AUTOMATION_TESTS

#include "Battle/BattleDataTableAdapter.h"
#include "Battle/BattleDataTableRows.h"
#include "BattleTestFactories.h"
#include "Engine/DataTable.h"
#include "Misc/AutomationTest.h"
#include "UObject/UObjectGlobals.h"

namespace BattleDataTableAdapterTests
{
	template <typename RowType>
	UDataTable* MakeTransientTable()
	{
		UDataTable* Table = NewObject<UDataTable>(GetTransientPackage());
		check(Table != nullptr);
		Table->RowStruct = RowType::StaticStruct();
		return Table;
	}

	struct FTransientBattleTables
	{
		UDataTable* SpeciesForms = nullptr;
		UDataTable* Natures = nullptr;
		UDataTable* Moves = nullptr;
		UDataTable* Abilities = nullptr;
		UDataTable* Items = nullptr;
		UDataTable* Conditions = nullptr;
		UDataTable* TypeChart = nullptr;

		FBattleDataTableSet AsInput() const
		{
			return
			{
				SpeciesForms,
				Natures,
				Moves,
				Abilities,
				Items,
				Conditions,
				TypeChart
			};
		}
	};

	FName PokemonTypeName(const int32 TypeIndex)
	{
		static const FName Names[] =
		{
			TEXT("Normal"),
			TEXT("Fire"),
			TEXT("Water"),
			TEXT("Electric"),
			TEXT("Grass"),
			TEXT("Ice"),
			TEXT("Fighting"),
			TEXT("Poison"),
			TEXT("Ground"),
			TEXT("Flying"),
			TEXT("Psychic"),
			TEXT("Bug"),
			TEXT("Rock"),
			TEXT("Ghost"),
			TEXT("Dragon"),
			TEXT("Dark"),
			TEXT("Steel"),
			TEXT("Fairy")
		};
		check(TypeIndex >= 0 && TypeIndex < UE_ARRAY_COUNT(Names));
		return Names[TypeIndex];
	}

	void AddCompleteNeutralTypeChart(UDataTable& TypeChart)
	{
		for (int32 AttackingIndex = 0; AttackingIndex < FBattleTypeChart::TypeCount; ++AttackingIndex)
		{
			FBattleTypeChartTableRow Row;
			for (int32 DefendingIndex = 0; DefendingIndex < FBattleTypeChart::TypeCount; ++DefendingIndex)
			{
				FBattleTypeChartCellTableRow Cell;
				Cell.DefendingType = PokemonTypeName(DefendingIndex);
				Cell.Numerator = 1;
				Cell.Denominator = 1;
				Row.Entries.Add(Cell);
			}
			TypeChart.AddRow(PokemonTypeName(AttackingIndex), Row);
		}
	}

	FTransientBattleTables MakeValidTransientTables(TArray<FString>& OutNatureImportProblems)
	{
		FTransientBattleTables Tables;
		Tables.SpeciesForms = MakeTransientTable<FBattleSpeciesFormTableRow>();
		Tables.Natures = MakeTransientTable<FBattleNatureTableRow>();
		Tables.Moves = MakeTransientTable<FBattleMoveTableRow>();
		Tables.Abilities = MakeTransientTable<FBattleAbilityTableRow>();
		Tables.Items = MakeTransientTable<FBattleItemTableRow>();
		Tables.Conditions = MakeTransientTable<FBattleConditionTableRow>();
		Tables.TypeChart = MakeTransientTable<FBattleTypeChartTableRow>();

		FBattleAbilityTableRow Blaze;
		Tables.Abilities->AddRow(FName(TEXT("Ability.Blaze")), Blaze);

		FBattleItemTableRow PokeBall;
		PokeBall.Kind = FName(TEXT("Capture"));
		Tables.Items->AddRow(FName(TEXT("Item.PokeBall")), PokeBall);

		FBattleConditionTableRow Burn;
		Burn.Kind = FName(TEXT("MajorStatus"));
		Tables.Conditions->AddRow(FName(TEXT("Condition.Burn")), Burn);

		FBattleSpeciesFormTableRow Charizard;
		Charizard.PrimaryType = FName(TEXT("Fire"));
		Charizard.SecondaryType = FName(TEXT("Flying"));
		Charizard.BaseHP = 78;
		Charizard.BaseAttack = 84;
		Charizard.BaseDefense = 78;
		Charizard.BaseSpecialAttack = 109;
		Charizard.BaseSpecialDefense = 85;
		Charizard.BaseSpeed = 100;
		Charizard.CatchRate = 45;
		Charizard.AbilityIds.Add(FName(TEXT("Ability.Blaze")));
		Tables.SpeciesForms->AddRow(FName(TEXT("Species.Charizard")), Charizard);

		const FString NatureJson = TEXT(
			"[{\"Name\":\"Nature.Adamant\",\"BoostedStat\":\"Attack\","
			"\"ReducedStat\":\"SpecialAttack\"}]");
		OutNatureImportProblems = Tables.Natures->CreateTableFromJSONString(NatureJson);

		FBattleMoveTableRow Flamethrower;
		Flamethrower.Type = FName(TEXT("Fire"));
		Flamethrower.Category = FName(TEXT("Special"));
		Flamethrower.Power = 90;
		Flamethrower.Accuracy = 100;
		Flamethrower.BasePP = 15;
		Flamethrower.bAllowsPPBoosts = true;
		Flamethrower.Priority = 0;
		Flamethrower.TargetClass = FName(TEXT("SelectedOpponent"));
		Flamethrower.Flags.Add(FName(TEXT("BlockedByProtect")));

		FBattleMoveEffectTableRow Damage;
		Damage.Order = 0;
		Damage.Kind = FName(TEXT("Damage"));
		Damage.Target = FName(TEXT("ResolvedTarget"));
		Flamethrower.Effects.Add(Damage);

		FBattleMoveEffectTableRow BurnSecondary;
		BurnSecondary.Order = 1;
		BurnSecondary.Kind = FName(TEXT("ApplyCondition"));
		BurnSecondary.Target = FName(TEXT("ResolvedTarget"));
		BurnSecondary.ConditionId = FName(TEXT("Condition.Burn"));
		BurnSecondary.ChanceNumerator = 10;
		BurnSecondary.ChanceDenominator = 100;
		Flamethrower.Effects.Add(BurnSecondary);
		Tables.Moves->AddRow(FName(TEXT("Move.Flamethrower")), Flamethrower);

		AddCompleteNeutralTypeChart(*Tables.TypeChart);
		return Tables;
	}

	using BattleTest::MakeDefinitionId;

	bool ContainsDiagnosticCode(
		const TArray<FBattleCatalogDiagnostic>& Diagnostics,
		const EBattleCatalogDiagnosticCode Code)
	{
		return Diagnostics.ContainsByPredicate(
			[Code](const FBattleCatalogDiagnostic& Diagnostic)
			{
				return Diagnostic.Code == Code;
			});
	}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC02BDataTableJsonCopyAndIsolationTest,
	"PokemonSolarus.Battle.C02B.Adapter.JsonCopyAndSourceMutationIsolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBattleC02BDataTableJsonCopyAndIsolationTest::RunTest(const FString& Parameters)
{
	TArray<FString> NatureImportProblems;
	const FTransientBattleTables Tables = MakeValidTransientTables(NatureImportProblems);
	TestEqual(TEXT("The nature JSON imports without problems"), NatureImportProblems.Num(), 0);

	FBattleDefinitionCatalog Catalog;
	TArray<FBattleCatalogDiagnostic> Diagnostics;
	TestTrue(
		TEXT("The adapter copies a complete transient Data Table set"),
		FBattleDataTableAdapter::BuildCatalog(Tables.AsInput(), Catalog, Diagnostics));
	TestTrue(TEXT("The copied catalog is valid"), Catalog.IsValid());
	TestEqual(TEXT("A successful adapter build reports no diagnostics"), Diagnostics.Num(), 0);

	const FMoveId FlamethrowerId = MakeDefinitionId<FMoveId>(TEXT("Move.Flamethrower"));
	const FBattleMoveDefinition* CopiedMove = Catalog.FindMove(FlamethrowerId);
	TestNotNull(TEXT("The copied move can be found"), CopiedMove);
	if (CopiedMove == nullptr)
	{
		return false;
	}
	TestEqual(TEXT("The adapter copied move power"), CopiedMove->Power, 90);
	TestEqual(TEXT("The adapter copied the ordered effect list"), CopiedMove->Effects.Num(), 2);
	TestEqual(TEXT("The adapter copied the secondary chance"), CopiedMove->Effects[1].ChanceNumerator, 10);

	const FBattleNatureDefinition* Adamant = Catalog.FindNature(
		MakeDefinitionId<FNatureId>(TEXT("Nature.Adamant")));
	TestNotNull(TEXT("A JSON-imported nature is present"), Adamant);
	if (Adamant != nullptr)
	{
		TestEqual(
			TEXT("The JSON-imported nature copied its raised stat"),
			Adamant->Modifier.GetBoostedStat(),
			ENatureStat::Attack);
		TestEqual(
			TEXT("The JSON-imported nature copied its lowered stat"),
			Adamant->Modifier.GetReducedStat(),
			ENatureStat::SpecialAttack);
	}

	FBattleMoveTableRow* MutableSourceRow = Tables.Moves->FindRow<FBattleMoveTableRow>(
		FName(TEXT("Move.Flamethrower")),
		TEXT("C02B mutation isolation"),
		false);
	TestNotNull(TEXT("The source row exists for the mutation proof"), MutableSourceRow);
	if (MutableSourceRow != nullptr)
	{
		MutableSourceRow->Power = 999;
		MutableSourceRow->Effects[0].Order = 99;
		MutableSourceRow->Effects[1].ChanceNumerator = 100;
	}

	CopiedMove = Catalog.FindMove(FlamethrowerId);
	TestNotNull(TEXT("The frozen catalog remains usable after source mutation"), CopiedMove);
	if (CopiedMove != nullptr)
	{
		TestEqual(TEXT("Later source-row power mutation cannot alter the catalog"), CopiedMove->Power, 90);
		TestEqual(TEXT("Later source-row order mutation cannot alter the catalog"), CopiedMove->Effects[0].Order, 0);
		TestEqual(
			TEXT("Later source-row chance mutation cannot alter the catalog"),
			CopiedMove->Effects[1].ChanceNumerator,
			10);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC02BDataTableValidationDiagnosticsTest,
	"PokemonSolarus.Battle.C02B.Adapter.TableValidationDiagnostics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBattleC02BDataTableValidationDiagnosticsTest::RunTest(const FString& Parameters)
{
	TArray<FString> NatureImportProblems;
	const FTransientBattleTables Tables = MakeValidTransientTables(NatureImportProblems);

	UDataTable* WrongMoveTable = MakeTransientTable<FBattleAbilityTableRow>();
	FBattleAbilityTableRow WrongMoveRow;
	WrongMoveTable->AddRow(FName(TEXT("Move.WrongType")), WrongMoveRow);

	FBattleDataTableSet InvalidTables = Tables.AsInput();
	InvalidTables.Moves = WrongMoveTable;
	InvalidTables.Conditions = nullptr;

	FBattleDefinitionCatalog Catalog;
	TArray<FBattleCatalogDiagnostic> Diagnostics;
	TestFalse(
		TEXT("Missing and wrong-typed tables reject the adapter build"),
		FBattleDataTableAdapter::BuildCatalog(InvalidTables, Catalog, Diagnostics));
	TestFalse(TEXT("Adapter failure returns no partial catalog"), Catalog.IsValid());
	TestTrue(
		TEXT("A missing table produces a typed diagnostic"),
		ContainsDiagnosticCode(Diagnostics, EBattleCatalogDiagnosticCode::MissingTable));
	TestTrue(
		TEXT("A wrong row type produces a typed diagnostic"),
		ContainsDiagnosticCode(Diagnostics, EBattleCatalogDiagnosticCode::WrongRowType));

	for (int32 Index = 1; Index < Diagnostics.Num(); ++Index)
	{
		TestFalse(
			FString::Printf(TEXT("Diagnostic %d does not sort before its predecessor"), Index),
			FBattleCatalogDiagnostic::Less(Diagnostics[Index], Diagnostics[Index - 1]));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC10TargetAdapterNamesTest,
	"PokemonSolarus.Battle.C04B.C10Targets.Adapter.TargetClassNamesAndUnknownFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBattleC10TargetAdapterNamesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	struct FTargetMapping
	{
		FName Name;
		EBattleTargetClass TargetClass;
	};
	const TArray<FTargetMapping> Mappings =
	{
		{FName(TEXT("SelectedOtherBattler")), EBattleTargetClass::SelectedOtherBattler},
		{FName(TEXT("FixedOpponentSpreadSet")), EBattleTargetClass::FixedOpponentSpreadSet}
	};

	for (const FTargetMapping& Mapping : Mappings)
	{
		TArray<FString> NatureImportProblems;
		const FTransientBattleTables Tables = MakeValidTransientTables(NatureImportProblems);
		FBattleMoveTableRow* MoveRow = Tables.Moves->FindRow<FBattleMoveTableRow>(
			FName(TEXT("Move.Flamethrower")),
			TEXT("C10 target mapping"),
			false);
		if (!TestNotNull(TEXT("The target-mapping source move exists"), MoveRow))
		{
			return false;
		}
		MoveRow->TargetClass = Mapping.Name;

		FBattleDefinitionCatalog Catalog;
		TArray<FBattleCatalogDiagnostic> Diagnostics;
		TestTrue(
			FString::Printf(TEXT("The adapter accepts target class %s"), *Mapping.Name.ToString()),
			FBattleDataTableAdapter::BuildCatalog(Tables.AsInput(), Catalog, Diagnostics));
		const FBattleMoveDefinition* Move = Catalog.FindMove(
			MakeDefinitionId<FMoveId>(TEXT("Move.Flamethrower")));
		TestTrue(
			FString::Printf(TEXT("The adapter freezes target class %s exactly"), *Mapping.Name.ToString()),
			Move != nullptr && Move->TargetClass == Mapping.TargetClass);
		TestTrue(TEXT("A known appended target name adds no diagnostics"), Diagnostics.IsEmpty());
	}

	TArray<FString> NatureImportProblems;
	const FTransientBattleTables UnknownTables = MakeValidTransientTables(NatureImportProblems);
	FBattleMoveTableRow* UnknownMove = UnknownTables.Moves->FindRow<FBattleMoveTableRow>(
		FName(TEXT("Move.Flamethrower")),
		TEXT("C10 unknown target mapping"),
		false);
	if (!TestNotNull(TEXT("The unknown-target source move exists"), UnknownMove))
	{
		return false;
	}
	UnknownMove->TargetClass = FName(TEXT("UnknownC10Target"));
	FBattleDefinitionCatalog RejectedCatalog;
	TArray<FBattleCatalogDiagnostic> Diagnostics;
	TestFalse(
		TEXT("An unknown authored target name rejects the adapter build"),
		FBattleDataTableAdapter::BuildCatalog(
			UnknownTables.AsInput(),
			RejectedCatalog,
			Diagnostics));
	TestFalse(TEXT("Unknown target failure returns no partial catalog"), RejectedCatalog.IsValid());
	TestTrue(
		TEXT("Unknown target failure reports InvalidAuthoredValue"),
		ContainsDiagnosticCode(
			Diagnostics,
			EBattleCatalogDiagnosticCode::InvalidAuthoredValue));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC10HeldItemMoveAdapterContractTest,
	"PokemonSolarus.Battle.C08C.C10HeldItemMoves.Contract.AdapterNamesPolicyAndUnknownRejection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBattleC10HeldItemMoveAdapterContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	struct FOperationMapping
	{
		FName Name;
		EBattleMoveHeldItemOperation Operation;
		bool bDamaging;
		bool bRestore;
	};
	const FOperationMapping Mappings[] =
	{
		{FName(TEXT("RemoveCurrent")), EBattleMoveHeldItemOperation::RemoveCurrent, true, false},
		{FName(TEXT("ExchangeCurrent")), EBattleMoveHeldItemOperation::ExchangeCurrent, false, false},
		{FName(TEXT("TransferCurrent")), EBattleMoveHeldItemOperation::TransferCurrent, true, false},
		{FName(TEXT("RestoreLastConsumed")), EBattleMoveHeldItemOperation::RestoreLastConsumed, false, true}
	};

	for (const FOperationMapping& Mapping : Mappings)
	{
		TArray<FString> NatureImportProblems;
		const FTransientBattleTables Tables = MakeValidTransientTables(NatureImportProblems);
		FBattleMoveTableRow* MoveRow = Tables.Moves->FindRow<FBattleMoveTableRow>(
			FName(TEXT("Move.Flamethrower")), TEXT("R5 operation mapping"), false);
		if (!TestNotNull(TEXT("The R5 mapping source move exists"), MoveRow))
		{
			return false;
		}
		MoveRow->Type = FName(TEXT("Normal"));
		MoveRow->Category = FName(Mapping.bDamaging ? TEXT("Physical") : TEXT("Status"));
		MoveRow->Power = Mapping.bDamaging ? 40 : 0;
		MoveRow->bAlwaysHits = !Mapping.bDamaging;
		MoveRow->Accuracy = Mapping.bDamaging ? 100 : 0;
		MoveRow->TargetClass = FName(Mapping.bRestore ? TEXT("Self") : TEXT("SelectedOpponent"));
		MoveRow->Flags.Reset();
		MoveRow->Effects.Reset();
		if (Mapping.bDamaging)
		{
			FBattleMoveEffectTableRow Damage;
			Damage.Order = 0;
			Damage.Kind = FName(TEXT("Damage"));
			Damage.Target = FName(TEXT("ResolvedTarget"));
			MoveRow->Effects.Add(Damage);
		}
		FBattleMoveEffectTableRow ChangeItem;
		ChangeItem.Order = MoveRow->Effects.Num();
		ChangeItem.Kind = FName(TEXT("ChangeItem"));
		ChangeItem.Target = FName(Mapping.bRestore ? TEXT("User") : TEXT("ResolvedTarget"));
		ChangeItem.HeldItemOperation = Mapping.Name;
		MoveRow->Effects.Add(ChangeItem);

		FBattleDefinitionCatalog Catalog;
		TArray<FBattleCatalogDiagnostic> Diagnostics;
		TestTrue(
			FString::Printf(TEXT("The adapter accepts exact operation name %s"), *Mapping.Name.ToString()),
			FBattleDataTableAdapter::BuildCatalog(Tables.AsInput(), Catalog, Diagnostics));
		const FBattleMoveDefinition* Move = Catalog.FindMove(
			MakeDefinitionId<FMoveId>(TEXT("Move.Flamethrower")));
		TestTrue(
			FString::Printf(TEXT("The adapter freezes operation %s exactly"), *Mapping.Name.ToString()),
			Move != nullptr
				&& Move->Effects.Last().HeldItemOperation == Mapping.Operation
				&& !Move->Effects.Last().ItemId.IsValid());
		TestTrue(TEXT("A known R5 operation adds no diagnostics"), Diagnostics.IsEmpty());
	}

	TArray<FString> NatureImportProblems;
	const FTransientBattleTables UnknownTables = MakeValidTransientTables(NatureImportProblems);
	FBattleMoveTableRow* UnknownMove = UnknownTables.Moves->FindRow<FBattleMoveTableRow>(
		FName(TEXT("Move.Flamethrower")), TEXT("R5 unknown operation"), false);
	if (!TestNotNull(TEXT("The unknown-operation source move exists"), UnknownMove))
	{
		return false;
	}
	UnknownMove->Effects[1].Kind = FName(TEXT("ChangeItem"));
	UnknownMove->Effects[1].ConditionId = NAME_None;
	UnknownMove->Effects[1].ChanceNumerator = 1;
	UnknownMove->Effects[1].ChanceDenominator = 1;
	UnknownMove->Effects[1].HeldItemOperation = FName(TEXT("UnknownHeldItemOperation"));
	FBattleDefinitionCatalog RejectedCatalog;
	TArray<FBattleCatalogDiagnostic> Diagnostics;
	TestFalse(
		TEXT("An unknown held-item operation rejects the adapter build"),
		FBattleDataTableAdapter::BuildCatalog(
			UnknownTables.AsInput(), RejectedCatalog, Diagnostics));
	TestFalse(TEXT("Unknown operation returns no partial catalog"), RejectedCatalog.IsValid());
	TestTrue(
		TEXT("Unknown operation produces an authored-value diagnostic"),
		ContainsDiagnosticCode(Diagnostics, EBattleCatalogDiagnosticCode::InvalidAuthoredValue));
	TestTrue(
		TEXT("Unknown operation identifies the authored operation field"),
		Diagnostics.ContainsByPredicate([](const FBattleCatalogDiagnostic& Diagnostic)
		{
			return Diagnostic.Field == FName(TEXT("Effects.HeldItemOperation"));
		}));

	TArray<FString> PolicyImportProblems;
	const FTransientBattleTables PolicyTables = MakeValidTransientTables(PolicyImportProblems);
	FBattleItemTableRow* ItemRow = PolicyTables.Items->FindRow<FBattleItemTableRow>(
		FName(TEXT("Item.PokeBall")), TEXT("R5 item policy"), false);
	if (!TestNotNull(TEXT("The item-policy source row exists"), ItemRow))
	{
		return false;
	}
	ItemRow->bCanBeTakenByMove = false;
	FBattleDefinitionCatalog PolicyCatalog;
	Diagnostics.Reset();
	TestTrue(
		TEXT("The adapter copies an explicit unremovable item policy"),
		FBattleDataTableAdapter::BuildCatalog(
			PolicyTables.AsInput(), PolicyCatalog, Diagnostics));
	const FBattleItemDefinition* Item = PolicyCatalog.FindItem(
		MakeDefinitionId<FItemId>(TEXT("Item.PokeBall")));
	TestTrue(TEXT("The frozen item retains its authored takeability policy"),
		Item != nullptr && !Item->bCanBeTakenByMove);
	ItemRow->bCanBeTakenByMove = true;
	Item = PolicyCatalog.FindItem(MakeDefinitionId<FItemId>(TEXT("Item.PokeBall")));
	TestTrue(TEXT("Later source mutation cannot change the frozen item policy"),
		Item != nullptr && !Item->bCanBeTakenByMove);
	return true;
}

}

#endif // WITH_DEV_AUTOMATION_TESTS
