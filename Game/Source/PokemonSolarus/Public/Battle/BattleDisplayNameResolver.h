#pragma once

#include "CoreMinimal.h"
#include "Battle/BattleIdentifiers.h"

/** Immutable presentation lookup for localized Battle definition names. */
class POKEMONSOLARUS_API IBattleDisplayNameResolver
{
public:
	virtual ~IBattleDisplayNameResolver() = default;

	/** Resolves one species/form identity without inventing fallback display text. */
	[[nodiscard]] virtual bool TryResolveSpeciesName(
		FSpeciesFormId SpeciesFormId,
		FText& OutDisplayName) const = 0;
};
