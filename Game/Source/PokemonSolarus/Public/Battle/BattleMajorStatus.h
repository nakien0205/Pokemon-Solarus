#pragma once

#include "CoreMinimal.h"
#include "Battle/BattleRandom.h"
#include "Battle/BattleTriggerFramework.h"
#include "Battle/BattleTypeChart.h"

/** The six major-status behaviors owned by C07B. */
enum class EBattleMajorStatusKind : uint8
{
	None = 0,
	Burn = 1,
	Paralysis = 2,
	Sleep = 3,
	Freeze = 4,
	Poison = 5,
	Toxic = 6,
	Invalid = 255
};

/** Stable result of checking a canonical major-status application. */
enum class EBattleMajorStatusApplicationOutcome : uint8
{
	CanApply = 0,
	AlreadyHasMajorStatus = 1,
	TypeImmune = 2,
	Prevented = 3,
	Invalid = 255
};

/** First prevention fact that stopped a canonical application. */
enum class EBattleMajorStatusPreventionReason : uint8
{
	None = 0,
	ExistingMajorStatus = 1,
	TypeImmunity = 2,
	Sun = 3,
	Terrain = 4,
	Safeguard = 5,
	Ability = 6,
	Item = 7,
	Invalid = 255
};

/** Explicit neutral-by-default seams for later field, Ability, and item packages. */
struct POKEMONSOLARUS_API FBattleMajorStatusPreventionInputs
{
	bool bSunActive = false;
	bool bTerrainPrevents = false;
	bool bSafeguardPrevents = false;
	bool bAbilityPrevents = false;
	bool bItemPrevents = false;
};

/** Complete immutable facts needed before a canonical application RNG call. */
struct POKEMONSOLARUS_API FBattleMajorStatusApplicationFacts
{
	FConditionId RequestedStatusId;
	FConditionId ExistingMajorStatusId;
	EPokemonType PrimaryType = EPokemonType::Invalid;
	EPokemonType SecondaryType = EPokemonType::Invalid;
	FBattleMajorStatusPreventionInputs Prevention;
};

/** Typed, presentation-free canonical application result. */
struct POKEMONSOLARUS_API FBattleMajorStatusApplicationResult
{
	bool bValid = false;
	EBattleMajorStatusKind Kind = EBattleMajorStatusKind::Invalid;
	EBattleMajorStatusApplicationOutcome Outcome = EBattleMajorStatusApplicationOutcome::Invalid;
	EBattleMajorStatusPreventionReason PreventionReason = EBattleMajorStatusPreventionReason::Invalid;
};

/** Result of the before-action major-status gate. */
enum class EBattleMajorStatusActionOutcome : uint8
{
	Allowed = 0,
	Denied = 1,
	CuredAndAllowed = 2,
	Invalid = 255
};

/** Action-time facts; later move content may opt into the two neutral hooks. */
struct POKEMONSOLARUS_API FBattleMajorStatusActionFacts
{
	FConditionId StatusId;
	TOptional<int32> RemainingSleepTurns;
	bool bMoveUsableWhileAsleep = false;
	bool bMoveThawsUser = false;
};

/** Typed action gate, including any one semantic RNG draw. */
struct POKEMONSOLARUS_API FBattleMajorStatusActionResult
{
	bool bValid = false;
	EBattleMajorStatusActionOutcome Outcome = EBattleMajorStatusActionOutcome::Invalid;
	TOptional<int32> RemainingSleepTurns;
	bool bCureStatus = false;
	bool bDrawConsumed = false;
	FBattleRandomDraw Draw;
};

/** End-turn inputs. ToxicLayerEncoding is stage plus one, in the inclusive range 1..16. */
struct POKEMONSOLARUS_API FBattleMajorStatusResidualFacts
{
	FConditionId StatusId;
	int32 BaseMaximumHP = 0;
	int32 ToxicLayerEncoding = 1;
};

/** Exact residual result and next hidden Toxic layer encoding. */
struct POKEMONSOLARUS_API FBattleMajorStatusResidualResult
{
	bool bValid = false;
	bool bAppliesDamage = false;
	int32 Damage = 0;
	int32 PreviousToxicStage = 0;
	int32 ToxicStage = 0;
	int32 ToxicLayerEncoding = 1;
};

/** One canonical Sleep duration draw made only when Sleep is successfully applied. */
struct POKEMONSOLARUS_API FBattleSleepDurationResult
{
	bool bValid = false;
	int32 Turns = 0;
	FBattleRandomDraw Draw;
};

/** Pure C07B rules plus canonical C07A trigger registrations. */
class POKEMONSOLARUS_API FBattleMajorStatusRules
{
public:
	[[nodiscard]] static FConditionId GetBurnId();
	[[nodiscard]] static FConditionId GetParalysisId();
	[[nodiscard]] static FConditionId GetSleepId();
	[[nodiscard]] static FConditionId GetFreezeId();
	[[nodiscard]] static FConditionId GetPoisonId();
	[[nodiscard]] static FConditionId GetToxicId();
	[[nodiscard]] static TArray<FConditionId> GetCanonicalIds();

	[[nodiscard]] static EBattleMajorStatusKind GetKind(const FConditionId& StatusId);
	[[nodiscard]] static bool IsCanonical(const FConditionId& StatusId);

	[[nodiscard]] static bool TryEvaluateApplication(
		const FBattleMajorStatusApplicationFacts& Facts,
		FBattleMajorStatusApplicationResult& OutResult);

	[[nodiscard]] static bool TryRollSleepDuration(
		const FBattleRandomContext& BaseContext,
		IBattleRandom& Random,
		FBattleSleepDurationResult& OutResult);

	[[nodiscard]] static bool TryResolveBeforeAction(
		const FBattleMajorStatusActionFacts& Facts,
		const FBattleRandomContext& BaseContext,
		IBattleRandom& Random,
		FBattleMajorStatusActionResult& OutResult);

	/** Applies Paralysis after ordinary stage calculation; zero is a valid result. */
	[[nodiscard]] static bool TryApplySpeedModifier(
		const FConditionId& StatusId,
		int32 StageAdjustedSpeed,
		int32& OutEffectiveSpeed);

	/** Supplies the existing final-damage Burn inputs without changing its formula pipeline. */
	[[nodiscard]] static bool ShouldApplyBurnPhysicalPenalty(
		const FConditionId& StatusId,
		EBattleMoveCategory MoveCategory,
		bool bBypassBurnPenalty);

	[[nodiscard]] static bool TryResolveResidual(
		const FBattleMajorStatusResidualFacts& Facts,
		FBattleMajorStatusResidualResult& OutResult);

	/** Toxic stage zero is encoded as one C07A layer and restored on every switch out. */
	[[nodiscard]] static constexpr int32 GetResetToxicLayerEncoding() { return 1; }

	[[nodiscard]] static bool ShouldThawReachedTarget(
		const FConditionId& TargetStatusId,
		EPokemonType MoveType,
		bool bDamagingMove,
		bool bMoveThawsTarget,
		bool bReachedTarget);

	[[nodiscard]] static bool TryBuildTriggerRegistrationSpecs(
		const FConditionId& StatusId,
		const FBattleTriggerSubject& Owner,
		const TOptional<int32>& SleepTurns,
		TArray<FBattleTriggerRegistrationSpec>& OutSpecs);

	[[nodiscard]] static bool TryRegisterTriggers(
		FBattleTriggerFramework& Framework,
		const FConditionId& StatusId,
		const FBattleTriggerSubject& Owner,
		const TOptional<int32>& SleepTurns,
		EBattleTriggerError& OutError);

	[[nodiscard]] static bool TryCleanupTriggers(
		FBattleTriggerFramework& Framework,
		const FConditionId& StatusId,
		const FBattleTriggerSubject& Owner,
		EBattleTriggerCleanupReason Reason,
		const FBattleTriggerOperationContext& Context,
		EBattleTriggerError& OutError);

	[[nodiscard]] static FDefinitionId GetSleepDurationPurpose();
	[[nodiscard]] static FDefinitionId GetParalysisActionGatePurpose();
	[[nodiscard]] static FDefinitionId GetFreezeNaturalThawPurpose();
};
