#include "Battle/BattleSwitching.h"

namespace
{
	bool IsKnownSwitchKind(const EBattleSwitchKind Kind)
	{
		return Kind == EBattleSwitchKind::Voluntary
			|| Kind == EBattleSwitchKind::Forced
			|| Kind == EBattleSwitchKind::Pivot
			|| Kind == EBattleSwitchKind::Replacement;
	}

	bool IsKnownTransferPolicy(const EBattleSwitchStateTransferPolicy Policy)
	{
		return Policy == EBattleSwitchStateTransferPolicy::ClearTransient
			|| Policy == EBattleSwitchStateTransferPolicy::BatonPassLike;
	}

	EBattleSwitchBlockReason CandidateReason(
		const FBattleSwitchCandidateFacts& Candidate,
		const FTrainerId ActingTrainerId)
	{
		if (!Candidate.bOccupied)
		{
			return EBattleSwitchBlockReason::EmptyPartySlot;
		}
		if (Candidate.TrainerId != ActingTrainerId)
		{
			return EBattleSwitchBlockReason::WrongOwner;
		}
		if (Candidate.bAlreadyActive)
		{
			return EBattleSwitchBlockReason::AlreadyActive;
		}
		if (Candidate.bRemoved)
		{
			return EBattleSwitchBlockReason::Removed;
		}
		if (Candidate.bCaptured)
		{
			return EBattleSwitchBlockReason::Captured;
		}
		if (Candidate.bEgg)
		{
			return EBattleSwitchBlockReason::Egg;
		}
		if (Candidate.bFainted)
		{
			return EBattleSwitchBlockReason::Fainted;
		}
		if (Candidate.bAlreadyReserved)
		{
			return EBattleSwitchBlockReason::AlreadyReserved;
		}
		return EBattleSwitchBlockReason::None;
	}

	bool CandidateFactsAreValid(const FBattleSwitchCandidateFacts& Candidate)
	{
		if (!Candidate.PartySlotId.IsValid())
		{
			return false;
		}
		if (Candidate.bOccupied)
		{
			return Candidate.TrainerId.IsValid() && Candidate.BattlerId.IsValid();
		}
		return !Candidate.TrainerId.IsValid()
			&& !Candidate.BattlerId.IsValid()
			&& !Candidate.bAlreadyActive
			&& !Candidate.bFainted
			&& !Candidate.bEgg
			&& !Candidate.bCaptured
			&& !Candidate.bRemoved
			&& !Candidate.bAlreadyReserved;
	}
}

bool FBattleSwitchResolver::TryBuildLegality(
	const FBattleSwitchLegalitySpec& Spec,
	FBattleSwitchLegalityResult& OutResult)
{
	OutResult = FBattleSwitchLegalityResult();
	const bool bActorlessReplacement = Spec.Kind == EBattleSwitchKind::Replacement;
	if (!IsKnownSwitchKind(Spec.Kind)
		|| !Spec.ActingTrainerId.IsValid()
		|| (bActorlessReplacement
			? Spec.ActingBattlerId.IsValid()
			: !Spec.ActingBattlerId.IsValid())
		|| !Spec.ActiveSlotId.IsValid()
		|| !IsKnownTransferPolicy(Spec.TransferPolicy)
		|| Spec.Candidates.IsEmpty()
		|| Spec.Candidates.Num() > FPartySlotId::PartySize
		|| (Spec.Kind == EBattleSwitchKind::Voluntary
			&& !Spec.Blockers.bEncounterPolicyAllows
			&& !Spec.Blockers.EncounterPolicyRuleId.IsValid())
		|| (Spec.Kind == EBattleSwitchKind::Voluntary
			&& Spec.Blockers.bTrapped
			&& !Spec.Blockers.TrappingRuleId.IsValid()))
	{
		return false;
	}

	TArray<FBattleSwitchCandidateFacts> Candidates = Spec.Candidates;
	Candidates.Sort(
		[](const FBattleSwitchCandidateFacts& Left, const FBattleSwitchCandidateFacts& Right)
		{
			return Left.PartySlotId < Right.PartySlotId;
		});
	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		if (!CandidateFactsAreValid(Candidates[Index])
			|| (Index > 0 && Candidates[Index - 1].PartySlotId == Candidates[Index].PartySlotId))
		{
			return false;
		}
	}

	OutResult.bValid = true;
	OutResult.Kind = Spec.Kind;
	if (Spec.TransferPolicy == EBattleSwitchStateTransferPolicy::BatonPassLike)
	{
		OutResult.bBlocked = true;
		OutResult.BlockReason = EBattleSwitchBlockReason::UnsupportedTransferPolicy;
	}
	else if (Spec.Kind == EBattleSwitchKind::Voluntary
		&& !Spec.Blockers.bEncounterPolicyAllows)
	{
		OutResult.bBlocked = true;
		OutResult.BlockReason = EBattleSwitchBlockReason::EncounterPolicy;
		OutResult.BlockingRuleId = Spec.Blockers.EncounterPolicyRuleId;
	}
	else if (Spec.Kind == EBattleSwitchKind::Voluntary && Spec.Blockers.bTrapped)
	{
		OutResult.bBlocked = true;
		OutResult.BlockReason = EBattleSwitchBlockReason::Trapped;
		OutResult.BlockingRuleId = Spec.Blockers.TrappingRuleId;
	}

	OutResult.Candidates.Reserve(Candidates.Num());
	for (const FBattleSwitchCandidateFacts& Candidate : Candidates)
	{
		FBattleSwitchCandidateResult CandidateResult;
		CandidateResult.PartySlotId = Candidate.PartySlotId;
		CandidateResult.BattlerId = Candidate.BattlerId;
		CandidateResult.Reason = CandidateReason(Candidate, Spec.ActingTrainerId);
		if (CandidateResult.Reason == EBattleSwitchBlockReason::None && OutResult.bBlocked)
		{
			CandidateResult.Reason = OutResult.BlockReason;
		}
		CandidateResult.bLegal = CandidateResult.Reason == EBattleSwitchBlockReason::None;
		if (CandidateResult.bLegal)
		{
			OutResult.LegalPartySlots.Add(CandidateResult.PartySlotId);
		}
		OutResult.Candidates.Add(MoveTemp(CandidateResult));
	}
	return true;
}

bool FBattleSwitchResolver::TryResolve(
	const FBattleSwitchLegalityResult& Legality,
	const FBattleSwitchSelectionSpec& Spec,
	IBattleRandom& Random,
	FBattleSwitchResolution& OutResolution)
{
	OutResolution = FBattleSwitchResolution();
	if (!Legality.bValid || !IsKnownSwitchKind(Legality.Kind))
	{
		return false;
	}

	OutResolution.bValid = true;
	if (Legality.bBlocked)
	{
		OutResolution.Reason = Legality.BlockReason;
		return true;
	}
	if (Legality.LegalPartySlots.IsEmpty())
	{
		OutResolution.Reason = EBattleSwitchBlockReason::NoLegalReserve;
		return true;
	}

	FPartySlotId SelectedPartySlotId;
	if (Legality.Kind == EBattleSwitchKind::Forced)
	{
		if (!Spec.RandomContext.IsValid())
		{
			OutResolution = FBattleSwitchResolution();
			return false;
		}
		FBattleRandomDraw Draw;
		if (!Random.TryDrawUniform(
			0,
			static_cast<uint32>(Legality.LegalPartySlots.Num() - 1),
			Spec.RandomContext,
			Draw))
		{
			OutResolution = FBattleSwitchResolution();
			return false;
		}
		SelectedPartySlotId = Legality.LegalPartySlots[Draw.Result];
		OutResolution.RandomDraw = Draw;
	}
	else
	{
		if (!Spec.RequestedPartySlotId.IsValid()
			|| !Legality.LegalPartySlots.Contains(Spec.RequestedPartySlotId))
		{
			OutResolution.Reason = EBattleSwitchBlockReason::IllegalRequestedSlot;
			return true;
		}
		SelectedPartySlotId = Spec.RequestedPartySlotId;
	}

	const FBattleSwitchCandidateResult* Selected = Legality.Candidates.FindByPredicate(
		[SelectedPartySlotId](const FBattleSwitchCandidateResult& Candidate)
		{
			return Candidate.bLegal && Candidate.PartySlotId == SelectedPartySlotId;
		});
	if (Selected == nullptr || !Selected->BattlerId.IsValid())
	{
		OutResolution = FBattleSwitchResolution();
		return false;
	}

	OutResolution.bHasSelection = true;
	OutResolution.Reason = EBattleSwitchBlockReason::None;
	OutResolution.SelectedPartySlotId = Selected->PartySlotId;
	OutResolution.SelectedBattlerId = Selected->BattlerId;
	return true;
}

FDefinitionId FBattleSwitchResolver::GetForcedSelectionRulePurpose()
{
	FDefinitionId Purpose;
	const bool bCreated = FDefinitionId::TryCreate(
		FName(TEXT("Battle.Switch.ForcedReserve")),
		Purpose);
	check(bCreated);
	return Purpose;
}
