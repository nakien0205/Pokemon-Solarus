#pragma once

#include "Battle/BattleAbility.h"
#include "Battle/BattleEngine.h"
#include "Battle/BattleFieldSideConditions.h"
#include "Battle/BattleItem.h"
#include "Battle/BattleState.h"
#include "Battle/BattleVolatile.h"
#include "Battle/BattleWildFlow.h"

namespace BattleEngineCommonPrivate
{
	template <typename TState>
	const FBattleActivePositionState* FindActiveForBattler(
		const TState& State,
		FBattlerId BattlerId);

	FResolutionId TakeResolutionId(FBattleEngineState& State);

	FActionId TakeActionId(FBattleEngineState& State);

	bool IsHeldItemActive(const FBattleBattlerState& Battler);

	const FBattleConditionState* FindVolatile(
		const FBattleBattlerState& Battler,
		const FConditionId& VolatileId);

	FBattleConditionState* FindMutableVolatile(
		FBattleBattlerState& Battler,
		const FConditionId& VolatileId);

	bool HasVolatile(
		const FBattleBattlerState& Battler,
		const FConditionId& VolatileId);

	template <typename TState>
	bool HasFieldRoom(
		const TState& State,
		const FConditionId& RoomId)
	{
		return State.Field.Rooms.ContainsByPredicate(
			[&RoomId](const FBattleConditionState& Condition)
			{
				return Condition.ConditionId == RoomId;
			});
	}

	template <typename TState>
	const FBattleSideState* FindSide(
		const TState& State,
		const EBattleSide Side)
	{
		return State.Sides.FindByPredicate(
			[Side](const FBattleSideState& Candidate)
			{
				return Candidate.Side == Side;
			});
	}

	template <typename TState>
	FBattleSideState* FindMutableSide(
		TState& State,
		const EBattleSide Side)
	{
		return State.Sides.FindByPredicate(
			[Side](const FBattleSideState& Candidate)
			{
				return Candidate.Side == Side;
			});
	}

	template <typename TState>
	bool HasSideCondition(
		const TState& State,
		const EBattleSide Side,
		const FConditionId& ConditionId)
	{
		const FBattleSideState* SideState = FindSide(State, Side);
		return SideState != nullptr && SideState->Conditions.ContainsByPredicate(
			[&ConditionId](const FBattleConditionState& Condition)
			{
				return Condition.ConditionId == ConditionId;
			});
	}

	template <typename TState>
	const FBattleConditionState* FindFieldSideCondition(
		const TState& State,
		const FBattleTriggerSubject& Owner,
		const FConditionId& ConditionId)
	{
		if (!FBattleFieldSideConditionRules::IsCanonical(ConditionId))
		{
			return nullptr;
		}
		if (Owner.Kind == EBattleTriggerSubjectKind::Field)
		{
			if (State.Field.Weather.IsSet()
				&& State.Field.Weather.GetValue().ConditionId == ConditionId)
			{
				return &State.Field.Weather.GetValue();
			}
			if (State.Field.Terrain.IsSet()
				&& State.Field.Terrain.GetValue().ConditionId == ConditionId)
			{
				return &State.Field.Terrain.GetValue();
			}
			return State.Field.Rooms.FindByPredicate(
				[&ConditionId](const FBattleConditionState& Condition)
				{
					return Condition.ConditionId == ConditionId;
				});
		}
		if (Owner.Kind != EBattleTriggerSubjectKind::Side || !Owner.bHasSide)
		{
			return nullptr;
		}
		const FBattleSideState* Side = FindSide(State, Owner.Side);
		if (Side == nullptr)
		{
			return nullptr;
		}
		const FBattleConditionState* Condition = Side->Conditions.FindByPredicate(
			[&ConditionId](const FBattleConditionState& Candidate)
			{
				return Candidate.ConditionId == ConditionId;
			});
		return Condition != nullptr
			? Condition
			: Side->Hazards.FindByPredicate(
				[&ConditionId](const FBattleConditionState& Candidate)
				{
					return Candidate.ConditionId == ConditionId;
				});
	}

	FBattleConditionState* FindMutableFieldSideCondition(
		FBattleEngineState& State,
		const FBattleTriggerSubject& Owner,
		const FConditionId& ConditionId);

	bool RemoveFieldSideConditionState(
		FBattleEngineState& State,
		const FBattleTriggerSubject& Owner,
		const FConditionId& ConditionId);

	template <typename TState>
	bool TryResolveGrounded(
		const TState& State,
		const FBattleBattlerState& Battler,
		bool& bOutGrounded,
		bool* bOutLevitateMadeAirborne = nullptr)
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
			Battler.bAbilitySuppressed);
		Facts.bAbilitySuppressed = Battler.bAbilitySuppressed;
		Facts.bItemMakesAirborne = IsHeldItemActive(Battler)
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

	bool IsVolatileSuppressed(
		const FBattleEngineState& State,
		const FBattlerId BattlerId,
		const FConditionId& VolatileId);

	template <typename TState>
	bool TryGetVolatilePayloadMoveId(
		const TState& State,
		const FBattlerId BattlerId,
		const FConditionId& VolatileId,
		FMoveId& OutMoveId)
	{
		OutMoveId = FMoveId();
		for (const FBattleTriggerRegistrationState& Registration :
			State.TriggerFramework.GetActiveRegistrations())
		{
			if (Registration.Spec.Owner.Kind == EBattleTriggerSubjectKind::Battler
				&& Registration.Spec.Owner.BattlerId == BattlerId
				&& Registration.Spec.SourceDefinition.Kind
					== EBattleTriggerSourceDefinitionKind::Condition
				&& Registration.Spec.SourceDefinition.ConditionId == VolatileId)
			{
				return FMoveId::TryCreate(
					Registration.Spec.Rule.PayloadId,
					OutMoveId);
			}
		}
		return false;
	}

	bool TryGetChargingTargetSlot(
		const FBattleEngineState& State,
		const FBattlerId BattlerId,
		FActiveSlotId& OutTargetSlotId);

	bool TryGetChargingTargetBattler(
		const FBattleEngineState& State,
		const FBattlerId BattlerId,
		FBattlerId& OutTargetBattlerId);

	bool IsReleasingCharge(
		const FBattleEngineState& State,
		const FBattleBattlerState& Battler,
		const FMoveId MoveId);

	template <typename TState>
	FBattleEventSource FindFallbackSource(const TState& State)
	{
		FBattleEventSource Source;
		const FBattleTrainerState* PlayerTrainer = State.Trainers.FindByPredicate(
			[](const FBattleTrainerState& Trainer)
			{
				return Trainer.Role == EBattleTrainerRole::Player;
			});
		if (PlayerTrainer != nullptr)
		{
			Source.TrainerId = PlayerTrainer->TrainerId;
		}
		const FBattleActivePositionState* PlayerLeft = State.ActivePositions.FindByPredicate(
			[](const FBattleActivePositionState& Position)
			{
				return Position.ActiveSlotId.GetSide() == EBattleSide::Player
					&& Position.ActiveSlotId.GetPosition() == EBattlePosition::Left;
			});
		if (PlayerLeft != nullptr)
		{
			Source.TrainerId = PlayerLeft->TrainerId;
			Source.BattlerId = PlayerLeft->BattlerId;
			Source.ActiveSlotId = PlayerLeft->ActiveSlotId;
		}
		return Source;
	}

	FBattleEventSource SourceFromRequest(
		const FBattleEngineState& State,
		const FBattleDecisionRequest* Request,
		const FBattleDecision* Decision);

	template <typename TState>
	FBattleEventSource SourceFromLockedAction(
		const TState& State,
		const FBattleLockedActionState& Action)
	{
		FBattleEventSource Source = FindFallbackSource(State);
		Source.TrainerId = Action.Decision.GetDecisionOwnerTrainerId();
		Source.BattlerId = Action.Decision.GetActingBattlerId();
		Source.ActiveSlotId = Action.OrderKey.ActingSlotId;
		if (Action.Decision.GetActionKind() == EBattleActionKind::Fight)
		{
			Source.DefinitionId = Action.Decision.GetMoveId().GetDefinitionId();
		}
		else if (Action.Decision.GetActionKind() == EBattleActionKind::Bag)
		{
			Source.DefinitionId = Action.Decision.GetItemId().GetDefinitionId();
		}
		return Source;
	}

	FBattleResolution MakeRejectedResolution(
		FBattleEngineState& State,
		const FResolutionId ResolutionId,
		const FBattleRejection& Rejection,
		const EBattleEventType EventType,
		const EBattleEventCause Cause,
		const EBattleActionKind ActionKind,
		const FBattleEventSource& Source);

	bool ActiveSlotLess(const FActiveSlotId& Left, const FActiveSlotId& Right);

	template <typename ElementType>
	void AddUnique(TArray<ElementType>& Values, const ElementType& Value)
	{
		if (!Values.Contains(Value))
		{
			Values.Add(Value);
		}
	}

	template <typename TState>
	const FBattleActivePositionState* FindActiveForBattler(
		const TState& State,
		const FBattlerId BattlerId)
	{
		return State.ActivePositions.FindByPredicate(
			[BattlerId](const FBattleActivePositionState& Position)
			{
				return Position.BattlerId == BattlerId;
			});
	}

	bool IsLivingSelectableBattler(const FBattleBattlerState* Battler);

	const FBattleBattlerState* FindLeftmostLivingWildOpponent(
		const FBattleEngineState& State);

	const FBattleWildFleePolicyState* FindWildFleePolicy(
		const FBattleEngineState& State,
		const FBattleBattlerState& Battler);

	template <typename TState>
	const FBattleTrainerEncounterPolicy* FindTrainerEncounterPolicy(
		const TState& State,
		const FTrainerId TrainerId)
	{
		return State.CompiledEncounterPolicies.FindTrainerPolicy(TrainerId);
	}

	bool CanOfferRunAction(
		const FBattleEngineState& State,
		const FBattleTrainerState& Trainer,
		const FBattleBattlerState& Battler);

	bool CanOfferWildFleeAction(
		const FBattleEngineState& State,
		const FBattleTrainerState& Trainer,
		const FBattleBattlerState& Battler);

	class FNoDrawBattleRandom final : public IBattleRandom
	{
	public:
		virtual bool TryDrawUniform(
			uint32 InclusiveMinimum,
			uint32 InclusiveMaximum,
			const FBattleRandomContext& Context,
			FBattleRandomDraw& OutDraw) override
		{
			(void)InclusiveMinimum;
			(void)InclusiveMaximum;
			(void)Context;
			OutDraw = FBattleRandomDraw();
			return false;
		}

		virtual TConstArrayView<FBattleRandomDraw> GetTrace() const override
		{
			return Trace;
		}

		virtual bool TryCreateTransaction(
			FResolutionId ResolutionId,
			FActionId OwningActionId,
			TUniquePtr<IBattleRandomTransaction>& OutTransaction) override
		{
			(void)ResolutionId;
			(void)OwningActionId;
			OutTransaction.Reset();
			return false;
		}

	private:
		TArray<FBattleRandomDraw> Trace;
	};
}
