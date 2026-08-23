#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "BattlePlayerController.generated.h"

class UBattleHUDWidget;
class UInputAction;
class UInputMappingContext;
struct FInputActionValue;

/** Battle-only local player input and HUD owner. */
UCLASS()
class POKEMONSOLARUS_API ABattlePlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ABattlePlayerController();

	UFUNCTION(BlueprintPure, Category = "Battle|UI")
	UBattleHUDWidget* GetBattleHUDWidget() const { return BattleHUDWidget; }

protected:
	virtual void BeginPlay() override;
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
	void HandleBattleNavigate(const FInputActionValue& InputValue);
	void HandleBattleConfirm();
	void HandleBattleCancel();
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
};
