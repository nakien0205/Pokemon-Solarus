#if WITH_DEV_AUTOMATION_TESTS

#include "Battle/BattleEngine.h"
#include "Battle/BattleEffectExecutor.h"
#include "Battle/BattleReplay.h"
#include "BattleTestFactories.h"
#include "BattleTestRandom.h"
#include "Misc/AutomationTest.h"

namespace BattleFaintOutcomeTests
{
	using BattleTest::MakeActiveSlotId;
	using BattleTest::MakeDefinitionId;
	using BattleTest::MakeNumericId;
	using BattleTest::MakePartySlotId;
	using FC05CStrictRandom = BattleTest::FStrictBattleRandom;

	constexpr uint64 BattleValue = 5506;
	constexpr uint64 PlayerTrainerValue = 1;
	constexpr uint64 OpponentTrainerValue = 2;
	constexpr uint64 PartnerTrainerValue = 3;
	constexpr uint64 PlayerLeftBattlerValue = 11;
	constexpr uint64 PlayerRightBattlerValue = 12;
	constexpr uint64 PartnerBattlerValue = 31;
	constexpr uint64 OpponentLeftBattlerValue = 21;
	constexpr uint64 OpponentRightBattlerValue = 22;
	constexpr uint64 OpponentReserveBattlerValue = 23;

	const TCHAR* MoveName = TEXT("Move.C05C.Test");
	const TCHAR* SpeciesName = TEXT("Species.C05C.Test");
	const TCHAR* AbilityName = TEXT("Ability.C05C.Test");
	const TCHAR* MajorConditionName = TEXT("Condition.C05C.Major");
	const TCHAR* VolatileConditionName = TEXT("Condition.C05C.Volatile");

	enum class EC05CPostAction : uint8
	{
		None = 0,
		FinishNextQueuedAction = 1,
		ApplyCheckpointTwice = 2,
		SubmitTerminalDecision = 3,
		FinishRemainingQueue = 4
	};

	struct FC05CBattlerFixture
	{
		uint64 TrainerValue = 0;
		uint64 BattlerValue = 0;
		int32 PartyIndex = 0;
		int32 CurrentHP = 200;
		int32 Speed = 100;
		int32 CurrentPP = 3;
		bool bActive = false;
		EBattleSide Side = EBattleSide::Player;
		EBattlePosition Position = EBattlePosition::Left;
	};

	struct FC05CScenario
	{
		EBattleFormat Format = EBattleFormat::Single;
		EBattleTargetClass TargetClass = EBattleTargetClass::SelectedOpponent;
		bool bFixedRecoil = false;
		uint64 ExpectedFirstActor = 0;
		int32 ExpectedDamageDraws = 0;
		EC05CPostAction PostAction = EC05CPostAction::None;
		TArray<FC05CBattlerFixture> Battlers;
	};

	struct FC05CEvidence
	{
		bool bSucceeded = false;
		bool bRandomExact = false;
		bool bFollowupAccepted = false;
		bool bFirstRefreshAccepted = false;
		bool bSecondRefreshRejected = false;
		EBattleRejectionReason SecondRefreshReason = EBattleRejectionReason::None;
		bool bTerminalDecisionRejected = false;
		EBattleRejectionReason TerminalDecisionReason = EBattleRejectionReason::None;
		uint64 StateVersionBeforeTerminalRejection = 0;
		uint64 StateVersionAfterTerminalRejection = 0;
		uint64 CheckpointOrdinal = 0;
		EBattlePhase Phase = EBattlePhase::Setup;
		EBattleOutcome Outcome = EBattleOutcome::InProgress;
		EBattleOutcomeCause OutcomeCause = EBattleOutcomeCause::None;
		FBattleSnapshot Snapshot;
		TArray<FBattleEvent> Events;
		TArray<FString> EventOrder;
		TArray<FBattleRandomDraw> RandomTrace;
		TArray<uint8> ReplayBytes;
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

	FBattleMoveDefinition MakeMove(const FC05CScenario& Scenario)
	{
		FBattleMoveDefinition Move;
		Move.Id = MakeDefinitionId<FMoveId>(MoveName);
		Move.Type = EPokemonType::Normal;
		Move.Category = EBattleMoveCategory::Physical;
		Move.Power = 40;
		Move.bAlwaysHits = true;
		Move.Accuracy = 0;
		Move.bUsesPP = true;
		Move.BasePP = 10;
		Move.bAllowsPPBoosts = true;
		Move.Priority = 0;
		Move.TargetClass = Scenario.TargetClass;
		Move.Flags = EBattleMoveFlags::NeverCritical;
		if (Scenario.bFixedRecoil)
		{
			FBattleMoveEffectDescriptor MajorStatus = MakeEffect(
				0,
				EBattleMoveEffectKind::ApplyCondition,
				EBattleEffectTarget::User);
			MajorStatus.ConditionId = MakeDefinitionId<FConditionId>(MajorConditionName);
			Move.Effects.Add(MajorStatus);

			FBattleMoveEffectDescriptor Volatile = MakeEffect(
				1,
				EBattleMoveEffectKind::ApplyCondition,
				EBattleEffectTarget::User);
			Volatile.ConditionId = MakeDefinitionId<FConditionId>(VolatileConditionName);
			Move.Effects.Add(Volatile);

			FBattleMoveEffectDescriptor Stage = MakeEffect(
				2,
				EBattleMoveEffectKind::ModifyStatStage,
				EBattleEffectTarget::User);
			Stage.Stat = EBattleStat::Attack;
			Stage.MagnitudeNumerator = 2;
			Move.Effects.Add(Stage);
		}
		Move.Effects.Add(MakeEffect(
			Scenario.bFixedRecoil ? 3 : 0,
			EBattleMoveEffectKind::Damage,
			EBattleEffectTarget::ResolvedTarget));
		if (Scenario.bFixedRecoil)
		{
			FBattleMoveEffectDescriptor Recoil = MakeEffect(
				4,
				EBattleMoveEffectKind::Recoil,
				EBattleEffectTarget::User);
			Recoil.MagnitudeNumerator = 1;
			Recoil.MagnitudeDenominator = 1;
			Move.Effects.Add(Recoil);
		}
		return Move;
	}

	TArray<FBattleTypeChartEntry> MakeNeutralTypeChart()
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

	FBattleDefinitionCatalog MakeCatalog(const FC05CScenario& Scenario)
	{
		FBattleDefinitionCatalogInput Input;
		Input.TypeChartEntries = MakeNeutralTypeChart();
		Input.Moves.Add(MakeMove(Scenario));
		Input.Abilities.Add({MakeDefinitionId<FAbilityId>(AbilityName)});
		Input.Conditions.Add(
			{MakeDefinitionId<FConditionId>(MajorConditionName), EBattleConditionKind::MajorStatus});
		Input.Conditions.Add(
			{MakeDefinitionId<FConditionId>(VolatileConditionName), EBattleConditionKind::Volatile});

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
				? TEXT("Selector.C05C.Player")
				: Role == EBattleTrainerRole::Partner
					? TEXT("Selector.C05C.Partner")
					: TEXT("Selector.C05C.Opponent"));
		return Trainer;
	}

	FBattlePartyEntrySetup MakePartyEntry(const FC05CBattlerFixture& Fixture)
	{
		FBattlePartyEntrySetup Entry;
		Entry.TrainerId = MakeNumericId<FTrainerId>(Fixture.TrainerValue);
		Entry.BattlerId = MakeNumericId<FBattlerId>(Fixture.BattlerValue);
		Entry.SourcePokemonId = MakeNumericId<FSourcePokemonId>(1000 + Fixture.BattlerValue);
		Entry.PartySlotId = MakePartySlotId(Fixture.PartyIndex);
		Entry.SpeciesFormId = MakeDefinitionId<FSpeciesFormId>(SpeciesName);
		Entry.Level = 50;
		Entry.Stats = {200, 100, 100, 100, 100, Fixture.Speed};
		Entry.CurrentHP = Fixture.CurrentHP;
		Entry.AbilityId = MakeDefinitionId<FAbilityId>(AbilityName);
		Entry.Moves.Add(
			{
				0,
				MakeDefinitionId<FMoveId>(MoveName),
				Fixture.CurrentPP,
				10
			});
		return Entry;
	}

	FBattleSetup MakeSetup(const FC05CScenario& Scenario)
	{
		FBattleSetupInput Input;
		Input.BattleId = MakeNumericId<FBattleId>(BattleValue);
		Input.SettingsReference =
			{MakeDefinitionId<FDefinitionId>(TEXT("Settings.C05C")), 1};
		Input.CatalogReference =
			{MakeDefinitionId<FDefinitionId>(TEXT("Catalog.C05C")), 1};
		Input.EncounterKind = EBattleEncounterKind::Trainer;
		Input.Format = Scenario.Format;
		Input.CaptureCapacity = {2, 100};
		Input.Policies.bBagAllowed = false;
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
		if (Scenario.Format == EBattleFormat::PartnerDouble)
		{
			Input.Trainers.Add(MakeTrainer(
				PartnerTrainerValue,
				EBattleSide::Player,
				EBattleTrainerRole::Partner,
				EBattleDecisionController::PartnerAI));
		}

		for (const FC05CBattlerFixture& Fixture : Scenario.Battlers)
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

		FBattleSetup Setup;
		EBattleSetupValidationError Error = EBattleSetupValidationError::None;
		const bool bCreated = FBattleSetup::TryCreate(Input, Setup, Error);
		check(bCreated);
		return Setup;
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

	bool LockAllFights(FBattleEngine& Engine)
	{
		FBattleRejection Rejection;
		if (!Engine.TryBeginActionDecisionSequence(Rejection))
		{
			return false;
		}

		int32 Guard = 0;
		const FMoveId MoveId = MakeDefinitionId<FMoveId>(MoveName);
		while (!Engine.GetPendingDecisionRequests().IsEmpty() && Guard++ < 8)
		{
			const TArray<FBattleDecisionRequest> Requests = Engine.GetPendingDecisionRequests();
			TArray<FBattleDecision> Decisions;
			for (const FBattleDecisionRequest& Request : Requests)
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
					const FBattleMoveTargetOption* Target = Request.GetLegalMoveTargets().FindByPredicate(
						[MoveId](const FBattleMoveTargetOption& Option)
						{
							return Option.MoveId == MoveId;
						});
					bCreated = Target != nullptr && FBattleDecision::TryCreateFight(
						Request.GetStateVersion(),
						Request.GetDecisionOwnerTrainerId(),
						Request.GetActingBattlerId(),
						MoveId,
						Target->ActiveSlotId,
						Decision);
				}
				if (!bCreated)
				{
					return false;
				}
				Decisions.Add(Decision);
			}

			if (!Engine.SubmitDecisionBatch(
				MakeBatch(Requests, MoveTemp(Decisions))).WasAccepted())
			{
				return false;
			}
		}
		return Guard < 8 && Engine.GetSnapshot().GetPhase() == EBattlePhase::Locked;
	}

	bool FinishRemainingQueue(FBattleEngine& Engine)
	{
		int32 Guard = 0;
		while (Engine.GetSnapshot().GetPhase() == EBattlePhase::Resolving && Guard++ < 8)
		{
			if (!Engine.BeginNextLockedAction().WasAccepted())
			{
				return false;
			}
			if (!Engine.GetCurrentLockedAction().IsSet())
			{
				continue;
			}
			if (!Engine.CommitCurrentMoveAfterPreMoveGates().WasAccepted())
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
		return Guard < 8 && Engine.GetSnapshot().GetPhase() != EBattlePhase::Resolving;
	}

	FString EventSignature(const FBattleEvent& Event)
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
		return FString::Printf(
			TEXT("%u:%u:%llu:%llu:%llu:%llu:%u:%u:%u:%s"),
			static_cast<uint8>(Event.GetType()),
			static_cast<uint8>(Event.GetCause()),
			Event.GetActionId().IsValid() ? Event.GetActionId().GetValue() : 0,
			Event.GetSource().BattlerId.IsValid() ? Event.GetSource().BattlerId.GetValue() : 0,
			Event.GetSimultaneousGroupId().IsSet()
				? Event.GetSimultaneousGroupId().GetValue()
				: 0,
			Event.GetEventOrdinal(),
			Event.GetHitIndex().IsSet() ? Event.GetHitIndex().GetValue() : 0,
			Event.GetHitCount().IsSet() ? Event.GetHitCount().GetValue() : 0,
			static_cast<uint8>(Event.GetOutcomeCause()),
			*Targets);
	}

	void GatherEvidence(FBattleEngine& Engine, FC05CEvidence& Evidence)
	{
		Evidence.Snapshot = Engine.GetSnapshot();
		Evidence.Phase = Evidence.Snapshot.GetPhase();
		Evidence.Outcome = Evidence.Snapshot.GetOutcome();
		Evidence.OutcomeCause = Evidence.Snapshot.GetOutcomeCause();
		Evidence.RandomTrace = Engine.ExportRandomTrace();
		const FBattleReplayRecord Record = Engine.ExportReplayRecord();
		for (const FBattleResolution& Resolution : Record.GetResolutions())
		{
			for (const FBattleEvent& Event : Resolution.GetEvents())
			{
				Evidence.Events.Add(Event);
				Evidence.EventOrder.Add(EventSignature(Event));
			}
		}
		FBattleRejection Rejection;
		Evidence.bSucceeded &= FBattleReplaySerializer::TrySerializeCanonical(
			Record,
			Evidence.ReplayBytes,
			Rejection);
	}

	FC05CEvidence RunScenario(const FC05CScenario& Scenario)
	{
		FC05CEvidence Evidence;
		TUniquePtr<FC05CStrictRandom> Random = MakeUnique<FC05CStrictRandom>(
			BattleTest::MakeRepeatedExpectedRandomDraws(
				Scenario.ExpectedDamageDraws,
				0,
				15,
				0,
				FBattleEffectExecutor::GetDamageRandomRulePurpose()));
		FC05CStrictRandom* RandomView = Random.Get();
		TUniquePtr<IBattleRandom> RandomOwner = MoveTemp(Random);
		TUniquePtr<FBattleEngine> Engine;
		FBattleRejection Rejection;
		Evidence.bSucceeded = FBattleEngine::TryCreate(
			MakeSetup(Scenario),
			MakeCatalog(Scenario),
			MoveTemp(RandomOwner),
			Engine,
			Rejection);
		if (!Evidence.bSucceeded || !Engine.IsValid())
		{
			return Evidence;
		}

		Evidence.bSucceeded &= LockAllFights(*Engine);
		const TArray<FBattleLockedAction> LockedActions = Engine->GetLockedActions();
		Evidence.bSucceeded &= !LockedActions.IsEmpty()
			&& LockedActions[0].Decision.GetActingBattlerId()
				== MakeNumericId<FBattlerId>(Scenario.ExpectedFirstActor);
		if (!Evidence.bSucceeded)
		{
			GatherEvidence(*Engine, Evidence);
			return Evidence;
		}

		const FBattleDecision TerminalDecision = LockedActions[0].Decision;
		Evidence.bSucceeded &= Engine->BeginNextLockedAction().WasAccepted();
		Evidence.bSucceeded &= Engine->CommitCurrentMoveAfterPreMoveGates().WasAccepted();
		Evidence.bSucceeded &= Engine->ResolveCurrentMoveTargets().WasAccepted();
		const FBattleResolution Effects = Engine->ExecuteCurrentMoveEffects();
		Evidence.bSucceeded &= Effects.WasAccepted();

		if (Scenario.PostAction == EC05CPostAction::FinishNextQueuedAction)
		{
			const FBattleResolution Followup = Engine->BeginNextLockedAction();
			Evidence.bFollowupAccepted = Followup.WasAccepted();
			Evidence.bSucceeded &= Evidence.bFollowupAccepted;
		}
		else if (Scenario.PostAction == EC05CPostAction::ApplyCheckpointTwice)
		{
			const FBattleEvent* Checkpoint = Effects.GetEvents().FindByPredicate(
				[](const FBattleEvent& Event)
				{
					return Event.GetType() == EBattleEventType::OpponentRemovalCheckpoint;
				});
			Evidence.bSucceeded &= Checkpoint != nullptr;
			if (Checkpoint != nullptr)
			{
				Evidence.CheckpointOrdinal = Checkpoint->GetEventOrdinal();
				FBattleBetweenActionsStatRefresh Refresh;
				Refresh.StateVersion = Engine->GetSnapshot().GetStateVersion();
				Refresh.OpponentRemovalCheckpointEventOrdinal = Evidence.CheckpointOrdinal;
				Refresh.BattlerId = MakeNumericId<FBattlerId>(PlayerLeftBattlerValue);
				Refresh.NewLevel = 51;
				Refresh.NewStats = {210, 110, 110, 110, 110, 310};
				Refresh.NewCurrentHP = 200;
				Evidence.bFirstRefreshAccepted = Engine->ApplyBetweenActionsStatRefresh(
					Refresh).WasAccepted();
				Refresh.StateVersion = Engine->GetSnapshot().GetStateVersion();
				const FBattleResolution Reused = Engine->ApplyBetweenActionsStatRefresh(Refresh);
				Evidence.bSecondRefreshRejected = !Reused.WasAccepted();
				Evidence.SecondRefreshReason = Reused.GetRejection().Reason;
				Evidence.bSucceeded &= Evidence.bFirstRefreshAccepted
					&& Evidence.bSecondRefreshRejected;
			}
		}
		else if (Scenario.PostAction == EC05CPostAction::FinishRemainingQueue)
		{
			Evidence.bFollowupAccepted = FinishRemainingQueue(*Engine);
			Evidence.bSucceeded &= Evidence.bFollowupAccepted;
		}
		else if (Scenario.PostAction == EC05CPostAction::SubmitTerminalDecision)
		{
			Evidence.StateVersionBeforeTerminalRejection = Engine->GetSnapshot().GetStateVersion();
			const FBattleResolution TerminalAttempt = Engine->SubmitDecision(TerminalDecision);
			Evidence.bTerminalDecisionRejected = !TerminalAttempt.WasAccepted();
			Evidence.TerminalDecisionReason = TerminalAttempt.GetRejection().Reason;
			Evidence.StateVersionAfterTerminalRejection = Engine->GetSnapshot().GetStateVersion();
			Evidence.bSucceeded &= Evidence.bTerminalDecisionRejected;
		}

		Evidence.bRandomExact = RandomView->IsExact();
		Evidence.bSucceeded &= Evidence.bRandomExact;
		GatherEvidence(*Engine, Evidence);
		return Evidence;
	}

	void TestDeterministicTwins(
		FAutomationTestBase& Test,
		const FC05CEvidence& First,
		const FC05CEvidence& Second)
	{
		Test.TestTrue(TEXT("The first public-engine run completes"), First.bSucceeded);
		Test.TestTrue(TEXT("The repeated public-engine run completes"), Second.bSucceeded);
		Test.TestTrue(TEXT("Twin runs preserve total event order"), First.EventOrder == Second.EventOrder);
		Test.TestTrue(TEXT("Twin runs preserve the exact RNG trace"), First.RandomTrace == Second.RandomTrace);
		Test.TestEqual(TEXT("Twin runs preserve the outcome"), First.Outcome, Second.Outcome);
		Test.TestEqual(TEXT("Twin runs preserve the outcome cause"), First.OutcomeCause, Second.OutcomeCause);
		Test.TestTrue(TEXT("Twin runs preserve canonical replay bytes"), First.ReplayBytes == Second.ReplayBytes);
	}

	int32 CountEvents(const FC05CEvidence& Evidence, const EBattleEventType Type)
	{
		int32 Count = 0;
		for (const FBattleEvent& Event : Evidence.Events)
		{
			if (Event.GetType() == Type)
			{
				++Count;
			}
		}
		return Count;
	}

	int32 CountTargetEvents(
		const FC05CEvidence& Evidence,
		const EBattleEventType Type,
		const uint64 BattlerValue)
	{
		int32 Count = 0;
		for (const FBattleEvent& Event : Evidence.Events)
		{
			if (Event.GetType() != Type)
			{
				continue;
			}
			for (const FBattleEventTarget& Target : Event.GetTargets())
			{
				if (Target.BattlerId.IsValid()
					&& Target.BattlerId.GetValue() == BattlerValue)
				{
					++Count;
					break;
				}
			}
		}
		return Count;
	}

	int32 CountEventsFromBattler(
		const FC05CEvidence& Evidence,
		const EBattleEventType Type,
		const uint64 BattlerValue)
	{
		int32 Count = 0;
		for (const FBattleEvent& Event : Evidence.Events)
		{
			if (Event.GetType() == Type
				&& Event.GetSource().BattlerId.IsValid()
				&& Event.GetSource().BattlerId.GetValue() == BattlerValue)
			{
				++Count;
			}
		}
		return Count;
	}

	int32 FindTargetEventIndex(
		const FC05CEvidence& Evidence,
		const EBattleEventType Type,
		const uint64 BattlerValue)
	{
		for (int32 EventIndex = 0; EventIndex < Evidence.Events.Num(); ++EventIndex)
		{
			const FBattleEvent& Event = Evidence.Events[EventIndex];
			if (Event.GetType() != Type)
			{
				continue;
			}
			for (const FBattleEventTarget& Target : Event.GetTargets())
			{
				if (Target.BattlerId.IsValid() && Target.BattlerId.GetValue() == BattlerValue)
				{
					return EventIndex;
				}
			}
		}
		return INDEX_NONE;
	}

	const FBattleEvent* FindTargetEvent(
		const FC05CEvidence& Evidence,
		const EBattleEventType Type,
		const uint64 BattlerValue)
	{
		const int32 EventIndex = FindTargetEventIndex(Evidence, Type, BattlerValue);
		return Evidence.Events.IsValidIndex(EventIndex)
			? &Evidence.Events[EventIndex]
			: nullptr;
	}

	int32 FindLastEventIndex(const FC05CEvidence& Evidence, const EBattleEventType Type)
	{
		for (int32 EventIndex = Evidence.Events.Num() - 1; EventIndex >= 0; --EventIndex)
		{
			if (Evidence.Events[EventIndex].GetType() == Type)
			{
				return EventIndex;
			}
		}
		return INDEX_NONE;
	}

	bool IsSlotVacant(
		const FC05CEvidence& Evidence,
		const EBattleSide Side,
		const EBattlePosition Position)
	{
		const FActiveSlotId SlotId = MakeActiveSlotId(Side, Position);
		for (const FBattleObservedActiveSlot& Slot : Evidence.Snapshot.GetObservedActiveSlots())
		{
			if (Slot.ActiveSlotId == SlotId)
			{
				return Slot.bAvailable
					&& !Slot.TrainerId.IsValid()
					&& !Slot.BattlerId.IsValid();
			}
		}
		return false;
	}

	void TestTerminalFacts(
		FAutomationTestBase& Test,
		const FC05CEvidence& Evidence,
		const EBattleOutcomeCause ExpectedCause)
	{
		Test.TestEqual(TEXT("The terminal outcome is emitted exactly once"), CountEvents(
			Evidence, EBattleEventType::BattleEnded), 1);
		Test.TestEqual(TEXT("Terminal resolution emits no replacement fact"), CountEvents(
			Evidence, EBattleEventType::ReplacementRequired), 0);
		const int32 CompletedIndex = FindLastEventIndex(Evidence, EBattleEventType::ActionCompleted);
		const int32 EndedIndex = FindLastEventIndex(Evidence, EBattleEventType::BattleEnded);
		Test.TestTrue(TEXT("Action completion precedes terminal resolution"),
			CompletedIndex != INDEX_NONE && EndedIndex > CompletedIndex);
		const FBattleEvent* Ended = Evidence.Events.IsValidIndex(EndedIndex)
			? &Evidence.Events[EndedIndex]
			: nullptr;
		Test.TestTrue(TEXT("BattleEnded carries the Outcome cause"),
			Ended != nullptr && Ended->GetCause() == EBattleEventCause::Outcome);
		if (Ended != nullptr)
		{
			Test.TestEqual(TEXT("BattleEnded carries the resolved outcome cause"),
				Ended->GetOutcomeCause(), ExpectedCause);
		}
	}

	int32 FindSourceEventIndex(
		const FC05CEvidence& Evidence,
		const EBattleEventType Type,
		const uint64 BattlerValue)
	{
		return Evidence.Events.IndexOfByPredicate(
			[Type, BattlerValue](const FBattleEvent& Event)
			{
				return Event.GetType() == Type
					&& Event.GetSource().BattlerId.IsValid()
					&& Event.GetSource().BattlerId.GetValue() == BattlerValue;
			});
	}

	int32 FindMovePP(const FC05CEvidence& Evidence, const uint64 BattlerValue)
	{
		const FBattlePartyEntrySetup* Battler = Evidence.Snapshot.FindBattler(
			MakeNumericId<FBattlerId>(BattlerValue));
		return Battler != nullptr && !Battler->Moves.IsEmpty()
			? Battler->Moves[0].CurrentPP
			: INDEX_NONE;
	}

	FC05CScenario MakeSingleFaintBeforeActionScenario(const EC05CPostAction PostAction)
	{
		FC05CScenario Scenario;
		Scenario.ExpectedFirstActor = PlayerLeftBattlerValue;
		Scenario.ExpectedDamageDraws = 1;
		Scenario.PostAction = PostAction;
		Scenario.Battlers =
		{
			{PlayerTrainerValue, PlayerLeftBattlerValue, 0, 200, 300, 3, true,
				EBattleSide::Player, EBattlePosition::Left},
			{OpponentTrainerValue, OpponentLeftBattlerValue, 0, 1, 100, 3, true,
				EBattleSide::Opponent, EBattlePosition::Left},
			{OpponentTrainerValue, OpponentReserveBattlerValue, 1, 200, 50, 3, false,
				EBattleSide::Opponent, EBattlePosition::Left}
		};
		return Scenario;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC05CFaintBeforeActionTest,
		"PokemonSolarus.Battle.C05C.Queue.FaintedActorCanceledWithoutResources",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FBattleC05CFaintBeforeActionTest::RunTest(const FString& Parameters)
	{
		const FC05CScenario Scenario = MakeSingleFaintBeforeActionScenario(
			EC05CPostAction::FinishNextQueuedAction);
		const FC05CEvidence First = RunScenario(Scenario);
		const FC05CEvidence Second = RunScenario(Scenario);
		TestDeterministicTwins(*this, First, Second);

		const int32 FaintedIndex = FindTargetEventIndex(
			First,
			EBattleEventType::Fainted,
			OpponentLeftBattlerValue);
		const int32 CanceledIndex = FindSourceEventIndex(
			First,
			EBattleEventType::ActionCanceled,
			OpponentLeftBattlerValue);
		TestTrue(TEXT("The target faints before its queued action is canceled"),
			FaintedIndex != INDEX_NONE && CanceledIndex > FaintedIndex);
		TestEqual(TEXT("Only the acting move consumes PP"), CountEvents(
			First, EBattleEventType::PPConsumed), 1);
		TestEqual(TEXT("The fainted queued actor consumes no PP"), CountEventsFromBattler(
			First, EBattleEventType::PPConsumed, OpponentLeftBattlerValue), 0);
		TestEqual(TEXT("The fainted queued actor keeps its PP"), FindMovePP(
			First, OpponentLeftBattlerValue), 3);
		TestEqual(TEXT("Only the completed damaging action draws RNG"), First.RandomTrace.Num(), 1);
		TestEqual(TEXT("The queue boundary requests one replacement"), CountEvents(
			First, EBattleEventType::ReplacementRequired), 1);
		const int32 CanceledCompletedIndex = FindSourceEventIndex(
			First,
			EBattleEventType::ActionCompleted,
			OpponentLeftBattlerValue);
		const int32 ReplacementIndex = First.Events.IndexOfByPredicate(
			[](const FBattleEvent& Event)
			{
				return Event.GetType() == EBattleEventType::ReplacementRequired;
			});
		TestTrue(TEXT("The canceled action completes before replacement is requested"),
			CanceledCompletedIndex != INDEX_NONE && ReplacementIndex > CanceledCompletedIndex);
		if (First.Events.IsValidIndex(ReplacementIndex))
		{
			const TConstArrayView<FBattleEventTarget> Targets = First.Events[ReplacementIndex].GetTargets();
			TestEqual(TEXT("ReplacementRequired has one stable slot target"), Targets.Num(), 1);
			if (Targets.Num() == 1)
			{
				TestTrue(TEXT("The replacement belongs to the opponent Trainer"),
					Targets[0].TrainerId == MakeNumericId<FTrainerId>(OpponentTrainerValue));
				TestTrue(TEXT("The replacement targets opponent Left"),
					Targets[0].ActiveSlotId
						== MakeActiveSlotId(EBattleSide::Opponent, EBattlePosition::Left));
				TestFalse(TEXT("No replacement battler is selected during C05C"),
					Targets[0].BattlerId.IsValid());
			}
		}
		TestEqual(TEXT("The battle waits at mandatory replacement"), First.Phase,
			EBattlePhase::MandatoryReplacement);
		TestEqual(TEXT("The nonterminal battle remains in progress"), First.Outcome,
			EBattleOutcome::InProgress);

		FC05CScenario EndTurnScenario;
		EndTurnScenario.ExpectedFirstActor = PlayerLeftBattlerValue;
		EndTurnScenario.ExpectedDamageDraws = 2;
		EndTurnScenario.PostAction = EC05CPostAction::FinishRemainingQueue;
		EndTurnScenario.Battlers =
		{
			{PlayerTrainerValue, PlayerLeftBattlerValue, 0, 200, 300, 3, true,
				EBattleSide::Player, EBattlePosition::Left},
			{OpponentTrainerValue, OpponentLeftBattlerValue, 0, 200, 100, 3, true,
				EBattleSide::Opponent, EBattlePosition::Left}
		};
		const FC05CEvidence EndTurnFirst = RunScenario(EndTurnScenario);
		const FC05CEvidence EndTurnSecond = RunScenario(EndTurnScenario);
		TestDeterministicTwins(*this, EndTurnFirst, EndTurnSecond);
		TestEqual(TEXT("A completed queue with no replacement need enters EndOfTurn"),
			EndTurnFirst.Phase, EBattlePhase::EndOfTurn);
		TestEqual(TEXT("The no-replacement boundary emits no replacement fact"), CountEvents(
			EndTurnFirst, EBattleEventType::ReplacementRequired), 0);
		TestEqual(TEXT("The EndOfTurn boundary remains nonterminal"), EndTurnFirst.Outcome,
			EBattleOutcome::InProgress);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC05CRecoilDoubleFaintTest,
		"PokemonSolarus.Battle.C05C.Recoil.TargetFaintBeforeDoubleFaint",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FBattleC05CRecoilDoubleFaintTest::RunTest(const FString& Parameters)
	{
		FC05CScenario Scenario;
		Scenario.bFixedRecoil = true;
		Scenario.ExpectedFirstActor = PlayerLeftBattlerValue;
		Scenario.ExpectedDamageDraws = 1;
		Scenario.Battlers =
		{
			{PlayerTrainerValue, PlayerLeftBattlerValue, 0, 1, 300, 3, true,
				EBattleSide::Player, EBattlePosition::Left},
			{OpponentTrainerValue, OpponentLeftBattlerValue, 0, 1, 100, 3, true,
				EBattleSide::Opponent, EBattlePosition::Left}
		};
		const FC05CEvidence First = RunScenario(Scenario);
		const FC05CEvidence Second = RunScenario(Scenario);
		TestDeterministicTwins(*this, First, Second);

		const int32 TargetDamage = FindTargetEventIndex(
			First, EBattleEventType::Damage, OpponentLeftBattlerValue);
		const int32 TargetHp = FindTargetEventIndex(
			First, EBattleEventType::HPChanged, OpponentLeftBattlerValue);
		const int32 TargetFaint = FindTargetEventIndex(
			First, EBattleEventType::Fainted, OpponentLeftBattlerValue);
		const int32 RecoilDamage = FindTargetEventIndex(
			First, EBattleEventType::Damage, PlayerLeftBattlerValue);
		const int32 RecoilHp = FindTargetEventIndex(
			First, EBattleEventType::HPChanged, PlayerLeftBattlerValue);
		const int32 RecoilFaint = FindTargetEventIndex(
			First, EBattleEventType::Fainted, PlayerLeftBattlerValue);
		const int32 UserStatus = FindTargetEventIndex(
			First, EBattleEventType::StatusChanged, PlayerLeftBattlerValue);
		const int32 UserStage = FindTargetEventIndex(
			First, EBattleEventType::StatStageChanged, PlayerLeftBattlerValue);
		TestEqual(TEXT("The user receives major and volatile state before damage"),
			CountTargetEvents(First, EBattleEventType::StatusChanged, PlayerLeftBattlerValue), 2);
		TestEqual(TEXT("The user receives one temporary stage before damage"),
			CountTargetEvents(First, EBattleEventType::StatStageChanged, PlayerLeftBattlerValue), 1);
		TestTrue(TEXT("Temporary user state exists before the damaging hit"),
			UserStatus != INDEX_NONE && UserStage != INDEX_NONE
				&& UserStatus < TargetDamage && UserStage < TargetDamage);
		TestTrue(TEXT("Damage, HP change, and target faint stay adjacent"),
			TargetDamage != INDEX_NONE
				&& TargetHp == TargetDamage + 1
				&& TargetFaint == TargetHp + 1);
		TestTrue(TEXT("The target faint precedes linked recoil and its faint"),
			RecoilDamage > TargetFaint && RecoilFaint > RecoilDamage);
		TestTrue(TEXT("Linked recoil emits an adjacent damage/HP/faint triple"),
			RecoilDamage != INDEX_NONE
				&& RecoilHp == RecoilDamage + 1
				&& RecoilFaint == RecoilHp + 1);
		TestEqual(TEXT("The recoil double faint is a player defeat"), First.Outcome,
			EBattleOutcome::Defeat);
		TestEqual(TEXT("The double faint has the simultaneous-faint cause"), First.OutcomeCause,
			EBattleOutcomeCause::SimultaneousFaint);

		const int32 OpponentLeftSlot = FindTargetEventIndex(
			First, EBattleEventType::LeftActiveSlot, OpponentLeftBattlerValue);
		const int32 OpponentRemoved = FindTargetEventIndex(
			First, EBattleEventType::Removed, OpponentLeftBattlerValue);
		const int32 OpponentCheckpoint = FindTargetEventIndex(
			First, EBattleEventType::OpponentRemovalCheckpoint, OpponentLeftBattlerValue);
		const int32 PlayerLeftSlot = FindTargetEventIndex(
			First, EBattleEventType::LeftActiveSlot, PlayerLeftBattlerValue);
		const int32 PlayerRemoved = FindTargetEventIndex(
			First, EBattleEventType::Removed, PlayerLeftBattlerValue);
		TestTrue(TEXT("The recoil user leaves and is removed only after its faint"),
			RecoilFaint != INDEX_NONE
				&& PlayerLeftSlot > RecoilFaint
				&& PlayerRemoved > PlayerLeftSlot
				&& OpponentLeftSlot > PlayerRemoved);
		TestTrue(TEXT("Removal and checkpoint wait for linked recoil and both faints"),
			RecoilFaint != INDEX_NONE
				&& OpponentLeftSlot > RecoilFaint
				&& OpponentRemoved > OpponentLeftSlot
				&& OpponentCheckpoint > OpponentRemoved);
		TestEqual(TEXT("Both fainted battlers leave their active slots"), CountEvents(
			First, EBattleEventType::LeftActiveSlot), 2);
		TestEqual(TEXT("Both fainted battlers emit one removal"), CountEvents(
			First, EBattleEventType::Removed), 2);
		TestTrue(TEXT("The player slot is vacant after removal"), IsSlotVacant(
			First, EBattleSide::Player, EBattlePosition::Left));
		TestTrue(TEXT("The opponent slot is vacant after removal"), IsSlotVacant(
			First, EBattleSide::Opponent, EBattlePosition::Left));

		const FBattleObservedBattler* RemovedPlayer = First.Snapshot.FindObservedBattler(
			MakeNumericId<FBattlerId>(PlayerLeftBattlerValue));
		TestTrue(TEXT("The removed user remains visible as a fainted party battler"),
			RemovedPlayer != nullptr && RemovedPlayer->bFainted);
		if (RemovedPlayer != nullptr)
		{
			int32 AttackStage = INDEX_NONE;
			TestTrue(TEXT("The removed user's Attack stage remains readable"),
				RemovedPlayer->StatStages.TryGetStage(EBattleStat::Attack, AttackStage));
			TestEqual(TEXT("Faint cleanup clears the major status"),
				RemovedPlayer->MajorStatusId.IsValid(), false);
			TestEqual(TEXT("Faint cleanup clears temporary stat stages"), AttackStage, 0);
		}
		TestTerminalFacts(*this, First, EBattleOutcomeCause::SimultaneousFaint);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC05CSpreadSimultaneousTest,
		"PokemonSolarus.Battle.C05C.Spread.SimultaneousGroupStableOrder",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FBattleC05CSpreadSimultaneousTest::RunTest(const FString& Parameters)
	{
		FC05CScenario Scenario;
		Scenario.Format = EBattleFormat::Double;
		Scenario.TargetClass = EBattleTargetClass::FixedSpreadSet;
		Scenario.ExpectedFirstActor = PlayerLeftBattlerValue;
		Scenario.ExpectedDamageDraws = 3;
		Scenario.Battlers =
		{
			{PlayerTrainerValue, PlayerLeftBattlerValue, 0, 200, 400, 3, true,
				EBattleSide::Player, EBattlePosition::Left},
			{PlayerTrainerValue, PlayerRightBattlerValue, 1, 1, 300, 3, true,
				EBattleSide::Player, EBattlePosition::Right},
			{OpponentTrainerValue, OpponentLeftBattlerValue, 0, 1, 200, 3, true,
				EBattleSide::Opponent, EBattlePosition::Left},
			{OpponentTrainerValue, OpponentRightBattlerValue, 1, 1, 100, 3, true,
				EBattleSide::Opponent, EBattlePosition::Right}
		};
		const FC05CEvidence First = RunScenario(Scenario);
		const FC05CEvidence Second = RunScenario(Scenario);
		TestDeterministicTwins(*this, First, Second);

		TArray<const FBattleEvent*> Faints;
		for (const FBattleEvent& Event : First.Events)
		{
			if (Event.GetType() == EBattleEventType::Fainted)
			{
				Faints.Add(&Event);
			}
		}
		TestEqual(TEXT("The spread action emits three individual faint facts"), Faints.Num(), 3);
		if (Faints.Num() == 3)
		{
			TestTrue(TEXT("Every spread faint carries a simultaneous group"),
				Faints[0]->GetSimultaneousGroupId().IsSet()
					&& Faints[1]->GetSimultaneousGroupId().IsSet()
					&& Faints[2]->GetSimultaneousGroupId().IsSet());
			if (Faints[0]->GetSimultaneousGroupId().IsSet()
				&& Faints[1]->GetSimultaneousGroupId().IsSet()
				&& Faints[2]->GetSimultaneousGroupId().IsSet())
			{
				const uint64 GroupId = Faints[0]->GetSimultaneousGroupId().GetValue();
				TestTrue(TEXT("All spread faints share one nonzero group"),
					GroupId > 0
						&& Faints[1]->GetSimultaneousGroupId().GetValue() == GroupId
						&& Faints[2]->GetSimultaneousGroupId().GetValue() == GroupId);
			}
			TestTrue(TEXT("Spread faint order is player Right, opponent Left, opponent Right"),
				Faints[0]->GetTargets()[0].BattlerId.GetValue() == PlayerRightBattlerValue
					&& Faints[1]->GetTargets()[0].BattlerId.GetValue() == OpponentLeftBattlerValue
					&& Faints[2]->GetTargets()[0].BattlerId.GetValue() == OpponentRightBattlerValue);

			if (Faints[0]->GetSimultaneousGroupId().IsSet())
			{
				const uint64 GroupId = Faints[0]->GetSimultaneousGroupId().GetValue();
				const TArray<uint64> FaintedBattlers =
				{
					PlayerRightBattlerValue,
					OpponentLeftBattlerValue,
					OpponentRightBattlerValue
				};
				for (const uint64 BattlerValue : FaintedBattlers)
				{
					const FBattleEvent* Damage = FindTargetEvent(
						First, EBattleEventType::Damage, BattlerValue);
					const FBattleEvent* HpChanged = FindTargetEvent(
						First, EBattleEventType::HPChanged, BattlerValue);
					const FBattleEvent* Fainted = FindTargetEvent(
						First, EBattleEventType::Fainted, BattlerValue);
					const int32 DamageIndex = FindTargetEventIndex(
						First, EBattleEventType::Damage, BattlerValue);
					const int32 HpIndex = FindTargetEventIndex(
						First, EBattleEventType::HPChanged, BattlerValue);
					const int32 FaintIndex = FindTargetEventIndex(
						First, EBattleEventType::Fainted, BattlerValue);
					TestTrue(TEXT("Each spread target emits an adjacent damage/HP/faint triple"),
						DamageIndex != INDEX_NONE
							&& HpIndex == DamageIndex + 1
							&& FaintIndex == HpIndex + 1);
					TestTrue(TEXT("Every spread triple shares the one simultaneous group"),
						Damage != nullptr && HpChanged != nullptr && Fainted != nullptr
							&& Damage->GetSimultaneousGroupId().IsSet()
							&& HpChanged->GetSimultaneousGroupId().IsSet()
							&& Fainted->GetSimultaneousGroupId().IsSet()
							&& Damage->GetSimultaneousGroupId().GetValue() == GroupId
							&& HpChanged->GetSimultaneousGroupId().GetValue() == GroupId
							&& Fainted->GetSimultaneousGroupId().GetValue() == GroupId);
					TestTrue(TEXT("Every spread triple preserves one-of-one hit metadata"),
						Damage != nullptr && HpChanged != nullptr && Fainted != nullptr
							&& Damage->GetHitIndex().IsSet() && Damage->GetHitIndex().GetValue() == 1
							&& HpChanged->GetHitIndex().IsSet() && HpChanged->GetHitIndex().GetValue() == 1
							&& Fainted->GetHitIndex().IsSet() && Fainted->GetHitIndex().GetValue() == 1
							&& Damage->GetHitCount().IsSet() && Damage->GetHitCount().GetValue() == 1
							&& HpChanged->GetHitCount().IsSet() && HpChanged->GetHitCount().GetValue() == 1
							&& Fainted->GetHitCount().IsSet() && Fainted->GetHitCount().GetValue() == 1);
				}
			}
		}
		TestEqual(TEXT("The spread win resolves only after all three damage draws"),
			First.RandomTrace.Num(), 3);
		TestEqual(TEXT("The surviving player battler wins"), First.Outcome,
			EBattleOutcome::Victory);
		TestTerminalFacts(*this, First, EBattleOutcomeCause::Ordinary);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC05COpponentCheckpointTest,
		"PokemonSolarus.Battle.C05C.Checkpoint.StableOneUseOpponentRemoval",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FBattleC05COpponentCheckpointTest::RunTest(const FString& Parameters)
	{
		const FC05CScenario Scenario = MakeSingleFaintBeforeActionScenario(
			EC05CPostAction::ApplyCheckpointTwice);
		const FC05CEvidence First = RunScenario(Scenario);
		const FC05CEvidence Second = RunScenario(Scenario);
		TestDeterministicTwins(*this, First, Second);

		TestEqual(TEXT("One opponent removal creates one checkpoint"), CountEvents(
			First, EBattleEventType::OpponentRemovalCheckpoint), 1);
		TestTrue(TEXT("The checkpoint ordinal is stable and nonzero"),
			First.CheckpointOrdinal > 0 && First.CheckpointOrdinal == Second.CheckpointOrdinal);
		const int32 RemovedIndex = FindTargetEventIndex(
			First, EBattleEventType::Removed, OpponentLeftBattlerValue);
		const int32 CheckpointIndex = FindTargetEventIndex(
			First, EBattleEventType::OpponentRemovalCheckpoint, OpponentLeftBattlerValue);
		const int32 RefreshIndex = FindLastEventIndex(First, EBattleEventType::StatRefreshApplied);
		TestTrue(TEXT("The checkpoint follows removal and precedes its one stat refresh"),
			RemovedIndex != INDEX_NONE
				&& CheckpointIndex > RemovedIndex
				&& RefreshIndex > CheckpointIndex);
		TestTrue(TEXT("The first matching stat refresh consumes the checkpoint"),
			First.bFirstRefreshAccepted);
		TestTrue(TEXT("The same checkpoint cannot be reused"), First.bSecondRefreshRejected);
		TestEqual(TEXT("Checkpoint reuse has the typed rejection"), First.SecondRefreshReason,
			EBattleRejectionReason::InvalidCheckpoint);
		TestEqual(TEXT("Exactly one refresh is applied"), CountEvents(
			First, EBattleEventType::StatRefreshApplied), 1);
		TestEqual(TEXT("Exactly one reuse is rejected"), CountEvents(
			First, EBattleEventType::StatRefreshRejected), 1);
		TestEqual(TEXT("Between-actions refresh remains in Resolving"), First.Phase,
			EBattlePhase::Resolving);
		TestTrue(TEXT("The removed opponent slot stays vacant through the stat refresh"), IsSlotVacant(
			First, EBattleSide::Opponent, EBattlePosition::Left));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC05CLastOpponentVictoryTest,
		"PokemonSolarus.Battle.C05C.Outcome.LastOpponentVictoryAndTerminalRejection",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FBattleC05CLastOpponentVictoryTest::RunTest(const FString& Parameters)
	{
		FC05CScenario Scenario;
		Scenario.ExpectedFirstActor = PlayerLeftBattlerValue;
		Scenario.ExpectedDamageDraws = 1;
		Scenario.PostAction = EC05CPostAction::SubmitTerminalDecision;
		Scenario.Battlers =
		{
			{PlayerTrainerValue, PlayerLeftBattlerValue, 0, 200, 300, 3, true,
				EBattleSide::Player, EBattlePosition::Left},
			{OpponentTrainerValue, OpponentLeftBattlerValue, 0, 1, 100, 3, true,
				EBattleSide::Opponent, EBattlePosition::Left}
		};
		const FC05CEvidence First = RunScenario(Scenario);
		const FC05CEvidence Second = RunScenario(Scenario);
		TestDeterministicTwins(*this, First, Second);

		TestEqual(TEXT("Removing the last opponent is Victory"), First.Outcome,
			EBattleOutcome::Victory);
		TestEqual(TEXT("An ordinary solo victory has the ordinary cause"), First.OutcomeCause,
			EBattleOutcomeCause::Ordinary);
		TestEqual(TEXT("Victory enters Terminal"), First.Phase, EBattlePhase::Terminal);
		TestTrue(TEXT("A later decision is rejected"), First.bTerminalDecisionRejected);
		TestEqual(TEXT("The later decision is rejected as terminal"), First.TerminalDecisionReason,
			EBattleRejectionReason::TerminalState);
		TestEqual(TEXT("Terminal rejection does not change state version"),
			First.StateVersionBeforeTerminalRejection,
			First.StateVersionAfterTerminalRejection);
		TestEqual(TEXT("Terminal rejection consumes no extra RNG"), First.RandomTrace.Num(), 1);
		const int32 FinalRemoved = FindTargetEventIndex(
			First, EBattleEventType::Removed, OpponentLeftBattlerValue);
		const int32 FinalCheckpoint = FindTargetEventIndex(
			First, EBattleEventType::OpponentRemovalCheckpoint, OpponentLeftBattlerValue);
		const int32 FinalCompleted = FindLastEventIndex(First, EBattleEventType::ActionCompleted);
		TestTrue(TEXT("The final opponent still publishes its future progression checkpoint"),
			FinalRemoved != INDEX_NONE
				&& FinalCheckpoint > FinalRemoved
				&& FinalCompleted > FinalCheckpoint);
		TestTerminalFacts(*this, First, EBattleOutcomeCause::Ordinary);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC05CPlayerWipeTest,
		"PokemonSolarus.Battle.C05C.Outcome.PlayerWipeDefeat",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FBattleC05CPlayerWipeTest::RunTest(const FString& Parameters)
	{
		FC05CScenario Scenario;
		Scenario.ExpectedFirstActor = OpponentLeftBattlerValue;
		Scenario.ExpectedDamageDraws = 1;
		Scenario.Battlers =
		{
			{PlayerTrainerValue, PlayerLeftBattlerValue, 0, 1, 100, 3, true,
				EBattleSide::Player, EBattlePosition::Left},
			{OpponentTrainerValue, OpponentLeftBattlerValue, 0, 200, 300, 3, true,
				EBattleSide::Opponent, EBattlePosition::Left}
		};
		const FC05CEvidence First = RunScenario(Scenario);
		const FC05CEvidence Second = RunScenario(Scenario);
		TestDeterministicTwins(*this, First, Second);

		TestEqual(TEXT("The player-side wipe is Defeat"), First.Outcome,
			EBattleOutcome::Defeat);
		TestEqual(TEXT("A one-sided wipe has the ordinary cause"), First.OutcomeCause,
			EBattleOutcomeCause::Ordinary);
		TestEqual(TEXT("The player wipe is terminal"), First.Phase, EBattlePhase::Terminal);
		TestEqual(TEXT("The player faint is emitted once"), CountEvents(
			First, EBattleEventType::Fainted), 1);
		TestEqual(TEXT("The wipe consumes one damage draw"), First.RandomTrace.Num(), 1);
		TestTerminalFacts(*this, First, EBattleOutcomeCause::Ordinary);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC05CPartnerTeamVictoryTest,
		"PokemonSolarus.Battle.C05C.Outcome.PartnerTeamVictory",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FBattleC05CPartnerTeamVictoryTest::RunTest(const FString& Parameters)
	{
		FC05CScenario Scenario;
		Scenario.Format = EBattleFormat::PartnerDouble;
		Scenario.TargetClass = EBattleTargetClass::FixedSpreadSet;
		Scenario.ExpectedFirstActor = PartnerBattlerValue;
		Scenario.ExpectedDamageDraws = 3;
		Scenario.Battlers =
		{
			{PlayerTrainerValue, PlayerLeftBattlerValue, 0, 1, 300, 3, true,
				EBattleSide::Player, EBattlePosition::Left},
			{PartnerTrainerValue, PartnerBattlerValue, 0, 200, 400, 3, true,
				EBattleSide::Player, EBattlePosition::Right},
			{OpponentTrainerValue, OpponentLeftBattlerValue, 0, 1, 200, 3, true,
				EBattleSide::Opponent, EBattlePosition::Left},
			{OpponentTrainerValue, OpponentRightBattlerValue, 1, 1, 100, 3, true,
				EBattleSide::Opponent, EBattlePosition::Right}
		};
		const FC05CEvidence First = RunScenario(Scenario);
		const FC05CEvidence Second = RunScenario(Scenario);
		TestDeterministicTwins(*this, First, Second);

		TestEqual(TEXT("The partner-only surviving side wins"), First.Outcome,
			EBattleOutcome::Victory);
		TestEqual(TEXT("The partner-only win has the Team Victory cause"), First.OutcomeCause,
			EBattleOutcomeCause::PartnerTeamVictory);
		const FBattleObservedBattler* Player = First.Snapshot.FindObservedBattler(
			MakeNumericId<FBattlerId>(PlayerLeftBattlerValue));
		const FBattleObservedBattler* Partner = First.Snapshot.FindObservedBattler(
			MakeNumericId<FBattlerId>(PartnerBattlerValue));
		TestTrue(
			TEXT("The player-owned battler is recovered to one HP"),
			Player != nullptr && !Player->bFainted && Player->CurrentHP == 1);
		TestTrue(TEXT("The partner remains usable"), Partner != nullptr && !Partner->bFainted);
		TestEqual(TEXT("The partner spread consumes three damage draws"),
			First.RandomTrace.Num(), 3);
		TestTerminalFacts(*this, First, EBattleOutcomeCause::PartnerTeamVictory);
		return true;
	}
}

#endif
