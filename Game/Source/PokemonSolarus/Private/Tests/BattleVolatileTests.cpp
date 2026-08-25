#if WITH_DEV_AUTOMATION_TESTS

#include "Battle/BattleActionQueue.h"
#include "Battle/BattleDefinitionCatalog.h"
#include "Battle/BattleEffectExecutor.h"
#include "Battle/BattleEngine.h"
#include "Battle/BattleSetup.h"
#include "Battle/BattleState.h"
#include "Battle/BattleTypeChart.h"
#include "Battle/BattleVolatile.h"
#include "BattleTestFactories.h"
#include "BattleTestRandom.h"
#include "Misc/AutomationTest.h"

class FBattleC07CEngineFixture
{
public:
	static bool ApplyVolatile(
		FBattleEngine& Engine,
		const FBattlerId TargetBattlerId,
		const FConditionId VolatileId,
		const FBattlerId SourceBattlerId,
		const TOptional<int32>& RemainingTurns = TOptional<int32>(),
		const int32 Layers = 1,
		const FDefinitionId& PayloadId = FDefinitionId(),
		const TArray<FBattleTriggerSubject>& Targets = TArray<FBattleTriggerSubject>(),
		const bool bSuppressed = false)
	{
		if (!Engine.State.IsValid())
		{
			return false;
		}
		FBattleBattlerState* Battler = Engine.State->FindMutableBattler(TargetBattlerId);
		if (Battler == nullptr || Battler->Volatiles.ContainsByPredicate(
			[VolatileId](const FBattleConditionState& Condition)
			{
				return Condition.ConditionId == VolatileId;
			}))
		{
			return false;
		}

		FBattleTriggerSubject Owner;
		FBattleTriggerSubject Source;
		if (!FBattleTriggerSubject::TryCreateBattler(TargetBattlerId, Owner))
		{
			return false;
		}
		if (VolatileId == FBattleVolatileRules::GetLeechSeedId())
		{
			const FBattleActivePositionState* Active =
				Engine.State->ActivePositions.FindByPredicate(
					[SourceBattlerId](const FBattleActivePositionState& Candidate)
					{
						return Candidate.BattlerId == SourceBattlerId;
					});
			if (Active == nullptr
				|| !FBattleTriggerSubject::TryCreateActiveSlot(Active->ActiveSlotId, Source))
			{
				return false;
			}
		}
		else if (!FBattleTriggerSubject::TryCreateBattler(SourceBattlerId, Source))
		{
			return false;
		}

		FBattleVolatileTriggerRegistrationFacts TriggerFacts;
		TriggerFacts.VolatileId = VolatileId;
		TriggerFacts.PayloadId = PayloadId.IsValid()
			? PayloadId
			: VolatileId.GetDefinitionId();
		TriggerFacts.Owner = Owner;
		TriggerFacts.Source = Source;
		TriggerFacts.Targets = Targets;
		TriggerFacts.RemainingTurns = RemainingTurns;
		TriggerFacts.Layers = Layers;
		TriggerFacts.bSuppressed = bSuppressed;
		EBattleTriggerError TriggerError = EBattleTriggerError::None;
		if (!FBattleVolatileRules::TryRegisterTriggers(
			Engine.State->TriggerFramework,
			TriggerFacts,
			TriggerError))
		{
			return false;
		}

		FBattleConditionState Condition;
		Condition.ConditionId = VolatileId;
		Condition.RemainingTurns = RemainingTurns;
		Condition.LayerCount = Layers;
		Condition.CreationOrdinal = Engine.State->NextConditionCreationOrdinal++;
		Condition.SourceBattlerId = SourceBattlerId;
		Battler->Volatiles.Add(MoveTemp(Condition));
		TArray<FBattleTriggerLifecycleFact> Ignored;
		Engine.State->TriggerFramework.DrainLifecycleFacts(Ignored);
		return true;
	}

	static bool HasVolatile(
		const FBattleEngine& Engine,
		const FBattlerId BattlerId,
		const FConditionId VolatileId)
	{
		const FBattleBattlerState* Battler = Engine.State.IsValid()
			? Engine.State->FindBattler(BattlerId)
			: nullptr;
		return Battler != nullptr && Battler->Volatiles.ContainsByPredicate(
			[VolatileId](const FBattleConditionState& Condition)
			{
				return Condition.ConditionId == VolatileId;
			});
	}

	static int32 GetVolatileLayers(
		const FBattleEngine& Engine,
		const FBattlerId BattlerId,
		const FConditionId VolatileId)
	{
		const FBattleBattlerState* Battler = Engine.State.IsValid()
			? Engine.State->FindBattler(BattlerId)
			: nullptr;
		const FBattleConditionState* Condition = Battler != nullptr
			? Battler->Volatiles.FindByPredicate(
				[VolatileId](const FBattleConditionState& Candidate)
				{
					return Candidate.ConditionId == VolatileId;
				})
			: nullptr;
		return Condition != nullptr ? Condition->LayerCount : INDEX_NONE;
	}

	static int32 GetCurrentHP(const FBattleEngine& Engine, const FBattlerId BattlerId)
	{
		const FBattleBattlerState* Battler = Engine.State.IsValid()
			? Engine.State->FindBattler(BattlerId)
			: nullptr;
		return Battler != nullptr ? Battler->CurrentHP : INDEX_NONE;
	}

	static bool SetCurrentHP(
		FBattleEngine& Engine,
		const FBattlerId BattlerId,
		const int32 CurrentHP)
	{
		FBattleBattlerState* Battler = Engine.State.IsValid()
			? Engine.State->FindMutableBattler(BattlerId)
			: nullptr;
		if (Battler == nullptr || CurrentHP <= 0 || CurrentHP > Battler->PermanentStats.MaxHP)
		{
			return false;
		}
		Battler->CurrentHP = CurrentHP;
		Battler->bFainted = false;
		Battler->bRemoved = false;
		Battler->bFaintTransitionPending = false;
		return true;
	}

	static bool ReplaceActiveBattler(
		FBattleEngine& Engine,
		const FBattlerId OutgoingBattlerId,
		const FBattlerId IncomingBattlerId)
	{
		if (!Engine.State.IsValid())
		{
			return false;
		}
		FBattleActivePositionState* Position =
			Engine.State->ActivePositions.FindByPredicate(
				[OutgoingBattlerId](const FBattleActivePositionState& Candidate)
				{
					return Candidate.BattlerId == OutgoingBattlerId;
				});
		const FBattleBattlerState* Incoming = Engine.State->FindBattler(IncomingBattlerId);
		if (Position == nullptr
			|| Incoming == nullptr
			|| Incoming->TrainerId != Position->TrainerId
			|| Engine.State->ActivePositions.ContainsByPredicate(
				[IncomingBattlerId](const FBattleActivePositionState& Candidate)
				{
					return Candidate.BattlerId == IncomingBattlerId;
				}))
		{
			return false;
		}
		Position->BattlerId = IncomingBattlerId;
		return true;
	}

	static bool SetLastMove(
		FBattleEngine& Engine,
		const FBattlerId BattlerId,
		const FMoveId MoveId,
		const int32 CurrentPP)
	{
		FBattleBattlerState* Battler = Engine.State.IsValid()
			? Engine.State->FindMutableBattler(BattlerId)
			: nullptr;
		FBattleMoveSlotState* Slot = Battler != nullptr
			? Battler->Moves.FindByPredicate(
				[MoveId](const FBattleMoveSlotState& Candidate)
				{
					return Candidate.MoveId == MoveId;
				})
			: nullptr;
		if (Battler == nullptr || Slot == nullptr || CurrentPP < 0 || CurrentPP > Slot->MaxPP)
		{
			return false;
		}
		Battler->LastMoveId = MoveId;
		Slot->CurrentPP = CurrentPP;
		return true;
	}

	static bool PrepareEndTurn(FBattleEngine& Engine)
	{
		if (!Engine.State.IsValid())
		{
			return false;
		}
		Engine.State->Phase = EBattlePhase::EndOfTurn;
		Engine.State->LockedActions.Reset();
		Engine.State->CurrentLockedActionIndex = 0;
		Engine.State->AcceptedSelections.Reset();
		Engine.State->DecisionOwnerSequence.Reset();
		Engine.State->CurrentDecisionOwnerIndex = INDEX_NONE;
		Engine.State->CurrentDecisionActorOffset = 0;
		Engine.State->PendingDecision.Reset();
		Engine.State->PendingDecisionRequests.Reset();
		Engine.State->PendingReplacements.Reset();
		Engine.State->bEndTurnTriggerPassComplete = false;
		EBattleStateValidationError Error = EBattleStateValidationError::None;
		return Engine.State->ValidateInvariants(Error);
	}

	static bool ExecuteMove(
		FBattleEngine& Engine,
		const FBattlerId UserBattlerId,
		const FBattlerId TargetBattlerId,
		const FMoveId MoveId,
		FBattleEffectExecutionResult& OutResult)
	{
		if (!Engine.State.IsValid())
		{
			return false;
		}
		const FBattleMoveDefinition* Move = Engine.State->Catalog.FindMove(MoveId);
		const FBattleActivePositionState* User =
			Engine.State->ActivePositions.FindByPredicate(
				[UserBattlerId](const FBattleActivePositionState& Candidate)
				{
					return Candidate.BattlerId == UserBattlerId;
				});
		const FBattleActivePositionState* Target =
			Engine.State->ActivePositions.FindByPredicate(
				[TargetBattlerId](const FBattleActivePositionState& Candidate)
				{
					return Candidate.BattlerId == TargetBattlerId;
				});
		if (Move == nullptr || User == nullptr || Target == nullptr)
		{
			return false;
		}
		FBattleBattlerTarget BattlerTarget;
		BattlerTarget.ActiveSlotId = Target->ActiveSlotId;
		BattlerTarget.BattlerId = TargetBattlerId;
		FBattleResolvedTarget ResolvedTarget;
		if (!FBattleResolvedTarget::TryCreateBattler(BattlerTarget, ResolvedTarget))
		{
			return false;
		}
		FBattleEffectExecutionRequest Request;
		Request.BattleId = Engine.State->Setup.GetBattleId();
		Request.TurnId = Engine.State->TurnId;
		Request.ActionId = BattleTest::MakeNumericId<FActionId>(700);
		Request.ResolutionId = BattleTest::MakeNumericId<FResolutionId>(700);
		Request.UserBattlerId = UserBattlerId;
		Request.UserSlotId = User->ActiveSlotId;
		Request.Move = Move;
		Request.Targets.Add(ResolvedTarget);
		EBattleEffectExecutorError Error = EBattleEffectExecutorError::None;
		return FBattleEffectExecutor::TryExecuteAgainstState(
			Request,
			*Engine.State,
			OutResult,
			Error);
	}

	static bool IsVolatileSuppressed(
		const FBattleEngine& Engine,
		const FBattlerId BattlerId,
		const FConditionId VolatileId)
	{
		if (!Engine.State.IsValid())
		{
			return false;
		}
		for (const FBattleTriggerRegistrationState& Registration :
			Engine.State->TriggerFramework.GetActiveRegistrations())
		{
			if (Registration.Spec.Owner.Kind == EBattleTriggerSubjectKind::Battler
				&& Registration.Spec.Owner.BattlerId == BattlerId
				&& Registration.Spec.SourceDefinition.Kind
					== EBattleTriggerSourceDefinitionKind::Condition
				&& Registration.Spec.SourceDefinition.ConditionId == VolatileId
				&& Registration.bSuppressed)
			{
				return true;
			}
		}
		return false;
	}

	static bool IsVolatileActiveForPhase(
		const FBattleEngine& Engine,
		const FBattlerId BattlerId,
		const FConditionId VolatileId,
		const EBattleTriggerPhase Phase)
	{
		if (!Engine.State.IsValid())
		{
			return false;
		}
		return Engine.State->TriggerFramework.GetActiveRegistrations().ContainsByPredicate(
			[BattlerId, VolatileId, Phase](
				const FBattleTriggerRegistrationState& Registration)
			{
				return Registration.Spec.Owner.Kind == EBattleTriggerSubjectKind::Battler
					&& Registration.Spec.Owner.BattlerId == BattlerId
					&& Registration.Spec.SourceDefinition.Kind
						== EBattleTriggerSourceDefinitionKind::Condition
					&& Registration.Spec.SourceDefinition.ConditionId == VolatileId
					&& Registration.Spec.Rule.Phase == Phase
					&& !Registration.bSuppressed;
			});
	}

	static int32 GetMovePP(
		const FBattleEngine& Engine,
		const FBattlerId BattlerId,
		const FMoveId MoveId)
	{
		const FBattleBattlerState* Battler = Engine.State.IsValid()
			? Engine.State->FindBattler(BattlerId)
			: nullptr;
		const FBattleMoveSlotState* Move = Battler != nullptr
			? Battler->Moves.FindByPredicate(
				[MoveId](const FBattleMoveSlotState& Candidate)
				{
					return Candidate.MoveId == MoveId;
				})
			: nullptr;
		return Move != nullptr ? Move->CurrentPP : INDEX_NONE;
	}

	static int32 GetTotalMovePP(
		const FBattleEngine& Engine,
		const FBattlerId BattlerId)
	{
		const FBattleBattlerState* Battler = Engine.State.IsValid()
			? Engine.State->FindBattler(BattlerId)
			: nullptr;
		if (Battler == nullptr)
		{
			return INDEX_NONE;
		}
		int32 Total = 0;
		for (const FBattleMoveSlotState& Move : Battler->Moves)
		{
			Total += Move.CurrentPP;
		}
		return Total;
	}

	static FBattlerId GetActiveBattlerId(
		const FBattleEngine& Engine,
		const FActiveSlotId ActiveSlotId)
	{
		const FBattleActivePositionState* Active = Engine.State.IsValid()
			? Engine.State->FindActivePosition(ActiveSlotId)
			: nullptr;
		return Active != nullptr ? Active->BattlerId : FBattlerId();
	}

	static bool PrepareStartedFight(
		FBattleEngine& Engine,
		const FBattlerId UserBattlerId,
		const FMoveId MoveId,
		const FActiveSlotId TargetSlotId,
		const FBattlerId SelectedTargetBattlerId)
	{
		if (!Engine.State.IsValid())
		{
			return false;
		}
		const FBattleBattlerState* User = Engine.State->FindBattler(UserBattlerId);
		const FBattleActivePositionState* UserActive =
			Engine.State->ActivePositions.FindByPredicate(
				[UserBattlerId](const FBattleActivePositionState& Candidate)
				{
					return Candidate.BattlerId == UserBattlerId;
				});
		const FBattleMoveDefinition* Move = Engine.State->Catalog.FindMove(MoveId);
		if (User == nullptr || UserActive == nullptr || Move == nullptr)
		{
			return false;
		}

		FBattleDecision Decision;
		if (!FBattleDecision::TryCreateFight(
				Engine.State->StateVersion,
				User->TrainerId,
				UserBattlerId,
				MoveId,
				TargetSlotId,
				Decision))
		{
			return false;
		}

		FBattleLockedActionState Action;
		Action.ActionId = BattleTest::MakeNumericId<FActionId>(901);
		Action.QueueOrdinal = 1;
		Action.Decision = MoveTemp(Decision);
		Action.OrderKey.CommandBand = EBattleActionCommandBand::Move;
		Action.OrderKey.MovePriority = Move->Priority;
		Action.OrderKey.EffectiveSpeed = User->PermanentStats.Speed;
		Action.OrderKey.ActingSlotId = UserActive->ActiveSlotId;
		Action.TargetClass = Move->TargetClass;
		Action.SelectedTargetBattlerId = SelectedTargetBattlerId;
		Action.bStarted = true;
		Engine.State->LockedActions.Reset();
		Engine.State->LockedActions.Add(MoveTemp(Action));
		Engine.State->CurrentLockedActionIndex = 0;
		Engine.State->Phase = EBattlePhase::Resolving;
		EBattleStateValidationError Error = EBattleStateValidationError::None;
		return Engine.State->ValidateInvariants(Error);
	}

	static bool PrepareLockedSwitch(
		FBattleEngine& Engine,
		const FBattlerId UserBattlerId,
		const FPartySlotId IncomingPartySlotId)
	{
		if (!Engine.State.IsValid())
		{
			return false;
		}
		const FBattleBattlerState* User = Engine.State->FindBattler(UserBattlerId);
		const FBattleActivePositionState* UserActive =
			Engine.State->ActivePositions.FindByPredicate(
				[UserBattlerId](const FBattleActivePositionState& Candidate)
				{
					return Candidate.BattlerId == UserBattlerId;
				});
		FBattleTrainerState* Trainer = User != nullptr
			? Engine.State->FindMutableTrainer(User->TrainerId)
			: nullptr;
		if (User == nullptr || UserActive == nullptr || Trainer == nullptr)
		{
			return false;
		}

		FBattleDecision Decision;
		if (!FBattleDecision::TryCreateSwitch(
				Engine.State->StateVersion,
				EBattleDecisionRequestKind::Action,
				User->TrainerId,
				UserBattlerId,
				IncomingPartySlotId,
				UserActive->ActiveSlotId,
				Decision))
		{
			return false;
		}

		FBattleLockedActionState Action;
		Action.ActionId = BattleTest::MakeNumericId<FActionId>(902);
		Action.QueueOrdinal = 1;
		Action.Decision = MoveTemp(Decision);
		Action.OrderKey.CommandBand = EBattleActionCommandBand::VoluntarySwitch;
		Action.OrderKey.EffectiveSpeed = User->PermanentStats.Speed;
		Action.OrderKey.ActingSlotId = UserActive->ActiveSlotId;
		Engine.State->LockedActions.Reset();
		Engine.State->LockedActions.Add(MoveTemp(Action));
		Engine.State->CurrentLockedActionIndex = 0;
		Engine.State->Phase = EBattlePhase::Locked;
		Trainer->ActionAllowance.MaximumActions = 1;
		Trainer->ActionAllowance.RemainingActions = 1;
		EBattleStateValidationError Error = EBattleStateValidationError::None;
		return Engine.State->ValidateInvariants(Error);
	}

	static bool MarkFaintedAndRemoveActive(
		FBattleEngine& Engine,
		const FBattlerId BattlerId)
	{
		if (!Engine.State.IsValid())
		{
			return false;
		}
		FBattleBattlerState* Battler = Engine.State->FindMutableBattler(BattlerId);
		FBattleActivePositionState* Active =
			Engine.State->ActivePositions.FindByPredicate(
				[BattlerId](const FBattleActivePositionState& Candidate)
				{
					return Candidate.BattlerId == BattlerId;
				});
		if (Battler == nullptr || Active == nullptr)
		{
			return false;
		}
		Battler->CurrentHP = 0;
		Battler->bFainted = true;
		Battler->bFaintTransitionPending = false;
		Battler->bRemoved = true;
		Battler->LastMoveId = FMoveId();
		Active->TrainerId = FTrainerId();
		Active->BattlerId = FBattlerId();
		EBattleStateValidationError Error = EBattleStateValidationError::None;
		return Engine.State->ValidateInvariants(Error);
	}
};

namespace BattleVolatileTests
{
	using BattleTest::MakeActiveSlotId;
	using BattleTest::MakeDefinitionId;
	using BattleTest::MakeNumericId;
	using BattleTest::MakePartySlotId;

	constexpr uint64 PlayerTrainerValue = 1;
	constexpr uint64 OpponentTrainerValue = 2;
	constexpr uint64 PlayerBattlerValue = 11;
	constexpr uint64 PlayerReserveValue = 12;
	constexpr uint64 OpponentBattlerValue = 21;
	constexpr uint64 OpponentRightBattlerValue = 22;
	const TCHAR* AbilityName = TEXT("Ability.C07C.Test");
	const TCHAR* SpeciesName = TEXT("Species.C07C.Test");
	const TCHAR* DamageMoveName = TEXT("Move.C07C.Damage");
	const TCHAR* AlternateMoveName = TEXT("Move.C07C.Alternate");
	const TCHAR* StatusMoveName = TEXT("Move.C07C.Status");
	const TCHAR* SubstituteMoveName = TEXT("Move.C07C.Substitute");
	const TCHAR* ChargeMoveName = TEXT("Move.C07C.FlyCharge");
	const TCHAR* RechargeMoveName = TEXT("Move.C07C.Recharge");
	const TCHAR* ReachMoveName = TEXT("Move.C07C.ReachFly");
	const TCHAR* BreakProtectMoveName = TEXT("Move.C07C.BreakProtect");

	using FExpectedDraw = BattleTest::FBattleExpectedRandomDraw;
	using FScriptedVolatileRandom = BattleTest::FStrictBattleRandom;

	FBattleRandomContext MakeRandomContext(const FDefinitionId& Purpose)
	{
		FBattleRandomContext Context;
		Context.BattleId = MakeNumericId<FBattleId>(7071);
		Context.TurnId = MakeNumericId<FTurnId>(1);
		Context.ActionId = MakeNumericId<FActionId>(1);
		Context.ResolutionId = MakeNumericId<FResolutionId>(1);
		Context.RulePurpose = Purpose;
		return Context;
	}

	TArray<FBattleTypeChartEntry> MakeNeutralTypeChart()
	{
		TArray<FBattleTypeChartEntry> Entries;
		Entries.Reserve(FBattleTypeChart::EntryCount);
		for (int32 Attack = 0; Attack < FBattleTypeChart::TypeCount; ++Attack)
		{
			for (int32 Defense = 0; Defense < FBattleTypeChart::TypeCount; ++Defense)
			{
				Entries.Add(
					{
						static_cast<EPokemonType>(Attack),
						static_cast<EPokemonType>(Defense),
						1,
						1
					});
			}
		}
		return Entries;
	}

	FBattleMoveDefinition MakeDamageMove(
		const TCHAR* Name,
		const int32 Power = 40,
		const EBattleMoveFlags ExtraFlags = EBattleMoveFlags::None)
	{
		FBattleMoveDefinition Move;
		Move.Id = MakeDefinitionId<FMoveId>(Name);
		Move.Type = EPokemonType::Normal;
		Move.Category = EBattleMoveCategory::Physical;
		Move.Power = Power;
		Move.bAlwaysHits = true;
		Move.bUsesPP = true;
		Move.BasePP = 20;
		Move.bAllowsPPBoosts = true;
		Move.TargetClass = EBattleTargetClass::SelectedOpponent;
		Move.Flags = EBattleMoveFlags::NeverCritical
			| EBattleMoveFlags::BlockedByProtect
			| ExtraFlags;
		FBattleMoveEffectDescriptor Damage;
		Damage.Kind = EBattleMoveEffectKind::Damage;
		Damage.Target = EBattleEffectTarget::ResolvedTarget;
		Move.Effects.Add(Damage);
		return Move;
	}

	FBattleMoveDefinition MakeStatusMove()
	{
		FBattleMoveDefinition Move;
		Move.Id = MakeDefinitionId<FMoveId>(StatusMoveName);
		Move.Type = EPokemonType::Normal;
		Move.Category = EBattleMoveCategory::Status;
		Move.bAlwaysHits = true;
		Move.bUsesPP = true;
		Move.BasePP = 20;
		Move.bAllowsPPBoosts = true;
		Move.TargetClass = EBattleTargetClass::SelectedOpponent;
		Move.Flags = EBattleMoveFlags::BlockedByProtect;
		FBattleMoveEffectDescriptor Effect;
		Effect.Kind = EBattleMoveEffectKind::ApplyCondition;
		Effect.Target = EBattleEffectTarget::ResolvedTarget;
		Effect.ConditionId = FBattleVolatileRules::GetConfusionId();
		Move.Effects.Add(Effect);
		return Move;
	}

	FBattleMoveDefinition MakeSubstituteMove()
	{
		FBattleMoveDefinition Move;
		Move.Id = MakeDefinitionId<FMoveId>(SubstituteMoveName);
		Move.Type = EPokemonType::Normal;
		Move.Category = EBattleMoveCategory::Status;
		Move.bAlwaysHits = true;
		Move.bUsesPP = true;
		Move.BasePP = 20;
		Move.bAllowsPPBoosts = true;
		Move.TargetClass = EBattleTargetClass::Self;
		FBattleMoveEffectDescriptor Effect;
		Effect.Kind = EBattleMoveEffectKind::ApplyCondition;
		Effect.Target = EBattleEffectTarget::User;
		Effect.ConditionId = FBattleVolatileRules::GetSubstituteId();
		Move.Effects.Add(Effect);
		return Move;
	}

	FBattleMoveDefinition MakeChargeMove()
	{
		FBattleMoveDefinition Move = MakeDamageMove(ChargeMoveName, 60);
		Move.Effects.Reset();
		FBattleMoveEffectDescriptor Charge;
		Charge.Order = 0;
		Charge.Kind = EBattleMoveEffectKind::Charge;
		Charge.Target = EBattleEffectTarget::User;
		Charge.ConditionId = FBattleVolatileRules::GetChargingId();
		Move.Effects.Add(Charge);
		FBattleMoveEffectDescriptor Fly;
		Fly.Order = 1;
		Fly.Kind = EBattleMoveEffectKind::SemiInvulnerability;
		Fly.Target = EBattleEffectTarget::User;
		Fly.ConditionId = FBattleVolatileRules::GetFlySemiInvulnerableId();
		Move.Effects.Add(Fly);
		FBattleMoveEffectDescriptor Damage;
		Damage.Order = 2;
		Damage.Kind = EBattleMoveEffectKind::Damage;
		Damage.Target = EBattleEffectTarget::ResolvedTarget;
		Move.Effects.Add(Damage);
		return Move;
	}

	FBattleMoveDefinition MakeRechargeMove()
	{
		FBattleMoveDefinition Move = MakeDamageMove(RechargeMoveName, 70);
		Move.Effects[0].Order = 0;
		FBattleMoveEffectDescriptor Recharge;
		Recharge.Order = 1;
		Recharge.Kind = EBattleMoveEffectKind::Recharge;
		Recharge.Target = EBattleEffectTarget::User;
		Recharge.ConditionId = FBattleVolatileRules::GetRechargeId();
		Move.Effects.Add(Recharge);
		return Move;
	}

	FBattleDefinitionCatalogInput MakeCatalogInput()
	{
		FBattleDefinitionCatalogInput Input;
		Input.TypeChartEntries = MakeNeutralTypeChart();
		Input.Moves = {
			MakeDamageMove(DamageMoveName, 200),
			MakeDamageMove(AlternateMoveName),
			MakeStatusMove(),
			MakeSubstituteMove(),
			MakeChargeMove(),
			MakeRechargeMove(),
			MakeDamageMove(
				BreakProtectMoveName,
				40,
				EBattleMoveFlags::BreaksProtection),
			MakeDamageMove(
				ReachMoveName,
				40,
				EBattleMoveFlags::ReachesAirborneSemiInvulnerableTarget
					| EBattleMoveFlags::DoublesPowerAgainstAirborneSemiInvulnerableTarget)
		};
		Input.Abilities.Add({MakeDefinitionId<FAbilityId>(AbilityName)});
		FBattleSpeciesFormDefinition Species;
		Species.Id = MakeDefinitionId<FSpeciesFormId>(SpeciesName);
		Species.PrimaryType = EPokemonType::Normal;
		Species.BaseStats = {80, 80, 80, 80, 80, 80};
		Species.CatchRate = 45;
		Species.AbilityChoices.Add(MakeDefinitionId<FAbilityId>(AbilityName));
		Input.SpeciesForms.Add(Species);
		for (const FConditionId& Id : FBattleVolatileRules::GetCanonicalIds())
		{
			Input.Conditions.Add({Id, EBattleConditionKind::Volatile});
		}
		return Input;
	}

	FBattleDefinitionCatalog MakeCatalog()
	{
		const FBattleDefinitionCatalogInput Input = MakeCatalogInput();
		FBattleDefinitionCatalog Catalog;
		TArray<FBattleCatalogDiagnostic> Diagnostics;
		const bool bCreated = FBattleDefinitionCatalog::TryCreate(
			Input,
			Catalog,
			Diagnostics);
		check(bCreated);
		return Catalog;
	}

	FBattleTrainerSetup MakeTrainer(
		const uint64 Value,
		const EBattleSide Side,
		const EBattleTrainerRole Role,
		const EBattleDecisionController Controller)
	{
		FBattleTrainerSetup Trainer;
		Trainer.TrainerId = MakeNumericId<FTrainerId>(Value);
		Trainer.Side = Side;
		Trainer.Role = Role;
		Trainer.Controller = Controller;
		Trainer.SelectorProfileId = MakeDefinitionId<FDefinitionId>(
			Role == EBattleTrainerRole::Player
				? TEXT("Selector.C07C.Player")
				: TEXT("Selector.C07C.Opponent"));
		return Trainer;
	}

	FBattlePartyEntrySetup MakePartyEntryWithMoves(
		const uint64 TrainerValue,
		const uint64 BattlerValue,
		const int32 PartyIndex,
		const int32 Speed,
		const TArray<FMoveId>& Moves)
	{
		FBattlePartyEntrySetup Entry;
		Entry.TrainerId = MakeNumericId<FTrainerId>(TrainerValue);
		Entry.BattlerId = MakeNumericId<FBattlerId>(BattlerValue);
		Entry.SourcePokemonId = MakeNumericId<FSourcePokemonId>(1000 + BattlerValue);
		Entry.PartySlotId = MakePartySlotId(PartyIndex);
		Entry.SpeciesFormId = MakeDefinitionId<FSpeciesFormId>(SpeciesName);
		Entry.Level = 50;
		Entry.Stats = {160, 100, 100, 100, 100, Speed};
		Entry.CurrentHP = 160;
		Entry.AbilityId = MakeDefinitionId<FAbilityId>(AbilityName);
		for (int32 Index = 0; Index < Moves.Num(); ++Index)
		{
			Entry.Moves.Add({static_cast<uint8>(Index), Moves[Index], 20, 20});
		}
		return Entry;
	}

	FBattlePartyEntrySetup MakePartyEntry(
		const uint64 TrainerValue,
		const uint64 BattlerValue,
		const int32 PartyIndex,
		const int32 Speed)
	{
		return MakePartyEntryWithMoves(
			TrainerValue,
			BattlerValue,
			PartyIndex,
			Speed,
			{
				MakeDefinitionId<FMoveId>(DamageMoveName),
				MakeDefinitionId<FMoveId>(AlternateMoveName),
				MakeDefinitionId<FMoveId>(StatusMoveName),
				MakeDefinitionId<FMoveId>(SubstituteMoveName)
			});
	}

	bool TryCreateEngine(
		const uint64 BattleValue,
		const uint64 Seed,
		TUniquePtr<FBattleEngine>& OutEngine)
	{
		FBattleSetupInput Input;
		Input.BattleId = MakeNumericId<FBattleId>(BattleValue);
		Input.SettingsReference =
			{MakeDefinitionId<FDefinitionId>(TEXT("Settings.C07C")), 1};
		Input.CatalogReference =
			{MakeDefinitionId<FDefinitionId>(TEXT("Catalog.C07C")), 1};
		Input.EncounterKind = EBattleEncounterKind::Trainer;
		Input.Format = EBattleFormat::Single;
		Input.CaptureCapacity = {2, 100};
		Input.Policies.bBagAllowed = false;
		Input.Policies.bRunAllowed = false;
		Input.Policies.bCaptureAllowed = false;
		Input.Policies.bShiftPromptEligible = false;
		Input.Trainers.Add(MakeTrainer(
			PlayerTrainerValue,
			EBattleSide::Player,
			EBattleTrainerRole::Player,
			EBattleDecisionController::Human));
		Input.Trainers.Add(MakeTrainer(
			OpponentTrainerValue,
			EBattleSide::Opponent,
			EBattleTrainerRole::Opponent,
			EBattleDecisionController::EnemyAI));
		Input.PartyEntries.Add(MakePartyEntry(PlayerTrainerValue, PlayerBattlerValue, 0, 101));
		Input.PartyEntries.Add(MakePartyEntry(PlayerTrainerValue, PlayerReserveValue, 1, 90));
		Input.PartyEntries.Add(MakePartyEntry(OpponentTrainerValue, OpponentBattlerValue, 0, 80));
		Input.StartingActive.Add(
			{
				MakeActiveSlotId(EBattleSide::Player, EBattlePosition::Left),
				MakeNumericId<FTrainerId>(PlayerTrainerValue),
				MakeNumericId<FBattlerId>(PlayerBattlerValue)
			});
		Input.StartingActive.Add(
			{
				MakeActiveSlotId(EBattleSide::Opponent, EBattlePosition::Left),
				MakeNumericId<FTrainerId>(OpponentTrainerValue),
				MakeNumericId<FBattlerId>(OpponentBattlerValue)
			});
		FBattleSetup Setup;
		EBattleSetupValidationError SetupError = EBattleSetupValidationError::None;
		if (!FBattleSetup::TryCreate(Input, Setup, SetupError))
		{
			return false;
		}
		FBattleRejection Rejection;
		return FBattleEngine::TryCreate(
			Setup,
			MakeCatalog(),
			MakeUnique<FSeededBattleRandom>(Seed),
			OutEngine,
			Rejection);
	}

	bool TryCreateDoubleChargeEngine(
		const uint64 BattleValue,
		const uint64 Seed,
		TUniquePtr<FBattleEngine>& OutEngine)
	{
		FBattleSetupInput Input;
		Input.BattleId = MakeNumericId<FBattleId>(BattleValue);
		Input.SettingsReference =
			{MakeDefinitionId<FDefinitionId>(TEXT("Settings.C07C.Double")), 1};
		Input.CatalogReference =
			{MakeDefinitionId<FDefinitionId>(TEXT("Catalog.C07C.Double")), 1};
		Input.EncounterKind = EBattleEncounterKind::Trainer;
		Input.Format = EBattleFormat::Double;
		Input.CaptureCapacity = {4, 100};
		Input.Policies.bBagAllowed = false;
		Input.Policies.bRunAllowed = false;
		Input.Policies.bCaptureAllowed = false;
		Input.Policies.bShiftPromptEligible = false;
		Input.Trainers.Add(MakeTrainer(
			PlayerTrainerValue,
			EBattleSide::Player,
			EBattleTrainerRole::Player,
			EBattleDecisionController::Human));
		Input.Trainers.Add(MakeTrainer(
			OpponentTrainerValue,
			EBattleSide::Opponent,
			EBattleTrainerRole::Opponent,
			EBattleDecisionController::EnemyAI));
		const TArray<FMoveId> Moves = {
			MakeDefinitionId<FMoveId>(ChargeMoveName),
			MakeDefinitionId<FMoveId>(AlternateMoveName),
			MakeDefinitionId<FMoveId>(StatusMoveName),
			MakeDefinitionId<FMoveId>(SubstituteMoveName)
		};
		Input.PartyEntries.Add(MakePartyEntryWithMoves(
			PlayerTrainerValue, PlayerBattlerValue, 0, 120, Moves));
		Input.PartyEntries.Add(MakePartyEntryWithMoves(
			PlayerTrainerValue, PlayerReserveValue, 1, 110, Moves));
		Input.PartyEntries.Add(MakePartyEntryWithMoves(
			OpponentTrainerValue, OpponentBattlerValue, 0, 90, Moves));
		Input.PartyEntries.Add(MakePartyEntryWithMoves(
			OpponentTrainerValue, OpponentRightBattlerValue, 1, 80, Moves));
		Input.StartingActive.Add(
			{
				MakeActiveSlotId(EBattleSide::Player, EBattlePosition::Left),
				MakeNumericId<FTrainerId>(PlayerTrainerValue),
				MakeNumericId<FBattlerId>(PlayerBattlerValue)
			});
		Input.StartingActive.Add(
			{
				MakeActiveSlotId(EBattleSide::Player, EBattlePosition::Right),
				MakeNumericId<FTrainerId>(PlayerTrainerValue),
				MakeNumericId<FBattlerId>(PlayerReserveValue)
			});
		Input.StartingActive.Add(
			{
				MakeActiveSlotId(EBattleSide::Opponent, EBattlePosition::Left),
				MakeNumericId<FTrainerId>(OpponentTrainerValue),
				MakeNumericId<FBattlerId>(OpponentBattlerValue)
			});
		Input.StartingActive.Add(
			{
				MakeActiveSlotId(EBattleSide::Opponent, EBattlePosition::Right),
				MakeNumericId<FTrainerId>(OpponentTrainerValue),
				MakeNumericId<FBattlerId>(OpponentRightBattlerValue)
			});
		FBattleSetup Setup;
		EBattleSetupValidationError SetupError = EBattleSetupValidationError::None;
		if (!FBattleSetup::TryCreate(Input, Setup, SetupError))
		{
			return false;
		}
		FBattleRejection Rejection;
		return FBattleEngine::TryCreate(
			Setup,
			MakeCatalog(),
			MakeUnique<FSeededBattleRandom>(Seed),
			OutEngine,
			Rejection);
	}

	bool TryMakeChargeTargets(
		const FActiveSlotId TargetSlotId,
		const FBattlerId TargetBattlerId,
		TArray<FBattleTriggerSubject>& OutTargets)
	{
		OutTargets.Reset();
		FBattleTriggerSubject SlotTarget;
		FBattleTriggerSubject BattlerTarget;
		if (!FBattleTriggerSubject::TryCreateActiveSlot(TargetSlotId, SlotTarget)
			|| !FBattleTriggerSubject::TryCreateBattler(TargetBattlerId, BattlerTarget))
		{
			return false;
		}
		OutTargets.Add(MoveTemp(SlotTarget));
		OutTargets.Add(MoveTemp(BattlerTarget));
		return true;
	}

	bool HasUnavailableReason(
		const FBattleDecisionRequest& Request,
		const FMoveId MoveId,
		const EBattleOptionUnavailableReason Reason)
	{
		return Request.GetUnavailableOptions().ContainsByPredicate(
			[MoveId, Reason](const FBattleUnavailableDecisionOption& Option)
			{
				return Option.Kind == EBattleDecisionOptionKind::Move
					&& Option.MoveId == MoveId
					&& Option.Reason == Reason;
			});
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC07CCanonicalTriggerContractTest,
	"PokemonSolarus.Battle.C07C.Contract.CanonicalIdsTriggersAndCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC07CCanonicalTriggerContractTest::RunTest(const FString& Parameters)
{
	using namespace BattleVolatileTests;
	(void)Parameters;
	const TArray<FConditionId> Ids = FBattleVolatileRules::GetCanonicalIds();
	const TArray<FName> ExpectedNames = {
		FName(TEXT("Condition.Confusion")),
		FName(TEXT("Condition.Flinch")),
		FName(TEXT("Condition.Protect")),
		FName(TEXT("Condition.LeechSeed")),
		FName(TEXT("Condition.PartialTrap")),
		FName(TEXT("Condition.Trap")),
		FName(TEXT("Condition.Taunt")),
		FName(TEXT("Condition.Encore")),
		FName(TEXT("Condition.Disable")),
		FName(TEXT("Condition.Substitute")),
		FName(TEXT("Condition.Charging")),
		FName(TEXT("Condition.Recharge")),
		FName(TEXT("Condition.FlySemiInvulnerable"))
	};
	TestEqual(TEXT("Exactly thirteen canonical volatile IDs exist"), Ids.Num(), 13);
	for (int32 Index = 0; Index < FMath::Min(Ids.Num(), ExpectedNames.Num()); ++Index)
	{
		TestEqual(
			*FString::Printf(TEXT("Canonical volatile %d has the frozen name"), Index),
			Ids[Index].GetDefinitionId().GetName(),
			ExpectedNames[Index]);
		TestTrue(TEXT("Every listed volatile is canonical"), FBattleVolatileRules::IsCanonical(Ids[Index]));
	}

	FBattleTriggerSubject Owner;
	FBattleTriggerSubject Source;
	FBattleTriggerSubject LockedTarget;
	TestTrue(TEXT("Owner subject is valid"), FBattleTriggerSubject::TryCreateBattler(
		MakeNumericId<FBattlerId>(PlayerBattlerValue), Owner));
	TestTrue(TEXT("Source subject is valid"), FBattleTriggerSubject::TryCreateBattler(
		MakeNumericId<FBattlerId>(OpponentBattlerValue), Source));
	TestTrue(TEXT("Locked target subject is valid"), FBattleTriggerSubject::TryCreateActiveSlot(
		MakeActiveSlotId(EBattleSide::Opponent, EBattlePosition::Left), LockedTarget));

	FBattleVolatileTriggerRegistrationFacts Confusion;
	Confusion.VolatileId = FBattleVolatileRules::GetConfusionId();
	Confusion.PayloadId = Confusion.VolatileId.GetDefinitionId();
	Confusion.Owner = Owner;
	Confusion.Source = Source;
	Confusion.RemainingTurns = 5;
	TArray<FBattleTriggerRegistrationSpec> Specs;
	TestTrue(TEXT("Confusion registration builds"),
		FBattleVolatileRules::TryBuildTriggerRegistrationSpecs(Confusion, Specs));
	TestEqual(TEXT("Confusion owns one trigger"), Specs.Num(), 1);
	if (Specs.Num() == 1)
	{
		TestEqual(TEXT("Confusion gates before action"), Specs[0].Rule.Phase,
			EBattleTriggerPhase::BeforeAction);
		TestTrue(TEXT("Confusion decrements before its gate"),
			Specs[0].Rule.bDecrementDurationBeforeEffect);
		TestTrue(TEXT("Confusion duration is present"), Specs[0].RemainingTurns.IsSet());
		if (Specs[0].RemainingTurns.IsSet())
		{
			TestEqual(TEXT("Confusion duration is preserved"),
				Specs[0].RemainingTurns.GetValue(), 5);
		}
	}

	FBattleVolatileTriggerRegistrationFacts Protect;
	Protect.VolatileId = FBattleVolatileRules::GetProtectId();
	Protect.PayloadId = Protect.VolatileId.GetDefinitionId();
	Protect.Owner = Owner;
	Protect.Source = Owner;
	Protect.Layers = 3;
	Protect.bSuppressed = true;
	TestTrue(TEXT("Protect chain registration builds"),
		FBattleVolatileRules::TryBuildTriggerRegistrationSpecs(Protect, Specs));
	TestEqual(TEXT("Protect owns hit and cleanup triggers"), Specs.Num(), 2);
	for (const FBattleTriggerRegistrationSpec& Spec : Specs)
	{
		TestEqual(TEXT("Protect chain counter is preserved"), Spec.Layers, 3);
		TestTrue(TEXT("Protect suppression is preserved"), Spec.bSuppressed);
	}

	FBattleVolatileTriggerRegistrationFacts Charging;
	Charging.VolatileId = FBattleVolatileRules::GetChargingId();
	Charging.PayloadId = MakeDefinitionId<FMoveId>(ChargeMoveName).GetDefinitionId();
	Charging.Owner = Owner;
	Charging.Source = Owner;
	Charging.Targets.Add(LockedTarget);
	TestTrue(TEXT("Charging registration builds"),
		FBattleVolatileRules::TryBuildTriggerRegistrationSpecs(Charging, Specs));
	TestEqual(TEXT("Charging owns one trigger"), Specs.Num(), 1);
	if (Specs.Num() == 1)
	{
		TestTrue(TEXT("Charging preserves its move payload"),
			Specs[0].Rule.PayloadId == Charging.PayloadId);
		TestEqual(TEXT("Charging preserves its locked target"), Specs[0].Targets.Num(), 1);
	}

	FBattleTriggerFramework Framework;
	EBattleTriggerError TriggerError = EBattleTriggerError::None;
	TestTrue(TEXT("Protect registers atomically"),
		FBattleVolatileRules::TryRegisterTriggers(Framework, Protect, TriggerError));
	TestEqual(TEXT("Both Protect registrations are active"),
		Framework.GetActiveRegistrations().Num(), 2);
	FBattleTriggerOperationContext CleanupContext;
	CleanupContext.ReentrancyToken = MakeNumericId<FBattleTriggerReentrancyToken>(1);
	TestTrue(TEXT("Protect cleanup succeeds"), FBattleVolatileRules::TryCleanupTriggers(
		Framework,
		Protect.VolatileId,
		Owner,
		EBattleTriggerCleanupReason::Removal,
		CleanupContext,
		TriggerError));
	TestEqual(TEXT("Protect cleanup removes every sibling registration"),
		Framework.GetActiveRegistrations().Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC07CConfusionFlinchTest,
	"PokemonSolarus.Battle.C07C.Rules.ConfusionAndFlinch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC07CConfusionFlinchTest::RunTest(const FString& Parameters)
{
	using namespace BattleVolatileTests;
	(void)Parameters;
	FBattleVolatileApplicationFacts ApplicationFacts;
	ApplicationFacts.RequestedVolatileId = FBattleVolatileRules::GetConfusionId();
	ApplicationFacts.bTargetGrounded = true;
	ApplicationFacts.bMistyTerrainActive = true;
	FBattleVolatileApplicationResult Application;
	TestTrue(TEXT("Grounded Confusion application resolves"),
		FBattleVolatileRules::TryEvaluateApplication(ApplicationFacts, Application));
	TestEqual(TEXT("Misty Terrain prevents grounded Confusion"), Application.Outcome,
		EBattleVolatileApplicationOutcome::PreventedByTerrain);
	ApplicationFacts.bMistyTerrainActive = false;
	ApplicationFacts.bSafeguardActive = true;
	ApplicationFacts.bAppliedByOpponent = true;
	TestTrue(TEXT("Safeguard Confusion application resolves"),
		FBattleVolatileRules::TryEvaluateApplication(ApplicationFacts, Application));
	TestEqual(TEXT("Opposing Confusion is stopped by Safeguard"), Application.Outcome,
		EBattleVolatileApplicationOutcome::PreventedBySafeguard);
	ApplicationFacts.bBypassesSafeguard = true;
	TestTrue(TEXT("Safeguard bypass resolves"),
		FBattleVolatileRules::TryEvaluateApplication(ApplicationFacts, Application));
	TestEqual(TEXT("Explicit Safeguard bypass permits Confusion"), Application.Outcome,
		EBattleVolatileApplicationOutcome::CanApply);

	FScriptedVolatileRandom Random({
		{2, 5, 2, FBattleVolatileRules::GetConfusionDurationPurpose()},
		{0, 99, 32, FBattleVolatileRules::GetConfusionActionGatePurpose()},
		{0, 99, 33, FBattleVolatileRules::GetConfusionActionGatePurpose()}
	});
	FBattleVolatileDurationResult Duration;
	TestTrue(TEXT("Confusion duration draw succeeds"),
		FBattleVolatileRules::TryRollConfusionDuration(
			MakeRandomContext(FBattleVolatileRules::GetConfusionDurationPurpose()),
			Random,
			Duration));
	TestEqual(TEXT("Confusion accepts the lower duration endpoint"), Duration.Turns, 2);

	FBattleVolatileActionResult Gate;
	TestTrue(TEXT("Confusion expiration gate resolves without RNG"),
		FBattleVolatileRules::TryResolveConfusionBeforeAction(
			1,
			MakeRandomContext(FBattleVolatileRules::GetConfusionActionGatePurpose()),
			Random,
			Gate));
	TestEqual(TEXT("Confusion cures at zero and permits the action"), Gate.Outcome,
		EBattleVolatileActionOutcome::CuredAndAllowed);
	TestTrue(TEXT("Expired Confusion requests removal"), Gate.bRemoveVolatile);
	TestFalse(TEXT("Expiration consumes no draw"), Gate.bDrawConsumed);
	TestTrue(TEXT("Confusion self-hit gate resolves"),
		FBattleVolatileRules::TryResolveConfusionBeforeAction(
			2,
			MakeRandomContext(FBattleVolatileRules::GetConfusionActionGatePurpose()),
			Random,
			Gate));
	TestEqual(TEXT("Roll 32 causes self-hit"), Gate.Outcome,
		EBattleVolatileActionOutcome::ConfusionSelfHit);
	TestTrue(TEXT("Confusion non-self-hit gate resolves"),
		FBattleVolatileRules::TryResolveConfusionBeforeAction(
			2,
			MakeRandomContext(FBattleVolatileRules::GetConfusionActionGatePurpose()),
			Random,
			Gate));
	TestEqual(TEXT("Roll 33 permits the move"), Gate.Outcome,
		EBattleVolatileActionOutcome::Allowed);
	TestEqual(TEXT("Confusion self-hit uses base power 40"),
		FBattleVolatileRules::GetConfusionSelfHitBasePower(), 40);
	TestTrue(TEXT("Every expected Confusion draw was consumed exactly"), Random.IsExact());

	ApplicationFacts = FBattleVolatileApplicationFacts();
	ApplicationFacts.RequestedVolatileId = FBattleVolatileRules::GetFlinchId();
	ApplicationFacts.bTargetAlreadyActed = true;
	TestTrue(TEXT("Flinch application resolves"),
		FBattleVolatileRules::TryEvaluateApplication(ApplicationFacts, Application));
	TestEqual(TEXT("Flinch cannot affect an already-acted target"), Application.Outcome,
		EBattleVolatileApplicationOutcome::TargetAlreadyActed);
	TestTrue(TEXT("Flinch gate resolves"), FBattleVolatileRules::TryResolveSimpleBeforeAction(
		FBattleVolatileRules::GetFlinchId(), Gate));
	TestEqual(TEXT("Flinch denies the action"), Gate.Outcome,
		EBattleVolatileActionOutcome::Denied);
	TestTrue(TEXT("Flinch is removed after its gate"), Gate.bRemoveVolatile);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC07CProtectTest,
	"PokemonSolarus.Battle.C07C.Rules.ProtectChainAndBlocking",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC07CProtectTest::RunTest(const FString& Parameters)
{
	using namespace BattleVolatileTests;
	(void)Parameters;
	FScriptedVolatileRandom Random({
		{0, 2, 0, FBattleVolatileRules::GetProtectConsecutiveUsePurpose()},
		{0, 2, 1, FBattleVolatileRules::GetProtectConsecutiveUsePurpose()},
		{0, 728, 0, FBattleVolatileRules::GetProtectConsecutiveUsePurpose()}
	});
	const FBattleRandomContext Context = MakeRandomContext(
		FBattleVolatileRules::GetProtectConsecutiveUsePurpose());
	FBattleProtectAttemptFacts Facts;
	Facts.bHasQueuedAction = true;
	FBattleProtectAttemptResult Result;
	TestTrue(TEXT("First Protect resolves"), FBattleVolatileRules::TryResolveProtectAttempt(
		Facts, Context, Random, Result));
	TestTrue(TEXT("First Protect succeeds"), Result.bSucceeded);
	TestFalse(TEXT("First Protect consumes no RNG"), Result.bDrawConsumed);
	TestEqual(TEXT("First Protect starts chain at three"), Result.NextChainCounter, 3);

	Facts.bConsecutiveEligibleUse = true;
	Facts.ChainCounter = 3;
	TestTrue(TEXT("Consecutive Protect success resolves"),
		FBattleVolatileRules::TryResolveProtectAttempt(Facts, Context, Random, Result));
	TestTrue(TEXT("Roll zero succeeds"), Result.bSucceeded);
	TestEqual(TEXT("Successful chain triples"), Result.NextChainCounter, 9);
	TestTrue(TEXT("Consecutive Protect failure resolves"),
		FBattleVolatileRules::TryResolveProtectAttempt(Facts, Context, Random, Result));
	TestFalse(TEXT("Nonzero roll fails"), Result.bSucceeded);
	TestEqual(TEXT("Failed Protect clears its chain"), Result.NextChainCounter, 0);

	Facts.ChainCounter = 729;
	TestTrue(TEXT("Capped Protect resolves"), FBattleVolatileRules::TryResolveProtectAttempt(
		Facts, Context, Random, Result));
	TestTrue(TEXT("Capped chain can still succeed"), Result.bSucceeded);
	TestEqual(TEXT("Protect chain caps at 729"), Result.NextChainCounter, 729);
	TestTrue(TEXT("Protect draws match the exact ranges"), Random.IsExact());
	TestTrue(TEXT("Protect blocks an eligible move"),
		FBattleVolatileRules::ShouldProtectBlockEffect(true, true, false));
	TestFalse(TEXT("Protect does not block an unmarked move"),
		FBattleVolatileRules::ShouldProtectBlockEffect(true, false, false));
	TestFalse(TEXT("Explicit bypass crosses Protect"),
		FBattleVolatileRules::ShouldProtectBlockEffect(true, true, true));

	TUniquePtr<FBattleEngine> Engine;
	TestTrue(TEXT("Protection-breaking engine is created"), TryCreateEngine(7099, 39, Engine));
	if (!Engine.IsValid())
	{
		return false;
	}
	const FBattlerId PlayerId = MakeNumericId<FBattlerId>(PlayerBattlerValue);
	const FBattlerId OpponentId = MakeNumericId<FBattlerId>(OpponentBattlerValue);
	TestTrue(TEXT("Successful Protect chain is seeded"),
		FBattleC07CEngineFixture::ApplyVolatile(
			*Engine,
			OpponentId,
			FBattleVolatileRules::GetProtectId(),
			OpponentId,
			TOptional<int32>(),
			3));
	TestTrue(TEXT("Seeded Protect shield is active before the hit"),
		FBattleC07CEngineFixture::IsVolatileActiveForPhase(
			*Engine,
			OpponentId,
			FBattleVolatileRules::GetProtectId(),
			EBattleTriggerPhase::BeforeHit));
	FBattleEffectExecutionResult Execution;
	TestTrue(TEXT("Protection-breaking move executes"),
		FBattleC07CEngineFixture::ExecuteMove(
			*Engine,
			PlayerId,
			OpponentId,
			MakeDefinitionId<FMoveId>(BreakProtectMoveName),
			Execution));
	TestTrue(TEXT("Protection-breaking move applies"), Execution.bValid);
	TestTrue(TEXT("Protection breaking preserves the Protect condition"),
		FBattleC07CEngineFixture::HasVolatile(
			*Engine, OpponentId, FBattleVolatileRules::GetProtectId()));
	TestEqual(TEXT("Protection breaking preserves the successful chain counter"),
		FBattleC07CEngineFixture::GetVolatileLayers(
			*Engine, OpponentId, FBattleVolatileRules::GetProtectId()), 3);
	TestFalse(TEXT("Protection breaking disables only the current shield"),
		FBattleC07CEngineFixture::IsVolatileActiveForPhase(
			*Engine,
			OpponentId,
			FBattleVolatileRules::GetProtectId(),
			EBattleTriggerPhase::BeforeHit));
	TestTrue(TEXT("Protection breaking leaves Protect end-turn cleanup active"),
		FBattleC07CEngineFixture::IsVolatileActiveForPhase(
			*Engine,
			OpponentId,
			FBattleVolatileRules::GetProtectId(),
			EBattleTriggerPhase::EndTurn));
	TestTrue(TEXT("Protection-breaking engine enters end turn"),
		FBattleC07CEngineFixture::PrepareEndTurn(*Engine));
	const FBattleResolution EndTurn = Engine->ResolveEndTurn();
	TestTrue(TEXT("Protection-breaking end turn is accepted"), EndTurn.WasAccepted());
	TestTrue(TEXT("Successful Protect chain remains after its shield expires"),
		FBattleC07CEngineFixture::HasVolatile(
			*Engine, OpponentId, FBattleVolatileRules::GetProtectId()));
	TestEqual(TEXT("Expired shield retains the chain counter for the next attempt"),
		FBattleC07CEngineFixture::GetVolatileLayers(
			*Engine, OpponentId, FBattleVolatileRules::GetProtectId()), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC07CLeechSeedTrappingTest,
	"PokemonSolarus.Battle.C07C.Rules.LeechSeedAndTrapping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC07CLeechSeedTrappingTest::RunTest(const FString& Parameters)
{
	using namespace BattleVolatileTests;
	(void)Parameters;
	FBattleVolatileApplicationFacts ApplicationFacts;
	ApplicationFacts.RequestedVolatileId = FBattleVolatileRules::GetLeechSeedId();
	ApplicationFacts.PrimaryType = EPokemonType::Grass;
	FBattleVolatileApplicationResult Application;
	TestTrue(TEXT("Leech Seed application resolves"),
		FBattleVolatileRules::TryEvaluateApplication(ApplicationFacts, Application));
	TestEqual(TEXT("Grass is immune to Leech Seed"), Application.Outcome,
		EBattleVolatileApplicationOutcome::TypeImmune);

	FBattleLeechSeedResidualFacts SeedFacts;
	SeedFacts.TargetBaseMaximumHP = 160;
	SeedFacts.TargetCurrentHP = 10;
	SeedFacts.bSourceSlotHasLivingRecipient = true;
	SeedFacts.RecipientMissingHP = 40;
	FBattleLeechSeedResidualResult SeedResult;
	TestTrue(TEXT("Leech Seed residual resolves"),
		FBattleVolatileRules::TryResolveLeechSeedResidual(SeedFacts, SeedResult));
	TestEqual(TEXT("Leech Seed requests one eighth Base Max HP"), SeedResult.RequestedDamage, 20);
	TestEqual(TEXT("Leech Seed uses actual HP removed"), SeedResult.ActualDamage, 10);
	TestEqual(TEXT("Leech Seed heals actual HP removed"), SeedResult.Heal, 10);
	SeedFacts.bSourceSlotHasLivingRecipient = false;
	TestTrue(TEXT("Empty source slot residual resolves"),
		FBattleVolatileRules::TryResolveLeechSeedResidual(SeedFacts, SeedResult));
	TestFalse(TEXT("Empty source slot causes no damage or healing"), SeedResult.bApplies);

	FScriptedVolatileRandom Random({
		{5, 6, 5, FBattleVolatileRules::GetPartialTrapDurationPurpose()}
	});
	FBattleVolatileDurationResult Duration;
	TestTrue(TEXT("Partial-trap duration resolves"),
		FBattleVolatileRules::TryRollPartialTrapDuration(
			MakeRandomContext(FBattleVolatileRules::GetPartialTrapDurationPurpose()),
			Random,
			Duration));
	TestEqual(TEXT("Partial trap accepts counter five"), Duration.Turns, 5);
	FBattlePartialTrapResidualFacts TrapFacts;
	TrapFacts.TargetBaseMaximumHP = 160;
	TrapFacts.TargetCurrentHP = 160;
	TrapFacts.bBindingSourceActiveAndLiving = true;
	FBattlePartialTrapResidualResult TrapResult;
	TestTrue(TEXT("Partial-trap residual resolves"),
		FBattleVolatileRules::TryResolvePartialTrapResidual(TrapFacts, TrapResult));
	TestEqual(TEXT("Partial trap deals one eighth Base Max HP"), TrapResult.ActualDamage, 20);
	TrapFacts.bBindingSourceActiveAndLiving = false;
	TestTrue(TEXT("Missing binding source resolves"),
		FBattleVolatileRules::TryResolvePartialTrapResidual(TrapFacts, TrapResult));
	TestTrue(TEXT("Missing binding source ends partial trap early"), TrapResult.bEndsEarly);

	TestTrue(TEXT("Live partial trap blocks voluntary switch"),
		FBattleVolatileRules::ShouldBlockVoluntarySwitch(
			FBattleVolatileRules::GetPartialTrapId(),
			EPokemonType::Normal,
			EPokemonType::Invalid,
			true,
			true));
	TestTrue(TEXT("Live ordinary trap blocks a non-Ghost"),
		FBattleVolatileRules::ShouldBlockVoluntarySwitch(
			FBattleVolatileRules::GetTrapId(),
			EPokemonType::Normal,
			EPokemonType::Invalid,
			true,
			true));
	TestFalse(TEXT("Ghost remains immune to ordinary trapping"),
		FBattleVolatileRules::ShouldBlockVoluntarySwitch(
			FBattleVolatileRules::GetTrapId(),
			EPokemonType::Ghost,
			EPokemonType::Invalid,
			true,
			true));
	TestFalse(TEXT("A departed source no longer traps"),
		FBattleVolatileRules::ShouldBlockVoluntarySwitch(
			FBattleVolatileRules::GetTrapId(),
			EPokemonType::Normal,
			EPokemonType::Invalid,
			false,
			true));
	TestTrue(TEXT("Partial-trap duration consumed exactly one draw"), Random.IsExact());

	TUniquePtr<FBattleEngine> Engine;
	TestTrue(TEXT("Source-change integration engine is created"), TryCreateEngine(7100, 40, Engine));
	if (!Engine.IsValid())
	{
		return false;
	}
	const FBattlerId PlayerId = MakeNumericId<FBattlerId>(PlayerBattlerValue);
	const FBattlerId ReserveId = MakeNumericId<FBattlerId>(PlayerReserveValue);
	const FBattlerId OpponentId = MakeNumericId<FBattlerId>(OpponentBattlerValue);
	TestTrue(TEXT("Replacement recipient starts injured"),
		FBattleC07CEngineFixture::SetCurrentHP(*Engine, ReserveId, 100));
	TestTrue(TEXT("Leech Seed is seeded from the player's active slot"),
		FBattleC07CEngineFixture::ApplyVolatile(
			*Engine,
			OpponentId,
			FBattleVolatileRules::GetLeechSeedId(),
			PlayerId));
	TestTrue(TEXT("Partial trap is seeded from the original battler"),
		FBattleC07CEngineFixture::ApplyVolatile(
			*Engine,
			OpponentId,
			FBattleVolatileRules::GetPartialTrapId(),
			PlayerId,
			5));
	TestTrue(TEXT("Source slot changes occupant"),
		FBattleC07CEngineFixture::ReplaceActiveBattler(*Engine, PlayerId, ReserveId));
	TestTrue(TEXT("Source-change engine enters end turn"),
		FBattleC07CEngineFixture::PrepareEndTurn(*Engine));
	const FBattleResolution Resolution = Engine->ResolveEndTurn();
	TestTrue(TEXT("Source-change residual pass is accepted"), Resolution.WasAccepted());
	TestEqual(TEXT("Leech Seed damages the target after source replacement"),
		FBattleC07CEngineFixture::GetCurrentHP(*Engine, OpponentId), 140);
	TestEqual(TEXT("Leech Seed heals the current source-slot recipient"),
		FBattleC07CEngineFixture::GetCurrentHP(*Engine, ReserveId), 120);
	TestTrue(TEXT("Leech Seed remains when only its original source leaves"),
		FBattleC07CEngineFixture::HasVolatile(
			*Engine, OpponentId, FBattleVolatileRules::GetLeechSeedId()));
	TestFalse(TEXT("Partial trap ends when its binding source leaves"),
		FBattleC07CEngineFixture::HasVolatile(
			*Engine, OpponentId, FBattleVolatileRules::GetPartialTrapId()));

	TUniquePtr<FBattleEngine> LethalEngine;
	TestTrue(TEXT("Lethal Leech Seed engine is created"),
		TryCreateEngine(7106, 46, LethalEngine));
	if (!LethalEngine.IsValid())
	{
		return false;
	}
	TestTrue(TEXT("Leech Seed source starts injured"),
		FBattleC07CEngineFixture::SetCurrentHP(*LethalEngine, OpponentId, 100));
	TestTrue(TEXT("Leech Seed target starts within lethal residual range"),
		FBattleC07CEngineFixture::SetCurrentHP(*LethalEngine, PlayerId, 10));
	TestTrue(TEXT("Lethal Leech Seed is seeded"),
		FBattleC07CEngineFixture::ApplyVolatile(
			*LethalEngine,
			PlayerId,
			FBattleVolatileRules::GetLeechSeedId(),
			OpponentId));
	TestTrue(TEXT("Lethal Leech Seed engine enters end turn"),
		FBattleC07CEngineFixture::PrepareEndTurn(*LethalEngine));
	const FBattleResolution LethalResolution = LethalEngine->ResolveEndTurn();
	TestTrue(TEXT("Lethal Leech Seed residual pass is accepted"),
		LethalResolution.WasAccepted());
	int32 FaintEventIndex = INDEX_NONE;
	int32 HealingEventIndex = INDEX_NONE;
	const TConstArrayView<FBattleEvent> LethalEvents = LethalResolution.GetEvents();
	for (int32 Index = 0; Index < LethalEvents.Num(); ++Index)
	{
		if (FaintEventIndex == INDEX_NONE
			&& LethalEvents[Index].GetType() == EBattleEventType::Fainted)
		{
			FaintEventIndex = Index;
		}
		if (HealingEventIndex == INDEX_NONE
			&& LethalEvents[Index].GetType() == EBattleEventType::Healing)
		{
			HealingEventIndex = Index;
		}
	}
	TestTrue(TEXT("Lethal residual emits a faint checkpoint"), FaintEventIndex != INDEX_NONE);
	TestTrue(TEXT("Lethal residual still emits linked healing"), HealingEventIndex != INDEX_NONE);
	TestTrue(TEXT("Target faint checkpoint precedes linked Leech Seed healing"),
		FaintEventIndex != INDEX_NONE
			&& HealingEventIndex != INDEX_NONE
			&& FaintEventIndex < HealingEventIndex);
	TestEqual(TEXT("Leech Seed heals only the ten HP actually removed"),
		FBattleC07CEngineFixture::GetCurrentHP(*LethalEngine, OpponentId), 110);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC07CMoveRestrictionsTest,
	"PokemonSolarus.Battle.C07C.Rules.TauntEncoreDisable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC07CMoveRestrictionsTest::RunTest(const FString& Parameters)
{
	using namespace BattleVolatileTests;
	(void)Parameters;
	TestEqual(TEXT("Taunt normally stores three turns"),
		FBattleVolatileRules::GetTauntDuration(false), 3);
	TestEqual(TEXT("Post-action Taunt stores one extra turn"),
		FBattleVolatileRules::GetTauntDuration(true), 4);
	TestEqual(TEXT("Encore stores three turns"),
		FBattleVolatileRules::GetEncoreDuration(), 3);
	TestEqual(TEXT("Disable stores five turns"),
		FBattleVolatileRules::GetDisableDuration(), 5);

	const FMoveId DamageId = MakeDefinitionId<FMoveId>(DamageMoveName);
	FBattleVolatileApplicationFacts ApplicationFacts;
	ApplicationFacts.RequestedVolatileId = FBattleVolatileRules::GetEncoreId();
	ApplicationFacts.LastMoveId = DamageId;
	ApplicationFacts.LastMoveCurrentPP = 1;
	ApplicationFacts.bLastMoveUnencoreable = true;
	FBattleVolatileApplicationResult Application;
	TestTrue(TEXT("Encore eligibility resolves"),
		FBattleVolatileRules::TryEvaluateApplication(ApplicationFacts, Application));
	TestEqual(TEXT("Unencoreable moves reject Encore"), Application.Outcome,
		EBattleVolatileApplicationOutcome::LastMoveUnencoreable);
	ApplicationFacts.bLastMoveUnencoreable = false;
	ApplicationFacts.LastMoveCurrentPP = 0;
	TestTrue(TEXT("Zero-PP Encore eligibility resolves"),
		FBattleVolatileRules::TryEvaluateApplication(ApplicationFacts, Application));
	TestEqual(TEXT("Zero PP rejects Encore"), Application.Outcome,
		EBattleVolatileApplicationOutcome::LastMoveHasNoPP);
	ApplicationFacts.RequestedVolatileId = FBattleVolatileRules::GetDisableId();
	ApplicationFacts.LastMoveCurrentPP = 1;
	ApplicationFacts.bLastMoveIsStruggle = true;
	TestTrue(TEXT("Disable eligibility resolves"),
		FBattleVolatileRules::TryEvaluateApplication(ApplicationFacts, Application));
	TestEqual(TEXT("Struggle cannot be disabled"), Application.Outcome,
		EBattleVolatileApplicationOutcome::LastMoveIsStruggle);

	FBattleVolatileMoveGateFacts GateFacts;
	GateFacts.SelectedMoveId = MakeDefinitionId<FMoveId>(StatusMoveName);
	GateFacts.SelectedMoveCategory = EBattleMoveCategory::Status;
	GateFacts.bTauntActive = true;
	FBattleVolatileMoveGateResult Gate;
	TestTrue(TEXT("Taunt gate resolves"),
		FBattleVolatileRules::TryResolveMoveGate(GateFacts, Gate));
	TestEqual(TEXT("Taunt rejects Status moves"), Gate.Outcome,
		EBattleVolatileMoveGateOutcome::Taunted);

	GateFacts = FBattleVolatileMoveGateFacts();
	GateFacts.SelectedMoveId = MakeDefinitionId<FMoveId>(AlternateMoveName);
	GateFacts.SelectedMoveCategory = EBattleMoveCategory::Physical;
	GateFacts.EncoreMoveId = DamageId;
	GateFacts.bEncoreMoveStillValid = true;
	GateFacts.EncoreMoveCurrentPP = 1;
	TestTrue(TEXT("Encore selection gate resolves"),
		FBattleVolatileRules::TryResolveMoveGate(GateFacts, Gate));
	TestEqual(TEXT("Encore rejects a different move"), Gate.Outcome,
		EBattleVolatileMoveGateOutcome::EncoreLocked);
	GateFacts.EncoreMoveCurrentPP = 0;
	TestTrue(TEXT("Expired Encore gate resolves"),
		FBattleVolatileRules::TryResolveMoveGate(GateFacts, Gate));
	TestTrue(TEXT("Zero PP ends Encore"), Gate.bEndEncore);
	TestEqual(TEXT("Expired Encore no longer blocks selection"), Gate.Outcome,
		EBattleVolatileMoveGateOutcome::Allowed);

	GateFacts = FBattleVolatileMoveGateFacts();
	GateFacts.SelectedMoveId = DamageId;
	GateFacts.SelectedMoveCategory = EBattleMoveCategory::Physical;
	GateFacts.DisabledMoveId = DamageId;
	GateFacts.bDisabledMoveStillValid = true;
	GateFacts.DisabledMoveCurrentPP = 1;
	TestTrue(TEXT("Disable gate resolves"),
		FBattleVolatileRules::TryResolveMoveGate(GateFacts, Gate));
	TestEqual(TEXT("Disable rejects its locked move"), Gate.Outcome,
		EBattleVolatileMoveGateOutcome::Disabled);
	GateFacts.SelectedMoveId = FBattleBuiltInMoveDefinitions::GetStruggleMoveId();
	GateFacts.SelectedMoveCategory = EBattleMoveCategory::Physical;
	GateFacts.bSelectedMoveIsStruggle = true;
	GateFacts.bNoUsableOrdinaryMove = true;
	GateFacts.EncoreMoveId = DamageId;
	GateFacts.bEncoreMoveStillValid = true;
	GateFacts.EncoreMoveCurrentPP = 1;
	TestTrue(TEXT("Encore plus Disable Struggle gate resolves"),
		FBattleVolatileRules::TryResolveMoveGate(GateFacts, Gate));
	TestEqual(TEXT("Struggle remains available when no ordinary move is usable"), Gate.Outcome,
		EBattleVolatileMoveGateOutcome::Allowed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC07CSubstituteTest,
	"PokemonSolarus.Battle.C07C.Integration.Substitute",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC07CSubstituteTest::RunTest(const FString& Parameters)
{
	using namespace BattleVolatileTests;
	(void)Parameters;
	FBattleSubstituteCreationResult Creation;
	TestTrue(TEXT("Substitute creation resolves"),
		FBattleVolatileRules::TryResolveSubstituteCreation(160, 160, Creation));
	TestTrue(TEXT("Healthy user can create Substitute"), Creation.bCanCreate);
	TestEqual(TEXT("Substitute costs floor Max HP over four"), Creation.HPCost, 40);
	TestEqual(TEXT("Substitute starts with the paid HP"), Creation.SubstituteHP, 40);
	TestTrue(TEXT("Exact-cost Substitute attempt resolves"),
		FBattleVolatileRules::TryResolveSubstituteCreation(160, 40, Creation));
	TestFalse(TEXT("Current HP must be strictly greater than the cost"), Creation.bCanCreate);
	TestTrue(TEXT("One-Max-HP Substitute attempt resolves"),
		FBattleVolatileRules::TryResolveSubstituteCreation(1, 1, Creation));
	TestFalse(TEXT("Max HP one cannot create Substitute"), Creation.bCanCreate);

	FBattleSubstituteDamageFacts DamageFacts;
	DamageFacts.SubstituteHP = 40;
	DamageFacts.OwnerCurrentHP = 120;
	DamageFacts.IncomingDamage = 60;
	FBattleSubstituteDamageResult Damage;
	TestTrue(TEXT("Substitute damage routing resolves"),
		FBattleVolatileRules::TryResolveSubstituteDamage(DamageFacts, Damage));
	TestEqual(TEXT("Substitute absorbs only its remaining HP"), Damage.DamageToSubstitute, 40);
	TestEqual(TEXT("Ordinary excess damage never spills"), Damage.DamageToOwner, 0);
	TestEqual(TEXT("Drain and recoil use actual Substitute damage"),
		Damage.ActualDamageForDrainOrRecoil, 40);
	TestTrue(TEXT("Zero Substitute HP reports a break"), Damage.bBrokeSubstitute);
	DamageFacts.bBypassesSubstitute = true;
	DamageFacts.OwnerCurrentHP = 25;
	TestTrue(TEXT("Bypassing damage routing resolves"),
		FBattleVolatileRules::TryResolveSubstituteDamage(DamageFacts, Damage));
	TestEqual(TEXT("Bypass damage reaches the owner's actual HP"), Damage.DamageToOwner, 25);
	TestEqual(TEXT("Bypass leaves Substitute intact"), Damage.RemainingSubstituteHP, 40);
	TestTrue(TEXT("Opposing ordinary effects are blocked"),
		FBattleVolatileRules::ShouldSubstituteBlockEffect(true, true, false));
	TestFalse(TEXT("Self effects are not blocked"),
		FBattleVolatileRules::ShouldSubstituteBlockEffect(true, false, false));
	TestFalse(TEXT("Explicit bypass crosses Substitute"),
		FBattleVolatileRules::ShouldSubstituteBlockEffect(true, true, true));

	TUniquePtr<FBattleEngine> Engine;
	TestTrue(TEXT("Substitute integration engine is created"), TryCreateEngine(7101, 41, Engine));
	if (!Engine.IsValid())
	{
		return false;
	}
	const FBattlerId PlayerId = MakeNumericId<FBattlerId>(PlayerBattlerValue);
	const FBattlerId OpponentId = MakeNumericId<FBattlerId>(OpponentBattlerValue);
	FBattleEffectExecutionResult Execution;
	TestTrue(TEXT("Substitute move executes"), FBattleC07CEngineFixture::ExecuteMove(
		*Engine,
		PlayerId,
		PlayerId,
		MakeDefinitionId<FMoveId>(SubstituteMoveName),
		Execution));
	TestTrue(TEXT("Substitute execution result is valid"), Execution.bValid);
	TestEqual(TEXT("Substitute cost mutates owner HP without fainting"),
		FBattleC07CEngineFixture::GetCurrentHP(*Engine, PlayerId), 120);
	TestTrue(TEXT("Substitute condition is active"), FBattleC07CEngineFixture::HasVolatile(
		*Engine, PlayerId, FBattleVolatileRules::GetSubstituteId()));
	TestEqual(TEXT("Runtime Substitute stores forty HP as layers"),
		FBattleC07CEngineFixture::GetVolatileLayers(
			*Engine, PlayerId, FBattleVolatileRules::GetSubstituteId()), 40);
	TestTrue(TEXT("Substitute cost emits an HP-change event"),
		Execution.Events.ContainsByPredicate(
			[](const FBattleEffectExecutionEvent& Event)
			{
				return Event.Type == EBattleEventType::HPChanged;
			}));

	TestTrue(TEXT("Heavy opposing damage executes"), FBattleC07CEngineFixture::ExecuteMove(
		*Engine,
		OpponentId,
		PlayerId,
		MakeDefinitionId<FMoveId>(DamageMoveName),
		Execution));
	TestEqual(TEXT("Absorbed damage leaves owner HP unchanged"),
		FBattleC07CEngineFixture::GetCurrentHP(*Engine, PlayerId), 120);
	TestFalse(TEXT("Depleted Substitute is removed"), FBattleC07CEngineFixture::HasVolatile(
		*Engine, PlayerId, FBattleVolatileRules::GetSubstituteId()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC07CChargeRechargeFlyTest,
	"PokemonSolarus.Battle.C07C.Integration.ChargeRechargeAndFly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC07CChargeRechargeFlyTest::RunTest(const FString& Parameters)
{
	using namespace BattleVolatileTests;
	(void)Parameters;
	const FMoveId ChargeId = MakeDefinitionId<FMoveId>(ChargeMoveName);
	const FBattlerId PlayerId = MakeNumericId<FBattlerId>(PlayerBattlerValue);
	const FBattlerId OpponentId = MakeNumericId<FBattlerId>(OpponentBattlerValue);
	FBattleChargeActionFacts ChargeFacts;
	ChargeFacts.SelectedMoveId = ChargeId;
	ChargeFacts.SelectedTargetBattlerId = OpponentId;
	FBattleChargeActionResult Charge;
	TestTrue(TEXT("First charge turn resolves"),
		FBattleVolatileRules::TryResolveChargeAction(ChargeFacts, Charge));
	TestEqual(TEXT("First turn begins charging"), Charge.Outcome,
		EBattleChargeActionOutcome::BeginCharge);
	TestTrue(TEXT("First turn pays PP"), Charge.bPayPP);
	TestTrue(TEXT("First turn adds charge state"), Charge.bAddCharge);
	ChargeFacts.bChargeActive = true;
	ChargeFacts.LockedMoveId = ChargeId;
	ChargeFacts.LockedTargetBattlerId = OpponentId;
	TestTrue(TEXT("Second charge turn resolves"),
		FBattleVolatileRules::TryResolveChargeAction(ChargeFacts, Charge));
	TestEqual(TEXT("Second turn executes the locked move"), Charge.Outcome,
		EBattleChargeActionOutcome::ExecuteChargedMove);
	TestFalse(TEXT("Second turn pays no second PP"), Charge.bPayPP);
	TestTrue(TEXT("Second turn removes charge"), Charge.bRemoveCharge);
	ChargeFacts.bExplicitlyCancelled = true;
	TestTrue(TEXT("Explicit charge cancellation resolves"),
		FBattleVolatileRules::TryResolveChargeAction(ChargeFacts, Charge));
	TestEqual(TEXT("Cancellation clears charge"), Charge.Outcome,
		EBattleChargeActionOutcome::CancelCharge);

	FBattleDefinitionCatalogInput MalformedInput = MakeCatalogInput();
	FBattleMoveDefinition* MalformedCharge = MalformedInput.Moves.FindByPredicate(
		[ChargeId](const FBattleMoveDefinition& Move)
		{
			return Move.Id == ChargeId;
		});
	TestNotNull(TEXT("Malformed catalog fixture finds the charge move"), MalformedCharge);
	if (MalformedCharge != nullptr)
	{
		Swap(MalformedCharge->Effects[0], MalformedCharge->Effects[2]);
		Swap(MalformedCharge->Effects[1], MalformedCharge->Effects[2]);
		for (int32 Index = 0; Index < MalformedCharge->Effects.Num(); ++Index)
		{
			MalformedCharge->Effects[Index].Order = Index;
		}
	}
	FBattleDefinitionCatalog RejectedCatalog;
	TArray<FBattleCatalogDiagnostic> MalformedDiagnostics;
	TestFalse(TEXT("Catalog rejects Damage before Charge"),
		FBattleDefinitionCatalog::TryCreate(
			MalformedInput,
			RejectedCatalog,
			MalformedDiagnostics));
	TestTrue(TEXT("Malformed descriptor order reports the charge contract"),
		MalformedDiagnostics.ContainsByPredicate(
			[ChargeId](const FBattleCatalogDiagnostic& Diagnostic)
			{
				return Diagnostic.Code == EBattleCatalogDiagnosticCode::IncompatibleEffect
					&& Diagnostic.DefinitionId == ChargeId.GetDefinitionId()
					&& Diagnostic.Field == FName(TEXT("Effects.Charge"));
			}));

	FBattleFlyReachabilityFacts FlyFacts;
	FlyFacts.bTargetFlySemiInvulnerable = true;
	FBattleFlyReachabilityResult Fly;
	TestTrue(TEXT("Ordinary Fly reachability resolves"),
		FBattleVolatileRules::TryResolveFlyReachability(FlyFacts, Fly));
	TestFalse(TEXT("Ordinary move cannot reach Fly-style target"), Fly.bReachable);
	FlyFacts.bMoveReachesFlyTarget = true;
	FlyFacts.bMoveDoublesPowerAgainstFlyTarget = true;
	TestTrue(TEXT("Official Fly exception resolves"),
		FBattleVolatileRules::TryResolveFlyReachability(FlyFacts, Fly));
	TestTrue(TEXT("Flagged move reaches Fly-style target"), Fly.bReachable);
	TestEqual(TEXT("Gust/Twister-style flag doubles power"), Fly.PowerMultiplierNumerator, 2);
	FBattleVolatileActionResult RechargeGate;
	TestTrue(TEXT("Recharge gate resolves"), FBattleVolatileRules::TryResolveSimpleBeforeAction(
		FBattleVolatileRules::GetRechargeId(), RechargeGate));
	TestEqual(TEXT("Recharge denies the next action"), RechargeGate.Outcome,
		EBattleVolatileActionOutcome::Denied);
	TestTrue(TEXT("Recharge removes itself after denial"), RechargeGate.bRemoveVolatile);

	TUniquePtr<FBattleEngine> GateEngine;
	TestTrue(TEXT("Early charge-gate engine is created"), TryCreateEngine(7107, 47, GateEngine));
	if (!GateEngine.IsValid())
	{
		return false;
	}
	TestTrue(TEXT("Charge target starts protected"),
		FBattleC07CEngineFixture::ApplyVolatile(
			*GateEngine,
			OpponentId,
			FBattleVolatileRules::GetProtectId(),
			OpponentId,
			TOptional<int32>(),
			3));
	TestTrue(TEXT("Charge target starts airborne"),
		FBattleC07CEngineFixture::ApplyVolatile(
			*GateEngine,
			OpponentId,
			FBattleVolatileRules::GetFlySemiInvulnerableId(),
			OpponentId));
	FBattleEffectExecutionResult GateExecution;
	TestTrue(TEXT("First charge turn executes before hit gates"),
		FBattleC07CEngineFixture::ExecuteMove(
			*GateEngine,
			PlayerId,
			OpponentId,
			ChargeId,
			GateExecution));
	TestTrue(TEXT("Protected airborne target still stores the charge"),
		GateExecution.bMoveDeferred
			&& FBattleC07CEngineFixture::HasVolatile(
				*GateEngine, PlayerId, FBattleVolatileRules::GetChargingId()));
	TestFalse(TEXT("First charge turn never reaches target gates"),
		GateExecution.Events.ContainsByPredicate(
			[](const FBattleEffectExecutionEvent& Event)
			{
				return Event.Type == EBattleEventType::Protected
					|| Event.Type == EBattleEventType::Unreachable
					|| Event.Type == EBattleEventType::AccuracyChecked;
			}));

	TUniquePtr<FBattleEngine> Engine;
	TestTrue(TEXT("Charge integration engine is created"), TryCreateEngine(7102, 42, Engine));
	if (!Engine.IsValid())
	{
		return false;
	}
	FBattleEffectExecutionResult Execution;
	TestTrue(TEXT("First Fly turn executes"), FBattleC07CEngineFixture::ExecuteMove(
		*Engine, PlayerId, OpponentId, ChargeId, Execution));
	TestTrue(TEXT("First Fly turn defers remaining effects"), Execution.bMoveDeferred);
	TestTrue(TEXT("Charging condition is stored"), FBattleC07CEngineFixture::HasVolatile(
		*Engine, PlayerId, FBattleVolatileRules::GetChargingId()));
	TestTrue(TEXT("Fly semi-invulnerability is stored"), FBattleC07CEngineFixture::HasVolatile(
		*Engine, PlayerId, FBattleVolatileRules::GetFlySemiInvulnerableId()));
	const int32 HPBefore = FBattleC07CEngineFixture::GetCurrentHP(*Engine, PlayerId);
	TestTrue(TEXT("Ordinary attack against Fly target resolves"),
		FBattleC07CEngineFixture::ExecuteMove(
			*Engine,
			OpponentId,
			PlayerId,
			MakeDefinitionId<FMoveId>(AlternateMoveName),
			Execution));
	TestEqual(TEXT("Unreachable attack leaves HP unchanged"),
		FBattleC07CEngineFixture::GetCurrentHP(*Engine, PlayerId), HPBefore);
	TestTrue(TEXT("Unreachable attack emits the typed event"),
		Execution.Events.ContainsByPredicate(
			[](const FBattleEffectExecutionEvent& Event)
			{
				return Event.Type == EBattleEventType::Unreachable;
			}));
	TestTrue(TEXT("Flagged Fly-reach move executes"), FBattleC07CEngineFixture::ExecuteMove(
		*Engine,
		OpponentId,
		PlayerId,
		MakeDefinitionId<FMoveId>(ReachMoveName),
		Execution));
	TestTrue(TEXT("Flagged Fly-reach move damages the target"),
		FBattleC07CEngineFixture::GetCurrentHP(*Engine, PlayerId) < HPBefore);
	TestTrue(TEXT("Recharge move executes"), FBattleC07CEngineFixture::ExecuteMove(
		*Engine,
		PlayerId,
		OpponentId,
		MakeDefinitionId<FMoveId>(RechargeMoveName),
		Execution));
	TestTrue(TEXT("Recharge condition is stored after the move"),
		FBattleC07CEngineFixture::HasVolatile(
			*Engine, PlayerId, FBattleVolatileRules::GetRechargeId()));

	const FActiveSlotId PlayerLeftSlot = MakeActiveSlotId(
		EBattleSide::Player, EBattlePosition::Left);
	const FActiveSlotId PlayerRightSlot = MakeActiveSlotId(
		EBattleSide::Player, EBattlePosition::Right);
	const FActiveSlotId OpponentLeftSlot = MakeActiveSlotId(
		EBattleSide::Opponent, EBattlePosition::Left);
	const FActiveSlotId OpponentRightSlot = MakeActiveSlotId(
		EBattleSide::Opponent, EBattlePosition::Right);
	const FBattlerId PlayerRightId = MakeNumericId<FBattlerId>(PlayerReserveValue);
	const FBattlerId OpponentRightId = MakeNumericId<FBattlerId>(OpponentRightBattlerValue);

	TUniquePtr<FBattleEngine> DeniedEngine;
	TestTrue(TEXT("Denied charge-release engine is created"),
		TryCreateDoubleChargeEngine(7108, 48, DeniedEngine));
	if (!DeniedEngine.IsValid())
	{
		return false;
	}
	TArray<FBattleTriggerSubject> DeniedTargets;
	TestTrue(TEXT("Denied release target identity is frozen"),
		TryMakeChargeTargets(OpponentLeftSlot, OpponentId, DeniedTargets));
	TestTrue(TEXT("Denied release starts after its first-turn PP cost"),
		FBattleC07CEngineFixture::SetLastMove(*DeniedEngine, PlayerId, ChargeId, 19));
	TestTrue(TEXT("Denied release charge state is seeded"),
		FBattleC07CEngineFixture::ApplyVolatile(
			*DeniedEngine,
			PlayerId,
			FBattleVolatileRules::GetChargingId(),
			PlayerId,
			TOptional<int32>(),
			1,
			ChargeId.GetDefinitionId(),
			DeniedTargets));
	TestTrue(TEXT("Denied release Fly state is seeded"),
		FBattleC07CEngineFixture::ApplyVolatile(
			*DeniedEngine,
			PlayerId,
			FBattleVolatileRules::GetFlySemiInvulnerableId(),
			PlayerId));
	TestTrue(TEXT("Flinch is seeded for the charged release"),
		FBattleC07CEngineFixture::ApplyVolatile(
			*DeniedEngine,
			PlayerId,
			FBattleVolatileRules::GetFlinchId(),
			OpponentId));
	TestTrue(TEXT("Charged release is prepared at the action gate"),
		FBattleC07CEngineFixture::PrepareStartedFight(
			*DeniedEngine,
			PlayerId,
			ChargeId,
			OpponentLeftSlot,
			OpponentId));
	const int32 DeniedPPBefore = FBattleC07CEngineFixture::GetMovePP(
		*DeniedEngine, PlayerId, ChargeId);
	const FBattleResolution DeniedRelease =
		DeniedEngine->CommitCurrentMoveAfterPreMoveGates();
	TestTrue(TEXT("Flinched charged release is accepted as a denied action"),
		DeniedRelease.WasAccepted());
	TestFalse(TEXT("Denied release clears Charging"),
		FBattleC07CEngineFixture::HasVolatile(
			*DeniedEngine, PlayerId, FBattleVolatileRules::GetChargingId()));
	TestFalse(TEXT("Denied release clears Fly semi-invulnerability"),
		FBattleC07CEngineFixture::HasVolatile(
			*DeniedEngine, PlayerId, FBattleVolatileRules::GetFlySemiInvulnerableId()));
	TestEqual(TEXT("Denied release pays no second PP cost"),
		FBattleC07CEngineFixture::GetMovePP(*DeniedEngine, PlayerId, ChargeId),
		DeniedPPBefore);

	TUniquePtr<FBattleEngine> RechargeSwitchEngine;
	TestTrue(TEXT("Non-Fight Recharge engine is created"),
		TryCreateEngine(7110, 50, RechargeSwitchEngine));
	if (!RechargeSwitchEngine.IsValid())
	{
		return false;
	}
	TestTrue(TEXT("Recharge is seeded before a voluntary switch"),
		FBattleC07CEngineFixture::ApplyVolatile(
			*RechargeSwitchEngine,
			PlayerId,
			FBattleVolatileRules::GetRechargeId(),
			PlayerId));
	TestTrue(TEXT("Voluntary switch is locked as the recharging battler's next action"),
		FBattleC07CEngineFixture::PrepareLockedSwitch(
			*RechargeSwitchEngine,
			PlayerId,
			MakePartySlotId(1)));
	const int32 RechargePPBefore = FBattleC07CEngineFixture::GetTotalMovePP(
		*RechargeSwitchEngine, PlayerId);
	const FBattlerId RechargeActiveBefore = FBattleC07CEngineFixture::GetActiveBattlerId(
		*RechargeSwitchEngine, PlayerLeftSlot);
	const int32 RechargeTraceBefore = RechargeSwitchEngine->ExportRandomTrace().Num();
	const FBattleResolution RechargeDeniedSwitch =
		RechargeSwitchEngine->BeginNextLockedAction();
	TestTrue(TEXT("Recharge accepts and consumes the denied Switch action"),
		RechargeDeniedSwitch.WasAccepted());
	bool bRechargeCanceledSwitch = false;
	bool bRechargeCompletedAction = false;
	bool bRechargeSwitched = false;
	bool bRechargeRandomCheck = false;
	for (const FBattleEvent& Event : RechargeDeniedSwitch.GetEvents())
	{
		bRechargeCanceledSwitch |= Event.GetType() == EBattleEventType::ActionCanceled;
		bRechargeCompletedAction |= Event.GetType() == EBattleEventType::ActionCompleted;
		bRechargeSwitched |= Event.GetType() == EBattleEventType::Switched;
		bRechargeRandomCheck |= Event.GetType() == EBattleEventType::RandomCheck;
	}
	TestTrue(TEXT("Recharge cancels the non-Fight action at the generic action gate"),
		bRechargeCanceledSwitch);
	TestTrue(TEXT("Recharge consumes the denied action slot"), bRechargeCompletedAction);
	TestFalse(TEXT("Denied Recharge action performs no switch mutation"), bRechargeSwitched);
	TestEqual(TEXT("Denied Recharge action leaves the active slot unchanged"),
		FBattleC07CEngineFixture::GetActiveBattlerId(
			*RechargeSwitchEngine, PlayerLeftSlot),
		RechargeActiveBefore);
	TestEqual(TEXT("Recharge denial occurs before any PP can be spent"),
		FBattleC07CEngineFixture::GetTotalMovePP(*RechargeSwitchEngine, PlayerId),
		RechargePPBefore);
	TestFalse(TEXT("Recharge removes itself after denying Switch"),
		FBattleC07CEngineFixture::HasVolatile(
			*RechargeSwitchEngine, PlayerId, FBattleVolatileRules::GetRechargeId()));
	TestFalse(TEXT("Recharge trigger registration is cleaned after denial"),
		FBattleC07CEngineFixture::IsVolatileActiveForPhase(
			*RechargeSwitchEngine,
			PlayerId,
			FBattleVolatileRules::GetRechargeId(),
			EBattleTriggerPhase::BeforeAction));
	TestFalse(TEXT("Denied Recharge Switch leaves no current action"),
		RechargeSwitchEngine->GetCurrentLockedAction().IsSet());
	TestFalse(TEXT("Recharge denial emits no random-check event"), bRechargeRandomCheck);
	TestEqual(TEXT("Recharge denial consumes no RNG"),
		RechargeSwitchEngine->ExportRandomTrace().Num(), RechargeTraceBefore);

	TUniquePtr<FBattleEngine> FallbackEngine;
	TestTrue(TEXT("Charged target-fallback engine is created"),
		TryCreateDoubleChargeEngine(7109, 49, FallbackEngine));
	if (!FallbackEngine.IsValid())
	{
		return false;
	}
	TestTrue(TEXT("Original charged target is fainted and removed"),
		FBattleC07CEngineFixture::MarkFaintedAndRemoveActive(
			*FallbackEngine, OpponentId));
	TArray<FBattleTriggerSubject> PlayerLeftTargets;
	TArray<FBattleTriggerSubject> PlayerRightTargets;
	TArray<FBattleTriggerSubject> OpponentRightTargets;
	TestTrue(TEXT("Player-left charge retains the empty slot and original target"),
		TryMakeChargeTargets(OpponentLeftSlot, OpponentId, PlayerLeftTargets));
	TestTrue(TEXT("Player-right charge target is frozen"),
		TryMakeChargeTargets(OpponentRightSlot, OpponentRightId, PlayerRightTargets));
	TestTrue(TEXT("Opponent-right charge target is frozen"),
		TryMakeChargeTargets(PlayerRightSlot, PlayerRightId, OpponentRightTargets));
	TestTrue(TEXT("Fallback release starts after its first-turn PP cost"),
		FBattleC07CEngineFixture::SetLastMove(*FallbackEngine, PlayerId, ChargeId, 19));
	TestTrue(TEXT("Player-left forced charge is seeded"),
		FBattleC07CEngineFixture::ApplyVolatile(
			*FallbackEngine,
			PlayerId,
			FBattleVolatileRules::GetChargingId(),
			PlayerId,
			TOptional<int32>(),
			1,
			ChargeId.GetDefinitionId(),
			PlayerLeftTargets));
	TestTrue(TEXT("Player-right forced charge is seeded"),
		FBattleC07CEngineFixture::ApplyVolatile(
			*FallbackEngine,
			PlayerRightId,
			FBattleVolatileRules::GetChargingId(),
			PlayerRightId,
			TOptional<int32>(),
			1,
			ChargeId.GetDefinitionId(),
			PlayerRightTargets));
	TestTrue(TEXT("Opponent-right forced charge is seeded"),
		FBattleC07CEngineFixture::ApplyVolatile(
			*FallbackEngine,
			OpponentRightId,
			FBattleVolatileRules::GetChargingId(),
			OpponentRightId,
			TOptional<int32>(),
			1,
			ChargeId.GetDefinitionId(),
			OpponentRightTargets));
	TestTrue(TEXT("Charged target-fallback engine enters end turn"),
		FBattleC07CEngineFixture::PrepareEndTurn(*FallbackEngine));
	const FBattleResolution FallbackEndTurn = FallbackEngine->ResolveEndTurn();
	TestTrue(TEXT("Forced charged releases lock with an empty stored target slot"),
		FallbackEndTurn.WasAccepted());
	const TArray<FBattleLockedAction> ForcedActions = FallbackEngine->GetLockedActions();
	TestEqual(TEXT("All three living charged battlers receive forced actions"),
		ForcedActions.Num(), 3);
	TestTrue(TEXT("Fastest forced action is the player-left release"),
		!ForcedActions.IsEmpty()
			&& ForcedActions[0].Decision.GetActingBattlerId() == PlayerId);
	const int32 FallbackPPBefore = FBattleC07CEngineFixture::GetMovePP(
		*FallbackEngine, PlayerId, ChargeId);
	TestTrue(TEXT("Forced player-left release starts"),
		FallbackEngine->BeginNextLockedAction().WasAccepted());
	TestTrue(TEXT("Forced player-left release commits"),
		FallbackEngine->CommitCurrentMoveAfterPreMoveGates().WasAccepted());
	TestEqual(TEXT("Forced release pays no second PP cost"),
		FBattleC07CEngineFixture::GetMovePP(*FallbackEngine, PlayerId, ChargeId),
		FallbackPPBefore);
	TestTrue(TEXT("Forced player-left release resolves targets"),
		FallbackEngine->ResolveCurrentMoveTargets().WasAccepted());
	const TOptional<FBattleLockedAction> CurrentFallbackAction =
		FallbackEngine->GetCurrentLockedAction();
	TestTrue(TEXT("Resolved forced release remains the current action"),
		CurrentFallbackAction.IsSet());
	if (CurrentFallbackAction.IsSet())
	{
		const TOptional<FBattleTargetResolutionResult>& TargetResolution =
			CurrentFallbackAction.GetValue().TargetResolution;
		TestTrue(TEXT("Fainted charged target uses the explicit fallback rule"),
			TargetResolution.IsSet()
				&& TargetResolution.GetValue().bUsedFaintedTargetFallback);
		TestTrue(TEXT("Charged release falls back to the other living opponent"),
			TargetResolution.IsSet()
				&& TargetResolution.GetValue().Targets.Num() == 1
				&& TargetResolution.GetValue().Targets[0].GetKind()
					== EBattleResolvedTargetKind::Battler
				&& TargetResolution.GetValue().Targets[0].GetBattler().BattlerId
					== OpponentRightId);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC07CEngineDeterminismCleanupTest,
	"PokemonSolarus.Battle.C07C.Integration.DeterminismEventsLegalityAndCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC07CEngineDeterminismCleanupTest::RunTest(const FString& Parameters)
{
	using namespace BattleVolatileTests;
	(void)Parameters;
	const FBattlerId PlayerId = MakeNumericId<FBattlerId>(PlayerBattlerValue);
	const FBattlerId OpponentId = MakeNumericId<FBattlerId>(OpponentBattlerValue);
	const FMoveId DamageId = MakeDefinitionId<FMoveId>(DamageMoveName);
	const FMoveId AlternateId = MakeDefinitionId<FMoveId>(AlternateMoveName);
	const FMoveId StatusId = MakeDefinitionId<FMoveId>(StatusMoveName);

	TUniquePtr<FBattleEngine> First;
	TUniquePtr<FBattleEngine> Second;
	TestTrue(TEXT("First deterministic engine is created"), TryCreateEngine(7103, 43, First));
	TestTrue(TEXT("Second deterministic engine is created"), TryCreateEngine(7103, 43, Second));
	if (!First.IsValid() || !Second.IsValid())
	{
		return false;
	}
	FBattleEffectExecutionResult FirstExecution;
	FBattleEffectExecutionResult SecondExecution;
	TestTrue(TEXT("First Confusion move executes"), FBattleC07CEngineFixture::ExecuteMove(
		*First, PlayerId, OpponentId, StatusId, FirstExecution));
	TestTrue(TEXT("Second Confusion move executes"), FBattleC07CEngineFixture::ExecuteMove(
		*Second, PlayerId, OpponentId, StatusId, SecondExecution));
	TestTrue(TEXT("Confusion is applied in the first engine"),
		FBattleC07CEngineFixture::HasVolatile(
			*First, OpponentId, FBattleVolatileRules::GetConfusionId()));
	TestEqual(TEXT("Equal seeds produce equal event counts"),
		FirstExecution.Events.Num(), SecondExecution.Events.Num());
	for (int32 Index = 0;
		Index < FMath::Min(FirstExecution.Events.Num(), SecondExecution.Events.Num());
		++Index)
	{
		TestEqual(TEXT("Equal seeds preserve event order"),
			FirstExecution.Events[Index].Type,
			SecondExecution.Events[Index].Type);
	}
	const TArray<FBattleRandomDraw> FirstTrace = First->ExportRandomTrace();
	const TArray<FBattleRandomDraw> SecondTrace = Second->ExportRandomTrace();
	TestEqual(TEXT("Equal seeds produce equal trace sizes"), FirstTrace.Num(), SecondTrace.Num());
	for (int32 Index = 0; Index < FMath::Min(FirstTrace.Num(), SecondTrace.Num()); ++Index)
	{
		TestTrue(TEXT("Equal seeds produce identical traced draws"),
			FirstTrace[Index] == SecondTrace[Index]);
	}
	TestTrue(TEXT("Confusion application emits a random-check event"),
		FirstExecution.Events.ContainsByPredicate(
			[](const FBattleEffectExecutionEvent& Event)
			{
				return Event.Type == EBattleEventType::RandomCheck;
			}));
	TestTrue(TEXT("Confusion application emits a status-change event"),
		FirstExecution.Events.ContainsByPredicate(
			[](const FBattleEffectExecutionEvent& Event)
			{
				return Event.Type == EBattleEventType::StatusChanged;
			}));

	TestTrue(TEXT("Flinch is seeded for cleanup"), FBattleC07CEngineFixture::ApplyVolatile(
		*First,
		PlayerId,
		FBattleVolatileRules::GetFlinchId(),
		OpponentId));
	TestTrue(TEXT("Protect is seeded for end-turn suppression"),
		FBattleC07CEngineFixture::ApplyVolatile(
			*First,
			PlayerId,
			FBattleVolatileRules::GetProtectId(),
			PlayerId,
			TOptional<int32>(),
			3));
	TestTrue(TEXT("Engine enters end turn"), FBattleC07CEngineFixture::PrepareEndTurn(*First));
	const FBattleResolution EndTurn = First->ResolveEndTurn();
	TestTrue(TEXT("End-turn volatile pass is accepted"), EndTurn.WasAccepted());
	TestFalse(TEXT("Flinch is removed at end turn"), FBattleC07CEngineFixture::HasVolatile(
		*First, PlayerId, FBattleVolatileRules::GetFlinchId()));
	TestTrue(TEXT("Protect chain remains for a possible consecutive use"),
		FBattleC07CEngineFixture::HasVolatile(
			*First, PlayerId, FBattleVolatileRules::GetProtectId()));
	TestTrue(TEXT("Expired active Protect is suppressed after the turn"),
		FBattleC07CEngineFixture::IsVolatileSuppressed(
			*First, PlayerId, FBattleVolatileRules::GetProtectId()));

	TUniquePtr<FBattleEngine> LegalityEngine;
	TestTrue(TEXT("Legality engine is created"), TryCreateEngine(7104, 44, LegalityEngine));
	if (!LegalityEngine.IsValid())
	{
		return false;
	}
	TestTrue(TEXT("Taunt is seeded"), FBattleC07CEngineFixture::ApplyVolatile(
		*LegalityEngine,
		PlayerId,
		FBattleVolatileRules::GetTauntId(),
		OpponentId,
		3));
	TestTrue(TEXT("Ordinary trap is seeded"), FBattleC07CEngineFixture::ApplyVolatile(
		*LegalityEngine,
		PlayerId,
		FBattleVolatileRules::GetTrapId(),
		OpponentId));
	FBattleRejection Rejection;
	TestTrue(TEXT("Taunt/trap decision sequence begins"),
		LegalityEngine->TryBeginActionDecisionSequence(Rejection));
	TestTrue(TEXT("Player decision is pending"), LegalityEngine->GetPendingDecision().IsSet());
	if (LegalityEngine->GetPendingDecision().IsSet())
	{
		const FBattleDecisionRequest Request = LegalityEngine->GetPendingDecision().GetValue();
		TestTrue(TEXT("Taunted Status move has typed unavailable reason"),
			HasUnavailableReason(Request, StatusId, EBattleOptionUnavailableReason::Taunted));
		TestTrue(TEXT("Physical move remains legal under Taunt"),
			Request.GetLegalMoveIds().Contains(DamageId));
		TestTrue(TEXT("Trap marks the reserve switch unavailable"),
			Request.GetUnavailableOptions().ContainsByPredicate(
				[](const FBattleUnavailableDecisionOption& Option)
				{
					return Option.Kind == EBattleDecisionOptionKind::SwitchPartySlot
						&& Option.Reason == EBattleOptionUnavailableReason::Trapped;
				}));
	}

	TUniquePtr<FBattleEngine> LockEngine;
	TestTrue(TEXT("Encore/Disable engine is created"), TryCreateEngine(7105, 45, LockEngine));
	if (!LockEngine.IsValid())
	{
		return false;
	}
	TestTrue(TEXT("Last move and PP are seeded"), FBattleC07CEngineFixture::SetLastMove(
		*LockEngine, PlayerId, DamageId, 20));
	TestTrue(TEXT("Encore is seeded with the locked move"),
		FBattleC07CEngineFixture::ApplyVolatile(
			*LockEngine,
			PlayerId,
			FBattleVolatileRules::GetEncoreId(),
			OpponentId,
			FBattleVolatileRules::GetEncoreDuration(),
			1,
			DamageId.GetDefinitionId()));
	TestTrue(TEXT("Disable is seeded on the same move"),
		FBattleC07CEngineFixture::ApplyVolatile(
			*LockEngine,
			PlayerId,
			FBattleVolatileRules::GetDisableId(),
			OpponentId,
			FBattleVolatileRules::GetDisableDuration(),
			1,
			DamageId.GetDefinitionId()));
	TestTrue(TEXT("Encore/Disable decision sequence begins"),
		LockEngine->TryBeginActionDecisionSequence(Rejection));
	TestTrue(TEXT("Encore/Disable decision is pending"), LockEngine->GetPendingDecision().IsSet());
	if (LockEngine->GetPendingDecision().IsSet())
	{
		const FBattleDecisionRequest Request = LockEngine->GetPendingDecision().GetValue();
		TestTrue(TEXT("Encore rejects a different ordinary move"),
			HasUnavailableReason(Request, AlternateId, EBattleOptionUnavailableReason::Encored));
		TestTrue(TEXT("Encore rejects the Status move"),
			HasUnavailableReason(Request, StatusId, EBattleOptionUnavailableReason::Encored));
		TestTrue(TEXT("Encore rejects the Substitute move"),
			HasUnavailableReason(
				Request,
				MakeDefinitionId<FMoveId>(SubstituteMoveName),
				EBattleOptionUnavailableReason::Encored));
		TestTrue(TEXT("Disable rejects the Encored move"),
			HasUnavailableReason(Request, DamageId, EBattleOptionUnavailableReason::Disabled));
		TestEqual(TEXT("Only the forced fallback remains legal"), Request.GetLegalMoveIds().Num(), 1);
		TestTrue(TEXT("Encore/Disable conflict exposes Struggle"),
			Request.GetLegalMoveIds().Contains(
				FBattleBuiltInMoveDefinitions::GetStruggleMoveId()));
	}
	return true;
}

#endif
