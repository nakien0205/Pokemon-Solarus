#pragma once

#include "CoreMinimal.h"
#include "Battle/BattleSnapshot.h"

/**
 * Immutable selector input containing one filtered observation and one exact
 * core-generated legal-action request from that observation.
 */
class POKEMONSOLARUS_API FBattleActionSelectorInput
{
public:
	FBattleActionSelectorInput() = default;

	/** Deep-copies one observer snapshot and selects one request by stable array index. */
	[[nodiscard]] static bool TryCreate(
		const FBattleSnapshot& FilteredObservation,
		int32 RequestIndex,
		FBattleActionSelectorInput& OutInput,
		FBattleRejection& OutRejection);

	[[nodiscard]] bool IsValid() const { return bValid; }
	[[nodiscard]] const FBattleSnapshot& GetObservation() const { return Observation; }
	[[nodiscard]] const FBattleDecisionRequest& GetLegalActions() const;

private:
	bool bValid = false;
	FBattleSnapshot Observation;
	int32 LegalActionRequestIndex = INDEX_NONE;
};

/**
 * Selector contract shared by human adapters, later AI, and deterministic tests.
 * Implementations choose a typed payload only; the boundary and engine own legality.
 */
class POKEMONSOLARUS_API IBattleActionSelector
{
public:
	virtual ~IBattleActionSelector() = default;

	[[nodiscard]] virtual bool TrySelectAction(
		const FBattleActionSelectorInput& Input,
		FBattleDecision& OutDecision,
		FBattleRejection& OutRejection) = 0;
};

/** Legal-only selector boundary. Engine submission still performs final stale-state revalidation. */
class POKEMONSOLARUS_API FBattleActionSelectorBoundary
{
public:
	/** Runs one selector and validates its payload against the exact generated request. */
	[[nodiscard]] static bool TrySelectLegalAction(
		IBattleActionSelector& Selector,
		const FBattleActionSelectorInput& Input,
		FBattleDecision& OutDecision,
		FBattleRejection& OutRejection);
};
