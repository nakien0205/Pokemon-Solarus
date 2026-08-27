#include "Battle/BattlePartnerFlow.h"

#include "Battle/BattleState.h"

bool FBattlePartnerFlow::TryApplyTeamVictoryRecovery(
	FBattleEngineState& State,
	FBattlePartnerTeamVictoryRecovery& OutRecovery)
{
	FBattlePartnerTeamVictoryRecoveryPlan Plan;
	if (!TryApplyTeamVictoryRecovery(
			static_cast<const FBattleEngineState&>(State),
			Plan)
		|| !TryApplyTeamVictoryRecoveryPlan(State, Plan))
	{
		OutRecovery = FBattlePartnerTeamVictoryRecovery();
		return false;
	}
	OutRecovery = Plan.Recovery;
	return true;
}

bool FBattlePartnerFlow::TryApplyTeamVictoryRecovery(
	const FBattleEngineState& State,
	FBattlePartnerTeamVictoryRecoveryPlan& OutPlan)
{
	return TryApplyTeamVictoryRecovery(
		State.Battlers,
		State.CompiledEncounterPolicies,
		OutPlan);
}

bool FBattlePartnerFlow::TryApplyTeamVictoryRecovery(
	const TConstArrayView<FBattleBattlerState> Battlers,
	const FBattleCompiledEncounterPolicies& CompiledEncounterPolicies,
	FBattlePartnerTeamVictoryRecoveryPlan& OutPlan)
{
	OutPlan = FBattlePartnerTeamVictoryRecoveryPlan();
	if (!CompiledEncounterPolicies.HasSeparatePartnerOwnership())
	{
		return false;
	}

	const FBattleBattlerState* FirstPlayerBattler = nullptr;
	for (const FBattleBattlerState& Battler : Battlers)
	{
		const FBattleTrainerEncounterPolicy* TrainerPolicy =
			CompiledEncounterPolicies.FindTrainerPolicy(Battler.TrainerId);
		if (TrainerPolicy == nullptr
			|| TrainerPolicy->Role != EBattleTrainerRole::Player
			|| Battler.bEgg
			|| Battler.bCaptured)
		{
			continue;
		}
		if (FirstPlayerBattler == nullptr
			|| Battler.PartySlotId < FirstPlayerBattler->PartySlotId)
		{
			FirstPlayerBattler = &Battler;
		}
	}

	if (FirstPlayerBattler == nullptr
		|| FirstPlayerBattler->CurrentHP != 0
		|| !FirstPlayerBattler->bFainted)
	{
		return false;
	}

	OutPlan.Recovery.Target.TrainerId = FirstPlayerBattler->TrainerId;
	OutPlan.Recovery.Target.BattlerId = FirstPlayerBattler->BattlerId;
	OutPlan.Recovery.PreviousHP = FirstPlayerBattler->CurrentHP;
	OutPlan.Recovery.NewHP = 1;
	OutPlan.Recovery.bMajorStatusCured = true;
	return true;
}

bool FBattlePartnerFlow::TryApplyTeamVictoryRecoveryPlan(
	FBattleEngineState& State,
	const FBattlePartnerTeamVictoryRecoveryPlan& Plan)
{
	return TryApplyTeamVictoryRecoveryPlan(State.Battlers, Plan);
}

bool FBattlePartnerFlow::TryApplyTeamVictoryRecoveryPlan(
	TArray<FBattleBattlerState>& Battlers,
	const FBattlePartnerTeamVictoryRecoveryPlan& Plan)
{
	if (!IsTeamVictoryRecoveryPlanApplicable(Battlers, Plan))
	{
		return false;
	}
	ApplyPreparedTeamVictoryRecoveryPlan(Battlers, Plan);
	return true;
}

bool FBattlePartnerFlow::IsTeamVictoryRecoveryPlanApplicable(
	const FBattleEngineState& State,
	const FBattlePartnerTeamVictoryRecoveryPlan& Plan)
{
	return IsTeamVictoryRecoveryPlanApplicable(State.Battlers, Plan);
}

bool FBattlePartnerFlow::IsTeamVictoryRecoveryPlanApplicable(
	const TConstArrayView<FBattleBattlerState> Battlers,
	const FBattlePartnerTeamVictoryRecoveryPlan& Plan)
{
	const FBattlePartnerTeamVictoryRecovery& Recovery = Plan.Recovery;
	const FBattleBattlerState* Battler = Battlers.FindByPredicate(
		[&Recovery](const FBattleBattlerState& Candidate)
		{
			return Candidate.BattlerId == Recovery.Target.BattlerId;
		});
	if (!Recovery.Target.TrainerId.IsValid()
		|| !Recovery.Target.BattlerId.IsValid()
		|| Recovery.PreviousHP != 0
		|| Recovery.NewHP != 1
		|| !Recovery.bMajorStatusCured
		|| Battler == nullptr
		|| Battler->TrainerId != Recovery.Target.TrainerId
		|| Battler->BattlerId != Recovery.Target.BattlerId
		|| Battler->CurrentHP != Recovery.PreviousHP
		|| !Battler->bFainted
		|| Battler->bEgg
		|| Battler->bCaptured)
	{
		return false;
	}
	return true;
}

void FBattlePartnerFlow::ApplyPreparedTeamVictoryRecoveryPlan(
	FBattleEngineState& State,
	const FBattlePartnerTeamVictoryRecoveryPlan& Plan)
{
	ApplyPreparedTeamVictoryRecoveryPlan(State.Battlers, Plan);
}

void FBattlePartnerFlow::ApplyPreparedTeamVictoryRecoveryPlan(
	TArray<FBattleBattlerState>& Battlers,
	const FBattlePartnerTeamVictoryRecoveryPlan& Plan)
{
	const FBattlePartnerTeamVictoryRecovery& Recovery = Plan.Recovery;
	FBattleBattlerState* Battler = Battlers.FindByPredicate(
		[&Recovery](const FBattleBattlerState& Candidate)
		{
			return Candidate.BattlerId == Recovery.Target.BattlerId;
		});
	check(Battler != nullptr);
	Battler->CurrentHP = Recovery.NewHP;
	Battler->bFainted = false;
	Battler->bRemoved = false;
	Battler->bFaintTransitionPending = false;
	Battler->MajorStatusId = FConditionId();
}
