#if WITH_DEV_AUTOMATION_TESTS

#include "Algo/Count.h"
#include "Battle/BattleEngine.h"
#include "Battle/BattleEvent.h"
#include "Battle/BattleTargeting.h"
#include "BattleTestFactories.h"
#include "BattleTestRandom.h"
#include "Misc/AutomationTest.h"

namespace BattleTargetingTests
{
	using BattleTest::MakeActiveSlotId;
	using BattleTest::MakeDefinitionId;
	using BattleTest::MakeNumericId;
	using BattleTest::MakePartySlotId;
	using BattleTest::FBattleExpectedRandomDraw;
	using BattleTest::FStrictBattleRandom;

	constexpr uint64 PlayerTrainerValue = 1;
	constexpr uint64 OpponentTrainerValue = 2;
	constexpr uint64 PlayerLeftBattlerValue = 11;
	constexpr uint64 PlayerRightBattlerValue = 12;
	constexpr uint64 OpponentLeftBattlerValue = 21;
	constexpr uint64 OpponentRightBattlerValue = 22;

	const TCHAR* FixedSpreadMoveName = TEXT("Move.C04B.FixedSpread");
	const TCHAR* FieldConditionName = TEXT("Condition.C04B.Field");
	const TCHAR* SideConditionName = TEXT("Condition.C04B.Side");
	const TCHAR* AbilityName = TEXT("Ability.C04B.Core");
	const TCHAR* SpeciesName = TEXT("Species.C04B.Core");

	FBattleTargetPositionFacts MakePosition(
		const EBattleSide Side,
		const EBattlePosition Position,
		const uint64 BattlerValue,
		const EBattleTargetPositionState State,
		const bool bSemiInvulnerable = false)
	{
		FBattleTargetPositionFacts Facts;
		Facts.ActiveSlotId = MakeActiveSlotId(Side, Position);
		if (State != EBattleTargetPositionState::Empty)
		{
			Facts.BattlerId = MakeNumericId<FBattlerId>(BattlerValue);
		}
		Facts.State = State;
		Facts.bSemiInvulnerable = bSemiInvulnerable;
		return Facts;
	}

	TArray<FBattleTargetPositionFacts> MakeSinglePositions()
	{
		return {
			MakePosition(
				EBattleSide::Opponent,
				EBattlePosition::Right,
				0,
				EBattleTargetPositionState::Empty),
			MakePosition(
				EBattleSide::Player,
				EBattlePosition::Left,
				PlayerLeftBattlerValue,
				EBattleTargetPositionState::Living),
			MakePosition(
				EBattleSide::Opponent,
				EBattlePosition::Left,
				OpponentLeftBattlerValue,
				EBattleTargetPositionState::Living),
			MakePosition(
				EBattleSide::Player,
				EBattlePosition::Right,
				0,
				EBattleTargetPositionState::Empty)
		};
	}

	TArray<FBattleTargetPositionFacts> MakeDoublePositions()
	{
		// Deliberately non-canonical input order proves resolver-owned stable ordering.
		return {
			MakePosition(
				EBattleSide::Opponent,
				EBattlePosition::Right,
				OpponentRightBattlerValue,
				EBattleTargetPositionState::Living),
			MakePosition(
				EBattleSide::Player,
				EBattlePosition::Left,
				PlayerLeftBattlerValue,
				EBattleTargetPositionState::Living),
			MakePosition(
				EBattleSide::Opponent,
				EBattlePosition::Left,
				OpponentLeftBattlerValue,
				EBattleTargetPositionState::Living),
			MakePosition(
				EBattleSide::Player,
				EBattlePosition::Right,
				PlayerRightBattlerValue,
				EBattleTargetPositionState::Living)
		};
	}

	FBattleBattlerTarget MakeBattlerTarget(
		const EBattleSide Side,
		const EBattlePosition Position,
		const uint64 BattlerValue)
	{
		FBattleBattlerTarget Target;
		Target.ActiveSlotId = MakeActiveSlotId(Side, Position);
		Target.BattlerId = MakeNumericId<FBattlerId>(BattlerValue);
		return Target;
	}

	FBattleEventTarget MakeEventBattlerTarget(
		const uint64 TrainerValue,
		const EBattleSide Side,
		const EBattlePosition Position,
		const uint64 BattlerValue)
	{
		FBattleEventTarget Target;
		Target.TrainerId = MakeNumericId<FTrainerId>(TrainerValue);
		Target.BattlerId = MakeNumericId<FBattlerId>(BattlerValue);
		Target.ActiveSlotId = MakeActiveSlotId(Side, Position);
		return Target;
	}

	FBattleRandomContext MakeRandomContext(const uint64 ResolutionValue = 1)
	{
		FBattleRandomContext Context;
		Context.BattleId = MakeNumericId<FBattleId>(4404);
		Context.TurnId = MakeNumericId<FTurnId>(1);
		Context.ActionId = MakeNumericId<FActionId>(1);
		Context.ResolutionId = MakeNumericId<FResolutionId>(ResolutionValue);
		Context.RulePurpose = FBattleTargetResolver::GetRandomLegalOpponentRulePurpose();
		return Context;
	}

	FBattleTargetSelectionSpec MakeSelectionSpec(
		const EBattleTargetClass TargetClass,
		TArray<FBattleTargetPositionFacts> Positions)
	{
		FBattleTargetSelectionSpec Spec;
		Spec.TargetClass = TargetClass;
		Spec.UserSlotId = MakeActiveSlotId(EBattleSide::Player, EBattlePosition::Left);
		Spec.UserBattlerId = MakeNumericId<FBattlerId>(PlayerLeftBattlerValue);
		Spec.Positions = MoveTemp(Positions);
		return Spec;
	}

	FBattleTargetResolutionSpec MakeResolutionSpec(
		const EBattleTargetClass TargetClass,
		TArray<FBattleTargetPositionFacts> Positions,
		const FBattleBattlerTarget& ExplicitTarget = FBattleBattlerTarget())
	{
		FBattleTargetResolutionSpec Spec;
		Spec.TargetClass = TargetClass;
		Spec.UserSlotId = MakeActiveSlotId(EBattleSide::Player, EBattlePosition::Left);
		Spec.UserBattlerId = MakeNumericId<FBattlerId>(PlayerLeftBattlerValue);
		Spec.Positions = MoveTemp(Positions);
		Spec.ExplicitTarget = ExplicitTarget;
		Spec.RandomContext = MakeRandomContext();
		return Spec;
	}

	FBattleEventSpec MakeSelectedOtherEventSpec(const FBattleEventTarget& Target)
	{
		FBattleEventSpec Spec;
		Spec.EventOrdinal = 1;
		Spec.BattleId = MakeNumericId<FBattleId>(4404);
		Spec.TurnId = MakeNumericId<FTurnId>(1);
		Spec.ActionId = MakeNumericId<FActionId>(1);
		Spec.ResolutionId = MakeNumericId<FResolutionId>(1);
		Spec.Type = EBattleEventType::TargetsResolved;
		Spec.Cause = EBattleEventCause::Targeting;
		Spec.CauseActionKind = EBattleActionKind::Fight;
		Spec.Source.TrainerId = MakeNumericId<FTrainerId>(PlayerTrainerValue);
		Spec.Source.BattlerId = MakeNumericId<FBattlerId>(PlayerLeftBattlerValue);
		Spec.Source.ActiveSlotId = MakeActiveSlotId(
			EBattleSide::Player,
			EBattlePosition::Left);
		Spec.Targets.Add(Target);
		FBattleTargetResolutionMetadata Metadata;
		Metadata.TargetClass = EBattleTargetClass::SelectedOtherBattler;
		Spec.TargetResolution = Metadata;
		return Spec;
	}

	bool ContainsCandidate(
		const TConstArrayView<FBattleBattlerTarget> Candidates,
		const FBattleBattlerTarget& Expected)
	{
		return Candidates.Contains(Expected);
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

	TArray<EBattleTargetClass> GetAllTargetClasses()
	{
		return {
			EBattleTargetClass::Self,
			EBattleTargetClass::SelectedAlly,
			EBattleTargetClass::SelectedOpponent,
			EBattleTargetClass::AnySelectedBattler,
			EBattleTargetClass::RandomLegalOpponent,
			EBattleTargetClass::UserSide,
			EBattleTargetClass::OpponentSide,
			EBattleTargetClass::BothSides,
			EBattleTargetClass::Field,
			EBattleTargetClass::FixedSpreadSet,
			EBattleTargetClass::SelectedOtherBattler,
			EBattleTargetClass::FixedOpponentSpreadSet
		};
	}

	bool RequiresExplicitChoice(const EBattleTargetClass TargetClass)
	{
		return TargetClass == EBattleTargetClass::SelectedAlly
			|| TargetClass == EBattleTargetClass::SelectedOpponent
			|| TargetClass == EBattleTargetClass::AnySelectedBattler
			|| TargetClass == EBattleTargetClass::SelectedOtherBattler;
	}

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

	FBattleDefinitionCatalog MakeCatalog(
		const EBattleTargetClass MoveTargetClass = EBattleTargetClass::FixedSpreadSet,
		const EBattleMoveCategory MoveCategory = EBattleMoveCategory::Physical)
	{
		FBattleDefinitionCatalogInput Input;
		Input.TypeChartEntries = MakeTypeChart();
		const bool bTargetsField = MoveTargetClass == EBattleTargetClass::Field;
		const bool bTargetsSide = MoveTargetClass == EBattleTargetClass::UserSide
			|| MoveTargetClass == EBattleTargetClass::OpponentSide
			|| MoveTargetClass == EBattleTargetClass::BothSides;

		FBattleMoveDefinition Move;
		Move.Id = MakeDefinitionId<FMoveId>(FixedSpreadMoveName);
		Move.Type = EPokemonType::Normal;
		Move.Category = bTargetsField || bTargetsSide
			? EBattleMoveCategory::Status
			: MoveCategory;
		Move.Power = Move.Category == EBattleMoveCategory::Status ? 0 : 40;
		Move.bAlwaysHits = Move.Category == EBattleMoveCategory::Status;
		Move.Accuracy = Move.bAlwaysHits ? 0 : 100;
		Move.BasePP = 20;
		Move.Priority = 0;
		Move.TargetClass = MoveTargetClass;
		FBattleMoveEffectDescriptor Effect;
		Effect.Order = 0;
		if (bTargetsField)
		{
			Effect.Kind = EBattleMoveEffectKind::SetFieldCondition;
			Effect.Target = EBattleEffectTarget::Field;
			Effect.ConditionId = MakeDefinitionId<FConditionId>(FieldConditionName);
			Effect.DurationTurns = 5;
			Input.Conditions.Add({Effect.ConditionId, EBattleConditionKind::Weather});
		}
		else if (bTargetsSide)
		{
			Effect.Kind = EBattleMoveEffectKind::SetSideCondition;
			Effect.Target = MoveTargetClass == EBattleTargetClass::UserSide
				? EBattleEffectTarget::UserSide
				: MoveTargetClass == EBattleTargetClass::OpponentSide
					? EBattleEffectTarget::TargetSide
					: EBattleEffectTarget::BothSides;
			Effect.ConditionId = MakeDefinitionId<FConditionId>(SideConditionName);
			Effect.DurationTurns = 5;
			Input.Conditions.Add({Effect.ConditionId, EBattleConditionKind::SideCondition});
		}
		else
		{
			Effect.Kind = EBattleMoveEffectKind::Damage;
			Effect.Target = EBattleEffectTarget::AllResolvedTargets;
		}
		Move.Effects.Add(Effect);
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
				? TEXT("Selector.C04B.Player")
				: TEXT("Selector.C04B.Opponent"));
		return Trainer;
	}

	FBattlePartyEntrySetup MakePartyEntry(
		const uint64 TrainerValue,
		const uint64 BattlerValue,
		const int32 PartyIndex,
		const int32 Speed,
		const int32 CurrentPP)
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
		Entry.Moves.Add(
			{
				0,
				MakeDefinitionId<FMoveId>(FixedSpreadMoveName),
				CurrentPP,
				20
			});
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

	FBattleSetup MakeSetup(const int32 CurrentPP, const EBattleFormat Format)
	{
		FBattleSetupInput Input;
		Input.BattleId = MakeNumericId<FBattleId>(4404);
		Input.SettingsReference = {MakeDefinitionId<FDefinitionId>(TEXT("Settings.C04B")), 1};
		Input.CatalogReference = {MakeDefinitionId<FDefinitionId>(TEXT("Catalog.C04B")), 1};
		Input.EncounterKind = EBattleEncounterKind::Trainer;
		Input.Format = Format;
		Input.CaptureCapacity = {2, 100};
		Input.Policies.bBagAllowed = false;

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
			400,
			CurrentPP));
		if (Format == EBattleFormat::Double)
		{
			Input.PartyEntries.Add(MakePartyEntry(
				PlayerTrainerValue,
				PlayerRightBattlerValue,
				1,
				300,
				CurrentPP));
		}
		Input.PartyEntries.Add(MakePartyEntry(
			OpponentTrainerValue,
			OpponentLeftBattlerValue,
			0,
			200,
			CurrentPP));
		if (Format == EBattleFormat::Double)
		{
			Input.PartyEntries.Add(MakePartyEntry(
				OpponentTrainerValue,
				OpponentRightBattlerValue,
				1,
				100,
				CurrentPP));
		}

		Input.StartingActive.Add(MakeActive(
			EBattleSide::Player,
			EBattlePosition::Left,
			PlayerTrainerValue,
			PlayerLeftBattlerValue));
		if (Format == EBattleFormat::Double)
		{
			Input.StartingActive.Add(MakeActive(
				EBattleSide::Player,
				EBattlePosition::Right,
				PlayerTrainerValue,
				PlayerRightBattlerValue));
		}
		Input.StartingActive.Add(MakeActive(
			EBattleSide::Opponent,
			EBattlePosition::Left,
			OpponentTrainerValue,
			OpponentLeftBattlerValue));
		if (Format == EBattleFormat::Double)
		{
			Input.StartingActive.Add(MakeActive(
				EBattleSide::Opponent,
				EBattlePosition::Right,
				OpponentTrainerValue,
				OpponentRightBattlerValue));
		}

		Input.ObedienceInputs.Add(
			{
				MakeNumericId<FBattlerId>(PlayerLeftBattlerValue),
				true,
				20,
				8
			});
		if (Format == EBattleFormat::Double)
		{
			Input.ObedienceInputs.Add(
				{
					MakeNumericId<FBattlerId>(PlayerRightBattlerValue),
					true,
					20,
					8
				});
		}

		FBattleSetup Setup;
		EBattleSetupValidationError Error = EBattleSetupValidationError::None;
		const bool bCreated = FBattleSetup::TryCreate(Input, Setup, Error);
		check(bCreated);
		return Setup;
	}

	TUniquePtr<FBattleEngine> MakeDoubleEngine(
		const int32 CurrentPP,
		const uint64 Seed = 4404,
		const EBattleTargetClass MoveTargetClass = EBattleTargetClass::FixedSpreadSet)
	{
		TUniquePtr<FBattleEngine> Engine;
		FBattleRejection Rejection;
		const bool bCreated = FBattleEngine::TryCreate(
			MakeSetup(CurrentPP, EBattleFormat::Double),
			MakeCatalog(MoveTargetClass),
			MakeUnique<FSeededBattleRandom>(Seed),
			Engine,
			Rejection);
		check(bCreated);
		return Engine;
	}

	TUniquePtr<FBattleEngine> MakeSingleEngine(
		const int32 CurrentPP,
		const uint64 Seed,
		const EBattleTargetClass MoveTargetClass,
		const EBattleMoveCategory MoveCategory = EBattleMoveCategory::Physical)
	{
		TUniquePtr<FBattleEngine> Engine;
		FBattleRejection Rejection;
		const bool bCreated = FBattleEngine::TryCreate(
			MakeSetup(CurrentPP, EBattleFormat::Single),
			MakeCatalog(MoveTargetClass, MoveCategory),
			MakeUnique<FSeededBattleRandom>(Seed),
			Engine,
			Rejection);
		check(bCreated);
		return Engine;
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

	bool LockAllConfiguredFights(FBattleEngine& Engine)
	{
		FBattleRejection Rejection;
		if (Engine.GetSnapshot().GetPhase() == EBattlePhase::Setup
			&& !Engine.TryBeginActionDecisionSequence(Rejection))
		{
			return false;
		}

		int32 Guard = 0;
		while (!Engine.GetPendingDecisionRequests().IsEmpty() && Guard++ < 8)
		{
			const TArray<FBattleDecisionRequest> Requests = Engine.GetPendingDecisionRequests();
			TArray<FBattleDecision> Decisions;
			for (const FBattleDecisionRequest& Request : Requests)
			{
				FBattleDecision Decision;
				const FMoveId MoveId = MakeDefinitionId<FMoveId>(FixedSpreadMoveName);
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

			const FBattleResolution Submitted = Engine.SubmitDecisionBatch(
				MakeBatch(Requests, MoveTemp(Decisions)));
			if (!Submitted.WasAccepted())
			{
				return false;
			}
		}

		return Guard < 8 && Engine.GetSnapshot().GetPhase() == EBattlePhase::Locked;
	}

	bool ResolveFirstConfiguredFight(
		FBattleEngine& Engine,
		FBattleResolution& OutTargetResolution)
	{
		if (!LockAllConfiguredFights(Engine))
		{
			return false;
		}

		const FBattleResolution Begun = Engine.BeginNextLockedAction();
		if (!Begun.WasAccepted())
		{
			return false;
		}
		const FBattleResolution Committed = Engine.CommitCurrentMoveAfterPreMoveGates();
		if (!Committed.WasAccepted())
		{
			return false;
		}
		OutTargetResolution = Engine.ResolveCurrentMoveTargets();
		return OutTargetResolution.WasAccepted();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC04BSinglesTargetClassesTest,
		"PokemonSolarus.Battle.C04B.TargetClasses.Singles",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FBattleC04BSinglesTargetClassesTest::RunTest(const FString& Parameters)
	{
		const FBattleBattlerTarget PlayerLeft = MakeBattlerTarget(
			EBattleSide::Player,
			EBattlePosition::Left,
			PlayerLeftBattlerValue);
		const FBattleBattlerTarget OpponentLeft = MakeBattlerTarget(
			EBattleSide::Opponent,
			EBattlePosition::Left,
			OpponentLeftBattlerValue);

		for (const EBattleTargetClass TargetClass : GetAllTargetClasses())
		{
			FBattleTargetSelectionResult Selection;
			EBattleTargetingError Error = EBattleTargetingError::None;
			const bool bBuilt = FBattleTargetResolver::TryBuildSelection(
				MakeSelectionSpec(TargetClass, MakeSinglePositions()),
				Selection,
				Error);
			TestTrue(TEXT("Each Singles target class produces a valid selection result"), bBuilt);
			TestEqual(TEXT("Selection preserves the target class"), Selection.TargetClass, TargetClass);
			TestEqual(
				TEXT("Only selected-battler classes request an explicit choice"),
				Selection.bRequiresExplicitChoice,
				RequiresExplicitChoice(TargetClass));

			switch (TargetClass)
			{
			case EBattleTargetClass::Self:
				TestEqual(TEXT("Self has one Singles candidate"), Selection.BattlerCandidates.Num(), 1);
				TestTrue(TEXT("Self selects the user"), ContainsCandidate(Selection.BattlerCandidates, PlayerLeft));
				TestEqual(TEXT("Self is automatic"), Selection.AutomaticTargets.Num(), 1);
				break;
			case EBattleTargetClass::SelectedAlly:
				TestFalse(TEXT("Singles has no selected-ally target"), Selection.bHasLegalTarget);
				TestTrue(TEXT("Singles selected-ally candidates are empty"), Selection.BattlerCandidates.IsEmpty());
				continue;
			case EBattleTargetClass::SelectedOpponent:
			case EBattleTargetClass::SelectedOtherBattler:
			case EBattleTargetClass::RandomLegalOpponent:
				TestEqual(TEXT("The living Singles opponent is the only candidate"), Selection.BattlerCandidates.Num(), 1);
				TestTrue(TEXT("The Singles opponent is present"), ContainsCandidate(Selection.BattlerCandidates, OpponentLeft));
				break;
			case EBattleTargetClass::AnySelectedBattler:
				TestEqual(TEXT("Any-selected includes both living Singles battlers"), Selection.BattlerCandidates.Num(), 2);
				TestTrue(TEXT("Any-selected starts with the user in stable order"), Selection.BattlerCandidates[0] == PlayerLeft);
				TestTrue(TEXT("Any-selected ends with the opponent in stable order"), Selection.BattlerCandidates[1] == OpponentLeft);
				break;
			case EBattleTargetClass::UserSide:
			case EBattleTargetClass::OpponentSide:
			case EBattleTargetClass::Field:
				TestEqual(TEXT("Single side or field classes have one automatic target"), Selection.AutomaticTargets.Num(), 1);
				break;
			case EBattleTargetClass::BothSides:
				TestEqual(TEXT("Both-sides has two stable automatic targets"), Selection.AutomaticTargets.Num(), 2);
				break;
			case EBattleTargetClass::FixedSpreadSet:
			case EBattleTargetClass::FixedOpponentSpreadSet:
				TestEqual(TEXT("Singles fixed spread excludes the user"), Selection.BattlerCandidates.Num(), 1);
				TestEqual(TEXT("Singles fixed spread is frozen automatically"), Selection.AutomaticTargets.Num(), 1);
				break;
			default:
				AddError(TEXT("An unexpected target class reached the Singles test"));
				continue;
			}

			TestTrue(TEXT("The Singles target class has a legal target"), Selection.bHasLegalTarget);

			FBattleBattlerTarget ExplicitTarget;
			if (TargetClass == EBattleTargetClass::SelectedOpponent
				|| TargetClass == EBattleTargetClass::AnySelectedBattler
				|| TargetClass == EBattleTargetClass::SelectedOtherBattler)
			{
				ExplicitTarget = OpponentLeft;
			}
			TArray<FBattleExpectedRandomDraw> ExpectedDraws;
			if (TargetClass == EBattleTargetClass::RandomLegalOpponent)
			{
				ExpectedDraws.Add(
					{
						0,
						0,
						0,
						FBattleTargetResolver::GetRandomLegalOpponentRulePurpose()
					});
			}
			FStrictBattleRandom Random(MoveTemp(ExpectedDraws));
			FBattleTargetResolutionResult Result;
			Error = EBattleTargetingError::None;
			const bool bResolved = FBattleTargetResolver::TryResolve(
				MakeResolutionSpec(TargetClass, MakeSinglePositions(), ExplicitTarget),
				Random,
				Result,
				Error);
			TestTrue(TEXT("Each legal Singles target class resolves"), bResolved);
			TestTrue(TEXT("Each Singles target class consumes its exact targeting RNG contract"), Random.IsExact());
			TestEqual(TEXT("Each legal Singles class resolves a target set"), Result.Outcome, EBattleTargetResolutionOutcome::Resolved);

			if (TargetClass == EBattleTargetClass::UserSide
				|| TargetClass == EBattleTargetClass::OpponentSide
				|| TargetClass == EBattleTargetClass::BothSides)
			{
				TestEqual(TEXT("Side target classes resolve typed sides"), Result.Targets[0].GetKind(), EBattleResolvedTargetKind::Side);
			}
			else if (TargetClass == EBattleTargetClass::Field)
			{
				TestEqual(TEXT("Field resolves a typed field target"), Result.Targets[0].GetKind(), EBattleResolvedTargetKind::Field);
			}
			else
			{
				TestEqual(TEXT("Battler target classes resolve typed battlers"), Result.Targets[0].GetKind(), EBattleResolvedTargetKind::Battler);
			}
		}

		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC04BNoTargetStruggleFallbackTest,
		"PokemonSolarus.Battle.C04B.Selection.NoTargetStruggleFallback",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FBattleC04BNoTargetStruggleFallbackTest::RunTest(const FString& Parameters)
	{
		const FMoveId ConfiguredMove = MakeDefinitionId<FMoveId>(FixedSpreadMoveName);
		const FMoveId Struggle = FBattleBuiltInMoveDefinitions::GetStruggleMoveId();
		TUniquePtr<FBattleEngine> Engine = MakeSingleEngine(
			10,
			4408,
			EBattleTargetClass::SelectedAlly);
		FBattleRejection Rejection;
		TestTrue(
			TEXT("A no-reserve Singles battle with only a PP-bearing ally move begins selection"),
			Engine->TryBeginActionDecisionSequence(Rejection));
		const TArray<FBattleDecisionRequest> Requests = Engine->GetPendingDecisionRequests();
		TestEqual(TEXT("The player receives one fallback request"), Requests.Num(), 1);
		if (Requests.Num() != 1)
		{
			return false;
		}

		const FBattleDecisionRequest& Request = Requests[0];
		TestEqual(TEXT("Struggle is the only legal move"), Request.GetLegalMoveIds().Num(), 1);
		TestTrue(TEXT("Struggle replaces the unusable ally move"), Request.GetLegalMoveIds().Contains(Struggle));
		TestTrue(TEXT("Fight remains a legal command"), Request.GetLegalActionKinds().Contains(EBattleActionKind::Fight));
		TestTrue(
			TEXT("The authored move retains its no-target reason"),
			HasUnavailableMove(Request, ConfiguredMove, EBattleOptionUnavailableReason::NoLegalTarget));
		const FActiveSlotId OpponentLeftSlot = MakeActiveSlotId(
			EBattleSide::Opponent,
			EBattlePosition::Left);
		TestTrue(
			TEXT("Singles Struggle exposes the living opponent"),
			Request.GetLegalMoveTargets().ContainsByPredicate(
				[Struggle, OpponentLeftSlot](const FBattleMoveTargetOption& Option)
				{
					return Option.MoveId == Struggle && Option.ActiveSlotId == OpponentLeftSlot;
				}));

		FBattleDecision Decision;
		TestTrue(
			TEXT("The fallback Struggle decision is constructible"),
			FBattleDecision::TryCreateFight(
				Request.GetStateVersion(),
				Request.GetDecisionOwnerTrainerId(),
				Request.GetActingBattlerId(),
				Struggle,
				OpponentLeftSlot,
				Decision));
		TestTrue(TEXT("The fallback request accepts Struggle"), Request.Allows(Decision, Rejection));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC04BFieldStatusEffectivenessTest,
		"PokemonSolarus.Battle.C04B.Snapshot.FieldStatusEffectiveness",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FBattleC04BFieldStatusEffectivenessTest::RunTest(const FString& Parameters)
	{
		const FMoveId ConfiguredMove = MakeDefinitionId<FMoveId>(FixedSpreadMoveName);
		TUniquePtr<FBattleEngine> Engine = MakeSingleEngine(
			10,
			4409,
			EBattleTargetClass::Field,
			EBattleMoveCategory::Status);
		FBattleRejection Rejection;
		TestTrue(TEXT("A Field Status move begins selection"), Engine->TryBeginActionDecisionSequence(Rejection));
		const FBattleSnapshot Snapshot = Engine->GetSnapshotForObserver(
			MakeNumericId<FTrainerId>(PlayerTrainerValue));
		const FBattleMoveEffectivenessKnowledge* Summary =
			Snapshot.GetMoveEffectivenessKnowledge().FindByPredicate(
				[ConfiguredMove](const FBattleMoveEffectivenessKnowledge& Knowledge)
				{
					return Knowledge.MoveId == ConfiguredMove;
				});
		TestNotNull(TEXT("The Field Status move has an effectiveness summary"), Summary);
		if (Summary != nullptr)
		{
			TestEqual(
				TEXT("Field Status effectiveness is not applicable"),
				Summary->Value,
				EBattleEffectivenessKnowledge::NotApplicable);
		}
		TestEqual(
			TEXT("A Field move creates no fake battler effectiveness rows"),
			static_cast<int32>(Algo::CountIf(
				Snapshot.GetTargetEffectivenessKnowledge(),
				[ConfiguredMove](const FBattleTargetEffectivenessKnowledge& Knowledge)
				{
					return Knowledge.MoveId == ConfiguredMove;
				})),
			0);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC04BDoublesTargetClassesTest,
		"PokemonSolarus.Battle.C04B.TargetClasses.DoublesAndStruggle",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FBattleC04BDoublesTargetClassesTest::RunTest(const FString& Parameters)
	{
		const FBattleBattlerTarget PlayerLeft = MakeBattlerTarget(
			EBattleSide::Player,
			EBattlePosition::Left,
			PlayerLeftBattlerValue);
		const FBattleBattlerTarget PlayerRight = MakeBattlerTarget(
			EBattleSide::Player,
			EBattlePosition::Right,
			PlayerRightBattlerValue);
		const FBattleBattlerTarget OpponentLeft = MakeBattlerTarget(
			EBattleSide::Opponent,
			EBattlePosition::Left,
			OpponentLeftBattlerValue);
		const FBattleBattlerTarget OpponentRight = MakeBattlerTarget(
			EBattleSide::Opponent,
			EBattlePosition::Right,
			OpponentRightBattlerValue);

		for (const EBattleTargetClass TargetClass : GetAllTargetClasses())
		{
			FBattleTargetSelectionResult Selection;
			EBattleTargetingError Error = EBattleTargetingError::None;
			const bool bBuilt = FBattleTargetResolver::TryBuildSelection(
				MakeSelectionSpec(TargetClass, MakeDoublePositions()),
				Selection,
				Error);
			TestTrue(TEXT("Each Doubles target class produces a valid selection result"), bBuilt);
			TestTrue(TEXT("Every Doubles target class has a legal target"), Selection.bHasLegalTarget);

			FBattleBattlerTarget ExplicitTarget;
			int32 ExpectedCandidateCount = 0;
			int32 ExpectedResolvedCount = 1;
			switch (TargetClass)
			{
			case EBattleTargetClass::Self:
				ExpectedCandidateCount = 1;
				break;
			case EBattleTargetClass::SelectedAlly:
				ExpectedCandidateCount = 1;
				ExplicitTarget = PlayerRight;
				break;
			case EBattleTargetClass::SelectedOpponent:
				ExpectedCandidateCount = 2;
				ExplicitTarget = OpponentLeft;
				break;
			case EBattleTargetClass::AnySelectedBattler:
				ExpectedCandidateCount = 4;
				ExplicitTarget = OpponentRight;
				break;
			case EBattleTargetClass::SelectedOtherBattler:
				ExpectedCandidateCount = 3;
				ExplicitTarget = PlayerRight;
				break;
			case EBattleTargetClass::RandomLegalOpponent:
				ExpectedCandidateCount = 2;
				break;
			case EBattleTargetClass::BothSides:
				ExpectedResolvedCount = 2;
				break;
			case EBattleTargetClass::FixedSpreadSet:
				ExpectedCandidateCount = 3;
				ExpectedResolvedCount = 3;
				break;
			case EBattleTargetClass::FixedOpponentSpreadSet:
				ExpectedCandidateCount = 2;
				ExpectedResolvedCount = 2;
				break;
			default:
				break;
			}

			TestEqual(TEXT("Doubles candidate count matches the target class"), Selection.BattlerCandidates.Num(), ExpectedCandidateCount);
			TArray<FBattleExpectedRandomDraw> ExpectedDraws;
			if (TargetClass == EBattleTargetClass::RandomLegalOpponent)
			{
				ExpectedDraws.Add(
					{
						0,
						1,
						1,
						FBattleTargetResolver::GetRandomLegalOpponentRulePurpose()
					});
			}
			FStrictBattleRandom Random(MoveTemp(ExpectedDraws));
			FBattleTargetResolutionResult Result;
			const bool bResolved = FBattleTargetResolver::TryResolve(
				MakeResolutionSpec(TargetClass, MakeDoublePositions(), ExplicitTarget),
				Random,
				Result,
				Error);
			TestTrue(TEXT("Each Doubles target class resolves"), bResolved);
			TestTrue(TEXT("Each Doubles target class consumes its exact targeting RNG contract"), Random.IsExact());
			TestEqual(TEXT("Each Doubles target class resolves successfully"), Result.Outcome, EBattleTargetResolutionOutcome::Resolved);
			TestEqual(TEXT("Doubles resolved-target count matches the class"), Result.Targets.Num(), ExpectedResolvedCount);

			if (TargetClass == EBattleTargetClass::FixedSpreadSet)
			{
				TestTrue(TEXT("Friendly-fire spread begins with the ally"), Result.Targets[0].GetBattler() == PlayerRight);
				TestTrue(TEXT("Friendly-fire spread then targets opponent Left"), Result.Targets[1].GetBattler() == OpponentLeft);
				TestTrue(TEXT("Friendly-fire spread ends with opponent Right"), Result.Targets[2].GetBattler() == OpponentRight);
			}
			else if (TargetClass == EBattleTargetClass::FixedOpponentSpreadSet)
			{
				TestTrue(TEXT("Opponent-only spread begins with opponent Left"), Result.Targets[0].GetBattler() == OpponentLeft);
				TestTrue(TEXT("Opponent-only spread ends with opponent Right"), Result.Targets[1].GetBattler() == OpponentRight);
			}
			else if (TargetClass == EBattleTargetClass::RandomLegalOpponent)
			{
				TestTrue(TEXT("Random index one selects stable opponent Right"), Result.Targets[0].GetBattler() == OpponentRight);
			}
			else if (TargetClass == EBattleTargetClass::Self)
			{
				TestTrue(TEXT("Doubles self resolves the user"), Result.Targets[0].GetBattler() == PlayerLeft);
			}
		}

		TUniquePtr<FBattleEngine> StruggleEngine = MakeDoubleEngine(0);
		FBattleRejection Rejection;
		TestTrue(TEXT("Zero-PP Doubles begins target selection"), StruggleEngine->TryBeginActionDecisionSequence(Rejection));
		const TArray<FBattleDecisionRequest> Requests = StruggleEngine->GetPendingDecisionRequests();
		TestEqual(TEXT("The player receives both Doubles requests"), Requests.Num(), 2);
		for (const FBattleDecisionRequest& Request : Requests)
		{
			const FMoveId Struggle = FBattleBuiltInMoveDefinitions::GetStruggleMoveId();
			TestTrue(TEXT("Struggle is the legal zero-PP move"), Request.GetLegalMoveIds().Contains(Struggle));
			TestFalse(TEXT("Doubles Struggle is not automatically targeted"), Request.GetAutomaticallyTargetedMoveIds().Contains(Struggle));
			const int32 StrugglePairs = Algo::CountIf(
				Request.GetLegalMoveTargets(),
				[Struggle](const FBattleMoveTargetOption& Option)
				{
					return Option.MoveId == Struggle;
				});
			TestEqual(TEXT("Doubles Struggle exposes both opponent choices"), StrugglePairs, 2);

			FBattleDecision Automatic;
			const bool bAutomaticCreated = FBattleDecision::TryCreateAutomaticallyTargetedFight(
				Request.GetStateVersion(),
				Request.GetDecisionOwnerTrainerId(),
				Request.GetActingBattlerId(),
				Struggle,
				Automatic);
			TestTrue(TEXT("An automatic-shaped decision can be constructed for rejection testing"), bAutomaticCreated);
			TestFalse(TEXT("The request rejects automatic Doubles Struggle"), Request.Allows(Automatic, Rejection));
			TestEqual(TEXT("Missing Struggle choice is an illegal target"), Rejection.Reason, EBattleRejectionReason::IllegalTarget);

			FBattleDecision Explicit;
			const bool bExplicitCreated = FBattleDecision::TryCreateFight(
				Request.GetStateVersion(),
				Request.GetDecisionOwnerTrainerId(),
				Request.GetActingBattlerId(),
				Struggle,
				OpponentLeft.ActiveSlotId,
				Explicit);
			TestTrue(TEXT("An explicit Doubles Struggle decision is constructible"), bExplicitCreated);
			TestTrue(TEXT("The request accepts an explicit legal Struggle target"), Request.Allows(Explicit, Rejection));
		}
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC10TargetStableCandidatesTest,
		"PokemonSolarus.Battle.C04B.C10Targets.Targeting.SinglesAndDoublesStableCandidates",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

	bool FBattleC10TargetStableCandidatesTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		const FBattleBattlerTarget PlayerLeft = MakeBattlerTarget(
			EBattleSide::Player,
			EBattlePosition::Left,
			PlayerLeftBattlerValue);
		const FBattleBattlerTarget PlayerRight = MakeBattlerTarget(
			EBattleSide::Player,
			EBattlePosition::Right,
			PlayerRightBattlerValue);
		const FBattleBattlerTarget OpponentLeft = MakeBattlerTarget(
			EBattleSide::Opponent,
			EBattlePosition::Left,
			OpponentLeftBattlerValue);
		const FBattleBattlerTarget OpponentRight = MakeBattlerTarget(
			EBattleSide::Opponent,
			EBattlePosition::Right,
			OpponentRightBattlerValue);

		FBattleTargetSelectionResult Selection;
		EBattleTargetingError Error = EBattleTargetingError::None;
		TestTrue(
			TEXT("Selected Other Battler builds a Singles choice"),
			FBattleTargetResolver::TryBuildSelection(
				MakeSelectionSpec(
					EBattleTargetClass::SelectedOtherBattler,
					MakeSinglePositions()),
				Selection,
				Error));
		TestTrue(
			TEXT("Singles selected-other excludes self and exposes only the opponent"),
			Selection.bRequiresExplicitChoice
				&& Selection.BattlerCandidates == TArray<FBattleBattlerTarget>({OpponentLeft})
				&& !Selection.BattlerCandidates.Contains(PlayerLeft)
				&& Selection.AutomaticTargets.IsEmpty());

		TestTrue(
			TEXT("Fixed Opponent Spread Set builds a Singles automatic target"),
			FBattleTargetResolver::TryBuildSelection(
				MakeSelectionSpec(
					EBattleTargetClass::FixedOpponentSpreadSet,
					MakeSinglePositions()),
				Selection,
				Error));
		TestTrue(
			TEXT("Singles opponent spread contains exactly one automatic opponent"),
			!Selection.bRequiresExplicitChoice
				&& Selection.BattlerCandidates == TArray<FBattleBattlerTarget>({OpponentLeft})
				&& Selection.AutomaticTargets.Num() == 1
				&& Selection.AutomaticTargets[0].GetBattler() == OpponentLeft);

		TestTrue(
			TEXT("Selected Other Battler builds a Doubles choice"),
			FBattleTargetResolver::TryBuildSelection(
				MakeSelectionSpec(
					EBattleTargetClass::SelectedOtherBattler,
					MakeDoublePositions()),
				Selection,
				Error));
		TestTrue(
			TEXT("Doubles selected-other is canonical ally then opponents and never self"),
			Selection.BattlerCandidates
				== TArray<FBattleBattlerTarget>({PlayerRight, OpponentLeft, OpponentRight})
				&& !Selection.BattlerCandidates.Contains(PlayerLeft));

		TestTrue(
			TEXT("Fixed Opponent Spread Set builds a Doubles automatic set"),
			FBattleTargetResolver::TryBuildSelection(
				MakeSelectionSpec(
					EBattleTargetClass::FixedOpponentSpreadSet,
					MakeDoublePositions()),
				Selection,
				Error));
		TestTrue(
			TEXT("Doubles opponent spread is canonical opponent Left then Right with no ally"),
			Selection.BattlerCandidates
				== TArray<FBattleBattlerTarget>({OpponentLeft, OpponentRight})
				&& Selection.AutomaticTargets.Num() == 2
				&& Selection.AutomaticTargets[0].GetBattler() == OpponentLeft
				&& Selection.AutomaticTargets[1].GetBattler() == OpponentRight
				&& !Selection.BattlerCandidates.Contains(PlayerRight));

		TArray<FBattleTargetPositionFacts> UnavailablePositions = MakeDoublePositions();
		for (FBattleTargetPositionFacts& Position : UnavailablePositions)
		{
			if (Position.ActiveSlotId == OpponentLeft.ActiveSlotId)
			{
				Position.State = EBattleTargetPositionState::Fainted;
			}
			else if (Position.ActiveSlotId == OpponentRight.ActiveSlotId)
			{
				Position.State = EBattleTargetPositionState::Removed;
			}
		}
		TestTrue(
			TEXT("Opponent spread accepts structurally unavailable opponent positions"),
			FBattleTargetResolver::TryBuildSelection(
				MakeSelectionSpec(
					EBattleTargetClass::FixedOpponentSpreadSet,
					MoveTemp(UnavailablePositions)),
				Selection,
				Error));
		TestTrue(
			TEXT("Fainted and removed opponents are excluded without admitting the ally"),
			!Selection.bHasLegalTarget
				&& Selection.BattlerCandidates.IsEmpty()
				&& Selection.AutomaticTargets.IsEmpty());
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC10TargetEventIdentityValidationTest,
		"PokemonSolarus.Battle.C04B.C10Targets.Event.SelectedOtherIdentityAliases",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

	bool FBattleC10TargetEventIdentityValidationTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		const FBattleEventTarget ExactSource = MakeEventBattlerTarget(
			PlayerTrainerValue,
			EBattleSide::Player,
			EBattlePosition::Left,
			PlayerLeftBattlerValue);
		const FBattleEventTarget SourceBattlerInOtherSlot = MakeEventBattlerTarget(
			PlayerTrainerValue,
			EBattleSide::Player,
			EBattlePosition::Right,
			PlayerLeftBattlerValue);
		const FBattleEventTarget OtherBattlerInSourceSlot = MakeEventBattlerTarget(
			PlayerTrainerValue,
			EBattleSide::Player,
			EBattlePosition::Left,
			PlayerRightBattlerValue);
		const FBattleEventTarget Ally = MakeEventBattlerTarget(
			PlayerTrainerValue,
			EBattleSide::Player,
			EBattlePosition::Right,
			PlayerRightBattlerValue);
		const FBattleEventTarget Opponent = MakeEventBattlerTarget(
			OpponentTrainerValue,
			EBattleSide::Opponent,
			EBattlePosition::Left,
			OpponentLeftBattlerValue);

		auto RejectsTarget = [this](const TCHAR* What, const FBattleEventTarget& Target)
		{
			FBattleEvent Event;
			const bool bCreated = FBattleEvent::TryCreate(
				MakeSelectedOtherEventSpec(Target),
				Event);
			return TestFalse(What, bCreated)
				&& TestFalse(TEXT("A rejected SelectedOther event remains invalid"), Event.IsValid());
		};

		bool bValid = RejectsTarget(
			TEXT("The Event boundary rejects the exact source identity"),
			ExactSource);
		bValid &= RejectsTarget(
			TEXT("The Event boundary rejects the source battler ID in another slot"),
			SourceBattlerInOtherSlot);
		bValid &= RejectsTarget(
			TEXT("The Event boundary rejects another battler ID in the source slot"),
			OtherBattlerInSourceSlot);

		FBattleEvent AllyEvent;
		bValid &= TestTrue(
			TEXT("The Event boundary accepts a canonical SelectedOther ally"),
			FBattleEvent::TryCreate(MakeSelectedOtherEventSpec(Ally), AllyEvent));
		FBattleEvent OpponentEvent;
		bValid &= TestTrue(
			TEXT("The Event boundary accepts a canonical SelectedOther opponent"),
			FBattleEvent::TryCreate(MakeSelectedOtherEventSpec(Opponent), OpponentEvent));
		return bValid;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC10TargetResolutionContractTest,
		"PokemonSolarus.Battle.C04B.C10Targets.Targeting.ResolutionFallbackAndNoRng",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

	bool FBattleC10TargetResolutionContractTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		const FBattleBattlerTarget PlayerLeft = MakeBattlerTarget(
			EBattleSide::Player,
			EBattlePosition::Left,
			PlayerLeftBattlerValue);
		const FBattleBattlerTarget PlayerRight = MakeBattlerTarget(
			EBattleSide::Player,
			EBattlePosition::Right,
			PlayerRightBattlerValue);
		const FBattleBattlerTarget OpponentLeft = MakeBattlerTarget(
			EBattleSide::Opponent,
			EBattlePosition::Left,
			OpponentLeftBattlerValue);
		const FBattleBattlerTarget OpponentRight = MakeBattlerTarget(
			EBattleSide::Opponent,
			EBattlePosition::Right,
			OpponentRightBattlerValue);

		FStrictBattleRandom NoTargetRandom({});
		FBattleTargetResolutionResult Result;
		EBattleTargetingError Error = EBattleTargetingError::None;
		TestFalse(
			TEXT("Selected Other Battler rejects the exact user"),
			FBattleTargetResolver::TryResolve(
				MakeResolutionSpec(
					EBattleTargetClass::SelectedOtherBattler,
					MakeDoublePositions(),
					PlayerLeft),
				NoTargetRandom,
				Result,
				Error));
		TestEqual(TEXT("Selecting self reports InvalidSelection"), Error,
			EBattleTargetingError::InvalidSelection);

		Error = EBattleTargetingError::None;
		TestTrue(
			TEXT("Selected Other Battler resolves a living ally explicitly"),
			FBattleTargetResolver::TryResolve(
				MakeResolutionSpec(
					EBattleTargetClass::SelectedOtherBattler,
					MakeDoublePositions(),
					PlayerRight),
				NoTargetRandom,
				Result,
				Error));
		TestTrue(
			TEXT("The explicit ally resolves without fallback or target RNG"),
			Result.Outcome == EBattleTargetResolutionOutcome::Resolved
				&& Result.Targets.Num() == 1
				&& Result.Targets[0].GetBattler() == PlayerRight
				&& !Result.bUsedFaintedTargetFallback
				&& NoTargetRandom.IsExact());

		TArray<FBattleTargetPositionFacts> FaintedOpponentPositions = MakeDoublePositions();
		for (FBattleTargetPositionFacts& Position : FaintedOpponentPositions)
		{
			if (Position.ActiveSlotId == OpponentLeft.ActiveSlotId)
			{
				Position.State = EBattleTargetPositionState::Fainted;
			}
		}
		Error = EBattleTargetingError::None;
		TestTrue(
			TEXT("Selected Other Battler resolves a fainted opponent through the existing fallback"),
			FBattleTargetResolver::TryResolve(
				MakeResolutionSpec(
					EBattleTargetClass::SelectedOtherBattler,
					MoveTemp(FaintedOpponentPositions),
					OpponentLeft),
				NoTargetRandom,
				Result,
				Error));
		TestTrue(
			TEXT("Fainted-opponent fallback stays on the opponent side and uses no RNG"),
			Result.Outcome == EBattleTargetResolutionOutcome::Resolved
				&& Result.bWasRedirected
				&& Result.bUsedFaintedTargetFallback
				&& Result.Targets.Num() == 1
				&& Result.Targets[0].GetBattler() == OpponentRight
				&& NoTargetRandom.IsExact());

		TArray<FBattleTargetPositionFacts> FaintedAllyPositions = MakeDoublePositions();
		for (FBattleTargetPositionFacts& Position : FaintedAllyPositions)
		{
			if (Position.ActiveSlotId == PlayerRight.ActiveSlotId)
			{
				Position.State = EBattleTargetPositionState::Fainted;
			}
		}
		Error = EBattleTargetingError::None;
		TestTrue(
			TEXT("A fainted selected ally is a valid no-target resolution"),
			FBattleTargetResolver::TryResolve(
				MakeResolutionSpec(
					EBattleTargetClass::SelectedOtherBattler,
					MoveTemp(FaintedAllyPositions),
					PlayerRight),
				NoTargetRandom,
				Result,
				Error));
		TestTrue(
			TEXT("Selected-other never crosses sides to replace a fainted ally"),
			Result.Outcome == EBattleTargetResolutionOutcome::NoLegalTarget
				&& Result.Targets.IsEmpty()
				&& !Result.bWasRedirected
				&& !Result.bUsedFaintedTargetFallback);

		FBattleTargetResolutionSpec SpreadSpec = MakeResolutionSpec(
			EBattleTargetClass::FixedOpponentSpreadSet,
			MakeDoublePositions());
		SpreadSpec.RedirectionProposals.Add({PlayerRight});
		Error = EBattleTargetingError::None;
		TestTrue(
			TEXT("Fixed Opponent Spread Set resolves the Doubles opponent set"),
			FBattleTargetResolver::TryResolve(
				SpreadSpec,
				NoTargetRandom,
				Result,
				Error));
		TestTrue(
			TEXT("Opponent spread ignores redirection and freezes both opponents in canonical order"),
			Result.Outcome == EBattleTargetResolutionOutcome::Resolved
				&& !Result.bWasRedirected
				&& Result.Targets.Num() == 2
				&& Result.Targets[0].GetBattler() == OpponentLeft
				&& Result.Targets[1].GetBattler() == OpponentRight
				&& NoTargetRandom.IsExact());

		TArray<FBattleTargetPositionFacts> NoOpponentPositions = MakeDoublePositions();
		for (FBattleTargetPositionFacts& Position : NoOpponentPositions)
		{
			if (Position.ActiveSlotId.GetSide() == EBattleSide::Opponent)
			{
				Position.State = EBattleTargetPositionState::Fainted;
			}
		}
		Error = EBattleTargetingError::None;
		TestTrue(
			TEXT("An empty opponent spread uses the existing no-target path"),
			FBattleTargetResolver::TryResolve(
				MakeResolutionSpec(
					EBattleTargetClass::FixedOpponentSpreadSet,
					MoveTemp(NoOpponentPositions)),
				NoTargetRandom,
				Result,
				Error));
		TestTrue(
			TEXT("Empty opponent spread has no targets, redirection, fallback, or RNG"),
			Result.Outcome == EBattleTargetResolutionOutcome::NoLegalTarget
				&& Result.Targets.IsEmpty()
				&& !Result.bWasRedirected
				&& !Result.bUsedFaintedTargetFallback
				&& NoTargetRandom.IsExact());
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC10SelectedOtherRedirectionTest,
		"PokemonSolarus.Battle.C04B.C10Targets.Targeting.SelectedOtherRedirection",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

	bool FBattleC10SelectedOtherRedirectionTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		const FBattleBattlerTarget PlayerLeft = MakeBattlerTarget(
			EBattleSide::Player,
			EBattlePosition::Left,
			PlayerLeftBattlerValue);
		const FBattleBattlerTarget PlayerRight = MakeBattlerTarget(
			EBattleSide::Player,
			EBattlePosition::Right,
			PlayerRightBattlerValue);
		const FBattleBattlerTarget OpponentLeft = MakeBattlerTarget(
			EBattleSide::Opponent,
			EBattlePosition::Left,
			OpponentLeftBattlerValue);
		const FBattleBattlerTarget OpponentRight = MakeBattlerTarget(
			EBattleSide::Opponent,
			EBattlePosition::Right,
			OpponentRightBattlerValue);

		FBattleTargetResolutionSpec Spec = MakeResolutionSpec(
			EBattleTargetClass::SelectedOtherBattler,
			MakeDoublePositions(),
			OpponentLeft);
		Spec.RedirectionProposals.Add({PlayerLeft});
		Spec.RedirectionProposals.Add({OpponentLeft});
		Spec.RedirectionProposals.Add({PlayerRight});
		Spec.RedirectionProposals.Add({OpponentRight});

		FStrictBattleRandom NoTargetRandom({});
		FBattleTargetResolutionResult Result;
		EBattleTargetingError Error = EBattleTargetingError::None;
		TestTrue(
			TEXT("SelectedOther evaluates ordered redirection proposals"),
			FBattleTargetResolver::TryResolve(Spec, NoTargetRandom, Result, Error));
		TestTrue(
			TEXT("SelectedOther skips self and no-op proposals before choosing the legal ally"),
			Result.Outcome == EBattleTargetResolutionOutcome::Resolved
				&& Result.Targets.Num() == 1
				&& Result.Targets[0].GetKind() == EBattleResolvedTargetKind::Battler
				&& Result.Targets[0].GetBattler() == PlayerRight);
		TestTrue(TEXT("SelectedOther reports the effective rule redirection"), Result.bWasRedirected);
		TestFalse(
			TEXT("SelectedOther rule redirection is not a fainted-target fallback"),
			Result.bUsedFaintedTargetFallback);
		TestTrue(
			TEXT("SelectedOther redirection consumes no target-selection RNG"),
			NoTargetRandom.IsExact() && NoTargetRandom.GetTrace().IsEmpty());
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC10SelectedOtherPublicEngineTest,
		"PokemonSolarus.Battle.C04B.C10Targets.Engine.SelectedOtherPublicQueueEventReplay",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FBattleC10SelectedOtherPublicEngineTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		const FMoveId MoveId = MakeDefinitionId<FMoveId>(FixedSpreadMoveName);
		const FBattlerId PlayerLeftId = MakeNumericId<FBattlerId>(PlayerLeftBattlerValue);
		const FBattlerId OpponentRightId = MakeNumericId<FBattlerId>(OpponentRightBattlerValue);
		const FActiveSlotId PlayerLeftSlot = MakeActiveSlotId(
			EBattleSide::Player,
			EBattlePosition::Left);
		const FActiveSlotId PlayerRightSlot = MakeActiveSlotId(
			EBattleSide::Player,
			EBattlePosition::Right);
		const FActiveSlotId OpponentLeftSlot = MakeActiveSlotId(
			EBattleSide::Opponent,
			EBattlePosition::Left);
		const FActiveSlotId OpponentRightSlot = MakeActiveSlotId(
			EBattleSide::Opponent,
			EBattlePosition::Right);

		TUniquePtr<FBattleEngine> Engine = MakeDoubleEngine(
			10,
			4410,
			EBattleTargetClass::SelectedOtherBattler);
		FBattleRejection Rejection;
		if (!TestTrue(
				TEXT("The catalog-authored SelectedOther battle begins through the public API"),
				Engine->TryBeginActionDecisionSequence(Rejection)))
		{
			return false;
		}

		const TArray<FBattleDecisionRequest> PlayerRequests =
			Engine->GetPendingDecisionRequests();
		TestEqual(TEXT("The player receives both active decision requests"), PlayerRequests.Num(), 2);
		const FBattleDecisionRequest* PlayerLeftRequest = PlayerRequests.FindByPredicate(
			[PlayerLeftId](const FBattleDecisionRequest& Request)
			{
				return Request.GetActingBattlerId() == PlayerLeftId;
			});
		if (!TestNotNull(TEXT("The Player Left SelectedOther request exists"), PlayerLeftRequest))
		{
			return false;
		}

		auto HasMoveTarget = [PlayerLeftRequest, MoveId](const FActiveSlotId Slot)
		{
			return PlayerLeftRequest->GetLegalMoveTargets().ContainsByPredicate(
				[MoveId, Slot](const FBattleMoveTargetOption& Option)
				{
					return Option.MoveId == MoveId && Option.ActiveSlotId == Slot;
				});
		};
		const int32 SelectedOtherOptionCount = Algo::CountIf(
			PlayerLeftRequest->GetLegalMoveTargets(),
			[MoveId](const FBattleMoveTargetOption& Option)
			{
				return Option.MoveId == MoveId;
			});
		TestTrue(
			TEXT("SelectedOther exposes the ally and both living opponents, but never the actor"),
			SelectedOtherOptionCount == 3
				&& HasMoveTarget(PlayerRightSlot)
				&& HasMoveTarget(OpponentLeftSlot)
				&& HasMoveTarget(OpponentRightSlot)
				&& !HasMoveTarget(PlayerLeftSlot));
		TestFalse(
			TEXT("SelectedOther is not classified as automatically targeted"),
			PlayerLeftRequest->GetAutomaticallyTargetedMoveIds().Contains(MoveId));

		TArray<FBattleDecision> PlayerDecisions;
		for (const FBattleDecisionRequest& Request : PlayerRequests)
		{
			const FActiveSlotId TargetSlot = Request.GetActingBattlerId() == PlayerLeftId
				? OpponentRightSlot
				: OpponentLeftSlot;
			FBattleDecision Decision;
			if (!TestTrue(
					TEXT("Each SelectedOther player choice is created with an explicit target"),
					FBattleDecision::TryCreateFight(
						Request.GetStateVersion(),
						Request.GetDecisionOwnerTrainerId(),
						Request.GetActingBattlerId(),
						MoveId,
						TargetSlot,
						Decision)))
			{
				return false;
			}
			PlayerDecisions.Add(Decision);
		}
		const FBattleResolution PlayerSubmitted = Engine->SubmitDecisionBatch(
			MakeBatch(PlayerRequests, MoveTemp(PlayerDecisions)));
		if (!TestTrue(
				TEXT("The public engine accepts the explicit SelectedOther player batch"),
				PlayerSubmitted.WasAccepted())
			|| !TestTrue(
				TEXT("The remaining configured explicit fights lock through the public engine"),
				LockAllConfiguredFights(*Engine)))
		{
			return false;
		}

		const TArray<FBattleLockedAction> LockedActions = Engine->GetLockedActions();
		const FBattleLockedAction* PlayerLeftAction = LockedActions.FindByPredicate(
			[PlayerLeftId](const FBattleLockedAction& Action)
			{
				return Action.Decision.GetActingBattlerId() == PlayerLeftId;
			});
		if (!TestNotNull(TEXT("The Player Left action is present in the public locked queue"), PlayerLeftAction))
		{
			return false;
		}
		TestTrue(
			TEXT("The queue retains target class 10 and the canonical explicit opponent identity"),
			PlayerLeftAction->TargetClass == EBattleTargetClass::SelectedOtherBattler
				&& static_cast<uint8>(PlayerLeftAction->TargetClass) == 10
				&& PlayerLeftAction->Decision.GetActiveTargetId() == OpponentRightSlot
				&& PlayerLeftAction->SelectedTargetBattlerId == OpponentRightId
				&& !PlayerLeftAction->TargetResolution.IsSet());

		if (!TestTrue(
				TEXT("The SelectedOther action starts through the public checkpoint"),
				Engine->BeginNextLockedAction().WasAccepted())
			|| !TestTrue(
				TEXT("The SelectedOther move commits through the public pre-move checkpoint"),
				Engine->CommitCurrentMoveAfterPreMoveGates().WasAccepted()))
		{
			return false;
		}
		const int32 RandomDrawsBeforeTargets = Engine->ExportRandomTrace().Num();
		const FBattleResolution TargetResolution = Engine->ResolveCurrentMoveTargets();
		TestTrue(
			TEXT("The public SelectedOther target checkpoint resolves"),
			TargetResolution.WasAccepted());
		TestEqual(
			TEXT("SelectedOther target resolution consumes no RNG draw"),
			Engine->ExportRandomTrace().Num(),
			RandomDrawsBeforeTargets);
		TestEqual(
			TEXT("The complete public flow needs no RNG before target resolution"),
			RandomDrawsBeforeTargets,
			0);

		const TOptional<FBattleLockedAction> Current = Engine->GetCurrentLockedAction();
		TestTrue(
			TEXT("The resolved public action retains the selected canonical opponent"),
			Current.IsSet()
				&& Current->TargetClass == EBattleTargetClass::SelectedOtherBattler
				&& Current->SelectedTargetBattlerId == OpponentRightId
				&& Current->TargetResolution.IsSet()
				&& Current->TargetResolution.GetValue().TargetClass
					== EBattleTargetClass::SelectedOtherBattler
				&& Current->TargetResolution.GetValue().Outcome
					== EBattleTargetResolutionOutcome::Resolved
				&& Current->TargetResolution.GetValue().Targets.Num() == 1
				&& Current->TargetResolution.GetValue().Targets[0].GetKind()
					== EBattleResolvedTargetKind::Battler
				&& Current->TargetResolution.GetValue().Targets[0].GetBattler().BattlerId
					== OpponentRightId
				&& Current->TargetResolution.GetValue().Targets[0].GetBattler().ActiveSlotId
					== OpponentRightSlot);

		const FBattleEvent* TargetsEvent = TargetResolution.GetEvents().FindByPredicate(
			[](const FBattleEvent& Event)
			{
				return Event.GetType() == EBattleEventType::TargetsResolved;
			});
		TestTrue(
			TEXT("The public target event preserves class 10 and the selected canonical identity"),
			TargetsEvent != nullptr
				&& TargetsEvent->GetTargetResolution().IsSet()
				&& TargetsEvent->GetTargetResolution().GetValue().TargetClass
					== EBattleTargetClass::SelectedOtherBattler
				&& TargetsEvent->GetTargets().Num() == 1
				&& TargetsEvent->GetTargets()[0].BattlerId == OpponentRightId
				&& TargetsEvent->GetTargets()[0].ActiveSlotId == OpponentRightSlot);

		const FBattleReplayRecord Replay = Engine->ExportReplayRecord();
		const bool bHasSubmittedDecision = Replay.GetInputs().Decisions.ContainsByPredicate(
			[PlayerLeftId, MoveId, OpponentRightSlot](const FBattleDecision& Decision)
			{
				return Decision.GetActingBattlerId() == PlayerLeftId
					&& Decision.GetMoveId() == MoveId
					&& Decision.GetActiveTargetId() == OpponentRightSlot;
			});
		int32 MatchingReplayResolutionCount = 0;
		const FBattleResolution* ReplayTargetResolution = nullptr;
		for (const FBattleResolution& Resolution : Replay.GetResolutions())
		{
			if (Resolution.GetResolutionId() == TargetResolution.GetResolutionId())
			{
				++MatchingReplayResolutionCount;
				ReplayTargetResolution = &Resolution;
			}
		}
		const FBattleEvent* ReplayTargetsEvent = ReplayTargetResolution != nullptr
			? ReplayTargetResolution->GetEvents().FindByPredicate(
				[](const FBattleEvent& Event)
				{
					return Event.GetType() == EBattleEventType::TargetsResolved;
				})
			: nullptr;
		TestTrue(
			TEXT("Replay keeps the explicit decision and one canonical SelectedOther target fact"),
			Replay.IsValid()
				&& Replay.GetSchemaVersion() == 6
				&& bHasSubmittedDecision
				&& MatchingReplayResolutionCount == 1
				&& ReplayTargetsEvent != nullptr
				&& ReplayTargetsEvent->GetTargetResolution().IsSet()
				&& ReplayTargetsEvent->GetTargetResolution().GetValue().TargetClass
					== EBattleTargetClass::SelectedOtherBattler
				&& ReplayTargetsEvent->GetTargets().Num() == 1
				&& ReplayTargetsEvent->GetTargets()[0].BattlerId == OpponentRightId
				&& ReplayTargetsEvent->GetTargets()[0].ActiveSlotId == OpponentRightSlot
				&& Replay.GetRandomTrace().IsEmpty());
		TArray<uint8> ReplayBytes;
		TestTrue(
			TEXT("The SelectedOther public replay serializes canonically"),
			FBattleReplaySerializer::TrySerializeCanonical(Replay, ReplayBytes, Rejection)
				&& !ReplayBytes.IsEmpty());
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC04BLifecycleTest,
		"PokemonSolarus.Battle.C04B.Lifecycle.SemiInvulnerability",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FBattleC04BLifecycleTest::RunTest(const FString& Parameters)
	{
		const FActiveSlotId OpponentRightSlot = MakeActiveSlotId(
			EBattleSide::Opponent,
			EBattlePosition::Right);
		const FBattleBattlerTarget OpponentRight = MakeBattlerTarget(
			EBattleSide::Opponent,
			EBattlePosition::Right,
			OpponentRightBattlerValue);
		const TArray<EBattleTargetPositionState> ExcludedStates = {
			EBattleTargetPositionState::Empty,
			EBattleTargetPositionState::Fainted,
			EBattleTargetPositionState::Captured,
			EBattleTargetPositionState::Removed
		};

		for (const EBattleTargetPositionState State : ExcludedStates)
		{
			TArray<FBattleTargetPositionFacts> Positions = MakeDoublePositions();
			Positions[0] = MakePosition(
				EBattleSide::Opponent,
				EBattlePosition::Right,
				OpponentRightBattlerValue,
				State);
			FBattleTargetSelectionResult Selection;
			EBattleTargetingError Error = EBattleTargetingError::None;
			const bool bBuilt = FBattleTargetResolver::TryBuildSelection(
				MakeSelectionSpec(EBattleTargetClass::AnySelectedBattler, MoveTemp(Positions)),
				Selection,
				Error);
			TestTrue(TEXT("Each lifecycle variant is structurally valid"), bBuilt);
			TestFalse(
				TEXT("Empty, fainted, captured, and removed positions are excluded from new choices"),
				Selection.BattlerCandidates.ContainsByPredicate(
					[OpponentRightSlot](const FBattleBattlerTarget& Candidate)
					{
						return Candidate.ActiveSlotId == OpponentRightSlot;
					}));
		}

		TArray<FBattleTargetPositionFacts> AirbornePositions = MakeDoublePositions();
		AirbornePositions[0].bSemiInvulnerable = true;
		FBattleTargetSelectionResult AirborneSelection;
		EBattleTargetingError Error = EBattleTargetingError::None;
		TestTrue(
			TEXT("A semi-invulnerable target remains selectable"),
			FBattleTargetResolver::TryBuildSelection(
				MakeSelectionSpec(EBattleTargetClass::SelectedOpponent, AirbornePositions),
				AirborneSelection,
				Error));
		TestTrue(
			TEXT("The semi-invulnerable target stays in the legal choices"),
			ContainsCandidate(AirborneSelection.BattlerCandidates, OpponentRight));

		FStrictBattleRandom AirborneRandom({});
		FBattleTargetResolutionResult AirborneResult;
		TestTrue(
			TEXT("Target resolution does not perform the later reachability check"),
			FBattleTargetResolver::TryResolve(
				MakeResolutionSpec(
					EBattleTargetClass::SelectedOpponent,
					AirbornePositions,
					OpponentRight),
				AirborneRandom,
				AirborneResult,
				Error));
		TestTrue(TEXT("Semi-invulnerable target resolution consumes no RNG"), AirborneRandom.IsExact());

		TArray<FBattleTargetPositionFacts> ReturnedPositions = AirbornePositions;
		ReturnedPositions[0].bSemiInvulnerable = false;
		FStrictBattleRandom ReturnedRandom({});
		FBattleTargetResolutionResult ReturnedResult;
		TestTrue(
			TEXT("A target that returned also resolves normally"),
			FBattleTargetResolver::TryResolve(
				MakeResolutionSpec(
					EBattleTargetClass::SelectedOpponent,
					ReturnedPositions,
					OpponentRight),
				ReturnedRandom,
				ReturnedResult,
				Error));
		TestTrue(TEXT("Returned target resolution consumes no RNG"), ReturnedRandom.IsExact());
		TestTrue(TEXT("Both lifecycle paths freeze the same target"), AirborneResult.Targets[0] == ReturnedResult.Targets[0]);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC04BFaintedAndCapturedTest,
		"PokemonSolarus.Battle.C04B.Resolution.FaintedFallbackAndCapturedCancellation",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FBattleC04BFaintedAndCapturedTest::RunTest(const FString& Parameters)
	{
		const FBattleBattlerTarget OpponentLeft = MakeBattlerTarget(
			EBattleSide::Opponent,
			EBattlePosition::Left,
			OpponentLeftBattlerValue);
		const FBattleBattlerTarget OpponentRight = MakeBattlerTarget(
			EBattleSide::Opponent,
			EBattlePosition::Right,
			OpponentRightBattlerValue);
		TArray<FBattleTargetPositionFacts> FaintedPositions = MakeDoublePositions();
		FaintedPositions[2].State = EBattleTargetPositionState::Fainted;

		FBattleTargetResolutionSpec FaintedSpec = MakeResolutionSpec(
			EBattleTargetClass::SelectedOpponent,
			FaintedPositions,
			OpponentLeft);
		FStrictBattleRandom FaintedRandom({});
		FBattleTargetResolutionResult FaintedResult;
		EBattleTargetingError Error = EBattleTargetingError::None;
		TestTrue(
			TEXT("A previously selected fainted opponent is a valid resolution state"),
			FBattleTargetResolver::TryResolve(FaintedSpec, FaintedRandom, FaintedResult, Error));
		TestTrue(TEXT("A living other opponent receives the action"), FaintedResult.Targets[0].GetBattler() == OpponentRight);
		TestTrue(TEXT("Fainted fallback is marked as redirection"), FaintedResult.bWasRedirected);
		TestTrue(TEXT("Fainted fallback has its specific metadata"), FaintedResult.bUsedFaintedTargetFallback);
		TestTrue(TEXT("Fainted fallback consumes no target RNG"), FaintedRandom.GetTrace().IsEmpty());
		TestTrue(TEXT("Fainted fallback satisfies the exact no-draw contract"), FaintedRandom.IsExact());

		TArray<FBattleTargetPositionFacts> CapturedPositions = MakeDoublePositions();
		CapturedPositions[2].State = EBattleTargetPositionState::Captured;
		FBattleTargetResolutionSpec CapturedSpec = MakeResolutionSpec(
			EBattleTargetClass::SelectedOpponent,
			CapturedPositions,
			OpponentLeft);
		CapturedSpec.RedirectionProposals.Add(FBattleTargetRedirectionProposal());
		FStrictBattleRandom CapturedRandom({});
		FBattleTargetResolutionResult CapturedResult;
		TestTrue(
			TEXT("Captured-target cancellation succeeds before redirection validation"),
			FBattleTargetResolver::TryResolve(CapturedSpec, CapturedRandom, CapturedResult, Error));
		TestEqual(
			TEXT("Captured targets cancel rather than redirect"),
			CapturedResult.Outcome,
			EBattleTargetResolutionOutcome::CapturedTargetCanceled);
		TestTrue(TEXT("Captured cancellation exposes no target"), CapturedResult.Targets.IsEmpty());
		TestFalse(TEXT("Captured cancellation is not ordinary redirection"), CapturedResult.bWasRedirected);
		TestTrue(TEXT("Captured cancellation consumes no RNG"), CapturedRandom.GetTrace().IsEmpty());
		TestTrue(TEXT("Captured cancellation satisfies the exact no-draw contract"), CapturedRandom.IsExact());

		TArray<FBattleTargetPositionFacts> NoFallbackPositions = FaintedPositions;
		NoFallbackPositions[0].State = EBattleTargetPositionState::Removed;
		FStrictBattleRandom NoFallbackRandom({});
		FBattleTargetResolutionResult NoFallbackResult;
		TestTrue(
			TEXT("A fainted target with no other living opponent resolves cleanly"),
			FBattleTargetResolver::TryResolve(
				MakeResolutionSpec(
					EBattleTargetClass::SelectedOpponent,
					NoFallbackPositions,
					OpponentLeft),
				NoFallbackRandom,
				NoFallbackResult,
				Error));
		TestEqual(TEXT("No living fallback yields no legal target"), NoFallbackResult.Outcome, EBattleTargetResolutionOutcome::NoLegalTarget);
		TestTrue(TEXT("No-fallback resolution satisfies the exact no-draw contract"), NoFallbackRandom.IsExact());
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC04BRedirectionTest,
		"PokemonSolarus.Battle.C04B.Redirection.OrderAndLegality",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FBattleC04BRedirectionTest::RunTest(const FString& Parameters)
	{
		const FBattleBattlerTarget PlayerRight = MakeBattlerTarget(
			EBattleSide::Player,
			EBattlePosition::Right,
			PlayerRightBattlerValue);
		const FBattleBattlerTarget OpponentLeft = MakeBattlerTarget(
			EBattleSide::Opponent,
			EBattlePosition::Left,
			OpponentLeftBattlerValue);
		const FBattleBattlerTarget OpponentRight = MakeBattlerTarget(
			EBattleSide::Opponent,
			EBattlePosition::Right,
			OpponentRightBattlerValue);

		FBattleTargetResolutionSpec Spec = MakeResolutionSpec(
			EBattleTargetClass::SelectedOpponent,
			MakeDoublePositions(),
			OpponentLeft);
		Spec.RedirectionProposals.Add({PlayerRight});
		Spec.RedirectionProposals.Add({OpponentLeft});
		Spec.RedirectionProposals.Add({OpponentRight});
		Spec.RedirectionProposals.Add({OpponentLeft});

		FStrictBattleRandom Random({});
		FBattleTargetResolutionResult Result;
		EBattleTargetingError Error = EBattleTargetingError::None;
		TestTrue(
			TEXT("Structurally valid ordered redirection proposals are evaluated"),
			FBattleTargetResolver::TryResolve(Spec, Random, Result, Error));
		TestTrue(TEXT("Illegal ally and no-op proposals are skipped before the first legal replacement"), Result.Targets[0].GetBattler() == OpponentRight);
		TestTrue(TEXT("The effective replacement is reported"), Result.bWasRedirected);
		TestFalse(TEXT("Rule redirection is distinct from faint fallback"), Result.bUsedFaintedTargetFallback);
		TestTrue(TEXT("Non-random redirection consumes no RNG"), Random.GetTrace().IsEmpty());
		TestTrue(TEXT("Redirection satisfies the exact no-draw contract"), Random.IsExact());

		FBattleTargetResolutionSpec InvalidSpec = MakeResolutionSpec(
			EBattleTargetClass::SelectedOpponent,
			MakeDoublePositions(),
			OpponentLeft);
		InvalidSpec.RedirectionProposals.Add(FBattleTargetRedirectionProposal());
		FStrictBattleRandom InvalidRandom({});
		FBattleTargetResolutionResult InvalidResult;
		Error = EBattleTargetingError::None;
		TestFalse(
			TEXT("A structurally invalid redirection proposal rejects the resolution"),
			FBattleTargetResolver::TryResolve(InvalidSpec, InvalidRandom, InvalidResult, Error));
		TestEqual(TEXT("Invalid proposals have a typed error"), Error, EBattleTargetingError::InvalidRedirectionProposal);
		TestEqual(TEXT("Rejected redirection leaves an invalid result"), InvalidResult.Outcome, EBattleTargetResolutionOutcome::Invalid);
		TestTrue(TEXT("Rejected redirection satisfies the exact no-draw contract"), InvalidRandom.IsExact());
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC04BRandomTargetTest,
		"PokemonSolarus.Battle.C04B.Random.StableDrawContract",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FBattleC04BRandomTargetTest::RunTest(const FString& Parameters)
	{
		const FBattleBattlerTarget OpponentLeft = MakeBattlerTarget(
			EBattleSide::Opponent,
			EBattlePosition::Left,
			OpponentLeftBattlerValue);
		const FBattleBattlerTarget OpponentRight = MakeBattlerTarget(
			EBattleSide::Opponent,
			EBattlePosition::Right,
			OpponentRightBattlerValue);
		const FBattleTargetResolutionSpec TwoCandidateSpec = MakeResolutionSpec(
			EBattleTargetClass::RandomLegalOpponent,
			MakeDoublePositions());

		FStrictBattleRandom FirstRandom(
			{{
				0,
				1,
				0,
				FBattleTargetResolver::GetRandomLegalOpponentRulePurpose()
			}});
		FBattleTargetResolutionResult FirstResult;
		EBattleTargetingError Error = EBattleTargetingError::None;
		TestTrue(
			TEXT("A two-candidate random opponent resolves"),
			FBattleTargetResolver::TryResolve(TwoCandidateSpec, FirstRandom, FirstResult, Error));
		TestTrue(TEXT("Stable index zero is opponent Left despite shuffled input"), FirstResult.Targets[0].GetBattler() == OpponentLeft);
		TestEqual(TEXT("Two candidates consume exactly one target draw"), FirstRandom.GetTrace().Num(), 1);
		TestEqual(TEXT("The target draw starts at zero"), FirstRandom.GetTrace()[0].InclusiveMinimum, 0U);
		TestEqual(TEXT("The target draw uses inclusive n minus one"), FirstRandom.GetTrace()[0].InclusiveMaximum, 1U);
		TestTrue(
			TEXT("The target draw uses the typed purpose"),
			FirstRandom.GetTrace()[0].RulePurpose
				== FBattleTargetResolver::GetRandomLegalOpponentRulePurpose());
		TestTrue(TEXT("Two-candidate targeting satisfies the exact draw contract"), FirstRandom.IsExact());

		TArray<FBattleTargetPositionFacts> OneCandidatePositions = MakeDoublePositions();
		OneCandidatePositions[0].State = EBattleTargetPositionState::Removed;
		FStrictBattleRandom OneCandidateRandom(
			{{
				0,
				0,
				0,
				FBattleTargetResolver::GetRandomLegalOpponentRulePurpose()
			}});
		FBattleTargetResolutionResult OneCandidateResult;
		TestTrue(
			TEXT("One candidate still resolves through RNG"),
			FBattleTargetResolver::TryResolve(
				MakeResolutionSpec(
					EBattleTargetClass::RandomLegalOpponent,
					OneCandidatePositions),
				OneCandidateRandom,
				OneCandidateResult,
				Error));
		TestTrue(TEXT("The sole candidate is selected"), OneCandidateResult.Targets[0].GetBattler() == OpponentLeft);
		TestEqual(TEXT("One candidate consumes one draw"), OneCandidateRandom.GetTrace().Num(), 1);
		TestEqual(TEXT("One-candidate range is U[0,0]"), OneCandidateRandom.GetTrace()[0].InclusiveMaximum, 0U);
		TestTrue(TEXT("One-candidate targeting satisfies the exact draw contract"), OneCandidateRandom.IsExact());

		TArray<FBattleTargetPositionFacts> EmptyPositions = MakeDoublePositions();
		EmptyPositions[0].State = EBattleTargetPositionState::Removed;
		EmptyPositions[2].State = EBattleTargetPositionState::Fainted;
		FStrictBattleRandom EmptyRandom({});
		FBattleTargetResolutionResult EmptyResult;
		TestTrue(
			TEXT("An empty legal opponent set is a valid no-target result"),
			FBattleTargetResolver::TryResolve(
				MakeResolutionSpec(EBattleTargetClass::RandomLegalOpponent, EmptyPositions),
				EmptyRandom,
				EmptyResult,
				Error));
		TestEqual(TEXT("Empty random sets report no legal target"), EmptyResult.Outcome, EBattleTargetResolutionOutcome::NoLegalTarget);
		TestTrue(TEXT("Empty random sets consume no draw"), EmptyRandom.GetTrace().IsEmpty());
		TestTrue(TEXT("Empty random targeting satisfies the exact no-draw contract"), EmptyRandom.IsExact());

		FSeededBattleRandom SeededA(404404);
		FSeededBattleRandom SeededB(404404);
		FBattleTargetResolutionResult SeededResultA;
		FBattleTargetResolutionResult SeededResultB;
		EBattleTargetingError ErrorA = EBattleTargetingError::None;
		EBattleTargetingError ErrorB = EBattleTargetingError::None;
		const bool bResolvedA = FBattleTargetResolver::TryResolve(
			TwoCandidateSpec,
			SeededA,
			SeededResultA,
			ErrorA);
		const bool bResolvedB = FBattleTargetResolver::TryResolve(
			TwoCandidateSpec,
			SeededB,
			SeededResultB,
			ErrorB);
		TestTrue(TEXT("The first seeded run resolves"), bResolvedA);
		TestTrue(TEXT("The repeated seeded run resolves"), bResolvedB);
		TestTrue(TEXT("Equal seeds choose an identical target"), SeededResultA.Targets[0] == SeededResultB.Targets[0]);
		TestTrue(TEXT("Equal seeds consume an identical trace"), SeededA.GetTrace()[0] == SeededB.GetTrace()[0]);
		TestTrue(TEXT("The seeded result remains one of the two legal targets"),
			SeededResultA.Targets[0].GetBattler() == OpponentLeft
			|| SeededResultA.Targets[0].GetBattler() == OpponentRight);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC04BIntegrationTest,
		"PokemonSolarus.Battle.C04B.Integration.LockedTargetsEventsAndReplay",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FBattleC04BIntegrationTest::RunTest(const FString& Parameters)
	{
		const FMoveId FixedSpreadMove = MakeDefinitionId<FMoveId>(FixedSpreadMoveName);
		const FBattleBattlerTarget PlayerRight = MakeBattlerTarget(
			EBattleSide::Player,
			EBattlePosition::Right,
			PlayerRightBattlerValue);
		const FBattleBattlerTarget OpponentLeft = MakeBattlerTarget(
			EBattleSide::Opponent,
			EBattlePosition::Left,
			OpponentLeftBattlerValue);
		const FBattleBattlerTarget OpponentRight = MakeBattlerTarget(
			EBattleSide::Opponent,
			EBattlePosition::Right,
			OpponentRightBattlerValue);

		TUniquePtr<FBattleEngine> FirstEngine = MakeDoubleEngine(10);
		FBattleRejection Rejection;
		TestTrue(TEXT("The integration battle begins selection"), FirstEngine->TryBeginActionDecisionSequence(Rejection));
		const TArray<FBattleDecisionRequest> FirstRequests = FirstEngine->GetPendingDecisionRequests();
		TestEqual(TEXT("The player receives both active requests"), FirstRequests.Num(), 2);
		for (const FBattleDecisionRequest& Request : FirstRequests)
		{
			TestTrue(TEXT("Fixed spread is marked automatic"), Request.GetAutomaticallyTargetedMoveIds().Contains(FixedSpreadMove));
			const int32 PreviewCount = Algo::CountIf(
				Request.GetLegalMoveTargets(),
				[FixedSpreadMove](const FBattleMoveTargetOption& Option)
				{
					return Option.MoveId == FixedSpreadMove;
				});
			TestEqual(TEXT("Fixed spread exposes all three affected-position previews"), PreviewCount, 3);
		}
		const FBattleReplayRecord FirstSelectingRecord = FirstEngine->ExportReplayRecord();
		TArray<uint8> FirstSelectingBytes;
		TestTrue(
			TEXT("A selecting-state replay serializes non-empty automatic-target move IDs"),
			FBattleReplaySerializer::TrySerializeCanonical(
				FirstSelectingRecord,
				FirstSelectingBytes,
				Rejection));
		TUniquePtr<FBattleEngine> SelectingTwin = MakeDoubleEngine(10);
		TestTrue(
			TEXT("The identical selecting-state battle begins"),
			SelectingTwin->TryBeginActionDecisionSequence(Rejection));
		TArray<uint8> SelectingTwinBytes;
		TestTrue(
			TEXT("The repeated selecting-state replay serializes"),
			FBattleReplaySerializer::TrySerializeCanonical(
				SelectingTwin->ExportReplayRecord(),
				SelectingTwinBytes,
				Rejection));
		TestTrue(
			TEXT("Automatic-target request encoding is deterministic"),
			FirstSelectingBytes == SelectingTwinBytes);

		TestTrue(TEXT("All automatic Fight choices lock"), LockAllConfiguredFights(*FirstEngine));
		const TArray<FBattleLockedAction> LockedBeforeResolution = FirstEngine->GetLockedActions();
		TestEqual(TEXT("Doubles locks four actions"), LockedBeforeResolution.Num(), 4);
		TestTrue(
			TEXT("The fastest battler acts first"),
			LockedBeforeResolution[0].Decision.GetActingBattlerId()
				== MakeNumericId<FBattlerId>(PlayerLeftBattlerValue));
		TestEqual(TEXT("The locked action freezes its target class"), LockedBeforeResolution[0].TargetClass, EBattleTargetClass::FixedSpreadSet);
		TestFalse(TEXT("Automatic moves freeze no selector-supplied active target"), LockedBeforeResolution[0].Decision.GetActiveTargetId().IsValid());
		TestFalse(TEXT("Final targets do not exist before the C04B checkpoint"), LockedBeforeResolution[0].TargetResolution.IsSet());

		const FBattleResolution Begun = FirstEngine->BeginNextLockedAction();
		TestTrue(TEXT("The first action starts"), Begun.WasAccepted());
		const FBattleResolution Committed = FirstEngine->CommitCurrentMoveAfterPreMoveGates();
		TestTrue(TEXT("The first move commits after pre-move gates"), Committed.WasAccepted());
		const FBattleResolution TargetResolution = FirstEngine->ResolveCurrentMoveTargets();
		TestTrue(TEXT("The engine resolves the final C04B target set"), TargetResolution.WasAccepted());

		const TOptional<FBattleLockedAction> Current = FirstEngine->GetCurrentLockedAction();
		TestTrue(TEXT("The C05 seam retains the current locked action"), Current.IsSet());
		if (Current.IsSet())
		{
			TestTrue(TEXT("The C05 seam exposes a frozen target result"), Current.GetValue().TargetResolution.IsSet());
			if (Current.GetValue().TargetResolution.IsSet())
			{
				const FBattleTargetResolutionResult& Result = Current.GetValue().TargetResolution.GetValue();
				TestEqual(TEXT("The engine target result is resolved"), Result.Outcome, EBattleTargetResolutionOutcome::Resolved);
				TestEqual(TEXT("The engine freezes the friendly-fire spread set"), Result.Targets.Num(), 3);
				if (Result.Targets.Num() == 3)
				{
					TestTrue(TEXT("The frozen set begins with player Right"), Result.Targets[0].GetBattler() == PlayerRight);
					TestTrue(TEXT("The frozen set then contains opponent Left"), Result.Targets[1].GetBattler() == OpponentLeft);
					TestTrue(TEXT("The frozen set ends with opponent Right"), Result.Targets[2].GetBattler() == OpponentRight);
				}
			}
		}

		const FBattleEvent* TargetsEvent = TargetResolution.GetEvents().FindByPredicate(
			[](const FBattleEvent& Event)
			{
				return Event.GetType() == EBattleEventType::TargetsResolved;
			});
		TestNotNull(TEXT("Target resolution emits a typed event"), TargetsEvent);
		if (TargetsEvent != nullptr)
		{
			TestEqual(TEXT("The targeting event has a typed cause"), TargetsEvent->GetCause(), EBattleEventCause::Targeting);
			TestTrue(TEXT("The targeting event carries metadata"), TargetsEvent->GetTargetResolution().IsSet());
			if (TargetsEvent->GetTargetResolution().IsSet())
			{
				TestEqual(
					TEXT("The event preserves the target class"),
					TargetsEvent->GetTargetResolution().GetValue().TargetClass,
					EBattleTargetClass::FixedSpreadSet);
			}
			TestEqual(TEXT("The event exposes all stable battler targets"), TargetsEvent->GetTargets().Num(), 3);
			if (TargetsEvent->GetTargets().Num() == 3)
			{
				TestTrue(TEXT("Event target zero is player Right"), TargetsEvent->GetTargets()[0].BattlerId == PlayerRight.BattlerId);
				TestTrue(TEXT("Event target one is opponent Left"), TargetsEvent->GetTargets()[1].BattlerId == OpponentLeft.BattlerId);
				TestTrue(TEXT("Event target two is opponent Right"), TargetsEvent->GetTargets()[2].BattlerId == OpponentRight.BattlerId);
			}
			for (const FBattleEventTarget& EventTarget : TargetsEvent->GetTargets())
			{
				TestFalse(TEXT("Battler event targets are not side targets"), EventTarget.bHasSide);
				TestFalse(TEXT("Battler event targets are not field targets"), EventTarget.bField);
			}
		}

		TUniquePtr<FBattleEngine> SelectedEngine = MakeDoubleEngine(
			10,
			4405,
			EBattleTargetClass::SelectedOpponent);
		FBattleResolution SelectedTargetResolution;
		TestTrue(
			TEXT("An explicit selected-opponent action reaches the C04B checkpoint"),
			ResolveFirstConfiguredFight(*SelectedEngine, SelectedTargetResolution));
		const TOptional<FBattleLockedAction> SelectedCurrent = SelectedEngine->GetCurrentLockedAction();
		TestTrue(TEXT("The explicit action remains available to C05"), SelectedCurrent.IsSet());
		if (SelectedCurrent.IsSet() && SelectedCurrent.GetValue().TargetResolution.IsSet())
		{
			TestEqual(
				TEXT("The explicit action freezes its selected-opponent class"),
				SelectedCurrent.GetValue().TargetClass,
				EBattleTargetClass::SelectedOpponent);
			TestTrue(
				TEXT("The explicit action freezes one opponent battler"),
				SelectedCurrent.GetValue().TargetResolution.GetValue().Targets.Num() == 1
					&& SelectedCurrent.GetValue().TargetResolution.GetValue().Targets[0].GetKind()
						== EBattleResolvedTargetKind::Battler
					&& SelectedCurrent.GetValue().TargetResolution.GetValue().Targets[0]
						.GetBattler().ActiveSlotId.GetSide() == EBattleSide::Opponent);
		}

		TUniquePtr<FBattleEngine> SideEngine = MakeDoubleEngine(
			10,
			4406,
			EBattleTargetClass::UserSide);
		FBattleResolution SideTargetResolution;
		TestTrue(
			TEXT("A side-target action reaches the C04B checkpoint"),
			ResolveFirstConfiguredFight(*SideEngine, SideTargetResolution));
		const FBattleEvent* SideEvent = SideTargetResolution.GetEvents().FindByPredicate(
			[](const FBattleEvent& Event)
			{
				return Event.GetType() == EBattleEventType::TargetsResolved;
			});
		TestTrue(
			TEXT("A side-target event uses the typed side union member"),
			SideEvent != nullptr
				&& SideEvent->GetTargets().Num() == 1
				&& SideEvent->GetTargets()[0].bHasSide
				&& SideEvent->GetTargets()[0].Side == EBattleSide::Player
				&& !SideEvent->GetTargets()[0].bField);
		TArray<uint8> SideReplayBytes;
		TestTrue(
			TEXT("The side-target event serializes canonically"),
			FBattleReplaySerializer::TrySerializeCanonical(
				SideEngine->ExportReplayRecord(),
				SideReplayBytes,
				Rejection));

		TUniquePtr<FBattleEngine> FieldEngine = MakeDoubleEngine(
			10,
			4407,
			EBattleTargetClass::Field);
		FBattleResolution FieldTargetResolution;
		TestTrue(
			TEXT("A field-target action reaches the C04B checkpoint"),
			ResolveFirstConfiguredFight(*FieldEngine, FieldTargetResolution));
		const FBattleEvent* FieldEvent = FieldTargetResolution.GetEvents().FindByPredicate(
			[](const FBattleEvent& Event)
			{
				return Event.GetType() == EBattleEventType::TargetsResolved;
			});
		TestTrue(
			TEXT("A field-target event uses the typed field union member"),
			FieldEvent != nullptr
				&& FieldEvent->GetTargets().Num() == 1
				&& FieldEvent->GetTargets()[0].bField
				&& !FieldEvent->GetTargets()[0].bHasSide);
		TArray<uint8> FieldReplayBytes;
		TestTrue(
			TEXT("The field-target event serializes canonically"),
			FBattleReplaySerializer::TrySerializeCanonical(
				FieldEngine->ExportReplayRecord(),
				FieldReplayBytes,
				Rejection));

		const FBattleReplayRecord FirstRecord = FirstEngine->ExportReplayRecord();
		TestTrue(TEXT("The integration replay record is valid"), FirstRecord.IsValid());
		TestEqual(TEXT("C04B exports the current replay schema"), FirstRecord.GetSchemaVersion(), 6U);
		TArray<uint8> FirstBytes;
		TestTrue(
			TEXT("The first C04B replay serializes canonically"),
			FBattleReplaySerializer::TrySerializeCanonical(FirstRecord, FirstBytes, Rejection));
		TestFalse(TEXT("The canonical replay is non-empty"), FirstBytes.IsEmpty());

		TUniquePtr<FBattleEngine> SecondEngine = MakeDoubleEngine(10);
		FBattleResolution SecondTargetResolution;
		TestTrue(
			TEXT("An identical battle reaches the same targeting checkpoint"),
			ResolveFirstConfiguredFight(*SecondEngine, SecondTargetResolution));
		const FBattleReplayRecord SecondRecord = SecondEngine->ExportReplayRecord();
		TArray<uint8> SecondBytes;
		TestTrue(
			TEXT("The repeated C04B replay serializes canonically"),
			FBattleReplaySerializer::TrySerializeCanonical(SecondRecord, SecondBytes, Rejection));
		TestTrue(TEXT("Identical setup, decisions, and RNG produce identical canonical replay bytes"), FirstBytes == SecondBytes);
		return true;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
