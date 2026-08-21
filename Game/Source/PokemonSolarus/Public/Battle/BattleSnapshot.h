#pragma once

#include "CoreMinimal.h"
#include "Battle/BattleDecision.h"
#include "Battle/BattleSetup.h"
#include "Battle/BattleStatStages.h"

/** Typed effectiveness knowledge consumed by UI and selectors without display strings. */
enum class EBattleEffectivenessKnowledge : uint8
{
	Unknown = 0,
	NotApplicable = 1,
	Neutral = 2,
	NotVeryEffective = 3,
	SuperEffective = 4,
	Immune = 5,
	Varies = 6
};

/** One move projection with PP included only when visible to the observer. */
struct POKEMONSOLARUS_API FBattleObservedMove
{
	uint8 SlotIndex = 255;
	FMoveId MoveId;
	bool bPPVisible = false;
	int32 CurrentPP = 0;
	int32 MaxPP = 0;
};

/** Observer-filtered Trainer facts. Private Bag counts are present only for their owner. */
struct POKEMONSOLARUS_API FBattleObservedTrainer
{
	FTrainerId TrainerId;
	EBattleSide Side = EBattleSide::Player;
	EBattleTrainerRole Role = EBattleTrainerRole::Player;
	EBattleDecisionController Controller = EBattleDecisionController::Human;
	bool bBagVisible = false;
	TArray<FBattleBagItemCount> Bag;
};

/** Observer-filtered battler facts copied from the authoritative state. */
struct POKEMONSOLARUS_API FBattleObservedBattler
{
	FTrainerId TrainerId;
	FBattlerId BattlerId;
	bool bPartySlotVisible = false;
	FPartySlotId PartySlotId;
	FSpeciesFormId SpeciesFormId;
	int32 Level = 0;
	int32 CurrentHP = 0;
	int32 MaxHP = 0;
	bool bFainted = false;
	FConditionId MajorStatusId;
	FBattleStatStages StatStages;
	bool bAbilityKnown = false;
	FAbilityId AbilityId;
	bool bHeldItemKnown = false;
	FItemId HeldItemId;
	TArray<FBattleObservedMove> Moves;
};

/** One structural active slot and its current public occupancy. */
struct POKEMONSOLARUS_API FBattleObservedActiveSlot
{
	FActiveSlotId ActiveSlotId;
	bool bAvailable = false;
	FTrainerId TrainerId;
	FBattlerId BattlerId;
};

/** One immutable condition projection with typed duration and layer data. */
struct POKEMONSOLARUS_API FBattleObservedCondition
{
	FConditionId ConditionId;
	TOptional<int32> RemainingTurns;
	int32 LayerCount = 0;
	uint64 CreationOrdinal = 0;
	FBattlerId SourceBattlerId;
};

/** Public conditions and hazards affecting one side. */
struct POKEMONSOLARUS_API FBattleObservedSide
{
	EBattleSide Side = EBattleSide::Player;
	TArray<FBattleObservedCondition> Conditions;
	TArray<FBattleObservedCondition> Hazards;
};

/** Per-target effectiveness knowledge for one currently selectable move. */
struct POKEMONSOLARUS_API FBattleTargetEffectivenessKnowledge
{
	FMoveId MoveId;
	FActiveSlotId TargetSlotId;
	EBattleEffectivenessKnowledge Value = EBattleEffectivenessKnowledge::Unknown;
};

/** Move-tile effectiveness summary across all currently legal targets. */
struct POKEMONSOLARUS_API FBattleMoveEffectivenessKnowledge
{
	FMoveId MoveId;
	EBattleEffectivenessKnowledge Value = EBattleEffectivenessKnowledge::Unknown;
};

/** External stat replacement allowed only at a matching opponent-removal checkpoint. */
struct POKEMONSOLARUS_API FBattleBetweenActionsStatRefresh
{
	uint64 StateVersion = 0;
	uint64 OpponentRemovalCheckpointEventOrdinal = 0;
	FBattlerId BattlerId;
	int32 NewLevel = 0;
	FPokemonBattleStats NewStats;
	int32 NewCurrentHP = 0;

	/** Returns whether the supplied version, checkpoint, identity, and stats are internally valid. */
	[[nodiscard]] bool IsValid() const
	{
		return StateVersion > 0
			&& OpponentRemovalCheckpointEventOrdinal > 0
			&& BattlerId.IsValid()
			&& NewLevel >= 1 && NewLevel <= 100
			&& NewStats.MaxHP > 0
			&& NewStats.Attack > 0
			&& NewStats.Defense > 0
			&& NewStats.SpecialAttack > 0
			&& NewStats.SpecialDefense > 0
			&& NewStats.Speed > 0
			&& NewCurrentHP >= 0 && NewCurrentHP <= NewStats.MaxHP;
	}
};

class FBattleEngine;

/** Deep immutable-by-interface projection of current public battle facts. */
class POKEMONSOLARUS_API FBattleSnapshot
{
public:
	/** Creates an invalid snapshot. */
	FBattleSnapshot() = default;

	/** Returns whether the core produced this snapshot. */
	[[nodiscard]] bool IsValid() const { return bValid; }
	/** Returns the monotonically increasing gameplay-state version. */
	[[nodiscard]] uint64 GetStateVersion() const { return StateVersion; }
	/** Returns the battle identity. */
	[[nodiscard]] FBattleId GetBattleId() const { return BattleId; }
	/** Returns the current turn identity. */
	[[nodiscard]] FTurnId GetTurnId() const { return TurnId; }
	/** Returns the current lifecycle phase. */
	[[nodiscard]] EBattlePhase GetPhase() const { return Phase; }
	/** Returns the frozen encounter family. */
	[[nodiscard]] EBattleEncounterKind GetEncounterKind() const { return EncounterKind; }
	/** Returns the frozen active-slot format. */
	[[nodiscard]] EBattleFormat GetFormat() const { return Format; }
	/** Returns the current outcome. */
	[[nodiscard]] EBattleOutcome GetOutcome() const { return Outcome; }
	/** Returns the typed outcome cause. */
	[[nodiscard]] EBattleOutcomeCause GetOutcomeCause() const { return OutcomeCause; }
	/** Returns the frozen settings reference. */
	[[nodiscard]] const FBattleSnapshotReference& GetSettingsReference() const { return SettingsReference; }
	/** Returns the frozen catalog reference. */
	[[nodiscard]] const FBattleSnapshotReference& GetCatalogReference() const { return CatalogReference; }
	/** Returns canonical Trainer facts. */
	[[nodiscard]] TConstArrayView<FBattleTrainerSetup> GetTrainers() const { return Trainers; }
	/** Returns a deep copy of current party/battler facts. */
	[[nodiscard]] TConstArrayView<FBattlePartyEntrySetup> GetPartyEntries() const { return PartyEntries; }
	/** Returns current active assignments. */
	[[nodiscard]] TConstArrayView<FBattleActiveAssignment> GetActiveAssignments() const { return ActiveAssignments; }
	/** Returns the pending request by value, if one exists. */
	[[nodiscard]] TOptional<FBattleDecisionRequest> GetPendingDecision() const { return PendingDecision; }
	/** Returns whether this projection was filtered for one observing Trainer. */
	[[nodiscard]] bool IsObserverFiltered() const { return bObserverFiltered; }
	/** Returns the observing Trainer, or invalid for the core-authority projection. */
	[[nodiscard]] FTrainerId GetObserverTrainerId() const { return ObserverTrainerId; }
	/** Returns observer-filtered Trainer facts. */
	[[nodiscard]] TConstArrayView<FBattleObservedTrainer> GetObservedTrainers() const { return ObservedTrainers; }
	/** Returns own-party and public-active battler facts visible to this observer. */
	[[nodiscard]] TConstArrayView<FBattleObservedBattler> GetObservedBattlers() const { return ObservedBattlers; }
	/** Returns all four structural active slots. */
	[[nodiscard]] TConstArrayView<FBattleObservedActiveSlot> GetObservedActiveSlots() const { return ObservedActiveSlots; }
	/** Returns the current weather projection, when present. */
	[[nodiscard]] const TOptional<FBattleObservedCondition>& GetWeather() const { return Weather; }
	/** Returns the current terrain projection, when present. */
	[[nodiscard]] const TOptional<FBattleObservedCondition>& GetTerrain() const { return Terrain; }
	/** Returns active room projections. */
	[[nodiscard]] TConstArrayView<FBattleObservedCondition> GetRooms() const { return Rooms; }
	/** Returns other active field-effect projections. */
	[[nodiscard]] TConstArrayView<FBattleObservedCondition> GetFieldEffects() const { return FieldEffects; }
	/** Returns both sides' condition and hazard projections. */
	[[nodiscard]] TConstArrayView<FBattleObservedSide> GetObservedSides() const { return ObservedSides; }
	/** Returns every request in the current owner's optional Left/Right batch. */
	[[nodiscard]] TConstArrayView<FBattleDecisionRequest> GetPendingDecisionRequests() const { return PendingDecisionRequests; }
	/** Returns already accepted selections visible to this observer. */
	[[nodiscard]] TConstArrayView<FBattleDecision> GetVisibleSelections() const { return VisibleSelections; }
	/** Returns move-tile effectiveness summaries. */
	[[nodiscard]] TConstArrayView<FBattleMoveEffectivenessKnowledge> GetMoveEffectivenessKnowledge() const { return MoveEffectivenessKnowledge; }
	/** Returns per-target effectiveness knowledge. */
	[[nodiscard]] TConstArrayView<FBattleTargetEffectivenessKnowledge> GetTargetEffectivenessKnowledge() const { return TargetEffectivenessKnowledge; }
	/** Finds one battler inside this snapshot copy, or returns null. */
	[[nodiscard]] const FBattlePartyEntrySetup* FindBattler(FBattlerId BattlerId) const;
	/** Finds one battler inside the observer-filtered projection, or returns null. */
	[[nodiscard]] const FBattleObservedBattler* FindObservedBattler(FBattlerId BattlerId) const;

private:
	friend class FBattleEngine;

	bool bValid = false;
	uint64 StateVersion = 0;
	FBattleId BattleId;
	FTurnId TurnId;
	EBattleEncounterKind EncounterKind = EBattleEncounterKind::Wild;
	EBattleFormat Format = EBattleFormat::Single;
	EBattlePhase Phase = EBattlePhase::Setup;
	EBattleOutcome Outcome = EBattleOutcome::InProgress;
	EBattleOutcomeCause OutcomeCause = EBattleOutcomeCause::None;
	FBattleSnapshotReference SettingsReference;
	FBattleSnapshotReference CatalogReference;
	TArray<FBattleTrainerSetup> Trainers;
	TArray<FBattlePartyEntrySetup> PartyEntries;
	TArray<FBattleActiveAssignment> ActiveAssignments;
	TOptional<FBattleDecisionRequest> PendingDecision;
	bool bObserverFiltered = false;
	FTrainerId ObserverTrainerId;
	TArray<FBattleObservedTrainer> ObservedTrainers;
	TArray<FBattleObservedBattler> ObservedBattlers;
	TArray<FBattleObservedActiveSlot> ObservedActiveSlots;
	TOptional<FBattleObservedCondition> Weather;
	TOptional<FBattleObservedCondition> Terrain;
	TArray<FBattleObservedCondition> Rooms;
	TArray<FBattleObservedCondition> FieldEffects;
	TArray<FBattleObservedSide> ObservedSides;
	TArray<FBattleDecisionRequest> PendingDecisionRequests;
	TArray<FBattleDecision> VisibleSelections;
	TArray<FBattleMoveEffectivenessKnowledge> MoveEffectivenessKnowledge;
	TArray<FBattleTargetEffectivenessKnowledge> TargetEffectivenessKnowledge;
};
