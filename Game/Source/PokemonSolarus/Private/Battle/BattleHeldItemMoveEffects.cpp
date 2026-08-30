#include "BattleHeldItemMoveEffects.h"

namespace BattleHeldItemMoveEffectsPrivate
{
	FDefinitionId GetRemoveCurrentPowerRuleId()
	{
		FDefinitionId RuleId;
		const bool bCreated = FDefinitionId::TryCreate(
			FName(TEXT("Rule.C08C.RemoveCurrentPower")),
			RuleId);
		check(bCreated);
		return RuleId;
	}

	bool IsResolvedOtherBattlerTargetClass(const EBattleTargetClass TargetClass)
	{
		return TargetClass == EBattleTargetClass::SelectedOpponent
			|| TargetClass == EBattleTargetClass::RandomLegalOpponent
			|| TargetClass == EBattleTargetClass::SelectedOtherBattler;
	}
}

bool FBattleHeldItemMoveEffects::IsKnownOperation(
	const EBattleMoveHeldItemOperation Operation)
{
	return Operation == EBattleMoveHeldItemOperation::None
		|| Operation == EBattleMoveHeldItemOperation::RemoveCurrent
		|| Operation == EBattleMoveHeldItemOperation::ExchangeCurrent
		|| Operation == EBattleMoveHeldItemOperation::TransferCurrent
		|| Operation == EBattleMoveHeldItemOperation::RestoreLastConsumed;
}

bool FBattleHeldItemMoveEffects::IsOperationDescriptorValid(
	const EBattleMoveCategory MoveCategory,
	const EBattleTargetClass MoveTargetClass,
	const FBattleMoveEffectDescriptor& Effect,
	const bool bIsFinalDescriptor,
	const bool bHasEarlierDamageDescriptor)
{
	if (Effect.Kind != EBattleMoveEffectKind::ChangeItem
		|| !IsKnownOperation(Effect.HeldItemOperation)
		|| Effect.HeldItemOperation == EBattleMoveHeldItemOperation::None
		|| Effect.ItemId.IsValid()
		|| EnumHasAllFlags(Effect.Flags, EBattleMoveEffectFlags::PerHit)
		|| !bIsFinalDescriptor)
	{
		return false;
	}

	const bool bDamagingMove = MoveCategory == EBattleMoveCategory::Physical
		|| MoveCategory == EBattleMoveCategory::Special;
	switch (Effect.HeldItemOperation)
	{
	case EBattleMoveHeldItemOperation::RemoveCurrent:
	case EBattleMoveHeldItemOperation::TransferCurrent:
		return bDamagingMove
			&& bHasEarlierDamageDescriptor
			&& BattleHeldItemMoveEffectsPrivate::IsResolvedOtherBattlerTargetClass(
				MoveTargetClass)
			&& Effect.Target == EBattleEffectTarget::ResolvedTarget;
	case EBattleMoveHeldItemOperation::ExchangeCurrent:
		return MoveCategory == EBattleMoveCategory::Status
			&& !bHasEarlierDamageDescriptor
			&& BattleHeldItemMoveEffectsPrivate::IsResolvedOtherBattlerTargetClass(
				MoveTargetClass)
			&& Effect.Target == EBattleEffectTarget::ResolvedTarget;
	case EBattleMoveHeldItemOperation::RestoreLastConsumed:
		return MoveCategory == EBattleMoveCategory::Status
			&& !bHasEarlierDamageDescriptor
			&& MoveTargetClass == EBattleTargetClass::Self
			&& Effect.Target == EBattleEffectTarget::User;
	default:
		return false;
	}
}

bool FBattleHeldItemMoveEffects::TryValidateMoveDefinition(
	const FBattleMoveDefinition& Move)
{
	int32 HeldItemMoveOperationCount = 0;
	bool bHasEarlierDamageDescriptor = false;
	for (int32 Index = 0; Index < Move.Effects.Num(); ++Index)
	{
		const FBattleMoveEffectDescriptor& Effect = Move.Effects[Index];
		if (!IsKnownOperation(Effect.HeldItemOperation))
		{
			return false;
		}
		if (Effect.Kind == EBattleMoveEffectKind::ChangeItem)
		{
			const bool bLegacyShape =
				Effect.HeldItemOperation == EBattleMoveHeldItemOperation::None
				&& Effect.ItemId.IsValid();
			const bool bR5Shape =
				Effect.HeldItemOperation != EBattleMoveHeldItemOperation::None
				&& IsOperationDescriptorValid(
					Move.Category,
					Move.TargetClass,
					Effect,
					Index == Move.Effects.Num() - 1,
					bHasEarlierDamageDescriptor);
			if (!bLegacyShape && !bR5Shape)
			{
				return false;
			}
			if (bR5Shape)
			{
				++HeldItemMoveOperationCount;
			}
		}
		else if (Effect.ItemId.IsValid()
			|| Effect.HeldItemOperation != EBattleMoveHeldItemOperation::None)
		{
			return false;
		}
		if (Effect.Kind == EBattleMoveEffectKind::Damage)
		{
			bHasEarlierDamageDescriptor = true;
		}
	}
	return HeldItemMoveOperationCount <= 1;
}

bool FBattleHeldItemMoveEffects::IsHeldItemTakeable(
	const bool bHasCurrentItem,
	const bool bConsumed,
	const bool bTemporarilyRemoved,
	const FBattleItemDefinition* ItemDefinition)
{
	return bHasCurrentItem
		&& !bConsumed
		&& !bTemporarilyRemoved
		&& ItemDefinition != nullptr
		&& ItemDefinition->bCanBeTakenByMove;
}

bool FBattleHeldItemMoveEffects::TryResolvePowerModifier(
	const FBattleMoveDefinition& Move,
	const bool bTargetHasCurrentItem,
	const bool bTargetItemConsumed,
	const bool bTargetItemTemporarilyRemoved,
	const FBattleItemDefinition* TargetItemDefinition,
	FBattleHeldItemMovePowerModifierResult& OutResult)
{
	OutResult = FBattleHeldItemMovePowerModifierResult();
	if (!TryValidateMoveDefinition(Move))
	{
		return false;
	}
	int32 RemoveCurrentCount = 0;
	for (const FBattleMoveEffectDescriptor& Effect : Move.Effects)
	{
		if (Effect.HeldItemOperation == EBattleMoveHeldItemOperation::RemoveCurrent)
		{
			++RemoveCurrentCount;
		}
	}
	if (RemoveCurrentCount > 1)
	{
		return false;
	}

	OutResult.bValid = true;
	if (RemoveCurrentCount == 0)
	{
		return true;
	}
	if (bTargetHasCurrentItem && TargetItemDefinition == nullptr)
	{
		OutResult = FBattleHeldItemMovePowerModifierResult();
		return false;
	}
	if (!IsHeldItemTakeable(
			bTargetHasCurrentItem,
			bTargetItemConsumed,
			bTargetItemTemporarilyRemoved,
			TargetItemDefinition))
	{
		return true;
	}

	OutResult.bApplies = true;
	OutResult.RuleId = BattleHeldItemMoveEffectsPrivate::GetRemoveCurrentPowerRuleId();
	OutResult.ModifierQ12 = RemoveCurrentPowerModifierQ12;
	return true;
}
