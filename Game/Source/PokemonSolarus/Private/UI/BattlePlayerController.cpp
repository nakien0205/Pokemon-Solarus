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
	BattleCancelAction = TSoftObjectPtr<UInputAction>(FSoftObjectPath(
		TEXT("/Game/Input/IA_BattleCancel.IA_BattleCancel")));

	bShowMouseCursor = false;
	bEnableClickEvents = false;
	bEnableMouseOverEvents = false;
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

	LoadedBattleCancelAction = BattleCancelAction.LoadSynchronous();
	if (!LoadedBattleCancelAction)
	{
		UE_LOG(LogBattlePlayerController, Error,
			TEXT("The battle Cancel Input Action could not be loaded."));
		return;
	}

	EnhancedInputComponent->BindAction(
		LoadedBattleCancelAction,
		ETriggerEvent::Started,
		this,
		&ABattlePlayerController::HandleBattleCancel);
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

void ABattlePlayerController::HandleBattleCancel()
{
	if (BattleHUDWidget && BattleHUDWidget->IsAnyHPAnimating())
	{
		BattleHUDWidget->CompleteHPAnimations();
	}
}
