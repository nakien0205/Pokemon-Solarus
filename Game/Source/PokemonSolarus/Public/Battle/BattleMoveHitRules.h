#pragma once

#include "CoreMinimal.h"
#include "Battle/BattleDefinitions.h"
#include "Battle/BattleTypeChart.h"

/** Typed reason an authored move hit-rule combination is incompatible. */
enum class EBattleMoveHitRuleValidationError : uint8
{
	None = 0,
	RequiresBattlerTarget = 1,
	StatusTypeImmunityRequiresStatusMove = 2,
	PoisonUserBypassRequiresPoisonStatusMove = 3,
	PoisonUserBypassRequiresNumericAccuracy = 4
};

/** Conditional hit behavior derived from the move user's immutable typing. */
struct POKEMONSOLARUS_API FBattleMoveUserHitQualifiers
{
	bool bBypassSemiInvulnerability = false;
	bool bBypassAccuracy = false;
};

/** Typed move-owned immunity reason resolved before Ability, item, and accuracy gates. */
enum class EBattleMoveHitImmunityReason : uint8
{
	None = 0,
	TypeChart = 1,
	Powder = 2,
	Invalid = 255
};

/** Complete pure result from the move-owned immunity checkpoint. */
struct POKEMONSOLARUS_API FBattleMoveHitImmunityResult
{
	EBattleMoveHitImmunityReason Reason = EBattleMoveHitImmunityReason::Invalid;

	/** Returns whether an authored move rule made the target immune. */
	[[nodiscard]] bool IsImmune() const
	{
		return Reason == EBattleMoveHitImmunityReason::TypeChart
			|| Reason == EBattleMoveHitImmunityReason::Powder;
	}
};

/** Stateless authored move hit qualifiers shared by catalog and runtime validation. */
class POKEMONSOLARUS_API FBattleMoveHitRules
{
public:
	/** Validates only the compatibility contract owned by the R4A move traits. */
	[[nodiscard]] static bool TryValidateMoveDefinition(
		const FBattleMoveDefinition& Move,
		EBattleMoveHitRuleValidationError& OutError);

	/** Resolves conditional reachability and accuracy bypass from the user's types. */
	[[nodiscard]] static bool TryResolveUserHitQualifiers(
		const FBattleMoveDefinition& Move,
		EPokemonType UserPrimaryType,
		EPokemonType UserSecondaryType,
		FBattleMoveUserHitQualifiers& OutQualifiers);

	/** Resolves opted-in status type immunity first, then Powder immunity. */
	[[nodiscard]] static bool TryResolveMoveImmunity(
		const FBattleMoveDefinition& Move,
		EPokemonType TargetPrimaryType,
		EPokemonType TargetSecondaryType,
		const FBattleTypeChart& TypeChart,
		FBattleMoveHitImmunityResult& OutResult);
};
