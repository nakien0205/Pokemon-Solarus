#if WITH_DEV_AUTOMATION_TESTS

#include "Battle/BattleEffectExecutorContext.h"
#include "Battle/BattleEffectExecutor.h"
#include "Battle/BattleFieldSideConditions.h"
#include "Battle/BattleItem.h"
#include "Battle/BattleReplay.h"
#include "Battle/BattleState.h"
#include "BattleAtomicCheckpointTestCommon.h"
#include "BattleAtomicMoveCheckpointTestSupport.h"
#include "Misc/AutomationTest.h"

/** R5-only wrapper over the shared ADR-0002 friend fixture. */
class FBattleC10HeldItemMovesFixture final
{
public:
	static FBattleEngineState& GetMutableState(FBattleEngine& Engine)
	{
		return FBattleC09BWildFlowEngineFixture::GetMutableState(Engine);
	}

	static const FBattleEngineState& GetState(const FBattleEngine& Engine)
	{
		return FBattleC09BWildFlowEngineFixture::GetState(Engine);
	}

	static bool ReplaceWithConsumedHistory(
		FBattleEngine& Engine,
		const TArray<TPair<FItemId, uint64>>& Items)
	{
		FBattleEngineState& State = GetMutableState(Engine);
		TArray<FBattleHeldItemInstanceState> LedgerStates;
		for (const FBattleHeldItemInstanceState& Existing :
			State.HeldItemLedger.GetStates())
		{
			LedgerStates.Add(Existing);
		}
		for (int32 Index = 0; Index < Items.Num(); ++Index)
		{
			FBattleHeldItemInstanceState Item;
			Item.InstanceId = BattleAtomicCheckpointTestCommonPrivate::MakeNumericId<
				FBattleHeldItemInstanceId>(9000 + Index);
			Item.Origin = EBattleHeldItemOrigin::BattleGenerated;
			Item.DefinitionItemId = Items[Index].Key;
			Item.LastConsumerTrainerId =
				BattleAtomicCheckpointTestCommonPrivate::MakeNumericId<FTrainerId>(
					BattleAtomicCheckpointTestCommonPrivate::PlayerTrainerValue);
			Item.LastConsumerBattlerId =
				BattleAtomicCheckpointTestCommonPrivate::MakeNumericId<FBattlerId>(
					BattleAtomicCheckpointTestCommonPrivate::PlayerLeftValue);
			Item.LastConsumptionFactOrdinal = Items[Index].Value;
			Item.bConsumed = true;
			LedgerStates.Add(MoveTemp(Item));
		}
		EBattleHeldItemContractError Error = EBattleHeldItemContractError::None;
		return FBattleHeldItemLedger::TryCreate(
			LedgerStates,
			State.HeldItemLedger,
			Error);
	}

	static bool SuppressHeldItem(
		FBattleEngine& Engine,
		const FBattlerId BattlerId)
	{
		FBattleEngineState& State = GetMutableState(Engine);
		FBattleBattlerState* Battler = State.FindMutableBattler(BattlerId);
		if (Battler == nullptr || !Battler->HeldItem.InstanceId.IsValid())
		{
			return false;
		}
		FBattleHeldItemOperationRequest Operation;
		Operation.Kind = EBattleHeldItemOperationKind::Suppress;
		Operation.PrimaryInstanceId = Battler->HeldItem.InstanceId;
		Operation.bSuppressed = true;
		FBattleHeldItemOperationFact Fact;
		EBattleHeldItemContractError ItemError = EBattleHeldItemContractError::None;
		if (!State.HeldItemLedger.TryApplyOperation(Operation, Fact, ItemError))
		{
			return false;
		}
		Battler->HeldItem.bSuppressed = true;
		Battler->HeldItem.ChoiceLockedMoveId = FMoveId();

		FBattleTriggerOperationContext Context;
		Context.ReentrancyToken =
			BattleAtomicCheckpointTestCommonPrivate::MakeNumericId<
				FBattleTriggerReentrancyToken>(9901);
		for (const FBattleTriggerRegistrationState& Registration :
			State.TriggerFramework.GetActiveRegistrations())
		{
			if (Registration.Spec.SourceDefinition.Kind
					== EBattleTriggerSourceDefinitionKind::Item
				&& Registration.Spec.Owner.Kind == EBattleTriggerSubjectKind::Battler
				&& Registration.Spec.Owner.BattlerId == BattlerId)
			{
				EBattleTriggerError TriggerError = EBattleTriggerError::None;
				if (!State.TriggerFramework.TrySetSuppressed(
						Registration.RegistrationId,
						true,
						Context,
						TriggerError))
				{
					return false;
				}
			}
		}
		TArray<FBattleTriggerLifecycleFact> IgnoredFacts;
		State.TriggerFramework.DrainLifecycleFacts(IgnoredFacts);
		return true;
	}

	static bool AddMagicRoom(FBattleEngine& Engine)
	{
		FBattleEngineState& State = GetMutableState(Engine);
		if (State.Field.Rooms.ContainsByPredicate(
			[](const FBattleConditionState& Condition)
			{
				return Condition.ConditionId
					== FBattleFieldSideConditionRules::GetMagicRoomId();
			}))
		{
			return false;
		}
		FBattleConditionState Room;
		Room.ConditionId = FBattleFieldSideConditionRules::GetMagicRoomId();
		Room.LayerCount = 1;
		Room.CreationOrdinal = State.NextConditionCreationOrdinal++;
		State.Field.Rooms.Add(MoveTemp(Room));
		return true;
	}

	static bool AddFieldCondition(
		FBattleEngine& Engine,
		const FConditionId ConditionId,
		const bool bTerrain)
	{
		FBattleEngineState& State = GetMutableState(Engine);
		if (!FBattleFieldSideConditionRules::IsCanonical(ConditionId)
			|| (bTerrain && State.Field.Terrain.IsSet())
			|| (!bTerrain && State.Field.Weather.IsSet()))
		{
			return false;
		}

		const FBattlerId SourceId =
			BattleAtomicCheckpointTestCommonPrivate::MakeNumericId<FBattlerId>(
				BattleAtomicCheckpointTestCommonPrivate::PlayerLeftValue);
		FBattleTriggerSubject Source;
		if (!FBattleTriggerSubject::TryCreateBattler(SourceId, Source))
		{
			return false;
		}
		FBattleFieldSideTriggerRegistrationFacts Facts;
		Facts.ConditionId = ConditionId;
		Facts.PayloadId = ConditionId.GetDefinitionId();
		Facts.Owner = FBattleTriggerSubject::CreateField();
		Facts.Source = Source;
		Facts.Targets.Add(Facts.Owner);
		Facts.RemainingTurns = 5;
		EBattleTriggerError TriggerError = EBattleTriggerError::None;
		if (!FBattleFieldSideConditionRules::TryRegisterTriggers(
				State.TriggerFramework,
				Facts,
				TriggerError))
		{
			return false;
		}

		FBattleConditionState Condition;
		Condition.ConditionId = ConditionId;
		Condition.RemainingTurns = 5;
		Condition.LayerCount = 1;
		Condition.CreationOrdinal = State.NextConditionCreationOrdinal++;
		Condition.SourceBattlerId = SourceId;
		if (bTerrain)
		{
			State.Field.Terrain = MoveTemp(Condition);
		}
		else
		{
			State.Field.Weather = MoveTemp(Condition);
		}
		TArray<FBattleTriggerLifecycleFact> IgnoredFacts;
		TArray<FBattleTriggerEffectRequest> IgnoredRequests;
		State.TriggerFramework.DrainLifecycleFacts(IgnoredFacts);
		State.TriggerFramework.DrainEffectRequests(IgnoredRequests);
		return true;
	}

	static bool SetVolatileLayers(
		FBattleEngine& Engine,
		const FBattlerId BattlerId,
		const FConditionId ConditionId,
		const int32 Layers)
	{
		FBattleBattlerState* Battler = GetMutableState(Engine).FindMutableBattler(
			BattlerId);
		FBattleConditionState* Condition = Battler != nullptr
			? Battler->Volatiles.FindByPredicate(
				[ConditionId](const FBattleConditionState& Candidate)
				{
					return Candidate.ConditionId == ConditionId;
				})
			: nullptr;
		if (Condition == nullptr || Layers <= 0)
		{
			return false;
		}
		Condition->LayerCount = Layers;
		return true;
	}
};

namespace BattleHeldItemMoveEffectTestsPrivate
{
	// Organization decision: the R5 held-item move matrix remains one cohesive
	// integration family because every identity shares the same catalog, engine,
	// ledger, event, reveal, hook-mirror, and atomic-checkpoint fixtures.
	using namespace BattleAtomicCheckpointTestCommonPrivate;
	using namespace BattleAtomicMoveCheckpointTestSupportPrivate;

	const TCHAR* const KnockOffMoveName = TEXT("Move.C10R5.KnockOffShape");
	const TCHAR* const TrickMoveName = TEXT("Move.C10R5.TrickShape");
	const TCHAR* const ThiefMoveName = TEXT("Move.C10R5.ThiefShape");
	const TCHAR* const RecycleMoveName = TEXT("Move.C10R5.RecycleShape");
	const TCHAR* const UnremovableItemName = TEXT("Item.C10R5.Unremovable");

	FMoveId MoveId(const TCHAR* Name)
	{
		return MakeDefinitionId<FMoveId>(Name);
	}

	FItemId UnremovableItemId()
	{
		return MakeDefinitionId<FItemId>(UnremovableItemName);
	}

	FBattlerId UserId()
	{
		return MakeNumericId<FBattlerId>(PlayerLeftValue);
	}

	FBattlerId TargetId()
	{
		return MakeNumericId<FBattlerId>(OpponentLeftValue);
	}

	FTrainerId UserTrainerId()
	{
		return MakeNumericId<FTrainerId>(PlayerTrainerValue);
	}

	FTrainerId TargetTrainerId()
	{
		return MakeNumericId<FTrainerId>(OpponentTrainerValue);
	}

	FBattleMoveDefinition MakeHeldItemMove(
		const TCHAR* Name,
		const EBattleMoveHeldItemOperation Operation,
		const bool bDamaging,
		const int32 Power = 40,
		const bool bAlwaysHits = true,
		const int32 Accuracy = 100)
	{
		FBattleMoveDefinition Move;
		Move.Id = MoveId(Name);
		Move.Type = EPokemonType::Normal;
		Move.Category = bDamaging
			? EBattleMoveCategory::Physical
			: EBattleMoveCategory::Status;
		Move.Power = bDamaging ? Power : 0;
		Move.Accuracy = bAlwaysHits ? 0 : Accuracy;
		Move.bAlwaysHits = bAlwaysHits;
		Move.BasePP = 20;
		Move.TargetClass = Operation == EBattleMoveHeldItemOperation::RestoreLastConsumed
			? EBattleTargetClass::Self
			: EBattleTargetClass::SelectedOpponent;
		Move.Flags = Operation == EBattleMoveHeldItemOperation::RestoreLastConsumed
			? EBattleMoveFlags::None
			: EBattleMoveFlags::BlockedByProtect;
		if (bDamaging)
		{
			Move.Flags |= EBattleMoveFlags::NeverCritical;
		}
		if (bDamaging)
		{
			FBattleMoveEffectDescriptor Damage;
			Damage.Order = 0;
			Damage.Kind = EBattleMoveEffectKind::Damage;
			Damage.Target = EBattleEffectTarget::ResolvedTarget;
			Move.Effects.Add(Damage);
		}
		FBattleMoveEffectDescriptor ItemEffect;
		ItemEffect.Order = bDamaging ? 1 : 0;
		ItemEffect.Kind = EBattleMoveEffectKind::ChangeItem;
		ItemEffect.Target = Operation
			== EBattleMoveHeldItemOperation::RestoreLastConsumed
				? EBattleEffectTarget::User
				: EBattleEffectTarget::ResolvedTarget;
		ItemEffect.HeldItemOperation = Operation;
		Move.Effects.Add(ItemEffect);
		return Move;
	}

	FBattleMoveDefinition MakeKnockOffMove(
		const int32 Power = 40,
		const bool bAlwaysHits = true,
		const int32 Accuracy = 100)
	{
		return MakeHeldItemMove(
			KnockOffMoveName,
			EBattleMoveHeldItemOperation::RemoveCurrent,
			true,
			Power,
			bAlwaysHits,
			Accuracy);
	}

	FBattleMoveDefinition MakeTrickMove()
	{
		return MakeHeldItemMove(
			TrickMoveName,
			EBattleMoveHeldItemOperation::ExchangeCurrent,
			false);
	}

	FBattleMoveDefinition MakeThiefMove(
		const int32 Power = 40,
		const bool bAlwaysHits = true,
		const int32 Accuracy = 100)
	{
		return MakeHeldItemMove(
			ThiefMoveName,
			EBattleMoveHeldItemOperation::TransferCurrent,
			true,
			Power,
			bAlwaysHits,
			Accuracy);
	}

	FBattleMoveDefinition MakeRecycleMove()
	{
		return MakeHeldItemMove(
			RecycleMoveName,
			EBattleMoveHeldItemOperation::RestoreLastConsumed,
			false);
	}

	TArray<FBattleTypeChartEntry> MakeTypeChart(const bool bNormalImmune)
	{
		TArray<FBattleTypeChartEntry> Entries = MakeNeutralTypeChart();
		if (bNormalImmune)
		{
			for (FBattleTypeChartEntry& Entry : Entries)
			{
				if (Entry.AttackingType == EPokemonType::Normal
					&& Entry.DefendingType == EPokemonType::Ghost)
				{
					Entry.Numerator = 0;
					Entry.Denominator = 1;
				}
			}
		}
		return Entries;
	}

	bool TryMakeCatalog(
		const FBattleMoveDefinition& Move,
		const bool bNormalImmune,
		FBattleDefinitionCatalog& OutCatalog)
	{
		FBattleDefinitionCatalogInput Input;
		Input.TypeChartEntries = MakeTypeChart(bNormalImmune);
		Input.Moves = {MakeProbeMove(), MakeTargetProbeMove(), Move};
		Input.Abilities =
		{
			{FBattleAbilityRules::GetBlazeId()},
			{FBattleAbilityRules::GetIntimidateId()},
			{FBattleAbilityRules::GetMagicGuardId()}
		};
		Input.Items =
		{
			{FBattleItemRules::GetLeftoversId(), EBattleItemKind::Held},
			{FBattleItemRules::GetSitrusBerryId(), EBattleItemKind::Held},
			{FBattleItemRules::GetLumBerryId(), EBattleItemKind::Held},
			{FBattleItemRules::GetAirBalloonId(), EBattleItemKind::Held},
			{FBattleItemRules::GetChoiceBandId(), EBattleItemKind::Held},
			{FBattleItemRules::GetHeavyDutyBootsId(), EBattleItemKind::Held},
			{FBattleItemRules::GetLifeOrbId(), EBattleItemKind::Held},
			{UnremovableItemId(), EBattleItemKind::Held, false}
		};
		for (const FConditionId& Id : FBattleVolatileRules::GetCanonicalIds())
		{
			Input.Conditions.Add({Id, EBattleConditionKind::Volatile});
		}
		for (const FConditionId& Id : FBattleMajorStatusRules::GetCanonicalIds())
		{
			Input.Conditions.Add({Id, EBattleConditionKind::MajorStatus});
		}
		for (const FConditionId& Id : FBattleFieldSideConditionRules::GetCanonicalIds())
		{
			Input.Conditions.Add(
				{Id, FBattleFieldSideConditionRules::GetConditionFamily(Id)});
		}
		FBattleSpeciesFormDefinition User = MakeSpecies(PlayerSpeciesName);
		User.PrimaryType = EPokemonType::Normal;
		Input.SpeciesForms.Add(MoveTemp(User));
		FBattleSpeciesFormDefinition Target = MakeSpecies(WildSpeciesName);
		Target.PrimaryType = bNormalImmune
			? EPokemonType::Ghost
			: EPokemonType::Normal;
		Input.SpeciesForms.Add(MoveTemp(Target));
		TArray<FBattleCatalogDiagnostic> Diagnostics;
		return FBattleDefinitionCatalog::TryCreate(Input, OutCatalog, Diagnostics)
			&& Diagnostics.IsEmpty();
	}

	bool TryMakeEngine(
		const FBattleMoveDefinition& Move,
		const FItemId UserItem,
		const FItemId TargetItem,
		const int32 UserHP,
		const int32 TargetHP,
		const bool bNormalImmune,
		TArray<FBattleExpectedRandomDraw> ExpectedDraws,
		TUniquePtr<FBattleEngine>& OutEngine,
		FStrictBattleRandom*& OutRandom)
	{
		OutEngine.Reset();
		OutRandom = nullptr;
		FAtomicWildScenario Scenario = MakePreMoveScenario(
			Move.Id,
			UserHP,
			FBattleAbilityRules::GetBlazeId(),
			UserItem);
		Scenario.TargetCurrentHP = TargetHP;
		FBattleSetupInput Input = MakeSetupInput(Scenario);
		for (FBattlePartyEntrySetup& Entry : Input.PartyEntries)
		{
			if (Entry.BattlerId == TargetId())
			{
				Entry.OriginalHeldItemId = TargetItem;
				Entry.CurrentHeldItemId = TargetItem;
			}
		}
		FBattleSetup Setup;
		EBattleSetupValidationError SetupError = EBattleSetupValidationError::None;
		FBattleDefinitionCatalog Catalog;
		if (!FBattleSetup::TryCreate(Input, Setup, SetupError)
			|| !TryMakeCatalog(Move, bNormalImmune, Catalog))
		{
			return false;
		}
		TUniquePtr<FStrictBattleRandom> Random =
			MakeUnique<FStrictBattleRandom>(MoveTemp(ExpectedDraws));
		OutRandom = Random.Get();
		FBattleRejection Rejection;
		if (!FBattleEngine::TryCreate(
			Setup,
			Catalog,
			MoveTemp(Random),
			OutEngine,
			Rejection))
		{
			return false;
		}
		return OutEngine->TryBeginActionDecisionSequence(Rejection);
	}

	bool TryBuildDirectRequest(
		FBattleEngine& Engine,
		const FMoveId Move,
		FBattleEffectExecutionRequest& OutRequest,
		const uint64 Identity = 900)
	{
		OutRequest = FBattleEffectExecutionRequest();
		FBattleEngineState& State =
			FBattleC10HeldItemMovesFixture::GetMutableState(Engine);
		const FBattleMoveDefinition* Definition = State.Catalog.FindMove(Move);
		const FBattlerId ResolvedBattlerId = Definition != nullptr
			&& Definition->TargetClass == EBattleTargetClass::Self
				? UserId()
				: TargetId();
		const FBattleActivePositionState* User =
			State.ActivePositions.FindByPredicate(
				[](const FBattleActivePositionState& Candidate)
				{
					return Candidate.BattlerId == UserId();
				});
		const FBattleActivePositionState* Target =
			State.ActivePositions.FindByPredicate(
				[ResolvedBattlerId](const FBattleActivePositionState& Candidate)
				{
					return Candidate.BattlerId == ResolvedBattlerId;
				});
		if (Definition == nullptr || User == nullptr || Target == nullptr)
		{
			return false;
		}
		FBattleBattlerTarget BattlerTarget{Target->ActiveSlotId, ResolvedBattlerId};
		FBattleResolvedTarget ResolvedTarget;
		if (!FBattleResolvedTarget::TryCreateBattler(BattlerTarget, ResolvedTarget))
		{
			return false;
		}
		OutRequest.BattleId = State.Setup.GetBattleId();
		OutRequest.TurnId = State.TurnId;
		OutRequest.ActionId = MakeNumericId<FActionId>(Identity);
		OutRequest.ResolutionId = MakeNumericId<FResolutionId>(Identity);
		OutRequest.UserBattlerId = UserId();
		OutRequest.UserSlotId = User->ActiveSlotId;
		OutRequest.Move = Definition;
		OutRequest.Targets.Add(ResolvedTarget);
		return true;
	}

	bool TryExecuteDirect(
		FBattleEngine& Engine,
		const FMoveId Move,
		FBattleEffectExecutionResult& OutResult,
		EBattleEffectExecutorError& OutError,
		const uint64 Identity = 900)
	{
		FBattleEffectExecutionRequest Request;
		if (!TryBuildDirectRequest(Engine, Move, Request, Identity))
		{
			return false;
		}
		return FBattleEffectExecutor::TryExecuteAgainstState(
			Request,
			FBattleC10HeldItemMovesFixture::GetMutableState(Engine),
			OutResult,
			OutError);
	}

	bool TryPrepareEffectsCheckpoint(FBattleEngine& Engine, const FMoveId Move)
	{
		return TryPrepareTargetCheckpoint(Engine, Move)
			&& Engine.ResolveCurrentMoveTargets().WasAccepted();
	}

	const FBattleBattlerState* FindBattler(
		const FBattleEngine& Engine,
		const FBattlerId BattlerId)
	{
		return FBattleC10HeldItemMovesFixture::GetState(Engine).FindBattler(BattlerId);
	}

	int32 FindExecutionEvent(
		const FBattleEffectExecutionResult& Result,
		const EBattleEventType Type,
		const int32 StartIndex = 0)
	{
		for (int32 Index = FMath::Max(0, StartIndex); Index < Result.Events.Num(); ++Index)
		{
			if (Result.Events[Index].Type == Type)
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

	int32 FindResolutionEvent(
		const FBattleResolution& Resolution,
		const EBattleEventType Type,
		const int32 StartIndex = 0)
	{
		for (int32 Index = FMath::Max(0, StartIndex);
			Index < Resolution.GetEvents().Num();
			++Index)
		{
			if (Resolution.GetEvents()[Index].GetType() == Type)
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

	bool HasExecutionEventTarget(
		const FBattleEffectExecutionResult& Result,
		const EBattleEventType Type,
		const FBattlerId BattlerId)
	{
		return Result.Events.ContainsByPredicate(
			[Type, BattlerId](const FBattleEffectExecutionEvent& Event)
			{
				return Event.Type == Type
					&& Event.Targets.ContainsByPredicate(
						[BattlerId](const FBattleEventTarget& Target)
						{
							return Target.BattlerId == BattlerId;
						});
			});
	}

	bool HasExactResolutionEvents(
		const FBattleResolution& Resolution,
		const TArray<EBattleEventType>& Expected)
	{
		if (Resolution.GetEvents().Num() != Expected.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < Expected.Num(); ++Index)
		{
			if (Resolution.GetEvents()[Index].GetType() != Expected[Index])
			{
				return false;
			}
		}
		return true;
	}

	bool IsMutationEvent(
		const FBattleEffectExecutionEvent& Event,
		const EBattleEventType Type,
		const FItemId ItemId,
		const FBattlerId SourceBattlerId,
		const FBattlerId TargetBattlerId,
		const int64 Before,
		const int64 After,
		const int64 Delta)
	{
		return Event.Type == Type
			&& Event.Cause == EBattleEventCause::Item
			&& Event.Outcome == EBattleEffectExecutionOutcome::Applied
			&& Event.SourceOverride.IsSet()
			&& Event.SourceOverride.GetValue().BattlerId == SourceBattlerId
			&& Event.SourceOverride.GetValue().DefinitionId
				== ItemId.GetDefinitionId()
			&& Event.Targets.Num() == 1
			&& Event.Targets[0].BattlerId == TargetBattlerId
			&& Event.NumericBefore.IsSet()
			&& Event.NumericBefore.GetValue() == Before
			&& Event.NumericAfter.IsSet()
			&& Event.NumericAfter.GetValue() == After
			&& Event.NumericDelta.IsSet()
			&& Event.NumericDelta.GetValue() == Delta;
	}

	bool IsPublicMutationEvent(
		const FBattleResolution& Resolution,
		const int32 EventIndex,
		const EBattleEventType Type,
		const FItemId ItemId,
		const FTrainerId SourceTrainerId,
		const FBattlerId SourceBattlerId,
		const FTrainerId TargetTrainerId,
		const FBattlerId TargetBattlerId,
		const int64 Before,
		const int64 After,
		const int64 Delta)
	{
		if (EventIndex < 0 || EventIndex >= Resolution.GetEvents().Num())
		{
			return false;
		}
		const FBattleEvent& Event = Resolution.GetEvents()[EventIndex];
		return Event.IsValid()
			&& Event.GetResolutionId() == Resolution.GetResolutionId()
			&& Event.GetActionId().IsValid()
			&& Event.GetType() == Type
			&& Event.GetCause() == EBattleEventCause::Item
			&& Event.GetCauseActionKind() == EBattleActionKind::Fight
			&& Event.GetSource().TrainerId == SourceTrainerId
			&& Event.GetSource().BattlerId == SourceBattlerId
			&& Event.GetSource().ActiveSlotId.IsValid()
			&& Event.GetSource().DefinitionId == ItemId.GetDefinitionId()
			&& Event.GetTargets().Num() == 1
			&& Event.GetTargets()[0].TrainerId == TargetTrainerId
			&& Event.GetTargets()[0].BattlerId == TargetBattlerId
			&& Event.GetTargets()[0].ActiveSlotId.IsValid()
			&& !Event.GetTargets()[0].bHasSide
			&& !Event.GetTargets()[0].bField
			&& Event.GetNumericBefore() == TOptional<int64>(Before)
			&& Event.GetNumericAfter() == TOptional<int64>(After)
			&& Event.GetNumericDelta() == TOptional<int64>(Delta)
			&& !Event.GetHitIndex().IsSet()
			&& !Event.GetHitCount().IsSet()
			&& Event.GetVisibility().Level == EBattleVisibilityLevel::Public
			&& !Event.GetVisibility().OwningTrainerId.IsValid()
			&& !Event.GetVisibility().bHasOwningSide
			&& Event.GetVisibility().bRevealSourceDefinition;
	}

	bool HasBeenRevealed(
		const FBattleEngine& Engine,
		const FItemId ItemId,
		const FBattlerId OwnerId)
	{
		FBattleTriggerSourceDefinition Source;
		FBattleTriggerSubject Owner;
		return FBattleTriggerSourceDefinition::TryCreateItem(ItemId, Source)
			&& FBattleTriggerSubject::TryCreateBattler(OwnerId, Owner)
			&& FBattleC10HeldItemMovesFixture::GetState(Engine)
				.AbilityItemRevealTracker.HasBeenRevealed(Source, Owner);
	}

	int32 CountItemHooks(
		const FBattleEngine& Engine,
		const FItemId ItemId,
		const FBattlerId OwnerId,
		const TOptional<bool> Suppressed = TOptional<bool>())
	{
		int32 Count = 0;
		for (const FBattleTriggerRegistrationState& Registration :
			FBattleC10HeldItemMovesFixture::GetState(Engine)
				.TriggerFramework.GetActiveRegistrations())
		{
			if (Registration.Spec.SourceDefinition.Kind
					== EBattleTriggerSourceDefinitionKind::Item
				&& Registration.Spec.SourceDefinition.ItemId == ItemId
				&& Registration.Spec.Owner.Kind == EBattleTriggerSubjectKind::Battler
				&& Registration.Spec.Owner.BattlerId == OwnerId
				&& (!Suppressed.IsSet()
					|| Registration.bSuppressed == Suppressed.GetValue()))
			{
				++Count;
			}
		}
		return Count;
	}

	bool TryBuildFinalFacts(
		const FBattleEngine& Engine,
		TArray<FBattleFinalHeldItemFact>& OutFacts)
	{
		EBattleHeldItemContractError Error = EBattleHeldItemContractError::None;
		return FBattleC10HeldItemMovesFixture::GetState(Engine)
			.HeldItemLedger.TryBuildFinalFacts({}, OutFacts, Error);
	}

	const FBattleFinalHeldItemFact* FindFinalFact(
		const TArray<FBattleFinalHeldItemFact>& Facts,
		const FItemId ItemId)
	{
		return Facts.FindByPredicate(
			[ItemId](const FBattleFinalHeldItemFact& Fact)
			{
				return Fact.DefinitionItemId == ItemId;
			});
	}

	const FBattleHeldItemInstanceState* FindLedgerItem(
		const FBattleEngine& Engine,
		const FItemId ItemId)
	{
		for (const FBattleHeldItemInstanceState& Item :
			FBattleC10HeldItemMovesFixture::GetState(Engine)
				.HeldItemLedger.GetStates())
		{
			if (Item.DefinitionItemId == ItemId)
			{
				return &Item;
			}
		}
		return nullptr;
	}

	TArray<FBattleHeldItemInstanceState> CopyLedger(const FBattleEngine& Engine)
	{
		TArray<FBattleHeldItemInstanceState> Copy;
		for (const FBattleHeldItemInstanceState& Item :
			FBattleC10HeldItemMovesFixture::GetState(Engine)
				.HeldItemLedger.GetStates())
		{
			Copy.Add(Item);
		}
		return Copy;
	}

	bool IsLedgerIdentical(
		const FBattleEngine& Engine,
		const TArray<FBattleHeldItemInstanceState>& Expected)
	{
		const TConstArrayView<FBattleHeldItemInstanceState> Actual =
			FBattleC10HeldItemMovesFixture::GetState(Engine)
				.HeldItemLedger.GetStates();
		if (Actual.Num() != Expected.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < Expected.Num(); ++Index)
		{
			if (!(Actual[Index] == Expected[Index]))
			{
				return false;
			}
		}
		return true;
	}

	bool TrySerializeReplay(FBattleEngine& Engine, TArray<uint8>& OutBytes)
	{
		FBattleRejection Rejection;
		return FBattleReplaySerializer::TrySerializeCanonical(
			Engine.ExportReplayRecord(),
			OutBytes,
			Rejection);
	}

	bool TrySeedProtect(FBattleEngine& Engine)
	{
		FBattleEngineState& State =
			FBattleC10HeldItemMovesFixture::GetMutableState(Engine);
		FBattleBattlerState* Battler = State.FindMutableBattler(TargetId());
		FBattleTriggerSubject Owner;
		if (Battler == nullptr
			|| !FBattleTriggerSubject::TryCreateBattler(TargetId(), Owner))
		{
			return false;
		}
		FBattleVolatileTriggerRegistrationFacts Facts;
		Facts.VolatileId = FBattleVolatileRules::GetProtectId();
		Facts.PayloadId = Facts.VolatileId.GetDefinitionId();
		Facts.Owner = Facts.Source = Owner;
		Facts.Targets.Add(Owner);
		Facts.Layers = FBattleVolatileRules::GetProtectInitialChainCounter();
		EBattleTriggerError Error = EBattleTriggerError::None;
		if (!FBattleVolatileRules::TryRegisterTriggers(
				State.TriggerFramework,
				Facts,
				Error))
		{
			return false;
		}
		FBattleConditionState Condition;
		Condition.ConditionId = Facts.VolatileId;
		Condition.LayerCount = Facts.Layers;
		Condition.CreationOrdinal = State.NextConditionCreationOrdinal++;
		Condition.SourceBattlerId = TargetId();
		Battler->Volatiles.Add(MoveTemp(Condition));
		TArray<FBattleTriggerLifecycleFact> IgnoredFacts;
		State.TriggerFramework.DrainLifecycleFacts(IgnoredFacts);
		return true;
	}
}

using namespace BattleHeldItemMoveEffectTestsPrivate;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC10HeldItemMovesKnockOffMutationTest,
	"PokemonSolarus.Battle.C08C.C10HeldItemMoves.KnockOff.MutationPowerMirrorsAndFinalFacts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC10HeldItemMovesKnockOffMutationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FBattleMoveDefinition Move = MakeKnockOffMove();
	const TArray<FBattleExpectedRandomDraw> Draws =
	{
		{0, 15, 0, FBattleEffectExecutor::GetDamageRandomRulePurpose()}
	};
	TUniquePtr<FBattleEngine> Takeable;
	TUniquePtr<FBattleEngine> Unremovable;
	FStrictBattleRandom* TakeableRandom = nullptr;
	FStrictBattleRandom* UnremovableRandom = nullptr;
	if (!TestTrue(TEXT("Takeable Knock Off engine is created"),
		TryMakeEngine(Move, FItemId(), FBattleItemRules::GetChoiceBandId(),
			200, 200, false, Draws, Takeable, TakeableRandom))
		|| !TestTrue(TEXT("Unremovable Knock Off engine is created"),
		TryMakeEngine(Move, FItemId(), UnremovableItemId(),
			200, 200, false, Draws, Unremovable, UnremovableRandom)))
	{
		return false;
	}
	FBattleBattlerState* Target = FBattleC10HeldItemMovesFixture::GetMutableState(
		*Takeable).FindMutableBattler(TargetId());
	if (!TestNotNull(TEXT("Takeable target exists"), Target))
	{
		return false;
	}
	Target->HeldItem.ChoiceLockedMoveId = MakeDefinitionId<FMoveId>(
		TEXT("Move.C10R5.ChoiceLock"));
	const int32 HooksBefore = CountItemHooks(
		*Takeable,
		FBattleItemRules::GetChoiceBandId(),
		TargetId());
	bool bValid = true;

	{
		TUniquePtr<FBattleEngine> Ordered;
		FStrictBattleRandom* OrderedParentRandom = nullptr;
		FBattleEffectExecutionRequest Request;
		const bool bOrderFixtureReady = TryMakeEngine(
			Move,
			FItemId(),
			FBattleItemRules::GetChoiceBandId(),
			200,
			200,
			false,
			{},
			Ordered,
			OrderedParentRandom)
			&& FBattleC10HeldItemMovesFixture::AddFieldCondition(
				*Ordered,
				FBattleFieldSideConditionRules::GetElectricTerrainId(),
				true)
			&& FBattleC10HeldItemMovesFixture::AddFieldCondition(
				*Ordered,
				FBattleFieldSideConditionRules::GetRainId(),
				false)
			&& TryBuildDirectRequest(*Ordered, Move.Id, Request, 899);
		bValid &= TestTrue(TEXT("Knock Off modifier-order fixture is prepared"),
			bOrderFixtureReady);
		if (bOrderFixtureReady)
		{
			FBattleEngineState& State =
				FBattleC10HeldItemMovesFixture::GetMutableState(*Ordered);
			const FMoveId AllyRule = MakeDefinitionId<FMoveId>(
				TEXT("Move.C10R5.AllyPower"));
			FBattleAllyActionPowerModifierRegistration Registration;
			Registration.TurnId = Request.TurnId;
			Registration.SourceActionId = MakeNumericId<FActionId>(898);
			Registration.SourceMoveId = AllyRule;
			Registration.TargetActionId = Request.ActionId;
			Registration.Target = {Request.UserSlotId, Request.UserBattlerId};
			Registration.MagnitudeNumerator = 3;
			Registration.MagnitudeDenominator = 2;
			State.AllyActionPowerModifierRegistrations.Add(Registration);

			FBattleMoveDefinition OrderedMove = Move;
			OrderedMove.Type = EPokemonType::Electric;
			OrderedMove.Flags |=
				EBattleMoveFlags::HalvesPowerInRainSandstormOrSnow;
			Request.Move = &OrderedMove;
			FStrictBattleRandom ContextRandom(
				TArray<FBattleExpectedRandomDraw>{});
			BattleEffectExecutorPrivate::FStateExecutionContext Context(
				Request,
				State,
				ContextRandom);
			FBattleFinalDamageInput ProbeInput;
			FBattleFinalDamageInput ActualInput;
			const bool bInputsBuilt = Context.TryBuildDamageInput(
				OrderedMove,
				Request.Targets[0],
				false,
				ProbeInput)
				&& Context.TryBuildDamageInput(
					OrderedMove,
					Request.Targets[0],
					false,
					ActualInput);
			const FDefinitionId WeatherRule = MakeDefinitionId<FDefinitionId>(
				TEXT("Rule.C10WeatherMoveRules.HalfPower"));
			const FDefinitionId HeldItemRule = MakeDefinitionId<FDefinitionId>(
				TEXT("Rule.C08C.RemoveCurrentPower"));
			bValid &= TestTrue(
				TEXT("Pre-accuracy Knock Off probe contains only terrain and held-item power modifiers"),
				bInputsBuilt
					&& ProbeInput.PowerModifiers.Num() == 2
					&& ProbeInput.PowerModifiers[0].RuleId
						== FBattleFieldSideConditionRules::GetElectricTerrainId()
							.GetDefinitionId()
					&& ProbeInput.PowerModifiers[1].RuleId == HeldItemRule
					&& ProbeInput.PowerModifiers[1].ModifierQ12 == 6144);
			bValid &= TestTrue(
				TEXT("Priority-0 Knock Off power follows ally, terrain, and weather modifiers"),
				bInputsBuilt
					&& ActualInput.PowerModifiers.Num() == 4
					&& ActualInput.PowerModifiers[0].RuleId
						== AllyRule.GetDefinitionId()
					&& ActualInput.PowerModifiers[0].ModifierQ12 == 6144
					&& ActualInput.PowerModifiers[1].RuleId
						== FBattleFieldSideConditionRules::GetElectricTerrainId()
							.GetDefinitionId()
					&& ActualInput.PowerModifiers[2].RuleId == WeatherRule
					&& ActualInput.PowerModifiers[2].ModifierQ12 == 2048
					&& ActualInput.PowerModifiers[3].RuleId == HeldItemRule
					&& ActualInput.PowerModifiers[3].ModifierQ12 == 6144
					&& ContextRandom.IsExact()
					&& OrderedParentRandom != nullptr
					&& OrderedParentRandom->IsExact());
		}
	}

	FBattleEffectExecutionResult TakeableResult;
	FBattleEffectExecutionResult UnremovableResult;
	EBattleEffectExecutorError Error = EBattleEffectExecutorError::None;
	bValid &= TestTrue(TEXT("Takeable Knock Off executes"),
		TryExecuteDirect(*Takeable, Move.Id, TakeableResult, Error));
	bValid &= TestTrue(TEXT("Unremovable Knock Off still resolves damage"),
		TryExecuteDirect(*Unremovable, Move.Id, UnremovableResult, Error));
	const int32 MutationIndex = FindExecutionEvent(
		TakeableResult,
		EBattleEventType::ItemRemoved);
	bValid &= TestTrue(TEXT("Knock Off emits one exact removal after damage"),
		MutationIndex > FindExecutionEvent(TakeableResult, EBattleEventType::HPChanged)
			&& IsMutationEvent(
				TakeableResult.Events[MutationIndex],
				EBattleEventType::ItemRemoved,
				FBattleItemRules::GetChoiceBandId(),
				TargetId(),
				TargetId(),
				1,
				0,
				-1));
	bValid &= TestTrue(TEXT("Takeable target receives the exact 1.5 power branch"),
		TakeableResult.TotalActualDamage > UnremovableResult.TotalActualDamage);
	const FBattleBattlerState* RemovedTarget = FindBattler(*Takeable, TargetId());
	bValid &= TestTrue(TEXT("Knock Off clears item and Choice lock mirrors"),
		RemovedTarget != nullptr
			&& RemovedTarget->HeldItem.bTemporarilyRemoved
			&& !RemovedTarget->HeldItem.bSuppressed
			&& !RemovedTarget->HeldItem.ChoiceLockedMoveId.IsValid());
	bValid &= TestTrue(TEXT("Knock Off removes every old-holder item hook"),
		HooksBefore > 0
			&& CountItemHooks(
				*Takeable,
				FBattleItemRules::GetChoiceBandId(),
				TargetId()) == 0);
	bValid &= TestTrue(TEXT("Knock Off synchronizes public reveal ownership"),
		HasBeenRevealed(
			*Takeable,
			FBattleItemRules::GetChoiceBandId(),
			TargetId()));
	const FBattleHeldItemInstanceState* LedgerItem = RemovedTarget != nullptr
		? FBattleC10HeldItemMovesFixture::GetState(*Takeable)
			.HeldItemLedger.FindState(RemovedTarget->HeldItem.InstanceId)
		: nullptr;
	bValid &= TestTrue(TEXT("Knock Off ledger and battler mirrors agree"),
		LedgerItem != nullptr
			&& LedgerItem->bTemporarilyRemoved
			&& LedgerItem->bRevealed
			&& LedgerItem->CurrentItemId == RemovedTarget->HeldItem.CurrentItemId);
	TArray<FBattleFinalHeldItemFact> FinalFacts;
	const FBattleFinalHeldItemFact* Final = nullptr;
	bValid &= TestTrue(TEXT("Knock Off final facts build"),
		TryBuildFinalFacts(*Takeable, FinalFacts));
	Final = FindFinalFact(FinalFacts, FBattleItemRules::GetChoiceBandId());
	bValid &= TestTrue(TEXT("Temporary removal restores original persistent ownership"),
		Final != nullptr
			&& Final->Disposition
				== EBattleHeldItemFinalDisposition::OriginalOwner
			&& Final->FinalOwnerTrainerId == TargetTrainerId()
			&& Final->FinalOwnerBattlerId == TargetId()
			&& Final->FinalItemId == FBattleItemRules::GetChoiceBandId());
	bValid &= TestTrue(TEXT("Both damage executions consume only their exact draw"),
		TakeableRandom != nullptr && UnremovableRandom != nullptr
			&& TakeableRandom->IsExact() && UnremovableRandom->IsExact());
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC10HeldItemMovesKnockOffGateMatrixTest,
	"PokemonSolarus.Battle.C08C.C10HeldItemMoves.KnockOff.GatesSuppressionSubstituteAndLethal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC10HeldItemMovesKnockOffGateMatrixTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	bool bValid = true;
	EBattleEffectExecutorError Error = EBattleEffectExecutorError::None;
	const TArray<FBattleExpectedRandomDraw> DamageDraw =
	{
		{0, 15, 0, FBattleEffectExecutor::GetDamageRandomRulePurpose()}
	};

	{
		TUniquePtr<FBattleEngine> Empty;
		FStrictBattleRandom* Random = nullptr;
		FBattleEffectExecutionResult Result;
		bValid &= TestTrue(TEXT("Empty-target Knock Off engine is created"),
			TryMakeEngine(MakeKnockOffMove(), FItemId(), FItemId(),
				200, 200, false, DamageDraw, Empty, Random));
		bValid &= TestTrue(TEXT("Empty-target Knock Off resolves ordinary damage"),
			Empty.IsValid()
				&& TryExecuteDirect(*Empty, MoveId(KnockOffMoveName), Result, Error));
		bValid &= TestTrue(TEXT("Empty target prevents only the item mutation"),
			FindExecutionEvent(Result, EBattleEventType::HPChanged) != INDEX_NONE
				&& FindExecutionEvent(Result, EBattleEventType::EffectPrevented)
					!= INDEX_NONE
				&& FindExecutionEvent(Result, EBattleEventType::ItemRemoved)
					== INDEX_NONE
				&& Random != nullptr && Random->IsExact());
	}

	{
		TUniquePtr<FBattleEngine> Unremovable;
		FStrictBattleRandom* Random = nullptr;
		FBattleEffectExecutionResult Result;
		bValid &= TestTrue(TEXT("Unremovable-target Knock Off engine is created"),
			TryMakeEngine(MakeKnockOffMove(), FItemId(), UnremovableItemId(),
				200, 200, false, DamageDraw, Unremovable, Random));
		bValid &= TestTrue(TEXT("Unremovable-target Knock Off executes"),
			Unremovable.IsValid()
				&& TryExecuteDirect(
					*Unremovable,
					MoveId(KnockOffMoveName),
					Result,
					Error));
		const FBattleBattlerState* Target = Unremovable.IsValid()
			? FindBattler(*Unremovable, TargetId())
			: nullptr;
		bValid &= TestTrue(TEXT("Unremovable item is retained atomically"),
			Target != nullptr
				&& Target->HeldItem.CurrentItemId == UnremovableItemId()
				&& !Target->HeldItem.bTemporarilyRemoved
				&& FindExecutionEvent(Result, EBattleEventType::EffectPrevented)
					!= INDEX_NONE
				&& FindExecutionEvent(Result, EBattleEventType::ItemRemoved)
					== INDEX_NONE
				&& Random != nullptr && Random->IsExact());
	}

	{
		TUniquePtr<FBattleEngine> Suppressed;
		FStrictBattleRandom* Random = nullptr;
		FBattleEffectExecutionResult Result;
		bValid &= TestTrue(TEXT("Suppressed-item Knock Off engine is created"),
			TryMakeEngine(MakeKnockOffMove(), FItemId(),
				FBattleItemRules::GetChoiceBandId(), 200, 200, false,
				DamageDraw, Suppressed, Random));
		bValid &= TestTrue(TEXT("Target item is suppressed through ledger and hooks"),
			Suppressed.IsValid()
				&& FBattleC10HeldItemMovesFixture::SuppressHeldItem(
					*Suppressed,
					TargetId()));
		FBattleEffectExecutionRequest SuppressedRequest;
		FBattleFinalDamageInput SuppressedProbeInput;
		FBattleFinalDamageInput SuppressedActualInput;
		FStrictBattleRandom ModifierRandom(
			TArray<FBattleExpectedRandomDraw>{});
		const bool bSuppressedInputsBuilt = Suppressed.IsValid()
			&& TryBuildDirectRequest(
				*Suppressed,
				MoveId(KnockOffMoveName),
				SuppressedRequest,
				901);
		if (bSuppressedInputsBuilt)
		{
			BattleEffectExecutorPrivate::FStateExecutionContext Context(
				SuppressedRequest,
				FBattleC10HeldItemMovesFixture::GetMutableState(*Suppressed),
				ModifierRandom);
			bValid &= TestTrue(
				TEXT("Suppressed takeable item qualifies for the 1.5x pre-accuracy and damage modifiers"),
				Context.TryBuildDamageInput(
					*SuppressedRequest.Move,
					SuppressedRequest.Targets[0],
					false,
					SuppressedProbeInput)
					&& Context.TryBuildDamageInput(
						*SuppressedRequest.Move,
						SuppressedRequest.Targets[0],
						false,
						SuppressedActualInput)
					&& SuppressedProbeInput.PowerModifiers.Num() == 1
					&& SuppressedProbeInput.PowerModifiers[0].RuleId
						== MakeDefinitionId<FDefinitionId>(
							TEXT("Rule.C08C.RemoveCurrentPower"))
					&& SuppressedProbeInput.PowerModifiers[0].ModifierQ12 == 6144
					&& SuppressedActualInput.PowerModifiers.Num() == 1
					&& SuppressedActualInput.PowerModifiers[0].RuleId
						== SuppressedProbeInput.PowerModifiers[0].RuleId
					&& SuppressedActualInput.PowerModifiers[0].ModifierQ12 == 6144
					&& !SuppressedActualInput.PowerModifiers[0].bIgnoredByCritical
					&& ModifierRandom.IsExact());
		}
		else
		{
			bValid &= TestTrue(
				TEXT("Suppressed Knock Off damage-input fixture is prepared"),
				false);
		}
		bValid &= TestTrue(TEXT("Suppression does not make a takeable item unremovable"),
			Suppressed.IsValid()
				&& TryExecuteDirect(
					*Suppressed,
					MoveId(KnockOffMoveName),
					Result,
					Error)
				&& FindExecutionEvent(Result, EBattleEventType::ItemRemoved)
					!= INDEX_NONE);
		const FBattleBattlerState* Target = Suppressed.IsValid()
			? FindBattler(*Suppressed, TargetId())
			: nullptr;
		bValid &= TestTrue(TEXT("Removal clears suppression and the held-item mirror"),
			Target != nullptr
				&& Target->HeldItem.bTemporarilyRemoved
				&& !Target->HeldItem.bSuppressed
				&& Random != nullptr && Random->IsExact());
	}

	{
		TUniquePtr<FBattleEngine> Protected;
		FStrictBattleRandom* Random = nullptr;
		FBattleEffectExecutionResult Result;
		bValid &= TestTrue(TEXT("Protected Knock Off engine is created"),
			TryMakeEngine(MakeKnockOffMove(), FItemId(),
				FBattleItemRules::GetLeftoversId(), 200, 200, false,
				{}, Protected, Random));
		bValid &= TestTrue(TEXT("Protect is seeded before execution"),
			Protected.IsValid() && TrySeedProtect(*Protected));
		bValid &= TestTrue(TEXT("Protect gates Knock Off before damage and mutation"),
			Protected.IsValid()
				&& TryExecuteDirect(
					*Protected,
					MoveId(KnockOffMoveName),
					Result,
					Error)
				&& FindExecutionEvent(Result, EBattleEventType::Protected)
					!= INDEX_NONE
				&& FindExecutionEvent(Result, EBattleEventType::Damage) == INDEX_NONE
				&& FindExecutionEvent(Result, EBattleEventType::ItemRemoved)
					== INDEX_NONE
				&& Random != nullptr && Random->IsExact()
				&& Random->GetTrace().IsEmpty());
	}

	{
		const FBattleMoveDefinition MissMove = MakeKnockOffMove(40, false, 50);
		TUniquePtr<FBattleEngine> Miss;
		FStrictBattleRandom* Random = nullptr;
		FBattleEffectExecutionResult Result;
		bValid &= TestTrue(TEXT("Boundary-miss Knock Off engine is created"),
			TryMakeEngine(MissMove, FItemId(), FBattleItemRules::GetLeftoversId(),
				200, 200, false,
				{{0, 99, 50, FBattleEffectExecutor::GetAccuracyRulePurpose()}},
				Miss, Random));
		bValid &= TestTrue(TEXT("Miss consumes accuracy only and retains the item"),
			Miss.IsValid()
				&& TryExecuteDirect(*Miss, MissMove.Id, Result, Error)
				&& FindExecutionEvent(Result, EBattleEventType::Missed) != INDEX_NONE
				&& FindExecutionEvent(Result, EBattleEventType::Damage) == INDEX_NONE
				&& FindExecutionEvent(Result, EBattleEventType::ItemRemoved)
					== INDEX_NONE
				&& FindBattler(*Miss, TargetId())->HeldItem.CurrentItemId
					== FBattleItemRules::GetLeftoversId()
				&& Random != nullptr && Random->IsExact());
	}

	{
		TUniquePtr<FBattleEngine> Immune;
		FStrictBattleRandom* Random = nullptr;
		FBattleEffectExecutionResult Result;
		bValid &= TestTrue(TEXT("Type-immune Knock Off engine is created"),
			TryMakeEngine(MakeKnockOffMove(), FItemId(),
				FBattleItemRules::GetLeftoversId(), 200, 200, true,
				{}, Immune, Random));
		bValid &= TestTrue(TEXT("Immunity gates Knock Off before RNG and mutation"),
			Immune.IsValid()
				&& TryExecuteDirect(
					*Immune,
					MoveId(KnockOffMoveName),
					Result,
					Error)
				&& FindExecutionEvent(Result, EBattleEventType::Immunity) != INDEX_NONE
				&& FindExecutionEvent(Result, EBattleEventType::ItemRemoved)
					== INDEX_NONE
				&& Random != nullptr && Random->IsExact()
				&& Random->GetTrace().IsEmpty());
	}

	{
		TUniquePtr<FBattleEngine> BoostedSubstitute;
		TUniquePtr<FBattleEngine> NeutralSubstitute;
		FStrictBattleRandom* BoostedRandom = nullptr;
		FStrictBattleRandom* NeutralRandom = nullptr;
		FBattleEffectExecutionResult BoostedResult;
		FBattleEffectExecutionResult NeutralResult;
		const bool bSubstitutesReady = TryMakeEngine(
			MakeKnockOffMove(),
			FItemId(),
			FBattleItemRules::GetLeftoversId(),
			200,
			200,
			false,
			DamageDraw,
			BoostedSubstitute,
			BoostedRandom)
			&& TryMakeEngine(
				MakeKnockOffMove(),
				FItemId(),
				UnremovableItemId(),
				200,
				200,
				false,
				DamageDraw,
				NeutralSubstitute,
				NeutralRandom)
			&& TrySeedActionStartVolatile(
				*BoostedSubstitute,
				TargetId(),
				FBattleVolatileRules::GetSubstituteId())
			&& TrySeedActionStartVolatile(
				*NeutralSubstitute,
				TargetId(),
				FBattleVolatileRules::GetSubstituteId())
			&& FBattleC10HeldItemMovesFixture::SetVolatileLayers(
				*BoostedSubstitute,
				TargetId(),
				FBattleVolatileRules::GetSubstituteId(),
				100)
			&& FBattleC10HeldItemMovesFixture::SetVolatileLayers(
				*NeutralSubstitute,
				TargetId(),
				FBattleVolatileRules::GetSubstituteId(),
				100);
		bValid &= TestTrue(TEXT("Boosted and neutral Substitute fixtures are seeded"),
			bSubstitutesReady);
		bValid &= TestTrue(
			TEXT("Knock Off boost reaches Substitute while item mutation remains blocked"),
			bSubstitutesReady
				&& TryExecuteDirect(
					*BoostedSubstitute,
					MoveId(KnockOffMoveName),
					BoostedResult,
					Error)
				&& TryExecuteDirect(
					*NeutralSubstitute,
					MoveId(KnockOffMoveName),
					NeutralResult,
					Error)
				&& BoostedResult.TotalActualDamage
					> NeutralResult.TotalActualDamage
				&& FindExecutionEvent(
					BoostedResult,
					EBattleEventType::ItemRemoved) == INDEX_NONE
				&& FindExecutionEvent(
					NeutralResult,
					EBattleEventType::ItemRemoved) == INDEX_NONE
				&& FindBattler(*BoostedSubstitute, TargetId())
					->HeldItem.CurrentItemId == FBattleItemRules::GetLeftoversId()
				&& FindBattler(*NeutralSubstitute, TargetId())
					->HeldItem.CurrentItemId == UnremovableItemId()
				&& BoostedRandom != nullptr && BoostedRandom->IsExact()
				&& NeutralRandom != nullptr && NeutralRandom->IsExact());
	}

	{
		const FBattleMoveDefinition LethalMove = MakeKnockOffMove(400);
		TUniquePtr<FBattleEngine> Lethal;
		FStrictBattleRandom* Random = nullptr;
		bValid &= TestTrue(TEXT("Lethal Knock Off engine is created"),
			TryMakeEngine(LethalMove, FItemId(),
				FBattleItemRules::GetChoiceBandId(), 200, 1, false,
				DamageDraw, Lethal, Random));
		bValid &= TestTrue(TEXT("Lethal Knock Off reaches the effects checkpoint"),
			Lethal.IsValid() && TryPrepareEffectsCheckpoint(*Lethal, LethalMove.Id));
		if (Lethal.IsValid())
		{
			const FBattleResolution Result = Lethal->ExecuteCurrentMoveEffects();
			const int32 Removed = FindResolutionEvent(
				Result,
				EBattleEventType::ItemRemoved);
			const int32 Fainted = FindResolutionEvent(
				Result,
				EBattleEventType::Fainted);
			bValid &= TestTrue(TEXT("Lethal hit mutates the item in the accepted faint checkpoint"),
				Result.WasAccepted()
					&& Removed != INDEX_NONE
					&& Fainted != INDEX_NONE
					&& IsReturnedResolutionAppendedExactlyOnce(*Lethal, Result));
			const FBattleHeldItemInstanceState* RemovedItem = FindLedgerItem(
				*Lethal,
				FBattleItemRules::GetChoiceBandId());
			bValid &= TestTrue(TEXT("Lethal Knock Off retains removal in the ledger"),
				RemovedItem != nullptr && RemovedItem->bTemporarilyRemoved
					&& Random != nullptr && Random->IsExact());
		}
	}
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC10HeldItemMovesTrickAtomicTest,
	"PokemonSolarus.Battle.C08C.C10HeldItemMoves.Trick.AtomicSwapOneEmptyAndFailures",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC10HeldItemMovesTrickAtomicTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	bool bValid = true;
	const FBattleMoveDefinition Move = MakeTrickMove();
	EBattleEffectExecutorError Error = EBattleEffectExecutorError::None;

	{
		TUniquePtr<FBattleEngine> BothHeld;
		FStrictBattleRandom* Random = nullptr;
		FBattleEffectExecutionResult Result;
		bValid &= TestTrue(TEXT("Both-held Trick engine is created"),
			TryMakeEngine(Move, FBattleItemRules::GetLeftoversId(),
				FBattleItemRules::GetChoiceBandId(), 200, 200, false,
				{}, BothHeld, Random));
		FBattleEngineState* State = BothHeld.IsValid()
			? &FBattleC10HeldItemMovesFixture::GetMutableState(*BothHeld)
			: nullptr;
		FBattleBattlerState* User = State != nullptr
			? State->FindMutableBattler(UserId())
			: nullptr;
		FBattleBattlerState* Target = State != nullptr
			? State->FindMutableBattler(TargetId())
			: nullptr;
		if (User != nullptr && Target != nullptr)
		{
			User->HeldItem.ChoiceLockedMoveId = MoveId(TEXT("Move.C10R5.UserLock"));
			Target->HeldItem.ChoiceLockedMoveId = MoveId(TEXT("Move.C10R5.TargetLock"));
		}
		const int32 UserHooksBefore = BothHeld.IsValid()
			? CountItemHooks(
				*BothHeld,
				FBattleItemRules::GetLeftoversId(),
				UserId())
			: 0;
		const int32 TargetHooksBefore = BothHeld.IsValid()
			? CountItemHooks(
				*BothHeld,
				FBattleItemRules::GetChoiceBandId(),
				TargetId())
			: 0;
		bValid &= TestTrue(TEXT("Both-held Trick executes atomically"),
			BothHeld.IsValid()
				&& TryExecuteDirect(*BothHeld, Move.Id, Result, Error));
		const int32 FirstTransfer = FindExecutionEvent(
			Result,
			EBattleEventType::ItemTransferred);
		const int32 SecondTransfer = FindExecutionEvent(
			Result,
			EBattleEventType::ItemTransferred,
			FirstTransfer + 1);
		bValid &= TestTrue(TEXT("Trick publishes both exact transfers in source order"),
			FirstTransfer != INDEX_NONE
				&& SecondTransfer == FirstTransfer + 1
				&& IsMutationEvent(
					Result.Events[FirstTransfer],
					EBattleEventType::ItemTransferred,
					FBattleItemRules::GetLeftoversId(),
					UserId(),
					TargetId(),
					1,
					1,
					0)
				&& IsMutationEvent(
					Result.Events[SecondTransfer],
					EBattleEventType::ItemTransferred,
					FBattleItemRules::GetChoiceBandId(),
					TargetId(),
					UserId(),
					1,
					1,
					0));
		const FBattleBattlerState* SwappedUser = BothHeld.IsValid()
			? FindBattler(*BothHeld, UserId())
			: nullptr;
		const FBattleBattlerState* SwappedTarget = BothHeld.IsValid()
			? FindBattler(*BothHeld, TargetId())
			: nullptr;
		const FBattleHeldItemInstanceState* SwappedLeftovers = BothHeld.IsValid()
			? FindLedgerItem(*BothHeld, FBattleItemRules::GetLeftoversId())
			: nullptr;
		const FBattleHeldItemInstanceState* SwappedChoiceBand = BothHeld.IsValid()
			? FindLedgerItem(*BothHeld, FBattleItemRules::GetChoiceBandId())
			: nullptr;
		bValid &= TestTrue(TEXT("Trick swaps mirrors and clears both Choice locks"),
			SwappedUser != nullptr && SwappedTarget != nullptr
				&& SwappedUser->HeldItem.CurrentItemId
					== FBattleItemRules::GetChoiceBandId()
				&& SwappedTarget->HeldItem.CurrentItemId
					== FBattleItemRules::GetLeftoversId()
				&& !SwappedUser->HeldItem.ChoiceLockedMoveId.IsValid()
				&& !SwappedTarget->HeldItem.ChoiceLockedMoveId.IsValid());
		bValid &= TestTrue(TEXT("Trick synchronizes reveal state in both mirrors and ledger entries"),
			SwappedUser != nullptr && SwappedTarget != nullptr
				&& SwappedLeftovers != nullptr && SwappedChoiceBand != nullptr
				&& SwappedUser->HeldItem.bRevealed
				&& SwappedTarget->HeldItem.bRevealed
				&& SwappedLeftovers->bRevealed
				&& SwappedChoiceBand->bRevealed);
		bValid &= TestTrue(TEXT("Trick moves hook ownership without duplicates"),
			UserHooksBefore > 0 && TargetHooksBefore > 0
				&& CountItemHooks(
					*BothHeld,
					FBattleItemRules::GetLeftoversId(),
					UserId()) == 0
				&& CountItemHooks(
					*BothHeld,
					FBattleItemRules::GetChoiceBandId(),
					TargetId()) == 0
				&& CountItemHooks(
					*BothHeld,
					FBattleItemRules::GetLeftoversId(),
					TargetId()) == UserHooksBefore
				&& CountItemHooks(
					*BothHeld,
					FBattleItemRules::GetChoiceBandId(),
					UserId()) == TargetHooksBefore);
		bValid &= TestTrue(TEXT("Trick reveals each item for both old and new holders"),
			HasBeenRevealed(
				*BothHeld,
				FBattleItemRules::GetLeftoversId(),
				UserId())
				&& HasBeenRevealed(
					*BothHeld,
					FBattleItemRules::GetLeftoversId(),
					TargetId())
				&& HasBeenRevealed(
					*BothHeld,
					FBattleItemRules::GetChoiceBandId(),
					TargetId())
				&& HasBeenRevealed(
					*BothHeld,
					FBattleItemRules::GetChoiceBandId(),
					UserId()));
		TArray<FBattleFinalHeldItemFact> FinalFacts;
		bValid &= TestTrue(TEXT("Trick final facts build"),
			TryBuildFinalFacts(*BothHeld, FinalFacts));
		const FBattleFinalHeldItemFact* UserFinal = FindFinalFact(
			FinalFacts,
			FBattleItemRules::GetLeftoversId());
		const FBattleFinalHeldItemFact* TargetFinal = FindFinalFact(
			FinalFacts,
			FBattleItemRules::GetChoiceBandId());
		bValid &= TestTrue(TEXT("Trick final facts restore both original owners"),
			UserFinal != nullptr && TargetFinal != nullptr
				&& UserFinal->FinalOwnerBattlerId == UserId()
				&& TargetFinal->FinalOwnerBattlerId == TargetId()
				&& Random != nullptr && Random->IsExact()
				&& Random->GetTrace().IsEmpty());
	}

	struct FOneEmptyCase
	{
		FItemId UserItem;
		FItemId TargetItem;
		FItemId MovedItem;
		FBattlerId Source;
		FBattlerId Destination;
	};
	const TArray<FOneEmptyCase> OneEmptyCases =
	{
		{FBattleItemRules::GetLeftoversId(), FItemId(),
			FBattleItemRules::GetLeftoversId(), UserId(), TargetId()},
		{FItemId(), FBattleItemRules::GetChoiceBandId(),
			FBattleItemRules::GetChoiceBandId(), TargetId(), UserId()}
	};
	for (const FOneEmptyCase& Case : OneEmptyCases)
	{
		TUniquePtr<FBattleEngine> Engine;
		FStrictBattleRandom* Random = nullptr;
		FBattleEffectExecutionResult Result;
		bValid &= TestTrue(TEXT("One-empty Trick engine is created"),
			TryMakeEngine(Move, Case.UserItem, Case.TargetItem,
				200, 200, false, {}, Engine, Random));
		bValid &= TestTrue(TEXT("One-empty Trick transfers the only item"),
			Engine.IsValid() && TryExecuteDirect(*Engine, Move.Id, Result, Error));
		const int32 Transfer = FindExecutionEvent(
			Result,
			EBattleEventType::ItemTransferred);
		bValid &= TestTrue(TEXT("One-empty Trick emits exactly one typed transfer"),
			Transfer != INDEX_NONE
				&& FindExecutionEvent(
					Result,
					EBattleEventType::ItemTransferred,
					Transfer + 1) == INDEX_NONE
				&& IsMutationEvent(
					Result.Events[Transfer],
					EBattleEventType::ItemTransferred,
					Case.MovedItem,
					Case.Source,
					Case.Destination,
					1,
					1,
					0)
				&& FindBattler(*Engine, Case.Destination)->HeldItem.CurrentItemId
					== Case.MovedItem
				&& !FindBattler(*Engine, Case.Source)->HeldItem.CurrentItemId.IsValid()
				&& Random != nullptr && Random->IsExact());
	}

	struct FFailureCase
	{
		FItemId UserItem;
		FItemId TargetItem;
	};
	const TArray<FFailureCase> FailureCases =
	{
		{FItemId(), FItemId()},
		{UnremovableItemId(), FBattleItemRules::GetLeftoversId()},
		{FBattleItemRules::GetLeftoversId(), UnremovableItemId()}
	};
	for (const FFailureCase& Case : FailureCases)
	{
		TUniquePtr<FBattleEngine> Engine;
		FStrictBattleRandom* Random = nullptr;
		FBattleEffectExecutionResult Result;
		bValid &= TestTrue(TEXT("Failed Trick engine is created"),
			TryMakeEngine(Move, Case.UserItem, Case.TargetItem,
				200, 200, false, {}, Engine, Random));
		const TArray<FBattleHeldItemInstanceState> Before = CopyLedger(*Engine);
		const FBattleHeldItemState UserBefore = FindBattler(*Engine, UserId())->HeldItem;
		const FBattleHeldItemState TargetBefore = FindBattler(*Engine, TargetId())->HeldItem;
		bValid &= TestTrue(TEXT("Failed Trick remains a valid gameplay resolution"),
			TryExecuteDirect(*Engine, Move.Id, Result, Error));
		bValid &= TestTrue(TEXT("Failed Trick changes neither item atomically"),
			FindExecutionEvent(Result, EBattleEventType::EffectFailed) != INDEX_NONE
				&& FindExecutionEvent(Result, EBattleEventType::ItemTransferred)
					== INDEX_NONE
				&& IsLedgerIdentical(*Engine, Before)
				&& AreActionStartHeldItemsIdentical(
					FindBattler(*Engine, UserId())->HeldItem,
					UserBefore)
				&& AreActionStartHeldItemsIdentical(
					FindBattler(*Engine, TargetId())->HeldItem,
					TargetBefore)
				&& Random != nullptr && Random->IsExact());
	}

	{
		TUniquePtr<FBattleEngine> Protected;
		FStrictBattleRandom* Random = nullptr;
		FBattleEffectExecutionResult Result;
		bValid &= TestTrue(TEXT("Protected Trick engine is created"),
			TryMakeEngine(Move, FBattleItemRules::GetLeftoversId(),
				FBattleItemRules::GetChoiceBandId(), 200, 200, false,
				{}, Protected, Random));
		const TArray<FBattleHeldItemInstanceState> Before = CopyLedger(*Protected);
		bValid &= TestTrue(TEXT("Protect is seeded for Trick"),
			TrySeedProtect(*Protected));
		bValid &= TestTrue(TEXT("Protect gates Trick before either transfer"),
			TryExecuteDirect(*Protected, Move.Id, Result, Error)
				&& FindExecutionEvent(Result, EBattleEventType::Protected)
					!= INDEX_NONE
				&& FindExecutionEvent(Result, EBattleEventType::ItemTransferred)
					== INDEX_NONE
				&& IsLedgerIdentical(*Protected, Before)
				&& Random != nullptr && Random->IsExact());
	}
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC10HeldItemMovesThiefTransferTest,
	"PokemonSolarus.Battle.C08C.C10HeldItemMoves.Thief.TransferGatesLethalAndLifeOrb",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC10HeldItemMovesThiefTransferTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	bool bValid = true;
	const FBattleMoveDefinition Move = MakeThiefMove();
	const TArray<FBattleExpectedRandomDraw> DamageDraw =
	{
		{0, 15, 0, FBattleEffectExecutor::GetDamageRandomRulePurpose()}
	};
	EBattleEffectExecutorError Error = EBattleEffectExecutorError::None;

	{
		TUniquePtr<FBattleEngine> Transfer;
		FStrictBattleRandom* Random = nullptr;
		FBattleEffectExecutionResult Result;
		bValid &= TestTrue(TEXT("Thief transfer engine is created"),
			TryMakeEngine(Move, FItemId(), FBattleItemRules::GetLeftoversId(),
				200, 200, false, DamageDraw, Transfer, Random));
		const int32 HooksBefore = Transfer.IsValid()
			? CountItemHooks(
				*Transfer,
				FBattleItemRules::GetLeftoversId(),
				TargetId())
			: 0;
		bValid &= TestTrue(TEXT("Thief transfers a takeable target item"),
			Transfer.IsValid()
				&& TryExecuteDirect(*Transfer, Move.Id, Result, Error));
		const int32 EventIndex = FindExecutionEvent(
			Result,
			EBattleEventType::ItemTransferred);
		bValid &= TestTrue(TEXT("Thief publishes one exact post-damage transfer"),
			EventIndex > FindExecutionEvent(Result, EBattleEventType::HPChanged)
				&& IsMutationEvent(
					Result.Events[EventIndex],
					EBattleEventType::ItemTransferred,
					FBattleItemRules::GetLeftoversId(),
					TargetId(),
					UserId(),
					1,
					1,
					0));
		const FBattleBattlerState* User = FindBattler(*Transfer, UserId());
		const FBattleBattlerState* Target = FindBattler(*Transfer, TargetId());
		const FBattleHeldItemInstanceState* TransferredItem = FindLedgerItem(
			*Transfer,
			FBattleItemRules::GetLeftoversId());
		bValid &= TestTrue(TEXT("Thief mirrors one transient ownership transfer"),
			User != nullptr && Target != nullptr
				&& User->HeldItem.CurrentItemId
					== FBattleItemRules::GetLeftoversId()
				&& !Target->HeldItem.CurrentItemId.IsValid()
				&& !Target->HeldItem.InstanceId.IsValid());
		bValid &= TestTrue(TEXT("Thief synchronizes reveal state in the new mirror and ledger"),
			User != nullptr && TransferredItem != nullptr
				&& User->HeldItem.bRevealed
				&& TransferredItem->bRevealed);
		bValid &= TestTrue(TEXT("Thief moves hook ownership exactly once"),
			HooksBefore > 0
				&& CountItemHooks(
					*Transfer,
					FBattleItemRules::GetLeftoversId(),
					TargetId()) == 0
				&& CountItemHooks(
					*Transfer,
					FBattleItemRules::GetLeftoversId(),
					UserId()) == HooksBefore);
		bValid &= TestTrue(TEXT("Thief reveals the item for old and new holders"),
			HasBeenRevealed(
				*Transfer,
				FBattleItemRules::GetLeftoversId(),
				TargetId())
				&& HasBeenRevealed(
					*Transfer,
					FBattleItemRules::GetLeftoversId(),
					UserId()));
		TArray<FBattleFinalHeldItemFact> FinalFacts;
		bValid &= TestTrue(TEXT("Thief final facts build"),
			TryBuildFinalFacts(*Transfer, FinalFacts));
		const FBattleFinalHeldItemFact* Final = FindFinalFact(
			FinalFacts,
			FBattleItemRules::GetLeftoversId());
		bValid &= TestTrue(TEXT("Thief final facts preserve original ownership"),
			Final != nullptr
				&& Final->Disposition
					== EBattleHeldItemFinalDisposition::OriginalOwner
				&& Final->FinalOwnerBattlerId == TargetId()
				&& Random != nullptr && Random->IsExact());
	}

	struct FPreventedCase
	{
		FItemId UserItem;
		FItemId TargetItem;
	};
	const TArray<FPreventedCase> PreventedCases =
	{
		{FBattleItemRules::GetChoiceBandId(), FBattleItemRules::GetLeftoversId()},
		{FItemId(), UnremovableItemId()},
		{FItemId(), FItemId()}
	};
	for (const FPreventedCase& Case : PreventedCases)
	{
		TUniquePtr<FBattleEngine> Engine;
		FStrictBattleRandom* Random = nullptr;
		FBattleEffectExecutionResult Result;
		bValid &= TestTrue(TEXT("Prevented Thief engine is created"),
			TryMakeEngine(Move, Case.UserItem, Case.TargetItem,
				200, 200, false, DamageDraw, Engine, Random));
		const FBattleHeldItemState UserBefore = FindBattler(*Engine, UserId())->HeldItem;
		const FBattleHeldItemState TargetBefore = FindBattler(*Engine, TargetId())->HeldItem;
		bValid &= TestTrue(TEXT("Occupied, empty-target, or unremovable Thief resolves damage"),
			TryExecuteDirect(*Engine, Move.Id, Result, Error));
		const FBattleHeldItemState& UserAfter =
			FindBattler(*Engine, UserId())->HeldItem;
		const FBattleHeldItemState& TargetAfter =
			FindBattler(*Engine, TargetId())->HeldItem;
		bValid &= TestTrue(TEXT("Prevented Thief retains both item ownership records"),
			FindExecutionEvent(Result, EBattleEventType::EffectPrevented) != INDEX_NONE
				&& FindExecutionEvent(Result, EBattleEventType::ItemTransferred)
					== INDEX_NONE
				&& UserAfter.InstanceId == UserBefore.InstanceId
				&& UserAfter.OriginalItemId == UserBefore.OriginalItemId
				&& UserAfter.CurrentItemId == UserBefore.CurrentItemId
				&& UserAfter.bConsumed == UserBefore.bConsumed
				&& UserAfter.bSuppressed == UserBefore.bSuppressed
				&& UserAfter.bTemporarilyRemoved
					== UserBefore.bTemporarilyRemoved
				&& TargetAfter.InstanceId == TargetBefore.InstanceId
				&& TargetAfter.OriginalItemId == TargetBefore.OriginalItemId
				&& TargetAfter.CurrentItemId == TargetBefore.CurrentItemId
				&& TargetAfter.bConsumed == TargetBefore.bConsumed
				&& TargetAfter.bSuppressed == TargetBefore.bSuppressed
				&& TargetAfter.bTemporarilyRemoved
					== TargetBefore.bTemporarilyRemoved
				&& Random != nullptr && Random->IsExact());
	}

	{
		TUniquePtr<FBattleEngine> Protected;
		FStrictBattleRandom* Random = nullptr;
		FBattleEffectExecutionResult Result;
		bValid &= TestTrue(TEXT("Protected Thief engine is created"),
			TryMakeEngine(Move, FItemId(), FBattleItemRules::GetLeftoversId(),
				200, 200, false, {}, Protected, Random));
		bValid &= TestTrue(TEXT("Protect is seeded for Thief"),
			TrySeedProtect(*Protected));
		bValid &= TestTrue(TEXT("Protect gates Thief before RNG and transfer"),
			TryExecuteDirect(*Protected, Move.Id, Result, Error)
				&& FindExecutionEvent(Result, EBattleEventType::Protected)
					!= INDEX_NONE
				&& FindExecutionEvent(Result, EBattleEventType::ItemTransferred)
					== INDEX_NONE
				&& Random != nullptr && Random->IsExact()
				&& Random->GetTrace().IsEmpty());
	}

	{
		const FBattleMoveDefinition MissMove = MakeThiefMove(40, false, 50);
		TUniquePtr<FBattleEngine> Miss;
		FStrictBattleRandom* Random = nullptr;
		FBattleEffectExecutionResult Result;
		bValid &= TestTrue(TEXT("Missed Thief engine is created"),
			TryMakeEngine(MissMove, FItemId(), FBattleItemRules::GetLeftoversId(),
				200, 200, false,
				{{0, 99, 50, FBattleEffectExecutor::GetAccuracyRulePurpose()}},
				Miss, Random));
		bValid &= TestTrue(TEXT("Miss retains target item and consumes accuracy only"),
			TryExecuteDirect(*Miss, MissMove.Id, Result, Error)
				&& FindExecutionEvent(Result, EBattleEventType::Missed) != INDEX_NONE
				&& FindExecutionEvent(Result, EBattleEventType::ItemTransferred)
					== INDEX_NONE
				&& FindBattler(*Miss, TargetId())->HeldItem.CurrentItemId
					== FBattleItemRules::GetLeftoversId()
				&& Random != nullptr && Random->IsExact());
	}

	{
		TUniquePtr<FBattleEngine> Immune;
		FStrictBattleRandom* Random = nullptr;
		FBattleEffectExecutionResult Result;
		bValid &= TestTrue(TEXT("Immune Thief engine is created"),
			TryMakeEngine(Move, FItemId(), FBattleItemRules::GetLeftoversId(),
				200, 200, true, {}, Immune, Random));
		bValid &= TestTrue(TEXT("Immunity gates Thief before RNG and transfer"),
			TryExecuteDirect(*Immune, Move.Id, Result, Error)
				&& FindExecutionEvent(Result, EBattleEventType::Immunity) != INDEX_NONE
				&& FindExecutionEvent(Result, EBattleEventType::ItemTransferred)
					== INDEX_NONE
				&& Random != nullptr && Random->IsExact()
				&& Random->GetTrace().IsEmpty());
	}

	{
		TUniquePtr<FBattleEngine> Substitute;
		FStrictBattleRandom* Random = nullptr;
		FBattleEffectExecutionResult Result;
		bValid &= TestTrue(TEXT("Substitute Thief engine is created"),
			TryMakeEngine(Move, FItemId(), FBattleItemRules::GetLeftoversId(),
				200, 200, false, DamageDraw, Substitute, Random));
		bValid &= TestTrue(TEXT("Substitute is seeded for Thief"),
			TrySeedActionStartVolatile(
				*Substitute,
				TargetId(),
				FBattleVolatileRules::GetSubstituteId()));
		bValid &= TestTrue(TEXT("Substitute blocks Thief transfer"),
			TryExecuteDirect(*Substitute, Move.Id, Result, Error)
				&& FindExecutionEvent(Result, EBattleEventType::ItemTransferred)
					== INDEX_NONE
				&& FindBattler(*Substitute, TargetId())->HeldItem.CurrentItemId
					== FBattleItemRules::GetLeftoversId()
				&& Random != nullptr && Random->IsExact());
	}

	{
		TUniquePtr<FBattleEngine> LifeOrb;
		FStrictBattleRandom* Random = nullptr;
		FBattleEffectExecutionResult Result;
		bValid &= TestTrue(TEXT("Life Orb Thief engine is created"),
			TryMakeEngine(Move, FItemId(), FBattleItemRules::GetLifeOrbId(),
				200, 200, false, DamageDraw, LifeOrb, Random));
		const int32 UserHPBefore = FindBattler(*LifeOrb, UserId())->CurrentHP;
		bValid &= TestTrue(TEXT("Thief steals Life Orb"),
			TryExecuteDirect(*LifeOrb, Move.Id, Result, Error));
		bValid &= TestTrue(
			TEXT("A stolen Life Orb causes no same-action boost qualification or recoil"),
			FindBattler(*LifeOrb, UserId())->HeldItem.CurrentItemId
					== FBattleItemRules::GetLifeOrbId()
				&& FindBattler(*LifeOrb, UserId())->CurrentHP == UserHPBefore
				&& !HasExecutionEventTarget(
					Result,
					EBattleEventType::Damage,
					UserId())
				&& Random != nullptr && Random->IsExact());
	}

	{
		TUniquePtr<FBattleEngine> Suppressed;
		FStrictBattleRandom* Random = nullptr;
		FBattleEffectExecutionResult Result;
		bValid &= TestTrue(TEXT("Suppressed starting-Life-Orb engine is created"),
			TryMakeEngine(
				Move,
				FBattleItemRules::GetLifeOrbId(),
				FBattleItemRules::GetLeftoversId(),
				200,
				200,
				false,
				DamageDraw,
				Suppressed,
				Random));
		bValid &= TestTrue(TEXT("Starting Life Orb is suppressed before the move"),
			Suppressed.IsValid()
				&& FBattleC10HeldItemMovesFixture::SuppressHeldItem(
					*Suppressed,
					UserId()));
		bValid &= TestTrue(
			TEXT("Suppressed starting Life Orb neither qualifies nor recoils"),
			Suppressed.IsValid()
				&& TryExecuteDirect(*Suppressed, Move.Id, Result, Error)
				&& FindBattler(*Suppressed, UserId())->CurrentHP == 200
				&& !HasExecutionEventTarget(
					Result,
					EBattleEventType::Damage,
					UserId())
				&& FindExecutionEvent(Result, EBattleEventType::ItemActivated)
					== INDEX_NONE
				&& Random != nullptr && Random->IsExact());
	}

	{
		const FBattleMoveDefinition MissMove = MakeThiefMove(40, false, 50);
		TUniquePtr<FBattleEngine> Miss;
		FStrictBattleRandom* Random = nullptr;
		FBattleEffectExecutionResult Result;
		bValid &= TestTrue(TEXT("Missed starting-Life-Orb engine is created"),
			TryMakeEngine(
				MissMove,
				FBattleItemRules::GetLifeOrbId(),
				FBattleItemRules::GetLeftoversId(),
				200,
				200,
				false,
				{{0, 99, 50, FBattleEffectExecutor::GetAccuracyRulePurpose()}},
				Miss,
				Random));
		bValid &= TestTrue(TEXT("A miss never qualifies starting Life Orb recoil"),
			Miss.IsValid()
				&& TryExecuteDirect(*Miss, MissMove.Id, Result, Error)
				&& FindExecutionEvent(Result, EBattleEventType::Missed) != INDEX_NONE
				&& FindBattler(*Miss, UserId())->CurrentHP == 200
				&& !HasExecutionEventTarget(
					Result,
					EBattleEventType::Damage,
					UserId())
				&& Random != nullptr && Random->IsExact());
	}

	{
		TUniquePtr<FBattleEngine> Immune;
		FStrictBattleRandom* Random = nullptr;
		FBattleEffectExecutionResult Result;
		bValid &= TestTrue(TEXT("Immune starting-Life-Orb engine is created"),
			TryMakeEngine(
				Move,
				FBattleItemRules::GetLifeOrbId(),
				FBattleItemRules::GetLeftoversId(),
				200,
				200,
				true,
				{},
				Immune,
				Random));
		bValid &= TestTrue(TEXT("Immunity never qualifies starting Life Orb recoil"),
			Immune.IsValid()
				&& TryExecuteDirect(*Immune, Move.Id, Result, Error)
				&& FindExecutionEvent(Result, EBattleEventType::Immunity) != INDEX_NONE
				&& FindBattler(*Immune, UserId())->CurrentHP == 200
				&& !HasExecutionEventTarget(
					Result,
					EBattleEventType::Damage,
					UserId())
				&& Random != nullptr && Random->IsExact()
				&& Random->GetTrace().IsEmpty());
	}

	{
		TUniquePtr<FBattleEngine> Protected;
		FStrictBattleRandom* Random = nullptr;
		FBattleEffectExecutionResult Result;
		bValid &= TestTrue(TEXT("Protected starting-Life-Orb engine is created"),
			TryMakeEngine(
				Move,
				FBattleItemRules::GetLifeOrbId(),
				FBattleItemRules::GetLeftoversId(),
				200,
				200,
				false,
				{},
				Protected,
				Random));
		bValid &= TestTrue(TEXT("Protect is seeded for starting Life Orb"),
			Protected.IsValid() && TrySeedProtect(*Protected));
		bValid &= TestTrue(
			TEXT("No affected different battler means no starting Life Orb recoil"),
			Protected.IsValid()
				&& TryExecuteDirect(*Protected, Move.Id, Result, Error)
				&& FindExecutionEvent(Result, EBattleEventType::Protected) != INDEX_NONE
				&& FindBattler(*Protected, UserId())->CurrentHP == 200
				&& !HasExecutionEventTarget(
					Result,
					EBattleEventType::Damage,
					UserId())
				&& Random != nullptr && Random->IsExact()
				&& Random->GetTrace().IsEmpty());
	}

	{
		const FBattleMoveDefinition LethalMove = MakeThiefMove(400);
		TUniquePtr<FBattleEngine> Lethal;
		FStrictBattleRandom* Random = nullptr;
		bValid &= TestTrue(TEXT("Lethal Thief engine is created"),
			TryMakeEngine(LethalMove, FItemId(),
				FBattleItemRules::GetLeftoversId(), 200, 1, false,
				DamageDraw, Lethal, Random));
		bValid &= TestTrue(TEXT("Lethal Thief reaches effects"),
			TryPrepareEffectsCheckpoint(*Lethal, LethalMove.Id));
		const FBattleResolution Result = Lethal->ExecuteCurrentMoveEffects();
		const int32 Transfer = FindResolutionEvent(
			Result,
			EBattleEventType::ItemTransferred);
		const int32 Fainted = FindResolutionEvent(Result, EBattleEventType::Fainted);
		bValid &= TestTrue(TEXT("Lethal Thief transfers in the accepted faint checkpoint"),
			Result.WasAccepted()
				&& Transfer != INDEX_NONE && Fainted != INDEX_NONE
				&& FindBattler(*Lethal, UserId())->HeldItem.CurrentItemId
					== FBattleItemRules::GetLeftoversId()
				&& Random != nullptr && Random->IsExact()
				&& IsReturnedResolutionAppendedExactlyOnce(*Lethal, Result));
	}
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC10HeldItemMovesRecycleHistoryTest,
	"PokemonSolarus.Battle.C08C.C10HeldItemMoves.Recycle.HistoryOccupiedMagicRoomAndFinalFacts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC10HeldItemMovesRecycleHistoryTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	bool bValid = true;
	const FBattleMoveDefinition Move = MakeRecycleMove();
	EBattleEffectExecutorError Error = EBattleEffectExecutorError::None;

	{
		TUniquePtr<FBattleEngine> Latest;
		FStrictBattleRandom* Random = nullptr;
		FBattleEffectExecutionResult Result;
		bValid &= TestTrue(TEXT("Latest-history Recycle engine is created"),
			TryMakeEngine(Move, FItemId(), FItemId(),
				200, 200, false, {}, Latest, Random));
		bValid &= TestTrue(TEXT("Two consumed generated histories are seeded"),
			Latest.IsValid()
				&& FBattleC10HeldItemMovesFixture::ReplaceWithConsumedHistory(
					*Latest,
					{{FBattleItemRules::GetLeftoversId(), 3},
					 {FBattleItemRules::GetChoiceBandId(), 7}}));
		bValid &= TestTrue(TEXT("Recycle restores the latest consumed item"),
			Latest.IsValid()
				&& TryExecuteDirect(*Latest, Move.Id, Result, Error));
		const int32 Restored = FindExecutionEvent(
			Result,
			EBattleEventType::ItemRestored);
		bValid &= TestTrue(TEXT("Recycle publishes one exact self restoration"),
			Restored != INDEX_NONE
				&& IsMutationEvent(
					Result.Events[Restored],
					EBattleEventType::ItemRestored,
					FBattleItemRules::GetChoiceBandId(),
					UserId(),
					UserId(),
					0,
					1,
					1));
		const FBattleBattlerState* User = FindBattler(*Latest, UserId());
		const FBattleHeldItemInstanceState* LatestItem = FindLedgerItem(
			*Latest,
			FBattleItemRules::GetChoiceBandId());
		const FBattleHeldItemInstanceState* OlderItem = FindLedgerItem(
			*Latest,
			FBattleItemRules::GetLeftoversId());
		bValid &= TestTrue(TEXT("Latest history owns the mirror; older history stays consumed"),
			User != nullptr && LatestItem != nullptr && OlderItem != nullptr
				&& User->HeldItem.CurrentItemId
					== FBattleItemRules::GetChoiceBandId()
				&& LatestItem->bRestoredAfterConsumption
				&& !LatestItem->bConsumed
				&& OlderItem->bConsumed
				&& !User->HeldItem.ChoiceLockedMoveId.IsValid());
		bValid &= TestTrue(TEXT("Recycle synchronizes reveal state in the restored mirror and ledger"),
			User != nullptr && LatestItem != nullptr
				&& User->HeldItem.bRevealed
				&& LatestItem->bRevealed);
		bValid &= TestTrue(TEXT("Recycle registers and reveals restored item hooks"),
			CountItemHooks(
				*Latest,
				FBattleItemRules::GetChoiceBandId(),
				UserId()) > 0
				&& HasBeenRevealed(
					*Latest,
					FBattleItemRules::GetChoiceBandId(),
					UserId()));
		TArray<FBattleFinalHeldItemFact> FinalFacts;
		bValid &= TestTrue(TEXT("Recycle final facts build"),
			TryBuildFinalFacts(*Latest, FinalFacts));
		const FBattleFinalHeldItemFact* Final = FindFinalFact(
			FinalFacts,
			FBattleItemRules::GetChoiceBandId());
		bValid &= TestTrue(TEXT("Generated restored item remains battle-local at battle end"),
			Final != nullptr
				&& Final->Disposition
					== EBattleHeldItemFinalDisposition::BattleGeneratedRemoved
				&& Final->bRestoredAfterConsumption
				&& Random != nullptr && Random->IsExact()
				&& Random->GetTrace().IsEmpty());
	}

	{
		TUniquePtr<FBattleEngine> NoHistory;
		FStrictBattleRandom* Random = nullptr;
		FBattleEffectExecutionResult Result;
		bValid &= TestTrue(TEXT("No-history Recycle engine is created"),
			TryMakeEngine(Move, FItemId(), FItemId(),
				200, 200, false, {}, NoHistory, Random));
		const TArray<FBattleHeldItemInstanceState> Before = CopyLedger(*NoHistory);
		bValid &= TestTrue(TEXT("Recycle without history is a gameplay failure"),
			TryExecuteDirect(*NoHistory, Move.Id, Result, Error));
		bValid &= TestTrue(TEXT("No-history Recycle changes no item state"),
			FindExecutionEvent(Result, EBattleEventType::EffectFailed) != INDEX_NONE
				&& FindExecutionEvent(Result, EBattleEventType::ItemRestored)
					== INDEX_NONE
				&& IsLedgerIdentical(*NoHistory, Before)
				&& Random != nullptr && Random->IsExact());
	}

	{
		TUniquePtr<FBattleEngine> Occupied;
		FStrictBattleRandom* Random = nullptr;
		FBattleEffectExecutionResult Result;
		bValid &= TestTrue(TEXT("Occupied Recycle engine is created"),
			TryMakeEngine(Move, FBattleItemRules::GetLeftoversId(), FItemId(),
				200, 200, false, {}, Occupied, Random));
		bValid &= TestTrue(TEXT("Occupied holder receives a consumed history"),
			FBattleC10HeldItemMovesFixture::ReplaceWithConsumedHistory(
				*Occupied,
				{{FBattleItemRules::GetChoiceBandId(), 4}}));
		const TArray<FBattleHeldItemInstanceState> Before = CopyLedger(*Occupied);
		bValid &= TestTrue(TEXT("Occupied Recycle is a gameplay failure"),
			TryExecuteDirect(*Occupied, Move.Id, Result, Error));
		bValid &= TestTrue(TEXT("Occupied Recycle preserves held and consumed items"),
			FindExecutionEvent(Result, EBattleEventType::EffectFailed) != INDEX_NONE
				&& FindExecutionEvent(Result, EBattleEventType::ItemRestored)
					== INDEX_NONE
				&& FindBattler(*Occupied, UserId())->HeldItem.CurrentItemId
					== FBattleItemRules::GetLeftoversId()
				&& IsLedgerIdentical(*Occupied, Before)
				&& Random != nullptr && Random->IsExact());
	}

	{
		TUniquePtr<FBattleEngine> MagicRoom;
		FStrictBattleRandom* Random = nullptr;
		FBattleEffectExecutionResult Result;
		bValid &= TestTrue(TEXT("Magic Room Recycle engine is created"),
			TryMakeEngine(Move, FItemId(), FItemId(),
				200, 200, false, {}, MagicRoom, Random));
		bValid &= TestTrue(TEXT("Consumed history and Magic Room are seeded"),
			FBattleC10HeldItemMovesFixture::ReplaceWithConsumedHistory(
				*MagicRoom,
				{{FBattleItemRules::GetLeftoversId(), 5}})
				&& FBattleC10HeldItemMovesFixture::AddMagicRoom(*MagicRoom));
		bValid &= TestTrue(TEXT("Recycle restores under Magic Room"),
			TryExecuteDirect(*MagicRoom, Move.Id, Result, Error));
		const FBattleBattlerState* User = FindBattler(*MagicRoom, UserId());
		const int32 HookCount = CountItemHooks(
			*MagicRoom,
			FBattleItemRules::GetLeftoversId(),
			UserId());
		bValid &= TestTrue(TEXT("Magic Room restores a suppressed mirror and hooks"),
			User != nullptr
				&& User->HeldItem.CurrentItemId
					== FBattleItemRules::GetLeftoversId()
				&& User->HeldItem.bSuppressed
				&& HookCount > 0
				&& CountItemHooks(
					*MagicRoom,
					FBattleItemRules::GetLeftoversId(),
					UserId(),
					true) == HookCount
				&& FindExecutionEvent(Result, EBattleEventType::ItemRestored)
					!= INDEX_NONE
				&& Random != nullptr && Random->IsExact());
	}
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC10HeldItemMovesRecycleSitrusRepeatTest,
	"PokemonSolarus.Battle.C08C.C10HeldItemMoves.Recycle.SitrusImmediateConsumeAndRepeat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC10HeldItemMovesRecycleSitrusRepeatTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	const FBattleMoveDefinition Move = MakeRecycleMove();
	TUniquePtr<FBattleEngine> Engine;
	FStrictBattleRandom* Random = nullptr;
	if (!TestTrue(TEXT("Sitrus Recycle engine is created"),
		TryMakeEngine(Move, FItemId(), FItemId(),
			50, 200, false, {}, Engine, Random))
		|| !TestTrue(TEXT("Consumed Sitrus history is seeded"),
		FBattleC10HeldItemMovesFixture::ReplaceWithConsumedHistory(
			*Engine,
			{{FBattleItemRules::GetSitrusBerryId(), 1}})))
	{
		return false;
	}
	EBattleEffectExecutorError Error = EBattleEffectExecutorError::None;
	FBattleEffectExecutionResult First;
	bool bValid = TestTrue(TEXT("First Sitrus Recycle executes"),
		TryExecuteDirect(*Engine, Move.Id, First, Error, 900));
	const int32 FirstRestored = FindExecutionEvent(
		First,
		EBattleEventType::ItemRestored);
	const int32 FirstActivated = FindExecutionEvent(
		First,
		EBattleEventType::ItemActivated);
	const int32 FirstConsumed = FindExecutionEvent(
		First,
		EBattleEventType::ItemConsumed);
	const int32 FirstHealing = FindExecutionEvent(First, EBattleEventType::Healing);
	const int32 FirstHP = FindExecutionEvent(First, EBattleEventType::HPChanged);
	bValid &= TestTrue(TEXT("Restored Sitrus immediately activates, consumes, and heals"),
		FirstRestored != INDEX_NONE
			&& FirstRestored < FirstActivated
			&& FirstActivated < FirstConsumed
			&& FirstConsumed < FirstHealing
			&& FirstHealing < FirstHP
			&& First.Events[FirstActivated].NumericBefore
				== TOptional<int64>(1)
			&& First.Events[FirstActivated].NumericAfter
				== TOptional<int64>(1)
			&& First.Events[FirstActivated].NumericDelta
				== TOptional<int64>(0));
	const FBattleBattlerState* User = FindBattler(*Engine, UserId());
	const FBattleHeldItemInstanceState* LedgerItem = FindLedgerItem(
		*Engine,
		FBattleItemRules::GetSitrusBerryId());
	bValid &= TestTrue(TEXT("Immediate Sitrus consumption synchronizes mirror and history"),
		User != nullptr && LedgerItem != nullptr
			&& User->CurrentHP == 100
			&& User->HeldItem.bConsumed
			&& !User->HeldItem.CurrentItemId.IsValid()
			&& LedgerItem->bConsumed
			&& LedgerItem->LastConsumptionFactOrdinal > 1
			&& CountItemHooks(
				*Engine,
				FBattleItemRules::GetSitrusBerryId(),
				UserId()) == 0);
	const uint64 FirstConsumptionOrdinal = LedgerItem != nullptr
		? LedgerItem->LastConsumptionFactOrdinal
		: 0;

	FBattleEffectExecutionResult Second;
	bValid &= TestTrue(TEXT("A newly consumed Sitrus can be recycled again"),
		TryExecuteDirect(*Engine, Move.Id, Second, Error, 901));
	const int32 SecondRestored = FindExecutionEvent(
		Second,
		EBattleEventType::ItemRestored);
	const int32 SecondConsumed = FindExecutionEvent(
		Second,
		EBattleEventType::ItemConsumed);
	const FBattleBattlerState* RepeatedUser = FindBattler(*Engine, UserId());
	const FBattleHeldItemInstanceState* RepeatedLedger = FindLedgerItem(
		*Engine,
		FBattleItemRules::GetSitrusBerryId());
	bValid &= TestTrue(TEXT("Repeated Recycle re-runs immediate consumption exactly once"),
		SecondRestored != INDEX_NONE
			&& SecondConsumed > SecondRestored
			&& RepeatedUser != nullptr
			&& RepeatedUser->CurrentHP == 150
			&& RepeatedUser->HeldItem.bConsumed
			&& RepeatedLedger != nullptr
			&& RepeatedLedger->bConsumed
			&& RepeatedLedger->LastConsumptionFactOrdinal
				> FirstConsumptionOrdinal
			&& Random != nullptr && Random->IsExact()
			&& Random->GetTrace().IsEmpty());
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC10HeldItemMovesReplayDeterminismTest,
	"PokemonSolarus.Battle.C08C.C10HeldItemMoves.Replay.Schema6ByteDeterminismAndNoExtraRNG",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC10HeldItemMovesReplayDeterminismTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	const FBattleMoveDefinition Move = MakeKnockOffMove();
	const TArray<FBattleExpectedRandomDraw> Draws =
	{
		{0, 15, 0, FBattleEffectExecutor::GetDamageRandomRulePurpose()}
	};
	TUniquePtr<FBattleEngine> First;
	TUniquePtr<FBattleEngine> Second;
	FStrictBattleRandom* FirstRandom = nullptr;
	FStrictBattleRandom* SecondRandom = nullptr;
	if (!TestTrue(TEXT("First held-item replay engine is created"),
		TryMakeEngine(Move, FItemId(), FBattleItemRules::GetChoiceBandId(),
			200, 200, false, Draws, First, FirstRandom))
		|| !TestTrue(TEXT("Second held-item replay engine is created"),
		TryMakeEngine(Move, FItemId(), FBattleItemRules::GetChoiceBandId(),
			200, 200, false, Draws, Second, SecondRandom))
		|| !TestTrue(TEXT("First held-item replay reaches effects"),
		TryPrepareEffectsCheckpoint(*First, Move.Id))
		|| !TestTrue(TEXT("Second held-item replay reaches effects"),
		TryPrepareEffectsCheckpoint(*Second, Move.Id)))
	{
		return false;
	}
	const FBattleResolution FirstResult = First->ExecuteCurrentMoveEffects();
	const FBattleResolution SecondResult = Second->ExecuteCurrentMoveEffects();
	const TArray<EBattleEventType> ExpectedEvents =
	{
		EBattleEventType::AccuracyChecked,
		EBattleEventType::CriticalChecked,
		EBattleEventType::RandomCheck,
		EBattleEventType::Effectiveness,
		EBattleEventType::Damage,
		EBattleEventType::HPChanged,
		EBattleEventType::ItemRemoved,
		EBattleEventType::ActionCompleted
	};
	bool bValid = TestTrue(TEXT("Both held-item checkpoints are accepted"),
		FirstResult.WasAccepted() && SecondResult.WasAccepted());
	bValid &= TestTrue(TEXT("Knock Off full public event order is exact"),
		HasExactResolutionEvents(FirstResult, ExpectedEvents)
			&& HasExactResolutionEvents(SecondResult, ExpectedEvents));
	const int32 RemovedIndex = FindResolutionEvent(
		FirstResult,
		EBattleEventType::ItemRemoved);
	bValid &= TestTrue(TEXT("Replay removal event preserves exact public fields"),
		IsPublicMutationEvent(
			FirstResult,
			RemovedIndex,
			EBattleEventType::ItemRemoved,
			FBattleItemRules::GetChoiceBandId(),
			TargetTrainerId(),
			TargetId(),
			TargetTrainerId(),
			TargetId(),
			1,
			0,
			-1));
	bValid &= TestTrue(TEXT("Held-item operations add no RNG draw"),
		FirstRandom != nullptr && SecondRandom != nullptr
			&& FirstRandom->IsExact() && SecondRandom->IsExact()
			&& First->ExportRandomTrace().Num() == 1
			&& Second->ExportRandomTrace().Num() == 1
			&& First->ExportRandomTrace()[0].RulePurpose
				== FBattleEffectExecutor::GetDamageRandomRulePurpose()
			&& First->ExportRandomTrace() == Second->ExportRandomTrace());
	bValid &= TestTrue(TEXT("Each accepted mutation resolution publishes once"),
		IsReturnedResolutionAppendedExactlyOnce(*First, FirstResult)
			&& IsReturnedResolutionAppendedExactlyOnce(*Second, SecondResult));

	const FBattleReplayRecord FirstRecord = First->ExportReplayRecord();
	const FBattleReplayRecord SecondRecord = Second->ExportReplayRecord();
	bValid &= TestTrue(TEXT("Held-item moves preserve replay schema 6"),
		FirstRecord.GetSchemaVersion() == 6
			&& SecondRecord.GetSchemaVersion() == 6);
	TArray<uint8> FirstBytes;
	TArray<uint8> FirstRepeatBytes;
	TArray<uint8> SecondBytes;
	bValid &= TestTrue(TEXT("Schema-6 held-item replays serialize"),
		TrySerializeReplay(*First, FirstBytes)
			&& TrySerializeReplay(*First, FirstRepeatBytes)
			&& TrySerializeReplay(*Second, SecondBytes));
	bValid &= TestTrue(TEXT("Repeated and independent replay bytes are identical"),
		FirstBytes == FirstRepeatBytes && FirstBytes == SecondBytes);

	const FBattleMoveDefinition TransferMove = MakeThiefMove();
	TUniquePtr<FBattleEngine> TransferFirst;
	TUniquePtr<FBattleEngine> TransferSecond;
	FStrictBattleRandom* TransferFirstRandom = nullptr;
	FStrictBattleRandom* TransferSecondRandom = nullptr;
	if (!TestTrue(TEXT("First transfer replay engine is created"),
		TryMakeEngine(
			TransferMove,
			FItemId(),
			FBattleItemRules::GetLeftoversId(),
			200,
			200,
			false,
			Draws,
			TransferFirst,
			TransferFirstRandom))
		|| !TestTrue(TEXT("Second transfer replay engine is created"),
			TryMakeEngine(
				TransferMove,
				FItemId(),
				FBattleItemRules::GetLeftoversId(),
				200,
				200,
				false,
				Draws,
				TransferSecond,
				TransferSecondRandom))
		|| !TestTrue(TEXT("First transfer replay reaches effects"),
			TryPrepareEffectsCheckpoint(*TransferFirst, TransferMove.Id))
		|| !TestTrue(TEXT("Second transfer replay reaches effects"),
			TryPrepareEffectsCheckpoint(*TransferSecond, TransferMove.Id)))
	{
		return false;
	}
	const FBattleResolution TransferFirstResult =
		TransferFirst->ExecuteCurrentMoveEffects();
	const FBattleResolution TransferSecondResult =
		TransferSecond->ExecuteCurrentMoveEffects();
	const int32 TransferredIndex = FindResolutionEvent(
		TransferFirstResult,
		EBattleEventType::ItemTransferred);
	bValid &= TestTrue(TEXT("Transfer event preserves exact public action and visibility fields"),
		TransferFirstResult.WasAccepted()
			&& TransferSecondResult.WasAccepted()
			&& IsPublicMutationEvent(
				TransferFirstResult,
				TransferredIndex,
				EBattleEventType::ItemTransferred,
				FBattleItemRules::GetLeftoversId(),
				TargetTrainerId(),
				TargetId(),
				UserTrainerId(),
				UserId(),
				1,
				1,
				0)
			&& TransferFirstRandom != nullptr
			&& TransferSecondRandom != nullptr
			&& TransferFirstRandom->IsExact()
			&& TransferSecondRandom->IsExact());
	const FBattleReplayRecord TransferFirstRecord =
		TransferFirst->ExportReplayRecord();
	const FBattleReplayRecord TransferSecondRecord =
		TransferSecond->ExportReplayRecord();
	TArray<uint8> TransferFirstBytes;
	TArray<uint8> TransferFirstRepeatBytes;
	TArray<uint8> TransferSecondBytes;
	bValid &= TestTrue(TEXT("ItemTransferred serializes deterministically in replay schema 6"),
		TransferFirstRecord.GetSchemaVersion() == 6
			&& TransferSecondRecord.GetSchemaVersion() == 6
			&& TrySerializeReplay(*TransferFirst, TransferFirstBytes)
			&& TrySerializeReplay(*TransferFirst, TransferFirstRepeatBytes)
			&& TrySerializeReplay(*TransferSecond, TransferSecondBytes)
			&& TransferFirstBytes == TransferFirstRepeatBytes
			&& TransferFirstBytes == TransferSecondBytes);

	const FBattleMoveDefinition RestoreMove = MakeRecycleMove();
	TUniquePtr<FBattleEngine> RestoreFirst;
	TUniquePtr<FBattleEngine> RestoreSecond;
	FStrictBattleRandom* RestoreFirstRandom = nullptr;
	FStrictBattleRandom* RestoreSecondRandom = nullptr;
	if (!TestTrue(TEXT("First restoration replay engine is created"),
		TryMakeEngine(
			RestoreMove,
			FItemId(),
			FItemId(),
			200,
			200,
			false,
			{},
			RestoreFirst,
			RestoreFirstRandom))
		|| !TestTrue(TEXT("Second restoration replay engine is created"),
			TryMakeEngine(
				RestoreMove,
				FItemId(),
				FItemId(),
				200,
				200,
				false,
				{},
				RestoreSecond,
				RestoreSecondRandom))
		|| !TestTrue(TEXT("First restoration history is seeded"),
			FBattleC10HeldItemMovesFixture::ReplaceWithConsumedHistory(
				*RestoreFirst,
				{{FBattleItemRules::GetChoiceBandId(), 7}}))
		|| !TestTrue(TEXT("Second restoration history is seeded"),
			FBattleC10HeldItemMovesFixture::ReplaceWithConsumedHistory(
				*RestoreSecond,
				{{FBattleItemRules::GetChoiceBandId(), 7}}))
		|| !TestTrue(TEXT("First restoration replay reaches effects"),
			TryPrepareEffectsCheckpoint(*RestoreFirst, RestoreMove.Id))
		|| !TestTrue(TEXT("Second restoration replay reaches effects"),
			TryPrepareEffectsCheckpoint(*RestoreSecond, RestoreMove.Id)))
	{
		return false;
	}
	const FBattleResolution RestoreFirstResult =
		RestoreFirst->ExecuteCurrentMoveEffects();
	const FBattleResolution RestoreSecondResult =
		RestoreSecond->ExecuteCurrentMoveEffects();
	const int32 RestoredIndex = FindResolutionEvent(
		RestoreFirstResult,
		EBattleEventType::ItemRestored);
	bValid &= TestTrue(TEXT("Restoration event preserves exact public action and visibility fields"),
		RestoreFirstResult.WasAccepted()
			&& RestoreSecondResult.WasAccepted()
			&& IsPublicMutationEvent(
				RestoreFirstResult,
				RestoredIndex,
				EBattleEventType::ItemRestored,
				FBattleItemRules::GetChoiceBandId(),
				UserTrainerId(),
				UserId(),
				UserTrainerId(),
				UserId(),
				0,
				1,
				1)
			&& RestoreFirstRandom != nullptr
			&& RestoreSecondRandom != nullptr
			&& RestoreFirstRandom->IsExact()
			&& RestoreSecondRandom->IsExact()
			&& RestoreFirst->ExportRandomTrace().IsEmpty()
			&& RestoreSecond->ExportRandomTrace().IsEmpty());
	const FBattleReplayRecord RestoreFirstRecord = RestoreFirst->ExportReplayRecord();
	const FBattleReplayRecord RestoreSecondRecord = RestoreSecond->ExportReplayRecord();
	TArray<uint8> RestoreFirstBytes;
	TArray<uint8> RestoreFirstRepeatBytes;
	TArray<uint8> RestoreSecondBytes;
	bValid &= TestTrue(TEXT("ItemRestored serializes deterministically in replay schema 6"),
		RestoreFirstRecord.GetSchemaVersion() == 6
			&& RestoreSecondRecord.GetSchemaVersion() == 6
			&& TrySerializeReplay(*RestoreFirst, RestoreFirstBytes)
			&& TrySerializeReplay(*RestoreFirst, RestoreFirstRepeatBytes)
			&& TrySerializeReplay(*RestoreSecond, RestoreSecondBytes)
			&& RestoreFirstBytes == RestoreFirstRepeatBytes
			&& RestoreFirstBytes == RestoreSecondBytes);
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC10HeldItemMovesAtomicRollbackTest,
	"PokemonSolarus.Battle.C08C.C10HeldItemMoves.Atomic.RandomFailureRollsBackItemState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC10HeldItemMovesAtomicRollbackTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	const TArray<FBattleExpectedRandomDraw> DamageDraw =
	{
		{0, 15, 0, FBattleEffectExecutor::GetDamageRandomRulePurpose()}
	};
	bool bValid = true;

	{
		const FBattleMoveDefinition Move = MakeKnockOffMove();
		TUniquePtr<FBattleEngine> Engine;
		FStrictBattleRandom* Random = nullptr;
		if (!TestTrue(TEXT("Random-failure rollback engine is created"),
			TryMakeEngine(Move, FItemId(), FBattleItemRules::GetChoiceBandId(),
				200, 200, false, {}, Engine, Random))
			|| !TestTrue(TEXT("Random-failure rollback reaches effects"),
				TryPrepareEffectsCheckpoint(*Engine, Move.Id)))
		{
			return false;
		}
		const FTargetCheckpointObservation Before = ObserveTargetCheckpoint(*Engine);
		const TArray<FBattleHeldItemInstanceState> LedgerBefore = CopyLedger(*Engine);
		const int32 HooksBefore = CountItemHooks(
			*Engine,
			FBattleItemRules::GetChoiceBandId(),
			TargetId());
		const FBattleResolution Rejected = Engine->ExecuteCurrentMoveEffects();
		bValid &= VerifyRejectedTargetCheckpoint(
			*this,
			*Engine,
			Before,
			EBattleRejectionReason::CheckpointRandomStageFailed,
			Rejected);
		const FBattleBattlerState* Target = FindBattler(*Engine, TargetId());
		bValid &= TestTrue(TEXT("Random failure rolls back ledger and mirrors"),
			Target != nullptr
				&& Target->HeldItem.CurrentItemId
					== FBattleItemRules::GetChoiceBandId()
				&& !Target->HeldItem.bTemporarilyRemoved
				&& !Target->HeldItem.bRevealed
				&& IsLedgerIdentical(*Engine, LedgerBefore));
		bValid &= TestTrue(TEXT("Random failure rolls back hooks and reveal tracker"),
			HooksBefore > 0
				&& CountItemHooks(
					*Engine,
					FBattleItemRules::GetChoiceBandId(),
					TargetId()) == HooksBefore
				&& !HasBeenRevealed(
					*Engine,
					FBattleItemRules::GetChoiceBandId(),
					TargetId()));
		bValid &= TestTrue(TEXT("Random failure commits no RNG draw"),
			Random != nullptr
				&& Random->GetTrace().IsEmpty()
				&& Engine->ExportRandomTrace().IsEmpty());
	}

	{
		const FBattleMoveDefinition Move = MakeKnockOffMove();
		TUniquePtr<FBattleEngine> Engine;
		FStrictBattleRandom* Random = nullptr;
		if (!TestTrue(TEXT("Hook-cleanup rollback engine is created"),
			TryMakeEngine(Move, FItemId(), FBattleItemRules::GetChoiceBandId(),
				200, 200, false, DamageDraw, Engine, Random))
			|| !TestTrue(TEXT("Hook-cleanup rollback reaches effects"),
				TryPrepareEffectsCheckpoint(*Engine, Move.Id)))
		{
			return false;
		}
		FBattleC09BWildFlowEngineFixture::SetNextTriggerReentrancyToken(
			*Engine,
			MAX_uint64);
		const FTargetCheckpointObservation Before = ObserveTargetCheckpoint(*Engine);
		const TArray<FBattleHeldItemInstanceState> LedgerBefore = CopyLedger(*Engine);
		const int32 HooksBefore = CountItemHooks(
			*Engine,
			FBattleItemRules::GetChoiceBandId(),
			TargetId());
		const FBattleResolution Rejected = Engine->ExecuteCurrentMoveEffects();
		bValid &= VerifyRejectedTargetCheckpoint(
			*this,
			*Engine,
			Before,
			EBattleRejectionReason::CheckpointPreparationFailed,
			Rejected);
		const FBattleBattlerState* Target = FindBattler(*Engine, TargetId());
		bValid &= TestTrue(TEXT("Failed old-holder cleanup commits no item work"),
			Target != nullptr
				&& Target->HeldItem.CurrentItemId
					== FBattleItemRules::GetChoiceBandId()
				&& !Target->HeldItem.bTemporarilyRemoved
				&& !Target->HeldItem.bRevealed
				&& IsLedgerIdentical(*Engine, LedgerBefore)
				&& HooksBefore > 0
				&& CountItemHooks(
					*Engine,
					FBattleItemRules::GetChoiceBandId(),
					TargetId()) == HooksBefore
				&& !HasBeenRevealed(
					*Engine,
					FBattleItemRules::GetChoiceBandId(),
					TargetId())
				&& Random != nullptr
				&& Random->GetTrace().IsEmpty()
				&& Engine->ExportRandomTrace().IsEmpty());
	}

	{
		const FBattleMoveDefinition Move = MakeThiefMove();
		TUniquePtr<FBattleEngine> Engine;
		FStrictBattleRandom* Random = nullptr;
		if (!TestTrue(TEXT("Late-registration rollback engine is created"),
			TryMakeEngine(Move, FItemId(), FBattleItemRules::GetLeftoversId(),
				200, 200, false, DamageDraw, Engine, Random))
			|| !TestTrue(TEXT("Late-registration rollback reaches effects"),
				TryPrepareEffectsCheckpoint(*Engine, Move.Id)))
		{
			return false;
		}
		FBattleC09BWildFlowEngineFixture::SetNextTriggerReentrancyToken(
			*Engine,
			MAX_uint64 - 1);
		const FTargetCheckpointObservation Before = ObserveTargetCheckpoint(*Engine);
		const TArray<FBattleHeldItemInstanceState> LedgerBefore = CopyLedger(*Engine);
		const FBattleHeldItemState UserBefore = FindBattler(*Engine, UserId())->HeldItem;
		const FBattleHeldItemState TargetBefore = FindBattler(*Engine, TargetId())->HeldItem;
		const int32 TargetHooksBefore = CountItemHooks(
			*Engine,
			FBattleItemRules::GetLeftoversId(),
			TargetId());
		const FBattleResolution Rejected = Engine->ExecuteCurrentMoveEffects();
		bValid &= VerifyRejectedTargetCheckpoint(
			*this,
			*Engine,
			Before,
			EBattleRejectionReason::CheckpointPreparationFailed,
			Rejected);
		bValid &= TestTrue(
			TEXT("Failed new-holder registration rolls back transfer, reveal, hooks, and RNG"),
			AreActionStartHeldItemsIdentical(
				FindBattler(*Engine, UserId())->HeldItem,
				UserBefore)
				&& AreActionStartHeldItemsIdentical(
					FindBattler(*Engine, TargetId())->HeldItem,
					TargetBefore)
				&& IsLedgerIdentical(*Engine, LedgerBefore)
				&& TargetHooksBefore > 0
				&& CountItemHooks(
					*Engine,
					FBattleItemRules::GetLeftoversId(),
					TargetId()) == TargetHooksBefore
				&& CountItemHooks(
					*Engine,
					FBattleItemRules::GetLeftoversId(),
					UserId()) == 0
				&& !HasBeenRevealed(
					*Engine,
					FBattleItemRules::GetLeftoversId(),
					TargetId())
				&& !HasBeenRevealed(
					*Engine,
					FBattleItemRules::GetLeftoversId(),
					UserId())
				&& Random != nullptr
				&& Random->GetTrace().IsEmpty()
				&& Engine->ExportRandomTrace().IsEmpty());
	}

	{
		const FBattleMoveDefinition Move = MakeRecycleMove();
		TUniquePtr<FBattleEngine> Engine;
		FStrictBattleRandom* Random = nullptr;
		if (!TestTrue(TEXT("Ledger-failure rollback engine is created"),
			TryMakeEngine(Move, FItemId(), FItemId(),
				200, 200, false, {}, Engine, Random))
			|| !TestTrue(TEXT("Ledger ordinal exhaustion is seeded"),
				FBattleC10HeldItemMovesFixture::ReplaceWithConsumedHistory(
					*Engine,
					{{FBattleItemRules::GetSitrusBerryId(), MAX_uint64 - 1}}))
			|| !TestTrue(TEXT("Ledger-failure rollback reaches effects"),
				TryPrepareEffectsCheckpoint(*Engine, Move.Id)))
		{
			return false;
		}
		const FTargetCheckpointObservation Before = ObserveTargetCheckpoint(*Engine);
		const TArray<FBattleHeldItemInstanceState> LedgerBefore = CopyLedger(*Engine);
		const FBattleResolution Rejected = Engine->ExecuteCurrentMoveEffects();
		bValid &= VerifyRejectedTargetCheckpoint(
			*this,
			*Engine,
			Before,
			EBattleRejectionReason::CheckpointPreparationFailed,
			Rejected);
		const FBattleHeldItemInstanceState* Item = FindLedgerItem(
			*Engine,
			FBattleItemRules::GetSitrusBerryId());
		bValid &= TestTrue(TEXT("Rejected ledger restore commits no mirror or reveal"),
			Item != nullptr
				&& Item->bConsumed
				&& Item->LastConsumptionFactOrdinal == MAX_uint64 - 1
				&& IsLedgerIdentical(*Engine, LedgerBefore)
				&& !FindBattler(*Engine, UserId())->HeldItem.CurrentItemId.IsValid()
				&& !HasBeenRevealed(
					*Engine,
					FBattleItemRules::GetSitrusBerryId(),
					UserId())
				&& Random != nullptr
				&& Random->GetTrace().IsEmpty()
				&& Engine->ExportRandomTrace().IsEmpty());
	}

	{
		const FBattleMoveDefinition Move = MakeRecycleMove();
		TUniquePtr<FBattleEngine> Engine;
		FStrictBattleRandom* Random = nullptr;
		if (!TestTrue(TEXT("Immediate-update rollback engine is created"),
			TryMakeEngine(Move, FItemId(), FItemId(),
				50, 200, false, {}, Engine, Random))
			|| !TestTrue(TEXT("Immediate-update consumed Sitrus is seeded"),
				FBattleC10HeldItemMovesFixture::ReplaceWithConsumedHistory(
					*Engine,
					{{FBattleItemRules::GetSitrusBerryId(), 1}}))
			|| !TestTrue(TEXT("Immediate-update rollback reaches effects"),
				TryPrepareEffectsCheckpoint(*Engine, Move.Id)))
		{
			return false;
		}
		FBattleC09BWildFlowEngineFixture::SetNextTriggerReentrancyToken(
			*Engine,
			MAX_uint64 - 1);
		const FTargetCheckpointObservation Before = ObserveTargetCheckpoint(*Engine);
		const TArray<FBattleHeldItemInstanceState> LedgerBefore = CopyLedger(*Engine);
		const FBattleResolution Rejected = Engine->ExecuteCurrentMoveEffects();
		bValid &= VerifyRejectedTargetCheckpoint(
			*this,
			*Engine,
			Before,
			EBattleRejectionReason::CheckpointPreparationFailed,
			Rejected);
		const FBattleHeldItemInstanceState* Item = FindLedgerItem(
			*Engine,
			FBattleItemRules::GetSitrusBerryId());
		bValid &= TestTrue(
			TEXT("Late immediate-update failure rolls back restore, event, reveal, hooks, and heal"),
			Item != nullptr
				&& Item->bConsumed
				&& Item->LastConsumptionFactOrdinal == 1
				&& IsLedgerIdentical(*Engine, LedgerBefore)
				&& FindBattler(*Engine, UserId())->CurrentHP == 50
				&& !FindBattler(*Engine, UserId())->HeldItem.CurrentItemId.IsValid()
				&& CountItemHooks(
					*Engine,
					FBattleItemRules::GetSitrusBerryId(),
					UserId()) == 0
				&& !HasBeenRevealed(
					*Engine,
					FBattleItemRules::GetSitrusBerryId(),
					UserId())
				&& Random != nullptr
				&& Random->GetTrace().IsEmpty()
				&& Engine->ExportRandomTrace().IsEmpty());
	}
	return bValid;
}

#endif
