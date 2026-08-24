#pragma once

#include "CoreMinimal.h"
#include "Battle/BattleActionSelector.h"

/** Deterministic FIFO selector available only to Automation tests. */
class FScriptedBattleActionSelector final : public IBattleActionSelector
{
public:
	explicit FScriptedBattleActionSelector(TArray<FBattleDecision> InScript)
		: Script(MoveTemp(InScript))
	{
	}

	[[nodiscard]] virtual bool TrySelectAction(
		const FBattleActionSelectorInput& Input,
		FBattleDecision& OutDecision,
		FBattleRejection& OutRejection) override
	{
		OutDecision = FBattleDecision();
		OutRejection = FBattleRejection();
		if (!Input.IsValid() || !Script.IsValidIndex(NextIndex))
		{
			OutRejection.Reason = EBattleRejectionReason::InvalidDecision;
			return false;
		}

		OutDecision = Script[NextIndex];
		++NextIndex;
		return true;
	}

	[[nodiscard]] int32 GetRemainingDecisionCount() const
	{
		return Script.Num() - NextIndex;
	}

private:
	TArray<FBattleDecision> Script;
	int32 NextIndex = 0;
};
