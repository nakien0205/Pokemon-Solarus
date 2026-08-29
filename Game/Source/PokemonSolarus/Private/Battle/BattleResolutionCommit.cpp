#include "BattleResolutionCommit.h"

#include "Battle/BattleState.h"

bool FBattleResolutionCommit::TryCaptureIdentity(
	const FBattleEngineState& State,
	const FResolutionId ResolutionId,
	const FActionId OwningActionId,
	FBattleResolutionCommitIdentity& OutIdentity)
{
	OutIdentity = FBattleResolutionCommitIdentity();
	if (!ResolutionId.IsValid()
		|| !OwningActionId.IsValid()
		|| State.StateVersion == 0
		|| !State.LockedActions.IsValidIndex(State.CurrentLockedActionIndex)
		|| !State.Random.IsValid()
		|| State.NextResolutionId == 0
		|| State.NextEventOrdinal == 0)
	{
		return false;
	}

	const FBattleLockedActionState& Action =
		State.LockedActions[State.CurrentLockedActionIndex];
	if (State.Phase != EBattlePhase::Resolving
		|| Action.ActionId != OwningActionId
		|| !Action.bStarted
		|| Action.bFinished)
	{
		return false;
	}

	OutIdentity.ResolutionId = ResolutionId;
	OutIdentity.OwningActionId = OwningActionId;
	OutIdentity.ExpectedStateVersion = State.StateVersion;
	OutIdentity.ExpectedLockedActionIndex = State.CurrentLockedActionIndex;
	OutIdentity.ExpectedNextResolutionId = State.NextResolutionId;
	OutIdentity.ExpectedEventOrdinal = State.NextEventOrdinal;
	OutIdentity.ExpectedResolutionCount = State.Resolutions.Num();
	OutIdentity.ExpectedRandomTraceCount = State.Random->GetTrace().Num();
	OutIdentity.ExpectedMoveRedirections = State.MoveRedirectionRegistrations;
	OutIdentity.ExpectedAllyActionPowerModifiers =
		State.AllyActionPowerModifierRegistrations;
	return true;
}

bool FBattleResolutionCommit::IsIdentityCurrent(
	const FBattleEngineState& State,
	const FBattleResolutionCommitIdentity& Identity)
{
	if (!Identity.ResolutionId.IsValid()
		|| !Identity.OwningActionId.IsValid()
		|| State.Phase != EBattlePhase::Resolving
		|| State.StateVersion != Identity.ExpectedStateVersion
		|| State.CurrentLockedActionIndex != Identity.ExpectedLockedActionIndex
		|| State.NextResolutionId != Identity.ExpectedNextResolutionId
		|| State.NextEventOrdinal != Identity.ExpectedEventOrdinal
		|| State.Resolutions.Num() != Identity.ExpectedResolutionCount
		|| !State.Random.IsValid()
		|| State.Random->GetTrace().Num() != Identity.ExpectedRandomTraceCount
		|| !FBattleMoveRedirection::AreRegistrationsIdentical(
			State.MoveRedirectionRegistrations,
			Identity.ExpectedMoveRedirections)
		|| !FBattleAllyActionPowerModifier::AreRegistrationsIdentical(
			State.AllyActionPowerModifierRegistrations,
			Identity.ExpectedAllyActionPowerModifiers)
		|| !State.LockedActions.IsValidIndex(Identity.ExpectedLockedActionIndex))
	{
		return false;
	}

	const FBattleLockedActionState& Action =
		State.LockedActions[Identity.ExpectedLockedActionIndex];
	return Action.ActionId == Identity.OwningActionId
		&& Action.bStarted
		&& !Action.bFinished;
}

bool FBattleResolutionCommit::TryBeginAcceptedPlan(
	const FBattleResolutionCommitIdentity& Identity,
	FBattleResolutionCommitPlan& OutPlan)
{
	OutPlan = FBattleResolutionCommitPlan();
	if (!Identity.ResolutionId.IsValid()
		|| !Identity.OwningActionId.IsValid()
		|| Identity.ExpectedStateVersion == 0
		|| Identity.ExpectedEventOrdinal == 0
		|| Identity.ExpectedEventOrdinal == TNumericLimits<uint64>::Max())
	{
		return false;
	}

	OutPlan.Identity = Identity;
	OutPlan.StartingEventOrdinal = Identity.ExpectedEventOrdinal;
	OutPlan.NextEventOrdinal = Identity.ExpectedEventOrdinal;
	return true;
}

bool FBattleResolutionCommit::TryStageEvent(
	FBattleResolutionCommitPlan& Plan,
	FBattleEventSpec EventSpec)
{
	if (Plan.NextEventOrdinal == 0
		|| Plan.NextEventOrdinal == TNumericLimits<uint64>::Max()
		|| EventSpec.ResolutionId != Plan.Identity.ResolutionId)
	{
		return false;
	}

	EventSpec.EventOrdinal = Plan.NextEventOrdinal;
	FBattleEvent Event;
	if (!FBattleEvent::TryCreate(EventSpec, Event))
	{
		return false;
	}

	Plan.Events.Add(MoveTemp(Event));
	++Plan.NextEventOrdinal;
	return true;
}

bool FBattleResolutionCommit::TryFinishAcceptedPlan(FBattleResolutionCommitPlan& Plan)
{
	if (Plan.Events.IsEmpty()
		|| Plan.Identity.ExpectedStateVersion == TNumericLimits<uint64>::Max())
	{
		return false;
	}

	FBattleResolutionSpec Spec;
	Spec.ResolutionId = Plan.Identity.ResolutionId;
	Spec.BeforeStateVersion = Plan.Identity.ExpectedStateVersion;
	Spec.AfterStateVersion = Plan.Identity.ExpectedStateVersion + 1;
	Spec.bAccepted = true;
	Spec.Events = Plan.Events;
	return FBattleResolution::TryCreate(Spec, Plan.Resolution);
}

bool FBattleResolutionCommit::TryBuildRejectedPlan(
	const FBattleEngineState& State,
	const FResolutionId ResolutionId,
	const FActionId OwningActionId,
	const EBattleRejectionReason Reason,
	const FTrainerId TrainerId,
	const FBattlerId BattlerId,
	const EBattleActionKind ActionKind,
	const FBattleEventSource& Source,
	FBattleResolutionCommitPlan& OutPlan)
{
	OutPlan = FBattleResolutionCommitPlan();
	if (!ResolutionId.IsValid()
		|| !OwningActionId.IsValid()
		|| Reason == EBattleRejectionReason::None
		|| State.StateVersion == 0
		|| State.NextEventOrdinal == 0
		|| State.NextEventOrdinal == TNumericLimits<uint64>::Max())
	{
		return false;
	}

	OutPlan.Identity.ResolutionId = ResolutionId;
	OutPlan.Identity.OwningActionId = OwningActionId;
	OutPlan.StartingEventOrdinal = State.NextEventOrdinal;
	OutPlan.NextEventOrdinal = State.NextEventOrdinal;

	FBattleEventSpec EventSpec;
	EventSpec.BattleId = State.Setup.GetBattleId();
	EventSpec.TurnId = State.TurnId;
	EventSpec.ActionId = OwningActionId;
	EventSpec.ResolutionId = ResolutionId;
	EventSpec.Type = EBattleEventType::ActionCanceled;
	EventSpec.Cause = EBattleEventCause::Rule;
	EventSpec.CauseActionKind = ActionKind;
	EventSpec.Source = Source;
	EventSpec.Visibility.Level = EBattleVisibilityLevel::Public;
	if (!TryStageEvent(OutPlan, MoveTemp(EventSpec)))
	{
		return false;
	}

	FBattleResolutionSpec ResolutionSpec;
	ResolutionSpec.ResolutionId = ResolutionId;
	ResolutionSpec.BeforeStateVersion = State.StateVersion;
	ResolutionSpec.AfterStateVersion = State.StateVersion;
	ResolutionSpec.bAccepted = false;
	ResolutionSpec.Rejection.Reason = Reason;
	ResolutionSpec.Rejection.TrainerId = TrainerId;
	ResolutionSpec.Rejection.BattlerId = BattlerId;
	ResolutionSpec.Rejection.ActionId = OwningActionId;
	ResolutionSpec.Events = OutPlan.Events;
	return FBattleResolution::TryCreate(ResolutionSpec, OutPlan.Resolution);
}

FBattleResolution FBattleResolutionCommit::PublishPrepared(
	FBattleEngineState& State,
	const FBattleResolutionCommitPlan& Plan)
{
	check(Plan.Resolution.IsValid());
	check(Plan.StartingEventOrdinal == State.NextEventOrdinal);
	check(Plan.NextEventOrdinal > Plan.StartingEventOrdinal);

	if (Plan.Resolution.WasAccepted())
	{
		check(Plan.Resolution.GetBeforeStateVersion() == State.StateVersion);
		State.StateVersion = Plan.Resolution.GetAfterStateVersion();
	}
	else
	{
		check(Plan.Resolution.GetBeforeStateVersion() == State.StateVersion);
		check(Plan.Resolution.GetAfterStateVersion() == State.StateVersion);
	}

	State.NextEventOrdinal = Plan.NextEventOrdinal;
	State.AppendResolution(Plan.Resolution);
	return Plan.Resolution;
}
