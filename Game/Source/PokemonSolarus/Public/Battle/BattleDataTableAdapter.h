#pragma once

#include "CoreMinimal.h"
#include "Battle/BattleDefinitionCatalog.h"

class UDataTable;

/** Unreal-facing table set consumed only while constructing one frozen catalog. */
struct POKEMONSOLARUS_API FBattleDataTableSet
{
	const UDataTable* SpeciesForms = nullptr;
	const UDataTable* Natures = nullptr;
	const UDataTable* Moves = nullptr;
	const UDataTable* Abilities = nullptr;
	const UDataTable* Items = nullptr;
	const UDataTable* Conditions = nullptr;
	const UDataTable* TypeChart = nullptr;
};

/** Copies validated Unreal Data Table rows into an immutable plain-C++ catalog. */
class POKEMONSOLARUS_API FBattleDataTableAdapter
{
public:
	/**
	 * Validates table presence and row types, copies every row in stable name order,
	 * and delegates cross-reference validation to FBattleDefinitionCatalog.
	 * No UDataTable or row pointer is retained after this call returns.
	 */
	[[nodiscard]] static bool BuildCatalog(
		const FBattleDataTableSet& Tables,
		FBattleDefinitionCatalog& OutCatalog,
		TArray<FBattleCatalogDiagnostic>& OutDiagnostics);
};
