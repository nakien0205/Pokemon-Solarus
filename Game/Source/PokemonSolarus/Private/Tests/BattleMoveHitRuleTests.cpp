#if WITH_DEV_AUTOMATION_TESTS

#include "Battle/BattleDataTableAdapter.h"
#include "Battle/BattleDataTableRows.h"
#include "Battle/BattleMoveHitRules.h"
#include "Battle/BattleReplay.h"
#include "BattleAtomicCheckpointTestCommon.h"
#include "BattleAtomicMoveCheckpointTestSupport.h"
#include "Engine/DataTable.h"
#include "Misc/AutomationTest.h"
#include "UObject/UObjectGlobals.h"

namespace BattleMoveHitRuleTestsPrivate
{
	// Organization decision: keep the seven R4A hit-rule identities and their
	// private fixtures together as one cohesive behavior family.
	using namespace BattleAtomicCheckpointTestCommonPrivate;
	using namespace BattleAtomicMoveCheckpointTestSupportPrivate;

	const TCHAR* const ThunderWaveMoveName = TEXT("Move.C10R4A.ThunderWaveShape");
	const TCHAR* const ConfuseRayMoveName = TEXT("Move.C10R4A.ConfuseRayShape");
	const TCHAR* const PowderMoveName = TEXT("Move.C10R4A.PowderShape");
	const TCHAR* const ToxicMoveAName = TEXT("Move.C10R4A.ToxicShapeA");
	const TCHAR* const ToxicMoveBName = TEXT("Move.C10R4A.ToxicShapeB");
	const TCHAR* const DamageMoveName = TEXT("Move.C10R4A.DamageShape");
	const TCHAR* const GroundGateMoveName = TEXT("Move.C10R4A.GroundGateShape");
	const TCHAR* const AdapterMoveName = TEXT("Move.C10R4A.AdapterShape");

	FMoveId MoveId(const TCHAR* Name)
	{
		return MakeDefinitionId<FMoveId>(Name);
	}

	FBattleMoveDefinition MakeConditionMove(
		const TCHAR* Name,
		const EPokemonType Type,
		const int32 Accuracy,
		const FConditionId ConditionId,
		const EBattleMoveFlags Flags)
	{
		FBattleMoveDefinition Move;
		Move.Id = MoveId(Name);
		Move.Type = Type;
		Move.Category = EBattleMoveCategory::Status;
		Move.bAlwaysHits = false;
		Move.Accuracy = Accuracy;
		Move.BasePP = 20;
		Move.TargetClass = EBattleTargetClass::SelectedOpponent;
		Move.Flags = Flags | EBattleMoveFlags::BlockedByProtect;
		FBattleMoveEffectDescriptor Effect;
		Effect.Kind = EBattleMoveEffectKind::ApplyCondition;
		Effect.Target = EBattleEffectTarget::ResolvedTarget;
		Effect.ConditionId = ConditionId;
		Move.Effects.Add(Effect);
		return Move;
	}

	FBattleMoveDefinition MakeThunderWaveMove()
	{
		return MakeConditionMove(
			ThunderWaveMoveName,
			EPokemonType::Electric,
			90,
			FBattleMajorStatusRules::GetParalysisId(),
			EBattleMoveFlags::RespectsTypeImmunity);
	}

	FBattleMoveDefinition MakeConfuseRayMove()
	{
		return MakeConditionMove(
			ConfuseRayMoveName,
			EPokemonType::Ghost,
			100,
			FBattleVolatileRules::GetConfusionId(),
			EBattleMoveFlags::None);
	}

	FBattleMoveDefinition MakePowderMove()
	{
		return MakeConditionMove(
			PowderMoveName,
			EPokemonType::Grass,
			75,
			FBattleMajorStatusRules::GetSleepId(),
			EBattleMoveFlags::Powder);
	}

	FBattleMoveDefinition MakeToxicMove(const TCHAR* Name, const int32 Accuracy = 90)
	{
		return MakeConditionMove(
			Name,
			EPokemonType::Poison,
			Accuracy,
			FBattleMajorStatusRules::GetToxicId(),
			EBattleMoveFlags::PoisonTypeUserBypassesSemiInvulnerabilityAndAccuracy);
	}

	FBattleMoveDefinition MakeDamageMove()
	{
		FBattleMoveDefinition Move;
		Move.Id = MoveId(DamageMoveName);
		Move.Type = EPokemonType::Electric;
		Move.Category = EBattleMoveCategory::Special;
		Move.Power = 40;
		Move.Accuracy = 100;
		Move.BasePP = 20;
		Move.TargetClass = EBattleTargetClass::SelectedOpponent;
		Move.Flags = EBattleMoveFlags::NeverCritical | EBattleMoveFlags::BlockedByProtect;
		FBattleMoveEffectDescriptor Effect;
		Effect.Kind = EBattleMoveEffectKind::Damage;
		Effect.Target = EBattleEffectTarget::ResolvedTarget;
		Move.Effects.Add(Effect);
		return Move;
	}

	FBattleMoveDefinition MakeGroundGateMove()
	{
		return MakeConditionMove(
			GroundGateMoveName,
			EPokemonType::Ground,
			80,
			FBattleMajorStatusRules::GetPoisonId(),
			EBattleMoveFlags::RespectsTypeImmunity);
	}

	TArray<FBattleTypeChartEntry> MakeHitRuleTypeChart()
	{
		TArray<FBattleTypeChartEntry> Entries = MakeNeutralTypeChart();
		for (FBattleTypeChartEntry& Entry : Entries)
		{
			if (Entry.AttackingType == EPokemonType::Electric
				&& Entry.DefendingType == EPokemonType::Ground)
			{
				Entry.Numerator = 0;
				Entry.Denominator = 1;
			}
		}
		return Entries;
	}

	bool TryMakeCatalog(
		const FBattleMoveDefinition& Move,
		const EPokemonType UserPrimary,
		const EPokemonType UserSecondary,
		const EPokemonType TargetPrimary,
		const EPokemonType TargetSecondary,
		FBattleDefinitionCatalog& OutCatalog)
	{
		FBattleDefinitionCatalogInput Input;
		Input.TypeChartEntries = MakeHitRuleTypeChart();
		Input.Moves = {MakeProbeMove(), MakeTargetProbeMove(), Move};
		Input.Abilities =
		{
			{FBattleAbilityRules::GetBlazeId()},
			{FBattleAbilityRules::GetIntimidateId()},
			{FBattleAbilityRules::GetMagicGuardId()},
			{FBattleAbilityRules::GetLevitateId()}
		};
		Input.Items.Add({FBattleItemRules::GetAirBalloonId(), EBattleItemKind::Held});
		for (const FConditionId& Id : FBattleVolatileRules::GetCanonicalIds())
		{
			Input.Conditions.Add({Id, EBattleConditionKind::Volatile});
		}
		for (const FConditionId& Id : FBattleMajorStatusRules::GetCanonicalIds())
		{
			Input.Conditions.Add({Id, EBattleConditionKind::MajorStatus});
		}

		FBattleSpeciesFormDefinition User = MakeSpecies(PlayerSpeciesName);
		User.PrimaryType = UserPrimary;
		User.SecondaryType = UserSecondary;
		User.AbilityChoices.AddUnique(FBattleAbilityRules::GetLevitateId());
		Input.SpeciesForms.Add(MoveTemp(User));
		FBattleSpeciesFormDefinition Target = MakeSpecies(WildSpeciesName);
		Target.PrimaryType = TargetPrimary;
		Target.SecondaryType = TargetSecondary;
		Target.AbilityChoices.AddUnique(FBattleAbilityRules::GetLevitateId());
		Input.SpeciesForms.Add(MoveTemp(Target));

		TArray<FBattleCatalogDiagnostic> Diagnostics;
		return FBattleDefinitionCatalog::TryCreate(Input, OutCatalog, Diagnostics)
			&& Diagnostics.IsEmpty();
	}

	bool TryMakeEngine(
		const FBattleMoveDefinition& Move,
		const EPokemonType UserPrimary,
		const EPokemonType UserSecondary,
		const EPokemonType TargetPrimary,
		const EPokemonType TargetSecondary,
		const FAbilityId TargetAbility,
		const FItemId TargetItem,
		TArray<FBattleExpectedRandomDraw> ExpectedDraws,
		TUniquePtr<FBattleEngine>& OutEngine,
		FStrictBattleRandom*& OutRandom)
	{
		OutEngine.Reset();
		OutRandom = nullptr;
		FAtomicWildScenario Scenario = MakePreMoveScenario(Move.Id);
		FBattleSetupInput Input = MakeSetupInput(Scenario);
		for (FBattlePartyEntrySetup& Entry : Input.PartyEntries)
		{
			if (Entry.BattlerId == MakeNumericId<FBattlerId>(OpponentLeftValue))
			{
				Entry.AbilityId = TargetAbility.IsValid()
					? TargetAbility
					: FBattleAbilityRules::GetBlazeId();
				Entry.OriginalHeldItemId = TargetItem;
				Entry.CurrentHeldItemId = TargetItem;
			}
		}
		FBattleSetup Setup;
		EBattleSetupValidationError SetupError = EBattleSetupValidationError::None;
		FBattleDefinitionCatalog Catalog;
		if (!FBattleSetup::TryCreate(Input, Setup, SetupError)
			|| !TryMakeCatalog(
				Move,
				UserPrimary,
				UserSecondary,
				TargetPrimary,
				TargetSecondary,
				Catalog))
		{
			return false;
		}
		TUniquePtr<FStrictBattleRandom> Random =
			MakeUnique<FStrictBattleRandom>(MoveTemp(ExpectedDraws));
		OutRandom = Random.Get();
		FBattleRejection Rejection;
		return FBattleEngine::TryCreate(
			Setup,
			Catalog,
			MoveTemp(Random),
			OutEngine,
			Rejection);
	}

	bool TryExecuteDirect(
		FBattleEngine& Engine,
		const FMoveId Move,
		FBattleEffectExecutionResult& OutResult,
		EBattleEffectExecutorError& OutError)
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
		if (Definition == nullptr || User == nullptr || Target == nullptr)
		{
			return false;
		}
		FBattleBattlerTarget BattlerTarget = {Target->ActiveSlotId, TargetId};
		FBattleResolvedTarget ResolvedTarget;
		if (!FBattleResolvedTarget::TryCreateBattler(BattlerTarget, ResolvedTarget))
		{
			return false;
		}
		FBattleEffectExecutionRequest Request;
		Request.BattleId = State.Setup.GetBattleId();
		Request.TurnId = State.TurnId;
		Request.ActionId = MakeNumericId<FActionId>(900);
		Request.ResolutionId = MakeNumericId<FResolutionId>(900);
		Request.UserBattlerId = UserId;
		Request.UserSlotId = User->ActiveSlotId;
		Request.Move = Definition;
		Request.Targets.Add(ResolvedTarget);
		return FBattleEffectExecutor::TryExecuteAgainstState(
			Request,
			State,
			OutResult,
			OutError);
	}

	bool TrySeedProtect(FBattleEngine& Engine, const FBattlerId BattlerId)
	{
		FBattleEngineState& State = FBattleC09BWildFlowEngineFixture::GetMutableState(Engine);
		FBattleBattlerState* Battler = State.FindMutableBattler(BattlerId);
		FBattleTriggerSubject Owner;
		if (Battler == nullptr || !FBattleTriggerSubject::TryCreateBattler(BattlerId, Owner))
		{
			return false;
		}
		FBattleVolatileTriggerRegistrationFacts Facts;
		Facts.VolatileId = FBattleVolatileRules::GetProtectId();
		Facts.PayloadId = Facts.VolatileId.GetDefinitionId();
		Facts.Owner = Facts.Source = Owner;
		Facts.Targets.Add(Owner);
		Facts.Layers = FBattleVolatileRules::GetProtectInitialChainCounter();
		EBattleTriggerError TriggerError = EBattleTriggerError::None;
		if (!FBattleVolatileRules::TryRegisterTriggers(State.TriggerFramework, Facts, TriggerError))
		{
			return false;
		}
		FBattleConditionState& Condition = Battler->Volatiles.AddDefaulted_GetRef();
		Condition.ConditionId = Facts.VolatileId;
		Condition.LayerCount = Facts.Layers;
		Condition.CreationOrdinal = State.NextConditionCreationOrdinal++;
		Condition.SourceBattlerId = BattlerId;
		TArray<FBattleTriggerLifecycleFact> IgnoredFacts;
		State.TriggerFramework.DrainLifecycleFacts(IgnoredFacts);
		return true;
	}

	bool TryPrepareEffectsCheckpoint(FBattleEngine& Engine, const FMoveId Move)
	{
		return TryPrepareTargetCheckpoint(Engine, Move)
			&& Engine.ResolveCurrentMoveTargets().WasAccepted();
	}

	const FBattleBattlerState* FindTarget(const FBattleEngine* Engine)
	{
		return Engine != nullptr
			? FBattleC09BWildFlowEngineFixture::GetState(*Engine).FindBattler(
				MakeNumericId<FBattlerId>(OpponentLeftValue))
			: nullptr;
	}

	bool HasMajorStatus(const FBattleEngine* Engine, const FConditionId Id)
	{
		const FBattleBattlerState* Target = FindTarget(Engine);
		return Target != nullptr && Target->MajorStatusId == Id;
	}

	bool HasNoMajorStatus(const FBattleEngine* Engine)
	{
		const FBattleBattlerState* Target = FindTarget(Engine);
		return Target != nullptr && !Target->MajorStatusId.IsValid();
	}

	bool HasVolatile(const FBattleEngine* Engine, const FConditionId Id)
	{
		const FBattleBattlerState* Target = FindTarget(Engine);
		return Target != nullptr && Target->Volatiles.ContainsByPredicate(
			[Id](const FBattleConditionState& Condition)
			{
				return Condition.ConditionId == Id;
			});
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

	bool HaveSameExecutionSemantics(
		const FBattleEffectExecutionResult& Left,
		const FBattleEffectExecutionResult& Right)
	{
		if (Left.bValid != Right.bValid
			|| Left.TotalActualDamage != Right.TotalActualDamage
			|| Left.CompletedHitsPerDamageTarget != Right.CompletedHitsPerDamageTarget
			|| Left.Events.Num() != Right.Events.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < Left.Events.Num(); ++Index)
		{
			const FBattleEffectExecutionEvent& L = Left.Events[Index];
			const FBattleEffectExecutionEvent& R = Right.Events[Index];
			if (L.Type != R.Type
				|| L.Cause != R.Cause
				|| L.Outcome != R.Outcome
				|| L.NumericBefore != R.NumericBefore
				|| L.NumericAfter != R.NumericAfter
				|| L.NumericDelta != R.NumericDelta)
			{
				return false;
			}
		}
		return true;
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

	bool TryBuildAdapterCatalog(FBattleDefinitionCatalog& OutCatalog)
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
		Row.Type = FName(TEXT("Poison"));
		Row.Category = FName(TEXT("Status"));
		Row.Accuracy = 73;
		Row.BasePP = 20;
		Row.TargetClass = FName(TEXT("SelectedOpponent"));
		Row.Flags =
		{
			FName(TEXT("RespectsTypeImmunity")),
			FName(TEXT("Powder")),
			FName(TEXT("PoisonTypeUserBypassesSemiInvulnerabilityAndAccuracy"))
		};
		FBattleMoveEffectTableRow Effect;
		Effect.Kind = FName(TEXT("ApplyCondition"));
		Effect.Target = FName(TEXT("ResolvedTarget"));
		Effect.ConditionId = FBattleMajorStatusRules::GetToxicId()
			.GetDefinitionId().GetName();
		Row.Effects.Add(Effect);
		Moves->AddRow(FName(AdapterMoveName), Row);

		FBattleConditionTableRow Toxic;
		Toxic.Kind = FName(TEXT("MajorStatus"));
		Conditions->AddRow(
			FBattleMajorStatusRules::GetToxicId().GetDefinitionId().GetName(),
			Toxic);
		for (int32 Attack = 0; Attack < FBattleTypeChart::TypeCount; ++Attack)
		{
			FBattleTypeChartTableRow TypeRow;
			for (int32 Defense = 0; Defense < FBattleTypeChart::TypeCount; ++Defense)
			{
				TypeRow.Entries.Add({PokemonTypeName(Defense), 1, 1});
			}
			TypeChart->AddRow(PokemonTypeName(Attack), TypeRow);
		}

		TArray<FBattleCatalogDiagnostic> Diagnostics;
		return FBattleDataTableAdapter::BuildCatalog(Tables, OutCatalog, Diagnostics)
			&& Diagnostics.IsEmpty();
	}

	bool TrySerializeReplay(FBattleEngine& Engine, TArray<uint8>& OutBytes)
	{
		FBattleRejection Rejection;
		return FBattleReplaySerializer::TrySerializeCanonical(
			Engine.ExportReplayRecord(),
			OutBytes,
			Rejection);
	}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC10HitRulesContractTest,
	"PokemonSolarus.Battle.C05B.C10HitRules.Contract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC10HitRulesContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	bool bValid = TestEqual(TEXT("Status immunity uses bit 16"),
		static_cast<uint32>(EBattleMoveFlags::RespectsTypeImmunity), 1U << 16);
	bValid &= TestEqual(TEXT("Powder uses bit 17"),
		static_cast<uint32>(EBattleMoveFlags::Powder), 1U << 17);
	bValid &= TestEqual(TEXT("Poison-user bypass uses bit 18"),
		static_cast<uint32>(
			EBattleMoveFlags::PoisonTypeUserBypassesSemiInvulnerabilityAndAccuracy),
		1U << 18);

	EBattleMoveHitRuleValidationError Error = EBattleMoveHitRuleValidationError::None;
	FBattleMoveDefinition Toxic73 = MakeToxicMove(ToxicMoveAName, 73);
	bValid &= TestTrue(TEXT("The Poison-user trait accepts ordinary numeric accuracy"),
		FBattleMoveHitRules::TryValidateMoveDefinition(Toxic73, Error));
	FBattleMoveUserHitQualifiers Qualifiers;
	bValid &= TestTrue(TEXT("A primary Poison type resolves qualifiers"),
		FBattleMoveHitRules::TryResolveUserHitQualifiers(
			Toxic73, EPokemonType::Poison, EPokemonType::Invalid, Qualifiers));
	bValid &= TestTrue(TEXT("A primary Poison type receives both bypasses"),
		Qualifiers.bBypassAccuracy && Qualifiers.bBypassSemiInvulnerability);
	bValid &= TestTrue(TEXT("A secondary Poison type resolves qualifiers"),
		FBattleMoveHitRules::TryResolveUserHitQualifiers(
			Toxic73, EPokemonType::Normal, EPokemonType::Poison, Qualifiers));
	bValid &= TestTrue(TEXT("A secondary Poison type receives both bypasses"),
		Qualifiers.bBypassAccuracy && Qualifiers.bBypassSemiInvulnerability);
	bValid &= TestTrue(TEXT("A non-Poison type resolves without bypasses"),
		FBattleMoveHitRules::TryResolveUserHitQualifiers(
			Toxic73, EPokemonType::Normal, EPokemonType::Invalid, Qualifiers)
			&& !Qualifiers.bBypassAccuracy
			&& !Qualifiers.bBypassSemiInvulnerability);

	FBattleMoveDefinition InvalidStatusTrait = MakeDamageMove();
	InvalidStatusTrait.Flags |= EBattleMoveFlags::RespectsTypeImmunity;
	bValid &= TestFalse(TEXT("A damaging move cannot opt into status type immunity"),
		FBattleMoveHitRules::TryValidateMoveDefinition(InvalidStatusTrait, Error));
	bValid &= TestEqual(TEXT("The status-only violation is typed"), Error,
		EBattleMoveHitRuleValidationError::StatusTypeImmunityRequiresStatusMove);
	FBattleMoveDefinition InvalidPoisonTrait = Toxic73;
	InvalidPoisonTrait.Type = EPokemonType::Fire;
	bValid &= TestFalse(TEXT("The Poison-user trait requires a Poison status move"),
		FBattleMoveHitRules::TryValidateMoveDefinition(InvalidPoisonTrait, Error));
	bValid &= TestEqual(TEXT("The Poison move-shape violation is typed"), Error,
		EBattleMoveHitRuleValidationError::PoisonUserBypassRequiresPoisonStatusMove);
	InvalidPoisonTrait = Toxic73;
	InvalidPoisonTrait.bAlwaysHits = true;
	InvalidPoisonTrait.Accuracy = 0;
	bValid &= TestFalse(TEXT("The Poison-user trait requires numeric accuracy"),
		FBattleMoveHitRules::TryValidateMoveDefinition(InvalidPoisonTrait, Error));
	bValid &= TestEqual(TEXT("The numeric-accuracy violation is typed"), Error,
		EBattleMoveHitRuleValidationError::PoisonUserBypassRequiresNumericAccuracy);
	FBattleMoveDefinition InvalidTargetTrait = MakeThunderWaveMove();
	InvalidTargetTrait.TargetClass = EBattleTargetClass::Field;
	bValid &= TestFalse(TEXT("An R4A hit trait requires a battler target"),
		FBattleMoveHitRules::TryValidateMoveDefinition(InvalidTargetTrait, Error));
	bValid &= TestEqual(TEXT("The battler-target violation is typed"), Error,
		EBattleMoveHitRuleValidationError::RequiresBattlerTarget);
	FBattleMoveDefinition GenericPowder = MakeDamageMove();
	GenericPowder.Flags |= EBattleMoveFlags::Powder;
	bValid &= TestTrue(TEXT("Powder remains a generic authored move trait"),
		FBattleMoveHitRules::TryValidateMoveDefinition(GenericPowder, Error));

	FBattleTypeChart TypeChart;
	EBattleTypeChartValidationError ChartError = EBattleTypeChartValidationError::None;
	bValid &= TestTrue(TEXT("The exact test type chart is valid"),
		FBattleTypeChart::TryCreate(MakeHitRuleTypeChart(), TypeChart, ChartError));
	FBattleMoveDefinition Ordered = MakeThunderWaveMove();
	Ordered.Flags |= EBattleMoveFlags::Powder;
	FBattleMoveHitImmunityResult Immunity;
	bValid &= TestTrue(TEXT("Combined move immunity resolves"),
		FBattleMoveHitRules::TryResolveMoveImmunity(
			Ordered,
			EPokemonType::Ground,
			EPokemonType::Grass,
			TypeChart,
			Immunity));
	bValid &= TestEqual(TEXT("Type-chart immunity precedes Powder immunity"),
		Immunity.Reason, EBattleMoveHitImmunityReason::TypeChart);

	FBattleDefinitionCatalog Adapted;
	bValid &= TestTrue(TEXT("The adapter accepts all three exact authored names"),
		TryBuildAdapterCatalog(Adapted));
	const FBattleMoveDefinition* AdaptedMove = Adapted.FindMove(MoveId(AdapterMoveName));
	bValid &= TestTrue(TEXT("The adapter preserves all three new flags"),
		AdaptedMove != nullptr
			&& EnumHasAllFlags(AdaptedMove->Flags, EBattleMoveFlags::RespectsTypeImmunity)
			&& EnumHasAllFlags(AdaptedMove->Flags, EBattleMoveFlags::Powder)
			&& EnumHasAllFlags(
				AdaptedMove->Flags,
				EBattleMoveFlags::PoisonTypeUserBypassesSemiInvulnerabilityAndAccuracy));

	FBattleDefinitionCatalogInput InvalidInput;
	InvalidInput.TypeChartEntries = MakeHitRuleTypeChart();
	InvalidInput.Moves.Add(InvalidStatusTrait);
	FBattleDefinitionCatalog Rejected;
	TArray<FBattleCatalogDiagnostic> Diagnostics;
	bValid &= TestFalse(TEXT("Catalog validation rejects the incompatible status trait"),
		FBattleDefinitionCatalog::TryCreate(InvalidInput, Rejected, Diagnostics));
	bValid &= TestTrue(TEXT("Catalog validation identifies Flags"),
		Diagnostics.ContainsByPredicate(
			[](const FBattleCatalogDiagnostic& Diagnostic)
			{
				return Diagnostic.Code == EBattleCatalogDiagnosticCode::IncompatibleEffect
					&& Diagnostic.Field == FName(TEXT("Flags"));
			}));
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC10HitRulesStatusTypeImmunityTest,
	"PokemonSolarus.Battle.C05B.C10HitRules.StatusTypeImmunity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC10HitRulesStatusTypeImmunityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	bool bValid = true;
	TUniquePtr<FBattleEngine> ThunderEngine;
	FStrictBattleRandom* ThunderRandom = nullptr;
	FBattleEffectExecutionResult ThunderResult;
	EBattleEffectExecutorError Error = EBattleEffectExecutorError::None;
	bValid &= TestTrue(TEXT("The Thunder-Wave-shaped engine is created"),
		TryMakeEngine(MakeThunderWaveMove(), EPokemonType::Normal, EPokemonType::Invalid,
			EPokemonType::Ground, EPokemonType::Invalid, FAbilityId(), FItemId(), {},
			ThunderEngine, ThunderRandom));
	bValid &= TestTrue(TEXT("Ground immunity resolves before accuracy"),
		ThunderEngine.IsValid()
			&& TryExecuteDirect(*ThunderEngine, MoveId(ThunderWaveMoveName), ThunderResult, Error));
	bValid &= TestTrue(TEXT("Ground immunity emits exactly Immunity"),
		HasExactExecutionEvents(ThunderResult, {EBattleEventType::Immunity}));
	bValid &= TestTrue(TEXT("Ground immunity consumes no accuracy or status RNG"),
		ThunderRandom != nullptr && ThunderRandom->IsExact()
			&& ThunderRandom->GetTrace().IsEmpty());
	bValid &= TestTrue(TEXT("Ground immunity applies no Paralysis"),
		HasNoMajorStatus(ThunderEngine.Get()));

	TUniquePtr<FBattleEngine> ConfuseEngine;
	FStrictBattleRandom* ConfuseRandom = nullptr;
	FBattleEffectExecutionResult ConfuseResult;
	bValid &= TestTrue(TEXT("The unflagged Confuse-Ray-shaped engine is created"),
		TryMakeEngine(MakeConfuseRayMove(), EPokemonType::Normal, EPokemonType::Invalid,
			EPokemonType::Ground, EPokemonType::Invalid, FAbilityId(), FItemId(),
			{{0, 99, 0, FBattleEffectExecutor::GetAccuracyRulePurpose()},
			 {2, 5, 2, FBattleVolatileRules::GetConfusionDurationPurpose()}},
			ConfuseEngine, ConfuseRandom));
	bValid &= TestTrue(TEXT("The unflagged status move remains executable"),
		ConfuseEngine.IsValid()
			&& TryExecuteDirect(*ConfuseEngine, MoveId(ConfuseRayMoveName), ConfuseResult, Error));
	bValid &= TestTrue(TEXT("Confuse Ray keeps exact ordinary events"),
		HasExactExecutionEvents(ConfuseResult,
			{EBattleEventType::RandomCheck, EBattleEventType::AccuracyChecked,
			 EBattleEventType::RandomCheck, EBattleEventType::StatusChanged}));
	bValid &= TestTrue(TEXT("Confuse Ray keeps accuracy and duration RNG"),
		ConfuseRandom != nullptr && ConfuseRandom->IsExact()
			&& ConfuseRandom->GetTrace().Num() == 2);
	bValid &= TestTrue(TEXT("Confuse Ray still applies Confusion"),
		HasVolatile(ConfuseEngine.Get(), FBattleVolatileRules::GetConfusionId()));

	TUniquePtr<FBattleEngine> DamageEngine;
	FStrictBattleRandom* DamageRandom = nullptr;
	FBattleEffectExecutionResult DamageResult;
	bValid &= TestTrue(TEXT("The damaging-immunity engine is created"),
		TryMakeEngine(MakeDamageMove(), EPokemonType::Normal, EPokemonType::Invalid,
			EPokemonType::Ground, EPokemonType::Invalid, FAbilityId(), FItemId(), {},
			DamageEngine, DamageRandom));
	bValid &= TestTrue(TEXT("Existing damaging type immunity still resolves"),
		DamageEngine.IsValid()
			&& TryExecuteDirect(*DamageEngine, MoveId(DamageMoveName), DamageResult, Error));
	bValid &= TestTrue(TEXT("Damaging immunity remains one existing gate event"),
		HasExactExecutionEvents(DamageResult, {EBattleEventType::Immunity}));
	bValid &= TestTrue(TEXT("Damaging immunity remains pre-accuracy"),
		DamageRandom != nullptr && DamageRandom->IsExact()
			&& DamageRandom->GetTrace().IsEmpty());
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC10HitRulesPowderTest,
	"PokemonSolarus.Battle.C05B.C10HitRules.Powder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC10HitRulesPowderTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	bool bValid = true;
	for (const TPair<EPokemonType, EPokemonType>& Types :
		TArray<TPair<EPokemonType, EPokemonType>>{
			{EPokemonType::Grass, EPokemonType::Invalid},
			{EPokemonType::Normal, EPokemonType::Grass}})
	{
		TUniquePtr<FBattleEngine> Engine;
		FStrictBattleRandom* Random = nullptr;
		FBattleEffectExecutionResult Result;
		EBattleEffectExecutorError Error = EBattleEffectExecutorError::None;
		bValid &= TestTrue(TEXT("A Grass-type Powder target engine is created"),
			TryMakeEngine(MakePowderMove(), EPokemonType::Normal, EPokemonType::Invalid,
				Types.Key, Types.Value, FAbilityId(), FItemId(), {}, Engine, Random));
		bValid &= TestTrue(TEXT("Either Grass target type blocks Powder"),
			Engine.IsValid()
				&& TryExecuteDirect(*Engine, MoveId(PowderMoveName), Result, Error)
				&& HasExactExecutionEvents(Result, {EBattleEventType::Immunity}));
		bValid &= TestTrue(TEXT("Grass Powder immunity consumes no RNG"),
			Random != nullptr && Random->IsExact() && Random->GetTrace().IsEmpty());
		bValid &= TestTrue(TEXT("Grass Powder immunity applies no status"),
			HasNoMajorStatus(Engine.Get()));
	}

	TUniquePtr<FBattleEngine> Ordinary;
	FStrictBattleRandom* OrdinaryRandom = nullptr;
	FBattleEffectExecutionResult OrdinaryResult;
	EBattleEffectExecutorError Error = EBattleEffectExecutorError::None;
	bValid &= TestTrue(TEXT("A non-Grass Powder target engine is created"),
		TryMakeEngine(MakePowderMove(), EPokemonType::Normal, EPokemonType::Invalid,
			EPokemonType::Water, EPokemonType::Invalid, FAbilityId(), FItemId(),
			{{0, 99, 74, FBattleEffectExecutor::GetAccuracyRulePurpose()},
			 {2, 4, 2, FBattleMajorStatusRules::GetSleepDurationPurpose()}},
			Ordinary, OrdinaryRandom));
	bValid &= TestTrue(TEXT("A non-Grass target retains ordinary Powder behavior"),
		Ordinary.IsValid()
			&& TryExecuteDirect(*Ordinary, MoveId(PowderMoveName), OrdinaryResult, Error));
	bValid &= TestTrue(TEXT("Non-Grass Powder keeps exact events"),
		HasExactExecutionEvents(OrdinaryResult,
			{EBattleEventType::RandomCheck, EBattleEventType::AccuracyChecked,
			 EBattleEventType::StatusChanged}));
	bValid &= TestTrue(TEXT("Non-Grass Powder consumes accuracy then status RNG"),
		OrdinaryRandom != nullptr && OrdinaryRandom->IsExact()
			&& OrdinaryRandom->GetTrace().Num() == 2);
	bValid &= TestTrue(TEXT("Powder hits at the lower 75-accuracy boundary and applies Sleep"),
		HasMajorStatus(Ordinary.Get(), FBattleMajorStatusRules::GetSleepId()));

	TUniquePtr<FBattleEngine> Miss;
	FStrictBattleRandom* MissRandom = nullptr;
	FBattleEffectExecutionResult MissResult;
	bValid &= TestTrue(TEXT("A non-Grass Powder boundary-miss engine is created"),
		TryMakeEngine(MakePowderMove(), EPokemonType::Normal, EPokemonType::Invalid,
			EPokemonType::Water, EPokemonType::Invalid, FAbilityId(), FItemId(),
			{{0, 99, 75, FBattleEffectExecutor::GetAccuracyRulePurpose()}},
			Miss, MissRandom));
	bValid &= TestTrue(TEXT("Powder misses at the upper 75-accuracy boundary"),
		Miss.IsValid()
			&& TryExecuteDirect(*Miss, MoveId(PowderMoveName), MissResult, Error));
	bValid &= TestTrue(TEXT("The Powder boundary miss keeps exact events"),
		HasExactExecutionEvents(MissResult,
			{EBattleEventType::RandomCheck, EBattleEventType::AccuracyChecked,
			 EBattleEventType::Missed}));
	bValid &= TestTrue(TEXT("The Powder boundary miss consumes exactly one accuracy draw"),
		MissRandom != nullptr && MissRandom->IsExact()
			&& MissRandom->GetTrace().Num() == 1);
	bValid &= TestTrue(TEXT("The Powder boundary miss applies no status"),
		HasNoMajorStatus(Miss.Get()));
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC10HitRulesPoisonTypeUserBypassTest,
	"PokemonSolarus.Battle.C05B.C10HitRules.PoisonTypeUserBypass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC10HitRulesPoisonTypeUserBypassTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	bool bValid = true;
	FBattleEffectExecutionResult SyntheticResults[2];
	TArray<FBattleRandomDraw> SyntheticTraces[2];
	const TCHAR* SyntheticNames[] = {ToxicMoveAName, ToxicMoveBName};
	for (int32 Index = 0; Index < 2; ++Index)
	{
		TUniquePtr<FBattleEngine> Engine;
		FStrictBattleRandom* Random = nullptr;
		EBattleEffectExecutorError SyntheticError = EBattleEffectExecutorError::None;
		bValid &= TestTrue(TEXT("A synthetic Poison-primary Fly engine is created"),
			TryMakeEngine(MakeToxicMove(SyntheticNames[Index]),
				EPokemonType::Poison, EPokemonType::Invalid,
				EPokemonType::Normal, EPokemonType::Invalid,
				FAbilityId(), FItemId(), {}, Engine, Random));
		bValid &= TestTrue(TEXT("A Poison-primary user bypasses Fly and accuracy"),
			Engine.IsValid()
				&& TrySeedActionStartVolatile(*Engine,
					MakeNumericId<FBattlerId>(OpponentLeftValue),
					FBattleVolatileRules::GetFlySemiInvulnerableId())
				&& TryExecuteDirect(*Engine, MoveId(SyntheticNames[Index]),
					SyntheticResults[Index], SyntheticError));
		bValid &= TestTrue(TEXT("Poison-primary bypass emits no reachability or RNG event"),
			HasExactExecutionEvents(SyntheticResults[Index],
				{EBattleEventType::AccuracyChecked, EBattleEventType::StatusChanged}));
		bValid &= TestTrue(TEXT("Poison-primary bypass consumes no RNG"),
			Random != nullptr && Random->IsExact() && Random->GetTrace().IsEmpty());
		bValid &= TestTrue(TEXT("Poison-primary bypass applies Toxic"),
			HasMajorStatus(Engine.Get(), FBattleMajorStatusRules::GetToxicId()));
		if (Engine.IsValid())
		{
			SyntheticTraces[Index] = Engine->ExportRandomTrace();
		}
	}
	bValid &= TestTrue(TEXT("Two synthetic IDs with identical traits behave identically"),
		HaveSameExecutionSemantics(SyntheticResults[0], SyntheticResults[1])
			&& SyntheticTraces[0] == SyntheticTraces[1]);

	TUniquePtr<FBattleEngine> Secondary;
	FStrictBattleRandom* SecondaryRandom = nullptr;
	FBattleEffectExecutionResult SecondaryResult;
	EBattleEffectExecutorError Error = EBattleEffectExecutorError::None;
	bValid &= TestTrue(TEXT("A Poison-secondary Fly engine is created"),
		TryMakeEngine(MakeToxicMove(ToxicMoveAName),
			EPokemonType::Normal, EPokemonType::Poison,
			EPokemonType::Normal, EPokemonType::Invalid,
			FAbilityId(), FItemId(), {}, Secondary, SecondaryRandom));
	bValid &= TestTrue(TEXT("A Poison-secondary user bypasses Fly and accuracy"),
		Secondary.IsValid()
			&& TrySeedActionStartVolatile(*Secondary,
				MakeNumericId<FBattlerId>(OpponentLeftValue),
				FBattleVolatileRules::GetFlySemiInvulnerableId())
			&& TryExecuteDirect(*Secondary, MoveId(ToxicMoveAName), SecondaryResult, Error));
	bValid &= TestTrue(TEXT("Poison-secondary bypass keeps exact events"),
		HasExactExecutionEvents(SecondaryResult,
			{EBattleEventType::AccuracyChecked, EBattleEventType::StatusChanged}));
	bValid &= TestTrue(TEXT("Poison-secondary bypass consumes no RNG"),
		SecondaryRandom != nullptr && SecondaryRandom->IsExact()
			&& SecondaryRandom->GetTrace().IsEmpty());
	bValid &= TestTrue(TEXT("Poison-secondary bypass applies Toxic"),
		HasMajorStatus(Secondary.Get(), FBattleMajorStatusRules::GetToxicId()));

	TUniquePtr<FBattleEngine> Ordinary;
	FStrictBattleRandom* OrdinaryRandom = nullptr;
	FBattleEffectExecutionResult OrdinaryResult;
	bValid &= TestTrue(TEXT("A non-Poison ordinary engine is created"),
		TryMakeEngine(MakeToxicMove(ToxicMoveAName),
			EPokemonType::Normal, EPokemonType::Invalid,
			EPokemonType::Normal, EPokemonType::Invalid,
			FAbilityId(), FItemId(),
			{{0, 99, 89, FBattleEffectExecutor::GetAccuracyRulePurpose()}},
			Ordinary, OrdinaryRandom));
	bValid &= TestTrue(TEXT("A non-Poison user retains numeric accuracy"),
		Ordinary.IsValid()
			&& TryExecuteDirect(*Ordinary, MoveId(ToxicMoveAName), OrdinaryResult, Error));
	bValid &= TestTrue(TEXT("Non-Poison use keeps exact ordinary events"),
		HasExactExecutionEvents(OrdinaryResult,
			{EBattleEventType::RandomCheck, EBattleEventType::AccuracyChecked,
			 EBattleEventType::StatusChanged}));
	bValid &= TestTrue(TEXT("Non-Poison use consumes the ordinary accuracy draw"),
		OrdinaryRandom != nullptr && OrdinaryRandom->IsExact()
			&& OrdinaryRandom->GetTrace().Num() == 1);
	bValid &= TestTrue(TEXT("Non-Poison use hits at the lower 90-accuracy boundary"),
		HasMajorStatus(Ordinary.Get(), FBattleMajorStatusRules::GetToxicId()));

	TUniquePtr<FBattleEngine> OrdinaryMiss;
	FStrictBattleRandom* OrdinaryMissRandom = nullptr;
	FBattleEffectExecutionResult OrdinaryMissResult;
	bValid &= TestTrue(TEXT("A non-Poison boundary-miss engine is created"),
		TryMakeEngine(MakeToxicMove(ToxicMoveAName),
			EPokemonType::Normal, EPokemonType::Invalid,
			EPokemonType::Normal, EPokemonType::Invalid,
			FAbilityId(), FItemId(),
			{{0, 99, 90, FBattleEffectExecutor::GetAccuracyRulePurpose()}},
			OrdinaryMiss, OrdinaryMissRandom));
	bValid &= TestTrue(TEXT("A non-Poison user misses at the upper 90-accuracy boundary"),
		OrdinaryMiss.IsValid()
			&& TryExecuteDirect(*OrdinaryMiss, MoveId(ToxicMoveAName),
				OrdinaryMissResult, Error));
	bValid &= TestTrue(TEXT("The non-Poison boundary miss keeps exact events"),
		HasExactExecutionEvents(OrdinaryMissResult,
			{EBattleEventType::RandomCheck, EBattleEventType::AccuracyChecked,
			 EBattleEventType::Missed}));
	bValid &= TestTrue(TEXT("The non-Poison boundary miss consumes one exact draw"),
		OrdinaryMissRandom != nullptr && OrdinaryMissRandom->IsExact()
			&& OrdinaryMissRandom->GetTrace().Num() == 1);
	bValid &= TestTrue(TEXT("The non-Poison boundary miss applies no Toxic"),
		HasNoMajorStatus(OrdinaryMiss.Get()));

	TUniquePtr<FBattleEngine> Unreachable;
	FStrictBattleRandom* UnreachableRandom = nullptr;
	FBattleEffectExecutionResult UnreachableResult;
	bValid &= TestTrue(TEXT("A non-Poison Fly engine is created"),
		TryMakeEngine(MakeToxicMove(ToxicMoveAName),
			EPokemonType::Normal, EPokemonType::Invalid,
			EPokemonType::Normal, EPokemonType::Invalid,
			FAbilityId(), FItemId(), {}, Unreachable, UnreachableRandom));
	bValid &= TestTrue(TEXT("A non-Poison user cannot reach Fly"),
		Unreachable.IsValid()
			&& TrySeedActionStartVolatile(*Unreachable,
				MakeNumericId<FBattlerId>(OpponentLeftValue),
				FBattleVolatileRules::GetFlySemiInvulnerableId())
			&& TryExecuteDirect(*Unreachable, MoveId(ToxicMoveAName),
				UnreachableResult, Error)
			&& HasExactExecutionEvents(UnreachableResult,
				{EBattleEventType::Unreachable}));
	bValid &= TestTrue(TEXT("Unreachable non-Poison use consumes no accuracy RNG"),
		UnreachableRandom != nullptr && UnreachableRandom->IsExact()
			&& UnreachableRandom->GetTrace().IsEmpty());

	TUniquePtr<FBattleEngine> TypeImmune;
	FStrictBattleRandom* TypeImmuneRandom = nullptr;
	FBattleEffectExecutionResult TypeImmuneResult;
	bValid &= TestTrue(TEXT("A Poison-user Poison-target Fly engine is created"),
		TryMakeEngine(MakeToxicMove(ToxicMoveAName),
			EPokemonType::Poison, EPokemonType::Invalid,
			EPokemonType::Poison, EPokemonType::Invalid,
			FAbilityId(), FItemId(), {}, TypeImmune, TypeImmuneRandom));
	bValid &= TestTrue(TEXT("The Poison-user bypass reaches the later type gate"),
		TypeImmune.IsValid()
			&& TrySeedActionStartVolatile(*TypeImmune,
				MakeNumericId<FBattlerId>(OpponentLeftValue),
				FBattleVolatileRules::GetFlySemiInvulnerableId())
			&& TryExecuteDirect(*TypeImmune, MoveId(ToxicMoveAName),
				TypeImmuneResult, Error));
	bValid &= TestTrue(TEXT("The later type gate keeps exact events"),
		HasExactExecutionEvents(TypeImmuneResult,
			{EBattleEventType::AccuracyChecked, EBattleEventType::Immunity}));
	bValid &= TestTrue(TEXT("The later type gate consumes no RNG"),
		TypeImmuneRandom != nullptr && TypeImmuneRandom->IsExact()
			&& TypeImmuneRandom->GetTrace().IsEmpty());
	bValid &= TestTrue(TEXT("The later type gate applies no Toxic"),
		HasNoMajorStatus(TypeImmune.Get()));
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC10HitRulesGateOrderingTest,
	"PokemonSolarus.Battle.C05B.C10HitRules.GateOrdering",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC10HitRulesGateOrderingTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	bool bValid = true;
	EBattleEffectExecutorError Error = EBattleEffectExecutorError::None;
	auto RunNoDrawGate = [this, &bValid, &Error](
		const TCHAR* Label,
		const FBattleMoveDefinition& Move,
		const EPokemonType UserType,
		const FAbilityId Ability,
		const FItemId Item,
		const FConditionId Volatile,
		const TArray<EBattleEventType>& Expected)
	{
		TUniquePtr<FBattleEngine> Engine;
		FStrictBattleRandom* Random = nullptr;
		FBattleEffectExecutionResult Result;
		bValid &= TestTrue(FString::Printf(TEXT("%s engine is created"), Label),
			TryMakeEngine(Move, UserType, EPokemonType::Invalid,
				EPokemonType::Normal, EPokemonType::Invalid,
				Ability, Item, {}, Engine, Random));
		if (Ability.IsValid() || Item.IsValid())
		{
			FBattleRejection Rejection;
			bValid &= TestTrue(FString::Printf(TEXT("%s runtime begins"), Label),
				Engine.IsValid() && Engine->TryBeginActionDecisionSequence(Rejection));
		}
		if (Volatile.IsValid())
		{
			bValid &= TestTrue(FString::Printf(TEXT("%s volatile is seeded"), Label),
				Engine.IsValid() && (Volatile == FBattleVolatileRules::GetProtectId()
					? TrySeedProtect(*Engine, MakeNumericId<FBattlerId>(OpponentLeftValue))
					: TrySeedActionStartVolatile(*Engine,
						MakeNumericId<FBattlerId>(OpponentLeftValue), Volatile)));
		}
		bValid &= TestTrue(FString::Printf(TEXT("%s remains effective"), Label),
			Engine.IsValid() && TryExecuteDirect(*Engine, Move.Id, Result, Error)
				&& HasExactExecutionEvents(Result, Expected));
		bValid &= TestTrue(FString::Printf(TEXT("%s consumes no RNG"), Label),
			Random != nullptr && Random->IsExact() && Random->GetTrace().IsEmpty());
		bValid &= TestTrue(FString::Printf(TEXT("%s applies no new major status"), Label),
			HasNoMajorStatus(Engine.Get()));
	};

	RunNoDrawGate(TEXT("Protect"), MakeToxicMove(ToxicMoveAName),
		EPokemonType::Poison, FAbilityId(), FItemId(),
		FBattleVolatileRules::GetProtectId(), {EBattleEventType::Protected});
	RunNoDrawGate(TEXT("Levitate"), MakeGroundGateMove(),
		EPokemonType::Normal, FBattleAbilityRules::GetLevitateId(), FItemId(),
		FConditionId(), {EBattleEventType::AbilityActivated, EBattleEventType::Immunity});
	RunNoDrawGate(TEXT("Air Balloon"), MakeGroundGateMove(),
		EPokemonType::Normal, FAbilityId(), FBattleItemRules::GetAirBalloonId(),
		FConditionId(), {EBattleEventType::ItemActivated, EBattleEventType::Immunity});
	RunNoDrawGate(TEXT("Substitute"), MakeToxicMove(ToxicMoveAName),
		EPokemonType::Poison, FAbilityId(), FItemId(),
		FBattleVolatileRules::GetSubstituteId(),
		{EBattleEventType::AccuracyChecked, EBattleEventType::EffectBlocked});

	TUniquePtr<FBattleEngine> MajorStatus;
	FStrictBattleRandom* MajorRandom = nullptr;
	FBattleEffectExecutionResult MajorResult;
	bValid &= TestTrue(TEXT("The existing-major-status engine is created"),
		TryMakeEngine(MakeToxicMove(ToxicMoveAName),
			EPokemonType::Poison, EPokemonType::Invalid,
			EPokemonType::Normal, EPokemonType::Invalid,
			FAbilityId(), FItemId(), {}, MajorStatus, MajorRandom));
	bValid &= TestTrue(TEXT("An existing major status is seeded"),
		MajorStatus.IsValid() && TrySeedPreMoveMajorStatus(*MajorStatus,
			MakeNumericId<FBattlerId>(OpponentLeftValue),
			FBattleMajorStatusRules::GetBurnId()));
	bValid &= TestTrue(TEXT("The later major-status gate remains effective"),
		MajorStatus.IsValid()
			&& TryExecuteDirect(*MajorStatus, MoveId(ToxicMoveAName), MajorResult, Error)
			&& HasExactExecutionEvents(MajorResult,
				{EBattleEventType::AccuracyChecked, EBattleEventType::EffectFailed}));
	bValid &= TestTrue(TEXT("The existing major status is unchanged"),
		HasMajorStatus(MajorStatus.Get(), FBattleMajorStatusRules::GetBurnId()));
	bValid &= TestTrue(TEXT("The Poison-user bypass reaches the later gate without RNG"),
		MajorRandom != nullptr && MajorRandom->IsExact()
			&& MajorRandom->GetTrace().IsEmpty());
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC10HitRulesReplayDeterminismTest,
	"PokemonSolarus.Battle.C05B.C10HitRules.ReplayDeterminism",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC10HitRulesReplayDeterminismTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TUniquePtr<FBattleEngine> First;
	TUniquePtr<FBattleEngine> Second;
	FStrictBattleRandom* FirstRandom = nullptr;
	FStrictBattleRandom* SecondRandom = nullptr;
	const FBattleMoveDefinition Move = MakeToxicMove(ToxicMoveAName);
	if (!TestTrue(TEXT("The first replay engine is created"),
			TryMakeEngine(Move, EPokemonType::Poison, EPokemonType::Invalid,
				EPokemonType::Normal, EPokemonType::Invalid,
				FAbilityId(), FItemId(), {}, First, FirstRandom))
		|| !TestTrue(TEXT("The second replay engine is created"),
			TryMakeEngine(Move, EPokemonType::Poison, EPokemonType::Invalid,
				EPokemonType::Normal, EPokemonType::Invalid,
				FAbilityId(), FItemId(), {}, Second, SecondRandom))
		|| !TestTrue(TEXT("The first move reaches the effects checkpoint"),
			TryPrepareEffectsCheckpoint(*First, Move.Id))
		|| !TestTrue(TEXT("The second move reaches the effects checkpoint"),
			TryPrepareEffectsCheckpoint(*Second, Move.Id)))
	{
		return false;
	}
	const FBattleResolution FirstResult = First->ExecuteCurrentMoveEffects();
	const FBattleResolution SecondResult = Second->ExecuteCurrentMoveEffects();
	bool bValid = TestTrue(TEXT("Both full checkpoints are accepted"),
		FirstResult.WasAccepted() && SecondResult.WasAccepted());
	bValid &= TestTrue(TEXT("The full public event order is exact"),
		HasExactResolutionEvents(FirstResult,
			{EBattleEventType::AccuracyChecked, EBattleEventType::StatusChanged,
			 EBattleEventType::ActionCompleted})
			&& HasExactResolutionEvents(SecondResult,
				{EBattleEventType::AccuracyChecked, EBattleEventType::StatusChanged,
				 EBattleEventType::ActionCompleted}));
	bValid &= TestTrue(TEXT("Both full checkpoints consume no RNG"),
		FirstRandom != nullptr && SecondRandom != nullptr
			&& FirstRandom->IsExact() && SecondRandom->IsExact()
			&& First->ExportRandomTrace().IsEmpty()
			&& Second->ExportRandomTrace().IsEmpty());
	bValid &= TestTrue(TEXT("Each accepted resolution is published exactly once"),
		IsReturnedResolutionAppendedExactlyOnce(*First, FirstResult)
			&& IsReturnedResolutionAppendedExactlyOnce(*Second, SecondResult));

	const FBattleReplayRecord FirstRecord = First->ExportReplayRecord();
	const FBattleReplayRecord SecondRecord = Second->ExportReplayRecord();
	bValid &= TestTrue(TEXT("Replay schema 6 is preserved"),
		FirstRecord.GetSchemaVersion() == 6 && SecondRecord.GetSchemaVersion() == 6);
	TArray<uint8> FirstBytes;
	TArray<uint8> FirstRepeatBytes;
	TArray<uint8> SecondBytes;
	bValid &= TestTrue(TEXT("Both schema-6 replays serialize"),
		TrySerializeReplay(*First, FirstBytes)
			&& TrySerializeReplay(*First, FirstRepeatBytes)
			&& TrySerializeReplay(*Second, SecondBytes));
	bValid &= TestTrue(TEXT("Repeated and independent replay bytes are identical"),
		FirstBytes == FirstRepeatBytes && FirstBytes == SecondBytes);
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC10HitRulesAtomicRollbackTest,
	"PokemonSolarus.Battle.C05B.C10HitRules.AtomicRollback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC10HitRulesAtomicRollbackTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FBattleMoveDefinition Move = MakePowderMove();
	TUniquePtr<FBattleEngine> Engine;
	FStrictBattleRandom* Random = nullptr;
	if (!TestTrue(TEXT("The rollback engine is created"),
			TryMakeEngine(Move, EPokemonType::Normal, EPokemonType::Invalid,
				EPokemonType::Water, EPokemonType::Invalid,
				FAbilityId(), FItemId(),
				{{0, 99, 0, FBattleEffectExecutor::GetAccuracyRulePurpose()}},
				Engine, Random))
		|| !TestTrue(TEXT("The rollback move reaches the effects checkpoint"),
			TryPrepareEffectsCheckpoint(*Engine, Move.Id)))
	{
		return false;
	}
	const FTargetCheckpointObservation Before = ObserveTargetCheckpoint(*Engine);
	const FBattleResolution Rejected = Engine->ExecuteCurrentMoveEffects();
	bool bValid = VerifyRejectedTargetCheckpoint(
		*this,
		*Engine,
		Before,
		EBattleRejectionReason::CheckpointRandomStageFailed,
		Rejected);
	bValid &= TestTrue(TEXT("Rejected preparation commits no staged RNG"),
		Random != nullptr && Random->GetTrace().IsEmpty()
			&& Engine->ExportRandomTrace().IsEmpty());
	bValid &= TestTrue(TEXT("Rejected preparation commits no Sleep"),
		HasNoMajorStatus(Engine.Get()));
	return bValid;
}

} // namespace BattleMoveHitRuleTestsPrivate

#endif
