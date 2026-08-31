#pragma once

#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Battle/BattleDefinitionCatalog.h"
#include "Battle/BattleEngine.h"
#include "Battle/BattleFinalDamageCalculator.h"
#include "Battle/BattleStatCalculator.h"
#include "BattleTestRandom.h"

class FAutomationTestBase;

namespace BattleCanonicalIntegrationTestSupport
{
	template <typename IdType>
	IdType MakeNumericId(const uint64 Value)
	{
		IdType Id;
		check(IdType::TryCreate(Value, Id));
		return Id;
	}

	template <typename IdType>
	IdType MakeDefinitionId(const TCHAR* Value)
	{
		IdType Id;
		check(IdType::TryCreate(FName(Value), Id));
		return Id;
	}

	FPartySlotId MakePartySlotId(int32 Index);
	FActiveSlotId MakeActiveSlotId(EBattleSide Side, EBattlePosition Position);

	struct FCatalogFixture
	{
		FBattleDefinitionCatalog Catalog;
		FBattleSetup ProductionSetup;
		FTrainerId LocalTrainerId;
	};

	struct FTrainerSpec
	{
		uint64 TrainerValue = 0;
		EBattleSide Side = EBattleSide::Player;
		EBattleTrainerRole Role = EBattleTrainerRole::Player;
		EBattleDecisionController Controller = EBattleDecisionController::Human;
		TArray<TPair<FName, int32>> Bag;
	};

	struct FBattlerSpec
	{
		uint64 TrainerValue = 0;
		uint64 BattlerValue = 0;
		int32 PartyIndex = 0;
		FName SpeciesId = FName(TEXT("Species.Charizard"));
		FName NatureId = FName(TEXT("Nature.Hardy"));
		FName AbilityId = FName(TEXT("Ability.Blaze"));
		TArray<FName> MoveIds;
		TArray<int32> MoveCurrentPP;
		int32 Level = 50;
		int32 CurrentHP = INDEX_NONE;
		FPokemonStatValues IndividualValues{31, 31, 31, 31, 31, 31};
		FPokemonStatValues EffortValues;
		FName OriginalHeldItemId = NAME_None;
		FName CurrentHeldItemId = NAME_None;
		EBattleCaptureSpeciesClassification CaptureClassification =
			EBattleCaptureSpeciesClassification::Normal;
	};

	struct FActiveSpec
	{
		EBattleSide Side = EBattleSide::Player;
		EBattlePosition Position = EBattlePosition::Left;
		uint64 TrainerValue = 0;
		uint64 BattlerValue = 0;
	};

	struct FObedienceSpec
	{
		uint64 BattlerValue = 0;
		bool bSubjectToPlayerCap = true;
		uint8 ReferenceLevel = 50;
		uint8 BadgeCount = 8;
	};

	struct FSetupSpec
	{
		uint64 BattleValue = 11001;
		EBattleEncounterKind EncounterKind = EBattleEncounterKind::Trainer;
		EBattleFormat Format = EBattleFormat::Single;
		TArray<FTrainerSpec> Trainers;
		TArray<FBattlerSpec> Battlers;
		TArray<FActiveSpec> Active;
		TArray<FObedienceSpec> Obedience;
		TArray<FBattleKnowledgeFact> Knowledge;
		FBattleCaptureCapacitySnapshot CaptureCapacity{0, 0};
		FBattleCaptureProgressionSnapshot CaptureProgression;
		FBattleEncounterPolicies Policies;
	};

	enum class EChoiceKind : uint8
	{
		Fight,
		Bag,
		Switch,
		Run,
		WildFlee,
		Replacement,
		ShiftAccept,
		ShiftDecline
	};

	struct FChoice
	{
		EChoiceKind Kind = EChoiceKind::Fight;
		FName DefinitionId = NAME_None;
		FActiveSlotId ActiveTarget;
		FPartySlotId PartyTarget;
	};

	struct FRunEvidence
	{
		TArray<FString> Checkpoints;
		TArray<FString> SelectorObservations;
		FBattleReplayRecord Replay;
		TArray<uint8> ReplayBytes;
		FString FinalSnapshotSignature;
	};

	using FChoiceProvider =
		TFunction<bool(const FBattleDecisionRequest&, FChoice&, FString&)>;
	using FDriveFunction =
		TFunction<bool(FBattleEngine&, FRunEvidence&, FString&)>;

	bool TryLoadProductionFixture(
		FAutomationTestBase& Test,
		FCatalogFixture& OutFixture,
		FString& OutError);

	bool TryBuildSetup(
		const FBattleDefinitionCatalog& Catalog,
		const FSetupSpec& Spec,
		FBattleSetup& OutSetup,
		FString& OutError);

	bool TryMakeDecision(
		const FBattleDecisionRequest& Request,
		const FChoice& Choice,
		FBattleDecision& OutDecision,
		FString& OutError);

	void RecordCheckpoint(
		const FBattleEngine& Engine,
		FRunEvidence& Evidence,
		const TCHAR* Label);

	bool SubmitPendingChoices(
		FBattleEngine& Engine,
		const FChoiceProvider& Provider,
		FRunEvidence& Evidence,
		FString& OutError);

	bool LockTurn(
		FBattleEngine& Engine,
		const FChoiceProvider& Provider,
		FRunEvidence& Evidence,
		FString& OutError);

	bool ExecuteLockedQueue(
		FBattleEngine& Engine,
		FRunEvidence& Evidence,
		FString& OutError);

	bool ResolveEndTurn(
		FBattleEngine& Engine,
		FRunEvidence& Evidence,
		FString& OutError);

	bool FinalizeEvidence(
		const FBattleEngine& Engine,
		FRunEvidence& Evidence,
		FString& OutError);

	bool RunDeterministicTwins(
		FAutomationTestBase& Test,
		const FString& Label,
		const FBattleDefinitionCatalog& Catalog,
		const FBattleSetup& Setup,
		const FDriveFunction& Drive,
		FRunEvidence* OutFirst = nullptr,
		uint64 DiscoverySeed = 0xC11A5EEDULL);

	bool RunStrictTwins(
		FAutomationTestBase& Test,
		const FString& Label,
		const FBattleDefinitionCatalog& Catalog,
		const FBattleSetup& Setup,
		const TArray<BattleTest::FBattleExpectedRandomDraw>& ExpectedDraws,
		const FDriveFunction& Drive,
		FRunEvidence* OutFirst = nullptr);

	bool ValidateCoverageManifest(
		FAutomationTestBase& Test,
		const FBattleDefinitionCatalog& Catalog);

	bool ValidateGlobalInvariants(
		FAutomationTestBase& Test,
		const FBattleDefinitionCatalog& Catalog,
		const FRunEvidence& Evidence,
		const FString& Label);

	bool HasEvent(const FBattleReplayRecord& Record, EBattleEventType Type);
	int32 CountEvents(const FBattleReplayRecord& Record, EBattleEventType Type);
	const FBattleEvent* FindEvent(
		const FBattleReplayRecord& Record,
		EBattleEventType Type,
		bool bLast = false);
	FString SnapshotSignature(const FBattleSnapshot& Snapshot);
}

#endif // WITH_DEV_AUTOMATION_TESTS
