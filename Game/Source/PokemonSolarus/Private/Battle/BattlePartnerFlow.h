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

/** Owned, non-mutating preparation for one Partner Team Victory recovery. */
struct FBattlePartnerTeamVictoryRecoveryPlan
{
	FBattlePartnerTeamVictoryRecovery Recovery;
};

/** Private C09C partner-only outcome mutations. */
class FBattlePartnerFlow
{
public:
	/** Restores the first valid player-owned party entry to 1 HP after Partner Team Victory. */
	[[nodiscard]] static bool TryApplyTeamVictoryRecovery(
		FBattleEngineState& State,
		FBattlePartnerTeamVictoryRecovery& OutRecovery);

	/** Produces exact recovery facts without changing the supplied state. */
	[[nodiscard]] static bool TryApplyTeamVictoryRecovery(
		const FBattleEngineState& State,
		FBattlePartnerTeamVictoryRecoveryPlan& OutPlan);

	/** Applies an already prepared recovery plan to caller-owned staged state. */
	[[nodiscard]] static bool TryApplyTeamVictoryRecoveryPlan(
		FBattleEngineState& State,
		const FBattlePartnerTeamVictoryRecoveryPlan& Plan);
};
