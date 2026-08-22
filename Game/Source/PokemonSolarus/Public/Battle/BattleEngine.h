#pragma once

#include "CoreMinimal.h"
#include "Battle/BattleDefinitionCatalog.h"
#include "Battle/BattleReplay.h"

#if WITH_DEV_AUTOMATION_TESTS
class FBattleEngineContractFixture;
class FBattleStateTestFixture;
class FBattleSnapshotDecisionTestFixture;
class FBattleC05BEngineFixture;
class FBattleC06AEngineFixture;
class FBattleC07BEngineFixture;
class FBattleC07CEngineFixture;
class FBattleC07DEngineFixture;
class FBattleC08BEngineFixture;
#endif
class FBattleEngineState;

/** The only mutable-state owner in the plain-C++ battle core. */
class POKEMONSOLARUS_API FBattleEngine
{
public:
	/** Destroys owned state and the injected RNG through complete private types. */
	~FBattleEngine();

	FBattleEngine(const FBattleEngine&) = delete;
	FBattleEngine& operator=(const FBattleEngine&) = delete;
	FBattleEngine(FBattleEngine&&) = delete;
	FBattleEngine& operator=(FBattleEngine&&) = delete;

	/** Creates an engine from validated setup, a frozen catalog, and a lifetime-safe RNG owner. */
	[[nodiscard]] static bool TryCreate(
		const FBattleSetup& Setup,
		const FBattleDefinitionCatalog& Catalog,
		TUniquePtr<IBattleRandom>&& Random,
		TUniquePtr<FBattleEngine>& OutEngine,
		FBattleRejection& OutRejection);

	/**
	 * Creates the catalog-free C01 no-mechanics compatibility fixture.
	 * Gameplay packages that query definitions use the catalog-aware overload.
	 */
	[[nodiscard]] static bool TryCreate(
		const FBattleSetup& Setup,
		TUniquePtr<IBattleRandom>&& Random,
		TUniquePtr<FBattleEngine>& OutEngine,
		FBattleRejection& OutRejection);

	/** Returns a deep immutable snapshot of current public facts. */
	[[nodiscard]] FBattleSnapshot GetSnapshot() const;
	/** Returns a deep snapshot filtered for one exact observing Trainer. */
	[[nodiscard]] FBattleSnapshot GetSnapshotForObserver(FTrainerId ObserverTrainerId) const;

	/** Starts C03B action selection and creates the first stable owner request batch. */
	[[nodiscard]] bool TryBeginActionDecisionSequence(FBattleRejection& OutRejection);

	/** Returns the pending decision by value, if one currently exists. */
	[[nodiscard]] TOptional<FBattleDecisionRequest> GetPendingDecision() const;
	/** Returns every pending Left/Right request for the current decision owner. */
	[[nodiscard]] TArray<FBattleDecisionRequest> GetPendingDecisionRequests() const;

	/** Submits one typed decision and returns its accepted/rejected ordered resolution. */
	[[nodiscard]] FBattleResolution SubmitDecision(const FBattleDecision& Decision);
	/** Submits one or two ordered choices atomically for the current decision owner. */
	[[nodiscard]] FBattleResolution SubmitDecisionBatch(const FBattleDecisionBatch& Batch);

	/** Returns the complete core-authority locked queue by deep copy. */
	[[nodiscard]] TArray<FBattleLockedAction> GetLockedActions() const;

	/** Returns the currently started locked action, if one awaits a later resolver. */
	[[nodiscard]] TOptional<FBattleLockedAction> GetCurrentLockedAction() const;

	/**
	 * Applies actor, captured-target, and obedience gates to the next locked action.
	 * A proceeding action remains current for later pre-move and effect resolvers.
	 */
	[[nodiscard]] FBattleResolution BeginNextLockedAction();

	/** Revalidates and executes the currently started voluntary Switch action exactly once. */
	[[nodiscard]] FBattleResolution ExecuteCurrentSwitch();

	/**
	 * Commits the current Fight action after future status/volatile gates allow it.
	 * Ordinary moves spend one PP here; engine-supplied Struggle spends none.
	 */
	[[nodiscard]] FBattleResolution CommitCurrentMoveAfterPreMoveGates();

	/**
	 * Freezes the committed Fight action's final C04B targets after PP deduction.
	 * A no-target result keeps spent PP, completes that action, and exposes no hit target to C05.
	 */
	[[nodiscard]] FBattleResolution ResolveCurrentMoveTargets();

	/** Executes the current committed Fight action's frozen C05B effect descriptors exactly once. */
	[[nodiscard]] FBattleResolution ExecuteCurrentMoveEffects();

	/** Resolves the ordered C07B residual pass, replacements, or the next turn. */
	[[nodiscard]] FBattleResolution ResolveEndTurn();

	/** Applies a validated between-actions stat refresh at one unused matching checkpoint. */
	[[nodiscard]] FBattleResolution ApplyBetweenActionsStatRefresh(
		const FBattleBetweenActionsStatRefresh& Refresh);

	/** Exports the frozen setup and all submitted external inputs by deep copy. */
	[[nodiscard]] FBattleReplayInputs ExportReplayInputs() const;

	/** Exports the injected RNG trace by deep copy. */
	[[nodiscard]] TArray<FBattleRandomDraw> ExportRandomTrace() const;

	/** Exports a complete versioned replay record. */
	[[nodiscard]] FBattleReplayRecord ExportReplayRecord() const;

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FBattleEngineContractFixture;
	friend class FBattleStateTestFixture;
	friend class FBattleSnapshotDecisionTestFixture;
	friend class FBattleC05BEngineFixture;
	friend class FBattleC06AEngineFixture;
	friend class FBattleC07BEngineFixture;
	friend class FBattleC07CEngineFixture;
	friend class FBattleC07DEngineFixture;
	friend class FBattleC08BEngineFixture;
#endif

	explicit FBattleEngine(TUniquePtr<FBattleEngineState>&& InState);

#if WITH_DEV_AUTOMATION_TESTS
	[[nodiscard]] static bool TryCreateForContractFixture(
		const FBattleSetup& Setup,
		TUniquePtr<IBattleRandom>&& Random,
		const FBattleDecisionRequest& PendingRequest,
		bool bSeedOpponentRemovalCheckpoint,
		TUniquePtr<FBattleEngine>& OutEngine,
		FBattleRejection& OutRejection);
#endif

	TUniquePtr<FBattleEngineState> State;

	/** Builds either the core-authority projection or one filtered observer projection. */
	[[nodiscard]] FBattleSnapshot BuildSnapshot(const FTrainerId* ObserverTrainerId) const;
};
