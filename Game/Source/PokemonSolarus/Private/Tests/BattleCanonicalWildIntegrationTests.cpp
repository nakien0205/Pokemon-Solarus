#if WITH_DEV_AUTOMATION_TESTS

#include "BattleCanonicalIntegrationTestSupport.h"

#include "Battle/BattleCapture.h"
#include "Battle/BattleWildFlow.h"
#include "Misc/AutomationTest.h"

namespace BattleCanonicalWildIntegrationPrivate
{
using namespace BattleCanonicalIntegrationTestSupport;

FSetupSpec MakeWildSpec(const uint64 BattleValue, const EBattleFormat Format = EBattleFormat::Single)
{
	FSetupSpec Spec;
	Spec.BattleValue = BattleValue;
	Spec.EncounterKind = EBattleEncounterKind::Wild;
	Spec.Format = Format;
	Spec.Policies.bRunAllowed = true;
	Spec.Policies.bBagAllowed = true;
	Spec.Policies.bCaptureAllowed = true;
	Spec.Policies.bShiftPromptEligible = false;
	Spec.Trainers = {{1, EBattleSide::Player, EBattleTrainerRole::Player, EBattleDecisionController::Human, {{TEXT("Item.PokeBall"), 4}}},
		{2, EBattleSide::Opponent, EBattleTrainerRole::Opponent, EBattleDecisionController::EnemyAI, {}}};
	Spec.Battlers.Add({1, 11, 0, TEXT("Species.Clefable"), TEXT("Nature.Hardy"), TEXT("Ability.MagicGuard"), {TEXT("Move.SwordsDance"), TEXT("Move.Protect")}});
	Spec.Battlers.Add({2, 21, 0, TEXT("Species.Espathra"), TEXT("Nature.Hardy"), TEXT("Ability.SpeedBoost"), {TEXT("Move.SwordsDance"), TEXT("Move.Protect")}});
	Spec.Active = {{EBattleSide::Player, EBattlePosition::Left, 1, 11}, {EBattleSide::Opponent, EBattlePosition::Left, 2, 21}};
	if (Format == EBattleFormat::Double)
	{
		Spec.Battlers.Add({1, 12, 1, TEXT("Species.Charizard"), TEXT("Nature.Hardy"), TEXT("Ability.Blaze"), {TEXT("Move.Protect")}});
		Spec.Battlers.Add({2, 22, 1, TEXT("Species.Venusaur"), TEXT("Nature.Hardy"), TEXT("Ability.Overgrow"), {TEXT("Move.SwordsDance")}});
		Spec.Active.Add({EBattleSide::Player, EBattlePosition::Right, 1, 12});
		Spec.Active.Add({EBattleSide::Opponent, EBattlePosition::Right, 2, 22});
	}
	Spec.CaptureCapacity = {1, 4};
	Spec.CaptureProgression.bHasSnapshot = true;
	return Spec;
}

bool Build(FAutomationTestBase& Test, const FSetupSpec& Spec, FCatalogFixture& Fixture, FBattleSetup& Setup)
{
	FString Error;
	if (!TryLoadProductionFixture(Test, Fixture, Error) || !TryBuildSetup(Fixture.Catalog, Spec, Setup, Error))
	{
		Test.AddError(Error); return false;
	}
	return true;
}

FChoice Fight(const TCHAR* Move)
{
	FChoice Choice; Choice.DefinitionId = FName(Move); return Choice;
}

bool Finish(FBattleEngine& Engine, FRunEvidence& Evidence, FString& Error)
{
	if (!ExecuteLockedQueue(Engine, Evidence, Error)) return false;
	return Engine.GetSnapshot().GetPhase() == EBattlePhase::EndOfTurn ? ResolveEndTurn(Engine, Evidence, Error) : true;
}

int32 CountSourceEvents(
	const FBattleReplayRecord& Record,
	const EBattleEventType Type,
	const uint64 BattlerValue)
{
	int32 Count = 0;
	for (const FBattleResolution& Resolution : Record.GetResolutions())
		for (const FBattleEvent& Event : Resolution.GetEvents())
			Count += Event.GetType() == Type
				&& Event.GetSource().BattlerId == MakeNumericId<FBattlerId>(BattlerValue) ? 1 : 0;
	return Count;
}

FBattlerId ActiveBattler(
	const FBattleSnapshot& Snapshot,
	const EBattleSide Side,
	const EBattlePosition Position)
{
	const FActiveSlotId Slot = MakeActiveSlotId(Side, Position);
	const FBattleActiveAssignment* Assignment = Snapshot.GetActiveAssignments().FindByPredicate(
		[Slot](const FBattleActiveAssignment& Value) { return Value.ActiveSlotId == Slot; });
	return Assignment == nullptr ? FBattlerId() : Assignment->BattlerId;
}

int32 MovePP(
	const FBattleSnapshot& Snapshot,
	const uint64 BattlerValue,
	const TCHAR* MoveName)
{
	const FBattlePartyEntrySetup* Battler = Snapshot.FindBattler(MakeNumericId<FBattlerId>(BattlerValue));
	const FMoveId MoveId = MakeDefinitionId<FMoveId>(MoveName);
	const FBattleMoveSlotSetup* Move = Battler == nullptr ? nullptr : Battler->Moves.FindByPredicate(
		[MoveId](const FBattleMoveSlotSetup& Value) { return Value.MoveId == MoveId; });
	return Move == nullptr ? INDEX_NONE : Move->CurrentPP;
}

int32 BagCount(
	const FBattleSnapshot& Snapshot,
	const uint64 TrainerValue,
	const TCHAR* ItemName)
{
	const FTrainerId TrainerId = MakeNumericId<FTrainerId>(TrainerValue);
	const FItemId ItemId = MakeDefinitionId<FItemId>(ItemName);
	for (const FBattleTrainerSetup& Trainer : Snapshot.GetTrainers())
		if (Trainer.TrainerId == TrainerId)
			for (const FBattleBagItemCount& Item : Trainer.Bag)
				if (Item.ItemId == ItemId) return Item.Count;
	return INDEX_NONE;
}

bool RejectWithoutMutation(
	FBattleEngine& Engine,
	const FBattleDecision& Decision,
	const EBattleRejectionReason ExpectedReason,
	FString& OutError)
{
	const FString SnapshotBefore = SnapshotSignature(Engine.GetSnapshot());
	const int32 TraceBefore = Engine.ExportRandomTrace().Num();
	const int32 RequestsBefore = Engine.GetPendingDecisionRequests().Num();
	const FBattleReplayRecord ReplayBefore = Engine.ExportReplayRecord();
	const FBattleResolution Rejected = Engine.SubmitDecision(Decision);
	const FBattleReplayRecord ReplayAfter = Engine.ExportReplayRecord();
	if (Rejected.WasAccepted()
		|| Rejected.GetRejection().Reason != ExpectedReason
		|| Rejected.GetBeforeStateVersion() != Rejected.GetAfterStateVersion()
		|| SnapshotSignature(Engine.GetSnapshot()) != SnapshotBefore
		|| Engine.ExportRandomTrace().Num() != TraceBefore
		|| Engine.GetPendingDecisionRequests().Num() != RequestsBefore
		|| Rejected.GetEvents().Num() != 1
		|| Rejected.GetEvents()[0].GetType() != EBattleEventType::DecisionRejected
		|| ReplayAfter.GetInputs().Decisions.Num() != ReplayBefore.GetInputs().Decisions.Num() + 1
		|| ReplayAfter.GetResolutions().Num() != ReplayBefore.GetResolutions().Num() + 1)
	{
		OutError = TEXT("Rejected wild-flow decision changed state, RNG, pending requests, resources, or its exact audit delta.");
		return false;
	}
	return true;
}
} // namespace BattleCanonicalWildIntegrationPrivate

using namespace BattleCanonicalWildIntegrationPrivate;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FC11AWildRun, "PokemonSolarus.Battle.C11A.WildPartner.Run.TrainerBlockedWildFailureSuccessCounterAndRng", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FC11AWildRun::RunTest(const FString& Parameters)
{
	FCatalogFixture Fixture; FBattleSetup TrainerSetup;
	FSetupSpec TrainerSpec = MakeWildSpec(11501);
	TrainerSpec.EncounterKind = EBattleEncounterKind::Trainer;
	TrainerSpec.Policies.bRunAllowed = false;
	TrainerSpec.Policies.bCaptureAllowed = false;
	TrainerSpec.CaptureCapacity = {0, 0};
	TrainerSpec.CaptureProgression = {};
	TrainerSpec.Trainers[0].Bag.Reset();
	if (!Build(*this, TrainerSpec, Fixture, TrainerSetup)) return false;
	const FDriveFunction TrainerDrive = [](FBattleEngine& Engine, FRunEvidence& Evidence, FString& Error)
	{
		FBattleRejection Rejection;
		if (!Engine.TryBeginActionDecisionSequence(Rejection))
		{
			Error = TEXT("Trainer Run rejection probe could not begin.");
			return false;
		}
		RecordCheckpoint(Engine, Evidence, TEXT("trainer-run-selection"));
		const TArray<FBattleDecisionRequest> Requests = Engine.GetPendingDecisionRequests();
		if (Requests.Num() != 1
			|| Requests[0].GetLegalActionKinds().Contains(EBattleActionKind::Run)
			|| !Requests[0].GetUnavailableOptions().ContainsByPredicate([](const FBattleUnavailableDecisionOption& Option)
			{
				return Option.Kind == EBattleDecisionOptionKind::Action
					&& Option.ActionKind == EBattleActionKind::Run
					&& Option.Reason == EBattleOptionUnavailableReason::RunRestricted;
			}))
		{
			Error = TEXT("Trainer selection did not expose the exact RunRestricted action option.");
			return false;
		}
		FChoice Choice; Choice.Kind = EChoiceKind::Run;
		FBattleDecision Forged;
		if (!TryMakeDecision(Requests[0], Choice, Forged, Error)
			|| !RejectWithoutMutation(Engine, Forged, EBattleRejectionReason::IllegalAction, Error)) return false;
		RecordCheckpoint(Engine, Evidence, TEXT("trainer-run-rejected"));
		return true;
	};
	FRunEvidence TrainerEvidence;
	const bool bTrainerTwins = RunDeterministicTwins(*this, TEXT("trainer Run rejection"),
		Fixture.Catalog, TrainerSetup, TrainerDrive, &TrainerEvidence, 5);
	TestEqual(TEXT("Trainer rejection publishes exactly one DecisionRejected"),
		CountEvents(TrainerEvidence.Replay, EBattleEventType::DecisionRejected), 1);

	FString Error;
	FBattleSetup WildSetup;
	if (!TryBuildSetup(Fixture.Catalog, MakeWildSpec(11502), WildSetup, Error)) { AddError(Error); return false; }
	const FChoiceProvider Provider = [](const FBattleDecisionRequest& Request, FChoice& Choice, FString&)
	{
		Choice = Request.GetDecisionOwnerTrainerId().GetValue() == 1 ? FChoice{EChoiceKind::Run} : Fight(TEXT("Move.SwordsDance"));
		return true;
	};
	const FDriveFunction Drive = [Provider](FBattleEngine& Engine, FRunEvidence& Evidence, FString& OutError)
	{
		return LockTurn(Engine, Provider, Evidence, OutError) && Finish(Engine, Evidence, OutError);
	};
	const FDefinitionId RunPurpose = FBattleRunRules::GetRandomCheckPurpose();
	FRunEvidence Failed;
	const bool bFailedTwins = RunStrictTwins(*this, TEXT("wild Run failure boundary"), Fixture.Catalog, WildSetup, {{0, 255, 112, RunPurpose}}, Drive, &Failed);
	TestEqual(TEXT("A legal failed Run persists counter two"), Failed.Replay.GetFinalSnapshot().GetEscapeAttemptCount(), 2U);
	TestFalse(TEXT("Failure does not escape"), HasEvent(Failed.Replay, EBattleEventType::Escaped));
	TestEqual(TEXT("Failure publishes one RunAttempted"), CountEvents(Failed.Replay, EBattleEventType::RunAttempted), 1);

	FRunEvidence Succeeded;
	const bool bSucceededTwins = RunStrictTwins(*this, TEXT("wild Run success boundary"), Fixture.Catalog, WildSetup, {{0, 255, 111, RunPurpose}}, Drive, &Succeeded);
	TestTrue(TEXT("Success publishes Escaped"), HasEvent(Succeeded.Replay, EBattleEventType::Escaped));
	TestEqual(TEXT("Success has the Escape outcome"), Succeeded.Replay.GetFinalSnapshot().GetOutcome(), EBattleOutcome::Escape);
	TestEqual(TEXT("Player Run has the ordinary outcome cause"), Succeeded.Replay.GetFinalSnapshot().GetOutcomeCause(), EBattleOutcomeCause::Ordinary);

	FSetupSpec PersistenceSpec = MakeWildSpec(11507);
	PersistenceSpec.Battlers.Add({1, 12, 1, TEXT("Species.Clefable"), TEXT("Nature.Hardy"), TEXT("Ability.MagicGuard"), {TEXT("Move.SwordsDance"), TEXT("Move.Protect")}});
	FBattleSetup PersistenceSetup;
	if (!TryBuildSetup(Fixture.Catalog, PersistenceSpec, PersistenceSetup, Error)) { AddError(Error); return false; }
	int32 PersistenceTurn = 0;
	const FChoiceProvider PersistenceProvider = [&PersistenceTurn](const FBattleDecisionRequest& Request, FChoice& Choice, FString&)
	{
		if (Request.GetDecisionOwnerTrainerId() != MakeNumericId<FTrainerId>(1))
		{
			Choice = Fight(TEXT("Move.SwordsDance"));
		}
		else if (PersistenceTurn == 0 || PersistenceTurn == 3)
		{
			Choice.Kind = EChoiceKind::Run;
		}
		else if (PersistenceTurn == 1)
		{
			Choice.Kind = EChoiceKind::Switch;
			Choice.PartyTarget = MakePartySlotId(1);
		}
		else
		{
			Choice = Fight(TEXT("Move.SwordsDance"));
		}
		return true;
	};
	const FDriveFunction PersistenceDrive = [&PersistenceTurn, PersistenceProvider](FBattleEngine& Engine, FRunEvidence& Evidence, FString& OutError) mutable
	{
		for (PersistenceTurn = 0; PersistenceTurn < 4; ++PersistenceTurn)
		{
			if (!LockTurn(Engine, PersistenceProvider, Evidence, OutError)
				|| !Finish(Engine, Evidence, OutError)) return false;
			const uint32 ExpectedCount = PersistenceTurn == 0 ? 2U : PersistenceTurn == 3 ? 3U : 2U;
			if (Engine.GetSnapshot().GetEscapeAttemptCount() != ExpectedCount)
			{
				OutError = FString::Printf(TEXT("Escape-attempt counter mismatch after persistence turn %d."), PersistenceTurn + 1);
				return false;
			}
		}
		return true;
	};
	FRunEvidence Persistence;
	const bool bPersistenceTwins = RunStrictTwins(*this, TEXT("Run counter across switch and ordinary action"),
		Fixture.Catalog, PersistenceSetup,
		{{0, 255, 112, RunPurpose}, {0, 255, 142, RunPurpose}}, PersistenceDrive, &Persistence);
	TestEqual(TEXT("Two equal-boundary legal Run failures persist counter three"),
		Persistence.Replay.GetFinalSnapshot().GetEscapeAttemptCount(), 3U);
	TestEqual(TEXT("Persistence path publishes exactly two Run attempts"),
		CountEvents(Persistence.Replay, EBattleEventType::RunAttempted), 2);
	TestEqual(TEXT("Persistence path performs exactly one voluntary switch"),
		CountEvents(Persistence.Replay, EBattleEventType::Switched), 1);
	TestFalse(TEXT("Equal-boundary attempts do not escape"), HasEvent(Persistence.Replay, EBattleEventType::Escaped));

	return bTrainerTwins && bFailedTwins && bSucceededTwins && bPersistenceTwins
		&& ValidateGlobalInvariants(*this, Fixture.Catalog, TrainerEvidence, TEXT("trainer Run rejection"))
		&& ValidateGlobalInvariants(*this, Fixture.Catalog, Failed, TEXT("failed Run"))
		&& ValidateGlobalInvariants(*this, Fixture.Catalog, Succeeded, TEXT("successful Run"))
		&& ValidateGlobalInvariants(*this, Fixture.Catalog, Persistence, TEXT("persistent Run counter"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FC11AWildCapture, "PokemonSolarus.Battle.C11A.WildPartner.Capture.CapacityFailureSuccessMultipleCancellationAndDestinations", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FC11AWildCapture::RunTest(const FString& Parameters)
{
	FCatalogFixture Fixture; FBattleSetup BlockedSetup;
	FSetupSpec BlockedSpec = MakeWildSpec(11503);
	BlockedSpec.CaptureCapacity = {0, 0};
	if (!Build(*this, BlockedSpec, Fixture, BlockedSetup)) return false;
	const FDriveFunction BlockedDrive = [](FBattleEngine& Engine, FRunEvidence& Evidence, FString& Error)
	{
		FBattleRejection Rejection;
		if (!Engine.TryBeginActionDecisionSequence(Rejection))
		{
			Error = TEXT("Capture-capacity rejection probe could not begin.");
			return false;
		}
		RecordCheckpoint(Engine, Evidence, TEXT("capacity-selection"));
		const TArray<FBattleDecisionRequest> Requests = Engine.GetPendingDecisionRequests();
		const FItemId BallId = MakeDefinitionId<FItemId>(TEXT("Item.PokeBall"));
		if (Requests.Num() != 1
			|| Requests[0].GetLegalActionKinds().Contains(EBattleActionKind::Bag)
			|| Requests[0].GetLegalItemIds().Contains(BallId)
			|| !Requests[0].GetUnavailableOptions().ContainsByPredicate([BallId](const FBattleUnavailableDecisionOption& Option)
			{
				return Option.Kind == EBattleDecisionOptionKind::Item
					&& Option.ItemId == BallId
					&& Option.Reason == EBattleOptionUnavailableReason::CaptureCapacityFull;
			})
			|| !Requests[0].GetUnavailableOptions().ContainsByPredicate([](const FBattleUnavailableDecisionOption& Option)
			{
				return Option.Kind == EBattleDecisionOptionKind::Action
					&& Option.ActionKind == EBattleActionKind::Bag
					&& Option.Reason == EBattleOptionUnavailableReason::CaptureCapacityFull;
			}))
		{
			Error = TEXT("Zero-capacity selection did not expose exact item and action unavailability.");
			return false;
		}
		FChoice Ball; Ball.Kind = EChoiceKind::Bag; Ball.DefinitionId = TEXT("Item.PokeBall");
		Ball.ActiveTarget = MakeActiveSlotId(EBattleSide::Opponent, EBattlePosition::Left);
		FBattleDecision Forged;
		if (!TryMakeDecision(Requests[0], Ball, Forged, Error)
			|| !RejectWithoutMutation(Engine, Forged, EBattleRejectionReason::IllegalAction, Error)) return false;
		RecordCheckpoint(Engine, Evidence, TEXT("capacity-rejected"));
		return true;
	};
	FRunEvidence BlockedEvidence;
	const bool bBlockedTwins = RunDeterministicTwins(*this, TEXT("capture capacity rejection"),
		Fixture.Catalog, BlockedSetup, BlockedDrive, &BlockedEvidence, 5);
	TestEqual(TEXT("Capacity rejection retains all four Poke Balls"),
		BagCount(BlockedEvidence.Replay.GetFinalSnapshot(), 1, TEXT("Item.PokeBall")), 4);
	TestEqual(TEXT("Capacity rejection publishes exactly one DecisionRejected"),
		CountEvents(BlockedEvidence.Replay, EBattleEventType::DecisionRejected), 1);

	FString Error;
	auto RunCaptureCase = [this, &Fixture](
		const uint64 BattleValue,
		const TCHAR* Label,
		const bool bCritical,
		const bool bSuccess,
		const TArray<BattleTest::FBattleExpectedRandomDraw>& ExpectedDraws,
		FRunEvidence& OutEvidence)
	{
		FSetupSpec Spec = MakeWildSpec(BattleValue);
		Spec.CaptureCapacity = {1, 0};
		Spec.Trainers[0].Bag = {{TEXT("Item.PokeBall"), 1}};
		Spec.CaptureProgression.bCriticalCaptureEnabled = bCritical;
		Spec.CaptureProgression.CaughtSpeciesCount = bCritical ? 601U : 0U;
		Spec.CaptureProgression.bCatchingCharm = bCritical;
		FBattleSetup Setup;
		FString CaseError;
		if (!TryBuildSetup(Fixture.Catalog, Spec, Setup, CaseError))
		{
			AddError(CaseError);
			return false;
		}
		const FChoiceProvider Provider = [](const FBattleDecisionRequest& Request, FChoice& Choice, FString&)
		{
			if (Request.GetDecisionOwnerTrainerId() == MakeNumericId<FTrainerId>(1))
			{
				Choice.Kind = EChoiceKind::Bag;
				Choice.DefinitionId = TEXT("Item.PokeBall");
				Choice.ActiveTarget = MakeActiveSlotId(EBattleSide::Opponent, EBattlePosition::Left);
			}
			else Choice = Fight(TEXT("Move.SwordsDance"));
			return true;
		};
		const FDriveFunction Drive = [Provider](FBattleEngine& Engine, FRunEvidence& Evidence, FString& OutError)
		{
			return LockTurn(Engine, Provider, Evidence, OutError) && Finish(Engine, Evidence, OutError);
		};
		bool bValid = RunStrictTwins(*this, Label, Fixture.Catalog, Setup, ExpectedDraws, Drive, &OutEvidence);
		const FBattleEvent* Attempt = FindEvent(OutEvidence.Replay, EBattleEventType::CaptureAttempted);
		const FBattleCaptureEventMetadata* Metadata = Attempt != nullptr && Attempt->GetCapture().IsSet()
			? &Attempt->GetCapture().GetValue() : nullptr;
		const uint8 Required = bCritical ? 1 : 4;
		bValid &= TestTrue(FString(Label) + TEXT(" publishes exact non-guaranteed metadata"),
			Metadata != nullptr
			&& Metadata->bCriticalEligible == bCritical
			&& Metadata->bCriticalCapture == bCritical
			&& !Metadata->bGuaranteedCapture
			&& !Metadata->bMustCapture
			&& Metadata->bSucceeded == bSuccess
			&& Metadata->RequiredShakeChecks == Required
			&& Metadata->ShakeChecksPerformed == (bSuccess ? Required : 1)
			&& Metadata->ShakeChecksPassed == (bSuccess ? Required : 0)
			&& Metadata->ShakeThreshold > 0
			&& (!bCritical || Metadata->CriticalThreshold > 0));
		bValid &= TestEqual(FString(Label) + TEXT(" consumes one ball"),
			BagCount(OutEvidence.Replay.GetFinalSnapshot(), 1, TEXT("Item.PokeBall")), 0);
		bValid &= TestEqual(FString(Label) + TEXT(" publishes one ItemConsumed"),
			CountEvents(OutEvidence.Replay, EBattleEventType::ItemConsumed), 1);
		bValid &= TestEqual(FString(Label) + TEXT(" has exact Captured count"),
			CountEvents(OutEvidence.Replay, EBattleEventType::Captured), bSuccess ? 1 : 0);
		return bValid && ValidateGlobalInvariants(*this, Fixture.Catalog, OutEvidence, Label);
	};

	const FDefinitionId CriticalPurpose = FBattleCaptureCalculator::GetCriticalCapturePurpose();
	const FDefinitionId ShakePurpose = FBattleCaptureCalculator::GetShakeCheckPurpose();
	FRunEvidence NormalSuccess, NormalFailure, CriticalSuccess, CriticalFailure;
	const bool bNormalSuccess = RunCaptureCase(11508, TEXT("normal four-shake capture success"), false, true,
		{{0, 65535, 0, ShakePurpose}, {0, 65535, 0, ShakePurpose},
		 {0, 65535, 0, ShakePurpose}, {0, 65535, 0, ShakePurpose}}, NormalSuccess);
	const bool bNormalFailure = RunCaptureCase(11509, TEXT("normal early-stop capture failure"), false, false,
		{{0, 65535, 65535, ShakePurpose}}, NormalFailure);
	const bool bCriticalSuccess = RunCaptureCase(11510, TEXT("critical one-shake capture success"), true, true,
		{{0, 255, 0, CriticalPurpose}, {0, 65535, 0, ShakePurpose}}, CriticalSuccess);
	const bool bCriticalFailure = RunCaptureCase(11511, TEXT("critical one-shake capture failure"), true, false,
		{{0, 255, 0, CriticalPurpose}, {0, 65535, 65535, ShakePurpose}}, CriticalFailure);

	FSetupSpec CaptureSpec = MakeWildSpec(11504, EBattleFormat::Double);
	CaptureSpec.CaptureCapacity = {1, 1};
	CaptureSpec.CaptureProgression.bMustCapture = true;
	CaptureSpec.Trainers[0].Bag = {{TEXT("Item.PokeBall"), 2}};
	FBattleSetup CaptureSetup;
	if (!TryBuildSetup(Fixture.Catalog, CaptureSpec, CaptureSetup, Error)) { AddError(Error); return false; }
	int32 CaptureTurn = 0;
	const FChoiceProvider Provider = [&CaptureTurn](const FBattleDecisionRequest& Pending, FChoice& Choice, FString&)
	{
		if (Pending.GetDecisionOwnerTrainerId().GetValue() == 1)
		{
			if (Pending.GetActingBattlerId().GetValue() == 11)
			{
				Choice.Kind = EChoiceKind::Bag; Choice.DefinitionId = TEXT("Item.PokeBall");
				Choice.ActiveTarget = MakeActiveSlotId(EBattleSide::Opponent, CaptureTurn == 0 ? EBattlePosition::Left : EBattlePosition::Right);
			}
			else Choice = Fight(TEXT("Move.Protect"));
		}
		else Choice = Fight(TEXT("Move.SwordsDance"));
		return true;
	};
	const FDriveFunction Drive = [&CaptureTurn, Provider](FBattleEngine& Engine, FRunEvidence& Evidence, FString& OutError) mutable
	{
		for (CaptureTurn = 0; CaptureTurn < 2 && Engine.GetSnapshot().GetOutcome() == EBattleOutcome::InProgress; ++CaptureTurn)
		{
			const uint64 Target = CaptureTurn == 0 ? 21 : 22;
			const int32 PPBefore = MovePP(Engine.GetSnapshot(), Target, TEXT("Move.SwordsDance"));
			if (!LockTurn(Engine, Provider, Evidence, OutError) || !Finish(Engine, Evidence, OutError)) return false;
			if (MovePP(Engine.GetSnapshot(), Target, TEXT("Move.SwordsDance")) != PPBefore)
			{
				OutError = TEXT("A captured battler spent PP on its capture turn.");
				return false;
			}
		}
		return true;
	};
	FRunEvidence Evidence;
	const bool bTwins = RunDeterministicTwins(*this, TEXT("two guaranteed captures"), Fixture.Catalog, CaptureSetup, Drive, &Evidence);
	TestEqual(TEXT("Both wild battlers are captured"), CountEvents(Evidence.Replay, EBattleEventType::Captured), 2);
	TestEqual(TEXT("Both balls are consumed exactly once"), CountEvents(Evidence.Replay, EBattleEventType::ItemConsumed), 2);
	TestTrue(TEXT("Nonterminal captured actor is canceled and neither target spends PP on its capture turn"),
		CountSourceEvents(Evidence.Replay, EBattleEventType::ActionCanceled, 21) == 1
		&& CountSourceEvents(Evidence.Replay, EBattleEventType::MoveUsed, 21) == 0
		&& CountSourceEvents(Evidence.Replay, EBattleEventType::PPConsumed, 21) == 0
		&& CountSourceEvents(Evidence.Replay, EBattleEventType::MoveUsed, 22) == 1
		&& CountSourceEvents(Evidence.Replay, EBattleEventType::PPConsumed, 22) == 1);
	const FBattleSnapshot& Final = Evidence.Replay.GetFinalSnapshot();
	TestEqual(TEXT("Two-capture path consumes the exact Bag resource"),
		BagCount(Final, 1, TEXT("Item.PokeBall")), 0);
	TestEqual(TEXT("Both pending captures are retained"), Final.GetPendingCaptures().Num(), 2);
	TestTrue(TEXT("Capture destinations remain Party then Storage"), Final.GetPendingCaptures().Num() == 2
		&& Final.GetPendingCaptures()[0].Destination == EBattlePendingCaptureDestination::Party
		&& Final.GetPendingCaptures()[1].Destination == EBattlePendingCaptureDestination::Storage);
	TArray<const FBattleEvent*> CapturedEvents;
	for (const FBattleResolution& Resolution : Evidence.Replay.GetResolutions())
		for (const FBattleEvent& Event : Resolution.GetEvents())
			if (Event.GetType() == EBattleEventType::Captured) CapturedEvents.Add(&Event);
	TestTrue(TEXT("Captured events publish target order and Party then Storage metadata"),
		CapturedEvents.Num() == 2
		&& CapturedEvents[0]->GetTargets().Num() == 1
		&& CapturedEvents[1]->GetTargets().Num() == 1
		&& CapturedEvents[0]->GetTargets()[0].BattlerId == MakeNumericId<FBattlerId>(21)
		&& CapturedEvents[1]->GetTargets()[0].BattlerId == MakeNumericId<FBattlerId>(22)
		&& CapturedEvents[0]->GetCapture().IsSet()
		&& CapturedEvents[1]->GetCapture().IsSet()
		&& CapturedEvents[0]->GetCapture()->PendingDestination == EBattlePendingCaptureDestination::Party
		&& CapturedEvents[1]->GetCapture()->PendingDestination == EBattlePendingCaptureDestination::Storage);
	TestEqual(TEXT("Last capture ends in Victory"), Final.GetOutcome(), EBattleOutcome::Victory);
	TestEqual(TEXT("Last capture uses the Capture outcome cause"), Final.GetOutcomeCause(), EBattleOutcomeCause::Capture);
	return bBlockedTwins && bNormalSuccess && bNormalFailure && bCriticalSuccess && bCriticalFailure && bTwins
		&& ValidateGlobalInvariants(*this, Fixture.Catalog, BlockedEvidence, TEXT("capture-capacity rejection"))
		&& ValidateGlobalInvariants(*this, Fixture.Catalog, Evidence, TEXT("two guaranteed captures"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FC11AWildFlee, "PokemonSolarus.Battle.C11A.WildPartner.WildFlee.DisabledChanceMultipleOpponentsAndOutcome", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FC11AWildFlee::RunTest(const FString& Parameters)
{
	FCatalogFixture Fixture; FBattleSetup DisabledSetup;
	if (!Build(*this, MakeWildSpec(11505), Fixture, DisabledSetup)) return false;
	const FDriveFunction DisabledDrive = [](FBattleEngine& Engine, FRunEvidence& Evidence, FString& Error)
	{
		FBattleRejection Rejection;
		if (!Engine.TryBeginActionDecisionSequence(Rejection))
		{
			Error = TEXT("Disabled WildFlee probe could not begin.");
			return false;
		}
		RecordCheckpoint(Engine, Evidence, TEXT("disabled-flee-player-selection"));
		const FChoiceProvider Player = [](const FBattleDecisionRequest&, FChoice& Choice, FString&)
		{
			Choice = Fight(TEXT("Move.SwordsDance"));
			return true;
		};
		if (!SubmitPendingChoices(Engine, Player, Evidence, Error)) return false;
		const TArray<FBattleDecisionRequest> Requests = Engine.GetPendingDecisionRequests();
		if (Requests.Num() != 1
			|| Requests[0].GetDecisionOwnerTrainerId() != MakeNumericId<FTrainerId>(2)
			|| Requests[0].GetLegalActionKinds().Contains(EBattleActionKind::WildFlee)
			|| Requests[0].GetUnavailableOptions().ContainsByPredicate([](const FBattleUnavailableDecisionOption& Option)
			{
				return Option.Kind == EBattleDecisionOptionKind::Action
					&& Option.ActionKind == EBattleActionKind::WildFlee;
			}))
		{
			Error = TEXT("Disabled WildFlee policy did not produce the exact absent action surface.");
			return false;
		}
		FChoice Flee; Flee.Kind = EChoiceKind::WildFlee;
		FBattleDecision Forged;
		if (!TryMakeDecision(Requests[0], Flee, Forged, Error)
			|| !RejectWithoutMutation(Engine, Forged, EBattleRejectionReason::IllegalAction, Error)) return false;
		RecordCheckpoint(Engine, Evidence, TEXT("disabled-flee-rejected"));
		return true;
	};
	FRunEvidence DisabledEvidence;
	const bool bDisabledTwins = RunDeterministicTwins(*this, TEXT("disabled WildFlee rejection"),
		Fixture.Catalog, DisabledSetup, DisabledDrive, &DisabledEvidence, 5);
	TestEqual(TEXT("Disabled WildFlee publishes one exact rejection"),
		CountEvents(DisabledEvidence.Replay, EBattleEventType::DecisionRejected), 1);

	FSetupSpec ChanceSpec = MakeWildSpec(11506, EBattleFormat::Double);
	ChanceSpec.Policies.WildFleeMode = EBattleWildFleeMode::Chance;
	ChanceSpec.Policies.WildFleeNumerator = 1;
	ChanceSpec.Policies.WildFleeDenominator = 2;
	FBattleSetup ChanceSetup;
	FString Error;
	if (!TryBuildSetup(Fixture.Catalog, ChanceSpec, ChanceSetup, Error)) { AddError(Error); return false; }
	const FChoiceProvider Provider = [](const FBattleDecisionRequest& Pending, FChoice& Choice, FString&)
	{
		const uint64 Actor = Pending.GetActingBattlerId().GetValue();
		if (Actor == 21) Choice = FChoice{EChoiceKind::WildFlee};
		else if (Actor == 22) Choice = Fight(TEXT("Move.SwordsDance"));
		else Choice = Fight(TEXT("Move.Protect"));
		return true;
	};
	const FDriveFunction Drive = [Provider](FBattleEngine& Engine, FRunEvidence& Evidence, FString& OutError)
	{
		return LockTurn(Engine, Provider, Evidence, OutError) && Finish(Engine, Evidence, OutError);
	};
	const FDefinitionId FleePurpose = FBattleWildFleeRules::GetRandomCheckPurpose();
	FRunEvidence Failed;
	const bool bFailedTwins = RunStrictTwins(*this, TEXT("configured WildFlee chance failure"),
		Fixture.Catalog, ChanceSetup, {{0, 1, 1, FleePurpose}}, Drive, &Failed);
	TestTrue(TEXT("Chance failure keeps both wild battlers active and spends no fleeing-actor PP"),
		Failed.Replay.GetFinalSnapshot().GetOutcome() == EBattleOutcome::InProgress
		&& ActiveBattler(Failed.Replay.GetFinalSnapshot(), EBattleSide::Opponent, EBattlePosition::Left)
			== MakeNumericId<FBattlerId>(21)
		&& ActiveBattler(Failed.Replay.GetFinalSnapshot(), EBattleSide::Opponent, EBattlePosition::Right)
			== MakeNumericId<FBattlerId>(22)
		&& CountSourceEvents(Failed.Replay, EBattleEventType::RunAttempted, 21) == 1
		&& CountSourceEvents(Failed.Replay, EBattleEventType::Escaped, 21) == 0
		&& CountSourceEvents(Failed.Replay, EBattleEventType::Removed, 21) == 0
		&& MovePP(Failed.Replay.GetFinalSnapshot(), 21, TEXT("Move.SwordsDance"))
			== ChanceSetup.FindBattler(MakeNumericId<FBattlerId>(21))->Moves[0].CurrentPP);

	FRunEvidence Succeeded;
	const bool bSucceededTwins = RunStrictTwins(*this, TEXT("configured WildFlee chance success"),
		Fixture.Catalog, ChanceSetup, {{0, 1, 0, FleePurpose}}, Drive, &Succeeded);
	TestTrue(TEXT("Chance success removes only the fleeing wild without spending its PP"),
		CountSourceEvents(Succeeded.Replay, EBattleEventType::RunAttempted, 21) == 1
		&& CountEvents(Succeeded.Replay, EBattleEventType::Escaped) == 1
		&& CountEvents(Succeeded.Replay, EBattleEventType::Removed) == 1
		&& !ActiveBattler(Succeeded.Replay.GetFinalSnapshot(), EBattleSide::Opponent, EBattlePosition::Left).IsValid()
		&& ActiveBattler(Succeeded.Replay.GetFinalSnapshot(), EBattleSide::Opponent, EBattlePosition::Right)
			== MakeNumericId<FBattlerId>(22)
		&& MovePP(Succeeded.Replay.GetFinalSnapshot(), 21, TEXT("Move.SwordsDance"))
			== ChanceSetup.FindBattler(MakeNumericId<FBattlerId>(21))->Moves[0].CurrentPP);
	TestEqual(TEXT("One remaining wild keeps the double battle ongoing"),
		Succeeded.Replay.GetFinalSnapshot().GetOutcome(), EBattleOutcome::InProgress);

	FSetupSpec TerminalSpec = MakeWildSpec(11512);
	TerminalSpec.Policies.WildFleeMode = EBattleWildFleeMode::Chance;
	TerminalSpec.Policies.WildFleeNumerator = 1;
	TerminalSpec.Policies.WildFleeDenominator = 2;
	FBattleSetup TerminalSetup;
	if (!TryBuildSetup(Fixture.Catalog, TerminalSpec, TerminalSetup, Error)) { AddError(Error); return false; }
	const FChoiceProvider TerminalProvider = [](const FBattleDecisionRequest& Request, FChoice& Choice, FString&)
	{
		if (Request.GetActingBattlerId() == MakeNumericId<FBattlerId>(21)) Choice.Kind = EChoiceKind::WildFlee;
		else Choice = Fight(TEXT("Move.SwordsDance"));
		return true;
	};
	const FDriveFunction TerminalDrive = [TerminalProvider](FBattleEngine& Engine, FRunEvidence& Evidence, FString& OutError)
	{
		return LockTurn(Engine, TerminalProvider, Evidence, OutError) && Finish(Engine, Evidence, OutError);
	};
	FRunEvidence Terminal;
	const bool bTerminalTwins = RunStrictTwins(*this, TEXT("last wild configured flee"),
		Fixture.Catalog, TerminalSetup, {{0, 1, 0, FleePurpose}}, TerminalDrive, &Terminal);
	const FBattleSnapshot& Final = Terminal.Replay.GetFinalSnapshot();
	const FBattleEvent* Attempt = FindEvent(Terminal.Replay, EBattleEventType::RunAttempted);
	const FBattleEvent* Escaped = FindEvent(Terminal.Replay, EBattleEventType::Escaped);
	const FBattleEvent* Removed = FindEvent(Terminal.Replay, EBattleEventType::Removed);
	const FBattleEvent* Completed = FindEvent(Terminal.Replay, EBattleEventType::ActionCompleted);
	const FBattleEvent* Ended = FindEvent(Terminal.Replay, EBattleEventType::BattleEnded);
	TestTrue(TEXT("Last wild flee publishes exact terminal outcome, causal order, and no queued player PP use"),
		Final.GetPhase() == EBattlePhase::Terminal
		&& Final.GetOutcome() == EBattleOutcome::Escape
		&& Final.GetOutcomeCause() == EBattleOutcomeCause::OpponentFled
		&& Attempt != nullptr && Escaped != nullptr && Removed != nullptr && Completed != nullptr && Ended != nullptr
		&& Attempt->GetEventOrdinal() < Escaped->GetEventOrdinal()
		&& Escaped->GetEventOrdinal() < Removed->GetEventOrdinal()
		&& Removed->GetEventOrdinal() < Completed->GetEventOrdinal()
		&& Completed->GetEventOrdinal() < Ended->GetEventOrdinal()
		&& CountSourceEvents(Terminal.Replay, EBattleEventType::MoveUsed, 11) == 0
		&& CountSourceEvents(Terminal.Replay, EBattleEventType::PPConsumed, 11) == 0
		&& MovePP(Final, 11, TEXT("Move.SwordsDance"))
			== TerminalSetup.FindBattler(MakeNumericId<FBattlerId>(11))->Moves[0].CurrentPP);

	return bDisabledTwins && bFailedTwins && bSucceededTwins && bTerminalTwins
		&& ValidateGlobalInvariants(*this, Fixture.Catalog, DisabledEvidence, TEXT("disabled WildFlee"))
		&& ValidateGlobalInvariants(*this, Fixture.Catalog, Failed, TEXT("failed WildFlee"))
		&& ValidateGlobalInvariants(*this, Fixture.Catalog, Succeeded, TEXT("nonterminal WildFlee"))
		&& ValidateGlobalInvariants(*this, Fixture.Catalog, Terminal, TEXT("terminal WildFlee"));
}

#endif // WITH_DEV_AUTOMATION_TESTS
