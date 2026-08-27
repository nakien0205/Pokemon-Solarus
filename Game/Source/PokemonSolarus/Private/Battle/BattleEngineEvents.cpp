#include "BattleEngineEvents.h"

namespace BattleEngineEventsPrivate
{
	using namespace BattleEngineCommonPrivate;
	using namespace BattleEngineTriggerRuntimePrivate;

	FBattleEventSource BuildFieldSideConditionSource(
		const FBattleEngineState& State,
		const FBattleTriggerSubject& Owner,
		const FConditionId& ConditionId)
	{
		FBattleEventSource Source;
		Source.DefinitionId = ConditionId.GetDefinitionId();
		const FBattleConditionState* Condition = FindFieldSideCondition(
			State,
			Owner,
			ConditionId);
		const FBattleBattlerState* SourceBattler = Condition != nullptr
			? State.FindBattler(Condition->SourceBattlerId)
			: nullptr;
		if (SourceBattler == nullptr)
		{
			return Source;
		}

		Source.TrainerId = SourceBattler->TrainerId;
		Source.BattlerId = SourceBattler->BattlerId;
		const FBattleActivePositionState* SourceActive =
			State.ActivePositions.FindByPredicate(
				[SourceBattler](const FBattleActivePositionState& Position)
				{
					return Position.BattlerId == SourceBattler->BattlerId;
				});
		if (SourceActive != nullptr)
		{
			Source.ActiveSlotId = SourceActive->ActiveSlotId;
		}
		return Source;
	}

	FBattleEvent MakeActionOrderLockedEvent(
		FBattleEngineState& State,
		const FResolutionId ResolutionId,
		const FBattleLockedActionState& Action)
	{
		FBattleEventSpec Spec;
		Spec.EventOrdinal = State.NextEventOrdinal;
		Spec.BattleId = State.Setup.GetBattleId();
		Spec.TurnId = State.TurnId;
		Spec.ActionId = Action.ActionId;
		Spec.ResolutionId = ResolutionId;
		Spec.Type = EBattleEventType::ActionOrderLocked;
		Spec.Cause = EBattleEventCause::Action;
		Spec.CauseActionKind = Action.Decision.GetActionKind();
		Spec.Source = SourceFromLockedAction(State, Action);
		Spec.ActionOrder = FBattleActionOrderMetadata{
			Action.QueueOrdinal,
			Action.OrderKey,
			State.bLockedOrderReversesSpeed
		};
		Spec.Visibility.Level = EBattleVisibilityLevel::CoreOnly;

		FBattleEvent Event;
		const bool bCreated = FBattleEvent::TryCreate(Spec, Event);
		check(bCreated);
		++State.NextEventOrdinal;
		return Event;
	}

	FBattleEvent ReissueEventWithNextOrdinal(
		FBattleEngineState& State,
		const FBattleEvent& Existing)
	{
		FBattleEventSpec Spec;
		Spec.EventOrdinal = State.NextEventOrdinal;
		Spec.BattleId = Existing.GetBattleId();
		Spec.TurnId = Existing.GetTurnId();
		Spec.ActionId = Existing.GetActionId();
		Spec.ResolutionId = Existing.GetResolutionId();
		Spec.Type = Existing.GetType();
		Spec.Cause = Existing.GetCause();
		Spec.CauseActionKind = Existing.GetCauseActionKind();
		Spec.OutcomeCause = Existing.GetOutcomeCause();
		Spec.Source = Existing.GetSource();
		for (const FBattleEventTarget& Target : Existing.GetTargets())
		{
			Spec.Targets.Add(Target);
		}
		Spec.NumericBefore = Existing.GetNumericBefore();
		Spec.NumericAfter = Existing.GetNumericAfter();
		Spec.NumericDelta = Existing.GetNumericDelta();
		Spec.SimultaneousGroupId = Existing.GetSimultaneousGroupId();
		Spec.HitIndex = Existing.GetHitIndex();
		Spec.HitCount = Existing.GetHitCount();
		Spec.ActionOrder = Existing.GetActionOrder();
		Spec.TargetResolution = Existing.GetTargetResolution();
		Spec.Capture = Existing.GetCapture();
		Spec.Visibility = Existing.GetVisibility();

		FBattleEvent Event;
		const bool bCreated = FBattleEvent::TryCreate(Spec, Event);
		check(bCreated);
		++State.NextEventOrdinal;
		return Event;
	}

	FBattleEvent MakeBattleEngineTargetsResolvedEvent(
		FBattleEngineState& State,
		const FResolutionId ResolutionId,
		const FBattleLockedActionState& Action,
		const FBattleTargetResolutionResult& TargetResolution)
	{
		FBattleEventSpec Spec;
		Spec.EventOrdinal = State.NextEventOrdinal;
		Spec.BattleId = State.Setup.GetBattleId();
		Spec.TurnId = State.TurnId;
		Spec.ActionId = Action.ActionId;
		Spec.ResolutionId = ResolutionId;
		Spec.Type = EBattleEventType::TargetsResolved;
		Spec.Cause = EBattleEventCause::Targeting;
		Spec.CauseActionKind = EBattleActionKind::Fight;
		Spec.Source = SourceFromLockedAction(State, Action);
		Spec.TargetResolution = FBattleTargetResolutionMetadata{
			TargetResolution.TargetClass,
			TargetResolution.bWasRedirected,
			TargetResolution.bUsedFaintedTargetFallback
		};
		Spec.Visibility.Level = EBattleVisibilityLevel::Public;

		for (const FBattleResolvedTarget& Target : TargetResolution.Targets)
		{
			FBattleEventTarget EventTarget;
			switch (Target.GetKind())
			{
			case EBattleResolvedTargetKind::Battler:
			{
				const FBattleBattlerTarget& BattlerTarget = Target.GetBattler();
				const FBattleBattlerState* Battler = State.FindBattler(BattlerTarget.BattlerId);
				check(Battler != nullptr);
				if (Battler != nullptr)
				{
					EventTarget.TrainerId = Battler->TrainerId;
				}
				EventTarget.BattlerId = BattlerTarget.BattlerId;
				EventTarget.ActiveSlotId = BattlerTarget.ActiveSlotId;
				break;
			}
			case EBattleResolvedTargetKind::Side:
				EventTarget.Side = Target.GetSide();
				EventTarget.bHasSide = true;
				break;
			case EBattleResolvedTargetKind::Field:
				EventTarget.bField = true;
				break;
			default:
				checkNoEntry();
				break;
			}
			Spec.Targets.Add(MoveTemp(EventTarget));
		}

		FBattleEvent Event;
		const bool bCreated = FBattleEvent::TryCreate(Spec, Event);
		check(bCreated);
		++State.NextEventOrdinal;
		return Event;
	}

	FBattleEvent MakeBagItemMutationEvent(
		FBattleEngineState& State,
		const FResolutionId ResolutionId,
		const FBattleLockedActionState& Action,
		const EBattleEventType Type,
		const FBattleEventTarget& Target,
		const TOptional<int64> NumericBefore,
		const TOptional<int64> NumericAfter,
		const TOptional<int64> NumericDelta)
	{
		FBattleEventSpec Spec;
		Spec.EventOrdinal = State.NextEventOrdinal;
		Spec.BattleId = State.Setup.GetBattleId();
		Spec.TurnId = State.TurnId;
		Spec.ActionId = Action.ActionId;
		Spec.ResolutionId = ResolutionId;
		Spec.Type = Type;
		Spec.Cause = EBattleEventCause::Item;
		Spec.CauseActionKind = EBattleActionKind::Bag;
		Spec.Source = SourceFromLockedAction(State, Action);
		Spec.Targets.Add(Target);
		Spec.NumericBefore = NumericBefore;
		Spec.NumericAfter = NumericAfter;
		Spec.NumericDelta = NumericDelta;
		Spec.Visibility.Level = EBattleVisibilityLevel::Public;
		Spec.Visibility.bRevealSourceDefinition =
			Type == EBattleEventType::ItemUsed
			|| Type == EBattleEventType::ItemConsumed;

		FBattleEvent Event;
		const bool bCreated = FBattleEvent::TryCreate(Spec, Event);
		check(bCreated);
		++State.NextEventOrdinal;
		return Event;
	}

	FBattleEvent MakeCaptureEvent(
		FBattleEngineState& State,
		const FResolutionId ResolutionId,
		const FBattleLockedActionState& Action,
		const EBattleEventType Type,
		const FBattleEventTarget& Target,
		const FBattleCaptureEventMetadata& Capture)
	{
		FBattleEventSpec Spec;
		Spec.EventOrdinal = State.NextEventOrdinal;
		Spec.BattleId = State.Setup.GetBattleId();
		Spec.TurnId = State.TurnId;
		Spec.ActionId = Action.ActionId;
		Spec.ResolutionId = ResolutionId;
		Spec.Type = Type;
		Spec.Cause = EBattleEventCause::Capture;
		Spec.CauseActionKind = EBattleActionKind::Bag;
		Spec.Source = SourceFromLockedAction(State, Action);
		Spec.Targets.Add(Target);
		Spec.Capture = Capture;
		Spec.Visibility.Level = EBattleVisibilityLevel::Public;
		Spec.Visibility.bRevealSourceDefinition = true;

		FBattleEvent Event;
		const bool bCreated = FBattleEvent::TryCreate(Spec, Event);
		check(bCreated);
		++State.NextEventOrdinal;
		return Event;
	}

	FBattleEvent MakeResidualMutationEvent(
		FBattleEngineState& State,
		const FResolutionId ResolutionId,
		const EBattleEventType Type,
		const FBattleEventSource& Source,
		const FBattleEventTarget& Target,
		const int64 NumericBefore,
		const int64 NumericAfter,
		const int64 NumericDelta)
	{
		FBattleEventSpec Spec;
		Spec.EventOrdinal = State.NextEventOrdinal;
		Spec.BattleId = State.Setup.GetBattleId();
		Spec.TurnId = State.TurnId;
		Spec.ResolutionId = ResolutionId;
		Spec.Type = Type;
		Spec.Cause = EBattleEventCause::Rule;
		Spec.CauseActionKind = EBattleActionKind::Residual;
		Spec.Source = Source;
		Spec.Targets.Add(Target);
		Spec.NumericBefore = NumericBefore;
		Spec.NumericAfter = NumericAfter;
		Spec.NumericDelta = NumericDelta;
		Spec.Visibility.Level = EBattleVisibilityLevel::Public;

		FBattleEvent Event;
		const bool bCreated = FBattleEvent::TryCreate(Spec, Event);
		check(bCreated);
		++State.NextEventOrdinal;
		return Event;
	}

	void AppendActionlessSwitchTransitionEvents(
		FBattleEngineState& State,
		const FResolutionId ResolutionId,
		const FBattleEventSource& Source,
		const FBattleEventTarget& Outgoing,
		const FBattleEventTarget& Incoming,
		TArray<FBattleEvent>& Events)
	{
		Events.Add(MakeTargetedActionlessEvent(
			State,
			ResolutionId,
			EBattleEventType::LeftActiveSlot,
			EBattleEventCause::Switch,
			EBattleActionKind::Switch,
			Source,
			Outgoing));
		Events.Add(MakeTargetedActionlessEvent(
			State,
			ResolutionId,
			EBattleEventType::SwitchTransientStateCleared,
			EBattleEventCause::Rule,
			EBattleActionKind::Switch,
			Source,
			Outgoing));
		Events.Add(MakeTargetedActionlessEvent(
			State,
			ResolutionId,
			EBattleEventType::EnteredActiveSlot,
			EBattleEventCause::Switch,
			EBattleActionKind::Switch,
			Source,
			Incoming));
		Events.Add(MakeTargetedActionlessEvent(
			State,
			ResolutionId,
			EBattleEventType::Switched,
			EBattleEventCause::Rule,
			EBattleActionKind::Switch,
			Source,
			Incoming));
	}

	FBattleEventSpec MakeAtomicSwitchStagedEventSpec(const FBattleEvent& Event)
	{
		FBattleEventSpec Spec;
		Spec.BattleId = Event.GetBattleId();
		Spec.TurnId = Event.GetTurnId();
		Spec.ActionId = Event.GetActionId();
		Spec.ResolutionId = Event.GetResolutionId();
		Spec.Type = Event.GetType();
		Spec.Cause = Event.GetCause();
		Spec.CauseActionKind = Event.GetCauseActionKind();
		Spec.OutcomeCause = Event.GetOutcomeCause();
		Spec.Source = Event.GetSource();
		for (const FBattleEventTarget& Target : Event.GetTargets())
		{
			Spec.Targets.Add(Target);
		}
		Spec.NumericBefore = Event.GetNumericBefore();
		Spec.NumericAfter = Event.GetNumericAfter();
		Spec.NumericDelta = Event.GetNumericDelta();
		Spec.SimultaneousGroupId = Event.GetSimultaneousGroupId();
		Spec.HitIndex = Event.GetHitIndex();
		Spec.HitCount = Event.GetHitCount();
		Spec.ActionOrder = Event.GetActionOrder();
		Spec.TargetResolution = Event.GetTargetResolution();
		Spec.Capture = Event.GetCapture();
		Spec.Visibility = Event.GetVisibility();
		return Spec;
	}
}
