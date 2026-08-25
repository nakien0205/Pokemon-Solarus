#if WITH_DEV_AUTOMATION_TESTS

#include "Battle/BattleEngine.h"
#include "Battle/BattleEffectExecutor.h"
#include "Battle/BattleReplay.h"
#include "BattleTestFactories.h"
#include "Misc/AutomationTest.h"

namespace BattleReplacementTests
{
	using BattleTest::MakeActiveSlotId;
	using BattleTest::MakeDefinitionId;
	using BattleTest::MakeNumericId;
	using BattleTest::MakePartySlotId;

	constexpr uint64 PlayerTrainerValue = 1;
	constexpr uint64 OpponentTrainerValue = 2;
	constexpr uint64 PartnerTrainerValue = 3;
	constexpr uint64 PlayerLeftBattlerValue = 11;
	constexpr uint64 PlayerRightBattlerValue = 12;
	constexpr uint64 OpponentLeftBattlerValue = 21;
	constexpr uint64 OpponentRightBattlerValue = 22;
	constexpr uint64 OpponentReserveLeftValue = 23;
	constexpr uint64 OpponentReserveRightValue = 24;
	constexpr uint64 PartnerBattlerValue = 31;

	const TCHAR* MoveName = TEXT("Move.C06B.Test");
	const TCHAR* SpeciesName = TEXT("Species.C06B.Test");
	const TCHAR* AbilityName = TEXT("Ability.C06B.Test");

	struct FC06BBattlerFixture
	{
		uint64 TrainerValue = 0;
		uint64 BattlerValue = 0;
		int32 PartyIndex = 0;
		int32 CurrentHP = 200;
		int32 Speed = 100;
		bool bActive = false;
		EBattleSide Side = EBattleSide::Player;
		EBattlePosition Position = EBattlePosition::Left;
		EBattleSide TargetSide = EBattleSide::Opponent;
		EBattlePosition TargetPosition = EBattlePosition::Left;
	};

	struct FC06BScenario
	{
		uint64 BattleValue = 6060;
		EBattleEncounterKind EncounterKind = EBattleEncounterKind::Trainer;
		EBattleFormat Format = EBattleFormat::Single;
		bool bOverrideShiftEligibility = false;
		bool bShiftEligible = true;
		bool bBuffUserBeforeDamage = false;
		TArray<FC06BBattlerFixture> Battlers;
	};

	FBattleMoveEffectDescriptor MakeEffect(
		const int32 Order,
		const EBattleMoveEffectKind Kind,
		const EBattleEffectTarget Target)
	{
		FBattleMoveEffectDescriptor Effect;
		Effect.Order = Order;
		Effect.Kind = Kind;
		Effect.Target = Target;
		return Effect;
	}

	TArray<FBattleTypeChartEntry> MakeNeutralTypeChart()
	{
		TArray<FBattleTypeChartEntry> Entries;
		Entries.Reserve(FBattleTypeChart::EntryCount);
		for (int32 AttackingIndex = 0;
			AttackingIndex < FBattleTypeChart::TypeCount;
			++AttackingIndex)
		{
			for (int32 DefendingIndex = 0;
				DefendingIndex < FBattleTypeChart::TypeCount;
				++DefendingIndex)
			{
				Entries.Add(
					{
						static_cast<EPokemonType>(AttackingIndex),
						static_cast<EPokemonType>(DefendingIndex),
						1,
						1
					});
			}
		}
		return Entries;
	}

	FBattleDefinitionCatalog MakeCatalog(const FC06BScenario& Scenario)
	{
		FBattleMoveDefinition Move;
		Move.Id = MakeDefinitionId<FMoveId>(MoveName);
		Move.Type = EPokemonType::Normal;
		Move.Category = EBattleMoveCategory::Physical;
		Move.Power = 40;
		Move.bAlwaysHits = true;
		Move.Accuracy = 0;
		Move.bUsesPP = true;
		Move.BasePP = 20;
		Move.bAllowsPPBoosts = true;
		Move.Priority = 0;
		Move.TargetClass = EBattleTargetClass::SelectedOpponent;
		Move.Flags = EBattleMoveFlags::NeverCritical;
		if (Scenario.bBuffUserBeforeDamage)
		{
			FBattleMoveEffectDescriptor Stage = MakeEffect(
				0,
				EBattleMoveEffectKind::ModifyStatStage,
				EBattleEffectTarget::User);
			Stage.Stat = EBattleStat::Attack;
			Stage.MagnitudeNumerator = 2;
			Move.Effects.Add(Stage);
		}
		Move.Effects.Add(MakeEffect(
			Scenario.bBuffUserBeforeDamage ? 1 : 0,
			EBattleMoveEffectKind::Damage,
			EBattleEffectTarget::ResolvedTarget));

		FBattleDefinitionCatalogInput Input;
		Input.TypeChartEntries = MakeNeutralTypeChart();
		Input.Moves.Add(Move);
		Input.Abilities.Add({MakeDefinitionId<FAbilityId>(AbilityName)});
		FBattleSpeciesFormDefinition Species;
		Species.Id = MakeDefinitionId<FSpeciesFormId>(SpeciesName);
		Species.PrimaryType = EPokemonType::Normal;
		Species.BaseStats = {80, 80, 80, 80, 80, 80};
		Species.CatchRate = 45;
		Species.AbilityChoices.Add(MakeDefinitionId<FAbilityId>(AbilityName));
		Input.SpeciesForms.Add(Species);

		FBattleDefinitionCatalog Catalog;
		TArray<FBattleCatalogDiagnostic> Diagnostics;
		const bool bCreated = FBattleDefinitionCatalog::TryCreate(
			Input,
			Catalog,
			Diagnostics);
		check(bCreated);
		return Catalog;
	}

	FBattleTrainerSetup MakeTrainer(
		const uint64 TrainerValue,
		const EBattleSide Side,
		const EBattleTrainerRole Role,
		const EBattleDecisionController Controller)
	{
		FBattleTrainerSetup Trainer;
		Trainer.TrainerId = MakeNumericId<FTrainerId>(TrainerValue);
		Trainer.Side = Side;
		Trainer.Role = Role;
		Trainer.Controller = Controller;
		Trainer.SelectorProfileId = MakeDefinitionId<FDefinitionId>(
			Role == EBattleTrainerRole::Player
				? TEXT("Selector.C06B.Player")
				: Role == EBattleTrainerRole::Partner
					? TEXT("Selector.C06B.Partner")
					: TEXT("Selector.C06B.Opponent"));
		return Trainer;
	}

	FBattlePartyEntrySetup MakePartyEntry(const FC06BBattlerFixture& Fixture)
	{
		FBattlePartyEntrySetup Entry;
		Entry.TrainerId = MakeNumericId<FTrainerId>(Fixture.TrainerValue);
		Entry.BattlerId = MakeNumericId<FBattlerId>(Fixture.BattlerValue);
		Entry.SourcePokemonId = MakeNumericId<FSourcePokemonId>(
			1000 + Fixture.BattlerValue);
		Entry.PartySlotId = MakePartySlotId(Fixture.PartyIndex);
		Entry.SpeciesFormId = MakeDefinitionId<FSpeciesFormId>(SpeciesName);
		Entry.Level = 50;
		Entry.Stats = {200, 120, 100, 100, 100, Fixture.Speed};
		Entry.CurrentHP = Fixture.CurrentHP;
		Entry.AbilityId = MakeDefinitionId<FAbilityId>(AbilityName);
		Entry.Moves.Add(
			{
				0,
				MakeDefinitionId<FMoveId>(MoveName),
				20,
				20
			});
		return Entry;
	}

	bool TryMakeSetup(const FC06BScenario& Scenario, FBattleSetup& OutSetup)
	{
		FBattleSetupInput Input;
		Input.BattleId = MakeNumericId<FBattleId>(Scenario.BattleValue);
		Input.SettingsReference =
			{MakeDefinitionId<FDefinitionId>(TEXT("Settings.C06B")), 1};
		Input.CatalogReference =
			{MakeDefinitionId<FDefinitionId>(TEXT("Catalog.C06B")), 1};
		Input.EncounterKind = Scenario.EncounterKind;
		Input.Format = Scenario.Format;
		Input.CaptureCapacity = {2, 100};
		Input.Policies.bBagAllowed = false;
		Input.Policies.bRunAllowed = false;
		Input.Policies.bCaptureAllowed = false;
		if (Scenario.bOverrideShiftEligibility)
		{
			Input.Policies.bShiftPromptEligible = Scenario.bShiftEligible;
		}
		Input.Trainers.Add(MakeTrainer(
			PlayerTrainerValue,
			EBattleSide::Player,
			EBattleTrainerRole::Player,
			EBattleDecisionController::Human));
		Input.Trainers.Add(MakeTrainer(
			OpponentTrainerValue,
			EBattleSide::Opponent,
			EBattleTrainerRole::Opponent,
			EBattleDecisionController::EnemyAI));
		if (Scenario.Format == EBattleFormat::PartnerDouble)
		{
			Input.Trainers.Add(MakeTrainer(
				PartnerTrainerValue,
				EBattleSide::Player,
				EBattleTrainerRole::Partner,
				EBattleDecisionController::PartnerAI));
		}

		for (const FC06BBattlerFixture& Fixture : Scenario.Battlers)
		{
			Input.PartyEntries.Add(MakePartyEntry(Fixture));
			if (Fixture.bActive)
			{
				Input.StartingActive.Add(
					{
						MakeActiveSlotId(Fixture.Side, Fixture.Position),
						MakeNumericId<FTrainerId>(Fixture.TrainerValue),
						MakeNumericId<FBattlerId>(Fixture.BattlerValue)
					});
			}
		}

		EBattleSetupValidationError Error = EBattleSetupValidationError::None;
		return FBattleSetup::TryCreate(Input, OutSetup, Error);
	}

	bool TryCreateEngine(
		const FC06BScenario& Scenario,
		const uint64 Seed,
		TUniquePtr<FBattleEngine>& OutEngine)
	{
		FBattleSetup Setup;
		if (!TryMakeSetup(Scenario, Setup))
		{
			return false;
		}
		FBattleRejection Rejection;
		return FBattleEngine::TryCreate(
			Setup,
			MakeCatalog(Scenario),
			MakeUnique<FSeededBattleRandom>(Seed),
			OutEngine,
			Rejection);
	}

	const FC06BBattlerFixture* FindFixture(
		const FC06BScenario& Scenario,
		const FBattlerId BattlerId)
	{
		return Scenario.Battlers.FindByPredicate(
			[BattlerId](const FC06BBattlerFixture& Fixture)
			{
				return BattlerId.IsValid()
					&& Fixture.BattlerValue == BattlerId.GetValue();
			});
	}

	bool TryMakeBatch(
		const TArray<FBattleDecisionRequest>& Requests,
		TArray<FBattleDecision> Decisions,
		FBattleDecisionBatch& OutBatch)
	{
		if (Requests.IsEmpty())
		{
			return false;
		}
		FBattleDecisionBatchSpec Spec;
		Spec.StateVersion = Requests[0].GetStateVersion();
		Spec.RequestKind = Requests[0].GetRequestKind();
		Spec.DecisionOwnerTrainerId = Requests[0].GetDecisionOwnerTrainerId();
		Spec.Decisions = MoveTemp(Decisions);
		FBattleRejection Rejection;
		return FBattleDecisionBatch::TryCreate(Spec, OutBatch, Rejection);
	}

	bool LockAllFights(FBattleEngine& Engine, const FC06BScenario& Scenario)
	{
		FBattleRejection Rejection;
		if (!Engine.TryBeginActionDecisionSequence(Rejection))
		{
			return false;
		}

		const FMoveId MoveId = MakeDefinitionId<FMoveId>(MoveName);
		int32 Guard = 0;
		while (!Engine.GetPendingDecisionRequests().IsEmpty() && Guard++ < 8)
		{
			const TArray<FBattleDecisionRequest> Requests =
				Engine.GetPendingDecisionRequests();
			TArray<FBattleDecision> Decisions;
			for (const FBattleDecisionRequest& Request : Requests)
			{
				const FC06BBattlerFixture* Fixture = FindFixture(
					Scenario,
					Request.GetActingBattlerId());
				if (Fixture == nullptr)
				{
					return false;
				}
				const FActiveSlotId Target = MakeActiveSlotId(
					Fixture->TargetSide,
					Fixture->TargetPosition);
				const bool bTargetLegal = Request.GetLegalMoveTargets().ContainsByPredicate(
					[MoveId, Target](const FBattleMoveTargetOption& Option)
					{
						return Option.MoveId == MoveId
							&& Option.ActiveSlotId == Target;
					});
				FBattleDecision Decision;
				if (!bTargetLegal
					|| !FBattleDecision::TryCreateFight(
						Request.GetStateVersion(),
						Request.GetDecisionOwnerTrainerId(),
						Request.GetActingBattlerId(),
						MoveId,
						Target,
						Decision))
				{
					return false;
				}
				Decisions.Add(Decision);
			}

			FBattleDecisionBatch Batch;
			if (!TryMakeBatch(Requests, MoveTemp(Decisions), Batch)
				|| !Engine.SubmitDecisionBatch(Batch).WasAccepted())
			{
				return false;
			}
		}
		return Guard < 8 && Engine.GetSnapshot().GetPhase() == EBattlePhase::Locked;
	}

	bool ExecuteNextQueuedAction(FBattleEngine& Engine)
	{
		if (!Engine.BeginNextLockedAction().WasAccepted())
		{
			return false;
		}
		if (!Engine.GetCurrentLockedAction().IsSet())
		{
			return true;
		}
		if (!Engine.CommitCurrentMoveAfterPreMoveGates().WasAccepted())
		{
			return false;
		}
		if (!Engine.GetCurrentLockedAction().IsSet())
		{
			return true;
		}
		if (!Engine.ResolveCurrentMoveTargets().WasAccepted())
		{
			return false;
		}
		if (!Engine.GetCurrentLockedAction().IsSet())
		{
			return true;
		}
		return Engine.ExecuteCurrentMoveEffects().WasAccepted();
	}

	bool FinishQueue(FBattleEngine& Engine)
	{
		int32 Guard = 0;
		while ((Engine.GetSnapshot().GetPhase() == EBattlePhase::Locked
				|| Engine.GetSnapshot().GetPhase() == EBattlePhase::Resolving)
			&& Guard++ < 16)
		{
			if (!ExecuteNextQueuedAction(Engine))
			{
				return false;
			}
		}
		return Guard < 16;
	}

	bool ReachReplacementBoundary(
		const FC06BScenario& Scenario,
		const uint64 Seed,
		TUniquePtr<FBattleEngine>& OutEngine)
	{
		return TryCreateEngine(Scenario, Seed, OutEngine)
			&& OutEngine.IsValid()
			&& LockAllFights(*OutEngine, Scenario)
			&& FinishQueue(*OutEngine);
	}

	bool TryMakeReplacementBatch(
		const TArray<FBattleDecisionRequest>& Requests,
		const TArray<int32>& PartyIndexes,
		FBattleDecisionBatch& OutBatch)
	{
		if (Requests.Num() != PartyIndexes.Num())
		{
			return false;
		}
		TArray<FBattleDecision> Decisions;
		for (int32 Index = 0; Index < Requests.Num(); ++Index)
		{
			FBattleDecision Decision;
			if (!FBattleDecision::TryCreateReplacement(
				Requests[Index].GetStateVersion(),
				Requests[Index].GetDecisionOwnerTrainerId(),
				MakePartySlotId(PartyIndexes[Index]),
				Requests[Index].GetActingSlotId(),
				Decision))
			{
				return false;
			}
			Decisions.Add(Decision);
		}
		return TryMakeBatch(Requests, MoveTemp(Decisions), OutBatch);
	}

	uint64 GetActiveBattlerValue(
		const FBattleSnapshot& Snapshot,
		const EBattleSide Side,
		const EBattlePosition Position)
	{
		const FActiveSlotId SlotId = MakeActiveSlotId(Side, Position);
		const FBattleActiveAssignment* Assignment =
			Snapshot.GetActiveAssignments().FindByPredicate(
				[SlotId](const FBattleActiveAssignment& Candidate)
				{
					return Candidate.ActiveSlotId == SlotId;
				});
		return Assignment != nullptr && Assignment->BattlerId.IsValid()
			? Assignment->BattlerId.GetValue()
			: 0;
	}

	FString ActiveSignature(const FBattleSnapshot& Snapshot)
	{
		FString Result;
		for (const FBattleActiveAssignment& Assignment : Snapshot.GetActiveAssignments())
		{
			Result += FString::Printf(
				TEXT("%u:%u:%llu:%llu|"),
				static_cast<uint8>(Assignment.ActiveSlotId.GetSide()),
				static_cast<uint8>(Assignment.ActiveSlotId.GetPosition()),
				Assignment.TrainerId.GetValue(),
				Assignment.BattlerId.GetValue());
		}
		return Result;
	}

	TArray<FBattleEvent> CollectEvents(const FBattleEngine& Engine)
	{
		TArray<FBattleEvent> Events;
		const FBattleReplayRecord Record = Engine.ExportReplayRecord();
		for (const FBattleResolution& Resolution : Record.GetResolutions())
		{
			for (const FBattleEvent& Event : Resolution.GetEvents())
			{
				Events.Add(Event);
			}
		}
		return Events;
	}

	int32 FindEventIndex(
		const TArray<FBattleEvent>& Events,
		const EBattleEventType Type,
		const bool bFindLast = false)
	{
		if (bFindLast)
		{
			for (int32 Index = Events.Num() - 1; Index >= 0; --Index)
			{
				if (Events[Index].GetType() == Type)
				{
					return Index;
				}
			}
			return INDEX_NONE;
		}
		for (int32 Index = 0; Index < Events.Num(); ++Index)
		{
			if (Events[Index].GetType() == Type)
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

	int32 CountTargetEvents(
		const TArray<FBattleEvent>& Events,
		const EBattleEventType Type,
		const uint64 BattlerValue)
	{
		int32 Count = 0;
		for (const FBattleEvent& Event : Events)
		{
			if (Event.GetType() == Type
				&& Event.GetTargets().ContainsByPredicate(
					[BattlerValue](const FBattleEventTarget& Target)
					{
						return Target.BattlerId.IsValid()
							&& Target.BattlerId.GetValue() == BattlerValue;
					}))
			{
				++Count;
			}
		}
		return Count;
	}

	FC06BScenario MakeSingleOpponentFaintScenario(
		const bool bPlayerReserve,
		const bool bOpponentReserve,
		const uint64 BattleValue)
	{
		FC06BScenario Scenario;
		Scenario.BattleValue = BattleValue;
		Scenario.Battlers.Add(
			{PlayerTrainerValue, PlayerLeftBattlerValue, 0, 200, 400, true,
				EBattleSide::Player, EBattlePosition::Left,
				EBattleSide::Opponent, EBattlePosition::Left});
		if (bPlayerReserve)
		{
			Scenario.Battlers.Add(
				{PlayerTrainerValue, PlayerRightBattlerValue, 1, 200, 200, false});
		}
		Scenario.Battlers.Add(
			{OpponentTrainerValue, OpponentLeftBattlerValue, 0, 1, 100, true,
				EBattleSide::Opponent, EBattlePosition::Left,
				EBattleSide::Player, EBattlePosition::Left});
		if (bOpponentReserve)
		{
			Scenario.Battlers.Add(
				{OpponentTrainerValue, OpponentRightBattlerValue, 1, 200, 150, false});
		}
		return Scenario;
	}

	FC06BScenario MakeSinglePlayerFaintScenario(const uint64 BattleValue)
	{
		FC06BScenario Scenario;
		Scenario.BattleValue = BattleValue;
		Scenario.Battlers.Add(
			{PlayerTrainerValue, PlayerLeftBattlerValue, 0, 1, 100, true,
				EBattleSide::Player, EBattlePosition::Left,
				EBattleSide::Opponent, EBattlePosition::Left});
		Scenario.Battlers.Add(
			{PlayerTrainerValue, PlayerRightBattlerValue, 1, 200, 200, false});
		Scenario.Battlers.Add(
			{OpponentTrainerValue, OpponentLeftBattlerValue, 0, 200, 400, true,
				EBattleSide::Opponent, EBattlePosition::Left,
				EBattleSide::Player, EBattlePosition::Left});
		return Scenario;
	}

	FC06BScenario MakeDoubleOpponentFaintScenario(
		const bool bFaintBoth,
		const int32 ReserveCount,
		const bool bPartnerDouble,
		const uint64 BattleValue)
	{
		FC06BScenario Scenario;
		Scenario.BattleValue = BattleValue;
		Scenario.Format = bPartnerDouble
			? EBattleFormat::PartnerDouble
			: EBattleFormat::Double;
		Scenario.Battlers.Add(
			{PlayerTrainerValue, PlayerLeftBattlerValue, 0, 200, 400, true,
				EBattleSide::Player, EBattlePosition::Left,
				EBattleSide::Opponent, EBattlePosition::Left});
		Scenario.Battlers.Add(
			{bPartnerDouble ? PartnerTrainerValue : PlayerTrainerValue,
				bPartnerDouble ? PartnerBattlerValue : PlayerRightBattlerValue,
				bPartnerDouble ? 0 : 1,
				200,
				350,
				true,
				EBattleSide::Player,
				EBattlePosition::Right,
				EBattleSide::Opponent,
				EBattlePosition::Right});
		Scenario.Battlers.Add(
			{OpponentTrainerValue, OpponentLeftBattlerValue, 0, 1, 100, true,
				EBattleSide::Opponent, EBattlePosition::Left,
				EBattleSide::Player, EBattlePosition::Left});
		Scenario.Battlers.Add(
			{OpponentTrainerValue, OpponentRightBattlerValue, 1,
				bFaintBoth ? 1 : 200, 90, true,
				EBattleSide::Opponent, EBattlePosition::Right,
				EBattleSide::Player, EBattlePosition::Right});
		if (ReserveCount >= 1)
		{
			Scenario.Battlers.Add(
				{OpponentTrainerValue, OpponentReserveLeftValue, 2, 200, 160, false});
		}
		if (ReserveCount >= 2)
		{
			Scenario.Battlers.Add(
				{OpponentTrainerValue, OpponentReserveRightValue, 3, 200, 170, false});
		}
		return Scenario;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FC06BShiftSetNormalizationTest,
	"PokemonSolarus.Battle.C06B.Setup.ShiftSetNormalization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FC06BShiftSetNormalizationTest::RunTest(const FString& Parameters)
{
	using namespace BattleReplacementTests;
	(void)Parameters;

	FBattleSetup DefaultShift;
	const FC06BScenario DefaultScenario = MakeSingleOpponentFaintScenario(true, true, 606001);
	TestTrue(TEXT("A default Single Trainer setup is valid"),
		TryMakeSetup(DefaultScenario, DefaultShift));
	if (DefaultShift.IsValid())
	{
		TestTrue(TEXT("Single Trainer defaults to Shift"),
			DefaultShift.GetPolicies().bShiftPromptEligible);
	}

	FC06BScenario ForcedSetScenario = DefaultScenario;
	ForcedSetScenario.BattleValue = 606002;
	ForcedSetScenario.bOverrideShiftEligibility = true;
	ForcedSetScenario.bShiftEligible = false;
	FBattleSetup ForcedSet;
	TestTrue(TEXT("An explicit forced-Set setup is valid"),
		TryMakeSetup(ForcedSetScenario, ForcedSet));
	if (ForcedSet.IsValid())
	{
		TestFalse(TEXT("Explicit false remains forced Set"),
			ForcedSet.GetPolicies().bShiftPromptEligible);
	}

	FC06BScenario WildScenario = MakeSingleOpponentFaintScenario(true, false, 606003);
	WildScenario.EncounterKind = EBattleEncounterKind::Wild;
	WildScenario.bOverrideShiftEligibility = true;
	WildScenario.bShiftEligible = true;
	FBattleSetup WildSetup;
	TestTrue(TEXT("The Wild setup is valid"), TryMakeSetup(WildScenario, WildSetup));
	if (WildSetup.IsValid())
	{
		TestFalse(TEXT("Wild forces Set during canonicalization"),
			WildSetup.GetPolicies().bShiftPromptEligible);
	}

	FC06BScenario DoubleScenario = MakeDoubleOpponentFaintScenario(false, 1, false, 606004);
	DoubleScenario.bOverrideShiftEligibility = true;
	DoubleScenario.bShiftEligible = true;
	FBattleSetup DoubleSetup;
	TestTrue(TEXT("The Double setup is valid"), TryMakeSetup(DoubleScenario, DoubleSetup));
	if (DoubleSetup.IsValid())
	{
		TestFalse(TEXT("Double forces Set during canonicalization"),
			DoubleSetup.GetPolicies().bShiftPromptEligible);
	}

	FC06BScenario PartnerScenario = MakeDoubleOpponentFaintScenario(false, 1, true, 606005);
	PartnerScenario.bOverrideShiftEligibility = true;
	PartnerScenario.bShiftEligible = true;
	FBattleSetup PartnerSetup;
	TestTrue(TEXT("The Partner Double setup is valid"),
		TryMakeSetup(PartnerScenario, PartnerSetup));
	if (PartnerSetup.IsValid())
	{
		TestFalse(TEXT("Partner Double forces Set during canonicalization"),
			PartnerSetup.GetPolicies().bShiftPromptEligible);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FC06BSingleMandatoryReplacementTest,
	"PokemonSolarus.Battle.C06B.Single.MandatoryReplacementIsFree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FC06BSingleMandatoryReplacementTest::RunTest(const FString& Parameters)
{
	using namespace BattleReplacementTests;
	(void)Parameters;

	TUniquePtr<FBattleEngine> Engine;
	TestTrue(TEXT("The player-faint scenario reaches replacement"),
		ReachReplacementBoundary(MakeSinglePlayerFaintScenario(606101), 606101, Engine));
	if (!Engine.IsValid())
	{
		return false;
	}

	const TArray<FBattleDecisionRequest> Requests = Engine->GetPendingDecisionRequests();
	TestEqual(TEXT("Exactly one player replacement is requested"), Requests.Num(), 1);
	if (Requests.Num() != 1)
	{
		return false;
	}
	TestEqual(TEXT("The request is mandatory replacement"),
		Requests[0].GetRequestKind(), EBattleDecisionRequestKind::MandatoryReplacement);
	TestFalse(TEXT("Mandatory replacement is actorless"),
		Requests[0].GetActingBattlerId().IsValid());
	TestTrue(TEXT("The empty destination is player Left"),
		Requests[0].GetActingSlotId()
			== MakeActiveSlotId(EBattleSide::Player, EBattlePosition::Left));

	const int32 LockedCountBefore = Engine->GetLockedActions().Num();
	const TArray<FBattleRandomDraw> RandomBefore = Engine->ExportRandomTrace();
	FBattleDecision Replacement;
	TestTrue(TEXT("The actorless replacement decision is created"),
		FBattleDecision::TryCreateReplacement(
			Requests[0].GetStateVersion(),
			Requests[0].GetDecisionOwnerTrainerId(),
			MakePartySlotId(1),
			Requests[0].GetActingSlotId(),
			Replacement));
	const FBattleResolution Resolution = Engine->SubmitDecision(Replacement);
	TestTrue(TEXT("The replacement is accepted"), Resolution.WasAccepted());
	TestEqual(TEXT("Replacement emits exactly three facts"),
		Resolution.GetEvents().Num(), 3);
	if (Resolution.GetEvents().Num() == 3)
	{
		TestEqual(TEXT("Replacement accepts the decision first"),
			Resolution.GetEvents()[0].GetType(), EBattleEventType::DecisionAccepted);
		TestEqual(TEXT("Replacement enters the slot second"),
			Resolution.GetEvents()[1].GetType(), EBattleEventType::EnteredActiveSlot);
		TestEqual(TEXT("Replacement exposes the entry trigger third"),
			Resolution.GetEvents()[2].GetType(), EBattleEventType::Switched);
	}
	for (const FBattleEvent& Event : Resolution.GetEvents())
	{
		TestFalse(TEXT("Replacement facts have no action ID"), Event.GetActionId().IsValid());
		TestFalse(TEXT("Replacement emits no action lifecycle fact"),
			Event.GetType() == EBattleEventType::ActionOrderLocked
				|| Event.GetType() == EBattleEventType::ActionLocked
				|| Event.GetType() == EBattleEventType::ActionStarted
				|| Event.GetType() == EBattleEventType::ActionCompleted);
	}
	TestEqual(TEXT("Replacement adds no locked queue entry"),
		Engine->GetLockedActions().Num(), LockedCountBefore);
	TestTrue(TEXT("Replacement consumes no RNG"),
		Engine->ExportRandomTrace() == RandomBefore);
	TestEqual(TEXT("The free replacement ends at EndOfTurn"),
		Engine->GetSnapshot().GetPhase(), EBattlePhase::EndOfTurn);
	TestEqual(TEXT("Player reserve now occupies Left"),
		GetActiveBattlerValue(
			Engine->GetSnapshot(),
			EBattleSide::Player,
			EBattlePosition::Left),
		PlayerRightBattlerValue);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FC06BDoubleQueueBoundaryTest,
	"PokemonSolarus.Battle.C06B.Double.QueueCompletesBeforeReplacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FC06BDoubleQueueBoundaryTest::RunTest(const FString& Parameters)
{
	using namespace BattleReplacementTests;
	(void)Parameters;

	const FC06BScenario Scenario = MakeDoubleOpponentFaintScenario(false, 1, false, 606201);
	TUniquePtr<FBattleEngine> Engine;
	TestTrue(TEXT("The one-faint Double engine is created"),
		TryCreateEngine(Scenario, 606201, Engine));
	TestTrue(TEXT("The full Double action queue locks"),
		Engine.IsValid() && LockAllFights(*Engine, Scenario));
	if (!Engine.IsValid())
	{
		return false;
	}
	const TArray<FBattleLockedAction> Locked = Engine->GetLockedActions();
	TestEqual(TEXT("Four Double actions were locked"), Locked.Num(), 4);
	TestTrue(TEXT("The intended KO action is first"),
		!Locked.IsEmpty()
			&& Locked[0].Decision.GetActingBattlerId()
				== MakeNumericId<FBattlerId>(PlayerLeftBattlerValue));

	TestTrue(TEXT("The KO action resolves"), ExecuteNextQueuedAction(*Engine));
	TestEqual(TEXT("The queue keeps resolving after the faint"),
		Engine->GetSnapshot().GetPhase(), EBattlePhase::Resolving);
	TestTrue(TEXT("No replacement request appears mid-queue"),
		Engine->GetPendingDecisionRequests().IsEmpty());
	TestTrue(TEXT("The remaining queued actions finish"), FinishQueue(*Engine));

	const TArray<FBattleDecisionRequest> Requests = Engine->GetPendingDecisionRequests();
	TestEqual(TEXT("Only opponent Left needs replacement"), Requests.Num(), 1);
	if (Requests.Num() == 1)
	{
		TestTrue(TEXT("The request destination is opponent Left"),
			Requests[0].GetActingSlotId()
				== MakeActiveSlotId(EBattleSide::Opponent, EBattlePosition::Left));
	}
	const TArray<FBattleEvent> Events = CollectEvents(*Engine);
	const int32 LastCompleted = FindEventIndex(Events, EBattleEventType::ActionCompleted, true);
	const int32 ReplacementRequired = FindEventIndex(Events, EBattleEventType::ReplacementRequired);
	TestTrue(TEXT("ReplacementRequired follows every queued ActionCompleted"),
		LastCompleted != INDEX_NONE
			&& ReplacementRequired != INDEX_NONE
			&& ReplacementRequired > LastCompleted);

	FBattleDecisionBatch ReplacementBatch;
	TestTrue(TEXT("The one-slot replacement batch is created"),
		TryMakeReplacementBatch(Requests, {2}, ReplacementBatch));
	TestTrue(TEXT("The queued replacement resolves"),
		Engine->SubmitDecisionBatch(ReplacementBatch).WasAccepted());
	TestEqual(TEXT("The Double battle stops at EndOfTurn"),
		Engine->GetSnapshot().GetPhase(), EBattlePhase::EndOfTurn);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FC06BAtomicTwoSlotReplacementTest,
	"PokemonSolarus.Battle.C06B.Double.AtomicTwoSlotReplacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FC06BAtomicTwoSlotReplacementTest::RunTest(const FString& Parameters)
{
	using namespace BattleReplacementTests;
	(void)Parameters;

	TUniquePtr<FBattleEngine> Engine;
	TestTrue(TEXT("The two-faint Double scenario reaches replacement"),
		ReachReplacementBoundary(
			MakeDoubleOpponentFaintScenario(true, 2, false, 606301),
			606301,
			Engine));
	if (!Engine.IsValid())
	{
		return false;
	}
	const TArray<FBattleDecisionRequest> Requests = Engine->GetPendingDecisionRequests();
	TestEqual(TEXT("Both empty opponent slots are requested atomically"), Requests.Num(), 2);
	if (Requests.Num() != 2)
	{
		return false;
	}
	TestTrue(TEXT("The first request is Left"),
		Requests[0].GetActingSlotId()
			== MakeActiveSlotId(EBattleSide::Opponent, EBattlePosition::Left));
	TestTrue(TEXT("The second request is Right"),
		Requests[1].GetActingSlotId()
			== MakeActiveSlotId(EBattleSide::Opponent, EBattlePosition::Right));

	FBattleDecision DuplicateLeft;
	FBattleDecision DuplicateRight;
	TestTrue(TEXT("The duplicate Left replacement decision is individually valid"),
		FBattleDecision::TryCreateReplacement(
			Requests[0].GetStateVersion(),
			Requests[0].GetDecisionOwnerTrainerId(),
			MakePartySlotId(2),
			Requests[0].GetActingSlotId(),
			DuplicateLeft));
	TestTrue(TEXT("The duplicate Right replacement decision is individually valid"),
		FBattleDecision::TryCreateReplacement(
			Requests[1].GetStateVersion(),
			Requests[1].GetDecisionOwnerTrainerId(),
			MakePartySlotId(2),
			Requests[1].GetActingSlotId(),
			DuplicateRight));
	FBattleDecisionBatchSpec DuplicateSpec;
	DuplicateSpec.StateVersion = Requests[0].GetStateVersion();
	DuplicateSpec.RequestKind = EBattleDecisionRequestKind::MandatoryReplacement;
	DuplicateSpec.DecisionOwnerTrainerId = Requests[0].GetDecisionOwnerTrainerId();
	DuplicateSpec.Decisions = {DuplicateLeft, DuplicateRight};
	FBattleDecisionBatch DuplicateBatch;
	FBattleRejection DuplicateRejection;
	TestFalse(TEXT("A duplicate reserve is rejected by the atomic batch factory"),
		FBattleDecisionBatch::TryCreate(
			DuplicateSpec,
			DuplicateBatch,
			DuplicateRejection));
	TestEqual(TEXT("The duplicate batch has a typed batch rejection"),
		DuplicateRejection.Reason, EBattleRejectionReason::InvalidDecisionBatch);

	const FBattleSnapshot BeforeRejected = Engine->GetSnapshot();
	const FString OccupancyBefore = ActiveSignature(BeforeRejected);
	const TArray<FBattleRandomDraw> RandomBefore = Engine->ExportRandomTrace();
	const FBattleResolution Rejected = Engine->SubmitDecisionBatch(DuplicateBatch);
	TestFalse(TEXT("Submitting the invalid duplicate batch is rejected"),
		Rejected.WasAccepted());
	TestEqual(TEXT("Duplicate rejection preserves gameplay state version"),
		Engine->GetSnapshot().GetStateVersion(), BeforeRejected.GetStateVersion());
	TestEqual(TEXT("Duplicate rejection preserves occupancy"),
		ActiveSignature(Engine->GetSnapshot()), OccupancyBefore);
	TestTrue(TEXT("Duplicate rejection preserves RNG trace"),
		Engine->ExportRandomTrace() == RandomBefore);
	TestEqual(TEXT("Duplicate rejection preserves both pending requests"),
		Engine->GetPendingDecisionRequests().Num(), 2);

	FBattleDecisionBatch ValidBatch;
	TestTrue(TEXT("Distinct Left and Right reserves form one batch"),
		TryMakeReplacementBatch(Requests, {2, 3}, ValidBatch));
	const FBattleResolution Accepted = Engine->SubmitDecisionBatch(ValidBatch);
	TestTrue(TEXT("The two-slot replacement batch is accepted atomically"),
		Accepted.WasAccepted());
	TestEqual(TEXT("Two replacements emit six ordered facts"),
		Accepted.GetEvents().Num(), 6);
	if (Accepted.GetEvents().Num() == 6)
	{
		TestEqual(TEXT("Both decisions are accepted before entries (first)"),
			Accepted.GetEvents()[0].GetType(), EBattleEventType::DecisionAccepted);
		TestEqual(TEXT("Both decisions are accepted before entries (second)"),
			Accepted.GetEvents()[1].GetType(), EBattleEventType::DecisionAccepted);
		TestEqual(TEXT("Left enters before Right"),
			Accepted.GetEvents()[2].GetTargets()[0].ActiveSlotId,
			MakeActiveSlotId(EBattleSide::Opponent, EBattlePosition::Left));
		TestEqual(TEXT("Right enters after Left"),
			Accepted.GetEvents()[4].GetTargets()[0].ActiveSlotId,
			MakeActiveSlotId(EBattleSide::Opponent, EBattlePosition::Right));
	}
	TestEqual(TEXT("Left receives the first selected reserve"),
		GetActiveBattlerValue(
			Engine->GetSnapshot(),
			EBattleSide::Opponent,
			EBattlePosition::Left),
		OpponentReserveLeftValue);
	TestEqual(TEXT("Right receives the second selected reserve"),
		GetActiveBattlerValue(
			Engine->GetSnapshot(),
			EBattleSide::Opponent,
			EBattlePosition::Right),
		OpponentReserveRightValue);
	TestEqual(TEXT("Atomic replacement ends at EndOfTurn"),
		Engine->GetSnapshot().GetPhase(), EBattlePhase::EndOfTurn);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FC06BLoneReserveLeftFallbackTest,
	"PokemonSolarus.Battle.C06B.Double.LoneReserveUsesLeft",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FC06BLoneReserveLeftFallbackTest::RunTest(const FString& Parameters)
{
	using namespace BattleReplacementTests;
	(void)Parameters;

	TUniquePtr<FBattleEngine> Engine;
	TestTrue(TEXT("The lone-reserve Double scenario reaches replacement"),
		ReachReplacementBoundary(
			MakeDoubleOpponentFaintScenario(true, 1, false, 606401),
			606401,
			Engine));
	if (!Engine.IsValid())
	{
		return false;
	}
	const TArray<FBattleDecisionRequest> Requests = Engine->GetPendingDecisionRequests();
	TestEqual(TEXT("Only one replacement is requested for one reserve"), Requests.Num(), 1);
	if (Requests.Num() != 1)
	{
		return false;
	}
	TestTrue(TEXT("The lone reserve is allocated to Left"),
		Requests[0].GetActingSlotId()
			== MakeActiveSlotId(EBattleSide::Opponent, EBattlePosition::Left));
	FBattleDecisionBatch Batch;
	TestTrue(TEXT("The lone Left replacement batch is created"),
		TryMakeReplacementBatch(Requests, {2}, Batch));
	TestTrue(TEXT("The lone Left replacement is accepted"),
		Engine->SubmitDecisionBatch(Batch).WasAccepted());
	const FBattleSnapshot Snapshot = Engine->GetSnapshot();
	TestEqual(TEXT("The reserve occupies opponent Left"),
		GetActiveBattlerValue(Snapshot, EBattleSide::Opponent, EBattlePosition::Left),
		OpponentReserveLeftValue);
	TestEqual(TEXT("Opponent Right remains empty"),
		GetActiveBattlerValue(Snapshot, EBattleSide::Opponent, EBattlePosition::Right),
		static_cast<uint64>(0));
	TestEqual(TEXT("The lone-reserve path ends at EndOfTurn"),
		Snapshot.GetPhase(), EBattlePhase::EndOfTurn);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FC06BShiftAcceptDeclineTest,
	"PokemonSolarus.Battle.C06B.Shift.AcceptAndDecline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FC06BShiftAcceptDeclineTest::RunTest(const FString& Parameters)
{
	using namespace BattleReplacementTests;
	(void)Parameters;

	FC06BScenario AcceptScenario = MakeSingleOpponentFaintScenario(true, true, 606501);
	AcceptScenario.bBuffUserBeforeDamage = true;
	TUniquePtr<FBattleEngine> AcceptEngine;
	TestTrue(TEXT("The Shift-accept scenario reaches its prompt"),
		ReachReplacementBoundary(AcceptScenario, 606501, AcceptEngine));
	if (!AcceptEngine.IsValid())
	{
		return false;
	}
	const TArray<FBattleDecisionRequest> AcceptRequests =
		AcceptEngine->GetPendingDecisionRequests();
	TestEqual(TEXT("Shift is offered before opponent replacement"),
		AcceptRequests.Num(), 1);
	if (AcceptRequests.Num() != 1)
	{
		return false;
	}
	TestEqual(TEXT("The first request is ShiftResponse"),
		AcceptRequests[0].GetRequestKind(), EBattleDecisionRequestKind::ShiftResponse);

	int32 AttackStageBefore = 0;
	const FBattleSnapshot BeforeShift = AcceptEngine->GetSnapshotForObserver(
		MakeNumericId<FTrainerId>(PlayerTrainerValue));
	const FBattleObservedBattler* OutgoingBefore =
		BeforeShift.GetObservedBattlers().FindByPredicate(
			[](const FBattleObservedBattler& Battler)
			{
				return Battler.BattlerId
					== MakeNumericId<FBattlerId>(PlayerLeftBattlerValue);
			});
	TestTrue(TEXT("The outgoing player is observable before Shift"),
		OutgoingBefore != nullptr);
	TestTrue(TEXT("The KO move raised the outgoing Attack stage"),
		OutgoingBefore != nullptr
			&& OutgoingBefore->StatStages.TryGetStage(
				EBattleStat::Attack,
				AttackStageBefore)
			&& AttackStageBefore == 2);

	const TArray<FBattleRandomDraw> RandomBeforeShift =
		AcceptEngine->ExportRandomTrace();
	const int32 LockedBeforeShift = AcceptEngine->GetLockedActions().Num();
	FBattleDecision Accept;
	TestTrue(TEXT("The typed Shift accept is created"),
		FBattleDecision::TryCreateShiftSwitch(
			AcceptRequests[0].GetStateVersion(),
			AcceptRequests[0].GetDecisionOwnerTrainerId(),
			AcceptRequests[0].GetActingBattlerId(),
			MakePartySlotId(1),
			AcceptRequests[0].GetActingSlotId(),
			Accept));
	const FBattleResolution AcceptedShift = AcceptEngine->SubmitDecision(Accept);
	TestTrue(TEXT("Shift accept is accepted"), AcceptedShift.WasAccepted());
	TestEqual(TEXT("Shift accept emits the five cleanup facts"),
		AcceptedShift.GetEvents().Num(), 5);
	const TArray<EBattleEventType> ExpectedShiftEvents =
	{
		EBattleEventType::DecisionAccepted,
		EBattleEventType::LeftActiveSlot,
		EBattleEventType::SwitchTransientStateCleared,
		EBattleEventType::EnteredActiveSlot,
		EBattleEventType::Switched
	};
	for (int32 Index = 0;
		Index < AcceptedShift.GetEvents().Num() && Index < ExpectedShiftEvents.Num();
		++Index)
	{
		TestEqual(TEXT("Shift cleanup event order is exact"),
			AcceptedShift.GetEvents()[Index].GetType(), ExpectedShiftEvents[Index]);
		TestFalse(TEXT("Shift cleanup is actionless"),
			AcceptedShift.GetEvents()[Index].GetActionId().IsValid());
	}
	TestEqual(TEXT("Shift adds no locked action"),
		AcceptEngine->GetLockedActions().Num(), LockedBeforeShift);
	TestTrue(TEXT("Shift consumes no RNG"),
		AcceptEngine->ExportRandomTrace() == RandomBeforeShift);
	TestEqual(TEXT("The selected player reserve is now active"),
		GetActiveBattlerValue(
			AcceptEngine->GetSnapshot(),
			EBattleSide::Player,
			EBattlePosition::Left),
		PlayerRightBattlerValue);

	int32 AttackStageAfter = 99;
	const FBattleSnapshot AfterShift = AcceptEngine->GetSnapshotForObserver(
		MakeNumericId<FTrainerId>(PlayerTrainerValue));
	const FBattleObservedBattler* OutgoingAfter =
		AfterShift.GetObservedBattlers().FindByPredicate(
			[](const FBattleObservedBattler& Battler)
			{
				return Battler.BattlerId
					== MakeNumericId<FBattlerId>(PlayerLeftBattlerValue);
			});
	TestTrue(TEXT("Shift clears the outgoing Attack stage"),
		OutgoingAfter != nullptr
			&& OutgoingAfter->StatStages.TryGetStage(
				EBattleStat::Attack,
				AttackStageAfter)
			&& AttackStageAfter == 0);
	const TArray<FBattleDecisionRequest> AfterAcceptRequests =
		AcceptEngine->GetPendingDecisionRequests();
	TestEqual(TEXT("Opponent replacement follows accepted Shift"),
		AfterAcceptRequests.Num(), 1);
	if (AfterAcceptRequests.Num() == 1)
	{
		TestEqual(TEXT("The follow-up is mandatory replacement"),
			AfterAcceptRequests[0].GetRequestKind(),
			EBattleDecisionRequestKind::MandatoryReplacement);
		FBattleDecisionBatch OpponentBatch;
		TestTrue(TEXT("Opponent replacement after accepted Shift is created"),
			TryMakeReplacementBatch(AfterAcceptRequests, {1}, OpponentBatch));
		TestTrue(TEXT("Opponent replacement after accepted Shift resolves"),
			AcceptEngine->SubmitDecisionBatch(OpponentBatch).WasAccepted());
	}

	FC06BScenario DeclineScenario = MakeSingleOpponentFaintScenario(true, true, 606502);
	TUniquePtr<FBattleEngine> DeclineEngine;
	TestTrue(TEXT("The Shift-decline scenario reaches its prompt"),
		ReachReplacementBoundary(DeclineScenario, 606502, DeclineEngine));
	if (!DeclineEngine.IsValid())
	{
		return false;
	}
	const TArray<FBattleDecisionRequest> DeclineRequests =
		DeclineEngine->GetPendingDecisionRequests();
	TestEqual(TEXT("The decline run exposes one Shift request"),
		DeclineRequests.Num(), 1);
	if (DeclineRequests.Num() != 1)
	{
		return false;
	}
	FBattleDecision Decline;
	TestTrue(TEXT("The typed Shift decline is created"),
		FBattleDecision::TryCreateShiftDecline(
			DeclineRequests[0].GetStateVersion(),
			DeclineRequests[0].GetDecisionOwnerTrainerId(),
			DeclineRequests[0].GetActingBattlerId(),
			Decline));
	const FBattleResolution DeclinedShift = DeclineEngine->SubmitDecision(Decline);
	TestTrue(TEXT("Shift decline is accepted"), DeclinedShift.WasAccepted());
	TestEqual(TEXT("Shift decline emits only DecisionAccepted"),
		DeclinedShift.GetEvents().Num(), 1);
	if (DeclinedShift.GetEvents().Num() == 1)
	{
		TestEqual(TEXT("Decline fact is DecisionAccepted"),
			DeclinedShift.GetEvents()[0].GetType(), EBattleEventType::DecisionAccepted);
		TestFalse(TEXT("Decline fact has no action ID"),
			DeclinedShift.GetEvents()[0].GetActionId().IsValid());
	}
	TestEqual(TEXT("Declining leaves the player active unchanged"),
		GetActiveBattlerValue(
			DeclineEngine->GetSnapshot(),
			EBattleSide::Player,
			EBattlePosition::Left),
		PlayerLeftBattlerValue);
	const TArray<FBattleDecisionRequest> AfterDeclineRequests =
		DeclineEngine->GetPendingDecisionRequests();
	TestEqual(TEXT("Opponent replacement follows declined Shift"),
		AfterDeclineRequests.Num(), 1);
	if (AfterDeclineRequests.Num() == 1)
	{
		TestEqual(TEXT("Decline follow-up is mandatory replacement"),
			AfterDeclineRequests[0].GetRequestKind(),
			EBattleDecisionRequestKind::MandatoryReplacement);
		FBattleDecisionBatch OpponentBatch;
		TestTrue(TEXT("Opponent replacement after decline is created"),
			TryMakeReplacementBatch(AfterDeclineRequests, {1}, OpponentBatch));
		TestTrue(TEXT("Opponent replacement after decline resolves"),
			DeclineEngine->SubmitDecisionBatch(OpponentBatch).WasAccepted());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FC06BShiftExclusionsTest,
	"PokemonSolarus.Battle.C06B.Shift.UnsupportedAndTerminalOutcomes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FC06BShiftExclusionsTest::RunTest(const FString& Parameters)
{
	using namespace BattleReplacementTests;
	(void)Parameters;

	auto TestDirectMandatory = [this](
		const FC06BScenario& Scenario,
		const uint64 Seed,
		const TCHAR* Label)
	{
		TUniquePtr<FBattleEngine> Engine;
		const bool bReached = ReachReplacementBoundary(Scenario, Seed, Engine);
		TestTrue(Label, bReached);
		if (!bReached || !Engine.IsValid())
		{
			return;
		}
		const TArray<FBattleDecisionRequest> Requests =
			Engine->GetPendingDecisionRequests();
		TestTrue(TEXT("An unsupported Shift case still exposes its replacement"),
			!Requests.IsEmpty());
		for (const FBattleDecisionRequest& Request : Requests)
		{
			TestEqual(TEXT("Unsupported Shift goes directly to mandatory replacement"),
				Request.GetRequestKind(),
				EBattleDecisionRequestKind::MandatoryReplacement);
		}
	};

	FC06BScenario ForcedSet = MakeSingleOpponentFaintScenario(true, true, 606601);
	ForcedSet.bOverrideShiftEligibility = true;
	ForcedSet.bShiftEligible = false;
	TestDirectMandatory(ForcedSet, 606601, TEXT("Forced Set reaches replacement"));

	FC06BScenario Wild = MakeSinglePlayerFaintScenario(606602);
	Wild.EncounterKind = EBattleEncounterKind::Wild;
	Wild.bOverrideShiftEligibility = true;
	Wild.bShiftEligible = true;
	TestDirectMandatory(Wild, 606602, TEXT("Wild reaches replacement without Shift"));

	TestDirectMandatory(
		MakeDoubleOpponentFaintScenario(false, 1, false, 606603),
		606603,
		TEXT("Double reaches replacement without Shift"));
	TestDirectMandatory(
		MakeDoubleOpponentFaintScenario(false, 1, true, 606604),
		606604,
		TEXT("Partner Double reaches replacement without Shift"));

	TUniquePtr<FBattleEngine> TerminalEngine;
	TestTrue(TEXT("The no-reserve Single scenario completes"),
		ReachReplacementBoundary(
			MakeSingleOpponentFaintScenario(true, false, 606605),
			606605,
			TerminalEngine));
	if (TerminalEngine.IsValid())
	{
		TestEqual(TEXT("No opponent reserve produces terminal victory"),
			TerminalEngine->GetSnapshot().GetPhase(), EBattlePhase::Terminal);
		TestEqual(TEXT("The terminal no-reserve result is victory"),
			TerminalEngine->GetSnapshot().GetOutcome(), EBattleOutcome::Victory);
		TestTrue(TEXT("Terminal outcomes expose no decision request"),
			TerminalEngine->GetPendingDecisionRequests().IsEmpty());
	}

	TUniquePtr<FBattleEngine> EmptySlotEngine;
	TestTrue(TEXT("The nonterminal Double no-reserve scenario completes"),
		ReachReplacementBoundary(
			MakeDoubleOpponentFaintScenario(false, 0, false, 606606),
			606606,
			EmptySlotEngine));
	if (EmptySlotEngine.IsValid())
	{
		TestEqual(TEXT("An empty slot without a legal reserve goes to EndOfTurn"),
			EmptySlotEngine->GetSnapshot().GetPhase(), EBattlePhase::EndOfTurn);
		TestTrue(TEXT("No-reserve empty slots expose no request"),
			EmptySlotEngine->GetPendingDecisionRequests().IsEmpty());
		TestEqual(TEXT("The unavailable Left slot stays empty"),
			GetActiveBattlerValue(
				EmptySlotEngine->GetSnapshot(),
				EBattleSide::Opponent,
				EBattlePosition::Left),
			static_cast<uint64>(0));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FC06BDeterministicReplayTest,
	"PokemonSolarus.Battle.C06B.Replay.DeterministicEntryFacts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FC06BDeterministicReplayTest::RunTest(const FString& Parameters)
{
	using namespace BattleReplacementTests;
	(void)Parameters;

	struct FEvidence
	{
		bool bSucceeded = false;
		TArray<FString> EventSignatures;
		TArray<FBattleEvent> Events;
		TArray<FBattleRandomDraw> RandomTrace;
		TArray<uint8> ReplayBytes;
	};

	auto RunDeterministicScenario = [](FEvidence& OutEvidence)
	{
		const FC06BScenario Scenario = MakeSingleOpponentFaintScenario(
			true,
			true,
			606701);
		TUniquePtr<FBattleEngine> Engine;
		OutEvidence.bSucceeded = ReachReplacementBoundary(
			Scenario,
			606701,
			Engine);
		if (!OutEvidence.bSucceeded || !Engine.IsValid())
		{
			return;
		}

		TArray<FBattleDecisionRequest> Requests = Engine->GetPendingDecisionRequests();
		FBattleDecision Shift;
		OutEvidence.bSucceeded &= Requests.Num() == 1
			&& Requests[0].GetRequestKind() == EBattleDecisionRequestKind::ShiftResponse
			&& FBattleDecision::TryCreateShiftSwitch(
				Requests[0].GetStateVersion(),
				Requests[0].GetDecisionOwnerTrainerId(),
				Requests[0].GetActingBattlerId(),
				MakePartySlotId(1),
				Requests[0].GetActingSlotId(),
				Shift)
			&& Engine->SubmitDecision(Shift).WasAccepted();
		if (!OutEvidence.bSucceeded)
		{
			return;
		}

		Requests = Engine->GetPendingDecisionRequests();
		FBattleDecisionBatch ReplacementBatch;
		OutEvidence.bSucceeded &= TryMakeReplacementBatch(
			Requests,
			{1},
			ReplacementBatch)
			&& Engine->SubmitDecisionBatch(ReplacementBatch).WasAccepted()
			&& Engine->GetSnapshot().GetPhase() == EBattlePhase::EndOfTurn;
		if (!OutEvidence.bSucceeded)
		{
			return;
		}

		OutEvidence.Events = CollectEvents(*Engine);
		for (const FBattleEvent& Event : OutEvidence.Events)
		{
			FString Targets;
			for (const FBattleEventTarget& Target : Event.GetTargets())
			{
				Targets += FString::Printf(
					TEXT("[%llu,%llu,%u,%u]"),
					Target.TrainerId.IsValid() ? Target.TrainerId.GetValue() : 0,
					Target.BattlerId.IsValid() ? Target.BattlerId.GetValue() : 0,
					Target.ActiveSlotId.IsValid()
						? static_cast<uint8>(Target.ActiveSlotId.GetSide())
						: 255,
					Target.ActiveSlotId.IsValid()
						? static_cast<uint8>(Target.ActiveSlotId.GetPosition())
						: 255);
			}
			OutEvidence.EventSignatures.Add(FString::Printf(
				TEXT("%llu:%u:%u:%llu:%llu:%s"),
				Event.GetEventOrdinal(),
				static_cast<uint8>(Event.GetType()),
				static_cast<uint8>(Event.GetCause()),
				Event.GetActionId().IsValid() ? Event.GetActionId().GetValue() : 0,
				Event.GetSource().BattlerId.IsValid()
					? Event.GetSource().BattlerId.GetValue()
					: 0,
				*Targets));
		}
		OutEvidence.RandomTrace = Engine->ExportRandomTrace();
		FBattleRejection Rejection;
		OutEvidence.bSucceeded &= FBattleReplaySerializer::TrySerializeCanonical(
			Engine->ExportReplayRecord(),
			OutEvidence.ReplayBytes,
			Rejection);
	};

	FEvidence First;
	FEvidence Second;
	RunDeterministicScenario(First);
	RunDeterministicScenario(Second);
	TestTrue(TEXT("The first deterministic replacement run succeeds"), First.bSucceeded);
	TestTrue(TEXT("The second deterministic replacement run succeeds"), Second.bSucceeded);
	TestTrue(TEXT("Identical setup and decisions produce identical events"),
		First.EventSignatures == Second.EventSignatures);
	TestTrue(TEXT("Identical seeds produce identical RNG traces"),
		First.RandomTrace == Second.RandomTrace);
	TestTrue(TEXT("Identical runs produce identical canonical replay bytes"),
		First.ReplayBytes == Second.ReplayBytes);
	TestEqual(TEXT("Shift entrant has exactly one entry trigger"),
		CountTargetEvents(
			First.Events,
			EBattleEventType::Switched,
			PlayerRightBattlerValue),
		1);
	TestEqual(TEXT("Opponent replacement entrant has exactly one entry trigger"),
		CountTargetEvents(
			First.Events,
			EBattleEventType::Switched,
			OpponentRightBattlerValue),
		1);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
