#if WITH_DEV_AUTOMATION_TESTS

#include "Battle/BattleActionQueue.h"
#include "Battle/BattleEngine.h"
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
	constexpr uint64 PlayerLeftBattlerValue = 11;
	constexpr uint64 PlayerRightBattlerValue = 12;
	constexpr uint64 PlayerReserveBattlerValue = 13;
	constexpr uint64 OpponentLeftBattlerValue = 21;
	constexpr uint64 OpponentRightBattlerValue = 22;
	constexpr uint64 OpponentReserveBattlerValue = 23;

	const TCHAR* NormalMoveName = TEXT("Move.C04A.Normal");
	const TCHAR* PriorityMoveName = TEXT("Move.C04A.Priority");
	const TCHAR* NegativePriorityMoveName = TEXT("Move.C04A.NegativePriority");
	const TCHAR* EmptyMoveName = TEXT("Move.C04A.Empty");
	const TCHAR* AbilityName = TEXT("Ability.C04A.Core");
	const TCHAR* PotionName = TEXT("Item.C04A.Potion");
	const TCHAR* BallName = TEXT("Item.C04A.Ball");
	const TCHAR* SpeciesName = TEXT("Species.C04A.Core");

	class FSequenceBattleRandom final : public IBattleRandom
	{
	public:
		explicit FSequenceBattleRandom(TArray<uint32> InResults)
			: Results(MoveTemp(InResults))
		{
		}

		virtual bool TryDrawUniform(
			const uint32 InclusiveMinimum,
			const uint32 InclusiveMaximum,
			const FBattleRandomContext& Context,
			FBattleRandomDraw& OutDraw) override
		{
			OutDraw = FBattleRandomDraw();
			if (InclusiveMinimum > InclusiveMaximum
				|| !Context.IsValid()
				|| !Results.IsValidIndex(NextResultIndex))
			{
				return false;
			}
			const uint32 Result = Results[NextResultIndex++];
			if (Result < InclusiveMinimum || Result > InclusiveMaximum)
			{
				return false;
			}

			OutDraw.InclusiveMinimum = InclusiveMinimum;
			OutDraw.InclusiveMaximum = InclusiveMaximum;
			OutDraw.Bound = static_cast<uint64>(InclusiveMaximum)
				- static_cast<uint64>(InclusiveMinimum) + 1;
			OutDraw.RawValue = Result;
			OutDraw.Result = Result;
			OutDraw.CallOrdinal = static_cast<uint64>(Trace.Num() + 1);
			OutDraw.BattleId = Context.BattleId;
			OutDraw.TurnId = Context.TurnId;
			OutDraw.ActionId = Context.ActionId;
			OutDraw.ResolutionId = Context.ResolutionId;
			OutDraw.RulePurpose = Context.RulePurpose;
			Trace.Add(OutDraw);
			return true;
		}

		virtual TConstArrayView<FBattleRandomDraw> GetTrace() const override
		{
			return Trace;
		}

	private:
		TArray<uint32> Results;
		int32 NextResultIndex = 0;
		TArray<FBattleRandomDraw> Trace;
	};

	TArray<FBattleTypeChartEntry> MakeTypeChart()
	{
		TArray<FBattleTypeChartEntry> Entries;
		Entries.Reserve(FBattleTypeChart::EntryCount);
		for (int32 AttackingIndex = 0; AttackingIndex < FBattleTypeChart::TypeCount; ++AttackingIndex)
		{
			for (int32 DefendingIndex = 0; DefendingIndex < FBattleTypeChart::TypeCount; ++DefendingIndex)
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

	FBattleMoveDefinition MakeMove(const TCHAR* Name, const int32 Priority)
	{
		FBattleMoveDefinition Move;
		Move.Id = MakeDefinitionId<FMoveId>(Name);
		Move.Type = EPokemonType::Normal;
		Move.Category = EBattleMoveCategory::Physical;
		Move.Power = 40;
		Move.Accuracy = 100;
		Move.BasePP = 20;
		Move.Priority = Priority;
		Move.TargetClass = EBattleTargetClass::SelectedOpponent;

		FBattleMoveEffectDescriptor Damage;
		Damage.Order = 0;
		Damage.Kind = EBattleMoveEffectKind::Damage;
		Damage.Target = EBattleEffectTarget::ResolvedTarget;
		Move.Effects.Add(Damage);
		return Move;
	}

	FBattleDefinitionCatalog MakeCatalog()
	{
		FBattleDefinitionCatalogInput Input;
		Input.TypeChartEntries = MakeTypeChart();
		Input.Moves.Add(MakeMove(NormalMoveName, 0));
		Input.Moves.Add(MakeMove(PriorityMoveName, 1));
		Input.Moves.Add(MakeMove(NegativePriorityMoveName, -1));
		Input.Moves.Add(MakeMove(EmptyMoveName, 0));
		Input.Abilities.Add({MakeDefinitionId<FAbilityId>(AbilityName)});
		Input.Items.Add({MakeDefinitionId<FItemId>(PotionName), EBattleItemKind::Battle});
		Input.Items.Add({MakeDefinitionId<FItemId>(BallName), EBattleItemKind::Capture});

		FBattleSpeciesFormDefinition Species;
		Species.Id = MakeDefinitionId<FSpeciesFormId>(SpeciesName);
		Species.PrimaryType = EPokemonType::Normal;
		Species.BaseStats = {80, 80, 80, 80, 80, 80};
		Species.CatchRate = 45;
		Species.AbilityChoices.Add(MakeDefinitionId<FAbilityId>(AbilityName));
		Input.SpeciesForms.Add(Species);

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
				? TEXT("Selector.C04A.Player")
				: TEXT("Selector.C04A.Opponent"));
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
		const int32 Speed,
		const bool bAllMovesEmpty)
	{
		FBattlePartyEntrySetup Entry;
		Entry.TrainerId = MakeNumericId<FTrainerId>(TrainerValue);
		Entry.BattlerId = MakeNumericId<FBattlerId>(BattlerValue);
		Entry.SourcePokemonId = MakeNumericId<FSourcePokemonId>(1000 + BattlerValue);
		Entry.PartySlotId = MakePartySlotId(PartyIndex);
		Entry.SpeciesFormId = MakeDefinitionId<FSpeciesFormId>(SpeciesName);
		Entry.Level = 50;
		Entry.Stats = {200, 100, 100, 100, 100, Speed};
		Entry.CurrentHP = 200;
		Entry.AbilityId = MakeDefinitionId<FAbilityId>(AbilityName);
		const int32 UsablePP = bAllMovesEmpty ? 0 : 10;
		Entry.Moves.Add({0, MakeDefinitionId<FMoveId>(NormalMoveName), UsablePP, 20});
		Entry.Moves.Add({1, MakeDefinitionId<FMoveId>(PriorityMoveName), UsablePP, 20});
		Entry.Moves.Add({2, MakeDefinitionId<FMoveId>(NegativePriorityMoveName), UsablePP, 20});
		Entry.Moves.Add({3, MakeDefinitionId<FMoveId>(EmptyMoveName), 0, 20});
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

	struct FSetupOptions
	{
		EBattleFormat Format = EBattleFormat::Single;
		EBattleEncounterKind EncounterKind = EBattleEncounterKind::Wild;
		bool bAllMovesEmpty = false;
		uint8 PlayerReferenceLevel = 20;
		uint8 PlayerBadgeCount = 0;
		int32 PlayerLeftSpeed = 200;
		int32 PlayerRightSpeed = 100;
		int32 OpponentLeftSpeed = 100;
		int32 OpponentRightSpeed = 100;
	};

	FBattleSetup MakeSetup(const FSetupOptions& Options)
	{
		FBattleSetupInput Input;
		Input.BattleId = MakeNumericId<FBattleId>(404);
		Input.SettingsReference = {MakeDefinitionId<FDefinitionId>(TEXT("Settings.C04A")), 1};
		Input.CatalogReference = {MakeDefinitionId<FDefinitionId>(TEXT("Catalog.C04A")), 1};
		Input.EncounterKind = Options.EncounterKind;
		Input.Format = Options.Format;
		Input.CaptureCapacity = {3, 100};
		Input.Policies.bBagAllowed = true;
		Input.Policies.bRunAllowed = Options.EncounterKind == EBattleEncounterKind::Wild;
		Input.Policies.bCaptureAllowed = Options.EncounterKind == EBattleEncounterKind::Wild;
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
			Options.PlayerLeftSpeed,
			Options.bAllMovesEmpty));
		Input.PartyEntries.Add(MakePartyEntry(
			PlayerTrainerValue,
			PlayerReserveBattlerValue,
			2,
			90,
			Options.bAllMovesEmpty));
		Input.PartyEntries.Add(MakePartyEntry(
			OpponentTrainerValue,
			OpponentLeftBattlerValue,
			0,
			Options.OpponentLeftSpeed,
			Options.bAllMovesEmpty));
		Input.PartyEntries.Add(MakePartyEntry(
			OpponentTrainerValue,
			OpponentReserveBattlerValue,
			2,
			90,
			Options.bAllMovesEmpty));
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

		if (Options.Format == EBattleFormat::Double)
		{
			Input.PartyEntries.Add(MakePartyEntry(
				PlayerTrainerValue,
				PlayerRightBattlerValue,
				1,
				Options.PlayerRightSpeed,
				Options.bAllMovesEmpty));
			Input.PartyEntries.Add(MakePartyEntry(
				OpponentTrainerValue,
				OpponentRightBattlerValue,
				1,
				Options.OpponentRightSpeed,
				Options.bAllMovesEmpty));
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

		for (const FBattlePartyEntrySetup& Entry : Input.PartyEntries)
		{
			if (Entry.TrainerId == MakeNumericId<FTrainerId>(PlayerTrainerValue))
			{
				Input.ObedienceInputs.Add(
					{
						Entry.BattlerId,
						true,
						Options.PlayerReferenceLevel,
						Options.PlayerBadgeCount
					});
			}
		}

		FBattleSetup Setup;
		EBattleSetupValidationError Error = EBattleSetupValidationError::None;
		const bool bCreated = FBattleSetup::TryCreate(Input, Setup, Error);
		check(bCreated);
		return Setup;
	}

	TUniquePtr<FBattleEngine> MakeEngine(
		const FSetupOptions& Options,
		const uint64 Seed = 404)
	{
		TUniquePtr<FBattleEngine> Engine;
		FBattleRejection Rejection;
		const bool bCreated = FBattleEngine::TryCreate(
			MakeSetup(Options),
			MakeCatalog(),
			MakeUnique<FSeededBattleRandom>(Seed),
			Engine,
			Rejection);
		check(bCreated);
		return Engine;
	}

	const FBattleMoveTargetOption* FindMoveTarget(
		const FBattleDecisionRequest& Request,
		const FMoveId MoveId)
	{
		return Request.GetLegalMoveTargets().FindByPredicate(
			[MoveId](const FBattleMoveTargetOption& Option)
			{
				return Option.MoveId == MoveId;
			});
	}

	FBattleDecision MakeFightDecision(
		const FBattleDecisionRequest& Request,
		FMoveId MoveId = FMoveId())
	{
		if (!MoveId.IsValid())
		{
			check(!Request.GetLegalMoveIds().IsEmpty());
			MoveId = Request.GetLegalMoveIds()[0];
		}
		const FBattleMoveTargetOption* Target = FindMoveTarget(Request, MoveId);
		check(Target != nullptr);

		FBattleDecision Decision;
		const bool bCreated = FBattleDecision::TryCreateFight(
			Request.GetStateVersion(),
			Request.GetDecisionOwnerTrainerId(),
			Request.GetActingBattlerId(),
			MoveId,
			Target->ActiveSlotId,
			Decision);
		check(bCreated);
		return Decision;
	}

	FBattleDecisionBatch MakeBatch(
		const TArray<FBattleDecisionRequest>& Requests,
		TArray<FBattleDecision> Decisions)
	{
		check(!Requests.IsEmpty());
		FBattleDecisionBatchSpec Spec;
		Spec.StateVersion = Requests[0].GetStateVersion();
		Spec.RequestKind = Requests[0].GetRequestKind();
		Spec.DecisionOwnerTrainerId = Requests[0].GetDecisionOwnerTrainerId();
		Spec.Decisions = MoveTemp(Decisions);
		FBattleDecisionBatch Batch;
		FBattleRejection Rejection;
		const bool bCreated = FBattleDecisionBatch::TryCreate(Spec, Batch, Rejection);
		check(bCreated);
		return Batch;
	}

	FBattleResolution LockAllFights(
		FBattleEngine& Engine,
		const TFunction<FMoveId(uint64)>& SelectMove)
	{
		FBattleRejection Rejection;
		if (Engine.GetSnapshot().GetPhase() == EBattlePhase::Setup)
		{
			const bool bStarted = Engine.TryBeginActionDecisionSequence(Rejection);
			check(bStarted);
		}

		FBattleResolution LastResolution;
		int32 Guard = 0;
		while (!Engine.GetPendingDecisionRequests().IsEmpty() && Guard++ < 8)
		{
			const TArray<FBattleDecisionRequest> Requests = Engine.GetPendingDecisionRequests();
			TArray<FBattleDecision> Decisions;
			for (const FBattleDecisionRequest& Request : Requests)
			{
				FMoveId MoveId = SelectMove(Request.GetActingBattlerId().GetValue());
				if (!MoveId.IsValid())
				{
					MoveId = Request.GetLegalMoveIds()[0];
				}
				Decisions.Add(MakeFightDecision(Request, MoveId));
			}
			LastResolution = Engine.SubmitDecisionBatch(MakeBatch(Requests, MoveTemp(Decisions)));
			check(LastResolution.WasAccepted());
		}
		check(Guard < 8);
		check(Engine.GetSnapshot().GetPhase() == EBattlePhase::Locked);
		return LastResolution;
	}

	int32 GetMovePP(
		const FBattleEngine& Engine,
		const uint64 BattlerValue,
		const FMoveId MoveId)
	{
		const FBattleSnapshot Snapshot = Engine.GetSnapshot();
		const FBattlePartyEntrySetup* Battler = Snapshot.FindBattler(
			MakeNumericId<FBattlerId>(BattlerValue));
		check(Battler != nullptr);
		const FBattleMoveSlotSetup* Move = Battler->Moves.FindByPredicate(
			[MoveId](const FBattleMoveSlotSetup& Candidate)
			{
				return Candidate.MoveId == MoveId;
			});
		check(Move != nullptr);
		return Move->CurrentPP;
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

	bool HasEvent(const FBattleResolution& Resolution, const EBattleEventType Type)
	{
		return Resolution.GetEvents().ContainsByPredicate(
			[Type](const FBattleEvent& Event)
			{
				return Event.GetType() == Type;
			});
	}

	FBattleActionOrderCandidate MakeOrderCandidate(
		const uint64 ActionValue,
		const uint64 TrainerValue,
		const uint64 BattlerValue,
		const EBattleSide Side,
		const EBattlePosition Position,
		const EBattleActionKind ActionKind,
		const EBattleActionCommandBand Band,
		const int32 Priority,
		const int32 FractionalPriorityTenths,
		const int32 Speed)
	{
		const FTrainerId TrainerId = MakeNumericId<FTrainerId>(TrainerValue);
		const FBattlerId BattlerId = MakeNumericId<FBattlerId>(BattlerValue);
		const FActiveSlotId ActingSlot = MakeActiveSlotId(Side, Position);
		FBattleDecision Decision;
		bool bCreated = false;
		if (ActionKind == EBattleActionKind::Fight)
		{
			bCreated = FBattleDecision::TryCreateFight(
				1,
				TrainerId,
				BattlerId,
				MakeDefinitionId<FMoveId>(NormalMoveName),
				MakeActiveSlotId(
					Side == EBattleSide::Player ? EBattleSide::Opponent : EBattleSide::Player,
					Position),
				Decision);
		}
		else if (ActionKind == EBattleActionKind::Bag)
		{
			bCreated = FBattleDecision::TryCreateBag(
				1,
				TrainerId,
				BattlerId,
				MakeDefinitionId<FItemId>(PotionName),
				MakePartySlotId(0),
				FActiveSlotId(),
				Decision);
		}
		else if (ActionKind == EBattleActionKind::Switch)
		{
			bCreated = FBattleDecision::TryCreateSwitch(
				1,
				EBattleDecisionRequestKind::Action,
				TrainerId,
				BattlerId,
				MakePartySlotId(2),
				ActingSlot,
				Decision);
		}
		else if (ActionKind == EBattleActionKind::Run)
		{
			bCreated = FBattleDecision::TryCreateSimpleAction(
				1,
				EBattleDecisionRequestKind::Action,
				TrainerId,
				BattlerId,
				ActionKind,
				Decision);
		}
		check(bCreated);

		FBattleActionOrderCandidate Candidate;
		Candidate.ActionId = MakeNumericId<FActionId>(ActionValue);
		Candidate.Decision = Decision;
		Candidate.OrderKey.CommandBand = Band;
		Candidate.OrderKey.MovePriority = Priority;
		Candidate.OrderKey.FractionalPriorityTenths = FractionalPriorityTenths;
		Candidate.OrderKey.EffectiveSpeed = Speed;
		Candidate.OrderKey.ActingSlotId = ActingSlot;
		if (ActionKind == EBattleActionKind::Fight)
		{
			Candidate.SelectedTargetBattlerId = MakeNumericId<FBattlerId>(
				Side == EBattleSide::Player ? OpponentLeftBattlerValue : PlayerLeftBattlerValue);
		}
		return Candidate;
	}

	FBattleActionQueueLockSpec MakeLockSpec(TArray<FBattleActionOrderCandidate> Candidates)
	{
		FBattleActionQueueLockSpec Spec;
		Spec.BattleId = MakeNumericId<FBattleId>(404);
		Spec.TurnId = MakeNumericId<FTurnId>(1);
		Spec.ResolutionId = MakeNumericId<FResolutionId>(1);
		Spec.Candidates = MoveTemp(Candidates);
		return Spec;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC04ASelectionLegalityTest,
	"PokemonSolarus.Battle.C04A.Selection.LegalityPPAndStruggle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC04ASelectionLegalityTest::RunTest(const FString& Parameters)
{
	const FMoveId NormalMove = MakeDefinitionId<FMoveId>(NormalMoveName);
	const FMoveId EmptyMove = MakeDefinitionId<FMoveId>(EmptyMoveName);
	const FMoveId Struggle = FBattleBuiltInMoveDefinitions::GetStruggleMoveId();

	TUniquePtr<FBattleEngine> Engine = MakeEngine(FSetupOptions());
	const uint64 InitialVersion = Engine->GetSnapshot().GetStateVersion();
	const int32 InitialPP = GetMovePP(*Engine, PlayerLeftBattlerValue, NormalMove);
	const int32 InitialTraceCount = Engine->ExportRandomTrace().Num();

	FBattleDecision InvalidDecision;
	const FBattleResolution Invalid = Engine->SubmitDecision(InvalidDecision);
	TestFalse(TEXT("An invalid decision is rejected before selection"), Invalid.WasAccepted());
	TestEqual(TEXT("Invalid input does not change the state version"), Engine->GetSnapshot().GetStateVersion(), InitialVersion);
	TestEqual(TEXT("Invalid input consumes no RNG"), Engine->ExportRandomTrace().Num(), InitialTraceCount);

	FBattleRejection Rejection;
	TestTrue(TEXT("Selection begins"), Engine->TryBeginActionDecisionSequence(Rejection));
	const FBattleDecisionRequest Request = Engine->GetPendingDecisionRequests()[0];
	TestTrue(TEXT("Fight is legal"), Request.GetLegalActionKinds().Contains(EBattleActionKind::Fight));
	TestTrue(TEXT("Bag is legal"), Request.GetLegalActionKinds().Contains(EBattleActionKind::Bag));
	TestTrue(TEXT("Switch is legal"), Request.GetLegalActionKinds().Contains(EBattleActionKind::Switch));
	TestTrue(TEXT("Wild Run is legal"), Request.GetLegalActionKinds().Contains(EBattleActionKind::Run));
	TestTrue(TEXT("A move with PP remains legal"), Request.GetLegalMoveIds().Contains(NormalMove));
	TestFalse(TEXT("A zero-PP move is not legal"), Request.GetLegalMoveIds().Contains(EmptyMove));
	TestTrue(TEXT("A zero-PP move keeps a typed reason"), HasUnavailableMove(Request, EmptyMove, EBattleOptionUnavailableReason::NoPP));
	TestFalse(TEXT("Struggle is not generated while any move has PP"), Request.GetLegalMoveIds().Contains(Struggle));

	FBattleDecision RunDecision;
	TestTrue(TEXT("Run payload is valid"), FBattleDecision::TryCreateSimpleAction(
		Request.GetStateVersion(),
		EBattleDecisionRequestKind::Action,
		Request.GetDecisionOwnerTrainerId(),
		Request.GetActingBattlerId(),
		EBattleActionKind::Run,
		RunDecision));
	TestTrue(TEXT("The request accepts legal wild Run"), Request.Allows(RunDecision, Rejection));

	FBattleDecision SwitchDecision;
	TestTrue(TEXT("Switch payload is valid"), FBattleDecision::TryCreateSwitch(
		Request.GetStateVersion(),
		EBattleDecisionRequestKind::Action,
		Request.GetDecisionOwnerTrainerId(),
		Request.GetActingBattlerId(),
		Request.GetLegalSwitchPartySlots()[0],
		Request.GetActingSlotId(),
		SwitchDecision));
	TestTrue(TEXT("The request accepts a legal voluntary switch"), Request.Allows(SwitchDecision, Rejection));

	const FBattleItemPartyTargetOption& ItemTarget = Request.GetLegalItemPartyTargets()[0];
	FBattleDecision BagDecision;
	TestTrue(TEXT("Bag payload is valid"), FBattleDecision::TryCreateBag(
		Request.GetStateVersion(),
		Request.GetDecisionOwnerTrainerId(),
		Request.GetActingBattlerId(),
		ItemTarget.ItemId,
		ItemTarget.PartySlotId,
		FActiveSlotId(),
		BagDecision));
	TestTrue(TEXT("The request accepts a legal owned item and target"), Request.Allows(BagDecision, Rejection));

	FBattleDecision WildFleeDecision;
	TestTrue(TEXT("WildFlee payload is structurally valid"), FBattleDecision::TryCreateSimpleAction(
		Request.GetStateVersion(),
		EBattleDecisionRequestKind::Action,
		Request.GetDecisionOwnerTrainerId(),
		Request.GetActingBattlerId(),
		EBattleActionKind::WildFlee,
		WildFleeDecision));
	TestFalse(TEXT("WildFlee is rejected without an authored selector policy"), Request.Allows(WildFleeDecision, Rejection));
	TestEqual(TEXT("WildFlee rejection is typed"), Rejection.Reason, EBattleRejectionReason::IllegalAction);

	FBattleDecision StaleDecision;
	TestTrue(TEXT("A stale Fight payload can be constructed"), FBattleDecision::TryCreateFight(
		Request.GetStateVersion() - 1,
		Request.GetDecisionOwnerTrainerId(),
		Request.GetActingBattlerId(),
		NormalMove,
		FindMoveTarget(Request, NormalMove)->ActiveSlotId,
		StaleDecision));
	const uint64 SelectingVersion = Engine->GetSnapshot().GetStateVersion();
	const FBattleResolution Stale = Engine->SubmitDecision(StaleDecision);
	TestFalse(TEXT("A stale selection is rejected"), Stale.WasAccepted());
	TestEqual(TEXT("Stale rejection is typed"), Stale.GetRejection().Reason, EBattleRejectionReason::StaleStateVersion);
	TestEqual(TEXT("A stale selection does not change gameplay state"), Engine->GetSnapshot().GetStateVersion(), SelectingVersion);
	TestEqual(TEXT("A stale selection consumes no PP"), GetMovePP(*Engine, PlayerLeftBattlerValue, NormalMove), InitialPP);
	TestEqual(TEXT("A stale selection consumes no RNG"), Engine->ExportRandomTrace().Num(), InitialTraceCount);

	FSetupOptions TrainerOptions;
	TrainerOptions.EncounterKind = EBattleEncounterKind::Trainer;
	TUniquePtr<FBattleEngine> TrainerEngine = MakeEngine(TrainerOptions);
	TestTrue(TEXT("Trainer selection begins"), TrainerEngine->TryBeginActionDecisionSequence(Rejection));
	const FBattleDecisionRequest TrainerRequest = TrainerEngine->GetPendingDecisionRequests()[0];
	FBattleDecision BlockedRun;
	TestTrue(TEXT("Blocked Run payload is structurally valid"), FBattleDecision::TryCreateSimpleAction(
		TrainerRequest.GetStateVersion(),
		EBattleDecisionRequestKind::Action,
		TrainerRequest.GetDecisionOwnerTrainerId(),
		TrainerRequest.GetActingBattlerId(),
		EBattleActionKind::Run,
		BlockedRun));
	const FBattleResolution Blocked = TrainerEngine->SubmitDecision(BlockedRun);
	TestFalse(TEXT("Trainer Run is rejected"), Blocked.WasAccepted());
	TestEqual(TEXT("Trainer Run has a typed illegal-action result"), Blocked.GetRejection().Reason, EBattleRejectionReason::IllegalAction);
	TestEqual(TEXT("Blocked Run consumes no PP"), GetMovePP(*TrainerEngine, PlayerLeftBattlerValue, NormalMove), InitialPP);
	TestEqual(TEXT("Blocked Run consumes no RNG"), TrainerEngine->ExportRandomTrace().Num(), 0);

	FSetupOptions EmptyOptions;
	EmptyOptions.bAllMovesEmpty = true;
	TUniquePtr<FBattleEngine> EmptyEngine = MakeEngine(EmptyOptions);
	TestTrue(TEXT("Empty-PP selection begins"), EmptyEngine->TryBeginActionDecisionSequence(Rejection));
	const FBattleDecisionRequest EmptyRequest = EmptyEngine->GetPendingDecisionRequests()[0];
	TestEqual(TEXT("Exactly one fallback move is legal"), EmptyRequest.GetLegalMoveIds().Num(), 1);
	TestTrue(TEXT("The fallback is engine-supplied Struggle"), EmptyRequest.GetLegalMoveIds()[0] == Struggle);
	TestTrue(TEXT("Original moves remain visible with NoPP"), HasUnavailableMove(EmptyRequest, NormalMove, EBattleOptionUnavailableReason::NoPP));
	const FBattleResolution Locked = LockAllFights(
		*EmptyEngine,
		[Struggle](const uint64) { return Struggle; });
	TestTrue(TEXT("The all-Struggle turn locks"), Locked.WasAccepted());
	TestEqual(TEXT("Selection and locking spend no PP"), GetMovePP(*EmptyEngine, PlayerLeftBattlerValue, NormalMove), 0);
	TestEqual(TEXT("Two Single actions are locked"), EmptyEngine->GetLockedActions().Num(), 2);

	const FBattleResolution Started = EmptyEngine->BeginNextLockedAction();
	TestTrue(TEXT("Struggle passes its action-start gate"), Started.WasAccepted());
	const FBattleResolution Committed = EmptyEngine->CommitCurrentMoveAfterPreMoveGates();
	TestTrue(TEXT("Struggle commits without authored PP"), Committed.WasAccepted());
	TestFalse(TEXT("Struggle emits no PP consumption"), HasEvent(Committed, EBattleEventType::PPConsumed));
	TestTrue(TEXT("Struggle still emits MoveUsed"), HasEvent(Committed, EBattleEventType::MoveUsed));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC04AOrderIntegrationTest,
	"PokemonSolarus.Battle.C04A.Order.CommandPrioritySpeedAndEvents",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC04AOrderIntegrationTest::RunTest(const FString& Parameters)
{
	TArray<FBattleActionOrderCandidate> BandCandidates;
	BandCandidates.Add(MakeOrderCandidate(1, PlayerTrainerValue, PlayerLeftBattlerValue, EBattleSide::Player, EBattlePosition::Left, EBattleActionKind::Fight, EBattleActionCommandBand::Move, 0, 0, 500));
	BandCandidates.Add(MakeOrderCandidate(2, PlayerTrainerValue, PlayerRightBattlerValue, EBattleSide::Player, EBattlePosition::Right, EBattleActionKind::Switch, EBattleActionCommandBand::VoluntarySwitch, 0, 0, 10));
	BandCandidates.Add(MakeOrderCandidate(3, OpponentTrainerValue, OpponentLeftBattlerValue, EBattleSide::Opponent, EBattlePosition::Left, EBattleActionKind::Bag, EBattleActionCommandBand::Bag, 0, 0, 300));
	BandCandidates.Add(MakeOrderCandidate(4, OpponentTrainerValue, OpponentRightBattlerValue, EBattleSide::Opponent, EBattlePosition::Right, EBattleActionKind::Run, EBattleActionCommandBand::Run, 0, 0, 1));
	FSequenceBattleRandom NoTieRandom({});
	TArray<FBattleLockedAction> BandQueue;
	EBattleActionQueueError QueueError = EBattleActionQueueError::None;
	TestTrue(TEXT("Command-band candidates lock"), FBattleActionQueueResolver::TryLock(
		MakeLockSpec(MoveTemp(BandCandidates)),
		NoTieRandom,
		BandQueue,
		QueueError));
	TestEqual(TEXT("Run is first"), BandQueue[0].Decision.GetActionKind(), EBattleActionKind::Run);
	TestEqual(TEXT("Voluntary switch is second"), BandQueue[1].Decision.GetActionKind(), EBattleActionKind::Switch);
	TestEqual(TEXT("Bag is third"), BandQueue[2].Decision.GetActionKind(), EBattleActionKind::Bag);
	TestEqual(TEXT("Moves are last"), BandQueue[3].Decision.GetActionKind(), EBattleActionKind::Fight);
	TestEqual(TEXT("Band ordering consumes no tie RNG"), NoTieRandom.GetTrace().Num(), 0);

	FSetupOptions Options;
	Options.Format = EBattleFormat::Double;
	Options.PlayerLeftSpeed = 40;
	Options.PlayerRightSpeed = 200;
	Options.OpponentLeftSpeed = 150;
	Options.OpponentRightSpeed = 300;
	TUniquePtr<FBattleEngine> Engine = MakeEngine(Options);
	const FMoveId NormalMove = MakeDefinitionId<FMoveId>(NormalMoveName);
	const FMoveId PriorityMove = MakeDefinitionId<FMoveId>(PriorityMoveName);
	const FMoveId NegativeMove = MakeDefinitionId<FMoveId>(NegativePriorityMoveName);
	const FBattleResolution Locked = LockAllFights(
		*Engine,
		[NormalMove, PriorityMove, NegativeMove](const uint64 BattlerValue)
		{
			if (BattlerValue == PlayerLeftBattlerValue)
			{
				return PriorityMove;
			}
			if (BattlerValue == OpponentRightBattlerValue)
			{
				return NegativeMove;
			}
			return NormalMove;
		});

	const TArray<FBattleLockedAction> Queue = Engine->GetLockedActions();
	TestEqual(TEXT("Four Double actions lock"), Queue.Num(), 4);
	TestEqual(TEXT("Positive priority wins regardless of Speed"), Queue[0].Decision.GetActingBattlerId().GetValue(), PlayerLeftBattlerValue);
	TestEqual(TEXT("Faster priority-zero battler is next"), Queue[1].Decision.GetActingBattlerId().GetValue(), PlayerRightBattlerValue);
	TestEqual(TEXT("Slower priority-zero battler follows"), Queue[2].Decision.GetActingBattlerId().GetValue(), OpponentLeftBattlerValue);
	TestEqual(TEXT("Negative priority remains last"), Queue[3].Decision.GetActingBattlerId().GetValue(), OpponentRightBattlerValue);

	int32 OrderEventCount = 0;
	for (const FBattleEvent& Event : Locked.GetEvents())
	{
		if (Event.GetType() != EBattleEventType::ActionOrderLocked)
		{
			continue;
		}
		TestTrue(TEXT("Order event has metadata"), Event.GetActionOrder().IsSet());
		TestEqual(TEXT("Order event is core-only"), Event.GetVisibility().Level, EBattleVisibilityLevel::CoreOnly);
		TestEqual(TEXT("Metadata ordinal follows event order"), Event.GetActionOrder()->QueueOrdinal, static_cast<uint64>(OrderEventCount + 1));
		TestTrue(TEXT("Metadata action ID is stable"), Event.GetActionId() == Queue[OrderEventCount].ActionId);
		++OrderEventCount;
	}
	TestEqual(TEXT("Every locked action gets one order event"), OrderEventCount, 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC04ATieRulesTest,
	"PokemonSolarus.Battle.C04A.Order.CrossSideAndSameSideTies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC04ATieRulesTest::RunTest(const FString& Parameters)
{
	TArray<FBattleActionOrderCandidate> FourWay;
	FourWay.Add(MakeOrderCandidate(1, OpponentTrainerValue, OpponentRightBattlerValue, EBattleSide::Opponent, EBattlePosition::Right, EBattleActionKind::Fight, EBattleActionCommandBand::Move, 0, 0, 100));
	FourWay.Add(MakeOrderCandidate(2, PlayerTrainerValue, PlayerLeftBattlerValue, EBattleSide::Player, EBattlePosition::Left, EBattleActionKind::Fight, EBattleActionCommandBand::Move, 0, 0, 100));
	FourWay.Add(MakeOrderCandidate(3, OpponentTrainerValue, OpponentLeftBattlerValue, EBattleSide::Opponent, EBattlePosition::Left, EBattleActionKind::Fight, EBattleActionCommandBand::Move, 0, 0, 100));
	FourWay.Add(MakeOrderCandidate(4, PlayerTrainerValue, PlayerRightBattlerValue, EBattleSide::Player, EBattlePosition::Right, EBattleActionKind::Fight, EBattleActionCommandBand::Move, 0, 0, 100));
	FSequenceBattleRandom TieRandom({1, 0});
	TArray<FBattleLockedAction> Queue;
	EBattleActionQueueError QueueError = EBattleActionQueueError::None;
	TestTrue(TEXT("A four-way tie locks"), FBattleActionQueueResolver::TryLock(
		MakeLockSpec(MoveTemp(FourWay)),
		TieRandom,
		Queue,
		QueueError));
	TestEqual(TEXT("Player-side actions occupy the first two slots"), Queue[0].OrderKey.ActingSlotId.GetSide(), EBattleSide::Player);
	TestEqual(TEXT("The second action is also player-side"), Queue[1].OrderKey.ActingSlotId.GetSide(), EBattleSide::Player);
	TestEqual(TEXT("Player draw 1 reverses stable Left/Right"), Queue[0].Decision.GetActingBattlerId().GetValue(), PlayerRightBattlerValue);
	TestEqual(TEXT("Player Left follows after reversal"), Queue[1].Decision.GetActingBattlerId().GetValue(), PlayerLeftBattlerValue);
	TestEqual(TEXT("Opponent draw 0 keeps stable Left/Right"), Queue[2].Decision.GetActingBattlerId().GetValue(), OpponentLeftBattlerValue);
	TestEqual(TEXT("Opponent Right stays fourth"), Queue[3].Decision.GetActingBattlerId().GetValue(), OpponentRightBattlerValue);
	TestEqual(TEXT("Exactly one draw is used per same-side tied group"), TieRandom.GetTrace().Num(), 2);
	for (const FBattleRandomDraw& Draw : TieRandom.GetTrace())
	{
		TestEqual(TEXT("Tie draw uses U[0,1] minimum"), Draw.InclusiveMinimum, static_cast<uint32>(0));
		TestEqual(TEXT("Tie draw uses U[0,1] maximum"), Draw.InclusiveMaximum, static_cast<uint32>(1));
		TestEqual(TEXT("Tie draw carries its frozen rule key"), Draw.RulePurpose.GetName(), FName(TEXT("Rule.ActionOrder.SameSideTie")));
	}

	TArray<FBattleActionOrderCandidate> CrossSide;
	CrossSide.Add(MakeOrderCandidate(10, OpponentTrainerValue, OpponentLeftBattlerValue, EBattleSide::Opponent, EBattlePosition::Left, EBattleActionKind::Fight, EBattleActionCommandBand::Move, 0, 0, 100));
	CrossSide.Add(MakeOrderCandidate(11, PlayerTrainerValue, PlayerLeftBattlerValue, EBattleSide::Player, EBattlePosition::Left, EBattleActionKind::Fight, EBattleActionCommandBand::Move, 0, 0, 100));
	FSequenceBattleRandom CrossSideRandom({1});
	TArray<FBattleLockedAction> CrossSideQueue;
	TestTrue(TEXT("A cross-side tie locks"), FBattleActionQueueResolver::TryLock(
		MakeLockSpec(MoveTemp(CrossSide)),
		CrossSideRandom,
		CrossSideQueue,
		QueueError));
	TestEqual(TEXT("Player side wins a cross-side tie"), CrossSideQueue[0].OrderKey.ActingSlotId.GetSide(), EBattleSide::Player);
	TestEqual(TEXT("Cross-side ties consume no draw"), CrossSideRandom.GetTrace().Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC04AQuickClawKeyTest,
	"PokemonSolarus.Battle.C04A.Order.QuickClawKeysAndFrozenOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC04AQuickClawKeyTest::RunTest(const FString& Parameters)
{
	TArray<FBattleActionOrderCandidate> Candidates;
	Candidates.Add(MakeOrderCandidate(1, PlayerTrainerValue, PlayerLeftBattlerValue, EBattleSide::Player, EBattlePosition::Left, EBattleActionKind::Fight, EBattleActionCommandBand::Move, 0, 1, 10));
	Candidates.Add(MakeOrderCandidate(2, OpponentTrainerValue, OpponentLeftBattlerValue, EBattleSide::Opponent, EBattlePosition::Left, EBattleActionKind::Fight, EBattleActionCommandBand::Move, 0, 0, 500));
	FSequenceBattleRandom Random({});
	TArray<FBattleLockedAction> Queue;
	EBattleActionQueueError QueueError = EBattleActionQueueError::None;
	TestTrue(TEXT("Fractional-priority candidates lock"), FBattleActionQueueResolver::TryLock(
		MakeLockSpec(Candidates),
		Random,
		Queue,
		QueueError));
	TestEqual(TEXT("A generic +0.1 hook key beats ordinary priority zero"), Queue[0].Decision.GetActingBattlerId().GetValue(), PlayerLeftBattlerValue);
	TestEqual(TEXT("The frozen key preserves +0.1 as one tenth"), Queue[0].OrderKey.FractionalPriorityTenths, 1);

	Candidates[0].OrderKey.EffectiveSpeed = 1;
	Candidates[1].OrderKey.EffectiveSpeed = 1000;
	TestEqual(TEXT("Later source-stat changes do not rewrite the locked actor"), Queue[0].Decision.GetActingBattlerId().GetValue(), PlayerLeftBattlerValue);
	TestEqual(TEXT("Later source-stat changes do not rewrite the locked Speed key"), Queue[0].OrderKey.EffectiveSpeed, 10);

	TArray<FBattleActionOrderCandidate> NegativePriority;
	NegativePriority.Add(MakeOrderCandidate(3, PlayerTrainerValue, PlayerLeftBattlerValue, EBattleSide::Player, EBattlePosition::Left, EBattleActionKind::Fight, EBattleActionCommandBand::Move, -1, 1, 500));
	NegativePriority.Add(MakeOrderCandidate(4, OpponentTrainerValue, OpponentLeftBattlerValue, EBattleSide::Opponent, EBattlePosition::Left, EBattleActionKind::Fight, EBattleActionCommandBand::Move, 0, 0, 1));
	TArray<FBattleLockedAction> NegativeQueue;
	TestTrue(TEXT("Negative-priority hook candidates lock"), FBattleActionQueueResolver::TryLock(
		MakeLockSpec(MoveTemp(NegativePriority)),
		Random,
		NegativeQueue,
		QueueError));
	TestEqual(TEXT("Negative priority plus 0.1 remains below priority zero"), NegativeQueue[0].Decision.GetActingBattlerId().GetValue(), OpponentLeftBattlerValue);
	TestEqual(TEXT("No non-tie Quick Claw key spends RNG in C04A"), Random.GetTrace().Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC04AStartRulesTest,
	"PokemonSolarus.Battle.C04A.Execution.PreflightResourcesAndObedience",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC04AStartRulesTest::RunTest(const FString& Parameters)
{
	auto TestNoResources = [this](const TCHAR* Prefix, const FBattleActionStartResult& Result)
	{
		TestFalse(FString::Printf(TEXT("%s consumes no PP"), Prefix), Result.bConsumesPP);
		TestFalse(FString::Printf(TEXT("%s consumes no item"), Prefix), Result.bConsumesItem);
		TestFalse(FString::Printf(TEXT("%s consumes no RNG"), Prefix), Result.bConsumesRng);
	};

	FBattleActionStartFacts Facts;
	Facts.ActionKind = EBattleActionKind::Fight;
	Facts.bActorActive = false;
	Facts.bActorLiving = true;
	FBattleActionStartResult Result;
	TestTrue(TEXT("Inactive actor facts are valid"), FBattleActionStartRules::TryEvaluate(Facts, Result));
	TestEqual(TEXT("Inactive actor is skipped"), Result.Outcome, EBattleActionStartOutcome::ActorUnavailable);
	TestTrue(TEXT("Inactive actor ends its committed slot"), Result.bEndsCommittedAction);
	TestNoResources(TEXT("Inactive actor"), Result);

	Facts.bActorActive = true;
	Facts.bActorLiving = true;
	Facts.bSelectedTargetCaptured = true;
	TestTrue(TEXT("Captured-target facts are valid"), FBattleActionStartRules::TryEvaluate(Facts, Result));
	TestEqual(TEXT("Captured selected target cancels the move"), Result.Outcome, EBattleActionStartOutcome::CapturedTargetCanceled);
	TestTrue(TEXT("Captured-target cancellation ends its committed slot"), Result.bEndsCommittedAction);
	TestNoResources(TEXT("Captured target"), Result);

	Facts.bSelectedTargetCaptured = false;
	Facts.bSubjectToPlayerObedience = true;
	Facts.ObedienceReferenceLevel = 20;
	Facts.BadgeCount = 0;
	TestTrue(TEXT("At-cap obedience facts are valid"), FBattleActionStartRules::TryEvaluate(Facts, Result));
	TestEqual(TEXT("Reference level at the cap obeys"), Result.Outcome, EBattleActionStartOutcome::Proceed);
	TestEqual(TEXT("Zero badges freeze a level-20 cap"), Result.ObedienceCap.GetValue(), static_cast<uint8>(20));
	TestNoResources(TEXT("Obedient gate"), Result);

	Facts.ObedienceReferenceLevel = 21;
	TestTrue(TEXT("Above-cap obedience facts are valid"), FBattleActionStartRules::TryEvaluate(Facts, Result));
	TestEqual(TEXT("Above-cap Solarus behavior is deterministic refusal"), Result.Outcome, EBattleActionStartOutcome::ObedienceRefused);
	TestTrue(TEXT("Refusal consumes the committed action slot"), Result.bEndsCommittedAction);
	TestNoResources(TEXT("Refusal"), Result);

	Facts.ObedienceReferenceLevel = 100;
	Facts.BadgeCount = 8;
	TestTrue(TEXT("Eight-badge obedience facts are valid"), FBattleActionStartRules::TryEvaluate(Facts, Result));
	TestEqual(TEXT("Eight badges allow level 100"), Result.Outcome, EBattleActionStartOutcome::Proceed);
	TestEqual(TEXT("Eight badges freeze the all-level cap"), Result.ObedienceCap.GetValue(), static_cast<uint8>(100));

	Facts.ObedienceReferenceLevel = 0;
	TestFalse(TEXT("Invalid frozen obedience input is rejected"), FBattleActionStartRules::TryEvaluate(Facts, Result));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC04AEngineObedienceTest,
	"PokemonSolarus.Battle.C04A.Execution.EngineObediencePPAndVisibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC04AEngineObedienceTest::RunTest(const FString& Parameters)
{
	const FMoveId NormalMove = MakeDefinitionId<FMoveId>(NormalMoveName);
	FSetupOptions RefusalOptions;
	RefusalOptions.PlayerReferenceLevel = 21;
	RefusalOptions.PlayerBadgeCount = 0;
	RefusalOptions.PlayerLeftSpeed = 200;
	RefusalOptions.OpponentLeftSpeed = 100;
	TUniquePtr<FBattleEngine> RefusalEngine = MakeEngine(RefusalOptions);
	FBattleRejection Rejection;
	TestTrue(TEXT("Refusal fixture begins selection"), RefusalEngine->TryBeginActionDecisionSequence(Rejection));
	TArray<FBattleDecisionRequest> Requests = RefusalEngine->GetPendingDecisionRequests();
	const FBattleResolution PlayerAccepted = RefusalEngine->SubmitDecisionBatch(
		MakeBatch(Requests, {MakeFightDecision(Requests[0], NormalMove)}));
	TestTrue(TEXT("Player command is accepted before obedience"), PlayerAccepted.WasAccepted());
	Requests = RefusalEngine->GetPendingDecisionRequests();
	const FBattleSnapshot EnemyObservation = RefusalEngine->GetSnapshotForObserver(
		MakeNumericId<FTrainerId>(OpponentTrainerValue));
	TestEqual(TEXT("Enemy observation cannot read the player's unexecuted command"), EnemyObservation.GetVisibleSelections().Num(), 0);
	TestTrue(TEXT("Enemy command locks the turn"), RefusalEngine->SubmitDecisionBatch(
		MakeBatch(Requests, {MakeFightDecision(Requests[0], NormalMove)})).WasAccepted());

	const int32 PPBeforeRefusal = GetMovePP(*RefusalEngine, PlayerLeftBattlerValue, NormalMove);
	const int32 TraceBeforeRefusal = RefusalEngine->ExportRandomTrace().Num();
	const FBattleResolution Refused = RefusalEngine->BeginNextLockedAction();
	TestTrue(TEXT("Deterministic refusal is an accepted action result"), Refused.WasAccepted());
	TestTrue(TEXT("Refusal emits a typed event"), HasEvent(Refused, EBattleEventType::ObedienceRefused));
	TestTrue(TEXT("Refused action completes immediately"), HasEvent(Refused, EBattleEventType::ActionCompleted));
	TestEqual(TEXT("Refusal consumes no PP"), GetMovePP(*RefusalEngine, PlayerLeftBattlerValue, NormalMove), PPBeforeRefusal);
	TestEqual(TEXT("Refusal consumes no RNG"), RefusalEngine->ExportRandomTrace().Num(), TraceBeforeRefusal);
	TestFalse(TEXT("A refused action is not exposed as current"), RefusalEngine->GetCurrentLockedAction().IsSet());

	FSetupOptions ObeyOptions;
	ObeyOptions.PlayerReferenceLevel = 20;
	ObeyOptions.PlayerBadgeCount = 0;
	ObeyOptions.PlayerLeftSpeed = 200;
	ObeyOptions.OpponentLeftSpeed = 100;
	TUniquePtr<FBattleEngine> ObeyEngine = MakeEngine(ObeyOptions);
	LockAllFights(*ObeyEngine, [NormalMove](const uint64) { return NormalMove; });
	const int32 PPBeforeCommit = GetMovePP(*ObeyEngine, PlayerLeftBattlerValue, NormalMove);
	const FBattleResolution Began = ObeyEngine->BeginNextLockedAction();
	TestTrue(TEXT("At-cap move begins"), Began.WasAccepted());
	TestTrue(TEXT("At-cap move emits typed obedience confirmation"), HasEvent(Began, EBattleEventType::ObedienceConfirmed));
	TestEqual(TEXT("Action-start gates do not spend PP"), GetMovePP(*ObeyEngine, PlayerLeftBattlerValue, NormalMove), PPBeforeCommit);
	TestTrue(TEXT("Proceeding action is available to later resolvers"), ObeyEngine->GetCurrentLockedAction().IsSet());
	const FBattleResolution Committed = ObeyEngine->CommitCurrentMoveAfterPreMoveGates();
	TestTrue(TEXT("Move commitment succeeds"), Committed.WasAccepted());
	TestTrue(TEXT("Ordinary move emits PP consumption"), HasEvent(Committed, EBattleEventType::PPConsumed));
	TestTrue(TEXT("Ordinary move emits MoveUsed"), HasEvent(Committed, EBattleEventType::MoveUsed));
	TestEqual(TEXT("Exactly one PP is spent at commitment"), GetMovePP(*ObeyEngine, PlayerLeftBattlerValue, NormalMove), PPBeforeCommit - 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC04AReplayTest,
	"PokemonSolarus.Battle.C04A.Replay.DeterministicLockedQueue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC04AReplayTest::RunTest(const FString& Parameters)
{
	FSetupOptions Options;
	Options.Format = EBattleFormat::Double;
	Options.PlayerLeftSpeed = 100;
	Options.PlayerRightSpeed = 100;
	Options.OpponentLeftSpeed = 100;
	Options.OpponentRightSpeed = 100;
	const FMoveId NormalMove = MakeDefinitionId<FMoveId>(NormalMoveName);
	TUniquePtr<FBattleEngine> First = MakeEngine(Options, 9001);
	TUniquePtr<FBattleEngine> Second = MakeEngine(Options, 9001);
	LockAllFights(*First, [NormalMove](const uint64) { return NormalMove; });
	LockAllFights(*Second, [NormalMove](const uint64) { return NormalMove; });

	const TArray<FBattleLockedAction> FirstQueue = First->GetLockedActions();
	const TArray<FBattleLockedAction> SecondQueue = Second->GetLockedActions();
	TestEqual(TEXT("Both queues contain four actions"), FirstQueue.Num(), 4);
	TestEqual(TEXT("Both deterministic traces contain two same-side tie draws"), First->ExportRandomTrace().Num(), 2);
	TestTrue(TEXT("Identical seeds produce identical tie traces"), First->ExportRandomTrace() == Second->ExportRandomTrace());
	for (int32 Index = 0; Index < FirstQueue.Num(); ++Index)
	{
		TestTrue(TEXT("Action IDs reproduce"), FirstQueue[Index].ActionId == SecondQueue[Index].ActionId);
		TestTrue(TEXT("Actor order reproduces"), FirstQueue[Index].Decision.GetActingBattlerId() == SecondQueue[Index].Decision.GetActingBattlerId());
		TestEqual(TEXT("Queue ordinals reproduce"), FirstQueue[Index].QueueOrdinal, SecondQueue[Index].QueueOrdinal);
	}

	const FBattleReplayRecord FirstRecord = First->ExportReplayRecord();
	const FBattleReplayRecord SecondRecord = Second->ExportReplayRecord();
	TestEqual(TEXT("Replay schema includes C04B targeting"), FirstRecord.GetSchemaVersion(), static_cast<uint32>(4));
	TArray<uint8> FirstBytes;
	TArray<uint8> SecondBytes;
	FBattleRejection Rejection;
	TestTrue(TEXT("First replay serializes"), FBattleReplaySerializer::TrySerializeCanonical(FirstRecord, FirstBytes, Rejection));
	TestTrue(TEXT("Second replay serializes"), FBattleReplaySerializer::TrySerializeCanonical(SecondRecord, SecondBytes, Rejection));
	TestTrue(TEXT("Identical setup, decisions, and RNG produce identical canonical bytes"), FirstBytes == SecondBytes);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
