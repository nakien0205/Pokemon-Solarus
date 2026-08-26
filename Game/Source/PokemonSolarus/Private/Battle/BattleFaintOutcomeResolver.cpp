#include "Battle/BattleFaintOutcomeResolver.h"

#include "Battle/BattleState.h"

namespace
{
	bool FaintActiveSlotLess(const FActiveSlotId Left, const FActiveSlotId Right)
	{
		if (Left.GetSide() != Right.GetSide())
		{
			return static_cast<uint8>(Left.GetSide()) < static_cast<uint8>(Right.GetSide());
		}
		return static_cast<uint8>(Left.GetPosition())
			< static_cast<uint8>(Right.GetPosition());
	}

	bool IsUsable(const FBattleBattlerState& Battler)
	{
		return !Battler.bEgg
			&& !Battler.bFainted
			&& !Battler.bCaptured
			&& !Battler.bRemoved;
	}

	bool IsZeroHpTransition(const FBattleEffectExecutionEvent& Event)
	{
		return Event.Type == EBattleEventType::HPChanged
			&& Event.Targets.Num() == 1
			&& Event.Targets[0].TrainerId.IsValid()
			&& Event.Targets[0].BattlerId.IsValid()
			&& Event.Targets[0].ActiveSlotId.IsValid()
			&& Event.NumericBefore.IsSet()
			&& Event.NumericAfter.IsSet()
			&& Event.NumericDelta.IsSet()
			&& Event.NumericBefore.GetValue() > 0
			&& Event.NumericAfter.GetValue() == 0
			&& Event.NumericDelta.GetValue() < 0;
	}

	bool IsMatchingDamageEvent(
		const FBattleEffectExecutionEvent& Damage,
		const FBattleEffectExecutionEvent& HpChanged)
	{
		return Damage.Type == EBattleEventType::Damage
			&& Damage.Targets.Num() == 1
			&& Damage.Targets[0].BattlerId == HpChanged.Targets[0].BattlerId
			&& Damage.Targets[0].ActiveSlotId == HpChanged.Targets[0].ActiveSlotId
			&& Damage.NumericBefore == HpChanged.NumericBefore
			&& Damage.NumericAfter == HpChanged.NumericAfter
			&& Damage.NumericDelta == HpChanged.NumericDelta;
	}

	int32 CountUsableOnSide(const FBattleEngineState& State, const EBattleSide Side)
	{
		int32 Count = 0;
		for (const FBattleBattlerState& Battler : State.Battlers)
		{
			const FBattleTrainerEncounterPolicy* TrainerPolicy =
				State.CompiledEncounterPolicies.FindTrainerPolicy(Battler.TrainerId);
			if (TrainerPolicy != nullptr
				&& TrainerPolicy->Side == Side
				&& IsUsable(Battler))
			{
				++Count;
			}
		}
		return Count;
	}

	int32 CountUsableForRole(
		const FBattleEngineState& State,
		const EBattleTrainerRole Role)
	{
		int32 Count = 0;
		for (const FBattleBattlerState& Battler : State.Battlers)
		{
			const FBattleTrainerEncounterPolicy* TrainerPolicy =
				State.CompiledEncounterPolicies.FindTrainerPolicy(Battler.TrainerId);
			if (TrainerPolicy != nullptr
				&& TrainerPolicy->Role == Role
				&& IsUsable(Battler))
			{
				++Count;
			}
		}
		return Count;
	}

	FTrainerId FindInitialSlotOwner(
		const FBattleEngineState& State,
		const FActiveSlotId ActiveSlotId)
	{
		for (const FBattleActiveAssignment& Assignment : State.Setup.GetStartingActive())
		{
			if (Assignment.ActiveSlotId == ActiveSlotId)
			{
				return Assignment.TrainerId;
			}
		}
		return FTrainerId();
	}

	int32 CountLivingInactiveForTrainer(
		const FBattleEngineState& State,
		const FTrainerId TrainerId)
	{
		int32 Count = 0;
		for (const FBattleBattlerState& Battler : State.Battlers)
		{
			if (Battler.TrainerId != TrainerId || !IsUsable(Battler))
			{
				continue;
			}
			const bool bActive = State.ActivePositions.ContainsByPredicate(
				[&Battler](const FBattleActivePositionState& Position)
				{
					return Position.BattlerId == Battler.BattlerId;
				});
			if (!bActive)
			{
				++Count;
			}
		}
		return Count;
	}
}

bool FBattleFaintOutcomeResolver::TryResolveAction(
	const FBattleEffectExecutionResult& EffectResult,
	const EBattleTargetClass TargetClass,
	const FResolutionId ResolutionId,
	FBattleEngineState& State,
	FBattleFaintOutcomeResolution& OutResolution)
{
	FBattleFaintOutcomePlan Plan;
	if (!TryResolveAction(
			EffectResult,
			TargetClass,
			ResolutionId,
			static_cast<const FBattleEngineState&>(State),
			Plan)
		|| !TryApplyActionPlan(State, Plan))
	{
		OutResolution = FBattleFaintOutcomeResolution();
		return false;
	}
	OutResolution = Plan.Resolution;
	return true;
}

bool FBattleFaintOutcomeResolver::TryResolveAction(
	const FBattleEffectExecutionResult& EffectResult,
	const EBattleTargetClass TargetClass,
	const FResolutionId ResolutionId,
	const FBattleEngineState& State,
	FBattleFaintOutcomePlan& OutPlan)
{
	OutPlan = FBattleFaintOutcomePlan();
	FBattleFaintOutcomeResolution& OutResolution = OutPlan.Resolution;
	if (!EffectResult.bValid || !ResolutionId.IsValid())
	{
		return false;
	}

	TSet<FBattlerId> SeenBattlers;
	for (int32 EventIndex = 0; EventIndex < EffectResult.Events.Num(); ++EventIndex)
	{
		const FBattleEffectExecutionEvent& Event = EffectResult.Events[EventIndex];
		if (!IsZeroHpTransition(Event))
		{
			continue;
		}

		const FBattleEventTarget& Target = Event.Targets[0];
		const FBattleBattlerState* Battler = State.FindBattler(Target.BattlerId);
		const FBattleActivePositionState* Position = State.FindActivePosition(Target.ActiveSlotId);
		if (SeenBattlers.Contains(Target.BattlerId)
			|| Battler == nullptr
			|| Position == nullptr
			|| Position->TrainerId != Target.TrainerId
			|| Position->BattlerId != Target.BattlerId
			|| Battler->CurrentHP != 0
			|| !Battler->bFainted
			|| !Battler->bFaintTransitionPending)
		{
			OutResolution = FBattleFaintOutcomeResolution();
			return false;
		}

		SeenBattlers.Add(Target.BattlerId);
		FBattleFaintTransitionRecord& Transition = OutResolution.Faints.AddDefaulted_GetRef();
		Transition.EffectEventIndex = EventIndex;
		Transition.Target = Target;
		Transition.HitIndex = Event.HitIndex;
		Transition.HitCount = Event.HitCount;
	}

	int32 PendingFaintCount = 0;
	for (const FBattleBattlerState& Battler : State.Battlers)
	{
		if (Battler.bFaintTransitionPending)
		{
			++PendingFaintCount;
		}
	}
	if (PendingFaintCount != OutResolution.Faints.Num())
	{
		OutResolution = FBattleFaintOutcomeResolution();
		return false;
	}

	TArray<int32> DirectSpreadFaintIndexes;
	if (TargetClass == EBattleTargetClass::FixedSpreadSet)
	{
		for (int32 FaintIndex = 0; FaintIndex < OutResolution.Faints.Num(); ++FaintIndex)
		{
			if (OutResolution.Faints[FaintIndex].HitIndex.IsSet())
			{
				DirectSpreadFaintIndexes.Add(FaintIndex);
			}
		}
	}
	if (DirectSpreadFaintIndexes.Num() > 1)
	{
		const uint64 GroupId = ResolutionId.GetValue();
		for (const int32 FaintIndex : DirectSpreadFaintIndexes)
		{
			FBattleFaintTransitionRecord& Transition = OutResolution.Faints[FaintIndex];
			Transition.SimultaneousGroupId = GroupId;
			OutResolution.SimultaneousGroupsByEffectEvent.Add(
				Transition.EffectEventIndex,
				GroupId);
			if (Transition.EffectEventIndex > 0
				&& IsMatchingDamageEvent(
					EffectResult.Events[Transition.EffectEventIndex - 1],
					EffectResult.Events[Transition.EffectEventIndex]))
			{
				OutResolution.SimultaneousGroupsByEffectEvent.Add(
					Transition.EffectEventIndex - 1,
					GroupId);
			}
		}
	}

	OutResolution.Removals = OutResolution.Faints;
	OutResolution.Removals.Sort(
		[](const FBattleFaintTransitionRecord& Left, const FBattleFaintTransitionRecord& Right)
		{
			return FaintActiveSlotLess(Left.Target.ActiveSlotId, Right.Target.ActiveSlotId);
		});

	bool bPlayerFaintedThisAction = false;
	bool bOpponentFaintedThisAction = false;
	for (const FBattleFaintTransitionRecord& Transition : OutResolution.Faints)
	{
		bPlayerFaintedThisAction |=
			Transition.Target.ActiveSlotId.GetSide() == EBattleSide::Player;
		bOpponentFaintedThisAction |=
			Transition.Target.ActiveSlotId.GetSide() == EBattleSide::Opponent;
	}

	const int32 PlayerUsable = CountUsableOnSide(State, EBattleSide::Player);
	const int32 OpponentUsable = CountUsableOnSide(State, EBattleSide::Opponent);
	if (PlayerUsable == 0
		&& OpponentUsable == 0
		&& bPlayerFaintedThisAction
		&& bOpponentFaintedThisAction)
	{
		OutResolution.Outcome = EBattleOutcome::Defeat;
		OutResolution.OutcomeCause = EBattleOutcomeCause::SimultaneousFaint;
	}
	else if (OpponentUsable == 0)
	{
		OutResolution.Outcome = EBattleOutcome::Victory;
		const bool bOnlyPartnerRemains =
			State.CompiledEncounterPolicies.HasSeparatePartnerOwnership()
			&& CountUsableForRole(State, EBattleTrainerRole::Player) == 0
			&& CountUsableForRole(State, EBattleTrainerRole::Partner) > 0;
		OutResolution.OutcomeCause = bOnlyPartnerRemains
			? EBattleOutcomeCause::PartnerTeamVictory
			: EBattleOutcomeCause::Ordinary;
		if (bOnlyPartnerRemains)
		{
			FBattlePartnerTeamVictoryRecoveryPlan RecoveryPlan;
			if (!FBattlePartnerFlow::TryApplyTeamVictoryRecovery(State, RecoveryPlan))
			{
				OutPlan = FBattleFaintOutcomePlan();
				return false;
			}
			OutResolution.PartnerTeamVictoryRecovery = RecoveryPlan.Recovery;
			OutPlan.PartnerRecoveryPlan = MoveTemp(RecoveryPlan);
		}
	}
	else if (PlayerUsable == 0)
	{
		OutResolution.Outcome = EBattleOutcome::Defeat;
		OutResolution.OutcomeCause = EBattleOutcomeCause::Ordinary;
	}

	if (OutResolution.Outcome != EBattleOutcome::InProgress)
	{
		OutResolution.bBattleEnded = true;
	}
	return true;
}

bool FBattleFaintOutcomeResolver::TryApplyActionPlan(
	FBattleEngineState& State,
	const FBattleFaintOutcomePlan& Plan)
{
	const FBattleFaintOutcomeResolution& Resolution = Plan.Resolution;
	for (const FBattleFaintTransitionRecord& Transition : Resolution.Removals)
	{
		const FBattleBattlerState* Battler = State.FindBattler(
			Transition.Target.BattlerId);
		const FBattleActivePositionState* Position = State.FindActivePosition(
			Transition.Target.ActiveSlotId);
		if (Battler == nullptr
			|| Position == nullptr
			|| Position->TrainerId != Transition.Target.TrainerId
			|| Position->BattlerId != Transition.Target.BattlerId
			|| Battler->CurrentHP != 0
			|| !Battler->bFainted
			|| !Battler->bFaintTransitionPending)
		{
			return false;
		}
	}

	for (const FBattleFaintTransitionRecord& Transition : Resolution.Removals)
	{
		FBattleBattlerState* Battler = State.FindMutableBattler(
			Transition.Target.BattlerId);
		FBattleActivePositionState* Position = State.FindMutableActivePosition(
			Transition.Target.ActiveSlotId);
		if (Battler == nullptr || Position == nullptr)
		{
			return false;
		}
		Battler->MajorStatusId = FConditionId();
		Battler->Stages = FBattleStatStages();
		Battler->Volatiles.Reset();
		Battler->bRemoved = true;
		Battler->bFaintTransitionPending = false;
		Position->TrainerId = FTrainerId();
		Position->BattlerId = FBattlerId();
	}

	if (Plan.PartnerRecoveryPlan.IsSet()
		&& !FBattlePartnerFlow::TryApplyTeamVictoryRecoveryPlan(
			State,
			Plan.PartnerRecoveryPlan.GetValue()))
	{
		return false;
	}
	if (Plan.PartnerRecoveryPlan.IsSet()
		!= Resolution.PartnerTeamVictoryRecovery.IsSet())
	{
		return false;
	}

	if (Resolution.bBattleEnded)
	{
		if (Resolution.Outcome == EBattleOutcome::InProgress
			|| Resolution.OutcomeCause == EBattleOutcomeCause::None)
		{
			return false;
		}
		State.Phase = EBattlePhase::Terminal;
		State.Outcome = Resolution.Outcome;
		State.OutcomeCause = Resolution.OutcomeCause;
		State.PendingDecision.Reset();
		State.PendingDecisionRequests.Reset();
	}
	return true;
}

void FBattleFaintOutcomeResolver::ResolveQueueBoundary(
	FBattleEngineState& State,
	TArray<FBattleReplacementRequirement>& OutRequirements)
{
	FBattleQueueBoundaryPlan Plan;
	if (!ResolveQueueBoundary(static_cast<const FBattleEngineState&>(State), Plan)
		|| !TryApplyQueueBoundaryPlan(State, Plan))
	{
		OutRequirements.Reset();
		return;
	}
	OutRequirements = MoveTemp(Plan.Requirements);
}

bool FBattleFaintOutcomeResolver::ResolveQueueBoundary(
	const FBattleEngineState& State,
	FBattleQueueBoundaryPlan& OutPlan)
{
	OutPlan = FBattleQueueBoundaryPlan();
	OutPlan.PhaseAfter = State.Phase;
	if (State.Outcome != EBattleOutcome::InProgress
		|| State.Phase != EBattlePhase::Resolving
		|| State.CurrentLockedActionIndex < State.LockedActions.Num())
	{
		return true;
	}

	TArray<FActiveSlotId> EmptyPositions;
	for (const FBattleActivePositionState& Position : State.ActivePositions)
	{
		if (Position.bAvailable && !Position.BattlerId.IsValid())
		{
			EmptyPositions.Add(Position.ActiveSlotId);
		}
	}
	EmptyPositions.Sort(
		[](const FActiveSlotId Left, const FActiveSlotId Right)
		{
			return FaintActiveSlotLess(Left, Right);
		});

	TMap<FTrainerId, int32> RemainingReserves;
	for (const FActiveSlotId ActiveSlotId : EmptyPositions)
	{
		const FTrainerId TrainerId = FindInitialSlotOwner(State, ActiveSlotId);
		if (!TrainerId.IsValid())
		{
			continue;
		}
		int32* Remaining = RemainingReserves.Find(TrainerId);
		if (Remaining == nullptr)
		{
			Remaining = &RemainingReserves.Add(
				TrainerId,
				CountLivingInactiveForTrainer(State, TrainerId));
		}
		if (*Remaining <= 0)
		{
			continue;
		}

		--(*Remaining);
		FBattleReplacementRequirement& Requirement =
			OutPlan.Requirements.AddDefaulted_GetRef();
		Requirement.Target.TrainerId = TrainerId;
		Requirement.Target.ActiveSlotId = ActiveSlotId;
	}

	OutPlan.PhaseAfter = OutPlan.Requirements.IsEmpty()
		? EBattlePhase::EndOfTurn
		: EBattlePhase::MandatoryReplacement;
	return true;
}

bool FBattleFaintOutcomeResolver::TryApplyQueueBoundaryPlan(
	FBattleEngineState& State,
	const FBattleQueueBoundaryPlan& Plan)
{
	if (Plan.PhaseAfter != EBattlePhase::Resolving
		&& Plan.PhaseAfter != EBattlePhase::EndOfTurn
		&& Plan.PhaseAfter != EBattlePhase::MandatoryReplacement
		&& Plan.PhaseAfter != EBattlePhase::Terminal)
	{
		return false;
	}
	for (const FBattleReplacementRequirement& Requirement : Plan.Requirements)
	{
		if (!Requirement.Target.TrainerId.IsValid()
			|| !Requirement.Target.ActiveSlotId.IsValid())
		{
			return false;
		}
	}
	State.Phase = Plan.PhaseAfter;
	return true;
}
