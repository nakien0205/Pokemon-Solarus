#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "BattleHUDWidget.generated.h"

class UBattlePokemonHealthPanel;

/** Native presentation seam for the reusable battle HUD. */
UCLASS(Abstract, BlueprintType, Blueprintable)
class POKEMONSOLARUS_API UBattleHUDWidget : public UUserWidget
{
	GENERATED_BODY()

public:
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

protected:
	virtual void NativeConstruct() override;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UBattlePokemonHealthPanel> HealthPanel_Player = nullptr;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UBattlePokemonHealthPanel> HealthPanel_Opponent = nullptr;
};
