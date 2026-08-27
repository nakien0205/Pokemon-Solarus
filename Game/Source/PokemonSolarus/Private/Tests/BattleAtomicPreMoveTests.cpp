#if WITH_DEV_AUTOMATION_TESTS

#include "BattleAtomicCheckpointTestCommon.h"
#include "BattleAtomicCheckpointTestFaults.h"
#include "BattleAtomicSwitchTestSupport.h"
#include "BattleAtomicMoveCheckpointTestSupport.h"

namespace BattleAtomicPreMoveTestsPrivate
{
	using namespace BattleAtomicCheckpointTestCommonPrivate;
	using namespace BattleAtomicCheckpointTestFaultsPrivate;
	using namespace BattleAtomicSwitchTestSupportPrivate;
	using namespace BattleAtomicMoveCheckpointTestSupportPrivate;

bool IsPreMoveSuccessEvent(const EBattleEventType Type)
	{
		switch (Type)
		{
		case EBattleEventType::PPConsumed:
		case EBattleEventType::MoveUsed:
		case EBattleEventType::RandomCheck:
		case EBattleEventType::StatusChanged:
		case EBattleEventType::Damage:
		case EBattleEventType::HPChanged:
		case EBattleEventType::Fainted:
		case EBattleEventType::LeftActiveSlot:
		case EBattleEventType::Removed:
		case EBattleEventType::AbilityActivated:
		case EBattleEventType::ItemActivated:
		case EBattleEventType::ItemConsumed:
		case EBattleEventType::ActionCompleted:
		case EBattleEventType::BattleEnded:
		case EBattleEventType::ReplacementRequired:
			return true;
		default:
			return false;
		}
	}

bool VerifyRejectedPreMoveCheckpoint(
		FAutomationTestBase& Test,
		const FBattleEngine& Engine,
		const FActionStartCheckpointObservation& BeforeAction,
		const FAtomicSwitchCheckpointObservation& BeforeMechanics,
		const EBattleRejectionReason ExpectedReason,
		const FBattleResolution& Returned)
	{
		const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
		const FTrainerId TrainerId = MakeNumericId<FTrainerId>(PlayerTrainerValue);
		bool bValid = VerifyRejectedActionStartCheckpoint(
			Test,
			Engine,
			ActorId,
			TrainerId,
			BeforeAction,
			BeforeAction.StateVersion,
			ExpectedReason,
			Returned);
		const FAtomicSwitchCheckpointObservation AfterMechanics =
			ObserveAtomicSwitchCheckpoint(Engine);
		bValid &= Test.TestTrue(
			TEXT("Rejected pre-move checkpoint preserves actor, target, conditions, triggers, item and cursor facts"),
			AreAtomicSwitchMechanicsIdentical(
				AfterMechanics,
				BeforeMechanics));
		bValid &= Test.TestFalse(
			TEXT("Rejected pre-move checkpoint publishes no checkpoint success fact"),
			Returned.GetEvents().ContainsByPredicate(
				[](const FBattleEvent& Event)
				{
					return IsPreMoveSuccessEvent(Event.GetType());
				}));
		const FBattleReplayRecord Replay = Engine.ExportReplayRecord();
		bValid &= Test.TestEqual(
			TEXT("Rejected pre-move replay schema remains 6"),
			Replay.GetSchemaVersion(),
			static_cast<uint32>(6));
		bValid &= Test.TestTrue(
			TEXT("Rejected pre-move replay contains the same rejection exactly once"),
			!Replay.GetResolutions().IsEmpty()
				&& Replay.GetResolutions().Last().GetResolutionId()
					== Returned.GetResolutionId()
				&& Replay.GetResolutions().Last().GetRejection().Reason
					== ExpectedReason
				&& Replay.GetResolutions().Last().GetEvents().Num() == 1
				&& !IsPreMoveSuccessEvent(
					Replay.GetResolutions().Last().GetEvents()[0].GetType()));
		return bValid;
	}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E4OrdinaryMoveTest,
	"PokemonSolarus.Battle.ADR0002.3E4.PreMoveGates.Success.OrdinaryMoveCommitsOnePP",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E4OrdinaryMoveTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	const FMoveId MoveId = MakeDefinitionId<FMoveId>(ProbeMoveName);
	TUniquePtr<FBattleEngine> Engine;
	if (!TestTrue(TEXT("Ordinary pre-move engine is created"),
			TryMakeSequenceEngine(MakePreMoveScenario(), {}, Engine))
		|| !TestTrue(TEXT("Ordinary Fight is locked and started"),
			TryLockAndBeginPreMove(*Engine)))
	{
		return false;
	}
	const FBattleEngineState& BeforeState =
		FBattleC09BWildFlowEngineFixture::GetState(*Engine);
	const uint64 VersionBefore = BeforeState.StateVersion;
	const int32 CursorBefore = BeforeState.CurrentLockedActionIndex;
	const int32 PPBefore = GetPreMovePP(*Engine, ActorId, MoveId);
	const FBattleResolution Resolution =
		Engine->CommitCurrentMoveAfterPreMoveGates();
	const FBattleEngineState& State =
		FBattleC09BWildFlowEngineFixture::GetState(*Engine);
	const FBattleBattlerState* Actor = State.FindBattler(ActorId);
	TestTrue(TEXT("Ordinary gate commits successfully"), Resolution.WasAccepted());
	TestTrue(TEXT("Ordinary gate publishes PPConsumed before MoveUsed"),
		HasExactEventOrder(
			Resolution,
			{EBattleEventType::PPConsumed, EBattleEventType::MoveUsed}));
	TestEqual(TEXT("Ordinary gate consumes exactly one PP"),
		GetPreMovePP(*Engine, ActorId, MoveId), PPBefore - 1);
	TestTrue(TEXT("Allowed move remains ready for target resolution"),
		State.CurrentLockedActionIndex == CursorBefore
			&& State.LockedActions.IsValidIndex(CursorBefore)
			&& State.LockedActions[CursorBefore].bStarted
			&& State.LockedActions[CursorBefore].bMoveCommitted
			&& !State.LockedActions[CursorBefore].TargetResolution.IsSet()
			&& !State.LockedActions[CursorBefore].bFinished);
	TestTrue(TEXT("Allowed move stores LastMoveId"),
		Actor != nullptr && Actor->LastMoveId == MoveId);
	TestEqual(TEXT("Accepted gate advances state version once"),
		State.StateVersion, VersionBefore + 1);
	TestTrue(TEXT("Accepted gate is returned and appended exactly once"),
		IsReturnedResolutionAppendedExactlyOnce(*Engine, Resolution));
	TestEqual(TEXT("Accepted gate replay schema remains 6"),
		Engine->ExportReplayRecord().GetSchemaVersion(), static_cast<uint32>(6));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E4SleepTest,
	"PokemonSolarus.Battle.ADR0002.3E4.PreMoveGates.Status.SleepDenialWakeAndExpiration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E4SleepTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	const FMoveId MoveId = MakeDefinitionId<FMoveId>(ProbeMoveName);
	TUniquePtr<FBattleEngine> Denied;
	if (!TestTrue(TEXT("Sleep-denial engine is created"),
			TryMakeSequenceEngine(MakePreMoveScenario(), {}, Denied))
		|| !TestTrue(TEXT("Two-turn Sleep is seeded"),
			TrySeedPreMoveMajorStatus(
				*Denied,
				ActorId,
				FBattleMajorStatusRules::GetSleepId(),
				2))
		|| !TestTrue(TEXT("Sleeping Fight is locked and started"),
			TryLockAndBeginPreMove(*Denied)))
	{
		return false;
	}
	const int32 DeniedPP = GetPreMovePP(*Denied, ActorId, MoveId);
	const FBattleResolution Denial = Denied->CommitCurrentMoveAfterPreMoveGates();
	const FBattleEngineState& DeniedState =
		FBattleC09BWildFlowEngineFixture::GetState(*Denied);
	const TArray<FBattleTriggerRegistrationState> SleepRegistrations =
		DeniedState.TriggerFramework.GetActiveRegistrations().FilterByPredicate(
			[](const FBattleTriggerRegistrationState& Registration)
			{
				return Registration.Spec.SourceDefinition.ConditionId
					== FBattleMajorStatusRules::GetSleepId();
			});
	TestTrue(TEXT("Sleep denial is an accepted rules outcome"), Denial.WasAccepted());
	TestEqual(TEXT("Sleep denial consumes no PP"),
		GetPreMovePP(*Denied, ActorId, MoveId), DeniedPP);
	TestEqual(TEXT("Sleep denial consumes no RNG"), Denied->ExportRandomTrace().Num(), 0);
	TestTrue(TEXT("Sleep duration decrements only in committed staged state"),
		SleepRegistrations.Num() == 1
			&& SleepRegistrations[0].RemainingTurns.IsSet()
			&& SleepRegistrations[0].RemainingTurns.GetValue() == 1);
	TestTrue(TEXT("Sleep denial completes and advances the action"),
		HasEvent(Denial, EBattleEventType::ActionCompleted)
			&& DeniedState.CurrentLockedActionIndex == 1);

	if (!TestTrue(TEXT("Opponent action begins after Sleep denial"),
			Denied->BeginNextLockedAction().WasAccepted())
		|| !TestTrue(TEXT("Opponent move passes its gate"),
			Denied->CommitCurrentMoveAfterPreMoveGates().WasAccepted())
		|| !TestTrue(TEXT("Opponent targets resolve"),
			Denied->ResolveCurrentMoveTargets().WasAccepted())
		|| !TestTrue(TEXT("Opponent effects complete turn one"),
			Denied->ExecuteCurrentMoveEffects().WasAccepted())
		|| !TestTrue(TEXT("Turn-one end phase resolves"),
			Denied->ResolveEndTurn().WasAccepted())
		|| !TestTrue(TEXT("Turn-two actions lock"),
			LockTurn(
				*Denied,
				PlayerLeftValue,
				EBattleActionKind::Fight))
		|| !TestTrue(TEXT("Expiring Sleep Fight starts"),
			BeginExpectedWildAction(
				*Denied,
				PlayerLeftValue,
				EBattleActionKind::Fight)))
	{
		return false;
	}
	const int32 WakePP = GetPreMovePP(*Denied, ActorId, MoveId);
	const int32 TraceBeforeWake = Denied->ExportRandomTrace().Num();
	const FBattleResolution Wake = Denied->CommitCurrentMoveAfterPreMoveGates();
	const FBattleBattlerState* Woken =
		FBattleC09BWildFlowEngineFixture::GetState(*Denied).FindBattler(ActorId);
	TestTrue(TEXT("Expired Sleep permits the move"), Wake.WasAccepted());
	TestTrue(TEXT("Expired Sleep cleanup precedes PP and MoveUsed"),
		HasExactEventOrder(
			Wake,
			{EBattleEventType::StatusChanged,
			 EBattleEventType::PPConsumed,
			 EBattleEventType::MoveUsed}));
	TestTrue(TEXT("Expired Sleep clears status and its trigger"),
		Woken != nullptr
			&& !Woken->MajorStatusId.IsValid()
			&& CountActionStartTriggerRegistrations(
				FBattleC09BWildFlowEngineFixture::GetState(*Denied),
				FBattleMajorStatusRules::GetSleepId().GetDefinitionId()) == 0);
	TestEqual(TEXT("Wake consumes one PP"),
		GetPreMovePP(*Denied, ActorId, MoveId), WakePP - 1);
	TestEqual(TEXT("Sleep expiration consumes no RNG"),
		Denied->ExportRandomTrace().Num(), TraceBeforeWake);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E4FreezeTest,
	"PokemonSolarus.Battle.ADR0002.3E4.PreMoveGates.Status.FreezeDeniedNaturalAndForcedThaw",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E4FreezeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	const FMoveId ProbeId = MakeDefinitionId<FMoveId>(ProbeMoveName);
	auto MakeFrozen = [&](TArray<uint32> Draws,
		const FAtomicWildScenario& Scenario,
		const FMoveId MoveId,
		TUniquePtr<FBattleEngine>& Out) -> bool
	{
		return TryMakeSequenceEngine(Scenario, MoveTemp(Draws), Out)
			&& TrySeedPreMoveMajorStatus(
				*Out,
				ActorId,
				FBattleMajorStatusRules::GetFreezeId())
			&& TryLockAndBeginPreMove(*Out, MoveId);
	};

	TUniquePtr<FBattleEngine> Denied;
	if (!TestTrue(TEXT("Failed-thaw Fight is prepared"),
			MakeFrozen({4}, MakePreMoveScenario(), FMoveId(), Denied)))
	{
		return false;
	}
	const int32 DeniedPP = GetPreMovePP(*Denied, ActorId, ProbeId);
	const FBattleResolution Denial = Denied->CommitCurrentMoveAfterPreMoveGates();
	TestTrue(TEXT("Failed natural thaw is accepted denial"), Denial.WasAccepted());
	TestEqual(TEXT("Failed thaw consumes no PP"),
		GetPreMovePP(*Denied, ActorId, ProbeId), DeniedPP);
	TestEqual(TEXT("Failed thaw commits exactly one transactional draw"),
		Denied->ExportRandomTrace().Num(), 1);

	TUniquePtr<FBattleEngine> Natural;
	if (!TestTrue(TEXT("Natural-thaw Fight is prepared"),
			MakeFrozen({0}, MakePreMoveScenario(), FMoveId(), Natural)))
	{
		return false;
	}
	const int32 NaturalPP = GetPreMovePP(*Natural, ActorId, ProbeId);
	const FBattleResolution Thaw = Natural->CommitCurrentMoveAfterPreMoveGates();
	const FBattleBattlerState* NaturallyThawed =
		FBattleC09BWildFlowEngineFixture::GetState(*Natural).FindBattler(ActorId);
	TestTrue(TEXT("Natural thaw permits the move"), Thaw.WasAccepted());
	TestTrue(TEXT("Natural thaw draw and cleanup precede PP"),
		HasExactEventOrder(
			Thaw,
			{EBattleEventType::RandomCheck,
			 EBattleEventType::StatusChanged,
			 EBattleEventType::PPConsumed,
			 EBattleEventType::MoveUsed}));
	TestTrue(TEXT("Natural thaw clears Freeze"),
		NaturallyThawed != nullptr && !NaturallyThawed->MajorStatusId.IsValid());
	TestEqual(TEXT("Natural thaw consumes one PP"),
		GetPreMovePP(*Natural, ActorId, ProbeId), NaturalPP - 1);
	TestEqual(TEXT("Natural thaw commits one draw"),
		Natural->ExportRandomTrace().Num(), 1);

	const FMoveId ThawMoveId = MakeDefinitionId<FMoveId>(ThawProbeMoveName);
	TUniquePtr<FBattleEngine> Forced;
	if (!TestTrue(TEXT("Forced-user-thaw Fight is prepared"),
			MakeFrozen(
				{},
				MakePreMoveScenario(ThawMoveId),
				ThawMoveId,
				Forced)))
	{
		return false;
	}
	const int32 ForcedPP = GetPreMovePP(*Forced, ActorId, ThawMoveId);
	const FBattleResolution ForcedThaw =
		Forced->CommitCurrentMoveAfterPreMoveGates();
	const FBattleBattlerState* ForceThawed =
		FBattleC09BWildFlowEngineFixture::GetState(*Forced).FindBattler(ActorId);
	TestTrue(TEXT("Forced user thaw permits the move"), ForcedThaw.WasAccepted());
	TestTrue(TEXT("Forced user thaw clears Freeze"),
		ForceThawed != nullptr && !ForceThawed->MajorStatusId.IsValid());
	TestEqual(TEXT("Forced user thaw consumes one PP"),
		GetPreMovePP(*Forced, ActorId, ThawMoveId), ForcedPP - 1);
	TestEqual(TEXT("Forced user thaw is a no-draw path"),
		Forced->ExportRandomTrace().Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E4ParalysisTest,
	"PokemonSolarus.Battle.ADR0002.3E4.PreMoveGates.Status.ParalysisDeniedAndAllowed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E4ParalysisTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	const FMoveId MoveId = MakeDefinitionId<FMoveId>(ProbeMoveName);
	auto Prepare = [&](const uint32 Draw, TUniquePtr<FBattleEngine>& Out) -> bool
	{
		return TryMakeSequenceEngine(MakePreMoveScenario(), {Draw}, Out)
			&& TrySeedPreMoveMajorStatus(
				*Out,
				ActorId,
				FBattleMajorStatusRules::GetParalysisId())
			&& TryLockAndBeginPreMove(*Out);
	};
	TUniquePtr<FBattleEngine> Denied;
	if (!TestTrue(TEXT("Full-Paralysis Fight is prepared"), Prepare(0, Denied)))
	{
		return false;
	}
	const int32 DeniedPP = GetPreMovePP(*Denied, ActorId, MoveId);
	const FBattleResolution Denial = Denied->CommitCurrentMoveAfterPreMoveGates();
	TestTrue(TEXT("Full Paralysis is accepted denial"), Denial.WasAccepted());
	TestEqual(TEXT("Full Paralysis consumes no PP"),
		GetPreMovePP(*Denied, ActorId, MoveId), DeniedPP);
	TestEqual(TEXT("Full Paralysis commits one transactional draw"),
		Denied->ExportRandomTrace().Num(), 1);
	TestTrue(TEXT("Full Paralysis completes the action"),
		HasEvent(Denial, EBattleEventType::ActionCompleted));

	TUniquePtr<FBattleEngine> Allowed;
	if (!TestTrue(TEXT("Allowed-Paralysis Fight is prepared"), Prepare(3, Allowed)))
	{
		return false;
	}
	const int32 AllowedPP = GetPreMovePP(*Allowed, ActorId, MoveId);
	const FBattleResolution Proceed = Allowed->CommitCurrentMoveAfterPreMoveGates();
	TestTrue(TEXT("Nonzero Paralysis roll permits the move"), Proceed.WasAccepted());
	TestTrue(TEXT("Allowed Paralysis draw precedes PP and MoveUsed"),
		HasExactEventOrder(
			Proceed,
			{EBattleEventType::RandomCheck,
			 EBattleEventType::PPConsumed,
			 EBattleEventType::MoveUsed}));
	TestEqual(TEXT("Allowed Paralysis consumes one PP"),
		GetPreMovePP(*Allowed, ActorId, MoveId), AllowedPP - 1);
	TestEqual(TEXT("Allowed Paralysis commits one transactional draw"),
		Allowed->ExportRandomTrace().Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E4ConfusionAllowedTest,
	"PokemonSolarus.Battle.ADR0002.3E4.PreMoveGates.Volatile.ConfusionAllowedAndDuration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E4ConfusionAllowedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	const FMoveId MoveId = MakeDefinitionId<FMoveId>(ProbeMoveName);
	TUniquePtr<FBattleEngine> Engine;
	if (!TestTrue(TEXT("Allowed-Confusion engine is created"),
			TryMakeSequenceEngine(MakePreMoveScenario(), {33}, Engine))
		|| !TestTrue(TEXT("Three-turn Confusion is seeded"),
			TrySeedActionStartVolatile(
				*Engine,
				ActorId,
				FBattleVolatileRules::GetConfusionId(),
				FDefinitionId(),
				3))
		|| !TestTrue(TEXT("Confused Fight is locked and started"),
			TryLockAndBeginPreMove(*Engine)))
	{
		return false;
	}
	const int32 PPBefore = GetPreMovePP(*Engine, ActorId, MoveId);
	const FBattleResolution Resolution =
		Engine->CommitCurrentMoveAfterPreMoveGates();
	const FBattleConditionState* Confusion = FindPreMoveVolatile(
		*Engine,
		ActorId,
		FBattleVolatileRules::GetConfusionId());
	TestTrue(TEXT("Allowed Confusion commits"), Resolution.WasAccepted());
	TestTrue(TEXT("Confusion gate draw precedes PP and MoveUsed"),
		HasExactEventOrder(
			Resolution,
			{EBattleEventType::RandomCheck,
			 EBattleEventType::PPConsumed,
			 EBattleEventType::MoveUsed}));
	TestTrue(TEXT("Confusion duration decrements atomically"),
		Confusion != nullptr
			&& Confusion->RemainingTurns.IsSet()
			&& Confusion->RemainingTurns.GetValue() == 2);
	TestEqual(TEXT("Allowed Confusion consumes one PP"),
		GetPreMovePP(*Engine, ActorId, MoveId), PPBefore - 1);
	TestEqual(TEXT("Allowed Confusion commits one draw"),
		Engine->ExportRandomTrace().Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E4ConfusionNonlethalTest,
	"PokemonSolarus.Battle.ADR0002.3E4.PreMoveGates.Volatile.ConfusionNonlethalSelfHit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E4ConfusionNonlethalTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	const FBattlerId TargetId = MakeNumericId<FBattlerId>(OpponentLeftValue);
	const FMoveId MoveId = MakeDefinitionId<FMoveId>(ProbeMoveName);
	TUniquePtr<FBattleEngine> Engine;
	if (!TestTrue(TEXT("Nonlethal self-hit engine is created"),
			TryMakeSequenceEngine(MakePreMoveScenario(), {0, 0}, Engine))
		|| !TestTrue(TEXT("Confusion is seeded for nonlethal self-hit"),
			TrySeedActionStartVolatile(
				*Engine,
				ActorId,
				FBattleVolatileRules::GetConfusionId(),
				FDefinitionId(),
				3))
		|| !TestTrue(TEXT("Nonlethal self-hit Fight is started"),
			TryLockAndBeginPreMove(*Engine)))
	{
		return false;
	}
	const FBattleEngineState& Before =
		FBattleC09BWildFlowEngineFixture::GetState(*Engine);
	const int32 HPBefore = Before.FindBattler(ActorId)->CurrentHP;
	const int32 TargetHPBefore = Before.FindBattler(TargetId)->CurrentHP;
	const int32 PPBefore = GetPreMovePP(*Engine, ActorId, MoveId);
	const int32 CursorBefore = Before.CurrentLockedActionIndex;
	const FBattleResolution Resolution =
		Engine->CommitCurrentMoveAfterPreMoveGates();
	const FBattleEngineState& State =
		FBattleC09BWildFlowEngineFixture::GetState(*Engine);
	const FBattleBattlerState* Actor = State.FindBattler(ActorId);
	const FBattleBattlerState* Target = State.FindBattler(TargetId);
	TestTrue(TEXT("Nonlethal self-hit is accepted denial"), Resolution.WasAccepted());
	TestTrue(TEXT("Self-hit commits gate draw, damage draw, damage and HP facts"),
		Resolution.GetEvents().Num() >= 7
			&& Resolution.GetEvents()[0].GetType() == EBattleEventType::RandomCheck
			&& Resolution.GetEvents()[1].GetType() == EBattleEventType::RandomCheck
			&& Resolution.GetEvents()[2].GetType() == EBattleEventType::Damage
			&& Resolution.GetEvents()[3].GetType() == EBattleEventType::HPChanged
			&& HasEvent(Resolution, EBattleEventType::ActionCompleted));
	TestTrue(TEXT("Nonlethal self-hit HP and completion commit together"),
		Actor != nullptr
			&& Actor->CurrentHP > 0
			&& Actor->CurrentHP < HPBefore
			&& State.CurrentLockedActionIndex == CursorBefore + 1);
	TestEqual(TEXT("Self-hit consumes no move PP"),
		GetPreMovePP(*Engine, ActorId, MoveId), PPBefore);
	TestTrue(TEXT("Self-hit leaves the target untouched"),
		Target != nullptr && Target->CurrentHP == TargetHPBefore);
	TestEqual(TEXT("Self-hit commits gate and damage draws"),
		Engine->ExportRandomTrace().Num(), 2);
	TestTrue(TEXT("Self-hit resolution is appended exactly once"),
		IsReturnedResolutionAppendedExactlyOnce(*Engine, Resolution));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E4MagicGuardConfusionTest,
	"PokemonSolarus.Battle.ADR0002.3E4.PreMoveGates.Volatile.MagicGuardPreventsConfusionDamage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E4MagicGuardConfusionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	const FMoveId MoveId = MakeDefinitionId<FMoveId>(ProbeMoveName);
	TUniquePtr<FBattleEngine> Engine;
	if (!TestTrue(TEXT("Magic Guard self-hit engine is created"),
			TryMakeSequenceEngine(
				MakePreMoveScenario(
					FMoveId(),
					200,
					FBattleAbilityRules::GetMagicGuardId()),
				{0},
				Engine))
		|| !TestTrue(TEXT("Confusion is seeded on Magic Guard actor"),
			TrySeedActionStartVolatile(
				*Engine,
				ActorId,
				FBattleVolatileRules::GetConfusionId(),
				FDefinitionId(),
				3))
		|| !TestTrue(TEXT("Magic Guard self-hit Fight is started"),
			TryLockAndBeginPreMove(*Engine)))
	{
		return false;
	}
	const FBattleEngineState& Before =
		FBattleC09BWildFlowEngineFixture::GetState(*Engine);
	const int32 HPBefore = Before.FindBattler(ActorId)->CurrentHP;
	const int32 PPBefore = GetPreMovePP(*Engine, ActorId, MoveId);
	const FBattleResolution Resolution =
		Engine->CommitCurrentMoveAfterPreMoveGates();
	const FBattleBattlerState* Actor =
		FBattleC09BWildFlowEngineFixture::GetState(*Engine).FindBattler(ActorId);
	TestTrue(TEXT("Magic Guard prevention is accepted denial"), Resolution.WasAccepted());
	TestTrue(TEXT("Magic Guard activation is published"),
		HasEvent(Resolution, EBattleEventType::AbilityActivated));
	TestFalse(TEXT("Magic Guard publishes no confusion damage"),
		HasEvent(Resolution, EBattleEventType::Damage)
			|| HasEvent(Resolution, EBattleEventType::HPChanged));
	TestTrue(TEXT("Magic Guard preserves HP"),
		Actor != nullptr && Actor->CurrentHP == HPBefore);
	TestEqual(TEXT("Magic Guard self-hit denial consumes no PP"),
		GetPreMovePP(*Engine, ActorId, MoveId), PPBefore);
	TestEqual(TEXT("Magic Guard commits only the gate draw and skips damage draw"),
		Engine->ExportRandomTrace().Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E4ConfusionLethalTest,
	"PokemonSolarus.Battle.ADR0002.3E4.PreMoveGates.Volatile.ConfusionLethalOutcomes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E4ConfusionLethalTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	const FMoveId MoveId = MakeDefinitionId<FMoveId>(ProbeMoveName);
	auto PrepareLethal = [&](const bool bReserve, TUniquePtr<FBattleEngine>& Out) -> bool
	{
		FAtomicWildScenario Scenario = MakePreMoveScenario(FMoveId(), 1);
		Scenario.PlayerLeftSpeed = 10;
		Scenario.bVoluntarySwitchFlow = bReserve;
		return TryMakeSequenceEngine(Scenario, {0, 0}, Out)
			&& TrySeedActionStartVolatile(
				*Out,
				ActorId,
				FBattleVolatileRules::GetConfusionId(),
				FDefinitionId(),
				3)
			&& LockTurn(
				*Out,
				PlayerLeftValue,
				EBattleActionKind::Fight)
			&& TryPrepareLastLockedAction(*Out, ActorId)
			&& BeginExpectedWildAction(
				*Out,
				PlayerLeftValue,
				EBattleActionKind::Fight);
	};

	TUniquePtr<FBattleEngine> Terminal;
	if (!TestTrue(TEXT("Terminal lethal self-hit is prepared"),
			PrepareLethal(false, Terminal)))
	{
		return false;
	}
	const int32 TerminalPP = GetPreMovePP(*Terminal, ActorId, MoveId);
	const FBattleResolution TerminalResult =
		Terminal->CommitCurrentMoveAfterPreMoveGates();
	const FBattleEngineState& TerminalState =
		FBattleC09BWildFlowEngineFixture::GetState(*Terminal);
	const FBattleBattlerState* Fainted = TerminalState.FindBattler(ActorId);
	TestTrue(TEXT("Lethal self-hit is accepted"), TerminalResult.WasAccepted());
	TestTrue(TEXT("Lethal self-hit publishes damage, faint cleanup and battle end"),
		HasEvent(TerminalResult, EBattleEventType::Damage)
			&& HasEvent(TerminalResult, EBattleEventType::Fainted)
			&& HasEvent(TerminalResult, EBattleEventType::LeftActiveSlot)
			&& HasEvent(TerminalResult, EBattleEventType::Removed)
			&& HasEvent(TerminalResult, EBattleEventType::ActionCompleted)
			&& HasEvent(TerminalResult, EBattleEventType::BattleEnded));
	TestTrue(TEXT("Terminal lethal self-hit commits faint and outcome together"),
		Fainted != nullptr
			&& Fainted->CurrentHP == 0
			&& Fainted->bFainted
			&& Fainted->bRemoved
			&& !Fainted->bFaintTransitionPending
			&& TerminalState.Phase == EBattlePhase::Terminal
			&& TerminalState.Outcome == EBattleOutcome::Defeat);
	TestEqual(TEXT("Terminal self-hit consumes no PP"),
		GetPreMovePP(*Terminal, ActorId, MoveId), TerminalPP);

	TUniquePtr<FBattleEngine> Replacement;
	if (!TestTrue(TEXT("Replacement lethal self-hit is prepared"),
			PrepareLethal(true, Replacement)))
	{
		return false;
	}
	const int32 ReplacementPP = GetPreMovePP(*Replacement, ActorId, MoveId);
	const FBattleResolution ReplacementResult =
		Replacement->CommitCurrentMoveAfterPreMoveGates();
	const FBattleEngineState& ReplacementState =
		FBattleC09BWildFlowEngineFixture::GetState(*Replacement);
	TestTrue(TEXT("Reserve-backed lethal self-hit is accepted"),
		ReplacementResult.WasAccepted());
	TestTrue(TEXT("Queue boundary requests mandatory replacement"),
		ReplacementState.Phase == EBattlePhase::MandatoryReplacement
			&& HasEvent(ReplacementResult, EBattleEventType::ReplacementRequired)
			&& !HasEvent(ReplacementResult, EBattleEventType::BattleEnded));
	TestEqual(TEXT("Replacement lethal self-hit consumes no PP"),
		GetPreMovePP(*Replacement, ActorId, MoveId), ReplacementPP);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E4PartnerRecoveryPlanTest,
	"PokemonSolarus.Battle.ADR0002.3E4.PreMoveGates.Projection.PartnerTeamVictoryRecoveryPlan",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E4PartnerRecoveryPlanTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FAtomicWildScenario Scenario = MakePreMoveScenario();
	Scenario.Format = EBattleFormat::PartnerDouble;
	TUniquePtr<FBattleEngine> Engine;
	if (!TestTrue(TEXT("Partner projection engine is created"),
			TryMakeSequenceEngine(Scenario, {}, Engine)))
	{
		return false;
	}
	FBattleEngineState& State =
		FBattleC09BWildFlowEngineFixture::GetMutableState(*Engine);
	FBattleBattlerState* Player = State.FindMutableBattler(
		MakeNumericId<FBattlerId>(PlayerLeftValue));
	if (!TestNotNull(TEXT("Partner projection finds player battler"), Player))
	{
		return false;
	}
	Player->CurrentHP = 0;
	Player->bFainted = true;
	Player->bRemoved = true;
	Player->MajorStatusId = FBattleMajorStatusRules::GetParalysisId();
	const int32 HPBeforePlan = Player->CurrentHP;
	const bool bFaintedBeforePlan = Player->bFainted;
	const FConditionId StatusBeforePlan = Player->MajorStatusId;
	FBattlePartnerTeamVictoryRecoveryPlan Plan;
	TestTrue(TEXT("Partner recovery plan is produced"),
		FBattlePartnerFlow::TryApplyTeamVictoryRecovery(
			static_cast<const FBattleEngineState&>(State),
			Plan));
	TestTrue(TEXT("Partner recovery preparation does not mutate supplied state"),
		Player->CurrentHP == HPBeforePlan
			&& Player->bFainted == bFaintedBeforePlan
			&& Player->MajorStatusId == StatusBeforePlan);
	TestTrue(TEXT("Partner recovery plan owns exact target and recovery facts"),
		Plan.Recovery.Target.BattlerId == Player->BattlerId
			&& Plan.Recovery.PreviousHP == 0
			&& Plan.Recovery.NewHP == 1
			&& Plan.Recovery.bMajorStatusCured);
	TestTrue(TEXT("Partner recovery plan applies to caller-owned staged state"),
		FBattlePartnerFlow::TryApplyTeamVictoryRecoveryPlan(State, Plan));
	TestTrue(TEXT("Applied partner plan restores exactly one HP and cures status"),
		Player->CurrentHP == 1
			&& !Player->bFainted
			&& !Player->bRemoved
			&& !Player->MajorStatusId.IsValid());

	TUniquePtr<FBattleEngine> InvalidEngine;
	if (!TestTrue(TEXT("Invalid partner-plan engine is created"),
			TryMakeSequenceEngine(Scenario, {}, InvalidEngine)))
	{
		return false;
	}
	const FBattleEngineState& InvalidState =
		FBattleC09BWildFlowEngineFixture::GetState(*InvalidEngine);
	const int32 LivingHP = InvalidState.FindBattler(
		MakeNumericId<FBattlerId>(PlayerLeftValue))->CurrentHP;
	FBattlePartnerTeamVictoryRecoveryPlan InvalidPlan;
	TestFalse(TEXT("Living player rejects partner recovery preparation"),
		FBattlePartnerFlow::TryApplyTeamVictoryRecovery(
			InvalidState,
			InvalidPlan));
	TestEqual(TEXT("Rejected partner preparation leaves player unchanged"),
		InvalidState.FindBattler(MakeNumericId<FBattlerId>(PlayerLeftValue))->CurrentHP,
		LivingHP);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E4SimpleVolatileCleanupTest,
	"PokemonSolarus.Battle.ADR0002.3E4.PreMoveGates.Volatile.SimpleAndRestrictionCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E4SimpleVolatileCleanupTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	const FMoveId MoveId = MakeDefinitionId<FMoveId>(ProbeMoveName);
	for (const FConditionId SimpleId : {
		FBattleVolatileRules::GetFlinchId(),
		FBattleVolatileRules::GetRechargeId()})
	{
		TUniquePtr<FBattleEngine> Engine;
		if (!TestTrue(TEXT("Simple-denial engine is created"),
				TryMakeSequenceEngine(MakePreMoveScenario(), {}, Engine))
			|| !TestTrue(TEXT("Simple-denial Fight starts"),
				TryLockAndBeginPreMove(*Engine))
			|| !TestTrue(TEXT("Simple volatile is seeded at the pre-move checkpoint"),
				TrySeedActionStartVolatile(*Engine, ActorId, SimpleId)))
		{
			return false;
		}
		const int32 PPBefore = GetPreMovePP(*Engine, ActorId, MoveId);
		const FBattleResolution Resolution =
			Engine->CommitCurrentMoveAfterPreMoveGates();
		TestTrue(TEXT("Simple volatile denial is accepted"), Resolution.WasAccepted());
		TestFalse(TEXT("Simple volatile is cleaned"),
			FindPreMoveVolatile(*Engine, ActorId, SimpleId) != nullptr);
		TestEqual(TEXT("Simple volatile denial consumes no PP"),
			GetPreMovePP(*Engine, ActorId, MoveId), PPBefore);
		TestTrue(TEXT("Simple volatile denial completes the action"),
			HasEvent(Resolution, EBattleEventType::ActionCompleted));
	}

	TUniquePtr<FBattleEngine> Taunted;
	if (!TestTrue(TEXT("Taunt engine is created"),
			TryMakeSequenceEngine(MakePreMoveScenario(), {}, Taunted))
		|| !TestTrue(TEXT("Taunt Fight starts"), TryLockAndBeginPreMove(*Taunted))
		|| !TestTrue(TEXT("Taunt is seeded"),
			TrySeedActionStartVolatile(
				*Taunted,
				ActorId,
				FBattleVolatileRules::GetTauntId(),
				FDefinitionId(),
				3)))
	{
		return false;
	}
	const int32 TauntPP = GetPreMovePP(*Taunted, ActorId, MoveId);
	const FBattleResolution TauntResult =
		Taunted->CommitCurrentMoveAfterPreMoveGates();
	TestTrue(TEXT("Taunt denial is accepted"), TauntResult.WasAccepted());
	TestEqual(TEXT("Taunt denial consumes no PP"),
		GetPreMovePP(*Taunted, ActorId, MoveId), TauntPP);
	TestTrue(TEXT("Taunt remains active after denying a status move"),
		FindPreMoveVolatile(
			*Taunted,
			ActorId,
			FBattleVolatileRules::GetTauntId()) != nullptr);

	TUniquePtr<FBattleEngine> ExpiredRestrictions;
	if (!TestTrue(TEXT("Restriction-cleanup engine is created"),
			TryMakeSequenceEngine(MakePreMoveScenario(), {}, ExpiredRestrictions))
		|| !TestTrue(TEXT("Restriction-cleanup Fight starts"),
			TryLockAndBeginPreMove(*ExpiredRestrictions))
		|| !TestTrue(TEXT("Invalid Encore payload is seeded"),
			TrySeedActionStartVolatile(
				*ExpiredRestrictions,
				ActorId,
				FBattleVolatileRules::GetEncoreId(),
				MakeDefinitionId<FDefinitionId>(TEXT("Move.ADR0002.3E4.MissingEncore")),
				3))
		|| !TestTrue(TEXT("Invalid Disable payload is seeded"),
			TrySeedActionStartVolatile(
				*ExpiredRestrictions,
				ActorId,
				FBattleVolatileRules::GetDisableId(),
				MakeDefinitionId<FDefinitionId>(TEXT("Move.ADR0002.3E4.MissingDisable")),
				5)))
	{
		return false;
	}
	const int32 RestrictionPP = GetPreMovePP(*ExpiredRestrictions, ActorId, MoveId);
	const FBattleResolution RestrictionResult =
		ExpiredRestrictions->CommitCurrentMoveAfterPreMoveGates();
	TestTrue(TEXT("Stale restrictions clean up and allow the move"),
		RestrictionResult.WasAccepted());
	TestFalse(TEXT("Stale Encore is removed"),
		FindPreMoveVolatile(
			*ExpiredRestrictions,
			ActorId,
			FBattleVolatileRules::GetEncoreId()) != nullptr);
	TestFalse(TEXT("Stale Disable is removed"),
		FindPreMoveVolatile(
			*ExpiredRestrictions,
			ActorId,
			FBattleVolatileRules::GetDisableId()) != nullptr);
	TestEqual(TEXT("Allowed restriction cleanup consumes one PP"),
		GetPreMovePP(*ExpiredRestrictions, ActorId, MoveId), RestrictionPP - 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E4StruggleChoiceTest,
	"PokemonSolarus.Battle.ADR0002.3E4.PreMoveGates.PP.StruggleAndChoiceLock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E4StruggleChoiceTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	TUniquePtr<FBattleEngine> StruggleEngine;
	if (!TestTrue(TEXT("Struggle engine is created"),
			TryMakeSequenceEngine(MakePreMoveScenario(), {}, StruggleEngine)))
	{
		return false;
	}
	FBattleBattlerState* Struggler =
		FBattleC09BWildFlowEngineFixture::GetMutableState(*StruggleEngine)
			.FindMutableBattler(ActorId);
	if (!TestNotNull(TEXT("Struggle actor exists"), Struggler))
	{
		return false;
	}
	for (FBattleMoveSlotState& Slot : Struggler->Moves)
	{
		Slot.CurrentPP = 0;
	}
	if (!TestTrue(TEXT("Struggle is locked and started"),
			TryLockAndBeginPreMove(
				*StruggleEngine,
				FBattleBuiltInMoveDefinitions::GetStruggleMoveId())))
	{
		return false;
	}
	const FBattleResolution Struggle =
		StruggleEngine->CommitCurrentMoveAfterPreMoveGates();
	TestTrue(TEXT("Struggle passes the checkpoint"), Struggle.WasAccepted());
	TestTrue(TEXT("Struggle publishes MoveUsed without PPConsumed"),
		HasExactEventOrder(Struggle, {EBattleEventType::MoveUsed}));
	TestTrue(TEXT("Struggle leaves every ordinary PP slot at zero"),
		Struggler->Moves.ContainsByPredicate(
			[](const FBattleMoveSlotState& Slot)
			{
				return Slot.CurrentPP == 0;
			})
			&& !Struggler->Moves.ContainsByPredicate(
				[](const FBattleMoveSlotState& Slot)
				{
					return Slot.CurrentPP != 0;
				}));

	TUniquePtr<FBattleEngine> ChoiceEngine;
	if (!TestTrue(TEXT("Choice Band engine is created"),
			TryMakeSequenceEngine(
				MakePreMoveScenario(
					FMoveId(),
					200,
					FAbilityId(),
					FBattleItemRules::GetChoiceBandId()),
				{},
				ChoiceEngine))
		|| !TestTrue(TEXT("Choice Band Fight starts"),
			TryLockAndBeginPreMove(*ChoiceEngine)))
	{
		return false;
	}
	const FMoveId ProbeId = MakeDefinitionId<FMoveId>(ProbeMoveName);
	const int32 ChoicePP = GetPreMovePP(*ChoiceEngine, ActorId, ProbeId);
	const FBattleResolution Choice =
		ChoiceEngine->CommitCurrentMoveAfterPreMoveGates();
	const FBattleBattlerState* ChoiceActor =
		FBattleC09BWildFlowEngineFixture::GetState(*ChoiceEngine).FindBattler(ActorId);
	TestTrue(TEXT("Choice Band ordinary move commits"), Choice.WasAccepted());
	TestTrue(TEXT("Choice Band establishes the exact move lock"),
		ChoiceActor != nullptr
			&& ChoiceActor->HeldItem.ChoiceLockedMoveId == ProbeId);
	TestEqual(TEXT("Choice Band first use consumes exactly one PP"),
		GetPreMovePP(*ChoiceEngine, ActorId, ProbeId), ChoicePP - 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E4ChargePPTest,
	"PokemonSolarus.Battle.ADR0002.3E4.PreMoveGates.PP.ChargedFirstTurnReleaseAndDeniedRelease",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E4ChargePPTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	const FMoveId ChargeId = MakeDefinitionId<FMoveId>(ChargeProbeMoveName);
	TUniquePtr<FBattleEngine> FirstTurn;
	if (!TestTrue(TEXT("Charge first-turn engine is created"),
			TryMakeSequenceEngine(MakePreMoveScenario(ChargeId), {}, FirstTurn))
		|| !TestTrue(TEXT("Charge first-turn Fight starts"),
			TryLockAndBeginPreMove(*FirstTurn, ChargeId)))
	{
		return false;
	}
	const int32 FirstPP = GetPreMovePP(*FirstTurn, ActorId, ChargeId);
	const FBattleResolution FirstCommit =
		FirstTurn->CommitCurrentMoveAfterPreMoveGates();
	TestTrue(TEXT("Charge first turn passes the checkpoint"),
		FirstCommit.WasAccepted());
	TestEqual(TEXT("Charge first turn consumes exactly one PP"),
		GetPreMovePP(*FirstTurn, ActorId, ChargeId), FirstPP - 1);

	auto PrepareRelease = [&](TUniquePtr<FBattleEngine>& Out) -> bool
	{
		if (!TryMakeSequenceEngine(MakePreMoveScenario(ChargeId), {}, Out))
		{
			return false;
		}
		FBattleBattlerState* Actor =
			FBattleC09BWildFlowEngineFixture::GetMutableState(*Out)
				.FindMutableBattler(ActorId);
		if (Actor == nullptr)
		{
			return false;
		}
		FBattleMoveSlotState* Slot = Actor->Moves.FindByPredicate(
			[ChargeId](const FBattleMoveSlotState& Candidate)
			{
				return Candidate.MoveId == ChargeId;
			});
		if (Slot == nullptr)
		{
			return false;
		}
		Slot->CurrentPP = 19;
		Actor->LastMoveId = ChargeId;
		return TrySeedActionStartVolatile(
				*Out,
				ActorId,
				FBattleVolatileRules::GetChargingId(),
				ChargeId.GetDefinitionId())
			&& TrySeedActionStartVolatile(
				*Out,
				ActorId,
				FBattleVolatileRules::GetFlySemiInvulnerableId())
			&& TryLockAndBeginPreMove(*Out, ChargeId);
	};

	TUniquePtr<FBattleEngine> Release;
	if (!TestTrue(TEXT("Allowed charged release is prepared"), PrepareRelease(Release)))
	{
		return false;
	}
	const int32 ReleasePP = GetPreMovePP(*Release, ActorId, ChargeId);
	const FBattleResolution ReleaseCommit =
		Release->CommitCurrentMoveAfterPreMoveGates();
	TestTrue(TEXT("Charged release passes the checkpoint"),
		ReleaseCommit.WasAccepted());
	TestEqual(TEXT("Charged release has no second PP cost"),
		GetPreMovePP(*Release, ActorId, ChargeId), ReleasePP);
	TestFalse(TEXT("Charged release publishes no PPConsumed"),
		HasEvent(ReleaseCommit, EBattleEventType::PPConsumed));

	TUniquePtr<FBattleEngine> DeniedRelease;
	if (!TestTrue(TEXT("Denied charged release is prepared"),
			PrepareRelease(DeniedRelease))
		|| !TestTrue(TEXT("Flinch is seeded at denied release gate"),
			TrySeedActionStartVolatile(
				*DeniedRelease,
				ActorId,
				FBattleVolatileRules::GetFlinchId())))
	{
		return false;
	}
	const int32 DeniedPP = GetPreMovePP(*DeniedRelease, ActorId, ChargeId);
	const FBattleResolution Denied =
		DeniedRelease->CommitCurrentMoveAfterPreMoveGates();
	TestTrue(TEXT("Denied charged release is accepted denial"), Denied.WasAccepted());
	TestEqual(TEXT("Denied charged release has no second PP cost"),
		GetPreMovePP(*DeniedRelease, ActorId, ChargeId), DeniedPP);
	TestFalse(TEXT("Denied release clears Charging atomically"),
		FindPreMoveVolatile(
			*DeniedRelease,
			ActorId,
			FBattleVolatileRules::GetChargingId()) != nullptr);
	TestFalse(TEXT("Denied release clears semi-invulnerability atomically"),
		FindPreMoveVolatile(
			*DeniedRelease,
			ActorId,
			FBattleVolatileRules::GetFlySemiInvulnerableId()) != nullptr);
	TestTrue(TEXT("Denied release completes the action"),
		HasEvent(Denied, EBattleEventType::ActionCompleted));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E4StatusFailureTest,
	"PokemonSolarus.Battle.ADR0002.3E4.PreMoveGates.Failure.StatusDispatchAndCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E4StatusFailureTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	const FTrainerId TrainerId = MakeNumericId<FTrainerId>(PlayerTrainerValue);

	TUniquePtr<FBattleEngine> Dispatch;
	if (!TestTrue(TEXT("Status-dispatch failure engine is created"),
			TryMakeSequenceEngine(MakePreMoveScenario(), {}, Dispatch))
		|| !TestTrue(TEXT("Status-dispatch Fight starts"),
			TryLockAndBeginPreMove(*Dispatch)))
	{
		return false;
	}
	FBattleBattlerState* DispatchActor =
		FBattleC09BWildFlowEngineFixture::GetMutableState(*Dispatch)
			.FindMutableBattler(ActorId);
	if (!TestNotNull(TEXT("Status-dispatch actor exists"), DispatchActor))
	{
		return false;
	}
	DispatchActor->MajorStatusId = FBattleMajorStatusRules::GetSleepId();
	const FActionStartCheckpointObservation DispatchBeforeAction =
		ObserveActionStartCheckpoint(*Dispatch, ActorId, TrainerId);
	const FAtomicSwitchCheckpointObservation DispatchBeforeMechanics =
		ObserveAtomicSwitchCheckpoint(*Dispatch);
	const FBattleResolution DispatchRejected =
		Dispatch->CommitCurrentMoveAfterPreMoveGates();
	bool bValid = VerifyRejectedPreMoveCheckpoint(
		*this,
		*Dispatch,
		DispatchBeforeAction,
		DispatchBeforeMechanics,
		EBattleRejectionReason::CheckpointPreparationFailed,
		DispatchRejected);

	TUniquePtr<FBattleEngine> Cleanup;
	if (!TestTrue(TEXT("Status-cleanup failure engine is created"),
			TryMakeSequenceEngine(MakePreMoveScenario(), {0}, Cleanup))
		|| !TestTrue(TEXT("Status-cleanup Fight starts"),
			TryLockAndBeginPreMove(*Cleanup))
		|| !TestTrue(TEXT("Freeze is seeded for cleanup failure"),
			TrySeedPreMoveMajorStatus(
				*Cleanup,
				ActorId,
				FBattleMajorStatusRules::GetFreezeId())))
	{
		return false;
	}
	FBattleC09BWildFlowEngineFixture::GetMutableState(*Cleanup)
		.NextTriggerReentrancyToken = TNumericLimits<uint64>::Max() - 1;
	const FActionStartCheckpointObservation CleanupBeforeAction =
		ObserveActionStartCheckpoint(*Cleanup, ActorId, TrainerId);
	const FAtomicSwitchCheckpointObservation CleanupBeforeMechanics =
		ObserveAtomicSwitchCheckpoint(*Cleanup);
	const FBattleResolution CleanupRejected =
		Cleanup->CommitCurrentMoveAfterPreMoveGates();
	bValid &= VerifyRejectedPreMoveCheckpoint(
		*this,
		*Cleanup,
		CleanupBeforeAction,
		CleanupBeforeMechanics,
		EBattleRejectionReason::CheckpointPreparationFailed,
		CleanupRejected);
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E4VolatileFailureTest,
	"PokemonSolarus.Battle.ADR0002.3E4.PreMoveGates.Failure.VolatileDispatchAndCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E4VolatileFailureTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	const FTrainerId TrainerId = MakeNumericId<FTrainerId>(PlayerTrainerValue);

	TUniquePtr<FBattleEngine> Dispatch;
	if (!TestTrue(TEXT("Volatile-dispatch failure engine is created"),
			TryMakeSequenceEngine(MakePreMoveScenario(), {}, Dispatch))
		|| !TestTrue(TEXT("Volatile-dispatch Fight starts"),
			TryLockAndBeginPreMove(*Dispatch)))
	{
		return false;
	}
	FBattleEngineState& DispatchState =
		FBattleC09BWildFlowEngineFixture::GetMutableState(*Dispatch);
	FBattleBattlerState* DispatchActor = DispatchState.FindMutableBattler(ActorId);
	if (!TestNotNull(TEXT("Volatile-dispatch actor exists"), DispatchActor))
	{
		return false;
	}
	FBattleConditionState MissingRegistration;
	MissingRegistration.ConditionId = FBattleVolatileRules::GetConfusionId();
	MissingRegistration.RemainingTurns = 3;
	MissingRegistration.LayerCount = 1;
	MissingRegistration.CreationOrdinal = DispatchState.NextConditionCreationOrdinal++;
	MissingRegistration.SourceBattlerId = ActorId;
	DispatchActor->Volatiles.Add(MissingRegistration);
	const FActionStartCheckpointObservation DispatchBeforeAction =
		ObserveActionStartCheckpoint(*Dispatch, ActorId, TrainerId);
	const FAtomicSwitchCheckpointObservation DispatchBeforeMechanics =
		ObserveAtomicSwitchCheckpoint(*Dispatch);
	const FBattleResolution DispatchRejected =
		Dispatch->CommitCurrentMoveAfterPreMoveGates();
	bool bValid = VerifyRejectedPreMoveCheckpoint(
		*this,
		*Dispatch,
		DispatchBeforeAction,
		DispatchBeforeMechanics,
		EBattleRejectionReason::CheckpointPreparationFailed,
		DispatchRejected);

	TUniquePtr<FBattleEngine> Cleanup;
	if (!TestTrue(TEXT("Volatile-cleanup failure engine is created"),
			TryMakeSequenceEngine(MakePreMoveScenario(), {}, Cleanup))
		|| !TestTrue(TEXT("Volatile-cleanup Fight starts"),
			TryLockAndBeginPreMove(*Cleanup))
		|| !TestTrue(TEXT("Flinch is seeded for cleanup failure"),
			TrySeedActionStartVolatile(
				*Cleanup,
				ActorId,
				FBattleVolatileRules::GetFlinchId())))
	{
		return false;
	}
	FBattleC09BWildFlowEngineFixture::GetMutableState(*Cleanup)
		.NextTriggerReentrancyToken = TNumericLimits<uint64>::Max() - 1;
	const FActionStartCheckpointObservation CleanupBeforeAction =
		ObserveActionStartCheckpoint(*Cleanup, ActorId, TrainerId);
	const FAtomicSwitchCheckpointObservation CleanupBeforeMechanics =
		ObserveAtomicSwitchCheckpoint(*Cleanup);
	const FBattleResolution CleanupRejected =
		Cleanup->CommitCurrentMoveAfterPreMoveGates();
	bValid &= VerifyRejectedPreMoveCheckpoint(
		*this,
		*Cleanup,
		CleanupBeforeAction,
		CleanupBeforeMechanics,
		EBattleRejectionReason::CheckpointPreparationFailed,
		CleanupRejected);
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E4ConfusionPreparationFailureTest,
	"PokemonSolarus.Battle.ADR0002.3E4.PreMoveGates.Failure.ConfusionDamageAndImmediateHeldItem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E4ConfusionPreparationFailureTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	const FTrainerId TrainerId = MakeNumericId<FTrainerId>(PlayerTrainerValue);

	TUniquePtr<FBattleEngine> Damage;
	FFaultBattleRandom* DamageRandom = nullptr;
	if (!TestTrue(TEXT("Confusion-damage failure engine is created"),
			TryMakeFaultEngine(
				MakePreMoveScenario(),
				{0, 0},
				EFaultRandomMode::Draw,
				Damage,
				DamageRandom,
				1))
		|| !TestNotNull(TEXT("Confusion-damage fault source is retained"), DamageRandom)
		|| !TestTrue(TEXT("Confusion-damage Fight starts"),
			TryLockAndBeginPreMove(*Damage))
		|| !TestTrue(TEXT("Confusion is seeded for damage failure"),
			TrySeedActionStartVolatile(
				*Damage,
				ActorId,
				FBattleVolatileRules::GetConfusionId(),
				FDefinitionId(),
				3)))
	{
		return false;
	}
	const FActionStartCheckpointObservation DamageBeforeAction =
		ObserveActionStartCheckpoint(*Damage, ActorId, TrainerId);
	const FAtomicSwitchCheckpointObservation DamageBeforeMechanics =
		ObserveAtomicSwitchCheckpoint(*Damage);
	const FBattleResolution DamageRejected =
		Damage->CommitCurrentMoveAfterPreMoveGates();
	bool bValid = VerifyRejectedPreMoveCheckpoint(
		*this,
		*Damage,
		DamageBeforeAction,
		DamageBeforeMechanics,
		EBattleRejectionReason::CheckpointRandomStageFailed,
		DamageRejected);
	bValid &= TestEqual(TEXT("Damage failure occurs after one staged gate draw"),
		DamageRandom->GetCounters().SuccessfulDraws, 1);

	TUniquePtr<FBattleEngine> Item;
	if (!TestTrue(TEXT("Immediate-item failure engine is created"),
			TryMakeSequenceEngine(
				MakePreMoveScenario(
					FMoveId(),
					200,
					FAbilityId(),
					FBattleItemRules::GetLumBerryId()),
				{0, 0},
				Item))
		|| !TestTrue(TEXT("Immediate-item Fight starts"),
			TryLockAndBeginPreMove(*Item))
		|| !TestTrue(TEXT("Confusion is seeded for immediate-item failure"),
			TrySeedActionStartVolatile(
				*Item,
				ActorId,
				FBattleVolatileRules::GetConfusionId(),
				FDefinitionId(),
				3)))
	{
		return false;
	}
	FBattleC09BWildFlowEngineFixture::GetMutableState(*Item)
		.NextTriggerReentrancyToken = TNumericLimits<uint64>::Max() - 1;
	const FActionStartCheckpointObservation ItemBeforeAction =
		ObserveActionStartCheckpoint(*Item, ActorId, TrainerId);
	const FAtomicSwitchCheckpointObservation ItemBeforeMechanics =
		ObserveAtomicSwitchCheckpoint(*Item);
	const FBattleResolution ItemRejected =
		Item->CommitCurrentMoveAfterPreMoveGates();
	bValid &= VerifyRejectedPreMoveCheckpoint(
		*this,
		*Item,
		ItemBeforeAction,
		ItemBeforeMechanics,
		EBattleRejectionReason::CheckpointPreparationFailed,
		ItemRejected);
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E4FaintRecoveryFailureTest,
	"PokemonSolarus.Battle.ADR0002.3E4.PreMoveGates.Failure.FaintOutcomeAndPartnerRecovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E4FaintRecoveryFailureTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	const FBattlerId TargetId = MakeNumericId<FBattlerId>(OpponentLeftValue);
	const FTrainerId TrainerId = MakeNumericId<FTrainerId>(PlayerTrainerValue);
	TUniquePtr<FBattleEngine> Engine;
	if (!TestTrue(TEXT("Faint-outcome failure engine is created"),
			TryMakeSequenceEngine(MakePreMoveScenario(FMoveId(), 1), {0, 0}, Engine))
		|| !TestTrue(TEXT("Faint-outcome Fight starts"),
			TryLockAndBeginPreMove(*Engine))
		|| !TestTrue(TEXT("Confusion is seeded for lethal projection"),
			TrySeedActionStartVolatile(
				*Engine,
				ActorId,
				FBattleVolatileRules::GetConfusionId(),
				FDefinitionId(),
				3)))
	{
		return false;
	}
	FBattleBattlerState* Target =
		FBattleC09BWildFlowEngineFixture::GetMutableState(*Engine)
			.FindMutableBattler(TargetId);
	if (!TestNotNull(TEXT("Faint-outcome target exists"), Target))
	{
		return false;
	}
	Target->bFaintTransitionPending = true;
	const FActionStartCheckpointObservation BeforeAction =
		ObserveActionStartCheckpoint(*Engine, ActorId, TrainerId);
	const FAtomicSwitchCheckpointObservation BeforeMechanics =
		ObserveAtomicSwitchCheckpoint(*Engine);
	const FBattleResolution Rejected =
		Engine->CommitCurrentMoveAfterPreMoveGates();
	bool bValid = VerifyRejectedPreMoveCheckpoint(
		*this,
		*Engine,
		BeforeAction,
		BeforeMechanics,
		EBattleRejectionReason::CheckpointPreparationFailed,
		Rejected);

	FAtomicWildScenario PartnerScenario = MakePreMoveScenario();
	PartnerScenario.Format = EBattleFormat::PartnerDouble;
	TUniquePtr<FBattleEngine> Partner;
	if (!TestTrue(TEXT("Partner-recovery failure engine is created"),
			TryMakeSequenceEngine(PartnerScenario, {}, Partner)))
	{
		return false;
	}
	const FBattleEngineState& PartnerState =
		FBattleC09BWildFlowEngineFixture::GetState(*Partner);
	const FBattleBattlerState* LivingPlayer = PartnerState.FindBattler(ActorId);
	const int32 LivingHP = LivingPlayer != nullptr ? LivingPlayer->CurrentHP : INDEX_NONE;
	FBattlePartnerTeamVictoryRecoveryPlan InvalidPlan;
	bValid &= TestFalse(TEXT("Invalid partner recovery preparation is recoverable"),
		FBattlePartnerFlow::TryApplyTeamVictoryRecovery(
			PartnerState,
			InvalidPlan));
	bValid &= TestTrue(TEXT("Partner recovery preparation failure is non-mutating"),
		LivingPlayer != nullptr
			&& LivingPlayer->CurrentHP == LivingHP
			&& !LivingPlayer->bFainted);
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E4RandomFailureTest,
	"PokemonSolarus.Battle.ADR0002.3E4.PreMoveGates.Failure.RandomTransactionCreateDrawCommit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E4RandomFailureTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	const FTrainerId TrainerId = MakeNumericId<FTrainerId>(PlayerTrainerValue);
	auto RunFault = [&](const EFaultRandomMode Mode,
		const uint32 Draw,
		const EBattleRejectionReason ExpectedReason) -> bool
	{
		TUniquePtr<FBattleEngine> Engine;
		FFaultBattleRandom* Random = nullptr;
		if (!TestTrue(TEXT("RNG-fault engine is created"),
				TryMakeFaultEngine(
					MakePreMoveScenario(),
					{Draw},
					Mode,
					Engine,
					Random))
			|| !TestNotNull(TEXT("RNG-fault source is retained"), Random)
			|| !TestTrue(TEXT("RNG-fault Fight starts"),
				TryLockAndBeginPreMove(*Engine))
			|| !TestTrue(TEXT("Paralysis is seeded for RNG fault"),
				TrySeedPreMoveMajorStatus(
					*Engine,
					ActorId,
					FBattleMajorStatusRules::GetParalysisId())))
		{
			return false;
		}
		const FActionStartCheckpointObservation BeforeAction =
			ObserveActionStartCheckpoint(*Engine, ActorId, TrainerId);
		const FAtomicSwitchCheckpointObservation BeforeMechanics =
			ObserveAtomicSwitchCheckpoint(*Engine);
		const FBattleResolution Rejected =
			Engine->CommitCurrentMoveAfterPreMoveGates();
		return VerifyRejectedPreMoveCheckpoint(
			*this,
			*Engine,
			BeforeAction,
			BeforeMechanics,
			ExpectedReason,
			Rejected);
	};
	bool bValid = RunFault(
		EFaultRandomMode::CreateTransaction,
		0,
		EBattleRejectionReason::CheckpointRandomStageFailed);
	bValid &= RunFault(
		EFaultRandomMode::Draw,
		0,
		EBattleRejectionReason::CheckpointRandomStageFailed);
	bValid &= RunFault(
		EFaultRandomMode::Commit,
		3,
		EBattleRejectionReason::RandomTransactionCommitFailed);
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E4PlanStaleFailureTest,
	"PokemonSolarus.Battle.ADR0002.3E4.PreMoveGates.Failure.PlanStagingAndStaleExactFightIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E4PlanStaleFailureTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	const FTrainerId TrainerId = MakeNumericId<FTrainerId>(PlayerTrainerValue);

	TUniquePtr<FBattleEngine> Plan;
	if (!TestTrue(TEXT("Plan-staging failure engine is created"),
			TryMakeSequenceEngine(MakePreMoveScenario(), {}, Plan))
		|| !TestTrue(TEXT("Plan-staging Fight starts"),
			TryLockAndBeginPreMove(*Plan)))
	{
		return false;
	}
	FBattleC09BWildFlowEngineFixture::GetMutableState(*Plan).NextEventOrdinal =
		TNumericLimits<uint64>::Max() - 1;
	const FActionStartCheckpointObservation PlanBeforeAction =
		ObserveActionStartCheckpoint(*Plan, ActorId, TrainerId);
	const FAtomicSwitchCheckpointObservation PlanBeforeMechanics =
		ObserveAtomicSwitchCheckpoint(*Plan);
	const FBattleResolution PlanRejected =
		Plan->CommitCurrentMoveAfterPreMoveGates();
	bool bValid = VerifyRejectedPreMoveCheckpoint(
		*this,
		*Plan,
		PlanBeforeAction,
		PlanBeforeMechanics,
		EBattleRejectionReason::CheckpointPreparationFailed,
		PlanRejected);

	TUniquePtr<FBattleEngine> Stale;
	FActionStartStaleRandom* StaleRandom = nullptr;
	if (!TestTrue(TEXT("Stale-Fight engine is created"),
			TryMakeActionStartStaleEngine(
				MakePreMoveScenario(),
				Stale,
				StaleRandom))
		|| !TestNotNull(TEXT("Stale-Fight random seam is retained"), StaleRandom)
		|| !TestTrue(TEXT("Stale-Fight action starts"),
			TryLockAndBeginPreMove(*Stale)))
	{
		return false;
	}
	const FActionStartCheckpointObservation StaleBefore =
		ObserveActionStartCheckpoint(*Stale, ActorId, TrainerId);
	const FAtomicSwitchCheckpointObservation StaleMechanicsBefore =
		ObserveAtomicSwitchCheckpoint(*Stale);
	FBattleEngineState& MutableStale =
		FBattleC09BWildFlowEngineFixture::GetMutableState(*Stale);
	const int32 ActionIndex = MutableStale.CurrentLockedActionIndex;
	const uint64 QueueOrdinalBefore =
		MutableStale.LockedActions[ActionIndex].QueueOrdinal;
	bool bInjectedMutation = false;
	StaleRandom->ArmAfterTraceRead(
		2,
		[EnginePtr = Stale.Get(), ActionIndex, &bInjectedMutation]()
		{
			FBattleEngineState& State =
				FBattleC09BWildFlowEngineFixture::GetMutableState(*EnginePtr);
			if (State.LockedActions.IsValidIndex(ActionIndex))
			{
				++State.LockedActions[ActionIndex].QueueOrdinal;
				bInjectedMutation = true;
			}
		});
	const FBattleResolution StaleRejected =
		Stale->CommitCurrentMoveAfterPreMoveGates();
	const int32 TraceReads = StaleRandom->GetReadsSinceArm();
	const bool bInjected = StaleRandom->WasInjected();
	StaleRandom->Disarm();
	const FBattleEngineState& StaleState =
		FBattleC09BWildFlowEngineFixture::GetState(*Stale);
	const FActionStartCheckpointObservation StaleAfter =
		ObserveActionStartCheckpoint(*Stale, ActorId, TrainerId);
	bValid &= TestTrue(TEXT("Exact Fight identity changes only at final recheck"),
		bInjected && bInjectedMutation && TraceReads == 2);
	bValid &= TestFalse(TEXT("Stale exact Fight identity rejects"),
		StaleRejected.WasAccepted());
	bValid &= TestEqual(TEXT("Stale Fight rejection is typed"),
		StaleRejected.GetRejection().Reason,
		EBattleRejectionReason::StaleCheckpointIdentity);
	bValid &= TestTrue(TEXT("Concurrent exact action change remains authoritative"),
		StaleState.LockedActions[ActionIndex].QueueOrdinal == QueueOrdinalBefore + 1);
	bValid &= TestEqual(TEXT("Stale Fight rejection preserves PP"),
		StaleAfter.TotalMovePP, StaleBefore.TotalMovePP);
	bValid &= TestEqual(TEXT("Stale Fight rejection preserves cursor"),
		StaleAfter.ActionIndex, StaleBefore.ActionIndex);
	bValid &= TestEqual(TEXT("Stale Fight rejection preserves gameplay RNG"),
		StaleAfter.RandomTraceCount, StaleBefore.RandomTraceCount);
	bValid &= TestTrue(TEXT("Stale Fight rejection preserves non-concurrent mechanics"),
		AreAtomicSwitchMechanicsIdentical(
			ObserveAtomicSwitchCheckpoint(*Stale),
			StaleMechanicsBefore));
	bValid &= TestTrue(TEXT("Stale Fight rejection is appended exactly once"),
		IsReturnedResolutionAppendedExactlyOnce(*Stale, StaleRejected));
	bValid &= TestFalse(TEXT("Stale Fight rejection publishes no success fact"),
		StaleRejected.GetEvents().ContainsByPredicate(
			[](const FBattleEvent& Event)
			{
				return IsPreMoveSuccessEvent(Event.GetType());
			}));
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E4RejectionPreservationTest,
	"PokemonSolarus.Battle.ADR0002.3E4.PreMoveGates.Failure.RejectionExactOncePreservesStateAndReplayFacts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E4RejectionPreservationTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	const FBattlerId TargetId = MakeNumericId<FBattlerId>(OpponentLeftValue);
	const FTrainerId TrainerId = MakeNumericId<FTrainerId>(PlayerTrainerValue);
	FAtomicWildScenario Scenario = MakePreMoveScenario(
		FMoveId(),
		150,
		FAbilityId(),
		FBattleItemRules::GetChoiceBandId());
	Scenario.TargetCurrentHP = 137;
	TUniquePtr<FBattleEngine> Engine;
	if (!TestTrue(TEXT("Preservation engine is created"),
			TryMakeSequenceEngine(Scenario, {}, Engine))
		|| !TestTrue(TEXT("Preservation Fight starts"),
			TryLockAndBeginPreMove(*Engine))
		|| !TestTrue(TEXT("Preserved actor volatile is seeded"),
			TrySeedActionStartVolatile(
				*Engine,
				ActorId,
				FBattleVolatileRules::GetFlinchId()))
		|| !TestTrue(TEXT("Preserved target status is seeded"),
			TrySeedPreMoveMajorStatus(
				*Engine,
				TargetId,
				FBattleMajorStatusRules::GetParalysisId())))
	{
		return false;
	}
	FBattleBattlerState* Actor =
		FBattleC09BWildFlowEngineFixture::GetMutableState(*Engine)
			.FindMutableBattler(ActorId);
	if (!TestNotNull(TEXT("Preservation actor exists"), Actor))
	{
		return false;
	}
	Actor->MajorStatusId = FBattleMajorStatusRules::GetSleepId();
	const FActionStartCheckpointObservation BeforeAction =
		ObserveActionStartCheckpoint(*Engine, ActorId, TrainerId);
	const FAtomicSwitchCheckpointObservation BeforeMechanics =
		ObserveAtomicSwitchCheckpoint(*Engine);
	const FBattleResolution Rejected =
		Engine->CommitCurrentMoveAfterPreMoveGates();
	return VerifyRejectedPreMoveCheckpoint(
		*this,
		*Engine,
		BeforeAction,
		BeforeMechanics,
		EBattleRejectionReason::CheckpointPreparationFailed,
		Rejected);
}

} // namespace BattleAtomicPreMoveTestsPrivate

#endif
