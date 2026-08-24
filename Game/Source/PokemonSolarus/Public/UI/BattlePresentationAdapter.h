#pragma once

#include "CoreMinimal.h"
#include "Battle/BattleIdentifiers.h"

#if WITH_DEV_AUTOMATION_TESTS
class FBattlePresentationAdapterTestFixture;
#endif
class FBattleDecisionRequest;
class IBattleDisplayNameResolver;
class FBattleSnapshot;
enum class EBattleActionKind : uint8;
enum class EBattleOptionUnavailableReason : uint8;
struct FBattleCommandAvailability;
struct FBattleCommandDisplayState;
struct FBattleHUDDisplayState;

/** Stateless conversion from observer-safe Battle facts to display-ready Battle UI state. */
class POKEMONSOLARUS_API FBattlePresentationAdapter final
{
public:
	FBattlePresentationAdapter() = delete;

	/**
	 * Builds one top-level command projection for the pending request at ActingSlotId.
	 * Output is reset and no partial display state escapes when validation fails.
	 */
	[[nodiscard]] static bool TryBuildCommandDisplayState(
		const FBattleSnapshot& ObserverSnapshot,
		FActiveSlotId ActingSlotId,
		FBattleCommandDisplayState& OutDisplayState,
		FString& OutError);

	/**
	 * Builds the complete fail-closed Single Battle HUD projection from one
	 * observer-safe snapshot. Output is reset unless every required value validates.
	 */
	[[nodiscard]] static bool TryBuildHUDDisplayState(
		const FBattleSnapshot& ObserverSnapshot,
		FActiveSlotId ActingSlotId,
		const IBattleDisplayNameResolver& DisplayNameResolver,
		FBattleHUDDisplayState& OutDisplayState,
		FString& OutError);

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FBattlePresentationAdapterTestFixture;
#endif

	[[nodiscard]] static bool TryMapUnavailableReason(
		EBattleActionKind ActionKind,
		EBattleOptionUnavailableReason Reason,
		FText& OutText);
	[[nodiscard]] static bool TryBuildCommandAvailability(
		const FBattleDecisionRequest& Request,
		EBattleActionKind ActionKind,
		FBattleCommandAvailability& OutAvailability,
		FString& OutError);
	[[nodiscard]] static bool TryBuildCommandAvailabilities(
		const FBattleDecisionRequest& Request,
		FBattleCommandDisplayState& OutDisplayState,
		FString& OutError);
};
