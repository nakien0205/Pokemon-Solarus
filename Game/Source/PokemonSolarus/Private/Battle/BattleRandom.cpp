#include "Battle/BattleRandom.h"

namespace
{
	uint64 AdvanceSplitMix64(uint64& InOutState)
	{
		InOutState += 0x9E3779B97F4A7C15ULL;
		uint64 Mixed = InOutState;
		Mixed = (Mixed ^ (Mixed >> 30U)) * 0xBF58476D1CE4E5B9ULL;
		Mixed = (Mixed ^ (Mixed >> 27U)) * 0x94D049BB133111EBULL;
		return Mixed ^ (Mixed >> 31U);
	}
}

bool IBattleRandomTransaction::TryCreateTransaction(
	const FResolutionId ResolutionId,
	const FActionId OwningActionId,
	TUniquePtr<IBattleRandomTransaction>& OutTransaction)
{
	(void)ResolutionId;
	(void)OwningActionId;
	OutTransaction.Reset();
	return false;
}

class FSeededBattleRandom::FTransaction final : public IBattleRandomTransaction
{
public:
	FTransaction(
		FSeededBattleRandom& InParent,
		const FResolutionId InResolutionId,
		const FActionId InOwningActionId)
		: ParentAtStart(&InParent)
		, ResolutionId(InResolutionId)
		, OwningActionId(InOwningActionId)
		, ParentStateAtStart(InParent.State)
		, ParentNextCallOrdinalAtStart(InParent.NextCallOrdinal)
		, ParentTraceNumAtStart(InParent.Trace.Num())
		, ParentPositionVersionAtStart(InParent.PositionVersion)
		, WorkingState(InParent.State)
		, WorkingNextCallOrdinal(InParent.NextCallOrdinal)
	{
	}

	virtual bool TryDrawUniform(
		const uint32 InclusiveMinimum,
		const uint32 InclusiveMaximum,
		const FBattleRandomContext& Context,
		FBattleRandomDraw& OutDraw) override
	{
		OutDraw = FBattleRandomDraw();
		if (bFinalized || StagedError != EBattleRandomTransactionCommitError::None)
		{
			return false;
		}
		if (Context.ResolutionId != ResolutionId)
		{
			StagedError = EBattleRandomTransactionCommitError::ResolutionIdentityMismatch;
			return false;
		}
		if (Context.ActionId != OwningActionId)
		{
			StagedError = EBattleRandomTransactionCommitError::ActionIdentityMismatch;
			return false;
		}
		if (InclusiveMinimum > InclusiveMaximum
			|| !Context.IsValid()
			|| WorkingNextCallOrdinal == 0)
		{
			StagedError = EBattleRandomTransactionCommitError::StagedDrawRejected;
			return false;
		}

		const uint64 Bound = static_cast<uint64>(InclusiveMaximum)
			- static_cast<uint64>(InclusiveMinimum)
			+ 1ULL;
		const uint64 RejectionThreshold = (0ULL - Bound) % Bound;

		uint64 RawValue = 0;
		do
		{
			RawValue = AdvanceSplitMix64(WorkingState);
		}
		while (RawValue < RejectionThreshold);

		OutDraw.InclusiveMinimum = InclusiveMinimum;
		OutDraw.InclusiveMaximum = InclusiveMaximum;
		OutDraw.Bound = Bound;
		OutDraw.RawValue = RawValue;
		OutDraw.Result = InclusiveMinimum + static_cast<uint32>(RawValue % Bound);
		OutDraw.CallOrdinal = WorkingNextCallOrdinal;
		OutDraw.BattleId = Context.BattleId;
		OutDraw.TurnId = Context.TurnId;
		OutDraw.ActionId = Context.ActionId;
		OutDraw.ResolutionId = Context.ResolutionId;
		OutDraw.RulePurpose = Context.RulePurpose;
		StagedTrace.Add(OutDraw);
		++WorkingNextCallOrdinal;
		return true;
	}

	virtual TConstArrayView<FBattleRandomDraw> GetTrace() const override
	{
		return StagedTrace;
	}

	virtual bool TryCommit(
		IBattleRandom& Parent,
		const FResolutionId InResolutionId,
		const FActionId InOwningActionId,
		EBattleRandomTransactionCommitError& OutError) override
	{
		OutError = EBattleRandomTransactionCommitError::None;
		if (bFinalized)
		{
			OutError = EBattleRandomTransactionCommitError::AlreadyFinalized;
			return false;
		}
		bFinalized = true;

		if (&Parent != static_cast<IBattleRandom*>(ParentAtStart))
		{
			OutError = EBattleRandomTransactionCommitError::ParentIdentityMismatch;
			return false;
		}
		if (InResolutionId != ResolutionId)
		{
			OutError = EBattleRandomTransactionCommitError::ResolutionIdentityMismatch;
			return false;
		}
		if (InOwningActionId != OwningActionId)
		{
			OutError = EBattleRandomTransactionCommitError::ActionIdentityMismatch;
			return false;
		}
		if (StagedError != EBattleRandomTransactionCommitError::None)
		{
			OutError = StagedError;
			return false;
		}
		if (ParentAtStart->State != ParentStateAtStart
			|| ParentAtStart->NextCallOrdinal != ParentNextCallOrdinalAtStart
			|| ParentAtStart->Trace.Num() != ParentTraceNumAtStart
			|| ParentAtStart->PositionVersion != ParentPositionVersionAtStart
			|| ParentAtStart->PositionVersion == MAX_uint64)
		{
			OutError = EBattleRandomTransactionCommitError::ParentPositionMismatch;
			return false;
		}

		ParentAtStart->State = WorkingState;
		ParentAtStart->NextCallOrdinal = WorkingNextCallOrdinal;
		ParentAtStart->Trace.Append(StagedTrace);
		++ParentAtStart->PositionVersion;
		return true;
	}

	virtual void Rollback() override
	{
		bFinalized = true;
	}

private:
	FSeededBattleRandom* ParentAtStart = nullptr;
	FResolutionId ResolutionId;
	FActionId OwningActionId;
	uint64 ParentStateAtStart = 0;
	uint64 ParentNextCallOrdinalAtStart = 0;
	int32 ParentTraceNumAtStart = 0;
	uint64 ParentPositionVersionAtStart = 0;
	uint64 WorkingState = 0;
	uint64 WorkingNextCallOrdinal = 0;
	TArray<FBattleRandomDraw> StagedTrace;
	EBattleRandomTransactionCommitError StagedError = EBattleRandomTransactionCommitError::None;
	bool bFinalized = false;
};

FSeededBattleRandom::FSeededBattleRandom(const uint64 InSeed)
	: InitialSeed(InSeed)
	, State(InSeed)
{
}

bool FSeededBattleRandom::TryDrawUniform(
	const uint32 InclusiveMinimum,
	const uint32 InclusiveMaximum,
	const FBattleRandomContext& Context,
	FBattleRandomDraw& OutDraw)
{
	OutDraw = FBattleRandomDraw();
	if (InclusiveMinimum > InclusiveMaximum || !Context.IsValid() || NextCallOrdinal == 0)
	{
		return false;
	}

	const uint64 Bound = static_cast<uint64>(InclusiveMaximum)
		- static_cast<uint64>(InclusiveMinimum)
		+ 1ULL;
	const uint64 RejectionThreshold = (0ULL - Bound) % Bound;

	uint64 RawValue = 0;
	do
	{
		RawValue = NextRawValue();
	}
	while (RawValue < RejectionThreshold);

	FBattleRandomDraw Draw;
	Draw.InclusiveMinimum = InclusiveMinimum;
	Draw.InclusiveMaximum = InclusiveMaximum;
	Draw.Bound = Bound;
	Draw.RawValue = RawValue;
	Draw.Result = InclusiveMinimum + static_cast<uint32>(RawValue % Bound);
	Draw.CallOrdinal = NextCallOrdinal;
	Draw.BattleId = Context.BattleId;
	Draw.TurnId = Context.TurnId;
	Draw.ActionId = Context.ActionId;
	Draw.ResolutionId = Context.ResolutionId;
	Draw.RulePurpose = Context.RulePurpose;

	Trace.Add(Draw);
	OutDraw = Draw;
	++NextCallOrdinal;
	++PositionVersion;
	return true;
}

TConstArrayView<FBattleRandomDraw> FSeededBattleRandom::GetTrace() const
{
	return Trace;
}

bool FSeededBattleRandom::TryCreateTransaction(
	const FResolutionId ResolutionId,
	const FActionId OwningActionId,
	TUniquePtr<IBattleRandomTransaction>& OutTransaction)
{
	OutTransaction.Reset();
	if (!ResolutionId.IsValid() || !OwningActionId.IsValid())
	{
		return false;
	}

	OutTransaction = MakeUnique<FTransaction>(*this, ResolutionId, OwningActionId);
	return true;
}

uint64 FSeededBattleRandom::NextRawValue()
{
	return AdvanceSplitMix64(State);
}
