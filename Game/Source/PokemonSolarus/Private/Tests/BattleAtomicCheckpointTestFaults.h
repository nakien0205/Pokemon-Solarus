#pragma once

#if WITH_DEV_AUTOMATION_TESTS

#include "BattleAtomicCheckpointTestCommon.h"

namespace BattleAtomicCheckpointTestFaultsPrivate
{
	using namespace BattleAtomicCheckpointTestCommonPrivate;

enum class EFaultRandomMode : uint8
	{
		PassThrough,
		CreateTransaction,
		Draw,
		StaleAfterDraw,
		Commit
	};

struct FFaultRandomCounters
	{
		int32 TransactionCreateAttempts = 0;
		int32 DrawAttempts = 0;
		int32 SuccessfulDraws = 0;
		int32 CommitAttempts = 0;
	};

class FFaultBattleRandomTransaction final : public IBattleRandomTransaction
	{
	public:
		FFaultBattleRandomTransaction(
			TUniquePtr<IBattleRandomTransaction>&& InInner,
			const EFaultRandomMode InMode,
			const int32 InSuccessfulDrawsBeforeFailure,
			TFunction<void()>* InAfterDraw,
			FFaultRandomCounters* InCounters)
			: Inner(MoveTemp(InInner))
			, Mode(InMode)
			, SuccessfulDrawsBeforeFailure(InSuccessfulDrawsBeforeFailure)
			, AfterDraw(InAfterDraw)
			, Counters(InCounters)
		{
		}

		virtual bool TryDrawUniform(
			const uint32 InclusiveMinimum,
			const uint32 InclusiveMaximum,
			const FBattleRandomContext& Context,
			FBattleRandomDraw& OutDraw) override
		{
			OutDraw = FBattleRandomDraw();
			if (Counters != nullptr)
			{
				++Counters->DrawAttempts;
			}
			if (bFinalized
				|| (Mode == EFaultRandomMode::Draw
					&& SuccessfulDrawCount >= SuccessfulDrawsBeforeFailure))
			{
				return false;
			}
			if (!Inner->TryDrawUniform(InclusiveMinimum, InclusiveMaximum, Context, OutDraw))
			{
				return false;
			}
			++SuccessfulDrawCount;
			if (Counters != nullptr)
			{
				++Counters->SuccessfulDraws;
			}
			if (Mode == EFaultRandomMode::StaleAfterDraw
				&& !bAfterDrawCalled
				&& AfterDraw != nullptr
				&& static_cast<bool>(*AfterDraw))
			{
				bAfterDrawCalled = true;
				(*AfterDraw)();
			}
			return true;
		}

		virtual TConstArrayView<FBattleRandomDraw> GetTrace() const override
		{
			return Inner->GetTrace();
		}

		virtual bool TryCommit(
			IBattleRandom& Parent,
			const FResolutionId ResolutionId,
			const FActionId OwningActionId,
			EBattleRandomTransactionCommitError& OutError) override
		{
			OutError = EBattleRandomTransactionCommitError::None;
			if (Counters != nullptr)
			{
				++Counters->CommitAttempts;
			}
			if (bFinalized)
			{
				OutError = EBattleRandomTransactionCommitError::AlreadyFinalized;
				return false;
			}
			bFinalized = true;
			if (Mode == EFaultRandomMode::Commit)
			{
				Inner->Rollback();
				OutError = EBattleRandomTransactionCommitError::ParentPositionMismatch;
				return false;
			}
			return Inner->TryCommit(Parent, ResolutionId, OwningActionId, OutError);
		}

		virtual void Rollback() override
		{
			bFinalized = true;
			Inner->Rollback();
		}

	private:
		TUniquePtr<IBattleRandomTransaction> Inner;
		EFaultRandomMode Mode;
		int32 SuccessfulDrawsBeforeFailure = 0;
		TFunction<void()>* AfterDraw = nullptr;
		FFaultRandomCounters* Counters = nullptr;
		int32 SuccessfulDrawCount = 0;
		bool bAfterDrawCalled = false;
		bool bFinalized = false;
	};

class FFaultBattleRandom final : public FScriptedBattleRandomBase
	{
	public:
		FFaultBattleRandom(
			TArray<uint32> Results,
			const EFaultRandomMode InMode,
			const int32 InSuccessfulDrawsBeforeFailure = 0)
			: FScriptedBattleRandomBase(MoveTemp(Results))
			, Mode(InMode)
			, SuccessfulDrawsBeforeFailure(InSuccessfulDrawsBeforeFailure)
		{
		}

		FFaultBattleRandom(
			TArray<FBattleExpectedRandomDraw> ExpectedDraws,
			const EFaultRandomMode InMode,
			const int32 InSuccessfulDrawsBeforeFailure = 0)
			: FScriptedBattleRandomBase(MoveTemp(ExpectedDraws))
			, Mode(InMode)
			, SuccessfulDrawsBeforeFailure(InSuccessfulDrawsBeforeFailure)
		{
		}

		void SetAfterDraw(TFunction<void()>&& InAfterDraw)
		{
			AfterDraw = MoveTemp(InAfterDraw);
		}

		const FFaultRandomCounters& GetCounters() const
		{
			return Counters;
		}

		virtual bool TryCreateTransaction(
			const FResolutionId ResolutionId,
			const FActionId OwningActionId,
			TUniquePtr<IBattleRandomTransaction>& OutTransaction) override
		{
			OutTransaction.Reset();
			++Counters.TransactionCreateAttempts;
			if (Mode == EFaultRandomMode::CreateTransaction)
			{
				return false;
			}

			TUniquePtr<IBattleRandomTransaction> Inner;
			if (!FScriptedBattleRandomBase::TryCreateTransaction(
					ResolutionId,
					OwningActionId,
					Inner))
			{
				return false;
			}
			OutTransaction = MakeUnique<FFaultBattleRandomTransaction>(
				MoveTemp(Inner),
				Mode,
				SuccessfulDrawsBeforeFailure,
				&AfterDraw,
				&Counters);
			return true;
		}

	private:
		EFaultRandomMode Mode;
		int32 SuccessfulDrawsBeforeFailure = 0;
		TFunction<void()> AfterDraw;
		FFaultRandomCounters Counters;
	};

class FActionStartStaleRandom final : public FScriptedBattleRandomBase
	{
	public:
		explicit FActionStartStaleRandom(TArray<uint32> Results)
			: FScriptedBattleRandomBase(MoveTemp(Results))
		{
		}

		void ArmAfterTraceRead(
			const int32 TraceReadOrdinal,
			TFunction<void()>&& InCallback)
		{
			ReadsSinceArm = 0;
			InjectionReadOrdinal = TraceReadOrdinal;
			Callback = MoveTemp(InCallback);
			bInjected = false;
		}

		void Disarm()
		{
			InjectionReadOrdinal = INDEX_NONE;
			Callback = TFunction<void()>();
		}

		void DisableFurtherTraceInjection()
		{
			InjectionReadOrdinal = INDEX_NONE;
		}

		int32 GetReadsSinceArm() const { return ReadsSinceArm; }
		bool WasInjected() const { return bInjected; }

		virtual TConstArrayView<FBattleRandomDraw> GetTrace() const override
		{
			const TConstArrayView<FBattleRandomDraw> Trace =
				FScriptedBattleRandomBase::GetTrace();
			if (InjectionReadOrdinal != INDEX_NONE)
			{
				++ReadsSinceArm;
				if (!bInjected
					&& ReadsSinceArm == InjectionReadOrdinal
					&& static_cast<bool>(Callback))
				{
					bInjected = true;
					Callback();
				}
			}
			return Trace;
		}

	private:
		mutable int32 ReadsSinceArm = 0;
		mutable int32 InjectionReadOrdinal = INDEX_NONE;
		mutable TFunction<void()> Callback;
		mutable bool bInjected = false;
	};

bool TryMakeFaultEngine(
		const FAtomicWildScenario& Scenario,
		TArray<uint32> Results,
		const EFaultRandomMode Mode,
		TUniquePtr<FBattleEngine>& OutEngine,
		FFaultBattleRandom*& OutRandom,
		const int32 SuccessfulDrawsBeforeFailure = 0);

bool TryMakeStrictFaultEngine(
		const FAtomicWildScenario& Scenario,
		TArray<FBattleExpectedRandomDraw> ExpectedDraws,
		const EFaultRandomMode Mode,
		TUniquePtr<FBattleEngine>& OutEngine,
		FFaultBattleRandom*& OutRandom,
		const int32 SuccessfulDrawsBeforeFailure = 0);

bool TryMakeActionStartStaleEngine(
		const FAtomicWildScenario& Scenario,
		TUniquePtr<FBattleEngine>& OutEngine,
		FActionStartStaleRandom*& OutRandom,
		TArray<uint32> Results = {});
}

#endif
