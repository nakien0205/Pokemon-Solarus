#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/BattleCommandWidget.h"
#include "BattleHUDWidget.generated.h"

class UBattlePokemonHealthPanel;

/** Native presentation seam for the reusable battle HUD. */
UCLASS(Abstract, BlueprintType, Blueprintable)
class POKEMONSOLARUS_API UBattleHUDWidget : public UUserWidget
{
	GENERATED_BODY()

public:
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

	/** Returns the command widget's current prompt or unavailable reason. */
	UFUNCTION(BlueprintPure, BlueprintCosmetic, Category = "Battle|UI|Command")
	bool TryGetCurrentBattleText(FText& OutBattleText) const;

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

	/** Optional until WBP_BattleCommandUI is reparented and wired by the frontend owner. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UBattleCommandWidget> CommandUI = nullptr;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UBattlePokemonHealthPanel> HealthPanel_Player = nullptr;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UBattlePokemonHealthPanel> HealthPanel_Opponent = nullptr;

private:
	UFUNCTION()
	void HandleCommandFocusChanged(EBattleUICommand FocusedCommand);

	UFUNCTION()
	void HandleCommandBattleTextChanged(FText BattleText);

	UFUNCTION()
	void HandleCommandPressed(EBattleUICommand PressedCommand);

	UFUNCTION()
	void HandleCommandRequested(EBattleUICommand RequestedCommand);
};
