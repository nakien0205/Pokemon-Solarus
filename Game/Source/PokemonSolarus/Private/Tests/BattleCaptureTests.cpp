#if WITH_DEV_AUTOMATION_TESTS

#include "Battle/BattleAbility.h"
#include "Battle/BattleBagItem.h"
#include "Battle/BattleCapture.h"
#include "Battle/BattleEngine.h"
#include "Battle/BattleMajorStatus.h"
#include "Battle/BattleState.h"
#include "BattleTestFactories.h"
#include "Misc/AutomationTest.h"

class FBattleC09BCaptureEngineFixture
{
public:
	static const FBattleEngineState& GetState(const FBattleEngine& Engine)
	{
		check(Engine.State.IsValid());
		return *Engine.State;
	}

	static const FBattleBattlerState* GetBattler(
		const FBattleEngine& Engine,
		const FBattlerId BattlerId)
	{
		return GetState(Engine).FindBattler(BattlerId);
	}

	static bool IsActive(
		const FBattleEngine& Engine,
		const FBattlerId BattlerId)
	{
		return GetState(Engine).ActivePositions.ContainsByPredicate(
			[BattlerId](const FBattleActivePositionState& Position)
			{
				return Position.BattlerId == BattlerId;
			});
	}
};

namespace
{
	using BattleTest::MakeActiveSlotId;
	using BattleTest::MakeDefinitionId;
	using BattleTest::MakeNumericId;
	using BattleTest::MakePartySlotId;

	constexpr uint64 PlayerTrainerValue = 1;
	constexpr uint64 OpponentTrainerValue = 2;
	constexpr uint64 PlayerLeftValue = 11;
	constexpr uint64 PlayerRightValue = 12;
	constexpr uint64 OpponentLeftValue = 21;
	constexpr uint64 OpponentRightValue = 22;
	constexpr uint64 ReinforcementValue = 23;

	const TCHAR* PlayerSpeciesName = TEXT("Species.C09B.Capture.Player");
	const TCHAR* TargetSpeciesName = TEXT("Species.C09B.Capture.Target");
	const TCHAR* ReinforcementSpeciesName = TEXT("Species.C09B.Capture.Reinforcement");
	const TCHAR* ProbeMoveName = TEXT("Move.C09B.Capture.Probe");
	const TCHAR* TagMoveName = TEXT("Move.C09B.Capture.Tag");
	const TCHAR* LeftHeldItemName = TEXT("Item.C09B.Capture.LeftHeld");
	const TCHAR* RightHeldItemName = TEXT("Item.C09B.Capture.RightHeld");

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
				- static_cast<uint64>(InclusiveMinimum) + 1ULL;
			OutDraw.RawValue = Result;
			OutDraw.Result = Result;
			OutDraw.CallOrdinal = static_cast<uint64>(Trace.Num()) + 1ULL;
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

	struct FCaptureScenario
	{
		EBattleFormat Format = EBattleFormat::Single;
		int32 CatchRate = 45;
		int32 TargetLevel = 50;
		int32 LeftHP = 200;
		int32 RightHP = 200;
		int32 PartyCapacity = 1;
		int32 StorageCapacity = 1;
		int32 PokeBallCount = 3;
		int32 PlayerLeftSpeed = 400;
		int32 OpponentLeftSpeed = 200;
		EBattleCaptureSpeciesClassification LeftClassification =
			EBattleCaptureSpeciesClassification::Normal;
		EBattleCaptureSpeciesClassification RightClassification =
			EBattleCaptureSpeciesClassification::Normal;
		bool bConfiguredReinforcement = false;
		FBattleCaptureProgressionSnapshot Progression;
	};

	FCaptureScenario MakeScenario()
	{
		FCaptureScenario Scenario;
		Scenario.Progression.bHasSnapshot = true;
		Scenario.Progression.BadgeCount = 8;
		Scenario.Progression.CaptureCoefficientQ12 = 4096;
		Scenario.Progression.bMustCapture = true;
		return Scenario;
	}

	TArray<FBattleTypeChartEntry> MakeNeutralTypeChart()
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

	FBattleMoveDefinition MakeProbeMove()
	{
		FBattleMoveDefinition Move;
		Move.Id = MakeDefinitionId<FMoveId>(ProbeMoveName);
		Move.Type = EPokemonType::Normal;
		Move.Category = EBattleMoveCategory::Status;
		Move.bAlwaysHits = true;
		Move.BasePP = 20;
		Move.TargetClass = EBattleTargetClass::SelectedOpponent;
		FBattleMoveEffectDescriptor Effect;
		Effect.Kind = EBattleMoveEffectKind::ModifyStatStage;
		Effect.Target = EBattleEffectTarget::User;
		Effect.Stat = EBattleStat::Attack;
		Effect.MagnitudeNumerator = 1;
		Move.Effects.Add(Effect);
		return Move;
	}

	FBattleMoveDefinition MakeTagMove()
	{
		FBattleMoveDefinition Move;
		Move.Id = MakeDefinitionId<FMoveId>(TagMoveName);
		Move.Type = EPokemonType::Normal;
		Move.Category = EBattleMoveCategory::Status;
		Move.bAlwaysHits = true;
		Move.BasePP = 20;
		Move.TargetClass = EBattleTargetClass::SelectedOpponent;
		FBattleMoveEffectDescriptor Effect;
		Effect.Kind = EBattleMoveEffectKind::ApplyCondition;
		Effect.Target = EBattleEffectTarget::ResolvedTarget;
		Effect.ConditionId = FBattleMajorStatusRules::GetParalysisId();
		Move.Effects.Add(Effect);
		return Move;
	}

	FBattleSpeciesFormDefinition MakeSpecies(
		const TCHAR* Name,
		const int32 CatchRate)
	{
		FBattleSpeciesFormDefinition Species;
		Species.Id = MakeDefinitionId<FSpeciesFormId>(Name);
		Species.PrimaryType = EPokemonType::Normal;
		Species.BaseStats = {80, 80, 80, 80, 80, 80};
		Species.CatchRate = CatchRate;
		Species.AbilityChoices.Add(FBattleAbilityRules::GetBlazeId());
		return Species;
	}

	FBattleDefinitionCatalog MakeCatalog(const int32 TargetCatchRate)
	{
		FBattleDefinitionCatalogInput Input;
		Input.TypeChartEntries = MakeNeutralTypeChart();
		Input.Moves.Add(MakeProbeMove());
		Input.Moves.Add(MakeTagMove());
		Input.Abilities.Add({FBattleAbilityRules::GetBlazeId()});
		Input.Items.Add({FBattleBagItemRules::GetPokeBallId(), EBattleItemKind::Capture});
		Input.Items.Add({MakeDefinitionId<FItemId>(LeftHeldItemName), EBattleItemKind::Held});
		Input.Items.Add({MakeDefinitionId<FItemId>(RightHeldItemName), EBattleItemKind::Held});
		for (const FConditionId& StatusId : FBattleMajorStatusRules::GetCanonicalIds())
		{
			Input.Conditions.Add({StatusId, EBattleConditionKind::MajorStatus});
		}
		Input.SpeciesForms.Add(MakeSpecies(PlayerSpeciesName, 45));
		Input.SpeciesForms.Add(MakeSpecies(TargetSpeciesName, TargetCatchRate));
		Input.SpeciesForms.Add(MakeSpecies(ReinforcementSpeciesName, 45));

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
		const EBattleDecisionController Controller,
		const int32 PokeBallCount)
	{
		FBattleTrainerSetup Trainer;
		Trainer.TrainerId = MakeNumericId<FTrainerId>(TrainerValue);
		Trainer.Side = Side;
		Trainer.Role = Role;
		Trainer.Controller = Controller;
		Trainer.SelectorProfileId = MakeDefinitionId<FDefinitionId>(
			Role == EBattleTrainerRole::Player
				? TEXT("Selector.C09B.Capture.Player")
				: TEXT("Selector.C09B.Capture.Opponent"));
		if (PokeBallCount >= 0)
		{
			Trainer.Bag.Add({FBattleBagItemRules::GetPokeBallId(), PokeBallCount});
		}
		return Trainer;
	}

	FBattlePartyEntrySetup MakePartyEntry(
		const uint64 TrainerValue,
		const uint64 BattlerValue,
		const int32 PartyIndex,
		const TCHAR* SpeciesName,
		const int32 Level,
		const int32 CurrentHP,
		const int32 Speed,
		const EBattleCaptureSpeciesClassification Classification,
		const int32 ProbePP,
		const FItemId OriginalItemId = FItemId(),
		const FItemId CurrentItemId = FItemId(),
		const bool bAddTagMove = false)
	{
		FBattlePartyEntrySetup Entry;
		Entry.TrainerId = MakeNumericId<FTrainerId>(TrainerValue);
		Entry.BattlerId = MakeNumericId<FBattlerId>(BattlerValue);
		Entry.SourcePokemonId = MakeNumericId<FSourcePokemonId>(1000 + BattlerValue);
		Entry.PartySlotId = MakePartySlotId(PartyIndex);
		Entry.SpeciesFormId = MakeDefinitionId<FSpeciesFormId>(SpeciesName);
		Entry.Level = Level;
		Entry.Stats = {200, 100, 100, 100, 100, Speed};
		Entry.CurrentHP = CurrentHP;
		Entry.AbilityId = FBattleAbilityRules::GetBlazeId();
		Entry.OriginalHeldItemId = OriginalItemId;
		Entry.CurrentHeldItemId = CurrentItemId;
		Entry.CaptureClassification = Classification;
		Entry.Moves.Add({0, MakeDefinitionId<FMoveId>(ProbeMoveName), ProbePP, 20});
		if (bAddTagMove)
		{
			Entry.Moves.Add({1, MakeDefinitionId<FMoveId>(TagMoveName), 20, 20});
		}
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
			MakeNumericId<FBattlerId>(BattlerValue)};
	}

	FBattleSetupInput MakeSetupInput(const FCaptureScenario& Scenario)
	{
		FBattleSetupInput Input;
		Input.BattleId = MakeNumericId<FBattleId>(9092);
		Input.SettingsReference = {
			MakeDefinitionId<FDefinitionId>(TEXT("Settings.C09B.Capture")),
			1};
		Input.CatalogReference = {
			MakeDefinitionId<FDefinitionId>(TEXT("Catalog.C09B.Capture")),
			1};
		Input.EncounterKind = EBattleEncounterKind::Wild;
		Input.Format = Scenario.Format;
		Input.CaptureCapacity = {Scenario.PartyCapacity, Scenario.StorageCapacity};
		Input.CaptureProgression = Scenario.Progression;
		Input.Policies.bBagAllowed = true;
		Input.Policies.bCaptureAllowed = true;
		Input.Policies.bRunAllowed = false;
		Input.Policies.WildFleeMode = EBattleWildFleeMode::Disabled;

		Input.Trainers.Add(MakeTrainer(
			PlayerTrainerValue,
			EBattleSide::Player,
			EBattleTrainerRole::Player,
			EBattleDecisionController::Human,
			Scenario.PokeBallCount));
		Input.Trainers.Add(MakeTrainer(
			OpponentTrainerValue,
			EBattleSide::Opponent,
			EBattleTrainerRole::Opponent,
			EBattleDecisionController::EnemyAI,
			-1));

		Input.PartyEntries.Add(MakePartyEntry(
			PlayerTrainerValue,
			PlayerLeftValue,
			0,
			PlayerSpeciesName,
			50,
			200,
			Scenario.PlayerLeftSpeed,
			EBattleCaptureSpeciesClassification::Normal,
			20,
			FItemId(),
			FItemId(),
			true));
		Input.PartyEntries.Add(MakePartyEntry(
			OpponentTrainerValue,
			OpponentLeftValue,
			0,
			TargetSpeciesName,
			Scenario.TargetLevel,
			Scenario.LeftHP,
			Scenario.OpponentLeftSpeed,
			Scenario.LeftClassification,
			17,
			MakeDefinitionId<FItemId>(LeftHeldItemName),
			MakeDefinitionId<FItemId>(LeftHeldItemName)));
		Input.StartingActive.Add(MakeActive(
			EBattleSide::Player,
			EBattlePosition::Left,
			PlayerTrainerValue,
			PlayerLeftValue));
		Input.StartingActive.Add(MakeActive(
			EBattleSide::Opponent,
			EBattlePosition::Left,
			OpponentTrainerValue,
			OpponentLeftValue));

		if (Scenario.Format == EBattleFormat::Double)
		{
			Input.PartyEntries.Add(MakePartyEntry(
				PlayerTrainerValue,
				PlayerRightValue,
				1,
				PlayerSpeciesName,
				50,
				200,
				300,
				EBattleCaptureSpeciesClassification::Normal,
				20));
			Input.PartyEntries.Add(MakePartyEntry(
				OpponentTrainerValue,
				OpponentRightValue,
				1,
				TargetSpeciesName,
				Scenario.TargetLevel,
				Scenario.RightHP,
				100,
				Scenario.RightClassification,
				17,
				MakeDefinitionId<FItemId>(RightHeldItemName),
				FItemId()));
			Input.StartingActive.Add(MakeActive(
				EBattleSide::Player,
				EBattlePosition::Right,
				PlayerTrainerValue,
				PlayerRightValue));
			Input.StartingActive.Add(MakeActive(
				EBattleSide::Opponent,
				EBattlePosition::Right,
				OpponentTrainerValue,
				OpponentRightValue));

			if (Scenario.bConfiguredReinforcement)
			{
				Input.PartyEntries.Add(MakePartyEntry(
					OpponentTrainerValue,
					ReinforcementValue,
					2,
					ReinforcementSpeciesName,
					50,
					200,
					50,
					EBattleCaptureSpeciesClassification::UltraBeast,
					17));
				Input.ConfiguredReinforcementBattlerId =
					MakeNumericId<FBattlerId>(ReinforcementValue);
			}
		}
		return Input;
	}

	bool TryMakeSetup(
		const FCaptureScenario& Scenario,
		FBattleSetup& OutSetup,
		EBattleSetupValidationError& OutError)
	{
		return FBattleSetup::TryCreate(MakeSetupInput(Scenario), OutSetup, OutError);
	}

	bool TryMakeEngine(
		const FCaptureScenario& Scenario,
		TArray<uint32> RandomResults,
		TUniquePtr<FBattleEngine>& OutEngine)
	{
		FBattleSetup Setup;
		EBattleSetupValidationError SetupError = EBattleSetupValidationError::None;
		if (!TryMakeSetup(Scenario, Setup, SetupError))
		{
			return false;
		}
		TUniquePtr<IBattleRandom> Random =
			MakeUnique<FSequenceBattleRandom>(MoveTemp(RandomResults));
		FBattleRejection Rejection;
		return FBattleEngine::TryCreate(
			Setup,
			MakeCatalog(Scenario.CatchRate),
			MoveTemp(Random),
			OutEngine,
			Rejection);
	}

	const FBattleMoveTargetOption* FindMoveTarget(
		const FBattleDecisionRequest& Request,
		const FMoveId MoveId,
		const FActiveSlotId DesiredTarget)
	{
		const FBattleMoveTargetOption* Exact = Request.GetLegalMoveTargets().FindByPredicate(
			[MoveId, DesiredTarget](const FBattleMoveTargetOption& Option)
			{
				return Option.MoveId == MoveId
					&& (!DesiredTarget.IsValid()
						|| Option.ActiveSlotId == DesiredTarget);
			});
		if (Exact != nullptr)
		{
			return Exact;
		}
		return Request.GetLegalMoveTargets().FindByPredicate(
			[MoveId](const FBattleMoveTargetOption& Option)
			{
				return Option.MoveId == MoveId;
			});
	}

	FBattleDecision MakeFightDecision(
		const FBattleDecisionRequest& Request,
		const FMoveId MoveId,
		const FActiveSlotId DesiredTarget)
	{
		const FBattleMoveTargetOption* Target = FindMoveTarget(
			Request,
			MoveId,
			DesiredTarget);
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

	FBattleDecision MakeBagDecision(
		const FBattleDecisionRequest& Request,
		const FActiveSlotId Target)
	{
		const FItemId PokeBallId = FBattleBagItemRules::GetPokeBallId();
		check(Request.GetLegalItemActiveTargets().ContainsByPredicate(
			[PokeBallId, Target](const FBattleItemActiveTargetOption& Option)
			{
				return Option.ItemId == PokeBallId
					&& Option.ActiveSlotId == Target;
			}));
		FBattleDecision Decision;
		const bool bCreated = FBattleDecision::TryCreateBag(
			Request.GetStateVersion(),
			Request.GetDecisionOwnerTrainerId(),
			Request.GetActingBattlerId(),
			PokeBallId,
			FPartySlotId(),
			Target,
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
		const bool bCreated = FBattleDecisionBatch::TryCreate(
			Spec,
			Batch,
			Rejection);
		check(bCreated);
		return Batch;
	}

	bool LockCaptureTurn(
		FBattleEngine& Engine,
		const FActiveSlotId CaptureTarget,
		const bool bOtherPlayerTargetsCapture = true)
	{
		if (Engine.GetSnapshot().GetPhase() == EBattlePhase::Setup)
		{
			FBattleRejection Rejection;
			if (!Engine.TryBeginActionDecisionSequence(Rejection))
			{
				return false;
			}
		}

		int32 Guard = 0;
		while (!Engine.GetPendingDecisionRequests().IsEmpty() && Guard++ < 4)
		{
			const TArray<FBattleDecisionRequest> Requests =
				Engine.GetPendingDecisionRequests();
			TArray<FBattleDecision> Decisions;
			for (const FBattleDecisionRequest& Request : Requests)
			{
				if (Request.GetDecisionOwnerTrainerId()
						== MakeNumericId<FTrainerId>(PlayerTrainerValue)
					&& Request.GetActingBattlerId()
						== MakeNumericId<FBattlerId>(PlayerLeftValue))
				{
					Decisions.Add(MakeBagDecision(Request, CaptureTarget));
					continue;
				}
				const FActiveSlotId DesiredTarget =
					Request.GetDecisionOwnerTrainerId()
						== MakeNumericId<FTrainerId>(PlayerTrainerValue)
						? (bOtherPlayerTargetsCapture
							? CaptureTarget
							: MakeActiveSlotId(
								EBattleSide::Opponent,
								EBattlePosition::Right))
						: MakeActiveSlotId(
							EBattleSide::Player,
							EBattlePosition::Left);
				Decisions.Add(MakeFightDecision(
					Request,
					MakeDefinitionId<FMoveId>(ProbeMoveName),
					DesiredTarget));
			}
			if (!Engine.SubmitDecisionBatch(
				MakeBatch(Requests, MoveTemp(Decisions))).WasAccepted())
			{
				return false;
			}
		}
		return Guard < 4
			&& Engine.GetSnapshot().GetPhase() == EBattlePhase::Locked;
	}

	bool LockTaggedFightTurn(FBattleEngine& Engine)
	{
		if (Engine.GetSnapshot().GetPhase() == EBattlePhase::Setup)
		{
			FBattleRejection Rejection;
			if (!Engine.TryBeginActionDecisionSequence(Rejection))
			{
				return false;
			}
		}

		int32 Guard = 0;
		while (!Engine.GetPendingDecisionRequests().IsEmpty() && Guard++ < 4)
		{
			const TArray<FBattleDecisionRequest> Requests =
				Engine.GetPendingDecisionRequests();
			TArray<FBattleDecision> Decisions;
			for (const FBattleDecisionRequest& Request : Requests)
			{
				const bool bPlayer = Request.GetDecisionOwnerTrainerId()
					== MakeNumericId<FTrainerId>(PlayerTrainerValue);
				const bool bTag = bPlayer
					&& Request.GetActingBattlerId()
						== MakeNumericId<FBattlerId>(PlayerLeftValue);
				const FActiveSlotId DesiredTarget = bPlayer
					? MakeActiveSlotId(
						EBattleSide::Opponent,
						bTag ? EBattlePosition::Left : EBattlePosition::Right)
					: MakeActiveSlotId(
						EBattleSide::Player,
						EBattlePosition::Left);
				Decisions.Add(MakeFightDecision(
					Request,
					MakeDefinitionId<FMoveId>(bTag ? TagMoveName : ProbeMoveName),
					DesiredTarget));
			}
			if (!Engine.SubmitDecisionBatch(
				MakeBatch(Requests, MoveTemp(Decisions))).WasAccepted())
			{
				return false;
			}
		}
		return Guard < 4
			&& Engine.GetSnapshot().GetPhase() == EBattlePhase::Locked;
	}

	bool ExecuteQueueToBoundary(FBattleEngine& Engine)
	{
		int32 Guard = 0;
		while ((Engine.GetSnapshot().GetPhase() == EBattlePhase::Locked
				|| Engine.GetSnapshot().GetPhase() == EBattlePhase::Resolving)
			&& Guard++ < 16)
		{
			if (!Engine.BeginNextLockedAction().WasAccepted())
			{
				return false;
			}
			const TOptional<FBattleLockedAction> Current =
				Engine.GetCurrentLockedAction();
			if (!Current.IsSet())
			{
				continue;
			}
			if (Current->Decision.GetActionKind() == EBattleActionKind::Bag)
			{
				if (!Engine.ExecuteCurrentBagItem().WasAccepted())
				{
					return false;
				}
				continue;
			}
			if (Current->Decision.GetActionKind() != EBattleActionKind::Fight
				|| !Engine.CommitCurrentMoveAfterPreMoveGates().WasAccepted())
			{
				return false;
			}
			if (!Engine.GetCurrentLockedAction().IsSet())
			{
				continue;
			}
			if (!Engine.ResolveCurrentMoveTargets().WasAccepted())
			{
				return false;
			}
			if (!Engine.GetCurrentLockedAction().IsSet())
			{
				continue;
			}
			if (!Engine.ExecuteCurrentMoveEffects().WasAccepted())
			{
				return false;
			}
		}
		const EBattlePhase Phase = Engine.GetSnapshot().GetPhase();
		return Guard < 16
			&& (Phase == EBattlePhase::EndOfTurn
				|| Phase == EBattlePhase::Terminal);
	}

	bool AdvanceFromEndTurn(FBattleEngine& Engine)
	{
		return Engine.GetSnapshot().GetPhase() == EBattlePhase::EndOfTurn
			&& Engine.ResolveEndTurn().WasAccepted()
			&& Engine.GetSnapshot().GetPhase() == EBattlePhase::Selecting;
	}

	bool RunOrderedCaptureScenario(FBattleEngine& Engine)
	{
		const FActiveSlotId OpponentLeft = MakeActiveSlotId(
			EBattleSide::Opponent,
			EBattlePosition::Left);
		const FActiveSlotId OpponentRight = MakeActiveSlotId(
			EBattleSide::Opponent,
			EBattlePosition::Right);
		return LockTaggedFightTurn(Engine)
			&& ExecuteQueueToBoundary(Engine)
			&& AdvanceFromEndTurn(Engine)
			&& LockCaptureTurn(Engine, OpponentLeft)
			&& ExecuteQueueToBoundary(Engine)
			&& AdvanceFromEndTurn(Engine)
			&& LockCaptureTurn(Engine, OpponentRight)
			&& ExecuteQueueToBoundary(Engine)
			&& Engine.GetSnapshot().GetPhase() == EBattlePhase::Terminal;
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

	bool HasEvent(
		const FBattleResolution& Resolution,
		const EBattleEventType Type)
	{
		return FindEventIndex(Resolution, Type) != INDEX_NONE;
	}

	bool HasUnavailableItem(
		const FBattleDecisionRequest& Request,
		const EBattleOptionUnavailableReason Reason)
	{
		const FItemId PokeBallId = FBattleBagItemRules::GetPokeBallId();
		return Request.GetUnavailableOptions().ContainsByPredicate(
			[PokeBallId, Reason](const FBattleUnavailableDecisionOption& Option)
			{
				return Option.Kind == EBattleDecisionOptionKind::Item
					&& Option.ItemId == PokeBallId
					&& Option.Reason == Reason;
			});
	}

	int32 GetBagCount(const FBattleEngine& Engine)
	{
		const FBattleSnapshot Snapshot = Engine.GetSnapshot();
		const FBattleTrainerSetup* Trainer = Snapshot.GetTrainers().FindByPredicate(
			[](const FBattleTrainerSetup& Candidate)
			{
				return Candidate.TrainerId
					== MakeNumericId<FTrainerId>(PlayerTrainerValue);
			});
		check(Trainer != nullptr);
		const FItemId PokeBallId = FBattleBagItemRules::GetPokeBallId();
		const FBattleBagItemCount* Item = Trainer->Bag.FindByPredicate(
			[PokeBallId](const FBattleBagItemCount& Candidate)
			{
				return Candidate.ItemId == PokeBallId;
			});
		check(Item != nullptr);
		return Item->Count;
	}

	int32 GetMovePP(
		const FBattleEngine& Engine,
		const uint64 BattlerValue)
	{
		const FBattleSnapshot Snapshot = Engine.GetSnapshot();
		const FBattlePartyEntrySetup* Battler = Snapshot.FindBattler(
			MakeNumericId<FBattlerId>(BattlerValue));
		if (Battler == nullptr)
		{
			return INDEX_NONE;
		}
		const FBattleMoveSlotSetup* Move = Battler->Moves.FindByPredicate(
			[](const FBattleMoveSlotSetup& Candidate)
			{
				return Candidate.SlotIndex == 0;
			});
		return Move != nullptr ? Move->CurrentPP : INDEX_NONE;
	}

	TArray<FBattleEvent> CollectEvents(
		const FBattleReplayRecord& Record,
		const EBattleEventType Type)
	{
		TArray<FBattleEvent> Events;
		for (const FBattleResolution& Resolution : Record.GetResolutions())
		{
			for (const FBattleEvent& Event : Resolution.GetEvents())
			{
				if (Event.GetType() == Type)
				{
					Events.Add(Event);
				}
			}
		}
		return Events;
	}

	FBattleCaptureCalculationInput MakeCalculationInput()
	{
		FBattleCaptureCalculationInput Input;
		Input.BallItemId = FBattleBagItemRules::GetPokeBallId();
		Input.BallMultiplierQ12 = 4096;
		Input.SpeciesClassification = EBattleCaptureSpeciesClassification::Normal;
		Input.SpeciesCatchRate = 45;
		Input.CurrentHP = 100;
		Input.MaximumHP = 200;
		Input.TargetLevel = 50;
		Input.PlayerLevel = 50;
		Input.Progression.bHasSnapshot = true;
		Input.Progression.CaptureCoefficientQ12 = 4096;
		Input.RandomContext.BattleId = MakeNumericId<FBattleId>(9091);
		Input.RandomContext.TurnId = MakeNumericId<FTurnId>(1);
		Input.RandomContext.ActionId = MakeNumericId<FActionId>(1);
		Input.RandomContext.ResolutionId = MakeNumericId<FResolutionId>(1);
		Input.RandomContext.RulePurpose = MakeDefinitionId<FDefinitionId>(
			TEXT("Rule.C09B.Capture.Test"));
		return Input;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC09BCaptureCalculationTest,
	"PokemonSolarus.Battle.C09B.Capture.Calculation.NormalCriticalGuaranteedMustCaptureGoldenVectors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC09BCaptureCalculationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FBattleCaptureCalculationInput Normal = MakeCalculationInput();
	Normal.MajorStatus = EBattleMajorStatusKind::Paralysis;
	Normal.Progression.BadgeCount = 2;
	Normal.Progression.CaughtSpeciesCount = 200;
	Normal.Progression.bUseCaughtCountHPComponentModifier = true;
	FSequenceBattleRandom NormalRandom({39055, 39055, 39055, 39055});
	FBattleCaptureCalculationResult NormalResult;
	TestTrue(TEXT("The normal golden vector resolves"),
		FBattleCaptureCalculator::TryResolve(Normal, NormalRandom, NormalResult));
	TestTrue(TEXT("The normal golden vector succeeds"), NormalResult.bSucceeded);
	TestEqual(TEXT("Caught-count HP modifier is exact"),
		NormalResult.CaughtCountHPModifierQ12, 2867U);
	TestEqual(TEXT("Badge modifier is exact"), NormalResult.BadgeModifierQ12, 2097U);
	TestEqual(TEXT("Status modifier is exact"), NormalResult.StatusModifierQ12, 6144U);
	TestEqual(TEXT("Normal capture indicator is exact"),
		NormalResult.CaptureIndicatorQ12, 66050ULL);
	TestEqual(TEXT("Single-precision powf shake boundary is exact"),
		NormalResult.ShakeThreshold, 39056U);
	TestEqual(TEXT("Normal success performs four shake checks"),
		NormalResult.ShakeChecksPerformed, static_cast<uint8>(4));

	FSequenceBattleRandom BoundaryFailureRandom({39056, 0});
	FBattleCaptureCalculationResult BoundaryFailure;
	TestTrue(TEXT("The boundary-failure vector resolves"),
		FBattleCaptureCalculator::TryResolve(
			Normal,
			BoundaryFailureRandom,
			BoundaryFailure));
	TestFalse(TEXT("A draw equal to B fails"), BoundaryFailure.bSucceeded);
	TestEqual(TEXT("Failure stops RNG after the first failed shake"),
		BoundaryFailure.ShakeChecksPerformed, static_cast<uint8>(1));
	TestEqual(TEXT("Only one failed shake draw is traced"),
		BoundaryFailureRandom.GetTrace().Num(), 1);

	FBattleCaptureCalculationInput Critical = MakeCalculationInput();
	Critical.SpeciesCatchRate = 120;
	Critical.CurrentHP = 10;
	Critical.MaximumHP = 100;
	Critical.TargetLevel = 40;
	Critical.MajorStatus = EBattleMajorStatusKind::Sleep;
	Critical.Progression.BadgeCount = 8;
	Critical.Progression.CaughtSpeciesCount = 451;
	Critical.Progression.bUseCaughtCountHPComponentModifier = true;
	Critical.Progression.bCriticalCaptureEnabled = true;
	Critical.Progression.bCatchingCharm = true;
	FSequenceBattleRandom CriticalRandom({0, 65391});
	FBattleCaptureCalculationResult CriticalResult;
	TestTrue(TEXT("The critical golden vector resolves"),
		FBattleCaptureCalculator::TryResolve(Critical, CriticalRandom, CriticalResult));
	TestTrue(TEXT("The critical draw succeeds"), CriticalResult.bCriticalCapture);
	TestTrue(TEXT("The critical capture succeeds"), CriticalResult.bSucceeded);
	TestEqual(TEXT("Critical capture indicator is exact"),
		CriticalResult.CaptureIndicatorQ12, 1032080ULL);
	TestEqual(TEXT("Critical modifier including charm is exact"),
		CriticalResult.CriticalModifierQ12, 16384U);
	TestEqual(TEXT("Critical boundary is exact"), CriticalResult.CriticalThreshold, 167U);
	TestEqual(TEXT("Critical shake threshold is exact"), CriticalResult.ShakeThreshold, 65392U);
	TestEqual(TEXT("Critical capture performs one shake"),
		CriticalResult.ShakeChecksPerformed, static_cast<uint8>(1));
	TestEqual(TEXT("Critical capture consumes exactly two draws"),
		CriticalRandom.GetTrace().Num(), 2);

	FBattleCaptureCalculationInput Guaranteed = MakeCalculationInput();
	Guaranteed.SpeciesCatchRate = 255;
	Guaranteed.CurrentHP = 1;
	Guaranteed.MaximumHP = 100;
	Guaranteed.TargetLevel = 5;
	Guaranteed.MajorStatus = EBattleMajorStatusKind::Sleep;
	Guaranteed.Progression.BadgeCount = 8;
	Guaranteed.Progression.CaughtSpeciesCount = 31;
	Guaranteed.Progression.bUseCaughtCountHPComponentModifier = true;
	Guaranteed.Progression.bCriticalCaptureEnabled = true;
	FSequenceBattleRandom GuaranteedRandom({255});
	FBattleCaptureCalculationResult GuaranteedResult;
	TestTrue(TEXT("The guaranteed golden vector resolves"),
		FBattleCaptureCalculator::TryResolve(
			Guaranteed,
			GuaranteedRandom,
			GuaranteedResult));
	TestTrue(TEXT("The indicator guarantees capture"), GuaranteedResult.bGuaranteedCapture);
	TestTrue(TEXT("Guaranteed capture succeeds"), GuaranteedResult.bSucceeded);
	TestEqual(TEXT("Guaranteed indicator is exact"),
		GuaranteedResult.CaptureIndicatorQ12, 3371925ULL);
	TestEqual(TEXT("Guaranteed capture performs no shake draws"),
		GuaranteedResult.ShakeChecksPerformed, static_cast<uint8>(0));
	TestEqual(TEXT("Guaranteed critical eligibility consumes only its one draw"),
		GuaranteedRandom.GetTrace().Num(), 1);

	FBattleCaptureCalculationInput MustCapture = MakeCalculationInput();
	MustCapture.Progression.bMustCapture = true;
	FSequenceBattleRandom MustRandom({});
	FBattleCaptureCalculationResult MustResult;
	TestTrue(TEXT("The must-capture vector resolves"),
		FBattleCaptureCalculator::TryResolve(MustCapture, MustRandom, MustResult));
	TestTrue(TEXT("Must-capture succeeds"), MustResult.bSucceeded && MustResult.bMustCapture);
	TestEqual(TEXT("Must-capture consumes no RNG"), MustRandom.GetTrace().Num(), 0);
	TestEqual(TEXT("Must-capture exposes the completed visual state"),
		MustResult.VisualShakeCount, static_cast<uint8>(3));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC09BCaptureValidationTest,
	"PokemonSolarus.Battle.C09B.Capture.Validation.CapacityPendingFailureConsumption",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC09BCaptureValidationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FCaptureScenario Scenario = MakeScenario();
	FBattleSetupInput MissingProgressionInput = MakeSetupInput(Scenario);
	MissingProgressionInput.CaptureProgression = FBattleCaptureProgressionSnapshot();
	FBattleSetup Setup;
	EBattleSetupValidationError SetupError = EBattleSetupValidationError::None;
	TestFalse(TEXT("Capture-enabled setup rejects absent progression"),
		FBattleSetup::TryCreate(MissingProgressionInput, Setup, SetupError));
	TestEqual(TEXT("Absent progression has a dedicated error"),
		SetupError, EBattleSetupValidationError::InvalidCaptureProgression);

	FBattleSetupInput InvalidClassificationInput = MakeSetupInput(Scenario);
	InvalidClassificationInput.PartyEntries[1].CaptureClassification =
		EBattleCaptureSpeciesClassification::Invalid;
	SetupError = EBattleSetupValidationError::None;
	TestFalse(TEXT("Unknown capture classification is rejected"),
		FBattleSetup::TryCreate(InvalidClassificationInput, Setup, SetupError));
	TestEqual(TEXT("Unknown classification is an invalid party entry"),
		SetupError, EBattleSetupValidationError::InvalidPartyEntry);

	FCaptureScenario Full = MakeScenario();
	Full.PartyCapacity = 0;
	Full.StorageCapacity = 0;
	TUniquePtr<FBattleEngine> FullEngine;
	TestTrue(TEXT("The zero-capacity engine is created"),
		TryMakeEngine(Full, {}, FullEngine));
	FBattleRejection Rejection;
	TestTrue(TEXT("Zero-capacity selection begins"),
		FullEngine->TryBeginActionDecisionSequence(Rejection));
	const TArray<FBattleDecisionRequest> FullRequests =
		FullEngine->GetPendingDecisionRequests();
	TestFalse(TEXT("Zero capacity exposes at least one player request"), FullRequests.IsEmpty());
	if (!FullRequests.IsEmpty())
	{
		TestTrue(TEXT("Poke Ball is marked CaptureCapacityFull"),
			HasUnavailableItem(
				FullRequests[0],
				EBattleOptionUnavailableReason::CaptureCapacityFull));
		TestFalse(TEXT("Poke Ball has no legal active target at capacity"),
			FullRequests[0].GetLegalItemActiveTargets().ContainsByPredicate(
				[](const FBattleItemActiveTargetOption& Option)
				{
					return Option.ItemId == FBattleBagItemRules::GetPokeBallId();
				}));
	}
	TestEqual(TEXT("Capacity rejection consumes no item"), GetBagCount(*FullEngine), 3);
	TestEqual(TEXT("Capacity rejection consumes no RNG"),
		FullEngine->ExportRandomTrace().Num(), 0);

	FCaptureScenario Classified = MakeScenario();
	Classified.LeftClassification = EBattleCaptureSpeciesClassification::UltraBeast;
	TUniquePtr<FBattleEngine> ClassifiedEngine;
	TestTrue(TEXT("The classified-target engine is created"),
		TryMakeEngine(Classified, {}, ClassifiedEngine));
	TestTrue(TEXT("Classified-target selection begins"),
		ClassifiedEngine->TryBeginActionDecisionSequence(Rejection));
	const TArray<FBattleDecisionRequest> ClassifiedRequests =
		ClassifiedEngine->GetPendingDecisionRequests();
	TestFalse(TEXT("Classified target exposes a player request"), ClassifiedRequests.IsEmpty());
	if (!ClassifiedRequests.IsEmpty())
	{
		TestTrue(TEXT("A normal Poke Ball has no legal Ultra Beast target"),
			HasUnavailableItem(
				ClassifiedRequests[0],
				EBattleOptionUnavailableReason::NoLegalTarget));
	}
	TestEqual(TEXT("Classification rejection consumes no item"),
		GetBagCount(*ClassifiedEngine), 3);
	TestEqual(TEXT("Classification rejection consumes no RNG"),
		ClassifiedEngine->ExportRandomTrace().Num(), 0);

	FCaptureScenario Failure = MakeScenario();
	Failure.CatchRate = 1;
	Failure.TargetLevel = 100;
	Failure.LeftHP = 200;
	Failure.Progression.BadgeCount = 0;
	Failure.Progression.CaughtSpeciesCount = 0;
	Failure.Progression.bMustCapture = false;
	Failure.PokeBallCount = 2;
	TUniquePtr<FBattleEngine> FailureEngine;
	TestTrue(TEXT("The legal-failure engine is created"),
		TryMakeEngine(Failure, {65535}, FailureEngine));
	const FActiveSlotId OpponentLeft = MakeActiveSlotId(
		EBattleSide::Opponent,
		EBattlePosition::Left);
	TestTrue(TEXT("The legal failure turn locks"),
		LockCaptureTurn(*FailureEngine, OpponentLeft));
	TestTrue(TEXT("The legal Poke Ball action starts"),
		FailureEngine->BeginNextLockedAction().WasAccepted());
	const FBattleResolution FailedCapture = FailureEngine->ExecuteCurrentBagItem();
	TestTrue(TEXT("The failed attempt resolves legally"), FailedCapture.WasAccepted());
	TestEqual(TEXT("A legal failure consumes exactly one Poke Ball"),
		GetBagCount(*FailureEngine), 1);
	TestEqual(TEXT("A legal failure consumes exactly one early-stopped draw"),
		FailureEngine->ExportRandomTrace().Num(), 1);
	TestEqual(TEXT("Failure creates no pending capture"),
		FailureEngine->GetSnapshot().GetPendingCaptures().Num(), 0);
	TestTrue(TEXT("Failure leaves the target active"),
		FBattleC09BCaptureEngineFixture::IsActive(
			*FailureEngine,
			MakeNumericId<FBattlerId>(OpponentLeftValue)));
	const int32 AttemptIndex = FindEventIndex(
		FailedCapture,
		EBattleEventType::CaptureAttempted);
	TestTrue(TEXT("Failure publishes CaptureAttempted metadata"), AttemptIndex != INDEX_NONE);
	if (AttemptIndex != INDEX_NONE)
	{
		const TOptional<FBattleCaptureEventMetadata>& Metadata =
			FailedCapture.GetEvents()[AttemptIndex].GetCapture();
		TestTrue(TEXT("Failure metadata is present"), Metadata.IsSet());
		if (Metadata.IsSet())
		{
			TestFalse(TEXT("Failure metadata reports no success"), Metadata->bSucceeded);
			TestEqual(TEXT("Failure indicator is exact"),
				Metadata->CaptureIndicatorQ12, 229ULL);
			TestEqual(TEXT("Failure boundary is exact"), Metadata->ShakeThreshold, 13502U);
			TestEqual(TEXT("Failure metadata records one performed check"),
				Metadata->ShakeChecksPerformed, static_cast<uint8>(1));
		}
	}

	FCaptureScenario PendingFull = MakeScenario();
	PendingFull.Format = EBattleFormat::Double;
	PendingFull.PartyCapacity = 1;
	PendingFull.StorageCapacity = 0;
	PendingFull.PokeBallCount = 2;
	TUniquePtr<FBattleEngine> PendingEngine;
	TestTrue(TEXT("The pending-capacity engine is created"),
		TryMakeEngine(PendingFull, {}, PendingEngine));
	TestTrue(TEXT("The first guaranteed capture turn locks"),
		LockCaptureTurn(*PendingEngine, OpponentLeft));
	TestTrue(TEXT("The first guaranteed capture turn completes"),
		ExecuteQueueToBoundary(*PendingEngine));
	TestTrue(TEXT("The completed turn advances to selection"),
		AdvanceFromEndTurn(*PendingEngine));
	TestEqual(TEXT("One pending capture fills the frozen capacity"),
		PendingEngine->GetSnapshot().GetPendingCaptures().Num(), 1);
	const TArray<FBattleDecisionRequest> PendingRequests =
		PendingEngine->GetPendingDecisionRequests();
	TestFalse(TEXT("The next player request exists"), PendingRequests.IsEmpty());
	if (!PendingRequests.IsEmpty())
	{
		TestTrue(TEXT("Pending captures participate in capacity validation"),
			HasUnavailableItem(
				PendingRequests[0],
				EBattleOptionUnavailableReason::CaptureCapacityFull));
	}
	TestEqual(TEXT("Pending-capacity rejection leaves the remaining ball intact"),
		GetBagCount(*PendingEngine), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC09BCaptureExecutionTest,
	"PokemonSolarus.Battle.C09B.Capture.Execution.SuccessRemovalCancellationAndFinalVictory",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC09BCaptureExecutionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FCaptureScenario Scenario = MakeScenario();
	Scenario.Format = EBattleFormat::Double;
	TUniquePtr<FBattleEngine> Engine;
	TestTrue(TEXT("The cancellation engine is created"),
		TryMakeEngine(Scenario, {}, Engine));
	const FActiveSlotId OpponentLeft = MakeActiveSlotId(
		EBattleSide::Opponent,
		EBattlePosition::Left);
	TestTrue(TEXT("The capture-and-cancellation turn locks"),
		LockCaptureTurn(*Engine, OpponentLeft));
	const TArray<FBattleLockedAction> Queue = Engine->GetLockedActions();
	TestEqual(TEXT("The Double turn locks four actions"), Queue.Num(), 4);
	if (Queue.Num() == 4)
	{
		TestTrue(TEXT("Bag executes before queued moves"),
			Queue[0].Decision.GetActionKind() == EBattleActionKind::Bag);
		TestTrue(TEXT("The exact-target player move follows the Bag"),
			Queue[1].Decision.GetActingBattlerId()
				== MakeNumericId<FBattlerId>(PlayerRightValue));
		TestTrue(TEXT("The target's own queued move follows"),
			Queue[2].Decision.GetActingBattlerId()
				== MakeNumericId<FBattlerId>(OpponentLeftValue));
	}

	const int32 PlayerRightPPBefore = GetMovePP(*Engine, PlayerRightValue);
	const int32 OpponentLeftPPBefore = GetMovePP(*Engine, OpponentLeftValue);
	TestTrue(TEXT("The Bag action starts"), Engine->BeginNextLockedAction().WasAccepted());
	const FBattleResolution Captured = Engine->ExecuteCurrentBagItem();
	TestTrue(TEXT("The successful capture resolves"), Captured.WasAccepted());
	TestTrue(TEXT("Successful capture emits Captured"),
		HasEvent(Captured, EBattleEventType::Captured));
	TestTrue(TEXT("Successful capture emits Removed"),
		HasEvent(Captured, EBattleEventType::Removed));
	const FBattleBattlerState* CapturedState =
		FBattleC09BCaptureEngineFixture::GetBattler(
			*Engine,
			MakeNumericId<FBattlerId>(OpponentLeftValue));
	TestTrue(TEXT("The target is marked captured and removed"),
		CapturedState != nullptr
			&& CapturedState->bCaptured
			&& CapturedState->bRemoved);
	TestFalse(TEXT("The captured target leaves its active slot"),
		FBattleC09BCaptureEngineFixture::IsActive(
			*Engine,
			MakeNumericId<FBattlerId>(OpponentLeftValue)));
	TestTrue(TEXT("The surviving opposing battler remains active"),
		FBattleC09BCaptureEngineFixture::IsActive(
			*Engine,
			MakeNumericId<FBattlerId>(OpponentRightValue)));

	const FBattleResolution ExactTargetCanceled = Engine->BeginNextLockedAction();
	TestTrue(TEXT("The captured exact target cancels the queued move"),
		ExactTargetCanceled.WasAccepted()
			&& HasEvent(ExactTargetCanceled, EBattleEventType::ActionCanceled));
	TestFalse(TEXT("Canceled exact-target move never resolves or redirects"),
		HasEvent(ExactTargetCanceled, EBattleEventType::TargetsResolved));
	TestFalse(TEXT("Canceled exact-target move spends no PP event"),
		HasEvent(ExactTargetCanceled, EBattleEventType::PPConsumed));
	TestEqual(TEXT("Canceled exact-target move retains PP"),
		GetMovePP(*Engine, PlayerRightValue), PlayerRightPPBefore);

	const FBattleResolution CapturedActorCanceled = Engine->BeginNextLockedAction();
	TestTrue(TEXT("The captured actor's queued move cancels"),
		CapturedActorCanceled.WasAccepted()
			&& HasEvent(CapturedActorCanceled, EBattleEventType::ActionCanceled));
	TestFalse(TEXT("Captured actor never resolves a target"),
		HasEvent(CapturedActorCanceled, EBattleEventType::TargetsResolved));
	TestFalse(TEXT("Captured actor spends no PP event"),
		HasEvent(CapturedActorCanceled, EBattleEventType::PPConsumed));
	TestEqual(TEXT("Captured actor retains PP"),
		GetMovePP(*Engine, OpponentLeftValue), OpponentLeftPPBefore);

	FCaptureScenario LastTarget = MakeScenario();
	TUniquePtr<FBattleEngine> TerminalEngine;
	TestTrue(TEXT("The last-target engine is created"),
		TryMakeEngine(LastTarget, {}, TerminalEngine));
	TestTrue(TEXT("The last-target capture turn locks"),
		LockCaptureTurn(*TerminalEngine, OpponentLeft));
	TestTrue(TEXT("The last-target Bag action starts"),
		TerminalEngine->BeginNextLockedAction().WasAccepted());
	const FBattleResolution TerminalCapture = TerminalEngine->ExecuteCurrentBagItem();
	TestTrue(TEXT("The last-target capture resolves"), TerminalCapture.WasAccepted());
	const FBattleSnapshot TerminalSnapshot = TerminalEngine->GetSnapshot();
	TestEqual(TEXT("Last capture ends in Victory"),
		TerminalSnapshot.GetOutcome(), EBattleOutcome::Victory);
	TestEqual(TEXT("Last capture records Capture as the outcome cause"),
		TerminalSnapshot.GetOutcomeCause(), EBattleOutcomeCause::Capture);
	TestEqual(TEXT("Last capture enters Terminal"),
		TerminalSnapshot.GetPhase(), EBattlePhase::Terminal);
	TestEqual(TEXT("Last capture creates one pending destination"),
		TerminalSnapshot.GetPendingCaptures().Num(), 1);
	const int32 CapturedIndex = FindEventIndex(TerminalCapture, EBattleEventType::Captured);
	const int32 RemovedIndex = FindEventIndex(TerminalCapture, EBattleEventType::Removed);
	const int32 CompletedIndex = FindEventIndex(
		TerminalCapture,
		EBattleEventType::ActionCompleted);
	const int32 EndedIndex = FindEventIndex(TerminalCapture, EBattleEventType::BattleEnded);
	TestTrue(TEXT("Terminal capture events retain causal order"),
		CapturedIndex != INDEX_NONE
			&& RemovedIndex > CapturedIndex
			&& CompletedIndex > RemovedIndex
			&& EndedIndex > CompletedIndex);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC09BCaptureResultsReplayTest,
	"PokemonSolarus.Battle.C09B.Capture.Results.OrderedDestinationsRetainedFactsAndReplayEquality",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC09BCaptureResultsReplayTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FCaptureScenario Scenario = MakeScenario();
	Scenario.Format = EBattleFormat::Double;
	Scenario.LeftHP = 123;
	Scenario.RightHP = 77;
	Scenario.PartyCapacity = 1;
	Scenario.StorageCapacity = 1;
	Scenario.bConfiguredReinforcement = true;
	Scenario.PlayerLeftSpeed = 50;
	Scenario.OpponentLeftSpeed = 220;
	Scenario.Progression.BadgeCount = 7;
	Scenario.Progression.CaughtSpeciesCount = 612;
	Scenario.Progression.bCriticalCaptureEnabled = true;
	Scenario.Progression.bCatchingCharm = true;
	Scenario.Progression.bUseCaughtCountHPComponentModifier = true;
	Scenario.Progression.CaptureCoefficientQ12 = 5000;

	TUniquePtr<FBattleEngine> First;
	TUniquePtr<FBattleEngine> Second;
	TestTrue(TEXT("The first ordered-capture engine is created"),
		TryMakeEngine(Scenario, {}, First));
	TestTrue(TEXT("The second ordered-capture engine is created"),
		TryMakeEngine(Scenario, {}, Second));
	TestTrue(TEXT("The first ordered capture scenario completes"),
		RunOrderedCaptureScenario(*First));
	TestTrue(TEXT("The repeated ordered capture scenario completes"),
		RunOrderedCaptureScenario(*Second));

	const FBattleSnapshot Snapshot = First->GetSnapshot();
	TestEqual(TEXT("Two captures are retained in exact success order"),
		Snapshot.GetPendingCaptures().Num(), 2);
	if (Snapshot.GetPendingCaptures().Num() == 2)
	{
		const FBattlePendingCaptureRecord& Left = Snapshot.GetPendingCaptures()[0];
		const FBattlePendingCaptureRecord& Right = Snapshot.GetPendingCaptures()[1];
		TestEqual(TEXT("First capture ordinal is one"), Left.CaptureOrdinal, 1ULL);
		TestEqual(TEXT("First capture fills the party"),
			Left.Destination, EBattlePendingCaptureDestination::Party);
		TestTrue(TEXT("First capture retains exact battler identity"),
			Left.BattlerId == MakeNumericId<FBattlerId>(OpponentLeftValue));
		TestEqual(TEXT("First capture retains HP"), Left.CurrentHP, 123);
		TestTrue(TEXT("First capture retains applied major status"),
			Left.MajorStatusId == FBattleMajorStatusRules::GetParalysisId());
		TestEqual(TEXT("First capture retains one move"), Left.Moves.Num(), 1);
		if (Left.Moves.Num() == 1)
		{
			TestEqual(TEXT("First capture retains decremented PP"),
				Left.Moves[0].CurrentPP, 16);
		}
		TestTrue(TEXT("First capture retains original held item"),
			Left.HeldItem.OriginalItemId
				== MakeDefinitionId<FItemId>(LeftHeldItemName));
		TestTrue(TEXT("First capture retains current held item"),
			Left.HeldItem.CurrentItemId
				== MakeDefinitionId<FItemId>(LeftHeldItemName));
		TestFalse(TEXT("Present held item is not marked consumed"),
			Left.HeldItem.bConsumed);

		TestEqual(TEXT("Second capture ordinal is two"), Right.CaptureOrdinal, 2ULL);
		TestEqual(TEXT("Second capture overflows to storage"),
			Right.Destination, EBattlePendingCaptureDestination::Storage);
		TestTrue(TEXT("Second capture retains exact battler identity"),
			Right.BattlerId == MakeNumericId<FBattlerId>(OpponentRightValue));
		TestEqual(TEXT("Second capture retains HP"), Right.CurrentHP, 77);
		TestEqual(TEXT("Second capture retains one move"), Right.Moves.Num(), 1);
		if (Right.Moves.Num() == 1)
		{
			TestEqual(TEXT("Second capture retains twice-decremented PP"),
				Right.Moves[0].CurrentPP, 15);
		}
		TestTrue(TEXT("Consumed holder retains original item identity"),
			Right.HeldItem.OriginalItemId
				== MakeDefinitionId<FItemId>(RightHeldItemName));
		TestFalse(TEXT("Consumed holder retains no current item"),
			Right.HeldItem.CurrentItemId.IsValid());
		TestTrue(TEXT("Consumed held-item fact is retained"), Right.HeldItem.bConsumed);
	}

	const FBattleReplayRecord FirstRecord = First->ExportReplayRecord();
	const FBattleReplayRecord SecondRecord = Second->ExportReplayRecord();
	TestTrue(TEXT("The capture replay record is valid"), FirstRecord.IsValid());
	TestEqual(TEXT("Capture exports replay schema 5"),
		FirstRecord.GetSchemaVersion(), 5U);
	TestTrue(TEXT("Replay input freezes dedicated capture progression"),
		FirstRecord.GetInputs().Setup.GetCaptureProgression() == Scenario.Progression);
	TestTrue(TEXT("Replay input freezes the configured reinforcement identity"),
		FirstRecord.GetInputs().Setup.GetConfiguredReinforcementBattlerId()
			== MakeNumericId<FBattlerId>(ReinforcementValue));
	const FBattlePartyEntrySetup* FrozenReinforcement =
		FirstRecord.GetInputs().Setup.FindBattler(
			MakeNumericId<FBattlerId>(ReinforcementValue));
	TestTrue(TEXT("Replay input freezes reinforcement capture classification"),
		FrozenReinforcement != nullptr
			&& FrozenReinforcement->CaptureClassification
				== EBattleCaptureSpeciesClassification::UltraBeast);
	TestEqual(TEXT("Snapshot freezes the one-based escape-attempt counter"),
		Snapshot.GetEscapeAttemptCount(), 1U);
	TestFalse(TEXT("Snapshot freezes reinforcement-success false"),
		Snapshot.HasSuccessfulReinforcement());
	TestTrue(TEXT("Core snapshot exposes configured reinforcement identity"),
		Snapshot.GetConfiguredReinforcementBattlerId()
			== MakeNumericId<FBattlerId>(ReinforcementValue));

	const FBattleSnapshot PlayerView = First->GetSnapshotForObserver(
		MakeNumericId<FTrainerId>(PlayerTrainerValue));
	const FBattleSnapshot OpponentView = First->GetSnapshotForObserver(
		MakeNumericId<FTrainerId>(OpponentTrainerValue));
	TestTrue(TEXT("Player projection exposes pending capture state"),
		PlayerView.IsCaptureStateVisible()
			&& PlayerView.GetPendingCaptures().Num() == 2);
	TestFalse(TEXT("Opponent projection hides capture state"),
		OpponentView.IsCaptureStateVisible());
	TestEqual(TEXT("Opponent projection contains no pending captures"),
		OpponentView.GetPendingCaptures().Num(), 0);
	TestFalse(TEXT("Filtered snapshots hide configured reinforcement identity"),
		PlayerView.GetConfiguredReinforcementBattlerId().IsValid());

	const TArray<FBattleEvent> CapturedEvents = CollectEvents(
		FirstRecord,
		EBattleEventType::Captured);
	TestEqual(TEXT("Exactly two public Captured events are exported"),
		CapturedEvents.Num(), 2);
	if (CapturedEvents.Num() == 2)
	{
		TestTrue(TEXT("Captured events carry destination metadata"),
			CapturedEvents[0].GetCapture().IsSet()
				&& CapturedEvents[1].GetCapture().IsSet());
		if (CapturedEvents[0].GetCapture().IsSet()
			&& CapturedEvents[1].GetCapture().IsSet())
		{
			TestEqual(TEXT("First event publishes party destination"),
				CapturedEvents[0].GetCapture()->PendingDestination,
				EBattlePendingCaptureDestination::Party);
			TestEqual(TEXT("Second event publishes storage destination"),
				CapturedEvents[1].GetCapture()->PendingDestination,
				EBattlePendingCaptureDestination::Storage);
			TestEqual(TEXT("Second event publishes ordinal two"),
				CapturedEvents[1].GetCapture()->PendingCaptureOrdinal,
				2ULL);
		}
		TestEqual(TEXT("Captured event metadata is public"),
			CapturedEvents[0].GetVisibility().Level,
			EBattleVisibilityLevel::Public);
	}

	TArray<uint8> FirstBytes;
	TArray<uint8> SecondBytes;
	FBattleRejection FirstRejection;
	FBattleRejection SecondRejection;
	TestTrue(TEXT("The first schema-5 replay serializes canonically"),
		FBattleReplaySerializer::TrySerializeCanonical(
			FirstRecord,
			FirstBytes,
			FirstRejection));
	TestTrue(TEXT("The repeated schema-5 replay serializes canonically"),
		FBattleReplaySerializer::TrySerializeCanonical(
			SecondRecord,
			SecondBytes,
			SecondRejection));
	TestFalse(TEXT("The schema-5 replay is non-empty"), FirstBytes.IsEmpty());
	TestTrue(TEXT("Identical setup, decisions, results, and capture state replay equally"),
		FirstBytes == SecondBytes);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
