#if WITH_DEV_AUTOMATION_TESTS

#include "Battle/BattleTriggerFramework.h"
#include "BattleTestFactories.h"
#include "Misc/AutomationTest.h"

namespace BattleTriggerFrameworkTests
{
	using BattleTest::MakeActiveSlotId;
	using BattleTest::MakeDefinitionId;
	using BattleTest::MakeNumericId;

	FBattleTriggerSubject MakeBattleSubject(const uint64 Value = 7007)
	{
		FBattleTriggerSubject Result;
		const bool bCreated = FBattleTriggerSubject::TryCreateBattle(
			MakeNumericId<FBattleId>(Value),
			Result);
		check(bCreated);
		return Result;
	}

	FBattleTriggerSubject MakeSideSubject(const EBattleSide Side)
	{
		FBattleTriggerSubject Result;
		const bool bCreated = FBattleTriggerSubject::TryCreateSide(Side, Result);
		check(bCreated);
		return Result;
	}

	FBattleTriggerSubject MakeTrainerSubject(const uint64 Value)
	{
		FBattleTriggerSubject Result;
		const bool bCreated = FBattleTriggerSubject::TryCreateTrainer(
			MakeNumericId<FTrainerId>(Value),
			Result);
		check(bCreated);
		return Result;
	}

	FBattleTriggerSubject MakeBattlerSubject(const uint64 Value)
	{
		FBattleTriggerSubject Result;
		const bool bCreated = FBattleTriggerSubject::TryCreateBattler(
			MakeNumericId<FBattlerId>(Value),
			Result);
		check(bCreated);
		return Result;
	}

	FBattleTriggerSubject MakeActiveSlotSubject(
		const EBattleSide Side,
		const EBattlePosition Position)
	{
		FBattleTriggerSubject Result;
		const bool bCreated = FBattleTriggerSubject::TryCreateActiveSlot(
			MakeActiveSlotId(Side, Position),
			Result);
		check(bCreated);
		return Result;
	}

	FBattleTriggerSourceDefinition MakeSourceDefinition(const int32 Index)
	{
		FBattleTriggerSourceDefinition Result;
		bool bCreated = false;
		const FString DefinitionName = FString::Printf(TEXT("Definition.C07A.Source.%d"), Index);
		switch (Index % 3)
		{
		case 0:
			bCreated = FBattleTriggerSourceDefinition::TryCreateCondition(
				MakeDefinitionId<FConditionId>(*DefinitionName),
				Result);
			break;
		case 1:
			bCreated = FBattleTriggerSourceDefinition::TryCreateAbility(
				MakeDefinitionId<FAbilityId>(*DefinitionName),
				Result);
			break;
		case 2:
			bCreated = FBattleTriggerSourceDefinition::TryCreateItem(
				MakeDefinitionId<FItemId>(*DefinitionName),
				Result);
			break;
		default:
			break;
		}
		check(bCreated);
		return Result;
	}

	FBattleTriggerRegistrationSpec MakeRegistrationSpec(
		const EBattleTriggerPhase Phase,
		const int32 Index,
		const FBattleTriggerSubject& Owner = MakeActiveSlotSubject(
			EBattleSide::Player,
			EBattlePosition::Left))
	{
		FBattleTriggerRegistrationSpec Spec;
		Spec.Rule.Phase = Phase;
		const FString EffectName = FString::Printf(TEXT("Effect.C07A.%d"), Index);
		const FString PayloadName = FString::Printf(TEXT("Payload.C07A.%d"), Index);
		Spec.Rule.EffectId = MakeDefinitionId<FBattleTriggerEffectId>(*EffectName);
		Spec.Rule.PayloadId = MakeDefinitionId<FDefinitionId>(*PayloadName);
		Spec.SourceDefinition = MakeSourceDefinition(Index);
		Spec.Owner = Owner;
		Spec.Source = Owner;
		Spec.Targets.Add(FBattleTriggerSubject::CreateField());
		Spec.DurationOwner = Owner;
		Spec.Layers = 1;
		Spec.Visibility = FBattleTriggerVisibility::CreatePublic();
		return Spec;
	}

	FBattleTriggerRegistrationId RegisterChecked(
		FBattleTriggerFramework& Framework,
		const FBattleTriggerRegistrationSpec& Spec)
	{
		FBattleTriggerRegistrationId Result;
		EBattleTriggerError Error = EBattleTriggerError::None;
		const bool bRegistered = Framework.TryRegister(Spec, Result, Error);
		check(bRegistered);
		check(Error == EBattleTriggerError::None);
		return Result;
	}

	FBattleTriggerReentrancyToken MakeToken(const uint64 Value)
	{
		return MakeNumericId<FBattleTriggerReentrancyToken>(Value);
	}

	FBattleTriggerSimultaneousGroupId MakeGroup(const uint64 Value)
	{
		return MakeNumericId<FBattleTriggerSimultaneousGroupId>(Value);
	}

	FBattleTriggerDispatchSpec MakeDispatch(
		const EBattleTriggerPhase Phase,
		const uint64 Token)
	{
		FBattleTriggerDispatchSpec Result;
		Result.Phase = Phase;
		Result.ReentrancyToken = MakeToken(Token);
		return Result;
	}

	FBattleTriggerOperationContext MakeOperationContext(const uint64 Token)
	{
		FBattleTriggerOperationContext Result;
		Result.ReentrancyToken = MakeToken(Token);
		return Result;
	}

	const FBattleTriggerEffectRequest* FindRequest(
		const TArray<FBattleTriggerEffectRequest>& Requests,
		const FBattleTriggerRegistrationId RegistrationId)
	{
		for (const FBattleTriggerEffectRequest& Request : Requests)
		{
			if (Request.RegistrationId == RegistrationId)
			{
				return &Request;
			}
		}
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC07AAllPhasesAndAtomicValidationTest,
	"PokemonSolarus.Battle.C07A.Contract.AllPhasesAndAtomicValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC07AAllPhasesAndAtomicValidationTest::RunTest(const FString& Parameters)
{
	using namespace BattleTriggerFrameworkTests;

	const TArray<EBattleTriggerPhase> Phases = {
		EBattleTriggerPhase::BattleStart,
		EBattleTriggerPhase::TurnStart,
		EBattleTriggerPhase::SelectionEligibility,
		EBattleTriggerPhase::ActionOrderCalculation,
		EBattleTriggerPhase::BeforeAction,
		EBattleTriggerPhase::BeforeAccuracy,
		EBattleTriggerPhase::BeforeHit,
		EBattleTriggerPhase::BeforeDamage,
		EBattleTriggerPhase::AfterDamage,
		EBattleTriggerPhase::AfterHit,
		EBattleTriggerPhase::AfterAction,
		EBattleTriggerPhase::SwitchOut,
		EBattleTriggerPhase::SwitchIn,
		EBattleTriggerPhase::Faint,
		EBattleTriggerPhase::Removal,
		EBattleTriggerPhase::EndTurn,
		EBattleTriggerPhase::Expiry
	};
	TestEqual(TEXT("C07A freezes exactly seventeen trigger phases"), Phases.Num(), 17);

	TestTrue(TEXT("Battle subjects are typed and valid"), MakeBattleSubject().IsValid());
	TestTrue(TEXT("Field subjects are typed and valid"), FBattleTriggerSubject::CreateField().IsValid());
	TestTrue(TEXT("Side subjects are typed and valid"), MakeSideSubject(EBattleSide::Player).IsValid());
	TestTrue(TEXT("Trainer subjects are typed and valid"), MakeTrainerSubject(1).IsValid());
	TestTrue(TEXT("Battler subjects are typed and valid"), MakeBattlerSubject(11).IsValid());
	TestTrue(
		TEXT("Active-slot subjects are typed and valid"),
		MakeActiveSlotSubject(EBattleSide::Opponent, EBattlePosition::Right).IsValid());

	FBattleTriggerFramework Framework;
	for (int32 Index = 0; Index < Phases.Num(); ++Index)
	{
		const FBattleTriggerRegistrationId RegistrationId = RegisterChecked(
			Framework,
			MakeRegistrationSpec(Phases[Index], Index + 1));
		TestEqual(
			TEXT("Successful phase registration receives the next stable identity"),
			RegistrationId.GetValue(),
			static_cast<uint64>(Index + 1));
	}
	TestEqual(
		TEXT("Every frozen phase can be registered"),
		Framework.GetActiveRegistrations().Num(),
		17);

	TArray<FBattleTriggerLifecycleFact> StartedFacts;
	Framework.DrainLifecycleFacts(StartedFacts);
	TestEqual(TEXT("Each successful registration emits one started fact"), StartedFacts.Num(), 17);
	for (int32 Index = 0; Index < StartedFacts.Num(); ++Index)
	{
		TestEqual(
			TEXT("Started facts preserve phase order"),
			StartedFacts[Index].Phase,
			Phases[Index]);
		TestEqual(
			TEXT("Started facts receive stable one-based ordinals"),
			StartedFacts[Index].FactOrdinal,
			static_cast<uint64>(Index + 1));
	}

	EBattleTriggerError Error = EBattleTriggerError::None;
	FBattleTriggerRegistrationId RejectedId;
	FBattleTriggerRegistrationSpec InvalidPhase = MakeRegistrationSpec(EBattleTriggerPhase::BattleStart, 100);
	InvalidPhase.Rule.Phase = static_cast<EBattleTriggerPhase>(255);
	TestFalse(
		TEXT("An unknown phase is rejected atomically"),
		Framework.TryRegister(InvalidPhase, RejectedId, Error));
	TestEqual(TEXT("Unknown phase reports the typed error"), Error, EBattleTriggerError::InvalidPhase);
	TestFalse(TEXT("A rejected registration returns no identity"), RejectedId.IsValid());

	FBattleTriggerRegistrationSpec InvalidDuration = MakeRegistrationSpec(EBattleTriggerPhase::BattleStart, 101);
	InvalidDuration.RemainingTurns = -1;
	TestFalse(
		TEXT("A negative finite duration is rejected atomically"),
		Framework.TryRegister(InvalidDuration, RejectedId, Error));
	TestEqual(TEXT("Negative duration reports the typed error"), Error, EBattleTriggerError::InvalidDuration);

	FBattleTriggerRegistrationSpec InvalidVisibility = MakeRegistrationSpec(EBattleTriggerPhase::BattleStart, 102);
	InvalidVisibility.Visibility.Level = EBattleVisibilityLevel::OwningTrainer;
	TestFalse(
		TEXT("Owning-Trainer visibility requires a Trainer identity"),
		Framework.TryRegister(InvalidVisibility, RejectedId, Error));
	TestEqual(TEXT("Invalid visibility reports the typed error"), Error, EBattleTriggerError::InvalidVisibility);
	TestEqual(
		TEXT("Rejected registrations leave active state unchanged"),
		Framework.GetActiveRegistrations().Num(),
		17);
	TestEqual(
		TEXT("Rejected registrations emit no lifecycle facts"),
		Framework.GetPendingLifecycleFactCount(),
		0);

	const FBattleTriggerRegistrationId NextId = RegisterChecked(
		Framework,
		MakeRegistrationSpec(EBattleTriggerPhase::BattleStart, 103));
	TestEqual(
		TEXT("Rejected registrations consume no registration ordinal"),
		NextId.GetValue(),
		18ULL);
	TArray<FBattleTriggerLifecycleFact> NextFacts;
	Framework.DrainLifecycleFacts(NextFacts);
	TestEqual(TEXT("The next valid registration emits one fact"), NextFacts.Num(), 1);
	TestEqual(
		TEXT("Rejected registrations consume no lifecycle ordinal"),
		NextFacts[0].FactOrdinal,
		18ULL);

	FBattleTriggerDispatchSpec InvalidDispatch = MakeDispatch(EBattleTriggerPhase::BattleStart, 1);
	InvalidDispatch.ReentrancyToken = FBattleTriggerReentrancyToken();
	TestFalse(
		TEXT("A dispatch without a reentrancy token is rejected atomically"),
		Framework.TryEnqueueDispatch(InvalidDispatch, Error));
	TestEqual(
		TEXT("The invalid dispatch reports its token error"),
		Error,
		EBattleTriggerError::InvalidReentrancyToken);
	TestEqual(TEXT("The invalid dispatch consumes no queue slot"), Framework.GetPendingDispatchCount(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC07ACanonicalOrderingTest,
	"PokemonSolarus.Battle.C07A.Ordering.CanonicalKeysAndCreationTie",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC07ACanonicalOrderingTest::RunTest(const FString& Parameters)
{
	using namespace BattleTriggerFrameworkTests;

	FBattleTriggerFramework Framework;
	TArray<FBattleTriggerRegistrationId> Ids;
	for (int32 Index = 1; Index <= 8; ++Index)
	{
		FBattleTriggerRegistrationSpec Spec = MakeRegistrationSpec(
			EBattleTriggerPhase::ActionOrderCalculation,
			Index);
		if (Index == 1)
		{
			Spec.Rule.Order = 1;
		}
		else if (Index == 2)
		{
			Spec.Rule.Priority = 0;
		}
		else
		{
			Spec.Rule.Priority = 1;
			Spec.Rule.Suborder = Index == 3 ? 1 : 0;
		}
		Ids.Add(RegisterChecked(Framework, Spec));
	}
	TArray<FBattleTriggerLifecycleFact> IgnoredFacts;
	Framework.DrainLifecycleFacts(IgnoredFacts);

	const TArray<int32> Speeds = { 0, 0, 0, 100, 200, 200, 200, 200 };
	const TArray<FActiveSlotId> Slots = {
		MakeActiveSlotId(EBattleSide::Player, EBattlePosition::Left),
		MakeActiveSlotId(EBattleSide::Player, EBattlePosition::Left),
		MakeActiveSlotId(EBattleSide::Player, EBattlePosition::Left),
		MakeActiveSlotId(EBattleSide::Player, EBattlePosition::Left),
		MakeActiveSlotId(EBattleSide::Opponent, EBattlePosition::Left),
		MakeActiveSlotId(EBattleSide::Player, EBattlePosition::Right),
		MakeActiveSlotId(EBattleSide::Player, EBattlePosition::Left),
		MakeActiveSlotId(EBattleSide::Player, EBattlePosition::Left)
	};

	auto BuildDispatch = [&Ids, &Speeds, &Slots](const uint64 Token)
	{
		FBattleTriggerDispatchSpec Dispatch = MakeDispatch(
			EBattleTriggerPhase::ActionOrderCalculation,
			Token);
		Dispatch.OrderPolicy.Priority = EBattleTriggerSortDirection::Descending;
		Dispatch.OrderPolicy.bUseEffectiveSpeed = true;
		Dispatch.OrderPolicy.EffectiveSpeed = EBattleTriggerSortDirection::Descending;
		for (int32 Index = 0; Index < Ids.Num(); ++Index)
		{
			FBattleTriggerDispatchParticipant Participant;
			Participant.RegistrationId = Ids[Index];
			Participant.EffectiveSpeed = Speeds[Index];
			Participant.ActiveSlotId = Slots[Index];
			Dispatch.Participants.Add(MoveTemp(Participant));
		}
		return Dispatch;
	};

	const TArray<int32> AscendingExpected = { 6, 7, 5, 4, 3, 2, 1, 0 };
	EBattleTriggerError Error = EBattleTriggerError::None;
	FBattleTriggerDispatchSpec FirstDispatch = BuildDispatch(1);
	TestTrue(TEXT("The canonical ascending dispatch is queued"), Framework.TryEnqueueDispatch(FirstDispatch, Error));
	FBattleTriggerDispatchResult Result;
	TestTrue(TEXT("The canonical ascending dispatch resolves"), Framework.TryResolveNextDispatch(Result, Error));
	TestEqual(TEXT("All eight ordering candidates emit requests"), Result.EffectRequestCount, 8);
	TArray<FBattleTriggerEffectRequest> Requests;
	Framework.DrainEffectRequests(Requests);
	for (int32 Index = 0; Index < AscendingExpected.Num(); ++Index)
	{
		TestTrue(
			TEXT("Canonical key order includes Speed, side, position, and creation"),
			Requests[Index].RegistrationId == Ids[AscendingExpected[Index]]);
	}
	TestEqual(TEXT("The first exact tie uses the earlier creation ordinal"), Requests[0].ResolvedOrder.CreationOrdinal, 7ULL);
	TestEqual(TEXT("The second exact tie uses the later creation ordinal"), Requests[1].ResolvedOrder.CreationOrdinal, 8ULL);

	FBattleTriggerDispatchSpec ReversedPolicy = BuildDispatch(2);
	ReversedPolicy.OrderPolicy.Order = EBattleTriggerSortDirection::Descending;
	ReversedPolicy.OrderPolicy.Priority = EBattleTriggerSortDirection::Ascending;
	ReversedPolicy.OrderPolicy.Suborder = EBattleTriggerSortDirection::Descending;
	ReversedPolicy.OrderPolicy.EffectiveSpeed = EBattleTriggerSortDirection::Ascending;
	ReversedPolicy.OrderPolicy.Side = EBattleTriggerSortDirection::Descending;
	ReversedPolicy.OrderPolicy.Position = EBattleTriggerSortDirection::Descending;
	ReversedPolicy.OrderPolicy.Creation = EBattleTriggerSortDirection::Descending;
	TestTrue(TEXT("The fully reversed policy is queued"), Framework.TryEnqueueDispatch(ReversedPolicy, Error));
	TestTrue(TEXT("The fully reversed policy resolves"), Framework.TryResolveNextDispatch(Result, Error));
	Framework.DrainEffectRequests(Requests);
	const TArray<int32> DescendingExpected = { 0, 1, 2, 3, 4, 5, 7, 6 };
	for (int32 Index = 0; Index < DescendingExpected.Num(); ++Index)
	{
		TestTrue(
			TEXT("Every caller-declared key direction is honored"),
			Requests[Index].RegistrationId == Ids[DescendingExpected[Index]]);
	}

	FBattleTriggerDispatchSpec ReversedInput = BuildDispatch(3);
	TArray<FBattleTriggerDispatchParticipant> ParticipantsInReverse;
	for (int32 Index = ReversedInput.Participants.Num() - 1; Index >= 0; --Index)
	{
		ParticipantsInReverse.Add(ReversedInput.Participants[Index]);
	}
	ReversedInput.Participants = MoveTemp(ParticipantsInReverse);
	TestTrue(TEXT("Reversed candidate input is queued"), Framework.TryEnqueueDispatch(ReversedInput, Error));
	TestTrue(TEXT("Reversed candidate input resolves"), Framework.TryResolveNextDispatch(Result, Error));
	Framework.DrainEffectRequests(Requests);
	for (int32 Index = 0; Index < AscendingExpected.Num(); ++Index)
	{
		TestTrue(
			TEXT("Input order cannot change canonical order"),
			Requests[Index].RegistrationId == Ids[AscendingExpected[Index]]);
	}
	TestFalse(TEXT("The framework contract exposes no random tie draw"), FBattleTriggerFramework::ConsumesRandomness());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC07ADeferredQueueAndReentrancyTest,
	"PokemonSolarus.Battle.C07A.Queue.DeferredRequestsAndReentrancy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC07ADeferredQueueAndReentrancyTest::RunTest(const FString& Parameters)
{
	using namespace BattleTriggerFrameworkTests;

	FBattleTriggerFramework Framework;
	FBattleTriggerRegistrationSpec OnceSpec = MakeRegistrationSpec(EBattleTriggerPhase::BeforeAction, 1);
	FBattleTriggerRegistrationSpec RepeatableSpec = MakeRegistrationSpec(EBattleTriggerPhase::BeforeAction, 2);
	RepeatableSpec.Rule.bRepeatable = true;
	const FBattleTriggerRegistrationId OnceId = RegisterChecked(Framework, OnceSpec);
	const FBattleTriggerRegistrationId RepeatableId = RegisterChecked(Framework, RepeatableSpec);
	TArray<FBattleTriggerLifecycleFact> IgnoredFacts;
	Framework.DrainLifecycleFacts(IgnoredFacts);

	EBattleTriggerError Error = EBattleTriggerError::None;
	const FBattleTriggerDispatchSpec SamePhase = MakeDispatch(EBattleTriggerPhase::BeforeAction, 10);
	TestTrue(TEXT("The first same-phase dispatch is queued"), Framework.TryEnqueueDispatch(SamePhase, Error));
	TestTrue(TEXT("The second same-phase dispatch is queued"), Framework.TryEnqueueDispatch(SamePhase, Error));
	TestEqual(TEXT("Both same-phase dispatches remain deferred"), Framework.GetPendingDispatchCount(), 2);
	TestEqual(TEXT("Queueing invokes no effect directly"), Framework.GetPendingEffectRequestCount(), 0);

	FBattleTriggerDispatchResult Result;
	TestTrue(TEXT("Only the first queued dispatch resolves"), Framework.TryResolveNextDispatch(Result, Error));
	TestEqual(TEXT("The first dispatch emits both declarative requests"), Result.EffectRequestCount, 2);
	TestEqual(TEXT("The second same-phase dispatch remains queued"), Framework.GetPendingDispatchCount(), 1);
	TArray<FBattleTriggerEffectRequest> Requests;
	Framework.DrainEffectRequests(Requests);
	TestEqual(TEXT("The first resolution queues two requests"), Requests.Num(), 2);
	TestTrue(TEXT("The non-repeatable request is present"), FindRequest(Requests, OnceId) != nullptr);
	TestTrue(TEXT("The repeatable request is present"), FindRequest(Requests, RepeatableId) != nullptr);

	TestTrue(TEXT("The second queued dispatch resolves later"), Framework.TryResolveNextDispatch(Result, Error));
	TestEqual(TEXT("The same token skips the non-repeatable trigger"), Result.EffectRequestCount, 1);
	Framework.DrainEffectRequests(Requests);
	TestEqual(TEXT("Only one repeated request is queued"), Requests.Num(), 1);
	TestTrue(TEXT("The explicitly repeatable trigger may repeat"), Requests[0].RegistrationId == RepeatableId);
	TestEqual(TEXT("The repeated request has the next stable ordinal"), Requests[0].RequestOrdinal, 3ULL);

	const FBattleTriggerDispatchSpec NewToken = MakeDispatch(EBattleTriggerPhase::BeforeAction, 11);
	TestTrue(TEXT("A new reentrancy token is queued"), Framework.TryEnqueueDispatch(NewToken, Error));
	TestTrue(TEXT("The new reentrancy token resolves"), Framework.TryResolveNextDispatch(Result, Error));
	TestEqual(TEXT("A new token enables both triggers"), Result.EffectRequestCount, 2);
	Framework.DrainEffectRequests(Requests);
	TestTrue(TEXT("The non-repeatable trigger runs once for the new token"), FindRequest(Requests, OnceId) != nullptr);
	TestTrue(TEXT("The repeatable trigger also runs for the new token"), FindRequest(Requests, RepeatableId) != nullptr);
	TestEqual(TEXT("Resolving work never recursively queues the same phase"), Framework.GetPendingDispatchCount(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC07ADurationAndExpiryTest,
	"PokemonSolarus.Battle.C07A.Duration.DecrementBeforeEffectAndExpiry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC07ADurationAndExpiryTest::RunTest(const FString& Parameters)
{
	using namespace BattleTriggerFrameworkTests;

	const FBattleTriggerSubject TickOwner = MakeBattlerSubject(41);
	const FBattleTriggerSubject WrongOwner = MakeBattlerSubject(42);
	FBattleTriggerFramework Framework;

	FBattleTriggerRegistrationSpec FiniteSpec = MakeRegistrationSpec(EBattleTriggerPhase::EndTurn, 1, TickOwner);
	FiniteSpec.Rule.bDecrementDurationBeforeEffect = true;
	FiniteSpec.RemainingTurns = 2;
	const FBattleTriggerRegistrationId FiniteId = RegisterChecked(Framework, FiniteSpec);

	FBattleTriggerRegistrationSpec IndefiniteSpec = MakeRegistrationSpec(EBattleTriggerPhase::EndTurn, 2, TickOwner);
	IndefiniteSpec.Rule.bDecrementDurationBeforeEffect = true;
	const FBattleTriggerRegistrationId IndefiniteId = RegisterChecked(Framework, IndefiniteSpec);

	FBattleTriggerRegistrationSpec ZeroSpec = MakeRegistrationSpec(EBattleTriggerPhase::EndTurn, 3, TickOwner);
	ZeroSpec.Rule.bDecrementDurationBeforeEffect = true;
	ZeroSpec.RemainingTurns = 0;
	const FBattleTriggerRegistrationId ZeroId = RegisterChecked(Framework, ZeroSpec);

	FBattleTriggerRegistrationSpec WrongOwnerSpec = MakeRegistrationSpec(EBattleTriggerPhase::EndTurn, 4, TickOwner);
	WrongOwnerSpec.Rule.bDecrementDurationBeforeEffect = true;
	WrongOwnerSpec.RemainingTurns = 2;
	WrongOwnerSpec.DurationOwner = WrongOwner;
	const FBattleTriggerRegistrationId WrongOwnerId = RegisterChecked(Framework, WrongOwnerSpec);

	TArray<FBattleTriggerLifecycleFact> Facts;
	Framework.DrainLifecycleFacts(Facts);
	FBattleTriggerDispatchSpec FirstTick = MakeDispatch(EBattleTriggerPhase::EndTurn, 1);
	FirstTick.DurationTickOwners.Add(TickOwner);
	EBattleTriggerError Error = EBattleTriggerError::None;
	TestTrue(TEXT("The first duration tick is queued"), Framework.TryEnqueueDispatch(FirstTick, Error));
	FBattleTriggerDispatchResult Result;
	TestTrue(TEXT("The first duration tick resolves"), Framework.TryResolveNextDispatch(Result, Error));
	TestEqual(TEXT("Zero duration expires before it can emit an effect"), Result.ExpiredCount, 1);
	TestEqual(TEXT("The other three registrations emit effects"), Result.EffectRequestCount, 3);
	TestTrue(TEXT("Expiry is queued instead of recursively executed"), Result.bQueuedExpiryDispatch);
	TestEqual(TEXT("The deferred expiry phase remains queued"), Framework.GetPendingDispatchCount(), 1);

	TArray<FBattleTriggerEffectRequest> Requests;
	Framework.DrainEffectRequests(Requests);
	const FBattleTriggerEffectRequest* FiniteRequest = FindRequest(Requests, FiniteId);
	const FBattleTriggerEffectRequest* IndefiniteRequest = FindRequest(Requests, IndefiniteId);
	const FBattleTriggerEffectRequest* WrongOwnerRequest = FindRequest(Requests, WrongOwnerId);
	TestNotNull(TEXT("Finite duration emits after decrementing"), FiniteRequest);
	TestNotNull(TEXT("Indefinite duration emits without decrementing"), IndefiniteRequest);
	TestNotNull(TEXT("A non-matching duration owner still permits the effect"), WrongOwnerRequest);
	TestTrue(
		TEXT("Finite duration is decremented before the request"),
		FiniteRequest != nullptr
			&& FiniteRequest->RemainingTurns.IsSet()
			&& FiniteRequest->RemainingTurns.GetValue() == 1);
	TestTrue(
		TEXT("Indefinite duration remains unset"),
		IndefiniteRequest != nullptr && !IndefiniteRequest->RemainingTurns.IsSet());
	TestTrue(
		TEXT("A wrong tick owner does not decrement finite duration"),
		WrongOwnerRequest != nullptr
			&& WrongOwnerRequest->RemainingTurns.IsSet()
			&& WrongOwnerRequest->RemainingTurns.GetValue() == 2);
	TestNull(TEXT("Zero duration queues no effect"), FindRequest(Requests, ZeroId));

	Framework.DrainLifecycleFacts(Facts);
	TestEqual(TEXT("The first tick emits two duration facts and one end fact"), Facts.Num(), 3);
	TestEqual(TEXT("The finite duration fact records two turns before"), Facts[0].PreviousRemainingTurns.GetValue(), 2);
	TestEqual(TEXT("The finite duration fact records one turn after"), Facts[0].RemainingTurns.GetValue(), 1);
	TestEqual(TEXT("Zero duration emits an end fact"), Facts[2].Kind, EBattleTriggerLifecycleFactKind::Ended);
	TestEqual(TEXT("Zero duration ends as expired"), Facts[2].EndReason.GetValue(), EBattleTriggerEndReason::Expired);

	FBattleTriggerDispatchSpec SecondTick = MakeDispatch(EBattleTriggerPhase::EndTurn, 2);
	SecondTick.DurationTickOwners.Add(TickOwner);
	TestTrue(TEXT("The second duration tick queues behind expiry"), Framework.TryEnqueueDispatch(SecondTick, Error));
	TestEqual(TEXT("Expiry and the next ordinary phase are both queued"), Framework.GetPendingDispatchCount(), 2);
	TestTrue(TEXT("The deferred expiry phase resolves first"), Framework.TryResolveNextDispatch(Result, Error));
	TestEqual(TEXT("The deferred phase is explicitly Expiry"), Result.Phase, EBattleTriggerPhase::Expiry);
	TestEqual(TEXT("No expiry hook was registered"), Result.EffectRequestCount, 0);
	TestTrue(TEXT("The second ordinary phase then resolves"), Framework.TryResolveNextDispatch(Result, Error));
	TestEqual(TEXT("The one-turn registration expires before its second effect"), Result.ExpiredCount, 1);
	TestEqual(TEXT("Only indefinite and wrong-owner effects remain"), Result.EffectRequestCount, 2);
	Framework.DrainEffectRequests(Requests);
	TestNull(TEXT("The expired finite trigger emits no second effect"), FindRequest(Requests, FiniteId));
	TestNotNull(TEXT("The indefinite trigger still emits"), FindRequest(Requests, IndefiniteId));
	TestNotNull(TEXT("The wrong-owner trigger still emits"), FindRequest(Requests, WrongOwnerId));

	FBattleTriggerRegistrationState State;
	TestFalse(TEXT("The finite registration is removed at zero"), Framework.TryGetRegistration(FiniteId, State));
	TestFalse(TEXT("The initially zero registration is removed"), Framework.TryGetRegistration(ZeroId, State));
	TestTrue(TEXT("The indefinite registration remains active"), Framework.TryGetRegistration(IndefiniteId, State));
	TestFalse(TEXT("Indefinite duration remains absent in runtime state"), State.RemainingTurns.IsSet());
	TestTrue(TEXT("The wrong-owner registration remains active"), Framework.TryGetRegistration(WrongOwnerId, State));
	TestEqual(TEXT("Wrong-owner duration remains two"), State.RemainingTurns.GetValue(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC07ATypedCleanupAndSuppressionTest,
	"PokemonSolarus.Battle.C07A.Cleanup.TypedPoliciesAndSuppression",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC07ATypedCleanupAndSuppressionTest::RunTest(const FString& Parameters)
{
	using namespace BattleTriggerFrameworkTests;

	FBattleTriggerFramework Framework;
	const TArray<FBattleTriggerSubject> Owners = {
		MakeBattlerSubject(101),
		MakeBattlerSubject(102),
		MakeBattlerSubject(103),
		MakeBattleSubject(),
		MakeBattlerSubject(105),
		MakeBattlerSubject(106)
	};
	const TArray<EBattleTriggerCleanupPolicy> Policies = {
		EBattleTriggerCleanupPolicy::OnSwitch,
		EBattleTriggerCleanupPolicy::OnFaint,
		EBattleTriggerCleanupPolicy::OnCapture,
		EBattleTriggerCleanupPolicy::OnBattleEnd,
		EBattleTriggerCleanupPolicy::OnRemoval,
		EBattleTriggerCleanupPolicy::None
	};
	TArray<FBattleTriggerRegistrationId> Ids;
	for (int32 Index = 0; Index < Owners.Num(); ++Index)
	{
		FBattleTriggerRegistrationSpec Spec = MakeRegistrationSpec(
			EBattleTriggerPhase::AfterAction,
			Index + 1,
			Owners[Index]);
		Spec.CleanupPolicy = Policies[Index];
		if (Index == 0)
		{
			Spec.RemainingTurns = 3;
			Spec.Layers = 2;
		}
		Ids.Add(RegisterChecked(Framework, Spec));
	}
	TArray<FBattleTriggerLifecycleFact> Facts;
	Framework.DrainLifecycleFacts(Facts);

	FBattleTriggerRegistrationState BeforeSuppression;
	TestTrue(TEXT("The switch-owned trigger can be queried"), Framework.TryGetRegistration(Ids[0], BeforeSuppression));
	EBattleTriggerError Error = EBattleTriggerError::None;
	TestTrue(
		TEXT("Suppression is applied as a reversible lifecycle change"),
		Framework.TrySetSuppressed(Ids[0], true, MakeOperationContext(1), Error));
	FBattleTriggerRegistrationState Suppressed;
	TestTrue(TEXT("The suppressed registration remains active"), Framework.TryGetRegistration(Ids[0], Suppressed));
	TestTrue(TEXT("Suppression state is visible"), Suppressed.bSuppressed);
	TestEqual(TEXT("Suppression preserves creation order"), Suppressed.CreationOrdinal, BeforeSuppression.CreationOrdinal);
	TestEqual(TEXT("Suppression preserves remaining duration"), Suppressed.RemainingTurns.GetValue(), 3);
	TestEqual(TEXT("Suppression preserves layers"), Suppressed.Layers, 2);

	FBattleTriggerDispatchSpec SuppressedDispatch = MakeDispatch(EBattleTriggerPhase::AfterAction, 2);
	FBattleTriggerDispatchParticipant SuppressedParticipant;
	SuppressedParticipant.RegistrationId = Ids[0];
	SuppressedDispatch.Participants.Add(SuppressedParticipant);
	TestTrue(TEXT("A suppressed trigger can be named by a queued dispatch"), Framework.TryEnqueueDispatch(SuppressedDispatch, Error));
	FBattleTriggerDispatchResult Result;
	TestTrue(TEXT("The suppressed dispatch resolves without mutation"), Framework.TryResolveNextDispatch(Result, Error));
	TestEqual(TEXT("Suppression queues no effect"), Result.EffectRequestCount, 0);

	TestTrue(
		TEXT("Restoration is a reversible lifecycle change"),
		Framework.TrySetSuppressed(Ids[0], false, MakeOperationContext(3), Error));
	FBattleTriggerRegistrationState Restored;
	TestTrue(TEXT("The restored registration remains active"), Framework.TryGetRegistration(Ids[0], Restored));
	TestFalse(TEXT("Restoration clears suppression only"), Restored.bSuppressed);
	TestEqual(TEXT("Restoration preserves creation order"), Restored.CreationOrdinal, BeforeSuppression.CreationOrdinal);
	TestEqual(TEXT("Restoration preserves remaining duration"), Restored.RemainingTurns.GetValue(), 3);
	TestEqual(TEXT("Restoration preserves layers"), Restored.Layers, 2);
	TestTrue(
		TEXT("The token skipped while suppressed remains usable after restoration"),
		Framework.TryEnqueueDispatch(SuppressedDispatch, Error));
	TestTrue(TEXT("The restored dispatch resolves"), Framework.TryResolveNextDispatch(Result, Error));
	TestEqual(TEXT("The restored trigger queues its declarative effect"), Result.EffectRequestCount, 1);
	TArray<FBattleTriggerEffectRequest> Requests;
	Framework.DrainEffectRequests(Requests);
	TestEqual(TEXT("Exactly one restored request is queued"), Requests.Num(), 1);

	FBattleTriggerCleanupRequest InvalidCleanup;
	InvalidCleanup.Reason = EBattleTriggerCleanupReason::Switch;
	InvalidCleanup.Context = MakeOperationContext(9);
	TestFalse(
		TEXT("A non-battle-end cleanup requires typed affected owners"),
		Framework.TryApplyCleanup(InvalidCleanup, Error));
	TestEqual(TEXT("Invalid cleanup is reported without mutation"), Error, EBattleTriggerError::InvalidCleanupRequest);
	TestEqual(TEXT("Invalid cleanup leaves all six registrations active"), Framework.GetActiveRegistrations().Num(), 6);

	auto ApplyTargetedCleanup = [this, &Framework, &Error](
		const EBattleTriggerCleanupReason Reason,
		const FBattleTriggerSubject& Owner,
		const uint64 Token)
	{
		FBattleTriggerCleanupRequest Request;
		Request.Reason = Reason;
		Request.AffectedOwners.Add(Owner);
		Request.Context = BattleTriggerFrameworkTests::MakeOperationContext(Token);
		TestTrue(TEXT("Typed targeted cleanup succeeds"), Framework.TryApplyCleanup(Request, Error));
	};
	ApplyTargetedCleanup(EBattleTriggerCleanupReason::Switch, Owners[0], 10);
	ApplyTargetedCleanup(EBattleTriggerCleanupReason::Faint, Owners[1], 11);
	ApplyTargetedCleanup(EBattleTriggerCleanupReason::Capture, Owners[2], 12);

	FBattleTriggerCleanupRequest BattleEnd;
	BattleEnd.Reason = EBattleTriggerCleanupReason::BattleEnd;
	BattleEnd.Context = MakeOperationContext(13);
	TestTrue(TEXT("Battle-end cleanup is globally typed"), Framework.TryApplyCleanup(BattleEnd, Error));
	ApplyTargetedCleanup(EBattleTriggerCleanupReason::Removal, Owners[4], 14);
	TestEqual(TEXT("Only the no-cleanup-policy registration survives"), Framework.GetActiveRegistrations().Num(), 1);
	FBattleTriggerRegistrationState Survivor;
	TestTrue(TEXT("The no-policy registration is the survivor"), Framework.TryGetRegistration(Ids[5], Survivor));

	Framework.DrainLifecycleFacts(Facts);
	TestEqual(TEXT("Suppression, restoration, and five cleanups emit seven facts"), Facts.Num(), 7);
	TestEqual(TEXT("Suppression emits its typed fact"), Facts[0].Kind, EBattleTriggerLifecycleFactKind::SuppressionChanged);
	TestTrue(TEXT("Suppression records the new state"), Facts[0].IsSuppressed.GetValue());
	TestEqual(TEXT("Restoration emits its typed fact"), Facts[1].Kind, EBattleTriggerLifecycleFactKind::SuppressionChanged);
	TestFalse(TEXT("Restoration records the new state"), Facts[1].IsSuppressed.GetValue());
	const TArray<EBattleTriggerEndReason> ExpectedReasons = {
		EBattleTriggerEndReason::Switch,
		EBattleTriggerEndReason::Faint,
		EBattleTriggerEndReason::Capture,
		EBattleTriggerEndReason::BattleEnd,
		EBattleTriggerEndReason::Removal
	};
	for (int32 Index = 0; Index < ExpectedReasons.Num(); ++Index)
	{
		TestEqual(TEXT("Cleanup emits an ended fact"), Facts[Index + 2].Kind, EBattleTriggerLifecycleFactKind::Ended);
		TestEqual(TEXT("Cleanup preserves its typed end reason"), Facts[Index + 2].EndReason.GetValue(), ExpectedReasons[Index]);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC07ASimultaneousOrdinalsTest,
	"PokemonSolarus.Battle.C07A.Simultaneous.SharedGroupStableOrdinals",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC07ASimultaneousOrdinalsTest::RunTest(const FString& Parameters)
{
	using namespace BattleTriggerFrameworkTests;

	const FBattleTriggerSubject TickOwner = MakeSideSubject(EBattleSide::Player);
	FBattleTriggerFramework Framework;
	for (int32 Index = 1; Index <= 2; ++Index)
	{
		FBattleTriggerRegistrationSpec Spec = MakeRegistrationSpec(
			EBattleTriggerPhase::AfterDamage,
			Index,
			TickOwner);
		Spec.Rule.bDecrementDurationBeforeEffect = true;
		Spec.RemainingTurns = 2;
		RegisterChecked(Framework, Spec);
	}
	TArray<FBattleTriggerLifecycleFact> Facts;
	Framework.DrainLifecycleFacts(Facts);

	const FBattleTriggerSimultaneousGroupId Group = MakeGroup(77);
	FBattleTriggerDispatchSpec Dispatch = MakeDispatch(EBattleTriggerPhase::AfterDamage, 50);
	Dispatch.SimultaneousGroupId = Group;
	Dispatch.DurationTickOwners.Add(TickOwner);
	EBattleTriggerError Error = EBattleTriggerError::None;
	TestTrue(TEXT("The simultaneous dispatch is queued"), Framework.TryEnqueueDispatch(Dispatch, Error));
	FBattleTriggerDispatchResult Result;
	TestTrue(TEXT("The simultaneous dispatch resolves"), Framework.TryResolveNextDispatch(Result, Error));
	TestEqual(TEXT("Both simultaneous triggers emit effects"), Result.EffectRequestCount, 2);

	TArray<FBattleTriggerEffectRequest> Requests;
	Framework.DrainEffectRequests(Requests);
	TestEqual(TEXT("Two simultaneous requests are queued"), Requests.Num(), 2);
	TestTrue(
		TEXT("The first request preserves the shared group"),
		Requests[0].SimultaneousGroupId.IsSet()
			&& Requests[0].SimultaneousGroupId.GetValue() == Group);
	TestTrue(
		TEXT("The second request preserves the shared group"),
		Requests[1].SimultaneousGroupId.IsSet()
			&& Requests[1].SimultaneousGroupId.GetValue() == Group);
	TestEqual(TEXT("The first request has stable ordinal one"), Requests[0].RequestOrdinal, 1ULL);
	TestEqual(TEXT("The second request has stable ordinal two"), Requests[1].RequestOrdinal, 2ULL);
	TestTrue(TEXT("Simultaneous requests retain distinct ordinals"), Requests[0].RequestOrdinal != Requests[1].RequestOrdinal);

	Framework.DrainLifecycleFacts(Facts);
	TestEqual(TEXT("Both duration changes emit lifecycle facts"), Facts.Num(), 2);
	TestTrue(
		TEXT("The first fact preserves the shared group"),
		Facts[0].SimultaneousGroupId.IsSet()
			&& Facts[0].SimultaneousGroupId.GetValue() == Group);
	TestTrue(
		TEXT("The second fact preserves the shared group"),
		Facts[1].SimultaneousGroupId.IsSet()
			&& Facts[1].SimultaneousGroupId.GetValue() == Group);
	TestEqual(TEXT("Fact ordinals continue after two started facts"), Facts[0].FactOrdinal, 3ULL);
	TestEqual(TEXT("The second fact receives the next stable ordinal"), Facts[1].FactOrdinal, 4ULL);
	TestTrue(TEXT("Simultaneous facts retain distinct ordinals"), Facts[0].FactOrdinal != Facts[1].FactOrdinal);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC07ADeepCopyVisibilityAndNoRngTest,
	"PokemonSolarus.Battle.C07A.Facts.DeepCopyVisibilityLayersAndNoRng",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC07ADeepCopyVisibilityAndNoRngTest::RunTest(const FString& Parameters)
{
	using namespace BattleTriggerFrameworkTests;

	const FTrainerId TrainerId = MakeNumericId<FTrainerId>(201);
	const FBattleTriggerSubject Owner = MakeTrainerSubject(201);
	const FBattleTriggerSubject Source = MakeBattlerSubject(211);
	const FBattleTriggerSubject DurationOwner = MakeSideSubject(EBattleSide::Player);
	const TArray<FBattleTriggerSubject> OriginalTargets = {
		FBattleTriggerSubject::CreateField(),
		MakeSideSubject(EBattleSide::Opponent),
		MakeActiveSlotSubject(EBattleSide::Opponent, EBattlePosition::Right)
	};
	FBattleTriggerVisibility OwningTrainerVisibility;
	const bool bVisibilityCreated = FBattleTriggerVisibility::TryCreateOwningTrainer(
		TrainerId,
		OwningTrainerVisibility);
	TestTrue(TEXT("Owning-Trainer visibility accepts a valid Trainer"), bVisibilityCreated);

	FBattleTriggerRegistrationSpec Spec = MakeRegistrationSpec(
		EBattleTriggerPhase::BeforeHit,
		1,
		Owner);
	Spec.Source = Source;
	Spec.Targets = OriginalTargets;
	Spec.DurationOwner = DurationOwner;
	Spec.RemainingTurns = 4;
	Spec.Layers = 2;
	Spec.Visibility = OwningTrainerVisibility;
	Spec.CleanupPolicy = EBattleTriggerCleanupPolicy::OnSwitch
		| EBattleTriggerCleanupPolicy::OnRemoval;
	const FBattleTriggerEffectId OriginalEffectId = Spec.Rule.EffectId;
	const FDefinitionId OriginalPayloadId = Spec.Rule.PayloadId;
	const FBattleTriggerSourceDefinition OriginalSourceDefinition = Spec.SourceDefinition;

	FBattleTriggerFramework Framework;
	const FBattleTriggerRegistrationId RegistrationId = RegisterChecked(Framework, Spec);

	Spec.Owner = FBattleTriggerSubject::CreateField();
	Spec.Source = MakeBattlerSubject(999);
	Spec.Targets.Reset();
	Spec.DurationOwner = MakeBattlerSubject(998);
	Spec.RemainingTurns = 99;
	Spec.Layers = 99;
	Spec.Visibility = FBattleTriggerVisibility::CreatePublic();
	Spec.Rule.EffectId = MakeDefinitionId<FBattleTriggerEffectId>(TEXT("Effect.C07A.Mutated"));
	Spec.Rule.PayloadId = MakeDefinitionId<FDefinitionId>(TEXT("Payload.C07A.Mutated"));
	Spec.SourceDefinition = MakeSourceDefinition(2);

	FBattleTriggerRegistrationState State;
	TestTrue(TEXT("The registered deep copy can be queried"), Framework.TryGetRegistration(RegistrationId, State));
	TestTrue(TEXT("The copied owner is unchanged"), State.Spec.Owner == Owner);
	TestTrue(TEXT("The copied source is unchanged"), State.Spec.Source == Source);
	TestEqual(TEXT("The copied target array retains all entries"), State.Spec.Targets.Num(), 3);
	for (int32 Index = 0; Index < OriginalTargets.Num(); ++Index)
	{
		TestTrue(TEXT("Copied targets preserve stable typed order"), State.Spec.Targets[Index] == OriginalTargets[Index]);
	}
	TestTrue(TEXT("The copied duration owner is unchanged"), State.Spec.DurationOwner == DurationOwner);
	TestEqual(TEXT("The copied finite duration is unchanged"), State.RemainingTurns.GetValue(), 4);
	TestEqual(TEXT("The copied layer count is unchanged"), State.Layers, 2);
	TestEqual(TEXT("The copied visibility level is unchanged"), State.Spec.Visibility.Level, EBattleVisibilityLevel::OwningTrainer);
	TestTrue(TEXT("The copied visibility Trainer is unchanged"), State.Spec.Visibility.OwningTrainerId == TrainerId);
	TestTrue(TEXT("The copied effect identity is unchanged"), State.Spec.Rule.EffectId == OriginalEffectId);
	TestTrue(TEXT("The copied payload identity is unchanged"), State.Spec.Rule.PayloadId == OriginalPayloadId);
	TestTrue(TEXT("The copied source definition is unchanged"), State.Spec.SourceDefinition == OriginalSourceDefinition);

	TArray<FBattleTriggerLifecycleFact> Facts;
	Framework.DrainLifecycleFacts(Facts);
	EBattleTriggerError Error = EBattleTriggerError::None;
	TestFalse(
		TEXT("A zero-layer update is rejected atomically"),
		Framework.TryUpdateLayers(RegistrationId, 0, MakeOperationContext(20), Error));
	TestEqual(TEXT("The invalid layer reports its typed error"), Error, EBattleTriggerError::InvalidLayers);
	TestEqual(TEXT("The invalid layer emits no fact"), Framework.GetPendingLifecycleFactCount(), 0);
	TestTrue(
		TEXT("A valid layer update succeeds"),
		Framework.TryUpdateLayers(RegistrationId, 4, MakeOperationContext(21), Error));
	TestTrue(TEXT("The updated registration remains queryable"), Framework.TryGetRegistration(RegistrationId, State));
	TestEqual(TEXT("The layer update changes runtime layers"), State.Layers, 4);
	TestEqual(TEXT("The layer update preserves duration"), State.RemainingTurns.GetValue(), 4);
	Framework.DrainLifecycleFacts(Facts);
	TestEqual(TEXT("A layer update emits one fact"), Facts.Num(), 1);
	TestEqual(TEXT("The layer fact has the typed kind"), Facts[0].Kind, EBattleTriggerLifecycleFactKind::LayerChanged);
	TestEqual(TEXT("The layer fact records the old value"), Facts[0].PreviousLayers.GetValue(), 2);
	TestEqual(TEXT("The layer fact records the new value"), Facts[0].Layers.GetValue(), 4);

	FBattleTriggerDispatchSpec Dispatch = MakeDispatch(EBattleTriggerPhase::BeforeHit, 31);
	Dispatch.SimultaneousGroupId = MakeGroup(301);
	FBattleTriggerDispatchParticipant Participant;
	Participant.RegistrationId = RegistrationId;
	Participant.EffectiveSpeed = 87;
	Participant.ActiveSlotId = MakeActiveSlotId(EBattleSide::Opponent, EBattlePosition::Right);
	Dispatch.Participants.Add(Participant);
	Dispatch.OrderPolicy.bUseEffectiveSpeed = true;
	TestTrue(TEXT("The deep-copy dispatch is queued"), Framework.TryEnqueueDispatch(Dispatch, Error));
	Dispatch.ReentrancyToken = FBattleTriggerReentrancyToken();
	Dispatch.SimultaneousGroupId.Reset();
	Dispatch.Participants.Reset();
	FBattleTriggerDispatchResult Result;
	TestTrue(TEXT("The queued dispatch is independent of caller mutation"), Framework.TryResolveNextDispatch(Result, Error));
	TestEqual(TEXT("The copied dispatch emits one request"), Result.EffectRequestCount, 1);

	TArray<FBattleTriggerEffectRequest> Requests;
	Framework.DrainEffectRequests(Requests);
	TestEqual(TEXT("Exactly one deep-copied request is queued"), Requests.Num(), 1);
	const FBattleTriggerEffectRequest& Request = Requests[0];
	TestTrue(TEXT("The request retains its owner"), Request.Owner == Owner);
	TestTrue(TEXT("The request retains its source"), Request.Source == Source);
	TestEqual(TEXT("The request retains all targets"), Request.Targets.Num(), 3);
	TestTrue(TEXT("The request retains its duration owner"), Request.DurationOwner == DurationOwner);
	TestEqual(TEXT("The request uses updated runtime layers"), Request.Layers, 4);
	TestEqual(TEXT("The request retains finite duration"), Request.RemainingTurns.GetValue(), 4);
	TestEqual(TEXT("The request retains Owning-Trainer visibility"), Request.Visibility.Level, EBattleVisibilityLevel::OwningTrainer);
	TestTrue(TEXT("The request retains its effect identity"), Request.EffectId == OriginalEffectId);
	TestTrue(TEXT("The request retains its payload identity"), Request.PayloadId == OriginalPayloadId);
	TestTrue(TEXT("The request retains its source definition"), Request.SourceDefinition == OriginalSourceDefinition);
	TestEqual(TEXT("The request retains copied effective Speed"), Request.ResolvedOrder.EffectiveSpeed.GetValue(), 87);
	TestEqual(TEXT("The request retains copied side order"), Request.ResolvedOrder.SideOrdinal, static_cast<uint8>(1));
	TestEqual(TEXT("The request retains copied position order"), Request.ResolvedOrder.PositionOrdinal, static_cast<uint8>(1));
	TestTrue(
		TEXT("The request retains the copied simultaneous group"),
		Request.SimultaneousGroupId.IsSet()
			&& Request.SimultaneousGroupId.GetValue() == MakeGroup(301));

	FBattleTriggerRegistrationSpec InvalidVisibility = MakeRegistrationSpec(
		EBattleTriggerPhase::BeforeHit,
		8,
		Owner);
	InvalidVisibility.Visibility.Level = EBattleVisibilityLevel::OwningSide;
	InvalidVisibility.Visibility.bHasOwningSide = false;
	FBattleTriggerRegistrationId RejectedId;
	TestFalse(
		TEXT("Owning-Side visibility requires an explicit side"),
		Framework.TryRegister(InvalidVisibility, RejectedId, Error));
	TestEqual(TEXT("The visibility failure is typed"), Error, EBattleTriggerError::InvalidVisibility);
	TestFalse(TEXT("The visibility failure assigns no identity"), RejectedId.IsValid());
	TestFalse(TEXT("C07A exposes a deterministic no-RNG contract"), FBattleTriggerFramework::ConsumesRandomness());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
