#if WITH_DEV_AUTOMATION_TESTS

#include "Battle/BattleRandom.h"
#include "BattleTestRandom.h"
#include "Misc/AutomationTest.h"

namespace BattleRandomTests
{
	FBattleRandomContext MakeRandomContext()
	{
		FBattleRandomContext Context;
		const bool bBattleIdCreated = FBattleId::TryCreate(10, Context.BattleId);
		const bool bTurnIdCreated = FTurnId::TryCreate(2, Context.TurnId);
		const bool bActionIdCreated = FActionId::TryCreate(7, Context.ActionId);
		const bool bResolutionIdCreated = FResolutionId::TryCreate(9, Context.ResolutionId);
		const bool bPurposeCreated = FDefinitionId::TryCreate(FName(TEXT("RNG.Test")), Context.RulePurpose);
		check(
			bBattleIdCreated
			&& bTurnIdCreated
			&& bActionIdCreated
			&& bResolutionIdCreated
			&& bPurposeCreated);
		return Context;
	}

	FResolutionId MakeResolutionId(const uint64 Value)
	{
		FResolutionId Id;
		check(FResolutionId::TryCreate(Value, Id));
		return Id;
	}

	FActionId MakeActionId(const uint64 Value)
	{
		FActionId Id;
		check(FActionId::TryCreate(Value, Id));
		return Id;
	}

	FBattleRandomContext MakeRandomContext(
		const FResolutionId ResolutionId,
		const FActionId ActionId)
	{
		FBattleRandomContext Context = MakeRandomContext();
		Context.ResolutionId = ResolutionId;
		Context.ActionId = ActionId;
		return Context;
	}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleRandomBoundsAndTraceTest,
	"PokemonSolarus.Battle.CoreContracts.Random.BoundsAndTrace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBattleRandomBoundsAndTraceTest::RunTest(const FString& Parameters)
{
	FSeededBattleRandom Random(1);
	FBattleRandomDraw Draw;
	Draw.CallOrdinal = 999;

	const FBattleRandomContext ValidContext = MakeRandomContext();
	TestFalse(
		TEXT("An inverted inclusive range is rejected"),
		Random.TryDrawUniform(5, 4, ValidContext, Draw));
	TestEqual(TEXT("A rejected range resets the output"), Draw.CallOrdinal, 0ULL);
	TestEqual(TEXT("A rejected range consumes no trace entry"), Random.GetTrace().Num(), 0);

	const FBattleRandomContext InvalidContext;
	TestFalse(
		TEXT("An invalid RNG context is rejected"),
		Random.TryDrawUniform(0, 99, InvalidContext, Draw));
	TestEqual(TEXT("An invalid context consumes no trace entry"), Random.GetTrace().Num(), 0);

	TestTrue(
		TEXT("A one-value range still consumes one draw"),
		Random.TryDrawUniform(0, 0, ValidContext, Draw));
	TestEqual(TEXT("The first call ordinal is one"), Draw.CallOrdinal, 1ULL);
	TestEqual(TEXT("The one-value range has bound one"), Draw.Bound, 1ULL);
	TestEqual(TEXT("The one-value result is zero"), Draw.Result, static_cast<uint32>(0));
	TestEqual(TEXT("The first raw SplitMix64 value is frozen"), Draw.RawValue, 0x910A2DEC89025CC1ULL);
	TestTrue(TEXT("The draw retains its battle ID"), Draw.BattleId == ValidContext.BattleId);
	TestTrue(TEXT("The draw retains its turn ID"), Draw.TurnId == ValidContext.TurnId);
	TestTrue(TEXT("The draw retains its action ID"), Draw.ActionId == ValidContext.ActionId);
	TestTrue(TEXT("The draw retains its resolution ID"), Draw.ResolutionId == ValidContext.ResolutionId);
	TestTrue(TEXT("The draw retains its rule purpose"), Draw.RulePurpose == ValidContext.RulePurpose);

	TestTrue(
		TEXT("A non-zero inclusive range is accepted"),
		Random.TryDrawUniform(2, 4, ValidContext, Draw));
	TestEqual(TEXT("The second call ordinal is two"), Draw.CallOrdinal, 2ULL);
	TestEqual(TEXT("The inclusive 2..4 range has bound three"), Draw.Bound, 3ULL);
	TestEqual(TEXT("The second raw value maps deterministically into 2..4"), Draw.Result, static_cast<uint32>(3));
	TestEqual(TEXT("The second raw SplitMix64 value is frozen"), Draw.RawValue, 0xBEEB8DA1658EEC67ULL);
	TestEqual(TEXT("Two valid calls produce two trace entries"), Random.GetTrace().Num(), 2);
	TestTrue(TEXT("The returned second draw matches the stored trace"), Random.GetTrace()[1] == Draw);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleRandomReplayEquivalenceTest,
	"PokemonSolarus.Battle.CoreContracts.Random.ReplayEquivalence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBattleRandomReplayEquivalenceTest::RunTest(const FString& Parameters)
{
	FSeededBattleRandom First(987654321ULL);
	FSeededBattleRandom Second(987654321ULL);
	const FBattleRandomContext Context = MakeRandomContext();
	const TArray<TPair<uint32, uint32>> Ranges = {
		{0, 0},
		{0, 99},
		{2, 4},
		{0, 65535}
	};

	for (const TPair<uint32, uint32>& Range : Ranges)
	{
		FBattleRandomDraw FirstDraw;
		FBattleRandomDraw SecondDraw;
		TestTrue(
			TEXT("The first replay stream accepts the range"),
			First.TryDrawUniform(Range.Key, Range.Value, Context, FirstDraw));
		TestTrue(
			TEXT("The second replay stream accepts the range"),
			Second.TryDrawUniform(Range.Key, Range.Value, Context, SecondDraw));
		TestTrue(TEXT("Equal seed and requests produce equal draw records"), FirstDraw == SecondDraw);
		TestTrue(
			TEXT("Every mapped result remains inside its inclusive range"),
			FirstDraw.Result >= Range.Key && FirstDraw.Result <= Range.Value);
	}

	TestEqual(TEXT("Replay traces contain the same number of calls"), First.GetTrace().Num(), Second.GetTrace().Num());
	for (int32 Index = 0; Index < First.GetTrace().Num(); ++Index)
	{
		TestTrue(
			FString::Printf(TEXT("Replay trace entry %d is identical"), Index),
			First.GetTrace()[Index] == Second.GetTrace()[Index]);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleTransactionalRandomSeededRollbackTest,
	"PokemonSolarus.Battle.ADR0002.3C.TransactionalRandom.Seeded.RollbackAndDestruction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBattleTransactionalRandomSeededRollbackTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FBattleRandomContext Context = MakeRandomContext();

	FSeededBattleRandom RolledBack(101ULL);
	FSeededBattleRandom RolledBackReference(101ULL);
	TUniquePtr<IBattleRandomTransaction> Transaction;
	if (!TestTrue(
		TEXT("A valid seeded transaction is created"),
		RolledBack.TryCreateTransaction(
			Context.ResolutionId,
			Context.ActionId,
			Transaction)))
	{
		return false;
	}

	FBattleRandomDraw StagedDraw;
	TestTrue(
		TEXT("A valid draw can be staged"),
		Transaction->TryDrawUniform(0, 99, Context, StagedDraw));
	TestEqual(TEXT("A staged draw remains absent from the parent trace"), RolledBack.GetTrace().Num(), 0);
	TestEqual(TEXT("The transaction exposes its private staged trace"), Transaction->GetTrace().Num(), 1);
	Transaction->Rollback();
	Transaction->Rollback();
	TestEqual(TEXT("Repeated rollback leaves the parent trace unchanged"), RolledBack.GetTrace().Num(), 0);

	FBattleRandomDraw AfterRollback;
	FBattleRandomDraw ReferenceAfterRollback;
	TestTrue(
		TEXT("The rolled-back parent still accepts its first direct draw"),
		RolledBack.TryDrawUniform(0, 99, Context, AfterRollback));
	TestTrue(
		TEXT("The rollback reference accepts its first direct draw"),
		RolledBackReference.TryDrawUniform(0, 99, Context, ReferenceAfterRollback));
	TestTrue(
		TEXT("Rollback preserves the exact parent stream position"),
		AfterRollback == ReferenceAfterRollback);

	FSeededBattleRandom Destroyed(202ULL);
	FSeededBattleRandom DestroyedReference(202ULL);
	{
		TUniquePtr<IBattleRandomTransaction> DestroyedTransaction;
		if (!TestTrue(
			TEXT("A transaction intended for destruction is created"),
			Destroyed.TryCreateTransaction(
				Context.ResolutionId,
				Context.ActionId,
				DestroyedTransaction)))
		{
			return false;
		}
		FBattleRandomDraw DestroyedStagedDraw;
		TestTrue(
			TEXT("The soon-to-be-destroyed transaction stages one draw"),
			DestroyedTransaction->TryDrawUniform(2, 4, Context, DestroyedStagedDraw));
	}
	TestEqual(TEXT("Destruction without commit leaves no parent trace"), Destroyed.GetTrace().Num(), 0);

	FBattleRandomDraw AfterDestruction;
	FBattleRandomDraw ReferenceAfterDestruction;
	TestTrue(
		TEXT("The destruction parent still accepts its first direct draw"),
		Destroyed.TryDrawUniform(2, 4, Context, AfterDestruction));
	TestTrue(
		TEXT("The destruction reference accepts its first direct draw"),
		DestroyedReference.TryDrawUniform(2, 4, Context, ReferenceAfterDestruction));
	TestTrue(
		TEXT("Destruction preserves the exact parent stream position"),
		AfterDestruction == ReferenceAfterDestruction);

	TUniquePtr<IBattleRandomTransaction> ResetTransaction;
	TestTrue(
		TEXT("A valid output transaction is populated before reset validation"),
		Destroyed.TryCreateTransaction(
			Context.ResolutionId,
			Context.ActionId,
			ResetTransaction));
	TestFalse(
		TEXT("An invalid resolution cannot create a transaction"),
		Destroyed.TryCreateTransaction(
			FResolutionId(),
			Context.ActionId,
			ResetTransaction));
	TestTrue(TEXT("Rejected creation resets the output transaction"), ResetTransaction == nullptr);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleTransactionalRandomSeededCommitTest,
	"PokemonSolarus.Battle.ADR0002.3C.TransactionalRandom.Seeded.CommitExactOnceAndEarlyStop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBattleTransactionalRandomSeededCommitTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FBattleRandomContext Context = MakeRandomContext();
	FSeededBattleRandom Parent(987654321ULL);
	FSeededBattleRandom Reference(987654321ULL);

	TUniquePtr<IBattleRandomTransaction> Transaction;
	if (!TestTrue(
		TEXT("The exact-once transaction is created"),
		Parent.TryCreateTransaction(
			Context.ResolutionId,
			Context.ActionId,
			Transaction)))
	{
		return false;
	}

	FBattleRandomDraw StagedFirst;
	FBattleRandomDraw StagedSecond;
	FBattleRandomDraw ReferenceFirst;
	FBattleRandomDraw ReferenceSecond;
	TestTrue(
		TEXT("The first early-stop draw is staged"),
		Transaction->TryDrawUniform(0, 99, Context, StagedFirst));
	TestTrue(
		TEXT("The second early-stop draw is staged"),
		Transaction->TryDrawUniform(0, 65535, Context, StagedSecond));
	TestTrue(
		TEXT("The reference accepts the first equivalent draw"),
		Reference.TryDrawUniform(0, 99, Context, ReferenceFirst));
	TestTrue(
		TEXT("The reference accepts the second equivalent draw"),
		Reference.TryDrawUniform(0, 65535, Context, ReferenceSecond));
	TestTrue(TEXT("The first staged draw is deterministic"), StagedFirst == ReferenceFirst);
	TestTrue(TEXT("The second staged draw is deterministic"), StagedSecond == ReferenceSecond);
	TestEqual(TEXT("The parent remains private before commit"), Parent.GetTrace().Num(), 0);

	EBattleRandomTransactionCommitError Error =
		EBattleRandomTransactionCommitError::AlreadyFinalized;
	TestTrue(
		TEXT("The staged prefix commits"),
		Transaction->TryCommit(
			Parent,
			Context.ResolutionId,
			Context.ActionId,
			Error));
	TestEqual(TEXT("A successful commit reports no error"), Error,
		EBattleRandomTransactionCommitError::None);
	TestEqual(TEXT("Only the two staged draws become visible"), Parent.GetTrace().Num(), 2);
	TestTrue(TEXT("The first committed trace entry is exact"), Parent.GetTrace()[0] == ReferenceFirst);
	TestTrue(TEXT("The second committed trace entry is exact"), Parent.GetTrace()[1] == ReferenceSecond);

	const int32 TraceCountAfterCommit = Parent.GetTrace().Num();
	TestFalse(
		TEXT("A committed transaction cannot commit twice"),
		Transaction->TryCommit(
			Parent,
			Context.ResolutionId,
			Context.ActionId,
			Error));
	TestEqual(TEXT("A second commit is typed as already finalized"), Error,
		EBattleRandomTransactionCommitError::AlreadyFinalized);
	TestEqual(TEXT("A second commit publishes nothing"), Parent.GetTrace().Num(), TraceCountAfterCommit);

	FBattleRandomDraw ParentNext;
	FBattleRandomDraw ReferenceNext;
	TestTrue(
		TEXT("The committed parent accepts the next draw"),
		Parent.TryDrawUniform(2, 4, Context, ParentNext));
	TestTrue(
		TEXT("The reference accepts the next draw"),
		Reference.TryDrawUniform(2, 4, Context, ReferenceNext));
	TestTrue(TEXT("Commit applies the exact working stream position"), ParentNext == ReferenceNext);

	TUniquePtr<IBattleRandomTransaction> StaleZeroDrawTransaction;
	TUniquePtr<IBattleRandomTransaction> ZeroDrawTransaction;
	if (!TestTrue(
		TEXT("A zero-draw stale observer is created"),
		Parent.TryCreateTransaction(
			Context.ResolutionId,
			Context.ActionId,
			StaleZeroDrawTransaction))
		|| !TestTrue(
			TEXT("A zero-draw transaction is created"),
			Parent.TryCreateTransaction(
				Context.ResolutionId,
				Context.ActionId,
				ZeroDrawTransaction)))
	{
		return false;
	}

	const int32 TraceCountBeforeZeroCommit = Parent.GetTrace().Num();
	TestTrue(
		TEXT("A zero-draw transaction commits"),
		ZeroDrawTransaction->TryCommit(
			Parent,
			Context.ResolutionId,
			Context.ActionId,
			Error));
	TestEqual(TEXT("A zero-draw commit reports no error"), Error,
		EBattleRandomTransactionCommitError::None);
	TestEqual(
		TEXT("A zero-draw commit changes no trace or call ordinal"),
		Parent.GetTrace().Num(),
		TraceCountBeforeZeroCommit);
	TestFalse(
		TEXT("The earlier zero-draw observer becomes stale"),
		StaleZeroDrawTransaction->TryCommit(
			Parent,
			Context.ResolutionId,
			Context.ActionId,
			Error));
	TestEqual(TEXT("A zero-draw commit advances only the transaction position"), Error,
		EBattleRandomTransactionCommitError::ParentPositionMismatch);

	FBattleRandomDraw ParentAfterZeroCommit;
	FBattleRandomDraw ReferenceAfterZeroCommit;
	TestTrue(
		TEXT("The parent draws after a zero-draw commit"),
		Parent.TryDrawUniform(0, 9, Context, ParentAfterZeroCommit));
	TestTrue(
		TEXT("The reference draws after no stream movement"),
		Reference.TryDrawUniform(0, 9, Context, ReferenceAfterZeroCommit));
	TestTrue(
		TEXT("A zero-draw commit leaves stream and call ordinal unchanged"),
		ParentAfterZeroCommit == ReferenceAfterZeroCommit);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleTransactionalRandomSeededFailuresTest,
	"PokemonSolarus.Battle.ADR0002.3C.TransactionalRandom.Seeded.TypedCommitFailures",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBattleTransactionalRandomSeededFailuresTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FBattleRandomContext Context = MakeRandomContext();
	const FResolutionId AlternateResolutionId = MakeResolutionId(10);
	const FActionId AlternateActionId = MakeActionId(8);

	{
		FSeededBattleRandom Parent(11ULL);
		FSeededBattleRandom WrongParent(12ULL);
		FSeededBattleRandom Reference(11ULL);
		TUniquePtr<IBattleRandomTransaction> Transaction;
		if (!Parent.TryCreateTransaction(Context.ResolutionId, Context.ActionId, Transaction))
		{
			AddError(TEXT("The parent-identity transaction was not created"));
			return false;
		}
		EBattleRandomTransactionCommitError Error = EBattleRandomTransactionCommitError::None;
		TestFalse(
			TEXT("A transaction rejects a different parent"),
			Transaction->TryCommit(
				WrongParent,
				Context.ResolutionId,
				Context.ActionId,
				Error));
		TestEqual(TEXT("The parent mismatch is typed"), Error,
			EBattleRandomTransactionCommitError::ParentIdentityMismatch);
		TestEqual(TEXT("The wrong parent remains unchanged"), WrongParent.GetTrace().Num(), 0);
		TestFalse(
			TEXT("The failed commit attempt finalizes the transaction"),
			Transaction->TryCommit(
				Parent,
				Context.ResolutionId,
				Context.ActionId,
				Error));
		TestEqual(TEXT("A retry after failure is already finalized"), Error,
			EBattleRandomTransactionCommitError::AlreadyFinalized);
		FBattleRandomDraw ParentDraw;
		FBattleRandomDraw ReferenceDraw;
		TestTrue(TEXT("The original parent still draws"), Parent.TryDrawUniform(0, 99, Context, ParentDraw));
		TestTrue(TEXT("The original reference still draws"), Reference.TryDrawUniform(0, 99, Context, ReferenceDraw));
		TestTrue(TEXT("Parent identity failure changes no parent position"), ParentDraw == ReferenceDraw);
	}

	{
		FSeededBattleRandom Parent(21ULL);
		FSeededBattleRandom Reference(21ULL);
		TUniquePtr<IBattleRandomTransaction> Transaction;
		if (!Parent.TryCreateTransaction(Context.ResolutionId, Context.ActionId, Transaction))
		{
			AddError(TEXT("The resolution-identity transaction was not created"));
			return false;
		}
		EBattleRandomTransactionCommitError Error = EBattleRandomTransactionCommitError::None;
		TestFalse(
			TEXT("A transaction rejects a different resolution at commit"),
			Transaction->TryCommit(Parent, AlternateResolutionId, Context.ActionId, Error));
		TestEqual(TEXT("The resolution mismatch is typed"), Error,
			EBattleRandomTransactionCommitError::ResolutionIdentityMismatch);
		FBattleRandomDraw ParentDraw;
		FBattleRandomDraw ReferenceDraw;
		TestTrue(TEXT("The resolution parent still draws"), Parent.TryDrawUniform(0, 99, Context, ParentDraw));
		TestTrue(TEXT("The resolution reference still draws"), Reference.TryDrawUniform(0, 99, Context, ReferenceDraw));
		TestTrue(TEXT("Resolution failure changes no parent position"), ParentDraw == ReferenceDraw);
	}

	{
		FSeededBattleRandom Parent(31ULL);
		FSeededBattleRandom Reference(31ULL);
		TUniquePtr<IBattleRandomTransaction> Transaction;
		if (!Parent.TryCreateTransaction(Context.ResolutionId, Context.ActionId, Transaction))
		{
			AddError(TEXT("The action-identity transaction was not created"));
			return false;
		}
		EBattleRandomTransactionCommitError Error = EBattleRandomTransactionCommitError::None;
		TestFalse(
			TEXT("A transaction rejects a different action at commit"),
			Transaction->TryCommit(Parent, Context.ResolutionId, AlternateActionId, Error));
		TestEqual(TEXT("The action mismatch is typed"), Error,
			EBattleRandomTransactionCommitError::ActionIdentityMismatch);
		FBattleRandomDraw ParentDraw;
		FBattleRandomDraw ReferenceDraw;
		TestTrue(TEXT("The action parent still draws"), Parent.TryDrawUniform(0, 99, Context, ParentDraw));
		TestTrue(TEXT("The action reference still draws"), Reference.TryDrawUniform(0, 99, Context, ReferenceDraw));
		TestTrue(TEXT("Action failure changes no parent position"), ParentDraw == ReferenceDraw);
	}

	{
		FSeededBattleRandom Parent(41ULL);
		FSeededBattleRandom Reference(41ULL);
		TUniquePtr<IBattleRandomTransaction> Transaction;
		if (!Parent.TryCreateTransaction(Context.ResolutionId, Context.ActionId, Transaction))
		{
			AddError(TEXT("The stale-parent transaction was not created"));
			return false;
		}
		FBattleRandomDraw ParentFirst;
		FBattleRandomDraw ReferenceFirst;
		TestTrue(TEXT("The parent advances outside the transaction"), Parent.TryDrawUniform(0, 99, Context, ParentFirst));
		TestTrue(TEXT("The stale reference advances equivalently"), Reference.TryDrawUniform(0, 99, Context, ReferenceFirst));
		TestTrue(TEXT("The direct parent advance is deterministic"), ParentFirst == ReferenceFirst);
		const int32 TraceCountBeforeFailure = Parent.GetTrace().Num();
		EBattleRandomTransactionCommitError Error = EBattleRandomTransactionCommitError::None;
		TestFalse(
			TEXT("A transaction rejects a parent that moved"),
			Transaction->TryCommit(
				Parent,
				Context.ResolutionId,
				Context.ActionId,
				Error));
		TestEqual(TEXT("The stale parent position is typed"), Error,
			EBattleRandomTransactionCommitError::ParentPositionMismatch);
		TestEqual(TEXT("The failed stale commit publishes nothing"), Parent.GetTrace().Num(), TraceCountBeforeFailure);
		FBattleRandomDraw ParentNext;
		FBattleRandomDraw ReferenceNext;
		TestTrue(TEXT("The stale parent accepts its next draw"), Parent.TryDrawUniform(2, 4, Context, ParentNext));
		TestTrue(TEXT("The stale reference accepts its next draw"), Reference.TryDrawUniform(2, 4, Context, ReferenceNext));
		TestTrue(TEXT("A stale commit changes no further parent position"), ParentNext == ReferenceNext);
	}

	{
		FSeededBattleRandom Parent(51ULL);
		FSeededBattleRandom Reference(51ULL);
		TUniquePtr<IBattleRandomTransaction> Transaction;
		if (!Parent.TryCreateTransaction(Context.ResolutionId, Context.ActionId, Transaction))
		{
			AddError(TEXT("The rejected-draw transaction was not created"));
			return false;
		}
		FBattleRandomDraw Draw;
		Draw.CallOrdinal = 99;
		TestFalse(
			TEXT("An invalid staged range is rejected"),
			Transaction->TryDrawUniform(5, 4, Context, Draw));
		TestEqual(TEXT("A rejected staged draw resets output"), Draw.CallOrdinal, 0ULL);
		TestFalse(
			TEXT("A staged failure remains sticky"),
			Transaction->TryDrawUniform(0, 99, Context, Draw));
		EBattleRandomTransactionCommitError Error = EBattleRandomTransactionCommitError::None;
		TestFalse(
			TEXT("A transaction with a rejected staged draw cannot commit"),
			Transaction->TryCommit(
				Parent,
				Context.ResolutionId,
				Context.ActionId,
				Error));
		TestEqual(TEXT("The staged failure is typed"), Error,
			EBattleRandomTransactionCommitError::StagedDrawRejected);
		TestEqual(TEXT("Rejected staged work leaves no parent trace"), Parent.GetTrace().Num(), 0);
		FBattleRandomDraw ParentDraw;
		FBattleRandomDraw ReferenceDraw;
		TestTrue(TEXT("The staged-failure parent still draws"), Parent.TryDrawUniform(0, 99, Context, ParentDraw));
		TestTrue(TEXT("The staged-failure reference still draws"), Reference.TryDrawUniform(0, 99, Context, ReferenceDraw));
		TestTrue(TEXT("Staged failure changes no parent position"), ParentDraw == ReferenceDraw);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleTransactionalRandomSequenceStrategyTest,
	"PokemonSolarus.Battle.ADR0002.3C.TransactionalRandom.Deterministic.SequenceStrategy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBattleTransactionalRandomSequenceStrategyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FBattleRandomContext Context = MakeRandomContext();
	BattleTest::FSequenceBattleRandom Parent({3, 7});

	TUniquePtr<IBattleRandomTransaction> RolledBack;
	if (!TestTrue(
		TEXT("The sequence rollback transaction is created"),
		Parent.TryCreateTransaction(Context.ResolutionId, Context.ActionId, RolledBack)))
	{
		return false;
	}
	FBattleRandomDraw Draw;
	TestTrue(TEXT("The sequence transaction stages its first result"),
		RolledBack->TryDrawUniform(0, 9, Context, Draw));
	TestEqual(TEXT("The first scripted sequence result is staged"), Draw.Result, static_cast<uint32>(3));
	TestEqual(TEXT("The scripted parent trace stays private"), Parent.GetTrace().Num(), 0);
	RolledBack->Rollback();

	TUniquePtr<IBattleRandomTransaction> Committed;
	if (!TestTrue(
		TEXT("The sequence commit transaction is created"),
		Parent.TryCreateTransaction(Context.ResolutionId, Context.ActionId, Committed)))
	{
		return false;
	}
	TUniquePtr<IBattleRandomTransaction> Nested;
	TestFalse(
		TEXT("A scripted transaction cannot nest"),
		Committed->TryCreateTransaction(Context.ResolutionId, Context.ActionId, Nested));
	TestTrue(TEXT("A rejected nested transaction resets its output"), Nested == nullptr);
	TestTrue(TEXT("Rollback restored the first sequence result"),
		Committed->TryDrawUniform(0, 9, Context, Draw));
	TestEqual(TEXT("The restored scripted result is three"), Draw.Result, static_cast<uint32>(3));
	EBattleRandomTransactionCommitError Error = EBattleRandomTransactionCommitError::AlreadyFinalized;
	TestTrue(
		TEXT("The scripted sequence prefix commits"),
		Committed->TryCommit(Parent, Context.ResolutionId, Context.ActionId, Error));
	TestEqual(TEXT("The scripted sequence commit reports no error"), Error,
		EBattleRandomTransactionCommitError::None);
	TestEqual(TEXT("Only one scripted result was committed"), Parent.GetTrace().Num(), 1);
	TestTrue(TEXT("The parent consumes the second scripted result directly"),
		Parent.TryDrawUniform(0, 9, Context, Draw));
	TestEqual(TEXT("The second scripted result is seven"), Draw.Result, static_cast<uint32>(7));
	TestEqual(TEXT("The direct draw continues at call ordinal two"), Draw.CallOrdinal, 2ULL);
	TestTrue(TEXT("The scripted sequence is exact after both results"), Parent.IsExact());

	BattleTest::FSequenceBattleRandom RejectedParent({5});
	TUniquePtr<IBattleRandomTransaction> Rejected;
	if (!RejectedParent.TryCreateTransaction(Context.ResolutionId, Context.ActionId, Rejected))
	{
		AddError(TEXT("The rejected sequence transaction was not created"));
		return false;
	}
	TestFalse(TEXT("An out-of-range scripted result is rejected while staged"),
		Rejected->TryDrawUniform(0, 4, Context, Draw));
	TestFalse(TEXT("The rejected scripted sequence cannot commit"),
		Rejected->TryCommit(RejectedParent, Context.ResolutionId, Context.ActionId, Error));
	TestEqual(TEXT("The scripted sequence failure is typed"), Error,
		EBattleRandomTransactionCommitError::StagedDrawRejected);
	TestEqual(TEXT("A rejected sequence commit leaves the parent private"), RejectedParent.GetTrace().Num(), 0);
	TestTrue(TEXT("The rejected parent retains its first scripted result"),
		RejectedParent.TryDrawUniform(0, 9, Context, Draw));
	TestEqual(TEXT("The retained sequence result is five"), Draw.Result, static_cast<uint32>(5));
	TestTrue(TEXT("The retained scripted sequence remains exact"), RejectedParent.IsExact());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleTransactionalRandomStrictStrategyTest,
	"PokemonSolarus.Battle.ADR0002.3C.TransactionalRandom.Deterministic.StrictStrategy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBattleTransactionalRandomStrictStrategyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FBattleRandomContext Context = MakeRandomContext();
	BattleTest::FStrictBattleRandom Parent({
		{0, 9, 4, Context.RulePurpose},
		{0, 1, 1, Context.RulePurpose}
	});

	TUniquePtr<IBattleRandomTransaction> RolledBack;
	if (!TestTrue(
		TEXT("The strict rollback transaction is created"),
		Parent.TryCreateTransaction(Context.ResolutionId, Context.ActionId, RolledBack)))
	{
		return false;
	}
	FBattleRandomDraw Draw;
	TestTrue(TEXT("The strict transaction accepts the exact first request"),
		RolledBack->TryDrawUniform(0, 9, Context, Draw));
	TestEqual(TEXT("The first strict result is staged"), Draw.Result, static_cast<uint32>(4));
	RolledBack->Rollback();
	TestEqual(TEXT("Strict rollback leaves the parent trace empty"), Parent.GetTrace().Num(), 0);
	TestFalse(TEXT("The untouched strict parent is not yet exact"), Parent.IsExact());

	TUniquePtr<IBattleRandomTransaction> Committed;
	if (!TestTrue(
		TEXT("The strict commit transaction is created"),
		Parent.TryCreateTransaction(Context.ResolutionId, Context.ActionId, Committed)))
	{
		return false;
	}
	TestTrue(TEXT("Strict rollback restored the exact first request"),
		Committed->TryDrawUniform(0, 9, Context, Draw));
	EBattleRandomTransactionCommitError Error = EBattleRandomTransactionCommitError::AlreadyFinalized;
	TestTrue(TEXT("The strict early-stop prefix commits"),
		Committed->TryCommit(Parent, Context.ResolutionId, Context.ActionId, Error));
	TestEqual(TEXT("The strict prefix commit reports no error"), Error,
		EBattleRandomTransactionCommitError::None);
	TestEqual(TEXT("Only one strict draw was committed"), Parent.GetTrace().Num(), 1);
	TestFalse(TEXT("One remaining strict draw keeps the parent inexact"), Parent.IsExact());
	TestTrue(TEXT("The parent accepts the exact second request directly"),
		Parent.TryDrawUniform(0, 1, Context, Draw));
	TestEqual(TEXT("The second strict result is one"), Draw.Result, static_cast<uint32>(1));
	TestEqual(TEXT("The second strict draw has call ordinal two"), Draw.CallOrdinal, 2ULL);
	TestTrue(TEXT("The strict script is exact after both requests"), Parent.IsExact());

	BattleTest::FStrictBattleRandom RejectedParent({
		{0, 9, 6, Context.RulePurpose}
	});
	TUniquePtr<IBattleRandomTransaction> Rejected;
	if (!RejectedParent.TryCreateTransaction(Context.ResolutionId, Context.ActionId, Rejected))
	{
		AddError(TEXT("The rejected strict transaction was not created"));
		return false;
	}
	TestFalse(TEXT("A strict range mismatch is rejected while staged"),
		Rejected->TryDrawUniform(0, 8, Context, Draw));
	TestFalse(TEXT("The rejected strict transaction cannot commit"),
		Rejected->TryCommit(RejectedParent, Context.ResolutionId, Context.ActionId, Error));
	TestEqual(TEXT("The strict strategy failure is typed"), Error,
		EBattleRandomTransactionCommitError::StagedDrawRejected);
	TestEqual(TEXT("A rejected strict commit leaves the parent trace empty"), RejectedParent.GetTrace().Num(), 0);
	TestFalse(TEXT("A rejected strict transaction leaves the parent unconsumed"), RejectedParent.IsExact());
	TestTrue(TEXT("The parent still accepts its original exact request"),
		RejectedParent.TryDrawUniform(0, 9, Context, Draw));
	TestEqual(TEXT("The retained strict result is six"), Draw.Result, static_cast<uint32>(6));
	TestTrue(TEXT("The retained strict script becomes exact"), RejectedParent.IsExact());
	return true;
}

}

#endif // WITH_DEV_AUTOMATION_TESTS
