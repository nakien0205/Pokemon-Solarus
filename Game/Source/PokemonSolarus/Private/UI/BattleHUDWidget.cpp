#include "UI/BattleHUDWidget.h"

#include "UI/BattlePokemonHealthPanel.h"

void UBattleHUDWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (HealthPanel_Player)
	{
		HealthPanel_Player->SetExactHPVisible(true);
	}

	if (HealthPanel_Opponent)
	{
		HealthPanel_Opponent->SetExactHPVisible(false);
	}
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
