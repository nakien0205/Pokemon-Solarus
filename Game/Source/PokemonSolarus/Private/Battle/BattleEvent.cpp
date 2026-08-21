#include "Battle/BattleEvent.h"

namespace
{
	bool IsKnownEventType(const EBattleEventType Value)
	{
		return static_cast<uint8>(Value) <= static_cast<uint8>(EBattleEventType::ObedienceRefused);
	}

	bool IsKnownEventCause(const EBattleEventCause Value)
	{
		return static_cast<uint8>(Value) <= static_cast<uint8>(EBattleEventCause::StatRefresh);
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
		return Target.TrainerId.IsValid()
			|| Target.BattlerId.IsValid()
			|| Target.ActiveSlotId.IsValid();
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
