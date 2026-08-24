#pragma once

#include "CoreMinimal.h"
#include "Battle/BattleEvent.h"

class FBattleEngineState;

/** One deterministic post-battle recovery required by Partner Team Victory. */
struct FBattlePartnerTeamVictoryRecovery
{
	FBattleEventTarget Target;
	int32 PreviousHP = 0;
	int32 NewHP = 0;
	bool bMajorStatusCured = false;
};

/** Private C09C partner-only outcome mutations. */
class FBattlePartnerFlow
{
public:
	/** Restores the first valid player-owned party entry to 1 HP after Partner Team Victory. */
	[[nodiscard]] static bool TryApplyTeamVictoryRecovery(
		FBattleEngineState& State,
		FBattlePartnerTeamVictoryRecovery& OutRecovery);
};
