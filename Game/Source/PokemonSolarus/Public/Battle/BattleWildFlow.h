#pragma once

#include "CoreMinimal.h"
#include "Battle/BattleRandom.h"
#include "Battle/BattleSetup.h"

/** Immutable inputs for one player Run calculation. */
struct POKEMONSOLARUS_API FBattleRunCalculationInput
{
	int32 PlayerPermanentSpeed = 0;
	int32 WildPermanentSpeed = 0;
	uint32 EscapeAttemptCount = 0;
	FBattleRandomContext RandomContext;
};

/** Exact threshold, optional draw, and outcome from one legal Run attempt. */
struct POKEMONSOLARUS_API FBattleRunCalculationResult
{
	int64 EscapeThreshold = 0;
	bool bSucceeded = false;
	TOptional<FBattleRandomDraw> RandomDraw;
};

/** Pure modern Run rules. The engine supplies permanent, unmodified Speed. */
class POKEMONSOLARUS_API FBattleRunRules
{
public:
	/** Returns whether both permanent Speed values meet the minimum legal value. */
	[[nodiscard]] static bool IsSpeedPairLegal(int32 PlayerPermanentSpeed, int32 WildPermanentSpeed);

	/** Resolves the exact Run formula and consumes RNG only when the threshold is at most 255. */
	[[nodiscard]] static bool TryResolve(
		const FBattleRunCalculationInput& Input,
		IBattleRandom& Random,
		FBattleRunCalculationResult& OutResult);

	/** Stable semantic purpose for the optional U[0,255] Run draw. */
	[[nodiscard]] static FDefinitionId GetRandomCheckPurpose();
};

/** One explicit typed WildFlee policy. An invalid species identity means encounter-wide. */
struct POKEMONSOLARUS_API FBattleWildFleePolicySpec
{
	FSpeciesFormId SpeciesFormId;
	FDefinitionId TriggerId;
	FDefinitionId EligibilityId;
	EBattleWildFleeMode ProbabilityMode = EBattleWildFleeMode::Disabled;
	uint32 Numerator = 0;
	uint32 Denominator = 0;
};

/** Inputs for one legal configured WildFlee attempt. */
struct POKEMONSOLARUS_API FBattleWildFleeCalculationInput
{
	FBattleWildFleePolicySpec Policy;
	FBattleRandomContext RandomContext;
};

/** Optional probability draw and outcome from one configured WildFlee attempt. */
struct POKEMONSOLARUS_API FBattleWildFleeCalculationResult
{
	bool bSucceeded = false;
	TOptional<FBattleRandomDraw> RandomDraw;
};

/** Pure explicit WildFlee policy rules. Disabled is represented by no policy. */
class POKEMONSOLARUS_API FBattleWildFleeRules
{
public:
	/** Validates typed identities plus Never, Always, or a proper Chance fraction. */
	[[nodiscard]] static bool IsPolicyValid(const FBattleWildFleePolicySpec& Policy);

	/** Resolves Never, Always, or Chance without drawing for the deterministic modes. */
	[[nodiscard]] static bool TryResolve(
		const FBattleWildFleeCalculationInput& Input,
		IBattleRandom& Random,
		FBattleWildFleeCalculationResult& OutResult);

	/** Canonical action-selection trigger used by encounter-wide C09B setup policy. */
	[[nodiscard]] static FDefinitionId GetActionSelectionTriggerId();
	/** Canonical eligibility for an active, living opponent in a wild encounter. */
	[[nodiscard]] static FDefinitionId GetActiveLivingWildEligibilityId();
	/** Stable semantic purpose for Chance(numerator, denominator). */
	[[nodiscard]] static FDefinitionId GetRandomCheckPurpose();
};
