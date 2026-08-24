#include "UI/BattlePlayerController.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "TimerManager.h"
#include "UI/BattleHUDDisplayState.h"
#include "UI/BattleHUDWidget.h"
#include "UI/BattlePresentationAdapter.h"

DEFINE_LOG_CATEGORY_STATIC(LogBattlePlayerController, Log, All);

ABattlePlayerController::ABattlePlayerController()
{
	BattleHUDWidgetClass = TSoftClassPtr<UBattleHUDWidget>(FSoftObjectPath(
		TEXT("/Game/UI/Battle/WBP_BattleHUD.WBP_BattleHUD_C")));
	BattleInputMappingContext = TSoftObjectPtr<UInputMappingContext>(FSoftObjectPath(
		TEXT("/Game/Input/IMC_Battle.IMC_Battle")));
	BattleNavigateAction = TSoftObjectPtr<UInputAction>(FSoftObjectPath(
		TEXT("/Game/Input/IA_BattleNavigate.IA_BattleNavigate")));
	BattleConfirmAction = TSoftObjectPtr<UInputAction>(FSoftObjectPath(
		TEXT("/Game/Input/IA_BattleConfirm.IA_BattleConfirm")));
	BattleCancelAction = TSoftObjectPtr<UInputAction>(FSoftObjectPath(
		TEXT("/Game/Input/IA_BattleCancel.IA_BattleCancel")));

	bShowMouseCursor = false;
	bEnableClickEvents = false;
	bEnableMouseOverEvents = false;
	bEnableTouchEvents = false;
	bEnableTouchOverEvents = false;
}

bool ABattlePlayerController::PresentCommandSelection(
	const FBattleSnapshot& ObserverSnapshot,
	const FActiveSlotId ActingSlotId)
{
	if (!EnsureCommandPresentationReady())
	{
		return false;
	}

	FBattleCommandDisplayState DisplayState;
	FString AdapterError;
	if (!FBattlePresentationAdapter::TryBuildCommandDisplayState(
			ObserverSnapshot,
			ActingSlotId,
			DisplayState,
			AdapterError))
	{
		DisableBattleHUDInputPreservingPresentation();
		UE_LOG(LogBattlePlayerController, Error,
			TEXT("Cannot present the Battle command selection: %s"),
			*AdapterError);
		return false;
	}

	if (!BattleHUDWidget->ApplyCommandDisplayState(DisplayState))
	{
		DisableBattleHUDInputPreservingPresentation();
		UE_LOG(LogBattlePlayerController, Error,
			TEXT("The Battle HUD rejected the adapter's command display state."));
		return false;
	}
	return true;
}

bool ABattlePlayerController::EnsureCommandPresentationReady()
{
	if (!IsBattleHUDAvailable())
	{
		DiscardBattleHUD();
		UE_LOG(LogBattlePlayerController, Error,
			TEXT("Cannot update Battle command selection because the HUD is structurally unavailable."));
		return false;
	}

	if (!IsBattleHUDVisible())
	{
		DisableBattleHUDInputPreservingPresentation();
		UE_LOG(LogBattlePlayerController, Error,
			TEXT("Cannot update Battle command selection before a complete HUD presentation is visible."));
		return false;
	}
	return true;
}

bool ABattlePlayerController::ApplyBattleHUDDisplayState(
	const FBattleHUDDisplayState& DisplayState)
{
	if (!IsBattleHUDAvailable())
	{
		DiscardBattleHUD();
		UE_LOG(LogBattlePlayerController, Error,
			TEXT("Cannot apply a Battle HUD display state because the HUD is structurally unavailable."));
		return false;
	}

	if (!BattleHUDWidget->ApplyHUDDisplayState(DisplayState))
	{
		UE_LOG(LogBattlePlayerController, Error,
			TEXT("The Battle HUD rejected the complete display state; command input remains disabled."));
		return false;
	}
	return true;
}

bool ABattlePlayerController::IsBattleHUDAvailable() const
{
	return IsValid(BattleHUDWidget)
		&& BattleHUDPresentationGeneration > 0
		&& AcceptedBattleHUDConstructionSerial > 0
		&& !bBattleHUDAttachmentFinalizePending
		&& BattleHUDWidget->GetNativeConstructionSerial()
			== AcceptedBattleHUDConstructionSerial
		&& BattleHUDWidget->IsInViewport()
		&& BattleHUDWidget->IsStructurallyReady();
}

bool ABattlePlayerController::IsBattleHUDReady() const
{
	return IsBattleHUDAvailable();
}

bool ABattlePlayerController::IsBattleHUDVisible() const
{
	return IsBattleHUDAvailable()
		&& BattleHUDWidget->IsPresentationVisible();
}

bool ABattlePlayerController::IsBattleCommandInputReady() const
{
	return IsBattleHUDAvailable()
		&& BattleHUDWidget->IsCommandInputEnabled();
}

void ABattlePlayerController::DisableBattleHUDInputPreservingPresentation()
{
	if (IsValid(BattleHUDWidget))
	{
		BattleHUDWidget->DisableCommandInputPreservingPresentation();
	}
}

void ABattlePlayerController::DismissCommandSelection()
{
	if (IsValid(BattleHUDWidget))
	{
		BattleHUDWidget->HideCommandMenu();
	}
}

void ABattlePlayerController::BeginPlay()
{
	Super::BeginPlay();
	InitializeLocalBattlePresentation();
}

void ABattlePlayerController::ReceivedPlayer()
{
	Super::ReceivedPlayer();
	InitializeLocalBattlePresentation();
}

void ABattlePlayerController::InitializeLocalBattlePresentation()
{
	if (!HasActorBegunPlay()
		|| !IsLocalController()
		|| GetLocalPlayer() == nullptr)
	{
		return;
	}

	FInputModeGameOnly InputMode;
	SetInputMode(InputMode);
	bShowMouseCursor = false;
	if (!LoadedBattleInputMappingContext)
	{
		InitializeBattleInputMapping();
	}
	if (!IsValid(BattleHUDWidget))
	{
		(void)TryCreateBattleHUD();
	}
}

void ABattlePlayerController::InitializeBattleInputMapping()
{
	if (ULocalPlayer* LocalPlayer = GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* InputSubsystem =
			ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LocalPlayer))
		{
			LoadedBattleInputMappingContext = BattleInputMappingContext.LoadSynchronous();
			if (LoadedBattleInputMappingContext)
			{
				InputSubsystem->AddMappingContext(LoadedBattleInputMappingContext, 100);
			}
			else
			{
				UE_LOG(LogBattlePlayerController, Error,
					TEXT("The battle input mapping context could not be loaded."));
			}
		}
	}
}

bool ABattlePlayerController::TryCreateBattleHUD()
{
	DiscardBattleHUD();

	UBattleHUDWidget* CandidateHUD = CreateBattleHUDCandidate();
	if (!CandidateHUD)
	{
		return false;
	}

	BindBattleHUDLifecycle(*CandidateHUD);
	if (!TryAttachAndValidateBattleHUD(*CandidateHUD))
	{
		UnbindBattleHUDLifecycle(*CandidateHUD);
		RemoveBattleHUDFromScreen(*CandidateHUD);
		return false;
	}

	BattleHUDWidget = CandidateHUD;
	if (!TryAcceptBattleHUDAttachment(*CandidateHUD))
	{
		DiscardBattleHUD();
		return false;
	}

	BattleHUDAvailableNativeDelegate.Broadcast(*this);
	return true;
}

UBattleHUDWidget* ABattlePlayerController::CreateBattleHUDCandidate()
{
	UClass* LoadedHUDClass = BattleHUDWidgetClass.LoadSynchronous();
	if (!LoadedHUDClass)
	{
		UE_LOG(LogBattlePlayerController, Error,
			TEXT("The battle HUD Widget Blueprint could not be loaded."));
		return nullptr;
	}

	UBattleHUDWidget* CandidateHUD = CreateWidget<UBattleHUDWidget>(
		this,
		LoadedHUDClass);
	if (!CandidateHUD)
	{
		UE_LOG(LogBattlePlayerController, Error,
			TEXT("The battle HUD could not be created. Confirm WBP_BattleHUD is reparented to BattleHUDWidget."));
		return nullptr;
	}
	return CandidateHUD;
}

bool ABattlePlayerController::TryAttachAndValidateBattleHUD(
	UBattleHUDWidget& CandidateHUD)
{
	CandidateHUD.SetVisibility(ESlateVisibility::Collapsed);
	const bool bAddedToPlayerScreen = CandidateHUD.AddToPlayerScreen();
	CandidateHUD.SetVisibility(ESlateVisibility::Collapsed);
	if (bAddedToPlayerScreen
		&& CandidateHUD.IsInViewport()
		&& CandidateHUD.IsStructurallyReady())
	{
		return true;
	}

	UE_LOG(LogBattlePlayerController, Error,
		TEXT("The battle HUD failed construction, viewport attachment, or required binding validation."));
	return false;
}

void ABattlePlayerController::BindBattleHUDLifecycle(
	UBattleHUDWidget& HUDWidget)
{
	BattleHUDConstructedHandle = HUDWidget.GetConstructedNativeDelegate().AddUObject(
		this,
		&ABattlePlayerController::HandleBattleHUDConstructed);
}

void ABattlePlayerController::UnbindBattleHUDLifecycle(
	UBattleHUDWidget& HUDWidget)
{
	if (BattleHUDConstructedHandle.IsValid())
	{
		HUDWidget.GetConstructedNativeDelegate().Remove(BattleHUDConstructedHandle);
		BattleHUDConstructedHandle.Reset();
	}
}

void ABattlePlayerController::HandleBattleHUDConstructed(
	UBattleHUDWidget& ConstructedHUD)
{
	if (BattleHUDWidget != &ConstructedHUD
		|| bBattleHUDAttachmentFinalizePending)
	{
		return;
	}

	bBattleHUDAttachmentFinalizePending = true;
	UWorld* World = GetWorld();
	if (!World)
	{
		bBattleHUDAttachmentFinalizePending = false;
		return;
	}
	(void)World->GetTimerManager().SetTimerForNextTick(
		this,
		&ABattlePlayerController::FinalizePendingBattleHUDAttachment);
}

void ABattlePlayerController::FinalizePendingBattleHUDAttachment()
{
	bBattleHUDAttachmentFinalizePending = false;
	if (!IsValid(BattleHUDWidget) || !BattleHUDWidget->IsInViewport())
	{
		return;
	}
	if (!BattleHUDWidget->IsStructurallyReady())
	{
		DiscardBattleHUD();
		return;
	}
	if (BattleHUDWidget->GetNativeConstructionSerial()
		== AcceptedBattleHUDConstructionSerial)
	{
		return;
	}

	BattleHUDWidget->DisableCommandInputPreservingPresentation();
	BattleHUDWidget->SetVisibility(ESlateVisibility::Collapsed);
	if (!TryAcceptBattleHUDAttachment(*BattleHUDWidget))
	{
		DiscardBattleHUD();
		return;
	}
	BattleHUDAvailableNativeDelegate.Broadcast(*this);
}

bool ABattlePlayerController::TryAcceptBattleHUDAttachment(
	const UBattleHUDWidget& HUDWidget)
{
	const uint64 ConstructionSerial = HUDWidget.GetNativeConstructionSerial();
	if (ConstructionSerial == 0
		|| ConstructionSerial == AcceptedBattleHUDConstructionSerial)
	{
		UE_LOG(LogBattlePlayerController, Error,
			TEXT("The Battle HUD attachment has no new completed construction pass."));
		return false;
	}
	if (!TryAdvanceBattleHUDPresentationGeneration())
	{
		return false;
	}

	AcceptedBattleHUDConstructionSerial = ConstructionSerial;
	return true;
}

bool ABattlePlayerController::TryAdvanceBattleHUDPresentationGeneration()
{
	if (BattleHUDPresentationGeneration == MAX_uint64)
	{
		UE_LOG(LogBattlePlayerController, Error,
			TEXT("The Battle HUD presentation generation cannot advance."));
		return false;
	}

	++BattleHUDPresentationGeneration;
	return true;
}

void ABattlePlayerController::RemoveBattleHUDFromScreen(
	UBattleHUDWidget& HUDWidget)
{
	HUDWidget.DisableCommandInputPreservingPresentation();
	HUDWidget.SetVisibility(ESlateVisibility::Collapsed);
	HUDWidget.RemoveFromParent();
}

void ABattlePlayerController::DiscardBattleHUD()
{
	bBattleHUDAttachmentFinalizePending = false;
	AcceptedBattleHUDConstructionSerial = 0;
	if (!IsValid(BattleHUDWidget))
	{
		BattleHUDWidget = nullptr;
		BattleHUDConstructedHandle.Reset();
		return;
	}

	UnbindBattleHUDLifecycle(*BattleHUDWidget);
	RemoveBattleHUDFromScreen(*BattleHUDWidget);
	BattleHUDWidget = nullptr;
}

void ABattlePlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	UEnhancedInputComponent* EnhancedInputComponent =
		Cast<UEnhancedInputComponent>(InputComponent);
	if (!EnhancedInputComponent)
	{
		UE_LOG(LogBattlePlayerController, Error,
			TEXT("BattlePlayerController requires EnhancedInputComponent."));
		return;
	}

	BindBattleNavigateAction(*EnhancedInputComponent);
	BindBattleConfirmAction(*EnhancedInputComponent);
	BindBattleCancelAction(*EnhancedInputComponent);
}

void ABattlePlayerController::BindBattleNavigateAction(
	UEnhancedInputComponent& EnhancedInputComponent)
{
	LoadedBattleNavigateAction = BattleNavigateAction.LoadSynchronous();
	if (LoadedBattleNavigateAction)
	{
		EnhancedInputComponent.BindAction(
			LoadedBattleNavigateAction,
			ETriggerEvent::Triggered,
			this,
			&ABattlePlayerController::HandleBattleNavigate);

		EnhancedInputComponent.BindAction(
			LoadedBattleNavigateAction,
			ETriggerEvent::Completed,
			this,
			&ABattlePlayerController::HandleBattleNavigateEnded);

		EnhancedInputComponent.BindAction(
			LoadedBattleNavigateAction,
			ETriggerEvent::Canceled,
			this,
			&ABattlePlayerController::HandleBattleNavigateEnded);
	}
	else
	{
		UE_LOG(LogBattlePlayerController, Error,
			TEXT("The battle Navigate Input Action could not be loaded."));
	}
}

void ABattlePlayerController::BindBattleConfirmAction(
	UEnhancedInputComponent& EnhancedInputComponent)
{
	LoadedBattleConfirmAction = BattleConfirmAction.LoadSynchronous();
	if (LoadedBattleConfirmAction)
	{
		EnhancedInputComponent.BindAction(
			LoadedBattleConfirmAction,
			ETriggerEvent::Started,
			this,
			&ABattlePlayerController::HandleBattleConfirm);
	}
	else
	{
		UE_LOG(LogBattlePlayerController, Error,
			TEXT("The battle Confirm Input Action could not be loaded."));
	}
}

void ABattlePlayerController::BindBattleCancelAction(
	UEnhancedInputComponent& EnhancedInputComponent)
{
	LoadedBattleCancelAction = BattleCancelAction.LoadSynchronous();
	if (LoadedBattleCancelAction)
	{
		EnhancedInputComponent.BindAction(
			LoadedBattleCancelAction,
			ETriggerEvent::Started,
			this,
			&ABattlePlayerController::HandleBattleCancel);
	}
	else
	{
		UE_LOG(LogBattlePlayerController, Error,
			TEXT("The battle Cancel Input Action could not be loaded."));
	}
}

void ABattlePlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (ULocalPlayer* LocalPlayer = GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* InputSubsystem =
			ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LocalPlayer))
		{
			if (LoadedBattleInputMappingContext)
			{
				InputSubsystem->RemoveMappingContext(LoadedBattleInputMappingContext);
			}
		}
	}

	DiscardBattleHUD();

	Super::EndPlay(EndPlayReason);
}

void ABattlePlayerController::HandleBattleNavigate(
	const FInputActionValue& InputValue)
{
	if (InputValue.GetValueType() != EInputActionValueType::Axis2D)
	{
		UE_LOG(LogBattlePlayerController, Error,
			TEXT("IA_BattleNavigate must provide an Axis2D value."));
		return;
	}

	const FVector2D CardinalDirection = QuantizeNavigationInput(
		InputValue.Get<FVector2D>());
	if (CardinalDirection.Equals(LastBattleNavigateDirection))
	{
		return;
	}

	LastBattleNavigateDirection = CardinalDirection;
	if (IsBattleCommandInputReady() && !CardinalDirection.IsNearlyZero())
	{
		BattleHUDWidget->NavigateCommandMenu(CardinalDirection);
	}
}

void ABattlePlayerController::HandleBattleNavigateEnded()
{
	LastBattleNavigateDirection = FVector2D::ZeroVector;
}

void ABattlePlayerController::HandleBattleConfirm()
{
	if (IsBattleCommandInputReady())
	{
		BattleHUDWidget->ConfirmCommandMenu();
	}
}

void ABattlePlayerController::HandleBattleCancel()
{
	if (IsBattleHUDVisible() && BattleHUDWidget->IsAnyHPAnimating())
	{
		BattleHUDWidget->CompleteHPAnimations();
		return;
	}

	if (IsBattleCommandInputReady())
	{
		BattleHUDWidget->CancelCommandMenu();
	}
}

FVector2D ABattlePlayerController::QuantizeNavigationInput(
	const FVector2D& InputValue)
{
	const double AbsoluteX = FMath::Abs(InputValue.X);
	const double AbsoluteY = FMath::Abs(InputValue.Y);
	if (FMath::IsNearlyZero(AbsoluteX) && FMath::IsNearlyZero(AbsoluteY))
	{
		return FVector2D::ZeroVector;
	}

	if (FMath::IsNearlyEqual(AbsoluteX, AbsoluteY))
	{
		return FVector2D::ZeroVector;
	}

	return AbsoluteX > AbsoluteY
		? FVector2D(InputValue.X > 0.0 ? 1.0 : -1.0, 0.0)
		: FVector2D(0.0, InputValue.Y > 0.0 ? 1.0 : -1.0);
}
