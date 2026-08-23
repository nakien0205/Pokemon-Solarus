#pragma once

#include "CoreMinimal.h"
#include "Battle/BattleAbilityItemContracts.h"
#include "Battle/BattleMoveCategory.h"
#include "Battle/BattleTypeChart.h"

/** The nine canonical held-item behaviors owned by C08C. */
enum class EBattleHeldItemRuleKind : uint8
{
	None = 0,
	Leftovers = 1,
	SitrusBerry = 2,
	LumBerry = 3,
	FocusSash = 4,
	LifeOrb = 5,
	ChoiceBand = 6,
	HeavyDutyBoots = 7,
	AirBalloon = 8,
	QuickClaw = 9,
	Invalid = 255
};

/** Runtime subjects used to register every phase-specific hook for one held item. */
struct POKEMONSOLARUS_API FBattleItemRegistrationFacts
{
	FItemId ItemId;
	FBattleTriggerSubject Owner;
	FBattleTriggerSubject Source;
	TArray<FBattleTriggerSubject> Targets;
	bool bSuppressed = false;
};

/** HP and suppression facts shared by Leftovers and Sitrus Berry. */
struct POKEMONSOLARUS_API FBattleItemRecoveryFacts
{
	FItemId ItemId;
	int32 CurrentHP = 0;
	int32 BaseMaximumHP = 0;
	bool bHealingPermitted = true;
	bool bSuppressed = false;
};

/** Typed result of one held-item recovery check. */
struct POKEMONSOLARUS_API FBattleItemRecoveryResult
{
	bool bValid = false;
	bool bApplies = false;
	bool bConsumesItem = false;
	int32 HealAmount = 0;
	EBattleAbilityItemActivationOutcome Outcome =
		EBattleAbilityItemActivationOutcome::Invalid;
};

/** Current curable conditions considered by Lum Berry's immediate-update hook. */
struct POKEMONSOLARUS_API FBattleLumBerryFacts
{
	FItemId ItemId;
	bool bHolderAbleToBattle = false;
	bool bHasMajorStatus = false;
	bool bHasConfusion = false;
	bool bSuppressed = false;
};

/** Atomic Lum Berry cure decision; consumption precedes every requested cure. */
struct POKEMONSOLARUS_API FBattleLumBerryResult
{
	bool bValid = false;
	bool bApplies = false;
	bool bConsumesItem = false;
	bool bCuresMajorStatus = false;
	bool bCuresConfusion = false;
	EBattleAbilityItemActivationOutcome Outcome =
		EBattleAbilityItemActivationOutcome::Invalid;
};

/** Per-hit facts for Focus Sash's direct move-damage adjustment. */
struct POKEMONSOLARUS_API FBattleFocusSashFacts
{
	FItemId ItemId;
	int32 CurrentHP = 0;
	int32 BaseMaximumHP = 0;
	int32 IncomingDamage = 0;
	bool bDirectMoveDamage = false;
	bool bDamageTargetsSubstitute = false;
	bool bSuppressed = false;
};

/** Typed Focus Sash result; the caller consumes before applying AdjustedDamage. */
struct POKEMONSOLARUS_API FBattleFocusSashResult
{
	bool bValid = false;
	bool bApplies = false;
	bool bConsumesItem = false;
	int32 AdjustedDamage = 0;
	EBattleAbilityItemActivationOutcome Outcome =
		EBattleAbilityItemActivationOutcome::Invalid;
};

/** Per-hit offensive facts shared by Life Orb and Choice Band. */
struct POKEMONSOLARUS_API FBattleItemDamageModifierFacts
{
	FItemId ItemId;
	EBattleMoveCategory MoveCategory = EBattleMoveCategory::Invalid;
	bool bDamagingMove = false;
	bool bSuppressed = false;
};

/** Typed Q12 modifier result for one held-item offensive query. */
struct POKEMONSOLARUS_API FBattleItemDamageModifierResult
{
	bool bValid = false;
	bool bApplies = false;
	int32 ModifierQ12 = 4096;
	EBattleAbilityItemActivationOutcome Outcome =
		EBattleAbilityItemActivationOutcome::Invalid;
};

/** Completed-move facts for Life Orb's indirect recoil hook. */
struct POKEMONSOLARUS_API FBattleLifeOrbRecoilFacts
{
	FItemId ItemId;
	int32 BaseMaximumHP = 0;
	bool bDamagingMove = false;
	bool bMoveAffectedTarget = false;
	bool bSourceAndTargetDiffer = false;
	bool bForcedSwitchSuppressesRecoil = false;
	bool bSuppressed = false;
};

/** Typed Life Orb recoil decision; Ability damage prevention remains caller-owned. */
struct POKEMONSOLARUS_API FBattleLifeOrbRecoilResult
{
	bool bValid = false;
	bool bApplies = false;
	int32 RecoilDamage = 0;
	EBattleAbilityItemActivationOutcome Outcome =
		EBattleAbilityItemActivationOutcome::Invalid;
};

/** Current and selected move facts for Choice Band selection and commit checks. */
struct POKEMONSOLARUS_API FBattleChoiceBandMoveFacts
{
	FItemId ItemId;
	FMoveId SelectedMoveId;
	FMoveId LockedMoveId;
	bool bSelectedMoveIsStruggle = false;
	bool bSuppressed = false;
};

/** Choice Band legality plus the lock a later successful commit should establish. */
struct POKEMONSOLARUS_API FBattleChoiceBandMoveResult
{
	bool bValid = false;
	bool bMoveAllowed = false;
	bool bShouldEstablishLock = false;
	FMoveId LockMoveId;
	EBattleAbilityItemActivationOutcome Outcome =
		EBattleAbilityItemActivationOutcome::Invalid;
};

/** Queue-lock facts checked before Quick Claw consumes its one eligible draw. */
struct POKEMONSOLARUS_API FBattleQuickClawFacts
{
	FItemId ItemId;
	int32 MovePriority = 0;
	bool bSelectedMoveEligible = false;
	bool bSuppressed = false;
};

/** Typed Quick Claw eligibility result; RNG remains owned by the queue locker. */
struct POKEMONSOLARUS_API FBattleQuickClawEligibilityResult
{
	bool bValid = false;
	bool bEligible = false;
	bool bConsumesRandomDraw = false;
	EBattleAbilityItemActivationOutcome Outcome =
		EBattleAbilityItemActivationOutcome::Invalid;
};

/** Resolution of one already-consumed Quick Claw U[0,4] draw. */
struct POKEMONSOLARUS_API FBattleQuickClawDrawResult
{
	bool bValid = false;
	bool bApplies = false;
	int32 FractionalPriorityTenths = 0;
	EBattleAbilityItemActivationOutcome Outcome =
		EBattleAbilityItemActivationOutcome::Invalid;
};

/**
 * Pure C08C held-item rules plus C08A hook definitions and C07A registrations.
 * This layer never mutates battle state, emits events, or performs RNG.
 */
class POKEMONSOLARUS_API FBattleItemRules
{
public:
	[[nodiscard]] static FItemId GetLeftoversId();
	[[nodiscard]] static FItemId GetSitrusBerryId();
	[[nodiscard]] static FItemId GetLumBerryId();
	[[nodiscard]] static FItemId GetFocusSashId();
	[[nodiscard]] static FItemId GetLifeOrbId();
	[[nodiscard]] static FItemId GetChoiceBandId();
	[[nodiscard]] static FItemId GetHeavyDutyBootsId();
	[[nodiscard]] static FItemId GetAirBalloonId();
	[[nodiscard]] static FItemId GetQuickClawId();
	[[nodiscard]] static TArray<FItemId> GetCanonicalIds();

	[[nodiscard]] static EBattleHeldItemRuleKind GetKind(const FItemId& ItemId);
	[[nodiscard]] static bool IsCanonical(const FItemId& ItemId);

	/** Builds every phase-specific C08A hook definition for one canonical held item. */
	[[nodiscard]] static bool TryBuildHookDefinitions(
		const FItemId& ItemId,
		TArray<FBattleAbilityItemHookDefinition>& OutDefinitions);

	/** Looks up one stable hook identity without relying on array position. */
	[[nodiscard]] static bool TryGetHookDefinition(
		const FItemId& ItemId,
		const FDefinitionId& HookId,
		FBattleAbilityItemHookDefinition& OutDefinition);

	/** Returns the held item's hooks for one C07A phase in authored order. */
	[[nodiscard]] static bool TryGetHookDefinitionsForPhase(
		const FItemId& ItemId,
		EBattleTriggerPhase Phase,
		TArray<FBattleAbilityItemHookDefinition>& OutDefinitions);

	/** Finds the matching hook and converts one C07A request into C08A typed work. */
	[[nodiscard]] static bool TryCreateTypedEffectRequest(
		const FBattleTriggerEffectRequest& TriggerRequest,
		FBattleAbilityItemEffectRequest& OutRequest,
		EBattleAbilityItemHookError& OutError);

	/** Builds validated C08A registration facts for every hook. */
	[[nodiscard]] static bool TryBuildHookRegistrationFacts(
		const FBattleItemRegistrationFacts& Facts,
		TArray<FBattleAbilityItemHookRegistrationFacts>& OutHookFacts,
		EBattleAbilityItemHookError& OutError);

	/** Registers every held-item hook atomically through the C08A/C07A contracts. */
	[[nodiscard]] static bool TryRegisterHooks(
		FBattleTriggerFramework& Framework,
		const FBattleItemRegistrationFacts& Facts,
		EBattleAbilityItemHookError& OutError);

	/** Resolves Leftovers or Sitrus Berry recovery without changing HP or item state. */
	[[nodiscard]] static bool TryEvaluateRecovery(
		const FBattleItemRecoveryFacts& Facts,
		FBattleItemRecoveryResult& OutResult);

	/** Resolves Lum Berry's one atomic major-status and/or Confusion cure. */
	[[nodiscard]] static bool TryEvaluateLumBerry(
		const FBattleLumBerryFacts& Facts,
		FBattleLumBerryResult& OutResult);

	/** Adjusts one direct hit for Focus Sash, leaving later hits independent. */
	[[nodiscard]] static bool TryEvaluateFocusSash(
		const FBattleFocusSashFacts& Facts,
		FBattleFocusSashResult& OutResult);

	/** Resolves Life Orb final damage or Choice Band Physical Attack modification. */
	[[nodiscard]] static bool TryEvaluateDamageModifier(
		const FBattleItemDamageModifierFacts& Facts,
		FBattleItemDamageModifierResult& OutResult);

	/** Resolves Life Orb's post-move indirect recoil amount. */
	[[nodiscard]] static bool TryEvaluateLifeOrbRecoil(
		const FBattleLifeOrbRecoilFacts& Facts,
		FBattleLifeOrbRecoilResult& OutResult);

	/** Rechecks Choice Band move legality before selection and again before PP. */
	[[nodiscard]] static bool TryEvaluateChoiceBandMove(
		const FBattleChoiceBandMoveFacts& Facts,
		FBattleChoiceBandMoveResult& OutResult);

	/** Choice lock cleanup is exact: switch, item loss, or suppression. */
	[[nodiscard]] static bool ShouldClearChoiceBandMoveLock(
		bool bSwitchedOut,
		bool bItemLost,
		bool bSuppressed);

	/** Heavy-Duty Boots bypass every approved entry-hazard handler while active. */
	[[nodiscard]] static bool ShouldBypassEntryHazards(
		const FItemId& ItemId,
		bool bSuppressed);

	/** Air Balloon's passive airborne state after item suppression. */
	[[nodiscard]] static bool IsAirBalloonAirborne(
		const FItemId& ItemId,
		bool bSuppressed);

	/** Ground immunity supplied by an active Air Balloon. */
	[[nodiscard]] static bool ShouldAirBalloonPreventMove(
		const FItemId& ItemId,
		EPokemonType MoveType,
		bool bSuppressed);

	/** Any connected damaging hit pops an active Balloon, including a Substitute hit. */
	[[nodiscard]] static bool ShouldPopAirBalloon(
		const FItemId& ItemId,
		bool bDamagingHitConnected,
		bool bSuppressed);

	/** Determines whether queue lock must consume one Quick Claw U[0,4] draw. */
	[[nodiscard]] static bool TryEvaluateQuickClawEligibility(
		const FBattleQuickClawFacts& Facts,
		FBattleQuickClawEligibilityResult& OutResult);

	/** Resolves one eligible Quick Claw raw draw; success is exactly zero. */
	[[nodiscard]] static bool TryResolveQuickClawDraw(
		const FBattleQuickClawFacts& Facts,
		uint32 RawDraw,
		FBattleQuickClawDrawResult& OutResult);

	[[nodiscard]] static FDefinitionId GetQuickClawActivationPurpose();

	[[nodiscard]] static constexpr int32 GetNeutralModifierQ12() { return 4096; }
	[[nodiscard]] static constexpr int32 GetChoiceBandModifierQ12() { return 6144; }
	[[nodiscard]] static constexpr int32 GetLifeOrbModifierQ12() { return 5324; }
	[[nodiscard]] static constexpr int32 GetLeftoversResidualOrder() { return 5; }
	[[nodiscard]] static constexpr int32 GetLeftoversResidualSuborder() { return 4; }
	[[nodiscard]] static constexpr uint32 GetQuickClawRollMaxInclusive() { return 4; }
	[[nodiscard]] static constexpr uint32 GetQuickClawSuccessRawDraw() { return 0; }
	[[nodiscard]] static constexpr int32 GetQuickClawFractionalPriorityTenths() { return 1; }
};
