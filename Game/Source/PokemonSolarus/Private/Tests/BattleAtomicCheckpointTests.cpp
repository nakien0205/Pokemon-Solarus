#if WITH_DEV_AUTOMATION_TESTS

#include "Battle/BattleAbility.h"
#include "Battle/BattleBagItem.h"
#include "Battle/BattleCapture.h"
#include "Battle/BattleEngine.h"
#include "Battle/BattleReplay.h"
#include "Battle/BattleState.h"
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
	constexpr uint64 OpponentLeftValue = 21;
	constexpr uint64 OpponentRightValue = 22;

	const TCHAR* PlayerSpeciesName = TEXT("Species.ADR0002.3D1.Player");
	const TCHAR* WildSpeciesName = TEXT("Species.ADR0002.3D1.Wild");
	const TCHAR* ProbeMoveName = TEXT("Move.ADR0002.3D1.Probe");
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
		return Species;
	}

	FBattleDefinitionCatalog MakeCatalog(const FAtomicWildScenario& Scenario)
	{
		FBattleDefinitionCatalogInput Input;
		Input.TypeChartEntries = MakeNeutralTypeChart();
		Input.Moves.Add(MakeProbeMove());
		Input.Abilities.Add({FBattleAbilityRules::GetBlazeId()});
		Input.Items.Add({FBattleBagItemRules::GetPokeBallId(), EBattleItemKind::Capture});
		Input.Items.Add({
			MakeDefinitionId<FItemId>(CaptureHeldItemName),
			EBattleItemKind::Held});
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
		const FItemId CurrentHeldItemId = FItemId())
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
		Entry.AbilityId = FBattleAbilityRules::GetBlazeId();
		Entry.OriginalHeldItemId = OriginalHeldItemId;
		Entry.CurrentHeldItemId = CurrentHeldItemId;
		Entry.CaptureClassification = EBattleCaptureSpeciesClassification::Normal;
		Entry.Moves.Add({0, MakeDefinitionId<FMoveId>(ProbeMoveName), 20, 20});
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
			Scenario.PlayerLeftSpeed));
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
		const EBattleActionKind ActionKind)
	{
		FBattleDecision Decision;
		bool bCreated = false;
		if (ActionKind == EBattleActionKind::Fight)
		{
			bCreated = FBattleDecision::TryCreateAutomaticallyTargetedFight(
				Request.GetStateVersion(),
				Request.GetDecisionOwnerTrainerId(),
				Request.GetActingBattlerId(),
				MakeDefinitionId<FMoveId>(ProbeMoveName),
				Decision);
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
		const EBattleActionKind SpecialAction)
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
				Decisions.Add(MakeDecision(Request, Choice));
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

#endif
