#if WITH_DEV_AUTOMATION_TESTS

#include "Battle/BattleDefinitionCatalog.h"
#include "BattleTestFactories.h"
#include "Algo/Reverse.h"
#include "Misc/AutomationTest.h"

namespace
{
	using BattleTest::MakeDefinitionId;

	TArray<FBattleTypeChartEntry> MakeCompleteNeutralChart()
	{
		TArray<FBattleTypeChartEntry> Entries;
		Entries.Reserve(FBattleTypeChart::EntryCount);
		for (int32 AttackingIndex = 0; AttackingIndex < FBattleTypeChart::TypeCount; ++AttackingIndex)
		{
			for (int32 DefendingIndex = 0; DefendingIndex < FBattleTypeChart::TypeCount; ++DefendingIndex)
			{
				Entries.Add(
					{
						static_cast<EPokemonType>(AttackingIndex),
						static_cast<EPokemonType>(DefendingIndex),
						1,
						1
					});
			}
		}
		return Entries;
	}

	FBattleMoveEffectDescriptor MakeEffect(
		const int32 Order,
		const EBattleMoveEffectKind Kind,
		const EBattleEffectTarget Target)
	{
		FBattleMoveEffectDescriptor Effect;
		Effect.Order = Order;
		Effect.Kind = Kind;
		Effect.Target = Target;
		return Effect;
	}

	FBattleMoveDefinition MakeDamagingMove(
		const TCHAR* IdName,
		const EPokemonType Type,
		const EBattleMoveCategory Category,
		const int32 Power,
		const EBattleTargetClass TargetClass)
	{
		FBattleMoveDefinition Move;
		Move.Id = MakeDefinitionId<FMoveId>(IdName);
		Move.Type = Type;
		Move.Category = Category;
		Move.Power = Power;
		Move.Accuracy = 100;
		Move.BasePP = 15;
		Move.Priority = 0;
		Move.TargetClass = TargetClass;
		Move.Flags = EBattleMoveFlags::BlockedByProtect;
		Move.Effects.Add(MakeEffect(0, EBattleMoveEffectKind::Damage, EBattleEffectTarget::ResolvedTarget));
		return Move;
	}

	FBattleDefinitionCatalogInput MakeValidCatalogInput()
	{
		FBattleDefinitionCatalogInput Input;
		Input.TypeChartEntries = MakeCompleteNeutralChart();

		const FAbilityId BlazeId = MakeDefinitionId<FAbilityId>(TEXT("Ability.Blaze"));
		const FAbilityId OvergrowId = MakeDefinitionId<FAbilityId>(TEXT("Ability.Overgrow"));
		Input.Abilities =
		{
			{OvergrowId},
			{BlazeId}
		};

		Input.Items =
		{
			{MakeDefinitionId<FItemId>(TEXT("Item.PokeBall")), EBattleItemKind::Capture},
			{MakeDefinitionId<FItemId>(TEXT("Item.Leftovers")), EBattleItemKind::Held},
			{MakeDefinitionId<FItemId>(TEXT("Item.HyperPotion")), EBattleItemKind::Battle}
		};

		const FConditionId BurnId = MakeDefinitionId<FConditionId>(TEXT("Condition.Burn"));
		const FConditionId HelpingHandId = MakeDefinitionId<FConditionId>(TEXT("Condition.HelpingHand"));
		const FConditionId ProtectId = MakeDefinitionId<FConditionId>(TEXT("Condition.Protect"));
		const FConditionId RainId = MakeDefinitionId<FConditionId>(TEXT("Condition.Rain"));
		Input.Conditions =
		{
			{ProtectId, EBattleConditionKind::Volatile},
			{RainId, EBattleConditionKind::Weather},
			{BurnId, EBattleConditionKind::MajorStatus},
			{HelpingHandId, EBattleConditionKind::Volatile}
		};

		FBattleSpeciesFormDefinition Venusaur;
		Venusaur.Id = MakeDefinitionId<FSpeciesFormId>(TEXT("Species.Venusaur"));
		Venusaur.PrimaryType = EPokemonType::Grass;
		Venusaur.SecondaryType = EPokemonType::Poison;
		Venusaur.BaseStats = {80, 82, 83, 100, 100, 80};
		Venusaur.CatchRate = 45;
		Venusaur.AbilityChoices.Add(OvergrowId);

		FBattleSpeciesFormDefinition Charizard;
		Charizard.Id = MakeDefinitionId<FSpeciesFormId>(TEXT("Species.Charizard"));
		Charizard.PrimaryType = EPokemonType::Fire;
		Charizard.SecondaryType = EPokemonType::Flying;
		Charizard.BaseStats = {78, 84, 78, 109, 85, 100};
		Charizard.CatchRate = 45;
		Charizard.AbilityChoices.Add(BlazeId);
		Input.SpeciesForms = {Venusaur, Charizard};

		FNatureStatModifier AdamantModifier;
		check(FNatureStatModifier::TryCreate(
			ENatureStat::Attack,
			ENatureStat::SpecialAttack,
			AdamantModifier));
		Input.Natures =
		{
			{MakeDefinitionId<FNatureId>(TEXT("Nature.Hardy")), FNatureStatModifier()},
			{MakeDefinitionId<FNatureId>(TEXT("Nature.Adamant")), AdamantModifier}
		};

		FBattleMoveDefinition Slash = MakeDamagingMove(
			TEXT("Move.Slash"),
			EPokemonType::Normal,
			EBattleMoveCategory::Physical,
			70,
			EBattleTargetClass::SelectedOpponent);

		FBattleMoveDefinition Flamethrower = MakeDamagingMove(
			TEXT("Move.Flamethrower"),
			EPokemonType::Fire,
			EBattleMoveCategory::Special,
			90,
			EBattleTargetClass::SelectedOpponent);
		FBattleMoveEffectDescriptor BurnSecondary = MakeEffect(
			1,
			EBattleMoveEffectKind::ApplyCondition,
			EBattleEffectTarget::ResolvedTarget);
		BurnSecondary.ConditionId = BurnId;
		BurnSecondary.ChanceNumerator = 10;
		BurnSecondary.ChanceDenominator = 100;
		Flamethrower.Effects.Add(BurnSecondary);

		FBattleMoveDefinition Protect;
		Protect.Id = MakeDefinitionId<FMoveId>(TEXT("Move.Protect"));
		Protect.Type = EPokemonType::Normal;
		Protect.Category = EBattleMoveCategory::Status;
		Protect.Power = 0;
		Protect.bAlwaysHits = true;
		Protect.Accuracy = 0;
		Protect.BasePP = 10;
		Protect.Priority = 4;
		Protect.TargetClass = EBattleTargetClass::Self;
		FBattleMoveEffectDescriptor ProtectEffect = MakeEffect(
			0,
			EBattleMoveEffectKind::Protect,
			EBattleEffectTarget::User);
		ProtectEffect.ConditionId = ProtectId;
		ProtectEffect.DurationTurns = 1;
		Protect.Effects.Add(ProtectEffect);

		FBattleMoveDefinition Swift = MakeDamagingMove(
			TEXT("Move.Swift"),
			EPokemonType::Normal,
			EBattleMoveCategory::Special,
			60,
			EBattleTargetClass::FixedSpreadSet);
		Swift.bAlwaysHits = true;
		Swift.Accuracy = 0;

		FBattleMoveDefinition Earthquake = MakeDamagingMove(
			TEXT("Move.Earthquake"),
			EPokemonType::Ground,
			EBattleMoveCategory::Physical,
			100,
			EBattleTargetClass::FixedSpreadSet);

		FBattleMoveDefinition HelpingHand;
		HelpingHand.Id = MakeDefinitionId<FMoveId>(TEXT("Move.HelpingHand"));
		HelpingHand.Type = EPokemonType::Normal;
		HelpingHand.Category = EBattleMoveCategory::Status;
		HelpingHand.bAlwaysHits = true;
		HelpingHand.BasePP = 20;
		HelpingHand.Priority = 5;
		HelpingHand.TargetClass = EBattleTargetClass::SelectedAlly;
		FBattleMoveEffectDescriptor HelpingHandEffect = MakeEffect(
			0,
			EBattleMoveEffectKind::ApplyCondition,
			EBattleEffectTarget::ResolvedTarget);
		HelpingHandEffect.ConditionId = HelpingHandId;
		HelpingHandEffect.DurationTurns = 1;
		HelpingHand.Effects.Add(HelpingHandEffect);

		FBattleMoveDefinition RainDance;
		RainDance.Id = MakeDefinitionId<FMoveId>(TEXT("Move.RainDance"));
		RainDance.Type = EPokemonType::Water;
		RainDance.Category = EBattleMoveCategory::Status;
		RainDance.bAlwaysHits = true;
		RainDance.BasePP = 5;
		RainDance.Priority = 0;
		RainDance.TargetClass = EBattleTargetClass::Field;
		FBattleMoveEffectDescriptor RainEffect = MakeEffect(
			0,
			EBattleMoveEffectKind::SetFieldCondition,
			EBattleEffectTarget::Field);
		RainEffect.ConditionId = RainId;
		RainEffect.DurationTurns = 5;
		RainDance.Effects.Add(RainEffect);

		Input.Moves =
		{
			RainDance,
			Swift,
			Slash,
			Protect,
			HelpingHand,
			Flamethrower,
			Earthquake
		};
		return Input;
	}

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
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC02BMoveRecordShapesTest,
	"PokemonSolarus.Battle.C02B.Catalog.MoveRecordShapes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBattleC02BMoveRecordShapesTest::RunTest(const FString& Parameters)
{
	const FBattleDefinitionCatalogInput Input = MakeValidCatalogInput();
	FBattleDefinitionCatalog Catalog;
	TArray<FBattleCatalogDiagnostic> Diagnostics;
	TestTrue(
		TEXT("The valid catalog fixture is accepted"),
		FBattleDefinitionCatalog::TryCreate(Input, Catalog, Diagnostics));
	TestTrue(TEXT("The accepted catalog is valid"), Catalog.IsValid());
	TestEqual(TEXT("A successful catalog reports no diagnostics"), Diagnostics.Num(), 0);

	const FBattleMoveDefinition* Slash = Catalog.FindMove(MakeDefinitionId<FMoveId>(TEXT("Move.Slash")));
	const FBattleMoveDefinition* Flamethrower = Catalog.FindMove(
		MakeDefinitionId<FMoveId>(TEXT("Move.Flamethrower")));
	const FBattleMoveDefinition* Protect = Catalog.FindMove(MakeDefinitionId<FMoveId>(TEXT("Move.Protect")));
	const FBattleMoveDefinition* Swift = Catalog.FindMove(MakeDefinitionId<FMoveId>(TEXT("Move.Swift")));
	const FBattleMoveDefinition* Earthquake = Catalog.FindMove(
		MakeDefinitionId<FMoveId>(TEXT("Move.Earthquake")));
	const FBattleMoveDefinition* HelpingHand = Catalog.FindMove(
		MakeDefinitionId<FMoveId>(TEXT("Move.HelpingHand")));
	const FBattleMoveDefinition* RainDance = Catalog.FindMove(
		MakeDefinitionId<FMoveId>(TEXT("Move.RainDance")));

	TestNotNull(TEXT("The Physical move is present"), Slash);
	TestNotNull(TEXT("The Special move is present"), Flamethrower);
	TestNotNull(TEXT("The Status move is present"), Protect);
	TestNotNull(TEXT("The always-hit move is present"), Swift);
	TestNotNull(TEXT("The spread move is present"), Earthquake);
	TestNotNull(TEXT("The ally move is present"), HelpingHand);
	TestNotNull(TEXT("The field move is present"), RainDance);
	if (Slash == nullptr || Flamethrower == nullptr || Protect == nullptr || Swift == nullptr
		|| Earthquake == nullptr || HelpingHand == nullptr || RainDance == nullptr)
	{
		return false;
	}

	TestEqual(TEXT("Slash is Physical"), Slash->Category, EBattleMoveCategory::Physical);
	TestEqual(TEXT("Flamethrower is Special"), Flamethrower->Category, EBattleMoveCategory::Special);
	TestEqual(TEXT("Protect is Status"), Protect->Category, EBattleMoveCategory::Status);
	TestTrue(TEXT("Swift has literal always-hit accuracy"), Swift->bAlwaysHits);
	TestEqual(TEXT("Always-hit records store no numeric accuracy"), Swift->Accuracy, 0);
	TestEqual(
		TEXT("Earthquake preserves the fixed spread target class"),
		Earthquake->TargetClass,
		EBattleTargetClass::FixedSpreadSet);
	TestEqual(
		TEXT("Helping Hand preserves the selected-ally target class"),
		HelpingHand->TargetClass,
		EBattleTargetClass::SelectedAlly);
	TestEqual(
		TEXT("Rain Dance preserves the field target class"),
		RainDance->TargetClass,
		EBattleTargetClass::Field);
	TestEqual(TEXT("Flamethrower preserves ordered effects"), Flamethrower->Effects.Num(), 2);
	TestEqual(TEXT("The secondary remains second"), Flamethrower->Effects[1].Order, 1);
	TestEqual(TEXT("The secondary keeps its exact numerator"), Flamethrower->Effects[1].ChanceNumerator, 10);
	TestEqual(TEXT("The secondary keeps its exact denominator"), Flamethrower->Effects[1].ChanceDenominator, 100);

	const FBattleNatureDefinition* Adamant = Catalog.FindNature(
		MakeDefinitionId<FNatureId>(TEXT("Nature.Adamant")));
	TestNotNull(TEXT("The authored nature can be found"), Adamant);
	if (Adamant != nullptr)
	{
		TestEqual(
			TEXT("Adamant raises Attack"),
			Adamant->Modifier.GetBoostedStat(),
			ENatureStat::Attack);
		TestEqual(
			TEXT("Adamant lowers Special Attack"),
			Adamant->Modifier.GetReducedStat(),
			ENatureStat::SpecialAttack);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC02BCatalogValidationAndAtomicFailureTest,
	"PokemonSolarus.Battle.C02B.Catalog.ValidationAndAtomicFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBattleC02BCatalogValidationAndAtomicFailureTest::RunTest(const FString& Parameters)
{
	FBattleDefinitionCatalogInput ValidInput = MakeValidCatalogInput();
	FBattleDefinitionCatalog Catalog;
	TArray<FBattleCatalogDiagnostic> Diagnostics;
	TestTrue(
		TEXT("The baseline catalog is valid before the atomic-failure check"),
		FBattleDefinitionCatalog::TryCreate(ValidInput, Catalog, Diagnostics));

	FBattleDefinitionCatalogInput InvalidInput = ValidInput;
	InvalidInput.TypeChartEntries.Pop(EAllowShrinking::No);
	const FBattleMoveDefinition DuplicateMove = InvalidInput.Moves[0];
	InvalidInput.Moves.Add(DuplicateMove);
	InvalidInput.Moves[1].Accuracy = 101;
	InvalidInput.Moves[2].Effects.Add(
		MakeEffect(InvalidInput.Moves[2].Effects.Last().Order, EBattleMoveEffectKind::Recoil, EBattleEffectTarget::User));
	InvalidInput.Moves[2].Effects.Last().MagnitudeNumerator = 1;
	InvalidInput.Moves[2].Effects.Last().MagnitudeDenominator = 3;
	InvalidInput.SpeciesForms[0].AbilityChoices.Add(
		MakeDefinitionId<FAbilityId>(TEXT("Ability.Missing")));

	TestFalse(
		TEXT("Invalid content is rejected atomically"),
		FBattleDefinitionCatalog::TryCreate(InvalidInput, Catalog, Diagnostics));
	TestFalse(TEXT("Failure resets a previously valid catalog"), Catalog.IsValid());
	TestEqual(TEXT("Failure exposes no partially usable moves"), Catalog.GetMoves().Num(), 0);
	TestTrue(
		TEXT("Incomplete chart rejection is diagnosed"),
		ContainsDiagnosticCode(Diagnostics, EBattleCatalogDiagnosticCode::IncompleteTypeChart));
	TestTrue(
		TEXT("Duplicate identity rejection is diagnosed"),
		ContainsDiagnosticCode(Diagnostics, EBattleCatalogDiagnosticCode::DuplicateIdentity));
	TestTrue(
		TEXT("Missing definition references are diagnosed"),
		ContainsDiagnosticCode(Diagnostics, EBattleCatalogDiagnosticCode::MissingReference));
	TestTrue(
		TEXT("Invalid ranges are diagnosed"),
		ContainsDiagnosticCode(Diagnostics, EBattleCatalogDiagnosticCode::InvalidRange));
	TestTrue(
		TEXT("Bad effect order is diagnosed"),
		ContainsDiagnosticCode(Diagnostics, EBattleCatalogDiagnosticCode::InvalidEffectOrder));

	FBattleDefinitionCatalogInput Incompatible = ValidInput;
	FBattleMoveDefinition InvalidStatus = Incompatible.Moves[2];
	InvalidStatus.Id = MakeDefinitionId<FMoveId>(TEXT("Move.InvalidStatusDamage"));
	InvalidStatus.Category = EBattleMoveCategory::Status;
	InvalidStatus.Power = 0;
	Incompatible.Moves.Add(InvalidStatus);
	TestFalse(
		TEXT("A Status-category move with a damage effect is rejected"),
		FBattleDefinitionCatalog::TryCreate(Incompatible, Catalog, Diagnostics));
	TestTrue(
		TEXT("Category/effect incompatibility is diagnosed"),
		ContainsDiagnosticCode(Diagnostics, EBattleCatalogDiagnosticCode::IncompatibleEffect));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC02BCatalogDeterministicOrderAndDiagnosticsTest,
	"PokemonSolarus.Battle.C02B.Catalog.DeterministicOrderAndDiagnostics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBattleC02BCatalogDeterministicOrderAndDiagnosticsTest::RunTest(const FString& Parameters)
{
	FBattleDefinitionCatalogInput Input = MakeValidCatalogInput();
	Algo::Reverse(Input.SpeciesForms);
	Algo::Reverse(Input.Natures);
	Algo::Reverse(Input.Moves);
	Algo::Reverse(Input.Abilities);
	Algo::Reverse(Input.Items);
	Algo::Reverse(Input.Conditions);
	Algo::Reverse(Input.TypeChartEntries);

	FBattleDefinitionCatalog Catalog;
	TArray<FBattleCatalogDiagnostic> Diagnostics;
	TestTrue(
		TEXT("Input order does not affect valid construction"),
		FBattleDefinitionCatalog::TryCreate(Input, Catalog, Diagnostics));

	auto TestLexicallySorted = [this](const TCHAR* Label, const auto& Values)
	{
		for (int32 Index = 1; Index < Values.Num(); ++Index)
		{
			TestTrue(
				FString::Printf(TEXT("%s entry %d follows lexical definition order"), Label, Index),
				Values[Index - 1].Id.LexicalLess(Values[Index].Id));
		}
	};
	TestLexicallySorted(TEXT("Species"), Catalog.GetSpeciesForms());
	TestLexicallySorted(TEXT("Nature"), Catalog.GetNatures());
	TestLexicallySorted(TEXT("Move"), Catalog.GetMoves());
	TestLexicallySorted(TEXT("Ability"), Catalog.GetAbilities());
	TestLexicallySorted(TEXT("Item"), Catalog.GetItems());
	TestLexicallySorted(TEXT("Condition"), Catalog.GetConditions());

	FBattleDefinitionCatalogInput InvalidA = MakeValidCatalogInput();
	const FBattleMoveDefinition DuplicateMove = InvalidA.Moves[0];
	InvalidA.Moves.Add(DuplicateMove);
	InvalidA.SpeciesForms[0].AbilityChoices.Add(
		MakeDefinitionId<FAbilityId>(TEXT("Ability.Missing")));
	InvalidA.Items[0].Kind = EBattleItemKind::Invalid;

	FBattleDefinitionCatalogInput InvalidB = InvalidA;
	Algo::Reverse(InvalidB.Moves);
	Algo::Reverse(InvalidB.SpeciesForms);
	Algo::Reverse(InvalidB.Items);

	FBattleDefinitionCatalog RejectedA;
	FBattleDefinitionCatalog RejectedB;
	TArray<FBattleCatalogDiagnostic> DiagnosticsA;
	TArray<FBattleCatalogDiagnostic> DiagnosticsB;
	TestFalse(
		TEXT("The first invalid ordering is rejected"),
		FBattleDefinitionCatalog::TryCreate(InvalidA, RejectedA, DiagnosticsA));
	TestFalse(
		TEXT("The reversed invalid ordering is rejected"),
		FBattleDefinitionCatalog::TryCreate(InvalidB, RejectedB, DiagnosticsB));
	TestEqual(TEXT("Diagnostic count is independent of input order"), DiagnosticsA.Num(), DiagnosticsB.Num());
	for (int32 Index = 0; Index < FMath::Min(DiagnosticsA.Num(), DiagnosticsB.Num()); ++Index)
	{
		TestTrue(
			FString::Printf(TEXT("Diagnostic %d is deterministic"), Index),
			DiagnosticsA[Index] == DiagnosticsB[Index]);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
