#include "Battle/BattleBagItem.h"

#include "Battle/BattleStatStages.h"

namespace
{
	template <typename IdType>
	IdType MakeBagDefinitionId(const TCHAR* Value)
	{
		IdType Id;
		const bool bCreated = IdType::TryCreate(FName(Value), Id);
		check(bCreated);
		return Id;
	}

	bool IsKnownTargetKind(const EBattleBagItemTargetKind Kind)
	{
		return Kind == EBattleBagItemTargetKind::Party
			|| Kind == EBattleBagItemTargetKind::Active;
	}

	bool IsKnownTrainerRole(const EBattleTrainerRole Role)
	{
		return Role == EBattleTrainerRole::Player
			|| Role == EBattleTrainerRole::Partner
			|| Role == EBattleTrainerRole::Opponent;
	}

	bool IsLivingTarget(const FBattleBagItemUseFacts& Facts)
	{
		return Facts.CurrentHP > 0
			&& !Facts.bTargetFainted
			&& !Facts.bTargetFaintTransitionPending
			&& !Facts.bTargetEgg
			&& !Facts.bTargetCaptured
			&& !Facts.bTargetRemoved;
	}
}

FItemId FBattleBagItemRules::GetPokeBallId()
{
	return MakeBagDefinitionId<FItemId>(TEXT("Item.PokeBall"));
}

FItemId FBattleBagItemRules::GetHyperPotionId()
{
	return MakeBagDefinitionId<FItemId>(TEXT("Item.HyperPotion"));
}

FItemId FBattleBagItemRules::GetReviveId()
{
	return MakeBagDefinitionId<FItemId>(TEXT("Item.Revive"));
}

FItemId FBattleBagItemRules::GetFullHealId()
{
	return MakeBagDefinitionId<FItemId>(TEXT("Item.FullHeal"));
}

FItemId FBattleBagItemRules::GetXAttackId()
{
	return MakeBagDefinitionId<FItemId>(TEXT("Item.XAttack"));
}

TArray<FItemId> FBattleBagItemRules::GetCanonicalIds()
{
	return {
		GetPokeBallId(),
		GetHyperPotionId(),
		GetReviveId(),
		GetFullHealId(),
		GetXAttackId()
	};
}

EBattleBagItemRuleKind FBattleBagItemRules::GetKind(const FItemId& ItemId)
{
	if (!ItemId.IsValid()) return EBattleBagItemRuleKind::None;
	if (ItemId == GetPokeBallId()) return EBattleBagItemRuleKind::PokeBall;
	if (ItemId == GetHyperPotionId()) return EBattleBagItemRuleKind::HyperPotion;
	if (ItemId == GetReviveId()) return EBattleBagItemRuleKind::Revive;
	if (ItemId == GetFullHealId()) return EBattleBagItemRuleKind::FullHeal;
	if (ItemId == GetXAttackId()) return EBattleBagItemRuleKind::XAttack;
	return EBattleBagItemRuleKind::Invalid;
}

bool FBattleBagItemRules::IsCanonical(const FItemId& ItemId)
{
	const EBattleBagItemRuleKind Kind = GetKind(ItemId);
	return Kind != EBattleBagItemRuleKind::None
		&& Kind != EBattleBagItemRuleKind::Invalid;
}

EBattleItemKind FBattleBagItemRules::GetExpectedDefinitionKind(
	const EBattleBagItemRuleKind Kind)
{
	return Kind == EBattleBagItemRuleKind::PokeBall
		? EBattleItemKind::Capture
		: (Kind >= EBattleBagItemRuleKind::HyperPotion
			&& Kind <= EBattleBagItemRuleKind::XAttack
			? EBattleItemKind::Battle
			: EBattleItemKind::Invalid);
}

EBattleBagItemTargetKind FBattleBagItemRules::GetTargetKind(
	const EBattleBagItemRuleKind Kind)
{
	switch (Kind)
	{
	case EBattleBagItemRuleKind::PokeBall:
	case EBattleBagItemRuleKind::XAttack:
		return EBattleBagItemTargetKind::Active;
	case EBattleBagItemRuleKind::HyperPotion:
	case EBattleBagItemRuleKind::Revive:
	case EBattleBagItemRuleKind::FullHeal:
		return EBattleBagItemTargetKind::Party;
	default:
		return EBattleBagItemTargetKind::Invalid;
	}
}

bool FBattleBagItemRules::TryEvaluateUse(
	const FBattleBagItemUseFacts& Facts,
	FBattleBagItemUseResult& OutResult)
{
	OutResult = FBattleBagItemUseResult();
	const EBattleBagItemRuleKind Kind = GetKind(Facts.ItemId);
	if (Kind == EBattleBagItemRuleKind::None
		|| Kind == EBattleBagItemRuleKind::Invalid
		|| !IsKnownTargetKind(Facts.TargetKind)
		|| !IsKnownTrainerRole(Facts.ActingTrainerRole)
		|| Facts.MaximumHP <= 0
		|| Facts.CurrentHP < 0
		|| Facts.CurrentHP > Facts.MaximumHP
		|| Facts.bTargetFainted != (Facts.CurrentHP == 0)
		|| (Facts.bTargetFaintTransitionPending && !Facts.bTargetFainted)
		|| Facts.AttackStage < FBattleStatStages::MinimumStage
		|| Facts.AttackStage > FBattleStatStages::MaximumStage
		|| (Facts.bTargetIsActingBattler && !Facts.bTargetOwnedByActingTrainer)
		|| (Facts.bTargetIsOpposingActive && Facts.bTargetOwnedByActingTrainer))
	{
		return false;
	}

	OutResult.bValid = true;
	OutResult.Kind = Kind;
	OutResult.ResultingAttackStage = Facts.AttackStage;
	if (Facts.DefinitionKind != GetExpectedDefinitionKind(Kind)
		|| Facts.TargetKind != GetTargetKind(Kind))
	{
		return true;
	}

	switch (Kind)
	{
	case EBattleBagItemRuleKind::PokeBall:
		OutResult.bLegal = Facts.ActingTrainerRole == EBattleTrainerRole::Player
			&& Facts.EncounterKind == EBattleEncounterKind::Wild
			&& Facts.bCaptureAllowed
			&& Facts.bTargetIsOpposingActive
			&& IsLivingTarget(Facts);
		OutResult.bCaptureHandoff = OutResult.bLegal;
		break;

	case EBattleBagItemRuleKind::HyperPotion:
		OutResult.bLegal = Facts.bTargetOwnedByActingTrainer
			&& IsLivingTarget(Facts)
			&& Facts.CurrentHP < Facts.MaximumHP;
		if (OutResult.bLegal)
		{
			OutResult.HealAmount = FMath::Min(
				GetHyperPotionHealAmount(),
				Facts.MaximumHP - Facts.CurrentHP);
		}
		break;

	case EBattleBagItemRuleKind::Revive:
		// An opponent reaches this rule only for an item in its finite authored Bag.
		// C09A admits that explicit Revive configuration for Boss/Gym encounters only.
		OutResult.bLegal = (Facts.ActingTrainerRole != EBattleTrainerRole::Opponent
				|| Facts.EncounterKind == EBattleEncounterKind::BossGym)
			&& Facts.bTargetOwnedByActingTrainer
			&& Facts.bTargetFainted
			&& !Facts.bTargetFaintTransitionPending
			&& !Facts.bTargetEgg
			&& !Facts.bTargetCaptured;
		if (OutResult.bLegal)
		{
			OutResult.bRevives = true;
			OutResult.HealAmount = FMath::Max(1, Facts.MaximumHP / 2);
		}
		break;

	case EBattleBagItemRuleKind::FullHeal:
		OutResult.bLegal = Facts.bTargetOwnedByActingTrainer
			&& IsLivingTarget(Facts)
			&& (Facts.bHasCanonicalMajorStatus || Facts.bHasConfusion);
		if (OutResult.bLegal)
		{
			OutResult.bCuresMajorStatus = Facts.bHasCanonicalMajorStatus;
			OutResult.bCuresConfusion = Facts.bHasConfusion;
		}
		break;

	case EBattleBagItemRuleKind::XAttack:
		OutResult.bLegal = Facts.bTargetOwnedByActingTrainer
			&& Facts.bTargetIsActingBattler
			&& IsLivingTarget(Facts)
			&& Facts.AttackStage < FBattleStatStages::MaximumStage;
		if (OutResult.bLegal)
		{
			OutResult.RequestedAttackStageDelta = GetXAttackStageIncrease();
			OutResult.ResultingAttackStage = FMath::Min(
				FBattleStatStages::MaximumStage,
				Facts.AttackStage + GetXAttackStageIncrease());
			OutResult.AppliedAttackStageDelta =
				OutResult.ResultingAttackStage - Facts.AttackStage;
		}
		break;

	default:
		return false;
	}

	return true;
}
