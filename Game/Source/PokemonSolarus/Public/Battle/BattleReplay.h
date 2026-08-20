#pragma once

#include "CoreMinimal.h"
#include "Battle/BattleEvent.h"
#include "Battle/BattleRandom.h"
#include "Battle/BattleSnapshot.h"

/** Deterministic external inputs required to reproduce one battle attempt stream. */
struct POKEMONSOLARUS_API FBattleReplayInputs
{
	FBattleSetup Setup;
	TArray<FBattleDecision> Decisions;
	TArray<FBattleBetweenActionsStatRefresh> StatRefreshes;
};

/** Versioned replay record containing inputs, resolutions, RNG trace, and final snapshot. */
class POKEMONSOLARUS_API FBattleReplayRecord
{
public:
	static constexpr uint32 CurrentSchemaVersion = 1;

	/** Creates an invalid replay record. */
	FBattleReplayRecord() = default;

	/** Creates a validated versioned replay record by deep copy. */
	[[nodiscard]] static bool TryCreate(
		uint32 SchemaVersion,
		const FBattleReplayInputs& Inputs,
		TConstArrayView<FBattleResolution> Resolutions,
		TConstArrayView<FBattleRandomDraw> RandomTrace,
		const FBattleSnapshot& FinalSnapshot,
		FBattleReplayRecord& OutRecord);

	/** Returns whether this record is complete enough for canonical serialization. */
	[[nodiscard]] bool IsValid() const { return bValid; }
	/** Returns the explicit replay schema version. */
	[[nodiscard]] uint32 GetSchemaVersion() const { return SchemaVersion; }
	/** Returns a deep copy of deterministic replay inputs. */
	[[nodiscard]] const FBattleReplayInputs& GetInputs() const { return Inputs; }
	/** Returns ordered operation resolutions. */
	[[nodiscard]] TConstArrayView<FBattleResolution> GetResolutions() const { return Resolutions; }
	/** Returns the ordered RNG trace. */
	[[nodiscard]] TConstArrayView<FBattleRandomDraw> GetRandomTrace() const { return RandomTrace; }
	/** Returns the final immutable snapshot. */
	[[nodiscard]] const FBattleSnapshot& GetFinalSnapshot() const { return FinalSnapshot; }

private:
	bool bValid = false;
	uint32 SchemaVersion = 0;
	FBattleReplayInputs Inputs;
	TArray<FBattleResolution> Resolutions;
	TArray<FBattleRandomDraw> RandomTrace;
	FBattleSnapshot FinalSnapshot;
};

/** Explicit fixed-order serializer for canonical replay bytes. */
class POKEMONSOLARUS_API FBattleReplaySerializer
{
public:
	/**
	 * Serializes every value explicitly in big-endian field order.
	 * It never copies raw struct bytes, padding, pointers, or unordered iteration.
	 */
	[[nodiscard]] static bool TrySerializeCanonical(
		const FBattleReplayRecord& Record,
		TArray<uint8>& OutBytes,
		FBattleRejection& OutRejection);
};
