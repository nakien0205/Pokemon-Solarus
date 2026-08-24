#include "Battle/BattleEncounterPolicy.h"

#include "Battle/BattleBagItem.h"

namespace
{
	bool IsKnownEncounterKind(const EBattleEncounterKind Value)
	{
		return Value == EBattleEncounterKind::Wild
			|| Value == EBattleEncounterKind::Trainer
			|| Value == EBattleEncounterKind::Rival
			|| Value == EBattleEncounterKind::BossGym
			|| Value == EBattleEncounterKind::TutorialScripted;
	}

	bool IsKnownFormat(const EBattleFormat Value)
	{
		return Value == EBattleFormat::Single
			|| Value == EBattleFormat::Double
			|| Value == EBattleFormat::PartnerDouble;
	}

	bool IsKnownWildFleeMode(const EBattleWildFleeMode Value)
	{
		return Value == EBattleWildFleeMode::Disabled
			|| Value == EBattleWildFleeMode::Never
			|| Value == EBattleWildFleeMode::Always
			|| Value == EBattleWildFleeMode::Chance;
	}

	EBattleSelectorProfileTag GetSelectorProfileTag(
		const EBattleEncounterKind EncounterKind,
		const EBattleTrainerRole Role)
	{
		if (Role == EBattleTrainerRole::Partner)
		{
			return EBattleSelectorProfileTag::Partner;
		}
		if (Role == EBattleTrainerRole::Player)
		{
			return EBattleSelectorProfileTag::None;
		}

		switch (EncounterKind)
		{
		case EBattleEncounterKind::Wild:
			return EBattleSelectorProfileTag::Wild;
		case EBattleEncounterKind::Trainer:
			return EBattleSelectorProfileTag::Basic;
		case EBattleEncounterKind::Rival:
			return EBattleSelectorProfileTag::Skilled;
		case EBattleEncounterKind::BossGym:
			return EBattleSelectorProfileTag::Boss;
		case EBattleEncounterKind::TutorialScripted:
			return EBattleSelectorProfileTag::Tutorial;
		default:
			return EBattleSelectorProfileTag::None;
		}
	}

	bool HasExplicitRevive(const FBattleTrainerSetup& Trainer)
	{
		const FItemId ReviveId = FBattleBagItemRules::GetReviveId();
		return Trainer.Bag.ContainsByPredicate(
			[ReviveId](const FBattleBagItemCount& Item)
			{
				return Item.ItemId == ReviveId && Item.Count > 0;
			});
	}
}

const FBattleTrainerEncounterPolicy* FBattleCompiledEncounterPolicies::FindTrainerPolicy(
	const FTrainerId TrainerId) const
{
	return TrainerPolicies.FindByPredicate(
		[TrainerId](const FBattleTrainerEncounterPolicy& Policy)
		{
			return Policy.TrainerId == TrainerId;
		});
}

bool FBattleEncounterPolicyCompiler::TryCompile(
	const FBattleSetup& Setup,
	FBattleCompiledEncounterPolicies& OutPolicies,
	EBattleEncounterPolicyError& OutError)
{
	OutPolicies = FBattleCompiledEncounterPolicies();
	OutError = EBattleEncounterPolicyError::None;

	auto Fail = [&OutError](const EBattleEncounterPolicyError Error)
	{
		OutError = Error;
		return false;
	};

	if (!Setup.IsValid())
	{
		return Fail(EBattleEncounterPolicyError::InvalidSetup);
	}
	if (!IsKnownEncounterKind(Setup.GetEncounterKind()))
	{
		return Fail(EBattleEncounterPolicyError::UnsupportedEncounterKind);
	}
	if (!IsKnownFormat(Setup.GetFormat()))
	{
		return Fail(EBattleEncounterPolicyError::UnsupportedFormat);
	}

	const FBattleEncounterPolicies& Source = Setup.GetPolicies();
	if (Setup.GetEncounterKind() != EBattleEncounterKind::Wild
		&& (Source.bRunAllowed || Source.bCaptureAllowed))
	{
		return Fail(EBattleEncounterPolicyError::IncompatibleCommandPolicy);
	}
	if (!IsKnownWildFleeMode(Source.WildFleeMode))
	{
		return Fail(EBattleEncounterPolicyError::InvalidWildFleePolicy);
	}
	if (Setup.GetEncounterKind() != EBattleEncounterKind::Wild
		&& Source.WildFleeMode != EBattleWildFleeMode::Disabled)
	{
		return Fail(EBattleEncounterPolicyError::InvalidWildFleePolicy);
	}
	if (Source.WildFleeMode == EBattleWildFleeMode::Chance)
	{
		if (Source.WildFleeNumerator == 0
			|| Source.WildFleeNumerator >= Source.WildFleeDenominator)
		{
			return Fail(EBattleEncounterPolicyError::InvalidWildFleePolicy);
		}
	}
	else if (Source.WildFleeNumerator != 0 || Source.WildFleeDenominator != 0)
	{
		return Fail(EBattleEncounterPolicyError::InvalidWildFleePolicy);
	}
	if (Source.bShiftPromptEligible
		&& (Setup.GetEncounterKind() == EBattleEncounterKind::Wild
			|| Setup.GetFormat() != EBattleFormat::Single))
	{
		return Fail(EBattleEncounterPolicyError::IncompatibleCommandPolicy);
	}

	int32 PlayerCount = 0;
	int32 PartnerCount = 0;
	int32 OpponentCount = 0;
	for (const FBattleTrainerSetup& Trainer : Setup.GetTrainers())
	{
		PlayerCount += Trainer.Role == EBattleTrainerRole::Player ? 1 : 0;
		PartnerCount += Trainer.Role == EBattleTrainerRole::Partner ? 1 : 0;
		OpponentCount += Trainer.Role == EBattleTrainerRole::Opponent ? 1 : 0;
	}
	const int32 ExpectedPartnerCount = Setup.GetFormat() == EBattleFormat::PartnerDouble ? 1 : 0;
	const int32 ExpectedTrainerCount = Setup.GetFormat() == EBattleFormat::PartnerDouble ? 3 : 2;
	if (Setup.GetTrainers().Num() != ExpectedTrainerCount
		|| PlayerCount != 1
		|| PartnerCount != ExpectedPartnerCount
		|| OpponentCount != 1)
	{
		return Fail(EBattleEncounterPolicyError::InvalidTrainerShape);
	}
	if (Setup.GetEncounterKind() == EBattleEncounterKind::Wild
		&& Setup.GetTrainers().ContainsByPredicate(
			[](const FBattleTrainerSetup& Trainer)
			{
				return Trainer.Role == EBattleTrainerRole::Opponent
					&& !Trainer.Bag.IsEmpty();
			}))
	{
		return Fail(EBattleEncounterPolicyError::IncompatibleCommandPolicy);
	}

	OutPolicies.EncounterKind = Setup.GetEncounterKind();
	OutPolicies.Format = Setup.GetFormat();
	OutPolicies.MaximumActiveBattlersPerSide = Setup.GetFormat() == EBattleFormat::Single ? 1 : 2;
	OutPolicies.MaximumPartySize = FPartySlotId::PartySize;
	OutPolicies.bRunAllowed = Source.bRunAllowed;
	OutPolicies.bCaptureAllowed = Source.bCaptureAllowed;
	OutPolicies.bBagAllowed = Source.bBagAllowed;
	OutPolicies.BattleStyle = Source.bShiftPromptEligible
		? EBattleStylePolicy::Shift
		: EBattleStylePolicy::Set;
	OutPolicies.ReinforcementPolicy = Setup.GetEncounterKind() == EBattleEncounterKind::Wild
		&& Setup.GetFormat() != EBattleFormat::Single
		? EBattleReinforcementPolicy::OneWildRightSlot
		: EBattleReinforcementPolicy::Disabled;
	OutPolicies.bWildFleeConfigured = Source.WildFleeMode != EBattleWildFleeMode::Disabled;
	OutPolicies.WildFleeMode = Source.WildFleeMode;
	OutPolicies.WildFleeNumerator = Source.WildFleeNumerator;
	OutPolicies.WildFleeDenominator = Source.WildFleeDenominator;
	OutPolicies.bScriptedEndingAllowed =
		Setup.GetEncounterKind() == EBattleEncounterKind::TutorialScripted;
	OutPolicies.bSeparatePartnerOwnership = Setup.GetFormat() == EBattleFormat::PartnerDouble;

	for (const FBattleTrainerSetup& Trainer : Setup.GetTrainers())
	{
		FBattleTrainerEncounterPolicy& Policy = OutPolicies.TrainerPolicies.AddDefaulted_GetRef();
		Policy.TrainerId = Trainer.TrainerId;
		Policy.Role = Trainer.Role;
		Policy.Controller = Trainer.Controller;
		Policy.SelectorProfileId = Trainer.SelectorProfileId;
		Policy.SelectorProfileTag = GetSelectorProfileTag(Setup.GetEncounterKind(), Trainer.Role);
		Policy.bMayUseBag = Source.bBagAllowed
			&& !(Setup.GetEncounterKind() == EBattleEncounterKind::Wild
				&& Trainer.Role == EBattleTrainerRole::Opponent);
		Policy.bMayRun = Source.bRunAllowed && Trainer.Role == EBattleTrainerRole::Player;
		Policy.bMayCapture = Source.bCaptureAllowed && Trainer.Role == EBattleTrainerRole::Player;
		Policy.bPartnerOwnsSeparatePartyAndBag =
			Setup.GetFormat() == EBattleFormat::PartnerDouble
			&& Trainer.Role == EBattleTrainerRole::Partner;
		Policy.bMayUseRevive = Policy.bMayUseBag
			&& (Trainer.Role != EBattleTrainerRole::Opponent
				|| (Setup.GetEncounterKind() == EBattleEncounterKind::BossGym
					&& HasExplicitRevive(Trainer)));
	}

	OutPolicies.bValid = true;
	return true;
}
