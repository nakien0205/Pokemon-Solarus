#include "Battle/BattleFaintOutcomeResolver.h"

#include "Battle/BattleState.h"
#include "BattleMoveRedirection.h"

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

	const FBattleBattlerState* FindBattler(
		const TConstArrayView<FBattleBattlerState> Battlers,
		const FBattlerId BattlerId)
	{
		return Battlers.FindByPredicate(
			[BattlerId](const FBattleBattlerState& Candidate)
			{
				return Candidate.BattlerId == BattlerId;
			});
	}

	const FBattleActivePositionState* FindActivePosition(
		const TConstArrayView<FBattleActivePositionState> ActivePositions,
		const FActiveSlotId ActiveSlotId)
	{
		return ActivePositions.FindByPredicate(
			[ActiveSlotId](const FBattleActivePositionState& Candidate)
			{
				return Candidate.ActiveSlotId == ActiveSlotId;
			});
	}

	int32 CountUsableOnSide(
		const TConstArrayView<FBattleBattlerState> Battlers,
		const FBattleCompiledEncounterPolicies& CompiledEncounterPolicies,
		const EBattleSide Side)
	{
		int32 Count = 0;
		for (const FBattleBattlerState& Battler : Battlers)
		{
			const FBattleTrainerEncounterPolicy* TrainerPolicy =
				CompiledEncounterPolicies.FindTrainerPolicy(Battler.TrainerId);
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
		const TConstArrayView<FBattleBattlerState> Battlers,
		const FBattleCompiledEncounterPolicies& CompiledEncounterPolicies,
		const EBattleTrainerRole Role)
	{
		int32 Count = 0;
		for (const FBattleBattlerState& Battler : Battlers)
		{
			const FBattleTrainerEncounterPolicy* TrainerPolicy =
				CompiledEncounterPolicies.FindTrainerPolicy(Battler.TrainerId);
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
		const TConstArrayView<FBattleActiveAssignment> StartingActive,
		const FActiveSlotId ActiveSlotId)
	{
		for (const FBattleActiveAssignment& Assignment : StartingActive)
		{
			if (Assignment.ActiveSlotId == ActiveSlotId)
			{
				return Assignment.TrainerId;
			}
		}
		return FTrainerId();
	}

	int32 CountLivingInactiveForTrainer(
		const TConstArrayView<FBattleBattlerState> Battlers,
		const TConstArrayView<FBattleActivePositionState> ActivePositions,
		const FTrainerId TrainerId)
	{
		int32 Count = 0;
		for (const FBattleBattlerState& Battler : Battlers)
		{
			if (Battler.TrainerId != TrainerId || !IsUsable(Battler))
			{
				continue;
			}
			const bool bActive = ActivePositions.ContainsByPredicate(
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
	return TryResolveAction(
		EffectResult,
		TargetClass,
		ResolutionId,
		State.Battlers,
		State.ActivePositions,
		State.MoveRedirectionRegistrations,
		State.AllyActionPowerModifierRegistrations,
		State.CompiledEncounterPolicies,
		OutPlan);
}

bool FBattleFaintOutcomeResolver::TryResolveAction(
	const FBattleEffectExecutionResult& EffectResult,
	const EBattleTargetClass TargetClass,
	const FResolutionId ResolutionId,
	const TConstArrayView<FBattleBattlerState> Battlers,
	const TConstArrayView<FBattleActivePositionState> ActivePositions,
	const TConstArrayView<FBattleMoveRedirectionRegistration> MoveRedirections,
	const TConstArrayView<FBattleAllyActionPowerModifierRegistration>
		AllyActionPowerModifiers,
	const FBattleCompiledEncounterPolicies& CompiledEncounterPolicies,
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
		const FBattleBattlerState* Battler = FindBattler(Battlers, Target.BattlerId);
		const FBattleActivePositionState* Position = FindActivePosition(
			ActivePositions,
			Target.ActiveSlotId);
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
	for (const FBattleBattlerState& Battler : Battlers)
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
	if (TargetClass == EBattleTargetClass::FixedSpreadSet
		|| TargetClass == EBattleTargetClass::FixedOpponentSpreadSet)
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
	OutPlan.MoveRedirectionsAfter.Reserve(MoveRedirections.Num());
	for (const FBattleMoveRedirectionRegistration& Registration : MoveRedirections)
	{
		OutPlan.MoveRedirectionsAfter.Add(Registration);
	}
	OutPlan.AllyActionPowerModifiersAfter.Reserve(AllyActionPowerModifiers.Num());
	for (const FBattleAllyActionPowerModifierRegistration& Registration :
		AllyActionPowerModifiers)
	{
		OutPlan.AllyActionPowerModifiersAfter.Add(Registration);
	}
	for (const FBattleFaintTransitionRecord& Removal : OutResolution.Removals)
	{
		FBattleMoveRedirection::RemoveForOccupant(
			OutPlan.MoveRedirectionsAfter,
			{Removal.Target.ActiveSlotId, Removal.Target.BattlerId});
		FBattleAllyActionPowerModifier::RemoveForOccupant(
			OutPlan.AllyActionPowerModifiersAfter,
			{Removal.Target.ActiveSlotId, Removal.Target.BattlerId});
	}
	bool bPlayerFaintedThisAction = false;
	bool bOpponentFaintedThisAction = false;
	for (const FBattleFaintTransitionRecord& Transition : OutResolution.Faints)
	{
		bPlayerFaintedThisAction |=
			Transition.Target.ActiveSlotId.GetSide() == EBattleSide::Player;
		bOpponentFaintedThisAction |=
			Transition.Target.ActiveSlotId.GetSide() == EBattleSide::Opponent;
	}

	const int32 PlayerUsable = CountUsableOnSide(
		Battlers,
		CompiledEncounterPolicies,
		EBattleSide::Player);
	const int32 OpponentUsable = CountUsableOnSide(
		Battlers,
		CompiledEncounterPolicies,
		EBattleSide::Opponent);
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
			CompiledEncounterPolicies.HasSeparatePartnerOwnership()
			&& CountUsableForRole(
				Battlers,
				CompiledEncounterPolicies,
				EBattleTrainerRole::Player) == 0
			&& CountUsableForRole(
				Battlers,
				CompiledEncounterPolicies,
				EBattleTrainerRole::Partner) > 0;
		OutResolution.OutcomeCause = bOnlyPartnerRemains
			? EBattleOutcomeCause::PartnerTeamVictory
			: EBattleOutcomeCause::Ordinary;
		if (bOnlyPartnerRemains)
		{
			FBattlePartnerTeamVictoryRecoveryPlan RecoveryPlan;
			if (!FBattlePartnerFlow::TryApplyTeamVictoryRecovery(
					Battlers,
					CompiledEncounterPolicies,
					RecoveryPlan))
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
		FBattleAllyActionPowerModifier::Clear(
			OutPlan.AllyActionPowerModifiersAfter);
	}
	return true;
}

bool FBattleFaintOutcomeResolver::TryApplyActionPlan(
	FBattleEngineState& State,
	const FBattleFaintOutcomePlan& Plan)
{
	return TryApplyActionPlan(
		State.Battlers,
		State.ActivePositions,
		State.MoveRedirectionRegistrations,
		State.AllyActionPowerModifierRegistrations,
		State.Phase,
		State.Outcome,
		State.OutcomeCause,
		State.PendingDecision,
		State.PendingDecisionRequests,
		Plan);
}

bool FBattleFaintOutcomeResolver::TryApplyActionPlan(
	TArray<FBattleBattlerState>& Battlers,
	TArray<FBattleActivePositionState>& ActivePositions,
	TArray<FBattleMoveRedirectionRegistration>& MoveRedirections,
	TArray<FBattleAllyActionPowerModifierRegistration>& AllyActionPowerModifiers,
	EBattlePhase& Phase,
	EBattleOutcome& Outcome,
	EBattleOutcomeCause& OutcomeCause,
	TOptional<FBattleDecisionRequest>& PendingDecision,
	TArray<FBattleDecisionRequest>& PendingDecisionRequests,
	const FBattleFaintOutcomePlan& Plan)
{
	if (!IsActionPlanApplicable(
			Battlers,
			ActivePositions,
			MoveRedirections,
			AllyActionPowerModifiers,
			Plan))
	{
		return false;
	}
	ApplyPreparedActionPlan(
		Battlers,
		ActivePositions,
		MoveRedirections,
		AllyActionPowerModifiers,
		Phase,
		Outcome,
		OutcomeCause,
		PendingDecision,
		PendingDecisionRequests,
		Plan);
	return true;
}

bool FBattleFaintOutcomeResolver::IsActionPlanApplicable(
	const FBattleEngineState& State,
	const FBattleFaintOutcomePlan& Plan)
{
	return IsActionPlanApplicable(
		State.Battlers,
		State.ActivePositions,
		State.MoveRedirectionRegistrations,
		State.AllyActionPowerModifierRegistrations,
		Plan);
}

bool FBattleFaintOutcomeResolver::IsActionPlanApplicable(
	const TConstArrayView<FBattleBattlerState> Battlers,
	const TConstArrayView<FBattleActivePositionState> ActivePositions,
	const TConstArrayView<FBattleMoveRedirectionRegistration> MoveRedirections,
	const TConstArrayView<FBattleAllyActionPowerModifierRegistration>
		AllyActionPowerModifiers,
	const FBattleFaintOutcomePlan& Plan)
{
	const FBattleFaintOutcomeResolution& Resolution = Plan.Resolution;
	if (Plan.PartnerRecoveryPlan.IsSet()
			!= Resolution.PartnerTeamVictoryRecovery.IsSet()
		|| (Resolution.bBattleEnded
			&& (Resolution.Outcome == EBattleOutcome::InProgress
				|| Resolution.OutcomeCause == EBattleOutcomeCause::None))
		|| (!Resolution.bBattleEnded
			&& (Resolution.Outcome != EBattleOutcome::InProgress
				|| Resolution.OutcomeCause != EBattleOutcomeCause::None)))
	{
		return false;
	}

	TSet<FBattlerId> SeenBattlerIds;
	TSet<FActiveSlotId> SeenActiveSlotIds;
	for (int32 RemovalIndex = 0; RemovalIndex < Resolution.Removals.Num(); ++RemovalIndex)
	{
		const FBattleFaintTransitionRecord& Transition = Resolution.Removals[RemovalIndex];
		const FBattleBattlerState* Battler = FindBattler(
			Battlers,
			Transition.Target.BattlerId);
		const FBattleActivePositionState* Position = FindActivePosition(
			ActivePositions,
			Transition.Target.ActiveSlotId);
		if (SeenBattlerIds.Contains(Transition.Target.BattlerId)
			|| SeenActiveSlotIds.Contains(Transition.Target.ActiveSlotId)
			|| Battler == nullptr
			|| Position == nullptr
			|| Battler->BattlerId != Transition.Target.BattlerId
			|| Position->ActiveSlotId != Transition.Target.ActiveSlotId
			|| Position->TrainerId != Transition.Target.TrainerId
			|| Position->BattlerId != Transition.Target.BattlerId
			|| Battler->CurrentHP != 0
			|| !Battler->bFainted
			|| !Battler->bFaintTransitionPending)
		{
			return false;
		}
		SeenBattlerIds.Add(Transition.Target.BattlerId);
		SeenActiveSlotIds.Add(Transition.Target.ActiveSlotId);
	}

	if (Plan.PartnerRecoveryPlan.IsSet()
		&& !FBattlePartnerFlow::IsTeamVictoryRecoveryPlanApplicable(
			Battlers,
			Plan.PartnerRecoveryPlan.GetValue()))
	{
		return false;
	}

	TArray<FBattleMoveRedirectionRegistration> ExpectedMoveRedirections;
	ExpectedMoveRedirections.Reserve(MoveRedirections.Num());
	for (const FBattleMoveRedirectionRegistration& Registration : MoveRedirections)
	{
		ExpectedMoveRedirections.Add(Registration);
	}
	for (const FBattleFaintTransitionRecord& Removal : Resolution.Removals)
	{
		FBattleMoveRedirection::RemoveForOccupant(
			ExpectedMoveRedirections,
			{Removal.Target.ActiveSlotId, Removal.Target.BattlerId});
	}
	if (!FBattleMoveRedirection::AreRegistrationsIdentical(
			ExpectedMoveRedirections,
			Plan.MoveRedirectionsAfter))
	{
		return false;
	}
	TArray<FBattleAllyActionPowerModifierRegistration>
		ExpectedAllyActionPowerModifiers;
	ExpectedAllyActionPowerModifiers.Reserve(AllyActionPowerModifiers.Num());
	for (const FBattleAllyActionPowerModifierRegistration& Registration :
		AllyActionPowerModifiers)
	{
		ExpectedAllyActionPowerModifiers.Add(Registration);
	}
	for (const FBattleFaintTransitionRecord& Removal : Resolution.Removals)
	{
		FBattleAllyActionPowerModifier::RemoveForOccupant(
			ExpectedAllyActionPowerModifiers,
			{Removal.Target.ActiveSlotId, Removal.Target.BattlerId});
	}
	if (Resolution.bBattleEnded)
	{
		FBattleAllyActionPowerModifier::Clear(
			ExpectedAllyActionPowerModifiers);
	}
	if (!FBattleAllyActionPowerModifier::AreRegistrationsIdentical(
			ExpectedAllyActionPowerModifiers,
			Plan.AllyActionPowerModifiersAfter))
	{
		return false;
	}
	return true;
}

void FBattleFaintOutcomeResolver::ApplyPreparedActionPlan(
	FBattleEngineState& State,
	const FBattleFaintOutcomePlan& Plan)
{
	ApplyPreparedActionPlan(
		State.Battlers,
		State.ActivePositions,
		State.MoveRedirectionRegistrations,
		State.AllyActionPowerModifierRegistrations,
		State.Phase,
		State.Outcome,
		State.OutcomeCause,
		State.PendingDecision,
		State.PendingDecisionRequests,
		Plan);
}

void FBattleFaintOutcomeResolver::ApplyPreparedActionPlan(
	TArray<FBattleBattlerState>& Battlers,
	TArray<FBattleActivePositionState>& ActivePositions,
	TArray<FBattleMoveRedirectionRegistration>& MoveRedirections,
	TArray<FBattleAllyActionPowerModifierRegistration>& AllyActionPowerModifiers,
	EBattlePhase& Phase,
	EBattleOutcome& Outcome,
	EBattleOutcomeCause& OutcomeCause,
	TOptional<FBattleDecisionRequest>& PendingDecision,
	TArray<FBattleDecisionRequest>& PendingDecisionRequests,
	const FBattleFaintOutcomePlan& Plan)
{
	const FBattleFaintOutcomeResolution& Resolution = Plan.Resolution;
	MoveRedirections = Plan.MoveRedirectionsAfter;
	AllyActionPowerModifiers = Plan.AllyActionPowerModifiersAfter;
	for (const FBattleFaintTransitionRecord& Transition : Resolution.Removals)
	{
		FBattleBattlerState* Battler = Battlers.FindByPredicate(
			[&Transition](const FBattleBattlerState& Candidate)
			{
				return Candidate.BattlerId == Transition.Target.BattlerId;
			});
		FBattleActivePositionState* Position = ActivePositions.FindByPredicate(
			[&Transition](const FBattleActivePositionState& Candidate)
			{
				return Candidate.ActiveSlotId == Transition.Target.ActiveSlotId;
			});
		check(Battler != nullptr && Position != nullptr);
		Battler->MajorStatusId = FConditionId();
		Battler->Stages = FBattleStatStages();
		Battler->Volatiles.Reset();
		Battler->bRemoved = true;
		Battler->bFaintTransitionPending = false;
		Position->TrainerId = FTrainerId();
		Position->BattlerId = FBattlerId();
	}

	if (Plan.PartnerRecoveryPlan.IsSet())
	{
		FBattlePartnerFlow::ApplyPreparedTeamVictoryRecoveryPlan(
			Battlers,
			Plan.PartnerRecoveryPlan.GetValue());
	}

	if (Resolution.bBattleEnded)
	{
		Phase = EBattlePhase::Terminal;
		Outcome = Resolution.Outcome;
		OutcomeCause = Resolution.OutcomeCause;
		PendingDecision.Reset();
		PendingDecisionRequests.Reset();
	}
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
	return ResolveQueueBoundary(
		State.Phase,
		State.Outcome,
		State.CurrentLockedActionIndex,
		State.LockedActions.Num(),
		State.Setup.GetStartingActive(),
		State.Battlers,
		State.ActivePositions,
		OutPlan);
}

bool FBattleFaintOutcomeResolver::ResolveQueueBoundary(
	const EBattlePhase Phase,
	const EBattleOutcome Outcome,
	const int32 CurrentLockedActionIndex,
	const int32 LockedActionCount,
	const TConstArrayView<FBattleActiveAssignment> StartingActive,
	const TConstArrayView<FBattleBattlerState> Battlers,
	const TConstArrayView<FBattleActivePositionState> ActivePositions,
	FBattleQueueBoundaryPlan& OutPlan)
{
	OutPlan = FBattleQueueBoundaryPlan();
	OutPlan.PhaseAfter = Phase;
	if (Outcome != EBattleOutcome::InProgress
		|| Phase != EBattlePhase::Resolving
		|| CurrentLockedActionIndex < LockedActionCount)
	{
		return true;
	}

	TArray<FActiveSlotId> EmptyPositions;
	for (const FBattleActivePositionState& Position : ActivePositions)
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
		const FTrainerId TrainerId = FindInitialSlotOwner(StartingActive, ActiveSlotId);
		if (!TrainerId.IsValid())
		{
			continue;
		}
		int32* Remaining = RemainingReserves.Find(TrainerId);
		if (Remaining == nullptr)
		{
			Remaining = &RemainingReserves.Add(
				TrainerId,
				CountLivingInactiveForTrainer(Battlers, ActivePositions, TrainerId));
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
	return TryApplyQueueBoundaryPlan(State.Phase, Plan);
}

bool FBattleFaintOutcomeResolver::TryApplyQueueBoundaryPlan(
	EBattlePhase& Phase,
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
	Phase = Plan.PhaseAfter;
	return true;
}
