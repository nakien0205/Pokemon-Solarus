#pragma once

#include "CoreMinimal.h"
#include "Battle/BattleDefinitions.h"
#include "Battle/BattleRandom.h"
#include "Battle/BattleTriggerFramework.h"

/** The approved C07C volatile proof set. */
enum class EBattleVolatileKind : uint8
{
	None = 0,
	Confusion = 1,
	Flinch = 2,
	Protect = 3,
	LeechSeed = 4,
	PartialTrap = 5,
	Trap = 6,
	Taunt = 7,
	Encore = 8,
	Disable = 9,
	Substitute = 10,
	Charging = 11,
	Recharge = 12,
	FlySemiInvulnerable = 13,
	Invalid = 255
};

/** Stable result of checking whether one approved volatile can be applied. */
enum class EBattleVolatileApplicationOutcome : uint8
{
	CanApply = 0,
	AlreadyPresent = 1,
	TypeImmune = 2,
	PreventedByTerrain = 3,
	PreventedBySafeguard = 4,
	TargetAlreadyActed = 5,
	InvalidLastMove = 6,
	LastMoveHasNoPP = 7,
	LastMoveUnencoreable = 8,
	LastMoveIsStruggle = 9,
	InsufficientHP = 10,
	Invalid = 255
};

/** Complete value facts used by canonical volatile application checks. */
struct POKEMONSOLARUS_API FBattleVolatileApplicationFacts
{
	FConditionId RequestedVolatileId;
	bool bAlreadyPresent = false;
	EPokemonType PrimaryType = EPokemonType::Invalid;
	EPokemonType SecondaryType = EPokemonType::Invalid;
	bool bTargetGrounded = false;
	bool bMistyTerrainActive = false;
	bool bSafeguardActive = false;
	bool bAppliedByOpponent = false;
	bool bBypassesSafeguard = false;
	bool bTargetAlreadyActed = false;
	FMoveId LastMoveId;
	int32 LastMoveCurrentPP = 0;
	bool bLastMoveUnencoreable = false;
	bool bLastMoveIsStruggle = false;
	int32 BaseMaximumHP = 0;
	int32 CurrentHP = 0;
};

/** Typed, presentation-free volatile application result. */
struct POKEMONSOLARUS_API FBattleVolatileApplicationResult
{
	bool bValid = false;
	EBattleVolatileKind Kind = EBattleVolatileKind::Invalid;
	EBattleVolatileApplicationOutcome Outcome = EBattleVolatileApplicationOutcome::Invalid;
};

/** One canonical volatile duration draw. */
struct POKEMONSOLARUS_API FBattleVolatileDurationResult
{
	bool bValid = false;
	int32 Turns = 0;
	FBattleRandomDraw Draw;
};

/** Before-action outcomes shared by Confusion, Flinch, and Recharge. */
enum class EBattleVolatileActionOutcome : uint8
{
	Allowed = 0,
	CuredAndAllowed = 1,
	Denied = 2,
	ConfusionSelfHit = 3,
	Invalid = 255
};

/** Action-gate result, including duration changes and at most one semantic draw. */
struct POKEMONSOLARUS_API FBattleVolatileActionResult
{
	bool bValid = false;
	EBattleVolatileActionOutcome Outcome = EBattleVolatileActionOutcome::Invalid;
	TOptional<int32> RemainingTurns;
	bool bRemoveVolatile = false;
	bool bDrawConsumed = false;
	FBattleRandomDraw Draw;
};

/** Exact inputs for one Protect attempt. */
struct POKEMONSOLARUS_API FBattleProtectAttemptFacts
{
	bool bHasQueuedAction = false;
	bool bConsecutiveEligibleUse = false;
	int32 ChainCounter = 0;
};

/** Exact result of one Protect attempt and its next consecutive-use state. */
struct POKEMONSOLARUS_API FBattleProtectAttemptResult
{
	bool bValid = false;
	bool bSucceeded = false;
	int32 NextChainCounter = 0;
	bool bDrawConsumed = false;
	FBattleRandomDraw Draw;
};

/** Inputs for Leech Seed's source-slot-sensitive residual. */
struct POKEMONSOLARUS_API FBattleLeechSeedResidualFacts
{
	int32 TargetBaseMaximumHP = 0;
	int32 TargetCurrentHP = 0;
	bool bSourceSlotHasLivingRecipient = false;
	int32 RecipientMissingHP = 0;
};

/** Exact target damage and source-slot recipient healing for Leech Seed. */
struct POKEMONSOLARUS_API FBattleLeechSeedResidualResult
{
	bool bValid = false;
	bool bApplies = false;
	int32 RequestedDamage = 0;
	int32 ActualDamage = 0;
	int32 Heal = 0;
};

/** Inputs for one source-sensitive partial-trapping residual. */
struct POKEMONSOLARUS_API FBattlePartialTrapResidualFacts
{
	int32 TargetBaseMaximumHP = 0;
	int32 TargetCurrentHP = 0;
	bool bBindingSourceActiveAndLiving = false;
};

/** Exact residual result for a partial trap after its duration tick. */
struct POKEMONSOLARUS_API FBattlePartialTrapResidualResult
{
	bool bValid = false;
	bool bEndsEarly = false;
	bool bAppliesDamage = false;
	int32 RequestedDamage = 0;
	int32 ActualDamage = 0;
};

/** Selection and before-action move restrictions owned by Taunt, Encore, and Disable. */
struct POKEMONSOLARUS_API FBattleVolatileMoveGateFacts
{
	FMoveId SelectedMoveId;
	EBattleMoveCategory SelectedMoveCategory = EBattleMoveCategory::Invalid;
	bool bSelectedMoveIsStruggle = false;
	bool bNoUsableOrdinaryMove = false;
	bool bTauntActive = false;
	TOptional<FMoveId> EncoreMoveId;
	bool bEncoreMoveStillValid = false;
	int32 EncoreMoveCurrentPP = 0;
	TOptional<FMoveId> DisabledMoveId;
	bool bDisabledMoveStillValid = false;
	int32 DisabledMoveCurrentPP = 0;
};

/** First restriction that rejects a selected move. */
enum class EBattleVolatileMoveGateOutcome : uint8
{
	Allowed = 0,
	Taunted = 1,
	EncoreLocked = 2,
	Disabled = 3,
	Invalid = 255
};

/** Move-gate result plus deterministic invalid/zero-PP expiry facts. */
struct POKEMONSOLARUS_API FBattleVolatileMoveGateResult
{
	bool bValid = false;
	EBattleVolatileMoveGateOutcome Outcome = EBattleVolatileMoveGateOutcome::Invalid;
	bool bEndEncore = false;
	bool bEndDisable = false;
};

/** Exact Substitute creation cost and eligibility. */
struct POKEMONSOLARUS_API FBattleSubstituteCreationResult
{
	bool bValid = false;
	bool bCanCreate = false;
	int32 HPCost = 0;
	int32 SubstituteHP = 0;
};

/** Inputs for routing one ordinary opposing move-damage amount through Substitute. */
struct POKEMONSOLARUS_API FBattleSubstituteDamageFacts
{
	int32 SubstituteHP = 0;
	int32 OwnerCurrentHP = 0;
	int32 IncomingDamage = 0;
	bool bBypassesSubstitute = false;
};

/** Damage routing result. Excess ordinary damage never spills into the owner. */
struct POKEMONSOLARUS_API FBattleSubstituteDamageResult
{
	bool bValid = false;
	int32 DamageToSubstitute = 0;
	int32 DamageToOwner = 0;
	int32 RemainingSubstituteHP = 0;
	int32 ActualDamageForDrainOrRecoil = 0;
	bool bBrokeSubstitute = false;
};

/** First- and second-turn charge resolution without battle-state mutation. */
enum class EBattleChargeActionOutcome : uint8
{
	BeginCharge = 0,
	ExecuteChargedMove = 1,
	CancelCharge = 2,
	Invalid = 255
};

/** Current charge lock and the move/target selected for a possible first turn. */
struct POKEMONSOLARUS_API FBattleChargeActionFacts
{
	bool bChargeActive = false;
	FMoveId SelectedMoveId;
	FBattlerId SelectedTargetBattlerId;
	FMoveId LockedMoveId;
	FBattlerId LockedTargetBattlerId;
	bool bExplicitlyCancelled = false;
};

/** Charge transition and exact move/target lock to use. */
struct POKEMONSOLARUS_API FBattleChargeActionResult
{
	bool bValid = false;
	EBattleChargeActionOutcome Outcome = EBattleChargeActionOutcome::Invalid;
	FMoveId MoveId;
	FBattlerId TargetBattlerId;
	bool bPayPP = false;
	bool bAddCharge = false;
	bool bRemoveCharge = false;
};

/** Fly-style reachability and power change represented entirely by move flags. */
struct POKEMONSOLARUS_API FBattleFlyReachabilityFacts
{
	bool bTargetFlySemiInvulnerable = false;
	bool bMoveReachesFlyTarget = false;
	bool bMoveDoublesPowerAgainstFlyTarget = false;
};

/** Hit reachability and a small exact base-power multiplier. */
struct POKEMONSOLARUS_API FBattleFlyReachabilityResult
{
	bool bValid = false;
	bool bReachable = false;
	int32 PowerMultiplierNumerator = 1;
	int32 PowerMultiplierDenominator = 1;
};

/** Value-only inputs used to build canonical C07A registrations. */
struct POKEMONSOLARUS_API FBattleVolatileTriggerRegistrationFacts
{
	FConditionId VolatileId;
	FDefinitionId PayloadId;
	FBattleTriggerSubject Owner;
	FBattleTriggerSubject Source;
	TArray<FBattleTriggerSubject> Targets;
	TOptional<int32> RemainingTurns;
	int32 Layers = 1;
	bool bSuppressed = false;
};

/** Pure C07C rules plus canonical C07A trigger registrations. */
class POKEMONSOLARUS_API FBattleVolatileRules
{
public:
	[[nodiscard]] static FConditionId GetConfusionId();
	[[nodiscard]] static FConditionId GetFlinchId();
	[[nodiscard]] static FConditionId GetProtectId();
	[[nodiscard]] static FConditionId GetLeechSeedId();
	[[nodiscard]] static FConditionId GetPartialTrapId();
	[[nodiscard]] static FConditionId GetTrapId();
	[[nodiscard]] static FConditionId GetTauntId();
	[[nodiscard]] static FConditionId GetEncoreId();
	[[nodiscard]] static FConditionId GetDisableId();
	[[nodiscard]] static FConditionId GetSubstituteId();
	[[nodiscard]] static FConditionId GetChargingId();
	[[nodiscard]] static FConditionId GetRechargeId();
	[[nodiscard]] static FConditionId GetFlySemiInvulnerableId();
	[[nodiscard]] static TArray<FConditionId> GetCanonicalIds();

	[[nodiscard]] static EBattleVolatileKind GetKind(const FConditionId& VolatileId);
	[[nodiscard]] static bool IsCanonical(const FConditionId& VolatileId);

	[[nodiscard]] static bool TryEvaluateApplication(
		const FBattleVolatileApplicationFacts& Facts,
		FBattleVolatileApplicationResult& OutResult);

	[[nodiscard]] static bool TryRollConfusionDuration(
		const FBattleRandomContext& BaseContext,
		IBattleRandom& Random,
		FBattleVolatileDurationResult& OutResult);
	[[nodiscard]] static bool TryRollPartialTrapDuration(
		const FBattleRandomContext& BaseContext,
		IBattleRandom& Random,
		FBattleVolatileDurationResult& OutResult);

	[[nodiscard]] static bool TryResolveConfusionBeforeAction(
		int32 RemainingTurns,
		const FBattleRandomContext& BaseContext,
		IBattleRandom& Random,
		FBattleVolatileActionResult& OutResult);
	[[nodiscard]] static bool TryResolveSimpleBeforeAction(
		const FConditionId& VolatileId,
		FBattleVolatileActionResult& OutResult);

	[[nodiscard]] static bool TryResolveProtectAttempt(
		const FBattleProtectAttemptFacts& Facts,
		const FBattleRandomContext& BaseContext,
		IBattleRandom& Random,
		FBattleProtectAttemptResult& OutResult);
	[[nodiscard]] static bool ShouldProtectBlockEffect(
		bool bProtectActive,
		bool bMoveBlockedByProtect,
		bool bMoveBypassesProtect);
	[[nodiscard]] static constexpr int32 GetClearedProtectChainCounter() { return 0; }

	[[nodiscard]] static bool TryResolveLeechSeedResidual(
		const FBattleLeechSeedResidualFacts& Facts,
		FBattleLeechSeedResidualResult& OutResult);
	[[nodiscard]] static bool TryResolvePartialTrapResidual(
		const FBattlePartialTrapResidualFacts& Facts,
		FBattlePartialTrapResidualResult& OutResult);
	[[nodiscard]] static bool ShouldBlockVoluntarySwitch(
		const FConditionId& VolatileId,
		EPokemonType PrimaryType,
		EPokemonType SecondaryType,
		bool bSourceActiveAndLiving,
		bool bEligibilityRemains);

	[[nodiscard]] static int32 GetTauntDuration(bool bTargetAlreadyActed);
	[[nodiscard]] static constexpr int32 GetEncoreDuration() { return 3; }
	[[nodiscard]] static constexpr int32 GetDisableDuration() { return 5; }
	[[nodiscard]] static bool TryResolveMoveGate(
		const FBattleVolatileMoveGateFacts& Facts,
		FBattleVolatileMoveGateResult& OutResult);

	[[nodiscard]] static bool TryResolveSubstituteCreation(
		int32 BaseMaximumHP,
		int32 CurrentHP,
		FBattleSubstituteCreationResult& OutResult);
	[[nodiscard]] static bool TryResolveSubstituteDamage(
		const FBattleSubstituteDamageFacts& Facts,
		FBattleSubstituteDamageResult& OutResult);
	[[nodiscard]] static bool ShouldSubstituteBlockEffect(
		bool bSubstituteActive,
		bool bEffectFromOpponent,
		bool bBypassesSubstitute);

	[[nodiscard]] static bool TryResolveChargeAction(
		const FBattleChargeActionFacts& Facts,
		FBattleChargeActionResult& OutResult);
	[[nodiscard]] static bool TryResolveFlyReachability(
		const FBattleFlyReachabilityFacts& Facts,
		FBattleFlyReachabilityResult& OutResult);

	[[nodiscard]] static constexpr int32 GetConfusionSelfHitBasePower() { return 40; }
	[[nodiscard]] static constexpr int32 GetProtectInitialChainCounter() { return 3; }
	[[nodiscard]] static constexpr int32 GetProtectMaximumChainCounter() { return 729; }

	[[nodiscard]] static bool TryGetTriggerEffectId(
		const FConditionId& VolatileId,
		EBattleTriggerPhase Phase,
		FBattleTriggerEffectId& OutEffectId);
	[[nodiscard]] static bool TryBuildTriggerRegistrationSpecs(
		const FBattleVolatileTriggerRegistrationFacts& Facts,
		TArray<FBattleTriggerRegistrationSpec>& OutSpecs);
	[[nodiscard]] static bool TryRegisterTriggers(
		FBattleTriggerFramework& Framework,
		const FBattleVolatileTriggerRegistrationFacts& Facts,
		EBattleTriggerError& OutError);
	[[nodiscard]] static bool TryCleanupTriggers(
		FBattleTriggerFramework& Framework,
		const FConditionId& VolatileId,
		const FBattleTriggerSubject& Owner,
		EBattleTriggerCleanupReason Reason,
		const FBattleTriggerOperationContext& Context,
		EBattleTriggerError& OutError);

	[[nodiscard]] static FDefinitionId GetConfusionDurationPurpose();
	[[nodiscard]] static FDefinitionId GetConfusionActionGatePurpose();
	[[nodiscard]] static FDefinitionId GetConfusionSelfHitDamagePurpose();
	[[nodiscard]] static FDefinitionId GetProtectConsecutiveUsePurpose();
	[[nodiscard]] static FDefinitionId GetPartialTrapDurationPurpose();
};
