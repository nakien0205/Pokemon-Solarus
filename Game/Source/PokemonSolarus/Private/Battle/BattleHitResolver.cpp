#include "Battle/BattleHitResolver.h"

#include "Battle/BattleStatCalculator.h"

namespace BattleHitResolverPrivate
{
	bool IsActionRandomContextValid(const FBattleRandomContext& Context)
	{
		return Context.IsValid() && Context.ActionId.IsValid();
	}

	void SetCriticalBehavior(FBattleCriticalCheckResult& Result)
	{
		const bool bCritical = Result.Outcome == EBattleCriticalCheckOutcome::Critical;
		Result.bIgnoreNegativeOffensiveStage = bCritical;
		Result.bIgnorePositiveDefensiveStage = bCritical;
		Result.bIgnoreScreens = bCritical;
	}
}

bool FBattleHitResolver::TryResolveAccuracy(
	const FBattleAccuracyCheckInput& Input,
	IBattleRandom& Random,
	FBattleAccuracyCheckResult& OutResult,
	EBattleHitResolverError& OutError)
{
	OutResult = FBattleAccuracyCheckResult();
	OutError = EBattleHitResolverError::None;

	if (Input.bAlwaysHits)
	{
		if (Input.BaseAccuracy != 0)
		{
			OutError = EBattleHitResolverError::InvalidAccuracy;
			return false;
		}
		OutResult.Outcome = EBattleAccuracyCheckOutcome::Hit;
		return true;
	}

	if (Input.BaseAccuracy <= 0
		|| Input.BaseAccuracy > 100
		|| !FBattleStatCalculator::TryCalculateEffectiveAccuracy(
			Input.BaseAccuracy,
			Input.AttackerStages,
			Input.DefenderStages,
			OutResult.EffectiveAccuracy))
	{
		OutResult = FBattleAccuracyCheckResult();
		OutError = EBattleHitResolverError::InvalidAccuracy;
		return false;
	}

	if (!BattleHitResolverPrivate::IsActionRandomContextValid(Input.RandomContext))
	{
		OutResult = FBattleAccuracyCheckResult();
		OutError = EBattleHitResolverError::InvalidRandomContext;
		return false;
	}

	if (!Random.TryDrawUniform(0, 99, Input.RandomContext, OutResult.Draw))
	{
		OutResult = FBattleAccuracyCheckResult();
		OutError = EBattleHitResolverError::RandomFailure;
		return false;
	}

	OutResult.bDrawConsumed = true;
	OutResult.Outcome = OutResult.Draw.Result < static_cast<uint32>(OutResult.EffectiveAccuracy)
		? EBattleAccuracyCheckOutcome::Hit
		: EBattleAccuracyCheckOutcome::Miss;
	return true;
}

bool FBattleHitResolver::TryResolveCritical(
	const FBattleCriticalCheckInput& Input,
	IBattleRandom& Random,
	FBattleCriticalCheckResult& OutResult,
	EBattleHitResolverError& OutError)
{
	OutResult = FBattleCriticalCheckResult();
	OutError = EBattleHitResolverError::None;

	if (Input.Mode == EBattleCriticalCheckMode::Always)
	{
		OutResult.bCriticalCandidate = true;
		OutResult.Outcome = Input.bDefenderBlocksCritical
			? EBattleCriticalCheckOutcome::Blocked
			: EBattleCriticalCheckOutcome::Critical;
		BattleHitResolverPrivate::SetCriticalBehavior(OutResult);
		return true;
	}
	if (Input.Mode == EBattleCriticalCheckMode::Never)
	{
		OutResult.Outcome = EBattleCriticalCheckOutcome::NotCritical;
		return true;
	}
	if (Input.Mode != EBattleCriticalCheckMode::Standard)
	{
		OutError = EBattleHitResolverError::InvalidCriticalMode;
		return false;
	}

	const int64 ModifiedStage = static_cast<int64>(Input.BaseStage)
		+ static_cast<int64>(Input.StageModifier);
	OutResult.ResolvedStage = static_cast<int32>(FMath::Clamp<int64>(ModifiedStage, 0, 4));
	if (OutResult.ResolvedStage == 0)
	{
		OutResult.Outcome = EBattleCriticalCheckOutcome::NotCritical;
		return true;
	}

	if (!BattleHitResolverPrivate::IsActionRandomContextValid(Input.RandomContext))
	{
		OutResult = FBattleCriticalCheckResult();
		OutError = EBattleHitResolverError::InvalidRandomContext;
		return false;
	}

	static constexpr uint32 InclusiveMaximumByStage[] = {0, 23, 7, 1, 0};
	const uint32 InclusiveMaximum = InclusiveMaximumByStage[OutResult.ResolvedStage];
	if (!Random.TryDrawUniform(0, InclusiveMaximum, Input.RandomContext, OutResult.Draw))
	{
		OutResult = FBattleCriticalCheckResult();
		OutError = EBattleHitResolverError::RandomFailure;
		return false;
	}

	OutResult.bDrawConsumed = true;
	OutResult.bCriticalCandidate = OutResult.Draw.Result == 0;
	if (!OutResult.bCriticalCandidate)
	{
		OutResult.Outcome = EBattleCriticalCheckOutcome::NotCritical;
	}
	else
	{
		OutResult.Outcome = Input.bDefenderBlocksCritical
			? EBattleCriticalCheckOutcome::Blocked
			: EBattleCriticalCheckOutcome::Critical;
	}
	BattleHitResolverPrivate::SetCriticalBehavior(OutResult);
	return true;
}
