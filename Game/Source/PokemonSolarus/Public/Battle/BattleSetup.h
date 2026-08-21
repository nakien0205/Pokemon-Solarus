#pragma once

#include "CoreMinimal.h"
#include "Battle/BattleIdentifiers.h"
#include "Battle/BattleSetupTypes.h"
#include "Battle/BattleStats.h"

/** Stable Trainer role inside one frozen battle setup. */
enum class EBattleTrainerRole : uint8
{
	Player = 0,
	Partner = 1,
	Opponent = 2
};

/** Decision source frozen when a battle begins. */
enum class EBattleDecisionController : uint8
{
	Human = 0,
	PartnerAI = 1,
	EnemyAI = 2,
	Scripted = 3
};

/** Explicit wild-flee probability policy; Disabled is the default. */
enum class EBattleWildFleeMode : uint8
{
	Disabled = 0,
	Never = 1,
	Always = 2,
	Chance = 3
};

/** Kind of frozen knowledge visible to one observing Trainer. */
enum class EBattleKnowledgeKind : uint8
{
	SpeciesFormKnown = 0,
	MoveRevealed = 1,
	AbilityRevealed = 2,
	ItemRevealed = 3
};

/** Stable setup-validation result. It is never display text. */
enum class EBattleSetupValidationError : uint8
{
	None = 0,
	InvalidIdentity = 1,
	InvalidReference = 2,
	InvalidEnum = 3,
	TrainerShape = 4,
	DuplicateIdentity = 5,
	PartyShape = 6,
	InvalidPartyEntry = 7,
	InvalidResource = 8,
	ActiveSlotShape = 9,
	TrainerOwnership = 10,
	InvalidKnowledge = 11,
	InvalidObedience = 12,
	InvalidEncounterPolicy = 13
};

/** Immutable content/settings snapshot identity used across replay boundaries. */
struct POKEMONSOLARUS_API FBattleSnapshotReference
{
	FDefinitionId SnapshotId;
	uint32 SchemaVersion = 0;

	/** Returns whether both the stable ID and schema version are usable. */
	[[nodiscard]] bool IsValid() const
	{
		return SnapshotId.IsValid() && SchemaVersion > 0;
	}
};

/** One finite Bag count owned by one Trainer. */
struct POKEMONSOLARUS_API FBattleBagItemCount
{
	FItemId ItemId;
	int32 Count = 0;
};

/** Frozen Trainer ownership and selector facts. */
struct POKEMONSOLARUS_API FBattleTrainerSetup
{
	FTrainerId TrainerId;
	EBattleSide Side = EBattleSide::Player;
	EBattleTrainerRole Role = EBattleTrainerRole::Player;
	EBattleDecisionController Controller = EBattleDecisionController::Human;
	FDefinitionId SelectorProfileId;
	TArray<FBattleBagItemCount> Bag;
};

/** One frozen move slot copied into a battle setup. */
struct POKEMONSOLARUS_API FBattleMoveSlotSetup
{
	uint8 SlotIndex = 255;
	FMoveId MoveId;
	int32 CurrentPP = 0;
	int32 MaxPP = 0;
};

/** One immutable battle-entry party record. */
struct POKEMONSOLARUS_API FBattlePartyEntrySetup
{
	FTrainerId TrainerId;
	FBattlerId BattlerId;
	FSourcePokemonId SourcePokemonId;
	FPartySlotId PartySlotId;
	FSpeciesFormId SpeciesFormId;
	int32 Level = 0;
	FPokemonBattleStats Stats;
	int32 CurrentHP = 0;
	bool bEgg = false;
	FAbilityId AbilityId;
	FItemId OriginalHeldItemId;
	FItemId CurrentHeldItemId;
	TArray<FBattleMoveSlotSetup> Moves;
};

/** Initial assignment of a living party battler to a structural active slot. */
struct POKEMONSOLARUS_API FBattleActiveAssignment
{
	FActiveSlotId ActiveSlotId;
	FTrainerId TrainerId;
	FBattlerId BattlerId;
};

/** Remaining party and storage capacity frozen for capture validation. */
struct POKEMONSOLARUS_API FBattleCaptureCapacitySnapshot
{
	int32 PartySlotsRemaining = 0;
	int32 StorageSlotsRemaining = 0;
};

/** One typed visibility/knowledge fact frozen at battle start. */
struct POKEMONSOLARUS_API FBattleKnowledgeFact
{
	FTrainerId ObserverTrainerId;
	FBattlerId SubjectBattlerId;
	EBattleKnowledgeKind Kind = EBattleKnowledgeKind::SpeciesFormKnown;
	FDefinitionId DefinitionId;
	EBattleVisibilityLevel Visibility = EBattleVisibilityLevel::OwningTrainer;
};

/** Standard-obedience facts copied from progression state. */
struct POKEMONSOLARUS_API FBattleObedienceInput
{
	FBattlerId BattlerId;
	bool bSubjectToPlayerCap = false;
	uint8 ReferenceLevel = 0;
	uint8 BadgeCount = 0;
};

/** Encounter switches and optional authored wild-flee chance. */
struct POKEMONSOLARUS_API FBattleEncounterPolicies
{
	bool bRunAllowed = false;
	bool bCaptureAllowed = false;
	bool bBagAllowed = true;
	bool bShiftPromptEligible = true;
	EBattleWildFleeMode WildFleeMode = EBattleWildFleeMode::Disabled;
	uint32 WildFleeNumerator = 0;
	uint32 WildFleeDenominator = 0;
};

/** Mutable construction input that is validated and canonicalized into FBattleSetup. */
struct POKEMONSOLARUS_API FBattleSetupInput
{
	FBattleId BattleId;
	FBattleSnapshotReference SettingsReference;
	FBattleSnapshotReference CatalogReference;
	EBattleEncounterKind EncounterKind = EBattleEncounterKind::Wild;
	EBattleFormat Format = EBattleFormat::Single;
	TArray<FBattleTrainerSetup> Trainers;
	TArray<FBattlePartyEntrySetup> PartyEntries;
	TArray<FBattleActiveAssignment> StartingActive;
	FBattleCaptureCapacitySnapshot CaptureCapacity;
	TArray<FBattleKnowledgeFact> KnowledgeFacts;
	TArray<FBattleObedienceInput> ObedienceInputs;
	FBattleEncounterPolicies Policies;
};

/**
 * Fully validated immutable-by-interface battle setup.
 * Construction canonicalizes every order-insensitive collection.
 */
class POKEMONSOLARUS_API FBattleSetup
{
public:
	/** Creates an invalid setup. */
	FBattleSetup() = default;

	/** Validates and canonicalizes all setup facts atomically. */
	[[nodiscard]] static bool TryCreate(
		const FBattleSetupInput& Input,
		FBattleSetup& OutSetup,
		EBattleSetupValidationError& OutError);

	/** Returns whether this object was produced by successful validation. */
	[[nodiscard]] bool IsValid() const
	{
		return bValid;
	}

	/** Returns the stable battle identity. */
	[[nodiscard]] FBattleId GetBattleId() const
	{
		return BattleId;
	}

	/** Returns the frozen settings reference. */
	[[nodiscard]] const FBattleSnapshotReference& GetSettingsReference() const
	{
		return SettingsReference;
	}

	/** Returns the frozen catalog reference. */
	[[nodiscard]] const FBattleSnapshotReference& GetCatalogReference() const
	{
		return CatalogReference;
	}

	/** Returns the encounter family. */
	[[nodiscard]] EBattleEncounterKind GetEncounterKind() const
	{
		return EncounterKind;
	}

	/** Returns the active-slot format. */
	[[nodiscard]] EBattleFormat GetFormat() const
	{
		return Format;
	}

	/** Returns Trainers in canonical numeric-ID order. */
	[[nodiscard]] TConstArrayView<FBattleTrainerSetup> GetTrainers() const
	{
		return Trainers;
	}

	/** Returns party entries in Trainer then party-slot order. */
	[[nodiscard]] TConstArrayView<FBattlePartyEntrySetup> GetPartyEntries() const
	{
		return PartyEntries;
	}

	/** Returns starting actives in side then Left/Right order. */
	[[nodiscard]] TConstArrayView<FBattleActiveAssignment> GetStartingActive() const
	{
		return StartingActive;
	}

	/** Returns the frozen capture-capacity facts. */
	[[nodiscard]] const FBattleCaptureCapacitySnapshot& GetCaptureCapacity() const
	{
		return CaptureCapacity;
	}

	/** Returns canonical frozen observer knowledge. */
	[[nodiscard]] TConstArrayView<FBattleKnowledgeFact> GetKnowledgeFacts() const
	{
		return KnowledgeFacts;
	}

	/** Returns canonical standard-obedience facts. */
	[[nodiscard]] TConstArrayView<FBattleObedienceInput> GetObedienceInputs() const
	{
		return ObedienceInputs;
	}

	/** Returns the frozen encounter policies. */
	[[nodiscard]] const FBattleEncounterPolicies& GetPolicies() const
	{
		return Policies;
	}

	/** Finds one Trainer by stable identity, or returns null. */
	[[nodiscard]] const FBattleTrainerSetup* FindTrainer(FTrainerId TrainerId) const;

	/** Finds one battler setup by stable identity, or returns null. */
	[[nodiscard]] const FBattlePartyEntrySetup* FindBattler(FBattlerId BattlerId) const;

	/** Finds one initial active assignment, or returns null. */
	[[nodiscard]] const FBattleActiveAssignment* FindActive(FActiveSlotId ActiveSlotId) const;

private:
	bool bValid = false;
	FBattleId BattleId;
	FBattleSnapshotReference SettingsReference;
	FBattleSnapshotReference CatalogReference;
	EBattleEncounterKind EncounterKind = EBattleEncounterKind::Wild;
	EBattleFormat Format = EBattleFormat::Single;
	TArray<FBattleTrainerSetup> Trainers;
	TArray<FBattlePartyEntrySetup> PartyEntries;
	TArray<FBattleActiveAssignment> StartingActive;
	FBattleCaptureCapacitySnapshot CaptureCapacity;
	TArray<FBattleKnowledgeFact> KnowledgeFacts;
	TArray<FBattleObedienceInput> ObedienceInputs;
	FBattleEncounterPolicies Policies;
};
