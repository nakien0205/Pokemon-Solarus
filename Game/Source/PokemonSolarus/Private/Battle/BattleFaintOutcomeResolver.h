#pragma once

#include "CoreMinimal.h"
#include "Battle/BattleEffectExecutor.h"
#include "Battle/BattlePartnerFlow.h"

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
	TOptional<FBattlePartnerTeamVictoryRecovery> PartnerTeamVictoryRecovery;
};

/** Owned projection plan for one action's faint, outcome, and recovery mutations. */
struct FBattleFaintOutcomePlan
{
	FBattleFaintOutcomeResolution Resolution;
	TOptional<FBattlePartnerTeamVictoryRecoveryPlan> PartnerRecoveryPlan;
};

/** Owned projection plan for the queue-exhaustion replacement boundary. */
struct FBattleQueueBoundaryPlan
{
	EBattlePhase PhaseAfter = EBattlePhase::Resolving;
	TArray<FBattleReplacementRequirement> Requirements;
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

	/** Produces every faint/outcome mutation without changing the supplied state. */
	[[nodiscard]] static bool TryResolveAction(
		const FBattleEffectExecutionResult& EffectResult,
		EBattleTargetClass TargetClass,
		FResolutionId ResolutionId,
		const FBattleEngineState& State,
		FBattleFaintOutcomePlan& OutPlan);

	/** Applies an already validated action plan to caller-owned staged state. */
	[[nodiscard]] static bool TryApplyActionPlan(
		FBattleEngineState& State,
		const FBattleFaintOutcomePlan& Plan);

	/** Enters MandatoryReplacement or EndOfTurn after the locked queue is exhausted. */
	static void ResolveQueueBoundary(
		FBattleEngineState& State,
		TArray<FBattleReplacementRequirement>& OutRequirements);

	/** Produces the queue-boundary phase and requirements without mutation. */
	[[nodiscard]] static bool ResolveQueueBoundary(
		const FBattleEngineState& State,
		FBattleQueueBoundaryPlan& OutPlan);

	/** Applies one validated queue-boundary plan to caller-owned staged state. */
	[[nodiscard]] static bool TryApplyQueueBoundaryPlan(
		FBattleEngineState& State,
		const FBattleQueueBoundaryPlan& Plan);
};
