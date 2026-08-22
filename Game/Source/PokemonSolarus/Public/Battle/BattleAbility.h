#pragma once

#include "CoreMinimal.h"
#include "Battle/BattleAbilityItemContracts.h"
#include "Battle/BattleTypeChart.h"

/** The eight canonical Ability behaviors owned by C08B. */
enum class EBattleAbilityKind : uint8
{
	None = 0,
	Blaze = 1,
	Overgrow = 2,
	Intimidate = 3,
	Levitate = 4,
	Drizzle = 5,
	SpeedBoost = 6,
	MagicGuard = 7,
	MoldBreaker = 8,
	Invalid = 255
};

/** Semantic source of one HP change considered by Magic Guard. */
enum class EBattleHPChangeSourceKind : uint8
{
	Move = 0,
	Condition = 1,
	Field = 2,
	Volatile = 3,
	Ability = 4,
	Item = 5,
	OtherIndirect = 6,
	Cost = 7,
	Invalid = 255
};

/** Runtime subjects used to register every phase-specific hook for one Ability. */
struct POKEMONSOLARUS_API FBattleAbilityRegistrationFacts
{
	FAbilityId AbilityId;
	FBattleTriggerSubject Owner;
	FBattleTriggerSubject Source;
	TArray<FBattleTriggerSubject> Targets;
	bool bSuppressed = false;
};

/** Per-hit facts for Blaze and Overgrow's offensive-stat query. */
struct POKEMONSOLARUS_API FBattleAbilityOffensiveStatFacts
{
	FAbilityId AbilityId;
	EPokemonType MoveType = EPokemonType::Invalid;
	int32 CurrentHP = 0;
	int32 BaseMaximumHP = 0;
	bool bSuppressed = false;
};

/** Typed result of one Blaze or Overgrow offensive-stat query. */
struct POKEMONSOLARUS_API FBattleAbilityOffensiveStatResult
{
	bool bValid = false;
	bool bApplies = false;
	int32 ModifierQ12 = 4096;
	EBattleAbilityItemActivationOutcome Outcome =
		EBattleAbilityItemActivationOutcome::Invalid;
};

/** Immutable target facts for one stable-order Intimidate entry attempt. */
struct POKEMONSOLARUS_API FBattleIntimidateTargetFacts
{
	bool bAdjacentOpponent = false;
	bool bTargetAbleToBattle = false;
	bool bSubstituteActive = false;
	bool bStatStageDropPrevented = false;
	int32 CurrentAttackStage = 0;
	bool bSuppressed = false;
};

/** Typed result for one Intimidate target; the caller owns target iteration order. */
struct POKEMONSOLARUS_API FBattleIntimidateTargetResult
{
	bool bValid = false;
	int32 AttackStageDelta = 0;
	EBattleAbilityItemActivationOutcome Outcome =
		EBattleAbilityItemActivationOutcome::Invalid;
};

/** Typed decision for Drizzle against the current one-weather slot. */
struct POKEMONSOLARUS_API FBattleDrizzleEntryResult
{
	bool bValid = false;
	FConditionId RainId;
	int32 DurationTurns = 0;
	bool bReplacesExistingWeather = false;
	EBattleAbilityItemActivationOutcome Outcome =
		EBattleAbilityItemActivationOutcome::Invalid;
};

/**
 * Pure C08B Ability rules plus C08A hook definitions and C07A registrations.
 * This layer never mutates battle state, emits events, or performs RNG.
 */
class POKEMONSOLARUS_API FBattleAbilityRules
{
public:
	[[nodiscard]] static FAbilityId GetBlazeId();
	[[nodiscard]] static FAbilityId GetOvergrowId();
	[[nodiscard]] static FAbilityId GetIntimidateId();
	[[nodiscard]] static FAbilityId GetLevitateId();
	[[nodiscard]] static FAbilityId GetDrizzleId();
	[[nodiscard]] static FAbilityId GetSpeedBoostId();
	[[nodiscard]] static FAbilityId GetMagicGuardId();
	[[nodiscard]] static FAbilityId GetMoldBreakerId();
	[[nodiscard]] static TArray<FAbilityId> GetCanonicalIds();

	[[nodiscard]] static EBattleAbilityKind GetKind(const FAbilityId& AbilityId);
	[[nodiscard]] static bool IsCanonical(const FAbilityId& AbilityId);

	/** Builds every phase-specific C08A hook definition for one canonical Ability. */
	[[nodiscard]] static bool TryBuildHookDefinitions(
		const FAbilityId& AbilityId,
		TArray<FBattleAbilityItemHookDefinition>& OutDefinitions);

	/** Looks up one stable hook identity without relying on array position. */
	[[nodiscard]] static bool TryGetHookDefinition(
		const FAbilityId& AbilityId,
		const FDefinitionId& HookId,
		FBattleAbilityItemHookDefinition& OutDefinition);

	/** Returns the Ability's hooks for one C07A phase in authored order. */
	[[nodiscard]] static bool TryGetHookDefinitionsForPhase(
		const FAbilityId& AbilityId,
		EBattleTriggerPhase Phase,
		TArray<FBattleAbilityItemHookDefinition>& OutDefinitions);

	/** Finds the matching hook and converts one C07A request into C08A typed work. */
	[[nodiscard]] static bool TryCreateTypedEffectRequest(
		const FBattleTriggerEffectRequest& TriggerRequest,
		FBattleAbilityItemEffectRequest& OutRequest,
		EBattleAbilityItemHookError& OutError);

	/** Builds validated C08A facts for every hook without changing the framework. */
	[[nodiscard]] static bool TryBuildHookRegistrationFacts(
		const FBattleAbilityRegistrationFacts& Facts,
		TArray<FBattleAbilityItemHookRegistrationFacts>& OutHookFacts,
		EBattleAbilityItemHookError& OutError);

	/** Registers every Ability hook atomically through the C08A/C07A contracts. */
	[[nodiscard]] static bool TryRegisterHooks(
		FBattleTriggerFramework& Framework,
		const FBattleAbilityRegistrationFacts& Facts,
		EBattleAbilityItemHookError& OutError);

	/** Rechecks the exact low-HP threshold and move type for every offensive query. */
	[[nodiscard]] static bool TryEvaluateOffensiveStatModifier(
		const FBattleAbilityOffensiveStatFacts& Facts,
		FBattleAbilityOffensiveStatResult& OutResult);

	/** Resolves one already ordered adjacent-opponent Intimidate attempt. */
	[[nodiscard]] static bool TryEvaluateIntimidateTarget(
		const FBattleIntimidateTargetFacts& Facts,
		FBattleIntimidateTargetResult& OutResult);

	/** Levitate's passive airborne state after suppression and move-scoped ignore. */
	[[nodiscard]] static bool IsLevitateAirborne(
		const FAbilityId& AbilityId,
		bool bSuppressed,
		bool bIgnoredForMove = false);

	/** Ground immunity supplied by an active, non-ignored Levitate hook. */
	[[nodiscard]] static bool ShouldLevitatePreventMove(
		const FAbilityId& AbilityId,
		EPokemonType MoveType,
		bool bSuppressed,
		bool bIgnoredForMove);

	/** Evaluates Rain creation/replacement without mutating the field slot. */
	[[nodiscard]] static bool TryEvaluateDrizzleEntry(
		const FAbilityId& AbilityId,
		const FConditionId& ExistingWeatherId,
		bool bSuppressed,
		FBattleDrizzleEntryResult& OutResult);

	/** Speed Boost is eligible only after one completed active turn and below +6. */
	[[nodiscard]] static bool ShouldApplySpeedBoost(
		const FAbilityId& AbilityId,
		uint32 ActiveTurns,
		int32 CurrentSpeedStage,
		bool bSuppressed);

	/** Magic Guard rejects recognized non-Move damage, never direct Move damage or HP costs. */
	[[nodiscard]] static bool ShouldMagicGuardPreventDamage(
		const FAbilityId& AbilityId,
		EBattleHPChangeSourceKind SourceKind,
		bool bSuppressed);

	/** Mold Breaker ignores only a matching defender Ability hook marked breakable. */
	[[nodiscard]] static bool ShouldMoldBreakerIgnoreDefenderHookForMove(
		const FAbilityId& AttackerAbilityId,
		bool bAttackerAbilitySuppressed,
		const FAbilityId& DefenderAbilityId,
		const FBattleAbilityItemHookDefinition& DefenderHook);

	[[nodiscard]] static constexpr int32 GetNeutralModifierQ12() { return 4096; }
	[[nodiscard]] static constexpr int32 GetLowHPBoostModifierQ12() { return 6144; }
	[[nodiscard]] static constexpr int32 GetDrizzleDurationTurns() { return 5; }
	[[nodiscard]] static constexpr int32 GetSpeedBoostResidualOrder() { return 28; }
	[[nodiscard]] static constexpr int32 GetSpeedBoostResidualSuborder() { return 2; }
};
