#pragma once

#include "CoreMinimal.h"
#include "UI/BattleCommandWidget.h"
#include "BattleHUDDisplayState.generated.h"

/** Display-ready identity and authoritative HP for one active Pokemon. */
USTRUCT(BlueprintType)
struct POKEMONSOLARUS_API FBattleHUDHealthDisplayState
{
	GENERATED_BODY()

	/** Required localized or authored display name. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle|UI|Health")
	FText PokemonName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle|UI|Health")
	int32 CurrentHP = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle|UI|Health")
	int32 MaxHP = 0;

	/** Returns whether the name and authoritative HP range are display-safe. */
	[[nodiscard]] bool IsValid() const
	{
		return !PokemonName.ToString().TrimStartAndEnd().IsEmpty()
			&& MaxHP > 0
			&& CurrentHP >= 0
			&& CurrentHP <= MaxHP;
	}
};

/** Atomic display-ready state required before the root Battle HUD may be shown. */
USTRUCT(BlueprintType)
struct POKEMONSOLARUS_API FBattleHUDDisplayState
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle|UI")
	FBattleHUDHealthDisplayState Player;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle|UI")
	FBattleHUDHealthDisplayState Opponent;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle|UI")
	FBattleCommandDisplayState Command;

	/** Validates one command projection without changing widget state. */
	[[nodiscard]] static bool IsCommandDisplayStateValid(
		const FBattleCommandDisplayState& DisplayState)
	{
		if (DisplayState.NormalPrompt.ToString().TrimStartAndEnd().IsEmpty())
		{
			return false;
		}

		const FBattleCommandAvailability* Availability[] = {
			&DisplayState.Fight,
			&DisplayState.Bag,
			&DisplayState.Pokemon,
			&DisplayState.Run};
		for (const FBattleCommandAvailability* CommandAvailability : Availability)
		{
			if (!CommandAvailability->bAvailable
				&& CommandAvailability->UnavailableReason.ToString()
					.TrimStartAndEnd().IsEmpty())
			{
				return false;
			}
		}
		return true;
	}

	/** Returns whether every required health and command value is display-safe. */
	[[nodiscard]] bool IsValid() const
	{
		return Player.IsValid()
			&& Opponent.IsValid()
			&& IsCommandDisplayStateValid(Command);
	}
};
