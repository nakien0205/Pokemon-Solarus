#include "UI/BattleHUDWidget.h"

#include "UI/BattlePokemonHealthPanel.h"

DEFINE_LOG_CATEGORY_STATIC(LogBattleHUDWidget, Log, All);

bool UBattleHUDWidget::ApplyHUDDisplayState(
	const FBattleHUDDisplayState& DisplayState)
{
	if (!IsStructurallyReady())
	{
		return RejectDisplayState(
			TEXT("Rejected a full Battle HUD display state because required widget bindings are unavailable."),
			true);
	}

	if (!DisplayState.IsValid())
	{
		return RejectDisplayState(
			TEXT("Rejected an incomplete or invalid full Battle HUD display state."),
			false);
	}

	bCommandInputEnabled = false;
	if (!ApplyValidatedHUDChildren(DisplayState))
	{
		return RejectDisplayState(
			TEXT("A structurally ready Battle HUD rejected prevalidated display state."),
			false);
	}

	LastValidatedDisplayState = DisplayState;
	bHasValidatedDisplayState = true;
	bPresentationVisible = true;
	bCommandInputEnabled = true;
	SetVisibility(ESlateVisibility::Visible);
	return true;
}

bool UBattleHUDWidget::IsStructurallyReady() const
{
	return bNativeConstructed
		&& IsValid(CommandUI)
		&& IsValid(HealthPanel_Player)
		&& HealthPanel_Player->IsStructurallyReady()
		&& IsValid(HealthPanel_Opponent)
		&& HealthPanel_Opponent->IsStructurallyReady();
}

bool UBattleHUDWidget::IsPresentationVisible() const
{
	return IsStructurallyReady()
		&& bHasValidatedDisplayState
		&& bPresentationVisible
		&& GetVisibility() == ESlateVisibility::Visible;
}

bool UBattleHUDWidget::IsCommandInputEnabled() const
{
	return bCommandInputEnabled
		&& IsPresentationVisible();
}

bool UBattleHUDWidget::TryGetLastValidatedDisplayState(
	FBattleHUDDisplayState& OutDisplayState) const
{
	OutDisplayState = FBattleHUDDisplayState();
	if (!bHasValidatedDisplayState)
	{
		return false;
	}

	OutDisplayState = LastValidatedDisplayState;
	return true;
}

void UBattleHUDWidget::DisableCommandInputPreservingPresentation()
{
	bCommandInputEnabled = false;
}

bool UBattleHUDWidget::RejectDisplayState(
	const TCHAR* ErrorMessage,
	const bool bMustCollapsePresentation)
{
	DisableCommandInputPreservingPresentation();
	if (bMustCollapsePresentation || !bPresentationVisible)
	{
		CollapsePresentation();
	}

	UE_LOG(LogBattleHUDWidget, Error, TEXT("%s"), ErrorMessage);
	return false;
}

bool UBattleHUDWidget::ApplyValidatedHUDChildren(
	const FBattleHUDDisplayState& DisplayState)
{
	const bool bPlayerApplied = HealthPanel_Player->ApplyDisplayState(
		DisplayState.Player.PokemonName,
		DisplayState.Player.CurrentHP,
		DisplayState.Player.MaxHP,
		true);
	const bool bOpponentApplied = HealthPanel_Opponent->ApplyDisplayState(
		DisplayState.Opponent.PokemonName,
		DisplayState.Opponent.CurrentHP,
		DisplayState.Opponent.MaxHP,
		false);
	const bool bCommandApplied = CommandUI->ApplyDisplayState(DisplayState.Command);
	return bPlayerApplied && bOpponentApplied && bCommandApplied;
}

void UBattleHUDWidget::HideRootPresentation()
{
	SetVisibility(ESlateVisibility::Collapsed);
	bPresentationVisible = false;
	bCommandInputEnabled = false;
}

void UBattleHUDWidget::CollapsePresentation()
{
	HideRootPresentation();
	if (CommandUI)
	{
		CommandUI->DeactivateCommandMenu();
	}
}

void UBattleHUDWidget::NativeConstruct()
{
	Super::NativeConstruct();
	HideRootPresentation();
	bNativeConstructed = false;
	if (!TryAdvanceNativeConstructionSerial())
	{
		return;
	}

	bNativeConstructed = true;
	BindCommandDelegates();
	InitializeHealthPanelVisibility();
	ConstructedNativeDelegate.Broadcast(*this);
}

bool UBattleHUDWidget::TryAdvanceNativeConstructionSerial()
{
	if (NativeConstructionSerial == MAX_uint64)
	{
		UE_LOG(LogBattleHUDWidget, Error,
			TEXT("The Battle HUD native construction serial cannot advance."));
		return false;
	}

	++NativeConstructionSerial;
	return true;
}

void UBattleHUDWidget::BindCommandDelegates()
{
	if (!CommandUI)
	{
		UE_LOG(LogBattleHUDWidget, Error,
			TEXT("Required Battle command child CommandUI is not bound."));
		return;
	}

	CommandUI->OnCommandFocusChanged.AddUniqueDynamic(
		this,
		&UBattleHUDWidget::HandleCommandFocusChanged);
	CommandUI->OnBattleTextChanged.AddUniqueDynamic(
		this,
		&UBattleHUDWidget::HandleCommandBattleTextChanged);
	CommandUI->OnCommandPressed.AddUniqueDynamic(
		this,
		&UBattleHUDWidget::HandleCommandPressed);
	CommandUI->OnCommandRequested.AddUniqueDynamic(
		this,
		&UBattleHUDWidget::HandleCommandRequested);
	RefreshCommandFacade();
}

void UBattleHUDWidget::RefreshCommandFacade()
{
	EBattleUICommand FocusedCommand = EBattleUICommand::Fight;
	if (CommandUI->TryGetFocusedCommand(FocusedCommand))
	{
		HandleCommandFocusChanged(FocusedCommand);
	}

	FText BattleText;
	if (CommandUI->TryGetCurrentBattleText(BattleText))
	{
		HandleCommandBattleTextChanged(BattleText);
	}
}

void UBattleHUDWidget::InitializeHealthPanelVisibility()
{
	if (HealthPanel_Player)
	{
		HealthPanel_Player->SetExactHPVisible(true);
	}

	if (HealthPanel_Opponent)
	{
		HealthPanel_Opponent->SetExactHPVisible(false);
	}
}

void UBattleHUDWidget::NativeDestruct()
{
	HideRootPresentation();
	bNativeConstructed = false;
	UnbindCommandDelegates();
	Super::NativeDestruct();
}

void UBattleHUDWidget::UnbindCommandDelegates()
{
	if (!CommandUI)
	{
		return;
	}

	CommandUI->OnCommandFocusChanged.RemoveDynamic(
		this,
		&UBattleHUDWidget::HandleCommandFocusChanged);
	CommandUI->OnBattleTextChanged.RemoveDynamic(
		this,
		&UBattleHUDWidget::HandleCommandBattleTextChanged);
	CommandUI->OnCommandPressed.RemoveDynamic(
		this,
		&UBattleHUDWidget::HandleCommandPressed);
	CommandUI->OnCommandRequested.RemoveDynamic(
		this,
		&UBattleHUDWidget::HandleCommandRequested);
}

bool UBattleHUDWidget::ApplyCommandDisplayState(
	const FBattleCommandDisplayState& DisplayState)
{
	if (!IsStructurallyReady())
	{
		return RejectDisplayState(
			TEXT("Cannot apply command state because required Battle HUD bindings are unavailable."),
			true);
	}

	if (!FBattleHUDDisplayState::IsCommandDisplayStateValid(DisplayState))
	{
		return RejectDisplayState(
			TEXT("Cannot apply an invalid command state."),
			false);
	}

	bCommandInputEnabled = false;
	if (!CommandUI->ApplyDisplayState(DisplayState))
	{
		return RejectDisplayState(
			TEXT("Battle command display state was rejected; command input remains fail-closed."),
			false);
	}

	if (bHasValidatedDisplayState)
	{
		LastValidatedDisplayState.Command = DisplayState;
		bCommandInputEnabled = IsPresentationVisible();
	}
	return true;
}

void UBattleHUDWidget::HideCommandMenu()
{
	bCommandInputEnabled = false;
	if (CommandUI)
	{
		CommandUI->DeactivateCommandMenu();
	}
}

bool UBattleHUDWidget::NavigateCommandMenu(const FVector2D& CardinalDirection)
{
	return bCommandInputEnabled
		&& IsPresentationVisible()
		&& CommandUI
		&& CommandUI->Navigate(CardinalDirection);
}

bool UBattleHUDWidget::ConfirmCommandMenu()
{
	return bCommandInputEnabled
		&& IsPresentationVisible()
		&& CommandUI
		&& CommandUI->ConfirmFocusedCommand();
}

void UBattleHUDWidget::CancelCommandMenu()
{
	if (bCommandInputEnabled && IsPresentationVisible() && CommandUI)
	{
		CommandUI->HandleTopLevelCancel();
	}
}

bool UBattleHUDWidget::IsCommandMenuActive() const
{
	return bCommandInputEnabled
		&& IsPresentationVisible()
		&& CommandUI
		&& CommandUI->IsCommandMenuActive();
}

bool UBattleHUDWidget::TryGetFocusedCommand(
	EBattleUICommand& OutFocusedCommand) const
{
	return CommandUI
		&& CommandUI->TryGetFocusedCommand(OutFocusedCommand);
}

bool UBattleHUDWidget::TryGetCurrentBattleText(FText& OutBattleText) const
{
	return CommandUI
		&& CommandUI->TryGetCurrentBattleText(OutBattleText);
}

bool UBattleHUDWidget::InitializeHealthPanels(
	const FText& PlayerPokemonName,
	const int32 PlayerCurrentHP,
	const int32 PlayerMaxHP,
	const FText& OpponentPokemonName,
	const int32 OpponentCurrentHP,
	const int32 OpponentMaxHP)
{
	FBattleHUDHealthDisplayState PlayerState;
	PlayerState.PokemonName = PlayerPokemonName;
	PlayerState.CurrentHP = PlayerCurrentHP;
	PlayerState.MaxHP = PlayerMaxHP;
	FBattleHUDHealthDisplayState OpponentState;
	OpponentState.PokemonName = OpponentPokemonName;
	OpponentState.CurrentHP = OpponentCurrentHP;
	OpponentState.MaxHP = OpponentMaxHP;
	return ApplyHealthPanelStates(PlayerState, OpponentState);
}

bool UBattleHUDWidget::ApplyHealthPanelStates(
	const FBattleHUDHealthDisplayState& PlayerState,
	const FBattleHUDHealthDisplayState& OpponentState)
{
	if (!IsStructurallyReady()
		|| !PlayerState.IsValid()
		|| !OpponentState.IsValid())
	{
		DisableCommandInputPreservingPresentation();
		return false;
	}

	const bool bPlayerApplied = HealthPanel_Player->ApplyDisplayState(
		PlayerState.PokemonName,
		PlayerState.CurrentHP,
		PlayerState.MaxHP,
		true);
	const bool bOpponentApplied = HealthPanel_Opponent->ApplyDisplayState(
		OpponentState.PokemonName,
		OpponentState.CurrentHP,
		OpponentState.MaxHP,
		false);
	if (bPlayerApplied && bOpponentApplied && bHasValidatedDisplayState)
	{
		LastValidatedDisplayState.Player = PlayerState;
		LastValidatedDisplayState.Opponent = OpponentState;
	}
	return bPlayerApplied && bOpponentApplied;
}

bool UBattleHUDWidget::AnimatePlayerHPTo(
	const int32 CurrentHP,
	const int32 MaxHP,
	const float DurationSeconds)
{
	return HealthPanel_Player
		&& HealthPanel_Player->AnimateHPTo(CurrentHP, MaxHP, DurationSeconds);
}

bool UBattleHUDWidget::AnimateOpponentHPTo(
	const int32 CurrentHP,
	const int32 MaxHP,
	const float DurationSeconds)
{
	return HealthPanel_Opponent
		&& HealthPanel_Opponent->AnimateHPTo(CurrentHP, MaxHP, DurationSeconds);
}

void UBattleHUDWidget::CompleteHPAnimations()
{
	if (HealthPanel_Player)
	{
		HealthPanel_Player->CompleteHPAnimation();
	}

	if (HealthPanel_Opponent)
	{
		HealthPanel_Opponent->CompleteHPAnimation();
	}
}

bool UBattleHUDWidget::IsAnyHPAnimating() const
{
	return (HealthPanel_Player && HealthPanel_Player->IsHPAnimating())
		|| (HealthPanel_Opponent && HealthPanel_Opponent->IsHPAnimating());
}

void UBattleHUDWidget::HandleCommandFocusChanged(
	const EBattleUICommand FocusedCommand)
{
	OnCommandFocusChanged.Broadcast(FocusedCommand);
}

void UBattleHUDWidget::HandleCommandBattleTextChanged(FText BattleText)
{
	OnCommandBattleTextChanged.Broadcast(BattleText);
}

void UBattleHUDWidget::HandleCommandPressed(
	const EBattleUICommand PressedCommand)
{
	if (bCommandInputEnabled && IsPresentationVisible())
	{
		OnCommandPressed.Broadcast(PressedCommand);
	}
}

void UBattleHUDWidget::HandleCommandRequested(
	const EBattleUICommand RequestedCommand)
{
	if (bCommandInputEnabled && IsPresentationVisible())
	{
		OnCommandRequested.Broadcast(RequestedCommand);
	}
}
