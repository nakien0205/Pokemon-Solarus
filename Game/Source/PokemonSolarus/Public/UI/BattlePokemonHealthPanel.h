#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "BattlePokemonHealthPanel.generated.h"

#if WITH_DEV_AUTOMATION_TESTS
class FBattleRuntimePresentationTestFixture;
#endif
class UProgressBar;
class UTextBlock;

/** Reusable visual-only health panel for one battler. */
UCLASS(Abstract, BlueprintType, Blueprintable)
class POKEMONSOLARUS_API UBattlePokemonHealthPanel : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Returns whether NativeConstruct and every required visual binding are ready. */
	UFUNCTION(BlueprintPure, BlueprintCosmetic, Category = "Battle|UI|Health")
	bool IsStructurallyReady() const;

	/** Validates and applies one complete health-panel state without partial mutation. */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "Battle|UI|Health")
	bool ApplyDisplayState(
		const FText& PokemonName,
		int32 CurrentHP,
		int32 MaxHP,
		bool bExactHPShouldBeVisible);

	/** Updates the displayed Pokemon name without changing battle state. */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "Battle|UI|Health")
	void SetPokemonName(const FText& PokemonName);

	/** Shows exact current/maximum HP for the player or hides it for an opponent. */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "Battle|UI|Health")
	void SetExactHPVisible(bool bVisible);

	/** Immediately presents one authoritative HP value. Invalid values are clamped safely. */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "Battle|UI|Health")
	bool SetHPImmediate(int32 CurrentHP, int32 MaxHP);

	/** Animates only the displayed HP toward an authoritative target over the supplied duration. */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "Battle|UI|Health")
	bool AnimateHPTo(int32 CurrentHP, int32 MaxHP, float DurationSeconds);

	/** Completes the current visual animation without changing authoritative battle HP. */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "Battle|UI|Health")
	void CompleteHPAnimation();

	UFUNCTION(BlueprintPure, BlueprintCosmetic, Category = "Battle|UI|Health")
	bool IsHPAnimating() const { return bHPAnimationActive; }

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_PokemonName = nullptr;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UProgressBar> ProgressBar_HP = nullptr;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_HPValue = nullptr;

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FBattleRuntimePresentationTestFixture;
#endif

	void ApplyPokemonName();
	void ApplyExactHPVisibility();
	void ApplyHPVisuals();

	FText CachedPokemonName;
	bool bHasPokemonName = false;
	bool bExactHPVisible = true;
	bool bHasHPValue = false;
	bool bHPAnimationActive = false;
	int32 TargetCurrentHP = 0;
	int32 MaximumHP = 1;
	float DisplayedPercent = 1.0f;
	float AnimationStartPercent = 1.0f;
	float AnimationTargetPercent = 1.0f;
	float AnimationElapsedSeconds = 0.0f;
	float AnimationDurationSeconds = 0.0f;
	bool bNativeConstructed = false;
};
