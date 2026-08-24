#pragma once

#include "CoreMinimal.h"
#include "Battle/BattleRuntimeSource.h"

class UDataTable;

/** Data-driven composition root for the approved initial local Battle. */
class FBattleDataTableRuntimeSource final : public IBattleRuntimeSource
{
public:
	/** Stores the soft reference used to load the runtime scenario on demand. */
	explicit FBattleDataTableRuntimeSource(TSoftObjectPtr<UDataTable> InRuntimeTable);

	virtual bool TryCreateInitialBattle(
		FBattleRuntimeBundle& OutBundle,
		FString& OutError) const override;

private:
	TSoftObjectPtr<UDataTable> RuntimeTable;
};
