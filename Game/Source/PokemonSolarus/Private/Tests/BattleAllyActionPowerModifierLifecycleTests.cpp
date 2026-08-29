#if WITH_DEV_AUTOMATION_TESTS

#include "Battle/BattleAllyActionPowerModifier.h"
#include "Battle/BattleEffectExecutor.h"
#include "Battle/BattleState.h"
#include "BattleAtomicMoveCheckpointTestSupport.h"
#include "BattleAtomicSwitchTestSupport.h"

namespace BattleAllyActionPowerModifierLifecycleTestsPrivate
{
	using namespace BattleAtomicCheckpointTestCommonPrivate;
	using namespace BattleAtomicMoveCheckpointTestSupportPrivate;

	const TCHAR* const R3LifecycleSourceMoveName =
		TEXT("Move.C10R3.Lifecycle.Source");
	const TCHAR* const R3LifecycleMultiHitMoveName =
		TEXT("Move.C10R3.Lifecycle.MultiHit");
	const TCHAR* const R3LifecycleSpreadMoveName =
		TEXT("Move.C10R3.Lifecycle.Spread");

	FBattleBattlerTarget FindR3LifecycleTarget(
		const FBattleEngineState& State,
		const FBattlerId BattlerId)
	{
		const FBattleActivePositionState* Active =
			State.ActivePositions.FindByPredicate(
				[BattlerId](const FBattleActivePositionState& Candidate)
				{
					return Candidate.bAvailable
						&& Candidate.BattlerId == BattlerId;
				});
		return Active != nullptr
			? FBattleBattlerTarget{Active->ActiveSlotId, BattlerId}
			: FBattleBattlerTarget();
	}

	const FBattleLockedActionState* FindR3LifecycleFightAction(
		const FBattleEngineState& State,
		const FBattlerId BattlerId)
	{
		return State.LockedActions.FindByPredicate(
			[BattlerId](const FBattleLockedActionState& Action)
			{
				return Action.Decision.GetActingBattlerId() == BattlerId
					&& Action.Decision.GetActionKind() == EBattleActionKind::Fight
					&& !Action.bFinished;
			});
	}

	bool TrySeedR3LifecycleBoundRegistration(
		FBattleEngine& Engine,
		const uint64 TargetBattlerValue,
		const uint64 SourceActionValue)
	{
		FBattleEngineState& State =
			FBattleC09BWildFlowEngineFixture::GetMutableState(Engine);
		const FBattlerId TargetBattlerId =
			MakeNumericId<FBattlerId>(TargetBattlerValue);
		const FBattleBattlerTarget Target =
			FindR3LifecycleTarget(State, TargetBattlerId);
		const FBattleLockedActionState* TargetAction =
			FindR3LifecycleFightAction(State, TargetBattlerId);
		if (!Target.IsValid() || TargetAction == nullptr)
		{
			return false;
		}

		FBattleAllyActionPowerModifierRegistration& Registration =
			State.AllyActionPowerModifierRegistrations.AddDefaulted_GetRef();
		Registration.TurnId = State.TurnId;
		Registration.SourceActionId =
			MakeNumericId<FActionId>(SourceActionValue);
		Registration.SourceMoveId = MakeDefinitionId<FMoveId>(
			R3LifecycleSourceMoveName);
		Registration.TargetActionId = TargetAction->ActionId;
		Registration.Target = Target;
		Registration.MagnitudeNumerator = 3;
		Registration.MagnitudeDenominator = 2;
		return FBattleAllyActionPowerModifier::IsRegistrationCollectionValid(
			State.Format,
			State.TurnId,
			State.AllyActionPowerModifierRegistrations,
			State.Battlers,
			State.ActivePositions,
			State.LockedActions);
	}

	bool TrySeedR3LifecycleNewEntryRegistration(
		FBattleEngine& Engine,
		const uint64 TargetBattlerValue,
		const uint64 SourceActionValue)
	{
		FBattleEngineState& State =
			FBattleC09BWildFlowEngineFixture::GetMutableState(Engine);
		const FBattlerId TargetBattlerId =
			MakeNumericId<FBattlerId>(TargetBattlerValue);
		const FBattleBattlerTarget Target =
			FindR3LifecycleTarget(State, TargetBattlerId);
		const FBattleBattlerState* TargetBattler =
			State.FindBattler(TargetBattlerId);
		if (!Target.IsValid()
			|| TargetBattler == nullptr
			|| TargetBattler->EnteredActiveOnTurnId != State.TurnId
			|| State.LockedActions.ContainsByPredicate(
				[TargetBattlerId](const FBattleLockedActionState& Action)
				{
					return Action.Decision.GetActingBattlerId()
						== TargetBattlerId;
				}))
		{
			return false;
		}

		FBattleAllyActionPowerModifierRegistration& Registration =
			State.AllyActionPowerModifierRegistrations.AddDefaulted_GetRef();
		Registration.TurnId = State.TurnId;
		Registration.SourceActionId =
			MakeNumericId<FActionId>(SourceActionValue);
		Registration.SourceMoveId = MakeDefinitionId<FMoveId>(
			R3LifecycleSourceMoveName);
		Registration.Target = Target;
		Registration.MagnitudeNumerator = 3;
		Registration.MagnitudeDenominator = 2;
		return FBattleAllyActionPowerModifier::IsRegistrationCollectionValid(
			State.Format,
			State.TurnId,
			State.AllyActionPowerModifierRegistrations,
			State.Battlers,
			State.ActivePositions,
			State.LockedActions);
	}

	bool HasR3LifecycleTarget(
		const FBattleEngineState& State,
		const uint64 BattlerValue)
	{
		const FBattlerId BattlerId = MakeNumericId<FBattlerId>(BattlerValue);
		return State.AllyActionPowerModifierRegistrations.ContainsByPredicate(
			[BattlerId](
				const FBattleAllyActionPowerModifierRegistration& Registration)
			{
				return Registration.Target.BattlerId == BattlerId;
			});
	}

	bool TryPrepareR3LifecycleEffectsCheckpoint(
		FBattleEngine& Engine,
		const FMoveId MoveId)
	{
		return TryPrepareTargetCheckpoint(Engine, MoveId)
			&& Engine.ResolveCurrentMoveTargets().WasAccepted();
	}

	bool TryMakeR3LifecycleOpponentReserveEngine(
		const FAtomicWildScenario& Scenario,
		TArray<FBattleExpectedRandomDraw> ExpectedDraws,
		TUniquePtr<FBattleEngine>& OutEngine,
		FStrictBattleRandom*& OutRandom)
	{
		FBattleSetupInput Input = MakeSetupInput(Scenario);
		FBattlePartyEntrySetup* Reserve = Input.PartyEntries.FindByPredicate(
			[](const FBattlePartyEntrySetup& Entry)
			{
				return Entry.BattlerId
					== MakeNumericId<FBattlerId>(OpponentReserveValue);
			});
		if (Reserve == nullptr)
		{
			return false;
		}
		Reserve->PartySlotId = MakePartySlotId(2);
		FBattleSetup Setup;
		EBattleSetupValidationError SetupError = EBattleSetupValidationError::None;
		if (!FBattleSetup::TryCreate(Input, Setup, SetupError))
		{
			return false;
		}
		TUniquePtr<FStrictBattleRandom> Strict =
			MakeUnique<FStrictBattleRandom>(MoveTemp(ExpectedDraws));
		OutRandom = Strict.Get();
		TUniquePtr<IBattleRandom> Random = MoveTemp(Strict);
		FBattleRejection Rejection;
		return FBattleEngine::TryCreate(
			Setup,
			MakeCatalog(Scenario),
			MoveTemp(Random),
			OutEngine,
			Rejection);
	}

	bool TryDrainR3LifecycleFightTurn(FBattleEngine& Engine)
	{
		for (int32 Guard = 0; Guard < 12; ++Guard)
		{
			const FBattleEngineState& Before =
				FBattleC09BWildFlowEngineFixture::GetState(Engine);
			if (Before.Phase == EBattlePhase::EndOfTurn)
			{
				return true;
			}
			if (Before.Phase != EBattlePhase::Resolving)
			{
				return false;
			}
			const int32 CursorBefore = Before.CurrentLockedActionIndex;
			if (!Before.LockedActions.IsValidIndex(CursorBefore))
			{
				return false;
			}
			if (!Before.LockedActions[CursorBefore].bStarted)
			{
				if (!Engine.BeginNextLockedAction().WasAccepted())
				{
					return false;
				}
				const FBattleEngineState& Started =
					FBattleC09BWildFlowEngineFixture::GetState(Engine);
				if (Started.Phase == EBattlePhase::EndOfTurn
					|| Started.CurrentLockedActionIndex != CursorBefore)
				{
					continue;
				}
			}

			const FBattleEngineState& Started =
				FBattleC09BWildFlowEngineFixture::GetState(Engine);
			if (!Started.LockedActions.IsValidIndex(
					Started.CurrentLockedActionIndex))
			{
				return false;
			}
			const FBattleLockedActionState& Action =
				Started.LockedActions[Started.CurrentLockedActionIndex];
			if (Action.bFinished)
			{
				continue;
			}
			if (Action.Decision.GetActionKind() != EBattleActionKind::Fight)
			{
				return false;
			}
			if (!Action.bMoveCommitted
				&& !Engine.CommitCurrentMoveAfterPreMoveGates().WasAccepted())
			{
				return false;
			}
			const FBattleEngineState& Committed =
				FBattleC09BWildFlowEngineFixture::GetState(Engine);
			if (Committed.Phase == EBattlePhase::EndOfTurn
				|| Committed.CurrentLockedActionIndex != CursorBefore)
			{
				continue;
			}
			if (!Committed.LockedActions[CursorBefore].TargetResolution.IsSet()
				&& !Engine.ResolveCurrentMoveTargets().WasAccepted())
			{
				return false;
			}
			const FBattleEngineState& Targeted =
				FBattleC09BWildFlowEngineFixture::GetState(Engine);
			if (Targeted.Phase == EBattlePhase::EndOfTurn
				|| Targeted.CurrentLockedActionIndex != CursorBefore)
			{
				continue;
			}
			if (!Targeted.LockedActions[CursorBefore].bFinished
				&& !Engine.ExecuteCurrentMoveEffects().WasAccepted())
			{
				return false;
			}
		}
		return FBattleC09BWildFlowEngineFixture::GetState(Engine).Phase
			== EBattlePhase::EndOfTurn;
	}

	FBattleAllyActionPowerModifierRegistration MakeR3LifecycleRegistration(
		const uint64 SourceActionValue,
		const uint64 TargetActionValue,
		const FBattleBattlerTarget& Target)
	{
		FBattleAllyActionPowerModifierRegistration Registration;
		Registration.TurnId = MakeNumericId<FTurnId>(1);
		Registration.SourceActionId =
			MakeNumericId<FActionId>(SourceActionValue);
		Registration.SourceMoveId = MakeDefinitionId<FMoveId>(
			R3LifecycleSourceMoveName);
		Registration.TargetActionId =
			MakeNumericId<FActionId>(TargetActionValue);
		Registration.Target = Target;
		Registration.MagnitudeNumerator = 3;
		Registration.MagnitudeDenominator = 2;
		return Registration;
	}

	FBattleMoveDefinition MakeR3LifecycleDamageMove(
		const TCHAR* Name,
		const EBattleTargetClass TargetClass,
		const bool bMultiHit)
	{
		FBattleMoveDefinition Move;
		Move.Id = MakeDefinitionId<FMoveId>(Name);
		Move.Type = EPokemonType::Normal;
		Move.Category = EBattleMoveCategory::Physical;
		Move.Power = 10;
		Move.bAlwaysHits = true;
		Move.bUsesPP = true;
		Move.BasePP = 20;
		Move.bAllowsPPBoosts = true;
		Move.TargetClass = TargetClass;
		Move.Flags = EBattleMoveFlags::NeverCritical;
		if (bMultiHit)
		{
			FBattleMoveEffectDescriptor MultiHit;
			MultiHit.Kind = EBattleMoveEffectKind::MultiHit;
			MultiHit.Target = EBattleEffectTarget::ResolvedTarget;
			MultiHit.MinimumCount = 3;
			MultiHit.MaximumCount = 3;
			Move.Effects.Add(MultiHit);
		}
		FBattleMoveEffectDescriptor Damage;
		Damage.Order = bMultiHit ? 1 : 0;
		Damage.Kind = EBattleMoveEffectKind::Damage;
		Damage.Target = TargetClass == EBattleTargetClass::FixedOpponentSpreadSet
			? EBattleEffectTarget::AllResolvedTargets
			: EBattleEffectTarget::ResolvedTarget;
		Move.Effects.Add(Damage);
		return Move;
	}

	bool TryMakeR3LifecycleDamageEngine(
		const FBattleMoveDefinition& Move,
		TArray<FBattleExpectedRandomDraw> ExpectedDraws,
		TUniquePtr<FBattleEngine>& OutEngine,
		FStrictBattleRandom*& OutRandom)
	{
		FAtomicWildScenario Scenario = MakePreMoveScenario(Move.Id);
		Scenario.Format = EBattleFormat::Double;
		FBattleDefinitionCatalogInput Input;
		Input.TypeChartEntries = MakeNeutralTypeChart();
		Input.Moves = {MakeProbeMove(), MakeTargetProbeMove(), Move};
		Input.Abilities = {{FBattleAbilityRules::GetBlazeId()},
			{FBattleAbilityRules::GetIntimidateId()},
			{FBattleAbilityRules::GetMagicGuardId()}};
		Input.SpeciesForms = {MakeSpecies(PlayerSpeciesName),
			MakeSpecies(WildSpeciesName)};
		FBattleDefinitionCatalog Catalog;
		TArray<FBattleCatalogDiagnostic> Diagnostics;
		FBattleSetup Setup;
		EBattleSetupValidationError SetupError =
			EBattleSetupValidationError::None;
		if (!FBattleDefinitionCatalog::TryCreate(
				Input, Catalog, Diagnostics)
			|| !FBattleSetup::TryCreate(
				MakeSetupInput(Scenario), Setup, SetupError))
		{
			return false;
		}
		TUniquePtr<FStrictBattleRandom> Strict =
			MakeUnique<FStrictBattleRandom>(MoveTemp(ExpectedDraws));
		OutRandom = Strict.Get();
		TUniquePtr<IBattleRandom> Random = MoveTemp(Strict);
		FBattleRejection Rejection;
		return FBattleEngine::TryCreate(
			Setup, Catalog, MoveTemp(Random), OutEngine, Rejection);
	}

	bool TryPrepareR3LifecycleDamage(
		FBattleEngine& Engine,
		const FMoveId MoveId)
	{
		return LockTurn(Engine, PlayerLeftValue,
				EBattleActionKind::Fight, MoveId)
			&& BeginExpectedWildAction(Engine, PlayerLeftValue,
				EBattleActionKind::Fight)
			&& Engine.CommitCurrentMoveAfterPreMoveGates().WasAccepted()
			&& Engine.ResolveCurrentMoveTargets().WasAccepted();
	}

	bool TryRunR3LifecycleDamage(
		const FBattleMoveDefinition& Move,
		TArray<FBattleExpectedRandomDraw> ExpectedDraws,
		const bool bSeedModifiers,
		TArray<int64>& OutDamageDeltas)
	{
		TUniquePtr<FBattleEngine> Engine;
		FStrictBattleRandom* Random = nullptr;
		if (!TryMakeR3LifecycleDamageEngine(
				Move, MoveTemp(ExpectedDraws), Engine, Random)
			|| !TryPrepareR3LifecycleDamage(*Engine, Move.Id)
			|| (bSeedModifiers
				&& (!TrySeedR3LifecycleBoundRegistration(
						*Engine, PlayerLeftValue, 7001)
					|| !TrySeedR3LifecycleBoundRegistration(
						*Engine, PlayerLeftValue, 7002))))
		{
			return false;
		}
		const FBattleResolution Resolution = Engine->ExecuteCurrentMoveEffects();
		OutDamageDeltas.Reset();
		for (const FBattleEvent& Event : Resolution.GetEvents())
		{
			if (Event.GetType() == EBattleEventType::Damage
				&& Event.GetNumericDelta().IsSet())
			{
				OutDamageDeltas.Add(Event.GetNumericDelta().GetValue());
			}
		}
		return Resolution.WasAccepted()
			&& Random != nullptr
			&& Random->IsExact()
			&& FBattleC09BWildFlowEngineFixture::GetState(*Engine)
				.AllyActionPowerModifierRegistrations.IsEmpty();
	}
}

using namespace BattleAllyActionPowerModifierLifecycleTestsPrivate;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC10R3MultiHitSpreadLifetimeTest,
	"PokemonSolarus.Battle.C05B.C10ActionModifiers.Damage.MultiHitSpreadOneActionThenExpires",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBattleC10R3MultiHitSpreadLifetimeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FBattleMoveDefinition MultiHit = MakeR3LifecycleDamageMove(
		R3LifecycleMultiHitMoveName,
		EBattleTargetClass::SelectedOpponent,
		true);
	const FBattleMoveDefinition Spread = MakeR3LifecycleDamageMove(
		R3LifecycleSpreadMoveName,
		EBattleTargetClass::FixedOpponentSpreadSet,
		false);
	const TArray<FBattleExpectedRandomDraw> MultiHitDraws =
	{
		{0, 15, 0, FBattleEffectExecutor::GetDamageRandomRulePurpose()},
		{0, 15, 0, FBattleEffectExecutor::GetDamageRandomRulePurpose()},
		{0, 15, 0, FBattleEffectExecutor::GetDamageRandomRulePurpose()}
	};
	const TArray<FBattleExpectedRandomDraw> SpreadDraws =
	{
		{0, 15, 0, FBattleEffectExecutor::GetDamageRandomRulePurpose()},
		{0, 15, 0, FBattleEffectExecutor::GetDamageRandomRulePurpose()}
	};
	TArray<int64> MultiHitBase;
	TArray<int64> MultiHitModified;
	TArray<int64> SpreadBase;
	TArray<int64> SpreadModified;
	bool bValid = TestTrue(TEXT("Baseline and modified three-hit actions execute"),
		TryRunR3LifecycleDamage(MultiHit, MultiHitDraws, false, MultiHitBase)
			&& TryRunR3LifecycleDamage(
				MultiHit, MultiHitDraws, true, MultiHitModified));
	bValid &= TestTrue(TEXT("Every hit receives both modifiers before expiry"),
		MultiHitBase.Num() == 3
			&& MultiHitModified.Num() == 3
			&& MultiHitModified[0] < MultiHitBase[0]
			&& MultiHitModified[1] < MultiHitBase[1]
			&& MultiHitModified[2] < MultiHitBase[2]);
	bValid &= TestTrue(TEXT("Baseline and modified two-target spreads execute"),
		TryRunR3LifecycleDamage(Spread, SpreadDraws, false, SpreadBase)
			&& TryRunR3LifecycleDamage(
				Spread, SpreadDraws, true, SpreadModified));
	bValid &= TestTrue(TEXT("Every spread target receives both modifiers before expiry"),
		SpreadBase.Num() == 2
			&& SpreadModified.Num() == 2
			&& SpreadModified[0] < SpreadBase[0]
			&& SpreadModified[1] < SpreadBase[1]);
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC10R3CancellationAndCompletionCleanupTest,
	"PokemonSolarus.Battle.C05B.C10ActionModifiers.Lifecycle.ActionStartPreMoveTargetAndCompletionCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC10R3CancellationAndCompletionCleanupTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	bool bValid = true;
	const FBattlerId PlayerLeft = MakeNumericId<FBattlerId>(PlayerLeftValue);

	FAtomicWildScenario RechargeScenario = MakePreMoveScenario();
	RechargeScenario.Format = EBattleFormat::Double;
	TUniquePtr<FBattleEngine> RechargeEngine;
	FStrictBattleRandom* RechargeRandom = nullptr;
	if (!TestTrue(TEXT("Recharge cleanup engine is created"),
			TryMakeStrictEngine(RechargeScenario, {},
				RechargeEngine, RechargeRandom))
		|| !TestTrue(TEXT("Recharge cleanup turn locks"),
			LockTurn(*RechargeEngine, PlayerLeftValue,
				EBattleActionKind::Fight))
		|| !TestTrue(TEXT("Recharge target binding is seeded"),
			TrySeedR3LifecycleBoundRegistration(
				*RechargeEngine, PlayerLeftValue, 7101))
		|| !TestTrue(TEXT("Recharge is seeded"),
			TrySeedActionStartVolatile(*RechargeEngine, PlayerLeft,
				FBattleVolatileRules::GetRechargeId())))
	{
		return false;
	}
	const FBattleResolution Recharge = RechargeEngine->BeginNextLockedAction();
	bValid &= TestTrue(TEXT("Action-start denial removes the bound registration"),
		Recharge.WasAccepted()
			&& !HasR3LifecycleTarget(
				FBattleC09BWildFlowEngineFixture::GetState(*RechargeEngine),
				PlayerLeftValue)
			&& RechargeRandom != nullptr
			&& RechargeRandom->IsExact());

	FAtomicWildScenario ObedienceScenario = MakePreMoveScenario();
	ObedienceScenario.Format = EBattleFormat::Double;
	ObedienceScenario.bPlayerSubjectToObedience = true;
	ObedienceScenario.PlayerReferenceLevel = 21;
	ObedienceScenario.PlayerBadgeCount = 0;
	TUniquePtr<FBattleEngine> ObedienceEngine;
	FStrictBattleRandom* ObedienceRandom = nullptr;
	if (!TestTrue(TEXT("Obedience-refusal cleanup engine is created"),
			TryMakeStrictEngine(ObedienceScenario, {},
				ObedienceEngine, ObedienceRandom))
		|| !TestTrue(TEXT("Obedience-refusal cleanup turn locks"),
			LockTurn(*ObedienceEngine, PlayerLeftValue,
				EBattleActionKind::Fight))
		|| !TestTrue(TEXT("Obedience-refusal target binding is seeded"),
			TrySeedR3LifecycleBoundRegistration(
				*ObedienceEngine, PlayerLeftValue, 7105)))
	{
		return false;
	}
	const FBattleResolution Obedience =
		ObedienceEngine->BeginNextLockedAction();
	bValid &= TestTrue(TEXT("Obedience refusal removes the bound registration"),
		Obedience.WasAccepted()
			&& HasEvent(Obedience, EBattleEventType::ObedienceRefused)
			&& !HasR3LifecycleTarget(
				FBattleC09BWildFlowEngineFixture::GetState(*ObedienceEngine),
				PlayerLeftValue)
			&& ObedienceRandom != nullptr
			&& ObedienceRandom->IsExact());

	FAtomicWildScenario SleepScenario = MakePreMoveScenario();
	SleepScenario.Format = EBattleFormat::Double;
	TUniquePtr<FBattleEngine> SleepEngine;
	FStrictBattleRandom* SleepRandom = nullptr;
	if (!TestTrue(TEXT("Pre-move cleanup engine is created"),
			TryMakeStrictEngine(SleepScenario, {}, SleepEngine, SleepRandom))
		|| !TestTrue(TEXT("Sleep is seeded"),
			TrySeedPreMoveMajorStatus(*SleepEngine, PlayerLeft,
				FBattleMajorStatusRules::GetSleepId(), 2))
		|| !TestTrue(TEXT("Sleep cleanup turn locks"),
			LockTurn(*SleepEngine, PlayerLeftValue, EBattleActionKind::Fight))
		|| !TestTrue(TEXT("Sleep target binding is seeded"),
			TrySeedR3LifecycleBoundRegistration(
				*SleepEngine, PlayerLeftValue, 7102))
		|| !TestTrue(TEXT("Sleeping action starts"),
			BeginExpectedWildAction(*SleepEngine, PlayerLeftValue,
				EBattleActionKind::Fight)))
	{
		return false;
	}
	bValid &= TestTrue(TEXT("The binding survives until the pre-move decision"),
		HasR3LifecycleTarget(
			FBattleC09BWildFlowEngineFixture::GetState(*SleepEngine),
			PlayerLeftValue));
	const FBattleResolution Sleep =
		SleepEngine->CommitCurrentMoveAfterPreMoveGates();
	bValid &= TestTrue(TEXT("Pre-move cancellation removes the bound registration"),
		Sleep.WasAccepted()
			&& !HasR3LifecycleTarget(
				FBattleC09BWildFlowEngineFixture::GetState(*SleepEngine),
				PlayerLeftValue)
			&& SleepRandom != nullptr
			&& SleepRandom->IsExact());

	FAtomicWildScenario TargetScenario = MakePreMoveScenario();
	TargetScenario.Format = EBattleFormat::Double;
	TUniquePtr<FBattleEngine> TargetEngine;
	FStrictBattleRandom* TargetRandom = nullptr;
	if (!TestTrue(TEXT("Target-failure cleanup engine is created"),
			TryMakeStrictEngine(TargetScenario, {}, TargetEngine, TargetRandom))
		|| !TestTrue(TEXT("Target-failure reaches target resolution"),
			TryPrepareTargetCheckpoint(*TargetEngine,
				MakeDefinitionId<FMoveId>(TargetProbeMoveName)))
		|| !TestTrue(TEXT("Target-failure binding is seeded"),
			TrySeedR3LifecycleBoundRegistration(
				*TargetEngine, PlayerLeftValue, 7103))
		|| !TestTrue(TEXT("Opponent Left is unavailable"),
			TryMarkTargetFainted(*TargetEngine,
				MakeNumericId<FBattlerId>(OpponentLeftValue)))
		|| !TestTrue(TEXT("Opponent Right is unavailable"),
			TryMarkTargetFainted(*TargetEngine,
				MakeNumericId<FBattlerId>(OpponentRightValue))))
	{
		return false;
	}
	const FBattleResolution NoTarget = TargetEngine->ResolveCurrentMoveTargets();
	bValid &= TestTrue(TEXT("Target-resolution cancellation removes the binding"),
		NoTarget.WasAccepted()
			&& !HasR3LifecycleTarget(
				FBattleC09BWildFlowEngineFixture::GetState(*TargetEngine),
				PlayerLeftValue)
			&& TargetRandom != nullptr
			&& TargetRandom->IsExact());

	FAtomicWildScenario CompletionScenario = MakePreMoveScenario();
	CompletionScenario.Format = EBattleFormat::Double;
	TUniquePtr<FBattleEngine> CompletionEngine;
	FStrictBattleRandom* CompletionRandom = nullptr;
	if (!TestTrue(TEXT("Completion cleanup engine is created"),
			TryMakeStrictEngine(CompletionScenario, {},
				CompletionEngine, CompletionRandom))
		|| !TestTrue(TEXT("Completion reaches target resolution"),
			TryPrepareTargetCheckpoint(*CompletionEngine,
				MakeDefinitionId<FMoveId>(TargetProbeMoveName)))
		|| !TestTrue(TEXT("Completion target binding is seeded"),
			TrySeedR3LifecycleBoundRegistration(
				*CompletionEngine, PlayerLeftValue, 7104))
		|| !TestTrue(TEXT("Completion resolves targets"),
			CompletionEngine->ResolveCurrentMoveTargets().WasAccepted()))
	{
		return false;
	}
	const FBattleResolution Completion =
		CompletionEngine->ExecuteCurrentMoveEffects();
	bValid &= TestTrue(TEXT("Normal effect completion removes the binding"),
		Completion.WasAccepted()
			&& !HasR3LifecycleTarget(
				FBattleC09BWildFlowEngineFixture::GetState(*CompletionEngine),
				PlayerLeftValue)
			&& CompletionRandom != nullptr
			&& CompletionRandom->IsExact());
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC10R3OccupantAndTurnCleanupTest,
	"PokemonSolarus.Battle.C05B.C10ActionModifiers.Lifecycle.FaintSwitchCaptureWildAndTurnCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC10R3OccupantAndTurnCleanupTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	bool bValid = true;
	const FMoveId DamageMoveId =
		MakeDefinitionId<FMoveId>(RandomExecutionProbeMoveName);
	FAtomicWildScenario FaintScenario = MakePreMoveScenario(DamageMoveId);
	FaintScenario.Format = EBattleFormat::Double;
	FaintScenario.TargetCurrentHP = 1;
	TUniquePtr<FBattleEngine> FaintEngine;
	FStrictBattleRandom* FaintRandom = nullptr;
	const TArray<FBattleExpectedRandomDraw> DamageDraws =
	{
		{0, 99, 0, FBattleEffectExecutor::GetAccuracyRulePurpose()},
		{0, 15, 0, FBattleEffectExecutor::GetDamageRandomRulePurpose()}
	};
	if (!TestTrue(TEXT("Faint cleanup engine is created"),
			TryMakeStrictEngine(FaintScenario, DamageDraws,
				FaintEngine, FaintRandom))
		|| !TestTrue(TEXT("Lethal move reaches effects"),
			TryPrepareR3LifecycleEffectsCheckpoint(*FaintEngine, DamageMoveId))
		|| !TestTrue(TEXT("Faint target binding is seeded"),
			TrySeedR3LifecycleBoundRegistration(
				*FaintEngine, OpponentLeftValue, 7201))
		|| !TestTrue(TEXT("Faint-unrelated ally binding is seeded"),
			TrySeedR3LifecycleBoundRegistration(
				*FaintEngine, PlayerRightValue, 7200)))
	{
		return false;
	}
	const FBattleResolution Faint = FaintEngine->ExecuteCurrentMoveEffects();
	bValid &= TestTrue(TEXT("Faint removes the exact target binding"),
		Faint.WasAccepted()
			&& !HasR3LifecycleTarget(
				FBattleC09BWildFlowEngineFixture::GetState(*FaintEngine),
				OpponentLeftValue)
			&& HasR3LifecycleTarget(
				FBattleC09BWildFlowEngineFixture::GetState(*FaintEngine),
				PlayerRightValue)
			&& FaintRandom != nullptr
			&& FaintRandom->IsExact());

	const FBattleMoveDefinition TerminalSpread = MakeR3LifecycleDamageMove(
		R3LifecycleSpreadMoveName,
		EBattleTargetClass::FixedOpponentSpreadSet,
		false);
	TUniquePtr<FBattleEngine> TerminalEngine;
	FStrictBattleRandom* TerminalRandom = nullptr;
	const TArray<FBattleExpectedRandomDraw> TerminalDraws =
	{
		{0, 15, 0, FBattleEffectExecutor::GetDamageRandomRulePurpose()},
		{0, 15, 0, FBattleEffectExecutor::GetDamageRandomRulePurpose()}
	};
	if (!TestTrue(TEXT("Terminal-faint cleanup engine is created"),
			TryMakeR3LifecycleDamageEngine(TerminalSpread, TerminalDraws,
				TerminalEngine, TerminalRandom))
		|| !TestTrue(TEXT("Terminal-faint spread reaches effects"),
			TryPrepareR3LifecycleDamage(*TerminalEngine, TerminalSpread.Id))
		|| !TestTrue(TEXT("A living ally pending-action binding is seeded"),
			TrySeedR3LifecycleBoundRegistration(
				*TerminalEngine, PlayerRightValue, 7205)))
	{
		return false;
	}
	FBattleEngineState& TerminalState =
		FBattleC09BWildFlowEngineFixture::GetMutableState(*TerminalEngine);
	FBattleBattlerState* TerminalLeft = TerminalState.FindMutableBattler(
		MakeNumericId<FBattlerId>(OpponentLeftValue));
	FBattleBattlerState* TerminalRight = TerminalState.FindMutableBattler(
		MakeNumericId<FBattlerId>(OpponentRightValue));
	if (TerminalLeft == nullptr || TerminalRight == nullptr)
	{
		return false;
	}
	TerminalLeft->CurrentHP = 1;
	TerminalRight->CurrentHP = 1;
	const FBattleResolution TerminalFaint =
		TerminalEngine->ExecuteCurrentMoveEffects();
	const FBattleEngineState& TerminalAfter =
		FBattleC09BWildFlowEngineFixture::GetState(*TerminalEngine);
	const FBattleBattlerState* LivingPendingTarget = TerminalAfter.FindBattler(
		MakeNumericId<FBattlerId>(PlayerRightValue));
	bValid &= TestTrue(
		TEXT("A terminal faint clears living pending-action bindings"),
		TerminalFaint.WasAccepted()
			&& TerminalAfter.Phase == EBattlePhase::Terminal
			&& TerminalAfter.Outcome == EBattleOutcome::Victory
			&& LivingPendingTarget != nullptr
			&& LivingPendingTarget->CurrentHP > 0
			&& !LivingPendingTarget->bFainted
			&& !LivingPendingTarget->bRemoved
			&& TerminalAfter.AllyActionPowerModifierRegistrations.IsEmpty()
			&& TerminalRandom != nullptr
			&& TerminalRandom->IsExact());

	FAtomicWildScenario CaptureScenario = MakeAtomicCaptureScenario();
	CaptureScenario.Format = EBattleFormat::Double;
	TUniquePtr<FBattleEngine> CaptureEngine;
	if (!TestTrue(TEXT("Capture cleanup engine is created"),
			TryMakeSequenceEngine(CaptureScenario, {0, 0, 0, 0},
				CaptureEngine))
		|| !TestTrue(TEXT("Capture turn locks"),
			LockTurn(*CaptureEngine, PlayerLeftValue, EBattleActionKind::Bag))
		|| !TestTrue(TEXT("Capture action starts"),
			BeginExpectedWildAction(*CaptureEngine, PlayerLeftValue,
				EBattleActionKind::Bag))
		|| !TestTrue(TEXT("Capture target binding is seeded"),
			TrySeedR3LifecycleBoundRegistration(
				*CaptureEngine, OpponentLeftValue, 7202)))
	{
		return false;
	}
	const FBattleResolution Capture = CaptureEngine->ExecuteCurrentBagItem();
	bValid &= TestTrue(TEXT("Capture removes the exact target binding"),
		Capture.WasAccepted()
			&& !HasR3LifecycleTarget(
				FBattleC09BWildFlowEngineFixture::GetState(*CaptureEngine),
				OpponentLeftValue));

	const FMoveId ForcedMoveId =
		MakeDefinitionId<FMoveId>(ForcedEntryProbeMoveName);
	FAtomicWildScenario SwitchScenario = MakePreMoveScenario(ForcedMoveId);
	SwitchScenario.Format = EBattleFormat::Double;
	SwitchScenario.bTrainerEncounter = true;
	SwitchScenario.bOpponentSwitchReserve = true;
	TUniquePtr<FBattleEngine> SwitchEngine;
	FStrictBattleRandom* SwitchRandom = nullptr;
	if (!TestTrue(TEXT("Forced-switch cleanup engine is created"),
			TryMakeR3LifecycleOpponentReserveEngine(
				SwitchScenario,
				{{0, 0, 0,
					FBattleSwitchResolver::GetForcedSelectionRulePurpose()}},
				SwitchEngine,
				SwitchRandom))
		|| !TestTrue(TEXT("Forced-switch move reaches effects"),
			TryPrepareR3LifecycleEffectsCheckpoint(*SwitchEngine, ForcedMoveId))
		|| !TestTrue(TEXT("Forced-switch target binding is seeded"),
			TrySeedR3LifecycleBoundRegistration(
				*SwitchEngine, OpponentLeftValue, 7203))
		|| !TestTrue(TEXT("Forced-switch unrelated ally binding is seeded"),
			TrySeedR3LifecycleBoundRegistration(
				*SwitchEngine, OpponentRightValue, 7202)))
	{
		return false;
	}
	const FBattleResolution ForcedSwitch =
		SwitchEngine->ExecuteCurrentMoveEffects();
	const FBattleEngineState& SwitchedState =
		FBattleC09BWildFlowEngineFixture::GetState(*SwitchEngine);
	const FBattleActivePositionState* SwitchedActive =
		SwitchedState.FindActivePosition(MakeActiveSlotId(
			EBattleSide::Opponent, EBattlePosition::Left));
	bValid &= TestTrue(TEXT("Forced/shared switch removes the outgoing target binding"),
		ForcedSwitch.WasAccepted()
			&& SwitchedActive != nullptr
			&& SwitchedActive->BattlerId
				== MakeNumericId<FBattlerId>(OpponentReserveValue)
			&& !HasR3LifecycleTarget(SwitchedState, OpponentLeftValue)
			&& HasR3LifecycleTarget(SwitchedState, OpponentRightValue));

	const FBattleBattlerTarget Outgoing = {
		MakeActiveSlotId(EBattleSide::Opponent, EBattlePosition::Left),
		MakeNumericId<FBattlerId>(OpponentLeftValue)};
	const FBattleBattlerTarget Other = {
		MakeActiveSlotId(EBattleSide::Opponent, EBattlePosition::Right),
		MakeNumericId<FBattlerId>(OpponentRightValue)};
	TArray<FBattleAllyActionPowerModifierRegistration> OccupantFacts =
	{
		MakeR3LifecycleRegistration(50, 60, Outgoing),
		MakeR3LifecycleRegistration(51, 61, Other)
	};
	FBattleAllyActionPowerModifier::RemoveForOccupant(OccupantFacts, Outgoing);
	bValid &= TestTrue(
		TEXT("Voluntary-switch and wild-removal cleanup is exact-target, not source-wide"),
		OccupantFacts.Num() == 1 && OccupantFacts[0].Target == Other);

	if (!TestTrue(TEXT("The forced entrant accepts the new-entry exception"),
			TrySeedR3LifecycleNewEntryRegistration(
				*SwitchEngine, OpponentReserveValue, 7204))
		|| !TestTrue(TEXT("The remaining ordinary queue reaches end of turn"),
			TryDrainR3LifecycleFightTurn(*SwitchEngine)))
	{
		return false;
	}
	const FBattleEngineState& BeforeEndTurn =
		FBattleC09BWildFlowEngineFixture::GetState(*SwitchEngine);
	const FTurnId PriorTurnId = BeforeEndTurn.TurnId;
	bValid &= TestTrue(TEXT("The unused new-entry binding survives until end turn"),
		BeforeEndTurn.Phase == EBattlePhase::EndOfTurn
			&& HasR3LifecycleTarget(BeforeEndTurn, OpponentReserveValue));
	const FBattleResolution EndTurn = SwitchEngine->ResolveEndTurn();
	const FBattleEngineState& AfterEndTurn =
		FBattleC09BWildFlowEngineFixture::GetState(*SwitchEngine);
	bValid &= TestTrue(TEXT("End turn clears before the TurnId increments"),
		EndTurn.WasAccepted()
			&& AfterEndTurn.TurnId.GetValue() == PriorTurnId.GetValue() + 1
			&& AfterEndTurn.AllyActionPowerModifierRegistrations.IsEmpty());
	bValid &= TestTrue(TEXT("The switch/end-turn flow consumes only its pinned draw"),
		SwitchRandom != nullptr && SwitchRandom->IsExact());
	return bValid;
}

#endif
