#pragma once

#include "CoreMinimal.h"
#include "Battle/BattleDisplayNameResolver.h"
#include "Battle/BattleEngine.h"

/** Atomically created gameplay and presentation dependencies for one local Battle. */
struct POKEMONSOLARUS_API FBattleRuntimeBundle
{
	TUniquePtr<FBattleEngine> Engine;
	FTrainerId LocalTrainerId;
	TSharedPtr<const IBattleDisplayNameResolver> DisplayNames;

	/** Returns whether every required runtime dependency is present. */
	[[nodiscard]] bool IsValid() const
	{
		return Engine.IsValid() && LocalTrainerId.IsValid() && DisplayNames.IsValid();
	}

	/** Clears every dependency after a failed or ended initialization. */
	void Reset()
	{
		Engine.Reset();
		LocalTrainerId = FTrainerId();
		DisplayNames.Reset();
	}
};

/** Injected source of a fully validated local Battle runtime. */
class POKEMONSOLARUS_API IBattleRuntimeSource
{
public:
	virtual ~IBattleRuntimeSource() = default;

	/** Creates the approved initial Battle atomically or returns a diagnostic. */
	[[nodiscard]] virtual bool TryCreateInitialBattle(
		FBattleRuntimeBundle& OutBundle,
		FString& OutError) const = 0;
};
