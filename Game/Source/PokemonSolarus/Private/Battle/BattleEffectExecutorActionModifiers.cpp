#include "BattleEffectExecutorContext.h"

#include "BattleAllyActionPowerModifier.h"

namespace BattleEffectExecutorPrivate
{
	FBattleEffectHookResult
	FStateExecutionContext::ApplyAllyActionPowerModifierRegistration(
		const FBattleMoveEffectDescriptor& Effect,
		const FBattleResolvedTarget& Target)
	{
		if (Target.GetKind() != EBattleResolvedTargetKind::Battler)
		{
			SetRuntimeFailure(EBattleEffectExecutorError::InvalidTarget);
			return Outcome(EBattleEffectExecutionOutcome::Failed);
		}

		int32 BindingCountBefore = 0;
		int32 BindingCountAfter = 0;
		const EBattleAllyActionPowerModifierRegistrationOutcome RegistrationOutcome =
			FBattleAllyActionPowerModifier::TryRegister(
				State.Format,
				Request.TurnId,
				Request.ActionId,
				Request.Move->Id,
				{Request.UserSlotId, Request.UserBattlerId},
				Target.GetBattler(),
				Effect.MagnitudeNumerator,
				Effect.MagnitudeDenominator,
				Battlers,
				ActivePositions,
				State.LockedActions,
				AllyActionPowerModifierRegistrations,
				BindingCountBefore,
				BindingCountAfter);
		if (RegistrationOutcome
			== EBattleAllyActionPowerModifierRegistrationOutcome::IneligibleFormat
			|| RegistrationOutcome
				== EBattleAllyActionPowerModifierRegistrationOutcome::IneligibleTarget)
		{
			return Outcome(EBattleEffectExecutionOutcome::Failed);
		}
		if (RegistrationOutcome
			!= EBattleAllyActionPowerModifierRegistrationOutcome::Registered)
		{
			SetRuntimeFailure(EBattleEffectExecutorError::InvalidHookResult);
			return Outcome(EBattleEffectExecutionOutcome::Failed);
		}

		FBattleEffectHookResult Result = Applied();
		Result.NumericBefore = BindingCountBefore;
		Result.NumericAfter = BindingCountAfter;
		Result.NumericDelta = BindingCountAfter - BindingCountBefore;
		Result.bStateMutated = true;
		return Result;
	}
}
