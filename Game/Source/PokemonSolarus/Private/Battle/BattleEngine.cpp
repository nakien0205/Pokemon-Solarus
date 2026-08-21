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

FBattleSnapshot FBattleEngine::GetSnapshot() const
{
	FBattleSnapshot Snapshot;
	if (!State.IsValid())
	{
		return Snapshot;
	}

	Snapshot.bValid = true;
	Snapshot.StateVersion = State->StateVersion;
	Snapshot.BattleId = State->Setup.GetBattleId();
	Snapshot.TurnId = State->TurnId;
	Snapshot.Phase = State->Phase;
	Snapshot.Outcome = State->Outcome;
	Snapshot.OutcomeCause = State->OutcomeCause;
	Snapshot.SettingsReference = State->Setup.GetSettingsReference();
	Snapshot.CatalogReference = State->Setup.GetCatalogReference();
	Snapshot.Trainers = State->BuildTrainerProjection();
	Snapshot.PartyEntries = State->BuildPartyProjection();
	Snapshot.ActiveAssignments = State->BuildActiveProjection();
	Snapshot.PendingDecision = State->PendingDecision;
	return Snapshot;
}

TOptional<FBattleDecisionRequest> FBattleEngine::GetPendingDecision() const
{
	return State.IsValid() ? State->PendingDecision : TOptional<FBattleDecisionRequest>();
}

FBattleResolution FBattleEngine::SubmitDecision(const FBattleDecision& Decision)
{
	check(State.IsValid());
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
