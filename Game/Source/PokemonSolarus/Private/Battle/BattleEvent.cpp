#include "Battle/BattleEvent.h"

namespace
{
	bool IsKnownEventType(const EBattleEventType Value)
	{
		return static_cast<uint8>(Value) <= static_cast<uint8>(EBattleEventType::TargetsResolved);
	}

	bool IsKnownEventCause(const EBattleEventCause Value)
	{
		return static_cast<uint8>(Value) <= static_cast<uint8>(EBattleEventCause::Targeting);
	}

	bool IsKnownAction(const EBattleActionKind Value)
	{
		return static_cast<uint8>(Value) <= static_cast<uint8>(EBattleActionKind::Abandon);
	}

	bool IsKnownOutcomeCause(const EBattleOutcomeCause Value)
	{
		return static_cast<uint8>(Value) <= static_cast<uint8>(EBattleOutcomeCause::OpponentFled);
	}

	bool IsKnownVisibility(const EBattleVisibilityLevel Value)
	{
		return static_cast<uint8>(Value) <= static_cast<uint8>(EBattleVisibilityLevel::Public);
	}

	bool HasSource(const FBattleEventSource& Source)
	{
		return Source.TrainerId.IsValid()
			|| Source.BattlerId.IsValid()
			|| Source.ActiveSlotId.IsValid()
			|| Source.DefinitionId.IsValid();
	}

	bool HasTarget(const FBattleEventTarget& Target)
	{
		const bool bHasIdentity = Target.TrainerId.IsValid()
			|| Target.BattlerId.IsValid()
			|| Target.ActiveSlotId.IsValid();
		if (Target.bField)
		{
			return !bHasIdentity
				&& !Target.bHasSide
				&& Target.Side == EBattleSide::Player;
		}
		if (Target.bHasSide)
		{
			return !bHasIdentity
				&& (Target.Side == EBattleSide::Player || Target.Side == EBattleSide::Opponent);
		}
		return bHasIdentity && Target.Side == EBattleSide::Player;
	}

	bool IsActionOrderMetadataValid(const FBattleActionOrderMetadata& Metadata)
	{
		const bool bKnownBand = Metadata.OrderKey.CommandBand == EBattleActionCommandBand::Move
			|| Metadata.OrderKey.CommandBand == EBattleActionCommandBand::Bag
			|| Metadata.OrderKey.CommandBand == EBattleActionCommandBand::VoluntarySwitch
			|| Metadata.OrderKey.CommandBand == EBattleActionCommandBand::Run;
		if (Metadata.QueueOrdinal == 0
			|| !bKnownBand
			|| !Metadata.OrderKey.ActingSlotId.IsValid()
			|| Metadata.OrderKey.EffectiveSpeed <= 0)
		{
			return false;
		}

		if (Metadata.OrderKey.CommandBand == EBattleActionCommandBand::Move)
		{
			return Metadata.OrderKey.MovePriority >= -7
				&& Metadata.OrderKey.MovePriority <= 5
				&& Metadata.OrderKey.FractionalPriorityTenths >= 0
				&& Metadata.OrderKey.FractionalPriorityTenths <= 9;
		}

		return Metadata.OrderKey.MovePriority == 0
			&& Metadata.OrderKey.FractionalPriorityTenths == 0;
	}

	bool IsTargetResolutionMetadataValid(const FBattleTargetResolutionMetadata& Metadata)
	{
		return static_cast<uint8>(Metadata.TargetClass)
			<= static_cast<uint8>(EBattleTargetClass::FixedSpreadSet);
	}

	int32 GetBattleEventActiveSlotOrder(const FActiveSlotId ActiveSlotId)
	{
		const int32 SideOffset = ActiveSlotId.GetSide() == EBattleSide::Player ? 0 : 2;
		const int32 PositionOffset = ActiveSlotId.GetPosition() == EBattlePosition::Left ? 0 : 1;
		return SideOffset + PositionOffset;
	}

	bool IsBattleEventBattlerTarget(const FBattleEventTarget& Target)
	{
		return Target.TrainerId.IsValid()
			&& Target.BattlerId.IsValid()
			&& Target.ActiveSlotId.IsValid()
			&& !Target.bHasSide
			&& !Target.bField;
	}

	bool IsBattleEventTargetResolutionShapeValid(const FBattleEventSpec& Spec)
	{
		const FBattleTargetResolutionMetadata& Metadata = Spec.TargetResolution.GetValue();
		if (!Spec.Source.TrainerId.IsValid()
			|| !Spec.Source.BattlerId.IsValid()
			|| !Spec.Source.ActiveSlotId.IsValid()
			|| (Metadata.bUsedFaintedTargetFallback
				&& Metadata.TargetClass != EBattleTargetClass::SelectedOpponent
				&& Metadata.TargetClass != EBattleTargetClass::AnySelectedBattler))
		{
			return false;
		}

		const bool bSupportsRedirection = Metadata.TargetClass == EBattleTargetClass::SelectedAlly
			|| Metadata.TargetClass == EBattleTargetClass::SelectedOpponent
			|| Metadata.TargetClass == EBattleTargetClass::AnySelectedBattler
			|| Metadata.TargetClass == EBattleTargetClass::RandomLegalOpponent;
		if (Metadata.bWasRedirected && !bSupportsRedirection)
		{
			return false;
		}

		if (Spec.Targets.IsEmpty())
		{
			return !Metadata.bWasRedirected
				&& (Metadata.TargetClass == EBattleTargetClass::SelectedAlly
					|| Metadata.TargetClass == EBattleTargetClass::SelectedOpponent
					|| Metadata.TargetClass == EBattleTargetClass::AnySelectedBattler
					|| Metadata.TargetClass == EBattleTargetClass::RandomLegalOpponent
					|| Metadata.TargetClass == EBattleTargetClass::FixedSpreadSet);
		}

		for (int32 Index = 0; Index < Spec.Targets.Num(); ++Index)
		{
			const FBattleEventTarget& Target = Spec.Targets[Index];
			if (!IsBattleEventBattlerTarget(Target))
			{
				break;
			}
			for (int32 PriorIndex = 0; PriorIndex < Index; ++PriorIndex)
			{
				if (Spec.Targets[PriorIndex].BattlerId == Target.BattlerId
					|| Spec.Targets[PriorIndex].ActiveSlotId == Target.ActiveSlotId)
				{
					return false;
				}
			}
			if (Index > 0
				&& GetBattleEventActiveSlotOrder(Spec.Targets[Index - 1].ActiveSlotId)
					>= GetBattleEventActiveSlotOrder(Target.ActiveSlotId))
			{
				return false;
			}
		}

		const EBattleSide UserSide = Spec.Source.ActiveSlotId.GetSide();
		const EBattleSide OtherSide = UserSide == EBattleSide::Player
			? EBattleSide::Opponent
			: EBattleSide::Player;
		switch (Metadata.TargetClass)
		{
		case EBattleTargetClass::Self:
			return Spec.Targets.Num() == 1
				&& IsBattleEventBattlerTarget(Spec.Targets[0])
				&& Spec.Targets[0].TrainerId == Spec.Source.TrainerId
				&& Spec.Targets[0].BattlerId == Spec.Source.BattlerId
				&& Spec.Targets[0].ActiveSlotId == Spec.Source.ActiveSlotId;
		case EBattleTargetClass::SelectedAlly:
			return Spec.Targets.Num() == 1
				&& IsBattleEventBattlerTarget(Spec.Targets[0])
				&& Spec.Targets[0].ActiveSlotId.GetSide() == UserSide
				&& Spec.Targets[0].BattlerId != Spec.Source.BattlerId;
		case EBattleTargetClass::SelectedOpponent:
		case EBattleTargetClass::RandomLegalOpponent:
			return Spec.Targets.Num() == 1
				&& IsBattleEventBattlerTarget(Spec.Targets[0])
				&& Spec.Targets[0].ActiveSlotId.GetSide() == OtherSide;
		case EBattleTargetClass::AnySelectedBattler:
			return Spec.Targets.Num() == 1 && IsBattleEventBattlerTarget(Spec.Targets[0]);
		case EBattleTargetClass::UserSide:
			return Spec.Targets.Num() == 1
				&& Spec.Targets[0].bHasSide
				&& Spec.Targets[0].Side == UserSide;
		case EBattleTargetClass::OpponentSide:
			return Spec.Targets.Num() == 1
				&& Spec.Targets[0].bHasSide
				&& Spec.Targets[0].Side == OtherSide;
		case EBattleTargetClass::BothSides:
			return Spec.Targets.Num() == 2
				&& Spec.Targets[0].bHasSide
				&& Spec.Targets[0].Side == EBattleSide::Player
				&& Spec.Targets[1].bHasSide
				&& Spec.Targets[1].Side == EBattleSide::Opponent;
		case EBattleTargetClass::Field:
			return Spec.Targets.Num() == 1 && Spec.Targets[0].bField;
		case EBattleTargetClass::FixedSpreadSet:
			if (Spec.Targets.Num() > 3)
			{
				return false;
			}
			for (const FBattleEventTarget& Target : Spec.Targets)
			{
				if (!IsBattleEventBattlerTarget(Target)
					|| (Target.BattlerId == Spec.Source.BattlerId
						&& Target.ActiveSlotId == Spec.Source.ActiveSlotId))
				{
					return false;
				}
			}
			return true;
		default:
			return false;
		}
	}
}

bool FBattleEvent::TryCreate(const FBattleEventSpec& Spec, FBattleEvent& OutEvent)
{
	OutEvent = FBattleEvent();
	if (Spec.EventOrdinal == 0
		|| !Spec.BattleId.IsValid()
		|| !Spec.TurnId.IsValid()
		|| !Spec.ResolutionId.IsValid()
		|| !IsKnownEventType(Spec.Type)
		|| !IsKnownEventCause(Spec.Cause)
		|| !IsKnownAction(Spec.CauseActionKind)
		|| !IsKnownOutcomeCause(Spec.OutcomeCause)
		|| !HasSource(Spec.Source)
		|| !IsKnownVisibility(Spec.Visibility.Level))
	{
		return false;
	}
	for (const FBattleEventTarget& Target : Spec.Targets)
	{
		if (!HasTarget(Target))
		{
			return false;
		}
	}
	if (Spec.SimultaneousGroupId.IsSet() && Spec.SimultaneousGroupId.GetValue() == 0)
	{
		return false;
	}
	if (Spec.HitIndex.IsSet() != Spec.HitCount.IsSet())
	{
		return false;
	}
	if (Spec.HitIndex.IsSet()
		&& (Spec.HitIndex.GetValue() == 0
			|| Spec.HitCount.GetValue() == 0
			|| Spec.HitIndex.GetValue() > Spec.HitCount.GetValue()))
	{
		return false;
	}
	if (Spec.Type == EBattleEventType::ActionOrderLocked)
	{
		if (!Spec.ActionId.IsValid()
			|| !Spec.ActionOrder.IsSet()
			|| !IsActionOrderMetadataValid(Spec.ActionOrder.GetValue()))
		{
			return false;
		}
	}
	else if (Spec.ActionOrder.IsSet())
	{
		return false;
	}
	if (Spec.Type == EBattleEventType::TargetsResolved)
	{
		if (!Spec.ActionId.IsValid()
			|| Spec.Cause != EBattleEventCause::Targeting
			|| Spec.CauseActionKind != EBattleActionKind::Fight
			|| !Spec.TargetResolution.IsSet()
			|| !IsTargetResolutionMetadataValid(Spec.TargetResolution.GetValue())
			|| (Spec.TargetResolution.GetValue().bUsedFaintedTargetFallback
				&& !Spec.TargetResolution.GetValue().bWasRedirected)
			|| !IsBattleEventTargetResolutionShapeValid(Spec))
		{
			return false;
		}
	}
	else if (Spec.TargetResolution.IsSet())
	{
		return false;
	}
	if (Spec.Visibility.Level == EBattleVisibilityLevel::OwningTrainer
		&& !Spec.Visibility.OwningTrainerId.IsValid())
	{
		return false;
	}
	if (Spec.Visibility.Level == EBattleVisibilityLevel::OwningSide
		&& (!Spec.Visibility.bHasOwningSide
			|| (Spec.Visibility.OwningSide != EBattleSide::Player
				&& Spec.Visibility.OwningSide != EBattleSide::Opponent)))
	{
		return false;
	}

	OutEvent.bValid = true;
	OutEvent.EventOrdinal = Spec.EventOrdinal;
	OutEvent.BattleId = Spec.BattleId;
	OutEvent.TurnId = Spec.TurnId;
	OutEvent.ActionId = Spec.ActionId;
	OutEvent.ResolutionId = Spec.ResolutionId;
	OutEvent.Type = Spec.Type;
	OutEvent.Cause = Spec.Cause;
	OutEvent.CauseActionKind = Spec.CauseActionKind;
	OutEvent.OutcomeCause = Spec.OutcomeCause;
	OutEvent.Source = Spec.Source;
	OutEvent.Targets = Spec.Targets;
	OutEvent.NumericBefore = Spec.NumericBefore;
	OutEvent.NumericAfter = Spec.NumericAfter;
	OutEvent.NumericDelta = Spec.NumericDelta;
	OutEvent.SimultaneousGroupId = Spec.SimultaneousGroupId;
	OutEvent.HitIndex = Spec.HitIndex;
	OutEvent.HitCount = Spec.HitCount;
	OutEvent.ActionOrder = Spec.ActionOrder;
	OutEvent.TargetResolution = Spec.TargetResolution;
	OutEvent.Visibility = Spec.Visibility;
	return true;
}

bool FBattleResolution::TryCreate(
	const FBattleResolutionSpec& Spec,
	FBattleResolution& OutResolution)
{
	OutResolution = FBattleResolution();
	if (!Spec.ResolutionId.IsValid()
		|| Spec.BeforeStateVersion == 0
		|| Spec.Events.IsEmpty())
	{
		return false;
	}
	if (Spec.bAccepted)
	{
		if (Spec.AfterStateVersion <= Spec.BeforeStateVersion || Spec.Rejection.IsRejected())
		{
			return false;
		}
	}
	else if (Spec.AfterStateVersion != Spec.BeforeStateVersion || !Spec.Rejection.IsRejected())
	{
		return false;
	}

	uint64 PreviousOrdinal = 0;
	for (const FBattleEvent& Event : Spec.Events)
	{
		if (!Event.IsValid()
			|| Event.GetResolutionId() != Spec.ResolutionId
			|| Event.GetEventOrdinal() <= PreviousOrdinal)
		{
			return false;
		}
		PreviousOrdinal = Event.GetEventOrdinal();
	}

	OutResolution.bValid = true;
	OutResolution.ResolutionId = Spec.ResolutionId;
	OutResolution.BeforeStateVersion = Spec.BeforeStateVersion;
	OutResolution.AfterStateVersion = Spec.AfterStateVersion;
	OutResolution.bAccepted = Spec.bAccepted;
	OutResolution.Rejection = Spec.Rejection;
	OutResolution.Events = Spec.Events;
	return true;
}
