#pragma once

#include "CoreMinimal.h"
#include "Battle/BattleDefinitions.h"
#include "Battle/BattleTargeting.h"

struct FBattleActivePositionState;
struct FBattleBattlerState;

/** One exact active occupant's turn-scoped move-redirection registration. */
struct FBattleMoveRedirectionRegistration
{
	FTurnId TurnId;
	FActionId SourceActionId;
	FBattleBattlerTarget Redirector;
};

/** Result of applying the typed RegisterTargetRedirection effect. */
enum class EBattleMoveRedirectionRegistrationOutcome : uint8
{
	Registered = 0,
	IneligibleFormat = 1,
	InvalidState = 2
};

/** Private deterministic owner for action-scoped move-redirection rules and lifecycle. */
class FBattleMoveRedirection
{
public:
	/** Returns whether a move contains the typed registration effect. */
	[[nodiscard]] static bool ContainsRegistrationEffect(
		const FBattleMoveDefinition& Move);

	/** Validates the complete exact descriptor shape owned by this rule. */
	[[nodiscard]] static bool IsRegistrationMoveDefinitionValid(
		const FBattleMoveDefinition& Move);

	/** Compares complete ordered registration identity for exact checkpoint checks. */
	[[nodiscard]] static bool AreRegistrationsIdentical(
		TConstArrayView<FBattleMoveRedirectionRegistration> Left,
		TConstArrayView<FBattleMoveRedirectionRegistration> Right);

	/** Validates that every registration names a unique current living occupant. */
	[[nodiscard]] static bool IsRegistrationCollectionValid(
		EBattleFormat Format,
		FTurnId TurnId,
		TConstArrayView<FBattleMoveRedirectionRegistration> Registrations,
		TConstArrayView<FBattleBattlerState> Battlers,
		TConstArrayView<FBattleActivePositionState> ActivePositions);

	/** Registers or replaces the exact current occupant without exposing priority as state. */
	[[nodiscard]] static EBattleMoveRedirectionRegistrationOutcome TryRegister(
		EBattleFormat Format,
		FTurnId TurnId,
		FActionId SourceActionId,
		const FBattleBattlerTarget& Redirector,
		TConstArrayView<FBattleBattlerState> Battlers,
		TConstArrayView<FBattleActivePositionState> ActivePositions,
		TArray<FBattleMoveRedirectionRegistration>& InOutRegistrations);

	/** Removes every registration belonging to one exact outgoing occupant. */
	static void RemoveForOccupant(
		TArray<FBattleMoveRedirectionRegistration>& InOutRegistrations,
		const FBattleBattlerTarget& Occupant);

	/** Clears every registration at the exact turn boundary. */
	static void Clear(TArray<FBattleMoveRedirectionRegistration>& InOutRegistrations);

	/**
	 * Selects at most one canonical opposing-side proposal. The supplied speed
	 * resolver must read a caller-owned copied current projection and consume no RNG.
	 */
	[[nodiscard]] static bool TrySelectWinningProposal(
		EBattleFormat Format,
		FTurnId TurnId,
		EBattleTargetClass TargetClass,
		const FBattleBattlerTarget& User,
		TConstArrayView<FBattleMoveRedirectionRegistration> Registrations,
		TConstArrayView<FBattleBattlerState> Battlers,
		TConstArrayView<FBattleActivePositionState> ActivePositions,
		TFunctionRef<bool(const FBattleBattlerTarget&, int32&)> ResolveEffectiveSpeed,
		TArray<FBattleTargetRedirectionProposal>& OutProposals);
};
