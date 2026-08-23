#pragma once

#include "CoreMinimal.h"
#include "Battle/BattleDefinitions.h"
#include "Battle/BattleEvent.h"
#include "Battle/BattleFinalDamageCalculator.h"
#include "Battle/BattleHitResolver.h"
#include "Battle/BattleSwitching.h"
#include "Battle/BattleTargeting.h"

class FBattleEngineState;

/** Typed outcome from one reached C05B gate or effect application. */
enum class EBattleEffectExecutionOutcome : uint8
{
	Applied = 0,
	ChanceFailed = 1,
	Unreachable = 2,
	Protected = 3,
	Immune = 4,
	Blocked = 5,
	Failed = 6,
	Capped = 7,
	Prevented = 8,
	Deferred = 9,
	Invalid = 255
};

/** Typed reason an effect request could not complete atomically. */
enum class EBattleEffectExecutorError : uint8
{
	None = 0,
	InvalidRequest = 1,
	InvalidMoveDefinition = 2,
	InvalidTarget = 3,
	InvalidHookResult = 4,
	RandomFailure = 5,
	HitResolutionFailure = 6,
	DamageResolutionFailure = 7,
	ArithmeticOverflow = 8
};

/** Complete typed hook result; mutation facts are populated only when state changed. */
struct FBattleEffectHookResult
{
	EBattleEffectExecutionOutcome Outcome = EBattleEffectExecutionOutcome::Invalid;
	FDefinitionId RuleId;
	TOptional<int64> NumericBefore;
	TOptional<int64> NumericAfter;
	TOptional<int64> NumericDelta;
	bool bStateMutated = false;
	bool bCapped = false;
	bool bAffectsSubstitute = false;
	bool bSubstituteBroken = false;
	bool bDefersMove = false;
};

/** One ordered event-shaped record materialized by the engine after execution succeeds. */
struct FBattleEffectExecutionEvent
{
	EBattleEventType Type = EBattleEventType::EffectFailed;
	EBattleEventCause Cause = EBattleEventCause::Move;
	EBattleEffectExecutionOutcome Outcome = EBattleEffectExecutionOutcome::Invalid;
	TOptional<FBattleEventSource> SourceOverride;
	TArray<FBattleEventTarget> Targets;
	TOptional<int64> NumericBefore;
	TOptional<int64> NumericAfter;
	TOptional<int64> NumericDelta;
	TOptional<uint16> HitIndex;
	TOptional<uint16> HitCount;
};

/** One reached Switch descriptor retained for C06A production resolution. */
struct FBattleSwitchEffectIntent
{
	EBattleSwitchKind Kind = EBattleSwitchKind::Forced;
	int32 EffectEventIndex = INDEX_NONE;
	FBattleResolvedTarget Target;
	bool bApplied = false;
	EBattleSwitchBlockReason BlockReason = EBattleSwitchBlockReason::None;
	FPartySlotId SelectedPartySlotId;
	FBattlerId IncomingBattlerId;
	FBattleEventTarget OutgoingTarget;
	FBattleEventTarget IncomingTarget;
};

/** Immutable input for one already committed and targeted Fight action. */
struct FBattleEffectExecutionRequest
{
	FBattleId BattleId;
	FTurnId TurnId;
	FActionId ActionId;
	FResolutionId ResolutionId;
	FBattlerId UserBattlerId;
	FActiveSlotId UserSlotId;
	const FBattleMoveDefinition* Move = nullptr;
	TArray<FBattleResolvedTarget> Targets;
};

/** Complete successful C05B result before public event ordinals are assigned. */
struct FBattleEffectExecutionResult
{
	bool bValid = false;
	int32 TotalActualDamage = 0;
	TArray<int32> CompletedHitsPerDamageTarget;
	TArray<FBattleEffectExecutionEvent> Events;
	TArray<FBattleSwitchEffectIntent> SwitchIntents;
	/** True when a first-turn charge was stored and the remaining move effects must wait. */
	bool bMoveDeferred = false;
};

/**
 * Narrow future-rule seam used by the pure executor. The production adapter stages
 * mutable state and commits it only after the complete execution succeeds.
 */
class IBattleEffectExecutionContext
{
public:
	virtual ~IBattleEffectExecutionContext() = default;

	/** Validates all context-owned references and dynamic inputs before execution can draw or mutate. */
	virtual bool PrevalidateRequest(const FBattleEffectExecutionRequest& Request) const = 0;

	virtual FBattleEffectHookResult CheckReachability(
		const FBattleMoveDefinition& Move,
		const FBattleResolvedTarget& Target) = 0;
	virtual FBattleEffectHookResult CheckProtection(
		const FBattleMoveDefinition& Move,
		const FBattleResolvedTarget& Target) = 0;
	virtual FBattleEffectHookResult CheckTryHit(
		const FBattleMoveDefinition& Move,
		const FBattleResolvedTarget& Target) = 0;
	virtual FBattleEffectHookResult CheckMoveImmunity(
		const FBattleMoveDefinition& Move,
		const FBattleResolvedTarget& Target) = 0;
	virtual FBattleEffectHookResult CheckAbilityImmunity(
		const FBattleMoveDefinition& Move,
		const FBattleResolvedTarget& Target) = 0;
	virtual FBattleEffectHookResult CheckItemImmunity(
		const FBattleMoveDefinition& Move,
		const FBattleResolvedTarget& Target) = 0;
	virtual FBattleEffectHookResult ApplyProtectionBreaking(
		const FBattleMoveDefinition& Move,
		const FBattleResolvedTarget& Target) = 0;

	virtual bool TryBuildAccuracyInput(
		const FBattleMoveDefinition& Move,
		const FBattleResolvedTarget& Target,
		FBattleAccuracyCheckInput& OutInput) = 0;
	virtual bool TryBuildCriticalInput(
		const FBattleMoveDefinition& Move,
		const FBattleResolvedTarget& Target,
		FBattleCriticalCheckInput& OutInput) = 0;
	virtual bool TryBuildDamageInput(
		const FBattleMoveDefinition& Move,
		const FBattleResolvedTarget& Target,
		bool bSpreadAcrossMultipleTargets,
		FBattleFinalDamageInput& OutInput) = 0;
	/** Marks the narrow interval in which ApplyHpDelta represents one direct move hit. */
	virtual void SetDirectMoveDamageHit(bool bActive)
	{
		(void)bActive;
	}

	virtual bool IsSourceAbleToContinue() const = 0;
	virtual bool IsTargetAbleToContinue(const FBattleResolvedTarget& Target) const = 0;
	/** Allows a staged context to omit setup-only descriptors on a stored charge's release turn. */
	virtual bool ShouldSkipEffectDescriptor(const FBattleMoveEffectDescriptor& Effect) const
	{
		(void)Effect;
		return false;
	}
	virtual FBattleEffectHookResult CheckEffectEligibility(
		const FBattleMoveEffectDescriptor& Effect,
		const FBattleResolvedTarget& Target) = 0;
	virtual bool TryGetHp(
		const FBattleResolvedTarget& Target,
		int32& OutCurrentHP,
		int32& OutMaximumHP) const = 0;
	virtual FBattleEffectHookResult ApplyHpDelta(
		const FBattleResolvedTarget& Target,
		int32 RequestedDelta) = 0;
	virtual FBattleEffectHookResult ApplyNonHpEffect(
		const FBattleMoveEffectDescriptor& Effect,
		const FBattleResolvedTarget& Target) = 0;
	virtual void RunImmediateUpdate(const FBattleResolvedTarget& Target) = 0;
	/** Lets a void immediate-update hook report an atomic runtime failure. */
	virtual bool IsRuntimeValid() const
	{
		return true;
	}
	virtual bool TryBuildEventTarget(
		const FBattleResolvedTarget& Target,
		FBattleEventTarget& OutTarget) const = 0;
};

/** Deterministic reusable C05B executor. */
class FBattleEffectExecutor
{
public:
	[[nodiscard]] static bool TryExecute(
		const FBattleEffectExecutionRequest& Request,
		IBattleEffectExecutionContext& Context,
		IBattleRandom& Random,
		FBattleEffectExecutionResult& OutResult,
		EBattleEffectExecutorError& OutError);

	/** Executes against staged copies of engine-owned state and commits only on success. */
	[[nodiscard]] static bool TryExecuteAgainstState(
		const FBattleEffectExecutionRequest& Request,
		FBattleEngineState& State,
		FBattleEffectExecutionResult& OutResult,
		EBattleEffectExecutorError& OutError);

	[[nodiscard]] static FDefinitionId GetAccuracyRulePurpose();
	[[nodiscard]] static FDefinitionId GetCriticalRulePurpose();
	[[nodiscard]] static FDefinitionId GetDamageRandomRulePurpose();
	[[nodiscard]] static FDefinitionId GetMultiHitRulePurpose();
	[[nodiscard]] static FDefinitionId GetSecondaryChanceRulePurpose();
};
