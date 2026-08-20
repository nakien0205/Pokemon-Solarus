#pragma once

#include "CoreMinimal.h"
#include "Battle/BattleReplay.h"

#if WITH_DEV_AUTOMATION_TESTS
class FBattleEngineContractFixture;
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

	/** Creates an engine from a fully validated setup and lifetime-safe RNG owner. */
	[[nodiscard]] static bool TryCreate(
		const FBattleSetup& Setup,
		TUniquePtr<IBattleRandom>&& Random,
		TUniquePtr<FBattleEngine>& OutEngine,
		FBattleRejection& OutRejection);

	/** Returns a deep immutable snapshot of current public facts. */
	[[nodiscard]] FBattleSnapshot GetSnapshot() const;

	/** Returns the pending decision by value, if one currently exists. */
	[[nodiscard]] TOptional<FBattleDecisionRequest> GetPendingDecision() const;

	/** Submits one typed decision and returns its accepted/rejected ordered resolution. */
	[[nodiscard]] FBattleResolution SubmitDecision(const FBattleDecision& Decision);

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
};
