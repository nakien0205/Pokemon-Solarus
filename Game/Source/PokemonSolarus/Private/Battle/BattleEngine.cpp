#include "Battle/BattleEngine.h"
#include "Battle/BattleState.h"

namespace
{
	FResolutionId TakeResolutionId(FBattleEngineState& State)
	{
		FResolutionId Id;
		const bool bCreated = State.NextResolutionId > 0
			&& FResolutionId::TryCreate(State.NextResolutionId, Id);
		check(bCreated);
		++State.NextResolutionId;
		return Id;
	}

	FActionId TakeActionId(FBattleEngineState& State)
	{
		FActionId Id;
		const bool bCreated = State.NextActionId > 0
			&& FActionId::TryCreate(State.NextActionId, Id);
		check(bCreated);
		++State.NextActionId;
		return Id;
	}

	FBattleEventSource FindFallbackSource(const FBattleEngineState& State)
	{
		FBattleEventSource Source;
		const FBattleTrainerState* PlayerTrainer = State.Trainers.FindByPredicate(
			[](const FBattleTrainerState& Trainer)
			{
				return Trainer.Role == EBattleTrainerRole::Player;
			});
		if (PlayerTrainer != nullptr)
		{
			Source.TrainerId = PlayerTrainer->TrainerId;
		}
		const FBattleActivePositionState* PlayerLeft = State.ActivePositions.FindByPredicate(
			[](const FBattleActivePositionState& Position)
			{
				return Position.ActiveSlotId.GetSide() == EBattleSide::Player
					&& Position.ActiveSlotId.GetPosition() == EBattlePosition::Left;
			});
		if (PlayerLeft != nullptr)
		{
			Source.TrainerId = PlayerLeft->TrainerId;
			Source.BattlerId = PlayerLeft->BattlerId;
			Source.ActiveSlotId = PlayerLeft->ActiveSlotId;
		}
		return Source;
	}

	FBattleEventSource SourceFromRequest(
		const FBattleEngineState& State,
		const FBattleDecisionRequest* Request,
		const FBattleDecision* Decision)
	{
		FBattleEventSource Source = FindFallbackSource(State);
		if (Request != nullptr && Request->IsValid())
		{
			Source.TrainerId = Request->GetDecisionOwnerTrainerId();
			Source.BattlerId = Request->GetActingBattlerId();
			Source.ActiveSlotId = Request->GetActingSlotId();
		}
		else if (Decision != nullptr && Decision->IsValid())
		{
			Source.TrainerId = Decision->GetDecisionOwnerTrainerId();
			Source.BattlerId = Decision->GetActingBattlerId();
		}
		return Source;
	}

	FBattleEvent MakeEvent(
		FBattleEngineState& State,
		const FResolutionId ResolutionId,
		const FActionId ActionId,
		const EBattleEventType Type,
		const EBattleEventCause Cause,
		const EBattleActionKind ActionKind,
		const EBattleOutcomeCause OutcomeCause,
		const FBattleEventSource& Source)
	{
		FBattleEventSpec Spec;
		Spec.EventOrdinal = State.NextEventOrdinal;
		Spec.BattleId = State.Setup.GetBattleId();
		Spec.TurnId = State.TurnId;
		Spec.ActionId = ActionId;
		Spec.ResolutionId = ResolutionId;
		Spec.Type = Type;
		Spec.Cause = Cause;
		Spec.CauseActionKind = ActionKind;
		Spec.OutcomeCause = OutcomeCause;
		Spec.Source = Source;
		Spec.Visibility.Level = EBattleVisibilityLevel::Public;

		FBattleEvent Event;
		const bool bCreated = FBattleEvent::TryCreate(Spec, Event);
		check(bCreated);
		++State.NextEventOrdinal;
		return Event;
	}

	FBattleResolution MakeRejectedResolution(
		FBattleEngineState& State,
		const FResolutionId ResolutionId,
		const FBattleRejection& Rejection,
		const EBattleEventType EventType,
		const EBattleEventCause Cause,
		const EBattleActionKind ActionKind,
		const FBattleEventSource& Source)
	{
		FBattleResolutionSpec Spec;
		Spec.ResolutionId = ResolutionId;
		Spec.BeforeStateVersion = State.StateVersion;
		Spec.AfterStateVersion = State.StateVersion;
		Spec.bAccepted = false;
		Spec.Rejection = Rejection;
		Spec.Events.Add(MakeEvent(
			State,
			ResolutionId,
			FActionId(),
			EventType,
			Cause,
			ActionKind,
			EBattleOutcomeCause::None,
			Source));

		FBattleResolution Resolution;
		const bool bCreated = FBattleResolution::TryCreate(Spec, Resolution);
		check(bCreated);
		State.AppendResolution(Resolution);
		return Resolution;
	}

	bool ActiveSlotLess(const FActiveSlotId& Left, const FActiveSlotId& Right)
	{
		if (Left.GetSide() != Right.GetSide())
		{
			return static_cast<uint8>(Left.GetSide()) < static_cast<uint8>(Right.GetSide());
		}
		return static_cast<uint8>(Left.GetPosition()) < static_cast<uint8>(Right.GetPosition());
	}

	template <typename ElementType>
	void AddUnique(TArray<ElementType>& Values, const ElementType& Value)
	{
		if (!Values.Contains(Value))
		{
			Values.Add(Value);
		}
	}

	const FBattleActivePositionState* FindActiveForBattler(
		const FBattleEngineState& State,
		const FBattlerId BattlerId)
	{
		return State.ActivePositions.FindByPredicate(
			[BattlerId](const FBattleActivePositionState& Position)
			{
				return Position.BattlerId == BattlerId;
			});
	}

	bool IsLivingSelectableBattler(const FBattleBattlerState* Battler)
	{
		return Battler != nullptr
			&& !Battler->bEgg
			&& !Battler->bFainted
			&& !Battler->bCaptured
			&& !Battler->bRemoved;
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

	TArray<FActiveSlotId> BuildMoveTargets(
		const FBattleEngineState& State,
		const FBattleActivePositionState& ActingPosition,
		const EBattleTargetClass TargetClass)
	{
		TArray<FActiveSlotId> Targets;
		for (const FBattleActivePositionState& Position : State.ActivePositions)
		{
			if (!Position.bAvailable || !Position.BattlerId.IsValid())
			{
				continue;
			}
			const FBattleBattlerState* Target = State.FindBattler(Position.BattlerId);
			if (!IsLivingSelectableBattler(Target))
			{
				continue;
			}

			const bool bSameSide = Position.ActiveSlotId.GetSide() == ActingPosition.ActiveSlotId.GetSide();
			const bool bSelf = Position.BattlerId == ActingPosition.BattlerId;
			bool bInclude = false;
			switch (TargetClass)
			{
			case EBattleTargetClass::Self:
				bInclude = bSelf;
				break;
			case EBattleTargetClass::SelectedAlly:
				bInclude = bSameSide && !bSelf;
				break;
			case EBattleTargetClass::SelectedOpponent:
			case EBattleTargetClass::RandomLegalOpponent:
			case EBattleTargetClass::OpponentSide:
				bInclude = !bSameSide;
				break;
			case EBattleTargetClass::AnySelectedBattler:
			case EBattleTargetClass::BothSides:
			case EBattleTargetClass::FixedSpreadSet:
				bInclude = true;
				break;
			case EBattleTargetClass::UserSide:
				bInclude = bSameSide;
				break;
			case EBattleTargetClass::Field:
				bInclude = bSelf;
				break;
			default:
				break;
			}
			if (bInclude)
			{
				Targets.Add(Position.ActiveSlotId);
			}
		}
		Targets.Sort(ActiveSlotLess);
		return Targets;
	}

	bool TryBuildDecisionRequest(
		const FBattleEngineState& State,
		const FBattleDecisionActorState& Actor,
		const uint64 StateVersion,
		FBattleDecisionRequest& OutRequest,
		FBattleRejection& OutRejection)
	{
		const FBattleActivePositionState* ActingPosition = State.FindActivePosition(Actor.ActiveSlotId);
		const FBattleBattlerState* Battler = State.FindBattler(Actor.BattlerId);
		const FBattleTrainerState* Trainer = Battler != nullptr ? State.FindTrainer(Battler->TrainerId) : nullptr;
		if (!State.bHasCatalog
			|| ActingPosition == nullptr
			|| Battler == nullptr
			|| Trainer == nullptr
			|| ActingPosition->BattlerId != Battler->BattlerId
			|| ActingPosition->TrainerId != Trainer->TrainerId
			|| !IsLivingSelectableBattler(Battler))
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

		bool bMoveRejectedForNoTarget = false;
		for (const FBattleMoveSlotState& Move : Battler->Moves)
		{
			const FBattleMoveDefinition* Definition = State.Catalog.FindMove(Move.MoveId);
			if (Definition == nullptr)
			{
				AddUnavailableMove(Spec, Move.MoveId, EBattleOptionUnavailableReason::MissingCatalogReference);
				continue;
			}
			if (Move.CurrentPP <= 0)
			{
				AddUnavailableMove(Spec, Move.MoveId, EBattleOptionUnavailableReason::NoPP);
				continue;
			}

			const TArray<FActiveSlotId> Targets = BuildMoveTargets(State, *ActingPosition, Definition->TargetClass);
			if (Targets.IsEmpty())
			{
				bMoveRejectedForNoTarget = true;
				AddUnavailableMove(Spec, Move.MoveId, EBattleOptionUnavailableReason::NoLegalTarget);
				continue;
			}

			Spec.LegalMoveIds.Add(Move.MoveId);
			for (const FActiveSlotId Target : Targets)
			{
				AddUnique(Spec.LegalActiveTargets, Target);
				Spec.LegalMoveTargets.Add({Move.MoveId, Target});
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

		for (const FBattlePartySlotState& PartySlot : Trainer->PartySlots)
		{
			if (!PartySlot.BattlerId.IsValid())
			{
				AddUnavailableSwitch(Spec, PartySlot.PartySlotId, EBattleOptionUnavailableReason::EmptyPartySlot);
				continue;
			}
			const FBattleBattlerState* Candidate = State.FindBattler(PartySlot.BattlerId);
			if (FindActiveForBattler(State, PartySlot.BattlerId) != nullptr)
			{
				AddUnavailableSwitch(Spec, PartySlot.PartySlotId, EBattleOptionUnavailableReason::AlreadyActive);
			}
			else if (Candidate == nullptr || Candidate->bCaptured || Candidate->bRemoved)
			{
				AddUnavailableSwitch(Spec, PartySlot.PartySlotId, EBattleOptionUnavailableReason::Removed);
			}
			else if (Candidate->bEgg)
			{
				AddUnavailableSwitch(Spec, PartySlot.PartySlotId, EBattleOptionUnavailableReason::Egg);
			}
			else if (Candidate->bFainted)
			{
				AddUnavailableSwitch(Spec, PartySlot.PartySlotId, EBattleOptionUnavailableReason::Fainted);
			}
			else
			{
				Spec.LegalSwitchPartySlots.Add(PartySlot.PartySlotId);
			}
		}
		if (!Spec.LegalSwitchPartySlots.IsEmpty())
		{
			Spec.LegalActionKinds.Add(EBattleActionKind::Switch);
			AddUnique(Spec.LegalActiveTargets, ActingPosition->ActiveSlotId);
		}
		else
		{
			AddUnavailableAction(Spec, EBattleActionKind::Switch, EBattleOptionUnavailableReason::NoLegalTarget);
		}

		if (!State.EncounterPolicies.bBagAllowed || !Trainer->ActionAllowance.bBagActionAvailable)
		{
			AddUnavailableAction(Spec, EBattleActionKind::Bag, EBattleOptionUnavailableReason::BagRestricted);
		}
		else
		{
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
				if (Item->Kind == EBattleItemKind::Held)
				{
					AddUnavailableItem(Spec, ItemCount.ItemId, EBattleOptionUnavailableReason::WrongItemKind);
					continue;
				}
				if (Item->Kind == EBattleItemKind::Capture
					&& (!State.EncounterPolicies.bCaptureAllowed || Trainer->Role != EBattleTrainerRole::Player))
				{
					AddUnavailableItem(Spec, ItemCount.ItemId, EBattleOptionUnavailableReason::CaptureRestricted);
					continue;
				}

				const int32 PartyPairStart = Spec.LegalItemPartyTargets.Num();
				const int32 ActivePairStart = Spec.LegalItemActiveTargets.Num();
				if (Item->Kind == EBattleItemKind::Battle)
				{
					for (const FBattlePartySlotState& PartySlot : Trainer->PartySlots)
					{
						const FBattleBattlerState* Target = State.FindBattler(PartySlot.BattlerId);
						if (Target != nullptr && !Target->bEgg && !Target->bCaptured && !Target->bRemoved)
						{
							AddUnique(Spec.LegalPartyTargets, PartySlot.PartySlotId);
							Spec.LegalItemPartyTargets.Add({ItemCount.ItemId, PartySlot.PartySlotId});
						}
					}
					for (const FBattleActivePositionState& Position : State.ActivePositions)
					{
						if (Position.TrainerId == Trainer->TrainerId && Position.BattlerId.IsValid())
						{
							AddUnique(Spec.LegalActiveTargets, Position.ActiveSlotId);
							Spec.LegalItemActiveTargets.Add({ItemCount.ItemId, Position.ActiveSlotId});
						}
					}
				}
				else if (Item->Kind == EBattleItemKind::Capture)
				{
					for (const FBattleActivePositionState& Position : State.ActivePositions)
					{
						if (Position.ActiveSlotId.GetSide() != Trainer->Side
							&& IsLivingSelectableBattler(State.FindBattler(Position.BattlerId)))
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
				AddUnavailableAction(Spec, EBattleActionKind::Bag, EBattleOptionUnavailableReason::NoItemRemaining);
			}
		}

		if (State.EncounterKind == EBattleEncounterKind::Wild
			&& State.EncounterPolicies.bRunAllowed
			&& Trainer->Role == EBattleTrainerRole::Player)
		{
			Spec.LegalActionKinds.Add(EBattleActionKind::Run);
		}
		else
		{
			AddUnavailableAction(Spec, EBattleActionKind::Run, EBattleOptionUnavailableReason::RunRestricted);
		}

		return FBattleDecisionRequest::TryCreate(Spec, OutRequest, OutRejection);
	}

	int32 GetDecisionSequenceBand(const FBattleTrainerState& Trainer)
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
		for (const FBattleTrainerState& Trainer : State.Trainers)
		{
			FBattleDecisionOwnerState Owner;
			Owner.TrainerId = Trainer.TrainerId;
			Owner.Controller = Trainer.Controller;
			for (const FBattleActivePositionState& Position : State.ActivePositions)
			{
				if (Position.TrainerId == Trainer.TrainerId
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
				const FBattleTrainerState* LeftTrainer = State.FindTrainer(Left.TrainerId);
				const FBattleTrainerState* RightTrainer = State.FindTrainer(Right.TrainerId);
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

FBattleEngine::FBattleEngine(TUniquePtr<FBattleEngineState>&& InState)
	: State(MoveTemp(InState))
{
}

FBattleEngine::~FBattleEngine() = default;

bool FBattleEngine::TryCreate(
	const FBattleSetup& Setup,
	const FBattleDefinitionCatalog& Catalog,
	TUniquePtr<IBattleRandom>&& Random,
	TUniquePtr<FBattleEngine>& OutEngine,
	FBattleRejection& OutRejection)
{
	OutEngine.Reset();
	OutRejection = FBattleRejection();
	TUniquePtr<FBattleEngineState> NewState;
	EBattleStateValidationError StateError = EBattleStateValidationError::None;
	if (!FBattleEngineState::TryCreate(
		Setup,
		&Catalog,
		MoveTemp(Random),
		NewState,
		StateError))
	{
		OutRejection.Reason = EBattleRejectionReason::InvalidSetup;
		return false;
	}

	OutEngine = TUniquePtr<FBattleEngine>(new FBattleEngine(MoveTemp(NewState)));
	return true;
}

bool FBattleEngine::TryCreate(
	const FBattleSetup& Setup,
	TUniquePtr<IBattleRandom>&& Random,
	TUniquePtr<FBattleEngine>& OutEngine,
	FBattleRejection& OutRejection)
{
	OutEngine.Reset();
	OutRejection = FBattleRejection();
	TUniquePtr<FBattleEngineState> NewState;
	EBattleStateValidationError StateError = EBattleStateValidationError::None;
	if (!FBattleEngineState::TryCreate(
		Setup,
		nullptr,
		MoveTemp(Random),
		NewState,
		StateError))
	{
		OutRejection.Reason = EBattleRejectionReason::InvalidSetup;
		return false;
	}

	OutEngine = TUniquePtr<FBattleEngine>(new FBattleEngine(MoveTemp(NewState)));
	return true;
}

#if WITH_DEV_AUTOMATION_TESTS
bool FBattleEngine::TryCreateForContractFixture(
	const FBattleSetup& Setup,
	TUniquePtr<IBattleRandom>&& Random,
	const FBattleDecisionRequest& PendingRequest,
	const bool bSeedOpponentRemovalCheckpoint,
	TUniquePtr<FBattleEngine>& OutEngine,
	FBattleRejection& OutRejection)
{
	if (!PendingRequest.IsValid()
		|| PendingRequest.GetStateVersion() != 1
		|| !TryCreate(Setup, MoveTemp(Random), OutEngine, OutRejection))
	{
		if (!OutRejection.IsRejected())
		{
			OutRejection.Reason = EBattleRejectionReason::InvalidDecision;
		}
		OutEngine.Reset();
		return false;
	}

	const FBattlePartyEntrySetup* Battler = Setup.FindBattler(PendingRequest.GetActingBattlerId());
	const FBattleTrainerSetup* Trainer = Setup.FindTrainer(PendingRequest.GetDecisionOwnerTrainerId());
	const FBattleActiveAssignment* Active = Setup.FindActive(PendingRequest.GetActingSlotId());
	if (Battler == nullptr
		|| Trainer == nullptr
		|| Active == nullptr
		|| Battler->TrainerId != Trainer->TrainerId
		|| Active->BattlerId != Battler->BattlerId)
	{
		OutRejection.Reason = EBattleRejectionReason::InvalidDecision;
		OutEngine.Reset();
		return false;
	}

	OutEngine->State->PendingDecision = PendingRequest;
	OutEngine->State->Phase = bSeedOpponentRemovalCheckpoint
		? EBattlePhase::Resolving
		: EBattlePhase::Selecting;
	if (bSeedOpponentRemovalCheckpoint)
	{
		OutEngine->State->AvailableOpponentRemovalCheckpoints.Add(1);
		OutEngine->State->NextEventOrdinal = 2;
	}
	return true;
}
#endif // WITH_DEV_AUTOMATION_TESTS

bool FBattleEngine::TryBeginActionDecisionSequence(FBattleRejection& OutRejection)
{
	OutRejection = FBattleRejection();
	if (!State.IsValid() || !State->bHasCatalog)
	{
		OutRejection.Reason = EBattleRejectionReason::InvalidSetup;
		return false;
	}
	if (State->Phase == EBattlePhase::Terminal)
	{
		OutRejection.Reason = EBattleRejectionReason::TerminalState;
		return false;
	}
	if (State->Phase != EBattlePhase::Setup
		|| State->PendingDecision.IsSet()
		|| !State->PendingDecisionRequests.IsEmpty()
		|| !State->DecisionOwnerSequence.IsEmpty())
	{
		OutRejection.Reason = EBattleRejectionReason::DecisionSequenceNotStarted;
		return false;
	}

	TArray<FBattleDecisionOwnerState> Sequence = BuildDecisionOwnerSequence(*State);
	TArray<FBattleDecisionRequest> Requests;
	const uint64 NewStateVersion = State->StateVersion + 1;
	if (Sequence.IsEmpty()
		|| NewStateVersion == 0
		|| !TryBuildPendingRequests(*State, Sequence, 0, 0, NewStateVersion, Requests, OutRejection))
	{
		if (!OutRejection.IsRejected())
		{
			OutRejection.Reason = EBattleRejectionReason::InvalidSetup;
		}
		return false;
	}

	State->DecisionOwnerSequence = MoveTemp(Sequence);
	State->CurrentDecisionOwnerIndex = 0;
	State->CurrentDecisionActorOffset = 0;
	State->PendingDecisionRequests = MoveTemp(Requests);
	State->PendingDecision = State->PendingDecisionRequests[0];
	State->Phase = EBattlePhase::Selecting;
	State->StateVersion = NewStateVersion;
	return true;
}

namespace
{
	bool IsEventVisibleToTrainer(
		const FBattleEngineState& State,
		const FBattleEventVisibility& Visibility,
		const FTrainerId ObserverTrainerId)
	{
		if (Visibility.Level == EBattleVisibilityLevel::Public)
		{
			return true;
		}
		if (Visibility.Level == EBattleVisibilityLevel::OwningTrainer)
		{
			return Visibility.OwningTrainerId == ObserverTrainerId;
		}
		if (Visibility.Level == EBattleVisibilityLevel::OwningSide && Visibility.bHasOwningSide)
		{
			const FBattleTrainerState* Observer = State.FindTrainer(ObserverTrainerId);
			return Observer != nullptr && Observer->Side == Visibility.OwningSide;
		}
		return false;
	}

	bool IsDefinitionKnown(
		const FBattleEngineState& State,
		const FTrainerId ObserverTrainerId,
		const FBattlerId SubjectBattlerId,
		const EBattleKnowledgeKind Kind,
		const FDefinitionId& DefinitionId)
	{
		const FBattleBattlerState* Subject = State.FindBattler(SubjectBattlerId);
		if (Subject != nullptr && Subject->TrainerId == ObserverTrainerId)
		{
			return true;
		}
		if (State.Setup.GetKnowledgeFacts().ContainsByPredicate(
			[ObserverTrainerId, SubjectBattlerId, Kind, &DefinitionId](const FBattleKnowledgeFact& Fact)
			{
				return Fact.ObserverTrainerId == ObserverTrainerId
					&& Fact.Visibility != EBattleVisibilityLevel::CoreOnly
					&& Fact.SubjectBattlerId == SubjectBattlerId
					&& Fact.Kind == Kind
					&& Fact.DefinitionId == DefinitionId;
			}))
		{
			return true;
		}
		if (Kind == EBattleKnowledgeKind::SpeciesFormKnown
			&& State.Setup.GetKnowledgeFacts().ContainsByPredicate(
				[ObserverTrainerId, Kind, &DefinitionId](const FBattleKnowledgeFact& Fact)
				{
					return Fact.ObserverTrainerId == ObserverTrainerId
						&& Fact.Visibility != EBattleVisibilityLevel::CoreOnly
						&& Fact.Kind == Kind
						&& Fact.DefinitionId == DefinitionId;
				}))
		{
			return true;
		}

		if (Kind == EBattleKnowledgeKind::SpeciesFormKnown)
		{
			return false;
		}

		return State.OrderedEvents.ContainsByPredicate(
			[&State, ObserverTrainerId, SubjectBattlerId, Kind, &DefinitionId](const FBattleEvent& Event)
			{
				const bool bMatchingDefinitionFamily =
					(Kind == EBattleKnowledgeKind::MoveRevealed
						&& Event.GetCause() == EBattleEventCause::Move)
					|| (Kind == EBattleKnowledgeKind::ItemRevealed
						&& Event.GetCause() == EBattleEventCause::Item)
					|| (Kind == EBattleKnowledgeKind::AbilityRevealed
						&& Event.GetCause() == EBattleEventCause::Rule);
				return Event.GetVisibility().bRevealSourceDefinition
					&& bMatchingDefinitionFamily
					&& Event.GetSource().BattlerId == SubjectBattlerId
					&& Event.GetSource().DefinitionId == DefinitionId
					&& IsEventVisibleToTrainer(State, Event.GetVisibility(), ObserverTrainerId);
			});
	}

	FBattleObservedCondition ProjectCondition(const FBattleConditionState& Condition)
	{
		FBattleObservedCondition Projection;
		Projection.ConditionId = Condition.ConditionId;
		Projection.RemainingTurns = Condition.RemainingTurns;
		Projection.LayerCount = Condition.LayerCount;
		Projection.CreationOrdinal = Condition.CreationOrdinal;
		Projection.SourceBattlerId = Condition.SourceBattlerId;
		return Projection;
	}

	EBattleEffectivenessKnowledge ToKnowledge(const FBattleTypeEffectiveness& Effectiveness)
	{
		if (Effectiveness.IsImmune())
		{
			return EBattleEffectivenessKnowledge::Immune;
		}
		if (Effectiveness.Numerator == Effectiveness.Denominator)
		{
			return EBattleEffectivenessKnowledge::Neutral;
		}
		return Effectiveness.Numerator < Effectiveness.Denominator
			? EBattleEffectivenessKnowledge::NotVeryEffective
			: EBattleEffectivenessKnowledge::SuperEffective;
	}

	EBattleEffectivenessKnowledge CalculateEffectivenessKnowledge(
		const FBattleEngineState& State,
		const FTrainerId ObserverTrainerId,
		const FMoveId MoveId,
		const FActiveSlotId TargetSlotId)
	{
		const FBattleMoveDefinition* Move = State.Catalog.FindMove(MoveId);
		if (Move == nullptr)
		{
			return EBattleEffectivenessKnowledge::Unknown;
		}
		if (Move->Category == EBattleMoveCategory::Status)
		{
			return EBattleEffectivenessKnowledge::NotApplicable;
		}

		const FBattleActivePositionState* Position = State.FindActivePosition(TargetSlotId);
		const FBattleBattlerState* Target = Position != nullptr ? State.FindBattler(Position->BattlerId) : nullptr;
		if (Target == nullptr
			|| !IsDefinitionKnown(
				State,
				ObserverTrainerId,
				Target->BattlerId,
				EBattleKnowledgeKind::SpeciesFormKnown,
				Target->SpeciesFormId.GetDefinitionId()))
		{
			return EBattleEffectivenessKnowledge::Unknown;
		}

		const FBattleSpeciesFormDefinition* Species = State.Catalog.FindSpeciesForm(Target->SpeciesFormId);
		if (Species == nullptr)
		{
			return EBattleEffectivenessKnowledge::Unknown;
		}

		FBattleTypeEffectiveness Effectiveness;
		const bool bFound = Species->SecondaryType == EPokemonType::Invalid
			? State.Catalog.GetTypeChart().TryGetEffectiveness(
				Move->Type,
				Species->PrimaryType,
				Effectiveness)
			: State.Catalog.GetTypeChart().TryGetDualEffectiveness(
				Move->Type,
				Species->PrimaryType,
				Species->SecondaryType,
				Effectiveness);
		return bFound ? ToKnowledge(Effectiveness) : EBattleEffectivenessKnowledge::Unknown;
	}

	EBattleEffectivenessKnowledge SummarizeEffectiveness(
		const TArray<EBattleEffectivenessKnowledge>& Values)
	{
		if (Values.IsEmpty())
		{
			return EBattleEffectivenessKnowledge::Unknown;
		}
		for (int32 Index = 1; Index < Values.Num(); ++Index)
		{
			if (Values[Index] != Values[0])
			{
				return EBattleEffectivenessKnowledge::Varies;
			}
		}
		return Values[0];
	}
}

FBattleSnapshot FBattleEngine::BuildSnapshot(const FTrainerId* ObserverTrainerId) const
{
	FBattleSnapshot Snapshot;
	if (!State.IsValid())
	{
		return Snapshot;
	}

	const bool bFiltered = ObserverTrainerId != nullptr;
	const FBattleTrainerState* Observer = bFiltered ? State->FindTrainer(*ObserverTrainerId) : nullptr;
	if (bFiltered && Observer == nullptr)
	{
		return Snapshot;
	}

	Snapshot.bValid = true;
	Snapshot.StateVersion = State->StateVersion;
	Snapshot.BattleId = State->Setup.GetBattleId();
	Snapshot.TurnId = State->TurnId;
	Snapshot.EncounterKind = State->EncounterKind;
	Snapshot.Format = State->Format;
	Snapshot.Phase = State->Phase;
	Snapshot.Outcome = State->Outcome;
	Snapshot.OutcomeCause = State->OutcomeCause;
	Snapshot.SettingsReference = State->Setup.GetSettingsReference();
	Snapshot.CatalogReference = State->Setup.GetCatalogReference();
	Snapshot.bObserverFiltered = bFiltered;
	if (bFiltered)
	{
		Snapshot.ObserverTrainerId = *ObserverTrainerId;
	}
	else
	{
		Snapshot.Trainers = State->BuildTrainerProjection();
		Snapshot.PartyEntries = State->BuildPartyProjection();
		Snapshot.ActiveAssignments = State->BuildActiveProjection();
	}

	for (const FBattleTrainerState& Trainer : State->Trainers)
	{
		FBattleObservedTrainer Projection;
		Projection.TrainerId = Trainer.TrainerId;
		Projection.Side = Trainer.Side;
		Projection.Role = Trainer.Role;
		Projection.Controller = Trainer.Controller;
		Projection.bBagVisible = !bFiltered || Trainer.TrainerId == *ObserverTrainerId;
		if (Projection.bBagVisible)
		{
			Projection.Bag = Trainer.Bag;
		}
		Snapshot.ObservedTrainers.Add(MoveTemp(Projection));
	}

	for (const FBattleActivePositionState& Position : State->ActivePositions)
	{
		Snapshot.ObservedActiveSlots.Add(
			{Position.ActiveSlotId, Position.bAvailable, Position.TrainerId, Position.BattlerId});
	}

	for (const FBattleBattlerState& Battler : State->Battlers)
	{
		const bool bOwned = bFiltered && Battler.TrainerId == *ObserverTrainerId;
		const bool bActive = FindActiveForBattler(*State, Battler.BattlerId) != nullptr;
		if (bFiltered && !bOwned && !bActive)
		{
			continue;
		}

		FBattleObservedBattler Projection;
		Projection.TrainerId = Battler.TrainerId;
		Projection.BattlerId = Battler.BattlerId;
		Projection.bPartySlotVisible = !bFiltered || bOwned;
		if (Projection.bPartySlotVisible)
		{
			Projection.PartySlotId = Battler.PartySlotId;
		}
		Projection.SpeciesFormId = Battler.SpeciesFormId;
		Projection.Level = Battler.Level;
		Projection.CurrentHP = Battler.CurrentHP;
		Projection.MaxHP = Battler.PermanentStats.MaxHP;
		Projection.bFainted = Battler.bFainted;
		Projection.MajorStatusId = Battler.MajorStatusId;
		Projection.StatStages = Battler.Stages;

		Projection.bAbilityKnown = !bFiltered
			|| bOwned
			|| IsDefinitionKnown(
				*State,
				*ObserverTrainerId,
				Battler.BattlerId,
				EBattleKnowledgeKind::AbilityRevealed,
				Battler.AbilityId.GetDefinitionId());
		if (Projection.bAbilityKnown)
		{
			Projection.AbilityId = Battler.AbilityId;
		}

		Projection.bHeldItemKnown = !bFiltered
			|| bOwned
			|| (Battler.HeldItem.CurrentItemId.IsValid()
				&& IsDefinitionKnown(
					*State,
					*ObserverTrainerId,
					Battler.BattlerId,
					EBattleKnowledgeKind::ItemRevealed,
					Battler.HeldItem.CurrentItemId.GetDefinitionId()));
		if (Projection.bHeldItemKnown)
		{
			Projection.HeldItemId = Battler.HeldItem.CurrentItemId;
		}

		for (const FBattleMoveSlotState& Move : Battler.Moves)
		{
			const bool bMoveKnown = !bFiltered
				|| bOwned
				|| IsDefinitionKnown(
					*State,
					*ObserverTrainerId,
					Battler.BattlerId,
					EBattleKnowledgeKind::MoveRevealed,
					Move.MoveId.GetDefinitionId());
			if (!bMoveKnown)
			{
				continue;
			}
			FBattleObservedMove MoveProjection;
			MoveProjection.SlotIndex = Move.SlotIndex;
			MoveProjection.MoveId = Move.MoveId;
			MoveProjection.bPPVisible = !bFiltered || bOwned;
			if (MoveProjection.bPPVisible)
			{
				MoveProjection.CurrentPP = Move.CurrentPP;
				MoveProjection.MaxPP = Move.MaxPP;
			}
			Projection.Moves.Add(MoveProjection);
		}
		Snapshot.ObservedBattlers.Add(MoveTemp(Projection));
	}

	if (State->Field.Weather.IsSet())
	{
		Snapshot.Weather = ProjectCondition(State->Field.Weather.GetValue());
	}
	if (State->Field.Terrain.IsSet())
	{
		Snapshot.Terrain = ProjectCondition(State->Field.Terrain.GetValue());
	}
	for (const FBattleConditionState& Room : State->Field.Rooms)
	{
		Snapshot.Rooms.Add(ProjectCondition(Room));
	}
	for (const FBattleConditionState& Effect : State->Field.Effects)
	{
		Snapshot.FieldEffects.Add(ProjectCondition(Effect));
	}
	for (const FBattleSideState& Side : State->Sides)
	{
		FBattleObservedSide SideProjection;
		SideProjection.Side = Side.Side;
		for (const FBattleConditionState& Condition : Side.Conditions)
		{
			SideProjection.Conditions.Add(ProjectCondition(Condition));
		}
		for (const FBattleConditionState& Hazard : Side.Hazards)
		{
			SideProjection.Hazards.Add(ProjectCondition(Hazard));
		}
		Snapshot.ObservedSides.Add(MoveTemp(SideProjection));
	}

	if (!bFiltered
		|| (!State->PendingDecisionRequests.IsEmpty()
			&& State->PendingDecisionRequests[0].GetDecisionOwnerTrainerId() == *ObserverTrainerId))
	{
		Snapshot.PendingDecisionRequests = State->PendingDecisionRequests;
		if (!Snapshot.PendingDecisionRequests.IsEmpty())
		{
			Snapshot.PendingDecision = Snapshot.PendingDecisionRequests[0];
		}
	}
	if (Snapshot.PendingDecisionRequests.IsEmpty()
		&& State->PendingDecision.IsSet()
		&& (!bFiltered
			|| State->PendingDecision.GetValue().GetDecisionOwnerTrainerId() == *ObserverTrainerId))
	{
		Snapshot.PendingDecision = State->PendingDecision;
		Snapshot.PendingDecisionRequests.Add(State->PendingDecision.GetValue());
	}

	for (const FBattleDecision& Decision : State->AcceptedSelections)
	{
		bool bVisible = !bFiltered || Decision.GetDecisionOwnerTrainerId() == *ObserverTrainerId;
		if (bFiltered && !bVisible && Observer->Role == EBattleTrainerRole::Partner)
		{
			const FBattleTrainerState* DecisionTrainer = State->FindTrainer(Decision.GetDecisionOwnerTrainerId());
			bVisible = DecisionTrainer != nullptr
				&& DecisionTrainer->Role == EBattleTrainerRole::Player
				&& DecisionTrainer->Side == Observer->Side;
		}
		if (bVisible)
		{
			Snapshot.VisibleSelections.Add(Decision);
		}
	}

	if (bFiltered)
	{
		for (const FBattleDecisionRequest& Request : Snapshot.PendingDecisionRequests)
		{
			for (const FMoveId& MoveId : Request.GetLegalMoveIds())
			{
				TArray<EBattleEffectivenessKnowledge> TargetValues;
				for (const FBattleMoveTargetOption& Pair : Request.GetLegalMoveTargets())
				{
					if (Pair.MoveId != MoveId)
					{
						continue;
					}
					const EBattleEffectivenessKnowledge Value = CalculateEffectivenessKnowledge(
						*State,
						*ObserverTrainerId,
						MoveId,
						Pair.ActiveSlotId);
					TargetValues.Add(Value);
					Snapshot.TargetEffectivenessKnowledge.Add({MoveId, Pair.ActiveSlotId, Value});
				}
				Snapshot.MoveEffectivenessKnowledge.Add({MoveId, SummarizeEffectiveness(TargetValues)});
			}
		}
	}
	return Snapshot;
}

FBattleSnapshot FBattleEngine::GetSnapshot() const
{
	return BuildSnapshot(nullptr);
}

FBattleSnapshot FBattleEngine::GetSnapshotForObserver(const FTrainerId ObserverTrainerId) const
{
	return BuildSnapshot(&ObserverTrainerId);
}

TOptional<FBattleDecisionRequest> FBattleEngine::GetPendingDecision() const
{
	return State.IsValid() ? State->PendingDecision : TOptional<FBattleDecisionRequest>();
}

TArray<FBattleDecisionRequest> FBattleEngine::GetPendingDecisionRequests() const
{
	return State.IsValid() ? State->PendingDecisionRequests : TArray<FBattleDecisionRequest>();
}

FBattleResolution FBattleEngine::SubmitDecisionBatch(const FBattleDecisionBatch& Batch)
{
	check(State.IsValid());
	if (Batch.IsValid())
	{
		for (const FBattleDecision& Decision : Batch.GetDecisions())
		{
			State->SubmittedDecisions.Add(Decision);
		}
	}

	const FResolutionId ResolutionId = TakeResolutionId(*State);
	const FBattleDecisionRequest* FirstRequest = State->PendingDecisionRequests.IsEmpty()
		? nullptr
		: &State->PendingDecisionRequests[0];
	const FBattleDecision* FirstDecision = Batch.IsValid() && !Batch.GetDecisions().IsEmpty()
		? &Batch.GetDecisions()[0]
		: nullptr;
	const FBattleEventSource FallbackSource = SourceFromRequest(*State, FirstRequest, FirstDecision);

	FBattleRejection Rejection;
	if (State->Phase == EBattlePhase::Terminal)
	{
		Rejection.Reason = EBattleRejectionReason::TerminalState;
	}
	else if (!Batch.IsValid())
	{
		Rejection.Reason = EBattleRejectionReason::InvalidDecisionBatch;
	}
	else if (State->Phase != EBattlePhase::Selecting || State->PendingDecisionRequests.IsEmpty())
	{
		Rejection.Reason = EBattleRejectionReason::DecisionSequenceNotStarted;
	}
	else if (Batch.GetStateVersion() != State->StateVersion)
	{
		Rejection.Reason = EBattleRejectionReason::StaleStateVersion;
	}
	else if (Batch.GetRequestKind() != EBattleDecisionRequestKind::Action)
	{
		Rejection.Reason = EBattleRejectionReason::WrongRequestKind;
	}
	else if (Batch.GetDecisionOwnerTrainerId()
		!= State->PendingDecisionRequests[0].GetDecisionOwnerTrainerId())
	{
		Rejection.Reason = EBattleRejectionReason::WrongDecisionOwner;
		Rejection.TrainerId = Batch.GetDecisionOwnerTrainerId();
	}
	else if (Batch.GetDecisions().IsEmpty()
		|| Batch.GetDecisions().Num() > State->PendingDecisionRequests.Num())
	{
		Rejection.Reason = EBattleRejectionReason::WrongDecisionCount;
	}
	else
	{
		for (int32 DecisionIndex = 0; DecisionIndex < Batch.GetDecisions().Num(); ++DecisionIndex)
		{
			const FBattleDecision& Decision = Batch.GetDecisions()[DecisionIndex];
			const FBattleDecisionRequest& Request = State->PendingDecisionRequests[DecisionIndex];
			if (Decision.GetActingBattlerId() != Request.GetActingBattlerId())
			{
				Rejection.Reason = EBattleRejectionReason::WrongDecisionOrder;
				Rejection.BattlerId = Decision.GetActingBattlerId();
				break;
			}
			if (!Request.Allows(Decision, Rejection))
			{
				break;
			}
		}
	}

	if (Rejection.IsRejected())
	{
		return MakeRejectedResolution(
			*State,
			ResolutionId,
			Rejection,
			EBattleEventType::DecisionRejected,
			EBattleEventCause::Decision,
			FirstDecision != nullptr ? FirstDecision->GetActionKind() : EBattleActionKind::Fight,
			FallbackSource);
	}

	const uint64 BeforeStateVersion = State->StateVersion;
	const uint64 AfterStateVersion = BeforeStateVersion + 1;
	if (AfterStateVersion == 0)
	{
		Rejection.Reason = EBattleRejectionReason::InvalidDecision;
		return MakeRejectedResolution(
			*State,
			ResolutionId,
			Rejection,
			EBattleEventType::DecisionRejected,
			EBattleEventCause::Decision,
			FirstDecision->GetActionKind(),
			FallbackSource);
	}

	int32 NextOwnerIndex = State->CurrentDecisionOwnerIndex;
	int32 NextActorOffset = State->CurrentDecisionActorOffset + Batch.GetDecisions().Num();
	if (!State->DecisionOwnerSequence.IsValidIndex(NextOwnerIndex))
	{
		Rejection.Reason = EBattleRejectionReason::InvalidDecision;
		return MakeRejectedResolution(
			*State,
			ResolutionId,
			Rejection,
			EBattleEventType::DecisionRejected,
			EBattleEventCause::Decision,
			FirstDecision->GetActionKind(),
			FallbackSource);
	}
	if (NextActorOffset >= State->DecisionOwnerSequence[NextOwnerIndex].Actors.Num())
	{
		++NextOwnerIndex;
		NextActorOffset = 0;
	}

	TArray<FBattleDecisionRequest> NextRequests;
	if (State->DecisionOwnerSequence.IsValidIndex(NextOwnerIndex)
		&& !TryBuildPendingRequests(
			*State,
			State->DecisionOwnerSequence,
			NextOwnerIndex,
			NextActorOffset,
			AfterStateVersion,
			NextRequests,
			Rejection))
	{
		if (!Rejection.IsRejected())
		{
			Rejection.Reason = EBattleRejectionReason::InvalidDecision;
		}
		return MakeRejectedResolution(
			*State,
			ResolutionId,
			Rejection,
			EBattleEventType::DecisionRejected,
			EBattleEventCause::Decision,
			FirstDecision->GetActionKind(),
			FallbackSource);
	}

	TArray<FBattleEvent> Events;
	for (int32 DecisionIndex = 0; DecisionIndex < Batch.GetDecisions().Num(); ++DecisionIndex)
	{
		const FBattleDecision& Decision = Batch.GetDecisions()[DecisionIndex];
		const FBattleDecisionRequest& Request = State->PendingDecisionRequests[DecisionIndex];
		State->AcceptedSelections.Add(Decision);
		Events.Add(MakeEvent(
			*State,
			ResolutionId,
			FActionId(),
			EBattleEventType::DecisionAccepted,
			EBattleEventCause::Decision,
			Decision.GetActionKind(),
			EBattleOutcomeCause::None,
			SourceFromRequest(*State, &Request, &Decision)));
	}

	State->StateVersion = AfterStateVersion;
	State->CurrentDecisionOwnerIndex = NextOwnerIndex;
	State->CurrentDecisionActorOffset = NextActorOffset;
	State->PendingDecisionRequests = MoveTemp(NextRequests);
	if (State->PendingDecisionRequests.IsEmpty())
	{
		State->PendingDecision.Reset();
		State->Phase = EBattlePhase::Locked;
	}
	else
	{
		State->PendingDecision = State->PendingDecisionRequests[0];
	}

	FBattleResolutionSpec ResolutionSpec;
	ResolutionSpec.ResolutionId = ResolutionId;
	ResolutionSpec.BeforeStateVersion = BeforeStateVersion;
	ResolutionSpec.AfterStateVersion = State->StateVersion;
	ResolutionSpec.bAccepted = true;
	ResolutionSpec.Events = MoveTemp(Events);
	FBattleResolution Resolution;
	const bool bResolutionCreated = FBattleResolution::TryCreate(ResolutionSpec, Resolution);
	check(bResolutionCreated);
	State->AppendResolution(Resolution);
	return Resolution;
}

FBattleResolution FBattleEngine::SubmitDecision(const FBattleDecision& Decision)
{
	check(State.IsValid());
	if (State->Phase == EBattlePhase::Selecting && !State->PendingDecisionRequests.IsEmpty())
	{
		FBattleDecisionBatchSpec BatchSpec;
		BatchSpec.StateVersion = Decision.GetStateVersion();
		BatchSpec.RequestKind = Decision.GetRequestKind();
		BatchSpec.DecisionOwnerTrainerId = Decision.GetDecisionOwnerTrainerId();
		BatchSpec.Decisions.Add(Decision);
		FBattleDecisionBatch Batch;
		FBattleRejection BatchRejection;
		if (FBattleDecisionBatch::TryCreate(BatchSpec, Batch, BatchRejection))
		{
			return SubmitDecisionBatch(Batch);
		}
	}
	if (Decision.IsValid())
	{
		State->SubmittedDecisions.Add(Decision);
	}

	const FResolutionId ResolutionId = TakeResolutionId(*State);
	const FBattleDecisionRequest* Request = State->PendingDecision.IsSet()
		? &State->PendingDecision.GetValue()
		: nullptr;
	const FBattleEventSource Source = SourceFromRequest(*State, Request, &Decision);
	const EBattleActionKind ActionKind = Decision.IsValid()
		? Decision.GetActionKind()
		: EBattleActionKind::Fight;

	FBattleRejection Rejection;
	if (State->Phase == EBattlePhase::Terminal)
	{
		Rejection.Reason = EBattleRejectionReason::TerminalState;
		return MakeRejectedResolution(
			*State,
			ResolutionId,
			Rejection,
			EBattleEventType::DecisionRejected,
			EBattleEventCause::Decision,
			ActionKind,
			Source);
	}
	if (!Decision.IsValid())
	{
		Rejection.Reason = EBattleRejectionReason::InvalidDecision;
		return MakeRejectedResolution(
			*State,
			ResolutionId,
			Rejection,
			EBattleEventType::DecisionRejected,
			EBattleEventCause::Decision,
			ActionKind,
			Source);
	}
	if (Request == nullptr)
	{
		Rejection.Reason = EBattleRejectionReason::NoPendingDecision;
		return MakeRejectedResolution(
			*State,
			ResolutionId,
			Rejection,
			EBattleEventType::DecisionRejected,
			EBattleEventCause::Decision,
			ActionKind,
			Source);
	}
	if (!Request->Allows(Decision, Rejection))
	{
		return MakeRejectedResolution(
			*State,
			ResolutionId,
			Rejection,
			EBattleEventType::DecisionRejected,
			EBattleEventCause::Decision,
			ActionKind,
			Source);
	}
	if (ActionKind != EBattleActionKind::ScriptedEnd && ActionKind != EBattleActionKind::Abandon)
	{
		Rejection.Reason = EBattleRejectionReason::IllegalAction;
		return MakeRejectedResolution(
			*State,
			ResolutionId,
			Rejection,
			EBattleEventType::DecisionRejected,
			EBattleEventCause::Decision,
			ActionKind,
			Source);
	}

	const uint64 BeforeStateVersion = State->StateVersion;
	const FActionId ActionId = TakeActionId(*State);
	TArray<FBattleEvent> Events;
	Events.Add(MakeEvent(*State, ResolutionId, ActionId, EBattleEventType::DecisionAccepted, EBattleEventCause::Decision, ActionKind, EBattleOutcomeCause::None, Source));
	Events.Add(MakeEvent(*State, ResolutionId, ActionId, EBattleEventType::ActionLocked, EBattleEventCause::Action, ActionKind, EBattleOutcomeCause::None, Source));
	Events.Add(MakeEvent(*State, ResolutionId, ActionId, EBattleEventType::ActionStarted, EBattleEventCause::Action, ActionKind, EBattleOutcomeCause::None, Source));
	Events.Add(MakeEvent(*State, ResolutionId, ActionId, EBattleEventType::ScriptedAction, EBattleEventCause::Scripted, ActionKind, EBattleOutcomeCause::None, Source));
	Events.Add(MakeEvent(*State, ResolutionId, ActionId, EBattleEventType::ActionCompleted, EBattleEventCause::Action, ActionKind, EBattleOutcomeCause::None, Source));

	State->Phase = EBattlePhase::Terminal;
	State->Outcome = ActionKind == EBattleActionKind::Abandon
		? EBattleOutcome::Abandoned
		: EBattleOutcome::ScriptedEnd;
	State->OutcomeCause = EBattleOutcomeCause::Ordinary;
	State->PendingDecision.Reset();
	++State->StateVersion;
	Events.Add(MakeEvent(*State, ResolutionId, ActionId, EBattleEventType::BattleEnded, EBattleEventCause::Outcome, ActionKind, State->OutcomeCause, Source));

	FBattleResolutionSpec ResolutionSpec;
	ResolutionSpec.ResolutionId = ResolutionId;
	ResolutionSpec.BeforeStateVersion = BeforeStateVersion;
	ResolutionSpec.AfterStateVersion = State->StateVersion;
	ResolutionSpec.bAccepted = true;
	ResolutionSpec.Events = MoveTemp(Events);
	FBattleResolution Resolution;
	const bool bResolutionCreated = FBattleResolution::TryCreate(ResolutionSpec, Resolution);
	check(bResolutionCreated);
	State->AppendResolution(Resolution);
	return Resolution;
}

FBattleResolution FBattleEngine::ApplyBetweenActionsStatRefresh(
	const FBattleBetweenActionsStatRefresh& Refresh)
{
	check(State.IsValid());
	if (Refresh.IsValid())
	{
		State->SubmittedStatRefreshes.Add(Refresh);
	}

	const FResolutionId ResolutionId = TakeResolutionId(*State);
	FBattleEventSource Source = FindFallbackSource(*State);
	Source.BattlerId = Refresh.BattlerId;
	const FBattleBattlerState* Existing = State->FindBattler(Refresh.BattlerId);
	if (Existing != nullptr)
	{
		Source.TrainerId = Existing->TrainerId;
	}

	FBattleRejection Rejection;
	if (State->Phase == EBattlePhase::Terminal)
	{
		Rejection.Reason = EBattleRejectionReason::TerminalState;
	}
	else if (!Refresh.IsValid())
	{
		Rejection.Reason = EBattleRejectionReason::InvalidDecision;
	}
	else if (Refresh.StateVersion != State->StateVersion)
	{
		Rejection.Reason = EBattleRejectionReason::StaleStateVersion;
	}
	else if (State->Phase != EBattlePhase::Resolving)
	{
		Rejection.Reason = EBattleRejectionReason::RefreshNotAllowed;
	}
	else if (!State->AvailableOpponentRemovalCheckpoints.Contains(Refresh.OpponentRemovalCheckpointEventOrdinal))
	{
		Rejection.Reason = EBattleRejectionReason::InvalidCheckpoint;
	}
	else if (Existing == nullptr)
	{
		Rejection.Reason = EBattleRejectionReason::WrongActingBattler;
		Rejection.BattlerId = Refresh.BattlerId;
	}

	if (Rejection.IsRejected())
	{
		return MakeRejectedResolution(
			*State,
			ResolutionId,
			Rejection,
			EBattleEventType::StatRefreshRejected,
			EBattleEventCause::StatRefresh,
			EBattleActionKind::Fight,
			Source);
	}

	const uint64 BeforeStateVersion = State->StateVersion;
	FBattleBattlerState* MutableEntry = State->FindMutableBattler(Refresh.BattlerId);
	check(MutableEntry != nullptr);
	const int32 PreviousLevel = MutableEntry->Level;
	MutableEntry->Level = Refresh.NewLevel;
	MutableEntry->PermanentStats = Refresh.NewStats;
	MutableEntry->CurrentHP = Refresh.NewCurrentHP;
	MutableEntry->bFainted = Refresh.NewCurrentHP == 0;
	State->AvailableOpponentRemovalCheckpoints.RemoveSingle(Refresh.OpponentRemovalCheckpointEventOrdinal);
	++State->StateVersion;

	FBattleEvent Event = MakeEvent(
		*State,
		ResolutionId,
		FActionId(),
		EBattleEventType::StatRefreshApplied,
		EBattleEventCause::StatRefresh,
		EBattleActionKind::Fight,
		EBattleOutcomeCause::None,
		Source);
	FBattleEventSpec EventSpec;
	EventSpec.EventOrdinal = Event.GetEventOrdinal();
	EventSpec.BattleId = Event.GetBattleId();
	EventSpec.TurnId = Event.GetTurnId();
	EventSpec.ResolutionId = Event.GetResolutionId();
	EventSpec.Type = Event.GetType();
	EventSpec.Cause = Event.GetCause();
	EventSpec.CauseActionKind = Event.GetCauseActionKind();
	EventSpec.Source = Event.GetSource();
	EventSpec.NumericBefore = PreviousLevel;
	EventSpec.NumericAfter = Refresh.NewLevel;
	EventSpec.NumericDelta = Refresh.NewLevel - PreviousLevel;
	EventSpec.Visibility = Event.GetVisibility();
	const bool bEventCreated = FBattleEvent::TryCreate(EventSpec, Event);
	check(bEventCreated);

	FBattleResolutionSpec ResolutionSpec;
	ResolutionSpec.ResolutionId = ResolutionId;
	ResolutionSpec.BeforeStateVersion = BeforeStateVersion;
	ResolutionSpec.AfterStateVersion = State->StateVersion;
	ResolutionSpec.bAccepted = true;
	ResolutionSpec.Events.Add(Event);
	FBattleResolution Resolution;
	const bool bResolutionCreated = FBattleResolution::TryCreate(ResolutionSpec, Resolution);
	check(bResolutionCreated);
	State->AppendResolution(Resolution);
	return Resolution;
}

FBattleReplayInputs FBattleEngine::ExportReplayInputs() const
{
	FBattleReplayInputs Inputs;
	if (State.IsValid())
	{
		Inputs.Setup = State->Setup;
		Inputs.Decisions = State->SubmittedDecisions;
		Inputs.StatRefreshes = State->SubmittedStatRefreshes;
	}
	return Inputs;
}

TArray<FBattleRandomDraw> FBattleEngine::ExportRandomTrace() const
{
	TArray<FBattleRandomDraw> Trace;
	if (State.IsValid() && State->Random.IsValid())
	{
		for (const FBattleRandomDraw& Draw : State->Random->GetTrace())
		{
			Trace.Add(Draw);
		}
	}
	return Trace;
}

FBattleReplayRecord FBattleEngine::ExportReplayRecord() const
{
	FBattleReplayRecord Record;
	if (!State.IsValid())
	{
		return Record;
	}
	const TArray<FBattleRandomDraw> Trace = ExportRandomTrace();
	const bool bCreated = FBattleReplayRecord::TryCreate(
		FBattleReplayRecord::CurrentSchemaVersion,
		ExportReplayInputs(),
		State->Resolutions,
		Trace,
		GetSnapshot(),
		Record);
	ensure(bCreated);
	return Record;
}

const FBattlePartyEntrySetup* FBattleSnapshot::FindBattler(const FBattlerId BattlerId) const
{
	return PartyEntries.FindByPredicate(
		[BattlerId](const FBattlePartyEntrySetup& Entry)
		{
			return Entry.BattlerId == BattlerId;
		});
}

const FBattleObservedBattler* FBattleSnapshot::FindObservedBattler(const FBattlerId BattlerId) const
{
	return ObservedBattlers.FindByPredicate(
		[BattlerId](const FBattleObservedBattler& Entry)
		{
			return Entry.BattlerId == BattlerId;
		});
}
