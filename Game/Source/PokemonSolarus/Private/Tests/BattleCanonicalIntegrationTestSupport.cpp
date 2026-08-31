#if WITH_DEV_AUTOMATION_TESTS
#include "BattleCanonicalIntegrationTestSupport.h"
#include "Battle/BattleActionSelector.h"
#include "Battle/BattleDataTableAdapter.h"
#include "Battle/BattleDataTableRuntimeSource.h"
#include "Battle/BattleRuntimeDataTableRows.h"
#include "Engine/DataTable.h"
#include "Misc/AutomationTest.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UObjectGlobals.h"
namespace BattleCanonicalIntegrationSupportPrivate
{
using namespace BattleCanonicalIntegrationTestSupport;
using BattleTest::FBattleExpectedRandomDraw;
using BattleTest::FStrictBattleRandom;

// C11A organization decision: keep this bounded integration harness cohesive.
// Its fixture, replay, selector, and evidence helpers share one contract, while
// splitting them would add an unapproved eleventh C11A source path.
const TCHAR *C11ACompletionStatus = TEXT("INCOMPLETE_CATALOG_DEFERRED");
const TCHAR *C11BEligibilityStatus = TEXT("USER_PERMITTED_NOT_STARTED");
const TCHAR *RuntimeTablePath = TEXT("/Game/Data/Battle/Initial/DT_BattleRuntimeScenario.DT_BattleRuntimeScenario");
const FName RuntimeRowName(TEXT("InitialBattle"));
const FName CatalogSnapshotName(TEXT("Catalog.InitialBattle"));
constexpr int32 ExpectedSpecies = 8;
constexpr int32 ExpectedNatures = 25;
constexpr int32 ExpectedMoves = 62;
constexpr int32 ExpectedAbilities = 8;
constexpr int32 ExpectedItems = 14;
constexpr int32 ExpectedConditions = 40;
template <typename IdType> FString NumericIdText(const IdType &Id)
{
	return Id.IsValid() ? LexToString(Id.GetValue()) : TEXT("0");
}
FString DefinitionIdText(const FDefinitionId &Id)
{
	return Id.IsValid() ? Id.GetName().ToString() : TEXT("None");
}
template <typename IdType> FString DefinitionIdText(const IdType &Id)
{
	return Id.IsValid() ? Id.GetDefinitionId().GetName().ToString() : TEXT("None");
}
FString ActiveIdText(const FActiveSlotId &Id)
{
	return Id.IsValid() ? FString::Printf(TEXT("%u:%u"), static_cast<uint8>(Id.GetSide()), static_cast<uint8>(Id.GetPosition())) : TEXT("None");
}
template <typename ValueType> FString OptionalNumber(const TOptional<ValueType> &Value)
{
	return Value.IsSet() ? LexToString(Value.GetValue()) : TEXT("-");
}
void AppendStages(FString &Out, const FBattleStatStages &Stages)
{
	for (uint8 Index = 0; Index < 7; ++Index)
	{
		int32 Value = 0;
		check(Stages.TryGetStage(static_cast<EBattleStat>(Index), Value));
		Out += FString::Printf(TEXT("%d,"), Value);
	}
}
FString DecisionSignature(const FBattleDecision &Decision)
{
	return FString::Printf(TEXT("%llu/%u/%s/%s/%u/%s/%s/%s/%s"), Decision.GetStateVersion(), static_cast<uint8>(Decision.GetRequestKind()), *NumericIdText(Decision.GetDecisionOwnerTrainerId()), *NumericIdText(Decision.GetActingBattlerId()),
						   static_cast<uint8>(Decision.GetActionKind()), *DefinitionIdText(Decision.GetMoveId()), *LexToString(Decision.GetSwitchPartySlotId().IsValid() ? Decision.GetSwitchPartySlotId().GetIndex() : INDEX_NONE),
						   *DefinitionIdText(Decision.GetItemId()), *ActiveIdText(Decision.GetActiveTargetId()));
}

class FCallbackBattleActionSelector final : public IBattleActionSelector
{
public:
	explicit FCallbackBattleActionSelector(
		const FChoiceProvider &InProvider,
		FString &InOutError)
		: Provider(InProvider), OutError(InOutError)
	{
	}

	[[nodiscard]] virtual bool TrySelectAction(
		const FBattleActionSelectorInput &Input,
		FBattleDecision &OutDecision,
		FBattleRejection &OutRejection) override
	{
		OutDecision = FBattleDecision();
		OutRejection = FBattleRejection();
		if (!Input.IsValid())
		{
			OutRejection.Reason = EBattleRejectionReason::InvalidSetup;
			return false;
		}

		const FBattleSnapshot &Observation = Input.GetObservation();
		const FTrainerId ObserverId = Observation.GetObserverTrainerId();
		const FBattleObservedTrainer *Observer = Observation.GetObservedTrainers().FindByPredicate(
			[ObserverId](const FBattleObservedTrainer &Trainer)
			{
				return Trainer.TrainerId == ObserverId;
			});
		if (Observer == nullptr)
		{
			OutError = TEXT("The selector observation did not contain its observing Trainer.");
			OutRejection.Reason = EBattleRejectionReason::InvalidSetup;
			return false;
		}
		for (const FBattleDecision &VisibleSelection : Observation.GetVisibleSelections())
		{
			const FTrainerId VisibleOwnerId = VisibleSelection.GetDecisionOwnerTrainerId();
			const FBattleObservedTrainer *VisibleOwner = Observation.GetObservedTrainers().FindByPredicate(
				[VisibleOwnerId](const FBattleObservedTrainer &Trainer)
				{
					return Trainer.TrainerId == VisibleOwnerId;
				});
			if (VisibleOwner == nullptr || VisibleOwner->Side != Observer->Side)
			{
				OutError = TEXT("The selector observation exposed an opposing side's hidden choice.");
				OutRejection.Reason = EBattleRejectionReason::InvalidDecision;
				return false;
			}
		}

		FChoice Choice;
		if (!Provider(Input.GetLegalActions(), Choice, OutError)
			|| !TryMakeDecision(Input.GetLegalActions(), Choice, OutDecision, OutError))
		{
			OutRejection.Reason = EBattleRejectionReason::InvalidDecision;
			return false;
		}
		return true;
	}

private:
	const FChoiceProvider &Provider;
	FString &OutError;
};
FString EventSignature(const FBattleEvent &Event)
{
	const FBattleEventSource &Source = Event.GetSource();
	const FBattleEventVisibility &Visibility = Event.GetVisibility();
	FString Out = FString::Printf(TEXT("%llu/%s/%s/%s/%s/%u/%u/%u/%u/S=%s,%s,%s,%s/N=%s,%s,%s/G=%s/H=%s,%s/V=%u,%s,%u,%d,%d"), Event.GetEventOrdinal(), *NumericIdText(Event.GetBattleId()), *NumericIdText(Event.GetTurnId()),
								  *NumericIdText(Event.GetActionId()), *NumericIdText(Event.GetResolutionId()), static_cast<uint8>(Event.GetType()), static_cast<uint8>(Event.GetCause()), static_cast<uint8>(Event.GetCauseActionKind()),
								  static_cast<uint8>(Event.GetOutcomeCause()), *NumericIdText(Source.TrainerId), *NumericIdText(Source.BattlerId), *ActiveIdText(Source.ActiveSlotId), *DefinitionIdText(Source.DefinitionId),
								  *OptionalNumber(Event.GetNumericBefore()), *OptionalNumber(Event.GetNumericAfter()), *OptionalNumber(Event.GetNumericDelta()), *OptionalNumber(Event.GetSimultaneousGroupId()),
								  *OptionalNumber(Event.GetHitIndex()), *OptionalNumber(Event.GetHitCount()), static_cast<uint8>(Visibility.Level), *NumericIdText(Visibility.OwningTrainerId), static_cast<uint8>(Visibility.OwningSide),
								  Visibility.bHasOwningSide, Visibility.bRevealSourceDefinition);
	for (const FBattleEventTarget &Target : Event.GetTargets())
	{
		Out += FString::Printf(TEXT("/T=%s,%s,%s,%u,%d,%d"), *NumericIdText(Target.TrainerId), *NumericIdText(Target.BattlerId), *ActiveIdText(Target.ActiveSlotId), static_cast<uint8>(Target.Side), Target.bHasSide, Target.bField);
	}
	if (Event.GetActionOrder().IsSet())
	{
		const FBattleActionOrderMetadata &Order = Event.GetActionOrder().GetValue();
		Out += FString::Printf(TEXT("/O=%llu,%u,%d,%d,%d,%s,%d"), Order.QueueOrdinal, static_cast<uint8>(Order.OrderKey.CommandBand), Order.OrderKey.MovePriority, Order.OrderKey.FractionalPriorityTenths, Order.OrderKey.EffectiveSpeed,
							   *ActiveIdText(Order.OrderKey.ActingSlotId), Order.bReverseSpeed);
	}
	if (Event.GetTargetResolution().IsSet())
	{
		const FBattleTargetResolutionMetadata &Target = Event.GetTargetResolution().GetValue();
		Out += FString::Printf(TEXT("/R=%u,%d,%d"), static_cast<uint8>(Target.TargetClass), Target.bWasRedirected, Target.bUsedFaintedTargetFallback);
	}
	if (Event.GetCapture().IsSet())
	{
		const FBattleCaptureEventMetadata &Capture = Event.GetCapture().GetValue();
		Out += FString::Printf(TEXT("/C=%llu,%u,%u,%u,%u,%u,%u,%u,%d,%d,%d,%d,%d,%u,%u,%u,%u,%d,%llu,%u"), Capture.CaptureIndicatorQ12, Capture.CaughtCountHPModifierQ12, Capture.BadgeModifierQ12, Capture.StatusModifierQ12,
							   Capture.CaptureCoefficientQ12, Capture.CriticalModifierQ12, Capture.CriticalThreshold, Capture.ShakeThreshold, Capture.bCriticalEligible, Capture.bCriticalCapture, Capture.bGuaranteedCapture,
							   Capture.bMustCapture, Capture.bSucceeded, Capture.RequiredShakeChecks, Capture.ShakeChecksPerformed, Capture.ShakeChecksPassed, Capture.VisualShakeCount, Capture.bHasPendingDestination,
							   Capture.PendingCaptureOrdinal, static_cast<uint8>(Capture.PendingDestination));
	}
	return Out;
}
FString ResolutionSignature(const FBattleResolution &Resolution)
{
	const FBattleRejection &Rejection = Resolution.GetRejection();
	FString Out = FString::Printf(TEXT("%s/%llu/%llu/%d/%u/%s/%s/%s/%s/%s/%s/%s"), *NumericIdText(Resolution.GetResolutionId()), Resolution.GetBeforeStateVersion(), Resolution.GetAfterStateVersion(), Resolution.WasAccepted(),
								  static_cast<uint8>(Rejection.Reason), *NumericIdText(Rejection.TrainerId), *NumericIdText(Rejection.BattlerId), *NumericIdText(Rejection.ActionId), *DefinitionIdText(Rejection.MoveId),
								  *DefinitionIdText(Rejection.ItemId), *LexToString(Rejection.PartySlotId.IsValid() ? Rejection.PartySlotId.GetIndex() : INDEX_NONE), *ActiveIdText(Rejection.ActiveSlotId));
	for (const FBattleEvent &Event : Resolution.GetEvents())
	{
		Out += TEXT("\nE:") + EventSignature(Event);
	}
	return Out;
}
FString ReplaySemanticSignature(const FBattleReplayRecord &Record)
{
	FString Out = FString::Printf(TEXT("Schema=%u\n"), Record.GetSchemaVersion());
	const FBattleReplayInputs &Inputs = Record.GetInputs();
	Out += FString::Printf(TEXT("Setup=%s/%u/%u/%s/%u/%s/%u\n"), *NumericIdText(Inputs.Setup.GetBattleId()), static_cast<uint8>(Inputs.Setup.GetEncounterKind()), static_cast<uint8>(Inputs.Setup.GetFormat()),
						   *DefinitionIdText(Inputs.Setup.GetSettingsReference().SnapshotId), Inputs.Setup.GetSettingsReference().SchemaVersion, *DefinitionIdText(Inputs.Setup.GetCatalogReference().SnapshotId),
						   Inputs.Setup.GetCatalogReference().SchemaVersion);
	for (const FBattleDecision &Decision : Inputs.Decisions)
	{
		Out += TEXT("D:") + DecisionSignature(Decision) + TEXT("\n");
	}
	for (const FBattleBetweenActionsStatRefresh &Refresh : Inputs.StatRefreshes)
	{
		Out += FString::Printf(TEXT("F:%llu/%llu/%s/%d/%d,%d,%d,%d,%d,%d/%d\n"), Refresh.StateVersion, Refresh.OpponentRemovalCheckpointEventOrdinal, *NumericIdText(Refresh.BattlerId), Refresh.NewLevel, Refresh.NewStats.MaxHP,
							   Refresh.NewStats.Attack, Refresh.NewStats.Defense, Refresh.NewStats.SpecialAttack, Refresh.NewStats.SpecialDefense, Refresh.NewStats.Speed, Refresh.NewCurrentHP);
	}
	for (const FBattleResolution &Resolution : Record.GetResolutions())
	{
		Out += TEXT("Q:") + ResolutionSignature(Resolution) + TEXT("\n");
	}
	for (const FBattleRandomDraw &Draw : Record.GetRandomTrace())
	{
		Out += FString::Printf(TEXT("G:%u/%u/%llu/%llu/%u/%llu/%s/%s/%s/%s/%s\n"), Draw.InclusiveMinimum, Draw.InclusiveMaximum, Draw.Bound, Draw.RawValue, Draw.Result, Draw.CallOrdinal, *NumericIdText(Draw.BattleId),
							   *NumericIdText(Draw.TurnId), *NumericIdText(Draw.ActionId), *NumericIdText(Draw.ResolutionId), *DefinitionIdText(Draw.RulePurpose));
	}
	Out += TEXT("Z:") + SnapshotSignature(Record.GetFinalSnapshot());
	return Out;
}
bool CreateEngine(const FBattleSetup &Setup, const FBattleDefinitionCatalog &Catalog, TUniquePtr<IBattleRandom> &&Random, TUniquePtr<FBattleEngine> &OutEngine, FString &OutError)
{
	FBattleRejection Rejection;
	if (!FBattleEngine::TryCreate(Setup, Catalog, MoveTemp(Random), OutEngine, Rejection))
	{
		OutError = FString::Printf(TEXT("FBattleEngine::TryCreate rejected the setup with reason %u."), static_cast<uint8>(Rejection.Reason));
		return false;
	}
	return true;
}
bool DriveOne(const FBattleDefinitionCatalog &Catalog, const FBattleSetup &Setup, TUniquePtr<IBattleRandom> &&Random, const FDriveFunction &Drive, FRunEvidence &OutEvidence, FString &OutError)
{
	TUniquePtr<FBattleEngine> Engine;
	if (!CreateEngine(Setup, Catalog, MoveTemp(Random), Engine, OutError))
	{
		return false;
	}
	RecordCheckpoint(*Engine, OutEvidence, TEXT("created"));
	if (!Drive(*Engine, OutEvidence, OutError))
	{
		return false;
	}
	return FinalizeEvidence(*Engine, OutEvidence, OutError);
}
bool CompareEvidence(FAutomationTestBase &Test, const FString &Label, const FRunEvidence &First, const FRunEvidence &Second)
{
	bool bEqual = true;
	bEqual &= Test.TestTrue(Label + TEXT(" canonical replay bytes are non-empty"), !First.ReplayBytes.IsEmpty());
	bEqual &= Test.TestTrue(Label + TEXT(" canonical replay bytes match"), First.ReplayBytes == Second.ReplayBytes);
	bEqual &= Test.TestEqual(Label + TEXT(" semantic replay fields match"), ReplaySemanticSignature(First.Replay), ReplaySemanticSignature(Second.Replay));
	bEqual &= Test.TestTrue(Label + TEXT(" snapshot checkpoints match"), First.Checkpoints == Second.Checkpoints);
	bEqual &= Test.TestTrue(Label + TEXT(" selector observations match"), First.SelectorObservations == Second.SelectorObservations);
	bEqual &= Test.TestEqual(Label + TEXT(" final public snapshot matches"), First.FinalSnapshotSignature, Second.FinalSnapshotSignature);
	const TConstArrayView<FBattleRandomDraw> FirstTrace = First.Replay.GetRandomTrace();
	const TConstArrayView<FBattleRandomDraw> SecondTrace = Second.Replay.GetRandomTrace();
	bool bRandomEqual = FirstTrace.Num() == SecondTrace.Num();
	for (int32 Index = 0; bRandomEqual && Index < FirstTrace.Num(); ++Index)
	{
		const FBattleRandomDraw& Left = FirstTrace[Index];
		const FBattleRandomDraw& Right = SecondTrace[Index];
		bRandomEqual = Left.InclusiveMinimum == Right.InclusiveMinimum && Left.InclusiveMaximum == Right.InclusiveMaximum
			&& Left.Bound == Right.Bound && Left.RawValue == Right.RawValue && Left.Result == Right.Result
			&& Left.CallOrdinal == Right.CallOrdinal && Left.BattleId == Right.BattleId && Left.TurnId == Right.TurnId
			&& Left.ActionId == Right.ActionId && Left.ResolutionId == Right.ResolutionId && Left.RulePurpose == Right.RulePurpose;
	}
	bEqual &= Test.TestTrue(Label + TEXT(" RNG draws and contexts match"), bRandomEqual);
	return bEqual;
}
template <typename DefinitionType, typename IdGetter> bool ValidateDefinitionSet(FAutomationTestBase &Test, const TCHAR *Label, TConstArrayView<DefinitionType> Definitions, const TArray<FName> &Manifest, IdGetter GetId)
{
	TSet<FName> Actual;
	for (const DefinitionType &Definition : Definitions)
	{
		Actual.Add(GetId(Definition));
	}
	TSet<FName> Expected;
	Expected.Reserve(Manifest.Num());
	for (const FName Id : Manifest)
	{
		Expected.Add(Id);
	}
	bool bSameSet = Actual.Num() == Expected.Num();
	for (const FName Id : Expected)
	{
		bSameSet &= Actual.Contains(Id);
	}
	return Test.TestEqual(FString(Label) + TEXT(" manifest count"), Manifest.Num(), Definitions.Num()) && Test.TestEqual(FString(Label) + TEXT(" manifest unique count"), Expected.Num(), Manifest.Num()) &&
		   Test.TestTrue(FString(Label) + TEXT(" manifest equals the production set"), bSameSet);
}
TArray<FName> Names(std::initializer_list<const TCHAR *> Values)
{
	TArray<FName> Out;
	Out.Reserve(static_cast<int32>(Values.size()));
	for (const TCHAR *Value : Values)
	{
		Out.Add(FName(Value));
	}
	return Out;
}
} // namespace BattleCanonicalIntegrationSupportPrivate
namespace BattleCanonicalIntegrationTestSupport
{
FPartySlotId MakePartySlotId(const int32 Index)
{
	FPartySlotId Id;
	check(FPartySlotId::TryCreate(Index, Id));
	return Id;
}
FActiveSlotId MakeActiveSlotId(const EBattleSide Side, const EBattlePosition Position)
{
	FActiveSlotId Id;
	check(FActiveSlotId::TryCreate(Side, Position, Id));
	return Id;
}
bool TryLoadProductionFixture(FAutomationTestBase &Test, FCatalogFixture &OutFixture, FString &OutError)
{
	using namespace BattleCanonicalIntegrationSupportPrivate;
	OutFixture = FCatalogFixture();
	OutError.Reset();
	UDataTable *RuntimeTable = LoadObject<UDataTable>(nullptr, RuntimeTablePath);
	if (RuntimeTable == nullptr || RuntimeTable->GetRowStruct() != FBattleRuntimeScenarioTableRow::StaticStruct())
	{
		OutError = TEXT("The accepted production runtime scenario table did not load with its exact row type.");
		return false;
	}
	const FBattleRuntimeScenarioTableRow *Row = RuntimeTable->FindRow<FBattleRuntimeScenarioTableRow>(RuntimeRowName, TEXT("C11A production catalog load"), false);
	if (Row == nullptr || Row->CatalogSnapshotId != CatalogSnapshotName || Row->CatalogSchemaVersion != 1)
	{
		OutError = TEXT("InitialBattle does not reference Catalog.InitialBattle schema 1.");
		return false;
	}
	FBattleDataTableSet Tables;
	Tables.SpeciesForms = Row->SpeciesForms.LoadSynchronous();
	Tables.Natures = Row->Natures.LoadSynchronous();
	Tables.Moves = Row->Moves.LoadSynchronous();
	Tables.Abilities = Row->Abilities.LoadSynchronous();
	Tables.Items = Row->Items.LoadSynchronous();
	Tables.Conditions = Row->Conditions.LoadSynchronous();
	Tables.TypeChart = Row->TypeChart.LoadSynchronous();
	FBattleDefinitionCatalog Catalog;
	TArray<FBattleCatalogDiagnostic> Diagnostics;
	if (!FBattleDataTableAdapter::BuildCatalog(Tables, Catalog, Diagnostics) || !Catalog.IsValid())
	{
		OutError = FString::Printf(TEXT("Production catalog construction failed with %d diagnostic(s)."), Diagnostics.Num());
		return false;
	}
	if (Catalog.GetSpeciesForms().Num() != ExpectedSpecies || Catalog.GetNatures().Num() != ExpectedNatures || Catalog.GetMoves().Num() != ExpectedMoves || Catalog.GetAbilities().Num() != ExpectedAbilities ||
		Catalog.GetItems().Num() != ExpectedItems || Catalog.GetConditions().Num() != ExpectedConditions || !Catalog.GetTypeChart().IsValid())
	{
		OutError = TEXT("The production catalog inventory differs from the accepted C10B counts.");
		return false;
	}
	OutFixture.Catalog = Catalog;
	FBattleDataTableRuntimeSource RuntimeSource{TSoftObjectPtr<UDataTable>(FSoftObjectPath(RuntimeTablePath))};
	FBattleRuntimeBundle Bundle;
	if (!RuntimeSource.TryCreateInitialBattle(Bundle, OutError) || !Bundle.IsValid())
	{
		return false;
	}
	OutFixture.ProductionSetup = Bundle.Engine->ExportReplayInputs().Setup;
	OutFixture.LocalTrainerId = Bundle.LocalTrainerId;
	Bundle.Reset();
	RuntimeTable = nullptr;
	Tables = FBattleDataTableSet();
	Test.TestEqual(TEXT("C11A production species count"), OutFixture.Catalog.GetSpeciesForms().Num(), 8);
	Test.TestEqual(TEXT("C11A production nature count"), OutFixture.Catalog.GetNatures().Num(), 25);
	Test.TestEqual(TEXT("C11A production move count"), OutFixture.Catalog.GetMoves().Num(), 62);
	Test.TestEqual(TEXT("C11A production Ability count"), OutFixture.Catalog.GetAbilities().Num(), 8);
	Test.TestEqual(TEXT("C11A production item count"), OutFixture.Catalog.GetItems().Num(), 14);
	Test.TestEqual(TEXT("C11A production condition count"), OutFixture.Catalog.GetConditions().Num(), 40);
	Test.TestEqual(TEXT("C11A runtime setup catalog reference"), OutFixture.ProductionSetup.GetCatalogReference().SnapshotId.GetName(), CatalogSnapshotName);
	Test.TestEqual(TEXT("C11A runtime setup catalog schema"), OutFixture.ProductionSetup.GetCatalogReference().SchemaVersion, 1U);
	return true;
}
bool TryBuildSetup(const FBattleDefinitionCatalog &Catalog, const FSetupSpec &Spec, FBattleSetup &OutSetup, FString &OutError)
{
	OutSetup = FBattleSetup();
	OutError.Reset();
	FBattleSetupInput Input;
	Input.BattleId = MakeNumericId<FBattleId>(Spec.BattleValue);
	Input.SettingsReference = {MakeDefinitionId<FDefinitionId>(TEXT("Settings.C11A.DeterministicIntegration")), 1};
	Input.CatalogReference = {MakeDefinitionId<FDefinitionId>(TEXT("Catalog.InitialBattle")), 1};
	Input.EncounterKind = Spec.EncounterKind;
	Input.Format = Spec.Format;
	Input.CaptureCapacity = Spec.CaptureCapacity;
	Input.CaptureProgression = Spec.CaptureProgression;
	Input.Policies = Spec.Policies;
	Input.KnowledgeFacts = Spec.Knowledge;
	for (const FTrainerSpec &TrainerSpec : Spec.Trainers)
	{
		FBattleTrainerSetup Trainer;
		Trainer.TrainerId = MakeNumericId<FTrainerId>(TrainerSpec.TrainerValue);
		Trainer.Side = TrainerSpec.Side;
		Trainer.Role = TrainerSpec.Role;
		Trainer.Controller = TrainerSpec.Controller;
		Trainer.SelectorProfileId = MakeDefinitionId<FDefinitionId>(TrainerSpec.Role == EBattleTrainerRole::Player	  ? TEXT("Selector.C11A.Player")
																	: TrainerSpec.Role == EBattleTrainerRole::Partner ? TEXT("Selector.C11A.Partner")
																													  : TEXT("Selector.C11A.Opponent"));
		for (const TPair<FName, int32> &BagEntry : TrainerSpec.Bag)
		{
			FItemId ItemId;
			if (!FItemId::TryCreate(BagEntry.Key, ItemId) || Catalog.FindItem(ItemId) == nullptr)
			{
				OutError = FString::Printf(TEXT("Unknown production Bag item %s."), *BagEntry.Key.ToString());
				return false;
			}
			Trainer.Bag.Add({ItemId, BagEntry.Value});
		}
		Input.Trainers.Add(MoveTemp(Trainer));
	}
	for (const FBattlerSpec &BattlerSpec : Spec.Battlers)
	{
		FSpeciesFormId SpeciesId;
		FNatureId NatureId;
		FAbilityId AbilityId;
		if (!FSpeciesFormId::TryCreate(BattlerSpec.SpeciesId, SpeciesId) || !FNatureId::TryCreate(BattlerSpec.NatureId, NatureId) || !FAbilityId::TryCreate(BattlerSpec.AbilityId, AbilityId))
		{
			OutError = TEXT("A canonical battler carries an invalid definition identity.");
			return false;
		}
		const FBattleSpeciesFormDefinition *Species = Catalog.FindSpeciesForm(SpeciesId);
		const FBattleNatureDefinition *Nature = Catalog.FindNature(NatureId);
		if (Species == nullptr || Nature == nullptr || Catalog.FindAbility(AbilityId) == nullptr || !Species->AbilityChoices.Contains(AbilityId))
		{
			OutError = FString::Printf(TEXT("Battler %llu does not use a valid production species/nature/Ability combination."), BattlerSpec.BattlerValue);
			return false;
		}
		FPokemonStatInputs StatInputs;
		StatInputs.Level = BattlerSpec.Level;
		StatInputs.BaseStats = Species->BaseStats;
		StatInputs.IndividualValues = BattlerSpec.IndividualValues;
		StatInputs.EffortValues = BattlerSpec.EffortValues;
		StatInputs.NatureModifier = Nature->Modifier;
		FPokemonBattleStats Stats;
		EBattleStatCalculationError StatError = EBattleStatCalculationError::None;
		if (!FBattleStatCalculator::TryCalculatePermanentStats(StatInputs, Stats, StatError))
		{
			OutError = FString::Printf(TEXT("Permanent stats for battler %llu failed with error %u."), BattlerSpec.BattlerValue, static_cast<uint8>(StatError));
			return false;
		}
		FBattlePartyEntrySetup Entry;
		Entry.TrainerId = MakeNumericId<FTrainerId>(BattlerSpec.TrainerValue);
		Entry.BattlerId = MakeNumericId<FBattlerId>(BattlerSpec.BattlerValue);
		Entry.SourcePokemonId = MakeNumericId<FSourcePokemonId>(100000 + BattlerSpec.BattlerValue);
		Entry.PartySlotId = MakePartySlotId(BattlerSpec.PartyIndex);
		Entry.SpeciesFormId = SpeciesId;
		Entry.Level = BattlerSpec.Level;
		Entry.Stats = Stats;
		Entry.CurrentHP = BattlerSpec.CurrentHP == INDEX_NONE ? Stats.MaxHP : BattlerSpec.CurrentHP;
		Entry.AbilityId = AbilityId;
		Entry.CaptureClassification = BattlerSpec.CaptureClassification;
		if (!BattlerSpec.OriginalHeldItemId.IsNone())
		{
			check(FItemId::TryCreate(BattlerSpec.OriginalHeldItemId, Entry.OriginalHeldItemId));
			const FBattleItemDefinition *Item = Catalog.FindItem(Entry.OriginalHeldItemId);
			if (Item == nullptr || Item->Kind != EBattleItemKind::Held)
			{
				OutError = TEXT("Original held item is not a production Held-kind definition.");
				return false;
			}
		}
		if (!BattlerSpec.CurrentHeldItemId.IsNone())
		{
			check(FItemId::TryCreate(BattlerSpec.CurrentHeldItemId, Entry.CurrentHeldItemId));
			const FBattleItemDefinition *Item = Catalog.FindItem(Entry.CurrentHeldItemId);
			if (Item == nullptr || Item->Kind != EBattleItemKind::Held)
			{
				OutError = TEXT("Current held item is not a production Held-kind definition.");
				return false;
			}
		}
		if (BattlerSpec.MoveIds.IsEmpty() || BattlerSpec.MoveIds.Num() > 4 || (!BattlerSpec.MoveCurrentPP.IsEmpty() && BattlerSpec.MoveCurrentPP.Num() != BattlerSpec.MoveIds.Num()))
		{
			OutError = TEXT("Every canonical battler needs one through four production moves and either zero or one starting-PP value per move.");
			return false;
		}
		for (int32 MoveIndex = 0; MoveIndex < BattlerSpec.MoveIds.Num(); ++MoveIndex)
		{
			FMoveId MoveId;
			check(FMoveId::TryCreate(BattlerSpec.MoveIds[MoveIndex], MoveId));
			const FBattleMoveDefinition *Move = Catalog.FindMove(MoveId);
			if (Move == nullptr)
			{
				OutError = FString::Printf(TEXT("Unknown production move %s."), *BattlerSpec.MoveIds[MoveIndex].ToString());
				return false;
			}
			const int32 CurrentPP = BattlerSpec.MoveCurrentPP.IsEmpty() ? Move->BasePP : BattlerSpec.MoveCurrentPP[MoveIndex];
			if (CurrentPP < 0 || CurrentPP > Move->BasePP)
			{
				OutError = FString::Printf(TEXT("Move %s has out-of-bounds starting PP."), *BattlerSpec.MoveIds[MoveIndex].ToString());
				return false;
			}
			Entry.Moves.Add({static_cast<uint8>(MoveIndex), MoveId, CurrentPP, Move->BasePP});
		}
		Input.PartyEntries.Add(MoveTemp(Entry));
	}
	for (const FActiveSpec &Active : Spec.Active)
	{
		Input.StartingActive.Add({MakeActiveSlotId(Active.Side, Active.Position), MakeNumericId<FTrainerId>(Active.TrainerValue), MakeNumericId<FBattlerId>(Active.BattlerValue)});
	}
	for (const FObedienceSpec &Obedience : Spec.Obedience)
	{
		Input.ObedienceInputs.Add({MakeNumericId<FBattlerId>(Obedience.BattlerValue), Obedience.bSubjectToPlayerCap, Obedience.ReferenceLevel, Obedience.BadgeCount});
	}
	EBattleSetupValidationError Error = EBattleSetupValidationError::None;
	if (!FBattleSetup::TryCreate(Input, OutSetup, Error))
	{
		OutError = FString::Printf(TEXT("FBattleSetup rejected the canonical scenario with error %u."), static_cast<uint8>(Error));
		return false;
	}
	return true;
}
bool TryMakeDecision(const FBattleDecisionRequest &Request, const FChoice &Choice, FBattleDecision &OutDecision, FString &OutError)
{
	OutDecision = FBattleDecision();
	OutError.Reset();
	bool bCreated = false;
	switch (Choice.Kind)
	{
	case EChoiceKind::Fight:
	{
		FMoveId MoveId;
		if (!FMoveId::TryCreate(Choice.DefinitionId, MoveId))
		{
			break;
		}
		if (Request.GetAutomaticallyTargetedMoveIds().Contains(MoveId))
		{
			bCreated = FBattleDecision::TryCreateAutomaticallyTargetedFight(Request.GetStateVersion(), Request.GetDecisionOwnerTrainerId(), Request.GetActingBattlerId(), MoveId, OutDecision);
		}
		else
		{
			const FBattleMoveTargetOption *Target =
				Request.GetLegalMoveTargets().FindByPredicate([MoveId, &Choice](const FBattleMoveTargetOption &Option) { return Option.MoveId == MoveId && (!Choice.ActiveTarget.IsValid() || Option.ActiveSlotId == Choice.ActiveTarget); });
			if (Target != nullptr)
			{
				bCreated = FBattleDecision::TryCreateFight(Request.GetStateVersion(), Request.GetDecisionOwnerTrainerId(), Request.GetActingBattlerId(), MoveId, Target->ActiveSlotId, OutDecision);
			}
		}
		break;
	}
	case EChoiceKind::Bag:
	{
		FItemId ItemId;
		if (FItemId::TryCreate(Choice.DefinitionId, ItemId))
		{
			bCreated = FBattleDecision::TryCreateBag(Request.GetStateVersion(), Request.GetDecisionOwnerTrainerId(), Request.GetActingBattlerId(), ItemId, Choice.PartyTarget, Choice.ActiveTarget, OutDecision);
		}
		break;
	}
	case EChoiceKind::Switch:
		bCreated = FBattleDecision::TryCreateSwitch(Request.GetStateVersion(), Request.GetRequestKind(), Request.GetDecisionOwnerTrainerId(), Request.GetActingBattlerId(), Choice.PartyTarget,
													Choice.ActiveTarget.IsValid() ? Choice.ActiveTarget : Request.GetActingSlotId(), OutDecision);
		break;
	case EChoiceKind::Run:
	case EChoiceKind::WildFlee:
		bCreated = FBattleDecision::TryCreateSimpleAction(Request.GetStateVersion(), Request.GetRequestKind(), Request.GetDecisionOwnerTrainerId(), Request.GetActingBattlerId(),
														  Choice.Kind == EChoiceKind::Run ? EBattleActionKind::Run : EBattleActionKind::WildFlee, OutDecision);
		break;
	case EChoiceKind::Replacement:
		bCreated = FBattleDecision::TryCreateReplacement(Request.GetStateVersion(), Request.GetDecisionOwnerTrainerId(), Choice.PartyTarget, Request.GetActingSlotId(), OutDecision);
		break;
	case EChoiceKind::ShiftAccept:
		bCreated = FBattleDecision::TryCreateShiftSwitch(Request.GetStateVersion(), Request.GetDecisionOwnerTrainerId(), Request.GetActingBattlerId(), Choice.PartyTarget, Request.GetActingSlotId(), OutDecision);
		break;
	case EChoiceKind::ShiftDecline:
		bCreated = FBattleDecision::TryCreateShiftDecline(Request.GetStateVersion(), Request.GetDecisionOwnerTrainerId(), Request.GetActingBattlerId(), OutDecision);
		break;
	}
	if (!bCreated)
	{
		OutError =
			FString::Printf(TEXT("Could not construct choice %u for battler %llu from the current legal request."), static_cast<uint8>(Choice.Kind), Request.GetActingBattlerId().IsValid() ? Request.GetActingBattlerId().GetValue() : 0);
	}
	return bCreated;
}
FString SnapshotSignature(const FBattleSnapshot &Snapshot)
{
	using namespace BattleCanonicalIntegrationSupportPrivate;
	FString Out = FString::Printf(TEXT("%d/%llu/%s/%s/%u/%u/%u/%u/%u/%d/%s/%u/%s/%u/%u/%d/%s\n"), Snapshot.IsValid(), Snapshot.GetStateVersion(), *NumericIdText(Snapshot.GetBattleId()), *NumericIdText(Snapshot.GetTurnId()),
								  static_cast<uint8>(Snapshot.GetPhase()), static_cast<uint8>(Snapshot.GetEncounterKind()), static_cast<uint8>(Snapshot.GetFormat()), static_cast<uint8>(Snapshot.GetOutcome()),
								  static_cast<uint8>(Snapshot.GetOutcomeCause()), Snapshot.IsCaptureStateVisible(), *DefinitionIdText(Snapshot.GetCatalogReference().SnapshotId), Snapshot.GetCatalogReference().SchemaVersion,
								  *DefinitionIdText(Snapshot.GetSettingsReference().SnapshotId), Snapshot.GetSettingsReference().SchemaVersion, Snapshot.GetEscapeAttemptCount(), Snapshot.HasSuccessfulReinforcement(),
								  *NumericIdText(Snapshot.GetConfiguredReinforcementBattlerId()));
	for (const FBattleTrainerSetup &Trainer : Snapshot.GetTrainers())
	{
		Out += FString::Printf(TEXT("T:%s/%u/%u/%u"), *NumericIdText(Trainer.TrainerId), static_cast<uint8>(Trainer.Side), static_cast<uint8>(Trainer.Role), static_cast<uint8>(Trainer.Controller));
		for (const FBattleBagItemCount &Item : Trainer.Bag)
		{
			Out += FString::Printf(TEXT("/%s=%d"), *DefinitionIdText(Item.ItemId), Item.Count);
		}
		Out += TEXT("\n");
	}
	for (const FBattlePartyEntrySetup &Entry : Snapshot.GetPartyEntries())
	{
		Out += FString::Printf(TEXT("P:%s/%s/%d/%s/%d/%d/%d,%d,%d,%d,%d,%d/%s/%s/%s"), *NumericIdText(Entry.TrainerId), *NumericIdText(Entry.BattlerId), Entry.PartySlotId.GetIndex(), *DefinitionIdText(Entry.SpeciesFormId), Entry.Level,
							   Entry.CurrentHP, Entry.Stats.MaxHP, Entry.Stats.Attack, Entry.Stats.Defense, Entry.Stats.SpecialAttack, Entry.Stats.SpecialDefense, Entry.Stats.Speed, *DefinitionIdText(Entry.AbilityId),
							   *DefinitionIdText(Entry.OriginalHeldItemId), *DefinitionIdText(Entry.CurrentHeldItemId));
		for (const FBattleMoveSlotSetup &Move : Entry.Moves)
		{
			Out += FString::Printf(TEXT("/M=%u,%s,%d,%d"), Move.SlotIndex, *DefinitionIdText(Move.MoveId), Move.CurrentPP, Move.MaxPP);
		}
		Out += TEXT("\n");
	}
	for (const FBattleActiveAssignment &Active : Snapshot.GetActiveAssignments())
	{
		Out += FString::Printf(TEXT("A:%s/%s/%s\n"), *ActiveIdText(Active.ActiveSlotId), *NumericIdText(Active.TrainerId), *NumericIdText(Active.BattlerId));
	}
	for (const FBattleObservedBattler &Battler : Snapshot.GetObservedBattlers())
	{
		Out += FString::Printf(TEXT("B:%s/%d/%d/%d/%s/"), *NumericIdText(Battler.BattlerId), Battler.CurrentHP, Battler.MaxHP, Battler.bFainted, *DefinitionIdText(Battler.MajorStatusId));
		AppendStages(Out, Battler.StatStages);
		Out += FString::Printf(TEXT("/%d,%s/%d,%s\n"), Battler.bAbilityKnown, *DefinitionIdText(Battler.AbilityId), Battler.bHeldItemKnown, *DefinitionIdText(Battler.HeldItemId));
	}
	for (const FBattleObservedTrainer &Trainer : Snapshot.GetObservedTrainers())
	{
		Out += FString::Printf(TEXT("O:%s/%d/%d\n"), *NumericIdText(Trainer.TrainerId), Trainer.bBagVisible, Trainer.Bag.Num());
	}
	for (const FBattleDecision &Decision : Snapshot.GetVisibleSelections()) {
		Out += FString::Printf(TEXT("V:%s/%s/%u/%s\n"), *NumericIdText(Decision.GetDecisionOwnerTrainerId()), *NumericIdText(Decision.GetActingBattlerId()), static_cast<uint8>(Decision.GetActionKind()), *DefinitionIdText(Decision.GetMoveId()));
	}
	auto AppendCondition = [&Out](const TCHAR *Prefix, const FBattleObservedCondition &Condition)
	{
		Out += FString::Printf(TEXT("%s:%s/%s/%d/%llu/%s\n"), Prefix, *DefinitionIdText(Condition.ConditionId), *OptionalNumber(Condition.RemainingTurns), Condition.LayerCount, Condition.CreationOrdinal,
							   *NumericIdText(Condition.SourceBattlerId));
	};
	if (Snapshot.GetWeather().IsSet())
		AppendCondition(TEXT("W"), Snapshot.GetWeather().GetValue());
	if (Snapshot.GetTerrain().IsSet())
		AppendCondition(TEXT("N"), Snapshot.GetTerrain().GetValue());
	for (const FBattleObservedCondition &Room : Snapshot.GetRooms())
		AppendCondition(TEXT("R"), Room);
	for (const FBattleObservedCondition &Field : Snapshot.GetFieldEffects())
		AppendCondition(TEXT("F"), Field);
	for (const FBattleObservedSide &Side : Snapshot.GetObservedSides())
	{
		Out += FString::Printf(TEXT("S:%u\n"), static_cast<uint8>(Side.Side));
		for (const FBattleObservedCondition &Condition : Side.Conditions)
			AppendCondition(TEXT("C"), Condition);
		for (const FBattleObservedCondition &Hazard : Side.Hazards)
			AppendCondition(TEXT("H"), Hazard);
	}
	for (const FBattlePendingCaptureRecord &Capture : Snapshot.GetPendingCaptures())
	{
		Out += FString::Printf(TEXT("K:%llu/%u/%s/%s/%s/%d/%d/%s/%s\n"), Capture.CaptureOrdinal, static_cast<uint8>(Capture.Destination), *NumericIdText(Capture.OriginalTrainerId), *NumericIdText(Capture.BattlerId),
							   *DefinitionIdText(Capture.SpeciesFormId), Capture.Level, Capture.CurrentHP, *DefinitionIdText(Capture.MajorStatusId), *DefinitionIdText(Capture.HeldItem.CurrentItemId));
	}
	return Out;
}
void RecordCheckpoint(const FBattleEngine &Engine, FRunEvidence &Evidence, const TCHAR *Label)
{
	Evidence.Checkpoints.Add(FString(Label) + TEXT("\n") + SnapshotSignature(Engine.GetSnapshot()));
}
bool SubmitPendingChoices(FBattleEngine &Engine, const FChoiceProvider &Provider, FRunEvidence &Evidence, FString &OutError)
{
	const TArray<FBattleDecisionRequest> Requests = Engine.GetPendingDecisionRequests();
	if (Requests.IsEmpty())
	{
		OutError = TEXT("No pending public decision request was available.");
		return false;
	}
	const FBattleSnapshot Observation = Engine.GetSnapshotForObserver(Requests[0].GetDecisionOwnerTrainerId());
	TArray<FBattleDecision> Decisions;
	for (int32 Index = 0; Index < Requests.Num(); ++Index)
	{
		FBattleActionSelectorInput SelectorInput;
		FBattleRejection Rejection;
		if (!FBattleActionSelectorInput::TryCreate(Observation, Index, SelectorInput, Rejection))
		{
			OutError = TEXT("The filtered selector observation could not bind its exact request.");
			return false;
		}
		BattleCanonicalIntegrationSupportPrivate::FCallbackBattleActionSelector Selector(
			Provider,
			OutError);
		FBattleDecision Selected;
		if (!FBattleActionSelectorBoundary::TrySelectLegalAction(Selector, SelectorInput, Selected, Rejection))
		{
			if (OutError.IsEmpty())
			{
				OutError = FString::Printf(TEXT("The public selector boundary rejected battler %llu with reason %u."), SelectorInput.GetLegalActions().GetActingBattlerId().IsValid() ? SelectorInput.GetLegalActions().GetActingBattlerId().GetValue() : 0,
										   static_cast<uint8>(Rejection.Reason));
			}
			return false;
		}
		Decisions.Add(Selected);
		Evidence.SelectorObservations.Add(SnapshotSignature(Observation));
	}
	if (Requests[0].GetRequestKind() != EBattleDecisionRequestKind::Action && Requests[0].GetRequestKind() != EBattleDecisionRequestKind::MandatoryReplacement)
	{
		if (Decisions.Num() != 1)
		{
			OutError = TEXT("An interrupt decision request did not have exactly one public decision.");
			return false;
		}
		const FBattleResolution Resolution = Engine.SubmitDecision(Decisions[0]);
		if (!Resolution.WasAccepted())
		{
			OutError = FString::Printf(TEXT("SubmitDecision for interrupt request %u rejected with reason %u."), static_cast<uint8>(Requests[0].GetRequestKind()), static_cast<uint8>(Resolution.GetRejection().Reason));
			return false;
		}
		RecordCheckpoint(Engine, Evidence, TEXT("interrupt-decision"));
		return true;
	}
	FBattleDecisionBatchSpec BatchSpec;
	BatchSpec.StateVersion = Requests[0].GetStateVersion();
	BatchSpec.RequestKind = Requests[0].GetRequestKind();
	BatchSpec.DecisionOwnerTrainerId = Requests[0].GetDecisionOwnerTrainerId();
	BatchSpec.Decisions = MoveTemp(Decisions);
	FBattleDecisionBatch Batch;
	FBattleRejection BatchRejection;
	if (!FBattleDecisionBatch::TryCreate(BatchSpec, Batch, BatchRejection))
	{
		OutError = FString::Printf(TEXT("Decision batch construction failed with reason %u."), static_cast<uint8>(BatchRejection.Reason));
		return false;
	}
	const FBattleResolution Resolution = Engine.SubmitDecisionBatch(Batch);
	if (!Resolution.WasAccepted())
	{
		OutError = FString::Printf(TEXT("SubmitDecisionBatch rejected with reason %u."), static_cast<uint8>(Resolution.GetRejection().Reason));
		return false;
	}
	RecordCheckpoint(Engine, Evidence, TEXT("decision"));
	return true;
}
bool LockTurn(FBattleEngine &Engine, const FChoiceProvider &Provider, FRunEvidence &Evidence, FString &OutError)
{
	if (Engine.GetPendingDecisionRequests().IsEmpty())
	{
		FBattleRejection Rejection;
		if (!Engine.TryBeginActionDecisionSequence(Rejection))
		{
			OutError = FString::Printf(TEXT("TryBeginActionDecisionSequence rejected with reason %u."), static_cast<uint8>(Rejection.Reason));
			return false;
		}
		RecordCheckpoint(Engine, Evidence, TEXT("selection"));
	}
	int32 Guard = 0;
	while (!Engine.GetPendingDecisionRequests().IsEmpty() && Guard++ < 16)
	{
		if (!SubmitPendingChoices(Engine, Provider, Evidence, OutError))
		{
			return false;
		}
	}
	if (Guard >= 16 || Engine.GetSnapshot().GetPhase() != EBattlePhase::Locked)
	{
		OutError = TEXT("Normal decisions did not reach the Locked phase within the guard.");
		return false;
	}
	return true;
}
bool ExecuteLockedQueue(FBattleEngine &Engine, FRunEvidence &Evidence, FString &OutError)
{
	int32 Guard = 0;
	while ((Engine.GetSnapshot().GetPhase() == EBattlePhase::Locked || Engine.GetSnapshot().GetPhase() == EBattlePhase::Resolving) && Guard++ < 64)
	{
		if (!Engine.GetPendingDecisionRequests().IsEmpty())
		{
			return true;
		}
		const FBattleResolution Begun = Engine.BeginNextLockedAction();
		if (!Begun.WasAccepted())
		{
			OutError = FString::Printf(TEXT("BeginNextLockedAction rejected with reason %u."), static_cast<uint8>(Begun.GetRejection().Reason));
			return false;
		}
		RecordCheckpoint(Engine, Evidence, TEXT("action-start"));
		const TOptional<FBattleLockedAction> Current = Engine.GetCurrentLockedAction();
		if (!Current.IsSet())
		{
			continue;
		}
		FBattleResolution Resolution;
		switch (Current->Decision.GetActionKind())
		{
		case EBattleActionKind::Fight:
			Resolution = Engine.CommitCurrentMoveAfterPreMoveGates();
			if (!Resolution.WasAccepted())
				break;
			RecordCheckpoint(Engine, Evidence, TEXT("move-commit"));
			if (!Engine.GetCurrentLockedAction().IsSet())
				continue;
			Resolution = Engine.ResolveCurrentMoveTargets();
			if (!Resolution.WasAccepted())
				break;
			RecordCheckpoint(Engine, Evidence, TEXT("targets"));
			if (!Engine.GetCurrentLockedAction().IsSet())
				continue;
			Resolution = Engine.ExecuteCurrentMoveEffects();
			break;
		case EBattleActionKind::Switch:
		case EBattleActionKind::Replacement:
			Resolution = Engine.ExecuteCurrentSwitch();
			break;
		case EBattleActionKind::Bag:
			Resolution = Engine.ExecuteCurrentBagItem();
			break;
		case EBattleActionKind::Run:
		case EBattleActionKind::WildFlee:
			Resolution = Engine.ExecuteCurrentWildAction();
			break;
		default:
			OutError = TEXT("The C11A public driver reached an unsupported current action kind.");
			return false;
		}
		if (!Resolution.WasAccepted())
		{
			OutError = FString::Printf(TEXT("Public action resolver %u rejected with reason %u."), static_cast<uint8>(Current->Decision.GetActionKind()), static_cast<uint8>(Resolution.GetRejection().Reason));
			return false;
		}
		RecordCheckpoint(Engine, Evidence, TEXT("action-resolved"));
	}
	if (Guard >= 64)
	{
		OutError = TEXT("The locked queue exceeded the C11A public-driver guard.");
		return false;
	}
	return true;
}
bool ResolveEndTurn(FBattleEngine &Engine, FRunEvidence &Evidence, FString &OutError)
{
	const FBattleResolution Resolution = Engine.ResolveEndTurn();
	if (!Resolution.WasAccepted())
	{
		OutError = FString::Printf(TEXT("ResolveEndTurn rejected with reason %u."), static_cast<uint8>(Resolution.GetRejection().Reason));
		return false;
	}
	RecordCheckpoint(Engine, Evidence, TEXT("end-turn"));
	return true;
}
bool FinalizeEvidence(const FBattleEngine &Engine, FRunEvidence &Evidence, FString &OutError)
{
	Evidence.Replay = Engine.ExportReplayRecord();
	if (!Evidence.Replay.IsValid())
	{
		OutError = TEXT("ExportReplayRecord returned an invalid schema-6 record.");
		return false;
	}
	FBattleRejection Rejection;
	if (!FBattleReplaySerializer::TrySerializeCanonical(Evidence.Replay, Evidence.ReplayBytes, Rejection))
	{
		OutError = FString::Printf(TEXT("Canonical replay serialization failed with reason %u."), static_cast<uint8>(Rejection.Reason));
		return false;
	}
	Evidence.FinalSnapshotSignature = SnapshotSignature(Engine.GetSnapshot());
	return true;
}
bool RunStrictTwins(FAutomationTestBase &Test, const FString &Label, const FBattleDefinitionCatalog &Catalog, const FBattleSetup &Setup, const TArray<BattleTest::FBattleExpectedRandomDraw> &ExpectedDraws, const FDriveFunction &Drive,
					FRunEvidence *OutFirst)
{
	using namespace BattleCanonicalIntegrationSupportPrivate;
	FRunEvidence First;
	FRunEvidence Second;
	FString Error;
	auto FirstRandom = MakeUnique<FStrictBattleRandom>(ExpectedDraws);
	if (!DriveOne(Catalog, Setup, MoveTemp(FirstRandom), Drive, First, Error))
	{
		Test.AddError(Label + TEXT(" first strict execution: ") + Error);
		return false;
	}
	auto SecondRandom = MakeUnique<FStrictBattleRandom>(ExpectedDraws);
	if (!DriveOne(Catalog, Setup, MoveTemp(SecondRandom), Drive, Second, Error))
	{
		Test.AddError(Label + TEXT(" second strict execution: ") + Error);
		return false;
	}
	const auto HasExactTrace = [&ExpectedDraws](const FRunEvidence &Evidence)
	{
		const TConstArrayView<FBattleRandomDraw> Actual = Evidence.Replay.GetRandomTrace();
		if (Actual.Num() != ExpectedDraws.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < Actual.Num(); ++Index)
		{
			if (Actual[Index].InclusiveMinimum != ExpectedDraws[Index].Minimum || Actual[Index].InclusiveMaximum != ExpectedDraws[Index].Maximum || Actual[Index].Result != ExpectedDraws[Index].Result ||
				Actual[Index].RulePurpose != ExpectedDraws[Index].RulePurpose)
			{
				return false;
			}
		}
		return true;
	};
	if (!Test.TestTrue(Label + TEXT(" first strict RNG trace is exact"), HasExactTrace(First)) || !Test.TestTrue(Label + TEXT(" second strict RNG trace is exact"), HasExactTrace(Second)))
	{
		return false;
	}
	const bool bEqual = CompareEvidence(Test, Label, First, Second);
	if (OutFirst != nullptr)
	{
		*OutFirst = MoveTemp(First);
	}
	return bEqual;
}

bool RunDeterministicTwins(FAutomationTestBase &Test, const FString &Label, const FBattleDefinitionCatalog &Catalog, const FBattleSetup &Setup, const FDriveFunction &Drive, FRunEvidence *OutFirst, const uint64 DiscoverySeed)
{
	FRunEvidence Discovery;
	FString Error;
	if (!BattleCanonicalIntegrationSupportPrivate::DriveOne(Catalog, Setup, MakeUnique<FSeededBattleRandom>(DiscoverySeed), Drive, Discovery, Error)) {
		Test.AddError(Label + TEXT(" RNG-discovery execution: ") + Error);
		return false;
	}
	TArray<BattleTest::FBattleExpectedRandomDraw> Expected;
	for (const FBattleRandomDraw &Draw : Discovery.Replay.GetRandomTrace()) {
		Expected.Add({Draw.InclusiveMinimum, Draw.InclusiveMaximum, Draw.Result, Draw.RulePurpose});
	}
	return RunStrictTwins(Test, Label, Catalog, Setup, Expected, Drive, OutFirst);
}

bool ValidateCoverageManifest(FAutomationTestBase &Test, const FBattleDefinitionCatalog &Catalog)
{
	using namespace BattleCanonicalIntegrationSupportPrivate;
	const TArray<FName> Species =
		Names({TEXT("Species.Charizard"), TEXT("Species.Clefable"), TEXT("Species.Espathra"), TEXT("Species.Excadrill"), TEXT("Species.Gyarados"), TEXT("Species.Pelipper"), TEXT("Species.Rotom"), TEXT("Species.Venusaur")});
	const TArray<FName> Natures =
		Names({TEXT("Nature.Adamant"), TEXT("Nature.Bashful"), TEXT("Nature.Bold"),	 TEXT("Nature.Brave"),	 TEXT("Nature.Calm"),	TEXT("Nature.Careful"), TEXT("Nature.Docile"), TEXT("Nature.Gentle"), TEXT("Nature.Hardy"),
			   TEXT("Nature.Hasty"),   TEXT("Nature.Impish"),  TEXT("Nature.Jolly"), TEXT("Nature.Lax"),	 TEXT("Nature.Lonely"), TEXT("Nature.Mild"),	TEXT("Nature.Modest"), TEXT("Nature.Naive"),  TEXT("Nature.Naughty"),
			   TEXT("Nature.Quiet"),   TEXT("Nature.Quirky"),  TEXT("Nature.Rash"),	 TEXT("Nature.Relaxed"), TEXT("Nature.Sassy"),	TEXT("Nature.Serious"), TEXT("Nature.Timid")});
	const TArray<FName> Moves = Names({TEXT("Move.AuroraVeil"),		TEXT("Move.Bite"),			TEXT("Move.BrickBreak"),	  TEXT("Move.BulletSeed"),	TEXT("Move.ConfuseRay"),   TEXT("Move.Defog"),		  TEXT("Move.Disable"),
									   TEXT("Move.DoubleEdge"),		TEXT("Move.Earthquake"),	TEXT("Move.ElectricTerrain"), TEXT("Move.Encore"),		TEXT("Move.Flamethrower"), TEXT("Move.Fly"),		  TEXT("Move.FollowMe"),
									   TEXT("Move.GigaDrain"),		TEXT("Move.GrassyTerrain"), TEXT("Move.HelpingHand"),	  TEXT("Move.HyperBeam"),	TEXT("Move.IceBeam"),	   TEXT("Move.KnockOff"),	  TEXT("Move.LeechSeed"),
									   TEXT("Move.LightScreen"),	TEXT("Move.MagicRoom"),		TEXT("Move.MeanLook"),		  TEXT("Move.Mist"),		TEXT("Move.MistyTerrain"), TEXT("Move.PoisonPowder"), TEXT("Move.Protect"),
									   TEXT("Move.PsychicTerrain"), TEXT("Move.QuickAttack"),	TEXT("Move.RainDance"),		  TEXT("Move.RapidSpin"),	TEXT("Move.Recover"),	   TEXT("Move.Recycle"),	  TEXT("Move.Reflect"),
									   TEXT("Move.Roar"),			TEXT("Move.Safeguard"),		TEXT("Move.Sandstorm"),		  TEXT("Move.SleepPowder"), TEXT("Move.Snowscape"),	   TEXT("Move.SolarBeam"),	  TEXT("Move.Spikes"),
									   TEXT("Move.StealthRock"),	TEXT("Move.StickyWeb"),		TEXT("Move.Substitute"),	  TEXT("Move.SunnyDay"),	TEXT("Move.Swift"),		   TEXT("Move.SwordsDance"),  TEXT("Move.Tailwind"),
									   TEXT("Move.Taunt"),			TEXT("Move.Thief"),			TEXT("Move.Thunder"),		  TEXT("Move.ThunderWave"), TEXT("Move.Toxic"),		   TEXT("Move.ToxicSpikes"),  TEXT("Move.Trick"),
									   TEXT("Move.TrickRoom"),		TEXT("Move.Uturn"),			TEXT("Move.VineWhip"),		  TEXT("Move.WillOWisp"),	TEXT("Move.WonderRoom"),   TEXT("Move.Wrap")});
	const TArray<FName> Abilities =
		Names({TEXT("Ability.Blaze"), TEXT("Ability.Drizzle"), TEXT("Ability.Intimidate"), TEXT("Ability.Levitate"), TEXT("Ability.MagicGuard"), TEXT("Ability.MoldBreaker"), TEXT("Ability.Overgrow"), TEXT("Ability.SpeedBoost")});
	const TArray<FName> Items = Names({TEXT("Item.AirBalloon"), TEXT("Item.ChoiceBand"), TEXT("Item.FocusSash"), TEXT("Item.FullHeal"), TEXT("Item.HeavyDutyBoots"), TEXT("Item.HyperPotion"), TEXT("Item.Leftovers"), TEXT("Item.LifeOrb"),
									   TEXT("Item.LumBerry"), TEXT("Item.PokeBall"), TEXT("Item.QuickClaw"), TEXT("Item.Revive"), TEXT("Item.SitrusBerry"), TEXT("Item.XAttack")});
	const TArray<FName> Conditions = Names({TEXT("Condition.AuroraVeil"),	TEXT("Condition.Burn"),			 TEXT("Condition.Charging"),
											TEXT("Condition.Confusion"),	TEXT("Condition.Disable"),		 TEXT("Condition.ElectricTerrain"),
											TEXT("Condition.Encore"),		TEXT("Condition.Flinch"),		 TEXT("Condition.FlySemiInvulnerable"),
											TEXT("Condition.Freeze"),		TEXT("Condition.GrassyTerrain"), TEXT("Condition.LeechSeed"),
											TEXT("Condition.LightScreen"),	TEXT("Condition.MagicRoom"),	 TEXT("Condition.Mist"),
											TEXT("Condition.MistyTerrain"), TEXT("Condition.Paralysis"),	 TEXT("Condition.PartialTrap"),
											TEXT("Condition.Poison"),		TEXT("Condition.Protect"),		 TEXT("Condition.PsychicTerrain"),
											TEXT("Condition.Rain"),			TEXT("Condition.Recharge"),		 TEXT("Condition.Reflect"),
											TEXT("Condition.Safeguard"),	TEXT("Condition.Sandstorm"),	 TEXT("Condition.Sleep"),
											TEXT("Condition.Snow"),			TEXT("Condition.Spikes"),		 TEXT("Condition.StealthRock"),
											TEXT("Condition.StickyWeb"),	TEXT("Condition.Substitute"),	 TEXT("Condition.Sun"),
											TEXT("Condition.Tailwind"),		TEXT("Condition.Taunt"),		 TEXT("Condition.Toxic"),
											TEXT("Condition.ToxicSpikes"),	TEXT("Condition.Trap"),			 TEXT("Condition.TrickRoom"),
											TEXT("Condition.WonderRoom")});
	TMap<FName, FString> Coverage; const auto Add = [&Coverage](const FName Id, const TCHAR *Path, const TCHAR *Subcase) { Coverage.Add(Id, FString(Path) + TEXT(" :: ") + Subcase + TEXT(" :: ") + Id.ToString()); };
	for (const FName Id : Species) Add(Id, TEXT("PokemonSolarus.Battle.C11A.Single.Baseline.ProductionCatalogAndCalculatorFixtures"), TEXT("level-50 row across all 25 nature modifiers"));
	for (const FName Id : Natures) Add(Id, TEXT("PokemonSolarus.Battle.C11A.Single.Baseline.ProductionCatalogAndCalculatorFixtures"), TEXT("modifier row across all 8 production species"));
	for (const FName Id : Moves) Add(Id, TEXT("PokemonSolarus.Battle.C11A.Single.Status.AllMajorVolatileAndCrossInteractions"), TEXT("ID-labeled twin MoveUsed effect-path and exact-once completion"));
	for (const FName Id : Abilities) {
		const TCHAR *Path = Id == TEXT("Ability.Blaze") ? TEXT("PokemonSolarus.Battle.C11A.ReplayInvariants.ObserverSnapshotsEventsAndHiddenInformation") : Id == TEXT("Ability.Levitate") ? TEXT("PokemonSolarus.Battle.C11A.Single.Conditions.HazardsGroundingItemsSwitchAndFaint") : Id == TEXT("Ability.Overgrow") || Id == TEXT("Ability.MoldBreaker") ? TEXT("PokemonSolarus.Battle.C11A.Single.Modifiers.B00BOrderTraceHpRevealAndCleanup") : TEXT("PokemonSolarus.Battle.C11A.Single.BagAbilityItem.ActionsTriggersOwnershipCleanupAndBlockedPaths");
		const TCHAR *Subcase = Id == TEXT("Ability.Blaze") ? TEXT("low-HP public reveal and damage activation") : Id == TEXT("Ability.Levitate") ? TEXT("switch-in hazard grounding exception") : Id == TEXT("Ability.Overgrow") ? TEXT("B00B modifier trace row") : Id == TEXT("Ability.MoldBreaker") ? TEXT("public Ground damage through Levitate while the control remains immune") : TEXT("ID-specific entry or end-turn activation matrix"); Add(Id, Path, Subcase); }
	for (const FName Id : Items) {
		const TCHAR *Path = Id == TEXT("Item.PokeBall") ? TEXT("PokemonSolarus.Battle.C11A.WildPartner.Capture.CapacityFailureSuccessMultipleCancellationAndDestinations") : Id == TEXT("Item.QuickClaw") ? TEXT("PokemonSolarus.Battle.C11A.Double.Order.PrioritySpeedTiesTrickRoomQuickClawAndLockedStability") : Id == TEXT("Item.AirBalloon") || Id == TEXT("Item.HeavyDutyBoots") ? TEXT("PokemonSolarus.Battle.C11A.Single.Conditions.HazardsGroundingItemsSwitchAndFaint") : TEXT("PokemonSolarus.Battle.C11A.Single.BagAbilityItem.ActionsTriggersOwnershipCleanupAndBlockedPaths");
		const TCHAR *Subcase = Id == TEXT("Item.PokeBall") ? TEXT("normal critical and guaranteed capture matrix") : Id == TEXT("Item.QuickClaw") ? TEXT("strict fractional-priority activation") : Id == TEXT("Item.AirBalloon") || Id == TEXT("Item.HeavyDutyBoots") ? TEXT("switch-in hazard exception") : TEXT("ID-specific Bag held-trigger or move-operation matrix"); Add(Id, Path, Subcase); }
	for (const FBattleConditionDefinition &Condition : Catalog.GetConditions())
	{
		const bool bBattlerCondition = Condition.Kind == EBattleConditionKind::MajorStatus
			|| Condition.Kind == EBattleConditionKind::Volatile;
		if (bBattlerCondition)
		{
			Add(
				Condition.Id.GetDefinitionId().GetName(),
				TEXT("PokemonSolarus.Battle.C11A.Single.Status.AllMajorVolatileAndCrossInteractions"),
				TEXT("major-status or ID-specific volatile mutation and behavior matrix"));
		}
	}
	const TCHAR *FieldPath = TEXT("PokemonSolarus.Battle.C11A.Single.Conditions.FieldSideLifecycleReplacementRemovalExpiryVisibility");
	const TCHAR *HazardPath = TEXT("PokemonSolarus.Battle.C11A.Single.Conditions.HazardsGroundingItemsSwitchAndFaint");
	Add(TEXT("Condition.Sun"), FieldPath, TEXT("Fire damage HP delta is greater than the no-weather control"));
	Add(TEXT("Condition.Rain"), FieldPath, TEXT("Fire damage HP delta is lower than the no-weather control"));
	Add(TEXT("Condition.Sandstorm"), FieldPath, TEXT("end-turn HP loss with Ground-Steel immune control"));
	Add(TEXT("Condition.Snow"), FieldPath, TEXT("Solar Beam half-power HP delta; Ice-defense branch remains catalog-deferred"));
	Add(TEXT("Condition.ElectricTerrain"), FieldPath, TEXT("grounded Sleep prevention with no-terrain status control"));
	Add(TEXT("Condition.GrassyTerrain"), FieldPath, TEXT("grounded end-turn healing with unchanged control HP"));
	Add(TEXT("Condition.MistyTerrain"), FieldPath, TEXT("grounded Toxic prevention with no-terrain status control"));
	Add(TEXT("Condition.PsychicTerrain"), FieldPath, TEXT("opposing Quick Attack blocked against grounded target with damage control"));
	Add(TEXT("Condition.Reflect"), FieldPath, TEXT("physical damage HP delta is lower than the no-screen control"));
	Add(TEXT("Condition.LightScreen"), FieldPath, TEXT("special damage HP delta is lower than the no-screen control"));
	Add(TEXT("Condition.AuroraVeil"), FieldPath, TEXT("special damage HP delta is lower under a valid Snow setup"));
	Add(TEXT("Condition.TrickRoom"), TEXT("PokemonSolarus.Battle.C11A.Double.Order.PrioritySpeedTiesTrickRoomQuickClawAndLockedStability"), TEXT("next-turn locked actor order reverses all four production Speed values"));
	Add(TEXT("Condition.MagicRoom"), FieldPath, TEXT("Choice Band damage is suppressed and restored after room toggle removal"));
	Add(TEXT("Condition.WonderRoom"), FieldPath, TEXT("Defense-Special Defense swap produces a distinguishable engine HP delta"));
	Add(TEXT("Condition.Safeguard"), FieldPath, TEXT("opponent Toxic is prevented while the no-Safeguard control receives it"));
	Add(TEXT("Condition.Mist"), FieldPath, TEXT("opposing Sticky Web Speed drop is prevented while the control reaches minus one"));
	Add(TEXT("Condition.Tailwind"), FieldPath, TEXT("doubled Speed changes the next locked-action actor order"));
	Add(TEXT("Condition.Spikes"), HazardPath, TEXT("three-layer grounded switch-in HP loss with airborne and Boots controls"));
	Add(TEXT("Condition.ToxicSpikes"), HazardPath, TEXT("two-layer grounded switch-in major status with airborne and Boots controls"));
	Add(TEXT("Condition.StealthRock"), HazardPath, TEXT("type-aware switch-in HP loss with Boots control and switch-in faint"));
	Add(TEXT("Condition.StickyWeb"), HazardPath, TEXT("grounded switch-in Speed drop with airborne Boots and Mist controls"));

	bool bValid = true;
	Test.AddInfo(FString::Printf(TEXT("C11A completion status: %s"), C11ACompletionStatus));
	Test.AddInfo(FString::Printf(TEXT("C11B implementation eligibility: %s"), C11BEligibilityStatus));
	const auto HasSpeciesType = [&Catalog](const EPokemonType Type)
	{
		return Catalog.GetSpeciesForms().ContainsByPredicate(
			[Type](const FBattleSpeciesFormDefinition &Species)
			{
				return Species.PrimaryType == Type || Species.SecondaryType == Type;
			});
	};
	const auto HasDamagingMoveType = [&Catalog](const EPokemonType Type)
	{
		return Catalog.GetMoves().ContainsByPredicate(
			[Type](const FBattleMoveDefinition &Move)
			{
				return Move.Type == Type
					&& Move.Category != EBattleMoveCategory::Status
					&& Move.Power > 0;
			});
	};
	const bool bHasDamagingBypassMove = Catalog.GetMoves().ContainsByPredicate(
		[](const FBattleMoveDefinition &Move)
		{
			return Move.Category != EBattleMoveCategory::Status
				&& Move.Power > 0
				&& EnumHasAllFlags(Move.Flags, EBattleMoveFlags::BypassesSideProtection);
		});
	const bool bHasStatusBypassMove = Catalog.GetMoves().ContainsByPredicate(
		[&Catalog](const FBattleMoveDefinition &Move)
		{
			if (!EnumHasAllFlags(Move.Flags, EBattleMoveFlags::BypassesSideProtection))
			{
				return false;
			}
			return Move.Effects.ContainsByPredicate(
				[&Catalog](const FBattleMoveEffectDescriptor &Effect)
				{
					const FBattleConditionDefinition *Condition =
						Catalog.FindCondition(Effect.ConditionId);
					return Effect.Kind == EBattleMoveEffectKind::ApplyCondition
						&& Condition != nullptr
						&& Condition->Kind == EBattleConditionKind::MajorStatus;
				});
		});
	const bool bHasExtendedConditionEffect = Catalog.GetMoves().ContainsByPredicate(
		[](const FBattleMoveDefinition &Move)
		{
			return Move.Effects.ContainsByPredicate(
				[](const FBattleMoveEffectDescriptor &Effect)
				{
					return Effect.DurationTurns == 8;
				});
		});
	struct FDeferredGap
	{
		const TCHAR *Id;
		const TCHAR *Code;
		const TCHAR *MissingData;
		const TCHAR *Retest;
	};
	const FDeferredGap DeferredGaps[] = {
		{TEXT("C11A-DATA-001"), TEXT("TryGetWeatherDirectDefensiveModifierQ12 Snow physical-defense branch"), TEXT("accepted catalog has no Ice species/form"), TEXT("compare the same physical hit against an Ice defender with and without Snow")},
		{TEXT("C11A-DATA-002"), TEXT("TryGetWeatherDirectDefensiveModifierQ12 Sandstorm special-defense branch"), TEXT("accepted catalog has no Rock species/form"), TEXT("compare the same special hit against a Rock defender with and without Sandstorm")},
		{TEXT("C11A-DATA-003"), TEXT("TryGetWeatherDamageModifierQ12 Water branches"), TEXT("accepted catalog has no damaging Water move"), TEXT("compare Water damage in Sun Rain and neutral weather")},
		{TEXT("C11A-DATA-004"), TEXT("TryGetTerrainFinalDamageModifierQ12 Misty Dragon branch"), TEXT("accepted catalog has no damaging Dragon move"), TEXT("compare Dragon damage against a grounded target with and without Misty Terrain")},
		{TEXT("C11A-DATA-005"), TEXT("screen and Safeguard BypassesSideProtection branches"), TEXT("accepted catalog has no damaging bypass move and no major-status bypass move"), TEXT("exercise both bypass exceptions through the public engine after catalog expansion")},
		{TEXT("C11A-DATA-006"), TEXT("eight-turn field-side duration extension"), TEXT("accepted catalog has no set-condition effect authored with duration 8"), TEXT("assert extended duration creation decrement and expiry after a qualifying descriptor is approved")}};
	for (const FDeferredGap &Gap : DeferredGaps)
	{
		Test.AddInfo(FString::Printf(
			TEXT("%s UNVERIFIED_DUE_TO_CATALOG_DATA | %s | %s | later: %s"),
			Gap.Id,
			Gap.Code,
			Gap.MissingData,
			Gap.Retest));
	}
	bValid &= Test.TestFalse(TEXT("C11A-DATA-001 remains deferred until an Ice species exists"), HasSpeciesType(EPokemonType::Ice));
	bValid &= Test.TestFalse(TEXT("C11A-DATA-002 remains deferred until a Rock species exists"), HasSpeciesType(EPokemonType::Rock));
	bValid &= Test.TestFalse(TEXT("C11A-DATA-003 remains deferred until a damaging Water move exists"), HasDamagingMoveType(EPokemonType::Water));
	bValid &= Test.TestFalse(TEXT("C11A-DATA-004 remains deferred until a damaging Dragon move exists"), HasDamagingMoveType(EPokemonType::Dragon));
	bValid &= Test.TestFalse(TEXT("C11A-DATA-005 damaging bypass remains deferred"), bHasDamagingBypassMove);
	bValid &= Test.TestFalse(TEXT("C11A-DATA-005 status bypass remains deferred"), bHasStatusBypassMove);
	bValid &= Test.TestFalse(TEXT("C11A-DATA-006 extended duration remains deferred"), bHasExtendedConditionEffect);
	bValid &= ValidateDefinitionSet(Test, TEXT("Species"), Catalog.GetSpeciesForms(), Species, [](const FBattleSpeciesFormDefinition &Value) { return Value.Id.GetDefinitionId().GetName(); });
	bValid &= ValidateDefinitionSet(Test, TEXT("Nature"), Catalog.GetNatures(), Natures, [](const FBattleNatureDefinition &Value) { return Value.Id.GetDefinitionId().GetName(); });
	bValid &= ValidateDefinitionSet(Test, TEXT("Move"), Catalog.GetMoves(), Moves, [](const FBattleMoveDefinition &Value) { return Value.Id.GetDefinitionId().GetName(); });
	bValid &= ValidateDefinitionSet(Test, TEXT("Ability"), Catalog.GetAbilities(), Abilities, [](const FBattleAbilityDefinition &Value) { return Value.Id.GetDefinitionId().GetName(); });
	bValid &= ValidateDefinitionSet(Test, TEXT("Item"), Catalog.GetItems(), Items, [](const FBattleItemDefinition &Value) { return Value.Id.GetDefinitionId().GetName(); });
	bValid &= ValidateDefinitionSet(Test, TEXT("Condition"), Catalog.GetConditions(), Conditions, [](const FBattleConditionDefinition &Value) { return Value.Id.GetDefinitionId().GetName(); });
	bValid &= Test.TestEqual(TEXT("Coverage manifest maps every catalog definition ID once"), Coverage.Num(), 157);
	for (const TPair<FName, FString> &Entry : Coverage) {
		bValid &= Test.TestTrue(TEXT("Every coverage row names a C11A test, subcase, and exact ID"), Entry.Value.StartsWith(TEXT("PokemonSolarus.Battle.C11A.")) && Entry.Value.Contains(TEXT(" :: ")) && Entry.Value.EndsWith(Entry.Key.ToString()));
	}
	int32 TypePairs = 0;
	for (int32 Attack = 0; Attack < FBattleTypeChart::TypeCount; ++Attack)
	{
		for (int32 Defense = 0; Defense < FBattleTypeChart::TypeCount; ++Defense)
		{
			FBattleTypeEffectiveness Effectiveness;
			if (Catalog.GetTypeChart().TryGetEffectiveness(static_cast<EPokemonType>(Attack), static_cast<EPokemonType>(Defense), Effectiveness))
			{
				++TypePairs;
			}
		}
	}
	bValid &= Test.TestEqual(TEXT("Type coverage manifest has 18 types"), FBattleTypeChart::TypeCount, 18);
	bValid &= Test.TestEqual(TEXT("Type coverage manifest has all 324 ordered pairs"), TypePairs, 324);
	bValid &= Test.TestNull(TEXT("Struggle is not a production catalog row"), Catalog.FindMove(MakeDefinitionId<FMoveId>(TEXT("Move.Struggle"))));
	return bValid;
}

bool ValidateGlobalInvariants(FAutomationTestBase &Test, const FBattleDefinitionCatalog &Catalog, const FRunEvidence &Evidence, const FString &Label)
{
	bool bValid = Test.TestTrue(Label + TEXT(" replay record is valid"), Evidence.Replay.IsValid());
	const FBattleSnapshot &Snapshot = Evidence.Replay.GetFinalSnapshot();
	for (const FBattlePartyEntrySetup &Entry : Snapshot.GetPartyEntries())
	{
		bValid &= Test.TestTrue(Label + TEXT(" HP remains in bounds"), Entry.CurrentHP >= 0 && Entry.CurrentHP <= Entry.Stats.MaxHP);
		bValid &= Test.TestTrue(Label + TEXT(" permanent stats stay positive"),
								Entry.Stats.MaxHP > 0 && Entry.Stats.Attack > 0 && Entry.Stats.Defense > 0 && Entry.Stats.SpecialAttack > 0 && Entry.Stats.SpecialDefense > 0 && Entry.Stats.Speed > 0);
		bValid &= Test.TestNotNull(Label + TEXT(" species remains catalog-backed"), Catalog.FindSpeciesForm(Entry.SpeciesFormId));
		bValid &= Test.TestNotNull(Label + TEXT(" Ability remains catalog-backed"), Catalog.FindAbility(Entry.AbilityId));
		for (const FBattleMoveSlotSetup &Move : Entry.Moves)
		{
			bValid &= Test.TestTrue(Label + TEXT(" PP remains in bounds"), Move.CurrentPP >= 0 && Move.CurrentPP <= Move.MaxPP);
			bValid &= Test.TestNotNull(Label + TEXT(" move remains catalog-backed"), Catalog.FindMove(Move.MoveId));
		}
	}
	for (const FBattleTrainerSetup &Trainer : Snapshot.GetTrainers())
	{
		for (const FBattleBagItemCount &Item : Trainer.Bag)
		{
			bValid &= Test.TestTrue(Label + TEXT(" Bag counts remain non-negative"), Item.Count >= 0);
		}
	}
	for (const FBattleObservedBattler &Battler : Snapshot.GetObservedBattlers())
	{
		for (uint8 Index = 0; Index < 7; ++Index)
		{
			int32 Stage = 0;
			bValid &=
				Battler.StatStages.TryGetStage(static_cast<EBattleStat>(Index), Stage) && Test.TestTrue(Label + TEXT(" stat stages remain in bounds"), Stage >= FBattleStatStages::MinimumStage && Stage <= FBattleStatStages::MaximumStage);
		}
	}
	const auto ValidateCondition = [&Test, &Label, &bValid](const FBattleObservedCondition &Condition) {
		bValid &= Test.TestTrue(Label + TEXT(" condition duration remains in bounds"), !Condition.RemainingTurns.IsSet() || Condition.RemainingTurns.GetValue() >= 0);
		bValid &= Test.TestTrue(Label + TEXT(" condition layers remain in bounds"), Condition.LayerCount >= 0 && Condition.LayerCount <= 3);
	};
	if (Snapshot.GetWeather().IsSet()) ValidateCondition(Snapshot.GetWeather().GetValue());
	if (Snapshot.GetTerrain().IsSet()) ValidateCondition(Snapshot.GetTerrain().GetValue());
	for (const FBattleObservedCondition &Condition : Snapshot.GetRooms()) ValidateCondition(Condition);
	for (const FBattleObservedSide &Side : Snapshot.GetObservedSides()) {
		for (const FBattleObservedCondition &Condition : Side.Conditions) ValidateCondition(Condition);
		for (const FBattleObservedCondition &Condition : Side.Hazards) ValidateCondition(Condition);
	}
	TMap<uint64, int32> StartsByAction;
	TMap<uint64, int32> CompletesByAction;
	for (const FBattleResolution &Resolution : Evidence.Replay.GetResolutions()) {
		for (const FBattleEvent &Event : Resolution.GetEvents()) {
			if (Event.GetType() == EBattleEventType::ActionStarted && Event.GetActionId().IsValid())
				++StartsByAction.FindOrAdd(Event.GetActionId().GetValue());
			if (Event.GetType() == EBattleEventType::ActionCompleted && Event.GetActionId().IsValid()) ++CompletesByAction.FindOrAdd(Event.GetActionId().GetValue());
		}
	}
	for (const TPair<uint64, int32> &Pair : StartsByAction) {
		bValid &= Test.TestEqual(Label + TEXT(" accepted actions start at most once"), Pair.Value, 1);
		bValid &= Test.TestTrue(Label + TEXT(" accepted actions complete at most once"), CompletesByAction.FindRef(Pair.Key) <= 1);
	}
	for (const TPair<uint64, int32> &Pair : CompletesByAction) {
		bValid &= Test.TestEqual(Label + TEXT(" accepted actions complete at most once"), Pair.Value, 1);
	}
	return bValid;
}

bool HasEvent(const FBattleReplayRecord &Record, const EBattleEventType Type)
{
	return FindEvent(Record, Type) != nullptr;
}
int32 CountEvents(const FBattleReplayRecord &Record, const EBattleEventType Type)
{
	int32 Count = 0;
	for (const FBattleResolution &Resolution : Record.GetResolutions())
	{
		for (const FBattleEvent &Event : Resolution.GetEvents())
		{
			Count += Event.GetType() == Type ? 1 : 0;
		}
	}
	return Count;
}
const FBattleEvent *FindEvent(const FBattleReplayRecord &Record, const EBattleEventType Type, const bool bLast)
{
	const FBattleEvent *Found = nullptr;
	for (const FBattleResolution &Resolution : Record.GetResolutions())
	{
		for (const FBattleEvent &Event : Resolution.GetEvents())
		{
			if (Event.GetType() == Type)
			{
				Found = &Event;
				if (!bLast)
					return Found;
			}
		}
	}
	return Found;
}
} // namespace BattleCanonicalIntegrationTestSupport
#endif // WITH_DEV_AUTOMATION_TESTS
