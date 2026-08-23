#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "BattleCommandWidget.generated.h"

/** UI-local identity for one top-level Battle command. */
UENUM(BlueprintType)
enum class EBattleUICommand : uint8
{
	Fight,
	Bag,
	Pokemon UMETA(DisplayName = "Pokémon"),
	Run
};

/** Display-ready availability for one top-level Battle command. */
USTRUCT(BlueprintType)
struct POKEMONSOLARUS_API FBattleCommandAvailability
{
	GENERATED_BODY()

	/** Whether Confirm may emit a local request for this command. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle|UI|Command")
	bool bAvailable = false;

	/** Required localized explanation when this command is unavailable. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle|UI|Command")
	FText UnavailableReason;
};

/** Atomic, display-ready state for the four-command Battle menu. */
USTRUCT(BlueprintType)
struct POKEMONSOLARUS_API FBattleCommandDisplayState
{
	GENERATED_BODY()

	/** Localized prompt shown while the focused command is available. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle|UI|Command")
	FText NormalPrompt;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle|UI|Command")
	FBattleCommandAvailability Fight;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle|UI|Command")
	FBattleCommandAvailability Bag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle|UI|Command")
	FBattleCommandAvailability Pokemon;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle|UI|Command")
	FBattleCommandAvailability Run;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FBattleCommandFocusChanged,
	EBattleUICommand,
	FocusedCommand);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FBattleCommandTextChanged,
	FText,
	BattleText);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FBattleCommandPressed,
	EBattleUICommand,
	PressedCommand);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FBattleCommandRequested,
	EBattleUICommand,
	RequestedCommand);

DECLARE_MULTICAST_DELEGATE_OneParam(
	FBattleCommandFocusChangedNative,
	EBattleUICommand);
DECLARE_MULTICAST_DELEGATE_OneParam(
	FBattleCommandTextChangedNative,
	const FText&);
DECLARE_MULTICAST_DELEGATE_OneParam(
	FBattleCommandPressedNative,
	EBattleUICommand);
DECLARE_MULTICAST_DELEGATE_OneParam(
	FBattleCommandRequestedNative,
	EBattleUICommand);

/**
 * Style-free local mechanics for the reusable top-level Battle command menu.
 * This widget never reads or mutates Battle-core state.
 */
UCLASS(BlueprintType, Blueprintable)
class POKEMONSOLARUS_API UBattleCommandWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UBattleCommandWidget(const FObjectInitializer& ObjectInitializer);

	/** Atomically validates and shows a new selection phase, focused on Fight. */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "Battle|UI|Command")
	bool ApplyDisplayState(const FBattleCommandDisplayState& InDisplayState);

	/** Hides the menu and rejects navigation and Confirm until valid state is applied. */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "Battle|UI|Command")
	void DeactivateCommandMenu();

	/** Moves focus for one exact cardinal direction; zero and diagonals are ignored. */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "Battle|UI|Command")
	bool Navigate(const FVector2D& CardinalDirection);

	/** Emits one local request for an available focus, or only reaffirms an unavailable reason. */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "Battle|UI|Command")
	bool ConfirmFocusedCommand();

	/** Top-level Cancel is intentionally a no-op. */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "Battle|UI|Command")
	void HandleTopLevelCancel();

	/** Returns whether this menu currently accepts local command input. */
	UFUNCTION(BlueprintPure, BlueprintCosmetic, Category = "Battle|UI|Command")
	bool IsCommandMenuActive() const { return bCommandMenuActive; }

	/** Copies the last fully validated display state when one exists. */
	UFUNCTION(BlueprintPure, BlueprintCosmetic, Category = "Battle|UI|Command")
	bool TryGetDisplayState(FBattleCommandDisplayState& OutDisplayState) const;

	/** Returns the current local focus only while the menu is active. */
	UFUNCTION(BlueprintPure, BlueprintCosmetic, Category = "Battle|UI|Command")
	bool TryGetFocusedCommand(EBattleUICommand& OutFocusedCommand) const;

	/** Returns the prompt or unavailable reason currently selected for display. */
	UFUNCTION(BlueprintPure, BlueprintCosmetic, Category = "Battle|UI|Command")
	bool TryGetCurrentBattleText(FText& OutBattleText) const;

	/** Blueprint signal for a real local focus change. */
	UPROPERTY(BlueprintAssignable, Category = "Battle|UI|Command")
	FBattleCommandFocusChanged OnCommandFocusChanged;

	/** Blueprint signal for prompt/reason updates, including unavailable reaffirmation. */
	UPROPERTY(BlueprintAssignable, Category = "Battle|UI|Command")
	FBattleCommandTextChanged OnBattleTextChanged;

	/** Blueprint visual-feedback signal emitted only for an available Confirm. */
	UPROPERTY(BlueprintAssignable, Category = "Battle|UI|Command")
	FBattleCommandPressed OnCommandPressed;

	/** Blueprint local request; consumers decide which child selector to open. */
	UPROPERTY(BlueprintAssignable, Category = "Battle|UI|Command")
	FBattleCommandRequested OnCommandRequested;

	/** Native observation seam corresponding to OnCommandFocusChanged. */
	FBattleCommandFocusChangedNative& GetCommandFocusChangedNativeDelegate()
	{
		return CommandFocusChangedNative;
	}

	/** Native observation seam corresponding to OnBattleTextChanged. */
	FBattleCommandTextChangedNative& GetBattleTextChangedNativeDelegate()
	{
		return BattleTextChangedNative;
	}

	/** Native observation seam corresponding to OnCommandPressed. */
	FBattleCommandPressedNative& GetCommandPressedNativeDelegate()
	{
		return CommandPressedNative;
	}

	/** Native observation seam corresponding to OnCommandRequested. */
	FBattleCommandRequestedNative& GetCommandRequestedNativeDelegate()
	{
		return CommandRequestedNative;
	}

protected:
	virtual void NativeConstruct() override;

private:
	static bool ValidateDisplayState(
		const FBattleCommandDisplayState& InDisplayState,
		FString& OutError);
	const FBattleCommandAvailability* FindAvailability(EBattleUICommand Command) const;
	void RefreshCurrentBattleText();
	void BroadcastFocusAndText();
	void BroadcastBattleText();
	void BroadcastCommandNotifications(bool bIncludeFocus, bool bIncludeText);

	FBattleCommandDisplayState DisplayState;
	FText CurrentBattleText;
	EBattleUICommand FocusedCommand = EBattleUICommand::Fight;
	bool bHasValidatedDisplayState = false;
	bool bCommandMenuActive = false;
	bool bBroadcastingCommandNotifications = false;
	bool bCommandFocusNotificationPending = false;
	bool bBattleTextNotificationPending = false;

	FBattleCommandFocusChangedNative CommandFocusChangedNative;
	FBattleCommandTextChangedNative BattleTextChangedNative;
	FBattleCommandPressedNative CommandPressedNative;
	FBattleCommandRequestedNative CommandRequestedNative;
};
