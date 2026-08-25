#pragma once

#if WITH_DEV_AUTOMATION_TESTS

#include "Battle/BattleEngine.h"
#include "Battle/BattleState.h"
#include "Math/NumericLimits.h"

/** Shared C09B/ADR-0002 friend fixture; production code receives no test switch. */
class FBattleC09BWildFlowEngineFixture
{
public:
	static FBattleEngineState& GetMutableState(FBattleEngine& Engine)
	{
		check(Engine.State.IsValid());
		return *Engine.State;
	}

	static const FBattleEngineState& GetState(const FBattleEngine& Engine)
	{
		check(Engine.State.IsValid());
		return *Engine.State;
	}

	static int32 GetRemainingActions(
		const FBattleEngine& Engine,
		const FTrainerId TrainerId)
	{
		const FBattleTrainerState* Trainer = GetState(Engine).FindTrainer(TrainerId);
		return Trainer != nullptr ? Trainer->ActionAllowance.RemainingActions : INDEX_NONE;
	}

	static bool ApplySpeedStage(
		FBattleEngine& Engine,
		const FBattlerId BattlerId,
		const int32 Delta)
	{
		FBattleBattlerState* Battler = GetMutableState(Engine).FindMutableBattler(BattlerId);
		return Battler != nullptr
			&& Battler->Stages.ApplyChange(EBattleStat::Speed, Delta).Outcome
				== EBattleStatStageChangeOutcome::Applied;
	}

	static bool SetPermanentSpeed(
		FBattleEngine& Engine,
		const FBattlerId BattlerId,
		const int32 Speed)
	{
		FBattleBattlerState* Battler = GetMutableState(Engine).FindMutableBattler(BattlerId);
		if (Battler == nullptr)
		{
			return false;
		}
		Battler->PermanentStats.Speed = Speed;
		return true;
	}

	static bool IsActive(const FBattleEngine& Engine, const FBattlerId BattlerId)
	{
		return GetState(Engine).ActivePositions.ContainsByPredicate(
			[BattlerId](const FBattleActivePositionState& Position)
			{
				return Position.BattlerId == BattlerId;
			});
	}

	static bool IsRemoved(const FBattleEngine& Engine, const FBattlerId BattlerId)
	{
		const FBattleBattlerState* Battler = GetState(Engine).FindBattler(BattlerId);
		return Battler != nullptr && Battler->bRemoved;
	}

	static int32 GetMovePP(const FBattleEngine& Engine, const FBattlerId BattlerId)
	{
		const FBattleBattlerState* Battler = GetState(Engine).FindBattler(BattlerId);
		return Battler != nullptr && !Battler->Moves.IsEmpty()
			? Battler->Moves[0].CurrentPP
			: INDEX_NONE;
	}

	static TConstArrayView<FBattleWildFleePolicyState> GetWildFleePolicies(
		const FBattleEngine& Engine)
	{
		return GetState(Engine).WildFleePolicies;
	}

	static void SetNextTriggerReentrancyToken(
		FBattleEngine& Engine,
		const uint64 Value)
	{
		GetMutableState(Engine).NextTriggerReentrancyToken = Value;
	}

	static void AdvanceStateVersion(FBattleEngine& Engine)
	{
		FBattleEngineState& State = GetMutableState(Engine);
		check(State.StateVersion != TNumericLimits<uint64>::Max());
		++State.StateVersion;
	}
};

#endif
