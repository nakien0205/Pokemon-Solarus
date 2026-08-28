#include "BattleEffectExecutorContext.h"

#include "Battle/BattleAbility.h"
#include "BattleEntryHazardPrevention.h"
#include "Battle/BattleFieldSideConditions.h"
#include "Battle/BattleItem.h"
#include "Battle/BattleMajorStatus.h"
#include "Battle/BattleState.h"
#include "Battle/BattleVolatile.h"
#include "BattleMoveRedirection.h"
#include "Math/NumericLimits.h"

namespace BattleEffectExecutorPrivate
{
	bool FStateExecutionContext::TryResolveForcedSwitches(
		FBattleEffectExecutionResult& Result,
		EBattleEffectExecutorError& OutError)
	{
		for (FBattleSwitchEffectIntent& Intent : Result.SwitchIntents)
		{
			if (Intent.Kind != EBattleSwitchKind::Forced)
			{
				continue;
			}
			if (Intent.Target.GetKind() != EBattleResolvedTargetKind::Battler)
			{
				OutError = EBattleEffectExecutorError::InvalidTarget;
				return false;
			}

			const FBattleBattlerTarget& ForcedTarget = Intent.Target.GetBattler();
			FBattleActivePositionState* Active = FindMutableActivePosition(
				ForcedTarget.ActiveSlotId);
			FBattleBattlerState* Outgoing = FindMutableBattler(ForcedTarget.BattlerId);
			if (Active == nullptr
				|| Outgoing == nullptr
				|| !Active->bAvailable
				|| Active->BattlerId != Outgoing->BattlerId
				|| Outgoing->CurrentHP <= 0
				|| Outgoing->bFainted
				|| Outgoing->bCaptured
				|| Outgoing->bRemoved)
			{
				Intent.BlockReason = EBattleSwitchBlockReason::ActingBattlerUnavailable;
				continue;
			}

			const FBattleTrainerState* Trainer = State.FindTrainer(Outgoing->TrainerId);
			if (Trainer == nullptr || Active->TrainerId != Trainer->TrainerId)
			{
				OutError = EBattleEffectExecutorError::InvalidTarget;
				return false;
			}

			FBattleSwitchLegalitySpec LegalitySpec;
			LegalitySpec.Kind = EBattleSwitchKind::Forced;
			LegalitySpec.ActingTrainerId = Trainer->TrainerId;
			LegalitySpec.ActingBattlerId = Outgoing->BattlerId;
			LegalitySpec.ActiveSlotId = Active->ActiveSlotId;
			LegalitySpec.TransferPolicy = EBattleSwitchStateTransferPolicy::ClearTransient;
			for (const FBattlePartySlotState& PartySlot : Trainer->PartySlots)
			{
				FBattleSwitchCandidateFacts Candidate;
				Candidate.PartySlotId = PartySlot.PartySlotId;
				Candidate.bOccupied = PartySlot.BattlerId.IsValid();
				if (Candidate.bOccupied)
				{
					const FBattleBattlerState* Battler = FindBattler(PartySlot.BattlerId);
					if (Battler == nullptr)
					{
						OutError = EBattleEffectExecutorError::InvalidTarget;
						return false;
					}
					Candidate.TrainerId = Battler->TrainerId;
					Candidate.BattlerId = Battler->BattlerId;
					Candidate.bAlreadyActive = FindActiveForBattler(Battler->BattlerId) != nullptr;
					Candidate.bFainted = Battler->CurrentHP <= 0 || Battler->bFainted;
					Candidate.bEgg = Battler->bEgg;
					Candidate.bCaptured = Battler->bCaptured;
					Candidate.bRemoved = Battler->bRemoved;
				}
				LegalitySpec.Candidates.Add(MoveTemp(Candidate));
			}

			FBattleSwitchLegalityResult Legality;
			if (!FBattleSwitchResolver::TryBuildLegality(LegalitySpec, Legality))
			{
				OutError = EBattleEffectExecutorError::InvalidTarget;
				return false;
			}
			FBattleSwitchSelectionSpec SelectionSpec;
			SelectionSpec.RandomContext.BattleId = Request.BattleId;
			SelectionSpec.RandomContext.TurnId = Request.TurnId;
			SelectionSpec.RandomContext.ActionId = Request.ActionId;
			SelectionSpec.RandomContext.ResolutionId = Request.ResolutionId;
			SelectionSpec.RandomContext.RulePurpose =
				FBattleSwitchResolver::GetForcedSelectionRulePurpose();
			FBattleSwitchResolution Resolution;
			if (!FBattleSwitchResolver::TryResolve(
				Legality,
				SelectionSpec,
				Random,
				Resolution))
			{
				OutError = EBattleEffectExecutorError::RandomFailure;
				return false;
			}
			Intent.BlockReason = Resolution.GetReason();
			if (!Resolution.HasSelection())
			{
				continue;
			}

			FBattleBattlerState* Incoming = FindMutableBattler(
				Resolution.GetSelectedBattlerId());
			if (Incoming == nullptr || Incoming->TrainerId != Trainer->TrainerId)
			{
				OutError = EBattleEffectExecutorError::InvalidTarget;
				return false;
			}
			Intent.OutgoingTarget.TrainerId = Outgoing->TrainerId;
			Intent.OutgoingTarget.BattlerId = Outgoing->BattlerId;
			Intent.OutgoingTarget.ActiveSlotId = Active->ActiveSlotId;
			Intent.IncomingTarget.TrainerId = Incoming->TrainerId;
			Intent.IncomingTarget.BattlerId = Incoming->BattlerId;
			Intent.IncomingTarget.ActiveSlotId = Active->ActiveSlotId;
			Intent.SelectedPartySlotId = Resolution.GetSelectedPartySlotId();
			Intent.IncomingBattlerId = Incoming->BattlerId;

			if (!TryRunSwitchOutStatus(*Outgoing))
			{
				OutError = EBattleEffectExecutorError::InvalidHookResult;
				return false;
			}
			if (!TryCleanupAllOwnedVolatiles(
					*Outgoing,
					EBattleTriggerCleanupReason::Switch)
				|| !TryCleanupSourceDependentVolatiles(
					Outgoing->BattlerId,
					EBattleTriggerCleanupReason::Removal)
				|| !TryCleanupAbilityHooks(
					*Outgoing,
					EBattleTriggerCleanupReason::Switch)
				|| !TryCleanupItemHooks(
					*Outgoing,
					Outgoing->HeldItem.CurrentItemId,
					EBattleTriggerCleanupReason::Switch))
			{
				OutError = EBattleEffectExecutorError::InvalidHookResult;
				return false;
			}
			Outgoing->Stages = FBattleStatStages();
			Outgoing->Volatiles.Reset();
			Outgoing->LastMoveId = FMoveId();
			Outgoing->HeldItem.ChoiceLockedMoveId = FMoveId();
			Outgoing->bAbilitySuppressed = false;
			Outgoing->EnteredActiveOnTurnId = FTurnId();
			FBattleMoveRedirection::RemoveForOccupant(
				MoveRedirectionRegistrations,
				ForcedTarget);
			Active->BattlerId = Incoming->BattlerId;
			Incoming->bAbilitySuppressed = false;
			Incoming->EnteredActiveOnTurnId = Request.TurnId;
			if (!TryRegisterAbilityHooks(*Incoming, *Active)
				|| !TryResolveHeldItemSwitchIn(*Incoming, *Active)
				|| !TryApplyEntryHazards(*Incoming, *Active))
			{
				OutError = EBattleEffectExecutorError::InvalidHookResult;
				return false;
			}
			Intent.bApplied = true;
		}
		return true;
	}

	bool FStateExecutionContext::TryApplyEntryHazards(
		FBattleBattlerState& Incoming,
		const FBattleActivePositionState& Active)
	{
		if (ExecutionResult == nullptr)
		{
			return false;
		}
		FBattleSideState* Side = FindMutableSide(Active.ActiveSlotId.GetSide());
		const FBattleSpeciesFormDefinition* Species = State.Catalog.FindSpeciesForm(
			Incoming.SpeciesFormId);
		if (Side == nullptr || Species == nullptr)
		{
			return false;
		}
		FBattleTriggerSubject SideOwner;
		TArray<FBattleTriggerEffectRequest> HazardRequests;
		if (!FBattleTriggerSubject::TryCreateSide(Active.ActiveSlotId.GetSide(), SideOwner)
			|| !TryDispatchFieldSidePhase(
				SideOwner,
				EBattleTriggerPhase::SwitchIn,
				FConditionId(),
				Active.ActiveSlotId,
				HazardRequests))
		{
			return false;
		}
		for (const FBattleTriggerEffectRequest& HazardRequest : HazardRequests)
		{
			if (Incoming.CurrentHP <= 0 || Incoming.bFainted)
			{
				break;
			}
			if (HazardRequest.Phase != EBattleTriggerPhase::SwitchIn
				|| HazardRequest.SourceDefinition.Kind
					!= EBattleTriggerSourceDefinitionKind::Condition)
			{
				return false;
			}
			const FConditionId HazardId = HazardRequest.SourceDefinition.ConditionId;
			const FBattleConditionState* CurrentHazard = Side->Hazards.FindByPredicate(
				[&HazardId](const FBattleConditionState& Condition)
				{
					return Condition.ConditionId == HazardId;
				});
			if (CurrentHazard == nullptr
				|| !FBattleFieldSideConditionRules::IsCanonical(HazardId))
			{
				continue;
			}

			bool bGrounded = false;
			bool bLevitateMadeAirborne = false;
			if (!TryIsGrounded(
					Incoming,
					bGrounded,
					false,
					&bLevitateMadeAirborne))
			{
				return false;
			}
			FBattleTypeEffectiveness RockEffectiveness{1, 1};
			if (HazardId == FBattleFieldSideConditionRules::GetStealthRockId())
			{
				const bool bTypeFound = Species->SecondaryType == EPokemonType::Invalid
					? State.Catalog.GetTypeChart().TryGetEffectiveness(
						EPokemonType::Rock,
						Species->PrimaryType,
						RockEffectiveness)
					: State.Catalog.GetTypeChart().TryGetDualEffectiveness(
						EPokemonType::Rock,
						Species->PrimaryType,
						Species->SecondaryType,
						RockEffectiveness);
				if (!bTypeFound)
				{
					return false;
				}
			}

			const FConditionId HazardStatusId = HazardRequest.Layers >= 2
				? FBattleMajorStatusRules::GetToxicId()
				: FBattleMajorStatusRules::GetPoisonId();
			const FBattleBattlerState* SourceBattler = FindBattler(
				HazardRequest.Source.BattlerId);
			const FBattleTrainerState* SourceTrainer = SourceBattler != nullptr
				? State.FindTrainer(SourceBattler->TrainerId)
				: nullptr;
			const bool bHazardAppliedByOpponent = SourceTrainer == nullptr
				|| SourceTrainer->Side != Active.ActiveSlotId.GetSide();
			FBattleMajorStatusApplicationFacts StatusFacts;
			StatusFacts.RequestedStatusId = HazardStatusId;
			StatusFacts.ExistingMajorStatusId = Incoming.MajorStatusId;
			StatusFacts.PrimaryType = Species->PrimaryType;
			StatusFacts.SecondaryType = Species->SecondaryType;
			const FConditionId TerrainId = GetTerrainId();
			bool bTerrainTriggerActive = false;
			if (FBattleFieldSideConditionRules::IsCanonical(TerrainId)
				&& !TryIsFieldSideConditionActiveForPhase(
					TerrainId,
					TOptional<EBattleSide>(),
					EBattleTriggerPhase::BeforeHit,
					Active.ActiveSlotId,
					bTerrainTriggerActive))
			{
				return false;
			}
			StatusFacts.Prevention.bTerrainPrevents =
				bTerrainTriggerActive
				&& FBattleFieldSideConditionRules::ShouldTerrainPreventMajorStatus(
					TerrainId,
					HazardStatusId,
					bGrounded);
			bool bSafeguardTriggerActive = false;
			if (!TryIsFieldSideConditionActiveForPhase(
					FBattleFieldSideConditionRules::GetSafeguardId(),
					Active.ActiveSlotId.GetSide(),
					EBattleTriggerPhase::BeforeHit,
					Active.ActiveSlotId,
					bSafeguardTriggerActive))
			{
				return false;
			}
			StatusFacts.Prevention.bSafeguardPrevents =
				FBattleFieldSideConditionRules::ShouldSafeguardPrevent(
					bSafeguardTriggerActive,
					bHazardAppliedByOpponent,
					false);
			FBattleMajorStatusApplicationResult StatusApplication;
			if (!FBattleMajorStatusRules::TryEvaluateApplication(
				StatusFacts,
				StatusApplication))
			{
				return false;
			}

			FBattleHazardSwitchInFacts Facts;
			Facts.HazardId = HazardId;
			Facts.Layers = HazardRequest.Layers;
			Facts.BaseMaximumHP = Incoming.PermanentStats.MaxHP;
			Facts.CurrentHP = Incoming.CurrentHP;
			Facts.PrimaryType = Species->PrimaryType;
			Facts.SecondaryType = Species->SecondaryType;
			Facts.bGrounded = bGrounded;
			const bool bBootsBypassActive = !Incoming.HeldItem.bConsumed
				&& !Incoming.HeldItem.bTemporarilyRemoved
				&& FBattleItemRules::ShouldBypassEntryHazards(
					Incoming.HeldItem.CurrentItemId,
					Incoming.HeldItem.bSuppressed);
			Facts.bMajorStatusPrevented = StatusApplication.Outcome
				!= EBattleMajorStatusApplicationOutcome::CanApply;
			bool bMistTriggerActive = false;
			if (!TryIsFieldSideConditionActiveForPhase(
					FBattleFieldSideConditionRules::GetMistId(),
					Active.ActiveSlotId.GetSide(),
					EBattleTriggerPhase::BeforeHit,
					Active.ActiveSlotId,
					bMistTriggerActive))
			{
				return false;
			}
			Facts.bStatStageDropPrevented =
				FBattleFieldSideConditionRules::ShouldMistPreventStatDrop(
					bMistTriggerActive,
					bHazardAppliedByOpponent,
					false,
					-1);
			Facts.RockEffectiveness = RockEffectiveness;
			const bool bDamagingHazardWouldApply =
				(HazardId == FBattleFieldSideConditionRules::GetSpikesId()
					&& bGrounded)
				|| (HazardId
						== FBattleFieldSideConditionRules::GetStealthRockId()
					&& !RockEffectiveness.IsImmune());
			const bool bMagicGuardWouldPrevent = bDamagingHazardWouldApply
				&& FBattleAbilityRules::ShouldMagicGuardPreventDamage(
					Incoming.AbilityId,
					EBattleHPChangeSourceKind::Condition,
					Incoming.bAbilitySuppressed);
			const BattleEntryHazardPrevention::FResult Prevention =
				BattleEntryHazardPrevention::Resolve(
					bBootsBypassActive,
					bMagicGuardWouldPrevent);
			Facts.bBypassesEntryHazards = Prevention.bBypassesEntryHazards;
			Facts.bIndirectDamagePrevented = Prevention.bIndirectDamagePrevented;
			if (Facts.bIndirectDamagePrevented)
			{
				FBattleAbilityItemEffectRequest AbilityRequest;
				if (!TryGetAbilityEffectRequest(
						Incoming,
						EBattleTriggerPhase::SwitchIn,
						EBattleAbilityItemHookPoint::FinalDamage,
						AbilityRequest)
					|| !TryRecordAbilityActivation(
						AbilityRequest,
						EBattleAbilityItemActivationOutcome::Applied,
						Incoming))
				{
					return false;
				}
			}
			FBattleHazardSwitchInResult HazardResult;
			if (!FBattleFieldSideConditionRules::TryResolveHazardSwitchIn(
				Facts,
				HazardResult))
			{
				return false;
			}
			if (bLevitateMadeAirborne)
			{
				FBattleHazardSwitchInFacts GroundedFacts = Facts;
				GroundedFacts.bGrounded = true;
				FBattleHazardSwitchInResult GroundedResult;
				if (!FBattleFieldSideConditionRules::TryResolveHazardSwitchIn(
						GroundedFacts,
						GroundedResult))
				{
					return false;
				}
				if (HazardResult.EffectKind == EBattleHazardSwitchInEffectKind::None
					&& GroundedResult.EffectKind
						!= EBattleHazardSwitchInEffectKind::None
					&& !TryRecordLevitateGroundedActivation(
						Incoming,
						EBattleTriggerPhase::SwitchIn,
						EBattleAbilityItemHookPoint::SwitchIn))
				{
					return false;
				}
			}

			FBattleResolvedTarget ResolvedIncoming;
			FBattleBattlerTarget IncomingTarget;
			IncomingTarget.ActiveSlotId = Active.ActiveSlotId;
			IncomingTarget.BattlerId = Incoming.BattlerId;
			if (!FBattleResolvedTarget::TryCreateBattler(
					IncomingTarget,
					ResolvedIncoming))
			{
				return false;
			}
			FBattleEventTarget EventTarget;
			if (!TryBuildEventTarget(ResolvedIncoming, EventTarget))
			{
				return false;
			}
			FBattleEventSource HazardSource;
			HazardSource.DefinitionId = HazardId.GetDefinitionId();
			if (SourceBattler != nullptr)
			{
				HazardSource.TrainerId = SourceBattler->TrainerId;
				HazardSource.BattlerId = SourceBattler->BattlerId;
				const FBattleActivePositionState* SourceActive = FindActiveForBattler(
					SourceBattler->BattlerId);
				if (SourceActive != nullptr)
				{
					HazardSource.ActiveSlotId = SourceActive->ActiveSlotId;
				}
			}

			if (HazardResult.EffectKind == EBattleHazardSwitchInEffectKind::Damage)
			{
				const int32 PreviousHP = Incoming.CurrentHP;
				const int32 AppliedDamage = FMath::Min(PreviousHP, HazardResult.Damage);
				Incoming.CurrentHP -= AppliedDamage;
				if (Incoming.CurrentHP == 0)
				{
					Incoming.bFainted = true;
					Incoming.bFaintTransitionPending = true;
				}
				for (const EBattleEventType EventType : {
					EBattleEventType::Damage,
					EBattleEventType::HPChanged})
				{
					FBattleEffectExecutionEvent& Event =
						ExecutionResult->Events.AddDefaulted_GetRef();
					Event.Type = EventType;
					Event.Cause = EBattleEventCause::Rule;
					Event.Outcome = EBattleEffectExecutionOutcome::Applied;
					Event.SourceOverride = HazardSource;
					Event.Targets.Add(EventTarget);
					Event.NumericBefore = PreviousHP;
					Event.NumericAfter = Incoming.CurrentHP;
					Event.NumericDelta = -AppliedDamage;
				}
			}
			else if (HazardResult.EffectKind
				== EBattleHazardSwitchInEffectKind::ApplyMajorStatus)
			{
				FBattleTriggerSubject Owner;
				EBattleTriggerError Error = EBattleTriggerError::None;
				if (!FBattleTriggerSubject::TryCreateBattler(Incoming.BattlerId, Owner)
					|| !FBattleMajorStatusRules::TryRegisterTriggers(
						TriggerFramework,
						HazardResult.MajorStatusId,
						Owner,
						TOptional<int32>(),
						Error))
				{
					return false;
				}
				DrainTriggerOutputs();
				Incoming.MajorStatusId = HazardResult.MajorStatusId;
				FBattleEffectExecutionEvent& Event =
					ExecutionResult->Events.AddDefaulted_GetRef();
				Event.Type = EBattleEventType::StatusChanged;
				Event.Cause = EBattleEventCause::Rule;
				Event.Outcome = EBattleEffectExecutionOutcome::Applied;
				Event.SourceOverride = HazardSource;
				Event.Targets.Add(EventTarget);
				Event.NumericBefore = 0;
				Event.NumericAfter = 1;
				Event.NumericDelta = 1;
			}
			else if (HazardResult.EffectKind
				== EBattleHazardSwitchInEffectKind::ModifyStatStage)
			{
				const FBattleStatStageChangeResult Change = Incoming.Stages.ApplyChange(
					HazardResult.Stat,
					HazardResult.StatStageDelta);
				if (Change.Outcome == EBattleStatStageChangeOutcome::Applied)
				{
					FBattleEffectExecutionEvent& Event =
						ExecutionResult->Events.AddDefaulted_GetRef();
					Event.Type = EBattleEventType::StatStageChanged;
					Event.Cause = EBattleEventCause::Rule;
					Event.Outcome = EBattleEffectExecutionOutcome::Applied;
					Event.SourceOverride = HazardSource;
					Event.Targets.Add(EventTarget);
					Event.NumericBefore = Change.PreviousStage;
					Event.NumericAfter = Change.NewStage;
					Event.NumericDelta = Change.AppliedDelta;
				}
			}
			else if (HazardResult.EffectKind
				== EBattleHazardSwitchInEffectKind::RemoveHazard)
			{
				if (!TryCleanupFieldSideCondition(
						HazardId,
						Active.ActiveSlotId.GetSide(),
						EBattleTriggerCleanupReason::Removal))
				{
					return false;
				}
				const int32 PreviousLayers = HazardRequest.Layers;
				const FConditionId RemovedId = HazardId;
				Side->Hazards.RemoveAll(
					[&RemovedId](const FBattleConditionState& Condition)
					{
						return Condition.ConditionId == RemovedId;
					});
				FBattleResolvedTarget ResolvedSide;
				if (!FBattleResolvedTarget::TryCreateSide(
						Active.ActiveSlotId.GetSide(),
						ResolvedSide))
				{
					return false;
				}
				FBattleEventTarget SideTarget;
				if (!TryBuildEventTarget(ResolvedSide, SideTarget))
				{
					return false;
				}
				FBattleEffectExecutionEvent& Event =
					ExecutionResult->Events.AddDefaulted_GetRef();
				Event.Type = EBattleEventType::FieldEffectChanged;
				Event.Cause = EBattleEventCause::Rule;
				Event.Outcome = EBattleEffectExecutionOutcome::Applied;
				Event.SourceOverride = HazardSource;
				Event.Targets.Add(SideTarget);
				Event.NumericBefore = PreviousLayers;
				Event.NumericAfter = 0;
				Event.NumericDelta = -PreviousLayers;
			}
			if (!TryRunImmediateHeldItemUpdate(Incoming))
			{
				return false;
			}
			if ((Incoming.CurrentHP <= 0 || Incoming.bFainted)
				&& !TryCleanupItemHooks(
					Incoming,
					Incoming.HeldItem.CurrentItemId,
					EBattleTriggerCleanupReason::Faint))
			{
				return false;
			}
		}
		return true;
	}

	bool FStateExecutionContext::TryRunSwitchOutStatus(const FBattleBattlerState& Battler)
	{
		if (Battler.MajorStatusId != FBattleMajorStatusRules::GetToxicId())
		{
			return true;
		}
		FBattleTriggerSubject Owner;
		FBattleTriggerSourceDefinition SourceDefinition;
		FBattleTriggerOperationContext Operation;
		if (!FBattleTriggerSubject::TryCreateBattler(Battler.BattlerId, Owner)
			|| !FBattleTriggerSourceDefinition::TryCreateCondition(
				Battler.MajorStatusId,
				SourceDefinition)
			|| !TryTakeTriggerContext(Operation))
		{
			return false;
		}

		const TArray<FBattleTriggerRegistrationState> Registrations =
			TriggerFramework.GetActiveRegistrations();
		FBattleTriggerDispatchSpec Dispatch;
		Dispatch.Phase = EBattleTriggerPhase::SwitchOut;
		Dispatch.ReentrancyToken = Operation.ReentrancyToken;
		for (const FBattleTriggerRegistrationState& Registration : Registrations)
		{
			if (Registration.Spec.SourceDefinition == SourceDefinition
				&& Registration.Spec.Owner == Owner
				&& Registration.Spec.Rule.Phase == EBattleTriggerPhase::SwitchOut)
			{
				FBattleTriggerDispatchParticipant& Participant =
					Dispatch.Participants.AddDefaulted_GetRef();
				Participant.RegistrationId = Registration.RegistrationId;
			}
		}

		EBattleTriggerError Error = EBattleTriggerError::None;
		if (!Dispatch.Participants.IsEmpty())
		{
			FBattleTriggerDispatchResult Result;
			if (!TriggerFramework.TryEnqueueDispatch(Dispatch, Error)
				|| !TriggerFramework.TryResolveNextDispatch(Result, Error))
			{
				return false;
			}
		}
		for (const FBattleTriggerRegistrationState& Registration : Registrations)
		{
			if (Registration.Spec.SourceDefinition == SourceDefinition
				&& Registration.Spec.Owner == Owner
				&& !TriggerFramework.TryUpdateLayers(
					Registration.RegistrationId,
					FBattleMajorStatusRules::GetResetToxicLayerEncoding(),
					Operation,
					Error))
			{
				return false;
			}
		}
		DrainTriggerOutputs();
		return true;
	}
}
