#pragma once

#include "CoreMinimal.h"
#include "Battle/BattleDecision.h"
#include "Battle/BattleDefinitions.h"
#include "Battle/BattleRandom.h"
#include "Battle/BattleTargeting.h"

/** Solarus normal-turn command bands. Higher values execute first. */
enum class EBattleActionCommandBand : uint8
{
	Move = 0,
	Bag = 1,
	VoluntarySwitch = 2,
	Run = 3
};

/** Stable failure from validating or locking one normal-turn queue. */
enum class EBattleActionQueueError : uint8
{
	None = 0,
	InvalidContext = 1,
	InvalidCandidate = 2,
	DuplicateAction = 3,
	DuplicateActor = 4,
	DuplicateActiveSlot = 5,
	RandomFailure = 6
};

/** Fully resolved immutable ordering keys for one selected normal-turn action. */
struct POKEMONSOLARUS_API FBattleActionOrderKey
{
	EBattleActionCommandBand CommandBand = EBattleActionCommandBand::Move;
	int32 MovePriority = 0;
	int32 FractionalPriorityTenths = 0;
	int32 EffectiveSpeed = 0;
	FActiveSlotId ActingSlotId;
};

/** One validated action plus the keys required to lock it into the queue. */
struct POKEMONSOLARUS_API FBattleActionOrderCandidate
{
	FActionId ActionId;
	FBattleDecision Decision;
	FBattleActionOrderKey OrderKey;
	EBattleTargetClass TargetClass = EBattleTargetClass::SelectedOpponent;
	FBattlerId SelectedTargetBattlerId;
};

/** One selected action after queue order and a stable one-based ordinal are frozen. */
struct POKEMONSOLARUS_API FBattleLockedAction
{
	FActionId ActionId;
	uint64 QueueOrdinal = 0;
	FBattleDecision Decision;
	FBattleActionOrderKey OrderKey;
	EBattleTargetClass TargetClass = EBattleTargetClass::SelectedOpponent;
	FBattlerId SelectedTargetBattlerId;
	TOptional<FBattleTargetResolutionResult> TargetResolution;
};

/** Validated context and candidates supplied to the deterministic queue resolver. */
struct POKEMONSOLARUS_API FBattleActionQueueLockSpec
{
	FBattleId BattleId;
	FTurnId TurnId;
	FResolutionId ResolutionId;
	bool bReverseSpeed = false;
	TArray<FBattleActionOrderCandidate> Candidates;
};

/** Pure normal-turn queue resolver. It owns no battle state and mutates only the injected RNG trace. */
class POKEMONSOLARUS_API FBattleActionQueueResolver
{
public:
	/**
	 * Validates every candidate, resolves same-side ties once, and returns a frozen queue.
	 * Cross-side exact ties always place the player side first and consume no draw.
	 */
	[[nodiscard]] static bool TryLock(
		const FBattleActionQueueLockSpec& Spec,
		IBattleRandom& Random,
		TArray<FBattleLockedAction>& OutQueue,
		EBattleActionQueueError& OutError);
};

/** Result of the C04A action-start gates before status/volatile gates and resource consumption. */
enum class EBattleActionStartOutcome : uint8
{
	Invalid = 0,
	Proceed = 1,
	ActorUnavailable = 2,
	CapturedTargetCanceled = 3,
	ObedienceRefused = 4
};

/** Current facts needed to revalidate one already locked action without exposing mutable state. */
struct POKEMONSOLARUS_API FBattleActionStartFacts
{
	EBattleActionKind ActionKind = EBattleActionKind::Fight;
	bool bActorActive = false;
	bool bActorLiving = false;
	bool bSelectedTargetCaptured = false;
	bool bSubjectToPlayerObedience = false;
	uint8 ObedienceReferenceLevel = 0;
	uint8 BadgeCount = 0;
};

/** Typed action-start result and exact immediate resource behavior. */
struct POKEMONSOLARUS_API FBattleActionStartResult
{
	EBattleActionStartOutcome Outcome = EBattleActionStartOutcome::Invalid;
	bool bEndsCommittedAction = false;
	bool bConsumesPP = false;
	bool bConsumesItem = false;
	bool bConsumesRng = false;
	TOptional<uint8> ObedienceCap;
};

/** Pure C04A execution gate shared by the engine and focused tests. */
class POKEMONSOLARUS_API FBattleActionStartRules
{
public:
	/**
	 * Applies actor, captured-target, and deterministic Solarus obedience gates.
	 * This checkpoint never consumes PP, items, or RNG; later resolvers own those resources.
	 */
	[[nodiscard]] static bool TryEvaluate(
		const FBattleActionStartFacts& Facts,
		FBattleActionStartResult& OutResult);
};

/** Engine-owned fallback move facts that never depend on an authored catalog row. */
class POKEMONSOLARUS_API FBattleBuiltInMoveDefinitions
{
public:
	/** Returns the stable engine-supplied Struggle move ID. */
	[[nodiscard]] static FMoveId GetStruggleMoveId();

	/** Returns the immutable modern Struggle definition used by later move resolvers. */
	[[nodiscard]] static const FBattleMoveDefinition& GetStruggle();
};
