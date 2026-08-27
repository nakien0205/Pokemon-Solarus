#include "Misc/AutomationTest.h"

#include "Battle/BattleAbility.h"
#include "Battle/BattleEngine.h"
#include "Battle/BattleFieldSideConditions.h"
#include "Battle/BattleMajorStatus.h"
#include "Battle/BattleState.h"
#include "Battle/BattleSwitching.h"
#include "Battle/BattleVolatile.h"
#include "BattleTestFactories.h"
#include "BattleTestRandom.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace BattleAbilityTests
{
	using BattleTest::MakeActiveSlotId;
	using BattleTest::MakeDefinitionId;
	using BattleTest::MakeNumericId;
	using BattleTest::MakePartySlotId;

	FBattleTriggerSubject MakeBattlerSubject(const uint64 BattlerValue)
	{
		FBattleTriggerSubject Subject;
		const bool bCreated = FBattleTriggerSubject::TryCreateBattler(
			MakeNumericId<FBattlerId>(BattlerValue),
			Subject);
		check(bCreated);
		return Subject;
	}

	FBattleAbilityRegistrationFacts MakeRegistrationFacts(
		const FAbilityId& AbilityId,
		const uint64 OwnerValue)
	{
		FBattleAbilityRegistrationFacts Facts;
		Facts.AbilityId = AbilityId;
		Facts.Owner = MakeBattlerSubject(OwnerValue);
		Facts.Source = Facts.Owner;
		Facts.Targets.Add(Facts.Owner);
		return Facts;
	}

	bool TryMakeTypedRequest(
		const FAbilityId& AbilityId,
		const EBattleTriggerPhase Phase,
		const uint64 OwnerValue,
		FBattleAbilityItemEffectRequest& OutRequest)
	{
		FBattleTriggerFramework Framework;
		EBattleAbilityItemHookError HookError = EBattleAbilityItemHookError::InvalidDefinition;
		if (!FBattleAbilityRules::TryRegisterHooks(
				Framework,
				MakeRegistrationFacts(AbilityId, OwnerValue),
				HookError))
		{
			return false;
		}

		TArray<FBattleTriggerLifecycleFact> StartedFacts;
		Framework.DrainLifecycleFacts(StartedFacts);

		FBattleTriggerDispatchSpec Dispatch;
		Dispatch.Phase = Phase;
		Dispatch.ReentrancyToken = MakeNumericId<FBattleTriggerReentrancyToken>(OwnerValue);
		EBattleTriggerError TriggerError = EBattleTriggerError::InvalidParticipant;
		if (!Framework.TryEnqueueDispatch(Dispatch, TriggerError))
		{
			return false;
		}
		FBattleTriggerDispatchResult DispatchResult;
		if (!Framework.TryResolveNextDispatch(DispatchResult, TriggerError))
		{
			return false;
		}

		TArray<FBattleTriggerEffectRequest> Requests;
		Framework.DrainEffectRequests(Requests);
		return Requests.Num() == 1
			&& FBattleAbilityRules::TryCreateTypedEffectRequest(
				Requests[0],
				OutRequest,
				HookError);
	}

	TArray<FBattleTypeChartEntry> MakeCompleteNeutralChart()
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

	using FExpectedIntegrationDraw = BattleTest::FBattleExpectedRandomDraw;
	using FScriptedIntegrationRandom = BattleTest::FStrictBattleRandom;

	FBattleMoveDefinition MakeIntegrationMove(
		const EPokemonType MoveType = EPokemonType::Normal)
	{
		FBattleMoveDefinition Move;
		Move.Id = MakeDefinitionId<FMoveId>(TEXT("Move.C08B.Integration"));
		Move.Type = MoveType;
		Move.Category = EBattleMoveCategory::Physical;
		Move.Power = 40;
		Move.Accuracy = 100;
		Move.BasePP = 35;
		Move.TargetClass = EBattleTargetClass::SelectedOpponent;
		FBattleMoveEffectDescriptor Damage;
		Damage.Order = 0;
		Damage.Kind = EBattleMoveEffectKind::Damage;
		Damage.Target = EBattleEffectTarget::ResolvedTarget;
		Move.Effects.Add(Damage);
		return Move;
	}

	FBattleMoveDefinition MakeIntegrationSubstituteMove()
	{
		FBattleMoveDefinition Move;
		Move.Id = MakeDefinitionId<FMoveId>(TEXT("Move.C08B.Substitute"));
		Move.Type = EPokemonType::Normal;
		Move.Category = EBattleMoveCategory::Status;
		Move.bAlwaysHits = true;
		Move.bUsesPP = true;
		Move.BasePP = 20;
		Move.bAllowsPPBoosts = true;
		Move.TargetClass = EBattleTargetClass::Self;
		FBattleMoveEffectDescriptor Effect;
		Effect.Kind = EBattleMoveEffectKind::ApplyCondition;
		Effect.Target = EBattleEffectTarget::User;
		Effect.ConditionId = FBattleVolatileRules::GetSubstituteId();
		Move.Effects.Add(Effect);
		return Move;
	}

	FBattleMoveDefinition MakeIntegrationForcedSwitchMove()
	{
		FBattleMoveDefinition Move;
		Move.Id = MakeDefinitionId<FMoveId>(TEXT("Move.C08B.ForcedSwitch"));
		Move.Type = EPokemonType::Normal;
		Move.Category = EBattleMoveCategory::Status;
		Move.bAlwaysHits = true;
		Move.bUsesPP = true;
		Move.BasePP = 20;
		Move.bAllowsPPBoosts = true;
		Move.TargetClass = EBattleTargetClass::SelectedOpponent;
		FBattleMoveEffectDescriptor Effect;
		Effect.Kind = EBattleMoveEffectKind::Switch;
		Effect.Target = EBattleEffectTarget::ResolvedTarget;
		Move.Effects.Add(Effect);
		return Move;
	}

	FBattleSpeciesFormDefinition MakeIntegrationSpecies(const TCHAR* Name)
	{
		FBattleSpeciesFormDefinition Species;
		Species.Id = MakeDefinitionId<FSpeciesFormId>(Name);
		Species.PrimaryType = EPokemonType::Normal;
		Species.BaseStats = {80, 80, 80, 80, 80, 80};
		Species.CatchRate = 45;
		Species.AbilityChoices = FBattleAbilityRules::GetCanonicalIds();
		return Species;
	}

	FBattleDefinitionCatalog MakeIntegrationCatalog(
		const EPokemonType MoveType = EPokemonType::Normal)
	{
		FBattleDefinitionCatalogInput Input;
		Input.TypeChartEntries = MakeCompleteNeutralChart();
		Input.Moves.Add(MakeIntegrationMove(MoveType));
		Input.Moves.Add(MakeIntegrationSubstituteMove());
		Input.Moves.Add(MakeIntegrationForcedSwitchMove());
		for (const FAbilityId& AbilityId : FBattleAbilityRules::GetCanonicalIds())
		{
			Input.Abilities.Add({AbilityId});
		}
		for (const FConditionId& StatusId : FBattleMajorStatusRules::GetCanonicalIds())
		{
			Input.Conditions.Add({StatusId, EBattleConditionKind::MajorStatus});
		}
		for (const FConditionId& ConditionId : FBattleFieldSideConditionRules::GetCanonicalIds())
		{
			Input.Conditions.Add({
				ConditionId,
				FBattleFieldSideConditionRules::GetConditionFamily(ConditionId)});
		}
		for (const FConditionId& VolatileId : FBattleVolatileRules::GetCanonicalIds())
		{
			Input.Conditions.Add({VolatileId, EBattleConditionKind::Volatile});
		}
		Input.SpeciesForms.Add(MakeIntegrationSpecies(TEXT("Species.C08B.Player")));
		Input.SpeciesForms.Add(MakeIntegrationSpecies(TEXT("Species.C08B.Opponent")));

		FBattleDefinitionCatalog Catalog;
		TArray<FBattleCatalogDiagnostic> Diagnostics;
		const bool bCreated = FBattleDefinitionCatalog::TryCreate(
			Input,
			Catalog,
			Diagnostics);
		check(bCreated);
		return Catalog;
	}

	FBattleTrainerSetup MakeIntegrationTrainer(
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
				? TEXT("Selector.C08B.Player")
				: TEXT("Selector.C08B.Opponent"));
		return Trainer;
	}

	FBattlePartyEntrySetup MakeIntegrationPartyEntry(
		const uint64 TrainerValue,
		const uint64 BattlerValue,
		const uint64 SourceValue,
		const uint8 PartySlotIndex,
		const TCHAR* SpeciesName,
		const FAbilityId& AbilityId,
		const int32 CurrentHP = 200,
		const int32 Speed = 100)
	{
		FBattlePartyEntrySetup Entry;
		Entry.TrainerId = MakeNumericId<FTrainerId>(TrainerValue);
		Entry.BattlerId = MakeNumericId<FBattlerId>(BattlerValue);
		Entry.SourcePokemonId = MakeNumericId<FSourcePokemonId>(SourceValue);
		Entry.PartySlotId = MakePartySlotId(PartySlotIndex);
		Entry.SpeciesFormId = MakeDefinitionId<FSpeciesFormId>(SpeciesName);
		Entry.Level = 50;
		Entry.Stats = {
			200,
			100,
			100,
			100,
			100,
			Speed};
		Entry.CurrentHP = CurrentHP;
		Entry.AbilityId = AbilityId;
		FBattleMoveSlotSetup Move;
		Move.SlotIndex = 0;
		Move.MoveId = MakeDefinitionId<FMoveId>(TEXT("Move.C08B.Integration"));
		Move.CurrentPP = 35;
		Move.MaxPP = 35;
		Entry.Moves.Add(Move);
		FBattleMoveSlotSetup Substitute;
		Substitute.SlotIndex = 1;
		Substitute.MoveId = MakeDefinitionId<FMoveId>(TEXT("Move.C08B.Substitute"));
		Substitute.CurrentPP = 20;
		Substitute.MaxPP = 20;
		Entry.Moves.Add(Substitute);
		FBattleMoveSlotSetup ForcedSwitch;
		ForcedSwitch.SlotIndex = 2;
		ForcedSwitch.MoveId =
			MakeDefinitionId<FMoveId>(TEXT("Move.C08B.ForcedSwitch"));
		ForcedSwitch.CurrentPP = 20;
		ForcedSwitch.MaxPP = 20;
		Entry.Moves.Add(ForcedSwitch);
		return Entry;
	}

	FBattleSetup MakeIntegrationSetup(
		const FAbilityId& PlayerAbility = FBattleAbilityRules::GetBlazeId(),
		const FAbilityId& OpponentAbility = FBattleAbilityRules::GetMoldBreakerId(),
		const int32 PlayerCurrentHP = 200,
		const FAbilityId& PlayerReserveAbility = FBattleAbilityRules::GetLevitateId(),
		const FAbilityId& OpponentReserveAbility = FBattleAbilityRules::GetBlazeId())
	{
		FBattleSetupInput Input;
		Input.BattleId = MakeNumericId<FBattleId>(808);
		Input.SettingsReference = {
			MakeDefinitionId<FDefinitionId>(TEXT("Settings.C08B")),
			1};
		Input.CatalogReference = {
			MakeDefinitionId<FDefinitionId>(TEXT("Catalog.C08B")),
			1};
		Input.EncounterKind = EBattleEncounterKind::Trainer;
		Input.Format = EBattleFormat::Single;
		Input.CaptureCapacity = {4, 100};
		Input.Policies.WildFleeMode = EBattleWildFleeMode::Disabled;
		Input.Trainers.Add(MakeIntegrationTrainer(
			1,
			EBattleSide::Player,
			EBattleTrainerRole::Player,
			EBattleDecisionController::Human));
		Input.Trainers.Add(MakeIntegrationTrainer(
			2,
			EBattleSide::Opponent,
			EBattleTrainerRole::Opponent,
			EBattleDecisionController::EnemyAI));
		Input.PartyEntries.Add(MakeIntegrationPartyEntry(
			1,
			11,
			111,
			0,
			TEXT("Species.C08B.Player"),
			PlayerAbility,
			PlayerCurrentHP,
			120));
		Input.PartyEntries.Add(MakeIntegrationPartyEntry(
			1,
			12,
			112,
			1,
			TEXT("Species.C08B.Player"),
			PlayerReserveAbility,
			200,
			100));
		Input.PartyEntries.Add(MakeIntegrationPartyEntry(
			2,
			21,
			211,
			0,
			TEXT("Species.C08B.Opponent"),
			OpponentAbility,
			200,
			80));
		Input.PartyEntries.Add(MakeIntegrationPartyEntry(
			2,
			22,
			212,
			1,
			TEXT("Species.C08B.Opponent"),
			OpponentReserveAbility,
			200,
			90));
		Input.StartingActive.Add(
			{
				MakeActiveSlotId(EBattleSide::Player, EBattlePosition::Left),
				MakeNumericId<FTrainerId>(1),
				MakeNumericId<FBattlerId>(11)
			});
		Input.StartingActive.Add(
			{
				MakeActiveSlotId(EBattleSide::Opponent, EBattlePosition::Left),
				MakeNumericId<FTrainerId>(2),
				MakeNumericId<FBattlerId>(21)
			});
		Input.ObedienceInputs.Add(
			{MakeNumericId<FBattlerId>(11), false, 50, 0});
		Input.ObedienceInputs.Add(
			{MakeNumericId<FBattlerId>(12), false, 50, 0});

		FBattleSetup Setup;
		EBattleSetupValidationError Error = EBattleSetupValidationError::None;
		const bool bCreated = FBattleSetup::TryCreate(Input, Setup, Error);
		check(bCreated);
		return Setup;
	}

	TUniquePtr<FBattleEngine> MakeIntegrationEngine(
		const FAbilityId& PlayerAbility,
		const FAbilityId& OpponentAbility,
		const EPokemonType MoveType,
		const int32 PlayerCurrentHP,
		const uint64 Seed)
	{
		TUniquePtr<FBattleEngine> Engine;
		FBattleRejection Rejection;
		const bool bCreated = FBattleEngine::TryCreate(
			MakeIntegrationSetup(PlayerAbility, OpponentAbility, PlayerCurrentHP),
			MakeIntegrationCatalog(MoveType),
			MakeUnique<FSeededBattleRandom>(Seed),
			Engine,
			Rejection);
		check(bCreated);
		return Engine;
	}

	bool LockIntegrationFights(
		FBattleEngine& Engine,
		const bool bPlayerUsesSubstitute = false)
	{
		FBattleRejection Rejection;
		if (Engine.GetSnapshot().GetPhase() == EBattlePhase::Setup
			&& !Engine.TryBeginActionDecisionSequence(Rejection))
		{
			return false;
		}
		const FMoveId DamageMoveId = MakeDefinitionId<FMoveId>(TEXT("Move.C08B.Integration"));
		const FMoveId SubstituteMoveId = MakeDefinitionId<FMoveId>(TEXT("Move.C08B.Substitute"));
		int32 Guard = 0;
		while (Engine.GetPendingDecision().IsSet() && Guard++ < 4)
		{
			const FBattleDecisionRequest Request = Engine.GetPendingDecision().GetValue();
			const FMoveId MoveId = bPlayerUsesSubstitute
				&& Request.GetActingBattlerId() == MakeNumericId<FBattlerId>(11)
					? SubstituteMoveId
					: DamageMoveId;
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
				bCreated = Target != nullptr
					&& FBattleDecision::TryCreateFight(
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
			const FBattleResolution Submitted = Engine.SubmitDecision(Decision);
			if (!Submitted.WasAccepted())
			{
				return false;
			}
		}
		return Guard < 4
			&& Engine.GetSnapshot().GetPhase() == EBattlePhase::Locked;
	}

	bool LockIntegrationFightsWithPlayerMove(
		FBattleEngine& Engine,
		const FMoveId PlayerMoveId)
	{
		FBattleRejection Rejection;
		if (Engine.GetSnapshot().GetPhase() == EBattlePhase::Setup
			&& !Engine.TryBeginActionDecisionSequence(Rejection))
		{
			return false;
		}
		const FMoveId DamageMoveId =
			MakeDefinitionId<FMoveId>(TEXT("Move.C08B.Integration"));
		int32 Guard = 0;
		while (Engine.GetPendingDecision().IsSet() && Guard++ < 4)
		{
			const FBattleDecisionRequest Request =
				Engine.GetPendingDecision().GetValue();
			const FMoveId MoveId = Request.GetActingBattlerId()
				== MakeNumericId<FBattlerId>(11)
					? PlayerMoveId
					: DamageMoveId;
			const FBattleMoveTargetOption* Target =
				Request.GetLegalMoveTargets().FindByPredicate(
					[MoveId](const FBattleMoveTargetOption& Option)
					{
						return Option.MoveId == MoveId;
					});
			FBattleDecision Decision;
			if (Target == nullptr
				|| !FBattleDecision::TryCreateFight(
					Request.GetStateVersion(),
					Request.GetDecisionOwnerTrainerId(),
					Request.GetActingBattlerId(),
					MoveId,
					Target->ActiveSlotId,
					Decision)
				|| !Engine.SubmitDecision(Decision).WasAccepted())
			{
				return false;
			}
		}
		return Guard < 4
			&& Engine.GetSnapshot().GetPhase() == EBattlePhase::Locked;
	}

	bool ExecuteFirstIntegrationMove(
		FBattleEngine& Engine,
		FBattleResolution& OutEffects,
		const bool bPlayerUsesSubstitute = false)
	{
		OutEffects = FBattleResolution();
		if (!LockIntegrationFights(Engine, bPlayerUsesSubstitute))
		{
			return false;
		}
		const FBattleResolution Started = Engine.BeginNextLockedAction();
		if (!Started.WasAccepted())
		{
			return false;
		}
		const FBattleResolution Committed = Engine.CommitCurrentMoveAfterPreMoveGates();
		if (!Committed.WasAccepted())
		{
			return false;
		}
		const FBattleResolution Targeted = Engine.ResolveCurrentMoveTargets();
		if (!Targeted.WasAccepted())
		{
			return false;
		}
		OutEffects = Engine.ExecuteCurrentMoveEffects();
		return OutEffects.WasAccepted();
	}

	bool ExecuteIntegrationTurnToEnd(FBattleEngine& Engine)
	{
		if (!LockIntegrationFights(Engine))
		{
			return false;
		}

		int32 Guard = 0;
		while ((Engine.GetSnapshot().GetPhase() == EBattlePhase::Locked
				|| Engine.GetSnapshot().GetPhase() == EBattlePhase::Resolving)
			&& !Engine.GetPendingDecision().IsSet()
			&& Guard++ < 8)
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
		return Guard < 8
			&& Engine.GetSnapshot().GetPhase() == EBattlePhase::EndOfTurn;
	}

	int32 FindFirstAbilityActivationIndex(
		const FBattleEngineState& State,
		const FAbilityId& AbilityId)
	{
		for (int32 Index = 0; Index < State.OrderedEvents.Num(); ++Index)
		{
			const FBattleEvent& Event = State.OrderedEvents[Index];
			if (Event.GetType() == EBattleEventType::AbilityActivated
				&& Event.GetSource().DefinitionId == AbilityId.GetDefinitionId())
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

	bool HasAbilityActivation(
		const FBattleEngineState& State,
		const FAbilityId& AbilityId)
	{
		return FindFirstAbilityActivationIndex(State, AbilityId) != INDEX_NONE;
	}
}

class FBattleC08BEngineFixture
{
public:
	static const FBattleEngineState& GetState(const FBattleEngine& Engine)
	{
		check(Engine.State.IsValid());
		return *Engine.State;
	}

	static FBattleEngineState& GetMutableState(FBattleEngine& Engine)
	{
		check(Engine.State.IsValid());
		return *Engine.State;
	}

	static bool AddMajorStatus(
		FBattleEngine& Engine,
		const FBattlerId BattlerId,
		const FConditionId& StatusId)
	{
		FBattleEngineState& State = GetMutableState(Engine);
		FBattleBattlerState* Battler = State.FindMutableBattler(BattlerId);
		FBattleTriggerSubject Owner;
		EBattleTriggerError Error = EBattleTriggerError::None;
		if (Battler == nullptr
			|| !FBattleTriggerSubject::TryCreateBattler(BattlerId, Owner)
			|| !FBattleMajorStatusRules::TryRegisterTriggers(
				State.TriggerFramework,
				StatusId,
				Owner,
				TOptional<int32>(),
				Error))
		{
			return false;
		}
		TArray<FBattleTriggerEffectRequest> Requests;
		TArray<FBattleTriggerLifecycleFact> Facts;
		State.TriggerFramework.DrainEffectRequests(Requests);
		State.TriggerFramework.DrainLifecycleFacts(Facts);
		Battler->MajorStatusId = StatusId;
		return true;
	}

	static bool SeedCondition(
		FBattleEngine& Engine,
		const FConditionId& ConditionId,
		const EBattleSide Side,
		const FBattlerId SourceBattlerId,
		const TOptional<int32>& RemainingTurns,
		const int32 Layers = 1)
	{
		FBattleEngineState& State = GetMutableState(Engine);
		if (!FBattleFieldSideConditionRules::IsCanonical(ConditionId) || Layers <= 0)
		{
			return false;
		}
		FBattleTriggerSubject Owner;
		if (FBattleFieldSideConditionRules::IsFieldOwned(ConditionId))
		{
			Owner = FBattleTriggerSubject::CreateField();
		}
		else if (!FBattleTriggerSubject::TryCreateSide(Side, Owner))
		{
			return false;
		}
		FBattleTriggerSubject Source;
		if (!FBattleTriggerSubject::TryCreateBattler(SourceBattlerId, Source))
		{
			return false;
		}
		FBattleFieldSideTriggerRegistrationFacts TriggerFacts;
		TriggerFacts.ConditionId = ConditionId;
		TriggerFacts.PayloadId = ConditionId.GetDefinitionId();
		TriggerFacts.Owner = Owner;
		TriggerFacts.Source = Source;
		TriggerFacts.Targets.Add(Owner);
		TriggerFacts.RemainingTurns = RemainingTurns;
		TriggerFacts.Layers = Layers;
		EBattleTriggerError TriggerError = EBattleTriggerError::None;
		if (!FBattleFieldSideConditionRules::TryRegisterTriggers(
				State.TriggerFramework,
				TriggerFacts,
				TriggerError))
		{
			return false;
		}
		FBattleConditionState Condition;
		Condition.ConditionId = ConditionId;
		Condition.RemainingTurns = RemainingTurns;
		Condition.LayerCount = Layers;
		Condition.CreationOrdinal = State.NextConditionCreationOrdinal++;
		Condition.SourceBattlerId = SourceBattlerId;
		switch (FBattleFieldSideConditionRules::GetConditionFamily(ConditionId))
		{
		case EBattleConditionKind::Weather:
			if (State.Field.Weather.IsSet()) return false;
			State.Field.Weather = Condition;
			break;
		case EBattleConditionKind::Terrain:
			if (State.Field.Terrain.IsSet()) return false;
			State.Field.Terrain = Condition;
			break;
		case EBattleConditionKind::Room:
			State.Field.Rooms.Add(Condition);
			break;
		case EBattleConditionKind::Hazard:
		{
			FBattleSideState* SideState = State.Sides.FindByPredicate(
				[Side](const FBattleSideState& Candidate)
				{
					return Candidate.Side == Side;
				});
			if (SideState == nullptr) return false;
			SideState->Hazards.Add(Condition);
			break;
		}
		case EBattleConditionKind::Screen:
		case EBattleConditionKind::SideCondition:
		{
			FBattleSideState* SideState = State.Sides.FindByPredicate(
				[Side](const FBattleSideState& Candidate)
				{
					return Candidate.Side == Side;
				});
			if (SideState == nullptr) return false;
			SideState->Conditions.Add(Condition);
			break;
		}
		default:
			return false;
		}
		TArray<FBattleTriggerEffectRequest> Requests;
		TArray<FBattleTriggerLifecycleFact> Facts;
		State.TriggerFramework.DrainEffectRequests(Requests);
		State.TriggerFramework.DrainLifecycleFacts(Facts);
		return true;
	}

	static bool AddVolatile(
		FBattleEngine& Engine,
		const FBattlerId TargetBattlerId,
		const FConditionId& VolatileId,
		const FBattlerId SourceBattlerId,
		const TOptional<int32>& RemainingTurns = TOptional<int32>())
	{
		FBattleEngineState& State = GetMutableState(Engine);
		FBattleBattlerState* Battler = State.FindMutableBattler(TargetBattlerId);
		if (Battler == nullptr || !FBattleVolatileRules::IsCanonical(VolatileId))
		{
			return false;
		}
		FBattleTriggerSubject Owner;
		FBattleTriggerSubject Source;
		if (!FBattleTriggerSubject::TryCreateBattler(TargetBattlerId, Owner))
		{
			return false;
		}
		if (VolatileId == FBattleVolatileRules::GetLeechSeedId())
		{
			const FBattleActivePositionState* SourceActive = State.ActivePositions.FindByPredicate(
				[SourceBattlerId](const FBattleActivePositionState& Candidate)
				{
					return Candidate.BattlerId == SourceBattlerId;
				});
			if (SourceActive == nullptr
				|| !FBattleTriggerSubject::TryCreateActiveSlot(
					SourceActive->ActiveSlotId,
					Source))
			{
				return false;
			}
		}
		else if (!FBattleTriggerSubject::TryCreateBattler(SourceBattlerId, Source))
		{
			return false;
		}
		FBattleVolatileTriggerRegistrationFacts TriggerFacts;
		TriggerFacts.VolatileId = VolatileId;
		TriggerFacts.PayloadId = VolatileId.GetDefinitionId();
		TriggerFacts.Owner = Owner;
		TriggerFacts.Source = Source;
		TriggerFacts.RemainingTurns = RemainingTurns;
		TriggerFacts.Layers = 1;
		EBattleTriggerError TriggerError = EBattleTriggerError::None;
		if (!FBattleVolatileRules::TryRegisterTriggers(
				State.TriggerFramework,
				TriggerFacts,
				TriggerError))
		{
			return false;
		}
		FBattleConditionState Condition;
		Condition.ConditionId = VolatileId;
		Condition.RemainingTurns = RemainingTurns;
		Condition.LayerCount = 1;
		Condition.CreationOrdinal = State.NextConditionCreationOrdinal++;
		Condition.SourceBattlerId = SourceBattlerId;
		Battler->Volatiles.Add(MoveTemp(Condition));
		TArray<FBattleTriggerEffectRequest> Requests;
		TArray<FBattleTriggerLifecycleFact> Facts;
		State.TriggerFramework.DrainEffectRequests(Requests);
		State.TriggerFramework.DrainLifecycleFacts(Facts);
		return true;
	}

	static bool PrepareLockedSwitch(
		FBattleEngine& Engine,
		const FBattlerId OutgoingBattlerId,
		const FPartySlotId IncomingPartySlotId)
	{
		FBattleEngineState& State = GetMutableState(Engine);
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
				IncomingPartySlotId,
				Active->ActiveSlotId,
				Decision))
		{
			return false;
		}
		FActionId ActionId;
		if (!FActionId::TryCreate(State.NextActionId, ActionId))
		{
			return false;
		}
		++State.NextActionId;
		FBattleLockedActionState Action;
		Action.ActionId = ActionId;
		Action.QueueOrdinal = 1;
		Action.Decision = MoveTemp(Decision);
		Action.OrderKey.CommandBand = EBattleActionCommandBand::VoluntarySwitch;
		Action.OrderKey.EffectiveSpeed = Outgoing->PermanentStats.Speed;
		Action.OrderKey.ActingSlotId = Active->ActiveSlotId;
		State.LockedActions.Reset();
		State.LockedActions.Add(MoveTemp(Action));
		State.bLockedOrderReversesSpeed = false;
		State.CurrentLockedActionIndex = 0;
		State.PendingDecision.Reset();
		State.PendingDecisionRequests.Reset();
		State.DecisionOwnerSequence.Reset();
		State.CurrentDecisionOwnerIndex = INDEX_NONE;
		State.CurrentDecisionActorOffset = 0;
		State.AcceptedSelections.Reset();
		State.Phase = EBattlePhase::Locked;
		Trainer->ActionAllowance.MaximumActions = 1;
		Trainer->ActionAllowance.RemainingActions = 1;
		EBattleStateValidationError Error = EBattleStateValidationError::None;
		return State.ValidateInvariants(Error);
	}

	static bool HasAbilityRegistration(
		const FBattleEngine& Engine,
		const FBattlerId BattlerId)
	{
		return GetState(Engine).TriggerFramework.GetActiveRegistrations().ContainsByPredicate(
			[BattlerId](const FBattleTriggerRegistrationState& Registration)
			{
				return Registration.Spec.Owner.Kind == EBattleTriggerSubjectKind::Battler
					&& Registration.Spec.Owner.BattlerId == BattlerId
					&& Registration.Spec.SourceDefinition.Kind
						== EBattleTriggerSourceDefinitionKind::Ability;
			});
	}
};

namespace BattleAbilityTests
{

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC08BCanonicalHooksTest,
	"PokemonSolarus.Battle.C08B.Contracts.CanonicalIdsHooksCleanupAndOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC08BCanonicalHooksTest::RunTest(const FString& Parameters)
{
	const TArray<FAbilityId> AbilityIds = FBattleAbilityRules::GetCanonicalIds();
	TestEqual(TEXT("C08B owns exactly eight canonical Ability IDs"), AbilityIds.Num(), 8);
	for (int32 LeftIndex = 0; LeftIndex < AbilityIds.Num(); ++LeftIndex)
	{
		TestTrue(TEXT("Every listed Ability ID is valid and canonical"),
			AbilityIds[LeftIndex].IsValid()
				&& FBattleAbilityRules::IsCanonical(AbilityIds[LeftIndex]));
		for (int32 RightIndex = LeftIndex + 1; RightIndex < AbilityIds.Num(); ++RightIndex)
		{
			TestTrue(TEXT("Canonical Ability IDs are unique"),
				AbilityIds[LeftIndex] != AbilityIds[RightIndex]);
		}

		TArray<FBattleAbilityItemHookDefinition> Definitions;
		TestTrue(TEXT("Every canonical Ability builds valid hook definitions"),
			FBattleAbilityRules::TryBuildHookDefinitions(
				AbilityIds[LeftIndex],
				Definitions));
		const int32 ExpectedHookCount =
			AbilityIds[LeftIndex] == FBattleAbilityRules::GetMagicGuardId()
				? 4
				: (AbilityIds[LeftIndex] == FBattleAbilityRules::GetLevitateId()
					? 3
					: (AbilityIds[LeftIndex] == FBattleAbilityRules::GetMoldBreakerId()
					? 2
					: 1));
		TestEqual(TEXT("Each Ability exposes only its authored hooks"),
			Definitions.Num(),
			ExpectedHookCount);

		FBattleTriggerFramework Framework;
		EBattleAbilityItemHookError HookError = EBattleAbilityItemHookError::InvalidDefinition;
		const FBattleAbilityRegistrationFacts Facts = MakeRegistrationFacts(
			AbilityIds[LeftIndex],
			100 + LeftIndex);
		TestTrue(TEXT("Canonical hooks register atomically through C08A and C07A"),
			FBattleAbilityRules::TryRegisterHooks(Framework, Facts, HookError));
		TestEqual(TEXT("Registration reports no hook error"),
			HookError,
			EBattleAbilityItemHookError::None);
		const TArray<FBattleTriggerRegistrationState> Registrations =
			Framework.GetActiveRegistrations();
		TestEqual(TEXT("Every authored hook has one active registration"),
			Registrations.Num(),
			ExpectedHookCount);
		for (int32 RegistrationIndex = 0;
			RegistrationIndex < Registrations.Num();
			++RegistrationIndex)
		{
			const FBattleTriggerRegistrationState& Registration =
				Registrations[RegistrationIndex];
			TestEqual(TEXT("Ability registrations keep their source kind"),
				Registration.Spec.SourceDefinition.Kind,
				EBattleTriggerSourceDefinitionKind::Ability);
			TestEqual(TEXT("Ability registrations keep their exact identity"),
				Registration.Spec.SourceDefinition.AbilityId,
				AbilityIds[LeftIndex]);
			TestTrue(TEXT("Creation order follows authored hook order"),
				Registration.CreationOrdinal
					== static_cast<uint64>(RegistrationIndex + 1));
			TestTrue(TEXT("Switch cleanup is explicit"),
				EnumHasAnyFlags(
					Registration.Spec.CleanupPolicy,
					EBattleTriggerCleanupPolicy::OnSwitch));
			TestTrue(TEXT("Faint cleanup is explicit"),
				EnumHasAnyFlags(
					Registration.Spec.CleanupPolicy,
					EBattleTriggerCleanupPolicy::OnFaint));
			TestTrue(TEXT("Battle-end cleanup is explicit"),
				EnumHasAnyFlags(
					Registration.Spec.CleanupPolicy,
					EBattleTriggerCleanupPolicy::OnBattleEnd));
		}

		TArray<FBattleTriggerLifecycleFact> StartedFacts;
		Framework.DrainLifecycleFacts(StartedFacts);
		FBattleTriggerCleanupRequest Cleanup;
		Cleanup.Reason = EBattleTriggerCleanupReason::Switch;
		Cleanup.AffectedOwners.Add(Facts.Owner);
		Cleanup.Context.ReentrancyToken =
			MakeNumericId<FBattleTriggerReentrancyToken>(1000 + LeftIndex);
		EBattleTriggerError TriggerError = EBattleTriggerError::InvalidCleanupRequest;
		TestTrue(TEXT("Switch cleanup applies to the exact Ability owner"),
			Framework.TryApplyCleanup(Cleanup, TriggerError));
		TestEqual(TEXT("Switch cleanup removes every hook for that owner"),
			Framework.GetActiveRegistrations().Num(),
			0);
		TArray<FBattleTriggerLifecycleFact> EndedFacts;
		Framework.DrainLifecycleFacts(EndedFacts);
		TestEqual(TEXT("Every cleaned hook emits one lifecycle fact"),
			EndedFacts.Num(),
			ExpectedHookCount);
		for (const FBattleTriggerLifecycleFact& Fact : EndedFacts)
		{
			TestTrue(TEXT("Cleanup facts are terminal switch facts"),
				Fact.Kind == EBattleTriggerLifecycleFactKind::Ended
					&& Fact.EndReason.IsSet()
					&& Fact.EndReason.GetValue() == EBattleTriggerEndReason::Switch);
		}

		TestTrue(TEXT("The same Ability hooks can register after re-entry"),
			FBattleAbilityRules::TryRegisterHooks(Framework, Facts, HookError));
		Framework.DrainLifecycleFacts(StartedFacts);
		Cleanup.Reason = EBattleTriggerCleanupReason::Faint;
		Cleanup.Context.ReentrancyToken =
			MakeNumericId<FBattleTriggerReentrancyToken>(2000 + LeftIndex);
		TestTrue(TEXT("Faint cleanup applies to the exact Ability owner"),
			Framework.TryApplyCleanup(Cleanup, TriggerError));
		TestEqual(TEXT("Faint cleanup removes every re-entry hook"),
			Framework.GetActiveRegistrations().Num(),
			0);
		Framework.DrainLifecycleFacts(EndedFacts);
		TestEqual(TEXT("Faint cleanup emits one fact per hook"),
			EndedFacts.Num(),
			ExpectedHookCount);
		for (const FBattleTriggerLifecycleFact& Fact : EndedFacts)
		{
			TestTrue(TEXT("Cleanup facts are terminal faint facts"),
				Fact.Kind == EBattleTriggerLifecycleFactKind::Ended
					&& Fact.EndReason.IsSet()
					&& Fact.EndReason.GetValue() == EBattleTriggerEndReason::Faint);
		}
	}

	TArray<FBattleAbilityItemHookDefinition> SpeedHooks;
	TestTrue(TEXT("Speed Boost has an EndTurn hook"),
		FBattleAbilityRules::TryGetHookDefinitionsForPhase(
			FBattleAbilityRules::GetSpeedBoostId(),
			EBattleTriggerPhase::EndTurn,
			SpeedHooks));
	if (SpeedHooks.Num() == 1)
	{
		TestEqual(TEXT("Speed Boost uses residual order 28"),
			SpeedHooks[0].TriggerRule.Order,
			FBattleAbilityRules::GetSpeedBoostResidualOrder());
		TestEqual(TEXT("Speed Boost uses residual suborder 2"),
			SpeedHooks[0].TriggerRule.Suborder,
			FBattleAbilityRules::GetSpeedBoostResidualSuborder());
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC08BOffensiveAbilitiesTest,
	"PokemonSolarus.Battle.C08B.Offense.BlazeOvergrowThresholdTypeSuppressionAndQ12",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC08BOffensiveAbilitiesTest::RunTest(const FString& Parameters)
{
	auto Evaluate = [this](
		const FAbilityId& AbilityId,
		const EPokemonType MoveType,
		const int32 CurrentHP,
		const int32 MaximumHP,
		const bool bSuppressed,
		const bool bExpectedApplies,
		const EBattleAbilityItemActivationOutcome ExpectedOutcome,
		const int32 ExpectedModifier)
	{
		FBattleAbilityOffensiveStatFacts Facts;
		Facts.AbilityId = AbilityId;
		Facts.MoveType = MoveType;
		Facts.CurrentHP = CurrentHP;
		Facts.BaseMaximumHP = MaximumHP;
		Facts.bSuppressed = bSuppressed;
		FBattleAbilityOffensiveStatResult Result;
		TestTrue(TEXT("A valid offensive-stat query is evaluated"),
			FBattleAbilityRules::TryEvaluateOffensiveStatModifier(Facts, Result));
		TestTrue(TEXT("The typed result is valid"), Result.bValid);
		TestEqual(TEXT("Application matches the exact query"),
			Result.bApplies,
			bExpectedApplies);
		TestEqual(TEXT("The activation outcome is explicit"),
			Result.Outcome,
			ExpectedOutcome);
		TestEqual(TEXT("The fixed-point modifier is exact"),
			Result.ModifierQ12,
			ExpectedModifier);
	};

	Evaluate(
		FBattleAbilityRules::GetBlazeId(),
		EPokemonType::Fire,
		33,
		100,
		false,
		true,
		EBattleAbilityItemActivationOutcome::Applied,
		6144);
	Evaluate(
		FBattleAbilityRules::GetBlazeId(),
		EPokemonType::Fire,
		34,
		100,
		false,
		false,
		EBattleAbilityItemActivationOutcome::Ineligible,
		4096);
	Evaluate(
		FBattleAbilityRules::GetBlazeId(),
		EPokemonType::Grass,
		1,
		100,
		false,
		false,
		EBattleAbilityItemActivationOutcome::Ineligible,
		4096);
	Evaluate(
		FBattleAbilityRules::GetOvergrowId(),
		EPokemonType::Grass,
		1,
		3,
		false,
		true,
		EBattleAbilityItemActivationOutcome::Applied,
		6144);
	Evaluate(
		FBattleAbilityRules::GetOvergrowId(),
		EPokemonType::Grass,
		1,
		3,
		true,
		false,
		EBattleAbilityItemActivationOutcome::Suppressed,
		4096);
	Evaluate(
		FBattleAbilityRules::GetOvergrowId(),
		EPokemonType::Grass,
		0,
		3,
		false,
		false,
		EBattleAbilityItemActivationOutcome::Ineligible,
		4096);

	FBattleAbilityOffensiveStatFacts InvalidFacts;
	InvalidFacts.AbilityId = FBattleAbilityRules::GetBlazeId();
	InvalidFacts.MoveType = EPokemonType::Fire;
	InvalidFacts.CurrentHP = 101;
	InvalidFacts.BaseMaximumHP = 100;
	FBattleAbilityOffensiveStatResult InvalidResult;
	TestFalse(TEXT("Impossible HP facts are rejected"),
		FBattleAbilityRules::TryEvaluateOffensiveStatModifier(
			InvalidFacts,
			InvalidResult));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC08BIntimidateTest,
	"PokemonSolarus.Battle.C08B.Entry.IntimidateTargetsBlocksAndStableOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC08BIntimidateTest::RunTest(const FString& Parameters)
{
	FBattleAbilityRegistrationFacts Registration = MakeRegistrationFacts(
		FBattleAbilityRules::GetIntimidateId(),
		301);
	Registration.Targets = {MakeBattlerSubject(322), MakeBattlerSubject(321)};
	TArray<FBattleAbilityItemHookRegistrationFacts> HookFacts;
	EBattleAbilityItemHookError HookError = EBattleAbilityItemHookError::InvalidDefinition;
	TestTrue(TEXT("Intimidate registration facts are valid"),
		FBattleAbilityRules::TryBuildHookRegistrationFacts(
			Registration,
			HookFacts,
			HookError));
	if (HookFacts.Num() == 1 && HookFacts[0].Targets.Num() == 2)
	{
		TestEqual(TEXT("The first stable target is preserved"),
			HookFacts[0].Targets[0].BattlerId,
			MakeNumericId<FBattlerId>(322));
		TestEqual(TEXT("The second stable target is preserved"),
			HookFacts[0].Targets[1].BattlerId,
			MakeNumericId<FBattlerId>(321));
	}

	auto Evaluate = [this](
		const FBattleIntimidateTargetFacts& Facts,
		const int32 ExpectedDelta,
		const EBattleAbilityItemActivationOutcome ExpectedOutcome)
	{
		FBattleIntimidateTargetResult Result;
		TestTrue(TEXT("Valid Intimidate target facts are evaluated"),
			FBattleAbilityRules::TryEvaluateIntimidateTarget(Facts, Result));
		TestEqual(TEXT("The Attack-stage delta is exact"),
			Result.AttackStageDelta,
			ExpectedDelta);
		TestEqual(TEXT("The target outcome is explicit"), Result.Outcome, ExpectedOutcome);
	};

	FBattleIntimidateTargetFacts Base;
	Base.bAdjacentOpponent = true;
	Base.bTargetAbleToBattle = true;
	Evaluate(Base, -1, EBattleAbilityItemActivationOutcome::Applied);
	FBattleIntimidateTargetFacts Substitute = Base;
	Substitute.bSubstituteActive = true;
	Evaluate(Substitute, 0, EBattleAbilityItemActivationOutcome::AttemptedButPrevented);
	FBattleIntimidateTargetFacts Mist = Base;
	Mist.bStatStageDropPrevented = true;
	Evaluate(Mist, 0, EBattleAbilityItemActivationOutcome::AttemptedButPrevented);
	FBattleIntimidateTargetFacts Capped = Base;
	Capped.CurrentAttackStage = -6;
	Evaluate(Capped, 0, EBattleAbilityItemActivationOutcome::AttemptedButPrevented);
	FBattleIntimidateTargetFacts Suppressed = Base;
	Suppressed.bSuppressed = true;
	Evaluate(Suppressed, 0, EBattleAbilityItemActivationOutcome::Suppressed);
	FBattleIntimidateTargetFacts NonAdjacent = Base;
	NonAdjacent.bAdjacentOpponent = false;
	Evaluate(NonAdjacent, 0, EBattleAbilityItemActivationOutcome::Ineligible);
	FBattleIntimidateTargetFacts Fainted = Base;
	Fainted.bTargetAbleToBattle = false;
	Evaluate(Fainted, 0, EBattleAbilityItemActivationOutcome::Ineligible);
	FBattleIntimidateTargetFacts Invalid = Base;
	Invalid.CurrentAttackStage = 7;
	FBattleIntimidateTargetResult InvalidResult;
	TestFalse(TEXT("Out-of-range stage facts are rejected"),
		FBattleAbilityRules::TryEvaluateIntimidateTarget(Invalid, InvalidResult));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC08BLevitateTest,
	"PokemonSolarus.Battle.C08B.Levitate.GroundingImmunitySuppressionAndMoveIgnore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC08BLevitateTest::RunTest(const FString& Parameters)
{
	const FAbilityId Levitate = FBattleAbilityRules::GetLevitateId();
	TestTrue(TEXT("Active Levitate makes its owner airborne"),
		FBattleAbilityRules::IsLevitateAirborne(Levitate, false));
	TestFalse(TEXT("Suppression removes Levitate's airborne effect"),
		FBattleAbilityRules::IsLevitateAirborne(Levitate, true));
	TestFalse(TEXT("Move-scoped ignore removes Levitate only for that move"),
		FBattleAbilityRules::IsLevitateAirborne(Levitate, false, true));
	TestTrue(TEXT("Levitate prevents Ground moves"),
		FBattleAbilityRules::ShouldLevitatePreventMove(
			Levitate,
			EPokemonType::Ground,
			false,
			false));
	TestFalse(TEXT("Levitate does not prevent non-Ground moves"),
		FBattleAbilityRules::ShouldLevitatePreventMove(
			Levitate,
			EPokemonType::Fire,
			false,
			false));
	TestFalse(TEXT("Suppressed Levitate provides no Ground immunity"),
		FBattleAbilityRules::ShouldLevitatePreventMove(
			Levitate,
			EPokemonType::Ground,
			true,
			false));
	TestFalse(TEXT("Ignored Levitate provides no Ground immunity for that move"),
		FBattleAbilityRules::ShouldLevitatePreventMove(
			Levitate,
			EPokemonType::Ground,
			false,
			true));
	TestFalse(TEXT("A different Ability cannot supply Levitate grounding"),
		FBattleAbilityRules::IsLevitateAirborne(
			FBattleAbilityRules::GetBlazeId(),
			false));

	TArray<FBattleAbilityItemHookDefinition> Hooks;
	TestTrue(TEXT("Levitate exposes one BeforeHit hook"),
		FBattleAbilityRules::TryGetHookDefinitionsForPhase(
			Levitate,
			EBattleTriggerPhase::BeforeHit,
			Hooks));
	if (Hooks.Num() == 1)
	{
		TestEqual(TEXT("Levitate hooks the type-immunity seam"),
			Hooks[0].HookPoint,
			EBattleAbilityItemHookPoint::TypeImmunity);
		TestTrue(TEXT("Levitate is explicitly breakable"), Hooks[0].bBreakable);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC08BDrizzleTest,
	"PokemonSolarus.Battle.C08B.Entry.DrizzleCreateReplaceNoRefreshAndSuppression",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC08BDrizzleTest::RunTest(const FString& Parameters)
{
	FBattleDrizzleEntryResult Result;
	TestTrue(TEXT("Drizzle evaluates an empty weather slot"),
		FBattleAbilityRules::TryEvaluateDrizzleEntry(
			FBattleAbilityRules::GetDrizzleId(),
			FConditionId(),
			false,
			Result));
	TestEqual(TEXT("Drizzle creates Rain"),
		Result.RainId,
		FBattleFieldSideConditionRules::GetRainId());
	TestEqual(TEXT("Drizzle creates five-turn Rain"), Result.DurationTurns, 5);
	TestFalse(TEXT("An empty weather slot is not a replacement"),
		Result.bReplacesExistingWeather);
	TestEqual(TEXT("Rain creation applies"),
		Result.Outcome,
		EBattleAbilityItemActivationOutcome::Applied);

	TestTrue(TEXT("Drizzle evaluates replacement weather"),
		FBattleAbilityRules::TryEvaluateDrizzleEntry(
			FBattleAbilityRules::GetDrizzleId(),
			FBattleFieldSideConditionRules::GetSunId(),
			false,
			Result));
	TestTrue(TEXT("Different weather is replaced"), Result.bReplacesExistingWeather);
	TestEqual(TEXT("Replacement still uses five turns"), Result.DurationTurns, 5);

	TestTrue(TEXT("Drizzle evaluates identical Rain"),
		FBattleAbilityRules::TryEvaluateDrizzleEntry(
			FBattleAbilityRules::GetDrizzleId(),
			FBattleFieldSideConditionRules::GetRainId(),
			false,
			Result));
	TestEqual(TEXT("Identical Rain is not refreshed"),
		Result.Outcome,
		EBattleAbilityItemActivationOutcome::Ineligible);
	TestFalse(TEXT("Identical Rain is not marked as replacement"),
		Result.bReplacesExistingWeather);

	TestTrue(TEXT("Suppressed Drizzle is a valid nonactivation"),
		FBattleAbilityRules::TryEvaluateDrizzleEntry(
			FBattleAbilityRules::GetDrizzleId(),
			FConditionId(),
			true,
			Result));
	TestEqual(TEXT("Suppression is explicit"),
		Result.Outcome,
		EBattleAbilityItemActivationOutcome::Suppressed);
	TestFalse(TEXT("A different Ability cannot create Drizzle Rain"),
		FBattleAbilityRules::TryEvaluateDrizzleEntry(
			FBattleAbilityRules::GetBlazeId(),
			FConditionId(),
			false,
			Result));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC08BSpeedBoostTest,
	"PokemonSolarus.Battle.C08B.EndTurn.SpeedBoostEligibilityOrderCapAndSuppression",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC08BSpeedBoostTest::RunTest(const FString& Parameters)
{
	const FAbilityId SpeedBoost = FBattleAbilityRules::GetSpeedBoostId();
	TestFalse(TEXT("Speed Boost does not run on its entry turn"),
		FBattleAbilityRules::ShouldApplySpeedBoost(SpeedBoost, 0, 0, false));
	TestTrue(TEXT("Speed Boost runs after one active turn"),
		FBattleAbilityRules::ShouldApplySpeedBoost(SpeedBoost, 1, 0, false));
	TestTrue(TEXT("Speed Boost can raise a negative stage"),
		FBattleAbilityRules::ShouldApplySpeedBoost(SpeedBoost, 1, -6, false));
	TestFalse(TEXT("Speed Boost does not run at the positive cap"),
		FBattleAbilityRules::ShouldApplySpeedBoost(SpeedBoost, 1, 6, false));
	TestFalse(TEXT("Suppressed Speed Boost does not run"),
		FBattleAbilityRules::ShouldApplySpeedBoost(SpeedBoost, 1, 0, true));
	TestFalse(TEXT("A different Ability cannot supply Speed Boost"),
		FBattleAbilityRules::ShouldApplySpeedBoost(
			FBattleAbilityRules::GetBlazeId(),
			1,
			0,
			false));

	TArray<FBattleAbilityItemHookDefinition> Hooks;
	TestTrue(TEXT("Speed Boost exposes its EndTurn hook"),
		FBattleAbilityRules::TryGetHookDefinitionsForPhase(
			SpeedBoost,
			EBattleTriggerPhase::EndTurn,
			Hooks));
	if (Hooks.Num() == 1)
	{
		TestEqual(TEXT("Speed Boost is ordered after approved residual families"),
			Hooks[0].TriggerRule.Order,
			28);
		TestEqual(TEXT("Speed Boost keeps its approved residual suborder"),
			Hooks[0].TriggerRule.Suborder,
			2);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC08BMagicGuardTest,
	"PokemonSolarus.Battle.C08B.MagicGuard.IndirectDamageFamiliesAndExceptions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC08BMagicGuardTest::RunTest(const FString& Parameters)
{
	const FAbilityId MagicGuard = FBattleAbilityRules::GetMagicGuardId();
	for (const EBattleHPChangeSourceKind PreventedSource :
		{
			EBattleHPChangeSourceKind::Condition,
			EBattleHPChangeSourceKind::Field,
			EBattleHPChangeSourceKind::Volatile,
			EBattleHPChangeSourceKind::Ability,
			EBattleHPChangeSourceKind::Item,
			EBattleHPChangeSourceKind::OtherIndirect
		})
	{
		TestTrue(TEXT("Magic Guard blocks every approved indirect source family"),
			FBattleAbilityRules::ShouldMagicGuardPreventDamage(
				MagicGuard,
				PreventedSource,
				false));
	}
	for (const EBattleHPChangeSourceKind AllowedSource :
		{
			EBattleHPChangeSourceKind::Move,
			EBattleHPChangeSourceKind::Cost,
			EBattleHPChangeSourceKind::Invalid
		})
	{
		TestFalse(TEXT("Magic Guard does not block direct moves, HP costs, or invalid facts"),
			FBattleAbilityRules::ShouldMagicGuardPreventDamage(
				MagicGuard,
				AllowedSource,
				false));
	}
	TestFalse(TEXT("Suppression disables Magic Guard"),
		FBattleAbilityRules::ShouldMagicGuardPreventDamage(
			MagicGuard,
			EBattleHPChangeSourceKind::Condition,
			true));
	TestFalse(TEXT("A different Ability cannot supply Magic Guard"),
		FBattleAbilityRules::ShouldMagicGuardPreventDamage(
			FBattleAbilityRules::GetBlazeId(),
			EBattleHPChangeSourceKind::Item,
			false));

	for (const EBattleTriggerPhase Phase :
		{
			EBattleTriggerPhase::SwitchIn,
			EBattleTriggerPhase::BeforeAction,
			EBattleTriggerPhase::AfterDamage,
			EBattleTriggerPhase::EndTurn
		})
	{
		TArray<FBattleAbilityItemHookDefinition> Hooks;
		TestTrue(TEXT("Magic Guard owns every required indirect-damage phase seam"),
			FBattleAbilityRules::TryGetHookDefinitionsForPhase(
				MagicGuard,
				Phase,
				Hooks));
		TestEqual(TEXT("Each Magic Guard phase has one unambiguous hook"),
			Hooks.Num(),
			1);
		if (Hooks.Num() == 1)
		{
			TestFalse(TEXT("Magic Guard is nonbreakable"), Hooks[0].bBreakable);
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC08BMoldBreakerTest,
	"PokemonSolarus.Battle.C08B.MoldBreaker.PerMoveBreakableOnlyBehavior",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC08BMoldBreakerTest::RunTest(const FString& Parameters)
{
	TArray<FBattleAbilityItemHookDefinition> LevitateHooks;
	TestTrue(TEXT("Levitate's BeforeHit hook is available"),
		FBattleAbilityRules::TryGetHookDefinitionsForPhase(
			FBattleAbilityRules::GetLevitateId(),
			EBattleTriggerPhase::BeforeHit,
			LevitateHooks));
	if (LevitateHooks.Num() == 1)
	{
		TestTrue(TEXT("Mold Breaker ignores canonical breakable Levitate per move"),
			FBattleAbilityRules::ShouldMoldBreakerIgnoreDefenderHookForMove(
				FBattleAbilityRules::GetMoldBreakerId(),
				false,
				FBattleAbilityRules::GetLevitateId(),
				LevitateHooks[0]));
		TestFalse(TEXT("Suppressed Mold Breaker ignores nothing"),
			FBattleAbilityRules::ShouldMoldBreakerIgnoreDefenderHookForMove(
				FBattleAbilityRules::GetMoldBreakerId(),
				true,
				FBattleAbilityRules::GetLevitateId(),
				LevitateHooks[0]));
		TestFalse(TEXT("A different attacker Ability ignores nothing"),
			FBattleAbilityRules::ShouldMoldBreakerIgnoreDefenderHookForMove(
				FBattleAbilityRules::GetBlazeId(),
				false,
				FBattleAbilityRules::GetLevitateId(),
				LevitateHooks[0]));
		FBattleAbilityItemHookDefinition Mutated = LevitateHooks[0];
		Mutated.bBreakable = false;
		TestFalse(TEXT("Mold Breaker cannot ignore a hook merely resembling Levitate"),
			FBattleAbilityRules::ShouldMoldBreakerIgnoreDefenderHookForMove(
				FBattleAbilityRules::GetMoldBreakerId(),
				false,
				FBattleAbilityRules::GetLevitateId(),
				Mutated));
	}

	TArray<FBattleAbilityItemHookDefinition> MagicGuardHooks;
	TestTrue(TEXT("Magic Guard's EndTurn hook is available"),
		FBattleAbilityRules::TryGetHookDefinitionsForPhase(
			FBattleAbilityRules::GetMagicGuardId(),
			EBattleTriggerPhase::EndTurn,
			MagicGuardHooks));
	if (MagicGuardHooks.Num() == 1)
	{
		TestFalse(TEXT("Mold Breaker never ignores nonbreakable Magic Guard"),
			FBattleAbilityRules::ShouldMoldBreakerIgnoreDefenderHookForMove(
				FBattleAbilityRules::GetMoldBreakerId(),
				false,
				FBattleAbilityRules::GetMagicGuardId(),
				MagicGuardHooks[0]));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC08BVisibilityTest,
	"PokemonSolarus.Battle.C08B.Visibility.FirstRepeatPublicAttemptAndHiddenFailures",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC08BVisibilityTest::RunTest(const FString& Parameters)
{
	FBattleAbilityItemEffectRequest BlazeRequest;
	TestTrue(TEXT("A canonical Blaze request is produced through C07A"),
		TryMakeTypedRequest(
			FBattleAbilityRules::GetBlazeId(),
			EBattleTriggerPhase::BeforeDamage,
			701,
			BlazeRequest));
	FBattleAbilityItemRevealTracker Tracker;
	TOptional<FBattleAbilityItemActivationFact> Fact;
	EBattleAbilityItemHookError Error = EBattleAbilityItemHookError::InvalidDefinition;
	for (const EBattleAbilityItemActivationOutcome HiddenOutcome :
		{
			EBattleAbilityItemActivationOutcome::AttemptedButPrevented,
			EBattleAbilityItemActivationOutcome::Ineligible,
			EBattleAbilityItemActivationOutcome::Suppressed,
			EBattleAbilityItemActivationOutcome::Ignored
		})
	{
		TestTrue(TEXT("A hidden Blaze nonactivation is a valid evaluation"),
			Tracker.TryRecordActivation(BlazeRequest, HiddenOutcome, Fact, Error));
		TestFalse(TEXT("Hidden nonactivation emits no public fact"), Fact.IsSet());
	}
	TestTrue(TEXT("An applied Blaze effect is recorded"),
		Tracker.TryRecordActivation(
			BlazeRequest,
			EBattleAbilityItemActivationOutcome::Applied,
			Fact,
			Error));
	TestTrue(TEXT("The first Blaze application reveals its Ability"),
		Fact.IsSet()
			&& Fact->bFirstPublicReveal
			&& Fact->RevealedSourceDefinition.IsSet());
	TestTrue(TEXT("A repeated Blaze effect is recorded"),
		Tracker.TryRecordActivation(
			BlazeRequest,
			EBattleAbilityItemActivationOutcome::Applied,
			Fact,
			Error));
	TestTrue(TEXT("A repeated public activation is not a first reveal"),
		Fact.IsSet()
			&& !Fact->bFirstPublicReveal
			&& Fact->RevealedSourceDefinition.IsSet());

	FBattleAbilityItemEffectRequest IntimidateRequest;
	TestTrue(TEXT("A canonical Intimidate request is produced through C07A"),
		TryMakeTypedRequest(
			FBattleAbilityRules::GetIntimidateId(),
			EBattleTriggerPhase::SwitchIn,
			702,
			IntimidateRequest));
	TestTrue(TEXT("A public Intimidate attempt can reveal when blocked"),
		Tracker.TryRecordActivation(
			IntimidateRequest,
			EBattleAbilityItemActivationOutcome::AttemptedButPrevented,
			Fact,
			Error));
	TestTrue(TEXT("The blocked public attempt emits its first reveal"),
		Fact.IsSet()
			&& Fact->bFirstPublicReveal
			&& Fact->RevealedSourceDefinition.IsSet());
	TestTrue(TEXT("Suppressed Intimidate is a valid hidden result"),
		Tracker.TryRecordActivation(
			IntimidateRequest,
			EBattleAbilityItemActivationOutcome::Suppressed,
			Fact,
			Error));
	TestFalse(TEXT("Suppression never leaks the Ability"), Fact.IsSet());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC08BEngineEntryTest,
	"PokemonSolarus.Battle.C08B.Integration.InitialRegistrationEntryRevealAndStateFields",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC08BEngineEntryTest::RunTest(const FString& Parameters)
{
	TUniquePtr<FBattleEngine> Engine;
	FBattleRejection Rejection;
	TestTrue(TEXT("The C08B integration engine is created"),
		FBattleEngine::TryCreate(
			MakeIntegrationSetup(),
			MakeIntegrationCatalog(),
			MakeUnique<FSeededBattleRandom>(808),
			Engine,
			Rejection));
	if (!Engine.IsValid())
	{
		return false;
	}

	const FBattleSnapshot SnapshotBefore =
		Engine->GetSnapshotForObserver(MakeNumericId<FTrainerId>(1));
	const FBattleObservedBattler* OpponentBefore =
		SnapshotBefore.FindObservedBattler(MakeNumericId<FBattlerId>(21));
	TestNotNull(TEXT("The opponent is present in the observer snapshot"), OpponentBefore);
	if (OpponentBefore != nullptr)
	{
		TestFalse(TEXT("Mold Breaker begins hidden before its official entry trigger"),
			OpponentBefore->bAbilityKnown);
	}

	const FBattleEngineState& BeforeState = FBattleC08BEngineFixture::GetState(*Engine);
	const FBattleBattlerState* PlayerBefore =
		BeforeState.FindBattler(MakeNumericId<FBattlerId>(11));
	const FBattleBattlerState* OpponentStateBefore =
		BeforeState.FindBattler(MakeNumericId<FBattlerId>(21));
	TestNotNull(TEXT("The player battler is present in authoritative state"), PlayerBefore);
	TestNotNull(TEXT("The opponent battler is present in authoritative state"), OpponentStateBefore);
	if (PlayerBefore != nullptr && OpponentStateBefore != nullptr)
	{
		TestFalse(TEXT("Starting actives have no switch-entry turn ID"),
			PlayerBefore->EnteredActiveOnTurnId.IsValid()
				|| OpponentStateBefore->EnteredActiveOnTurnId.IsValid());
		TestFalse(TEXT("Ability suppression starts disabled"),
			PlayerBefore->bAbilitySuppressed || OpponentStateBefore->bAbilitySuppressed);
	}

	TestTrue(TEXT("Starting action selection resolves initial Ability entry"),
		Engine->TryBeginActionDecisionSequence(Rejection));
	const FBattleEngineState& AfterState = FBattleC08BEngineFixture::GetState(*Engine);
	const TArray<FBattleTriggerRegistrationState> Registrations =
		AfterState.TriggerFramework.GetActiveRegistrations();
	TestEqual(TEXT("Blaze and Mold Breaker register all three authored hooks"),
		Registrations.Num(),
		3);
	for (const FBattleTriggerRegistrationState& Registration : Registrations)
	{
		TestEqual(TEXT("Initial entry registers only Ability sources"),
			Registration.Spec.SourceDefinition.Kind,
			EBattleTriggerSourceDefinitionKind::Ability);
	}

	const FBattleEvent* MoldBreakerEvent = AfterState.OrderedEvents.FindByPredicate(
		[](const FBattleEvent& Event)
		{
			return Event.GetType() == EBattleEventType::AbilityActivated
				&& Event.GetSource().DefinitionId
					== FBattleAbilityRules::GetMoldBreakerId().GetDefinitionId();
		});
	TestNotNull(TEXT("Mold Breaker emits its immediate entry activation"), MoldBreakerEvent);
	if (MoldBreakerEvent != nullptr)
	{
		TestEqual(TEXT("The entry reveal is attributed to the Ability owner"),
			MoldBreakerEvent->GetSource().BattlerId,
			MakeNumericId<FBattlerId>(21));
		TestTrue(TEXT("The official entry activation reveals the source definition"),
			MoldBreakerEvent->GetVisibility().bRevealSourceDefinition);
	}

	const FBattleSnapshot SnapshotAfter =
		Engine->GetSnapshotForObserver(MakeNumericId<FTrainerId>(1));
	const FBattleObservedBattler* OpponentAfter =
		SnapshotAfter.FindObservedBattler(MakeNumericId<FBattlerId>(21));
	TestNotNull(TEXT("The opponent remains present after entry"), OpponentAfter);
	if (OpponentAfter != nullptr)
	{
		TestTrue(TEXT("The official Mold Breaker entry trigger updates observer knowledge"),
			OpponentAfter->bAbilityKnown);
		TestEqual(TEXT("The revealed Ability identity is exact"),
			OpponentAfter->AbilityId,
			FBattleAbilityRules::GetMoldBreakerId());
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC08BEngineEntryEffectsTest,
	"PokemonSolarus.Battle.C08B.Integration.IntimidateAndDrizzleRuntime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC08BEngineEntryEffectsTest::RunTest(const FString& Parameters)
{
	TUniquePtr<FBattleEngine> Engine = MakeIntegrationEngine(
		FBattleAbilityRules::GetIntimidateId(),
		FBattleAbilityRules::GetDrizzleId(),
		EPokemonType::Normal,
		200,
		809);
	FBattleRejection Rejection;
	TestTrue(TEXT("Initial entry Ability effects resolve"),
		Engine->TryBeginActionDecisionSequence(Rejection));
	const FBattleEngineState& State = FBattleC08BEngineFixture::GetState(*Engine);
	const FBattleBattlerState* Opponent = State.FindBattler(
		MakeNumericId<FBattlerId>(21));
	TestNotNull(TEXT("The Intimidate target remains active"), Opponent);
	if (Opponent != nullptr)
	{
		int32 AttackStage = 0;
		TestTrue(TEXT("The target Attack stage is readable"),
			Opponent->Stages.TryGetStage(EBattleStat::Attack, AttackStage));
		TestEqual(TEXT("Intimidate applies exactly one Attack drop"), AttackStage, -1);
	}
	TestTrue(TEXT("Drizzle creates the field weather slot"), State.Field.Weather.IsSet());
	if (State.Field.Weather.IsSet())
	{
		TestEqual(TEXT("Drizzle creates Rain"),
			State.Field.Weather.GetValue().ConditionId,
			FBattleFieldSideConditionRules::GetRainId());
		TestTrue(TEXT("Drizzle Rain has a five-turn duration"),
			State.Field.Weather.GetValue().RemainingTurns.IsSet()
				&& State.Field.Weather.GetValue().RemainingTurns.GetValue() == 5);
	}
	for (const FAbilityId& AbilityId : {
		FBattleAbilityRules::GetIntimidateId(),
		FBattleAbilityRules::GetDrizzleId()})
	{
		TestTrue(TEXT("Each applied entry Ability emits an activation"),
			State.OrderedEvents.ContainsByPredicate(
				[AbilityId](const FBattleEvent& Event)
				{
					return Event.GetType() == EBattleEventType::AbilityActivated
						&& Event.GetSource().DefinitionId
							== AbilityId.GetDefinitionId();
				}));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC08BEngineEntryOrderTest,
	"PokemonSolarus.Battle.C08B.Integration.EntryAbilityEffectiveSpeedOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC08BEngineEntryOrderTest::RunTest(const FString& Parameters)
{
	const FBattlerId PlayerId = MakeNumericId<FBattlerId>(11);
	TUniquePtr<FBattleEngine> ParalysisEngine = MakeIntegrationEngine(
		FBattleAbilityRules::GetIntimidateId(),
		FBattleAbilityRules::GetDrizzleId(),
		EPokemonType::Normal,
		200,
		815);
	TestTrue(TEXT("Paralysis is installed before simultaneous entry ordering"),
		FBattleC08BEngineFixture::AddMajorStatus(
			*ParalysisEngine,
			PlayerId,
			FBattleMajorStatusRules::GetParalysisId()));
	FBattleRejection Rejection;
	TestTrue(TEXT("Paralyzed entry Abilities resolve"),
		ParalysisEngine->TryBeginActionDecisionSequence(Rejection));
	const FBattleEngineState& ParalysisState = FBattleC08BEngineFixture::GetState(
		*ParalysisEngine);
	const int32 ParalyzedIntimidateIndex = FindFirstAbilityActivationIndex(
		ParalysisState,
		FBattleAbilityRules::GetIntimidateId());
	const int32 FasterDrizzleIndex = FindFirstAbilityActivationIndex(
		ParalysisState,
		FBattleAbilityRules::GetDrizzleId());
	TestTrue(TEXT("Paralysis changes simultaneous Ability order through effective Speed"),
		FasterDrizzleIndex != INDEX_NONE
			&& ParalyzedIntimidateIndex != INDEX_NONE
			&& FasterDrizzleIndex < ParalyzedIntimidateIndex);

	TUniquePtr<FBattleEngine> TailwindEngine = MakeIntegrationEngine(
		FBattleAbilityRules::GetIntimidateId(),
		FBattleAbilityRules::GetDrizzleId(),
		EPokemonType::Normal,
		200,
		816);
	FBattleBattlerState* TailwindOwner = FBattleC08BEngineFixture::GetMutableState(
		*TailwindEngine).FindMutableBattler(PlayerId);
	TestNotNull(TEXT("The Tailwind entry owner exists"), TailwindOwner);
	if (TailwindOwner != nullptr)
	{
		TailwindOwner->PermanentStats.Speed = 60;
	}
	TestTrue(TEXT("Tailwind is installed before simultaneous entry ordering"),
		FBattleC08BEngineFixture::SeedCondition(
			*TailwindEngine,
			FBattleFieldSideConditionRules::GetTailwindId(),
			EBattleSide::Player,
			PlayerId,
			TOptional<int32>(4)));
	TestTrue(TEXT("Tailwind entry Abilities resolve"),
		TailwindEngine->TryBeginActionDecisionSequence(Rejection));
	const FBattleEngineState& TailwindState = FBattleC08BEngineFixture::GetState(
		*TailwindEngine);
	const int32 TailwindIntimidateIndex = FindFirstAbilityActivationIndex(
		TailwindState,
		FBattleAbilityRules::GetIntimidateId());
	const int32 SlowerDrizzleIndex = FindFirstAbilityActivationIndex(
		TailwindState,
		FBattleAbilityRules::GetDrizzleId());
	TestTrue(TEXT("Tailwind changes simultaneous Ability order through effective Speed"),
		TailwindIntimidateIndex != INDEX_NONE
			&& SlowerDrizzleIndex != INDEX_NONE
			&& TailwindIntimidateIndex < SlowerDrizzleIndex);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC08BEngineLevitateTest,
	"PokemonSolarus.Battle.C08B.Integration.LevitateAndMoldBreakerRuntime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC08BEngineLevitateTest::RunTest(const FString& Parameters)
{
	TUniquePtr<FBattleEngine> Blocked = MakeIntegrationEngine(
		FBattleAbilityRules::GetOvergrowId(),
		FBattleAbilityRules::GetLevitateId(),
		EPokemonType::Ground,
		200,
		810);
	FBattleResolution BlockedEffects;
	TestTrue(TEXT("A Ground move reaches the production executor"),
		ExecuteFirstIntegrationMove(*Blocked, BlockedEffects));
	const FBattleSnapshot BlockedSnapshot = Blocked->GetSnapshot();
	const FBattlePartyEntrySetup* BlockedTarget = BlockedSnapshot.FindBattler(
		MakeNumericId<FBattlerId>(21));
	TestNotNull(TEXT("The Levitate target remains visible"), BlockedTarget);
	if (BlockedTarget != nullptr)
	{
		TestEqual(TEXT("Levitate prevents all direct Ground damage"),
			BlockedTarget->CurrentHP,
			200);
	}
	const FBattleEngineState& BlockedState = FBattleC08BEngineFixture::GetState(*Blocked);
	TestTrue(TEXT("Applied Levitate emits a public activation"),
		BlockedState.OrderedEvents.ContainsByPredicate(
			[](const FBattleEvent& Event)
			{
				return Event.GetType() == EBattleEventType::AbilityActivated
					&& Event.GetSource().DefinitionId
						== FBattleAbilityRules::GetLevitateId().GetDefinitionId();
			}));

	TUniquePtr<FBattleEngine> Ignored = MakeIntegrationEngine(
		FBattleAbilityRules::GetMoldBreakerId(),
		FBattleAbilityRules::GetLevitateId(),
		EPokemonType::Ground,
		200,
		810);
	FBattleResolution IgnoredEffects;
	TestTrue(TEXT("Mold Breaker reaches the same production executor"),
		ExecuteFirstIntegrationMove(*Ignored, IgnoredEffects));
	const FBattleSnapshot IgnoredSnapshot = Ignored->GetSnapshot();
	const FBattlePartyEntrySetup* IgnoredTarget = IgnoredSnapshot.FindBattler(
		MakeNumericId<FBattlerId>(21));
	TestNotNull(TEXT("The ignored Levitate target remains visible"), IgnoredTarget);
	if (IgnoredTarget != nullptr)
	{
		TestTrue(TEXT("Mold Breaker ignores Levitate only for its Ground move"),
			IgnoredTarget->CurrentHP < 200);
	}
	const FBattleEngineState& IgnoredState = FBattleC08BEngineFixture::GetState(*Ignored);
	TestFalse(TEXT("Ignored Levitate does not reveal itself"),
		IgnoredState.OrderedEvents.ContainsByPredicate(
			[](const FBattleEvent& Event)
			{
				return Event.GetType() == EBattleEventType::AbilityActivated
					&& Event.GetSource().DefinitionId
						== FBattleAbilityRules::GetLevitateId().GetDefinitionId();
			}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC08BEngineLevitateGroundedTest,
	"PokemonSolarus.Battle.C08B.Integration.LevitateHazardsTerrainAndSwitchCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC08BEngineLevitateGroundedTest::RunTest(const FString& Parameters)
{
	const FBattlerId PlayerId = MakeNumericId<FBattlerId>(11);
	const FBattlerId ReserveId = MakeNumericId<FBattlerId>(12);
	const FBattlerId OpponentId = MakeNumericId<FBattlerId>(21);
	TUniquePtr<FBattleEngine> SwitchEngine = MakeIntegrationEngine(
		FBattleAbilityRules::GetBlazeId(),
		FBattleAbilityRules::GetMoldBreakerId(),
		EPokemonType::Normal,
		200,
		817);
	TestTrue(TEXT("Spikes is seeded before the Levitate switch"),
		FBattleC08BEngineFixture::SeedCondition(
			*SwitchEngine,
			FBattleFieldSideConditionRules::GetSpikesId(),
			EBattleSide::Player,
			OpponentId,
			TOptional<int32>(),
			1));
	TestTrue(TEXT("Stealth Rock is seeded after Spikes"),
		FBattleC08BEngineFixture::SeedCondition(
			*SwitchEngine,
			FBattleFieldSideConditionRules::GetStealthRockId(),
			EBattleSide::Player,
			OpponentId,
			TOptional<int32>(),
			1));
	FBattleRejection Rejection;
	TestTrue(TEXT("The initial Blaze hook is registered before the real switch"),
		SwitchEngine->TryBeginActionDecisionSequence(Rejection));
	TestTrue(TEXT("The outgoing active owns a live Ability registration"),
		FBattleC08BEngineFixture::HasAbilityRegistration(*SwitchEngine, PlayerId));
	TestTrue(TEXT("A production voluntary switch to Levitate is prepared"),
		FBattleC08BEngineFixture::PrepareLockedSwitch(
			*SwitchEngine,
			PlayerId,
			MakePartySlotId(1)));
	TestTrue(TEXT("The Levitate switch action starts"),
		SwitchEngine->BeginNextLockedAction().WasAccepted());
	const FBattleResolution LevitateSwitch = SwitchEngine->ExecuteCurrentSwitch();
	TestTrue(TEXT("The Levitate switch action resolves"), LevitateSwitch.WasAccepted());
	const FBattleSnapshot SwitchSnapshotAfter = SwitchEngine->GetSnapshot();
	const FBattlePartyEntrySetup* ReserveAfter = SwitchSnapshotAfter.FindBattler(
		ReserveId);
	TestNotNull(TEXT("The Levitate reserve is visible after entry"), ReserveAfter);
	if (ReserveAfter != nullptr)
	{
		TestEqual(TEXT("Levitate skips Spikes but not Stealth Rock"),
			ReserveAfter->CurrentHP,
			175);
	}
	TestFalse(TEXT("The switch cleans every outgoing Blaze registration"),
		FBattleC08BEngineFixture::HasAbilityRegistration(*SwitchEngine, PlayerId));
	TestTrue(TEXT("The incoming Levitate hooks are live"),
		FBattleC08BEngineFixture::HasAbilityRegistration(*SwitchEngine, ReserveId));
	TestTrue(TEXT("Skipping a grounded hazard publicly activates Levitate"),
		LevitateSwitch.GetEvents().ContainsByPredicate(
			[](const FBattleEvent& Event)
			{
				return Event.GetType() == EBattleEventType::AbilityActivated
					&& Event.GetSource().DefinitionId
						== FBattleAbilityRules::GetLevitateId().GetDefinitionId()
					&& Event.GetVisibility().bRevealSourceDefinition;
			}));
	TestTrue(TEXT("A production switch back from Levitate is prepared"),
		FBattleC08BEngineFixture::PrepareLockedSwitch(
			*SwitchEngine,
			ReserveId,
			MakePartySlotId(0)));
	TestTrue(TEXT("The switch back starts"),
		SwitchEngine->BeginNextLockedAction().WasAccepted());
	TestTrue(TEXT("The switch back resolves"),
		SwitchEngine->ExecuteCurrentSwitch().WasAccepted());
	TestFalse(TEXT("Switching out cleans all three Levitate registrations"),
		FBattleC08BEngineFixture::HasAbilityRegistration(*SwitchEngine, ReserveId));

	TUniquePtr<FBattleEngine> TerrainLevitate = MakeIntegrationEngine(
		FBattleAbilityRules::GetBlazeId(),
		FBattleAbilityRules::GetLevitateId(),
		EPokemonType::Dragon,
		200,
		818);
	TUniquePtr<FBattleEngine> TerrainGrounded = MakeIntegrationEngine(
		FBattleAbilityRules::GetBlazeId(),
		FBattleAbilityRules::GetBlazeId(),
		EPokemonType::Dragon,
		200,
		818);
	for (FBattleEngine* Engine : {TerrainLevitate.Get(), TerrainGrounded.Get()})
	{
		TestTrue(TEXT("Misty Terrain is seeded for the grounded interaction"),
			FBattleC08BEngineFixture::SeedCondition(
				*Engine,
				FBattleFieldSideConditionRules::GetMistyTerrainId(),
				EBattleSide::Player,
				PlayerId,
				TOptional<int32>(5)));
	}
	FBattleResolution LevitateDamage;
	FBattleResolution GroundedDamage;
	TestTrue(TEXT("The Levitate terrain damage path executes"),
		ExecuteFirstIntegrationMove(*TerrainLevitate, LevitateDamage));
	TestTrue(TEXT("The grounded terrain control executes"),
		ExecuteFirstIntegrationMove(*TerrainGrounded, GroundedDamage));
	const FBattleSnapshot LevitateSnapshot = TerrainLevitate->GetSnapshot();
	const FBattleSnapshot GroundedSnapshot = TerrainGrounded->GetSnapshot();
	const FBattlePartyEntrySetup* LevitateTarget = LevitateSnapshot.FindBattler(
		OpponentId);
	const FBattlePartyEntrySetup* GroundedTarget = GroundedSnapshot.FindBattler(
		OpponentId);
	TestNotNull(TEXT("The Levitate terrain target remains visible"), LevitateTarget);
	TestNotNull(TEXT("The grounded terrain target remains visible"), GroundedTarget);
	if (LevitateTarget != nullptr && GroundedTarget != nullptr)
	{
		TestTrue(TEXT("Levitate makes the defender ineligible for Misty Terrain reduction"),
			LevitateTarget->CurrentHP < GroundedTarget->CurrentHP);
	}
	TestTrue(TEXT("A public terrain damage difference reveals Levitate"),
		HasAbilityActivation(
			FBattleC08BEngineFixture::GetState(*TerrainLevitate),
			FBattleAbilityRules::GetLevitateId()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC08BEngineBlazeTest,
	"PokemonSolarus.Battle.C08B.Integration.BlazePerHitRuntime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC08BEngineBlazeTest::RunTest(const FString& Parameters)
{
	TUniquePtr<FBattleEngine> LowHP = MakeIntegrationEngine(
		FBattleAbilityRules::GetBlazeId(),
		FBattleAbilityRules::GetOvergrowId(),
		EPokemonType::Fire,
		60,
		811);
	TUniquePtr<FBattleEngine> HighHP = MakeIntegrationEngine(
		FBattleAbilityRules::GetBlazeId(),
		FBattleAbilityRules::GetOvergrowId(),
		EPokemonType::Fire,
		100,
		811);
	FBattleResolution LowEffects;
	FBattleResolution HighEffects;
	TestTrue(TEXT("The low-HP Blaze move executes"),
		ExecuteFirstIntegrationMove(*LowHP, LowEffects));
	TestTrue(TEXT("The above-threshold Blaze move executes"),
		ExecuteFirstIntegrationMove(*HighHP, HighEffects));
	const FBattleSnapshot LowSnapshot = LowHP->GetSnapshot();
	const FBattleSnapshot HighSnapshot = HighHP->GetSnapshot();
	const FBattlePartyEntrySetup* LowTarget = LowSnapshot.FindBattler(
		MakeNumericId<FBattlerId>(21));
	const FBattlePartyEntrySetup* HighTarget = HighSnapshot.FindBattler(
		MakeNumericId<FBattlerId>(21));
	TestNotNull(TEXT("The boosted target remains visible"), LowTarget);
	TestNotNull(TEXT("The control target remains visible"), HighTarget);
	if (LowTarget != nullptr && HighTarget != nullptr)
	{
		TestTrue(TEXT("Blaze's Q12 modifier increases production damage"),
			LowTarget->CurrentHP < HighTarget->CurrentHP);
	}
	const FBattleEngineState& LowState = FBattleC08BEngineFixture::GetState(*LowHP);
	const FBattleEngineState& HighState = FBattleC08BEngineFixture::GetState(*HighHP);
	auto HasBlazeActivation = [](const FBattleEngineState& State)
	{
		return State.OrderedEvents.ContainsByPredicate(
			[](const FBattleEvent& Event)
			{
				return Event.GetType() == EBattleEventType::AbilityActivated
					&& Event.GetSource().DefinitionId
						== FBattleAbilityRules::GetBlazeId().GetDefinitionId();
			});
	};
	TestTrue(TEXT("Eligible Blaze reveals on applied public damage"),
		HasBlazeActivation(LowState));
	TestFalse(TEXT("Above-threshold Blaze remains hidden"),
		HasBlazeActivation(HighState));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC08BEngineResidualAbilitiesTest,
	"PokemonSolarus.Battle.C08B.Integration.SpeedBoostAndMagicGuardResidualRuntime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC08BEngineResidualAbilitiesTest::RunTest(const FString& Parameters)
{
	TUniquePtr<FBattleEngine> SpeedEngine = MakeIntegrationEngine(
		FBattleAbilityRules::GetSpeedBoostId(),
		FBattleAbilityRules::GetBlazeId(),
		EPokemonType::Normal,
		200,
		812);
	FBattleRejection Rejection;
	TestTrue(TEXT("Speed Boost registers at initial entry"),
		SpeedEngine->TryBeginActionDecisionSequence(Rejection));
	TestTrue(TEXT("The production action flow reaches the end-turn boundary"),
		ExecuteIntegrationTurnToEnd(*SpeedEngine));
	TestTrue(TEXT("Speed Boost resolves through production end-turn ordering"),
		SpeedEngine->ResolveEndTurn().WasAccepted());
	const FBattleBattlerState* SpeedOwner = FBattleC08BEngineFixture::GetState(
		*SpeedEngine).FindBattler(MakeNumericId<FBattlerId>(11));
	TestNotNull(TEXT("The Speed Boost owner remains active"), SpeedOwner);
	if (SpeedOwner != nullptr)
	{
		int32 SpeedStage = 0;
		TestTrue(TEXT("The Speed stage is readable"),
			SpeedOwner->Stages.TryGetStage(EBattleStat::Speed, SpeedStage));
		TestEqual(TEXT("Speed Boost applies exactly +1 after a completed active turn"),
			SpeedStage,
			1);
	}

	TUniquePtr<FBattleEngine> SameTurnEngine = MakeIntegrationEngine(
		FBattleAbilityRules::GetSpeedBoostId(),
		FBattleAbilityRules::GetBlazeId(),
		EPokemonType::Normal,
		200,
		813);
	TestTrue(TEXT("The same-turn fixture registers Speed Boost"),
		SameTurnEngine->TryBeginActionDecisionSequence(Rejection));
	FBattleBattlerState* SameTurnOwner = FBattleC08BEngineFixture::GetMutableState(
		*SameTurnEngine).FindMutableBattler(MakeNumericId<FBattlerId>(11));
	TestNotNull(TEXT("The same-turn Speed Boost owner exists"), SameTurnOwner);
	if (SameTurnOwner != nullptr)
	{
		SameTurnOwner->EnteredActiveOnTurnId =
			FBattleC08BEngineFixture::GetState(*SameTurnEngine).TurnId;
	}
	TestTrue(TEXT("The same-turn production flow reaches the end-turn boundary"),
		ExecuteIntegrationTurnToEnd(*SameTurnEngine));
	TestTrue(TEXT("The same-turn end-turn boundary resolves"),
		SameTurnEngine->ResolveEndTurn().WasAccepted());
	if (SameTurnOwner != nullptr)
	{
		int32 SpeedStage = 0;
		TestTrue(TEXT("The same-turn Speed stage is readable"),
			SameTurnOwner->Stages.TryGetStage(EBattleStat::Speed, SpeedStage));
		TestEqual(TEXT("A same-turn entrant does not receive Speed Boost"), SpeedStage, 0);
	}

	TUniquePtr<FBattleEngine> GuardEngine = MakeIntegrationEngine(
		FBattleAbilityRules::GetMagicGuardId(),
		FBattleAbilityRules::GetBlazeId(),
		EPokemonType::Normal,
		200,
		814);
	TestTrue(TEXT("Magic Guard registers at initial entry"),
		GuardEngine->TryBeginActionDecisionSequence(Rejection));
	TestTrue(TEXT("The focused fixture installs Poison through the canonical trigger"),
		FBattleC08BEngineFixture::AddMajorStatus(
			*GuardEngine,
			MakeNumericId<FBattlerId>(11),
			FBattleMajorStatusRules::GetPoisonId()));
	TestTrue(TEXT("The Poison fixture reaches the production end-turn boundary"),
		ExecuteIntegrationTurnToEnd(*GuardEngine));
	const FBattleSnapshot GuardSnapshotBeforeEnd = GuardEngine->GetSnapshot();
	const FBattlePartyEntrySetup* GuardOwnerBeforeEnd = GuardSnapshotBeforeEnd.FindBattler(
		MakeNumericId<FBattlerId>(11));
	TestNotNull(TEXT("The Magic Guard owner exists before residual damage"), GuardOwnerBeforeEnd);
	const int32 HPBeforeResidual = GuardOwnerBeforeEnd != nullptr
		? GuardOwnerBeforeEnd->CurrentHP
		: 0;
	TestTrue(TEXT("The Poison residual boundary resolves"),
		GuardEngine->ResolveEndTurn().WasAccepted());
	const FBattleBattlerState* GuardOwner = FBattleC08BEngineFixture::GetState(
		*GuardEngine).FindBattler(MakeNumericId<FBattlerId>(11));
	TestNotNull(TEXT("The Magic Guard owner remains active"), GuardOwner);
	if (GuardOwner != nullptr)
	{
		TestEqual(TEXT("Magic Guard prevents Poison's indirect HP loss"),
			GuardOwner->CurrentHP,
			HPBeforeResidual);
	}
	TestTrue(TEXT("Preventing Poison publicly activates Magic Guard"),
		FBattleC08BEngineFixture::GetState(*GuardEngine).OrderedEvents.ContainsByPredicate(
			[](const FBattleEvent& Event)
			{
				return Event.GetType() == EBattleEventType::AbilityActivated
					&& Event.GetSource().DefinitionId
						== FBattleAbilityRules::GetMagicGuardId().GetDefinitionId();
			}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC08BEngineMagicGuardFamiliesTest,
	"PokemonSolarus.Battle.C08B.Integration.MagicGuardResidualFamiliesAndSuppression",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC08BEngineMagicGuardFamiliesTest::RunTest(const FString& Parameters)
{
	const FBattlerId PlayerId = MakeNumericId<FBattlerId>(11);
	const FBattlerId OpponentId = MakeNumericId<FBattlerId>(21);
	const TArray<FConditionId> StatusIds{
		FBattleMajorStatusRules::GetBurnId(),
		FBattleMajorStatusRules::GetPoisonId(),
		FBattleMajorStatusRules::GetToxicId()};
	for (int32 Index = 0; Index < StatusIds.Num(); ++Index)
	{
		TUniquePtr<FBattleEngine> Engine = MakeIntegrationEngine(
			FBattleAbilityRules::GetMagicGuardId(),
			FBattleAbilityRules::GetMoldBreakerId(),
			EPokemonType::Normal,
			200,
			820 + Index);
		FBattleRejection Rejection;
		TestTrue(TEXT("A status-family Magic Guard engine registers"),
			Engine->TryBeginActionDecisionSequence(Rejection));
		TestTrue(TEXT("The canonical damaging status is installed"),
			FBattleC08BEngineFixture::AddMajorStatus(
				*Engine,
				PlayerId,
				StatusIds[Index]));
		TestTrue(TEXT("The status-family engine reaches end turn"),
			ExecuteIntegrationTurnToEnd(*Engine));
		const FBattleSnapshot SnapshotBefore = Engine->GetSnapshot();
		const FBattlePartyEntrySetup* Before = SnapshotBefore.FindBattler(PlayerId);
		const int32 HPBefore = Before != nullptr ? Before->CurrentHP : 0;
		TestNotNull(TEXT("The status-family owner exists before residuals"), Before);
		TestTrue(TEXT("The status-family residual pass resolves"),
			Engine->ResolveEndTurn().WasAccepted());
		const FBattleSnapshot SnapshotAfter = Engine->GetSnapshot();
		const FBattlePartyEntrySetup* After = SnapshotAfter.FindBattler(PlayerId);
		TestNotNull(TEXT("The status-family owner survives residuals"), After);
		if (After != nullptr)
		{
			TestEqual(TEXT("Magic Guard blocks the complete major-status damage family"),
				After->CurrentHP,
				HPBefore);
		}
		TestTrue(TEXT("Each prevented status family publicly activates Magic Guard"),
			HasAbilityActivation(
				FBattleC08BEngineFixture::GetState(*Engine),
				FBattleAbilityRules::GetMagicGuardId()));
	}

	TUniquePtr<FBattleEngine> WeatherEngine = MakeIntegrationEngine(
		FBattleAbilityRules::GetMagicGuardId(),
		FBattleAbilityRules::GetMoldBreakerId(),
		EPokemonType::Normal,
		200,
		824);
	TestTrue(TEXT("Sandstorm is seeded for the field-damage family"),
		FBattleC08BEngineFixture::SeedCondition(
			*WeatherEngine,
			FBattleFieldSideConditionRules::GetSandstormId(),
			EBattleSide::Player,
			OpponentId,
			TOptional<int32>(5)));
	TestTrue(TEXT("The weather-family engine reaches end turn"),
		ExecuteIntegrationTurnToEnd(*WeatherEngine));
	const FBattleSnapshot WeatherSnapshotBefore = WeatherEngine->GetSnapshot();
	const FBattlePartyEntrySetup* WeatherBefore = WeatherSnapshotBefore.FindBattler(PlayerId);
	const int32 WeatherHPBefore = WeatherBefore != nullptr ? WeatherBefore->CurrentHP : 0;
	TestTrue(TEXT("The Sandstorm residual pass resolves"),
		WeatherEngine->ResolveEndTurn().WasAccepted());
	const FBattleSnapshot WeatherSnapshotAfter = WeatherEngine->GetSnapshot();
	const FBattlePartyEntrySetup* WeatherAfter = WeatherSnapshotAfter.FindBattler(PlayerId);
	TestTrue(TEXT("Magic Guard blocks field-origin Sandstorm damage"),
		WeatherBefore != nullptr
			&& WeatherAfter != nullptr
			&& WeatherAfter->CurrentHP == WeatherHPBefore);
	TestTrue(TEXT("Prevented weather damage publicly activates Magic Guard"),
		HasAbilityActivation(
			FBattleC08BEngineFixture::GetState(*WeatherEngine),
			FBattleAbilityRules::GetMagicGuardId()));

	TUniquePtr<FBattleEngine> VolatileEngine = MakeIntegrationEngine(
		FBattleAbilityRules::GetMagicGuardId(),
		FBattleAbilityRules::GetMoldBreakerId(),
		EPokemonType::Normal,
		200,
		825);
	FBattleRejection Rejection;
	TestTrue(TEXT("The volatile-family Magic Guard engine registers"),
		VolatileEngine->TryBeginActionDecisionSequence(Rejection));
	TestTrue(TEXT("Leech Seed is installed through its canonical trigger"),
		FBattleC08BEngineFixture::AddVolatile(
			*VolatileEngine,
			PlayerId,
			FBattleVolatileRules::GetLeechSeedId(),
			OpponentId));
	TestTrue(TEXT("Partial trapping is installed through its canonical trigger"),
		FBattleC08BEngineFixture::AddVolatile(
			*VolatileEngine,
			PlayerId,
			FBattleVolatileRules::GetPartialTrapId(),
			OpponentId,
			TOptional<int32>(5)));
	TestTrue(TEXT("The volatile-family engine reaches end turn"),
		ExecuteIntegrationTurnToEnd(*VolatileEngine));
	const FBattleSnapshot VolatileSnapshotBefore = VolatileEngine->GetSnapshot();
	const FBattlePartyEntrySetup* VolatileBefore = VolatileSnapshotBefore.FindBattler(PlayerId);
	const int32 VolatileHPBefore = VolatileBefore != nullptr ? VolatileBefore->CurrentHP : 0;
	TestTrue(TEXT("The volatile-family residual pass resolves"),
		VolatileEngine->ResolveEndTurn().WasAccepted());
	const FBattleSnapshot VolatileSnapshotAfter = VolatileEngine->GetSnapshot();
	const FBattlePartyEntrySetup* VolatileAfter = VolatileSnapshotAfter.FindBattler(PlayerId);
	TestTrue(TEXT("Magic Guard blocks both Leech Seed and partial-trap damage"),
		VolatileBefore != nullptr
			&& VolatileAfter != nullptr
			&& VolatileAfter->CurrentHP == VolatileHPBefore);
	TestTrue(TEXT("Prevented volatile damage publicly activates Magic Guard"),
		HasAbilityActivation(
			FBattleC08BEngineFixture::GetState(*VolatileEngine),
			FBattleAbilityRules::GetMagicGuardId()));

	TUniquePtr<FBattleEngine> SuppressedEngine = MakeIntegrationEngine(
		FBattleAbilityRules::GetMagicGuardId(),
		FBattleAbilityRules::GetMoldBreakerId(),
		EPokemonType::Normal,
		200,
		826);
	TestTrue(TEXT("The suppression engine registers Magic Guard"),
		SuppressedEngine->TryBeginActionDecisionSequence(Rejection));
	FBattleBattlerState* SuppressedOwner = FBattleC08BEngineFixture::GetMutableState(
		*SuppressedEngine).FindMutableBattler(PlayerId);
	TestNotNull(TEXT("The suppressible Magic Guard owner exists"), SuppressedOwner);
	if (SuppressedOwner != nullptr)
	{
		SuppressedOwner->bAbilitySuppressed = true;
	}
	TestTrue(TEXT("Poison is installed while Magic Guard is suppressed"),
		FBattleC08BEngineFixture::AddMajorStatus(
			*SuppressedEngine,
			PlayerId,
			FBattleMajorStatusRules::GetPoisonId()));
	TestTrue(TEXT("The suppression engine reaches end turn"),
		ExecuteIntegrationTurnToEnd(*SuppressedEngine));
	const FBattleSnapshot SuppressedSnapshotBefore = SuppressedEngine->GetSnapshot();
	const FBattlePartyEntrySetup* SuppressedBefore = SuppressedSnapshotBefore.FindBattler(
		PlayerId);
	const int32 SuppressedHPBefore = SuppressedBefore != nullptr
		? SuppressedBefore->CurrentHP
		: 0;
	TestTrue(TEXT("The suppressed residual pass resolves"),
		SuppressedEngine->ResolveEndTurn().WasAccepted());
	const FBattleSnapshot SuppressedSnapshotAfter = SuppressedEngine->GetSnapshot();
	const FBattlePartyEntrySetup* SuppressedAfter = SuppressedSnapshotAfter.FindBattler(
		PlayerId);
	TestTrue(TEXT("Runtime suppression permits indirect damage without deleting ownership"),
		SuppressedBefore != nullptr
			&& SuppressedAfter != nullptr
			&& SuppressedAfter->CurrentHP < SuppressedHPBefore
			&& SuppressedAfter->AbilityId == FBattleAbilityRules::GetMagicGuardId());
	TestFalse(TEXT("A suppressed non-activation does not reveal Magic Guard"),
		HasAbilityActivation(
			FBattleC08BEngineFixture::GetState(*SuppressedEngine),
			FBattleAbilityRules::GetMagicGuardId()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC08BEngineMagicGuardHazardsTest,
	"PokemonSolarus.Battle.C08B.Integration.MagicGuardHazardsDirectMoveAndSubstituteCost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC08BEngineMagicGuardHazardsTest::RunTest(const FString& Parameters)
{
	const FBattlerId PlayerId = MakeNumericId<FBattlerId>(11);
	const FBattlerId ReserveId = MakeNumericId<FBattlerId>(12);
	const FBattlerId OpponentId = MakeNumericId<FBattlerId>(21);
	TUniquePtr<FBattleEngine> HazardEngine;
	FBattleRejection Rejection;
	TestTrue(TEXT("The Magic Guard hazard engine is created"),
		FBattleEngine::TryCreate(
			MakeIntegrationSetup(
				FBattleAbilityRules::GetBlazeId(),
				FBattleAbilityRules::GetMoldBreakerId(),
				200,
				FBattleAbilityRules::GetMagicGuardId()),
			MakeIntegrationCatalog(),
			MakeUnique<FSeededBattleRandom>(827),
			HazardEngine,
			Rejection));
	if (!HazardEngine.IsValid())
	{
		return false;
	}
	for (const FConditionId& HazardId : {
		FBattleFieldSideConditionRules::GetSpikesId(),
		FBattleFieldSideConditionRules::GetToxicSpikesId(),
		FBattleFieldSideConditionRules::GetStealthRockId(),
		FBattleFieldSideConditionRules::GetStickyWebId()})
	{
		TestTrue(TEXT("An approved hazard is seeded for Magic Guard"),
			FBattleC08BEngineFixture::SeedCondition(
				*HazardEngine,
				HazardId,
				EBattleSide::Player,
				OpponentId,
				TOptional<int32>(),
				1));
	}
	TestTrue(TEXT("The initial active Ability registers before the hazard switch"),
		HazardEngine->TryBeginActionDecisionSequence(Rejection));
	TestTrue(TEXT("The Magic Guard reserve switch is prepared"),
		FBattleC08BEngineFixture::PrepareLockedSwitch(
			*HazardEngine,
			PlayerId,
			MakePartySlotId(1)));
	TestTrue(TEXT("The Magic Guard reserve switch starts"),
		HazardEngine->BeginNextLockedAction().WasAccepted());
	const FBattleResolution HazardSwitch = HazardEngine->ExecuteCurrentSwitch();
	TestTrue(TEXT("The Magic Guard reserve switch resolves"), HazardSwitch.WasAccepted());
	const FBattleBattlerState* HazardOwner = FBattleC08BEngineFixture::GetState(
		*HazardEngine).FindBattler(ReserveId);
	TestNotNull(TEXT("The Magic Guard reserve is authoritative after entry"), HazardOwner);
	if (HazardOwner != nullptr)
	{
		int32 SpeedStage = 0;
		TestTrue(TEXT("The reserve Speed stage is readable"),
			HazardOwner->Stages.TryGetStage(EBattleStat::Speed, SpeedStage));
		TestEqual(TEXT("Magic Guard prevents Spikes and Stealth Rock damage"),
			HazardOwner->CurrentHP,
			200);
		TestEqual(TEXT("Magic Guard does not block Toxic Spikes status"),
			HazardOwner->MajorStatusId,
			FBattleMajorStatusRules::GetPoisonId());
		TestEqual(TEXT("Magic Guard does not block Sticky Web's non-damage effect"),
			SpeedStage,
			-1);
	}
	TestTrue(TEXT("Prevented hazard damage publicly activates Magic Guard"),
		HazardSwitch.GetEvents().ContainsByPredicate(
			[](const FBattleEvent& Event)
			{
				return Event.GetType() == EBattleEventType::AbilityActivated
					&& Event.GetSource().DefinitionId
						== FBattleAbilityRules::GetMagicGuardId().GetDefinitionId();
			}));

	TUniquePtr<FBattleEngine> DirectEngine = MakeIntegrationEngine(
		FBattleAbilityRules::GetBlazeId(),
		FBattleAbilityRules::GetMagicGuardId(),
		EPokemonType::Normal,
		200,
		828);
	FBattleResolution DirectEffects;
	TestTrue(TEXT("A direct move against Magic Guard executes"),
		ExecuteFirstIntegrationMove(*DirectEngine, DirectEffects));
	const FBattleSnapshot DirectSnapshot = DirectEngine->GetSnapshot();
	const FBattlePartyEntrySetup* DirectTarget = DirectSnapshot.FindBattler(
		OpponentId);
	TestTrue(TEXT("Magic Guard does not block direct move damage"),
		DirectTarget != nullptr && DirectTarget->CurrentHP < 200);
	TestFalse(TEXT("Direct damage alone does not reveal Magic Guard"),
		HasAbilityActivation(
			FBattleC08BEngineFixture::GetState(*DirectEngine),
			FBattleAbilityRules::GetMagicGuardId()));

	TUniquePtr<FBattleEngine> SubstituteEngine = MakeIntegrationEngine(
		FBattleAbilityRules::GetMagicGuardId(),
		FBattleAbilityRules::GetMoldBreakerId(),
		EPokemonType::Normal,
		200,
		829);
	FBattleResolution SubstituteEffects;
	TestTrue(TEXT("A Magic Guard user can execute Substitute"),
		ExecuteFirstIntegrationMove(*SubstituteEngine, SubstituteEffects, true));
	const FBattleBattlerState* SubstituteOwner = FBattleC08BEngineFixture::GetState(
		*SubstituteEngine).FindBattler(PlayerId);
	TestTrue(TEXT("Magic Guard does not block Substitute's HP cost"),
		SubstituteOwner != nullptr
			&& SubstituteOwner->CurrentHP == 150
			&& SubstituteOwner->Volatiles.ContainsByPredicate(
				[](const FBattleConditionState& Condition)
				{
					return Condition.ConditionId == FBattleVolatileRules::GetSubstituteId();
				}));
	TestFalse(TEXT("Substitute cost alone does not reveal Magic Guard"),
		HasAbilityActivation(
			FBattleC08BEngineFixture::GetState(*SubstituteEngine),
			FBattleAbilityRules::GetMagicGuardId()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC08BEngineMagicGuardConfusionTest,
	"PokemonSolarus.Battle.C08B.Integration.MagicGuardConfusionSelfHit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC08BEngineMagicGuardConfusionTest::RunTest(const FString& Parameters)
{
	const FBattlerId PlayerId = MakeNumericId<FBattlerId>(11);
	const FBattlerId OpponentId = MakeNumericId<FBattlerId>(21);
	TUniquePtr<FBattleEngine> Engine;
	FBattleRejection Rejection;
	TArray<FExpectedIntegrationDraw> Draws;
	Draws.Add({
		0,
		99,
		32,
		FBattleVolatileRules::GetConfusionActionGatePurpose()});
	TestTrue(TEXT("The scripted Confusion engine is created"),
		FBattleEngine::TryCreate(
			MakeIntegrationSetup(
				FBattleAbilityRules::GetMagicGuardId(),
				FBattleAbilityRules::GetMoldBreakerId()),
			MakeIntegrationCatalog(),
			MakeUnique<FScriptedIntegrationRandom>(MoveTemp(Draws)),
			Engine,
			Rejection));
	if (!Engine.IsValid())
	{
		return false;
	}
	TestTrue(TEXT("Initial Ability registration precedes the Confusion fixture"),
		Engine->TryBeginActionDecisionSequence(Rejection));
	TestTrue(TEXT("Confusion is installed with a live before-action trigger"),
		FBattleC08BEngineFixture::AddVolatile(
			*Engine,
			PlayerId,
			FBattleVolatileRules::GetConfusionId(),
			OpponentId,
			TOptional<int32>(2)));
	TestTrue(TEXT("The scripted Confusion turn locks"), LockIntegrationFights(*Engine));
	const TArray<FBattleLockedAction> LockedActions = Engine->GetLockedActions();
	TestTrue(TEXT("The Confused battler owns the first locked action"),
		!LockedActions.IsEmpty()
			&& LockedActions[0].Decision.GetActingBattlerId() == PlayerId);
	const FBattleResolution Started = Engine->BeginNextLockedAction();
	TestTrue(TEXT("The Confused action starts"), Started.WasAccepted());
	const FBattleResolution Committed = Engine->CommitCurrentMoveAfterPreMoveGates();
	TestTrue(TEXT("The Confusion self-hit gate resolves"), Committed.WasAccepted());
	const FBattleSnapshot Snapshot = Engine->GetSnapshot();
	const FBattlePartyEntrySetup* Owner = Snapshot.FindBattler(PlayerId);
	TestTrue(TEXT("Magic Guard prevents Confusion self-hit damage"),
		Owner != nullptr && Owner->CurrentHP == 200);
	TestTrue(TEXT("Prevented Confusion self-hit publicly activates Magic Guard"),
		HasAbilityActivation(
			FBattleC08BEngineFixture::GetState(*Engine),
			FBattleAbilityRules::GetMagicGuardId()));
	const TArray<FBattleRandomDraw> Trace = Engine->ExportRandomTrace();
	TestTrue(TEXT("The focused Confusion proof consumes only the scripted gate draw"),
		Trace.Num() == 1
			&& Trace[0].Result == 32
			&& Trace[0].RulePurpose
				== FBattleVolatileRules::GetConfusionActionGatePurpose());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC08BEngineFaintCleanupTest,
	"PokemonSolarus.Battle.C08B.Integration.FaintCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC08BEngineFaintCleanupTest::RunTest(const FString& Parameters)
{
	const FBattlerId PlayerId = MakeNumericId<FBattlerId>(11);
	const FBattlerId OpponentId = MakeNumericId<FBattlerId>(21);
	TUniquePtr<FBattleEngine> Engine = MakeIntegrationEngine(
		FBattleAbilityRules::GetBlazeId(),
		FBattleAbilityRules::GetSpeedBoostId(),
		EPokemonType::Normal,
		200,
		830);
	FBattleBattlerState* Opponent = FBattleC08BEngineFixture::GetMutableState(
		*Engine).FindMutableBattler(OpponentId);
	TestNotNull(TEXT("The faint-cleanup target exists"), Opponent);
	if (Opponent != nullptr)
	{
		Opponent->CurrentHP = 1;
	}
	FBattleResolution Effects;
	TestTrue(TEXT("The production move reaches a faint transition"),
		ExecuteFirstIntegrationMove(*Engine, Effects));
	TestTrue(TEXT("The production result reports the faint"),
		Effects.GetEvents().ContainsByPredicate(
			[OpponentId](const FBattleEvent& Event)
			{
				return Event.GetType() == EBattleEventType::Fainted
					&& Event.GetTargets().ContainsByPredicate(
						[OpponentId](const FBattleEventTarget& Target)
						{
							return Target.BattlerId == OpponentId;
						});
			}));
	TestFalse(TEXT("Faint cleanup removes the defeated owner's Ability hooks"),
		FBattleC08BEngineFixture::HasAbilityRegistration(*Engine, OpponentId));
	TestTrue(TEXT("Faint cleanup leaves the living owner's Ability hooks intact"),
		FBattleC08BEngineFixture::HasAbilityRegistration(*Engine, PlayerId));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E6ForcedEntryAbilityTest,
	"PokemonSolarus.Battle.ADR0002.3E6.Effects.Ability.ForcedEntryAtomicOrderAndExactRng",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E6ForcedEntryAbilityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TArray<FExpectedIntegrationDraw> Draws = {{
		0,
		0,
		0,
		FBattleSwitchResolver::GetForcedSelectionRulePurpose()}};
	TUniquePtr<FScriptedIntegrationRandom> Random =
		MakeUnique<FScriptedIntegrationRandom>(MoveTemp(Draws));
	FScriptedIntegrationRandom* RandomView = Random.Get();
	TUniquePtr<IBattleRandom> RandomOwner = MoveTemp(Random);
	TUniquePtr<FBattleEngine> Engine;
	FBattleRejection Rejection;
	if (!TestTrue(TEXT("The forced-entry Ability engine is created"),
			FBattleEngine::TryCreate(
				MakeIntegrationSetup(
					FBattleAbilityRules::GetBlazeId(),
					FBattleAbilityRules::GetBlazeId(),
					200,
					FBattleAbilityRules::GetLevitateId(),
					FBattleAbilityRules::GetIntimidateId()),
				MakeIntegrationCatalog(),
				MoveTemp(RandomOwner),
				Engine,
				Rejection))
		|| !TestTrue(TEXT("The forced-switch action locks"),
			LockIntegrationFightsWithPlayerMove(
				*Engine,
				MakeDefinitionId<FMoveId>(TEXT("Move.C08B.ForcedSwitch"))))
		|| !TestTrue(TEXT("The forced-switch action starts"),
			Engine->BeginNextLockedAction().WasAccepted())
		|| !TestTrue(TEXT("The forced-switch PP commits"),
			Engine->CommitCurrentMoveAfterPreMoveGates().WasAccepted())
		|| !TestTrue(TEXT("The forced-switch target commits"),
			Engine->ResolveCurrentMoveTargets().WasAccepted()))
	{
		return false;
	}

	const FBattleResolution Resolution = Engine->ExecuteCurrentMoveEffects();
	const FBattleEngineState& State = FBattleC08BEngineFixture::GetState(*Engine);
	const FBattleBattlerState* Player = State.FindBattler(
		MakeNumericId<FBattlerId>(11));
	int32 AttackStage = INDEX_NONE;
	const bool bHasAttackStage = Player != nullptr
		&& Player->Stages.TryGetStage(EBattleStat::Attack, AttackStage);
	const int32 SwitchInIndex = Resolution.GetEvents().IndexOfByPredicate(
		[](const FBattleEvent& Event)
		{
			return Event.GetType() == EBattleEventType::EnteredActiveSlot;
		});
	const int32 AbilityIndex = Resolution.GetEvents().IndexOfByPredicate(
		[](const FBattleEvent& Event)
		{
			return Event.GetType() == EBattleEventType::AbilityActivated
				&& Event.GetSource().DefinitionId
					== FBattleAbilityRules::GetIntimidateId().GetDefinitionId();
		});
	const int32 CompletionIndex = Resolution.GetEvents().IndexOfByPredicate(
		[](const FBattleEvent& Event)
		{
			return Event.GetType() == EBattleEventType::ActionCompleted;
		});
	bool bValid = TestTrue(TEXT("The forced-entry Ability checkpoint is accepted"),
		Resolution.WasAccepted());
	bValid &= TestTrue(TEXT("The reserve becomes the exact active opponent"),
		State.FindActivePosition(
			MakeActiveSlotId(EBattleSide::Opponent, EBattlePosition::Left))
				->BattlerId == MakeNumericId<FBattlerId>(22));
	bValid &= TestTrue(TEXT("Intimidate state and event publish together"),
		bHasAttackStage && AttackStage == -1 && AbilityIndex != INDEX_NONE);
	bValid &= TestTrue(TEXT("Switch-in precedes Ability and action completion"),
		SwitchInIndex != INDEX_NONE
			&& AbilityIndex > SwitchInIndex
			&& CompletionIndex > AbilityIndex);
	bValid &= TestTrue(TEXT("The forced-selection transaction is exact"),
		RandomView != nullptr && RandomView->IsExact());
	const TArray<FBattleRandomDraw> Trace = Engine->ExportRandomTrace();
	bValid &= TestTrue(TEXT("The exact forced-selection draw commits once"),
		Trace.Num() == 1
			&& Trace[0].InclusiveMinimum == 0
			&& Trace[0].InclusiveMaximum == 0
			&& Trace[0].RulePurpose
				== FBattleSwitchResolver::GetForcedSelectionRulePurpose());
	return bValid;
}

}

#endif
