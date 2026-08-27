#pragma once

#include "Battle/BattleEffectExecutor.h"
#include "BattleEngineCommon.h"
#include "BattleEngineTriggerRuntime.h"
#include "BattleFaintOutcomeResolver.h"
#include "BattlePartnerFlow.h"

namespace BattleEngineEventsPrivate
{
	using namespace BattleEngineCommonPrivate;
	using namespace BattleEngineTriggerRuntimePrivate;

	FBattleEventSource BuildFieldSideConditionSource(
		const FBattleEngineState& State,
		const FBattleTriggerSubject& Owner,
		const FConditionId& ConditionId);

	template <typename TState>
	FBattleEvent MakeEvent(
		TState& State,
		const FResolutionId ResolutionId,
		const FActionId ActionId,
		const EBattleEventType Type,
		const EBattleEventCause Cause,
		const EBattleActionKind ActionKind,
		const EBattleOutcomeCause OutcomeCause,
		const FBattleEventSource& Source)
	{
		FBattleEventSpec Spec;
		Spec.EventOrdinal = State.NextEventOrdinal;
		Spec.BattleId = State.Setup.GetBattleId();
		Spec.TurnId = State.TurnId;
		Spec.ActionId = ActionId;
		Spec.ResolutionId = ResolutionId;
		Spec.Type = Type;
		Spec.Cause = Cause;
		Spec.CauseActionKind = ActionKind;
		Spec.OutcomeCause = OutcomeCause;
		Spec.Source = Source;
		Spec.Visibility.Level = EBattleVisibilityLevel::Public;

		FBattleEvent Event;
		const bool bCreated = FBattleEvent::TryCreate(Spec, Event);
		check(bCreated);
		++State.NextEventOrdinal;
		return Event;
	}

	FBattleEvent MakeActionOrderLockedEvent(
		FBattleEngineState& State,
		const FResolutionId ResolutionId,
		const FBattleLockedActionState& Action);

	template <typename TState>
	FBattleEvent MakeActionDetailEvent(
		TState& State,
		const FResolutionId ResolutionId,
		const FBattleLockedActionState& Action,
		const EBattleEventType Type,
		const EBattleEventCause Cause,
		const TOptional<int64> NumericBefore = TOptional<int64>(),
		const TOptional<int64> NumericAfter = TOptional<int64>(),
		const TOptional<int64> NumericDelta = TOptional<int64>(),
		const EBattleVisibilityLevel Visibility = EBattleVisibilityLevel::Public)
	{
		FBattleEventSpec Spec;
		Spec.EventOrdinal = State.NextEventOrdinal;
		Spec.BattleId = State.Setup.GetBattleId();
		Spec.TurnId = State.TurnId;
		Spec.ActionId = Action.ActionId;
		Spec.ResolutionId = ResolutionId;
		Spec.Type = Type;
		Spec.Cause = Cause;
		Spec.CauseActionKind = Action.Decision.GetActionKind();
		Spec.Source = SourceFromLockedAction(State, Action);
		Spec.NumericBefore = NumericBefore;
		Spec.NumericAfter = NumericAfter;
		Spec.NumericDelta = NumericDelta;
		Spec.Visibility.Level = Visibility;

		FBattleEvent Event;
		const bool bCreated = FBattleEvent::TryCreate(Spec, Event);
		check(bCreated);
		++State.NextEventOrdinal;
		return Event;
	}

	FBattleEvent ReissueEventWithNextOrdinal(
		FBattleEngineState& State,
		const FBattleEvent& Existing);

	FBattleEvent MakeBattleEngineTargetsResolvedEvent(
		FBattleEngineState& State,
		const FResolutionId ResolutionId,
		const FBattleLockedActionState& Action,
		const FBattleTargetResolutionResult& TargetResolution);

	template <typename TState>
	FBattleEvent MakeBattleEffectEvent(
		TState& State,
		const FResolutionId ResolutionId,
		const FBattleLockedActionState& Action,
		const FBattleEffectExecutionEvent& Record,
		const TOptional<uint64> SimultaneousGroupId = TOptional<uint64>())
	{
		FBattleEventSpec Spec;
		Spec.EventOrdinal = State.NextEventOrdinal;
		Spec.BattleId = State.Setup.GetBattleId();
		Spec.TurnId = State.TurnId;
		Spec.ActionId = Action.ActionId;
		Spec.ResolutionId = ResolutionId;
		Spec.Type = Record.Type;
		Spec.Cause = Record.Cause;
		Spec.CauseActionKind = EBattleActionKind::Fight;
		Spec.Source = Record.SourceOverride.IsSet()
			? Record.SourceOverride.GetValue()
			: SourceFromLockedAction(State, Action);
		Spec.Targets = Record.Targets;
		Spec.NumericBefore = Record.NumericBefore;
		Spec.NumericAfter = Record.NumericAfter;
		Spec.NumericDelta = Record.NumericDelta;
		Spec.SimultaneousGroupId = SimultaneousGroupId;
		Spec.HitIndex = Record.HitIndex;
		Spec.HitCount = Record.HitCount;
		Spec.Visibility.Level = EBattleVisibilityLevel::Public;
		Spec.Visibility.bRevealSourceDefinition =
			Record.Type == EBattleEventType::AbilityActivated
			|| Record.Type == EBattleEventType::ItemActivated
			|| Record.Type == EBattleEventType::ItemRemoved;

		FBattleEvent Event;
		const bool bCreated = FBattleEvent::TryCreate(Spec, Event);
		check(bCreated);
		++State.NextEventOrdinal;
		return Event;
	}

	template <typename TState>
	FBattleEvent MakeTargetedActionEvent(
		TState& State,
		const FResolutionId ResolutionId,
		const FBattleLockedActionState& Action,
		const EBattleEventType Type,
		const EBattleEventCause Cause,
		const FBattleEventTarget& Target,
		const EBattleOutcomeCause OutcomeCause = EBattleOutcomeCause::None,
		const TOptional<uint64> SimultaneousGroupId = TOptional<uint64>(),
		const TOptional<uint16> HitIndex = TOptional<uint16>(),
		const TOptional<uint16> HitCount = TOptional<uint16>(),
		const FBattleEventSource* SourceOverride = nullptr)
	{
		FBattleEventSpec Spec;
		Spec.EventOrdinal = State.NextEventOrdinal;
		Spec.BattleId = State.Setup.GetBattleId();
		Spec.TurnId = State.TurnId;
		Spec.ActionId = Action.ActionId;
		Spec.ResolutionId = ResolutionId;
		Spec.Type = Type;
		Spec.Cause = Cause;
		Spec.CauseActionKind = Action.Decision.GetActionKind();
		Spec.OutcomeCause = OutcomeCause;
		Spec.Source = SourceOverride != nullptr
			? *SourceOverride
			: SourceFromLockedAction(State, Action);
		Spec.Targets.Add(Target);
		Spec.SimultaneousGroupId = SimultaneousGroupId;
		Spec.HitIndex = HitIndex;
		Spec.HitCount = HitCount;
		Spec.Visibility.Level = EBattleVisibilityLevel::Public;

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
		const TOptional<int64> NumericBefore = TOptional<int64>(),
		const TOptional<int64> NumericAfter = TOptional<int64>(),
		const TOptional<int64> NumericDelta = TOptional<int64>());

	FBattleEvent MakeCaptureEvent(
		FBattleEngineState& State,
		const FResolutionId ResolutionId,
		const FBattleLockedActionState& Action,
		const EBattleEventType Type,
		const FBattleEventTarget& Target,
		const FBattleCaptureEventMetadata& Capture);

	template <typename TState>
	FBattleEvent MakeTargetedActionlessEvent(
		TState& State,
		const FResolutionId ResolutionId,
		const EBattleEventType Type,
		const EBattleEventCause Cause,
		const EBattleActionKind ActionKind,
		const FBattleEventSource& Source,
		const FBattleEventTarget& Target)
	{
		FBattleEventSpec Spec;
		Spec.EventOrdinal = State.NextEventOrdinal;
		Spec.BattleId = State.Setup.GetBattleId();
		Spec.TurnId = State.TurnId;
		Spec.ResolutionId = ResolutionId;
		Spec.Type = Type;
		Spec.Cause = Cause;
		Spec.CauseActionKind = ActionKind;
		Spec.Source = Source;
		Spec.Targets.Add(Target);
		Spec.Visibility.Level = EBattleVisibilityLevel::Public;

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
		const int64 NumericDelta);

	template <typename TState>
	FBattleEvent MakeRuleMutationEvent(
		TState& State,
		const FResolutionId ResolutionId,
		const EBattleEventType Type,
		const EBattleActionKind ActionKind,
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
		Spec.CauseActionKind = ActionKind;
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

	template <typename TState>
	FBattleEvent MakeAbilityActivationEvent(
		TState& State,
		const FResolutionId ResolutionId,
		const FActionId ActionId,
		const EBattleActionKind ActionKind,
		const FBattleTriggerEffectRequest& Request,
		const FBattleAbilityItemActivationFact& Fact,
		const FBattleEventTarget* ExplicitTarget = nullptr)
	{
		FBattleEventSpec Spec;
		Spec.EventOrdinal = State.NextEventOrdinal;
		Spec.BattleId = State.Setup.GetBattleId();
		Spec.TurnId = State.TurnId;
		Spec.ActionId = ActionId;
		Spec.ResolutionId = ResolutionId;
		Spec.Type = EBattleEventType::AbilityActivated;
		Spec.Cause = EBattleEventCause::Rule;
		Spec.CauseActionKind = ActionKind;
		const FBattleBattlerState* SourceBattler =
			Request.Source.Kind == EBattleTriggerSubjectKind::Battler
				? State.FindBattler(Request.Source.BattlerId)
				: nullptr;
		if (SourceBattler != nullptr)
		{
			Spec.Source.TrainerId = SourceBattler->TrainerId;
			Spec.Source.BattlerId = SourceBattler->BattlerId;
			const FBattleActivePositionState* SourceActive = FindActiveForBattler(
				State,
				SourceBattler->BattlerId);
			if (SourceActive != nullptr)
			{
				Spec.Source.ActiveSlotId = SourceActive->ActiveSlotId;
			}
		}
		Spec.Source.DefinitionId = Request.SourceDefinition.AbilityId.GetDefinitionId();
		if (ExplicitTarget != nullptr)
		{
			Spec.Targets.Add(*ExplicitTarget);
		}
		else
		{
			for (const FBattleTriggerSubject& Subject : Request.Targets)
			{
				FBattleEventTarget Target;
				if (Subject.Kind == EBattleTriggerSubjectKind::Battler)
				{
					const FBattleBattlerState* TargetBattler = State.FindBattler(
						Subject.BattlerId);
					if (TargetBattler == nullptr)
					{
						continue;
					}
					Target.TrainerId = TargetBattler->TrainerId;
					Target.BattlerId = TargetBattler->BattlerId;
					const FBattleActivePositionState* TargetActive = FindActiveForBattler(
						State,
						TargetBattler->BattlerId);
					if (TargetActive != nullptr)
					{
						Target.ActiveSlotId = TargetActive->ActiveSlotId;
					}
				}
				else if (Subject.Kind == EBattleTriggerSubjectKind::ActiveSlot)
				{
					Target.ActiveSlotId = Subject.ActiveSlotId;
					const FBattleActivePositionState* TargetActive = State.FindActivePosition(
						Subject.ActiveSlotId);
					if (TargetActive != nullptr)
					{
						Target.TrainerId = TargetActive->TrainerId;
						Target.BattlerId = TargetActive->BattlerId;
					}
				}
				else if (Subject.Kind == EBattleTriggerSubjectKind::Side)
				{
					Target.Side = Subject.Side;
					Target.bHasSide = true;
				}
				else if (Subject.Kind == EBattleTriggerSubjectKind::Field)
				{
					Target.bField = true;
				}
				else
				{
					continue;
				}
				Spec.Targets.Add(MoveTemp(Target));
			}
		}
		Spec.NumericBefore = Fact.bFirstPublicReveal ? 0 : 1;
		Spec.NumericAfter = 1;
		Spec.NumericDelta = Fact.bFirstPublicReveal ? 1 : 0;
		Spec.Visibility.Level = EBattleVisibilityLevel::Public;
		Spec.Visibility.bRevealSourceDefinition = Fact.RevealedSourceDefinition.IsSet();

		FBattleEvent Event;
		const bool bCreated = FBattleEvent::TryCreate(Spec, Event);
		check(bCreated);
		++State.NextEventOrdinal;
		return Event;
	}

	template <typename TState>
	FBattleEvent MakeItemActivationEvent(
		TState& State,
		const FResolutionId ResolutionId,
		const FActionId ActionId,
		const EBattleActionKind ActionKind,
		const FBattleTriggerEffectRequest& Request,
		const FBattleAbilityItemActivationFact& Fact,
		const FBattleEventTarget* ExplicitTarget = nullptr)
	{
		FBattleEventSpec Spec;
		Spec.EventOrdinal = State.NextEventOrdinal;
		Spec.BattleId = State.Setup.GetBattleId();
		Spec.TurnId = State.TurnId;
		Spec.ActionId = ActionId;
		Spec.ResolutionId = ResolutionId;
		Spec.Type = EBattleEventType::ItemActivated;
		Spec.Cause = EBattleEventCause::Item;
		Spec.CauseActionKind = ActionKind;
		const FBattleBattlerState* SourceBattler =
			Request.Source.Kind == EBattleTriggerSubjectKind::Battler
				? State.FindBattler(Request.Source.BattlerId)
				: nullptr;
		if (SourceBattler != nullptr)
		{
			Spec.Source.TrainerId = SourceBattler->TrainerId;
			Spec.Source.BattlerId = SourceBattler->BattlerId;
			const FBattleActivePositionState* SourceActive = FindActiveForBattler(
				State,
				SourceBattler->BattlerId);
			if (SourceActive != nullptr)
			{
				Spec.Source.ActiveSlotId = SourceActive->ActiveSlotId;
			}
		}
		Spec.Source.DefinitionId = Request.SourceDefinition.ItemId.GetDefinitionId();
		if (ExplicitTarget != nullptr)
		{
			Spec.Targets.Add(*ExplicitTarget);
		}
		else
		{
			for (const FBattleTriggerSubject& Subject : Request.Targets)
			{
				if (Subject.Kind != EBattleTriggerSubjectKind::Battler)
				{
					continue;
				}
				const FBattleBattlerState* TargetBattler = State.FindBattler(Subject.BattlerId);
				if (TargetBattler == nullptr)
				{
					continue;
				}
				FBattleEventTarget Target;
				Target.TrainerId = TargetBattler->TrainerId;
				Target.BattlerId = TargetBattler->BattlerId;
				const FBattleActivePositionState* TargetActive = FindActiveForBattler(
					State,
					TargetBattler->BattlerId);
				if (TargetActive != nullptr)
				{
					Target.ActiveSlotId = TargetActive->ActiveSlotId;
				}
				Spec.Targets.Add(MoveTemp(Target));
			}
		}
		Spec.NumericBefore = Fact.bFirstPublicReveal ? 0 : 1;
		Spec.NumericAfter = 1;
		Spec.NumericDelta = Fact.bFirstPublicReveal ? 1 : 0;
		Spec.Visibility.Level = EBattleVisibilityLevel::Public;
		Spec.Visibility.bRevealSourceDefinition = Fact.RevealedSourceDefinition.IsSet();

		FBattleEvent Event;
		const bool bCreated = FBattleEvent::TryCreate(Spec, Event);
		check(bCreated);
		++State.NextEventOrdinal;
		return Event;
	}

	template <typename TState>
	bool TryAppendAbilityActivationForPhase(
		TState& State,
		const FBattlerId BattlerId,
		const EBattleTriggerPhase Phase,
		const EBattleAbilityItemActivationOutcome Outcome,
		const FResolutionId ResolutionId,
		const FActionId ActionId,
		const EBattleActionKind ActionKind,
		TArray<FBattleEvent>& Events,
		const FBattleEventTarget* ExplicitTarget = nullptr)
	{
		TArray<FBattleTriggerEffectRequest> Requests;
		const TArray<FBattlerId> Owners{BattlerId};
		if (!TryDispatchAbilityPhase(State, Owners, Phase, Requests))
		{
			return false;
		}
		const FBattleBattlerState* Battler = State.FindBattler(BattlerId);
		const FBattleTriggerEffectRequest* Request = Battler != nullptr
			? Requests.FindByPredicate(
				[Battler](const FBattleTriggerEffectRequest& Candidate)
				{
					return Candidate.SourceDefinition.Kind
						== EBattleTriggerSourceDefinitionKind::Ability
						&& Candidate.SourceDefinition.AbilityId == Battler->AbilityId;
				})
			: nullptr;
		if (Request == nullptr)
		{
			return Outcome != EBattleAbilityItemActivationOutcome::Applied
				&& Outcome != EBattleAbilityItemActivationOutcome::AttemptedButPrevented;
		}
		TOptional<FBattleAbilityItemActivationFact> Fact;
		if (!TryRecordAbilityActivation(State, *Request, Outcome, Fact))
		{
			return false;
		}
		if (Fact.IsSet())
		{
			Events.Add(MakeAbilityActivationEvent(
				State,
				ResolutionId,
				ActionId,
				ActionKind,
				*Request,
				Fact.GetValue(),
				ExplicitTarget));
		}
		return true;
	}

	template <typename TState>
	bool TryAppendItemActivationForPhase(
		TState& State,
		const FBattlerId BattlerId,
		const EBattleTriggerPhase Phase,
		const EBattleAbilityItemActivationOutcome Outcome,
		const FResolutionId ResolutionId,
		const FActionId ActionId,
		const EBattleActionKind ActionKind,
		TArray<FBattleEvent>& Events,
		const FBattleEventTarget* ExplicitTarget = nullptr)
	{
		TArray<FBattleTriggerEffectRequest> Requests;
		const TArray<FBattlerId> Owners{BattlerId};
		if (!TryDispatchItemPhase(State, Owners, Phase, Requests))
		{
			return false;
		}
		const FBattleBattlerState* Battler = State.FindBattler(BattlerId);
		const FBattleTriggerEffectRequest* Request = Battler != nullptr
			? Requests.FindByPredicate(
				[Battler](const FBattleTriggerEffectRequest& Candidate)
				{
					return Candidate.SourceDefinition.Kind
						== EBattleTriggerSourceDefinitionKind::Item
						&& Candidate.SourceDefinition.ItemId
							== Battler->HeldItem.CurrentItemId;
				})
			: nullptr;
		if (Request == nullptr)
		{
			return Outcome != EBattleAbilityItemActivationOutcome::Applied
				&& Outcome != EBattleAbilityItemActivationOutcome::AttemptedButPrevented;
		}
		TOptional<FBattleAbilityItemActivationFact> Fact;
		if (!TryRecordItemActivation(State, *Request, Outcome, Fact))
		{
			return false;
		}
		if (Fact.IsSet())
		{
			Events.Add(MakeItemActivationEvent(
				State,
				ResolutionId,
				ActionId,
				ActionKind,
				*Request,
				Fact.GetValue(),
				ExplicitTarget));
		}
		return true;
	}

	template <typename TState>
	FBattleEvent MakeHeldItemMutationEvent(
		TState& State,
		const FResolutionId ResolutionId,
		const FActionId ActionId,
		const EBattleActionKind ActionKind,
		const EBattleEventType Type,
		const FBattlerId BattlerId,
		const FActiveSlotId ActiveSlotId,
		const FItemId& ItemId,
		const int64 NumericBefore,
		const int64 NumericAfter,
		const int64 NumericDelta)
	{
		const FBattleBattlerState* Battler = State.FindBattler(BattlerId);
		check(Battler != nullptr);
		FBattleEventSpec Spec;
		Spec.EventOrdinal = State.NextEventOrdinal;
		Spec.BattleId = State.Setup.GetBattleId();
		Spec.TurnId = State.TurnId;
		Spec.ActionId = ActionId;
		Spec.ResolutionId = ResolutionId;
		Spec.Type = Type;
		Spec.Cause = EBattleEventCause::Item;
		Spec.CauseActionKind = ActionKind;
		Spec.Source.TrainerId = Battler->TrainerId;
		Spec.Source.BattlerId = BattlerId;
		Spec.Source.ActiveSlotId = ActiveSlotId;
		Spec.Source.DefinitionId = ItemId.GetDefinitionId();
		FBattleEventTarget Target;
		Target.TrainerId = Battler->TrainerId;
		Target.BattlerId = BattlerId;
		Target.ActiveSlotId = ActiveSlotId;
		Spec.Targets.Add(Target);
		Spec.NumericBefore = NumericBefore;
		Spec.NumericAfter = NumericAfter;
		Spec.NumericDelta = NumericDelta;
		Spec.Visibility.Level = EBattleVisibilityLevel::Public;
		Spec.Visibility.bRevealSourceDefinition = true;
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
		TArray<FBattleEvent>& Events);

	template <typename TState>
	void AppendSwitchTransitionEvents(
		TState& State,
		const FResolutionId ResolutionId,
		const FBattleLockedActionState& Action,
		const FBattleEventTarget& Outgoing,
		const FBattleEventTarget& Incoming,
		TArray<FBattleEvent>& Events)
	{
		Events.Add(MakeTargetedActionEvent(
			State,
			ResolutionId,
			Action,
			EBattleEventType::LeftActiveSlot,
			EBattleEventCause::Switch,
			Outgoing));
		Events.Add(MakeTargetedActionEvent(
			State,
			ResolutionId,
			Action,
			EBattleEventType::SwitchTransientStateCleared,
			EBattleEventCause::Rule,
			Outgoing));
		Events.Add(MakeTargetedActionEvent(
			State,
			ResolutionId,
			Action,
			EBattleEventType::EnteredActiveSlot,
			EBattleEventCause::Switch,
			Incoming));
		Events.Add(MakeTargetedActionEvent(
			State,
			ResolutionId,
			Action,
			EBattleEventType::Switched,
			EBattleEventCause::Rule,
			Incoming));
	}

	template <typename TState>
	FBattleEvent MakeBattleEndedEvent(
		TState& State,
		const FResolutionId ResolutionId,
		const FBattleLockedActionState& Action,
		const EBattleOutcomeCause OutcomeCause,
		const FBattleEventSource* SourceOverride = nullptr)
	{
		FBattleEventSpec Spec;
		Spec.EventOrdinal = State.NextEventOrdinal;
		Spec.BattleId = State.Setup.GetBattleId();
		Spec.TurnId = State.TurnId;
		Spec.ActionId = Action.ActionId;
		Spec.ResolutionId = ResolutionId;
		Spec.Type = EBattleEventType::BattleEnded;
		Spec.Cause = EBattleEventCause::Outcome;
		Spec.CauseActionKind = Action.Decision.GetActionKind();
		Spec.OutcomeCause = OutcomeCause;
		Spec.Source = SourceOverride != nullptr
			? *SourceOverride
			: SourceFromLockedAction(State, Action);
		Spec.Visibility.Level = EBattleVisibilityLevel::Public;

		FBattleEvent Event;
		const bool bCreated = FBattleEvent::TryCreate(Spec, Event);
		check(bCreated);
		++State.NextEventOrdinal;
		return Event;
	}

	template <typename TState>
	void AppendPartnerTeamVictoryRecoveryEvent(
		TState& State,
		const FResolutionId ResolutionId,
		const FActionId ActionId,
		const EBattleActionKind ActionKind,
		const FBattleEventSource& Source,
		const FBattleFaintOutcomeResolution& FaintResolution,
		TArray<FBattleEvent>& Events)
	{
		if (!FaintResolution.PartnerTeamVictoryRecovery.IsSet())
		{
			return;
		}

		const FBattlePartnerTeamVictoryRecovery& Recovery =
			FaintResolution.PartnerTeamVictoryRecovery.GetValue();
		FBattleEventSpec Spec;
		Spec.EventOrdinal = State.NextEventOrdinal;
		Spec.BattleId = State.Setup.GetBattleId();
		Spec.TurnId = State.TurnId;
		Spec.ActionId = ActionId;
		Spec.ResolutionId = ResolutionId;
		Spec.Type = EBattleEventType::PartnerTeamVictoryRecovery;
		Spec.Cause = EBattleEventCause::Outcome;
		Spec.CauseActionKind = ActionKind;
		Spec.OutcomeCause = EBattleOutcomeCause::PartnerTeamVictory;
		Spec.Source = Source;
		Spec.Targets.Add(Recovery.Target);
		Spec.NumericBefore = Recovery.PreviousHP;
		Spec.NumericAfter = Recovery.NewHP;
		Spec.NumericDelta = Recovery.NewHP - Recovery.PreviousHP;
		Spec.Visibility.Level = EBattleVisibilityLevel::Public;

		FBattleEvent Event;
		const bool bCreated = FBattleEvent::TryCreate(Spec, Event);
		check(bCreated && Recovery.bMajorStatusCured);
		++State.NextEventOrdinal;
		Events.Add(MoveTemp(Event));
	}

	FBattleEventSpec MakeAtomicSwitchStagedEventSpec(const FBattleEvent& Event);
}
