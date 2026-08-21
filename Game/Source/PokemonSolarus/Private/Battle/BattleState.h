#pragma once

#include "CoreMinimal.h"
#include "Battle/BattleDefinitionCatalog.h"
#include "Battle/BattleReplay.h"
#include "Battle/BattleStatStages.h"

/** Typed result from validating the authoritative internal battle state. */
enum class EBattleStateValidationError : uint8
{
	None = 0,
	InvalidSetup = 1,
	InvalidCatalog = 2,
	MissingCatalogReference = 3,
	InvalidLifecycle = 4,
	InvalidTrainer = 5,
	DuplicateTrainer = 6,
	InvalidParty = 7,
	DuplicateBattler = 8,
	InvalidBattler = 9,
	InvalidHP = 10,
	InvalidPP = 11,
	InvalidStage = 12,
	InvalidResource = 13,
	InvalidActivePosition = 14,
	DuplicateActiveBattler = 15,
	InvalidCondition = 16,
	InvalidPendingCapture = 17,
	InvalidWildFleePolicy = 18,
	InvalidCounter = 19,
	InvalidEventOrder = 20
};

/** Per-turn action capacity owned by one Trainer. */
struct FBattleTrainerActionAllowance
{
	int32 MaximumActions = 0;
	int32 RemainingActions = 0;
	bool bBagActionAvailable = true;
};

/** One of the six structural party positions owned by a Trainer. */
struct FBattlePartySlotState
{
	FPartySlotId PartySlotId;
	FBattlerId BattlerId;
};

/** Authoritative mutable Trainer facts. */
struct FBattleTrainerState
{
	FTrainerId TrainerId;
	EBattleSide Side = EBattleSide::Player;
	EBattleTrainerRole Role = EBattleTrainerRole::Player;
	EBattleDecisionController Controller = EBattleDecisionController::Human;
	FDefinitionId SelectorProfileId;
	TArray<FBattleBagItemCount> Bag;
	FBattleTrainerActionAllowance ActionAllowance;
	TArray<FBattlePartySlotState> PartySlots;
};

/** One mutable move slot with invariant-safe PP bounds. */
struct FBattleMoveSlotState
{
	uint8 SlotIndex = 255;
	FMoveId MoveId;
	int32 CurrentPP = 0;
	int32 MaxPP = 0;
};

/** Original and current held-item ownership are separate battle facts. */
struct FBattleHeldItemState
{
	FItemId OriginalItemId;
	FItemId CurrentItemId;
	bool bConsumed = false;
	bool bSuppressed = false;
};

/** Frozen standard-obedience facts copied from the validated setup. */
struct FBattleObedienceState
{
	bool bHasSnapshot = false;
	bool bSubjectToPlayerCap = false;
	uint8 ReferenceLevel = 0;
	uint8 BadgeCount = 0;
};

/** Generic typed condition storage; later packages supply behavior. */
struct FBattleConditionState
{
	FConditionId ConditionId;
	TOptional<int32> RemainingTurns;
	int32 LayerCount = 0;
	uint64 CreationOrdinal = 0;
	FBattlerId SourceBattlerId;
};

/** Authoritative mutable battler facts. */
struct FBattleBattlerState
{
	FTrainerId TrainerId;
	FBattlerId BattlerId;
	FSourcePokemonId SourcePokemonId;
	FPartySlotId PartySlotId;
	FSpeciesFormId SpeciesFormId;
	int32 Level = 0;
	FPokemonBattleStats PermanentStats;
	int32 CurrentHP = 0;
	bool bFainted = false;
	bool bCaptured = false;
	bool bRemoved = false;
	bool bFaintTransitionPending = false;
	bool bEgg = false;
	FConditionId MajorStatusId;
	FBattleStatStages Stages;
	TArray<FBattleConditionState> Volatiles;
	FAbilityId AbilityId;
	FBattleHeldItemState HeldItem;
	TArray<FBattleMoveSlotState> Moves;
	FBattleObedienceState Obedience;
};

/** One of the four side/position records stored in every format. */
struct FBattleActivePositionState
{
	FActiveSlotId ActiveSlotId;
	bool bAvailable = false;
	FTrainerId TrainerId;
	FBattlerId BattlerId;
};

/** Field-owned condition collections; C07 supplies their behavior. */
struct FBattleFieldState
{
	TOptional<FBattleConditionState> Weather;
	TOptional<FBattleConditionState> Terrain;
	TArray<FBattleConditionState> Rooms;
	TArray<FBattleConditionState> Effects;
};

/** Side-owned conditions and entry hazards; C07 supplies their behavior. */
struct FBattleSideState
{
	EBattleSide Side = EBattleSide::Player;
	TArray<FBattleConditionState> Conditions;
	TArray<FBattleConditionState> Hazards;
};

/** Captured battle facts retained until an external post-battle system consumes them. */
struct FBattlePendingCaptureState
{
	FBattlerId BattlerId;
	FSourcePokemonId SourcePokemonId;
	FSpeciesFormId SpeciesFormId;
	int32 CurrentHP = 0;
	int32 MaxHP = 0;
	FConditionId MajorStatusId;
	TArray<FBattleMoveSlotState> Moves;
	FBattleHeldItemState HeldItem;
};

/** Authored flee-policy storage. Empty storage means fleeing is disabled. */
struct FBattleWildFleePolicyState
{
	FSpeciesFormId SpeciesFormId;
	FDefinitionId TriggerId;
	FDefinitionId EligibilityId;
	EBattleWildFleeMode ProbabilityMode = EBattleWildFleeMode::Disabled;
	uint32 Numerator = 0;
	uint32 Denominator = 0;
};

/** One selected action retained while the normal-turn queue is locked. */
struct FBattleLockedActionState
{
	FActionId ActionId;
	uint64 QueueOrdinal = 0;
	FBattleDecision Decision;
	FBattleActionOrderKey OrderKey;
	FBattlerId SelectedTargetBattlerId;
	bool bStarted = false;
	bool bMoveCommitted = false;
	bool bFinished = false;
};

/** One active battler awaiting a choice inside a Trainer-owned Left/Right group. */
struct FBattleDecisionActorState
{
	FBattlerId BattlerId;
	FActiveSlotId ActiveSlotId;
};

/** One stable decision-owner group in the C03B human/partner/enemy sequence. */
struct FBattleDecisionOwnerState
{
	FTrainerId TrainerId;
	EBattleDecisionController Controller = EBattleDecisionController::Human;
	TArray<FBattleDecisionActorState> Actors;
};

/**
 * Single authoritative mutable state owned by FBattleEngine.
 * The header is private to the runtime module; later rule packages consume const queries.
 */
class FBattleEngineState
{
public:
	/** Builds state atomically from validated setup facts and an optional frozen catalog. */
	[[nodiscard]] static bool TryCreate(
		const FBattleSetup& Setup,
		const FBattleDefinitionCatalog* Catalog,
		TUniquePtr<IBattleRandom>&& Random,
		TUniquePtr<FBattleEngineState>& OutState,
		EBattleStateValidationError& OutError);

	/** Validates every cross-record invariant without changing state. */
	[[nodiscard]] bool ValidateInvariants(EBattleStateValidationError& OutError) const;

	[[nodiscard]] TConstArrayView<FBattleTrainerState> GetTrainers() const { return Trainers; }
	[[nodiscard]] TConstArrayView<FBattleBattlerState> GetBattlers() const { return Battlers; }
	[[nodiscard]] TConstArrayView<FBattleActivePositionState> GetActivePositions() const { return ActivePositions; }
	[[nodiscard]] TConstArrayView<FBattleSideState> GetSides() const { return Sides; }
	[[nodiscard]] TConstArrayView<FBattlePendingCaptureState> GetPendingCaptures() const { return PendingCaptures; }
	[[nodiscard]] TConstArrayView<FBattleWildFleePolicyState> GetWildFleePolicies() const { return WildFleePolicies; }
	[[nodiscard]] TConstArrayView<FBattleLockedActionState> GetLockedActions() const { return LockedActions; }
	[[nodiscard]] TConstArrayView<FBattleDecisionRequest> GetPendingDecisionRequests() const { return PendingDecisionRequests; }
	[[nodiscard]] TConstArrayView<FBattleDecision> GetAcceptedSelections() const { return AcceptedSelections; }
	[[nodiscard]] TConstArrayView<FBattleEvent> GetOrderedEvents() const { return OrderedEvents; }
	[[nodiscard]] FBattleId GetBattleId() const { return Setup.GetBattleId(); }
	[[nodiscard]] FTurnId GetTurnId() const { return TurnId; }
	[[nodiscard]] EBattleEncounterKind GetEncounterKind() const { return EncounterKind; }
	[[nodiscard]] EBattleFormat GetFormat() const { return Format; }
	[[nodiscard]] EBattlePhase GetPhase() const { return Phase; }
	[[nodiscard]] EBattleOutcome GetOutcome() const { return Outcome; }
	[[nodiscard]] EBattleOutcomeCause GetOutcomeCause() const { return OutcomeCause; }
	[[nodiscard]] uint64 GetStateVersion() const { return StateVersion; }
	[[nodiscard]] uint32 GetEscapeAttemptCount() const { return EscapeAttemptCount; }
	[[nodiscard]] bool HasSuccessfulReinforcement() const { return bReinforcementSucceeded; }
	[[nodiscard]] bool HasCatalog() const { return bHasCatalog; }

	[[nodiscard]] const FBattleTrainerState* FindTrainer(FTrainerId TrainerId) const;
	[[nodiscard]] FBattleTrainerState* FindMutableTrainer(FTrainerId TrainerId);
	[[nodiscard]] const FBattleBattlerState* FindBattler(FBattlerId BattlerId) const;
	[[nodiscard]] FBattleBattlerState* FindMutableBattler(FBattlerId BattlerId);
	[[nodiscard]] const FBattleActivePositionState* FindActivePosition(FActiveSlotId ActiveSlotId) const;
	[[nodiscard]] FBattleActivePositionState* FindMutableActivePosition(FActiveSlotId ActiveSlotId);

	/** Builds the existing minimal snapshot Trainer projection from authoritative state. */
	[[nodiscard]] TArray<FBattleTrainerSetup> BuildTrainerProjection() const;
	/** Builds the existing minimal snapshot battler projection from authoritative state. */
	[[nodiscard]] TArray<FBattlePartyEntrySetup> BuildPartyProjection() const;
	/** Builds occupied active assignments from all four structural positions. */
	[[nodiscard]] TArray<FBattleActiveAssignment> BuildActiveProjection() const;

	/** Adds one operation resolution and its ordered events to the authoritative history. */
	void AppendResolution(const FBattleResolution& Resolution);

	FBattleSetup Setup;
	FBattleDefinitionCatalog Catalog;
	bool bHasCatalog = false;
	uint64 StateVersion = 1;
	FTurnId TurnId;
	EBattleEncounterKind EncounterKind = EBattleEncounterKind::Wild;
	EBattleFormat Format = EBattleFormat::Single;
	EBattlePhase Phase = EBattlePhase::Setup;
	EBattleOutcome Outcome = EBattleOutcome::InProgress;
	EBattleOutcomeCause OutcomeCause = EBattleOutcomeCause::None;
	TArray<FBattleTrainerState> Trainers;
	TArray<FBattleBattlerState> Battlers;
	TArray<FBattleActivePositionState> ActivePositions;
	FBattleFieldState Field;
	TArray<FBattleSideState> Sides;
	TArray<FBattlePendingCaptureState> PendingCaptures;
	bool bReinforcementSucceeded = false;
	uint32 EscapeAttemptCount = 1;
	FBattleCaptureCapacitySnapshot CaptureCapacity;
	FBattleEncounterPolicies EncounterPolicies;
	TArray<FBattleWildFleePolicyState> WildFleePolicies;
	TArray<FBattleLockedActionState> LockedActions;
	bool bLockedOrderReversesSpeed = false;
	int32 CurrentLockedActionIndex = 0;
	TOptional<FBattleDecisionRequest> PendingDecision;
	TArray<FBattleDecisionOwnerState> DecisionOwnerSequence;
	int32 CurrentDecisionOwnerIndex = INDEX_NONE;
	int32 CurrentDecisionActorOffset = 0;
	TArray<FBattleDecisionRequest> PendingDecisionRequests;
	TArray<FBattleDecision> AcceptedSelections;
	TUniquePtr<IBattleRandom> Random;
	uint64 NextResolutionId = 1;
	uint64 NextActionId = 1;
	uint64 NextEventOrdinal = 1;
	uint64 NextConditionCreationOrdinal = 1;
	TArray<uint64> AvailableOpponentRemovalCheckpoints;
	TArray<FBattleDecision> SubmittedDecisions;
	TArray<FBattleBetweenActionsStatRefresh> SubmittedStatRefreshes;
	TArray<FBattleResolution> Resolutions;
	TArray<FBattleEvent> OrderedEvents;
};
