#pragma once

#include "CoreMinimal.h"
#include "Battle/BattleSetup.h"
#include "Battle/BattleTriggerFramework.h"

struct FBattleHeldItemInstanceIdTag;

/** Stable identity of one held-item instance inside a battle. */
using FBattleHeldItemInstanceId = TNumericBattleId<FBattleHeldItemInstanceIdTag>;

/** Semantic hook contexts shared by Ability and item definitions. */
enum class EBattleAbilityItemHookPoint : uint8
{
	SelectionEligibility = 0,
	ActionEligibility = 1,
	ActionPriority = 2,
	Speed = 3,
	Accuracy = 4,
	TargetReachability = 5,
	TypeImmunity = 6,
	Power = 7,
	OffensiveStat = 8,
	DefensiveStat = 9,
	FinalDamage = 10,
	StatusApplication = 11,
	EffectApplication = 12,
	SwitchOut = 13,
	SwitchIn = 14,
	ItemUse = 15,
	AfterDamage = 16,
	EndTurn = 17,
	FaintPrevention = 18,
	FieldCreation = 19,
	Invalid = 255
};

/** Typed operation emitted by a semantic Ability or item hook. */
enum class EBattleAbilityItemEffectKind : uint8
{
	Modify = 0,
	Prevent = 1,
	CreateField = 2,
	Suppress = 3,
	Ignore = 4,
	Reveal = 5,
	ConsumeItem = 6,
	RestoreItem = 7,
	RemoveItem = 8,
	SwapItems = 9,
	TemporarilyStealItem = 10,
	Invalid = 255
};

/** Rule-selected point at which a normally hidden definition becomes public. */
enum class EBattleAbilityItemRevealPolicy : uint8
{
	Never = 0,
	OnAppliedEffect = 1,
	OnPublicAttempt = 2,
	Invalid = 255
};

/** Semantic outcome supplied after a typed hook request is evaluated. */
enum class EBattleAbilityItemActivationOutcome : uint8
{
	Applied = 0,
	AttemptedButPrevented = 1,
	Ineligible = 2,
	Suppressed = 3,
	Ignored = 4,
	Invalid = 255
};

/** Stable C08A hook-contract failure. */
enum class EBattleAbilityItemHookError : uint8
{
	None = 0,
	InvalidDefinition = 1,
	InvalidSourceDefinition = 2,
	InvalidRegistration = 3,
	MismatchedTriggerRequest = 4,
	InvalidActivationOutcome = 5
};

/** Immutable semantic metadata paired with one C07A trigger rule. */
struct POKEMONSOLARUS_API FBattleAbilityItemHookDefinition
{
	FDefinitionId HookId;
	EBattleAbilityItemHookPoint HookPoint = EBattleAbilityItemHookPoint::Invalid;
	EBattleAbilityItemEffectKind EffectKind = EBattleAbilityItemEffectKind::Invalid;
	FBattleTriggerRuleDefinition TriggerRule;
	EBattleAbilityItemRevealPolicy RevealPolicy = EBattleAbilityItemRevealPolicy::Never;
	bool bBreakable = false;
};

/** Value-only runtime facts used to register one Ability or item hook with C07A. */
struct POKEMONSOLARUS_API FBattleAbilityItemHookRegistrationFacts
{
	FBattleAbilityItemHookDefinition Definition;
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

/** C07A request paired with the semantic operation that a later executor may apply. */
struct POKEMONSOLARUS_API FBattleAbilityItemEffectRequest
{
	FBattleTriggerEffectRequest TriggerRequest;
	FDefinitionId HookId;
	EBattleAbilityItemHookPoint HookPoint = EBattleAbilityItemHookPoint::Invalid;
	EBattleAbilityItemEffectKind EffectKind = EBattleAbilityItemEffectKind::Invalid;
	EBattleAbilityItemRevealPolicy RevealPolicy = EBattleAbilityItemRevealPolicy::Never;
	bool bBreakable = false;
};

/** Definition-and-owner key used to remember whether a public reveal already occurred. */
struct POKEMONSOLARUS_API FBattleAbilityItemRevealKey
{
	FBattleTriggerSourceDefinition SourceDefinition;
	FBattleTriggerSubject Owner;

	friend bool operator==(
		const FBattleAbilityItemRevealKey& Left,
		const FBattleAbilityItemRevealKey& Right)
	{
		return Left.SourceDefinition == Right.SourceDefinition
			&& Left.Owner == Right.Owner;
	}
};

/** Public-safe activation fact; the source definition is absent unless the rule reveals it. */
struct POKEMONSOLARUS_API FBattleAbilityItemActivationFact
{
	EBattleAbilityItemHookPoint HookPoint = EBattleAbilityItemHookPoint::Invalid;
	EBattleAbilityItemEffectKind EffectKind = EBattleAbilityItemEffectKind::Invalid;
	EBattleAbilityItemActivationOutcome Outcome = EBattleAbilityItemActivationOutcome::Invalid;
	FBattleTriggerSubject Owner;
	FBattleTriggerVisibility Visibility;
	TOptional<FBattleTriggerSourceDefinition> RevealedSourceDefinition;
	bool bFirstPublicReveal = false;
};

/**
 * Plain-C++ bridge from semantic Ability/item hooks into C07A's deterministic scheduler.
 * It emits typed work and public-safe facts only; it never mutates battle state.
 */
class POKEMONSOLARUS_API FBattleAbilityItemHookContracts
{
public:
	/** Returns whether the semantic hook metadata is complete and uses known enum values. */
	[[nodiscard]] static bool IsDefinitionValid(
		const FBattleAbilityItemHookDefinition& Definition);

	/** Builds and fully validates one C07A registration without registering it. */
	[[nodiscard]] static bool TryBuildTriggerRegistration(
		const FBattleAbilityItemHookRegistrationFacts& Facts,
		FBattleTriggerRegistrationSpec& OutRegistration,
		EBattleAbilityItemHookError& OutError);

	/** Adds a validated Ability/item registration to the supplied C07A framework. */
	[[nodiscard]] static bool TryRegisterHook(
		FBattleTriggerFramework& Framework,
		const FBattleAbilityItemHookRegistrationFacts& Facts,
		FBattleTriggerRegistrationId& OutRegistrationId,
		EBattleAbilityItemHookError& OutError);

	/** Converts one matching C07A request into semantic typed work for a later executor. */
	[[nodiscard]] static bool TryCreateTypedEffectRequest(
		const FBattleAbilityItemHookDefinition& Definition,
		const FBattleTriggerEffectRequest& TriggerRequest,
		FBattleAbilityItemEffectRequest& OutRequest,
		EBattleAbilityItemHookError& OutError);
};

/** Reveal-state owner that prevents hidden failures from leaking Ability or item identities. */
class POKEMONSOLARUS_API FBattleAbilityItemRevealTracker
{
public:
	/**
	 * Resolves an evaluated activation into an optional public-safe fact.
	 * Ineligible, suppressed, ignored, and non-public failed attempts emit no fact.
	 */
	[[nodiscard]] bool TryRecordActivation(
		const FBattleAbilityItemEffectRequest& Request,
		EBattleAbilityItemActivationOutcome Outcome,
		TOptional<FBattleAbilityItemActivationFact>& OutFact,
		EBattleAbilityItemHookError& OutError);

	/** Returns whether this definition has already been publicly revealed for this owner. */
	[[nodiscard]] bool HasBeenRevealed(
		const FBattleTriggerSourceDefinition& SourceDefinition,
		const FBattleTriggerSubject& Owner) const;

private:
	TArray<FBattleAbilityItemRevealKey> RevealedKeys;
};

/** Whether a held-item instance entered battle from persistence or was generated in battle. */
enum class EBattleHeldItemOrigin : uint8
{
	Persistent = 0,
	BattleGenerated = 1,
	Invalid = 255
};

/** Typed mutation allowed against the transient held-item ledger. */
enum class EBattleHeldItemOperationKind : uint8
{
	Suppress = 0,
	Reveal = 1,
	Consume = 2,
	Restore = 3,
	Remove = 4,
	Swap = 5,
	TemporarilySteal = 6,
	Invalid = 255
};

/** Stable held-item ownership-contract failure. */
enum class EBattleHeldItemContractError : uint8
{
	None = 0,
	InvalidState = 1,
	DuplicateInstance = 2,
	DuplicateOriginalOwner = 3,
	DuplicateCurrentHolder = 4,
	InvalidOperation = 5,
	InstanceNotFound = 6,
	HolderOccupied = 7,
	InvalidCapturedOwner = 8
};

/** Final persistent disposition emitted for one held-item instance. */
enum class EBattleHeldItemFinalDisposition : uint8
{
	OriginalOwner = 0,
	CapturedOriginalOwner = 1,
	Consumed = 2,
	BattleGeneratedRemoved = 3,
	Invalid = 255
};

/** Original ownership plus complete current transient state for one held-item instance. */
struct POKEMONSOLARUS_API FBattleHeldItemInstanceState
{
	FBattleHeldItemInstanceId InstanceId;
	EBattleHeldItemOrigin Origin = EBattleHeldItemOrigin::Invalid;
	FItemId DefinitionItemId;
	FTrainerId OriginalOwnerTrainerId;
	FBattlerId OriginalOwnerBattlerId;
	FItemId OriginalItemId;
	FTrainerId CurrentHolderTrainerId;
	FBattlerId CurrentHolderBattlerId;
	FItemId CurrentItemId;
	bool bConsumed = false;
	bool bSuppressed = false;
	bool bRevealed = false;
	bool bTemporarilyRemoved = false;
	bool bRestoredAfterConsumption = false;

	friend bool operator==(
		const FBattleHeldItemInstanceState& Left,
		const FBattleHeldItemInstanceState& Right)
	{
		return Left.InstanceId == Right.InstanceId
			&& Left.Origin == Right.Origin
			&& Left.DefinitionItemId == Right.DefinitionItemId
			&& Left.OriginalOwnerTrainerId == Right.OriginalOwnerTrainerId
			&& Left.OriginalOwnerBattlerId == Right.OriginalOwnerBattlerId
			&& Left.OriginalItemId == Right.OriginalItemId
			&& Left.CurrentHolderTrainerId == Right.CurrentHolderTrainerId
			&& Left.CurrentHolderBattlerId == Right.CurrentHolderBattlerId
			&& Left.CurrentItemId == Right.CurrentItemId
			&& Left.bConsumed == Right.bConsumed
			&& Left.bSuppressed == Right.bSuppressed
			&& Left.bRevealed == Right.bRevealed
			&& Left.bTemporarilyRemoved == Right.bTemporarilyRemoved
			&& Left.bRestoredAfterConsumption == Right.bRestoredAfterConsumption;
	}
};

/** One typed request against the transient held-item ledger. */
struct POKEMONSOLARUS_API FBattleHeldItemOperationRequest
{
	EBattleHeldItemOperationKind Kind = EBattleHeldItemOperationKind::Invalid;
	FBattleHeldItemInstanceId PrimaryInstanceId;
	FBattleHeldItemInstanceId SecondaryInstanceId;
	FTrainerId TargetHolderTrainerId;
	FBattlerId TargetHolderBattlerId;
	bool bSuppressed = false;
};

/** Atomic before/after fact emitted for a successful held-item operation. */
struct POKEMONSOLARUS_API FBattleHeldItemOperationFact
{
	uint64 FactOrdinal = 0;
	EBattleHeldItemOperationKind Kind = EBattleHeldItemOperationKind::Invalid;
	FBattleHeldItemInstanceState PrimaryBefore;
	FBattleHeldItemInstanceState PrimaryAfter;
	TOptional<FBattleHeldItemInstanceState> SecondaryBefore;
	TOptional<FBattleHeldItemInstanceState> SecondaryAfter;
};

/** Final item fact suitable for a future inventory or captured-Pokemon service. */
struct POKEMONSOLARUS_API FBattleFinalHeldItemFact
{
	FBattleHeldItemInstanceId InstanceId;
	FItemId DefinitionItemId;
	FTrainerId OriginalOwnerTrainerId;
	FBattlerId OriginalOwnerBattlerId;
	FItemId OriginalItemId;
	EBattleHeldItemFinalDisposition Disposition = EBattleHeldItemFinalDisposition::Invalid;
	FTrainerId FinalOwnerTrainerId;
	FBattlerId FinalOwnerBattlerId;
	FItemId FinalItemId;
	bool bRestoredAfterConsumption = false;
};

/**
 * Plain-C++ item-instance ledger. Every transient ownership change is atomic and typed.
 * Battle-end resolution emits facts only and never writes a persistent inventory.
 */
class POKEMONSOLARUS_API FBattleHeldItemLedger
{
public:
	/** Validates and canonicalizes a complete item-instance snapshot atomically. */
	[[nodiscard]] static bool TryCreate(
		TConstArrayView<FBattleHeldItemInstanceState> InitialStates,
		FBattleHeldItemLedger& OutLedger,
		EBattleHeldItemContractError& OutError);

	/** Applies one typed transient operation atomically and emits its before/after fact. */
	[[nodiscard]] bool TryApplyOperation(
		const FBattleHeldItemOperationRequest& Request,
		FBattleHeldItemOperationFact& OutFact,
		EBattleHeldItemContractError& OutError);

	/** Builds canonical battle-end facts, resetting temporary ownership by construction. */
	[[nodiscard]] bool TryBuildFinalFacts(
		TConstArrayView<FBattlerId> CapturedOriginalOwners,
		TArray<FBattleFinalHeldItemFact>& OutFacts,
		EBattleHeldItemContractError& OutError) const;

	/** Returns item instances in stable numeric-instance order. */
	[[nodiscard]] TConstArrayView<FBattleHeldItemInstanceState> GetStates() const
	{
		return States;
	}

	/** Finds one immutable item-instance state, or null. */
	[[nodiscard]] const FBattleHeldItemInstanceState* FindState(
		FBattleHeldItemInstanceId InstanceId) const;

private:
	TArray<FBattleHeldItemInstanceState> States;
	uint64 NextFactOrdinal = 1;
};

/** One Trainer's finite battle-local Bag snapshot and per-turn Bag quota. */
struct POKEMONSOLARUS_API FBattleTrainerBagState
{
	FTrainerId TrainerId;
	TArray<FBattleBagItemCount> Items;
	bool bBagActionAvailable = true;
};

/** Pre-use facts needed to evaluate one Trainer-owned Bag action. */
struct POKEMONSOLARUS_API FBattleBagUseRequest
{
	FTrainerId ActingTrainerId;
	FItemId ItemId;
	FTrainerId TargetOwnerTrainerId;
	bool bItemExplicitlyAllowsOtherTrainerTarget = false;
	bool bItemSpecificTargetLegal = false;
	bool bEffectPreventedAfterLegalUse = false;
};

/** Stable pre-use rejection reason; a rejected request consumes no resource. */
enum class EBattleBagUseRejectionReason : uint8
{
	None = 0,
	BagQuotaUsed = 1,
	NoItemRemaining = 2,
	WrongTargetOwner = 3,
	IllegalItemTarget = 4,
	Invalid = 255
};

/** Resource outcome of evaluating one Bag request. */
enum class EBattleBagUseOutcome : uint8
{
	PreUseRejected = 0,
	Applied = 1,
	EffectPreventedAfterLegalUse = 2,
	Invalid = 255
};

/** Complete typed result of a Bag use evaluation. */
struct POKEMONSOLARUS_API FBattleBagUseResult
{
	bool bValid = false;
	EBattleBagUseOutcome Outcome = EBattleBagUseOutcome::Invalid;
	EBattleBagUseRejectionReason RejectionReason = EBattleBagUseRejectionReason::Invalid;
	int32 CountBefore = 0;
	int32 CountAfter = 0;
	bool bItemConsumed = false;
	bool bActionConsumed = false;
};

/** Stable Trainer-Bag contract failure distinct from a valid pre-use rejection. */
enum class EBattleBagContractError : uint8
{
	None = 0,
	InvalidState = 1,
	DuplicateTrainer = 2,
	DuplicateItem = 3,
	InvalidRequest = 4,
	TrainerNotFound = 5
};

/**
 * Battle-local Bag ownership and quota evaluator.
 * It owns copied counts only; it performs no persistent inventory write.
 */
class POKEMONSOLARUS_API FBattleBagOwnershipContract
{
public:
	/** Validates and canonicalizes separate Trainer Bag snapshots atomically. */
	[[nodiscard]] static bool TryCreate(
		TConstArrayView<FBattleTrainerBagState> InitialStates,
		FBattleBagOwnershipContract& OutContract,
		EBattleBagContractError& OutError);

	/** Evaluates and, for a legal use, consumes exactly one item and that Trainer's quota. */
	[[nodiscard]] bool TryApplyUse(
		const FBattleBagUseRequest& Request,
		FBattleBagUseResult& OutResult,
		EBattleBagContractError& OutError);

	/** Restores one Bag action for every Trainer at the next normal-turn boundary. */
	void ResetTurnQuotas();

	/** Returns Trainer Bag states in stable Trainer-ID order. */
	[[nodiscard]] TConstArrayView<FBattleTrainerBagState> GetTrainerStates() const
	{
		return TrainerStates;
	}

	/** Finds one immutable Trainer Bag state, or null. */
	[[nodiscard]] const FBattleTrainerBagState* FindTrainerState(FTrainerId TrainerId) const;

private:
	TArray<FBattleTrainerBagState> TrainerStates;
};
