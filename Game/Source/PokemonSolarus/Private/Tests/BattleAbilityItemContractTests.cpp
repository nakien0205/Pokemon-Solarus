#include "Misc/AutomationTest.h"

#include "Battle/BattleAbilityItemContracts.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	template <typename IdType>
	IdType MakeC08ANumericId(const uint64 Value)
	{
		IdType Id;
		const bool bCreated = IdType::TryCreate(Value, Id);
		check(bCreated);
		return Id;
	}

	template <typename IdType>
	IdType MakeNamedId(const TCHAR* Name)
	{
		IdType Id;
		const bool bCreated = IdType::TryCreate(FName(Name), Id);
		check(bCreated);
		return Id;
	}

	FBattleTriggerSubject MakeBattlerSubject(const uint64 BattlerValue)
	{
		FBattleTriggerSubject Subject;
		const bool bCreated = FBattleTriggerSubject::TryCreateBattler(
			MakeC08ANumericId<FBattlerId>(BattlerValue),
			Subject);
		check(bCreated);
		return Subject;
	}

	FBattleAbilityItemHookDefinition MakeHookDefinition(
		const TCHAR* Suffix,
		const EBattleAbilityItemHookPoint HookPoint,
		const EBattleAbilityItemEffectKind EffectKind,
		const EBattleTriggerPhase Phase,
		const int32 Order = 0,
		const EBattleAbilityItemRevealPolicy RevealPolicy =
			EBattleAbilityItemRevealPolicy::OnAppliedEffect,
		const bool bBreakable = false)
	{
		FBattleAbilityItemHookDefinition Definition;
		const FString HookName = FString::Printf(TEXT("Hook.C08A.%s"), Suffix);
		const FString EffectName = FString::Printf(TEXT("HookEffect.C08A.%s"), Suffix);
		const FString PayloadName = FString::Printf(TEXT("HookPayload.C08A.%s"), Suffix);
		Definition.HookId = MakeNamedId<FDefinitionId>(*HookName);
		Definition.HookPoint = HookPoint;
		Definition.EffectKind = EffectKind;
		Definition.TriggerRule.Phase = Phase;
		Definition.TriggerRule.EffectId = MakeNamedId<FBattleTriggerEffectId>(*EffectName);
		Definition.TriggerRule.PayloadId = MakeNamedId<FDefinitionId>(*PayloadName);
		Definition.TriggerRule.Order = Order;
		Definition.RevealPolicy = RevealPolicy;
		Definition.bBreakable = bBreakable;
		return Definition;
	}

	FBattleAbilityItemHookRegistrationFacts MakeHookFacts(
		const FBattleAbilityItemHookDefinition& Definition,
		const uint64 BattlerValue,
		const TCHAR* SourceName,
		const bool bAbilitySource)
	{
		FBattleAbilityItemHookRegistrationFacts Facts;
		Facts.Definition = Definition;
		const bool bSourceCreated = bAbilitySource
			? FBattleTriggerSourceDefinition::TryCreateAbility(
				MakeNamedId<FAbilityId>(SourceName),
				Facts.SourceDefinition)
			: FBattleTriggerSourceDefinition::TryCreateItem(
				MakeNamedId<FItemId>(SourceName),
				Facts.SourceDefinition);
		check(bSourceCreated);
		Facts.Owner = MakeBattlerSubject(BattlerValue);
		Facts.Source = Facts.Owner;
		Facts.Targets.Add(Facts.Owner);
		Facts.DurationOwner = Facts.Owner;
		Facts.Visibility = FBattleTriggerVisibility::CreatePublic();
		return Facts;
	}

	FBattleTriggerEffectRequest MakeTriggerRequest(
		const FBattleAbilityItemHookDefinition& Definition,
		const FBattleAbilityItemHookRegistrationFacts& Facts,
		const uint64 RequestOrdinal = 1)
	{
		FBattleTriggerEffectRequest Request;
		Request.RequestOrdinal = RequestOrdinal;
		Request.RegistrationId = MakeC08ANumericId<FBattleTriggerRegistrationId>(RequestOrdinal);
		Request.Phase = Definition.TriggerRule.Phase;
		Request.EffectId = Definition.TriggerRule.EffectId;
		Request.PayloadId = Definition.TriggerRule.PayloadId;
		Request.SourceDefinition = Facts.SourceDefinition;
		Request.Owner = Facts.Owner;
		Request.Source = Facts.Source;
		Request.Targets = Facts.Targets;
		Request.DurationOwner = Facts.DurationOwner;
		Request.Layers = Facts.Layers;
		Request.Visibility = Facts.Visibility;
		Request.ReentrancyToken = MakeC08ANumericId<FBattleTriggerReentrancyToken>(RequestOrdinal);
		return Request;
	}

	FBattleHeldItemInstanceState MakePersistentItem(
		const uint64 InstanceValue,
		const uint64 TrainerValue,
		const uint64 BattlerValue,
		const TCHAR* ItemName)
	{
		FBattleHeldItemInstanceState State;
		State.InstanceId = MakeC08ANumericId<FBattleHeldItemInstanceId>(InstanceValue);
		State.Origin = EBattleHeldItemOrigin::Persistent;
		State.DefinitionItemId = MakeNamedId<FItemId>(ItemName);
		State.OriginalOwnerTrainerId = MakeC08ANumericId<FTrainerId>(TrainerValue);
		State.OriginalOwnerBattlerId = MakeC08ANumericId<FBattlerId>(BattlerValue);
		State.OriginalItemId = State.DefinitionItemId;
		State.CurrentHolderTrainerId = State.OriginalOwnerTrainerId;
		State.CurrentHolderBattlerId = State.OriginalOwnerBattlerId;
		State.CurrentItemId = State.DefinitionItemId;
		return State;
	}

	FBattleHeldItemInstanceState MakeGeneratedItem(
		const uint64 InstanceValue,
		const uint64 TrainerValue,
		const uint64 BattlerValue,
		const TCHAR* ItemName)
	{
		FBattleHeldItemInstanceState State;
		State.InstanceId = MakeC08ANumericId<FBattleHeldItemInstanceId>(InstanceValue);
		State.Origin = EBattleHeldItemOrigin::BattleGenerated;
		State.DefinitionItemId = MakeNamedId<FItemId>(ItemName);
		State.CurrentHolderTrainerId = MakeC08ANumericId<FTrainerId>(TrainerValue);
		State.CurrentHolderBattlerId = MakeC08ANumericId<FBattlerId>(BattlerValue);
		State.CurrentItemId = State.DefinitionItemId;
		return State;
	}

	TArray<FBattleHeldItemInstanceState> CopyLedgerStates(const FBattleHeldItemLedger& Ledger)
	{
		TArray<FBattleHeldItemInstanceState> Copy;
		for (const FBattleHeldItemInstanceState& State : Ledger.GetStates())
		{
			Copy.Add(State);
		}
		return Copy;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC08AHookVocabularyTest,
	"PokemonSolarus.Battle.C08A.Contracts.HookVocabularyAndAtomicValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC08AHookVocabularyTest::RunTest(const FString& Parameters)
{
	for (uint8 HookIndex = 0;
		HookIndex <= static_cast<uint8>(EBattleAbilityItemHookPoint::FieldCreation);
		++HookIndex)
	{
		const EBattleAbilityItemHookPoint HookPoint =
			static_cast<EBattleAbilityItemHookPoint>(HookIndex);
		const EBattleAbilityItemEffectKind EffectKind =
			static_cast<EBattleAbilityItemEffectKind>(
				HookIndex % (static_cast<uint8>(EBattleAbilityItemEffectKind::TemporarilyStealItem) + 1));
		const FString Suffix = FString::Printf(TEXT("Vocabulary.%u"), HookIndex);
		const FBattleAbilityItemHookDefinition Definition = MakeHookDefinition(
			*Suffix,
			HookPoint,
			EffectKind,
			static_cast<EBattleTriggerPhase>(
				HookIndex % (static_cast<uint8>(EBattleTriggerPhase::Expiry) + 1)));
		const FBattleAbilityItemHookRegistrationFacts Facts = MakeHookFacts(
			Definition,
			100 + HookIndex,
			HookIndex % 2 == 0 ? TEXT("Ability.C08A.Generic") : TEXT("Item.C08A.Generic"),
			HookIndex % 2 == 0);

		FBattleTriggerRegistrationSpec Registration;
		EBattleAbilityItemHookError Error = EBattleAbilityItemHookError::InvalidDefinition;
		TestTrue(
			FString::Printf(TEXT("Hook point %u builds a validated registration"), HookIndex),
			FBattleAbilityItemHookContracts::TryBuildTriggerRegistration(
				Facts,
				Registration,
				Error));
		TestEqual(TEXT("A valid registration reports no contract error"), Error,
			EBattleAbilityItemHookError::None);
		TestEqual(TEXT("The authored phase is preserved"), Registration.Rule.Phase,
			Definition.TriggerRule.Phase);
		TestTrue(TEXT("Only Ability or item sources reach C07A"),
			Registration.SourceDefinition.Kind == EBattleTriggerSourceDefinitionKind::Ability
			|| Registration.SourceDefinition.Kind == EBattleTriggerSourceDefinitionKind::Item);
	}

	FBattleAbilityItemHookDefinition InvalidDefinition = MakeHookDefinition(
		TEXT("Invalid"),
		EBattleAbilityItemHookPoint::Speed,
		EBattleAbilityItemEffectKind::Modify,
		EBattleTriggerPhase::ActionOrderCalculation);
	InvalidDefinition.HookPoint = EBattleAbilityItemHookPoint::Invalid;
	TestFalse(TEXT("An invalid semantic hook is rejected"),
		FBattleAbilityItemHookContracts::IsDefinitionValid(InvalidDefinition));

	FBattleAbilityItemHookRegistrationFacts ConditionFacts = MakeHookFacts(
		MakeHookDefinition(
			TEXT("ConditionRejected"),
			EBattleAbilityItemHookPoint::EndTurn,
			EBattleAbilityItemEffectKind::Prevent,
			EBattleTriggerPhase::EndTurn),
		200,
		TEXT("Ability.C08A.Replaced"),
		true);
	TestTrue(TEXT("Condition source fixture is valid"),
		FBattleTriggerSourceDefinition::TryCreateCondition(
			MakeNamedId<FConditionId>(TEXT("Condition.C08A.NotAllowed")),
			ConditionFacts.SourceDefinition));
	FBattleTriggerRegistrationSpec RejectedRegistration;
	EBattleAbilityItemHookError RejectedError = EBattleAbilityItemHookError::None;
	TestFalse(TEXT("C08A does not claim condition registrations"),
		FBattleAbilityItemHookContracts::TryBuildTriggerRegistration(
			ConditionFacts,
			RejectedRegistration,
			RejectedError));
	TestEqual(TEXT("The condition-source rejection is typed"), RejectedError,
		EBattleAbilityItemHookError::InvalidSourceDefinition);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC08AOrderingTest,
	"PokemonSolarus.Battle.C08A.Ordering.CanonicalC07ARequests",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC08AOrderingTest::RunTest(const FString& Parameters)
{
	struct FRegisteredHook
	{
		FBattleAbilityItemHookDefinition Definition;
		FBattleAbilityItemHookRegistrationFacts Facts;
		FBattleTriggerRegistrationId RegistrationId;
		FActiveSlotId ActiveSlotId;
	};

	TArray<FRegisteredHook> Hooks;
	auto AddHook = [&Hooks, this](
		const TCHAR* Suffix,
		const int32 Order,
		const uint64 BattlerValue,
		const EBattleSide Side,
		const EBattlePosition Position,
		const bool bAbility,
		FBattleTriggerFramework& Framework)
	{
		FRegisteredHook& Hook = Hooks.AddDefaulted_GetRef();
		Hook.Definition = MakeHookDefinition(
			Suffix,
			EBattleAbilityItemHookPoint::FinalDamage,
			EBattleAbilityItemEffectKind::Modify,
			EBattleTriggerPhase::BeforeDamage,
			Order);
		Hook.Facts = MakeHookFacts(
			Hook.Definition,
			BattlerValue,
			bAbility ? TEXT("Ability.C08A.Order") : TEXT("Item.C08A.Order"),
			bAbility);
		EBattleAbilityItemHookError Error = EBattleAbilityItemHookError::InvalidDefinition;
		TestTrue(FString::Printf(TEXT("%s registers through C07A"), Suffix),
			FBattleAbilityItemHookContracts::TryRegisterHook(
				Framework,
				Hook.Facts,
				Hook.RegistrationId,
				Error));
		TestEqual(TEXT("Registration reports no contract error"), Error,
			EBattleAbilityItemHookError::None);
		TestTrue(TEXT("The ordering slot is valid"),
			FActiveSlotId::TryCreate(Side, Position, Hook.ActiveSlotId));
	};

	FBattleTriggerFramework Framework;
	AddHook(TEXT("HighOrder"), 20, 301, EBattleSide::Opponent, EBattlePosition::Right, true, Framework);
	AddHook(TEXT("PlayerCreationFirst"), 10, 302, EBattleSide::Player, EBattlePosition::Right, false, Framework);
	AddHook(TEXT("PlayerCreationSecond"), 10, 303, EBattleSide::Player, EBattlePosition::Right, true, Framework);
	AddHook(TEXT("OpponentAfterPlayer"), 10, 304, EBattleSide::Opponent, EBattlePosition::Left, false, Framework);

	TArray<FBattleTriggerLifecycleFact> RegistrationFacts;
	Framework.DrainLifecycleFacts(RegistrationFacts);

	FBattleTriggerDispatchSpec Dispatch;
	Dispatch.Phase = EBattleTriggerPhase::BeforeDamage;
	Dispatch.ReentrancyToken = MakeC08ANumericId<FBattleTriggerReentrancyToken>(88);
	Dispatch.OrderPolicy.Order = EBattleTriggerSortDirection::Descending;
	for (const FRegisteredHook& Hook : Hooks)
	{
		FBattleTriggerDispatchParticipant& Participant =
			Dispatch.Participants.AddDefaulted_GetRef();
		Participant.RegistrationId = Hook.RegistrationId;
		Participant.ActiveSlotId = Hook.ActiveSlotId;
	}

	EBattleTriggerError TriggerError = EBattleTriggerError::InvalidParticipant;
	FBattleTriggerDispatchResult DispatchResult;
	TestTrue(TEXT("The shared framework accepts the C08A dispatch"),
		Framework.TryEnqueueDispatch(Dispatch, TriggerError));
	TestTrue(TEXT("The shared framework resolves the C08A dispatch"),
		Framework.TryResolveNextDispatch(DispatchResult, TriggerError));
	TestFalse(TEXT("C08A ordering adds no RNG"), FBattleTriggerFramework::ConsumesRandomness());

	TArray<FBattleTriggerEffectRequest> Requests;
	Framework.DrainEffectRequests(Requests);
	TestEqual(TEXT("All four generic hooks emit requests"), Requests.Num(), 4);
	if (Requests.Num() == 4)
	{
		TestEqual(TEXT("Canonical order runs first"), Requests[0].EffectId,
			Hooks[0].Definition.TriggerRule.EffectId);
		TestEqual(TEXT("Player side wins the remaining stable side key"), Requests[1].EffectId,
			Hooks[1].Definition.TriggerRule.EffectId);
		TestEqual(TEXT("Creation order breaks a same-position tie"), Requests[2].EffectId,
			Hooks[2].Definition.TriggerRule.EffectId);
		TestEqual(TEXT("Opponent side follows the tied player hooks"), Requests[3].EffectId,
			Hooks[3].Definition.TriggerRule.EffectId);

		for (int32 RequestIndex = 0; RequestIndex < Requests.Num(); ++RequestIndex)
		{
			const FRegisteredHook* MatchingHook = Hooks.FindByPredicate(
				[&Requests, RequestIndex](const FRegisteredHook& Hook)
				{
					return Hook.Definition.TriggerRule.EffectId == Requests[RequestIndex].EffectId;
				});
			TestNotNull(TEXT("Each request retains a matching semantic definition"), MatchingHook);
			if (MatchingHook != nullptr)
			{
				FBattleAbilityItemEffectRequest TypedRequest;
				EBattleAbilityItemHookError Error = EBattleAbilityItemHookError::InvalidDefinition;
				TestTrue(TEXT("Ordered C07A work converts to typed C08A work"),
					FBattleAbilityItemHookContracts::TryCreateTypedEffectRequest(
						MatchingHook->Definition,
						Requests[RequestIndex],
						TypedRequest,
						Error));
				TestEqual(TEXT("The semantic hook point is preserved"), TypedRequest.HookPoint,
					EBattleAbilityItemHookPoint::FinalDamage);
			}
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC08ATypedRequestTest,
	"PokemonSolarus.Battle.C08A.Requests.TypedOperationsAndMismatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC08ATypedRequestTest::RunTest(const FString& Parameters)
{
	for (uint8 EffectIndex = 0;
		EffectIndex <= static_cast<uint8>(EBattleAbilityItemEffectKind::TemporarilyStealItem);
		++EffectIndex)
	{
		const EBattleAbilityItemEffectKind EffectKind =
			static_cast<EBattleAbilityItemEffectKind>(EffectIndex);
		const FString Suffix = FString::Printf(TEXT("Typed.%u"), EffectIndex);
		const FBattleAbilityItemHookDefinition Definition = MakeHookDefinition(
			*Suffix,
			EBattleAbilityItemHookPoint::EffectApplication,
			EffectKind,
			EBattleTriggerPhase::AfterHit,
			0,
			EBattleAbilityItemRevealPolicy::Never,
			EffectKind == EBattleAbilityItemEffectKind::Prevent);
		const FBattleAbilityItemHookRegistrationFacts Facts = MakeHookFacts(
			Definition,
			400 + EffectIndex,
			EffectIndex % 2 == 0 ? TEXT("Ability.C08A.Typed") : TEXT("Item.C08A.Typed"),
			EffectIndex % 2 == 0);
		const FBattleTriggerEffectRequest TriggerRequest = MakeTriggerRequest(
			Definition,
			Facts,
			EffectIndex + 1);

		FBattleAbilityItemEffectRequest TypedRequest;
		EBattleAbilityItemHookError Error = EBattleAbilityItemHookError::InvalidDefinition;
		TestTrue(FString::Printf(TEXT("Effect kind %u creates typed work"), EffectIndex),
			FBattleAbilityItemHookContracts::TryCreateTypedEffectRequest(
				Definition,
				TriggerRequest,
				TypedRequest,
				Error));
		TestEqual(TEXT("The exact operation kind is retained"), TypedRequest.EffectKind,
			EffectKind);
		TestEqual(TEXT("Breakability remains explicit"), TypedRequest.bBreakable,
			Definition.bBreakable);
	}

	const FBattleAbilityItemHookDefinition Definition = MakeHookDefinition(
		TEXT("Mismatch"),
		EBattleAbilityItemHookPoint::TypeImmunity,
		EBattleAbilityItemEffectKind::Prevent,
		EBattleTriggerPhase::BeforeHit);
	const FBattleAbilityItemHookRegistrationFacts Facts = MakeHookFacts(
		Definition,
		500,
		TEXT("Ability.C08A.Mismatch"),
		true);
	FBattleTriggerEffectRequest Mismatched = MakeTriggerRequest(Definition, Facts);
	Mismatched.PayloadId = MakeNamedId<FDefinitionId>(TEXT("HookPayload.C08A.Wrong"));
	FBattleAbilityItemEffectRequest RejectedRequest;
	EBattleAbilityItemHookError RejectedError = EBattleAbilityItemHookError::None;
	TestFalse(TEXT("A request cannot be interpreted using a different payload"),
		FBattleAbilityItemHookContracts::TryCreateTypedEffectRequest(
			Definition,
			Mismatched,
			RejectedRequest,
			RejectedError));
	TestEqual(TEXT("The mismatch is typed"), RejectedError,
		EBattleAbilityItemHookError::MismatchedTriggerRequest);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC08AVisibilityTest,
	"PokemonSolarus.Battle.C08A.Visibility.NoLeakAndRepeatReveal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC08AVisibilityTest::RunTest(const FString& Parameters)
{
	const FBattleAbilityItemHookDefinition AppliedDefinition = MakeHookDefinition(
		TEXT("RevealApplied"),
		EBattleAbilityItemHookPoint::TypeImmunity,
		EBattleAbilityItemEffectKind::Prevent,
		EBattleTriggerPhase::BeforeHit,
		0,
		EBattleAbilityItemRevealPolicy::OnAppliedEffect,
		true);
	const FBattleAbilityItemHookRegistrationFacts AppliedFacts = MakeHookFacts(
		AppliedDefinition,
		600,
		TEXT("Ability.C08A.Hidden"),
		true);
	FBattleAbilityItemEffectRequest AppliedRequest;
	EBattleAbilityItemHookError Error = EBattleAbilityItemHookError::InvalidDefinition;
	TestTrue(TEXT("The reveal fixture creates typed work"),
		FBattleAbilityItemHookContracts::TryCreateTypedEffectRequest(
			AppliedDefinition,
			MakeTriggerRequest(AppliedDefinition, AppliedFacts),
			AppliedRequest,
			Error));

	FBattleAbilityItemRevealTracker Tracker;
	TOptional<FBattleAbilityItemActivationFact> Fact;
	for (const EBattleAbilityItemActivationOutcome HiddenOutcome :
		{EBattleAbilityItemActivationOutcome::AttemptedButPrevented,
		 EBattleAbilityItemActivationOutcome::Ineligible,
		 EBattleAbilityItemActivationOutcome::Suppressed,
		 EBattleAbilityItemActivationOutcome::Ignored})
	{
		TestTrue(TEXT("A hidden non-activation is a valid evaluated result"),
			Tracker.TryRecordActivation(AppliedRequest, HiddenOutcome, Fact, Error));
		TestFalse(TEXT("A hidden non-activation emits no public fact"), Fact.IsSet());
		TestFalse(TEXT("A hidden non-activation reveals no definition"),
			Tracker.HasBeenRevealed(
				AppliedRequest.TriggerRequest.SourceDefinition,
				AppliedRequest.TriggerRequest.Owner));
	}

	TestTrue(TEXT("An applied effect is recorded"),
		Tracker.TryRecordActivation(
			AppliedRequest,
			EBattleAbilityItemActivationOutcome::Applied,
			Fact,
			Error));
	TestTrue(TEXT("The first applied effect emits a fact"), Fact.IsSet());
	if (Fact.IsSet())
	{
		TestTrue(TEXT("The official public trigger reveals its definition"),
			Fact->RevealedSourceDefinition.IsSet());
		TestTrue(TEXT("The first public trigger is marked as the first reveal"),
			Fact->bFirstPublicReveal);
	}

	TestTrue(TEXT("A later applied effect is recorded"),
		Tracker.TryRecordActivation(
			AppliedRequest,
			EBattleAbilityItemActivationOutcome::Applied,
			Fact,
			Error));
	if (Fact.IsSet())
	{
		TestFalse(TEXT("A repeat trigger is not marked as a first reveal"),
			Fact->bFirstPublicReveal);
	}

	const FBattleAbilityItemHookDefinition AttemptDefinition = MakeHookDefinition(
		TEXT("RevealAttempt"),
		EBattleAbilityItemHookPoint::SwitchIn,
		EBattleAbilityItemEffectKind::Modify,
		EBattleTriggerPhase::SwitchIn,
		0,
		EBattleAbilityItemRevealPolicy::OnPublicAttempt);
	const FBattleAbilityItemHookRegistrationFacts AttemptFacts = MakeHookFacts(
		AttemptDefinition,
		601,
		TEXT("Ability.C08A.Attempt"),
		true);
	FBattleAbilityItemEffectRequest AttemptRequest;
	TestTrue(TEXT("The public-attempt fixture creates typed work"),
		FBattleAbilityItemHookContracts::TryCreateTypedEffectRequest(
			AttemptDefinition,
			MakeTriggerRequest(AttemptDefinition, AttemptFacts, 2),
			AttemptRequest,
			Error));
	TestTrue(TEXT("An official public attempt can reveal even when prevented"),
		Tracker.TryRecordActivation(
			AttemptRequest,
			EBattleAbilityItemActivationOutcome::AttemptedButPrevented,
			Fact,
			Error));
	TestTrue(TEXT("The public attempt emits a reveal fact"),
		Fact.IsSet() && Fact->RevealedSourceDefinition.IsSet());

	const FBattleAbilityItemHookDefinition NeverDefinition = MakeHookDefinition(
		TEXT("NeverReveal"),
		EBattleAbilityItemHookPoint::Speed,
		EBattleAbilityItemEffectKind::Modify,
		EBattleTriggerPhase::ActionOrderCalculation,
		0,
		EBattleAbilityItemRevealPolicy::Never);
	const FBattleAbilityItemHookRegistrationFacts NeverFacts = MakeHookFacts(
		NeverDefinition,
		602,
		TEXT("Item.C08A.Never"),
		false);
	FBattleAbilityItemEffectRequest NeverRequest;
	TestTrue(TEXT("The never-reveal fixture creates typed work"),
		FBattleAbilityItemHookContracts::TryCreateTypedEffectRequest(
			NeverDefinition,
			MakeTriggerRequest(NeverDefinition, NeverFacts, 3),
			NeverRequest,
			Error));
	TestTrue(TEXT("An applied hidden effect may emit a generic fact"),
		Tracker.TryRecordActivation(
			NeverRequest,
			EBattleAbilityItemActivationOutcome::Applied,
			Fact,
			Error));
	TestTrue(TEXT("The generic fact exists without a source definition"),
		Fact.IsSet() && !Fact->RevealedSourceDefinition.IsSet());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC10HeldItemMoveLedgerContractTest,
	"PokemonSolarus.Battle.C08C.C10HeldItemMoves.Contract.LedgerHistoryAndDirectReveal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBattleC10HeldItemMoveLedgerContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FTrainerId ConsumerTrainer = MakeC08ANumericId<FTrainerId>(1);
	const FBattlerId ConsumerBattler = MakeC08ANumericId<FBattlerId>(101);
	const FBattleHeldItemInstanceState First = MakePersistentItem(
		1, ConsumerTrainer.GetValue(), ConsumerBattler.GetValue(), TEXT("Item.C10R5.First"));
	const FBattleHeldItemInstanceState Second = MakePersistentItem(
		2, 2, 202, TEXT("Item.C10R5.Second"));
	FBattleHeldItemLedger Ledger;
	EBattleHeldItemContractError Error = EBattleHeldItemContractError::InvalidState;
	const TArray<FBattleHeldItemInstanceState> InitialStates{First, Second};
	TestTrue(TEXT("The R5 history ledger starts valid"),
		FBattleHeldItemLedger::TryCreate(InitialStates, Ledger, Error));

	auto Apply = [&Ledger, &Error](const FBattleHeldItemOperationRequest& Request,
		FBattleHeldItemOperationFact& OutFact)
	{
		return Ledger.TryApplyOperation(Request, OutFact, Error);
	};
	FBattleHeldItemOperationFact Fact;
	FBattleHeldItemOperationRequest ConsumeFirst;
	ConsumeFirst.Kind = EBattleHeldItemOperationKind::Consume;
	ConsumeFirst.PrimaryInstanceId = First.InstanceId;
	TestTrue(TEXT("The first item consumption succeeds"), Apply(ConsumeFirst, Fact));
	TestEqual(TEXT("Consumption history uses the same fact ordinal"),
		Fact.PrimaryAfter.LastConsumptionFactOrdinal, Fact.FactOrdinal);
	TestTrue(TEXT("Consumption history captures the pre-consumption holder"),
		Fact.PrimaryAfter.LastConsumerTrainerId == ConsumerTrainer
			&& Fact.PrimaryAfter.LastConsumerBattlerId == ConsumerBattler);

	FBattleHeldItemOperationRequest RestoreFirst;
	RestoreFirst.Kind = EBattleHeldItemOperationKind::Restore;
	RestoreFirst.PrimaryInstanceId = First.InstanceId;
	RestoreFirst.TargetHolderTrainerId = ConsumerTrainer;
	RestoreFirst.TargetHolderBattlerId = ConsumerBattler;
	TestTrue(TEXT("The first item restores to its last consumer"), Apply(RestoreFirst, Fact));
	TestTrue(TEXT("A repeated consumption succeeds"), Apply(ConsumeFirst, Fact));
	const uint64 RepeatedFirstOrdinal = Fact.FactOrdinal;

	FBattleHeldItemOperationRequest TransferSecond;
	TransferSecond.Kind = EBattleHeldItemOperationKind::TemporarilySteal;
	TransferSecond.PrimaryInstanceId = Second.InstanceId;
	TransferSecond.TargetHolderTrainerId = ConsumerTrainer;
	TransferSecond.TargetHolderBattlerId = ConsumerBattler;
	TestTrue(TEXT("The empty consumer can receive the second item"), Apply(TransferSecond, Fact));
	FBattleHeldItemOperationRequest ConsumeSecond;
	ConsumeSecond.Kind = EBattleHeldItemOperationKind::Consume;
	ConsumeSecond.PrimaryInstanceId = Second.InstanceId;
	TestTrue(TEXT("The second item can be consumed by the same battler"), Apply(ConsumeSecond, Fact));
	TestTrue(TEXT("The later consumption ordinal is greater"),
		Fact.FactOrdinal > RepeatedFirstOrdinal);
	const FBattleHeldItemInstanceState* MostRecent = Ledger.FindMostRecentlyConsumedBy(
		ConsumerTrainer, ConsumerBattler);
	TestTrue(TEXT("Recycle lookup selects the greatest matching consumption ordinal"),
		MostRecent != nullptr && MostRecent->InstanceId == Second.InstanceId
			&& MostRecent->LastConsumptionFactOrdinal == Fact.FactOrdinal);
	TestNull(TEXT("A battler with no consumption history has no Recycle candidate"),
		Ledger.FindMostRecentlyConsumedBy(
			MakeC08ANumericId<FTrainerId>(9), MakeC08ANumericId<FBattlerId>(909)));

	const FBattleHeldItemOperationRequest Rejected = ConsumeFirst;
	const uint64 OrdinalBeforeFailure = Fact.FactOrdinal;
	TestFalse(TEXT("Consuming an already consumed item is rejected without a fact"),
		Apply(Rejected, Fact));
	FBattleHeldItemOperationRequest RestoreSecond = RestoreFirst;
	RestoreSecond.PrimaryInstanceId = Second.InstanceId;
	TestTrue(TEXT("A later valid restore still succeeds"), Apply(RestoreSecond, Fact));
	TestEqual(TEXT("A rejected operation consumes no fact ordinal"),
		Fact.FactOrdinal, OrdinalBeforeFailure + 1);

	FBattleHeldItemInstanceState SeededOld = MakePersistentItem(
		10, 10, 1001, TEXT("Item.C10R5.SeededOld"));
	SeededOld.LastConsumerTrainerId = SeededOld.CurrentHolderTrainerId;
	SeededOld.LastConsumerBattlerId = SeededOld.CurrentHolderBattlerId;
	SeededOld.LastConsumptionFactOrdinal = 5;
	SeededOld.CurrentHolderTrainerId = FTrainerId();
	SeededOld.CurrentHolderBattlerId = FBattlerId();
	SeededOld.CurrentItemId = FItemId();
	SeededOld.bConsumed = true;
	FBattleHeldItemInstanceState SeededNew = MakePersistentItem(
		11, 11, 1101, TEXT("Item.C10R5.SeededNew"));
	SeededNew.LastConsumerTrainerId = SeededNew.CurrentHolderTrainerId;
	SeededNew.LastConsumerBattlerId = SeededNew.CurrentHolderBattlerId;
	SeededNew.LastConsumptionFactOrdinal = 12;
	SeededNew.CurrentHolderTrainerId = FTrainerId();
	SeededNew.CurrentHolderBattlerId = FBattlerId();
	SeededNew.CurrentItemId = FItemId();
	SeededNew.bConsumed = true;
	FBattleHeldItemLedger SeededLedger;
	const TArray<FBattleHeldItemInstanceState> SeededStates{SeededOld, SeededNew};
	TestTrue(TEXT("A complete seeded consumption history is accepted"),
		FBattleHeldItemLedger::TryCreate(SeededStates, SeededLedger, Error));
	FBattleHeldItemOperationRequest SeededRestore;
	SeededRestore.Kind = EBattleHeldItemOperationKind::Restore;
	SeededRestore.PrimaryInstanceId = SeededNew.InstanceId;
	SeededRestore.TargetHolderTrainerId = SeededNew.LastConsumerTrainerId;
	SeededRestore.TargetHolderBattlerId = SeededNew.LastConsumerBattlerId;
	TestTrue(TEXT("An operation after seeded history succeeds"),
		SeededLedger.TryApplyOperation(SeededRestore, Fact, Error));
	TestEqual(TEXT("The next fact ordinal starts after the greatest seeded history"),
		Fact.FactOrdinal, static_cast<uint64>(13));

	FBattleHeldItemInstanceState Partial = SeededOld;
	Partial.LastConsumerBattlerId = FBattlerId();
	FBattleHeldItemLedger RejectedLedger;
	const TArray<FBattleHeldItemInstanceState> PartialStates{Partial};
	TestFalse(TEXT("Partial consumption history is rejected"),
		FBattleHeldItemLedger::TryCreate(PartialStates, RejectedLedger, Error));
	FBattleHeldItemInstanceState Duplicate = SeededNew;
	Duplicate.LastConsumptionFactOrdinal = SeededOld.LastConsumptionFactOrdinal;
	const TArray<FBattleHeldItemInstanceState> DuplicateStates{SeededOld, Duplicate};
	TestFalse(TEXT("Duplicate nonzero consumption ordinals are rejected"),
		FBattleHeldItemLedger::TryCreate(DuplicateStates, RejectedLedger, Error));

	FBattleHeldItemInstanceState InitialConsumed = MakePersistentItem(
		20, 20, 2001, TEXT("Item.C10R5.InitialConsumed"));
	InitialConsumed.CurrentHolderTrainerId = FTrainerId();
	InitialConsumed.CurrentHolderBattlerId = FBattlerId();
	InitialConsumed.CurrentItemId = FItemId();
	InitialConsumed.bConsumed = true;
	FBattleHeldItemLedger InitialConsumedLedger;
	const TArray<FBattleHeldItemInstanceState> InitialConsumedStates{
		InitialConsumed};
	TestTrue(TEXT("A persistent item consumed before setup is accepted without battle history"),
		FBattleHeldItemLedger::TryCreate(
			InitialConsumedStates,
			InitialConsumedLedger,
			Error));
	TestNull(TEXT("A pre-setup consumption is not a Recycle history candidate"),
		InitialConsumedLedger.FindMostRecentlyConsumedBy(
			InitialConsumed.OriginalOwnerTrainerId,
			InitialConsumed.OriginalOwnerBattlerId));

	FBattleHeldItemInstanceState GeneratedWithoutHistory = MakeGeneratedItem(
		21, 21, 2101, TEXT("Item.C10R5.GeneratedWithoutHistory"));
	GeneratedWithoutHistory.CurrentHolderTrainerId = FTrainerId();
	GeneratedWithoutHistory.CurrentHolderBattlerId = FBattlerId();
	GeneratedWithoutHistory.CurrentItemId = FItemId();
	GeneratedWithoutHistory.bConsumed = true;
	const TArray<FBattleHeldItemInstanceState> GeneratedWithoutHistoryStates{
		GeneratedWithoutHistory};
	TestFalse(TEXT("A generated consumed item still requires battle history"),
		FBattleHeldItemLedger::TryCreate(
			GeneratedWithoutHistoryStates,
			RejectedLedger,
			Error));
	FBattleHeldItemInstanceState NonzeroHistoryWithoutConsumer = InitialConsumed;
	NonzeroHistoryWithoutConsumer.LastConsumptionFactOrdinal = 1;
	const TArray<FBattleHeldItemInstanceState> NonzeroHistoryWithoutConsumerStates{
		NonzeroHistoryWithoutConsumer};
	TestFalse(TEXT("A nonzero consumption ordinal still requires a consumer pair"),
		FBattleHeldItemLedger::TryCreate(
			NonzeroHistoryWithoutConsumerStates,
			RejectedLedger,
			Error));
	FBattleHeldItemInstanceState RestoredWithoutHistory = InitialConsumed;
	RestoredWithoutHistory.bRestoredAfterConsumption = true;
	const TArray<FBattleHeldItemInstanceState> RestoredWithoutHistoryStates{
		RestoredWithoutHistory};
	TestFalse(TEXT("A restored item still requires battle consumption history"),
		FBattleHeldItemLedger::TryCreate(
			RestoredWithoutHistoryStates,
			RejectedLedger,
			Error));

	FBattleAbilityItemRevealTracker Tracker;
	FBattleTriggerSourceDefinition Source;
	TestTrue(TEXT("The direct reveal source is a typed item"),
		FBattleTriggerSourceDefinition::TryCreateItem(First.DefinitionItemId, Source));
	const FBattleTriggerSubject Owner = MakeBattlerSubject(ConsumerBattler.GetValue());
	bool bFirstReveal = false;
	EBattleAbilityItemHookError RevealError = EBattleAbilityItemHookError::InvalidDefinition;
	TestTrue(TEXT("A direct public reveal is accepted"),
		Tracker.TryRecordPublicReveal(Source, Owner, bFirstReveal, RevealError));
	TestTrue(TEXT("The first direct public reveal is identified"), bFirstReveal);
	TestTrue(TEXT("The direct reveal synchronizes the tracker key"),
		Tracker.HasBeenRevealed(Source, Owner));
	TestTrue(TEXT("A repeat direct public reveal remains valid"),
		Tracker.TryRecordPublicReveal(Source, Owner, bFirstReveal, RevealError));
	TestFalse(TEXT("A repeat direct public reveal is not first"), bFirstReveal);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC08AHeldItemOperationsTest,
	"PokemonSolarus.Battle.C08A.HeldItems.TypedOwnershipOperations",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC08AHeldItemOperationsTest::RunTest(const FString& Parameters)
{
	const FBattleHeldItemInstanceState First = MakePersistentItem(
		1, 1, 11, TEXT("Item.C08A.First"));
	const FBattleHeldItemInstanceState Second = MakePersistentItem(
		2, 2, 22, TEXT("Item.C08A.Second"));
	const TArray<FBattleHeldItemInstanceState> InitialStates{First, Second};
	FBattleHeldItemLedger Ledger;
	EBattleHeldItemContractError Error = EBattleHeldItemContractError::InvalidState;
	TestTrue(TEXT("A two-owner item ledger is valid"),
		FBattleHeldItemLedger::TryCreate(InitialStates, Ledger, Error));

	FBattleHeldItemOperationFact Fact;
	FBattleHeldItemOperationRequest Suppress;
	Suppress.Kind = EBattleHeldItemOperationKind::Suppress;
	Suppress.PrimaryInstanceId = First.InstanceId;
	Suppress.bSuppressed = true;
	TestTrue(TEXT("Suppression applies only through a typed request"),
		Ledger.TryApplyOperation(Suppress, Fact, Error));
	TestTrue(TEXT("The suppression state changes"), Fact.PrimaryAfter.bSuppressed);
	TestEqual(TEXT("The first successful operation gets ordinal one"), Fact.FactOrdinal,
		static_cast<uint64>(1));

	FBattleHeldItemOperationRequest Reveal;
	Reveal.Kind = EBattleHeldItemOperationKind::Reveal;
	Reveal.PrimaryInstanceId = First.InstanceId;
	TestTrue(TEXT("Reveal applies only through a typed request"),
		Ledger.TryApplyOperation(Reveal, Fact, Error));
	TestTrue(TEXT("The item reveal state changes"), Fact.PrimaryAfter.bRevealed);

	FBattleHeldItemOperationRequest Swap;
	Swap.Kind = EBattleHeldItemOperationKind::Swap;
	Swap.PrimaryInstanceId = First.InstanceId;
	Swap.SecondaryInstanceId = Second.InstanceId;
	TestTrue(TEXT("Swap applies atomically"), Ledger.TryApplyOperation(Swap, Fact, Error));
	TestTrue(TEXT("Swap emits both before/after records"),
		Fact.SecondaryBefore.IsSet() && Fact.SecondaryAfter.IsSet());
	TestEqual(TEXT("The first item now has the second holder"),
		Fact.PrimaryAfter.CurrentHolderBattlerId.GetValue(), static_cast<uint64>(22));

	const TArray<FBattleHeldItemInstanceState> BeforeRejectedSteal = CopyLedgerStates(Ledger);
	FBattleHeldItemOperationRequest OccupiedSteal;
	OccupiedSteal.Kind = EBattleHeldItemOperationKind::TemporarilySteal;
	OccupiedSteal.PrimaryInstanceId = First.InstanceId;
	OccupiedSteal.TargetHolderTrainerId = MakeC08ANumericId<FTrainerId>(1);
	OccupiedSteal.TargetHolderBattlerId = MakeC08ANumericId<FBattlerId>(11);
	TestFalse(TEXT("A holder cannot transiently own two items"),
		Ledger.TryApplyOperation(OccupiedSteal, Fact, Error));
	TestEqual(TEXT("The occupied-holder failure is typed"), Error,
		EBattleHeldItemContractError::HolderOccupied);
	const TArray<FBattleHeldItemInstanceState> AfterRejectedSteal = CopyLedgerStates(Ledger);
	TestEqual(TEXT("A failed operation preserves the state count"),
		AfterRejectedSteal.Num(), BeforeRejectedSteal.Num());
	for (int32 Index = 0; Index < BeforeRejectedSteal.Num(); ++Index)
	{
		TestTrue(TEXT("A failed operation is atomic"),
			AfterRejectedSteal[Index] == BeforeRejectedSteal[Index]);
	}

	FBattleHeldItemOperationRequest Remove;
	Remove.Kind = EBattleHeldItemOperationKind::Remove;
	Remove.PrimaryInstanceId = Second.InstanceId;
	TestTrue(TEXT("Temporary removal frees the current holder"),
		Ledger.TryApplyOperation(Remove, Fact, Error));
	TestTrue(TEXT("Removal preserves the item identity for battle-end reset"),
		Fact.PrimaryAfter.CurrentItemId.IsValid());
	TestFalse(TEXT("A removed item has no current holder"),
		Fact.PrimaryAfter.CurrentHolderBattlerId.IsValid());

	TestTrue(TEXT("Steal succeeds after the target holder is free"),
		Ledger.TryApplyOperation(OccupiedSteal, Fact, Error));
	TestEqual(TEXT("Steal records the new transient holder"),
		Fact.PrimaryAfter.CurrentHolderBattlerId.GetValue(), static_cast<uint64>(11));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC08AHeldItemFinalFactsTest,
	"PokemonSolarus.Battle.C08A.HeldItems.ConsumeRestoreCaptureAndGeneratedFinalFacts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC08AHeldItemFinalFactsTest::RunTest(const FString& Parameters)
{
	const FBattleHeldItemInstanceState Restored = MakePersistentItem(
		1, 1, 101, TEXT("Item.C08A.Restored"));
	const FBattleHeldItemInstanceState Captured = MakePersistentItem(
		2, 2, 202, TEXT("Item.C08A.Captured"));
	const FBattleHeldItemInstanceState Consumed = MakePersistentItem(
		3, 3, 303, TEXT("Item.C08A.Consumed"));
	const FBattleHeldItemInstanceState Generated = MakeGeneratedItem(
		4, 4, 404, TEXT("Item.C08A.Generated"));
	const TArray<FBattleHeldItemInstanceState> InitialStates{
		Generated,
		Consumed,
		Captured,
		Restored};

	FBattleHeldItemLedger Ledger;
	EBattleHeldItemContractError Error = EBattleHeldItemContractError::InvalidState;
	TestTrue(TEXT("The finalization ledger is valid"),
		FBattleHeldItemLedger::TryCreate(InitialStates, Ledger, Error));

	auto Apply = [&Ledger, &Error, this](const FBattleHeldItemOperationRequest& Request)
	{
		FBattleHeldItemOperationFact Fact;
		return Ledger.TryApplyOperation(Request, Fact, Error);
	};

	FBattleHeldItemOperationRequest ConsumeRestored;
	ConsumeRestored.Kind = EBattleHeldItemOperationKind::Consume;
	ConsumeRestored.PrimaryInstanceId = Restored.InstanceId;
	TestTrue(TEXT("The first persistent item is consumed"), Apply(ConsumeRestored));

	FBattleHeldItemOperationRequest RestoreRequest;
	RestoreRequest.Kind = EBattleHeldItemOperationKind::Restore;
	RestoreRequest.PrimaryInstanceId = Restored.InstanceId;
	RestoreRequest.TargetHolderTrainerId = Restored.OriginalOwnerTrainerId;
	RestoreRequest.TargetHolderBattlerId = Restored.OriginalOwnerBattlerId;
	TestTrue(TEXT("Recycle-style restoration is explicit"), Apply(RestoreRequest));

	FBattleHeldItemOperationRequest RemoveRestored;
	RemoveRestored.Kind = EBattleHeldItemOperationKind::Remove;
	RemoveRestored.PrimaryInstanceId = Restored.InstanceId;
	TestTrue(TEXT("Temporary removal does not erase restoration"), Apply(RemoveRestored));

	FBattleHeldItemOperationRequest StealCaptured;
	StealCaptured.Kind = EBattleHeldItemOperationKind::TemporarilySteal;
	StealCaptured.PrimaryInstanceId = Captured.InstanceId;
	StealCaptured.TargetHolderTrainerId = Restored.OriginalOwnerTrainerId;
	StealCaptured.TargetHolderBattlerId = Restored.OriginalOwnerBattlerId;
	TestTrue(TEXT("The captured owner's item may be transiently stolen"), Apply(StealCaptured));

	FBattleHeldItemOperationRequest ConsumePermanent;
	ConsumePermanent.Kind = EBattleHeldItemOperationKind::Consume;
	ConsumePermanent.PrimaryInstanceId = Consumed.InstanceId;
	TestTrue(TEXT("Normal consumption is recorded"), Apply(ConsumePermanent));

	TArray<FBattleFinalHeldItemFact> FinalFacts;
	const FBattlerId CapturedOwner = Captured.OriginalOwnerBattlerId;
	const TArray<FBattlerId> CapturedOwners{CapturedOwner};
	TestTrue(TEXT("Final facts build without an inventory write"),
		Ledger.TryBuildFinalFacts(CapturedOwners, FinalFacts, Error));
	TestEqual(TEXT("Every item instance emits one canonical final fact"), FinalFacts.Num(), 4);
	if (FinalFacts.Num() == 4)
	{
		TestEqual(TEXT("Recycle restoration returns the original item"),
			FinalFacts[0].Disposition, EBattleHeldItemFinalDisposition::OriginalOwner);
		TestTrue(TEXT("The restoration fact remains explicit"),
			FinalFacts[0].bRestoredAfterConsumption);
		TestEqual(TEXT("A captured Pokemon keeps its original item despite transient theft"),
			FinalFacts[1].Disposition,
			EBattleHeldItemFinalDisposition::CapturedOriginalOwner);
		TestEqual(TEXT("A normally consumed item remains consumed"),
			FinalFacts[2].Disposition, EBattleHeldItemFinalDisposition::Consumed);
		TestFalse(TEXT("A consumed item has no final item identity"),
			FinalFacts[2].FinalItemId.IsValid());
		TestEqual(TEXT("A battle-generated item disappears"),
			FinalFacts[3].Disposition,
			EBattleHeldItemFinalDisposition::BattleGeneratedRemoved);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC08ABagOwnershipTest,
	"PokemonSolarus.Battle.C08A.Bags.SeparateOwnershipQuotaAndResources",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC08ABagOwnershipTest::RunTest(const FString& Parameters)
{
	const FTrainerId PlayerTrainer = MakeC08ANumericId<FTrainerId>(1);
	const FTrainerId PartnerTrainer = MakeC08ANumericId<FTrainerId>(2);
	const FItemId PlayerItem = MakeNamedId<FItemId>(TEXT("Item.C08A.PlayerBag"));
	const FItemId PartnerItem = MakeNamedId<FItemId>(TEXT("Item.C08A.PartnerBag"));

	FBattleTrainerBagState PlayerBag;
	PlayerBag.TrainerId = PlayerTrainer;
	PlayerBag.Items.Add({PlayerItem, 2});
	FBattleTrainerBagState PartnerBag;
	PartnerBag.TrainerId = PartnerTrainer;
	PartnerBag.Items.Add({PartnerItem, 1});
	const TArray<FBattleTrainerBagState> InitialBags{PartnerBag, PlayerBag};

	FBattleBagOwnershipContract Contract;
	EBattleBagContractError Error = EBattleBagContractError::InvalidState;
	TestTrue(TEXT("Separate player and partner Bag snapshots are valid"),
		FBattleBagOwnershipContract::TryCreate(InitialBags, Contract, Error));
	TestEqual(TEXT("Trainer Bags are canonicalized by owner"),
		Contract.GetTrainerStates()[0].TrainerId.GetValue(), static_cast<uint64>(1));

	FBattleBagUseRequest Request;
	Request.ActingTrainerId = PlayerTrainer;
	Request.ItemId = PlayerItem;
	Request.TargetOwnerTrainerId = PartnerTrainer;
	Request.bItemSpecificTargetLegal = true;
	FBattleBagUseResult Result;
	TestTrue(TEXT("Wrong-owner targeting is a valid pre-use rejection"),
		Contract.TryApplyUse(Request, Result, Error));
	TestEqual(TEXT("The wrong-owner rejection is typed"), Result.RejectionReason,
		EBattleBagUseRejectionReason::WrongTargetOwner);
	TestFalse(TEXT("A pre-use rejection consumes no item"), Result.bItemConsumed);
	TestFalse(TEXT("A pre-use rejection consumes no action"), Result.bActionConsumed);

	Request.TargetOwnerTrainerId = PlayerTrainer;
	Request.bItemSpecificTargetLegal = false;
	TestTrue(TEXT("Item-specific illegality is a pre-use rejection"),
		Contract.TryApplyUse(Request, Result, Error));
	TestEqual(TEXT("The item-specific rejection is typed"), Result.RejectionReason,
		EBattleBagUseRejectionReason::IllegalItemTarget);

	Request.bItemSpecificTargetLegal = true;
	Request.bEffectPreventedAfterLegalUse = true;
	TestTrue(TEXT("A legal prevented use resolves"),
		Contract.TryApplyUse(Request, Result, Error));
	TestEqual(TEXT("The prevented-after-use outcome is explicit"), Result.Outcome,
		EBattleBagUseOutcome::EffectPreventedAfterLegalUse);
	TestTrue(TEXT("A legal prevented use consumes the item"), Result.bItemConsumed);
	TestTrue(TEXT("A legal prevented use consumes the action"), Result.bActionConsumed);
	TestEqual(TEXT("Only the acting Trainer's count changes"), Result.CountAfter, 1);

	Request.bEffectPreventedAfterLegalUse = false;
	TestTrue(TEXT("A second same-turn use is evaluated"),
		Contract.TryApplyUse(Request, Result, Error));
	TestEqual(TEXT("One Bag action per Trainer is enforced"), Result.RejectionReason,
		EBattleBagUseRejectionReason::BagQuotaUsed);
	TestEqual(TEXT("The quota rejection preserves the count"), Result.CountAfter, 1);

	Contract.ResetTurnQuotas();
	TestTrue(TEXT("The next turn permits the player's remaining item"),
		Contract.TryApplyUse(Request, Result, Error));
	TestEqual(TEXT("The legal use applies"), Result.Outcome, EBattleBagUseOutcome::Applied);
	TestEqual(TEXT("The player's copied count reaches zero"), Result.CountAfter, 0);

	FBattleBagUseRequest PartnerRequest;
	PartnerRequest.ActingTrainerId = PartnerTrainer;
	PartnerRequest.ItemId = PartnerItem;
	PartnerRequest.TargetOwnerTrainerId = PartnerTrainer;
	PartnerRequest.bItemSpecificTargetLegal = true;
	TestTrue(TEXT("The partner retains a separate Bag and quota"),
		Contract.TryApplyUse(PartnerRequest, Result, Error));
	TestEqual(TEXT("The partner's own item is consumed"), Result.CountAfter, 0);

	const FBattleTrainerBagState* FinalPlayerBag = Contract.FindTrainerState(PlayerTrainer);
	const FBattleTrainerBagState* FinalPartnerBag = Contract.FindTrainerState(PartnerTrainer);
	TestNotNull(TEXT("The final player Bag snapshot remains queryable"), FinalPlayerBag);
	TestNotNull(TEXT("The final partner Bag snapshot remains queryable"), FinalPartnerBag);
	if (FinalPlayerBag != nullptr && FinalPartnerBag != nullptr)
	{
		TestEqual(TEXT("The player and partner counts stayed separate"),
			FinalPlayerBag->Items[0].Count, 0);
		TestEqual(TEXT("The partner consumed only the partner item"),
			FinalPartnerBag->Items[0].Count, 0);
	}

	return true;
}

#endif
