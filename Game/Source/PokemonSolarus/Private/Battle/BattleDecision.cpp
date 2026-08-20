#include "Battle/BattleDecision.h"

namespace
{
	bool IsKnownRequestKind(const EBattleDecisionRequestKind Value)
	{
		return Value == EBattleDecisionRequestKind::Action
			|| Value == EBattleDecisionRequestKind::MandatoryReplacement
			|| Value == EBattleDecisionRequestKind::ShiftResponse
			|| Value == EBattleDecisionRequestKind::Scripted;
	}

	bool IsKnownActionKind(const EBattleActionKind Value)
	{
		return Value == EBattleActionKind::Fight
			|| Value == EBattleActionKind::Bag
			|| Value == EBattleActionKind::Switch
			|| Value == EBattleActionKind::Run
			|| Value == EBattleActionKind::WildFlee
			|| Value == EBattleActionKind::Replacement
			|| Value == EBattleActionKind::ScriptedEnd
			|| Value == EBattleActionKind::Abandon;
	}

	template <typename ElementType, typename EqualType>
	bool HasDuplicates(const TArray<ElementType>& Values, EqualType Equal)
	{
		for (int32 Left = 0; Left < Values.Num(); ++Left)
		{
			for (int32 Right = Left + 1; Right < Values.Num(); ++Right)
			{
				if (Equal(Values[Left], Values[Right]))
				{
					return true;
				}
			}
		}
		return false;
	}

	bool ActiveSlotLess(const FActiveSlotId& Left, const FActiveSlotId& Right)
	{
		if (Left.GetSide() != Right.GetSide())
		{
			return static_cast<uint8>(Left.GetSide()) < static_cast<uint8>(Right.GetSide());
		}
		return static_cast<uint8>(Left.GetPosition()) < static_cast<uint8>(Right.GetPosition());
	}
}

bool FBattleDecisionRequest::TryCreate(
	const FBattleDecisionRequestSpec& Spec,
	FBattleDecisionRequest& OutRequest,
	FBattleRejection& OutRejection)
{
	OutRequest = FBattleDecisionRequest();
	OutRejection = FBattleRejection();
	if (Spec.StateVersion == 0
		|| !IsKnownRequestKind(Spec.RequestKind)
		|| !Spec.DecisionOwnerTrainerId.IsValid()
		|| !Spec.ActingBattlerId.IsValid()
		|| !Spec.ActingSlotId.IsValid()
		|| Spec.LegalActionKinds.IsEmpty())
	{
		OutRejection.Reason = EBattleRejectionReason::InvalidDecision;
		return false;
	}

	for (const EBattleActionKind Action : Spec.LegalActionKinds)
	{
		if (!IsKnownActionKind(Action))
		{
			OutRejection.Reason = EBattleRejectionReason::InvalidDecision;
			return false;
		}
	}
	for (const FMoveId& MoveId : Spec.LegalMoveIds)
	{
		if (!MoveId.IsValid())
		{
			OutRejection.Reason = EBattleRejectionReason::InvalidDecision;
			return false;
		}
	}
	for (const FPartySlotId PartySlotId : Spec.LegalSwitchPartySlots)
	{
		if (!PartySlotId.IsValid())
		{
			OutRejection.Reason = EBattleRejectionReason::InvalidDecision;
			return false;
		}
	}
	for (const FItemId& ItemId : Spec.LegalItemIds)
	{
		if (!ItemId.IsValid())
		{
			OutRejection.Reason = EBattleRejectionReason::InvalidDecision;
			return false;
		}
	}
	for (const FActiveSlotId ActiveSlotId : Spec.LegalActiveTargets)
	{
		if (!ActiveSlotId.IsValid())
		{
			OutRejection.Reason = EBattleRejectionReason::InvalidDecision;
			return false;
		}
	}
	for (const FPartySlotId PartySlotId : Spec.LegalPartyTargets)
	{
		if (!PartySlotId.IsValid())
		{
			OutRejection.Reason = EBattleRejectionReason::InvalidDecision;
			return false;
		}
	}

	if (HasDuplicates(Spec.LegalActionKinds, [](const EBattleActionKind Left, const EBattleActionKind Right) { return Left == Right; })
		|| HasDuplicates(Spec.LegalMoveIds, [](const FMoveId& Left, const FMoveId& Right) { return Left == Right; })
		|| HasDuplicates(Spec.LegalSwitchPartySlots, [](const FPartySlotId Left, const FPartySlotId Right) { return Left == Right; })
		|| HasDuplicates(Spec.LegalItemIds, [](const FItemId& Left, const FItemId& Right) { return Left == Right; })
		|| HasDuplicates(Spec.LegalActiveTargets, [](const FActiveSlotId Left, const FActiveSlotId Right) { return Left == Right; })
		|| HasDuplicates(Spec.LegalPartyTargets, [](const FPartySlotId Left, const FPartySlotId Right) { return Left == Right; }))
	{
		OutRejection.Reason = EBattleRejectionReason::InvalidDecision;
		return false;
	}

	OutRequest.bValid = true;
	OutRequest.StateVersion = Spec.StateVersion;
	OutRequest.RequestKind = Spec.RequestKind;
	OutRequest.DecisionOwnerTrainerId = Spec.DecisionOwnerTrainerId;
	OutRequest.ActingBattlerId = Spec.ActingBattlerId;
	OutRequest.ActingSlotId = Spec.ActingSlotId;
	OutRequest.LegalActionKinds = Spec.LegalActionKinds;
	OutRequest.LegalMoveIds = Spec.LegalMoveIds;
	OutRequest.LegalSwitchPartySlots = Spec.LegalSwitchPartySlots;
	OutRequest.LegalItemIds = Spec.LegalItemIds;
	OutRequest.LegalActiveTargets = Spec.LegalActiveTargets;
	OutRequest.LegalPartyTargets = Spec.LegalPartyTargets;

	OutRequest.LegalActionKinds.Sort(
		[](const EBattleActionKind Left, const EBattleActionKind Right)
		{
			return static_cast<uint8>(Left) < static_cast<uint8>(Right);
		});
	OutRequest.LegalMoveIds.Sort(
		[](const FMoveId& Left, const FMoveId& Right)
		{
			return Left.LexicalLess(Right);
		});
	OutRequest.LegalSwitchPartySlots.Sort();
	OutRequest.LegalItemIds.Sort(
		[](const FItemId& Left, const FItemId& Right)
		{
			return Left.LexicalLess(Right);
		});
	OutRequest.LegalActiveTargets.Sort(ActiveSlotLess);
	OutRequest.LegalPartyTargets.Sort();
	return true;
}

bool FBattleDecisionRequest::Allows(
	const FBattleDecision& Decision,
	FBattleRejection& OutRejection) const
{
	OutRejection = FBattleRejection();
	if (!bValid || !Decision.IsValid())
	{
		OutRejection.Reason = EBattleRejectionReason::InvalidDecision;
		return false;
	}
	if (Decision.GetStateVersion() != StateVersion)
	{
		OutRejection.Reason = EBattleRejectionReason::StaleStateVersion;
		return false;
	}
	if (Decision.GetRequestKind() != RequestKind)
	{
		OutRejection.Reason = EBattleRejectionReason::WrongRequestKind;
		return false;
	}
	if (Decision.GetDecisionOwnerTrainerId() != DecisionOwnerTrainerId)
	{
		OutRejection.Reason = EBattleRejectionReason::WrongDecisionOwner;
		OutRejection.TrainerId = Decision.GetDecisionOwnerTrainerId();
		return false;
	}
	if (Decision.GetActingBattlerId() != ActingBattlerId)
	{
		OutRejection.Reason = EBattleRejectionReason::WrongActingBattler;
		OutRejection.BattlerId = Decision.GetActingBattlerId();
		return false;
	}
	if (!LegalActionKinds.Contains(Decision.GetActionKind()))
	{
		OutRejection.Reason = EBattleRejectionReason::IllegalAction;
		return false;
	}

	switch (Decision.GetActionKind())
	{
	case EBattleActionKind::Fight:
		if (!LegalMoveIds.Contains(Decision.GetMoveId()))
		{
			OutRejection.Reason = EBattleRejectionReason::IllegalMove;
			OutRejection.MoveId = Decision.GetMoveId();
			return false;
		}
		if (!LegalActiveTargets.Contains(Decision.GetActiveTargetId()))
		{
			OutRejection.Reason = EBattleRejectionReason::IllegalTarget;
			OutRejection.ActiveSlotId = Decision.GetActiveTargetId();
			return false;
		}
		break;
	case EBattleActionKind::Switch:
	case EBattleActionKind::Replacement:
		if (!LegalSwitchPartySlots.Contains(Decision.GetSwitchPartySlotId()))
		{
			OutRejection.Reason = EBattleRejectionReason::IllegalSwitch;
			OutRejection.PartySlotId = Decision.GetSwitchPartySlotId();
			return false;
		}
		if (!LegalActiveTargets.Contains(Decision.GetActiveTargetId()))
		{
			OutRejection.Reason = EBattleRejectionReason::IllegalTarget;
			OutRejection.ActiveSlotId = Decision.GetActiveTargetId();
			return false;
		}
		break;
	case EBattleActionKind::Bag:
		if (!LegalItemIds.Contains(Decision.GetItemId()))
		{
			OutRejection.Reason = EBattleRejectionReason::IllegalItem;
			OutRejection.ItemId = Decision.GetItemId();
			return false;
		}
		if (Decision.GetItemPartyTargetId().IsValid()
			&& !LegalPartyTargets.Contains(Decision.GetItemPartyTargetId()))
		{
			OutRejection.Reason = EBattleRejectionReason::IllegalTarget;
			OutRejection.PartySlotId = Decision.GetItemPartyTargetId();
			return false;
		}
		if (Decision.GetActiveTargetId().IsValid()
			&& !LegalActiveTargets.Contains(Decision.GetActiveTargetId()))
		{
			OutRejection.Reason = EBattleRejectionReason::IllegalTarget;
			OutRejection.ActiveSlotId = Decision.GetActiveTargetId();
			return false;
		}
		break;
	default:
		break;
	}
	return true;
}

bool FBattleDecision::TryCreateSimpleAction(
	const uint64 InStateVersion,
	const EBattleDecisionRequestKind InRequestKind,
	const FTrainerId InDecisionOwnerTrainerId,
	const FBattlerId InActingBattlerId,
	const EBattleActionKind InActionKind,
	FBattleDecision& OutDecision)
{
	OutDecision = FBattleDecision();
	const bool bSimple = InActionKind == EBattleActionKind::Run
		|| InActionKind == EBattleActionKind::WildFlee
		|| InActionKind == EBattleActionKind::ScriptedEnd
		|| InActionKind == EBattleActionKind::Abandon;
	if (InStateVersion == 0
		|| !IsKnownRequestKind(InRequestKind)
		|| !InDecisionOwnerTrainerId.IsValid()
		|| !InActingBattlerId.IsValid()
		|| !bSimple)
	{
		return false;
	}

	OutDecision.bValid = true;
	OutDecision.StateVersion = InStateVersion;
	OutDecision.RequestKind = InRequestKind;
	OutDecision.DecisionOwnerTrainerId = InDecisionOwnerTrainerId;
	OutDecision.ActingBattlerId = InActingBattlerId;
	OutDecision.ActionKind = InActionKind;
	return true;
}

bool FBattleDecision::TryCreateFight(
	const uint64 InStateVersion,
	const FTrainerId InDecisionOwnerTrainerId,
	const FBattlerId InActingBattlerId,
	const FMoveId InMoveId,
	const FActiveSlotId InTarget,
	FBattleDecision& OutDecision)
{
	OutDecision = FBattleDecision();
	if (InStateVersion == 0
		|| !InDecisionOwnerTrainerId.IsValid()
		|| !InActingBattlerId.IsValid()
		|| !InMoveId.IsValid()
		|| !InTarget.IsValid())
	{
		return false;
	}
	OutDecision.bValid = true;
	OutDecision.StateVersion = InStateVersion;
	OutDecision.RequestKind = EBattleDecisionRequestKind::Action;
	OutDecision.DecisionOwnerTrainerId = InDecisionOwnerTrainerId;
	OutDecision.ActingBattlerId = InActingBattlerId;
	OutDecision.ActionKind = EBattleActionKind::Fight;
	OutDecision.MoveId = InMoveId;
	OutDecision.ActiveTargetId = InTarget;
	return true;
}

bool FBattleDecision::TryCreateSwitch(
	const uint64 InStateVersion,
	const EBattleDecisionRequestKind InRequestKind,
	const FTrainerId InDecisionOwnerTrainerId,
	const FBattlerId InActingBattlerId,
	const FPartySlotId InPartySlotId,
	const FActiveSlotId InDestination,
	FBattleDecision& OutDecision)
{
	OutDecision = FBattleDecision();
	const bool bVoluntary = InRequestKind == EBattleDecisionRequestKind::Action;
	const bool bReplacement = InRequestKind == EBattleDecisionRequestKind::MandatoryReplacement;
	if (InStateVersion == 0
		|| (!bVoluntary && !bReplacement)
		|| !InDecisionOwnerTrainerId.IsValid()
		|| !InActingBattlerId.IsValid()
		|| !InPartySlotId.IsValid()
		|| !InDestination.IsValid())
	{
		return false;
	}
	OutDecision.bValid = true;
	OutDecision.StateVersion = InStateVersion;
	OutDecision.RequestKind = InRequestKind;
	OutDecision.DecisionOwnerTrainerId = InDecisionOwnerTrainerId;
	OutDecision.ActingBattlerId = InActingBattlerId;
	OutDecision.ActionKind = bReplacement ? EBattleActionKind::Replacement : EBattleActionKind::Switch;
	OutDecision.SwitchPartySlotId = InPartySlotId;
	OutDecision.ActiveTargetId = InDestination;
	return true;
}

bool FBattleDecision::TryCreateBag(
	const uint64 InStateVersion,
	const FTrainerId InDecisionOwnerTrainerId,
	const FBattlerId InActingBattlerId,
	const FItemId InItemId,
	const FPartySlotId InPartyTarget,
	const FActiveSlotId InActiveTarget,
	FBattleDecision& OutDecision)
{
	OutDecision = FBattleDecision();
	if (InStateVersion == 0
		|| !InDecisionOwnerTrainerId.IsValid()
		|| !InActingBattlerId.IsValid()
		|| !InItemId.IsValid()
		|| (!InPartyTarget.IsValid() && !InActiveTarget.IsValid()))
	{
		return false;
	}
	OutDecision.bValid = true;
	OutDecision.StateVersion = InStateVersion;
	OutDecision.RequestKind = EBattleDecisionRequestKind::Action;
	OutDecision.DecisionOwnerTrainerId = InDecisionOwnerTrainerId;
	OutDecision.ActingBattlerId = InActingBattlerId;
	OutDecision.ActionKind = EBattleActionKind::Bag;
	OutDecision.ItemId = InItemId;
	OutDecision.ItemPartyTargetId = InPartyTarget;
	OutDecision.ActiveTargetId = InActiveTarget;
	return true;
}
