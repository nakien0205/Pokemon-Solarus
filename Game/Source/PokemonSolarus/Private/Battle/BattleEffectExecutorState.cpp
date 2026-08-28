#include "BattleEffectExecutorContext.h"

#include "Battle/BattleAbility.h"
#include "BattleEntryHazardPrevention.h"
#include "Battle/BattleFieldSideConditions.h"
#include "Battle/BattleItem.h"
#include "Battle/BattleMajorStatus.h"
#include "Battle/BattleState.h"
#include "Battle/BattleVolatile.h"
#include "Math/NumericLimits.h"

namespace BattleEffectExecutorPrivate
{
	bool IsKnownSide(EBattleSide Side);
	bool AreMoveDefinitionsIdentical(
		const FBattleMoveDefinition& Left,
		const FBattleMoveDefinition& Right);
	bool RequiresConditionReference(EBattleMoveEffectKind Kind);
	bool IsRemovalTargetCompatible(
		EBattleEffectTarget Target,
		EBattleConditionKind ConditionKind);

	FStateExecutionContext::FStateExecutionContext(
		const FBattleEffectExecutionRequest& InRequest,
		const FBattleEngineState& InState,
		IBattleRandom& InRandom)
		: Request(InRequest)
		, State(InState)
		, Random(InRandom)
		, Battlers(InState.Battlers)
		, ActivePositions(InState.ActivePositions)
		, Field(InState.Field)
		, Sides(InState.Sides)
		, TriggerFramework(InState.TriggerFramework)
		, AbilityItemRevealTracker(InState.AbilityItemRevealTracker)
		, HeldItemLedger(InState.HeldItemLedger)
		, NextConditionCreationOrdinal(InState.NextConditionCreationOrdinal)
		, NextTriggerReentrancyToken(InState.NextTriggerReentrancyToken)
	{
	}

	void FStateExecutionContext::MovePreparedState(FBattleEffectExecutionPlan& OutPlan)
	{
		OutPlan.Battlers = MoveTemp(Battlers);
		OutPlan.ActivePositions = MoveTemp(ActivePositions);
		OutPlan.Field = MoveTemp(Field);
		OutPlan.Sides = MoveTemp(Sides);
		OutPlan.TriggerFramework = MoveTemp(TriggerFramework);
		OutPlan.AbilityItemRevealTracker = MoveTemp(AbilityItemRevealTracker);
		OutPlan.HeldItemLedger = MoveTemp(HeldItemLedger);
		OutPlan.NextConditionCreationOrdinal = NextConditionCreationOrdinal;
		OutPlan.NextTriggerReentrancyToken = NextTriggerReentrancyToken;
	}

	void FStateExecutionContext::BindExecutionResult(FBattleEffectExecutionResult& InResult)
	{
		ExecutionResult = &InResult;
	}

	bool FStateExecutionContext::PrevalidateRequest(
		const FBattleEffectExecutionRequest& CandidateRequest) const
	{
		if (&CandidateRequest != &Request)
		{
			return false;
		}
		const FBattleBattlerState* User = FindBattler(Request.UserBattlerId);
		const FBattleActivePositionState* UserPosition = State.FindActivePosition(
			Request.UserSlotId);
		if (User == nullptr
			|| UserPosition == nullptr
			|| !UserPosition->bAvailable
			|| UserPosition->BattlerId != Request.UserBattlerId
			|| User->CurrentHP <= 0
			|| User->bFainted
			|| User->bCaptured
			|| User->bRemoved)
		{
			return false;
		}

		const bool bStruggle = Request.Move->Id
			== FBattleBuiltInMoveDefinitions::GetStruggleMoveId();
		const FBattleMoveDefinition* CatalogMove = State.Catalog.FindMove(Request.Move->Id);
		if ((bStruggle
				&& !AreMoveDefinitionsIdentical(
					*Request.Move,
					FBattleBuiltInMoveDefinitions::GetStruggle()))
			|| (!bStruggle
				&& (CatalogMove == nullptr
					|| !AreMoveDefinitionsIdentical(*Request.Move, *CatalogMove))))
		{
			return false;
		}

		for (const FBattleResolvedTarget& Target : Request.Targets)
		{
			if (Target.GetKind() == EBattleResolvedTargetKind::Battler)
			{
				const FBattleActivePositionState* Position = State.FindActivePosition(
					Target.GetBattler().ActiveSlotId);
				if (Position == nullptr
					|| !Position->bAvailable
					|| Position->BattlerId != Target.GetBattler().BattlerId
					|| FindBattler(Target.GetBattler().BattlerId) == nullptr)
				{
					return false;
				}
			}
			else if (Target.GetKind() == EBattleResolvedTargetKind::Side)
			{
				if (Sides.FindByPredicate(
					[&Target](const FBattleSideState& Side)
					{
						return Side.Side == Target.GetSide();
					}) == nullptr)
				{
					return false;
				}
			}
			else if (Target.GetKind() != EBattleResolvedTargetKind::Field)
			{
				return false;
			}
		}

		for (const FBattleMoveEffectDescriptor& Effect : Request.Move->Effects)
		{
			const bool bNeedsCondition = RequiresConditionReference(Effect.Kind);
			const FBattleConditionDefinition* Condition = bNeedsCondition
				? State.Catalog.FindCondition(Effect.ConditionId)
				: nullptr;
			if (bNeedsCondition && Condition == nullptr)
			{
				return false;
			}
			if (Effect.Kind == EBattleMoveEffectKind::ApplyCondition
				&& Condition->Kind != EBattleConditionKind::MajorStatus
				&& Condition->Kind != EBattleConditionKind::Volatile)
			{
				return false;
			}
			if (Effect.Kind == EBattleMoveEffectKind::SetFieldCondition
				&& Condition->Kind != EBattleConditionKind::Weather
				&& Condition->Kind != EBattleConditionKind::Terrain
				&& Condition->Kind != EBattleConditionKind::Room)
			{
				return false;
			}
			if (Effect.Kind == EBattleMoveEffectKind::SetSideCondition
				&& Condition->Kind != EBattleConditionKind::Hazard
				&& Condition->Kind != EBattleConditionKind::Screen
				&& Condition->Kind != EBattleConditionKind::SideCondition)
			{
				return false;
			}
			if ((Effect.Kind == EBattleMoveEffectKind::Charge
					|| Effect.Kind == EBattleMoveEffectKind::Recharge
					|| Effect.Kind == EBattleMoveEffectKind::Protect
					|| Effect.Kind == EBattleMoveEffectKind::SemiInvulnerability)
				&& Condition->Kind != EBattleConditionKind::Volatile)
			{
				return false;
			}
			if (Effect.Kind == EBattleMoveEffectKind::RemoveCondition
				&& !IsRemovalTargetCompatible(Effect.Target, Condition->Kind))
			{
				return false;
			}
			if (Effect.Kind == EBattleMoveEffectKind::ChangeItem
				&& State.Catalog.FindItem(Effect.ItemId) == nullptr)
			{
				return false;
			}
		}
		return true;
	}

	bool FStateExecutionContext::IsRuntimeValid() const
	{
		return bRuntimeValid;
	}

	EBattleEffectExecutorError FStateExecutionContext::GetRuntimeError() const
	{
		return RuntimeError;
	}

	bool FStateExecutionContext::IsSourceAbleToContinue() const
	{
		const FBattleBattlerState* Source = FindBattler(Request.UserBattlerId);
		return Source != nullptr
			&& Source->CurrentHP > 0
			&& !Source->bFainted
			&& !Source->bCaptured
			&& !Source->bRemoved;
	}

	bool FStateExecutionContext::IsTargetAbleToContinue(const FBattleResolvedTarget& Target) const
	{
		if (Target.GetKind() != EBattleResolvedTargetKind::Battler)
		{
			return Target.IsValid();
		}
		const FBattleBattlerState* Battler = FindBattler(Target.GetBattler().BattlerId);
		return Battler != nullptr
			&& Battler->CurrentHP > 0
			&& !Battler->bFainted
			&& !Battler->bCaptured
			&& !Battler->bRemoved;
	}

	bool FStateExecutionContext::TryBuildEventTarget(
		const FBattleResolvedTarget& Target,
		FBattleEventTarget& OutTarget) const
	{
		OutTarget = FBattleEventTarget();
		if (Target.GetKind() == EBattleResolvedTargetKind::Battler)
		{
			const FBattleBattlerState* Battler = FindBattler(Target.GetBattler().BattlerId);
			if (Battler == nullptr)
			{
				return false;
			}
			OutTarget.TrainerId = Battler->TrainerId;
			OutTarget.BattlerId = Battler->BattlerId;
			OutTarget.ActiveSlotId = Target.GetBattler().ActiveSlotId;
			return true;
		}
		if (Target.GetKind() == EBattleResolvedTargetKind::Side)
		{
			OutTarget.Side = Target.GetSide();
			OutTarget.bHasSide = true;
			return IsKnownSide(Target.GetSide());
		}
		if (Target.GetKind() == EBattleResolvedTargetKind::Field)
		{
			OutTarget.bField = true;
			return true;
		}
		return false;
	}

	FBattleEffectHookResult FStateExecutionContext::Applied()
	{
		return Outcome(EBattleEffectExecutionOutcome::Applied);
	}

	FBattleEffectHookResult FStateExecutionContext::Outcome(const EBattleEffectExecutionOutcome Value)
	{
		FBattleEffectHookResult Result;
		Result.Outcome = Value;
		return Result;
	}

	const FBattleBattlerState* FStateExecutionContext::FindBattler(const FBattlerId Id) const
	{
		return Battlers.FindByPredicate(
			[Id](const FBattleBattlerState& Battler)
			{
				return Battler.BattlerId == Id;
			});
	}

	FBattleBattlerState* FStateExecutionContext::FindMutableBattler(const FBattlerId Id)
	{
		return Battlers.FindByPredicate(
			[Id](const FBattleBattlerState& Battler)
			{
				return Battler.BattlerId == Id;
			});
	}

	const FBattleActivePositionState* FStateExecutionContext::FindActiveForBattler(const FBattlerId Id) const
	{
		return ActivePositions.FindByPredicate(
			[Id](const FBattleActivePositionState& Position)
			{
				return Position.BattlerId == Id;
			});
	}

	FBattleActivePositionState* FStateExecutionContext::FindMutableActivePosition(const FActiveSlotId Id)
	{
		return ActivePositions.FindByPredicate(
			[Id](const FBattleActivePositionState& Position)
			{
				return Position.ActiveSlotId == Id;
			});
	}

	FBattleSideState* FStateExecutionContext::FindMutableSide(const EBattleSide Side)
	{
		return Sides.FindByPredicate(
			[Side](const FBattleSideState& Entry)
			{
				return Entry.Side == Side;
			});
	}

	const FBattleSideState* FStateExecutionContext::FindSide(const EBattleSide Side) const
	{
		return Sides.FindByPredicate(
			[Side](const FBattleSideState& Entry)
			{
				return Entry.Side == Side;
			});
	}

	FConditionId FStateExecutionContext::GetWeatherId() const
	{
		return Field.Weather.IsSet() ? Field.Weather.GetValue().ConditionId : FConditionId();
	}

	FConditionId FStateExecutionContext::GetTerrainId() const
	{
		return Field.Terrain.IsSet() ? Field.Terrain.GetValue().ConditionId : FConditionId();
	}

	bool FStateExecutionContext::TryIsGrounded(
		const FBattleBattlerState& Battler,
		bool& bOutGrounded,
		const bool bAbilityIgnoredForMove,
		bool* bOutLevitateMadeAirborne) const
	{
		if (bOutLevitateMadeAirborne != nullptr)
		{
			*bOutLevitateMadeAirborne = false;
		}
		const FBattleSpeciesFormDefinition* Species = State.Catalog.FindSpeciesForm(
			Battler.SpeciesFormId);
		if (Species == nullptr)
		{
			bOutGrounded = false;
			return false;
		}
		FBattleGroundedFacts Facts;
		Facts.PrimaryType = Species->PrimaryType;
		Facts.SecondaryType = Species->SecondaryType;
		Facts.bAbilityMakesAirborne = FBattleAbilityRules::IsLevitateAirborne(
			Battler.AbilityId,
			false,
			bAbilityIgnoredForMove);
		Facts.bAbilitySuppressed = Battler.bAbilitySuppressed;
		Facts.bItemMakesAirborne = !Battler.HeldItem.bConsumed
			&& !Battler.HeldItem.bTemporarilyRemoved
			&& FBattleItemRules::IsAirBalloonAirborne(
				Battler.HeldItem.CurrentItemId,
				Battler.HeldItem.bSuppressed);
		Facts.bItemSuppressed = Battler.HeldItem.bSuppressed;
		Facts.bAirborneSemiInvulnerable = HasVolatile(
			Battler,
			FBattleVolatileRules::GetFlySemiInvulnerableId());
		if (!FBattleFieldSideConditionRules::TryResolveGrounded(Facts, bOutGrounded))
		{
			return false;
		}
		if (bOutLevitateMadeAirborne != nullptr && Facts.bAbilityMakesAirborne)
		{
			FBattleGroundedFacts WithoutLevitate = Facts;
			WithoutLevitate.bAbilityMakesAirborne = false;
			bool bGroundedWithoutLevitate = false;
			if (!FBattleFieldSideConditionRules::TryResolveGrounded(
					WithoutLevitate,
					bGroundedWithoutLevitate))
			{
				return false;
			}
			*bOutLevitateMadeAirborne = bGroundedWithoutLevitate && !bOutGrounded;
		}
		return true;
	}

	bool FStateExecutionContext::ShouldIgnoreLevitateForCurrentMove(
		const FBattleBattlerState& Defender) const
	{
		if (Defender.AbilityId != FBattleAbilityRules::GetLevitateId())
		{
			return false;
		}
		const FBattleBattlerState* User = FindBattler(Request.UserBattlerId);
		TArray<FBattleAbilityItemHookDefinition> Hooks;
		if (User == nullptr
			|| !FBattleAbilityRules::TryGetHookDefinitionsForPhase(
				Defender.AbilityId,
				EBattleTriggerPhase::BeforeHit,
				Hooks))
		{
			return false;
		}
		const FBattleAbilityItemHookDefinition* TypeImmunityHook =
			Hooks.FindByPredicate(
				[](const FBattleAbilityItemHookDefinition& Hook)
				{
					return Hook.HookPoint
						== EBattleAbilityItemHookPoint::TypeImmunity;
				});
		return TypeImmunityHook != nullptr
			&& FBattleAbilityRules::ShouldMoldBreakerIgnoreDefenderHookForMove(
				User->AbilityId,
				User->bAbilitySuppressed,
				Defender.AbilityId,
				*TypeImmunityHook);
	}

	void FStateExecutionContext::SetRuntimeFailure(const EBattleEffectExecutorError Error)
	{
		bRuntimeValid = false;
		RuntimeError = Error;
	}
}

bool FBattleEffectExecutor::TryExecuteAgainstState(
	const FBattleEffectExecutionRequest& Request,
	FBattleEngineState& State,
	FBattleEffectExecutionResult& OutResult,
	EBattleEffectExecutorError& OutError)
{
	OutResult = FBattleEffectExecutionResult();
	OutError = EBattleEffectExecutorError::None;
	if (!State.Random.IsValid())
	{
		OutError = EBattleEffectExecutorError::InvalidRequest;
		return false;
	}
	FBattleEffectExecutionPlan Plan;
	if (!TryPrepareAgainstState(Request, State, *State.Random, Plan, OutError))
	{
		return false;
	}
	OutResult = MoveTemp(Plan.Result);
	ApplyPreparedPlan(State, MoveTemp(Plan));
	return true;
}

bool FBattleEffectExecutor::TryPrepareAgainstState(
	const FBattleEffectExecutionRequest& Request,
	const FBattleEngineState& State,
	IBattleRandom& Random,
	FBattleEffectExecutionPlan& OutPlan,
	EBattleEffectExecutorError& OutError)
{
	OutPlan = FBattleEffectExecutionPlan();
	OutError = EBattleEffectExecutorError::None;
	BattleEffectExecutorPrivate::FStateExecutionContext Context(Request, State, Random);
	Context.BindExecutionResult(OutPlan.Result);
	if (!TryExecute(Request, Context, Random, OutPlan.Result, OutError)
		|| !Context.TryResolveForcedSwitches(OutPlan.Result, OutError)
		|| !Context.TryApplyPostMoveLifeOrbRecoil(OutPlan.Result, OutError))
	{
		OutPlan = FBattleEffectExecutionPlan();
		return false;
	}
	Context.MovePreparedState(OutPlan);
	return true;
}

void FBattleEffectExecutor::ApplyPreparedPlan(
	FBattleEngineState& State,
	FBattleEffectExecutionPlan&& Plan)
{
	State.Battlers = MoveTemp(Plan.Battlers);
	State.ActivePositions = MoveTemp(Plan.ActivePositions);
	State.Field = MoveTemp(Plan.Field);
	State.Sides = MoveTemp(Plan.Sides);
	State.TriggerFramework = MoveTemp(Plan.TriggerFramework);
	State.AbilityItemRevealTracker = MoveTemp(Plan.AbilityItemRevealTracker);
	State.HeldItemLedger = MoveTemp(Plan.HeldItemLedger);
	State.NextConditionCreationOrdinal = Plan.NextConditionCreationOrdinal;
	State.NextTriggerReentrancyToken = Plan.NextTriggerReentrancyToken;
}
