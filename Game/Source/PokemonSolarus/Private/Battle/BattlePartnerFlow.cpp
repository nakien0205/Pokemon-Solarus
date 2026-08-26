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
	OutPlan = FBattlePartnerTeamVictoryRecoveryPlan();
	if (!State.CompiledEncounterPolicies.HasSeparatePartnerOwnership())
	{
		return false;
	}

	const FBattleBattlerState* FirstPlayerBattler = nullptr;
	for (const FBattleBattlerState& Battler : State.Battlers)
	{
		const FBattleTrainerEncounterPolicy* TrainerPolicy =
			State.CompiledEncounterPolicies.FindTrainerPolicy(Battler.TrainerId);
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
	const FBattlePartnerTeamVictoryRecovery& Recovery = Plan.Recovery;
	FBattleBattlerState* Battler = State.FindMutableBattler(Recovery.Target.BattlerId);
	if (!Recovery.Target.TrainerId.IsValid()
		|| !Recovery.Target.BattlerId.IsValid()
		|| Recovery.PreviousHP != 0
		|| Recovery.NewHP != 1
		|| !Recovery.bMajorStatusCured
		|| Battler == nullptr
		|| Battler->TrainerId != Recovery.Target.TrainerId
		|| Battler->CurrentHP != Recovery.PreviousHP
		|| !Battler->bFainted
		|| Battler->bEgg
		|| Battler->bCaptured)
	{
		return false;
	}

	Battler->CurrentHP = Recovery.NewHP;
	Battler->bFainted = false;
	Battler->bRemoved = false;
	Battler->bFaintTransitionPending = false;
	Battler->MajorStatusId = FConditionId();
	return true;
}
