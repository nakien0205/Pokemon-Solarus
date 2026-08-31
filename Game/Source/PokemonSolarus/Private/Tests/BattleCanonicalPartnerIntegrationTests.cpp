#if WITH_DEV_AUTOMATION_TESTS

#include "BattleCanonicalIntegrationTestSupport.h"

#include "Misc/AutomationTest.h"

namespace BattleCanonicalPartnerIntegrationPrivate
{
using namespace BattleCanonicalIntegrationTestSupport;

FSetupSpec MakePartnerSpec(const uint64 BattleValue, const int32 PlayerHP = INDEX_NONE, const int32 OpponentHP = INDEX_NONE, const bool bReserves = true)
{
	FSetupSpec Spec;
	Spec.BattleValue = BattleValue;
	Spec.EncounterKind = EBattleEncounterKind::Trainer;
	Spec.Format = EBattleFormat::PartnerDouble;
	Spec.Policies.bBagAllowed = true;
	Spec.Policies.bRunAllowed = false;
	Spec.Policies.bCaptureAllowed = false;
	Spec.Policies.bShiftPromptEligible = false;
	Spec.Trainers = {{1, EBattleSide::Player, EBattleTrainerRole::Player, EBattleDecisionController::Human, {{TEXT("Item.HyperPotion"), 1}, {TEXT("Item.PokeBall"), 1}}},
		{3, EBattleSide::Player, EBattleTrainerRole::Partner, EBattleDecisionController::PartnerAI, {{TEXT("Item.FullHeal"), 1}, {TEXT("Item.HyperPotion"), 2}, {TEXT("Item.PokeBall"), 1}}},
		{2, EBattleSide::Opponent, EBattleTrainerRole::Opponent, EBattleDecisionController::EnemyAI, {}}};
	FBattlerSpec Player{1, 11, 0, TEXT("Species.Clefable"), TEXT("Nature.Hardy"), TEXT("Ability.MagicGuard"), {TEXT("Move.SwordsDance"), TEXT("Move.Protect")}};
	Player.CurrentHP = PlayerHP;
	FBattlerSpec Partner{3, 31, 0, TEXT("Species.Espathra"), TEXT("Nature.Timid"), TEXT("Ability.SpeedBoost"), {TEXT("Move.Swift"), TEXT("Move.HelpingHand"), TEXT("Move.Protect")}};
	Partner.EffortValues.Speed = 252;
	FBattlerSpec OpponentLeft{2, 21, 0, TEXT("Species.Venusaur"), TEXT("Nature.Hardy"), TEXT("Ability.Overgrow"), {TEXT("Move.QuickAttack"), TEXT("Move.SwordsDance"), TEXT("Move.WillOWisp")}};
	FBattlerSpec OpponentRight{2, 22, 1, TEXT("Species.Pelipper"), TEXT("Nature.Hardy"), TEXT("Ability.Drizzle"), {TEXT("Move.SwordsDance"), TEXT("Move.Protect")}};
	OpponentLeft.CurrentHP = OpponentHP;
	OpponentRight.CurrentHP = OpponentHP;
	Spec.Battlers = {Player, Partner, OpponentLeft, OpponentRight};
	Spec.Active = {{EBattleSide::Player, EBattlePosition::Left, 1, 11}, {EBattleSide::Player, EBattlePosition::Right, 3, 31},
		{EBattleSide::Opponent, EBattlePosition::Left, 2, 21}, {EBattleSide::Opponent, EBattlePosition::Right, 2, 22}};
	if (bReserves)
	{
		Spec.Battlers.Add({1, 12, 1, TEXT("Species.Charizard"), TEXT("Nature.Jolly"), TEXT("Ability.Blaze"), {TEXT("Move.Flamethrower")}});
		Spec.Battlers.Add({3, 32, 2, TEXT("Species.Gyarados"), TEXT("Nature.Adamant"), TEXT("Ability.Intimidate"), {TEXT("Move.Bite")}});
		Spec.Battlers.Add({2, 23, 2, TEXT("Species.Rotom"), TEXT("Nature.Calm"), TEXT("Ability.Levitate"), {TEXT("Move.Thunder")}});
	}
	return Spec;
}

bool Build(FAutomationTestBase& Test, const FSetupSpec& Spec, FCatalogFixture& Fixture, FBattleSetup& Setup)
{
	FString Error;
	if (!TryLoadProductionFixture(Test, Fixture, Error) || !TryBuildSetup(Fixture.Catalog, Spec, Setup, Error))
	{
		Test.AddError(Error); return false;
	}
	return true;
}

FChoice Fight(const TCHAR* Move, EBattleSide Side = EBattleSide::Opponent, EBattlePosition Position = EBattlePosition::Left)
{
	FChoice Choice; Choice.DefinitionId = FName(Move); Choice.ActiveTarget = MakeActiveSlotId(Side, Position); return Choice;
}

bool Finish(FBattleEngine& Engine, FRunEvidence& Evidence, FString& Error)
{
	if (!ExecuteLockedQueue(Engine, Evidence, Error)) return false;
	while (!Engine.GetPendingDecisionRequests().IsEmpty())
	{
		const FChoiceProvider Replacement = [](const FBattleDecisionRequest& Request, FChoice& Choice, FString& OutError)
		{
			const TConstArrayView<FPartySlotId> LegalSwitches = Request.GetLegalSwitchPartySlots();
			if (LegalSwitches.IsEmpty())
			{
				OutError = FString::Printf(TEXT("Mandatory replacement for trainer %llu has no legal switch slot."), Request.GetDecisionOwnerTrainerId().GetValue());
				return false;
			}
			Choice.Kind = EChoiceKind::Replacement; Choice.PartyTarget = LegalSwitches[0]; return true;
		};
		if (!SubmitPendingChoices(Engine, Replacement, Evidence, Error) || !ExecuteLockedQueue(Engine, Evidence, Error)) return false;
	}
	return Engine.GetSnapshot().GetPhase() == EBattlePhase::EndOfTurn ? ResolveEndTurn(Engine, Evidence, Error) : true;
}

const FBattleEvent* FindSourceEvent(
	const FBattleReplayRecord& Record,
	const EBattleEventType Type,
	const uint64 BattlerValue)
{
	for (const FBattleResolution& Resolution : Record.GetResolutions())
		for (const FBattleEvent& Event : Resolution.GetEvents())
			if (Event.GetType() == Type
				&& Event.GetSource().BattlerId == MakeNumericId<FBattlerId>(BattlerValue)) return &Event;
	return nullptr;
}

int32 CountSourceEvents(
	const FBattleReplayRecord& Record,
	const EBattleEventType Type,
	const uint64 BattlerValue)
{
	int32 Count = 0;
	for (const FBattleResolution& Resolution : Record.GetResolutions())
		for (const FBattleEvent& Event : Resolution.GetEvents())
			Count += Event.GetType() == Type
				&& Event.GetSource().BattlerId == MakeNumericId<FBattlerId>(BattlerValue) ? 1 : 0;
	return Count;
}

FBattlerId ActiveBattler(
	const FBattleSnapshot& Snapshot,
	const EBattleSide Side,
	const EBattlePosition Position)
{
	const FActiveSlotId Slot = MakeActiveSlotId(Side, Position);
	const FBattleActiveAssignment* Assignment = Snapshot.GetActiveAssignments().FindByPredicate(
		[Slot](const FBattleActiveAssignment& Value) { return Value.ActiveSlotId == Slot; });
	return Assignment == nullptr ? FBattlerId() : Assignment->BattlerId;
}

int32 BagCount(
	const FBattleSnapshot& Snapshot,
	const uint64 TrainerValue,
	const TCHAR* ItemName)
{
	const FTrainerId TrainerId = MakeNumericId<FTrainerId>(TrainerValue);
	const FItemId ItemId = MakeDefinitionId<FItemId>(ItemName);
	for (const FBattleTrainerSetup& Trainer : Snapshot.GetTrainers())
		if (Trainer.TrainerId == TrainerId)
			for (const FBattleBagItemCount& Item : Trainer.Bag)
				if (Item.ItemId == ItemId) return Item.Count;
	return INDEX_NONE;
}

bool RejectWithoutMutation(
	FBattleEngine& Engine,
	const FBattleDecision& Decision,
	const EBattleRejectionReason ExpectedReason,
	FString& OutError)
{
	const FString SnapshotBefore = SnapshotSignature(Engine.GetSnapshot());
	const int32 TraceBefore = Engine.ExportRandomTrace().Num();
	const int32 RequestsBefore = Engine.GetPendingDecisionRequests().Num();
	const FBattleReplayRecord ReplayBefore = Engine.ExportReplayRecord();
	const FBattleResolution Rejected = Engine.SubmitDecision(Decision);
	const FBattleReplayRecord ReplayAfter = Engine.ExportReplayRecord();
	if (Rejected.WasAccepted()
		|| Rejected.GetRejection().Reason != ExpectedReason
		|| Rejected.GetBeforeStateVersion() != Rejected.GetAfterStateVersion()
		|| SnapshotSignature(Engine.GetSnapshot()) != SnapshotBefore
		|| Engine.ExportRandomTrace().Num() != TraceBefore
		|| Engine.GetPendingDecisionRequests().Num() != RequestsBefore
		|| Rejected.GetEvents().Num() != 1
		|| Rejected.GetEvents()[0].GetType() != EBattleEventType::DecisionRejected
		|| ReplayAfter.GetInputs().Decisions.Num() != ReplayBefore.GetInputs().Decisions.Num() + 1
		|| ReplayAfter.GetResolutions().Num() != ReplayBefore.GetResolutions().Num() + 1)
	{
		OutError = TEXT("Partner ownership rejection changed state, RNG, pending requests, resources, or its exact audit delta.");
		return false;
	}
	return true;
}
} // namespace BattleCanonicalPartnerIntegrationPrivate

using namespace BattleCanonicalPartnerIntegrationPrivate;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FC11APartnerSelection, "PokemonSolarus.Battle.C11A.WildPartner.Partner.SelectionOrderVisibilityAndAllyTargeting", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FC11APartnerSelection::RunTest(const FString& Parameters)
{
	FCatalogFixture Fixture; FBattleSetup Setup;
	if (!Build(*this, MakePartnerSpec(11601), Fixture, Setup)) return false;
	const FChoiceProvider Provider = [](const FBattleDecisionRequest& Request, FChoice& Choice, FString&)
	{
		const uint64 Owner = Request.GetDecisionOwnerTrainerId().GetValue();
		if (Owner == 1) Choice = Fight(TEXT("Move.Protect"));
		else if (Owner == 3) Choice = Fight(TEXT("Move.HelpingHand"), EBattleSide::Player, EBattlePosition::Left);
		else if (Request.GetActingBattlerId() == MakeNumericId<FBattlerId>(21))
			Choice = Fight(TEXT("Move.QuickAttack"), EBattleSide::Player, EBattlePosition::Left);
		else Choice = Fight(TEXT("Move.Protect"));
		return true;
	};
	const FDriveFunction Drive = [Provider](FBattleEngine& Engine, FRunEvidence& Evidence, FString& Error)
	{
		return LockTurn(Engine, Provider, Evidence, Error) && Finish(Engine, Evidence, Error);
	};
	FRunEvidence Evidence;
	const bool bTwins = RunDeterministicTwins(*this, TEXT("partner selector ordering"), Fixture.Catalog, Setup, Drive, &Evidence);
	TArray<uint64> OwnerOrder;
	for (const FBattleResolution& Resolution : Evidence.Replay.GetResolutions())
	{
		for (const FBattleEvent& Event : Resolution.GetEvents())
		{
			if (Event.GetType() == EBattleEventType::DecisionAccepted && Event.GetSource().TrainerId.IsValid()) OwnerOrder.Add(Event.GetSource().TrainerId.GetValue());
		}
	}
	TestTrue(TEXT("Visible selection order is exactly player, partner, enemy Left, enemy Right"),
		OwnerOrder == TArray<uint64>{1, 3, 2, 2});
	const FString PlayerSelection = FString::Printf(TEXT("V:1/11/%u/Move.Protect\n"),
		static_cast<uint8>(EBattleActionKind::Fight));
	TestTrue(TEXT("Filtered selector observations reveal player choice only to the partner before enemy selection"),
		Evidence.SelectorObservations.Num() == 4
		&& !Evidence.SelectorObservations[0].Contains(TEXT("V:"))
		&& Evidence.SelectorObservations[1].Contains(PlayerSelection)
		&& !Evidence.SelectorObservations[2].Contains(TEXT("V:"))
		&& !Evidence.SelectorObservations[3].Contains(TEXT("V:")));
	const FBattleEvent* EnemyLeftMove = FindSourceEvent(
		Evidence.Replay, EBattleEventType::MoveUsed, 21);
	const FBattleEvent* EnemyRightMove = FindSourceEvent(
		Evidence.Replay, EBattleEventType::MoveUsed, 22);
	TestTrue(TEXT("Each enemy decision originates from its exact filtered legal-action request"),
		EnemyLeftMove != nullptr
		&& EnemyLeftMove->GetSource().DefinitionId
			== MakeDefinitionId<FDefinitionId>(TEXT("Move.QuickAttack"))
		&& EnemyRightMove != nullptr
		&& EnemyRightMove->GetSource().DefinitionId
			== MakeDefinitionId<FDefinitionId>(TEXT("Move.Protect")));
	const FBattleEvent* Support = FindSourceEvent(
		Evidence.Replay, EBattleEventType::ActionPowerModifierRegistered, 31);
	TestTrue(TEXT("Partner Helping Hand publicly registers on the exact player ally"),
		Support != nullptr
		&& Support->GetSource().TrainerId == MakeNumericId<FTrainerId>(3)
		&& Support->GetSource().DefinitionId == MakeDefinitionId<FDefinitionId>(TEXT("Move.HelpingHand"))
		&& Support->GetTargets().Num() == 1
		&& Support->GetTargets()[0].TrainerId == MakeNumericId<FTrainerId>(1)
		&& Support->GetTargets()[0].BattlerId == MakeNumericId<FBattlerId>(11)
		&& Support->GetNumericBefore() == TOptional<int64>(0)
		&& Support->GetNumericAfter() == TOptional<int64>(1)
		&& Support->GetNumericDelta() == TOptional<int64>(1));
	return bTwins
		&& ValidateGlobalInvariants(*this, Fixture.Catalog, Evidence, TEXT("partner selector ordering"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FC11APartnerOwnership, "PokemonSolarus.Battle.C11A.WildPartner.Partner.OwnershipBagSwitchCaptureAndCrossOwnerRejections", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FC11APartnerOwnership::RunTest(const FString& Parameters)
{
	FCatalogFixture Fixture; FBattleSetup Setup;
	FSetupSpec OwnershipSpec = MakePartnerSpec(11602, 1);
	FBattlerSpec* PartnerSpec = OwnershipSpec.Battlers.FindByPredicate(
		[](const FBattlerSpec& Battler) { return Battler.BattlerValue == 31; });
	if (PartnerSpec == nullptr)
	{
		AddError(TEXT("Partner ownership fixture is missing battler 31."));
		return false;
	}
	PartnerSpec->CurrentHP = 1;
	if (!Build(*this, OwnershipSpec, Fixture, Setup)) return false;
	const FDriveFunction Drive = [](FBattleEngine& Engine, FRunEvidence& Evidence, FString& Error)
	{
		FBattleRejection Rejection;
		if (!Engine.TryBeginActionDecisionSequence(Rejection))
		{
			Error = TEXT("Partner ownership probe could not begin.");
			return false;
		}
		RecordCheckpoint(Engine, Evidence, TEXT("ownership-player-selection"));
		TArray<FBattleDecisionRequest> Requests = Engine.GetPendingDecisionRequests();
		const FItemId BallId = MakeDefinitionId<FItemId>(TEXT("Item.PokeBall"));
		if (Requests.Num() != 1
			|| Requests[0].GetDecisionOwnerTrainerId() != MakeNumericId<FTrainerId>(1)
			|| !Requests[0].GetLegalSwitchPartySlots().Contains(MakePartySlotId(1))
			|| Requests[0].GetLegalSwitchPartySlots().Contains(MakePartySlotId(2))
			|| Requests[0].GetLegalItemIds().Contains(BallId)
			|| !Requests[0].GetUnavailableOptions().ContainsByPredicate([BallId](const FBattleUnavailableDecisionOption& Option)
			{
				return Option.Kind == EBattleDecisionOptionKind::Item
					&& Option.ItemId == BallId
					&& Option.Reason == EBattleOptionUnavailableReason::CaptureRestricted;
			}))
		{
			Error = TEXT("Player ownership request does not expose the exact switch and capture restrictions.");
			return false;
		}
		auto RejectChoice = [&Engine, &Error](const FBattleDecisionRequest& Request, const FChoice& Choice, const EBattleRejectionReason Reason)
		{
			FBattleDecision Decision;
			return TryMakeDecision(Request, Choice, Decision, Error)
				&& RejectWithoutMutation(Engine, Decision, Reason, Error);
		};
		FChoice PlayerPartnerItem; PlayerPartnerItem.Kind = EChoiceKind::Bag;
		PlayerPartnerItem.DefinitionId = TEXT("Item.FullHeal"); PlayerPartnerItem.PartyTarget = MakePartySlotId(0);
		FChoice PlayerCrossTarget; PlayerCrossTarget.Kind = EChoiceKind::Bag;
		PlayerCrossTarget.DefinitionId = TEXT("Item.HyperPotion"); PlayerCrossTarget.PartyTarget = MakePartySlotId(2);
		FChoice PlayerCrossSwitch; PlayerCrossSwitch.Kind = EChoiceKind::Switch; PlayerCrossSwitch.PartyTarget = MakePartySlotId(2);
		FChoice PlayerCapture; PlayerCapture.Kind = EChoiceKind::Bag; PlayerCapture.DefinitionId = TEXT("Item.PokeBall");
		PlayerCapture.ActiveTarget = MakeActiveSlotId(EBattleSide::Opponent, EBattlePosition::Left);
		if (!RejectChoice(Requests[0], PlayerPartnerItem, EBattleRejectionReason::IllegalItem)
			|| !RejectChoice(Requests[0], PlayerCrossTarget, EBattleRejectionReason::IllegalTarget)
			|| !RejectChoice(Requests[0], PlayerCrossSwitch, EBattleRejectionReason::IllegalSwitch)
			|| !RejectChoice(Requests[0], PlayerCapture, EBattleRejectionReason::IllegalItem)) return false;
		FChoice PlayerHeal; PlayerHeal.Kind = EChoiceKind::Bag;
		PlayerHeal.DefinitionId = TEXT("Item.HyperPotion"); PlayerHeal.PartyTarget = MakePartySlotId(0);
		FBattleDecision PlayerDecision;
		if (!TryMakeDecision(Requests[0], PlayerHeal, PlayerDecision, Error)
			|| !Engine.SubmitDecision(PlayerDecision).WasAccepted())
		{
			Error = TEXT("Player legal Bag decision was not accepted after rejection probes.");
			return false;
		}
		RecordCheckpoint(Engine, Evidence, TEXT("ownership-player-accepted"));

		Requests = Engine.GetPendingDecisionRequests();
		if (Requests.Num() != 1
			|| Requests[0].GetDecisionOwnerTrainerId() != MakeNumericId<FTrainerId>(3)
			|| !Requests[0].GetLegalSwitchPartySlots().Contains(MakePartySlotId(2))
			|| Requests[0].GetLegalSwitchPartySlots().Contains(MakePartySlotId(1))
			|| Requests[0].GetLegalItemIds().Contains(BallId)
			|| !Requests[0].GetUnavailableOptions().ContainsByPredicate([BallId](const FBattleUnavailableDecisionOption& Option)
			{
				return Option.Kind == EBattleDecisionOptionKind::Item
					&& Option.ItemId == BallId
					&& Option.Reason == EBattleOptionUnavailableReason::CaptureRestricted;
			}))
		{
			Error = TEXT("Partner ownership request does not expose its exact switch and capture restrictions.");
			return false;
		}
		FChoice PartnerCrossTarget; PartnerCrossTarget.Kind = EChoiceKind::Bag;
		PartnerCrossTarget.DefinitionId = TEXT("Item.HyperPotion"); PartnerCrossTarget.PartyTarget = MakePartySlotId(1);
		FChoice PartnerCrossSwitch; PartnerCrossSwitch.Kind = EChoiceKind::Switch; PartnerCrossSwitch.PartyTarget = MakePartySlotId(1);
		FChoice PartnerCapture; PartnerCapture.Kind = EChoiceKind::Bag; PartnerCapture.DefinitionId = TEXT("Item.PokeBall");
		PartnerCapture.ActiveTarget = MakeActiveSlotId(EBattleSide::Opponent, EBattlePosition::Left);
		if (!RejectChoice(Requests[0], PartnerCrossTarget, EBattleRejectionReason::IllegalTarget)
			|| !RejectChoice(Requests[0], PartnerCrossSwitch, EBattleRejectionReason::IllegalSwitch)
			|| !RejectChoice(Requests[0], PartnerCapture, EBattleRejectionReason::IllegalItem)) return false;
		FChoice PartnerSwitch; PartnerSwitch.Kind = EChoiceKind::Switch; PartnerSwitch.PartyTarget = MakePartySlotId(2);
		FBattleDecision PartnerDecision;
		if (!TryMakeDecision(Requests[0], PartnerSwitch, PartnerDecision, Error)
			|| !Engine.SubmitDecision(PartnerDecision).WasAccepted())
		{
			Error = TEXT("Partner legal switch was not accepted after rejection probes.");
			return false;
		}
		RecordCheckpoint(Engine, Evidence, TEXT("ownership-partner-accepted"));
		const FChoiceProvider Enemy = [](const FBattleDecisionRequest&, FChoice& Choice, FString&)
		{
			Choice = Fight(TEXT("Move.SwordsDance"));
			return true;
		};
		if (!SubmitPendingChoices(Engine, Enemy, Evidence, Error)) return false;
		return Finish(Engine, Evidence, Error);
	};
	FRunEvidence Evidence;
	const bool bTwins = RunDeterministicTwins(*this, TEXT("separate partner owners"), Fixture.Catalog, Setup, Drive, &Evidence);
	const FBattleSnapshot& Final = Evidence.Replay.GetFinalSnapshot();
	TestTrue(TEXT("Seven cross-owner and capture probes publish exact immutable rejections"),
		CountEvents(Evidence.Replay, EBattleEventType::DecisionRejected) == 7);
	TestTrue(TEXT("Only the player's Hyper Potion is consumed from separate Bag ownership"),
		CountEvents(Evidence.Replay, EBattleEventType::ItemConsumed) == 1
		&& BagCount(Final, 1, TEXT("Item.HyperPotion")) == 0
		&& BagCount(Final, 1, TEXT("Item.PokeBall")) == 1
		&& BagCount(Final, 3, TEXT("Item.FullHeal")) == 1
		&& BagCount(Final, 3, TEXT("Item.HyperPotion")) == 2
		&& BagCount(Final, 3, TEXT("Item.PokeBall")) == 1);
	TestTrue(TEXT("Partner alone switches to its slot-two reserve"),
		CountEvents(Evidence.Replay, EBattleEventType::Switched) == 1
		&& ActiveBattler(Final, EBattleSide::Player, EBattlePosition::Left)
			== MakeNumericId<FBattlerId>(11)
		&& ActiveBattler(Final, EBattleSide::Player, EBattlePosition::Right)
			== MakeNumericId<FBattlerId>(32));
	return bTwins
		&& ValidateGlobalInvariants(*this, Fixture.Catalog, Evidence, TEXT("separate partner owners"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FC11APartnerContinuation, "PokemonSolarus.Battle.C11A.WildPartner.Partner.ContinuationTeamVictoryAndPlayerRecovery", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FC11APartnerContinuation::RunTest(const FString& Parameters)
{
	FCatalogFixture Fixture; FBattleSetup Setup;
	if (!Build(*this, MakePartnerSpec(11603, 1, 1, false), Fixture, Setup)) return false;
	int32 Turn = 0;
	const FChoiceProvider Provider = [&Turn](const FBattleDecisionRequest& Request, FChoice& Choice, FString&)
	{
		const uint64 Actor = Request.GetActingBattlerId().GetValue();
		if (Turn == 0 && Actor == 31) Choice = Fight(TEXT("Move.Protect"), EBattleSide::Player, EBattlePosition::Right);
		else if (Turn == 0 && Actor == 21) Choice = Fight(TEXT("Move.WillOWisp"), EBattleSide::Player, EBattlePosition::Left);
		else if (Turn == 1 && Actor == 31) Choice = Fight(TEXT("Move.Swift"));
		else if (Turn == 1 && Actor == 21) Choice = Fight(TEXT("Move.QuickAttack"), EBattleSide::Player, EBattlePosition::Left);
		else Choice = Fight(TEXT("Move.SwordsDance"));
		return true;
	};
	const FDriveFunction Drive = [&Turn, Provider](FBattleEngine& Engine, FRunEvidence& Evidence, FString& Error) mutable
	{
		for (Turn = 0; Turn < 2; ++Turn)
		{
			if (!LockTurn(Engine, Provider, Evidence, Error) || !Finish(Engine, Evidence, Error)) return false;
			if (Turn == 0)
			{
				const FBattleObservedBattler* Player = Engine.GetSnapshot().FindObservedBattler(
					MakeNumericId<FBattlerId>(11));
				if (Engine.GetSnapshot().GetOutcome() != EBattleOutcome::InProgress
					|| Player == nullptr
					|| Player->CurrentHP != 1
					|| Player->bFainted
					|| Player->MajorStatusId != MakeDefinitionId<FConditionId>(TEXT("Condition.Burn")))
				{
					Error = TEXT("The first turn did not retain the burned one-HP player for partner continuation.");
					return false;
				}
			}
		}
		return true;
	};
	FRunEvidence Evidence;
	const bool bTwins = RunDeterministicTwins(*this, TEXT("partner team victory recovery"), Fixture.Catalog, Setup, Drive, &Evidence);
	const FBattleSnapshot& Final = Evidence.Replay.GetFinalSnapshot();
	TestEqual(TEXT("Partner continuation ends in Team Victory"), Final.GetOutcome(), EBattleOutcome::Victory);
	TestEqual(TEXT("Partner Team Victory has its typed cause"), Final.GetOutcomeCause(), EBattleOutcomeCause::PartnerTeamVictory);
	TestTrue(TEXT("Recovery is published exactly once"), CountEvents(Evidence.Replay, EBattleEventType::PartnerTeamVictoryRecovery) == 1);
	const FBattlePartyEntrySetup* Player = Final.GetPartyEntries().FindByPredicate([](const FBattlePartyEntrySetup& Entry) { return Entry.BattlerId.GetValue() == 11; });
	const FBattleObservedBattler* ObservedPlayer = Final.FindObservedBattler(MakeNumericId<FBattlerId>(11));
	TestTrue(TEXT("First player party member recovers to one HP and has Burn cured"),
		Player != nullptr && Player->CurrentHP == 1
		&& ObservedPlayer != nullptr && !ObservedPlayer->bFainted && !ObservedPlayer->MajorStatusId.IsValid());
	const FBattleEvent* Burn = nullptr;
	const FBattleEvent* PlayerFaint = nullptr;
	for (const FBattleResolution& Resolution : Evidence.Replay.GetResolutions())
	{
		for (const FBattleEvent& Event : Resolution.GetEvents())
		{
			if (Event.GetType() == EBattleEventType::StatusChanged
				&& Event.GetSource().BattlerId == MakeNumericId<FBattlerId>(21)
				&& Event.GetSource().DefinitionId == MakeDefinitionId<FDefinitionId>(TEXT("Move.WillOWisp"))) Burn = &Event;
			if (Event.GetType() == EBattleEventType::Fainted
				&& Event.GetTargets().Num() == 1
				&& Event.GetTargets()[0].BattlerId == MakeNumericId<FBattlerId>(11)) PlayerFaint = &Event;
		}
	}
	const FBattleEvent* PartnerDamage = FindSourceEvent(Evidence.Replay, EBattleEventType::Damage, 31);
	const FBattleEvent* Recovery = FindEvent(Evidence.Replay, EBattleEventType::PartnerTeamVictoryRecovery);
	const FBattleEvent* Ended = FindEvent(Evidence.Replay, EBattleEventType::BattleEnded);
	TestTrue(TEXT("Burn, player faint, partner damage, exact recovery, and battle end retain causal order"),
		Burn != nullptr && Burn->GetTargets().Num() == 1
		&& Burn->GetTargets()[0].BattlerId == MakeNumericId<FBattlerId>(11)
		&& PlayerFaint != nullptr && PartnerDamage != nullptr && Recovery != nullptr && Ended != nullptr
		&& Burn->GetEventOrdinal() < PlayerFaint->GetEventOrdinal()
		&& PlayerFaint->GetEventOrdinal() < PartnerDamage->GetEventOrdinal()
		&& PartnerDamage->GetEventOrdinal() < Recovery->GetEventOrdinal()
		&& Recovery->GetEventOrdinal() < Ended->GetEventOrdinal()
		&& Recovery->GetTargets().Num() == 1
		&& Recovery->GetTargets()[0].TrainerId == MakeNumericId<FTrainerId>(1)
		&& Recovery->GetTargets()[0].BattlerId == MakeNumericId<FBattlerId>(11)
		&& Recovery->GetNumericBefore() == TOptional<int64>(0)
		&& Recovery->GetNumericAfter() == TOptional<int64>(1)
		&& Recovery->GetNumericDelta() == TOptional<int64>(1));
	const TConstArrayView<FBattlePersistentProgressionEligibilityFact> Facts =
		Final.GetPersistentProgressionEligibilityFacts();
	TestTrue(TEXT("The surviving NPC partner has the exact external EXP and EV exclusion"),
		Facts.Num() == 1 && Facts[0].IsValid()
		&& Facts[0].TrainerId == MakeNumericId<FTrainerId>(3)
		&& Facts[0].BattlerId == MakeNumericId<FBattlerId>(31));
	return bTwins && ValidateGlobalInvariants(*this, Fixture.Catalog, Evidence, TEXT("partner victory"));
}

#endif // WITH_DEV_AUTOMATION_TESTS
