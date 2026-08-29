#if WITH_DEV_AUTOMATION_TESTS

#include "Battle/BattleAllyActionPowerModifier.h"
#include "Battle/BattleReplay.h"
#include "BattleAtomicMoveCheckpointTestSupport.h"

#include <type_traits>
#include <utility>

namespace BattleAllyActionPowerModifierReplayTestsPrivate
{
	using namespace BattleAtomicCheckpointTestCommonPrivate;

	const TCHAR* const R3ReplayRegistrationMoveName =
		TEXT("Move.C10R3.RegisterAllyActionPowerModifier.Replay");

	template <typename T, typename = void>
	struct THasR3ReplayPublicRegistrationGetter : std::false_type
	{
	};

	template <typename T>
	struct THasR3ReplayPublicRegistrationGetter<T, std::void_t<decltype(
		std::declval<const T&>().GetAllyActionPowerModifierRegistrations())>>
		: std::true_type
	{
	};

	template <typename T, typename = void>
	struct THasR3ReplayPublicRegistrationField : std::false_type
	{
	};

	template <typename T>
	struct THasR3ReplayPublicRegistrationField<T, std::void_t<decltype(
		std::declval<const T&>().AllyActionPowerModifierRegistrations)>>
		: std::true_type
	{
	};

	static_assert(!THasR3ReplayPublicRegistrationGetter<FBattleSnapshot>::value);
	static_assert(!THasR3ReplayPublicRegistrationGetter<FBattleReplayRecord>::value);
	static_assert(!THasR3ReplayPublicRegistrationGetter<FBattleEngine>::value);
	static_assert(!THasR3ReplayPublicRegistrationField<FBattleSnapshot>::value);
	static_assert(!THasR3ReplayPublicRegistrationField<FBattleReplayInputs>::value);

	FMoveId GetR3ReplayRegistrationMoveId()
	{
		return MakeDefinitionId<FMoveId>(R3ReplayRegistrationMoveName);
	}

	FBattleBattlerTarget GetR3ReplaySourceTarget()
	{
		return {
			MakeActiveSlotId(EBattleSide::Opponent, EBattlePosition::Left),
			MakeNumericId<FBattlerId>(OpponentLeftValue)};
	}

	FBattleBattlerTarget GetR3ReplayAllyTarget()
	{
		return {
			MakeActiveSlotId(EBattleSide::Opponent, EBattlePosition::Right),
			MakeNumericId<FBattlerId>(OpponentRightValue)};
	}

	FBattleMoveDefinition MakeR3ReplayRegistrationMove()
	{
		FBattleMoveDefinition Move;
		Move.Id = GetR3ReplayRegistrationMoveId();
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
		Effect.Kind = EBattleMoveEffectKind::RegisterAllyActionPowerModifier;
		Effect.Target = EBattleEffectTarget::ResolvedTarget;
		Effect.MagnitudeNumerator = 3;
		Effect.MagnitudeDenominator = 2;
		Move.Effects.Add(Effect);
		return Move;
	}

	bool TryMakeR3ReplayEngine(
		TUniquePtr<FBattleEngine>& OutEngine,
		FStrictBattleRandom*& OutRandom,
		const int32 SourceSpeed = 100,
		const int32 TargetSpeed = 90,
		const int32 RegistrationPriority = 2)
	{
		FAtomicWildScenario Scenario;
		Scenario.Format = EBattleFormat::Double;
		Scenario.OpponentLeftSpeed = SourceSpeed;
		Scenario.OpponentRightSpeed = TargetSpeed;

		FBattleDefinitionCatalogInput CatalogInput;
		CatalogInput.TypeChartEntries = MakeNeutralTypeChart();
		CatalogInput.Moves.Add(MakeProbeMove());
		CatalogInput.Moves.Add(MakeTargetProbeMove());
		FBattleMoveDefinition RegistrationMove =
			MakeR3ReplayRegistrationMove();
		RegistrationMove.Priority = RegistrationPriority;
		CatalogInput.Moves.Add(RegistrationMove);
		CatalogInput.Abilities.Add({FBattleAbilityRules::GetBlazeId()});
		CatalogInput.Abilities.Add({FBattleAbilityRules::GetIntimidateId()});
		CatalogInput.Abilities.Add({FBattleAbilityRules::GetMagicGuardId()});
		CatalogInput.SpeciesForms.Add(MakeSpecies(PlayerSpeciesName));
		CatalogInput.SpeciesForms.Add(MakeSpecies(WildSpeciesName));
		FBattleDefinitionCatalog Catalog;
		TArray<FBattleCatalogDiagnostic> Diagnostics;
		if (!FBattleDefinitionCatalog::TryCreate(
				CatalogInput,
				Catalog,
				Diagnostics))
		{
			return false;
		}

		FBattleSetupInput SetupInput = MakeSetupInput(Scenario);
		FBattlePartyEntrySetup* Source = SetupInput.PartyEntries.FindByPredicate(
			[](const FBattlePartyEntrySetup& Entry)
			{
				return Entry.BattlerId
					== MakeNumericId<FBattlerId>(OpponentLeftValue);
			});
		if (Source == nullptr)
		{
			return false;
		}
		Source->Moves.Add({2, GetR3ReplayRegistrationMoveId(), 5, 5});

		FBattleSetup Setup;
		EBattleSetupValidationError SetupError = EBattleSetupValidationError::None;
		if (!FBattleSetup::TryCreate(SetupInput, Setup, SetupError))
		{
			return false;
		}

		TUniquePtr<FStrictBattleRandom> Strict =
			MakeUnique<FStrictBattleRandom>(TArray<FBattleExpectedRandomDraw>());
		OutRandom = Strict.Get();
		TUniquePtr<IBattleRandom> Random = MoveTemp(Strict);
		FBattleRejection Rejection;
		return FBattleEngine::TryCreate(
			Setup,
			Catalog,
			MoveTemp(Random),
			OutEngine,
			Rejection);
	}

	bool TryLockR3ReplayTurn(FBattleEngine& Engine)
	{
		FBattleRejection Rejection;
		if (!Engine.TryBeginActionDecisionSequence(Rejection))
		{
			return false;
		}

		int32 Guard = 0;
		while (!Engine.GetPendingDecisionRequests().IsEmpty() && Guard++ < 4)
		{
			const TArray<FBattleDecisionRequest> Requests =
				Engine.GetPendingDecisionRequests();
			TArray<FBattleDecision> Decisions;
			for (const FBattleDecisionRequest& Request : Requests)
			{
				FBattleDecision Decision;
				if (Request.GetActingBattlerId()
					== MakeNumericId<FBattlerId>(OpponentLeftValue))
				{
					const FMoveId MoveId = GetR3ReplayRegistrationMoveId();
					const FActiveSlotId TargetSlot =
						GetR3ReplayAllyTarget().ActiveSlotId;
					const FBattleMoveTargetOption* Target =
						Request.GetLegalMoveTargets().FindByPredicate(
							[MoveId, TargetSlot](const FBattleMoveTargetOption& Option)
							{
								return Option.MoveId == MoveId
									&& Option.ActiveSlotId == TargetSlot;
							});
					if (Target == nullptr
						|| !FBattleDecision::TryCreateFight(
							Request.GetStateVersion(),
							Request.GetDecisionOwnerTrainerId(),
							Request.GetActingBattlerId(),
							MoveId,
							TargetSlot,
							Decision))
					{
						return false;
					}
				}
				else
				{
					Decision = MakeDecision(Request, EBattleActionKind::Fight);
				}
				Decisions.Add(MoveTemp(Decision));
			}
			if (!Engine.SubmitDecisionBatch(
					MakeBatch(Requests, MoveTemp(Decisions))).WasAccepted())
			{
				return false;
			}
		}
		return Guard < 4 && Engine.GetSnapshot().GetPhase() == EBattlePhase::Locked;
	}

	struct FR3ReplayExecutedMove
	{
		FBattlerId ActorId;
		FActionId ActionId;
		FBattleTargetResolutionResult Targets;
		FBattleResolution TargetResolution;
		FBattleResolution EffectResolution;
	};

	bool TrySeedR3ReplayPrivateRegistration(
		FBattleEngine& Engine,
		FActionId SourceActionId);

	bool TryExecuteR3ReplayCurrentMove(
		FBattleEngine& Engine,
		FR3ReplayExecutedMove& OutMove,
		const bool bSeedRegistrationBeforeEffects = false)
	{
		OutMove = FR3ReplayExecutedMove();
		if (!Engine.BeginNextLockedAction().WasAccepted())
		{
			return false;
		}
		const TOptional<FBattleLockedAction> Current =
			Engine.GetCurrentLockedAction();
		if (!Current.IsSet()
			|| Current->Decision.GetActionKind() != EBattleActionKind::Fight
			|| !Engine.CommitCurrentMoveAfterPreMoveGates().WasAccepted())
		{
			return false;
		}

		OutMove.ActorId = Current->Decision.GetActingBattlerId();
		OutMove.ActionId = Current->ActionId;
		OutMove.TargetResolution = Engine.ResolveCurrentMoveTargets();
		if (!OutMove.TargetResolution.WasAccepted())
		{
			return false;
		}
		const FBattleEngineState& State =
			FBattleC09BWildFlowEngineFixture::GetState(Engine);
		if (!State.LockedActions.IsValidIndex(State.CurrentLockedActionIndex)
			|| !State.LockedActions[State.CurrentLockedActionIndex]
				.TargetResolution.IsSet())
		{
			return false;
		}
		OutMove.Targets = State.LockedActions[State.CurrentLockedActionIndex]
			.TargetResolution.GetValue();
		if (bSeedRegistrationBeforeEffects
			&& !TrySeedR3ReplayPrivateRegistration(
				Engine, OutMove.ActionId))
		{
			return false;
		}
		OutMove.EffectResolution = Engine.ExecuteCurrentMoveEffects();
		return OutMove.EffectResolution.WasAccepted();
	}

	struct FR3ReplayFlowEvidence
	{
		FR3ReplayExecutedMove Registration;
		FR3ReplayExecutedMove TargetAction;
		FBattleAllyActionPowerModifierRegistration StoredRegistration;
		FBattleReplayRecord Replay;
		TArray<uint8> ReplayBytes;
		bool bRegistrationExpired = false;
		bool bRandomExact = false;
	};

	bool TryRunR3ReplayFlow(
		FR3ReplayFlowEvidence& OutEvidence,
		const bool bSeedRegistrationBeforeEffects = false)
	{
		OutEvidence = FR3ReplayFlowEvidence();
		TUniquePtr<FBattleEngine> Engine;
		FStrictBattleRandom* Random = nullptr;
		if (!TryMakeR3ReplayEngine(Engine, Random)
			|| !TryLockR3ReplayTurn(*Engine)
			|| !TryExecuteR3ReplayCurrentMove(
				*Engine,
				OutEvidence.Registration,
				bSeedRegistrationBeforeEffects)
			|| OutEvidence.Registration.ActorId
				!= GetR3ReplaySourceTarget().BattlerId)
		{
			return false;
		}

		const FBattleEngineState& RegisteredState =
			FBattleC09BWildFlowEngineFixture::GetState(*Engine);
		const int32 ExpectedRegistrationCount =
			bSeedRegistrationBeforeEffects ? 2 : 1;
		if (RegisteredState.AllyActionPowerModifierRegistrations.Num()
			!= ExpectedRegistrationCount)
		{
			return false;
		}
		OutEvidence.StoredRegistration =
			RegisteredState.AllyActionPowerModifierRegistrations.Last();

		if (!TryExecuteR3ReplayCurrentMove(*Engine, OutEvidence.TargetAction)
			|| OutEvidence.TargetAction.ActorId != GetR3ReplayAllyTarget().BattlerId)
		{
			return false;
		}
		OutEvidence.bRegistrationExpired =
			FBattleC09BWildFlowEngineFixture::GetState(*Engine)
				.AllyActionPowerModifierRegistrations.IsEmpty();

		OutEvidence.Replay = Engine->ExportReplayRecord();
		FBattleRejection Rejection;
		OutEvidence.bRandomExact = Random != nullptr && Random->IsExact();
		return FBattleReplaySerializer::TrySerializeCanonical(
			OutEvidence.Replay,
			OutEvidence.ReplayBytes,
			Rejection);
	}

	int32 CountR3ReplayRegistrationEvents(
		const FBattleReplayRecord& Replay,
		const FBattleEvent*& OutEvent)
	{
		OutEvent = nullptr;
		int32 Count = 0;
		for (const FBattleResolution& Resolution : Replay.GetResolutions())
		{
			for (const FBattleEvent& Event : Resolution.GetEvents())
			{
				if (Event.GetType()
					== EBattleEventType::ActionPowerModifierRegistered)
				{
					++Count;
					OutEvent = &Event;
				}
			}
		}
		return Count;
	}

	bool TryPrepareR3ReplayPrivacyProbe(
		TUniquePtr<FBattleEngine>& OutEngine,
		FStrictBattleRandom*& OutRandom,
		FActionId& OutSourceActionId)
	{
		OutSourceActionId = FActionId();
		if (!TryMakeR3ReplayEngine(OutEngine, OutRandom)
			|| !TryLockR3ReplayTurn(*OutEngine)
			|| !OutEngine->BeginNextLockedAction().WasAccepted())
		{
			return false;
		}
		const TOptional<FBattleLockedAction> Current =
			OutEngine->GetCurrentLockedAction();
		if (!Current.IsSet()
			|| Current->Decision.GetActingBattlerId()
				!= GetR3ReplaySourceTarget().BattlerId
			|| Current->Decision.GetMoveId() != GetR3ReplayRegistrationMoveId()
			|| !OutEngine->CommitCurrentMoveAfterPreMoveGates().WasAccepted()
			|| !OutEngine->ResolveCurrentMoveTargets().WasAccepted())
		{
			return false;
		}
		OutSourceActionId = Current->ActionId;
		return true;
	}

	bool TrySeedR3ReplayPrivateRegistration(
		FBattleEngine& Engine,
		const FActionId SourceActionId)
	{
		FBattleEngineState& State =
			FBattleC09BWildFlowEngineFixture::GetMutableState(Engine);
		int32 CountBefore = INDEX_NONE;
		int32 CountAfter = INDEX_NONE;
		return FBattleAllyActionPowerModifier::TryRegister(
			State.Format,
			State.TurnId,
			SourceActionId,
			GetR3ReplayRegistrationMoveId(),
			GetR3ReplaySourceTarget(),
			GetR3ReplayAllyTarget(),
			3,
			2,
			State.Battlers,
			State.ActivePositions,
			State.LockedActions,
			State.AllyActionPowerModifierRegistrations,
			CountBefore,
			CountAfter)
			== EBattleAllyActionPowerModifierRegistrationOutcome::Registered
			&& CountBefore == 0
			&& CountAfter == 1;
	}
}

using namespace BattleAllyActionPowerModifierReplayTestsPrivate;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC10R3EventOrderPrivacySchema6ReplayTest,
	"PokemonSolarus.Battle.C05B.C10ActionModifiers.Integration.EventOrderPrivacyAndSchema6Replay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC10R3EventOrderPrivacySchema6ReplayTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	FR3ReplayFlowEvidence First;
	FR3ReplayFlowEvidence Second;
	FR3ReplayFlowEvidence Stacked;
	if (!TestTrue(TEXT("The first complete ally-modifier replay flow executes"),
			TryRunR3ReplayFlow(First))
		|| !TestTrue(TEXT("The second complete ally-modifier replay flow executes"),
			TryRunR3ReplayFlow(Second))
		|| !TestTrue(TEXT("A real stacked registration flow executes"),
			TryRunR3ReplayFlow(Stacked, true)))
	{
		return false;
	}

	const TConstArrayView<FBattleEvent> TargetEvents =
		First.Registration.TargetResolution.GetEvents();
	const TConstArrayView<FBattleEvent> EffectEvents =
		First.Registration.EffectResolution.GetEvents();
	const int32 RegisteredIndex = EffectEvents.IndexOfByPredicate(
		[](const FBattleEvent& Event)
		{
			return Event.GetType()
				== EBattleEventType::ActionPowerModifierRegistered;
		});
	const int32 RegistrationCount = EffectEvents.FilterByPredicate(
		[](const FBattleEvent& Event)
		{
			return Event.GetType()
				== EBattleEventType::ActionPowerModifierRegistered;
		}).Num();
	const int32 CompletedIndex = EffectEvents.IndexOfByPredicate(
		[](const FBattleEvent& Event)
		{
			return Event.GetType() == EBattleEventType::ActionCompleted;
		});

	bool bValid = TestTrue(
		TEXT("Registration publishes targets, one typed registration, then completion"),
		TargetEvents.Num() == 1
			&& TargetEvents[0].GetType() == EBattleEventType::TargetsResolved
			&& RegisteredIndex != INDEX_NONE
			&& RegistrationCount == 1
			&& CompletedIndex > RegisteredIndex
			&& TargetEvents[0].GetEventOrdinal()
				< EffectEvents[RegisteredIndex].GetEventOrdinal());

	if (RegisteredIndex != INDEX_NONE)
	{
		const FBattleEvent& Event = EffectEvents[RegisteredIndex];
		const FBattleEventSource& Source = Event.GetSource();
		const TConstArrayView<FBattleEventTarget> Targets = Event.GetTargets();
		bValid &= TestTrue(TEXT("The registration event preserves exact source identity"),
			Event.GetActionId() == First.Registration.ActionId
				&& Event.GetCause() == EBattleEventCause::Move
				&& Event.GetCauseActionKind() == EBattleActionKind::Fight
				&& Source.TrainerId
					== MakeNumericId<FTrainerId>(OpponentTrainerValue)
				&& Source.BattlerId == GetR3ReplaySourceTarget().BattlerId
				&& Source.ActiveSlotId == GetR3ReplaySourceTarget().ActiveSlotId
				&& Source.DefinitionId
					== GetR3ReplayRegistrationMoveId().GetDefinitionId());
		bValid &= TestTrue(TEXT("The registration event preserves the exact ally and binding counts"),
			Targets.Num() == 1
				&& Targets[0].TrainerId
					== MakeNumericId<FTrainerId>(OpponentTrainerValue)
				&& Targets[0].BattlerId == GetR3ReplayAllyTarget().BattlerId
				&& Targets[0].ActiveSlotId == GetR3ReplayAllyTarget().ActiveSlotId
				&& !Targets[0].bHasSide
				&& !Targets[0].bField
				&& Event.GetNumericBefore() == TOptional<int64>(0)
				&& Event.GetNumericAfter() == TOptional<int64>(1)
				&& Event.GetNumericDelta() == TOptional<int64>(1)
				&& !Event.GetHitIndex().IsSet()
				&& !Event.GetHitCount().IsSet()
				&& Event.GetVisibility().Level == EBattleVisibilityLevel::Public);
	}

	bValid &= TestTrue(TEXT("The stored private fact binds the exact later ally action"),
		First.StoredRegistration.SourceActionId == First.Registration.ActionId
			&& First.StoredRegistration.SourceMoveId
				== GetR3ReplayRegistrationMoveId()
			&& First.StoredRegistration.TargetActionId
				== First.TargetAction.ActionId
			&& First.StoredRegistration.Target == GetR3ReplayAllyTarget()
			&& First.StoredRegistration.MagnitudeNumerator == 3
			&& First.StoredRegistration.MagnitudeDenominator == 2);
	bValid &= TestTrue(TEXT("The private binding expires without publishing a consumption event"),
		First.bRegistrationExpired);

	const FBattleEvent* FirstReplayEvent = nullptr;
	const FBattleEvent* SecondReplayEvent = nullptr;
	const int32 FirstReplayEventCount =
		CountR3ReplayRegistrationEvents(First.Replay, FirstReplayEvent);
	const int32 SecondReplayEventCount =
		CountR3ReplayRegistrationEvents(Second.Replay, SecondReplayEvent);
	bValid &= TestTrue(TEXT("Schema 6 replay retains exactly one typed registration event"),
		FBattleReplayRecord::CurrentSchemaVersion == 6
			&& First.Replay.GetSchemaVersion() == 6
			&& Second.Replay.GetSchemaVersion() == 6
			&& FirstReplayEventCount == 1
			&& SecondReplayEventCount == 1
			&& FirstReplayEvent != nullptr
			&& SecondReplayEvent != nullptr);
	if (FirstReplayEvent != nullptr && SecondReplayEvent != nullptr)
	{
		bValid &= TestTrue(TEXT("Replay preserves the registration event fields exactly"),
			FirstReplayEvent->GetEventOrdinal()
				== EffectEvents[RegisteredIndex].GetEventOrdinal()
				&& FirstReplayEvent->GetActionId() == First.Registration.ActionId
				&& FirstReplayEvent->GetSource().DefinitionId
					== GetR3ReplayRegistrationMoveId().GetDefinitionId()
				&& FirstReplayEvent->GetTargets().Num() == 1
				&& FirstReplayEvent->GetTargets()[0].BattlerId
					== GetR3ReplayAllyTarget().BattlerId
				&& FirstReplayEvent->GetNumericBefore() == TOptional<int64>(0)
				&& FirstReplayEvent->GetNumericAfter() == TOptional<int64>(1)
				&& FirstReplayEvent->GetNumericDelta() == TOptional<int64>(1));
	}
	bValid &= TestTrue(TEXT("Identical flows consume no RNG and serialize canonically"),
		First.bRandomExact
			&& Second.bRandomExact
			&& !First.ReplayBytes.IsEmpty()
			&& First.ReplayBytes == Second.ReplayBytes);
	const FBattleEvent* StackedReplayEvent = nullptr;
	bValid &= TestTrue(TEXT("A real second registration publishes the 1 to 2 count"),
		CountR3ReplayRegistrationEvents(
			Stacked.Replay, StackedReplayEvent) == 1
			&& StackedReplayEvent != nullptr
			&& StackedReplayEvent->GetNumericBefore() == TOptional<int64>(1)
			&& StackedReplayEvent->GetNumericAfter() == TOptional<int64>(2)
			&& StackedReplayEvent->GetNumericDelta() == TOptional<int64>(1)
			&& Stacked.bRegistrationExpired
			&& Stacked.bRandomExact);

	TUniquePtr<FBattleEngine> PrivacyEngine;
	FStrictBattleRandom* PrivacyRandom = nullptr;
	FActionId PrivacySourceActionId;
	if (!TestTrue(TEXT("The private-state projection probe reaches an eligible checkpoint"),
			TryPrepareR3ReplayPrivacyProbe(
				PrivacyEngine,
				PrivacyRandom,
				PrivacySourceActionId)))
	{
		return false;
	}
	const FBattleSnapshot BeforeSnapshot = PrivacyEngine->GetSnapshot();
	TArray<uint8> BeforeBytes;
	TArray<uint8> AfterBytes;
	FBattleRejection Rejection;
	bValid &= TestTrue(TEXT("The baseline public replay projection serializes"),
		FBattleReplaySerializer::TrySerializeCanonical(
			PrivacyEngine->ExportReplayRecord(),
			BeforeBytes,
			Rejection));
	bValid &= TestTrue(TEXT("One valid registration can be added only to private state"),
		TrySeedR3ReplayPrivateRegistration(
			*PrivacyEngine,
			PrivacySourceActionId));
	const FBattleSnapshot AfterSnapshot = PrivacyEngine->GetSnapshot();
	bValid &= TestTrue(TEXT("The public replay projection after private mutation serializes"),
		FBattleReplaySerializer::TrySerializeCanonical(
			PrivacyEngine->ExportReplayRecord(),
			AfterBytes,
			Rejection));
	bValid &= TestTrue(TEXT("Private registration state changes no public snapshot or replay bytes"),
		BeforeSnapshot.GetStateVersion() == AfterSnapshot.GetStateVersion()
			&& BeforeSnapshot.GetTurnId() == AfterSnapshot.GetTurnId()
			&& BeforeSnapshot.GetPhase() == AfterSnapshot.GetPhase()
			&& BeforeSnapshot.GetOutcome() == AfterSnapshot.GetOutcome()
			&& BeforeSnapshot.GetPartyEntries().Num()
				== AfterSnapshot.GetPartyEntries().Num()
			&& BeforeSnapshot.GetActiveAssignments().Num()
				== AfterSnapshot.GetActiveAssignments().Num()
			&& BeforeBytes == AfterBytes);
	bValid &= TestTrue(TEXT("The private-state projection probe consumes no RNG"),
		PrivacyRandom != nullptr && PrivacyRandom->IsExact());

	TUniquePtr<FBattleEngine> AlreadyActedEngine;
	FStrictBattleRandom* AlreadyActedRandom = nullptr;
	FR3ReplayExecutedMove EarlierTargetAction;
	FR3ReplayExecutedMove RejectedRegistration;
	if (!TestTrue(TEXT("The real already-acted rejection engine is created"),
			TryMakeR3ReplayEngine(
				AlreadyActedEngine, AlreadyActedRandom, 200, 210, -1))
		|| !TestTrue(TEXT("The real already-acted rejection turn locks"),
			TryLockR3ReplayTurn(*AlreadyActedEngine))
		|| !TestTrue(TEXT("The target ally acts before the registering source"),
			TryExecuteR3ReplayCurrentMove(
				*AlreadyActedEngine, EarlierTargetAction)
				&& EarlierTargetAction.ActorId
					== GetR3ReplayAllyTarget().BattlerId))
	{
		return false;
	}
	bool bRegisteringActionResolved = false;
	for (int32 Guard = 0; Guard < 3 && !bRegisteringActionResolved; ++Guard)
	{
		FR3ReplayExecutedMove Candidate;
		if (!TryExecuteR3ReplayCurrentMove(*AlreadyActedEngine, Candidate))
		{
			break;
		}
		if (Candidate.ActorId == GetR3ReplaySourceTarget().BattlerId)
		{
			RejectedRegistration = MoveTemp(Candidate);
			bRegisteringActionResolved = true;
		}
	}
	if (!TestTrue(TEXT("The later registering action resolves normally"),
		bRegisteringActionResolved))
	{
		return false;
	}
	const FBattleEngineState& AlreadyActedState =
		FBattleC09BWildFlowEngineFixture::GetState(*AlreadyActedEngine);
	bValid &= TestTrue(
		TEXT("Already-acted eligibility fails without state, event, or RNG mutation"),
		HasEvent(RejectedRegistration.EffectResolution,
			EBattleEventType::EffectFailed)
			&& !HasEvent(RejectedRegistration.EffectResolution,
				EBattleEventType::ActionPowerModifierRegistered)
			&& AlreadyActedState.AllyActionPowerModifierRegistrations.IsEmpty()
			&& AlreadyActedRandom != nullptr
			&& AlreadyActedRandom->IsExact());
	return bValid;
}

#endif
