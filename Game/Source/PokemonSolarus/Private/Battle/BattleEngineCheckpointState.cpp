#include "BattleEngineCheckpointState.h"

namespace BattleEngineCheckpointStatePrivate
{
	using namespace BattleEngineCommonPrivate;

	FVoluntarySwitchBattlerIdentity MakeVoluntarySwitchBattlerIdentity(
		const FBattleBattlerState& Battler)
	{
		FVoluntarySwitchBattlerIdentity Identity;
		Identity.TrainerId = Battler.TrainerId;
		Identity.BattlerId = Battler.BattlerId;
		Identity.SourcePokemonId = Battler.SourcePokemonId;
		Identity.PartySlotId = Battler.PartySlotId;
		Identity.HeldItemInstanceId = Battler.HeldItem.InstanceId;
		Identity.CurrentHeldItemId = Battler.HeldItem.CurrentItemId;
		Identity.AbilityId = Battler.AbilityId;
		Identity.MajorStatusId = Battler.MajorStatusId;
		Identity.LastMoveId = Battler.LastMoveId;
		Identity.EnteredActiveOnTurnId = Battler.EnteredActiveOnTurnId;
		Identity.CurrentHP = Battler.CurrentHP;
		Identity.VolatileCount = Battler.Volatiles.Num();
		Identity.bFainted = Battler.bFainted;
		Identity.bCaptured = Battler.bCaptured;
		Identity.bRemoved = Battler.bRemoved;
		Identity.bFaintTransitionPending = Battler.bFaintTransitionPending;
		Identity.bEgg = Battler.bEgg;
		Identity.bAbilitySuppressed = Battler.bAbilitySuppressed;
		return Identity;
	}

	bool MatchesVoluntarySwitchBattlerIdentity(
		const FBattleBattlerState& Battler,
		const FVoluntarySwitchBattlerIdentity& Identity)
	{
		return Battler.TrainerId == Identity.TrainerId
			&& Battler.BattlerId == Identity.BattlerId
			&& Battler.SourcePokemonId == Identity.SourcePokemonId
			&& Battler.PartySlotId == Identity.PartySlotId
			&& Battler.HeldItem.InstanceId == Identity.HeldItemInstanceId
			&& Battler.HeldItem.CurrentItemId == Identity.CurrentHeldItemId
			&& Battler.AbilityId == Identity.AbilityId
			&& Battler.MajorStatusId == Identity.MajorStatusId
			&& Battler.LastMoveId == Identity.LastMoveId
			&& Battler.EnteredActiveOnTurnId == Identity.EnteredActiveOnTurnId
			&& Battler.CurrentHP == Identity.CurrentHP
			&& Battler.Volatiles.Num() == Identity.VolatileCount
			&& Battler.bFainted == Identity.bFainted
			&& Battler.bCaptured == Identity.bCaptured
			&& Battler.bRemoved == Identity.bRemoved
			&& Battler.bFaintTransitionPending == Identity.bFaintTransitionPending
			&& Battler.bEgg == Identity.bEgg
			&& Battler.bAbilitySuppressed == Identity.bAbilitySuppressed;
	}

	bool ArePivotDecisionsIdentical(
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

	bool ArePivotDecisionRequestsIdentical(
		const FBattleDecisionRequest& Left,
		const FBattleDecisionRequest& Right)
	{
		return Left.IsValid() == Right.IsValid()
			&& Left.GetStateVersion() == Right.GetStateVersion()
			&& Left.GetRequestKind() == Right.GetRequestKind()
			&& Left.GetDecisionOwnerTrainerId() == Right.GetDecisionOwnerTrainerId()
			&& Left.GetActingBattlerId() == Right.GetActingBattlerId()
			&& Left.GetActingSlotId() == Right.GetActingSlotId()
			&& AreOrderedPivotIdentityValuesEqual(
				Left.GetLegalActionKinds(),
				Right.GetLegalActionKinds(),
				[](const EBattleActionKind L, const EBattleActionKind R)
				{
					return L == R;
				})
			&& AreOrderedPivotIdentityValuesEqual(
				Left.GetLegalMoveIds(),
				Right.GetLegalMoveIds(),
				[](const FMoveId& L, const FMoveId& R)
				{
					return L == R;
				})
			&& AreOrderedPivotIdentityValuesEqual(
				Left.GetAutomaticallyTargetedMoveIds(),
				Right.GetAutomaticallyTargetedMoveIds(),
				[](const FMoveId& L, const FMoveId& R)
				{
					return L == R;
				})
			&& AreOrderedPivotIdentityValuesEqual(
				Left.GetLegalSwitchPartySlots(),
				Right.GetLegalSwitchPartySlots(),
				[](const FPartySlotId L, const FPartySlotId R)
				{
					return L == R;
				})
			&& AreOrderedPivotIdentityValuesEqual(
				Left.GetLegalItemIds(),
				Right.GetLegalItemIds(),
				[](const FItemId& L, const FItemId& R)
				{
					return L == R;
				})
			&& AreOrderedPivotIdentityValuesEqual(
				Left.GetLegalActiveTargets(),
				Right.GetLegalActiveTargets(),
				[](const FActiveSlotId L, const FActiveSlotId R)
				{
					return L == R;
				})
			&& AreOrderedPivotIdentityValuesEqual(
				Left.GetLegalPartyTargets(),
				Right.GetLegalPartyTargets(),
				[](const FPartySlotId L, const FPartySlotId R)
				{
					return L == R;
				})
			&& AreOrderedPivotIdentityValuesEqual(
				Left.GetLegalMoveTargets(),
				Right.GetLegalMoveTargets(),
				[](const FBattleMoveTargetOption& L, const FBattleMoveTargetOption& R)
				{
					return L.MoveId == R.MoveId && L.ActiveSlotId == R.ActiveSlotId;
				})
			&& AreOrderedPivotIdentityValuesEqual(
				Left.GetLegalItemPartyTargets(),
				Right.GetLegalItemPartyTargets(),
				[](const FBattleItemPartyTargetOption& L,
					const FBattleItemPartyTargetOption& R)
				{
					return L.ItemId == R.ItemId && L.PartySlotId == R.PartySlotId;
				})
			&& AreOrderedPivotIdentityValuesEqual(
				Left.GetLegalItemActiveTargets(),
				Right.GetLegalItemActiveTargets(),
				[](const FBattleItemActiveTargetOption& L,
					const FBattleItemActiveTargetOption& R)
				{
					return L.ItemId == R.ItemId && L.ActiveSlotId == R.ActiveSlotId;
				})
			&& AreOrderedPivotIdentityValuesEqual(
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

	bool ArePivotTargetResolutionsIdentical(
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
			&& AreOrderedPivotIdentityValuesEqual(
				TConstArrayView<FBattleResolvedTarget>(L.Targets),
				TConstArrayView<FBattleResolvedTarget>(R.Targets),
				[](const FBattleResolvedTarget& LTarget,
					const FBattleResolvedTarget& RTarget)
				{
					return LTarget == RTarget;
				});
	}

	bool ArePivotLockedActionsIdentical(
		const FBattleLockedActionState& Left,
		const FBattleLockedActionState& Right)
	{
		return Left.ActionId == Right.ActionId
			&& Left.QueueOrdinal == Right.QueueOrdinal
			&& ArePivotDecisionsIdentical(Left.Decision, Right.Decision)
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
			&& ArePivotTargetResolutionsIdentical(
				Left.TargetResolution,
				Right.TargetResolution)
			&& Left.EffectExecutionState == Right.EffectExecutionState
			&& Left.bFinished == Right.bFinished;
	}

	/** Exact caller-serialized identity for a Pivot response continuing one Fight action. */

	bool TryCaptureAtomicCheckpointCommonDelta(
		const FAtomicCheckpointCommonPreparation& Preparation,
		FAtomicCheckpointCommonDelta& OutDelta)
	{
		OutDelta = FAtomicCheckpointCommonDelta();
		TSet<FBattlerId> BattlerIds;
		for (const FBattleBattlerState& Battler : Preparation.Battlers)
		{
			if (!Battler.BattlerId.IsValid() || BattlerIds.Contains(Battler.BattlerId))
			{
				return false;
			}
			BattlerIds.Add(Battler.BattlerId);
			FAtomicBattlerRecordDelta& Record = OutDelta.Battlers.AddDefaulted_GetRef();
			Record.BattlerId = Battler.BattlerId;
			Record.After = Battler;
		}

		TSet<FActiveSlotId> ActiveSlotIds;
		for (const FBattleActivePositionState& Position : Preparation.ActivePositions)
		{
			if (!Position.ActiveSlotId.IsValid()
				|| ActiveSlotIds.Contains(Position.ActiveSlotId))
			{
				return false;
			}
			ActiveSlotIds.Add(Position.ActiveSlotId);
			FAtomicActivePositionRecordDelta& Record =
				OutDelta.ActivePositions.AddDefaulted_GetRef();
			Record.ActiveSlotId = Position.ActiveSlotId;
			Record.After = Position;
		}

		OutDelta.TriggerFramework = Preparation.TriggerFramework;
		OutDelta.MoveRedirectionRegistrations =
			Preparation.MoveRedirectionRegistrations;
		OutDelta.AllyActionPowerModifierRegistrations =
			Preparation.AllyActionPowerModifierRegistrations;
		OutDelta.AbilityItemRevealTracker = Preparation.AbilityItemRevealTracker;
		OutDelta.HeldItemLedger = Preparation.HeldItemLedger;
		OutDelta.NextConditionCreationOrdinal = Preparation.NextConditionCreationOrdinal;
		OutDelta.NextTriggerReentrancyToken = Preparation.NextTriggerReentrancyToken;
		OutDelta.NextLockedActionIndex = Preparation.CurrentLockedActionIndex;
		OutDelta.Phase = Preparation.Phase;
		OutDelta.Outcome = Preparation.Outcome;
		OutDelta.OutcomeCause = Preparation.OutcomeCause;
		OutDelta.PendingDecision = Preparation.PendingDecision;
		OutDelta.PendingDecisionRequests = Preparation.PendingDecisionRequests;
		OutDelta.PendingReplacements = Preparation.PendingReplacements;
		OutDelta.AvailableOpponentRemovalCheckpoints =
			Preparation.AvailableOpponentRemovalCheckpoints;
		return true;
	}

	bool TryCaptureAtomicFieldSideDelta(
		const FAtomicCheckpointCommonPreparation& Common,
		const FBattleFieldState& Field,
		const TConstArrayView<FBattleSideState> Sides,
		FAtomicSwitchStateDelta& OutDelta)
	{
		OutDelta = FAtomicSwitchStateDelta();
		if (!TryCaptureAtomicCheckpointCommonDelta(Common, OutDelta))
		{
			return false;
		}
		OutDelta.Field = Field;
		TSet<EBattleSide> SeenSides;
		for (const FBattleSideState& Side : Sides)
		{
			if (SeenSides.Contains(Side.Side))
			{
				return false;
			}
			SeenSides.Add(Side.Side);
			FAtomicSideRecordDelta& Record = OutDelta.Sides.AddDefaulted_GetRef();
			Record.Side = Side.Side;
			Record.After = Side;
		}
		return true;
	}

	bool TryCaptureAtomicSwitchDelta(
		const FSwitchCheckpointPreparation& Preparation,
		FAtomicSwitchStateDelta& OutDelta)
	{
		return TryCaptureAtomicFieldSideDelta(
			Preparation.Common,
			Preparation.Field,
			Preparation.Sides,
			OutDelta);
	}

	bool AreAtomicCheckpointCommonDeltaRecordsValid(
		const TConstArrayView<FVoluntarySwitchBattlerIdentity> ExpectedBattlers,
		const TConstArrayView<FVoluntarySwitchActiveIdentity> ExpectedActivePositions,
		const FAtomicCheckpointCommonDelta& Delta)
	{
		if (Delta.Battlers.Num() != ExpectedBattlers.Num()
			|| Delta.ActivePositions.Num() != ExpectedActivePositions.Num())
		{
			return false;
		}
		for (const FVoluntarySwitchBattlerIdentity& Expected : ExpectedBattlers)
		{
			const FAtomicBattlerRecordDelta* Staged = Delta.Battlers.FindByPredicate(
				[&Expected](const FAtomicBattlerRecordDelta& Candidate)
				{
					return Candidate.BattlerId == Expected.BattlerId;
				});
			if (Staged == nullptr
				|| Staged->After.TrainerId != Expected.TrainerId
				|| Staged->After.SourcePokemonId != Expected.SourcePokemonId
				|| Staged->After.PartySlotId != Expected.PartySlotId)
			{
				return false;
			}
		}
		for (const FVoluntarySwitchActiveIdentity& Expected : ExpectedActivePositions)
		{
			if (!Delta.ActivePositions.ContainsByPredicate(
					[&Expected](const FAtomicActivePositionRecordDelta& Candidate)
					{
						return Candidate.ActiveSlotId == Expected.ActiveSlotId
							&& Candidate.After.bAvailable == Expected.bAvailable;
					}))
			{
				return false;
			}
		}
		for (const FBattleHeldItemInstanceState& Item : Delta.HeldItemLedger.GetStates())
		{
			if (!Item.InstanceId.IsValid())
			{
				return false;
			}
		}
		for (const FBattleTriggerRegistrationState& Registration :
			Delta.TriggerFramework.GetActiveRegistrations())
		{
			if (!Registration.RegistrationId.IsValid())
			{
				return false;
			}
		}
		return true;
	}

	void ApplyAtomicCheckpointCommonDelta(
		FBattleEngineState& State,
		const FAtomicCheckpointCommonDelta& Delta)
	{
		for (const FAtomicBattlerRecordDelta& Record : Delta.Battlers)
		{
			FBattleBattlerState* Battler = State.FindMutableBattler(Record.BattlerId);
			check(Battler != nullptr);
			*Battler = Record.After;
		}
		for (const FAtomicActivePositionRecordDelta& Record : Delta.ActivePositions)
		{
			FBattleActivePositionState* Position =
				State.FindMutableActivePosition(Record.ActiveSlotId);
			check(Position != nullptr);
			*Position = Record.After;
		}
		State.TriggerFramework = Delta.TriggerFramework;
		State.MoveRedirectionRegistrations = Delta.MoveRedirectionRegistrations;
		State.AllyActionPowerModifierRegistrations =
			Delta.AllyActionPowerModifierRegistrations;
		State.AbilityItemRevealTracker = Delta.AbilityItemRevealTracker;
		State.HeldItemLedger = Delta.HeldItemLedger;
		State.NextConditionCreationOrdinal = Delta.NextConditionCreationOrdinal;
		State.NextTriggerReentrancyToken = Delta.NextTriggerReentrancyToken;
		State.CurrentLockedActionIndex = Delta.NextLockedActionIndex;
		State.Phase = Delta.Phase;
		State.Outcome = Delta.Outcome;
		State.OutcomeCause = Delta.OutcomeCause;
		State.PendingDecision = Delta.PendingDecision;
		State.PendingDecisionRequests = Delta.PendingDecisionRequests;
		State.PendingReplacements = Delta.PendingReplacements;
		State.AvailableOpponentRemovalCheckpoints =
			Delta.AvailableOpponentRemovalCheckpoints;
	}

	void ApplyAtomicSwitchStateDelta(
		FBattleEngineState& State,
		const FAtomicSwitchStateDelta& Delta)
	{
		ApplyAtomicCheckpointCommonDelta(State, Delta);
		State.Field = Delta.Field;
		for (const FAtomicSideRecordDelta& Record : Delta.Sides)
		{
			FBattleSideState* Side = State.Sides.FindByPredicate(
				[&Record](const FBattleSideState& Candidate)
				{
					return Candidate.Side == Record.Side;
				});
			check(Side != nullptr);
			*Side = Record.After;
		}
	}
}
