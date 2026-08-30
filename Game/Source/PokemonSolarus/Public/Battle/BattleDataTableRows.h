#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "BattleDataTableRows.generated.h"

/** Reflected authored effect payload used only while importing a move row. */
USTRUCT(BlueprintType)
struct POKEMONSOLARUS_API FBattleMoveEffectTableRow
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Battle")
	int32 Order = 0;

	UPROPERTY(EditAnywhere, Category = "Battle")
	FName Kind = NAME_None;

	UPROPERTY(EditAnywhere, Category = "Battle")
	FName Target = NAME_None;

	UPROPERTY(EditAnywhere, Category = "Battle")
	FName ConditionId = NAME_None;

	UPROPERTY(EditAnywhere, Category = "Battle")
	FName ItemId = NAME_None;

	UPROPERTY(EditAnywhere, Category = "Battle")
	FName HeldItemOperation = NAME_None;

	UPROPERTY(EditAnywhere, Category = "Battle")
	FName Stat = NAME_None;

	UPROPERTY(EditAnywhere, Category = "Battle")
	int32 ChanceNumerator = 1;

	UPROPERTY(EditAnywhere, Category = "Battle")
	int32 ChanceDenominator = 1;

	UPROPERTY(EditAnywhere, Category = "Battle")
	int32 MagnitudeNumerator = 0;

	UPROPERTY(EditAnywhere, Category = "Battle")
	int32 MagnitudeDenominator = 1;

	UPROPERTY(EditAnywhere, Category = "Battle")
	int32 MinimumCount = 0;

	UPROPERTY(EditAnywhere, Category = "Battle")
	int32 MaximumCount = 0;

	UPROPERTY(EditAnywhere, Category = "Battle")
	int32 DurationTurns = 0;

	UPROPERTY(EditAnywhere, Category = "Battle")
	int32 LayerCount = 0;

	UPROPERTY(EditAnywhere, Category = "Battle")
	TArray<FName> Flags;
};

/** Reflected species/form row; the Data Table row Name supplies the stable ID. */
USTRUCT(BlueprintType)
struct POKEMONSOLARUS_API FBattleSpeciesFormTableRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Battle")
	FName PrimaryType = NAME_None;

	UPROPERTY(EditAnywhere, Category = "Battle")
	FName SecondaryType = NAME_None;

	UPROPERTY(EditAnywhere, Category = "Battle")
	int32 BaseHP = 0;

	UPROPERTY(EditAnywhere, Category = "Battle")
	int32 BaseAttack = 0;

	UPROPERTY(EditAnywhere, Category = "Battle")
	int32 BaseDefense = 0;

	UPROPERTY(EditAnywhere, Category = "Battle")
	int32 BaseSpecialAttack = 0;

	UPROPERTY(EditAnywhere, Category = "Battle")
	int32 BaseSpecialDefense = 0;

	UPROPERTY(EditAnywhere, Category = "Battle")
	int32 BaseSpeed = 0;

	UPROPERTY(EditAnywhere, Category = "Battle")
	int32 CatchRate = 0;

	UPROPERTY(EditAnywhere, Category = "Battle")
	TArray<FName> AbilityIds;
};

/** Reflected nature row; the Data Table row Name supplies the stable ID. */
USTRUCT(BlueprintType)
struct POKEMONSOLARUS_API FBattleNatureTableRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Battle")
	FName BoostedStat = NAME_None;

	UPROPERTY(EditAnywhere, Category = "Battle")
	FName ReducedStat = NAME_None;
};

/** Reflected move row with JSON-friendly ordered nested effect descriptors. */
USTRUCT(BlueprintType)
struct POKEMONSOLARUS_API FBattleMoveTableRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Battle")
	FName Type = NAME_None;

	UPROPERTY(EditAnywhere, Category = "Battle")
	FName Category = NAME_None;

	UPROPERTY(EditAnywhere, Category = "Battle")
	int32 Power = 0;

	UPROPERTY(EditAnywhere, Category = "Battle")
	bool bAlwaysHits = false;

	UPROPERTY(EditAnywhere, Category = "Battle")
	int32 Accuracy = 0;

	UPROPERTY(EditAnywhere, Category = "Battle")
	bool bUsesPP = true;

	UPROPERTY(EditAnywhere, Category = "Battle")
	int32 BasePP = 0;

	UPROPERTY(EditAnywhere, Category = "Battle")
	bool bAllowsPPBoosts = true;

	UPROPERTY(EditAnywhere, Category = "Battle")
	int32 Priority = 0;

	UPROPERTY(EditAnywhere, Category = "Battle")
	FName TargetClass = NAME_None;

	UPROPERTY(EditAnywhere, Category = "Battle")
	TArray<FName> Flags;

	UPROPERTY(EditAnywhere, Category = "Battle")
	TArray<FBattleMoveEffectTableRow> Effects;
};

/** Reflected Ability identity row; later packages may add authored rule fields. */
USTRUCT(BlueprintType)
struct POKEMONSOLARUS_API FBattleAbilityTableRow : public FTableRowBase
{
	GENERATED_BODY()
};

/** Reflected item row; the Data Table row Name supplies the stable ID. */
USTRUCT(BlueprintType)
struct POKEMONSOLARUS_API FBattleItemTableRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Battle")
	FName Kind = NAME_None;

	UPROPERTY(EditAnywhere, Category = "Battle")
	bool bCanBeTakenByMove = true;
};

/** Reflected condition row; the Data Table row Name supplies the stable ID. */
USTRUCT(BlueprintType)
struct POKEMONSOLARUS_API FBattleConditionTableRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Battle")
	FName Kind = NAME_None;
};

/** One reflected defending-type cell nested under an attacking-type row. */
USTRUCT(BlueprintType)
struct POKEMONSOLARUS_API FBattleTypeChartCellTableRow
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Battle")
	FName DefendingType = NAME_None;

	UPROPERTY(EditAnywhere, Category = "Battle")
	int32 Numerator = 0;

	UPROPERTY(EditAnywhere, Category = "Battle")
	int32 Denominator = 1;
};

/** Reflected attacking-type row containing the complete ordered defending-type cells. */
USTRUCT(BlueprintType)
struct POKEMONSOLARUS_API FBattleTypeChartTableRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Battle")
	TArray<FBattleTypeChartCellTableRow> Entries;
};
