#pragma once

#include "CoreMinimal.h"
#include "Battle/BattleEffectExecutor.h"

class FBattleEngineState;

/** One zero-HP transition retained until the engine assigns public event ordinals. */
struct FBattleFaintTransitionRecord
{
	int32 EffectEventIndex = INDEX_NONE;
	FBattleEventTarget Target;
	TOptional<uint64> SimultaneousGroupId;
	TOptional<uint16> HitIndex;
	TOptional<uint16> HitCount;
};

/** One empty active position that has a living inactive reserve. */
struct FBattleReplacementRequirement
{
	FBattleEventTarget Target;
};

/** Complete private result of the post-effect faint and outcome checkpoint. */
struct FBattleFaintOutcomeResolution
{
	TArray<FBattleFaintTransitionRecord> Faints;
	TArray<FBattleFaintTransitionRecord> Removals;
	TMap<int32, uint64> SimultaneousGroupsByEffectEvent;
	bool bBattleEnded = false;
	EBattleOutcome Outcome = EBattleOutcome::InProgress;
	EBattleOutcomeCause OutcomeCause = EBattleOutcomeCause::None;
};

/** Private deterministic C05C rules for faint cleanup, outcomes, and queue boundaries. */
class FBattleFaintOutcomeResolver
{
public:
	/** Resolves every zero-HP transition produced by one complete effect execution. */
	[[nodiscard]] static bool TryResolveAction(
		const FBattleEffectExecutionResult& EffectResult,
		EBattleTargetClass TargetClass,
		FResolutionId ResolutionId,
		FBattleEngineState& State,
		FBattleFaintOutcomeResolution& OutResolution);

	/** Enters MandatoryReplacement or EndOfTurn after the locked queue is exhausted. */
	static void ResolveQueueBoundary(
		FBattleEngineState& State,
		TArray<FBattleReplacementRequirement>& OutRequirements);
};
