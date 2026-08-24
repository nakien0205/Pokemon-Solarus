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
			const FBattleTrainerState* Trainer = State.FindTrainer(Battler.TrainerId);
			if (Trainer != nullptr && Trainer->Side == Side && IsUsable(Battler))
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
			const FBattleTrainerState* Trainer = State.FindTrainer(Battler.TrainerId);
			if (Trainer != nullptr && Trainer->Role == Role && IsUsable(Battler))
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
	OutResolution = FBattleFaintOutcomeResolution();
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

	for (const FBattleFaintTransitionRecord& Transition : OutResolution.Removals)
	{
		FBattleBattlerState* Battler = State.FindMutableBattler(Transition.Target.BattlerId);
		FBattleActivePositionState* Position = State.FindMutableActivePosition(
			Transition.Target.ActiveSlotId);
		check(Battler != nullptr && Position != nullptr);
		Battler->MajorStatusId = FConditionId();
		Battler->Stages = FBattleStatStages();
		Battler->Volatiles.Reset();
		Battler->bRemoved = true;
		Battler->bFaintTransitionPending = false;
		Position->TrainerId = FTrainerId();
		Position->BattlerId = FBattlerId();
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
		const bool bOnlyPartnerRemains = CountUsableForRole(State, EBattleTrainerRole::Player) == 0
			&& CountUsableForRole(State, EBattleTrainerRole::Partner) > 0;
		OutResolution.OutcomeCause = bOnlyPartnerRemains
			? EBattleOutcomeCause::PartnerTeamVictory
			: EBattleOutcomeCause::Ordinary;
		if (bOnlyPartnerRemains)
		{
			FBattlePartnerTeamVictoryRecovery Recovery;
			if (!FBattlePartnerFlow::TryApplyTeamVictoryRecovery(State, Recovery))
			{
				OutResolution = FBattleFaintOutcomeResolution();
				return false;
			}
			OutResolution.PartnerTeamVictoryRecovery = MoveTemp(Recovery);
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
		State.Phase = EBattlePhase::Terminal;
		State.Outcome = OutResolution.Outcome;
		State.OutcomeCause = OutResolution.OutcomeCause;
		State.PendingDecision.Reset();
		State.PendingDecisionRequests.Reset();
	}
	return true;
}

void FBattleFaintOutcomeResolver::ResolveQueueBoundary(
	FBattleEngineState& State,
	TArray<FBattleReplacementRequirement>& OutRequirements)
{
	OutRequirements.Reset();
	if (State.Outcome != EBattleOutcome::InProgress
		|| State.Phase != EBattlePhase::Resolving
		|| State.CurrentLockedActionIndex < State.LockedActions.Num())
	{
		return;
	}

	TArray<const FBattleActivePositionState*> EmptyPositions;
	for (const FBattleActivePositionState& Position : State.ActivePositions)
	{
		if (Position.bAvailable && !Position.BattlerId.IsValid())
		{
			EmptyPositions.Add(&Position);
		}
	}
	EmptyPositions.Sort(
		[](const FBattleActivePositionState& Left, const FBattleActivePositionState& Right)
		{
			return FaintActiveSlotLess(Left.ActiveSlotId, Right.ActiveSlotId);
		});

	TMap<FTrainerId, int32> RemainingReserves;
	for (const FBattleActivePositionState* Position : EmptyPositions)
	{
		const FTrainerId TrainerId = FindInitialSlotOwner(State, Position->ActiveSlotId);
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
		FBattleReplacementRequirement& Requirement = OutRequirements.AddDefaulted_GetRef();
		Requirement.Target.TrainerId = TrainerId;
		Requirement.Target.ActiveSlotId = Position->ActiveSlotId;
	}

	State.Phase = OutRequirements.IsEmpty()
		? EBattlePhase::EndOfTurn
		: EBattlePhase::MandatoryReplacement;
}
