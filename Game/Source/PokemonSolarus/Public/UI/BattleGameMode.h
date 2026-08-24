#pragma once

#include "CoreMinimal.h"
#include "Battle/BattleEngine.h"
#include "Battle/BattleIdentifiers.h"
#include "Battle/BattleRuntimeSource.h"
#include "GameFramework/GameModeBase.h"
#include "BattleGameMode.generated.h"

#if WITH_DEV_AUTOMATION_TESTS
class FBattleRuntimePresentationTestFixture;
#endif
class ABattlePlayerController;
class FBattleDecisionRequest;
class FBattleSnapshot;
class UDataTable;
struct FBattleHUDDisplayState;

/** Minimal GameMode for isolated battle levels. */
UCLASS()
class POKEMONSOLARUS_API ABattleGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	/** Configures the isolated Battle level composition root. */
	ABattleGameMode();
	/** Releases plain Battle runtime dependencies outside generated code. */
	virtual ~ABattleGameMode() override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FBattleRuntimePresentationTestFixture;
#endif

	[[nodiscard]] bool InitializeBattleRuntime();
	void ResetBattleRuntime();
	void EnsureBattleRuntimeSource();
	[[nodiscard]] bool TryCreateStartedRuntimeBundle(
		FBattleRuntimeBundle& OutBundle,
		FString& OutError) const;
	void AttachToBattlePlayerController(ABattlePlayerController& PlayerController);
	void DetachFromBattlePlayerController();
	void HandleBattleHUDAvailable(ABattlePlayerController& PlayerController);
	bool RefreshBattleHUDPresentation();
	/** Compatibility seam retained for focused native presentation tests. */
	bool RefreshCommandSelectionPresentation();
	[[nodiscard]] bool TryGetCurrentPresentationContext(
		FBattleSnapshot& OutSnapshot,
		FBattleDecisionRequest& OutRequest,
		FString& OutError) const;
	[[nodiscard]] bool TryFindLocalActionRequest(
		const FBattleSnapshot& Snapshot,
		const FBattleDecisionRequest*& OutRequest,
		FString& OutError) const;
	[[nodiscard]] bool TryApplyHUDDisplayState(
		ABattlePlayerController& PlayerController,
		const FBattleSnapshot& Snapshot,
		const FBattleDecisionRequest& Request,
		FString& OutError) const;
	void DisableInvalidPresentation(ABattlePlayerController& PlayerController);
	[[nodiscard]] bool RejectPresentationUpdate(
		ABattlePlayerController& PlayerController,
		const FString& Error);
	[[nodiscard]] bool IsSamePresentedRequest(
		const FBattleDecisionRequest& Request,
		uint64 HUDGeneration) const;
	void RememberPresentedRequest(
		const FBattleDecisionRequest& Request,
		uint64 HUDGeneration);
	void ResetPresentedRequest();
	void SetBattleRuntimeSourceForTesting(
		TUniquePtr<IBattleRuntimeSource>&& RuntimeSource);

	/** Soft composition-root reference for the authored initial Battle scenario. */
	UPROPERTY(EditDefaultsOnly, Category = "Battle|Runtime")
	TSoftObjectPtr<UDataTable> BattleRuntimeScenarioTable;

	TUniquePtr<IBattleRuntimeSource> BattleRuntimeSource;
	TUniquePtr<FBattleEngine> BattleEngine;
	FTrainerId LocalTrainerId;
	TSharedPtr<const IBattleDisplayNameResolver> BattleDisplayNames;
	TWeakObjectPtr<ABattlePlayerController> BattlePlayerController;
	FDelegateHandle BattleHUDAvailableHandle;

	bool bHasPresentedRequest = false;
	uint64 PresentedHUDGeneration = 0;
	uint64 PresentedStateVersion = 0;
	FTrainerId PresentedTrainerId;
	FBattlerId PresentedBattlerId;
	FActiveSlotId PresentedActiveSlotId;
};
