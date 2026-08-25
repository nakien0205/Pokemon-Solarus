#pragma once

#include "CoreMinimal.h"
#include "Battle/BattleIdentifiers.h"

/** IDs and typed rule purpose attached to one semantic battle RNG call. */
struct POKEMONSOLARUS_API FBattleRandomContext
{
	FBattleId BattleId;
	FTurnId TurnId;
	FActionId ActionId;
	FResolutionId ResolutionId;
	FDefinitionId RulePurpose;

	/**
	 * A battle, turn, resolution, and rule purpose are required.
	 * ActionId may be invalid for queue-wide or end-of-turn checks.
	 */
	[[nodiscard]] bool IsValid() const
	{
		return BattleId.IsValid()
			&& TurnId.IsValid()
			&& ResolutionId.IsValid()
			&& RulePurpose.IsValid();
	}
};

/** Complete immutable-by-copy trace record returned for one valid RNG call. */
struct POKEMONSOLARUS_API FBattleRandomDraw
{
	uint32 InclusiveMinimum = 0;
	uint32 InclusiveMaximum = 0;
	uint64 Bound = 0;
	uint64 RawValue = 0;
	uint32 Result = 0;
	uint64 CallOrdinal = 0;
	FBattleId BattleId;
	FTurnId TurnId;
	FActionId ActionId;
	FResolutionId ResolutionId;
	FDefinitionId RulePurpose;

	friend bool operator==(const FBattleRandomDraw& Left, const FBattleRandomDraw& Right)
	{
		return Left.InclusiveMinimum == Right.InclusiveMinimum
			&& Left.InclusiveMaximum == Right.InclusiveMaximum
			&& Left.Bound == Right.Bound
			&& Left.RawValue == Right.RawValue
			&& Left.Result == Right.Result
			&& Left.CallOrdinal == Right.CallOrdinal
			&& Left.BattleId == Right.BattleId
			&& Left.TurnId == Right.TurnId
			&& Left.ActionId == Right.ActionId
			&& Left.ResolutionId == Right.ResolutionId
			&& Left.RulePurpose == Right.RulePurpose;
	}

	friend bool operator!=(const FBattleRandomDraw& Left, const FBattleRandomDraw& Right)
	{
		return !(Left == Right);
	}
};

/** Typed reason why a staged Battle RNG transaction could not commit. */
enum class EBattleRandomTransactionCommitError : uint8
{
	None = 0,
	AlreadyFinalized,
	ParentIdentityMismatch,
	ParentPositionMismatch,
	ResolutionIdentityMismatch,
	ActionIdentityMismatch,
	StagedDrawRejected
};

class IBattleRandomTransaction;

/** Lifetime-safe injectable source of bounded, traced battle randomness. */
class POKEMONSOLARUS_API IBattleRandom
{
public:
	virtual ~IBattleRandom() = default;

	/**
	 * Attempts one uniform draw over the inclusive range.
	 * Invalid bounds or context return false, reset OutDraw, and consume nothing.
	 */
	[[nodiscard]] virtual bool TryDrawUniform(
		uint32 InclusiveMinimum,
		uint32 InclusiveMaximum,
		const FBattleRandomContext& Context,
		FBattleRandomDraw& OutDraw) = 0;

	/** Returns the ordered, read-only trace of every successful semantic draw. */
	[[nodiscard]] virtual TConstArrayView<FBattleRandomDraw> GetTrace() const = 0;

	/**
	 * Creates a private working stream for one resolution checkpoint and its
	 * owning action. Invalid identities reset OutTransaction and change nothing.
	 */
	[[nodiscard]] virtual bool TryCreateTransaction(
		FResolutionId ResolutionId,
		FActionId OwningActionId,
		TUniquePtr<IBattleRandomTransaction>& OutTransaction) = 0;
};

/**
 * Private working RNG stream whose draws are invisible to its parent until one
 * exact commit. Transactions are resolution/action scoped and cannot nest.
 */
class POKEMONSOLARUS_API IBattleRandomTransaction : public IBattleRandom
{
public:
	virtual ~IBattleRandomTransaction() override = default;

	/** Transactions cannot create nested transactions. */
	[[nodiscard]] virtual bool TryCreateTransaction(
		FResolutionId ResolutionId,
		FActionId OwningActionId,
		TUniquePtr<IBattleRandomTransaction>& OutTransaction) final override;

	/**
	 * Publishes the staged working position and ordered trace exactly once.
	 * Every false return supplies a typed reason and leaves Parent unchanged.
	 */
	[[nodiscard]] virtual bool TryCommit(
		IBattleRandom& Parent,
		FResolutionId ResolutionId,
		FActionId OwningActionId,
		EBattleRandomTransactionCommitError& OutError) = 0;

	/** Discards all staged work. Repeated rollback is harmless. */
	virtual void Rollback() = 0;
};

/** Fixed SplitMix64 stream with unbiased bounded mapping and stable replay behavior. */
class POKEMONSOLARUS_API FSeededBattleRandom final : public IBattleRandom
{
public:
	/** Creates one deterministic stream from an explicit seed, including seed zero. */
	explicit FSeededBattleRandom(uint64 InSeed);

	FSeededBattleRandom(const FSeededBattleRandom&) = delete;
	FSeededBattleRandom& operator=(const FSeededBattleRandom&) = delete;

	virtual bool TryDrawUniform(
		uint32 InclusiveMinimum,
		uint32 InclusiveMaximum,
		const FBattleRandomContext& Context,
		FBattleRandomDraw& OutDraw) override;

	virtual TConstArrayView<FBattleRandomDraw> GetTrace() const override;

	virtual bool TryCreateTransaction(
		FResolutionId ResolutionId,
		FActionId OwningActionId,
		TUniquePtr<IBattleRandomTransaction>& OutTransaction) override;

	/** Returns the seed needed to reproduce this stream. */
	[[nodiscard]] uint64 GetInitialSeed() const
	{
		return InitialSeed;
	}

private:
	class FTransaction;

	[[nodiscard]] uint64 NextRawValue();

	uint64 InitialSeed = 0;
	uint64 State = 0;
	uint64 NextCallOrdinal = 1;
	uint64 PositionVersion = 1;
	TArray<FBattleRandomDraw> Trace;
};
