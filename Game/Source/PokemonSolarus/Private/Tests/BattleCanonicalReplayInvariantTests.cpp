#if WITH_DEV_AUTOMATION_TESTS

#include "BattleCanonicalIntegrationTestSupport.h"

#include "Misc/AutomationTest.h"

namespace BattleCanonicalReplayInvariantPrivate
{
using namespace BattleCanonicalIntegrationTestSupport;

FSetupSpec MakeSpec(const uint64 BattleValue, const int32 OpponentHP = INDEX_NONE, const bool bOpponentReserve = false)
{
	FSetupSpec Spec;
	Spec.BattleValue = BattleValue;
	Spec.Format = EBattleFormat::Single;
	Spec.Policies.bBagAllowed = true;
	Spec.Policies.bShiftPromptEligible = false;
	Spec.Trainers = {{1, EBattleSide::Player, EBattleTrainerRole::Player, EBattleDecisionController::Human, {{TEXT("Item.HyperPotion"), 1}}},
		{2, EBattleSide::Opponent, EBattleTrainerRole::Opponent, EBattleDecisionController::EnemyAI, {}}};
	FBattlerSpec Player{1, 11, 0, TEXT("Species.Charizard"), TEXT("Nature.Jolly"), TEXT("Ability.Blaze"),
		{TEXT("Move.Flamethrower"), TEXT("Move.SwordsDance"), TEXT("Move.Protect")}};
	Player.EffortValues.Speed = 252;
	Player.OriginalHeldItemId = TEXT("Item.LifeOrb");
	Player.CurrentHeldItemId = TEXT("Item.LifeOrb");
	FBattlerSpec Opponent{2, 21, 0, TEXT("Species.Venusaur"), TEXT("Nature.Hardy"), TEXT("Ability.Overgrow"),
		{TEXT("Move.SwordsDance"), TEXT("Move.Protect")}};
	Opponent.CurrentHP = OpponentHP;
	Opponent.OriginalHeldItemId = TEXT("Item.Leftovers");
	Opponent.CurrentHeldItemId = TEXT("Item.Leftovers");
	Spec.Battlers = {Player, Opponent};
	Spec.Active = {{EBattleSide::Player, EBattlePosition::Left, 1, 11}, {EBattleSide::Opponent, EBattlePosition::Left, 2, 21}};
	if (bOpponentReserve)
	{
		Spec.Battlers.Add({2, 22, 1, TEXT("Species.Pelipper"), TEXT("Nature.Modest"), TEXT("Ability.Drizzle"), {TEXT("Move.RainDance")}});
	}
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

FChoice Fight(const TCHAR* Move, EBattleSide Side = EBattleSide::Opponent)
{
	FChoice Choice; Choice.DefinitionId = FName(Move); Choice.ActiveTarget = MakeActiveSlotId(Side, EBattlePosition::Left); return Choice;
}

bool Finish(FBattleEngine& Engine, FRunEvidence& Evidence, FString& Error)
{
	if (!ExecuteLockedQueue(Engine, Evidence, Error)) return false;
	while (!Engine.GetPendingDecisionRequests().IsEmpty())
	{
		const FChoiceProvider Replacement = [](const FBattleDecisionRequest& Request, FChoice& Choice, FString& OutError)
		{
			const TConstArrayView<FPartySlotId> LegalSwitches = Request.GetLegalSwitchPartySlots();
			if (LegalSwitches.IsEmpty())
			{
				OutError = FString::Printf(TEXT("Mandatory replacement for trainer %llu has no legal switch slot."), Request.GetDecisionOwnerTrainerId().GetValue());
				return false;
			}
			Choice.Kind = EChoiceKind::Replacement; Choice.PartyTarget = LegalSwitches[0]; return true;
		};
		if (!SubmitPendingChoices(Engine, Replacement, Evidence, Error) || !ExecuteLockedQueue(Engine, Evidence, Error)) return false;
	}
	return Engine.GetSnapshot().GetPhase() == EBattlePhase::EndOfTurn ? ResolveEndTurn(Engine, Evidence, Error) : true;
}

FChoiceProvider StandardProvider(const TCHAR* PlayerMove = TEXT("Move.Flamethrower"))
{
	return [PlayerMove](const FBattleDecisionRequest& Request, FChoice& Choice, FString&)
	{
		Choice = Request.GetDecisionOwnerTrainerId().GetValue() == 1 ? Fight(PlayerMove) : Fight(TEXT("Move.SwordsDance"), EBattleSide::Player);
		return true;
	};
}

FSetupSpec MakeWildDoubleSpec(const uint64 BattleValue)
{
	FSetupSpec Spec;
	Spec.BattleValue = BattleValue;
	Spec.EncounterKind = EBattleEncounterKind::Wild;
	Spec.Format = EBattleFormat::Double;
	Spec.Policies.bBagAllowed = true;
	Spec.Policies.bRunAllowed = true;
	Spec.Policies.bCaptureAllowed = true;
	Spec.Policies.bShiftPromptEligible = false;
	Spec.Trainers = {{1, EBattleSide::Player, EBattleTrainerRole::Player, EBattleDecisionController::Human, {{TEXT("Item.PokeBall"), 1}}},
		{2, EBattleSide::Opponent, EBattleTrainerRole::Opponent, EBattleDecisionController::EnemyAI, {}}};
	Spec.Battlers = {
		{1, 11, 0, TEXT("Species.Clefable"), TEXT("Nature.Hardy"), TEXT("Ability.MagicGuard"), {TEXT("Move.Protect")}},
		{1, 12, 1, TEXT("Species.Charizard"), TEXT("Nature.Jolly"), TEXT("Ability.Blaze"), {TEXT("Move.Flamethrower")}},
		{2, 21, 0, TEXT("Species.Venusaur"), TEXT("Nature.Hardy"), TEXT("Ability.Overgrow"), {TEXT("Move.SwordsDance")}},
		{2, 22, 1, TEXT("Species.Espathra"), TEXT("Nature.Hardy"), TEXT("Ability.SpeedBoost"), {TEXT("Move.SwordsDance")}}};
	Spec.Active = {{EBattleSide::Player, EBattlePosition::Left, 1, 11}, {EBattleSide::Player, EBattlePosition::Right, 1, 12},
		{EBattleSide::Opponent, EBattlePosition::Left, 2, 21}, {EBattleSide::Opponent, EBattlePosition::Right, 2, 22}};
	Spec.CaptureCapacity = {1, 1};
	Spec.CaptureProgression.bHasSnapshot = true;
	return Spec;
}

const FBattleEvent* FindSourceEvent(
	const FBattleReplayRecord& Record,
	const EBattleEventType Type,
	const uint64 BattlerValue)
{
	for (const FBattleResolution& Resolution : Record.GetResolutions())
		for (const FBattleEvent& Event : Resolution.GetEvents())
			if (Event.GetType() == Type
				&& Event.GetSource().BattlerId == MakeNumericId<FBattlerId>(BattlerValue)) return &Event;
	return nullptr;
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

bool RejectDecisionWithoutMutation(
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
		OutError = TEXT("Rejected replay decision changed gameplay state, RNG, requests, resources, or its exact audit delta.");
		return false;
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FC11AReplayTwins, "PokemonSolarus.Battle.C11A.ReplayInvariants.Twins.EventsRngSnapshotsOutcomeCaptureItemsAndBytes", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FC11AReplayTwins::RunTest(const FString& Parameters)
{
	FCatalogFixture Fixture; FBattleSetup Setup;
	if (!Build(*this, MakeSpec(11701), Fixture, Setup)) return false;
	const FChoiceProvider Provider = StandardProvider();
	const FDriveFunction Drive = [Provider](FBattleEngine& Engine, FRunEvidence& Evidence, FString& Error)
	{
		return LockTurn(Engine, Provider, Evidence, Error) && Finish(Engine, Evidence, Error);
	};
	FRunEvidence Evidence;
	const bool bTwins = RunDeterministicTwins(*this, TEXT("global twin replay"), Fixture.Catalog, Setup, Drive, &Evidence);
	TestTrue(TEXT("Twin scenario records decisions, RNG, snapshots, and typed resolutions"), !Evidence.Replay.GetInputs().Decisions.IsEmpty() && !Evidence.Replay.GetRandomTrace().IsEmpty() && !Evidence.Checkpoints.IsEmpty() && !Evidence.Replay.GetResolutions().IsEmpty());
	TestTrue(TEXT("Twin canonical bytes are non-empty"), !Evidence.ReplayBytes.IsEmpty());
	TestTrue(TEXT("Public held-item facts are represented by typed events"), HasEvent(Evidence.Replay, EBattleEventType::ItemActivated));

	FSetupSpec ItemSpec = MakeSpec(11706);
	FBattlerSpec* ItemPlayer = ItemSpec.Battlers.FindByPredicate(
		[](const FBattlerSpec& Battler) { return Battler.BattlerValue == 11; });
	if (ItemPlayer == nullptr) { AddError(TEXT("Item replay fixture is missing player battler 11.")); return false; }
	ItemPlayer->CurrentHP = 1;
	FBattleSetup ItemSetup; FString Error;
	if (!TryBuildSetup(Fixture.Catalog, ItemSpec, ItemSetup, Error)) { AddError(Error); return false; }
	const FChoiceProvider ItemProvider = [](const FBattleDecisionRequest& Request, FChoice& Choice, FString&)
	{
		if (Request.GetDecisionOwnerTrainerId() == MakeNumericId<FTrainerId>(1))
		{
			Choice.Kind = EChoiceKind::Bag; Choice.DefinitionId = TEXT("Item.HyperPotion");
			Choice.PartyTarget = MakePartySlotId(0);
		}
		else Choice = Fight(TEXT("Move.SwordsDance"), EBattleSide::Player);
		return true;
	};
	const FDriveFunction ItemDrive = [ItemProvider](FBattleEngine& Engine, FRunEvidence& ItemEvidence, FString& OutError)
	{
		return LockTurn(Engine, ItemProvider, ItemEvidence, OutError) && Finish(Engine, ItemEvidence, OutError);
	};
	FRunEvidence ItemEvidence;
	const bool bItemTwins = RunDeterministicTwins(*this, TEXT("representative Bag-item twin replay"),
		Fixture.Catalog, ItemSetup, ItemDrive, &ItemEvidence);
	TestTrue(TEXT("Bag-item twins record exact consumption, healing, resources, and bytes"),
		CountEvents(ItemEvidence.Replay, EBattleEventType::ItemUsed) == 1
		&& CountEvents(ItemEvidence.Replay, EBattleEventType::ItemConsumed) == 1
		&& CountEvents(ItemEvidence.Replay, EBattleEventType::Healing) == 1
		&& BagCount(ItemEvidence.Replay.GetFinalSnapshot(), 1, TEXT("Item.HyperPotion")) == 0
		&& !ItemEvidence.ReplayBytes.IsEmpty());

	FSetupSpec CaptureSpec = MakeWildDoubleSpec(11707);
	CaptureSpec.CaptureProgression.bMustCapture = true;
	FBattleSetup CaptureSetup;
	if (!TryBuildSetup(Fixture.Catalog, CaptureSpec, CaptureSetup, Error)) { AddError(Error); return false; }
	const FChoiceProvider CaptureProvider = [](const FBattleDecisionRequest& Request, FChoice& Choice, FString&)
	{
		const uint64 Actor = Request.GetActingBattlerId().GetValue();
		if (Actor == 11)
		{
			Choice.Kind = EChoiceKind::Bag; Choice.DefinitionId = TEXT("Item.PokeBall");
			Choice.ActiveTarget = MakeActiveSlotId(EBattleSide::Opponent, EBattlePosition::Left);
		}
		else if (Actor == 12) Choice = Fight(TEXT("Move.Flamethrower"));
		else Choice = Fight(TEXT("Move.SwordsDance"), EBattleSide::Player);
		return true;
	};
	const FDriveFunction CaptureDrive = [CaptureProvider](FBattleEngine& Engine, FRunEvidence& CaptureEvidence, FString& OutError)
	{
		return LockTurn(Engine, CaptureProvider, CaptureEvidence, OutError)
			&& Finish(Engine, CaptureEvidence, OutError);
	};
	FRunEvidence CaptureEvidence;
	const bool bCaptureTwins = RunDeterministicTwins(*this, TEXT("representative capture twin replay"),
		Fixture.Catalog, CaptureSetup, CaptureDrive, &CaptureEvidence);
	const FBattleSnapshot& CaptureFinal = CaptureEvidence.Replay.GetFinalSnapshot();
	TestTrue(TEXT("Capture twins record exact pending capture and cancel captured actor and target-dependent action"),
		CountEvents(CaptureEvidence.Replay, EBattleEventType::Captured) == 1
		&& CountEvents(CaptureEvidence.Replay, EBattleEventType::ItemConsumed) == 1
		&& CaptureFinal.GetPendingCaptures().Num() == 1
		&& CaptureFinal.GetPendingCaptures()[0].BattlerId == MakeNumericId<FBattlerId>(21)
		&& CaptureFinal.GetOutcome() == EBattleOutcome::InProgress
		&& CountSourceEvents(CaptureEvidence.Replay, EBattleEventType::ActionCanceled, 12) == 1
		&& CountSourceEvents(CaptureEvidence.Replay, EBattleEventType::ActionCanceled, 21) == 1
		&& CountSourceEvents(CaptureEvidence.Replay, EBattleEventType::MoveUsed, 12) == 0
		&& CountSourceEvents(CaptureEvidence.Replay, EBattleEventType::MoveUsed, 21) == 0
		&& MovePP(CaptureFinal, 12, TEXT("Move.Flamethrower"))
			== CaptureSetup.FindBattler(MakeNumericId<FBattlerId>(12))->Moves[0].CurrentPP
		&& MovePP(CaptureFinal, 21, TEXT("Move.SwordsDance"))
			== CaptureSetup.FindBattler(MakeNumericId<FBattlerId>(21))->Moves[0].CurrentPP
		&& !CaptureEvidence.ReplayBytes.IsEmpty());
	TestTrue(TEXT("Move, Bag-item, and capture histories have distinct canonical bytes"),
		Evidence.ReplayBytes != ItemEvidence.ReplayBytes
		&& Evidence.ReplayBytes != CaptureEvidence.ReplayBytes
		&& ItemEvidence.ReplayBytes != CaptureEvidence.ReplayBytes);
	return bTwins && bItemTwins && bCaptureTwins
		&& ValidateGlobalInvariants(*this, Fixture.Catalog, Evidence, TEXT("global twins"))
		&& ValidateGlobalInvariants(*this, Fixture.Catalog, ItemEvidence, TEXT("Bag-item twins"))
		&& ValidateGlobalInvariants(*this, Fixture.Catalog, CaptureEvidence, TEXT("capture twins"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FC11AReplayStatRefresh, "PokemonSolarus.Battle.C11A.ReplayInvariants.ExternalStatRefreshSequenceAndReplayEquality", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FC11AReplayStatRefresh::RunTest(const FString& Parameters)
{
	FCatalogFixture Fixture; FBattleSetup Setup;
	if (!Build(*this, MakeSpec(11702, 1, true), Fixture, Setup)) return false;
	const FBattleSpeciesFormDefinition* Species = Fixture.Catalog.FindSpeciesForm(MakeDefinitionId<FSpeciesFormId>(TEXT("Species.Charizard")));
	const FBattleNatureDefinition* Nature = Fixture.Catalog.FindNature(MakeDefinitionId<FNatureId>(TEXT("Nature.Jolly")));
	if (Species == nullptr || Nature == nullptr) { AddError(TEXT("Canonical refresh inputs are missing.")); return false; }
	FPokemonStatInputs Inputs; Inputs.Level = 51; Inputs.BaseStats = Species->BaseStats; Inputs.IndividualValues = {31, 31, 31, 31, 31, 31}; Inputs.EffortValues.Speed = 252; Inputs.NatureModifier = Nature->Modifier;
	FPokemonBattleStats RefreshedStats; EBattleStatCalculationError StatError = EBattleStatCalculationError::None;
	if (!FBattleStatCalculator::TryCalculatePermanentStats(Inputs, RefreshedStats, StatError)) { AddError(TEXT("Canonical level-51 refresh calculation failed.")); return false; }
	const FChoiceProvider Provider = StandardProvider();
	const FDriveFunction Drive = [Provider, RefreshedStats](FBattleEngine& Engine, FRunEvidence& Evidence, FString& Error)
	{
		if (!LockTurn(Engine, Provider, Evidence, Error)) return false;
		const FBattleResolution Begun = Engine.BeginNextLockedAction();
		if (!Begun.WasAccepted()) { Error = TEXT("Refresh scenario could not start the first action."); return false; }
		RecordCheckpoint(Engine, Evidence, TEXT("refresh-action-start"));
		const FBattleResolution Committed = Engine.CommitCurrentMoveAfterPreMoveGates();
		const FBattleResolution Targeted = Committed.WasAccepted() ? Engine.ResolveCurrentMoveTargets() : FBattleResolution();
		const FBattleResolution Effects = Targeted.WasAccepted() ? Engine.ExecuteCurrentMoveEffects() : FBattleResolution();
		if (!Committed.WasAccepted() || !Targeted.WasAccepted() || !Effects.WasAccepted()) { Error = TEXT("Refresh scenario move failed before the checkpoint."); return false; }
		const FBattleEvent* Checkpoint = Effects.GetEvents().FindByPredicate([](const FBattleEvent& Event) { return Event.GetType() == EBattleEventType::OpponentRemovalCheckpoint; });
		if (Checkpoint == nullptr) { Error = TEXT("Opponent removal did not publish the public refresh checkpoint."); return false; }
		FBattleBetweenActionsStatRefresh Refresh;
		Refresh.StateVersion = Engine.GetSnapshot().GetStateVersion();
		Refresh.OpponentRemovalCheckpointEventOrdinal = Checkpoint->GetEventOrdinal();
		Refresh.BattlerId = MakeNumericId<FBattlerId>(11);
		Refresh.NewLevel = 51;
		Refresh.NewStats = RefreshedStats;
		const FBattlePartyEntrySetup* Current = Engine.GetSnapshot().FindBattler(Refresh.BattlerId);
		Refresh.NewCurrentHP = Current == nullptr ? 1 : FMath::Min(Current->CurrentHP, RefreshedStats.MaxHP);
		if (!Engine.ApplyBetweenActionsStatRefresh(Refresh).WasAccepted()) { Error = TEXT("Public stat refresh was rejected at its exact checkpoint."); return false; }
		RecordCheckpoint(Engine, Evidence, TEXT("stat-refresh"));
		return Finish(Engine, Evidence, Error);
	};
	FRunEvidence Evidence;
	const bool bTwins = RunDeterministicTwins(*this, TEXT("external stat refresh"), Fixture.Catalog, Setup, Drive, &Evidence);
	TestEqual(TEXT("Refresh is applied exactly once"), CountEvents(Evidence.Replay, EBattleEventType::StatRefreshApplied), 1);
	TestEqual(TEXT("Replay records one external refresh"), Evidence.Replay.GetInputs().StatRefreshes.Num(), 1);
	return bTwins
		&& ValidateGlobalInvariants(*this, Fixture.Catalog, Evidence, TEXT("external stat refresh"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FC11AReplayBounds, "PokemonSolarus.Battle.C11A.ReplayInvariants.BoundsPermanentStatsAndActionExactlyOnce", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FC11AReplayBounds::RunTest(const FString& Parameters)
{
	FCatalogFixture Fixture; FBattleSetup Setup;
	if (!Build(*this, MakeSpec(11703), Fixture, Setup)) return false;
	const FPokemonBattleStats InitialStats = Setup.GetPartyEntries()[0].Stats;
	const FChoiceProvider Provider = StandardProvider(TEXT("Move.SwordsDance"));
	const FDriveFunction Drive = [Provider](FBattleEngine& Engine, FRunEvidence& Evidence, FString& Error)
	{
		for (int32 Turn = 0; Turn < 8; ++Turn)
		{
			if (!LockTurn(Engine, Provider, Evidence, Error) || !Finish(Engine, Evidence, Error)) return false;
		}
		return true;
	};
	FRunEvidence Evidence;
	const bool bTwins = RunDeterministicTwins(*this, TEXT("bounds and exact-once"), Fixture.Catalog, Setup, Drive, &Evidence);
	const FBattlePartyEntrySetup* FinalPlayer = Evidence.Replay.GetFinalSnapshot().FindBattler(MakeNumericId<FBattlerId>(11));
	TestTrue(TEXT("Transient stages do not mutate permanent calculated stats"), FinalPlayer != nullptr
		&& FinalPlayer->Stats.MaxHP == InitialStats.MaxHP && FinalPlayer->Stats.Attack == InitialStats.Attack
		&& FinalPlayer->Stats.Defense == InitialStats.Defense && FinalPlayer->Stats.SpecialAttack == InitialStats.SpecialAttack
		&& FinalPlayer->Stats.SpecialDefense == InitialStats.SpecialDefense && FinalPlayer->Stats.Speed == InitialStats.Speed);
	return bTwins && ValidateGlobalInvariants(*this, Fixture.Catalog, Evidence, TEXT("bounds"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FC11AReplayInvalidTerminal, "PokemonSolarus.Battle.C11A.ReplayInvariants.InvalidStaleCanceledRemovedAndTerminalImmutability", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FC11AReplayInvalidTerminal::RunTest(const FString& Parameters)
{
	FCatalogFixture Fixture; FBattleSetup Setup;
	if (!Build(*this, MakeSpec(11704, 1, false), Fixture, Setup)) return false;
	TUniquePtr<FBattleEngine> Engine; FBattleRejection Rejection;
	if (!FBattleEngine::TryCreate(Setup, Fixture.Catalog, MakeUnique<FSeededBattleRandom>(11704), Engine, Rejection) || !Engine->TryBeginActionDecisionSequence(Rejection)) { AddError(TEXT("Terminal probe could not begin.")); return false; }
	const FBattleDecisionRequest PlayerRequest = Engine->GetPendingDecisionRequests()[0];
	FBattleDecision StaleDecision; FString Error;
	if (!TryMakeDecision(PlayerRequest, Fight(TEXT("Move.Flamethrower")), StaleDecision, Error) || !Engine->SubmitDecision(StaleDecision).WasAccepted()) { AddError(TEXT("Terminal probe player decision failed.")); return false; }
	if (!RejectDecisionWithoutMutation(*Engine, StaleDecision, EBattleRejectionReason::StaleStateVersion, Error))
	{
		AddError(TEXT("Preterminal stale decision probe failed: ") + Error);
		return false;
	}
	const FBattleDecisionRequest OpponentRequest = Engine->GetPendingDecisionRequests()[0];
	FBattleDecision OpponentDecision;
	if (!TryMakeDecision(OpponentRequest, Fight(TEXT("Move.SwordsDance"), EBattleSide::Player), OpponentDecision, Error) || !Engine->SubmitDecision(OpponentDecision).WasAccepted()) { AddError(TEXT("Terminal probe opponent decision failed.")); return false; }
	FRunEvidence Evidence;
	if (!ExecuteLockedQueue(*Engine, Evidence, Error)) { AddError(Error); return false; }
	TestEqual(TEXT("Lethal scenario is terminal"), Engine->GetSnapshot().GetPhase(), EBattlePhase::Terminal);
	const FString Before = SnapshotSignature(Engine->GetSnapshot());
	const TArray<FBattleRandomDraw> DrawsBefore = Engine->ExportRandomTrace();
	const FBattleReplayRecord ReplayBefore = Engine->ExportReplayRecord();
	TArray<uint8> BytesBefore; TArray<uint8> BytesAfterRejection; TArray<uint8> BytesAfterResolvers; TArray<uint8> BytesAfterBegin;
	const bool bSerializedBefore = FBattleReplaySerializer::TrySerializeCanonical(ReplayBefore, BytesBefore, Rejection);
	const FBattleResolution TerminalRejected = Engine->SubmitDecision(StaleDecision);
	const FBattleReplayRecord ReplayAfterRejection = Engine->ExportReplayRecord();
	TestFalse(TEXT("Stale decision is rejected after terminal"), TerminalRejected.WasAccepted());
	TestEqual(TEXT("Terminal decision has the exact rejection reason"), TerminalRejected.GetRejection().Reason, EBattleRejectionReason::TerminalState);
	TestEqual(TEXT("Terminal rejection does not advance state version"), TerminalRejected.GetAfterStateVersion(), TerminalRejected.GetBeforeStateVersion());
	TestEqual(TEXT("Terminal rejection publishes exactly one audit event"), TerminalRejected.GetEvents().Num(), 1);
	if (TerminalRejected.GetEvents().Num() == 1)
	{
		TestEqual(TEXT("Terminal rejection publishes DecisionRejected"), TerminalRejected.GetEvents()[0].GetType(), EBattleEventType::DecisionRejected);
	}
	TestEqual(TEXT("Submitted terminal decision is recorded exactly once"), ReplayAfterRejection.GetInputs().Decisions.Num(), ReplayBefore.GetInputs().Decisions.Num() + 1);
	TestEqual(TEXT("Terminal rejection resolution is recorded exactly once"), ReplayAfterRejection.GetResolutions().Num(), ReplayBefore.GetResolutions().Num() + 1);
	if (ReplayAfterRejection.GetInputs().Decisions.Num() == ReplayBefore.GetInputs().Decisions.Num() + 1)
	{
		const FBattleDecision& Recorded = ReplayAfterRejection.GetInputs().Decisions.Last();
		TestTrue(TEXT("Replay records the exact rejected terminal decision"), Recorded.IsValid()
			&& Recorded.GetStateVersion() == StaleDecision.GetStateVersion()
			&& Recorded.GetRequestKind() == StaleDecision.GetRequestKind()
			&& Recorded.GetDecisionOwnerTrainerId() == StaleDecision.GetDecisionOwnerTrainerId()
			&& Recorded.GetActingBattlerId() == StaleDecision.GetActingBattlerId()
			&& Recorded.GetActionKind() == StaleDecision.GetActionKind()
			&& Recorded.GetMoveId() == StaleDecision.GetMoveId()
			&& Recorded.GetActiveTargetId() == StaleDecision.GetActiveTargetId());
	}
	const FBattleResolution TerminalActionRejected = Engine->BeginNextLockedAction();
	const FBattleReplayRecord ReplayAfterAction = Engine->ExportReplayRecord();
	const FBattleResolution TerminalEndRejected = Engine->ResolveEndTurn();
	const FBattleReplayRecord ReplayAfterResolvers = Engine->ExportReplayRecord();
	TestTrue(TEXT("Terminal action and end-turn resolvers reject with exact immutable audit facts"),
		!TerminalActionRejected.WasAccepted()
		&& TerminalActionRejected.GetRejection().Reason == EBattleRejectionReason::TerminalState
		&& TerminalActionRejected.GetBeforeStateVersion() == TerminalActionRejected.GetAfterStateVersion()
		&& TerminalActionRejected.GetEvents().Num() == 1
		&& TerminalActionRejected.GetEvents()[0].GetType() == EBattleEventType::ActionCanceled
		&& !TerminalEndRejected.WasAccepted()
		&& TerminalEndRejected.GetRejection().Reason == EBattleRejectionReason::TerminalState
		&& TerminalEndRejected.GetBeforeStateVersion() == TerminalEndRejected.GetAfterStateVersion()
		&& TerminalEndRejected.GetEvents().Num() == 1
		&& TerminalEndRejected.GetEvents()[0].GetType() == EBattleEventType::ActionCanceled
		&& ReplayAfterAction.GetInputs().Decisions.Num() == ReplayAfterRejection.GetInputs().Decisions.Num()
		&& ReplayAfterAction.GetResolutions().Num() == ReplayAfterRejection.GetResolutions().Num() + 1
		&& ReplayAfterResolvers.GetInputs().Decisions.Num() == ReplayAfterAction.GetInputs().Decisions.Num()
		&& ReplayAfterResolvers.GetResolutions().Num() == ReplayAfterAction.GetResolutions().Num() + 1);
	FBattleRejection BeginRejection;
	TestFalse(TEXT("Terminal state cannot begin another selection"), Engine->TryBeginActionDecisionSequence(BeginRejection));
	TestEqual(TEXT("Terminal begin failure has the exact rejection reason"), BeginRejection.Reason, EBattleRejectionReason::TerminalState);
	const FBattleReplayRecord ReplayAfterBegin = Engine->ExportReplayRecord();
	TestEqual(TEXT("Failed terminal begin adds no replay input"), ReplayAfterBegin.GetInputs().Decisions.Num(), ReplayAfterResolvers.GetInputs().Decisions.Num());
	TestEqual(TEXT("Failed terminal begin adds no resolution"), ReplayAfterBegin.GetResolutions().Num(), ReplayAfterResolvers.GetResolutions().Num());
	const bool bSerializedAfterRejection = FBattleReplaySerializer::TrySerializeCanonical(ReplayAfterRejection, BytesAfterRejection, Rejection);
	const bool bSerializedAfterResolvers = FBattleReplaySerializer::TrySerializeCanonical(ReplayAfterResolvers, BytesAfterResolvers, Rejection);
	const bool bSerializedAfterBegin = FBattleReplaySerializer::TrySerializeCanonical(ReplayAfterBegin, BytesAfterBegin, Rejection);
	TestTrue(TEXT("Terminal replay serializes before and after rejected calls"), bSerializedBefore && bSerializedAfterRejection && bSerializedAfterResolvers && bSerializedAfterBegin);
	TestEqual(TEXT("Terminal rejection preserves snapshot"), SnapshotSignature(Engine->GetSnapshot()), Before);
	TestTrue(TEXT("Terminal rejection preserves RNG"), Engine->ExportRandomTrace() == DrawsBefore);
	TestTrue(TEXT("Recorded terminal rejection changes canonical replay bytes"), BytesAfterRejection != BytesBefore);
	TestTrue(TEXT("Recorded terminal resolver audits change replay bytes without gameplay mutation"), BytesAfterResolvers != BytesAfterRejection);
	TestTrue(TEXT("Failed terminal begin preserves canonical replay bytes"), BytesAfterBegin == BytesAfterResolvers);
	FRunEvidence TerminalEvidence = MoveTemp(Evidence);
	if (!FinalizeEvidence(*Engine, TerminalEvidence, Error)) { AddError(Error); return false; }

	FSetupSpec FaintedSpec = MakeSpec(11708, 1, true);
	FBattleSetup FaintedSetup;
	if (!TryBuildSetup(Fixture.Catalog, FaintedSpec, FaintedSetup, Error)) { AddError(Error); return false; }
	const FChoiceProvider FaintedProvider = StandardProvider();
	const FDriveFunction FaintedDrive = [FaintedProvider](FBattleEngine& FaintedEngine, FRunEvidence& FaintedEvidence, FString& OutError)
	{
		return LockTurn(FaintedEngine, FaintedProvider, FaintedEvidence, OutError)
			&& Finish(FaintedEngine, FaintedEvidence, OutError);
	};
	FRunEvidence FaintedEvidence;
	const bool bFaintedTwins = RunDeterministicTwins(*this, TEXT("fainted queued actor cancellation"),
		Fixture.Catalog, FaintedSetup, FaintedDrive, &FaintedEvidence);
	const FBattleSnapshot& FaintedFinal = FaintedEvidence.Replay.GetFinalSnapshot();
	const FBattlePartyEntrySetup* FaintedActor = FaintedFinal.FindBattler(MakeNumericId<FBattlerId>(21));
	TestTrue(TEXT("A fainted queued actor cannot start, use its move, or spend PP before replacement"),
		FaintedActor != nullptr && FaintedActor->CurrentHP == 0
		&& CountSourceEvents(FaintedEvidence.Replay, EBattleEventType::ActionCanceled, 21) == 1
		&& CountSourceEvents(FaintedEvidence.Replay, EBattleEventType::MoveUsed, 21) == 0
		&& CountSourceEvents(FaintedEvidence.Replay, EBattleEventType::PPConsumed, 21) == 0
		&& MovePP(FaintedFinal, 21, TEXT("Move.SwordsDance"))
			== FaintedSetup.FindBattler(MakeNumericId<FBattlerId>(21))->Moves[0].CurrentPP
		&& ActiveBattler(FaintedFinal, EBattleSide::Opponent, EBattlePosition::Left)
			== MakeNumericId<FBattlerId>(22));

	FSetupSpec RemovedSpec = MakeWildDoubleSpec(11709);
	RemovedSpec.Policies.WildFleeMode = EBattleWildFleeMode::Always;
	FBattleSetup RemovedSetup;
	if (!TryBuildSetup(Fixture.Catalog, RemovedSpec, RemovedSetup, Error)) { AddError(Error); return false; }
	const FChoiceProvider RemovedProvider = [](const FBattleDecisionRequest& Request, FChoice& Choice, FString&)
	{
		const uint64 Actor = Request.GetActingBattlerId().GetValue();
		if (Actor == 11) Choice = Fight(TEXT("Move.Protect"), EBattleSide::Player);
		else if (Actor == 12) Choice = Fight(TEXT("Move.Flamethrower"));
		else if (Actor == 21) Choice.Kind = EChoiceKind::WildFlee;
		else Choice = Fight(TEXT("Move.SwordsDance"), EBattleSide::Player);
		return true;
	};
	const FDriveFunction RemovedDrive = [RemovedProvider](FBattleEngine& RemovedEngine, FRunEvidence& RemovedEvidence, FString& OutError)
	{
		return LockTurn(RemovedEngine, RemovedProvider, RemovedEvidence, OutError)
			&& Finish(RemovedEngine, RemovedEvidence, OutError);
	};
	FRunEvidence RemovedEvidence;
	const bool bRemovedTwins = RunDeterministicTwins(*this, TEXT("removed selected-target cancellation"),
		Fixture.Catalog, RemovedSetup, RemovedDrive, &RemovedEvidence);
	const FBattleSnapshot& RemovedFinal = RemovedEvidence.Replay.GetFinalSnapshot();
	const FBattleEvent* LostTargets = FindSourceEvent(RemovedEvidence.Replay, EBattleEventType::TargetsResolved, 12);
	TestTrue(TEXT("A removed selected target is not retargeted and the committed move cancels after PP"),
		CountEvents(RemovedEvidence.Replay, EBattleEventType::Removed) == 1
		&& CountSourceEvents(RemovedEvidence.Replay, EBattleEventType::MoveUsed, 21) == 0
		&& CountSourceEvents(RemovedEvidence.Replay, EBattleEventType::PPConsumed, 21) == 0
		&& CountSourceEvents(RemovedEvidence.Replay, EBattleEventType::MoveUsed, 12) == 1
		&& CountSourceEvents(RemovedEvidence.Replay, EBattleEventType::PPConsumed, 12) == 1
		&& CountSourceEvents(RemovedEvidence.Replay, EBattleEventType::ActionCanceled, 12) == 1
		&& CountSourceEvents(RemovedEvidence.Replay, EBattleEventType::Damage, 12) == 0
		&& LostTargets != nullptr && LostTargets->GetTargets().IsEmpty()
		&& MovePP(RemovedFinal, 12, TEXT("Move.Flamethrower"))
			== RemovedSetup.FindBattler(MakeNumericId<FBattlerId>(12))->Moves[0].CurrentPP - 1
		&& !ActiveBattler(RemovedFinal, EBattleSide::Opponent, EBattlePosition::Left).IsValid()
		&& ActiveBattler(RemovedFinal, EBattleSide::Opponent, EBattlePosition::Right)
			== MakeNumericId<FBattlerId>(22)
		&& RemovedFinal.GetOutcome() == EBattleOutcome::InProgress);

	return bFaintedTwins && bRemovedTwins
		&& ValidateGlobalInvariants(*this, Fixture.Catalog, TerminalEvidence, TEXT("terminal immutability"))
		&& ValidateGlobalInvariants(*this, Fixture.Catalog, FaintedEvidence, TEXT("fainted actor cancellation"))
		&& ValidateGlobalInvariants(*this, Fixture.Catalog, RemovedEvidence, TEXT("removed target cancellation"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FC11AReplayObservers, "PokemonSolarus.Battle.C11A.ReplayInvariants.ObserverSnapshotsEventsAndHiddenInformation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FC11AReplayObservers::RunTest(const FString& Parameters)
{
	FCatalogFixture Fixture; FBattleSetup Setup;
	FSetupSpec ObserverSpec = MakeSpec(11705);
	FBattlerSpec* PlayerSpec = ObserverSpec.Battlers.FindByPredicate(
		[](const FBattlerSpec& Battler) { return Battler.BattlerValue == 11; });
	FBattlerSpec* OpponentSpec = ObserverSpec.Battlers.FindByPredicate(
		[](const FBattlerSpec& Battler) { return Battler.BattlerValue == 21; });
	if (PlayerSpec == nullptr || OpponentSpec == nullptr)
	{
		AddError(TEXT("Observer reveal fixture is missing a required battler."));
		return false;
	}
	PlayerSpec->CurrentHP = 40;
	OpponentSpec->SpeciesId = TEXT("Species.Charizard");
	OpponentSpec->AbilityId = TEXT("Ability.Blaze");
	if (!Build(*this, ObserverSpec, Fixture, Setup)) return false;
	TUniquePtr<FBattleEngine> Probe; FBattleRejection Rejection;
	if (!FBattleEngine::TryCreate(Setup, Fixture.Catalog, MakeUnique<FSeededBattleRandom>(11705), Probe, Rejection)) { AddError(TEXT("Observer probe could not be created.")); return false; }
	const FBattleSnapshot PlayerView = Probe->GetSnapshotForObserver(MakeNumericId<FTrainerId>(1));
	const FBattleSnapshot OpponentView = Probe->GetSnapshotForObserver(MakeNumericId<FTrainerId>(2));
	const FBattleObservedBattler* PlayerSeesOpponent = PlayerView.FindObservedBattler(MakeNumericId<FBattlerId>(21));
	const FBattleObservedBattler* OpponentSeesPlayer = OpponentView.FindObservedBattler(MakeNumericId<FBattlerId>(11));
	TestTrue(TEXT("Observer snapshots are explicitly filtered"), PlayerView.IsObserverFiltered() && OpponentView.IsObserverFiltered());
	TestTrue(TEXT("Unrevealed opponent Ability and item stay hidden"), PlayerSeesOpponent != nullptr && !PlayerSeesOpponent->bAbilityKnown && !PlayerSeesOpponent->bHeldItemKnown);
	TestTrue(TEXT("Reverse observer also cannot see hidden Ability and item"), OpponentSeesPlayer != nullptr && !OpponentSeesPlayer->bAbilityKnown && !OpponentSeesPlayer->bHeldItemKnown);
	const FChoiceProvider Provider = StandardProvider();
	const FDriveFunction Drive = [Provider](FBattleEngine& Engine, FRunEvidence& Evidence, FString& Error)
	{
		const FBattleSnapshot InitialPlayer = Engine.GetSnapshotForObserver(MakeNumericId<FTrainerId>(1));
		const FBattleSnapshot InitialOpponent = Engine.GetSnapshotForObserver(MakeNumericId<FTrainerId>(2));
		const FBattleObservedBattler* InitialOpponentBattler = InitialPlayer.FindObservedBattler(MakeNumericId<FBattlerId>(21));
		const FBattleObservedBattler* InitialPlayerBattler = InitialOpponent.FindObservedBattler(MakeNumericId<FBattlerId>(11));
		if (InitialOpponentBattler == nullptr || InitialPlayerBattler == nullptr
			|| InitialOpponentBattler->bAbilityKnown || InitialOpponentBattler->bHeldItemKnown
			|| InitialPlayerBattler->bAbilityKnown || InitialPlayerBattler->bHeldItemKnown
			|| !LockTurn(Engine, Provider, Evidence, Error)
			|| !ExecuteLockedQueue(Engine, Evidence, Error))
		{
			if (Error.IsEmpty()) Error = TEXT("Initial observer hiding or action execution failed.");
			return false;
		}
		const FBattleSnapshot MidPlayer = Engine.GetSnapshotForObserver(MakeNumericId<FTrainerId>(1));
		const FBattleSnapshot MidOpponent = Engine.GetSnapshotForObserver(MakeNumericId<FTrainerId>(2));
		const FBattleObservedBattler* MidOpponentBattler = MidPlayer.FindObservedBattler(MakeNumericId<FBattlerId>(21));
		const FBattleObservedBattler* MidPlayerBattler = MidOpponent.FindObservedBattler(MakeNumericId<FBattlerId>(11));
		if (Engine.GetSnapshot().GetPhase() != EBattlePhase::EndOfTurn
			|| MidOpponentBattler == nullptr || MidPlayerBattler == nullptr
			|| MidOpponentBattler->bAbilityKnown || MidOpponentBattler->bHeldItemKnown
			|| !MidPlayerBattler->bAbilityKnown
			|| MidPlayerBattler->AbilityId != MakeDefinitionId<FAbilityId>(TEXT("Ability.Blaze"))
			|| !MidPlayerBattler->bHeldItemKnown
			|| MidPlayerBattler->HeldItemId != MakeDefinitionId<FItemId>(TEXT("Item.LifeOrb")))
		{
			Error = TEXT("Before residuals, Blaze/Life Orb did not reveal while the unused opposing sources stayed hidden.");
			return false;
		}
		if (!ResolveEndTurn(Engine, Evidence, Error)) return false;
		const FBattleSnapshot FinalPlayer = Engine.GetSnapshotForObserver(MakeNumericId<FTrainerId>(1));
		const FBattleSnapshot FinalOpponent = Engine.GetSnapshotForObserver(MakeNumericId<FTrainerId>(2));
		const FBattleObservedBattler* FinalOpponentBattler = FinalPlayer.FindObservedBattler(MakeNumericId<FBattlerId>(21));
		const FBattleObservedBattler* FinalPlayerBattler = FinalOpponent.FindObservedBattler(MakeNumericId<FBattlerId>(11));
		if (FinalOpponentBattler == nullptr || FinalPlayerBattler == nullptr
			|| FinalOpponentBattler->bAbilityKnown
			|| !FinalOpponentBattler->bHeldItemKnown
			|| FinalOpponentBattler->HeldItemId != MakeDefinitionId<FItemId>(TEXT("Item.Leftovers"))
			|| !FinalPlayerBattler->bAbilityKnown || !FinalPlayerBattler->bHeldItemKnown)
		{
			Error = TEXT("Residual reveal did not expose Leftovers while unused opposing Blaze stayed hidden.");
			return false;
		}
		return true;
	};
	FRunEvidence Evidence;
	const bool bTwins = RunDeterministicTwins(*this, TEXT("observer-safe replay"), Fixture.Catalog, Setup, Drive, &Evidence);
	bool bVisibilityValid = true;
	for (const FBattleResolution& Resolution : Evidence.Replay.GetResolutions())
	{
		for (const FBattleEvent& Event : Resolution.GetEvents())
		{
			if (Event.GetVisibility().Level == EBattleVisibilityLevel::OwningTrainer) bVisibilityValid &= Event.GetVisibility().OwningTrainerId.IsValid();
		}
	}
	TestTrue(TEXT("Restricted events always name their owning observer"), bVisibilityValid);
	const FBattleEvent* PlayerAbility = FindSourceEvent(Evidence.Replay, EBattleEventType::AbilityActivated, 11);
	const FBattleEvent* PlayerItem = FindSourceEvent(Evidence.Replay, EBattleEventType::ItemActivated, 11);
	const FBattleEvent* OpponentItem = FindSourceEvent(Evidence.Replay, EBattleEventType::ItemActivated, 21);
	TestTrue(TEXT("Observer reveal evolution is backed by exact public source events"),
		PlayerAbility != nullptr
		&& PlayerAbility->GetSource().DefinitionId == MakeDefinitionId<FDefinitionId>(TEXT("Ability.Blaze"))
		&& PlayerItem != nullptr
		&& PlayerItem->GetSource().DefinitionId == MakeDefinitionId<FDefinitionId>(TEXT("Item.LifeOrb"))
		&& OpponentItem != nullptr
		&& OpponentItem->GetSource().DefinitionId == MakeDefinitionId<FDefinitionId>(TEXT("Item.Leftovers")));
	return bTwins
		&& ValidateGlobalInvariants(*this, Fixture.Catalog, Evidence, TEXT("observer reveal evolution"));
}

} // namespace BattleCanonicalReplayInvariantPrivate

#endif // WITH_DEV_AUTOMATION_TESTS
