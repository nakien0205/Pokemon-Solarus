#if WITH_DEV_AUTOMATION_TESTS

#include "Battle/BattleAbility.h"
#include "Battle/BattleBagItem.h"
#include "Battle/BattleEngine.h"
#include "Battle/BattleMajorStatus.h"
#include "Battle/BattleState.h"
#include "Battle/BattleVolatile.h"
#include "BattleTestFactories.h"
#include "Math/NumericLimits.h"
#include "Misc/AutomationTest.h"

class FBattleC08CBagEngineFixture
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

	static const FBattleBattlerState* GetBattler(
		const FBattleEngine& Engine,
		const FBattlerId BattlerId)
	{
		return Engine.State.IsValid()
			? Engine.State->FindBattler(BattlerId)
			: nullptr;
	}

	static const FBattleTrainerState* GetTrainer(
		const FBattleEngine& Engine,
		const FTrainerId TrainerId)
	{
		return Engine.State.IsValid()
			? Engine.State->FindTrainer(TrainerId)
			: nullptr;
	}

	static int32 GetResolutionCount(const FBattleEngine& Engine)
	{
		return Engine.State.IsValid() ? Engine.State->Resolutions.Num() : INDEX_NONE;
	}

	static uint64 GetNextResolutionValue(const FBattleEngine& Engine)
	{
		return Engine.State.IsValid() ? Engine.State->NextResolutionId : 0;
	}

	static uint64 GetNextEventOrdinal(const FBattleEngine& Engine)
	{
		return Engine.State.IsValid() ? Engine.State->NextEventOrdinal : 0;
	}

	static int32 GetOrderedEventCount(const FBattleEngine& Engine)
	{
		return Engine.State.IsValid() ? Engine.State->OrderedEvents.Num() : INDEX_NONE;
	}

	static uint64 GetStateVersion(const FBattleEngine& Engine)
	{
		return Engine.State.IsValid() ? Engine.State->StateVersion : 0;
	}

	static int32 GetCurrentLockedActionIndex(const FBattleEngine& Engine)
	{
		return Engine.State.IsValid()
			? Engine.State->CurrentLockedActionIndex
			: INDEX_NONE;
	}

	static bool IsCurrentActionStartedAndUnfinished(const FBattleEngine& Engine)
	{
		if (!Engine.State.IsValid()
			|| !Engine.State->LockedActions.IsValidIndex(
				Engine.State->CurrentLockedActionIndex))
		{
			return false;
		}
		const FBattleLockedActionState& Action =
			Engine.State->LockedActions[Engine.State->CurrentLockedActionIndex];
		return Action.bStarted && !Action.bFinished;
	}

	static uint64 GetNextTriggerReentrancyToken(const FBattleEngine& Engine)
	{
		return Engine.State.IsValid()
			? Engine.State->NextTriggerReentrancyToken
			: 0;
	}

	static void SetNextTriggerReentrancyToken(
		FBattleEngine& Engine,
		const uint64 Value)
	{
		check(Engine.State.IsValid());
		Engine.State->NextTriggerReentrancyToken = Value;
	}

	static int32 GetActiveTriggerRegistrationCount(const FBattleEngine& Engine)
	{
		return Engine.State.IsValid()
			? Engine.State->TriggerFramework.GetActiveRegistrations().Num()
			: INDEX_NONE;
	}

	static int32 GetPendingTriggerDispatchCount(const FBattleEngine& Engine)
	{
		return Engine.State.IsValid()
			? Engine.State->TriggerFramework.GetPendingDispatchCount()
			: INDEX_NONE;
	}

	static int32 GetPendingTriggerEffectRequestCount(const FBattleEngine& Engine)
	{
		return Engine.State.IsValid()
			? Engine.State->TriggerFramework.GetPendingEffectRequestCount()
			: INDEX_NONE;
	}

	static bool SetCurrentHP(
		FBattleEngine& Engine,
		const FBattlerId BattlerId,
		const int32 CurrentHP)
	{
		FBattleBattlerState* Battler = Engine.State.IsValid()
			? Engine.State->FindMutableBattler(BattlerId)
			: nullptr;
		if (Battler == nullptr
			|| CurrentHP <= 0
			|| CurrentHP > Battler->PermanentStats.MaxHP)
		{
			return false;
		}
		Battler->CurrentHP = CurrentHP;
		Battler->bFainted = false;
		Battler->bFaintTransitionPending = false;
		Battler->bRemoved = false;
		return true;
	}

	static bool ApplyAttackStageChange(
		FBattleEngine& Engine,
		const FBattlerId BattlerId,
		const int32 Delta)
	{
		FBattleBattlerState* Battler = Engine.State.IsValid()
			? Engine.State->FindMutableBattler(BattlerId)
			: nullptr;
		if (Battler == nullptr)
		{
			return false;
		}
		const FBattleStatStageChangeResult Result = Battler->Stages.ApplyChange(
			EBattleStat::Attack,
			Delta);
		return Result.Outcome == EBattleStatStageChangeOutcome::Applied;
	}

	static int32 GetAttackStage(
		const FBattleEngine& Engine,
		const FBattlerId BattlerId)
	{
		const FBattleBattlerState* Battler = GetBattler(Engine, BattlerId);
		int32 Stage = INDEX_NONE;
		return Battler != nullptr
			&& Battler->Stages.TryGetStage(EBattleStat::Attack, Stage)
			? Stage
			: INDEX_NONE;
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
		const TOptional<int32> SleepTurns =
			StatusId == FBattleMajorStatusRules::GetSleepId()
				? TOptional<int32>(3)
				: TOptional<int32>();
		if (Battler == nullptr
			|| !FBattleMajorStatusRules::IsCanonical(StatusId)
			|| !FBattleTriggerSubject::TryCreateBattler(BattlerId, Owner)
			|| !FBattleMajorStatusRules::TryRegisterTriggers(
				State.TriggerFramework,
				StatusId,
				Owner,
				SleepTurns,
				Error))
		{
			return false;
		}
		Battler->MajorStatusId = StatusId;
		TArray<FBattleTriggerEffectRequest> Requests;
		TArray<FBattleTriggerLifecycleFact> Lifecycle;
		State.TriggerFramework.DrainEffectRequests(Requests);
		State.TriggerFramework.DrainLifecycleFacts(Lifecycle);
		return true;
	}

	static bool AddVolatile(
		FBattleEngine& Engine,
		const FBattlerId TargetBattlerId,
		const FConditionId& VolatileId,
		const FBattlerId SourceBattlerId,
		const int32 Layers = 1)
	{
		FBattleEngineState& State = GetMutableState(Engine);
		FBattleBattlerState* Battler = State.FindMutableBattler(TargetBattlerId);
		FBattleTriggerSubject Owner;
		FBattleTriggerSubject Source;
		if (Battler == nullptr
			|| Layers <= 0
			|| !FBattleVolatileRules::IsCanonical(VolatileId)
			|| !FBattleTriggerSubject::TryCreateBattler(TargetBattlerId, Owner)
			|| !FBattleTriggerSubject::TryCreateBattler(SourceBattlerId, Source))
		{
			return false;
		}

		FBattleVolatileTriggerRegistrationFacts Facts;
		Facts.VolatileId = VolatileId;
		Facts.PayloadId = VolatileId.GetDefinitionId();
		Facts.Owner = Owner;
		Facts.Source = Source;
		Facts.RemainingTurns = VolatileId == FBattleVolatileRules::GetConfusionId()
			? TOptional<int32>(3)
			: TOptional<int32>();
		Facts.Layers = Layers;
		EBattleTriggerError Error = EBattleTriggerError::None;
		if (!FBattleVolatileRules::TryRegisterTriggers(
			State.TriggerFramework,
			Facts,
			Error))
		{
			return false;
		}

		FBattleConditionState Condition;
		Condition.ConditionId = VolatileId;
		Condition.RemainingTurns = Facts.RemainingTurns;
		Condition.LayerCount = Layers;
		Condition.CreationOrdinal = State.NextConditionCreationOrdinal++;
		Condition.SourceBattlerId = SourceBattlerId;
		Battler->Volatiles.Add(MoveTemp(Condition));
		TArray<FBattleTriggerEffectRequest> Requests;
		TArray<FBattleTriggerLifecycleFact> Lifecycle;
		State.TriggerFramework.DrainEffectRequests(Requests);
		State.TriggerFramework.DrainLifecycleFacts(Lifecycle);
		return true;
	}

	static bool SetConditionRegistrationLayers(
		FBattleEngine& Engine,
		const FBattlerId BattlerId,
		const FConditionId& ConditionId,
		const int32 Layers)
	{
		FBattleEngineState& State = GetMutableState(Engine);
		FBattleTriggerSubject Owner;
		FBattleTriggerSourceDefinition Source;
		if (!FBattleTriggerSubject::TryCreateBattler(BattlerId, Owner)
			|| !FBattleTriggerSourceDefinition::TryCreateCondition(ConditionId, Source))
		{
			return false;
		}
		TArray<FBattleTriggerRegistrationState> Matches;
		for (const FBattleTriggerRegistrationState& Registration :
			State.TriggerFramework.GetActiveRegistrations())
		{
			if (Registration.Spec.Owner == Owner
				&& Registration.Spec.SourceDefinition == Source)
			{
				Matches.Add(Registration);
			}
		}
		if (Matches.IsEmpty())
		{
			return false;
		}
		FBattleTriggerOperationContext Context;
		if (!FBattleTriggerReentrancyToken::TryCreate(
				State.NextTriggerReentrancyToken++,
				Context.ReentrancyToken))
		{
			return false;
		}
		EBattleTriggerError Error = EBattleTriggerError::None;
		for (const FBattleTriggerRegistrationState& Match : Matches)
		{
			if (!State.TriggerFramework.TryUpdateLayers(
				Match.RegistrationId,
				Layers,
				Context,
				Error))
			{
				return false;
			}
		}
		TArray<FBattleTriggerLifecycleFact> Lifecycle;
		State.TriggerFramework.DrainLifecycleFacts(Lifecycle);
		return true;
	}

	static bool HasVolatile(
		const FBattleEngine& Engine,
		const FBattlerId BattlerId,
		const FConditionId& VolatileId)
	{
		const FBattleBattlerState* Battler = GetBattler(Engine, BattlerId);
		return Battler != nullptr && Battler->Volatiles.ContainsByPredicate(
			[&VolatileId](const FBattleConditionState& Condition)
			{
				return Condition.ConditionId == VolatileId;
			});
	}

	static int32 CountConditionRegistrations(
		const FBattleEngine& Engine,
		const FBattlerId BattlerId,
		const FConditionId& ConditionId)
	{
		if (!Engine.State.IsValid())
		{
			return INDEX_NONE;
		}
		FBattleTriggerSubject Owner;
		FBattleTriggerSourceDefinition Source;
		if (!FBattleTriggerSubject::TryCreateBattler(BattlerId, Owner)
			|| !FBattleTriggerSourceDefinition::TryCreateCondition(ConditionId, Source))
		{
			return INDEX_NONE;
		}
		int32 Count = 0;
		for (const FBattleTriggerRegistrationState& Registration :
			Engine.State->TriggerFramework.GetActiveRegistrations())
		{
			if (Registration.Spec.Owner == Owner
				&& Registration.Spec.SourceDefinition == Source)
			{
				++Count;
			}
		}
		return Count;
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
		FBattleLockedActionState Action;
		if (!FActionId::TryCreate(80890, Action.ActionId))
		{
			return false;
		}
		Action.QueueOrdinal = 1;
		Action.Decision = MoveTemp(Decision);
		Action.OrderKey.CommandBand = EBattleActionCommandBand::VoluntarySwitch;
		Action.OrderKey.EffectiveSpeed = Outgoing->PermanentStats.Speed;
		Action.OrderKey.ActingSlotId = Active->ActiveSlotId;
		State.LockedActions = {MoveTemp(Action)};
		State.CurrentLockedActionIndex = 0;
		State.PendingDecision.Reset();
		State.PendingDecisionRequests.Reset();
		State.DecisionOwnerSequence.Reset();
		State.AcceptedSelections.Reset();
		State.Phase = EBattlePhase::Locked;
		Trainer->ActionAllowance.MaximumActions = 1;
		Trainer->ActionAllowance.RemainingActions = 1;
		EBattleStateValidationError Validation = EBattleStateValidationError::None;
		return State.ValidateInvariants(Validation);
	}
};

namespace BattleBagItemTestAccess
{
	bool IsBagActionAvailable(
		const FBattleEngine& Engine,
		const FTrainerId TrainerId)
	{
		const FBattleTrainerState* Trainer =
			FBattleC08CBagEngineFixture::GetTrainer(Engine, TrainerId);
		return Trainer != nullptr
			&& Trainer->ActionAllowance.bBagActionAvailable;
	}
}

namespace BattleBagItemTests
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
	constexpr uint64 OpponentBattlerValue = 21;
	constexpr uint64 OpponentReserveValue = 22;
	constexpr uint64 PartnerBattlerValue = 31;
	constexpr uint64 PartnerReserveValue = 32;
	const TCHAR* SpeciesName = TEXT("Species.C08C.Bag");
	const TCHAR* MoveName = TEXT("Move.C08C.Bag.Normal");

	struct FBagScenario
	{
		EBattleEncounterKind EncounterKind = EBattleEncounterKind::Trainer;
		EBattleFormat Format = EBattleFormat::Single;
		bool bCaptureAllowed = false;
		TArray<FBattleBagItemCount> PlayerBag;
		TArray<FBattleBagItemCount> OpponentBag;
		TArray<FBattleBagItemCount> PartnerBag;
		int32 PlayerHP = 200;
		int32 PlayerReserveHP = 200;
		int32 OpponentHP = 200;
		int32 OpponentReserveHP = 200;
		int32 PartnerHP = 200;
		int32 PartnerReserveHP = 200;
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

	FBattleDefinitionCatalog MakeCatalog()
	{
		FBattleDefinitionCatalogInput Input;
		Input.TypeChartEntries = MakeNeutralTypeChart();

		FBattleMoveDefinition Move;
		Move.Id = MakeDefinitionId<FMoveId>(MoveName);
		Move.Type = EPokemonType::Normal;
		Move.Category = EBattleMoveCategory::Physical;
		Move.Power = 40;
		Move.bAlwaysHits = true;
		Move.BasePP = 20;
		Move.TargetClass = EBattleTargetClass::SelectedOpponent;
		FBattleMoveEffectDescriptor Damage;
		Damage.Kind = EBattleMoveEffectKind::Damage;
		Damage.Target = EBattleEffectTarget::ResolvedTarget;
		Move.Effects.Add(Damage);
		Input.Moves.Add(Move);

		Input.Abilities.Add({FBattleAbilityRules::GetBlazeId()});
		for (const FItemId& ItemId : FBattleBagItemRules::GetCanonicalIds())
		{
			Input.Items.Add({
				ItemId,
				FBattleBagItemRules::GetExpectedDefinitionKind(
					FBattleBagItemRules::GetKind(ItemId))});
		}
		for (const FConditionId& StatusId : FBattleMajorStatusRules::GetCanonicalIds())
		{
			Input.Conditions.Add({StatusId, EBattleConditionKind::MajorStatus});
		}
		for (const FConditionId& VolatileId : FBattleVolatileRules::GetCanonicalIds())
		{
			Input.Conditions.Add({VolatileId, EBattleConditionKind::Volatile});
		}

		FBattleSpeciesFormDefinition Species;
		Species.Id = MakeDefinitionId<FSpeciesFormId>(SpeciesName);
		Species.PrimaryType = EPokemonType::Normal;
		Species.BaseStats = {80, 80, 80, 80, 80, 80};
		Species.CatchRate = 45;
		Species.AbilityChoices.Add(FBattleAbilityRules::GetBlazeId());
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
		const EBattleDecisionController Controller,
		const TArray<FBattleBagItemCount>& Bag)
	{
		FBattleTrainerSetup Trainer;
		Trainer.TrainerId = MakeNumericId<FTrainerId>(TrainerValue);
		Trainer.Side = Side;
		Trainer.Role = Role;
		Trainer.Controller = Controller;
		const TCHAR* SelectorName = Role == EBattleTrainerRole::Player
			? TEXT("Selector.C08C.Bag.Player")
			: (Role == EBattleTrainerRole::Partner
				? TEXT("Selector.C08C.Bag.Partner")
				: TEXT("Selector.C08C.Bag.Opponent"));
		Trainer.SelectorProfileId = MakeDefinitionId<FDefinitionId>(SelectorName);
		Trainer.Bag = Bag;
		return Trainer;
	}

	FBattlePartyEntrySetup MakePartyEntry(
		const uint64 TrainerValue,
		const uint64 BattlerValue,
		const uint8 PartyIndex,
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
		Entry.AbilityId = FBattleAbilityRules::GetBlazeId();
		Entry.Moves.Add({
			0,
			MakeDefinitionId<FMoveId>(MoveName),
			20,
			20});
		return Entry;
	}

	FBattleSetup MakeSetup(const FBagScenario& Scenario)
	{
		FBattleSetupInput Input;
		Input.BattleId = MakeNumericId<FBattleId>(8083);
		Input.SettingsReference = {
			MakeDefinitionId<FDefinitionId>(TEXT("Settings.C08C.Bag")),
			1};
		Input.CatalogReference = {
			MakeDefinitionId<FDefinitionId>(TEXT("Catalog.C08C.Bag")),
			1};
		Input.EncounterKind = Scenario.EncounterKind;
		Input.Format = Scenario.Format;
		Input.CaptureCapacity = {4, 100};
		Input.Policies.bBagAllowed = true;
		Input.Policies.bCaptureAllowed = Scenario.bCaptureAllowed;
		if (Scenario.bCaptureAllowed)
		{
			Input.CaptureProgression.bHasSnapshot = true;
		}
		Input.Policies.bRunAllowed = false;
		Input.Policies.WildFleeMode = EBattleWildFleeMode::Disabled;
		Input.Trainers.Add(MakeTrainer(
			PlayerTrainerValue,
			EBattleSide::Player,
			EBattleTrainerRole::Player,
			EBattleDecisionController::Human,
			Scenario.PlayerBag));
		Input.Trainers.Add(MakeTrainer(
			OpponentTrainerValue,
			EBattleSide::Opponent,
			EBattleTrainerRole::Opponent,
			EBattleDecisionController::EnemyAI,
			Scenario.OpponentBag));
		if (Scenario.Format == EBattleFormat::PartnerDouble)
		{
			Input.Trainers.Add(MakeTrainer(
				PartnerTrainerValue,
				EBattleSide::Player,
				EBattleTrainerRole::Partner,
				EBattleDecisionController::PartnerAI,
				Scenario.PartnerBag));
		}
		Input.PartyEntries.Add(MakePartyEntry(
			PlayerTrainerValue,
			PlayerBattlerValue,
			0,
			Scenario.PlayerHP,
			120));
		Input.PartyEntries.Add(MakePartyEntry(
			PlayerTrainerValue,
			PlayerReserveValue,
			1,
			Scenario.PlayerReserveHP,
			100));
		Input.PartyEntries.Add(MakePartyEntry(
			OpponentTrainerValue,
			OpponentBattlerValue,
			0,
			Scenario.OpponentHP,
			80));
		if (Scenario.EncounterKind != EBattleEncounterKind::Wild
			|| Scenario.Format != EBattleFormat::Single)
		{
			Input.PartyEntries.Add(MakePartyEntry(
				OpponentTrainerValue,
				OpponentReserveValue,
				1,
				Scenario.OpponentReserveHP,
				90));
		}
		if (Scenario.Format == EBattleFormat::PartnerDouble)
		{
			Input.PartyEntries.Add(MakePartyEntry(
				PartnerTrainerValue,
				PartnerBattlerValue,
				0,
				Scenario.PartnerHP,
				110));
			Input.PartyEntries.Add(MakePartyEntry(
				PartnerTrainerValue,
				PartnerReserveValue,
				1,
				Scenario.PartnerReserveHP,
				95));
		}
		Input.StartingActive.Add({
			MakeActiveSlotId(EBattleSide::Player, EBattlePosition::Left),
			MakeNumericId<FTrainerId>(PlayerTrainerValue),
			MakeNumericId<FBattlerId>(PlayerBattlerValue)});
		Input.StartingActive.Add({
			MakeActiveSlotId(EBattleSide::Opponent, EBattlePosition::Left),
			MakeNumericId<FTrainerId>(OpponentTrainerValue),
			MakeNumericId<FBattlerId>(OpponentBattlerValue)});
		if (Scenario.Format == EBattleFormat::Double)
		{
			Input.StartingActive.Add({
				MakeActiveSlotId(EBattleSide::Player, EBattlePosition::Right),
				MakeNumericId<FTrainerId>(PlayerTrainerValue),
				MakeNumericId<FBattlerId>(PlayerReserveValue)});
			Input.StartingActive.Add({
				MakeActiveSlotId(EBattleSide::Opponent, EBattlePosition::Right),
				MakeNumericId<FTrainerId>(OpponentTrainerValue),
				MakeNumericId<FBattlerId>(OpponentReserveValue)});
		}
		else if (Scenario.Format == EBattleFormat::PartnerDouble)
		{
			Input.StartingActive.Add({
				MakeActiveSlotId(EBattleSide::Player, EBattlePosition::Right),
				MakeNumericId<FTrainerId>(PartnerTrainerValue),
				MakeNumericId<FBattlerId>(PartnerBattlerValue)});
			Input.StartingActive.Add({
				MakeActiveSlotId(EBattleSide::Opponent, EBattlePosition::Right),
				MakeNumericId<FTrainerId>(OpponentTrainerValue),
				MakeNumericId<FBattlerId>(OpponentReserveValue)});
		}
		for (const uint64 BattlerValue : {PlayerBattlerValue, PlayerReserveValue})
		{
			Input.ObedienceInputs.Add({
				MakeNumericId<FBattlerId>(BattlerValue),
				false,
				50,
				0});
		}

		FBattleSetup Setup;
		EBattleSetupValidationError Error = EBattleSetupValidationError::None;
		const bool bCreated = FBattleSetup::TryCreate(Input, Setup, Error);
		check(bCreated);
		return Setup;
	}

	TUniquePtr<FBattleEngine> MakeEngine(
		const FBagScenario& Scenario,
		const uint64 Seed = 8083)
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

	class FObservedBagRandom final : public IBattleRandom
	{
	public:
		explicit FObservedBagRandom(const uint64 Seed)
			: Inner(Seed)
		{
		}

		virtual bool TryDrawUniform(
			const uint32 InclusiveMinimum,
			const uint32 InclusiveMaximum,
			const FBattleRandomContext& Context,
			FBattleRandomDraw& OutDraw) override
		{
			++DrawAttemptCount;
			return Inner.TryDrawUniform(
				InclusiveMinimum,
				InclusiveMaximum,
				Context,
				OutDraw);
		}

		virtual TConstArrayView<FBattleRandomDraw> GetTrace() const override
		{
			return Inner.GetTrace();
		}

		virtual bool TryCreateTransaction(
			const FResolutionId ResolutionId,
			const FActionId OwningActionId,
			TUniquePtr<IBattleRandomTransaction>& OutTransaction) override
		{
			++TransactionAttemptCount;
			return Inner.TryCreateTransaction(
				ResolutionId,
				OwningActionId,
				OutTransaction);
		}

		[[nodiscard]] int32 GetDrawAttemptCount() const
		{
			return DrawAttemptCount;
		}

		[[nodiscard]] int32 GetTransactionAttemptCount() const
		{
			return TransactionAttemptCount;
		}

	private:
		FSeededBattleRandom Inner;
		int32 DrawAttemptCount = 0;
		int32 TransactionAttemptCount = 0;
	};

	TUniquePtr<FBattleEngine> MakeObservedEngine(
		const FBagScenario& Scenario,
		const uint64 Seed,
		FObservedBagRandom*& OutRandom)
	{
		TUniquePtr<FObservedBagRandom> Random =
			MakeUnique<FObservedBagRandom>(Seed);
		OutRandom = Random.Get();
		TUniquePtr<IBattleRandom> RandomOwner = MoveTemp(Random);
		TUniquePtr<FBattleEngine> Engine;
		FBattleRejection Rejection;
		const bool bCreated = FBattleEngine::TryCreate(
			MakeSetup(Scenario),
			MakeCatalog(),
			MoveTemp(RandomOwner),
			Engine,
			Rejection);
		check(bCreated);
		return Engine;
	}

	bool BeginRuntime(FBattleEngine& Engine)
	{
		FBattleRejection Rejection;
		return Engine.TryBeginActionDecisionSequence(Rejection);
	}

	bool SubmitFight(FBattleEngine& Engine)
	{
		const TOptional<FBattleDecisionRequest> Pending = Engine.GetPendingDecision();
		if (!Pending.IsSet())
		{
			return false;
		}
		const FBattleDecisionRequest Request = Pending.GetValue();
		const FMoveId MoveId = MakeDefinitionId<FMoveId>(MoveName);
		const FBattleMoveTargetOption* Target = Request.GetLegalMoveTargets().FindByPredicate(
			[MoveId](const FBattleMoveTargetOption& Option)
			{
				return Option.MoveId == MoveId;
			});
		FBattleDecision Decision;
		return Target != nullptr
			&& FBattleDecision::TryCreateFight(
				Request.GetStateVersion(),
				Request.GetDecisionOwnerTrainerId(),
				Request.GetActingBattlerId(),
				MoveId,
				Target->ActiveSlotId,
				Decision)
			&& Engine.SubmitDecision(Decision).WasAccepted();
	}

	bool SubmitBag(
		FBattleEngine& Engine,
		const FItemId& ItemId,
		const FPartySlotId PartyTarget,
		const FActiveSlotId ActiveTarget)
	{
		const TOptional<FBattleDecisionRequest> Pending = Engine.GetPendingDecision();
		if (!Pending.IsSet())
		{
			return false;
		}
		const FBattleDecisionRequest Request = Pending.GetValue();
		FBattleDecision Decision;
		return FBattleDecision::TryCreateBag(
			Request.GetStateVersion(),
			Request.GetDecisionOwnerTrainerId(),
			Request.GetActingBattlerId(),
			ItemId,
			PartyTarget,
			ActiveTarget,
			Decision)
			&& Engine.SubmitDecision(Decision).WasAccepted();
	}

	bool TryMakeFightDecision(
		const FBattleDecisionRequest& Request,
		FBattleDecision& OutDecision)
	{
		OutDecision = FBattleDecision();
		const FMoveId MoveId = MakeDefinitionId<FMoveId>(MoveName);
		const FBattleMoveTargetOption* Target = Request.GetLegalMoveTargets().FindByPredicate(
			[MoveId](const FBattleMoveTargetOption& Option)
			{
				return Option.MoveId == MoveId;
			});
		return Target != nullptr && FBattleDecision::TryCreateFight(
			Request.GetStateVersion(),
			Request.GetDecisionOwnerTrainerId(),
			Request.GetActingBattlerId(),
			MoveId,
			Target->ActiveSlotId,
			OutDecision);
	}

	bool TryMakeBagDecision(
		const FBattleDecisionRequest& Request,
		const FItemId& ItemId,
		const FPartySlotId PartyTarget,
		const FActiveSlotId ActiveTarget,
		FBattleDecision& OutDecision)
	{
		OutDecision = FBattleDecision();
		return FBattleDecision::TryCreateBag(
			Request.GetStateVersion(),
			Request.GetDecisionOwnerTrainerId(),
			Request.GetActingBattlerId(),
			ItemId,
			PartyTarget,
			ActiveTarget,
			OutDecision);
	}

	bool TryMakeDecisionBatch(
		const TConstArrayView<FBattleDecisionRequest> Requests,
		TArray<FBattleDecision>&& Decisions,
		FBattleDecisionBatch& OutBatch)
	{
		OutBatch = FBattleDecisionBatch();
		if (Requests.IsEmpty() || Decisions.IsEmpty())
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

	bool SubmitFightBatch(FBattleEngine& Engine)
	{
		const TArray<FBattleDecisionRequest> Requests =
			Engine.GetPendingDecisionRequests();
		if (Requests.IsEmpty())
		{
			return false;
		}
		TArray<FBattleDecision> Decisions;
		for (const FBattleDecisionRequest& Request : Requests)
		{
			FBattleDecision Decision;
			if (!TryMakeFightDecision(Request, Decision))
			{
				return false;
			}
			Decisions.Add(MoveTemp(Decision));
		}
		FBattleDecisionBatch Batch;
		return TryMakeDecisionBatch(Requests, MoveTemp(Decisions), Batch)
			&& Engine.SubmitDecisionBatch(Batch).WasAccepted();
	}

	bool LockPlayerBagAndOpponentFight(
		FBattleEngine& Engine,
		const FItemId& ItemId,
		const FPartySlotId PartyTarget = FPartySlotId(),
		const FActiveSlotId ActiveTarget = FActiveSlotId())
	{
		if (Engine.GetSnapshot().GetPhase() == EBattlePhase::Setup
			&& !BeginRuntime(Engine))
		{
			return false;
		}
		if (!SubmitBag(Engine, ItemId, PartyTarget, ActiveTarget)
			|| !SubmitFight(Engine))
		{
			return false;
		}
		return Engine.GetSnapshot().GetPhase() == EBattlePhase::Locked;
	}

	bool LockBothBags(
		FBattleEngine& Engine,
		const FItemId& PlayerItem,
		const FPartySlotId PlayerPartyTarget,
		const FActiveSlotId PlayerActiveTarget,
		const FItemId& OpponentItem,
		const FPartySlotId OpponentPartyTarget,
		const FActiveSlotId OpponentActiveTarget)
	{
		return BeginRuntime(Engine)
			&& SubmitBag(
				Engine,
				PlayerItem,
				PlayerPartyTarget,
				PlayerActiveTarget)
			&& SubmitBag(
				Engine,
				OpponentItem,
				OpponentPartyTarget,
				OpponentActiveTarget)
			&& Engine.GetSnapshot().GetPhase() == EBattlePhase::Locked;
	}

	int32 GetBagCount(
		const FBattleEngine& Engine,
		const FTrainerId TrainerId,
		const FItemId& ItemId)
	{
		const FBattleTrainerState* Trainer =
			FBattleC08CBagEngineFixture::GetTrainer(Engine, TrainerId);
		const FBattleBagItemCount* Item = Trainer != nullptr
			? Trainer->Bag.FindByPredicate(
				[&ItemId](const FBattleBagItemCount& Candidate)
				{
					return Candidate.ItemId == ItemId;
				})
			: nullptr;
		return Item != nullptr ? Item->Count : INDEX_NONE;
	}

	int32 GetSetupBagCount(
		const FBattleEngine& Engine,
		const FTrainerId TrainerId,
		const FItemId& ItemId)
	{
		const FBattleTrainerSetup* Trainer =
			FBattleC08CBagEngineFixture::GetState(Engine).Setup.GetTrainers().FindByPredicate(
				[TrainerId](const FBattleTrainerSetup& Candidate)
				{
					return Candidate.TrainerId == TrainerId;
				});
		const FBattleBagItemCount* Item = Trainer != nullptr
			? Trainer->Bag.FindByPredicate(
				[&ItemId](const FBattleBagItemCount& Candidate)
				{
					return Candidate.ItemId == ItemId;
				})
			: nullptr;
		return Item != nullptr ? Item->Count : INDEX_NONE;
	}

	int32 FindEvent(
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

	bool IsReturnedResolutionAppendedExactlyOnce(
		const FBattleEngine& Engine,
		const FBattleResolution& Returned)
	{
		const FBattleEngineState& State =
			FBattleC08CBagEngineFixture::GetState(Engine);
		int32 MatchingIdentityCount = 0;
		const FBattleResolution* Appended = nullptr;
		for (const FBattleResolution& Candidate : State.Resolutions)
		{
			if (Candidate.GetResolutionId() == Returned.GetResolutionId())
			{
				++MatchingIdentityCount;
				Appended = &Candidate;
			}
		}
		if (MatchingIdentityCount != 1
			|| Appended == nullptr
			|| Appended->WasAccepted() != Returned.WasAccepted()
			|| Appended->GetBeforeStateVersion()
				!= Returned.GetBeforeStateVersion()
			|| Appended->GetAfterStateVersion()
				!= Returned.GetAfterStateVersion()
			|| Appended->GetRejection().Reason
				!= Returned.GetRejection().Reason
			|| Appended->GetEvents().Num() != Returned.GetEvents().Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < Appended->GetEvents().Num(); ++Index)
		{
			const FBattleEvent& Left = Appended->GetEvents()[Index];
			const FBattleEvent& Right = Returned.GetEvents()[Index];
			if (Left.GetEventOrdinal() != Right.GetEventOrdinal()
				|| Left.GetBattleId() != Right.GetBattleId()
				|| Left.GetTurnId() != Right.GetTurnId()
				|| Left.GetActionId() != Right.GetActionId()
				|| Left.GetResolutionId() != Right.GetResolutionId()
				|| Left.GetType() != Right.GetType()
				|| Left.GetCause() != Right.GetCause()
				|| Left.GetCauseActionKind() != Right.GetCauseActionKind()
				|| Left.GetNumericBefore() != Right.GetNumericBefore()
				|| Left.GetNumericAfter() != Right.GetNumericAfter()
				|| Left.GetNumericDelta() != Right.GetNumericDelta())
			{
				return false;
			}
		}
		return true;
	}

	bool AreStaleBagEventSourcesIdentical(
		const FBattleEventSource& Left,
		const FBattleEventSource& Right)
	{
		return Left.TrainerId == Right.TrainerId
			&& Left.BattlerId == Right.BattlerId
			&& Left.ActiveSlotId == Right.ActiveSlotId
			&& Left.DefinitionId == Right.DefinitionId;
	}

	bool AreStaleBagEventTargetsIdentical(
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

	bool AreStaleBagEventsIdentical(
		const FBattleEvent& Left,
		const FBattleEvent& Right)
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
			|| !AreStaleBagEventSourcesIdentical(
				Left.GetSource(),
				Right.GetSource())
			|| !AreStaleBagEventTargetsIdentical(
				Left.GetTargets(),
				Right.GetTargets())
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
			const FBattleActionOrderMetadata& L =
				Left.GetActionOrder().GetValue();
			const FBattleActionOrderMetadata& R =
				Right.GetActionOrder().GetValue();
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
				|| L.bUsedFaintedTargetFallback
					!= R.bUsedFaintedTargetFallback)
			{
				return false;
			}
		}
		if (Left.GetCapture().IsSet()
			&& !(Left.GetCapture().GetValue()
				== Right.GetCapture().GetValue()))
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

	bool AreStaleBagRejectionsIdentical(
		const FBattleRejection& Left,
		const FBattleRejection& Right)
	{
		return Left.Reason == Right.Reason
			&& Left.TrainerId == Right.TrainerId
			&& Left.BattlerId == Right.BattlerId
			&& Left.ActionId == Right.ActionId
			&& Left.MoveId == Right.MoveId
			&& Left.ItemId == Right.ItemId
			&& Left.PartySlotId == Right.PartySlotId
			&& Left.ActiveSlotId == Right.ActiveSlotId;
	}

	bool AreStaleBagResolutionsIdentical(
		const FBattleResolution& Left,
		const FBattleResolution& Right)
	{
		if (Left.IsValid() != Right.IsValid()
			|| Left.WasAccepted() != Right.WasAccepted()
			|| Left.GetResolutionId() != Right.GetResolutionId()
			|| Left.GetBeforeStateVersion() != Right.GetBeforeStateVersion()
			|| Left.GetAfterStateVersion() != Right.GetAfterStateVersion()
			|| !AreStaleBagRejectionsIdentical(
				Left.GetRejection(),
				Right.GetRejection())
			|| Left.GetEvents().Num() != Right.GetEvents().Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < Left.GetEvents().Num(); ++Index)
		{
			if (!AreStaleBagEventsIdentical(
					Left.GetEvents()[Index],
					Right.GetEvents()[Index]))
			{
				return false;
			}
		}
		return true;
	}

	bool IsStaleBagEventHistoryPrefix(
		const TConstArrayView<FBattleEvent> Prefix,
		const TConstArrayView<FBattleEvent> Complete)
	{
		if (Prefix.Num() > Complete.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < Prefix.Num(); ++Index)
		{
			if (!AreStaleBagEventsIdentical(Prefix[Index], Complete[Index]))
			{
				return false;
			}
		}
		return true;
	}

	bool IsStaleBagResolutionHistoryPrefix(
		const TConstArrayView<FBattleResolution> Prefix,
		const TConstArrayView<FBattleResolution> Complete)
	{
		if (Prefix.Num() > Complete.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < Prefix.Num(); ++Index)
		{
			if (!AreStaleBagResolutionsIdentical(Prefix[Index], Complete[Index]))
			{
				return false;
			}
		}
		return true;
	}

	template <typename T>
	bool AreStaleBagSimpleViewsIdentical(
		const TConstArrayView<T> Left,
		const TConstArrayView<T> Right)
	{
		if (Left.Num() != Right.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < Left.Num(); ++Index)
		{
			if (Left[Index] != Right[Index])
			{
				return false;
			}
		}
		return true;
	}

	bool AreStaleBagRequestsIdentical(
		const FBattleDecisionRequest& Left,
		const FBattleDecisionRequest& Right)
	{
		if (Left.IsValid() != Right.IsValid()
			|| Left.GetStateVersion() != Right.GetStateVersion()
			|| Left.GetRequestKind() != Right.GetRequestKind()
			|| Left.GetDecisionOwnerTrainerId()
				!= Right.GetDecisionOwnerTrainerId()
			|| Left.GetActingBattlerId() != Right.GetActingBattlerId()
			|| Left.GetActingSlotId() != Right.GetActingSlotId()
			|| !AreStaleBagSimpleViewsIdentical(
				Left.GetLegalActionKinds(),
				Right.GetLegalActionKinds())
			|| !AreStaleBagSimpleViewsIdentical(
				Left.GetLegalMoveIds(),
				Right.GetLegalMoveIds())
			|| !AreStaleBagSimpleViewsIdentical(
				Left.GetAutomaticallyTargetedMoveIds(),
				Right.GetAutomaticallyTargetedMoveIds())
			|| !AreStaleBagSimpleViewsIdentical(
				Left.GetLegalSwitchPartySlots(),
				Right.GetLegalSwitchPartySlots())
			|| !AreStaleBagSimpleViewsIdentical(
				Left.GetLegalItemIds(),
				Right.GetLegalItemIds())
			|| !AreStaleBagSimpleViewsIdentical(
				Left.GetLegalActiveTargets(),
				Right.GetLegalActiveTargets())
			|| !AreStaleBagSimpleViewsIdentical(
				Left.GetLegalPartyTargets(),
				Right.GetLegalPartyTargets()))
		{
			return false;
		}

		const TConstArrayView<FBattleMoveTargetOption> LMove =
			Left.GetLegalMoveTargets();
		const TConstArrayView<FBattleMoveTargetOption> RMove =
			Right.GetLegalMoveTargets();
		const TConstArrayView<FBattleItemPartyTargetOption> LParty =
			Left.GetLegalItemPartyTargets();
		const TConstArrayView<FBattleItemPartyTargetOption> RParty =
			Right.GetLegalItemPartyTargets();
		const TConstArrayView<FBattleItemActiveTargetOption> LActive =
			Left.GetLegalItemActiveTargets();
		const TConstArrayView<FBattleItemActiveTargetOption> RActive =
			Right.GetLegalItemActiveTargets();
		const TConstArrayView<FBattleUnavailableDecisionOption> LUnavailable =
			Left.GetUnavailableOptions();
		const TConstArrayView<FBattleUnavailableDecisionOption> RUnavailable =
			Right.GetUnavailableOptions();
		if (LMove.Num() != RMove.Num()
			|| LParty.Num() != RParty.Num()
			|| LActive.Num() != RActive.Num()
			|| LUnavailable.Num() != RUnavailable.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < LMove.Num(); ++Index)
		{
			if (LMove[Index].MoveId != RMove[Index].MoveId
				|| LMove[Index].ActiveSlotId != RMove[Index].ActiveSlotId)
			{
				return false;
			}
		}
		for (int32 Index = 0; Index < LParty.Num(); ++Index)
		{
			if (LParty[Index].ItemId != RParty[Index].ItemId
				|| LParty[Index].PartySlotId != RParty[Index].PartySlotId)
			{
				return false;
			}
		}
		for (int32 Index = 0; Index < LActive.Num(); ++Index)
		{
			if (LActive[Index].ItemId != RActive[Index].ItemId
				|| LActive[Index].ActiveSlotId != RActive[Index].ActiveSlotId)
			{
				return false;
			}
		}
		for (int32 Index = 0; Index < LUnavailable.Num(); ++Index)
		{
			const FBattleUnavailableDecisionOption& L = LUnavailable[Index];
			const FBattleUnavailableDecisionOption& R = RUnavailable[Index];
			if (L.Kind != R.Kind
				|| L.Reason != R.Reason
				|| L.ActionKind != R.ActionKind
				|| L.MoveId != R.MoveId
				|| L.PartySlotId != R.PartySlotId
				|| L.ItemId != R.ItemId
				|| L.ActiveSlotId != R.ActiveSlotId)
			{
				return false;
			}
		}
		return true;
	}

	bool AreStaleBagPendingRequestsIdentical(
		const TOptional<FBattleDecisionRequest>& LeftPending,
		const TOptional<FBattleDecisionRequest>& RightPending,
		const TConstArrayView<FBattleDecisionRequest> LeftRequests,
		const TConstArrayView<FBattleDecisionRequest> RightRequests)
	{
		if (LeftPending.IsSet() != RightPending.IsSet()
			|| LeftRequests.Num() != RightRequests.Num())
		{
			return false;
		}
		if (LeftPending.IsSet()
			&& !AreStaleBagRequestsIdentical(
				LeftPending.GetValue(),
				RightPending.GetValue()))
		{
			return false;
		}
		for (int32 Index = 0; Index < LeftRequests.Num(); ++Index)
		{
			if (!AreStaleBagRequestsIdentical(
					LeftRequests[Index],
					RightRequests[Index]))
			{
				return false;
			}
		}
		return true;
	}

	bool AreStaleBagPendingReplacementsIdentical(
		const TConstArrayView<FBattlePendingReplacementState> Left,
		const TConstArrayView<FBattlePendingReplacementState> Right)
	{
		if (Left.Num() != Right.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < Left.Num(); ++Index)
		{
			if (Left[Index].TrainerId != Right[Index].TrainerId
				|| Left[Index].ActiveSlotId != Right[Index].ActiveSlotId)
			{
				return false;
			}
		}
		return true;
	}

	bool AreStaleBagTrainersIdentical(
		const TConstArrayView<FBattleTrainerState> Left,
		const TConstArrayView<FBattleTrainerState> Right)
	{
		if (Left.Num() != Right.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < Left.Num(); ++Index)
		{
			const FBattleTrainerState& L = Left[Index];
			const FBattleTrainerState& R = Right[Index];
			if (L.TrainerId != R.TrainerId
				|| L.Side != R.Side
				|| L.Role != R.Role
				|| L.Controller != R.Controller
				|| L.SelectorProfileId != R.SelectorProfileId
				|| L.ActionAllowance.MaximumActions
					!= R.ActionAllowance.MaximumActions
				|| L.ActionAllowance.RemainingActions
					!= R.ActionAllowance.RemainingActions
				|| L.ActionAllowance.bBagActionAvailable
					!= R.ActionAllowance.bBagActionAvailable
				|| L.Bag.Num() != R.Bag.Num()
				|| L.PartySlots.Num() != R.PartySlots.Num())
			{
				return false;
			}
			for (int32 BagIndex = 0; BagIndex < L.Bag.Num(); ++BagIndex)
			{
				if (L.Bag[BagIndex].ItemId != R.Bag[BagIndex].ItemId
					|| L.Bag[BagIndex].Count != R.Bag[BagIndex].Count)
				{
					return false;
				}
			}
			for (int32 PartyIndex = 0;
				PartyIndex < L.PartySlots.Num();
				++PartyIndex)
			{
				if (L.PartySlots[PartyIndex].PartySlotId
						!= R.PartySlots[PartyIndex].PartySlotId
					|| L.PartySlots[PartyIndex].BattlerId
						!= R.PartySlots[PartyIndex].BattlerId)
				{
					return false;
				}
			}
		}
		return true;
	}

	bool AreStaleBagStatStagesIdentical(
		const FBattleStatStages& Left,
		const FBattleStatStages& Right)
	{
		for (uint8 Index = static_cast<uint8>(EBattleStat::Attack);
			Index <= static_cast<uint8>(EBattleStat::Evasion);
			++Index)
		{
			int32 LeftStage = 0;
			int32 RightStage = 0;
			const EBattleStat Stat = static_cast<EBattleStat>(Index);
			if (!Left.TryGetStage(Stat, LeftStage)
				|| !Right.TryGetStage(Stat, RightStage)
				|| LeftStage != RightStage)
			{
				return false;
			}
		}
		return true;
	}

	bool AreStaleBagConditionsIdentical(
		const TConstArrayView<FBattleConditionState> Left,
		const TConstArrayView<FBattleConditionState> Right)
	{
		if (Left.Num() != Right.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < Left.Num(); ++Index)
		{
			if (Left[Index].ConditionId != Right[Index].ConditionId
				|| Left[Index].RemainingTurns != Right[Index].RemainingTurns
				|| Left[Index].LayerCount != Right[Index].LayerCount
				|| Left[Index].CreationOrdinal != Right[Index].CreationOrdinal
				|| Left[Index].SourceBattlerId != Right[Index].SourceBattlerId)
			{
				return false;
			}
		}
		return true;
	}

	bool AreStaleBagBattlersIdentical(
		const TConstArrayView<FBattleBattlerState> Left,
		const TConstArrayView<FBattleBattlerState> Right)
	{
		if (Left.Num() != Right.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < Left.Num(); ++Index)
		{
			const FBattleBattlerState& L = Left[Index];
			const FBattleBattlerState& R = Right[Index];
			if (L.TrainerId != R.TrainerId
				|| L.BattlerId != R.BattlerId
				|| L.SourcePokemonId != R.SourcePokemonId
				|| L.PartySlotId != R.PartySlotId
				|| L.SpeciesFormId != R.SpeciesFormId
				|| L.CaptureClassification != R.CaptureClassification
				|| L.Level != R.Level
				|| L.PermanentStats.MaxHP != R.PermanentStats.MaxHP
				|| L.PermanentStats.Attack != R.PermanentStats.Attack
				|| L.PermanentStats.Defense != R.PermanentStats.Defense
				|| L.PermanentStats.SpecialAttack
					!= R.PermanentStats.SpecialAttack
				|| L.PermanentStats.SpecialDefense
					!= R.PermanentStats.SpecialDefense
				|| L.PermanentStats.Speed != R.PermanentStats.Speed
				|| L.CurrentHP != R.CurrentHP
				|| L.bFainted != R.bFainted
				|| L.bCaptured != R.bCaptured
				|| L.bRemoved != R.bRemoved
				|| L.bFaintTransitionPending != R.bFaintTransitionPending
				|| L.bEgg != R.bEgg
				|| L.MajorStatusId != R.MajorStatusId
				|| !AreStaleBagStatStagesIdentical(L.Stages, R.Stages)
				|| !AreStaleBagConditionsIdentical(L.Volatiles, R.Volatiles)
				|| L.AbilityId != R.AbilityId
				|| L.bAbilitySuppressed != R.bAbilitySuppressed
				|| L.EnteredActiveOnTurnId != R.EnteredActiveOnTurnId
				|| L.HeldItem.InstanceId != R.HeldItem.InstanceId
				|| L.HeldItem.OriginalItemId != R.HeldItem.OriginalItemId
				|| L.HeldItem.CurrentItemId != R.HeldItem.CurrentItemId
				|| L.HeldItem.bConsumed != R.HeldItem.bConsumed
				|| L.HeldItem.bSuppressed != R.HeldItem.bSuppressed
				|| L.HeldItem.bRevealed != R.HeldItem.bRevealed
				|| L.HeldItem.bTemporarilyRemoved
					!= R.HeldItem.bTemporarilyRemoved
				|| L.HeldItem.ChoiceLockedMoveId
					!= R.HeldItem.ChoiceLockedMoveId
				|| L.LastMoveId != R.LastMoveId
				|| L.Obedience.bHasSnapshot != R.Obedience.bHasSnapshot
				|| L.Obedience.bSubjectToPlayerCap
					!= R.Obedience.bSubjectToPlayerCap
				|| L.Obedience.ReferenceLevel != R.Obedience.ReferenceLevel
				|| L.Obedience.BadgeCount != R.Obedience.BadgeCount
				|| L.Moves.Num() != R.Moves.Num())
			{
				return false;
			}
			for (int32 MoveIndex = 0; MoveIndex < L.Moves.Num(); ++MoveIndex)
			{
				if (L.Moves[MoveIndex].SlotIndex != R.Moves[MoveIndex].SlotIndex
					|| L.Moves[MoveIndex].MoveId != R.Moves[MoveIndex].MoveId
					|| L.Moves[MoveIndex].CurrentPP
						!= R.Moves[MoveIndex].CurrentPP
					|| L.Moves[MoveIndex].MaxPP != R.Moves[MoveIndex].MaxPP)
				{
					return false;
				}
			}
		}
		return true;
	}

	bool AreStaleBagActivePositionsIdentical(
		const TConstArrayView<FBattleActivePositionState> Left,
		const TConstArrayView<FBattleActivePositionState> Right)
	{
		if (Left.Num() != Right.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < Left.Num(); ++Index)
		{
			if (Left[Index].ActiveSlotId != Right[Index].ActiveSlotId
				|| Left[Index].bAvailable != Right[Index].bAvailable
				|| Left[Index].TrainerId != Right[Index].TrainerId
				|| Left[Index].BattlerId != Right[Index].BattlerId)
			{
				return false;
			}
		}
		return true;
	}

	bool AreStaleBagDecisionsIdentical(
		const FBattleDecision& Left,
		const FBattleDecision& Right)
	{
		return Left.IsValid() == Right.IsValid()
			&& Left.GetStateVersion() == Right.GetStateVersion()
			&& Left.GetRequestKind() == Right.GetRequestKind()
			&& Left.GetDecisionOwnerTrainerId()
				== Right.GetDecisionOwnerTrainerId()
			&& Left.GetActingBattlerId() == Right.GetActingBattlerId()
			&& Left.GetActionKind() == Right.GetActionKind()
			&& Left.GetMoveId() == Right.GetMoveId()
			&& Left.GetSwitchPartySlotId() == Right.GetSwitchPartySlotId()
			&& Left.GetItemId() == Right.GetItemId()
			&& Left.GetItemPartyTargetId() == Right.GetItemPartyTargetId()
			&& Left.GetActiveTargetId() == Right.GetActiveTargetId();
	}

	bool AreStaleBagLockedActionsIdentical(
		const FBattleLockedActionState& Left,
		const FBattleLockedActionState& Right,
		const bool bIgnoreFinished = false)
	{
		if (Left.ActionId != Right.ActionId
			|| Left.QueueOrdinal != Right.QueueOrdinal
			|| !AreStaleBagDecisionsIdentical(Left.Decision, Right.Decision)
			|| Left.OrderKey.CommandBand != Right.OrderKey.CommandBand
			|| Left.OrderKey.MovePriority != Right.OrderKey.MovePriority
			|| Left.OrderKey.FractionalPriorityTenths
				!= Right.OrderKey.FractionalPriorityTenths
			|| Left.OrderKey.EffectiveSpeed != Right.OrderKey.EffectiveSpeed
			|| Left.OrderKey.ActingSlotId != Right.OrderKey.ActingSlotId
			|| Left.TargetClass != Right.TargetClass
			|| Left.SelectedTargetBattlerId != Right.SelectedTargetBattlerId
			|| Left.bStarted != Right.bStarted
			|| Left.bMoveCommitted != Right.bMoveCommitted
			|| Left.TargetResolution.IsSet() != Right.TargetResolution.IsSet()
			|| Left.EffectExecutionState != Right.EffectExecutionState
			|| (!bIgnoreFinished && Left.bFinished != Right.bFinished))
		{
			return false;
		}
		if (Left.TargetResolution.IsSet())
		{
			const FBattleTargetResolutionResult& L =
				Left.TargetResolution.GetValue();
			const FBattleTargetResolutionResult& R =
				Right.TargetResolution.GetValue();
			return L.TargetClass == R.TargetClass
				&& L.Outcome == R.Outcome
				&& L.Targets == R.Targets
				&& L.bWasRedirected == R.bWasRedirected
				&& L.bUsedFaintedTargetFallback
					== R.bUsedFaintedTargetFallback;
		}
		return true;
	}

	bool AreStaleBagLockedActionArraysIdentical(
		const TConstArrayView<FBattleLockedActionState> Left,
		const TConstArrayView<FBattleLockedActionState> Right,
		const int32 FinishedDifferenceIndex = INDEX_NONE)
	{
		if (Left.Num() != Right.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < Left.Num(); ++Index)
		{
			if (!AreStaleBagLockedActionsIdentical(
					Left[Index],
					Right[Index],
					Index == FinishedDifferenceIndex))
			{
				return false;
			}
		}
		return true;
	}

	struct FStaleBagObservation
	{
		uint64 StateVersion = 0;
		uint64 NextResolutionId = 0;
		uint64 NextEventOrdinal = 0;
		uint64 NextActionId = 0;
		uint64 NextConditionCreationOrdinal = 0;
		uint64 NextTriggerReentrancyToken = 0;
		int32 CurrentLockedActionIndex = INDEX_NONE;
		EBattlePhase Phase = EBattlePhase::Setup;
		EBattleOutcome Outcome = EBattleOutcome::InProgress;
		EBattleOutcomeCause OutcomeCause = EBattleOutcomeCause::None;
		uint32 EscapeAttemptCount = 0;
		bool bEndTurnTriggerPassComplete = false;
		bool bReinforcementSucceeded = false;
		FBattleCaptureCapacitySnapshot CaptureCapacity;
		TArray<FBattleTrainerState> Trainers;
		TArray<FBattleBattlerState> Battlers;
		TArray<FBattleActivePositionState> ActivePositions;
		TArray<FBattlePendingCaptureState> PendingCaptures;
		TArray<FBattleLockedActionState> LockedActions;
		TOptional<FBattleDecisionRequest> PendingDecision;
		TArray<FBattleDecisionRequest> PendingDecisionRequests;
		TArray<FBattlePendingReplacementState> PendingReplacements;
		TArray<FBattleRandomDraw> RandomTrace;
		int32 RandomDrawAttemptCount = INDEX_NONE;
		int32 RandomTransactionAttemptCount = INDEX_NONE;
		bool bHasActingTrainerPolicy = false;
		bool bActingTrainerMayUseBag = false;
		TArray<FBattleResolution> Resolutions;
		TArray<FBattleEvent> Events;
	};

	FStaleBagObservation ObserveStaleBag(
		const FBattleEngine& Engine,
		const FObservedBagRandom& Random,
		const FTrainerId ActingTrainerId)
	{
		const FBattleEngineState& State =
			FBattleC08CBagEngineFixture::GetState(Engine);
		FStaleBagObservation Observation;
		Observation.StateVersion = State.StateVersion;
		Observation.NextResolutionId = State.NextResolutionId;
		Observation.NextEventOrdinal = State.NextEventOrdinal;
		Observation.NextActionId = State.NextActionId;
		Observation.NextConditionCreationOrdinal =
			State.NextConditionCreationOrdinal;
		Observation.NextTriggerReentrancyToken =
			State.NextTriggerReentrancyToken;
		Observation.CurrentLockedActionIndex = State.CurrentLockedActionIndex;
		Observation.Phase = State.Phase;
		Observation.Outcome = State.Outcome;
		Observation.OutcomeCause = State.OutcomeCause;
		Observation.EscapeAttemptCount = State.EscapeAttemptCount;
		Observation.bEndTurnTriggerPassComplete =
			State.bEndTurnTriggerPassComplete;
		Observation.bReinforcementSucceeded = State.bReinforcementSucceeded;
		Observation.CaptureCapacity = State.CaptureCapacity;
		Observation.Trainers = State.Trainers;
		Observation.Battlers = State.Battlers;
		Observation.ActivePositions = State.ActivePositions;
		Observation.PendingCaptures = State.PendingCaptures;
		Observation.LockedActions = State.LockedActions;
		Observation.PendingDecision = State.PendingDecision;
		Observation.PendingDecisionRequests = State.PendingDecisionRequests;
		Observation.PendingReplacements = State.PendingReplacements;
		Observation.RandomTrace.Append(State.Random->GetTrace());
		Observation.RandomDrawAttemptCount = Random.GetDrawAttemptCount();
		Observation.RandomTransactionAttemptCount =
			Random.GetTransactionAttemptCount();
		const FBattleTrainerEncounterPolicy* Policy =
			State.CompiledEncounterPolicies.FindTrainerPolicy(ActingTrainerId);
		Observation.bHasActingTrainerPolicy = Policy != nullptr;
		Observation.bActingTrainerMayUseBag =
			Policy != nullptr && Policy->bMayUseBag;
		Observation.Resolutions = State.Resolutions;
		Observation.Events = State.OrderedEvents;
		return Observation;
	}

	bool AreStaleBagResourceFactsIdentical(
		const FStaleBagObservation& Left,
		const FStaleBagObservation& Right)
	{
		return AreStaleBagTrainersIdentical(Left.Trainers, Right.Trainers)
			&& AreStaleBagBattlersIdentical(Left.Battlers, Right.Battlers)
			&& AreStaleBagActivePositionsIdentical(
				Left.ActivePositions,
				Right.ActivePositions)
			&& Left.PendingCaptures == Right.PendingCaptures
			&& Left.RandomTrace == Right.RandomTrace
			&& Left.RandomDrawAttemptCount == Right.RandomDrawAttemptCount
			&& Left.RandomTransactionAttemptCount
				== Right.RandomTransactionAttemptCount
			&& Left.bHasActingTrainerPolicy == Right.bHasActingTrainerPolicy
			&& Left.bActingTrainerMayUseBag == Right.bActingTrainerMayUseBag;
	}

	bool AreRejectedStaleBagFactsIdentical(
		const FStaleBagObservation& Left,
		const FStaleBagObservation& Right)
	{
		return Left.StateVersion == Right.StateVersion
			&& Left.NextActionId == Right.NextActionId
			&& Left.NextConditionCreationOrdinal
				== Right.NextConditionCreationOrdinal
			&& Left.NextTriggerReentrancyToken
				== Right.NextTriggerReentrancyToken
			&& Left.CurrentLockedActionIndex
				== Right.CurrentLockedActionIndex
			&& Left.Phase == Right.Phase
			&& Left.Outcome == Right.Outcome
			&& Left.OutcomeCause == Right.OutcomeCause
			&& Left.EscapeAttemptCount == Right.EscapeAttemptCount
			&& Left.bEndTurnTriggerPassComplete
				== Right.bEndTurnTriggerPassComplete
			&& Left.bReinforcementSucceeded == Right.bReinforcementSucceeded
			&& Left.CaptureCapacity.PartySlotsRemaining
				== Right.CaptureCapacity.PartySlotsRemaining
			&& Left.CaptureCapacity.StorageSlotsRemaining
				== Right.CaptureCapacity.StorageSlotsRemaining
			&& AreStaleBagResourceFactsIdentical(Left, Right)
			&& AreStaleBagLockedActionArraysIdentical(
				Left.LockedActions,
				Right.LockedActions)
			&& AreStaleBagPendingRequestsIdentical(
				Left.PendingDecision,
				Right.PendingDecision,
				Left.PendingDecisionRequests,
				Right.PendingDecisionRequests)
			&& AreStaleBagPendingReplacementsIdentical(
				Left.PendingReplacements,
				Right.PendingReplacements);
	}

	bool IsStaleBagResolutionAppendedExactlyOnce(
		const FBattleEngine& Engine,
		const FBattleResolution& Returned)
	{
		const FBattleEngineState& State =
			FBattleC08CBagEngineFixture::GetState(Engine);
		int32 MatchingCount = 0;
		const FBattleResolution* Match = nullptr;
		for (const FBattleResolution& Candidate : State.Resolutions)
		{
			if (Candidate.GetResolutionId() == Returned.GetResolutionId())
			{
				++MatchingCount;
				Match = &Candidate;
			}
		}
		return MatchingCount == 1
			&& Match != nullptr
			&& AreStaleBagResolutionsIdentical(*Match, Returned);
	}

	bool DoesStaleBagReplayContainResolutionExactlyOnce(
		const FBattleEngine& Engine,
		const FStaleBagObservation& Before,
		const FBattleResolution& Returned)
	{
		const FBattleReplayRecord Replay = Engine.ExportReplayRecord();
		if (!Replay.IsValid()
			|| Replay.GetSchemaVersion()
				!= FBattleReplayRecord::CurrentSchemaVersion
			|| Replay.GetResolutions().Num() != Before.Resolutions.Num() + 1
			|| !IsStaleBagResolutionHistoryPrefix(
				Before.Resolutions,
				Replay.GetResolutions()))
		{
			return false;
		}
		int32 MatchingCount = 0;
		const FBattleResolution* Match = nullptr;
		for (const FBattleResolution& Candidate : Replay.GetResolutions())
		{
			if (Candidate.GetResolutionId() == Returned.GetResolutionId())
			{
				++MatchingCount;
				Match = &Candidate;
			}
		}
		return MatchingCount == 1
			&& Match != nullptr
			&& AreStaleBagResolutionsIdentical(*Match, Returned);
	}

	bool VerifyRejectedStaleBagFailure(
		FAutomationTestBase& Test,
		const FString& Label,
		const FBattleEngine& Engine,
		const FObservedBagRandom& Random,
		const FTrainerId ActingTrainerId,
		const FStaleBagObservation& Before,
		const FBattleResolution& Returned)
	{
		const FStaleBagObservation After = ObserveStaleBag(
			Engine,
			Random,
			ActingTrainerId);
		bool bValid = true;
		bValid &= Test.TestFalse(
			Label + TEXT(" rejects the fallible cancellation"),
			Returned.WasAccepted());
		bValid &= Test.TestEqual(
			Label + TEXT(" reports the typed checkpoint failure"),
			Returned.GetRejection().Reason,
			EBattleRejectionReason::CheckpointPreparationFailed);
		bValid &= Test.TestTrue(
			Label + TEXT(" appends the exact rejection resolution once"),
			IsStaleBagResolutionAppendedExactlyOnce(Engine, Returned));
		bValid &= Test.TestTrue(
			Label + TEXT(" publishes exactly one rule rejection event"),
			Returned.GetEvents().Num() == 1
				&& Returned.GetEvents()[0].GetType()
					== EBattleEventType::ActionCanceled
				&& Returned.GetEvents()[0].GetCause()
					== EBattleEventCause::Rule
				&& Returned.GetEvents()[0].GetCauseActionKind()
					== EBattleActionKind::Bag
				&& Returned.GetEvents()[0].GetEventOrdinal()
					== Before.NextEventOrdinal
				&& Returned.GetEvents()[0].GetVisibility().Level
					== EBattleVisibilityLevel::Public);
		bValid &= Test.TestEqual(
			Label + TEXT(" appends one resolution"),
			After.Resolutions.Num(),
			Before.Resolutions.Num() + 1);
		bValid &= Test.TestEqual(
			Label + TEXT(" appends one event"),
			After.Events.Num(),
			Before.Events.Num() + 1);
		bValid &= Test.TestEqual(
			Label + TEXT(" advances only the rejection resolution identity"),
			After.NextResolutionId,
			Before.NextResolutionId + 1);
		bValid &= Test.TestEqual(
			Label + TEXT(" advances only the rejection event identity"),
			After.NextEventOrdinal,
			Before.NextEventOrdinal + 1);
		bValid &= Test.TestTrue(
			Label + TEXT(" preserves every non-history checkpoint fact"),
			AreRejectedStaleBagFactsIdentical(After, Before));
		bValid &= Test.TestTrue(
			Label + TEXT(" preserves the existing resolution history prefix"),
			IsStaleBagResolutionHistoryPrefix(
				Before.Resolutions,
				After.Resolutions));
		bValid &= Test.TestTrue(
			Label + TEXT(" preserves the existing event history prefix"),
			IsStaleBagEventHistoryPrefix(Before.Events, After.Events));
		bValid &= Test.TestTrue(
			Label + TEXT(" replays the same rejection exactly once"),
			DoesStaleBagReplayContainResolutionExactlyOnce(
				Engine,
				Before,
				Returned));
		return bValid;
	}

	bool VerifyAcceptedStaleBagCancellation(
		FAutomationTestBase& Test,
		const FString& Label,
		const FBattleEngine& Engine,
		const FObservedBagRandom& Random,
		const FTrainerId ActingTrainerId,
		const EBattlePhase ExpectedPhase,
		const FStaleBagObservation& Before,
		const FBattleResolution& Returned)
	{
		const FStaleBagObservation After = ObserveStaleBag(
			Engine,
			Random,
			ActingTrainerId);
		const FBattleEngineState& State =
			FBattleC08CBagEngineFixture::GetState(Engine);
		const int32 CanceledIndex = Before.CurrentLockedActionIndex;
		bool bValid = true;
		bValid &= Test.TestTrue(
			Label + TEXT(" accepts the stale cancellation"),
			Returned.WasAccepted());
		bValid &= Test.TestTrue(
			Label + TEXT(" appends the exact accepted resolution once"),
			IsStaleBagResolutionAppendedExactlyOnce(Engine, Returned));
		bValid &= Test.TestTrue(
			Label + TEXT(" publishes exact cancellation event order"),
			HasExactEventOrder(Returned, {
				EBattleEventType::ActionCanceled,
				EBattleEventType::ActionCompleted})
				&& Returned.GetEvents()[0].GetCause() == EBattleEventCause::Item
				&& Returned.GetEvents()[1].GetCause() == EBattleEventCause::Action
				&& Returned.GetEvents()[0].GetEventOrdinal()
					== Before.NextEventOrdinal
				&& Returned.GetEvents()[1].GetEventOrdinal()
					== Before.NextEventOrdinal + 1);
		bValid &= Test.TestEqual(
			Label + TEXT(" advances state version once"),
			After.StateVersion,
			Before.StateVersion + 1);
		bValid &= Test.TestTrue(
			Label + TEXT(" reports the exact accepted version pair"),
			Returned.GetBeforeStateVersion() == Before.StateVersion
				&& Returned.GetAfterStateVersion() == Before.StateVersion + 1);
		bValid &= Test.TestEqual(
			Label + TEXT(" advances the action cursor once"),
			After.CurrentLockedActionIndex,
			Before.CurrentLockedActionIndex + 1);
		bValid &= Test.TestEqual(
			Label + TEXT(" reaches the exact boundary phase"),
			After.Phase,
			ExpectedPhase);
		bValid &= Test.TestTrue(
			Label + TEXT(" finishes only the canceled action"),
			State.LockedActions.IsValidIndex(CanceledIndex)
				&& Before.LockedActions.IsValidIndex(CanceledIndex)
				&& !Before.LockedActions[CanceledIndex].bFinished
				&& State.LockedActions[CanceledIndex].bFinished
				&& AreStaleBagLockedActionArraysIdentical(
					Before.LockedActions,
					State.LockedActions,
					CanceledIndex));
		bValid &= Test.TestTrue(
			Label + TEXT(" consumes no item, quota, RNG, battler, slot, or capture fact"),
			AreStaleBagResourceFactsIdentical(After, Before));
		bValid &= Test.TestTrue(
			Label + TEXT(" preserves outcome and pending-request facts"),
			After.Outcome == Before.Outcome
				&& After.OutcomeCause == Before.OutcomeCause
				&& AreStaleBagPendingRequestsIdentical(
					After.PendingDecision,
					Before.PendingDecision,
					After.PendingDecisionRequests,
					Before.PendingDecisionRequests)
				&& AreStaleBagPendingReplacementsIdentical(
					After.PendingReplacements,
					Before.PendingReplacements));
		bValid &= Test.TestEqual(
			Label + TEXT(" appends one resolution"),
			After.Resolutions.Num(),
			Before.Resolutions.Num() + 1);
		bValid &= Test.TestEqual(
			Label + TEXT(" appends exactly two events"),
			After.Events.Num(),
			Before.Events.Num() + 2);
		bValid &= Test.TestEqual(
			Label + TEXT(" advances one resolution identity"),
			After.NextResolutionId,
			Before.NextResolutionId + 1);
		bValid &= Test.TestEqual(
			Label + TEXT(" advances two event identities"),
			After.NextEventOrdinal,
			Before.NextEventOrdinal + 2);
		bValid &= Test.TestTrue(
			Label + TEXT(" preserves prior resolution history as a prefix"),
			IsStaleBagResolutionHistoryPrefix(
				Before.Resolutions,
				After.Resolutions));
		bValid &= Test.TestTrue(
			Label + TEXT(" preserves prior event history as a prefix"),
			IsStaleBagEventHistoryPrefix(Before.Events, After.Events));
		bValid &= Test.TestTrue(
			Label + TEXT(" replays the accepted cancellation exactly once"),
			DoesStaleBagReplayContainResolutionExactlyOnce(
				Engine,
				Before,
				Returned));
		return bValid;
	}

	bool TryMakeCurrentBagTargetStale(
		FBattleEngine& Engine,
		const FBattlerId ReplacementTargetId)
	{
		FBattleEngineState& State =
			FBattleC08CBagEngineFixture::GetMutableState(Engine);
		if (!State.LockedActions.IsValidIndex(State.CurrentLockedActionIndex)
			|| State.LockedActions[State.CurrentLockedActionIndex]
				.Decision.GetActionKind() != EBattleActionKind::Bag)
		{
			return false;
		}
		State.LockedActions[State.CurrentLockedActionIndex]
			.SelectedTargetBattlerId = ReplacementTargetId;
		return true;
	}

	bool TrySetCompiledBagPermission(
		FBattleEngine& Engine,
		const FTrainerId TrainerId,
		const bool bMayUseBag)
	{
		FBattleEngineState& State =
			FBattleC08CBagEngineFixture::GetMutableState(Engine);
		for (const FBattleTrainerEncounterPolicy& ConstPolicy :
			State.CompiledEncounterPolicies.GetTrainerPolicies())
		{
			if (ConstPolicy.TrainerId == TrainerId)
			{
				FBattleTrainerEncounterPolicy& Policy =
					const_cast<FBattleTrainerEncounterPolicy&>(ConstPolicy);
				Policy.bMayUseBag = bMayUseBag;
				return true;
			}
		}
		return false;
	}

	bool TrySetBagItemCount(
		FBattleEngine& Engine,
		const FTrainerId TrainerId,
		const FItemId& ItemId,
		const int32 Count)
	{
		FBattleTrainerState* Trainer =
			FBattleC08CBagEngineFixture::GetMutableState(Engine)
				.FindMutableTrainer(TrainerId);
		FBattleBagItemCount* Item = Trainer != nullptr
			? Trainer->Bag.FindByPredicate(
				[&ItemId](const FBattleBagItemCount& Candidate)
				{
					return Candidate.ItemId == ItemId;
				})
			: nullptr;
		if (Item == nullptr || Count < 0)
		{
			return false;
		}
		Item->Count = Count;
		return true;
	}

	bool TryKeepOnlyCurrentBagAction(FBattleEngine& Engine)
	{
		FBattleEngineState& State =
			FBattleC08CBagEngineFixture::GetMutableState(Engine);
		if (!State.LockedActions.IsValidIndex(State.CurrentLockedActionIndex)
			|| State.LockedActions[State.CurrentLockedActionIndex]
				.Decision.GetActionKind() != EBattleActionKind::Bag)
		{
			return false;
		}
		State.LockedActions.SetNum(State.CurrentLockedActionIndex + 1);
		return true;
	}

	bool HasUnavailableItem(
		const FBattleDecisionRequest& Request,
		const FItemId& ItemId,
		const EBattleOptionUnavailableReason Reason)
	{
		return Request.GetUnavailableOptions().ContainsByPredicate(
			[&ItemId, Reason](const FBattleUnavailableDecisionOption& Option)
			{
				return Option.ItemId == ItemId
					&& Option.Reason == Reason;
			});
	}

	FBattleBagItemUseFacts MakeBaseFacts(const FItemId& ItemId)
	{
		FBattleBagItemUseFacts Facts;
		Facts.ItemId = ItemId;
		Facts.DefinitionKind = FBattleBagItemRules::GetExpectedDefinitionKind(
			FBattleBagItemRules::GetKind(ItemId));
		Facts.TargetKind = FBattleBagItemRules::GetTargetKind(
			FBattleBagItemRules::GetKind(ItemId));
		Facts.bActingTrainerMayUseBag = true;
		Facts.bActingTrainerMayCapture = true;
		Facts.bActingTrainerMayUseRevive = true;
		Facts.bTargetOwnedByActingTrainer = true;
		Facts.bTargetIsActingBattler = true;
		Facts.CurrentHP = 80;
		Facts.MaximumHP = 200;
		return Facts;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC08CBagCanonicalRulesTest,
		"PokemonSolarus.Battle.C08C.Bag.Rules.CanonicalIdsAndExactEffects",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FBattleC08CBagCanonicalRulesTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		const TArray<FItemId> Canonical = FBattleBagItemRules::GetCanonicalIds();
		TestEqual(TEXT("C08C owns exactly five Bag items"), Canonical.Num(), 5);
		if (Canonical.Num() == 5)
		{
			TestEqual(TEXT("Poke Ball uses the exact stable ID"),
				Canonical[0].GetDefinitionId().GetName(), FName(TEXT("Item.PokeBall")));
			TestEqual(TEXT("Hyper Potion uses the exact stable ID"),
				Canonical[1].GetDefinitionId().GetName(), FName(TEXT("Item.HyperPotion")));
			TestEqual(TEXT("Revive uses the exact stable ID"),
				Canonical[2].GetDefinitionId().GetName(), FName(TEXT("Item.Revive")));
			TestEqual(TEXT("Full Heal uses the exact stable ID"),
				Canonical[3].GetDefinitionId().GetName(), FName(TEXT("Item.FullHeal")));
			TestEqual(TEXT("X Attack uses the exact stable ID"),
				Canonical[4].GetDefinitionId().GetName(), FName(TEXT("Item.XAttack")));
		}

		FBattleBagItemUseResult Result;
		FBattleBagItemUseFacts Facts = MakeBaseFacts(
			FBattleBagItemRules::GetHyperPotionId());
		TestTrue(TEXT("Hyper Potion facts evaluate"),
			FBattleBagItemRules::TryEvaluateUse(Facts, Result));
		TestTrue(TEXT("An injured living owner target is legal"), Result.bLegal);
		TestEqual(TEXT("Hyper Potion heals exactly 120"), Result.HealAmount, 120);
		Facts.CurrentHP = 150;
		TestTrue(TEXT("Capped Hyper Potion facts evaluate"),
			FBattleBagItemRules::TryEvaluateUse(Facts, Result));
		TestEqual(TEXT("Hyper Potion caps at maximum HP"), Result.HealAmount, 50);
		Facts.CurrentHP = 200;
		TestTrue(TEXT("Full-HP Hyper Potion facts evaluate"),
			FBattleBagItemRules::TryEvaluateUse(Facts, Result));
		TestFalse(TEXT("Hyper Potion rejects full HP"), Result.bLegal);
		Facts.CurrentHP = 0;
		Facts.bTargetFainted = true;
		TestTrue(TEXT("Fainted Hyper Potion facts evaluate"),
			FBattleBagItemRules::TryEvaluateUse(Facts, Result));
		TestFalse(TEXT("Hyper Potion rejects fainted targets"), Result.bLegal);

		Facts = MakeBaseFacts(FBattleBagItemRules::GetReviveId());
		Facts.CurrentHP = 0;
		Facts.MaximumHP = 201;
		Facts.bTargetFainted = true;
		Facts.bTargetIsActingBattler = false;
		TestTrue(TEXT("Player Revive facts evaluate"),
			FBattleBagItemRules::TryEvaluateUse(Facts, Result));
		TestTrue(TEXT("Player can Revive an owned fainted target"), Result.bLegal);
		TestEqual(TEXT("Revive uses floor of half maximum HP"), Result.HealAmount, 100);
		Facts.MaximumHP = 1;
		TestTrue(TEXT("One-HP Revive facts evaluate"),
			FBattleBagItemRules::TryEvaluateUse(Facts, Result));
		TestEqual(TEXT("Revive restores at least one HP"), Result.HealAmount, 1);
		TestTrue(TEXT("Partner Revive facts evaluate"),
			FBattleBagItemRules::TryEvaluateUse(Facts, Result));
		TestTrue(TEXT("A partner can Revive its own target"), Result.bLegal);
		Facts.bActingTrainerMayUseRevive = false;
		TestTrue(TEXT("Opponent Revive facts evaluate"),
			FBattleBagItemRules::TryEvaluateUse(Facts, Result));
		TestFalse(TEXT("An ordinary opponent can never Revive"), Result.bLegal);

		Facts = MakeBaseFacts(FBattleBagItemRules::GetFullHealId());
		Facts.bHasCanonicalMajorStatus = true;
		Facts.bHasConfusion = true;
		TestTrue(TEXT("Full Heal facts evaluate"),
			FBattleBagItemRules::TryEvaluateUse(Facts, Result));
		TestTrue(TEXT("Full Heal cures a canonical major status"),
			Result.bCuresMajorStatus);
		TestTrue(TEXT("Full Heal cures Confusion"), Result.bCuresConfusion);
		TestEqual(TEXT("The canonical major-status family has six members"),
			FBattleMajorStatusRules::GetCanonicalIds().Num(), 6);

		Facts = MakeBaseFacts(FBattleBagItemRules::GetXAttackId());
		Facts.AttackStage = 5;
		TestTrue(TEXT("Near-cap X Attack facts evaluate"),
			FBattleBagItemRules::TryEvaluateUse(Facts, Result));
		TestTrue(TEXT("X Attack is legal below the cap"), Result.bLegal);
		TestEqual(TEXT("X Attack requests two stages"),
			Result.RequestedAttackStageDelta, 2);
		TestEqual(TEXT("X Attack applies only one stage at plus five"),
			Result.AppliedAttackStageDelta, 1);
		TestEqual(TEXT("X Attack caps at plus six"), Result.ResultingAttackStage, 6);
		Facts.AttackStage = 6;
		TestTrue(TEXT("At-cap X Attack facts evaluate"),
			FBattleBagItemRules::TryEvaluateUse(Facts, Result));
		TestFalse(TEXT("X Attack rejects plus six"), Result.bLegal);

		Facts = MakeBaseFacts(FBattleBagItemRules::GetPokeBallId());
		Facts.bTargetOwnedByActingTrainer = false;
		Facts.bTargetIsActingBattler = false;
		Facts.bTargetIsOpposingActive = true;
		TestTrue(TEXT("Poke Ball handoff facts evaluate"),
			FBattleBagItemRules::TryEvaluateUse(Facts, Result));
		TestTrue(TEXT("A legal Poke Ball request reaches the handoff"),
			Result.bLegal && Result.bCaptureHandoff);
		Facts.bActingTrainerMayCapture = false;
		TestTrue(TEXT("Partner Poke Ball facts evaluate"),
			FBattleBagItemRules::TryEvaluateUse(Facts, Result));
		TestFalse(TEXT("A partner cannot capture"), Result.bLegal);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleADR00023B2ExplicitBagPermissionsTest,
		"PokemonSolarus.Battle.ADR0002.3B2.RuntimeAuthority.Bag.ExplicitCompiledPermissions",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

	bool FBattleADR00023B2ExplicitBagPermissionsTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		FBattleBagItemUseResult Result;
		FBattleBagItemUseFacts Facts = MakeBaseFacts(
			FBattleBagItemRules::GetHyperPotionId());
		Facts.bActingTrainerMayUseBag = false;
		TestTrue(TEXT("A denied Bag permission is a valid rule input"),
			FBattleBagItemRules::TryEvaluateUse(Facts, Result));
		TestFalse(TEXT("General item use obeys explicit Bag permission"), Result.bLegal);

		Facts = MakeBaseFacts(FBattleBagItemRules::GetPokeBallId());
		Facts.bTargetOwnedByActingTrainer = false;
		Facts.bTargetIsActingBattler = false;
		Facts.bTargetIsOpposingActive = true;
		Facts.bActingTrainerMayCapture = false;
		TestTrue(TEXT("A denied Capture permission is a valid rule input"),
			FBattleBagItemRules::TryEvaluateUse(Facts, Result));
		TestFalse(TEXT("Poke Ball obeys explicit Capture permission"), Result.bLegal);

		Facts = MakeBaseFacts(FBattleBagItemRules::GetReviveId());
		Facts.CurrentHP = 0;
		Facts.bTargetFainted = true;
		Facts.bTargetIsActingBattler = false;
		Facts.bActingTrainerMayUseRevive = false;
		TestTrue(TEXT("A denied Revive permission is a valid rule input"),
			FBattleBagItemRules::TryEvaluateUse(Facts, Result));
		TestFalse(TEXT("Revive obeys explicit Revive permission"), Result.bLegal);
		Facts.bActingTrainerMayUseRevive = true;
		TestTrue(TEXT("An admitted Revive permission is evaluated"),
			FBattleBagItemRules::TryEvaluateUse(Facts, Result));
		TestTrue(TEXT("Admitted Revive remains legal for its owned fainted target"),
			Result.bLegal);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC08CBagSelectionTest,
		"PokemonSolarus.Battle.C08C.Bag.Selection.ExactTargetsAndOwnerPolicy",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FBattleC08CBagSelectionTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		FBagScenario Scenario;
		Scenario.PlayerHP = 50;
		Scenario.PlayerReserveHP = 0;
		Scenario.OpponentHP = 80;
		Scenario.OpponentReserveHP = 0;
		Scenario.PlayerBag = {
			{FBattleBagItemRules::GetPokeBallId(), 1},
			{FBattleBagItemRules::GetHyperPotionId(), 1},
			{FBattleBagItemRules::GetReviveId(), 1},
			{FBattleBagItemRules::GetFullHealId(), 1},
			{FBattleBagItemRules::GetXAttackId(), 1}};
		Scenario.OpponentBag = {
			{FBattleBagItemRules::GetHyperPotionId(), 1},
			{FBattleBagItemRules::GetReviveId(), 1}};
		TUniquePtr<FBattleEngine> Engine = MakeEngine(Scenario);
		TestTrue(TEXT("Selection runtime begins"), BeginRuntime(*Engine));
		const FBattleDecisionRequest PlayerRequest =
			Engine->GetPendingDecision().GetValue();
		const TConstArrayView<FBattleItemPartyTargetOption> PlayerPartyPairs =
			PlayerRequest.GetLegalItemPartyTargets();
		const TConstArrayView<FBattleItemActiveTargetOption> PlayerActivePairs =
			PlayerRequest.GetLegalItemActiveTargets();
		TestEqual(TEXT("The player request returns exactly two item/party pairs"),
			PlayerPartyPairs.Num(),
			2);
		if (PlayerPartyPairs.Num() == 2)
		{
			TestTrue(TEXT("The first exact party pair is Hyper Potion to slot zero"),
				PlayerPartyPairs[0].ItemId == FBattleBagItemRules::GetHyperPotionId()
					&& PlayerPartyPairs[0].PartySlotId == MakePartySlotId(0));
			TestTrue(TEXT("The second exact party pair is Revive to slot one"),
				PlayerPartyPairs[1].ItemId == FBattleBagItemRules::GetReviveId()
					&& PlayerPartyPairs[1].PartySlotId == MakePartySlotId(1));
		}
		TestEqual(TEXT("The player request returns exactly one item/active pair"),
			PlayerActivePairs.Num(),
			1);
		if (PlayerActivePairs.Num() == 1)
		{
			TestTrue(TEXT("The only exact active pair is X Attack to the acting slot"),
				PlayerActivePairs[0].ItemId == FBattleBagItemRules::GetXAttackId()
					&& PlayerActivePairs[0].ActiveSlotId == MakeActiveSlotId(
						EBattleSide::Player,
						EBattlePosition::Left));
		}
		TestTrue(TEXT("Hyper Potion pairs only with the injured player party slot"),
			PlayerRequest.GetLegalItemPartyTargets().ContainsByPredicate(
				[](const FBattleItemPartyTargetOption& Option)
				{
					return Option.ItemId == FBattleBagItemRules::GetHyperPotionId()
						&& Option.PartySlotId == MakePartySlotId(0);
				}));
		TestTrue(TEXT("Revive pairs only with the fainted player reserve"),
			PlayerRequest.GetLegalItemPartyTargets().ContainsByPredicate(
				[](const FBattleItemPartyTargetOption& Option)
				{
					return Option.ItemId == FBattleBagItemRules::GetReviveId()
						&& Option.PartySlotId == MakePartySlotId(1);
				}));
		TestTrue(TEXT("X Attack pairs with the acting active battler"),
			PlayerRequest.GetLegalItemActiveTargets().ContainsByPredicate(
				[](const FBattleItemActiveTargetOption& Option)
				{
					return Option.ItemId == FBattleBagItemRules::GetXAttackId()
						&& Option.ActiveSlotId == MakeActiveSlotId(
							EBattleSide::Player,
							EBattlePosition::Left);
				}));
		TestTrue(TEXT("Trainer battle marks Poke Ball capture-restricted"),
			HasUnavailableItem(
				PlayerRequest,
				FBattleBagItemRules::GetPokeBallId(),
				EBattleOptionUnavailableReason::CaptureRestricted));
		TestTrue(TEXT("Full Heal with no curable condition has no legal target"),
			HasUnavailableItem(
				PlayerRequest,
				FBattleBagItemRules::GetFullHealId(),
				EBattleOptionUnavailableReason::NoLegalTarget));

		FBattleDecision WrongShape;
		TestTrue(TEXT("A syntactically valid cross-family target can be constructed"),
			FBattleDecision::TryCreateBag(
				PlayerRequest.GetStateVersion(),
				PlayerRequest.GetDecisionOwnerTrainerId(),
				PlayerRequest.GetActingBattlerId(),
				FBattleBagItemRules::GetHyperPotionId(),
				FPartySlotId(),
				MakeActiveSlotId(EBattleSide::Player, EBattlePosition::Left),
				WrongShape));
		const FBattleResolution WrongShapeResult = Engine->SubmitDecision(WrongShape);
		TestFalse(TEXT("The engine rejects a Hyper Potion active-target payload"),
			WrongShapeResult.WasAccepted());
		TestEqual(TEXT("The rejection identifies the illegal target pairing"),
			WrongShapeResult.GetRejection().Reason,
			EBattleRejectionReason::IllegalTarget);
		TestTrue(TEXT("The player can answer with a legal Fight after rejection"),
			SubmitFight(*Engine));

		const FBattleDecisionRequest OpponentRequest =
			Engine->GetPendingDecision().GetValue();
		const TConstArrayView<FBattleItemPartyTargetOption> OpponentPartyPairs =
			OpponentRequest.GetLegalItemPartyTargets();
		const TConstArrayView<FBattleItemActiveTargetOption> OpponentActivePairs =
			OpponentRequest.GetLegalItemActiveTargets();
		TestEqual(TEXT("The opponent request returns exactly one item/party pair"),
			OpponentPartyPairs.Num(),
			1);
		if (OpponentPartyPairs.Num() == 1)
		{
			TestTrue(TEXT("The only opponent pair is its Hyper Potion to slot zero"),
				OpponentPartyPairs[0].ItemId == FBattleBagItemRules::GetHyperPotionId()
					&& OpponentPartyPairs[0].PartySlotId == MakePartySlotId(0));
		}
		TestEqual(TEXT("The opponent request returns no item/active pairs"),
			OpponentActivePairs.Num(),
			0);
		TestTrue(TEXT("Opponent Hyper Potion targets only its own injured active"),
			OpponentRequest.GetLegalItemPartyTargets().ContainsByPredicate(
				[](const FBattleItemPartyTargetOption& Option)
				{
					return Option.ItemId == FBattleBagItemRules::GetHyperPotionId()
						&& Option.PartySlotId == MakePartySlotId(0);
				}));
		TestTrue(TEXT("Ordinary opponent Revive has no legal target by policy"),
			HasUnavailableItem(
				OpponentRequest,
				FBattleBagItemRules::GetReviveId(),
				EBattleOptionUnavailableReason::NoLegalTarget));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC08CBagHyperPotionTest,
		"PokemonSolarus.Battle.C08C.Bag.Engine.HyperPotionConsumeHealAndStale",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FBattleC08CBagHyperPotionTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		const FItemId HyperPotionId = FBattleBagItemRules::GetHyperPotionId();
		const FTrainerId PlayerTrainerId = MakeNumericId<FTrainerId>(PlayerTrainerValue);
		const FBattlerId PlayerId = MakeNumericId<FBattlerId>(PlayerBattlerValue);
		FBagScenario Scenario;
		Scenario.PlayerHP = 50;
		Scenario.PlayerBag = {{HyperPotionId, 2}};
		TUniquePtr<FBattleEngine> Engine = MakeEngine(Scenario);
		TestTrue(TEXT("A legal Hyper Potion action locks"),
			LockPlayerBagAndOpponentFight(
				*Engine,
				HyperPotionId,
				MakePartySlotId(0)));
		TestTrue(TEXT("The Hyper Potion action starts"),
			Engine->BeginNextLockedAction().WasAccepted());
		const FBattleResolution Applied = Engine->ExecuteCurrentBagItem();
		TestTrue(TEXT("The Hyper Potion action resolves"), Applied.WasAccepted());
		TestEqual(TEXT("Hyper Potion heals exactly 120 in the live engine"),
			FBattleC08CBagEngineFixture::GetBattler(*Engine, PlayerId)->CurrentHP,
			170);
		TestEqual(TEXT("A legal use consumes exactly one item"),
			GetBagCount(*Engine, PlayerTrainerId, HyperPotionId),
			1);
		const FBattleTrainerState* Trainer =
			FBattleC08CBagEngineFixture::GetTrainer(*Engine, PlayerTrainerId);
		TestTrue(TEXT("A legal use consumes the per-turn Bag quota"),
			Trainer != nullptr && !Trainer->ActionAllowance.bBagActionAvailable);
		const int32 ConsumedIndex = FindEvent(Applied, EBattleEventType::ItemConsumed);
		const int32 HealingIndex = FindEvent(Applied, EBattleEventType::Healing);
		TestTrue(TEXT("Consumption is recorded before healing mutation"),
			ConsumedIndex != INDEX_NONE
				&& HealingIndex != INDEX_NONE
				&& ConsumedIndex < HealingIndex);

		TUniquePtr<FBattleEngine> StaleEngine = MakeEngine(Scenario, 8084);
		TestTrue(TEXT("A second Hyper Potion action locks while legal"),
			LockPlayerBagAndOpponentFight(
				*StaleEngine,
				HyperPotionId,
				MakePartySlotId(0)));
		TestTrue(TEXT("The stale candidate action starts"),
			StaleEngine->BeginNextLockedAction().WasAccepted());
		TestTrue(TEXT("The target becomes full before mutation"),
			FBattleC08CBagEngineFixture::SetCurrentHP(*StaleEngine, PlayerId, 200));
		const int32 TraceBefore = StaleEngine->ExportRandomTrace().Num();
		const FBattleResolution Stale = StaleEngine->ExecuteCurrentBagItem();
		TestTrue(TEXT("A stale committed Bag action cancels deterministically"),
			Stale.WasAccepted()
				&& FindEvent(Stale, EBattleEventType::ActionCanceled) != INDEX_NONE);
		TestEqual(TEXT("Stale cancellation consumes no item"),
			GetBagCount(*StaleEngine, PlayerTrainerId, HyperPotionId),
			2);
		Trainer = FBattleC08CBagEngineFixture::GetTrainer(*StaleEngine, PlayerTrainerId);
		TestTrue(TEXT("Stale cancellation consumes no Bag quota"),
			Trainer != nullptr && Trainer->ActionAllowance.bBagActionAvailable);
		TestEqual(TEXT("Stale cancellation consumes no RNG"),
			StaleEngine->ExportRandomTrace().Num(),
			TraceBefore);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC08CBagReviveTest,
		"PokemonSolarus.Battle.C08C.Bag.Engine.ReviveLifecycleAndNoActionGrant",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FBattleC08CBagReviveTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		const FItemId ReviveId = FBattleBagItemRules::GetReviveId();
		const FTrainerId PlayerTrainerId = MakeNumericId<FTrainerId>(PlayerTrainerValue);
		const FBattlerId ReserveId = MakeNumericId<FBattlerId>(PlayerReserveValue);
		FBagScenario Scenario;
		Scenario.PlayerReserveHP = 0;
		Scenario.PlayerBag = {{ReviveId, 1}};
		TUniquePtr<FBattleEngine> Engine = MakeEngine(Scenario);
		TestTrue(TEXT("Revive locks onto the fainted owned reserve"),
			LockPlayerBagAndOpponentFight(
				*Engine,
				ReviveId,
				MakePartySlotId(1)));
		TestTrue(TEXT("The Revive action starts"),
			Engine->BeginNextLockedAction().WasAccepted());
		const FBattleResolution Resolution = Engine->ExecuteCurrentBagItem();
		TestTrue(TEXT("Revive resolves"), Resolution.WasAccepted());
		const FBattleBattlerState* Reserve =
			FBattleC08CBagEngineFixture::GetBattler(*Engine, ReserveId);
		TestTrue(TEXT("Revive clears the fainted lifecycle state"),
			Reserve != nullptr
				&& !Reserve->bFainted
				&& !Reserve->bFaintTransitionPending
				&& !Reserve->bRemoved);
		TestEqual(TEXT("Revive restores floor half of maximum HP"),
			Reserve != nullptr ? Reserve->CurrentHP : INDEX_NONE,
			100);
		TestEqual(TEXT("Revive consumes its item"),
			GetBagCount(*Engine, PlayerTrainerId, ReviveId),
			0);
		const FBattleTrainerState* Trainer =
			FBattleC08CBagEngineFixture::GetTrainer(*Engine, PlayerTrainerId);
		TestTrue(TEXT("The revived reserve receives no current-turn action"),
			Trainer != nullptr
				&& Trainer->ActionAllowance.MaximumActions == 1
				&& Trainer->ActionAllowance.RemainingActions == 0);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC08CBagFullHealTest,
		"PokemonSolarus.Battle.C08C.Bag.Engine.FullHealMajorConfusionToxicCleanup",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FBattleC08CBagFullHealTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		const FItemId FullHealId = FBattleBagItemRules::GetFullHealId();
		const FBattlerId PlayerId = MakeNumericId<FBattlerId>(PlayerBattlerValue);
		const FBattlerId OpponentId = MakeNumericId<FBattlerId>(OpponentBattlerValue);
		const TArray<FConditionId> Statuses = FBattleMajorStatusRules::GetCanonicalIds();
		TestEqual(TEXT("The live cleanup proof covers all six major statuses"),
			Statuses.Num(),
			6);
		for (int32 Index = 0; Index < Statuses.Num(); ++Index)
		{
			FBagScenario Scenario;
			Scenario.PlayerBag = {{FullHealId, 1}};
			TUniquePtr<FBattleEngine> Engine = MakeEngine(
				Scenario,
				static_cast<uint64>(8090 + Index));
			TestTrue(TEXT("A canonical major status is seeded"),
				FBattleC08CBagEngineFixture::AddMajorStatus(
					*Engine,
					PlayerId,
					Statuses[Index]));
			const bool bToxic = Statuses[Index] == FBattleMajorStatusRules::GetToxicId();
			if (bToxic)
			{
				TestTrue(TEXT("Toxic's private counter is advanced"),
					FBattleC08CBagEngineFixture::SetConditionRegistrationLayers(
						*Engine,
						PlayerId,
						Statuses[Index],
						6));
				TestTrue(TEXT("Confusion is seeded beside Toxic"),
					FBattleC08CBagEngineFixture::AddVolatile(
						*Engine,
						PlayerId,
						FBattleVolatileRules::GetConfusionId(),
						OpponentId));
				TestTrue(TEXT("An unrelated Substitute is seeded"),
					FBattleC08CBagEngineFixture::AddVolatile(
						*Engine,
						PlayerId,
						FBattleVolatileRules::GetSubstituteId(),
						PlayerId,
						50));
			}
			TestTrue(TEXT("Full Heal locks for a curable target"),
				LockPlayerBagAndOpponentFight(
					*Engine,
					FullHealId,
					MakePartySlotId(0)));
			TestTrue(TEXT("Full Heal starts"),
				Engine->BeginNextLockedAction().WasAccepted());
			TestTrue(TEXT("Full Heal resolves"),
				Engine->ExecuteCurrentBagItem().WasAccepted());
			const FBattleBattlerState* Player =
				FBattleC08CBagEngineFixture::GetBattler(*Engine, PlayerId);
			TestTrue(TEXT("The canonical major status is cleared"),
				Player != nullptr && !Player->MajorStatusId.IsValid());
			TestEqual(TEXT("The status-owned private registrations are removed"),
				FBattleC08CBagEngineFixture::CountConditionRegistrations(
					*Engine,
					PlayerId,
					Statuses[Index]),
				0);
			if (bToxic)
			{
				TestFalse(TEXT("Full Heal clears Confusion"),
					FBattleC08CBagEngineFixture::HasVolatile(
						*Engine,
						PlayerId,
						FBattleVolatileRules::GetConfusionId()));
				TestEqual(TEXT("Confusion's private registration is removed"),
					FBattleC08CBagEngineFixture::CountConditionRegistrations(
						*Engine,
						PlayerId,
						FBattleVolatileRules::GetConfusionId()),
					0);
				TestTrue(TEXT("Full Heal preserves unrelated volatiles"),
					FBattleC08CBagEngineFixture::HasVolatile(
						*Engine,
						PlayerId,
						FBattleVolatileRules::GetSubstituteId()));
				TestTrue(TEXT("The unrelated volatile registration remains"),
					FBattleC08CBagEngineFixture::CountConditionRegistrations(
						*Engine,
						PlayerId,
						FBattleVolatileRules::GetSubstituteId()) > 0);
			}
		}
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleADR00023D2FullHealAtomicCleanupTest,
		"PokemonSolarus.Battle.ADR0002.3D2.Bag.FullHeal.SuccessAndPreparationFailure",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FBattleADR00023D2FullHealAtomicCleanupTest::RunTest(
		const FString& Parameters)
	{
		(void)Parameters;
		const FItemId FullHealId = FBattleBagItemRules::GetFullHealId();
		const FConditionId ToxicId = FBattleMajorStatusRules::GetToxicId();
		const FConditionId ConfusionId = FBattleVolatileRules::GetConfusionId();
		const FConditionId SubstituteId = FBattleVolatileRules::GetSubstituteId();
		const FTrainerId PlayerTrainerId =
			MakeNumericId<FTrainerId>(PlayerTrainerValue);
		const FBattlerId PlayerId = MakeNumericId<FBattlerId>(PlayerBattlerValue);
		const FBattlerId OpponentId =
			MakeNumericId<FBattlerId>(OpponentBattlerValue);

		auto MakePreparedEngine = [&](const uint64 Seed)
		{
			FBagScenario Scenario;
			Scenario.PlayerBag = {{FullHealId, 1}};
			TUniquePtr<FBattleEngine> Engine = MakeEngine(Scenario, Seed);
			const bool bPrepared =
				FBattleC08CBagEngineFixture::AddMajorStatus(
					*Engine,
					PlayerId,
					ToxicId)
				&& FBattleC08CBagEngineFixture::SetConditionRegistrationLayers(
					*Engine,
					PlayerId,
					ToxicId,
					6)
				&& FBattleC08CBagEngineFixture::AddVolatile(
					*Engine,
					PlayerId,
					ConfusionId,
					OpponentId)
				&& FBattleC08CBagEngineFixture::AddVolatile(
					*Engine,
					PlayerId,
					SubstituteId,
					PlayerId,
					50)
				&& LockPlayerBagAndOpponentFight(
					*Engine,
					FullHealId,
					MakePartySlotId(0))
				&& Engine->BeginNextLockedAction().WasAccepted();
			return TPair<TUniquePtr<FBattleEngine>, bool>(MoveTemp(Engine), bPrepared);
		};

		auto SuccessPair = MakePreparedEngine(8130);
		TUniquePtr<FBattleEngine> Success = MoveTemp(SuccessPair.Key);
		if (!TestTrue(TEXT("Successful Full Heal fixture is prepared"), SuccessPair.Value))
		{
			return false;
		}
		const int32 SuccessResolutionCountBefore =
			FBattleC08CBagEngineFixture::GetResolutionCount(*Success);
		const FBattleResolution Applied = Success->ExecuteCurrentBagItem();
		TestTrue(TEXT("Full Heal succeeds after private cleanup is staged"),
			Applied.WasAccepted());
		TestTrue(TEXT("Successful Full Heal returns the exact appended resolution once"),
			IsReturnedResolutionAppendedExactlyOnce(*Success, Applied));
		TestEqual(TEXT("Successful Full Heal appends one resolution"),
			FBattleC08CBagEngineFixture::GetResolutionCount(*Success),
			SuccessResolutionCountBefore + 1);
		TestTrue(TEXT("Successful Full Heal preserves exact public event order"),
			HasExactEventOrder(Applied, {
				EBattleEventType::ItemUsed,
				EBattleEventType::ItemConsumed,
				EBattleEventType::StatusChanged,
				EBattleEventType::ActionCompleted}));
		const FBattleBattlerState* SuccessfulPlayer =
			FBattleC08CBagEngineFixture::GetBattler(*Success, PlayerId);
		TestTrue(TEXT("Successful Full Heal clears Toxic and Confusion"),
			SuccessfulPlayer != nullptr
				&& !SuccessfulPlayer->MajorStatusId.IsValid()
				&& !FBattleC08CBagEngineFixture::HasVolatile(
					*Success,
					PlayerId,
					ConfusionId));
		TestTrue(TEXT("Successful Full Heal preserves unrelated Substitute"),
			FBattleC08CBagEngineFixture::HasVolatile(
				*Success,
				PlayerId,
				SubstituteId));
		TestEqual(TEXT("Successful Full Heal clears Toxic's private registration"),
			FBattleC08CBagEngineFixture::CountConditionRegistrations(
				*Success,
				PlayerId,
				ToxicId),
			0);
		TestEqual(TEXT("Successful Full Heal clears Confusion's registration"),
			FBattleC08CBagEngineFixture::CountConditionRegistrations(
				*Success,
				PlayerId,
				ConfusionId),
			0);
		TestTrue(TEXT("Successful Full Heal preserves Substitute's registration"),
			FBattleC08CBagEngineFixture::CountConditionRegistrations(
				*Success,
				PlayerId,
				SubstituteId) > 0);
		TestEqual(TEXT("Successful Full Heal consumes exactly one item"),
			GetBagCount(*Success, PlayerTrainerId, FullHealId),
			0);
		const FBattleTrainerState* SuccessfulTrainer =
			FBattleC08CBagEngineFixture::GetTrainer(*Success, PlayerTrainerId);
		TestTrue(TEXT("Successful Full Heal consumes the Trainer Bag quota"),
			SuccessfulTrainer != nullptr
				&& !SuccessfulTrainer->ActionAllowance.bBagActionAvailable);

		auto FailurePair = MakePreparedEngine(8131);
		TUniquePtr<FBattleEngine> Failure = MoveTemp(FailurePair.Key);
		if (!TestTrue(TEXT("Failing Full Heal fixture is prepared"), FailurePair.Value))
		{
			return false;
		}
		FBattleC08CBagEngineFixture::SetNextTriggerReentrancyToken(
			*Failure,
			TNumericLimits<uint64>::Max());
		const uint64 StateVersionBefore =
			FBattleC08CBagEngineFixture::GetStateVersion(*Failure);
		const uint64 NextResolutionBefore =
			FBattleC08CBagEngineFixture::GetNextResolutionValue(*Failure);
		const uint64 NextEventBefore =
			FBattleC08CBagEngineFixture::GetNextEventOrdinal(*Failure);
		const uint64 TriggerTokenBefore =
			FBattleC08CBagEngineFixture::GetNextTriggerReentrancyToken(*Failure);
		const int32 CursorBefore =
			FBattleC08CBagEngineFixture::GetCurrentLockedActionIndex(*Failure);
		const int32 ResolutionCountBefore =
			FBattleC08CBagEngineFixture::GetResolutionCount(*Failure);
		const int32 EventCountBefore =
			FBattleC08CBagEngineFixture::GetOrderedEventCount(*Failure);
		const int32 RegistrationCountBefore =
			FBattleC08CBagEngineFixture::GetActiveTriggerRegistrationCount(*Failure);
		const int32 DispatchCountBefore =
			FBattleC08CBagEngineFixture::GetPendingTriggerDispatchCount(*Failure);
		const int32 EffectRequestCountBefore =
			FBattleC08CBagEngineFixture::GetPendingTriggerEffectRequestCount(*Failure);
		const int32 ToxicRegistrationsBefore =
			FBattleC08CBagEngineFixture::CountConditionRegistrations(
				*Failure,
				PlayerId,
				ToxicId);
		const int32 ConfusionRegistrationsBefore =
			FBattleC08CBagEngineFixture::CountConditionRegistrations(
				*Failure,
				PlayerId,
				ConfusionId);
		const int32 SubstituteRegistrationsBefore =
			FBattleC08CBagEngineFixture::CountConditionRegistrations(
				*Failure,
				PlayerId,
				SubstituteId);
		const int32 RandomTraceBefore = Failure->ExportRandomTrace().Num();

		const FBattleResolution Rejected = Failure->ExecuteCurrentBagItem();
		TestFalse(TEXT("Injected Full Heal cleanup failure is rejected"),
			Rejected.WasAccepted());
		TestEqual(TEXT("Injected cleanup failure has the typed preparation reason"),
			Rejected.GetRejection().Reason,
			EBattleRejectionReason::CheckpointPreparationFailed);
		TestTrue(TEXT("Rejected Full Heal returns the exact appended resolution once"),
			IsReturnedResolutionAppendedExactlyOnce(*Failure, Rejected));
		TestTrue(TEXT("Rejected Full Heal publishes exactly ActionCanceled"),
			HasExactEventOrder(Rejected, {EBattleEventType::ActionCanceled}));
		TestFalse(TEXT("Rejected Full Heal publishes no ItemUsed fact"),
			FindEvent(Rejected, EBattleEventType::ItemUsed) != INDEX_NONE);
		TestFalse(TEXT("Rejected Full Heal publishes no ItemConsumed fact"),
			FindEvent(Rejected, EBattleEventType::ItemConsumed) != INDEX_NONE);
		TestFalse(TEXT("Rejected Full Heal publishes no StatusChanged fact"),
			FindEvent(Rejected, EBattleEventType::StatusChanged) != INDEX_NONE);
		TestFalse(TEXT("Rejected Full Heal publishes no ActionCompleted fact"),
			FindEvent(Rejected, EBattleEventType::ActionCompleted) != INDEX_NONE);
		TestEqual(TEXT("Rejected Full Heal appends exactly one resolution"),
			FBattleC08CBagEngineFixture::GetResolutionCount(*Failure),
			ResolutionCountBefore + 1);
		TestEqual(TEXT("Rejected Full Heal appends exactly one event"),
			FBattleC08CBagEngineFixture::GetOrderedEventCount(*Failure),
			EventCountBefore + 1);
		TestEqual(TEXT("Rejected Full Heal consumes one invocation resolution ID"),
			FBattleC08CBagEngineFixture::GetNextResolutionValue(*Failure),
			NextResolutionBefore + 1);
		TestEqual(TEXT("Rejected Full Heal consumes one event ordinal"),
			FBattleC08CBagEngineFixture::GetNextEventOrdinal(*Failure),
			NextEventBefore + 1);
		TestEqual(TEXT("Rejected Full Heal does not advance state version"),
			FBattleC08CBagEngineFixture::GetStateVersion(*Failure),
			StateVersionBefore);
		TestEqual(TEXT("Rejected Full Heal does not advance the action cursor"),
			FBattleC08CBagEngineFixture::GetCurrentLockedActionIndex(*Failure),
			CursorBefore);
		TestTrue(TEXT("Rejected Full Heal leaves the action started and unfinished"),
			FBattleC08CBagEngineFixture::IsCurrentActionStartedAndUnfinished(*Failure));
		TestEqual(TEXT("Rejected Full Heal consumes no item"),
			GetBagCount(*Failure, PlayerTrainerId, FullHealId),
			1);
		const FBattleTrainerState* FailedTrainer =
			FBattleC08CBagEngineFixture::GetTrainer(*Failure, PlayerTrainerId);
		TestTrue(TEXT("Rejected Full Heal consumes no Trainer Bag quota"),
			FailedTrainer != nullptr
				&& FailedTrainer->ActionAllowance.bBagActionAvailable);
		const FBattleBattlerState* FailedPlayer =
			FBattleC08CBagEngineFixture::GetBattler(*Failure, PlayerId);
		TestTrue(TEXT("Rejected Full Heal preserves Toxic target state"),
			FailedPlayer != nullptr && FailedPlayer->MajorStatusId == ToxicId);
		TestTrue(TEXT("Rejected Full Heal preserves Confusion target state"),
			FBattleC08CBagEngineFixture::HasVolatile(
				*Failure,
				PlayerId,
				ConfusionId));
		TestTrue(TEXT("Rejected Full Heal preserves unrelated volatile state"),
			FBattleC08CBagEngineFixture::HasVolatile(
				*Failure,
				PlayerId,
				SubstituteId));
		TestEqual(TEXT("Rejected Full Heal preserves all trigger registrations"),
			FBattleC08CBagEngineFixture::GetActiveTriggerRegistrationCount(*Failure),
			RegistrationCountBefore);
		TestEqual(TEXT("Rejected Full Heal preserves Toxic trigger state"),
			FBattleC08CBagEngineFixture::CountConditionRegistrations(
				*Failure,
				PlayerId,
				ToxicId),
			ToxicRegistrationsBefore);
		TestEqual(TEXT("Rejected Full Heal preserves Confusion trigger state"),
			FBattleC08CBagEngineFixture::CountConditionRegistrations(
				*Failure,
				PlayerId,
				ConfusionId),
			ConfusionRegistrationsBefore);
		TestEqual(TEXT("Rejected Full Heal preserves unrelated trigger state"),
			FBattleC08CBagEngineFixture::CountConditionRegistrations(
				*Failure,
				PlayerId,
				SubstituteId),
			SubstituteRegistrationsBefore);
		TestEqual(TEXT("Rejected Full Heal preserves pending trigger dispatches"),
			FBattleC08CBagEngineFixture::GetPendingTriggerDispatchCount(*Failure),
			DispatchCountBefore);
		TestEqual(TEXT("Rejected Full Heal preserves pending trigger effects"),
			FBattleC08CBagEngineFixture::GetPendingTriggerEffectRequestCount(*Failure),
			EffectRequestCountBefore);
		TestEqual(TEXT("Rejected Full Heal preserves the trigger token"),
			FBattleC08CBagEngineFixture::GetNextTriggerReentrancyToken(*Failure),
			TriggerTokenBefore);
		TestEqual(TEXT("Rejected Full Heal consumes no RNG"),
			Failure->ExportRandomTrace().Num(),
			RandomTraceBefore);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleADR00023D2PrevalidatedBagTransitionsTest,
		"PokemonSolarus.Battle.ADR0002.3D2.Bag.PrevalidatedTransitions.ExactOncePublication",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FBattleADR00023D2PrevalidatedBagTransitionsTest::RunTest(
		const FString& Parameters)
	{
		(void)Parameters;
		const FTrainerId PlayerTrainerId =
			MakeNumericId<FTrainerId>(PlayerTrainerValue);
		const FBattlerId PlayerId = MakeNumericId<FBattlerId>(PlayerBattlerValue);
		const FBattlerId ReserveId = MakeNumericId<FBattlerId>(PlayerReserveValue);

		const FItemId HyperPotionId = FBattleBagItemRules::GetHyperPotionId();
		FBagScenario HyperScenario;
		HyperScenario.PlayerHP = 50;
		HyperScenario.PlayerBag = {{HyperPotionId, 1}};
		TUniquePtr<FBattleEngine> Hyper = MakeEngine(HyperScenario, 8132);
		if (!TestTrue(TEXT("Hyper Potion transition locks"),
			LockPlayerBagAndOpponentFight(
				*Hyper,
				HyperPotionId,
				MakePartySlotId(0)))
			|| !TestTrue(TEXT("Hyper Potion transition starts"),
				Hyper->BeginNextLockedAction().WasAccepted()))
		{
			return false;
		}
		const FBattleResolution HyperResolution = Hyper->ExecuteCurrentBagItem();
		TestTrue(TEXT("Hyper Potion remains accepted"), HyperResolution.WasAccepted());
		TestTrue(TEXT("Hyper Potion returns the exact appended resolution once"),
			IsReturnedResolutionAppendedExactlyOnce(*Hyper, HyperResolution));
		TestTrue(TEXT("Hyper Potion preserves exact event order"),
			HasExactEventOrder(HyperResolution, {
				EBattleEventType::ItemUsed,
				EBattleEventType::ItemConsumed,
				EBattleEventType::Healing,
				EBattleEventType::HPChanged,
				EBattleEventType::ActionCompleted}));
		TestEqual(TEXT("Hyper Potion still heals exactly 120"),
			FBattleC08CBagEngineFixture::GetBattler(*Hyper, PlayerId)->CurrentHP,
			170);
		TestEqual(TEXT("Hyper Potion still consumes one item"),
			GetBagCount(*Hyper, PlayerTrainerId, HyperPotionId),
			0);

		const FItemId ReviveId = FBattleBagItemRules::GetReviveId();
		FBagScenario ReviveScenario;
		ReviveScenario.PlayerReserveHP = 0;
		ReviveScenario.PlayerBag = {{ReviveId, 1}};
		TUniquePtr<FBattleEngine> Revive = MakeEngine(ReviveScenario, 8133);
		if (!TestTrue(TEXT("Revive transition locks"),
			LockPlayerBagAndOpponentFight(
				*Revive,
				ReviveId,
				MakePartySlotId(1)))
			|| !TestTrue(TEXT("Revive transition starts"),
				Revive->BeginNextLockedAction().WasAccepted()))
		{
			return false;
		}
		const FBattleResolution ReviveResolution = Revive->ExecuteCurrentBagItem();
		TestTrue(TEXT("Revive remains accepted"), ReviveResolution.WasAccepted());
		TestTrue(TEXT("Revive returns the exact appended resolution once"),
			IsReturnedResolutionAppendedExactlyOnce(*Revive, ReviveResolution));
		TestTrue(TEXT("Revive preserves exact event order"),
			HasExactEventOrder(ReviveResolution, {
				EBattleEventType::ItemUsed,
				EBattleEventType::ItemConsumed,
				EBattleEventType::Healing,
				EBattleEventType::HPChanged,
				EBattleEventType::ActionCompleted}));
		const FBattleBattlerState* Revived =
			FBattleC08CBagEngineFixture::GetBattler(*Revive, ReserveId);
		TestTrue(TEXT("Revive preserves its exact half-HP lifecycle transition"),
			Revived != nullptr
				&& Revived->CurrentHP == 100
				&& !Revived->bFainted
				&& !Revived->bFaintTransitionPending
				&& !Revived->bRemoved);
		TestEqual(TEXT("Revive still consumes one item"),
			GetBagCount(*Revive, PlayerTrainerId, ReviveId),
			0);

		const FItemId XAttackId = FBattleBagItemRules::GetXAttackId();
		FBagScenario XAttackScenario;
		XAttackScenario.PlayerBag = {{XAttackId, 1}};
		TUniquePtr<FBattleEngine> XAttack = MakeEngine(XAttackScenario, 8134);
		if (!TestTrue(TEXT("X Attack transition locks"),
			LockPlayerBagAndOpponentFight(
				*XAttack,
				XAttackId,
				FPartySlotId(),
				MakeActiveSlotId(
					EBattleSide::Player,
					EBattlePosition::Left)))
			|| !TestTrue(TEXT("X Attack transition starts"),
				XAttack->BeginNextLockedAction().WasAccepted()))
		{
			return false;
		}
		const FBattleResolution XAttackResolution = XAttack->ExecuteCurrentBagItem();
		TestTrue(TEXT("X Attack remains accepted"), XAttackResolution.WasAccepted());
		TestTrue(TEXT("X Attack returns the exact appended resolution once"),
			IsReturnedResolutionAppendedExactlyOnce(*XAttack, XAttackResolution));
		TestTrue(TEXT("X Attack preserves exact event order"),
			HasExactEventOrder(XAttackResolution, {
				EBattleEventType::ItemUsed,
				EBattleEventType::ItemConsumed,
				EBattleEventType::StatStageChanged,
				EBattleEventType::ActionCompleted}));
		TestEqual(TEXT("X Attack still raises Attack by two"),
			FBattleC08CBagEngineFixture::GetAttackStage(*XAttack, PlayerId),
			2);
		TestEqual(TEXT("X Attack still consumes one item"),
			GetBagCount(*XAttack, PlayerTrainerId, XAttackId),
			0);
		for (const TUniquePtr<FBattleEngine>* Engine : {&Hyper, &Revive, &XAttack})
		{
			const FBattleTrainerState* Trainer =
				FBattleC08CBagEngineFixture::GetTrainer(**Engine, PlayerTrainerId);
			TestTrue(TEXT("Each prevalidated transition consumes its Trainer Bag quota"),
				Trainer != nullptr
					&& !Trainer->ActionAllowance.bBagActionAvailable);
		}
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleADR00023D2StaleBagCancellationRoutesTest,
		"PokemonSolarus.Battle.ADR0002.3D2.Bag.StaleCancellation.RoutesAndEndOfTurnSuccess",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FBattleADR00023D2StaleBagCancellationRoutesTest::RunTest(
		const FString& Parameters)
	{
		(void)Parameters;
		const FTrainerId PlayerTrainerId =
			MakeNumericId<FTrainerId>(PlayerTrainerValue);
		const FBattlerId PlayerId =
			MakeNumericId<FBattlerId>(PlayerBattlerValue);
		const FBattlerId PlayerReserveId =
			MakeNumericId<FBattlerId>(PlayerReserveValue);
		const FActiveSlotId PlayerSlot = MakeActiveSlotId(
			EBattleSide::Player,
			EBattlePosition::Left);
		const FItemId HyperPotionId = FBattleBagItemRules::GetHyperPotionId();
		const FItemId XAttackId = FBattleBagItemRules::GetXAttackId();
		bool bValid = true;

		{
			FBagScenario Scenario;
			Scenario.PlayerHP = 50;
			Scenario.PlayerBag = {{HyperPotionId, 2}};
			FObservedBagRandom* Random = nullptr;
			TUniquePtr<FBattleEngine> Engine = MakeObservedEngine(
				Scenario,
				8110,
				Random);
			const bool bPrepared = LockPlayerBagAndOpponentFight(
					*Engine,
					HyperPotionId,
					MakePartySlotId(0))
				&& Engine->BeginNextLockedAction().WasAccepted()
				&& TryMakeCurrentBagTargetStale(*Engine, PlayerReserveId);
			bValid &= TestTrue(
				TEXT("The stale-target Bag route is reachable"),
				bPrepared);
			if (!bPrepared || Random == nullptr)
			{
				return false;
			}
			const FStaleBagObservation Before = ObserveStaleBag(
				*Engine,
				*Random,
				PlayerTrainerId);
			const FBattleResolution Canceled = Engine->ExecuteCurrentBagItem();
			bValid &= VerifyAcceptedStaleBagCancellation(
				*this,
				TEXT("Stale target"),
				*Engine,
				*Random,
				PlayerTrainerId,
				EBattlePhase::Resolving,
				Before,
				Canceled);
		}

		{
			FBagScenario Scenario;
			Scenario.PlayerHP = 50;
			Scenario.PlayerBag = {{HyperPotionId, 2}};
			FObservedBagRandom* Random = nullptr;
			TUniquePtr<FBattleEngine> Engine = MakeObservedEngine(
				Scenario,
				8111,
				Random);
			const bool bPrepared = LockPlayerBagAndOpponentFight(
					*Engine,
					HyperPotionId,
					MakePartySlotId(0))
				&& Engine->BeginNextLockedAction().WasAccepted()
				&& TrySetCompiledBagPermission(
					*Engine,
					PlayerTrainerId,
					false);
			bValid &= TestTrue(
				TEXT("The stale compiled Bag-policy route is reachable"),
				bPrepared);
			if (!bPrepared || Random == nullptr)
			{
				return false;
			}
			const FStaleBagObservation Before = ObserveStaleBag(
				*Engine,
				*Random,
				PlayerTrainerId);
			bValid &= TestFalse(
				TEXT("The execution-time Bag policy is now denied"),
				Before.bActingTrainerMayUseBag);
			const FBattleResolution Canceled = Engine->ExecuteCurrentBagItem();
			bValid &= VerifyAcceptedStaleBagCancellation(
				*this,
				TEXT("Stale Bag policy"),
				*Engine,
				*Random,
				PlayerTrainerId,
				EBattlePhase::Resolving,
				Before,
				Canceled);
		}

		{
			FBagScenario Scenario;
			Scenario.PlayerHP = 50;
			Scenario.PlayerBag = {{HyperPotionId, 2}};
			FObservedBagRandom* Random = nullptr;
			TUniquePtr<FBattleEngine> Engine = MakeObservedEngine(
				Scenario,
				8112,
				Random);
			const bool bPrepared = LockPlayerBagAndOpponentFight(
					*Engine,
					HyperPotionId,
					MakePartySlotId(0))
				&& Engine->BeginNextLockedAction().WasAccepted()
				&& FBattleC08CBagEngineFixture::SetCurrentHP(
					*Engine,
					PlayerId,
					200);
			bValid &= TestTrue(
				TEXT("The newly illegal Hyper Potion route is reachable"),
				bPrepared);
			if (!bPrepared || Random == nullptr)
			{
				return false;
			}
			const FStaleBagObservation Before = ObserveStaleBag(
				*Engine,
				*Random,
				PlayerTrainerId);
			const FBattleResolution Canceled = Engine->ExecuteCurrentBagItem();
			bValid &= VerifyAcceptedStaleBagCancellation(
				*this,
				TEXT("Illegal Hyper Potion"),
				*Engine,
				*Random,
				PlayerTrainerId,
				EBattlePhase::Resolving,
				Before,
				Canceled);
		}

		{
			FBagScenario Scenario;
			Scenario.PlayerBag = {{XAttackId, 2}};
			FObservedBagRandom* Random = nullptr;
			TUniquePtr<FBattleEngine> Engine = MakeObservedEngine(
				Scenario,
				8113,
				Random);
			const bool bPrepared =
				FBattleC08CBagEngineFixture::ApplyAttackStageChange(
					*Engine,
					PlayerId,
					5)
				&& LockPlayerBagAndOpponentFight(
					*Engine,
					XAttackId,
					FPartySlotId(),
					PlayerSlot)
				&& Engine->BeginNextLockedAction().WasAccepted()
				&& FBattleC08CBagEngineFixture::ApplyAttackStageChange(
					*Engine,
					PlayerId,
					1);
			bValid &= TestTrue(
				TEXT("The newly illegal X Attack route is reachable"),
				bPrepared);
			if (!bPrepared || Random == nullptr)
			{
				return false;
			}
			const FStaleBagObservation Before = ObserveStaleBag(
				*Engine,
				*Random,
				PlayerTrainerId);
			const FBattleResolution Canceled = Engine->ExecuteCurrentBagItem();
			bValid &= VerifyAcceptedStaleBagCancellation(
				*this,
				TEXT("Illegal X Attack"),
				*Engine,
				*Random,
				PlayerTrainerId,
				EBattlePhase::Resolving,
				Before,
				Canceled);
		}

		{
			FBagScenario Scenario;
			Scenario.PlayerHP = 50;
			Scenario.PlayerBag = {{HyperPotionId, 2}};
			FObservedBagRandom* Random = nullptr;
			TUniquePtr<FBattleEngine> Engine = MakeObservedEngine(
				Scenario,
				8114,
				Random);
			const bool bPrepared = LockPlayerBagAndOpponentFight(
					*Engine,
					HyperPotionId,
					MakePartySlotId(0))
				&& Engine->BeginNextLockedAction().WasAccepted()
				&& TrySetBagItemCount(
					*Engine,
					PlayerTrainerId,
					HyperPotionId,
					0);
			bValid &= TestTrue(
				TEXT("The Bag-contract pre-use rejection route is reachable"),
				bPrepared);
			if (!bPrepared || Random == nullptr)
			{
				return false;
			}
			const FStaleBagObservation Before = ObserveStaleBag(
				*Engine,
				*Random,
				PlayerTrainerId);
			const FBattleResolution Canceled = Engine->ExecuteCurrentBagItem();
			bValid &= VerifyAcceptedStaleBagCancellation(
				*this,
				TEXT("Bag-contract pre-use rejection"),
				*Engine,
				*Random,
				PlayerTrainerId,
				EBattlePhase::Resolving,
				Before,
				Canceled);
		}

		{
			FBagScenario Scenario;
			Scenario.PlayerHP = 50;
			Scenario.PlayerBag = {{HyperPotionId, 2}};
			FObservedBagRandom* Random = nullptr;
			TUniquePtr<FBattleEngine> Engine = MakeObservedEngine(
				Scenario,
				8115,
				Random);
			const bool bPrepared = LockPlayerBagAndOpponentFight(
					*Engine,
					HyperPotionId,
					MakePartySlotId(0))
				&& Engine->BeginNextLockedAction().WasAccepted()
				&& FBattleC08CBagEngineFixture::SetCurrentHP(
					*Engine,
					PlayerId,
					200)
				&& TryKeepOnlyCurrentBagAction(*Engine);
			bValid &= TestTrue(
				TEXT("A stale final Bag action reaches the EndOfTurn boundary"),
				bPrepared);
			if (!bPrepared || Random == nullptr)
			{
				return false;
			}
			const FStaleBagObservation Before = ObserveStaleBag(
				*Engine,
				*Random,
				PlayerTrainerId);
			const FBattleResolution Canceled = Engine->ExecuteCurrentBagItem();
			bValid &= VerifyAcceptedStaleBagCancellation(
				*this,
				TEXT("End-of-turn stale Bag"),
				*Engine,
				*Random,
				PlayerTrainerId,
				EBattlePhase::EndOfTurn,
				Before,
				Canceled);
		}

		return bValid;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleADR00023D2StaleBagCancellationFailureTest,
		"PokemonSolarus.Battle.ADR0002.3D2.Bag.StaleCancellation.EventPlanAndResolutionFailure",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FBattleADR00023D2StaleBagCancellationFailureTest::RunTest(
		const FString& Parameters)
	{
		(void)Parameters;
		const FTrainerId PlayerTrainerId =
			MakeNumericId<FTrainerId>(PlayerTrainerValue);
		const FBattlerId PlayerId =
			MakeNumericId<FBattlerId>(PlayerBattlerValue);
		const FItemId HyperPotionId = FBattleBagItemRules::GetHyperPotionId();
		bool bValid = true;

		{
			FBagScenario Scenario;
			Scenario.PlayerHP = 50;
			Scenario.PlayerBag = {{HyperPotionId, 2}};
			FObservedBagRandom* Random = nullptr;
			TUniquePtr<FBattleEngine> Engine = MakeObservedEngine(
				Scenario,
				8116,
				Random);
			const bool bPrepared = LockPlayerBagAndOpponentFight(
					*Engine,
					HyperPotionId,
					MakePartySlotId(0))
				&& Engine->BeginNextLockedAction().WasAccepted()
				&& FBattleC08CBagEngineFixture::SetCurrentHP(
					*Engine,
					PlayerId,
					200);
			bValid &= TestTrue(
				TEXT("The event-plan failure starts from a stale Bag action"),
				bPrepared);
			if (!bPrepared || Random == nullptr)
			{
				return false;
			}
			FBattleC08CBagEngineFixture::GetMutableState(*Engine).NextEventOrdinal =
				TNumericLimits<uint64>::Max() - 1;
			const FStaleBagObservation Before = ObserveStaleBag(
				*Engine,
				*Random,
				PlayerTrainerId);
			bValid &= TestEqual(
				TEXT("Two accepted events cannot fit the remaining event identity space"),
				Before.NextEventOrdinal,
				TNumericLimits<uint64>::Max() - 1);
			const FBattleResolution Rejected = Engine->ExecuteCurrentBagItem();
			bValid &= VerifyRejectedStaleBagFailure(
				*this,
				TEXT("Event-plan exhaustion"),
				*Engine,
				*Random,
				PlayerTrainerId,
				Before,
				Rejected);
		}

		{
			FBagScenario Scenario;
			Scenario.PlayerHP = 50;
			Scenario.PlayerBag = {{HyperPotionId, 2}};
			FObservedBagRandom* Random = nullptr;
			TUniquePtr<FBattleEngine> Engine = MakeObservedEngine(
				Scenario,
				8117,
				Random);
			const bool bPrepared = LockPlayerBagAndOpponentFight(
					*Engine,
					HyperPotionId,
					MakePartySlotId(0))
				&& Engine->BeginNextLockedAction().WasAccepted()
				&& FBattleC08CBagEngineFixture::SetCurrentHP(
					*Engine,
					PlayerId,
					200);
			bValid &= TestTrue(
				TEXT("The resolution-construction failure starts from a stale Bag action"),
				bPrepared);
			if (!bPrepared || Random == nullptr)
			{
				return false;
			}
			FBattleC08CBagEngineFixture::GetMutableState(*Engine).StateVersion =
				TNumericLimits<uint64>::Max();
			const FStaleBagObservation Before = ObserveStaleBag(
				*Engine,
				*Random,
				PlayerTrainerId);
			bValid &= TestEqual(
				TEXT("An accepted resolution cannot construct a next state version"),
				Before.StateVersion,
				TNumericLimits<uint64>::Max());
			const FBattleResolution Rejected = Engine->ExecuteCurrentBagItem();
			bValid &= VerifyRejectedStaleBagFailure(
				*this,
				TEXT("Accepted-resolution construction"),
				*Engine,
				*Random,
				PlayerTrainerId,
				Before,
				Rejected);
		}

		return bValid;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC08CBagXAttackTest,
		"PokemonSolarus.Battle.C08C.Bag.Engine.XAttackCapRevalidationAndSwitchCleanup",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FBattleC08CBagXAttackTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		const FItemId XAttackId = FBattleBagItemRules::GetXAttackId();
		const FTrainerId PlayerTrainerId = MakeNumericId<FTrainerId>(PlayerTrainerValue);
		const FBattlerId PlayerId = MakeNumericId<FBattlerId>(PlayerBattlerValue);
		const FActiveSlotId PlayerSlot = MakeActiveSlotId(
			EBattleSide::Player,
			EBattlePosition::Left);
		FBagScenario Scenario;
		Scenario.PlayerBag = {{XAttackId, 2}};
		TUniquePtr<FBattleEngine> Engine = MakeEngine(Scenario);
		TestTrue(TEXT("X Attack locks onto the acting active battler"),
			LockPlayerBagAndOpponentFight(
				*Engine,
				XAttackId,
				FPartySlotId(),
				PlayerSlot));
		TestTrue(TEXT("The X Attack action starts"),
			Engine->BeginNextLockedAction().WasAccepted());
		TestTrue(TEXT("X Attack resolves"),
			Engine->ExecuteCurrentBagItem().WasAccepted());
		TestEqual(TEXT("X Attack raises Attack by exactly two"),
			FBattleC08CBagEngineFixture::GetAttackStage(*Engine, PlayerId),
			2);
		TestTrue(TEXT("A voluntary switch can be prepared after X Attack"),
			FBattleC08CBagEngineFixture::PrepareLockedSwitch(
				*Engine,
				PlayerId,
				MakePartySlotId(1)));
		TestTrue(TEXT("The switch starts"),
			Engine->BeginNextLockedAction().WasAccepted());
		TestTrue(TEXT("The switch resolves"),
			Engine->ExecuteCurrentSwitch().WasAccepted());
		TestEqual(TEXT("Switching clears the X Attack stage"),
			FBattleC08CBagEngineFixture::GetAttackStage(*Engine, PlayerId),
			0);

		TUniquePtr<FBattleEngine> StaleEngine = MakeEngine(Scenario, 8101);
		TestTrue(TEXT("Attack can be seeded to plus five"),
			FBattleC08CBagEngineFixture::ApplyAttackStageChange(
				*StaleEngine,
				PlayerId,
				5));
		TestTrue(TEXT("A below-cap X Attack locks"),
			LockPlayerBagAndOpponentFight(
				*StaleEngine,
				XAttackId,
				FPartySlotId(),
				PlayerSlot));
		TestTrue(TEXT("The below-cap X Attack starts"),
			StaleEngine->BeginNextLockedAction().WasAccepted());
		TestTrue(TEXT("Attack reaches plus six before Bag mutation"),
			FBattleC08CBagEngineFixture::ApplyAttackStageChange(
				*StaleEngine,
				PlayerId,
				1));
		const int32 TraceBefore = StaleEngine->ExportRandomTrace().Num();
		const FBattleResolution Stale = StaleEngine->ExecuteCurrentBagItem();
		TestTrue(TEXT("At-cap revalidation cancels the stale action"),
			Stale.WasAccepted()
				&& FindEvent(Stale, EBattleEventType::ActionCanceled) != INDEX_NONE);
		TestEqual(TEXT("At-cap cancellation consumes no item"),
			GetBagCount(*StaleEngine, PlayerTrainerId, XAttackId),
			2);
		const FBattleTrainerState* Trainer =
			FBattleC08CBagEngineFixture::GetTrainer(*StaleEngine, PlayerTrainerId);
		TestTrue(TEXT("At-cap cancellation consumes no Bag quota"),
			Trainer != nullptr && Trainer->ActionAllowance.bBagActionAvailable);
		TestEqual(TEXT("At-cap cancellation consumes no RNG"),
			StaleEngine->ExportRandomTrace().Num(),
			TraceBefore);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC08CBagOwnershipTest,
		"PokemonSolarus.Battle.C08C.Bag.Engine.SeparateTrainerBagsQuotaAndNoPersistentWrite",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FBattleC08CBagOwnershipTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		const FItemId HyperPotionId = FBattleBagItemRules::GetHyperPotionId();
		const FTrainerId PlayerTrainerId = MakeNumericId<FTrainerId>(PlayerTrainerValue);
		const FTrainerId OpponentTrainerId = MakeNumericId<FTrainerId>(OpponentTrainerValue);
		FBagScenario Scenario;
		Scenario.PlayerHP = 50;
		Scenario.OpponentHP = 70;
		Scenario.PlayerBag = {{HyperPotionId, 2}};
		Scenario.OpponentBag = {{HyperPotionId, 3}};
		TUniquePtr<FBattleEngine> Engine = MakeEngine(Scenario);
		TestTrue(TEXT("Both Trainers independently lock their own Bag action"),
			LockBothBags(
				*Engine,
				HyperPotionId,
				MakePartySlotId(0),
				FActiveSlotId(),
				HyperPotionId,
				MakePartySlotId(0),
				FActiveSlotId()));
		TestTrue(TEXT("The first Bag action starts"),
			Engine->BeginNextLockedAction().WasAccepted());
		TestTrue(TEXT("The first Bag action resolves"),
			Engine->ExecuteCurrentBagItem().WasAccepted());
		TestTrue(TEXT("The second Bag action starts"),
			Engine->BeginNextLockedAction().WasAccepted());
		TestTrue(TEXT("The second Bag action resolves"),
			Engine->ExecuteCurrentBagItem().WasAccepted());
		TestEqual(TEXT("Only the player's own count decrements from two"),
			GetBagCount(*Engine, PlayerTrainerId, HyperPotionId),
			1);
		TestEqual(TEXT("Only the opponent's own count decrements from three"),
			GetBagCount(*Engine, OpponentTrainerId, HyperPotionId),
			2);
		const FBattleTrainerState* PlayerTrainer =
			FBattleC08CBagEngineFixture::GetTrainer(*Engine, PlayerTrainerId);
		const FBattleTrainerState* OpponentTrainer =
			FBattleC08CBagEngineFixture::GetTrainer(*Engine, OpponentTrainerId);
		TestTrue(TEXT("Each Trainer independently spends one Bag quota"),
			PlayerTrainer != nullptr
				&& OpponentTrainer != nullptr
				&& !PlayerTrainer->ActionAllowance.bBagActionAvailable
				&& !OpponentTrainer->ActionAllowance.bBagActionAvailable);
		TestEqual(TEXT("The frozen player setup inventory is not persisted over"),
			GetSetupBagCount(*Engine, PlayerTrainerId, HyperPotionId),
			2);
		TestEqual(TEXT("The frozen opponent setup inventory is not persisted over"),
			GetSetupBagCount(*Engine, OpponentTrainerId, HyperPotionId),
			3);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC08CBagPartnerAndDoubleQuotaTest,
		"PokemonSolarus.Battle.C08C.Bag.Engine.PartnerSeparateBagsAndDoubleSharedQuota",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FBattleC08CBagPartnerAndDoubleQuotaTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		const FItemId HyperPotionId = FBattleBagItemRules::GetHyperPotionId();
		const FTrainerId PlayerTrainerId = MakeNumericId<FTrainerId>(PlayerTrainerValue);
		const FTrainerId PartnerTrainerId = MakeNumericId<FTrainerId>(PartnerTrainerValue);
		const FBattlerId PlayerId = MakeNumericId<FBattlerId>(PlayerBattlerValue);
		const FBattlerId PlayerRightId = MakeNumericId<FBattlerId>(PlayerReserveValue);
		const FBattlerId PartnerId = MakeNumericId<FBattlerId>(PartnerBattlerValue);

		FBagScenario PartnerScenario;
		PartnerScenario.Format = EBattleFormat::PartnerDouble;
		PartnerScenario.PlayerHP = 50;
		PartnerScenario.PartnerHP = 60;
		PartnerScenario.PlayerBag = {{HyperPotionId, 2}};
		PartnerScenario.PartnerBag = {{HyperPotionId, 3}};
		TUniquePtr<FBattleEngine> PartnerEngine = MakeEngine(PartnerScenario, 8120);
		TestTrue(TEXT("Partner Double selection begins"), BeginRuntime(*PartnerEngine));
		TArray<FBattleDecisionRequest> Requests =
			PartnerEngine->GetPendingDecisionRequests();
		TestEqual(TEXT("The player owns the first Partner Double request"),
			Requests.Num(),
			1);
		if (Requests.Num() == 1)
		{
			TestTrue(TEXT("The first request belongs to the player Trainer"),
				Requests[0].GetDecisionOwnerTrainerId() == PlayerTrainerId);
			TestEqual(TEXT("The player's Bag exposes only its own injured party target"),
				Requests[0].GetLegalItemPartyTargets().Num(),
				1);
		}
		TestTrue(TEXT("The player locks an item from the player Bag"),
			SubmitBag(
				*PartnerEngine,
				HyperPotionId,
				MakePartySlotId(0),
				FActiveSlotId()));

		Requests = PartnerEngine->GetPendingDecisionRequests();
		TestEqual(TEXT("The partner owns a separate request"), Requests.Num(), 1);
		if (Requests.Num() == 1)
		{
			TestTrue(TEXT("The second request belongs to the partner Trainer"),
				Requests[0].GetDecisionOwnerTrainerId() == PartnerTrainerId);
			TestEqual(TEXT("The partner Bag exposes only its own injured party target"),
				Requests[0].GetLegalItemPartyTargets().Num(),
				1);
		}
		TestTrue(TEXT("The partner independently locks an item from the partner Bag"),
			SubmitBag(
				*PartnerEngine,
				HyperPotionId,
				MakePartySlotId(0),
				FActiveSlotId()));
		TestTrue(TEXT("The opposing Double owner submits both Fight choices"),
			SubmitFightBatch(*PartnerEngine));
		TestEqual(TEXT("The Partner Double turn locks"),
			PartnerEngine->GetSnapshot().GetPhase(),
			EBattlePhase::Locked);
		TestTrue(TEXT("The player Bag action starts"),
			PartnerEngine->BeginNextLockedAction().WasAccepted());
		TestTrue(TEXT("The player Bag action resolves"),
			PartnerEngine->ExecuteCurrentBagItem().WasAccepted());
		TestTrue(TEXT("The partner Bag action starts"),
			PartnerEngine->BeginNextLockedAction().WasAccepted());
		TestTrue(TEXT("The partner Bag action resolves"),
			PartnerEngine->ExecuteCurrentBagItem().WasAccepted());
		TestEqual(TEXT("Only the player Bag decrements from its own count"),
			GetBagCount(*PartnerEngine, PlayerTrainerId, HyperPotionId),
			1);
		TestEqual(TEXT("Only the partner Bag decrements from its own count"),
			GetBagCount(*PartnerEngine, PartnerTrainerId, HyperPotionId),
			2);
		const FBattleBattlerState* PlayerBattler =
			FBattleC08CBagEngineFixture::GetBattler(*PartnerEngine, PlayerId);
		const FBattleBattlerState* PartnerBattler =
			FBattleC08CBagEngineFixture::GetBattler(*PartnerEngine, PartnerId);
		TestEqual(TEXT("The player item heals the player-owned active"),
			PlayerBattler != nullptr ? PlayerBattler->CurrentHP : INDEX_NONE,
			170);
		TestEqual(TEXT("The partner item heals the partner-owned active"),
			PartnerBattler != nullptr ? PartnerBattler->CurrentHP : INDEX_NONE,
			180);
		const FBattleTrainerState* PlayerTrainer =
			FBattleC08CBagEngineFixture::GetTrainer(*PartnerEngine, PlayerTrainerId);
		const FBattleTrainerState* PartnerTrainer =
			FBattleC08CBagEngineFixture::GetTrainer(*PartnerEngine, PartnerTrainerId);
		TestTrue(TEXT("Player and partner spend independent Bag quotas"),
			PlayerTrainer != nullptr
				&& PartnerTrainer != nullptr
				&& !PlayerTrainer->ActionAllowance.bBagActionAvailable
				&& !PartnerTrainer->ActionAllowance.bBagActionAvailable);

		FBagScenario DoubleScenario;
		DoubleScenario.Format = EBattleFormat::Double;
		DoubleScenario.PlayerHP = 50;
		DoubleScenario.PlayerReserveHP = 60;
		DoubleScenario.PlayerBag = {{HyperPotionId, 2}};
		TUniquePtr<FBattleEngine> DoubleEngine = MakeEngine(DoubleScenario, 8121);
		TestTrue(TEXT("Ordinary Double selection begins"), BeginRuntime(*DoubleEngine));
		Requests = DoubleEngine->GetPendingDecisionRequests();
		TestEqual(TEXT("One Trainer receives two allied-active requests"),
			Requests.Num(),
			2);
		if (Requests.Num() == 2)
		{
			TestTrue(TEXT("Both active requests belong to the same Trainer"),
				Requests[0].GetDecisionOwnerTrainerId() == PlayerTrainerId
					&& Requests[1].GetDecisionOwnerTrainerId() == PlayerTrainerId);
			TestTrue(TEXT("Each active initially sees the Trainer-owned Bag option"),
				Requests[0].GetLegalActionKinds().Contains(EBattleActionKind::Bag)
					&& Requests[1].GetLegalActionKinds().Contains(EBattleActionKind::Bag));
		}

		FBattleDecision FirstBag;
		FBattleDecisionBatch FirstSelection;
		TestTrue(TEXT("The first allied active can construct a Bag choice"),
			Requests.Num() == 2
				&& TryMakeBagDecision(
					Requests[0],
					HyperPotionId,
					MakePartySlotId(0),
					FActiveSlotId(),
					FirstBag));
		TArray<FBattleDecision> FirstDecisions;
		FirstDecisions.Add(FirstBag);
		TestTrue(TEXT("The first allied choice forms a partial owner batch"),
			TryMakeDecisionBatch(Requests, MoveTemp(FirstDecisions), FirstSelection));
		TestTrue(TEXT("The first allied Bag choice is accepted"),
			DoubleEngine->SubmitDecisionBatch(FirstSelection).WasAccepted());

		Requests = DoubleEngine->GetPendingDecisionRequests();
		TestEqual(TEXT("Only the second allied active remains to choose"),
			Requests.Num(),
			1);
		if (Requests.Num() == 1)
		{
			TestFalse(TEXT("The shared Trainer quota removes Bag from the second actor"),
				Requests[0].GetLegalActionKinds().Contains(EBattleActionKind::Bag));
			TestTrue(TEXT("The second actor receives the typed Bag-restricted reason"),
				Requests[0].GetUnavailableOptions().ContainsByPredicate(
					[](const FBattleUnavailableDecisionOption& Option)
					{
						return Option.Kind == EBattleDecisionOptionKind::Action
							&& Option.ActionKind == EBattleActionKind::Bag
							&& Option.Reason
								== EBattleOptionUnavailableReason::BagRestricted;
					}));
		}
		TestTrue(TEXT("The second allied active submits a Fight instead"),
			SubmitFightBatch(*DoubleEngine));
		TestTrue(TEXT("The opposing Trainer submits both Fight choices"),
			SubmitFightBatch(*DoubleEngine));
		TestEqual(TEXT("The ordinary Double turn locks"),
			DoubleEngine->GetSnapshot().GetPhase(),
			EBattlePhase::Locked);
		TestTrue(TEXT("The one permitted Trainer Bag action starts"),
			DoubleEngine->BeginNextLockedAction().WasAccepted());
		TestTrue(TEXT("The one permitted Trainer Bag action resolves"),
			DoubleEngine->ExecuteCurrentBagItem().WasAccepted());
		TestEqual(TEXT("The shared Bag consumes exactly one item"),
			GetBagCount(*DoubleEngine, PlayerTrainerId, HyperPotionId),
			1);
		PlayerBattler =
			FBattleC08CBagEngineFixture::GetBattler(*DoubleEngine, PlayerId);
		const FBattleBattlerState* PlayerRight =
			FBattleC08CBagEngineFixture::GetBattler(*DoubleEngine, PlayerRightId);
		TestEqual(TEXT("Only the selected allied target is healed"),
			PlayerBattler != nullptr ? PlayerBattler->CurrentHP : INDEX_NONE,
			170);
		TestEqual(TEXT("The other allied active remains unchanged"),
			PlayerRight != nullptr ? PlayerRight->CurrentHP : INDEX_NONE,
			60);
		PlayerTrainer =
			FBattleC08CBagEngineFixture::GetTrainer(*DoubleEngine, PlayerTrainerId);
		TestTrue(TEXT("The shared per-Trainer Bag quota is spent"),
			PlayerTrainer != nullptr
				&& !PlayerTrainer->ActionAllowance.bBagActionAvailable);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC08CBagCaptureHandoffTest,
		"PokemonSolarus.Battle.C08C.Bag.Handoff.PokeBallReachesC09BWithoutEarlyConsumptionOrRng",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FBattleC08CBagCaptureHandoffTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		const FItemId PokeBallId = FBattleBagItemRules::GetPokeBallId();
		const FTrainerId PlayerTrainerId = MakeNumericId<FTrainerId>(PlayerTrainerValue);
		const FActiveSlotId OpponentSlot = MakeActiveSlotId(
			EBattleSide::Opponent,
			EBattlePosition::Left);
		FBagScenario Scenario;
		Scenario.EncounterKind = EBattleEncounterKind::Wild;
		Scenario.bCaptureAllowed = true;
		Scenario.PlayerBag = {{PokeBallId, 2}};
		TUniquePtr<FBattleEngine> Engine = MakeEngine(Scenario, 8110);
		TestTrue(TEXT("A legal Poke Ball action locks for the wild opposing active"),
			LockPlayerBagAndOpponentFight(
				*Engine,
				PokeBallId,
				FPartySlotId(),
				OpponentSlot));
		TestTrue(TEXT("The Poke Ball action starts"),
			Engine->BeginNextLockedAction().WasAccepted());
		TestTrue(TEXT("The started Poke Ball action reaches the C09B execution checkpoint"),
			Engine->GetCurrentLockedAction().IsSet());
		TestEqual(TEXT("C08C consumes no Poke Ball before C09B execution"),
			GetBagCount(*Engine, PlayerTrainerId, PokeBallId),
			2);
		const FBattleTrainerState* Trainer =
			FBattleC08CBagEngineFixture::GetTrainer(*Engine, PlayerTrainerId);
		TestTrue(TEXT("C08C consumes no Bag quota before C09B execution"),
			Trainer != nullptr && Trainer->ActionAllowance.bBagActionAvailable);
		TestEqual(TEXT("C08C consumes no capture RNG before C09B execution"),
			Engine->ExportRandomTrace().Num(),
			0);
		return true;
	}
}

#endif
