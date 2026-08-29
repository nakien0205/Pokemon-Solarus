#include "Battle/BattleEngine.h"
#include "Battle/BattleAbility.h"
#include "Battle/BattleBagItem.h"
#include "Battle/BattleCapture.h"
#include "Battle/BattleEffectExecutor.h"
#include "BattleEntryHazardPrevention.h"
#include "Battle/BattleFieldSideConditions.h"
#include "Battle/BattleFaintOutcomeResolver.h"
#include "Battle/BattleItem.h"
#include "Battle/BattleMajorStatus.h"
#include "Battle/BattleState.h"
#include "Battle/BattleStatCalculator.h"
#include "Battle/BattleSwitching.h"
#include "Battle/BattleVolatile.h"
#include "Battle/BattleWildFlow.h"
#include "BattleEngineCheckpointState.h"
#include "BattleEngineCommon.h"
#include "BattleEngineEvents.h"
#include "BattleEngineQueueBoundary.h"
#include "BattleEngineSwitchPipeline.h"
#include "BattleEngineTriggerRuntime.h"
#include "BattleAllyActionPowerModifier.h"
#include "BattleResolutionCommit.h"
#include "Math/NumericLimits.h"

namespace BattleEngineActionStartPrivate
{
	using namespace BattleEngineCheckpointStatePrivate;
	using namespace BattleEngineCommonPrivate;
	using namespace BattleEngineEventsPrivate;
	using namespace BattleEngineQueueBoundaryPrivate;
	using namespace BattleEngineSwitchPipelinePrivate;
	using namespace BattleEngineTriggerRuntimePrivate;

	/** Exact immutable identity for one not-yet-started locked action. */
	struct FActionStartCheckpointIdentity
	{
		FBattleResolutionCommitIdentity CommitIdentity;
		EBattlePhase ExpectedPhase = EBattlePhase::Setup;
		int32 ExpectedLockedActionCount = 0;
		int32 ExpectedEventCount = 0;
		uint64 ExpectedQueueOrdinal = 0;
		FTrainerId ExpectedDecisionOwnerTrainerId;
		FBattlerId ExpectedActorId;
		FActiveSlotId ExpectedActingSlotId;
		EBattleActionKind ExpectedActionKind = EBattleActionKind::Fight;
		FMoveId ExpectedMoveId;
		FPartySlotId ExpectedSwitchPartySlotId;
		FItemId ExpectedItemId;
		FPartySlotId ExpectedItemPartyTargetId;
		FActiveSlotId ExpectedActiveTargetId;
		EBattleTargetClass ExpectedTargetClass = EBattleTargetClass::SelectedOpponent;
		FBattlerId ExpectedSelectedTargetBattlerId;
		bool bExpectedStarted = false;
		bool bExpectedMoveCommitted = false;
		bool bExpectedTargetResolution = false;
		EBattleLockedEffectExecutionState ExpectedEffectExecutionState =
			EBattleLockedEffectExecutionState::Pending;
		bool bExpectedFinished = false;
	};

	bool TryCaptureActionStartCheckpointIdentity(
		const FBattleEngineState& State,
		const FResolutionId ResolutionId,
		const FBattleLockedActionState& Action,
		FActionStartCheckpointIdentity& OutIdentity)
	{
		OutIdentity = FActionStartCheckpointIdentity();
		if (!ResolutionId.IsValid()
			|| !Action.ActionId.IsValid()
			|| State.StateVersion == 0
			|| (State.Phase != EBattlePhase::Locked
				&& State.Phase != EBattlePhase::Resolving)
			|| !State.LockedActions.IsValidIndex(State.CurrentLockedActionIndex)
			|| &State.LockedActions[State.CurrentLockedActionIndex] != &Action
			|| Action.bStarted
			|| Action.bMoveCommitted
			|| Action.TargetResolution.IsSet()
			|| Action.EffectExecutionState != EBattleLockedEffectExecutionState::Pending
			|| Action.bFinished
			|| !State.Random.IsValid()
			|| State.NextResolutionId == 0
			|| State.NextEventOrdinal == 0)
		{
			return false;
		}

		OutIdentity.CommitIdentity.ResolutionId = ResolutionId;
		OutIdentity.CommitIdentity.OwningActionId = Action.ActionId;
		OutIdentity.CommitIdentity.ExpectedStateVersion = State.StateVersion;
		OutIdentity.CommitIdentity.ExpectedLockedActionIndex =
			State.CurrentLockedActionIndex;
		OutIdentity.CommitIdentity.ExpectedNextResolutionId = State.NextResolutionId;
		OutIdentity.CommitIdentity.ExpectedEventOrdinal = State.NextEventOrdinal;
		OutIdentity.CommitIdentity.ExpectedResolutionCount = State.Resolutions.Num();
		OutIdentity.CommitIdentity.ExpectedRandomTraceCount = State.Random->GetTrace().Num();
		OutIdentity.CommitIdentity.ExpectedAllyActionPowerModifiers =
			State.AllyActionPowerModifierRegistrations;
		OutIdentity.ExpectedPhase = State.Phase;
		OutIdentity.ExpectedLockedActionCount = State.LockedActions.Num();
		OutIdentity.ExpectedEventCount = State.OrderedEvents.Num();
		OutIdentity.ExpectedQueueOrdinal = Action.QueueOrdinal;
		OutIdentity.ExpectedDecisionOwnerTrainerId =
			Action.Decision.GetDecisionOwnerTrainerId();
		OutIdentity.ExpectedActorId = Action.Decision.GetActingBattlerId();
		OutIdentity.ExpectedActingSlotId = Action.OrderKey.ActingSlotId;
		OutIdentity.ExpectedActionKind = Action.Decision.GetActionKind();
		OutIdentity.ExpectedMoveId = Action.Decision.GetMoveId();
		OutIdentity.ExpectedSwitchPartySlotId =
			Action.Decision.GetSwitchPartySlotId();
		OutIdentity.ExpectedItemId = Action.Decision.GetItemId();
		OutIdentity.ExpectedItemPartyTargetId =
			Action.Decision.GetItemPartyTargetId();
		OutIdentity.ExpectedActiveTargetId = Action.Decision.GetActiveTargetId();
		OutIdentity.ExpectedTargetClass = Action.TargetClass;
		OutIdentity.ExpectedSelectedTargetBattlerId =
			Action.SelectedTargetBattlerId;
		OutIdentity.bExpectedStarted = Action.bStarted;
		OutIdentity.bExpectedMoveCommitted = Action.bMoveCommitted;
		OutIdentity.bExpectedTargetResolution = Action.TargetResolution.IsSet();
		OutIdentity.ExpectedEffectExecutionState = Action.EffectExecutionState;
		OutIdentity.bExpectedFinished = Action.bFinished;
		return true;
	}

	bool IsActionStartCheckpointIdentityCurrent(
		const FBattleEngineState& State,
		const FActionStartCheckpointIdentity& Identity)
	{
		// Read the trace before comparing state so a test double can deterministically
		// inject a stale caller-serialized identity without consuming randomness.
		const int32 RandomTraceCount = State.Random.IsValid()
			? State.Random->GetTrace().Num()
			: INDEX_NONE;
		const FBattleResolutionCommitIdentity& Commit = Identity.CommitIdentity;
		if (!Commit.ResolutionId.IsValid()
			|| !Commit.OwningActionId.IsValid()
			|| State.Phase != Identity.ExpectedPhase
			|| State.StateVersion != Commit.ExpectedStateVersion
			|| State.CurrentLockedActionIndex != Commit.ExpectedLockedActionIndex
			|| State.LockedActions.Num() != Identity.ExpectedLockedActionCount
			|| State.NextResolutionId != Commit.ExpectedNextResolutionId
			|| State.NextEventOrdinal != Commit.ExpectedEventOrdinal
			|| State.Resolutions.Num() != Commit.ExpectedResolutionCount
			|| State.OrderedEvents.Num() != Identity.ExpectedEventCount
			|| RandomTraceCount != Commit.ExpectedRandomTraceCount
			|| !FBattleAllyActionPowerModifier::AreRegistrationsIdentical(
				State.AllyActionPowerModifierRegistrations,
				Commit.ExpectedAllyActionPowerModifiers)
			|| !State.LockedActions.IsValidIndex(Commit.ExpectedLockedActionIndex))
		{
			return false;
		}

		const FBattleLockedActionState& Action =
			State.LockedActions[Commit.ExpectedLockedActionIndex];
		return Action.ActionId == Commit.OwningActionId
			&& Action.QueueOrdinal == Identity.ExpectedQueueOrdinal
			&& Action.Decision.GetDecisionOwnerTrainerId()
				== Identity.ExpectedDecisionOwnerTrainerId
			&& Action.Decision.GetActingBattlerId() == Identity.ExpectedActorId
			&& Action.OrderKey.ActingSlotId == Identity.ExpectedActingSlotId
			&& Action.Decision.GetActionKind() == Identity.ExpectedActionKind
			&& Action.Decision.GetMoveId() == Identity.ExpectedMoveId
			&& Action.Decision.GetSwitchPartySlotId()
				== Identity.ExpectedSwitchPartySlotId
			&& Action.Decision.GetItemId() == Identity.ExpectedItemId
			&& Action.Decision.GetItemPartyTargetId()
				== Identity.ExpectedItemPartyTargetId
			&& Action.Decision.GetActiveTargetId() == Identity.ExpectedActiveTargetId
			&& Action.TargetClass == Identity.ExpectedTargetClass
			&& Action.SelectedTargetBattlerId
				== Identity.ExpectedSelectedTargetBattlerId
			&& Action.bStarted == Identity.bExpectedStarted
			&& Action.bMoveCommitted == Identity.bExpectedMoveCommitted
			&& Action.TargetResolution.IsSet()
				== Identity.bExpectedTargetResolution
			&& Action.EffectExecutionState == Identity.ExpectedEffectExecutionState
			&& Action.bFinished == Identity.bExpectedFinished;
	}

	struct FActionStartStagedHeldItem
	{
		FBattlerId BattlerId;
		FBattleHeldItemState HeldItem;
	};

	/** Small action-start-only projection of the fallible trigger/item/volatile work. */
	struct FActionStartMechanicsStage
	{
		FBattleTriggerFramework TriggerFramework;
		uint64 NextTriggerReentrancyToken = 0;
		FBattleHeldItemLedger HeldItemLedger;
		TArray<FActionStartStagedHeldItem> HeldItems;
		FBattlerId ActorId;
		TArray<FBattleConditionState> ActorVolatiles;

		void Capture(
			const FBattleEngineState& State,
			const FBattlerId InActorId)
		{
			TriggerFramework = State.TriggerFramework;
			NextTriggerReentrancyToken = State.NextTriggerReentrancyToken;
			HeldItemLedger = State.HeldItemLedger;
			HeldItems.Reset();
			for (const FBattleBattlerState& Battler : State.Battlers)
			{
				if (!IsHeldItemActive(Battler))
				{
					continue;
				}
				FActionStartStagedHeldItem& Staged = HeldItems.AddDefaulted_GetRef();
				Staged.BattlerId = Battler.BattlerId;
				Staged.HeldItem = Battler.HeldItem;
			}
			ActorId = InActorId;
			const FBattleBattlerState* Actor = State.FindBattler(ActorId);
			ActorVolatiles = Actor != nullptr
				? Actor->Volatiles
				: TArray<FBattleConditionState>();
		}

		FActionStartStagedHeldItem* FindMutableHeldItem(const FBattlerId BattlerId)
		{
			return HeldItems.FindByPredicate(
				[BattlerId](const FActionStartStagedHeldItem& Candidate)
				{
					return Candidate.BattlerId == BattlerId;
				});
		}

		bool HasActorVolatile(const FConditionId& VolatileId) const
		{
			return ActorVolatiles.ContainsByPredicate(
				[&VolatileId](const FBattleConditionState& Condition)
				{
					return Condition.ConditionId == VolatileId;
				});
		}
	};

	bool TryTakeActionStartTriggerOperationContext(
		FActionStartMechanicsStage& Stage,
		FBattleTriggerOperationContext& OutContext)
	{
		OutContext = FBattleTriggerOperationContext();
		if (Stage.NextTriggerReentrancyToken == 0
			|| Stage.NextTriggerReentrancyToken == TNumericLimits<uint64>::Max()
			|| !FBattleTriggerReentrancyToken::TryCreate(
				Stage.NextTriggerReentrancyToken,
				OutContext.ReentrancyToken))
		{
			return false;
		}
		++Stage.NextTriggerReentrancyToken;
		return true;
	}

	void DrainActionStartTriggerOutputs(FActionStartMechanicsStage& Stage)
	{
		TArray<FBattleTriggerEffectRequest> IgnoredRequests;
		TArray<FBattleTriggerLifecycleFact> IgnoredFacts;
		Stage.TriggerFramework.DrainEffectRequests(IgnoredRequests);
		Stage.TriggerFramework.DrainLifecycleFacts(IgnoredFacts);
	}

	bool TryStageActionStartFieldConditionDispatch(
		const FBattleEngineState& State,
		FActionStartMechanicsStage& Stage,
		const FConditionId& ConditionId,
		const EBattleTriggerPhase Phase,
		const TOptional<FActiveSlotId>& ActiveSlotId,
		bool& bOutActive)
	{
		bOutActive = false;
		if (!FBattleFieldSideConditionRules::IsFieldOwned(ConditionId))
		{
			return false;
		}
		const FBattleTriggerSubject Owner = FBattleTriggerSubject::CreateField();
		FBattleTriggerDispatchSpec Dispatch;
		Dispatch.Phase = Phase;
		for (const FBattleTriggerRegistrationState& Registration :
			Stage.TriggerFramework.GetActiveRegistrations())
		{
			if (Registration.Spec.Owner != Owner
				|| Registration.Spec.Rule.Phase != Phase
				|| Registration.Spec.SourceDefinition.Kind
					!= EBattleTriggerSourceDefinitionKind::Condition
				|| Registration.Spec.SourceDefinition.ConditionId != ConditionId
				|| FindFieldSideCondition(State, Owner, ConditionId) == nullptr)
			{
				continue;
			}
			FBattleTriggerDispatchParticipant& Participant =
				Dispatch.Participants.AddDefaulted_GetRef();
			Participant.RegistrationId = Registration.RegistrationId;
			Participant.ActiveSlotId = ActiveSlotId;
		}
		if (Dispatch.Participants.IsEmpty())
		{
			return true;
		}

		FBattleTriggerOperationContext Operation;
		EBattleTriggerError Error = EBattleTriggerError::None;
		FBattleTriggerDispatchResult Result;
		if (!TryTakeActionStartTriggerOperationContext(Stage, Operation))
		{
			return false;
		}
		Dispatch.ReentrancyToken = Operation.ReentrancyToken;
		if (!Stage.TriggerFramework.TryEnqueueDispatch(Dispatch, Error)
			|| !Stage.TriggerFramework.TryResolveNextDispatch(Result, Error))
		{
			return false;
		}
		TArray<FBattleTriggerEffectRequest> Requests;
		TArray<FBattleTriggerLifecycleFact> Facts;
		Stage.TriggerFramework.DrainEffectRequests(Requests);
		Stage.TriggerFramework.DrainLifecycleFacts(Facts);
		if (Result.bQueuedExpiryDispatch)
		{
			FBattleTriggerDispatchResult ExpiryResult;
			if (!Stage.TriggerFramework.TryResolveNextDispatch(ExpiryResult, Error))
			{
				return false;
			}
			TArray<FBattleTriggerEffectRequest> ExpiryRequests;
			TArray<FBattleTriggerLifecycleFact> ExpiryFacts;
			Stage.TriggerFramework.DrainEffectRequests(ExpiryRequests);
			Stage.TriggerFramework.DrainLifecycleFacts(ExpiryFacts);
			Requests.Append(MoveTemp(ExpiryRequests));
			Facts.Append(MoveTemp(ExpiryFacts));
		}
		bOutActive = Requests.ContainsByPredicate(
			[&ConditionId, Phase](const FBattleTriggerEffectRequest& Request)
			{
				return Request.Phase == Phase
					&& Request.SourceDefinition.Kind
						== EBattleTriggerSourceDefinitionKind::Condition
					&& Request.SourceDefinition.ConditionId == ConditionId;
			});
		return true;
	}

	bool TryStageActionStartItemCleanup(
		FActionStartMechanicsStage& Stage,
		const FItemId& ItemId,
		const FBattlerId BattlerId)
	{
		if (!FBattleItemRules::IsCanonical(ItemId))
		{
			return true;
		}
		FBattleTriggerSubject Owner;
		FBattleTriggerSourceDefinition SourceDefinition;
		FBattleTriggerOperationContext Operation;
		if (!TryMakeBattlerTriggerSubject(BattlerId, Owner)
			|| !FBattleTriggerSourceDefinition::TryCreateItem(ItemId, SourceDefinition)
			|| !TryTakeActionStartTriggerOperationContext(Stage, Operation))
		{
			return false;
		}

		FBattleTriggerCleanupRequest Cleanup;
		Cleanup.Reason = EBattleTriggerCleanupReason::Removal;
		Cleanup.AffectedOwners.Add(Owner);
		Cleanup.SourceDefinitionFilter = SourceDefinition;
		Cleanup.Context = Operation;
		EBattleTriggerError Error = EBattleTriggerError::None;
		if (!Stage.TriggerFramework.TryApplyCleanup(Cleanup, Error))
		{
			return false;
		}
		DrainActionStartTriggerOutputs(Stage);
		return true;
	}

	bool TryStageActionStartItemRegistration(
		const FBattleEngineState& State,
		FActionStartMechanicsStage& Stage,
		const FActionStartStagedHeldItem& Staged)
	{
		if (!FBattleItemRules::IsCanonical(Staged.HeldItem.CurrentItemId))
		{
			return true;
		}
		const bool bAlreadyRegistered =
			Stage.TriggerFramework.GetActiveRegistrations().ContainsByPredicate(
				[&Staged](const FBattleTriggerRegistrationState& Registration)
				{
					return Registration.Spec.Owner.Kind
							== EBattleTriggerSubjectKind::Battler
						&& Registration.Spec.Owner.BattlerId == Staged.BattlerId
						&& Registration.Spec.SourceDefinition.Kind
							== EBattleTriggerSourceDefinitionKind::Item
						&& Registration.Spec.SourceDefinition.ItemId
							== Staged.HeldItem.CurrentItemId;
				});
		if (bAlreadyRegistered
			&& !TryStageActionStartItemCleanup(
				Stage,
				Staged.HeldItem.CurrentItemId,
				Staged.BattlerId))
		{
			return false;
		}

		FBattleTriggerSubject Owner;
		if (FindActiveForBattler(State, Staged.BattlerId) == nullptr
			|| !TryMakeBattlerTriggerSubject(Staged.BattlerId, Owner))
		{
			return false;
		}
		FBattleItemRegistrationFacts Facts;
		Facts.ItemId = Staged.HeldItem.CurrentItemId;
		Facts.Owner = Owner;
		Facts.Source = Owner;
		Facts.Targets.Add(Owner);
		Facts.bSuppressed = Staged.HeldItem.bSuppressed;
		EBattleAbilityItemHookError Error = EBattleAbilityItemHookError::None;
		if (!FBattleItemRules::TryRegisterHooks(Stage.TriggerFramework, Facts, Error))
		{
			return false;
		}
		DrainActionStartTriggerOutputs(Stage);
		return true;
	}

	bool TryStageAllActionStartHeldItemsSuppressed(
		const FBattleEngineState& State,
		FActionStartMechanicsStage& Stage,
		const bool bSuppressed)
	{
		for (FActionStartStagedHeldItem& Staged : Stage.HeldItems)
		{
			const bool bActive = FindActiveForBattler(State, Staged.BattlerId) != nullptr;
			if (Staged.HeldItem.bSuppressed == bSuppressed)
			{
				continue;
			}
			if (bActive
				&& !TryStageActionStartItemCleanup(
					Stage,
					Staged.HeldItem.CurrentItemId,
					Staged.BattlerId))
			{
				return false;
			}

			FBattleHeldItemOperationRequest Request;
			Request.Kind = EBattleHeldItemOperationKind::Suppress;
			Request.PrimaryInstanceId = Staged.HeldItem.InstanceId;
			Request.bSuppressed = bSuppressed;
			FBattleHeldItemOperationFact Fact;
			EBattleHeldItemContractError Error = EBattleHeldItemContractError::None;
			if (!Stage.HeldItemLedger.TryApplyOperation(Request, Fact, Error))
			{
				return false;
			}
			Staged.HeldItem.CurrentItemId = Fact.PrimaryAfter.CurrentItemId;
			Staged.HeldItem.bConsumed = Fact.PrimaryAfter.bConsumed;
			Staged.HeldItem.bSuppressed = Fact.PrimaryAfter.bSuppressed;
			Staged.HeldItem.bRevealed = Fact.PrimaryAfter.bRevealed;
			Staged.HeldItem.bTemporarilyRemoved =
				Fact.PrimaryAfter.bTemporarilyRemoved;
			if (bSuppressed)
			{
				Staged.HeldItem.ChoiceLockedMoveId = FMoveId();
			}
			if (bActive
				&& !TryStageActionStartItemRegistration(State, Stage, Staged))
			{
				return false;
			}
		}
		return true;
	}

	bool TryStageActionStartVolatileCleanup(
		FActionStartMechanicsStage& Stage,
		const FConditionId& VolatileId)
	{
		if (!FBattleVolatileRules::IsCanonical(VolatileId))
		{
			return true;
		}
		FBattleTriggerSubject Owner;
		FBattleTriggerOperationContext Operation;
		EBattleTriggerError Error = EBattleTriggerError::None;
		if (!TryMakeBattlerTriggerSubject(Stage.ActorId, Owner)
			|| !TryTakeActionStartTriggerOperationContext(Stage, Operation)
			|| !FBattleVolatileRules::TryCleanupTriggers(
				Stage.TriggerFramework,
				VolatileId,
				Owner,
				EBattleTriggerCleanupReason::Removal,
				Operation,
				Error))
		{
			return false;
		}
		DrainActionStartTriggerOutputs(Stage);
		return true;
	}

	bool TryStageRechargeDenial(
		const FBattleEngineState& State,
		FActionStartMechanicsStage& Stage)
	{
		const FConditionId RechargeId = FBattleVolatileRules::GetRechargeId();
		if (!Stage.HasActorVolatile(RechargeId))
		{
			return false;
		}
		FBattleTriggerSubject Owner;
		if (!TryMakeBattlerTriggerSubject(Stage.ActorId, Owner))
		{
			return false;
		}

		FBattleTriggerDispatchSpec Dispatch;
		Dispatch.Phase = EBattleTriggerPhase::BeforeAction;
		for (const FBattleTriggerRegistrationState& Registration :
			Stage.TriggerFramework.GetActiveRegistrations())
		{
			if (Registration.Spec.Owner != Owner
				|| Registration.Spec.Rule.Phase != EBattleTriggerPhase::BeforeAction
				|| Registration.Spec.SourceDefinition.Kind
					!= EBattleTriggerSourceDefinitionKind::Condition
				|| Registration.Spec.SourceDefinition.ConditionId != RechargeId)
			{
				continue;
			}
			FBattleTriggerDispatchParticipant& Participant =
				Dispatch.Participants.AddDefaulted_GetRef();
			Participant.RegistrationId = Registration.RegistrationId;
			const FBattleActivePositionState* Active =
				FindActiveForBattler(State, Stage.ActorId);
			if (Active != nullptr)
			{
				Participant.ActiveSlotId = Active->ActiveSlotId;
			}
		}
		if (Dispatch.Participants.IsEmpty())
		{
			return false;
		}

		FBattleTriggerOperationContext Operation;
		FBattleTriggerDispatchResult DispatchResult;
		EBattleTriggerError TriggerError = EBattleTriggerError::None;
		if (!TryTakeActionStartTriggerOperationContext(Stage, Operation))
		{
			return false;
		}
		Dispatch.ReentrancyToken = Operation.ReentrancyToken;
		if (!Stage.TriggerFramework.TryEnqueueDispatch(Dispatch, TriggerError)
			|| !Stage.TriggerFramework.TryResolveNextDispatch(
				DispatchResult,
				TriggerError))
		{
			return false;
		}
		TArray<FBattleTriggerEffectRequest> Requests;
		TArray<FBattleTriggerLifecycleFact> Facts;
		Stage.TriggerFramework.DrainEffectRequests(Requests);
		Stage.TriggerFramework.DrainLifecycleFacts(Facts);
		if (DispatchResult.bQueuedExpiryDispatch)
		{
			FBattleTriggerDispatchResult ExpiryResult;
			if (!Stage.TriggerFramework.TryResolveNextDispatch(
					ExpiryResult,
					TriggerError))
			{
				return false;
			}
			TArray<FBattleTriggerEffectRequest> ExpiryRequests;
			TArray<FBattleTriggerLifecycleFact> ExpiryFacts;
			Stage.TriggerFramework.DrainEffectRequests(ExpiryRequests);
			Stage.TriggerFramework.DrainLifecycleFacts(ExpiryFacts);
			Requests.Append(MoveTemp(ExpiryRequests));
			Facts.Append(MoveTemp(ExpiryFacts));
		}

		FBattleVolatileActionResult Gate;
		if (Requests.Num() != 1
			|| !Facts.IsEmpty()
			|| Requests[0].SourceDefinition.Kind
				!= EBattleTriggerSourceDefinitionKind::Condition
			|| Requests[0].SourceDefinition.ConditionId != RechargeId
			|| !FBattleVolatileRules::TryResolveSimpleBeforeAction(RechargeId, Gate)
			|| Gate.Outcome != EBattleVolatileActionOutcome::Denied
			|| !Gate.bRemoveVolatile
			|| !TryStageActionStartVolatileCleanup(Stage, RechargeId)
			|| Stage.ActorVolatiles.RemoveAll(
				[RechargeId](const FBattleConditionState& Condition)
				{
					return Condition.ConditionId == RechargeId;
				}) != 1)
		{
			return false;
		}
		return true;
	}

	bool TryStageClearActionStartChargeState(FActionStartMechanicsStage& Stage)
	{
		for (const FConditionId& Id : {
			FBattleVolatileRules::GetChargingId(),
			FBattleVolatileRules::GetFlySemiInvulnerableId()})
		{
			if (!Stage.HasActorVolatile(Id))
			{
				continue;
			}
			if (!TryStageActionStartVolatileCleanup(Stage, Id))
			{
				return false;
			}
			Stage.ActorVolatiles.RemoveAll(
				[&Id](const FBattleConditionState& Condition)
				{
					return Condition.ConditionId == Id;
				});
		}
		return true;
	}

	struct FActionStartStateDelta
	{
		FTrainerId ActingTrainerId;
		FBattleTrainerActionAllowance TrainerAllowance;
		bool bApplyMechanicsStage = false;
		FActionStartMechanicsStage MechanicsStage;
		bool bStarted = false;
		bool bMoveCommitted = false;
		TOptional<FBattleTargetResolutionResult> TargetResolution;
		EBattleLockedEffectExecutionState EffectExecutionState =
			EBattleLockedEffectExecutionState::Pending;
		bool bFinished = false;
		int32 NextLockedActionIndex = INDEX_NONE;
		EBattlePhase Phase = EBattlePhase::Resolving;
		EBattleOutcome Outcome = EBattleOutcome::InProgress;
		EBattleOutcomeCause OutcomeCause = EBattleOutcomeCause::None;
		TOptional<FBattleDecisionRequest> PendingDecision;
		TArray<FBattleDecisionRequest> PendingDecisionRequests;
		TArray<FBattlePendingReplacementState> PendingReplacements;
		TArray<FBattleAllyActionPowerModifierRegistration>
			AllyActionPowerModifierRegistrations;
	};

	void InitializeActionStartDelta(
		const FBattleEngineState& State,
		const FBattleLockedActionState& Action,
		const FBattleTrainerState& Trainer,
		FActionStartStateDelta& OutDelta)
	{
		OutDelta = FActionStartStateDelta();
		OutDelta.ActingTrainerId = Trainer.TrainerId;
		OutDelta.TrainerAllowance = Trainer.ActionAllowance;
		OutDelta.bStarted = Action.bStarted;
		OutDelta.bMoveCommitted = Action.bMoveCommitted;
		OutDelta.TargetResolution = Action.TargetResolution;
		OutDelta.EffectExecutionState = Action.EffectExecutionState;
		OutDelta.bFinished = Action.bFinished;
		OutDelta.NextLockedActionIndex = State.CurrentLockedActionIndex;
		OutDelta.Phase = EBattlePhase::Resolving;
		OutDelta.Outcome = State.Outcome;
		OutDelta.OutcomeCause = State.OutcomeCause;
		OutDelta.PendingDecision = State.PendingDecision;
		OutDelta.PendingDecisionRequests = State.PendingDecisionRequests;
		OutDelta.PendingReplacements = State.PendingReplacements;
		OutDelta.AllyActionPowerModifierRegistrations =
			State.AllyActionPowerModifierRegistrations;
	}

	bool TryPrepareActionStartBoundary(
		const FBattleEngineState& State,
		const uint64 RequestStateVersion,
		FActionStartStateDelta& Delta,
		TArray<FBattleReplacementRequirement>& OutRequirements)
	{
		OutRequirements.Reset();
		if (RequestStateVersion == 0
			|| Delta.NextLockedActionIndex <= State.CurrentLockedActionIndex
			|| Delta.NextLockedActionIndex > State.LockedActions.Num())
		{
			return false;
		}

		FBattleEngineState Projection;
		Projection.Setup = State.Setup;
		Projection.Catalog = State.Catalog;
		Projection.bHasCatalog = State.bHasCatalog;
		Projection.StateVersion = State.StateVersion;
		Projection.TurnId = State.TurnId;
		Projection.EncounterKind = State.EncounterKind;
		Projection.Format = State.Format;
		Projection.Phase = EBattlePhase::Resolving;
		Projection.Outcome = Delta.Outcome;
		Projection.OutcomeCause = Delta.OutcomeCause;
		Projection.Trainers = State.Trainers;
		Projection.Battlers = State.Battlers;
		Projection.ActivePositions = State.ActivePositions;
		Projection.CompiledEncounterPolicies = State.CompiledEncounterPolicies;
		Projection.LockedActions = State.LockedActions;
		Projection.CurrentLockedActionIndex = Delta.NextLockedActionIndex;
		Projection.PendingDecision = Delta.PendingDecision;
		Projection.PendingDecisionRequests = Delta.PendingDecisionRequests;
		Projection.PendingReplacements = Delta.PendingReplacements;

		FBattleFaintOutcomeResolver::ResolveQueueBoundary(
			Projection,
			OutRequirements);
		Delta.Phase = Projection.Phase;
		Delta.Outcome = Projection.Outcome;
		Delta.OutcomeCause = Projection.OutcomeCause;
		if (Projection.Phase == EBattlePhase::MandatoryReplacement)
		{
			Projection.PendingReplacements.Reset();
			for (const FBattleReplacementRequirement& Requirement : OutRequirements)
			{
				FBattlePendingReplacementState& Pending =
					Projection.PendingReplacements.AddDefaulted_GetRef();
				Pending.TrainerId = Requirement.Target.TrainerId;
				Pending.ActiveSlotId = Requirement.Target.ActiveSlotId;
			}
			TArray<FBattleDecisionRequest> Requests;
			if (!TryBuildReplacementCheckpointRequests(
					Projection,
					RequestStateVersion,
					true,
					Requests)
				|| Requests.IsEmpty())
			{
				return false;
			}
			Delta.PendingReplacements = Projection.PendingReplacements;
			Delta.PendingDecisionRequests = MoveTemp(Requests);
			Delta.PendingDecision = Delta.PendingDecisionRequests[0];
		}
		else if (Projection.Phase == EBattlePhase::EndOfTurn)
		{
			Delta.PendingReplacements.Reset();
			Delta.PendingDecisionRequests.Reset();
			Delta.PendingDecision.Reset();
		}
		else if (Projection.Phase != EBattlePhase::Resolving)
		{
			return false;
		}
		return true;
	}

	FBattleEventSpec MakeStagedActionStartEventSpec(
		const FBattleEngineState& State,
		const FResolutionId ResolutionId,
		const FActionId ActionId,
		const EBattleActionKind ActionKind,
		const FBattleEventSource& Source,
		const EBattleEventType Type,
		const EBattleEventCause Cause,
		const TOptional<int64> NumericBefore = TOptional<int64>(),
		const TOptional<int64> NumericAfter = TOptional<int64>(),
		const TOptional<int64> NumericDelta = TOptional<int64>(),
		const EBattleVisibilityLevel Visibility = EBattleVisibilityLevel::Public,
		const FBattleEventTarget* Target = nullptr)
	{
		FBattleEventSpec Spec;
		Spec.BattleId = State.Setup.GetBattleId();
		Spec.TurnId = State.TurnId;
		Spec.ActionId = ActionId;
		Spec.ResolutionId = ResolutionId;
		Spec.Type = Type;
		Spec.Cause = Cause;
		Spec.CauseActionKind = ActionKind;
		Spec.Source = Source;
		Spec.NumericBefore = NumericBefore;
		Spec.NumericAfter = NumericAfter;
		Spec.NumericDelta = NumericDelta;
		Spec.Visibility.Level = Visibility;
		if (Target != nullptr)
		{
			Spec.Targets.Add(*Target);
		}
		return Spec;
	}

	FBattleResolution PublishActionStartCheckpointRejection(
		FBattleEngineState& State,
		const FResolutionId ResolutionId,
		const FActionId ActionId,
		const EBattleRejectionReason Reason,
		const FTrainerId TrainerId,
		const FBattlerId BattlerId,
		const EBattleActionKind ActionKind,
		const FBattleEventSource& Source)
	{
		FBattleResolutionCommitPlan RejectedPlan;
		const bool bPrepared = FBattleResolutionCommit::TryBuildRejectedPlan(
			State,
			ResolutionId,
			ActionId,
			Reason,
			TrainerId,
			BattlerId,
			ActionKind,
			Source,
			RejectedPlan);
		check(bPrepared);
		const FBattleResolution Resolution = FBattleResolutionCommit::PublishPrepared(
			State,
			RejectedPlan);
		EBattleStateValidationError StateError = EBattleStateValidationError::None;
		const bool bStateValid = State.ValidateInvariants(StateError);
		check(bStateValid);
		return Resolution;
	}

	void ApplyActionStartDelta(
		FBattleEngineState& State,
		const FActionStartCheckpointIdentity& Identity,
		const FActionStartStateDelta& Delta)
	{
		const int32 ActionIndex = Identity.CommitIdentity.ExpectedLockedActionIndex;
		check(State.LockedActions.IsValidIndex(ActionIndex));
		FBattleLockedActionState& Action = State.LockedActions[ActionIndex];
		FBattleTrainerState* Trainer = State.FindMutableTrainer(Delta.ActingTrainerId);
		FBattleBattlerState* Actor = Delta.bApplyMechanicsStage
			? State.FindMutableBattler(Delta.MechanicsStage.ActorId)
			: nullptr;
		check(Action.ActionId == Identity.CommitIdentity.OwningActionId
			&& Trainer != nullptr
			&& (!Delta.bApplyMechanicsStage || Actor != nullptr));
		if (Delta.bApplyMechanicsStage)
		{
			for (const FActionStartStagedHeldItem& Staged :
				Delta.MechanicsStage.HeldItems)
			{
				const FBattleBattlerState* Battler = State.FindBattler(Staged.BattlerId);
				check(Battler != nullptr
					&& Battler->HeldItem.InstanceId == Staged.HeldItem.InstanceId);
			}
		}

		Trainer->ActionAllowance = Delta.TrainerAllowance;
		if (Delta.bApplyMechanicsStage)
		{
			State.TriggerFramework = Delta.MechanicsStage.TriggerFramework;
			State.NextTriggerReentrancyToken =
				Delta.MechanicsStage.NextTriggerReentrancyToken;
			State.HeldItemLedger = Delta.MechanicsStage.HeldItemLedger;
			for (const FActionStartStagedHeldItem& Staged :
				Delta.MechanicsStage.HeldItems)
			{
				FBattleBattlerState* Battler = State.FindMutableBattler(Staged.BattlerId);
				check(Battler != nullptr);
				Battler->HeldItem = Staged.HeldItem;
			}
			check(Actor != nullptr);
			Actor->Volatiles = Delta.MechanicsStage.ActorVolatiles;
		}
		Action.bStarted = Delta.bStarted;
		Action.bMoveCommitted = Delta.bMoveCommitted;
		Action.TargetResolution = Delta.TargetResolution;
		Action.EffectExecutionState = Delta.EffectExecutionState;
		Action.bFinished = Delta.bFinished;
		State.CurrentLockedActionIndex = Delta.NextLockedActionIndex;
		State.Phase = Delta.Phase;
		State.Outcome = Delta.Outcome;
		State.OutcomeCause = Delta.OutcomeCause;
		State.PendingDecision = Delta.PendingDecision;
		State.PendingDecisionRequests = Delta.PendingDecisionRequests;
		State.PendingReplacements = Delta.PendingReplacements;
		State.AllyActionPowerModifierRegistrations =
			Delta.AllyActionPowerModifierRegistrations;
	}
}

using namespace BattleEngineActionStartPrivate;

FBattleResolution FBattleEngine::BeginNextLockedAction()
{
	check(State.IsValid());
	const FResolutionId ResolutionId = TakeResolutionId(*State);
	const FBattleLockedActionState* ExistingAction = State->LockedActions.IsValidIndex(
		State->CurrentLockedActionIndex)
		? &State->LockedActions[State->CurrentLockedActionIndex]
		: nullptr;
	const FBattleEventSource FallbackSource = ExistingAction != nullptr
		? SourceFromLockedAction(*State, *ExistingAction)
		: FindFallbackSource(*State);
	const EBattleActionKind ActionKind = ExistingAction != nullptr
		? ExistingAction->Decision.GetActionKind()
		: EBattleActionKind::Fight;

	FBattleRejection Rejection;
	if (State->Phase == EBattlePhase::Terminal)
	{
		Rejection.Reason = EBattleRejectionReason::TerminalState;
	}
	else if (State->Battlers.ContainsByPredicate(
		[](const FBattleBattlerState& Candidate)
		{
			return Candidate.bFaintTransitionPending;
		}))
	{
		// C05B leaves zero-HP battlers pending for C05C. Do not allow a later
		// locked action to bypass that checkpoint before C05C owns the transition.
		Rejection.Reason = EBattleRejectionReason::IllegalAction;
	}
	else if ((State->Phase != EBattlePhase::Locked && State->Phase != EBattlePhase::Resolving)
		|| ExistingAction == nullptr
		|| ExistingAction->bFinished)
	{
		Rejection.Reason = EBattleRejectionReason::IllegalAction;
	}
	else if (ExistingAction->bStarted)
	{
		Rejection.Reason = EBattleRejectionReason::IllegalAction;
		Rejection.ActionId = ExistingAction->ActionId;
	}

	const FBattleBattlerState* Battler = ExistingAction != nullptr
		? State->FindBattler(ExistingAction->Decision.GetActingBattlerId())
		: nullptr;
	const FBattleTrainerState* Trainer = Battler != nullptr
		? State->FindTrainer(Battler->TrainerId)
		: nullptr;
	if (!Rejection.IsRejected()
		&& (Trainer == nullptr || Trainer->ActionAllowance.RemainingActions <= 0))
	{
		Rejection.Reason = EBattleRejectionReason::IllegalAction;
		if (Trainer != nullptr)
		{
			Rejection.TrainerId = Trainer->TrainerId;
		}
	}

	if (Rejection.IsRejected())
	{
		return MakeRejectedResolution(
			*State,
			ResolutionId,
			Rejection,
			EBattleEventType::ActionCanceled,
			EBattleEventCause::Action,
			ActionKind,
			FallbackSource);
	}

	check(ExistingAction != nullptr && Battler != nullptr && Trainer != nullptr);
	const FActionId ActionId = ExistingAction->ActionId;
	const FTrainerId ActingTrainerId = Trainer->TrainerId;
	const FBattlerId ActingBattlerId = Battler->BattlerId;
	const FBattleEventSource Source = SourceFromLockedAction(*State, *ExistingAction);
	const bool bChargedFight = ExistingAction->Decision.GetActionKind()
			== EBattleActionKind::Fight
		&& IsReleasingCharge(
			*State,
			*Battler,
			ExistingAction->Decision.GetMoveId());
	const FBattleActivePositionState* Active = State->FindActivePosition(
		ExistingAction->OrderKey.ActingSlotId);
	const FBattleBattlerState* SelectedTarget = ExistingAction->SelectedTargetBattlerId.IsValid()
		? State->FindBattler(ExistingAction->SelectedTargetBattlerId)
		: nullptr;

	FBattleActionStartFacts Facts;
	Facts.ActionKind = ExistingAction->Decision.GetActionKind();
	Facts.bActorActive = Active != nullptr
		&& Active->BattlerId == Battler->BattlerId;
	Facts.bActorLiving = IsLivingSelectableBattler(Battler);
	Facts.bSelectedTargetCaptured = Facts.ActionKind == EBattleActionKind::Fight
		&& SelectedTarget != nullptr
		&& SelectedTarget->bCaptured;
	Facts.bSubjectToPlayerObedience = Facts.ActionKind == EBattleActionKind::Fight
		&& Trainer->Role == EBattleTrainerRole::Player
		&& Trainer->Controller == EBattleDecisionController::Human
		&& Battler->Obedience.bHasSnapshot
		&& Battler->Obedience.bSubjectToPlayerCap;
	if (Facts.bSubjectToPlayerObedience)
	{
		Facts.ObedienceReferenceLevel = Battler->Obedience.ReferenceLevel;
		Facts.BadgeCount = Battler->Obedience.BadgeCount;
	}

	FActionStartCheckpointIdentity CheckpointIdentity;
	if (!TryCaptureActionStartCheckpointIdentity(
			*State,
			ResolutionId,
			*ExistingAction,
			CheckpointIdentity))
	{
		return PublishActionStartCheckpointRejection(
			*State,
			ResolutionId,
			ActionId,
			EBattleRejectionReason::CheckpointPreparationFailed,
			ActingTrainerId,
			ActingBattlerId,
			ActionKind,
			Source);
	}

	auto RejectPreparedCheckpoint = [&](const EBattleRejectionReason Reason)
	{
		return PublishActionStartCheckpointRejection(
			*State,
			ResolutionId,
			ActionId,
			Reason,
			ActingTrainerId,
			ActingBattlerId,
			ActionKind,
			Source);
	};

	FBattleActionStartResult StartResult;
	if (!FBattleActionStartRules::TryEvaluate(Facts, StartResult))
	{
		Rejection.Reason = EBattleRejectionReason::InvalidDecision;
		return MakeRejectedResolution(
			*State,
			ResolutionId,
			Rejection,
			EBattleEventType::ActionCanceled,
			EBattleEventCause::Action,
			ActionKind,
			FallbackSource);
	}
	if (!IsActionStartCheckpointIdentityCurrent(*State, CheckpointIdentity))
	{
		return RejectPreparedCheckpoint(
			EBattleRejectionReason::StaleCheckpointIdentity);
	}

	FActionStartStateDelta Delta;
	InitializeActionStartDelta(*State, *ExistingAction, *Trainer, Delta);
	if (Delta.TrainerAllowance.RemainingActions <= 0)
	{
		return RejectPreparedCheckpoint(
			EBattleRejectionReason::CheckpointPreparationFailed);
	}
	--Delta.TrainerAllowance.RemainingActions;
	Delta.MechanicsStage.Capture(*State, ActingBattlerId);

	bool bRechargeDeniedAction = false;
	if (StartResult.Outcome == EBattleActionStartOutcome::Proceed)
	{
		Delta.bApplyMechanicsStage = true;
		bool bMagicRoomTriggerActive = false;
		if (!TryStageActionStartFieldConditionDispatch(
				*State,
				Delta.MechanicsStage,
				FBattleFieldSideConditionRules::GetMagicRoomId(),
				EBattleTriggerPhase::BeforeAction,
				Active != nullptr
					? TOptional<FActiveSlotId>(Active->ActiveSlotId)
					: TOptional<FActiveSlotId>(),
				bMagicRoomTriggerActive))
		{
			return RejectPreparedCheckpoint(
				EBattleRejectionReason::CheckpointPreparationFailed);
		}
		if (!IsActionStartCheckpointIdentityCurrent(*State, CheckpointIdentity))
		{
			return RejectPreparedCheckpoint(
				EBattleRejectionReason::StaleCheckpointIdentity);
		}
		if (!TryStageAllActionStartHeldItemsSuppressed(
				*State,
				Delta.MechanicsStage,
				bMagicRoomTriggerActive))
		{
			return RejectPreparedCheckpoint(
				EBattleRejectionReason::CheckpointPreparationFailed);
		}
		if (!IsActionStartCheckpointIdentityCurrent(*State, CheckpointIdentity))
		{
			return RejectPreparedCheckpoint(
				EBattleRejectionReason::StaleCheckpointIdentity);
		}
		if (HasVolatile(*Battler, FBattleVolatileRules::GetRechargeId()))
		{
			if (!TryStageRechargeDenial(*State, Delta.MechanicsStage))
			{
				return RejectPreparedCheckpoint(
					EBattleRejectionReason::CheckpointPreparationFailed);
			}
			if (!IsActionStartCheckpointIdentityCurrent(*State, CheckpointIdentity))
			{
				return RejectPreparedCheckpoint(
					EBattleRejectionReason::StaleCheckpointIdentity);
			}
			bRechargeDeniedAction = true;
		}
	}

	const bool bNeedsChargeCleanup = bChargedFight
		&& (bRechargeDeniedAction
			|| StartResult.Outcome != EBattleActionStartOutcome::Proceed);
	if (bNeedsChargeCleanup)
	{
		Delta.bApplyMechanicsStage = true;
		if (!TryStageClearActionStartChargeState(Delta.MechanicsStage))
		{
			return RejectPreparedCheckpoint(
				EBattleRejectionReason::CheckpointPreparationFailed);
		}
		if (!IsActionStartCheckpointIdentityCurrent(*State, CheckpointIdentity))
		{
			return RejectPreparedCheckpoint(
				EBattleRejectionReason::StaleCheckpointIdentity);
		}
	}

	bool bCompletesAction = false;
	if (bRechargeDeniedAction)
	{
		Delta.bStarted = true;
		Delta.bFinished = true;
		bCompletesAction = true;
	}
	else if (StartResult.Outcome == EBattleActionStartOutcome::Proceed)
	{
		Delta.bStarted = true;
	}
	else if (StartResult.Outcome == EBattleActionStartOutcome::ObedienceRefused)
	{
		Delta.bStarted = true;
		Delta.bFinished = true;
		bCompletesAction = true;
	}
	else
	{
		Delta.bFinished = true;
		bCompletesAction = true;
	}

	TArray<FBattleReplacementRequirement> ReplacementRequirements;
	if (bCompletesAction)
	{
		FBattleAllyActionPowerModifier::RemoveForAction(
			Delta.AllyActionPowerModifierRegistrations,
			ActionId);
		Delta.NextLockedActionIndex = State->CurrentLockedActionIndex + 1;
		if (CheckpointIdentity.CommitIdentity.ExpectedStateVersion
				== TNumericLimits<uint64>::Max()
			|| !TryPrepareActionStartBoundary(
				*State,
				CheckpointIdentity.CommitIdentity.ExpectedStateVersion + 1,
				Delta,
				ReplacementRequirements))
		{
			return RejectPreparedCheckpoint(
				EBattleRejectionReason::CheckpointPreparationFailed);
		}
		if (!IsActionStartCheckpointIdentityCurrent(*State, CheckpointIdentity))
		{
			return RejectPreparedCheckpoint(
				EBattleRejectionReason::StaleCheckpointIdentity);
		}
	}

	FBattleResolutionCommitPlan CommitPlan;
	if (!FBattleResolutionCommit::TryBeginAcceptedPlan(
			CheckpointIdentity.CommitIdentity,
			CommitPlan))
	{
		return RejectPreparedCheckpoint(
			EBattleRejectionReason::CheckpointPreparationFailed);
	}
	if (!IsActionStartCheckpointIdentityCurrent(*State, CheckpointIdentity))
	{
		return RejectPreparedCheckpoint(
			EBattleRejectionReason::StaleCheckpointIdentity);
	}

	EBattleRejectionReason StageFailureReason = EBattleRejectionReason::None;
	auto StageEvent = [&](const EBattleEventType Type,
		const EBattleEventCause Cause,
		const TOptional<int64> NumericBefore = TOptional<int64>(),
		const TOptional<int64> NumericAfter = TOptional<int64>(),
		const TOptional<int64> NumericDelta = TOptional<int64>(),
		const EBattleVisibilityLevel Visibility = EBattleVisibilityLevel::Public,
		const FBattleEventTarget* Target = nullptr)
	{
		if (!FBattleResolutionCommit::TryStageEvent(
				CommitPlan,
				MakeStagedActionStartEventSpec(
					*State,
					ResolutionId,
					ActionId,
					ActionKind,
					Source,
					Type,
					Cause,
					NumericBefore,
					NumericAfter,
					NumericDelta,
					Visibility,
					Target)))
		{
			StageFailureReason = EBattleRejectionReason::CheckpointPreparationFailed;
			return false;
		}
		if (!IsActionStartCheckpointIdentityCurrent(*State, CheckpointIdentity))
		{
			StageFailureReason = EBattleRejectionReason::StaleCheckpointIdentity;
			return false;
		}
		return true;
	};

	bool bEventsStaged = true;
	if (bRechargeDeniedAction)
	{
		bEventsStaged = StageEvent(
				EBattleEventType::ActionStarted,
				EBattleEventCause::Action)
			&& StageEvent(
				EBattleEventType::StatusChanged,
				EBattleEventCause::Rule,
				static_cast<int64>(1),
				static_cast<int64>(0),
				static_cast<int64>(-1))
			&& StageEvent(
				EBattleEventType::EffectPrevented,
				EBattleEventCause::Rule)
			&& StageEvent(
				EBattleEventType::ActionCanceled,
				EBattleEventCause::Rule)
			&& StageEvent(
				EBattleEventType::ActionCompleted,
				EBattleEventCause::Action);
	}
	else if (StartResult.Outcome == EBattleActionStartOutcome::Proceed)
	{
		bEventsStaged = StageEvent(
			EBattleEventType::ActionStarted,
			EBattleEventCause::Action);
		if (bEventsStaged && StartResult.ObedienceCap.IsSet())
		{
			bEventsStaged = StageEvent(
				EBattleEventType::ObedienceConfirmed,
				EBattleEventCause::Rule,
				static_cast<int64>(Facts.ObedienceReferenceLevel),
				static_cast<int64>(StartResult.ObedienceCap.GetValue()),
				static_cast<int64>(Facts.ObedienceReferenceLevel)
					- static_cast<int64>(StartResult.ObedienceCap.GetValue()),
				EBattleVisibilityLevel::CoreOnly);
		}
	}
	else if (StartResult.Outcome == EBattleActionStartOutcome::ObedienceRefused)
	{
		bEventsStaged = StageEvent(
				EBattleEventType::ActionStarted,
				EBattleEventCause::Action)
			&& StageEvent(
				EBattleEventType::ObedienceRefused,
				EBattleEventCause::Rule,
				static_cast<int64>(Facts.ObedienceReferenceLevel),
				static_cast<int64>(StartResult.ObedienceCap.GetValue()),
				static_cast<int64>(Facts.ObedienceReferenceLevel)
					- static_cast<int64>(StartResult.ObedienceCap.GetValue()))
			&& StageEvent(
				EBattleEventType::ActionCompleted,
				EBattleEventCause::Action);
	}
	else
	{
		bEventsStaged = StageEvent(
				EBattleEventType::ActionCanceled,
				EBattleEventCause::Action)
			&& StageEvent(
				EBattleEventType::ActionCompleted,
				EBattleEventCause::Action);
	}
	for (const FBattleReplacementRequirement& Requirement : ReplacementRequirements)
	{
		if (!bEventsStaged)
		{
			break;
		}
		bEventsStaged = StageEvent(
			EBattleEventType::ReplacementRequired,
			EBattleEventCause::Rule,
			TOptional<int64>(),
			TOptional<int64>(),
			TOptional<int64>(),
			EBattleVisibilityLevel::Public,
			&Requirement.Target);
	}
	if (!bEventsStaged)
	{
		check(StageFailureReason != EBattleRejectionReason::None);
		return RejectPreparedCheckpoint(StageFailureReason);
	}
	if (!FBattleResolutionCommit::TryFinishAcceptedPlan(CommitPlan))
	{
		return RejectPreparedCheckpoint(
			EBattleRejectionReason::CheckpointPreparationFailed);
	}
	if (!IsActionStartCheckpointIdentityCurrent(*State, CheckpointIdentity))
	{
		return RejectPreparedCheckpoint(
			EBattleRejectionReason::StaleCheckpointIdentity);
	}

	ApplyActionStartDelta(*State, CheckpointIdentity, Delta);
	const FBattleResolution Resolution = FBattleResolutionCommit::PublishPrepared(
		*State,
		CommitPlan);
	return Resolution;
}
