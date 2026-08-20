#pragma once

#include "CoreMinimal.h"
#include "Battle/BattleDecision.h"
#include "Battle/BattleSetup.h"

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
	/** Finds one battler inside this snapshot copy, or returns null. */
	[[nodiscard]] const FBattlePartyEntrySetup* FindBattler(FBattlerId BattlerId) const;

private:
	friend class FBattleEngine;

	bool bValid = false;
	uint64 StateVersion = 0;
	FBattleId BattleId;
	FTurnId TurnId;
	EBattlePhase Phase = EBattlePhase::Setup;
	EBattleOutcome Outcome = EBattleOutcome::InProgress;
	EBattleOutcomeCause OutcomeCause = EBattleOutcomeCause::None;
	FBattleSnapshotReference SettingsReference;
	FBattleSnapshotReference CatalogReference;
	TArray<FBattleTrainerSetup> Trainers;
	TArray<FBattlePartyEntrySetup> PartyEntries;
	TArray<FBattleActiveAssignment> ActiveAssignments;
	TOptional<FBattleDecisionRequest> PendingDecision;
};
