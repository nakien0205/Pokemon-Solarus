#if WITH_DEV_AUTOMATION_TESTS

#include "Battle/BattleEngine.h"
#include "Battle/BattleState.h"
#include "BattleTestFactories.h"
#include "Misc/AutomationTest.h"

namespace
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
	constexpr uint64 PlayerReserveBattlerValue = 13;
	constexpr uint64 OpponentLeftBattlerValue = 21;
	constexpr uint64 OpponentRightBattlerValue = 22;
	constexpr uint64 OpponentReserveBattlerValue = 23;
	constexpr uint64 PartnerBattlerValue = 31;

	const TCHAR* NormalMoveName = TEXT("Move.C03B.Neutral");
	const TCHAR* EmptyMoveName = TEXT("Move.C03B.EmptyPP");
	const TCHAR* AbilityName = TEXT("Ability.C03B.Core");
	const TCHAR* PotionName = TEXT("Item.C03B.Potion");
	const TCHAR* BallName = TEXT("Item.C03B.Ball");
	const TCHAR* SecretItemName = TEXT("Item.C03B.Secret");
	const TCHAR* RainName = TEXT("Condition.C03B.Rain");
	const TCHAR* SpikesName = TEXT("Condition.C03B.Spikes");

	TArray<FBattleTypeChartEntry> MakeTypeChart()
	{
		TArray<FBattleTypeChartEntry> Entries;
		Entries.Reserve(FBattleTypeChart::EntryCount);
		for (int32 AttackingIndex = 0; AttackingIndex < FBattleTypeChart::TypeCount; ++AttackingIndex)
		{
			for (int32 DefendingIndex = 0; DefendingIndex < FBattleTypeChart::TypeCount; ++DefendingIndex)
			{
				const bool bNormalIntoGhost = AttackingIndex == static_cast<int32>(EPokemonType::Normal)
					&& DefendingIndex == static_cast<int32>(EPokemonType::Ghost);
				Entries.Add(
					{
						static_cast<EPokemonType>(AttackingIndex),
						static_cast<EPokemonType>(DefendingIndex),
						bNormalIntoGhost ? 0 : 1,
						1
					});
			}
		}
		return Entries;
	}

	FBattleMoveDefinition MakeMove(const TCHAR* Name)
	{
		FBattleMoveDefinition Move;
		Move.Id = MakeDefinitionId<FMoveId>(Name);
		Move.Type = EPokemonType::Normal;
		Move.Category = EBattleMoveCategory::Physical;
		Move.Power = 40;
		Move.Accuracy = 100;
		Move.BasePP = 20;
		Move.TargetClass = EBattleTargetClass::SelectedOpponent;

		FBattleMoveEffectDescriptor Damage;
		Damage.Order = 0;
		Damage.Kind = EBattleMoveEffectKind::Damage;
		Damage.Target = EBattleEffectTarget::ResolvedTarget;
		Move.Effects.Add(Damage);
		return Move;
	}

	FBattleSpeciesFormDefinition MakeSpecies(const TCHAR* Name, const EPokemonType Type)
	{
		FBattleSpeciesFormDefinition Species;
		Species.Id = MakeDefinitionId<FSpeciesFormId>(Name);
		Species.PrimaryType = Type;
		Species.BaseStats = {80, 80, 80, 80, 80, 80};
		Species.CatchRate = 45;
		Species.AbilityChoices.Add(MakeDefinitionId<FAbilityId>(AbilityName));
		return Species;
	}

	FBattleDefinitionCatalog MakeCatalog()
	{
		FBattleDefinitionCatalogInput Input;
		Input.TypeChartEntries = MakeTypeChart();
		Input.Moves.Add(MakeMove(NormalMoveName));
		Input.Moves.Add(MakeMove(EmptyMoveName));
		Input.Abilities.Add({MakeDefinitionId<FAbilityId>(AbilityName)});
		Input.Items.Add({MakeDefinitionId<FItemId>(PotionName), EBattleItemKind::Battle});
		Input.Items.Add({MakeDefinitionId<FItemId>(BallName), EBattleItemKind::Capture});
		Input.Items.Add({MakeDefinitionId<FItemId>(SecretItemName), EBattleItemKind::Held});
		Input.Conditions.Add(
			{MakeDefinitionId<FConditionId>(RainName), EBattleConditionKind::Weather});
		Input.Conditions.Add(
			{MakeDefinitionId<FConditionId>(SpikesName), EBattleConditionKind::Hazard});
		Input.SpeciesForms.Add(MakeSpecies(TEXT("Species.C03B.PlayerLeft"), EPokemonType::Normal));
		Input.SpeciesForms.Add(MakeSpecies(TEXT("Species.C03B.PlayerRight"), EPokemonType::Normal));
		Input.SpeciesForms.Add(MakeSpecies(TEXT("Species.C03B.PlayerReserve"), EPokemonType::Normal));
		Input.SpeciesForms.Add(MakeSpecies(TEXT("Species.C03B.Partner"), EPokemonType::Normal));
		Input.SpeciesForms.Add(MakeSpecies(TEXT("Species.C03B.OpponentLeft"), EPokemonType::Normal));
		Input.SpeciesForms.Add(MakeSpecies(TEXT("Species.C03B.OpponentRight"), EPokemonType::Ghost));
		Input.SpeciesForms.Add(MakeSpecies(TEXT("Species.C03B.OpponentReserve"), EPokemonType::Normal));

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
			Role == EBattleTrainerRole::Player ? TEXT("Selector.C03B.Player")
			: (Role == EBattleTrainerRole::Partner ? TEXT("Selector.C03B.Partner") : TEXT("Selector.C03B.Enemy")));
		Trainer.Bag.Add({MakeDefinitionId<FItemId>(PotionName), 2});
		if (Role == EBattleTrainerRole::Player)
		{
			Trainer.Bag.Add({MakeDefinitionId<FItemId>(BallName), 1});
		}
		return Trainer;
	}

	FBattlePartyEntrySetup MakePartyEntry(
		const uint64 TrainerValue,
		const uint64 BattlerValue,
		const int32 PartyIndex,
		const TCHAR* SpeciesName,
		const bool bSecretHeldItem = false)
	{
		FBattlePartyEntrySetup Entry;
		Entry.TrainerId = MakeNumericId<FTrainerId>(TrainerValue);
		Entry.BattlerId = MakeNumericId<FBattlerId>(BattlerValue);
		Entry.SourcePokemonId = MakeNumericId<FSourcePokemonId>(1000 + BattlerValue);
		Entry.PartySlotId = MakePartySlotId(PartyIndex);
		Entry.SpeciesFormId = MakeDefinitionId<FSpeciesFormId>(SpeciesName);
		Entry.Level = 50;
		Entry.Stats = {200, 100, 100, 100, 100, 100};
		Entry.CurrentHP = 200;
		Entry.AbilityId = MakeDefinitionId<FAbilityId>(AbilityName);
		if (bSecretHeldItem)
		{
			Entry.OriginalHeldItemId = MakeDefinitionId<FItemId>(SecretItemName);
			Entry.CurrentHeldItemId = Entry.OriginalHeldItemId;
		}

		Entry.Moves.Add({0, MakeDefinitionId<FMoveId>(NormalMoveName), 10, 20});
		Entry.Moves.Add({1, MakeDefinitionId<FMoveId>(EmptyMoveName), 0, 20});
		return Entry;
	}

	FBattleActiveAssignment MakeActive(
		const EBattleSide Side,
		const EBattlePosition Position,
		const uint64 TrainerValue,
		const uint64 BattlerValue)
	{
		return {
			MakeActiveSlotId(Side, Position),
			MakeNumericId<FTrainerId>(TrainerValue),
			MakeNumericId<FBattlerId>(BattlerValue)
		};
	}

	void AddKnowledge(
		FBattleSetupInput& Input,
		const uint64 ObserverTrainerValue,
		const uint64 SubjectBattlerValue,
		const EBattleKnowledgeKind Kind,
		const FDefinitionId& DefinitionId)
	{
		FBattleKnowledgeFact Fact;
		Fact.ObserverTrainerId = MakeNumericId<FTrainerId>(ObserverTrainerValue);
		if (SubjectBattlerValue != 0)
		{
			Fact.SubjectBattlerId = MakeNumericId<FBattlerId>(SubjectBattlerValue);
		}
		Fact.Kind = Kind;
		Fact.DefinitionId = DefinitionId;
		Fact.Visibility = EBattleVisibilityLevel::OwningTrainer;
		Input.KnowledgeFacts.Add(Fact);
	}

	FBattleSetupInput MakeSetupInput(
		const EBattleFormat Format,
		const EBattleDecisionController PartnerController = EBattleDecisionController::PartnerAI)
	{
		FBattleSetupInput Input;
		Input.BattleId = MakeNumericId<FBattleId>(303);
		Input.SettingsReference =
			{MakeDefinitionId<FDefinitionId>(TEXT("Settings.C03B")), 1};
		Input.CatalogReference =
			{MakeDefinitionId<FDefinitionId>(TEXT("Catalog.C03B")), 1};
		Input.EncounterKind = EBattleEncounterKind::Trainer;
		Input.Format = Format;
		Input.CaptureCapacity = {3, 100};
		Input.Policies.bBagAllowed = true;
		Input.Policies.bRunAllowed = false;
		Input.Policies.bCaptureAllowed = false;
		Input.Policies.WildFleeMode = EBattleWildFleeMode::Disabled;

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

		Input.PartyEntries.Add(MakePartyEntry(
			PlayerTrainerValue,
			PlayerLeftBattlerValue,
			0,
			TEXT("Species.C03B.PlayerLeft")));
		Input.PartyEntries.Add(MakePartyEntry(
			PlayerTrainerValue,
			PlayerReserveBattlerValue,
			2,
			TEXT("Species.C03B.PlayerReserve")));
		Input.PartyEntries.Add(MakePartyEntry(
			OpponentTrainerValue,
			OpponentLeftBattlerValue,
			0,
			TEXT("Species.C03B.OpponentLeft"),
			true));
		Input.PartyEntries.Add(MakePartyEntry(
			OpponentTrainerValue,
			OpponentReserveBattlerValue,
			2,
			TEXT("Species.C03B.OpponentReserve")));
		Input.StartingActive.Add(MakeActive(
			EBattleSide::Player,
			EBattlePosition::Left,
			PlayerTrainerValue,
			PlayerLeftBattlerValue));
		Input.StartingActive.Add(MakeActive(
			EBattleSide::Opponent,
			EBattlePosition::Left,
			OpponentTrainerValue,
			OpponentLeftBattlerValue));

		if (Format == EBattleFormat::Double)
		{
			Input.PartyEntries.Add(MakePartyEntry(
				PlayerTrainerValue,
				PlayerRightBattlerValue,
				1,
				TEXT("Species.C03B.PlayerRight")));
			Input.PartyEntries.Add(MakePartyEntry(
				OpponentTrainerValue,
				OpponentRightBattlerValue,
				1,
				TEXT("Species.C03B.OpponentRight")));
			Input.StartingActive.Add(MakeActive(
				EBattleSide::Player,
				EBattlePosition::Right,
				PlayerTrainerValue,
				PlayerRightBattlerValue));
			Input.StartingActive.Add(MakeActive(
				EBattleSide::Opponent,
				EBattlePosition::Right,
				OpponentTrainerValue,
				OpponentRightBattlerValue));
		}
		else if (Format == EBattleFormat::PartnerDouble)
		{
			Input.Trainers.Add(MakeTrainer(
				PartnerTrainerValue,
				EBattleSide::Player,
				EBattleTrainerRole::Partner,
				PartnerController));
			Input.PartyEntries.Add(MakePartyEntry(
				PartnerTrainerValue,
				PartnerBattlerValue,
				0,
				TEXT("Species.C03B.Partner")));
			Input.PartyEntries.Add(MakePartyEntry(
				OpponentTrainerValue,
				OpponentRightBattlerValue,
				1,
				TEXT("Species.C03B.OpponentRight")));
			Input.StartingActive.Add(MakeActive(
				EBattleSide::Player,
				EBattlePosition::Right,
				PartnerTrainerValue,
				PartnerBattlerValue));
			Input.StartingActive.Add(MakeActive(
				EBattleSide::Opponent,
				EBattlePosition::Right,
				OpponentTrainerValue,
				OpponentRightBattlerValue));
		}

		AddKnowledge(
			Input,
			PlayerTrainerValue,
			0,
			EBattleKnowledgeKind::SpeciesFormKnown,
			MakeDefinitionId<FSpeciesFormId>(TEXT("Species.C03B.OpponentLeft")).GetDefinitionId());
		AddKnowledge(
			Input,
			PlayerTrainerValue,
			OpponentLeftBattlerValue,
			EBattleKnowledgeKind::AbilityRevealed,
			MakeDefinitionId<FAbilityId>(AbilityName).GetDefinitionId());
		AddKnowledge(
			Input,
			PlayerTrainerValue,
			OpponentLeftBattlerValue,
			EBattleKnowledgeKind::MoveRevealed,
			MakeDefinitionId<FMoveId>(NormalMoveName).GetDefinitionId());

		for (const FBattlePartyEntrySetup& Entry : Input.PartyEntries)
		{
			if (Entry.TrainerId == MakeNumericId<FTrainerId>(PlayerTrainerValue))
			{
				Input.ObedienceInputs.Add({Entry.BattlerId, true, 20, 0});
			}
		}
		return Input;
	}

	FBattleSetup MakeSetup(
		const EBattleFormat Format,
		const EBattleDecisionController PartnerController = EBattleDecisionController::PartnerAI)
	{
		FBattleSetup Setup;
		EBattleSetupValidationError Error = EBattleSetupValidationError::None;
		const bool bCreated = FBattleSetup::TryCreate(
			MakeSetupInput(Format, PartnerController),
			Setup,
			Error);
		check(bCreated);
		return Setup;
	}

	TUniquePtr<FBattleEngine> MakeEngine(
		const EBattleFormat Format,
		const EBattleDecisionController PartnerController = EBattleDecisionController::PartnerAI)
	{
		TUniquePtr<FBattleEngine> Engine;
		FBattleRejection Rejection;
		const bool bCreated = FBattleEngine::TryCreate(
			MakeSetup(Format, PartnerController),
			MakeCatalog(),
			MakeUnique<FSeededBattleRandom>(303),
			Engine,
			Rejection);
		check(bCreated);
		return Engine;
	}

	FBattleDecision MakeFightDecision(
		const FBattleDecisionRequest& Request,
		const uint64 StateVersionOverride = 0)
	{
		check(!Request.GetLegalMoveIds().IsEmpty());
		const FMoveId MoveId = Request.GetLegalMoveIds()[0];
		const FBattleMoveTargetOption* Pair = Request.GetLegalMoveTargets().FindByPredicate(
			[MoveId](const FBattleMoveTargetOption& Candidate)
			{
				return Candidate.MoveId == MoveId;
			});
		check(Pair != nullptr);

		FBattleDecision Decision;
		const bool bCreated = FBattleDecision::TryCreateFight(
			StateVersionOverride == 0 ? Request.GetStateVersion() : StateVersionOverride,
			Request.GetDecisionOwnerTrainerId(),
			Request.GetActingBattlerId(),
			MoveId,
			Pair->ActiveSlotId,
			Decision);
		check(bCreated);
		return Decision;
	}

	FBattleDecisionBatch MakeBatch(
		const TConstArrayView<FBattleDecisionRequest> Requests,
		const int32 DecisionCount,
		const bool bReverse,
		const uint64 StateVersionOverride = 0)
	{
		check(DecisionCount > 0 && DecisionCount <= Requests.Num());
		FBattleDecisionBatchSpec Spec;
		Spec.StateVersion = StateVersionOverride == 0 ? Requests[0].GetStateVersion() : StateVersionOverride;
		Spec.RequestKind = Requests[0].GetRequestKind();
		Spec.DecisionOwnerTrainerId = Requests[0].GetDecisionOwnerTrainerId();
		for (int32 Index = 0; Index < DecisionCount; ++Index)
		{
			Spec.Decisions.Add(MakeFightDecision(Requests[Index], StateVersionOverride));
		}
		if (bReverse)
		{
			check(Spec.Decisions.Num() == 2);
			Spec.Decisions.Swap(0, 1);
		}

		FBattleDecisionBatch Batch;
		FBattleRejection Rejection;
		const bool bCreated = FBattleDecisionBatch::TryCreate(Spec, Batch, Rejection);
		check(bCreated);
		return Batch;
	}

	const FBattleObservedTrainer* FindObservedTrainer(
		const FBattleSnapshot& Snapshot,
		const FTrainerId TrainerId)
	{
		return Snapshot.GetObservedTrainers().FindByPredicate(
			[TrainerId](const FBattleObservedTrainer& Trainer)
			{
				return Trainer.TrainerId == TrainerId;
			});
	}

	bool HasUnavailableMove(
		const FBattleDecisionRequest& Request,
		const FMoveId MoveId,
		const EBattleOptionUnavailableReason Reason)
	{
		return Request.GetUnavailableOptions().ContainsByPredicate(
			[MoveId, Reason](const FBattleUnavailableDecisionOption& Option)
			{
				return Option.Kind == EBattleDecisionOptionKind::Move
					&& Option.MoveId == MoveId
					&& Option.Reason == Reason;
			});
	}

	bool HasUnavailableItem(
		const FBattleDecisionRequest& Request,
		const FItemId ItemId,
		const EBattleOptionUnavailableReason Reason)
	{
		return Request.GetUnavailableOptions().ContainsByPredicate(
			[ItemId, Reason](const FBattleUnavailableDecisionOption& Option)
			{
				return Option.Kind == EBattleDecisionOptionKind::Item
					&& Option.ItemId == ItemId
					&& Option.Reason == Reason;
			});
	}
}

class FBattleSnapshotDecisionTestFixture
{
public:
	static FBattleEngineState& GetMutableState(FBattleEngine& Engine)
	{
		check(Engine.State.IsValid());
		return *Engine.State;
	}

	static const FBattleEngineState& GetState(const FBattleEngine& Engine)
	{
		check(Engine.State.IsValid());
		return *Engine.State;
	}

	static void MutateAfterSnapshot(FBattleEngine& Engine)
	{
		FBattleEngineState& State = GetMutableState(Engine);
		FBattleBattlerState* Battler = State.FindMutableBattler(
			MakeNumericId<FBattlerId>(PlayerLeftBattlerValue));
		FBattleTrainerState* Trainer = State.FindMutableTrainer(
			MakeNumericId<FTrainerId>(PlayerTrainerValue));
		check(Battler != nullptr && Trainer != nullptr);
		Battler->CurrentHP = 150;
		Battler->Stages.ApplyChange(EBattleStat::Attack, 2);
		Trainer->Bag[0].Count = 1;

		FBattleConditionState Weather;
		Weather.ConditionId = MakeDefinitionId<FConditionId>(RainName);
		Weather.RemainingTurns = 5;
		Weather.CreationOrdinal = State.NextConditionCreationOrdinal++;
		State.Field.Weather = Weather;
	}

	static uint64 SeedRefreshCheckpoint(FBattleEngine& Engine)
	{
		FBattleEngineState& State = GetMutableState(Engine);
		check(State.Phase == EBattlePhase::Locked);
		check(State.AcceptedSelections.Num() >= 2);

		FActionId FirstActionId;
		FActionId SecondActionId;
		const bool bFirstCreated = FActionId::TryCreate(700, FirstActionId);
		const bool bSecondCreated = FActionId::TryCreate(701, SecondActionId);
		check(bFirstCreated && bSecondCreated);
		State.LockedActions.Add({FirstActionId, 1, State.AcceptedSelections[0]});
		State.LockedActions.Add({SecondActionId, 2, State.AcceptedSelections[1]});
		State.Phase = EBattlePhase::Resolving;

		FResolutionId ResolutionId;
		const bool bResolutionCreated = FResolutionId::TryCreate(700, ResolutionId);
		check(bResolutionCreated);
		FBattleEventSpec EventSpec;
		EventSpec.EventOrdinal = State.NextEventOrdinal;
		EventSpec.BattleId = State.GetBattleId();
		EventSpec.TurnId = State.GetTurnId();
		EventSpec.ResolutionId = ResolutionId;
		EventSpec.Type = EBattleEventType::OpponentRemovalCheckpoint;
		EventSpec.Cause = EBattleEventCause::Outcome;
		EventSpec.Source.TrainerId = MakeNumericId<FTrainerId>(OpponentTrainerValue);
		EventSpec.Source.BattlerId = MakeNumericId<FBattlerId>(OpponentLeftBattlerValue);
		EventSpec.Visibility.Level = EBattleVisibilityLevel::Public;
		FBattleEvent Event;
		const bool bEventCreated = FBattleEvent::TryCreate(EventSpec, Event);
		check(bEventCreated);
		State.OrderedEvents.Add(Event);
		State.AvailableOpponentRemovalCheckpoints.Add(EventSpec.EventOrdinal);
		++State.NextEventOrdinal;
		return EventSpec.EventOrdinal;
	}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC03BSnapshotVisibilityTest,
	"PokemonSolarus.Battle.C03B.Snapshot.VisibilityAndDeepCopy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBattleC03BSnapshotVisibilityTest::RunTest(const FString& Parameters)
{
	TUniquePtr<FBattleEngine> Engine = MakeEngine(EBattleFormat::Single);
	FBattleRejection Rejection;
	TestTrue(TEXT("Single action selection begins"), Engine->TryBeginActionDecisionSequence(Rejection));

	const FTrainerId PlayerId = MakeNumericId<FTrainerId>(PlayerTrainerValue);
	const FBattleSnapshot Before = Engine->GetSnapshotForObserver(PlayerId);
	TestTrue(TEXT("The observer snapshot is valid"), Before.IsValid());
	TestTrue(TEXT("The snapshot is observer filtered"), Before.IsObserverFiltered());
	TestTrue(TEXT("The observer identity is retained"), Before.GetObserverTrainerId() == PlayerId);
	TestEqual(TEXT("Filtered snapshots do not expose legacy full setup parties"), Before.GetPartyEntries().Num(), 0);
	TestNotNull(
		TEXT("The player's reserve remains visible to its owner"),
		Before.FindObservedBattler(MakeNumericId<FBattlerId>(PlayerReserveBattlerValue)));
	TestNull(
		TEXT("The opponent's unrelated reserve stays hidden"),
		Before.FindObservedBattler(MakeNumericId<FBattlerId>(OpponentReserveBattlerValue)));

	const FBattleObservedBattler* Opponent = Before.FindObservedBattler(
		MakeNumericId<FBattlerId>(OpponentLeftBattlerValue));
	TestNotNull(TEXT("The active opponent is visible"), Opponent);
	if (Opponent != nullptr)
	{
		TestTrue(TEXT("A revealed opponent Ability is visible"), Opponent->bAbilityKnown);
		TestFalse(TEXT("An unrevealed opponent held item stays hidden"), Opponent->bHeldItemKnown);
		TestEqual(TEXT("Only the revealed opponent move is projected"), Opponent->Moves.Num(), 1);
		if (!Opponent->Moves.IsEmpty())
		{
			TestFalse(TEXT("Opponent move PP stays hidden"), Opponent->Moves[0].bPPVisible);
		}
	}

	const FBattleObservedTrainer* Player = FindObservedTrainer(Before, PlayerId);
	const FBattleObservedTrainer* OpponentTrainer = FindObservedTrainer(
		Before,
		MakeNumericId<FTrainerId>(OpponentTrainerValue));
	TestNotNull(TEXT("The observing Trainer is projected"), Player);
	TestNotNull(TEXT("The opposing Trainer is projected"), OpponentTrainer);
	if (Player != nullptr && OpponentTrainer != nullptr)
	{
		TestTrue(TEXT("The owner sees its finite Bag snapshot"), Player->bBagVisible);
		TestFalse(TEXT("The opponent Bag stays hidden"), OpponentTrainer->bBagVisible);
	}

	const FBattleObservedBattler* BeforePlayer = Before.FindObservedBattler(
		MakeNumericId<FBattlerId>(PlayerLeftBattlerValue));
	check(BeforePlayer != nullptr);
	int32 BeforeAttackStage = 0;
	const bool bBeforeStageRead = BeforePlayer->StatStages.TryGetStage(
		EBattleStat::Attack,
		BeforeAttackStage);
	check(bBeforeStageRead);
	FBattleSnapshotDecisionTestFixture::MutateAfterSnapshot(*Engine);
	const FBattleSnapshot After = Engine->GetSnapshotForObserver(PlayerId);
	const FBattleObservedBattler* AfterPlayer = After.FindObservedBattler(
		MakeNumericId<FBattlerId>(PlayerLeftBattlerValue));
	check(AfterPlayer != nullptr);
	int32 AfterAttackStage = 0;
	const bool bAfterStageRead = AfterPlayer->StatStages.TryGetStage(
		EBattleStat::Attack,
		AfterAttackStage);
	check(bAfterStageRead);
	TestEqual(TEXT("The old snapshot retains copied HP"), BeforePlayer->CurrentHP, 200);
	TestEqual(TEXT("A new snapshot sees changed HP"), AfterPlayer->CurrentHP, 150);
	TestEqual(TEXT("The old snapshot retains copied stages"), BeforeAttackStage, 0);
	TestEqual(TEXT("A new snapshot sees changed stages"), AfterAttackStage, 2);
	TestFalse(TEXT("The old snapshot has no later weather"), Before.GetWeather().IsSet());
	TestTrue(TEXT("A new snapshot sees later weather"), After.GetWeather().IsSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC03BSingleSequenceTest,
	"PokemonSolarus.Battle.C03B.Decisions.SingleSequenceAndStaleRejection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBattleC03BSingleSequenceTest::RunTest(const FString& Parameters)
{
	TUniquePtr<FBattleEngine> Engine = MakeEngine(EBattleFormat::Single);
	FBattleRejection Rejection;
	TestTrue(TEXT("Single action selection begins"), Engine->TryBeginActionDecisionSequence(Rejection));
	TArray<FBattleDecisionRequest> Requests = Engine->GetPendingDecisionRequests();
	TestEqual(TEXT("Single requests one player action first"), Requests.Num(), 1);
	check(Requests.Num() == 1);
	TestEqual(TEXT("The first request owner is the player"), Requests[0].GetDecisionOwnerTrainerId().GetValue(), PlayerTrainerValue);
	TestEqual(TEXT("The first request actor is Player Left"), Requests[0].GetActingBattlerId().GetValue(), PlayerLeftBattlerValue);

	const uint64 VersionBeforeStale = Engine->GetSnapshot().GetStateVersion();
	const int32 DrawsBeforeStale = Engine->ExportRandomTrace().Num();
	const FBattleDecisionBatch StaleBatch = MakeBatch(Requests, 1, false, VersionBeforeStale - 1);
	const FBattleResolution Stale = Engine->SubmitDecisionBatch(StaleBatch);
	TestFalse(TEXT("A stale response is rejected"), Stale.WasAccepted());
	TestEqual(TEXT("The stale response has a typed reason"), Stale.GetRejection().Reason, EBattleRejectionReason::StaleStateVersion);
	TestEqual(TEXT("A stale response leaves gameplay version unchanged"), Engine->GetSnapshot().GetStateVersion(), VersionBeforeStale);
	TestEqual(TEXT("A stale response consumes no RNG"), Engine->ExportRandomTrace().Num(), DrawsBeforeStale);
	TestEqual(TEXT("The same player request remains pending"), Engine->GetPendingDecisionRequests()[0].GetActingBattlerId().GetValue(), PlayerLeftBattlerValue);

	const FBattleResolution PlayerAccepted = Engine->SubmitDecisionBatch(MakeBatch(Requests, 1, false));
	TestTrue(TEXT("The player choice is accepted"), PlayerAccepted.WasAccepted());
	Requests = Engine->GetPendingDecisionRequests();
	TestEqual(TEXT("Single next requests one enemy action"), Requests.Num(), 1);
	check(Requests.Num() == 1);
	TestEqual(TEXT("The second request owner is the enemy"), Requests[0].GetDecisionOwnerTrainerId().GetValue(), OpponentTrainerValue);
	TestEqual(TEXT("The second request actor is Opponent Left"), Requests[0].GetActingBattlerId().GetValue(), OpponentLeftBattlerValue);

	const FBattleResolution EnemyAccepted = Engine->SubmitDecisionBatch(MakeBatch(Requests, 1, false));
	TestTrue(TEXT("The enemy choice is accepted"), EnemyAccepted.WasAccepted());
	TestEqual(TEXT("All Single choices end in Locked phase"), Engine->GetSnapshot().GetPhase(), EBattlePhase::Locked);
	TestEqual(TEXT("No request remains after all choices"), Engine->GetPendingDecisionRequests().Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC03BDoubleBatchTest,
	"PokemonSolarus.Battle.C03B.Decisions.DoubleBatchOptionsAndKnowledge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBattleC03BDoubleBatchTest::RunTest(const FString& Parameters)
{
	TUniquePtr<FBattleEngine> Engine = MakeEngine(EBattleFormat::Double);
	FBattleRejection Rejection;
	TestTrue(TEXT("Double action selection begins"), Engine->TryBeginActionDecisionSequence(Rejection));
	const TArray<FBattleDecisionRequest> Requests = Engine->GetPendingDecisionRequests();
	TestEqual(TEXT("One player Trainer receives a two-choice batch"), Requests.Num(), 2);
	check(Requests.Num() == 2);
	TestEqual(TEXT("Double batch orders Player Left first"), Requests[0].GetActingSlotId().GetPosition(), EBattlePosition::Left);
	TestEqual(TEXT("Double batch orders Player Right second"), Requests[1].GetActingSlotId().GetPosition(), EBattlePosition::Right);

	const FMoveId NormalMove = MakeDefinitionId<FMoveId>(NormalMoveName);
	const FMoveId EmptyMove = MakeDefinitionId<FMoveId>(EmptyMoveName);
	const FItemId Potion = MakeDefinitionId<FItemId>(PotionName);
	const FItemId Ball = MakeDefinitionId<FItemId>(BallName);
	TestTrue(TEXT("Only the usable catalog move is legal"), Requests[0].GetLegalMoveIds().Contains(NormalMove));
	TestFalse(TEXT("A zero-PP move is not legal"), Requests[0].GetLegalMoveIds().Contains(EmptyMove));
	TestTrue(TEXT("The zero-PP move has a typed reason"), HasUnavailableMove(Requests[0], EmptyMove, EBattleOptionUnavailableReason::NoPP));
	TestTrue(TEXT("The owned battle item is legal"), Requests[0].GetLegalItemIds().Contains(Potion));
	TestFalse(TEXT("Capture item is not legal in this Trainer battle"), Requests[0].GetLegalItemIds().Contains(Ball));
	TestTrue(TEXT("Capture restriction has a typed reason"), HasUnavailableItem(Requests[0], Ball, EBattleOptionUnavailableReason::CaptureRestricted));
	TestTrue(TEXT("The living reserve is a legal switch"), Requests[0].GetLegalSwitchPartySlots().Contains(MakePartySlotId(2)));
	for (const FMoveId& MoveId : Requests[0].GetLegalMoveIds())
	{
		TestNotNull(TEXT("Every legal move exists in the frozen catalog"), MakeCatalog().FindMove(MoveId));
	}

	const FBattleSnapshot PlayerSnapshot = Engine->GetSnapshotForObserver(
		MakeNumericId<FTrainerId>(PlayerTrainerValue));
	const FBattleMoveEffectivenessKnowledge* Summary =
		PlayerSnapshot.GetMoveEffectivenessKnowledge().FindByPredicate(
			[NormalMove](const FBattleMoveEffectivenessKnowledge& Knowledge)
			{
				return Knowledge.MoveId == NormalMove;
			});
	TestNotNull(TEXT("The move has a typed effectiveness summary"), Summary);
	if (Summary != nullptr)
	{
		TestEqual(TEXT("Known plus unknown Double targets summarize as Varies"), Summary->Value, EBattleEffectivenessKnowledge::Varies);
	}
	const FBattleTargetEffectivenessKnowledge* KnownTarget =
		PlayerSnapshot.GetTargetEffectivenessKnowledge().FindByPredicate(
			[](const FBattleTargetEffectivenessKnowledge& Knowledge)
			{
				return Knowledge.TargetSlotId.GetSide() == EBattleSide::Opponent
					&& Knowledge.TargetSlotId.GetPosition() == EBattlePosition::Left;
			});
	const FBattleTargetEffectivenessKnowledge* UnknownTarget =
		PlayerSnapshot.GetTargetEffectivenessKnowledge().FindByPredicate(
			[](const FBattleTargetEffectivenessKnowledge& Knowledge)
			{
				return Knowledge.TargetSlotId.GetSide() == EBattleSide::Opponent
					&& Knowledge.TargetSlotId.GetPosition() == EBattlePosition::Right;
			});
	TestNotNull(TEXT("Opponent Left effectiveness is projected"), KnownTarget);
	TestNotNull(TEXT("Opponent Right effectiveness is projected"), UnknownTarget);
	if (KnownTarget != nullptr && UnknownTarget != nullptr)
	{
		TestEqual(TEXT("Known neutral target is typed Neutral"), KnownTarget->Value, EBattleEffectivenessKnowledge::Neutral);
		TestEqual(TEXT("Unknown species stays typed Unknown"), UnknownTarget->Value, EBattleEffectivenessKnowledge::Unknown);
	}

	const uint64 VersionBeforeReverse = Engine->GetSnapshot().GetStateVersion();
	const FBattleResolution Reverse = Engine->SubmitDecisionBatch(MakeBatch(Requests, 2, true));
	TestFalse(TEXT("A Right/Left batch is rejected"), Reverse.WasAccepted());
	TestEqual(TEXT("Reversed decisions have a typed order error"), Reverse.GetRejection().Reason, EBattleRejectionReason::WrongDecisionOrder);
	TestEqual(TEXT("A reversed batch leaves gameplay version unchanged"), Engine->GetSnapshot().GetStateVersion(), VersionBeforeReverse);

	const FBattleResolution Accepted = Engine->SubmitDecisionBatch(MakeBatch(Requests, 2, false));
	TestTrue(TEXT("A Left/Right batch is accepted atomically"), Accepted.WasAccepted());
	const TArray<FBattleDecisionRequest> EnemyRequests = Engine->GetPendingDecisionRequests();
	TestEqual(TEXT("The enemy Trainer then receives its two-choice batch"), EnemyRequests.Num(), 2);
	if (EnemyRequests.Num() == 2)
	{
		TestEqual(TEXT("Enemy Left remains first"), EnemyRequests[0].GetActingSlotId().GetPosition(), EBattlePosition::Left);
		TestEqual(TEXT("Enemy Right remains second"), EnemyRequests[1].GetActingSlotId().GetPosition(), EBattlePosition::Right);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC03BPartnerSequenceTest,
	"PokemonSolarus.Battle.C03B.Decisions.PartnerAndEnemyVisibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBattleC03BPartnerSequenceTest::RunTest(const FString& Parameters)
{
	TUniquePtr<FBattleEngine> Engine = MakeEngine(EBattleFormat::PartnerDouble);
	FBattleRejection Rejection;
	TestTrue(TEXT("Partner Double action selection begins"), Engine->TryBeginActionDecisionSequence(Rejection));
	TArray<FBattleDecisionRequest> Requests = Engine->GetPendingDecisionRequests();
	TestEqual(TEXT("The human player's own Trainer is requested first"), Requests[0].GetDecisionOwnerTrainerId().GetValue(), PlayerTrainerValue);
	TestTrue(TEXT("The player choice is accepted"), Engine->SubmitDecisionBatch(MakeBatch(Requests, 1, false)).WasAccepted());

	Requests = Engine->GetPendingDecisionRequests();
	TestEqual(TEXT("Partner AI is requested after the player"), Requests[0].GetDecisionOwnerTrainerId().GetValue(), PartnerTrainerValue);
	const FBattleSnapshot PartnerSnapshot = Engine->GetSnapshotForObserver(
		MakeNumericId<FTrainerId>(PartnerTrainerValue));
	const FBattleSnapshot EarlyEnemySnapshot = Engine->GetSnapshotForObserver(
		MakeNumericId<FTrainerId>(OpponentTrainerValue));
	TestEqual(TEXT("Partner AI may observe the player's accepted choice"), PartnerSnapshot.GetVisibleSelections().Num(), 1);
	TestEqual(TEXT("Enemy pre-choice observation hides the player's choice"), EarlyEnemySnapshot.GetVisibleSelections().Num(), 0);
	TestEqual(TEXT("Enemy does not receive the partner-owned request"), EarlyEnemySnapshot.GetPendingDecisionRequests().Num(), 0);

	TestTrue(TEXT("The partner choice is accepted"), Engine->SubmitDecisionBatch(MakeBatch(Requests, 1, false)).WasAccepted());
	Requests = Engine->GetPendingDecisionRequests();
	TestEqual(TEXT("Enemy actions are requested last"), Requests[0].GetDecisionOwnerTrainerId().GetValue(), OpponentTrainerValue);
	const FBattleSnapshot EnemySnapshot = Engine->GetSnapshotForObserver(
		MakeNumericId<FTrainerId>(OpponentTrainerValue));
	TestEqual(TEXT("The enemy sees its own pending batch"), EnemySnapshot.GetPendingDecisionRequests().Num(), 2);
	TestEqual(TEXT("The enemy still sees no unexecuted player-side choices"), EnemySnapshot.GetVisibleSelections().Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC03BPlayerControlsPartnerTest,
	"PokemonSolarus.Battle.C03B.Decisions.PlayerControlsBothTrainerOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBattleC03BPlayerControlsPartnerTest::RunTest(const FString& Parameters)
{
	TUniquePtr<FBattleEngine> Engine = MakeEngine(
		EBattleFormat::PartnerDouble,
		EBattleDecisionController::Human);
	FBattleRejection Rejection;
	TestTrue(TEXT("Player-controlled Partner Double selection begins"), Engine->TryBeginActionDecisionSequence(Rejection));
	TArray<FBattleDecisionRequest> Requests = Engine->GetPendingDecisionRequests();
	TestEqual(TEXT("The player's own Trainer remains first"), Requests[0].GetDecisionOwnerTrainerId().GetValue(), PlayerTrainerValue);
	TestTrue(TEXT("The own-Trainer choice is accepted"), Engine->SubmitDecisionBatch(MakeBatch(Requests, 1, false)).WasAccepted());
	Requests = Engine->GetPendingDecisionRequests();
	TestEqual(TEXT("The human-controlled partner Trainer is second"), Requests[0].GetDecisionOwnerTrainerId().GetValue(), PartnerTrainerValue);
	TestTrue(TEXT("The partner-Trainer choice is accepted"), Engine->SubmitDecisionBatch(MakeBatch(Requests, 1, false)).WasAccepted());
	Requests = Engine->GetPendingDecisionRequests();
	TestEqual(TEXT("The enemy remains last"), Requests[0].GetDecisionOwnerTrainerId().GetValue(), OpponentTrainerValue);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC03BStatRefreshTest,
	"PokemonSolarus.Battle.C03B.Progression.StatRefreshCheckpoint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBattleC03BStatRefreshTest::RunTest(const FString& Parameters)
{
	TUniquePtr<FBattleEngine> Engine = MakeEngine(EBattleFormat::Single);
	FBattleRejection Rejection;
	TestTrue(TEXT("Single action selection begins"), Engine->TryBeginActionDecisionSequence(Rejection));
	TArray<FBattleDecisionRequest> Requests = Engine->GetPendingDecisionRequests();
	TestTrue(TEXT("The player choice is accepted"), Engine->SubmitDecisionBatch(MakeBatch(Requests, 1, false)).WasAccepted());
	Requests = Engine->GetPendingDecisionRequests();
	TestTrue(TEXT("The enemy choice is accepted"), Engine->SubmitDecisionBatch(MakeBatch(Requests, 1, false)).WasAccepted());

	const uint64 Checkpoint = FBattleSnapshotDecisionTestFixture::SeedRefreshCheckpoint(*Engine);
	const FBattleEngineState& BeforeState = FBattleSnapshotDecisionTestFixture::GetState(*Engine);
	TArray<FActionId> LockedOrderBefore;
	for (const FBattleLockedActionState& Action : BeforeState.LockedActions)
	{
		LockedOrderBefore.Add(Action.ActionId);
	}
	TArray<uint64> EventOrdinalsBefore;
	TArray<EBattleEventType> EventTypesBefore;
	for (const FBattleEvent& Event : BeforeState.OrderedEvents)
	{
		EventOrdinalsBefore.Add(Event.GetEventOrdinal());
		EventTypesBefore.Add(Event.GetType());
	}

	FBattleBetweenActionsStatRefresh Refresh;
	Refresh.StateVersion = Engine->GetSnapshot().GetStateVersion();
	Refresh.OpponentRemovalCheckpointEventOrdinal = Checkpoint;
	Refresh.BattlerId = MakeNumericId<FBattlerId>(PlayerLeftBattlerValue);
	Refresh.NewLevel = 51;
	Refresh.NewStats = {210, 110, 108, 109, 107, 105};
	Refresh.NewCurrentHP = 205;
	const FBattleResolution Applied = Engine->ApplyBetweenActionsStatRefresh(Refresh);
	TestTrue(TEXT("A matching between-actions refresh is accepted"), Applied.WasAccepted());
	const FBattlePartyEntrySetup* Refreshed = Engine->GetSnapshot().FindBattler(Refresh.BattlerId);
	TestNotNull(TEXT("The refreshed battler remains projected"), Refreshed);
	if (Refreshed != nullptr)
	{
		TestEqual(TEXT("The supplied level is applied"), Refreshed->Level, 51);
		TestEqual(TEXT("The supplied Max HP is applied"), Refreshed->Stats.MaxHP, 210);
		TestEqual(TEXT("The supplied current HP is applied"), Refreshed->CurrentHP, 205);
	}

	const FBattleEngineState& AfterState = FBattleSnapshotDecisionTestFixture::GetState(*Engine);
	TestEqual(TEXT("Locked action count is unchanged"), AfterState.LockedActions.Num(), LockedOrderBefore.Num());
	for (int32 Index = 0; Index < LockedOrderBefore.Num(); ++Index)
	{
		TestTrue(TEXT("Locked action order is unchanged"), AfterState.LockedActions[Index].ActionId == LockedOrderBefore[Index]);
	}
	TestTrue(TEXT("The refresh appends rather than replacing prior events"), AfterState.OrderedEvents.Num() > EventOrdinalsBefore.Num());
	for (int32 Index = 0; Index < EventOrdinalsBefore.Num(); ++Index)
	{
		TestEqual(TEXT("Previous event ordinals stay unchanged"), AfterState.OrderedEvents[Index].GetEventOrdinal(), EventOrdinalsBefore[Index]);
		TestEqual(TEXT("Previous event types stay unchanged"), AfterState.OrderedEvents[Index].GetType(), EventTypesBefore[Index]);
	}

	const uint64 VersionAfterApplied = Engine->GetSnapshot().GetStateVersion();
	const int32 DrawsAfterApplied = Engine->ExportRandomTrace().Num();
	Refresh.StateVersion = VersionAfterApplied - 1;
	const FBattleResolution Stale = Engine->ApplyBetweenActionsStatRefresh(Refresh);
	TestFalse(TEXT("A stale refresh is rejected"), Stale.WasAccepted());
	TestEqual(TEXT("The stale refresh reason is typed"), Stale.GetRejection().Reason, EBattleRejectionReason::StaleStateVersion);
	TestEqual(TEXT("A stale refresh leaves gameplay version unchanged"), Engine->GetSnapshot().GetStateVersion(), VersionAfterApplied);
	TestEqual(TEXT("A stale refresh consumes no RNG"), Engine->ExportRandomTrace().Num(), DrawsAfterApplied);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
