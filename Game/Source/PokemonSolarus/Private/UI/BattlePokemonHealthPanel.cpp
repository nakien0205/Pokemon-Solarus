#include "UI/BattlePokemonHealthPanel.h"

#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"

#define LOCTEXT_NAMESPACE "BattlePokemonHealthPanel"

void UBattlePokemonHealthPanel::NativeConstruct()
{
	Super::NativeConstruct();

	ApplyPokemonName();
	ApplyExactHPVisibility();
	if (bHasHPValue)
	{
		ApplyHPVisuals();
	}
}

void UBattlePokemonHealthPanel::NativeTick(
	const FGeometry& MyGeometry,
	const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (!bHPAnimationActive)
	{
		return;
	}

	AnimationElapsedSeconds = FMath::Min(
		AnimationElapsedSeconds + FMath::Max(0.0f, InDeltaTime),
		AnimationDurationSeconds);

	const float Alpha = AnimationDurationSeconds > 0.0f
		? AnimationElapsedSeconds / AnimationDurationSeconds
		: 1.0f;
	DisplayedPercent = FMath::Lerp(
		AnimationStartPercent,
		AnimationTargetPercent,
		Alpha);

	if (Alpha >= 1.0f)
	{
		DisplayedPercent = AnimationTargetPercent;
		bHPAnimationActive = false;
	}

	ApplyHPVisuals();
}

void UBattlePokemonHealthPanel::SetPokemonName(const FText& PokemonName)
{
	CachedPokemonName = PokemonName;
	bHasPokemonName = true;
	ApplyPokemonName();
}

void UBattlePokemonHealthPanel::SetExactHPVisible(const bool bVisible)
{
	bExactHPVisible = bVisible;
	ApplyExactHPVisibility();
}

bool UBattlePokemonHealthPanel::SetHPImmediate(
	const int32 CurrentHP,
	const int32 MaxHP)
{
	if (MaxHP <= 0)
	{
		return false;
	}

	const int32 ClampedCurrentHP = FMath::Clamp(CurrentHP, 0, MaxHP);
	const bool bInputWasInRange = ClampedCurrentHP == CurrentHP;

	TargetCurrentHP = ClampedCurrentHP;
	MaximumHP = MaxHP;
	DisplayedPercent = static_cast<float>(TargetCurrentHP) / static_cast<float>(MaximumHP);
	AnimationStartPercent = DisplayedPercent;
	AnimationTargetPercent = DisplayedPercent;
	AnimationElapsedSeconds = 0.0f;
	AnimationDurationSeconds = 0.0f;
	bHasHPValue = true;
	bHPAnimationActive = false;

	ApplyHPVisuals();
	return bInputWasInRange;
}

bool UBattlePokemonHealthPanel::AnimateHPTo(
	const int32 CurrentHP,
	const int32 MaxHP,
	const float DurationSeconds)
{
	if (MaxHP <= 0)
	{
		return false;
	}

	const int32 ClampedCurrentHP = FMath::Clamp(CurrentHP, 0, MaxHP);
	const bool bInputWasInRange = ClampedCurrentHP == CurrentHP;
	if (!bHasHPValue || DurationSeconds <= 0.0f)
	{
		SetHPImmediate(ClampedCurrentHP, MaxHP);
		return bInputWasInRange;
	}

	TargetCurrentHP = ClampedCurrentHP;
	MaximumHP = MaxHP;
	AnimationStartPercent = FMath::Clamp(DisplayedPercent, 0.0f, 1.0f);
	AnimationTargetPercent = static_cast<float>(TargetCurrentHP) / static_cast<float>(MaximumHP);
	AnimationElapsedSeconds = 0.0f;
	AnimationDurationSeconds = DurationSeconds;
	bHasHPValue = true;
	bHPAnimationActive = !FMath::IsNearlyEqual(
		AnimationStartPercent,
		AnimationTargetPercent);

	if (!bHPAnimationActive)
	{
		DisplayedPercent = AnimationTargetPercent;
	}

	ApplyHPVisuals();
	return bInputWasInRange;
}

void UBattlePokemonHealthPanel::CompleteHPAnimation()
{
	if (!bHPAnimationActive)
	{
		return;
	}

	DisplayedPercent = AnimationTargetPercent;
	AnimationElapsedSeconds = AnimationDurationSeconds;
	bHPAnimationActive = false;
	ApplyHPVisuals();
}

void UBattlePokemonHealthPanel::ApplyPokemonName()
{
	if (bHasPokemonName && Text_PokemonName)
	{
		Text_PokemonName->SetText(CachedPokemonName);
	}
}

void UBattlePokemonHealthPanel::ApplyExactHPVisibility()
{
	if (Text_HPValue)
	{
		Text_HPValue->SetVisibility(
			bExactHPVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
}

void UBattlePokemonHealthPanel::ApplyHPVisuals()
{
	if (!bHasHPValue)
	{
		return;
	}

	DisplayedPercent = FMath::Clamp(DisplayedPercent, 0.0f, 1.0f);
	if (ProgressBar_HP)
	{
		ProgressBar_HP->SetPercent(DisplayedPercent);
	}

	if (Text_HPValue)
	{
		const int32 DisplayedCurrentHP = bHPAnimationActive
			? FMath::Clamp(FMath::RoundToInt(DisplayedPercent * MaximumHP), 0, MaximumHP)
			: TargetCurrentHP;
		Text_HPValue->SetText(FText::Format(
			LOCTEXT("ExactHPFormat", "{0} / {1}"),
			FText::AsNumber(DisplayedCurrentHP),
			FText::AsNumber(MaximumHP)));
	}
}

#undef LOCTEXT_NAMESPACE
