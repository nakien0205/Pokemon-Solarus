#if WITH_DEV_AUTOMATION_TESTS

#include "BattleAtomicMoveCheckpointTestSupport.h"

namespace BattleAtomicMoveCheckpointTestSupportPrivate
{
	using namespace BattleAtomicCheckpointTestCommonPrivate;
	using namespace BattleAtomicSwitchTestSupportPrivate;

bool TrySeedActionStartVolatile(
		FBattleEngine& Engine,
		const FBattlerId BattlerId,
		const FConditionId VolatileId,
		const FDefinitionId PayloadId ,
		const TOptional<int32> RemainingTurns )
{
		FBattleEngineState& State =
			FBattleC09BWildFlowEngineFixture::GetMutableState(Engine);
		FBattleBattlerState* Battler = State.FindMutableBattler(BattlerId);
		if (Battler == nullptr
			|| Battler->Volatiles.ContainsByPredicate(
				[VolatileId](const FBattleConditionState& Condition)
				{
					return Condition.ConditionId == VolatileId;
				}))
		{
			return false;
		}

		FBattleTriggerSubject Owner;
		if (!FBattleTriggerSubject::TryCreateBattler(BattlerId, Owner))
		{
			return false;
		}
		FBattleVolatileTriggerRegistrationFacts Facts;
		Facts.VolatileId = VolatileId;
		Facts.PayloadId = PayloadId.IsValid()
			? PayloadId
			: VolatileId.GetDefinitionId();
		Facts.Owner = Owner;
		Facts.Source = Owner;
		Facts.Targets.Add(Owner);
		Facts.Layers = 1;
		Facts.RemainingTurns = RemainingTurns;
		EBattleTriggerError TriggerError = EBattleTriggerError::None;
		if (!FBattleVolatileRules::TryRegisterTriggers(
				State.TriggerFramework,
				Facts,
				TriggerError))
		{
			return false;
		}

		FBattleConditionState Condition;
		Condition.ConditionId = VolatileId;
		Condition.LayerCount = 1;
		Condition.RemainingTurns = RemainingTurns;
		Condition.CreationOrdinal = State.NextConditionCreationOrdinal++;
		Condition.SourceBattlerId = BattlerId;
		Battler->Volatiles.Add(MoveTemp(Condition));
		TArray<FBattleTriggerLifecycleFact> IgnoredFacts;
		State.TriggerFramework.DrainLifecycleFacts(IgnoredFacts);
		return true;
	}

bool TrySeedPreMoveMajorStatus(
		FBattleEngine& Engine,
		const FBattlerId BattlerId,
		const FConditionId StatusId,
		const TOptional<int32> SleepTurns )
{
		FBattleEngineState& State =
			FBattleC09BWildFlowEngineFixture::GetMutableState(Engine);
		FBattleBattlerState* Battler = State.FindMutableBattler(BattlerId);
		FBattleTriggerSubject Owner;
		EBattleTriggerError TriggerError = EBattleTriggerError::None;
		if (Battler == nullptr
			|| Battler->MajorStatusId.IsValid()
			|| !FBattleTriggerSubject::TryCreateBattler(BattlerId, Owner)
			|| !FBattleMajorStatusRules::TryRegisterTriggers(
				State.TriggerFramework,
				StatusId,
				Owner,
				SleepTurns,
				TriggerError))
		{
			return false;
		}
		Battler->MajorStatusId = StatusId;
		TArray<FBattleTriggerLifecycleFact> IgnoredFacts;
		State.TriggerFramework.DrainLifecycleFacts(IgnoredFacts);
		return true;
	}

int32 CountActionStartTriggerRegistrations(
		const FBattleEngineState& State,
		const FDefinitionId DefinitionId)
{
		int32 Count = 0;
		for (const FBattleTriggerRegistrationState& Registration :
			State.TriggerFramework.GetActiveRegistrations())
		{
			const FBattleTriggerSourceDefinition& Source =
				Registration.Spec.SourceDefinition;
			bool bMatches = false;
			switch (Source.Kind)
			{
			case EBattleTriggerSourceDefinitionKind::Condition:
				bMatches = Source.ConditionId.GetDefinitionId() == DefinitionId;
				break;
			case EBattleTriggerSourceDefinitionKind::Ability:
				bMatches = Source.AbilityId.GetDefinitionId() == DefinitionId;
				break;
			case EBattleTriggerSourceDefinitionKind::Item:
				bMatches = Source.ItemId.GetDefinitionId() == DefinitionId;
				break;
			default:
				break;
			}
			Count += bMatches ? 1 : 0;
		}
		return Count;
	}

bool TryPrepareLastLockedAction(
		FBattleEngine& Engine,
		const FBattlerId ExpectedActorId)
{
		FBattleEngineState& State =
			FBattleC09BWildFlowEngineFixture::GetMutableState(Engine);
		if (State.LockedActions.Num() < 2)
		{
			return false;
		}
		const int32 LastIndex = State.LockedActions.Num() - 1;
		if (State.LockedActions[LastIndex].Decision.GetActingBattlerId()
			!= ExpectedActorId)
		{
			return false;
		}
		for (int32 Index = 0; Index < LastIndex; ++Index)
		{
			State.LockedActions[Index].bStarted = true;
			State.LockedActions[Index].bFinished = true;
		}
		State.CurrentLockedActionIndex = LastIndex;
		State.Phase = EBattlePhase::Resolving;
		EBattleStateValidationError StateError = EBattleStateValidationError::None;
		return State.ValidateInvariants(StateError);
	}

FActionStartCheckpointObservation ObserveActionStartCheckpoint(
		const FBattleEngine& Engine,
		const FBattlerId ActorId,
		const FTrainerId TrainerId)
{
		const FBattleEngineState& State =
			FBattleC09BWildFlowEngineFixture::GetState(Engine);
		FActionStartCheckpointObservation Observation;
		Observation.StateVersion = State.StateVersion;
		Observation.NextResolutionId = State.NextResolutionId;
		Observation.NextEventOrdinal = State.NextEventOrdinal;
		Observation.NextTriggerToken = State.NextTriggerReentrancyToken;
		Observation.NextConditionCreationOrdinal = State.NextConditionCreationOrdinal;
		Observation.ActionIndex = State.CurrentLockedActionIndex;
		Observation.ResolutionCount = State.Resolutions.Num();
		Observation.EventCount = State.OrderedEvents.Num();
		Observation.RandomTraceCount = State.Random->GetTrace().Num();
		Observation.Phase = State.Phase;
		Observation.Outcome = State.Outcome;
		Observation.OutcomeCause = State.OutcomeCause;
		Observation.bPendingDecisionSet = State.PendingDecision.IsSet();
		Observation.PendingDecisionRequestCount = State.PendingDecisionRequests.Num();
		Observation.PendingReplacementCount = State.PendingReplacements.Num();
		Observation.RoomCount = State.Field.Rooms.Num();
		if (State.LockedActions.IsValidIndex(Observation.ActionIndex))
		{
			const FBattleLockedActionState& Action =
				State.LockedActions[Observation.ActionIndex];
			Observation.bActionStarted = Action.bStarted;
			Observation.bMoveCommitted = Action.bMoveCommitted;
			Observation.bTargetResolutionSet = Action.TargetResolution.IsSet();
			Observation.EffectExecutionState = Action.EffectExecutionState;
			Observation.bActionFinished = Action.bFinished;
		}
		const FBattleTrainerState* Trainer = State.FindTrainer(TrainerId);
		if (Trainer != nullptr)
		{
			Observation.MaximumActions = Trainer->ActionAllowance.MaximumActions;
			Observation.RemainingActions = Trainer->ActionAllowance.RemainingActions;
			Observation.bBagActionAvailable =
				Trainer->ActionAllowance.bBagActionAvailable;
		}
		const FBattleBattlerState* Actor = State.FindBattler(ActorId);
		if (Actor != nullptr)
		{
			for (const FBattleMoveSlotState& Move : Actor->Moves)
			{
				Observation.TotalMovePP += Move.CurrentPP;
			}
			Observation.bHasHeldItem = Actor->HeldItem.InstanceId.IsValid();
			Observation.HeldItem = Actor->HeldItem;
			for (const FBattleConditionState& Condition : Actor->Volatiles)
			{
				Observation.ActorVolatileIds.Add(Condition.ConditionId);
			}
		}
		Observation.bActorActive = State.ActivePositions.ContainsByPredicate(
			[ActorId](const FBattleActivePositionState& Active)
			{
				return Active.BattlerId == ActorId;
			});
		Observation.LedgerStates.Append(State.HeldItemLedger.GetStates());
		for (const FBattleTriggerRegistrationState& Registration :
			State.TriggerFramework.GetActiveRegistrations())
		{
			Observation.TriggerRegistrationIds.Add(Registration.RegistrationId);
			Observation.TriggerCreationOrdinals.Add(Registration.CreationOrdinal);
			Observation.TriggerSources.Add(Registration.Spec.SourceDefinition);
			Observation.TriggerSuppression.Add(Registration.bSuppressed ? 1 : 0);
		}
		Observation.PendingTriggerDispatchCount =
			State.TriggerFramework.GetPendingDispatchCount();
		Observation.PendingTriggerEffectCount =
			State.TriggerFramework.GetPendingEffectRequestCount();
		Observation.PendingTriggerLifecycleCount =
			State.TriggerFramework.GetPendingLifecycleFactCount();
		return Observation;
	}

bool VerifyRejectedActionStartCheckpoint(
		FAutomationTestBase& Test,
		const FBattleEngine& Engine,
		const FBattlerId ActorId,
		const FTrainerId TrainerId,
		const FActionStartCheckpointObservation& Before,
		const uint64 ExpectedStateVersion,
		const EBattleRejectionReason ExpectedReason,
		const FBattleResolution& Returned)
{
		const FActionStartCheckpointObservation After =
			ObserveActionStartCheckpoint(Engine, ActorId, TrainerId);
		bool bValid = true;
		bValid &= Test.TestFalse(TEXT("Action-start checkpoint failure is rejected"),
			Returned.WasAccepted());
		bValid &= Test.TestEqual(TEXT("Action-start rejection reason is typed"),
			Returned.GetRejection().Reason, ExpectedReason);
		bValid &= Test.TestTrue(TEXT("Action-start rejection is appended exactly once"),
			IsReturnedResolutionAppendedExactlyOnce(Engine, Returned));
		bValid &= Test.TestTrue(TEXT("Action-start rejection publishes one cancellation only"),
			Returned.GetEvents().Num() == 1
				&& Returned.GetEvents()[0].GetType()
					== EBattleEventType::ActionCanceled
				&& Returned.GetEvents()[0].GetCause() == EBattleEventCause::Rule
				&& Returned.GetEvents()[0].GetEventOrdinal()
					== Before.NextEventOrdinal
				&& Returned.GetEvents()[0].GetVisibility().Level
					== EBattleVisibilityLevel::Public);
		bValid &= Test.TestEqual(TEXT("Rejection appends one resolution"),
			After.ResolutionCount, Before.ResolutionCount + 1);
		bValid &= Test.TestEqual(TEXT("Rejection appends one event"),
			After.EventCount, Before.EventCount + 1);
		bValid &= Test.TestEqual(TEXT("Rejection consumes one resolution identity"),
			After.NextResolutionId, Before.NextResolutionId + 1);
		bValid &= Test.TestEqual(TEXT("Rejection consumes one event ordinal"),
			After.NextEventOrdinal, Before.NextEventOrdinal + 1);
		bValid &= Test.TestEqual(TEXT("Rejection publishes no accepted version delta"),
			After.StateVersion, ExpectedStateVersion);
		bValid &= Test.TestEqual(TEXT("Rejection preserves the action cursor"),
			After.ActionIndex, Before.ActionIndex);
		bValid &= Test.TestEqual(TEXT("Rejection preserves phase"),
			After.Phase, Before.Phase);
		bValid &= Test.TestEqual(TEXT("Rejection preserves outcome"),
			After.Outcome, Before.Outcome);
		bValid &= Test.TestEqual(TEXT("Rejection preserves outcome cause"),
			After.OutcomeCause, Before.OutcomeCause);
		bValid &= Test.TestEqual(TEXT("Rejection preserves Trainer maximum allowance"),
			After.MaximumActions, Before.MaximumActions);
		bValid &= Test.TestEqual(TEXT("Rejection preserves Trainer remaining allowance"),
			After.RemainingActions, Before.RemainingActions);
		bValid &= Test.TestEqual(TEXT("Rejection preserves Trainer Bag allowance"),
			After.bBagActionAvailable, Before.bBagActionAvailable);
		bValid &= Test.TestEqual(TEXT("Rejection preserves bStarted"),
			After.bActionStarted, Before.bActionStarted);
		bValid &= Test.TestEqual(TEXT("Rejection preserves bMoveCommitted"),
			After.bMoveCommitted, Before.bMoveCommitted);
		bValid &= Test.TestEqual(TEXT("Rejection preserves target-resolution state"),
			After.bTargetResolutionSet, Before.bTargetResolutionSet);
		bValid &= Test.TestEqual(TEXT("Rejection preserves effect-execution state"),
			After.EffectExecutionState, Before.EffectExecutionState);
		bValid &= Test.TestEqual(TEXT("Rejection preserves bFinished"),
			After.bActionFinished, Before.bActionFinished);
		bValid &= Test.TestEqual(TEXT("Rejection preserves move PP"),
			After.TotalMovePP, Before.TotalMovePP);
		bValid &= Test.TestEqual(TEXT("Rejection consumes no gameplay RNG"),
			After.RandomTraceCount, Before.RandomTraceCount);
		bValid &= Test.TestEqual(TEXT("Rejection preserves trigger token"),
			After.NextTriggerToken, Before.NextTriggerToken);
		bValid &= Test.TestEqual(TEXT("Rejection preserves condition ordinal"),
			After.NextConditionCreationOrdinal, Before.NextConditionCreationOrdinal);
		bValid &= Test.TestEqual(TEXT("Rejection preserves room state"),
			After.RoomCount, Before.RoomCount);
		bValid &= Test.TestEqual(TEXT("Rejection preserves actor active state"),
			After.bActorActive, Before.bActorActive);
		bValid &= Test.TestEqual(TEXT("Rejection preserves pending-decision presence"),
			After.bPendingDecisionSet, Before.bPendingDecisionSet);
		bValid &= Test.TestEqual(TEXT("Rejection preserves pending decision requests"),
			After.PendingDecisionRequestCount, Before.PendingDecisionRequestCount);
		bValid &= Test.TestEqual(TEXT("Rejection preserves pending replacements"),
			After.PendingReplacementCount, Before.PendingReplacementCount);
		bValid &= Test.TestEqual(TEXT("Rejection preserves held-item presence"),
			After.bHasHeldItem, Before.bHasHeldItem);
		bValid &= Test.TestTrue(TEXT("Rejection preserves held-item battler facts"),
			AreActionStartHeldItemsIdentical(After.HeldItem, Before.HeldItem));
		bValid &= Test.TestTrue(TEXT("Rejection preserves the held-item ledger"),
			After.LedgerStates == Before.LedgerStates);
		bValid &= Test.TestTrue(TEXT("Rejection preserves actor volatiles"),
			After.ActorVolatileIds == Before.ActorVolatileIds);
		bValid &= Test.TestTrue(TEXT("Rejection preserves trigger registration ids"),
			After.TriggerRegistrationIds == Before.TriggerRegistrationIds);
		bValid &= Test.TestTrue(TEXT("Rejection preserves trigger creation order"),
			After.TriggerCreationOrdinals == Before.TriggerCreationOrdinals);
		bValid &= Test.TestTrue(TEXT("Rejection preserves trigger sources"),
			After.TriggerSources == Before.TriggerSources);
		bValid &= Test.TestTrue(TEXT("Rejection preserves trigger suppression"),
			After.TriggerSuppression == Before.TriggerSuppression);
		bValid &= Test.TestEqual(TEXT("Rejection preserves pending trigger dispatches"),
			After.PendingTriggerDispatchCount, Before.PendingTriggerDispatchCount);
		bValid &= Test.TestEqual(TEXT("Rejection preserves pending trigger effects"),
			After.PendingTriggerEffectCount, Before.PendingTriggerEffectCount);
		bValid &= Test.TestEqual(TEXT("Rejection preserves pending trigger lifecycle facts"),
			After.PendingTriggerLifecycleCount, Before.PendingTriggerLifecycleCount);
		const FBattleReplayRecord Replay = Engine.ExportReplayRecord();
		bValid &= Test.TestEqual(TEXT("Rejection replay keeps the canonical schema"),
			Replay.GetSchemaVersion(), FBattleReplayRecord::CurrentSchemaVersion);
		bValid &= Test.TestTrue(TEXT("Rejection replay contains the same resolution"),
			!Replay.GetResolutions().IsEmpty()
				&& Replay.GetResolutions().Last().GetResolutionId()
					== Returned.GetResolutionId()
				&& Replay.GetResolutions().Last().GetRejection().Reason
					== ExpectedReason);
		return bValid;
	}

FAtomicWildScenario MakePreMoveScenario(
		const FMoveId ExtraMoveId ,
		const int32 PlayerCurrentHP ,
		const FAbilityId AbilityId ,
		const FItemId HeldItemId )
{
		FAtomicWildScenario Scenario;
		Scenario.PlayerLeftSpeed = 300;
		Scenario.OpponentLeftSpeed = 50;
		Scenario.PlayerCurrentHP = PlayerCurrentHP;
		Scenario.PlayerAbilityId = AbilityId;
		Scenario.PlayerHeldItemId = HeldItemId;
		Scenario.PlayerExtraMoveId = ExtraMoveId;
		return Scenario;
	}

bool TryLockAndBeginPreMove(
		FBattleEngine& Engine,
		const FMoveId MoveId )
{
		return LockTurn(
				Engine,
				PlayerLeftValue,
				EBattleActionKind::Fight,
				MoveId)
			&& BeginExpectedWildAction(
				Engine,
				PlayerLeftValue,
				EBattleActionKind::Fight);
	}

int32 GetPreMovePP(
		const FBattleEngine& Engine,
		const FBattlerId BattlerId,
		const FMoveId MoveId)
{
		const FBattleBattlerState* Battler =
			FBattleC09BWildFlowEngineFixture::GetState(Engine).FindBattler(BattlerId);
		const FBattleMoveSlotState* Move = Battler != nullptr
			? Battler->Moves.FindByPredicate(
				[MoveId](const FBattleMoveSlotState& Candidate)
				{
					return Candidate.MoveId == MoveId;
				})
			: nullptr;
		return Move != nullptr ? Move->CurrentPP : INDEX_NONE;
	}

const FBattleConditionState* FindPreMoveVolatile(
		const FBattleEngine& Engine,
		const FBattlerId BattlerId,
		const FConditionId VolatileId)
{
		const FBattleBattlerState* Battler =
			FBattleC09BWildFlowEngineFixture::GetState(Engine).FindBattler(BattlerId);
		return Battler != nullptr
			? Battler->Volatiles.FindByPredicate(
				[VolatileId](const FBattleConditionState& Candidate)
				{
					return Candidate.ConditionId == VolatileId;
				})
			: nullptr;
	}

FBattleExpectedRandomDraw MakeTargetExpectedDraw(
		const uint32 Maximum,
		const uint32 Result)
{
		return {
			0,
			Maximum,
			Result,
			FBattleTargetResolver::GetRandomLegalOpponentRulePurpose()};
	}

bool TryPrepareTargetCheckpoint(
		FBattleEngine& Engine,
		const FMoveId MoveId)
{
		if (!TryLockAndBeginPreMove(Engine, MoveId))
		{
			return false;
		}
		const FBattleResolution Commit = Engine.CommitCurrentMoveAfterPreMoveGates();
		const FBattleEngineState& State =
			FBattleC09BWildFlowEngineFixture::GetState(Engine);
		const FBattleLockedActionState* Current =
			State.LockedActions.IsValidIndex(State.CurrentLockedActionIndex)
				? &State.LockedActions[State.CurrentLockedActionIndex]
				: nullptr;
		return Commit.WasAccepted()
			&& Current != nullptr
			&& Current->bStarted
			&& Current->bMoveCommitted
			&& !Current->TargetResolution.IsSet()
			&& !Current->bFinished;
	}

bool TryPrepareLastTargetCheckpoint(
		FBattleEngine& Engine,
		const FMoveId MoveId)
{
		const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
		return LockTurn(
				Engine,
				PlayerLeftValue,
				EBattleActionKind::Fight,
				MoveId)
			&& TryPrepareLastLockedAction(Engine, ActorId)
			&& BeginExpectedWildAction(
				Engine,
				PlayerLeftValue,
				EBattleActionKind::Fight)
			&& Engine.CommitCurrentMoveAfterPreMoveGates().WasAccepted();
	}

bool TryMarkTargetFainted(
		FBattleEngine& Engine,
		const FBattlerId BattlerId)
{
		FBattleBattlerState* Battler =
			FBattleC09BWildFlowEngineFixture::GetMutableState(Engine)
				.FindMutableBattler(BattlerId);
		if (Battler == nullptr)
		{
			return false;
		}
		Battler->CurrentHP = 0;
		Battler->bFainted = true;
		Battler->bFaintTransitionPending = false;
		return true;
	}

bool TryClearTargetActivePosition(
		FBattleEngine& Engine,
		const FActiveSlotId ActiveSlotId)
{
		FBattleActivePositionState* Position =
			FBattleC09BWildFlowEngineFixture::GetMutableState(Engine)
				.ActivePositions.FindByPredicate(
					[ActiveSlotId](const FBattleActivePositionState& Candidate)
					{
						return Candidate.ActiveSlotId == ActiveSlotId;
					});
		if (Position == nullptr)
		{
			return false;
		}
		Position->TrainerId = FTrainerId();
		Position->BattlerId = FBattlerId();
		return true;
	}

bool TrySeedChargedReleaseTargetCheckpoint(
		FBattleEngine& Engine,
		const FMoveId ChargeMoveId)
{
		const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
		FBattleBattlerState* Actor =
			FBattleC09BWildFlowEngineFixture::GetMutableState(Engine)
				.FindMutableBattler(ActorId);
		FBattleMoveSlotState* Slot = Actor != nullptr
			? Actor->Moves.FindByPredicate(
				[ChargeMoveId](const FBattleMoveSlotState& Candidate)
				{
					return Candidate.MoveId == ChargeMoveId;
				})
			: nullptr;
		if (Actor == nullptr || Slot == nullptr)
		{
			return false;
		}
		Slot->CurrentPP = 19;
		Actor->LastMoveId = ChargeMoveId;
		return TrySeedActionStartVolatile(
				Engine,
				ActorId,
				FBattleVolatileRules::GetChargingId(),
				ChargeMoveId.GetDefinitionId())
			&& TrySeedActionStartVolatile(
				Engine,
				ActorId,
				FBattleVolatileRules::GetFlySemiInvulnerableId())
			&& TryPrepareTargetCheckpoint(Engine, ChargeMoveId);
	}

bool IsTargetCheckpointSuccessEvent(const FBattleEvent& Event)
{
		switch (Event.GetType())
		{
		case EBattleEventType::TargetsResolved:
		case EBattleEventType::ActionCompleted:
		case EBattleEventType::ReplacementRequired:
		case EBattleEventType::BattleEnded:
			return true;
		case EBattleEventType::ActionCanceled:
			return Event.GetCause() == EBattleEventCause::Targeting;
		default:
			return false;
		}
	}

bool HasExactTargetEventOrder(
		const FBattleResolution& Resolution,
		const TConstArrayView<EBattleEventType> Expected)
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

FTargetCheckpointBattlerObservation ObserveTargetCheckpointBattler(
		const FBattleEngineState& State,
		const FBattleBattlerState& Battler)
{
		FTargetCheckpointBattlerObservation Observation;
		Observation.Facts = ObserveAtomicSwitchBattler(State, Battler.BattlerId);
		Observation.bEgg = Battler.bEgg;
		for (const FBattleMoveSlotState& Move : Battler.Moves)
		{
			Observation.MoveIds.Add(Move.MoveId);
			Observation.CurrentPP.Add(Move.CurrentPP);
			Observation.MaximumPP.Add(Move.MaxPP);
		}
		return Observation;
	}

bool AreTargetCheckpointBattlersIdentical(
		const FTargetCheckpointBattlerObservation& Left,
		const FTargetCheckpointBattlerObservation& Right)
{
		return AreAtomicSwitchBattlersIdentical(Left.Facts, Right.Facts)
			&& Left.bEgg == Right.bEgg
			&& Left.MoveIds == Right.MoveIds
			&& Left.CurrentPP == Right.CurrentPP
			&& Left.MaximumPP == Right.MaximumPP;
	}

FTargetCheckpointObservation ObserveTargetCheckpoint(
		const FBattleEngine& Engine)
{
		const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
		const FTrainerId TrainerId = MakeNumericId<FTrainerId>(PlayerTrainerValue);
		const FBattleEngineState& State =
			FBattleC09BWildFlowEngineFixture::GetState(Engine);
		FTargetCheckpointObservation Observation;
		Observation.Action = ObserveActionStartCheckpoint(Engine, ActorId, TrainerId);
		Observation.Mechanics = ObserveAtomicSwitchCheckpoint(Engine);
		if (State.LockedActions.IsValidIndex(State.CurrentLockedActionIndex))
		{
			Observation.bHasCurrentAction = true;
			Observation.CurrentAction = State.LockedActions[State.CurrentLockedActionIndex];
		}
		for (const FBattleBattlerState& Battler : State.Battlers)
		{
			Observation.Battlers.Add(ObserveTargetCheckpointBattler(State, Battler));
		}
		for (const FBattleRandomDraw& Draw : State.Random->GetTrace())
		{
			Observation.RandomTrace.Add(Draw);
		}
		Observation.PendingDecision = State.PendingDecision;
		Observation.PendingRequests = State.PendingDecisionRequests;
		Observation.PendingReplacements = State.PendingReplacements;
		for (const FBattleEvent& Event : State.OrderedEvents)
		{
			Observation.TargetSuccessEventCount +=
				IsTargetCheckpointSuccessEvent(Event) ? 1 : 0;
		}
		return Observation;
	}

bool AreTargetPendingRequestsIdentical(
		const TConstArrayView<FBattleDecisionRequest> Left,
		const TConstArrayView<FBattleDecisionRequest> Right)
{
		if (Left.Num() != Right.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < Left.Num(); ++Index)
		{
			if (!ArePivotTestRequestsIdentical(Left[Index], Right[Index]))
			{
				return false;
			}
		}
		return true;
	}

bool AreTargetPendingReplacementsIdentical(
		const TConstArrayView<FBattlePendingReplacementState> Left,
		const TConstArrayView<FBattlePendingReplacementState> Right)
{
		if (Left.Num() != Right.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < Left.Num(); ++Index)
		{
			if (Left[Index].TrainerId != Right[Index].TrainerId
				|| Left[Index].ActiveSlotId != Right[Index].ActiveSlotId)
			{
				return false;
			}
		}
		return true;
	}

bool AreTargetCheckpointGameplayFactsIdentical(
		const FTargetCheckpointObservation& Left,
		const FTargetCheckpointObservation& Right)
{
		if (!AreAtomicSwitchMechanicsIdentical(Left.Mechanics, Right.Mechanics)
			|| Left.bHasCurrentAction != Right.bHasCurrentAction
			|| (Left.bHasCurrentAction
				&& !ArePivotTestLockedActionsIdentical(
					Left.CurrentAction,
					Right.CurrentAction))
			|| Left.Battlers.Num() != Right.Battlers.Num()
			|| Left.RandomTrace != Right.RandomTrace
			|| Left.PendingDecision.IsSet() != Right.PendingDecision.IsSet()
			|| (Left.PendingDecision.IsSet()
				&& !ArePivotTestRequestsIdentical(
					Left.PendingDecision.GetValue(),
					Right.PendingDecision.GetValue()))
			|| !AreTargetPendingRequestsIdentical(
				Left.PendingRequests,
				Right.PendingRequests)
			|| !AreTargetPendingReplacementsIdentical(
				Left.PendingReplacements,
				Right.PendingReplacements)
			|| Left.TargetSuccessEventCount != Right.TargetSuccessEventCount)
		{
			return false;
		}
		for (int32 Index = 0; Index < Left.Battlers.Num(); ++Index)
		{
			if (!AreTargetCheckpointBattlersIdentical(
					Left.Battlers[Index],
					Right.Battlers[Index]))
			{
				return false;
			}
		}
		return true;
	}

bool VerifyRejectedTargetEnvelope(
		FAutomationTestBase& Test,
		const FBattleEngine& Engine,
		const FTargetCheckpointObservation& Before,
		const EBattleRejectionReason ExpectedReason,
		const FBattleResolution& Returned)
{
		const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
		const FTrainerId TrainerId = MakeNumericId<FTrainerId>(PlayerTrainerValue);
		bool bValid = VerifyRejectedActionStartCheckpoint(
			Test,
			Engine,
			ActorId,
			TrainerId,
			Before.Action,
			Before.Action.StateVersion,
			ExpectedReason,
			Returned);
		const FTargetCheckpointObservation After = ObserveTargetCheckpoint(Engine);
		bValid &= Test.TestTrue(
			TEXT("Rejected target checkpoint publishes one rule cancellation only"),
			Returned.GetEvents().Num() == 1
				&& Returned.GetEvents()[0].GetType()
					== EBattleEventType::ActionCanceled
				&& Returned.GetEvents()[0].GetCause() == EBattleEventCause::Rule);
		bValid &= Test.TestFalse(
			TEXT("Rejected target checkpoint publishes no 3E5 success fact"),
			Returned.GetEvents().ContainsByPredicate(
				[](const FBattleEvent& Event)
				{
					return IsTargetCheckpointSuccessEvent(Event);
				}));
		bValid &= Test.TestEqual(
			TEXT("Rejected target checkpoint preserves prior success-event history"),
			After.TargetSuccessEventCount,
			Before.TargetSuccessEventCount);
		const FBattleReplayRecord Replay = Engine.ExportReplayRecord();
		bValid &= Test.TestEqual(
			TEXT("Rejected target checkpoint replay remains schema 6"),
			Replay.GetSchemaVersion(),
			static_cast<uint32>(6));
		bValid &= Test.TestTrue(
			TEXT("Rejected target checkpoint replay appends the same rejection once"),
			!Replay.GetResolutions().IsEmpty()
				&& Replay.GetResolutions().Last().GetResolutionId()
					== Returned.GetResolutionId()
				&& Replay.GetResolutions().Last().GetEvents().Num() == 1
				&& !IsTargetCheckpointSuccessEvent(
					Replay.GetResolutions().Last().GetEvents()[0]));
		return bValid;
	}

bool VerifyRejectedTargetCheckpoint(
		FAutomationTestBase& Test,
		const FBattleEngine& Engine,
		const FTargetCheckpointObservation& Before,
		const EBattleRejectionReason ExpectedReason,
		const FBattleResolution& Returned)
{
		bool bValid = VerifyRejectedTargetEnvelope(
			Test,
			Engine,
			Before,
			ExpectedReason,
			Returned);
		bValid &= Test.TestTrue(
			TEXT("Rejected target checkpoint preserves action, PP, battlers, positions, charge, triggers, cursor, pending facts and parent RNG"),
			AreTargetCheckpointGameplayFactsIdentical(
				ObserveTargetCheckpoint(Engine),
				Before));
		return bValid;
	}
}

#endif
