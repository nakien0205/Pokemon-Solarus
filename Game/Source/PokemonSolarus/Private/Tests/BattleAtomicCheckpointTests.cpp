#if WITH_DEV_AUTOMATION_TESTS

#include "Battle/BattleAbility.h"
#include "Battle/BattleBagItem.h"
#include "Battle/BattleCapture.h"
#include "Battle/BattleEngine.h"
#include "Battle/BattleFieldSideConditions.h"
#include "Battle/BattleItem.h"
#include "Battle/BattleReplay.h"
#include "Battle/BattleState.h"
#include "Battle/BattleVolatile.h"
#include "Battle/BattleWildFlow.h"
#include "BattleAtomicCheckpointTestHarness.h"
#include "BattleTestFactories.h"
#include "BattleTestRandom.h"
#include "Math/NumericLimits.h"
#include "Misc/AutomationTest.h"

namespace
{
	using BattleTest::FScriptedBattleRandomBase;
	using BattleTest::FSequenceBattleRandom;
	using BattleTest::MakeActiveSlotId;
	using BattleTest::MakeDefinitionId;
	using BattleTest::MakeNumericId;
	using BattleTest::MakePartySlotId;

	constexpr uint64 PlayerTrainerValue = 1;
	constexpr uint64 OpponentTrainerValue = 2;
	constexpr uint64 PlayerLeftValue = 11;
	constexpr uint64 PlayerRightValue = 12;
	constexpr uint64 PlayerReserveValue = 13;
	constexpr uint64 OpponentLeftValue = 21;
	constexpr uint64 OpponentRightValue = 22;

	const TCHAR* PlayerSpeciesName = TEXT("Species.ADR0002.3D1.Player");
	const TCHAR* WildSpeciesName = TEXT("Species.ADR0002.3D1.Wild");
	const TCHAR* ProbeMoveName = TEXT("Move.ADR0002.3D1.Probe");
	const TCHAR* TargetProbeMoveName = TEXT("Move.ADR0002.3E1.TargetProbe");
	const TCHAR* CaptureHeldItemName = TEXT("Item.ADR0002.3D3.Capture.Held");

	struct FAtomicWildScenario
	{
		EBattleFormat Format = EBattleFormat::Single;
		int32 PlayerLeftSpeed = 50;
		int32 PlayerRightSpeed = 73;
		int32 OpponentLeftSpeed = 100;
		int32 OpponentRightSpeed = 4;
		EBattleWildFleeMode WildFleeMode = EBattleWildFleeMode::Disabled;
		uint32 WildFleeNumerator = 0;
		uint32 WildFleeDenominator = 0;
		bool bCaptureFlow = false;
		int32 CatchRate = 45;
		int32 TargetCurrentHP = 200;
		int32 PokeBallCount = 3;
		int32 PartyCaptureCapacity = 1;
		int32 StorageCaptureCapacity = 2;
		FBattleCaptureProgressionSnapshot CaptureProgression;
		bool bPlayerHasCanonicalHeldItem = false;
		bool bPlayerSubjectToObedience = false;
		uint8 PlayerReferenceLevel = 20;
		uint8 PlayerBadgeCount = 0;
		bool bVoluntarySwitchFlow = false;
		int32 SwitchIncomingCurrentHP = 200;
		FAbilityId SwitchIncomingAbilityId;
		FItemId SwitchIncomingHeldItemId;
	};

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
		Move.TargetClass = EBattleTargetClass::Self;
		FBattleMoveEffectDescriptor Effect;
		Effect.Kind = EBattleMoveEffectKind::ModifyStatStage;
		Effect.Target = EBattleEffectTarget::User;
		Effect.Stat = EBattleStat::Attack;
		Effect.MagnitudeNumerator = 1;
		Move.Effects.Add(Effect);
		return Move;
	}

	FBattleMoveDefinition MakeTargetProbeMove()
	{
		FBattleMoveDefinition Move = MakeProbeMove();
		Move.Id = MakeDefinitionId<FMoveId>(TargetProbeMoveName);
		Move.TargetClass = EBattleTargetClass::SelectedOpponent;
		return Move;
	}

	FBattleSpeciesFormDefinition MakeSpecies(
		const TCHAR* Name,
		const int32 CatchRate = 45)
	{
		FBattleSpeciesFormDefinition Species;
		Species.Id = MakeDefinitionId<FSpeciesFormId>(Name);
		Species.PrimaryType = EPokemonType::Normal;
		Species.BaseStats = {80, 80, 80, 80, 80, 80};
		Species.CatchRate = CatchRate;
		Species.AbilityChoices.Add(FBattleAbilityRules::GetBlazeId());
		Species.AbilityChoices.Add(FBattleAbilityRules::GetIntimidateId());
		return Species;
	}

	FBattleDefinitionCatalog MakeCatalog(const FAtomicWildScenario& Scenario)
	{
		FBattleDefinitionCatalogInput Input;
		Input.TypeChartEntries = MakeNeutralTypeChart();
		Input.Moves.Add(MakeProbeMove());
		Input.Moves.Add(MakeTargetProbeMove());
		Input.Abilities.Add({FBattleAbilityRules::GetBlazeId()});
		Input.Abilities.Add({FBattleAbilityRules::GetIntimidateId()});
		Input.Items.Add({FBattleBagItemRules::GetPokeBallId(), EBattleItemKind::Capture});
		Input.Items.Add({FBattleItemRules::GetLeftoversId(), EBattleItemKind::Held});
		Input.Items.Add({FBattleItemRules::GetSitrusBerryId(), EBattleItemKind::Held});
		Input.Items.Add({FBattleItemRules::GetAirBalloonId(), EBattleItemKind::Held});
		Input.Items.Add({
			MakeDefinitionId<FItemId>(CaptureHeldItemName),
			EBattleItemKind::Held});
		Input.Conditions.Add({
			FBattleFieldSideConditionRules::GetMagicRoomId(),
			EBattleConditionKind::Room});
		Input.Conditions.Add({
			FBattleFieldSideConditionRules::GetSpikesId(),
			EBattleConditionKind::Hazard});
		Input.Conditions.Add({
			FBattleFieldSideConditionRules::GetStealthRockId(),
			EBattleConditionKind::Hazard});
		for (const FConditionId& VolatileId : FBattleVolatileRules::GetCanonicalIds())
		{
			Input.Conditions.Add({VolatileId, EBattleConditionKind::Volatile});
		}
		Input.SpeciesForms.Add(MakeSpecies(PlayerSpeciesName));
		Input.SpeciesForms.Add(MakeSpecies(WildSpeciesName, Scenario.CatchRate));

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
		const EBattleDecisionController Controller,
		const int32 PokeBallCount = -1)
	{
		FBattleTrainerSetup Trainer;
		Trainer.TrainerId = MakeNumericId<FTrainerId>(TrainerValue);
		Trainer.Side = Side;
		Trainer.Role = Role;
		Trainer.Controller = Controller;
		Trainer.SelectorProfileId = MakeDefinitionId<FDefinitionId>(
			Role == EBattleTrainerRole::Player
				? TEXT("Selector.ADR0002.3D1.Player")
				: TEXT("Selector.ADR0002.3D1.Opponent"));
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
		const int32 Speed,
		const int32 CurrentHP = 200,
		const FItemId OriginalHeldItemId = FItemId(),
		const FItemId CurrentHeldItemId = FItemId(),
		const FAbilityId AbilityId = FBattleAbilityRules::GetBlazeId())
	{
		FBattlePartyEntrySetup Entry;
		Entry.TrainerId = MakeNumericId<FTrainerId>(TrainerValue);
		Entry.BattlerId = MakeNumericId<FBattlerId>(BattlerValue);
		Entry.SourcePokemonId = MakeNumericId<FSourcePokemonId>(3000 + BattlerValue);
		Entry.PartySlotId = MakePartySlotId(PartyIndex);
		Entry.SpeciesFormId = MakeDefinitionId<FSpeciesFormId>(SpeciesName);
		Entry.Level = 50;
		Entry.Stats = {200, 100, 100, 100, 100, Speed};
		Entry.CurrentHP = CurrentHP;
		Entry.AbilityId = AbilityId;
		Entry.OriginalHeldItemId = OriginalHeldItemId;
		Entry.CurrentHeldItemId = CurrentHeldItemId;
		Entry.CaptureClassification = EBattleCaptureSpeciesClassification::Normal;
		Entry.Moves.Add({0, MakeDefinitionId<FMoveId>(ProbeMoveName), 20, 20});
		Entry.Moves.Add({1, MakeDefinitionId<FMoveId>(TargetProbeMoveName), 20, 20});
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

	FBattleSetupInput MakeSetupInput(const FAtomicWildScenario& Scenario)
	{
		FBattleSetupInput Input;
		Input.BattleId = MakeNumericId<FBattleId>(30031);
		Input.SettingsReference = {
			MakeDefinitionId<FDefinitionId>(TEXT("Settings.ADR0002.3D1")),
			1};
		Input.CatalogReference = {
			MakeDefinitionId<FDefinitionId>(TEXT("Catalog.ADR0002.3D1")),
			1};
		Input.EncounterKind = EBattleEncounterKind::Wild;
		Input.Format = Scenario.Format;
		if (Scenario.bCaptureFlow)
		{
			Input.CaptureCapacity = {
				Scenario.PartyCaptureCapacity,
				Scenario.StorageCaptureCapacity};
			Input.CaptureProgression = Scenario.CaptureProgression;
		}
		Input.Policies.bBagAllowed = Scenario.bCaptureFlow;
		Input.Policies.bCaptureAllowed = Scenario.bCaptureFlow;
		Input.Policies.bRunAllowed = true;
		Input.Policies.bShiftPromptEligible = false;
		Input.Policies.WildFleeMode = Scenario.WildFleeMode;
		Input.Policies.WildFleeNumerator = Scenario.WildFleeNumerator;
		Input.Policies.WildFleeDenominator = Scenario.WildFleeDenominator;

		Input.Trainers.Add(MakeTrainer(
			PlayerTrainerValue,
			EBattleSide::Player,
			EBattleTrainerRole::Player,
			EBattleDecisionController::Human,
			Scenario.bCaptureFlow ? Scenario.PokeBallCount : -1));
		Input.Trainers.Add(MakeTrainer(
			OpponentTrainerValue,
			EBattleSide::Opponent,
			EBattleTrainerRole::Opponent,
			EBattleDecisionController::EnemyAI));

		Input.PartyEntries.Add(MakePartyEntry(
			PlayerTrainerValue,
			PlayerLeftValue,
			0,
			PlayerSpeciesName,
			Scenario.PlayerLeftSpeed,
			200,
			Scenario.bPlayerHasCanonicalHeldItem
				? FBattleItemRules::GetLeftoversId()
				: FItemId(),
			Scenario.bPlayerHasCanonicalHeldItem
				? FBattleItemRules::GetLeftoversId()
				: FItemId()));
		Input.PartyEntries.Add(MakePartyEntry(
			OpponentTrainerValue,
			OpponentLeftValue,
			0,
			WildSpeciesName,
			Scenario.OpponentLeftSpeed,
			Scenario.TargetCurrentHP,
			Scenario.bCaptureFlow
				? MakeDefinitionId<FItemId>(CaptureHeldItemName)
				: FItemId(),
			Scenario.bCaptureFlow
				? MakeDefinitionId<FItemId>(CaptureHeldItemName)
				: FItemId()));
		if (Scenario.bVoluntarySwitchFlow)
		{
			Input.PartyEntries.Add(MakePartyEntry(
				PlayerTrainerValue,
				PlayerReserveValue,
				1,
				PlayerSpeciesName,
				60,
				Scenario.SwitchIncomingCurrentHP,
				Scenario.SwitchIncomingHeldItemId,
				Scenario.SwitchIncomingHeldItemId,
				Scenario.SwitchIncomingAbilityId.IsValid()
					? Scenario.SwitchIncomingAbilityId
					: FBattleAbilityRules::GetBlazeId()));
		}
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
				Scenario.PlayerRightSpeed));
			Input.PartyEntries.Add(MakePartyEntry(
				OpponentTrainerValue,
				OpponentRightValue,
				1,
				WildSpeciesName,
				Scenario.OpponentRightSpeed));
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
		}
		if (Scenario.bPlayerSubjectToObedience)
		{
			Input.ObedienceInputs.Add({
				MakeNumericId<FBattlerId>(PlayerLeftValue),
				true,
				Scenario.PlayerReferenceLevel,
				Scenario.PlayerBadgeCount});
		}
		return Input;
	}

	bool TryMakeEngine(
		const FAtomicWildScenario& Scenario,
		TUniquePtr<IBattleRandom>&& Random,
		TUniquePtr<FBattleEngine>& OutEngine)
	{
		FBattleSetup Setup;
		EBattleSetupValidationError SetupError = EBattleSetupValidationError::None;
		if (!FBattleSetup::TryCreate(MakeSetupInput(Scenario), Setup, SetupError))
		{
			return false;
		}
		FBattleRejection Rejection;
		return FBattleEngine::TryCreate(
			Setup,
			MakeCatalog(Scenario),
			MoveTemp(Random),
			OutEngine,
			Rejection);
	}

	bool TryMakeSequenceEngine(
		const FAtomicWildScenario& Scenario,
		TArray<uint32> Results,
		TUniquePtr<FBattleEngine>& OutEngine)
	{
		TUniquePtr<IBattleRandom> Random =
			MakeUnique<FSequenceBattleRandom>(MoveTemp(Results));
		return TryMakeEngine(Scenario, MoveTemp(Random), OutEngine);
	}

	FAtomicWildScenario MakeAtomicCaptureScenario()
	{
		FAtomicWildScenario Scenario;
		Scenario.bCaptureFlow = true;
		Scenario.CaptureProgression.bHasSnapshot = true;
		Scenario.CaptureProgression.BadgeCount = 8;
		Scenario.CaptureProgression.CaptureCoefficientQ12 = 4096;
		return Scenario;
	}

	FBattleDecision MakeDecision(
		const FBattleDecisionRequest& Request,
		const EBattleActionKind ActionKind,
		const FMoveId FightMoveId = FMoveId())
	{
		FBattleDecision Decision;
		bool bCreated = false;
		if (ActionKind == EBattleActionKind::Fight)
		{
			if (FightMoveId.IsValid())
			{
				const FBattleMoveTargetOption* Target =
					Request.GetLegalMoveTargets().FindByPredicate(
						[FightMoveId](const FBattleMoveTargetOption& Option)
						{
							return Option.MoveId == FightMoveId;
						});
				check(Target != nullptr);
				bCreated = FBattleDecision::TryCreateFight(
					Request.GetStateVersion(),
					Request.GetDecisionOwnerTrainerId(),
					Request.GetActingBattlerId(),
					FightMoveId,
					Target->ActiveSlotId,
					Decision);
			}
			else
			{
				bCreated = FBattleDecision::TryCreateAutomaticallyTargetedFight(
					Request.GetStateVersion(),
					Request.GetDecisionOwnerTrainerId(),
					Request.GetActingBattlerId(),
					MakeDefinitionId<FMoveId>(ProbeMoveName),
					Decision);
			}
		}
		else if (ActionKind == EBattleActionKind::Bag)
		{
			const FItemId PokeBallId = FBattleBagItemRules::GetPokeBallId();
			const FBattleItemActiveTargetOption* Target =
				Request.GetLegalItemActiveTargets().FindByPredicate(
					[PokeBallId](const FBattleItemActiveTargetOption& Option)
					{
						return Option.ItemId == PokeBallId
							&& Option.ActiveSlotId == MakeActiveSlotId(
								EBattleSide::Opponent,
								EBattlePosition::Left);
					});
			check(Target != nullptr);
			bCreated = FBattleDecision::TryCreateBag(
				Request.GetStateVersion(),
				Request.GetDecisionOwnerTrainerId(),
				Request.GetActingBattlerId(),
				PokeBallId,
				FPartySlotId(),
				Target->ActiveSlotId,
				Decision);
		}
		else if (ActionKind == EBattleActionKind::Run
			|| ActionKind == EBattleActionKind::WildFlee)
		{
			bCreated = FBattleDecision::TryCreateSimpleAction(
				Request.GetStateVersion(),
				Request.GetRequestKind(),
				Request.GetDecisionOwnerTrainerId(),
				Request.GetActingBattlerId(),
				ActionKind,
				Decision);
		}
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

	bool LockTurn(
		FBattleEngine& Engine,
		const uint64 SpecialBattlerValue,
		const EBattleActionKind SpecialAction,
		const FMoveId SpecialFightMoveId = FMoveId())
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
			const TArray<FBattleDecisionRequest> Requests = Engine.GetPendingDecisionRequests();
			TArray<FBattleDecision> Decisions;
			for (const FBattleDecisionRequest& Request : Requests)
			{
				const EBattleActionKind Choice = Request.GetActingBattlerId()
					== MakeNumericId<FBattlerId>(SpecialBattlerValue)
					? SpecialAction
					: EBattleActionKind::Fight;
				Decisions.Add(MakeDecision(
					Request,
					Choice,
					Request.GetActingBattlerId()
						== MakeNumericId<FBattlerId>(SpecialBattlerValue)
						? SpecialFightMoveId
						: FMoveId()));
			}
			if (!Engine.SubmitDecisionBatch(
					MakeBatch(Requests, MoveTemp(Decisions))).WasAccepted())
			{
				return false;
			}
		}
		return Guard < 4 && Engine.GetSnapshot().GetPhase() == EBattlePhase::Locked;
	}

	bool LockCaptureThenTargetTurn(FBattleEngine& Engine)
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
				if (Request.GetActingBattlerId()
					== MakeNumericId<FBattlerId>(PlayerLeftValue))
				{
					Decisions.Add(MakeDecision(Request, EBattleActionKind::Bag));
				}
				else if (Request.GetActingBattlerId()
					== MakeNumericId<FBattlerId>(PlayerRightValue))
				{
					Decisions.Add(MakeDecision(
						Request,
						EBattleActionKind::Fight,
						MakeDefinitionId<FMoveId>(TargetProbeMoveName)));
				}
				else
				{
					Decisions.Add(MakeDecision(Request, EBattleActionKind::Fight));
				}
			}
			if (!Engine.SubmitDecisionBatch(
					MakeBatch(Requests, MoveTemp(Decisions))).WasAccepted())
			{
				return false;
			}
		}
		return Guard < 4 && Engine.GetSnapshot().GetPhase() == EBattlePhase::Locked;
	}

	bool BeginExpectedWildAction(
		FBattleEngine& Engine,
		const uint64 BattlerValue,
		const EBattleActionKind ActionKind)
	{
		if (!Engine.BeginNextLockedAction().WasAccepted())
		{
			return false;
		}
		const TOptional<FBattleLockedAction> Current = Engine.GetCurrentLockedAction();
		return Current.IsSet()
			&& Current->Decision.GetActingBattlerId()
				== MakeNumericId<FBattlerId>(BattlerValue)
			&& Current->Decision.GetActionKind() == ActionKind;
	}

	bool ExecuteRemainingQueue(FBattleEngine& Engine)
	{
		int32 Guard = 0;
		while (Engine.GetSnapshot().GetPhase() == EBattlePhase::Resolving
			&& Guard++ < 12)
		{
			if (!Engine.BeginNextLockedAction().WasAccepted())
			{
				return false;
			}
			const TOptional<FBattleLockedAction> Current = Engine.GetCurrentLockedAction();
			if (!Current.IsSet()
				|| Current->Decision.GetActionKind() != EBattleActionKind::Fight
				|| !Engine.CommitCurrentMoveAfterPreMoveGates().WasAccepted())
			{
				return false;
			}
			if (Engine.GetCurrentLockedAction().IsSet()
				&& !Engine.ResolveCurrentMoveTargets().WasAccepted())
			{
				return false;
			}
			if (Engine.GetCurrentLockedAction().IsSet()
				&& !Engine.ExecuteCurrentMoveEffects().WasAccepted())
			{
				return false;
			}
		}
		return Guard < 12 && Engine.GetSnapshot().GetPhase() == EBattlePhase::EndOfTurn;
	}

	bool HasEvent(const FBattleResolution& Resolution, const EBattleEventType Type)
	{
		return Resolution.GetEvents().ContainsByPredicate(
			[Type](const FBattleEvent& Event)
			{
				return Event.GetType() == Type;
			});
	}

	bool HasExactEventOrder(
		const FBattleResolution& Resolution,
		const TArray<EBattleEventType>& Expected)
	{
		if (Resolution.GetEvents().Num() != Expected.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < Expected.Num(); ++Index)
		{
			if (Resolution.GetEvents()[Index].GetType() != Expected[Index])
			{
				return false;
			}
		}
		return true;
	}

	bool AreEventSourcesIdentical(
		const FBattleEventSource& Left,
		const FBattleEventSource& Right)
	{
		return Left.TrainerId == Right.TrainerId
			&& Left.BattlerId == Right.BattlerId
			&& Left.ActiveSlotId == Right.ActiveSlotId
			&& Left.DefinitionId == Right.DefinitionId;
	}

	bool AreEventTargetsIdentical(
		const TConstArrayView<FBattleEventTarget> Left,
		const TConstArrayView<FBattleEventTarget> Right)
	{
		if (Left.Num() != Right.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < Left.Num(); ++Index)
		{
			if (Left[Index].TrainerId != Right[Index].TrainerId
				|| Left[Index].BattlerId != Right[Index].BattlerId
				|| Left[Index].ActiveSlotId != Right[Index].ActiveSlotId
				|| Left[Index].Side != Right[Index].Side
				|| Left[Index].bHasSide != Right[Index].bHasSide
				|| Left[Index].bField != Right[Index].bField)
			{
				return false;
			}
		}
		return true;
	}

	bool AreEventsIdentical(const FBattleEvent& Left, const FBattleEvent& Right)
	{
		if (Left.IsValid() != Right.IsValid()
			|| Left.GetEventOrdinal() != Right.GetEventOrdinal()
			|| Left.GetBattleId() != Right.GetBattleId()
			|| Left.GetTurnId() != Right.GetTurnId()
			|| Left.GetActionId() != Right.GetActionId()
			|| Left.GetResolutionId() != Right.GetResolutionId()
			|| Left.GetType() != Right.GetType()
			|| Left.GetCause() != Right.GetCause()
			|| Left.GetCauseActionKind() != Right.GetCauseActionKind()
			|| Left.GetOutcomeCause() != Right.GetOutcomeCause()
			|| !AreEventSourcesIdentical(Left.GetSource(), Right.GetSource())
			|| !AreEventTargetsIdentical(Left.GetTargets(), Right.GetTargets())
			|| Left.GetNumericBefore() != Right.GetNumericBefore()
			|| Left.GetNumericAfter() != Right.GetNumericAfter()
			|| Left.GetNumericDelta() != Right.GetNumericDelta()
			|| Left.GetSimultaneousGroupId() != Right.GetSimultaneousGroupId()
			|| Left.GetHitIndex() != Right.GetHitIndex()
			|| Left.GetHitCount() != Right.GetHitCount()
			|| Left.GetActionOrder().IsSet() != Right.GetActionOrder().IsSet()
			|| Left.GetTargetResolution().IsSet()
				!= Right.GetTargetResolution().IsSet()
			|| Left.GetCapture().IsSet() != Right.GetCapture().IsSet())
		{
			return false;
		}

		if (Left.GetActionOrder().IsSet())
		{
			const FBattleActionOrderMetadata& L = Left.GetActionOrder().GetValue();
			const FBattleActionOrderMetadata& R = Right.GetActionOrder().GetValue();
			if (L.QueueOrdinal != R.QueueOrdinal
				|| L.OrderKey.CommandBand != R.OrderKey.CommandBand
				|| L.OrderKey.MovePriority != R.OrderKey.MovePriority
				|| L.OrderKey.FractionalPriorityTenths
					!= R.OrderKey.FractionalPriorityTenths
				|| L.OrderKey.EffectiveSpeed != R.OrderKey.EffectiveSpeed
				|| L.OrderKey.ActingSlotId != R.OrderKey.ActingSlotId
				|| L.bReverseSpeed != R.bReverseSpeed)
			{
				return false;
			}
		}
		if (Left.GetTargetResolution().IsSet())
		{
			const FBattleTargetResolutionMetadata& L =
				Left.GetTargetResolution().GetValue();
			const FBattleTargetResolutionMetadata& R =
				Right.GetTargetResolution().GetValue();
			if (L.TargetClass != R.TargetClass
				|| L.bWasRedirected != R.bWasRedirected
				|| L.bUsedFaintedTargetFallback != R.bUsedFaintedTargetFallback)
			{
				return false;
			}
		}
		if (Left.GetCapture().IsSet()
			&& !(Left.GetCapture().GetValue() == Right.GetCapture().GetValue()))
		{
			return false;
		}

		const FBattleEventVisibility& LVisibility = Left.GetVisibility();
		const FBattleEventVisibility& RVisibility = Right.GetVisibility();
		return LVisibility.Level == RVisibility.Level
			&& LVisibility.OwningTrainerId == RVisibility.OwningTrainerId
			&& LVisibility.OwningSide == RVisibility.OwningSide
			&& LVisibility.bHasOwningSide == RVisibility.bHasOwningSide
			&& LVisibility.bRevealSourceDefinition
				== RVisibility.bRevealSourceDefinition;
	}

	bool IsReturnedResolutionAppendedExactlyOnce(
		const FBattleEngine& Engine,
		const FBattleResolution& Returned)
	{
		const FBattleEngineState& State =
			FBattleC09BWildFlowEngineFixture::GetState(Engine);
		if (State.Resolutions.IsEmpty())
		{
			return false;
		}
		const FBattleResolution& Appended = State.Resolutions.Last();
		const FBattleRejection& AppendedRejection = Appended.GetRejection();
		const FBattleRejection& ReturnedRejection = Returned.GetRejection();
		if (Appended.IsValid() != Returned.IsValid()
			|| Appended.GetResolutionId() != Returned.GetResolutionId()
			|| Appended.WasAccepted() != Returned.WasAccepted()
			|| Appended.GetBeforeStateVersion() != Returned.GetBeforeStateVersion()
			|| Appended.GetAfterStateVersion() != Returned.GetAfterStateVersion()
			|| AppendedRejection.Reason != ReturnedRejection.Reason
			|| AppendedRejection.TrainerId != ReturnedRejection.TrainerId
			|| AppendedRejection.BattlerId != ReturnedRejection.BattlerId
			|| AppendedRejection.ActionId != ReturnedRejection.ActionId
			|| AppendedRejection.MoveId != ReturnedRejection.MoveId
			|| AppendedRejection.ItemId != ReturnedRejection.ItemId
			|| AppendedRejection.PartySlotId != ReturnedRejection.PartySlotId
			|| AppendedRejection.ActiveSlotId != ReturnedRejection.ActiveSlotId
			|| Appended.GetEvents().Num() != Returned.GetEvents().Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < Appended.GetEvents().Num(); ++Index)
		{
			if (!AreEventsIdentical(
					Appended.GetEvents()[Index],
					Returned.GetEvents()[Index]))
			{
				return false;
			}
		}

		int32 MatchingResolutionCount = 0;
		for (const FBattleResolution& Candidate : State.Resolutions)
		{
			MatchingResolutionCount += Candidate.GetResolutionId()
				== Returned.GetResolutionId();
		}
		if (MatchingResolutionCount != 1)
		{
			return false;
		}
		for (const FBattleEvent& ReturnedEvent : Returned.GetEvents())
		{
			int32 MatchingEventCount = 0;
			for (const FBattleEvent& Candidate : State.OrderedEvents)
			{
				MatchingEventCount += Candidate.GetEventOrdinal()
					== ReturnedEvent.GetEventOrdinal();
			}
			if (MatchingEventCount != 1)
			{
				return false;
			}
		}
		return true;
	}

	struct FCheckpointObservation
	{
		uint64 StateVersion = 0;
		uint64 NextResolutionId = 0;
		uint64 NextEventOrdinal = 0;
		uint64 NextTriggerToken = 0;
		uint32 EscapeAttemptCount = 0;
		int32 LockedActionIndex = INDEX_NONE;
		int32 ResolutionCount = 0;
		int32 EventCount = 0;
		int32 RandomTraceCount = 0;
		int32 PokeBallCount = INDEX_NONE;
		int32 PendingCaptureCount = 0;
		EBattlePhase Phase = EBattlePhase::Setup;
		EBattleOutcome Outcome = EBattleOutcome::InProgress;
		EBattleOutcomeCause OutcomeCause = EBattleOutcomeCause::None;
		bool bActionStarted = false;
		bool bActionFinished = false;
		bool bBagActionAvailable = false;
		bool bObservedBattlerActive = false;
		bool bObservedBattlerCaptured = false;
		bool bObservedBattlerRemoved = false;
	};

	FCheckpointObservation ObserveCheckpoint(
		const FBattleEngine& Engine,
		const FBattlerId ObservedBattlerId)
	{
		const FBattleEngineState& State =
			FBattleC09BWildFlowEngineFixture::GetState(Engine);
		FCheckpointObservation Observation;
		Observation.StateVersion = State.StateVersion;
		Observation.NextResolutionId = State.NextResolutionId;
		Observation.NextEventOrdinal = State.NextEventOrdinal;
		Observation.NextTriggerToken = State.NextTriggerReentrancyToken;
		Observation.EscapeAttemptCount = State.EscapeAttemptCount;
		Observation.LockedActionIndex = State.CurrentLockedActionIndex;
		Observation.ResolutionCount = State.Resolutions.Num();
		Observation.EventCount = State.OrderedEvents.Num();
		Observation.RandomTraceCount = State.Random->GetTrace().Num();
		Observation.PendingCaptureCount = State.PendingCaptures.Num();
		Observation.Phase = State.Phase;
		Observation.Outcome = State.Outcome;
		Observation.OutcomeCause = State.OutcomeCause;
		if (State.LockedActions.IsValidIndex(State.CurrentLockedActionIndex))
		{
			Observation.bActionStarted =
				State.LockedActions[State.CurrentLockedActionIndex].bStarted;
			Observation.bActionFinished =
				State.LockedActions[State.CurrentLockedActionIndex].bFinished;
		}
		const FBattleTrainerState* PlayerTrainer = State.FindTrainer(
			MakeNumericId<FTrainerId>(PlayerTrainerValue));
		if (PlayerTrainer != nullptr)
		{
			Observation.bBagActionAvailable =
				PlayerTrainer->ActionAllowance.bBagActionAvailable;
			const FItemId PokeBallId = FBattleBagItemRules::GetPokeBallId();
			const FBattleBagItemCount* PokeBall = PlayerTrainer->Bag.FindByPredicate(
				[PokeBallId](const FBattleBagItemCount& Candidate)
				{
					return Candidate.ItemId == PokeBallId;
				});
			Observation.PokeBallCount = PokeBall != nullptr ? PokeBall->Count : INDEX_NONE;
		}
		Observation.bObservedBattlerActive =
			FBattleC09BWildFlowEngineFixture::IsActive(Engine, ObservedBattlerId);
		const FBattleBattlerState* Observed = State.FindBattler(ObservedBattlerId);
		Observation.bObservedBattlerCaptured =
			Observed != nullptr && Observed->bCaptured;
		Observation.bObservedBattlerRemoved = Observed != nullptr && Observed->bRemoved;
		return Observation;
	}

	bool VerifyRejectedCheckpoint(
		FAutomationTestBase& Test,
		const FBattleEngine& Engine,
		const FBattlerId ObservedBattlerId,
		const FCheckpointObservation& Before,
		const uint64 ExpectedStateVersion,
		const EBattleRejectionReason ExpectedReason,
		const FBattleResolution& Returned)
	{
		const FBattleEngineState& State =
			FBattleC09BWildFlowEngineFixture::GetState(Engine);
		bool bValid = true;
		bValid &= Test.TestFalse(TEXT("Checkpoint failure is rejected"), Returned.WasAccepted());
		bValid &= Test.TestEqual(
			TEXT("Checkpoint failure has the expected typed reason"),
			Returned.GetRejection().Reason,
			ExpectedReason);
		bValid &= Test.TestTrue(
			TEXT("Returned rejection is the exact appended resolution"),
			IsReturnedResolutionAppendedExactlyOnce(Engine, Returned));
		bValid &= Test.TestEqual(
			TEXT("Exactly one rejection resolution is appended"),
			State.Resolutions.Num(),
			Before.ResolutionCount + 1);
		bValid &= Test.TestEqual(
			TEXT("Exactly one rejection event is appended"),
			State.OrderedEvents.Num(),
			Before.EventCount + 1);
		bValid &= Test.TestEqual(
			TEXT("The rejection consumes one invocation resolution identity"),
			State.NextResolutionId,
			Before.NextResolutionId + 1);
		bValid &= Test.TestEqual(
			TEXT("The rejection consumes one event ordinal"),
			State.NextEventOrdinal,
			Before.NextEventOrdinal + 1);
		bValid &= Test.TestEqual(
			TEXT("Rejection does not advance checkpoint state version"),
			State.StateVersion,
			ExpectedStateVersion);
		bValid &= Test.TestEqual(
			TEXT("Rejection does not advance the action cursor"),
			State.CurrentLockedActionIndex,
			Before.LockedActionIndex);
		bValid &= Test.TestEqual(
			TEXT("Rejection does not change the escape counter"),
			State.EscapeAttemptCount,
			Before.EscapeAttemptCount);
		bValid &= Test.TestEqual(TEXT("Rejection preserves phase"), State.Phase, Before.Phase);
		bValid &= Test.TestEqual(TEXT("Rejection preserves outcome"), State.Outcome, Before.Outcome);
		bValid &= Test.TestEqual(
			TEXT("Rejection preserves outcome cause"),
			State.OutcomeCause,
			Before.OutcomeCause);
		bValid &= Test.TestEqual(
			TEXT("Rejection leaves the parent RNG trace unchanged"),
			State.Random->GetTrace().Num(),
			Before.RandomTraceCount);
		const FBattleTrainerState* PlayerTrainer = State.FindTrainer(
			MakeNumericId<FTrainerId>(PlayerTrainerValue));
		const FItemId PokeBallId = FBattleBagItemRules::GetPokeBallId();
		const FBattleBagItemCount* PokeBall = PlayerTrainer != nullptr
			? PlayerTrainer->Bag.FindByPredicate(
				[PokeBallId](const FBattleBagItemCount& Candidate)
				{
					return Candidate.ItemId == PokeBallId;
				})
			: nullptr;
		bValid &= Test.TestEqual(
			TEXT("Rejection preserves the Poke Ball count"),
			PokeBall != nullptr ? PokeBall->Count : INDEX_NONE,
			Before.PokeBallCount);
		bValid &= Test.TestEqual(
			TEXT("Rejection preserves Trainer Bag quota"),
			PlayerTrainer != nullptr
				? PlayerTrainer->ActionAllowance.bBagActionAvailable
				: false,
			Before.bBagActionAvailable);
		bValid &= Test.TestEqual(
			TEXT("Rejection preserves pending captures"),
			State.PendingCaptures.Num(),
			Before.PendingCaptureCount);
		bValid &= Test.TestEqual(
			TEXT("Rejection preserves trigger-token state"),
			State.NextTriggerReentrancyToken,
			Before.NextTriggerToken);
		bValid &= Test.TestEqual(
			TEXT("Observed battler active state is unchanged"),
			FBattleC09BWildFlowEngineFixture::IsActive(Engine, ObservedBattlerId),
			Before.bObservedBattlerActive);
		bValid &= Test.TestEqual(
			TEXT("Observed battler removal state is unchanged"),
			FBattleC09BWildFlowEngineFixture::IsRemoved(Engine, ObservedBattlerId),
			Before.bObservedBattlerRemoved);
		const FBattleBattlerState* Observed = State.FindBattler(ObservedBattlerId);
		bValid &= Test.TestEqual(
			TEXT("Observed battler capture state is unchanged"),
			Observed != nullptr && Observed->bCaptured,
			Before.bObservedBattlerCaptured);
		bValid &= Test.TestTrue(
			TEXT("Started action remains current after checkpoint rejection"),
			State.LockedActions.IsValidIndex(State.CurrentLockedActionIndex)
				&& State.LockedActions[State.CurrentLockedActionIndex].bStarted
				&& !State.LockedActions[State.CurrentLockedActionIndex].bFinished);
		bValid &= Test.TestTrue(
			TEXT("Rejection publishes exactly ActionCanceled"),
			Returned.GetEvents().Num() == 1
				&& Returned.GetEvents()[0].GetType() == EBattleEventType::ActionCanceled
				&& Returned.GetEvents()[0].GetActionId().IsValid());
		bValid &= Test.TestFalse(
			TEXT("Rejection publishes no RunAttempted fact"),
			HasEvent(Returned, EBattleEventType::RunAttempted));
		bValid &= Test.TestFalse(
			TEXT("Rejection publishes no ActionCompleted fact"),
			HasEvent(Returned, EBattleEventType::ActionCompleted));

		const FBattleReplayRecord Replay = Engine.ExportReplayRecord();
		bValid &= Test.TestEqual(TEXT("Replay schema remains 6"), Replay.GetSchemaVersion(), 6U);
		bValid &= Test.TestTrue(
			TEXT("Replay contains the same rejected resolution"),
			!Replay.GetResolutions().IsEmpty()
				&& Replay.GetResolutions().Last().GetResolutionId()
					== Returned.GetResolutionId()
				&& Replay.GetResolutions().Last().GetRejection().Reason == ExpectedReason);
		return bValid;
	}

	enum class EFaultRandomMode : uint8
	{
		CreateTransaction,
		Draw,
		StaleAfterDraw,
		Commit
	};

	struct FFaultRandomCounters
	{
		int32 TransactionCreateAttempts = 0;
		int32 DrawAttempts = 0;
		int32 SuccessfulDraws = 0;
		int32 CommitAttempts = 0;
	};

	class FFaultBattleRandomTransaction final : public IBattleRandomTransaction
	{
	public:
		FFaultBattleRandomTransaction(
			TUniquePtr<IBattleRandomTransaction>&& InInner,
			const EFaultRandomMode InMode,
			const int32 InSuccessfulDrawsBeforeFailure,
			TFunction<void()>* InAfterDraw,
			FFaultRandomCounters* InCounters)
			: Inner(MoveTemp(InInner))
			, Mode(InMode)
			, SuccessfulDrawsBeforeFailure(InSuccessfulDrawsBeforeFailure)
			, AfterDraw(InAfterDraw)
			, Counters(InCounters)
		{
		}

		virtual bool TryDrawUniform(
			const uint32 InclusiveMinimum,
			const uint32 InclusiveMaximum,
			const FBattleRandomContext& Context,
			FBattleRandomDraw& OutDraw) override
		{
			OutDraw = FBattleRandomDraw();
			if (Counters != nullptr)
			{
				++Counters->DrawAttempts;
			}
			if (bFinalized
				|| (Mode == EFaultRandomMode::Draw
					&& SuccessfulDrawCount >= SuccessfulDrawsBeforeFailure))
			{
				return false;
			}
			if (!Inner->TryDrawUniform(InclusiveMinimum, InclusiveMaximum, Context, OutDraw))
			{
				return false;
			}
			++SuccessfulDrawCount;
			if (Counters != nullptr)
			{
				++Counters->SuccessfulDraws;
			}
			if (Mode == EFaultRandomMode::StaleAfterDraw
				&& !bAfterDrawCalled
				&& AfterDraw != nullptr
				&& static_cast<bool>(*AfterDraw))
			{
				bAfterDrawCalled = true;
				(*AfterDraw)();
			}
			return true;
		}

		virtual TConstArrayView<FBattleRandomDraw> GetTrace() const override
		{
			return Inner->GetTrace();
		}

		virtual bool TryCommit(
			IBattleRandom& Parent,
			const FResolutionId ResolutionId,
			const FActionId OwningActionId,
			EBattleRandomTransactionCommitError& OutError) override
		{
			OutError = EBattleRandomTransactionCommitError::None;
			if (Counters != nullptr)
			{
				++Counters->CommitAttempts;
			}
			if (bFinalized)
			{
				OutError = EBattleRandomTransactionCommitError::AlreadyFinalized;
				return false;
			}
			bFinalized = true;
			if (Mode == EFaultRandomMode::Commit)
			{
				Inner->Rollback();
				OutError = EBattleRandomTransactionCommitError::ParentPositionMismatch;
				return false;
			}
			return Inner->TryCommit(Parent, ResolutionId, OwningActionId, OutError);
		}

		virtual void Rollback() override
		{
			bFinalized = true;
			Inner->Rollback();
		}

	private:
		TUniquePtr<IBattleRandomTransaction> Inner;
		EFaultRandomMode Mode;
		int32 SuccessfulDrawsBeforeFailure = 0;
		TFunction<void()>* AfterDraw = nullptr;
		FFaultRandomCounters* Counters = nullptr;
		int32 SuccessfulDrawCount = 0;
		bool bAfterDrawCalled = false;
		bool bFinalized = false;
	};

	class FFaultBattleRandom final : public FScriptedBattleRandomBase
	{
	public:
		FFaultBattleRandom(
			TArray<uint32> Results,
			const EFaultRandomMode InMode,
			const int32 InSuccessfulDrawsBeforeFailure = 0)
			: FScriptedBattleRandomBase(MoveTemp(Results))
			, Mode(InMode)
			, SuccessfulDrawsBeforeFailure(InSuccessfulDrawsBeforeFailure)
		{
		}

		void SetAfterDraw(TFunction<void()>&& InAfterDraw)
		{
			AfterDraw = MoveTemp(InAfterDraw);
		}

		const FFaultRandomCounters& GetCounters() const
		{
			return Counters;
		}

		virtual bool TryCreateTransaction(
			const FResolutionId ResolutionId,
			const FActionId OwningActionId,
			TUniquePtr<IBattleRandomTransaction>& OutTransaction) override
		{
			OutTransaction.Reset();
			++Counters.TransactionCreateAttempts;
			if (Mode == EFaultRandomMode::CreateTransaction)
			{
				return false;
			}

			TUniquePtr<IBattleRandomTransaction> Inner;
			if (!FScriptedBattleRandomBase::TryCreateTransaction(
					ResolutionId,
					OwningActionId,
					Inner))
			{
				return false;
			}
			OutTransaction = MakeUnique<FFaultBattleRandomTransaction>(
				MoveTemp(Inner),
				Mode,
				SuccessfulDrawsBeforeFailure,
				&AfterDraw,
				&Counters);
			return true;
		}

	private:
		EFaultRandomMode Mode;
		int32 SuccessfulDrawsBeforeFailure = 0;
		TFunction<void()> AfterDraw;
		FFaultRandomCounters Counters;
	};

	bool TryMakeFaultEngine(
		const FAtomicWildScenario& Scenario,
		TArray<uint32> Results,
		const EFaultRandomMode Mode,
		TUniquePtr<FBattleEngine>& OutEngine,
		FFaultBattleRandom*& OutRandom,
		const int32 SuccessfulDrawsBeforeFailure = 0)
	{
		TUniquePtr<FFaultBattleRandom> Fault =
			MakeUnique<FFaultBattleRandom>(
				MoveTemp(Results),
				Mode,
				SuccessfulDrawsBeforeFailure);
		OutRandom = Fault.Get();
		TUniquePtr<IBattleRandom> Random = MoveTemp(Fault);
		return TryMakeEngine(Scenario, MoveTemp(Random), OutEngine);
	}

	bool TrySeedActionStartVolatile(
		FBattleEngine& Engine,
		const FBattlerId BattlerId,
		const FConditionId VolatileId,
		const FDefinitionId PayloadId = FDefinitionId())
	{
		FBattleEngineState& State =
			FBattleC09BWildFlowEngineFixture::GetMutableState(Engine);
		FBattleBattlerState* Battler = State.FindMutableBattler(BattlerId);
		if (Battler == nullptr
			|| Battler->Volatiles.ContainsByPredicate(
				[VolatileId](const FBattleConditionState& Condition)
				{
					return Condition.ConditionId == VolatileId;
				}))
		{
			return false;
		}

		FBattleTriggerSubject Owner;
		if (!FBattleTriggerSubject::TryCreateBattler(BattlerId, Owner))
		{
			return false;
		}
		FBattleVolatileTriggerRegistrationFacts Facts;
		Facts.VolatileId = VolatileId;
		Facts.PayloadId = PayloadId.IsValid()
			? PayloadId
			: VolatileId.GetDefinitionId();
		Facts.Owner = Owner;
		Facts.Source = Owner;
		Facts.Targets.Add(Owner);
		Facts.Layers = 1;
		EBattleTriggerError TriggerError = EBattleTriggerError::None;
		if (!FBattleVolatileRules::TryRegisterTriggers(
				State.TriggerFramework,
				Facts,
				TriggerError))
		{
			return false;
		}

		FBattleConditionState Condition;
		Condition.ConditionId = VolatileId;
		Condition.LayerCount = 1;
		Condition.CreationOrdinal = State.NextConditionCreationOrdinal++;
		Condition.SourceBattlerId = BattlerId;
		Battler->Volatiles.Add(MoveTemp(Condition));
		TArray<FBattleTriggerLifecycleFact> IgnoredFacts;
		State.TriggerFramework.DrainLifecycleFacts(IgnoredFacts);
		return true;
	}

	bool TrySeedActionStartMagicRoom(
		FBattleEngine& Engine,
		const FBattlerId SourceBattlerId)
	{
		FBattleEngineState& State =
			FBattleC09BWildFlowEngineFixture::GetMutableState(Engine);
		const FConditionId MagicRoomId =
			FBattleFieldSideConditionRules::GetMagicRoomId();
		if (State.Field.Rooms.ContainsByPredicate(
			[MagicRoomId](const FBattleConditionState& Condition)
			{
				return Condition.ConditionId == MagicRoomId;
			}))
		{
			return false;
		}

		FBattleTriggerSubject Source;
		if (!FBattleTriggerSubject::TryCreateBattler(SourceBattlerId, Source))
		{
			return false;
		}
		FBattleFieldSideTriggerRegistrationFacts Facts;
		Facts.ConditionId = MagicRoomId;
		Facts.PayloadId = MagicRoomId.GetDefinitionId();
		Facts.Owner = FBattleTriggerSubject::CreateField();
		Facts.Source = Source;
		Facts.Targets.Add(Facts.Owner);
		Facts.RemainingTurns = 5;
		Facts.Layers = 1;
		EBattleTriggerError TriggerError = EBattleTriggerError::None;
		if (!FBattleFieldSideConditionRules::TryRegisterTriggers(
				State.TriggerFramework,
				Facts,
				TriggerError))
		{
			return false;
		}

		FBattleConditionState MagicRoom;
		MagicRoom.ConditionId = MagicRoomId;
		MagicRoom.RemainingTurns = Facts.RemainingTurns;
		MagicRoom.LayerCount = Facts.Layers;
		MagicRoom.CreationOrdinal = State.NextConditionCreationOrdinal++;
		MagicRoom.SourceBattlerId = SourceBattlerId;
		State.Field.Rooms.Add(MoveTemp(MagicRoom));
		TArray<FBattleTriggerLifecycleFact> IgnoredFacts;
		State.TriggerFramework.DrainLifecycleFacts(IgnoredFacts);
		return true;
	}

	bool HasActionStartVolatile(
		const FBattleEngineState& State,
		const FBattlerId BattlerId,
		const FConditionId VolatileId)
	{
		const FBattleBattlerState* Battler = State.FindBattler(BattlerId);
		return Battler != nullptr
			&& Battler->Volatiles.ContainsByPredicate(
				[VolatileId](const FBattleConditionState& Condition)
				{
					return Condition.ConditionId == VolatileId;
				});
	}

	int32 CountActionStartTriggerRegistrations(
		const FBattleEngineState& State,
		const FDefinitionId DefinitionId)
	{
		int32 Count = 0;
		for (const FBattleTriggerRegistrationState& Registration :
			State.TriggerFramework.GetActiveRegistrations())
		{
			const FBattleTriggerSourceDefinition& Source =
				Registration.Spec.SourceDefinition;
			bool bMatches = false;
			switch (Source.Kind)
			{
			case EBattleTriggerSourceDefinitionKind::Condition:
				bMatches = Source.ConditionId.GetDefinitionId() == DefinitionId;
				break;
			case EBattleTriggerSourceDefinitionKind::Ability:
				bMatches = Source.AbilityId.GetDefinitionId() == DefinitionId;
				break;
			case EBattleTriggerSourceDefinitionKind::Item:
				bMatches = Source.ItemId.GetDefinitionId() == DefinitionId;
				break;
			default:
				break;
			}
			Count += bMatches ? 1 : 0;
		}
		return Count;
	}

	bool TryPrepareLastLockedAction(
		FBattleEngine& Engine,
		const FBattlerId ExpectedActorId)
	{
		FBattleEngineState& State =
			FBattleC09BWildFlowEngineFixture::GetMutableState(Engine);
		if (State.LockedActions.Num() < 2)
		{
			return false;
		}
		const int32 LastIndex = State.LockedActions.Num() - 1;
		if (State.LockedActions[LastIndex].Decision.GetActingBattlerId()
			!= ExpectedActorId)
		{
			return false;
		}
		for (int32 Index = 0; Index < LastIndex; ++Index)
		{
			State.LockedActions[Index].bStarted = true;
			State.LockedActions[Index].bFinished = true;
		}
		State.CurrentLockedActionIndex = LastIndex;
		State.Phase = EBattlePhase::Resolving;
		EBattleStateValidationError StateError = EBattleStateValidationError::None;
		return State.ValidateInvariants(StateError);
	}

	bool TryClearActionStartActiveSlot(
		FBattleEngine& Engine,
		const FActiveSlotId ActiveSlotId)
	{
		FBattleEngineState& State =
			FBattleC09BWildFlowEngineFixture::GetMutableState(Engine);
		FBattleActivePositionState* Active = State.FindMutableActivePosition(ActiveSlotId);
		if (Active == nullptr)
		{
			return false;
		}
		Active->TrainerId = FTrainerId();
		Active->BattlerId = FBattlerId();
		return true;
	}

	class FActionStartStaleRandom final : public FScriptedBattleRandomBase
	{
	public:
		explicit FActionStartStaleRandom(TArray<uint32> Results)
			: FScriptedBattleRandomBase(MoveTemp(Results))
		{
		}

		void ArmAfterTraceRead(
			const int32 TraceReadOrdinal,
			TFunction<void()>&& InCallback)
		{
			ReadsSinceArm = 0;
			InjectionReadOrdinal = TraceReadOrdinal;
			Callback = MoveTemp(InCallback);
			bInjected = false;
		}

		void Disarm()
		{
			InjectionReadOrdinal = INDEX_NONE;
			Callback = TFunction<void()>();
		}

		int32 GetReadsSinceArm() const { return ReadsSinceArm; }
		bool WasInjected() const { return bInjected; }

		virtual TConstArrayView<FBattleRandomDraw> GetTrace() const override
		{
			const TConstArrayView<FBattleRandomDraw> Trace =
				FScriptedBattleRandomBase::GetTrace();
			if (InjectionReadOrdinal != INDEX_NONE)
			{
				++ReadsSinceArm;
				if (!bInjected
					&& ReadsSinceArm == InjectionReadOrdinal
					&& static_cast<bool>(Callback))
				{
					bInjected = true;
					Callback();
				}
			}
			return Trace;
		}

	private:
		mutable int32 ReadsSinceArm = 0;
		mutable int32 InjectionReadOrdinal = INDEX_NONE;
		mutable TFunction<void()> Callback;
		mutable bool bInjected = false;
	};

	bool TryMakeActionStartStaleEngine(
		const FAtomicWildScenario& Scenario,
		TUniquePtr<FBattleEngine>& OutEngine,
		FActionStartStaleRandom*& OutRandom)
	{
		TUniquePtr<FActionStartStaleRandom> Random =
			MakeUnique<FActionStartStaleRandom>(TArray<uint32>());
		OutRandom = Random.Get();
		TUniquePtr<IBattleRandom> Base = MoveTemp(Random);
		return TryMakeEngine(Scenario, MoveTemp(Base), OutEngine);
	}

	struct FActionStartCheckpointObservation
	{
		uint64 StateVersion = 0;
		uint64 NextResolutionId = 0;
		uint64 NextEventOrdinal = 0;
		uint64 NextTriggerToken = 0;
		uint64 NextConditionCreationOrdinal = 0;
		int32 ActionIndex = INDEX_NONE;
		int32 ResolutionCount = 0;
		int32 EventCount = 0;
		int32 RandomTraceCount = 0;
		int32 TotalMovePP = 0;
		int32 MaximumActions = INDEX_NONE;
		int32 RemainingActions = INDEX_NONE;
		bool bBagActionAvailable = false;
		EBattlePhase Phase = EBattlePhase::Setup;
		EBattleOutcome Outcome = EBattleOutcome::InProgress;
		EBattleOutcomeCause OutcomeCause = EBattleOutcomeCause::None;
		bool bActionStarted = false;
		bool bMoveCommitted = false;
		bool bTargetResolutionSet = false;
		EBattleLockedEffectExecutionState EffectExecutionState =
			EBattleLockedEffectExecutionState::Pending;
		bool bActionFinished = false;
		bool bPendingDecisionSet = false;
		int32 PendingDecisionRequestCount = 0;
		int32 PendingReplacementCount = 0;
		int32 RoomCount = 0;
		bool bActorActive = false;
		bool bHasHeldItem = false;
		FBattleHeldItemState HeldItem;
		TArray<FBattleHeldItemInstanceState> LedgerStates;
		TArray<FConditionId> ActorVolatileIds;
		TArray<FBattleTriggerRegistrationId> TriggerRegistrationIds;
		TArray<uint64> TriggerCreationOrdinals;
		TArray<FBattleTriggerSourceDefinition> TriggerSources;
		TArray<uint8> TriggerSuppression;
		int32 PendingTriggerDispatchCount = 0;
		int32 PendingTriggerEffectCount = 0;
		int32 PendingTriggerLifecycleCount = 0;
	};

	FActionStartCheckpointObservation ObserveActionStartCheckpoint(
		const FBattleEngine& Engine,
		const FBattlerId ActorId,
		const FTrainerId TrainerId)
	{
		const FBattleEngineState& State =
			FBattleC09BWildFlowEngineFixture::GetState(Engine);
		FActionStartCheckpointObservation Observation;
		Observation.StateVersion = State.StateVersion;
		Observation.NextResolutionId = State.NextResolutionId;
		Observation.NextEventOrdinal = State.NextEventOrdinal;
		Observation.NextTriggerToken = State.NextTriggerReentrancyToken;
		Observation.NextConditionCreationOrdinal = State.NextConditionCreationOrdinal;
		Observation.ActionIndex = State.CurrentLockedActionIndex;
		Observation.ResolutionCount = State.Resolutions.Num();
		Observation.EventCount = State.OrderedEvents.Num();
		Observation.RandomTraceCount = State.Random->GetTrace().Num();
		Observation.Phase = State.Phase;
		Observation.Outcome = State.Outcome;
		Observation.OutcomeCause = State.OutcomeCause;
		Observation.bPendingDecisionSet = State.PendingDecision.IsSet();
		Observation.PendingDecisionRequestCount = State.PendingDecisionRequests.Num();
		Observation.PendingReplacementCount = State.PendingReplacements.Num();
		Observation.RoomCount = State.Field.Rooms.Num();
		if (State.LockedActions.IsValidIndex(Observation.ActionIndex))
		{
			const FBattleLockedActionState& Action =
				State.LockedActions[Observation.ActionIndex];
			Observation.bActionStarted = Action.bStarted;
			Observation.bMoveCommitted = Action.bMoveCommitted;
			Observation.bTargetResolutionSet = Action.TargetResolution.IsSet();
			Observation.EffectExecutionState = Action.EffectExecutionState;
			Observation.bActionFinished = Action.bFinished;
		}
		const FBattleTrainerState* Trainer = State.FindTrainer(TrainerId);
		if (Trainer != nullptr)
		{
			Observation.MaximumActions = Trainer->ActionAllowance.MaximumActions;
			Observation.RemainingActions = Trainer->ActionAllowance.RemainingActions;
			Observation.bBagActionAvailable =
				Trainer->ActionAllowance.bBagActionAvailable;
		}
		const FBattleBattlerState* Actor = State.FindBattler(ActorId);
		if (Actor != nullptr)
		{
			for (const FBattleMoveSlotState& Move : Actor->Moves)
			{
				Observation.TotalMovePP += Move.CurrentPP;
			}
			Observation.bHasHeldItem = Actor->HeldItem.InstanceId.IsValid();
			Observation.HeldItem = Actor->HeldItem;
			for (const FBattleConditionState& Condition : Actor->Volatiles)
			{
				Observation.ActorVolatileIds.Add(Condition.ConditionId);
			}
		}
		Observation.bActorActive = State.ActivePositions.ContainsByPredicate(
			[ActorId](const FBattleActivePositionState& Active)
			{
				return Active.BattlerId == ActorId;
			});
		Observation.LedgerStates.Append(State.HeldItemLedger.GetStates());
		for (const FBattleTriggerRegistrationState& Registration :
			State.TriggerFramework.GetActiveRegistrations())
		{
			Observation.TriggerRegistrationIds.Add(Registration.RegistrationId);
			Observation.TriggerCreationOrdinals.Add(Registration.CreationOrdinal);
			Observation.TriggerSources.Add(Registration.Spec.SourceDefinition);
			Observation.TriggerSuppression.Add(Registration.bSuppressed ? 1 : 0);
		}
		Observation.PendingTriggerDispatchCount =
			State.TriggerFramework.GetPendingDispatchCount();
		Observation.PendingTriggerEffectCount =
			State.TriggerFramework.GetPendingEffectRequestCount();
		Observation.PendingTriggerLifecycleCount =
			State.TriggerFramework.GetPendingLifecycleFactCount();
		return Observation;
	}

	bool AreActionStartHeldItemsIdentical(
		const FBattleHeldItemState& Left,
		const FBattleHeldItemState& Right)
	{
		return Left.InstanceId == Right.InstanceId
			&& Left.OriginalItemId == Right.OriginalItemId
			&& Left.CurrentItemId == Right.CurrentItemId
			&& Left.bConsumed == Right.bConsumed
			&& Left.bSuppressed == Right.bSuppressed
			&& Left.bRevealed == Right.bRevealed
			&& Left.bTemporarilyRemoved == Right.bTemporarilyRemoved
			&& Left.ChoiceLockedMoveId == Right.ChoiceLockedMoveId;
	}

	bool VerifyRejectedActionStartCheckpoint(
		FAutomationTestBase& Test,
		const FBattleEngine& Engine,
		const FBattlerId ActorId,
		const FTrainerId TrainerId,
		const FActionStartCheckpointObservation& Before,
		const uint64 ExpectedStateVersion,
		const EBattleRejectionReason ExpectedReason,
		const FBattleResolution& Returned)
	{
		const FActionStartCheckpointObservation After =
			ObserveActionStartCheckpoint(Engine, ActorId, TrainerId);
		bool bValid = true;
		bValid &= Test.TestFalse(TEXT("Action-start checkpoint failure is rejected"),
			Returned.WasAccepted());
		bValid &= Test.TestEqual(TEXT("Action-start rejection reason is typed"),
			Returned.GetRejection().Reason, ExpectedReason);
		bValid &= Test.TestTrue(TEXT("Action-start rejection is appended exactly once"),
			IsReturnedResolutionAppendedExactlyOnce(Engine, Returned));
		bValid &= Test.TestTrue(TEXT("Action-start rejection publishes one cancellation only"),
			Returned.GetEvents().Num() == 1
				&& Returned.GetEvents()[0].GetType()
					== EBattleEventType::ActionCanceled
				&& Returned.GetEvents()[0].GetCause() == EBattleEventCause::Rule
				&& Returned.GetEvents()[0].GetEventOrdinal()
					== Before.NextEventOrdinal
				&& Returned.GetEvents()[0].GetVisibility().Level
					== EBattleVisibilityLevel::Public);
		bValid &= Test.TestEqual(TEXT("Rejection appends one resolution"),
			After.ResolutionCount, Before.ResolutionCount + 1);
		bValid &= Test.TestEqual(TEXT("Rejection appends one event"),
			After.EventCount, Before.EventCount + 1);
		bValid &= Test.TestEqual(TEXT("Rejection consumes one resolution identity"),
			After.NextResolutionId, Before.NextResolutionId + 1);
		bValid &= Test.TestEqual(TEXT("Rejection consumes one event ordinal"),
			After.NextEventOrdinal, Before.NextEventOrdinal + 1);
		bValid &= Test.TestEqual(TEXT("Rejection publishes no accepted version delta"),
			After.StateVersion, ExpectedStateVersion);
		bValid &= Test.TestEqual(TEXT("Rejection preserves the action cursor"),
			After.ActionIndex, Before.ActionIndex);
		bValid &= Test.TestEqual(TEXT("Rejection preserves phase"),
			After.Phase, Before.Phase);
		bValid &= Test.TestEqual(TEXT("Rejection preserves outcome"),
			After.Outcome, Before.Outcome);
		bValid &= Test.TestEqual(TEXT("Rejection preserves outcome cause"),
			After.OutcomeCause, Before.OutcomeCause);
		bValid &= Test.TestEqual(TEXT("Rejection preserves Trainer maximum allowance"),
			After.MaximumActions, Before.MaximumActions);
		bValid &= Test.TestEqual(TEXT("Rejection preserves Trainer remaining allowance"),
			After.RemainingActions, Before.RemainingActions);
		bValid &= Test.TestEqual(TEXT("Rejection preserves Trainer Bag allowance"),
			After.bBagActionAvailable, Before.bBagActionAvailable);
		bValid &= Test.TestEqual(TEXT("Rejection preserves bStarted"),
			After.bActionStarted, Before.bActionStarted);
		bValid &= Test.TestEqual(TEXT("Rejection preserves bMoveCommitted"),
			After.bMoveCommitted, Before.bMoveCommitted);
		bValid &= Test.TestEqual(TEXT("Rejection preserves target-resolution state"),
			After.bTargetResolutionSet, Before.bTargetResolutionSet);
		bValid &= Test.TestEqual(TEXT("Rejection preserves effect-execution state"),
			After.EffectExecutionState, Before.EffectExecutionState);
		bValid &= Test.TestEqual(TEXT("Rejection preserves bFinished"),
			After.bActionFinished, Before.bActionFinished);
		bValid &= Test.TestEqual(TEXT("Rejection preserves move PP"),
			After.TotalMovePP, Before.TotalMovePP);
		bValid &= Test.TestEqual(TEXT("Rejection consumes no gameplay RNG"),
			After.RandomTraceCount, Before.RandomTraceCount);
		bValid &= Test.TestEqual(TEXT("Rejection preserves trigger token"),
			After.NextTriggerToken, Before.NextTriggerToken);
		bValid &= Test.TestEqual(TEXT("Rejection preserves condition ordinal"),
			After.NextConditionCreationOrdinal, Before.NextConditionCreationOrdinal);
		bValid &= Test.TestEqual(TEXT("Rejection preserves room state"),
			After.RoomCount, Before.RoomCount);
		bValid &= Test.TestEqual(TEXT("Rejection preserves actor active state"),
			After.bActorActive, Before.bActorActive);
		bValid &= Test.TestEqual(TEXT("Rejection preserves pending-decision presence"),
			After.bPendingDecisionSet, Before.bPendingDecisionSet);
		bValid &= Test.TestEqual(TEXT("Rejection preserves pending decision requests"),
			After.PendingDecisionRequestCount, Before.PendingDecisionRequestCount);
		bValid &= Test.TestEqual(TEXT("Rejection preserves pending replacements"),
			After.PendingReplacementCount, Before.PendingReplacementCount);
		bValid &= Test.TestEqual(TEXT("Rejection preserves held-item presence"),
			After.bHasHeldItem, Before.bHasHeldItem);
		bValid &= Test.TestTrue(TEXT("Rejection preserves held-item battler facts"),
			AreActionStartHeldItemsIdentical(After.HeldItem, Before.HeldItem));
		bValid &= Test.TestTrue(TEXT("Rejection preserves the held-item ledger"),
			After.LedgerStates == Before.LedgerStates);
		bValid &= Test.TestTrue(TEXT("Rejection preserves actor volatiles"),
			After.ActorVolatileIds == Before.ActorVolatileIds);
		bValid &= Test.TestTrue(TEXT("Rejection preserves trigger registration ids"),
			After.TriggerRegistrationIds == Before.TriggerRegistrationIds);
		bValid &= Test.TestTrue(TEXT("Rejection preserves trigger creation order"),
			After.TriggerCreationOrdinals == Before.TriggerCreationOrdinals);
		bValid &= Test.TestTrue(TEXT("Rejection preserves trigger sources"),
			After.TriggerSources == Before.TriggerSources);
		bValid &= Test.TestTrue(TEXT("Rejection preserves trigger suppression"),
			After.TriggerSuppression == Before.TriggerSuppression);
		bValid &= Test.TestEqual(TEXT("Rejection preserves pending trigger dispatches"),
			After.PendingTriggerDispatchCount, Before.PendingTriggerDispatchCount);
		bValid &= Test.TestEqual(TEXT("Rejection preserves pending trigger effects"),
			After.PendingTriggerEffectCount, Before.PendingTriggerEffectCount);
		bValid &= Test.TestEqual(TEXT("Rejection preserves pending trigger lifecycle facts"),
			After.PendingTriggerLifecycleCount, Before.PendingTriggerLifecycleCount);
		const FBattleReplayRecord Replay = Engine.ExportReplayRecord();
		bValid &= Test.TestEqual(TEXT("Rejection replay keeps the canonical schema"),
			Replay.GetSchemaVersion(), FBattleReplayRecord::CurrentSchemaVersion);
		bValid &= Test.TestTrue(TEXT("Rejection replay contains the same resolution"),
			!Replay.GetResolutions().IsEmpty()
				&& Replay.GetResolutions().Last().GetResolutionId()
					== Returned.GetResolutionId()
				&& Replay.GetResolutions().Last().GetRejection().Reason
					== ExpectedReason);
		return bValid;
	}

	FAtomicWildScenario MakeAtomicVoluntarySwitchScenario(
		const FItemId IncomingItemId = FItemId(),
		const FAbilityId IncomingAbilityId = FBattleAbilityRules::GetBlazeId(),
		const int32 IncomingCurrentHP = 200)
	{
		FAtomicWildScenario Scenario;
		Scenario.bVoluntarySwitchFlow = true;
		Scenario.SwitchIncomingHeldItemId = IncomingItemId;
		Scenario.SwitchIncomingAbilityId = IncomingAbilityId;
		Scenario.SwitchIncomingCurrentHP = IncomingCurrentHP;
		return Scenario;
	}

	bool TryPrepareAtomicVoluntarySwitch(FBattleEngine& Engine)
	{
		FBattleEngineState& State =
			FBattleC09BWildFlowEngineFixture::GetMutableState(Engine);
		const FBattlerId OutgoingBattlerId =
			MakeNumericId<FBattlerId>(PlayerLeftValue);
		const FBattleBattlerState* Outgoing = State.FindBattler(OutgoingBattlerId);
		const FBattleActivePositionState* Active = State.ActivePositions.FindByPredicate(
			[OutgoingBattlerId](const FBattleActivePositionState& Candidate)
			{
				return Candidate.BattlerId == OutgoingBattlerId;
			});
		FBattleTrainerState* Trainer = Outgoing != nullptr
			? State.FindMutableTrainer(Outgoing->TrainerId)
			: nullptr;
		if (Outgoing == nullptr || Active == nullptr || Trainer == nullptr)
		{
			return false;
		}

		FBattleDecision Decision;
		if (!FBattleDecision::TryCreateSwitch(
				State.StateVersion,
				EBattleDecisionRequestKind::Action,
				Outgoing->TrainerId,
				OutgoingBattlerId,
				MakePartySlotId(1),
				Active->ActiveSlotId,
				Decision))
		{
			return false;
		}
		FBattleLockedActionState Action;
		Action.ActionId = MakeNumericId<FActionId>(3003201);
		Action.QueueOrdinal = 1;
		Action.Decision = MoveTemp(Decision);
		Action.OrderKey.CommandBand = EBattleActionCommandBand::VoluntarySwitch;
		Action.OrderKey.EffectiveSpeed = Outgoing->PermanentStats.Speed;
		Action.OrderKey.ActingSlotId = Active->ActiveSlotId;
		State.LockedActions = {MoveTemp(Action)};
		State.CurrentLockedActionIndex = 0;
		State.PendingDecision.Reset();
		State.PendingDecisionRequests.Reset();
		State.PendingReplacements.Reset();
		State.DecisionOwnerSequence.Reset();
		State.AcceptedSelections.Reset();
		State.Phase = EBattlePhase::Locked;
		Trainer->ActionAllowance.MaximumActions = 1;
		Trainer->ActionAllowance.RemainingActions = 1;
		EBattleStateValidationError Validation = EBattleStateValidationError::None;
		return State.ValidateInvariants(Validation);
	}

	bool TryBeginAtomicVoluntarySwitch(FBattleEngine& Engine)
	{
		if (!Engine.BeginNextLockedAction().WasAccepted())
		{
			return false;
		}
		const TOptional<FBattleLockedAction> Current = Engine.GetCurrentLockedAction();
		return Current.IsSet()
			&& Current->Decision.GetActionKind() == EBattleActionKind::Switch
			&& Current->Decision.GetActingBattlerId()
				== MakeNumericId<FBattlerId>(PlayerLeftValue)
			&& Current->Decision.GetSwitchPartySlotId() == MakePartySlotId(1);
	}

	bool TrySeedAtomicSwitchHazard(
		FBattleEngine& Engine,
		const FConditionId HazardId,
		const int32 Layers = 1)
	{
		FBattleEngineState& State =
			FBattleC09BWildFlowEngineFixture::GetMutableState(Engine);
		FBattleTriggerSubject Owner;
		FBattleTriggerSubject Source;
		const FBattlerId SourceBattlerId =
			MakeNumericId<FBattlerId>(OpponentLeftValue);
		if (Layers <= 0
			|| FBattleFieldSideConditionRules::GetConditionFamily(HazardId)
				!= EBattleConditionKind::Hazard
			|| !FBattleTriggerSubject::TryCreateSide(EBattleSide::Player, Owner)
			|| !FBattleTriggerSubject::TryCreateBattler(SourceBattlerId, Source))
		{
			return false;
		}
		FBattleSideState* Side = State.Sides.FindByPredicate(
			[](const FBattleSideState& Candidate)
			{
				return Candidate.Side == EBattleSide::Player;
			});
		if (Side == nullptr
			|| Side->Hazards.ContainsByPredicate(
				[HazardId](const FBattleConditionState& Candidate)
				{
					return Candidate.ConditionId == HazardId;
				}))
		{
			return false;
		}

		FBattleFieldSideTriggerRegistrationFacts Facts;
		Facts.ConditionId = HazardId;
		Facts.PayloadId = HazardId.GetDefinitionId();
		Facts.Owner = Owner;
		Facts.Source = Source;
		Facts.Targets.Add(Owner);
		Facts.Layers = Layers;
		EBattleTriggerError TriggerError = EBattleTriggerError::None;
		if (!FBattleFieldSideConditionRules::TryRegisterTriggers(
				State.TriggerFramework,
				Facts,
				TriggerError))
		{
			return false;
		}
		FBattleConditionState Condition;
		Condition.ConditionId = HazardId;
		Condition.LayerCount = Layers;
		Condition.CreationOrdinal = State.NextConditionCreationOrdinal++;
		Condition.SourceBattlerId = SourceBattlerId;
		Side->Hazards.Add(MoveTemp(Condition));
		TArray<FBattleTriggerEffectRequest> IgnoredRequests;
		TArray<FBattleTriggerLifecycleFact> IgnoredLifecycle;
		State.TriggerFramework.DrainEffectRequests(IgnoredRequests);
		State.TriggerFramework.DrainLifecycleFacts(IgnoredLifecycle);
		return true;
	}

	bool TrySeedAtomicSwitchOutgoingTransients(FBattleEngine& Engine)
	{
		const FBattlerId OutgoingId = MakeNumericId<FBattlerId>(PlayerLeftValue);
		FBattleEngineState& State =
			FBattleC09BWildFlowEngineFixture::GetMutableState(Engine);
		FBattleBattlerState* Outgoing = State.FindMutableBattler(OutgoingId);
		if (Outgoing == nullptr)
		{
			return false;
		}
		const FBattleStatStageChangeResult Change =
			Outgoing->Stages.ApplyChange(EBattleStat::Attack, 2);
		Outgoing->LastMoveId = MakeDefinitionId<FMoveId>(ProbeMoveName);
		return Change.Outcome == EBattleStatStageChangeOutcome::Applied;
	}

	struct FAtomicSwitchConditionObservation
	{
		FConditionId ConditionId;
		int32 RemainingTurns = 0;
		int32 LayerCount = 0;
		uint64 CreationOrdinal = 0;
		FBattlerId SourceBattlerId;
		bool bHasRemainingTurns = false;
	};

	FAtomicSwitchConditionObservation ObserveAtomicSwitchCondition(
		const FBattleConditionState& Condition)
	{
		FAtomicSwitchConditionObservation Observation;
		Observation.ConditionId = Condition.ConditionId;
		Observation.bHasRemainingTurns = Condition.RemainingTurns.IsSet();
		Observation.RemainingTurns = Condition.RemainingTurns.Get(0);
		Observation.LayerCount = Condition.LayerCount;
		Observation.CreationOrdinal = Condition.CreationOrdinal;
		Observation.SourceBattlerId = Condition.SourceBattlerId;
		return Observation;
	}

	bool AreAtomicSwitchConditionsIdentical(
		const TArray<FAtomicSwitchConditionObservation>& Left,
		const TArray<FAtomicSwitchConditionObservation>& Right)
	{
		if (Left.Num() != Right.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < Left.Num(); ++Index)
		{
			const FAtomicSwitchConditionObservation& L = Left[Index];
			const FAtomicSwitchConditionObservation& R = Right[Index];
			if (L.ConditionId != R.ConditionId
				|| L.bHasRemainingTurns != R.bHasRemainingTurns
				|| L.RemainingTurns != R.RemainingTurns
				|| L.LayerCount != R.LayerCount
				|| L.CreationOrdinal != R.CreationOrdinal
				|| L.SourceBattlerId != R.SourceBattlerId)
			{
				return false;
			}
		}
		return true;
	}

	struct FAtomicSwitchBattlerObservation
	{
		FTrainerId TrainerId;
		FBattlerId BattlerId;
		FPartySlotId PartySlotId;
		int32 CurrentHP = 0;
		TArray<int32> Stages;
		TArray<FAtomicSwitchConditionObservation> Volatiles;
		FConditionId MajorStatusId;
		FAbilityId AbilityId;
		FBattleHeldItemState HeldItem;
		FMoveId LastMoveId;
		FTurnId EnteredActiveOnTurnId;
		bool bFainted = false;
		bool bCaptured = false;
		bool bRemoved = false;
		bool bFaintTransitionPending = false;
		bool bAbilitySuppressed = false;
	};

	FAtomicSwitchBattlerObservation ObserveAtomicSwitchBattler(
		const FBattleEngineState& State,
		const FBattlerId BattlerId)
	{
		FAtomicSwitchBattlerObservation Observation;
		const FBattleBattlerState* Battler = State.FindBattler(BattlerId);
		if (Battler == nullptr)
		{
			return Observation;
		}
		Observation.TrainerId = Battler->TrainerId;
		Observation.BattlerId = Battler->BattlerId;
		Observation.PartySlotId = Battler->PartySlotId;
		Observation.CurrentHP = Battler->CurrentHP;
		for (uint8 StatValue = static_cast<uint8>(EBattleStat::Attack);
			StatValue <= static_cast<uint8>(EBattleStat::Evasion);
			++StatValue)
		{
			int32 Stage = 0;
			const bool bRead = Battler->Stages.TryGetStage(
				static_cast<EBattleStat>(StatValue),
				Stage);
			check(bRead);
			Observation.Stages.Add(Stage);
		}
		for (const FBattleConditionState& Condition : Battler->Volatiles)
		{
			Observation.Volatiles.Add(ObserveAtomicSwitchCondition(Condition));
		}
		Observation.MajorStatusId = Battler->MajorStatusId;
		Observation.AbilityId = Battler->AbilityId;
		Observation.HeldItem = Battler->HeldItem;
		Observation.LastMoveId = Battler->LastMoveId;
		Observation.EnteredActiveOnTurnId = Battler->EnteredActiveOnTurnId;
		Observation.bFainted = Battler->bFainted;
		Observation.bCaptured = Battler->bCaptured;
		Observation.bRemoved = Battler->bRemoved;
		Observation.bFaintTransitionPending = Battler->bFaintTransitionPending;
		Observation.bAbilitySuppressed = Battler->bAbilitySuppressed;
		return Observation;
	}

	bool AreAtomicSwitchBattlersIdentical(
		const FAtomicSwitchBattlerObservation& Left,
		const FAtomicSwitchBattlerObservation& Right)
	{
		return Left.TrainerId == Right.TrainerId
			&& Left.BattlerId == Right.BattlerId
			&& Left.PartySlotId == Right.PartySlotId
			&& Left.CurrentHP == Right.CurrentHP
			&& Left.Stages == Right.Stages
			&& AreAtomicSwitchConditionsIdentical(Left.Volatiles, Right.Volatiles)
			&& Left.MajorStatusId == Right.MajorStatusId
			&& Left.AbilityId == Right.AbilityId
			&& AreActionStartHeldItemsIdentical(Left.HeldItem, Right.HeldItem)
			&& Left.LastMoveId == Right.LastMoveId
			&& Left.EnteredActiveOnTurnId == Right.EnteredActiveOnTurnId
			&& Left.bFainted == Right.bFainted
			&& Left.bCaptured == Right.bCaptured
			&& Left.bRemoved == Right.bRemoved
			&& Left.bFaintTransitionPending == Right.bFaintTransitionPending
			&& Left.bAbilitySuppressed == Right.bAbilitySuppressed;
	}

	bool IsAtomicSwitchDefinitionRevealed(
		const FBattleEngineState& State,
		const FBattlerId BattlerId,
		const bool bAbility)
	{
		const FBattleBattlerState* Battler = State.FindBattler(BattlerId);
		FBattleTriggerSubject Owner;
		FBattleTriggerSourceDefinition Source;
		if (Battler == nullptr
			|| !FBattleTriggerSubject::TryCreateBattler(BattlerId, Owner)
			|| !(bAbility
				? FBattleTriggerSourceDefinition::TryCreateAbility(Battler->AbilityId, Source)
				: FBattleTriggerSourceDefinition::TryCreateItem(
					Battler->HeldItem.CurrentItemId,
					Source)))
		{
			return false;
		}
		return State.AbilityItemRevealTracker.HasBeenRevealed(Source, Owner);
	}

	struct FAtomicSwitchCheckpointObservation
	{
		uint64 StateVersion = 0;
		uint64 NextResolutionId = 0;
		uint64 NextEventOrdinal = 0;
		uint64 NextConditionCreationOrdinal = 0;
		uint64 NextTriggerToken = 0;
		int32 ActionIndex = INDEX_NONE;
		int32 ResolutionCount = 0;
		int32 EventCount = 0;
		int32 RandomTraceCount = 0;
		int32 PendingDecisionRequestCount = 0;
		int32 PendingReplacementCount = 0;
		int32 OpponentRemovalCheckpointCount = 0;
		int32 PendingTriggerDispatchCount = 0;
		int32 PendingTriggerEffectCount = 0;
		int32 PendingTriggerLifecycleCount = 0;
		EBattlePhase Phase = EBattlePhase::Setup;
		EBattleOutcome Outcome = EBattleOutcome::InProgress;
		EBattleOutcomeCause OutcomeCause = EBattleOutcomeCause::None;
		bool bPendingDecisionSet = false;
		bool bActionStarted = false;
		bool bActionFinished = false;
		bool bIncomingAbilityRevealed = false;
		bool bIncomingItemRevealed = false;
		FAtomicSwitchBattlerObservation Outgoing;
		FAtomicSwitchBattlerObservation Incoming;
		FAtomicSwitchBattlerObservation Opponent;
		TArray<FActiveSlotId> ActiveSlotIds;
		TArray<FTrainerId> ActiveTrainerIds;
		TArray<FBattlerId> ActiveBattlerIds;
		TArray<uint8> ActiveAvailability;
		TArray<FAtomicSwitchConditionObservation> PlayerHazards;
		TArray<FBattleHeldItemInstanceState> LedgerStates;
		TArray<FBattleTriggerRegistrationId> TriggerRegistrationIds;
		TArray<uint64> TriggerCreationOrdinals;
		TArray<FBattleTriggerSourceDefinition> TriggerSources;
		TArray<uint8> TriggerSuppression;
	};

	FAtomicSwitchCheckpointObservation ObserveAtomicSwitchCheckpoint(
		const FBattleEngine& Engine)
	{
		const FBattleEngineState& State =
			FBattleC09BWildFlowEngineFixture::GetState(Engine);
		FAtomicSwitchCheckpointObservation Observation;
		Observation.StateVersion = State.StateVersion;
		Observation.NextResolutionId = State.NextResolutionId;
		Observation.NextEventOrdinal = State.NextEventOrdinal;
		Observation.NextConditionCreationOrdinal = State.NextConditionCreationOrdinal;
		Observation.NextTriggerToken = State.NextTriggerReentrancyToken;
		Observation.ActionIndex = State.CurrentLockedActionIndex;
		Observation.ResolutionCount = State.Resolutions.Num();
		Observation.EventCount = State.OrderedEvents.Num();
		Observation.RandomTraceCount = State.Random->GetTrace().Num();
		Observation.PendingDecisionRequestCount = State.PendingDecisionRequests.Num();
		Observation.PendingReplacementCount = State.PendingReplacements.Num();
		Observation.OpponentRemovalCheckpointCount =
			State.AvailableOpponentRemovalCheckpoints.Num();
		Observation.PendingTriggerDispatchCount =
			State.TriggerFramework.GetPendingDispatchCount();
		Observation.PendingTriggerEffectCount =
			State.TriggerFramework.GetPendingEffectRequestCount();
		Observation.PendingTriggerLifecycleCount =
			State.TriggerFramework.GetPendingLifecycleFactCount();
		Observation.Phase = State.Phase;
		Observation.Outcome = State.Outcome;
		Observation.OutcomeCause = State.OutcomeCause;
		Observation.bPendingDecisionSet = State.PendingDecision.IsSet();
		if (State.LockedActions.IsValidIndex(State.CurrentLockedActionIndex))
		{
			const FBattleLockedActionState& Action =
				State.LockedActions[State.CurrentLockedActionIndex];
			Observation.bActionStarted = Action.bStarted;
			Observation.bActionFinished = Action.bFinished;
		}
		const FBattlerId OutgoingId = MakeNumericId<FBattlerId>(PlayerLeftValue);
		const FBattlerId IncomingId = MakeNumericId<FBattlerId>(PlayerReserveValue);
		Observation.Outgoing = ObserveAtomicSwitchBattler(State, OutgoingId);
		Observation.Incoming = ObserveAtomicSwitchBattler(State, IncomingId);
		Observation.Opponent = ObserveAtomicSwitchBattler(
			State,
			MakeNumericId<FBattlerId>(OpponentLeftValue));
		Observation.bIncomingAbilityRevealed =
			IsAtomicSwitchDefinitionRevealed(State, IncomingId, true);
		Observation.bIncomingItemRevealed =
			IsAtomicSwitchDefinitionRevealed(State, IncomingId, false);
		for (const FBattleActivePositionState& Active : State.ActivePositions)
		{
			Observation.ActiveSlotIds.Add(Active.ActiveSlotId);
			Observation.ActiveTrainerIds.Add(Active.TrainerId);
			Observation.ActiveBattlerIds.Add(Active.BattlerId);
			Observation.ActiveAvailability.Add(Active.bAvailable ? 1 : 0);
		}
		const FBattleSideState* PlayerSide = State.Sides.FindByPredicate(
			[](const FBattleSideState& Candidate)
			{
				return Candidate.Side == EBattleSide::Player;
			});
		if (PlayerSide != nullptr)
		{
			for (const FBattleConditionState& Hazard : PlayerSide->Hazards)
			{
				Observation.PlayerHazards.Add(ObserveAtomicSwitchCondition(Hazard));
			}
		}
		Observation.LedgerStates.Append(State.HeldItemLedger.GetStates());
		for (const FBattleTriggerRegistrationState& Registration :
			State.TriggerFramework.GetActiveRegistrations())
		{
			Observation.TriggerRegistrationIds.Add(Registration.RegistrationId);
			Observation.TriggerCreationOrdinals.Add(Registration.CreationOrdinal);
			Observation.TriggerSources.Add(Registration.Spec.SourceDefinition);
			Observation.TriggerSuppression.Add(Registration.bSuppressed ? 1 : 0);
		}
		return Observation;
	}

	bool AreAtomicSwitchMechanicsIdentical(
		const FAtomicSwitchCheckpointObservation& Left,
		const FAtomicSwitchCheckpointObservation& Right)
	{
		return Left.NextConditionCreationOrdinal == Right.NextConditionCreationOrdinal
			&& Left.NextTriggerToken == Right.NextTriggerToken
			&& Left.ActionIndex == Right.ActionIndex
			&& Left.RandomTraceCount == Right.RandomTraceCount
			&& Left.PendingDecisionRequestCount == Right.PendingDecisionRequestCount
			&& Left.PendingReplacementCount == Right.PendingReplacementCount
			&& Left.OpponentRemovalCheckpointCount == Right.OpponentRemovalCheckpointCount
			&& Left.PendingTriggerDispatchCount == Right.PendingTriggerDispatchCount
			&& Left.PendingTriggerEffectCount == Right.PendingTriggerEffectCount
			&& Left.PendingTriggerLifecycleCount == Right.PendingTriggerLifecycleCount
			&& Left.Phase == Right.Phase
			&& Left.Outcome == Right.Outcome
			&& Left.OutcomeCause == Right.OutcomeCause
			&& Left.bPendingDecisionSet == Right.bPendingDecisionSet
			&& Left.bActionStarted == Right.bActionStarted
			&& Left.bActionFinished == Right.bActionFinished
			&& Left.bIncomingAbilityRevealed == Right.bIncomingAbilityRevealed
			&& Left.bIncomingItemRevealed == Right.bIncomingItemRevealed
			&& AreAtomicSwitchBattlersIdentical(Left.Outgoing, Right.Outgoing)
			&& AreAtomicSwitchBattlersIdentical(Left.Incoming, Right.Incoming)
			&& AreAtomicSwitchBattlersIdentical(Left.Opponent, Right.Opponent)
			&& Left.ActiveSlotIds == Right.ActiveSlotIds
			&& Left.ActiveTrainerIds == Right.ActiveTrainerIds
			&& Left.ActiveBattlerIds == Right.ActiveBattlerIds
			&& Left.ActiveAvailability == Right.ActiveAvailability
			&& AreAtomicSwitchConditionsIdentical(Left.PlayerHazards, Right.PlayerHazards)
			&& Left.LedgerStates == Right.LedgerStates
			&& Left.TriggerRegistrationIds == Right.TriggerRegistrationIds
			&& Left.TriggerCreationOrdinals == Right.TriggerCreationOrdinals
			&& Left.TriggerSources == Right.TriggerSources
			&& Left.TriggerSuppression == Right.TriggerSuppression;
	}

	bool VerifyRejectedAtomicVoluntarySwitch(
		FAutomationTestBase& Test,
		const FBattleEngine& Engine,
		const FAtomicSwitchCheckpointObservation& Before,
		const uint64 ExpectedStateVersion,
		const EBattleRejectionReason ExpectedReason,
		const FBattleResolution& Returned)
	{
		const FAtomicSwitchCheckpointObservation After =
			ObserveAtomicSwitchCheckpoint(Engine);
		bool bValid = true;
		bValid &= Test.TestFalse(TEXT("Voluntary Switch checkpoint failure is rejected"),
			Returned.WasAccepted());
		bValid &= Test.TestEqual(TEXT("Voluntary Switch rejection is typed"),
			Returned.GetRejection().Reason,
			ExpectedReason);
		bValid &= Test.TestTrue(TEXT("Rejected Switch is appended exactly once"),
			IsReturnedResolutionAppendedExactlyOnce(Engine, Returned));
		bValid &= Test.TestTrue(TEXT("Rejected Switch publishes cancellation only"),
			Returned.GetEvents().Num() == 1
				&& Returned.GetEvents()[0].GetType() == EBattleEventType::ActionCanceled
				&& Returned.GetEvents()[0].GetCause() == EBattleEventCause::Rule);
		bValid &= Test.TestEqual(TEXT("Rejection appends one resolution"),
			After.ResolutionCount, Before.ResolutionCount + 1);
		bValid &= Test.TestEqual(TEXT("Rejection appends one event"),
			After.EventCount, Before.EventCount + 1);
		bValid &= Test.TestEqual(TEXT("Rejection consumes one resolution id"),
			After.NextResolutionId, Before.NextResolutionId + 1);
		bValid &= Test.TestEqual(TEXT("Rejection consumes one event ordinal"),
			After.NextEventOrdinal, Before.NextEventOrdinal + 1);
		bValid &= Test.TestEqual(TEXT("Rejection has no accepted version delta"),
			After.StateVersion, ExpectedStateVersion);
		bValid &= Test.TestTrue(TEXT("Rejection preserves every staged gameplay domain"),
			AreAtomicSwitchMechanicsIdentical(After, Before));
		const FBattleReplayRecord Replay = Engine.ExportReplayRecord();
		bValid &= Test.TestEqual(TEXT("Rejected Switch replay uses schema 6"),
			Replay.GetSchemaVersion(), FBattleReplayRecord::CurrentSchemaVersion);
		bValid &= Test.TestTrue(TEXT("Rejected Switch replay contains the same fact"),
			!Replay.GetResolutions().IsEmpty()
				&& Replay.GetResolutions().Last().GetResolutionId()
					== Returned.GetResolutionId()
				&& Replay.GetResolutions().Last().GetRejection().Reason
					== Returned.GetRejection().Reason);
		return bValid;
	}

	enum class EAtomicSwitchFailureFamily : uint8
	{
		EntryItemReveal,
		EntryHazard,
		ImmediateHeldItem,
		EntryAbility
	};

	bool RunAtomicVoluntarySwitchFailureFamily(
		FAutomationTestBase& Test,
		const EAtomicSwitchFailureFamily Family)
	{
		FItemId IncomingItemId;
		FAbilityId IncomingAbilityId = FBattleAbilityRules::GetBlazeId();
		if (Family == EAtomicSwitchFailureFamily::EntryItemReveal)
		{
			IncomingItemId = FBattleItemRules::GetAirBalloonId();
		}
		else if (Family == EAtomicSwitchFailureFamily::ImmediateHeldItem)
		{
			IncomingItemId = FBattleItemRules::GetSitrusBerryId();
		}
		else if (Family == EAtomicSwitchFailureFamily::EntryAbility)
		{
			IncomingAbilityId = FBattleAbilityRules::GetIntimidateId();
		}
		TUniquePtr<FBattleEngine> Engine;
		if (!Test.TestTrue(TEXT("Failure-family Switch engine is created"),
				TryMakeSequenceEngine(
					MakeAtomicVoluntarySwitchScenario(
						IncomingItemId,
						IncomingAbilityId,
						Family == EAtomicSwitchFailureFamily::ImmediateHeldItem ? 90 : 200),
					{},
					Engine))
			|| !Test.TestTrue(TEXT("Failure-family Switch is locked"),
				TryPrepareAtomicVoluntarySwitch(*Engine))
			|| !Test.TestTrue(TEXT("Outgoing transient state is seeded"),
				TrySeedAtomicSwitchOutgoingTransients(*Engine))
			|| !Test.TestTrue(TEXT("Failure-family Switch is started"),
				TryBeginAtomicVoluntarySwitch(*Engine)))
		{
			return false;
		}
		if (Family == EAtomicSwitchFailureFamily::EntryHazard
			&& !Test.TestTrue(TEXT("Entry hazard is seeded"),
				TrySeedAtomicSwitchHazard(
					*Engine,
					FBattleFieldSideConditionRules::GetSpikesId())))
		{
			return false;
		}
		FBattleEngineState& MutableState =
			FBattleC09BWildFlowEngineFixture::GetMutableState(*Engine);
		MutableState.NextTriggerReentrancyToken =
			TNumericLimits<uint64>::Max() - 1;
		const FAtomicSwitchCheckpointObservation Before =
			ObserveAtomicSwitchCheckpoint(*Engine);
		const FBattleResolution Rejected = Engine->ExecuteCurrentSwitch();
		return VerifyRejectedAtomicVoluntarySwitch(
			Test,
			*Engine,
			Before,
			Before.StateVersion,
			EBattleRejectionReason::CheckpointPreparationFailed,
			Rejected);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023D1RunAtomicCommitTest,
	"PokemonSolarus.Battle.ADR0002.3D1.WildActions.Run.SuccessFailureAndNoDraw",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023D1RunAtomicCommitTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FAtomicWildScenario Scenario;

	TUniquePtr<FBattleEngine> Failed;
	if (!TestTrue(TEXT("Failed-Run engine is created"),
		TryMakeSequenceEngine(Scenario, {200}, Failed))
		|| !TestTrue(TEXT("Failed Run turn locks"),
			LockTurn(*Failed, PlayerLeftValue, EBattleActionKind::Run))
		|| !TestTrue(TEXT("Failed Run starts"),
			BeginExpectedWildAction(*Failed, PlayerLeftValue, EBattleActionKind::Run)))
	{
		return false;
	}
	const int32 FailedResolutionCountBefore =
		FBattleC09BWildFlowEngineFixture::GetState(*Failed).Resolutions.Num();
	const FBattleResolution FailedResolution = Failed->ExecuteCurrentWildAction();
	TestTrue(TEXT("Legal failed Run is accepted"), FailedResolution.WasAccepted());
	TestEqual(TEXT("Failed Run increments the escape counter"),
		Failed->GetSnapshot().GetEscapeAttemptCount(), 2U);
	TestEqual(TEXT("Failed Run commits one parent draw"),
		FBattleC09BWildFlowEngineFixture::GetState(*Failed).Random->GetTrace().Num(), 1);
	TestTrue(TEXT("Failed Run preserves exact events"),
		HasExactEventOrder(FailedResolution, {
			EBattleEventType::RunAttempted,
			EBattleEventType::ActionCompleted}));
	TestEqual(TEXT("Failed Run appends one resolution"),
		FBattleC09BWildFlowEngineFixture::GetState(*Failed).Resolutions.Num(),
		FailedResolutionCountBefore + 1);
	TestTrue(TEXT("Failed Run returns the appended resolution"),
		IsReturnedResolutionAppendedExactlyOnce(*Failed, FailedResolution));

	TUniquePtr<FBattleEngine> Succeeded;
	if (!TestTrue(TEXT("Successful-Run engine is created"),
		TryMakeSequenceEngine(Scenario, {0}, Succeeded))
		|| !TestTrue(TEXT("Successful Run turn locks"),
			LockTurn(*Succeeded, PlayerLeftValue, EBattleActionKind::Run))
		|| !TestTrue(TEXT("Successful Run starts"),
			BeginExpectedWildAction(*Succeeded, PlayerLeftValue, EBattleActionKind::Run)))
	{
		return false;
	}
	const FBattleResolution SuccessResolution = Succeeded->ExecuteCurrentWildAction();
	TestTrue(TEXT("Legal successful Run is accepted"), SuccessResolution.WasAccepted());
	TestEqual(TEXT("Successful Run reaches terminal Escape"),
		Succeeded->GetSnapshot().GetOutcome(), EBattleOutcome::Escape);
	TestEqual(TEXT("Successful Run keeps ordinary cause"),
		Succeeded->GetSnapshot().GetOutcomeCause(), EBattleOutcomeCause::Ordinary);
	TestTrue(TEXT("Successful Run preserves exact events"),
		HasExactEventOrder(SuccessResolution, {
			EBattleEventType::RunAttempted,
			EBattleEventType::Escaped,
			EBattleEventType::ActionCompleted,
			EBattleEventType::BattleEnded}));
	TestTrue(TEXT("Successful Run returns the appended resolution"),
		IsReturnedResolutionAppendedExactlyOnce(*Succeeded, SuccessResolution));

	FAtomicWildScenario GuaranteedScenario;
	GuaranteedScenario.PlayerLeftSpeed = 100;
	GuaranteedScenario.OpponentLeftSpeed = 4;
	TUniquePtr<FBattleEngine> Guaranteed;
	if (!TestTrue(TEXT("Guaranteed-Run engine is created"),
		TryMakeSequenceEngine(GuaranteedScenario, {}, Guaranteed))
		|| !TestTrue(TEXT("Guaranteed Run turn locks"),
			LockTurn(*Guaranteed, PlayerLeftValue, EBattleActionKind::Run))
		|| !TestTrue(TEXT("Guaranteed Run starts"),
			BeginExpectedWildAction(*Guaranteed, PlayerLeftValue, EBattleActionKind::Run)))
	{
		return false;
	}
	const FBattleResolution GuaranteedResolution = Guaranteed->ExecuteCurrentWildAction();
	TestTrue(TEXT("F greater than 255 succeeds"), GuaranteedResolution.WasAccepted());
	TestEqual(TEXT("Guaranteed Run publishes no parent draw"),
		FBattleC09BWildFlowEngineFixture::GetState(*Guaranteed).Random->GetTrace().Num(), 0);
	TestEqual(TEXT("Guaranteed Run remains ordinary Escape"),
		Guaranteed->GetSnapshot().GetOutcomeCause(), EBattleOutcomeCause::Ordinary);
	TestEqual(TEXT("Run replay schema remains 6"),
		Guaranteed->ExportReplayRecord().GetSchemaVersion(), 6U);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023D1WildFleeModesAtomicCommitTest,
	"PokemonSolarus.Battle.ADR0002.3D1.WildActions.WildFlee.DeterministicAndChance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023D1WildFleeModesAtomicCommitTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	for (const EBattleWildFleeMode Mode : {
		EBattleWildFleeMode::Never,
		EBattleWildFleeMode::Always})
	{
		FAtomicWildScenario Scenario;
		Scenario.WildFleeMode = Mode;
		TUniquePtr<FBattleEngine> Engine;
		if (!TestTrue(TEXT("Deterministic WildFlee engine is created"),
			TryMakeSequenceEngine(Scenario, {}, Engine))
			|| !TestTrue(TEXT("Deterministic WildFlee turn locks"),
				LockTurn(*Engine, OpponentLeftValue, EBattleActionKind::WildFlee))
			|| !TestTrue(TEXT("Deterministic WildFlee starts"),
				BeginExpectedWildAction(*Engine, OpponentLeftValue, EBattleActionKind::WildFlee)))
		{
			return false;
		}
		const FBattleResolution Resolution = Engine->ExecuteCurrentWildAction();
		TestTrue(TEXT("Deterministic WildFlee checkpoint is accepted"), Resolution.WasAccepted());
		TestEqual(TEXT("Deterministic WildFlee publishes no parent draw"),
			FBattleC09BWildFlowEngineFixture::GetState(*Engine).Random->GetTrace().Num(), 0);
		TestEqual(TEXT("Never leaves the actor active; Always removes it"),
			FBattleC09BWildFlowEngineFixture::IsRemoved(
				*Engine,
				MakeNumericId<FBattlerId>(OpponentLeftValue)),
			Mode == EBattleWildFleeMode::Always);
		TestTrue(TEXT("Deterministic result returns the appended resolution"),
			IsReturnedResolutionAppendedExactlyOnce(*Engine, Resolution));
	}

	for (const uint32 Draw : {1U, 0U})
	{
		FAtomicWildScenario Scenario;
		Scenario.WildFleeMode = EBattleWildFleeMode::Chance;
		Scenario.WildFleeNumerator = 1;
		Scenario.WildFleeDenominator = 2;
		TUniquePtr<FBattleEngine> Engine;
		if (!TestTrue(TEXT("Chance WildFlee engine is created"),
			TryMakeSequenceEngine(Scenario, {Draw}, Engine))
			|| !TestTrue(TEXT("Chance WildFlee turn locks"),
				LockTurn(*Engine, OpponentLeftValue, EBattleActionKind::WildFlee))
			|| !TestTrue(TEXT("Chance WildFlee starts"),
				BeginExpectedWildAction(*Engine, OpponentLeftValue, EBattleActionKind::WildFlee)))
		{
			return false;
		}
		const FBattleResolution Resolution = Engine->ExecuteCurrentWildAction();
		TestTrue(TEXT("Chance WildFlee checkpoint is accepted"), Resolution.WasAccepted());
		TestEqual(TEXT("Chance WildFlee commits exactly one parent draw"),
			FBattleC09BWildFlowEngineFixture::GetState(*Engine).Random->GetTrace().Num(), 1);
		TestEqual(TEXT("Chance outcome follows draw below numerator"),
			FBattleC09BWildFlowEngineFixture::IsRemoved(
				*Engine,
				MakeNumericId<FBattlerId>(OpponentLeftValue)),
			Draw == 0);
		TestTrue(TEXT("Chance result returns the appended resolution"),
			IsReturnedResolutionAppendedExactlyOnce(*Engine, Resolution));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023D1MultiWildQueueContinuationTest,
	"PokemonSolarus.Battle.ADR0002.3D1.WildActions.WildFlee.MultiActiveQueueContinuation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023D1MultiWildQueueContinuationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FAtomicWildScenario Scenario;
	Scenario.Format = EBattleFormat::Double;
	Scenario.WildFleeMode = EBattleWildFleeMode::Always;
	TUniquePtr<FBattleEngine> Engine;
	if (!TestTrue(TEXT("Multi-Wild engine is created"),
		TryMakeSequenceEngine(Scenario, {}, Engine))
		|| !TestTrue(TEXT("Multi-Wild turn locks"),
			LockTurn(*Engine, OpponentLeftValue, EBattleActionKind::WildFlee))
		|| !TestTrue(TEXT("WildFlee is the first locked action"),
			BeginExpectedWildAction(*Engine, OpponentLeftValue, EBattleActionKind::WildFlee)))
	{
		return false;
	}

	const FBattleResolution Resolution = Engine->ExecuteCurrentWildAction();
	TestTrue(TEXT("One active Wild may flee"), Resolution.WasAccepted());
	TestTrue(TEXT("Only the fleeing Wild is removed"),
		FBattleC09BWildFlowEngineFixture::IsRemoved(
			*Engine,
			MakeNumericId<FBattlerId>(OpponentLeftValue)));
	TestFalse(TEXT("The remaining active Wild is not removed"),
		FBattleC09BWildFlowEngineFixture::IsRemoved(
			*Engine,
			MakeNumericId<FBattlerId>(OpponentRightValue)));
	TestTrue(TEXT("The remaining Wild stays active"),
		FBattleC09BWildFlowEngineFixture::IsActive(
			*Engine,
			MakeNumericId<FBattlerId>(OpponentRightValue)));
	TestEqual(TEXT("Battle remains in progress"),
		Engine->GetSnapshot().GetOutcome(), EBattleOutcome::InProgress);
	TestTrue(TEXT("Multi-Wild flee preserves exact event order"),
		HasExactEventOrder(Resolution, {
			EBattleEventType::RunAttempted,
			EBattleEventType::Escaped,
			EBattleEventType::LeftActiveSlot,
			EBattleEventType::Removed,
			EBattleEventType::OpponentRemovalCheckpoint,
			EBattleEventType::ActionCompleted}));
	TestTrue(TEXT("Remaining locked actions execute to queue boundary"),
		ExecuteRemainingQueue(*Engine));
	TestEqual(TEXT("Queue reaches EndOfTurn without Wild replacement"),
		Engine->GetSnapshot().GetPhase(), EBattlePhase::EndOfTurn);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023D1PreparationFailureTest,
	"PokemonSolarus.Battle.ADR0002.3D1.WildActions.Failure.Preparation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023D1PreparationFailureTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FAtomicWildScenario RunScenario;
	RunScenario.PlayerLeftSpeed = 100;
	RunScenario.OpponentLeftSpeed = 4;
	TUniquePtr<FBattleEngine> RunEngine;
	if (!TestTrue(TEXT("Run cleanup-failure engine is created"),
		TryMakeSequenceEngine(RunScenario, {}, RunEngine))
		|| !TestTrue(TEXT("Run cleanup-failure turn locks"),
			LockTurn(*RunEngine, PlayerLeftValue, EBattleActionKind::Run))
		|| !TestTrue(TEXT("Run cleanup-failure action starts"),
			BeginExpectedWildAction(*RunEngine, PlayerLeftValue, EBattleActionKind::Run)))
	{
		return false;
	}
	FBattleC09BWildFlowEngineFixture::SetNextTriggerReentrancyToken(
		*RunEngine,
		TNumericLimits<uint64>::Max());
	const FCheckpointObservation RunBefore = ObserveCheckpoint(
		*RunEngine,
		MakeNumericId<FBattlerId>(PlayerLeftValue));
	const FBattleResolution RunRejected = RunEngine->ExecuteCurrentWildAction();
	VerifyRejectedCheckpoint(
		*this,
		*RunEngine,
		MakeNumericId<FBattlerId>(PlayerLeftValue),
		RunBefore,
		RunBefore.StateVersion,
		EBattleRejectionReason::CheckpointPreparationFailed,
		RunRejected);

	FAtomicWildScenario FleeScenario;
	FleeScenario.WildFleeMode = EBattleWildFleeMode::Always;
	TUniquePtr<FBattleEngine> FleeEngine;
	if (!TestTrue(TEXT("WildFlee cleanup-failure engine is created"),
		TryMakeSequenceEngine(FleeScenario, {}, FleeEngine))
		|| !TestTrue(TEXT("WildFlee cleanup-failure turn locks"),
			LockTurn(*FleeEngine, OpponentLeftValue, EBattleActionKind::WildFlee))
		|| !TestTrue(TEXT("WildFlee cleanup-failure action starts"),
			BeginExpectedWildAction(*FleeEngine, OpponentLeftValue, EBattleActionKind::WildFlee)))
	{
		return false;
	}
	FBattleC09BWildFlowEngineFixture::SetNextTriggerReentrancyToken(
		*FleeEngine,
		TNumericLimits<uint64>::Max());
	const FCheckpointObservation FleeBefore = ObserveCheckpoint(
		*FleeEngine,
		MakeNumericId<FBattlerId>(OpponentLeftValue));
	const FBattleResolution FleeRejected = FleeEngine->ExecuteCurrentWildAction();
	VerifyRejectedCheckpoint(
		*this,
		*FleeEngine,
		MakeNumericId<FBattlerId>(OpponentLeftValue),
		FleeBefore,
		FleeBefore.StateVersion,
		EBattleRejectionReason::CheckpointPreparationFailed,
		FleeRejected);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023D1RandomStageFailureTest,
	"PokemonSolarus.Battle.ADR0002.3D1.WildActions.Failure.RandomStage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023D1RandomStageFailureTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FAtomicWildScenario RunScenario;
	TUniquePtr<FBattleEngine> CreateEngine;
	FFaultBattleRandom* CreateRandom = nullptr;
	if (!TestTrue(TEXT("Transaction-create failure engine is created"),
		TryMakeFaultEngine(
			RunScenario,
			{0},
			EFaultRandomMode::CreateTransaction,
			CreateEngine,
			CreateRandom))
		|| !TestTrue(TEXT("Transaction-create failure turn locks"),
			LockTurn(*CreateEngine, PlayerLeftValue, EBattleActionKind::Run))
		|| !TestTrue(TEXT("Transaction-create failure action starts"),
			BeginExpectedWildAction(*CreateEngine, PlayerLeftValue, EBattleActionKind::Run)))
	{
		return false;
	}
	check(CreateRandom != nullptr);
	const FCheckpointObservation CreateBefore = ObserveCheckpoint(
		*CreateEngine,
		MakeNumericId<FBattlerId>(PlayerLeftValue));
	const FBattleResolution CreateRejected = CreateEngine->ExecuteCurrentWildAction();
	VerifyRejectedCheckpoint(
		*this,
		*CreateEngine,
		MakeNumericId<FBattlerId>(PlayerLeftValue),
		CreateBefore,
		CreateBefore.StateVersion,
		EBattleRejectionReason::CheckpointRandomStageFailed,
		CreateRejected);

	FAtomicWildScenario ChanceScenario;
	ChanceScenario.WildFleeMode = EBattleWildFleeMode::Chance;
	ChanceScenario.WildFleeNumerator = 1;
	ChanceScenario.WildFleeDenominator = 2;
	TUniquePtr<FBattleEngine> DrawEngine;
	FFaultBattleRandom* DrawRandom = nullptr;
	if (!TestTrue(TEXT("Staged-draw failure engine is created"),
		TryMakeFaultEngine(
			ChanceScenario,
			{0},
			EFaultRandomMode::Draw,
			DrawEngine,
			DrawRandom))
		|| !TestTrue(TEXT("Staged-draw failure turn locks"),
			LockTurn(*DrawEngine, OpponentLeftValue, EBattleActionKind::WildFlee))
		|| !TestTrue(TEXT("Staged-draw failure action starts"),
			BeginExpectedWildAction(*DrawEngine, OpponentLeftValue, EBattleActionKind::WildFlee)))
	{
		return false;
	}
	check(DrawRandom != nullptr);
	const FCheckpointObservation DrawBefore = ObserveCheckpoint(
		*DrawEngine,
		MakeNumericId<FBattlerId>(OpponentLeftValue));
	const FBattleResolution DrawRejected = DrawEngine->ExecuteCurrentWildAction();
	VerifyRejectedCheckpoint(
		*this,
		*DrawEngine,
		MakeNumericId<FBattlerId>(OpponentLeftValue),
		DrawBefore,
		DrawBefore.StateVersion,
		EBattleRejectionReason::CheckpointRandomStageFailed,
		DrawRejected);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023D1StaleIdentityFailureTest,
	"PokemonSolarus.Battle.ADR0002.3D1.WildActions.Failure.StaleIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023D1StaleIdentityFailureTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FAtomicWildScenario Scenario;
	TUniquePtr<FBattleEngine> Engine;
	FFaultBattleRandom* Random = nullptr;
	if (!TestTrue(TEXT("Stale-identity engine is created"),
		TryMakeFaultEngine(
			Scenario,
			{0},
			EFaultRandomMode::StaleAfterDraw,
			Engine,
			Random))
		|| !TestTrue(TEXT("Stale-identity turn locks"),
			LockTurn(*Engine, PlayerLeftValue, EBattleActionKind::Run))
		|| !TestTrue(TEXT("Stale-identity action starts"),
			BeginExpectedWildAction(*Engine, PlayerLeftValue, EBattleActionKind::Run)))
	{
		return false;
	}
	check(Random != nullptr);
	Random->SetAfterDraw([EnginePtr = Engine.Get()]()
	{
		FBattleC09BWildFlowEngineFixture::AdvanceStateVersion(*EnginePtr);
	});
	const FCheckpointObservation Before = ObserveCheckpoint(
		*Engine,
		MakeNumericId<FBattlerId>(PlayerLeftValue));
	const FBattleResolution Rejected = Engine->ExecuteCurrentWildAction();
	VerifyRejectedCheckpoint(
		*this,
		*Engine,
		MakeNumericId<FBattlerId>(PlayerLeftValue),
		Before,
		Before.StateVersion + 1,
		EBattleRejectionReason::StaleCheckpointIdentity,
		Rejected);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023D1RandomCommitFailureTest,
	"PokemonSolarus.Battle.ADR0002.3D1.WildActions.Failure.RandomCommit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023D1RandomCommitFailureTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FAtomicWildScenario Scenario;
	TUniquePtr<FBattleEngine> Engine;
	FFaultBattleRandom* Random = nullptr;
	if (!TestTrue(TEXT("Random-commit failure engine is created"),
		TryMakeFaultEngine(
			Scenario,
			{0},
			EFaultRandomMode::Commit,
			Engine,
			Random))
		|| !TestTrue(TEXT("Random-commit failure turn locks"),
			LockTurn(*Engine, PlayerLeftValue, EBattleActionKind::Run))
		|| !TestTrue(TEXT("Random-commit failure action starts"),
			BeginExpectedWildAction(*Engine, PlayerLeftValue, EBattleActionKind::Run)))
	{
		return false;
	}
	check(Random != nullptr);
	const FCheckpointObservation Before = ObserveCheckpoint(
		*Engine,
		MakeNumericId<FBattlerId>(PlayerLeftValue));
	const FBattleResolution Rejected = Engine->ExecuteCurrentWildAction();
	VerifyRejectedCheckpoint(
		*this,
		*Engine,
		MakeNumericId<FBattlerId>(PlayerLeftValue),
		Before,
		Before.StateVersion,
		EBattleRejectionReason::RandomTransactionCommitFailed,
		Rejected);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023D3CaptureZeroIndicatorTest,
	"PokemonSolarus.Battle.ADR0002.3D3.Capture.Preparation.ZeroIndicatorBeforeTransaction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023D3CaptureZeroIndicatorTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FAtomicWildScenario Scenario = MakeAtomicCaptureScenario();
	Scenario.CatchRate = 1;
	Scenario.CaptureProgression.CaptureCoefficientQ12 = 1;
	TUniquePtr<FBattleEngine> Engine;
	FFaultBattleRandom* Random = nullptr;
	if (!TestTrue(TEXT("Zero-indicator engine is created"),
		TryMakeFaultEngine(
			Scenario,
			{},
			EFaultRandomMode::Commit,
			Engine,
			Random))
		|| !TestTrue(TEXT("Zero-indicator Capture turn locks"),
			LockTurn(*Engine, PlayerLeftValue, EBattleActionKind::Bag))
		|| !TestTrue(TEXT("Zero-indicator Capture starts"),
			BeginExpectedWildAction(*Engine, PlayerLeftValue, EBattleActionKind::Bag)))
	{
		return false;
	}

	check(Random != nullptr);
	const FBattlerId TargetId = MakeNumericId<FBattlerId>(OpponentLeftValue);
	const FCheckpointObservation Before = ObserveCheckpoint(*Engine, TargetId);
	const FBattleResolution Rejected = Engine->ExecuteCurrentBagItem();
	VerifyRejectedCheckpoint(
		*this,
		*Engine,
		TargetId,
		Before,
		Before.StateVersion,
		EBattleRejectionReason::CheckpointPreparationFailed,
		Rejected);
	TestEqual(TEXT("Zero indicator creates no RNG transaction"),
		Random->GetCounters().TransactionCreateAttempts, 0);
	TestEqual(TEXT("Zero indicator attempts no RNG draw"),
		Random->GetCounters().DrawAttempts, 0);
	TestEqual(TEXT("Zero indicator attempts no RNG commit"),
		Random->GetCounters().CommitAttempts, 0);
	TestFalse(TEXT("Zero-indicator rejection publishes no item success fact"),
		HasEvent(Rejected, EBattleEventType::ItemUsed));
	TestFalse(TEXT("Zero-indicator rejection publishes no CaptureAttempted fact"),
		HasEvent(Rejected, EBattleEventType::CaptureAttempted));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023D3CaptureRandomRollbackTest,
	"PokemonSolarus.Battle.ADR0002.3D3.Capture.Failure.RandomCreationCriticalShakeRollback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023D3CaptureRandomRollbackTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FBattlerId TargetId = MakeNumericId<FBattlerId>(OpponentLeftValue);

	FAtomicWildScenario CreateScenario = MakeAtomicCaptureScenario();
	TUniquePtr<FBattleEngine> CreateEngine;
	FFaultBattleRandom* CreateRandom = nullptr;
	if (!TestTrue(TEXT("Capture transaction-create failure engine is created"),
		TryMakeFaultEngine(
			CreateScenario,
			{0, 0, 0, 0},
			EFaultRandomMode::CreateTransaction,
			CreateEngine,
			CreateRandom))
		|| !TestTrue(TEXT("Capture transaction-create failure turn locks"),
			LockTurn(*CreateEngine, PlayerLeftValue, EBattleActionKind::Bag))
		|| !TestTrue(TEXT("Capture transaction-create failure starts"),
			BeginExpectedWildAction(
				*CreateEngine,
				PlayerLeftValue,
				EBattleActionKind::Bag)))
	{
		return false;
	}
	check(CreateRandom != nullptr);
	const FCheckpointObservation CreateBefore = ObserveCheckpoint(*CreateEngine, TargetId);
	const FBattleResolution CreateRejected = CreateEngine->ExecuteCurrentBagItem();
	VerifyRejectedCheckpoint(
		*this,
		*CreateEngine,
		TargetId,
		CreateBefore,
		CreateBefore.StateVersion,
		EBattleRejectionReason::CheckpointRandomStageFailed,
		CreateRejected);
	TestEqual(TEXT("Capture transaction creation is attempted once"),
		CreateRandom->GetCounters().TransactionCreateAttempts, 1);
	TestEqual(TEXT("Failed transaction creation performs no draw"),
		CreateRandom->GetCounters().DrawAttempts, 0);

	FAtomicWildScenario DrawScenario = MakeAtomicCaptureScenario();
	DrawScenario.CatchRate = 120;
	DrawScenario.TargetCurrentHP = 10;
	DrawScenario.CaptureProgression.CaughtSpeciesCount = 451;
	DrawScenario.CaptureProgression.bCriticalCaptureEnabled = true;
	DrawScenario.CaptureProgression.bCatchingCharm = true;
	DrawScenario.CaptureProgression.bUseCaughtCountHPComponentModifier = true;
	TUniquePtr<FBattleEngine> DrawEngine;
	FFaultBattleRandom* DrawRandom = nullptr;
	if (!TestTrue(TEXT("Critical/shake rollback engine is created"),
		TryMakeFaultEngine(
			DrawScenario,
			{255, 0, 0, 0},
			EFaultRandomMode::Draw,
			DrawEngine,
			DrawRandom,
			2))
		|| !TestTrue(TEXT("Critical/shake rollback turn locks"),
			LockTurn(*DrawEngine, PlayerLeftValue, EBattleActionKind::Bag))
		|| !TestTrue(TEXT("Critical/shake rollback Capture starts"),
			BeginExpectedWildAction(
				*DrawEngine,
				PlayerLeftValue,
				EBattleActionKind::Bag)))
	{
		return false;
	}
	check(DrawRandom != nullptr);
	const FCheckpointObservation DrawBefore = ObserveCheckpoint(*DrawEngine, TargetId);
	const FBattleResolution DrawRejected = DrawEngine->ExecuteCurrentBagItem();
	VerifyRejectedCheckpoint(
		*this,
		*DrawEngine,
		TargetId,
		DrawBefore,
		DrawBefore.StateVersion,
		EBattleRejectionReason::CheckpointRandomStageFailed,
		DrawRejected);
	TestEqual(TEXT("Rollback staged one critical and one shake draw"),
		DrawRandom->GetCounters().SuccessfulDraws, 2);
	TestEqual(TEXT("A later shake failure is the third draw attempt"),
		DrawRandom->GetCounters().DrawAttempts, 3);
	TestEqual(TEXT("Random-stage rollback never attempts parent commit"),
		DrawRandom->GetCounters().CommitAttempts, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023D3CaptureIdentityCommitFailureTest,
	"PokemonSolarus.Battle.ADR0002.3D3.Capture.Failure.StaleIdentityAndRandomCommit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023D3CaptureIdentityCommitFailureTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FBattlerId TargetId = MakeNumericId<FBattlerId>(OpponentLeftValue);
	FAtomicWildScenario Scenario = MakeAtomicCaptureScenario();

	TUniquePtr<FBattleEngine> StaleEngine;
	FFaultBattleRandom* StaleRandom = nullptr;
	if (!TestTrue(TEXT("Stale Capture engine is created"),
		TryMakeFaultEngine(
			Scenario,
			{0, 0, 0, 0},
			EFaultRandomMode::StaleAfterDraw,
			StaleEngine,
			StaleRandom))
		|| !TestTrue(TEXT("Stale Capture turn locks"),
			LockTurn(*StaleEngine, PlayerLeftValue, EBattleActionKind::Bag))
		|| !TestTrue(TEXT("Stale Capture starts"),
			BeginExpectedWildAction(
				*StaleEngine,
				PlayerLeftValue,
				EBattleActionKind::Bag)))
	{
		return false;
	}
	check(StaleRandom != nullptr);
	StaleRandom->SetAfterDraw([EnginePtr = StaleEngine.Get()]()
	{
		FBattleC09BWildFlowEngineFixture::AdvanceStateVersion(*EnginePtr);
	});
	const FCheckpointObservation StaleBefore = ObserveCheckpoint(*StaleEngine, TargetId);
	const FBattleResolution StaleRejected = StaleEngine->ExecuteCurrentBagItem();
	VerifyRejectedCheckpoint(
		*this,
		*StaleEngine,
		TargetId,
		StaleBefore,
		StaleBefore.StateVersion + 1,
		EBattleRejectionReason::StaleCheckpointIdentity,
		StaleRejected);
	TestTrue(TEXT("Stale identity is injected after staged Capture draws"),
		StaleRandom->GetCounters().SuccessfulDraws > 0);
	TestEqual(TEXT("Stale identity prevents parent RNG commit"),
		StaleRandom->GetCounters().CommitAttempts, 0);

	TUniquePtr<FBattleEngine> CommitEngine;
	FFaultBattleRandom* CommitRandom = nullptr;
	if (!TestTrue(TEXT("Capture commit-failure engine is created"),
		TryMakeFaultEngine(
			Scenario,
			{0, 0, 0, 0},
			EFaultRandomMode::Commit,
			CommitEngine,
			CommitRandom))
		|| !TestTrue(TEXT("Capture commit-failure turn locks"),
			LockTurn(*CommitEngine, PlayerLeftValue, EBattleActionKind::Bag))
		|| !TestTrue(TEXT("Capture commit-failure starts"),
			BeginExpectedWildAction(
				*CommitEngine,
				PlayerLeftValue,
				EBattleActionKind::Bag)))
	{
		return false;
	}
	check(CommitRandom != nullptr);
	const FCheckpointObservation CommitBefore = ObserveCheckpoint(*CommitEngine, TargetId);
	const FBattleResolution CommitRejected = CommitEngine->ExecuteCurrentBagItem();
	VerifyRejectedCheckpoint(
		*this,
		*CommitEngine,
		TargetId,
		CommitBefore,
		CommitBefore.StateVersion,
		EBattleRejectionReason::RandomTransactionCommitFailed,
		CommitRejected);
	TestEqual(TEXT("Capture RNG commit is attempted exactly once"),
		CommitRandom->GetCounters().CommitAttempts, 1);
	TestTrue(TEXT("Commit failure occurs after all four staged shakes"),
		CommitRandom->GetCounters().SuccessfulDraws == 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023D3CaptureStaleExecutionTest,
	"PokemonSolarus.Battle.ADR0002.3D3.Capture.Execution.StalePreUseCancellation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023D3CaptureStaleExecutionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FAtomicWildScenario Scenario = MakeAtomicCaptureScenario();
	Scenario.CaptureProgression.bMustCapture = true;
	TUniquePtr<FBattleEngine> Engine;
	if (!TestTrue(TEXT("Pre-use stale Capture engine is created"),
		TryMakeSequenceEngine(Scenario, {}, Engine))
		|| !TestTrue(TEXT("Pre-use stale Capture turn locks"),
			LockTurn(*Engine, PlayerLeftValue, EBattleActionKind::Bag))
		|| !TestTrue(TEXT("Pre-use stale Capture starts"),
			BeginExpectedWildAction(*Engine, PlayerLeftValue, EBattleActionKind::Bag)))
	{
		return false;
	}

	FBattleEngineState& MutableState =
		FBattleC09BWildFlowEngineFixture::GetMutableState(*Engine);
	FBattleBattlerState* Target = MutableState.FindMutableBattler(
		MakeNumericId<FBattlerId>(OpponentLeftValue));
	check(Target != nullptr);
	Target->CaptureClassification = EBattleCaptureSpeciesClassification::UltraBeast;
	const FCheckpointObservation Before = ObserveCheckpoint(*Engine, Target->BattlerId);
	const FBattleResolution Canceled = Engine->ExecuteCurrentBagItem();
	const FBattleEngineState& State =
		FBattleC09BWildFlowEngineFixture::GetState(*Engine);
	const FBattleTrainerState* PlayerTrainer = State.FindTrainer(
		MakeNumericId<FTrainerId>(PlayerTrainerValue));
	TestTrue(TEXT("A stale pre-use Capture is an accepted queue cancellation"),
		Canceled.WasAccepted());
	TestTrue(TEXT("Stale pre-use Capture preserves exact event order"),
		HasExactEventOrder(Canceled, {
			EBattleEventType::ActionCanceled,
			EBattleEventType::ActionCompleted}));
	TestEqual(TEXT("Stale pre-use Capture preserves Poke Balls"),
		PlayerTrainer != nullptr ? PlayerTrainer->Bag[0].Count : INDEX_NONE,
		Before.PokeBallCount);
	TestEqual(TEXT("Stale pre-use Capture preserves Bag quota"),
		PlayerTrainer != nullptr
			? PlayerTrainer->ActionAllowance.bBagActionAvailable
			: false,
		Before.bBagActionAvailable);
	TestEqual(TEXT("Stale pre-use Capture consumes no gameplay RNG"),
		State.Random->GetTrace().Num(), Before.RandomTraceCount);
	TestEqual(TEXT("Stale pre-use Capture creates no pending record"),
		State.PendingCaptures.Num(), Before.PendingCaptureCount);
	TestTrue(TEXT("Stale pre-use Capture leaves the target active"),
		FBattleC09BWildFlowEngineFixture::IsActive(*Engine, Target->BattlerId));
	TestEqual(TEXT("Stale pre-use cancellation advances only its action cursor"),
		State.CurrentLockedActionIndex, Before.LockedActionIndex + 1);
	TestTrue(TEXT("Stale pre-use resolution returns the exact single append"),
		IsReturnedResolutionAppendedExactlyOnce(*Engine, Canceled));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023D3CaptureAtomicPublicationTest,
	"PokemonSolarus.Battle.ADR0002.3D3.Capture.Execution.LegalFailureAndAtomicSuccess",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023D3CaptureAtomicPublicationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FBattlerId TargetId = MakeNumericId<FBattlerId>(OpponentLeftValue);
	const FTrainerId PlayerTrainerId = MakeNumericId<FTrainerId>(PlayerTrainerValue);
	FAtomicWildScenario Scenario = MakeAtomicCaptureScenario();

	TUniquePtr<FBattleEngine> FailureEngine;
	if (!TestTrue(TEXT("Legal failed-Capture engine is created"),
		TryMakeSequenceEngine(Scenario, {65535}, FailureEngine))
		|| !TestTrue(TEXT("Legal failed-Capture turn locks"),
			LockTurn(*FailureEngine, PlayerLeftValue, EBattleActionKind::Bag))
		|| !TestTrue(TEXT("Legal failed-Capture starts"),
			BeginExpectedWildAction(
				*FailureEngine,
				PlayerLeftValue,
				EBattleActionKind::Bag)))
	{
		return false;
	}
	const FCheckpointObservation FailureBefore = ObserveCheckpoint(*FailureEngine, TargetId);
	const FBattleResolution Failed = FailureEngine->ExecuteCurrentBagItem();
	const FBattleEngineState& FailureState =
		FBattleC09BWildFlowEngineFixture::GetState(*FailureEngine);
	const FBattleTrainerState* FailureTrainer = FailureState.FindTrainer(PlayerTrainerId);
	TestTrue(TEXT("Legal unsuccessful Capture is accepted"), Failed.WasAccepted());
	TestTrue(TEXT("Legal unsuccessful Capture preserves exact event order"),
		HasExactEventOrder(Failed, {
			EBattleEventType::ItemUsed,
			EBattleEventType::ItemConsumed,
			EBattleEventType::CaptureAttempted,
			EBattleEventType::ActionCompleted}));
	TestEqual(TEXT("Legal failure consumes exactly one Ball"),
		FailureTrainer != nullptr ? FailureTrainer->Bag[0].Count : INDEX_NONE,
		FailureBefore.PokeBallCount - 1);
	TestFalse(TEXT("Legal failure consumes exactly one Trainer Bag action"),
		FailureTrainer != nullptr
			&& FailureTrainer->ActionAllowance.bBagActionAvailable);
	TestEqual(TEXT("Legal failure commits one early-stopping draw"),
		FailureState.Random->GetTrace().Num(), FailureBefore.RandomTraceCount + 1);
	TestTrue(TEXT("Legal failure draw uses the Capture shake purpose"),
		!FailureState.Random->GetTrace().IsEmpty()
			&& FailureState.Random->GetTrace().Last().RulePurpose
				== FBattleCaptureCalculator::GetShakeCheckPurpose());
	TestTrue(TEXT("Legal failure leaves its target active"),
		FBattleC09BWildFlowEngineFixture::IsActive(*FailureEngine, TargetId));
	TestEqual(TEXT("Legal failure creates no pending record"),
		FailureState.PendingCaptures.Num(), 0);
	TestTrue(TEXT("Legal failure returns the exact single append"),
		IsReturnedResolutionAppendedExactlyOnce(*FailureEngine, Failed));

	TUniquePtr<FBattleEngine> SuccessEngine;
	if (!TestTrue(TEXT("Atomic successful-Capture engine is created"),
		TryMakeSequenceEngine(Scenario, {0, 0, 0, 0}, SuccessEngine))
		|| !TestTrue(TEXT("Atomic successful-Capture turn locks"),
			LockTurn(*SuccessEngine, PlayerLeftValue, EBattleActionKind::Bag))
		|| !TestTrue(TEXT("Atomic successful-Capture starts"),
			BeginExpectedWildAction(
				*SuccessEngine,
				PlayerLeftValue,
				EBattleActionKind::Bag)))
	{
		return false;
	}
	const FCheckpointObservation SuccessBefore = ObserveCheckpoint(*SuccessEngine, TargetId);
	const FBattleResolution Succeeded = SuccessEngine->ExecuteCurrentBagItem();
	const FBattleEngineState& SuccessState =
		FBattleC09BWildFlowEngineFixture::GetState(*SuccessEngine);
	const FBattleTrainerState* SuccessTrainer = SuccessState.FindTrainer(PlayerTrainerId);
	const FBattleBattlerState* CapturedBattler = SuccessState.FindBattler(TargetId);
	TestTrue(TEXT("Successful Capture is accepted"), Succeeded.WasAccepted());
	TestTrue(TEXT("Terminal successful Capture preserves exact event order"),
		HasExactEventOrder(Succeeded, {
			EBattleEventType::ItemUsed,
			EBattleEventType::ItemConsumed,
			EBattleEventType::CaptureAttempted,
			EBattleEventType::Captured,
			EBattleEventType::LeftActiveSlot,
			EBattleEventType::Removed,
			EBattleEventType::OpponentRemovalCheckpoint,
			EBattleEventType::ActionCompleted,
			EBattleEventType::BattleEnded}));
	TestEqual(TEXT("Successful Capture consumes exactly one Ball"),
		SuccessTrainer != nullptr ? SuccessTrainer->Bag[0].Count : INDEX_NONE,
		SuccessBefore.PokeBallCount - 1);
	TestFalse(TEXT("Successful Capture consumes Trainer Bag quota"),
		SuccessTrainer != nullptr
			&& SuccessTrainer->ActionAllowance.bBagActionAvailable);
	TestEqual(TEXT("Successful Capture commits exactly four shake draws"),
		SuccessState.Random->GetTrace().Num(), SuccessBefore.RandomTraceCount + 4);
	TestTrue(TEXT("Successful Capture atomically marks and removes the target"),
		CapturedBattler != nullptr
			&& CapturedBattler->bCaptured
			&& CapturedBattler->bRemoved
			&& !FBattleC09BWildFlowEngineFixture::IsActive(*SuccessEngine, TargetId));
	TestEqual(TEXT("Successful Capture appends one pending record"),
		SuccessState.PendingCaptures.Num(), SuccessBefore.PendingCaptureCount + 1);
	if (!SuccessState.PendingCaptures.IsEmpty())
	{
		const FBattlePendingCaptureRecord& Pending = SuccessState.PendingCaptures.Last();
		const FItemId HeldItemId = MakeDefinitionId<FItemId>(CaptureHeldItemName);
		TestEqual(TEXT("Pending Capture preserves current HP"),
			Pending.CurrentHP, Scenario.TargetCurrentHP);
		TestEqual(TEXT("Pending Capture preserves Party-first destination"),
			Pending.Destination, EBattlePendingCaptureDestination::Party);
		TestTrue(TEXT("Pending Capture retains original and current held item"),
			Pending.HeldItem.OriginalItemId == HeldItemId
				&& Pending.HeldItem.CurrentItemId == HeldItemId);
	}
	TestEqual(TEXT("Last-target Capture enters Victory"),
		SuccessState.Outcome, EBattleOutcome::Victory);
	TestEqual(TEXT("Last-target Capture retains Capture outcome cause"),
		SuccessState.OutcomeCause, EBattleOutcomeCause::Capture);
	TestEqual(TEXT("Last-target Capture is terminal"),
		SuccessState.Phase, EBattlePhase::Terminal);
	const int32 RemovalEventIndex = Succeeded.GetEvents().IndexOfByPredicate(
		[](const FBattleEvent& Event)
		{
			return Event.GetType() == EBattleEventType::OpponentRemovalCheckpoint;
		});
	TestTrue(TEXT("Removal checkpoint is staged into authoritative availability"),
		RemovalEventIndex != INDEX_NONE
			&& SuccessState.AvailableOpponentRemovalCheckpoints.Contains(
				Succeeded.GetEvents()[RemovalEventIndex].GetEventOrdinal()));
	TestTrue(TEXT("Successful Capture returns the exact single append"),
		IsReturnedResolutionAppendedExactlyOnce(*SuccessEngine, Succeeded));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E1ActionStartProceedTest,
	"PokemonSolarus.Battle.ADR0002.3E1.ActionStart.Success.ProceedSuppressionAndObedience",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E1ActionStartProceedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FAtomicWildScenario Scenario;
	Scenario.PlayerLeftSpeed = 150;
	Scenario.bPlayerHasCanonicalHeldItem = true;
	Scenario.bPlayerSubjectToObedience = true;
	Scenario.PlayerReferenceLevel = 20;
	Scenario.PlayerBadgeCount = 0;
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	const FTrainerId TrainerId = MakeNumericId<FTrainerId>(PlayerTrainerValue);

	TUniquePtr<FBattleEngine> Engine;
	if (!TestTrue(TEXT("Proceed engine is created"),
		TryMakeSequenceEngine(Scenario, {}, Engine))
		|| !TestTrue(TEXT("Proceed action is locked first"),
			LockTurn(*Engine, PlayerLeftValue, EBattleActionKind::Fight))
		|| !TestTrue(TEXT("Magic Room is seeded before action start"),
			TrySeedActionStartMagicRoom(*Engine, ActorId)))
	{
		return false;
	}
	const FActionStartCheckpointObservation Before =
		ObserveActionStartCheckpoint(*Engine, ActorId, TrainerId);
	const FBattleResolution Resolution = Engine->BeginNextLockedAction();
	const FBattleEngineState& State =
		FBattleC09BWildFlowEngineFixture::GetState(*Engine);
	const FBattleTrainerState* Trainer = State.FindTrainer(TrainerId);
	const FBattleBattlerState* Actor = State.FindBattler(ActorId);
	const FBattleHeldItemInstanceState* LedgerItem = Actor != nullptr
		? State.HeldItemLedger.FindState(Actor->HeldItem.InstanceId)
		: nullptr;

	TestTrue(TEXT("Proceed action start is accepted"), Resolution.WasAccepted());
	TestTrue(TEXT("Proceed keeps exact action-start event order"),
		HasExactEventOrder(Resolution, {
			EBattleEventType::ActionStarted,
			EBattleEventType::ObedienceConfirmed}));
	TestTrue(TEXT("Proceed obedience fact keeps canonical numeric metadata"),
		Resolution.GetEvents().Num() == 2
			&& Resolution.GetEvents()[1].GetNumericBefore()
				== TOptional<int64>(20)
			&& Resolution.GetEvents()[1].GetNumericAfter()
				== TOptional<int64>(20)
			&& Resolution.GetEvents()[1].GetNumericDelta()
				== TOptional<int64>(0));
	TestTrue(TEXT("Obedience confirmation remains CoreOnly"),
		Resolution.GetEvents().Num() == 2
			&& Resolution.GetEvents()[0].GetVisibility().Level
				== EBattleVisibilityLevel::Public
			&& Resolution.GetEvents()[1].GetVisibility().Level
				== EBattleVisibilityLevel::CoreOnly);
	TestTrue(TEXT("Proceed returns the exact single append"),
		IsReturnedResolutionAppendedExactlyOnce(*Engine, Resolution));
	TestEqual(TEXT("Proceed increments state version once"),
		State.StateVersion, Before.StateVersion + 1);
	TestEqual(TEXT("Proceed resolution reports the exact version pair"),
		Resolution.GetBeforeStateVersion(), Before.StateVersion);
	TestEqual(TEXT("Proceed resolution reports one after-version"),
		Resolution.GetAfterStateVersion(), Before.StateVersion + 1);
	TestEqual(TEXT("Proceed keeps the current action cursor"),
		State.CurrentLockedActionIndex, Before.ActionIndex);
	TestEqual(TEXT("Proceed enters Resolving"), State.Phase, EBattlePhase::Resolving);
	TestTrue(TEXT("Proceed starts without finishing or committing the action"),
		State.LockedActions.IsValidIndex(Before.ActionIndex)
			&& State.LockedActions[Before.ActionIndex].bStarted
			&& !State.LockedActions[Before.ActionIndex].bFinished
			&& !State.LockedActions[Before.ActionIndex].bMoveCommitted
			&& !State.LockedActions[Before.ActionIndex].TargetResolution.IsSet()
			&& State.LockedActions[Before.ActionIndex].EffectExecutionState
				== EBattleLockedEffectExecutionState::Pending);
	TestEqual(TEXT("Proceed consumes one Trainer action"),
		Trainer != nullptr ? Trainer->ActionAllowance.RemainingActions : INDEX_NONE,
		Before.RemainingActions - 1);
	TestTrue(TEXT("Proceed suppresses both battler and ledger held-item facts"),
		Actor != nullptr
			&& Actor->HeldItem.CurrentItemId == FBattleItemRules::GetLeftoversId()
			&& Actor->HeldItem.bSuppressed
			&& !Actor->HeldItem.bConsumed
			&& LedgerItem != nullptr
			&& LedgerItem->bSuppressed
			&& !LedgerItem->bConsumed);
	TestTrue(TEXT("Proceed preserves Magic Room and re-registers suppressed item hooks"),
		CountActionStartTriggerRegistrations(
			State,
			FBattleFieldSideConditionRules::GetMagicRoomId().GetDefinitionId()) > 0
			&& CountActionStartTriggerRegistrations(
				State,
				FBattleItemRules::GetLeftoversId().GetDefinitionId()) > 0
			&& State.TriggerFramework.GetActiveRegistrations().ContainsByPredicate(
				[](const FBattleTriggerRegistrationState& Registration)
				{
					return Registration.Spec.SourceDefinition.Kind
							== EBattleTriggerSourceDefinitionKind::Item
						&& Registration.Spec.SourceDefinition.ItemId
							== FBattleItemRules::GetLeftoversId()
						&& Registration.bSuppressed;
				}));
	TestEqual(TEXT("Proceed commits the staged Magic Room and item-cleanup tokens"),
		State.NextTriggerReentrancyToken, Before.NextTriggerToken + 2);
	TestEqual(TEXT("Proceed consumes no PP"),
		ObserveActionStartCheckpoint(*Engine, ActorId, TrainerId).TotalMovePP,
		Before.TotalMovePP);
	TestEqual(TEXT("Proceed consumes no RNG"),
		State.Random->GetTrace().Num(), Before.RandomTraceCount);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E1ActionStartRechargeTest,
	"PokemonSolarus.Battle.ADR0002.3E1.ActionStart.Success.RechargeDenial",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E1ActionStartRechargeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FAtomicWildScenario Scenario;
	Scenario.PlayerLeftSpeed = 150;
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	const FTrainerId TrainerId = MakeNumericId<FTrainerId>(PlayerTrainerValue);
	const FMoveId MoveId = MakeDefinitionId<FMoveId>(ProbeMoveName);
	TUniquePtr<FBattleEngine> Engine;
	if (!TestTrue(TEXT("Recharge engine is created"),
		TryMakeSequenceEngine(Scenario, {}, Engine))
		|| !TestTrue(TEXT("Recharge action is locked first"),
			LockTurn(*Engine, PlayerLeftValue, EBattleActionKind::Fight))
		|| !TestTrue(TEXT("Charging is seeded"),
			TrySeedActionStartVolatile(
				*Engine,
				ActorId,
				FBattleVolatileRules::GetChargingId(),
				MoveId.GetDefinitionId()))
		|| !TestTrue(TEXT("Fly semi-invulnerability is seeded"),
			TrySeedActionStartVolatile(
				*Engine,
				ActorId,
				FBattleVolatileRules::GetFlySemiInvulnerableId()))
		|| !TestTrue(TEXT("Recharge is seeded"),
			TrySeedActionStartVolatile(
				*Engine,
				ActorId,
				FBattleVolatileRules::GetRechargeId())))
	{
		return false;
	}
	const FActionStartCheckpointObservation Before =
		ObserveActionStartCheckpoint(*Engine, ActorId, TrainerId);
	const FBattleResolution Resolution = Engine->BeginNextLockedAction();
	const FBattleEngineState& State =
		FBattleC09BWildFlowEngineFixture::GetState(*Engine);
	const FBattleTrainerState* Trainer = State.FindTrainer(TrainerId);

	TestTrue(TEXT("Recharge denial is an accepted consumed action"),
		Resolution.WasAccepted());
	TestTrue(TEXT("Recharge denial keeps exact event order"),
		HasExactEventOrder(Resolution, {
			EBattleEventType::ActionStarted,
			EBattleEventType::StatusChanged,
			EBattleEventType::EffectPrevented,
			EBattleEventType::ActionCanceled,
			EBattleEventType::ActionCompleted}));
	TestTrue(TEXT("Recharge status removal keeps exact numeric metadata"),
		Resolution.GetEvents().Num() == 5
			&& Resolution.GetEvents()[1].GetNumericBefore()
				== TOptional<int64>(1)
			&& Resolution.GetEvents()[1].GetNumericAfter()
				== TOptional<int64>(0)
			&& Resolution.GetEvents()[1].GetNumericDelta()
				== TOptional<int64>(-1));
	TestTrue(TEXT("Recharge denial returns the exact single append"),
		IsReturnedResolutionAppendedExactlyOnce(*Engine, Resolution));
	TestEqual(TEXT("Recharge denial increments state version once"),
		State.StateVersion, Before.StateVersion + 1);
	TestEqual(TEXT("Recharge denial advances exactly one queue action"),
		State.CurrentLockedActionIndex, Before.ActionIndex + 1);
	TestEqual(TEXT("Recharge denial remains Resolving while another action waits"),
		State.Phase, EBattlePhase::Resolving);
	TestTrue(TEXT("Recharge denial marks the consumed action started and finished"),
		State.LockedActions.IsValidIndex(Before.ActionIndex)
			&& State.LockedActions[Before.ActionIndex].bStarted
			&& State.LockedActions[Before.ActionIndex].bFinished
			&& !State.LockedActions[Before.ActionIndex].bMoveCommitted);
	TestEqual(TEXT("Recharge denial consumes one Trainer action"),
		Trainer != nullptr ? Trainer->ActionAllowance.RemainingActions : INDEX_NONE,
		Before.RemainingActions - 1);
	TestFalse(TEXT("Recharge denial removes Recharge"),
		HasActionStartVolatile(State, ActorId, FBattleVolatileRules::GetRechargeId()));
	TestFalse(TEXT("Recharge denial clears Charging"),
		HasActionStartVolatile(State, ActorId, FBattleVolatileRules::GetChargingId()));
	TestFalse(TEXT("Recharge denial clears Fly semi-invulnerability"),
		HasActionStartVolatile(
			State,
			ActorId,
			FBattleVolatileRules::GetFlySemiInvulnerableId()));
	TestTrue(TEXT("Recharge and charge trigger registrations are cleaned"),
		CountActionStartTriggerRegistrations(
			State,
			FBattleVolatileRules::GetRechargeId().GetDefinitionId()) == 0
			&& CountActionStartTriggerRegistrations(
				State,
				FBattleVolatileRules::GetChargingId().GetDefinitionId()) == 0
			&& CountActionStartTriggerRegistrations(
				State,
				FBattleVolatileRules::GetFlySemiInvulnerableId().GetDefinitionId()) == 0);
	TestEqual(TEXT("Recharge denial commits dispatch plus three cleanup tokens"),
		State.NextTriggerReentrancyToken, Before.NextTriggerToken + 4);
	TestEqual(TEXT("Recharge denial consumes no PP"),
		ObserveActionStartCheckpoint(*Engine, ActorId, TrainerId).TotalMovePP,
		Before.TotalMovePP);
	TestEqual(TEXT("Recharge denial consumes no RNG"),
		State.Random->GetTrace().Num(), Before.RandomTraceCount);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E1ActionStartObedienceRefusalTest,
	"PokemonSolarus.Battle.ADR0002.3E1.ActionStart.Success.ObedienceRefusalAndChargeCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E1ActionStartObedienceRefusalTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	FAtomicWildScenario Scenario;
	Scenario.PlayerLeftSpeed = 150;
	Scenario.bPlayerSubjectToObedience = true;
	Scenario.PlayerReferenceLevel = 21;
	Scenario.PlayerBadgeCount = 0;
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	const FTrainerId TrainerId = MakeNumericId<FTrainerId>(PlayerTrainerValue);
	const FMoveId MoveId = MakeDefinitionId<FMoveId>(ProbeMoveName);
	TUniquePtr<FBattleEngine> Engine;
	if (!TestTrue(TEXT("Obedience-refusal engine is created"),
		TryMakeSequenceEngine(Scenario, {}, Engine))
		|| !TestTrue(TEXT("Obedience-refusal action is locked first"),
			LockTurn(*Engine, PlayerLeftValue, EBattleActionKind::Fight))
		|| !TestTrue(TEXT("Refused action Charging is seeded"),
			TrySeedActionStartVolatile(
				*Engine,
				ActorId,
				FBattleVolatileRules::GetChargingId(),
				MoveId.GetDefinitionId()))
		|| !TestTrue(TEXT("Refused action Fly state is seeded"),
			TrySeedActionStartVolatile(
				*Engine,
				ActorId,
				FBattleVolatileRules::GetFlySemiInvulnerableId())))
	{
		return false;
	}
	const FActionStartCheckpointObservation Before =
		ObserveActionStartCheckpoint(*Engine, ActorId, TrainerId);
	const FBattleResolution Resolution = Engine->BeginNextLockedAction();
	const FBattleEngineState& State =
		FBattleC09BWildFlowEngineFixture::GetState(*Engine);
	const FBattleTrainerState* Trainer = State.FindTrainer(TrainerId);

	TestTrue(TEXT("Obedience refusal is an accepted consumed action"),
		Resolution.WasAccepted());
	TestTrue(TEXT("Obedience refusal keeps exact event order"),
		HasExactEventOrder(Resolution, {
			EBattleEventType::ActionStarted,
			EBattleEventType::ObedienceRefused,
			EBattleEventType::ActionCompleted}));
	TestTrue(TEXT("Obedience refusal keeps exact public numeric metadata"),
		Resolution.GetEvents().Num() == 3
			&& Resolution.GetEvents()[1].GetVisibility().Level
				== EBattleVisibilityLevel::Public
			&& Resolution.GetEvents()[1].GetNumericBefore()
				== TOptional<int64>(21)
			&& Resolution.GetEvents()[1].GetNumericAfter()
				== TOptional<int64>(20)
			&& Resolution.GetEvents()[1].GetNumericDelta()
				== TOptional<int64>(1));
	TestTrue(TEXT("Obedience refusal returns the exact single append"),
		IsReturnedResolutionAppendedExactlyOnce(*Engine, Resolution));
	TestEqual(TEXT("Obedience refusal increments state version once"),
		State.StateVersion, Before.StateVersion + 1);
	TestEqual(TEXT("Obedience refusal advances one queue action"),
		State.CurrentLockedActionIndex, Before.ActionIndex + 1);
	TestTrue(TEXT("Obedience refusal starts and finishes the action"),
		State.LockedActions.IsValidIndex(Before.ActionIndex)
			&& State.LockedActions[Before.ActionIndex].bStarted
			&& State.LockedActions[Before.ActionIndex].bFinished
			&& !State.LockedActions[Before.ActionIndex].bMoveCommitted);
	TestEqual(TEXT("Obedience refusal consumes one Trainer action"),
		Trainer != nullptr ? Trainer->ActionAllowance.RemainingActions : INDEX_NONE,
		Before.RemainingActions - 1);
	TestFalse(TEXT("Obedience refusal clears Charging"),
		HasActionStartVolatile(State, ActorId, FBattleVolatileRules::GetChargingId()));
	TestFalse(TEXT("Obedience refusal clears Fly semi-invulnerability"),
		HasActionStartVolatile(
			State,
			ActorId,
			FBattleVolatileRules::GetFlySemiInvulnerableId()));
	TestEqual(TEXT("Obedience refusal commits both charge-cleanup tokens"),
		State.NextTriggerReentrancyToken, Before.NextTriggerToken + 2);
	TestEqual(TEXT("Obedience refusal consumes no PP"),
		ObserveActionStartCheckpoint(*Engine, ActorId, TrainerId).TotalMovePP,
		Before.TotalMovePP);
	TestEqual(TEXT("Obedience refusal consumes no RNG"),
		State.Random->GetTrace().Num(), Before.RandomTraceCount);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E1ActionStartActorInvalidationTest,
	"PokemonSolarus.Battle.ADR0002.3E1.ActionStart.Success.ActorInvalidationReplacementBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E1ActionStartActorInvalidationTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	FAtomicWildScenario Scenario;
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	const FTrainerId TrainerId = MakeNumericId<FTrainerId>(PlayerTrainerValue);
	const FActiveSlotId ActorSlot = MakeActiveSlotId(
		EBattleSide::Player,
		EBattlePosition::Left);
	TUniquePtr<FBattleEngine> Engine;
	if (!TestTrue(TEXT("Actor-invalidation engine is created"),
		TryMakeSequenceEngine(Scenario, {}, Engine))
		|| !TestTrue(TEXT("Actor-invalidation turn is locked"),
			LockTurn(*Engine, PlayerLeftValue, EBattleActionKind::Fight))
		|| !TestTrue(TEXT("Player action is selected as the last queued start"),
			TryPrepareLastLockedAction(*Engine, ActorId))
		|| !TestTrue(TEXT("The last action actor is invalidated from its active slot"),
			TryClearActionStartActiveSlot(*Engine, ActorSlot)))
	{
		return false;
	}
	const FActionStartCheckpointObservation Before =
		ObserveActionStartCheckpoint(*Engine, ActorId, TrainerId);
	const FBattleResolution Resolution = Engine->BeginNextLockedAction();
	const FBattleEngineState& State =
		FBattleC09BWildFlowEngineFixture::GetState(*Engine);
	const FBattleTrainerState* Trainer = State.FindTrainer(TrainerId);

	TestTrue(TEXT("Actor invalidation is an accepted cancellation"),
		Resolution.WasAccepted());
	TestTrue(TEXT("Actor invalidation and replacement keep exact event order"),
		HasExactEventOrder(Resolution, {
			EBattleEventType::ActionCanceled,
			EBattleEventType::ActionCompleted,
			EBattleEventType::ReplacementRequired}));
	TestTrue(TEXT("Actor invalidation cancellation stays public and action-caused"),
		Resolution.GetEvents().Num() == 3
			&& Resolution.GetEvents()[0].GetCause()
				== EBattleEventCause::Action
			&& Resolution.GetEvents()[0].GetVisibility().Level
				== EBattleVisibilityLevel::Public);
	TestTrue(TEXT("Actor invalidation returns the exact single append"),
		IsReturnedResolutionAppendedExactlyOnce(*Engine, Resolution));
	TestEqual(TEXT("Actor invalidation increments state version once"),
		State.StateVersion, Before.StateVersion + 1);
	TestEqual(TEXT("Actor invalidation exhausts the queue"),
		State.CurrentLockedActionIndex, State.LockedActions.Num());
	TestTrue(TEXT("Actor invalidation finishes without starting the action"),
		State.LockedActions.IsValidIndex(Before.ActionIndex)
			&& !State.LockedActions[Before.ActionIndex].bStarted
			&& State.LockedActions[Before.ActionIndex].bFinished
			&& !State.LockedActions[Before.ActionIndex].bMoveCommitted);
	TestEqual(TEXT("Actor invalidation consumes one Trainer action"),
		Trainer != nullptr ? Trainer->ActionAllowance.RemainingActions : INDEX_NONE,
		Before.RemainingActions - 1);
	TestEqual(TEXT("Actor invalidation stages MandatoryReplacement"),
		State.Phase, EBattlePhase::MandatoryReplacement);
	TestTrue(TEXT("Actor invalidation stages one canonical replacement request"),
		State.PendingReplacements.Num() == 1
			&& State.PendingReplacements[0].TrainerId == TrainerId
			&& State.PendingReplacements[0].ActiveSlotId == ActorSlot
			&& State.PendingDecision.IsSet()
			&& State.PendingDecisionRequests.Num() == 1
			&& State.PendingDecisionRequests[0].GetRequestKind()
				== EBattleDecisionRequestKind::MandatoryReplacement
			&& State.PendingDecisionRequests[0].GetStateVersion()
				== State.StateVersion);
	TestTrue(TEXT("ReplacementRequired targets the exact empty slot"),
		Resolution.GetEvents().Num() == 3
			&& Resolution.GetEvents()[2].GetTargets().Num() == 1
			&& Resolution.GetEvents()[2].GetTargets()[0].TrainerId == TrainerId
			&& Resolution.GetEvents()[2].GetTargets()[0].ActiveSlotId == ActorSlot);
	TestEqual(TEXT("Actor invalidation consumes no PP"),
		ObserveActionStartCheckpoint(*Engine, ActorId, TrainerId).TotalMovePP,
		Before.TotalMovePP);
	TestEqual(TEXT("Actor invalidation consumes no RNG"),
		State.Random->GetTrace().Num(), Before.RandomTraceCount);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E1ActionStartCapturedTargetTest,
	"PokemonSolarus.Battle.ADR0002.3E1.ActionStart.Success.CapturedTargetCancellation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E1ActionStartCapturedTargetTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	FAtomicWildScenario Scenario = MakeAtomicCaptureScenario();
	Scenario.Format = EBattleFormat::Double;
	Scenario.PlayerLeftSpeed = 160;
	Scenario.PlayerRightSpeed = 150;
	Scenario.OpponentLeftSpeed = 100;
	Scenario.OpponentRightSpeed = 4;
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerRightValue);
	const FTrainerId TrainerId = MakeNumericId<FTrainerId>(PlayerTrainerValue);
	const FBattlerId TargetId = MakeNumericId<FBattlerId>(OpponentLeftValue);
	TUniquePtr<FBattleEngine> Engine;
	if (!TestTrue(TEXT("Captured-target engine is created"),
		TryMakeSequenceEngine(Scenario, {0, 0, 0, 0}, Engine))
		|| !TestTrue(TEXT("Capture-before-selected-target turn is locked"),
			LockCaptureThenTargetTurn(*Engine))
		|| !TestTrue(TEXT("The faster Capture action starts first"),
			BeginExpectedWildAction(
				*Engine,
				PlayerLeftValue,
				EBattleActionKind::Bag)))
	{
		return false;
	}
	const FBattleResolution Capture = Engine->ExecuteCurrentBagItem();
	const FBattleEngineState& CapturedState =
		FBattleC09BWildFlowEngineFixture::GetState(*Engine);
	const FBattleBattlerState* CapturedBeforeStart = CapturedState.FindBattler(TargetId);
	if (!TestTrue(TEXT("The first action successfully captures its target"),
		Capture.WasAccepted()
			&& HasEvent(Capture, EBattleEventType::Captured)
			&& CapturedBeforeStart != nullptr
			&& CapturedBeforeStart->bCaptured
			&& CapturedBeforeStart->bRemoved
			&& !FBattleC09BWildFlowEngineFixture::IsActive(*Engine, TargetId))
		|| !TestTrue(TEXT("The next locked action retains the captured selected target"),
			CapturedState.LockedActions.IsValidIndex(
				CapturedState.CurrentLockedActionIndex)
				&& CapturedState.LockedActions[CapturedState.CurrentLockedActionIndex]
					.Decision.GetActingBattlerId() == ActorId
				&& CapturedState.LockedActions[CapturedState.CurrentLockedActionIndex]
					.SelectedTargetBattlerId == TargetId))
	{
		return false;
	}
	const FActionStartCheckpointObservation Before =
		ObserveActionStartCheckpoint(*Engine, ActorId, TrainerId);
	const FBattleResolution Resolution = Engine->BeginNextLockedAction();
	const FBattleEngineState& State =
		FBattleC09BWildFlowEngineFixture::GetState(*Engine);
	const FBattleTrainerState* Trainer = State.FindTrainer(TrainerId);
	const FBattleBattlerState* CapturedTarget = State.FindBattler(TargetId);

	TestTrue(TEXT("Captured target produces an accepted cancellation"),
		Resolution.WasAccepted());
	TestTrue(TEXT("Captured-target cancellation keeps exact event order"),
		HasExactEventOrder(Resolution, {
			EBattleEventType::ActionCanceled,
			EBattleEventType::ActionCompleted}));
	TestTrue(TEXT("Captured-target cancellation returns the exact single append"),
		IsReturnedResolutionAppendedExactlyOnce(*Engine, Resolution));
	TestEqual(TEXT("Captured-target cancellation increments state version once"),
		State.StateVersion, Before.StateVersion + 1);
	TestEqual(TEXT("Captured-target cancellation advances one queue action"),
		State.CurrentLockedActionIndex, Before.ActionIndex + 1);
	TestEqual(TEXT("Captured-target cancellation remains Resolving"),
		State.Phase, EBattlePhase::Resolving);
	TestTrue(TEXT("Captured-target cancellation finishes without starting"),
		State.LockedActions.IsValidIndex(Before.ActionIndex)
			&& !State.LockedActions[Before.ActionIndex].bStarted
			&& State.LockedActions[Before.ActionIndex].bFinished
			&& !State.LockedActions[Before.ActionIndex].bMoveCommitted);
	TestEqual(TEXT("Captured-target cancellation consumes one Trainer action"),
		Trainer != nullptr ? Trainer->ActionAllowance.RemainingActions : INDEX_NONE,
		Before.RemainingActions - 1);
	TestTrue(TEXT("Captured-target facts remain captured, removed, and inactive"),
		CapturedTarget != nullptr
			&& CapturedTarget->bCaptured
			&& CapturedTarget->bRemoved
			&& !FBattleC09BWildFlowEngineFixture::IsActive(*Engine, TargetId));
	TestEqual(TEXT("Captured-target cancellation consumes no PP"),
		ObserveActionStartCheckpoint(*Engine, ActorId, TrainerId).TotalMovePP,
		Before.TotalMovePP);
	TestEqual(TEXT("Captured-target cancellation consumes no RNG"),
		State.Random->GetTrace().Num(), Before.RandomTraceCount);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E1ActionStartPreparationFailureTest,
	"PokemonSolarus.Battle.ADR0002.3E1.ActionStart.Failure.PreparationBeforePublication",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E1ActionStartPreparationFailureTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	FAtomicWildScenario Scenario;
	Scenario.PlayerLeftSpeed = 150;
	Scenario.bPlayerHasCanonicalHeldItem = true;
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	const FTrainerId TrainerId = MakeNumericId<FTrainerId>(PlayerTrainerValue);
	TUniquePtr<FBattleEngine> Engine;
	if (!TestTrue(TEXT("Preparation-failure engine is created"),
		TryMakeSequenceEngine(Scenario, {}, Engine))
		|| !TestTrue(TEXT("Preparation-failure action is locked first"),
			LockTurn(*Engine, PlayerLeftValue, EBattleActionKind::Fight))
		|| !TestTrue(TEXT("Preparation-failure Magic Room is seeded"),
			TrySeedActionStartMagicRoom(*Engine, ActorId)))
	{
		return false;
	}
	FBattleEngineState& MutableState =
		FBattleC09BWildFlowEngineFixture::GetMutableState(*Engine);
	MutableState.NextTriggerReentrancyToken = TNumericLimits<uint64>::Max() - 1;
	const FActionStartCheckpointObservation Before =
		ObserveActionStartCheckpoint(*Engine, ActorId, TrainerId);
	const FBattleResolution Rejected = Engine->BeginNextLockedAction();
	const bool bRejectedWithoutDelta = VerifyRejectedActionStartCheckpoint(
		*this,
		*Engine,
		ActorId,
		TrainerId,
		Before,
		Before.StateVersion,
		EBattleRejectionReason::CheckpointPreparationFailed,
		Rejected);
	const FBattleEngineState& State =
		FBattleC09BWildFlowEngineFixture::GetState(*Engine);
	const FBattleBattlerState* Actor = State.FindBattler(ActorId);
	TestTrue(TEXT("Preparation failure occurs after staged Magic Room dispatch"),
		Before.NextTriggerToken == TNumericLimits<uint64>::Max() - 1
			&& CountActionStartTriggerRegistrations(
				State,
				FBattleFieldSideConditionRules::GetMagicRoomId().GetDefinitionId()) > 0);
	TestTrue(TEXT("Preparation failure publishes no held-item suppression"),
		Actor != nullptr
			&& Actor->HeldItem.CurrentItemId == FBattleItemRules::GetLeftoversId()
			&& !Actor->HeldItem.bSuppressed
			&& !Actor->HeldItem.bConsumed);
	return bRejectedWithoutDelta;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E1ActionStartStaleIdentityTest,
	"PokemonSolarus.Battle.ADR0002.3E1.ActionStart.Failure.StaleIdentityAfterPreparation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E1ActionStartStaleIdentityTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	FAtomicWildScenario Scenario;
	Scenario.PlayerLeftSpeed = 150;
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	const FTrainerId TrainerId = MakeNumericId<FTrainerId>(PlayerTrainerValue);
	TUniquePtr<FBattleEngine> Engine;
	FActionStartStaleRandom* Random = nullptr;
	if (!TestTrue(TEXT("Stale-identity engine is created"),
		TryMakeActionStartStaleEngine(Scenario, Engine, Random))
		|| !TestNotNull(TEXT("Stale-identity random seam is retained"), Random)
		|| !TestTrue(TEXT("Stale-identity action is locked first"),
			LockTurn(*Engine, PlayerLeftValue, EBattleActionKind::Fight)))
	{
		return false;
	}
	const FActionStartCheckpointObservation Before =
		ObserveActionStartCheckpoint(*Engine, ActorId, TrainerId);
	Random->ArmAfterTraceRead(
		7,
		[EnginePtr = Engine.Get()]()
		{
			FBattleC09BWildFlowEngineFixture::AdvanceStateVersion(*EnginePtr);
		});
	const FBattleResolution Rejected = Engine->BeginNextLockedAction();
	const int32 TraceReads = Random->GetReadsSinceArm();
	const bool bInjected = Random->WasInjected();
	Random->Disarm();
	TestTrue(TEXT("Stale identity is injected at the final post-plan recheck"),
		bInjected && TraceReads == 7);
	return VerifyRejectedActionStartCheckpoint(
		*this,
		*Engine,
		ActorId,
		TrainerId,
		Before,
		Before.StateVersion + 1,
		EBattleRejectionReason::StaleCheckpointIdentity,
		Rejected);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E1ActionStartPlanFailureTest,
	"PokemonSolarus.Battle.ADR0002.3E1.ActionStart.Failure.ResolutionPlanStaging",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E1ActionStartPlanFailureTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	FAtomicWildScenario Scenario;
	Scenario.PlayerLeftSpeed = 150;
	Scenario.bPlayerSubjectToObedience = true;
	Scenario.PlayerReferenceLevel = 21;
	Scenario.PlayerBadgeCount = 0;
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	const FTrainerId TrainerId = MakeNumericId<FTrainerId>(PlayerTrainerValue);
	TUniquePtr<FBattleEngine> Engine;
	if (!TestTrue(TEXT("Plan-failure engine is created"),
		TryMakeSequenceEngine(Scenario, {}, Engine))
		|| !TestTrue(TEXT("Plan-failure refusal action is locked first"),
			LockTurn(*Engine, PlayerLeftValue, EBattleActionKind::Fight)))
	{
		return false;
	}
	FBattleEngineState& MutableState =
		FBattleC09BWildFlowEngineFixture::GetMutableState(*Engine);
	MutableState.NextEventOrdinal = TNumericLimits<uint64>::Max() - 2;
	const FActionStartCheckpointObservation Before =
		ObserveActionStartCheckpoint(*Engine, ActorId, TrainerId);
	const FBattleResolution Rejected = Engine->BeginNextLockedAction();
	TestEqual(TEXT("Plan failure starts at the bounded near-overflow ordinal"),
		Before.NextEventOrdinal, TNumericLimits<uint64>::Max() - 2);
	return VerifyRejectedActionStartCheckpoint(
		*this,
		*Engine,
		ActorId,
		TrainerId,
		Before,
		Before.StateVersion,
		EBattleRejectionReason::CheckpointPreparationFailed,
		Rejected);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E2VoluntarySwitchFullEntryChainTest,
	"PokemonSolarus.Battle.ADR0002.3E2.VoluntarySwitch.Success.FullEntryChain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E2VoluntarySwitchFullEntryChainTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	TUniquePtr<FBattleEngine> Engine;
	if (!TestTrue(TEXT("Full-chain Switch engine is created"),
			TryMakeSequenceEngine(
				MakeAtomicVoluntarySwitchScenario(
					FBattleItemRules::GetSitrusBerryId(),
					FBattleAbilityRules::GetIntimidateId(),
					110),
				{},
				Engine))
		|| !TestTrue(TEXT("Full-chain Switch is locked"),
			TryPrepareAtomicVoluntarySwitch(*Engine))
		|| !TestTrue(TEXT("Full-chain outgoing transients are seeded"),
			TrySeedAtomicSwitchOutgoingTransients(*Engine))
		|| !TestTrue(TEXT("Full-chain Switch is started"),
			TryBeginAtomicVoluntarySwitch(*Engine))
		|| !TestTrue(TEXT("Full-chain Stealth Rock is seeded"),
			TrySeedAtomicSwitchHazard(
				*Engine,
				FBattleFieldSideConditionRules::GetStealthRockId())))
	{
		return false;
	}

	const FAtomicSwitchCheckpointObservation Before =
		ObserveAtomicSwitchCheckpoint(*Engine);
	const FBattleResolution Resolution = Engine->ExecuteCurrentSwitch();
	const FBattleEngineState& State =
		FBattleC09BWildFlowEngineFixture::GetState(*Engine);
	const FBattleBattlerState* Outgoing = State.FindBattler(
		MakeNumericId<FBattlerId>(PlayerLeftValue));
	const FBattleBattlerState* Incoming = State.FindBattler(
		MakeNumericId<FBattlerId>(PlayerReserveValue));
	const FBattleBattlerState* Opponent = State.FindBattler(
		MakeNumericId<FBattlerId>(OpponentLeftValue));
	const FBattleActivePositionState* PlayerActive = State.FindActivePosition(
		MakeActiveSlotId(EBattleSide::Player, EBattlePosition::Left));
	int32 OpponentAttackStage = 0;
	const bool bOpponentStageRead = Opponent != nullptr
		&& Opponent->Stages.TryGetStage(EBattleStat::Attack, OpponentAttackStage);

	TestTrue(TEXT("Full entry chain is accepted"), Resolution.WasAccepted());
	TestTrue(TEXT("Full entry chain has exact event order"),
		HasExactEventOrder(
			Resolution,
			{
				EBattleEventType::LeftActiveSlot,
				EBattleEventType::SwitchTransientStateCleared,
				EBattleEventType::EnteredActiveSlot,
				EBattleEventType::Switched,
				EBattleEventType::Damage,
				EBattleEventType::HPChanged,
				EBattleEventType::ItemActivated,
				EBattleEventType::ItemConsumed,
				EBattleEventType::Healing,
				EBattleEventType::HPChanged,
				EBattleEventType::AbilityActivated,
				EBattleEventType::StatStageChanged,
				EBattleEventType::ActionCompleted
			}));
	TestTrue(TEXT("Full entry chain is appended exactly once"),
		IsReturnedResolutionAppendedExactlyOnce(*Engine, Resolution));
	TestEqual(TEXT("Full entry chain advances one version"),
		State.StateVersion, Before.StateVersion + 1);
	TestEqual(TEXT("Full entry chain consumes no gameplay RNG"),
		State.Random->GetTrace().Num(), Before.RandomTraceCount);
	TestTrue(TEXT("Outgoing battler is inactive and fully cleaned"),
		Outgoing != nullptr
			&& PlayerActive != nullptr
			&& PlayerActive->BattlerId == MakeNumericId<FBattlerId>(PlayerReserveValue)
			&& Outgoing->Volatiles.IsEmpty()
			&& !Outgoing->LastMoveId.IsValid()
			&& !Outgoing->bAbilitySuppressed
			&& !Outgoing->EnteredActiveOnTurnId.IsValid());
	int32 OutgoingAttackStage = 1;
	TestTrue(TEXT("Outgoing stages reset at commit"),
		Outgoing != nullptr
			&& Outgoing->Stages.TryGetStage(EBattleStat::Attack, OutgoingAttackStage)
			&& OutgoingAttackStage == 0);
	TestTrue(TEXT("Incoming hazard, item, and entry Ability all commit"),
		Incoming != nullptr
			&& Incoming->CurrentHP == 135
			&& Incoming->HeldItem.bConsumed
			&& !Incoming->HeldItem.CurrentItemId.IsValid()
			&& Incoming->EnteredActiveOnTurnId == State.TurnId
			&& bOpponentStageRead
			&& OpponentAttackStage == -1);
	TestTrue(TEXT("Incoming Sitrus and Intimidate reveal facts commit"),
		IsAtomicSwitchDefinitionRevealed(
			State,
			MakeNumericId<FBattlerId>(PlayerReserveValue),
			true)
			&& Incoming != nullptr
			&& Incoming->HeldItem.bRevealed);
	TestEqual(TEXT("Full entry chain reaches EndOfTurn"),
		State.Phase, EBattlePhase::EndOfTurn);
	TestEqual(TEXT("Full entry chain advances the action cursor"),
		State.CurrentLockedActionIndex, 1);
	TestTrue(TEXT("Full entry chain marks its exact action finished"),
		State.LockedActions.Num() == 1 && State.LockedActions[0].bFinished);
	const FBattleReplayRecord Replay = Engine->ExportReplayRecord();
	TestEqual(TEXT("Full entry chain replay keeps schema 6"),
		Replay.GetSchemaVersion(), FBattleReplayRecord::CurrentSchemaVersion);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E2VoluntarySwitchAirBalloonTest,
	"PokemonSolarus.Battle.ADR0002.3E2.VoluntarySwitch.Success.AirBalloonReveal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E2VoluntarySwitchAirBalloonTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	TUniquePtr<FBattleEngine> Engine;
	if (!TestTrue(TEXT("Air Balloon Switch engine is created"),
			TryMakeSequenceEngine(
				MakeAtomicVoluntarySwitchScenario(FBattleItemRules::GetAirBalloonId()),
				{},
				Engine))
		|| !TestTrue(TEXT("Air Balloon Switch is locked"),
			TryPrepareAtomicVoluntarySwitch(*Engine))
		|| !TestTrue(TEXT("Air Balloon Switch is started"),
			TryBeginAtomicVoluntarySwitch(*Engine)))
	{
		return false;
	}
	const FAtomicSwitchCheckpointObservation Before =
		ObserveAtomicSwitchCheckpoint(*Engine);
	const FBattleResolution Resolution = Engine->ExecuteCurrentSwitch();
	const FBattleEngineState& State =
		FBattleC09BWildFlowEngineFixture::GetState(*Engine);
	const FBattleBattlerState* Incoming = State.FindBattler(
		MakeNumericId<FBattlerId>(PlayerReserveValue));
	TestTrue(TEXT("Air Balloon Switch is accepted"), Resolution.WasAccepted());
	TestTrue(TEXT("Air Balloon Switch has exact event order"),
		HasExactEventOrder(
			Resolution,
			{
				EBattleEventType::LeftActiveSlot,
				EBattleEventType::SwitchTransientStateCleared,
				EBattleEventType::EnteredActiveSlot,
				EBattleEventType::Switched,
				EBattleEventType::ItemActivated,
				EBattleEventType::ActionCompleted
			}));
	TestTrue(TEXT("Air Balloon Switch is appended exactly once"),
		IsReturnedResolutionAppendedExactlyOnce(*Engine, Resolution));
	TestTrue(TEXT("Air Balloon reveal commits to mirror and tracker"),
		Incoming != nullptr
			&& Incoming->HeldItem.CurrentItemId == FBattleItemRules::GetAirBalloonId()
			&& Incoming->HeldItem.bRevealed
			&& IsAtomicSwitchDefinitionRevealed(
				State,
				Incoming->BattlerId,
				false));
	TestEqual(TEXT("Air Balloon Switch advances one version"),
		State.StateVersion, Before.StateVersion + 1);
	TestEqual(TEXT("Air Balloon Switch consumes no RNG"),
		State.Random->GetTrace().Num(), Before.RandomTraceCount);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E2VoluntarySwitchLethalHazardTest,
	"PokemonSolarus.Battle.ADR0002.3E2.VoluntarySwitch.Success.LethalHazardReplacementBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E2VoluntarySwitchLethalHazardTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	TUniquePtr<FBattleEngine> Engine;
	if (!TestTrue(TEXT("Lethal-hazard Switch engine is created"),
			TryMakeSequenceEngine(MakeAtomicVoluntarySwitchScenario(FItemId(),
				FBattleAbilityRules::GetBlazeId(), 10), {}, Engine))
		|| !TestTrue(TEXT("Lethal-hazard Switch is locked"),
			TryPrepareAtomicVoluntarySwitch(*Engine))
		|| !TestTrue(TEXT("Lethal-hazard Switch is started"),
			TryBeginAtomicVoluntarySwitch(*Engine))
		|| !TestTrue(TEXT("Lethal Stealth Rock is seeded"),
			TrySeedAtomicSwitchHazard(
				*Engine,
				FBattleFieldSideConditionRules::GetStealthRockId())))
	{
		return false;
	}
	const FAtomicSwitchCheckpointObservation Before =
		ObserveAtomicSwitchCheckpoint(*Engine);
	const FBattleResolution Resolution = Engine->ExecuteCurrentSwitch();
	const FBattleEngineState& State =
		FBattleC09BWildFlowEngineFixture::GetState(*Engine);
	const FBattleBattlerState* Incoming = State.FindBattler(
		MakeNumericId<FBattlerId>(PlayerReserveValue));
	const FBattleActivePositionState* PlayerActive = State.FindActivePosition(
		MakeActiveSlotId(EBattleSide::Player, EBattlePosition::Left));
	TestTrue(TEXT("Lethal-hazard Switch is accepted"), Resolution.WasAccepted());
	TestTrue(TEXT("Lethal-hazard Switch has exact event and boundary order"),
		HasExactEventOrder(
			Resolution,
			{
				EBattleEventType::LeftActiveSlot,
				EBattleEventType::SwitchTransientStateCleared,
				EBattleEventType::EnteredActiveSlot,
				EBattleEventType::Switched,
				EBattleEventType::Damage,
				EBattleEventType::HPChanged,
				EBattleEventType::Fainted,
				EBattleEventType::LeftActiveSlot,
				EBattleEventType::Removed,
				EBattleEventType::ActionCompleted,
				EBattleEventType::ReplacementRequired
			}));
	TestTrue(TEXT("Lethal-hazard Switch is appended exactly once"),
		IsReturnedResolutionAppendedExactlyOnce(*Engine, Resolution));
	TestTrue(TEXT("Lethal incoming battler fully faints and leaves its slot"),
		Incoming != nullptr
			&& Incoming->CurrentHP == 0
			&& Incoming->bFainted
			&& Incoming->bRemoved
			&& PlayerActive != nullptr
			&& !PlayerActive->BattlerId.IsValid()
			&& !PlayerActive->TrainerId.IsValid());
	TestEqual(TEXT("Lethal hazard reaches mandatory replacement"),
		State.Phase, EBattlePhase::MandatoryReplacement);
	TestTrue(TEXT("Lethal hazard prepares one complete replacement checkpoint"),
		State.PendingReplacements.Num() == 1
			&& State.PendingDecisionRequests.Num() == 1
			&& State.PendingDecision.IsSet()
			&& State.PendingReplacements[0].TrainerId
				== MakeNumericId<FTrainerId>(PlayerTrainerValue));
	TestEqual(TEXT("Lethal hazard advances one version"),
		State.StateVersion, Before.StateVersion + 1);
	TestEqual(TEXT("Lethal hazard consumes no RNG"),
		State.Random->GetTrace().Num(), Before.RandomTraceCount);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E2VoluntarySwitchEntryItemFailureTest,
	"PokemonSolarus.Battle.ADR0002.3E2.VoluntarySwitch.Failure.EntryItemReveal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E2VoluntarySwitchEntryItemFailureTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	return RunAtomicVoluntarySwitchFailureFamily(
		*this,
		EAtomicSwitchFailureFamily::EntryItemReveal);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E2VoluntarySwitchEntryHazardFailureTest,
	"PokemonSolarus.Battle.ADR0002.3E2.VoluntarySwitch.Failure.EntryHazard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E2VoluntarySwitchEntryHazardFailureTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	return RunAtomicVoluntarySwitchFailureFamily(
		*this,
		EAtomicSwitchFailureFamily::EntryHazard);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E2VoluntarySwitchImmediateItemFailureTest,
	"PokemonSolarus.Battle.ADR0002.3E2.VoluntarySwitch.Failure.ImmediateHeldItem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E2VoluntarySwitchImmediateItemFailureTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	return RunAtomicVoluntarySwitchFailureFamily(
		*this,
		EAtomicSwitchFailureFamily::ImmediateHeldItem);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E2VoluntarySwitchEntryAbilityFailureTest,
	"PokemonSolarus.Battle.ADR0002.3E2.VoluntarySwitch.Failure.EntryAbility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E2VoluntarySwitchEntryAbilityFailureTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	return RunAtomicVoluntarySwitchFailureFamily(
		*this,
		EAtomicSwitchFailureFamily::EntryAbility);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E2VoluntarySwitchStaleIdentityTest,
	"PokemonSolarus.Battle.ADR0002.3E2.VoluntarySwitch.Failure.StaleIdentityBeforeCommit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E2VoluntarySwitchStaleIdentityTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	TUniquePtr<FBattleEngine> Engine;
	FActionStartStaleRandom* Random = nullptr;
	if (!TestTrue(TEXT("Stale Switch engine is created"),
			TryMakeActionStartStaleEngine(
				MakeAtomicVoluntarySwitchScenario(),
				Engine,
				Random))
		|| !TestNotNull(TEXT("Stale Switch random seam is retained"), Random)
		|| !TestTrue(TEXT("Stale Switch is locked"),
			TryPrepareAtomicVoluntarySwitch(*Engine))
		|| !TestTrue(TEXT("Stale Switch outgoing transients are seeded"),
			TrySeedAtomicSwitchOutgoingTransients(*Engine))
		|| !TestTrue(TEXT("Stale Switch is started"),
			TryBeginAtomicVoluntarySwitch(*Engine)))
	{
		return false;
	}
	const FAtomicSwitchCheckpointObservation Before =
		ObserveAtomicSwitchCheckpoint(*Engine);
	check(Random != nullptr);
	Random->ArmAfterTraceRead(
		2,
		[EnginePtr = Engine.Get()]()
		{
			FBattleC09BWildFlowEngineFixture::AdvanceStateVersion(*EnginePtr);
		});
	const FBattleResolution Rejected = Engine->ExecuteCurrentSwitch();
	const int32 TraceReads = Random->GetReadsSinceArm();
	const bool bInjected = Random->WasInjected();
	Random->Disarm();
	TestTrue(TEXT("Stale identity is injected only at the final recheck"),
		bInjected && TraceReads == 2);
	return VerifyRejectedAtomicVoluntarySwitch(
		*this,
		*Engine,
		Before,
		Before.StateVersion + 1,
		EBattleRejectionReason::StaleCheckpointIdentity,
		Rejected);
}

#endif
