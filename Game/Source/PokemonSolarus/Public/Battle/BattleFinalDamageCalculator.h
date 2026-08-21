#pragma once

#include "CoreMinimal.h"
#include "Battle/BattleMoveCategory.h"
#include "Battle/BattleRandom.h"
#include "Battle/BattleStatStages.h"
#include "Battle/BattleStats.h"
#include "Battle/BattleTypeChart.h"

/** Whether final damage reached a non-immune hit or a typed no-effect result. */
enum class EBattleDamageOutcome : uint8
{
	Damage = 0,
	NoEffect = 1,
	Invalid = 255
};

/** Typed source of a successful zero-damage no-effect result. */
enum class EBattleDamageNoEffectReason : uint8
{
	None = 0,
	TypeImmunity = 1,
	RuleHook = 2
};

/** Typed reason final-damage calculation was rejected. */
enum class EBattleDamageCalculationError : uint8
{
	None = 0,
	InvalidInput = 1,
	InvalidModifier = 2,
	InvalidTypeEffectiveness = 3,
	InvalidRandomContext = 4,
	RandomFailure = 5,
	BaseCalculationFailed = 6,
	ArithmeticOverflow = 7
};

/** Stable typed name for every integer recorded in a final-damage trace. */
enum class EBattleDamageTraceStep : uint8
{
	NoEffect = 0,
	InputPower = 1,
	PowerModifierChain = 2,
	EffectivePower = 3,
	OffensiveStageInput = 4,
	OffensiveStageUsed = 5,
	DefensiveStageInput = 6,
	DefensiveStageUsed = 7,
	StagedOffensiveStat = 8,
	StagedDefensiveStat = 9,
	DirectDefensiveStat = 10,
	OffensiveModifierChain = 11,
	DefensiveModifierChain = 12,
	EffectiveOffensiveStat = 13,
	EffectiveDefensiveStat = 14,
	LevelTerm = 15,
	PowerTerm = 16,
	AttackTerm = 17,
	Quotient = 18,
	BaseDamage = 19,
	SpreadDamage = 20,
	WeatherDamage = 21,
	CriticalDamage = 22,
	RandomRoll = 23,
	RandomFactor = 24,
	RandomDamage = 25,
	StabDamage = 26,
	TypeEffectivenessNumerator = 27,
	TypeEffectivenessDenominator = 28,
	TypeDamage = 29,
	BurnDamage = 30,
	FinalModifierIgnored = 31,
	FinalModifierChain = 32,
	FinalModifierClamped = 33,
	FinalDamage = 34
};

/** One named Q12 modifier supplied by a field, Ability, item, or later rule hook. */
struct POKEMONSOLARUS_API FBattleDamageModifier
{
	FDefinitionId RuleId;
	int32 ModifierQ12 = 4096;
	bool bIgnoredByCritical = false;
};

/** One ordered integer or typed hook result in the exact damage calculation. */
struct POKEMONSOLARUS_API FBattleDamageTraceEntry
{
	EBattleDamageTraceStep Step = EBattleDamageTraceStep::InputPower;
	int64 Value = 0;
	FDefinitionId RuleId;
};

/** Complete ordered diagnostic record for one successful damage or no-effect result. */
struct POKEMONSOLARUS_API FDamageTrace
{
	TArray<FBattleDamageTraceEntry> Entries;
};

/** Complete pure input for one reached final-damage stage. */
struct POKEMONSOLARUS_API FBattleFinalDamageInput
{
	int32 AttackerLevel = 0;
	FPokemonBattleStats AttackerStats;
	FPokemonBattleStats DefenderStats;
	FBattleStatStages AttackerStages;
	FBattleStatStages DefenderStages;
	EBattleMoveCategory MoveCategory = EBattleMoveCategory::Invalid;
	int32 MovePower = 0;
	bool bBypassTypeImmunity = false;
	bool bSpreadAcrossMultipleTargets = false;
	bool bCritical = false;
	bool bAttackerBurned = false;
	bool bBypassBurnPenalty = false;
	int32 WeatherModifierQ12 = 4096;
	int32 StabModifierQ12 = 4096;
	FBattleTypeEffectiveness TypeEffectiveness{1, 1};
	FDefinitionId BlockingRuleId;
	TArray<FBattleDamageModifier> PowerModifiers;
	TArray<FBattleDamageModifier> OffensiveStatModifiers;
	TArray<FBattleDamageModifier> DirectDefensiveStatModifiers;
	TArray<FBattleDamageModifier> DefensiveStatModifiers;
	TArray<FBattleDamageModifier> FinalModifiers;
	FBattleRandomContext RandomContext;
};

/** Complete typed damage/no-effect output, random draw, and diagnostic trace. */
struct POKEMONSOLARUS_API FBattleFinalDamageResult
{
	EBattleDamageOutcome Outcome = EBattleDamageOutcome::Invalid;
	EBattleDamageNoEffectReason NoEffectReason = EBattleDamageNoEffectReason::None;
	FDefinitionId NoEffectRuleId;
	int32 Damage = 0;
	bool bRandomDrawConsumed = false;
	FBattleRandomDraw RandomDraw;
	FDamageTrace Trace;
};

/** Pure checked implementation of the accepted staged modern final-damage sequence. */
class POKEMONSOLARUS_API FBattleFinalDamageCalculator
{
public:
	static constexpr int32 Q12Neutral = 4096;
	static constexpr int32 MinimumFinalModifierQ12 = 41;
	static constexpr int32 MaximumFinalModifierQ12 = 131072;

	/**
	 * Classifies only the supplied type or named rule-hook immunity facts before accuracy.
	 * Later damage fields must already come from validated action/content contracts.
	 */
	[[nodiscard]] static bool TryResolvePreAccuracyNoEffect(
		const FBattleFinalDamageInput& Input,
		bool& bOutNoEffect,
		FBattleFinalDamageResult& OutResult,
		EBattleDamageCalculationError& OutError);

	/**
	 * Calculates one final damage result from immutable values and ordered named hooks.
	 * The existing FBattleDamageCalculator remains the authoritative base stage.
	 */
	[[nodiscard]] static bool TryCalculateFinalDamage(
		const FBattleFinalDamageInput& Input,
		IBattleRandom& Random,
		FBattleFinalDamageResult& OutResult,
		EBattleDamageCalculationError& OutError);
};
