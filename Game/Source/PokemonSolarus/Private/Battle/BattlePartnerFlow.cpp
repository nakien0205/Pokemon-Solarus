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

	int32 FirstPlayerBattlerIndex = INDEX_NONE;
	for (int32 BattlerIndex = 0; BattlerIndex < State.Battlers.Num(); ++BattlerIndex)
	{
		const FBattleBattlerState& Battler = State.Battlers[BattlerIndex];
		const FBattleTrainerEncounterPolicy* TrainerPolicy =
			State.CompiledEncounterPolicies.FindTrainerPolicy(Battler.TrainerId);
		if (TrainerPolicy == nullptr
			|| TrainerPolicy->Role != EBattleTrainerRole::Player
			|| Battler.bEgg
			|| Battler.bCaptured)
		{
			continue;
		}
		if (FirstPlayerBattlerIndex == INDEX_NONE
			|| Battler.PartySlotId
				< State.Battlers[FirstPlayerBattlerIndex].PartySlotId)
		{
			FirstPlayerBattlerIndex = BattlerIndex;
		}
	}

	if (!State.Battlers.IsValidIndex(FirstPlayerBattlerIndex))
	{
		return false;
	}
	const FBattleBattlerState& FirstPlayerBattler = State.Battlers[FirstPlayerBattlerIndex];
	if (FirstPlayerBattler.CurrentHP != 0 || !FirstPlayerBattler.bFainted)
	{
		return false;
	}

	OutPlan.Recovery.Target.TrainerId = FirstPlayerBattler.TrainerId;
	OutPlan.Recovery.Target.BattlerId = FirstPlayerBattler.BattlerId;
	OutPlan.Recovery.PreviousHP = FirstPlayerBattler.CurrentHP;
	OutPlan.Recovery.NewHP = 1;
	OutPlan.Recovery.bMajorStatusCured = true;
	OutPlan.BattlerIndex = FirstPlayerBattlerIndex;
	return true;
}

bool FBattlePartnerFlow::TryApplyTeamVictoryRecoveryPlan(
	FBattleEngineState& State,
	const FBattlePartnerTeamVictoryRecoveryPlan& Plan)
{
	if (!IsTeamVictoryRecoveryPlanApplicable(State, Plan))
	{
		return false;
	}
	ApplyPreparedTeamVictoryRecoveryPlan(State, Plan);
	return true;
}

bool FBattlePartnerFlow::IsTeamVictoryRecoveryPlanApplicable(
	const FBattleEngineState& State,
	const FBattlePartnerTeamVictoryRecoveryPlan& Plan)
{
	const FBattlePartnerTeamVictoryRecovery& Recovery = Plan.Recovery;
	const FBattleBattlerState* Battler = State.Battlers.IsValidIndex(Plan.BattlerIndex)
		? &State.Battlers[Plan.BattlerIndex]
		: nullptr;
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
	FBattleBattlerState& Battler = State.Battlers[Plan.BattlerIndex];
	const FBattlePartnerTeamVictoryRecovery& Recovery = Plan.Recovery;
	Battler.CurrentHP = Recovery.NewHP;
	Battler.bFainted = false;
	Battler.bRemoved = false;
	Battler.bFaintTransitionPending = false;
	Battler.MajorStatusId = FConditionId();
}
