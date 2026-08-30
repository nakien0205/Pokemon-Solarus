#pragma once

#include "CoreMinimal.h"
#include "Battle/BattleDefinitions.h"

/** Pure result for the conditional held-item move power rule. */
struct FBattleHeldItemMovePowerModifierResult
{
	bool bValid = false;
	bool bApplies = false;
	FDefinitionId RuleId;
	int32 ModifierQ12 = 4096;
};

/** Stateless validation and policy for authored held-item move operations. */
class FBattleHeldItemMoveEffects final
{
public:
	static constexpr int32 RemoveCurrentPowerModifierQ12 = 6144;

	[[nodiscard]] static bool IsKnownOperation(
		EBattleMoveHeldItemOperation Operation);

	[[nodiscard]] static bool IsOperationDescriptorValid(
		EBattleMoveCategory MoveCategory,
		EBattleTargetClass MoveTargetClass,
		const FBattleMoveEffectDescriptor& Effect,
		bool bIsFinalDescriptor,
		bool bHasEarlierDamageDescriptor);

	/** Validates every legacy/R5 held-item payload shape without catalog access. */
	[[nodiscard]] static bool TryValidateMoveDefinition(
		const FBattleMoveDefinition& Move);

	[[nodiscard]] static bool IsHeldItemTakeable(
		bool bHasCurrentItem,
		bool bConsumed,
		bool bTemporarilyRemoved,
		const FBattleItemDefinition* ItemDefinition);

	[[nodiscard]] static bool TryResolvePowerModifier(
		const FBattleMoveDefinition& Move,
		bool bTargetHasCurrentItem,
		bool bTargetItemConsumed,
		bool bTargetItemTemporarilyRemoved,
		const FBattleItemDefinition* TargetItemDefinition,
		FBattleHeldItemMovePowerModifierResult& OutResult);
};
