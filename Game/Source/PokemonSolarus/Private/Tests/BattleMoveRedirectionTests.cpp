#if WITH_DEV_AUTOMATION_TESTS

#include "Battle/BattleDataTableAdapter.h"
#include "Battle/BattleDataTableRows.h"
#include "Battle/BattleEffectExecutor.h"
#include "Battle/BattleEvent.h"
#include "Battle/BattleStatCalculator.h"
#include "Battle/BattleTargeting.h"
#include "Battle/BattleMoveRedirection.h"
#include "BattleTestFactories.h"
#include "BattleTestRandom.h"
#include "Engine/DataTable.h"
#include "Misc/AutomationTest.h"
#include "UObject/UObjectGlobals.h"

namespace BattleMoveRedirectionTests
{
	using BattleTest::FStrictBattleRandom;
	using BattleTest::MakeActiveSlotId;
	using BattleTest::MakeDefinitionId;
	using BattleTest::MakeNumericId;

	constexpr uint64 PlayerTrainerValue = 1;
	constexpr uint64 OpponentTrainerValue = 2;
	constexpr uint64 PlayerLeftBattlerValue = 11;
	constexpr uint64 PlayerRightBattlerValue = 12;
	constexpr uint64 OpponentLeftBattlerValue = 21;
	constexpr uint64 OpponentRightBattlerValue = 22;

	const TCHAR* RegistrationMoveName = TEXT("Move.C10R2.RegisterTargetRedirection");

	FName PokemonTypeName(const int32 TypeIndex)
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

	TArray<FBattleTypeChartEntry> MakeNeutralTypeChart()
	{
		TArray<FBattleTypeChartEntry> Entries;
		Entries.Reserve(FBattleTypeChart::EntryCount);
		for (int32 Attacking = 0; Attacking < FBattleTypeChart::TypeCount; ++Attacking)
		{
			for (int32 Defending = 0; Defending < FBattleTypeChart::TypeCount; ++Defending)
			{
				Entries.Add(
					{
						static_cast<EPokemonType>(Attacking),
						static_cast<EPokemonType>(Defending),
						1,
						1
					});
			}
		}
		return Entries;
	}

	FBattleMoveDefinition MakeRegistrationMove()
	{
		FBattleMoveDefinition Move;
		Move.Id = MakeDefinitionId<FMoveId>(RegistrationMoveName);
		Move.Type = EPokemonType::Normal;
		Move.Category = EBattleMoveCategory::Status;
		Move.Power = 0;
		Move.bAlwaysHits = true;
		Move.Accuracy = 0;
		Move.bUsesPP = true;
		Move.BasePP = 5;
		Move.bAllowsPPBoosts = true;
		Move.Priority = 2;
		Move.TargetClass = EBattleTargetClass::Self;

		FBattleMoveEffectDescriptor Effect;
		Effect.Order = 0;
		Effect.Kind = EBattleMoveEffectKind::RegisterTargetRedirection;
		Effect.Target = EBattleEffectTarget::User;
		Move.Effects.Add(Effect);
		return Move;
	}

	template <typename RowType>
	UDataTable* MakeTransientTable()
	{
		UDataTable* Table = NewObject<UDataTable>(GetTransientPackage());
		check(Table != nullptr);
		Table->RowStruct = RowType::StaticStruct();
		return Table;
	}

	void AddNeutralTypeChartRows(UDataTable& TypeChart)
	{
		for (int32 Attacking = 0; Attacking < FBattleTypeChart::TypeCount; ++Attacking)
		{
			FBattleTypeChartTableRow Row;
			for (int32 Defending = 0; Defending < FBattleTypeChart::TypeCount; ++Defending)
			{
				Row.Entries.Add({PokemonTypeName(Defending), 1, 1});
			}
			TypeChart.AddRow(PokemonTypeName(Attacking), Row);
		}
	}

	FBattleDataTableSet MakeRegistrationTables()
	{
		FBattleDataTableSet Tables;
		Tables.SpeciesForms = MakeTransientTable<FBattleSpeciesFormTableRow>();
		Tables.Natures = MakeTransientTable<FBattleNatureTableRow>();
		UDataTable* Moves = MakeTransientTable<FBattleMoveTableRow>();
		Tables.Moves = Moves;
		Tables.Abilities = MakeTransientTable<FBattleAbilityTableRow>();
		Tables.Items = MakeTransientTable<FBattleItemTableRow>();
		Tables.Conditions = MakeTransientTable<FBattleConditionTableRow>();
		UDataTable* TypeChart = MakeTransientTable<FBattleTypeChartTableRow>();
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
		Move.TargetClass = FName(TEXT("Self"));
		FBattleMoveEffectTableRow Effect;
		Effect.Order = 0;
		Effect.Kind = FName(TEXT("RegisterTargetRedirection"));
		Effect.Target = FName(TEXT("User"));
		Move.Effects.Add(Effect);
		Moves->AddRow(FName(RegistrationMoveName), Move);
		AddNeutralTypeChartRows(*TypeChart);
		return Tables;
	}

	bool ContainsIncompatibleEffectDiagnostic(
		const TConstArrayView<FBattleCatalogDiagnostic> Diagnostics,
		const FName ExpectedField)
	{
		return Diagnostics.ContainsByPredicate(
			[ExpectedField](const FBattleCatalogDiagnostic& Diagnostic)
			{
				return Diagnostic.Code == EBattleCatalogDiagnosticCode::IncompatibleEffect
					&& Diagnostic.Field == ExpectedField;
			});
	}

	FBattleEffectHookResult MakeAppliedHookResult()
	{
		FBattleEffectHookResult Result;
		Result.Outcome = EBattleEffectExecutionOutcome::Applied;
		Result.RuleId = MakeDefinitionId<FDefinitionId>(TEXT("Rule.C10R2.ValidationOnly"));
		return Result;
	}

	class FValidationOnlyExecutionContext final : public IBattleEffectExecutionContext
	{
	public:
		bool bPrevalidateCalled = false;

		virtual bool PrevalidateRequest(const FBattleEffectExecutionRequest& Request) const override
		{
			(void)Request;
			const_cast<FValidationOnlyExecutionContext*>(this)->bPrevalidateCalled = true;
			return true;
		}

		virtual FBattleEffectHookResult CheckReachability(
			const FBattleMoveDefinition& Move,
			const FBattleResolvedTarget& Target) override
		{
			(void)Move;
			(void)Target;
			return MakeAppliedHookResult();
		}

		virtual FBattleEffectHookResult CheckProtection(
			const FBattleMoveDefinition& Move,
			const FBattleResolvedTarget& Target) override
		{
			(void)Move;
			(void)Target;
			return MakeAppliedHookResult();
		}

		virtual FBattleEffectHookResult CheckTryHit(
			const FBattleMoveDefinition& Move,
			const FBattleResolvedTarget& Target) override
		{
			(void)Move;
			(void)Target;
			return MakeAppliedHookResult();
		}

		virtual FBattleEffectHookResult CheckMoveImmunity(
			const FBattleMoveDefinition& Move,
			const FBattleResolvedTarget& Target) override
		{
			(void)Move;
			(void)Target;
			return MakeAppliedHookResult();
		}

		virtual FBattleEffectHookResult CheckAbilityImmunity(
			const FBattleMoveDefinition& Move,
			const FBattleResolvedTarget& Target) override
		{
			(void)Move;
			(void)Target;
			return MakeAppliedHookResult();
		}

		virtual FBattleEffectHookResult CheckItemImmunity(
			const FBattleMoveDefinition& Move,
			const FBattleResolvedTarget& Target) override
		{
			(void)Move;
			(void)Target;
			return MakeAppliedHookResult();
		}

		virtual FBattleEffectHookResult ApplyProtectionBreaking(
			const FBattleMoveDefinition& Move,
			const FBattleResolvedTarget& Target) override
		{
			(void)Move;
			(void)Target;
			return MakeAppliedHookResult();
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

		virtual bool IsSourceAbleToContinue() const override
		{
			return true;
		}

		virtual bool IsTargetAbleToContinue(const FBattleResolvedTarget& Target) const override
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
			return MakeAppliedHookResult();
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
			return MakeAppliedHookResult();
		}

		virtual FBattleEffectHookResult ApplyNonHpEffect(
			const FBattleMoveEffectDescriptor& Effect,
			const FBattleResolvedTarget& Target) override
		{
			(void)Effect;
			(void)Target;
			return MakeAppliedHookResult();
		}

		virtual void RunImmediateUpdate(const FBattleResolvedTarget& Target) override
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

	FBattleBattlerTarget MakeTarget(
		const EBattleSide Side,
		const EBattlePosition Position,
		const uint64 BattlerValue)
	{
		FBattleBattlerTarget Target;
		Target.ActiveSlotId = MakeActiveSlotId(Side, Position);
		Target.BattlerId = MakeNumericId<FBattlerId>(BattlerValue);
		return Target;
	}

	FBattleBattlerState MakeBattler(
		const uint64 TrainerValue,
		const uint64 BattlerValue,
		const int32 Speed)
	{
		FBattleBattlerState Battler;
		Battler.TrainerId = MakeNumericId<FTrainerId>(TrainerValue);
		Battler.BattlerId = MakeNumericId<FBattlerId>(BattlerValue);
		Battler.PermanentStats = {100, 100, 100, 100, 100, Speed};
		Battler.CurrentHP = 100;
		return Battler;
	}

	FBattleActivePositionState MakePosition(
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

	struct FDoubleBoard
	{
		TArray<FBattleBattlerState> Battlers;
		TArray<FBattleActivePositionState> ActivePositions;
	};

	FDoubleBoard MakeDoubleBoard(
		const int32 PlayerLeftSpeed = 100,
		const int32 PlayerRightSpeed = 100,
		const int32 OpponentLeftSpeed = 100,
		const int32 OpponentRightSpeed = 100)
	{
		FDoubleBoard Board;
		Board.Battlers =
		{
			MakeBattler(PlayerTrainerValue, PlayerLeftBattlerValue, PlayerLeftSpeed),
			MakeBattler(PlayerTrainerValue, PlayerRightBattlerValue, PlayerRightSpeed),
			MakeBattler(OpponentTrainerValue, OpponentLeftBattlerValue, OpponentLeftSpeed),
			MakeBattler(OpponentTrainerValue, OpponentRightBattlerValue, OpponentRightSpeed)
		};
		Board.ActivePositions =
		{
			MakePosition(
				EBattleSide::Player,
				EBattlePosition::Left,
				PlayerTrainerValue,
				PlayerLeftBattlerValue),
			MakePosition(
				EBattleSide::Player,
				EBattlePosition::Right,
				PlayerTrainerValue,
				PlayerRightBattlerValue),
			MakePosition(
				EBattleSide::Opponent,
				EBattlePosition::Left,
				OpponentTrainerValue,
				OpponentLeftBattlerValue),
			MakePosition(
				EBattleSide::Opponent,
				EBattlePosition::Right,
				OpponentTrainerValue,
				OpponentRightBattlerValue)
		};
		return Board;
	}

	bool TryResolveCurrentSpeed(
		const TConstArrayView<FBattleBattlerState> Battlers,
		const FBattleBattlerTarget& Target,
		int32& OutSpeed)
	{
		OutSpeed = 0;
		const FBattleBattlerState* Battler = Battlers.FindByPredicate(
			[&Target](const FBattleBattlerState& Candidate)
			{
				return Candidate.BattlerId == Target.BattlerId;
			});
		return Battler != nullptr
			&& FBattleStatCalculator::TryCalculateEffectiveStat(
				Battler->PermanentStats,
				Battler->Stages,
				EBattleStat::Speed,
				OutSpeed);
	}

	TArray<FBattleTargetPositionFacts> MakeTargetingPositions(const FDoubleBoard& Board)
	{
		TArray<FBattleTargetPositionFacts> Positions;
		for (const FBattleActivePositionState& Active : Board.ActivePositions)
		{
			Positions.Add(
				{
					Active.ActiveSlotId,
					Active.BattlerId,
					EBattleTargetPositionState::Living,
					false
				});
		}
		return Positions;
	}

	bool RegisterRedirector(
		const EBattleFormat Format,
		const FTurnId TurnId,
		const uint64 ActionValue,
		const FBattleBattlerTarget& Redirector,
		const FDoubleBoard& Board,
		TArray<FBattleMoveRedirectionRegistration>& InOutRegistrations)
	{
		return FBattleMoveRedirection::TryRegister(
			Format,
			TurnId,
			MakeNumericId<FActionId>(ActionValue),
			Redirector,
			Board.Battlers,
			Board.ActivePositions,
			InOutRegistrations)
			== EBattleMoveRedirectionRegistrationOutcome::Registered;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC10R2RedirectionContractTest,
		"PokemonSolarus.Battle.C04B.C10Redirection.Contract.DescriptorAdapterCatalogAndRuntime",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

	bool FBattleC10R2RedirectionContractTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		TestEqual(
			TEXT("The appended effect kind retains its frozen numeric value"),
			static_cast<uint8>(EBattleMoveEffectKind::RegisterTargetRedirection),
			static_cast<uint8>(16));
		TestEqual(
			TEXT("The appended event type retains its frozen numeric value"),
			static_cast<uint8>(EBattleEventType::TargetRedirectionRegistered),
			static_cast<uint8>(53));

		const FBattleMoveDefinition RegistrationMove = MakeRegistrationMove();
		TestTrue(
			TEXT("The exact typed registration descriptor is accepted by its rule owner"),
			FBattleMoveRedirection::IsRegistrationMoveDefinitionValid(RegistrationMove));

		FBattleDefinitionCatalogInput Input;
		Input.TypeChartEntries = MakeNeutralTypeChart();
		Input.Moves.Add(RegistrationMove);
		FBattleDefinitionCatalog Catalog;
		TArray<FBattleCatalogDiagnostic> Diagnostics;
		TestTrue(
			TEXT("The frozen catalog accepts the exact registration descriptor"),
			FBattleDefinitionCatalog::TryCreate(Input, Catalog, Diagnostics));
		TestTrue(TEXT("The exact descriptor adds no catalog diagnostics"), Diagnostics.IsEmpty());

		const FBattleDataTableSet Tables = MakeRegistrationTables();
		FBattleDefinitionCatalog AdaptedCatalog;
		Diagnostics.Reset();
		TestTrue(
			TEXT("The Data Table adapter recognizes the authored registration kind name"),
			FBattleDataTableAdapter::BuildCatalog(Tables, AdaptedCatalog, Diagnostics));
		const FBattleMoveDefinition* AdaptedMove = AdaptedCatalog.FindMove(
			MakeDefinitionId<FMoveId>(RegistrationMoveName));
		TestTrue(
			TEXT("The adapter freezes the mapped kind as RegisterTargetRedirection"),
			AdaptedMove != nullptr
				&& AdaptedMove->Effects.Num() == 1
				&& AdaptedMove->Effects[0].Kind
					== EBattleMoveEffectKind::RegisterTargetRedirection);

		const FBattleBattlerTarget User = MakeTarget(
			EBattleSide::Player,
			EBattlePosition::Left,
			PlayerLeftBattlerValue);
		FBattleResolvedTarget ResolvedUser;
		check(FBattleResolvedTarget::TryCreateBattler(User, ResolvedUser));
		FBattleEffectExecutionRequest Request;
		Request.BattleId = MakeNumericId<FBattleId>(1001);
		Request.TurnId = MakeNumericId<FTurnId>(1);
		Request.ActionId = MakeNumericId<FActionId>(1);
		Request.ResolutionId = MakeNumericId<FResolutionId>(1);
		Request.UserBattlerId = User.BattlerId;
		Request.UserSlotId = User.ActiveSlotId;
		Request.Targets.Add(ResolvedUser);

		struct FDescriptorDeviation
		{
			const TCHAR* Field;
			void (*Apply)(FBattleMoveDefinition&);
			FName ExpectedDiagnosticField = FName(TEXT("Effects.RegisterTargetRedirection"));
		};
		const FDescriptorDeviation Deviations[] =
		{
			{TEXT("Move.Category"), [](FBattleMoveDefinition& Move) { Move.Category = EBattleMoveCategory::Physical; }},
			{TEXT("Move.Power"), [](FBattleMoveDefinition& Move) { Move.Power = 1; }},
			{TEXT("Move.bAlwaysHits"), [](FBattleMoveDefinition& Move) { Move.bAlwaysHits = false; }},
			{TEXT("Move.Accuracy"), [](FBattleMoveDefinition& Move) { Move.Accuracy = 1; }},
			{TEXT("Move.TargetClass"), [](FBattleMoveDefinition& Move) { Move.TargetClass = EBattleTargetClass::SelectedOpponent; }},
			{TEXT("Move.Effects.Num"), [](FBattleMoveDefinition& Move) {
				const FBattleMoveEffectDescriptor Duplicate = Move.Effects[0];
				Move.Effects.Add(Duplicate);
			}},
			{TEXT("Effect.Order"), [](FBattleMoveDefinition& Move) { Move.Effects[0].Order = 1; }},
			{TEXT("Effect.Kind"), [](FBattleMoveDefinition& Move) { Move.Effects[0].Kind = EBattleMoveEffectKind::Damage; }, FName(TEXT("Effects.Target"))},
			{TEXT("Effect.Target"), [](FBattleMoveDefinition& Move) { Move.Effects[0].Target = EBattleEffectTarget::ResolvedTarget; }},
			{TEXT("Effect.ChanceNumerator"), [](FBattleMoveDefinition& Move) { Move.Effects[0].ChanceNumerator = 0; }},
			{TEXT("Effect.ChanceDenominator"), [](FBattleMoveDefinition& Move) { Move.Effects[0].ChanceDenominator = 2; }},
			{TEXT("Effect.ConditionId"), [](FBattleMoveDefinition& Move) { Move.Effects[0].ConditionId = MakeDefinitionId<FConditionId>(TEXT("Condition.C10R2.Unused")); }},
			{TEXT("Effect.ItemId"), [](FBattleMoveDefinition& Move) { Move.Effects[0].ItemId = MakeDefinitionId<FItemId>(TEXT("Item.C10R2.Unused")); }},
			{TEXT("Effect.Stat"), [](FBattleMoveDefinition& Move) { Move.Effects[0].Stat = EBattleStat::Speed; }},
			{TEXT("Effect.MagnitudeNumerator"), [](FBattleMoveDefinition& Move) { Move.Effects[0].MagnitudeNumerator = 1; }},
			{TEXT("Effect.MagnitudeDenominator"), [](FBattleMoveDefinition& Move) { Move.Effects[0].MagnitudeDenominator = 2; }},
			{TEXT("Effect.MinimumCount"), [](FBattleMoveDefinition& Move) { Move.Effects[0].MinimumCount = 1; }},
			{TEXT("Effect.MaximumCount"), [](FBattleMoveDefinition& Move) { Move.Effects[0].MaximumCount = 1; }},
			{TEXT("Effect.DurationTurns"), [](FBattleMoveDefinition& Move) { Move.Effects[0].DurationTurns = 1; }},
			{TEXT("Effect.LayerCount"), [](FBattleMoveDefinition& Move) { Move.Effects[0].LayerCount = 1; }},
			{TEXT("Effect.Flags"), [](FBattleMoveDefinition& Move) { Move.Effects[0].Flags = EBattleMoveEffectFlags::MinimumOne; }}
		};
		for (const FDescriptorDeviation& Deviation : Deviations)
		{
			FBattleMoveDefinition MalformedMove = RegistrationMove;
			Deviation.Apply(MalformedMove);
			TestFalse(
				FString::Printf(TEXT("%s violates the exact registration descriptor"), Deviation.Field),
				FBattleMoveRedirection::IsRegistrationMoveDefinitionValid(MalformedMove));

			FBattleDefinitionCatalogInput MalformedInput = Input;
			MalformedInput.Moves[0] = MalformedMove;
			FBattleDefinitionCatalog RejectedCatalog;
			Diagnostics.Reset();
			TestFalse(
				FString::Printf(TEXT("The catalog rejects deviation %s"), Deviation.Field),
				FBattleDefinitionCatalog::TryCreate(
					MalformedInput,
					RejectedCatalog,
					Diagnostics));
			TestTrue(
				FString::Printf(TEXT("Catalog rejection identifies deviation %s"), Deviation.Field),
				ContainsIncompatibleEffectDiagnostic(
					Diagnostics,
					Deviation.ExpectedDiagnosticField));

			Request.Move = &MalformedMove;
			FValidationOnlyExecutionContext Context;
			FStrictBattleRandom NoRandom({});
			FBattleEffectExecutionResult Result;
			EBattleEffectExecutorError Error = EBattleEffectExecutorError::None;
			TestFalse(
				FString::Printf(TEXT("The executor rejects deviation %s"), Deviation.Field),
				FBattleEffectExecutor::TryExecute(Request, Context, NoRandom, Result, Error));
			TestEqual(
				FString::Printf(TEXT("Runtime rejection for %s is typed"), Deviation.Field),
				Error,
				EBattleEffectExecutorError::InvalidMoveDefinition);
			TestTrue(
				FString::Printf(TEXT("Runtime rejection for %s is pre-mutation"), Deviation.Field),
				!Context.bPrevalidateCalled && NoRandom.IsExact() && Result.Events.IsEmpty());
		}
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC10R2RedirectionRegistrationFormatsTest,
		"PokemonSolarus.Battle.C04B.C10Redirection.Registration.FormatsAndExactOccupantUpsert",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

	bool FBattleC10R2RedirectionRegistrationFormatsTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		const FDoubleBoard Board = MakeDoubleBoard();
		const FTurnId TurnId = MakeNumericId<FTurnId>(4);
		const FBattleBattlerTarget OpponentLeft = MakeTarget(
			EBattleSide::Opponent,
			EBattlePosition::Left,
			OpponentLeftBattlerValue);
		TArray<FBattleMoveRedirectionRegistration> Registrations;

		TestEqual(
			TEXT("Singles rejects registration as an ineligible format"),
			FBattleMoveRedirection::TryRegister(
				EBattleFormat::Single,
				TurnId,
				MakeNumericId<FActionId>(1),
				OpponentLeft,
				Board.Battlers,
				Board.ActivePositions,
				Registrations),
			EBattleMoveRedirectionRegistrationOutcome::IneligibleFormat);
		TestTrue(TEXT("Singles rejection leaves no registration"), Registrations.IsEmpty());

		for (const EBattleFormat Format : {EBattleFormat::Double, EBattleFormat::PartnerDouble})
		{
			Registrations.Reset();
			TestTrue(
				TEXT("Each supported Doubles format registers a living exact occupant"),
				RegisterRedirector(Format, TurnId, 10, OpponentLeft, Board, Registrations));
			TestEqual(TEXT("Initial registration creates one record"), Registrations.Num(), 1);
			TestTrue(
				TEXT("Initial registration stores the current turn, source action, and exact occupant"),
				Registrations[0].TurnId == TurnId
					&& Registrations[0].SourceActionId == MakeNumericId<FActionId>(10)
					&& Registrations[0].Redirector == OpponentLeft);

			TestTrue(
				TEXT("Re-registering the same exact occupant succeeds"),
				RegisterRedirector(Format, TurnId, 11, OpponentLeft, Board, Registrations));
			TestTrue(
				TEXT("Exact-occupant upsert replaces the source action without duplication"),
				Registrations.Num() == 1
					&& Registrations[0].SourceActionId == MakeNumericId<FActionId>(11)
					&& Registrations[0].Redirector == OpponentLeft);
			TestTrue(
				TEXT("The upserted registration collection remains invariant-valid"),
				FBattleMoveRedirection::IsRegistrationCollectionValid(
					Format,
					TurnId,
					Registrations,
					Board.Battlers,
					Board.ActivePositions));
		}
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC10R2CurrentSpeedSelectionTest,
		"PokemonSolarus.Battle.C04B.C10Redirection.Selection.CurrentEffectiveSpeedWithoutRng",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

	bool FBattleC10R2CurrentSpeedSelectionTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		FDoubleBoard Board = MakeDoubleBoard(100, 100, 200, 100);
		const FTurnId TurnId = MakeNumericId<FTurnId>(8);
		const FBattleBattlerTarget User = MakeTarget(
			EBattleSide::Player,
			EBattlePosition::Left,
			PlayerLeftBattlerValue);
		const FBattleBattlerTarget OpponentLeft = MakeTarget(
			EBattleSide::Opponent,
			EBattlePosition::Left,
			OpponentLeftBattlerValue);
		const FBattleBattlerTarget OpponentRight = MakeTarget(
			EBattleSide::Opponent,
			EBattlePosition::Right,
			OpponentRightBattlerValue);
		TArray<FBattleMoveRedirectionRegistration> Registrations;
		TestTrue(
			TEXT("The first opposing redirector registers"),
			RegisterRedirector(
				EBattleFormat::Double,
				TurnId,
				1,
				OpponentLeft,
				Board,
				Registrations));
		TestTrue(
			TEXT("The second opposing redirector registers"),
			RegisterRedirector(
				EBattleFormat::Double,
				TurnId,
				2,
				OpponentRight,
				Board,
				Registrations));

		FBattleBattlerState* RightState = Board.Battlers.FindByPredicate(
			[](const FBattleBattlerState& Battler)
			{
				return Battler.BattlerId
					== MakeNumericId<FBattlerId>(OpponentRightBattlerValue);
			});
		check(RightState != nullptr);
		const FBattleStatStageChangeResult StageChange = RightState->Stages.ApplyChange(
			EBattleStat::Speed,
			3);
		TestEqual(
			TEXT("The post-registration current Speed stage is applied"),
			StageChange.Outcome,
			EBattleStatStageChangeOutcome::Applied);

		int32 SpeedResolutionCount = 0;
		TArray<FBattleTargetRedirectionProposal> Proposals;
		TestTrue(
			TEXT("Selection resolves both candidates from the current copied battler facts"),
			FBattleMoveRedirection::TrySelectWinningProposal(
				EBattleFormat::Double,
				TurnId,
				EBattleTargetClass::SelectedOpponent,
				User,
				Registrations,
				Board.Battlers,
				Board.ActivePositions,
				[&Board, &SpeedResolutionCount](
					const FBattleBattlerTarget& Target,
					int32& OutSpeed)
				{
					++SpeedResolutionCount;
					return TryResolveCurrentSpeed(Board.Battlers, Target, OutSpeed);
				},
				Proposals));
		TestEqual(TEXT("Each eligible candidate resolves current speed once"), SpeedResolutionCount, 2);
		TestTrue(
			TEXT("The later Speed-stage change makes the originally slower redirector win"),
			Proposals.Num() == 1 && Proposals[0].ProposedTarget == OpponentRight);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC10R2TieAndNoFallbackTest,
		"PokemonSolarus.Battle.C04B.C10Redirection.Selection.LeftTieAndSelectedWinnerNoFallback",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

	bool FBattleC10R2TieAndNoFallbackTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		const FDoubleBoard Board = MakeDoubleBoard();
		const FTurnId TurnId = MakeNumericId<FTurnId>(9);
		const FBattleBattlerTarget User = MakeTarget(
			EBattleSide::Player,
			EBattlePosition::Left,
			PlayerLeftBattlerValue);
		const FBattleBattlerTarget OpponentLeft = MakeTarget(
			EBattleSide::Opponent,
			EBattlePosition::Left,
			OpponentLeftBattlerValue);
		const FBattleBattlerTarget OpponentRight = MakeTarget(
			EBattleSide::Opponent,
			EBattlePosition::Right,
			OpponentRightBattlerValue);
		TArray<FBattleMoveRedirectionRegistration> Registrations;
		TestTrue(
			TEXT("Right registers first to make insertion order non-canonical"),
			RegisterRedirector(
				EBattleFormat::Double,
				TurnId,
				1,
				OpponentRight,
				Board,
				Registrations));
		TestTrue(
			TEXT("Left registers second at equal speed and priority"),
			RegisterRedirector(
				EBattleFormat::Double,
				TurnId,
				2,
				OpponentLeft,
				Board,
				Registrations));

		TArray<FBattleTargetRedirectionProposal> Proposals;
		TestTrue(
			TEXT("The tied candidates produce a canonical proposal"),
			FBattleMoveRedirection::TrySelectWinningProposal(
				EBattleFormat::Double,
				TurnId,
				EBattleTargetClass::SelectedOpponent,
				User,
				Registrations,
				Board.Battlers,
				Board.ActivePositions,
				[&Board](const FBattleBattlerTarget& Target, int32& OutSpeed)
				{
					return TryResolveCurrentSpeed(Board.Battlers, Target, OutSpeed);
				},
				Proposals));
		TestTrue(
			TEXT("Equal candidates choose Left and expose at most one proposal"),
			Proposals.Num() == 1 && Proposals[0].ProposedTarget == OpponentLeft);

		FBattleTargetResolutionSpec Spec;
		Spec.TargetClass = EBattleTargetClass::SelectedOpponent;
		Spec.UserSlotId = User.ActiveSlotId;
		Spec.UserBattlerId = User.BattlerId;
		Spec.Positions = MakeTargetingPositions(Board);
		Spec.ExplicitTarget = OpponentLeft;
		Spec.RedirectionProposals = Proposals;
		FStrictBattleRandom NoRandom({});
		FBattleTargetResolutionResult Result;
		EBattleTargetingError Error = EBattleTargetingError::None;
		TestTrue(
			TEXT("The resolver accepts a winning proposal equal to the selected target"),
			FBattleTargetResolver::TryResolve(Spec, NoRandom, Result, Error));
		TestTrue(
			TEXT("The no-op winner is skipped without falling back to the lower-ranked Right handler"),
			Result.Outcome == EBattleTargetResolutionOutcome::Resolved
				&& Result.Targets.Num() == 1
				&& Result.Targets[0].GetBattler() == OpponentLeft
				&& !Result.bWasRedirected);
		TestTrue(TEXT("The no-fallback path consumes no RNG"), NoRandom.IsExact());
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC10R2FiveTargetClassLegalityTest,
		"PokemonSolarus.Battle.C04B.C10Redirection.Selection.FiveTargetClassLegality",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

	bool FBattleC10R2FiveTargetClassLegalityTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		const FDoubleBoard Board = MakeDoubleBoard(100, 400, 200, 300);
		const FTurnId TurnId = MakeNumericId<FTurnId>(10);
		const FBattleBattlerTarget User = MakeTarget(
			EBattleSide::Player,
			EBattlePosition::Left,
			PlayerLeftBattlerValue);
		const FBattleBattlerTarget PlayerRight = MakeTarget(
			EBattleSide::Player,
			EBattlePosition::Right,
			PlayerRightBattlerValue);
		const FBattleBattlerTarget OpponentLeft = MakeTarget(
			EBattleSide::Opponent,
			EBattlePosition::Left,
			OpponentLeftBattlerValue);
		const FBattleBattlerTarget OpponentRight = MakeTarget(
			EBattleSide::Opponent,
			EBattlePosition::Right,
			OpponentRightBattlerValue);
		TArray<FBattleMoveRedirectionRegistration> Registrations;
		TestTrue(
			TEXT("A same-side handler can register but cannot intercept the user's move"),
			RegisterRedirector(
				EBattleFormat::Double,
				TurnId,
				1,
				PlayerRight,
				Board,
				Registrations));
		TestTrue(
			TEXT("Opponent Left registers"),
			RegisterRedirector(
				EBattleFormat::Double,
				TurnId,
				2,
				OpponentLeft,
				Board,
				Registrations));
		TestTrue(
			TEXT("Opponent Right registers"),
			RegisterRedirector(
				EBattleFormat::Double,
				TurnId,
				3,
				OpponentRight,
				Board,
				Registrations));

		struct FTargetClassCase
		{
			EBattleTargetClass TargetClass;
			bool bExpectsProposal;
		};
		const FTargetClassCase Cases[] =
		{
			{EBattleTargetClass::SelectedAlly, false},
			{EBattleTargetClass::SelectedOpponent, true},
			{EBattleTargetClass::AnySelectedBattler, true},
			{EBattleTargetClass::RandomLegalOpponent, true},
			{EBattleTargetClass::SelectedOtherBattler, true}
		};
		for (const FTargetClassCase& Case : Cases)
		{
			TArray<FBattleTargetRedirectionProposal> Proposals;
			TestTrue(
				TEXT("Each supported target class completes legality filtering"),
				FBattleMoveRedirection::TrySelectWinningProposal(
					EBattleFormat::Double,
					TurnId,
					Case.TargetClass,
					User,
					Registrations,
					Board.Battlers,
					Board.ActivePositions,
					[&Board](const FBattleBattlerTarget& Target, int32& OutSpeed)
					{
						return TryResolveCurrentSpeed(Board.Battlers, Target, OutSpeed);
					},
					Proposals));
			if (Case.bExpectsProposal)
			{
				TestTrue(
					TEXT("Each foe-compatible class selects the fastest opposing handler"),
					Proposals.Num() == 1
						&& Proposals[0].ProposedTarget == OpponentRight);
			}
			else
			{
				TestTrue(
					TEXT("Selected Ally rejects opposing handlers and never uses a same-side handler"),
					Proposals.IsEmpty());
			}
		}
		return true;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
