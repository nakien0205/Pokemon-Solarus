#pragma once

#include "CoreMinimal.h"
#include "Battle/BattleSetup.h"

/** Stable failure from compiling one validated setup into C09A encounter policies. */
enum class EBattleEncounterPolicyError : uint8
{
	None = 0,
	InvalidSetup = 1,
	UnsupportedEncounterKind = 2,
	UnsupportedFormat = 3,
	InvalidTrainerShape = 4,
	IncompatibleCommandPolicy = 5,
	InvalidWildFleePolicy = 6
};

/** Typed selector family used to route an authored selector profile. */
enum class EBattleSelectorProfileTag : uint8
{
	None = 0,
	Wild = 1,
	Basic = 2,
	Skilled = 3,
	Boss = 4,
	Tutorial = 5,
	Partner = 6
};

/** Frozen switch-prompt behavior for one encounter. */
enum class EBattleStylePolicy : uint8
{
	Set = 0,
	Shift = 1
};

/** Frozen reinforcement capacity. C09B owns the actual action and slot mutation. */
enum class EBattleReinforcementPolicy : uint8
{
	Disabled = 0,
	OneWildRightSlot = 1
};

/** One Trainer's derived permissions and selector routing inside an encounter. */
struct POKEMONSOLARUS_API FBattleTrainerEncounterPolicy
{
	FTrainerId TrainerId;
	EBattleTrainerRole Role = EBattleTrainerRole::Player;
	EBattleDecisionController Controller = EBattleDecisionController::Human;
	FDefinitionId SelectorProfileId;
	EBattleSelectorProfileTag SelectorProfileTag = EBattleSelectorProfileTag::None;
	bool bMayUseBag = false;
	bool bMayUseRevive = false;
	bool bMayRun = false;
	bool bMayCapture = false;
	bool bPartnerOwnsSeparatePartyAndBag = false;
};

/**
 * Immutable-by-interface result of compiling encounter kind, format, and authored
 * setup switches. The result is derived entirely from facts already serialized
 * by the setup/replay contract.
 */
class POKEMONSOLARUS_API FBattleCompiledEncounterPolicies
{
public:
	FBattleCompiledEncounterPolicies() = default;

	[[nodiscard]] bool IsValid() const { return bValid; }
	[[nodiscard]] EBattleEncounterKind GetEncounterKind() const { return EncounterKind; }
	[[nodiscard]] EBattleFormat GetFormat() const { return Format; }
	[[nodiscard]] int32 GetMaximumActiveBattlersPerSide() const { return MaximumActiveBattlersPerSide; }
	[[nodiscard]] int32 GetMaximumPartySize() const { return MaximumPartySize; }
	[[nodiscard]] bool IsRunAllowed() const { return bRunAllowed; }
	[[nodiscard]] bool IsCaptureAllowed() const { return bCaptureAllowed; }
	[[nodiscard]] bool IsBagAllowed() const { return bBagAllowed; }
	[[nodiscard]] EBattleStylePolicy GetBattleStyle() const { return BattleStyle; }
	[[nodiscard]] EBattleReinforcementPolicy GetReinforcementPolicy() const { return ReinforcementPolicy; }
	[[nodiscard]] bool IsWildFleeConfigured() const { return bWildFleeConfigured; }
	[[nodiscard]] EBattleWildFleeMode GetWildFleeMode() const { return WildFleeMode; }
	[[nodiscard]] uint32 GetWildFleeNumerator() const { return WildFleeNumerator; }
	[[nodiscard]] uint32 GetWildFleeDenominator() const { return WildFleeDenominator; }
	[[nodiscard]] bool IsScriptedEndingAllowed() const { return bScriptedEndingAllowed; }
	[[nodiscard]] bool HasSeparatePartnerOwnership() const { return bSeparatePartnerOwnership; }
	[[nodiscard]] TConstArrayView<FBattleTrainerEncounterPolicy> GetTrainerPolicies() const
	{
		return TrainerPolicies;
	}

	/** Finds one compiled Trainer policy by stable identity, or returns null. */
	[[nodiscard]] const FBattleTrainerEncounterPolicy* FindTrainerPolicy(FTrainerId TrainerId) const;

private:
	friend class FBattleEncounterPolicyCompiler;

	bool bValid = false;
	EBattleEncounterKind EncounterKind = EBattleEncounterKind::Wild;
	EBattleFormat Format = EBattleFormat::Single;
	int32 MaximumActiveBattlersPerSide = 0;
	int32 MaximumPartySize = 0;
	bool bRunAllowed = false;
	bool bCaptureAllowed = false;
	bool bBagAllowed = false;
	EBattleStylePolicy BattleStyle = EBattleStylePolicy::Set;
	EBattleReinforcementPolicy ReinforcementPolicy = EBattleReinforcementPolicy::Disabled;
	bool bWildFleeConfigured = false;
	EBattleWildFleeMode WildFleeMode = EBattleWildFleeMode::Disabled;
	uint32 WildFleeNumerator = 0;
	uint32 WildFleeDenominator = 0;
	bool bScriptedEndingAllowed = false;
	bool bSeparatePartnerOwnership = false;
	TArray<FBattleTrainerEncounterPolicy> TrainerPolicies;
};

/** Pure C09A setup-to-policy compiler. It owns no battle state or RNG. */
class POKEMONSOLARUS_API FBattleEncounterPolicyCompiler
{
public:
	/** Validates encounter-only constraints and derives the complete typed policy set. */
	[[nodiscard]] static bool TryCompile(
		const FBattleSetup& Setup,
		FBattleCompiledEncounterPolicies& OutPolicies,
		EBattleEncounterPolicyError& OutError);
};
