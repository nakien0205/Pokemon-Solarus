#pragma once

#include "CoreMinimal.h"
#include "Battle/BattleEncounterPolicyTypes.h"
#include "Battle/BattleSetup.h"

/** Stable failure from compiling one validated setup into C09A encounter policies. */
enum class EBattleEncounterPolicyError : uint8
{
	None = 0,
	InvalidSetup = 1,
	UnsupportedEncounterKind = 2,
	UnsupportedFormat = 3,
	InvalidTrainerShape = 4,
	IncompatibleCommandPolicy = 5,
	InvalidWildFleePolicy = 6,
	InvalidPartnerController = 7,
	InvalidWildReserve = 8
};

/** Pure C09A setup-to-policy compiler. It owns no battle state or RNG. */
class POKEMONSOLARUS_API FBattleEncounterPolicyCompiler
{
public:
	/** Validates encounter-only constraints and derives the complete typed policy set. */
	[[nodiscard]] static bool TryCompile(
		const FBattleSetup& Setup,
		FBattleCompiledEncounterPolicies& OutPolicies,
		EBattleEncounterPolicyError& OutError);
};
