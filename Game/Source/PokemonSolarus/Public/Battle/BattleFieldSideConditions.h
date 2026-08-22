#pragma once

#include "CoreMinimal.h"
#include "Battle/BattleDefinitions.h"
#include "Battle/BattleTriggerFramework.h"
#include "Battle/BattleTypeChart.h"

/** The complete approved C07D weather, terrain, hazard, screen, room, and side proof set. */
enum class EBattleFieldSideConditionKind : uint8
{
	None = 0,
	Sun = 1,
	Rain = 2,
	Sandstorm = 3,
	Snow = 4,
	ElectricTerrain = 5,
	GrassyTerrain = 6,
	MistyTerrain = 7,
	PsychicTerrain = 8,
	Spikes = 9,
	ToxicSpikes = 10,
	StealthRock = 11,
	StickyWeb = 12,
	Reflect = 13,
	LightScreen = 14,
	AuroraVeil = 15,
	TrickRoom = 16,
	MagicRoom = 17,
	WonderRoom = 18,
	Tailwind = 19,
	Safeguard = 20,
	Mist = 21,
	Invalid = 255
};

/** Stable semantic result of trying to establish one C07D condition. */
enum class EBattleFieldSideApplicationOutcome : uint8
{
	Create = 0,
	ReplaceExclusive = 1,
	AddLayer = 2,
	ToggleOff = 3,
	AlreadyActive = 4,
	LayerCapReached = 5,
	ActivationRequirementFailed = 6,
	Invalid = 255
};

/** Complete current facts used by the generic C07D application policy. */
struct POKEMONSOLARUS_API FBattleFieldSideApplicationFacts
{
	FConditionId RequestedConditionId;
	TOptional<FConditionId> ExistingExclusiveConditionId;
	bool bRequestedAlreadyActive = false;
	int32 ExistingLayers = 0;
	bool bSnowActive = false;
	bool bDurationExtensionActive = false;
};

/** Mutation instructions returned without changing field or side state. */
struct POKEMONSOLARUS_API FBattleFieldSideApplicationResult
{
	bool bValid = false;
	EBattleFieldSideConditionKind Kind = EBattleFieldSideConditionKind::Invalid;
	EBattleFieldSideApplicationOutcome Outcome = EBattleFieldSideApplicationOutcome::Invalid;
	TOptional<int32> DurationTurns;
	int32 Layers = 0;
	int32 MaximumLayers = 0;
	bool bRemoveExistingExclusive = false;
};

/** Grounded inputs with neutral-by-default seams for the later Ability and item package. */
struct POKEMONSOLARUS_API FBattleGroundedFacts
{
	EPokemonType PrimaryType = EPokemonType::Invalid;
	EPokemonType SecondaryType = EPokemonType::Invalid;
	bool bAbilityMakesAirborne = false;
	bool bAbilitySuppressed = false;
	bool bItemMakesAirborne = false;
	bool bItemSuppressed = false;
	bool bAirborneSemiInvulnerable = false;
};

/** One approved field residual mutation, or a valid no-effect result. */
enum class EBattleFieldResidualEffectKind : uint8
{
	None = 0,
	Damage = 1,
	Heal = 2,
	Invalid = 255
};

/** Immutable facts for Sandstorm damage or Grassy Terrain healing. */
struct POKEMONSOLARUS_API FBattleFieldResidualFacts
{
	FConditionId ConditionId;
	int32 BaseMaximumHP = 0;
	int32 CurrentHP = 0;
	EPokemonType PrimaryType = EPokemonType::Invalid;
	EPokemonType SecondaryType = EPokemonType::Invalid;
	bool bGrounded = false;
	bool bIndirectDamagePrevented = false;
};

/** Exact actual amount after eligibility, minimum-one, and healing-cap rules. */
struct POKEMONSOLARUS_API FBattleFieldResidualResult
{
	bool bValid = false;
	EBattleFieldResidualEffectKind EffectKind = EBattleFieldResidualEffectKind::Invalid;
	int32 Amount = 0;
};

/** Typed switch-in mutation produced by one hazard in first-creation order. */
enum class EBattleHazardSwitchInEffectKind : uint8
{
	None = 0,
	Damage = 1,
	ApplyMajorStatus = 2,
	RemoveHazard = 3,
	ModifyStatStage = 4,
	Invalid = 255
};

/** Complete hazard facts; later Ability/item hooks resolve into the neutral booleans. */
struct POKEMONSOLARUS_API FBattleHazardSwitchInFacts
{
	FConditionId HazardId;
	int32 Layers = 0;
	int32 BaseMaximumHP = 0;
	int32 CurrentHP = 0;
	EPokemonType PrimaryType = EPokemonType::Invalid;
	EPokemonType SecondaryType = EPokemonType::Invalid;
	bool bGrounded = false;
	bool bBypassesEntryHazards = false;
	bool bIndirectDamagePrevented = false;
	bool bMajorStatusPrevented = false;
	bool bStatStageDropPrevented = false;
	FBattleTypeEffectiveness RockEffectiveness{1, 1};
};

/** Exact single-hazard result; the caller applies and settles it before visiting the next hazard. */
struct POKEMONSOLARUS_API FBattleHazardSwitchInResult
{
	bool bValid = false;
	EBattleHazardSwitchInEffectKind EffectKind = EBattleHazardSwitchInEffectKind::Invalid;
	int32 Damage = 0;
	FConditionId MajorStatusId;
	EBattleStat Stat = static_cast<EBattleStat>(255);
	int32 StatStageDelta = 0;
	bool bRemoveHazard = false;
};

/** Value-only inputs used to build canonical C07A registrations for C07D content. */
struct POKEMONSOLARUS_API FBattleFieldSideTriggerRegistrationFacts
{
	FConditionId ConditionId;
	FDefinitionId PayloadId;
	FBattleTriggerSubject Owner;
	FBattleTriggerSubject Source;
	TArray<FBattleTriggerSubject> Targets;
	TOptional<int32> RemainingTurns;
	int32 Layers = 1;
	bool bSuppressed = false;
};

/** Pure C07D rules plus canonical C07A registration and cleanup identities. */
class POKEMONSOLARUS_API FBattleFieldSideConditionRules
{
public:
	[[nodiscard]] static FConditionId GetSunId();
	[[nodiscard]] static FConditionId GetRainId();
	[[nodiscard]] static FConditionId GetSandstormId();
	[[nodiscard]] static FConditionId GetSnowId();
	[[nodiscard]] static FConditionId GetElectricTerrainId();
	[[nodiscard]] static FConditionId GetGrassyTerrainId();
	[[nodiscard]] static FConditionId GetMistyTerrainId();
	[[nodiscard]] static FConditionId GetPsychicTerrainId();
	[[nodiscard]] static FConditionId GetSpikesId();
	[[nodiscard]] static FConditionId GetToxicSpikesId();
	[[nodiscard]] static FConditionId GetStealthRockId();
	[[nodiscard]] static FConditionId GetStickyWebId();
	[[nodiscard]] static FConditionId GetReflectId();
	[[nodiscard]] static FConditionId GetLightScreenId();
	[[nodiscard]] static FConditionId GetAuroraVeilId();
	[[nodiscard]] static FConditionId GetTrickRoomId();
	[[nodiscard]] static FConditionId GetMagicRoomId();
	[[nodiscard]] static FConditionId GetWonderRoomId();
	[[nodiscard]] static FConditionId GetTailwindId();
	[[nodiscard]] static FConditionId GetSafeguardId();
	[[nodiscard]] static FConditionId GetMistId();
	[[nodiscard]] static TArray<FConditionId> GetCanonicalIds();

	[[nodiscard]] static EBattleFieldSideConditionKind GetKind(const FConditionId& ConditionId);
	[[nodiscard]] static EBattleConditionKind GetConditionFamily(const FConditionId& ConditionId);
	[[nodiscard]] static bool IsCanonical(const FConditionId& ConditionId);
	[[nodiscard]] static bool IsFieldOwned(const FConditionId& ConditionId);
	[[nodiscard]] static bool IsSideOwned(const FConditionId& ConditionId);
	[[nodiscard]] static bool SupportsDurationExtension(const FConditionId& ConditionId);
	[[nodiscard]] static bool TryGetDuration(
		const FConditionId& ConditionId,
		bool bDurationExtensionActive,
		TOptional<int32>& OutDurationTurns);
	[[nodiscard]] static bool TryGetMaximumLayers(
		const FConditionId& ConditionId,
		int32& OutMaximumLayers);
	[[nodiscard]] static bool TryEvaluateApplication(
		const FBattleFieldSideApplicationFacts& Facts,
		FBattleFieldSideApplicationResult& OutResult);

	[[nodiscard]] static bool TryResolveGrounded(
		const FBattleGroundedFacts& Facts,
		bool& bOutGrounded);

	[[nodiscard]] static bool TryGetWeatherDamageModifierQ12(
		const FConditionId& WeatherId,
		EPokemonType MoveType,
		int32& OutModifierQ12);
	[[nodiscard]] static bool TryGetWeatherDirectDefensiveModifierQ12(
		const FConditionId& WeatherId,
		EPokemonType DefenderPrimaryType,
		EPokemonType DefenderSecondaryType,
		EBattleMoveCategory MoveCategory,
		int32& OutModifierQ12);
	[[nodiscard]] static bool ShouldSunPreventFreeze(const FConditionId& WeatherId);

	[[nodiscard]] static bool TryGetTerrainPowerModifierQ12(
		const FConditionId& TerrainId,
		EPokemonType MoveType,
		bool bAttackerGrounded,
		int32& OutModifierQ12);
	[[nodiscard]] static bool TryGetTerrainFinalDamageModifierQ12(
		const FConditionId& TerrainId,
		EPokemonType MoveType,
		bool bDefenderGrounded,
		bool bMoveAffectedByGrassyTerrain,
		int32& OutModifierQ12);
	[[nodiscard]] static bool ShouldTerrainPreventMajorStatus(
		const FConditionId& TerrainId,
		const FConditionId& RequestedStatusId,
		bool bTargetGrounded);
	[[nodiscard]] static bool ShouldTerrainPreventConfusion(
		const FConditionId& TerrainId,
		bool bTargetGrounded);
	[[nodiscard]] static bool ShouldPsychicTerrainBlockMove(
		const FConditionId& TerrainId,
		bool bAppliedByOpponent,
		bool bTargetGrounded,
		int32 ResolvedIntegerPriority,
		int32 ResolvedFractionalPriorityTenths);

	[[nodiscard]] static bool TryResolveFieldResidual(
		const FBattleFieldResidualFacts& Facts,
		FBattleFieldResidualResult& OutResult);
	[[nodiscard]] static bool TryResolveHazardSwitchIn(
		const FBattleHazardSwitchInFacts& Facts,
		FBattleHazardSwitchInResult& OutResult);

	[[nodiscard]] static bool TryGetScreenDamageModifierQ12(
		TConstArrayView<FConditionId> ActiveSideConditions,
		EBattleMoveCategory MoveCategory,
		bool bDoubles,
		bool bCritical,
		bool bBypassesScreens,
		int32& OutModifierQ12);
	[[nodiscard]] static bool TryApplyTailwindSpeed(
		bool bTailwindActive,
		int32 EffectiveSpeed,
		int32& OutEffectiveSpeed);
	[[nodiscard]] static bool ShouldSafeguardPrevent(
		bool bSafeguardActive,
		bool bAppliedByOpponent,
		bool bBypassesSideProtection);
	[[nodiscard]] static bool ShouldMistPreventStatDrop(
		bool bMistActive,
		bool bAppliedByOpponent,
		bool bBypassesSideProtection,
		int32 RequestedStageDelta);

	[[nodiscard]] static bool ShouldReverseSpeedOrder(bool bTrickRoomActive);
	[[nodiscard]] static bool ShouldSuppressHeldItemEffects(bool bMagicRoomActive);
	[[nodiscard]] static EBattleStat ResolveWonderRoomDefensiveStat(
		bool bWonderRoomActive,
		EBattleStat RequestedStat);
	[[nodiscard]] static constexpr int32 GetTrickRoomMovePriority() { return -7; }

	[[nodiscard]] static bool TryGetTriggerEffectId(
		const FConditionId& ConditionId,
		EBattleTriggerPhase Phase,
		FBattleTriggerEffectId& OutEffectId);
	[[nodiscard]] static bool TryBuildTriggerRegistrationSpecs(
		const FBattleFieldSideTriggerRegistrationFacts& Facts,
		TArray<FBattleTriggerRegistrationSpec>& OutSpecs);
	[[nodiscard]] static bool TryRegisterTriggers(
		FBattleTriggerFramework& Framework,
		const FBattleFieldSideTriggerRegistrationFacts& Facts,
		EBattleTriggerError& OutError);
	[[nodiscard]] static bool TryUpdateTriggerLayers(
		FBattleTriggerFramework& Framework,
		const FConditionId& ConditionId,
		const FBattleTriggerSubject& Owner,
		int32 NewLayers,
		const FBattleTriggerOperationContext& Context,
		EBattleTriggerError& OutError);
	[[nodiscard]] static bool TryCleanupTriggers(
		FBattleTriggerFramework& Framework,
		const FConditionId& ConditionId,
		const FBattleTriggerSubject& Owner,
		EBattleTriggerCleanupReason Reason,
		const FBattleTriggerOperationContext& Context,
		EBattleTriggerError& OutError);

	[[nodiscard]] static constexpr int32 GetNeutralModifierQ12() { return 4096; }
	[[nodiscard]] static constexpr int32 GetBoostedModifierQ12() { return 6144; }
	[[nodiscard]] static constexpr int32 GetWeakenedModifierQ12() { return 2048; }
	[[nodiscard]] static constexpr int32 GetTerrainBoostModifierQ12() { return 5325; }
	[[nodiscard]] static constexpr int32 GetDoublesScreenModifierQ12() { return 2732; }
};
