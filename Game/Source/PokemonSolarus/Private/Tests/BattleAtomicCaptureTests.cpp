#if WITH_DEV_AUTOMATION_TESTS

#include "BattleAtomicCheckpointTestCommon.h"
#include "BattleAtomicCheckpointTestFaults.h"

namespace BattleAtomicCaptureTestsPrivate
{
	using namespace BattleAtomicCheckpointTestCommonPrivate;
	using namespace BattleAtomicCheckpointTestFaultsPrivate;

bool PrepareCaptureAsLastActionAfterFailedWildFlee(FBattleEngine& Engine)
	{
		if (Engine.GetSnapshot().GetPhase() == EBattlePhase::Setup)
		{
			FBattleRejection Rejection;
			if (!Engine.TryBeginActionDecisionSequence(Rejection))
			{
				return false;
			}
		}

		int32 Guard = 0;
		while (!Engine.GetPendingDecisionRequests().IsEmpty() && Guard++ < 4)
		{
			const TArray<FBattleDecisionRequest> Requests =
				Engine.GetPendingDecisionRequests();
			TArray<FBattleDecision> Decisions;
			for (const FBattleDecisionRequest& Request : Requests)
			{
				const FBattlerId ActorId = Request.GetActingBattlerId();
				if (ActorId == MakeNumericId<FBattlerId>(PlayerLeftValue))
				{
					Decisions.Add(MakeDecision(Request, EBattleActionKind::Bag));
				}
				else if (ActorId == MakeNumericId<FBattlerId>(OpponentLeftValue))
				{
					Decisions.Add(MakeDecision(Request, EBattleActionKind::WildFlee));
				}
				else
				{
					Decisions.Add(MakeDecision(Request, EBattleActionKind::Fight));
				}
			}
			if (!Engine.SubmitDecisionBatch(
					MakeBatch(Requests, MoveTemp(Decisions))).WasAccepted())
			{
				return false;
			}
		}
		if (Guard >= 4 || Engine.GetSnapshot().GetPhase() != EBattlePhase::Locked
			|| !BeginExpectedWildAction(
				Engine,
				OpponentLeftValue,
				EBattleActionKind::WildFlee))
		{
			return false;
		}

		const FBattleResolution FailedFlee = Engine.ExecuteCurrentWildAction();
		if (!FailedFlee.WasAccepted()
			|| FBattleC09BWildFlowEngineFixture::IsRemoved(
				Engine,
				MakeNumericId<FBattlerId>(OpponentLeftValue))
			|| !BeginExpectedWildAction(
				Engine,
				PlayerLeftValue,
				EBattleActionKind::Bag))
		{
			return false;
		}

		const FBattleEngineState& State =
			FBattleC09BWildFlowEngineFixture::GetState(Engine);
		return State.CurrentLockedActionIndex == State.LockedActions.Num() - 1;
	}

template <typename LeftRangeType, typename RightRangeType, typename EqualType>
	bool AreOrderedCaptureValuesIdentical(
		const LeftRangeType& Left,
		const RightRangeType& Right,
		EqualType Equal)
	{
		if (Left.Num() != Right.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < Left.Num(); ++Index)
		{
			if (!Equal(Left[Index], Right[Index]))
			{
				return false;
			}
		}
		return true;
	}

bool AreCaptureStatsIdentical(
		const FPokemonBattleStats& Left,
		const FPokemonBattleStats& Right)
	{
		return Left.MaxHP == Right.MaxHP
			&& Left.Attack == Right.Attack
			&& Left.Defense == Right.Defense
			&& Left.SpecialAttack == Right.SpecialAttack
			&& Left.SpecialDefense == Right.SpecialDefense
			&& Left.Speed == Right.Speed;
	}

bool AreCaptureStatStagesIdentical(
		const FBattleStatStages& Left,
		const FBattleStatStages& Right)
	{
		for (uint8 StatValue = static_cast<uint8>(EBattleStat::Attack);
			StatValue <= static_cast<uint8>(EBattleStat::Evasion);
			++StatValue)
		{
			int32 LeftStage = 0;
			int32 RightStage = 0;
			if (!Left.TryGetStage(static_cast<EBattleStat>(StatValue), LeftStage)
				|| !Right.TryGetStage(static_cast<EBattleStat>(StatValue), RightStage)
				|| LeftStage != RightStage)
			{
				return false;
			}
		}
		return true;
	}

bool AreCaptureConditionsIdentical(
		const TConstArrayView<FBattleConditionState> Left,
		const TConstArrayView<FBattleConditionState> Right)
	{
		return AreOrderedCaptureValuesIdentical(
			Left,
			Right,
			[](const FBattleConditionState& L, const FBattleConditionState& R)
			{
				return L.ConditionId == R.ConditionId
					&& L.RemainingTurns == R.RemainingTurns
					&& L.LayerCount == R.LayerCount
					&& L.CreationOrdinal == R.CreationOrdinal
					&& L.SourceBattlerId == R.SourceBattlerId;
			});
	}

bool AreCaptureHeldItemsIdentical(
		const FBattleHeldItemState& Left,
		const FBattleHeldItemState& Right)
	{
		return Left.InstanceId == Right.InstanceId
			&& Left.OriginalItemId == Right.OriginalItemId
			&& Left.CurrentItemId == Right.CurrentItemId
			&& Left.bConsumed == Right.bConsumed
			&& Left.bSuppressed == Right.bSuppressed
			&& Left.bRevealed == Right.bRevealed
			&& Left.bTemporarilyRemoved == Right.bTemporarilyRemoved
			&& Left.ChoiceLockedMoveId == Right.ChoiceLockedMoveId;
	}

bool AreCaptureBattlersIdentical(
		const TConstArrayView<FBattleBattlerState> Left,
		const TConstArrayView<FBattleBattlerState> Right)
	{
		return AreOrderedCaptureValuesIdentical(
			Left,
			Right,
			[](const FBattleBattlerState& L, const FBattleBattlerState& R)
			{
				return L.TrainerId == R.TrainerId
					&& L.BattlerId == R.BattlerId
					&& L.SourcePokemonId == R.SourcePokemonId
					&& L.PartySlotId == R.PartySlotId
					&& L.SpeciesFormId == R.SpeciesFormId
					&& L.CaptureClassification == R.CaptureClassification
					&& L.Level == R.Level
					&& AreCaptureStatsIdentical(L.PermanentStats, R.PermanentStats)
					&& L.CurrentHP == R.CurrentHP
					&& L.bFainted == R.bFainted
					&& L.bCaptured == R.bCaptured
					&& L.bRemoved == R.bRemoved
					&& L.bFaintTransitionPending == R.bFaintTransitionPending
					&& L.bEgg == R.bEgg
					&& L.MajorStatusId == R.MajorStatusId
					&& AreCaptureStatStagesIdentical(L.Stages, R.Stages)
					&& AreCaptureConditionsIdentical(L.Volatiles, R.Volatiles)
					&& L.AbilityId == R.AbilityId
					&& L.bAbilitySuppressed == R.bAbilitySuppressed
					&& L.EnteredActiveOnTurnId == R.EnteredActiveOnTurnId
					&& AreCaptureHeldItemsIdentical(L.HeldItem, R.HeldItem)
					&& AreOrderedCaptureValuesIdentical(
						L.Moves,
						R.Moves,
						[](const FBattleMoveSlotState& LMove,
							const FBattleMoveSlotState& RMove)
						{
							return LMove.SlotIndex == RMove.SlotIndex
								&& LMove.MoveId == RMove.MoveId
								&& LMove.CurrentPP == RMove.CurrentPP
								&& LMove.MaxPP == RMove.MaxPP;
						})
					&& L.LastMoveId == R.LastMoveId
					&& L.Obedience.bHasSnapshot == R.Obedience.bHasSnapshot
					&& L.Obedience.bSubjectToPlayerCap
						== R.Obedience.bSubjectToPlayerCap
					&& L.Obedience.ReferenceLevel == R.Obedience.ReferenceLevel
					&& L.Obedience.BadgeCount == R.Obedience.BadgeCount;
			});
	}

bool AreCaptureTrainersIdentical(
		const TConstArrayView<FBattleTrainerState> Left,
		const TConstArrayView<FBattleTrainerState> Right)
	{
		return AreOrderedCaptureValuesIdentical(
			Left,
			Right,
			[](const FBattleTrainerState& L, const FBattleTrainerState& R)
			{
				return L.TrainerId == R.TrainerId
					&& L.Side == R.Side
					&& L.Role == R.Role
					&& L.Controller == R.Controller
					&& L.SelectorProfileId == R.SelectorProfileId
					&& AreOrderedCaptureValuesIdentical(
						L.Bag,
						R.Bag,
						[](const FBattleBagItemCount& LItem,
							const FBattleBagItemCount& RItem)
						{
							return LItem.ItemId == RItem.ItemId
								&& LItem.Count == RItem.Count;
						})
					&& L.ActionAllowance.MaximumActions
						== R.ActionAllowance.MaximumActions
					&& L.ActionAllowance.RemainingActions
						== R.ActionAllowance.RemainingActions
					&& L.ActionAllowance.bBagActionAvailable
						== R.ActionAllowance.bBagActionAvailable
					&& AreOrderedCaptureValuesIdentical(
						L.PartySlots,
						R.PartySlots,
						[](const FBattlePartySlotState& LSlot,
							const FBattlePartySlotState& RSlot)
						{
							return LSlot.PartySlotId == RSlot.PartySlotId
								&& LSlot.BattlerId == RSlot.BattlerId;
						});
			});
	}

bool AreCaptureActivePositionsIdentical(
		const TConstArrayView<FBattleActivePositionState> Left,
		const TConstArrayView<FBattleActivePositionState> Right)
	{
		return AreOrderedCaptureValuesIdentical(
			Left,
			Right,
			[](const FBattleActivePositionState& L,
				const FBattleActivePositionState& R)
			{
				return L.ActiveSlotId == R.ActiveSlotId
					&& L.bAvailable == R.bAvailable
					&& L.TrainerId == R.TrainerId
					&& L.BattlerId == R.BattlerId;
			});
	}

bool AreCaptureDecisionsIdentical(
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

bool AreCaptureDecisionRequestsIdentical(
		const FBattleDecisionRequest& Left,
		const FBattleDecisionRequest& Right)
	{
		return Left.IsValid() == Right.IsValid()
			&& Left.GetStateVersion() == Right.GetStateVersion()
			&& Left.GetRequestKind() == Right.GetRequestKind()
			&& Left.GetDecisionOwnerTrainerId() == Right.GetDecisionOwnerTrainerId()
			&& Left.GetActingBattlerId() == Right.GetActingBattlerId()
			&& Left.GetActingSlotId() == Right.GetActingSlotId()
			&& AreOrderedCaptureValuesIdentical(
				Left.GetLegalActionKinds(),
				Right.GetLegalActionKinds(),
				[](const EBattleActionKind L, const EBattleActionKind R)
				{
					return L == R;
				})
			&& AreOrderedCaptureValuesIdentical(
				Left.GetLegalMoveIds(),
				Right.GetLegalMoveIds(),
				[](const FMoveId& L, const FMoveId& R)
				{
					return L == R;
				})
			&& AreOrderedCaptureValuesIdentical(
				Left.GetAutomaticallyTargetedMoveIds(),
				Right.GetAutomaticallyTargetedMoveIds(),
				[](const FMoveId& L, const FMoveId& R)
				{
					return L == R;
				})
			&& AreOrderedCaptureValuesIdentical(
				Left.GetLegalSwitchPartySlots(),
				Right.GetLegalSwitchPartySlots(),
				[](const FPartySlotId L, const FPartySlotId R)
				{
					return L == R;
				})
			&& AreOrderedCaptureValuesIdentical(
				Left.GetLegalItemIds(),
				Right.GetLegalItemIds(),
				[](const FItemId& L, const FItemId& R)
				{
					return L == R;
				})
			&& AreOrderedCaptureValuesIdentical(
				Left.GetLegalActiveTargets(),
				Right.GetLegalActiveTargets(),
				[](const FActiveSlotId L, const FActiveSlotId R)
				{
					return L == R;
				})
			&& AreOrderedCaptureValuesIdentical(
				Left.GetLegalPartyTargets(),
				Right.GetLegalPartyTargets(),
				[](const FPartySlotId L, const FPartySlotId R)
				{
					return L == R;
				})
			&& AreOrderedCaptureValuesIdentical(
				Left.GetLegalMoveTargets(),
				Right.GetLegalMoveTargets(),
				[](const FBattleMoveTargetOption& L,
					const FBattleMoveTargetOption& R)
				{
					return L.MoveId == R.MoveId
						&& L.ActiveSlotId == R.ActiveSlotId;
				})
			&& AreOrderedCaptureValuesIdentical(
				Left.GetLegalItemPartyTargets(),
				Right.GetLegalItemPartyTargets(),
				[](const FBattleItemPartyTargetOption& L,
					const FBattleItemPartyTargetOption& R)
				{
					return L.ItemId == R.ItemId
						&& L.PartySlotId == R.PartySlotId;
				})
			&& AreOrderedCaptureValuesIdentical(
				Left.GetLegalItemActiveTargets(),
				Right.GetLegalItemActiveTargets(),
				[](const FBattleItemActiveTargetOption& L,
					const FBattleItemActiveTargetOption& R)
				{
					return L.ItemId == R.ItemId
						&& L.ActiveSlotId == R.ActiveSlotId;
				})
			&& AreOrderedCaptureValuesIdentical(
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

bool AreCaptureTargetResolutionsIdentical(
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
			&& L.Targets == R.Targets;
	}

bool AreCaptureLockedActionsIdentical(
		const TConstArrayView<FBattleLockedActionState> Left,
		const TConstArrayView<FBattleLockedActionState> Right)
	{
		return AreOrderedCaptureValuesIdentical(
			Left,
			Right,
			[](const FBattleLockedActionState& L,
				const FBattleLockedActionState& R)
			{
				return L.ActionId == R.ActionId
					&& L.QueueOrdinal == R.QueueOrdinal
					&& AreCaptureDecisionsIdentical(L.Decision, R.Decision)
					&& L.OrderKey.CommandBand == R.OrderKey.CommandBand
					&& L.OrderKey.MovePriority == R.OrderKey.MovePriority
					&& L.OrderKey.FractionalPriorityTenths
						== R.OrderKey.FractionalPriorityTenths
					&& L.OrderKey.EffectiveSpeed == R.OrderKey.EffectiveSpeed
					&& L.OrderKey.ActingSlotId == R.OrderKey.ActingSlotId
					&& L.TargetClass == R.TargetClass
					&& L.SelectedTargetBattlerId == R.SelectedTargetBattlerId
					&& L.bStarted == R.bStarted
					&& L.bMoveCommitted == R.bMoveCommitted
					&& AreCaptureTargetResolutionsIdentical(
						L.TargetResolution,
						R.TargetResolution)
					&& L.EffectExecutionState == R.EffectExecutionState
					&& L.bFinished == R.bFinished;
			});
	}

bool AreCapturePendingRequestsIdentical(
		const TConstArrayView<FBattleDecisionRequest> Left,
		const TConstArrayView<FBattleDecisionRequest> Right)
	{
		return AreOrderedCaptureValuesIdentical(
			Left,
			Right,
			[](const FBattleDecisionRequest& L, const FBattleDecisionRequest& R)
			{
				return AreCaptureDecisionRequestsIdentical(L, R);
			});
	}

bool AreCaptureDecisionArraysIdentical(
		const TConstArrayView<FBattleDecision> Left,
		const TConstArrayView<FBattleDecision> Right)
	{
		return AreOrderedCaptureValuesIdentical(
			Left,
			Right,
			[](const FBattleDecision& L, const FBattleDecision& R)
			{
				return AreCaptureDecisionsIdentical(L, R);
			});
	}

bool AreCaptureDecisionOwnerSequencesIdentical(
		const TConstArrayView<FBattleDecisionOwnerState> Left,
		const TConstArrayView<FBattleDecisionOwnerState> Right)
	{
		return AreOrderedCaptureValuesIdentical(
			Left,
			Right,
			[](const FBattleDecisionOwnerState& L,
				const FBattleDecisionOwnerState& R)
			{
				return L.TrainerId == R.TrainerId
					&& L.Controller == R.Controller
					&& AreOrderedCaptureValuesIdentical(
						L.Actors,
						R.Actors,
						[](const FBattleDecisionActorState& LActor,
							const FBattleDecisionActorState& RActor)
						{
							return LActor.BattlerId == RActor.BattlerId
								&& LActor.ActiveSlotId == RActor.ActiveSlotId;
						});
			});
	}

bool AreCaptureReplacementStatesIdentical(
		const TConstArrayView<FBattlePendingReplacementState> Left,
		const TConstArrayView<FBattlePendingReplacementState> Right)
	{
		return AreOrderedCaptureValuesIdentical(
			Left,
			Right,
			[](const FBattlePendingReplacementState& L,
				const FBattlePendingReplacementState& R)
			{
				return L.TrainerId == R.TrainerId
					&& L.ActiveSlotId == R.ActiveSlotId;
			});
	}

bool AreCapturePoliciesIdentical(
		const FBattleCompiledEncounterPolicies& Left,
		const FBattleCompiledEncounterPolicies& Right)
	{
		if (Left.IsValid() != Right.IsValid()
			|| Left.GetEncounterKind() != Right.GetEncounterKind()
			|| Left.GetFormat() != Right.GetFormat()
			|| Left.GetMaximumActiveBattlersPerSide()
				!= Right.GetMaximumActiveBattlersPerSide()
			|| Left.GetMaximumPartySize() != Right.GetMaximumPartySize()
			|| Left.IsRunAllowed() != Right.IsRunAllowed()
			|| Left.IsCaptureAllowed() != Right.IsCaptureAllowed()
			|| Left.IsBagAllowed() != Right.IsBagAllowed()
			|| Left.GetBattleStyle() != Right.GetBattleStyle()
			|| Left.GetReinforcementPolicy() != Right.GetReinforcementPolicy()
			|| Left.IsWildFleeConfigured() != Right.IsWildFleeConfigured()
			|| Left.GetWildFleeMode() != Right.GetWildFleeMode()
			|| Left.GetWildFleeNumerator() != Right.GetWildFleeNumerator()
			|| Left.GetWildFleeDenominator() != Right.GetWildFleeDenominator()
			|| Left.IsScriptedEndingAllowed() != Right.IsScriptedEndingAllowed()
			|| Left.HasSeparatePartnerOwnership() != Right.HasSeparatePartnerOwnership())
		{
			return false;
		}
		return AreOrderedCaptureValuesIdentical(
			Left.GetTrainerPolicies(),
			Right.GetTrainerPolicies(),
			[](const FBattleTrainerEncounterPolicy& L,
				const FBattleTrainerEncounterPolicy& R)
			{
				return L.TrainerId == R.TrainerId
					&& L.Side == R.Side
					&& L.Role == R.Role
					&& L.Controller == R.Controller
					&& L.SelectorProfileId == R.SelectorProfileId
					&& L.SelectorProfileTag == R.SelectorProfileTag
					&& L.bMayUseBag == R.bMayUseBag
					&& L.bMayUseRevive == R.bMayUseRevive
					&& L.bMayRun == R.bMayRun
					&& L.bMayCapture == R.bMayCapture
					&& L.bMayVoluntarilySwitch == R.bMayVoluntarilySwitch
					&& L.bPartnerOwnsSeparatePartyAndBag
						== R.bPartnerOwnsSeparatePartyAndBag;
			});
	}

bool AreCaptureWildFleePoliciesIdentical(
		const TConstArrayView<FBattleWildFleePolicyState> Left,
		const TConstArrayView<FBattleWildFleePolicyState> Right)
	{
		return AreOrderedCaptureValuesIdentical(
			Left,
			Right,
			[](const FBattleWildFleePolicyState& L,
				const FBattleWildFleePolicyState& R)
			{
				return L.SpeciesFormId == R.SpeciesFormId
					&& L.TriggerId == R.TriggerId
					&& L.EligibilityId == R.EligibilityId
					&& L.ProbabilityMode == R.ProbabilityMode
					&& L.Numerator == R.Numerator
					&& L.Denominator == R.Denominator;
			});
	}

bool AreCaptureStatRefreshesIdentical(
		const TConstArrayView<FBattleBetweenActionsStatRefresh> Left,
		const TConstArrayView<FBattleBetweenActionsStatRefresh> Right)
	{
		return AreOrderedCaptureValuesIdentical(
			Left,
			Right,
			[](const FBattleBetweenActionsStatRefresh& L,
				const FBattleBetweenActionsStatRefresh& R)
			{
				return L.StateVersion == R.StateVersion
					&& L.OpponentRemovalCheckpointEventOrdinal
						== R.OpponentRemovalCheckpointEventOrdinal
					&& L.BattlerId == R.BattlerId
					&& L.NewLevel == R.NewLevel
					&& AreCaptureStatsIdentical(L.NewStats, R.NewStats)
					&& L.NewCurrentHP == R.NewCurrentHP;
			});
	}

bool AreCaptureRejectionsIdentical(
		const FBattleRejection& Left,
		const FBattleRejection& Right)
	{
		return Left.Reason == Right.Reason
			&& Left.TrainerId == Right.TrainerId
			&& Left.BattlerId == Right.BattlerId
			&& Left.ActionId == Right.ActionId
			&& Left.MoveId == Right.MoveId
			&& Left.ItemId == Right.ItemId
			&& Left.PartySlotId == Right.PartySlotId
			&& Left.ActiveSlotId == Right.ActiveSlotId;
	}

bool AreCaptureResolutionsIdentical(
		const FBattleResolution& Left,
		const FBattleResolution& Right)
	{
		return Left.IsValid() == Right.IsValid()
			&& Left.WasAccepted() == Right.WasAccepted()
			&& Left.GetResolutionId() == Right.GetResolutionId()
			&& Left.GetBeforeStateVersion() == Right.GetBeforeStateVersion()
			&& Left.GetAfterStateVersion() == Right.GetAfterStateVersion()
			&& AreCaptureRejectionsIdentical(
				Left.GetRejection(),
				Right.GetRejection())
			&& AreOrderedCaptureValuesIdentical(
				Left.GetEvents(),
				Right.GetEvents(),
				[](const FBattleEvent& L, const FBattleEvent& R)
				{
					return AreEventsIdentical(L, R);
				});
	}

struct FCaptureCheckpointObservation
	{
		uint64 StateVersion = 0;
		uint64 NextResolutionId = 0;
		uint64 NextActionId = 0;
		uint64 NextEventOrdinal = 0;
		uint64 NextConditionCreationOrdinal = 0;
		uint64 NextTriggerToken = 0;
		FTurnId TurnId;
		EBattleEncounterKind EncounterKind = EBattleEncounterKind::Wild;
		EBattleFormat Format = EBattleFormat::Single;
		EBattlePhase Phase = EBattlePhase::Setup;
		EBattleOutcome Outcome = EBattleOutcome::InProgress;
		EBattleOutcomeCause OutcomeCause = EBattleOutcomeCause::None;
		uint32 EscapeAttemptCount = 0;
		int32 LockedActionIndex = INDEX_NONE;
		int32 CurrentDecisionOwnerIndex = INDEX_NONE;
		int32 CurrentDecisionActorOffset = 0;
		bool bHasCatalog = false;
		bool bLockedOrderReversesSpeed = false;
		bool bReinforcementSucceeded = false;
		bool bEndTurnTriggerPassComplete = false;
		FBattleCaptureCapacitySnapshot CaptureCapacity;
		FBattleCompiledEncounterPolicies Policies;
		TArray<FBattleTrainerState> Trainers;
		TArray<FBattleBattlerState> Battlers;
		TArray<FBattleActivePositionState> ActivePositions;
		TArray<FBattlePendingCaptureState> PendingCaptures;
		TArray<FBattleWildFleePolicyState> WildFleePolicies;
		TArray<FBattleLockedActionState> LockedActions;
		TOptional<FBattleDecisionRequest> PendingDecision;
		TArray<FBattleDecisionOwnerState> DecisionOwnerSequence;
		TArray<FBattleDecisionRequest> PendingDecisionRequests;
		TArray<FBattlePendingReplacementState> PendingReplacements;
		TArray<FBattleDecision> AcceptedSelections;
		TArray<FBattleHeldItemInstanceState> HeldItemLedgerStates;
		TArray<uint64> AvailableOpponentRemovalCheckpoints;
		TArray<FBattleDecision> SubmittedDecisions;
		TArray<FBattleBetweenActionsStatRefresh> SubmittedStatRefreshes;
		TArray<FBattleRandomDraw> RandomTrace;
		TArray<FBattleResolution> Resolutions;
		TArray<FBattleEvent> OrderedEvents;
		FBattleReplayRecord Replay;
	};

FCaptureCheckpointObservation ObserveCaptureCheckpoint(
		const FBattleEngine& Engine)
	{
		const FBattleEngineState& State =
			FBattleC09BWildFlowEngineFixture::GetState(Engine);
		FCaptureCheckpointObservation Observation;
		Observation.StateVersion = State.StateVersion;
		Observation.NextResolutionId = State.NextResolutionId;
		Observation.NextActionId = State.NextActionId;
		Observation.NextEventOrdinal = State.NextEventOrdinal;
		Observation.NextConditionCreationOrdinal = State.NextConditionCreationOrdinal;
		Observation.NextTriggerToken = State.NextTriggerReentrancyToken;
		Observation.TurnId = State.TurnId;
		Observation.EncounterKind = State.EncounterKind;
		Observation.Format = State.Format;
		Observation.Phase = State.Phase;
		Observation.Outcome = State.Outcome;
		Observation.OutcomeCause = State.OutcomeCause;
		Observation.EscapeAttemptCount = State.EscapeAttemptCount;
		Observation.LockedActionIndex = State.CurrentLockedActionIndex;
		Observation.CurrentDecisionOwnerIndex = State.CurrentDecisionOwnerIndex;
		Observation.CurrentDecisionActorOffset = State.CurrentDecisionActorOffset;
		Observation.bHasCatalog = State.bHasCatalog;
		Observation.bLockedOrderReversesSpeed = State.bLockedOrderReversesSpeed;
		Observation.bReinforcementSucceeded = State.bReinforcementSucceeded;
		Observation.bEndTurnTriggerPassComplete = State.bEndTurnTriggerPassComplete;
		Observation.CaptureCapacity = State.CaptureCapacity;
		Observation.Policies = State.CompiledEncounterPolicies;
		Observation.Trainers = State.Trainers;
		Observation.Battlers = State.Battlers;
		Observation.ActivePositions = State.ActivePositions;
		Observation.PendingCaptures = State.PendingCaptures;
		Observation.WildFleePolicies = State.WildFleePolicies;
		Observation.LockedActions = State.LockedActions;
		Observation.PendingDecision = State.PendingDecision;
		Observation.DecisionOwnerSequence = State.DecisionOwnerSequence;
		Observation.PendingDecisionRequests = State.PendingDecisionRequests;
		Observation.PendingReplacements = State.PendingReplacements;
		Observation.AcceptedSelections = State.AcceptedSelections;
		Observation.HeldItemLedgerStates.Append(State.HeldItemLedger.GetStates());
		Observation.AvailableOpponentRemovalCheckpoints =
			State.AvailableOpponentRemovalCheckpoints;
		Observation.SubmittedDecisions = State.SubmittedDecisions;
		Observation.SubmittedStatRefreshes = State.SubmittedStatRefreshes;
		Observation.RandomTrace.Append(State.Random->GetTrace());
		Observation.Resolutions = State.Resolutions;
		Observation.OrderedEvents = State.OrderedEvents;
		Observation.Replay = Engine.ExportReplayRecord();
		return Observation;
	}

bool AreCapturePreservedFactsIdentical(
		const FCaptureCheckpointObservation& Left,
		const FCaptureCheckpointObservation& Right)
	{
		return Left.NextActionId == Right.NextActionId
			&& Left.NextConditionCreationOrdinal == Right.NextConditionCreationOrdinal
			&& Left.NextTriggerToken == Right.NextTriggerToken
			&& Left.TurnId == Right.TurnId
			&& Left.EncounterKind == Right.EncounterKind
			&& Left.Format == Right.Format
			&& Left.Outcome == Right.Outcome
			&& Left.OutcomeCause == Right.OutcomeCause
			&& Left.EscapeAttemptCount == Right.EscapeAttemptCount
			&& Left.CurrentDecisionOwnerIndex == Right.CurrentDecisionOwnerIndex
			&& Left.CurrentDecisionActorOffset == Right.CurrentDecisionActorOffset
			&& Left.bHasCatalog == Right.bHasCatalog
			&& Left.bLockedOrderReversesSpeed == Right.bLockedOrderReversesSpeed
			&& Left.bReinforcementSucceeded == Right.bReinforcementSucceeded
			&& Left.bEndTurnTriggerPassComplete == Right.bEndTurnTriggerPassComplete
			&& Left.CaptureCapacity.PartySlotsRemaining
				== Right.CaptureCapacity.PartySlotsRemaining
			&& Left.CaptureCapacity.StorageSlotsRemaining
				== Right.CaptureCapacity.StorageSlotsRemaining
			&& AreCapturePoliciesIdentical(Left.Policies, Right.Policies)
			&& AreCaptureTrainersIdentical(Left.Trainers, Right.Trainers)
			&& AreCaptureBattlersIdentical(Left.Battlers, Right.Battlers)
			&& AreCaptureActivePositionsIdentical(
				Left.ActivePositions,
				Right.ActivePositions)
			&& Left.PendingCaptures == Right.PendingCaptures
			&& AreCaptureWildFleePoliciesIdentical(
				Left.WildFleePolicies,
				Right.WildFleePolicies)
			&& AreCaptureDecisionOwnerSequencesIdentical(
				Left.DecisionOwnerSequence,
				Right.DecisionOwnerSequence)
			&& AreCaptureDecisionArraysIdentical(
				Left.AcceptedSelections,
				Right.AcceptedSelections)
			&& Left.HeldItemLedgerStates == Right.HeldItemLedgerStates
			&& Left.AvailableOpponentRemovalCheckpoints
				== Right.AvailableOpponentRemovalCheckpoints
			&& AreCaptureDecisionArraysIdentical(
				Left.SubmittedDecisions,
				Right.SubmittedDecisions)
			&& AreCaptureStatRefreshesIdentical(
				Left.SubmittedStatRefreshes,
				Right.SubmittedStatRefreshes)
			&& Left.RandomTrace == Right.RandomTrace;
	}

bool AreCapturePendingFactsIdentical(
		const FCaptureCheckpointObservation& Left,
		const FCaptureCheckpointObservation& Right)
	{
		if (Left.PendingDecision.IsSet() != Right.PendingDecision.IsSet())
		{
			return false;
		}
		return (!Left.PendingDecision.IsSet()
				|| AreCaptureDecisionRequestsIdentical(
					Left.PendingDecision.GetValue(),
					Right.PendingDecision.GetValue()))
			&& AreCapturePendingRequestsIdentical(
				Left.PendingDecisionRequests,
				Right.PendingDecisionRequests)
			&& AreCaptureReplacementStatesIdentical(
				Left.PendingReplacements,
				Right.PendingReplacements);
	}

bool HasCaptureHistoryPrefix(
		const FCaptureCheckpointObservation& Before,
		const FCaptureCheckpointObservation& After)
	{
		if (After.Resolutions.Num() < Before.Resolutions.Num()
			|| After.OrderedEvents.Num() < Before.OrderedEvents.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < Before.Resolutions.Num(); ++Index)
		{
			if (!AreCaptureResolutionsIdentical(
					Before.Resolutions[Index],
					After.Resolutions[Index]))
			{
				return false;
			}
		}
		for (int32 Index = 0; Index < Before.OrderedEvents.Num(); ++Index)
		{
			if (!AreEventsIdentical(
					Before.OrderedEvents[Index],
					After.OrderedEvents[Index]))
			{
				return false;
			}
		}
		return true;
	}

bool AreCaptureReplayRecordsIdentical(
		const FBattleReplayRecord& Left,
		const FBattleReplayRecord& Right)
	{
		TArray<uint8> LeftBytes;
		TArray<uint8> RightBytes;
		FBattleRejection LeftRejection;
		FBattleRejection RightRejection;
		return Left.IsValid()
			&& Right.IsValid()
			&& FBattleReplaySerializer::TrySerializeCanonical(
				Left,
				LeftBytes,
				LeftRejection)
			&& FBattleReplaySerializer::TrySerializeCanonical(
				Right,
				RightBytes,
				RightRejection)
			&& LeftBytes == RightBytes;
	}

bool AreCaptureCheckpointObservationsIdentical(
		const FCaptureCheckpointObservation& Left,
		const FCaptureCheckpointObservation& Right)
	{
		return Left.StateVersion == Right.StateVersion
			&& Left.NextResolutionId == Right.NextResolutionId
			&& Left.NextEventOrdinal == Right.NextEventOrdinal
			&& Left.Phase == Right.Phase
			&& Left.LockedActionIndex == Right.LockedActionIndex
			&& AreCapturePreservedFactsIdentical(Left, Right)
			&& AreCapturePendingFactsIdentical(Left, Right)
			&& AreCaptureLockedActionsIdentical(
				Left.LockedActions,
				Right.LockedActions)
			&& AreOrderedCaptureValuesIdentical(
				Left.Resolutions,
				Right.Resolutions,
				[](const FBattleResolution& L, const FBattleResolution& R)
				{
					return AreCaptureResolutionsIdentical(L, R);
				})
			&& AreOrderedCaptureValuesIdentical(
				Left.OrderedEvents,
				Right.OrderedEvents,
				[](const FBattleEvent& L, const FBattleEvent& R)
				{
					return AreEventsIdentical(L, R);
				})
			&& AreCaptureReplayRecordsIdentical(Left.Replay, Right.Replay);
	}

bool VerifyCaptureReplay(
		FAutomationTestBase& Test,
		const FBattleEngine& Engine,
		const FCaptureCheckpointObservation& Before,
		const FBattleResolution& Returned,
		const FBattleSnapshot& ExpectedFinalSnapshot)
	{
		TArray<FBattleResolution> ExpectedResolutions = Before.Resolutions;
		ExpectedResolutions.Add(Returned);
		FBattleReplayRecord ExpectedReplay;
		const bool bExpectedCreated = FBattleReplayRecord::TryCreate(
			FBattleReplayRecord::CurrentSchemaVersion,
			Before.Replay.GetInputs(),
			ExpectedResolutions,
			Before.RandomTrace,
			ExpectedFinalSnapshot,
			ExpectedReplay);
		const FBattleReplayRecord ActualReplay = Engine.ExportReplayRecord();
		TArray<uint8> ExpectedBytes;
		TArray<uint8> ActualBytes;
		FBattleRejection ExpectedRejection;
		FBattleRejection ActualRejection;
		const bool bExpectedSerialized = bExpectedCreated
			&& FBattleReplaySerializer::TrySerializeCanonical(
				ExpectedReplay,
				ExpectedBytes,
				ExpectedRejection);
		const bool bActualSerialized = ActualReplay.IsValid()
			&& ActualReplay.GetSchemaVersion()
				== FBattleReplayRecord::CurrentSchemaVersion
			&& FBattleReplaySerializer::TrySerializeCanonical(
				ActualReplay,
				ActualBytes,
				ActualRejection);
		bool bValid = Test.TestTrue(
			TEXT("Capture checkpoint expected replay serializes canonically"),
			bExpectedSerialized);
		bValid &= Test.TestTrue(
			TEXT("Capture checkpoint actual replay remains schema-6 canonical"),
			bActualSerialized);
		bValid &= Test.TestTrue(
			TEXT("Capture checkpoint replay changes only by the exact returned resolution and expected final snapshot"),
			bExpectedSerialized && bActualSerialized && ExpectedBytes == ActualBytes);
		return bValid;
	}

bool VerifyAcceptedCaptureCancellationCheckpoint(
		FAutomationTestBase& Test,
		const FBattleEngine& Engine,
		const FCaptureCheckpointObservation& Before,
		const EBattlePhase ExpectedPhase,
		const TArray<EBattleEventType>& ExpectedEventOrder,
		const FBattleResolution& Returned)
	{
		const FCaptureCheckpointObservation After = ObserveCaptureCheckpoint(Engine);
		TArray<FBattleLockedActionState> ExpectedActions = Before.LockedActions;
		if (ExpectedActions.IsValidIndex(Before.LockedActionIndex))
		{
			ExpectedActions[Before.LockedActionIndex].bFinished = true;
		}
		bool bValid = true;
		bValid &= Test.TestTrue(
			TEXT("Stale Capture use is an accepted queue cancellation"),
			Returned.WasAccepted());
		bValid &= Test.TestTrue(
			TEXT("Accepted stale Capture cancellation has the exact event order"),
			HasExactEventOrder(Returned, ExpectedEventOrder));
		bValid &= Test.TestTrue(
			TEXT("Accepted stale Capture cancellation is appended exactly once"),
			IsReturnedResolutionAppendedExactlyOnce(Engine, Returned));
		bValid &= Test.TestEqual(
			TEXT("Accepted stale Capture cancellation advances state once"),
			After.StateVersion,
			Before.StateVersion + 1);
		bValid &= Test.TestEqual(
			TEXT("Accepted stale Capture cancellation consumes one resolution identity"),
			After.NextResolutionId,
			Before.NextResolutionId + 1);
		bValid &= Test.TestEqual(
			TEXT("Accepted stale Capture cancellation consumes only its staged event ordinals"),
			After.NextEventOrdinal,
			Before.NextEventOrdinal + static_cast<uint64>(ExpectedEventOrder.Num()));
		bValid &= Test.TestEqual(
			TEXT("Accepted stale Capture cancellation advances exactly one queue action"),
			After.LockedActionIndex,
			Before.LockedActionIndex + 1);
		bValid &= Test.TestEqual(
			TEXT("Accepted stale Capture cancellation reaches the expected boundary phase"),
			After.Phase,
			ExpectedPhase);
		bValid &= Test.TestTrue(
			TEXT("Accepted stale Capture cancellation changes only the current action finish bit"),
			AreCaptureLockedActionsIdentical(After.LockedActions, ExpectedActions));
		bValid &= Test.TestTrue(
			TEXT("Accepted stale Capture cancellation preserves all Capture, Trainer, battler, active-slot, outcome, RNG, and input-history facts"),
			AreCapturePreservedFactsIdentical(After, Before));
		bValid &= Test.TestEqual(
			TEXT("Accepted stale Capture cancellation appends one resolution"),
			After.Resolutions.Num(),
			Before.Resolutions.Num() + 1);
		bValid &= Test.TestEqual(
			TEXT("Accepted stale Capture cancellation appends its exact event count"),
			After.OrderedEvents.Num(),
			Before.OrderedEvents.Num() + ExpectedEventOrder.Num());
		bValid &= Test.TestTrue(
			TEXT("Accepted stale Capture cancellation preserves every prior resolution and event"),
			HasCaptureHistoryPrefix(Before, After));
		const FBattleSnapshot ExpectedFinalSnapshot = Engine.GetSnapshot();
		bValid &= VerifyCaptureReplay(
			Test,
			Engine,
			Before,
			Returned,
			ExpectedFinalSnapshot);
		return bValid;
	}

bool VerifyRejectedCaptureCancellationCheckpoint(
		FAutomationTestBase& Test,
		const FBattleEngine& Engine,
		const FCaptureCheckpointObservation& Before,
		const EBattleRejectionReason ExpectedReason,
		const FBattleResolution& Returned)
	{
		const FCaptureCheckpointObservation After = ObserveCaptureCheckpoint(Engine);
		bool bValid = true;
		bValid &= Test.TestFalse(
			TEXT("Fallible stale Capture cancellation is rejected"),
			Returned.WasAccepted());
		bValid &= Test.TestEqual(
			TEXT("Fallible stale Capture cancellation has the expected typed reason"),
			Returned.GetRejection().Reason,
			ExpectedReason);
		bValid &= Test.TestTrue(
			TEXT("Fallible stale Capture cancellation is appended exactly once"),
			IsReturnedResolutionAppendedExactlyOnce(Engine, Returned));
		bValid &= Test.TestEqual(
			TEXT("Fallible stale Capture cancellation preserves state version"),
			After.StateVersion,
			Before.StateVersion);
		bValid &= Test.TestEqual(
			TEXT("Fallible stale Capture cancellation consumes one invocation resolution identity"),
			After.NextResolutionId,
			Before.NextResolutionId + 1);
		bValid &= Test.TestEqual(
			TEXT("Fallible stale Capture cancellation consumes one rejection event ordinal"),
			After.NextEventOrdinal,
			Before.NextEventOrdinal + 1);
		bValid &= Test.TestEqual(
			TEXT("Fallible stale Capture cancellation preserves the queue cursor"),
			After.LockedActionIndex,
			Before.LockedActionIndex);
		bValid &= Test.TestEqual(
			TEXT("Fallible stale Capture cancellation preserves phase"),
			After.Phase,
			Before.Phase);
		bValid &= Test.TestTrue(
			TEXT("Fallible stale Capture cancellation preserves the complete locked-action array"),
			AreCaptureLockedActionsIdentical(After.LockedActions, Before.LockedActions));
		bValid &= Test.TestTrue(
			TEXT("Fallible stale Capture cancellation preserves complete pending decision and replacement state"),
			AreCapturePendingFactsIdentical(After, Before));
		bValid &= Test.TestTrue(
			TEXT("Fallible stale Capture cancellation preserves all Capture, Trainer, battler, active-slot, outcome, RNG, and input-history facts"),
			AreCapturePreservedFactsIdentical(After, Before));
		bValid &= Test.TestTrue(
			TEXT("Fallible stale Capture cancellation leaves the current action started and unfinished"),
			After.LockedActions.IsValidIndex(After.LockedActionIndex)
				&& After.LockedActions[After.LockedActionIndex].bStarted
				&& !After.LockedActions[After.LockedActionIndex].bFinished);
		bValid &= Test.TestTrue(
			TEXT("Fallible stale Capture cancellation publishes exactly ActionCanceled"),
			HasExactEventOrder(Returned, {EBattleEventType::ActionCanceled}));
		bValid &= Test.TestFalse(
			TEXT("Fallible stale Capture cancellation publishes no completion or replacement fact"),
			HasEvent(Returned, EBattleEventType::ActionCompleted)
				|| HasEvent(Returned, EBattleEventType::ReplacementRequired));
		bValid &= Test.TestFalse(
			TEXT("Fallible stale Capture cancellation publishes no Capture attempt fact"),
			HasEvent(Returned, EBattleEventType::CaptureAttempted));
		bValid &= Test.TestEqual(
			TEXT("Fallible stale Capture cancellation appends one rejection resolution"),
			After.Resolutions.Num(),
			Before.Resolutions.Num() + 1);
		bValid &= Test.TestEqual(
			TEXT("Fallible stale Capture cancellation appends one rejection event"),
			After.OrderedEvents.Num(),
			Before.OrderedEvents.Num() + 1);
		bValid &= Test.TestTrue(
			TEXT("Fallible stale Capture cancellation preserves every prior resolution and event"),
			HasCaptureHistoryPrefix(Before, After));
		bValid &= VerifyCaptureReplay(
			Test,
			Engine,
			Before,
			Returned,
			Before.Replay.GetFinalSnapshot());
		return bValid;
	}

FAtomicWildScenario MakeStaleCaptureCancellationScenario(
		const bool bReplacementReserve = false)
	{
		FAtomicWildScenario Scenario = MakeAtomicCaptureScenario();
		Scenario.WildFleeMode = EBattleWildFleeMode::Chance;
		Scenario.WildFleeNumerator = 1;
		Scenario.WildFleeDenominator = 2;
		Scenario.bVoluntarySwitchFlow = bReplacementReserve;
		return Scenario;
	}

FBattleTrainerEncounterPolicy* FindMutableCapturePolicy(
		FBattleEngineState& State,
		const FTrainerId TrainerId)
	{
		return const_cast<FBattleTrainerEncounterPolicy*>(
			State.CompiledEncounterPolicies.FindTrainerPolicy(TrainerId));
	}

enum class ECaptureStaleUseMutation : uint8
	{
		BagPolicyUnavailable,
		CapturePermissionUnavailable,
		ChangedClassification,
		InvalidClassification,
		InvalidTargetLevel,
		CapacityUnavailable,
		BagQuotaUnavailable,
		PokeBallUnavailable
	};

bool TryApplyCaptureStaleUseMutation(
		FBattleEngine& Engine,
		const ECaptureStaleUseMutation Mutation)
	{
		FBattleEngineState& State =
			FBattleC09BWildFlowEngineFixture::GetMutableState(Engine);
		FBattleTrainerState* PlayerTrainer = State.FindMutableTrainer(
			MakeNumericId<FTrainerId>(PlayerTrainerValue));
		FBattleBattlerState* Target = State.FindMutableBattler(
			MakeNumericId<FBattlerId>(OpponentLeftValue));
		FBattleTrainerEncounterPolicy* PlayerPolicy = FindMutableCapturePolicy(
			State,
			MakeNumericId<FTrainerId>(PlayerTrainerValue));
		if (PlayerTrainer == nullptr || Target == nullptr || PlayerPolicy == nullptr)
		{
			return false;
		}

		switch (Mutation)
		{
		case ECaptureStaleUseMutation::BagPolicyUnavailable:
			PlayerPolicy->bMayUseBag = false;
			return true;
		case ECaptureStaleUseMutation::CapturePermissionUnavailable:
			PlayerPolicy->bMayCapture = false;
			return true;
		case ECaptureStaleUseMutation::ChangedClassification:
			Target->CaptureClassification =
				EBattleCaptureSpeciesClassification::UltraBeast;
			return true;
		case ECaptureStaleUseMutation::InvalidClassification:
			Target->CaptureClassification = EBattleCaptureSpeciesClassification::Invalid;
			return true;
		case ECaptureStaleUseMutation::InvalidTargetLevel:
			Target->Level = 0;
			return true;
		case ECaptureStaleUseMutation::CapacityUnavailable:
			State.CaptureCapacity.PartySlotsRemaining = 0;
			State.CaptureCapacity.StorageSlotsRemaining = 0;
			return true;
		case ECaptureStaleUseMutation::BagQuotaUnavailable:
			PlayerTrainer->ActionAllowance.bBagActionAvailable = false;
			return true;
		case ECaptureStaleUseMutation::PokeBallUnavailable:
		{
			const FItemId PokeBallId = FBattleBagItemRules::GetPokeBallId();
			FBattleBagItemCount* PokeBall = PlayerTrainer->Bag.FindByPredicate(
				[PokeBallId](const FBattleBagItemCount& Candidate)
				{
					return Candidate.ItemId == PokeBallId;
				});
			if (PokeBall == nullptr)
			{
				return false;
			}
			PokeBall->Count = 0;
			return true;
		}
		default:
			return false;
		}
	}

bool TryPrepareCaptureReplacementBoundary(FBattleEngine& Engine)
	{
		FBattleEngineState& State =
			FBattleC09BWildFlowEngineFixture::GetMutableState(Engine);
		const FActiveSlotId PlayerLeft = MakeActiveSlotId(
			EBattleSide::Player,
			EBattlePosition::Left);
		FBattleBattlerState* Actor = State.FindMutableBattler(
			MakeNumericId<FBattlerId>(PlayerLeftValue));
		FBattleBattlerState* Target = State.FindMutableBattler(
			MakeNumericId<FBattlerId>(OpponentLeftValue));
		const FBattleBattlerState* Reserve = State.FindBattler(
			MakeNumericId<FBattlerId>(PlayerReserveValue));
		FBattleActivePositionState* Active = State.FindMutableActivePosition(PlayerLeft);
		if (Actor == nullptr
			|| Target == nullptr
			|| Reserve == nullptr
			|| Active == nullptr
			|| State.CurrentLockedActionIndex != State.LockedActions.Num() - 1
			|| !State.LockedActions.IsValidIndex(State.CurrentLockedActionIndex)
			|| !State.LockedActions[State.CurrentLockedActionIndex].bStarted
			|| State.LockedActions[State.CurrentLockedActionIndex].bFinished)
		{
			return false;
		}

		Actor->CurrentHP = 0;
		Actor->bFainted = true;
		Actor->bCaptured = false;
		Actor->bRemoved = true;
		Actor->bFaintTransitionPending = false;
		Target->CaptureClassification =
			EBattleCaptureSpeciesClassification::UltraBeast;
		Active->TrainerId = FTrainerId();
		Active->BattlerId = FBattlerId();

		FBattleQueueBoundaryPlan BoundaryPlan;
		return Reserve->CurrentHP > 0
			&& !Reserve->bFainted
			&& !Reserve->bCaptured
			&& !Reserve->bRemoved
			&& FBattleFaintOutcomeResolver::ResolveQueueBoundary(
				State.Phase,
				State.Outcome,
				State.CurrentLockedActionIndex + 1,
				State.LockedActions.Num(),
				State.Setup.GetStartingActive(),
				State.Battlers,
				State.ActivePositions,
				BoundaryPlan)
			&& BoundaryPlan.PhaseAfter == EBattlePhase::MandatoryReplacement
			&& BoundaryPlan.Requirements.Num() == 1
			&& BoundaryPlan.Requirements[0].Target.TrainerId
				== MakeNumericId<FTrainerId>(PlayerTrainerValue)
			&& BoundaryPlan.Requirements[0].Target.ActiveSlotId == PlayerLeft;
	}

bool TryBuildPlayerReplacementCandidateSpec(
		const FBattleEngine& Engine,
		FBattleSwitchLegalitySpec& OutSpec)
	{
		OutSpec = FBattleSwitchLegalitySpec();
		const FBattleEngineState& State =
			FBattleC09BWildFlowEngineFixture::GetState(Engine);
		const FBattleTrainerState* PlayerTrainer = State.FindTrainer(
			MakeNumericId<FTrainerId>(PlayerTrainerValue));
		if (PlayerTrainer == nullptr
			|| PlayerTrainer->PartySlots.Num() != FPartySlotId::PartySize)
		{
			return false;
		}

		OutSpec.Kind = EBattleSwitchKind::Replacement;
		OutSpec.ActingTrainerId = PlayerTrainer->TrainerId;
		OutSpec.ActiveSlotId = MakeActiveSlotId(
			EBattleSide::Player,
			EBattlePosition::Left);
		for (const FBattlePartySlotState& PartySlot : PlayerTrainer->PartySlots)
		{
			FBattleSwitchCandidateFacts Candidate;
			Candidate.PartySlotId = PartySlot.PartySlotId;
			Candidate.bOccupied = PartySlot.BattlerId.IsValid();
			if (Candidate.bOccupied)
			{
				const FBattleBattlerState* Battler = State.FindBattler(PartySlot.BattlerId);
				if (Battler == nullptr)
				{
					return false;
				}
				Candidate.TrainerId = Battler->TrainerId;
				Candidate.BattlerId = Battler->BattlerId;
				Candidate.bAlreadyActive = State.ActivePositions.ContainsByPredicate(
					[Battler](const FBattleActivePositionState& Position)
					{
						return Position.BattlerId == Battler->BattlerId;
					});
				Candidate.bFainted = Battler->CurrentHP <= 0 || Battler->bFainted;
				Candidate.bEgg = Battler->bEgg;
				Candidate.bCaptured = Battler->bCaptured;
				Candidate.bRemoved = Battler->bRemoved;
			}
			OutSpec.Candidates.Add(MoveTemp(Candidate));
		}
		return true;
	}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023D3CaptureZeroIndicatorTest,
	"PokemonSolarus.Battle.ADR0002.3D3.Capture.Preparation.ZeroIndicatorBeforeTransaction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023D3CaptureZeroIndicatorTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FAtomicWildScenario Scenario = MakeAtomicCaptureScenario();
	Scenario.CatchRate = 1;
	Scenario.CaptureProgression.CaptureCoefficientQ12 = 1;
	TUniquePtr<FBattleEngine> Engine;
	FFaultBattleRandom* Random = nullptr;
	if (!TestTrue(TEXT("Zero-indicator engine is created"),
		TryMakeFaultEngine(
			Scenario,
			{},
			EFaultRandomMode::Commit,
			Engine,
			Random))
		|| !TestTrue(TEXT("Zero-indicator Capture turn locks"),
			LockTurn(*Engine, PlayerLeftValue, EBattleActionKind::Bag))
		|| !TestTrue(TEXT("Zero-indicator Capture starts"),
			BeginExpectedWildAction(*Engine, PlayerLeftValue, EBattleActionKind::Bag)))
	{
		return false;
	}

	check(Random != nullptr);
	const FBattlerId TargetId = MakeNumericId<FBattlerId>(OpponentLeftValue);
	const FCheckpointObservation Before = ObserveCheckpoint(*Engine, TargetId);
	const FBattleResolution Rejected = Engine->ExecuteCurrentBagItem();
	VerifyRejectedCheckpoint(
		*this,
		*Engine,
		TargetId,
		Before,
		Before.StateVersion,
		EBattleRejectionReason::CheckpointPreparationFailed,
		Rejected);
	TestEqual(TEXT("Zero indicator creates no RNG transaction"),
		Random->GetCounters().TransactionCreateAttempts, 0);
	TestEqual(TEXT("Zero indicator attempts no RNG draw"),
		Random->GetCounters().DrawAttempts, 0);
	TestEqual(TEXT("Zero indicator attempts no RNG commit"),
		Random->GetCounters().CommitAttempts, 0);
	TestFalse(TEXT("Zero-indicator rejection publishes no item success fact"),
		HasEvent(Rejected, EBattleEventType::ItemUsed));
	TestFalse(TEXT("Zero-indicator rejection publishes no CaptureAttempted fact"),
		HasEvent(Rejected, EBattleEventType::CaptureAttempted));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023D3CaptureRandomRollbackTest,
	"PokemonSolarus.Battle.ADR0002.3D3.Capture.Failure.RandomCreationCriticalShakeRollback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023D3CaptureRandomRollbackTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FBattlerId TargetId = MakeNumericId<FBattlerId>(OpponentLeftValue);

	FAtomicWildScenario CreateScenario = MakeAtomicCaptureScenario();
	TUniquePtr<FBattleEngine> CreateEngine;
	FFaultBattleRandom* CreateRandom = nullptr;
	if (!TestTrue(TEXT("Capture transaction-create failure engine is created"),
		TryMakeFaultEngine(
			CreateScenario,
			{0, 0, 0, 0},
			EFaultRandomMode::CreateTransaction,
			CreateEngine,
			CreateRandom))
		|| !TestTrue(TEXT("Capture transaction-create failure turn locks"),
			LockTurn(*CreateEngine, PlayerLeftValue, EBattleActionKind::Bag))
		|| !TestTrue(TEXT("Capture transaction-create failure starts"),
			BeginExpectedWildAction(
				*CreateEngine,
				PlayerLeftValue,
				EBattleActionKind::Bag)))
	{
		return false;
	}
	check(CreateRandom != nullptr);
	const FCheckpointObservation CreateBefore = ObserveCheckpoint(*CreateEngine, TargetId);
	const FBattleResolution CreateRejected = CreateEngine->ExecuteCurrentBagItem();
	VerifyRejectedCheckpoint(
		*this,
		*CreateEngine,
		TargetId,
		CreateBefore,
		CreateBefore.StateVersion,
		EBattleRejectionReason::CheckpointRandomStageFailed,
		CreateRejected);
	TestEqual(TEXT("Capture transaction creation is attempted once"),
		CreateRandom->GetCounters().TransactionCreateAttempts, 1);
	TestEqual(TEXT("Failed transaction creation performs no draw"),
		CreateRandom->GetCounters().DrawAttempts, 0);

	FAtomicWildScenario DrawScenario = MakeAtomicCaptureScenario();
	DrawScenario.CatchRate = 120;
	DrawScenario.TargetCurrentHP = 10;
	DrawScenario.CaptureProgression.CaughtSpeciesCount = 451;
	DrawScenario.CaptureProgression.bCriticalCaptureEnabled = true;
	DrawScenario.CaptureProgression.bCatchingCharm = true;
	DrawScenario.CaptureProgression.bUseCaughtCountHPComponentModifier = true;
	TUniquePtr<FBattleEngine> DrawEngine;
	FFaultBattleRandom* DrawRandom = nullptr;
	if (!TestTrue(TEXT("Critical/shake rollback engine is created"),
		TryMakeFaultEngine(
			DrawScenario,
			{255, 0, 0, 0},
			EFaultRandomMode::Draw,
			DrawEngine,
			DrawRandom,
			2))
		|| !TestTrue(TEXT("Critical/shake rollback turn locks"),
			LockTurn(*DrawEngine, PlayerLeftValue, EBattleActionKind::Bag))
		|| !TestTrue(TEXT("Critical/shake rollback Capture starts"),
			BeginExpectedWildAction(
				*DrawEngine,
				PlayerLeftValue,
				EBattleActionKind::Bag)))
	{
		return false;
	}
	check(DrawRandom != nullptr);
	const FCheckpointObservation DrawBefore = ObserveCheckpoint(*DrawEngine, TargetId);
	const FBattleResolution DrawRejected = DrawEngine->ExecuteCurrentBagItem();
	VerifyRejectedCheckpoint(
		*this,
		*DrawEngine,
		TargetId,
		DrawBefore,
		DrawBefore.StateVersion,
		EBattleRejectionReason::CheckpointRandomStageFailed,
		DrawRejected);
	TestEqual(TEXT("Rollback staged one critical and one shake draw"),
		DrawRandom->GetCounters().SuccessfulDraws, 2);
	TestEqual(TEXT("A later shake failure is the third draw attempt"),
		DrawRandom->GetCounters().DrawAttempts, 3);
	TestEqual(TEXT("Random-stage rollback never attempts parent commit"),
		DrawRandom->GetCounters().CommitAttempts, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023D3CaptureIdentityCommitFailureTest,
	"PokemonSolarus.Battle.ADR0002.3D3.Capture.Failure.StaleIdentityAndRandomCommit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023D3CaptureIdentityCommitFailureTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FBattlerId TargetId = MakeNumericId<FBattlerId>(OpponentLeftValue);
	FAtomicWildScenario Scenario = MakeAtomicCaptureScenario();

	TUniquePtr<FBattleEngine> StaleEngine;
	FFaultBattleRandom* StaleRandom = nullptr;
	if (!TestTrue(TEXT("Stale Capture engine is created"),
		TryMakeFaultEngine(
			Scenario,
			{0, 0, 0, 0},
			EFaultRandomMode::StaleAfterDraw,
			StaleEngine,
			StaleRandom))
		|| !TestTrue(TEXT("Stale Capture turn locks"),
			LockTurn(*StaleEngine, PlayerLeftValue, EBattleActionKind::Bag))
		|| !TestTrue(TEXT("Stale Capture starts"),
			BeginExpectedWildAction(
				*StaleEngine,
				PlayerLeftValue,
				EBattleActionKind::Bag)))
	{
		return false;
	}
	check(StaleRandom != nullptr);
	StaleRandom->SetAfterDraw([EnginePtr = StaleEngine.Get()]()
	{
		FBattleC09BWildFlowEngineFixture::AdvanceStateVersion(*EnginePtr);
	});
	const FCheckpointObservation StaleBefore = ObserveCheckpoint(*StaleEngine, TargetId);
	const FBattleResolution StaleRejected = StaleEngine->ExecuteCurrentBagItem();
	VerifyRejectedCheckpoint(
		*this,
		*StaleEngine,
		TargetId,
		StaleBefore,
		StaleBefore.StateVersion + 1,
		EBattleRejectionReason::StaleCheckpointIdentity,
		StaleRejected);
	TestTrue(TEXT("Stale identity is injected after staged Capture draws"),
		StaleRandom->GetCounters().SuccessfulDraws > 0);
	TestEqual(TEXT("Stale identity prevents parent RNG commit"),
		StaleRandom->GetCounters().CommitAttempts, 0);

	TUniquePtr<FBattleEngine> CommitEngine;
	FFaultBattleRandom* CommitRandom = nullptr;
	if (!TestTrue(TEXT("Capture commit-failure engine is created"),
		TryMakeFaultEngine(
			Scenario,
			{0, 0, 0, 0},
			EFaultRandomMode::Commit,
			CommitEngine,
			CommitRandom))
		|| !TestTrue(TEXT("Capture commit-failure turn locks"),
			LockTurn(*CommitEngine, PlayerLeftValue, EBattleActionKind::Bag))
		|| !TestTrue(TEXT("Capture commit-failure starts"),
			BeginExpectedWildAction(
				*CommitEngine,
				PlayerLeftValue,
				EBattleActionKind::Bag)))
	{
		return false;
	}
	check(CommitRandom != nullptr);
	const FCheckpointObservation CommitBefore = ObserveCheckpoint(*CommitEngine, TargetId);
	const FBattleResolution CommitRejected = CommitEngine->ExecuteCurrentBagItem();
	VerifyRejectedCheckpoint(
		*this,
		*CommitEngine,
		TargetId,
		CommitBefore,
		CommitBefore.StateVersion,
		EBattleRejectionReason::RandomTransactionCommitFailed,
		CommitRejected);
	TestEqual(TEXT("Capture RNG commit is attempted exactly once"),
		CommitRandom->GetCounters().CommitAttempts, 1);
	TestTrue(TEXT("Commit failure occurs after all four staged shakes"),
		CommitRandom->GetCounters().SuccessfulDraws == 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023D3CaptureStaleExecutionTest,
	"PokemonSolarus.Battle.ADR0002.3D3.Capture.Execution.StalePreUseCancellation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023D3CaptureStaleExecutionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FAtomicWildScenario Scenario = MakeAtomicCaptureScenario();
	Scenario.CaptureProgression.bMustCapture = true;
	TUniquePtr<FBattleEngine> Engine;
	if (!TestTrue(TEXT("Pre-use stale Capture engine is created"),
		TryMakeSequenceEngine(Scenario, {}, Engine))
		|| !TestTrue(TEXT("Pre-use stale Capture turn locks"),
			LockTurn(*Engine, PlayerLeftValue, EBattleActionKind::Bag))
		|| !TestTrue(TEXT("Pre-use stale Capture starts"),
			BeginExpectedWildAction(*Engine, PlayerLeftValue, EBattleActionKind::Bag)))
	{
		return false;
	}

	FBattleEngineState& MutableState =
		FBattleC09BWildFlowEngineFixture::GetMutableState(*Engine);
	FBattleBattlerState* Target = MutableState.FindMutableBattler(
		MakeNumericId<FBattlerId>(OpponentLeftValue));
	check(Target != nullptr);
	Target->CaptureClassification = EBattleCaptureSpeciesClassification::UltraBeast;
	const FCheckpointObservation Before = ObserveCheckpoint(*Engine, Target->BattlerId);
	const FBattleResolution Canceled = Engine->ExecuteCurrentBagItem();
	const FBattleEngineState& State =
		FBattleC09BWildFlowEngineFixture::GetState(*Engine);
	const FBattleTrainerState* PlayerTrainer = State.FindTrainer(
		MakeNumericId<FTrainerId>(PlayerTrainerValue));
	TestTrue(TEXT("A stale pre-use Capture is an accepted queue cancellation"),
		Canceled.WasAccepted());
	TestTrue(TEXT("Stale pre-use Capture preserves exact event order"),
		HasExactEventOrder(Canceled, {
			EBattleEventType::ActionCanceled,
			EBattleEventType::ActionCompleted}));
	TestEqual(TEXT("Stale pre-use Capture preserves Poke Balls"),
		PlayerTrainer != nullptr ? PlayerTrainer->Bag[0].Count : INDEX_NONE,
		Before.PokeBallCount);
	TestEqual(TEXT("Stale pre-use Capture preserves Bag quota"),
		PlayerTrainer != nullptr
			? PlayerTrainer->ActionAllowance.bBagActionAvailable
			: false,
		Before.bBagActionAvailable);
	TestEqual(TEXT("Stale pre-use Capture consumes no gameplay RNG"),
		State.Random->GetTrace().Num(), Before.RandomTraceCount);
	TestEqual(TEXT("Stale pre-use Capture creates no pending record"),
		State.PendingCaptures.Num(), Before.PendingCaptureCount);
	TestTrue(TEXT("Stale pre-use Capture leaves the target active"),
		FBattleC09BWildFlowEngineFixture::IsActive(*Engine, Target->BattlerId));
	TestEqual(TEXT("Stale pre-use cancellation advances only its action cursor"),
		State.CurrentLockedActionIndex, Before.LockedActionIndex + 1);
	TestTrue(TEXT("Stale pre-use resolution returns the exact single append"),
		IsReturnedResolutionAppendedExactlyOnce(*Engine, Canceled));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023D3CaptureStaleUseFamilyTest,
	"PokemonSolarus.Battle.ADR0002.3D3.Capture.Cancellation.CompleteStaleUseFamily",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023D3CaptureStaleUseFamilyTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	struct FCaptureStaleUseCase
	{
		const TCHAR* Name = TEXT("");
		ECaptureStaleUseMutation Mutation =
			ECaptureStaleUseMutation::BagPolicyUnavailable;
	};
	const TArray<FCaptureStaleUseCase> Cases = {
		{TEXT("BagPolicyUnavailable"),
			ECaptureStaleUseMutation::BagPolicyUnavailable},
		{TEXT("CapturePermissionUnavailable"),
			ECaptureStaleUseMutation::CapturePermissionUnavailable},
		{TEXT("ChangedClassification"),
			ECaptureStaleUseMutation::ChangedClassification},
		{TEXT("InvalidClassification"),
			ECaptureStaleUseMutation::InvalidClassification},
		{TEXT("InvalidTargetLevel"),
			ECaptureStaleUseMutation::InvalidTargetLevel},
		{TEXT("CapacityUnavailable"),
			ECaptureStaleUseMutation::CapacityUnavailable},
		{TEXT("BagQuotaUnavailable"),
			ECaptureStaleUseMutation::BagQuotaUnavailable},
		{TEXT("PokeBallUnavailable"),
			ECaptureStaleUseMutation::PokeBallUnavailable}
	};

	bool bValid = true;
	for (const FCaptureStaleUseCase& Case : Cases)
	{
		TUniquePtr<FBattleEngine> Engine;
		if (!TestTrue(
				FString::Printf(TEXT("%s Capture engine is created"), Case.Name),
				TryMakeSequenceEngine(
					MakeStaleCaptureCancellationScenario(),
					{1},
					Engine))
			|| !TestTrue(
				FString::Printf(TEXT("%s Capture is the last started action"), Case.Name),
				PrepareCaptureAsLastActionAfterFailedWildFlee(*Engine))
			|| !TestTrue(
				FString::Printf(TEXT("%s stale Capture mutation is applied"), Case.Name),
				TryApplyCaptureStaleUseMutation(*Engine, Case.Mutation)))
		{
			return false;
		}

		const FCaptureCheckpointObservation Before =
			ObserveCaptureCheckpoint(*Engine);
		const FBattleResolution Canceled = Engine->ExecuteCurrentBagItem();
		bool bCase = TestTrue(
			FString::Printf(TEXT("%s starts with one committed WildFlee draw"), Case.Name),
			Before.RandomTrace.Num() == 1);
		bCase &= VerifyAcceptedCaptureCancellationCheckpoint(
			*this,
			*Engine,
			Before,
			EBattlePhase::EndOfTurn,
			{
				EBattleEventType::ActionCanceled,
				EBattleEventType::ActionCompleted
			},
			Canceled);
		bCase &= TestTrue(
			FString::Printf(TEXT("%s leaves all pending state empty and unchanged"), Case.Name),
			AreCapturePendingFactsIdentical(
				ObserveCaptureCheckpoint(*Engine),
				Before));
		bValid &= bCase;
	}
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023D3CaptureMandatoryReplacementTest,
	"PokemonSolarus.Battle.ADR0002.3D3.Capture.Cancellation.MandatoryReplacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023D3CaptureMandatoryReplacementTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	TUniquePtr<FBattleEngine> Engine;
	if (!TestTrue(TEXT("Mandatory-replacement Capture engine is created"),
			TryMakeSequenceEngine(
				MakeStaleCaptureCancellationScenario(true),
				{1},
				Engine))
		|| !TestTrue(TEXT("Mandatory-replacement Capture is the last started action"),
			PrepareCaptureAsLastActionAfterFailedWildFlee(*Engine))
		|| !TestTrue(TEXT("Capture target becomes unavailable with one living reserve"),
			TryPrepareCaptureReplacementBoundary(*Engine)))
	{
		return false;
	}

	const FActiveSlotId PlayerLeft = MakeActiveSlotId(
		EBattleSide::Player,
		EBattlePosition::Left);
	const FCaptureCheckpointObservation Before = ObserveCaptureCheckpoint(*Engine);
	const FBattleResolution Canceled = Engine->ExecuteCurrentBagItem();
	bool bValid = VerifyAcceptedCaptureCancellationCheckpoint(
		*this,
		*Engine,
		Before,
		EBattlePhase::MandatoryReplacement,
		{
			EBattleEventType::ActionCanceled,
			EBattleEventType::ActionCompleted,
			EBattleEventType::ReplacementRequired
		},
		Canceled);

	const FCaptureCheckpointObservation After = ObserveCaptureCheckpoint(*Engine);
	bValid &= TestTrue(
		TEXT("Mandatory replacement freezes the exact player-left requirement"),
		After.PendingReplacements.Num() == 1
			&& After.PendingReplacements[0].TrainerId
				== MakeNumericId<FTrainerId>(PlayerTrainerValue)
			&& After.PendingReplacements[0].ActiveSlotId == PlayerLeft);
	bValid &= TestTrue(
		TEXT("Mandatory replacement publishes one exact state-versioned request"),
		After.PendingDecisionRequests.Num() == 1
			&& After.PendingDecisionRequests[0].IsValid()
			&& After.PendingDecisionRequests[0].GetStateVersion()
				== Before.StateVersion + 1
			&& After.PendingDecisionRequests[0].GetStateVersion()
				== After.StateVersion
			&& After.PendingDecisionRequests[0].GetRequestKind()
				== EBattleDecisionRequestKind::MandatoryReplacement
			&& After.PendingDecisionRequests[0].GetDecisionOwnerTrainerId()
				== MakeNumericId<FTrainerId>(PlayerTrainerValue)
			&& !After.PendingDecisionRequests[0].GetActingBattlerId().IsValid()
			&& After.PendingDecisionRequests[0].GetActingSlotId() == PlayerLeft);
	if (After.PendingDecisionRequests.Num() == 1)
	{
		const FBattleDecisionRequest& Request = After.PendingDecisionRequests[0];
		bValid &= TestTrue(
			TEXT("Mandatory replacement request has the exact legal action, slot, and reserve"),
			Request.GetLegalActionKinds().Num() == 1
				&& Request.GetLegalActionKinds()[0] == EBattleActionKind::Replacement
				&& Request.GetLegalActiveTargets().Num() == 1
				&& Request.GetLegalActiveTargets()[0] == PlayerLeft
				&& Request.GetLegalSwitchPartySlots().Num() == 1
				&& Request.GetLegalSwitchPartySlots()[0] == MakePartySlotId(1)
				&& Request.GetLegalMoveIds().IsEmpty()
				&& Request.GetAutomaticallyTargetedMoveIds().IsEmpty()
				&& Request.GetLegalItemIds().IsEmpty()
				&& Request.GetLegalPartyTargets().IsEmpty()
				&& Request.GetLegalMoveTargets().IsEmpty()
				&& Request.GetLegalItemPartyTargets().IsEmpty()
				&& Request.GetLegalItemActiveTargets().IsEmpty());
		const TConstArrayView<FBattleUnavailableDecisionOption> Unavailable =
			Request.GetUnavailableOptions();
		bool bExactUnavailable = Unavailable.Num() == FPartySlotId::PartySize - 1;
		int32 UnavailableIndex = 0;
		for (int32 PartyIndex = 0;
			bExactUnavailable && PartyIndex < FPartySlotId::PartySize;
			++PartyIndex)
		{
			if (PartyIndex == 1)
			{
				continue;
			}
			const EBattleOptionUnavailableReason ExpectedReason = PartyIndex == 0
				? EBattleOptionUnavailableReason::Removed
				: EBattleOptionUnavailableReason::EmptyPartySlot;
			bExactUnavailable = Unavailable.IsValidIndex(UnavailableIndex)
				&& Unavailable[UnavailableIndex].Kind
					== EBattleDecisionOptionKind::SwitchPartySlot
				&& Unavailable[UnavailableIndex].Reason == ExpectedReason
				&& Unavailable[UnavailableIndex].PartySlotId
					== MakePartySlotId(PartyIndex);
			++UnavailableIndex;
		}
		bValid &= TestTrue(
			TEXT("Mandatory replacement request reports the exact removed and empty party slots"),
			bExactUnavailable);
	}
	bValid &= TestTrue(
		TEXT("PendingDecision is the exact first mandatory-replacement request"),
		After.PendingDecision.IsSet()
			&& After.PendingDecisionRequests.Num() == 1
			&& AreCaptureDecisionRequestsIdentical(
				After.PendingDecision.GetValue(),
				After.PendingDecisionRequests[0]));
	bValid &= TestTrue(
		TEXT("ReplacementRequired targets only the exact empty player-left slot"),
		Canceled.GetEvents().Num() == 3
			&& Canceled.GetEvents()[2].GetTargets().Num() == 1
			&& Canceled.GetEvents()[2].GetTargets()[0].TrainerId
				== MakeNumericId<FTrainerId>(PlayerTrainerValue)
			&& !Canceled.GetEvents()[2].GetTargets()[0].BattlerId.IsValid()
			&& Canceled.GetEvents()[2].GetTargets()[0].ActiveSlotId == PlayerLeft);
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023D3CaptureReplacementPreparationFailureTest,
	"PokemonSolarus.Battle.ADR0002.3D3.Capture.Cancellation.ReplacementPreparationFailures",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023D3CaptureReplacementPreparationFailureTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	bool bValid = true;

	TUniquePtr<FBattleEngine> RequestEngine;
	if (!TestTrue(TEXT("Replacement-request failure Capture engine is created"),
			TryMakeSequenceEngine(
				MakeStaleCaptureCancellationScenario(true),
				{1},
				RequestEngine))
		|| !TestTrue(TEXT("Replacement-request failure Capture is the last started action"),
			PrepareCaptureAsLastActionAfterFailedWildFlee(*RequestEngine))
		|| !TestTrue(TEXT("Replacement-request failure has one exact boundary requirement"),
			TryPrepareCaptureReplacementBoundary(*RequestEngine)))
	{
		return false;
	}

	const FActiveSlotId PlayerLeft = MakeActiveSlotId(
		EBattleSide::Player,
		EBattlePosition::Left);
	const FCaptureCheckpointObservation BeforeRequestProbe =
		ObserveCaptureCheckpoint(*RequestEngine);
	FBattleDecisionRequestSpec RequestSpec;
	RequestSpec.StateVersion = BeforeRequestProbe.StateVersion + 1;
	RequestSpec.RequestKind = EBattleDecisionRequestKind::MandatoryReplacement;
	RequestSpec.DecisionOwnerTrainerId = MakeNumericId<FTrainerId>(PlayerTrainerValue);
	RequestSpec.ActingSlotId = PlayerLeft;
	RequestSpec.LegalActionKinds.Add(EBattleActionKind::Replacement);
	RequestSpec.LegalSwitchPartySlots.Add(MakePartySlotId(1));
	RequestSpec.LegalActiveTargets.Add(PlayerLeft);
	FBattleDecisionRequest BaselineRequest;
	FBattleRejection BaselineRequestRejection;
	bValid &= TestTrue(
		TEXT("Representable mandatory-replacement request constructs canonically"),
		FBattleDecisionRequest::TryCreate(
			RequestSpec,
			BaselineRequest,
			BaselineRequestRejection));
	RequestSpec.StateVersion = 0;
	FBattleDecisionRequest FailedRequest = BaselineRequest;
	FBattleRejection FailedRequestRejection;
	bValid &= TestFalse(
		TEXT("Unrepresentable replacement request state version fails construction"),
		FBattleDecisionRequest::TryCreate(
			RequestSpec,
			FailedRequest,
			FailedRequestRejection));
	bValid &= TestTrue(
		TEXT("Failed replacement-request construction resets its output and reports InvalidDecision"),
		!FailedRequest.IsValid()
			&& FailedRequestRejection.Reason == EBattleRejectionReason::InvalidDecision);
	bValid &= TestTrue(
		TEXT("Replacement-request construction probes preserve the complete engine checkpoint"),
		AreCaptureCheckpointObservationsIdentical(
			ObserveCaptureCheckpoint(*RequestEngine),
			BeforeRequestProbe));

	FBattleEngineState& RequestState =
		FBattleC09BWildFlowEngineFixture::GetMutableState(*RequestEngine);
	RequestState.StateVersion = TNumericLimits<uint64>::Max();
	const FCaptureCheckpointObservation BeforeRequestFailure =
		ObserveCaptureCheckpoint(*RequestEngine);
	const FBattleResolution RequestRejected = RequestEngine->ExecuteCurrentBagItem();
	bValid &= VerifyRejectedCaptureCancellationCheckpoint(
		*this,
		*RequestEngine,
		BeforeRequestFailure,
		EBattleRejectionReason::CheckpointPreparationFailed,
		RequestRejected);
	bValid &= TestTrue(
		TEXT("Unrepresentable next request version preserves the prior non-empty RNG trace exactly"),
		BeforeRequestFailure.RandomTrace.Num() == 1
			&& RequestEngine->ExportRandomTrace() == BeforeRequestFailure.RandomTrace);

	TUniquePtr<FBattleEngine> CandidateEngine;
	if (!TestTrue(TEXT("Bounded-candidate Capture engine is created"),
			TryMakeSequenceEngine(
				MakeStaleCaptureCancellationScenario(true),
				{1},
				CandidateEngine))
		|| !TestTrue(TEXT("Bounded-candidate Capture is the last started action"),
			PrepareCaptureAsLastActionAfterFailedWildFlee(*CandidateEngine))
		|| !TestTrue(TEXT("Bounded-candidate Capture has one exact boundary requirement"),
			TryPrepareCaptureReplacementBoundary(*CandidateEngine)))
	{
		return false;
	}

	const FCaptureCheckpointObservation BeforeCandidateProbe =
		ObserveCaptureCheckpoint(*CandidateEngine);
	FBattleSwitchLegalitySpec CandidateSpec;
	if (!TestTrue(
			TEXT("Exact live replacement candidate facts are copied without mutation"),
			TryBuildPlayerReplacementCandidateSpec(*CandidateEngine, CandidateSpec)))
	{
		return false;
	}
	FBattleSwitchLegalityResult BaselineLegality;
	bValid &= TestTrue(
		TEXT("The exact six-candidate replacement baseline validates"),
		FBattleSwitchResolver::TryBuildLegality(CandidateSpec, BaselineLegality)
			&& BaselineLegality.IsValid()
			&& BaselineLegality.GetLegalPartySlots().Num() == 1
			&& BaselineLegality.GetLegalPartySlots()[0] == MakePartySlotId(1));
	const FBattleSwitchCandidateFacts SeventhCandidate = CandidateSpec.Candidates[0];
	CandidateSpec.Candidates.Add(SeventhCandidate);
	FBattleSwitchLegalityResult OverflowLegality = BaselineLegality;
	bValid &= TestFalse(
		TEXT("The production replacement candidate validator rejects a seventh candidate"),
		FBattleSwitchResolver::TryBuildLegality(CandidateSpec, OverflowLegality));
	bValid &= TestTrue(
		TEXT("Bounded candidate-validation failure resets its result"),
		!OverflowLegality.IsValid()
			&& OverflowLegality.GetCandidates().IsEmpty()
			&& OverflowLegality.GetLegalPartySlots().IsEmpty());
	EBattleStateValidationError StateError = EBattleStateValidationError::None;
	bValid &= TestTrue(
		TEXT("Bounded candidate proof leaves the live engine invariant-valid"),
		FBattleC09BWildFlowEngineFixture::GetState(*CandidateEngine).ValidateInvariants(
			StateError));
	bValid &= TestTrue(
		TEXT("Bounded candidate-validation failure preserves complete Capture, pending, RNG, history, and replay state"),
		AreCaptureCheckpointObservationsIdentical(
			ObserveCaptureCheckpoint(*CandidateEngine),
			BeforeCandidateProbe));
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023D3CaptureCancellationStaleIdentityTest,
	"PokemonSolarus.Battle.ADR0002.3D3.Capture.Cancellation.FinalStaleIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023D3CaptureCancellationStaleIdentityTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	TUniquePtr<FBattleEngine> Engine;
	FActionStartStaleRandom* Random = nullptr;
	if (!TestTrue(TEXT("Final-stale Capture engine is created"),
			TryMakeActionStartStaleEngine(
				MakeStaleCaptureCancellationScenario(true),
				Engine,
				Random,
				{1}))
		|| !TestNotNull(TEXT("Final-stale Capture random seam is retained"), Random)
		|| !TestTrue(TEXT("Final-stale Capture is the last started action"),
			PrepareCaptureAsLastActionAfterFailedWildFlee(*Engine))
		|| !TestTrue(TEXT("Final-stale Capture has one exact replacement boundary"),
			TryPrepareCaptureReplacementBoundary(*Engine)))
	{
		return false;
	}

	const FCaptureCheckpointObservation Before = ObserveCaptureCheckpoint(*Engine);
	FCaptureCheckpointObservation AfterInjection;
	bool bObservedAfterInjection = false;
	bool bMutationSucceeded = false;
	const FActionId StaleActionId = MakeNumericId<FActionId>(900003);
	check(Random != nullptr);
	Random->ArmAfterTraceRead(
		2,
		[EnginePtr = Engine.Get(), Random, StaleActionId, &AfterInjection,
			&bObservedAfterInjection, &bMutationSucceeded]()
		{
			FBattleEngineState& State =
				FBattleC09BWildFlowEngineFixture::GetMutableState(*EnginePtr);
			if (State.LockedActions.IsValidIndex(State.CurrentLockedActionIndex))
			{
				State.LockedActions[State.CurrentLockedActionIndex].ActionId =
					StaleActionId;
				bMutationSucceeded = true;
			}
			Random->DisableFurtherTraceInjection();
			AfterInjection = ObserveCaptureCheckpoint(*EnginePtr);
			--AfterInjection.NextResolutionId;
			bObservedAfterInjection = true;
		});
	const FBattleResolution Rejected = Engine->ExecuteCurrentBagItem();
	const int32 TraceReads = Random->GetReadsSinceArm();
	const bool bInjected = Random->WasInjected();
	Random->Disarm();

	bool bValid = TestTrue(
		TEXT("Final stale identity mutation is observed before rejection"),
		bObservedAfterInjection && bMutationSucceeded);
	bValid &= TestTrue(
		TEXT("Final stale identity uses the second and final cancellation trace read"),
		bInjected && TraceReads == 2);
	bValid &= TestEqual(
		TEXT("Final stale identity mutation preserves the parent state version"),
		AfterInjection.StateVersion,
		Before.StateVersion);
	TArray<FBattleLockedActionState> ExpectedActions = Before.LockedActions;
	if (ExpectedActions.IsValidIndex(Before.LockedActionIndex))
	{
		ExpectedActions[Before.LockedActionIndex].ActionId = StaleActionId;
	}
	bValid &= TestTrue(
		TEXT("Final stale identity mutation changes only the current action identity"),
		AreCapturePreservedFactsIdentical(AfterInjection, Before)
			&& AreCapturePendingFactsIdentical(AfterInjection, Before)
			&& AreCaptureLockedActionsIdentical(
				AfterInjection.LockedActions,
				ExpectedActions)
			&& AfterInjection.Phase == Before.Phase
			&& AfterInjection.LockedActionIndex == Before.LockedActionIndex);
	bValid &= VerifyRejectedCaptureCancellationCheckpoint(
		*this,
		*Engine,
		AfterInjection,
		EBattleRejectionReason::StaleCheckpointIdentity,
		Rejected);
	bValid &= TestTrue(
		TEXT("Final stale rejection preserves the prior WildFlee RNG trace exactly"),
		Engine->ExportRandomTrace() == Before.RandomTrace);
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023D3CaptureAtomicPublicationTest,
	"PokemonSolarus.Battle.ADR0002.3D3.Capture.Execution.LegalFailureAndAtomicSuccess",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023D3CaptureAtomicPublicationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FBattlerId TargetId = MakeNumericId<FBattlerId>(OpponentLeftValue);
	const FTrainerId PlayerTrainerId = MakeNumericId<FTrainerId>(PlayerTrainerValue);
	FAtomicWildScenario Scenario = MakeAtomicCaptureScenario();

	TUniquePtr<FBattleEngine> FailureEngine;
	if (!TestTrue(TEXT("Legal failed-Capture engine is created"),
		TryMakeSequenceEngine(Scenario, {65535}, FailureEngine))
		|| !TestTrue(TEXT("Legal failed-Capture turn locks"),
			LockTurn(*FailureEngine, PlayerLeftValue, EBattleActionKind::Bag))
		|| !TestTrue(TEXT("Legal failed-Capture starts"),
			BeginExpectedWildAction(
				*FailureEngine,
				PlayerLeftValue,
				EBattleActionKind::Bag)))
	{
		return false;
	}
	const FCheckpointObservation FailureBefore = ObserveCheckpoint(*FailureEngine, TargetId);
	const FBattleResolution Failed = FailureEngine->ExecuteCurrentBagItem();
	const FBattleEngineState& FailureState =
		FBattleC09BWildFlowEngineFixture::GetState(*FailureEngine);
	const FBattleTrainerState* FailureTrainer = FailureState.FindTrainer(PlayerTrainerId);
	TestTrue(TEXT("Legal unsuccessful Capture is accepted"), Failed.WasAccepted());
	TestTrue(TEXT("Legal unsuccessful Capture preserves exact event order"),
		HasExactEventOrder(Failed, {
			EBattleEventType::ItemUsed,
			EBattleEventType::ItemConsumed,
			EBattleEventType::CaptureAttempted,
			EBattleEventType::ActionCompleted}));
	TestEqual(TEXT("Legal failure consumes exactly one Ball"),
		FailureTrainer != nullptr ? FailureTrainer->Bag[0].Count : INDEX_NONE,
		FailureBefore.PokeBallCount - 1);
	TestFalse(TEXT("Legal failure consumes exactly one Trainer Bag action"),
		FailureTrainer != nullptr
			&& FailureTrainer->ActionAllowance.bBagActionAvailable);
	TestEqual(TEXT("Legal failure commits one early-stopping draw"),
		FailureState.Random->GetTrace().Num(), FailureBefore.RandomTraceCount + 1);
	TestTrue(TEXT("Legal failure draw uses the Capture shake purpose"),
		!FailureState.Random->GetTrace().IsEmpty()
			&& FailureState.Random->GetTrace().Last().RulePurpose
				== FBattleCaptureCalculator::GetShakeCheckPurpose());
	TestTrue(TEXT("Legal failure leaves its target active"),
		FBattleC09BWildFlowEngineFixture::IsActive(*FailureEngine, TargetId));
	TestEqual(TEXT("Legal failure creates no pending record"),
		FailureState.PendingCaptures.Num(), 0);
	TestTrue(TEXT("Legal failure returns the exact single append"),
		IsReturnedResolutionAppendedExactlyOnce(*FailureEngine, Failed));

	TUniquePtr<FBattleEngine> SuccessEngine;
	if (!TestTrue(TEXT("Atomic successful-Capture engine is created"),
		TryMakeSequenceEngine(Scenario, {0, 0, 0, 0}, SuccessEngine))
		|| !TestTrue(TEXT("Atomic successful-Capture turn locks"),
			LockTurn(*SuccessEngine, PlayerLeftValue, EBattleActionKind::Bag))
		|| !TestTrue(TEXT("Atomic successful-Capture starts"),
			BeginExpectedWildAction(
				*SuccessEngine,
				PlayerLeftValue,
				EBattleActionKind::Bag)))
	{
		return false;
	}
	const FCheckpointObservation SuccessBefore = ObserveCheckpoint(*SuccessEngine, TargetId);
	const FBattleResolution Succeeded = SuccessEngine->ExecuteCurrentBagItem();
	const FBattleEngineState& SuccessState =
		FBattleC09BWildFlowEngineFixture::GetState(*SuccessEngine);
	const FBattleTrainerState* SuccessTrainer = SuccessState.FindTrainer(PlayerTrainerId);
	const FBattleBattlerState* CapturedBattler = SuccessState.FindBattler(TargetId);
	TestTrue(TEXT("Successful Capture is accepted"), Succeeded.WasAccepted());
	TestTrue(TEXT("Terminal successful Capture preserves exact event order"),
		HasExactEventOrder(Succeeded, {
			EBattleEventType::ItemUsed,
			EBattleEventType::ItemConsumed,
			EBattleEventType::CaptureAttempted,
			EBattleEventType::Captured,
			EBattleEventType::LeftActiveSlot,
			EBattleEventType::Removed,
			EBattleEventType::OpponentRemovalCheckpoint,
			EBattleEventType::ActionCompleted,
			EBattleEventType::BattleEnded}));
	TestEqual(TEXT("Successful Capture consumes exactly one Ball"),
		SuccessTrainer != nullptr ? SuccessTrainer->Bag[0].Count : INDEX_NONE,
		SuccessBefore.PokeBallCount - 1);
	TestFalse(TEXT("Successful Capture consumes Trainer Bag quota"),
		SuccessTrainer != nullptr
			&& SuccessTrainer->ActionAllowance.bBagActionAvailable);
	TestEqual(TEXT("Successful Capture commits exactly four shake draws"),
		SuccessState.Random->GetTrace().Num(), SuccessBefore.RandomTraceCount + 4);
	TestTrue(TEXT("Successful Capture atomically marks and removes the target"),
		CapturedBattler != nullptr
			&& CapturedBattler->bCaptured
			&& CapturedBattler->bRemoved
			&& !FBattleC09BWildFlowEngineFixture::IsActive(*SuccessEngine, TargetId));
	TestEqual(TEXT("Successful Capture appends one pending record"),
		SuccessState.PendingCaptures.Num(), SuccessBefore.PendingCaptureCount + 1);
	if (!SuccessState.PendingCaptures.IsEmpty())
	{
		const FBattlePendingCaptureRecord& Pending = SuccessState.PendingCaptures.Last();
		const FItemId HeldItemId = MakeDefinitionId<FItemId>(CaptureHeldItemName);
		TestEqual(TEXT("Pending Capture preserves current HP"),
			Pending.CurrentHP, Scenario.TargetCurrentHP);
		TestEqual(TEXT("Pending Capture preserves Party-first destination"),
			Pending.Destination, EBattlePendingCaptureDestination::Party);
		TestTrue(TEXT("Pending Capture retains original and current held item"),
			Pending.HeldItem.OriginalItemId == HeldItemId
				&& Pending.HeldItem.CurrentItemId == HeldItemId);
	}
	TestEqual(TEXT("Last-target Capture enters Victory"),
		SuccessState.Outcome, EBattleOutcome::Victory);
	TestEqual(TEXT("Last-target Capture retains Capture outcome cause"),
		SuccessState.OutcomeCause, EBattleOutcomeCause::Capture);
	TestEqual(TEXT("Last-target Capture is terminal"),
		SuccessState.Phase, EBattlePhase::Terminal);
	const int32 RemovalEventIndex = Succeeded.GetEvents().IndexOfByPredicate(
		[](const FBattleEvent& Event)
		{
			return Event.GetType() == EBattleEventType::OpponentRemovalCheckpoint;
		});
	TestTrue(TEXT("Removal checkpoint is staged into authoritative availability"),
		RemovalEventIndex != INDEX_NONE
			&& SuccessState.AvailableOpponentRemovalCheckpoints.Contains(
				Succeeded.GetEvents()[RemovalEventIndex].GetEventOrdinal()));
	TestTrue(TEXT("Successful Capture returns the exact single append"),
		IsReturnedResolutionAppendedExactlyOnce(*SuccessEngine, Succeeded));
	return true;
}

} // namespace BattleAtomicCaptureTestsPrivate

#endif
