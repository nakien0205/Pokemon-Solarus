#pragma once

#include "CoreMinimal.h"
#include "Battle/BattleDefinitions.h"
#include "Battle/BattleSetup.h"

/** The five canonical Bag-item behaviors owned by C08C. */
enum class EBattleBagItemRuleKind : uint8
{
	None = 0,
	PokeBall = 1,
	HyperPotion = 2,
	Revive = 3,
	FullHeal = 4,
	XAttack = 5,
	Invalid = 255
};

/** The selector payload family required by one canonical Bag item. */
enum class EBattleBagItemTargetKind : uint8
{
	Party = 0,
	Active = 1,
	Invalid = 255
};

/** Complete battle-local facts used for both selection and execution revalidation. */
struct POKEMONSOLARUS_API FBattleBagItemUseFacts
{
	FItemId ItemId;
	EBattleItemKind DefinitionKind = EBattleItemKind::Invalid;
	EBattleBagItemTargetKind TargetKind = EBattleBagItemTargetKind::Invalid;
	bool bActingTrainerMayUseBag = false;
	bool bActingTrainerMayCapture = false;
	bool bActingTrainerMayUseRevive = false;
	bool bTargetOwnedByActingTrainer = false;
	bool bTargetIsActingBattler = false;
	bool bTargetIsOpposingActive = false;
	bool bTargetEgg = false;
	bool bTargetCaptured = false;
	bool bTargetRemoved = false;
	bool bTargetFainted = false;
	bool bTargetFaintTransitionPending = false;
	int32 CurrentHP = 0;
	int32 MaximumHP = 0;
	bool bHasCanonicalMajorStatus = false;
	bool bHasConfusion = false;
	int32 AttackStage = 0;
};

/** Pure legality and exact mutation values for one canonical Bag-item use. */
struct POKEMONSOLARUS_API FBattleBagItemUseResult
{
	bool bValid = false;
	bool bLegal = false;
	EBattleBagItemRuleKind Kind = EBattleBagItemRuleKind::Invalid;
	bool bCaptureHandoff = false;
	bool bRevives = false;
	int32 HealAmount = 0;
	bool bCuresMajorStatus = false;
	bool bCuresConfusion = false;
	int32 RequestedAttackStageDelta = 0;
	int32 AppliedAttackStageDelta = 0;
	int32 ResultingAttackStage = 0;
};

/**
 * Pure C08C Bag-item rules. This layer performs no state mutation, event emission,
 * persistent inventory write, capture calculation, or RNG.
 */
class POKEMONSOLARUS_API FBattleBagItemRules
{
public:
	[[nodiscard]] static FItemId GetPokeBallId();
	[[nodiscard]] static FItemId GetHyperPotionId();
	[[nodiscard]] static FItemId GetReviveId();
	[[nodiscard]] static FItemId GetFullHealId();
	[[nodiscard]] static FItemId GetXAttackId();
	[[nodiscard]] static TArray<FItemId> GetCanonicalIds();

	[[nodiscard]] static EBattleBagItemRuleKind GetKind(const FItemId& ItemId);
	[[nodiscard]] static bool IsCanonical(const FItemId& ItemId);
	[[nodiscard]] static EBattleItemKind GetExpectedDefinitionKind(
		EBattleBagItemRuleKind Kind);
	[[nodiscard]] static EBattleBagItemTargetKind GetTargetKind(
		EBattleBagItemRuleKind Kind);

	/** Evaluates exact pre-use legality and mutation values without consuming resources. */
	[[nodiscard]] static bool TryEvaluateUse(
		const FBattleBagItemUseFacts& Facts,
		FBattleBagItemUseResult& OutResult);

	[[nodiscard]] static constexpr int32 GetHyperPotionHealAmount() { return 120; }
	[[nodiscard]] static constexpr int32 GetXAttackStageIncrease() { return 2; }
};
