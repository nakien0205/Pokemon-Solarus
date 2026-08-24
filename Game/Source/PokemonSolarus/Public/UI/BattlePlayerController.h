#pragma once

#include "CoreMinimal.h"
#include "Battle/BattleIdentifiers.h"
#include "GameFramework/PlayerController.h"
#include "BattlePlayerController.generated.h"

#if WITH_DEV_AUTOMATION_TESTS
class FBattleHUDProductionLifecycleTestFixture;
class FBattlePresentationAdapterTestFixture;
class FBattleRuntimePresentationTestFixture;
#endif
class FBattleSnapshot;
class UBattleHUDWidget;
class UEnhancedInputComponent;
class UInputAction;
class UInputMappingContext;
struct FBattleHUDDisplayState;
struct FInputActionValue;

class ABattlePlayerController;
DECLARE_MULTICAST_DELEGATE_OneParam(
	FOnBattleHUDAvailableNative,
	ABattlePlayerController&);
using FOnBattleHUDReadyNative = FOnBattleHUDAvailableNative;

/** Battle-only local player input and HUD owner. */
UCLASS()
class POKEMONSOLARUS_API ABattlePlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ABattlePlayerController();

	UFUNCTION(BlueprintPure, Category = "Battle|UI")
	UBattleHUDWidget* GetBattleHUDWidget() const { return BattleHUDWidget; }

	/** Converts one observer-safe pending action request and applies it to the owned HUD. */
	[[nodiscard]] bool PresentCommandSelection(
		const FBattleSnapshot& ObserverSnapshot,
		FActiveSlotId ActingSlotId);

	/** Atomically applies a complete display state and reveals the structurally ready HUD. */
	[[nodiscard]] bool ApplyBattleHUDDisplayState(
		const FBattleHUDDisplayState& DisplayState);

	/** Returns whether the HUD is constructed, bound, and present in the local viewport. */
	[[nodiscard]] bool IsBattleHUDAvailable() const;

	/** Compatibility alias for IsBattleHUDAvailable. */
	[[nodiscard]] bool IsBattleHUDReady() const;

	/** Returns whether a validated full presentation is currently visible. */
	[[nodiscard]] bool IsBattleHUDVisible() const;

	/** Returns whether local command input may currently reach the command widget. */
	[[nodiscard]] bool IsBattleCommandInputReady() const;

	/** Identifies the current successfully constructed and added HUD instance. */
	[[nodiscard]] uint64 GetBattleHUDPresentationGeneration() const
	{
		return BattleHUDPresentationGeneration;
	}

	/** Gates command navigation, confirmation, and requests without replacing visible state. */
	void DisableBattleHUDInputPreservingPresentation();

	/** Hides command input without exposing Battle state to the HUD. */
	void DismissCommandSelection();

	/** Native-only structural-availability signal consumed by the Battle runtime owner. */
	FOnBattleHUDAvailableNative& GetBattleHUDAvailableNativeDelegate()
	{
		return BattleHUDAvailableNativeDelegate;
	}

	/** Compatibility alias for GetBattleHUDAvailableNativeDelegate. */
	FOnBattleHUDReadyNative& GetBattleHUDReadyNativeDelegate()
	{
		return BattleHUDAvailableNativeDelegate;
	}

protected:
	virtual void BeginPlay() override;
	virtual void ReceivedPlayer() override;
	virtual void SetupInputComponent() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditDefaultsOnly, Category = "Battle|UI")
	TSoftClassPtr<UBattleHUDWidget> BattleHUDWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "Battle|Input")
	TSoftObjectPtr<UInputMappingContext> BattleInputMappingContext;

	UPROPERTY(EditDefaultsOnly, Category = "Battle|Input")
	TSoftObjectPtr<UInputAction> BattleNavigateAction;

	UPROPERTY(EditDefaultsOnly, Category = "Battle|Input")
	TSoftObjectPtr<UInputAction> BattleConfirmAction;

	UPROPERTY(EditDefaultsOnly, Category = "Battle|Input")
	TSoftObjectPtr<UInputAction> BattleCancelAction;

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FBattleHUDProductionLifecycleTestFixture;
	friend class FBattlePresentationAdapterTestFixture;
	friend class FBattleRuntimePresentationTestFixture;
#endif

	void HandleBattleNavigate(const FInputActionValue& InputValue);
	void HandleBattleConfirm();
	void HandleBattleCancel();
	[[nodiscard]] bool EnsureCommandPresentationReady();
	void InitializeLocalBattlePresentation();
	void InitializeBattleInputMapping();
	void BindBattleNavigateAction(UEnhancedInputComponent& EnhancedInputComponent);
	void BindBattleConfirmAction(UEnhancedInputComponent& EnhancedInputComponent);
	void BindBattleCancelAction(UEnhancedInputComponent& EnhancedInputComponent);
	[[nodiscard]] bool TryCreateBattleHUD();
	[[nodiscard]] UBattleHUDWidget* CreateBattleHUDCandidate();
	[[nodiscard]] static bool TryAttachAndValidateBattleHUD(
		UBattleHUDWidget& CandidateHUD);
	void BindBattleHUDLifecycle(UBattleHUDWidget& HUDWidget);
	void UnbindBattleHUDLifecycle(UBattleHUDWidget& HUDWidget);
	void HandleBattleHUDConstructed(UBattleHUDWidget& ConstructedHUD);
	void FinalizePendingBattleHUDAttachment();
	[[nodiscard]] bool TryAcceptBattleHUDAttachment(
		const UBattleHUDWidget& HUDWidget);
	[[nodiscard]] bool TryAdvanceBattleHUDPresentationGeneration();
	static void RemoveBattleHUDFromScreen(UBattleHUDWidget& HUDWidget);
	void DiscardBattleHUD();
	static FVector2D QuantizeNavigationInput(const FVector2D& InputValue);

	UPROPERTY(Transient)
	TObjectPtr<UBattleHUDWidget> BattleHUDWidget = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> LoadedBattleInputMappingContext = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> LoadedBattleNavigateAction = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> LoadedBattleConfirmAction = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> LoadedBattleCancelAction = nullptr;

	uint64 BattleHUDPresentationGeneration = 0;
	uint64 AcceptedBattleHUDConstructionSerial = 0;
	bool bBattleHUDAttachmentFinalizePending = false;
	FDelegateHandle BattleHUDConstructedHandle;
	FOnBattleHUDAvailableNative BattleHUDAvailableNativeDelegate;
};
