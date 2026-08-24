#if WITH_DEV_AUTOMATION_TESTS

#include "Battle/BattleBagItem.h"
#include "Battle/BattleEngine.h"
#include "Battle/BattleEncounterPolicy.h"
#include "Battle/BattleMajorStatus.h"
#include "Battle/BattleState.h"
#include "BattleTestFactories.h"
#include "Misc/AutomationTest.h"

class FBattleC09CPartnerEngineFixture
{
public:
	static bool ApplyMajorStatus(
		FBattleEngine& Engine,
		const FBattlerId BattlerId,
		const FConditionId StatusId)
	{
		if (!Engine.State.IsValid())
		{
			return false;
		}
		FBattleBattlerState* Battler = Engine.State->FindMutableBattler(BattlerId);
		FBattleTriggerSubject Owner;
		EBattleTriggerError Error = EBattleTriggerError::None;
		if (Battler == nullptr
			|| Battler->MajorStatusId.IsValid()
			|| !FBattleMajorStatusRules::IsCanonical(StatusId)
			|| !FBattleTriggerSubject::TryCreateBattler(BattlerId, Owner)
			|| !FBattleMajorStatusRules::TryRegisterTriggers(
				Engine.State->TriggerFramework,
				StatusId,
				Owner,
				TOptional<int32>(),
				Error))
		{
			return false;
		}
		Battler->MajorStatusId = StatusId;
		TArray<FBattleTriggerEffectRequest> Requests;
		TArray<FBattleTriggerLifecycleFact> Lifecycle;
		Engine.State->TriggerFramework.DrainEffectRequests(Requests);
		Engine.State->TriggerFramework.DrainLifecycleFacts(Lifecycle);
		return true;
	}
};

namespace BattlePartnerFlowTests
{
	using BattleTest::MakeActiveSlotId;
	using BattleTest::MakeDefinitionId;
	using BattleTest::MakeNumericId;
	using BattleTest::MakePartySlotId;

	constexpr uint64 PlayerTrainerValue = 1;
	constexpr uint64 OpponentTrainerValue = 2;
	constexpr uint64 PartnerTrainerValue = 3;
	constexpr uint64 PlayerBattlerValue = 11;
	constexpr uint64 PlayerReserveValue = 12;
	constexpr uint64 PartnerBattlerValue = 31;
	constexpr uint64 PartnerReserveValue = 32;
	constexpr uint64 OpponentLeftValue = 21;
	constexpr uint64 OpponentRightValue = 22;

	const TCHAR* AttackMoveName = TEXT("Move.C09C.Attack");
	const TCHAR* SupportMoveName = TEXT("Move.C09C.AllySupport");
	const TCHAR* SpreadMoveName = TEXT("Move.C09C.PartnerSpread");
	const TCHAR* AbilityName = TEXT("Ability.C09C.Plain");

	struct FC09CScenario
	{
		EBattleDecisionController PartnerController = EBattleDecisionController::PartnerAI;
		int32 PlayerHP = 200;
		int32 PlayerReserveHP = 0;
		int32 PartnerHP = 200;
		int32 PartnerReserveHP = 200;
		int32 OpponentLeftHP = 200;
		int32 OpponentRightHP = 200;
		int32 PlayerSpeed = 100;
		int32 PartnerSpeed = 300;
		int32 OpponentLeftSpeed = 90;
		int32 OpponentRightSpeed = 80;
	};

	TArray<FBattleTypeChartEntry> MakeTypeChart()
	{
		TArray<FBattleTypeChartEntry> Entries;
		Entries.Reserve(FBattleTypeChart::EntryCount);
		for (int32 Attack = 0; Attack < FBattleTypeChart::TypeCount; ++Attack)
		{
			for (int32 Defense = 0; Defense < FBattleTypeChart::TypeCount; ++Defense)
			{
				Entries.Add({
					static_cast<EPokemonType>(Attack),
					static_cast<EPokemonType>(Defense),
					1,
					1});
			}
		}
		return Entries;
	}

	FBattleMoveDefinition MakeMove(
		const TCHAR* Name,
		const EBattleTargetClass TargetClass,
		const int32 Power)
	{
		FBattleMoveDefinition Move;
		Move.Id = MakeDefinitionId<FMoveId>(Name);
		Move.Type = EPokemonType::Normal;
		Move.Category = EBattleMoveCategory::Physical;
		Move.Power = Power;
		Move.bAlwaysHits = true;
		Move.BasePP = 20;
		Move.TargetClass = TargetClass;
		FBattleMoveEffectDescriptor Damage;
		Damage.Order = 0;
		Damage.Kind = EBattleMoveEffectKind::Damage;
		Damage.Target = EBattleEffectTarget::ResolvedTarget;
		Move.Effects.Add(Damage);
		return Move;
	}

	FBattleSpeciesFormDefinition MakeSpecies(const TCHAR* Name)
	{
		FBattleSpeciesFormDefinition Species;
		Species.Id = MakeDefinitionId<FSpeciesFormId>(Name);
		Species.PrimaryType = EPokemonType::Normal;
		Species.BaseStats = {80, 80, 80, 80, 80, 80};
		Species.CatchRate = 45;
		Species.AbilityChoices.Add(MakeDefinitionId<FAbilityId>(AbilityName));
		return Species;
	}

	FBattleDefinitionCatalog MakeCatalog()
	{
		FBattleDefinitionCatalogInput Input;
		Input.TypeChartEntries = MakeTypeChart();
		Input.Moves.Add(MakeMove(
			AttackMoveName,
			EBattleTargetClass::SelectedOpponent,
			40));
		Input.Moves.Add(MakeMove(
			SupportMoveName,
			EBattleTargetClass::SelectedAlly,
			40));
		Input.Moves.Add(MakeMove(
			SpreadMoveName,
			EBattleTargetClass::FixedSpreadSet,
			200));
		Input.Abilities.Add({MakeDefinitionId<FAbilityId>(AbilityName)});
		Input.Items.Add({FBattleBagItemRules::GetHyperPotionId(), EBattleItemKind::Battle});
		Input.Items.Add({FBattleBagItemRules::GetPokeBallId(), EBattleItemKind::Capture});
		Input.SpeciesForms.Add(MakeSpecies(TEXT("Species.C09C.Player")));
		Input.SpeciesForms.Add(MakeSpecies(TEXT("Species.C09C.PlayerReserve")));
		Input.SpeciesForms.Add(MakeSpecies(TEXT("Species.C09C.Partner")));
		Input.SpeciesForms.Add(MakeSpecies(TEXT("Species.C09C.PartnerReserve")));
		Input.SpeciesForms.Add(MakeSpecies(TEXT("Species.C09C.OpponentLeft")));
		Input.SpeciesForms.Add(MakeSpecies(TEXT("Species.C09C.OpponentRight")));

		FBattleDefinitionCatalog Catalog;
		TArray<FBattleCatalogDiagnostic> Diagnostics;
		const bool bCreated = FBattleDefinitionCatalog::TryCreate(Input, Catalog, Diagnostics);
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
				? TEXT("Selector.C09C.Player")
				: Role == EBattleTrainerRole::Partner
					? TEXT("Selector.C09C.Partner")
					: TEXT("Selector.C09C.Enemy"));
		if (Role != EBattleTrainerRole::Opponent)
		{
			Trainer.Bag.Add({FBattleBagItemRules::GetHyperPotionId(),
				Role == EBattleTrainerRole::Player ? 2 : 3});
			Trainer.Bag.Add({FBattleBagItemRules::GetPokeBallId(), 1});
		}
		return Trainer;
	}

	FBattlePartyEntrySetup MakePartyEntry(
		const uint64 TrainerValue,
		const uint64 BattlerValue,
		const int32 PartyIndex,
		const TCHAR* SpeciesName,
		const int32 CurrentHP,
		const int32 Speed)
	{
		FBattlePartyEntrySetup Entry;
		Entry.TrainerId = MakeNumericId<FTrainerId>(TrainerValue);
		Entry.BattlerId = MakeNumericId<FBattlerId>(BattlerValue);
		Entry.SourcePokemonId = MakeNumericId<FSourcePokemonId>(1000 + BattlerValue);
		Entry.PartySlotId = MakePartySlotId(PartyIndex);
		Entry.SpeciesFormId = MakeDefinitionId<FSpeciesFormId>(SpeciesName);
		Entry.Level = 50;
		Entry.Stats = {200, 100, 100, 100, 100, Speed};
		Entry.CurrentHP = CurrentHP;
		Entry.AbilityId = MakeDefinitionId<FAbilityId>(AbilityName);
		Entry.Moves.Add({0, MakeDefinitionId<FMoveId>(AttackMoveName), 20, 20});
		Entry.Moves.Add({1, MakeDefinitionId<FMoveId>(SupportMoveName), 20, 20});
		Entry.Moves.Add({2, MakeDefinitionId<FMoveId>(SpreadMoveName), 20, 20});
		return Entry;
	}

	FBattleSetup MakeSetup(const FC09CScenario& Scenario)
	{
		FBattleSetupInput Input;
		Input.BattleId = MakeNumericId<FBattleId>(9093);
		Input.SettingsReference = {
			MakeDefinitionId<FDefinitionId>(TEXT("Settings.C09C.ResolvedControl")), 1};
		Input.CatalogReference = {
			MakeDefinitionId<FDefinitionId>(TEXT("Catalog.C09C")), 1};
		Input.EncounterKind = EBattleEncounterKind::Trainer;
		Input.Format = EBattleFormat::PartnerDouble;
		Input.Policies.bBagAllowed = true;
		Input.Policies.bRunAllowed = false;
		Input.Policies.bCaptureAllowed = false;
		Input.Policies.bShiftPromptEligible = false;

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
		Input.Trainers.Add(MakeTrainer(
			PartnerTrainerValue,
			EBattleSide::Player,
			EBattleTrainerRole::Partner,
			Scenario.PartnerController));

		Input.PartyEntries.Add(MakePartyEntry(
			PlayerTrainerValue,
			PlayerBattlerValue,
			0,
			TEXT("Species.C09C.Player"),
			Scenario.PlayerHP,
			Scenario.PlayerSpeed));
		Input.PartyEntries.Add(MakePartyEntry(
			PlayerTrainerValue,
			PlayerReserveValue,
			1,
			TEXT("Species.C09C.PlayerReserve"),
			Scenario.PlayerReserveHP,
			90));
		Input.PartyEntries.Add(MakePartyEntry(
			PartnerTrainerValue,
			PartnerBattlerValue,
			0,
			TEXT("Species.C09C.Partner"),
			Scenario.PartnerHP,
			Scenario.PartnerSpeed));
		Input.PartyEntries.Add(MakePartyEntry(
			PartnerTrainerValue,
			PartnerReserveValue,
			2,
			TEXT("Species.C09C.PartnerReserve"),
			Scenario.PartnerReserveHP,
			85));
		Input.PartyEntries.Add(MakePartyEntry(
			OpponentTrainerValue,
			OpponentLeftValue,
			0,
			TEXT("Species.C09C.OpponentLeft"),
			Scenario.OpponentLeftHP,
			Scenario.OpponentLeftSpeed));
		Input.PartyEntries.Add(MakePartyEntry(
			OpponentTrainerValue,
			OpponentRightValue,
			1,
			TEXT("Species.C09C.OpponentRight"),
			Scenario.OpponentRightHP,
			Scenario.OpponentRightSpeed));

		Input.StartingActive.Add({
			MakeActiveSlotId(EBattleSide::Player, EBattlePosition::Left),
			MakeNumericId<FTrainerId>(PlayerTrainerValue),
			MakeNumericId<FBattlerId>(PlayerBattlerValue)});
		Input.StartingActive.Add({
			MakeActiveSlotId(EBattleSide::Player, EBattlePosition::Right),
			MakeNumericId<FTrainerId>(PartnerTrainerValue),
			MakeNumericId<FBattlerId>(PartnerBattlerValue)});
		Input.StartingActive.Add({
			MakeActiveSlotId(EBattleSide::Opponent, EBattlePosition::Left),
			MakeNumericId<FTrainerId>(OpponentTrainerValue),
			MakeNumericId<FBattlerId>(OpponentLeftValue)});
		Input.StartingActive.Add({
			MakeActiveSlotId(EBattleSide::Opponent, EBattlePosition::Right),
			MakeNumericId<FTrainerId>(OpponentTrainerValue),
			MakeNumericId<FBattlerId>(OpponentRightValue)});

		FBattleSetup Setup;
		EBattleSetupValidationError Error = EBattleSetupValidationError::None;
		const bool bCreated = FBattleSetup::TryCreate(Input, Setup, Error);
		check(bCreated);
		return Setup;
	}

	TUniquePtr<FBattleEngine> MakeEngine(const FC09CScenario& Scenario, const uint64 Seed = 9093)
	{
		TUniquePtr<FBattleEngine> Engine;
		FBattleRejection Rejection;
		const bool bCreated = FBattleEngine::TryCreate(
			MakeSetup(Scenario),
			MakeCatalog(),
			MakeUnique<FSeededBattleRandom>(Seed),
			Engine,
			Rejection);
		check(bCreated);
		return Engine;
	}

	FBattleDecision MakeFight(
		const FBattleDecisionRequest& Request,
		const FMoveId MoveId,
		const FActiveSlotId PreferredTarget = FActiveSlotId())
	{
		FBattleDecision Decision;
		bool bCreated = false;
		if (Request.GetAutomaticallyTargetedMoveIds().Contains(MoveId))
		{
			bCreated = FBattleDecision::TryCreateAutomaticallyTargetedFight(
				Request.GetStateVersion(),
				Request.GetDecisionOwnerTrainerId(),
				Request.GetActingBattlerId(),
				MoveId,
				Decision);
		}
		else
		{
			const FBattleMoveTargetOption* Option = Request.GetLegalMoveTargets().FindByPredicate(
				[MoveId, PreferredTarget](const FBattleMoveTargetOption& Candidate)
				{
					return Candidate.MoveId == MoveId
						&& (!PreferredTarget.IsValid()
							|| Candidate.ActiveSlotId == PreferredTarget);
				});
			check(Option != nullptr);
			bCreated = FBattleDecision::TryCreateFight(
				Request.GetStateVersion(),
				Request.GetDecisionOwnerTrainerId(),
				Request.GetActingBattlerId(),
				MoveId,
				Option->ActiveSlotId,
				Decision);
		}
		check(bCreated);
		return Decision;
	}

	FBattleDecisionBatch MakeBatch(
		const TArray<FBattleDecisionRequest>& Requests,
		const TArray<FBattleDecision>& Decisions)
	{
		check(!Requests.IsEmpty());
		FBattleDecisionBatchSpec Spec;
		Spec.StateVersion = Requests[0].GetStateVersion();
		Spec.RequestKind = Requests[0].GetRequestKind();
		Spec.DecisionOwnerTrainerId = Requests[0].GetDecisionOwnerTrainerId();
		Spec.Decisions = Decisions;
		FBattleDecisionBatch Batch;
		FBattleRejection Rejection;
		const bool bCreated = FBattleDecisionBatch::TryCreate(Spec, Batch, Rejection);
		check(bCreated);
		return Batch;
	}

	void LockTurn(
		FBattleEngine& Engine,
		const FMoveId PlayerMove,
		const FMoveId PartnerMove,
		const FActiveSlotId OpponentLeftTarget,
		const FActiveSlotId OpponentRightTarget)
	{
		FBattleRejection Rejection;
		check(Engine.TryBeginActionDecisionSequence(Rejection));
		TArray<FBattleDecisionRequest> Requests = Engine.GetPendingDecisionRequests();
		check(Requests.Num() == 1);
		check(Engine.SubmitDecision(MakeFight(
			Requests[0],
			PlayerMove,
			MakeActiveSlotId(EBattleSide::Opponent, EBattlePosition::Left))).WasAccepted());

		Requests = Engine.GetPendingDecisionRequests();
		check(Requests.Num() == 1);
		check(Engine.SubmitDecision(MakeFight(
			Requests[0],
			PartnerMove,
			MakeActiveSlotId(EBattleSide::Opponent, EBattlePosition::Right))).WasAccepted());

		Requests = Engine.GetPendingDecisionRequests();
		check(Requests.Num() == 2);
		const FMoveId AttackId = MakeDefinitionId<FMoveId>(AttackMoveName);
		TArray<FBattleDecision> EnemyDecisions;
		EnemyDecisions.Add(MakeFight(Requests[0], AttackId, OpponentLeftTarget));
		EnemyDecisions.Add(MakeFight(Requests[1], AttackId, OpponentRightTarget));
		check(Engine.SubmitDecisionBatch(MakeBatch(Requests, EnemyDecisions)).WasAccepted());
	}

	FBattleResolution ExecuteCurrentFight(FBattleEngine& Engine)
	{
		check(Engine.BeginNextLockedAction().WasAccepted());
		check(Engine.CommitCurrentMoveAfterPreMoveGates().WasAccepted());
		check(Engine.ResolveCurrentMoveTargets().WasAccepted());
		return Engine.ExecuteCurrentMoveEffects();
	}

	const FBattleObservedActiveSlot* FindObservedSlot(
		const FBattleSnapshot& Snapshot,
		const EBattleSide Side,
		const EBattlePosition Position)
	{
		const FActiveSlotId SlotId = MakeActiveSlotId(Side, Position);
		return Snapshot.GetObservedActiveSlots().FindByPredicate(
			[SlotId](const FBattleObservedActiveSlot& Slot)
			{
				return Slot.ActiveSlotId == SlotId;
			});
	}

	int32 FindEventIndex(
		const FBattleResolution& Resolution,
		const EBattleEventType Type)
	{
		for (int32 Index = 0; Index < Resolution.GetEvents().Num(); ++Index)
		{
			if (Resolution.GetEvents()[Index].GetType() == Type)
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

	struct FTeamVictoryEvidence
	{
		FBattleResolution Resolution;
		FBattleSnapshot Snapshot;
		TArray<uint8> ReplayBytes;
	};

	FTeamVictoryEvidence RunTeamVictory(
		const uint64 Seed,
		const bool bApplyStartingStatus = false)
	{
		FC09CScenario Scenario;
		Scenario.PlayerHP = 1;
		Scenario.PlayerReserveHP = 0;
		Scenario.PartnerHP = 200;
		Scenario.OpponentLeftHP = 1;
		Scenario.OpponentRightHP = 1;
		Scenario.PartnerSpeed = 400;
		TUniquePtr<FBattleEngine> Engine = MakeEngine(Scenario, Seed);
		if (bApplyStartingStatus)
		{
			check(FBattleC09CPartnerEngineFixture::ApplyMajorStatus(
				*Engine,
				MakeNumericId<FBattlerId>(PlayerBattlerValue),
				FBattleMajorStatusRules::GetBurnId()));
		}
		LockTurn(
			*Engine,
			MakeDefinitionId<FMoveId>(AttackMoveName),
			MakeDefinitionId<FMoveId>(SpreadMoveName),
			MakeActiveSlotId(EBattleSide::Player, EBattlePosition::Left),
			MakeActiveSlotId(EBattleSide::Player, EBattlePosition::Right));

		FTeamVictoryEvidence Evidence;
		Evidence.Resolution = ExecuteCurrentFight(*Engine);
		Evidence.Snapshot = Engine->GetSnapshot();
		FBattleRejection Rejection;
		const bool bSerialized = FBattleReplaySerializer::TrySerializeCanonical(
			Engine->ExportReplayRecord(),
			Evidence.ReplayBytes,
			Rejection);
		check(bSerialized);
		return Evidence;
	}
}

using namespace BattlePartnerFlowTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC09CControlVisibilityTest,
	"PokemonSolarus.Battle.C09C.Control.ResolvedModesOwnershipVisibilityAndAllySupport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC09CControlVisibilityTest::RunTest(const FString& Parameters)
{
	for (const EBattleDecisionController Controller : {
		EBattleDecisionController::PartnerAI,
		EBattleDecisionController::Human})
	{
		FC09CScenario Scenario;
		Scenario.PartnerController = Controller;
		TUniquePtr<FBattleEngine> Engine = MakeEngine(Scenario);
		const FBattleSnapshot Initial = Engine->GetSnapshot();
		const FBattleTrainerSetup* Partner = Initial.GetTrainers().FindByPredicate(
			[](const FBattleTrainerSetup& Trainer)
			{
				return Trainer.Role == EBattleTrainerRole::Partner;
			});
		TestTrue(TEXT("The resolved partner controller is frozen in setup"),
			Partner != nullptr && Partner->Controller == Controller);

		FBattleRejection Rejection;
		TestTrue(TEXT("Partner Double selection begins"),
			Engine->TryBeginActionDecisionSequence(Rejection));
		TArray<FBattleDecisionRequest> Requests = Engine->GetPendingDecisionRequests();
		TestEqual(TEXT("The player Trainer commands first"),
			Requests[0].GetDecisionOwnerTrainerId().GetValue(), PlayerTrainerValue);
		const FBattleDecision Support = MakeFight(
			Requests[0],
			MakeDefinitionId<FMoveId>(SupportMoveName),
			MakeActiveSlotId(EBattleSide::Player, EBattlePosition::Right));
		TestTrue(TEXT("An allied partner is a legal support target"),
			Engine->SubmitDecision(Support).WasAccepted());

		Requests = Engine->GetPendingDecisionRequests();
		TestEqual(TEXT("The partner remains a separate command owner"),
			Requests[0].GetDecisionOwnerTrainerId().GetValue(), PartnerTrainerValue);
		const FBattleSnapshot PartnerView = Engine->GetSnapshotForObserver(
			MakeNumericId<FTrainerId>(PartnerTrainerValue));
		const FBattleSnapshot EnemyView = Engine->GetSnapshotForObserver(
			MakeNumericId<FTrainerId>(OpponentTrainerValue));
		TestEqual(TEXT("Partner coordination sees the player's selected action"),
			PartnerView.GetVisibleSelections().Num(), 1);
		TestEqual(TEXT("Enemy selectors do not see player-side unexecuted choices"),
			EnemyView.GetVisibleSelections().Num(), 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC09CSeparateResourcesTest,
	"PokemonSolarus.Battle.C09C.Ownership.SeparateBagsSwitchesAndCaptureRestrictions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC09CSeparateResourcesTest::RunTest(const FString& Parameters)
{
	FC09CScenario Scenario;
	Scenario.PlayerHP = 100;
	Scenario.PlayerReserveHP = 200;
	Scenario.PartnerHP = 100;
	TUniquePtr<FBattleEngine> Engine = MakeEngine(Scenario);
	const FBattleSnapshot Initial = Engine->GetSnapshot();
	const FBattleTrainerSetup* Player = Initial.GetTrainers().FindByPredicate(
		[](const FBattleTrainerSetup& Trainer)
		{
			return Trainer.Role == EBattleTrainerRole::Player;
		});
	const FBattleTrainerSetup* Partner = Initial.GetTrainers().FindByPredicate(
		[](const FBattleTrainerSetup& Trainer)
		{
			return Trainer.Role == EBattleTrainerRole::Partner;
		});
	TestTrue(TEXT("Player and partner retain separate Bag counts"),
		Player != nullptr && Partner != nullptr
			&& Player->Bag[0].Count != Partner->Bag[0].Count);

	FBattleRejection Rejection;
	TestTrue(TEXT("Selection begins"), Engine->TryBeginActionDecisionSequence(Rejection));
	TArray<FBattleDecisionRequest> Requests = Engine->GetPendingDecisionRequests();
	const FBattleDecisionRequest PlayerRequest = Requests[0];
	TestTrue(TEXT("The player may switch only to its slot 1 reserve"),
		PlayerRequest.GetLegalSwitchPartySlots().Contains(MakePartySlotId(1))
			&& !PlayerRequest.GetLegalSwitchPartySlots().Contains(MakePartySlotId(2)));
	TestFalse(TEXT("Poke Ball is not legal in a partner Trainer battle"),
		PlayerRequest.GetLegalItemIds().Contains(FBattleBagItemRules::GetPokeBallId()));

	FBattleDecision CrossOwnerItem;
	TestTrue(TEXT("The typed cross-owner item payload can be formed for rejection"),
		FBattleDecision::TryCreateBag(
			PlayerRequest.GetStateVersion(),
			PlayerRequest.GetDecisionOwnerTrainerId(),
			PlayerRequest.GetActingBattlerId(),
			FBattleBagItemRules::GetHyperPotionId(),
			FPartySlotId(),
			MakeActiveSlotId(EBattleSide::Player, EBattlePosition::Right),
			CrossOwnerItem));
	TestFalse(TEXT("The player's item cannot target the partner-owned active"),
		Engine->SubmitDecision(CrossOwnerItem).WasAccepted());

	TestTrue(TEXT("The player selection advances"),
		Engine->SubmitDecision(MakeFight(
			PlayerRequest,
			MakeDefinitionId<FMoveId>(AttackMoveName),
			MakeActiveSlotId(EBattleSide::Opponent, EBattlePosition::Left))).WasAccepted());
	Requests = Engine->GetPendingDecisionRequests();
	TestTrue(TEXT("The partner may switch only to its slot 2 reserve"),
		Requests[0].GetLegalSwitchPartySlots().Contains(MakePartySlotId(2))
			&& !Requests[0].GetLegalSwitchPartySlots().Contains(MakePartySlotId(1)));
	TestFalse(TEXT("A partner cannot expose its authored Poke Ball as capture"),
		Requests[0].GetLegalItemIds().Contains(FBattleBagItemRules::GetPokeBallId()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC09CPartnerSlotExhaustionTest,
	"PokemonSolarus.Battle.C09C.Ownership.ExhaustedPartnerSlotStaysEmpty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC09CPartnerSlotExhaustionTest::RunTest(const FString& Parameters)
{
	FC09CScenario Scenario;
	Scenario.PartnerHP = 1;
	Scenario.PartnerReserveHP = 0;
	Scenario.PlayerReserveHP = 200;
	Scenario.OpponentLeftSpeed = 400;
	TUniquePtr<FBattleEngine> Engine = MakeEngine(Scenario);
	LockTurn(
		*Engine,
		MakeDefinitionId<FMoveId>(AttackMoveName),
		MakeDefinitionId<FMoveId>(AttackMoveName),
		MakeActiveSlotId(EBattleSide::Player, EBattlePosition::Right),
		MakeActiveSlotId(EBattleSide::Player, EBattlePosition::Left));
	TestTrue(TEXT("The faster opponent action resolves"), ExecuteCurrentFight(*Engine).WasAccepted());

	const FBattleSnapshot Snapshot = Engine->GetSnapshot();
	const FBattleObservedActiveSlot* PartnerSlot = FindObservedSlot(
		Snapshot,
		EBattleSide::Player,
		EBattlePosition::Right);
	TestTrue(TEXT("The exhausted partner slot is empty"),
		PartnerSlot != nullptr && !PartnerSlot->BattlerId.IsValid());
	TestFalse(TEXT("The player's living reserve does not fill the partner slot"),
		Snapshot.GetActiveAssignments().ContainsByPredicate(
			[](const FBattleActiveAssignment& Assignment)
			{
				return Assignment.BattlerId
					== MakeNumericId<FBattlerId>(PlayerReserveValue);
			}));
	TestEqual(TEXT("The battle remains active after only the partner slot is exhausted"),
		Snapshot.GetOutcome(), EBattleOutcome::InProgress);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC09CPlayerWipeContinuationTest,
	"PokemonSolarus.Battle.C09C.Outcome.PlayerWipeContinuesWithPartner",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC09CPlayerWipeContinuationTest::RunTest(const FString& Parameters)
{
	FC09CScenario Scenario;
	Scenario.PlayerHP = 1;
	Scenario.PlayerReserveHP = 0;
	Scenario.OpponentLeftSpeed = 400;
	TUniquePtr<FBattleEngine> Engine = MakeEngine(Scenario);
	LockTurn(
		*Engine,
		MakeDefinitionId<FMoveId>(AttackMoveName),
		MakeDefinitionId<FMoveId>(AttackMoveName),
		MakeActiveSlotId(EBattleSide::Player, EBattlePosition::Left),
		MakeActiveSlotId(EBattleSide::Player, EBattlePosition::Right));
	TestTrue(TEXT("The player-wiping action resolves"), ExecuteCurrentFight(*Engine).WasAccepted());

	const FBattleSnapshot Snapshot = Engine->GetSnapshot();
	TestEqual(TEXT("A player-only wipe is not terminal while the partner can continue"),
		Snapshot.GetOutcome(), EBattleOutcome::InProgress);
	TestEqual(TEXT("The engine continues resolving the locked turn"),
		Snapshot.GetPhase(), EBattlePhase::Resolving);
	const FBattleObservedBattler* Partner = Snapshot.FindObservedBattler(
		MakeNumericId<FBattlerId>(PartnerBattlerValue));
	TestTrue(TEXT("The partner remains usable"), Partner != nullptr && !Partner->bFainted);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC09CTeamVictoryResultTest,
	"PokemonSolarus.Battle.C09C.Outcome.TeamVictoryRecoveryAndProgressionFacts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC09CTeamVictoryResultTest::RunTest(const FString& Parameters)
{
	const FTeamVictoryEvidence Evidence = RunTeamVictory(9094, true);
	TestTrue(TEXT("The terminal move resolution is accepted"), Evidence.Resolution.WasAccepted());
	TestEqual(TEXT("The partner-only surviving side wins"),
		Evidence.Snapshot.GetOutcome(), EBattleOutcome::Victory);
	TestEqual(TEXT("The terminal cause is Partner Team Victory"),
		Evidence.Snapshot.GetOutcomeCause(), EBattleOutcomeCause::PartnerTeamVictory);
	const FBattlePartyEntrySetup* Player = Evidence.Snapshot.FindBattler(
		MakeNumericId<FBattlerId>(PlayerBattlerValue));
	const FBattleObservedBattler* ObservedPlayer = Evidence.Snapshot.FindObservedBattler(
		MakeNumericId<FBattlerId>(PlayerBattlerValue));
	TestTrue(TEXT("The first valid player Pokemon is restored to exactly 1 HP"),
		Player != nullptr && Player->CurrentHP == 1);
	TestTrue(TEXT("The restored player Pokemon has its starting major status cured"),
		ObservedPlayer != nullptr && !ObservedPlayer->MajorStatusId.IsValid());

	const int32 RecoveryIndex = FindEventIndex(
		Evidence.Resolution,
		EBattleEventType::PartnerTeamVictoryRecovery);
	const int32 EndIndex = FindEventIndex(Evidence.Resolution, EBattleEventType::BattleEnded);
	TestTrue(TEXT("The typed recovery event precedes BattleEnded"),
		RecoveryIndex != INDEX_NONE && EndIndex > RecoveryIndex);
	if (RecoveryIndex != INDEX_NONE)
	{
		const FBattleEvent& Recovery = Evidence.Resolution.GetEvents()[RecoveryIndex];
		TestTrue(TEXT("The recovery event carries the exact 0 to 1 mutation"),
			Recovery.GetNumericBefore() == TOptional<int64>(0)
				&& Recovery.GetNumericAfter() == TOptional<int64>(1)
				&& Recovery.GetNumericDelta() == TOptional<int64>(1));
		TestTrue(TEXT("The recovery targets the player-owned battler"),
			Recovery.GetTargets().Num() == 1
				&& Recovery.GetTargets()[0].TrainerId
					== MakeNumericId<FTrainerId>(PlayerTrainerValue));
	}

	const TConstArrayView<FBattlePersistentProgressionEligibilityFact> Facts =
		Evidence.Snapshot.GetPersistentProgressionEligibilityFacts();
	TestEqual(TEXT("Every partner party member has one external progression fact"),
		Facts.Num(), 2);
	for (const FBattlePersistentProgressionEligibilityFact& Fact : Facts)
	{
		TestTrue(TEXT("Partner facts exclude persistent EXP and EV without calculating rewards"),
			Fact.IsValid()
				&& !Fact.bExperienceEligible
				&& !Fact.bEffortValueEligible
				&& Fact.Restriction == EBattlePersistentProgressionRestriction::NpcPartner);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC09CReplayTest,
	"PokemonSolarus.Battle.C09C.Replay.TeamVictoryDeterminismSchema6",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC09CReplayTest::RunTest(const FString& Parameters)
{
	const FTeamVictoryEvidence First = RunTeamVictory(9095);
	const FTeamVictoryEvidence Second = RunTeamVictory(9095);
	TestEqual(TEXT("C09C advances the canonical replay schema"),
		FBattleReplayRecord::CurrentSchemaVersion, static_cast<uint32>(6));
	TestTrue(TEXT("The same setup, actions, and RNG reproduce identical replay bytes"),
		First.ReplayBytes == Second.ReplayBytes);
	TestTrue(TEXT("The deterministic replay includes data"), !First.ReplayBytes.IsEmpty());
	TestEqual(TEXT("The deterministic twins finish with the same outcome cause"),
		First.Snapshot.GetOutcomeCause(), Second.Snapshot.GetOutcomeCause());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
