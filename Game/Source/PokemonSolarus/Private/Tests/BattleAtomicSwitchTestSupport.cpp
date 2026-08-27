#if WITH_DEV_AUTOMATION_TESTS

#include "BattleAtomicSwitchTestSupport.h"

namespace BattleAtomicSwitchTestSupportPrivate
{
	using namespace BattleAtomicCheckpointTestCommonPrivate;

FAtomicWildScenario MakeAtomicVoluntarySwitchScenario(
		const FItemId IncomingItemId ,
		const FAbilityId IncomingAbilityId ,
		const int32 IncomingCurrentHP )
{
		FAtomicWildScenario Scenario;
		Scenario.bVoluntarySwitchFlow = true;
		Scenario.SwitchIncomingHeldItemId = IncomingItemId;
		Scenario.SwitchIncomingAbilityId = IncomingAbilityId;
		Scenario.SwitchIncomingCurrentHP = IncomingCurrentHP;
		return Scenario;
	}

FAtomicWildScenario MakeAtomicPivotSwitchScenario(
		const FItemId IncomingItemId ,
		const FAbilityId IncomingAbilityId ,
		const int32 IncomingCurrentHP ,
		const bool bSecondReserve )
{
		FAtomicWildScenario Scenario = MakeAtomicVoluntarySwitchScenario(
			IncomingItemId,
			IncomingAbilityId,
			IncomingCurrentHP);
		Scenario.bPivotSwitchFlow = true;
		Scenario.bSecondSwitchReserve = bSecondReserve;
		Scenario.PlayerLeftSpeed = 150;
		Scenario.OpponentLeftSpeed = 50;
		return Scenario;
	}

bool TrySeedAtomicSwitchHazard(
		FBattleEngine& Engine,
		const FConditionId HazardId,
		const int32 Layers ,
		const EBattleSide AffectedSide )
{
		FBattleEngineState& State =
			FBattleC09BWildFlowEngineFixture::GetMutableState(Engine);
		FBattleTriggerSubject Owner;
		FBattleTriggerSubject Source;
		const FBattlerId SourceBattlerId =
			MakeNumericId<FBattlerId>(
				AffectedSide == EBattleSide::Player
					? OpponentLeftValue
					: PlayerLeftValue);
		if (Layers <= 0
			|| FBattleFieldSideConditionRules::GetConditionFamily(HazardId)
				!= EBattleConditionKind::Hazard
			|| !FBattleTriggerSubject::TryCreateSide(AffectedSide, Owner)
			|| !FBattleTriggerSubject::TryCreateBattler(SourceBattlerId, Source))
		{
			return false;
		}
		FBattleSideState* Side = State.Sides.FindByPredicate(
			[AffectedSide](const FBattleSideState& Candidate)
			{
				return Candidate.Side == AffectedSide;
			});
		if (Side == nullptr
			|| Side->Hazards.ContainsByPredicate(
				[HazardId](const FBattleConditionState& Candidate)
				{
					return Candidate.ConditionId == HazardId;
				}))
		{
			return false;
		}

		FBattleFieldSideTriggerRegistrationFacts Facts;
		Facts.ConditionId = HazardId;
		Facts.PayloadId = HazardId.GetDefinitionId();
		Facts.Owner = Owner;
		Facts.Source = Source;
		Facts.Targets.Add(Owner);
		Facts.Layers = Layers;
		EBattleTriggerError TriggerError = EBattleTriggerError::None;
		if (!FBattleFieldSideConditionRules::TryRegisterTriggers(
				State.TriggerFramework,
				Facts,
				TriggerError))
		{
			return false;
		}
		FBattleConditionState Condition;
		Condition.ConditionId = HazardId;
		Condition.LayerCount = Layers;
		Condition.CreationOrdinal = State.NextConditionCreationOrdinal++;
		Condition.SourceBattlerId = SourceBattlerId;
		Side->Hazards.Add(MoveTemp(Condition));
		TArray<FBattleTriggerEffectRequest> IgnoredRequests;
		TArray<FBattleTriggerLifecycleFact> IgnoredLifecycle;
		State.TriggerFramework.DrainEffectRequests(IgnoredRequests);
		State.TriggerFramework.DrainLifecycleFacts(IgnoredLifecycle);
		return true;
	}

bool TrySeedAtomicSwitchOutgoingTransients(FBattleEngine& Engine)
{
		const FBattlerId OutgoingId = MakeNumericId<FBattlerId>(PlayerLeftValue);
		FBattleEngineState& State =
			FBattleC09BWildFlowEngineFixture::GetMutableState(Engine);
		FBattleBattlerState* Outgoing = State.FindMutableBattler(OutgoingId);
		if (Outgoing == nullptr)
		{
			return false;
		}
		const FBattleStatStageChangeResult Change =
			Outgoing->Stages.ApplyChange(EBattleStat::Attack, 2);
		Outgoing->LastMoveId = MakeDefinitionId<FMoveId>(ProbeMoveName);
		return Change.Outcome == EBattleStatStageChangeOutcome::Applied;
	}

FAtomicSwitchConditionObservation ObserveAtomicSwitchCondition(
		const FBattleConditionState& Condition)
{
		FAtomicSwitchConditionObservation Observation;
		Observation.ConditionId = Condition.ConditionId;
		Observation.bHasRemainingTurns = Condition.RemainingTurns.IsSet();
		Observation.RemainingTurns = Condition.RemainingTurns.Get(0);
		Observation.LayerCount = Condition.LayerCount;
		Observation.CreationOrdinal = Condition.CreationOrdinal;
		Observation.SourceBattlerId = Condition.SourceBattlerId;
		return Observation;
	}

bool AreAtomicSwitchConditionsIdentical(
		const TArray<FAtomicSwitchConditionObservation>& Left,
		const TArray<FAtomicSwitchConditionObservation>& Right)
{
		if (Left.Num() != Right.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < Left.Num(); ++Index)
		{
			const FAtomicSwitchConditionObservation& L = Left[Index];
			const FAtomicSwitchConditionObservation& R = Right[Index];
			if (L.ConditionId != R.ConditionId
				|| L.bHasRemainingTurns != R.bHasRemainingTurns
				|| L.RemainingTurns != R.RemainingTurns
				|| L.LayerCount != R.LayerCount
				|| L.CreationOrdinal != R.CreationOrdinal
				|| L.SourceBattlerId != R.SourceBattlerId)
			{
				return false;
			}
		}
		return true;
	}

FAtomicSwitchBattlerObservation ObserveAtomicSwitchBattler(
		const FBattleEngineState& State,
		const FBattlerId BattlerId)
{
		FAtomicSwitchBattlerObservation Observation;
		const FBattleBattlerState* Battler = State.FindBattler(BattlerId);
		if (Battler == nullptr)
		{
			return Observation;
		}
		Observation.TrainerId = Battler->TrainerId;
		Observation.BattlerId = Battler->BattlerId;
		Observation.PartySlotId = Battler->PartySlotId;
		Observation.CurrentHP = Battler->CurrentHP;
		for (uint8 StatValue = static_cast<uint8>(EBattleStat::Attack);
			StatValue <= static_cast<uint8>(EBattleStat::Evasion);
			++StatValue)
		{
			int32 Stage = 0;
			const bool bRead = Battler->Stages.TryGetStage(
				static_cast<EBattleStat>(StatValue),
				Stage);
			check(bRead);
			Observation.Stages.Add(Stage);
		}
		for (const FBattleConditionState& Condition : Battler->Volatiles)
		{
			Observation.Volatiles.Add(ObserveAtomicSwitchCondition(Condition));
		}
		Observation.MajorStatusId = Battler->MajorStatusId;
		Observation.AbilityId = Battler->AbilityId;
		Observation.HeldItem = Battler->HeldItem;
		Observation.LastMoveId = Battler->LastMoveId;
		Observation.EnteredActiveOnTurnId = Battler->EnteredActiveOnTurnId;
		Observation.bFainted = Battler->bFainted;
		Observation.bCaptured = Battler->bCaptured;
		Observation.bRemoved = Battler->bRemoved;
		Observation.bFaintTransitionPending = Battler->bFaintTransitionPending;
		Observation.bAbilitySuppressed = Battler->bAbilitySuppressed;
		return Observation;
	}

bool AreAtomicSwitchBattlersIdentical(
		const FAtomicSwitchBattlerObservation& Left,
		const FAtomicSwitchBattlerObservation& Right)
{
		return Left.TrainerId == Right.TrainerId
			&& Left.BattlerId == Right.BattlerId
			&& Left.PartySlotId == Right.PartySlotId
			&& Left.CurrentHP == Right.CurrentHP
			&& Left.Stages == Right.Stages
			&& AreAtomicSwitchConditionsIdentical(Left.Volatiles, Right.Volatiles)
			&& Left.MajorStatusId == Right.MajorStatusId
			&& Left.AbilityId == Right.AbilityId
			&& AreActionStartHeldItemsIdentical(Left.HeldItem, Right.HeldItem)
			&& Left.LastMoveId == Right.LastMoveId
			&& Left.EnteredActiveOnTurnId == Right.EnteredActiveOnTurnId
			&& Left.bFainted == Right.bFainted
			&& Left.bCaptured == Right.bCaptured
			&& Left.bRemoved == Right.bRemoved
			&& Left.bFaintTransitionPending == Right.bFaintTransitionPending
			&& Left.bAbilitySuppressed == Right.bAbilitySuppressed;
	}

bool IsAtomicSwitchDefinitionRevealed(
		const FBattleEngineState& State,
		const FBattlerId BattlerId,
		const bool bAbility)
{
		const FBattleBattlerState* Battler = State.FindBattler(BattlerId);
		FBattleTriggerSubject Owner;
		FBattleTriggerSourceDefinition Source;
		if (Battler == nullptr
			|| !FBattleTriggerSubject::TryCreateBattler(BattlerId, Owner)
			|| !(bAbility
				? FBattleTriggerSourceDefinition::TryCreateAbility(Battler->AbilityId, Source)
				: FBattleTriggerSourceDefinition::TryCreateItem(
					Battler->HeldItem.CurrentItemId,
					Source)))
		{
			return false;
		}
		return State.AbilityItemRevealTracker.HasBeenRevealed(Source, Owner);
	}

FAtomicSwitchCheckpointObservation ObserveAtomicSwitchCheckpoint(
		const FBattleEngine& Engine)
{
		const FBattleEngineState& State =
			FBattleC09BWildFlowEngineFixture::GetState(Engine);
		FAtomicSwitchCheckpointObservation Observation;
		Observation.StateVersion = State.StateVersion;
		Observation.NextResolutionId = State.NextResolutionId;
		Observation.NextEventOrdinal = State.NextEventOrdinal;
		Observation.NextConditionCreationOrdinal = State.NextConditionCreationOrdinal;
		Observation.NextTriggerToken = State.NextTriggerReentrancyToken;
		Observation.ActionIndex = State.CurrentLockedActionIndex;
		Observation.ResolutionCount = State.Resolutions.Num();
		Observation.EventCount = State.OrderedEvents.Num();
		Observation.RandomTraceCount = State.Random->GetTrace().Num();
		Observation.PendingDecisionRequestCount = State.PendingDecisionRequests.Num();
		Observation.PendingReplacementCount = State.PendingReplacements.Num();
		Observation.OpponentRemovalCheckpointCount =
			State.AvailableOpponentRemovalCheckpoints.Num();
		Observation.PendingTriggerDispatchCount =
			State.TriggerFramework.GetPendingDispatchCount();
		Observation.PendingTriggerEffectCount =
			State.TriggerFramework.GetPendingEffectRequestCount();
		Observation.PendingTriggerLifecycleCount =
			State.TriggerFramework.GetPendingLifecycleFactCount();
		Observation.Phase = State.Phase;
		Observation.Outcome = State.Outcome;
		Observation.OutcomeCause = State.OutcomeCause;
		Observation.bPendingDecisionSet = State.PendingDecision.IsSet();
		if (State.LockedActions.IsValidIndex(State.CurrentLockedActionIndex))
		{
			const FBattleLockedActionState& Action =
				State.LockedActions[State.CurrentLockedActionIndex];
			Observation.bActionStarted = Action.bStarted;
			Observation.bActionFinished = Action.bFinished;
		}
		const FBattlerId OutgoingId = MakeNumericId<FBattlerId>(PlayerLeftValue);
		const FBattlerId IncomingId = MakeNumericId<FBattlerId>(PlayerReserveValue);
		Observation.Outgoing = ObserveAtomicSwitchBattler(State, OutgoingId);
		Observation.Incoming = ObserveAtomicSwitchBattler(State, IncomingId);
		Observation.Opponent = ObserveAtomicSwitchBattler(
			State,
			MakeNumericId<FBattlerId>(OpponentLeftValue));
		Observation.bIncomingAbilityRevealed =
			IsAtomicSwitchDefinitionRevealed(State, IncomingId, true);
		Observation.bIncomingItemRevealed =
			IsAtomicSwitchDefinitionRevealed(State, IncomingId, false);
		for (const FBattleActivePositionState& Active : State.ActivePositions)
		{
			Observation.ActiveSlotIds.Add(Active.ActiveSlotId);
			Observation.ActiveTrainerIds.Add(Active.TrainerId);
			Observation.ActiveBattlerIds.Add(Active.BattlerId);
			Observation.ActiveAvailability.Add(Active.bAvailable ? 1 : 0);
		}
		const FBattleSideState* PlayerSide = State.Sides.FindByPredicate(
			[](const FBattleSideState& Candidate)
			{
				return Candidate.Side == EBattleSide::Player;
			});
		if (PlayerSide != nullptr)
		{
			for (const FBattleConditionState& Hazard : PlayerSide->Hazards)
			{
				Observation.PlayerHazards.Add(ObserveAtomicSwitchCondition(Hazard));
			}
		}
		Observation.LedgerStates.Append(State.HeldItemLedger.GetStates());
		for (const FBattleTriggerRegistrationState& Registration :
			State.TriggerFramework.GetActiveRegistrations())
		{
			Observation.TriggerRegistrationIds.Add(Registration.RegistrationId);
			Observation.TriggerCreationOrdinals.Add(Registration.CreationOrdinal);
			Observation.TriggerSources.Add(Registration.Spec.SourceDefinition);
			Observation.TriggerSuppression.Add(Registration.bSuppressed ? 1 : 0);
		}
		return Observation;
	}

bool AreAtomicSwitchMechanicsIdentical(
		const FAtomicSwitchCheckpointObservation& Left,
		const FAtomicSwitchCheckpointObservation& Right)
{
		return Left.NextConditionCreationOrdinal == Right.NextConditionCreationOrdinal
			&& Left.NextTriggerToken == Right.NextTriggerToken
			&& Left.ActionIndex == Right.ActionIndex
			&& Left.RandomTraceCount == Right.RandomTraceCount
			&& Left.PendingDecisionRequestCount == Right.PendingDecisionRequestCount
			&& Left.PendingReplacementCount == Right.PendingReplacementCount
			&& Left.OpponentRemovalCheckpointCount == Right.OpponentRemovalCheckpointCount
			&& Left.PendingTriggerDispatchCount == Right.PendingTriggerDispatchCount
			&& Left.PendingTriggerEffectCount == Right.PendingTriggerEffectCount
			&& Left.PendingTriggerLifecycleCount == Right.PendingTriggerLifecycleCount
			&& Left.Phase == Right.Phase
			&& Left.Outcome == Right.Outcome
			&& Left.OutcomeCause == Right.OutcomeCause
			&& Left.bPendingDecisionSet == Right.bPendingDecisionSet
			&& Left.bActionStarted == Right.bActionStarted
			&& Left.bActionFinished == Right.bActionFinished
			&& Left.bIncomingAbilityRevealed == Right.bIncomingAbilityRevealed
			&& Left.bIncomingItemRevealed == Right.bIncomingItemRevealed
			&& AreAtomicSwitchBattlersIdentical(Left.Outgoing, Right.Outgoing)
			&& AreAtomicSwitchBattlersIdentical(Left.Incoming, Right.Incoming)
			&& AreAtomicSwitchBattlersIdentical(Left.Opponent, Right.Opponent)
			&& Left.ActiveSlotIds == Right.ActiveSlotIds
			&& Left.ActiveTrainerIds == Right.ActiveTrainerIds
			&& Left.ActiveBattlerIds == Right.ActiveBattlerIds
			&& Left.ActiveAvailability == Right.ActiveAvailability
			&& AreAtomicSwitchConditionsIdentical(Left.PlayerHazards, Right.PlayerHazards)
			&& Left.LedgerStates == Right.LedgerStates
			&& Left.TriggerRegistrationIds == Right.TriggerRegistrationIds
			&& Left.TriggerCreationOrdinals == Right.TriggerCreationOrdinals
			&& Left.TriggerSources == Right.TriggerSources
			&& Left.TriggerSuppression == Right.TriggerSuppression;
	}

bool ArePivotTestDecisionsIdentical(
		const FBattleDecision& Left,
		const FBattleDecision& Right)
{
		return Left.IsValid() == Right.IsValid()
			&& Left.GetStateVersion() == Right.GetStateVersion()
			&& Left.GetRequestKind() == Right.GetRequestKind()
			&& Left.GetDecisionOwnerTrainerId() == Right.GetDecisionOwnerTrainerId()
			&& Left.GetActingBattlerId() == Right.GetActingBattlerId()
			&& Left.GetActionKind() == Right.GetActionKind()
			&& Left.GetMoveId() == Right.GetMoveId()
			&& Left.GetSwitchPartySlotId() == Right.GetSwitchPartySlotId()
			&& Left.GetItemId() == Right.GetItemId()
			&& Left.GetItemPartyTargetId() == Right.GetItemPartyTargetId()
			&& Left.GetActiveTargetId() == Right.GetActiveTargetId();
	}

bool ArePivotTestRequestsIdentical(
		const FBattleDecisionRequest& Left,
		const FBattleDecisionRequest& Right)
{
		return Left.IsValid() == Right.IsValid()
			&& Left.GetStateVersion() == Right.GetStateVersion()
			&& Left.GetRequestKind() == Right.GetRequestKind()
			&& Left.GetDecisionOwnerTrainerId() == Right.GetDecisionOwnerTrainerId()
			&& Left.GetActingBattlerId() == Right.GetActingBattlerId()
			&& Left.GetActingSlotId() == Right.GetActingSlotId()
			&& AreOrderedPivotTestValuesEqual(
				Left.GetLegalActionKinds(),
				Right.GetLegalActionKinds(),
				[](const EBattleActionKind L, const EBattleActionKind R)
				{
					return L == R;
				})
			&& AreOrderedPivotTestValuesEqual(
				Left.GetLegalMoveIds(),
				Right.GetLegalMoveIds(),
				[](const FMoveId& L, const FMoveId& R)
				{
					return L == R;
				})
			&& AreOrderedPivotTestValuesEqual(
				Left.GetAutomaticallyTargetedMoveIds(),
				Right.GetAutomaticallyTargetedMoveIds(),
				[](const FMoveId& L, const FMoveId& R)
				{
					return L == R;
				})
			&& AreOrderedPivotTestValuesEqual(
				Left.GetLegalSwitchPartySlots(),
				Right.GetLegalSwitchPartySlots(),
				[](const FPartySlotId L, const FPartySlotId R)
				{
					return L == R;
				})
			&& AreOrderedPivotTestValuesEqual(
				Left.GetLegalItemIds(),
				Right.GetLegalItemIds(),
				[](const FItemId& L, const FItemId& R)
				{
					return L == R;
				})
			&& AreOrderedPivotTestValuesEqual(
				Left.GetLegalActiveTargets(),
				Right.GetLegalActiveTargets(),
				[](const FActiveSlotId L, const FActiveSlotId R)
				{
					return L == R;
				})
			&& AreOrderedPivotTestValuesEqual(
				Left.GetLegalPartyTargets(),
				Right.GetLegalPartyTargets(),
				[](const FPartySlotId L, const FPartySlotId R)
				{
					return L == R;
				})
			&& AreOrderedPivotTestValuesEqual(
				Left.GetLegalMoveTargets(),
				Right.GetLegalMoveTargets(),
				[](const FBattleMoveTargetOption& L, const FBattleMoveTargetOption& R)
				{
					return L.MoveId == R.MoveId && L.ActiveSlotId == R.ActiveSlotId;
				})
			&& AreOrderedPivotTestValuesEqual(
				Left.GetLegalItemPartyTargets(),
				Right.GetLegalItemPartyTargets(),
				[](const FBattleItemPartyTargetOption& L,
					const FBattleItemPartyTargetOption& R)
				{
					return L.ItemId == R.ItemId && L.PartySlotId == R.PartySlotId;
				})
			&& AreOrderedPivotTestValuesEqual(
				Left.GetLegalItemActiveTargets(),
				Right.GetLegalItemActiveTargets(),
				[](const FBattleItemActiveTargetOption& L,
					const FBattleItemActiveTargetOption& R)
				{
					return L.ItemId == R.ItemId && L.ActiveSlotId == R.ActiveSlotId;
				})
			&& AreOrderedPivotTestValuesEqual(
				Left.GetUnavailableOptions(),
				Right.GetUnavailableOptions(),
				[](const FBattleUnavailableDecisionOption& L,
					const FBattleUnavailableDecisionOption& R)
				{
					return L.Kind == R.Kind
						&& L.Reason == R.Reason
						&& L.ActionKind == R.ActionKind
						&& L.MoveId == R.MoveId
						&& L.PartySlotId == R.PartySlotId
						&& L.ItemId == R.ItemId
						&& L.ActiveSlotId == R.ActiveSlotId;
				});
	}

bool ArePivotTestTargetResolutionsIdentical(
		const TOptional<FBattleTargetResolutionResult>& Left,
		const TOptional<FBattleTargetResolutionResult>& Right)
{
		if (Left.IsSet() != Right.IsSet())
		{
			return false;
		}
		if (!Left.IsSet())
		{
			return true;
		}
		const FBattleTargetResolutionResult& L = Left.GetValue();
		const FBattleTargetResolutionResult& R = Right.GetValue();
		return L.TargetClass == R.TargetClass
			&& L.Outcome == R.Outcome
			&& L.bWasRedirected == R.bWasRedirected
			&& L.bUsedFaintedTargetFallback == R.bUsedFaintedTargetFallback
			&& AreOrderedPivotTestValuesEqual(
				TConstArrayView<FBattleResolvedTarget>(L.Targets),
				TConstArrayView<FBattleResolvedTarget>(R.Targets),
				[](const FBattleResolvedTarget& LTarget,
					const FBattleResolvedTarget& RTarget)
				{
					return LTarget == RTarget;
				});
	}

bool ArePivotTestLockedActionsIdentical(
		const FBattleLockedActionState& Left,
		const FBattleLockedActionState& Right)
{
		return Left.ActionId == Right.ActionId
			&& Left.QueueOrdinal == Right.QueueOrdinal
			&& ArePivotTestDecisionsIdentical(Left.Decision, Right.Decision)
			&& Left.OrderKey.CommandBand == Right.OrderKey.CommandBand
			&& Left.OrderKey.MovePriority == Right.OrderKey.MovePriority
			&& Left.OrderKey.FractionalPriorityTenths
				== Right.OrderKey.FractionalPriorityTenths
			&& Left.OrderKey.EffectiveSpeed == Right.OrderKey.EffectiveSpeed
			&& Left.OrderKey.ActingSlotId == Right.OrderKey.ActingSlotId
			&& Left.TargetClass == Right.TargetClass
			&& Left.SelectedTargetBattlerId == Right.SelectedTargetBattlerId
			&& Left.bStarted == Right.bStarted
			&& Left.bMoveCommitted == Right.bMoveCommitted
			&& ArePivotTestTargetResolutionsIdentical(
				Left.TargetResolution,
				Right.TargetResolution)
			&& Left.EffectExecutionState == Right.EffectExecutionState
			&& Left.bFinished == Right.bFinished;
	}
}

#endif
