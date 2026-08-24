#if WITH_DEV_AUTOMATION_TESTS

#include "Battle/BattleActionQueue.h"
#include "Battle/BattleEngine.h"
#include "Battle/BattleEffectExecutor.h"
#include "Battle/BattleFinalDamageCalculator.h"
#include "Battle/BattleMajorStatus.h"
#include "Battle/BattleReplay.h"
#include "Battle/BattleState.h"
#include "BattleTestFactories.h"
#include "Misc/AutomationTest.h"

class FBattleC07BEngineFixture
{
public:
	static bool ApplyStatus(
		FBattleEngine& Engine,
		const FBattlerId BattlerId,
		const FConditionId StatusId,
		const TOptional<int32>& SleepTurns = TOptional<int32>())
	{
		if (!Engine.State.IsValid())
		{
			return false;
		}
		FBattleBattlerState* Battler = Engine.State->FindMutableBattler(BattlerId);
		FBattleTriggerSubject Owner;
		EBattleTriggerError Error = EBattleTriggerError::None;
		if (Battler == nullptr
			|| Battler->MajorStatusId.IsValid()
			|| !FBattleTriggerSubject::TryCreateBattler(BattlerId, Owner)
			|| !FBattleMajorStatusRules::TryRegisterTriggers(
				Engine.State->TriggerFramework,
				StatusId,
				Owner,
				SleepTurns,
				Error))
		{
			return false;
		}
		Battler->MajorStatusId = StatusId;
		TArray<FBattleTriggerLifecycleFact> Facts;
		Engine.State->TriggerFramework.DrainLifecycleFacts(Facts);
		return true;
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
		Battler->bRemoved = false;
		Battler->bFaintTransitionPending = false;
		return true;
	}

	static bool SetSpeciesForm(
		FBattleEngine& Engine,
		const FBattlerId BattlerId,
		const FSpeciesFormId SpeciesFormId)
	{
		FBattleBattlerState* Battler = Engine.State.IsValid()
			? Engine.State->FindMutableBattler(BattlerId)
			: nullptr;
		if (Battler == nullptr || Engine.State->Catalog.FindSpeciesForm(SpeciesFormId) == nullptr)
		{
			return false;
		}
		Battler->SpeciesFormId = SpeciesFormId;
		return true;
	}

	static bool SetStatusLayers(
		FBattleEngine& Engine,
		const FBattlerId BattlerId,
		const FConditionId StatusId,
		const int32 Layers)
	{
		if (!Engine.State.IsValid())
		{
			return false;
		}
		FBattleTriggerSubject Owner;
		FBattleTriggerSourceDefinition Source;
		FBattleTriggerOperationContext Context;
		if (!FBattleTriggerSubject::TryCreateBattler(BattlerId, Owner)
			|| !FBattleTriggerSourceDefinition::TryCreateCondition(StatusId, Source)
			|| !FBattleTriggerReentrancyToken::TryCreate(
				Engine.State->NextTriggerReentrancyToken,
				Context.ReentrancyToken))
		{
			return false;
		}
		++Engine.State->NextTriggerReentrancyToken;
		EBattleTriggerError Error = EBattleTriggerError::None;
		for (const FBattleTriggerRegistrationState& Registration :
			Engine.State->TriggerFramework.GetActiveRegistrations())
		{
			if (Registration.Spec.Owner == Owner
				&& Registration.Spec.SourceDefinition == Source
				&& !Engine.State->TriggerFramework.TryUpdateLayers(
					Registration.RegistrationId,
					Layers,
					Context,
					Error))
			{
				return false;
			}
		}
		TArray<FBattleTriggerLifecycleFact> Facts;
		Engine.State->TriggerFramework.DrainLifecycleFacts(Facts);
		return true;
	}

	static bool ExecuteCatalogMove(
		FBattleEngine& Engine,
		const FBattlerId UserBattlerId,
		const FBattlerId TargetBattlerId,
		const FMoveId MoveId,
		FBattleEffectExecutionResult& OutResult)
	{
		if (!Engine.State.IsValid())
		{
			return false;
		}
		const FBattleMoveDefinition* Move = Engine.State->Catalog.FindMove(MoveId);
		const FBattleActivePositionState* UserActive =
			Engine.State->ActivePositions.FindByPredicate(
				[UserBattlerId](const FBattleActivePositionState& Candidate)
				{
					return Candidate.BattlerId == UserBattlerId;
				});
		const FBattleActivePositionState* TargetActive =
			Engine.State->ActivePositions.FindByPredicate(
				[TargetBattlerId](const FBattleActivePositionState& Candidate)
				{
					return Candidate.BattlerId == TargetBattlerId;
				});
		if (Move == nullptr || UserActive == nullptr || TargetActive == nullptr)
		{
			return false;
		}
		FBattleBattlerTarget BattlerTarget;
		BattlerTarget.ActiveSlotId = TargetActive->ActiveSlotId;
		BattlerTarget.BattlerId = TargetBattlerId;
		FBattleResolvedTarget Target;
		if (!FBattleResolvedTarget::TryCreateBattler(BattlerTarget, Target))
		{
			return false;
		}
		FBattleEffectExecutionRequest Request;
		Request.BattleId = Engine.State->Setup.GetBattleId();
		Request.TurnId = Engine.State->TurnId;
		if (!FActionId::TryCreate(900, Request.ActionId)
			|| !FResolutionId::TryCreate(900, Request.ResolutionId))
		{
			return false;
		}
		Request.UserBattlerId = UserBattlerId;
		Request.UserSlotId = UserActive->ActiveSlotId;
		Request.Move = Move;
		Request.Targets.Add(Target);
		EBattleEffectExecutorError Error = EBattleEffectExecutorError::None;
		return FBattleEffectExecutor::TryExecuteAgainstState(
			Request,
			*Engine.State,
			OutResult,
			Error);
	}

	static bool PrepareEndTurn(FBattleEngine& Engine)
	{
		if (!Engine.State.IsValid())
		{
			return false;
		}
		Engine.State->Phase = EBattlePhase::EndOfTurn;
		Engine.State->LockedActions.Reset();
		Engine.State->CurrentLockedActionIndex = 0;
		Engine.State->AcceptedSelections.Reset();
		Engine.State->DecisionOwnerSequence.Reset();
		Engine.State->CurrentDecisionOwnerIndex = INDEX_NONE;
		Engine.State->CurrentDecisionActorOffset = 0;
		Engine.State->PendingDecision.Reset();
		Engine.State->PendingDecisionRequests.Reset();
		Engine.State->PendingReplacements.Reset();
		Engine.State->bEndTurnTriggerPassComplete = false;
		EBattleStateValidationError Error = EBattleStateValidationError::None;
		return Engine.State->ValidateInvariants(Error);
	}

	static int32 GetCurrentHP(const FBattleEngine& Engine, const FBattlerId BattlerId)
	{
		const FBattleBattlerState* Battler = Engine.State.IsValid()
			? Engine.State->FindBattler(BattlerId)
			: nullptr;
		return Battler != nullptr ? Battler->CurrentHP : INDEX_NONE;
	}

	static int32 GetCurrentPP(const FBattleEngine& Engine, const FBattlerId BattlerId)
	{
		const FBattleBattlerState* Battler = Engine.State.IsValid()
			? Engine.State->FindBattler(BattlerId)
			: nullptr;
		return Battler != nullptr && !Battler->Moves.IsEmpty()
			? Battler->Moves[0].CurrentPP
			: INDEX_NONE;
	}

	static FConditionId GetMajorStatus(
		const FBattleEngine& Engine,
		const FBattlerId BattlerId)
	{
		const FBattleBattlerState* Battler = Engine.State.IsValid()
			? Engine.State->FindBattler(BattlerId)
			: nullptr;
		return Battler != nullptr ? Battler->MajorStatusId : FConditionId();
	}

	static bool IsFainted(const FBattleEngine& Engine, const FBattlerId BattlerId)
	{
		const FBattleBattlerState* Battler = Engine.State.IsValid()
			? Engine.State->FindBattler(BattlerId)
			: nullptr;
		return Battler != nullptr && Battler->bFainted;
	}

	static int32 GetActiveRegistrationCount(const FBattleEngine& Engine)
	{
		return Engine.State.IsValid()
			? Engine.State->TriggerFramework.GetActiveRegistrations().Num()
			: INDEX_NONE;
	}

	static int32 GetStatusLayerEncoding(
		const FBattleEngine& Engine,
		const FBattlerId BattlerId,
		const FConditionId StatusId)
	{
		if (!Engine.State.IsValid())
		{
			return INDEX_NONE;
		}
		FBattleTriggerSubject Owner;
		FBattleTriggerSourceDefinition Source;
		if (!FBattleTriggerSubject::TryCreateBattler(BattlerId, Owner)
			|| !FBattleTriggerSourceDefinition::TryCreateCondition(StatusId, Source))
		{
			return INDEX_NONE;
		}
		for (const FBattleTriggerRegistrationState& Registration :
			Engine.State->TriggerFramework.GetActiveRegistrations())
		{
			if (Registration.Spec.Owner == Owner
				&& Registration.Spec.SourceDefinition == Source)
			{
				return Registration.Layers;
			}
		}
		return INDEX_NONE;
	}

	static FBattlerId GetActiveBattler(
		const FBattleEngine& Engine,
		const FActiveSlotId ActiveSlotId)
	{
		const FBattleActivePositionState* Active = Engine.State.IsValid()
			? Engine.State->FindActivePosition(ActiveSlotId)
			: nullptr;
		return Active != nullptr ? Active->BattlerId : FBattlerId();
	}

	static FPokemonBattleStats GetPermanentStats(
		const FBattleEngine& Engine,
		const FBattlerId BattlerId)
	{
		const FBattleBattlerState* Battler = Engine.State.IsValid()
			? Engine.State->FindBattler(BattlerId)
			: nullptr;
		return Battler != nullptr ? Battler->PermanentStats : FPokemonBattleStats();
	}

	static EBattlePhase GetPhase(const FBattleEngine& Engine)
	{
		return Engine.State.IsValid() ? Engine.State->Phase : EBattlePhase::Terminal;
	}

	static EBattleOutcome GetOutcome(const FBattleEngine& Engine)
	{
		return Engine.State.IsValid() ? Engine.State->Outcome : EBattleOutcome::Abandoned;
	}

	static FTurnId GetTurnId(const FBattleEngine& Engine)
	{
		return Engine.State.IsValid() ? Engine.State->TurnId : FTurnId();
	}

	static int32 GetPendingReplacementCount(const FBattleEngine& Engine)
	{
		return Engine.State.IsValid() ? Engine.State->PendingReplacements.Num() : INDEX_NONE;
	}

	static TArray<FBattleEvent> GetOrderedEvents(const FBattleEngine& Engine)
	{
		return Engine.State.IsValid() ? Engine.State->OrderedEvents : TArray<FBattleEvent>();
	}

	static bool ConsumeParalysisGate(
		FBattleEngine& Engine,
		FBattleMajorStatusActionResult& OutResult)
	{
		if (!Engine.State.IsValid() || !Engine.State->Random.IsValid())
		{
			return false;
		}
		FBattleMajorStatusActionFacts Facts;
		Facts.StatusId = FBattleMajorStatusRules::GetParalysisId();
		FBattleRandomContext Context;
		Context.BattleId = Engine.State->Setup.GetBattleId();
		Context.TurnId = Engine.State->TurnId;
		if (!FActionId::TryCreate(700, Context.ActionId)
			|| !FResolutionId::TryCreate(700, Context.ResolutionId))
		{
			return false;
		}
		Context.RulePurpose = FBattleMajorStatusRules::GetParalysisActionGatePurpose();
		return FBattleMajorStatusRules::TryResolveBeforeAction(
			Facts,
			Context,
			*Engine.State->Random,
			OutResult);
	}
};

namespace BattleMajorStatusTests
{
	using BattleTest::MakeActiveSlotId;
	using BattleTest::MakeDefinitionId;
	using BattleTest::MakeNumericId;
	using BattleTest::MakePartySlotId;

	constexpr uint64 PlayerTrainerValue = 1;
	constexpr uint64 OpponentTrainerValue = 2;
	constexpr uint64 PlayerBattlerValue = 11;
	constexpr uint64 OpponentBattlerValue = 21;
	constexpr uint64 OpponentReserveValue = 22;
	const TCHAR* MoveName = TEXT("Move.C07B.Test");
	const TCHAR* SpeciesName = TEXT("Species.C07B.Test");
	const TCHAR* FireSpeciesName = TEXT("Species.C07B.Fire");
	const TCHAR* AbilityName = TEXT("Ability.C07B.Test");
	const TCHAR* GenericStatusName = TEXT("Condition.C07B.GenericMajorStatus");

	struct FExpectedDraw
	{
		uint32 Minimum = 0;
		uint32 Maximum = 0;
		uint32 Result = 0;
		FDefinitionId Purpose;
	};

	class FScriptedStatusRandom final : public IBattleRandom
	{
	public:
		explicit FScriptedStatusRandom(TArray<FExpectedDraw> InExpected)
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
			if (bMismatch || !Expected.IsValidIndex(NextIndex))
			{
				bMismatch = true;
				return false;
			}
			const FExpectedDraw& Draw = Expected[NextIndex];
			if (!Context.IsValid()
				|| Draw.Minimum != InclusiveMinimum
				|| Draw.Maximum != InclusiveMaximum
				|| Draw.Purpose != Context.RulePurpose
				|| Draw.Result < InclusiveMinimum
				|| Draw.Result > InclusiveMaximum)
			{
				bMismatch = true;
				return false;
			}
			++NextIndex;
			OutDraw.InclusiveMinimum = InclusiveMinimum;
			OutDraw.InclusiveMaximum = InclusiveMaximum;
			OutDraw.Bound = static_cast<uint64>(InclusiveMaximum)
				- static_cast<uint64>(InclusiveMinimum) + 1;
			OutDraw.RawValue = Draw.Result;
			OutDraw.Result = Draw.Result;
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

		[[nodiscard]] bool IsExact() const
		{
			return !bMismatch && NextIndex == Expected.Num();
		}

	private:
		TArray<FExpectedDraw> Expected;
		int32 NextIndex = 0;
		bool bMismatch = false;
		TArray<FBattleRandomDraw> Trace;
	};

	FBattleRandomContext MakeRandomContext(const FDefinitionId& Purpose)
	{
		FBattleRandomContext Context;
		Context.BattleId = MakeNumericId<FBattleId>(7070);
		Context.TurnId = MakeNumericId<FTurnId>(1);
		Context.ActionId = MakeNumericId<FActionId>(1);
		Context.ResolutionId = MakeNumericId<FResolutionId>(1);
		Context.RulePurpose = Purpose;
		return Context;
	}

	TArray<FBattleTypeChartEntry> MakeNeutralTypeChart()
	{
		TArray<FBattleTypeChartEntry> Entries;
		Entries.Reserve(FBattleTypeChart::EntryCount);
		for (int32 Attack = 0; Attack < FBattleTypeChart::TypeCount; ++Attack)
		{
			for (int32 Defense = 0; Defense < FBattleTypeChart::TypeCount; ++Defense)
			{
				Entries.Add(
					{
						static_cast<EPokemonType>(Attack),
						static_cast<EPokemonType>(Defense),
						1,
						1
					});
			}
		}
		return Entries;
	}

	FBattleMoveDefinition MakeMove()
	{
		FBattleMoveDefinition Move;
		Move.Id = MakeDefinitionId<FMoveId>(MoveName);
		Move.Type = EPokemonType::Normal;
		Move.Category = EBattleMoveCategory::Physical;
		Move.Power = 40;
		Move.bAlwaysHits = true;
		Move.Accuracy = 0;
		Move.bUsesPP = true;
		Move.BasePP = 20;
		Move.bAllowsPPBoosts = true;
		Move.TargetClass = EBattleTargetClass::SelectedOpponent;
		Move.Flags = EBattleMoveFlags::NeverCritical;
		FBattleMoveEffectDescriptor Damage;
		Damage.Order = 0;
		Damage.Kind = EBattleMoveEffectKind::Damage;
		Damage.Target = EBattleEffectTarget::ResolvedTarget;
		Move.Effects.Add(Damage);
		return Move;
	}

	FBattleMoveDefinition MakeConditionMove(
		const TCHAR* IdName,
		const FConditionId ConditionId,
		const EBattleMoveEffectKind Kind = EBattleMoveEffectKind::ApplyCondition,
		const int32 ChanceNumerator = 1,
		const int32 ChanceDenominator = 1)
	{
		FBattleMoveDefinition Move;
		Move.Id = MakeDefinitionId<FMoveId>(IdName);
		Move.Type = EPokemonType::Normal;
		Move.Category = EBattleMoveCategory::Status;
		Move.Power = 0;
		Move.bAlwaysHits = true;
		Move.Accuracy = 0;
		Move.bUsesPP = true;
		Move.BasePP = 20;
		Move.bAllowsPPBoosts = true;
		Move.TargetClass = EBattleTargetClass::SelectedOpponent;
		Move.Flags = EBattleMoveFlags::NeverCritical;
		FBattleMoveEffectDescriptor Effect;
		Effect.Order = 0;
		Effect.Kind = Kind;
		Effect.Target = EBattleEffectTarget::ResolvedTarget;
		Effect.ConditionId = ConditionId;
		Effect.ChanceNumerator = ChanceNumerator;
		Effect.ChanceDenominator = ChanceDenominator;
		Move.Effects.Add(Effect);
		return Move;
	}

	FBattleMoveDefinition MakeFireMove(
		const TCHAR* IdName,
		const int32 Power,
		const bool bTwoHits)
	{
		FBattleMoveDefinition Move = MakeMove();
		Move.Id = MakeDefinitionId<FMoveId>(IdName);
		Move.Type = EPokemonType::Fire;
		Move.Power = Power;
		if (bTwoHits)
		{
			Move.Effects[0].Order = 1;
			FBattleMoveEffectDescriptor MultiHit;
			MultiHit.Order = 0;
			MultiHit.Kind = EBattleMoveEffectKind::MultiHit;
			MultiHit.Target = EBattleEffectTarget::ResolvedTarget;
			MultiHit.MinimumCount = 2;
			MultiHit.MaximumCount = 2;
			Move.Effects.Insert(MultiHit, 0);
		}
		return Move;
	}

	FBattleMoveDefinition MakeForcedSwitchMove(const TCHAR* IdName)
	{
		FBattleMoveDefinition Move;
		Move.Id = MakeDefinitionId<FMoveId>(IdName);
		Move.Type = EPokemonType::Normal;
		Move.Category = EBattleMoveCategory::Status;
		Move.bAlwaysHits = true;
		Move.bUsesPP = true;
		Move.BasePP = 20;
		Move.bAllowsPPBoosts = true;
		Move.TargetClass = EBattleTargetClass::SelectedOpponent;
		Move.Flags = EBattleMoveFlags::NeverCritical;
		FBattleMoveEffectDescriptor Switch;
		Switch.Order = 0;
		Switch.Kind = EBattleMoveEffectKind::Switch;
		Switch.Target = EBattleEffectTarget::ResolvedTarget;
		Move.Effects.Add(Switch);
		return Move;
	}

	FBattleDefinitionCatalog MakeCatalog(const FBattleMoveDefinition& Move)
	{
		FBattleDefinitionCatalogInput Input;
		Input.TypeChartEntries = MakeNeutralTypeChart();
		Input.Moves.Add(Move);
		Input.Abilities.Add({MakeDefinitionId<FAbilityId>(AbilityName)});
		FBattleSpeciesFormDefinition Species;
		Species.Id = MakeDefinitionId<FSpeciesFormId>(SpeciesName);
		Species.PrimaryType = EPokemonType::Normal;
		Species.BaseStats = {80, 80, 80, 80, 80, 80};
		Species.CatchRate = 45;
		Species.AbilityChoices.Add(MakeDefinitionId<FAbilityId>(AbilityName));
		Input.SpeciesForms.Add(Species);
		FBattleSpeciesFormDefinition FireSpecies = Species;
		FireSpecies.Id = MakeDefinitionId<FSpeciesFormId>(FireSpeciesName);
		FireSpecies.PrimaryType = EPokemonType::Fire;
		Input.SpeciesForms.Add(FireSpecies);
		for (const FConditionId& StatusId : FBattleMajorStatusRules::GetCanonicalIds())
		{
			Input.Conditions.Add({StatusId, EBattleConditionKind::MajorStatus});
		}
		Input.Conditions.Add(
			{MakeDefinitionId<FConditionId>(GenericStatusName), EBattleConditionKind::MajorStatus});
		FBattleDefinitionCatalog Catalog;
		TArray<FBattleCatalogDiagnostic> Diagnostics;
		const bool bCreated = FBattleDefinitionCatalog::TryCreate(
			Input,
			Catalog,
			Diagnostics);
		check(bCreated);
		return Catalog;
	}

	FBattleDefinitionCatalog MakeCatalog()
	{
		return MakeCatalog(MakeMove());
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
				? TEXT("Selector.C07B.Player")
				: TEXT("Selector.C07B.Opponent"));
		return Trainer;
	}

	FBattlePartyEntrySetup MakePartyEntry(
		const uint64 TrainerValue,
		const uint64 BattlerValue,
		const int32 PartyIndex,
		const FMoveId MoveId,
		const int32 Speed)
	{
		FBattlePartyEntrySetup Entry;
		Entry.TrainerId = MakeNumericId<FTrainerId>(TrainerValue);
		Entry.BattlerId = MakeNumericId<FBattlerId>(BattlerValue);
		Entry.SourcePokemonId = MakeNumericId<FSourcePokemonId>(1000 + BattlerValue);
		Entry.PartySlotId = MakePartySlotId(PartyIndex);
		Entry.SpeciesFormId = MakeDefinitionId<FSpeciesFormId>(SpeciesName);
		Entry.Level = 50;
		Entry.Stats = {160, 100, 100, 100, 100, Speed};
		Entry.CurrentHP = 160;
		Entry.AbilityId = MakeDefinitionId<FAbilityId>(AbilityName);
		Entry.Moves.Add({0, MoveId, 20, 20});
		return Entry;
	}

	bool TryMakeSetupWithMove(
		const uint64 BattleValue,
		const bool bOpponentReserve,
		const FMoveId MoveId,
		const int32 PlayerSpeed,
		FBattleSetup& OutSetup)
	{
		FBattleSetupInput Input;
		Input.BattleId = MakeNumericId<FBattleId>(BattleValue);
		Input.SettingsReference =
			{MakeDefinitionId<FDefinitionId>(TEXT("Settings.C07B")), 1};
		Input.CatalogReference =
			{MakeDefinitionId<FDefinitionId>(TEXT("Catalog.C07B")), 1};
		Input.EncounterKind = EBattleEncounterKind::Trainer;
		Input.Format = EBattleFormat::Single;
		Input.CaptureCapacity = {2, 100};
		Input.Policies.bBagAllowed = false;
		Input.Policies.bRunAllowed = false;
		Input.Policies.bCaptureAllowed = false;
		Input.Policies.bShiftPromptEligible = false;
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
			PlayerTrainerValue, PlayerBattlerValue, 0, MoveId, PlayerSpeed));
		Input.PartyEntries.Add(MakePartyEntry(
			OpponentTrainerValue, OpponentBattlerValue, 0, MoveId, 80));
		if (bOpponentReserve)
		{
			Input.PartyEntries.Add(MakePartyEntry(
				OpponentTrainerValue,
				OpponentReserveValue,
				1,
				MoveId,
				70));
		}
		Input.StartingActive.Add(
			{
				MakeActiveSlotId(EBattleSide::Player, EBattlePosition::Left),
				MakeNumericId<FTrainerId>(PlayerTrainerValue),
				MakeNumericId<FBattlerId>(PlayerBattlerValue)
			});
		Input.StartingActive.Add(
			{
				MakeActiveSlotId(EBattleSide::Opponent, EBattlePosition::Left),
				MakeNumericId<FTrainerId>(OpponentTrainerValue),
				MakeNumericId<FBattlerId>(OpponentBattlerValue)
			});
		EBattleSetupValidationError Error = EBattleSetupValidationError::None;
		return FBattleSetup::TryCreate(Input, OutSetup, Error);
	}

	bool TryMakeSetup(
		const uint64 BattleValue,
		const bool bOpponentReserve,
		FBattleSetup& OutSetup)
	{
		return TryMakeSetupWithMove(
			BattleValue,
			bOpponentReserve,
			MakeDefinitionId<FMoveId>(MoveName),
			101,
			OutSetup);
	}

	bool TryCreateEngineWithMove(
		const uint64 BattleValue,
		const bool bOpponentReserve,
		const FBattleMoveDefinition& Move,
		TUniquePtr<IBattleRandom>&& Random,
		TUniquePtr<FBattleEngine>& OutEngine,
		const int32 PlayerSpeed = 101)
	{
		FBattleSetup Setup;
		if (!TryMakeSetupWithMove(
			BattleValue,
			bOpponentReserve,
			Move.Id,
			PlayerSpeed,
			Setup))
		{
			return false;
		}
		FBattleRejection Rejection;
		return FBattleEngine::TryCreate(
			Setup,
			MakeCatalog(Move),
			MoveTemp(Random),
			OutEngine,
			Rejection);
	}

	bool TryCreateEngine(
		const uint64 BattleValue,
		const bool bOpponentReserve,
		const uint64 Seed,
		TUniquePtr<FBattleEngine>& OutEngine)
	{
		FBattleSetup Setup;
		if (!TryMakeSetup(BattleValue, bOpponentReserve, Setup))
		{
			return false;
		}
		FBattleRejection Rejection;
		return FBattleEngine::TryCreate(
			Setup,
			MakeCatalog(),
			MakeUnique<FSeededBattleRandom>(Seed),
			OutEngine,
			Rejection);
	}

	bool LockAllFights(FBattleEngine& Engine, const FMoveId MoveId)
	{
		FBattleRejection Rejection;
		if (Engine.GetSnapshot().GetPhase() == EBattlePhase::Setup
			&& !Engine.TryBeginActionDecisionSequence(Rejection))
		{
			return false;
		}
		int32 Guard = 0;
		while (Engine.GetPendingDecision().IsSet() && Guard++ < 4)
		{
			const FBattleDecisionRequest Request = Engine.GetPendingDecision().GetValue();
			FBattleDecision Decision;
			const FBattleMoveTargetOption* Target =
				Request.GetLegalMoveTargets().FindByPredicate(
					[MoveId](const FBattleMoveTargetOption& Option)
					{
						return Option.MoveId == MoveId;
					});
			const bool bCreated = Target != nullptr
				&& FBattleDecision::TryCreateFight(
					Request.GetStateVersion(),
					Request.GetDecisionOwnerTrainerId(),
					Request.GetActingBattlerId(),
					MoveId,
					Target->ActiveSlotId,
					Decision);
			if (!bCreated || !Engine.SubmitDecision(Decision).WasAccepted())
			{
				return false;
			}
		}
		return Guard < 4 && Engine.GetSnapshot().GetPhase() == EBattlePhase::Locked;
	}

	bool StatsEqual(const FPokemonBattleStats& Left, const FPokemonBattleStats& Right)
	{
		return Left.MaxHP == Right.MaxHP
			&& Left.Attack == Right.Attack
			&& Left.Defense == Right.Defense
			&& Left.SpecialAttack == Right.SpecialAttack
			&& Left.SpecialDefense == Right.SpecialDefense
			&& Left.Speed == Right.Speed;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC07BCanonicalContractTest,
	"PokemonSolarus.Battle.C07B.Contract.CanonicalIdsTriggersAndCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC07BCanonicalContractTest::RunTest(const FString& Parameters)
{
	using namespace BattleMajorStatusTests;
	(void)Parameters;
	const TArray<FConditionId> Ids = FBattleMajorStatusRules::GetCanonicalIds();
	TestEqual(TEXT("Exactly six canonical IDs exist"), Ids.Num(), 6);
	const TArray<FName> ExpectedNames = {
		FName(TEXT("Condition.Burn")),
		FName(TEXT("Condition.Paralysis")),
		FName(TEXT("Condition.Sleep")),
		FName(TEXT("Condition.Freeze")),
		FName(TEXT("Condition.Poison")),
		FName(TEXT("Condition.Toxic"))
	};
	for (int32 Index = 0; Index < Ids.Num() && Index < ExpectedNames.Num(); ++Index)
	{
		TestEqual(TEXT("Every major status uses its exact canonical name"),
			Ids[Index].GetDefinitionId().GetName(), ExpectedNames[Index]);
	}
	TestFalse(TEXT("An arbitrary MajorStatus fixture ID is not canonical C07B content"),
		FBattleMajorStatusRules::IsCanonical(
			MakeDefinitionId<FConditionId>(GenericStatusName)));

	FBattleTriggerSubject Owner;
	TestTrue(TEXT("A battler trigger owner is valid"), FBattleTriggerSubject::TryCreateBattler(
		MakeNumericId<FBattlerId>(PlayerBattlerValue), Owner));
	int32 RegistrationCount = 0;
	for (const FConditionId& Id : Ids)
	{
		TArray<FBattleTriggerRegistrationSpec> Specs;
		const TOptional<int32> SleepTurns = Id == FBattleMajorStatusRules::GetSleepId()
			? TOptional<int32>(3)
			: TOptional<int32>();
		TestTrue(TEXT("Every canonical status builds trigger specs"),
			FBattleMajorStatusRules::TryBuildTriggerRegistrationSpecs(
				Id, Owner, SleepTurns, Specs));
		RegistrationCount += Specs.Num();
		auto HasPhase = [&Specs](const EBattleTriggerPhase Phase)
		{
			return Specs.ContainsByPredicate(
				[Phase](const FBattleTriggerRegistrationSpec& Spec)
				{
					return Spec.Rule.Phase == Phase;
				});
		};
		if (Id == FBattleMajorStatusRules::GetBurnId())
		{
			TestTrue(TEXT("Burn registers BeforeDamage"),
				HasPhase(EBattleTriggerPhase::BeforeDamage));
			TestTrue(TEXT("Burn registers EndTurn"),
				HasPhase(EBattleTriggerPhase::EndTurn));
		}
		else if (Id == FBattleMajorStatusRules::GetParalysisId())
		{
			TestTrue(TEXT("Paralysis registers ActionOrderCalculation"),
				HasPhase(EBattleTriggerPhase::ActionOrderCalculation));
			TestTrue(TEXT("Paralysis registers BeforeAction"),
				HasPhase(EBattleTriggerPhase::BeforeAction));
		}
		else if (Id == FBattleMajorStatusRules::GetSleepId())
		{
			TestEqual(TEXT("Sleep owns one BeforeAction trigger"), Specs.Num(), 1);
			TestTrue(TEXT("Sleep registers BeforeAction"),
				HasPhase(EBattleTriggerPhase::BeforeAction));
			if (Specs.Num() == 1)
			{
				TestTrue(TEXT("Sleep decrements before its effect"),
					Specs[0].Rule.bDecrementDurationBeforeEffect);
				TestTrue(TEXT("Sleep stores a hidden duration"),
					Specs[0].RemainingTurns.IsSet());
				if (Specs[0].RemainingTurns.IsSet())
				{
					TestEqual(TEXT("Sleep stores the exact hidden duration"),
						Specs[0].RemainingTurns.GetValue(), 3);
				}
			}
		}
		else if (Id == FBattleMajorStatusRules::GetFreezeId())
		{
			TestEqual(TEXT("Freeze owns one BeforeAction trigger"), Specs.Num(), 1);
			TestTrue(TEXT("Freeze registers BeforeAction"),
				HasPhase(EBattleTriggerPhase::BeforeAction));
		}
		else if (Id == FBattleMajorStatusRules::GetPoisonId())
		{
			TestEqual(TEXT("Poison owns one EndTurn trigger"), Specs.Num(), 1);
			TestTrue(TEXT("Poison registers EndTurn"),
				HasPhase(EBattleTriggerPhase::EndTurn));
		}
		else if (Id == FBattleMajorStatusRules::GetToxicId())
		{
			TestTrue(TEXT("Toxic registers EndTurn"),
				HasPhase(EBattleTriggerPhase::EndTurn));
			TestTrue(TEXT("Toxic registers SwitchOut"),
				HasPhase(EBattleTriggerPhase::SwitchOut));
		}
		for (const FBattleTriggerRegistrationSpec& Spec : Specs)
		{
			if (Spec.Rule.Phase == EBattleTriggerPhase::EndTurn)
			{
				const int32 ExpectedOrder = Id == FBattleMajorStatusRules::GetBurnId() ? 10 : 9;
				TestEqual(TEXT("Residual order is canonical"), Spec.Rule.Order, ExpectedOrder);
			}
		}
	}
	TestEqual(TEXT("The six statuses own exactly nine registrations"), RegistrationCount, 9);

	FBattleTriggerFramework Framework;
	EBattleTriggerError Error = EBattleTriggerError::None;
	TestTrue(TEXT("Burn registers"), FBattleMajorStatusRules::TryRegisterTriggers(
		Framework, FBattleMajorStatusRules::GetBurnId(), Owner, TOptional<int32>(), Error));
	TestTrue(TEXT("Poison registers beside Burn"), FBattleMajorStatusRules::TryRegisterTriggers(
		Framework, FBattleMajorStatusRules::GetPoisonId(), Owner, TOptional<int32>(), Error));
	TArray<FBattleTriggerLifecycleFact> Started;
	Framework.DrainLifecycleFacts(Started);
	TestEqual(TEXT("Burn plus Poison owns three registrations"),
		Framework.GetActiveRegistrations().Num(), 3);
	FBattleTriggerOperationContext CleanupContext;
	CleanupContext.ReentrancyToken = MakeNumericId<FBattleTriggerReentrancyToken>(1);
	TestTrue(TEXT("Filtered Burn cleanup succeeds"), FBattleMajorStatusRules::TryCleanupTriggers(
		Framework,
		FBattleMajorStatusRules::GetBurnId(),
		Owner,
		EBattleTriggerCleanupReason::Removal,
		CleanupContext,
		Error));
	const TArray<FBattleTriggerRegistrationState> Remaining = Framework.GetActiveRegistrations();
	TestEqual(TEXT("Only Poison survives precise cleanup"), Remaining.Num(), 1);
	TestTrue(TEXT("The survivor is Poison"), Remaining[0].Spec.SourceDefinition.ConditionId
		== FBattleMajorStatusRules::GetPoisonId());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC07BApplicationTest,
	"PokemonSolarus.Battle.C07B.Application.MutualExclusionImmunityAndHooks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC07BApplicationTest::RunTest(const FString& Parameters)
{
	using namespace BattleMajorStatusTests;
	(void)Parameters;
	FBattleMajorStatusApplicationFacts Facts;
	Facts.RequestedStatusId = FBattleMajorStatusRules::GetBurnId();
	Facts.PrimaryType = EPokemonType::Normal;
	FBattleMajorStatusApplicationResult Result;
	TestTrue(TEXT("A neutral Burn application evaluates"),
		FBattleMajorStatusRules::TryEvaluateApplication(Facts, Result));
	TestEqual(TEXT("Neutral Burn can apply"), Result.Outcome,
		EBattleMajorStatusApplicationOutcome::CanApply);

	Facts.ExistingMajorStatusId = FBattleMajorStatusRules::GetSleepId();
	TestTrue(TEXT("Mutual exclusion evaluates"),
		FBattleMajorStatusRules::TryEvaluateApplication(Facts, Result));
	TestEqual(TEXT("One existing status blocks another"), Result.Outcome,
		EBattleMajorStatusApplicationOutcome::AlreadyHasMajorStatus);
	Facts.ExistingMajorStatusId = FConditionId();

	struct FImmunityCase { FConditionId StatusId; EPokemonType Type; };
	const TArray<FImmunityCase> Immunities = {
		{FBattleMajorStatusRules::GetBurnId(), EPokemonType::Fire},
		{FBattleMajorStatusRules::GetParalysisId(), EPokemonType::Electric},
		{FBattleMajorStatusRules::GetFreezeId(), EPokemonType::Ice},
		{FBattleMajorStatusRules::GetPoisonId(), EPokemonType::Poison},
		{FBattleMajorStatusRules::GetPoisonId(), EPokemonType::Steel},
		{FBattleMajorStatusRules::GetToxicId(), EPokemonType::Poison},
		{FBattleMajorStatusRules::GetToxicId(), EPokemonType::Steel}
	};
	for (const FImmunityCase& Immunity : Immunities)
	{
		Facts = FBattleMajorStatusApplicationFacts();
		Facts.RequestedStatusId = Immunity.StatusId;
		Facts.PrimaryType = Immunity.Type;
		TestTrue(TEXT("A canonical immunity evaluates"),
			FBattleMajorStatusRules::TryEvaluateApplication(Facts, Result));
		TestEqual(TEXT("The canonical type is immune"), Result.Outcome,
			EBattleMajorStatusApplicationOutcome::TypeImmune);
	}

	auto TestPrevention = [this](
		const TCHAR* What,
		const FBattleMajorStatusPreventionInputs& Prevention,
		const FConditionId StatusId,
		const EBattleMajorStatusPreventionReason Expected)
	{
		FBattleMajorStatusApplicationFacts HookFacts;
		HookFacts.RequestedStatusId = StatusId;
		HookFacts.PrimaryType = EPokemonType::Normal;
		HookFacts.Prevention = Prevention;
		FBattleMajorStatusApplicationResult HookResult;
		TestTrue(What, FBattleMajorStatusRules::TryEvaluateApplication(HookFacts, HookResult));
		TestEqual(What, HookResult.PreventionReason, Expected);
	};
	FBattleMajorStatusPreventionInputs Prevention;
	Prevention.bSunActive = true;
	TestPrevention(TEXT("Sun prevents Freeze"), Prevention,
		FBattleMajorStatusRules::GetFreezeId(), EBattleMajorStatusPreventionReason::Sun);
	Prevention = FBattleMajorStatusPreventionInputs(); Prevention.bTerrainPrevents = true;
	TestPrevention(TEXT("Terrain hook is explicit"), Prevention,
		FBattleMajorStatusRules::GetBurnId(), EBattleMajorStatusPreventionReason::Terrain);
	Prevention = FBattleMajorStatusPreventionInputs(); Prevention.bSafeguardPrevents = true;
	TestPrevention(TEXT("Safeguard hook is explicit"), Prevention,
		FBattleMajorStatusRules::GetBurnId(), EBattleMajorStatusPreventionReason::Safeguard);
	Prevention = FBattleMajorStatusPreventionInputs(); Prevention.bAbilityPrevents = true;
	TestPrevention(TEXT("Ability hook is explicit"), Prevention,
		FBattleMajorStatusRules::GetBurnId(), EBattleMajorStatusPreventionReason::Ability);
	Prevention = FBattleMajorStatusPreventionInputs(); Prevention.bItemPrevents = true;
	TestPrevention(TEXT("Item hook is explicit"), Prevention,
		FBattleMajorStatusRules::GetBurnId(), EBattleMajorStatusPreventionReason::Item);

	const FBattlerId PlayerId = MakeNumericId<FBattlerId>(PlayerBattlerValue);
	const FBattlerId OpponentId = MakeNumericId<FBattlerId>(OpponentBattlerValue);
	const FConditionId GenericStatusId = MakeDefinitionId<FConditionId>(GenericStatusName);
	const FBattleMoveDefinition GenericMove = MakeConditionMove(
		TEXT("Move.C07B.ApplyGeneric"), GenericStatusId);
	TUniquePtr<FBattleEngine> GenericEngine;
	TestTrue(TEXT("The generic-status engine is created"), TryCreateEngineWithMove(
		7071,
		false,
		GenericMove,
		MakeUnique<FSeededBattleRandom>(1),
		GenericEngine));
	FBattleEffectExecutionResult Execution;
	TestTrue(TEXT("An arbitrary MajorStatus retains generic storage behavior"),
		FBattleC07BEngineFixture::ExecuteCatalogMove(
			*GenericEngine, PlayerId, OpponentId, GenericMove.Id, Execution));
	TestTrue(TEXT("The arbitrary status is stored"),
		FBattleC07BEngineFixture::GetMajorStatus(*GenericEngine, OpponentId)
			== GenericStatusId);
	TestEqual(TEXT("The arbitrary status receives no C07B registrations"),
		FBattleC07BEngineFixture::GetActiveRegistrationCount(*GenericEngine), 0);

	const FBattleMoveDefinition BurnMove = MakeConditionMove(
		TEXT("Move.C07B.ApplyBurn"), FBattleMajorStatusRules::GetBurnId());
	TUniquePtr<FBattleEngine> BurnEngine;
	TestTrue(TEXT("The canonical Burn engine is created"), TryCreateEngineWithMove(
		7072,
		false,
		BurnMove,
		MakeUnique<FSeededBattleRandom>(2),
		BurnEngine));
	TestTrue(TEXT("Canonical Burn applies through the live executor"),
		FBattleC07BEngineFixture::ExecuteCatalogMove(
			*BurnEngine, PlayerId, OpponentId, BurnMove.Id, Execution));
	TestTrue(TEXT("Canonical Burn is stored"),
		FBattleC07BEngineFixture::GetMajorStatus(*BurnEngine, OpponentId)
			== FBattleMajorStatusRules::GetBurnId());
	TestEqual(TEXT("Canonical Burn creates both C07A registrations"),
		FBattleC07BEngineFixture::GetActiveRegistrationCount(*BurnEngine), 2);

	const FBattleMoveDefinition ImmuneBurnMove = MakeConditionMove(
		TEXT("Move.C07B.ImmuneBurn"),
		FBattleMajorStatusRules::GetBurnId(),
		EBattleMoveEffectKind::ApplyCondition,
		50,
		100);
	TUniquePtr<FBattleEngine> ImmuneEngine;
	TestTrue(TEXT("The immune Burn engine is created"), TryCreateEngineWithMove(
		7073,
		false,
		ImmuneBurnMove,
		MakeUnique<FScriptedStatusRandom>(TArray<FExpectedDraw>()),
		ImmuneEngine));
	TestTrue(TEXT("The target uses the Fire species"),
		FBattleC07BEngineFixture::SetSpeciesForm(
			*ImmuneEngine,
			OpponentId,
			MakeDefinitionId<FSpeciesFormId>(FireSpeciesName)));
	TestTrue(TEXT("A type-immune application resolves without a chance draw"),
		FBattleC07BEngineFixture::ExecuteCatalogMove(
			*ImmuneEngine, PlayerId, OpponentId, ImmuneBurnMove.Id, Execution));
	TestFalse(TEXT("Type immunity leaves the target unstatused"),
		FBattleC07BEngineFixture::GetMajorStatus(*ImmuneEngine, OpponentId).IsValid());
	TestEqual(TEXT("Application prevention occurs before status RNG"),
		ImmuneEngine->ExportRandomTrace().Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC07BSleepTest,
	"PokemonSolarus.Battle.C07B.Sleep.DurationActionGateAndPersistence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC07BSleepTest::RunTest(const FString& Parameters)
{
	using namespace BattleMajorStatusTests;
	(void)Parameters;
	FScriptedStatusRandom DurationRandom({
		{2, 4, 3, FBattleMajorStatusRules::GetSleepDurationPurpose()}
	});
	FBattleSleepDurationResult Duration;
	TestTrue(TEXT("Sleep draws U[2,4] once"), FBattleMajorStatusRules::TryRollSleepDuration(
		MakeRandomContext(FBattleMajorStatusRules::GetSleepDurationPurpose()),
		DurationRandom,
		Duration));
	TestEqual(TEXT("The scripted duration is retained"), Duration.Turns, 3);
	TestTrue(TEXT("The duration draw is exact"), DurationRandom.IsExact());

	const FBattlerId PlayerId = MakeNumericId<FBattlerId>(PlayerBattlerValue);
	const FBattlerId OpponentId = MakeNumericId<FBattlerId>(OpponentBattlerValue);
	const FBattleMoveDefinition ApplySleepMove = MakeConditionMove(
		TEXT("Move.C07B.ApplySleep"), FBattleMajorStatusRules::GetSleepId());
	TUniquePtr<FBattleEngine> ApplicationEngine;
	TestTrue(TEXT("The live Sleep application engine is created"),
		TryCreateEngineWithMove(
			7074,
			false,
			ApplySleepMove,
			MakeUnique<FScriptedStatusRandom>(TArray<FExpectedDraw>({
				{2, 4, 3, FBattleMajorStatusRules::GetSleepDurationPurpose()}
			})),
			ApplicationEngine));
	FBattleEffectExecutionResult SleepExecution;
	TestTrue(TEXT("Sleep applies through the live executor"),
		FBattleC07BEngineFixture::ExecuteCatalogMove(
			*ApplicationEngine,
			PlayerId,
			OpponentId,
			ApplySleepMove.Id,
			SleepExecution));
	const TArray<FBattleRandomDraw> ApplicationTrace = ApplicationEngine->ExportRandomTrace();
	TestEqual(TEXT("Live Sleep application consumes exactly one draw"),
		ApplicationTrace.Num(), 1);
	if (ApplicationTrace.Num() == 1)
	{
		TestTrue(TEXT("The live draw has the Sleep-duration purpose"),
			ApplicationTrace[0].RulePurpose
				== FBattleMajorStatusRules::GetSleepDurationPurpose());
	}
	const FBattleSnapshot SleepSnapshot = ApplicationEngine->GetSnapshotForObserver(
		MakeNumericId<FTrainerId>(PlayerTrainerValue));
	const FBattleObservedBattler* SleepingProjection =
		SleepSnapshot.FindObservedBattler(OpponentId);
	TestNotNull(TEXT("The sleeping battler remains publicly projected"), SleepingProjection);
	if (SleepingProjection != nullptr)
	{
		TestTrue(TEXT("The public snapshot exposes only the Sleep status identity"),
			SleepingProjection->MajorStatusId == FBattleMajorStatusRules::GetSleepId());
	}

	FScriptedStatusRandom NoDraw({});
	FBattleMajorStatusActionFacts Facts;
	Facts.StatusId = FBattleMajorStatusRules::GetSleepId();
	Facts.RemainingSleepTurns = 2;
	FBattleMajorStatusActionResult Action;
	TestTrue(TEXT("Sleep decrements before the action"),
		FBattleMajorStatusRules::TryResolveBeforeAction(
			Facts, MakeRandomContext(FBattleMajorStatusRules::GetSleepDurationPurpose()), NoDraw, Action));
	TestEqual(TEXT("Two turns becomes one"), Action.RemainingSleepTurns.GetValue(), 1);
	TestEqual(TEXT("The action is denied while still asleep"), Action.Outcome,
		EBattleMajorStatusActionOutcome::Denied);
	Facts.RemainingSleepTurns = 1;
	TestTrue(TEXT("The waking action evaluates"),
		FBattleMajorStatusRules::TryResolveBeforeAction(
			Facts, MakeRandomContext(FBattleMajorStatusRules::GetSleepDurationPurpose()), NoDraw, Action));
	TestTrue(TEXT("Zero cures Sleep"), Action.bCureStatus);
	TestEqual(TEXT("Waking allows the action"), Action.Outcome,
		EBattleMajorStatusActionOutcome::CuredAndAllowed);
	Facts.RemainingSleepTurns = 3;
	Facts.bMoveUsableWhileAsleep = true;
	TestTrue(TEXT("The asleep-usable hook evaluates"),
		FBattleMajorStatusRules::TryResolveBeforeAction(
			Facts, MakeRandomContext(FBattleMajorStatusRules::GetSleepDurationPurpose()), NoDraw, Action));
	TestEqual(TEXT("An asleep-usable move is allowed"), Action.Outcome,
		EBattleMajorStatusActionOutcome::Allowed);
	TestTrue(TEXT("Sleep action checks consume no RNG"), NoDraw.IsExact());

	FBattleTriggerSubject Owner;
	TestTrue(TEXT("Sleep receives a valid trigger owner"),
		FBattleTriggerSubject::TryCreateBattler(MakeNumericId<FBattlerId>(PlayerBattlerValue), Owner));
	FBattleTriggerFramework Framework;
	EBattleTriggerError Error = EBattleTriggerError::None;
	TestTrue(TEXT("Sleep registers with its hidden counter"),
		FBattleMajorStatusRules::TryRegisterTriggers(
			Framework, FBattleMajorStatusRules::GetSleepId(), Owner, 3, Error));
	TArray<FBattleTriggerLifecycleFact> FactsOut;
	Framework.DrainLifecycleFacts(FactsOut);
	FBattleTriggerOperationContext SwitchContext;
	SwitchContext.ReentrancyToken = MakeNumericId<FBattleTriggerReentrancyToken>(1);
	TestTrue(TEXT("Switch cleanup request is valid"), FBattleMajorStatusRules::TryCleanupTriggers(
		Framework,
		FBattleMajorStatusRules::GetSleepId(),
		Owner,
		EBattleTriggerCleanupReason::Switch,
		SwitchContext,
		Error));
	TestEqual(TEXT("Sleep and its counter persist through switch"),
		Framework.GetActiveRegistrations().Num(), 1);

	TUniquePtr<FBattleEngine> DeniedEngine;
	TestTrue(TEXT("The live Sleep denial engine is created"),
		TryCreateEngine(7075, false, 5, DeniedEngine));
	TestTrue(TEXT("Two-turn Sleep is attached to the live actor"),
		FBattleC07BEngineFixture::ApplyStatus(
			*DeniedEngine, PlayerId, FBattleMajorStatusRules::GetSleepId(), 2));
	TestTrue(TEXT("The live Sleep battle locks its actions"),
		LockAllFights(*DeniedEngine, MakeDefinitionId<FMoveId>(MoveName)));
	TestTrue(TEXT("The sleeping actor is first before the status gate"),
		!DeniedEngine->GetLockedActions().IsEmpty()
			&& DeniedEngine->GetLockedActions()[0].Decision.GetActingBattlerId() == PlayerId);
	TestTrue(TEXT("The sleeping action begins"),
		DeniedEngine->BeginNextLockedAction().WasAccepted());
	const int32 PPBeforeDenial =
		FBattleC07BEngineFixture::GetCurrentPP(*DeniedEngine, PlayerId);
	const FBattleResolution DeniedResolution =
		DeniedEngine->CommitCurrentMoveAfterPreMoveGates();
	TestTrue(TEXT("Sleep denial consumes the action as an accepted resolution"),
		DeniedResolution.WasAccepted());
	TestEqual(TEXT("Sleep denial occurs before PP"),
		FBattleC07BEngineFixture::GetCurrentPP(*DeniedEngine, PlayerId),
		PPBeforeDenial);
	TestFalse(TEXT("The denied action emits no PP event"),
		DeniedResolution.GetEvents().ContainsByPredicate(
			[](const FBattleEvent& Event)
			{
				return Event.GetType() == EBattleEventType::PPConsumed;
			}));

	TestTrue(TEXT("The opponent action begins after the Sleep denial"),
		DeniedEngine->BeginNextLockedAction().WasAccepted());
	TestTrue(TEXT("The opponent move commits"),
		DeniedEngine->CommitCurrentMoveAfterPreMoveGates().WasAccepted());
	TestTrue(TEXT("The opponent target resolves"),
		DeniedEngine->ResolveCurrentMoveTargets().WasAccepted());
	TestTrue(TEXT("The opponent effects complete turn one"),
		DeniedEngine->ExecuteCurrentMoveEffects().WasAccepted());
	TestEqual(TEXT("The completed queue reaches EndOfTurn"),
		FBattleC07BEngineFixture::GetPhase(*DeniedEngine), EBattlePhase::EndOfTurn);
	TestTrue(TEXT("Turn one residual processing advances"),
		DeniedEngine->ResolveEndTurn().WasAccepted());
	TestTrue(TEXT("The waking battle locks turn-two actions"),
		LockAllFights(*DeniedEngine, MakeDefinitionId<FMoveId>(MoveName)));
	TestTrue(TEXT("The waking action begins"),
		DeniedEngine->BeginNextLockedAction().WasAccepted());
	const int32 PPBeforeWake = FBattleC07BEngineFixture::GetCurrentPP(*DeniedEngine, PlayerId);
	const FBattleResolution WakeResolution =
		DeniedEngine->CommitCurrentMoveAfterPreMoveGates();
	TestTrue(TEXT("The waking action proceeds"), WakeResolution.WasAccepted());
	TestFalse(TEXT("Waking clears the live public status"),
		FBattleC07BEngineFixture::GetMajorStatus(*DeniedEngine, PlayerId).IsValid());
	TestEqual(TEXT("The waking move spends PP after the gate"),
		FBattleC07BEngineFixture::GetCurrentPP(*DeniedEngine, PlayerId),
		PPBeforeWake - 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC07BParalysisTest,
	"PokemonSolarus.Battle.C07B.Paralysis.SpeedAndActionGate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC07BParalysisTest::RunTest(const FString& Parameters)
{
	using namespace BattleMajorStatusTests;
	(void)Parameters;
	int32 Speed = INDEX_NONE;
	TestTrue(TEXT("Paralysis modifies stage-adjusted Speed"),
		FBattleMajorStatusRules::TryApplySpeedModifier(
			FBattleMajorStatusRules::GetParalysisId(), 101, Speed));
	TestEqual(TEXT("Paralysis floors Speed/2"), Speed, 50);
	TestTrue(TEXT("A Speed of one is valid"), FBattleMajorStatusRules::TryApplySpeedModifier(
		FBattleMajorStatusRules::GetParalysisId(), 1, Speed));
	TestEqual(TEXT("Paralysis may produce Speed zero"), Speed, 0);

	FBattleDecision Decision;
	TestTrue(TEXT("A queue decision is valid"), FBattleDecision::TryCreateFight(
		1,
		MakeNumericId<FTrainerId>(PlayerTrainerValue),
		MakeNumericId<FBattlerId>(PlayerBattlerValue),
		MakeDefinitionId<FMoveId>(MoveName),
		MakeActiveSlotId(EBattleSide::Opponent, EBattlePosition::Left),
		Decision));
	FBattleActionQueueLockSpec LockSpec;
	LockSpec.BattleId = MakeNumericId<FBattleId>(7070);
	LockSpec.TurnId = MakeNumericId<FTurnId>(1);
	LockSpec.ResolutionId = MakeNumericId<FResolutionId>(1);
	FBattleActionOrderCandidate Candidate;
	Candidate.ActionId = MakeNumericId<FActionId>(1);
	Candidate.Decision = Decision;
	Candidate.OrderKey.CommandBand = EBattleActionCommandBand::Move;
	Candidate.OrderKey.EffectiveSpeed = 0;
	Candidate.OrderKey.ActingSlotId = MakeActiveSlotId(
		EBattleSide::Player, EBattlePosition::Left);
	Candidate.TargetClass = EBattleTargetClass::SelectedOpponent;
	Candidate.SelectedTargetBattlerId = MakeNumericId<FBattlerId>(OpponentBattlerValue);
	LockSpec.Candidates.Add(Candidate);
	FScriptedStatusRandom QueueRandom({});
	TArray<FBattleLockedAction> Locked;
	EBattleActionQueueError QueueError = EBattleActionQueueError::None;
	TestTrue(TEXT("The queue accepts effective Speed zero"),
		FBattleActionQueueResolver::TryLock(LockSpec, QueueRandom, Locked, QueueError));
	TestEqual(TEXT("The zero-Speed action remains locked"), Locked.Num(), 1);

	FBattleMajorStatusActionFacts Facts;
	Facts.StatusId = FBattleMajorStatusRules::GetParalysisId();
	FBattleMajorStatusActionResult Action;
	FScriptedStatusRandom DeniedRandom({
		{0, 3, 0, FBattleMajorStatusRules::GetParalysisActionGatePurpose()}
	});
	TestTrue(TEXT("The paralysis denial draw resolves"),
		FBattleMajorStatusRules::TryResolveBeforeAction(
			Facts,
			MakeRandomContext(FBattleMajorStatusRules::GetParalysisActionGatePurpose()),
			DeniedRandom,
			Action));
	TestEqual(TEXT("U[0,3] result zero denies"), Action.Outcome,
		EBattleMajorStatusActionOutcome::Denied);
	FScriptedStatusRandom AllowedRandom({
		{0, 3, 3, FBattleMajorStatusRules::GetParalysisActionGatePurpose()}
	});
	TestTrue(TEXT("The paralysis allowance draw resolves"),
		FBattleMajorStatusRules::TryResolveBeforeAction(
			Facts,
			MakeRandomContext(FBattleMajorStatusRules::GetParalysisActionGatePurpose()),
			AllowedRandom,
			Action));
	TestEqual(TEXT("A nonzero result allows"), Action.Outcome,
		EBattleMajorStatusActionOutcome::Allowed);

	const FBattlerId PlayerId = MakeNumericId<FBattlerId>(PlayerBattlerValue);
	const FBattlerId OpponentId = MakeNumericId<FBattlerId>(OpponentBattlerValue);
	TUniquePtr<FBattleEngine> OrderEngine;
	TestTrue(TEXT("The live Paralysis order engine is created"),
		TryCreateEngine(7077, false, 7, OrderEngine));
	TestTrue(TEXT("Paralysis is attached to the live actor"),
		FBattleC07BEngineFixture::ApplyStatus(
			*OrderEngine, PlayerId, FBattleMajorStatusRules::GetParalysisId()));
	TestTrue(TEXT("The paralyzed battle locks its actions"),
		LockAllFights(*OrderEngine, MakeDefinitionId<FMoveId>(MoveName)));
	const TArray<FBattleLockedAction> OrderedActions = OrderEngine->GetLockedActions();
	TestEqual(TEXT("Both live actions are locked"), OrderedActions.Num(), 2);
	if (OrderedActions.Num() == 2)
	{
		TestTrue(TEXT("Paralysis moves the originally faster player behind the opponent"),
			OrderedActions[0].Decision.GetActingBattlerId() == OpponentId
				&& OrderedActions[1].Decision.GetActingBattlerId() == PlayerId);
		TestEqual(TEXT("The live queue stores floor(101/2)"),
			OrderedActions[1].OrderKey.EffectiveSpeed, 50);
	}

	const FBattleMoveDefinition GateMove = MakeMove();
	TUniquePtr<FBattleEngine> GateEngine;
	TestTrue(TEXT("The live Paralysis gate engine is created"),
		TryCreateEngineWithMove(
			7078,
			false,
			GateMove,
			MakeUnique<FScriptedStatusRandom>(TArray<FExpectedDraw>({
				{0, 3, 0, FBattleMajorStatusRules::GetParalysisActionGatePurpose()}
			})),
			GateEngine,
			201));
	TestTrue(TEXT("Paralysis is attached to the faster gate actor"),
		FBattleC07BEngineFixture::ApplyStatus(
			*GateEngine, PlayerId, FBattleMajorStatusRules::GetParalysisId()));
	TestTrue(TEXT("The gate battle locks its actions"),
		LockAllFights(*GateEngine, GateMove.Id));
	TestTrue(TEXT("The gate actor remains first after floor(201/2)"),
		!GateEngine->GetLockedActions().IsEmpty()
			&& GateEngine->GetLockedActions()[0].Decision.GetActingBattlerId() == PlayerId);
	TestTrue(TEXT("The paralyzed action begins"),
		GateEngine->BeginNextLockedAction().WasAccepted());
	const int32 PPBeforeGate = FBattleC07BEngineFixture::GetCurrentPP(*GateEngine, PlayerId);
	const FBattleResolution GateResolution =
		GateEngine->CommitCurrentMoveAfterPreMoveGates();
	TestTrue(TEXT("Full paralysis consumes the action"), GateResolution.WasAccepted());
	TestEqual(TEXT("Full paralysis consumes no PP"),
		FBattleC07BEngineFixture::GetCurrentPP(*GateEngine, PlayerId), PPBeforeGate);
	const TArray<FBattleRandomDraw> GateTrace = GateEngine->ExportRandomTrace();
	TestEqual(TEXT("The live gate consumes exactly one U[0,3] draw"), GateTrace.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC07BFreezeTest,
	"PokemonSolarus.Battle.C07B.Freeze.ThawPaths",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC07BFreezeTest::RunTest(const FString& Parameters)
{
	using namespace BattleMajorStatusTests;
	(void)Parameters;
	FBattleMajorStatusActionFacts Facts;
	Facts.StatusId = FBattleMajorStatusRules::GetFreezeId();
	FBattleMajorStatusActionResult Action;
	FScriptedStatusRandom NaturalThaw({
		{0, 4, 0, FBattleMajorStatusRules::GetFreezeNaturalThawPurpose()}
	});
	TestTrue(TEXT("Natural thaw resolves"), FBattleMajorStatusRules::TryResolveBeforeAction(
		Facts,
		MakeRandomContext(FBattleMajorStatusRules::GetFreezeNaturalThawPurpose()),
		NaturalThaw,
		Action));
	TestTrue(TEXT("Natural result zero cures"), Action.bCureStatus);
	FScriptedStatusRandom FailedThaw({
		{0, 4, 4, FBattleMajorStatusRules::GetFreezeNaturalThawPurpose()}
	});
	TestTrue(TEXT("Failed natural thaw resolves"), FBattleMajorStatusRules::TryResolveBeforeAction(
		Facts,
		MakeRandomContext(FBattleMajorStatusRules::GetFreezeNaturalThawPurpose()),
		FailedThaw,
		Action));
	TestEqual(TEXT("A failed thaw denies"), Action.Outcome,
		EBattleMajorStatusActionOutcome::Denied);
	Facts.bMoveThawsUser = true;
	FScriptedStatusRandom ForcedThaw({});
	TestTrue(TEXT("A user-thawing move resolves before RNG"),
		FBattleMajorStatusRules::TryResolveBeforeAction(
			Facts,
			MakeRandomContext(FBattleMajorStatusRules::GetFreezeNaturalThawPurpose()),
			ForcedThaw,
			Action));
	TestTrue(TEXT("Forced user thaw consumes no draw"), ForcedThaw.IsExact());
	TestTrue(TEXT("Damaging Fire thaws a reached target"),
		FBattleMajorStatusRules::ShouldThawReachedTarget(
			FBattleMajorStatusRules::GetFreezeId(), EPokemonType::Fire, true, false, true));
	TestTrue(TEXT("The target-thaw hook works for a reached status move"),
		FBattleMajorStatusRules::ShouldThawReachedTarget(
			FBattleMajorStatusRules::GetFreezeId(), EPokemonType::Normal, false, true, true));
	TestFalse(TEXT("An unreached target never thaws"),
		FBattleMajorStatusRules::ShouldThawReachedTarget(
			FBattleMajorStatusRules::GetFreezeId(), EPokemonType::Fire, true, true, false));

	const FBattlerId PlayerId = MakeNumericId<FBattlerId>(PlayerBattlerValue);
	const FBattlerId OpponentId = MakeNumericId<FBattlerId>(OpponentBattlerValue);
	const FBattleMoveDefinition GateMove = MakeMove();
	TUniquePtr<FBattleEngine> GateEngine;
	TestTrue(TEXT("The live Freeze gate engine is created"),
		TryCreateEngineWithMove(
			7079,
			false,
			GateMove,
			MakeUnique<FScriptedStatusRandom>(TArray<FExpectedDraw>({
				{0, 4, 4, FBattleMajorStatusRules::GetFreezeNaturalThawPurpose()}
			})),
			GateEngine));
	TestTrue(TEXT("Freeze is attached to the live actor"),
		FBattleC07BEngineFixture::ApplyStatus(
			*GateEngine, PlayerId, FBattleMajorStatusRules::GetFreezeId()));
	TestTrue(TEXT("The frozen battle locks its actions"),
		LockAllFights(*GateEngine, GateMove.Id));
	TestTrue(TEXT("The frozen action begins"),
		GateEngine->BeginNextLockedAction().WasAccepted());
	const int32 PPBeforeFreeze = FBattleC07BEngineFixture::GetCurrentPP(*GateEngine, PlayerId);
	TestTrue(TEXT("Failed natural thaw consumes the action"),
		GateEngine->CommitCurrentMoveAfterPreMoveGates().WasAccepted());
	TestEqual(TEXT("Failed natural thaw consumes no PP"),
		FBattleC07BEngineFixture::GetCurrentPP(*GateEngine, PlayerId), PPBeforeFreeze);

	FBattleMoveDefinition UserThawMove = MakeMove();
	UserThawMove.Id = MakeDefinitionId<FMoveId>(TEXT("Move.C07B.ThawUser"));
	UserThawMove.Flags |= EBattleMoveFlags::ThawsUser;
	TUniquePtr<FBattleEngine> UserThawEngine;
	TestTrue(TEXT("The live forced-user-thaw engine is created"),
		TryCreateEngineWithMove(
			7080,
			false,
			UserThawMove,
			MakeUnique<FScriptedStatusRandom>(TArray<FExpectedDraw>()),
			UserThawEngine));
	TestTrue(TEXT("Freeze is attached before the user-thawing move"),
		FBattleC07BEngineFixture::ApplyStatus(
			*UserThawEngine, PlayerId, FBattleMajorStatusRules::GetFreezeId()));
	TestTrue(TEXT("The user-thaw battle locks its actions"),
		LockAllFights(*UserThawEngine, UserThawMove.Id));
	TestTrue(TEXT("The user-thaw action begins"),
		UserThawEngine->BeginNextLockedAction().WasAccepted());
	const int32 PPBeforeUserThaw =
		FBattleC07BEngineFixture::GetCurrentPP(*UserThawEngine, PlayerId);
	TestTrue(TEXT("The user-thawing move passes its live gate"),
		UserThawEngine->CommitCurrentMoveAfterPreMoveGates().WasAccepted());
	TestFalse(TEXT("The user-thawing move cures Freeze"),
		FBattleC07BEngineFixture::GetMajorStatus(*UserThawEngine, PlayerId).IsValid());
	TestEqual(TEXT("The user-thawing move spends PP after curing"),
		FBattleC07BEngineFixture::GetCurrentPP(*UserThawEngine, PlayerId),
		PPBeforeUserThaw - 1);
	TestEqual(TEXT("Forced user thaw consumes no random draw"),
		UserThawEngine->ExportRandomTrace().Num(), 0);

	const FBattleMoveDefinition TwoHitFire = MakeFireMove(
		TEXT("Move.C07B.TwoHitFire"), 10, true);
	TUniquePtr<FBattleEngine> TargetThawEngine;
	TestTrue(TEXT("The reached-target thaw engine is created"),
		TryCreateEngineWithMove(
			7081,
			false,
			TwoHitFire,
			MakeUnique<FScriptedStatusRandom>(TArray<FExpectedDraw>({
				{0, 15, 0, FBattleEffectExecutor::GetDamageRandomRulePurpose()},
				{0, 15, 0, FBattleEffectExecutor::GetDamageRandomRulePurpose()}
			})),
			TargetThawEngine));
	TestTrue(TEXT("The live target starts frozen"),
		FBattleC07BEngineFixture::ApplyStatus(
			*TargetThawEngine, OpponentId, FBattleMajorStatusRules::GetFreezeId()));
	FBattleEffectExecutionResult TargetThawResult;
	TestTrue(TEXT("The two-hit Fire move executes against live state"),
		FBattleC07BEngineFixture::ExecuteCatalogMove(
			*TargetThawEngine, PlayerId, OpponentId, TwoHitFire.Id, TargetThawResult));
	TArray<int32> DamageIndices;
	int32 StatusIndex = INDEX_NONE;
	for (int32 Index = 0; Index < TargetThawResult.Events.Num(); ++Index)
	{
		if (TargetThawResult.Events[Index].Type == EBattleEventType::Damage)
		{
			DamageIndices.Add(Index);
		}
		if (TargetThawResult.Events[Index].Type == EBattleEventType::StatusChanged)
		{
			StatusIndex = Index;
		}
	}
	TestTrue(TEXT("Reached-target thaw is ordered between the two Fire hits"),
		DamageIndices.Num() == 2
			&& StatusIndex > DamageIndices[0]
			&& StatusIndex < DamageIndices[1]);
	TestFalse(TEXT("The reached target is no longer frozen"),
		FBattleC07BEngineFixture::GetMajorStatus(*TargetThawEngine, OpponentId).IsValid());
	TestEqual(TEXT("Reached-target thaw removes only its status registrations"),
		FBattleC07BEngineFixture::GetActiveRegistrationCount(*TargetThawEngine), 0);

	const FBattleMoveDefinition KnockoutFire = MakeFireMove(
		TEXT("Move.C07B.KnockoutFire"), 1000, false);
	TUniquePtr<FBattleEngine> KnockoutEngine;
	TestTrue(TEXT("The knockout-thaw engine is created"),
		TryCreateEngineWithMove(
			7082,
			false,
			KnockoutFire,
			MakeUnique<FScriptedStatusRandom>(TArray<FExpectedDraw>({
				{0, 15, 0, FBattleEffectExecutor::GetDamageRandomRulePurpose()}
			})),
			KnockoutEngine));
	TestTrue(TEXT("The knockout target starts frozen"),
		FBattleC07BEngineFixture::ApplyStatus(
			*KnockoutEngine, OpponentId, FBattleMajorStatusRules::GetFreezeId()));
	TestTrue(TEXT("The knockout target is reduced to one HP"),
		FBattleC07BEngineFixture::SetCurrentHP(*KnockoutEngine, OpponentId, 1));
	FBattleEffectExecutionResult KnockoutResult;
	TestTrue(TEXT("The knockout Fire move executes"),
		FBattleC07BEngineFixture::ExecuteCatalogMove(
			*KnockoutEngine, PlayerId, OpponentId, KnockoutFire.Id, KnockoutResult));
	TestTrue(TEXT("The direct damage marks the target fainted"),
		FBattleC07BEngineFixture::IsFainted(*KnockoutEngine, OpponentId));
	TestFalse(TEXT("A knockout never emits a separate thaw event"),
		KnockoutResult.Events.ContainsByPredicate(
			[](const FBattleEffectExecutionEvent& Event)
			{
				return Event.Type == EBattleEventType::StatusChanged;
			}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC07BBurnTest,
	"PokemonSolarus.Battle.C07B.Burn.DamageAndResidual",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC07BBurnTest::RunTest(const FString& Parameters)
{
	using namespace BattleMajorStatusTests;
	(void)Parameters;
	TestTrue(TEXT("Burn penalizes ordinary Physical damage"),
		FBattleMajorStatusRules::ShouldApplyBurnPhysicalPenalty(
			FBattleMajorStatusRules::GetBurnId(), EBattleMoveCategory::Physical, false));
	TestFalse(TEXT("The explicit burn exception bypasses the penalty"),
		FBattleMajorStatusRules::ShouldApplyBurnPhysicalPenalty(
			FBattleMajorStatusRules::GetBurnId(), EBattleMoveCategory::Physical, true));
	TestFalse(TEXT("Special damage is not penalized"),
		FBattleMajorStatusRules::ShouldApplyBurnPhysicalPenalty(
			FBattleMajorStatusRules::GetBurnId(), EBattleMoveCategory::Special, false));

	FBattleFinalDamageInput Input;
	Input.AttackerLevel = 50;
	Input.AttackerStats = {100, 100, 100, 100, 100, 100};
	Input.DefenderStats = {100, 100, 100, 100, 100, 100};
	Input.MoveCategory = EBattleMoveCategory::Physical;
	Input.MovePower = 17;
	Input.WeatherModifierQ12 = FBattleFinalDamageCalculator::Q12Neutral;
	Input.StabModifierQ12 = FBattleFinalDamageCalculator::Q12Neutral;
	Input.TypeEffectiveness = {1, 1};
	Input.RandomContext = MakeRandomContext(MakeDefinitionId<FDefinitionId>(TEXT("Rule.C07B.Damage")));
	Input.bAttackerBurned = true;
	FScriptedStatusRandom BurnRandom({
		{0, 15, 0, Input.RandomContext.RulePurpose}
	});
	FBattleFinalDamageResult Damage;
	EBattleDamageCalculationError Error = EBattleDamageCalculationError::None;
	TestTrue(TEXT("Burned final damage resolves"),
		FBattleFinalDamageCalculator::TryCalculateFinalDamage(Input, BurnRandom, Damage, Error));
	TestEqual(TEXT("Burn floors base damage nine to four"), Damage.Damage, 4);
	Input.bBypassBurnPenalty = true;
	FScriptedStatusRandom BypassRandom({
		{0, 15, 0, Input.RandomContext.RulePurpose}
	});
	TestTrue(TEXT("Burn bypass final damage resolves"),
		FBattleFinalDamageCalculator::TryCalculateFinalDamage(Input, BypassRandom, Damage, Error));
	TestEqual(TEXT("The burn exception preserves base damage nine"), Damage.Damage, 9);

	FBattleMajorStatusResidualFacts ResidualFacts;
	ResidualFacts.StatusId = FBattleMajorStatusRules::GetBurnId();
	ResidualFacts.BaseMaximumHP = 160;
	FBattleMajorStatusResidualResult Residual;
	TestTrue(TEXT("Burn residual resolves"),
		FBattleMajorStatusRules::TryResolveResidual(ResidualFacts, Residual));
	TestEqual(TEXT("Burn uses floor(MaxHP/16)"), Residual.Damage, 10);
	ResidualFacts.BaseMaximumHP = 1;
	TestTrue(TEXT("Minimum Burn residual resolves"),
		FBattleMajorStatusRules::TryResolveResidual(ResidualFacts, Residual));
	TestEqual(TEXT("Burn residual has minimum one"), Residual.Damage, 1);

	const FBattlerId PlayerId = MakeNumericId<FBattlerId>(PlayerBattlerValue);
	const FBattlerId OpponentId = MakeNumericId<FBattlerId>(OpponentBattlerValue);
	const FBattleMoveDefinition LiveMove = MakeMove();
	TUniquePtr<FBattleEngine> HealthyEngine;
	TUniquePtr<FBattleEngine> BurnedEngine;
	TestTrue(TEXT("The healthy damage engine is created"),
		TryCreateEngineWithMove(
			7083,
			false,
			LiveMove,
			MakeUnique<FScriptedStatusRandom>(TArray<FExpectedDraw>({
				{0, 15, 0, FBattleEffectExecutor::GetDamageRandomRulePurpose()}
			})),
			HealthyEngine));
	TestTrue(TEXT("The burned damage engine is created"),
		TryCreateEngineWithMove(
			7084,
			false,
			LiveMove,
			MakeUnique<FScriptedStatusRandom>(TArray<FExpectedDraw>({
				{0, 15, 0, FBattleEffectExecutor::GetDamageRandomRulePurpose()}
			})),
			BurnedEngine));
	TestTrue(TEXT("Burn is attached to the live attacker"),
		FBattleC07BEngineFixture::ApplyStatus(
			*BurnedEngine, PlayerId, FBattleMajorStatusRules::GetBurnId()));
	FBattleEffectExecutionResult HealthyExecution;
	FBattleEffectExecutionResult BurnedExecution;
	TestTrue(TEXT("Healthy live damage executes"),
		FBattleC07BEngineFixture::ExecuteCatalogMove(
			*HealthyEngine, PlayerId, OpponentId, LiveMove.Id, HealthyExecution));
	TestTrue(TEXT("Burned live damage executes"),
		FBattleC07BEngineFixture::ExecuteCatalogMove(
			*BurnedEngine, PlayerId, OpponentId, LiveMove.Id, BurnedExecution));
	TestEqual(TEXT("The live Burn penalty halves at the final-damage step"),
		BurnedExecution.TotalActualDamage,
		HealthyExecution.TotalActualDamage / 2);

	const FBattleMoveDefinition KnockoutMove = MakeFireMove(
		TEXT("Move.C07B.BurnedKnockout"), 1000, false);
	TUniquePtr<FBattleEngine> TerminalEngine;
	TestTrue(TEXT("The normal-KO cleanup engine is created"),
		TryCreateEngineWithMove(
			7085,
			false,
			KnockoutMove,
			MakeUnique<FScriptedStatusRandom>(TArray<FExpectedDraw>({
				{0, 15, 0, FBattleEffectExecutor::GetDamageRandomRulePurpose()}
			})),
			TerminalEngine));
	TestTrue(TEXT("The surviving attacker begins the KO with Burn"),
		FBattleC07BEngineFixture::ApplyStatus(
			*TerminalEngine, PlayerId, FBattleMajorStatusRules::GetBurnId()));
	TestTrue(TEXT("The normal-KO battle locks its actions"),
		LockAllFights(*TerminalEngine, KnockoutMove.Id));
	TestTrue(TEXT("The normal-KO action begins"),
		TerminalEngine->BeginNextLockedAction().WasAccepted());
	TestTrue(TEXT("The normal-KO move commits"),
		TerminalEngine->CommitCurrentMoveAfterPreMoveGates().WasAccepted());
	TestTrue(TEXT("The normal-KO target resolves"),
		TerminalEngine->ResolveCurrentMoveTargets().WasAccepted());
	TestTrue(TEXT("The normal-KO effects resolve"),
		TerminalEngine->ExecuteCurrentMoveEffects().WasAccepted());
	TestEqual(TEXT("The normal move KO reaches terminal"),
		FBattleC07BEngineFixture::GetPhase(*TerminalEngine), EBattlePhase::Terminal);
	TestEqual(TEXT("BattleEnd cleanup removes the surviving Burn registrations"),
		FBattleC07BEngineFixture::GetActiveRegistrationCount(*TerminalEngine), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC07BPoisonToxicTest,
	"PokemonSolarus.Battle.C07B.PoisonToxic.ResidualStageSwitchAndCure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC07BPoisonToxicTest::RunTest(const FString& Parameters)
{
	using namespace BattleMajorStatusTests;
	(void)Parameters;
	FBattleMajorStatusResidualFacts Facts;
	Facts.StatusId = FBattleMajorStatusRules::GetPoisonId();
	Facts.BaseMaximumHP = 160;
	FBattleMajorStatusResidualResult Result;
	TestTrue(TEXT("Poison residual resolves"),
		FBattleMajorStatusRules::TryResolveResidual(Facts, Result));
	TestEqual(TEXT("Poison uses floor(MaxHP/8)"), Result.Damage, 20);

	Facts.StatusId = FBattleMajorStatusRules::GetToxicId();
	Facts.ToxicLayerEncoding = 1;
	for (int32 ExpectedStage = 1; ExpectedStage <= 15; ++ExpectedStage)
	{
		TestTrue(TEXT("A Toxic stage resolves"),
			FBattleMajorStatusRules::TryResolveResidual(Facts, Result));
		TestEqual(TEXT("Toxic increments before damage"), Result.ToxicStage, ExpectedStage);
		TestEqual(TEXT("Toxic damage uses the incremented stage"),
			Result.Damage, 10 * ExpectedStage);
		Facts.ToxicLayerEncoding = Result.ToxicLayerEncoding;
	}
	TestTrue(TEXT("Clamped Toxic resolves again"),
		FBattleMajorStatusRules::TryResolveResidual(Facts, Result));
	TestEqual(TEXT("Toxic clamps at stage fifteen"), Result.ToxicStage, 15);

	FBattleTriggerSubject Owner;
	TestTrue(TEXT("Toxic receives a valid trigger owner"),
		FBattleTriggerSubject::TryCreateBattler(MakeNumericId<FBattlerId>(PlayerBattlerValue), Owner));
	FBattleTriggerFramework Framework;
	EBattleTriggerError Error = EBattleTriggerError::None;
	TestTrue(TEXT("Toxic registers EndTurn and SwitchOut"),
		FBattleMajorStatusRules::TryRegisterTriggers(
			Framework, FBattleMajorStatusRules::GetToxicId(), Owner, TOptional<int32>(), Error));
	TArray<FBattleTriggerLifecycleFact> Lifecycle;
	Framework.DrainLifecycleFacts(Lifecycle);
	FBattleTriggerOperationContext LayerContext;
	LayerContext.ReentrancyToken = MakeNumericId<FBattleTriggerReentrancyToken>(1);
	for (const FBattleTriggerRegistrationState& Registration : Framework.GetActiveRegistrations())
	{
		TestTrue(TEXT("Both Toxic registrations share the encoded stage"),
			Framework.TryUpdateLayers(Registration.RegistrationId, 6, LayerContext, Error));
	}
	FBattleTriggerOperationContext ResetContext;
	ResetContext.ReentrancyToken = MakeNumericId<FBattleTriggerReentrancyToken>(2);
	for (const FBattleTriggerRegistrationState& Registration : Framework.GetActiveRegistrations())
	{
		TestTrue(TEXT("Switch resets every Toxic registration"),
			Framework.TryUpdateLayers(
				Registration.RegistrationId,
				FBattleMajorStatusRules::GetResetToxicLayerEncoding(),
				ResetContext,
				Error));
	}
	for (const FBattleTriggerRegistrationState& Registration : Framework.GetActiveRegistrations())
	{
		TestEqual(TEXT("Toxic switch reset returns to stage zero"), Registration.Layers, 1);
	}
	FBattleTriggerOperationContext CureContext;
	CureContext.ReentrancyToken = MakeNumericId<FBattleTriggerReentrancyToken>(3);
	TestTrue(TEXT("Toxic cure cleanup succeeds"), FBattleMajorStatusRules::TryCleanupTriggers(
		Framework,
		FBattleMajorStatusRules::GetToxicId(),
		Owner,
		EBattleTriggerCleanupReason::Removal,
		CureContext,
		Error));
	TestEqual(TEXT("Toxic cure removes both registrations"),
		Framework.GetActiveRegistrations().Num(), 0);

	const FBattlerId PlayerId = MakeNumericId<FBattlerId>(PlayerBattlerValue);
	const FBattlerId OpponentId = MakeNumericId<FBattlerId>(OpponentBattlerValue);
	const FBattleMoveDefinition ForcedMove = MakeForcedSwitchMove(
		TEXT("Move.C07B.ForcedToxicSwitch"));
	TUniquePtr<FBattleEngine> SwitchEngine;
	TestTrue(TEXT("The live Toxic switch engine is created"),
		TryCreateEngineWithMove(
			7086,
			true,
			ForcedMove,
			MakeUnique<FSeededBattleRandom>(86),
			SwitchEngine));
	TestTrue(TEXT("Toxic is attached to the outgoing live target"),
		FBattleC07BEngineFixture::ApplyStatus(
			*SwitchEngine, OpponentId, FBattleMajorStatusRules::GetToxicId()));
	TestTrue(TEXT("The hidden Toxic stage is raised before switching"),
		FBattleC07BEngineFixture::SetStatusLayers(
			*SwitchEngine,
			OpponentId,
			FBattleMajorStatusRules::GetToxicId(),
			6));
	FBattleEffectExecutionResult SwitchExecution;
	TestTrue(TEXT("The forced switch executes through live state"),
		FBattleC07BEngineFixture::ExecuteCatalogMove(
			*SwitchEngine,
			PlayerId,
			OpponentId,
			ForcedMove.Id,
			SwitchExecution));
	TestTrue(TEXT("The opponent reserve enters the active slot"),
		FBattleC07BEngineFixture::GetActiveBattler(
			*SwitchEngine,
			MakeActiveSlotId(EBattleSide::Opponent, EBattlePosition::Left))
			== MakeNumericId<FBattlerId>(OpponentReserveValue));
	TestTrue(TEXT("Toxic itself persists on the outgoing battler"),
		FBattleC07BEngineFixture::GetMajorStatus(*SwitchEngine, OpponentId)
			== FBattleMajorStatusRules::GetToxicId());
	TestEqual(TEXT("The live SwitchOut path resets Toxic to stage zero"),
		FBattleC07BEngineFixture::GetStatusLayerEncoding(
			*SwitchEngine,
			OpponentId,
			FBattleMajorStatusRules::GetToxicId()),
		FBattleMajorStatusRules::GetResetToxicLayerEncoding());

	const FBattleMoveDefinition CureMove = MakeConditionMove(
		TEXT("Move.C07B.CureToxic"),
		FBattleMajorStatusRules::GetToxicId(),
		EBattleMoveEffectKind::RemoveCondition);
	TUniquePtr<FBattleEngine> CureEngine;
	TestTrue(TEXT("The live Toxic cure engine is created"),
		TryCreateEngineWithMove(
			7087,
			false,
			CureMove,
			MakeUnique<FScriptedStatusRandom>(TArray<FExpectedDraw>()),
			CureEngine));
	TestTrue(TEXT("Toxic is attached before the live cure"),
		FBattleC07BEngineFixture::ApplyStatus(
			*CureEngine, OpponentId, FBattleMajorStatusRules::GetToxicId()));
	FBattleEffectExecutionResult CureExecution;
	TestTrue(TEXT("The live Toxic cure executes"),
		FBattleC07BEngineFixture::ExecuteCatalogMove(
			*CureEngine, PlayerId, OpponentId, CureMove.Id, CureExecution));
	TestFalse(TEXT("The live cure clears Toxic"),
		FBattleC07BEngineFixture::GetMajorStatus(*CureEngine, OpponentId).IsValid());
	TestEqual(TEXT("The live cure removes both Toxic registrations"),
		FBattleC07BEngineFixture::GetActiveRegistrationCount(*CureEngine), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC07BEndTurnIntegrationTest,
	"PokemonSolarus.Battle.C07B.EndTurn.OrderFaintReplacementAndNextTurn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC07BEndTurnIntegrationTest::RunTest(const FString& Parameters)
{
	using namespace BattleMajorStatusTests;
	(void)Parameters;
	const FBattlerId PlayerId = MakeNumericId<FBattlerId>(PlayerBattlerValue);
	const FBattlerId OpponentId = MakeNumericId<FBattlerId>(OpponentBattlerValue);

	TUniquePtr<FBattleEngine> OrderedEngine;
	TestTrue(TEXT("The ordered residual engine is created"),
		TryCreateEngine(7081, false, 11, OrderedEngine));
	TestTrue(TEXT("Player Burn is seeded"), FBattleC07BEngineFixture::ApplyStatus(
		*OrderedEngine, PlayerId, FBattleMajorStatusRules::GetBurnId()));
	TestTrue(TEXT("Opponent Poison is seeded"), FBattleC07BEngineFixture::ApplyStatus(
		*OrderedEngine, OpponentId, FBattleMajorStatusRules::GetPoisonId()));
	TestTrue(TEXT("The engine enters EndOfTurn"),
		FBattleC07BEngineFixture::PrepareEndTurn(*OrderedEngine));
	const FBattleResolution OrderedResolution = OrderedEngine->ResolveEndTurn();
	TestTrue(TEXT("The residual pass is accepted"), OrderedResolution.WasAccepted());
	TArray<FBattlerId> DamageTargets;
	for (const FBattleEvent& Event : OrderedResolution.GetEvents())
	{
		if (Event.GetType() == EBattleEventType::Damage && !Event.GetTargets().IsEmpty())
		{
			DamageTargets.Add(Event.GetTargets()[0].BattlerId);
		}
	}
	TestEqual(TEXT("Exactly two residual mutations occur"), DamageTargets.Num(), 2);
	TestTrue(TEXT("Order nine Poison resolves before order ten Burn"),
		DamageTargets[0] == OpponentId && DamageTargets[1] == PlayerId);
	TestEqual(TEXT("Poison removes one eighth"),
		FBattleC07BEngineFixture::GetCurrentHP(*OrderedEngine, OpponentId), 140);
	TestEqual(TEXT("Burn removes one sixteenth"),
		FBattleC07BEngineFixture::GetCurrentHP(*OrderedEngine, PlayerId), 150);
	TestEqual(TEXT("A surviving pass starts the next turn"),
		FBattleC07BEngineFixture::GetPhase(*OrderedEngine), EBattlePhase::Selecting);
	TestEqual(TEXT("The next turn is turn two"),
		FBattleC07BEngineFixture::GetTurnId(*OrderedEngine).GetValue(), 2ULL);

	TUniquePtr<FBattleEngine> ReplacementEngine;
	TestTrue(TEXT("The replacement engine is created"),
		TryCreateEngine(7082, true, 12, ReplacementEngine));
	TestTrue(TEXT("Replacement Poison is seeded"), FBattleC07BEngineFixture::ApplyStatus(
		*ReplacementEngine, OpponentId, FBattleMajorStatusRules::GetPoisonId()));
	TestTrue(TEXT("The residual target is set to one HP"),
		FBattleC07BEngineFixture::SetCurrentHP(*ReplacementEngine, OpponentId, 1));
	TestTrue(TEXT("The replacement engine enters EndOfTurn"),
		FBattleC07BEngineFixture::PrepareEndTurn(*ReplacementEngine));
	const FBattleResolution ReplacementResolution = ReplacementEngine->ResolveEndTurn();
	TestTrue(TEXT("Residual faint resolution is accepted"), ReplacementResolution.WasAccepted());
	TestEqual(TEXT("A living reserve requests replacement"),
		FBattleC07BEngineFixture::GetPhase(*ReplacementEngine),
		EBattlePhase::MandatoryReplacement);
	TestEqual(TEXT("Exactly one replacement is pending"),
		FBattleC07BEngineFixture::GetPendingReplacementCount(*ReplacementEngine), 1);
	int32 HpChangedIndex = INDEX_NONE;
	int32 FaintedIndex = INDEX_NONE;
	for (int32 Index = 0; Index < ReplacementResolution.GetEvents().Num(); ++Index)
	{
		const EBattleEventType Type = ReplacementResolution.GetEvents()[Index].GetType();
		if (Type == EBattleEventType::HPChanged) HpChangedIndex = Index;
		if (Type == EBattleEventType::Fainted) FaintedIndex = Index;
	}
	TestTrue(TEXT("Faint processing follows its HP mutation immediately"),
		HpChangedIndex != INDEX_NONE && FaintedIndex == HpChangedIndex + 1);
	const TArray<FBattleDecisionRequest> ReplacementRequests =
		ReplacementEngine->GetPendingDecisionRequests();
	TestEqual(TEXT("The residual replacement exposes one decision request"),
		ReplacementRequests.Num(), 1);
	if (ReplacementRequests.Num() == 1)
	{
		FBattleDecision ReplacementDecision;
		TestTrue(TEXT("The residual replacement decision is created"),
			FBattleDecision::TryCreateReplacement(
				ReplacementRequests[0].GetStateVersion(),
				ReplacementRequests[0].GetDecisionOwnerTrainerId(),
				MakePartySlotId(1),
				ReplacementRequests[0].GetActingSlotId(),
				ReplacementDecision));
		TestTrue(TEXT("The residual replacement is accepted"),
			ReplacementEngine->SubmitDecision(ReplacementDecision).WasAccepted());
		TestEqual(TEXT("Replacement completion returns to EndOfTurn"),
			FBattleC07BEngineFixture::GetPhase(*ReplacementEngine),
			EBattlePhase::EndOfTurn);
		TestTrue(TEXT("The completed residual pass advances without running twice"),
			ReplacementEngine->ResolveEndTurn().WasAccepted());
		TestEqual(TEXT("Residual replacement continuation reaches next-turn selection"),
			FBattleC07BEngineFixture::GetPhase(*ReplacementEngine),
			EBattlePhase::Selecting);
		TestEqual(TEXT("Residual replacement continuation reaches turn two"),
			FBattleC07BEngineFixture::GetTurnId(*ReplacementEngine).GetValue(), 2ULL);
	}

	TUniquePtr<FBattleEngine> TerminalEngine;
	TestTrue(TEXT("The terminal engine is created"),
		TryCreateEngine(7083, false, 13, TerminalEngine));
	TestTrue(TEXT("Terminal Poison is seeded"), FBattleC07BEngineFixture::ApplyStatus(
		*TerminalEngine, OpponentId, FBattleMajorStatusRules::GetPoisonId()));
	TestTrue(TEXT("The terminal target is set to one HP"),
		FBattleC07BEngineFixture::SetCurrentHP(*TerminalEngine, OpponentId, 1));
	TestTrue(TEXT("The terminal engine enters EndOfTurn"),
		FBattleC07BEngineFixture::PrepareEndTurn(*TerminalEngine));
	const FBattleResolution TerminalResolution = TerminalEngine->ResolveEndTurn();
	TestTrue(TEXT("Terminal residual resolution is accepted"), TerminalResolution.WasAccepted());
	TestEqual(TEXT("No reserve produces a terminal state"),
		FBattleC07BEngineFixture::GetPhase(*TerminalEngine), EBattlePhase::Terminal);
	TestEqual(TEXT("The player wins the terminal residual"),
		FBattleC07BEngineFixture::GetOutcome(*TerminalEngine), EBattleOutcome::Victory);
	TestTrue(TEXT("The terminal resolution emits BattleEnded"),
		TerminalResolution.GetEvents().ContainsByPredicate(
			[](const FBattleEvent& Event)
			{
				return Event.GetType() == EBattleEventType::BattleEnded;
			}));
	TestEqual(TEXT("Terminal residual cleanup removes every C07A registration"),
		FBattleC07BEngineFixture::GetActiveRegistrationCount(*TerminalEngine), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC07BDeterminismTest,
	"PokemonSolarus.Battle.C07B.Determinism.SeedEventsReplayAndPermanentState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC07BDeterminismTest::RunTest(const FString& Parameters)
{
	using namespace BattleMajorStatusTests;
	(void)Parameters;
	const FBattlerId PlayerId = MakeNumericId<FBattlerId>(PlayerBattlerValue);
	const FBattlerId OpponentId = MakeNumericId<FBattlerId>(OpponentBattlerValue);
	TUniquePtr<FBattleEngine> First;
	TUniquePtr<FBattleEngine> Second;
	TestTrue(TEXT("The first deterministic engine is created"),
		TryCreateEngine(7090, false, 999, First));
	TestTrue(TEXT("The second deterministic engine is created"),
		TryCreateEngine(7090, false, 999, Second));
	const FPokemonBattleStats PermanentBefore =
		FBattleC07BEngineFixture::GetPermanentStats(*First, PlayerId);
	FBattleMajorStatusActionResult FirstGate;
	FBattleMajorStatusActionResult SecondGate;
	TestTrue(TEXT("The first semantic status draw resolves"),
		FBattleC07BEngineFixture::ConsumeParalysisGate(*First, FirstGate));
	TestTrue(TEXT("The second semantic status draw resolves"),
		FBattleC07BEngineFixture::ConsumeParalysisGate(*Second, SecondGate));
	TestEqual(TEXT("Identical seeds produce the same status gate"),
		FirstGate.Outcome, SecondGate.Outcome);
	for (FBattleEngine* Engine : {First.Get(), Second.Get()})
	{
		TestTrue(TEXT("Deterministic Burn is seeded"),
			FBattleC07BEngineFixture::ApplyStatus(
				*Engine, PlayerId, FBattleMajorStatusRules::GetBurnId()));
		TestTrue(TEXT("Deterministic Poison is seeded"),
			FBattleC07BEngineFixture::ApplyStatus(
				*Engine, OpponentId, FBattleMajorStatusRules::GetPoisonId()));
		TestTrue(TEXT("The deterministic engine enters EndOfTurn"),
			FBattleC07BEngineFixture::PrepareEndTurn(*Engine));
		TestTrue(TEXT("The deterministic residual pass is accepted"),
			Engine->ResolveEndTurn().WasAccepted());
	}
	const TArray<FBattleRandomDraw> FirstTrace = First->ExportRandomTrace();
	const TArray<FBattleRandomDraw> SecondTrace = Second->ExportRandomTrace();
	TestTrue(TEXT("Identical seeds preserve the exact RNG trace"), FirstTrace == SecondTrace);
	const TArray<FBattleEvent> FirstEvents = FBattleC07BEngineFixture::GetOrderedEvents(*First);
	const TArray<FBattleEvent> SecondEvents = FBattleC07BEngineFixture::GetOrderedEvents(*Second);
	TestEqual(TEXT("Identical runs emit the same event count"),
		FirstEvents.Num(), SecondEvents.Num());
	for (int32 Index = 0; Index < FirstEvents.Num(); ++Index)
	{
		TestEqual(TEXT("Event types are deterministic"),
			FirstEvents[Index].GetType(), SecondEvents[Index].GetType());
		TestEqual(TEXT("Event ordinals are deterministic"),
			FirstEvents[Index].GetEventOrdinal(), SecondEvents[Index].GetEventOrdinal());
	}

	const FBattleReplayRecord FirstRecord = First->ExportReplayRecord();
	const FBattleReplayRecord SecondRecord = Second->ExportReplayRecord();
	TestTrue(TEXT("The first replay is valid"), FirstRecord.IsValid());
	TestEqual(TEXT("Replay schema remains current"), FirstRecord.GetSchemaVersion(), 5U);
	TArray<uint8> FirstBytes;
	TArray<uint8> SecondBytes;
	FBattleRejection FirstRejection;
	FBattleRejection SecondRejection;
	TestTrue(TEXT("The first replay serializes"),
		FBattleReplaySerializer::TrySerializeCanonical(
			FirstRecord, FirstBytes, FirstRejection));
	TestTrue(TEXT("The second replay serializes"),
		FBattleReplaySerializer::TrySerializeCanonical(
			SecondRecord, SecondBytes, SecondRejection));
	TestTrue(TEXT("Identical runs serialize to identical replay bytes"),
		FirstBytes == SecondBytes);

	const FPokemonBattleStats PermanentAfter =
		FBattleC07BEngineFixture::GetPermanentStats(*First, PlayerId);
	TestTrue(TEXT("C07B never mutates permanent stats"),
		StatsEqual(PermanentBefore, PermanentAfter));
	const FBattleReplayInputs Inputs = First->ExportReplayInputs();
	const FBattlePartyEntrySetup* FrozenPlayer = Inputs.Setup.FindBattler(PlayerId);
	TestNotNull(TEXT("The frozen setup still contains the player"), FrozenPlayer);
	if (FrozenPlayer != nullptr)
	{
		TestEqual(TEXT("Residual HP never mutates frozen setup HP"),
			FrozenPlayer->CurrentHP, 160);
		TestTrue(TEXT("Frozen setup stats remain permanent"),
			StatsEqual(FrozenPlayer->Stats, PermanentBefore));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
