#if WITH_DEV_AUTOMATION_TESTS

#include "Battle/BattleDataTableAdapter.h"
#include "Battle/BattleDataTableRows.h"
#include "Battle/BattleDataTableRuntimeSource.h"
#include "Battle/BattleReplay.h"
#include "Battle/BattleRuntimeDataTableRows.h"
#include "Engine/DataTable.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UObjectGlobals.h"

namespace BattleCanonicalContentTests
{
struct FTables
{
	UDataTable* Species = nullptr;
	UDataTable* Natures = nullptr;
	UDataTable* Moves = nullptr;
	UDataTable* Abilities = nullptr;
	UDataTable* Items = nullptr;
	UDataTable* Conditions = nullptr;
	UDataTable* Types = nullptr;
	UDataTable* Display = nullptr;

	FBattleDataTableSet CatalogInput() const
	{
		return {Species, Natures, Moves, Abilities, Items, Conditions, Types};
	}
};

template <typename RowType>
UDataTable* JsonTable(FAutomationTestBase& Test, const TCHAR* FileName)
{
	UDataTable* Table = NewObject<UDataTable>(GetTransientPackage());
	Table->RowStruct = RowType::StaticStruct();
	FString Json;
	const FString Path = FPaths::Combine(
		FPaths::ProjectDir(), TEXT("SourceData/Battle/Initial"), FileName);
	if (!Test.TestTrue(*FString::Printf(TEXT("Read %s"), FileName),
		FFileHelper::LoadFileToString(Json, *Path)))
	{
		return Table;
	}
	const TArray<FString> Problems = Table->CreateTableFromJSONString(Json);
	for (const FString& Problem : Problems)
	{
		Test.AddError(FString::Printf(TEXT("%s: %s"), FileName, *Problem));
	}
	return Table;
}

FTables SourceTables(FAutomationTestBase& Test)
{
	return {
		JsonTable<FBattleSpeciesFormTableRow>(Test, TEXT("species_forms.json")),
		JsonTable<FBattleNatureTableRow>(Test, TEXT("natures.json")),
		JsonTable<FBattleMoveTableRow>(Test, TEXT("moves.json")),
		JsonTable<FBattleAbilityTableRow>(Test, TEXT("abilities.json")),
		JsonTable<FBattleItemTableRow>(Test, TEXT("items.json")),
		JsonTable<FBattleConditionTableRow>(Test, TEXT("conditions.json")),
		JsonTable<FBattleTypeChartTableRow>(Test, TEXT("type_chart.json")),
		JsonTable<FBattleDisplayNameTableRow>(Test, TEXT("display_names.json"))};
}

UDataTable* ProductionTable(FAutomationTestBase& Test, const TCHAR* Path)
{
	UDataTable* Table = LoadObject<UDataTable>(nullptr, Path);
	Test.TestNotNull(*FString::Printf(TEXT("Load %s"), Path), Table);
	return Table;
}

FTables ProductionTables(FAutomationTestBase& Test)
{
	return {
		ProductionTable(Test, TEXT("/Game/Data/Battle/Initial/DT_InitialBattleSpeciesForms.DT_InitialBattleSpeciesForms")),
		ProductionTable(Test, TEXT("/Game/Data/Battle/Initial/DT_InitialBattleNatures.DT_InitialBattleNatures")),
		ProductionTable(Test, TEXT("/Game/Data/Battle/Initial/DT_InitialBattleMoves.DT_InitialBattleMoves")),
		ProductionTable(Test, TEXT("/Game/Data/Battle/Initial/DT_InitialBattleAbilities.DT_InitialBattleAbilities")),
		ProductionTable(Test, TEXT("/Game/Data/Battle/Initial/DT_InitialBattleItems.DT_InitialBattleItems")),
		ProductionTable(Test, TEXT("/Game/Data/Battle/Initial/DT_InitialBattleConditions.DT_InitialBattleConditions")),
		ProductionTable(Test, TEXT("/Game/Data/Battle/Initial/DT_InitialBattleTypeChart.DT_InitialBattleTypeChart")),
		ProductionTable(Test, TEXT("/Game/Data/Battle/Initial/DT_InitialBattleDisplayNames.DT_InitialBattleDisplayNames"))};
}

template <typename IdType>
FString IdText(const IdType& Id)
{
	return Id.IsValid() ? Id.GetDefinitionId().GetName().ToString() : TEXT("None");
}

void Field(FString& Out, const TCHAR* Key, const FString& Value)
{
	const FTCHARToUTF8 Bytes(*Value);
	Out += FString::Printf(TEXT("%s=%d:"), Key, Bytes.Length());
	Out += Value;
	Out += TEXT("\n");
}

template <typename ValueType>
void Number(FString& Out, const TCHAR* Key, const ValueType Value)
{
	Field(Out, Key, LexToString(Value));
}

FString CatalogSummary(const FBattleDefinitionCatalog& Catalog)
{
	FString Out;
	Number(Out, TEXT("type.count"), FBattleTypeChart::EntryCount);
	for (int32 A = 0; A < FBattleTypeChart::TypeCount; ++A)
	{
		for (int32 D = 0; D < FBattleTypeChart::TypeCount; ++D)
		{
			FBattleTypeEffectiveness Value;
			const bool bFound = Catalog.GetTypeChart().TryGetEffectiveness(
				static_cast<EPokemonType>(A), static_cast<EPokemonType>(D), Value);
			Number(Out, TEXT("type.a"), A);
			Number(Out, TEXT("type.d"), D);
			Number(Out, TEXT("type.ok"), bFound ? 1 : 0);
			Number(Out, TEXT("type.n"), Value.Numerator);
			Number(Out, TEXT("type.q"), Value.Denominator);
		}
	}
	Number(Out, TEXT("species.count"), Catalog.GetSpeciesForms().Num());
	for (const FBattleSpeciesFormDefinition& Value : Catalog.GetSpeciesForms())
	{
		Field(Out, TEXT("species.id"), IdText(Value.Id));
		Number(Out, TEXT("species.primary"), static_cast<uint8>(Value.PrimaryType));
		Number(Out, TEXT("species.secondary"), static_cast<uint8>(Value.SecondaryType));
		Number(Out, TEXT("species.hp"), Value.BaseStats.HP);
		Number(Out, TEXT("species.attack"), Value.BaseStats.Attack);
		Number(Out, TEXT("species.defense"), Value.BaseStats.Defense);
		Number(Out, TEXT("species.specialAttack"), Value.BaseStats.SpecialAttack);
		Number(Out, TEXT("species.specialDefense"), Value.BaseStats.SpecialDefense);
		Number(Out, TEXT("species.speed"), Value.BaseStats.Speed);
		Number(Out, TEXT("species.catchRate"), Value.CatchRate);
		Number(Out, TEXT("species.abilities.count"), Value.AbilityChoices.Num());
		for (const FAbilityId& Id : Value.AbilityChoices) Field(Out, TEXT("species.ability"), IdText(Id));
	}
	Number(Out, TEXT("nature.count"), Catalog.GetNatures().Num());
	for (const FBattleNatureDefinition& Value : Catalog.GetNatures())
	{
		Field(Out, TEXT("nature.id"), IdText(Value.Id));
		Number(Out, TEXT("nature.boost"), static_cast<uint8>(Value.Modifier.GetBoostedStat()));
		Number(Out, TEXT("nature.reduce"), static_cast<uint8>(Value.Modifier.GetReducedStat()));
	}
	Number(Out, TEXT("move.count"), Catalog.GetMoves().Num());
	for (const FBattleMoveDefinition& Value : Catalog.GetMoves())
	{
		Field(Out, TEXT("move.id"), IdText(Value.Id));
		Number(Out, TEXT("move.type"), static_cast<uint8>(Value.Type));
		Number(Out, TEXT("move.category"), static_cast<uint8>(Value.Category));
		Number(Out, TEXT("move.power"), Value.Power);
		Number(Out, TEXT("move.alwaysHits"), Value.bAlwaysHits ? 1 : 0);
		Number(Out, TEXT("move.accuracy"), Value.Accuracy);
		Number(Out, TEXT("move.usesPP"), Value.bUsesPP ? 1 : 0);
		Number(Out, TEXT("move.basePP"), Value.BasePP);
		Number(Out, TEXT("move.allowsPPBoosts"), Value.bAllowsPPBoosts ? 1 : 0);
		Number(Out, TEXT("move.priority"), Value.Priority);
		Number(Out, TEXT("move.target"), static_cast<uint8>(Value.TargetClass));
		Number(Out, TEXT("move.flags"), static_cast<uint32>(Value.Flags));
		Number(Out, TEXT("move.effects.count"), Value.Effects.Num());
		for (const FBattleMoveEffectDescriptor& Effect : Value.Effects)
		{
			Number(Out, TEXT("effect.order"), Effect.Order);
			Number(Out, TEXT("effect.kind"), static_cast<uint8>(Effect.Kind));
			Number(Out, TEXT("effect.target"), static_cast<uint8>(Effect.Target));
			Field(Out, TEXT("effect.condition"), IdText(Effect.ConditionId));
			Field(Out, TEXT("effect.item"), IdText(Effect.ItemId));
			Number(Out, TEXT("effect.itemOp"), static_cast<uint8>(Effect.HeldItemOperation));
			Number(Out, TEXT("effect.stat"), static_cast<uint8>(Effect.Stat));
			Number(Out, TEXT("effect.chanceN"), Effect.ChanceNumerator);
			Number(Out, TEXT("effect.chanceD"), Effect.ChanceDenominator);
			Number(Out, TEXT("effect.magnitudeN"), Effect.MagnitudeNumerator);
			Number(Out, TEXT("effect.magnitudeD"), Effect.MagnitudeDenominator);
			Number(Out, TEXT("effect.minimum"), Effect.MinimumCount);
			Number(Out, TEXT("effect.maximum"), Effect.MaximumCount);
			Number(Out, TEXT("effect.duration"), Effect.DurationTurns);
			Number(Out, TEXT("effect.layers"), Effect.LayerCount);
			Number(Out, TEXT("effect.flags"), static_cast<uint32>(Effect.Flags));
		}
	}
	Number(Out, TEXT("ability.count"), Catalog.GetAbilities().Num());
	for (const FBattleAbilityDefinition& Value : Catalog.GetAbilities()) Field(Out, TEXT("ability.id"), IdText(Value.Id));
	Number(Out, TEXT("item.count"), Catalog.GetItems().Num());
	for (const FBattleItemDefinition& Value : Catalog.GetItems())
	{
		Field(Out, TEXT("item.id"), IdText(Value.Id));
		Number(Out, TEXT("item.kind"), static_cast<uint8>(Value.Kind));
		Number(Out, TEXT("item.takeable"), Value.bCanBeTakenByMove ? 1 : 0);
	}
	Number(Out, TEXT("condition.count"), Catalog.GetConditions().Num());
	for (const FBattleConditionDefinition& Value : Catalog.GetConditions())
	{
		Field(Out, TEXT("condition.id"), IdText(Value.Id));
		Number(Out, TEXT("condition.kind"), static_cast<uint8>(Value.Kind));
	}
	return Out;
}

uint32 RotateRight(const uint32 Value, const uint32 Count)
{
	return (Value >> Count) | (Value << (32 - Count));
}

FString Sha256(const FString& Value)
{
	static constexpr uint32 Constants[64] = {
		0x428A2F98, 0x71374491, 0xB5C0FBCF, 0xE9B5DBA5, 0x3956C25B, 0x59F111F1, 0x923F82A4, 0xAB1C5ED5,
		0xD807AA98, 0x12835B01, 0x243185BE, 0x550C7DC3, 0x72BE5D74, 0x80DEB1FE, 0x9BDC06A7, 0xC19BF174,
		0xE49B69C1, 0xEFBE4786, 0x0FC19DC6, 0x240CA1CC, 0x2DE92C6F, 0x4A7484AA, 0x5CB0A9DC, 0x76F988DA,
		0x983E5152, 0xA831C66D, 0xB00327C8, 0xBF597FC7, 0xC6E00BF3, 0xD5A79147, 0x06CA6351, 0x14292967,
		0x27B70A85, 0x2E1B2138, 0x4D2C6DFC, 0x53380D13, 0x650A7354, 0x766A0ABB, 0x81C2C92E, 0x92722C85,
		0xA2BFE8A1, 0xA81A664B, 0xC24B8B70, 0xC76C51A3, 0xD192E819, 0xD6990624, 0xF40E3585, 0x106AA070,
		0x19A4C116, 0x1E376C08, 0x2748774C, 0x34B0BCB5, 0x391C0CB3, 0x4ED8AA4A, 0x5B9CCA4F, 0x682E6FF3,
		0x748F82EE, 0x78A5636F, 0x84C87814, 0x8CC70208, 0x90BEFFFA, 0xA4506CEB, 0xBEF9A3F7, 0xC67178F2};
	const FTCHARToUTF8 Bytes(*Value);
	const uint64 BitCount = static_cast<uint64>(Bytes.Length()) * 8;
	TArray<uint8> Padded;
	Padded.Append(reinterpret_cast<const uint8*>(Bytes.Get()), Bytes.Length());
	Padded.Add(0x80);
	while (Padded.Num() % 64 != 56) Padded.Add(0);
	for (int32 Shift = 56; Shift >= 0; Shift -= 8) Padded.Add(static_cast<uint8>(BitCount >> Shift));
	uint32 State[8] = {0x6A09E667, 0xBB67AE85, 0x3C6EF372, 0xA54FF53A, 0x510E527F, 0x9B05688C, 0x1F83D9AB, 0x5BE0CD19};
	for (int32 Offset = 0; Offset < Padded.Num(); Offset += 64)
	{
		uint32 Words[64]{};
		for (int32 Index = 0; Index < 16; ++Index)
		{
			const int32 Start = Offset + Index * 4;
			Words[Index] = uint32(Padded[Start]) << 24 | uint32(Padded[Start + 1]) << 16 | uint32(Padded[Start + 2]) << 8 | Padded[Start + 3];
		}
		for (int32 Index = 16; Index < 64; ++Index)
		{
			const uint32 S0 = RotateRight(Words[Index - 15], 7) ^ RotateRight(Words[Index - 15], 18) ^ (Words[Index - 15] >> 3);
			const uint32 S1 = RotateRight(Words[Index - 2], 17) ^ RotateRight(Words[Index - 2], 19) ^ (Words[Index - 2] >> 10);
			Words[Index] = Words[Index - 16] + S0 + Words[Index - 7] + S1;
		}
		uint32 A = State[0], B = State[1], C = State[2], D = State[3], E = State[4], F = State[5], G = State[6], H = State[7];
		for (int32 Index = 0; Index < 64; ++Index)
		{
			const uint32 S1 = RotateRight(E, 6) ^ RotateRight(E, 11) ^ RotateRight(E, 25);
			const uint32 Temp1 = H + S1 + ((E & F) ^ (~E & G)) + Constants[Index] + Words[Index];
			const uint32 S0 = RotateRight(A, 2) ^ RotateRight(A, 13) ^ RotateRight(A, 22);
			const uint32 Temp2 = S0 + ((A & B) ^ (A & C) ^ (B & C));
			H = G; G = F; F = E; E = D + Temp1; D = C; C = B; B = A; A = Temp1 + Temp2;
		}
		State[0] += A; State[1] += B; State[2] += C; State[3] += D;
		State[4] += E; State[5] += F; State[6] += G; State[7] += H;
	}
	return FString::Printf(TEXT("%08X%08X%08X%08X%08X%08X%08X%08X"), State[0], State[1], State[2], State[3], State[4], State[5], State[6], State[7]);
}

bool Build(FAutomationTestBase& Test, const FTables& Tables, FBattleDefinitionCatalog& Out)
{
	TArray<FBattleCatalogDiagnostic> Diagnostics;
	const bool bBuilt = FBattleDataTableAdapter::BuildCatalog(Tables.CatalogInput(), Out, Diagnostics);
	Test.TestTrue(TEXT("Catalog builds"), bBuilt);
	Test.TestEqual(TEXT("Catalog build diagnostics"), Diagnostics.Num(), 0);
	return bBuilt && Out.IsValid();
}

bool CheckDisplay(FAutomationTestBase& Test, const UDataTable* Table)
{
	static const TPair<const TCHAR*, const TCHAR*> Expected[] = {
		{TEXT("Species.Charizard"), TEXT("Charizard")}, {TEXT("Species.Venusaur"), TEXT("Venusaur")},
		{TEXT("Species.Gyarados"), TEXT("Gyarados")}, {TEXT("Species.Rotom"), TEXT("Rotom")},
		{TEXT("Species.Pelipper"), TEXT("Pelipper")}, {TEXT("Species.Espathra"), TEXT("Espathra")},
		{TEXT("Species.Clefable"), TEXT("Clefable")}, {TEXT("Species.Excadrill"), TEXT("Excadrill")}};
	bool bValid = Test.TestNotNull(TEXT("Display-name table exists"), Table)
		&& Test.TestEqual(TEXT("Eight display-name rows"), Table->GetRowNames().Num(), 8);
	for (const auto& Pair : Expected)
	{
		const FBattleDisplayNameTableRow* Row = Table != nullptr
			? Table->FindRow<FBattleDisplayNameTableRow>(FName(Pair.Key), TEXT("C10B display"), false) : nullptr;
		bValid &= Test.TestTrue(*FString::Printf(TEXT("Display %s"), Pair.Key),
			Row != nullptr && Row->DisplayName.ToString() == Pair.Value);
	}
	return bValid;
}

FString KindName(const EBattleMoveEffectKind Value)
{
	static const TCHAR* Names[] = {TEXT("Damage"), TEXT("ApplyCondition"), TEXT("ModifyStatStage"), TEXT("Heal"), TEXT("Drain"), TEXT("Recoil"), TEXT("MultiHit"), TEXT("SetFieldCondition"), TEXT("SetSideCondition"), TEXT("Switch"), TEXT("ChangeItem"), TEXT("Charge"), TEXT("Recharge"), TEXT("Protect"), TEXT("SemiInvulnerability"), TEXT("RemoveCondition"), TEXT("RegisterTargetRedirection"), TEXT("RegisterAllyActionPowerModifier")};
	const uint8 Index = static_cast<uint8>(Value);
	return Index < UE_ARRAY_COUNT(Names) ? Names[Index] : TEXT("Invalid");
}

FString TargetName(const EBattleEffectTarget Value)
{
	static const TCHAR* Names[] = {TEXT("User"), TEXT("ResolvedTarget"), TEXT("AllResolvedTargets"), TEXT("UserSide"), TEXT("TargetSide"), TEXT("BothSides"), TEXT("Field")};
	const uint8 Index = static_cast<uint8>(Value);
	return Index < UE_ARRAY_COUNT(Names) ? Names[Index] : TEXT("Invalid");
}

FString OperationName(const EBattleMoveHeldItemOperation Value)
{
	static const TCHAR* Names[] = {TEXT("None"), TEXT("RemoveCurrent"), TEXT("ExchangeCurrent"), TEXT("TransferCurrent"), TEXT("RestoreLastConsumed")};
	const uint8 Index = static_cast<uint8>(Value);
	return Index < UE_ARRAY_COUNT(Names) ? Names[Index] : TEXT("Invalid");
}

FString StatName(const EBattleStat Value)
{
	static const TCHAR* Names[] = {TEXT("Attack"), TEXT("Defense"), TEXT("SpecialAttack"), TEXT("SpecialDefense"), TEXT("Speed"), TEXT("Accuracy"), TEXT("Evasion")};
	const uint8 Index = static_cast<uint8>(Value);
	return Index < UE_ARRAY_COUNT(Names) ? Names[Index] : TEXT("None");
}

FString EffectSequence(const FBattleMoveDefinition& Move)
{
	TArray<FString> Parts;
	for (const FBattleMoveEffectDescriptor& E : Move.Effects)
	{
		TArray<FString> Flags;
		if (EnumHasAllFlags(E.Flags, EBattleMoveEffectFlags::BypassesSubstitute)) Flags.Add(TEXT("BypassesSubstitute"));
		if (EnumHasAllFlags(E.Flags, EBattleMoveEffectFlags::UsesActualDamage)) Flags.Add(TEXT("UsesActualDamage"));
		if (EnumHasAllFlags(E.Flags, EBattleMoveEffectFlags::MinimumOne)) Flags.Add(TEXT("MinimumOne"));
		if (EnumHasAllFlags(E.Flags, EBattleMoveEffectFlags::StopOnFaint)) Flags.Add(TEXT("StopOnFaint"));
		if (EnumHasAllFlags(E.Flags, EBattleMoveEffectFlags::PerHit)) Flags.Add(TEXT("PerHit"));
		if (EnumHasAllFlags(E.Flags, EBattleMoveEffectFlags::OptionalIfAbsent)) Flags.Add(TEXT("OptionalIfAbsent"));
		Parts.Add(FString::Printf(TEXT("%d,%s,%s,%s,%s,%s,%s,%d/%d,%d/%d,%d-%d,%d,%d,%s"), E.Order,
			*KindName(E.Kind), *TargetName(E.Target), *IdText(E.ConditionId), *IdText(E.ItemId), *OperationName(E.HeldItemOperation), *StatName(E.Stat),
			E.ChanceNumerator, E.ChanceDenominator, E.MagnitudeNumerator, E.MagnitudeDenominator, E.MinimumCount, E.MaximumCount, E.DurationTurns, E.LayerCount,
			Flags.IsEmpty() ? TEXT("-") : *FString::Join(Flags, TEXT("&"))));
	}
	return FString::Join(Parts, TEXT("|"));
}

template <typename IdType>
IdType MakeId(const TCHAR* Name)
{
	IdType Result;
	check(IdType::TryCreate(FName(Name), Result));
	return Result;
}
bool ExecuteFirstMove(FBattleEngine& Engine)
{
	FBattleRejection Rejection;
	if (!Engine.TryBeginActionDecisionSequence(Rejection)) return false;
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const TArray<FBattleDecisionRequest> Requests = Engine.GetPendingDecisionRequests();
		if (Requests.Num() != 1 || Requests[0].GetLegalMoveTargets().IsEmpty()) return false;
		const FBattleMoveTargetOption& Option = Requests[0].GetLegalMoveTargets()[0];
		FBattleDecision Decision;
		if (!FBattleDecision::TryCreateFight(Requests[0].GetStateVersion(), Requests[0].GetDecisionOwnerTrainerId(), Requests[0].GetActingBattlerId(), Option.MoveId, Option.ActiveSlotId, Decision)
			|| !Engine.SubmitDecision(Decision).WasAccepted()) return false;
	}
	return Engine.BeginNextLockedAction().WasAccepted() && Engine.CommitCurrentMoveAfterPreMoveGates().WasAccepted()
		&& Engine.ResolveCurrentMoveTargets().WasAccepted() && Engine.ExecuteCurrentMoveEffects().WasAccepted();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FC10BCatalogEquivalence,
	"PokemonSolarus.Battle.C10B.ImportAndCatalog.CatalogEquivalenceAndDeterminism",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FC10BCatalogEquivalence::RunTest(const FString& Parameters)
{
	const FTables Source = SourceTables(*this);
	const FTables ProductionA = ProductionTables(*this);
	const FTables ProductionB = ProductionTables(*this);
	FBattleDefinitionCatalog SourceCatalog, CatalogA, CatalogB;
	if (!Build(*this, Source, SourceCatalog) || !Build(*this, ProductionA, CatalogA) || !Build(*this, ProductionB, CatalogB)) return false;
	TestEqual(TEXT("Species inventory"), CatalogA.GetSpeciesForms().Num(), 8);
	TestEqual(TEXT("Nature inventory"), CatalogA.GetNatures().Num(), 25);
	TestEqual(TEXT("Move inventory"), CatalogA.GetMoves().Num(), 62);
	TestEqual(TEXT("Ability inventory"), CatalogA.GetAbilities().Num(), 8);
	TestEqual(TEXT("Item inventory"), CatalogA.GetItems().Num(), 14);
	TestEqual(TEXT("Condition inventory"), CatalogA.GetConditions().Num(), 40);
	CheckDisplay(*this, Source.Display);
	CheckDisplay(*this, ProductionA.Display);
	const FString SourceSummary = CatalogSummary(SourceCatalog);
	const FString SummaryA = CatalogSummary(CatalogA);
	const FString SummaryB = CatalogSummary(CatalogB);
	TestEqual(TEXT("Source and production summaries are byte-identical"), SourceSummary, SummaryA);
	TestEqual(TEXT("Independent production summaries are byte-identical"), SummaryA, SummaryB);
	TestEqual(TEXT("SHA-256 known vector"), Sha256(TEXT("abc")), TEXT("BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD"));
	const FString Hash = Sha256(SummaryA);
	TestEqual(TEXT("Source and production SHA-256 match"), Sha256(SourceSummary), Hash);
	TestEqual(TEXT("Independent production SHA-256 values match"), Sha256(SummaryB), Hash);
	TestTrue(TEXT("SHA-256 is 64 hex characters"), Hash.Len() == 64 && Hash != TEXT("HASH_FAILURE"));
	AddInfo(FString::Printf(TEXT("C10B canonical catalog SHA-256: %s"), *Hash));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FC10BSequencesAndIsolation,
	"PokemonSolarus.Battle.C10B.ImportAndCatalog.CanonicalSequencesAndIsolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FC10BSequencesAndIsolation::RunTest(const FString& Parameters)
{
	const FTables Tables = SourceTables(*this);
	FBattleDefinitionCatalog Catalog;
	if (!Build(*this, Tables, Catalog)) return false;
	static const TPair<const TCHAR*, const TCHAR*> Expected[] = {
		{TEXT("Move.FollowMe"), TEXT("0,RegisterTargetRedirection,User,None,None,None,None,1/1,0/1,0-0,0,0,-")},
		{TEXT("Move.HelpingHand"), TEXT("0,RegisterAllyActionPowerModifier,ResolvedTarget,None,None,None,None,1/1,3/2,0-0,0,0,-")},
		{TEXT("Move.SolarBeam"), TEXT("0,Charge,User,Condition.Charging,None,None,None,1/1,0/1,0-0,0,0,-|1,Damage,ResolvedTarget,None,None,None,None,1/1,0/1,0-0,0,0,-")},
		{TEXT("Move.Fly"), TEXT("0,Charge,User,Condition.Charging,None,None,None,1/1,0/1,0-0,0,0,-|1,SemiInvulnerability,User,Condition.FlySemiInvulnerable,None,None,None,1/1,0/1,0-0,0,0,-|2,Damage,ResolvedTarget,None,None,None,None,1/1,0/1,0-0,0,0,-")},
		{TEXT("Move.Thunder"), TEXT("0,Damage,ResolvedTarget,None,None,None,None,1/1,0/1,0-0,0,0,-|1,ApplyCondition,ResolvedTarget,Condition.Paralysis,None,None,None,30/100,0/1,0-0,0,0,-")},
		{TEXT("Move.KnockOff"), TEXT("0,Damage,ResolvedTarget,None,None,None,None,1/1,0/1,0-0,0,0,-|1,ChangeItem,ResolvedTarget,None,None,RemoveCurrent,None,1/1,0/1,0-0,0,0,-")},
		{TEXT("Move.Trick"), TEXT("0,ChangeItem,ResolvedTarget,None,None,ExchangeCurrent,None,1/1,0/1,0-0,0,0,-")},
		{TEXT("Move.Thief"), TEXT("0,Damage,ResolvedTarget,None,None,None,None,1/1,0/1,0-0,0,0,-|1,ChangeItem,ResolvedTarget,None,None,TransferCurrent,None,1/1,0/1,0-0,0,0,-")},
		{TEXT("Move.Recycle"), TEXT("0,ChangeItem,User,None,None,RestoreLastConsumed,None,1/1,0/1,0-0,0,0,-")},
		{TEXT("Move.RapidSpin"), TEXT("0,Damage,ResolvedTarget,None,None,None,None,1/1,0/1,0-0,0,0,-|1,RemoveCondition,User,Condition.LeechSeed,None,None,None,1/1,0/1,0-0,0,0,OptionalIfAbsent|2,RemoveCondition,User,Condition.PartialTrap,None,None,None,1/1,0/1,0-0,0,0,OptionalIfAbsent|3,RemoveCondition,UserSide,Condition.Spikes,None,None,None,1/1,0/1,0-0,0,0,OptionalIfAbsent|4,RemoveCondition,UserSide,Condition.ToxicSpikes,None,None,None,1/1,0/1,0-0,0,0,OptionalIfAbsent|5,RemoveCondition,UserSide,Condition.StealthRock,None,None,None,1/1,0/1,0-0,0,0,OptionalIfAbsent|6,RemoveCondition,UserSide,Condition.StickyWeb,None,None,None,1/1,0/1,0-0,0,0,OptionalIfAbsent|7,ModifyStatStage,User,None,None,None,Speed,100/100,1/1,0-0,0,0,-")},
		{TEXT("Move.Defog"), TEXT("0,ModifyStatStage,ResolvedTarget,None,None,None,Evasion,1/1,-1/1,0-0,0,0,-|1,RemoveCondition,TargetSide,Condition.Reflect,None,None,None,1/1,0/1,0-0,0,0,OptionalIfAbsent|2,RemoveCondition,TargetSide,Condition.LightScreen,None,None,None,1/1,0/1,0-0,0,0,OptionalIfAbsent|3,RemoveCondition,TargetSide,Condition.AuroraVeil,None,None,None,1/1,0/1,0-0,0,0,OptionalIfAbsent|4,RemoveCondition,TargetSide,Condition.Safeguard,None,None,None,1/1,0/1,0-0,0,0,OptionalIfAbsent|5,RemoveCondition,TargetSide,Condition.Mist,None,None,None,1/1,0/1,0-0,0,0,OptionalIfAbsent|6,RemoveCondition,UserSide,Condition.Spikes,None,None,None,1/1,0/1,0-0,0,0,OptionalIfAbsent|7,RemoveCondition,UserSide,Condition.ToxicSpikes,None,None,None,1/1,0/1,0-0,0,0,OptionalIfAbsent|8,RemoveCondition,UserSide,Condition.StealthRock,None,None,None,1/1,0/1,0-0,0,0,OptionalIfAbsent|9,RemoveCondition,UserSide,Condition.StickyWeb,None,None,None,1/1,0/1,0-0,0,0,OptionalIfAbsent|10,RemoveCondition,TargetSide,Condition.Spikes,None,None,None,1/1,0/1,0-0,0,0,OptionalIfAbsent|11,RemoveCondition,TargetSide,Condition.ToxicSpikes,None,None,None,1/1,0/1,0-0,0,0,OptionalIfAbsent|12,RemoveCondition,TargetSide,Condition.StealthRock,None,None,None,1/1,0/1,0-0,0,0,OptionalIfAbsent|13,RemoveCondition,TargetSide,Condition.StickyWeb,None,None,None,1/1,0/1,0-0,0,0,OptionalIfAbsent|14,RemoveCondition,Field,Condition.ElectricTerrain,None,None,None,1/1,0/1,0-0,0,0,OptionalIfAbsent|15,RemoveCondition,Field,Condition.GrassyTerrain,None,None,None,1/1,0/1,0-0,0,0,OptionalIfAbsent|16,RemoveCondition,Field,Condition.MistyTerrain,None,None,None,1/1,0/1,0-0,0,0,OptionalIfAbsent|17,RemoveCondition,Field,Condition.PsychicTerrain,None,None,None,1/1,0/1,0-0,0,0,OptionalIfAbsent")},
		{TEXT("Move.BrickBreak"), TEXT("0,RemoveCondition,TargetSide,Condition.Reflect,None,None,None,1/1,0/1,0-0,0,0,OptionalIfAbsent|1,RemoveCondition,TargetSide,Condition.LightScreen,None,None,None,1/1,0/1,0-0,0,0,OptionalIfAbsent|2,RemoveCondition,TargetSide,Condition.AuroraVeil,None,None,None,1/1,0/1,0-0,0,0,OptionalIfAbsent|3,Damage,ResolvedTarget,None,None,None,None,1/1,0/1,0-0,0,0,-")}};
	for (const auto& Pair : Expected)
	{
		const FBattleMoveDefinition* Move = Catalog.FindMove(MakeId<FMoveId>(Pair.Key));
		TestTrue(*FString::Printf(TEXT("Exact sequence %s"), Pair.Key), Move != nullptr && EffectSequence(*Move) == Pair.Value);
	}
	FBattleRuntimeBundle RuntimeBundle;
	FString RuntimeError;
	FBattleDataTableRuntimeSource RuntimeSource(TSoftObjectPtr<UDataTable>(FSoftObjectPath(
		TEXT("/Game/Data/Battle/Initial/DT_BattleRuntimeScenario.DT_BattleRuntimeScenario"))));
	if (!TestTrue(TEXT("Production setup is available for battle-copy isolation"),
		RuntimeSource.TryCreateInitialBattle(RuntimeBundle, RuntimeError)))
	{
		AddError(RuntimeError);
		return false;
	}
	TUniquePtr<FBattleEngine> Battle;
	FBattleRejection Rejection;
	if (!TestTrue(TEXT("Transient catalog is copied into a battle before source mutation"),
		FBattleEngine::TryCreate(RuntimeBundle.Engine->ExportReplayInputs().Setup, Catalog,
			MakeUnique<FSeededBattleRandom>(23449611999008083ULL), Battle, Rejection)))
	{
		return false;
	}
	FBattlerId TargetId; check(FBattlerId::TryCreate(21, TargetId));
	if (!TestTrue(TEXT("Baseline battle executes the original first move"), ExecuteFirstMove(*RuntimeBundle.Engine))) return false;
	const FBattlePartyEntrySetup* ExpectedTarget = RuntimeBundle.Engine->GetSnapshot().FindBattler(TargetId);
	if (!TestNotNull(TEXT("Baseline target remains available"), ExpectedTarget)) return false;
	const int32 ExpectedTargetHP = ExpectedTarget->CurrentHP;
	const FString Before = CatalogSummary(Catalog);
	FBattleMoveTableRow* SourceMove = Tables.Moves->FindRow<FBattleMoveTableRow>(FName(TEXT("Move.Flamethrower")), TEXT("C10B isolation"), false);
	TestNotNull(TEXT("Mutable source move exists"), SourceMove);
	if (SourceMove != nullptr) { SourceMove->Power = 999; SourceMove->Effects[0].Order = 99; }
	TestEqual(TEXT("Frozen catalog ignores later source-table mutation"), CatalogSummary(Catalog), Before);
	Catalog = FBattleDefinitionCatalog();
	if (!TestTrue(TEXT("Battle executes after source and adapter copies are discarded"), ExecuteFirstMove(*Battle))) return false;
	const FBattlePartyEntrySetup* ActualTarget = Battle->GetSnapshot().FindBattler(TargetId);
	TestTrue(TEXT("Battle-owned catalog retains original Flamethrower power behavior"),
		ActualTarget != nullptr && ActualTarget->CurrentHP == ExpectedTargetHP);
	return true;
}

struct FBadFixture
{
	const TCHAR* Name;
	TFunction<void(FTables&)> Mutate;
	EBattleCatalogDiagnosticCode Code;
	EBattleDefinitionFamily Family;
	FName DefinitionId;
	FName Field;
	int32 EntryIndex;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FC10BDiagnostics,
	"PokemonSolarus.Battle.C10B.ImportAndCatalog.AtomicDiagnostics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FC10BDiagnostics::RunTest(const FString& Parameters)
{
	const FBadFixture Fixtures[] = {
		{TEXT("duplicate"), [](FTables& T) { auto* R = T.Types->FindRow<FBattleTypeChartTableRow>(FName(TEXT("Normal")), TEXT("duplicate"), false); R->Entries[1].DefendingType = R->Entries[0].DefendingType; }, EBattleCatalogDiagnosticCode::DuplicateTypeChartEntry, EBattleDefinitionFamily::TypeChart, NAME_None, FName(TEXT("Entries")), INDEX_NONE},
		{TEXT("missing"), [](FTables& T) { T.Abilities->RemoveRow(FName(TEXT("Ability.Blaze"))); }, EBattleCatalogDiagnosticCode::MissingReference, EBattleDefinitionFamily::SpeciesForm, FName(TEXT("Species.Charizard")), FName(TEXT("AbilityChoices")), 0},
		{TEXT("wrong-family"), [](FTables& T) { T.Moves = NewObject<UDataTable>(GetTransientPackage()); T.Moves->RowStruct = FBattleAbilityTableRow::StaticStruct(); }, EBattleCatalogDiagnosticCode::WrongRowType, EBattleDefinitionFamily::Move, NAME_None, FName(TEXT("Moves")), INDEX_NONE},
		{TEXT("unknown-enum"), [](FTables& T) { T.Moves->FindRow<FBattleMoveTableRow>(FName(TEXT("Move.Flamethrower")), TEXT("enum"), false)->Type = FName(TEXT("UnknownType")); }, EBattleCatalogDiagnosticCode::InvalidAuthoredValue, EBattleDefinitionFamily::Move, FName(TEXT("Move.Flamethrower")), FName(TEXT("Type")), INDEX_NONE},
		{TEXT("bad-range"), [](FTables& T) { T.Moves->FindRow<FBattleMoveTableRow>(FName(TEXT("Move.Flamethrower")), TEXT("range"), false)->Accuracy = 101; }, EBattleCatalogDiagnosticCode::InvalidRange, EBattleDefinitionFamily::Move, FName(TEXT("Move.Flamethrower")), FName(TEXT("Accuracy")), INDEX_NONE},
		{TEXT("bad-target"), [](FTables& T) { T.Moves->FindRow<FBattleMoveTableRow>(FName(TEXT("Move.Flamethrower")), TEXT("target"), false)->Effects[0].Target = FName(TEXT("User")); }, EBattleCatalogDiagnosticCode::IncompatibleEffect, EBattleDefinitionFamily::Move, FName(TEXT("Move.Flamethrower")), FName(TEXT("Effects.Target")), 0},
		{TEXT("malformed-effect"), [](FTables& T) { auto* M = T.Moves->FindRow<FBattleMoveTableRow>(FName(TEXT("Move.Flamethrower")), TEXT("order"), false); M->Effects[1].Order = M->Effects[0].Order; }, EBattleCatalogDiagnosticCode::InvalidEffectOrder, EBattleDefinitionFamily::Move, FName(TEXT("Move.Flamethrower")), FName(TEXT("Effects.Order")), 1}};
	for (const FBadFixture& Fixture : Fixtures)
	{
		FTables Tables = SourceTables(*this);
		Fixture.Mutate(Tables);
		FBattleDefinitionCatalog Catalog;
		TArray<FBattleCatalogDiagnostic> Diagnostics;
		TestFalse(FString::Printf(TEXT("%s fixture fails"), Fixture.Name),
			FBattleDataTableAdapter::BuildCatalog(Tables.CatalogInput(), Catalog, Diagnostics));
		TestFalse(FString::Printf(TEXT("%s has no partial catalog"), Fixture.Name), Catalog.IsValid());
		TestTrue(FString::Printf(TEXT("%s precise diagnostic"), Fixture.Name), Diagnostics.ContainsByPredicate([&Fixture](const FBattleCatalogDiagnostic& D)
			{
				const bool bIdMatches = Fixture.DefinitionId.IsNone()
					? !D.DefinitionId.IsValid()
					: D.DefinitionId.IsValid() && D.DefinitionId.GetName() == Fixture.DefinitionId;
				return D.Code == Fixture.Code && D.Family == Fixture.Family
					&& bIdMatches && D.Field == Fixture.Field && D.EntryIndex == Fixture.EntryIndex;
			}));
	}
	return true;
}
}

#endif // WITH_DEV_AUTOMATION_TESTS
