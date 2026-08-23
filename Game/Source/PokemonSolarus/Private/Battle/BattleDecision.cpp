#include "Battle/BattleDecision.h"

namespace
{
	bool IsKnownRequestKind(const EBattleDecisionRequestKind Value)
	{
		return Value == EBattleDecisionRequestKind::Action
			|| Value == EBattleDecisionRequestKind::MandatoryReplacement
			|| Value == EBattleDecisionRequestKind::ShiftResponse
			|| Value == EBattleDecisionRequestKind::Scripted
			|| Value == EBattleDecisionRequestKind::PivotSwitch;
	}

	bool IsKnownBattleDecisionActionKind(const EBattleActionKind Value)
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

	bool BattleDecisionActiveSlotLess(const FActiveSlotId& Left, const FActiveSlotId& Right)
	{
		if (Left.GetSide() != Right.GetSide())
		{
			return static_cast<uint8>(Left.GetSide()) < static_cast<uint8>(Right.GetSide());
		}
		return static_cast<uint8>(Left.GetPosition()) < static_cast<uint8>(Right.GetPosition());
	}

	bool IsKnownOptionKind(const EBattleDecisionOptionKind Value)
	{
		return Value == EBattleDecisionOptionKind::Action
			|| Value == EBattleDecisionOptionKind::Move
			|| Value == EBattleDecisionOptionKind::SwitchPartySlot
			|| Value == EBattleDecisionOptionKind::Item
			|| Value == EBattleDecisionOptionKind::ActiveTarget
			|| Value == EBattleDecisionOptionKind::PartyTarget;
	}

	bool IsKnownUnavailableReason(const EBattleOptionUnavailableReason Value)
	{
		return Value >= EBattleOptionUnavailableReason::NoPP
			&& Value <= EBattleOptionUnavailableReason::ChoiceLocked;
	}

	bool IsUnavailableOptionValid(const FBattleUnavailableDecisionOption& Option)
	{
		if (!IsKnownOptionKind(Option.Kind) || !IsKnownUnavailableReason(Option.Reason))
		{
			return false;
		}
		if (Option.Reason == EBattleOptionUnavailableReason::ChoiceLocked
			&& Option.Kind != EBattleDecisionOptionKind::Move)
		{
			return false;
		}

		switch (Option.Kind)
		{
		case EBattleDecisionOptionKind::Action:
			return IsKnownBattleDecisionActionKind(Option.ActionKind)
				&& !Option.MoveId.IsValid()
				&& !Option.PartySlotId.IsValid()
				&& !Option.ItemId.IsValid()
				&& !Option.ActiveSlotId.IsValid();
		case EBattleDecisionOptionKind::Move:
			return Option.MoveId.IsValid()
				&& !Option.PartySlotId.IsValid()
				&& !Option.ItemId.IsValid()
				&& !Option.ActiveSlotId.IsValid();
		case EBattleDecisionOptionKind::SwitchPartySlot:
		case EBattleDecisionOptionKind::PartyTarget:
			return Option.PartySlotId.IsValid()
				&& !Option.MoveId.IsValid()
				&& !Option.ItemId.IsValid()
				&& !Option.ActiveSlotId.IsValid();
		case EBattleDecisionOptionKind::Item:
			return Option.ItemId.IsValid()
				&& !Option.MoveId.IsValid()
				&& !Option.PartySlotId.IsValid()
				&& !Option.ActiveSlotId.IsValid();
		case EBattleDecisionOptionKind::ActiveTarget:
			return Option.ActiveSlotId.IsValid()
				&& !Option.MoveId.IsValid()
				&& !Option.PartySlotId.IsValid()
				&& !Option.ItemId.IsValid();
		default:
			return false;
		}
	}

	bool UnavailableOptionLess(
		const FBattleUnavailableDecisionOption& Left,
		const FBattleUnavailableDecisionOption& Right)
	{
		if (Left.Kind != Right.Kind)
		{
			return static_cast<uint8>(Left.Kind) < static_cast<uint8>(Right.Kind);
		}
		switch (Left.Kind)
		{
		case EBattleDecisionOptionKind::Action:
			return static_cast<uint8>(Left.ActionKind) < static_cast<uint8>(Right.ActionKind);
		case EBattleDecisionOptionKind::Move:
			return Left.MoveId.LexicalLess(Right.MoveId);
		case EBattleDecisionOptionKind::SwitchPartySlot:
		case EBattleDecisionOptionKind::PartyTarget:
			return Left.PartySlotId < Right.PartySlotId;
		case EBattleDecisionOptionKind::Item:
			return Left.ItemId.LexicalLess(Right.ItemId);
		case EBattleDecisionOptionKind::ActiveTarget:
			return BattleDecisionActiveSlotLess(Left.ActiveSlotId, Right.ActiveSlotId);
		default:
			return static_cast<uint8>(Left.Reason) < static_cast<uint8>(Right.Reason);
		}
	}
}

bool FBattleDecisionRequest::TryCreate(
	const FBattleDecisionRequestSpec& Spec,
	FBattleDecisionRequest& OutRequest,
	FBattleRejection& OutRejection)
{
	OutRequest = FBattleDecisionRequest();
	OutRejection = FBattleRejection();
	const bool bActorlessReplacement =
		Spec.RequestKind == EBattleDecisionRequestKind::MandatoryReplacement;
	if (Spec.StateVersion == 0
		|| !IsKnownRequestKind(Spec.RequestKind)
		|| !Spec.DecisionOwnerTrainerId.IsValid()
		|| (bActorlessReplacement
			? Spec.ActingBattlerId.IsValid()
			: !Spec.ActingBattlerId.IsValid())
		|| !Spec.ActingSlotId.IsValid()
		|| Spec.LegalActionKinds.IsEmpty())
	{
		OutRejection.Reason = EBattleRejectionReason::InvalidDecision;
		return false;
	}

	for (const EBattleActionKind Action : Spec.LegalActionKinds)
	{
		if (!IsKnownBattleDecisionActionKind(Action))
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
	for (const FMoveId& MoveId : Spec.AutomaticallyTargetedMoveIds)
	{
		if (!MoveId.IsValid() || !Spec.LegalMoveIds.Contains(MoveId))
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
	for (const FBattleMoveTargetOption& Option : Spec.LegalMoveTargets)
	{
		if (!Option.MoveId.IsValid()
			|| !Option.ActiveSlotId.IsValid()
			|| !Spec.LegalMoveIds.Contains(Option.MoveId)
			|| !Spec.LegalActiveTargets.Contains(Option.ActiveSlotId))
		{
			OutRejection.Reason = EBattleRejectionReason::InvalidDecision;
			return false;
		}
	}
	for (const FBattleItemPartyTargetOption& Option : Spec.LegalItemPartyTargets)
	{
		if (!Option.ItemId.IsValid()
			|| !Option.PartySlotId.IsValid()
			|| !Spec.LegalItemIds.Contains(Option.ItemId)
			|| !Spec.LegalPartyTargets.Contains(Option.PartySlotId))
		{
			OutRejection.Reason = EBattleRejectionReason::InvalidDecision;
			return false;
		}
	}
	for (const FBattleItemActiveTargetOption& Option : Spec.LegalItemActiveTargets)
	{
		if (!Option.ItemId.IsValid()
			|| !Option.ActiveSlotId.IsValid()
			|| !Spec.LegalItemIds.Contains(Option.ItemId)
			|| !Spec.LegalActiveTargets.Contains(Option.ActiveSlotId))
		{
			OutRejection.Reason = EBattleRejectionReason::InvalidDecision;
			return false;
		}
	}
	for (const FBattleUnavailableDecisionOption& Option : Spec.UnavailableOptions)
	{
		if (!IsUnavailableOptionValid(Option))
		{
			OutRejection.Reason = EBattleRejectionReason::InvalidDecision;
			return false;
		}
	}

	if (HasDuplicates(Spec.LegalActionKinds, [](const EBattleActionKind Left, const EBattleActionKind Right) { return Left == Right; })
		|| HasDuplicates(Spec.LegalMoveIds, [](const FMoveId& Left, const FMoveId& Right) { return Left == Right; })
		|| HasDuplicates(Spec.AutomaticallyTargetedMoveIds, [](const FMoveId& Left, const FMoveId& Right) { return Left == Right; })
		|| HasDuplicates(Spec.LegalSwitchPartySlots, [](const FPartySlotId Left, const FPartySlotId Right) { return Left == Right; })
		|| HasDuplicates(Spec.LegalItemIds, [](const FItemId& Left, const FItemId& Right) { return Left == Right; })
		|| HasDuplicates(Spec.LegalActiveTargets, [](const FActiveSlotId Left, const FActiveSlotId Right) { return Left == Right; })
		|| HasDuplicates(Spec.LegalPartyTargets, [](const FPartySlotId Left, const FPartySlotId Right) { return Left == Right; })
		|| HasDuplicates(
			Spec.LegalMoveTargets,
			[](const FBattleMoveTargetOption& Left, const FBattleMoveTargetOption& Right)
			{
				return Left.MoveId == Right.MoveId && Left.ActiveSlotId == Right.ActiveSlotId;
			})
		|| HasDuplicates(
			Spec.LegalItemPartyTargets,
			[](const FBattleItemPartyTargetOption& Left, const FBattleItemPartyTargetOption& Right)
			{
				return Left.ItemId == Right.ItemId && Left.PartySlotId == Right.PartySlotId;
			})
		|| HasDuplicates(
			Spec.LegalItemActiveTargets,
			[](const FBattleItemActiveTargetOption& Left, const FBattleItemActiveTargetOption& Right)
			{
				return Left.ItemId == Right.ItemId && Left.ActiveSlotId == Right.ActiveSlotId;
			})
		|| HasDuplicates(
			Spec.UnavailableOptions,
			[](const FBattleUnavailableDecisionOption& Left, const FBattleUnavailableDecisionOption& Right)
			{
				return Left.Kind == Right.Kind
					&& Left.ActionKind == Right.ActionKind
					&& Left.MoveId == Right.MoveId
					&& Left.PartySlotId == Right.PartySlotId
					&& Left.ItemId == Right.ItemId
					&& Left.ActiveSlotId == Right.ActiveSlotId;
			}))
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
	OutRequest.AutomaticallyTargetedMoveIds = Spec.AutomaticallyTargetedMoveIds;
	OutRequest.LegalSwitchPartySlots = Spec.LegalSwitchPartySlots;
	OutRequest.LegalItemIds = Spec.LegalItemIds;
	OutRequest.LegalActiveTargets = Spec.LegalActiveTargets;
	OutRequest.LegalPartyTargets = Spec.LegalPartyTargets;
	OutRequest.LegalMoveTargets = Spec.LegalMoveTargets;
	OutRequest.LegalItemPartyTargets = Spec.LegalItemPartyTargets;
	OutRequest.LegalItemActiveTargets = Spec.LegalItemActiveTargets;
	OutRequest.UnavailableOptions = Spec.UnavailableOptions;

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
	OutRequest.AutomaticallyTargetedMoveIds.Sort(
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
	OutRequest.LegalActiveTargets.Sort(BattleDecisionActiveSlotLess);
	OutRequest.LegalPartyTargets.Sort();
	OutRequest.LegalMoveTargets.Sort(
		[](const FBattleMoveTargetOption& Left, const FBattleMoveTargetOption& Right)
		{
			return Left.MoveId == Right.MoveId
				? BattleDecisionActiveSlotLess(Left.ActiveSlotId, Right.ActiveSlotId)
				: Left.MoveId.LexicalLess(Right.MoveId);
		});
	OutRequest.LegalItemPartyTargets.Sort(
		[](const FBattleItemPartyTargetOption& Left, const FBattleItemPartyTargetOption& Right)
		{
			return Left.ItemId == Right.ItemId
				? Left.PartySlotId < Right.PartySlotId
				: Left.ItemId.LexicalLess(Right.ItemId);
		});
	OutRequest.LegalItemActiveTargets.Sort(
		[](const FBattleItemActiveTargetOption& Left, const FBattleItemActiveTargetOption& Right)
		{
			return Left.ItemId == Right.ItemId
				? BattleDecisionActiveSlotLess(Left.ActiveSlotId, Right.ActiveSlotId)
				: Left.ItemId.LexicalLess(Right.ItemId);
		});
	OutRequest.UnavailableOptions.Sort(UnavailableOptionLess);
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
		if (AutomaticallyTargetedMoveIds.Contains(Decision.GetMoveId()))
		{
			if (Decision.GetActiveTargetId().IsValid())
			{
				OutRejection.Reason = EBattleRejectionReason::IllegalTarget;
				OutRejection.ActiveSlotId = Decision.GetActiveTargetId();
				return false;
			}
		}
		else if (!LegalActiveTargets.Contains(Decision.GetActiveTargetId())
			|| !LegalMoveTargets.ContainsByPredicate(
				[&Decision](const FBattleMoveTargetOption& Option)
				{
					return Option.MoveId == Decision.GetMoveId()
						&& Option.ActiveSlotId == Decision.GetActiveTargetId();
				}))
		{
			OutRejection.Reason = EBattleRejectionReason::IllegalTarget;
			OutRejection.ActiveSlotId = Decision.GetActiveTargetId();
			return false;
		}
		break;
	case EBattleActionKind::Switch:
	case EBattleActionKind::Replacement:
		if (RequestKind == EBattleDecisionRequestKind::ShiftResponse
			&& !Decision.GetSwitchPartySlotId().IsValid()
			&& !Decision.GetActiveTargetId().IsValid())
		{
			return true;
		}
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
			&& (!LegalPartyTargets.Contains(Decision.GetItemPartyTargetId())
				|| (!LegalItemPartyTargets.IsEmpty()
					&& !LegalItemPartyTargets.ContainsByPredicate(
						[&Decision](const FBattleItemPartyTargetOption& Option)
						{
							return Option.ItemId == Decision.GetItemId()
								&& Option.PartySlotId == Decision.GetItemPartyTargetId();
						}))))
		{
			OutRejection.Reason = EBattleRejectionReason::IllegalTarget;
			OutRejection.PartySlotId = Decision.GetItemPartyTargetId();
			return false;
		}
		if (Decision.GetActiveTargetId().IsValid()
			&& (!LegalActiveTargets.Contains(Decision.GetActiveTargetId())
				|| (!LegalItemActiveTargets.IsEmpty()
					&& !LegalItemActiveTargets.ContainsByPredicate(
						[&Decision](const FBattleItemActiveTargetOption& Option)
						{
							return Option.ItemId == Decision.GetItemId()
								&& Option.ActiveSlotId == Decision.GetActiveTargetId();
						}))))
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

bool FBattleDecision::TryCreateAutomaticallyTargetedFight(
	const uint64 InStateVersion,
	const FTrainerId InDecisionOwnerTrainerId,
	const FBattlerId InActingBattlerId,
	const FMoveId InMoveId,
	FBattleDecision& OutDecision)
{
	OutDecision = FBattleDecision();
	if (InStateVersion == 0
		|| !InDecisionOwnerTrainerId.IsValid()
		|| !InActingBattlerId.IsValid()
		|| !InMoveId.IsValid())
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
	const bool bPivot = InRequestKind == EBattleDecisionRequestKind::PivotSwitch;
	if (InStateVersion == 0
		|| (!bVoluntary && !bPivot)
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
	OutDecision.ActionKind = EBattleActionKind::Switch;
	OutDecision.SwitchPartySlotId = InPartySlotId;
	OutDecision.ActiveTargetId = InDestination;
	return true;
}

bool FBattleDecision::TryCreateReplacement(
	const uint64 InStateVersion,
	const FTrainerId InDecisionOwnerTrainerId,
	const FPartySlotId InPartySlotId,
	const FActiveSlotId InDestination,
	FBattleDecision& OutDecision)
{
	OutDecision = FBattleDecision();
	if (InStateVersion == 0
		|| !InDecisionOwnerTrainerId.IsValid()
		|| !InPartySlotId.IsValid()
		|| !InDestination.IsValid())
	{
		return false;
	}
	OutDecision.bValid = true;
	OutDecision.StateVersion = InStateVersion;
	OutDecision.RequestKind = EBattleDecisionRequestKind::MandatoryReplacement;
	OutDecision.DecisionOwnerTrainerId = InDecisionOwnerTrainerId;
	OutDecision.ActionKind = EBattleActionKind::Replacement;
	OutDecision.SwitchPartySlotId = InPartySlotId;
	OutDecision.ActiveTargetId = InDestination;
	return true;
}

bool FBattleDecision::TryCreateShiftSwitch(
	const uint64 InStateVersion,
	const FTrainerId InDecisionOwnerTrainerId,
	const FBattlerId InActingBattlerId,
	const FPartySlotId InPartySlotId,
	const FActiveSlotId InDestination,
	FBattleDecision& OutDecision)
{
	OutDecision = FBattleDecision();
	if (InStateVersion == 0
		|| !InDecisionOwnerTrainerId.IsValid()
		|| !InActingBattlerId.IsValid()
		|| !InPartySlotId.IsValid()
		|| !InDestination.IsValid())
	{
		return false;
	}
	OutDecision.bValid = true;
	OutDecision.StateVersion = InStateVersion;
	OutDecision.RequestKind = EBattleDecisionRequestKind::ShiftResponse;
	OutDecision.DecisionOwnerTrainerId = InDecisionOwnerTrainerId;
	OutDecision.ActingBattlerId = InActingBattlerId;
	OutDecision.ActionKind = EBattleActionKind::Switch;
	OutDecision.SwitchPartySlotId = InPartySlotId;
	OutDecision.ActiveTargetId = InDestination;
	return true;
}

bool FBattleDecision::TryCreateShiftDecline(
	const uint64 InStateVersion,
	const FTrainerId InDecisionOwnerTrainerId,
	const FBattlerId InActingBattlerId,
	FBattleDecision& OutDecision)
{
	OutDecision = FBattleDecision();
	if (InStateVersion == 0
		|| !InDecisionOwnerTrainerId.IsValid()
		|| !InActingBattlerId.IsValid())
	{
		return false;
	}
	OutDecision.bValid = true;
	OutDecision.StateVersion = InStateVersion;
	OutDecision.RequestKind = EBattleDecisionRequestKind::ShiftResponse;
	OutDecision.DecisionOwnerTrainerId = InDecisionOwnerTrainerId;
	OutDecision.ActingBattlerId = InActingBattlerId;
	OutDecision.ActionKind = EBattleActionKind::Switch;
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

bool FBattleDecisionBatch::TryCreate(
	const FBattleDecisionBatchSpec& Spec,
	FBattleDecisionBatch& OutBatch,
	FBattleRejection& OutRejection)
{
	OutBatch = FBattleDecisionBatch();
	OutRejection = FBattleRejection();
	if (Spec.StateVersion == 0
		|| !IsKnownRequestKind(Spec.RequestKind)
		|| !Spec.DecisionOwnerTrainerId.IsValid()
		|| Spec.Decisions.IsEmpty()
		|| Spec.Decisions.Num() > 2)
	{
		OutRejection.Reason = EBattleRejectionReason::InvalidDecisionBatch;
		return false;
	}

	const bool bReplacementBatch =
		Spec.RequestKind == EBattleDecisionRequestKind::MandatoryReplacement;
	TArray<FBattlerId> Actors;
	TArray<FActiveSlotId> Destinations;
	TArray<FPartySlotId> Reserves;
	for (const FBattleDecision& Decision : Spec.Decisions)
	{
		if (!Decision.IsValid()
			|| Decision.GetStateVersion() != Spec.StateVersion
			|| Decision.GetRequestKind() != Spec.RequestKind
			|| Decision.GetDecisionOwnerTrainerId() != Spec.DecisionOwnerTrainerId)
		{
			OutRejection.Reason = EBattleRejectionReason::InvalidDecisionBatch;
			return false;
		}

		if (bReplacementBatch)
		{
			if (Decision.GetActingBattlerId().IsValid()
				|| Decision.GetActionKind() != EBattleActionKind::Replacement
				|| !Decision.GetActiveTargetId().IsValid()
				|| !Decision.GetSwitchPartySlotId().IsValid()
				|| Destinations.Contains(Decision.GetActiveTargetId())
				|| Reserves.Contains(Decision.GetSwitchPartySlotId()))
			{
				OutRejection.Reason = EBattleRejectionReason::InvalidDecisionBatch;
				return false;
			}
			Destinations.Add(Decision.GetActiveTargetId());
			Reserves.Add(Decision.GetSwitchPartySlotId());
		}
		else
		{
			if (!Decision.GetActingBattlerId().IsValid()
				|| Actors.Contains(Decision.GetActingBattlerId()))
			{
				OutRejection.Reason = EBattleRejectionReason::InvalidDecisionBatch;
				return false;
			}
			Actors.Add(Decision.GetActingBattlerId());
		}
	}

	OutBatch.bValid = true;
	OutBatch.StateVersion = Spec.StateVersion;
	OutBatch.RequestKind = Spec.RequestKind;
	OutBatch.DecisionOwnerTrainerId = Spec.DecisionOwnerTrainerId;
	OutBatch.Decisions = Spec.Decisions;
	return true;
}
