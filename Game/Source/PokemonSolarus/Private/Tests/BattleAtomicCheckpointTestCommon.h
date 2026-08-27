#pragma once

#if WITH_DEV_AUTOMATION_TESTS

#include "Battle/BattleAbility.h"
#include "Battle/BattleBagItem.h"
#include "Battle/BattleCapture.h"
#include "Battle/BattleEngine.h"
#include "Battle/BattleFieldSideConditions.h"
#include "Battle/BattleItem.h"
#include "Battle/BattleMajorStatus.h"
#include "Battle/BattleReplay.h"
#include "Battle/BattleState.h"
#include "Battle/BattleSwitching.h"
#include "Battle/BattleVolatile.h"
#include "Battle/BattleWildFlow.h"
#include "Battle/BattleFaintOutcomeResolver.h"
#include "Battle/BattlePartnerFlow.h"
#include "BattleAtomicCheckpointTestHarness.h"
#include "BattleTestFactories.h"
#include "BattleTestRandom.h"
#include "Math/NumericLimits.h"
#include "Misc/AutomationTest.h"

namespace BattleAtomicCheckpointTestCommonPrivate
{
	using BattleTest::FScriptedBattleRandomBase;
	using BattleTest::FSequenceBattleRandom;
	using BattleTest::FBattleExpectedRandomDraw;
	using BattleTest::FStrictBattleRandom;
	using BattleTest::MakeActiveSlotId;
	using BattleTest::MakeDefinitionId;
	using BattleTest::MakeNumericId;
	using BattleTest::MakePartySlotId;

	extern const uint64 PlayerTrainerValue;
	extern const uint64 OpponentTrainerValue;
	extern const uint64 PartnerTrainerValue;
	extern const uint64 PlayerLeftValue;
	extern const uint64 PlayerRightValue;
	extern const uint64 PlayerReserveValue;
	extern const uint64 PlayerSecondReserveValue;
	extern const uint64 OpponentLeftValue;
	extern const uint64 OpponentRightValue;
	extern const uint64 OpponentReserveValue;

	extern const TCHAR* const PlayerSpeciesName;
	extern const TCHAR* const WildSpeciesName;
	extern const TCHAR* const ProbeMoveName;
	extern const TCHAR* const TargetProbeMoveName;
	extern const TCHAR* const PivotProbeMoveName;
	extern const TCHAR* const ThawProbeMoveName;
	extern const TCHAR* const ChargeProbeMoveName;
	extern const TCHAR* const RandomTargetProbeMoveName;
	extern const TCHAR* const RandomExecutionProbeMoveName;
	extern const TCHAR* const ForcedEntryProbeMoveName;
	extern const TCHAR* const CaptureHeldItemName;

struct FAtomicWildScenario
	{
		EBattleFormat Format = EBattleFormat::Single;
		int32 PlayerLeftSpeed = 50;
		int32 PlayerRightSpeed = 73;
		int32 OpponentLeftSpeed = 100;
		int32 OpponentRightSpeed = 4;
		EBattleWildFleeMode WildFleeMode = EBattleWildFleeMode::Disabled;
		uint32 WildFleeNumerator = 0;
		uint32 WildFleeDenominator = 0;
		bool bCaptureFlow = false;
		bool bTrainerEncounter = false;
		int32 CatchRate = 45;
		int32 TargetCurrentHP = 200;
		int32 PlayerCurrentHP = 200;
		int32 PokeBallCount = 3;
		int32 PartyCaptureCapacity = 1;
		int32 StorageCaptureCapacity = 2;
		FBattleCaptureProgressionSnapshot CaptureProgression;
		bool bPlayerHasCanonicalHeldItem = false;
		bool bPlayerSubjectToObedience = false;
		uint8 PlayerReferenceLevel = 20;
		uint8 PlayerBadgeCount = 0;
		bool bVoluntarySwitchFlow = false;
		bool bPivotSwitchFlow = false;
		FAbilityId PlayerAbilityId;
		FItemId PlayerHeldItemId;
		FMoveId PlayerExtraMoveId;
		bool bSecondSwitchReserve = false;
		int32 SwitchIncomingCurrentHP = 200;
		FAbilityId SwitchIncomingAbilityId;
		FItemId SwitchIncomingHeldItemId;
		bool bOpponentSwitchReserve = false;
		int32 OpponentReserveCurrentHP = 200;
		FAbilityId OpponentReserveAbilityId;
		FItemId OpponentReserveHeldItemId;
	};

struct FCheckpointObservation
	{
		uint64 StateVersion = 0;
		uint64 NextResolutionId = 0;
		uint64 NextEventOrdinal = 0;
		uint64 NextTriggerToken = 0;
		uint32 EscapeAttemptCount = 0;
		int32 LockedActionIndex = INDEX_NONE;
		int32 ResolutionCount = 0;
		int32 EventCount = 0;
		int32 RandomTraceCount = 0;
		int32 PokeBallCount = INDEX_NONE;
		int32 PendingCaptureCount = 0;
		EBattlePhase Phase = EBattlePhase::Setup;
		EBattleOutcome Outcome = EBattleOutcome::InProgress;
		EBattleOutcomeCause OutcomeCause = EBattleOutcomeCause::None;
		bool bActionStarted = false;
		bool bActionFinished = false;
		bool bBagActionAvailable = false;
		bool bObservedBattlerActive = false;
		bool bObservedBattlerCaptured = false;
		bool bObservedBattlerRemoved = false;
	};

TArray<FBattleTypeChartEntry> MakeNeutralTypeChart();

FBattleMoveDefinition MakeProbeMove();

FBattleMoveDefinition MakeTargetProbeMove();

FBattleMoveDefinition MakeRandomTargetProbeMove();

FBattleMoveDefinition MakeRandomExecutionProbeMove();

FBattleMoveDefinition MakeForcedEntryProbeMove();

FBattleMoveDefinition MakePivotProbeMove();

FBattleMoveDefinition MakeThawProbeMove();

FBattleMoveDefinition MakeChargeProbeMove();

FBattleSpeciesFormDefinition MakeSpecies(
		const TCHAR* Name,
		const int32 CatchRate = 45);

FBattleDefinitionCatalog MakeCatalog(const FAtomicWildScenario& Scenario);

FBattleTrainerSetup MakeTrainer(
		const uint64 TrainerValue,
		const EBattleSide Side,
		const EBattleTrainerRole Role,
		const EBattleDecisionController Controller,
		const int32 PokeBallCount = -1);

FBattlePartyEntrySetup MakePartyEntry(
		const uint64 TrainerValue,
		const uint64 BattlerValue,
		const int32 PartyIndex,
		const TCHAR* SpeciesName,
		const int32 Speed,
		const int32 CurrentHP = 200,
		const FItemId OriginalHeldItemId = FItemId(),
		const FItemId CurrentHeldItemId = FItemId(),
		const FAbilityId AbilityId = FBattleAbilityRules::GetBlazeId(),
		const bool bAddPivotMove = false,
		const FMoveId ExtraMoveId = FMoveId());

FBattleActiveAssignment MakeActive(
		const EBattleSide Side,
		const EBattlePosition Position,
		const uint64 TrainerValue,
		const uint64 BattlerValue);

FBattleSetupInput MakeSetupInput(const FAtomicWildScenario& Scenario);

bool TryMakeEngine(
		const FAtomicWildScenario& Scenario,
		TUniquePtr<IBattleRandom>&& Random,
		TUniquePtr<FBattleEngine>& OutEngine);

bool TryMakeSequenceEngine(
		const FAtomicWildScenario& Scenario,
		TArray<uint32> Results,
		TUniquePtr<FBattleEngine>& OutEngine);

bool TryMakeStrictEngine(
		const FAtomicWildScenario& Scenario,
		TArray<FBattleExpectedRandomDraw> ExpectedDraws,
		TUniquePtr<FBattleEngine>& OutEngine,
		FStrictBattleRandom*& OutRandom);

FAtomicWildScenario MakeAtomicCaptureScenario();

FBattleDecision MakeDecision(
		const FBattleDecisionRequest& Request,
		const EBattleActionKind ActionKind,
		const FMoveId FightMoveId = FMoveId());

FBattleDecisionBatch MakeBatch(
		const TArray<FBattleDecisionRequest>& Requests,
		TArray<FBattleDecision> Decisions);

bool LockTurn(
		FBattleEngine& Engine,
		const uint64 SpecialBattlerValue,
		const EBattleActionKind SpecialAction,
		const FMoveId SpecialFightMoveId = FMoveId());

bool BeginExpectedWildAction(
		FBattleEngine& Engine,
		const uint64 BattlerValue,
		const EBattleActionKind ActionKind);

bool HasEvent(const FBattleResolution& Resolution, const EBattleEventType Type);

bool HasExactEventOrder(
		const FBattleResolution& Resolution,
		const TArray<EBattleEventType>& Expected);

bool AreEventSourcesIdentical(
		const FBattleEventSource& Left,
		const FBattleEventSource& Right);

bool AreEventTargetsIdentical(
		const TConstArrayView<FBattleEventTarget> Left,
		const TConstArrayView<FBattleEventTarget> Right);

bool AreEventsIdentical(const FBattleEvent& Left, const FBattleEvent& Right);

bool AreActionStartHeldItemsIdentical(
		const FBattleHeldItemState& Left,
		const FBattleHeldItemState& Right);

bool IsReturnedResolutionAppendedExactlyOnce(
		const FBattleEngine& Engine,
		const FBattleResolution& Returned);

FCheckpointObservation ObserveCheckpoint(
		const FBattleEngine& Engine,
		const FBattlerId ObservedBattlerId);

bool VerifyRejectedCheckpoint(
		FAutomationTestBase& Test,
		const FBattleEngine& Engine,
		const FBattlerId ObservedBattlerId,
		const FCheckpointObservation& Before,
		const uint64 ExpectedStateVersion,
		const EBattleRejectionReason ExpectedReason,
		const FBattleResolution& Returned);
}

#endif
