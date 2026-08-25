#include "Battle/BattlePartnerFlow.h"

#include "Battle/BattleState.h"

bool FBattlePartnerFlow::TryApplyTeamVictoryRecovery(
	FBattleEngineState& State,
	FBattlePartnerTeamVictoryRecovery& OutRecovery)
{
	OutRecovery = FBattlePartnerTeamVictoryRecovery();
	if (!State.CompiledEncounterPolicies.HasSeparatePartnerOwnership())
	{
		return false;
	}

	FBattleBattlerState* FirstPlayerBattler = nullptr;
	for (FBattleBattlerState& Battler : State.Battlers)
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

	OutRecovery.Target.TrainerId = FirstPlayerBattler->TrainerId;
	OutRecovery.Target.BattlerId = FirstPlayerBattler->BattlerId;
	OutRecovery.PreviousHP = FirstPlayerBattler->CurrentHP;
	OutRecovery.NewHP = 1;
	OutRecovery.bMajorStatusCured = true;

	FirstPlayerBattler->CurrentHP = 1;
	FirstPlayerBattler->bFainted = false;
	FirstPlayerBattler->bRemoved = false;
	FirstPlayerBattler->bFaintTransitionPending = false;
	FirstPlayerBattler->MajorStatusId = FConditionId();
	return true;
}
