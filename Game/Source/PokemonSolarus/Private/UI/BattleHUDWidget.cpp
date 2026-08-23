#include "UI/BattleHUDWidget.h"

#include "UI/BattlePokemonHealthPanel.h"

DEFINE_LOG_CATEGORY_STATIC(LogBattleHUDWidget, Log, All);

void UBattleHUDWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (CommandUI)
	{
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
	else
	{
		UE_LOG(LogBattleHUDWidget, Warning,
			TEXT("Optional Battle command child CommandUI is not bound. Command input remains inactive until frontend wiring is complete."));
	}

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
	if (CommandUI)
	{
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

	Super::NativeDestruct();
}

bool UBattleHUDWidget::ApplyCommandDisplayState(
	const FBattleCommandDisplayState& DisplayState)
{
	if (!CommandUI)
	{
		UE_LOG(LogBattleHUDWidget, Error,
			TEXT("Cannot apply Battle command display state because CommandUI is not bound."));
		return false;
	}

	if (!CommandUI->ApplyDisplayState(DisplayState))
	{
		UE_LOG(LogBattleHUDWidget, Error,
			TEXT("Battle command display state was rejected; command input remains fail-closed."));
		return false;
	}

	return true;
}

void UBattleHUDWidget::HideCommandMenu()
{
	if (CommandUI)
	{
		CommandUI->DeactivateCommandMenu();
	}
}

bool UBattleHUDWidget::NavigateCommandMenu(const FVector2D& CardinalDirection)
{
	return CommandUI && CommandUI->Navigate(CardinalDirection);
}

bool UBattleHUDWidget::ConfirmCommandMenu()
{
	return CommandUI && CommandUI->ConfirmFocusedCommand();
}

void UBattleHUDWidget::CancelCommandMenu()
{
	if (CommandUI)
	{
		CommandUI->HandleTopLevelCancel();
	}
}

bool UBattleHUDWidget::IsCommandMenuActive() const
{
	return CommandUI && CommandUI->IsCommandMenuActive();
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
	if (!HealthPanel_Player || !HealthPanel_Opponent)
	{
		return false;
	}

	HealthPanel_Player->SetPokemonName(PlayerPokemonName);
	HealthPanel_Opponent->SetPokemonName(OpponentPokemonName);
	HealthPanel_Player->SetExactHPVisible(true);
	HealthPanel_Opponent->SetExactHPVisible(false);

	const bool bPlayerHPValid = HealthPanel_Player->SetHPImmediate(
		PlayerCurrentHP,
		PlayerMaxHP);
	const bool bOpponentHPValid = HealthPanel_Opponent->SetHPImmediate(
		OpponentCurrentHP,
		OpponentMaxHP);
	return bPlayerHPValid && bOpponentHPValid;
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
	OnCommandPressed.Broadcast(PressedCommand);
}

void UBattleHUDWidget::HandleCommandRequested(
	const EBattleUICommand RequestedCommand)
{
	OnCommandRequested.Broadcast(RequestedCommand);
}
