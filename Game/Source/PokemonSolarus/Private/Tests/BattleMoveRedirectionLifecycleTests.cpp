#if WITH_DEV_AUTOMATION_TESTS

#include "Battle/BattleMoveRedirection.h"
#include "Battle/BattleReplay.h"
#include "Battle/BattleEngineSwitchPipeline.h"
#include "BattleAtomicMoveCheckpointTestSupport.h"
#include "BattleAtomicSwitchTestSupport.h"

#include <type_traits>
#include <utility>

namespace BattleMoveRedirectionLifecycleTestsPrivate
{
	using namespace BattleAtomicCheckpointTestCommonPrivate;
	using namespace BattleAtomicMoveCheckpointTestSupportPrivate;
	using namespace BattleAtomicSwitchTestSupportPrivate;
	using namespace BattleEngineSwitchPipelinePrivate;

	const TCHAR* const RegistrationMoveName =
		TEXT("Move.C10R2.RegisterTargetRedirection.Lifecycle");

	template <typename T, typename = void>
	struct THasPublicMoveRedirectionGetter : std::false_type
	{
	};

	template <typename T>
	struct THasPublicMoveRedirectionGetter<T, std::void_t<decltype(
		std::declval<const T&>().GetMoveRedirectionRegistrations())>> : std::true_type
	{
	};

	template <typename T, typename = void>
	struct THasPublicMoveRedirectionField : std::false_type
	{
	};

	template <typename T>
	struct THasPublicMoveRedirectionField<T, std::void_t<decltype(
		std::declval<const T&>().MoveRedirectionRegistrations)>> : std::true_type
	{
	};

	static_assert(!THasPublicMoveRedirectionGetter<FBattleSnapshot>::value);
	static_assert(!THasPublicMoveRedirectionField<FBattleSnapshot>::value);

	FBattleMoveDefinition MakeRegistrationMove()
	{
		FBattleMoveDefinition Move;
		Move.Id = MakeDefinitionId<FMoveId>(RegistrationMoveName);
		Move.Type = EPokemonType::Normal;
		Move.Category = EBattleMoveCategory::Status;
		Move.bAlwaysHits = true;
		Move.bUsesPP = true;
		Move.BasePP = 5;
		Move.bAllowsPPBoosts = true;
		Move.Priority = 2;
		Move.TargetClass = EBattleTargetClass::Self;
		FBattleMoveEffectDescriptor Effect;
		Effect.Kind = EBattleMoveEffectKind::RegisterTargetRedirection;
		Effect.Target = EBattleEffectTarget::User;
		Move.Effects.Add(Effect);
		return Move;
	}

	bool TrySeedRegistration(
		FBattleEngine& Engine,
		const uint64 BattlerValue,
		const uint64 ActionValue)
	{
		FBattleEngineState& State =
			FBattleC09BWildFlowEngineFixture::GetMutableState(Engine);
		const FBattlerId BattlerId = MakeNumericId<FBattlerId>(BattlerValue);
		const FBattleActivePositionState* Active = State.ActivePositions.FindByPredicate(
			[BattlerId](const FBattleActivePositionState& Candidate)
			{
				return Candidate.BattlerId == BattlerId;
			});
		return Active != nullptr
			&& FBattleMoveRedirection::TryRegister(
				State.Format,
				State.TurnId,
				MakeNumericId<FActionId>(ActionValue),
				{Active->ActiveSlotId, BattlerId},
				State.Battlers,
				State.ActivePositions,
				State.MoveRedirectionRegistrations)
				== EBattleMoveRedirectionRegistrationOutcome::Registered;
	}

	bool HasRegistrationFor(const FBattleEngineState& State, const uint64 BattlerValue)
	{
		const FBattlerId BattlerId = MakeNumericId<FBattlerId>(BattlerValue);
		return State.MoveRedirectionRegistrations.ContainsByPredicate(
			[BattlerId](const FBattleMoveRedirectionRegistration& Registration)
			{
				return Registration.Redirector.BattlerId == BattlerId;
			});
	}

	bool TryPrepareEffectsCheckpoint(FBattleEngine& Engine, const FMoveId MoveId)
	{
		return TryPrepareTargetCheckpoint(Engine, MoveId)
			&& Engine.ResolveCurrentMoveTargets().WasAccepted();
	}

	bool TryMakeDoubleOpponentReserveStrictEngine(
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

	bool TryMakeRegistrationEngine(
		const EBattleFormat Format,
		TUniquePtr<FBattleEngine>& OutEngine,
		FStrictBattleRandom*& OutRandom)
	{
		FAtomicWildScenario Scenario;
		Scenario.Format = Format;
		FBattleDefinitionCatalogInput CatalogInput;
		CatalogInput.TypeChartEntries = MakeNeutralTypeChart();
		CatalogInput.Moves.Add(MakeProbeMove());
		CatalogInput.Moves.Add(MakeTargetProbeMove());
		CatalogInput.Moves.Add(MakeRegistrationMove());
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
		FBattlePartyEntrySetup* Redirector = SetupInput.PartyEntries.FindByPredicate(
			[](const FBattlePartyEntrySetup& Entry)
			{
				return Entry.BattlerId
					== MakeNumericId<FBattlerId>(OpponentLeftValue);
			});
		if (Redirector == nullptr)
		{
			return false;
		}
		Redirector->Moves.Add({
			2,
			MakeDefinitionId<FMoveId>(RegistrationMoveName),
			5,
			5});
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

	bool TryLockRegistrationTurn(FBattleEngine& Engine)
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
					Decision = MakeDecision(
						Request,
						EBattleActionKind::Fight,
						MakeDefinitionId<FMoveId>(RegistrationMoveName));
				}
				else if (Request.GetActingBattlerId()
					== MakeNumericId<FBattlerId>(PlayerLeftValue))
				{
					const FMoveId TargetMoveId =
						MakeDefinitionId<FMoveId>(TargetProbeMoveName);
					const FBattleMoveTargetOption* Target =
						Request.GetLegalMoveTargets().FindByPredicate(
							[TargetMoveId](const FBattleMoveTargetOption& Option)
							{
								return Option.MoveId == TargetMoveId
									&& Option.ActiveSlotId == MakeActiveSlotId(
										EBattleSide::Opponent,
										EBattlePosition::Right);
							});
					if (Target == nullptr)
					{
						Target = Request.GetLegalMoveTargets().FindByPredicate(
							[TargetMoveId](const FBattleMoveTargetOption& Option)
							{
								return Option.MoveId == TargetMoveId;
							});
					}
					if (Target == nullptr)
					{
						return false;
					}
					if (!FBattleDecision::TryCreateFight(
							Request.GetStateVersion(),
							Request.GetDecisionOwnerTrainerId(),
							Request.GetActingBattlerId(),
							TargetMoveId,
							Target->ActiveSlotId,
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

	struct FExecutedMove
	{
		FBattlerId ActorId;
		FActionId ActionId;
		FBattleTargetResolutionResult Targets;
		FBattleResolution TargetResolution;
		FBattleResolution EffectResolution;
	};

	bool TryExecuteCurrentMove(FBattleEngine& Engine, FExecutedMove& OutMove)
	{
		OutMove = FExecutedMove();
		if (!Engine.BeginNextLockedAction().WasAccepted())
		{
			return false;
		}
		const TOptional<FBattleLockedAction> Current = Engine.GetCurrentLockedAction();
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
		OutMove.EffectResolution = Engine.ExecuteCurrentMoveEffects();
		return OutMove.EffectResolution.WasAccepted();
	}

	struct FRedirectionFlowEvidence
	{
		FExecutedMove Registration;
		FExecutedMove Redirected;
		FBattleMoveRedirectionRegistration StoredRegistration;
		TArray<uint8> ReplayBytes;
		uint32 ReplaySchema = 0;
		bool bRandomExact = false;
	};

	bool TryRunRedirectionFlow(FRedirectionFlowEvidence& OutEvidence)
	{
		OutEvidence = FRedirectionFlowEvidence();
		TUniquePtr<FBattleEngine> Engine;
		FStrictBattleRandom* Random = nullptr;
		FExecutedMove Intervening;
		if (!TryMakeRegistrationEngine(EBattleFormat::Double, Engine, Random)
			|| !TryLockRegistrationTurn(*Engine)
			|| !TryExecuteCurrentMove(*Engine, OutEvidence.Registration))
		{
			return false;
		}
		const FBattleEngineState& RegisteredState =
			FBattleC09BWildFlowEngineFixture::GetState(*Engine);
		if (RegisteredState.MoveRedirectionRegistrations.Num() != 1)
		{
			return false;
		}
		OutEvidence.StoredRegistration =
			RegisteredState.MoveRedirectionRegistrations[0];
		if (!TryExecuteCurrentMove(*Engine, Intervening)
			|| !TryExecuteCurrentMove(*Engine, OutEvidence.Redirected))
		{
			return false;
		}
		const FBattleReplayRecord Replay = Engine->ExportReplayRecord();
		FBattleRejection Rejection;
		OutEvidence.ReplaySchema = Replay.GetSchemaVersion();
		OutEvidence.bRandomExact = Random != nullptr && Random->IsExact();
		return FBattleReplaySerializer::TrySerializeCanonical(
			Replay,
			OutEvidence.ReplayBytes,
			Rejection);
	}

	struct FRegistrationFormatEvidence
	{
		int32 RegistrationCount = 0;
		bool bRegisteredEvent = false;
		bool bFailedEvent = false;
		bool bRandomExact = false;
	};

	bool TryRunRegistrationFormat(
		const EBattleFormat Format,
		FRegistrationFormatEvidence& OutEvidence)
	{
		OutEvidence = FRegistrationFormatEvidence();
		TUniquePtr<FBattleEngine> Engine;
		FStrictBattleRandom* Random = nullptr;
		FExecutedMove Executed;
		if (!TryMakeRegistrationEngine(Format, Engine, Random)
			|| !TryLockRegistrationTurn(*Engine)
			|| !TryExecuteCurrentMove(*Engine, Executed))
		{
			return false;
		}
		const FBattleEngineState& State =
			FBattleC09BWildFlowEngineFixture::GetState(*Engine);
		OutEvidence.RegistrationCount = State.MoveRedirectionRegistrations.Num();
		OutEvidence.bRegisteredEvent = Executed.EffectResolution.GetEvents().ContainsByPredicate(
			[](const FBattleEvent& Event)
			{
				return Event.GetType() == EBattleEventType::TargetRedirectionRegistered;
			});
		OutEvidence.bFailedEvent = Executed.EffectResolution.GetEvents().ContainsByPredicate(
			[](const FBattleEvent& Event)
			{
				return Event.GetType() == EBattleEventType::EffectFailed;
			});
		OutEvidence.bRandomExact = Random != nullptr && Random->IsExact();
		return true;
	}

	bool TryLockDoubleVoluntarySwitch(FBattleEngine& Engine)
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
					== MakeNumericId<FBattlerId>(PlayerLeftValue))
				{
					if (!FBattleDecision::TryCreateSwitch(
							Request.GetStateVersion(),
							EBattleDecisionRequestKind::Action,
							Request.GetDecisionOwnerTrainerId(),
							Request.GetActingBattlerId(),
							MakePartySlotId(2),
							MakeActiveSlotId(
								EBattleSide::Player,
								EBattlePosition::Left),
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

	bool TryPrepareDoublePivotSwitch(
		FBattleEngine& Engine,
		FBattleDecisionRequest& OutRequest)
	{
		const FMoveId PivotMoveId = MakeDefinitionId<FMoveId>(PivotProbeMoveName);
		if (!LockTurn(Engine, PlayerLeftValue, EBattleActionKind::Fight, PivotMoveId))
		{
			return false;
		}
		int32 Guard = 0;
		while (Guard++ < 4)
		{
			if (!Engine.BeginNextLockedAction().WasAccepted())
			{
				return false;
			}
			const TOptional<FBattleLockedAction> Current = Engine.GetCurrentLockedAction();
			if (!Current.IsSet()
				|| Current->Decision.GetActionKind() != EBattleActionKind::Fight)
			{
				return false;
			}
			if (Current->Decision.GetActingBattlerId()
					== MakeNumericId<FBattlerId>(PlayerLeftValue)
				&& Current->Decision.GetMoveId() == PivotMoveId)
			{
				break;
			}
			if (!Engine.CommitCurrentMoveAfterPreMoveGates().WasAccepted()
				|| !Engine.ResolveCurrentMoveTargets().WasAccepted()
				|| !Engine.ExecuteCurrentMoveEffects().WasAccepted())
			{
				return false;
			}
		}
		if (Guard > 4
			|| !Engine.CommitCurrentMoveAfterPreMoveGates().WasAccepted()
			|| !Engine.ResolveCurrentMoveTargets().WasAccepted()
			|| !Engine.ExecuteCurrentMoveEffects().WasAccepted())
		{
			return false;
		}
		const TArray<FBattleDecisionRequest> Requests =
			Engine.GetPendingDecisionRequests();
		if (Requests.Num() != 1
			|| Requests[0].GetRequestKind()
				!= EBattleDecisionRequestKind::PivotSwitch)
		{
			return false;
		}
		OutRequest = Requests[0];
		return true;
	}

	bool RunSharedSwitchCleanupCase(
		FAutomationTestBase& Test,
		const TCHAR* Label,
		const EBattleSwitchKind Kind)
	{
		FAtomicWildScenario Scenario = MakeAtomicVoluntarySwitchScenario();
		Scenario.Format = EBattleFormat::Double;
		TUniquePtr<FBattleEngine> Engine;
		if (!Test.TestTrue(
				FString::Printf(TEXT("%s cleanup engine is created"), Label),
				TryMakeSequenceEngine(Scenario, {}, Engine))
			|| !Test.TestTrue(
				FString::Printf(TEXT("%s outgoing registration is seeded"), Label),
				TrySeedRegistration(*Engine, PlayerLeftValue, 7101))
			|| !Test.TestTrue(
				FString::Printf(TEXT("%s unrelated registration is seeded"), Label),
				TrySeedRegistration(*Engine, OpponentLeftValue, 7102)))
		{
			return false;
		}
		FBattleEngineState& State =
			FBattleC09BWildFlowEngineFixture::GetMutableState(*Engine);
		FBattleSwitchLegalityResult Legality;
		FBattleSwitchSelectionSpec Selection;
		Selection.RequestedPartySlotId = MakePartySlotId(2);
		FBattleSwitchResolution Resolution;
		FSequenceBattleRandom NoRandom({});
		FBattleEventTarget Outgoing;
		FBattleEventTarget Incoming;
		const bool bApplied = TryBuildSwitchLegality(
				State,
				Kind,
				MakeNumericId<FTrainerId>(PlayerTrainerValue),
				MakeNumericId<FBattlerId>(PlayerLeftValue),
				MakeActiveSlotId(EBattleSide::Player, EBattlePosition::Left),
				TConstArrayView<FPartySlotId>(),
				Legality)
			&& FBattleSwitchResolver::TryResolve(
				Legality,
				Selection,
				NoRandom,
				Resolution)
			&& TryApplySwitchSelection(
				State,
				MakeNumericId<FTrainerId>(PlayerTrainerValue),
				MakeNumericId<FBattlerId>(PlayerLeftValue),
				MakeActiveSlotId(EBattleSide::Player, EBattlePosition::Left),
				Resolution,
				Outgoing,
				Incoming);
		bool bValid = Test.TestTrue(
			FString::Printf(TEXT("%s uses the shared occupancy-changing seam"), Label),
			bApplied);
		bValid &= Test.TestTrue(
			FString::Printf(TEXT("%s removes only the outgoing registration"), Label),
			!HasRegistrationFor(State, PlayerLeftValue)
				&& HasRegistrationFor(State, OpponentLeftValue));
		bValid &= Test.TestTrue(
			FString::Printf(TEXT("%s installs the exact reserve without RNG"), Label),
			Incoming.BattlerId == MakeNumericId<FBattlerId>(PlayerReserveValue)
				&& NoRandom.GetTrace().IsEmpty());
		return bValid;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC10R2EventReplayPrivacyTest,
		"PokemonSolarus.Battle.C04B.C10Redirection.Integration.EventOrderPrivacyAndSchema6Replay",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FBattleC10R2EventReplayPrivacyTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		FRedirectionFlowEvidence First;
		FRedirectionFlowEvidence Second;
		if (!TestTrue(TEXT("The first complete redirection flow executes"),
				TryRunRedirectionFlow(First))
			|| !TestTrue(TEXT("The second complete redirection flow executes"),
				TryRunRedirectionFlow(Second)))
		{
			return false;
		}
		const TConstArrayView<FBattleEvent> RegistrationTargets =
			First.Registration.TargetResolution.GetEvents();
		const TConstArrayView<FBattleEvent> RegistrationEffects =
			First.Registration.EffectResolution.GetEvents();
		const int32 RegisteredIndex = RegistrationEffects.IndexOfByPredicate(
			[](const FBattleEvent& Event)
			{
				return Event.GetType()
					== EBattleEventType::TargetRedirectionRegistered;
			});
		const int32 RegistrationCompletedIndex = RegistrationEffects.IndexOfByPredicate(
			[](const FBattleEvent& Event)
			{
				return Event.GetType() == EBattleEventType::ActionCompleted;
			});
		bool bValid = TestTrue(TEXT("Registration publishes targets, registration, then completion"),
			RegistrationTargets.Num() == 1
				&& RegistrationTargets[0].GetType()
					== EBattleEventType::TargetsResolved
				&& RegisteredIndex != INDEX_NONE
				&& RegistrationCompletedIndex > RegisteredIndex
				&& RegistrationTargets[0].GetEventOrdinal()
					< RegistrationEffects[RegisteredIndex].GetEventOrdinal());
		bValid &= TestTrue(TEXT("The stored fact retains the exact source action and occupant"),
			First.StoredRegistration.SourceActionId == First.Registration.ActionId
				&& First.StoredRegistration.Redirector.BattlerId
					== MakeNumericId<FBattlerId>(OpponentLeftValue));
		bValid &= TestTrue(TEXT("The later selected-opponent action redirects to the winner"),
			First.Redirected.Targets.bWasRedirected
				&& First.Redirected.Targets.Targets.Num() == 1
				&& First.Redirected.Targets.Targets[0].GetKind()
					== EBattleResolvedTargetKind::Battler
				&& First.Redirected.Targets.Targets[0].GetBattler().BattlerId
					== MakeNumericId<FBattlerId>(OpponentLeftValue));
		const TConstArrayView<FBattleEvent> RedirectedTargets =
			First.Redirected.TargetResolution.GetEvents();
		const TConstArrayView<FBattleEvent> RedirectedEffects =
			First.Redirected.EffectResolution.GetEvents();
		const int32 EffectIndex = RedirectedEffects.IndexOfByPredicate(
			[](const FBattleEvent& Event)
			{
				return Event.GetType() == EBattleEventType::StatStageChanged;
			});
		const int32 RedirectedCompletedIndex = RedirectedEffects.IndexOfByPredicate(
			[](const FBattleEvent& Event)
			{
				return Event.GetType() == EBattleEventType::ActionCompleted;
			});
		bValid &= TestTrue(TEXT("Redirected resolution publishes targets before effects and completion"),
			RedirectedTargets.Num() == 1
				&& RedirectedTargets[0].GetType()
					== EBattleEventType::TargetsResolved
				&& RedirectedTargets[0].GetTargetResolution().IsSet()
				&& RedirectedTargets[0].GetTargetResolution()->bWasRedirected
				&& EffectIndex != INDEX_NONE
				&& RedirectedCompletedIndex > EffectIndex
				&& RedirectedTargets[0].GetEventOrdinal()
					< RedirectedEffects[EffectIndex].GetEventOrdinal());
		bValid &= TestTrue(TEXT("Both flows consume no RNG and serialize identically"),
			First.bRandomExact
				&& Second.bRandomExact
				&& First.ReplayBytes == Second.ReplayBytes);
		bValid &= TestTrue(TEXT("Schema 6 remains the exact replay contract"),
			First.ReplaySchema == 6 && Second.ReplaySchema == 6);

		FRegistrationFormatEvidence Single;
		FRegistrationFormatEvidence PartnerDouble;
		bValid &= TestTrue(TEXT("A real Single registration action executes"),
			TryRunRegistrationFormat(EBattleFormat::Single, Single));
		bValid &= TestTrue(TEXT("Singles fail the effect without registering or consuming RNG"),
			Single.RegistrationCount == 0
				&& !Single.bRegisteredEvent
				&& Single.bFailedEvent
				&& Single.bRandomExact);
		bValid &= TestTrue(TEXT("A real Partner Double registration action executes"),
			TryRunRegistrationFormat(EBattleFormat::PartnerDouble, PartnerDouble));
		bValid &= TestTrue(TEXT("Partner Double reaches the executor registration path without RNG"),
			PartnerDouble.RegistrationCount == 1
				&& PartnerDouble.bRegisteredEvent
				&& !PartnerDouble.bFailedEvent
				&& PartnerDouble.bRandomExact);

		TUniquePtr<FBattleEngine> PrivacyEngine;
		FStrictBattleRandom* PrivacyRandom = nullptr;
		if (!TestTrue(TEXT("The privacy probe engine is created"),
				TryMakeRegistrationEngine(
					EBattleFormat::Double,
					PrivacyEngine,
					PrivacyRandom)))
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
		bValid &= TestTrue(TEXT("A private registration can be seeded"),
			TrySeedRegistration(*PrivacyEngine, OpponentLeftValue, 7201));
		const FBattleSnapshot AfterSnapshot = PrivacyEngine->GetSnapshot();
		bValid &= TestTrue(TEXT("The public projection after private mutation serializes"),
			FBattleReplaySerializer::TrySerializeCanonical(
				PrivacyEngine->ExportReplayRecord(),
				AfterBytes,
				Rejection));
		bValid &= TestTrue(TEXT("Private registrations change no existing public snapshot facts or replay bytes"),
			BeforeSnapshot.GetStateVersion() == AfterSnapshot.GetStateVersion()
				&& BeforeSnapshot.GetTurnId() == AfterSnapshot.GetTurnId()
				&& BeforeSnapshot.GetPhase() == AfterSnapshot.GetPhase()
				&& BeforeSnapshot.GetPartyEntries().Num()
					== AfterSnapshot.GetPartyEntries().Num()
				&& BeforeSnapshot.GetActiveAssignments().Num()
					== AfterSnapshot.GetActiveAssignments().Num()
				&& BeforeBytes == AfterBytes);
		bValid &= TestTrue(TEXT("The privacy probe consumes no RNG"),
			PrivacyRandom != nullptr && PrivacyRandom->IsExact());
		return bValid;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC10R2SwitchCleanupTest,
		"PokemonSolarus.Battle.C04B.C10Redirection.Lifecycle.SharedSwitchAndForcedExecutorCleanup",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FBattleC10R2SwitchCleanupTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		bool bValid = true;
		FAtomicWildScenario VoluntaryScenario = MakeAtomicVoluntarySwitchScenario();
		VoluntaryScenario.Format = EBattleFormat::Double;
		TUniquePtr<FBattleEngine> VoluntaryEngine;
		if (!TestTrue(TEXT("The real voluntary-switch cleanup engine is created"),
				TryMakeSequenceEngine(VoluntaryScenario, {}, VoluntaryEngine))
			|| !TestTrue(TEXT("The real voluntary switch turn locks"),
				TryLockDoubleVoluntarySwitch(*VoluntaryEngine))
			|| !TestTrue(TEXT("The real voluntary switch action starts"),
				BeginExpectedWildAction(
					*VoluntaryEngine,
					PlayerLeftValue,
					EBattleActionKind::Switch))
			|| !TestTrue(TEXT("The voluntary outgoing registration is seeded"),
				TrySeedRegistration(*VoluntaryEngine, PlayerLeftValue, 7001))
			|| !TestTrue(TEXT("The voluntary unrelated registration is seeded"),
				TrySeedRegistration(*VoluntaryEngine, OpponentLeftValue, 7002)))
		{
			return false;
		}
		const FBattleResolution VoluntaryResolution =
			VoluntaryEngine->ExecuteCurrentSwitch();
		const FBattleEngineState& VoluntaryState =
			FBattleC09BWildFlowEngineFixture::GetState(*VoluntaryEngine);
		bValid &= TestTrue(TEXT("The real voluntary route removes only its outgoing registration"),
			VoluntaryResolution.WasAccepted()
				&& !HasRegistrationFor(VoluntaryState, PlayerLeftValue)
				&& HasRegistrationFor(VoluntaryState, OpponentLeftValue));

		FAtomicWildScenario PivotScenario = MakeAtomicPivotSwitchScenario();
		PivotScenario.Format = EBattleFormat::Double;
		TUniquePtr<FBattleEngine> PivotEngine;
		FBattleDecisionRequest PivotRequest;
		if (!TestTrue(TEXT("The real pivot cleanup engine is created"),
				TryMakeSequenceEngine(PivotScenario, {}, PivotEngine))
			|| !TestTrue(TEXT("The real pivot reaches its response route"),
				TryPrepareDoublePivotSwitch(*PivotEngine, PivotRequest))
			|| !TestTrue(TEXT("The pivot outgoing registration is seeded"),
				TrySeedRegistration(*PivotEngine, PlayerLeftValue, 7003))
			|| !TestTrue(TEXT("The pivot unrelated registration is seeded"),
				TrySeedRegistration(*PivotEngine, OpponentLeftValue, 7004)))
		{
			return false;
		}
		FBattleDecision PivotDecision;
		if (!TestTrue(TEXT("The real pivot response is created"),
				FBattleDecision::TryCreateSwitch(
					PivotRequest.GetStateVersion(),
					PivotRequest.GetRequestKind(),
					PivotRequest.GetDecisionOwnerTrainerId(),
					PivotRequest.GetActingBattlerId(),
					MakePartySlotId(2),
					PivotRequest.GetActingSlotId(),
					PivotDecision)))
		{
			return false;
		}
		const FBattleResolution PivotResolution = PivotEngine->SubmitDecision(PivotDecision);
		const FBattleEngineState& PivotState =
			FBattleC09BWildFlowEngineFixture::GetState(*PivotEngine);
		bValid &= TestTrue(TEXT("The real pivot route removes only its outgoing registration"),
			PivotResolution.WasAccepted()
				&& !HasRegistrationFor(PivotState, PlayerLeftValue)
				&& HasRegistrationFor(PivotState, OpponentLeftValue));

		// ShiftResponse is Single-only, so a legal Shift state cannot contain a
		// registration. DecisionFlow routes it through this same occupancy seam.
		bValid &= RunSharedSwitchCleanupCase(
			*this,
			TEXT("Shift shared seam"),
			EBattleSwitchKind::Voluntary);

		const FMoveId MoveId = MakeDefinitionId<FMoveId>(ForcedEntryProbeMoveName);
		FAtomicWildScenario Scenario = MakePreMoveScenario(MoveId);
		Scenario.Format = EBattleFormat::Double;
		Scenario.bTrainerEncounter = true;
		Scenario.bOpponentSwitchReserve = true;
		TUniquePtr<FBattleEngine> Engine;
		FStrictBattleRandom* Random = nullptr;
		if (!TestTrue(TEXT("The forced-switch cleanup engine is created"),
				TryMakeDoubleOpponentReserveStrictEngine(
					Scenario,
					{{0, 0, 0, FBattleSwitchResolver::GetForcedSelectionRulePurpose()}},
					Engine,
					Random))
			|| !TestTrue(TEXT("The forced-switch move reaches effects"),
				TryPrepareEffectsCheckpoint(*Engine, MoveId))
			|| !TestTrue(TEXT("The forced target registration is seeded"),
				TrySeedRegistration(*Engine, OpponentLeftValue, 7301)))
		{
			return false;
		}
		const FBattleResolution Resolution = Engine->ExecuteCurrentMoveEffects();
		const FBattleEngineState& State =
			FBattleC09BWildFlowEngineFixture::GetState(*Engine);
		const FBattleActivePositionState* Active = State.FindActivePosition(
			MakeActiveSlotId(EBattleSide::Opponent, EBattlePosition::Left));
		bValid &= TestTrue(TEXT("Forced switching succeeds through the executor"),
			Resolution.WasAccepted()
				&& Active != nullptr
				&& Active->BattlerId
					== MakeNumericId<FBattlerId>(OpponentReserveValue));
		bValid &= TestTrue(TEXT("The executor removes the exact outgoing registration"),
			!HasRegistrationFor(State, OpponentLeftValue));
		bValid &= TestTrue(TEXT("Forced selection consumes only its exact required draw"),
			Random != nullptr && Random->IsExact());
		return bValid;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC10R2RemovalAndTurnCleanupTest,
		"PokemonSolarus.Battle.C04B.C10Redirection.Lifecycle.FaintCaptureWildAndTurnCleanup",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FBattleC10R2RemovalAndTurnCleanupTest::RunTest(const FString& Parameters)
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
		if (!TestTrue(TEXT("The faint-cleanup engine is created"),
				TryMakeStrictEngine(FaintScenario, DamageDraws, FaintEngine, FaintRandom))
			|| !TestTrue(TEXT("The lethal move reaches effects"),
				TryPrepareEffectsCheckpoint(*FaintEngine, DamageMoveId))
			|| !TestTrue(TEXT("The faint target registration is seeded"),
				TrySeedRegistration(*FaintEngine, OpponentLeftValue, 7401)))
		{
			return false;
		}
		const FBattleResolution FaintResolution =
			FaintEngine->ExecuteCurrentMoveEffects();
		const FBattleEngineState& FaintState =
			FBattleC09BWildFlowEngineFixture::GetState(*FaintEngine);
		bValid &= TestTrue(TEXT("Faint cleanup removes the exact registration"),
			FaintResolution.WasAccepted()
				&& !HasRegistrationFor(FaintState, OpponentLeftValue)
				&& FaintRandom != nullptr
				&& FaintRandom->IsExact());

		FAtomicWildScenario CaptureScenario = MakeAtomicCaptureScenario();
		CaptureScenario.Format = EBattleFormat::Double;
		TUniquePtr<FBattleEngine> CaptureEngine;
		if (!TestTrue(TEXT("The capture-cleanup engine is created"),
				TryMakeSequenceEngine(CaptureScenario, {0, 0, 0, 0}, CaptureEngine))
			|| !TestTrue(TEXT("The capture turn locks"),
				LockTurn(*CaptureEngine, PlayerLeftValue, EBattleActionKind::Bag))
			|| !TestTrue(TEXT("The capture action starts"),
				BeginExpectedWildAction(
					*CaptureEngine,
					PlayerLeftValue,
					EBattleActionKind::Bag))
			|| !TestTrue(TEXT("The capture target registration is seeded"),
				TrySeedRegistration(*CaptureEngine, OpponentLeftValue, 7402)))
		{
			return false;
		}
		const FBattleResolution CaptureResolution =
			CaptureEngine->ExecuteCurrentBagItem();
		bValid &= TestTrue(TEXT("Successful capture removes the exact registration"),
			CaptureResolution.WasAccepted()
				&& !HasRegistrationFor(
					FBattleC09BWildFlowEngineFixture::GetState(*CaptureEngine),
					OpponentLeftValue));

		FAtomicWildScenario FleeScenario;
		FleeScenario.Format = EBattleFormat::Double;
		FleeScenario.WildFleeMode = EBattleWildFleeMode::Always;
		TUniquePtr<FBattleEngine> FleeEngine;
		if (!TestTrue(TEXT("The wild-removal cleanup engine is created"),
				TryMakeSequenceEngine(FleeScenario, {}, FleeEngine))
			|| !TestTrue(TEXT("The WildFlee turn locks"),
				LockTurn(*FleeEngine, OpponentLeftValue, EBattleActionKind::WildFlee))
			|| !TestTrue(TEXT("The WildFlee action starts"),
				BeginExpectedWildAction(
					*FleeEngine,
					OpponentLeftValue,
					EBattleActionKind::WildFlee))
			|| !TestTrue(TEXT("The fleeing registration is seeded"),
				TrySeedRegistration(*FleeEngine, OpponentLeftValue, 7403)))
		{
			return false;
		}
		const FBattleResolution FleeResolution =
			FleeEngine->ExecuteCurrentWildAction();
		bValid &= TestTrue(TEXT("Wild removal clears the exact registration"),
			FleeResolution.WasAccepted()
				&& !HasRegistrationFor(
					FBattleC09BWildFlowEngineFixture::GetState(*FleeEngine),
					OpponentLeftValue));

		FAtomicWildScenario TurnScenario;
		TurnScenario.Format = EBattleFormat::Double;
		TUniquePtr<FBattleEngine> TurnEngine;
		FStrictBattleRandom* TurnRandom = nullptr;
		if (!TestTrue(TEXT("The turn-boundary cleanup engine is created"),
				TryMakeStrictEngine(TurnScenario, {}, TurnEngine, TurnRandom))
			|| !TestTrue(TEXT("The ordinary Double turn locks"),
				LockTurn(*TurnEngine, PlayerLeftValue, EBattleActionKind::Fight))
			|| !TestTrue(TEXT("The turn-scoped registration is seeded"),
				TrySeedRegistration(*TurnEngine, OpponentLeftValue, 7404)))
		{
			return false;
		}
		for (int32 ActionIndex = 0; ActionIndex < 4; ++ActionIndex)
		{
			FExecutedMove Move;
			if (!TestTrue(TEXT("Every ordinary Double action completes"),
					TryExecuteCurrentMove(*TurnEngine, Move)))
			{
				return false;
			}
		}
		const FBattleEngineState& BeforeEndTurn =
			FBattleC09BWildFlowEngineFixture::GetState(*TurnEngine);
		bValid &= TestTrue(TEXT("The registration remains until the exact turn boundary"),
			BeforeEndTurn.Phase == EBattlePhase::EndOfTurn
				&& HasRegistrationFor(BeforeEndTurn, OpponentLeftValue));
		const FTurnId PriorTurnId = BeforeEndTurn.TurnId;
		const FBattleResolution EndTurn = TurnEngine->ResolveEndTurn();
		const FBattleEngineState& AfterEndTurn =
			FBattleC09BWildFlowEngineFixture::GetState(*TurnEngine);
		bValid &= TestTrue(TEXT("The exact turn boundary clears every registration"),
			EndTurn.WasAccepted()
				&& AfterEndTurn.TurnId.GetValue() == PriorTurnId.GetValue() + 1
				&& AfterEndTurn.MoveRedirectionRegistrations.IsEmpty());
		bValid &= TestTrue(TEXT("Ordinary turn cleanup consumes no RNG"),
			TurnRandom != nullptr && TurnRandom->IsExact());
		return bValid;
	}
}

#endif
