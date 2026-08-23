#include "UI/BattleCommandWidget.h"

#include "Components/Widget.h"

DEFINE_LOG_CATEGORY_STATIC(LogBattleCommandWidget, Log, All);

namespace
{
	constexpr int32 MaxReentrantNotificationPasses = 8;

	bool HasDisplayText(const FText& Text)
	{
		return !Text.ToString().TrimStartAndEnd().IsEmpty();
	}
}

UBattleCommandWidget::UBattleCommandWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetVisibility(ESlateVisibility::Collapsed);
}

void UBattleCommandWidget::NativeConstruct()
{
	Super::NativeConstruct();

	SetVisibility(
		bCommandMenuActive
			? ESlateVisibility::Visible
			: ESlateVisibility::Collapsed);
}

bool UBattleCommandWidget::ApplyDisplayState(
	const FBattleCommandDisplayState& InDisplayState)
{
	FString ValidationError;
	if (!ValidateDisplayState(InDisplayState, ValidationError))
	{
		bCommandMenuActive = false;
		SetVisibility(ESlateVisibility::Collapsed);
		UE_LOG(LogBattleCommandWidget, Error,
			TEXT("Rejected invalid Battle command display state: %s"),
			*ValidationError);
		return false;
	}

	DisplayState = InDisplayState;
	bHasValidatedDisplayState = true;
	FocusedCommand = EBattleUICommand::Fight;
	bCommandMenuActive = true;
	RefreshCurrentBattleText();
	SetVisibility(ESlateVisibility::Visible);
	BroadcastFocusAndText();
	return true;
}

void UBattleCommandWidget::DeactivateCommandMenu()
{
	bCommandMenuActive = false;
	SetVisibility(ESlateVisibility::Collapsed);
}

bool UBattleCommandWidget::Navigate(const FVector2D& CardinalDirection)
{
	if (!bCommandMenuActive)
	{
		return false;
	}

	const bool bHasHorizontalInput = !FMath::IsNearlyZero(CardinalDirection.X);
	const bool bHasVerticalInput = !FMath::IsNearlyZero(CardinalDirection.Y);
	if (bHasHorizontalInput == bHasVerticalInput)
	{
		return false;
	}

	EBattleUICommand NextCommand = FocusedCommand;
	if (bHasHorizontalInput)
	{
		if (CardinalDirection.X > 0.0)
		{
			if (FocusedCommand == EBattleUICommand::Fight)
			{
				NextCommand = EBattleUICommand::Bag;
			}
			else if (FocusedCommand == EBattleUICommand::Pokemon)
			{
				NextCommand = EBattleUICommand::Run;
			}
		}
		else
		{
			if (FocusedCommand == EBattleUICommand::Bag)
			{
				NextCommand = EBattleUICommand::Fight;
			}
			else if (FocusedCommand == EBattleUICommand::Run)
			{
				NextCommand = EBattleUICommand::Pokemon;
			}
		}
	}
	else if (CardinalDirection.Y > 0.0)
	{
		if (FocusedCommand == EBattleUICommand::Pokemon)
		{
			NextCommand = EBattleUICommand::Fight;
		}
		else if (FocusedCommand == EBattleUICommand::Run)
		{
			NextCommand = EBattleUICommand::Bag;
		}
	}
	else
	{
		if (FocusedCommand == EBattleUICommand::Fight)
		{
			NextCommand = EBattleUICommand::Pokemon;
		}
		else if (FocusedCommand == EBattleUICommand::Bag)
		{
			NextCommand = EBattleUICommand::Run;
		}
	}

	if (NextCommand == FocusedCommand)
	{
		return false;
	}

	FocusedCommand = NextCommand;
	RefreshCurrentBattleText();
	BroadcastFocusAndText();
	return true;
}

bool UBattleCommandWidget::ConfirmFocusedCommand()
{
	if (!bCommandMenuActive)
	{
		return false;
	}

	const FBattleCommandAvailability* Availability = FindAvailability(FocusedCommand);
	if (!Availability)
	{
		return false;
	}

	if (!Availability->bAvailable)
	{
		CurrentBattleText = Availability->UnavailableReason;
		BroadcastBattleText();
		return false;
	}

	const EBattleUICommand ConfirmedCommand = FocusedCommand;
	OnCommandPressed.Broadcast(ConfirmedCommand);
	CommandPressedNative.Broadcast(ConfirmedCommand);
	OnCommandRequested.Broadcast(ConfirmedCommand);
	CommandRequestedNative.Broadcast(ConfirmedCommand);
	return true;
}

void UBattleCommandWidget::HandleTopLevelCancel()
{
}

bool UBattleCommandWidget::TryGetDisplayState(
	FBattleCommandDisplayState& OutDisplayState) const
{
	if (!bHasValidatedDisplayState)
	{
		return false;
	}

	OutDisplayState = DisplayState;
	return true;
}

bool UBattleCommandWidget::TryGetFocusedCommand(
	EBattleUICommand& OutFocusedCommand) const
{
	if (!bCommandMenuActive)
	{
		return false;
	}

	OutFocusedCommand = FocusedCommand;
	return true;
}

bool UBattleCommandWidget::TryGetCurrentBattleText(FText& OutBattleText) const
{
	if (!bCommandMenuActive)
	{
		return false;
	}

	OutBattleText = CurrentBattleText;
	return true;
}

bool UBattleCommandWidget::ValidateDisplayState(
	const FBattleCommandDisplayState& InDisplayState,
	FString& OutError)
{
	if (!HasDisplayText(InDisplayState.NormalPrompt))
	{
		OutError = TEXT("NormalPrompt is empty.");
		return false;
	}

	const struct
	{
		const TCHAR* Name;
		const FBattleCommandAvailability* Availability;
	} CommandStates[] = {
		{TEXT("Fight"), &InDisplayState.Fight},
		{TEXT("Bag"), &InDisplayState.Bag},
		{TEXT("Pokemon"), &InDisplayState.Pokemon},
		{TEXT("Run"), &InDisplayState.Run}};

	for (const auto& CommandState : CommandStates)
	{
		if (!CommandState.Availability->bAvailable
			&& !HasDisplayText(CommandState.Availability->UnavailableReason))
		{
			OutError = FString::Printf(
				TEXT("%s is unavailable but has no display reason."),
				CommandState.Name);
			return false;
		}
	}

	OutError.Reset();
	return true;
}

const FBattleCommandAvailability* UBattleCommandWidget::FindAvailability(
	const EBattleUICommand Command) const
{
	switch (Command)
	{
	case EBattleUICommand::Fight:
		return &DisplayState.Fight;
	case EBattleUICommand::Bag:
		return &DisplayState.Bag;
	case EBattleUICommand::Pokemon:
		return &DisplayState.Pokemon;
	case EBattleUICommand::Run:
		return &DisplayState.Run;
	default:
		return nullptr;
	}
}

void UBattleCommandWidget::RefreshCurrentBattleText()
{
	const FBattleCommandAvailability* Availability = FindAvailability(FocusedCommand);
	CurrentBattleText = Availability && !Availability->bAvailable
		? Availability->UnavailableReason
		: DisplayState.NormalPrompt;
}

void UBattleCommandWidget::BroadcastFocusAndText()
{
	BroadcastCommandNotifications(true, true);
}

void UBattleCommandWidget::BroadcastBattleText()
{
	BroadcastCommandNotifications(false, true);
}

void UBattleCommandWidget::BroadcastCommandNotifications(
	const bool bIncludeFocus,
	const bool bIncludeText)
{
	bCommandFocusNotificationPending |= bIncludeFocus;
	bBattleTextNotificationPending |= bIncludeText;
	if (bBroadcastingCommandNotifications)
	{
		return;
	}

	TGuardValue<bool> BroadcastGuard(bBroadcastingCommandNotifications, true);
	for (int32 PassIndex = 0;
		PassIndex < MaxReentrantNotificationPasses;
		++PassIndex)
	{
		const bool bBroadcastFocus = bCommandFocusNotificationPending;
		const bool bBroadcastText = bBattleTextNotificationPending;
		bCommandFocusNotificationPending = false;
		bBattleTextNotificationPending = false;

		if (!bBroadcastFocus && !bBroadcastText)
		{
			return;
		}

		const EBattleUICommand FocusedCommandSnapshot = FocusedCommand;
		const FText BattleTextSnapshot = CurrentBattleText;
		if (bBroadcastFocus)
		{
			OnCommandFocusChanged.Broadcast(FocusedCommandSnapshot);
			CommandFocusChangedNative.Broadcast(FocusedCommandSnapshot);
		}

		if (bBroadcastText)
		{
			OnBattleTextChanged.Broadcast(BattleTextSnapshot);
			BattleTextChangedNative.Broadcast(BattleTextSnapshot);
		}

		if (!bCommandFocusNotificationPending && !bBattleTextNotificationPending)
		{
			return;
		}
	}

	bCommandFocusNotificationPending = false;
	bBattleTextNotificationPending = false;
	bCommandMenuActive = false;
	SetVisibility(ESlateVisibility::Collapsed);
	UE_LOG(LogBattleCommandWidget, Error,
		TEXT("Battle command listeners exceeded the bounded re-entrant notification limit; command input was disabled."));
}
