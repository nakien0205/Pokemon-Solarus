#include "Battle/BattleWildFlow.h"

namespace
{
	FDefinitionId MakeRuleId(const TCHAR* Value)
	{
		FDefinitionId Id;
		const bool bCreated = FDefinitionId::TryCreate(FName(Value), Id);
		check(bCreated);
		return Id;
	}
}

bool FBattleRunRules::IsSpeedPairLegal(
	const int32 PlayerPermanentSpeed,
	const int32 WildPermanentSpeed)
{
	return PlayerPermanentSpeed >= 4 && WildPermanentSpeed >= 4;
}

bool FBattleRunRules::TryResolve(
	const FBattleRunCalculationInput& Input,
	IBattleRandom& Random,
	FBattleRunCalculationResult& OutResult)
{
	OutResult = FBattleRunCalculationResult();
	if (!IsSpeedPairLegal(Input.PlayerPermanentSpeed, Input.WildPermanentSpeed)
		|| Input.EscapeAttemptCount == 0)
	{
		return false;
	}

	const int64 WildQuarterSpeed = static_cast<int64>(Input.WildPermanentSpeed / 4);
	check(WildQuarterSpeed > 0);
	OutResult.EscapeThreshold =
		(static_cast<int64>(Input.PlayerPermanentSpeed) * 32LL) / WildQuarterSpeed
		+ 30LL * static_cast<int64>(Input.EscapeAttemptCount);
	if (OutResult.EscapeThreshold > 255)
	{
		OutResult.bSucceeded = true;
		return true;
	}

	FBattleRandomContext Context = Input.RandomContext;
	Context.RulePurpose = GetRandomCheckPurpose();
	FBattleRandomDraw Draw;
	if (!Context.IsValid() || !Random.TryDrawUniform(0, 255, Context, Draw))
	{
		OutResult = FBattleRunCalculationResult();
		return false;
	}

	OutResult.bSucceeded = static_cast<int64>(Draw.Result) < OutResult.EscapeThreshold;
	OutResult.RandomDraw = Draw;
	return true;
}

FDefinitionId FBattleRunRules::GetRandomCheckPurpose()
{
	return MakeRuleId(TEXT("Rule.C09B.Run.Check"));
}

bool FBattleWildFleeRules::IsPolicyValid(const FBattleWildFleePolicySpec& Policy)
{
	if (!Policy.TriggerId.IsValid()
		|| !Policy.EligibilityId.IsValid()
		|| (Policy.ProbabilityMode != EBattleWildFleeMode::Never
			&& Policy.ProbabilityMode != EBattleWildFleeMode::Always
			&& Policy.ProbabilityMode != EBattleWildFleeMode::Chance))
	{
		return false;
	}

	if (Policy.ProbabilityMode == EBattleWildFleeMode::Chance)
	{
		return Policy.Numerator > 0 && Policy.Numerator < Policy.Denominator;
	}
	return Policy.Numerator == 0 && Policy.Denominator == 0;
}

bool FBattleWildFleeRules::TryResolve(
	const FBattleWildFleeCalculationInput& Input,
	IBattleRandom& Random,
	FBattleWildFleeCalculationResult& OutResult)
{
	OutResult = FBattleWildFleeCalculationResult();
	if (!IsPolicyValid(Input.Policy))
	{
		return false;
	}

	if (Input.Policy.ProbabilityMode == EBattleWildFleeMode::Never)
	{
		return true;
	}
	if (Input.Policy.ProbabilityMode == EBattleWildFleeMode::Always)
	{
		OutResult.bSucceeded = true;
		return true;
	}

	FBattleRandomContext Context = Input.RandomContext;
	Context.RulePurpose = GetRandomCheckPurpose();
	FBattleRandomDraw Draw;
	if (!Context.IsValid()
		|| !Random.TryDrawUniform(0, Input.Policy.Denominator - 1, Context, Draw))
	{
		OutResult = FBattleWildFleeCalculationResult();
		return false;
	}

	OutResult.bSucceeded = Draw.Result < Input.Policy.Numerator;
	OutResult.RandomDraw = Draw;
	return true;
}

FDefinitionId FBattleWildFleeRules::GetActionSelectionTriggerId()
{
	return MakeRuleId(TEXT("Trigger.C09B.WildFlee.ActionSelection"));
}

FDefinitionId FBattleWildFleeRules::GetActiveLivingWildEligibilityId()
{
	return MakeRuleId(TEXT("Eligibility.C09B.WildFlee.ActiveLivingWildOpponent"));
}

FDefinitionId FBattleWildFleeRules::GetRandomCheckPurpose()
{
	return MakeRuleId(TEXT("Rule.C09B.WildFlee.Check"));
}
