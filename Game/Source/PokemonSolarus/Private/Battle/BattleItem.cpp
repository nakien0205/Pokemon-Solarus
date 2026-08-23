#include "Battle/BattleItem.h"

namespace BattleItemPrivate
{
	template <typename IdType>
	IdType MakeNamedId(const TCHAR* Name)
	{
		IdType Result;
		const bool bCreated = IdType::TryCreate(FName(Name), Result);
		check(bCreated);
		return Result;
	}

	const TCHAR* GetKindName(const EBattleHeldItemRuleKind Kind)
	{
		switch (Kind)
		{
		case EBattleHeldItemRuleKind::Leftovers: return TEXT("Leftovers");
		case EBattleHeldItemRuleKind::SitrusBerry: return TEXT("SitrusBerry");
		case EBattleHeldItemRuleKind::LumBerry: return TEXT("LumBerry");
		case EBattleHeldItemRuleKind::FocusSash: return TEXT("FocusSash");
		case EBattleHeldItemRuleKind::LifeOrb: return TEXT("LifeOrb");
		case EBattleHeldItemRuleKind::ChoiceBand: return TEXT("ChoiceBand");
		case EBattleHeldItemRuleKind::HeavyDutyBoots: return TEXT("HeavyDutyBoots");
		case EBattleHeldItemRuleKind::AirBalloon: return TEXT("AirBalloon");
		case EBattleHeldItemRuleKind::QuickClaw: return TEXT("QuickClaw");
		default: return TEXT("Invalid");
		}
	}

	FBattleAbilityItemHookDefinition MakeHookDefinition(
		const FItemId& ItemId,
		const EBattleHeldItemRuleKind Kind,
		const TCHAR* HookName,
		const EBattleAbilityItemHookPoint HookPoint,
		const EBattleAbilityItemEffectKind EffectKind,
		const EBattleTriggerPhase Phase,
		const EBattleAbilityItemRevealPolicy RevealPolicy,
		const int32 Order = 0,
		const int32 Suborder = 0,
		const bool bRepeatable = false)
	{
		const TCHAR* KindName = GetKindName(Kind);
		FBattleAbilityItemHookDefinition Definition;
		Definition.HookId = MakeNamedId<FDefinitionId>(
			*FString::Printf(TEXT("Hook.Item.%s.%s"), KindName, HookName));
		Definition.HookPoint = HookPoint;
		Definition.EffectKind = EffectKind;
		Definition.TriggerRule.Phase = Phase;
		Definition.TriggerRule.EffectId = MakeNamedId<FBattleTriggerEffectId>(
			*FString::Printf(TEXT("HookEffect.Item.%s.%s"), KindName, HookName));
		Definition.TriggerRule.PayloadId = ItemId.GetDefinitionId();
		Definition.TriggerRule.Order = Order;
		Definition.TriggerRule.Suborder = Suborder;
		Definition.TriggerRule.bRepeatable = bRepeatable;
		Definition.RevealPolicy = RevealPolicy;
		Definition.bBreakable = false;
		return Definition;
	}

	bool IsKnownPhase(const EBattleTriggerPhase Phase)
	{
		return static_cast<uint8>(Phase) <= static_cast<uint8>(EBattleTriggerPhase::Expiry);
	}

	bool IsKnownMoveCategory(const EBattleMoveCategory Category)
	{
		return Category == EBattleMoveCategory::Physical
			|| Category == EBattleMoveCategory::Special
			|| Category == EBattleMoveCategory::Status;
	}

}

FItemId FBattleItemRules::GetLeftoversId()
{
	return BattleItemPrivate::MakeNamedId<FItemId>(TEXT("Item.Leftovers"));
}

FItemId FBattleItemRules::GetSitrusBerryId()
{
	return BattleItemPrivate::MakeNamedId<FItemId>(TEXT("Item.SitrusBerry"));
}

FItemId FBattleItemRules::GetLumBerryId()
{
	return BattleItemPrivate::MakeNamedId<FItemId>(TEXT("Item.LumBerry"));
}

FItemId FBattleItemRules::GetFocusSashId()
{
	return BattleItemPrivate::MakeNamedId<FItemId>(TEXT("Item.FocusSash"));
}

FItemId FBattleItemRules::GetLifeOrbId()
{
	return BattleItemPrivate::MakeNamedId<FItemId>(TEXT("Item.LifeOrb"));
}

FItemId FBattleItemRules::GetChoiceBandId()
{
	return BattleItemPrivate::MakeNamedId<FItemId>(TEXT("Item.ChoiceBand"));
}

FItemId FBattleItemRules::GetHeavyDutyBootsId()
{
	return BattleItemPrivate::MakeNamedId<FItemId>(TEXT("Item.HeavyDutyBoots"));
}

FItemId FBattleItemRules::GetAirBalloonId()
{
	return BattleItemPrivate::MakeNamedId<FItemId>(TEXT("Item.AirBalloon"));
}

FItemId FBattleItemRules::GetQuickClawId()
{
	return BattleItemPrivate::MakeNamedId<FItemId>(TEXT("Item.QuickClaw"));
}

TArray<FItemId> FBattleItemRules::GetCanonicalIds()
{
	return {
		GetLeftoversId(),
		GetSitrusBerryId(),
		GetLumBerryId(),
		GetFocusSashId(),
		GetLifeOrbId(),
		GetChoiceBandId(),
		GetHeavyDutyBootsId(),
		GetAirBalloonId(),
		GetQuickClawId()
	};
}

EBattleHeldItemRuleKind FBattleItemRules::GetKind(const FItemId& ItemId)
{
	if (!ItemId.IsValid()) return EBattleHeldItemRuleKind::None;
	if (ItemId == GetLeftoversId()) return EBattleHeldItemRuleKind::Leftovers;
	if (ItemId == GetSitrusBerryId()) return EBattleHeldItemRuleKind::SitrusBerry;
	if (ItemId == GetLumBerryId()) return EBattleHeldItemRuleKind::LumBerry;
	if (ItemId == GetFocusSashId()) return EBattleHeldItemRuleKind::FocusSash;
	if (ItemId == GetLifeOrbId()) return EBattleHeldItemRuleKind::LifeOrb;
	if (ItemId == GetChoiceBandId()) return EBattleHeldItemRuleKind::ChoiceBand;
	if (ItemId == GetHeavyDutyBootsId()) return EBattleHeldItemRuleKind::HeavyDutyBoots;
	if (ItemId == GetAirBalloonId()) return EBattleHeldItemRuleKind::AirBalloon;
	if (ItemId == GetQuickClawId()) return EBattleHeldItemRuleKind::QuickClaw;
	return EBattleHeldItemRuleKind::Invalid;
}

bool FBattleItemRules::IsCanonical(const FItemId& ItemId)
{
	const EBattleHeldItemRuleKind Kind = GetKind(ItemId);
	return Kind != EBattleHeldItemRuleKind::None
		&& Kind != EBattleHeldItemRuleKind::Invalid;
}

bool FBattleItemRules::TryBuildHookDefinitions(
	const FItemId& ItemId,
	TArray<FBattleAbilityItemHookDefinition>& OutDefinitions)
{
	OutDefinitions.Reset();
	const EBattleHeldItemRuleKind Kind = GetKind(ItemId);
	using EHook = EBattleAbilityItemHookPoint;
	using EEffect = EBattleAbilityItemEffectKind;
	using EReveal = EBattleAbilityItemRevealPolicy;

	auto Add = [&OutDefinitions, &ItemId, Kind](
		const TCHAR* HookName,
		const EHook HookPoint,
		const EEffect EffectKind,
		const EBattleTriggerPhase Phase,
		const EReveal RevealPolicy,
		const int32 Order = 0,
		const int32 Suborder = 0,
		const bool bRepeatable = false)
	{
		OutDefinitions.Add(BattleItemPrivate::MakeHookDefinition(
			ItemId,
			Kind,
			HookName,
			HookPoint,
			EffectKind,
			Phase,
			RevealPolicy,
			Order,
			Suborder,
			bRepeatable));
	};

	switch (Kind)
	{
	case EBattleHeldItemRuleKind::Leftovers:
		Add(TEXT("EndTurn"), EHook::EndTurn, EEffect::Modify,
			EBattleTriggerPhase::EndTurn, EReveal::OnAppliedEffect,
			GetLeftoversResidualOrder(), GetLeftoversResidualSuborder());
		break;
	case EBattleHeldItemRuleKind::SitrusBerry:
		Add(TEXT("ImmediateRecovery"), EHook::AfterDamage, EEffect::ConsumeItem,
			EBattleTriggerPhase::AfterDamage, EReveal::OnAppliedEffect,
			0, 0, true);
		break;
	case EBattleHeldItemRuleKind::LumBerry:
		Add(TEXT("ImmediateCure"), EHook::EffectApplication, EEffect::ConsumeItem,
			EBattleTriggerPhase::AfterHit, EReveal::OnAppliedEffect,
			0, 0, true);
		break;
	case EBattleHeldItemRuleKind::FocusSash:
		Add(TEXT("FaintPrevention"), EHook::FaintPrevention, EEffect::Prevent,
			EBattleTriggerPhase::BeforeDamage, EReveal::OnAppliedEffect,
			0, 0, true);
		break;
	case EBattleHeldItemRuleKind::LifeOrb:
		Add(TEXT("FinalDamage"), EHook::FinalDamage, EEffect::Modify,
			EBattleTriggerPhase::BeforeDamage, EReveal::Never,
			0, 0, true);
		Add(TEXT("PostMoveRecoil"), EHook::AfterDamage, EEffect::Modify,
			EBattleTriggerPhase::AfterAction, EReveal::OnAppliedEffect,
			0, 0, true);
		break;
	case EBattleHeldItemRuleKind::ChoiceBand:
		Add(TEXT("SelectionEligibility"), EHook::SelectionEligibility, EEffect::Prevent,
			EBattleTriggerPhase::SelectionEligibility, EReveal::Never,
			0, 0, true);
		Add(TEXT("EstablishMoveLock"), EHook::ActionEligibility, EEffect::Modify,
			EBattleTriggerPhase::BeforeAction, EReveal::Never,
			0, 0, true);
		Add(TEXT("PhysicalAttack"), EHook::OffensiveStat, EEffect::Modify,
			EBattleTriggerPhase::BeforeDamage, EReveal::Never,
			0, 0, true);
		Add(TEXT("SwitchCleanup"), EHook::SwitchOut, EEffect::Modify,
			EBattleTriggerPhase::SwitchOut, EReveal::Never);
		break;
	case EBattleHeldItemRuleKind::HeavyDutyBoots:
		Add(TEXT("EntryHazards"), EHook::SwitchIn, EEffect::Prevent,
			EBattleTriggerPhase::SwitchIn, EReveal::OnAppliedEffect,
			0, 0, true);
		break;
	case EBattleHeldItemRuleKind::AirBalloon:
		Add(TEXT("EntryReveal"), EHook::SwitchIn, EEffect::Reveal,
			EBattleTriggerPhase::SwitchIn, EReveal::OnAppliedEffect);
		Add(TEXT("TypeImmunity"), EHook::TypeImmunity, EEffect::Prevent,
			EBattleTriggerPhase::BeforeHit, EReveal::OnAppliedEffect,
			0, 0, true);
		Add(TEXT("PopOnHit"), EHook::AfterDamage, EEffect::RemoveItem,
			EBattleTriggerPhase::AfterDamage, EReveal::OnAppliedEffect,
			0, 0, true);
		break;
	case EBattleHeldItemRuleKind::QuickClaw:
		Add(TEXT("ActionPriority"), EHook::ActionPriority, EEffect::Modify,
			EBattleTriggerPhase::ActionOrderCalculation, EReveal::OnAppliedEffect,
			0, 0, true);
		break;
	default:
		return false;
	}

	for (const FBattleAbilityItemHookDefinition& Definition : OutDefinitions)
	{
		if (!FBattleAbilityItemHookContracts::IsDefinitionValid(Definition))
		{
			OutDefinitions.Reset();
			return false;
		}
	}
	return true;
}

bool FBattleItemRules::TryGetHookDefinition(
	const FItemId& ItemId,
	const FDefinitionId& HookId,
	FBattleAbilityItemHookDefinition& OutDefinition)
{
	OutDefinition = FBattleAbilityItemHookDefinition();
	if (!HookId.IsValid())
	{
		return false;
	}
	TArray<FBattleAbilityItemHookDefinition> Definitions;
	if (!TryBuildHookDefinitions(ItemId, Definitions))
	{
		return false;
	}
	const FBattleAbilityItemHookDefinition* Match = Definitions.FindByPredicate(
		[&HookId](const FBattleAbilityItemHookDefinition& Definition)
		{
			return Definition.HookId == HookId;
		});
	if (Match == nullptr)
	{
		return false;
	}
	OutDefinition = *Match;
	return true;
}

bool FBattleItemRules::TryGetHookDefinitionsForPhase(
	const FItemId& ItemId,
	const EBattleTriggerPhase Phase,
	TArray<FBattleAbilityItemHookDefinition>& OutDefinitions)
{
	OutDefinitions.Reset();
	if (!BattleItemPrivate::IsKnownPhase(Phase))
	{
		return false;
	}
	TArray<FBattleAbilityItemHookDefinition> AllDefinitions;
	if (!TryBuildHookDefinitions(ItemId, AllDefinitions))
	{
		return false;
	}
	for (const FBattleAbilityItemHookDefinition& Definition : AllDefinitions)
	{
		if (Definition.TriggerRule.Phase == Phase)
		{
			OutDefinitions.Add(Definition);
		}
	}
	return !OutDefinitions.IsEmpty();
}

bool FBattleItemRules::TryCreateTypedEffectRequest(
	const FBattleTriggerEffectRequest& TriggerRequest,
	FBattleAbilityItemEffectRequest& OutRequest,
	EBattleAbilityItemHookError& OutError)
{
	OutRequest = FBattleAbilityItemEffectRequest();
	if (!TriggerRequest.SourceDefinition.IsValid()
		|| TriggerRequest.SourceDefinition.Kind
			!= EBattleTriggerSourceDefinitionKind::Item)
	{
		OutError = EBattleAbilityItemHookError::InvalidSourceDefinition;
		return false;
	}

	TArray<FBattleAbilityItemHookDefinition> Definitions;
	if (!TryBuildHookDefinitions(
			TriggerRequest.SourceDefinition.ItemId,
			Definitions))
	{
		OutError = EBattleAbilityItemHookError::InvalidDefinition;
		return false;
	}
	const FBattleAbilityItemHookDefinition* Match = Definitions.FindByPredicate(
		[&TriggerRequest](const FBattleAbilityItemHookDefinition& Definition)
		{
			return Definition.TriggerRule.Phase == TriggerRequest.Phase
				&& Definition.TriggerRule.EffectId == TriggerRequest.EffectId
				&& Definition.TriggerRule.PayloadId == TriggerRequest.PayloadId;
		});
	if (Match == nullptr)
	{
		OutError = EBattleAbilityItemHookError::MismatchedTriggerRequest;
		return false;
	}
	return FBattleAbilityItemHookContracts::TryCreateTypedEffectRequest(
		*Match,
		TriggerRequest,
		OutRequest,
		OutError);
}

bool FBattleItemRules::TryBuildHookRegistrationFacts(
	const FBattleItemRegistrationFacts& Facts,
	TArray<FBattleAbilityItemHookRegistrationFacts>& OutHookFacts,
	EBattleAbilityItemHookError& OutError)
{
	OutHookFacts.Reset();
	TArray<FBattleAbilityItemHookDefinition> Definitions;
	if (!TryBuildHookDefinitions(Facts.ItemId, Definitions))
	{
		OutError = EBattleAbilityItemHookError::InvalidDefinition;
		return false;
	}

	FBattleTriggerSourceDefinition SourceDefinition;
	if (!FBattleTriggerSourceDefinition::TryCreateItem(
			Facts.ItemId,
			SourceDefinition))
	{
		OutError = EBattleAbilityItemHookError::InvalidSourceDefinition;
		return false;
	}

	for (const FBattleAbilityItemHookDefinition& Definition : Definitions)
	{
		FBattleAbilityItemHookRegistrationFacts HookFacts;
		HookFacts.Definition = Definition;
		HookFacts.SourceDefinition = SourceDefinition;
		HookFacts.Owner = Facts.Owner;
		HookFacts.Source = Facts.Source;
		HookFacts.Targets = Facts.Targets;
		HookFacts.DurationOwner = Facts.Owner;
		HookFacts.Layers = 1;
		HookFacts.Visibility = FBattleTriggerVisibility::CreatePublic();
		HookFacts.CleanupPolicy = EBattleTriggerCleanupPolicy::OnSwitch
			| EBattleTriggerCleanupPolicy::OnFaint
			| EBattleTriggerCleanupPolicy::OnCapture
			| EBattleTriggerCleanupPolicy::OnBattleEnd
			| EBattleTriggerCleanupPolicy::OnRemoval;
		HookFacts.bSuppressed = Facts.bSuppressed;

		FBattleTriggerRegistrationSpec IgnoredRegistration;
		if (!FBattleAbilityItemHookContracts::TryBuildTriggerRegistration(
				HookFacts,
				IgnoredRegistration,
				OutError))
		{
			OutHookFacts.Reset();
			return false;
		}
		OutHookFacts.Add(MoveTemp(HookFacts));
	}
	OutError = EBattleAbilityItemHookError::None;
	return true;
}

bool FBattleItemRules::TryRegisterHooks(
	FBattleTriggerFramework& Framework,
	const FBattleItemRegistrationFacts& Facts,
	EBattleAbilityItemHookError& OutError)
{
	TArray<FBattleAbilityItemHookRegistrationFacts> HookFacts;
	if (!TryBuildHookRegistrationFacts(Facts, HookFacts, OutError))
	{
		return false;
	}

	FBattleTriggerFramework Staged = Framework;
	for (const FBattleAbilityItemHookRegistrationFacts& Hook : HookFacts)
	{
		FBattleTriggerRegistrationId IgnoredRegistrationId;
		if (!FBattleAbilityItemHookContracts::TryRegisterHook(
				Staged,
				Hook,
				IgnoredRegistrationId,
				OutError))
		{
			return false;
		}
	}
	Framework = MoveTemp(Staged);
	OutError = EBattleAbilityItemHookError::None;
	return true;
}

bool FBattleItemRules::TryEvaluateRecovery(
	const FBattleItemRecoveryFacts& Facts,
	FBattleItemRecoveryResult& OutResult)
{
	OutResult = FBattleItemRecoveryResult();
	const EBattleHeldItemRuleKind Kind = GetKind(Facts.ItemId);
	if ((Kind != EBattleHeldItemRuleKind::Leftovers
			&& Kind != EBattleHeldItemRuleKind::SitrusBerry)
		|| Facts.BaseMaximumHP <= 0
		|| Facts.CurrentHP < 0
		|| Facts.CurrentHP > Facts.BaseMaximumHP)
	{
		return false;
	}

	OutResult.bValid = true;
	if (Facts.bSuppressed)
	{
		OutResult.Outcome = EBattleAbilityItemActivationOutcome::Suppressed;
		return true;
	}
	if (!Facts.bHealingPermitted
		|| Facts.CurrentHP <= 0
		|| Facts.CurrentHP == Facts.BaseMaximumHP)
	{
		OutResult.Outcome = EBattleAbilityItemActivationOutcome::Ineligible;
		return true;
	}
	if (Kind == EBattleHeldItemRuleKind::SitrusBerry
		&& static_cast<int64>(Facts.CurrentHP) * 2
			> static_cast<int64>(Facts.BaseMaximumHP))
	{
		OutResult.Outcome = EBattleAbilityItemActivationOutcome::Ineligible;
		return true;
	}

	const int32 Divisor = Kind == EBattleHeldItemRuleKind::Leftovers ? 16 : 4;
	OutResult.bApplies = true;
	OutResult.bConsumesItem = Kind == EBattleHeldItemRuleKind::SitrusBerry;
	OutResult.HealAmount = FMath::Min(
		FMath::Max(1, Facts.BaseMaximumHP / Divisor),
		Facts.BaseMaximumHP - Facts.CurrentHP);
	OutResult.Outcome = EBattleAbilityItemActivationOutcome::Applied;
	return true;
}

bool FBattleItemRules::TryEvaluateLumBerry(
	const FBattleLumBerryFacts& Facts,
	FBattleLumBerryResult& OutResult)
{
	OutResult = FBattleLumBerryResult();
	if (Facts.ItemId != GetLumBerryId())
	{
		return false;
	}

	OutResult.bValid = true;
	if (Facts.bSuppressed)
	{
		OutResult.Outcome = EBattleAbilityItemActivationOutcome::Suppressed;
		return true;
	}
	if (!Facts.bHolderAbleToBattle
		|| (!Facts.bHasMajorStatus && !Facts.bHasConfusion))
	{
		OutResult.Outcome = EBattleAbilityItemActivationOutcome::Ineligible;
		return true;
	}

	OutResult.bApplies = true;
	OutResult.bConsumesItem = true;
	OutResult.bCuresMajorStatus = Facts.bHasMajorStatus;
	OutResult.bCuresConfusion = Facts.bHasConfusion;
	OutResult.Outcome = EBattleAbilityItemActivationOutcome::Applied;
	return true;
}

bool FBattleItemRules::TryEvaluateFocusSash(
	const FBattleFocusSashFacts& Facts,
	FBattleFocusSashResult& OutResult)
{
	OutResult = FBattleFocusSashResult();
	if (Facts.ItemId != GetFocusSashId()
		|| Facts.BaseMaximumHP <= 0
		|| Facts.CurrentHP < 0
		|| Facts.CurrentHP > Facts.BaseMaximumHP
		|| Facts.IncomingDamage < 0)
	{
		return false;
	}

	OutResult.bValid = true;
	OutResult.AdjustedDamage = Facts.IncomingDamage;
	if (Facts.bSuppressed)
	{
		OutResult.Outcome = EBattleAbilityItemActivationOutcome::Suppressed;
		return true;
	}
	if (!Facts.bDirectMoveDamage
		|| Facts.bDamageTargetsSubstitute
		|| Facts.CurrentHP <= 0
		|| Facts.CurrentHP != Facts.BaseMaximumHP
		|| Facts.IncomingDamage < Facts.CurrentHP)
	{
		OutResult.Outcome = EBattleAbilityItemActivationOutcome::Ineligible;
		return true;
	}

	OutResult.bApplies = true;
	OutResult.bConsumesItem = true;
	OutResult.AdjustedDamage = Facts.CurrentHP - 1;
	OutResult.Outcome = EBattleAbilityItemActivationOutcome::Applied;
	return true;
}

bool FBattleItemRules::TryEvaluateDamageModifier(
	const FBattleItemDamageModifierFacts& Facts,
	FBattleItemDamageModifierResult& OutResult)
{
	OutResult = FBattleItemDamageModifierResult();
	const EBattleHeldItemRuleKind Kind = GetKind(Facts.ItemId);
	if ((Kind != EBattleHeldItemRuleKind::LifeOrb
			&& Kind != EBattleHeldItemRuleKind::ChoiceBand)
		|| !BattleItemPrivate::IsKnownMoveCategory(Facts.MoveCategory)
		|| (Facts.bDamagingMove && Facts.MoveCategory == EBattleMoveCategory::Status))
	{
		return false;
	}

	OutResult.bValid = true;
	OutResult.ModifierQ12 = GetNeutralModifierQ12();
	if (Facts.bSuppressed)
	{
		OutResult.Outcome = EBattleAbilityItemActivationOutcome::Suppressed;
		return true;
	}
	const bool bApplies = Facts.bDamagingMove
		&& (Kind == EBattleHeldItemRuleKind::LifeOrb
			|| Facts.MoveCategory == EBattleMoveCategory::Physical);
	if (!bApplies)
	{
		OutResult.Outcome = EBattleAbilityItemActivationOutcome::Ineligible;
		return true;
	}

	OutResult.bApplies = true;
	OutResult.ModifierQ12 = Kind == EBattleHeldItemRuleKind::LifeOrb
		? GetLifeOrbModifierQ12()
		: GetChoiceBandModifierQ12();
	OutResult.Outcome = EBattleAbilityItemActivationOutcome::Applied;
	return true;
}

bool FBattleItemRules::TryEvaluateLifeOrbRecoil(
	const FBattleLifeOrbRecoilFacts& Facts,
	FBattleLifeOrbRecoilResult& OutResult)
{
	OutResult = FBattleLifeOrbRecoilResult();
	if (Facts.ItemId != GetLifeOrbId() || Facts.BaseMaximumHP <= 0)
	{
		return false;
	}

	OutResult.bValid = true;
	if (Facts.bSuppressed)
	{
		OutResult.Outcome = EBattleAbilityItemActivationOutcome::Suppressed;
		return true;
	}
	if (!Facts.bDamagingMove
		|| !Facts.bMoveAffectedTarget
		|| !Facts.bSourceAndTargetDiffer
		|| Facts.bForcedSwitchSuppressesRecoil)
	{
		OutResult.Outcome = EBattleAbilityItemActivationOutcome::Ineligible;
		return true;
	}

	OutResult.bApplies = true;
	OutResult.RecoilDamage = FMath::Max(1, Facts.BaseMaximumHP / 10);
	OutResult.Outcome = EBattleAbilityItemActivationOutcome::Applied;
	return true;
}

bool FBattleItemRules::TryEvaluateChoiceBandMove(
	const FBattleChoiceBandMoveFacts& Facts,
	FBattleChoiceBandMoveResult& OutResult)
{
	OutResult = FBattleChoiceBandMoveResult();
	if (Facts.ItemId != GetChoiceBandId() || !Facts.SelectedMoveId.IsValid())
	{
		return false;
	}

	OutResult.bValid = true;
	if (Facts.bSuppressed)
	{
		OutResult.bMoveAllowed = true;
		OutResult.Outcome = EBattleAbilityItemActivationOutcome::Suppressed;
		return true;
	}
	if (Facts.bSelectedMoveIsStruggle)
	{
		OutResult.bMoveAllowed = true;
		OutResult.Outcome = EBattleAbilityItemActivationOutcome::Ineligible;
		return true;
	}
	if (!Facts.LockedMoveId.IsValid())
	{
		OutResult.bMoveAllowed = true;
		OutResult.bShouldEstablishLock = true;
		OutResult.LockMoveId = Facts.SelectedMoveId;
		OutResult.Outcome = EBattleAbilityItemActivationOutcome::Applied;
		return true;
	}
	if (Facts.LockedMoveId == Facts.SelectedMoveId)
	{
		OutResult.bMoveAllowed = true;
		OutResult.LockMoveId = Facts.LockedMoveId;
		OutResult.Outcome = EBattleAbilityItemActivationOutcome::Applied;
		return true;
	}

	OutResult.LockMoveId = Facts.LockedMoveId;
	OutResult.Outcome = EBattleAbilityItemActivationOutcome::AttemptedButPrevented;
	return true;
}

bool FBattleItemRules::ShouldClearChoiceBandMoveLock(
	const bool bSwitchedOut,
	const bool bItemLost,
	const bool bSuppressed)
{
	return bSwitchedOut || bItemLost || bSuppressed;
}

bool FBattleItemRules::ShouldBypassEntryHazards(
	const FItemId& ItemId,
	const bool bSuppressed)
{
	return ItemId == GetHeavyDutyBootsId() && !bSuppressed;
}

bool FBattleItemRules::IsAirBalloonAirborne(
	const FItemId& ItemId,
	const bool bSuppressed)
{
	return ItemId == GetAirBalloonId() && !bSuppressed;
}

bool FBattleItemRules::ShouldAirBalloonPreventMove(
	const FItemId& ItemId,
	const EPokemonType MoveType,
	const bool bSuppressed)
{
	return MoveType == EPokemonType::Ground
		&& IsAirBalloonAirborne(ItemId, bSuppressed);
}

bool FBattleItemRules::ShouldPopAirBalloon(
	const FItemId& ItemId,
	const bool bDamagingHitConnected,
	const bool bSuppressed)
{
	return ItemId == GetAirBalloonId()
		&& bDamagingHitConnected
		&& !bSuppressed;
}

bool FBattleItemRules::TryEvaluateQuickClawEligibility(
	const FBattleQuickClawFacts& Facts,
	FBattleQuickClawEligibilityResult& OutResult)
{
	OutResult = FBattleQuickClawEligibilityResult();
	if (Facts.ItemId != GetQuickClawId()
		|| Facts.MovePriority < -7
		|| Facts.MovePriority > 5)
	{
		return false;
	}

	OutResult.bValid = true;
	if (Facts.bSuppressed)
	{
		OutResult.Outcome = EBattleAbilityItemActivationOutcome::Suppressed;
		return true;
	}
	if (!Facts.bSelectedMoveEligible || Facts.MovePriority > 0)
	{
		OutResult.Outcome = EBattleAbilityItemActivationOutcome::Ineligible;
		return true;
	}

	OutResult.bEligible = true;
	OutResult.bConsumesRandomDraw = true;
	OutResult.Outcome = EBattleAbilityItemActivationOutcome::Applied;
	return true;
}

bool FBattleItemRules::TryResolveQuickClawDraw(
	const FBattleQuickClawFacts& Facts,
	const uint32 RawDraw,
	FBattleQuickClawDrawResult& OutResult)
{
	OutResult = FBattleQuickClawDrawResult();
	FBattleQuickClawEligibilityResult Eligibility;
	if (!TryEvaluateQuickClawEligibility(Facts, Eligibility)
		|| !Eligibility.bEligible
		|| RawDraw > GetQuickClawRollMaxInclusive())
	{
		return false;
	}

	OutResult.bValid = true;
	if (RawDraw != GetQuickClawSuccessRawDraw())
	{
		OutResult.Outcome = EBattleAbilityItemActivationOutcome::Ineligible;
		return true;
	}

	OutResult.bApplies = true;
	OutResult.FractionalPriorityTenths = GetQuickClawFractionalPriorityTenths();
	OutResult.Outcome = EBattleAbilityItemActivationOutcome::Applied;
	return true;
}

FDefinitionId FBattleItemRules::GetQuickClawActivationPurpose()
{
	return BattleItemPrivate::MakeNamedId<FDefinitionId>(
		TEXT("Rule.Item.QuickClaw.ActionOrder"));
}
