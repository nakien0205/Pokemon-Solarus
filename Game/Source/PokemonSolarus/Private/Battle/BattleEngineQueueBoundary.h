#pragma once

#include "Battle/BattleActionQueue.h"
#include "Battle/BattleBagItem.h"
#include "Battle/BattleItem.h"
#include "Battle/BattleTargeting.h"
#include "Battle/BattleVolatile.h"
#include "Battle/BattleWildFlow.h"
#include "BattleEngineCommon.h"
#include "BattleEngineEvents.h"
#include "BattleEngineSwitchPipeline.h"
#include "BattleEngineTriggerRuntime.h"
#include "BattleFaintOutcomeResolver.h"
#include "Math/NumericLimits.h"

namespace BattleEngineQueueBoundaryPrivate
{
	using namespace BattleEngineCommonPrivate;
	using namespace BattleEngineEventsPrivate;
	using namespace BattleEngineSwitchPipelinePrivate;
	using namespace BattleEngineTriggerRuntimePrivate;

	template <typename TState>
	bool TryBuildReplacementCheckpointRequests(
		const TState& State,
		uint64 StateVersion,
		bool bAllowShiftPrompt,
		TArray<FBattleDecisionRequest>& OutRequests);

	bool TryRebuildReplacementCheckpointAfterEntryHazards(
		FBattleEngineState& State,
		const uint64 RequestStateVersion,
		const FResolutionId ResolutionId,
		const EBattleActionKind ActionKind,
		const FBattleEventSource& Source,
		const TConstArrayView<FBattlePendingReplacementState> AlreadyAnnouncedRequirements,
		TArray<FBattleEvent>& Events);

	EBattleOptionUnavailableReason ToUnavailableReason(const EBattleSwitchBlockReason Reason);

	bool IsBattleEngineExplicitTargetClass(const EBattleTargetClass TargetClass);

	TArray<FBattleTargetPositionFacts> BuildBattleEngineTargetPositions(
		const FBattleEngineState& State);

	bool TryGetCommandBand(
		const EBattleActionKind ActionKind,
		EBattleActionCommandBand& OutBand);

	bool TryBuildLockedActions(
		FBattleEngineState& State,
		const TArray<FBattleDecision>& Selections,
		const FResolutionId ResolutionId,
		TArray<FBattleLockedActionState>& OutActions,
		TArray<FBattleEvent>& OutPreLockEvents,
		bool& bOutReverseSpeed);

	void AddUnavailableAction(
		FBattleDecisionRequestSpec& Spec,
		const EBattleActionKind ActionKind,
		const EBattleOptionUnavailableReason Reason);

	void AddUnavailableMove(
		FBattleDecisionRequestSpec& Spec,
		const FMoveId MoveId,
		const EBattleOptionUnavailableReason Reason);

	void AddUnavailableSwitch(
		FBattleDecisionRequestSpec& Spec,
		const FPartySlotId PartySlotId,
		const EBattleOptionUnavailableReason Reason);

	void AddUnavailableItem(
		FBattleDecisionRequestSpec& Spec,
		const FItemId ItemId,
		const EBattleOptionUnavailableReason Reason);

	template <typename TState>
	bool TryBuildReplacementDecisionRequest(
		const TState& State,
		const FBattlePendingReplacementState& Pending,
		const uint64 StateVersion,
		FBattleDecisionRequest& OutRequest)
	{
		OutRequest = FBattleDecisionRequest();
		FBattleSwitchLegalityResult Legality;
		if (!TryBuildSwitchLegality(
			State,
			EBattleSwitchKind::Replacement,
			Pending.TrainerId,
			FBattlerId(),
			Pending.ActiveSlotId,
			TConstArrayView<FPartySlotId>(),
			Legality)
			|| Legality.GetLegalPartySlots().IsEmpty())
		{
			return false;
		}

		FBattleDecisionRequestSpec Spec;
		Spec.StateVersion = StateVersion;
		Spec.RequestKind = EBattleDecisionRequestKind::MandatoryReplacement;
		Spec.DecisionOwnerTrainerId = Pending.TrainerId;
		Spec.ActingSlotId = Pending.ActiveSlotId;
		Spec.LegalActionKinds.Add(EBattleActionKind::Replacement);
		Spec.LegalActiveTargets.Add(Pending.ActiveSlotId);
		for (const FBattleSwitchCandidateResult& Candidate : Legality.GetCandidates())
		{
			if (Candidate.bLegal)
			{
				Spec.LegalSwitchPartySlots.Add(Candidate.PartySlotId);
			}
			else
			{
				AddUnavailableSwitch(
					Spec,
					Candidate.PartySlotId,
					ToUnavailableReason(Candidate.Reason));
			}
		}

		FBattleRejection Rejection;
		return FBattleDecisionRequest::TryCreate(Spec, OutRequest, Rejection);
	}

	template <typename TState>
	bool TryBuildMandatoryReplacementRequests(
		const TState& State,
		const uint64 StateVersion,
		TArray<FBattleDecisionRequest>& OutRequests)
	{
		OutRequests.Reset();
		if (State.PendingReplacements.IsEmpty())
		{
			return false;
		}

		const FTrainerId OwnerTrainerId = State.PendingReplacements[0].TrainerId;
		for (const FBattlePendingReplacementState& Pending : State.PendingReplacements)
		{
			if (Pending.TrainerId != OwnerTrainerId)
			{
				continue;
			}

			FBattleDecisionRequest Request;
			if (!TryBuildReplacementDecisionRequest(
				State,
				Pending,
				StateVersion,
				Request))
			{
				OutRequests.Reset();
				return false;
			}
			OutRequests.Add(MoveTemp(Request));
		}
		return !OutRequests.IsEmpty() && OutRequests.Num() <= 2;
	}

	template <typename TState>
	bool TryBuildShiftDecisionRequest(
		const TState& State,
		const uint64 StateVersion,
		FBattleDecisionRequest& OutRequest)
	{
		OutRequest = FBattleDecisionRequest();
		if (State.CompiledEncounterPolicies.GetBattleStyle() != EBattleStylePolicy::Shift
			|| State.CompiledEncounterPolicies.GetFormat() != EBattleFormat::Single
			|| State.PendingReplacements.IsEmpty()
			|| State.PendingReplacements.ContainsByPredicate(
				[](const FBattlePendingReplacementState& Pending)
				{
					return Pending.ActiveSlotId.GetSide() == EBattleSide::Player;
				})
			|| !State.PendingReplacements.ContainsByPredicate(
				[](const FBattlePendingReplacementState& Pending)
				{
					return Pending.ActiveSlotId.GetSide() == EBattleSide::Opponent;
				}))
		{
			return false;
		}

		const FBattleTrainerEncounterPolicy* PlayerPolicy =
			State.CompiledEncounterPolicies.GetTrainerPolicies().FindByPredicate(
			[](const FBattleTrainerEncounterPolicy& Policy)
			{
				return Policy.Side == EBattleSide::Player
					&& Policy.Role == EBattleTrainerRole::Player;
			});
		const FBattleTrainerState* PlayerTrainer = PlayerPolicy != nullptr
			? State.FindTrainer(PlayerPolicy->TrainerId)
			: nullptr;
		const FBattleActivePositionState* PlayerActive = State.ActivePositions.FindByPredicate(
			[](const FBattleActivePositionState& Position)
			{
				return Position.ActiveSlotId.GetSide() == EBattleSide::Player
					&& Position.ActiveSlotId.GetPosition() == EBattlePosition::Left;
			});
		const FBattleBattlerState* PlayerBattler = PlayerActive != nullptr
			? State.FindBattler(PlayerActive->BattlerId)
			: nullptr;
		if (PlayerTrainer == nullptr
			|| PlayerActive == nullptr
			|| PlayerActive->TrainerId != PlayerTrainer->TrainerId
			|| !IsLivingSelectableBattler(PlayerBattler))
		{
			return false;
		}

		FBattleSwitchLegalityResult Legality;
		if (!TryBuildSwitchLegality(
			State,
			EBattleSwitchKind::Voluntary,
			PlayerTrainer->TrainerId,
			PlayerBattler->BattlerId,
			PlayerActive->ActiveSlotId,
			TConstArrayView<FPartySlotId>(),
			Legality)
			|| Legality.GetLegalPartySlots().IsEmpty())
		{
			return false;
		}

		FBattleDecisionRequestSpec Spec;
		Spec.StateVersion = StateVersion;
		Spec.RequestKind = EBattleDecisionRequestKind::ShiftResponse;
		Spec.DecisionOwnerTrainerId = PlayerTrainer->TrainerId;
		Spec.ActingBattlerId = PlayerBattler->BattlerId;
		Spec.ActingSlotId = PlayerActive->ActiveSlotId;
		Spec.LegalActionKinds.Add(EBattleActionKind::Switch);
		Spec.LegalActiveTargets.Add(PlayerActive->ActiveSlotId);
		for (const FBattleSwitchCandidateResult& Candidate : Legality.GetCandidates())
		{
			if (Candidate.bLegal)
			{
				Spec.LegalSwitchPartySlots.Add(Candidate.PartySlotId);
			}
			else
			{
				AddUnavailableSwitch(
					Spec,
					Candidate.PartySlotId,
					ToUnavailableReason(Candidate.Reason));
			}
		}

		FBattleRejection Rejection;
		return FBattleDecisionRequest::TryCreate(Spec, OutRequest, Rejection);
	}

	template <typename TState>
	bool TryBuildReplacementCheckpointRequests(
		const TState& State,
		const uint64 StateVersion,
		const bool bAllowShiftPrompt,
		TArray<FBattleDecisionRequest>& OutRequests)
	{
		OutRequests.Reset();
		if (State.Phase != EBattlePhase::MandatoryReplacement
			|| StateVersion == 0
			|| State.PendingReplacements.IsEmpty())
		{
			return false;
		}

		if (bAllowShiftPrompt)
		{
			FBattleDecisionRequest ShiftRequest;
			if (TryBuildShiftDecisionRequest(State, StateVersion, ShiftRequest))
			{
				OutRequests.Add(MoveTemp(ShiftRequest));
				return true;
			}
		}

		return TryBuildMandatoryReplacementRequests(State, StateVersion, OutRequests);
	}

	bool TryBuildPivotDecisionRequest(
		const FBattleEngineState& State,
		const FBattleLockedActionState& Action,
		const uint64 StateVersion,
		FBattleDecisionRequest& OutRequest);

	bool TryAddBattleEngineMoveSelection(
		const FBattleActivePositionState& ActingPosition,
		const TArray<FBattleTargetPositionFacts>& Positions,
		const FMoveId MoveId,
		const EBattleTargetClass TargetClass,
		FBattleDecisionRequestSpec& InOutSpec,
		bool& OutHasLegalTarget);

	int32 GetMoveCurrentPP(
		const FBattleBattlerState& Battler,
		const FMoveId MoveId);

	template <typename TState>
	bool TryResolveVolatileMoveGate(
		const TState& State,
		const FBattleBattlerState& Battler,
		const FBattleMoveDefinition& SelectedMove,
		const bool bNoUsableOrdinaryMove,
		FBattleVolatileMoveGateResult& OutResult)
	{
		FBattleVolatileMoveGateFacts Facts;
		Facts.SelectedMoveId = SelectedMove.Id;
		Facts.SelectedMoveCategory = SelectedMove.Category;
		Facts.bSelectedMoveIsStruggle = SelectedMove.Id
			== FBattleBuiltInMoveDefinitions::GetStruggleMoveId();
		Facts.bNoUsableOrdinaryMove = bNoUsableOrdinaryMove;
		Facts.bTauntActive = HasVolatile(Battler, FBattleVolatileRules::GetTauntId());

		FMoveId EncoreMoveId;
		if (HasVolatile(Battler, FBattleVolatileRules::GetEncoreId())
			&& TryGetVolatilePayloadMoveId(
				State,
				Battler.BattlerId,
				FBattleVolatileRules::GetEncoreId(),
				EncoreMoveId))
		{
			Facts.EncoreMoveId = EncoreMoveId;
			Facts.bEncoreMoveStillValid = State.Catalog.FindMove(EncoreMoveId) != nullptr
				&& Battler.Moves.ContainsByPredicate(
					[EncoreMoveId](const FBattleMoveSlotState& Slot)
					{
						return Slot.MoveId == EncoreMoveId;
					});
			Facts.EncoreMoveCurrentPP = GetMoveCurrentPP(Battler, EncoreMoveId);
		}

		FMoveId DisabledMoveId;
		if (HasVolatile(Battler, FBattleVolatileRules::GetDisableId())
			&& TryGetVolatilePayloadMoveId(
				State,
				Battler.BattlerId,
				FBattleVolatileRules::GetDisableId(),
				DisabledMoveId))
		{
			Facts.DisabledMoveId = DisabledMoveId;
			Facts.bDisabledMoveStillValid = State.Catalog.FindMove(DisabledMoveId) != nullptr
				&& Battler.Moves.ContainsByPredicate(
					[DisabledMoveId](const FBattleMoveSlotState& Slot)
					{
						return Slot.MoveId == DisabledMoveId;
					});
			Facts.DisabledMoveCurrentPP = GetMoveCurrentPP(Battler, DisabledMoveId);
		}
		return FBattleVolatileRules::TryResolveMoveGate(Facts, OutResult);
	}

	EBattleOptionUnavailableReason ToUnavailableReason(
		const EBattleVolatileMoveGateOutcome Outcome);

	bool TryBuildBagItemUseFacts(
		const FBattleEngineState& State,
		const FBattleTrainerState& ActingTrainer,
		const FBattleBattlerState& ActingBattler,
		const FBattleItemDefinition& ItemDefinition,
		const EBattleBagItemTargetKind TargetKind,
		const FBattleBattlerState& TargetBattler,
		const FBattleActivePositionState* TargetActive,
		FBattleBagItemUseFacts& OutFacts);

	bool TryBuildDecisionRequest(
		const FBattleEngineState& State,
		const FBattleDecisionActorState& Actor,
		const uint64 StateVersion,
		const TConstArrayView<FBattleDecision> AdditionalSelections,
		FBattleDecisionRequest& OutRequest,
		FBattleRejection& OutRejection);

	int32 GetDecisionSequenceBand(const FBattleTrainerEncounterPolicy& Trainer);

	TArray<FBattleDecisionOwnerState> BuildDecisionOwnerSequence(const FBattleEngineState& State);

	bool TryBuildPendingRequests(
		const FBattleEngineState& State,
		const TArray<FBattleDecisionOwnerState>& Sequence,
		const int32 OwnerIndex,
		const int32 ActorOffset,
		const uint64 StateVersion,
		const TConstArrayView<FBattleDecision> AdditionalSelections,
		TArray<FBattleDecisionRequest>& OutRequests,
		FBattleRejection& OutRejection);

	template <typename TState>
	bool TryAppendAtomicSwitchBoundaryEvents(
		TState& StateView,
		const FResolutionId ResolutionId,
		const FBattleLockedActionState& Action,
		TArray<FBattleEvent>& Events)
	{
		FBattleQueueBoundaryPlan BoundaryPlan;
		if (!FBattleFaintOutcomeResolver::ResolveQueueBoundary(
				StateView.Phase,
				StateView.Outcome,
				StateView.CurrentLockedActionIndex,
				StateView.LockedActions.Num(),
				StateView.Setup.GetStartingActive(),
				StateView.Battlers,
				StateView.ActivePositions,
				BoundaryPlan)
			|| !FBattleFaintOutcomeResolver::TryApplyQueueBoundaryPlan(
				StateView.Phase,
				BoundaryPlan))
		{
			return false;
		}
		const TArray<FBattleReplacementRequirement>& Requirements =
			BoundaryPlan.Requirements;
		if (StateView.Phase == EBattlePhase::MandatoryReplacement)
		{
			if (Requirements.IsEmpty()
				|| StateView.StateVersion == TNumericLimits<uint64>::Max())
			{
				return false;
			}
			StateView.PendingReplacements.Reset();
			for (const FBattleReplacementRequirement& Requirement : Requirements)
			{
				FBattlePendingReplacementState& Pending =
					StateView.PendingReplacements.AddDefaulted_GetRef();
				Pending.TrainerId = Requirement.Target.TrainerId;
				Pending.ActiveSlotId = Requirement.Target.ActiveSlotId;
			}
			TArray<FBattleDecisionRequest> Requests;
			if (!TryBuildReplacementCheckpointRequests(
					StateView,
					StateView.StateVersion + 1,
					true,
					Requests)
				|| Requests.IsEmpty())
			{
				return false;
			}
			StateView.PendingDecisionRequests = MoveTemp(Requests);
			StateView.PendingDecision = StateView.PendingDecisionRequests[0];
		}
		else if (StateView.Phase == EBattlePhase::EndOfTurn)
		{
			StateView.PendingReplacements.Reset();
			StateView.PendingDecisionRequests.Reset();
			StateView.PendingDecision.Reset();
		}
		else if (StateView.Phase != EBattlePhase::Resolving
			&& StateView.Phase != EBattlePhase::Terminal)
		{
			return false;
		}

		for (const FBattleReplacementRequirement& Requirement : Requirements)
		{
			Events.Add(MakeTargetedActionEvent(
				StateView,
				ResolutionId,
				Action,
				EBattleEventType::ReplacementRequired,
				EBattleEventCause::Rule,
				Requirement.Target));
		}
		return true;
	}
}
