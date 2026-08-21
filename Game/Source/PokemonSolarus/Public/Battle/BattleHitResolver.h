#pragma once

#include "CoreMinimal.h"
#include "Battle/BattleRandom.h"
#include "Battle/BattleStatStages.h"

/** Typed result of one accuracy check. */
enum class EBattleAccuracyCheckOutcome : uint8
{
	Hit = 0,
	Miss = 1,
	Invalid = 255
};

/** Whether a move uses the ordinary critical table or an explicit fixed rule. */
enum class EBattleCriticalCheckMode : uint8
{
	Standard = 0,
	Always = 1,
	Never = 2,
	Invalid = 255
};

/** Typed result of one critical-hit check. */
enum class EBattleCriticalCheckOutcome : uint8
{
	NotCritical = 0,
	Critical = 1,
	Blocked = 2,
	Invalid = 255
};

/** Typed reason an accuracy or critical check was rejected. */
enum class EBattleHitResolverError : uint8
{
	None = 0,
	InvalidAccuracy = 1,
	InvalidCriticalMode = 2,
	InvalidRandomContext = 3,
	RandomFailure = 4
};

/** Complete pure input for one reached accuracy stage. */
struct POKEMONSOLARUS_API FBattleAccuracyCheckInput
{
	bool bAlwaysHits = false;
	int32 BaseAccuracy = 0;
	FBattleStatStages AttackerStages;
	FBattleStatStages DefenderStages;
	FBattleRandomContext RandomContext;
};

/** Complete result and optional draw for one accuracy stage. */
struct POKEMONSOLARUS_API FBattleAccuracyCheckResult
{
	EBattleAccuracyCheckOutcome Outcome = EBattleAccuracyCheckOutcome::Invalid;
	int32 EffectiveAccuracy = 0;
	bool bDrawConsumed = false;
	FBattleRandomDraw Draw;
};

/** Complete pure input for one reached critical-hit stage. */
struct POKEMONSOLARUS_API FBattleCriticalCheckInput
{
	EBattleCriticalCheckMode Mode = EBattleCriticalCheckMode::Standard;
	int32 BaseStage = 1;
	int32 StageModifier = 0;
	bool bDefenderBlocksCritical = false;
	FBattleRandomContext RandomContext;
};

/** Complete result, stage behavior, and optional draw for one critical stage. */
struct POKEMONSOLARUS_API FBattleCriticalCheckResult
{
	EBattleCriticalCheckOutcome Outcome = EBattleCriticalCheckOutcome::Invalid;
	int32 ResolvedStage = 0;
	bool bCriticalCandidate = false;
	bool bIgnoreNegativeOffensiveStage = false;
	bool bIgnorePositiveDefensiveStage = false;
	bool bIgnoreScreens = false;
	bool bDrawConsumed = false;
	FBattleRandomDraw Draw;
};

/** Pure accuracy and critical-hit stages using the injected battle RNG. */
class POKEMONSOLARUS_API FBattleHitResolver
{
public:
	/**
	 * Resolves literal always-hit or numeric modern accuracy.
	 * Numeric checks always consume U[0,99], including effective accuracy 100 or above.
	 */
	[[nodiscard]] static bool TryResolveAccuracy(
		const FBattleAccuracyCheckInput& Input,
		IBattleRandom& Random,
		FBattleAccuracyCheckResult& OutResult,
		EBattleHitResolverError& OutError);

	/**
	 * Resolves the clamped modern critical stage or an explicit always/never rule.
	 * A successful candidate is passed through the supplied defender-block hook.
	 */
	[[nodiscard]] static bool TryResolveCritical(
		const FBattleCriticalCheckInput& Input,
		IBattleRandom& Random,
		FBattleCriticalCheckResult& OutResult,
		EBattleHitResolverError& OutError);
};
