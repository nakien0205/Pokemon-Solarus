#pragma once

#include "CoreMinimal.h"
#include "Battle/BattleIdentifiers.h"
#include "Battle/BattleSetupTypes.h"

struct FBattleTriggerRegistrationIdTag;
struct FBattleTriggerEffectIdTag;
struct FBattleTriggerReentrancyTokenTag;
struct FBattleTriggerSimultaneousGroupIdTag;

/** Stable identity assigned to one registered trigger. */
using FBattleTriggerRegistrationId = TNumericBattleId<FBattleTriggerRegistrationIdTag>;
/** Stable authored identity of one declarative rule effect. */
using FBattleTriggerEffectId = TTypedDefinitionId<FBattleTriggerEffectIdTag>;
/** Non-zero token that bounds non-repeatable trigger execution. */
using FBattleTriggerReentrancyToken = TNumericBattleId<FBattleTriggerReentrancyTokenTag>;
/** Optional non-zero identity shared by simultaneous lifecycle facts and effect requests. */
using FBattleTriggerSimultaneousGroupId = TNumericBattleId<FBattleTriggerSimultaneousGroupIdTag>;

/** The complete C07A trigger-phase vocabulary. */
enum class EBattleTriggerPhase : uint8
{
	BattleStart = 0,
	TurnStart = 1,
	SelectionEligibility = 2,
	ActionOrderCalculation = 3,
	BeforeAction = 4,
	BeforeAccuracy = 5,
	BeforeHit = 6,
	BeforeDamage = 7,
	AfterDamage = 8,
	AfterHit = 9,
	AfterAction = 10,
	SwitchOut = 11,
	SwitchIn = 12,
	Faint = 13,
	Removal = 14,
	EndTurn = 15,
	Expiry = 16
};

/** Authored definition family that owns a trigger registration. */
enum class EBattleTriggerSourceDefinitionKind : uint8
{
	Condition = 0,
	Ability = 1,
	Item = 2
};

/** Typed Condition, Ability, or item definition source. */
struct POKEMONSOLARUS_API FBattleTriggerSourceDefinition
{
	EBattleTriggerSourceDefinitionKind Kind = EBattleTriggerSourceDefinitionKind::Condition;
	FConditionId ConditionId;
	FAbilityId AbilityId;
	FItemId ItemId;

	[[nodiscard]] static bool TryCreateCondition(
		const FConditionId& InConditionId,
		FBattleTriggerSourceDefinition& OutSource);
	[[nodiscard]] static bool TryCreateAbility(
		const FAbilityId& InAbilityId,
		FBattleTriggerSourceDefinition& OutSource);
	[[nodiscard]] static bool TryCreateItem(
		const FItemId& InItemId,
		FBattleTriggerSourceDefinition& OutSource);
	[[nodiscard]] bool IsValid() const;

	friend bool operator==(
		const FBattleTriggerSourceDefinition& Left,
		const FBattleTriggerSourceDefinition& Right)
	{
		return Left.Kind == Right.Kind
			&& Left.ConditionId == Right.ConditionId
			&& Left.AbilityId == Right.AbilityId
			&& Left.ItemId == Right.ItemId;
	}
};

/** Runtime subject family used by owners, sources, targets, and duration owners. */
enum class EBattleTriggerSubjectKind : uint8
{
	Battle = 0,
	Field = 1,
	Side = 2,
	Trainer = 3,
	Battler = 4,
	ActiveSlot = 5
};

/** One validated typed subject without pointers into mutable battle state. */
struct POKEMONSOLARUS_API FBattleTriggerSubject
{
	EBattleTriggerSubjectKind Kind = EBattleTriggerSubjectKind::Battle;
	FBattleId BattleId;
	EBattleSide Side = EBattleSide::Player;
	bool bHasSide = false;
	FTrainerId TrainerId;
	FBattlerId BattlerId;
	FActiveSlotId ActiveSlotId;

	[[nodiscard]] static bool TryCreateBattle(
		const FBattleId InBattleId,
		FBattleTriggerSubject& OutSubject);
	[[nodiscard]] static FBattleTriggerSubject CreateField();
	[[nodiscard]] static bool TryCreateSide(
		const EBattleSide InSide,
		FBattleTriggerSubject& OutSubject);
	[[nodiscard]] static bool TryCreateTrainer(
		const FTrainerId InTrainerId,
		FBattleTriggerSubject& OutSubject);
	[[nodiscard]] static bool TryCreateBattler(
		const FBattlerId InBattlerId,
		FBattleTriggerSubject& OutSubject);
	[[nodiscard]] static bool TryCreateActiveSlot(
		const FActiveSlotId InActiveSlotId,
		FBattleTriggerSubject& OutSubject);
	[[nodiscard]] bool IsValid() const;

	friend bool operator==(const FBattleTriggerSubject& Left, const FBattleTriggerSubject& Right)
	{
		return Left.Kind == Right.Kind
			&& Left.BattleId == Right.BattleId
			&& Left.Side == Right.Side
			&& Left.bHasSide == Right.bHasSide
			&& Left.TrainerId == Right.TrainerId
			&& Left.BattlerId == Right.BattlerId
			&& Left.ActiveSlotId == Right.ActiveSlotId;
	}
};

/** Visibility contract for fact-only lifecycle records and declarative requests. */
struct POKEMONSOLARUS_API FBattleTriggerVisibility
{
	EBattleVisibilityLevel Level = EBattleVisibilityLevel::CoreOnly;
	FTrainerId OwningTrainerId;
	EBattleSide OwningSide = EBattleSide::Player;
	bool bHasOwningSide = false;

	[[nodiscard]] static FBattleTriggerVisibility CreateCoreOnly();
	[[nodiscard]] static FBattleTriggerVisibility CreatePublic();
	[[nodiscard]] static bool TryCreateOwningTrainer(
		const FTrainerId InTrainerId,
		FBattleTriggerVisibility& OutVisibility);
	[[nodiscard]] static bool TryCreateOwningSide(
		const EBattleSide InSide,
		FBattleTriggerVisibility& OutVisibility);
	[[nodiscard]] bool IsValid() const;
};

/** Typed lifecycle reasons that may remove a registration. */
enum class EBattleTriggerCleanupReason : uint8
{
	Switch = 0,
	Faint = 1,
	Capture = 2,
	BattleEnd = 3,
	Removal = 4
};

/** Independent cleanup policies that can be combined on a registration. */
enum class EBattleTriggerCleanupPolicy : uint8
{
	None = 0,
	OnSwitch = 1 << 0,
	OnFaint = 1 << 1,
	OnCapture = 1 << 2,
	OnBattleEnd = 1 << 3,
	OnRemoval = 1 << 4
};
ENUM_CLASS_FLAGS(EBattleTriggerCleanupPolicy);

/** Caller-selected direction for one canonical ordering key. */
enum class EBattleTriggerSortDirection : uint8
{
	Ascending = 0,
	Descending = 1
};

/** Complete deterministic sort policy; no random tie breaker exists in this contract. */
struct POKEMONSOLARUS_API FBattleTriggerOrderPolicy
{
	EBattleTriggerSortDirection Order = EBattleTriggerSortDirection::Ascending;
	EBattleTriggerSortDirection Priority = EBattleTriggerSortDirection::Ascending;
	EBattleTriggerSortDirection Suborder = EBattleTriggerSortDirection::Ascending;
	bool bUseEffectiveSpeed = false;
	EBattleTriggerSortDirection EffectiveSpeed = EBattleTriggerSortDirection::Descending;
	EBattleTriggerSortDirection Side = EBattleTriggerSortDirection::Ascending;
	EBattleTriggerSortDirection Position = EBattleTriggerSortDirection::Ascending;
	EBattleTriggerSortDirection Creation = EBattleTriggerSortDirection::Ascending;
};

/** Immutable authored rule fields copied into one registration. */
struct POKEMONSOLARUS_API FBattleTriggerRuleDefinition
{
	EBattleTriggerPhase Phase = EBattleTriggerPhase::BattleStart;
	FBattleTriggerEffectId EffectId;
	FDefinitionId PayloadId;
	int32 Order = 0;
	int32 Priority = 0;
	int32 Suborder = 0;
	bool bRepeatable = false;
	bool bDecrementDurationBeforeEffect = false;
};

/** Complete mutable input for one atomic, deep-copied trigger registration. */
struct POKEMONSOLARUS_API FBattleTriggerRegistrationSpec
{
	FBattleTriggerRuleDefinition Rule;
	FBattleTriggerSourceDefinition SourceDefinition;
	FBattleTriggerSubject Owner;
	FBattleTriggerSubject Source;
	TArray<FBattleTriggerSubject> Targets;
	FBattleTriggerSubject DurationOwner;
	TOptional<int32> RemainingTurns;
	int32 Layers = 1;
	FBattleTriggerVisibility Visibility;
	EBattleTriggerCleanupPolicy CleanupPolicy = EBattleTriggerCleanupPolicy::None;
	bool bSuppressed = false;
};

/** Immutable-by-interface snapshot returned by registration queries. */
struct POKEMONSOLARUS_API FBattleTriggerRegistrationState
{
	FBattleTriggerRegistrationId RegistrationId;
	uint64 CreationOrdinal = 0;
	FBattleTriggerRegistrationSpec Spec;
	TOptional<int32> RemainingTurns;
	int32 Layers = 0;
	bool bSuppressed = false;
};

/** Per-registration facts supplied by the caller for one phase dispatch. */
struct POKEMONSOLARUS_API FBattleTriggerDispatchParticipant
{
	FBattleTriggerRegistrationId RegistrationId;
	TOptional<int32> EffectiveSpeed;
	TOptional<FActiveSlotId> ActiveSlotId;
};

/** One queued phase dispatch. Empty participants select every active registration in that phase. */
struct POKEMONSOLARUS_API FBattleTriggerDispatchSpec
{
	EBattleTriggerPhase Phase = EBattleTriggerPhase::BattleStart;
	FBattleTriggerReentrancyToken ReentrancyToken;
	TOptional<FBattleTriggerSimultaneousGroupId> SimultaneousGroupId;
	FBattleTriggerOrderPolicy OrderPolicy;
	TArray<FBattleTriggerDispatchParticipant> Participants;
	TArray<FBattleTriggerSubject> DurationTickOwners;
};

/** Shared context for layer, suppression, and cleanup lifecycle operations. */
struct POKEMONSOLARUS_API FBattleTriggerOperationContext
{
	FBattleTriggerReentrancyToken ReentrancyToken;
	TOptional<FBattleTriggerSimultaneousGroupId> SimultaneousGroupId;
};

/** Typed cleanup request; BattleEnd applies globally and all other reasons target owners. */
struct POKEMONSOLARUS_API FBattleTriggerCleanupRequest
{
	EBattleTriggerCleanupReason Reason = EBattleTriggerCleanupReason::Removal;
	TArray<FBattleTriggerSubject> AffectedOwners;
	/** Optional exact authored-source filter used when one owner has several conditions. */
	TOptional<FBattleTriggerSourceDefinition> SourceDefinitionFilter;
	FBattleTriggerOperationContext Context;
};

/** Fully resolved order keys attached to a declarative request. */
struct POKEMONSOLARUS_API FBattleTriggerResolvedOrder
{
	int32 Order = 0;
	int32 Priority = 0;
	int32 Suborder = 0;
	TOptional<int32> EffectiveSpeed;
	uint8 SideOrdinal = 2;
	uint8 PositionOrdinal = 2;
	uint64 CreationOrdinal = 0;
};

/** Declarative work emitted by C07A for a later effect executor. */
struct POKEMONSOLARUS_API FBattleTriggerEffectRequest
{
	uint64 RequestOrdinal = 0;
	FBattleTriggerRegistrationId RegistrationId;
	EBattleTriggerPhase Phase = EBattleTriggerPhase::BattleStart;
	FBattleTriggerEffectId EffectId;
	FDefinitionId PayloadId;
	FBattleTriggerSourceDefinition SourceDefinition;
	FBattleTriggerSubject Owner;
	FBattleTriggerSubject Source;
	TArray<FBattleTriggerSubject> Targets;
	FBattleTriggerSubject DurationOwner;
	TOptional<int32> RemainingTurns;
	int32 Layers = 0;
	FBattleTriggerVisibility Visibility;
	FBattleTriggerReentrancyToken ReentrancyToken;
	TOptional<FBattleTriggerSimultaneousGroupId> SimultaneousGroupId;
	FBattleTriggerResolvedOrder ResolvedOrder;
};

/** Fact-only lifecycle family; presentation text and animation timing are intentionally absent. */
enum class EBattleTriggerLifecycleFactKind : uint8
{
	Started = 0,
	DurationChanged = 1,
	LayerChanged = 2,
	SuppressionChanged = 3,
	Ended = 4
};

/** Typed terminal reason for an Ended lifecycle fact. */
enum class EBattleTriggerEndReason : uint8
{
	Expired = 0,
	Switch = 1,
	Faint = 2,
	Capture = 3,
	BattleEnd = 4,
	Removal = 5
};

/** Ordered lifecycle fact containing values only, with no display or timing fields. */
struct POKEMONSOLARUS_API FBattleTriggerLifecycleFact
{
	uint64 FactOrdinal = 0;
	EBattleTriggerLifecycleFactKind Kind = EBattleTriggerLifecycleFactKind::Started;
	FBattleTriggerRegistrationId RegistrationId;
	EBattleTriggerPhase Phase = EBattleTriggerPhase::BattleStart;
	FBattleTriggerSourceDefinition SourceDefinition;
	FBattleTriggerSubject Owner;
	TOptional<FBattleTriggerReentrancyToken> ReentrancyToken;
	TOptional<FBattleTriggerSimultaneousGroupId> SimultaneousGroupId;
	TOptional<int32> PreviousRemainingTurns;
	TOptional<int32> RemainingTurns;
	TOptional<int32> PreviousLayers;
	TOptional<int32> Layers;
	TOptional<bool> WasSuppressed;
	TOptional<bool> IsSuppressed;
	TOptional<EBattleTriggerEndReason> EndReason;
};

/** Stable validation failure; errors never carry presentation strings. */
enum class EBattleTriggerError : uint8
{
	None = 0,
	InvalidPhase = 1,
	InvalidDefinition = 2,
	InvalidSubject = 3,
	InvalidVisibility = 4,
	InvalidDuration = 5,
	InvalidLayers = 6,
	InvalidCleanupPolicy = 7,
	InvalidOrderPolicy = 8,
	InvalidRegistrationId = 9,
	InvalidReentrancyToken = 10,
	InvalidSimultaneousGroup = 11,
	InvalidParticipant = 12,
	DuplicateParticipant = 13,
	MissingEffectiveSpeed = 14,
	InvalidCleanupRequest = 15,
	RegistrationNotFound = 16,
	QueueEmpty = 17
};

/** Summary of one resolved queued dispatch. Effect requests remain queued until explicitly drained. */
struct POKEMONSOLARUS_API FBattleTriggerDispatchResult
{
	EBattleTriggerPhase Phase = EBattleTriggerPhase::BattleStart;
	int32 ConsideredCount = 0;
	int32 EffectRequestCount = 0;
	int32 ExpiredCount = 0;
	bool bQueuedExpiryDispatch = false;
};

/**
 * Standalone C07A scheduler. It owns registrations and queues declarative output only.
 * It never invokes callbacks, mutates battle state, or accepts an RNG dependency.
 */
class POKEMONSOLARUS_API FBattleTriggerFramework
{
public:
	/** C07A ordering never consumes a random draw, including exact ties. */
	[[nodiscard]] static constexpr bool ConsumesRandomness() { return false; }

	[[nodiscard]] bool TryRegister(
		const FBattleTriggerRegistrationSpec& Spec,
		FBattleTriggerRegistrationId& OutRegistrationId,
		EBattleTriggerError& OutError);
	[[nodiscard]] bool TryEnqueueDispatch(
		const FBattleTriggerDispatchSpec& Spec,
		EBattleTriggerError& OutError);
	[[nodiscard]] bool TryResolveNextDispatch(
		FBattleTriggerDispatchResult& OutResult,
		EBattleTriggerError& OutError);
	[[nodiscard]] bool TryUpdateLayers(
		FBattleTriggerRegistrationId RegistrationId,
		int32 NewLayers,
		const FBattleTriggerOperationContext& Context,
		EBattleTriggerError& OutError);
	[[nodiscard]] bool TrySetSuppressed(
		FBattleTriggerRegistrationId RegistrationId,
		bool bSuppressed,
		const FBattleTriggerOperationContext& Context,
		EBattleTriggerError& OutError);
	[[nodiscard]] bool TryApplyCleanup(
		const FBattleTriggerCleanupRequest& Request,
		EBattleTriggerError& OutError);

	[[nodiscard]] bool TryGetRegistration(
		FBattleTriggerRegistrationId RegistrationId,
		FBattleTriggerRegistrationState& OutState) const;
	[[nodiscard]] TArray<FBattleTriggerRegistrationState> GetActiveRegistrations() const;
	[[nodiscard]] int32 GetPendingDispatchCount() const { return PendingDispatches.Num(); }
	[[nodiscard]] int32 GetPendingEffectRequestCount() const { return PendingEffectRequests.Num(); }
	[[nodiscard]] int32 GetPendingLifecycleFactCount() const { return PendingLifecycleFacts.Num(); }

	void DrainEffectRequests(TArray<FBattleTriggerEffectRequest>& OutRequests);
	void DrainLifecycleFacts(TArray<FBattleTriggerLifecycleFact>& OutFacts);

private:
	struct FRuntimeRegistration
	{
		FBattleTriggerRegistrationId RegistrationId;
		uint64 CreationOrdinal = 0;
		FBattleTriggerRegistrationSpec Spec;
		TOptional<int32> RemainingTurns;
		int32 Layers = 0;
		bool bSuppressed = false;
		TSet<uint64> ExecutedTokens;
		TSet<uint64> DurationTickTokens;
	};

	[[nodiscard]] int32 FindRegistrationIndex(FBattleTriggerRegistrationId RegistrationId) const;
	[[nodiscard]] bool ValidateDispatch(
		const FBattleTriggerDispatchSpec& Spec,
		EBattleTriggerError& OutError) const;
	void AppendLifecycleFact(FBattleTriggerLifecycleFact&& Fact);

	TArray<FRuntimeRegistration> Registrations;
	TArray<FBattleTriggerDispatchSpec> PendingDispatches;
	TArray<FBattleTriggerEffectRequest> PendingEffectRequests;
	TArray<FBattleTriggerLifecycleFact> PendingLifecycleFacts;
	uint64 NextRegistrationValue = 1;
	uint64 NextCreationOrdinal = 1;
	uint64 NextEffectRequestOrdinal = 1;
	uint64 NextLifecycleFactOrdinal = 1;
};
