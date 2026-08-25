#if WITH_DEV_AUTOMATION_TESTS

#include "Battle/BattleEngine.h"
#include "BattleTestFactories.h"
#include "Misc/AutomationTest.h"

namespace BattleEngineContractTests
{
	using BattleTest::MakeActiveSlotId;
	using BattleTest::MakeDefinitionId;
	using BattleTest::MakeNumericId;
	using BattleTest::MakePartySlotId;

	FBattleSetup MakeEngineSetup(
		const EBattleEncounterKind EncounterKind = EBattleEncounterKind::TutorialScripted)
	{
		FBattleSetupInput Input;
		Input.BattleId = MakeNumericId<FBattleId>(500);
		Input.SettingsReference = {MakeDefinitionId<FDefinitionId>(TEXT("Settings.Casual")), 1};
		Input.CatalogReference = {MakeDefinitionId<FDefinitionId>(TEXT("Catalog.Contract")), 1};
		Input.EncounterKind = EncounterKind;
		Input.Format = EBattleFormat::Single;
		Input.CaptureCapacity = {4, 100};
		Input.Policies.WildFleeMode = EBattleWildFleeMode::Disabled;

		FBattleTrainerSetup Player;
		Player.TrainerId = MakeNumericId<FTrainerId>(1);
		Player.Side = EBattleSide::Player;
		Player.Role = EBattleTrainerRole::Player;
		Player.Controller = EBattleDecisionController::Human;
		Player.SelectorProfileId = MakeDefinitionId<FDefinitionId>(TEXT("Selector.Player"));
		Input.Trainers.Add(Player);

		FBattleTrainerSetup Opponent;
		Opponent.TrainerId = MakeNumericId<FTrainerId>(2);
		Opponent.Side = EBattleSide::Opponent;
		Opponent.Role = EBattleTrainerRole::Opponent;
		Opponent.Controller = EBattleDecisionController::EnemyAI;
		Opponent.SelectorProfileId = MakeDefinitionId<FDefinitionId>(TEXT("Selector.Opponent"));
		Input.Trainers.Add(Opponent);

		auto AddParty = [&Input](const uint64 TrainerValue, const uint64 BattlerValue, const uint64 SourceValue, const TCHAR* Species)
		{
			FBattlePartyEntrySetup Entry;
			Entry.TrainerId = MakeNumericId<FTrainerId>(TrainerValue);
			Entry.BattlerId = MakeNumericId<FBattlerId>(BattlerValue);
			Entry.SourcePokemonId = MakeNumericId<FSourcePokemonId>(SourceValue);
			Entry.PartySlotId = MakePartySlotId(0);
			Entry.SpeciesFormId = MakeDefinitionId<FSpeciesFormId>(Species);
			Entry.Level = 50;
			Entry.Stats = {200, 100, 100, 100, 100, 100};
			Entry.CurrentHP = 200;
			Entry.AbilityId = MakeDefinitionId<FAbilityId>(TEXT("Ability.Test"));
			Input.PartyEntries.Add(Entry);
		};
		AddParty(1, 11, 111, TEXT("Species.Charizard"));
		AddParty(2, 21, 211, TEXT("Species.Venusaur"));

		FBattleActiveAssignment PlayerActive;
		PlayerActive.ActiveSlotId = MakeActiveSlotId(EBattleSide::Player, EBattlePosition::Left);
		PlayerActive.TrainerId = MakeNumericId<FTrainerId>(1);
		PlayerActive.BattlerId = MakeNumericId<FBattlerId>(11);
		Input.StartingActive.Add(PlayerActive);

		FBattleActiveAssignment OpponentActive;
		OpponentActive.ActiveSlotId = MakeActiveSlotId(EBattleSide::Opponent, EBattlePosition::Left);
		OpponentActive.TrainerId = MakeNumericId<FTrainerId>(2);
		OpponentActive.BattlerId = MakeNumericId<FBattlerId>(21);
		Input.StartingActive.Add(OpponentActive);

		FBattleSetup Setup;
		EBattleSetupValidationError Error;
		check(FBattleSetup::TryCreate(Input, Setup, Error));
		return Setup;
	}

	FBattleDecisionRequest MakeScriptedRequest(const uint64 StateVersion)
	{
		FBattleDecisionRequestSpec Spec;
		Spec.StateVersion = StateVersion;
		Spec.RequestKind = EBattleDecisionRequestKind::Action;
		Spec.DecisionOwnerTrainerId = MakeNumericId<FTrainerId>(1);
		Spec.ActingBattlerId = MakeNumericId<FBattlerId>(11);
		Spec.ActingSlotId = MakeActiveSlotId(EBattleSide::Player, EBattlePosition::Left);
		Spec.LegalActionKinds.Add(EBattleActionKind::ScriptedEnd);

		FBattleDecisionRequest Request;
		FBattleRejection Rejection;
		check(FBattleDecisionRequest::TryCreate(Spec, Request, Rejection));
		return Request;
	}

	FBattleDecision MakeScriptedDecision(const uint64 StateVersion)
	{
		FBattleDecision Decision;
		check(FBattleDecision::TryCreateSimpleAction(
			StateVersion,
			EBattleDecisionRequestKind::Action,
			MakeNumericId<FTrainerId>(1),
			MakeNumericId<FBattlerId>(11),
			EBattleActionKind::ScriptedEnd,
			Decision));
		return Decision;
	}
}

class FBattleEngineContractFixture
{
public:
	static bool TryCreate(
		const FBattleSetup& Setup,
		TUniquePtr<IBattleRandom>&& Random,
		const FBattleDecisionRequest& PendingRequest,
		const bool bSeedOpponentRemovalCheckpoint,
		TUniquePtr<FBattleEngine>& OutEngine,
		FBattleRejection& OutRejection)
	{
		return FBattleEngine::TryCreateForContractFixture(
			Setup,
			MoveTemp(Random),
			PendingRequest,
			bSeedOpponentRemovalCheckpoint,
			OutEngine,
			OutRejection);
	}
};

namespace BattleEngineContractTests
{

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC01BDecisionRejectionTest,
	"PokemonSolarus.Battle.C01B.Decision.RejectionGuards",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBattleC01BDecisionRejectionTest::RunTest(const FString& Parameters)
{
	TUniquePtr<FBattleEngine> Engine;
	FBattleRejection CreationRejection;
	TestTrue(
		TEXT("The contract fixture is created"),
		FBattleEngineContractFixture::TryCreate(
			MakeEngineSetup(),
			MakeUnique<FSeededBattleRandom>(123),
			MakeScriptedRequest(1),
			false,
			Engine,
			CreationRejection));
	TestTrue(TEXT("Engine creation produces a valid initial turn ID"), Engine->GetSnapshot().GetTurnId().IsValid());

	const FBattleResolution Stale = Engine->SubmitDecision(MakeScriptedDecision(2));
	TestTrue(TEXT("A rejected operation still produces a valid resolution"), Stale.IsValid());
	TestTrue(TEXT("A rejected operation receives a valid resolution ID"), Stale.GetResolutionId().IsValid());
	TestFalse(TEXT("A stale decision is rejected"), Stale.WasAccepted());
	TestEqual(TEXT("The stale reason is stable"), Stale.GetRejection().Reason, EBattleRejectionReason::StaleStateVersion);
	TestEqual(TEXT("A stale decision does not change state version"), Engine->GetSnapshot().GetStateVersion(), 1ULL);
	TestEqual(TEXT("A stale decision consumes no RNG"), Engine->ExportRandomTrace().Num(), 0);

	FBattleDecision Illegal;
	check(FBattleDecision::TryCreateSimpleAction(
		1,
		EBattleDecisionRequestKind::Action,
		MakeNumericId<FTrainerId>(1),
		MakeNumericId<FBattlerId>(11),
		EBattleActionKind::Abandon,
		Illegal));
	const FBattleResolution IllegalResult = Engine->SubmitDecision(Illegal);
	TestFalse(TEXT("An action absent from the legal request is rejected"), IllegalResult.WasAccepted());
	TestEqual(TEXT("The illegal action reason is stable"), IllegalResult.GetRejection().Reason, EBattleRejectionReason::IllegalAction);
	TestEqual(TEXT("Rejected decisions still consume no RNG"), Engine->ExportRandomTrace().Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC01BSyntheticDecisionTest,
	"PokemonSolarus.Battle.C01B.Engine.SyntheticDecisionTrace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBattleC01BSyntheticDecisionTest::RunTest(const FString& Parameters)
{
	TUniquePtr<FBattleEngine> Engine;
	FBattleRejection Rejection;
	check(FBattleEngineContractFixture::TryCreate(
		MakeEngineSetup(),
		MakeUnique<FSeededBattleRandom>(999),
		MakeScriptedRequest(1),
		false,
		Engine,
		Rejection));

	const FBattleResolution Resolution = Engine->SubmitDecision(MakeScriptedDecision(1));
	TestTrue(TEXT("The accepted operation produces a valid resolution"), Resolution.IsValid());
	TestTrue(TEXT("The accepted operation receives a valid resolution ID"), Resolution.GetResolutionId().IsValid());
	TestTrue(TEXT("The matching synthetic decision is accepted"), Resolution.WasAccepted());
	TestEqual(TEXT("An accepted action advances state exactly once"), Resolution.GetAfterStateVersion(), 2ULL);
	TestEqual(TEXT("The synthetic trace has six ordered events"), Resolution.GetEvents().Num(), 6);
	for (int32 Index = 0; Index < Resolution.GetEvents().Num(); ++Index)
	{
		TestEqual(
			FString::Printf(TEXT("Event %d has its total ordinal"), Index),
			Resolution.GetEvents()[Index].GetEventOrdinal(),
			static_cast<uint64>(Index + 1));
		TestTrue(TEXT("Every accepted-action event carries the same action ID"), Resolution.GetEvents()[Index].GetActionId().IsValid());
		TestTrue(TEXT("Every accepted-action event is valid"), Resolution.GetEvents()[Index].IsValid());
		TestTrue(TEXT("Every accepted-action event carries a valid turn ID"), Resolution.GetEvents()[Index].GetTurnId().IsValid());
		TestTrue(
			TEXT("Every accepted-action event carries the operation resolution ID"),
			Resolution.GetEvents()[Index].GetResolutionId() == Resolution.GetResolutionId());
	}
	TestEqual(TEXT("The final event ends the battle"), Resolution.GetEvents().Last().GetType(), EBattleEventType::BattleEnded);
	TestEqual(TEXT("The engine is terminal"), Engine->GetSnapshot().GetPhase(), EBattlePhase::Terminal);
	TestEqual(TEXT("The terminal outcome is Scripted End"), Engine->GetSnapshot().GetOutcome(), EBattleOutcome::ScriptedEnd);
	TestEqual(TEXT("The accepted synthetic action consumes no RNG"), Engine->ExportRandomTrace().Num(), 0);

	const FBattleResolution Terminal = Engine->SubmitDecision(MakeScriptedDecision(2));
	TestFalse(TEXT("A terminal engine rejects later decisions"), Terminal.WasAccepted());
	TestEqual(TEXT("Terminal rejection is typed"), Terminal.GetRejection().Reason, EBattleRejectionReason::TerminalState);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC01BStatRefreshTest,
	"PokemonSolarus.Battle.C01B.Engine.StatRefreshCheckpoint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBattleC01BStatRefreshTest::RunTest(const FString& Parameters)
{
	TUniquePtr<FBattleEngine> Engine;
	FBattleRejection Rejection;
	check(FBattleEngineContractFixture::TryCreate(
		MakeEngineSetup(),
		MakeUnique<FSeededBattleRandom>(77),
		MakeScriptedRequest(1),
		true,
		Engine,
		Rejection));

	FBattleBetweenActionsStatRefresh Refresh;
	Refresh.StateVersion = 1;
	Refresh.OpponentRemovalCheckpointEventOrdinal = 1;
	Refresh.BattlerId = MakeNumericId<FBattlerId>(11);
	Refresh.NewLevel = 51;
	Refresh.NewStats = {204, 104, 103, 105, 103, 102};
	Refresh.NewCurrentHP = 204;

	const FBattleResolution Applied = Engine->ApplyBetweenActionsStatRefresh(Refresh);
	TestTrue(TEXT("A matching between-actions refresh is accepted"), Applied.WasAccepted());
	TestEqual(TEXT("The refresh advances the state version"), Engine->GetSnapshot().GetStateVersion(), 2ULL);
	const FBattlePartyEntrySetup* Battler = Engine->GetSnapshot().FindBattler(Refresh.BattlerId);
	TestNotNull(TEXT("The refreshed battler remains in the snapshot"), Battler);
	if (Battler != nullptr)
	{
		TestEqual(TEXT("The refreshed level is visible"), Battler->Level, 51);
		TestEqual(TEXT("The refreshed Max HP is visible"), Battler->Stats.MaxHP, 204);
		TestEqual(TEXT("The supplied current HP adjustment is visible"), Battler->CurrentHP, 204);
	}

	const FBattleResolution Reused = Engine->ApplyBetweenActionsStatRefresh(Refresh);
	TestFalse(TEXT("The consumed checkpoint cannot be reused"), Reused.WasAccepted());
	TestEqual(TEXT("The stale refresh is rejected first by version"), Reused.GetRejection().Reason, EBattleRejectionReason::StaleStateVersion);
	TestEqual(TEXT("Stat refreshes consume no RNG"), Engine->ExportRandomTrace().Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC01BEventOrderingTest,
	"PokemonSolarus.Battle.C01B.Events.OrderingAndOrdinals",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBattleC01BEventOrderingTest::RunTest(const FString& Parameters)
{
	FBattleEventSpec FirstSpec;
	FirstSpec.EventOrdinal = 10;
	FirstSpec.BattleId = MakeNumericId<FBattleId>(1);
	FirstSpec.TurnId = MakeNumericId<FTurnId>(1);
	FirstSpec.ActionId = MakeNumericId<FActionId>(1);
	FirstSpec.ResolutionId = MakeNumericId<FResolutionId>(1);
	FirstSpec.Type = EBattleEventType::Damage;
	FirstSpec.Cause = EBattleEventCause::Move;
	FirstSpec.Source.BattlerId = MakeNumericId<FBattlerId>(11);
	FirstSpec.Targets.Add({MakeNumericId<FTrainerId>(2), MakeNumericId<FBattlerId>(21), FActiveSlotId()});
	FirstSpec.SimultaneousGroupId = 42;
	FirstSpec.HitIndex = 1;
	FirstSpec.HitCount = 2;

	FBattleEvent First;
	FBattleEvent Second;
	TestTrue(TEXT("The first hit event is valid"), FBattleEvent::TryCreate(FirstSpec, First));
	FBattleEventSpec SecondSpec = FirstSpec;
	SecondSpec.EventOrdinal = 11;
	SecondSpec.HitIndex = 2;
	TestTrue(TEXT("The second hit event is valid"), FBattleEvent::TryCreate(SecondSpec, Second));

	FBattleResolutionSpec ResolutionSpec;
	ResolutionSpec.ResolutionId = MakeNumericId<FResolutionId>(1);
	ResolutionSpec.BeforeStateVersion = 1;
	ResolutionSpec.AfterStateVersion = 2;
	ResolutionSpec.bAccepted = true;
	ResolutionSpec.Events = {First, Second};
	FBattleResolution Resolution;
	TestTrue(TEXT("Strictly ordered grouped hits form a resolution"), FBattleResolution::TryCreate(ResolutionSpec, Resolution));
	TestEqual(TEXT("The first hit stays first"), Resolution.GetEvents()[0].GetHitIndex().GetValue(), static_cast<uint16>(1));
	TestEqual(TEXT("The simultaneous group is preserved"), Resolution.GetEvents()[1].GetSimultaneousGroupId().GetValue(), 42ULL);

	ResolutionSpec.Events = {Second, First};
	TestFalse(TEXT("Descending event ordinals are rejected"), FBattleResolution::TryCreate(ResolutionSpec, Resolution));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC01BSnapshotImmutabilityTest,
	"PokemonSolarus.Battle.C01B.Snapshot.Immutability",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBattleC01BSnapshotImmutabilityTest::RunTest(const FString& Parameters)
{
	TUniquePtr<FBattleEngine> Engine;
	FBattleRejection Rejection;
	check(FBattleEngineContractFixture::TryCreate(
		MakeEngineSetup(),
		MakeUnique<FSeededBattleRandom>(1),
		MakeScriptedRequest(1),
		false,
		Engine,
		Rejection));

	const FBattleSnapshot Before = Engine->GetSnapshot();
	const FBattlePartyEntrySetup* BeforeBattler = Before.FindBattler(MakeNumericId<FBattlerId>(11));
	check(BeforeBattler != nullptr);
	const int32 BeforeHP = BeforeBattler->CurrentHP;
	const FBattleResolution Resolution = Engine->SubmitDecision(MakeScriptedDecision(1));
	TestTrue(TEXT("The mutation used to test snapshot immutability is accepted"), Resolution.WasAccepted());

	TestEqual(TEXT("The old snapshot keeps its original version"), Before.GetStateVersion(), 1ULL);
	TestEqual(TEXT("The old snapshot keeps its original phase"), Before.GetPhase(), EBattlePhase::Selecting);
	TestEqual(TEXT("The old snapshot keeps its deep-copied battler facts"), Before.FindBattler(MakeNumericId<FBattlerId>(11))->CurrentHP, BeforeHP);
	TestEqual(TEXT("A new snapshot observes the advanced version"), Engine->GetSnapshot().GetStateVersion(), 2ULL);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023B2ScriptedEndingAuthorityTest,
	"PokemonSolarus.Battle.ADR0002.3B2.RuntimeAuthority.ScriptedEnding.CompiledPermission",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBattleADR00023B2ScriptedEndingAuthorityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TUniquePtr<FBattleEngine> Engine;
	FBattleRejection Rejection;
	TestTrue(
		TEXT("A Trainer fixture can expose a forged scripted-ending request"),
		FBattleEngineContractFixture::TryCreate(
			MakeEngineSetup(EBattleEncounterKind::Trainer),
			MakeUnique<FSeededBattleRandom>(123),
			MakeScriptedRequest(1),
			false,
			Engine,
			Rejection));
	check(Engine.IsValid());
	TestFalse(TEXT("Trainer policy denies scripted endings"),
		Engine->GetCompiledEncounterPolicies().IsScriptedEndingAllowed());

	const FBattleResolution Result = Engine->SubmitDecision(MakeScriptedDecision(1));
	TestFalse(TEXT("A request cannot override compiled scripted-ending policy"),
		Result.WasAccepted());
	TestEqual(TEXT("The denial is a typed illegal action"),
		Result.GetRejection().Reason, EBattleRejectionReason::IllegalAction);
	TestEqual(TEXT("Denied scripted ending leaves the battle selecting"),
		Engine->GetSnapshot().GetPhase(), EBattlePhase::Selecting);
	TestEqual(TEXT("Denied scripted ending consumes no RNG"),
		Engine->ExportRandomTrace().Num(), 0);
	return true;
}

}

#endif // WITH_DEV_AUTOMATION_TESTS
