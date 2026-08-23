#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "BattlePlayerController.generated.h"

class UBattleHUDWidget;
class UInputAction;
class UInputMappingContext;

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
	TSoftObjectPtr<UInputAction> BattleCancelAction;

private:
	void HandleBattleCancel();

	UPROPERTY(Transient)
	TObjectPtr<UBattleHUDWidget> BattleHUDWidget = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> LoadedBattleInputMappingContext = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> LoadedBattleCancelAction = nullptr;
};
