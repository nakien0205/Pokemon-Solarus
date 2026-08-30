#if WITH_DEV_AUTOMATION_TESTS

#include "Battle/BattleAllyActionPowerModifier.h"
#include "Battle/BattleDataTableAdapter.h"
#include "Battle/BattleDataTableRows.h"
#include "Battle/BattleEffectExecutor.h"
#include "Battle/BattleEvent.h"
#include "Battle/BattleState.h"
#include "BattleTestFactories.h"
#include "BattleTestRandom.h"
#include "Engine/DataTable.h"
#include "Misc/AutomationTest.h"
#include "UObject/UObjectGlobals.h"

namespace BattleAllyActionPowerModifierTestsPrivate
{
	using BattleTest::FSequenceBattleRandom;
	using BattleTest::FStrictBattleRandom;
	using BattleTest::MakeActiveSlotId;
	using BattleTest::MakeDefinitionId;
	using BattleTest::MakeNumericId;

	constexpr uint64 R3PlayerTrainerValue = 1;
	constexpr uint64 R3OpponentTrainerValue = 2;
	constexpr uint64 R3PlayerLeftValue = 11;
	constexpr uint64 R3PlayerRightValue = 12;
	constexpr uint64 R3OpponentLeftValue = 21;
	constexpr uint64 R3OpponentRightValue = 22;
	const TCHAR* const R3RegistrationMoveName =
		TEXT("Move.C10R3.RegisterAllyActionPowerModifier");
	const TCHAR* const R3TargetMoveName = TEXT("Move.C10R3.TargetAction");

	FMoveId GetR3RegistrationMoveId()
	{
		return MakeDefinitionId<FMoveId>(R3RegistrationMoveName);
	}

	FMoveId GetR3TargetMoveId()
	{
		return MakeDefinitionId<FMoveId>(R3TargetMoveName);
	}

	FBattleBattlerTarget MakeR3Target(
		const EBattleSide Side,
		const EBattlePosition Position,
		const uint64 BattlerValue)
	{
		return {
			MakeActiveSlotId(Side, Position),
			MakeNumericId<FBattlerId>(BattlerValue)};
	}

	FBattleMoveDefinition MakeR3RegistrationMove()
	{
		FBattleMoveDefinition Move;
		Move.Id = GetR3RegistrationMoveId();
		Move.Type = EPokemonType::Normal;
		Move.Category = EBattleMoveCategory::Status;
		Move.Power = 0;
		Move.bAlwaysHits = true;
		Move.Accuracy = 0;
		Move.bUsesPP = true;
		Move.BasePP = 5;
		Move.bAllowsPPBoosts = true;
		Move.Priority = 2;
		Move.TargetClass = EBattleTargetClass::SelectedAlly;
		FBattleMoveEffectDescriptor Effect;
		Effect.Order = 0;
		Effect.Kind = EBattleMoveEffectKind::RegisterAllyActionPowerModifier;
		Effect.Target = EBattleEffectTarget::ResolvedTarget;
		Effect.ChanceNumerator = 1;
		Effect.ChanceDenominator = 1;
		Effect.MagnitudeNumerator = 3;
		Effect.MagnitudeDenominator = 2;
		Move.Effects.Add(Effect);
		return Move;
	}

	FName GetR3PokemonTypeName(const int32 TypeIndex)
	{
		static const FName Names[] =
		{
			TEXT("Normal"), TEXT("Fire"), TEXT("Water"), TEXT("Electric"),
			TEXT("Grass"), TEXT("Ice"), TEXT("Fighting"), TEXT("Poison"),
			TEXT("Ground"), TEXT("Flying"), TEXT("Psychic"), TEXT("Bug"),
			TEXT("Rock"), TEXT("Ghost"), TEXT("Dragon"), TEXT("Dark"),
			TEXT("Steel"), TEXT("Fairy")
		};
		check(TypeIndex >= 0 && TypeIndex < UE_ARRAY_COUNT(Names));
		return Names[TypeIndex];
	}

	TArray<FBattleTypeChartEntry> MakeR3NeutralTypeChart()
	{
		TArray<FBattleTypeChartEntry> Entries;
		Entries.Reserve(FBattleTypeChart::EntryCount);
		for (int32 Attacking = 0;
			Attacking < FBattleTypeChart::TypeCount;
			++Attacking)
		{
			for (int32 Defending = 0;
				Defending < FBattleTypeChart::TypeCount;
				++Defending)
			{
				Entries.Add({
					static_cast<EPokemonType>(Attacking),
					static_cast<EPokemonType>(Defending),
					1,
					1});
			}
		}
		return Entries;
	}

	template <typename RowType>
	UDataTable* MakeR3TransientTable()
	{
		UDataTable* Table = NewObject<UDataTable>(GetTransientPackage());
		check(Table != nullptr);
		Table->RowStruct = RowType::StaticStruct();
		return Table;
	}

	FBattleDataTableSet MakeR3RegistrationTables()
	{
		FBattleDataTableSet Tables;
		Tables.SpeciesForms = MakeR3TransientTable<FBattleSpeciesFormTableRow>();
		Tables.Natures = MakeR3TransientTable<FBattleNatureTableRow>();
		UDataTable* Moves = MakeR3TransientTable<FBattleMoveTableRow>();
		Tables.Moves = Moves;
		Tables.Abilities = MakeR3TransientTable<FBattleAbilityTableRow>();
		Tables.Items = MakeR3TransientTable<FBattleItemTableRow>();
		Tables.Conditions = MakeR3TransientTable<FBattleConditionTableRow>();
		UDataTable* TypeChart = MakeR3TransientTable<FBattleTypeChartTableRow>();
		Tables.TypeChart = TypeChart;

		FBattleMoveTableRow Move;
		Move.Type = FName(TEXT("Normal"));
		Move.Category = FName(TEXT("Status"));
		Move.Power = 0;
		Move.bAlwaysHits = true;
		Move.Accuracy = 0;
		Move.bUsesPP = true;
		Move.BasePP = 5;
		Move.bAllowsPPBoosts = true;
		Move.Priority = 2;
		Move.TargetClass = FName(TEXT("SelectedAlly"));
		FBattleMoveEffectTableRow Effect;
		Effect.Kind = FName(TEXT("RegisterAllyActionPowerModifier"));
		Effect.Target = FName(TEXT("ResolvedTarget"));
		Effect.MagnitudeNumerator = 3;
		Effect.MagnitudeDenominator = 2;
		Move.Effects.Add(Effect);
		Moves->AddRow(FName(R3RegistrationMoveName), Move);

		for (int32 Attacking = 0;
			Attacking < FBattleTypeChart::TypeCount;
			++Attacking)
		{
			FBattleTypeChartTableRow Row;
			for (int32 Defending = 0;
				Defending < FBattleTypeChart::TypeCount;
				++Defending)
			{
				Row.Entries.Add({GetR3PokemonTypeName(Defending), 1, 1});
			}
			TypeChart->AddRow(GetR3PokemonTypeName(Attacking), Row);
		}
		return Tables;
	}

	FBattleEffectHookResult MakeR3AppliedHookResult()
	{
		FBattleEffectHookResult Result;
		Result.Outcome = EBattleEffectExecutionOutcome::Applied;
		Result.RuleId = MakeDefinitionId<FDefinitionId>(
			TEXT("Rule.C10R3.ValidationOnly"));
		return Result;
	}

	class FR3ValidationOnlyExecutionContext final
		: public IBattleEffectExecutionContext
	{
	public:
		bool bPrevalidateCalled = false;

		virtual bool PrevalidateRequest(
			const FBattleEffectExecutionRequest& Request) const override
		{
			(void)Request;
			const_cast<FR3ValidationOnlyExecutionContext*>(this)
				->bPrevalidateCalled = true;
			return true;
		}

		virtual FBattleEffectHookResult CheckReachability(
			const FBattleMoveDefinition& Move,
			const FBattleResolvedTarget& Target) override
		{
			(void)Move;
			(void)Target;
			return MakeR3AppliedHookResult();
		}

		virtual FBattleEffectHookResult CheckProtection(
			const FBattleMoveDefinition& Move,
			const FBattleResolvedTarget& Target) override
		{
			(void)Move;
			(void)Target;
			return MakeR3AppliedHookResult();
		}

		virtual FBattleEffectHookResult CheckTryHit(
			const FBattleMoveDefinition& Move,
			const FBattleResolvedTarget& Target) override
		{
			(void)Move;
			(void)Target;
			return MakeR3AppliedHookResult();
		}

		virtual FBattleEffectHookResult CheckMoveImmunity(
			const FBattleMoveDefinition& Move,
			const FBattleResolvedTarget& Target) override
		{
			(void)Move;
			(void)Target;
			return MakeR3AppliedHookResult();
		}

		virtual FBattleEffectHookResult CheckAbilityImmunity(
			const FBattleMoveDefinition& Move,
			const FBattleResolvedTarget& Target) override
		{
			(void)Move;
			(void)Target;
			return MakeR3AppliedHookResult();
		}

		virtual FBattleEffectHookResult CheckItemImmunity(
			const FBattleMoveDefinition& Move,
			const FBattleResolvedTarget& Target) override
		{
			(void)Move;
			(void)Target;
			return MakeR3AppliedHookResult();
		}

		virtual FBattleEffectHookResult ApplyProtectionBreaking(
			const FBattleMoveDefinition& Move,
			const FBattleResolvedTarget& Target) override
		{
			(void)Move;
			(void)Target;
			return MakeR3AppliedHookResult();
		}

		virtual bool TryBuildAccuracyInput(
			const FBattleMoveDefinition& Move,
			const FBattleResolvedTarget& Target,
			FBattleAccuracyCheckInput& OutInput) override
		{
			(void)Move;
			(void)Target;
			OutInput = FBattleAccuracyCheckInput();
			return false;
		}

		virtual bool TryBuildCriticalInput(
			const FBattleMoveDefinition& Move,
			const FBattleResolvedTarget& Target,
			FBattleCriticalCheckInput& OutInput) override
		{
			(void)Move;
			(void)Target;
			OutInput = FBattleCriticalCheckInput();
			return false;
		}

		virtual bool TryBuildDamageInput(
			const FBattleMoveDefinition& Move,
			const FBattleResolvedTarget& Target,
			const bool bSpreadAcrossMultipleTargets,
			FBattleFinalDamageInput& OutInput) override
		{
			(void)Move;
			(void)Target;
			(void)bSpreadAcrossMultipleTargets;
			OutInput = FBattleFinalDamageInput();
			return false;
		}

		virtual bool IsSourceAbleToContinue() const override { return true; }

		virtual bool IsTargetAbleToContinue(
			const FBattleResolvedTarget& Target) const override
		{
			(void)Target;
			return true;
		}

		virtual FBattleEffectHookResult CheckEffectEligibility(
			const FBattleMoveEffectDescriptor& Effect,
			const FBattleResolvedTarget& Target) override
		{
			(void)Effect;
			(void)Target;
			return MakeR3AppliedHookResult();
		}

		virtual bool TryGetHp(
			const FBattleResolvedTarget& Target,
			int32& OutCurrentHP,
			int32& OutMaximumHP) const override
		{
			(void)Target;
			OutCurrentHP = 100;
			OutMaximumHP = 100;
			return true;
		}

		virtual FBattleEffectHookResult ApplyHpDelta(
			const FBattleResolvedTarget& Target,
			const int32 RequestedDelta) override
		{
			(void)Target;
			(void)RequestedDelta;
			return MakeR3AppliedHookResult();
		}

		virtual FBattleEffectHookResult ApplyNonHpEffect(
			const FBattleMoveEffectDescriptor& Effect,
			const FBattleResolvedTarget& Target) override
		{
			(void)Effect;
			(void)Target;
			return MakeR3AppliedHookResult();
		}

		virtual void RunImmediateUpdate(
			const FBattleResolvedTarget& Target) override
		{
			(void)Target;
		}

		virtual bool TryBuildEventTarget(
			const FBattleResolvedTarget& Target,
			FBattleEventTarget& OutTarget) const override
		{
			(void)Target;
			OutTarget = FBattleEventTarget();
			return false;
		}
	};

	FBattleBattlerState MakeR3Battler(
		const uint64 TrainerValue,
		const uint64 BattlerValue)
	{
		FBattleBattlerState Battler;
		Battler.TrainerId = MakeNumericId<FTrainerId>(TrainerValue);
		Battler.BattlerId = MakeNumericId<FBattlerId>(BattlerValue);
		Battler.PermanentStats = {100, 100, 100, 100, 100, 100};
		Battler.CurrentHP = 100;
		return Battler;
	}

	FBattleActivePositionState MakeR3Position(
		const EBattleSide Side,
		const EBattlePosition Position,
		const uint64 TrainerValue,
		const uint64 BattlerValue)
	{
		FBattleActivePositionState Active;
		Active.ActiveSlotId = MakeActiveSlotId(Side, Position);
		Active.bAvailable = true;
		Active.TrainerId = MakeNumericId<FTrainerId>(TrainerValue);
		Active.BattlerId = MakeNumericId<FBattlerId>(BattlerValue);
		return Active;
	}

	struct FR3Board
	{
		TArray<FBattleBattlerState> Battlers;
		TArray<FBattleActivePositionState> ActivePositions;
	};

	FR3Board MakeR3Board()
	{
		FR3Board Board;
		Board.Battlers =
		{
			MakeR3Battler(R3PlayerTrainerValue, R3PlayerLeftValue),
			MakeR3Battler(R3PlayerTrainerValue, R3PlayerRightValue),
			MakeR3Battler(R3OpponentTrainerValue, R3OpponentLeftValue),
			MakeR3Battler(R3OpponentTrainerValue, R3OpponentRightValue)
		};
		Board.ActivePositions =
		{
			MakeR3Position(EBattleSide::Player, EBattlePosition::Left,
				R3PlayerTrainerValue, R3PlayerLeftValue),
			MakeR3Position(EBattleSide::Player, EBattlePosition::Right,
				R3PlayerTrainerValue, R3PlayerRightValue),
			MakeR3Position(EBattleSide::Opponent, EBattlePosition::Left,
				R3OpponentTrainerValue, R3OpponentLeftValue),
			MakeR3Position(EBattleSide::Opponent, EBattlePosition::Right,
				R3OpponentTrainerValue, R3OpponentRightValue)
		};
		return Board;
	}

	FBattleLockedActionState MakeR3FightAction(
		const uint64 ActionValue,
		const uint64 TrainerValue,
		const uint64 BattlerValue,
		const EBattleSide Side,
		const EBattlePosition Position,
		const FMoveId MoveId,
		const FActiveSlotId TargetSlot,
		const bool bStarted,
		const bool bFinished)
	{
		FBattleDecision Decision;
		check(FBattleDecision::TryCreateFight(
			1,
			MakeNumericId<FTrainerId>(TrainerValue),
			MakeNumericId<FBattlerId>(BattlerValue),
			MoveId,
			TargetSlot,
			Decision));
		FBattleLockedActionState Action;
		Action.ActionId = MakeNumericId<FActionId>(ActionValue);
		Action.QueueOrdinal = ActionValue;
		Action.Decision = MoveTemp(Decision);
		Action.OrderKey.ActingSlotId = MakeActiveSlotId(Side, Position);
		Action.TargetClass = MoveId == GetR3RegistrationMoveId()
			? EBattleTargetClass::SelectedAlly
			: EBattleTargetClass::SelectedOpponent;
		Action.bStarted = bStarted;
		Action.bFinished = bFinished;
		return Action;
	}

	TArray<FBattleLockedActionState> MakeR3SourceAndPendingTargetActions()
	{
		const FBattleBattlerTarget Source = MakeR3Target(
			EBattleSide::Player, EBattlePosition::Left, R3PlayerLeftValue);
		const FBattleBattlerTarget Ally = MakeR3Target(
			EBattleSide::Player, EBattlePosition::Right, R3PlayerRightValue);
		return {
			MakeR3FightAction(10, R3PlayerTrainerValue, R3PlayerLeftValue,
				EBattleSide::Player, EBattlePosition::Left,
				GetR3RegistrationMoveId(), Ally.ActiveSlotId, true, false),
			MakeR3FightAction(20, R3PlayerTrainerValue, R3PlayerRightValue,
				EBattleSide::Player, EBattlePosition::Right,
				GetR3TargetMoveId(),
				MakeActiveSlotId(EBattleSide::Opponent, EBattlePosition::Left),
				false, false)
		};
	}

	EBattleAllyActionPowerModifierRegistrationOutcome RegisterR3(
		const EBattleFormat Format,
		const FTurnId TurnId,
		const FR3Board& Board,
		const TConstArrayView<FBattleLockedActionState> Actions,
		TArray<FBattleAllyActionPowerModifierRegistration>& Registrations,
		int32& OutBefore,
		int32& OutAfter)
	{
		return FBattleAllyActionPowerModifier::TryRegister(
			Format,
			TurnId,
			MakeNumericId<FActionId>(10),
			GetR3RegistrationMoveId(),
			MakeR3Target(EBattleSide::Player, EBattlePosition::Left,
				R3PlayerLeftValue),
			MakeR3Target(EBattleSide::Player, EBattlePosition::Right,
				R3PlayerRightValue),
			3,
			2,
			Board.Battlers,
			Board.ActivePositions,
			Actions,
			Registrations,
			OutBefore,
			OutAfter);
	}

	FPokemonBattleStats MakeR3DamageStats()
	{
		return {100, 100, 100, 100, 100, 100};
	}

	FBattleFinalDamageInput MakeR3DamageInput()
	{
		FBattleFinalDamageInput Input;
		Input.AttackerLevel = 50;
		Input.AttackerStats = MakeR3DamageStats();
		Input.DefenderStats = MakeR3DamageStats();
		Input.MoveCategory = EBattleMoveCategory::Physical;
		Input.MovePower = 100;
		Input.TypeEffectiveness = {1, 1};
		Input.RandomContext.BattleId = MakeNumericId<FBattleId>(1);
		Input.RandomContext.TurnId = MakeNumericId<FTurnId>(1);
		Input.RandomContext.ActionId = MakeNumericId<FActionId>(20);
		Input.RandomContext.ResolutionId = MakeNumericId<FResolutionId>(1);
		Input.RandomContext.RulePurpose = MakeDefinitionId<FDefinitionId>(
			TEXT("Rule.C10R3.DamageRandom"));
		return Input;
	}
}

using namespace BattleAllyActionPowerModifierTestsPrivate;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC10R3ActionModifierContractTest,
	"PokemonSolarus.Battle.C05B.C10ActionModifiers.Contract.DescriptorAdapterCatalogAndRuntime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBattleC10R3ActionModifierContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	bool bValid = TestEqual(TEXT("The new effect ordinal is frozen"),
		static_cast<uint8>(EBattleMoveEffectKind::RegisterAllyActionPowerModifier),
		static_cast<uint8>(17));
	bValid &= TestEqual(TEXT("The new event ordinal is frozen"),
		static_cast<uint8>(EBattleEventType::ActionPowerModifierRegistered),
		static_cast<uint8>(54));

	const FBattleMoveDefinition ExactMove = MakeR3RegistrationMove();
	bValid &= TestTrue(TEXT("The exact descriptor is accepted"),
		FBattleAllyActionPowerModifier::IsRegistrationMoveDefinitionValid(
			ExactMove));
	FBattleMoveDefinition AuthoredMove = ExactMove;
	AuthoredMove.BasePP = 20;
	AuthoredMove.Priority = -3;
	AuthoredMove.Flags = EBattleMoveFlags::Unencoreable;
	bValid &= TestTrue(TEXT("Authored PP, priority, and move flags remain legal"),
		FBattleAllyActionPowerModifier::IsRegistrationMoveDefinitionValid(
			AuthoredMove));

	FBattleDefinitionCatalogInput CatalogInput;
	CatalogInput.TypeChartEntries = MakeR3NeutralTypeChart();
	CatalogInput.Moves.Add(AuthoredMove);
	FBattleDefinitionCatalog Catalog;
	TArray<FBattleCatalogDiagnostic> Diagnostics;
	bValid &= TestTrue(TEXT("The catalog accepts the exact reusable descriptor"),
		FBattleDefinitionCatalog::TryCreate(
			CatalogInput, Catalog, Diagnostics));
	bValid &= TestTrue(TEXT("The exact descriptor has no diagnostics"),
		Diagnostics.IsEmpty());

	const FBattleDataTableSet Tables = MakeR3RegistrationTables();
	FBattleDefinitionCatalog AdaptedCatalog;
	Diagnostics.Reset();
	bValid &= TestTrue(TEXT("The Data Table adapter maps the new authored kind"),
		FBattleDataTableAdapter::BuildCatalog(
			Tables, AdaptedCatalog, Diagnostics));
	const FBattleMoveDefinition* AdaptedMove = AdaptedCatalog.FindMove(
		GetR3RegistrationMoveId());
	bValid &= TestTrue(TEXT("The adapter preserves target and exact rational"),
		AdaptedMove != nullptr
			&& AdaptedMove->Effects.Num() == 1
			&& AdaptedMove->Effects[0].Kind
				== EBattleMoveEffectKind::RegisterAllyActionPowerModifier
			&& AdaptedMove->Effects[0].Target
				== EBattleEffectTarget::ResolvedTarget
			&& AdaptedMove->Effects[0].MagnitudeNumerator == 3
			&& AdaptedMove->Effects[0].MagnitudeDenominator == 2);

	struct FR3DescriptorDeviation
	{
		const TCHAR* Name;
		void (*Apply)(FBattleMoveDefinition&);
	};
	const FR3DescriptorDeviation Deviations[] =
	{
		{TEXT("category"), [](FBattleMoveDefinition& Move) {
			Move.Category = EBattleMoveCategory::Physical; }},
		{TEXT("power"), [](FBattleMoveDefinition& Move) { Move.Power = 1; }},
		{TEXT("always-hit"), [](FBattleMoveDefinition& Move) {
			Move.bAlwaysHits = false; }},
		{TEXT("accuracy"), [](FBattleMoveDefinition& Move) { Move.Accuracy = 1; }},
		{TEXT("move target"), [](FBattleMoveDefinition& Move) {
			Move.TargetClass = EBattleTargetClass::SelectedOpponent; }},
		{TEXT("effect count"), [](FBattleMoveDefinition& Move) {
			const FBattleMoveEffectDescriptor Duplicate = Move.Effects[0];
			Move.Effects.Add(Duplicate); }},
		{TEXT("effect order"), [](FBattleMoveDefinition& Move) {
			Move.Effects[0].Order = 1; }},
		{TEXT("effect kind"), [](FBattleMoveDefinition& Move) {
			Move.Effects[0].Kind = EBattleMoveEffectKind::Damage; }},
		{TEXT("effect target"), [](FBattleMoveDefinition& Move) {
			Move.Effects[0].Target = EBattleEffectTarget::User; }},
		{TEXT("chance numerator"), [](FBattleMoveDefinition& Move) {
			Move.Effects[0].ChanceNumerator = 0; }},
		{TEXT("chance denominator"), [](FBattleMoveDefinition& Move) {
			Move.Effects[0].ChanceDenominator = 2; }},
		{TEXT("condition payload"), [](FBattleMoveDefinition& Move) {
			Move.Effects[0].ConditionId = MakeDefinitionId<FConditionId>(
				TEXT("Condition.C10R3.Unused")); }},
		{TEXT("item payload"), [](FBattleMoveDefinition& Move) {
			Move.Effects[0].ItemId = MakeDefinitionId<FItemId>(
				TEXT("Item.C10R3.Unused")); }},
		{TEXT("held-item operation payload"), [](FBattleMoveDefinition& Move) {
			Move.Effects[0].HeldItemOperation =
				EBattleMoveHeldItemOperation::RemoveCurrent; }},
		{TEXT("stat payload"), [](FBattleMoveDefinition& Move) {
			Move.Effects[0].Stat = EBattleStat::Speed; }},
		{TEXT("zero numerator"), [](FBattleMoveDefinition& Move) {
			Move.Effects[0].MagnitudeNumerator = 0; }},
		{TEXT("negative numerator"), [](FBattleMoveDefinition& Move) {
			Move.Effects[0].MagnitudeNumerator = -1; }},
		{TEXT("zero denominator"), [](FBattleMoveDefinition& Move) {
			Move.Effects[0].MagnitudeDenominator = 0; }},
		{TEXT("negative denominator"), [](FBattleMoveDefinition& Move) {
			Move.Effects[0].MagnitudeDenominator = -2; }},
		{TEXT("inexact Q12 rational"), [](FBattleMoveDefinition& Move) {
			Move.Effects[0].MagnitudeNumerator = 1;
			Move.Effects[0].MagnitudeDenominator = 3; }},
		{TEXT("Q12 overflow"), [](FBattleMoveDefinition& Move) {
			Move.Effects[0].MagnitudeNumerator = 33;
			Move.Effects[0].MagnitudeDenominator = 1; }},
		{TEXT("minimum count"), [](FBattleMoveDefinition& Move) {
			Move.Effects[0].MinimumCount = 1; }},
		{TEXT("maximum count"), [](FBattleMoveDefinition& Move) {
			Move.Effects[0].MaximumCount = 1; }},
		{TEXT("duration"), [](FBattleMoveDefinition& Move) {
			Move.Effects[0].DurationTurns = 1; }},
		{TEXT("layers"), [](FBattleMoveDefinition& Move) {
			Move.Effects[0].LayerCount = 1; }},
		{TEXT("effect flags"), [](FBattleMoveDefinition& Move) {
			Move.Effects[0].Flags = EBattleMoveEffectFlags::MinimumOne; }}
	};

	const FBattleBattlerTarget Source = MakeR3Target(
		EBattleSide::Player, EBattlePosition::Left, R3PlayerLeftValue);
	const FBattleBattlerTarget Ally = MakeR3Target(
		EBattleSide::Player, EBattlePosition::Right, R3PlayerRightValue);
	FBattleResolvedTarget ResolvedAlly;
	check(FBattleResolvedTarget::TryCreateBattler(Ally, ResolvedAlly));
	FBattleEffectExecutionRequest Request;
	Request.BattleId = MakeNumericId<FBattleId>(1);
	Request.TurnId = MakeNumericId<FTurnId>(1);
	Request.ActionId = MakeNumericId<FActionId>(10);
	Request.ResolutionId = MakeNumericId<FResolutionId>(1);
	Request.UserBattlerId = Source.BattlerId;
	Request.UserSlotId = Source.ActiveSlotId;
	Request.Targets.Add(ResolvedAlly);
	for (const FR3DescriptorDeviation& Deviation : Deviations)
	{
		FBattleMoveDefinition Malformed = ExactMove;
		Deviation.Apply(Malformed);
		bValid &= TestFalse(
			FString::Printf(TEXT("Rule rejects %s"), Deviation.Name),
			FBattleAllyActionPowerModifier::IsRegistrationMoveDefinitionValid(
				Malformed));
		FBattleDefinitionCatalogInput MalformedInput = CatalogInput;
		MalformedInput.Moves[0] = Malformed;
		FBattleDefinitionCatalog RejectedCatalog;
		Diagnostics.Reset();
		bValid &= TestFalse(
			FString::Printf(TEXT("Catalog rejects %s"), Deviation.Name),
			FBattleDefinitionCatalog::TryCreate(
				MalformedInput, RejectedCatalog, Diagnostics));
		bValid &= TestTrue(
			FString::Printf(TEXT("Catalog diagnoses %s"), Deviation.Name),
			!Diagnostics.IsEmpty());

		Request.Move = &Malformed;
		FR3ValidationOnlyExecutionContext Context;
		FStrictBattleRandom NoRandom({});
		FBattleEffectExecutionResult Result;
		EBattleEffectExecutorError Error = EBattleEffectExecutorError::None;
		bValid &= TestFalse(
			FString::Printf(TEXT("Runtime rejects %s"), Deviation.Name),
			FBattleEffectExecutor::TryExecute(
				Request, Context, NoRandom, Result, Error));
		bValid &= TestTrue(
			FString::Printf(TEXT("Runtime rejection for %s is pre-mutation"),
				Deviation.Name),
			Error == EBattleEffectExecutorError::InvalidMoveDefinition
				&& !Context.bPrevalidateCalled
				&& NoRandom.IsExact()
				&& Result.Events.IsEmpty());
	}
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC10R3PendingFightAndNewEntryTest,
	"PokemonSolarus.Battle.C05B.C10ActionModifiers.Registration.PendingFightAndNewlySwitchedEligibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBattleC10R3PendingFightAndNewEntryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	bool bValid = true;
	const FTurnId TurnId = MakeNumericId<FTurnId>(7);
	for (const EBattleFormat Format :
		{EBattleFormat::Double, EBattleFormat::PartnerDouble})
	{
		const FR3Board Board = MakeR3Board();
		const TArray<FBattleLockedActionState> Actions =
			MakeR3SourceAndPendingTargetActions();
		TArray<FBattleAllyActionPowerModifierRegistration> Registrations;
		int32 Before = INDEX_NONE;
		int32 After = INDEX_NONE;
		bValid &= TestTrue(TEXT("A pending ally Fight binds in each Doubles format"),
			RegisterR3(Format, TurnId, Board, Actions, Registrations,
				Before, After)
				== EBattleAllyActionPowerModifierRegistrationOutcome::Registered);
		bValid &= TestTrue(TEXT("The first binding reports 0 to 1 and exact action"),
			Registrations.Num() == 1
				&& Before == 0
				&& After == 1
				&& Registrations[0].TargetActionId
					== MakeNumericId<FActionId>(20));
		bValid &= TestTrue(TEXT("A repeated registration appends stably"),
			RegisterR3(Format, TurnId, Board, Actions, Registrations,
				Before, After)
				== EBattleAllyActionPowerModifierRegistrationOutcome::Registered
				&& Registrations.Num() == 2
				&& Before == 1
				&& After == 2
				&& Registrations[0].SourceActionId
					== Registrations[1].SourceActionId);
	}

	FR3Board NewEntryBoard = MakeR3Board();
	FBattleBattlerState* NewEntry = NewEntryBoard.Battlers.FindByPredicate(
		[](const FBattleBattlerState& Battler)
		{
			return Battler.BattlerId
				== MakeNumericId<FBattlerId>(R3PlayerRightValue);
		});
	check(NewEntry != nullptr);
	NewEntry->EnteredActiveOnTurnId = TurnId;
	TArray<FBattleLockedActionState> SourceOnly =
		MakeR3SourceAndPendingTargetActions();
	SourceOnly.RemoveAt(1);
	TArray<FBattleAllyActionPowerModifierRegistration> NewEntryRegistrations;
	int32 Before = INDEX_NONE;
	int32 After = INDEX_NONE;
	TArray<FBattleAllyActionPowerModifierRegistration> OrdinaryRegistrations;
	bValid &= TestTrue(TEXT("An ordinary ally without a pending Fight is ineligible"),
		RegisterR3(EBattleFormat::Double, TurnId, MakeR3Board(), SourceOnly,
			OrdinaryRegistrations, Before, After)
			== EBattleAllyActionPowerModifierRegistrationOutcome::IneligibleTarget
			&& OrdinaryRegistrations.IsEmpty());
	bValid &= TestTrue(TEXT("A newly entered ally without a pending Fight can bind"),
		RegisterR3(EBattleFormat::Double, TurnId, NewEntryBoard, SourceOnly,
			NewEntryRegistrations, Before, After)
			== EBattleAllyActionPowerModifierRegistrationOutcome::Registered);
	bValid &= TestTrue(TEXT("The new-entry exception stores only an invalid action ID"),
		NewEntryRegistrations.Num() == 1
			&& !NewEntryRegistrations[0].TargetActionId.IsValid()
			&& Before == 0
			&& After == 1);

	TArray<FBattleAllyActionPowerModifierRegistration> Singles;
	bValid &= TestTrue(TEXT("Singles is a typed no-mutation ineligible format"),
		RegisterR3(EBattleFormat::Single, TurnId, MakeR3Board(),
			MakeR3SourceAndPendingTargetActions(), Singles, Before, After)
			== EBattleAllyActionPowerModifierRegistrationOutcome::IneligibleFormat
			&& Singles.IsEmpty());
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC10R3AlreadyActedNoMutationTest,
	"PokemonSolarus.Battle.C05B.C10ActionModifiers.Registration.AlreadyActedInvalidTargetNoMutationOrRng",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBattleC10R3AlreadyActedNoMutationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FTurnId TurnId = MakeNumericId<FTurnId>(8);
	FR3Board Board = MakeR3Board();
	FBattleBattlerState* TargetBattler = Board.Battlers.FindByPredicate(
		[](const FBattleBattlerState& Battler)
		{
			return Battler.BattlerId
				== MakeNumericId<FBattlerId>(R3PlayerRightValue);
		});
	check(TargetBattler != nullptr);
	TargetBattler->EnteredActiveOnTurnId = TurnId;
	TArray<FBattleLockedActionState> Actions =
		MakeR3SourceAndPendingTargetActions();
	Actions[1].bStarted = true;
	TArray<FBattleAllyActionPowerModifierRegistration> Registrations;
	FBattleAllyActionPowerModifierRegistration Sentinel;
	Sentinel.TurnId = TurnId;
	Sentinel.SourceActionId = MakeNumericId<FActionId>(99);
	Sentinel.SourceMoveId = MakeDefinitionId<FMoveId>(TEXT("Move.C10R3.Sentinel"));
	Sentinel.TargetActionId = MakeNumericId<FActionId>(20);
	Sentinel.Target = MakeR3Target(EBattleSide::Player,
		EBattlePosition::Right, R3PlayerRightValue);
	Sentinel.MagnitudeNumerator = 3;
	Sentinel.MagnitudeDenominator = 2;
	Registrations.Add(Sentinel);
	const TArray<FBattleAllyActionPowerModifierRegistration> BeforeState =
		Registrations;
	int32 Before = INDEX_NONE;
	int32 After = INDEX_NONE;
	const EBattleAllyActionPowerModifierRegistrationOutcome Outcome =
		RegisterR3(EBattleFormat::Double, TurnId, Board, Actions,
			Registrations, Before, After);
	bool bValid = TestTrue(TEXT("An already-started target is ineligible"),
		Outcome == EBattleAllyActionPowerModifierRegistrationOutcome::IneligibleTarget);
	bValid &= TestTrue(TEXT("Already-acted rejection changes no ordered fact"),
		FBattleAllyActionPowerModifier::AreRegistrationsIdentical(
			Registrations, BeforeState));
	const FBattleBattlerTarget Source = MakeR3Target(
		EBattleSide::Player, EBattlePosition::Left, R3PlayerLeftValue);
	auto RejectTarget =
		[this, TurnId, &Actions, &Source](
			const TCHAR* Label,
			const FR3Board& CaseBoard,
			const FBattleBattlerTarget& Target,
			const EBattleAllyActionPowerModifierRegistrationOutcome Expected)
		{
			TArray<FBattleAllyActionPowerModifierRegistration> Empty;
			int32 CaseBefore = INDEX_NONE;
			int32 CaseAfter = INDEX_NONE;
			const auto CaseOutcome = FBattleAllyActionPowerModifier::TryRegister(
				EBattleFormat::Double, TurnId,
				MakeNumericId<FActionId>(10), GetR3RegistrationMoveId(),
				Source, Target, 3, 2, CaseBoard.Battlers,
				CaseBoard.ActivePositions, Actions, Empty,
				CaseBefore, CaseAfter);
			return TestTrue(Label, CaseOutcome == Expected && Empty.IsEmpty());
		};
	bValid &= RejectTarget(TEXT("Self-target registration is rejected"),
		Board, Source,
		EBattleAllyActionPowerModifierRegistrationOutcome::IneligibleTarget);
	bValid &= RejectTarget(TEXT("Opposite-side registration is rejected"),
		Board, MakeR3Target(EBattleSide::Opponent,
			EBattlePosition::Left, R3OpponentLeftValue),
		EBattleAllyActionPowerModifierRegistrationOutcome::IneligibleTarget);
	bValid &= RejectTarget(TEXT("Stale target occupant identity is rejected"),
		Board, MakeR3Target(EBattleSide::Player,
			EBattlePosition::Right, 999),
		EBattleAllyActionPowerModifierRegistrationOutcome::IneligibleTarget);
	FR3Board DeadTargetBoard = Board;
	FBattleBattlerState* DeadTarget = DeadTargetBoard.Battlers.FindByPredicate(
		[](const FBattleBattlerState& Battler) {
			return Battler.BattlerId
				== MakeNumericId<FBattlerId>(R3PlayerRightValue); });
	check(DeadTarget != nullptr);
	DeadTarget->CurrentHP = 0;
	DeadTarget->bFainted = true;
	bValid &= RejectTarget(TEXT("Fainted target registration is rejected"),
		DeadTargetBoard, MakeR3Target(EBattleSide::Player,
			EBattlePosition::Right, R3PlayerRightValue),
		EBattleAllyActionPowerModifierRegistrationOutcome::IneligibleTarget);
	TArray<FBattleAllyActionPowerModifierRegistration> Forged;
	FBattleAllyActionPowerModifierRegistration InvalidBinding = Sentinel;
	InvalidBinding.TargetActionId = FActionId();
	Forged.Add(InvalidBinding);
	bValid &= TestTrue(
		TEXT("A stored new-entry binding stays valid until occupant or turn cleanup"),
		FBattleAllyActionPowerModifier::IsRegistrationCollectionValid(
			EBattleFormat::Double, TurnId, Forged, Board.Battlers,
			Board.ActivePositions, Actions));
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC10R3RationalPriorityStackingTest,
	"PokemonSolarus.Battle.C05B.C10ActionModifiers.Damage.RationalCheckpointPriorityAndMultiplicativeStacking",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBattleC10R3RationalPriorityStackingTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	int32 ModifierQ12 = 0;
	bool bValid = TestTrue(TEXT("The exact 3/2 rational freezes as Q12 6144"),
		FBattleAllyActionPowerModifier::TryConvertMagnitudeToQ12(
			3, 2, ModifierQ12)
			&& ModifierQ12 == 6144);
	bValid &= TestFalse(TEXT("A nonpositive numerator is rejected"),
		FBattleAllyActionPowerModifier::TryConvertMagnitudeToQ12(
			0, 2, ModifierQ12));
	bValid &= TestFalse(TEXT("A zero denominator is rejected"),
		FBattleAllyActionPowerModifier::TryConvertMagnitudeToQ12(
			3, 0, ModifierQ12));
	bValid &= TestFalse(TEXT("An inexact Q12 rational is rejected"),
		FBattleAllyActionPowerModifier::TryConvertMagnitudeToQ12(
			1, 3, ModifierQ12));
	bValid &= TestFalse(TEXT("A rational above the final-modifier ceiling is rejected"),
		FBattleAllyActionPowerModifier::TryConvertMagnitudeToQ12(
			33, 1, ModifierQ12));

	const FTurnId TurnId = MakeNumericId<FTurnId>(9);
	const FActionId ActionId = MakeNumericId<FActionId>(20);
	const FBattleBattlerTarget DamageUser = MakeR3Target(
		EBattleSide::Player, EBattlePosition::Right, R3PlayerRightValue);
	TArray<FBattleAllyActionPowerModifierRegistration> Registrations;
	for (const TCHAR* MoveName :
		{TEXT("Move.C10R3.First"), TEXT("Move.C10R3.Second")})
	{
		FBattleAllyActionPowerModifierRegistration& Registration =
			Registrations.AddDefaulted_GetRef();
		Registration.TurnId = TurnId;
		Registration.SourceActionId = MakeNumericId<FActionId>(10);
		Registration.SourceMoveId = MakeDefinitionId<FMoveId>(MoveName);
		Registration.TargetActionId = ActionId;
		Registration.Target = DamageUser;
		Registration.MagnitudeNumerator = 3;
		Registration.MagnitudeDenominator = 2;
	}

	TArray<FBattleDamageModifier> Modifiers;
	bValid &= TestTrue(TEXT("Non-actual damage probes do not convert or append"),
		FBattleAllyActionPowerModifier::AppendMatchingPowerModifiers(
			TurnId, ActionId, DamageUser, false, Registrations, Modifiers)
			&& Modifiers.IsEmpty());
	bValid &= TestTrue(TEXT("The actual checkpoint appends both modifiers stably"),
		FBattleAllyActionPowerModifier::AppendMatchingPowerModifiers(
			TurnId, ActionId, DamageUser, true, Registrations, Modifiers)
			&& Modifiers.Num() == 2
			&& Modifiers[0].RuleId
				== Registrations[0].SourceMoveId.GetDefinitionId()
			&& Modifiers[1].RuleId
				== Registrations[1].SourceMoveId.GetDefinitionId()
			&& Modifiers[0].ModifierQ12 == 6144
			&& Modifiers[1].ModifierQ12 == 6144);
	FBattleDamageModifier Terrain;
	Terrain.RuleId = MakeDefinitionId<FDefinitionId>(
		TEXT("Condition.C10R3.Terrain"));
	Terrain.ModifierQ12 = 5325;
	Modifiers.Add(Terrain);

	FBattleFinalDamageInput Input = MakeR3DamageInput();
	Input.PowerModifiers = Modifiers;
	FSequenceBattleRandom Random({15});
	FBattleFinalDamageResult Damage;
	EBattleDamageCalculationError Error = EBattleDamageCalculationError::None;
	bValid &= TestTrue(TEXT("The ordered power chain resolves"),
		FBattleFinalDamageCalculator::TryCalculateFinalDamage(
			Input, Random, Damage, Error));
	const TArray<FBattleDamageTraceEntry> PowerChain =
		Damage.Trace.Entries.FilterByPredicate(
			[](const FBattleDamageTraceEntry& Entry)
			{
				return Entry.Step
					== EBattleDamageTraceStep::PowerModifierChain;
			});
	bValid &= TestTrue(
		TEXT("The two 3/2 modifiers multiply to 9/4 before terrain"),
		PowerChain.Num() == 3
			&& PowerChain[0].RuleId == Modifiers[0].RuleId
			&& PowerChain[0].Value == 6144
			&& PowerChain[1].RuleId == Modifiers[1].RuleId
			&& PowerChain[1].Value == 9216
			&& PowerChain[2].RuleId == Terrain.RuleId);

	TArray<FBattleDamageModifier> Mismatch;
	bValid &= TestTrue(TEXT("A different action receives no modifier"),
		FBattleAllyActionPowerModifier::AppendMatchingPowerModifiers(
			TurnId, MakeNumericId<FActionId>(21), DamageUser, true,
			Registrations, Mismatch)
			&& Mismatch.IsEmpty());
	bValid &= TestTrue(TEXT("A different turn receives no modifier"),
		FBattleAllyActionPowerModifier::AppendMatchingPowerModifiers(
			MakeNumericId<FTurnId>(10), ActionId, DamageUser, true,
			Registrations, Mismatch)
			&& Mismatch.IsEmpty());
	const FBattleBattlerTarget WrongSlot = {
		MakeActiveSlotId(EBattleSide::Player, EBattlePosition::Left),
		DamageUser.BattlerId};
	bValid &= TestTrue(TEXT("A stale slot identity receives no modifier"),
		FBattleAllyActionPowerModifier::AppendMatchingPowerModifiers(
			TurnId, ActionId, WrongSlot, true, Registrations, Mismatch)
			&& Mismatch.IsEmpty());
	const FBattleBattlerTarget WrongBattler = {
		DamageUser.ActiveSlotId,
		MakeNumericId<FBattlerId>(R3PlayerLeftValue)};
	bValid &= TestTrue(TEXT("A stale battler identity receives no modifier"),
		FBattleAllyActionPowerModifier::AppendMatchingPowerModifiers(
			TurnId, ActionId, WrongBattler, true, Registrations, Mismatch)
			&& Mismatch.IsEmpty());
	return bValid;
}

#endif
