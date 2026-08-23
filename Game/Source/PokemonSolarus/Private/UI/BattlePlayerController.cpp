#include "UI/BattlePlayerController.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "UI/BattleHUDWidget.h"

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

void ABattlePlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (!IsLocalController())
	{
		return;
	}

	FInputModeGameOnly InputMode;
	SetInputMode(InputMode);
	bShowMouseCursor = false;

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

	UClass* LoadedHUDClass = BattleHUDWidgetClass.LoadSynchronous();
	if (!LoadedHUDClass)
	{
		UE_LOG(LogBattlePlayerController, Error,
			TEXT("The battle HUD Widget Blueprint could not be loaded."));
		return;
	}

	BattleHUDWidget = CreateWidget<UBattleHUDWidget>(this, LoadedHUDClass);
	if (!BattleHUDWidget)
	{
		UE_LOG(LogBattlePlayerController, Error,
			TEXT("The battle HUD could not be created. Confirm WBP_BattleHUD is reparented to BattleHUDWidget."));
		return;
	}

	if (!BattleHUDWidget->AddToPlayerScreen())
	{
		UE_LOG(LogBattlePlayerController, Error,
			TEXT("The battle HUD could not be added to the local player's screen."));
		BattleHUDWidget = nullptr;
	}
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

	LoadedBattleNavigateAction = BattleNavigateAction.LoadSynchronous();
	if (LoadedBattleNavigateAction)
	{
		EnhancedInputComponent->BindAction(
			LoadedBattleNavigateAction,
			ETriggerEvent::Started,
			this,
			&ABattlePlayerController::HandleBattleNavigate);
	}
	else
	{
		UE_LOG(LogBattlePlayerController, Error,
			TEXT("The battle Navigate Input Action could not be loaded."));
	}

	LoadedBattleConfirmAction = BattleConfirmAction.LoadSynchronous();
	if (LoadedBattleConfirmAction)
	{
		EnhancedInputComponent->BindAction(
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

	LoadedBattleCancelAction = BattleCancelAction.LoadSynchronous();
	if (LoadedBattleCancelAction)
	{
		EnhancedInputComponent->BindAction(
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

	if (BattleHUDWidget)
	{
		BattleHUDWidget->RemoveFromParent();
		BattleHUDWidget = nullptr;
	}

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
	if (BattleHUDWidget && !CardinalDirection.IsNearlyZero())
	{
		BattleHUDWidget->NavigateCommandMenu(CardinalDirection);
	}
}

void ABattlePlayerController::HandleBattleConfirm()
{
	if (BattleHUDWidget)
	{
		BattleHUDWidget->ConfirmCommandMenu();
	}
}

void ABattlePlayerController::HandleBattleCancel()
{
	if (BattleHUDWidget && BattleHUDWidget->IsAnyHPAnimating())
	{
		BattleHUDWidget->CompleteHPAnimations();
		return;
	}

	if (BattleHUDWidget)
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
