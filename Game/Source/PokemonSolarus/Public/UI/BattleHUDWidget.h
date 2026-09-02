#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/BattleCommandWidget.h"
#include "UI/BattleHUDDisplayState.h"
#include "BattleHUDWidget.generated.h"

#if WITH_DEV_AUTOMATION_TESTS
class FBattleHUDProductionLifecycleTestFixture;
class FBattlePresentationAdapterTestFixture;
class FBattleRuntimePresentationTestFixture;
#endif
class UBattleHUDWidget;
class UBattlePokemonHealthPanel;
DECLARE_MULTICAST_DELEGATE_OneParam(
	FOnBattleHUDConstructedNative,
	UBattleHUDWidget&);

/** Native presentation seam for the reusable battle HUD. */
UCLASS(Abstract, BlueprintType, Blueprintable)
class POKEMONSOLARUS_API UBattleHUDWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Applies one fully validated Battle presentation and reveals the root HUD. */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "Battle|UI")
	bool ApplyHUDDisplayState(const FBattleHUDDisplayState& DisplayState);

	/** Returns whether NativeConstruct and every required child binding are ready. */
	UFUNCTION(BlueprintPure, BlueprintCosmetic, Category = "Battle|UI")
	bool IsStructurallyReady() const;

	/** Returns whether a validated full presentation is currently visible. */
	UFUNCTION(BlueprintPure, BlueprintCosmetic, Category = "Battle|UI")
	bool IsPresentationVisible() const;

	/** Returns whether local command navigation, confirmation, and requests are enabled. */
	UFUNCTION(BlueprintPure, BlueprintCosmetic, Category = "Battle|UI|Command")
	bool IsCommandInputEnabled() const;

	/** Copies the most recent complete validated state without exposing child widgets. */
	UFUNCTION(BlueprintPure, BlueprintCosmetic, Category = "Battle|UI")
	bool TryGetLastValidatedDisplayState(FBattleHUDDisplayState& OutDisplayState) const;

	/** Identifies the most recent completed native construction pass. */
	[[nodiscard]] uint64 GetNativeConstructionSerial() const
	{
		return NativeConstructionSerial;
	}

	/** Native lifecycle signal emitted after each completed NativeConstruct pass. */
	FOnBattleHUDConstructedNative& GetConstructedNativeDelegate()
	{
		return ConstructedNativeDelegate;
	}

	/** Gates command input and facade requests without changing the current visuals. */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "Battle|UI|Command")
	void DisableCommandInputPreservingPresentation();

	/** Atomically forwards validated display-ready state to the optional command widget. */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "Battle|UI|Command")
	bool ApplyCommandDisplayState(const FBattleCommandDisplayState& DisplayState);

	/** Hides and deactivates the top-level command menu. */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "Battle|UI|Command")
	void HideCommandMenu();

	/** Forwards one exact cardinal navigation input to the command widget. */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "Battle|UI|Command")
	bool NavigateCommandMenu(const FVector2D& CardinalDirection);

	/** Forwards Confirm without submitting any Battle-core decision. */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "Battle|UI|Command")
	bool ConfirmCommandMenu();

	/** Forwards the intentionally empty top-level Cancel behavior. */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "Battle|UI|Command")
	void CancelCommandMenu();

	/** Returns whether the optional command widget currently accepts input. */
	UFUNCTION(BlueprintPure, BlueprintCosmetic, Category = "Battle|UI|Command")
	bool IsCommandMenuActive() const;

	/** Returns the command widget's current focus without inventing a missing value. */
	UFUNCTION(BlueprintPure, BlueprintCosmetic, Category = "Battle|UI|Command")
	bool TryGetFocusedCommand(EBattleUICommand& OutFocusedCommand) const;

	/** Returns terminal/failure text, or the command widget's current prompt/reason. */
	UFUNCTION(BlueprintPure, BlueprintCosmetic, Category = "Battle|UI|Command")
	bool TryGetCurrentBattleText(FText& OutBattleText) const;

	/** Keeps the current health presentation, disables commands, and emits status text. */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "Battle|UI")
	bool PresentBattleStatusText(const FText& BattleText);

	/** Initializes both health panels from authoritative battle presentation data. */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "Battle|UI|Health")
	bool InitializeHealthPanels(
		const FText& PlayerPokemonName,
		int32 PlayerCurrentHP,
		int32 PlayerMaxHP,
		const FText& OpponentPokemonName,
		int32 OpponentCurrentHP,
		int32 OpponentMaxHP);

	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "Battle|UI|Health")
	bool AnimatePlayerHPTo(int32 CurrentHP, int32 MaxHP, float DurationSeconds);

	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "Battle|UI|Health")
	bool AnimateOpponentHPTo(int32 CurrentHP, int32 MaxHP, float DurationSeconds);

	/** Completes either active visual HP animation without changing battle state. */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "Battle|UI|Health")
	void CompleteHPAnimations();

	UFUNCTION(BlueprintPure, BlueprintCosmetic, Category = "Battle|UI|Health")
	bool IsAnyHPAnimating() const;

	/** Facade signal for a real local command-focus change. */
	UPROPERTY(BlueprintAssignable, Category = "Battle|UI|Command")
	FBattleCommandFocusChanged OnCommandFocusChanged;

	/** Facade signal for the prompt or unavailable reason to display. */
	UPROPERTY(BlueprintAssignable, Category = "Battle|UI|Command")
	FBattleCommandTextChanged OnCommandBattleTextChanged;

	/** Facade visual-feedback signal emitted only for an available Confirm. */
	UPROPERTY(BlueprintAssignable, Category = "Battle|UI|Command")
	FBattleCommandPressed OnCommandPressed;

	/** Facade local request; no Battle-core decision is submitted here. */
	UPROPERTY(BlueprintAssignable, Category = "Battle|UI|Command")
	FBattleCommandRequested OnCommandRequested;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UBattleCommandWidget> CommandUI = nullptr;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UBattlePokemonHealthPanel> HealthPanel_Player = nullptr;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UBattlePokemonHealthPanel> HealthPanel_Opponent = nullptr;

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FBattleHUDProductionLifecycleTestFixture;
	friend class FBattlePresentationAdapterTestFixture;
	friend class FBattleRuntimePresentationTestFixture;
#endif

	UFUNCTION()
	void HandleCommandFocusChanged(EBattleUICommand FocusedCommand);

	UFUNCTION()
	void HandleCommandBattleTextChanged(FText BattleText);

	UFUNCTION()
	void HandleCommandPressed(EBattleUICommand PressedCommand);

	UFUNCTION()
	void HandleCommandRequested(EBattleUICommand RequestedCommand);
	[[nodiscard]] bool RejectDisplayState(
		const TCHAR* ErrorMessage,
		bool bMustCollapsePresentation);
	[[nodiscard]] bool ApplyValidatedHUDChildren(
		const FBattleHUDDisplayState& DisplayState);
	[[nodiscard]] bool ApplyHealthPanelStates(
		const FBattleHUDHealthDisplayState& PlayerState,
		const FBattleHUDHealthDisplayState& OpponentState);
	void HideRootPresentation();
	void CollapsePresentation();
	void BindCommandDelegates();
	void UnbindCommandDelegates();
	void RefreshCommandFacade();
	void InitializeHealthPanelVisibility();
	[[nodiscard]] bool TryAdvanceNativeConstructionSerial();

	FBattleHUDDisplayState LastValidatedDisplayState;
	FText PresentedBattleStatusText;
	uint64 NativeConstructionSerial = 0;
	bool bNativeConstructed = false;
	bool bHasValidatedDisplayState = false;
	bool bPresentationVisible = false;
	bool bCommandInputEnabled = false;
	FOnBattleHUDConstructedNative ConstructedNativeDelegate;
};
