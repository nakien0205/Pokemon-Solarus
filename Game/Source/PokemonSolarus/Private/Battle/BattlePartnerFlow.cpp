#include "Battle/BattlePartnerFlow.h"

#include "Battle/BattleState.h"

bool FBattlePartnerFlow::TryApplyTeamVictoryRecovery(
	FBattleEngineState& State,
	FBattlePartnerTeamVictoryRecovery& OutRecovery)
{
	OutRecovery = FBattlePartnerTeamVictoryRecovery();
	if (State.Format != EBattleFormat::PartnerDouble)
	{
		return false;
	}

	FBattleBattlerState* FirstPlayerBattler = nullptr;
	for (FBattleBattlerState& Battler : State.Battlers)
	{
		const FBattleTrainerState* Trainer = State.FindTrainer(Battler.TrainerId);
		if (Trainer == nullptr
			|| Trainer->Role != EBattleTrainerRole::Player
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
