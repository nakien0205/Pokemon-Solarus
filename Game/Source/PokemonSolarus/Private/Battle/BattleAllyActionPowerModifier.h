#pragma once

#include "CoreMinimal.h"
#include "Battle/BattleDefinitions.h"
#include "Battle/BattleFinalDamageCalculator.h"
#include "Battle/BattleTargeting.h"

struct FBattleActivePositionState;
struct FBattleBattlerState;
struct FBattleLockedActionState;

/** One exact ally occupant's ordered, turn- and action-scoped power modifier. */
struct FBattleAllyActionPowerModifierRegistration
{
	FTurnId TurnId;
	FActionId SourceActionId;
	FMoveId SourceMoveId;
	FActionId TargetActionId;
	FBattleBattlerTarget Target;
	int32 MagnitudeNumerator = 0;
	int32 MagnitudeDenominator = 1;
};

/** Typed outcome from applying one RegisterAllyActionPowerModifier descriptor. */
enum class EBattleAllyActionPowerModifierRegistrationOutcome : uint8
{
	Registered = 0,
	IneligibleFormat = 1,
	IneligibleTarget = 2,
	InvalidState = 3
};

/** Private deterministic owner for ally action power-modifier rules and lifetime. */
class FBattleAllyActionPowerModifier
{
public:
	/** Returns whether a move contains the typed registration effect. */
	[[nodiscard]] static bool ContainsRegistrationEffect(
		const FBattleMoveDefinition& Move);

	/** Validates the complete reusable descriptor shape owned by this rule. */
	[[nodiscard]] static bool IsRegistrationMoveDefinitionValid(
		const FBattleMoveDefinition& Move);

	/** Compares complete ordered registration identity for checkpoint checks. */
	[[nodiscard]] static bool AreRegistrationsIdentical(
		TConstArrayView<FBattleAllyActionPowerModifierRegistration> Left,
		TConstArrayView<FBattleAllyActionPowerModifierRegistration> Right);

	/** Validates every exact target binding against the current private state. */
	[[nodiscard]] static bool IsRegistrationCollectionValid(
		EBattleFormat Format,
		FTurnId TurnId,
		TConstArrayView<FBattleAllyActionPowerModifierRegistration> Registrations,
		TConstArrayView<FBattleBattlerState> Battlers,
		TConstArrayView<FBattleActivePositionState> ActivePositions,
		TConstArrayView<FBattleLockedActionState> LockedActions);

	/** Appends one eligible exact target binding without consuming RNG. */
	[[nodiscard]] static EBattleAllyActionPowerModifierRegistrationOutcome TryRegister(
		EBattleFormat Format,
		FTurnId TurnId,
		FActionId SourceActionId,
		const FMoveId& SourceMoveId,
		const FBattleBattlerTarget& Source,
		const FBattleBattlerTarget& Target,
		int32 MagnitudeNumerator,
		int32 MagnitudeDenominator,
		TConstArrayView<FBattleBattlerState> Battlers,
		TConstArrayView<FBattleActivePositionState> ActivePositions,
		TConstArrayView<FBattleLockedActionState> LockedActions,
		TArray<FBattleAllyActionPowerModifierRegistration>& InOutRegistrations,
		int32& OutBindingCountBefore,
		int32& OutBindingCountAfter);

	/** Converts an authored rational only when the actual damage checkpoint is reached. */
	[[nodiscard]] static bool TryConvertMagnitudeToQ12(
		int32 MagnitudeNumerator,
		int32 MagnitudeDenominator,
		int32& OutModifierQ12);

	/** Appends every matching modifier in stable registration order. */
	[[nodiscard]] static bool AppendMatchingPowerModifiers(
		FTurnId TurnId,
		FActionId ActionId,
		const FBattleBattlerTarget& DamageUser,
		bool bActualDamageBuild,
		TConstArrayView<FBattleAllyActionPowerModifierRegistration> Registrations,
		TArray<FBattleDamageModifier>& InOutPowerModifiers);

	/** Removes every registration bound to one completed or canceled action. */
	static void RemoveForAction(
		TArray<FBattleAllyActionPowerModifierRegistration>& InOutRegistrations,
		FActionId ActionId);

	/** Removes registrations targeting one exact outgoing occupant. */
	static void RemoveForOccupant(
		TArray<FBattleAllyActionPowerModifierRegistration>& InOutRegistrations,
		const FBattleBattlerTarget& Occupant);

	/** Clears all registrations before the turn identifier advances. */
	static void Clear(
		TArray<FBattleAllyActionPowerModifierRegistration>& InOutRegistrations);
};
