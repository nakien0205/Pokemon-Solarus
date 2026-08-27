#include "BattleEngineQueueBoundary.h"

namespace BattleEngineQueueBoundaryPrivate
{
	using namespace BattleEngineCommonPrivate;
	using namespace BattleEngineEventsPrivate;
	using namespace BattleEngineSwitchPipelinePrivate;
	using namespace BattleEngineTriggerRuntimePrivate;

	bool TryRebuildReplacementCheckpointAfterEntryHazards(
		FBattleEngineState& State,
		const uint64 RequestStateVersion,
		const FResolutionId ResolutionId,
		const EBattleActionKind ActionKind,
		const FBattleEventSource& Source,
		const TConstArrayView<FBattlePendingReplacementState> AlreadyAnnouncedRequirements,
		TArray<FBattleEvent>& Events)
	{
		if (RequestStateVersion == 0 || !ResolutionId.IsValid())
		{
			return false;
		}

		State.PendingReplacements.Reset();
		State.PendingDecision.Reset();
		State.PendingDecisionRequests.Reset();
		if (State.Phase == EBattlePhase::Terminal)
		{
			return true;
		}

		State.Phase = EBattlePhase::Resolving;
		State.CurrentLockedActionIndex = State.LockedActions.Num();
		TArray<FBattleReplacementRequirement> Requirements;
		FBattleFaintOutcomeResolver::ResolveQueueBoundary(State, Requirements);
		if (State.Phase == EBattlePhase::MandatoryReplacement)
		{
			if (Requirements.IsEmpty())
			{
				return false;
			}
			for (const FBattleReplacementRequirement& Requirement : Requirements)
			{
				FBattlePendingReplacementState& Pending =
					State.PendingReplacements.AddDefaulted_GetRef();
				Pending.TrainerId = Requirement.Target.TrainerId;
				Pending.ActiveSlotId = Requirement.Target.ActiveSlotId;
			}

			TArray<FBattleDecisionRequest> Requests;
			if (!TryBuildReplacementCheckpointRequests(
					State,
					RequestStateVersion,
					false,
					Requests)
				|| Requests.IsEmpty())
			{
				return false;
			}
			State.PendingDecisionRequests = MoveTemp(Requests);
			State.PendingDecision = State.PendingDecisionRequests[0];
		}
		else if (State.Phase != EBattlePhase::EndOfTurn)
		{
			return false;
		}

		for (const FBattleReplacementRequirement& Requirement : Requirements)
		{
			const bool bAlreadyAnnounced = AlreadyAnnouncedRequirements.ContainsByPredicate(
				[&Requirement](const FBattlePendingReplacementState& Pending)
				{
					return Pending.TrainerId == Requirement.Target.TrainerId
						&& Pending.ActiveSlotId == Requirement.Target.ActiveSlotId;
				});
			if (bAlreadyAnnounced)
			{
				continue;
			}
			Events.Add(MakeTargetedActionlessEvent(
				State,
				ResolutionId,
				EBattleEventType::ReplacementRequired,
				EBattleEventCause::Rule,
				ActionKind,
				Source,
				Requirement.Target));
		}
		return true;
	}

	EBattleOptionUnavailableReason ToUnavailableReason(const EBattleSwitchBlockReason Reason)
	{
		switch (Reason)
		{
		case EBattleSwitchBlockReason::EmptyPartySlot:
			return EBattleOptionUnavailableReason::EmptyPartySlot;
		case EBattleSwitchBlockReason::AlreadyActive:
			return EBattleOptionUnavailableReason::AlreadyActive;
		case EBattleSwitchBlockReason::Fainted:
			return EBattleOptionUnavailableReason::Fainted;
		case EBattleSwitchBlockReason::Egg:
			return EBattleOptionUnavailableReason::Egg;
		case EBattleSwitchBlockReason::Captured:
			return EBattleOptionUnavailableReason::Captured;
		case EBattleSwitchBlockReason::WrongOwner:
			return EBattleOptionUnavailableReason::WrongOwner;
		case EBattleSwitchBlockReason::AlreadyReserved:
			return EBattleOptionUnavailableReason::AlreadyReserved;
		case EBattleSwitchBlockReason::Trapped:
			return EBattleOptionUnavailableReason::Trapped;
		case EBattleSwitchBlockReason::EncounterPolicy:
			return EBattleOptionUnavailableReason::SwitchRestricted;
		case EBattleSwitchBlockReason::Removed:
		default:
			return EBattleOptionUnavailableReason::Removed;
		}
	}

	bool IsBattleEngineExplicitTargetClass(const EBattleTargetClass TargetClass)
	{
		return TargetClass == EBattleTargetClass::SelectedAlly
			|| TargetClass == EBattleTargetClass::SelectedOpponent
			|| TargetClass == EBattleTargetClass::AnySelectedBattler;
	}

	TArray<FBattleTargetPositionFacts> BuildBattleEngineTargetPositions(
		const FBattleEngineState& State)
	{
		TArray<FBattleTargetPositionFacts> Positions;
		Positions.Reserve(State.ActivePositions.Num());
		for (const FBattleActivePositionState& Position : State.ActivePositions)
		{
			FBattleTargetPositionFacts Facts;
			Facts.ActiveSlotId = Position.ActiveSlotId;
			const FBattleBattlerState* Battler = Position.BattlerId.IsValid()
				? State.FindBattler(Position.BattlerId)
				: nullptr;
			if (!Position.bAvailable || Battler == nullptr)
			{
				Facts.State = EBattleTargetPositionState::Empty;
			}
			else
			{
				Facts.BattlerId = Battler->BattlerId;
				if (Battler->bCaptured)
				{
					Facts.State = EBattleTargetPositionState::Captured;
				}
				else if (Battler->bRemoved)
				{
					Facts.State = EBattleTargetPositionState::Removed;
				}
				else if (Battler->bFainted)
				{
					Facts.State = EBattleTargetPositionState::Fainted;
				}
				else
				{
					Facts.State = EBattleTargetPositionState::Living;
				}
			}
			Positions.Add(MoveTemp(Facts));
		}
		return Positions;
	}

	bool TryGetCommandBand(
		const EBattleActionKind ActionKind,
		EBattleActionCommandBand& OutBand)
	{
		OutBand = EBattleActionCommandBand::Move;
		switch (ActionKind)
		{
		case EBattleActionKind::Fight:
			OutBand = EBattleActionCommandBand::Move;
			return true;
		case EBattleActionKind::Bag:
			OutBand = EBattleActionCommandBand::Bag;
			return true;
		case EBattleActionKind::Switch:
			OutBand = EBattleActionCommandBand::VoluntarySwitch;
			return true;
		case EBattleActionKind::Run:
		case EBattleActionKind::WildFlee:
			OutBand = EBattleActionCommandBand::Run;
			return true;
		default:
			return false;
		}
	}

	bool TryBuildLockedActions(
		FBattleEngineState& State,
		const TArray<FBattleDecision>& Selections,
		const FResolutionId ResolutionId,
		TArray<FBattleLockedActionState>& OutActions,
		TArray<FBattleEvent>& OutPreLockEvents,
		bool& bOutReverseSpeed)
	{
		OutActions.Reset();
		OutPreLockEvents.Reset();
		bOutReverseSpeed = false;
		const uint64 NextActionIdAfterLock = State.NextActionId + static_cast<uint64>(Selections.Num());
		if (Selections.IsEmpty()
			|| Selections.Num() > 4
			|| !State.Random.IsValid()
			|| NextActionIdAfterLock <= State.NextActionId)
		{
			return false;
		}

		FBattleActionQueueLockSpec LockSpec;
		LockSpec.BattleId = State.Setup.GetBattleId();
		LockSpec.TurnId = State.TurnId;
		LockSpec.ResolutionId = ResolutionId;
		bool bTrickRoomTriggerActive = false;
		if (!TryIsFieldSideConditionActiveForPhase(
				State,
				FBattleFieldSideConditionRules::GetTrickRoomId(),
				TOptional<EBattleSide>(),
				EBattleTriggerPhase::ActionOrderCalculation,
				TOptional<FActiveSlotId>(),
				bTrickRoomTriggerActive))
		{
			return false;
		}
		LockSpec.bReverseSpeed = FBattleFieldSideConditionRules::ShouldReverseSpeedOrder(
			bTrickRoomTriggerActive);
		bOutReverseSpeed = LockSpec.bReverseSpeed;
		LockSpec.Candidates.Reserve(Selections.Num());

		for (int32 Index = 0; Index < Selections.Num(); ++Index)
		{
			const FBattleDecision& Decision = Selections[Index];
			const FBattleBattlerState* Battler = State.FindBattler(Decision.GetActingBattlerId());
			const FBattleActivePositionState* Active = Battler != nullptr
				? FindActiveForBattler(State, Battler->BattlerId)
				: nullptr;
			const FBattleTrainerState* Trainer = Battler != nullptr
				? State.FindTrainer(Battler->TrainerId)
				: nullptr;
			if (Battler == nullptr
				|| Active == nullptr
				|| Trainer == nullptr
				|| Trainer->TrainerId != Decision.GetDecisionOwnerTrainerId()
				|| !IsLivingSelectableBattler(Battler))
			{
				return false;
			}

			const uint64 RawActionId = State.NextActionId + static_cast<uint64>(Index);
			FActionId ActionId;
			if (RawActionId < State.NextActionId
				|| !FActionId::TryCreate(RawActionId, ActionId))
			{
				return false;
			}

			FBattleActionOrderCandidate Candidate;
			Candidate.ActionId = ActionId;
			Candidate.Decision = Decision;
			Candidate.OrderKey.ActingSlotId = Active->ActiveSlotId;
			if (!TryGetCommandBand(Decision.GetActionKind(), Candidate.OrderKey.CommandBand)
				|| !TryCalculateEffectiveSpeedForOrdering(
					State,
					*Battler,
					Active->ActiveSlotId,
					Candidate.OrderKey.EffectiveSpeed))
			{
				return false;
			}

			if (Decision.GetActionKind() == EBattleActionKind::Fight)
			{
				const FMoveId MoveId = Decision.GetMoveId();
				const FBattleMoveDefinition* Move = MoveId == FBattleBuiltInMoveDefinitions::GetStruggleMoveId()
					? &FBattleBuiltInMoveDefinitions::GetStruggle()
					: State.Catalog.FindMove(MoveId);
				if (Move == nullptr)
				{
					return false;
				}
				Candidate.OrderKey.MovePriority = Move->Priority;
				Candidate.TargetClass = Move->TargetClass;

				if (IsBattleEngineExplicitTargetClass(Candidate.TargetClass))
				{
					const FBattleActivePositionState* Target = State.FindActivePosition(
						Decision.GetActiveTargetId());
					if (Target == nullptr)
					{
						return false;
					}
					if (Target->BattlerId.IsValid())
					{
						Candidate.SelectedTargetBattlerId = Target->BattlerId;
					}
					else
					{
						FMoveId StoredChargeMoveId;
						FBattlerId StoredTargetBattlerId;
						if (!HasVolatile(
								*Battler,
								FBattleVolatileRules::GetChargingId())
							|| !TryGetVolatilePayloadMoveId(
								State,
								Battler->BattlerId,
								FBattleVolatileRules::GetChargingId(),
								StoredChargeMoveId)
							|| StoredChargeMoveId != MoveId
							|| !TryGetChargingTargetBattler(
								State,
								Battler->BattlerId,
								StoredTargetBattlerId))
						{
							return false;
						}
						Candidate.SelectedTargetBattlerId = StoredTargetBattlerId;
					}
				}
			}
			else if (Decision.GetActionKind() == EBattleActionKind::Bag)
			{
				if (Decision.GetItemPartyTargetId().IsValid()
					&& !Decision.GetActiveTargetId().IsValid())
				{
					const FBattlePartySlotState* PartySlot = Trainer->PartySlots.FindByPredicate(
						[&Decision](const FBattlePartySlotState& CandidateSlot)
						{
							return CandidateSlot.PartySlotId
								== Decision.GetItemPartyTargetId();
						});
					if (PartySlot == nullptr || !PartySlot->BattlerId.IsValid())
					{
						return false;
					}
					Candidate.SelectedTargetBattlerId = PartySlot->BattlerId;
				}
				else if (Decision.GetActiveTargetId().IsValid()
					&& !Decision.GetItemPartyTargetId().IsValid())
				{
					const FBattleActivePositionState* Target = State.FindActivePosition(
						Decision.GetActiveTargetId());
					if (Target == nullptr || !Target->BattlerId.IsValid())
					{
						return false;
					}
					Candidate.SelectedTargetBattlerId = Target->BattlerId;
				}
				else
				{
					return false;
				}
			}
			LockSpec.Candidates.Add(MoveTemp(Candidate));
		}

		struct FQuickClawDrawRecord
		{
			FActionId ActionId;
			FBattlerId BattlerId;
			FBattleRandomDraw Draw;
			bool bActivated = false;
		};
		TArray<int32> QuickClawCandidateIndices;
		for (int32 CandidateIndex = 0;
			CandidateIndex < LockSpec.Candidates.Num();
			++CandidateIndex)
		{
			const FBattleActionOrderCandidate& Candidate = LockSpec.Candidates[CandidateIndex];
			if (Candidate.Decision.GetActionKind() != EBattleActionKind::Fight)
			{
				continue;
			}
			const FBattleBattlerState* Battler = State.FindBattler(
				Candidate.Decision.GetActingBattlerId());
			if (Battler == nullptr || !IsHeldItemActive(*Battler))
			{
				continue;
			}
			FBattleQuickClawFacts Facts;
			Facts.ItemId = Battler->HeldItem.CurrentItemId;
			Facts.MovePriority = Candidate.OrderKey.MovePriority;
			Facts.bSelectedMoveEligible = true;
			Facts.bSuppressed = Battler->HeldItem.bSuppressed;
			FBattleQuickClawEligibilityResult Eligibility;
			if (!FBattleItemRules::TryEvaluateQuickClawEligibility(Facts, Eligibility))
			{
				if (Battler->HeldItem.CurrentItemId == FBattleItemRules::GetQuickClawId())
				{
					return false;
				}
				continue;
			}
			if (Eligibility.bEligible && Eligibility.bConsumesRandomDraw)
			{
				QuickClawCandidateIndices.Add(CandidateIndex);
			}
		}
		QuickClawCandidateIndices.Sort(
			[&LockSpec](const int32 LeftIndex, const int32 RightIndex)
			{
				const FActiveSlotId Left =
					LockSpec.Candidates[LeftIndex].OrderKey.ActingSlotId;
				const FActiveSlotId Right =
					LockSpec.Candidates[RightIndex].OrderKey.ActingSlotId;
				const uint8 LeftSide = static_cast<uint8>(Left.GetSide());
				const uint8 RightSide = static_cast<uint8>(Right.GetSide());
				return LeftSide != RightSide
					? LeftSide < RightSide
					: static_cast<uint8>(Left.GetPosition())
						< static_cast<uint8>(Right.GetPosition());
			});
		TArray<FQuickClawDrawRecord> QuickClawDraws;
		for (const int32 CandidateIndex : QuickClawCandidateIndices)
		{
			FBattleActionOrderCandidate& Candidate = LockSpec.Candidates[CandidateIndex];
			const FBattleBattlerState* Battler = State.FindBattler(
				Candidate.Decision.GetActingBattlerId());
			check(Battler != nullptr);
			FBattleRandomContext RandomContext;
			RandomContext.BattleId = State.Setup.GetBattleId();
			RandomContext.TurnId = State.TurnId;
			RandomContext.ActionId = Candidate.ActionId;
			RandomContext.ResolutionId = ResolutionId;
			RandomContext.RulePurpose = FBattleItemRules::GetQuickClawActivationPurpose();
			FBattleRandomDraw Draw;
			if (!State.Random->TryDrawUniform(
					0,
					FBattleItemRules::GetQuickClawRollMaxInclusive(),
					RandomContext,
					Draw))
			{
				return false;
			}
			FBattleQuickClawFacts Facts;
			Facts.ItemId = Battler->HeldItem.CurrentItemId;
			Facts.MovePriority = Candidate.OrderKey.MovePriority;
			Facts.bSelectedMoveEligible = true;
			Facts.bSuppressed = Battler->HeldItem.bSuppressed;
			FBattleQuickClawDrawResult DrawResult;
			if (!FBattleItemRules::TryResolveQuickClawDraw(
					Facts,
					Draw.Result,
					DrawResult))
			{
				return false;
			}
			Candidate.OrderKey.FractionalPriorityTenths =
				DrawResult.FractionalPriorityTenths;
			FQuickClawDrawRecord& Record = QuickClawDraws.AddDefaulted_GetRef();
			Record.ActionId = Candidate.ActionId;
			Record.BattlerId = Battler->BattlerId;
			Record.Draw = Draw;
			Record.bActivated = DrawResult.bApplies;
		}

		TArray<FBattleLockedAction> Locked;
		EBattleActionQueueError QueueError = EBattleActionQueueError::None;
		if (!FBattleActionQueueResolver::TryLock(
			LockSpec,
			*State.Random,
			Locked,
			QueueError))
		{
			return false;
		}

		OutActions.Reserve(Locked.Num());
		for (const FBattleLockedAction& Action : Locked)
		{
			FBattleLockedActionState StateAction;
			StateAction.ActionId = Action.ActionId;
			StateAction.QueueOrdinal = Action.QueueOrdinal;
			StateAction.Decision = Action.Decision;
			StateAction.OrderKey = Action.OrderKey;
			StateAction.TargetClass = Action.TargetClass;
			StateAction.SelectedTargetBattlerId = Action.SelectedTargetBattlerId;
			OutActions.Add(MoveTemp(StateAction));
		}
		for (const FQuickClawDrawRecord& DrawRecord : QuickClawDraws)
		{
			const FBattleLockedActionState* LockedAction = OutActions.FindByPredicate(
				[&DrawRecord](const FBattleLockedActionState& Candidate)
				{
					return Candidate.ActionId == DrawRecord.ActionId;
				});
			if (LockedAction == nullptr)
			{
				return false;
			}
			OutPreLockEvents.Add(MakeActionDetailEvent(
				State,
				ResolutionId,
				*LockedAction,
				EBattleEventType::RandomCheck,
				EBattleEventCause::Rule,
				static_cast<int64>(DrawRecord.Draw.InclusiveMinimum),
				static_cast<int64>(DrawRecord.Draw.Result),
				static_cast<int64>(DrawRecord.Draw.InclusiveMaximum),
				EBattleVisibilityLevel::CoreOnly));
			if (DrawRecord.bActivated
				&& !TryAppendItemActivationForPhase(
					State,
					DrawRecord.BattlerId,
					EBattleTriggerPhase::ActionOrderCalculation,
					EBattleAbilityItemActivationOutcome::Applied,
					ResolutionId,
					DrawRecord.ActionId,
					EBattleActionKind::Fight,
					OutPreLockEvents))
			{
				return false;
			}
		}
		return true;
	}

	void AddUnavailableAction(
		FBattleDecisionRequestSpec& Spec,
		const EBattleActionKind ActionKind,
		const EBattleOptionUnavailableReason Reason)
	{
		FBattleUnavailableDecisionOption Option;
		Option.Kind = EBattleDecisionOptionKind::Action;
		Option.Reason = Reason;
		Option.ActionKind = ActionKind;
		Spec.UnavailableOptions.Add(Option);
	}

	void AddUnavailableMove(
		FBattleDecisionRequestSpec& Spec,
		const FMoveId MoveId,
		const EBattleOptionUnavailableReason Reason)
	{
		FBattleUnavailableDecisionOption Option;
		Option.Kind = EBattleDecisionOptionKind::Move;
		Option.Reason = Reason;
		Option.MoveId = MoveId;
		Spec.UnavailableOptions.Add(Option);
	}

	void AddUnavailableSwitch(
		FBattleDecisionRequestSpec& Spec,
		const FPartySlotId PartySlotId,
		const EBattleOptionUnavailableReason Reason)
	{
		FBattleUnavailableDecisionOption Option;
		Option.Kind = EBattleDecisionOptionKind::SwitchPartySlot;
		Option.Reason = Reason;
		Option.PartySlotId = PartySlotId;
		Spec.UnavailableOptions.Add(Option);
	}

	void AddUnavailableItem(
		FBattleDecisionRequestSpec& Spec,
		const FItemId ItemId,
		const EBattleOptionUnavailableReason Reason)
	{
		FBattleUnavailableDecisionOption Option;
		Option.Kind = EBattleDecisionOptionKind::Item;
		Option.Reason = Reason;
		Option.ItemId = ItemId;
		Spec.UnavailableOptions.Add(Option);
	}

	bool TryBuildPivotDecisionRequest(
		const FBattleEngineState& State,
		const FBattleLockedActionState& Action,
		const uint64 StateVersion,
		FBattleDecisionRequest& OutRequest)
	{
		OutRequest = FBattleDecisionRequest();
		FBattleSwitchLegalityResult Legality;
		if (!TryBuildSwitchLegality(
			State,
			EBattleSwitchKind::Pivot,
			Action.Decision.GetDecisionOwnerTrainerId(),
			Action.Decision.GetActingBattlerId(),
			Action.OrderKey.ActingSlotId,
			TConstArrayView<FPartySlotId>(),
			Legality)
			|| Legality.GetLegalPartySlots().IsEmpty())
		{
			return false;
		}

		FBattleDecisionRequestSpec Spec;
		Spec.StateVersion = StateVersion;
		Spec.RequestKind = EBattleDecisionRequestKind::PivotSwitch;
		Spec.DecisionOwnerTrainerId = Action.Decision.GetDecisionOwnerTrainerId();
		Spec.ActingBattlerId = Action.Decision.GetActingBattlerId();
		Spec.ActingSlotId = Action.OrderKey.ActingSlotId;
		Spec.LegalActionKinds.Add(EBattleActionKind::Switch);
		for (const FPartySlotId PartySlotId : Legality.GetLegalPartySlots())
		{
			Spec.LegalSwitchPartySlots.Add(PartySlotId);
		}
		Spec.LegalActiveTargets.Add(Action.OrderKey.ActingSlotId);
		FBattleRejection Rejection;
		return FBattleDecisionRequest::TryCreate(Spec, OutRequest, Rejection);
	}

	bool TryAddBattleEngineMoveSelection(
		const FBattleActivePositionState& ActingPosition,
		const TArray<FBattleTargetPositionFacts>& Positions,
		const FMoveId MoveId,
		const EBattleTargetClass TargetClass,
		FBattleDecisionRequestSpec& InOutSpec,
		bool& OutHasLegalTarget)
	{
		OutHasLegalTarget = false;
		FBattleTargetSelectionSpec TargetSpec;
		TargetSpec.TargetClass = TargetClass;
		TargetSpec.UserSlotId = ActingPosition.ActiveSlotId;
		TargetSpec.UserBattlerId = ActingPosition.BattlerId;
		TargetSpec.Positions = Positions;

		FBattleTargetSelectionResult TargetSelection;
		EBattleTargetingError TargetError = EBattleTargetingError::None;
		if (!FBattleTargetResolver::TryBuildSelection(
			TargetSpec,
			TargetSelection,
			TargetError))
		{
			return false;
		}
		if (!TargetSelection.bHasLegalTarget)
		{
			return true;
		}

		OutHasLegalTarget = true;
		InOutSpec.LegalMoveIds.Add(MoveId);
		if (!TargetSelection.bRequiresExplicitChoice)
		{
			InOutSpec.AutomaticallyTargetedMoveIds.Add(MoveId);
		}
		for (const FBattleBattlerTarget& Target : TargetSelection.BattlerCandidates)
		{
			AddUnique(InOutSpec.LegalActiveTargets, Target.ActiveSlotId);
			InOutSpec.LegalMoveTargets.Add({MoveId, Target.ActiveSlotId});
		}
		return true;
	}

	int32 GetMoveCurrentPP(
		const FBattleBattlerState& Battler,
		const FMoveId MoveId)
	{
		if (!MoveId.IsValid())
		{
			return 0;
		}
		if (MoveId == FBattleBuiltInMoveDefinitions::GetStruggleMoveId())
		{
			return 1;
		}
		const FBattleMoveSlotState* Slot = Battler.Moves.FindByPredicate(
			[MoveId](const FBattleMoveSlotState& Candidate)
			{
				return Candidate.MoveId == MoveId;
			});
		return Slot != nullptr ? Slot->CurrentPP : 0;
	}

	EBattleOptionUnavailableReason ToUnavailableReason(
		const EBattleVolatileMoveGateOutcome Outcome)
	{
		switch (Outcome)
		{
		case EBattleVolatileMoveGateOutcome::Taunted:
			return EBattleOptionUnavailableReason::Taunted;
		case EBattleVolatileMoveGateOutcome::EncoreLocked:
			return EBattleOptionUnavailableReason::Encored;
		case EBattleVolatileMoveGateOutcome::Disabled:
			return EBattleOptionUnavailableReason::Disabled;
		default:
			return EBattleOptionUnavailableReason::Removed;
		}
	}

	bool TryBuildBagItemUseFacts(
		const FBattleEngineState& State,
		const FBattleTrainerState& ActingTrainer,
		const FBattleBattlerState& ActingBattler,
		const FBattleItemDefinition& ItemDefinition,
		const EBattleBagItemTargetKind TargetKind,
		const FBattleBattlerState& TargetBattler,
		const FBattleActivePositionState* TargetActive,
		FBattleBagItemUseFacts& OutFacts)
	{
		OutFacts = FBattleBagItemUseFacts();
		int32 AttackStage = 0;
		if (!TargetBattler.Stages.TryGetStage(EBattleStat::Attack, AttackStage))
		{
			return false;
		}

		OutFacts.ItemId = ItemDefinition.Id;
		OutFacts.DefinitionKind = ItemDefinition.Kind;
		OutFacts.TargetKind = TargetKind;
		const FBattleTrainerEncounterPolicy* TrainerPolicy =
			FindTrainerEncounterPolicy(State, ActingTrainer.TrainerId);
		if (TrainerPolicy == nullptr)
		{
			return false;
		}
		OutFacts.bActingTrainerMayUseBag = TrainerPolicy->bMayUseBag;
		OutFacts.bActingTrainerMayCapture = TrainerPolicy->bMayCapture;
		OutFacts.bActingTrainerMayUseRevive = TrainerPolicy->bMayUseRevive;
		OutFacts.bTargetOwnedByActingTrainer =
			TargetBattler.TrainerId == ActingTrainer.TrainerId;
		OutFacts.bTargetIsActingBattler =
			TargetBattler.BattlerId == ActingBattler.BattlerId;
		OutFacts.bTargetIsOpposingActive = TargetActive != nullptr
			&& TargetActive->bAvailable
			&& TargetActive->BattlerId == TargetBattler.BattlerId
			&& TargetActive->ActiveSlotId.GetSide() != ActingTrainer.Side;
		OutFacts.bTargetEgg = TargetBattler.bEgg;
		OutFacts.bTargetCaptured = TargetBattler.bCaptured;
		OutFacts.bTargetRemoved = TargetBattler.bRemoved;
		OutFacts.bTargetFainted = TargetBattler.bFainted;
		OutFacts.bTargetFaintTransitionPending = TargetBattler.bFaintTransitionPending;
		OutFacts.CurrentHP = TargetBattler.CurrentHP;
		OutFacts.MaximumHP = TargetBattler.PermanentStats.MaxHP;
		OutFacts.bHasCanonicalMajorStatus =
			FBattleMajorStatusRules::IsCanonical(TargetBattler.MajorStatusId);
		OutFacts.bHasConfusion = HasVolatile(
			TargetBattler,
			FBattleVolatileRules::GetConfusionId());
		OutFacts.AttackStage = AttackStage;
		return true;
	}

	bool TryBuildDecisionRequest(
		const FBattleEngineState& State,
		const FBattleDecisionActorState& Actor,
		const uint64 StateVersion,
		const TConstArrayView<FBattleDecision> AdditionalSelections,
		FBattleDecisionRequest& OutRequest,
		FBattleRejection& OutRejection)
	{
		const FBattleActivePositionState* ActingPosition = State.FindActivePosition(Actor.ActiveSlotId);
		const FBattleBattlerState* Battler = State.FindBattler(Actor.BattlerId);
		const FBattleTrainerState* Trainer = Battler != nullptr ? State.FindTrainer(Battler->TrainerId) : nullptr;
		const FBattleTrainerEncounterPolicy* TrainerPolicy = Trainer != nullptr
			? FindTrainerEncounterPolicy(State, Trainer->TrainerId)
			: nullptr;
		if (!State.bHasCatalog
			|| ActingPosition == nullptr
			|| Battler == nullptr
			|| Trainer == nullptr
			|| TrainerPolicy == nullptr
			|| ActingPosition->BattlerId != Battler->BattlerId
			|| ActingPosition->TrainerId != Trainer->TrainerId
			|| !IsLivingSelectableBattler(Battler)
			|| Trainer->ActionAllowance.RemainingActions <= 0)
		{
			OutRejection = FBattleRejection();
			OutRejection.Reason = EBattleRejectionReason::InvalidSetup;
			return false;
		}

		FBattleDecisionRequestSpec Spec;
		Spec.StateVersion = StateVersion;
		Spec.RequestKind = EBattleDecisionRequestKind::Action;
		Spec.DecisionOwnerTrainerId = Trainer->TrainerId;
		Spec.ActingBattlerId = Battler->BattlerId;
		Spec.ActingSlotId = ActingPosition->ActiveSlotId;
		const TArray<FBattleTargetPositionFacts> TargetPositions =
			BuildBattleEngineTargetPositions(State);

		bool bMoveRejectedForNoTarget = false;
		for (const FBattleMoveSlotState& Move : Battler->Moves)
		{
			const FBattleMoveDefinition* Definition = State.Catalog.FindMove(Move.MoveId);
			if (Definition == nullptr)
			{
				AddUnavailableMove(Spec, Move.MoveId, EBattleOptionUnavailableReason::MissingCatalogReference);
				continue;
			}
			if (IsHeldItemActive(*Battler)
				&& Battler->HeldItem.CurrentItemId == FBattleItemRules::GetChoiceBandId())
			{
				FBattleChoiceBandMoveFacts ChoiceFacts;
				ChoiceFacts.ItemId = Battler->HeldItem.CurrentItemId;
				ChoiceFacts.SelectedMoveId = Move.MoveId;
				ChoiceFacts.LockedMoveId = Battler->HeldItem.ChoiceLockedMoveId;
				ChoiceFacts.bSuppressed = Battler->HeldItem.bSuppressed;
				FBattleChoiceBandMoveResult ChoiceResult;
				if (!FBattleItemRules::TryEvaluateChoiceBandMove(
						ChoiceFacts,
						ChoiceResult))
				{
					OutRejection = FBattleRejection();
					OutRejection.Reason = EBattleRejectionReason::InvalidSetup;
					return false;
				}
				if (!ChoiceResult.bMoveAllowed)
				{
					AddUnavailableMove(
						Spec,
						Move.MoveId,
						EBattleOptionUnavailableReason::ChoiceLocked);
					continue;
				}
			}
			if (Move.CurrentPP <= 0)
			{
				AddUnavailableMove(Spec, Move.MoveId, EBattleOptionUnavailableReason::NoPP);
				continue;
			}
			FBattleVolatileMoveGateResult MoveGate;
			if (!TryResolveVolatileMoveGate(
					State,
					*Battler,
					*Definition,
					false,
					MoveGate))
			{
				OutRejection = FBattleRejection();
				OutRejection.Reason = EBattleRejectionReason::InvalidSetup;
				return false;
			}
			if (MoveGate.Outcome != EBattleVolatileMoveGateOutcome::Allowed)
			{
				AddUnavailableMove(
					Spec,
					Move.MoveId,
					ToUnavailableReason(MoveGate.Outcome));
				continue;
			}
			bool bHasLegalTarget = false;
			if (!TryAddBattleEngineMoveSelection(
				*ActingPosition,
				TargetPositions,
				Move.MoveId,
				Definition->TargetClass,
				Spec,
				bHasLegalTarget))
			{
				OutRejection = FBattleRejection();
				OutRejection.Reason = EBattleRejectionReason::InvalidSetup;
				return false;
			}
			if (!bHasLegalTarget)
			{
				bMoveRejectedForNoTarget = true;
				AddUnavailableMove(Spec, Move.MoveId, EBattleOptionUnavailableReason::NoLegalTarget);
			}
		}
		if (Spec.LegalMoveIds.IsEmpty())
		{
			const FBattleMoveDefinition& Struggle = FBattleBuiltInMoveDefinitions::GetStruggle();
			bool bHasLegalTarget = false;
			if (!TryAddBattleEngineMoveSelection(
				*ActingPosition,
				TargetPositions,
				Struggle.Id,
				Struggle.TargetClass,
				Spec,
				bHasLegalTarget))
			{
				OutRejection = FBattleRejection();
				OutRejection.Reason = EBattleRejectionReason::InvalidSetup;
				return false;
			}
			if (!bHasLegalTarget)
			{
				bMoveRejectedForNoTarget = true;
			}
		}

		if (!Spec.LegalMoveIds.IsEmpty())
		{
			Spec.LegalActionKinds.Add(EBattleActionKind::Fight);
		}
		else
		{
			AddUnavailableAction(
				Spec,
				EBattleActionKind::Fight,
				bMoveRejectedForNoTarget ? EBattleOptionUnavailableReason::NoLegalTarget : EBattleOptionUnavailableReason::NoPP);
		}

		TArray<FPartySlotId> ReservedPartySlots;
		auto AddReservedSlots = [&ReservedPartySlots, Trainer](
			const TConstArrayView<FBattleDecision> Decisions)
		{
			for (const FBattleDecision& Decision : Decisions)
			{
				if (Decision.GetDecisionOwnerTrainerId() == Trainer->TrainerId
					&& Decision.GetActionKind() == EBattleActionKind::Switch
					&& Decision.GetSwitchPartySlotId().IsValid())
				{
					AddUnique(ReservedPartySlots, Decision.GetSwitchPartySlotId());
				}
			}
		};
		AddReservedSlots(State.AcceptedSelections);
		AddReservedSlots(AdditionalSelections);

		FBattleSwitchLegalityResult SwitchLegality;
		if (!TryBuildSwitchLegality(
			State,
			EBattleSwitchKind::Voluntary,
			Trainer->TrainerId,
			Battler->BattlerId,
			ActingPosition->ActiveSlotId,
			ReservedPartySlots,
			SwitchLegality))
		{
			OutRejection = FBattleRejection();
			OutRejection.Reason = EBattleRejectionReason::InvalidSetup;
			return false;
		}
		for (const FBattleSwitchCandidateResult& Candidate : SwitchLegality.GetCandidates())
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
		if (!Spec.LegalSwitchPartySlots.IsEmpty())
		{
			Spec.LegalActionKinds.Add(EBattleActionKind::Switch);
			AddUnique(Spec.LegalActiveTargets, ActingPosition->ActiveSlotId);
		}
		else
		{
			AddUnavailableAction(
				Spec,
				EBattleActionKind::Switch,
				SwitchLegality.IsBlocked()
					? ToUnavailableReason(SwitchLegality.GetBlockReason())
					: EBattleOptionUnavailableReason::NoLegalTarget);
		}

		const auto HasTrainerBagSelection = [Trainer](
			const TConstArrayView<FBattleDecision> Decisions)
		{
			for (const FBattleDecision& Decision : Decisions)
			{
				if (Decision.GetDecisionOwnerTrainerId() == Trainer->TrainerId
					&& Decision.GetActionKind() == EBattleActionKind::Bag)
				{
					return true;
				}
			}
			return false;
		};
		const bool bBagAlreadySelected = HasTrainerBagSelection(State.AcceptedSelections)
			|| HasTrainerBagSelection(AdditionalSelections);
		if (!TrainerPolicy->bMayUseBag
			|| !Trainer->ActionAllowance.bBagActionAvailable
			|| bBagAlreadySelected)
		{
			AddUnavailableAction(Spec, EBattleActionKind::Bag, EBattleOptionUnavailableReason::BagRestricted);
		}
		else
		{
			bool bAnyRemainingCanonicalItem = false;
			bool bCaptureCapacityBlocked = false;
			const int64 TotalCaptureCapacity =
				static_cast<int64>(State.CaptureCapacity.PartySlotsRemaining)
				+ static_cast<int64>(State.CaptureCapacity.StorageSlotsRemaining);
			const bool bCaptureCapacityFull =
				static_cast<int64>(State.PendingCaptures.Num()) >= TotalCaptureCapacity;
			for (const FBattleBagItemCount& ItemCount : Trainer->Bag)
			{
				const FBattleItemDefinition* Item = State.Catalog.FindItem(ItemCount.ItemId);
				if (Item == nullptr)
				{
					AddUnavailableItem(Spec, ItemCount.ItemId, EBattleOptionUnavailableReason::MissingCatalogReference);
					continue;
				}
				if (ItemCount.Count <= 0)
				{
					AddUnavailableItem(Spec, ItemCount.ItemId, EBattleOptionUnavailableReason::NoItemRemaining);
					continue;
				}
				const EBattleBagItemRuleKind RuleKind =
					FBattleBagItemRules::GetKind(ItemCount.ItemId);
				if (RuleKind == EBattleBagItemRuleKind::None
					|| RuleKind == EBattleBagItemRuleKind::Invalid
					|| Item->Kind
						!= FBattleBagItemRules::GetExpectedDefinitionKind(RuleKind))
				{
					AddUnavailableItem(Spec, ItemCount.ItemId, EBattleOptionUnavailableReason::WrongItemKind);
					continue;
				}
				bAnyRemainingCanonicalItem = true;
				if (RuleKind == EBattleBagItemRuleKind::PokeBall
					&& !TrainerPolicy->bMayCapture)
				{
					AddUnavailableItem(
						Spec,
						ItemCount.ItemId,
						EBattleOptionUnavailableReason::CaptureRestricted);
					continue;
				}
				if (RuleKind == EBattleBagItemRuleKind::PokeBall
					&& bCaptureCapacityFull)
				{
					bCaptureCapacityBlocked = true;
					AddUnavailableItem(
						Spec,
						ItemCount.ItemId,
						EBattleOptionUnavailableReason::CaptureCapacityFull);
					continue;
				}

				const int32 PartyPairStart = Spec.LegalItemPartyTargets.Num();
				const int32 ActivePairStart = Spec.LegalItemActiveTargets.Num();
				if (FBattleBagItemRules::GetTargetKind(RuleKind)
					== EBattleBagItemTargetKind::Party)
				{
					for (const FBattlePartySlotState& PartySlot : Trainer->PartySlots)
					{
						const FBattleBattlerState* Target = State.FindBattler(PartySlot.BattlerId);
						if (Target == nullptr)
						{
							continue;
						}
						const FBattleActivePositionState* TargetActive =
							FindActiveForBattler(State, Target->BattlerId);
						FBattleBagItemUseFacts Facts;
						FBattleBagItemUseResult Result;
						if (!TryBuildBagItemUseFacts(
								State,
								*Trainer,
								*Battler,
								*Item,
								EBattleBagItemTargetKind::Party,
								*Target,
								TargetActive,
								Facts)
							|| !FBattleBagItemRules::TryEvaluateUse(Facts, Result))
						{
							OutRejection = FBattleRejection();
							OutRejection.Reason = EBattleRejectionReason::InvalidSetup;
							return false;
						}
						if (Result.bLegal)
						{
							AddUnique(Spec.LegalPartyTargets, PartySlot.PartySlotId);
							Spec.LegalItemPartyTargets.Add({ItemCount.ItemId, PartySlot.PartySlotId});
						}
					}
				}
				else
				{
					for (const FBattleActivePositionState& Position : State.ActivePositions)
					{
						const FBattleBattlerState* Target = State.FindBattler(Position.BattlerId);
						if (Target == nullptr
							|| (RuleKind == EBattleBagItemRuleKind::PokeBall
								&& Target->CaptureClassification
									!= EBattleCaptureSpeciesClassification::Normal))
						{
							continue;
						}
						FBattleBagItemUseFacts Facts;
						FBattleBagItemUseResult Result;
						if (!TryBuildBagItemUseFacts(
								State,
								*Trainer,
								*Battler,
								*Item,
								EBattleBagItemTargetKind::Active,
								*Target,
								&Position,
								Facts)
							|| !FBattleBagItemRules::TryEvaluateUse(Facts, Result))
						{
							OutRejection = FBattleRejection();
							OutRejection.Reason = EBattleRejectionReason::InvalidSetup;
							return false;
						}
						if (Result.bLegal)
						{
							AddUnique(Spec.LegalActiveTargets, Position.ActiveSlotId);
							Spec.LegalItemActiveTargets.Add({ItemCount.ItemId, Position.ActiveSlotId});
						}
					}
				}

				if (Spec.LegalItemPartyTargets.Num() == PartyPairStart
					&& Spec.LegalItemActiveTargets.Num() == ActivePairStart)
				{
					AddUnavailableItem(Spec, ItemCount.ItemId, EBattleOptionUnavailableReason::NoLegalTarget);
				}
				else
				{
					Spec.LegalItemIds.Add(ItemCount.ItemId);
				}
			}

			if (!Spec.LegalItemIds.IsEmpty())
			{
				Spec.LegalActionKinds.Add(EBattleActionKind::Bag);
			}
			else
			{
				AddUnavailableAction(
					Spec,
					EBattleActionKind::Bag,
					bCaptureCapacityBlocked
						? EBattleOptionUnavailableReason::CaptureCapacityFull
						: bAnyRemainingCanonicalItem
						? EBattleOptionUnavailableReason::NoLegalTarget
						: EBattleOptionUnavailableReason::NoItemRemaining);
			}
		}

		if (CanOfferRunAction(State, *Trainer, *Battler))
		{
			Spec.LegalActionKinds.Add(EBattleActionKind::Run);
		}
		else
		{
			AddUnavailableAction(Spec, EBattleActionKind::Run, EBattleOptionUnavailableReason::RunRestricted);
		}

		if (CanOfferWildFleeAction(State, *Trainer, *Battler))
		{
			Spec.LegalActionKinds.Add(EBattleActionKind::WildFlee);
		}

		return FBattleDecisionRequest::TryCreate(Spec, OutRequest, OutRejection);
	}

	int32 GetDecisionSequenceBand(const FBattleTrainerEncounterPolicy& Trainer)
	{
		if (Trainer.Side == EBattleSide::Player
			&& Trainer.Role == EBattleTrainerRole::Player
			&& Trainer.Controller == EBattleDecisionController::Human)
		{
			return 0;
		}
		if (Trainer.Side == EBattleSide::Player
			&& Trainer.Role == EBattleTrainerRole::Partner
			&& Trainer.Controller == EBattleDecisionController::Human)
		{
			return 1;
		}
		if (Trainer.Controller == EBattleDecisionController::PartnerAI)
		{
			return 2;
		}
		if (Trainer.Controller == EBattleDecisionController::EnemyAI)
		{
			return 3;
		}
		return 4;
	}

	TArray<FBattleDecisionOwnerState> BuildDecisionOwnerSequence(const FBattleEngineState& State)
	{
		TArray<FBattleDecisionOwnerState> Sequence;
		for (const FBattleTrainerEncounterPolicy& TrainerPolicy :
			State.CompiledEncounterPolicies.GetTrainerPolicies())
		{
			const FBattleTrainerState* Trainer = State.FindTrainer(TrainerPolicy.TrainerId);
			check(Trainer != nullptr);
			FBattleDecisionOwnerState Owner;
			Owner.TrainerId = TrainerPolicy.TrainerId;
			Owner.Controller = TrainerPolicy.Controller;
			for (const FBattleActivePositionState& Position : State.ActivePositions)
			{
				if (Position.TrainerId == TrainerPolicy.TrainerId
					&& IsLivingSelectableBattler(State.FindBattler(Position.BattlerId)))
				{
					Owner.Actors.Add({Position.BattlerId, Position.ActiveSlotId});
				}
			}
			Owner.Actors.Sort(
				[](const FBattleDecisionActorState& Left, const FBattleDecisionActorState& Right)
				{
					return ActiveSlotLess(Left.ActiveSlotId, Right.ActiveSlotId);
				});
			if (!Owner.Actors.IsEmpty())
			{
				Sequence.Add(MoveTemp(Owner));
			}
		}

		Sequence.Sort(
			[&State](const FBattleDecisionOwnerState& Left, const FBattleDecisionOwnerState& Right)
			{
				const FBattleTrainerEncounterPolicy* LeftTrainer =
					FindTrainerEncounterPolicy(State, Left.TrainerId);
				const FBattleTrainerEncounterPolicy* RightTrainer =
					FindTrainerEncounterPolicy(State, Right.TrainerId);
				check(LeftTrainer != nullptr && RightTrainer != nullptr);
				const int32 LeftBand = GetDecisionSequenceBand(*LeftTrainer);
				const int32 RightBand = GetDecisionSequenceBand(*RightTrainer);
				return LeftBand == RightBand
					? Left.TrainerId < Right.TrainerId
					: LeftBand < RightBand;
			});
		return Sequence;
	}

	bool TryBuildPendingRequests(
		const FBattleEngineState& State,
		const TArray<FBattleDecisionOwnerState>& Sequence,
		const int32 OwnerIndex,
		const int32 ActorOffset,
		const uint64 StateVersion,
		const TConstArrayView<FBattleDecision> AdditionalSelections,
		TArray<FBattleDecisionRequest>& OutRequests,
		FBattleRejection& OutRejection)
	{
		OutRequests.Reset();
		OutRejection = FBattleRejection();
		if (!Sequence.IsValidIndex(OwnerIndex)
			|| ActorOffset < 0
			|| ActorOffset >= Sequence[OwnerIndex].Actors.Num())
		{
			OutRejection.Reason = EBattleRejectionReason::InvalidSetup;
			return false;
		}

		for (int32 ActorIndex = ActorOffset; ActorIndex < Sequence[OwnerIndex].Actors.Num(); ++ActorIndex)
		{
			FBattleDecisionRequest Request;
			if (!TryBuildDecisionRequest(
				State,
				Sequence[OwnerIndex].Actors[ActorIndex],
				StateVersion,
				AdditionalSelections,
				Request,
				OutRejection))
			{
				OutRequests.Reset();
				return false;
			}
			OutRequests.Add(MoveTemp(Request));
		}
		return !OutRequests.IsEmpty();
	}
}
