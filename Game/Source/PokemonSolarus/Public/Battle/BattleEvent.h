#pragma once

#include "CoreMinimal.h"
#include "Battle/BattleActionQueue.h"

/** Public event family with explicit canonical encoding. */
enum class EBattleEventType : uint8
{
	DecisionAccepted = 0,
	DecisionRejected = 1,
	ActionLocked = 2,
	ActionStarted = 3,
	ActionCanceled = 4,
	ActionCompleted = 5,
	MoveUsed = 6,
	ItemUsed = 7,
	Switched = 8,
	RunAttempted = 9,
	CaptureAttempted = 10,
	ScriptedAction = 11,
	PPConsumed = 12,
	ItemConsumed = 13,
	RandomCheck = 14,
	AccuracyChecked = 15,
	Missed = 16,
	Immunity = 17,
	Protected = 18,
	CriticalChecked = 19,
	Effectiveness = 20,
	Damage = 21,
	Healing = 22,
	HPChanged = 23,
	StatusChanged = 24,
	StatStageChanged = 25,
	FieldEffectChanged = 26,
	EnteredActiveSlot = 27,
	LeftActiveSlot = 28,
	Fainted = 29,
	Captured = 30,
	Escaped = 31,
	Removed = 32,
	ReplacementRequired = 33,
	OpponentRemovalCheckpoint = 34,
	BattleEnded = 35,
	StatRefreshApplied = 36,
	StatRefreshRejected = 37,
	ActionOrderLocked = 38,
	ObedienceConfirmed = 39,
	ObedienceRefused = 40,
	TargetsResolved = 41,
	Unreachable = 42,
	EffectBlocked = 43,
	EffectFailed = 44,
	EffectCapped = 45,
	EffectPrevented = 46,
	EffectDeferred = 47,
	SwitchTransientStateCleared = 48
};

/** Typed source family for an event cause. */
enum class EBattleEventCause : uint8
{
	System = 0,
	Decision = 1,
	Action = 2,
	Move = 3,
	Item = 4,
	Switch = 5,
	Run = 6,
	Capture = 7,
	Scripted = 8,
	Rule = 9,
	Outcome = 10,
	StatRefresh = 11,
	Targeting = 12
};

/** Optional typed source identities for one event. */
struct POKEMONSOLARUS_API FBattleEventSource
{
	FTrainerId TrainerId;
	FBattlerId BattlerId;
	FActiveSlotId ActiveSlotId;
	FDefinitionId DefinitionId;
};

/** One typed target in stable event target order. */
struct POKEMONSOLARUS_API FBattleEventTarget
{
	FTrainerId TrainerId;
	FBattlerId BattlerId;
	FActiveSlotId ActiveSlotId;
	EBattleSide Side = EBattleSide::Player;
	bool bHasSide = false;
	bool bField = false;
};

/** Public visibility and reveal metadata for one event. */
struct POKEMONSOLARUS_API FBattleEventVisibility
{
	EBattleVisibilityLevel Level = EBattleVisibilityLevel::Public;
	FTrainerId OwningTrainerId;
	EBattleSide OwningSide = EBattleSide::Player;
	bool bHasOwningSide = false;
	bool bRevealSourceDefinition = false;
};

/** Complete queue-order facts attached only to an ActionOrderLocked event. */
struct POKEMONSOLARUS_API FBattleActionOrderMetadata
{
	uint64 QueueOrdinal = 0;
	FBattleActionOrderKey OrderKey;
	bool bReverseSpeed = false;
};

/** Target-class and redirection facts attached only to a TargetsResolved event. */
struct POKEMONSOLARUS_API FBattleTargetResolutionMetadata
{
	EBattleTargetClass TargetClass = EBattleTargetClass::SelectedOpponent;
	bool bWasRedirected = false;
	bool bUsedFaintedTargetFallback = false;
};

/** Mutable construction input for one validated immutable event. */
struct POKEMONSOLARUS_API FBattleEventSpec
{
	uint64 EventOrdinal = 0;
	FBattleId BattleId;
	FTurnId TurnId;
	FActionId ActionId;
	FResolutionId ResolutionId;
	EBattleEventType Type = EBattleEventType::DecisionRejected;
	EBattleEventCause Cause = EBattleEventCause::System;
	EBattleActionKind CauseActionKind = EBattleActionKind::Fight;
	EBattleOutcomeCause OutcomeCause = EBattleOutcomeCause::None;
	FBattleEventSource Source;
	TArray<FBattleEventTarget> Targets;
	TOptional<int64> NumericBefore;
	TOptional<int64> NumericAfter;
	TOptional<int64> NumericDelta;
	TOptional<uint64> SimultaneousGroupId;
	TOptional<uint16> HitIndex;
	TOptional<uint16> HitCount;
	TOptional<FBattleActionOrderMetadata> ActionOrder;
	TOptional<FBattleTargetResolutionMetadata> TargetResolution;
	FBattleEventVisibility Visibility;
};

/** One validated battle event, immutable through its public interface. */
class POKEMONSOLARUS_API FBattleEvent
{
public:
	/** Creates an invalid event. */
	FBattleEvent() = default;

	/** Validates IDs, optional group/hit metadata, and target identities. */
	[[nodiscard]] static bool TryCreate(const FBattleEventSpec& Spec, FBattleEvent& OutEvent);

	/** Returns whether validation produced this event. */
	[[nodiscard]] bool IsValid() const { return bValid; }
	/** Returns the total event ordinal. */
	[[nodiscard]] uint64 GetEventOrdinal() const { return EventOrdinal; }
	/** Returns the battle identity. */
	[[nodiscard]] FBattleId GetBattleId() const { return BattleId; }
	/** Returns the turn identity. */
	[[nodiscard]] FTurnId GetTurnId() const { return TurnId; }
	/** Returns the action identity, or invalid for actionless events. */
	[[nodiscard]] FActionId GetActionId() const { return ActionId; }
	/** Returns the resolution-attempt identity. */
	[[nodiscard]] FResolutionId GetResolutionId() const { return ResolutionId; }
	/** Returns the event family. */
	[[nodiscard]] EBattleEventType GetType() const { return Type; }
	/** Returns the typed cause family. */
	[[nodiscard]] EBattleEventCause GetCause() const { return Cause; }
	/** Returns the related action family. */
	[[nodiscard]] EBattleActionKind GetCauseActionKind() const { return CauseActionKind; }
	/** Returns the related outcome cause. */
	[[nodiscard]] EBattleOutcomeCause GetOutcomeCause() const { return OutcomeCause; }
	/** Returns typed source identities. */
	[[nodiscard]] const FBattleEventSource& GetSource() const { return Source; }
	/** Returns targets in deterministic order. */
	[[nodiscard]] TConstArrayView<FBattleEventTarget> GetTargets() const { return Targets; }
	/** Returns an optional numeric before value. */
	[[nodiscard]] const TOptional<int64>& GetNumericBefore() const { return NumericBefore; }
	/** Returns an optional numeric after value. */
	[[nodiscard]] const TOptional<int64>& GetNumericAfter() const { return NumericAfter; }
	/** Returns an optional numeric delta. */
	[[nodiscard]] const TOptional<int64>& GetNumericDelta() const { return NumericDelta; }
	/** Returns the optional simultaneous-group identity. */
	[[nodiscard]] const TOptional<uint64>& GetSimultaneousGroupId() const { return SimultaneousGroupId; }
	/** Returns the optional one-based hit index. */
	[[nodiscard]] const TOptional<uint16>& GetHitIndex() const { return HitIndex; }
	/** Returns the optional total reached hit count. */
	[[nodiscard]] const TOptional<uint16>& GetHitCount() const { return HitCount; }
	/** Returns complete queue-order metadata only for ActionOrderLocked. */
	[[nodiscard]] const TOptional<FBattleActionOrderMetadata>& GetActionOrder() const { return ActionOrder; }
	/** Returns target-class and redirect metadata only for TargetsResolved. */
	[[nodiscard]] const TOptional<FBattleTargetResolutionMetadata>& GetTargetResolution() const { return TargetResolution; }
	/** Returns public visibility/reveal metadata. */
	[[nodiscard]] const FBattleEventVisibility& GetVisibility() const { return Visibility; }

private:
	bool bValid = false;
	uint64 EventOrdinal = 0;
	FBattleId BattleId;
	FTurnId TurnId;
	FActionId ActionId;
	FResolutionId ResolutionId;
	EBattleEventType Type = EBattleEventType::DecisionRejected;
	EBattleEventCause Cause = EBattleEventCause::System;
	EBattleActionKind CauseActionKind = EBattleActionKind::Fight;
	EBattleOutcomeCause OutcomeCause = EBattleOutcomeCause::None;
	FBattleEventSource Source;
	TArray<FBattleEventTarget> Targets;
	TOptional<int64> NumericBefore;
	TOptional<int64> NumericAfter;
	TOptional<int64> NumericDelta;
	TOptional<uint64> SimultaneousGroupId;
	TOptional<uint16> HitIndex;
	TOptional<uint16> HitCount;
	TOptional<FBattleActionOrderMetadata> ActionOrder;
	TOptional<FBattleTargetResolutionMetadata> TargetResolution;
	FBattleEventVisibility Visibility;
};

/** Mutable construction input for one immutable decision/stat-refresh resolution. */
struct POKEMONSOLARUS_API FBattleResolutionSpec
{
	FResolutionId ResolutionId;
	uint64 BeforeStateVersion = 0;
	uint64 AfterStateVersion = 0;
	bool bAccepted = false;
	FBattleRejection Rejection;
	TArray<FBattleEvent> Events;
};

/** One accepted or rejected attempt with an immutable ordered event list. */
class POKEMONSOLARUS_API FBattleResolution
{
public:
	/** Creates an invalid resolution. */
	FBattleResolution() = default;

	/** Validates version behavior, rejection state, and strictly increasing event ordinals. */
	[[nodiscard]] static bool TryCreate(const FBattleResolutionSpec& Spec, FBattleResolution& OutResolution);

	/** Returns whether this resolution was constructed successfully. */
	[[nodiscard]] bool IsValid() const { return bValid; }
	/** Returns whether the submitted operation was accepted. */
	[[nodiscard]] bool WasAccepted() const { return bAccepted; }
	/** Returns the resolution identity. */
	[[nodiscard]] FResolutionId GetResolutionId() const { return ResolutionId; }
	/** Returns the state version observed before the operation. */
	[[nodiscard]] uint64 GetBeforeStateVersion() const { return BeforeStateVersion; }
	/** Returns the state version after the operation. */
	[[nodiscard]] uint64 GetAfterStateVersion() const { return AfterStateVersion; }
	/** Returns the typed rejection, or None after acceptance. */
	[[nodiscard]] const FBattleRejection& GetRejection() const { return Rejection; }
	/** Returns events in total deterministic order. */
	[[nodiscard]] TConstArrayView<FBattleEvent> GetEvents() const { return Events; }

private:
	bool bValid = false;
	FResolutionId ResolutionId;
	uint64 BeforeStateVersion = 0;
	uint64 AfterStateVersion = 0;
	bool bAccepted = false;
	FBattleRejection Rejection;
	TArray<FBattleEvent> Events;
};
