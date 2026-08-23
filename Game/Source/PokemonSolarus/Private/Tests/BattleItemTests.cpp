#if WITH_DEV_AUTOMATION_TESTS

#include "Battle/BattleAbility.h"
#include "Battle/BattleEffectExecutor.h"
#include "Battle/BattleEngine.h"
#include "Battle/BattleFieldSideConditions.h"
#include "Battle/BattleItem.h"
#include "Battle/BattleMajorStatus.h"
#include "Battle/BattleState.h"
#include "Battle/BattleVolatile.h"
#include "BattleTestFactories.h"
#include "Misc/AutomationTest.h"

class FBattleC08CEngineFixture
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
		return Engine.State.IsValid() ? Engine.State->FindBattler(BattlerId) : nullptr;
	}

	static bool SetCurrentHP(
		FBattleEngine& Engine,
		const FBattlerId BattlerId,
		const int32 CurrentHP)
	{
		FBattleBattlerState* Battler = Engine.State.IsValid()
			? Engine.State->FindMutableBattler(BattlerId)
			: nullptr;
		if (Battler == nullptr || CurrentHP <= 0
			|| CurrentHP > Battler->PermanentStats.MaxHP)
		{
			return false;
		}
		Battler->CurrentHP = CurrentHP;
		Battler->bFainted = false;
		Battler->bFaintTransitionPending = false;
		return true;
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
		Battler->MajorStatusId = StatusId;
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
		FBattleVolatileTriggerRegistrationFacts TriggerFacts;
		TriggerFacts.VolatileId = VolatileId;
		TriggerFacts.PayloadId = VolatileId.GetDefinitionId();
		TriggerFacts.Owner = Owner;
		TriggerFacts.Source = Source;
		TriggerFacts.RemainingTurns = VolatileId == FBattleVolatileRules::GetConfusionId()
			? TOptional<int32>(2)
			: TOptional<int32>();
		TriggerFacts.Layers = Layers;
		EBattleTriggerError Error = EBattleTriggerError::None;
		if (!FBattleVolatileRules::TryRegisterTriggers(
			State.TriggerFramework,
			TriggerFacts,
			Error))
		{
			return false;
		}
		FBattleConditionState Condition;
		Condition.ConditionId = VolatileId;
		Condition.RemainingTurns = TriggerFacts.RemainingTurns;
		Condition.LayerCount = Layers;
		Condition.CreationOrdinal = State.NextConditionCreationOrdinal++;
		Condition.SourceBattlerId = SourceBattlerId;
		Battler->Volatiles.Add(MoveTemp(Condition));
		TArray<FBattleTriggerEffectRequest> Requests;
		TArray<FBattleTriggerLifecycleFact> Facts;
		State.TriggerFramework.DrainEffectRequests(Requests);
		State.TriggerFramework.DrainLifecycleFacts(Facts);
		return true;
	}

	static bool SeedCondition(
		FBattleEngine& Engine,
		const FConditionId& ConditionId,
		const EBattleSide Side,
		const FBattlerId SourceBattlerId,
		const int32 Layers = 1)
	{
		FBattleEngineState& State = GetMutableState(Engine);
		FBattleTriggerSubject Owner;
		if (!FBattleFieldSideConditionRules::IsCanonical(ConditionId)
			|| Layers <= 0)
		{
			return false;
		}
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
		FBattleFieldSideTriggerRegistrationFacts Facts;
		Facts.ConditionId = ConditionId;
		Facts.PayloadId = ConditionId.GetDefinitionId();
		Facts.Owner = Owner;
		Facts.Source = Source;
		Facts.Targets.Add(Owner);
		const EBattleConditionKind Family =
			FBattleFieldSideConditionRules::GetConditionFamily(ConditionId);
		Facts.RemainingTurns = Family == EBattleConditionKind::Weather
				|| Family == EBattleConditionKind::Terrain
				|| Family == EBattleConditionKind::Room
			? TOptional<int32>(5)
			: TOptional<int32>();
		Facts.Layers = Layers;
		EBattleTriggerError Error = EBattleTriggerError::None;
		if (!FBattleFieldSideConditionRules::TryRegisterTriggers(
			State.TriggerFramework,
			Facts,
			Error))
		{
			return false;
		}
		FBattleConditionState Condition;
		Condition.ConditionId = ConditionId;
		Condition.RemainingTurns = Facts.RemainingTurns;
		Condition.LayerCount = Layers;
		Condition.CreationOrdinal = State.NextConditionCreationOrdinal++;
		Condition.SourceBattlerId = SourceBattlerId;
		switch (Family)
		{
		case EBattleConditionKind::Weather:
			if (State.Field.Weather.IsSet())
			{
				return false;
			}
			State.Field.Weather = Condition;
			break;
		case EBattleConditionKind::Terrain:
			if (State.Field.Terrain.IsSet())
			{
				return false;
			}
			State.Field.Terrain = Condition;
			break;
		case EBattleConditionKind::Room:
			State.Field.Rooms.Add(Condition);
			break;
		case EBattleConditionKind::Hazard:
		case EBattleConditionKind::Screen:
		case EBattleConditionKind::SideCondition:
		{
			FBattleSideState* SideState = State.Sides.FindByPredicate(
				[Side](const FBattleSideState& Candidate)
				{
					return Candidate.Side == Side;
				});
			if (SideState == nullptr)
			{
				return false;
			}
			if (Family == EBattleConditionKind::Hazard)
			{
				SideState->Hazards.Add(Condition);
			}
			else
			{
				SideState->Conditions.Add(Condition);
			}
			break;
		}
		default:
			return false;
		}
		TArray<FBattleTriggerEffectRequest> Requests;
		TArray<FBattleTriggerLifecycleFact> Lifecycle;
		State.TriggerFramework.DrainEffectRequests(Requests);
		State.TriggerFramework.DrainLifecycleFacts(Lifecycle);
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
		FBattleLockedActionState Action;
		Action.ActionId = BattleTest::MakeNumericId<FActionId>(80801);
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

	static bool PrepareEndTurn(FBattleEngine& Engine)
	{
		FBattleEngineState& State = GetMutableState(Engine);
		State.Phase = EBattlePhase::EndOfTurn;
		State.LockedActions.Reset();
		State.CurrentLockedActionIndex = 0;
		State.PendingDecision.Reset();
		State.PendingDecisionRequests.Reset();
		State.DecisionOwnerSequence.Reset();
		State.AcceptedSelections.Reset();
		State.PendingReplacements.Reset();
		State.bEndTurnTriggerPassComplete = false;
		EBattleStateValidationError Validation = EBattleStateValidationError::None;
		return State.ValidateInvariants(Validation);
	}

	static bool TryMakeBattlerTarget(
		const FBattleEngine& Engine,
		const FBattlerId BattlerId,
		FBattleResolvedTarget& OutTarget)
	{
		const FBattleActivePositionState* Active = Engine.State.IsValid()
			? Engine.State->ActivePositions.FindByPredicate(
				[BattlerId](const FBattleActivePositionState& Candidate)
				{
					return Candidate.BattlerId == BattlerId;
				})
			: nullptr;
		if (Active == nullptr)
		{
			return false;
		}
		FBattleBattlerTarget Target;
		Target.ActiveSlotId = Active->ActiveSlotId;
		Target.BattlerId = BattlerId;
		return FBattleResolvedTarget::TryCreateBattler(Target, OutTarget);
	}

	static bool ExecuteCatalogMove(
		FBattleEngine& Engine,
		const FBattlerId UserBattlerId,
		const FMoveId& MoveId,
		const FBattlerId TargetBattlerId,
		FBattleEffectExecutionResult& OutResult,
		const uint64 OperationValue)
	{
		FBattleResolvedTarget Target;
		const FBattleMoveDefinition* Move = Engine.State.IsValid()
			? Engine.State->Catalog.FindMove(MoveId)
			: nullptr;
		const FBattleActivePositionState* User = Engine.State.IsValid()
			? Engine.State->ActivePositions.FindByPredicate(
				[UserBattlerId](const FBattleActivePositionState& Candidate)
				{
					return Candidate.BattlerId == UserBattlerId;
				})
			: nullptr;
		if (Move == nullptr || User == nullptr
			|| !TryMakeBattlerTarget(Engine, TargetBattlerId, Target))
		{
			return false;
		}
		FBattleEffectExecutionRequest Request;
		Request.BattleId = Engine.State->Setup.GetBattleId();
		Request.TurnId = Engine.State->TurnId;
		Request.ActionId = BattleTest::MakeNumericId<FActionId>(OperationValue);
		Request.ResolutionId = BattleTest::MakeNumericId<FResolutionId>(OperationValue);
		Request.UserBattlerId = UserBattlerId;
		Request.UserSlotId = User->ActiveSlotId;
		Request.Move = Move;
		Request.Targets.Add(Target);
		EBattleEffectExecutorError Error = EBattleEffectExecutorError::None;
		return FBattleEffectExecutor::TryExecuteAgainstState(
			Request,
			*Engine.State,
			OutResult,
			Error);
	}
};

namespace BattleItemTests
{
	using BattleTest::MakeActiveSlotId;
	using BattleTest::MakeDefinitionId;
	using BattleTest::MakeNumericId;
	using BattleTest::MakePartySlotId;

	constexpr uint64 PlayerTrainerValue = 1;
	constexpr uint64 OpponentTrainerValue = 2;
	constexpr uint64 PlayerValue = 11;
	constexpr uint64 PlayerReserveValue = 12;
	constexpr uint64 OpponentValue = 21;
	constexpr uint64 OpponentReserveValue = 22;

	const TCHAR* PlayerSpeciesName = TEXT("Species.C08C.Player");
	const TCHAR* OpponentSpeciesName = TEXT("Species.C08C.Opponent");
	const TCHAR* NormalMoveName = TEXT("Move.C08C.Normal");
	const TCHAR* SecondMoveName = TEXT("Move.C08C.Second");
	const TCHAR* NegativeMoveName = TEXT("Move.C08C.Negative");
	const TCHAR* StatusMoveName = TEXT("Move.C08C.Status");
	const TCHAR* GroundMoveName = TEXT("Move.C08C.Ground");
	const TCHAR* MultiHitMoveName = TEXT("Move.C08C.MultiHit");
	const TCHAR* LethalMultiHitMoveName = TEXT("Move.C08C.LethalMultiHit");
	const TCHAR* ForcedSwitchMoveName = TEXT("Move.C08C.ForcedSwitch");
	const TCHAR* IndirectRecoilMoveName = TEXT("Move.C08C.IndirectRecoil");

	struct FC08CScenario
	{
		FItemId PlayerItem;
		FItemId PlayerReserveItem;
		FItemId OpponentItem;
		FItemId OpponentReserveItem;
		FAbilityId PlayerAbility = FBattleAbilityRules::GetBlazeId();
		FAbilityId OpponentAbility = FBattleAbilityRules::GetMoldBreakerId();
		int32 PlayerHP = 200;
		int32 PlayerReserveHP = 200;
		int32 OpponentHP = 200;
		int32 PlayerSpeed = 120;
		int32 OpponentSpeed = 80;
	};

	struct FExpectedDraw
	{
		uint32 Minimum = 0;
		uint32 Maximum = 0;
		uint32 Result = 0;
		FDefinitionId Purpose;
	};

	class FScriptedRandom final : public IBattleRandom
	{
	public:
		explicit FScriptedRandom(TArray<FExpectedDraw> InExpected)
			: Expected(MoveTemp(InExpected))
		{
		}

		virtual bool TryDrawUniform(
			const uint32 InclusiveMinimum,
			const uint32 InclusiveMaximum,
			const FBattleRandomContext& Context,
			FBattleRandomDraw& OutDraw) override
		{
			OutDraw = FBattleRandomDraw();
			if (!Expected.IsValidIndex(NextIndex))
			{
				bMismatch = true;
				return false;
			}
			const FExpectedDraw& Next = Expected[NextIndex++];
			if (!Context.IsValid()
				|| InclusiveMinimum != Next.Minimum
				|| InclusiveMaximum != Next.Maximum
				|| Context.RulePurpose != Next.Purpose
				|| Next.Result < InclusiveMinimum
				|| Next.Result > InclusiveMaximum)
			{
				bMismatch = true;
				return false;
			}
			OutDraw.InclusiveMinimum = InclusiveMinimum;
			OutDraw.InclusiveMaximum = InclusiveMaximum;
			OutDraw.Bound = static_cast<uint64>(InclusiveMaximum)
				- static_cast<uint64>(InclusiveMinimum) + 1;
			OutDraw.RawValue = Next.Result;
			OutDraw.Result = Next.Result;
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
		TArray<FExpectedDraw> Expected;
		TArray<FBattleRandomDraw> Trace;
		int32 NextIndex = 0;
		bool bMismatch = false;
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

	FBattleMoveDefinition MakeDamageMove(
		const TCHAR* Name,
		const EPokemonType Type,
		const int32 Power,
		const int32 Priority = 0,
		const EBattleMoveCategory Category = EBattleMoveCategory::Physical)
	{
		FBattleMoveDefinition Move;
		Move.Id = MakeDefinitionId<FMoveId>(Name);
		Move.Type = Type;
		Move.Category = Category;
		Move.Power = Power;
		Move.bAlwaysHits = true;
		Move.BasePP = 20;
		Move.Priority = Priority;
		Move.TargetClass = EBattleTargetClass::SelectedOpponent;
		Move.Flags = EBattleMoveFlags::NeverCritical;
		FBattleMoveEffectDescriptor Damage;
		Damage.Kind = EBattleMoveEffectKind::Damage;
		Damage.Target = EBattleEffectTarget::ResolvedTarget;
		Move.Effects.Add(Damage);
		return Move;
	}

	FBattleMoveDefinition MakeMultiHitMove(const TCHAR* Name, const int32 Power)
	{
		FBattleMoveDefinition Move = MakeDamageMove(
			Name,
			EPokemonType::Normal,
			Power);
		Move.Effects[0].Order = 1;
		FBattleMoveEffectDescriptor MultiHit;
		MultiHit.Kind = EBattleMoveEffectKind::MultiHit;
		MultiHit.Target = EBattleEffectTarget::ResolvedTarget;
		MultiHit.MinimumCount = 2;
		MultiHit.MaximumCount = 2;
		Move.Effects.Insert(MultiHit, 0);
		return Move;
	}

	FBattleMoveDefinition MakeStatusMove()
	{
		FBattleMoveDefinition Move;
		Move.Id = MakeDefinitionId<FMoveId>(StatusMoveName);
		Move.Type = EPokemonType::Normal;
		Move.Category = EBattleMoveCategory::Status;
		Move.bAlwaysHits = true;
		Move.BasePP = 20;
		Move.TargetClass = EBattleTargetClass::SelectedOpponent;
		FBattleMoveEffectDescriptor Status;
		Status.Kind = EBattleMoveEffectKind::ApplyCondition;
		Status.Target = EBattleEffectTarget::ResolvedTarget;
		Status.ConditionId = FBattleMajorStatusRules::GetBurnId();
		Move.Effects.Add(Status);
		return Move;
	}

	FBattleMoveDefinition MakeForcedSwitchMove()
	{
		FBattleMoveDefinition Move = MakeDamageMove(
			ForcedSwitchMoveName,
			EPokemonType::Normal,
			40);
		FBattleMoveEffectDescriptor Switch;
		Switch.Order = 1;
		Switch.Kind = EBattleMoveEffectKind::Switch;
		Switch.Target = EBattleEffectTarget::ResolvedTarget;
		Move.Effects.Add(Switch);
		return Move;
	}

	FBattleMoveDefinition MakeIndirectRecoilMove()
	{
		FBattleMoveDefinition Move = MakeDamageMove(
			IndirectRecoilMoveName,
			EPokemonType::Normal,
			40);
		FBattleMoveEffectDescriptor Recoil;
		Recoil.Order = 1;
		Recoil.Kind = EBattleMoveEffectKind::Recoil;
		Recoil.Target = EBattleEffectTarget::User;
		Recoil.MagnitudeNumerator = 200;
		Recoil.MagnitudeDenominator = 1;
		Move.Effects.Add(Recoil);
		return Move;
	}

	FBattleDefinitionCatalog MakeCatalog()
	{
		FBattleDefinitionCatalogInput Input;
		Input.TypeChartEntries = MakeNeutralTypeChart();
		Input.Moves.Add(MakeDamageMove(
			NormalMoveName,
			EPokemonType::Normal,
			40));
		Input.Moves.Add(MakeDamageMove(
			SecondMoveName,
			EPokemonType::Normal,
			40));
		Input.Moves.Add(MakeDamageMove(
			NegativeMoveName,
			EPokemonType::Normal,
			40,
			-1));
		Input.Moves.Add(MakeStatusMove());
		Input.Moves.Add(MakeDamageMove(
			GroundMoveName,
			EPokemonType::Ground,
			1000));
		Input.Moves.Add(MakeMultiHitMove(MultiHitMoveName, 20));
		Input.Moves.Add(MakeMultiHitMove(LethalMultiHitMoveName, 1000));
		Input.Moves.Add(MakeForcedSwitchMove());
		Input.Moves.Add(MakeIndirectRecoilMove());
		for (const FAbilityId& AbilityId : FBattleAbilityRules::GetCanonicalIds())
		{
			Input.Abilities.Add({AbilityId});
		}
		for (const FItemId& ItemId : FBattleItemRules::GetCanonicalIds())
		{
			Input.Items.Add({ItemId, EBattleItemKind::Held});
		}
		for (const FConditionId& StatusId : FBattleMajorStatusRules::GetCanonicalIds())
		{
			Input.Conditions.Add({StatusId, EBattleConditionKind::MajorStatus});
		}
		for (const FConditionId& ConditionId :
			FBattleFieldSideConditionRules::GetCanonicalIds())
		{
			Input.Conditions.Add({
				ConditionId,
				FBattleFieldSideConditionRules::GetConditionFamily(ConditionId)});
		}
		for (const FConditionId& VolatileId : FBattleVolatileRules::GetCanonicalIds())
		{
			Input.Conditions.Add({VolatileId, EBattleConditionKind::Volatile});
		}
		for (const TCHAR* SpeciesName : {PlayerSpeciesName, OpponentSpeciesName})
		{
			FBattleSpeciesFormDefinition Species;
			Species.Id = MakeDefinitionId<FSpeciesFormId>(SpeciesName);
			Species.PrimaryType = EPokemonType::Normal;
			Species.BaseStats = {80, 80, 80, 80, 80, 80};
			Species.CatchRate = 45;
			Species.AbilityChoices = FBattleAbilityRules::GetCanonicalIds();
			Input.SpeciesForms.Add(MoveTemp(Species));
		}
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
				? TEXT("Selector.C08C.Player")
				: TEXT("Selector.C08C.Opponent"));
		return Trainer;
	}

	FBattlePartyEntrySetup MakePartyEntry(
		const uint64 TrainerValue,
		const uint64 BattlerValue,
		const uint8 PartyIndex,
		const TCHAR* SpeciesName,
		const FAbilityId& AbilityId,
		const FItemId& HeldItemId,
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
		Entry.AbilityId = AbilityId;
		Entry.OriginalHeldItemId = HeldItemId;
		Entry.CurrentHeldItemId = HeldItemId;
		const TArray<FMoveId> MoveIds = {
			MakeDefinitionId<FMoveId>(NormalMoveName),
			MakeDefinitionId<FMoveId>(SecondMoveName),
			MakeDefinitionId<FMoveId>(NegativeMoveName),
			MakeDefinitionId<FMoveId>(StatusMoveName)};
		for (int32 Index = 0; Index < MoveIds.Num(); ++Index)
		{
			FBattleMoveSlotSetup Move;
			Move.SlotIndex = static_cast<uint8>(Index);
			Move.MoveId = MoveIds[Index];
			Move.CurrentPP = 20;
			Move.MaxPP = 20;
			Entry.Moves.Add(Move);
		}
		return Entry;
	}

	FBattleSetup MakeSetup(const FC08CScenario& Scenario)
	{
		FBattleSetupInput Input;
		Input.BattleId = MakeNumericId<FBattleId>(808);
		Input.SettingsReference = {
			MakeDefinitionId<FDefinitionId>(TEXT("Settings.C08C")),
			1};
		Input.CatalogReference = {
			MakeDefinitionId<FDefinitionId>(TEXT("Catalog.C08C")),
			1};
		Input.EncounterKind = EBattleEncounterKind::Trainer;
		Input.Format = EBattleFormat::Single;
		Input.CaptureCapacity = {4, 100};
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
			PlayerValue,
			0,
			PlayerSpeciesName,
			Scenario.PlayerAbility,
			Scenario.PlayerItem,
			Scenario.PlayerHP,
			Scenario.PlayerSpeed));
		Input.PartyEntries.Add(MakePartyEntry(
			PlayerTrainerValue,
			PlayerReserveValue,
			1,
			PlayerSpeciesName,
			FBattleAbilityRules::GetBlazeId(),
			Scenario.PlayerReserveItem,
			Scenario.PlayerReserveHP,
			100));
		Input.PartyEntries.Add(MakePartyEntry(
			OpponentTrainerValue,
			OpponentValue,
			0,
			OpponentSpeciesName,
			Scenario.OpponentAbility,
			Scenario.OpponentItem,
			Scenario.OpponentHP,
			Scenario.OpponentSpeed));
		Input.PartyEntries.Add(MakePartyEntry(
			OpponentTrainerValue,
			OpponentReserveValue,
			1,
			OpponentSpeciesName,
			FBattleAbilityRules::GetBlazeId(),
			Scenario.OpponentReserveItem,
			200,
			90));
		Input.StartingActive.Add({
			MakeActiveSlotId(EBattleSide::Player, EBattlePosition::Left),
			MakeNumericId<FTrainerId>(PlayerTrainerValue),
			MakeNumericId<FBattlerId>(PlayerValue)});
		Input.StartingActive.Add({
			MakeActiveSlotId(EBattleSide::Opponent, EBattlePosition::Left),
			MakeNumericId<FTrainerId>(OpponentTrainerValue),
			MakeNumericId<FBattlerId>(OpponentValue)});
		for (const uint64 BattlerValue : {PlayerValue, PlayerReserveValue})
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
		const FC08CScenario& Scenario,
		TUniquePtr<IBattleRandom>&& Random = MakeUnique<FSeededBattleRandom>(808))
	{
		TUniquePtr<FBattleEngine> Engine;
		FBattleRejection Rejection;
		const bool bCreated = FBattleEngine::TryCreate(
			MakeSetup(Scenario),
			MakeCatalog(),
			MoveTemp(Random),
			Engine,
			Rejection);
		check(bCreated);
		return Engine;
	}

	bool BeginRuntime(FBattleEngine& Engine, FBattleResolution* OutResolution = nullptr)
	{
		FBattleRejection Rejection;
		const bool bStarted = Engine.TryBeginActionDecisionSequence(Rejection);
		if (OutResolution != nullptr)
		{
			const TArray<FBattleResolution>& Resolutions =
				FBattleC08CEngineFixture::GetState(Engine).Resolutions;
			if (!Resolutions.IsEmpty())
			{
				*OutResolution = Resolutions.Last();
			}
		}
		return bStarted;
	}

	int32 FindExecutionEvent(
		const FBattleEffectExecutionResult& Result,
		const EBattleEventType Type,
		const int32 Occurrence = 0)
	{
		int32 Seen = 0;
		for (int32 Index = 0; Index < Result.Events.Num(); ++Index)
		{
			if (Result.Events[Index].Type == Type && Seen++ == Occurrence)
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

	bool HasResolutionEvent(
		const FBattleResolution& Resolution,
		const EBattleEventType Type,
		const FDefinitionId& DefinitionId = FDefinitionId())
	{
		return Resolution.GetEvents().ContainsByPredicate(
			[Type, &DefinitionId](const FBattleEvent& Event)
			{
				return Event.GetType() == Type
					&& (!DefinitionId.IsValid()
						|| Event.GetSource().DefinitionId == DefinitionId);
			});
	}

	bool SubmitFight(FBattleEngine& Engine, const FMoveId& MoveId)
	{
		const TOptional<FBattleDecisionRequest> Pending = Engine.GetPendingDecision();
		if (!Pending.IsSet())
		{
			return false;
		}
		const FBattleDecisionRequest Request = Pending.GetValue();
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
			const FBattleMoveTargetOption* Target =
				Request.GetLegalMoveTargets().FindByPredicate(
					[&MoveId](const FBattleMoveTargetOption& Option)
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
		return bCreated && Engine.SubmitDecision(Decision).WasAccepted();
	}

	bool LockTwoFights(
		FBattleEngine& Engine,
		const FMoveId& PlayerMove,
		const FMoveId& OpponentMove)
	{
		if (Engine.GetSnapshot().GetPhase() == EBattlePhase::Setup
			&& !BeginRuntime(Engine))
		{
			return false;
		}
		int32 Guard = 0;
		while (Engine.GetPendingDecision().IsSet() && Guard++ < 4)
		{
			const FBattlerId Actor =
				Engine.GetPendingDecision().GetValue().GetActingBattlerId();
			if (!SubmitFight(
				Engine,
				Actor == MakeNumericId<FBattlerId>(PlayerValue)
					? PlayerMove
					: OpponentMove))
			{
				return false;
			}
		}
		return Guard < 4 && Engine.GetSnapshot().GetPhase() == EBattlePhase::Locked;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC08CEngineAirBalloonTest,
		"PokemonSolarus.Battle.C08C.HeldItem.Engine.AirBalloonRevealGroundImmunityAndRemove",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FBattleC08CEngineAirBalloonTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		FC08CScenario Scenario;
		Scenario.PlayerReserveItem = FBattleItemRules::GetAirBalloonId();
		TUniquePtr<FBattleEngine> Engine = MakeEngine(Scenario);
		TestTrue(TEXT("The held-item runtime begins"), BeginRuntime(*Engine));
		TestTrue(TEXT("A Balloon reserve can be locked for switch-in"),
			FBattleC08CEngineFixture::PrepareLockedSwitch(
				*Engine,
				MakeNumericId<FBattlerId>(PlayerValue),
				MakePartySlotId(1)));
		TestTrue(TEXT("The Balloon switch action starts"),
			Engine->BeginNextLockedAction().WasAccepted());
		const FBattleResolution Switched = Engine->ExecuteCurrentSwitch();
		TestTrue(TEXT("The Balloon holder enters"), Switched.WasAccepted());
		TestTrue(TEXT("Switch-in publicly reveals the Air Balloon"),
			HasResolutionEvent(
				Switched,
				EBattleEventType::ItemActivated,
				FBattleItemRules::GetAirBalloonId().GetDefinitionId()));

		const FBattlerId HolderId = MakeNumericId<FBattlerId>(PlayerReserveValue);
		const FBattlerId AttackerId = MakeNumericId<FBattlerId>(OpponentValue);
		const FBattleBattlerState* Holder =
			FBattleC08CEngineFixture::GetBattler(*Engine, HolderId);
		TestTrue(TEXT("The mirror records the first reveal"),
			Holder != nullptr && Holder->HeldItem.bRevealed);
		FBattleEffectExecutionResult GroundResult;
		TestTrue(TEXT("The live Ground move resolves against Air Balloon"),
			FBattleC08CEngineFixture::ExecuteCatalogMove(
				*Engine,
				AttackerId,
				MakeDefinitionId<FMoveId>(GroundMoveName),
				HolderId,
				GroundResult,
				80811));
		TestEqual(TEXT("Ground immunity preserves HP"),
			FBattleC08CEngineFixture::GetBattler(*Engine, HolderId)->CurrentHP,
			200);
		TestTrue(TEXT("Ground immunity emits an immunity fact"),
			FindExecutionEvent(GroundResult, EBattleEventType::Immunity) != INDEX_NONE);
		Holder = FBattleC08CEngineFixture::GetBattler(*Engine, HolderId);
		TestTrue(TEXT("A fully blocked Ground move does not pop Air Balloon"),
			Holder != nullptr && !Holder->HeldItem.bTemporarilyRemoved);

		FBattleEffectExecutionResult PopResult;
		TestTrue(TEXT("A connected non-Ground damaging hit resolves"),
			FBattleC08CEngineFixture::ExecuteCatalogMove(
				*Engine,
				AttackerId,
				MakeDefinitionId<FMoveId>(NormalMoveName),
				HolderId,
				PopResult,
				80812));
		Holder = FBattleC08CEngineFixture::GetBattler(*Engine, HolderId);
		TestTrue(TEXT("The damaging hit removes rather than consumes the Balloon"),
			Holder != nullptr
				&& Holder->HeldItem.bTemporarilyRemoved
				&& !Holder->HeldItem.bConsumed);
		TestTrue(TEXT("Balloon removal is public and typed"),
			FindExecutionEvent(PopResult, EBattleEventType::ItemRemoved) != INDEX_NONE);
		const FBattleHeldItemInstanceState* LedgerState =
			FBattleC08CEngineFixture::GetState(*Engine).HeldItemLedger.FindState(
				Holder->HeldItem.InstanceId);
		TestTrue(TEXT("Ledger and battler mirror agree on temporary removal"),
			LedgerState != nullptr
				&& LedgerState->bTemporarilyRemoved
				&& !LedgerState->bConsumed);
		const FBattlePartyEntrySetup* SnapshotEntry =
			Engine->GetSnapshot().GetPartyEntries().FindByPredicate(
				[HolderId](const FBattlePartyEntrySetup& Entry)
				{
					return Entry.BattlerId == HolderId;
				});
		TestTrue(TEXT("A removed Balloon is absent from the battle snapshot"),
			SnapshotEntry != nullptr && !SnapshotEntry->CurrentHeldItemId.IsValid());
		const FBattleSnapshot OpponentView = Engine->GetSnapshotForObserver(
			MakeNumericId<FTrainerId>(OpponentTrainerValue));
		const FBattleObservedBattler* ObservedHolder =
			OpponentView.FindObservedBattler(HolderId);
		TestTrue(TEXT("Public Balloon removal is projected as known empty"),
			ObservedHolder != nullptr
				&& ObservedHolder->bHeldItemKnown
				&& !ObservedHolder->HeldItemId.IsValid());

		FBattleEffectExecutionResult ForcedOut;
		TestTrue(TEXT("A forced-switch hit can move the popped Balloon holder out"),
			FBattleC08CEngineFixture::ExecuteCatalogMove(
				*Engine,
				AttackerId,
				MakeDefinitionId<FMoveId>(ForcedSwitchMoveName),
				HolderId,
				ForcedOut,
				80815)
				&& ForcedOut.SwitchIntents.Num() == 1
				&& ForcedOut.SwitchIntents[0].bApplied);
		FBattleEffectExecutionResult ForcedReentry;
		TestTrue(TEXT("A forced-switch can re-enter the popped Balloon through no-op hooks"),
			FBattleC08CEngineFixture::ExecuteCatalogMove(
				*Engine,
				AttackerId,
				MakeDefinitionId<FMoveId>(ForcedSwitchMoveName),
				MakeNumericId<FBattlerId>(PlayerValue),
				ForcedReentry,
				80816)
				&& ForcedReentry.SwitchIntents.Num() == 1
				&& ForcedReentry.SwitchIntents[0].bApplied);
		Holder = FBattleC08CEngineFixture::GetBattler(*Engine, HolderId);
		TestTrue(TEXT("Re-entry succeeds without restoring or revealing the removed Balloon"),
			Holder != nullptr
				&& Holder->HeldItem.bTemporarilyRemoved
				&& FindExecutionEvent(
					ForcedReentry,
					EBattleEventType::ItemActivated) == INDEX_NONE);

		FC08CScenario SubstituteScenario;
		SubstituteScenario.OpponentItem = FBattleItemRules::GetAirBalloonId();
		TUniquePtr<FBattleEngine> Substitute = MakeEngine(SubstituteScenario);
		TestTrue(TEXT("Substitute Balloon runtime begins"), BeginRuntime(*Substitute));
		const FBattlerId SubstituteHolderId = MakeNumericId<FBattlerId>(OpponentValue);
		TestTrue(TEXT("A Substitute is installed on the Balloon holder"),
			FBattleC08CEngineFixture::AddVolatile(
				*Substitute,
				SubstituteHolderId,
				FBattleVolatileRules::GetSubstituteId(),
				SubstituteHolderId,
				50));
		FBattleEffectExecutionResult SubstituteHit;
		TestTrue(TEXT("A damaging Substitute hit resolves"),
			FBattleC08CEngineFixture::ExecuteCatalogMove(
				*Substitute,
				MakeNumericId<FBattlerId>(PlayerValue),
				MakeDefinitionId<FMoveId>(NormalMoveName),
				SubstituteHolderId,
				SubstituteHit,
				80813));
		const FBattleBattlerState* SubstituteHolder =
			FBattleC08CEngineFixture::GetBattler(*Substitute, SubstituteHolderId);
		TestTrue(TEXT("A connected damaging Substitute hit pops Air Balloon"),
			SubstituteHolder != nullptr
				&& SubstituteHolder->CurrentHP == 200
				&& SubstituteHolder->HeldItem.bTemporarilyRemoved
				&& FindExecutionEvent(SubstituteHit, EBattleEventType::ItemRemoved)
					!= INDEX_NONE);

		TUniquePtr<FBattleEngine> NonDamaging = MakeEngine(SubstituteScenario);
		TestTrue(TEXT("Non-damaging Balloon runtime begins"), BeginRuntime(*NonDamaging));
		FBattleEffectExecutionResult StatusResult;
		TestTrue(TEXT("A non-damaging status move resolves"),
			FBattleC08CEngineFixture::ExecuteCatalogMove(
				*NonDamaging,
				MakeNumericId<FBattlerId>(PlayerValue),
				MakeDefinitionId<FMoveId>(StatusMoveName),
				SubstituteHolderId,
				StatusResult,
				80814));
		const FBattleBattlerState* NonDamagingHolder =
			FBattleC08CEngineFixture::GetBattler(*NonDamaging, SubstituteHolderId);
		TestTrue(TEXT("A non-damaging reached move does not pop Air Balloon"),
			NonDamagingHolder != nullptr
				&& !NonDamagingHolder->HeldItem.bTemporarilyRemoved
				&& FindExecutionEvent(StatusResult, EBattleEventType::ItemRemoved)
					== INDEX_NONE);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC08CEngineBootsTest,
		"PokemonSolarus.Battle.C08C.HeldItem.Engine.HeavyDutyBootsAllHazardsAndSuppression",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FBattleC08CEngineBootsTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		auto RunEntry = [this](const bool bSuppressed)
		{
			FC08CScenario Scenario;
			Scenario.PlayerReserveItem = FBattleItemRules::GetHeavyDutyBootsId();
			TUniquePtr<FBattleEngine> Engine = MakeEngine(Scenario);
			TestTrue(TEXT("Boots runtime begins"), BeginRuntime(*Engine));
			const FBattlerId Source = MakeNumericId<FBattlerId>(OpponentValue);
			for (const TPair<FConditionId, int32>& Hazard : {
				TPair<FConditionId, int32>(FBattleFieldSideConditionRules::GetSpikesId(), 3),
				TPair<FConditionId, int32>(FBattleFieldSideConditionRules::GetToxicSpikesId(), 2),
				TPair<FConditionId, int32>(FBattleFieldSideConditionRules::GetStealthRockId(), 1),
				TPair<FConditionId, int32>(FBattleFieldSideConditionRules::GetStickyWebId(), 1)})
			{
				TestTrue(TEXT("Each approved entry hazard is seeded"),
					FBattleC08CEngineFixture::SeedCondition(
						*Engine,
						Hazard.Key,
						EBattleSide::Player,
						Source,
						Hazard.Value));
			}
			const FBattlerId Incoming = MakeNumericId<FBattlerId>(PlayerReserveValue);
			if (bSuppressed)
			{
				TestTrue(TEXT("Magic Room suppresses Boots through the live field hook"),
					FBattleC08CEngineFixture::SeedCondition(
						*Engine,
						FBattleFieldSideConditionRules::GetMagicRoomId(),
						EBattleSide::Player,
						Source));
			}
			TestTrue(TEXT("The hazard entry switch is prepared"),
				FBattleC08CEngineFixture::PrepareLockedSwitch(
					*Engine,
					MakeNumericId<FBattlerId>(PlayerValue),
					MakePartySlotId(1)));
			TestTrue(TEXT("The hazard entry switch starts"),
				Engine->BeginNextLockedAction().WasAccepted());
			TestTrue(TEXT("The hazard entry switch resolves"),
				Engine->ExecuteCurrentSwitch().WasAccepted());
			const FBattleBattlerState* Battler =
				FBattleC08CEngineFixture::GetBattler(*Engine, Incoming);
			int32 SpeedStage = 0;
			const bool bHasSpeedStage = Battler != nullptr
				&& Battler->Stages.TryGetStage(EBattleStat::Speed, SpeedStage);
			TestTrue(TEXT("The incoming holder remains inspectable"),
				Battler != nullptr && bHasSpeedStage);
			if (bSuppressed)
			{
				TestTrue(TEXT("Suppressed Boots allow hazard damage"),
					Battler->CurrentHP < 200);
				TestTrue(TEXT("Suppressed Boots allow Toxic Spikes"),
					Battler->MajorStatusId.IsValid());
				TestEqual(TEXT("Suppressed Boots allow Sticky Web"), SpeedStage, -1);
			}
			else
			{
				TestEqual(TEXT("Active Boots bypass hazard damage"), Battler->CurrentHP, 200);
				TestFalse(TEXT("Active Boots bypass Toxic Spikes"),
					Battler->MajorStatusId.IsValid());
				TestEqual(TEXT("Active Boots bypass Sticky Web"), SpeedStage, 0);
			}
		};
		RunEntry(false);
		RunEntry(true);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC08CEngineBerryTimingTest,
		"PokemonSolarus.Battle.C08C.HeldItem.Engine.SitrusAndLumImmediatePerHitTiming",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FBattleC08CEngineBerryTimingTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		FC08CScenario SitrusScenario;
		SitrusScenario.OpponentItem = FBattleItemRules::GetSitrusBerryId();
		SitrusScenario.OpponentHP = 105;
		TUniquePtr<FBattleEngine> Sitrus = MakeEngine(SitrusScenario);
		TestTrue(TEXT("Sitrus starts above half without consuming"), BeginRuntime(*Sitrus));
		FBattleEffectExecutionResult SitrusResult;
		TestTrue(TEXT("The fixed two-hit move resolves against Sitrus"),
			FBattleC08CEngineFixture::ExecuteCatalogMove(
				*Sitrus,
				MakeNumericId<FBattlerId>(PlayerValue),
				MakeDefinitionId<FMoveId>(MultiHitMoveName),
				MakeNumericId<FBattlerId>(OpponentValue),
				SitrusResult,
				80821));
		const int32 Consumed = FindExecutionEvent(
			SitrusResult,
			EBattleEventType::ItemConsumed);
		const int32 Healing = FindExecutionEvent(SitrusResult, EBattleEventType::Healing);
		const int32 SecondDamage = FindExecutionEvent(
			SitrusResult,
			EBattleEventType::Damage,
			1);
		TestTrue(TEXT("Sitrus consumes before healing between reached hits"),
			Consumed != INDEX_NONE && Consumed < Healing && Healing < SecondDamage);
		const FBattleBattlerState* SitrusHolder = FBattleC08CEngineFixture::GetBattler(
			*Sitrus,
			MakeNumericId<FBattlerId>(OpponentValue));
		TestTrue(TEXT("Sitrus consumption is mirrored"),
			SitrusHolder != nullptr && SitrusHolder->HeldItem.bConsumed);

		FC08CScenario LumScenario;
		LumScenario.OpponentItem = FBattleItemRules::GetLumBerryId();
		TUniquePtr<FBattleEngine> Lum = MakeEngine(LumScenario);
		TestTrue(TEXT("Lum runtime begins before conditions exist"), BeginRuntime(*Lum));
		const FBattlerId LumHolderId = MakeNumericId<FBattlerId>(OpponentValue);
		TestTrue(TEXT("A major status is added after entry"),
			FBattleC08CEngineFixture::AddMajorStatus(
				*Lum,
				LumHolderId,
				FBattleMajorStatusRules::GetBurnId()));
		TestTrue(TEXT("Confusion is added alongside the major status"),
			FBattleC08CEngineFixture::AddVolatile(
				*Lum,
				LumHolderId,
				FBattleVolatileRules::GetConfusionId(),
				MakeNumericId<FBattlerId>(PlayerValue)));
		FBattleEffectExecutionResult LumResult;
		TestTrue(TEXT("A reached hit runs Lum's immediate update"),
			FBattleC08CEngineFixture::ExecuteCatalogMove(
				*Lum,
				MakeNumericId<FBattlerId>(PlayerValue),
				MakeDefinitionId<FMoveId>(NormalMoveName),
				LumHolderId,
				LumResult,
				80822));
		const FBattleBattlerState* LumHolder =
			FBattleC08CEngineFixture::GetBattler(*Lum, LumHolderId);
		TestTrue(TEXT("Lum consumes before one atomic two-condition cure"),
			FindExecutionEvent(LumResult, EBattleEventType::ItemConsumed)
				< FindExecutionEvent(LumResult, EBattleEventType::StatusChanged));
		TestTrue(TEXT("Lum cures the major status and Confusion together"),
			LumHolder != nullptr
				&& LumHolder->HeldItem.bConsumed
				&& !LumHolder->MajorStatusId.IsValid()
				&& !LumHolder->Volatiles.ContainsByPredicate(
					[](const FBattleConditionState& Condition)
					{
						return Condition.ConditionId
							== FBattleVolatileRules::GetConfusionId();
					}));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC08CEngineFocusSashTest,
		"PokemonSolarus.Battle.C08C.HeldItem.Engine.FocusSashPerHitConsumeAndSubstituteBoundary",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FBattleC08CEngineFocusSashTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		FC08CScenario Scenario;
		Scenario.OpponentItem = FBattleItemRules::GetFocusSashId();
		TUniquePtr<FBattleEngine> Engine = MakeEngine(Scenario);
		TestTrue(TEXT("Focus Sash runtime begins"), BeginRuntime(*Engine));
		FBattleEffectExecutionResult Result;
		TestTrue(TEXT("A fixed lethal two-hit move resolves"),
			FBattleC08CEngineFixture::ExecuteCatalogMove(
				*Engine,
				MakeNumericId<FBattlerId>(PlayerValue),
				MakeDefinitionId<FMoveId>(LethalMultiHitMoveName),
				MakeNumericId<FBattlerId>(OpponentValue),
				Result,
				80831));
		const FBattleBattlerState* Holder = FBattleC08CEngineFixture::GetBattler(
			*Engine,
			MakeNumericId<FBattlerId>(OpponentValue));
		TestTrue(TEXT("Sash is consumed on the first lethal hit and a later hit can KO"),
			Holder != nullptr
				&& Holder->HeldItem.bConsumed
				&& Holder->CurrentHP == 0
				&& Holder->bFainted);
		TestTrue(TEXT("Sash consumption precedes the adjusted first damage event"),
			FindExecutionEvent(Result, EBattleEventType::ItemConsumed)
				< FindExecutionEvent(Result, EBattleEventType::Damage));
		TestEqual(TEXT("Both hits reached the target"),
			Result.CompletedHitsPerDamageTarget.IsEmpty()
				? 0
				: Result.CompletedHitsPerDamageTarget[0],
			2);

		TUniquePtr<FBattleEngine> Substitute = MakeEngine(Scenario);
		TestTrue(TEXT("Substitute boundary runtime begins"), BeginRuntime(*Substitute));
		TestTrue(TEXT("A Substitute is installed before the hit"),
			FBattleC08CEngineFixture::AddVolatile(
				*Substitute,
				MakeNumericId<FBattlerId>(OpponentValue),
				FBattleVolatileRules::GetSubstituteId(),
				MakeNumericId<FBattlerId>(OpponentValue),
				50));
		FBattleEffectExecutionResult SubstituteResult;
		TestTrue(TEXT("The lethal move routes into Substitute"),
			FBattleC08CEngineFixture::ExecuteCatalogMove(
				*Substitute,
				MakeNumericId<FBattlerId>(PlayerValue),
				MakeDefinitionId<FMoveId>(GroundMoveName),
				MakeNumericId<FBattlerId>(OpponentValue),
				SubstituteResult,
				80832));
		Holder = FBattleC08CEngineFixture::GetBattler(
			*Substitute,
			MakeNumericId<FBattlerId>(OpponentValue));
		TestTrue(TEXT("Substitute damage neither consumes nor activates Focus Sash"),
			Holder != nullptr
				&& !Holder->HeldItem.bConsumed
				&& FindExecutionEvent(SubstituteResult, EBattleEventType::ItemConsumed)
					== INDEX_NONE);

		FC08CScenario IndirectScenario;
		IndirectScenario.PlayerItem = FBattleItemRules::GetFocusSashId();
		TUniquePtr<FBattleEngine> Indirect = MakeEngine(IndirectScenario);
		TestTrue(TEXT("Indirect-damage Focus Sash runtime begins"),
			BeginRuntime(*Indirect));
		FBattleEffectExecutionResult IndirectResult;
		TestTrue(TEXT("A fixed lethal recoil descriptor resolves"),
			FBattleC08CEngineFixture::ExecuteCatalogMove(
				*Indirect,
				MakeNumericId<FBattlerId>(PlayerValue),
				MakeDefinitionId<FMoveId>(IndirectRecoilMoveName),
				MakeNumericId<FBattlerId>(OpponentValue),
				IndirectResult,
				80833));
		const FBattleBattlerState* IndirectHolder =
			FBattleC08CEngineFixture::GetBattler(
				*Indirect,
				MakeNumericId<FBattlerId>(PlayerValue));
		TestTrue(TEXT("Focus Sash does not prevent or consume on lethal indirect recoil"),
			IndirectHolder != nullptr
				&& IndirectHolder->CurrentHP == 0
				&& IndirectHolder->bFainted
				&& !IndirectHolder->HeldItem.bConsumed
				&& FindExecutionEvent(IndirectResult, EBattleEventType::ItemConsumed)
					== INDEX_NONE);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC08CEngineLifeOrbTest,
		"PokemonSolarus.Battle.C08C.HeldItem.Engine.LifeOrbDamageRecoilMagicGuardAndForcedSwitch",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FBattleC08CEngineLifeOrbTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		const FBattlerId PlayerId = MakeNumericId<FBattlerId>(PlayerValue);
		const FBattlerId OpponentId = MakeNumericId<FBattlerId>(OpponentValue);
		const FMoveId NormalMove = MakeDefinitionId<FMoveId>(NormalMoveName);
		TUniquePtr<FBattleEngine> Baseline = MakeEngine(FC08CScenario());
		TestTrue(TEXT("The neutral damage baseline begins"), BeginRuntime(*Baseline));
		FBattleEffectExecutionResult BaselineResult;
		TestTrue(TEXT("The neutral comparison move resolves"),
			FBattleC08CEngineFixture::ExecuteCatalogMove(
				*Baseline,
				PlayerId,
				NormalMove,
				OpponentId,
				BaselineResult,
				80840));
		const int32 BaselineDamage = 200
			- FBattleC08CEngineFixture::GetBattler(*Baseline, OpponentId)->CurrentHP;

		FC08CScenario Scenario;
		Scenario.PlayerItem = FBattleItemRules::GetLifeOrbId();
		TUniquePtr<FBattleEngine> Engine = MakeEngine(Scenario);
		TestTrue(TEXT("Life Orb runtime begins"), BeginRuntime(*Engine));
		FBattleEffectExecutionResult Result;
		TestTrue(TEXT("Life Orb modifies a live damaging move"),
			FBattleC08CEngineFixture::ExecuteCatalogMove(
				*Engine,
				PlayerId,
				NormalMove,
				OpponentId,
				Result,
				80841));
		const int32 LifeOrbDamage = 200
			- FBattleC08CEngineFixture::GetBattler(*Engine, OpponentId)->CurrentHP;
		TestTrue(TEXT("Life Orb increases the live final damage"),
			BaselineDamage > 0 && LifeOrbDamage > BaselineDamage);
		TestEqual(TEXT("Life Orb applies one tenth maximum-HP recoil after the move"),
			FBattleC08CEngineFixture::GetBattler(*Engine, PlayerId)->CurrentHP,
			180);
		TestTrue(TEXT("Life Orb activation is publicly represented"),
			FindExecutionEvent(Result, EBattleEventType::ItemActivated) != INDEX_NONE);

		FC08CScenario GuardScenario = Scenario;
		GuardScenario.PlayerAbility = FBattleAbilityRules::GetMagicGuardId();
		TUniquePtr<FBattleEngine> Guard = MakeEngine(GuardScenario);
		TestTrue(TEXT("Magic Guard Life Orb runtime begins"), BeginRuntime(*Guard));
		FBattleEffectExecutionResult GuardResult;
		TestTrue(TEXT("Magic Guard holder still deals Life Orb damage"),
			FBattleC08CEngineFixture::ExecuteCatalogMove(
				*Guard,
				PlayerId,
				NormalMove,
				OpponentId,
				GuardResult,
				80842));
		TestEqual(TEXT("Magic Guard does not suppress Life Orb's damage modifier"),
			200 - FBattleC08CEngineFixture::GetBattler(*Guard, OpponentId)->CurrentHP,
			LifeOrbDamage);
		TestEqual(TEXT("Magic Guard blocks only Life Orb recoil"),
			FBattleC08CEngineFixture::GetBattler(*Guard, PlayerId)->CurrentHP,
			200);
		TestTrue(TEXT("Magic Guard prevention is publicly represented"),
			FindExecutionEvent(GuardResult, EBattleEventType::AbilityActivated)
				!= INDEX_NONE);
		const FBattleBattlerState* GuardHolder =
			FBattleC08CEngineFixture::GetBattler(*Guard, PlayerId);
		TestTrue(TEXT("Prevented recoil and invisible damage do not reveal Life Orb"),
			GuardHolder != nullptr
				&& !GuardHolder->HeldItem.bRevealed
				&& FindExecutionEvent(GuardResult, EBattleEventType::ItemActivated)
					== INDEX_NONE);

		FC08CScenario FaintScenario = Scenario;
		FaintScenario.PlayerHP = 20;
		TUniquePtr<FBattleEngine> RecoilFaint = MakeEngine(FaintScenario);
		TestTrue(TEXT("Recoil-faint Life Orb runtime begins"), BeginRuntime(*RecoilFaint));
		FBattleEffectExecutionResult RecoilFaintResult;
		TestTrue(TEXT("Life Orb recoil can resolve a real faint transition"),
			FBattleC08CEngineFixture::ExecuteCatalogMove(
				*RecoilFaint,
				PlayerId,
				NormalMove,
				OpponentId,
				RecoilFaintResult,
				80844));
		const FBattleBattlerState* RecoilFaintedHolder =
			FBattleC08CEngineFixture::GetBattler(*RecoilFaint, PlayerId);
		TestTrue(TEXT("One-tenth recoil reaches zero and marks the holder fainted"),
			RecoilFaintedHolder != nullptr
				&& RecoilFaintedHolder->CurrentHP == 0
				&& RecoilFaintedHolder->bFainted
				&& RecoilFaintedHolder->bFaintTransitionPending);

		TUniquePtr<FBattleEngine> Forced = MakeEngine(Scenario);
		TestTrue(TEXT("Forced-switch Life Orb runtime begins"), BeginRuntime(*Forced));
		FBattleEffectExecutionResult ForcedResult;
		TestTrue(TEXT("The damaging forced-switch move resolves"),
			FBattleC08CEngineFixture::ExecuteCatalogMove(
				*Forced,
				PlayerId,
				MakeDefinitionId<FMoveId>(ForcedSwitchMoveName),
				OpponentId,
				ForcedResult,
				80843));
		TestTrue(TEXT("The forced-switch exception requires an applied switch"),
			ForcedResult.SwitchIntents.Num() == 1
				&& ForcedResult.SwitchIntents[0].bApplied);
		TestEqual(TEXT("An applied forced switch suppresses Life Orb recoil"),
			FBattleC08CEngineFixture::GetBattler(*Forced, PlayerId)->CurrentHP,
			200);
		const FBattleBattlerState* ForcedHolder =
			FBattleC08CEngineFixture::GetBattler(*Forced, PlayerId);
		TestTrue(TEXT("Forced-switch recoil suppression does not reveal Life Orb"),
			ForcedHolder != nullptr
				&& !ForcedHolder->HeldItem.bRevealed
				&& FindExecutionEvent(ForcedResult, EBattleEventType::ItemActivated)
					== INDEX_NONE);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC08CEngineChoiceBandTest,
		"PokemonSolarus.Battle.C08C.HeldItem.Engine.ChoiceBandSelectionCommitStruggleAndSwitchCleanup",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FBattleC08CEngineChoiceBandTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		FC08CScenario Scenario;
		Scenario.PlayerItem = FBattleItemRules::GetChoiceBandId();
		const FBattlerId PlayerId = MakeNumericId<FBattlerId>(PlayerValue);
		const FBattlerId OpponentId = MakeNumericId<FBattlerId>(OpponentValue);
		const FMoveId FirstMove = MakeDefinitionId<FMoveId>(NormalMoveName);
		const FMoveId SecondMove = MakeDefinitionId<FMoveId>(SecondMoveName);

		TUniquePtr<FBattleEngine> Baseline = MakeEngine(FC08CScenario());
		TUniquePtr<FBattleEngine> Damage = MakeEngine(Scenario);
		TestTrue(TEXT("Choice Band damage comparison runtimes begin"),
			BeginRuntime(*Baseline) && BeginRuntime(*Damage));
		FBattleEffectExecutionResult BaselineResult;
		FBattleEffectExecutionResult DamageResult;
		TestTrue(TEXT("Neutral and Choice Band physical moves resolve live"),
			FBattleC08CEngineFixture::ExecuteCatalogMove(
				*Baseline,
				PlayerId,
				FirstMove,
				OpponentId,
				BaselineResult,
				80850)
				&& FBattleC08CEngineFixture::ExecuteCatalogMove(
					*Damage,
					PlayerId,
					FirstMove,
					OpponentId,
					DamageResult,
					80851));
		const int32 BaselineDamage = 200
			- FBattleC08CEngineFixture::GetBattler(*Baseline, OpponentId)->CurrentHP;
		const int32 ChoiceDamage = 200
			- FBattleC08CEngineFixture::GetBattler(*Damage, OpponentId)->CurrentHP;
		const FBattleBattlerState* DamageHolder =
			FBattleC08CEngineFixture::GetBattler(*Damage, PlayerId);
		TestTrue(TEXT("Choice Band increases live physical damage without an invisible reveal"),
			BaselineDamage > 0
				&& ChoiceDamage > BaselineDamage
				&& DamageHolder != nullptr
				&& !DamageHolder->HeldItem.bRevealed
				&& FindExecutionEvent(DamageResult, EBattleEventType::ItemActivated)
					== INDEX_NONE);

		TUniquePtr<FBattleEngine> Engine = MakeEngine(Scenario);
		TestTrue(TEXT("Choice Band runtime begins"), BeginRuntime(*Engine));
		TestTrue(TEXT("Before a commit both physical moves are selectable"),
			Engine->GetPendingDecision().IsSet()
				&& Engine->GetPendingDecision().GetValue().GetLegalMoveIds().Contains(FirstMove)
				&& Engine->GetPendingDecision().GetValue().GetLegalMoveIds().Contains(SecondMove));
		TestTrue(TEXT("The selected moves lock into a queue"),
			LockTwoFights(*Engine, FirstMove, FirstMove));
		TestTrue(TEXT("The Choice Band holder starts first"),
			Engine->BeginNextLockedAction().WasAccepted()
				&& Engine->GetCurrentLockedAction().IsSet()
				&& Engine->GetCurrentLockedAction().GetValue().Decision.GetActingBattlerId()
					== MakeNumericId<FBattlerId>(PlayerValue));
		TestTrue(TEXT("The first successful commit consumes PP"),
			Engine->CommitCurrentMoveAfterPreMoveGates().WasAccepted());
		const FBattleBattlerState* Holder = FBattleC08CEngineFixture::GetBattler(
			*Engine,
			MakeNumericId<FBattlerId>(PlayerValue));
		TestTrue(TEXT("Choice lock is established only after commit"),
			Holder != nullptr && Holder->HeldItem.ChoiceLockedMoveId == FirstMove);
		TestTrue(TEXT("A voluntary switch can replace the locked holder"),
			FBattleC08CEngineFixture::PrepareLockedSwitch(
				*Engine,
				MakeNumericId<FBattlerId>(PlayerValue),
				MakePartySlotId(1))
				&& Engine->BeginNextLockedAction().WasAccepted()
				&& Engine->ExecuteCurrentSwitch().WasAccepted());
		Holder = FBattleC08CEngineFixture::GetBattler(
			*Engine,
			MakeNumericId<FBattlerId>(PlayerValue));
		TestTrue(TEXT("Switch-out clears the Choice lock"),
			Holder != nullptr && !Holder->HeldItem.ChoiceLockedMoveId.IsValid());

		TUniquePtr<FBattleEngine> Alternate = MakeEngine(Scenario);
		FBattleBattlerState* AlternateHolder =
			FBattleC08CEngineFixture::GetMutableState(*Alternate).FindMutableBattler(
				PlayerId);
		check(AlternateHolder != nullptr);
		AlternateHolder->HeldItem.ChoiceLockedMoveId = FirstMove;
		TestTrue(TEXT("A pre-locked Choice Band holder can begin selection"),
			BeginRuntime(*Alternate));
		const TOptional<FBattleDecisionRequest> AlternateRequest =
			Alternate->GetPendingDecision();
		const FBattleMoveSlotState* AlternateSlot = AlternateHolder->Moves.FindByPredicate(
			[&SecondMove](const FBattleMoveSlotState& Slot)
			{
				return Slot.MoveId == SecondMove;
			});
		const int32 AlternatePPBefore = AlternateSlot != nullptr
			? AlternateSlot->CurrentPP
			: INDEX_NONE;
		FBattleDecision AlternateDecision;
		const bool bAlternateCreated = AlternateRequest.IsSet()
			&& FBattleDecision::TryCreateFight(
				AlternateRequest->GetStateVersion(),
				AlternateRequest->GetDecisionOwnerTrainerId(),
				AlternateRequest->GetActingBattlerId(),
				SecondMove,
				MakeActiveSlotId(EBattleSide::Opponent, EBattlePosition::Left),
				AlternateDecision);
		TestTrue(TEXT("The alternate Choice move request is structurally valid"),
			bAlternateCreated);
		const FBattleResolution AlternateRejected = bAlternateCreated
			? Alternate->SubmitDecision(AlternateDecision)
			: FBattleResolution();
		AlternateHolder =
			FBattleC08CEngineFixture::GetMutableState(*Alternate).FindMutableBattler(
				PlayerId);
		AlternateSlot = AlternateHolder != nullptr
			? AlternateHolder->Moves.FindByPredicate(
				[&SecondMove](const FBattleMoveSlotState& Slot)
				{
					return Slot.MoveId == SecondMove;
				})
			: nullptr;
		TestTrue(TEXT("An alternate Choice move is rejected before PP changes"),
			!AlternateRejected.WasAccepted()
				&& AlternateSlot != nullptr
				&& AlternateSlot->CurrentPP == AlternatePPBefore);

		TUniquePtr<FBattleEngine> Stale = MakeEngine(Scenario);
		TestTrue(TEXT("A Choice action can become stale after queue lock"),
			LockTwoFights(*Stale, SecondMove, FirstMove));
		FBattleBattlerState* StaleHolder =
			FBattleC08CEngineFixture::GetMutableState(*Stale).FindMutableBattler(PlayerId);
		check(StaleHolder != nullptr);
		StaleHolder->HeldItem.ChoiceLockedMoveId = FirstMove;
		const FBattleMoveSlotState* StaleSlot = StaleHolder->Moves.FindByPredicate(
			[&SecondMove](const FBattleMoveSlotState& Slot)
			{
				return Slot.MoveId == SecondMove;
			});
		const int32 StalePPBefore = StaleSlot != nullptr
			? StaleSlot->CurrentPP
			: INDEX_NONE;
		TestTrue(TEXT("The stale Choice action reaches its live commit checkpoint"),
			Stale->BeginNextLockedAction().WasAccepted());
		const FBattleResolution StaleCanceled =
			Stale->CommitCurrentMoveAfterPreMoveGates();
		StaleHolder =
			FBattleC08CEngineFixture::GetMutableState(*Stale).FindMutableBattler(PlayerId);
		StaleSlot = StaleHolder != nullptr
			? StaleHolder->Moves.FindByPredicate(
				[&SecondMove](const FBattleMoveSlotState& Slot)
				{
					return Slot.MoveId == SecondMove;
				})
			: nullptr;
		TestTrue(TEXT("The stale Choice action cancels generically before PP changes"),
			StaleCanceled.WasAccepted()
				&& StaleSlot != nullptr
				&& StaleSlot->CurrentPP == StalePPBefore
				&& StaleCanceled.GetEvents().ContainsByPredicate(
					[](const FBattleEvent& Event)
					{
						return Event.GetType() == EBattleEventType::EffectPrevented
							&& Event.GetCause() == EBattleEventCause::Rule;
					})
				&& StaleCanceled.GetEvents().ContainsByPredicate(
					[](const FBattleEvent& Event)
					{
						return Event.GetType() == EBattleEventType::ActionCanceled
							&& Event.GetCause() == EBattleEventCause::Rule;
					}));
		const FDefinitionId ChoiceDefinition =
			FBattleItemRules::GetChoiceBandId().GetDefinitionId();
		TestFalse(TEXT("Stale Choice cancellation exposes no item cause or definition"),
			StaleCanceled.GetEvents().ContainsByPredicate(
				[&ChoiceDefinition](const FBattleEvent& Event)
				{
					return Event.GetCause() == EBattleEventCause::Item
						|| Event.GetSource().DefinitionId == ChoiceDefinition
						|| (Event.GetVisibility().bRevealSourceDefinition
							&& Event.GetSource().DefinitionId == ChoiceDefinition);
				}));
		const FBattleSnapshot StaleOpponentView = Stale->GetSnapshotForObserver(
			MakeNumericId<FTrainerId>(OpponentTrainerValue));
		const FBattleObservedBattler* StaleObservedHolder =
			StaleOpponentView.FindObservedBattler(PlayerId);
		TestTrue(TEXT("Stale Choice cancellation leaves the held item unrevealed"),
			StaleHolder != nullptr
				&& !StaleHolder->HeldItem.bRevealed
				&& StaleObservedHolder != nullptr
				&& !StaleObservedHolder->bHeldItemKnown);

		FC08CScenario FaintScenario = Scenario;
		FaintScenario.PlayerHP = 1;
		TUniquePtr<FBattleEngine> Faint = MakeEngine(FaintScenario);
		TestTrue(TEXT("The live Choice faint scenario locks both actions"),
			LockTwoFights(*Faint, FirstMove, FirstMove));
		TestTrue(TEXT("The Choice holder commits and executes its move"),
			Faint->BeginNextLockedAction().WasAccepted()
				&& Faint->CommitCurrentMoveAfterPreMoveGates().WasAccepted()
				&& Faint->ResolveCurrentMoveTargets().WasAccepted()
				&& Faint->ExecuteCurrentMoveEffects().WasAccepted());
		const FBattleBattlerState* FaintHolder =
			FBattleC08CEngineFixture::GetBattler(*Faint, PlayerId);
		TestTrue(TEXT("The live Choice lock exists before the opposing hit"),
			FaintHolder != nullptr
				&& FaintHolder->HeldItem.ChoiceLockedMoveId == FirstMove);
		TestTrue(TEXT("The opposing action resolves the real faint transition"),
			Faint->BeginNextLockedAction().WasAccepted()
				&& Faint->CommitCurrentMoveAfterPreMoveGates().WasAccepted()
				&& Faint->ResolveCurrentMoveTargets().WasAccepted()
				&& Faint->ExecuteCurrentMoveEffects().WasAccepted());
		FaintHolder = FBattleC08CEngineFixture::GetBattler(*Faint, PlayerId);
		TestTrue(TEXT("Actual faint cleanup clears the Choice lock"),
			FaintHolder != nullptr
				&& FaintHolder->bFainted
				&& !FaintHolder->HeldItem.ChoiceLockedMoveId.IsValid());

		TUniquePtr<FBattleEngine> MagicRoom = MakeEngine(Scenario);
		TestTrue(TEXT("Magic Room Choice Band runtime begins"),
			BeginRuntime(*MagicRoom));
		TestTrue(TEXT("The Magic Room scenario locks both moves"),
			LockTwoFights(*MagicRoom, FirstMove, FirstMove));
		TestTrue(TEXT("The Choice Band move starts and commits before suppression"),
			MagicRoom->BeginNextLockedAction().WasAccepted()
				&& MagicRoom->CommitCurrentMoveAfterPreMoveGates().WasAccepted());
		const FBattlerId MagicHolderId = MakeNumericId<FBattlerId>(PlayerValue);
		const FBattleBattlerState* MagicHolder =
			FBattleC08CEngineFixture::GetBattler(*MagicRoom, MagicHolderId);
		TestTrue(TEXT("The live Choice lock exists before Magic Room becomes active"),
			MagicHolder != nullptr && MagicHolder->HeldItem.ChoiceLockedMoveId == FirstMove);
		TestTrue(TEXT("Magic Room is registered while the first action resolves"),
			FBattleC08CEngineFixture::SeedCondition(
				*MagicRoom,
				FBattleFieldSideConditionRules::GetMagicRoomId(),
				EBattleSide::Player,
				MakeNumericId<FBattlerId>(OpponentValue)));
		TestTrue(TEXT("The committed move reaches and executes its target"),
			MagicRoom->ResolveCurrentMoveTargets().WasAccepted()
				&& MagicRoom->ExecuteCurrentMoveEffects().WasAccepted());
		TestTrue(TEXT("The next live action checkpoint applies Magic Room suppression"),
			MagicRoom->BeginNextLockedAction().WasAccepted());
		MagicHolder = FBattleC08CEngineFixture::GetBattler(*MagicRoom, MagicHolderId);
		const FBattleHeldItemInstanceState* MagicLedger = MagicHolder != nullptr
			? FBattleC08CEngineFixture::GetState(*MagicRoom).HeldItemLedger.FindState(
				MagicHolder->HeldItem.InstanceId)
			: nullptr;
		TestTrue(TEXT("Live Magic Room suppression clears Choice lock in mirror and ledger"),
			MagicHolder != nullptr
				&& MagicHolder->HeldItem.bSuppressed
				&& !MagicHolder->HeldItem.ChoiceLockedMoveId.IsValid()
				&& MagicLedger != nullptr
				&& MagicLedger->bSuppressed);

		TUniquePtr<FBattleEngine> Struggle = MakeEngine(Scenario);
		FBattleBattlerState* StruggleHolder =
			FBattleC08CEngineFixture::GetMutableState(*Struggle).FindMutableBattler(
				MakeNumericId<FBattlerId>(PlayerValue));
		check(StruggleHolder != nullptr);
		StruggleHolder->HeldItem.ChoiceLockedMoveId = FirstMove;
		for (FBattleMoveSlotState& Move : StruggleHolder->Moves)
		{
			Move.CurrentPP = 0;
		}
		TestTrue(TEXT("A zero-PP locked holder can begin selection"),
			BeginRuntime(*Struggle));
		TestTrue(TEXT("Engine-supplied Struggle remains selectable through a Choice lock"),
			Struggle->GetPendingDecision().IsSet()
				&& Struggle->GetPendingDecision().GetValue().GetLegalMoveIds().Contains(
					FBattleBuiltInMoveDefinitions::GetStruggleMoveId()));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC08CEngineQuickClawTest,
		"PokemonSolarus.Battle.C08C.HeldItem.Engine.QuickClawStableDrawOrderPriorityAndSuccessOnlyReveal",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FBattleC08CEngineQuickClawTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		FC08CScenario Scenario;
		Scenario.PlayerItem = FBattleItemRules::GetQuickClawId();
		Scenario.OpponentItem = FBattleItemRules::GetQuickClawId();
		TArray<FExpectedDraw> Draws = {
			{0, 4, 1, FBattleItemRules::GetQuickClawActivationPurpose()},
			{0, 4, 0, FBattleItemRules::GetQuickClawActivationPurpose()}};
		TUniquePtr<FBattleEngine> Engine = MakeEngine(
			Scenario,
			MakeUnique<FScriptedRandom>(MoveTemp(Draws)));
		const FMoveId NormalMove = MakeDefinitionId<FMoveId>(NormalMoveName);
		TestTrue(TEXT("Two Quick Claw decisions lock deterministically"),
			LockTwoFights(*Engine, NormalMove, NormalMove));
		const TArray<FBattleRandomDraw> Trace = Engine->ExportRandomTrace();
		TestTrue(TEXT("Quick Claw consumes stable Player then Opponent U[0,4] draws"),
			Trace.Num() == 2
				&& Trace[0].InclusiveMinimum == 0
				&& Trace[0].InclusiveMaximum == 4
				&& Trace[0].Result == 1
				&& Trace[1].Result == 0);
		const TArray<FBattleLockedAction> Locked = Engine->GetLockedActions();
		TestTrue(TEXT("Only the successful positive 0.1 band outranks speed"),
			Locked.Num() == 2
				&& Locked[0].Decision.GetActingBattlerId()
					== MakeNumericId<FBattlerId>(OpponentValue)
				&& Locked[0].OrderKey.FractionalPriorityTenths == 1
				&& Locked[1].OrderKey.FractionalPriorityTenths == 0);
		const FBattleBattlerState* FailedHolder = FBattleC08CEngineFixture::GetBattler(
			*Engine,
			MakeNumericId<FBattlerId>(PlayerValue));
		const FBattleBattlerState* SuccessfulHolder = FBattleC08CEngineFixture::GetBattler(
			*Engine,
			MakeNumericId<FBattlerId>(OpponentValue));
		const FBattleEngineState& State = FBattleC08CEngineFixture::GetState(*Engine);
		TestTrue(TEXT("A failed roll remains core-only with no public item activation"),
			FailedHolder != nullptr
				&& !FailedHolder->HeldItem.bRevealed
				&& !State.OrderedEvents.ContainsByPredicate(
					[](const FBattleEvent& Event)
					{
						return Event.GetType() == EBattleEventType::ItemActivated
							&& Event.GetSource().BattlerId
								== MakeNumericId<FBattlerId>(PlayerValue);
					}));
		TestTrue(TEXT("Only the successful roll reveals and activates Quick Claw"),
			SuccessfulHolder != nullptr
				&& SuccessfulHolder->HeldItem.bRevealed
				&& State.OrderedEvents.ContainsByPredicate(
					[](const FBattleEvent& Event)
					{
						return Event.GetType() == EBattleEventType::ItemActivated
							&& Event.GetSource().BattlerId
								== MakeNumericId<FBattlerId>(OpponentValue);
					}));

		FC08CScenario NegativeScenario;
		NegativeScenario.PlayerItem = FBattleItemRules::GetQuickClawId();
		TUniquePtr<FBattleEngine> Negative = MakeEngine(
			NegativeScenario,
			MakeUnique<FScriptedRandom>(TArray<FExpectedDraw>{
				{0, 4, 0, FBattleItemRules::GetQuickClawActivationPurpose()}}));
		TestTrue(TEXT("A negative-priority Quick Claw action locks"),
			LockTwoFights(
				*Negative,
				MakeDefinitionId<FMoveId>(NegativeMoveName),
				NormalMove));
		const TArray<FBattleLockedAction> NegativeLocked = Negative->GetLockedActions();
		TestTrue(TEXT("Negative priority plus 0.1 stays below priority zero"),
			NegativeLocked.Num() == 2
				&& NegativeLocked[0].Decision.GetActingBattlerId()
					== MakeNumericId<FBattlerId>(OpponentValue));

		FC08CScenario EqualSpeedScenario;
		EqualSpeedScenario.PlayerItem = FBattleItemRules::GetQuickClawId();
		EqualSpeedScenario.PlayerSpeed = 100;
		EqualSpeedScenario.OpponentSpeed = 100;
		TUniquePtr<FBattleEngine> EqualSpeed = MakeEngine(
			EqualSpeedScenario,
			MakeUnique<FScriptedRandom>(TArray<FExpectedDraw>{
				{0, 4, 0, FBattleItemRules::GetQuickClawActivationPurpose()}}));
		TestTrue(TEXT("A successful Quick Claw locks against an equal-Speed opponent"),
			LockTwoFights(*EqualSpeed, NormalMove, NormalMove));
		const TArray<FBattleLockedAction> EqualSpeedLocked =
			EqualSpeed->GetLockedActions();
		TestTrue(TEXT("Positive 0.1 decides the equal-Speed action order"),
			EqualSpeedLocked.Num() == 2
				&& EqualSpeedLocked[0].OrderKey.EffectiveSpeed
					== EqualSpeedLocked[1].OrderKey.EffectiveSpeed
				&& EqualSpeedLocked[0].OrderKey.FractionalPriorityTenths == 1
				&& EqualSpeedLocked[1].OrderKey.FractionalPriorityTenths == 0
				&& EqualSpeedLocked[0].Decision.GetActingBattlerId()
					== MakeNumericId<FBattlerId>(PlayerValue));

		TUniquePtr<FBattleEngine> TrickRoom = MakeEngine(
			Scenario,
			MakeUnique<FScriptedRandom>(TArray<FExpectedDraw>{
				{0, 4, 0, FBattleItemRules::GetQuickClawActivationPurpose()},
				{0, 4, 0, FBattleItemRules::GetQuickClawActivationPurpose()}}));
		TestTrue(TEXT("Trick Room is registered before queue lock"),
			FBattleC08CEngineFixture::SeedCondition(
				*TrickRoom,
				FBattleFieldSideConditionRules::GetTrickRoomId(),
				EBattleSide::Player,
				MakeNumericId<FBattlerId>(PlayerValue)));
		TestTrue(TEXT("Both successful Quick Claw actions lock under Trick Room"),
			LockTwoFights(*TrickRoom, NormalMove, NormalMove));
		const TArray<FBattleLockedAction> TrickLocked = TrickRoom->GetLockedActions();
		TestTrue(TEXT("Equal priority and plus-0.1 actions still use reversed Speed"),
			TrickLocked.Num() == 2
				&& FBattleC08CEngineFixture::GetState(*TrickRoom).bLockedOrderReversesSpeed
				&& TrickLocked[0].OrderKey.MovePriority
					== TrickLocked[1].OrderKey.MovePriority
				&& TrickLocked[0].OrderKey.FractionalPriorityTenths == 1
				&& TrickLocked[1].OrderKey.FractionalPriorityTenths == 1
				&& TrickLocked[0].OrderKey.EffectiveSpeed
					< TrickLocked[1].OrderKey.EffectiveSpeed
				&& TrickLocked[0].Decision.GetActingBattlerId()
					== MakeNumericId<FBattlerId>(OpponentValue));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC08CEngineLeftoversTest,
		"PokemonSolarus.Battle.C08C.HeldItem.Engine.LeftoversEndTurnOrderAndHealing",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FBattleC08CEngineLeftoversTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		FC08CScenario Scenario;
		Scenario.PlayerItem = FBattleItemRules::GetLeftoversId();
		Scenario.PlayerHP = 100;
		TUniquePtr<FBattleEngine> Engine = MakeEngine(Scenario);
		TestTrue(TEXT("Leftovers runtime begins"), BeginRuntime(*Engine));
		TestTrue(TEXT("A real Grassy Terrain is active for the shared residual band"),
			FBattleC08CEngineFixture::SeedCondition(
				*Engine,
				FBattleFieldSideConditionRules::GetGrassyTerrainId(),
				EBattleSide::Player,
				MakeNumericId<FBattlerId>(OpponentValue)));
		const FBattleTriggerRegistrationState* Registration =
			FBattleC08CEngineFixture::GetState(*Engine)
				.TriggerFramework.GetActiveRegistrations().FindByPredicate(
					[](const FBattleTriggerRegistrationState& Candidate)
					{
						return Candidate.Spec.SourceDefinition.Kind
								== EBattleTriggerSourceDefinitionKind::Item
							&& Candidate.Spec.SourceDefinition.ItemId
								== FBattleItemRules::GetLeftoversId()
							&& Candidate.Spec.Rule.Phase == EBattleTriggerPhase::EndTurn;
					});
		TestTrue(TEXT("Live Leftovers registration retains residual order 5 suborder 4"),
			Registration != nullptr
				&& Registration->Spec.Rule.Order == 5
				&& Registration->Spec.Rule.Suborder == 4);
		TestTrue(TEXT("The engine can enter the end-turn checkpoint"),
			FBattleC08CEngineFixture::PrepareEndTurn(*Engine));
		const FBattleResolution Resolution = Engine->ResolveEndTurn();
		TestTrue(TEXT("Leftovers resolves at end turn"), Resolution.WasAccepted());
		TestEqual(TEXT("Grassy Terrain then Leftovers each heal floor(max HP / 16)"),
			FBattleC08CEngineFixture::GetBattler(
				*Engine,
				MakeNumericId<FBattlerId>(PlayerValue))->CurrentHP,
			124);
		const int32 GrassyHealing = Resolution.GetEvents().IndexOfByPredicate(
			[](const FBattleEvent& Event)
			{
				return Event.GetType() == EBattleEventType::Healing
					&& Event.GetSource().DefinitionId
						== FBattleFieldSideConditionRules::GetGrassyTerrainId()
							.GetDefinitionId();
			});
		const int32 Activation = Resolution.GetEvents().IndexOfByPredicate(
			[](const FBattleEvent& Event)
			{
				return Event.GetType() == EBattleEventType::ItemActivated
					&& Event.GetSource().DefinitionId
						== FBattleItemRules::GetLeftoversId().GetDefinitionId();
			});
		const int32 Healing = Resolution.GetEvents().IndexOfByPredicate(
			[](const FBattleEvent& Event)
			{
				return Event.GetType() == EBattleEventType::Healing
					&& Event.GetSource().DefinitionId
						== FBattleItemRules::GetLeftoversId().GetDefinitionId();
			});
		TestTrue(TEXT("Real Grassy Terrain suborder two precedes Leftovers suborder four"),
			GrassyHealing != INDEX_NONE
				&& Activation != INDEX_NONE
				&& GrassyHealing < Activation
				&& Activation < Healing);
		return true;
	}
}

#endif
