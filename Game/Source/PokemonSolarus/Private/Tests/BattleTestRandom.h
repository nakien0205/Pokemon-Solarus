#pragma once

#include "Battle/BattleRandom.h"

namespace BattleTest
{
	/** One exact scripted draw expected by the strict deterministic test source. */
	struct FBattleExpectedRandomDraw
	{
		uint32 Minimum = 0;
		uint32 Maximum = 0;
		uint32 Result = 0;
		FDefinitionId RulePurpose;
	};

	namespace Private
	{
		enum class EScriptedBattleRandomMode : uint8
		{
			Sequence,
			Strict
		};

		struct FScriptedBattleRandomState
		{
			EScriptedBattleRandomMode Mode = EScriptedBattleRandomMode::Sequence;
			TArray<uint32> Results;
			TArray<FBattleExpectedRandomDraw> ExpectedDraws;
			int32 NextIndex = 0;
			uint64 NextCallOrdinal = 1;
			uint64 PositionVersion = 1;
			bool bMismatch = false;
			FString Mismatch;
			TArray<FBattleRandomDraw> Trace;
		};

		inline void RecordMismatch(
			FScriptedBattleRandomState& State,
			const FString& Message)
		{
			State.bMismatch = true;
			State.Mismatch = Message;
			++State.PositionVersion;
		}

		inline bool TryDrawFromState(
			FScriptedBattleRandomState& State,
			const uint32 InclusiveMinimum,
			const uint32 InclusiveMaximum,
			const FBattleRandomContext& Context,
			FBattleRandomDraw& OutDraw,
			const FResolutionId* TransactionResolutionId = nullptr,
			const FActionId* TransactionActionId = nullptr,
			EBattleRandomTransactionCommitError* OutTransactionError = nullptr)
		{
			OutDraw = FBattleRandomDraw();
			if (OutTransactionError != nullptr)
			{
				*OutTransactionError = EBattleRandomTransactionCommitError::None;
				if (TransactionResolutionId == nullptr
					|| Context.ResolutionId != *TransactionResolutionId)
				{
					*OutTransactionError =
						EBattleRandomTransactionCommitError::ResolutionIdentityMismatch;
					return false;
				}
				if (TransactionActionId == nullptr
					|| Context.ActionId != *TransactionActionId)
				{
					*OutTransactionError =
						EBattleRandomTransactionCommitError::ActionIdentityMismatch;
					return false;
				}
			}

			if (InclusiveMinimum > InclusiveMaximum
				|| !Context.IsValid()
				|| State.NextCallOrdinal == 0)
			{
				if (OutTransactionError != nullptr)
				{
					*OutTransactionError = EBattleRandomTransactionCommitError::StagedDrawRejected;
				}
				else if (State.Mode == EScriptedBattleRandomMode::Strict)
				{
					RecordMismatch(State, TEXT("An invalid RNG draw was requested"));
				}
				return false;
			}

			uint32 Result = 0;
			if (State.Mode == EScriptedBattleRandomMode::Sequence)
			{
				if (!State.Results.IsValidIndex(State.NextIndex))
				{
					if (OutTransactionError != nullptr)
					{
						*OutTransactionError = EBattleRandomTransactionCommitError::StagedDrawRejected;
					}
					return false;
				}
				Result = State.Results[State.NextIndex];
				if (Result < InclusiveMinimum || Result > InclusiveMaximum)
				{
					if (OutTransactionError != nullptr)
					{
						*OutTransactionError = EBattleRandomTransactionCommitError::StagedDrawRejected;
					}
					return false;
				}
			}
			else
			{
				if (State.bMismatch || !State.ExpectedDraws.IsValidIndex(State.NextIndex))
				{
					if (OutTransactionError != nullptr)
					{
						*OutTransactionError = EBattleRandomTransactionCommitError::StagedDrawRejected;
					}
					else
					{
						RecordMismatch(State, TEXT("An unexpected extra RNG draw was requested"));
					}
					return false;
				}

				const FBattleExpectedRandomDraw& Expected = State.ExpectedDraws[State.NextIndex];
				if (Expected.Minimum != InclusiveMinimum
					|| Expected.Maximum != InclusiveMaximum
					|| Expected.RulePurpose != Context.RulePurpose
					|| Expected.Result < InclusiveMinimum
					|| Expected.Result > InclusiveMaximum)
				{
					if (OutTransactionError != nullptr)
					{
						*OutTransactionError = EBattleRandomTransactionCommitError::StagedDrawRejected;
					}
					else
					{
						RecordMismatch(
							State,
							FString::Printf(
								TEXT("RNG draw %d differed: got U[%u,%u] purpose %s, expected U[%u,%u] purpose %s result %u"),
								State.NextIndex,
								InclusiveMinimum,
								InclusiveMaximum,
								*Context.RulePurpose.GetName().ToString(),
								Expected.Minimum,
								Expected.Maximum,
								*Expected.RulePurpose.GetName().ToString(),
								Expected.Result));
					}
					return false;
				}
				Result = Expected.Result;
			}

			++State.NextIndex;
			OutDraw.InclusiveMinimum = InclusiveMinimum;
			OutDraw.InclusiveMaximum = InclusiveMaximum;
			OutDraw.Bound = static_cast<uint64>(InclusiveMaximum)
				- static_cast<uint64>(InclusiveMinimum)
				+ 1ULL;
			OutDraw.RawValue = Result;
			OutDraw.Result = Result;
			OutDraw.CallOrdinal = State.NextCallOrdinal;
			OutDraw.BattleId = Context.BattleId;
			OutDraw.TurnId = Context.TurnId;
			OutDraw.ActionId = Context.ActionId;
			OutDraw.ResolutionId = Context.ResolutionId;
			OutDraw.RulePurpose = Context.RulePurpose;
			State.Trace.Add(OutDraw);
			++State.NextCallOrdinal;
			++State.PositionVersion;
			return true;
		}
	}

	class FScriptedBattleRandomTransaction;

	/** Shared transaction-capable base for deterministic Battle test sources. */
	class FScriptedBattleRandomBase : public IBattleRandom
	{
	public:
		virtual bool TryDrawUniform(
			const uint32 InclusiveMinimum,
			const uint32 InclusiveMaximum,
			const FBattleRandomContext& Context,
			FBattleRandomDraw& OutDraw) override
		{
			return Private::TryDrawFromState(
				State,
				InclusiveMinimum,
				InclusiveMaximum,
				Context,
				OutDraw);
		}

		virtual TConstArrayView<FBattleRandomDraw> GetTrace() const override
		{
			return State.Trace;
		}

		virtual bool TryCreateTransaction(
			FResolutionId ResolutionId,
			FActionId OwningActionId,
			TUniquePtr<IBattleRandomTransaction>& OutTransaction) override;

		[[nodiscard]] bool IsExact() const
		{
			const int32 ExpectedCount =
				State.Mode == Private::EScriptedBattleRandomMode::Sequence
					? State.Results.Num()
					: State.ExpectedDraws.Num();
			return !State.bMismatch && State.NextIndex == ExpectedCount;
		}

		[[nodiscard]] const FString& GetMismatch() const
		{
			return State.Mismatch;
		}

	protected:
		explicit FScriptedBattleRandomBase(TArray<uint32> InResults)
		{
			State.Mode = Private::EScriptedBattleRandomMode::Sequence;
			State.Results = MoveTemp(InResults);
		}

		explicit FScriptedBattleRandomBase(TArray<FBattleExpectedRandomDraw> InExpectedDraws)
		{
			State.Mode = Private::EScriptedBattleRandomMode::Strict;
			State.ExpectedDraws = MoveTemp(InExpectedDraws);
		}

	private:
		friend class FScriptedBattleRandomTransaction;

		Private::FScriptedBattleRandomState State;
	};

	class FScriptedBattleRandomTransaction final : public IBattleRandomTransaction
	{
	public:
		FScriptedBattleRandomTransaction(
			FScriptedBattleRandomBase& InParent,
			const FResolutionId InResolutionId,
			const FActionId InOwningActionId)
			: ParentAtStart(&InParent)
			, ResolutionId(InResolutionId)
			, OwningActionId(InOwningActionId)
			, ParentNextIndexAtStart(InParent.State.NextIndex)
			, ParentNextCallOrdinalAtStart(InParent.State.NextCallOrdinal)
			, ParentTraceNumAtStart(InParent.State.Trace.Num())
			, ParentPositionVersionAtStart(InParent.State.PositionVersion)
			, ParentMismatchAtStart(InParent.State.bMismatch)
			, WorkingState(InParent.State)
		{
			WorkingState.Trace.Reset();
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

			EBattleRandomTransactionCommitError DrawError =
				EBattleRandomTransactionCommitError::None;
			if (!Private::TryDrawFromState(
				WorkingState,
				InclusiveMinimum,
				InclusiveMaximum,
				Context,
				OutDraw,
				&ResolutionId,
				&OwningActionId,
				&DrawError))
			{
				StagedError = DrawError;
				return false;
			}
			return true;
		}

		virtual TConstArrayView<FBattleRandomDraw> GetTrace() const override
		{
			return WorkingState.Trace;
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
			if (ParentAtStart->State.NextIndex != ParentNextIndexAtStart
				|| ParentAtStart->State.NextCallOrdinal != ParentNextCallOrdinalAtStart
				|| ParentAtStart->State.Trace.Num() != ParentTraceNumAtStart
				|| ParentAtStart->State.PositionVersion != ParentPositionVersionAtStart
				|| ParentAtStart->State.bMismatch != ParentMismatchAtStart
				|| ParentAtStart->State.PositionVersion == MAX_uint64)
			{
				OutError = EBattleRandomTransactionCommitError::ParentPositionMismatch;
				return false;
			}

			ParentAtStart->State.NextIndex = WorkingState.NextIndex;
			ParentAtStart->State.NextCallOrdinal = WorkingState.NextCallOrdinal;
			ParentAtStart->State.bMismatch = WorkingState.bMismatch;
			ParentAtStart->State.Mismatch = WorkingState.Mismatch;
			ParentAtStart->State.Trace.Append(WorkingState.Trace);
			++ParentAtStart->State.PositionVersion;
			return true;
		}

		virtual void Rollback() override
		{
			bFinalized = true;
		}

	private:
		FScriptedBattleRandomBase* ParentAtStart = nullptr;
		FResolutionId ResolutionId;
		FActionId OwningActionId;
		int32 ParentNextIndexAtStart = 0;
		uint64 ParentNextCallOrdinalAtStart = 0;
		int32 ParentTraceNumAtStart = 0;
		uint64 ParentPositionVersionAtStart = 0;
		bool ParentMismatchAtStart = false;
		Private::FScriptedBattleRandomState WorkingState;
		EBattleRandomTransactionCommitError StagedError =
			EBattleRandomTransactionCommitError::None;
		bool bFinalized = false;
	};

	inline bool FScriptedBattleRandomBase::TryCreateTransaction(
		const FResolutionId ResolutionId,
		const FActionId OwningActionId,
		TUniquePtr<IBattleRandomTransaction>& OutTransaction)
	{
		OutTransaction.Reset();
		if (!ResolutionId.IsValid() || !OwningActionId.IsValid())
		{
			return false;
		}

		OutTransaction = MakeUnique<FScriptedBattleRandomTransaction>(
			*this,
			ResolutionId,
			OwningActionId);
		return true;
	}

	/** Deterministic source that consumes one caller-supplied result per draw. */
	class FSequenceBattleRandom final : public FScriptedBattleRandomBase
	{
	public:
		explicit FSequenceBattleRandom(TArray<uint32> InResults)
			: FScriptedBattleRandomBase(MoveTemp(InResults))
		{
		}
	};

	/** Deterministic source that also verifies every range and rule purpose. */
	class FStrictBattleRandom final : public FScriptedBattleRandomBase
	{
	public:
		explicit FStrictBattleRandom(TArray<FBattleExpectedRandomDraw> InExpectedDraws)
			: FScriptedBattleRandomBase(MoveTemp(InExpectedDraws))
		{
		}
	};

	inline TArray<FBattleExpectedRandomDraw> MakeRepeatedExpectedRandomDraws(
		const int32 Count,
		const uint32 Minimum,
		const uint32 Maximum,
		const uint32 Result,
		const FDefinitionId RulePurpose)
	{
		TArray<FBattleExpectedRandomDraw> Draws;
		Draws.Reserve(FMath::Max(0, Count));
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Draws.Add({Minimum, Maximum, Result, RulePurpose});
		}
		return Draws;
	}
}
