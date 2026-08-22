#if WITH_DEV_AUTOMATION_TESTS

#include "Battle/BattleActionQueue.h"
#include "Battle/BattleDefinitionCatalog.h"
#include "Battle/BattleEffectExecutor.h"
#include "Battle/BattleEngine.h"
#include "Battle/BattleFieldSideConditions.h"
#include "Battle/BattleMajorStatus.h"
#include "Battle/BattleState.h"
#include "BattleTestFactories.h"
#include "Math/NumericLimits.h"
#include "Misc/AutomationTest.h"

class FBattleC07DEngineFixture
{
public:
	static bool SeedCondition(
		FBattleEngine& Engine,
		const FConditionId ConditionId,
		const EBattleSide Side,
		const FBattlerId SourceBattlerId,
		const TOptional<int32>& RemainingTurns,
		const int32 Layers = 1)
	{
		if (!Engine.State.IsValid()
			|| !FBattleFieldSideConditionRules::IsCanonical(ConditionId)
			|| Layers <= 0)
		{
			return false;
		}

		FBattleTriggerSubject Owner;
		if (FBattleFieldSideConditionRules::IsFieldOwned(ConditionId))
		{
			Owner = FBattleTriggerSubject::CreateField();
		}
		else if (!FBattleTriggerSubject::TryCreateSide(Side, Owner))
		{
			return false;
		}
		FBattleTriggerSubject Source;
		if (!FBattleTriggerSubject::TryCreateBattler(SourceBattlerId, Source))
		{
			return false;
		}

		FBattleFieldSideTriggerRegistrationFacts TriggerFacts;
		TriggerFacts.ConditionId = ConditionId;
		TriggerFacts.PayloadId = ConditionId.GetDefinitionId();
		TriggerFacts.Owner = Owner;
		TriggerFacts.Source = Source;
		TriggerFacts.Targets.Add(Owner);
		TriggerFacts.RemainingTurns = RemainingTurns;
		TriggerFacts.Layers = Layers;
		EBattleTriggerError TriggerError = EBattleTriggerError::None;
		if (!FBattleFieldSideConditionRules::TryRegisterTriggers(
			Engine.State->TriggerFramework,
			TriggerFacts,
			TriggerError))
		{
			return false;
		}

		FBattleConditionState Condition;
		Condition.ConditionId = ConditionId;
		Condition.RemainingTurns = RemainingTurns;
		Condition.LayerCount = Layers;
		Condition.CreationOrdinal = Engine.State->NextConditionCreationOrdinal++;
		Condition.SourceBattlerId = SourceBattlerId;

		switch (FBattleFieldSideConditionRules::GetConditionFamily(ConditionId))
		{
		case EBattleConditionKind::Weather:
			if (Engine.State->Field.Weather.IsSet()) return false;
			Engine.State->Field.Weather = Condition;
			break;
		case EBattleConditionKind::Terrain:
			if (Engine.State->Field.Terrain.IsSet()) return false;
			Engine.State->Field.Terrain = Condition;
			break;
		case EBattleConditionKind::Room:
			Engine.State->Field.Rooms.Add(Condition);
			break;
		case EBattleConditionKind::Hazard:
		{
			FBattleSideState* SideState = Engine.State->Sides.FindByPredicate(
				[Side](const FBattleSideState& Candidate) { return Candidate.Side == Side; });
			if (SideState == nullptr) return false;
			SideState->Hazards.Add(Condition);
			break;
		}
		case EBattleConditionKind::Screen:
		case EBattleConditionKind::SideCondition:
		{
			FBattleSideState* SideState = Engine.State->Sides.FindByPredicate(
				[Side](const FBattleSideState& Candidate) { return Candidate.Side == Side; });
			if (SideState == nullptr) return false;
			SideState->Conditions.Add(Condition);
			break;
		}
		default:
			return false;
		}

		TArray<FBattleTriggerLifecycleFact> Ignored;
		Engine.State->TriggerFramework.DrainLifecycleFacts(Ignored);
		return true;
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

	static bool PrepareLockedSwitch(
		FBattleEngine& Engine,
		const FBattlerId OutgoingBattlerId,
		const FPartySlotId IncomingPartySlotId)
	{
		if (!Engine.State.IsValid())
		{
			return false;
		}
		const FBattleBattlerState* Outgoing = Engine.State->FindBattler(OutgoingBattlerId);
		const FBattleActivePositionState* Active =
			Engine.State->ActivePositions.FindByPredicate(
				[OutgoingBattlerId](const FBattleActivePositionState& Candidate)
				{
					return Candidate.BattlerId == OutgoingBattlerId;
				});
		FBattleTrainerState* Trainer = Outgoing != nullptr
			? Engine.State->FindMutableTrainer(Outgoing->TrainerId)
			: nullptr;
		if (Outgoing == nullptr || Active == nullptr || Trainer == nullptr)
		{
			return false;
		}

		FBattleDecision Decision;
		if (!FBattleDecision::TryCreateSwitch(
			Engine.State->StateVersion,
			EBattleDecisionRequestKind::Action,
			Outgoing->TrainerId,
			OutgoingBattlerId,
			IncomingPartySlotId,
			Active->ActiveSlotId,
			Decision))
		{
			return false;
		}

		FBattleLockedActionState Action;
		Action.ActionId = BattleTest::MakeNumericId<FActionId>(7074);
		Action.QueueOrdinal = 1;
		Action.Decision = MoveTemp(Decision);
		Action.OrderKey.CommandBand = EBattleActionCommandBand::VoluntarySwitch;
		Action.OrderKey.EffectiveSpeed = Outgoing->PermanentStats.Speed;
		Action.OrderKey.ActingSlotId = Active->ActiveSlotId;
		Engine.State->LockedActions.Reset();
		Engine.State->LockedActions.Add(MoveTemp(Action));
		Engine.State->CurrentLockedActionIndex = 0;
		Engine.State->Phase = EBattlePhase::Locked;
		Trainer->ActionAllowance.MaximumActions = 1;
		Trainer->ActionAllowance.RemainingActions = 1;
		EBattleStateValidationError Error = EBattleStateValidationError::None;
		return Engine.State->ValidateInvariants(Error);
	}

	static int32 GetCurrentHP(const FBattleEngine& Engine, const FBattlerId BattlerId)
	{
		const FBattleBattlerState* Battler = Engine.State.IsValid()
			? Engine.State->FindBattler(BattlerId)
			: nullptr;
		return Battler != nullptr ? Battler->CurrentHP : INDEX_NONE;
	}

	static bool IsFainted(const FBattleEngine& Engine, const FBattlerId BattlerId)
	{
		const FBattleBattlerState* Battler = Engine.State.IsValid()
			? Engine.State->FindBattler(BattlerId)
			: nullptr;
		return Battler != nullptr && Battler->bFainted;
	}

	static FConditionId GetMajorStatus(
		const FBattleEngine& Engine,
		const FBattlerId BattlerId)
	{
		const FBattleBattlerState* Battler = Engine.State.IsValid()
			? Engine.State->FindBattler(BattlerId)
			: nullptr;
		return Battler != nullptr ? Battler->MajorStatusId : FConditionId();
	}

	static int32 GetStage(
		const FBattleEngine& Engine,
		const FBattlerId BattlerId,
		const EBattleStat Stat)
	{
		const FBattleBattlerState* Battler = Engine.State.IsValid()
			? Engine.State->FindBattler(BattlerId)
			: nullptr;
		int32 Stage = INDEX_NONE;
		return Battler != nullptr && Battler->Stages.TryGetStage(Stat, Stage)
			? Stage
			: INDEX_NONE;
	}

	static int32 GetPendingReplacementCount(const FBattleEngine& Engine)
	{
		return Engine.State.IsValid() ? Engine.State->PendingReplacements.Num() : INDEX_NONE;
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

	static bool ExecuteCatalogMove(
		FBattleEngine& Engine,
		const FBattlerId UserBattlerId,
		const FMoveId MoveId,
		const FBattleResolvedTarget& Target,
		FBattleEffectExecutionResult& OutResult,
		const uint64 OperationValue)
	{
		if (!Engine.State.IsValid())
		{
			return false;
		}
		const FBattleMoveDefinition* Move = Engine.State->Catalog.FindMove(MoveId);
		const FBattleActivePositionState* UserActive =
			Engine.State->ActivePositions.FindByPredicate(
				[UserBattlerId](const FBattleActivePositionState& Candidate)
				{
					return Candidate.BattlerId == UserBattlerId;
				});
		if (Move == nullptr || UserActive == nullptr)
		{
			return false;
		}
		FBattleEffectExecutionRequest Request;
		Request.BattleId = Engine.State->Setup.GetBattleId();
		Request.TurnId = Engine.State->TurnId;
		Request.ActionId = BattleTest::MakeNumericId<FActionId>(OperationValue);
		Request.ResolutionId = BattleTest::MakeNumericId<FResolutionId>(OperationValue);
		Request.UserBattlerId = UserBattlerId;
		Request.UserSlotId = UserActive->ActiveSlotId;
		Request.Move = Move;
		Request.Targets.Add(Target);
		EBattleEffectExecutorError Error = EBattleEffectExecutorError::None;
		return FBattleEffectExecutor::TryExecuteAgainstState(
			Request,
			*Engine.State,
			OutResult,
			Error);
	}

	static bool TryMakeBattlerTarget(
		const FBattleEngine& Engine,
		const FBattlerId BattlerId,
		FBattleResolvedTarget& OutTarget)
	{
		if (!Engine.State.IsValid())
		{
			return false;
		}
		const FBattleActivePositionState* Active =
			Engine.State->ActivePositions.FindByPredicate(
				[BattlerId](const FBattleActivePositionState& Candidate)
				{
					return Candidate.BattlerId == BattlerId;
				});
		if (Active == nullptr)
		{
			return false;
		}
		FBattleBattlerTarget Target;
		Target.ActiveSlotId = Active->ActiveSlotId;
		Target.BattlerId = BattlerId;
		return FBattleResolvedTarget::TryCreateBattler(Target, OutTarget);
	}

	static bool IsHeldItemSuppressed(
		const FBattleEngine& Engine,
		const FBattlerId BattlerId)
	{
		const FBattleBattlerState* Battler = Engine.State.IsValid()
			? Engine.State->FindBattler(BattlerId)
			: nullptr;
		return Battler != nullptr && Battler->HeldItem.bSuppressed;
	}

	static FItemId GetCurrentHeldItem(
		const FBattleEngine& Engine,
		const FBattlerId BattlerId)
	{
		const FBattleBattlerState* Battler = Engine.State.IsValid()
			? Engine.State->FindBattler(BattlerId)
			: nullptr;
		return Battler != nullptr ? Battler->HeldItem.CurrentItemId : FItemId();
	}

	static bool IsHeldItemConsumed(
		const FBattleEngine& Engine,
		const FBattlerId BattlerId)
	{
		const FBattleBattlerState* Battler = Engine.State.IsValid()
			? Engine.State->FindBattler(BattlerId)
			: nullptr;
		return Battler != nullptr && Battler->HeldItem.bConsumed;
	}

	static FBattlerId GetActiveBattler(
		const FBattleEngine& Engine,
		const EBattleSide Side)
	{
		const FBattleActivePositionState* Active = Engine.State.IsValid()
			? Engine.State->FindActivePosition(BattleTest::MakeActiveSlotId(
				Side,
				EBattlePosition::Left))
			: nullptr;
		return Active != nullptr ? Active->BattlerId : FBattlerId();
	}

	static int32 GetActiveTriggerCount(
		const FBattleEngine& Engine,
		const FConditionId ConditionId)
	{
		if (!Engine.State.IsValid())
		{
			return INDEX_NONE;
		}
		int32 Count = 0;
		for (const FBattleTriggerRegistrationState& Registration :
			Engine.State->TriggerFramework.GetActiveRegistrations())
		{
			if (Registration.Spec.SourceDefinition.Kind
					== EBattleTriggerSourceDefinitionKind::Condition
				&& Registration.Spec.SourceDefinition.ConditionId == ConditionId)
			{
				++Count;
			}
		}
		return Count;
	}

	static bool SetConditionPhaseSuppressed(
		FBattleEngine& Engine,
		const FConditionId ConditionId,
		const TOptional<EBattleSide>& Side,
		const EBattleTriggerPhase Phase,
		const bool bSuppressed)
	{
		if (!Engine.State.IsValid()
			|| !FBattleFieldSideConditionRules::IsCanonical(ConditionId)
			|| Engine.State->NextTriggerReentrancyToken == 0
			|| Engine.State->NextTriggerReentrancyToken == TNumericLimits<uint64>::Max())
		{
			return false;
		}

		FBattleTriggerSubject Owner;
		if (FBattleFieldSideConditionRules::IsFieldOwned(ConditionId))
		{
			if (Side.IsSet())
			{
				return false;
			}
			Owner = FBattleTriggerSubject::CreateField();
		}
		else if (!Side.IsSet()
			|| !FBattleTriggerSubject::TryCreateSide(Side.GetValue(), Owner))
		{
			return false;
		}

		FBattleTriggerOperationContext Operation;
		if (!FBattleTriggerReentrancyToken::TryCreate(
				Engine.State->NextTriggerReentrancyToken,
				Operation.ReentrancyToken))
		{
			return false;
		}
		++Engine.State->NextTriggerReentrancyToken;

		bool bMatched = false;
		EBattleTriggerError Error = EBattleTriggerError::None;
		const TArray<FBattleTriggerRegistrationState> Registrations =
			Engine.State->TriggerFramework.GetActiveRegistrations();
		for (const FBattleTriggerRegistrationState& Registration : Registrations)
		{
			if (Registration.Spec.Owner == Owner
				&& Registration.Spec.Rule.Phase == Phase
				&& Registration.Spec.SourceDefinition.Kind
					== EBattleTriggerSourceDefinitionKind::Condition
				&& Registration.Spec.SourceDefinition.ConditionId == ConditionId)
			{
				if (!Engine.State->TriggerFramework.TrySetSuppressed(
						Registration.RegistrationId,
						bSuppressed,
						Operation,
						Error))
				{
					return false;
				}
				bMatched = true;
			}
		}
		TArray<FBattleTriggerEffectRequest> IgnoredRequests;
		TArray<FBattleTriggerLifecycleFact> IgnoredFacts;
		Engine.State->TriggerFramework.DrainEffectRequests(IgnoredRequests);
		Engine.State->TriggerFramework.DrainLifecycleFacts(IgnoredFacts);
		return bMatched;
	}

	static int32 GetLockedEffectiveSpeed(
		const FBattleEngine& Engine,
		const FBattlerId BattlerId)
	{
		if (!Engine.State.IsValid())
		{
			return INDEX_NONE;
		}
		const FBattleLockedActionState* Action = Engine.State->LockedActions.FindByPredicate(
			[BattlerId](const FBattleLockedActionState& Candidate)
			{
				return Candidate.Decision.GetActingBattlerId() == BattlerId;
			});
		return Action != nullptr ? Action->OrderKey.EffectiveSpeed : INDEX_NONE;
	}
};

namespace BattleFieldSideConditionTests
{
	using BattleTest::MakeActiveSlotId;
	using BattleTest::MakeDefinitionId;
	using BattleTest::MakeNumericId;
	using BattleTest::MakePartySlotId;

	constexpr uint64 PlayerTrainerValue = 1;
	constexpr uint64 OpponentTrainerValue = 2;
	constexpr uint64 PlayerBattlerValue = 11;
	constexpr uint64 PlayerReserveValue = 12;
	constexpr uint64 PlayerSecondReserveValue = 13;
	constexpr uint64 OpponentBattlerValue = 21;
	constexpr uint64 OpponentReserveValue = 22;
	const TCHAR* MoveName = TEXT("Move.C07D.Test");
	const TCHAR* SetSunMoveName = TEXT("Move.C07D.SetSun");
	const TCHAR* SetGenericWeatherMoveName = TEXT("Move.C07D.SetGenericWeather");
	const TCHAR* RemoveSunMoveName = TEXT("Move.C07D.RemoveSun");
	const TCHAR* SetTailwindMoveName = TEXT("Move.C07D.SetTailwind");
	const TCHAR* RemoveTailwindMoveName = TEXT("Move.C07D.RemoveTailwind");
	const TCHAR* SetMagicRoomMoveName = TEXT("Move.C07D.SetMagicRoom");
	const TCHAR* RemoveMagicRoomMoveName = TEXT("Move.C07D.RemoveMagicRoom");
	const TCHAR* ForcedSwitchMoveName = TEXT("Move.C07D.ForcedSwitch");
	const TCHAR* PivotSwitchMoveName = TEXT("Move.C07D.PivotSwitch");
	const TCHAR* FireDamageMoveName = TEXT("Move.C07D.FireDamage");
	const TCHAR* GenericWeatherConditionName = TEXT("Condition.C07D.GenericWeather");
	const TCHAR* SpeciesName = TEXT("Species.C07D.Test");
	const TCHAR* AbilityName = TEXT("Ability.C07D.Test");
	const TCHAR* HeldItemName = TEXT("Item.C07D.TestHeld");

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

	FBattleMoveDefinition MakeMove()
	{
		FBattleMoveDefinition Move;
		Move.Id = MakeDefinitionId<FMoveId>(MoveName);
		Move.Type = EPokemonType::Normal;
		Move.Category = EBattleMoveCategory::Physical;
		Move.Power = 40;
		Move.bAlwaysHits = true;
		Move.bUsesPP = true;
		Move.BasePP = 20;
		Move.bAllowsPPBoosts = true;
		Move.TargetClass = EBattleTargetClass::SelectedOpponent;
		Move.Flags = EBattleMoveFlags::NeverCritical
			| EBattleMoveFlags::BypassesSideProtection
			| EBattleMoveFlags::ReducedByGrassyTerrain;
		FBattleMoveEffectDescriptor Damage;
		Damage.Kind = EBattleMoveEffectKind::Damage;
		Damage.Target = EBattleEffectTarget::ResolvedTarget;
		Move.Effects.Add(Damage);
		return Move;
	}

	FBattleMoveDefinition MakeConditionOperationMove(
		const TCHAR* Name,
		const EBattleMoveEffectKind EffectKind,
		const EBattleTargetClass TargetClass,
		const EBattleEffectTarget EffectTarget,
		const FConditionId ConditionId,
		const int32 DurationTurns = 0)
	{
		FBattleMoveDefinition Move;
		Move.Id = MakeDefinitionId<FMoveId>(Name);
		Move.Type = EPokemonType::Normal;
		Move.Category = EBattleMoveCategory::Status;
		Move.bAlwaysHits = true;
		Move.bUsesPP = true;
		Move.BasePP = 20;
		Move.bAllowsPPBoosts = true;
		Move.TargetClass = TargetClass;
		Move.Flags = EBattleMoveFlags::NeverCritical;
		FBattleMoveEffectDescriptor Effect;
		Effect.Kind = EffectKind;
		Effect.Target = EffectTarget;
		Effect.ConditionId = ConditionId;
		Effect.DurationTurns = DurationTurns;
		Move.Effects.Add(Effect);
		return Move;
	}

	FBattleMoveDefinition MakeFireDamageMove()
	{
		FBattleMoveDefinition Move;
		Move.Id = MakeDefinitionId<FMoveId>(FireDamageMoveName);
		Move.Type = EPokemonType::Fire;
		Move.Category = EBattleMoveCategory::Special;
		Move.Power = 40;
		Move.bAlwaysHits = true;
		Move.bUsesPP = true;
		Move.BasePP = 20;
		Move.bAllowsPPBoosts = true;
		Move.TargetClass = EBattleTargetClass::SelectedOpponent;
		Move.Flags = EBattleMoveFlags::NeverCritical;
		FBattleMoveEffectDescriptor Damage;
		Damage.Kind = EBattleMoveEffectKind::Damage;
		Damage.Target = EBattleEffectTarget::ResolvedTarget;
		Move.Effects.Add(Damage);
		return Move;
	}

	FBattleMoveDefinition MakeForcedSwitchMove()
	{
		FBattleMoveDefinition Move;
		Move.Id = MakeDefinitionId<FMoveId>(ForcedSwitchMoveName);
		Move.Type = EPokemonType::Normal;
		Move.Category = EBattleMoveCategory::Status;
		Move.bAlwaysHits = true;
		Move.bUsesPP = true;
		Move.BasePP = 20;
		Move.bAllowsPPBoosts = true;
		Move.TargetClass = EBattleTargetClass::SelectedOpponent;
		Move.Flags = EBattleMoveFlags::NeverCritical;
		FBattleMoveEffectDescriptor Switch;
		Switch.Kind = EBattleMoveEffectKind::Switch;
		Switch.Target = EBattleEffectTarget::ResolvedTarget;
		Move.Effects.Add(Switch);
		return Move;
	}

	FBattleMoveDefinition MakePivotSwitchMove()
	{
		FBattleMoveDefinition Move;
		Move.Id = MakeDefinitionId<FMoveId>(PivotSwitchMoveName);
		Move.Type = EPokemonType::Normal;
		Move.Category = EBattleMoveCategory::Status;
		Move.bAlwaysHits = true;
		Move.bUsesPP = true;
		Move.BasePP = 20;
		Move.bAllowsPPBoosts = true;
		Move.TargetClass = EBattleTargetClass::SelectedOpponent;
		Move.Flags = EBattleMoveFlags::NeverCritical;
		FBattleMoveEffectDescriptor Switch;
		Switch.Kind = EBattleMoveEffectKind::Switch;
		Switch.Target = EBattleEffectTarget::User;
		Move.Effects.Add(Switch);
		return Move;
	}

	FBattleDefinitionCatalog MakeCatalog()
	{
		FBattleDefinitionCatalogInput Input;
		Input.TypeChartEntries = MakeNeutralTypeChart();
		Input.Moves.Add(MakeMove());
		Input.Moves.Add(MakeFireDamageMove());
		Input.Moves.Add(MakeConditionOperationMove(
			SetSunMoveName,
			EBattleMoveEffectKind::SetFieldCondition,
			EBattleTargetClass::Field,
			EBattleEffectTarget::Field,
			FBattleFieldSideConditionRules::GetSunId(),
			5));
		Input.Moves.Add(MakeConditionOperationMove(
			SetGenericWeatherMoveName,
			EBattleMoveEffectKind::SetFieldCondition,
			EBattleTargetClass::Field,
			EBattleEffectTarget::Field,
			MakeDefinitionId<FConditionId>(GenericWeatherConditionName),
			5));
		Input.Moves.Add(MakeConditionOperationMove(
			RemoveSunMoveName,
			EBattleMoveEffectKind::RemoveCondition,
			EBattleTargetClass::Field,
			EBattleEffectTarget::Field,
			FBattleFieldSideConditionRules::GetSunId()));
		Input.Moves.Add(MakeConditionOperationMove(
			SetTailwindMoveName,
			EBattleMoveEffectKind::SetSideCondition,
			EBattleTargetClass::UserSide,
			EBattleEffectTarget::UserSide,
			FBattleFieldSideConditionRules::GetTailwindId(),
			4));
		Input.Moves.Add(MakeConditionOperationMove(
			RemoveTailwindMoveName,
			EBattleMoveEffectKind::RemoveCondition,
			EBattleTargetClass::UserSide,
			EBattleEffectTarget::UserSide,
			FBattleFieldSideConditionRules::GetTailwindId()));
		Input.Moves.Add(MakeConditionOperationMove(
			SetMagicRoomMoveName,
			EBattleMoveEffectKind::SetFieldCondition,
			EBattleTargetClass::Field,
			EBattleEffectTarget::Field,
			FBattleFieldSideConditionRules::GetMagicRoomId(),
			5));
		Input.Moves.Add(MakeConditionOperationMove(
			RemoveMagicRoomMoveName,
			EBattleMoveEffectKind::RemoveCondition,
			EBattleTargetClass::Field,
			EBattleEffectTarget::Field,
			FBattleFieldSideConditionRules::GetMagicRoomId()));
		Input.Moves.Add(MakeForcedSwitchMove());
		Input.Moves.Add(MakePivotSwitchMove());
		Input.Abilities.Add({MakeDefinitionId<FAbilityId>(AbilityName)});
		Input.Items.Add(
			{MakeDefinitionId<FItemId>(HeldItemName), EBattleItemKind::Held});
		FBattleSpeciesFormDefinition Species;
		Species.Id = MakeDefinitionId<FSpeciesFormId>(SpeciesName);
		Species.PrimaryType = EPokemonType::Normal;
		Species.BaseStats = {80, 80, 80, 80, 80, 80};
		Species.CatchRate = 45;
		Species.AbilityChoices.Add(MakeDefinitionId<FAbilityId>(AbilityName));
		Input.SpeciesForms.Add(Species);
		for (const FConditionId& ConditionId :
			FBattleFieldSideConditionRules::GetCanonicalIds())
		{
			Input.Conditions.Add(
				{ConditionId, FBattleFieldSideConditionRules::GetConditionFamily(ConditionId)});
		}
		Input.Conditions.Add(
			{MakeDefinitionId<FConditionId>(GenericWeatherConditionName),
				EBattleConditionKind::Weather});
		for (const FConditionId& StatusId : FBattleMajorStatusRules::GetCanonicalIds())
		{
			Input.Conditions.Add({StatusId, EBattleConditionKind::MajorStatus});
		}
		FBattleDefinitionCatalog Catalog;
		TArray<FBattleCatalogDiagnostic> Diagnostics;
		const bool bCreated = FBattleDefinitionCatalog::TryCreate(Input, Catalog, Diagnostics);
		check(bCreated);
		return Catalog;
	}

	FBattleTrainerSetup MakeTrainer(
		const uint64 TrainerValue,
		const EBattleSide Side,
		const EBattleTrainerRole Role)
	{
		FBattleTrainerSetup Trainer;
		Trainer.TrainerId = MakeNumericId<FTrainerId>(TrainerValue);
		Trainer.Side = Side;
		Trainer.Role = Role;
		Trainer.Controller = Role == EBattleTrainerRole::Player
			? EBattleDecisionController::Human
			: EBattleDecisionController::EnemyAI;
		Trainer.SelectorProfileId = MakeDefinitionId<FDefinitionId>(
			Role == EBattleTrainerRole::Player
				? TEXT("Selector.C07D.Player")
				: TEXT("Selector.C07D.Opponent"));
		return Trainer;
	}

	FBattlePartyEntrySetup MakePartyEntry(
		const uint64 TrainerValue,
		const uint64 BattlerValue,
		const int32 PartyIndex,
		const int32 Speed)
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
		Entry.OriginalHeldItemId = MakeDefinitionId<FItemId>(HeldItemName);
		Entry.CurrentHeldItemId = Entry.OriginalHeldItemId;
		Entry.Moves.Add({0, MakeDefinitionId<FMoveId>(MoveName), 20, 20});
		Entry.Moves.Add({1, MakeDefinitionId<FMoveId>(PivotSwitchMoveName), 20, 20});
		return Entry;
	}

	bool TryMakeSetup(
		const uint64 BattleValue,
		FBattleSetup& OutSetup,
		const bool bShiftPromptEligible = false)
	{
		FBattleSetupInput Input;
		Input.BattleId = MakeNumericId<FBattleId>(BattleValue);
		Input.SettingsReference =
			{MakeDefinitionId<FDefinitionId>(TEXT("Settings.C07D")), 1};
		Input.CatalogReference =
			{MakeDefinitionId<FDefinitionId>(TEXT("Catalog.C07D")), 1};
		Input.EncounterKind = EBattleEncounterKind::Trainer;
		Input.Format = EBattleFormat::Single;
		Input.CaptureCapacity = {2, 100};
		Input.Policies.bBagAllowed = false;
		Input.Policies.bRunAllowed = false;
		Input.Policies.bCaptureAllowed = false;
		Input.Policies.bShiftPromptEligible = bShiftPromptEligible;
		Input.Trainers.Add(MakeTrainer(
			PlayerTrainerValue, EBattleSide::Player, EBattleTrainerRole::Player));
		Input.Trainers.Add(MakeTrainer(
			OpponentTrainerValue, EBattleSide::Opponent, EBattleTrainerRole::Opponent));
		Input.PartyEntries.Add(MakePartyEntry(
			PlayerTrainerValue, PlayerBattlerValue, 0, 100));
		Input.PartyEntries.Add(MakePartyEntry(
			PlayerTrainerValue, PlayerReserveValue, 1, 90));
		Input.PartyEntries.Add(MakePartyEntry(
			PlayerTrainerValue, PlayerSecondReserveValue, 2, 85));
		Input.PartyEntries.Add(MakePartyEntry(
			OpponentTrainerValue, OpponentBattlerValue, 0, 80));
		Input.PartyEntries.Add(MakePartyEntry(
			OpponentTrainerValue, OpponentReserveValue, 1, 70));
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
		EBattleSetupValidationError Error = EBattleSetupValidationError::None;
		return FBattleSetup::TryCreate(Input, OutSetup, Error);
	}

	bool TryCreateEngine(
		const uint64 BattleValue,
		TUniquePtr<FBattleEngine>& OutEngine,
		const bool bShiftPromptEligible = false)
	{
		FBattleSetup Setup;
		if (!TryMakeSetup(BattleValue, Setup, bShiftPromptEligible))
		{
			return false;
		}
		FBattleRejection Rejection;
		return FBattleEngine::TryCreate(
			Setup,
			MakeCatalog(),
			MakeUnique<FSeededBattleRandom>(7074),
			OutEngine,
			Rejection);
	}

	bool TryCreateFightDecision(
		const FBattleDecisionRequest& Request,
		const FMoveId MoveId,
		const FActiveSlotId Target,
		FBattleDecision& OutDecision)
	{
		if (!Request.GetLegalMoveTargets().ContainsByPredicate(
			[MoveId, Target](const FBattleMoveTargetOption& Option)
			{
				return Option.MoveId == MoveId && Option.ActiveSlotId == Target;
			}))
		{
			return false;
		}
		return FBattleDecision::TryCreateFight(
			Request.GetStateVersion(),
			Request.GetDecisionOwnerTrainerId(),
			Request.GetActingBattlerId(),
			MoveId,
			Target,
			OutDecision);
	}

	bool LockSingleFightTurn(
		FBattleEngine& Engine,
		const FMoveId PlayerMoveId)
	{
		FBattleRejection Rejection;
		if (!Engine.TryBeginActionDecisionSequence(Rejection))
		{
			return false;
		}

		TArray<FBattleDecisionRequest> Requests = Engine.GetPendingDecisionRequests();
		if (Requests.Num() != 1
			|| Requests[0].GetDecisionOwnerTrainerId()
				!= MakeNumericId<FTrainerId>(PlayerTrainerValue))
		{
			return false;
		}
		FBattleDecision PlayerDecision;
		if (!TryCreateFightDecision(
				Requests[0],
				PlayerMoveId,
				MakeActiveSlotId(EBattleSide::Opponent, EBattlePosition::Left),
				PlayerDecision)
			|| !Engine.SubmitDecision(PlayerDecision).WasAccepted())
		{
			return false;
		}

		Requests = Engine.GetPendingDecisionRequests();
		if (Requests.Num() != 1
			|| Requests[0].GetDecisionOwnerTrainerId()
				!= MakeNumericId<FTrainerId>(OpponentTrainerValue))
		{
			return false;
		}
		FBattleDecision OpponentDecision;
		return TryCreateFightDecision(
				Requests[0],
				MakeDefinitionId<FMoveId>(MoveName),
				MakeActiveSlotId(EBattleSide::Player, EBattlePosition::Left),
				OpponentDecision)
			&& Engine.SubmitDecision(OpponentDecision).WasAccepted()
			&& Engine.GetSnapshot().GetPhase() == EBattlePhase::Locked;
	}

	bool AdvanceCurrentFightToEffects(FBattleEngine& Engine)
	{
		return Engine.BeginNextLockedAction().WasAccepted()
			&& Engine.CommitCurrentMoveAfterPreMoveGates().WasAccepted()
			&& Engine.ResolveCurrentMoveTargets().WasAccepted();
	}

	bool ExecuteNextQueuedAction(FBattleEngine& Engine)
	{
		if (!Engine.BeginNextLockedAction().WasAccepted())
		{
			return false;
		}
		if (!Engine.GetCurrentLockedAction().IsSet())
		{
			return true;
		}
		if (!Engine.CommitCurrentMoveAfterPreMoveGates().WasAccepted())
		{
			return false;
		}
		if (!Engine.GetCurrentLockedAction().IsSet())
		{
			return true;
		}
		if (!Engine.ResolveCurrentMoveTargets().WasAccepted())
		{
			return false;
		}
		if (!Engine.GetCurrentLockedAction().IsSet())
		{
			return true;
		}
		return Engine.ExecuteCurrentMoveEffects().WasAccepted();
	}

	bool FinishLockedQueue(FBattleEngine& Engine)
	{
		int32 Guard = 0;
		while ((Engine.GetSnapshot().GetPhase() == EBattlePhase::Locked
				|| Engine.GetSnapshot().GetPhase() == EBattlePhase::Resolving)
			&& !Engine.GetPendingDecision().IsSet()
			&& Guard++ < 8)
		{
			if (!ExecuteNextQueuedAction(Engine))
			{
				return false;
			}
		}
		return Guard < 8;
	}

	FBattleResolvedTarget MakeSideTarget(const EBattleSide Side)
	{
		FBattleResolvedTarget Target;
		const bool bCreated = FBattleResolvedTarget::TryCreateSide(Side, Target);
		check(bCreated);
		return Target;
	}

}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC07DCanonicalCatalogTriggerLifecycleTest,
	"PokemonSolarus.Battle.C07D.Contract.CanonicalCatalogTriggerLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC07DCanonicalCatalogTriggerLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace BattleFieldSideConditionTests;
	(void)Parameters;
	const TArray<FConditionId> Ids = FBattleFieldSideConditionRules::GetCanonicalIds();
	TestEqual(TEXT("Exactly 21 approved C07D conditions exist"), Ids.Num(), 21);
	const FBattleDefinitionCatalog Catalog = MakeCatalog();
	const FBattleMoveDefinition* HookMove = Catalog.FindMove(
		MakeDefinitionId<FMoveId>(MoveName));
	TestTrue(TEXT("The catalog accepts both reusable C07D move hooks"),
		HookMove != nullptr
			&& EnumHasAnyFlags(
				HookMove->Flags, EBattleMoveFlags::BypassesSideProtection)
			&& EnumHasAnyFlags(
				HookMove->Flags, EBattleMoveFlags::ReducedByGrassyTerrain));
	const TArray<FName> ExpectedNames = {
		FName(TEXT("Condition.Sun")),
		FName(TEXT("Condition.Rain")),
		FName(TEXT("Condition.Sandstorm")),
		FName(TEXT("Condition.Snow")),
		FName(TEXT("Condition.ElectricTerrain")),
		FName(TEXT("Condition.GrassyTerrain")),
		FName(TEXT("Condition.MistyTerrain")),
		FName(TEXT("Condition.PsychicTerrain")),
		FName(TEXT("Condition.Spikes")),
		FName(TEXT("Condition.ToxicSpikes")),
		FName(TEXT("Condition.StealthRock")),
		FName(TEXT("Condition.StickyWeb")),
		FName(TEXT("Condition.Reflect")),
		FName(TEXT("Condition.LightScreen")),
		FName(TEXT("Condition.AuroraVeil")),
		FName(TEXT("Condition.TrickRoom")),
		FName(TEXT("Condition.MagicRoom")),
		FName(TEXT("Condition.WonderRoom")),
		FName(TEXT("Condition.Tailwind")),
		FName(TEXT("Condition.Safeguard")),
		FName(TEXT("Condition.Mist"))
	};
	for (int32 Index = 0; Index < Ids.Num(); ++Index)
	{
		TestEqual(TEXT("Canonical condition order and identity are stable"),
			Ids[Index].GetDefinitionId().GetName(), ExpectedNames[Index]);
		TestTrue(TEXT("Every listed condition is canonical"),
			FBattleFieldSideConditionRules::IsCanonical(Ids[Index]));
		const FBattleConditionDefinition* Definition = Catalog.FindCondition(Ids[Index]);
		TestTrue(TEXT("Every canonical condition is frozen into the catalog family"),
			Definition != nullptr
				&& Definition->Kind
					== FBattleFieldSideConditionRules::GetConditionFamily(Ids[Index]));
		TestTrue(TEXT("Every canonical condition has exactly one ownership family"),
			FBattleFieldSideConditionRules::IsFieldOwned(Ids[Index])
				!= FBattleFieldSideConditionRules::IsSideOwned(Ids[Index]));

		TOptional<int32> Duration;
		TestTrue(TEXT("Every canonical condition exposes a duration policy"),
			FBattleFieldSideConditionRules::TryGetDuration(Ids[Index], false, Duration));
		int32 MaximumLayers = 0;
		TestTrue(TEXT("Every canonical condition exposes a layer policy"),
			FBattleFieldSideConditionRules::TryGetMaximumLayers(
				Ids[Index], MaximumLayers));
		TestTrue(TEXT("Every canonical layer cap is positive"), MaximumLayers > 0);

		FBattleTriggerSubject Owner;
		if (FBattleFieldSideConditionRules::IsFieldOwned(Ids[Index]))
		{
			Owner = FBattleTriggerSubject::CreateField();
		}
		else
		{
			TestTrue(TEXT("A side trigger owner is created"),
				FBattleTriggerSubject::TryCreateSide(EBattleSide::Player, Owner));
		}
		FBattleTriggerSubject Source;
		TestTrue(TEXT("A battler source is created"), FBattleTriggerSubject::TryCreateBattler(
			MakeNumericId<FBattlerId>(PlayerBattlerValue), Source));
		FBattleFieldSideTriggerRegistrationFacts Facts;
		Facts.ConditionId = Ids[Index];
		Facts.PayloadId = Ids[Index].GetDefinitionId();
		Facts.Owner = Owner;
		Facts.Source = Source;
		Facts.Targets.Add(Owner);
		Facts.RemainingTurns = Duration;
		Facts.Layers = 1;
		TArray<FBattleTriggerRegistrationSpec> Specs;
		TestTrue(TEXT("Every canonical condition builds C07A trigger registrations"),
			FBattleFieldSideConditionRules::TryBuildTriggerRegistrationSpecs(Facts, Specs));
		TestTrue(TEXT("Every canonical condition owns at least one trigger point"),
			!Specs.IsEmpty());
		for (const FBattleTriggerRegistrationSpec& Spec : Specs)
		{
			TestTrue(TEXT("Every registration uses the exact condition owner"),
				Spec.Owner == Owner);
			TestTrue(TEXT("Every registration keeps the condition source identity"),
				Spec.SourceDefinition.ConditionId == Ids[Index]);
		}
		if (Ids[Index] == FBattleFieldSideConditionRules::GetSandstormId())
		{
			TestTrue(TEXT("Sandstorm residual runs before default-last expiry"),
				Specs.ContainsByPredicate([](const FBattleTriggerRegistrationSpec& Spec)
				{
					return Spec.Rule.Phase == EBattleTriggerPhase::EndTurn
						&& Spec.Rule.Order == 1
						&& !Spec.Rule.bDecrementDurationBeforeEffect;
				})
				&& Specs.ContainsByPredicate([](const FBattleTriggerRegistrationSpec& Spec)
				{
					return Spec.Rule.Phase == EBattleTriggerPhase::EndTurn
						&& Spec.Rule.Order == TNumericLimits<int32>::Max()
						&& Spec.Rule.bDecrementDurationBeforeEffect;
				}));
		}
		if (Ids[Index] == FBattleFieldSideConditionRules::GetGrassyTerrainId())
		{
			TestTrue(TEXT("Grassy healing keeps order five suborder two before expiry"),
				Specs.ContainsByPredicate([](const FBattleTriggerRegistrationSpec& Spec)
				{
					return Spec.Rule.Phase == EBattleTriggerPhase::EndTurn
						&& Spec.Rule.Order == 5
						&& Spec.Rule.Suborder == 2
						&& !Spec.Rule.bDecrementDurationBeforeEffect;
				})
				&& Specs.ContainsByPredicate([](const FBattleTriggerRegistrationSpec& Spec)
				{
					return Spec.Rule.Phase == EBattleTriggerPhase::EndTurn
						&& Spec.Rule.Order == TNumericLimits<int32>::Max()
						&& Spec.Rule.bDecrementDurationBeforeEffect;
				}));
		}
	}

	FBattleTriggerSubject SideOwner;
	FBattleTriggerSubject Source;
	TestTrue(TEXT("Lifecycle side owner is created"),
		FBattleTriggerSubject::TryCreateSide(EBattleSide::Player, SideOwner));
	TestTrue(TEXT("Lifecycle source is created"),
		FBattleTriggerSubject::TryCreateBattler(
			MakeNumericId<FBattlerId>(PlayerBattlerValue), Source));
	FBattleFieldSideTriggerRegistrationFacts LifecycleFacts;
	LifecycleFacts.ConditionId = FBattleFieldSideConditionRules::GetSpikesId();
	LifecycleFacts.PayloadId = LifecycleFacts.ConditionId.GetDefinitionId();
	LifecycleFacts.Owner = SideOwner;
	LifecycleFacts.Source = Source;
	LifecycleFacts.Targets.Add(SideOwner);
	LifecycleFacts.Layers = 1;
	FBattleTriggerFramework Framework;
	EBattleTriggerError TriggerError = EBattleTriggerError::None;
	TestTrue(TEXT("Spikes registers through the shared trigger framework"),
		FBattleFieldSideConditionRules::TryRegisterTriggers(
			Framework, LifecycleFacts, TriggerError));
	TestTrue(TEXT("Spikes registration is active"),
		!Framework.GetActiveRegistrations().IsEmpty());
	FBattleTriggerOperationContext Context;
	TestTrue(TEXT("Lifecycle token is created"),
		FBattleTriggerReentrancyToken::TryCreate(1, Context.ReentrancyToken));
	TestTrue(TEXT("Layer updates use the shared trigger lifecycle"),
		FBattleFieldSideConditionRules::TryUpdateTriggerLayers(
			Framework,
			FBattleFieldSideConditionRules::GetSpikesId(),
			SideOwner,
			2,
			Context,
			TriggerError));
	for (const FBattleTriggerRegistrationState& Registration :
		Framework.GetActiveRegistrations())
	{
		TestEqual(TEXT("Every Spikes trigger receives the new layer count"),
			Registration.Layers, 2);
	}
	TestTrue(TEXT("Explicit removal cleans up condition registrations"),
		FBattleFieldSideConditionRules::TryCleanupTriggers(
			Framework,
			FBattleFieldSideConditionRules::GetSpikesId(),
			SideOwner,
			EBattleTriggerCleanupReason::Removal,
			Context,
			TriggerError));
	TestTrue(TEXT("No Spikes trigger remains after cleanup"),
		Framework.GetActiveRegistrations().IsEmpty());
	TArray<FBattleTriggerLifecycleFact> Lifecycle;
	Framework.DrainLifecycleFacts(Lifecycle);
	TestTrue(TEXT("Registration, layer change, and removal are observable lifecycle facts"),
		Lifecycle.ContainsByPredicate([](const FBattleTriggerLifecycleFact& Fact)
		{
			return Fact.Kind == EBattleTriggerLifecycleFactKind::Started;
		})
		&& Lifecycle.ContainsByPredicate([](const FBattleTriggerLifecycleFact& Fact)
		{
			return Fact.Kind == EBattleTriggerLifecycleFactKind::LayerChanged;
		})
		&& Lifecycle.ContainsByPredicate([](const FBattleTriggerLifecycleFact& Fact)
		{
			return Fact.Kind == EBattleTriggerLifecycleFactKind::Ended;
		}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC07DWeatherReplacementResidualExpiryTest,
	"PokemonSolarus.Battle.C07D.Weather.ReplacementResidualExpiry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC07DWeatherReplacementResidualExpiryTest::RunTest(const FString& Parameters)
{
	using namespace BattleFieldSideConditionTests;
	(void)Parameters;
	FBattleFieldSideApplicationFacts Application;
	Application.RequestedConditionId = FBattleFieldSideConditionRules::GetSunId();
	Application.ExistingExclusiveConditionId = FBattleFieldSideConditionRules::GetSunId();
	FBattleFieldSideApplicationResult ApplicationResult;
	TestTrue(TEXT("An identical weather request is evaluated"),
		FBattleFieldSideConditionRules::TryEvaluateApplication(
			Application, ApplicationResult));
	TestEqual(TEXT("Identical weather does not refresh"), ApplicationResult.Outcome,
		EBattleFieldSideApplicationOutcome::AlreadyActive);
	Application.RequestedConditionId = FBattleFieldSideConditionRules::GetRainId();
	TestTrue(TEXT("A different weather request is evaluated"),
		FBattleFieldSideConditionRules::TryEvaluateApplication(
			Application, ApplicationResult));
	TestEqual(TEXT("Different weather replaces the active weather"),
		ApplicationResult.Outcome,
		EBattleFieldSideApplicationOutcome::ReplaceExclusive);
	TestTrue(TEXT("Replacement explicitly removes the prior weather"),
		ApplicationResult.bRemoveExistingExclusive);

	TOptional<int32> Duration;
	TestTrue(TEXT("Ordinary Sun duration resolves"),
		FBattleFieldSideConditionRules::TryGetDuration(
			FBattleFieldSideConditionRules::GetSunId(), false, Duration));
	TestTrue(TEXT("Ordinary weather lasts five turns"),
		Duration.IsSet() && Duration.GetValue() == 5);
	TestTrue(TEXT("Extended Sun duration resolves"),
		FBattleFieldSideConditionRules::TryGetDuration(
			FBattleFieldSideConditionRules::GetSunId(), true, Duration));
	TestTrue(TEXT("The weather extension hook lasts eight turns"),
		Duration.IsSet() && Duration.GetValue() == 8);

	int32 Modifier = 0;
	TestTrue(TEXT("Sun Fire damage modifier resolves"),
		FBattleFieldSideConditionRules::TryGetWeatherDamageModifierQ12(
			FBattleFieldSideConditionRules::GetSunId(), EPokemonType::Fire, Modifier));
	TestEqual(TEXT("Sun boosts Fire by 1.5x"), Modifier, 6144);
	TestTrue(TEXT("Sun Water damage modifier resolves"),
		FBattleFieldSideConditionRules::TryGetWeatherDamageModifierQ12(
			FBattleFieldSideConditionRules::GetSunId(), EPokemonType::Water, Modifier));
	TestEqual(TEXT("Sun weakens Water by 0.5x"), Modifier, 2048);
	TestTrue(TEXT("Rain Fire damage modifier resolves"),
		FBattleFieldSideConditionRules::TryGetWeatherDamageModifierQ12(
			FBattleFieldSideConditionRules::GetRainId(), EPokemonType::Fire, Modifier));
	TestEqual(TEXT("Rain weakens Fire by 0.5x"), Modifier, 2048);
	TestTrue(TEXT("Rain Water damage modifier resolves"),
		FBattleFieldSideConditionRules::TryGetWeatherDamageModifierQ12(
			FBattleFieldSideConditionRules::GetRainId(), EPokemonType::Water, Modifier));
	TestEqual(TEXT("Rain boosts Water by 1.5x"), Modifier, 6144);
	TestTrue(TEXT("Sun blocks Freeze"),
		FBattleFieldSideConditionRules::ShouldSunPreventFreeze(
			FBattleFieldSideConditionRules::GetSunId()));

	TestTrue(TEXT("Sandstorm Rock special defense modifier resolves"),
		FBattleFieldSideConditionRules::TryGetWeatherDirectDefensiveModifierQ12(
			FBattleFieldSideConditionRules::GetSandstormId(),
			EPokemonType::Rock,
			EPokemonType::Invalid,
			EBattleMoveCategory::Special,
			Modifier));
	TestEqual(TEXT("Sandstorm directly boosts Rock special defense by 1.5x"),
		Modifier, 6144);
	TestTrue(TEXT("Snow Ice physical defense modifier resolves"),
		FBattleFieldSideConditionRules::TryGetWeatherDirectDefensiveModifierQ12(
			FBattleFieldSideConditionRules::GetSnowId(),
			EPokemonType::Ice,
			EPokemonType::Invalid,
			EBattleMoveCategory::Physical,
			Modifier));
	TestEqual(TEXT("Snow directly boosts Ice defense by 1.5x"), Modifier, 6144);

	TUniquePtr<FBattleEngine> NeutralDamageEngine;
	TUniquePtr<FBattleEngine> ActiveSunDamageEngine;
	TUniquePtr<FBattleEngine> SuppressedSunDamageEngine;
	const bool bDamageEnginesCreated =
		TryCreateEngine(70751, NeutralDamageEngine)
		&& TryCreateEngine(70752, ActiveSunDamageEngine)
		&& TryCreateEngine(70753, SuppressedSunDamageEngine);
	TestTrue(TEXT("The weather-trigger damage engines are created"),
		bDamageEnginesCreated);
	if (!bDamageEnginesCreated)
	{
		return false;
	}
	const FBattlerId DamageUserId = MakeNumericId<FBattlerId>(PlayerBattlerValue);
	const FBattlerId DamageTargetId = MakeNumericId<FBattlerId>(OpponentBattlerValue);
	TestTrue(TEXT("Active Sun is seeded for live damage"),
		FBattleC07DEngineFixture::SeedCondition(
			*ActiveSunDamageEngine,
			FBattleFieldSideConditionRules::GetSunId(),
			EBattleSide::Player,
			DamageUserId,
			TOptional<int32>(5)));
	TestTrue(TEXT("Suppressed Sun is seeded for live damage"),
		FBattleC07DEngineFixture::SeedCondition(
			*SuppressedSunDamageEngine,
			FBattleFieldSideConditionRules::GetSunId(),
			EBattleSide::Player,
			DamageUserId,
			TOptional<int32>(5)));
	TestTrue(TEXT("Sun's damage trigger can be suppressed without removing Sun"),
		FBattleC07DEngineFixture::SetConditionPhaseSuppressed(
			*SuppressedSunDamageEngine,
			FBattleFieldSideConditionRules::GetSunId(),
			TOptional<EBattleSide>(),
			EBattleTriggerPhase::BeforeDamage,
			true));
	FBattleResolvedTarget NeutralDamageTarget;
	FBattleResolvedTarget ActiveSunDamageTarget;
	FBattleResolvedTarget SuppressedSunDamageTarget;
	const bool bDamageTargetsResolved =
		FBattleC07DEngineFixture::TryMakeBattlerTarget(
			*NeutralDamageEngine, DamageTargetId, NeutralDamageTarget)
		&& FBattleC07DEngineFixture::TryMakeBattlerTarget(
			*ActiveSunDamageEngine, DamageTargetId, ActiveSunDamageTarget)
		&& FBattleC07DEngineFixture::TryMakeBattlerTarget(
			*SuppressedSunDamageEngine, DamageTargetId, SuppressedSunDamageTarget);
	TestTrue(TEXT("The weather-trigger damage targets resolve"), bDamageTargetsResolved);
	if (!bDamageTargetsResolved)
	{
		return false;
	}
	FBattleEffectExecutionResult NeutralDamageResult;
	FBattleEffectExecutionResult ActiveSunDamageResult;
	FBattleEffectExecutionResult SuppressedSunDamageResult;
	TestTrue(TEXT("Neutral Fire damage executes"),
		FBattleC07DEngineFixture::ExecuteCatalogMove(
			*NeutralDamageEngine,
			DamageUserId,
			MakeDefinitionId<FMoveId>(FireDamageMoveName),
			NeutralDamageTarget,
			NeutralDamageResult,
			707510));
	TestTrue(TEXT("Active-Sun Fire damage executes"),
		FBattleC07DEngineFixture::ExecuteCatalogMove(
			*ActiveSunDamageEngine,
			DamageUserId,
			MakeDefinitionId<FMoveId>(FireDamageMoveName),
			ActiveSunDamageTarget,
			ActiveSunDamageResult,
			707520));
	TestTrue(TEXT("Suppressed-Sun Fire damage executes"),
		FBattleC07DEngineFixture::ExecuteCatalogMove(
			*SuppressedSunDamageEngine,
			DamageUserId,
			MakeDefinitionId<FMoveId>(FireDamageMoveName),
			SuppressedSunDamageTarget,
			SuppressedSunDamageResult,
			707530));
	const int32 NeutralTargetHP = FBattleC07DEngineFixture::GetCurrentHP(
		*NeutralDamageEngine, DamageTargetId);
	const int32 ActiveSunTargetHP = FBattleC07DEngineFixture::GetCurrentHP(
		*ActiveSunDamageEngine, DamageTargetId);
	const int32 SuppressedSunTargetHP = FBattleC07DEngineFixture::GetCurrentHP(
		*SuppressedSunDamageEngine, DamageTargetId);
	TestTrue(TEXT("An emitted Sun trigger boosts live Fire damage"),
		ActiveSunTargetHP < NeutralTargetHP);
	TestEqual(TEXT("A suppressed Sun trigger contributes no damage modifier"),
		SuppressedSunTargetHP,
		NeutralTargetHP);
	TestTrue(TEXT("Suppressing the damage trigger does not remove Sun"),
		SuppressedSunDamageEngine->GetSnapshot().GetWeather().IsSet());

	FBattleFieldResidualFacts Residual;
	Residual.ConditionId = FBattleFieldSideConditionRules::GetSandstormId();
	Residual.BaseMaximumHP = 160;
	Residual.CurrentHP = 160;
	Residual.PrimaryType = EPokemonType::Normal;
	Residual.SecondaryType = EPokemonType::Invalid;
	FBattleFieldResidualResult ResidualResult;
	TestTrue(TEXT("Sandstorm residual resolves"),
		FBattleFieldSideConditionRules::TryResolveFieldResidual(
			Residual, ResidualResult));
	TestEqual(TEXT("Sandstorm deals a sixteenth of base maximum HP"),
		ResidualResult.Amount, 10);
	TestEqual(TEXT("Sandstorm residual is damage"), ResidualResult.EffectKind,
		EBattleFieldResidualEffectKind::Damage);
	Residual.PrimaryType = EPokemonType::Rock;
	TestTrue(TEXT("Rock immunity resolves"),
		FBattleFieldSideConditionRules::TryResolveFieldResidual(
			Residual, ResidualResult));
	TestEqual(TEXT("Rock types take no Sandstorm residual"), ResidualResult.EffectKind,
		EBattleFieldResidualEffectKind::None);
	Residual.ConditionId = FBattleFieldSideConditionRules::GetSnowId();
	Residual.PrimaryType = EPokemonType::Normal;
	TestTrue(TEXT("Snow residual resolves"),
		FBattleFieldSideConditionRules::TryResolveFieldResidual(
			Residual, ResidualResult));
	TestEqual(TEXT("Snow has no residual damage"), ResidualResult.EffectKind,
		EBattleFieldResidualEffectKind::None);

	TUniquePtr<FBattleEngine> EndTurnEngine;
	TestTrue(TEXT("The live field-residual engine is created"),
		TryCreateEngine(70744, EndTurnEngine));
	if (!EndTurnEngine.IsValid())
	{
		return false;
	}
	const FBattlerId PlayerId = MakeNumericId<FBattlerId>(PlayerBattlerValue);
	const FBattlerId OpponentId = MakeNumericId<FBattlerId>(OpponentBattlerValue);
	TestTrue(TEXT("The player is placed below maximum HP for ordered field residuals"),
		FBattleC07DEngineFixture::SetCurrentHP(*EndTurnEngine, PlayerId, 100));
	TestTrue(TEXT("The opponent is placed below maximum HP for ordered field residuals"),
		FBattleC07DEngineFixture::SetCurrentHP(*EndTurnEngine, OpponentId, 100));
	TestTrue(TEXT("Live Sandstorm is seeded with its ordinary duration"),
		FBattleC07DEngineFixture::SeedCondition(
			*EndTurnEngine,
			FBattleFieldSideConditionRules::GetSandstormId(),
			EBattleSide::Player,
			PlayerId,
			TOptional<int32>(5)));
	TestTrue(TEXT("Live Grassy Terrain is seeded with its ordinary duration"),
		FBattleC07DEngineFixture::SeedCondition(
			*EndTurnEngine,
			FBattleFieldSideConditionRules::GetGrassyTerrainId(),
			EBattleSide::Player,
			OpponentId,
			TOptional<int32>(5)));
	TestTrue(TEXT("The live engine enters its first field end turn"),
		FBattleC07DEngineFixture::PrepareEndTurn(*EndTurnEngine));
	const FBattleResolution FirstFieldEndTurn = EndTurnEngine->ResolveEndTurn();
	TestTrue(TEXT("The first live field end turn resolves"),
		FirstFieldEndTurn.WasAccepted());
	TestEqual(TEXT("Sandstorm damage then Grassy healing leaves player HP unchanged"),
		FBattleC07DEngineFixture::GetCurrentHP(*EndTurnEngine, PlayerId), 100);
	TestEqual(TEXT("Sandstorm damage then Grassy healing leaves opponent HP unchanged"),
		FBattleC07DEngineFixture::GetCurrentHP(*EndTurnEngine, OpponentId), 100);
	int32 LastSandEventIndex = INDEX_NONE;
	int32 FirstGrassyEventIndex = INDEX_NONE;
	bool bSandSourceExact = true;
	bool bGrassySourceExact = true;
	for (int32 Index = 0; Index < FirstFieldEndTurn.GetEvents().Num(); ++Index)
	{
		const FBattleEvent& Event = FirstFieldEndTurn.GetEvents()[Index];
		if (Event.GetSource().DefinitionId
			== FBattleFieldSideConditionRules::GetSandstormId().GetDefinitionId())
		{
			LastSandEventIndex = Index;
			bSandSourceExact = bSandSourceExact
				&& Event.GetSource().BattlerId == PlayerId;
		}
		if (Event.GetSource().DefinitionId
			== FBattleFieldSideConditionRules::GetGrassyTerrainId().GetDefinitionId())
		{
			if (FirstGrassyEventIndex == INDEX_NONE)
			{
				FirstGrassyEventIndex = Index;
			}
			bGrassySourceExact = bGrassySourceExact
				&& Event.GetSource().BattlerId == OpponentId;
		}
	}
	TestTrue(TEXT("Every Sandstorm mutation precedes every Grassy Terrain mutation"),
		LastSandEventIndex != INDEX_NONE
			&& FirstGrassyEventIndex != INDEX_NONE
			&& LastSandEventIndex < FirstGrassyEventIndex);
	TestTrue(TEXT("Sandstorm residual events preserve the creating battler source"),
		bSandSourceExact);
	TestTrue(TEXT("Grassy Terrain residual events preserve the creating battler source"),
		bGrassySourceExact);
	const FBattleSnapshot FourTurnSnapshot = EndTurnEngine->GetSnapshot();
	TestTrue(TEXT("Live weather duration decrements from five to four"),
		FourTurnSnapshot.GetWeather().IsSet()
			&& FourTurnSnapshot.GetWeather().GetValue().RemainingTurns.IsSet()
			&& FourTurnSnapshot.GetWeather().GetValue().RemainingTurns.GetValue() == 4);
	TestTrue(TEXT("Live terrain duration decrements from five to four"),
		FourTurnSnapshot.GetTerrain().IsSet()
			&& FourTurnSnapshot.GetTerrain().GetValue().RemainingTurns.IsSet()
			&& FourTurnSnapshot.GetTerrain().GetValue().RemainingTurns.GetValue() == 4);

	for (int32 ExpectedRemaining = 3; ExpectedRemaining >= 1; --ExpectedRemaining)
	{
		TestTrue(TEXT("The live engine re-enters end turn for another duration tick"),
			FBattleC07DEngineFixture::PrepareEndTurn(*EndTurnEngine));
		TestTrue(TEXT("The live duration tick resolves"),
			EndTurnEngine->ResolveEndTurn().WasAccepted());
		const FBattleSnapshot TickSnapshot = EndTurnEngine->GetSnapshot();
		TestTrue(TEXT("Weather publishes the exact live remaining duration"),
			TickSnapshot.GetWeather().IsSet()
				&& TickSnapshot.GetWeather().GetValue().RemainingTurns.IsSet()
				&& TickSnapshot.GetWeather().GetValue().RemainingTurns.GetValue()
					== ExpectedRemaining);
		TestTrue(TEXT("Terrain publishes the exact live remaining duration"),
			TickSnapshot.GetTerrain().IsSet()
				&& TickSnapshot.GetTerrain().GetValue().RemainingTurns.IsSet()
				&& TickSnapshot.GetTerrain().GetValue().RemainingTurns.GetValue()
					== ExpectedRemaining);
	}
	TestTrue(TEXT("The live engine enters the conditions' final active turn"),
		FBattleC07DEngineFixture::PrepareEndTurn(*EndTurnEngine));
	const FBattleResolution FinalFieldEndTurn = EndTurnEngine->ResolveEndTurn();
	TestTrue(TEXT("The final active field turn resolves"),
		FinalFieldEndTurn.WasAccepted());
	TestTrue(TEXT("Sandstorm still acts on its final active turn"),
		FinalFieldEndTurn.GetEvents().ContainsByPredicate(
			[](const FBattleEvent& Event)
			{
				return Event.GetType() == EBattleEventType::Damage
					&& Event.GetSource().DefinitionId
						== FBattleFieldSideConditionRules::GetSandstormId().GetDefinitionId();
			}));
	TestTrue(TEXT("Grassy Terrain still acts on its final active turn"),
		FinalFieldEndTurn.GetEvents().ContainsByPredicate(
			[](const FBattleEvent& Event)
			{
				return Event.GetType() == EBattleEventType::Healing
					&& Event.GetSource().DefinitionId
						== FBattleFieldSideConditionRules::GetGrassyTerrainId().GetDefinitionId();
			}));
	TestFalse(TEXT("Weather expires after its fifth live end turn"),
		EndTurnEngine->GetSnapshot().GetWeather().IsSet());
	TestFalse(TEXT("Terrain expires after its fifth live end turn"),
		EndTurnEngine->GetSnapshot().GetTerrain().IsSet());
	TestEqual(TEXT("Field residuals and duration expiry consume no RNG"),
		EndTurnEngine->ExportRandomTrace().Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC07DTerrainGroundedRulesPriorityExpiryTest,
	"PokemonSolarus.Battle.C07D.Terrain.GroundedRulesPriorityExpiry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC07DTerrainGroundedRulesPriorityExpiryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FBattleGroundedFacts GroundedFacts;
	GroundedFacts.PrimaryType = EPokemonType::Normal;
	GroundedFacts.SecondaryType = EPokemonType::Invalid;
	bool bGrounded = false;
	TestTrue(TEXT("Ordinary grounding resolves"),
		FBattleFieldSideConditionRules::TryResolveGrounded(GroundedFacts, bGrounded));
	TestTrue(TEXT("An ordinary battler is grounded"), bGrounded);
	GroundedFacts.PrimaryType = EPokemonType::Flying;
	TestTrue(TEXT("Flying grounding resolves"),
		FBattleFieldSideConditionRules::TryResolveGrounded(GroundedFacts, bGrounded));
	TestFalse(TEXT("Flying types are not grounded"), bGrounded);
	GroundedFacts.PrimaryType = EPokemonType::Normal;
	GroundedFacts.bAbilityMakesAirborne = true;
	TestTrue(TEXT("Ability-provided airborne state resolves"),
		FBattleFieldSideConditionRules::TryResolveGrounded(GroundedFacts, bGrounded));
	TestFalse(TEXT("An unsuppressed airborne Ability prevents grounding"), bGrounded);
	GroundedFacts.bAbilitySuppressed = true;
	TestTrue(TEXT("Suppressed Ability grounding resolves"),
		FBattleFieldSideConditionRules::TryResolveGrounded(GroundedFacts, bGrounded));
	TestTrue(TEXT("Suppression restores grounding"), bGrounded);
	GroundedFacts.bAbilityMakesAirborne = false;
	GroundedFacts.bAbilitySuppressed = false;
	GroundedFacts.bItemMakesAirborne = true;
	GroundedFacts.bItemSuppressed = false;
	TestTrue(TEXT("Item-provided airborne state resolves"),
		FBattleFieldSideConditionRules::TryResolveGrounded(GroundedFacts, bGrounded));
	TestFalse(TEXT("An unsuppressed airborne item prevents grounding"), bGrounded);
	GroundedFacts.bItemSuppressed = true;
	GroundedFacts.bAirborneSemiInvulnerable = true;
	TestTrue(TEXT("Semi-invulnerable grounding resolves"),
		FBattleFieldSideConditionRules::TryResolveGrounded(GroundedFacts, bGrounded));
	TestFalse(TEXT("Airborne semi-invulnerability prevents grounding"), bGrounded);

	int32 Modifier = 0;
	TestTrue(TEXT("Electric Terrain power modifier resolves"),
		FBattleFieldSideConditionRules::TryGetTerrainPowerModifierQ12(
			FBattleFieldSideConditionRules::GetElectricTerrainId(),
			EPokemonType::Electric,
			true,
			Modifier));
	TestEqual(TEXT("Grounded Electric attacks receive the modern terrain boost"),
		Modifier, 5325);
	TestTrue(TEXT("Airborne Electric attack modifier resolves"),
		FBattleFieldSideConditionRules::TryGetTerrainPowerModifierQ12(
			FBattleFieldSideConditionRules::GetElectricTerrainId(),
			EPokemonType::Electric,
			false,
			Modifier));
	TestEqual(TEXT("Airborne attackers receive no terrain boost"), Modifier, 4096);
	TestTrue(TEXT("Grassy Terrain Ground reduction resolves"),
		FBattleFieldSideConditionRules::TryGetTerrainFinalDamageModifierQ12(
			FBattleFieldSideConditionRules::GetGrassyTerrainId(),
			EPokemonType::Ground,
			true,
			true,
			Modifier));
	TestEqual(TEXT("Affected Ground moves are halved against grounded defenders"),
		Modifier, 2048);
	TestTrue(TEXT("Misty Terrain Dragon reduction resolves"),
		FBattleFieldSideConditionRules::TryGetTerrainFinalDamageModifierQ12(
			FBattleFieldSideConditionRules::GetMistyTerrainId(),
			EPokemonType::Dragon,
			true,
			false,
			Modifier));
	TestEqual(TEXT("Dragon damage is halved against grounded defenders"), Modifier, 2048);

	TestTrue(TEXT("Electric Terrain prevents grounded Sleep"),
		FBattleFieldSideConditionRules::ShouldTerrainPreventMajorStatus(
			FBattleFieldSideConditionRules::GetElectricTerrainId(),
			FBattleMajorStatusRules::GetSleepId(),
			true));
	TestFalse(TEXT("Electric Terrain does not prevent airborne Sleep"),
		FBattleFieldSideConditionRules::ShouldTerrainPreventMajorStatus(
			FBattleFieldSideConditionRules::GetElectricTerrainId(),
			FBattleMajorStatusRules::GetSleepId(),
			false));
	TestTrue(TEXT("Misty Terrain prevents grounded major status"),
		FBattleFieldSideConditionRules::ShouldTerrainPreventMajorStatus(
			FBattleFieldSideConditionRules::GetMistyTerrainId(),
			FBattleMajorStatusRules::GetBurnId(),
			true));
	TestTrue(TEXT("Misty Terrain prevents grounded Confusion"),
		FBattleFieldSideConditionRules::ShouldTerrainPreventConfusion(
			FBattleFieldSideConditionRules::GetMistyTerrainId(), true));
	TestTrue(TEXT("Psychic Terrain blocks an opposing positive-integer-priority move"),
		FBattleFieldSideConditionRules::ShouldPsychicTerrainBlockMove(
			FBattleFieldSideConditionRules::GetPsychicTerrainId(), true, true, 1, 0));
	TestFalse(TEXT("Quick Claw-only plus 0.1 priority is not blocked"),
		FBattleFieldSideConditionRules::ShouldPsychicTerrainBlockMove(
			FBattleFieldSideConditionRules::GetPsychicTerrainId(), true, true, 0, 1));
	TestFalse(TEXT("Psychic Terrain does not block allies"),
		FBattleFieldSideConditionRules::ShouldPsychicTerrainBlockMove(
			FBattleFieldSideConditionRules::GetPsychicTerrainId(), false, true, 1, 0));
	TestFalse(TEXT("Psychic Terrain does not block moves against airborne defenders"),
		FBattleFieldSideConditionRules::ShouldPsychicTerrainBlockMove(
			FBattleFieldSideConditionRules::GetPsychicTerrainId(), true, false, 1, 0));

	FBattleFieldResidualFacts Residual;
	Residual.ConditionId = FBattleFieldSideConditionRules::GetGrassyTerrainId();
	Residual.BaseMaximumHP = 160;
	Residual.CurrentHP = 100;
	Residual.PrimaryType = EPokemonType::Normal;
	Residual.SecondaryType = EPokemonType::Invalid;
	Residual.bGrounded = true;
	FBattleFieldResidualResult ResidualResult;
	TestTrue(TEXT("Grassy Terrain residual resolves"),
		FBattleFieldSideConditionRules::TryResolveFieldResidual(Residual, ResidualResult));
	TestEqual(TEXT("Grassy Terrain heals one sixteenth of base maximum HP"),
		ResidualResult.Amount, 10);
	TestEqual(TEXT("Grassy Terrain residual is healing"), ResidualResult.EffectKind,
		EBattleFieldResidualEffectKind::Heal);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC07DHazardLayersGroundingRemovalTest,
	"PokemonSolarus.Battle.C07D.Hazards.LayersGroundingRemoval",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC07DHazardLayersGroundingRemovalTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FBattleFieldSideApplicationFacts Application;
	Application.RequestedConditionId = FBattleFieldSideConditionRules::GetSpikesId();
	Application.bRequestedAlreadyActive = true;
	Application.ExistingLayers = 1;
	FBattleFieldSideApplicationResult ApplicationResult;
	TestTrue(TEXT("A second Spikes layer is evaluated"),
		FBattleFieldSideConditionRules::TryEvaluateApplication(
			Application, ApplicationResult));
	TestEqual(TEXT("Spikes adds a layer below its cap"), ApplicationResult.Outcome,
		EBattleFieldSideApplicationOutcome::AddLayer);
	TestEqual(TEXT("The second Spikes application stores two layers"),
		ApplicationResult.Layers, 2);
	Application.ExistingLayers = 3;
	TestTrue(TEXT("Capped Spikes is evaluated"),
		FBattleFieldSideConditionRules::TryEvaluateApplication(
			Application, ApplicationResult));
	TestEqual(TEXT("Spikes stops at three layers"), ApplicationResult.Outcome,
		EBattleFieldSideApplicationOutcome::LayerCapReached);

	FBattleHazardSwitchInFacts Hazard;
	Hazard.HazardId = FBattleFieldSideConditionRules::GetSpikesId();
	Hazard.BaseMaximumHP = 160;
	Hazard.CurrentHP = 160;
	Hazard.PrimaryType = EPokemonType::Normal;
	Hazard.SecondaryType = EPokemonType::Invalid;
	Hazard.bGrounded = true;
	FBattleHazardSwitchInResult HazardResult;
	const TArray<int32> ExpectedSpikesDamage = {20, 26, 40};
	for (int32 Layers = 1; Layers <= 3; ++Layers)
	{
		Hazard.Layers = Layers;
		TestTrue(TEXT("A valid Spikes layer resolves"),
			FBattleFieldSideConditionRules::TryResolveHazardSwitchIn(
				Hazard, HazardResult));
		TestEqual(TEXT("Spikes uses the exact modern layer fraction"),
			HazardResult.Damage, ExpectedSpikesDamage[Layers - 1]);
	}
	Hazard.bGrounded = false;
	TestTrue(TEXT("Airborne Spikes resolution is valid"),
		FBattleFieldSideConditionRules::TryResolveHazardSwitchIn(Hazard, HazardResult));
	TestEqual(TEXT("Airborne battlers ignore Spikes"), HazardResult.EffectKind,
		EBattleHazardSwitchInEffectKind::None);
	Hazard.bGrounded = true;
	Hazard.bBypassesEntryHazards = true;
	TestTrue(TEXT("Entry-hazard bypass resolves"),
		FBattleFieldSideConditionRules::TryResolveHazardSwitchIn(Hazard, HazardResult));
	TestEqual(TEXT("The item hook bypasses every entry hazard"), HazardResult.EffectKind,
		EBattleHazardSwitchInEffectKind::None);

	Hazard = FBattleHazardSwitchInFacts();
	Hazard.HazardId = FBattleFieldSideConditionRules::GetToxicSpikesId();
	Hazard.Layers = 1;
	Hazard.BaseMaximumHP = 160;
	Hazard.CurrentHP = 160;
	Hazard.PrimaryType = EPokemonType::Normal;
	Hazard.SecondaryType = EPokemonType::Invalid;
	Hazard.bGrounded = true;
	TestTrue(TEXT("One Toxic Spikes layer resolves"),
		FBattleFieldSideConditionRules::TryResolveHazardSwitchIn(Hazard, HazardResult));
	TestEqual(TEXT("One Toxic Spikes layer applies Poison"), HazardResult.MajorStatusId,
		FBattleMajorStatusRules::GetPoisonId());
	Hazard.Layers = 2;
	TestTrue(TEXT("Two Toxic Spikes layers resolve"),
		FBattleFieldSideConditionRules::TryResolveHazardSwitchIn(Hazard, HazardResult));
	TestEqual(TEXT("Two Toxic Spikes layers apply Toxic"), HazardResult.MajorStatusId,
		FBattleMajorStatusRules::GetToxicId());
	Hazard.PrimaryType = EPokemonType::Poison;
	TestTrue(TEXT("A grounded Poison type resolves Toxic Spikes"),
		FBattleFieldSideConditionRules::TryResolveHazardSwitchIn(Hazard, HazardResult));
	TestTrue(TEXT("A grounded Poison type removes Toxic Spikes"),
		HazardResult.bRemoveHazard
			&& HazardResult.EffectKind == EBattleHazardSwitchInEffectKind::RemoveHazard);
	Hazard.PrimaryType = EPokemonType::Steel;
	TestTrue(TEXT("A grounded Steel type resolves Toxic Spikes"),
		FBattleFieldSideConditionRules::TryResolveHazardSwitchIn(Hazard, HazardResult));
	TestEqual(TEXT("Steel types are unaffected by Toxic Spikes"), HazardResult.EffectKind,
		EBattleHazardSwitchInEffectKind::None);

	Hazard = FBattleHazardSwitchInFacts();
	Hazard.HazardId = FBattleFieldSideConditionRules::GetStealthRockId();
	Hazard.Layers = 1;
	Hazard.BaseMaximumHP = 160;
	Hazard.CurrentHP = 160;
	Hazard.PrimaryType = EPokemonType::Normal;
	Hazard.SecondaryType = EPokemonType::Invalid;
	Hazard.RockEffectiveness = {2, 1};
	TestTrue(TEXT("Stealth Rock resolves"),
		FBattleFieldSideConditionRules::TryResolveHazardSwitchIn(Hazard, HazardResult));
	TestEqual(TEXT("Stealth Rock scales one eighth by Rock effectiveness"),
		HazardResult.Damage, 40);

	Hazard = FBattleHazardSwitchInFacts();
	Hazard.HazardId = FBattleFieldSideConditionRules::GetStickyWebId();
	Hazard.Layers = 1;
	Hazard.BaseMaximumHP = 160;
	Hazard.CurrentHP = 160;
	Hazard.PrimaryType = EPokemonType::Normal;
	Hazard.SecondaryType = EPokemonType::Invalid;
	Hazard.bGrounded = true;
	TestTrue(TEXT("Sticky Web resolves"),
		FBattleFieldSideConditionRules::TryResolveHazardSwitchIn(Hazard, HazardResult));
	TestTrue(TEXT("Sticky Web requests exactly one Speed-stage drop"),
		HazardResult.EffectKind == EBattleHazardSwitchInEffectKind::ModifyStatStage
			&& HazardResult.Stat == EBattleStat::Speed
			&& HazardResult.StatStageDelta == -1);
	Hazard.bStatStageDropPrevented = true;
	TestTrue(TEXT("Sticky Web prevention resolves"),
		FBattleFieldSideConditionRules::TryResolveHazardSwitchIn(Hazard, HazardResult));
	TestEqual(TEXT("The prevention hook blocks Sticky Web"), HazardResult.EffectKind,
		EBattleHazardSwitchInEffectKind::None);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC07DHazardSwitchReplacementFaintBoundaryTest,
	"PokemonSolarus.Battle.C07D.Hazards.SwitchReplacementFaintBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC07DHazardSwitchReplacementFaintBoundaryTest::RunTest(
	const FString& Parameters)
{
	using namespace BattleFieldSideConditionTests;
	(void)Parameters;
	TUniquePtr<FBattleEngine> Engine;
	TestTrue(TEXT("The switch-in hazard engine is created"), TryCreateEngine(70741, Engine));
	if (!Engine.IsValid())
	{
		return false;
	}
	const FBattlerId PlayerId = MakeNumericId<FBattlerId>(PlayerBattlerValue);
	const FBattlerId ReserveId = MakeNumericId<FBattlerId>(PlayerReserveValue);
	TestTrue(TEXT("The incoming reserve is placed in lethal Spikes range"),
		FBattleC07DEngineFixture::SetCurrentHP(*Engine, ReserveId, 10));
	TestTrue(TEXT("Spikes is seeded first on the incoming side"),
		FBattleC07DEngineFixture::SeedCondition(
			*Engine,
			FBattleFieldSideConditionRules::GetSpikesId(),
			EBattleSide::Player,
			MakeNumericId<FBattlerId>(OpponentBattlerValue),
			TOptional<int32>(),
			1));
	TestTrue(TEXT("Sticky Web is seeded after Spikes"),
		FBattleC07DEngineFixture::SeedCondition(
			*Engine,
			FBattleFieldSideConditionRules::GetStickyWebId(),
			EBattleSide::Player,
			MakeNumericId<FBattlerId>(OpponentBattlerValue),
			TOptional<int32>(),
			1));
	TestTrue(TEXT("A legal voluntary switch is locked"),
		FBattleC07DEngineFixture::PrepareLockedSwitch(
			*Engine, PlayerId, MakePartySlotId(1)));
	TestTrue(TEXT("The switch action starts"), Engine->BeginNextLockedAction().WasAccepted());
	const FBattleResolution LethalSwitch = Engine->ExecuteCurrentSwitch();
	TestTrue(TEXT("The switch action resolves"), LethalSwitch.WasAccepted());
	TestEqual(TEXT("Spikes damage is settled at switch-in"),
		FBattleC07DEngineFixture::GetCurrentHP(*Engine, ReserveId), 0);
	TestTrue(TEXT("A hazard-lethal incoming battler faints immediately"),
		FBattleC07DEngineFixture::IsFainted(*Engine, ReserveId));
	TestEqual(TEXT("Later-created Sticky Web does not run after the faint"),
		FBattleC07DEngineFixture::GetStage(*Engine, ReserveId, EBattleStat::Speed), 0);
	TestTrue(TEXT("The lethal switch emits Spikes damage for the incoming battler"),
		LethalSwitch.GetEvents().ContainsByPredicate(
			[ReserveId](const FBattleEvent& Event)
			{
				return Event.GetType() == EBattleEventType::Damage
					&& Event.GetSource().DefinitionId
						== FBattleFieldSideConditionRules::GetSpikesId().GetDefinitionId()
					&& Event.GetTargets().ContainsByPredicate(
						[ReserveId](const FBattleEventTarget& Target)
						{
							return Target.BattlerId == ReserveId;
						});
			}));
	TestFalse(TEXT("No Sticky Web stage-change event occurs after lethal Spikes"),
		LethalSwitch.GetEvents().ContainsByPredicate(
			[ReserveId](const FBattleEvent& Event)
			{
				return Event.GetType() == EBattleEventType::StatStageChanged
					&& Event.GetSource().DefinitionId
						== FBattleFieldSideConditionRules::GetStickyWebId().GetDefinitionId()
					&& Event.GetTargets().ContainsByPredicate(
						[ReserveId](const FBattleEventTarget& Target)
						{
							return Target.BattlerId == ReserveId;
						});
			}));
	TestEqual(TEXT("The faint is processed before another action"),
		Engine->GetSnapshot().GetPhase(), EBattlePhase::MandatoryReplacement);
	TestEqual(TEXT("The vacated slot has one replacement requirement"),
		FBattleC07DEngineFixture::GetPendingReplacementCount(*Engine), 1);

	TestTrue(TEXT("The first replacement is placed back in lethal Spikes range"),
		FBattleC07DEngineFixture::SetCurrentHP(*Engine, PlayerId, 10));
	const TArray<FBattleDecisionRequest> FirstReplacementRequests =
		Engine->GetPendingDecisionRequests();
	TestEqual(TEXT("One actorless replacement request is exposed"),
		FirstReplacementRequests.Num(), 1);
	if (FirstReplacementRequests.Num() != 1)
	{
		return false;
	}
	FBattleDecision FirstReplacement;
	TestTrue(TEXT("The first mandatory replacement decision is created"),
		FBattleDecision::TryCreateReplacement(
			FirstReplacementRequests[0].GetStateVersion(),
			FirstReplacementRequests[0].GetDecisionOwnerTrainerId(),
			MakePartySlotId(0),
			FirstReplacementRequests[0].GetActingSlotId(),
			FirstReplacement));
	const FBattleResolution FirstReplacementResolution =
		Engine->SubmitDecision(FirstReplacement);
	TestTrue(TEXT("The first mandatory replacement resolves"),
		FirstReplacementResolution.WasAccepted());
	TestTrue(TEXT("Hazards may faint a mandatory replacement immediately"),
		FBattleC07DEngineFixture::IsFainted(*Engine, PlayerId));
	TestEqual(TEXT("A hazard-fainted mandatory replacement is requested again"),
		Engine->GetSnapshot().GetPhase(), EBattlePhase::MandatoryReplacement);
	TestEqual(TEXT("Exactly one replacement remains after the hazard faint"),
		FBattleC07DEngineFixture::GetPendingReplacementCount(*Engine), 1);

	const TArray<FBattleDecisionRequest> SecondReplacementRequests =
		Engine->GetPendingDecisionRequests();
	TestEqual(TEXT("The replacement chain exposes its next actorless request"),
		SecondReplacementRequests.Num(), 1);
	if (SecondReplacementRequests.Num() != 1)
	{
		return false;
	}
	FBattleDecision SecondReplacement;
	TestTrue(TEXT("The surviving replacement decision is created"),
		FBattleDecision::TryCreateReplacement(
			SecondReplacementRequests[0].GetStateVersion(),
			SecondReplacementRequests[0].GetDecisionOwnerTrainerId(),
			MakePartySlotId(2),
			SecondReplacementRequests[0].GetActingSlotId(),
			SecondReplacement));
	TestTrue(TEXT("The surviving replacement resolves"),
		Engine->SubmitDecision(SecondReplacement).WasAccepted());
	const FBattlerId SecondReserveId =
		MakeNumericId<FBattlerId>(PlayerSecondReserveValue);
	TestEqual(TEXT("The surviving replacement takes Spikes damage"),
		FBattleC07DEngineFixture::GetCurrentHP(*Engine, SecondReserveId), 140);
	TestEqual(TEXT("The later-created Sticky Web follows Spikes for a survivor"),
		FBattleC07DEngineFixture::GetStage(
			*Engine, SecondReserveId, EBattleStat::Speed), -1);
	TestEqual(TEXT("The completed replacement chain reaches end of turn"),
		Engine->GetSnapshot().GetPhase(), EBattlePhase::EndOfTurn);
	TestEqual(TEXT("Deterministic hazards consume no RNG"),
		Engine->ExportRandomTrace().Num(), 0);

	TUniquePtr<FBattleEngine> ShiftEngine;
	TestTrue(TEXT("The Shift hazard engine is created with Shift enabled"),
		TryCreateEngine(70747, ShiftEngine, true));
	if (!ShiftEngine.IsValid())
	{
		return false;
	}
	const FBattlerId OpponentId =
		MakeNumericId<FBattlerId>(OpponentBattlerValue);
	TestTrue(TEXT("The opponent is placed in range of the Shift-opening KO"),
		FBattleC07DEngineFixture::SetCurrentHP(*ShiftEngine, OpponentId, 1));
	TestTrue(TEXT("The Shift incoming reserve is placed in lethal Spikes range"),
		FBattleC07DEngineFixture::SetCurrentHP(*ShiftEngine, ReserveId, 10));
	TestTrue(TEXT("Spikes is seeded on the Shift user's side"),
		FBattleC07DEngineFixture::SeedCondition(
			*ShiftEngine,
			FBattleFieldSideConditionRules::GetSpikesId(),
			EBattleSide::Player,
			OpponentId,
			TOptional<int32>(),
			1));
	TestTrue(TEXT("The Shift-opening fight turn is locked"),
		LockSingleFightTurn(
			*ShiftEngine,
			MakeDefinitionId<FMoveId>(MoveName)));
	TestTrue(TEXT("The Shift-opening turn reaches the replacement checkpoint"),
		FinishLockedQueue(*ShiftEngine));
	const TArray<FBattleDecisionRequest> ShiftRequests =
		ShiftEngine->GetPendingDecisionRequests();
	TestTrue(TEXT("The opponent KO exposes exactly one Shift response"),
		ShiftRequests.Num() == 1
			&& ShiftRequests[0].GetRequestKind()
				== EBattleDecisionRequestKind::ShiftResponse);
	if (ShiftRequests.Num() != 1)
	{
		return false;
	}
	FBattleDecision ShiftDecision;
	TestTrue(TEXT("The Shift switch decision is created"),
		FBattleDecision::TryCreateShiftSwitch(
			ShiftRequests[0].GetStateVersion(),
			ShiftRequests[0].GetDecisionOwnerTrainerId(),
			ShiftRequests[0].GetActingBattlerId(),
			MakePartySlotId(1),
			ShiftRequests[0].GetActingSlotId(),
			ShiftDecision));
	const FBattleResolution ShiftResolution =
		ShiftEngine->SubmitDecision(ShiftDecision);
	TestTrue(TEXT("The Shift response resolves"), ShiftResolution.WasAccepted());
	TestTrue(TEXT("Shift entry hazards can faint the incoming reserve"),
		FBattleC07DEngineFixture::IsFainted(*ShiftEngine, ReserveId));
	TestTrue(TEXT("The Shift hazard event identifies Spikes and its setter"),
		ShiftResolution.GetEvents().ContainsByPredicate(
			[ReserveId, OpponentId](const FBattleEvent& Event)
			{
				return Event.GetType() == EBattleEventType::Damage
					&& Event.GetSource().DefinitionId
						== FBattleFieldSideConditionRules::GetSpikesId().GetDefinitionId()
					&& Event.GetSource().BattlerId == OpponentId
					&& Event.GetTargets().ContainsByPredicate(
						[ReserveId](const FBattleEventTarget& Target)
						{
							return Target.BattlerId == ReserveId;
						});
			}));
	TestEqual(TEXT("A lethal Shift entry rescans both newly empty slots"),
		FBattleC07DEngineFixture::GetPendingReplacementCount(*ShiftEngine), 2);
	const TArray<FBattleDecisionRequest> PostShiftRequests =
		ShiftEngine->GetPendingDecisionRequests();
	TestTrue(TEXT("The player empty slot is requested before opponent replacement"),
		PostShiftRequests.Num() == 1
			&& PostShiftRequests[0].GetRequestKind()
				== EBattleDecisionRequestKind::MandatoryReplacement
			&& PostShiftRequests[0].GetDecisionOwnerTrainerId()
				== MakeNumericId<FTrainerId>(PlayerTrainerValue)
			&& PostShiftRequests[0].GetActingSlotId()
				== MakeActiveSlotId(EBattleSide::Player, EBattlePosition::Left));

	TUniquePtr<FBattleEngine> PivotEngine;
	TestTrue(TEXT("The pivot hazard engine is created"),
		TryCreateEngine(70749, PivotEngine));
	if (!PivotEngine.IsValid())
	{
		return false;
	}
	TestTrue(TEXT("The pivot incoming reserve is placed in lethal Spikes range"),
		FBattleC07DEngineFixture::SetCurrentHP(*PivotEngine, ReserveId, 10));
	TestTrue(TEXT("Spikes is seeded on the pivot user's side"),
		FBattleC07DEngineFixture::SeedCondition(
			*PivotEngine,
			FBattleFieldSideConditionRules::GetSpikesId(),
			EBattleSide::Player,
			OpponentId,
			TOptional<int32>(),
			1));
	TestTrue(TEXT("The pivot fight turn is locked"),
		LockSingleFightTurn(
			*PivotEngine,
			MakeDefinitionId<FMoveId>(PivotSwitchMoveName)));
	TestTrue(TEXT("The pivot action reaches effect execution"),
		AdvanceCurrentFightToEffects(*PivotEngine));
	const FBattleResolution PivotEffects = PivotEngine->ExecuteCurrentMoveEffects();
	TestTrue(TEXT("The pivot effect reaches its decision checkpoint"),
		PivotEffects.WasAccepted());
	const TArray<FBattleDecisionRequest> PivotRequests =
		PivotEngine->GetPendingDecisionRequests();
	TestTrue(TEXT("The pivot switch request is exposed"),
		PivotRequests.Num() == 1
			&& PivotRequests[0].GetRequestKind()
				== EBattleDecisionRequestKind::PivotSwitch);
	if (PivotRequests.Num() != 1)
	{
		return false;
	}
	FBattleDecision PivotDecision;
	TestTrue(TEXT("The pivot switch decision is created"),
		FBattleDecision::TryCreateSwitch(
			PivotRequests[0].GetStateVersion(),
			PivotRequests[0].GetRequestKind(),
			PivotRequests[0].GetDecisionOwnerTrainerId(),
			PivotRequests[0].GetActingBattlerId(),
			MakePartySlotId(1),
			PivotRequests[0].GetActingSlotId(),
			PivotDecision));
	const FBattleResolution PivotResolution =
		PivotEngine->SubmitDecision(PivotDecision);
	TestTrue(TEXT("The pivot switch resolves"), PivotResolution.WasAccepted());
	TestTrue(TEXT("Pivot entry hazards can faint the incoming reserve"),
		FBattleC07DEngineFixture::IsFainted(*PivotEngine, ReserveId));
	TestTrue(TEXT("The pivot hazard event identifies Spikes and its setter"),
		PivotResolution.GetEvents().ContainsByPredicate(
			[ReserveId, OpponentId](const FBattleEvent& Event)
			{
				return Event.GetType() == EBattleEventType::Damage
					&& Event.GetSource().DefinitionId
						== FBattleFieldSideConditionRules::GetSpikesId().GetDefinitionId()
					&& Event.GetSource().BattlerId == OpponentId
					&& Event.GetTargets().ContainsByPredicate(
						[ReserveId](const FBattleEventTarget& Target)
						{
							return Target.BattlerId == ReserveId;
						});
			}));

	TUniquePtr<FBattleEngine> SelfOwnedHazardEngine;
	TestTrue(TEXT("The self-owned hazard source engine is created"),
		TryCreateEngine(70750, SelfOwnedHazardEngine));
	if (!SelfOwnedHazardEngine.IsValid())
	{
		return false;
	}
	TestTrue(TEXT("Safeguard is seeded on the switching side"),
		FBattleC07DEngineFixture::SeedCondition(
			*SelfOwnedHazardEngine,
			FBattleFieldSideConditionRules::GetSafeguardId(),
			EBattleSide::Player,
			PlayerId,
			TOptional<int32>(5)));
	TestTrue(TEXT("Mist is seeded on the switching side"),
		FBattleC07DEngineFixture::SeedCondition(
			*SelfOwnedHazardEngine,
			FBattleFieldSideConditionRules::GetMistId(),
			EBattleSide::Player,
			PlayerId,
			TOptional<int32>(5)));
	TestTrue(TEXT("Self-owned Toxic Spikes is seeded on the switching side"),
		FBattleC07DEngineFixture::SeedCondition(
			*SelfOwnedHazardEngine,
			FBattleFieldSideConditionRules::GetToxicSpikesId(),
			EBattleSide::Player,
			PlayerId,
			TOptional<int32>(),
			1));
	TestTrue(TEXT("Self-owned Sticky Web is seeded on the switching side"),
		FBattleC07DEngineFixture::SeedCondition(
			*SelfOwnedHazardEngine,
			FBattleFieldSideConditionRules::GetStickyWebId(),
			EBattleSide::Player,
			PlayerId,
			TOptional<int32>(),
			1));
	TestTrue(TEXT("The self-owned hazard switch is locked"),
		FBattleC07DEngineFixture::PrepareLockedSwitch(
			*SelfOwnedHazardEngine,
			PlayerId,
			MakePartySlotId(1)));
	TestTrue(TEXT("The self-owned hazard switch starts"),
		SelfOwnedHazardEngine->BeginNextLockedAction().WasAccepted());
	TestTrue(TEXT("The self-owned hazard switch resolves"),
		SelfOwnedHazardEngine->ExecuteCurrentSwitch().WasAccepted());
	TestEqual(TEXT("Safeguard does not block a self-owned Toxic Spikes source"),
		FBattleC07DEngineFixture::GetMajorStatus(*SelfOwnedHazardEngine, ReserveId),
		FBattleMajorStatusRules::GetPoisonId());
	TestEqual(TEXT("Mist does not block a self-owned Sticky Web source"),
		FBattleC07DEngineFixture::GetStage(
			*SelfOwnedHazardEngine,
			ReserveId,
			EBattleStat::Speed),
		-1);

	TUniquePtr<FBattleEngine> SuppressedInteractionEngine;
	TestTrue(TEXT("The suppressed hazard-interaction engine is created"),
		TryCreateEngine(70754, SuppressedInteractionEngine));
	if (!SuppressedInteractionEngine.IsValid())
	{
		return false;
	}
	TestTrue(TEXT("Safeguard is seeded for suppression proof"),
		FBattleC07DEngineFixture::SeedCondition(
			*SuppressedInteractionEngine,
			FBattleFieldSideConditionRules::GetSafeguardId(),
			EBattleSide::Player,
			PlayerId,
			TOptional<int32>(5)));
	TestTrue(TEXT("Mist is seeded for suppression proof"),
		FBattleC07DEngineFixture::SeedCondition(
			*SuppressedInteractionEngine,
			FBattleFieldSideConditionRules::GetMistId(),
			EBattleSide::Player,
			PlayerId,
			TOptional<int32>(5)));
	TestTrue(TEXT("Spikes is seeded for suppression proof"),
		FBattleC07DEngineFixture::SeedCondition(
			*SuppressedInteractionEngine,
			FBattleFieldSideConditionRules::GetSpikesId(),
			EBattleSide::Player,
			OpponentId,
			TOptional<int32>(),
			1));
	TestTrue(TEXT("Toxic Spikes is seeded for suppression proof"),
		FBattleC07DEngineFixture::SeedCondition(
			*SuppressedInteractionEngine,
			FBattleFieldSideConditionRules::GetToxicSpikesId(),
			EBattleSide::Player,
			OpponentId,
			TOptional<int32>(),
			1));
	TestTrue(TEXT("Sticky Web is seeded for suppression proof"),
		FBattleC07DEngineFixture::SeedCondition(
			*SuppressedInteractionEngine,
			FBattleFieldSideConditionRules::GetStickyWebId(),
			EBattleSide::Player,
			OpponentId,
			TOptional<int32>(),
			1));
	TestTrue(TEXT("The Spikes switch-in trigger is suppressed in place"),
		FBattleC07DEngineFixture::SetConditionPhaseSuppressed(
			*SuppressedInteractionEngine,
			FBattleFieldSideConditionRules::GetSpikesId(),
			TOptional<EBattleSide>(EBattleSide::Player),
			EBattleTriggerPhase::SwitchIn,
			true));
	TestTrue(TEXT("The Safeguard prevention trigger is suppressed in place"),
		FBattleC07DEngineFixture::SetConditionPhaseSuppressed(
			*SuppressedInteractionEngine,
			FBattleFieldSideConditionRules::GetSafeguardId(),
			TOptional<EBattleSide>(EBattleSide::Player),
			EBattleTriggerPhase::BeforeHit,
			true));
	TestTrue(TEXT("The Mist prevention trigger is suppressed in place"),
		FBattleC07DEngineFixture::SetConditionPhaseSuppressed(
			*SuppressedInteractionEngine,
			FBattleFieldSideConditionRules::GetMistId(),
			TOptional<EBattleSide>(EBattleSide::Player),
			EBattleTriggerPhase::BeforeHit,
			true));
	TestTrue(TEXT("The suppressed-interaction switch is locked"),
		FBattleC07DEngineFixture::PrepareLockedSwitch(
			*SuppressedInteractionEngine,
			PlayerId,
			MakePartySlotId(1)));
	TestTrue(TEXT("The suppressed-interaction switch starts"),
		SuppressedInteractionEngine->BeginNextLockedAction().WasAccepted());
	TestTrue(TEXT("The suppressed-interaction switch resolves"),
		SuppressedInteractionEngine->ExecuteCurrentSwitch().WasAccepted());
	TestEqual(TEXT("Suppressed Spikes deals no switch-in damage"),
		FBattleC07DEngineFixture::GetCurrentHP(
			*SuppressedInteractionEngine, ReserveId),
		160);
	TestEqual(TEXT("Suppressed Safeguard does not block opposing Toxic Spikes"),
		FBattleC07DEngineFixture::GetMajorStatus(
			*SuppressedInteractionEngine, ReserveId),
		FBattleMajorStatusRules::GetPoisonId());
	TestEqual(TEXT("Suppressed Mist does not block opposing Sticky Web"),
		FBattleC07DEngineFixture::GetStage(
			*SuppressedInteractionEngine,
			ReserveId,
			EBattleStat::Speed),
		-1);
	const FBattleSnapshot SuppressedInteractionSnapshot =
		SuppressedInteractionEngine->GetSnapshot();
	const FBattleObservedSide* SuppressedPlayerSide =
		SuppressedInteractionSnapshot.GetObservedSides().FindByPredicate(
			[](const FBattleObservedSide& Side)
			{
				return Side.Side == EBattleSide::Player;
			});
	TestTrue(TEXT("Suppressed Spikes remains in the public side snapshot"),
		SuppressedPlayerSide != nullptr
			&& SuppressedPlayerSide->Hazards.ContainsByPredicate(
				[](const FBattleObservedCondition& Condition)
				{
					return Condition.ConditionId
						== FBattleFieldSideConditionRules::GetSpikesId();
				}));

	TUniquePtr<FBattleEngine> ForcedEngine;
	TestTrue(TEXT("The forced-switch hazard engine is created"),
		TryCreateEngine(70745, ForcedEngine));
	if (!ForcedEngine.IsValid())
	{
		return false;
	}
	TestTrue(TEXT("Spikes is seeded on the forced target's side"),
		FBattleC07DEngineFixture::SeedCondition(
			*ForcedEngine,
			FBattleFieldSideConditionRules::GetSpikesId(),
			EBattleSide::Opponent,
			PlayerId,
			TOptional<int32>(),
			1));
	FBattleResolvedTarget ForcedTarget;
	TestTrue(TEXT("The active opponent target is resolved"),
		FBattleC07DEngineFixture::TryMakeBattlerTarget(
			*ForcedEngine,
			MakeNumericId<FBattlerId>(OpponentBattlerValue),
			ForcedTarget));
	FBattleEffectExecutionResult ForcedResult;
	TestTrue(TEXT("The live forced-switch descriptor executes"),
		FBattleC07DEngineFixture::ExecuteCatalogMove(
			*ForcedEngine,
			PlayerId,
			MakeDefinitionId<FMoveId>(ForcedSwitchMoveName),
			ForcedTarget,
			ForcedResult,
			707450));
	const FBattlerId OpponentReserveId =
		MakeNumericId<FBattlerId>(OpponentReserveValue);
	TestTrue(TEXT("The forced-switch intent selects and applies the only reserve"),
		ForcedResult.SwitchIntents.Num() == 1
			&& ForcedResult.SwitchIntents[0].bApplied
			&& ForcedResult.SwitchIntents[0].IncomingBattlerId == OpponentReserveId);
	TestEqual(TEXT("The forced route installs the reserve in the active slot"),
		FBattleC07DEngineFixture::GetActiveBattler(
			*ForcedEngine, EBattleSide::Opponent),
		OpponentReserveId);
	TestEqual(TEXT("The forced route applies entry hazards after installation"),
		FBattleC07DEngineFixture::GetCurrentHP(*ForcedEngine, OpponentReserveId), 140);
	TestTrue(TEXT("The forced route exposes its hazard damage event"),
		ForcedResult.Events.ContainsByPredicate(
			[OpponentReserveId, PlayerId](const FBattleEffectExecutionEvent& Event)
			{
				return Event.Type == EBattleEventType::Damage
					&& Event.SourceOverride.IsSet()
					&& Event.SourceOverride.GetValue().DefinitionId
						== FBattleFieldSideConditionRules::GetSpikesId().GetDefinitionId()
					&& Event.SourceOverride.GetValue().BattlerId == PlayerId
					&& Event.Targets.ContainsByPredicate(
						[OpponentReserveId](const FBattleEventTarget& Target)
						{
							return Target.BattlerId == OpponentReserveId;
						});
			}));
	const TArray<FBattleRandomDraw> ForcedTrace = ForcedEngine->ExportRandomTrace();
	TestTrue(TEXT("Forced reserve selection consumes its one semantic RNG draw"),
		ForcedTrace.Num() == 1
			&& ForcedTrace[0].RulePurpose
				== FBattleSwitchResolver::GetForcedSelectionRulePurpose());

	TUniquePtr<FBattleEngine> SuppressedForcedEngine;
	TestTrue(TEXT("The suppressed forced-switch engine is created"),
		TryCreateEngine(70755, SuppressedForcedEngine));
	if (!SuppressedForcedEngine.IsValid())
	{
		return false;
	}
	TestTrue(TEXT("Spikes is seeded on the suppressed forced target side"),
		FBattleC07DEngineFixture::SeedCondition(
			*SuppressedForcedEngine,
			FBattleFieldSideConditionRules::GetSpikesId(),
			EBattleSide::Opponent,
			PlayerId,
			TOptional<int32>(),
			1));
	TestTrue(TEXT("The forced-path Spikes trigger is suppressed in place"),
		FBattleC07DEngineFixture::SetConditionPhaseSuppressed(
			*SuppressedForcedEngine,
			FBattleFieldSideConditionRules::GetSpikesId(),
			TOptional<EBattleSide>(EBattleSide::Opponent),
			EBattleTriggerPhase::SwitchIn,
			true));
	FBattleResolvedTarget SuppressedForcedTarget;
	TestTrue(TEXT("The suppressed forced-switch target resolves"),
		FBattleC07DEngineFixture::TryMakeBattlerTarget(
			*SuppressedForcedEngine,
			MakeNumericId<FBattlerId>(OpponentBattlerValue),
			SuppressedForcedTarget));
	FBattleEffectExecutionResult SuppressedForcedResult;
	TestTrue(TEXT("The suppressed forced-switch descriptor executes"),
		FBattleC07DEngineFixture::ExecuteCatalogMove(
			*SuppressedForcedEngine,
			PlayerId,
			MakeDefinitionId<FMoveId>(ForcedSwitchMoveName),
			SuppressedForcedTarget,
			SuppressedForcedResult,
			707550));
	TestEqual(TEXT("Suppressed Spikes deals no forced switch-in damage"),
		FBattleC07DEngineFixture::GetCurrentHP(
			*SuppressedForcedEngine, OpponentReserveId),
		160);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC07DScreensSingleDoubleBypassAuroraTest,
	"PokemonSolarus.Battle.C07D.Screens.SingleDoubleBypassAurora",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC07DScreensSingleDoubleBypassAuroraTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	int32 Modifier = 0;
	TArray<FConditionId> Conditions = {FBattleFieldSideConditionRules::GetReflectId()};
	TestTrue(TEXT("Single Reflect resolves"),
		FBattleFieldSideConditionRules::TryGetScreenDamageModifierQ12(
			Conditions, EBattleMoveCategory::Physical, false, false, false, Modifier));
	TestEqual(TEXT("Single Reflect uses the exact one-half modifier"), Modifier, 2048);
	TestTrue(TEXT("Reflect leaves special damage neutral"),
		FBattleFieldSideConditionRules::TryGetScreenDamageModifierQ12(
			Conditions, EBattleMoveCategory::Special, false, false, false, Modifier));
	TestEqual(TEXT("Reflect does not cover special damage"), Modifier, 4096);
	Conditions.Add(BattleTest::MakeDefinitionId<FConditionId>(
		TEXT("Condition.C07D.GenericSide")));
	TestTrue(TEXT("Unrelated valid side conditions are ignored by screen rules"),
		FBattleFieldSideConditionRules::TryGetScreenDamageModifierQ12(
			Conditions, EBattleMoveCategory::Physical, false, false, false, Modifier));
	TestEqual(TEXT("An unrelated side condition does not change Reflect"), Modifier, 2048);

	Conditions = {FBattleFieldSideConditionRules::GetLightScreenId()};
	TestTrue(TEXT("Double Light Screen resolves"),
		FBattleFieldSideConditionRules::TryGetScreenDamageModifierQ12(
			Conditions, EBattleMoveCategory::Special, true, false, false, Modifier));
	TestEqual(TEXT("Double Light Screen uses the exact two-thirds modifier"), Modifier, 2732);
	TestTrue(TEXT("Critical screen bypass resolves"),
		FBattleFieldSideConditionRules::TryGetScreenDamageModifierQ12(
			Conditions, EBattleMoveCategory::Special, true, true, false, Modifier));
	TestEqual(TEXT("Critical hits ignore screens"), Modifier, 4096);
	TestTrue(TEXT("Explicit infiltration resolves"),
		FBattleFieldSideConditionRules::TryGetScreenDamageModifierQ12(
			Conditions, EBattleMoveCategory::Special, true, false, true, Modifier));
	TestEqual(TEXT("Explicit infiltration ignores screens"), Modifier, 4096);

	Conditions = {
		FBattleFieldSideConditionRules::GetReflectId(),
		FBattleFieldSideConditionRules::GetAuroraVeilId()
	};
	TestTrue(TEXT("Reflect and Aurora Veil coexistence resolves"),
		FBattleFieldSideConditionRules::TryGetScreenDamageModifierQ12(
			Conditions, EBattleMoveCategory::Physical, false, false, false, Modifier));
	TestEqual(TEXT("Aurora Veil and Reflect never double-stack"), Modifier, 2048);
	TestTrue(TEXT("Aurora Veil also covers special damage"),
		FBattleFieldSideConditionRules::TryGetScreenDamageModifierQ12(
			Conditions, EBattleMoveCategory::Special, false, false, false, Modifier));
	TestEqual(TEXT("Aurora Veil uses the single-battle modifier"), Modifier, 2048);

	FBattleFieldSideApplicationFacts Application;
	Application.RequestedConditionId = FBattleFieldSideConditionRules::GetAuroraVeilId();
	FBattleFieldSideApplicationResult ApplicationResult;
	TestTrue(TEXT("Aurora Veil without Snow is evaluated"),
		FBattleFieldSideConditionRules::TryEvaluateApplication(
			Application, ApplicationResult));
	TestEqual(TEXT("Aurora Veil requires Snow when created"), ApplicationResult.Outcome,
		EBattleFieldSideApplicationOutcome::ActivationRequirementFailed);
	Application.bSnowActive = true;
	TestTrue(TEXT("Aurora Veil in Snow is evaluated"),
		FBattleFieldSideConditionRules::TryEvaluateApplication(
			Application, ApplicationResult));
	TestEqual(TEXT("Aurora Veil can be created in Snow"), ApplicationResult.Outcome,
		EBattleFieldSideApplicationOutcome::Create);
	Application.bRequestedAlreadyActive = true;
	TestTrue(TEXT("An already-active Aurora Veil request is evaluated"),
		FBattleFieldSideConditionRules::TryEvaluateApplication(
			Application, ApplicationResult));
	TestEqual(TEXT("An active screen is not refreshed"), ApplicationResult.Outcome,
		EBattleFieldSideApplicationOutcome::AlreadyActive);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC07DRoomsToggleItemsDefensiveQueriesTest,
	"PokemonSolarus.Battle.C07D.Rooms.ToggleItemsDefensiveQueries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC07DRoomsToggleItemsDefensiveQueriesTest::RunTest(
	const FString& Parameters)
{
	using namespace BattleFieldSideConditionTests;
	(void)Parameters;
	FBattleFieldSideApplicationFacts Application;
	Application.RequestedConditionId = FBattleFieldSideConditionRules::GetTrickRoomId();
	Application.bRequestedAlreadyActive = true;
	FBattleFieldSideApplicationResult ApplicationResult;
	TestTrue(TEXT("An active Trick Room request is evaluated"),
		FBattleFieldSideConditionRules::TryEvaluateApplication(
			Application, ApplicationResult));
	TestEqual(TEXT("Using the same room toggles it off"), ApplicationResult.Outcome,
		EBattleFieldSideApplicationOutcome::ToggleOff);
	Application.RequestedConditionId = FBattleFieldSideConditionRules::GetWonderRoomId();
	Application.bRequestedAlreadyActive = false;
	TestTrue(TEXT("A different room request is independently evaluated"),
		FBattleFieldSideConditionRules::TryEvaluateApplication(
			Application, ApplicationResult));
	TestEqual(TEXT("Different rooms can coexist"), ApplicationResult.Outcome,
		EBattleFieldSideApplicationOutcome::Create);

	FBattleDecision FastDecision;
	FBattleDecision SlowDecision;
	const FActiveSlotId PlayerSlot = MakeActiveSlotId(
		EBattleSide::Player, EBattlePosition::Left);
	const FActiveSlotId OpponentSlot = MakeActiveSlotId(
		EBattleSide::Opponent, EBattlePosition::Left);
	TestTrue(TEXT("The fast Fight decision is created"),
		FBattleDecision::TryCreateFight(
			1,
			MakeNumericId<FTrainerId>(PlayerTrainerValue),
			MakeNumericId<FBattlerId>(PlayerBattlerValue),
			MakeDefinitionId<FMoveId>(MoveName),
			OpponentSlot,
			FastDecision));
	TestTrue(TEXT("The slow Fight decision is created"),
		FBattleDecision::TryCreateFight(
			1,
			MakeNumericId<FTrainerId>(OpponentTrainerValue),
			MakeNumericId<FBattlerId>(OpponentBattlerValue),
			MakeDefinitionId<FMoveId>(MoveName),
			PlayerSlot,
			SlowDecision));
	FBattleActionOrderCandidate Fast;
	Fast.ActionId = MakeNumericId<FActionId>(1);
	Fast.Decision = FastDecision;
	Fast.OrderKey.CommandBand = EBattleActionCommandBand::Move;
	Fast.OrderKey.EffectiveSpeed = 120;
	Fast.OrderKey.ActingSlotId = PlayerSlot;
	Fast.TargetClass = EBattleTargetClass::SelectedOpponent;
	Fast.SelectedTargetBattlerId = MakeNumericId<FBattlerId>(OpponentBattlerValue);
	FBattleActionOrderCandidate Slow;
	Slow.ActionId = MakeNumericId<FActionId>(2);
	Slow.Decision = SlowDecision;
	Slow.OrderKey.CommandBand = EBattleActionCommandBand::Move;
	Slow.OrderKey.EffectiveSpeed = 60;
	Slow.OrderKey.ActingSlotId = OpponentSlot;
	Slow.TargetClass = EBattleTargetClass::SelectedOpponent;
	Slow.SelectedTargetBattlerId = MakeNumericId<FBattlerId>(PlayerBattlerValue);
	FBattleActionQueueLockSpec QueueSpec;
	QueueSpec.BattleId = MakeNumericId<FBattleId>(70742);
	QueueSpec.TurnId = MakeNumericId<FTurnId>(1);
	QueueSpec.ResolutionId = MakeNumericId<FResolutionId>(1);
	QueueSpec.Candidates = {Fast, Slow};
	FSeededBattleRandom Random(70742);
	TArray<FBattleLockedAction> Queue;
	EBattleActionQueueError QueueError = EBattleActionQueueError::None;
	TestTrue(TEXT("Ordinary speed order locks"),
		FBattleActionQueueResolver::TryLock(QueueSpec, Random, Queue, QueueError));
	TestEqual(TEXT("The faster action leads outside Trick Room"),
		Queue[0].ActionId, Fast.ActionId);
	QueueSpec.bReverseSpeed =
		FBattleFieldSideConditionRules::ShouldReverseSpeedOrder(true);
	TestTrue(TEXT("Trick Room speed order locks"),
		FBattleActionQueueResolver::TryLock(QueueSpec, Random, Queue, QueueError));
	TestEqual(TEXT("Trick Room reverses speed inside equal priority"),
		Queue[0].ActionId, Slow.ActionId);

	TestTrue(TEXT("Magic Room suppresses held-item effects"),
		FBattleFieldSideConditionRules::ShouldSuppressHeldItemEffects(true));
	FBattleHeldItemState HeldItem;
	HeldItem.OriginalItemId = MakeDefinitionId<FItemId>(TEXT("Item.C07D.RoomProof"));
	HeldItem.CurrentItemId = HeldItem.OriginalItemId;
	TestFalse(TEXT("Held-item effects return when Magic Room is absent"),
		FBattleFieldSideConditionRules::ShouldSuppressHeldItemEffects(false));
	TestTrue(TEXT("Magic Room never deletes or consumes the held item"),
		HeldItem.CurrentItemId == HeldItem.OriginalItemId && !HeldItem.bConsumed);

	TUniquePtr<FBattleEngine> MagicRoomEngine;
	TestTrue(TEXT("The live Magic Room engine is created"),
		TryCreateEngine(70746, MagicRoomEngine));
	if (!MagicRoomEngine.IsValid())
	{
		return false;
	}
	const FBattlerId MagicRoomUser =
		MakeNumericId<FBattlerId>(PlayerBattlerValue);
	const FItemId OriginalItem =
		FBattleC07DEngineFixture::GetCurrentHeldItem(*MagicRoomEngine, MagicRoomUser);
	TestEqual(TEXT("The Magic Room proof begins with a real held item"),
		OriginalItem, MakeDefinitionId<FItemId>(HeldItemName));
	FBattleEffectExecutionResult SetMagicResult;
	TestTrue(TEXT("Magic Room is applied through live effect execution"),
		FBattleC07DEngineFixture::ExecuteCatalogMove(
			*MagicRoomEngine,
			MagicRoomUser,
			MakeDefinitionId<FMoveId>(SetMagicRoomMoveName),
			FBattleResolvedTarget::CreateField(),
			SetMagicResult,
			707460));
	TestTrue(TEXT("Live Magic Room suppresses the held item"),
		FBattleC07DEngineFixture::IsHeldItemSuppressed(
			*MagicRoomEngine, MagicRoomUser));
	TestTrue(TEXT("Live Magic Room registers its shared triggers"),
		FBattleC07DEngineFixture::GetActiveTriggerCount(
			*MagicRoomEngine,
			FBattleFieldSideConditionRules::GetMagicRoomId()) > 0);
	TestTrue(TEXT("Magic Room suppression neither deletes nor consumes the held item"),
		FBattleC07DEngineFixture::GetCurrentHeldItem(
			*MagicRoomEngine, MagicRoomUser) == OriginalItem
			&& !FBattleC07DEngineFixture::IsHeldItemConsumed(
				*MagicRoomEngine, MagicRoomUser));
	TestTrue(TEXT("The live room is publicly projected"),
		MagicRoomEngine->GetSnapshot().GetRooms().ContainsByPredicate(
			[](const FBattleObservedCondition& Condition)
			{
				return Condition.ConditionId
					== FBattleFieldSideConditionRules::GetMagicRoomId();
			}));
	FBattleEffectExecutionResult RemoveMagicResult;
	TestTrue(TEXT("Magic Room is removed through live effect execution"),
		FBattleC07DEngineFixture::ExecuteCatalogMove(
			*MagicRoomEngine,
			MagicRoomUser,
			MakeDefinitionId<FMoveId>(RemoveMagicRoomMoveName),
			FBattleResolvedTarget::CreateField(),
			RemoveMagicResult,
			707461));
	TestFalse(TEXT("Removing Magic Room restores held-item eligibility"),
		FBattleC07DEngineFixture::IsHeldItemSuppressed(
			*MagicRoomEngine, MagicRoomUser));
	TestEqual(TEXT("Removing Magic Room cleans every matching trigger"),
		FBattleC07DEngineFixture::GetActiveTriggerCount(
			*MagicRoomEngine,
			FBattleFieldSideConditionRules::GetMagicRoomId()),
		0);
	TestTrue(TEXT("Restoration preserves the same unconsumed held item"),
		FBattleC07DEngineFixture::GetCurrentHeldItem(
			*MagicRoomEngine, MagicRoomUser) == OriginalItem
			&& !FBattleC07DEngineFixture::IsHeldItemConsumed(
				*MagicRoomEngine, MagicRoomUser));
	TestFalse(TEXT("Removed Magic Room leaves no room snapshot"),
		MagicRoomEngine->GetSnapshot().GetRooms().ContainsByPredicate(
			[](const FBattleObservedCondition& Condition)
			{
				return Condition.ConditionId
					== FBattleFieldSideConditionRules::GetMagicRoomId();
			}));

	TUniquePtr<FBattleEngine> SuppressedMagicRoomEngine;
	TestTrue(TEXT("The suppressed Magic Room engine is created"),
		TryCreateEngine(70758, SuppressedMagicRoomEngine));
	if (!SuppressedMagicRoomEngine.IsValid())
	{
		return false;
	}
	FBattleEffectExecutionResult SetSuppressedMagicResult;
	TestTrue(TEXT("Magic Room is applied before its trigger is suppressed"),
		FBattleC07DEngineFixture::ExecuteCatalogMove(
			*SuppressedMagicRoomEngine,
			MagicRoomUser,
			MakeDefinitionId<FMoveId>(SetMagicRoomMoveName),
			FBattleResolvedTarget::CreateField(),
			SetSuppressedMagicResult,
			707580));
	TestTrue(TEXT("Magic Room initially suppresses the live held item"),
		FBattleC07DEngineFixture::IsHeldItemSuppressed(
			*SuppressedMagicRoomEngine, MagicRoomUser));
	TestTrue(TEXT("Magic Room's item trigger is suppressed in place"),
		FBattleC07DEngineFixture::SetConditionPhaseSuppressed(
			*SuppressedMagicRoomEngine,
			FBattleFieldSideConditionRules::GetMagicRoomId(),
			TOptional<EBattleSide>(),
			EBattleTriggerPhase::BeforeAction,
			true));
	TestTrue(TEXT("An action is locked for the suppressed Magic Room proof"),
		FBattleC07DEngineFixture::PrepareLockedSwitch(
			*SuppressedMagicRoomEngine,
			MagicRoomUser,
			MakePartySlotId(1)));
	TestTrue(TEXT("The suppressed Magic Room action starts"),
		SuppressedMagicRoomEngine->BeginNextLockedAction().WasAccepted());
	TestFalse(TEXT("A suppressed Magic Room trigger contributes no item suppression"),
		FBattleC07DEngineFixture::IsHeldItemSuppressed(
			*SuppressedMagicRoomEngine, MagicRoomUser));
	TestTrue(TEXT("Suppressing its item trigger does not remove Magic Room"),
		SuppressedMagicRoomEngine->GetSnapshot().GetRooms().ContainsByPredicate(
			[](const FBattleObservedCondition& Condition)
			{
				return Condition.ConditionId
					== FBattleFieldSideConditionRules::GetMagicRoomId();
			}));
	FBattleStatStages Stages;
	Stages.ApplyChange(EBattleStat::Defense, 2);
	Stages.ApplyChange(EBattleStat::SpecialDefense, -1);
	int32 DefenseStageBefore = 0;
	int32 SpecialDefenseStageBefore = 0;
	TestTrue(TEXT("Defensive proof stages are readable"),
		Stages.TryGetStage(EBattleStat::Defense, DefenseStageBefore)
			&& Stages.TryGetStage(EBattleStat::SpecialDefense, SpecialDefenseStageBefore));
	TestEqual(TEXT("Wonder Room maps Defense queries to Special Defense"),
		FBattleFieldSideConditionRules::ResolveWonderRoomDefensiveStat(
			true, EBattleStat::Defense),
		EBattleStat::SpecialDefense);
	TestEqual(TEXT("Wonder Room maps Special Defense queries to Defense"),
		FBattleFieldSideConditionRules::ResolveWonderRoomDefensiveStat(
			true, EBattleStat::SpecialDefense),
		EBattleStat::Defense);
	TestEqual(TEXT("Wonder Room does not alter unrelated stat queries"),
		FBattleFieldSideConditionRules::ResolveWonderRoomDefensiveStat(
			true, EBattleStat::Attack),
		EBattleStat::Attack);
	int32 DefenseStageAfter = 0;
	int32 SpecialDefenseStageAfter = 0;
	TestTrue(TEXT("Wonder Room leaves stored defensive stages unchanged"),
		Stages.TryGetStage(EBattleStat::Defense, DefenseStageAfter)
			&& Stages.TryGetStage(EBattleStat::SpecialDefense, SpecialDefenseStageAfter)
			&& DefenseStageAfter == DefenseStageBefore
			&& SpecialDefenseStageAfter == SpecialDefenseStageBefore);
	TestEqual(TEXT("The room move owns exact priority minus seven"),
		FBattleFieldSideConditionRules::GetTrickRoomMovePriority(), -7);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC07DSideConditionsOrderStatusStagePreventionTest,
	"PokemonSolarus.Battle.C07D.SideConditions.OrderStatusStagePrevention",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC07DSideConditionsOrderStatusStagePreventionTest::RunTest(
	const FString& Parameters)
{
	using namespace BattleFieldSideConditionTests;
	(void)Parameters;
	int32 EffectiveSpeed = 0;
	TestTrue(TEXT("Paralysis applies after the ordinary Speed-stage query"),
		FBattleMajorStatusRules::TryApplySpeedModifier(
			FBattleMajorStatusRules::GetParalysisId(), 101, EffectiveSpeed));
	TestEqual(TEXT("Paralysis floors an odd Speed before Tailwind"), EffectiveSpeed, 50);
	TestTrue(TEXT("Tailwind applies to the paralysis-adjusted Speed"),
		FBattleFieldSideConditionRules::TryApplyTailwindSpeed(
			true, EffectiveSpeed, EffectiveSpeed));
	TestEqual(TEXT("Tailwind doubles after the Paralysis floor"), EffectiveSpeed, 100);

	const FActiveSlotId PlayerSlot = MakeActiveSlotId(
		EBattleSide::Player, EBattlePosition::Left);
	const FActiveSlotId OpponentSlot = MakeActiveSlotId(
		EBattleSide::Opponent, EBattlePosition::Left);
	FBattleDecision PlayerDecision;
	FBattleDecision OpponentDecision;
	TestTrue(TEXT("The combined-order player action is created"),
		FBattleDecision::TryCreateFight(
			1,
			MakeNumericId<FTrainerId>(PlayerTrainerValue),
			MakeNumericId<FBattlerId>(PlayerBattlerValue),
			MakeDefinitionId<FMoveId>(MoveName),
			OpponentSlot,
			PlayerDecision));
	TestTrue(TEXT("The combined-order opponent action is created"),
		FBattleDecision::TryCreateFight(
			1,
			MakeNumericId<FTrainerId>(OpponentTrainerValue),
			MakeNumericId<FBattlerId>(OpponentBattlerValue),
			MakeDefinitionId<FMoveId>(MoveName),
			PlayerSlot,
			OpponentDecision));
	FBattleActionOrderCandidate PlayerAction;
	PlayerAction.ActionId = MakeNumericId<FActionId>(8071);
	PlayerAction.Decision = PlayerDecision;
	PlayerAction.OrderKey.CommandBand = EBattleActionCommandBand::Move;
	PlayerAction.OrderKey.EffectiveSpeed = EffectiveSpeed;
	PlayerAction.OrderKey.ActingSlotId = PlayerSlot;
	PlayerAction.TargetClass = EBattleTargetClass::SelectedOpponent;
	PlayerAction.SelectedTargetBattlerId =
		MakeNumericId<FBattlerId>(OpponentBattlerValue);
	FBattleActionOrderCandidate OpponentAction;
	OpponentAction.ActionId = MakeNumericId<FActionId>(8072);
	OpponentAction.Decision = OpponentDecision;
	OpponentAction.OrderKey.CommandBand = EBattleActionCommandBand::Move;
	OpponentAction.OrderKey.EffectiveSpeed = 80;
	OpponentAction.OrderKey.ActingSlotId = OpponentSlot;
	OpponentAction.TargetClass = EBattleTargetClass::SelectedOpponent;
	OpponentAction.SelectedTargetBattlerId =
		MakeNumericId<FBattlerId>(PlayerBattlerValue);
	FBattleActionQueueLockSpec CombinedOrder;
	CombinedOrder.BattleId = MakeNumericId<FBattleId>(70747);
	CombinedOrder.TurnId = MakeNumericId<FTurnId>(1);
	CombinedOrder.ResolutionId = MakeNumericId<FResolutionId>(1);
	CombinedOrder.Candidates = {PlayerAction, OpponentAction};
	FSeededBattleRandom OrderRandom(70747);
	TArray<FBattleLockedAction> CombinedQueue;
	EBattleActionQueueError OrderError = EBattleActionQueueError::None;
	TestTrue(TEXT("Tailwind and Paralysis combined order locks"),
		FBattleActionQueueResolver::TryLock(
			CombinedOrder, OrderRandom, CombinedQueue, OrderError));
	TestEqual(TEXT("Tailwind's post-Paralysis Speed wins ordinary order"),
		CombinedQueue[0].ActionId, PlayerAction.ActionId);
	CombinedOrder.Candidates[1].OrderKey.FractionalPriorityTenths = 1;
	TestTrue(TEXT("Quick Claw fractional priority order locks"),
		FBattleActionQueueResolver::TryLock(
			CombinedOrder, OrderRandom, CombinedQueue, OrderError));
	TestEqual(TEXT("Quick Claw plus 0.1 outranks the faster Tailwind action"),
		CombinedQueue[0].ActionId, OpponentAction.ActionId);
	CombinedOrder.Candidates[1].OrderKey.FractionalPriorityTenths = 0;
	CombinedOrder.bReverseSpeed =
		FBattleFieldSideConditionRules::ShouldReverseSpeedOrder(true);
	TestTrue(TEXT("Trick Room combined speed order locks"),
		FBattleActionQueueResolver::TryLock(
			CombinedOrder, OrderRandom, CombinedQueue, OrderError));
	TestEqual(TEXT("Trick Room reverses the Tailwind-adjusted equal-priority speeds"),
		CombinedQueue[0].ActionId, OpponentAction.ActionId);
	CombinedOrder.Candidates[1].OrderKey.FractionalPriorityTenths = 1;
	TestTrue(TEXT("Quick Claw order under Trick Room locks"),
		FBattleActionQueueResolver::TryLock(
			CombinedOrder, OrderRandom, CombinedQueue, OrderError));
	TestEqual(TEXT("Quick Claw priority is resolved before Trick Room speed reversal"),
		CombinedQueue[0].ActionId, OpponentAction.ActionId);

	TestTrue(TEXT("Safeguard blocks an opponent's status or Confusion"),
		FBattleFieldSideConditionRules::ShouldSafeguardPrevent(true, true, false));
	TestFalse(TEXT("Safeguard does not block an ally or self"),
		FBattleFieldSideConditionRules::ShouldSafeguardPrevent(true, false, false));
	TestFalse(TEXT("Explicit infiltration bypasses Safeguard"),
		FBattleFieldSideConditionRules::ShouldSafeguardPrevent(true, true, true));
	TestTrue(TEXT("Mist blocks an opposing negative stage change"),
		FBattleFieldSideConditionRules::ShouldMistPreventStatDrop(
			true, true, false, -1));
	TestFalse(TEXT("Mist permits a positive stage change"),
		FBattleFieldSideConditionRules::ShouldMistPreventStatDrop(
			true, true, false, 1));
	TestFalse(TEXT("Mist does not block self-inflicted stage loss"),
		FBattleFieldSideConditionRules::ShouldMistPreventStatDrop(
			true, false, false, -1));
	TestFalse(TEXT("Explicit infiltration bypasses Mist"),
		FBattleFieldSideConditionRules::ShouldMistPreventStatDrop(
			true, true, true, -1));

	TUniquePtr<FBattleEngine> ActiveTailwindEngine;
	TUniquePtr<FBattleEngine> SuppressedTailwindEngine;
	const bool bTailwindEnginesCreated =
		TryCreateEngine(70756, ActiveTailwindEngine)
		&& TryCreateEngine(70757, SuppressedTailwindEngine);
	TestTrue(TEXT("The Tailwind trigger-order engines are created"),
		bTailwindEnginesCreated);
	if (!bTailwindEnginesCreated)
	{
		return false;
	}
	const FBattlerId LivePlayerId = MakeNumericId<FBattlerId>(PlayerBattlerValue);
	TestTrue(TEXT("Active Tailwind is seeded for live action order"),
		FBattleC07DEngineFixture::SeedCondition(
			*ActiveTailwindEngine,
			FBattleFieldSideConditionRules::GetTailwindId(),
			EBattleSide::Player,
			LivePlayerId,
			TOptional<int32>(4)));
	TestTrue(TEXT("Suppressed Tailwind is seeded for live action order"),
		FBattleC07DEngineFixture::SeedCondition(
			*SuppressedTailwindEngine,
			FBattleFieldSideConditionRules::GetTailwindId(),
			EBattleSide::Player,
			LivePlayerId,
			TOptional<int32>(4)));
	TestTrue(TEXT("Tailwind's action-order trigger is suppressed in place"),
		FBattleC07DEngineFixture::SetConditionPhaseSuppressed(
			*SuppressedTailwindEngine,
			FBattleFieldSideConditionRules::GetTailwindId(),
			TOptional<EBattleSide>(EBattleSide::Player),
			EBattleTriggerPhase::ActionOrderCalculation,
			true));
	TestTrue(TEXT("The active-Tailwind fight turn locks"),
		LockSingleFightTurn(
			*ActiveTailwindEngine,
			MakeDefinitionId<FMoveId>(MoveName)));
	TestTrue(TEXT("The suppressed-Tailwind fight turn locks"),
		LockSingleFightTurn(
			*SuppressedTailwindEngine,
			MakeDefinitionId<FMoveId>(MoveName)));
	TestEqual(TEXT("An emitted Tailwind trigger doubles live effective Speed"),
		FBattleC07DEngineFixture::GetLockedEffectiveSpeed(
			*ActiveTailwindEngine, LivePlayerId),
		200);
	TestEqual(TEXT("A suppressed Tailwind trigger contributes no Speed modifier"),
		FBattleC07DEngineFixture::GetLockedEffectiveSpeed(
			*SuppressedTailwindEngine, LivePlayerId),
		100);

	TOptional<int32> Duration;
	TestTrue(TEXT("Tailwind duration resolves"),
		FBattleFieldSideConditionRules::TryGetDuration(
			FBattleFieldSideConditionRules::GetTailwindId(), false, Duration));
	TestTrue(TEXT("Tailwind lasts exactly four turns"),
		Duration.IsSet() && Duration.GetValue() == 4);
	TestTrue(TEXT("Safeguard duration resolves"),
		FBattleFieldSideConditionRules::TryGetDuration(
			FBattleFieldSideConditionRules::GetSafeguardId(), false, Duration));
	TestTrue(TEXT("Safeguard lasts exactly five turns"),
		Duration.IsSet() && Duration.GetValue() == 5);
	TestTrue(TEXT("Mist duration resolves"),
		FBattleFieldSideConditionRules::TryGetDuration(
			FBattleFieldSideConditionRules::GetMistId(), false, Duration));
	TestTrue(TEXT("Mist lasts exactly five turns"),
		Duration.IsSet() && Duration.GetValue() == 5);
	FBattleFieldSideApplicationFacts DuplicateTailwind;
	DuplicateTailwind.RequestedConditionId =
		FBattleFieldSideConditionRules::GetTailwindId();
	DuplicateTailwind.bRequestedAlreadyActive = true;
	FBattleFieldSideApplicationResult DuplicateResult;
	TestTrue(TEXT("An already-active Tailwind request is evaluated"),
		FBattleFieldSideConditionRules::TryEvaluateApplication(
			DuplicateTailwind, DuplicateResult));
	TestEqual(TEXT("An active side condition is not refreshed"),
		DuplicateResult.Outcome,
		EBattleFieldSideApplicationOutcome::AlreadyActive);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC07DIntegrationDeterminismSnapshotsRngEventsTest,
	"PokemonSolarus.Battle.C07D.Integration.DeterminismSnapshotsRngEvents",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC07DIntegrationDeterminismSnapshotsRngEventsTest::RunTest(
	const FString& Parameters)
{
	using namespace BattleFieldSideConditionTests;
	(void)Parameters;
	TUniquePtr<FBattleEngine> First;
	TUniquePtr<FBattleEngine> Second;
	TestTrue(TEXT("The first deterministic engine is created"), TryCreateEngine(70743, First));
	TestTrue(TEXT("The second deterministic engine is created"), TryCreateEngine(70743, Second));
	if (!First.IsValid() || !Second.IsValid())
	{
		return false;
	}
	const FBattlerId SourceId = MakeNumericId<FBattlerId>(PlayerBattlerValue);
	auto SeedProofSet = [SourceId](FBattleEngine& Engine)
	{
		return FBattleC07DEngineFixture::SeedCondition(
				Engine,
				FBattleFieldSideConditionRules::GetSunId(),
				EBattleSide::Player,
				SourceId,
				TOptional<int32>(5))
			&& FBattleC07DEngineFixture::SeedCondition(
				Engine,
				FBattleFieldSideConditionRules::GetTrickRoomId(),
				EBattleSide::Player,
				SourceId,
				TOptional<int32>(5))
			&& FBattleC07DEngineFixture::SeedCondition(
				Engine,
				FBattleFieldSideConditionRules::GetSpikesId(),
				EBattleSide::Opponent,
				SourceId,
				TOptional<int32>(),
				2)
			&& FBattleC07DEngineFixture::SeedCondition(
				Engine,
				FBattleFieldSideConditionRules::GetTailwindId(),
				EBattleSide::Player,
				SourceId,
				TOptional<int32>(4));
	};
	TestTrue(TEXT("The first proof condition set is seeded"), SeedProofSet(*First));
	TestTrue(TEXT("The second proof condition set is seeded"), SeedProofSet(*Second));
	const FBattleSnapshot OldSnapshot = First->GetSnapshot();
	const FBattleSnapshot SecondSnapshot = Second->GetSnapshot();
	TestTrue(TEXT("Weather is publicly projected"), OldSnapshot.GetWeather().IsSet());
	if (OldSnapshot.GetWeather().IsSet())
	{
		const FBattleObservedCondition& Weather = OldSnapshot.GetWeather().GetValue();
		TestEqual(TEXT("Weather snapshot keeps the exact ID"), Weather.ConditionId,
			FBattleFieldSideConditionRules::GetSunId());
		TestTrue(TEXT("Weather snapshot keeps the exact remaining duration"),
			Weather.RemainingTurns.IsSet() && Weather.RemainingTurns.GetValue() == 5);
		TestEqual(TEXT("Weather snapshot keeps its creation ordinal"),
			Weather.CreationOrdinal, static_cast<uint64>(1));
		TestEqual(TEXT("Weather snapshot keeps its source"), Weather.SourceBattlerId, SourceId);
	}
	TestEqual(TEXT("One room is publicly projected"), OldSnapshot.GetRooms().Num(), 1);
	if (OldSnapshot.GetRooms().Num() == 1)
	{
		TestTrue(TEXT("Room snapshot keeps exact duration"),
			OldSnapshot.GetRooms()[0].RemainingTurns.IsSet()
				&& OldSnapshot.GetRooms()[0].RemainingTurns.GetValue() == 5);
		TestEqual(TEXT("Room snapshot keeps deterministic creation order"),
			OldSnapshot.GetRooms()[0].CreationOrdinal, static_cast<uint64>(2));
	}
	const FBattleObservedSide* OpponentSide = OldSnapshot.GetObservedSides().FindByPredicate(
		[](const FBattleObservedSide& Side) { return Side.Side == EBattleSide::Opponent; });
	TestTrue(TEXT("The opponent side snapshot exists"), OpponentSide != nullptr);
	if (OpponentSide != nullptr)
	{
		TestEqual(TEXT("One hazard is publicly projected"), OpponentSide->Hazards.Num(), 1);
		if (OpponentSide->Hazards.Num() == 1)
		{
			TestEqual(TEXT("Hazard snapshot keeps exact layers"),
				OpponentSide->Hazards[0].LayerCount, 2);
			TestFalse(TEXT("Persistent hazards have no duration"),
				OpponentSide->Hazards[0].RemainingTurns.IsSet());
			TestEqual(TEXT("Hazard snapshot keeps deterministic creation order"),
				OpponentSide->Hazards[0].CreationOrdinal, static_cast<uint64>(3));
		}
	}
	const FBattleObservedSide* PlayerSide = OldSnapshot.GetObservedSides().FindByPredicate(
		[](const FBattleObservedSide& Side) { return Side.Side == EBattleSide::Player; });
	TestTrue(TEXT("The player side snapshot exists"), PlayerSide != nullptr);
	if (PlayerSide != nullptr)
	{
		TestEqual(TEXT("One side condition is publicly projected"),
			PlayerSide->Conditions.Num(), 1);
		if (PlayerSide->Conditions.Num() == 1)
		{
			TestTrue(TEXT("Tailwind snapshot keeps exact duration"),
				PlayerSide->Conditions[0].RemainingTurns.IsSet()
					&& PlayerSide->Conditions[0].RemainingTurns.GetValue() == 4);
			TestEqual(TEXT("Tailwind snapshot keeps deterministic creation order"),
				PlayerSide->Conditions[0].CreationOrdinal, static_cast<uint64>(4));
		}
	}

	TestTrue(TEXT("The first deterministic switch is locked"),
		FBattleC07DEngineFixture::PrepareLockedSwitch(
			*First,
			MakeNumericId<FBattlerId>(PlayerBattlerValue),
			MakePartySlotId(1)));
	TestTrue(TEXT("The second deterministic switch is locked"),
		FBattleC07DEngineFixture::PrepareLockedSwitch(
			*Second,
			MakeNumericId<FBattlerId>(PlayerBattlerValue),
			MakePartySlotId(1)));
	const FBattleResolution FirstStart = First->BeginNextLockedAction();
	const FBattleResolution SecondStart = Second->BeginNextLockedAction();
	TestTrue(TEXT("Both deterministic switch actions start"),
		FirstStart.WasAccepted() && SecondStart.WasAccepted());
	const FBattleResolution FirstSwitch = First->ExecuteCurrentSwitch();
	const FBattleResolution SecondSwitch = Second->ExecuteCurrentSwitch();
	TestTrue(TEXT("Both deterministic switch actions resolve"),
		FirstSwitch.WasAccepted() && SecondSwitch.WasAccepted());
	TestEqual(TEXT("Identical switches emit the same event count"),
		FirstSwitch.GetEvents().Num(), SecondSwitch.GetEvents().Num());
	bool bExactEventTrace = FirstSwitch.GetEvents().Num() == SecondSwitch.GetEvents().Num();
	for (int32 Index = 0; bExactEventTrace && Index < FirstSwitch.GetEvents().Num(); ++Index)
	{
		const FBattleEvent& Left = FirstSwitch.GetEvents()[Index];
		const FBattleEvent& Right = SecondSwitch.GetEvents()[Index];
		bExactEventTrace = Left.GetEventOrdinal() == Right.GetEventOrdinal()
			&& Left.GetType() == Right.GetType()
			&& Left.GetCause() == Right.GetCause()
			&& Left.GetCauseActionKind() == Right.GetCauseActionKind()
			&& Left.GetSource().TrainerId == Right.GetSource().TrainerId
			&& Left.GetSource().BattlerId == Right.GetSource().BattlerId
			&& Left.GetSource().ActiveSlotId == Right.GetSource().ActiveSlotId
			&& Left.GetSource().DefinitionId == Right.GetSource().DefinitionId
			&& Left.GetNumericBefore() == Right.GetNumericBefore()
			&& Left.GetNumericAfter() == Right.GetNumericAfter()
			&& Left.GetNumericDelta() == Right.GetNumericDelta()
			&& Left.GetTargets().Num() == Right.GetTargets().Num();
		for (int32 TargetIndex = 0;
			bExactEventTrace && TargetIndex < Left.GetTargets().Num();
			++TargetIndex)
		{
			const FBattleEventTarget& LeftTarget = Left.GetTargets()[TargetIndex];
			const FBattleEventTarget& RightTarget = Right.GetTargets()[TargetIndex];
			bExactEventTrace = LeftTarget.TrainerId == RightTarget.TrainerId
				&& LeftTarget.BattlerId == RightTarget.BattlerId
				&& LeftTarget.ActiveSlotId == RightTarget.ActiveSlotId
				&& LeftTarget.Side == RightTarget.Side
				&& LeftTarget.bHasSide == RightTarget.bHasSide
				&& LeftTarget.bField == RightTarget.bField;
		}
	}
	TestTrue(TEXT("Identical setup and inputs emit an exact deterministic event trace"),
		bExactEventTrace);

	TestTrue(TEXT("A later condition can be seeded after taking a snapshot"),
		FBattleC07DEngineFixture::SeedCondition(
			*First,
			FBattleFieldSideConditionRules::GetMistId(),
			EBattleSide::Player,
			SourceId,
			TOptional<int32>(5)));
	const FBattleSnapshot NewSnapshot = First->GetSnapshot();
	const FBattleObservedSide* OldPlayerSide = OldSnapshot.GetObservedSides().FindByPredicate(
		[](const FBattleObservedSide& Side) { return Side.Side == EBattleSide::Player; });
	const FBattleObservedSide* NewPlayerSide = NewSnapshot.GetObservedSides().FindByPredicate(
		[](const FBattleObservedSide& Side) { return Side.Side == EBattleSide::Player; });
	TestTrue(TEXT("Old snapshots remain immutable after later state changes"),
		OldPlayerSide != nullptr && OldPlayerSide->Conditions.Num() == 1);
	TestTrue(TEXT("New snapshots include the later condition"),
		NewPlayerSide != nullptr && NewPlayerSide->Conditions.Num() == 2);
	TestTrue(TEXT("Identical setup and mutation order produce identical weather snapshots"),
		SecondSnapshot.GetWeather().IsSet()
			&& OldSnapshot.GetWeather().IsSet()
			&& SecondSnapshot.GetWeather().GetValue().ConditionId
				== OldSnapshot.GetWeather().GetValue().ConditionId
			&& SecondSnapshot.GetWeather().GetValue().RemainingTurns
				== OldSnapshot.GetWeather().GetValue().RemainingTurns
			&& SecondSnapshot.GetWeather().GetValue().CreationOrdinal
				== OldSnapshot.GetWeather().GetValue().CreationOrdinal);
	TestEqual(TEXT("Deterministic condition operations consume no RNG in the first engine"),
		First->ExportRandomTrace().Num(), 0);
	TestEqual(TEXT("Deterministic condition operations consume no RNG in the second engine"),
		Second->ExportRandomTrace().Num(), 0);

	TUniquePtr<FBattleEngine> EffectEngine;
	TestTrue(TEXT("The live condition-effect engine is created"),
		TryCreateEngine(70748, EffectEngine));
	if (!EffectEngine.IsValid())
	{
		return false;
	}
	const FBattlerId EffectUser = MakeNumericId<FBattlerId>(PlayerBattlerValue);
	FBattleEffectExecutionResult SetFieldResult;
	TestTrue(TEXT("Sun is applied through live field-effect execution"),
		FBattleC07DEngineFixture::ExecuteCatalogMove(
			*EffectEngine,
			EffectUser,
			MakeDefinitionId<FMoveId>(SetSunMoveName),
			FBattleResolvedTarget::CreateField(),
			SetFieldResult,
			707480));
	TestTrue(TEXT("Live field application emits a field mutation"),
		SetFieldResult.Events.ContainsByPredicate(
			[](const FBattleEffectExecutionEvent& Event)
			{
				return Event.Type == EBattleEventType::FieldEffectChanged
					&& Event.Outcome == EBattleEffectExecutionOutcome::Applied;
			}));
	TestTrue(TEXT("Live field application stores exact public duration and source"),
		EffectEngine->GetSnapshot().GetWeather().IsSet()
			&& EffectEngine->GetSnapshot().GetWeather().GetValue().ConditionId
				== FBattleFieldSideConditionRules::GetSunId()
			&& EffectEngine->GetSnapshot().GetWeather().GetValue().RemainingTurns.IsSet()
			&& EffectEngine->GetSnapshot().GetWeather().GetValue().RemainingTurns.GetValue()
				== 5
			&& EffectEngine->GetSnapshot().GetWeather().GetValue().SourceBattlerId
				== EffectUser);
	TestTrue(TEXT("Live field application registers shared triggers"),
		FBattleC07DEngineFixture::GetActiveTriggerCount(
			*EffectEngine,
			FBattleFieldSideConditionRules::GetSunId()) > 0);
	FBattleEffectExecutionResult SetGenericWeatherResult;
	TestTrue(TEXT("Custom weather replaces approved weather through live execution"),
		FBattleC07DEngineFixture::ExecuteCatalogMove(
			*EffectEngine,
			EffectUser,
			MakeDefinitionId<FMoveId>(SetGenericWeatherMoveName),
			FBattleResolvedTarget::CreateField(),
			SetGenericWeatherResult,
			707481));
	TestTrue(TEXT("Custom weather is publicly projected after replacement"),
		EffectEngine->GetSnapshot().GetWeather().IsSet()
			&& EffectEngine->GetSnapshot().GetWeather().GetValue().ConditionId
				== MakeDefinitionId<FConditionId>(GenericWeatherConditionName));
	TestEqual(TEXT("Replacing approved weather with custom weather cleans its triggers"),
		FBattleC07DEngineFixture::GetActiveTriggerCount(
			*EffectEngine,
			FBattleFieldSideConditionRules::GetSunId()),
		0);
	FBattleEffectExecutionResult ReplaceGenericWeatherResult;
	TestTrue(TEXT("Approved weather replaces custom weather through live execution"),
		FBattleC07DEngineFixture::ExecuteCatalogMove(
			*EffectEngine,
			EffectUser,
			MakeDefinitionId<FMoveId>(SetSunMoveName),
			FBattleResolvedTarget::CreateField(),
			ReplaceGenericWeatherResult,
			707482));
	TestTrue(TEXT("Approved weather is restored after replacing custom weather"),
		EffectEngine->GetSnapshot().GetWeather().IsSet()
			&& EffectEngine->GetSnapshot().GetWeather().GetValue().ConditionId
				== FBattleFieldSideConditionRules::GetSunId());
	TestTrue(TEXT("Replacing custom weather registers approved weather triggers"),
		FBattleC07DEngineFixture::GetActiveTriggerCount(
			*EffectEngine,
			FBattleFieldSideConditionRules::GetSunId()) > 0);
	FBattleEffectExecutionResult RemoveFieldResult;
	TestTrue(TEXT("Sun is removed through live field-effect execution"),
		FBattleC07DEngineFixture::ExecuteCatalogMove(
			*EffectEngine,
			EffectUser,
			MakeDefinitionId<FMoveId>(RemoveSunMoveName),
			FBattleResolvedTarget::CreateField(),
			RemoveFieldResult,
			707483));
	TestFalse(TEXT("Live field removal clears the public weather snapshot"),
		EffectEngine->GetSnapshot().GetWeather().IsSet());
	TestEqual(TEXT("Live field removal cleans every matching trigger"),
		FBattleC07DEngineFixture::GetActiveTriggerCount(
			*EffectEngine,
			FBattleFieldSideConditionRules::GetSunId()),
		0);

	const FBattleResolvedTarget PlayerSideTarget = MakeSideTarget(EBattleSide::Player);
	FBattleEffectExecutionResult SetSideResult;
	TestTrue(TEXT("Tailwind is applied through live side-effect execution"),
		FBattleC07DEngineFixture::ExecuteCatalogMove(
			*EffectEngine,
			EffectUser,
			MakeDefinitionId<FMoveId>(SetTailwindMoveName),
			PlayerSideTarget,
			SetSideResult,
			707482));
	const FBattleSnapshot LiveSideApplicationSnapshot = EffectEngine->GetSnapshot();
	const FBattleObservedSide* LivePlayerSide =
		LiveSideApplicationSnapshot.GetObservedSides().FindByPredicate(
			[](const FBattleObservedSide& Side)
			{
				return Side.Side == EBattleSide::Player;
			});
	TestTrue(TEXT("Live side application stores exact duration and source"),
		LivePlayerSide != nullptr
			&& LivePlayerSide->Conditions.ContainsByPredicate(
				[EffectUser](const FBattleObservedCondition& Condition)
				{
					return Condition.ConditionId
							== FBattleFieldSideConditionRules::GetTailwindId()
						&& Condition.RemainingTurns.IsSet()
						&& Condition.RemainingTurns.GetValue() == 4
						&& Condition.SourceBattlerId == EffectUser;
				}));
	TestTrue(TEXT("Live side application registers shared triggers"),
		FBattleC07DEngineFixture::GetActiveTriggerCount(
			*EffectEngine,
			FBattleFieldSideConditionRules::GetTailwindId()) > 0);
	FBattleEffectExecutionResult RemoveSideResult;
	TestTrue(TEXT("Tailwind is removed through live side-effect execution"),
		FBattleC07DEngineFixture::ExecuteCatalogMove(
			*EffectEngine,
			EffectUser,
			MakeDefinitionId<FMoveId>(RemoveTailwindMoveName),
			PlayerSideTarget,
			RemoveSideResult,
			707483));
	const FBattleSnapshot LiveSideRemovalSnapshot = EffectEngine->GetSnapshot();
	LivePlayerSide = LiveSideRemovalSnapshot.GetObservedSides().FindByPredicate(
		[](const FBattleObservedSide& Side)
		{
			return Side.Side == EBattleSide::Player;
		});
	TestTrue(TEXT("Live side removal clears the exact side condition"),
		LivePlayerSide != nullptr
			&& !LivePlayerSide->Conditions.ContainsByPredicate(
				[](const FBattleObservedCondition& Condition)
				{
					return Condition.ConditionId
						== FBattleFieldSideConditionRules::GetTailwindId();
				}));
	TestEqual(TEXT("Live side removal cleans every matching trigger"),
		FBattleC07DEngineFixture::GetActiveTriggerCount(
			*EffectEngine,
			FBattleFieldSideConditionRules::GetTailwindId()),
		0);
	TestEqual(TEXT("Live field and side operations consume no RNG"),
		EffectEngine->ExportRandomTrace().Num(), 0);
	return true;
}

#endif
