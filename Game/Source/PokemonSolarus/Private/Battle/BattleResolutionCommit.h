#pragma once

#include "CoreMinimal.h"
#include "Battle/BattleEvent.h"
#include "BattleMoveRedirection.h"
#include "Math/NumericLimits.h"

class FBattleEngineState;

/** Stable caller-serialized identity captured for one staged resolution checkpoint. */
struct FBattleResolutionCommitIdentity
{
	FResolutionId ResolutionId;
	FActionId OwningActionId;
	uint64 ExpectedStateVersion = 0;
	int32 ExpectedLockedActionIndex = INDEX_NONE;
	uint64 ExpectedNextResolutionId = 0;
	uint64 ExpectedEventOrdinal = 0;
	int32 ExpectedResolutionCount = INDEX_NONE;
	int32 ExpectedRandomTraceCount = INDEX_NONE;
	TArray<FBattleMoveRedirectionRegistration> ExpectedMoveRedirections;
};

/** Prepared events and immutable resolution for one exact checkpoint publication. */
struct FBattleResolutionCommitPlan
{
	FBattleResolutionCommitIdentity Identity;
	uint64 StartingEventOrdinal = 0;
	uint64 NextEventOrdinal = 0;
	FBattleResolution Resolution;
	TArray<FBattleEvent> Events;
};

/** Private reusable seam for staged Battle resolution construction and publication. */
class FBattleResolutionCommit
{
public:
	/** Captures the exact current action/checkpoint identity without mutation. */
	[[nodiscard]] static bool TryCaptureIdentity(
		const FBattleEngineState& State,
		FResolutionId ResolutionId,
		FActionId OwningActionId,
		FBattleResolutionCommitIdentity& OutIdentity);

	/** Rechecks caller-serialized state, action, history, event, and RNG trace identity. */
	[[nodiscard]] static bool IsIdentityCurrent(
		const FBattleEngineState& State,
		const FBattleResolutionCommitIdentity& Identity);

	/** Starts an accepted plan at the captured event ordinal. */
	[[nodiscard]] static bool TryBeginAcceptedPlan(
		const FBattleResolutionCommitIdentity& Identity,
		FBattleResolutionCommitPlan& OutPlan);

	/** Validates and appends one event to a private plan without touching live state. */
	[[nodiscard]] static bool TryStageEvent(
		FBattleResolutionCommitPlan& Plan,
		FBattleEventSpec EventSpec);

	/** Builds the immutable accepted resolution after every event is staged. */
	[[nodiscard]] static bool TryFinishAcceptedPlan(FBattleResolutionCommitPlan& Plan);

	/** Builds one action-correlated typed checkpoint rejection against current state. */
	[[nodiscard]] static bool TryBuildRejectedPlan(
		const FBattleEngineState& State,
		FResolutionId ResolutionId,
		FActionId OwningActionId,
		EBattleRejectionReason Reason,
		FTrainerId TrainerId,
		FBattlerId BattlerId,
		EBattleActionKind ActionKind,
		const FBattleEventSource& Source,
		FBattleResolutionCommitPlan& OutPlan);

	/** Publishes an already complete plan exactly once; no gameplay work occurs here. */
	[[nodiscard]] static FBattleResolution PublishPrepared(
		FBattleEngineState& State,
		const FBattleResolutionCommitPlan& Plan);
};
