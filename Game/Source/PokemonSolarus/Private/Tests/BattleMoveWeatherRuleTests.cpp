// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Battle/BattleAllyActionPowerModifier.h"
#include "Battle/BattleDataTableAdapter.h"
#include "Battle/BattleDataTableRows.h"
#include "Battle/BattleEffectExecutorContext.h"
#include "Battle/BattleFinalDamageCalculator.h"
#include "Battle/BattleMoveWeatherRules.h"
#include "Battle/BattleReplay.h"
#include "BattleAtomicCheckpointTestCommon.h"
#include "BattleAtomicMoveCheckpointTestSupport.h"
#include "Engine/DataTable.h"
#include "Math/NumericLimits.h"
#include "Misc/AutomationTest.h"
#include "UObject/UObjectGlobals.h"

namespace BattleMoveWeatherRuleTestsPrivate
{
	// Organization decision: the eight R4B identities and their shared fixtures
	// form one cohesive weather-move behavior family.
	using namespace BattleAtomicCheckpointTestCommonPrivate;
	using namespace BattleAtomicMoveCheckpointTestSupportPrivate;

	const TCHAR* const SolarMoveName = TEXT("Move.C10R4B.SolarShape");
	const TCHAR* const SolarMoveBName = TEXT("Move.C10R4B.SolarShapeB");
	const TCHAR* const ThunderMoveName = TEXT("Move.C10R4B.ThunderShape");
	const TCHAR* const AdapterMoveName = TEXT("Move.C10R4B.AdapterShape");

	FMoveId MoveId(const TCHAR* Name)
	{
		return MakeDefinitionId<FMoveId>(Name);
	}

	FBattleMoveDefinition MakeSolarMove(
		const TCHAR* Name = SolarMoveName,
		const bool bPairedSemiInvulnerability = true)
	{
		FBattleMoveDefinition Move;
		Move.Id = MoveId(Name);
		Move.Type = EPokemonType::Grass;
		Move.Category = EBattleMoveCategory::Special;
		Move.Power = 120;
		Move.Accuracy = 100;
		Move.bAlwaysHits = false;
		Move.bUsesPP = true;
		Move.BasePP = 20;
		Move.bAllowsPPBoosts = true;
		Move.TargetClass = EBattleTargetClass::SelectedOpponent;
		Move.Flags = EBattleMoveFlags::NeverCritical
			| EBattleMoveFlags::BlockedByProtect
			| EBattleMoveFlags::SkipsChargeInSun
			| EBattleMoveFlags::HalvesPowerInRainSandstormOrSnow;
		FBattleMoveEffectDescriptor Charge;
		Charge.Order = 0;
		Charge.Kind = EBattleMoveEffectKind::Charge;
		Charge.Target = EBattleEffectTarget::User;
		Charge.ConditionId = FBattleVolatileRules::GetChargingId();
		Move.Effects.Add(Charge);
		if (bPairedSemiInvulnerability)
		{
			FBattleMoveEffectDescriptor Semi;
			Semi.Order = 1;
			Semi.Kind = EBattleMoveEffectKind::SemiInvulnerability;
			Semi.Target = EBattleEffectTarget::User;
			Semi.ConditionId = FBattleVolatileRules::GetFlySemiInvulnerableId();
			Move.Effects.Add(Semi);
		}
		FBattleMoveEffectDescriptor Damage;
		Damage.Order = bPairedSemiInvulnerability ? 2 : 1;
		Damage.Kind = EBattleMoveEffectKind::Damage;
		Damage.Target = EBattleEffectTarget::ResolvedTarget;
		Move.Effects.Add(Damage);
		return Move;
	}

	FBattleMoveDefinition MakeThunderMove(const TCHAR* Name = ThunderMoveName)
	{
		FBattleMoveDefinition Move;
		Move.Id = MoveId(Name);
		Move.Type = EPokemonType::Electric;
		Move.Category = EBattleMoveCategory::Special;
		Move.Power = 110;
		Move.Accuracy = 70;
		Move.bAlwaysHits = false;
		Move.bUsesPP = true;
		Move.BasePP = 20;
		Move.bAllowsPPBoosts = true;
		Move.TargetClass = EBattleTargetClass::SelectedOpponent;
		Move.Flags = EBattleMoveFlags::NeverCritical
			| EBattleMoveFlags::BlockedByProtect
			| EBattleMoveFlags::ReachesAirborneSemiInvulnerableTarget
			| EBattleMoveFlags::RainAlwaysHitsSunAccuracyFifty;
		FBattleMoveEffectDescriptor Damage;
		Damage.Order = 0;
		Damage.Kind = EBattleMoveEffectKind::Damage;
		Damage.Target = EBattleEffectTarget::ResolvedTarget;
		Move.Effects.Add(Damage);
		FBattleMoveEffectDescriptor Paralysis;
		Paralysis.Order = 1;
		Paralysis.Kind = EBattleMoveEffectKind::ApplyCondition;
		Paralysis.Target = EBattleEffectTarget::ResolvedTarget;
		Paralysis.ConditionId = FBattleMajorStatusRules::GetParalysisId();
		Paralysis.ChanceNumerator = 30;
		Paralysis.ChanceDenominator = 100;
		Move.Effects.Add(Paralysis);
		return Move;
	}

	bool TryMakeCatalog(
		const TArray<FBattleMoveDefinition>& Moves,
		FBattleDefinitionCatalog& OutCatalog,
		TArray<FBattleCatalogDiagnostic>* OutDiagnostics = nullptr)
	{
		FBattleDefinitionCatalogInput Input;
		Input.TypeChartEntries = MakeNeutralTypeChart();
		Input.Moves = {MakeProbeMove(), MakeTargetProbeMove()};
		Input.Moves.Append(Moves);
		Input.Abilities =
		{
			{FBattleAbilityRules::GetBlazeId()},
			{FBattleAbilityRules::GetIntimidateId()},
			{FBattleAbilityRules::GetMagicGuardId()}
		};
		for (const FConditionId& Id : FBattleVolatileRules::GetCanonicalIds())
		{
			Input.Conditions.Add({Id, EBattleConditionKind::Volatile});
		}
		for (const FConditionId& Id : FBattleMajorStatusRules::GetCanonicalIds())
		{
			Input.Conditions.Add({Id, EBattleConditionKind::MajorStatus});
		}
		for (const FConditionId& Id : FBattleFieldSideConditionRules::GetCanonicalIds())
		{
			Input.Conditions.Add(
				{Id, FBattleFieldSideConditionRules::GetConditionFamily(Id)});
		}
		Input.SpeciesForms.Add(MakeSpecies(PlayerSpeciesName));
		Input.SpeciesForms.Add(MakeSpecies(WildSpeciesName));
		TArray<FBattleCatalogDiagnostic> Diagnostics;
		const bool bCreated = FBattleDefinitionCatalog::TryCreate(
			Input, OutCatalog, Diagnostics);
		if (OutDiagnostics != nullptr)
		{
			*OutDiagnostics = Diagnostics;
		}
		return bCreated && Diagnostics.IsEmpty();
	}

	bool TryMakeEngine(
		const FBattleMoveDefinition& Move,
		TArray<FBattleExpectedRandomDraw> Draws,
		TUniquePtr<FBattleEngine>& OutEngine,
		FStrictBattleRandom*& OutRandom)
	{
		OutEngine.Reset();
		OutRandom = nullptr;
		FAtomicWildScenario Scenario = MakePreMoveScenario(Move.Id);
		FBattleSetup Setup;
		EBattleSetupValidationError SetupError = EBattleSetupValidationError::None;
		FBattleDefinitionCatalog Catalog;
		if (!FBattleSetup::TryCreate(MakeSetupInput(Scenario), Setup, SetupError)
			|| !TryMakeCatalog({Move}, Catalog))
		{
			return false;
		}
		TUniquePtr<FStrictBattleRandom> Random =
			MakeUnique<FStrictBattleRandom>(MoveTemp(Draws));
		OutRandom = Random.Get();
		FBattleRejection Rejection;
		return FBattleEngine::TryCreate(
			Setup, Catalog, MoveTemp(Random), OutEngine, Rejection);
	}

	bool TrySeedWeather(
		FBattleEngine& Engine,
		const FConditionId WeatherId,
		const bool bSuppressRelevantPhase = false,
		const EBattleTriggerPhase Phase = EBattleTriggerPhase::BeforeAccuracy)
	{
		FBattleEngineState& State =
			FBattleC09BWildFlowEngineFixture::GetMutableState(Engine);
		if (State.Field.Weather.IsSet()
			|| !FBattleFieldSideConditionRules::IsCanonical(WeatherId))
		{
			return false;
		}
		FBattleTriggerSubject Source;
		const FBattlerId SourceId = MakeNumericId<FBattlerId>(PlayerLeftValue);
		if (!FBattleTriggerSubject::TryCreateBattler(SourceId, Source))
		{
			return false;
		}
		FBattleFieldSideTriggerRegistrationFacts Facts;
		Facts.ConditionId = WeatherId;
		Facts.PayloadId = WeatherId.GetDefinitionId();
		Facts.Owner = FBattleTriggerSubject::CreateField();
		Facts.Source = Source;
		Facts.Targets.Add(Facts.Owner);
		Facts.RemainingTurns = 5;
		EBattleTriggerError TriggerError = EBattleTriggerError::None;
		if (!FBattleFieldSideConditionRules::TryRegisterTriggers(
				State.TriggerFramework, Facts, TriggerError))
		{
			return false;
		}
		FBattleConditionState Weather;
		Weather.ConditionId = WeatherId;
		Weather.RemainingTurns = 5;
		Weather.LayerCount = 1;
		Weather.CreationOrdinal = State.NextConditionCreationOrdinal++;
		Weather.SourceBattlerId = SourceId;
		State.Field.Weather = Weather;
		if (bSuppressRelevantPhase)
		{
			FBattleTriggerOperationContext Operation;
			if (!FBattleTriggerReentrancyToken::TryCreate(
					State.NextTriggerReentrancyToken++, Operation.ReentrancyToken))
			{
				return false;
			}
			bool bFound = false;
			for (const FBattleTriggerRegistrationState& Registration :
				State.TriggerFramework.GetActiveRegistrations())
			{
				if (Registration.Spec.SourceDefinition.Kind
						== EBattleTriggerSourceDefinitionKind::Condition
					&& Registration.Spec.SourceDefinition.ConditionId == WeatherId
					&& Registration.Spec.Rule.Phase == Phase)
				{
					if (!State.TriggerFramework.TrySetSuppressed(
							Registration.RegistrationId,
							true,
							Operation,
							TriggerError))
					{
						return false;
					}
					bFound = true;
				}
			}
			if (!bFound)
			{
				return false;
			}
		}
		TArray<FBattleTriggerLifecycleFact> IgnoredFacts;
		TArray<FBattleTriggerEffectRequest> IgnoredRequests;
		State.TriggerFramework.DrainLifecycleFacts(IgnoredFacts);
		State.TriggerFramework.DrainEffectRequests(IgnoredRequests);
		return true;
	}

	bool TrySeedTerrain(FBattleEngine& Engine, const FConditionId TerrainId)
	{
		FBattleEngineState& State =
			FBattleC09BWildFlowEngineFixture::GetMutableState(Engine);
		if (State.Field.Terrain.IsSet())
		{
			return false;
		}
		FBattleTriggerSubject Source;
		const FBattlerId SourceId = MakeNumericId<FBattlerId>(PlayerLeftValue);
		if (!FBattleTriggerSubject::TryCreateBattler(SourceId, Source))
		{
			return false;
		}
		FBattleFieldSideTriggerRegistrationFacts Facts;
		Facts.ConditionId = TerrainId;
		Facts.PayloadId = TerrainId.GetDefinitionId();
		Facts.Owner = FBattleTriggerSubject::CreateField();
		Facts.Source = Source;
		Facts.Targets.Add(Facts.Owner);
		Facts.RemainingTurns = 5;
		EBattleTriggerError Error = EBattleTriggerError::None;
		if (!FBattleFieldSideConditionRules::TryRegisterTriggers(
				State.TriggerFramework, Facts, Error))
		{
			return false;
		}
		FBattleConditionState Terrain;
		Terrain.ConditionId = TerrainId;
		Terrain.RemainingTurns = 5;
		Terrain.LayerCount = 1;
		Terrain.CreationOrdinal = State.NextConditionCreationOrdinal++;
		Terrain.SourceBattlerId = SourceId;
		State.Field.Terrain = Terrain;
		TArray<FBattleTriggerLifecycleFact> Ignored;
		State.TriggerFramework.DrainLifecycleFacts(Ignored);
		return true;
	}

	bool TryMakeRequest(
		FBattleEngine& Engine,
		const FMoveId Move,
		FBattleEffectExecutionRequest& OutRequest,
		FBattleResolvedTarget& OutTarget,
		const uint64 OperationValue = 9400)
	{
		FBattleEngineState& State =
			FBattleC09BWildFlowEngineFixture::GetMutableState(Engine);
		const FBattleMoveDefinition* Definition = State.Catalog.FindMove(Move);
		const FBattlerId UserId = MakeNumericId<FBattlerId>(PlayerLeftValue);
		const FBattlerId TargetId = MakeNumericId<FBattlerId>(OpponentLeftValue);
		const FBattleActivePositionState* User = State.ActivePositions.FindByPredicate(
			[UserId](const FBattleActivePositionState& Candidate)
			{
				return Candidate.BattlerId == UserId;
			});
		const FBattleActivePositionState* Target = State.ActivePositions.FindByPredicate(
			[TargetId](const FBattleActivePositionState& Candidate)
			{
				return Candidate.BattlerId == TargetId;
			});
		if (Definition == nullptr || User == nullptr || Target == nullptr
			|| !FBattleResolvedTarget::TryCreateBattler(
				{Target->ActiveSlotId, TargetId}, OutTarget))
		{
			return false;
		}
		OutRequest = FBattleEffectExecutionRequest();
		OutRequest.BattleId = State.Setup.GetBattleId();
		OutRequest.TurnId = State.TurnId;
		OutRequest.ActionId = MakeNumericId<FActionId>(OperationValue);
		OutRequest.ResolutionId = MakeNumericId<FResolutionId>(OperationValue);
		OutRequest.UserBattlerId = UserId;
		OutRequest.UserSlotId = User->ActiveSlotId;
		OutRequest.Move = Definition;
		OutRequest.Targets.Add(OutTarget);
		return true;
	}

	bool TryExecuteDirect(
		FBattleEngine& Engine,
		const FMoveId Move,
		FBattleEffectExecutionResult& OutResult,
		EBattleEffectExecutorError& OutError)
	{
		FBattleEffectExecutionRequest Request;
		FBattleResolvedTarget Target;
		return TryMakeRequest(Engine, Move, Request, Target)
			&& FBattleEffectExecutor::TryExecuteAgainstState(
				Request,
				FBattleC09BWildFlowEngineFixture::GetMutableState(Engine),
				OutResult,
				OutError);
	}

	bool TryPrepareEffectsCheckpoint(FBattleEngine& Engine, const FMoveId Move)
	{
		return TryPrepareTargetCheckpoint(Engine, Move)
			&& Engine.ResolveCurrentMoveTargets().WasAccepted();
	}

	bool HasVolatile(
		const FBattleEngine& Engine,
		const uint64 BattlerValue,
		const FConditionId VolatileId)
	{
		const FBattleBattlerState* Battler =
			FBattleC09BWildFlowEngineFixture::GetState(Engine).FindBattler(
				MakeNumericId<FBattlerId>(BattlerValue));
		return Battler != nullptr && Battler->Volatiles.ContainsByPredicate(
			[VolatileId](const FBattleConditionState& State)
			{
				return State.ConditionId == VolatileId;
			});
	}

	bool HasMajorStatus(const FBattleEngine& Engine, const FConditionId StatusId)
	{
		const FBattleBattlerState* Battler =
			FBattleC09BWildFlowEngineFixture::GetState(Engine).FindBattler(
				MakeNumericId<FBattlerId>(OpponentLeftValue));
		return Battler != nullptr && Battler->MajorStatusId == StatusId;
	}

	bool HasExactExecutionEvents(
		const FBattleEffectExecutionResult& Result,
		const TArray<EBattleEventType>& Expected)
	{
		if (Result.Events.Num() != Expected.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < Expected.Num(); ++Index)
		{
			if (Result.Events[Index].Type != Expected[Index])
			{
				return false;
			}
		}
		return true;
	}

	bool HasExactResolutionEvents(
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

	const FBattleDamageTraceEntry* FindTrace(
		const FDamageTrace& Trace,
		const EBattleDamageTraceStep Step,
		const FDefinitionId* RuleId = nullptr)
	{
		return Trace.Entries.FindByPredicate(
			[Step, RuleId](const FBattleDamageTraceEntry& Entry)
			{
				return Entry.Step == Step
					&& (RuleId == nullptr || Entry.RuleId == *RuleId);
			});
	}

	bool TrySerializeReplay(FBattleEngine& Engine, TArray<uint8>& OutBytes)
	{
		FBattleRejection Rejection;
		return FBattleReplaySerializer::TrySerializeCanonical(
			Engine.ExportReplayRecord(), OutBytes, Rejection);
	}

	FName PokemonTypeName(const int32 Index)
	{
		static const FName Names[] =
		{
			TEXT("Normal"), TEXT("Fire"), TEXT("Water"), TEXT("Electric"),
			TEXT("Grass"), TEXT("Ice"), TEXT("Fighting"), TEXT("Poison"),
			TEXT("Ground"), TEXT("Flying"), TEXT("Psychic"), TEXT("Bug"),
			TEXT("Rock"), TEXT("Ghost"), TEXT("Dragon"), TEXT("Dark"),
			TEXT("Steel"), TEXT("Fairy")
		};
		check(Index >= 0 && Index < UE_ARRAY_COUNT(Names));
		return Names[Index];
	}

	template <typename RowType>
	UDataTable* MakeTransientTable()
	{
		UDataTable* Table = NewObject<UDataTable>(GetTransientPackage());
		check(Table != nullptr);
		Table->RowStruct = RowType::StaticStruct();
		return Table;
	}

	bool TryBuildAdapterCatalog(
		const TArray<FName>& Flags,
		FBattleDefinitionCatalog& OutCatalog,
		TArray<FBattleCatalogDiagnostic>& OutDiagnostics,
		const int32 Accuracy = 100)
	{
		FBattleDataTableSet Tables;
		Tables.SpeciesForms = MakeTransientTable<FBattleSpeciesFormTableRow>();
		Tables.Natures = MakeTransientTable<FBattleNatureTableRow>();
		UDataTable* Moves = MakeTransientTable<FBattleMoveTableRow>();
		Tables.Moves = Moves;
		Tables.Abilities = MakeTransientTable<FBattleAbilityTableRow>();
		Tables.Items = MakeTransientTable<FBattleItemTableRow>();
		UDataTable* Conditions = MakeTransientTable<FBattleConditionTableRow>();
		Tables.Conditions = Conditions;
		UDataTable* TypeChart = MakeTransientTable<FBattleTypeChartTableRow>();
		Tables.TypeChart = TypeChart;

		FBattleMoveTableRow Row;
		Row.Type = FName(TEXT("Grass"));
		Row.Category = FName(TEXT("Special"));
		Row.Power = 120;
		Row.Accuracy = Accuracy;
		Row.bAlwaysHits = false;
		Row.BasePP = 10;
		Row.TargetClass = FName(TEXT("SelectedOpponent"));
		Row.Flags = Flags;
		FBattleMoveEffectTableRow Charge;
		Charge.Order = 0;
		Charge.Kind = FName(TEXT("Charge"));
		Charge.Target = FName(TEXT("User"));
		Charge.ConditionId = FBattleVolatileRules::GetChargingId()
			.GetDefinitionId().GetName();
		Row.Effects.Add(Charge);
		FBattleMoveEffectTableRow Damage;
		Damage.Order = 1;
		Damage.Kind = FName(TEXT("Damage"));
		Damage.Target = FName(TEXT("ResolvedTarget"));
		Row.Effects.Add(Damage);
		Moves->AddRow(FName(AdapterMoveName), Row);

		FBattleConditionTableRow Charging;
		Charging.Kind = FName(TEXT("Volatile"));
		Conditions->AddRow(
			FBattleVolatileRules::GetChargingId().GetDefinitionId().GetName(),
			Charging);
		for (int32 Attack = 0; Attack < FBattleTypeChart::TypeCount; ++Attack)
		{
			FBattleTypeChartTableRow TypeRow;
			for (int32 Defense = 0; Defense < FBattleTypeChart::TypeCount; ++Defense)
			{
				TypeRow.Entries.Add({PokemonTypeName(Defense), 1, 1});
			}
			TypeChart->AddRow(PokemonTypeName(Attack), TypeRow);
		}
		return FBattleDataTableAdapter::BuildCatalog(
			Tables, OutCatalog, OutDiagnostics);
	}
}

using namespace BattleMoveWeatherRuleTestsPrivate;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC10WeatherMoveRulesContractTest,
	"PokemonSolarus.Battle.C05B.C10WeatherMoveRules.Contract.FlagsAdapterCatalogAndTriggerPhases",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC10WeatherMoveRulesContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	bool bValid = TestEqual(TEXT("Sun charge-skip uses append-only bit 19"),
		static_cast<uint32>(EBattleMoveFlags::SkipsChargeInSun), 1U << 19);
	bValid &= TestEqual(TEXT("Weather half-power uses append-only bit 20"),
		static_cast<uint32>(EBattleMoveFlags::HalvesPowerInRainSandstormOrSnow),
		1U << 20);
	bValid &= TestEqual(TEXT("Rain/Sun accuracy uses append-only bit 21"),
		static_cast<uint32>(EBattleMoveFlags::RainAlwaysHitsSunAccuracyFifty),
		1U << 21);

	EBattleMoveWeatherRuleValidationError RuleError =
		EBattleMoveWeatherRuleValidationError::None;
	const FBattleMoveDefinition Solar = MakeSolarMove();
	const FBattleMoveDefinition Thunder = MakeThunderMove();
	bValid &= TestTrue(TEXT("The authored Solar shape is valid"),
		FBattleMoveWeatherRules::TryValidateMoveDefinition(Solar, RuleError));
	bValid &= TestTrue(TEXT("The authored Thunder shape is valid"),
		FBattleMoveWeatherRules::TryValidateMoveDefinition(Thunder, RuleError));
	bValid &= TestTrue(TEXT("Catalog construction accepts both valid authored traits"),
		[&Solar, &Thunder]()
		{
			FBattleDefinitionCatalog Catalog;
			return TryMakeCatalog({Solar, Thunder}, Catalog);
		}());

	TArray<FBattleMoveDefinition> InvalidMoves;
	TArray<EBattleMoveWeatherRuleValidationError> ExpectedErrors;
	auto AddInvalid = [&InvalidMoves, &ExpectedErrors](
		FBattleMoveDefinition Move,
		const EBattleMoveWeatherRuleValidationError Error)
	{
		InvalidMoves.Add(Move);
		ExpectedErrors.Add(Error);
	};
	FBattleMoveDefinition Invalid = Solar;
	Invalid.Flags = EBattleMoveFlags::SkipsChargeInSun;
	Invalid.Category = EBattleMoveCategory::Status;
	Invalid.Power = 0;
	AddInvalid(Invalid,
		EBattleMoveWeatherRuleValidationError::ChargeSkipRequiresDamagingMove);
	Invalid = Solar;
	Invalid.Effects.RemoveAt(0);
	AddInvalid(Invalid,
		EBattleMoveWeatherRuleValidationError::ChargeSkipRequiresPrimaryChargeBeforeDamage);
	Invalid = Solar;
	const FBattleMoveEffectDescriptor DuplicateCharge = Invalid.Effects[0];
	Invalid.Effects.Insert(DuplicateCharge, 0);
	AddInvalid(Invalid,
		EBattleMoveWeatherRuleValidationError::ChargeSkipRequiresPrimaryChargeBeforeDamage);
	Invalid = Solar;
	Invalid.Effects[0].Order = Invalid.Effects.Last().Order + 1;
	AddInvalid(Invalid,
		EBattleMoveWeatherRuleValidationError::ChargeSkipRequiresPrimaryChargeBeforeDamage);
	Invalid = Solar;
	Invalid.Effects[0].Target = EBattleEffectTarget::ResolvedTarget;
	AddInvalid(Invalid,
		EBattleMoveWeatherRuleValidationError::ChargeSkipRequiresPrimaryChargeBeforeDamage);
	Invalid = Solar;
	Invalid.Effects[0].ChanceNumerator = 50;
	Invalid.Effects[0].ChanceDenominator = 100;
	AddInvalid(Invalid,
		EBattleMoveWeatherRuleValidationError::ChargeSkipRequiresPrimaryChargeBeforeDamage);
	Invalid = Solar;
	Invalid.Effects[0].ConditionId = FBattleVolatileRules::GetProtectId();
	AddInvalid(Invalid,
		EBattleMoveWeatherRuleValidationError::ChargeSkipRequiresPrimaryChargeBeforeDamage);
	Invalid = Solar;
	Invalid.Flags = EBattleMoveFlags::HalvesPowerInRainSandstormOrSnow;
	Invalid.Category = EBattleMoveCategory::Status;
	Invalid.Power = 0;
	AddInvalid(Invalid,
		EBattleMoveWeatherRuleValidationError::PowerModifierRequiresDamagingMove);
	Invalid = Thunder;
	Invalid.Category = EBattleMoveCategory::Status;
	Invalid.Power = 0;
	AddInvalid(Invalid,
		EBattleMoveWeatherRuleValidationError::WeatherAccuracyRequiresDamagingMove);
	Invalid = Thunder;
	Invalid.TargetClass = EBattleTargetClass::Field;
	AddInvalid(Invalid,
		EBattleMoveWeatherRuleValidationError::WeatherAccuracyRequiresBattlerTarget);
	Invalid = Thunder;
	Invalid.Accuracy = 71;
	AddInvalid(Invalid,
		EBattleMoveWeatherRuleValidationError::WeatherAccuracyRequiresSeventyAccuracy);
	Invalid = Thunder;
	Invalid.bAlwaysHits = true;
	AddInvalid(Invalid,
		EBattleMoveWeatherRuleValidationError::WeatherAccuracyRequiresOrdinaryAccuracy);
	Invalid = Thunder;
	Invalid.Flags |= EBattleMoveFlags::UsesPerHitAccuracy;
	AddInvalid(Invalid,
		EBattleMoveWeatherRuleValidationError::WeatherAccuracyDisallowsPerHitAccuracy);

	TUniquePtr<FBattleEngine> ValidationEngine;
	FStrictBattleRandom* ValidationRandom = nullptr;
	bValid &= TestTrue(TEXT("The direct-validation fixture is created"),
		TryMakeEngine(Solar, {}, ValidationEngine, ValidationRandom));
	for (int32 Index = 0; Index < InvalidMoves.Num(); ++Index)
	{
		RuleError = EBattleMoveWeatherRuleValidationError::None;
		bValid &= TestFalse(FString::Printf(TEXT("Invalid trait %d is rejected by the pure seam"), Index),
			FBattleMoveWeatherRules::TryValidateMoveDefinition(
				InvalidMoves[Index], RuleError));
		bValid &= TestEqual(FString::Printf(TEXT("Invalid trait %d has its typed error"), Index),
			RuleError, ExpectedErrors[Index]);
		FBattleDefinitionCatalog RejectedCatalog;
		TArray<FBattleCatalogDiagnostic> Diagnostics;
		bValid &= TestFalse(FString::Printf(TEXT("Invalid trait %d is rejected by catalog construction"), Index),
			TryMakeCatalog({InvalidMoves[Index]}, RejectedCatalog, &Diagnostics));
		bValid &= TestTrue(FString::Printf(TEXT("Invalid trait %d reports the Flags field"), Index),
			Diagnostics.ContainsByPredicate(
				[](const FBattleCatalogDiagnostic& Diagnostic)
				{
					return Diagnostic.Code
							== EBattleCatalogDiagnosticCode::IncompatibleEffect
						&& Diagnostic.Field == FName(TEXT("Flags"));
				}));

		if (ValidationEngine.IsValid())
		{
			FBattleEffectExecutionRequest Request;
			FBattleResolvedTarget Target;
			if (TestTrue(FString::Printf(TEXT("Invalid trait %d request is built"), Index),
				TryMakeRequest(*ValidationEngine, Solar.Id, Request, Target, 9500 + Index)))
			{
				Request.Move = &InvalidMoves[Index];
				const uint64 BeforeVersion =
					FBattleC09BWildFlowEngineFixture::GetState(*ValidationEngine)
						.StateVersion;
				FBattleEffectExecutionResult Result;
				EBattleEffectExecutorError ExecutorError = EBattleEffectExecutorError::None;
				bValid &= TestFalse(FString::Printf(TEXT("Invalid trait %d is rejected by direct execution"), Index),
					FBattleEffectExecutor::TryExecuteAgainstState(
						Request,
						FBattleC09BWildFlowEngineFixture::GetMutableState(*ValidationEngine),
						Result,
						ExecutorError));
				bValid &= TestEqual(FString::Printf(TEXT("Invalid trait %d has the direct typed error"), Index),
					ExecutorError, EBattleEffectExecutorError::InvalidMoveDefinition);
				bValid &= TestEqual(FString::Printf(TEXT("Invalid trait %d mutates no state version"), Index),
					FBattleC09BWildFlowEngineFixture::GetState(*ValidationEngine).StateVersion,
					BeforeVersion);
			}
		}
	}

	FBattleDefinitionCatalog Adapted;
	TArray<FBattleCatalogDiagnostic> AdapterDiagnostics;
	const TArray<FName> AllNames =
	{
		FName(TEXT("SkipsChargeInSun")),
		FName(TEXT("HalvesPowerInRainSandstormOrSnow")),
		FName(TEXT("RainAlwaysHitsSunAccuracyFifty"))
	};
	bValid &= TestTrue(TEXT("The adapter parses all three exact names"),
		TryBuildAdapterCatalog(AllNames, Adapted, AdapterDiagnostics, 70));
	const FBattleMoveDefinition* AdaptedMove = Adapted.FindMove(MoveId(AdapterMoveName));
	bValid &= TestTrue(TEXT("The adapter preserves all three exact flag values"),
		AdaptedMove != nullptr
			&& EnumHasAllFlags(AdaptedMove->Flags, EBattleMoveFlags::SkipsChargeInSun)
			&& EnumHasAllFlags(AdaptedMove->Flags,
				EBattleMoveFlags::HalvesPowerInRainSandstormOrSnow)
			&& EnumHasAllFlags(AdaptedMove->Flags,
				EBattleMoveFlags::RainAlwaysHitsSunAccuracyFifty));
	AdapterDiagnostics.Reset();
	bValid &= TestFalse(TEXT("The adapter rejects a duplicate flag name"),
		TryBuildAdapterCatalog(
			{AllNames[0], AllNames[0]}, Adapted, AdapterDiagnostics));
	bValid &= TestTrue(TEXT("Duplicate flags report the Flags field"),
		AdapterDiagnostics.ContainsByPredicate(
			[](const FBattleCatalogDiagnostic& Diagnostic)
			{
				return Diagnostic.Field == FName(TEXT("Flags"));
			}));
	AdapterDiagnostics.Reset();
	bValid &= TestFalse(TEXT("The adapter rejects an unknown flag name"),
		TryBuildAdapterCatalog(
			{FName(TEXT("WeatherMoveRuleTypo"))}, Adapted, AdapterDiagnostics));

	FBattleTriggerSubject Source;
	bValid &= TestTrue(TEXT("Trigger-contract source is valid"),
		FBattleTriggerSubject::TryCreateBattler(
			MakeNumericId<FBattlerId>(PlayerLeftValue), Source));
	auto ExactPhases = [&Source](
		const FConditionId ConditionId,
		const TArray<EBattleTriggerPhase>& Expected)
	{
		FBattleFieldSideTriggerRegistrationFacts Facts;
		Facts.ConditionId = ConditionId;
		Facts.PayloadId = ConditionId.GetDefinitionId();
		Facts.Owner = FBattleTriggerSubject::CreateField();
		Facts.Source = Source;
		Facts.Targets.Add(Facts.Owner);
		Facts.RemainingTurns = 5;
		TArray<FBattleTriggerRegistrationSpec> Specs;
		if (!FBattleFieldSideConditionRules::TryBuildTriggerRegistrationSpecs(
				Facts, Specs)
			|| Specs.Num() != Expected.Num() + 1)
		{
			return false;
		}
		for (int32 Index = 0; Index < Expected.Num(); ++Index)
		{
			if (Specs[Index].Rule.Phase != Expected[Index])
			{
				return false;
			}
		}
		return Specs.Last().Rule.Phase == EBattleTriggerPhase::EndTurn;
	};
	bValid &= TestTrue(TEXT("Sun has exact BeforeHit, BeforeAccuracy, BeforeDamage registrations"),
		ExactPhases(FBattleFieldSideConditionRules::GetSunId(),
			{EBattleTriggerPhase::BeforeHit, EBattleTriggerPhase::BeforeAccuracy,
			 EBattleTriggerPhase::BeforeDamage}));
	bValid &= TestTrue(TEXT("Rain has exact BeforeAccuracy, BeforeDamage registrations"),
		ExactPhases(FBattleFieldSideConditionRules::GetRainId(),
			{EBattleTriggerPhase::BeforeAccuracy, EBattleTriggerPhase::BeforeDamage}));
	FBattleTriggerEffectId SunAccuracyEffect;
	FBattleTriggerEffectId RainAccuracyEffect;
	bValid &= TestTrue(TEXT("Sun exposes the literal BeforeAccuracy trigger identity"),
		FBattleFieldSideConditionRules::TryGetTriggerEffectId(
			FBattleFieldSideConditionRules::GetSunId(),
			EBattleTriggerPhase::BeforeAccuracy,
			SunAccuracyEffect)
			&& SunAccuracyEffect.GetDefinitionId().GetName()
				== FName(TEXT("Trigger.FieldSide.Sun.BeforeAccuracy")));
	bValid &= TestTrue(TEXT("Rain exposes the literal BeforeAccuracy trigger identity"),
		FBattleFieldSideConditionRules::TryGetTriggerEffectId(
			FBattleFieldSideConditionRules::GetRainId(),
			EBattleTriggerPhase::BeforeAccuracy,
			RainAccuracyEffect)
			&& RainAccuracyEffect.GetDefinitionId().GetName()
				== FName(TEXT("Trigger.FieldSide.Rain.BeforeAccuracy")));
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC10WeatherMoveRulesChargeTest,
	"PokemonSolarus.Battle.C05B.C10WeatherMoveRules.Charge.SunSkipOrdinaryChargePpTargetLockCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC10WeatherMoveRulesChargeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FBattleMoveDefinition Move = MakeSolarMove();
	const FBattlerId UserId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	TUniquePtr<FBattleEngine> Ordinary;
	FStrictBattleRandom* OrdinaryRandom = nullptr;
	if (!TestTrue(TEXT("Ordinary-weather charge engine is created"),
		TryMakeEngine(Move, {}, Ordinary, OrdinaryRandom)))
	{
		return false;
	}
	const int32 OrdinaryPPBefore = GetPreMovePP(*Ordinary, UserId, Move.Id);
	if (!TestTrue(TEXT("Ordinary Solar move reaches its effects checkpoint"),
		TryPrepareEffectsCheckpoint(*Ordinary, Move.Id)))
	{
		return false;
	}
	const FBattleResolution FirstTurn = Ordinary->ExecuteCurrentMoveEffects();
	const FBattleEngineState& OrdinaryState =
		FBattleC09BWildFlowEngineFixture::GetState(*Ordinary);
	const TArray<FBattleTriggerRegistrationState> ChargeRegistrations =
		OrdinaryState.TriggerFramework.GetActiveRegistrations();
	const FBattlerId TargetId = MakeNumericId<FBattlerId>(OpponentLeftValue);
	const FBattleActivePositionState* TargetPosition =
		OrdinaryState.ActivePositions.FindByPredicate(
			[TargetId](const FBattleActivePositionState& Candidate)
			{
				return Candidate.BattlerId == TargetId;
			});
	const FBattleTriggerRegistrationState* ChargeRegistration =
		ChargeRegistrations.FindByPredicate(
				[UserId](const FBattleTriggerRegistrationState& Registration)
				{
					return Registration.Spec.Owner.Kind
							== EBattleTriggerSubjectKind::Battler
						&& Registration.Spec.Owner.BattlerId == UserId
						&& Registration.Spec.SourceDefinition.Kind
							== EBattleTriggerSourceDefinitionKind::Condition
						&& Registration.Spec.SourceDefinition.ConditionId
							== FBattleVolatileRules::GetChargingId();
				});
	bool bValid = TestTrue(TEXT("Ordinary weather accepts and defers the first turn"),
		FirstTurn.WasAccepted()
			&& HasVolatile(*Ordinary, PlayerLeftValue,
				FBattleVolatileRules::GetChargingId()));
	bValid &= TestTrue(TEXT("The ordinary charge turn publishes the exact events"),
		HasExactResolutionEvents(FirstTurn,
			{EBattleEventType::StatusChanged,
			 EBattleEventType::ActionCompleted}));
	bValid &= TestEqual(TEXT("The first turn consumes exactly one PP"),
		GetPreMovePP(*Ordinary, UserId, Move.Id), OrdinaryPPBefore - 1);
	bValid &= TestTrue(TEXT("The charge locks the selected target identity"),
		ChargeRegistration != nullptr
			&& TargetPosition != nullptr
			&& ChargeRegistration->Spec.Rule.PayloadId == Move.Id.GetDefinitionId()
			&& ChargeRegistration->Spec.Targets.Num() == 2
			&& ChargeRegistration->Spec.Targets[0].Kind
				== EBattleTriggerSubjectKind::ActiveSlot
			&& ChargeRegistration->Spec.Targets[0].ActiveSlotId
				== TargetPosition->ActiveSlotId
			&& ChargeRegistration->Spec.Targets[1].Kind
				== EBattleTriggerSubjectKind::Battler
			&& ChargeRegistration->Spec.Targets[1].BattlerId == TargetId);
	bValid &= TestTrue(TEXT("Paired semi-invulnerability is installed on charge"),
		HasVolatile(*Ordinary, PlayerLeftValue,
			FBattleVolatileRules::GetFlySemiInvulnerableId()));
	bValid &= TestTrue(TEXT("The charge turn consumes no RNG"),
		OrdinaryRandom != nullptr && OrdinaryRandom->IsExact());

	const TArray<FBattleExpectedRandomDraw> HitDraws =
	{
		{0, 99, 0, FBattleEffectExecutor::GetAccuracyRulePurpose()},
		{0, 15, 0, FBattleEffectExecutor::GetDamageRandomRulePurpose()}
	};
	TUniquePtr<FBattleEngine> Release;
	FStrictBattleRandom* ReleaseRandom = nullptr;
	bValid &= TestTrue(TEXT("Charged-release engine is created"),
		TryMakeEngine(Move, HitDraws, Release, ReleaseRandom));
	if (Release.IsValid())
	{
		bValid &= TestTrue(TEXT("Charged release reaches its target checkpoint"),
			TrySeedChargedReleaseTargetCheckpoint(*Release, Move.Id));
		const int32 ReleasePP = GetPreMovePP(*Release, UserId, Move.Id);
		bValid &= TestTrue(TEXT("Charged release resolves its frozen target"),
			Release->ResolveCurrentMoveTargets().WasAccepted());
		const FBattleResolution Released = Release->ExecuteCurrentMoveEffects();
		bValid &= TestTrue(TEXT("Charged release executes damage"),
			Released.WasAccepted()
				&& Released.GetEvents().ContainsByPredicate(
					[](const FBattleEvent& Event)
					{
						return Event.GetType() == EBattleEventType::Damage;
					}));
		bValid &= TestEqual(TEXT("Charged release consumes no second PP"),
			GetPreMovePP(*Release, UserId, Move.Id), ReleasePP);
		bValid &= TestFalse(TEXT("Charged release cleans Charging"),
			HasVolatile(*Release, PlayerLeftValue, FBattleVolatileRules::GetChargingId()));
		bValid &= TestFalse(TEXT("Charged release cleans paired semi-invulnerability"),
			HasVolatile(*Release, PlayerLeftValue,
				FBattleVolatileRules::GetFlySemiInvulnerableId()));
		bValid &= TestTrue(TEXT("Charged release consumes exactly hit RNG"),
			ReleaseRandom != nullptr && ReleaseRandom->IsExact());
		bValid &= TestTrue(TEXT("Charged release publishes the exact damage events"),
			HasExactResolutionEvents(Released,
				{EBattleEventType::RandomCheck,
				 EBattleEventType::AccuracyChecked,
				 EBattleEventType::CriticalChecked,
				 EBattleEventType::RandomCheck,
				 EBattleEventType::Effectiveness,
				 EBattleEventType::Damage,
				 EBattleEventType::HPChanged,
				 EBattleEventType::ActionCompleted}));
	}

	TUniquePtr<FBattleEngine> Sun;
	FStrictBattleRandom* SunRandom = nullptr;
	bValid &= TestTrue(TEXT("Sun charge-skip engine is created"),
		TryMakeEngine(Move, HitDraws, Sun, SunRandom));
	if (Sun.IsValid())
	{
		bValid &= TestTrue(TEXT("Sun is registered before action preparation"),
			TrySeedWeather(*Sun, FBattleFieldSideConditionRules::GetSunId(),
				false, EBattleTriggerPhase::BeforeHit));
		const int32 SunPPBefore = GetPreMovePP(*Sun, UserId, Move.Id);
		bValid &= TestTrue(TEXT("Sun Solar move reaches its effects checkpoint"),
			TryPrepareEffectsCheckpoint(*Sun, Move.Id));
		const uint64 TriggerTokenBefore =
			FBattleC09BWildFlowEngineFixture::GetState(*Sun)
				.NextTriggerReentrancyToken;
		const FBattleResolution SunResult = Sun->ExecuteCurrentMoveEffects();
		bValid &= TestTrue(TEXT("Active Sun executes immediately without deferral"),
			SunResult.WasAccepted()
				&& !HasVolatile(*Sun, PlayerLeftValue,
					FBattleVolatileRules::GetChargingId())
				&& !HasVolatile(*Sun, PlayerLeftValue,
					FBattleVolatileRules::GetFlySemiInvulnerableId()));
		bValid &= TestEqual(TEXT("Sun skip still consumes exactly one PP"),
			GetPreMovePP(*Sun, UserId, Move.Id), SunPPBefore - 1);
		bValid &= TestTrue(TEXT("Sun skip consumes only ordinary hit RNG"),
			SunRandom != nullptr && SunRandom->IsExact());
		bValid &= TestEqual(
			TEXT("Cached charge skip dispatches BeforeHit once, then accuracy and damage once"),
			FBattleC09BWildFlowEngineFixture::GetState(*Sun)
				.NextTriggerReentrancyToken,
			TriggerTokenBefore + 3);
		bValid &= TestTrue(TEXT("Sun charge skip publishes the exact damage events"),
			HasExactResolutionEvents(SunResult,
				{EBattleEventType::RandomCheck,
				 EBattleEventType::AccuracyChecked,
				 EBattleEventType::CriticalChecked,
				 EBattleEventType::RandomCheck,
				 EBattleEventType::Effectiveness,
				 EBattleEventType::Damage,
				 EBattleEventType::HPChanged,
				 EBattleEventType::ActionCompleted}));
	}
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC10WeatherMoveRulesCurrentWeatherTest,
	"PokemonSolarus.Battle.C05B.C10WeatherMoveRules.Charge.ExecutionUsesCurrentWeather",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC10WeatherMoveRulesCurrentWeatherTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FBattleMoveDefinition Move = MakeSolarMove();
	FBattleMoveWeatherChargeSkipResult ChargeSkip;
	bool bValid = TestTrue(TEXT("No weather resolves without charge skip"),
		FBattleMoveWeatherRules::TryResolveChargeSkip(
			Move, EBattleFieldSideConditionKind::None, ChargeSkip)
			&& !ChargeSkip.bShouldSkipCharge);
	bValid &= TestTrue(TEXT("Only Sun resolves the authored charge skip"),
		FBattleMoveWeatherRules::TryResolveChargeSkip(
			Move, EBattleFieldSideConditionKind::Sun, ChargeSkip)
			&& ChargeSkip.bShouldSkipCharge);
	for (const EBattleFieldSideConditionKind NeutralKind :
		{EBattleFieldSideConditionKind::Rain,
		 EBattleFieldSideConditionKind::Sandstorm,
		 EBattleFieldSideConditionKind::Snow,
		 EBattleFieldSideConditionKind::ElectricTerrain,
		 EBattleFieldSideConditionKind::Invalid})
	{
		bValid &= TestTrue(TEXT("A non-Sun kind keeps the charge turn"),
			FBattleMoveWeatherRules::TryResolveChargeSkip(
				Move, NeutralKind, ChargeSkip)
				&& !ChargeSkip.bShouldSkipCharge);
	}

	const TArray<FBattleExpectedRandomDraw> HitDraws =
	{
		{0, 99, 0, FBattleEffectExecutor::GetAccuracyRulePurpose()},
		{0, 15, 0, FBattleEffectExecutor::GetDamageRandomRulePurpose()}
	};
	auto GetDamageAmount = [](const FBattleResolution& Resolution, int64& OutDamage)
	{
		OutDamage = 0;
		const FBattleEvent* Event = Resolution.GetEvents().FindByPredicate(
			[](const FBattleEvent& Candidate)
			{
				return Candidate.GetType() == EBattleEventType::Damage
					&& Candidate.GetNumericDelta().IsSet();
			});
		if (Event == nullptr || Event->GetNumericDelta().GetValue() >= 0)
		{
			return false;
		}
		OutDamage = -Event->GetNumericDelta().GetValue();
		return true;
	};
	TUniquePtr<FBattleEngine> NeutralRelease;
	FStrictBattleRandom* NeutralRandom = nullptr;
	int64 NeutralDamage = 0;
	bValid &= TestTrue(TEXT("Neutral charged-release engine is created"),
		TryMakeEngine(Move, HitDraws, NeutralRelease, NeutralRandom));
	if (NeutralRelease.IsValid())
	{
		bValid &= TestTrue(TEXT("Neutral charged release is staged and resolved"),
			TrySeedChargedReleaseTargetCheckpoint(*NeutralRelease, Move.Id)
				&& NeutralRelease->ResolveCurrentMoveTargets().WasAccepted());
		const FBattleResolution NeutralResult =
			NeutralRelease->ExecuteCurrentMoveEffects();
		bValid &= TestTrue(TEXT("Neutral charged release exposes its damage"),
			NeutralResult.WasAccepted()
				&& GetDamageAmount(NeutralResult, NeutralDamage)
				&& NeutralRandom != nullptr && NeutralRandom->IsExact());
	}
	TUniquePtr<FBattleEngine> Engine;
	FStrictBattleRandom* Random = nullptr;
	if (!TestTrue(TEXT("Weather-change engine is created"),
		TryMakeEngine(Move, HitDraws, Engine, Random)))
	{
		return false;
	}
	bValid &= TestTrue(TEXT("A charged release is staged before weather changes"),
		TrySeedChargedReleaseTargetCheckpoint(*Engine, Move.Id)
			&& HasVolatile(*Engine, PlayerLeftValue,
				FBattleVolatileRules::GetChargingId()));
	bValid &= TestTrue(TEXT("Rain can become authoritative after charging began"),
		TrySeedWeather(*Engine, FBattleFieldSideConditionRules::GetRainId(),
			false, EBattleTriggerPhase::BeforeDamage));
	bValid &= TestTrue(TEXT("The charged release resolves its locked target"),
		Engine->ResolveCurrentMoveTargets().WasAccepted());
	const FBattleResolution Released = Engine->ExecuteCurrentMoveEffects();
	int64 RainDamage = 0;
	bValid &= TestTrue(TEXT("Release uses current Rain and completes damage"),
		Released.WasAccepted()
			&& GetDamageAmount(Released, RainDamage));
	bValid &= TestTrue(TEXT("Rain appearing after charge reduces the actual release damage"),
		NeutralDamage > 0 && RainDamage > 0 && RainDamage < NeutralDamage);
	bValid &= TestFalse(TEXT("Release cleanup is independent of current weather"),
		HasVolatile(*Engine, PlayerLeftValue, FBattleVolatileRules::GetChargingId()));
	bValid &= TestTrue(TEXT("Release draws exact hit RNG under current Rain"),
		Random != nullptr && Random->IsExact()
			&& Random->GetTrace().Num() == 2);

	TUniquePtr<FBattleEngine> SuppressedSun;
	FStrictBattleRandom* SuppressedRandom = nullptr;
	bValid &= TestTrue(TEXT("Suppressed-Sun engine is created"),
		TryMakeEngine(Move, {}, SuppressedSun, SuppressedRandom));
	if (SuppressedSun.IsValid())
	{
		EBattleEffectExecutorError Error = EBattleEffectExecutorError::None;
		bValid &= TestTrue(TEXT("Sun BeforeHit is suppressed"),
			TrySeedWeather(*SuppressedSun,
				FBattleFieldSideConditionRules::GetSunId(), true,
				EBattleTriggerPhase::BeforeHit));
		FBattleEffectExecutionResult SuppressedResult;
		bValid &= TestTrue(TEXT("Suppressed Sun retains ordinary charging"),
			TryExecuteDirect(*SuppressedSun, Move.Id, SuppressedResult, Error)
				&& SuppressedResult.bMoveDeferred);
		bValid &= TestTrue(TEXT("Suppressed Sun consumes no first-turn RNG"),
			SuppressedRandom != nullptr && SuppressedRandom->IsExact());
	}
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC10WeatherMoveRulesPowerTest,
	"PokemonSolarus.Battle.C05B.C10WeatherMoveRules.Power.SupportedUnsupportedExactHalfAndPriority",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC10WeatherMoveRulesPowerTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FBattleMoveDefinition Move = MakeSolarMove();
	FBattleMoveWeatherPowerModifierResult Rule;
	bool bValid = true;
	for (const EBattleFieldSideConditionKind Kind :
		{EBattleFieldSideConditionKind::Rain,
		 EBattleFieldSideConditionKind::Sandstorm,
		 EBattleFieldSideConditionKind::Snow})
	{
		bValid &= TestTrue(TEXT("A supported weather resolves exact named half power"),
			FBattleMoveWeatherRules::TryResolvePowerModifier(Move, Kind, Rule)
				&& Rule.bApplies
				&& Rule.ModifierQ12 == 2048
				&& Rule.RuleId.IsValid());
	}
	const FDefinitionId HalfPowerRuleId = Rule.RuleId;
	bValid &= TestEqual(TEXT("The half-power modifier keeps its literal identity"),
		HalfPowerRuleId.GetName(),
		FName(TEXT("Rule.C10WeatherMoveRules.HalfPower")));
	for (const EBattleFieldSideConditionKind Kind :
		{EBattleFieldSideConditionKind::None,
		 EBattleFieldSideConditionKind::Sun,
		 EBattleFieldSideConditionKind::ElectricTerrain,
		 EBattleFieldSideConditionKind::Invalid})
	{
		bValid &= TestTrue(TEXT("A neutral or unsupported kind preserves authored power"),
			FBattleMoveWeatherRules::TryResolvePowerModifier(Move, Kind, Rule)
				&& !Rule.bApplies
				&& Rule.ModifierQ12 == 4096);
	}

	FConditionId UnsupportedWeatherId;
	bValid &= TestTrue(TEXT("The unsupported weather identity is valid"),
		FConditionId::TryCreate(
			FName(TEXT("Condition.C10R4B.UnsupportedWeather")),
			UnsupportedWeatherId));
	struct FPowerBridgeCase
	{
		FConditionId WeatherId;
		bool bSuppress = false;
		bool bUnsupported = false;
		bool bExpectedHalfPower = false;
	};
	const TArray<FPowerBridgeCase> BridgeCases =
	{
		{FBattleFieldSideConditionRules::GetRainId(), false, false, true},
		{FBattleFieldSideConditionRules::GetSandstormId(), false, false, true},
		{FBattleFieldSideConditionRules::GetSnowId(), false, false, true},
		{FBattleFieldSideConditionRules::GetSunId(), false, false, false},
		{FConditionId(), false, false, false},
		{UnsupportedWeatherId, false, true, false},
		{FBattleFieldSideConditionRules::GetRainId(), true, false, false}
	};
	for (int32 Index = 0; Index < BridgeCases.Num(); ++Index)
	{
		TUniquePtr<FBattleEngine> CaseEngine;
		FStrictBattleRandom* CaseParentRandom = nullptr;
		bValid &= TestTrue(
			FString::Printf(TEXT("Power bridge case %d engine is created"), Index),
			TryMakeEngine(Move, {}, CaseEngine, CaseParentRandom));
		if (!CaseEngine.IsValid())
		{
			continue;
		}
		const FPowerBridgeCase& Case = BridgeCases[Index];
		if (Case.WeatherId.IsValid() && !Case.bUnsupported)
		{
			bValid &= TestTrue(
				FString::Printf(TEXT("Power bridge case %d weather is seeded"), Index),
				TrySeedWeather(*CaseEngine, Case.WeatherId, Case.bSuppress,
					EBattleTriggerPhase::BeforeDamage));
		}
		else if (Case.bUnsupported)
		{
			FBattleConditionState UnsupportedWeather;
			UnsupportedWeather.ConditionId = Case.WeatherId;
			UnsupportedWeather.LayerCount = 1;
			FBattleC09BWildFlowEngineFixture::GetMutableState(*CaseEngine)
				.Field.Weather = UnsupportedWeather;
		}
		FBattleEffectExecutionRequest CaseRequest;
		FBattleResolvedTarget CaseTarget;
		const bool bRequestBuilt = TryMakeRequest(
			*CaseEngine, Move.Id, CaseRequest, CaseTarget, 9700 + Index);
		bValid &= TestTrue(
			FString::Printf(TEXT("Power bridge case %d request is built"), Index),
			bRequestBuilt);
		if (!bRequestBuilt)
		{
			continue;
		}
		FStrictBattleRandom CaseRandom({});
		BattleEffectExecutorPrivate::FStateExecutionContext CaseContext(
			CaseRequest,
			FBattleC09BWildFlowEngineFixture::GetState(*CaseEngine),
			CaseRandom);
		FBattleFinalDamageInput CaseProbe;
		FBattleFinalDamageInput CaseActual;
		const bool bBuilt = CaseContext.TryBuildDamageInput(
				Move, CaseTarget, false, CaseProbe)
			&& CaseContext.TryBuildDamageInput(
				Move, CaseTarget, false, CaseActual);
		bValid &= TestTrue(
			FString::Printf(TEXT("Power bridge case %d inputs build"), Index),
			bBuilt);
		const int32 MatchingCount = bBuilt
			? CaseActual.PowerModifiers.FilterByPredicate(
				[HalfPowerRuleId](const FBattleDamageModifier& Modifier)
				{
					return Modifier.RuleId == HalfPowerRuleId
						&& Modifier.ModifierQ12 == 2048;
				}).Num()
			: INDEX_NONE;
		bValid &= TestEqual(
			FString::Printf(TEXT("Power bridge case %d has the exact half-power count"), Index),
			MatchingCount,
			Case.bExpectedHalfPower ? 1 : 0);
	}

	TUniquePtr<FBattleEngine> Engine;
	FStrictBattleRandom* ParentRandom = nullptr;
	if (!TestTrue(TEXT("Power-order engine is created"),
		TryMakeEngine(Move, {}, Engine, ParentRandom)))
	{
		return false;
	}
	bValid &= TestTrue(TEXT("Rain and Grassy Terrain are registered"),
		TrySeedWeather(*Engine, FBattleFieldSideConditionRules::GetRainId(),
			false, EBattleTriggerPhase::BeforeDamage)
			&& TrySeedTerrain(*Engine,
				FBattleFieldSideConditionRules::GetGrassyTerrainId()));
	FBattleEffectExecutionRequest Request;
	FBattleResolvedTarget Target;
	bValid &= TestTrue(TEXT("Power-order request is built"),
		TryMakeRequest(*Engine, Move.Id, Request, Target, 9600));
	if (!bValid)
	{
		return false;
	}
	const FMoveId AllyRuleMove = MoveId(TEXT("Move.C10R4B.Priority10"));
	FBattleAllyActionPowerModifierRegistration Ally;
	Ally.TurnId = Request.TurnId;
	Ally.SourceActionId = MakeNumericId<FActionId>(9599);
	Ally.SourceMoveId = AllyRuleMove;
	Ally.TargetActionId = Request.ActionId;
	Ally.Target = {Request.UserSlotId, Request.UserBattlerId};
	Ally.MagnitudeNumerator = 3;
	Ally.MagnitudeDenominator = 2;
	FBattleC09BWildFlowEngineFixture::GetMutableState(*Engine)
		.AllyActionPowerModifierRegistrations.Add(Ally);
	FStrictBattleRandom ContextRandom({});
	BattleEffectExecutorPrivate::FStateExecutionContext Context(
		Request,
		FBattleC09BWildFlowEngineFixture::GetState(*Engine),
		ContextRandom);
	FBattleFinalDamageInput Probe;
	FBattleFinalDamageInput Actual;
	bValid &= TestTrue(TEXT("Pre-accuracy and actual damage inputs build"),
		Context.TryBuildDamageInput(Move, Target, false, Probe)
			&& Context.TryBuildDamageInput(Move, Target, false, Actual));
	bValid &= TestTrue(TEXT("Power modifiers follow priority 10, priority 6, then default 0"),
		Actual.PowerModifiers.Num() == 3
			&& Actual.PowerModifiers[0].RuleId == AllyRuleMove.GetDefinitionId()
			&& Actual.PowerModifiers[1].RuleId
				== FBattleFieldSideConditionRules::GetGrassyTerrainId().GetDefinitionId()
			&& Actual.PowerModifiers[2].RuleId == HalfPowerRuleId
			&& Actual.PowerModifiers[2].ModifierQ12 == 2048);

	Actual.PowerModifiers = {{HalfPowerRuleId, 2048, false}};
	Actual.RandomContext = {
		Request.BattleId, Request.TurnId, Request.ActionId, Request.ResolutionId,
		FBattleEffectExecutor::GetDamageRandomRulePurpose()};
	FStrictBattleRandom DamageRandom({
		{0, 15, 0, FBattleEffectExecutor::GetDamageRandomRulePurpose()}});
	FBattleFinalDamageResult Damage;
	EBattleDamageCalculationError DamageError =
		EBattleDamageCalculationError::None;
	bValid &= TestTrue(TEXT("Exact half-power input completes final damage"),
		FBattleFinalDamageCalculator::TryCalculateFinalDamage(
			Actual, DamageRandom, Damage, DamageError));
	const FBattleDamageTraceEntry* InputPower = FindTrace(
		Damage.Trace, EBattleDamageTraceStep::InputPower);
	const FBattleDamageTraceEntry* EffectivePower = FindTrace(
		Damage.Trace, EBattleDamageTraceStep::EffectivePower);
	const FBattleDamageTraceEntry* NamedModifier = FindTrace(
		Damage.Trace, EBattleDamageTraceStep::PowerModifierChain, &HalfPowerRuleId);
	bValid &= TestTrue(TEXT("The calculator records exact 120 to 60 arithmetic"),
		InputPower != nullptr && InputPower->Value == 120
			&& EffectivePower != nullptr && EffectivePower->Value == 60
			&& NamedModifier != nullptr && NamedModifier->Value == 2048);
	bValid &= TestTrue(TEXT("Exact arithmetic consumes only damage RNG"),
		DamageRandom.IsExact());
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC10WeatherMoveRulesAccuracyTest,
	"PokemonSolarus.Battle.C05B.C10WeatherMoveRules.Accuracy.RainSunOrdinaryDrawsAndSuppression",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC10WeatherMoveRulesAccuracyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FBattleMoveDefinition Move = MakeThunderMove();
	FBattleMoveWeatherAccuracyResult Accuracy;
	bool bValid = TestTrue(TEXT("Rain resolves literal always-hit with zero base accuracy"),
		FBattleMoveWeatherRules::TryResolveAccuracy(
			Move, EBattleFieldSideConditionKind::Rain, Accuracy)
			&& Accuracy.bAlwaysHits && Accuracy.BaseAccuracy == 0);
	bValid &= TestTrue(TEXT("Sun resolves ordinary numeric accuracy 50"),
		FBattleMoveWeatherRules::TryResolveAccuracy(
			Move, EBattleFieldSideConditionKind::Sun, Accuracy)
			&& !Accuracy.bAlwaysHits && Accuracy.BaseAccuracy == 50);
	for (const EBattleFieldSideConditionKind Kind :
		{EBattleFieldSideConditionKind::None,
		 EBattleFieldSideConditionKind::Sandstorm,
		 EBattleFieldSideConditionKind::Snow,
		 EBattleFieldSideConditionKind::ElectricTerrain,
		 EBattleFieldSideConditionKind::Invalid})
	{
		bValid &= TestTrue(TEXT("Other weather preserves authored accuracy 70"),
			FBattleMoveWeatherRules::TryResolveAccuracy(Move, Kind, Accuracy)
				&& !Accuracy.bAlwaysHits && Accuracy.BaseAccuracy == 70);
	}

	struct FAccuracyCase
	{
		FConditionId WeatherId;
		bool bSuppressed = false;
		int32 AccuracyDraw = INDEX_NONE;
		int32 ExpectedEffectiveAccuracy = 70;
	};
	const TArray<FAccuracyCase> Cases =
	{
		{FBattleFieldSideConditionRules::GetRainId(), false, INDEX_NONE, 0},
		{FBattleFieldSideConditionRules::GetSunId(), false, 49, 50},
		{FConditionId(), false, 69, 70},
		{FBattleFieldSideConditionRules::GetRainId(), true, 69, 70}
	};
	for (int32 Index = 0; Index < Cases.Num(); ++Index)
	{
		TArray<FBattleExpectedRandomDraw> Draws;
		if (Cases[Index].AccuracyDraw != INDEX_NONE)
		{
			Draws.Add({0, 99, static_cast<uint32>(Cases[Index].AccuracyDraw),
				FBattleEffectExecutor::GetAccuracyRulePurpose()});
		}
		Draws.Add({0, 15, 0, FBattleEffectExecutor::GetDamageRandomRulePurpose()});
		Draws.Add({0, 99, 99, FBattleEffectExecutor::GetSecondaryChanceRulePurpose()});
		TUniquePtr<FBattleEngine> Engine;
		FStrictBattleRandom* Random = nullptr;
		bValid &= TestTrue(FString::Printf(TEXT("Accuracy case %d engine is created"), Index),
			TryMakeEngine(Move, Draws, Engine, Random));
		if (!Engine.IsValid())
		{
			continue;
		}
		if (Cases[Index].WeatherId.IsValid())
		{
			bValid &= TestTrue(FString::Printf(TEXT("Accuracy case %d weather is seeded"), Index),
				TrySeedWeather(*Engine, Cases[Index].WeatherId,
					Cases[Index].bSuppressed,
					EBattleTriggerPhase::BeforeAccuracy));
		}
		FBattleEffectExecutionResult Result;
		EBattleEffectExecutorError Error = EBattleEffectExecutorError::None;
		bValid &= TestTrue(FString::Printf(TEXT("Accuracy case %d hits"), Index),
			TryExecuteDirect(*Engine, Move.Id, Result, Error));
		const FBattleEffectExecutionEvent* AccuracyEvent =
			Result.Events.FindByPredicate(
				[](const FBattleEffectExecutionEvent& Event)
				{
					return Event.Type == EBattleEventType::AccuracyChecked;
				});
		bValid &= TestTrue(FString::Printf(TEXT("Accuracy case %d exposes its exact route"), Index),
			AccuracyEvent != nullptr
				&& AccuracyEvent->NumericBefore.IsSet()
				&& AccuracyEvent->NumericBefore.GetValue()
					== Cases[Index].ExpectedEffectiveAccuracy);
		int32 AccuracyDrawCount = Random != nullptr ? 0 : INDEX_NONE;
		if (Random != nullptr)
		{
			for (const FBattleRandomDraw& Draw : Random->GetTrace())
			{
				AccuracyDrawCount += Draw.RulePurpose
					== FBattleEffectExecutor::GetAccuracyRulePurpose()
					? 1 : 0;
			}
		}
		bValid &= TestEqual(FString::Printf(TEXT("Accuracy case %d has strict draw count"), Index),
			AccuracyDrawCount, Cases[Index].AccuracyDraw == INDEX_NONE ? 0 : 1);
		bValid &= TestTrue(FString::Printf(TEXT("Accuracy case %d consumes only expected RNG"), Index),
			Random != nullptr && Random->IsExact());
		bValid &= TestFalse(FString::Printf(TEXT("Accuracy case %d failed secondary does not paralyze"), Index),
			HasMajorStatus(*Engine, FBattleMajorStatusRules::GetParalysisId()));
	}
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC10WeatherMoveRulesOrderTest,
	"PokemonSolarus.Battle.C05B.C10WeatherMoveRules.Order.FlyReachAccuracySecondaryAndEvents",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC10WeatherMoveRulesOrderTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FBattleMoveDefinition Move = MakeThunderMove();
	const FBattlerId TargetId = MakeNumericId<FBattlerId>(OpponentLeftValue);
	TUniquePtr<FBattleEngine> Miss;
	FStrictBattleRandom* MissRandom = nullptr;
	bool bValid = TestTrue(TEXT("Airborne Sun-miss engine is created"),
		TryMakeEngine(Move,
			{{0, 99, 50, FBattleEffectExecutor::GetAccuracyRulePurpose()}},
			Miss, MissRandom));
	if (Miss.IsValid())
	{
		bValid &= TestTrue(TEXT("The target is made Fly-style airborne"),
			TrySeedActionStartVolatile(
				*Miss, TargetId,
				FBattleVolatileRules::GetFlySemiInvulnerableId()));
		bValid &= TestTrue(TEXT("Sun is registered before accuracy"),
			TrySeedWeather(*Miss, FBattleFieldSideConditionRules::GetSunId()));
		FBattleEffectExecutionResult Result;
		EBattleEffectExecutorError Error = EBattleEffectExecutorError::None;
		bValid &= TestTrue(TEXT("Thunder reaches Fly before taking its Sun accuracy miss"),
			TryExecuteDirect(*Miss, Move.Id, Result, Error));
		bValid &= TestTrue(TEXT("Fly reach then miss has exact public executor events"),
			HasExactExecutionEvents(Result,
				{EBattleEventType::RandomCheck,
				 EBattleEventType::AccuracyChecked,
				 EBattleEventType::Missed}));
		bValid &= TestTrue(TEXT("A miss consumes only the accuracy draw"),
			MissRandom != nullptr && MissRandom->IsExact()
				&& MissRandom->GetTrace().Num() == 1
				&& MissRandom->GetTrace()[0].RulePurpose
					== FBattleEffectExecutor::GetAccuracyRulePurpose());
		bValid &= TestFalse(TEXT("A miss never runs the Paralysis secondary"),
			HasMajorStatus(*Miss, FBattleMajorStatusRules::GetParalysisId()));
	}

	TUniquePtr<FBattleEngine> Hit;
	FStrictBattleRandom* HitRandom = nullptr;
	bValid &= TestTrue(TEXT("Airborne Rain-hit engine is created"),
		TryMakeEngine(Move,
			{{0, 15, 0, FBattleEffectExecutor::GetDamageRandomRulePurpose()},
			 {0, 99, 0, FBattleEffectExecutor::GetSecondaryChanceRulePurpose()}},
			Hit, HitRandom));
	if (Hit.IsValid())
	{
		bValid &= TestTrue(TEXT("Rain-hit target is airborne and Rain is active"),
			TrySeedActionStartVolatile(
				*Hit, TargetId,
				FBattleVolatileRules::GetFlySemiInvulnerableId())
				&& TrySeedWeather(*Hit,
					FBattleFieldSideConditionRules::GetRainId()));
		FBattleEffectExecutionResult Result;
		EBattleEffectExecutorError Error = EBattleEffectExecutorError::None;
		bValid &= TestTrue(TEXT("Rain Thunder reaches Fly, hits, then checks Paralysis"),
			TryExecuteDirect(*Hit, Move.Id, Result, Error));
		bValid &= TestTrue(TEXT("Rain hit has exact executor event order"),
			HasExactExecutionEvents(Result,
				{EBattleEventType::AccuracyChecked,
				 EBattleEventType::CriticalChecked,
				 EBattleEventType::RandomCheck,
				 EBattleEventType::Effectiveness,
				 EBattleEventType::Damage,
				 EBattleEventType::HPChanged,
				 EBattleEventType::RandomCheck,
				 EBattleEventType::StatusChanged}));
		bValid &= TestTrue(TEXT("Hit RNG is damage first and secondary second"),
			HitRandom != nullptr && HitRandom->IsExact()
				&& HitRandom->GetTrace().Num() == 2
				&& HitRandom->GetTrace()[0].RulePurpose
					== FBattleEffectExecutor::GetDamageRandomRulePurpose()
				&& HitRandom->GetTrace()[1].RulePurpose
					== FBattleEffectExecutor::GetSecondaryChanceRulePurpose());
		bValid &= TestTrue(TEXT("The post-hit 30 percent secondary applies Paralysis"),
			HasMajorStatus(*Hit, FBattleMajorStatusRules::GetParalysisId()));
	}

	FBattleMoveDefinition NonReaching = Move;
	NonReaching.Id = MoveId(TEXT("Move.C10R4B.NonReachingWeatherAccuracy"));
	NonReaching.Flags &= ~EBattleMoveFlags::ReachesAirborneSemiInvulnerableTarget;
	TUniquePtr<FBattleEngine> Unreachable;
	FStrictBattleRandom* UnreachableRandom = nullptr;
	bValid &= TestTrue(TEXT("Non-reaching weather-accuracy engine is created"),
		TryMakeEngine(NonReaching, {}, Unreachable, UnreachableRandom));
	if (Unreachable.IsValid())
	{
		bValid &= TestTrue(TEXT("The non-reaching target is airborne in active Sun"),
			TrySeedActionStartVolatile(
				*Unreachable, TargetId,
				FBattleVolatileRules::GetFlySemiInvulnerableId())
				&& TrySeedWeather(*Unreachable,
					FBattleFieldSideConditionRules::GetSunId()));
		FBattleEffectExecutionResult Result;
		EBattleEffectExecutorError Error = EBattleEffectExecutorError::None;
		bValid &= TestTrue(TEXT("Fly reach rejection is a valid no-hit outcome"),
			TryExecuteDirect(*Unreachable, NonReaching.Id, Result, Error));
		bValid &= TestFalse(TEXT("Fly reach rejection occurs before AccuracyChecked"),
			Result.Events.ContainsByPredicate(
				[](const FBattleEffectExecutionEvent& Event)
				{
					return Event.Type == EBattleEventType::AccuracyChecked;
				}));
		bValid &= TestTrue(TEXT("Fly reach rejection consumes no accuracy draw"),
			UnreachableRandom != nullptr && UnreachableRandom->IsExact()
				&& UnreachableRandom->GetTrace().IsEmpty());
	}
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC10WeatherMoveRulesReplayTest,
	"PokemonSolarus.Battle.C05B.C10WeatherMoveRules.Replay.DeterminismNoDuplicationAndCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC10WeatherMoveRulesReplayTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FBattleMoveDefinition Move = MakeThunderMove();
	const TArray<FBattleExpectedRandomDraw> Draws =
	{
		{0, 15, 0, FBattleEffectExecutor::GetDamageRandomRulePurpose()},
		{0, 99, 99, FBattleEffectExecutor::GetSecondaryChanceRulePurpose()}
	};
	TUniquePtr<FBattleEngine> First;
	TUniquePtr<FBattleEngine> Second;
	FStrictBattleRandom* FirstRandom = nullptr;
	FStrictBattleRandom* SecondRandom = nullptr;
	if (!TestTrue(TEXT("First replay engine is created"),
		TryMakeEngine(Move, Draws, First, FirstRandom))
		|| !TestTrue(TEXT("Second replay engine is created"),
			TryMakeEngine(Move, Draws, Second, SecondRandom))
		|| !TestTrue(TEXT("First replay engine receives Rain"),
			TrySeedWeather(*First, FBattleFieldSideConditionRules::GetRainId()))
		|| !TestTrue(TEXT("Second replay engine receives Rain"),
			TrySeedWeather(*Second, FBattleFieldSideConditionRules::GetRainId()))
		|| !TestTrue(TEXT("First replay engine reaches effects"),
			TryPrepareEffectsCheckpoint(*First, Move.Id))
		|| !TestTrue(TEXT("Second replay engine reaches effects"),
			TryPrepareEffectsCheckpoint(*Second, Move.Id)))
	{
		return false;
	}
	const FBattleResolution FirstResult = First->ExecuteCurrentMoveEffects();
	const FBattleResolution SecondResult = Second->ExecuteCurrentMoveEffects();
	bool bValid = TestTrue(TEXT("Both Rain executions are accepted"),
		FirstResult.WasAccepted() && SecondResult.WasAccepted());
	bValid &= TestTrue(TEXT("Both full resolutions preserve exact event order"),
		HasExactResolutionEvents(FirstResult,
			{EBattleEventType::AccuracyChecked,
			 EBattleEventType::CriticalChecked,
			 EBattleEventType::RandomCheck,
			 EBattleEventType::Effectiveness,
			 EBattleEventType::Damage,
			 EBattleEventType::HPChanged,
			 EBattleEventType::RandomCheck,
			 EBattleEventType::ActionCompleted})
			&& HasExactResolutionEvents(SecondResult,
				{EBattleEventType::AccuracyChecked,
				 EBattleEventType::CriticalChecked,
				 EBattleEventType::RandomCheck,
				 EBattleEventType::Effectiveness,
				 EBattleEventType::Damage,
				 EBattleEventType::HPChanged,
				 EBattleEventType::RandomCheck,
				 EBattleEventType::ActionCompleted}));
	bValid &= TestTrue(TEXT("Both runs consume identical exact RNG purposes"),
		FirstRandom != nullptr && SecondRandom != nullptr
			&& FirstRandom->IsExact() && SecondRandom->IsExact()
			&& First->ExportRandomTrace() == Second->ExportRandomTrace());
	bValid &= TestTrue(TEXT("Each returned resolution is published exactly once"),
		IsReturnedResolutionAppendedExactlyOnce(*First, FirstResult)
			&& IsReturnedResolutionAppendedExactlyOnce(*Second, SecondResult));
	bValid &= TestFalse(TEXT("Failed secondary leaves no Paralysis in either run"),
		HasMajorStatus(*First, FBattleMajorStatusRules::GetParalysisId())
			|| HasMajorStatus(*Second, FBattleMajorStatusRules::GetParalysisId()));

	TArray<uint8> FirstBytes;
	TArray<uint8> FirstRepeatBytes;
	TArray<uint8> SecondBytes;
	bValid &= TestTrue(TEXT("Schema-6 replays serialize deterministically"),
		TrySerializeReplay(*First, FirstBytes)
			&& TrySerializeReplay(*First, FirstRepeatBytes)
			&& TrySerializeReplay(*Second, SecondBytes));
	bValid &= TestTrue(TEXT("Repeated and independent replay bytes are identical"),
		FirstBytes == FirstRepeatBytes && FirstBytes == SecondBytes);
	bValid &= TestTrue(TEXT("The public replay schema remains unchanged"),
		First->ExportReplayRecord().GetSchemaVersion() == 6
			&& Second->ExportReplayRecord().GetSchemaVersion() == 6);

	const FBattleMoveDefinition Solar = MakeSolarMove();
	const TArray<FBattleExpectedRandomDraw> SolarDraws =
	{
		{0, 99, 0, FBattleEffectExecutor::GetAccuracyRulePurpose()},
		{0, 15, 0, FBattleEffectExecutor::GetDamageRandomRulePurpose()}
	};
	TUniquePtr<FBattleEngine> SolarFirst;
	TUniquePtr<FBattleEngine> SolarSecond;
	FStrictBattleRandom* SolarFirstRandom = nullptr;
	FStrictBattleRandom* SolarSecondRandom = nullptr;
	bValid &= TestTrue(TEXT("Both Solar replay engines are created"),
		TryMakeEngine(Solar, SolarDraws, SolarFirst, SolarFirstRandom)
			&& TryMakeEngine(Solar, SolarDraws, SolarSecond, SolarSecondRandom));
	if (SolarFirst.IsValid() && SolarSecond.IsValid())
	{
		bValid &= TestTrue(TEXT("Both Solar replay engines receive active Sun"),
			TrySeedWeather(*SolarFirst, FBattleFieldSideConditionRules::GetSunId())
				&& TrySeedWeather(*SolarSecond,
					FBattleFieldSideConditionRules::GetSunId()));
		bValid &= TestTrue(TEXT("Both Solar replay engines reach effects"),
			TryPrepareEffectsCheckpoint(*SolarFirst, Solar.Id)
				&& TryPrepareEffectsCheckpoint(*SolarSecond, Solar.Id));
		const FBattleResolution SolarFirstResult =
			SolarFirst->ExecuteCurrentMoveEffects();
		const FBattleResolution SolarSecondResult =
			SolarSecond->ExecuteCurrentMoveEffects();
		bValid &= TestTrue(TEXT("Both Sun-skipped Solar runs are accepted exactly once"),
			SolarFirstResult.WasAccepted() && SolarSecondResult.WasAccepted()
				&& IsReturnedResolutionAppendedExactlyOnce(
					*SolarFirst, SolarFirstResult)
				&& IsReturnedResolutionAppendedExactlyOnce(
					*SolarSecond, SolarSecondResult));
		bValid &= TestTrue(TEXT("Solar replay leaves no charge setup to clean"),
			!HasVolatile(*SolarFirst, PlayerLeftValue,
				FBattleVolatileRules::GetChargingId())
				&& !HasVolatile(*SolarFirst, PlayerLeftValue,
					FBattleVolatileRules::GetFlySemiInvulnerableId())
				&& !HasVolatile(*SolarSecond, PlayerLeftValue,
					FBattleVolatileRules::GetChargingId())
				&& !HasVolatile(*SolarSecond, PlayerLeftValue,
					FBattleVolatileRules::GetFlySemiInvulnerableId()));
		bValid &= TestTrue(TEXT("Solar replay consumes identical exact RNG"),
			SolarFirstRandom != nullptr && SolarSecondRandom != nullptr
				&& SolarFirstRandom->IsExact() && SolarSecondRandom->IsExact()
				&& SolarFirst->ExportRandomTrace()
					== SolarSecond->ExportRandomTrace());
		TArray<uint8> SolarFirstBytes;
		TArray<uint8> SolarSecondBytes;
		bValid &= TestTrue(TEXT("Solar replay bytes are deterministic"),
			TrySerializeReplay(*SolarFirst, SolarFirstBytes)
				&& TrySerializeReplay(*SolarSecond, SolarSecondBytes)
				&& SolarFirstBytes == SolarSecondBytes);
	}
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC10WeatherMoveRulesAtomicTest,
	"PokemonSolarus.Battle.C05B.C10WeatherMoveRules.Atomic.RollbackRngStateAndPublication",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC10WeatherMoveRulesAtomicTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FBattleMoveDefinition Solar = MakeSolarMove();
	TUniquePtr<FBattleEngine> ChargeFailure;
	FStrictBattleRandom* ChargeRandom = nullptr;
	if (!TestTrue(TEXT("Charge-dispatch failure engine is created"),
		TryMakeEngine(Solar, {}, ChargeFailure, ChargeRandom))
		|| !TestTrue(TEXT("Charge-dispatch failure has active Sun"),
			TrySeedWeather(*ChargeFailure,
				FBattleFieldSideConditionRules::GetSunId(), false,
				EBattleTriggerPhase::BeforeHit)))
	{
		return false;
	}
	FBattleEngineState& ChargeState =
		FBattleC09BWildFlowEngineFixture::GetMutableState(*ChargeFailure);
	ChargeState.NextTriggerReentrancyToken = TNumericLimits<uint64>::Max();
	const uint64 ChargeVersionBefore = ChargeState.StateVersion;
	const int32 ChargeRegistrationCount =
		ChargeState.TriggerFramework.GetActiveRegistrations().Num();
	FBattleEffectExecutionResult ChargeResult;
	EBattleEffectExecutorError Error = EBattleEffectExecutorError::None;
	bool bValid = TestFalse(TEXT("BeforeHit dispatch failure rejects execution"),
		TryExecuteDirect(*ChargeFailure, Solar.Id, ChargeResult, Error));
	bValid &= TestEqual(TEXT("Charge dispatch failure is InvalidHookResult"),
		Error, EBattleEffectExecutorError::InvalidHookResult);
	bValid &= TestTrue(TEXT("Charge dispatch failure mutates no state or trigger facts"),
		ChargeState.StateVersion == ChargeVersionBefore
			&& ChargeState.NextTriggerReentrancyToken
				== TNumericLimits<uint64>::Max()
			&& ChargeState.TriggerFramework.GetActiveRegistrations().Num()
				== ChargeRegistrationCount
			&& !HasVolatile(*ChargeFailure, PlayerLeftValue,
				FBattleVolatileRules::GetChargingId()));
	bValid &= TestTrue(TEXT("Charge dispatch failure commits no RNG"),
		ChargeRandom != nullptr && ChargeRandom->GetTrace().IsEmpty());

	TUniquePtr<FBattleEngine> StaleCharge;
	FStrictBattleRandom* StaleRandom = nullptr;
	bValid &= TestTrue(TEXT("Stale-charge failure engine is created"),
		TryMakeEngine(Solar, {}, StaleCharge, StaleRandom));
	if (StaleCharge.IsValid())
	{
		const FMoveId OtherMove = MakeProbeMove().Id;
		bValid &= TestTrue(TEXT("A mismatched charge payload is seeded"),
			TrySeedActionStartVolatile(
				*StaleCharge,
				MakeNumericId<FBattlerId>(PlayerLeftValue),
				FBattleVolatileRules::GetChargingId(),
				OtherMove.GetDefinitionId()));
		const uint64 BeforeVersion =
			FBattleC09BWildFlowEngineFixture::GetState(*StaleCharge).StateVersion;
		FBattleEffectExecutionResult Result;
		Error = EBattleEffectExecutorError::None;
		bValid &= TestFalse(TEXT("Mismatched live charge rejects without mutation"),
			TryExecuteDirect(*StaleCharge, Solar.Id, Result, Error));
		bValid &= TestEqual(TEXT("Mismatched charge is an invalid hook result"),
			Error, EBattleEffectExecutorError::InvalidHookResult);
		bValid &= TestTrue(TEXT("Mismatched charge remains byte-owned by live state"),
			FBattleC09BWildFlowEngineFixture::GetState(*StaleCharge).StateVersion
				== BeforeVersion
				&& HasVolatile(*StaleCharge, PlayerLeftValue,
					FBattleVolatileRules::GetChargingId())
				&& StaleRandom != nullptr && StaleRandom->GetTrace().IsEmpty());
	}

	const FBattleMoveDefinition Thunder = MakeThunderMove();
	TUniquePtr<FBattleEngine> AccuracyFailure;
	FStrictBattleRandom* AccuracyFailureRandom = nullptr;
	bValid &= TestTrue(TEXT("BeforeAccuracy failure engine is created"),
		TryMakeEngine(Thunder, {}, AccuracyFailure, AccuracyFailureRandom));
	if (AccuracyFailure.IsValid())
	{
		bValid &= TestTrue(TEXT("BeforeAccuracy failure has active Sun"),
			TrySeedWeather(*AccuracyFailure,
				FBattleFieldSideConditionRules::GetSunId()));
		FBattleEngineState& AccuracyState =
			FBattleC09BWildFlowEngineFixture::GetMutableState(*AccuracyFailure);
		AccuracyState.NextTriggerReentrancyToken = TNumericLimits<uint64>::Max();
		const uint64 VersionBefore = AccuracyState.StateVersion;
		const int32 ResolutionCountBefore = AccuracyState.Resolutions.Num();
		const int32 RegistrationCountBefore =
			AccuracyState.TriggerFramework.GetActiveRegistrations().Num();
		FBattleEffectExecutionResult Result;
		Error = EBattleEffectExecutorError::None;
		bValid &= TestFalse(TEXT("BeforeAccuracy dispatch failure rejects execution"),
			TryExecuteDirect(*AccuracyFailure, Thunder.Id, Result, Error));
		bValid &= TestEqual(TEXT("BeforeAccuracy failure uses the hit-resolution path"),
			Error, EBattleEffectExecutorError::HitResolutionFailure);
		bValid &= TestTrue(TEXT("BeforeAccuracy failure rolls back state and publication"),
			AccuracyState.StateVersion == VersionBefore
				&& AccuracyState.NextTriggerReentrancyToken
					== TNumericLimits<uint64>::Max()
				&& AccuracyState.Resolutions.Num() == ResolutionCountBefore
				&& AccuracyState.TriggerFramework.GetActiveRegistrations().Num()
					== RegistrationCountBefore
				&& Result.Events.IsEmpty());
		bValid &= TestTrue(TEXT("BeforeAccuracy failure commits no RNG"),
			AccuracyFailureRandom != nullptr
				&& AccuracyFailureRandom->GetTrace().IsEmpty());
	}
	TUniquePtr<FBattleEngine> LateFailure;
	FStrictBattleRandom* LateRandom = nullptr;
	const TArray<FBattleExpectedRandomDraw> LateDraws =
	{
		{0, 99, 0, FBattleEffectExecutor::GetAccuracyRulePurpose()},
		{0, 15, 0, FBattleEffectExecutor::GetDamageRandomRulePurpose()},
		{0, 99, 99, FBattleEffectExecutor::GetSecondaryChanceRulePurpose()}
	};
	if (!TestTrue(TEXT("Late weather failure engine is created"),
		TryMakeEngine(Thunder, LateDraws, LateFailure, LateRandom))
		|| !TestTrue(TEXT("Late weather failure has active Sun"),
			TrySeedWeather(*LateFailure,
				FBattleFieldSideConditionRules::GetSunId()))
		|| !TestTrue(TEXT("Late weather failure reaches effects"),
			TryPrepareEffectsCheckpoint(*LateFailure, Thunder.Id)))
	{
		return false;
	}
	FBattleC09BWildFlowEngineFixture::GetMutableState(*LateFailure)
		.NextTriggerReentrancyToken = TNumericLimits<uint64>::Max() - 1;
	const FTargetCheckpointObservation Before =
		ObserveTargetCheckpoint(*LateFailure);
	const FBattleResolution Rejected = LateFailure->ExecuteCurrentMoveEffects();
	bValid &= VerifyRejectedTargetCheckpoint(
		*this,
		*LateFailure,
		Before,
		EBattleRejectionReason::CheckpointPreparationFailed,
		Rejected);
	bValid &= TestTrue(TEXT("Staged accuracy RNG is rolled back with late trigger failure"),
		LateRandom != nullptr && LateRandom->GetTrace().IsEmpty()
			&& LateFailure->ExportRandomTrace().IsEmpty());
	bValid &= TestTrue(TEXT("Late failure publishes exactly one rejection and no partial success"),
		IsReturnedResolutionAppendedExactlyOnce(*LateFailure, Rejected));
	return bValid;
}

#endif
