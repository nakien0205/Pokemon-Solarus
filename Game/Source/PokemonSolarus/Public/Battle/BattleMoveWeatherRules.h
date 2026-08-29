#pragma once

#include "CoreMinimal.h"
#include "Battle/BattleDefinitions.h"
#include "Battle/BattleFieldSideConditions.h"

/** Typed reason an authored R4B weather-move trait is incompatible. */
enum class EBattleMoveWeatherRuleValidationError : uint8
{
	None = 0,
	ChargeSkipRequiresDamagingMove = 1,
	ChargeSkipRequiresPrimaryChargeBeforeDamage = 2,
	PowerModifierRequiresDamagingMove = 3,
	WeatherAccuracyRequiresDamagingMove = 4,
	WeatherAccuracyRequiresBattlerTarget = 5,
	WeatherAccuracyRequiresSeventyAccuracy = 6,
	WeatherAccuracyRequiresOrdinaryAccuracy = 7,
	WeatherAccuracyDisallowsPerHitAccuracy = 8
};

/** Pure result of resolving whether the authored Charge descriptor is skipped. */
struct POKEMONSOLARUS_API FBattleMoveWeatherChargeSkipResult
{
	bool bShouldSkipCharge = false;
};

/** Pure authored accuracy values after the active weather rule is applied. */
struct POKEMONSOLARUS_API FBattleMoveWeatherAccuracyResult
{
	bool bAlwaysHits = false;
	int32 BaseAccuracy = 0;
};

/** Optional named power modifier appended by the actual-damage owner. */
struct POKEMONSOLARUS_API FBattleMoveWeatherPowerModifierResult
{
	bool bApplies = false;
	FDefinitionId RuleId;
	int32 ModifierQ12 = 4096;
};

/** Stateless authored R4B move validation and weather resolution. */
class POKEMONSOLARUS_API FBattleMoveWeatherRules
{
public:
	/** Validates only the compatibility contract owned by the R4B move traits. */
	[[nodiscard]] static bool TryValidateMoveDefinition(
		const FBattleMoveDefinition& Move,
		EBattleMoveWeatherRuleValidationError& OutError);

	/** Resolves Sun's first-turn Charge skip without changing charge state. */
	[[nodiscard]] static bool TryResolveChargeSkip(
		const FBattleMoveDefinition& Move,
		EBattleFieldSideConditionKind WeatherKind,
		FBattleMoveWeatherChargeSkipResult& OutResult);

	/** Resolves Rain's literal always-hit route and Sun's numeric 50 accuracy. */
	[[nodiscard]] static bool TryResolveAccuracy(
		const FBattleMoveDefinition& Move,
		EBattleFieldSideConditionKind WeatherKind,
		FBattleMoveWeatherAccuracyResult& OutResult);

	/** Resolves the named half-power modifier for Rain, Sandstorm, and Snow. */
	[[nodiscard]] static bool TryResolvePowerModifier(
		const FBattleMoveDefinition& Move,
		EBattleFieldSideConditionKind WeatherKind,
		FBattleMoveWeatherPowerModifierResult& OutResult);
};
