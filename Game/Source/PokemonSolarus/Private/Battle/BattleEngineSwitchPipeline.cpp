#include "BattleEngineSwitchPipeline.h"

namespace BattleEngineSwitchPipelinePrivate
{
	using namespace BattleEngineCommonPrivate;
	using namespace BattleEngineEventsPrivate;
	using namespace BattleEngineTriggerRuntimePrivate;

	FDefinitionId GetWildOpponentSwitchRestrictionRuleId()
	{
		FDefinitionId RuleId;
		const bool bCreated = FDefinitionId::TryCreate(
			FName(TEXT("Battle.Switch.NoOrdinaryWildOpponent")),
			RuleId);
		check(bCreated);
		return RuleId;
	}

	bool TryApplyReplacementSelection(
		FBattleEngineState& State,
		const FTrainerId TrainerId,
		const FActiveSlotId ActiveSlotId,
		const FBattleSwitchResolution& Resolution,
		FBattleEventTarget& OutIncomingTarget)
	{
		OutIncomingTarget = FBattleEventTarget();
		if (!Resolution.IsValid() || !Resolution.HasSelection())
		{
			return false;
		}

		FBattleActivePositionState* Active = State.FindMutableActivePosition(ActiveSlotId);
		FBattleBattlerState* Incoming = State.FindMutableBattler(
			Resolution.GetSelectedBattlerId());
		const FBattleTrainerState* Trainer = State.FindTrainer(TrainerId);
		if (Active == nullptr
			|| Incoming == nullptr
			|| Trainer == nullptr
			|| !Active->bAvailable
			|| Active->ActiveSlotId.GetSide() != Trainer->Side
			|| Active->TrainerId.IsValid()
			|| Active->BattlerId.IsValid()
			|| Incoming->TrainerId != Trainer->TrainerId
			|| Incoming->PartySlotId != Resolution.GetSelectedPartySlotId()
			|| !IsLivingSelectableBattler(Incoming)
			|| FindActiveForBattler(State, Incoming->BattlerId) != nullptr)
		{
			return false;
		}

		Active->TrainerId = Trainer->TrainerId;
		Active->BattlerId = Incoming->BattlerId;
		Incoming->bAbilitySuppressed = false;
		Incoming->EnteredActiveOnTurnId = State.TurnId;
		OutIncomingTarget.TrainerId = Incoming->TrainerId;
		OutIncomingTarget.BattlerId = Incoming->BattlerId;
		OutIncomingTarget.ActiveSlotId = Active->ActiveSlotId;
		return TryRegisterAbilityTriggers(State, Incoming->BattlerId)
			&& TryRegisterItemTriggers(State, Incoming->BattlerId);
	}
}
