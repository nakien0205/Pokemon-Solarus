#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "BattleRuntimeDataTableRows.generated.h"

/** Localized display text keyed by the Data Table row's stable definition ID. */
USTRUCT(BlueprintType)
struct POKEMONSOLARUS_API FBattleDisplayNameTableRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Battle")
	FText DisplayName;
};

/** Reflected six-stat authoring block used for IVs and EVs. */
USTRUCT(BlueprintType)
struct POKEMONSOLARUS_API FBattleRuntimeStatValuesTableEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Battle")
	int32 HP = 0;

	UPROPERTY(EditAnywhere, Category = "Battle")
	int32 Attack = 0;

	UPROPERTY(EditAnywhere, Category = "Battle")
	int32 Defense = 0;

	UPROPERTY(EditAnywhere, Category = "Battle")
	int32 SpecialAttack = 0;

	UPROPERTY(EditAnywhere, Category = "Battle")
	int32 SpecialDefense = 0;

	UPROPERTY(EditAnywhere, Category = "Battle")
	int32 Speed = 0;
};

/** One authored Bag count copied into a Trainer setup. */
USTRUCT(BlueprintType)
struct POKEMONSOLARUS_API FBattleRuntimeBagItemTableEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Battle")
	FName ItemId = NAME_None;

	UPROPERTY(EditAnywhere, Category = "Battle")
	int32 Count = 0;
};

/** One authored Trainer and selector assignment. */
USTRUCT(BlueprintType)
struct POKEMONSOLARUS_API FBattleRuntimeTrainerTableEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Battle")
	FString TrainerId;

	UPROPERTY(EditAnywhere, Category = "Battle")
	FName Side = NAME_None;

	UPROPERTY(EditAnywhere, Category = "Battle")
	FName Role = NAME_None;

	UPROPERTY(EditAnywhere, Category = "Battle")
	FName Controller = NAME_None;

	UPROPERTY(EditAnywhere, Category = "Battle")
	FName SelectorProfileId = NAME_None;

	UPROPERTY(EditAnywhere, Category = "Battle")
	TArray<FBattleRuntimeBagItemTableEntry> Bag;
};

/** One authored move slot; PP is derived from catalog BasePP and PP Ups. */
USTRUCT(BlueprintType)
struct POKEMONSOLARUS_API FBattleRuntimeMoveTableEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Battle")
	int32 SlotIndex = INDEX_NONE;

	UPROPERTY(EditAnywhere, Category = "Battle")
	FName MoveId = NAME_None;

	UPROPERTY(EditAnywhere, Category = "Battle")
	int32 PPUps = 0;
};

/** One authored party Pokemon; permanent stats and starting HP are derived. */
USTRUCT(BlueprintType)
struct POKEMONSOLARUS_API FBattleRuntimePartyEntryTableEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Battle")
	FString TrainerId;

	UPROPERTY(EditAnywhere, Category = "Battle")
	FString BattlerId;

	UPROPERTY(EditAnywhere, Category = "Battle")
	FString SourcePokemonId;

	UPROPERTY(EditAnywhere, Category = "Battle")
	int32 PartySlotIndex = INDEX_NONE;

	UPROPERTY(EditAnywhere, Category = "Battle")
	FName SpeciesFormId = NAME_None;

	UPROPERTY(EditAnywhere, Category = "Battle")
	int32 Level = 0;

	UPROPERTY(EditAnywhere, Category = "Battle")
	FName NatureId = NAME_None;

	UPROPERTY(EditAnywhere, Category = "Battle")
	FBattleRuntimeStatValuesTableEntry IndividualValues;

	UPROPERTY(EditAnywhere, Category = "Battle")
	FBattleRuntimeStatValuesTableEntry EffortValues;

	UPROPERTY(EditAnywhere, Category = "Battle")
	bool bEgg = false;

	UPROPERTY(EditAnywhere, Category = "Battle")
	FName AbilityId = NAME_None;

	UPROPERTY(EditAnywhere, Category = "Battle")
	FName OriginalHeldItemId = NAME_None;

	UPROPERTY(EditAnywhere, Category = "Battle")
	FName CurrentHeldItemId = NAME_None;

	UPROPERTY(EditAnywhere, Category = "Battle")
	TArray<FBattleRuntimeMoveTableEntry> Moves;
};

/** One authored active-slot assignment. */
USTRUCT(BlueprintType)
struct POKEMONSOLARUS_API FBattleRuntimeActiveAssignmentTableEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Battle")
	FName Side = NAME_None;

	UPROPERTY(EditAnywhere, Category = "Battle")
	FName Position = NAME_None;

	UPROPERTY(EditAnywhere, Category = "Battle")
	FString TrainerId;

	UPROPERTY(EditAnywhere, Category = "Battle")
	FString BattlerId;
};

/** One authored standard-obedience input. */
USTRUCT(BlueprintType)
struct POKEMONSOLARUS_API FBattleRuntimeObedienceTableEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Battle")
	FString BattlerId;

	UPROPERTY(EditAnywhere, Category = "Battle")
	bool bSubjectToPlayerCap = false;

	UPROPERTY(EditAnywhere, Category = "Battle")
	int32 ReferenceLevel = 0;

	UPROPERTY(EditAnywhere, Category = "Battle")
	int32 BadgeCount = 0;
};

/** Reflected encounter switches copied into immutable setup policies. */
USTRUCT(BlueprintType)
struct POKEMONSOLARUS_API FBattleRuntimeEncounterPoliciesTableEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Battle")
	bool bRunAllowed = false;

	UPROPERTY(EditAnywhere, Category = "Battle")
	bool bCaptureAllowed = false;

	UPROPERTY(EditAnywhere, Category = "Battle")
	bool bBagAllowed = true;

	UPROPERTY(EditAnywhere, Category = "Battle")
	bool bShiftPromptEligible = true;

	UPROPERTY(EditAnywhere, Category = "Battle")
	FName WildFleeMode = NAME_None;

	UPROPERTY(EditAnywhere, Category = "Battle")
	int64 WildFleeNumerator = 0;

	UPROPERTY(EditAnywhere, Category = "Battle")
	int64 WildFleeDenominator = 0;
};

/** One composition-root row for a complete local Battle runtime. */
USTRUCT(BlueprintType)
struct POKEMONSOLARUS_API FBattleRuntimeScenarioTableRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Battle|Catalog")
	TSoftObjectPtr<UDataTable> SpeciesForms;

	UPROPERTY(EditAnywhere, Category = "Battle|Catalog")
	TSoftObjectPtr<UDataTable> Natures;

	UPROPERTY(EditAnywhere, Category = "Battle|Catalog")
	TSoftObjectPtr<UDataTable> Moves;

	UPROPERTY(EditAnywhere, Category = "Battle|Catalog")
	TSoftObjectPtr<UDataTable> Abilities;

	UPROPERTY(EditAnywhere, Category = "Battle|Catalog")
	TSoftObjectPtr<UDataTable> Items;

	UPROPERTY(EditAnywhere, Category = "Battle|Catalog")
	TSoftObjectPtr<UDataTable> Conditions;

	UPROPERTY(EditAnywhere, Category = "Battle|Catalog")
	TSoftObjectPtr<UDataTable> TypeChart;

	UPROPERTY(EditAnywhere, Category = "Battle|Presentation")
	TSoftObjectPtr<UDataTable> DisplayNames;

	UPROPERTY(EditAnywhere, Category = "Battle|Identity")
	FString Seed;

	UPROPERTY(EditAnywhere, Category = "Battle|Identity")
	FString BattleId;

	UPROPERTY(EditAnywhere, Category = "Battle|Identity")
	FString LocalTrainerId;

	UPROPERTY(EditAnywhere, Category = "Battle|Identity")
	FName SettingsSnapshotId = NAME_None;

	UPROPERTY(EditAnywhere, Category = "Battle|Identity")
	int32 SettingsSchemaVersion = 0;

	UPROPERTY(EditAnywhere, Category = "Battle|Identity")
	FName CatalogSnapshotId = NAME_None;

	UPROPERTY(EditAnywhere, Category = "Battle|Identity")
	int32 CatalogSchemaVersion = 0;

	UPROPERTY(EditAnywhere, Category = "Battle|Setup")
	FName EncounterKind = NAME_None;

	UPROPERTY(EditAnywhere, Category = "Battle|Setup")
	FName Format = NAME_None;

	UPROPERTY(EditAnywhere, Category = "Battle|Setup")
	int32 PartySlotsRemaining = 0;

	UPROPERTY(EditAnywhere, Category = "Battle|Setup")
	int32 StorageSlotsRemaining = 0;

	UPROPERTY(EditAnywhere, Category = "Battle|Setup")
	FBattleRuntimeEncounterPoliciesTableEntry Policies;

	UPROPERTY(EditAnywhere, Category = "Battle|Setup")
	TArray<FBattleRuntimeTrainerTableEntry> Trainers;

	UPROPERTY(EditAnywhere, Category = "Battle|Setup")
	TArray<FBattleRuntimePartyEntryTableEntry> PartyEntries;

	UPROPERTY(EditAnywhere, Category = "Battle|Setup")
	TArray<FBattleRuntimeActiveAssignmentTableEntry> StartingActive;

	UPROPERTY(EditAnywhere, Category = "Battle|Setup")
	TArray<FBattleRuntimeObedienceTableEntry> ObedienceInputs;
};
