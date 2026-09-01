#if WITH_DEV_AUTOMATION_TESTS

#include "BattleCanonicalIntegrationTestSupport.h"

#include "Battle/BattleSwitching.h"
#include "Misc/AutomationTest.h"

namespace BattleCanonicalSingleCorePrivate
{
	using namespace BattleCanonicalIntegrationTestSupport;

	FChoice Fight(const TCHAR* Move, const EBattleSide Side = EBattleSide::Opponent)
	{
		FChoice Choice;
		Choice.Kind = EChoiceKind::Fight;
		Choice.DefinitionId = FName(Move);
		Choice.ActiveTarget = MakeActiveSlotId(Side, EBattlePosition::Left);
		return Choice;
	}

	FSetupSpec SingleSpec(
		const uint64 BattleValue,
		const TArray<FName>& PlayerMoves,
		const TArray<FName>& OpponentMoves,
		const int32 OpponentHP = INDEX_NONE,
		const bool bPlayerReserve = false,
		const bool bOpponentReserve = false,
		const bool bShift = false)
	{
		FSetupSpec Spec;
		Spec.BattleValue = BattleValue;
		Spec.EncounterKind = EBattleEncounterKind::Trainer;
		Spec.Format = EBattleFormat::Single;
		Spec.Policies.bBagAllowed = true;
		Spec.Policies.bShiftPromptEligible = bShift;
		Spec.Trainers = {
			{1, EBattleSide::Player, EBattleTrainerRole::Player, EBattleDecisionController::Human, {}},
			{2, EBattleSide::Opponent, EBattleTrainerRole::Opponent, EBattleDecisionController::EnemyAI, {}}};
		FBattlerSpec Player;
		Player.TrainerValue = 1;
		Player.BattlerValue = 11;
		Player.SpeciesId = FName(TEXT("Species.Charizard"));
		Player.AbilityId = FName(TEXT("Ability.Blaze"));
		Player.MoveIds = PlayerMoves;
		Player.EffortValues.Speed = 252;
		Spec.Battlers.Add(Player);
		FBattlerSpec Opponent;
		Opponent.TrainerValue = 2;
		Opponent.BattlerValue = 21;
		Opponent.SpeciesId = FName(TEXT("Species.Venusaur"));
		Opponent.AbilityId = FName(TEXT("Ability.Overgrow"));
		Opponent.MoveIds = OpponentMoves;
		Opponent.CurrentHP = OpponentHP;
		Spec.Battlers.Add(Opponent);
		Spec.Active = {
			{EBattleSide::Player, EBattlePosition::Left, 1, 11},
			{EBattleSide::Opponent, EBattlePosition::Left, 2, 21}};
		if (bPlayerReserve)
		{
			FBattlerSpec Reserve;
			Reserve.TrainerValue = 1;
			Reserve.BattlerValue = 12;
			Reserve.PartyIndex = 1;
			Reserve.SpeciesId = FName(TEXT("Species.Gyarados"));
			Reserve.AbilityId = FName(TEXT("Ability.Intimidate"));
			Reserve.MoveIds = {FName(TEXT("Move.Bite"))};
			Spec.Battlers.Add(Reserve);
		}
		if (bOpponentReserve)
		{
			FBattlerSpec Reserve;
			Reserve.TrainerValue = 2;
			Reserve.BattlerValue = 22;
			Reserve.PartyIndex = 1;
			Reserve.SpeciesId = FName(TEXT("Species.Pelipper"));
			Reserve.AbilityId = FName(TEXT("Ability.Drizzle"));
			Reserve.MoveIds = {FName(TEXT("Move.RainDance"))};
			Reserve.CurrentHP = 1;
			Spec.Battlers.Add(Reserve);
		}
		return Spec;
	}

	bool Build(
		FAutomationTestBase& Test,
		const FSetupSpec& Spec,
		FCatalogFixture& Fixture,
		FBattleSetup& Setup)
	{
		FString Error;
		if (!TryLoadProductionFixture(Test, Fixture, Error)
			|| !TryBuildSetup(Fixture.Catalog, Spec, Setup, Error))
		{
			Test.AddError(Error);
			return false;
		}
		return true;
	}

	bool HandleRequestsAndQueue(
		FBattleEngine& Engine,
		const FChoiceProvider& Provider,
		FRunEvidence& Evidence,
		FString& Error)
	{
		int32 Guard = 0;
		while (Guard++ < 12)
		{
			if (!Engine.GetPendingDecisionRequests().IsEmpty())
			{
				if (!SubmitPendingChoices(Engine, Provider, Evidence, Error)) return false;
				continue;
			}
			if (Engine.GetSnapshot().GetPhase() == EBattlePhase::Locked
				|| Engine.GetSnapshot().GetPhase() == EBattlePhase::Resolving)
			{
				if (!ExecuteLockedQueue(Engine, Evidence, Error)) return false;
				continue;
			}
			return true;
		}
		Error = TEXT("Request/queue interrupt guard exceeded.");
		return false;
	}

	FBattlerId ActiveBattler(
		const FBattleSnapshot& Snapshot,
		const EBattleSide Side)
	{
		const FActiveSlotId Slot = MakeActiveSlotId(Side, EBattlePosition::Left);
		const FBattleActiveAssignment* Assignment = Snapshot.GetActiveAssignments().FindByPredicate(
			[Slot](const FBattleActiveAssignment& Value) { return Value.ActiveSlotId == Slot; });
		return Assignment == nullptr ? FBattlerId() : Assignment->BattlerId;
	}

	bool RunTrappedScenario(
		FAutomationTestBase& Test,
		const FCatalogFixture& Fixture)
	{
		FSetupSpec Spec = SingleSpec(11022, {TEXT("Move.SwordsDance")},
			{TEXT("Move.MeanLook")}, INDEX_NONE, true, false);
		FBattleSetup Setup;
		FString Error;
		if (!TryBuildSetup(Fixture.Catalog, Spec, Setup, Error))
		{
			Test.AddError(Error);
			return false;
		}
		const FChoiceProvider Provider = [](const FBattleDecisionRequest& Request, FChoice& Out, FString&)
		{
			Out = Request.GetDecisionOwnerTrainerId() == MakeNumericId<FTrainerId>(1)
				? Fight(TEXT("Move.SwordsDance"))
				: Fight(TEXT("Move.MeanLook"), EBattleSide::Player);
			return true;
		};
		const FDriveFunction Drive = [Provider](FBattleEngine& Engine, FRunEvidence& Evidence, FString& DriveError)
		{
			if (!LockTurn(Engine, Provider, Evidence, DriveError)
				|| !ExecuteLockedQueue(Engine, Evidence, DriveError)
				|| !ResolveEndTurn(Engine, Evidence, DriveError)) return false;
			FBattleRejection BeginRejection;
			if (Engine.GetPendingDecisionRequests().IsEmpty()
				&& !Engine.TryBeginActionDecisionSequence(BeginRejection))
			{
				DriveError = TEXT("Trapped follow-up selection did not begin.");
				return false;
			}
			RecordCheckpoint(Engine, Evidence, TEXT("trapped-selection"));
			const TArray<FBattleDecisionRequest> Requests = Engine.GetPendingDecisionRequests();
			const FBattleDecisionRequest* Request = Requests.FindByPredicate([](const FBattleDecisionRequest& Value)
			{
				return Value.GetDecisionOwnerTrainerId() == MakeNumericId<FTrainerId>(1);
			});
			const bool bTypedTrap = Request != nullptr
				&& !Request->GetLegalActionKinds().Contains(EBattleActionKind::Switch)
				&& Request->GetLegalSwitchPartySlots().IsEmpty()
				&& Request->GetUnavailableOptions().ContainsByPredicate([](const FBattleUnavailableDecisionOption& Option)
				{
					return Option.Kind == EBattleDecisionOptionKind::SwitchPartySlot
						&& Option.PartySlotId == MakePartySlotId(1)
						&& Option.Reason == EBattleOptionUnavailableReason::Trapped;
				});
			if (!bTypedTrap)
			{
				DriveError = TEXT("Mean Look did not expose the exact trapped switch option.");
				return false;
			}
			FBattleDecision Forged;
			if (!FBattleDecision::TryCreateSwitch(
				Request->GetStateVersion(), Request->GetRequestKind(),
				Request->GetDecisionOwnerTrainerId(), Request->GetActingBattlerId(),
				MakePartySlotId(1), Request->GetActingSlotId(), Forged))
			{
				DriveError = TEXT("Could not construct the trapped rejection probe.");
				return false;
			}
			const FString SnapshotBefore = SnapshotSignature(Engine.GetSnapshot());
			const int32 TraceBefore = Engine.ExportRandomTrace().Num();
			const int32 RequestsBefore = Engine.GetPendingDecisionRequests().Num();
			const FBattleReplayRecord ReplayBefore = Engine.ExportReplayRecord();
			const FBattleResolution Rejected = Engine.SubmitDecision(Forged);
			const FBattleReplayRecord ReplayAfter = Engine.ExportReplayRecord();
			if (Rejected.WasAccepted()
				|| Rejected.GetRejection().Reason != EBattleRejectionReason::IllegalAction
				|| Rejected.GetBeforeStateVersion() != Rejected.GetAfterStateVersion()
				|| SnapshotSignature(Engine.GetSnapshot()) != SnapshotBefore
				|| Engine.ExportRandomTrace().Num() != TraceBefore
				|| Engine.GetPendingDecisionRequests().Num() != RequestsBefore
				|| Rejected.GetEvents().Num() != 1
				|| Rejected.GetEvents()[0].GetType() != EBattleEventType::DecisionRejected
				|| ReplayAfter.GetInputs().Decisions.Num() != ReplayBefore.GetInputs().Decisions.Num() + 1
				|| ReplayAfter.GetResolutions().Num() != ReplayBefore.GetResolutions().Num() + 1)
			{
				DriveError = TEXT("The trapped switch rejection changed gameplay state, RNG, resources, or its audit delta.");
				return false;
			}
			return true;
		};
		FRunEvidence Evidence;
		return RunDeterministicTwins(Test, TEXT("trapped voluntary switch rejection"),
			Fixture.Catalog, Setup, Drive, &Evidence, 5)
			&& Test.TestTrue(TEXT("Mean Look publishes the production Trap condition"),
				HasEvent(Evidence.Replay, EBattleEventType::StatusChanged))
			&& ValidateGlobalInvariants(Test, Fixture.Catalog, Evidence, TEXT("trapped switch"));
	}

	bool RunForcedScenarios(
		FAutomationTestBase& Test,
		const FCatalogFixture& Fixture)
	{
		bool bValid = true;
		for (int32 HasReserve = 0; HasReserve <= 1; ++HasReserve)
		{
			FSetupSpec Spec = SingleSpec(11023 + HasReserve, {TEXT("Move.Roar")},
				{TEXT("Move.SwordsDance")}, INDEX_NONE, false, HasReserve != 0);
			FBattleSetup Setup;
			FString Error;
			if (!TryBuildSetup(Fixture.Catalog, Spec, Setup, Error))
			{
				Test.AddError(Error);
				return false;
			}
			const FChoiceProvider Provider = [](const FBattleDecisionRequest& Request, FChoice& Out, FString&)
			{
				Out = Request.GetDecisionOwnerTrainerId() == MakeNumericId<FTrainerId>(1)
					? Fight(TEXT("Move.Roar")) : Fight(TEXT("Move.SwordsDance"), EBattleSide::Player);
				return true;
			};
			const FDriveFunction Drive = [Provider](FBattleEngine& Engine, FRunEvidence& Evidence, FString& DriveError)
			{
				return LockTurn(Engine, Provider, Evidence, DriveError)
					&& ExecuteLockedQueue(Engine, Evidence, DriveError);
			};
			FRunEvidence Evidence;
			const FString Label = HasReserve ? TEXT("forced switch with reserve") : TEXT("forced switch without reserve");
			bValid &= RunDeterministicTwins(Test, Label, Fixture.Catalog, Setup, Drive, &Evidence, 5);
			const TConstArrayView<FBattleRandomDraw> Trace = Evidence.Replay.GetRandomTrace();
			if (HasReserve)
			{
				bValid &= Test.TestTrue(TEXT("Roar consumes exactly one party-ordered forced-switch draw"),
					Trace.Num() == 1 && Trace[0].InclusiveMinimum == 0 && Trace[0].InclusiveMaximum == 0
					&& Trace[0].RulePurpose == FBattleSwitchResolver::GetForcedSelectionRulePurpose());
				bValid &= Test.TestTrue(TEXT("Roar replaces the exact opponent occupant and publishes the full switch path"),
					ActiveBattler(Evidence.Replay.GetFinalSnapshot(), EBattleSide::Opponent)
						== MakeNumericId<FBattlerId>(22)
					&& CountEvents(Evidence.Replay, EBattleEventType::LeftActiveSlot) == 1
					&& CountEvents(Evidence.Replay, EBattleEventType::EnteredActiveSlot) == 1
					&& CountEvents(Evidence.Replay, EBattleEventType::Switched) == 1);
			}
			else
			{
				bValid &= Test.TestTrue(TEXT("Roar without a reserve is an applied no-switch failure with no RNG"),
					Trace.IsEmpty()
					&& ActiveBattler(Evidence.Replay.GetFinalSnapshot(), EBattleSide::Opponent)
						== MakeNumericId<FBattlerId>(21)
					&& CountEvents(Evidence.Replay, EBattleEventType::EffectFailed) == 1
					&& CountEvents(Evidence.Replay, EBattleEventType::Switched) == 0);
			}
			bValid &= ValidateGlobalInvariants(Test, Fixture.Catalog, Evidence, Label);
		}
		return bValid;
	}

	bool RunShiftScenarios(
		FAutomationTestBase& Test,
		const FCatalogFixture& Fixture)
	{
		struct FCase
		{
			const TCHAR* Label;
			bool bShiftPolicy;
			bool bPlayerReserve;
			bool bAccept;
			bool bExpectPrompt;
		};
		const FCase Cases[] = {
			{TEXT("Shift accepted"), true, true, true, true},
			{TEXT("Shift declined"), true, true, false, true},
			{TEXT("Set policy"), false, true, false, false},
			{TEXT("Shift without player reserve"), true, false, false, false}};
		bool bValid = true;
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Cases); ++Index)
		{
			const FCase& ShiftCase = Cases[Index];
			FSetupSpec Spec = SingleSpec(11025 + Index, {TEXT("Move.Flamethrower")},
				{TEXT("Move.SwordsDance")}, 1, ShiftCase.bPlayerReserve, true, ShiftCase.bShiftPolicy);
			FBattleSetup Setup;
			FString Error;
			if (!TryBuildSetup(Fixture.Catalog, Spec, Setup, Error))
			{
				Test.AddError(Error);
				return false;
			}
			const FChoiceProvider Provider = [ShiftCase](const FBattleDecisionRequest& Request, FChoice& Out, FString&)
			{
				if (Request.GetRequestKind() == EBattleDecisionRequestKind::ShiftResponse)
				{
					Out.Kind = ShiftCase.bAccept ? EChoiceKind::ShiftAccept : EChoiceKind::ShiftDecline;
					if (ShiftCase.bAccept) Out.PartyTarget = MakePartySlotId(1);
				}
				else if (Request.GetRequestKind() == EBattleDecisionRequestKind::MandatoryReplacement)
				{
					Out.Kind = EChoiceKind::Replacement;
					Out.PartyTarget = MakePartySlotId(1);
				}
				else
				{
					Out = Request.GetDecisionOwnerTrainerId() == MakeNumericId<FTrainerId>(1)
						? Fight(TEXT("Move.Flamethrower"))
						: Fight(TEXT("Move.SwordsDance"), EBattleSide::Player);
				}
				return true;
			};
			const FDriveFunction Drive = [Provider](FBattleEngine& Engine, FRunEvidence& Evidence, FString& DriveError)
			{
				return LockTurn(Engine, Provider, Evidence, DriveError)
					&& HandleRequestsAndQueue(Engine, Provider, Evidence, DriveError);
			};
			FRunEvidence Evidence;
			bValid &= RunDeterministicTwins(Test, ShiftCase.Label, Fixture.Catalog, Setup, Drive, &Evidence, 5);
			int32 ShiftResponses = 0;
			for (const FBattleDecision& Decision : Evidence.Replay.GetInputs().Decisions)
			{
				ShiftResponses += Decision.GetRequestKind() == EBattleDecisionRequestKind::ShiftResponse ? 1 : 0;
			}
			bValid &= Test.TestEqual(FString(ShiftCase.Label) + TEXT(" exact Shift-response count"),
				ShiftResponses, ShiftCase.bExpectPrompt ? 1 : 0);
			bValid &= Test.TestTrue(FString(ShiftCase.Label) + TEXT(" keeps the exact active occupants"),
				ActiveBattler(Evidence.Replay.GetFinalSnapshot(), EBattleSide::Player)
					== MakeNumericId<FBattlerId>(ShiftCase.bAccept ? 12 : 11)
				&& ActiveBattler(Evidence.Replay.GetFinalSnapshot(), EBattleSide::Opponent)
					== MakeNumericId<FBattlerId>(22));
			bValid &= Test.TestEqual(FString(ShiftCase.Label) + TEXT(" exact switch count"),
				CountEvents(Evidence.Replay, EBattleEventType::Switched), ShiftCase.bAccept ? 2 : 1);
			bValid &= ValidateGlobalInvariants(Test, Fixture.Catalog, Evidence, ShiftCase.Label);
		}
		return bValid;
	}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FC11AProductionCatalogAndCalculatorFixtures,
	"PokemonSolarus.Battle.C11A.Single.Baseline.ProductionCatalogAndCalculatorFixtures",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FC11AProductionCatalogAndCalculatorFixtures::RunTest(const FString& Parameters)
{
	FCatalogFixture Fixture;
	FString Error;
	if (!TryLoadProductionFixture(*this, Fixture, Error))
	{
		AddError(Error);
		return false;
	}
	bool bValid = ValidateCoverageManifest(*this, Fixture.Catalog);
	for (const FBattleSpeciesFormDefinition& Species : Fixture.Catalog.GetSpeciesForms())
	{
		for (const FBattleNatureDefinition& Nature : Fixture.Catalog.GetNatures())
		{
			FPokemonStatInputs Inputs;
			Inputs.Level = 50;
			Inputs.BaseStats = Species.BaseStats;
			Inputs.IndividualValues = {31, 31, 31, 31, 31, 31};
			Inputs.NatureModifier = Nature.Modifier;
			FPokemonBattleStats Stats;
			EBattleStatCalculationError StatError = EBattleStatCalculationError::None;
			bValid &= TestTrue(TEXT("Every production species/nature pair calculates"),
				FBattleStatCalculator::TryCalculatePermanentStats(Inputs, Stats, StatError));
			bValid &= TestTrue(TEXT("Calculated permanent stats are positive"),
				Stats.MaxHP > 0 && Stats.Attack > 0 && Stats.Defense > 0
				&& Stats.SpecialAttack > 0 && Stats.SpecialDefense > 0 && Stats.Speed > 0);
		}
	}
	FBattleTypeEffectiveness Value;
	bValid &= TestTrue(TEXT("Neutral type representative resolves"),
		Fixture.Catalog.GetTypeChart().TryGetEffectiveness(
			EPokemonType::Fire, EPokemonType::Poison, Value)
			&& Value.Numerator == 1 && Value.Denominator == 1);
	bValid &= TestTrue(TEXT("Raised effectiveness representative resolves"),
		Fixture.Catalog.GetTypeChart().TryGetEffectiveness(
			EPokemonType::Fire, EPokemonType::Grass, Value)
			&& Value.Numerator == 2 && Value.Denominator == 1);
	bValid &= TestTrue(TEXT("Immunity representative resolves"),
		Fixture.Catalog.GetTypeChart().TryGetEffectiveness(
			EPokemonType::Normal, EPokemonType::Ghost, Value)
			&& Value.Numerator == 0 && Value.Denominator == 1);
	bValid &= TestTrue(TEXT("Dual-type representative resolves"),
		Fixture.Catalog.GetTypeChart().TryGetDualEffectiveness(
			EPokemonType::Grass, EPokemonType::Fire, EPokemonType::Flying, Value)
			&& Value.Numerator == 1 && Value.Denominator == 4);
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FC11AFullFlowFaintReplacementTerminal,
	"PokemonSolarus.Battle.C11A.Single.Core.FullFlowFaintReplacementEndTurnTerminal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FC11AFullFlowFaintReplacementTerminal::RunTest(const FString& Parameters)
{
	FCatalogFixture Fixture;
	FBattleSetup Setup;
	if (!Build(*this, SingleSpec(11002, {FName(TEXT("Move.Flamethrower"))},
		{FName(TEXT("Move.SwordsDance"))}, 1, false, true), Fixture, Setup)) return false;
	const FChoiceProvider Provider = [](const FBattleDecisionRequest& Request, FChoice& Out, FString&)
	{
		if (Request.GetRequestKind() == EBattleDecisionRequestKind::MandatoryReplacement)
		{
			Out.Kind = EChoiceKind::Replacement;
			Out.PartyTarget = MakePartySlotId(1);
		}
		else
		{
			Out = Request.GetDecisionOwnerTrainerId() == MakeNumericId<FTrainerId>(1)
				? Fight(TEXT("Move.Flamethrower"))
				: Request.GetActingBattlerId() == MakeNumericId<FBattlerId>(22)
					? Fight(TEXT("Move.RainDance"), EBattleSide::Player)
					: Fight(TEXT("Move.SwordsDance"), EBattleSide::Player);
		}
		return true;
	};
	const FDriveFunction Drive = [Provider](FBattleEngine& Engine, FRunEvidence& Evidence, FString& Error)
	{
		for (int32 Turn = 0; Turn < 2 && Engine.GetSnapshot().GetPhase() != EBattlePhase::Terminal; ++Turn)
		{
			if (!LockTurn(Engine, Provider, Evidence, Error)
				|| !HandleRequestsAndQueue(Engine, Provider, Evidence, Error)) return false;
			if (Engine.GetSnapshot().GetPhase() == EBattlePhase::EndOfTurn
				&& !ResolveEndTurn(Engine, Evidence, Error)) return false;
		}
		return Engine.GetSnapshot().GetPhase() == EBattlePhase::Terminal;
	};
	FRunEvidence Evidence;
	if (!RunDeterministicTwins(*this, TEXT("single full flow"), Fixture.Catalog, Setup, Drive, &Evidence)) return false;
	TestEqual(TEXT("Full flow ends in Victory"), Evidence.Replay.GetFinalSnapshot().GetOutcome(), EBattleOutcome::Victory);
	TestTrue(TEXT("Full flow includes faint facts"), HasEvent(Evidence.Replay, EBattleEventType::Fainted));
	TestTrue(TEXT("Full flow includes a free replacement"), HasEvent(Evidence.Replay, EBattleEventType::ReplacementRequired));
	TestTrue(TEXT("Full flow publishes BattleEnded"), HasEvent(Evidence.Replay, EBattleEventType::BattleEnded));
	return ValidateGlobalInvariants(*this, Fixture.Catalog, Evidence, TEXT("single full flow"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FC11AObedienceBoundaries,
	"PokemonSolarus.Battle.C11A.Single.Obedience.CapBoundariesPPActionAndRng",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FC11AObedienceBoundaries::RunTest(const FString& Parameters)
{
	struct FCase { uint8 Level; uint8 Badges; bool bObeys; };
	const FCase Cases[] = {{20, 0, true}, {21, 0, false}, {100, 8, true}, {100, 7, false}};
	bool bValid = true;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Cases); ++Index)
	{
		FCatalogFixture Fixture;
		FSetupSpec Spec = SingleSpec(11010 + Index,
			{FName(TEXT("Move.Flamethrower"))}, {FName(TEXT("Move.SwordsDance"))});
		Spec.Obedience.Add({11, true, Cases[Index].Level, Cases[Index].Badges});
		FBattleSetup Setup;
		if (!Build(*this, Spec, Fixture, Setup)) return false;
		const FChoiceProvider Provider = [](const FBattleDecisionRequest& Request, FChoice& Out, FString&)
		{
			Out = Request.GetDecisionOwnerTrainerId() == MakeNumericId<FTrainerId>(1)
				? Fight(TEXT("Move.Flamethrower")) : Fight(TEXT("Move.SwordsDance"), EBattleSide::Player);
			return true;
		};
		const FDriveFunction Drive = [Provider](FBattleEngine& Engine, FRunEvidence& Evidence, FString& Error)
		{
			if (!LockTurn(Engine, Provider, Evidence, Error)) return false;
			const FBattleResolution Start = Engine.BeginNextLockedAction();
			if (!Start.WasAccepted()) return false;
			RecordCheckpoint(Engine, Evidence, TEXT("obedience-start"));
			if (Engine.GetCurrentLockedAction().IsSet())
			{
				const FBattleResolution Commit = Engine.CommitCurrentMoveAfterPreMoveGates();
				if (!Commit.WasAccepted()) return false;
				RecordCheckpoint(Engine, Evidence, TEXT("obedience-commit"));
			}
			return true;
		};
		FRunEvidence Evidence;
		const FString Label = FString::Printf(TEXT("obedience %d/%d"), Cases[Index].Level, Cases[Index].Badges);
		const TArray<BattleTest::FBattleExpectedRandomDraw> ExpectedDraws;
		bValid &= RunStrictTwins(*this, Label, Fixture.Catalog, Setup, ExpectedDraws, Drive, &Evidence);
		bValid &= TestEqual(Label + TEXT(" refusal branch"),
			HasEvent(Evidence.Replay, EBattleEventType::ObedienceRefused), !Cases[Index].bObeys);
		bValid &= TestEqual(Label + TEXT(" confirmation branch"),
			HasEvent(Evidence.Replay, EBattleEventType::ObedienceConfirmed), Cases[Index].bObeys);
		bValid &= TestEqual(Label + TEXT(" PP consumption"),
			CountEvents(Evidence.Replay, EBattleEventType::PPConsumed), Cases[Index].bObeys ? 1 : 0);
		bValid &= TestEqual(Label + TEXT(" action completion boundary"),
			CountEvents(Evidence.Replay, EBattleEventType::ActionCompleted), Cases[Index].bObeys ? 0 : 1);
		bValid &= TestEqual(Label + TEXT(" exact authoritative RNG trace"),
			Evidence.Replay.GetRandomTrace().Num(), 0);
		const FBattlePartyEntrySetup* BeforeActor = Setup.FindBattler(MakeNumericId<FBattlerId>(11));
		const FBattlePartyEntrySetup* AfterActor = Evidence.Replay.GetFinalSnapshot().FindBattler(MakeNumericId<FBattlerId>(11));
		const int32 BeforePP = BeforeActor != nullptr && !BeforeActor->Moves.IsEmpty()
			? BeforeActor->Moves[0].CurrentPP : INDEX_NONE;
		const int32 AfterPP = AfterActor != nullptr && !AfterActor->Moves.IsEmpty()
			? AfterActor->Moves[0].CurrentPP : INDEX_NONE;
		bValid &= TestEqual(Label + TEXT(" exact move-slot PP"),
			AfterPP, Cases[Index].bObeys ? BeforePP - 1 : BeforePP);
		bValid &= ValidateGlobalInvariants(*this, Fixture.Catalog, Evidence, Label);
	}
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FC11ASwitchingMatrix,
	"PokemonSolarus.Battle.C11A.Single.Switching.VoluntaryTrapPivotForcedShiftSetAndNoReserve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FC11ASwitchingMatrix::RunTest(const FString& Parameters)
{
	FCatalogFixture Fixture;
	FBattleSetup Setup;
	FSetupSpec Spec = SingleSpec(11020, {FName(TEXT("Move.Uturn")), FName(TEXT("Move.SwordsDance"))},
		{FName(TEXT("Move.MeanLook")), FName(TEXT("Move.Roar"))}, INDEX_NONE, true, true, false);
	if (!Build(*this, Spec, Fixture, Setup)) return false;
	const FChoiceProvider Provider = [](const FBattleDecisionRequest& Request, FChoice& Out, FString&)
	{
		if (Request.GetRequestKind() == EBattleDecisionRequestKind::PivotSwitch)
		{
			Out.Kind = EChoiceKind::Switch;
			Out.PartyTarget = MakePartySlotId(1);
			Out.ActiveTarget = Request.GetActingSlotId();
		}
		else if (Request.GetRequestKind() == EBattleDecisionRequestKind::MandatoryReplacement)
		{
			Out.Kind = EChoiceKind::Replacement;
			Out.PartyTarget = MakePartySlotId(1);
		}
		else
		{
			Out = Request.GetDecisionOwnerTrainerId() == MakeNumericId<FTrainerId>(1)
				? Fight(TEXT("Move.Uturn")) : Fight(TEXT("Move.MeanLook"), EBattleSide::Player);
		}
		return true;
	};
	const FDriveFunction Drive = [Provider](FBattleEngine& Engine, FRunEvidence& Evidence, FString& Error)
	{
		if (!LockTurn(Engine, Provider, Evidence, Error)
			|| !HandleRequestsAndQueue(Engine, Provider, Evidence, Error)) return false;
		return HasEvent(Engine.ExportReplayRecord(), EBattleEventType::Switched);
	};
	FRunEvidence Evidence;
	if (!RunDeterministicTwins(*this, TEXT("pivot switching"), Fixture.Catalog, Setup, Drive, &Evidence)) return false;
	TestTrue(TEXT("U-turn reaches the public pivot switch request"),
		Evidence.Replay.GetInputs().Decisions.ContainsByPredicate([](const FBattleDecision& Decision)
		{
			return Decision.GetRequestKind() == EBattleDecisionRequestKind::PivotSwitch;
		}));
	TestTrue(TEXT("Pivot emits switch facts"), HasEvent(Evidence.Replay, EBattleEventType::Switched));

	FSetupSpec VoluntarySpec = SingleSpec(11021, {FName(TEXT("Move.SwordsDance"))},
		{FName(TEXT("Move.SwordsDance"))}, INDEX_NONE, true, false);
	FBattleSetup VoluntarySetup;
	FString SetupError;
	if (!TryBuildSetup(Fixture.Catalog, VoluntarySpec, VoluntarySetup, SetupError))
	{
		AddError(SetupError);
		return false;
	}
	const FChoiceProvider Voluntary = [](const FBattleDecisionRequest& Request, FChoice& Out, FString&)
	{
		if (Request.GetDecisionOwnerTrainerId() == MakeNumericId<FTrainerId>(1))
		{
			Out.Kind = EChoiceKind::Switch;
			Out.PartyTarget = MakePartySlotId(1);
			Out.ActiveTarget = Request.GetActingSlotId();
		}
		else Out = Fight(TEXT("Move.SwordsDance"), EBattleSide::Player);
		return true;
	};
	const FDriveFunction VoluntaryDrive = [Voluntary](FBattleEngine& Engine, FRunEvidence& Ev, FString& Error)
	{
		return LockTurn(Engine, Voluntary, Ev, Error) && ExecuteLockedQueue(Engine, Ev, Error);
	};
	FRunEvidence VoluntaryEvidence;
	TestTrue(TEXT("Voluntary switch twins pass"),
		RunDeterministicTwins(*this, TEXT("voluntary switch"), Fixture.Catalog,
			VoluntarySetup, VoluntaryDrive, &VoluntaryEvidence));
	TestTrue(TEXT("Voluntary switch emits Switched"), HasEvent(VoluntaryEvidence.Replay, EBattleEventType::Switched));
	return ValidateGlobalInvariants(*this, Fixture.Catalog, Evidence, TEXT("switching matrix"))
		&& ValidateGlobalInvariants(*this, Fixture.Catalog, VoluntaryEvidence, TEXT("voluntary switch"))
		&& RunTrappedScenario(*this, Fixture)
		&& RunForcedScenarios(*this, Fixture)
		&& RunShiftScenarios(*this, Fixture);
}

} // namespace BattleCanonicalSingleCorePrivate

#endif // WITH_DEV_AUTOMATION_TESTS
