#pragma once

#include "CoreMinimal.h"

/** The seven temporary battle stages supported by modern battle rules. */
enum class EBattleStat : uint8
{
	Attack = 0,
	Defense = 1,
	SpecialAttack = 2,
	SpecialDefense = 3,
	Speed = 4,
	Accuracy = 5,
	Evasion = 6
};

/** Whether one stage-change request changed state, was blocked, or was invalid. */
enum class EBattleStatStageChangeOutcome : uint8
{
	Applied = 0,
	Blocked = 1,
	Invalid = 2
};

/** Complete deterministic result of one temporary stage-change request. */
struct POKEMONSOLARUS_API FBattleStatStageChangeResult
{
	EBattleStatStageChangeOutcome Outcome = EBattleStatStageChangeOutcome::Invalid;
	int32 PreviousStage = 0;
	int32 RequestedDelta = 0;
	int32 AppliedDelta = 0;
	int32 NewStage = 0;
	bool bClamped = false;
};

/**
 * Owns the seven temporary battle stages while enforcing the -6 through +6 range.
 * Default construction produces seven neutral stages.
 */
class POKEMONSOLARUS_API FBattleStatStages
{
public:
	static constexpr int32 MinimumStage = -6;
	static constexpr int32 MaximumStage = 6;

	/** Reads one known stage. Invalid input returns false and resets OutStage. */
	[[nodiscard]] bool TryGetStage(EBattleStat Stat, int32& OutStage) const;

	/** Applies one signed request and reports the actual clamped result. */
	FBattleStatStageChangeResult ApplyChange(EBattleStat Stat, int32 RequestedDelta);

private:
	bool TrySetStage(EBattleStat Stat, int8 NewStage);

	int8 Attack = 0;
	int8 Defense = 0;
	int8 SpecialAttack = 0;
	int8 SpecialDefense = 0;
	int8 Speed = 0;
	int8 Accuracy = 0;
	int8 Evasion = 0;
};
