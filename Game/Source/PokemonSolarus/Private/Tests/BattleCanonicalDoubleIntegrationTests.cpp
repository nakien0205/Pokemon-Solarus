#if WITH_DEV_AUTOMATION_TESTS

#include "BattleCanonicalIntegrationTestSupport.h"

#include "Battle/BattleItem.h"
#include "Battle/BattleTargeting.h"
#include "Misc/AutomationTest.h"

namespace BattleCanonicalDoubleIntegrationPrivate
{
using namespace BattleCanonicalIntegrationTestSupport;

FSetupSpec MakeDoubleSpec(const uint64 BattleValue, const bool bReserves = false, const int32 OpponentHP = INDEX_NONE)
{
	FSetupSpec Spec;
	Spec.BattleValue = BattleValue;
	Spec.Format = EBattleFormat::Double;
	Spec.Trainers = {{1, EBattleSide::Player, EBattleTrainerRole::Player, EBattleDecisionController::Human, {}},
		{2, EBattleSide::Opponent, EBattleTrainerRole::Opponent, EBattleDecisionController::EnemyAI, {}}};
	FBattlerSpec PlayerLeft{1, 11, 0, TEXT("Species.Charizard"), TEXT("Nature.Jolly"), TEXT("Ability.Blaze"),
		{TEXT("Move.QuickAttack"), TEXT("Move.FollowMe"), TEXT("Move.Earthquake"), TEXT("Move.Uturn")}};
	PlayerLeft.EffortValues.Speed = 252;
	FBattlerSpec PlayerRight{1, 12, 1, TEXT("Species.Espathra"), TEXT("Nature.Timid"), TEXT("Ability.SpeedBoost"),
		{TEXT("Move.Swift"), TEXT("Move.HelpingHand"), TEXT("Move.TrickRoom"), TEXT("Move.Protect")}};
	PlayerRight.EffortValues.Speed = 252;
	PlayerRight.OriginalHeldItemId = TEXT("Item.QuickClaw");
	PlayerRight.CurrentHeldItemId = TEXT("Item.QuickClaw");
	FBattlerSpec OpponentLeft{2, 21, 0, TEXT("Species.Venusaur"), TEXT("Nature.Hardy"), TEXT("Ability.Overgrow"),
		{TEXT("Move.SwordsDance"), TEXT("Move.QuickAttack"), TEXT("Move.Protect")}};
	FBattlerSpec OpponentRight{2, 22, 1, TEXT("Species.Clefable"), TEXT("Nature.Hardy"), TEXT("Ability.MagicGuard"),
		{TEXT("Move.FollowMe"), TEXT("Move.HelpingHand"), TEXT("Move.QuickAttack"), TEXT("Move.SwordsDance")}};
	OpponentLeft.CurrentHP = OpponentHP;
	OpponentRight.CurrentHP = OpponentHP;
	Spec.Battlers = {PlayerLeft, PlayerRight, OpponentLeft, OpponentRight};
	Spec.Active = {{EBattleSide::Player, EBattlePosition::Left, 1, 11}, {EBattleSide::Player, EBattlePosition::Right, 1, 12},
		{EBattleSide::Opponent, EBattlePosition::Left, 2, 21}, {EBattleSide::Opponent, EBattlePosition::Right, 2, 22}};
	if (bReserves)
	{
		Spec.Battlers.Add({1, 13, 2, TEXT("Species.Gyarados"), TEXT("Nature.Adamant"), TEXT("Ability.Intimidate"), {TEXT("Move.Bite")}});
		Spec.Battlers.Add({1, 14, 3, TEXT("Species.Pelipper"), TEXT("Nature.Modest"), TEXT("Ability.Drizzle"), {TEXT("Move.RainDance")}});
		Spec.Battlers.Add({2, 23, 2, TEXT("Species.Rotom"), TEXT("Nature.Calm"), TEXT("Ability.Levitate"), {TEXT("Move.Thunder")}});
		Spec.Battlers.Add({2, 24, 3, TEXT("Species.Gyarados"), TEXT("Nature.Adamant"), TEXT("Ability.Intimidate"), {TEXT("Move.Bite")}});
	}
	return Spec;
}

bool Build(FAutomationTestBase& Test, const FSetupSpec& Spec, FCatalogFixture& Fixture, FBattleSetup& Setup)
{
	FString Error;
	if (!TryLoadProductionFixture(Test, Fixture, Error) || !TryBuildSetup(Fixture.Catalog, Spec, Setup, Error))
	{
		Test.AddError(Error);
		return false;
	}
	return true;
}

FChoice Fight(const TCHAR* Move, EBattleSide Side = EBattleSide::Opponent, EBattlePosition Position = EBattlePosition::Left)
{
	FChoice Choice;
	Choice.DefinitionId = FName(Move);
	Choice.ActiveTarget = MakeActiveSlotId(Side, Position);
	return Choice;
}

bool FinishTurn(FBattleEngine& Engine, FRunEvidence& Evidence, FString& Error)
{
	if (!ExecuteLockedQueue(Engine, Evidence, Error)) return false;
	while (!Engine.GetPendingDecisionRequests().IsEmpty())
	{
		TArray<FPartySlotId> ReservedPartySlots;
		const FChoiceProvider Replacement = [&ReservedPartySlots](const FBattleDecisionRequest& Request, FChoice& Choice, FString& OutError)
		{
			for (const FPartySlotId PartySlotId : Request.GetLegalSwitchPartySlots())
			{
				if (!ReservedPartySlots.Contains(PartySlotId))
				{
					Choice.Kind = EChoiceKind::Replacement;
					Choice.PartyTarget = PartySlotId;
					ReservedPartySlots.Add(PartySlotId);
					return true;
				}
			}
			OutError = FString::Printf(TEXT("Mandatory replacement for trainer %llu has no distinct legal switch slot."), Request.GetDecisionOwnerTrainerId().GetValue());
			return false;
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
	{
		for (const FBattleEvent& Event : Resolution.GetEvents())
		{
			if (Event.GetType() == Type
				&& Event.GetSource().BattlerId == MakeNumericId<FBattlerId>(BattlerValue)) return &Event;
		}
	}
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

bool HasExactBattlerTargets(
	const FBattleEvent* Event,
	const TArray<uint64>& Expected)
{
	if (Event == nullptr || Event->GetTargets().Num() != Expected.Num()) return false;
	for (int32 Index = 0; Index < Expected.Num(); ++Index)
	{
		if (Event->GetTargets()[Index].BattlerId
			!= MakeNumericId<FBattlerId>(Expected[Index])) return false;
	}
	return true;
}

bool HasExactActorOrder(
	const TArray<FBattleLockedAction>& Locked,
	const TArray<uint64>& Expected)
{
	if (Locked.Num() != Expected.Num()) return false;
	for (int32 Index = 0; Index < Expected.Num(); ++Index)
	{
		if (Locked[Index].Decision.GetActingBattlerId()
			!= MakeNumericId<FBattlerId>(Expected[Index])) return false;
	}
	return true;
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

int32 MovePP(
	const FBattleSnapshot& Snapshot,
	const uint64 BattlerValue,
	const TCHAR* MoveName)
{
	const FBattlePartyEntrySetup* Battler = Snapshot.FindBattler(MakeNumericId<FBattlerId>(BattlerValue));
	const FMoveId MoveId = MakeDefinitionId<FMoveId>(MoveName);
	const FBattleMoveSlotSetup* Move = Battler == nullptr ? nullptr : Battler->Moves.FindByPredicate(
		[MoveId](const FBattleMoveSlotSetup& Value) { return Value.MoveId == MoveId; });
	return Move == nullptr ? INDEX_NONE : Move->CurrentPP;
}
} // namespace BattleCanonicalDoubleIntegrationPrivate

using namespace BattleCanonicalDoubleIntegrationPrivate;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FC11ADoubleOrder, "PokemonSolarus.Battle.C11A.Double.Order.PrioritySpeedTiesTrickRoomQuickClawAndLockedStability", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FC11ADoubleOrder::RunTest(const FString& Parameters)
{
	FCatalogFixture Fixture; FBattleSetup Setup;
	if (!Build(*this, MakeDoubleSpec(11401), Fixture, Setup)) return false;
	int32 Turn = 0;
	const FChoiceProvider Provider = [&Turn](const FBattleDecisionRequest& Request, FChoice& Choice, FString&)
	{
		const uint64 Actor = Request.GetActingBattlerId().GetValue();
		if (Turn == 0 && Actor == 12) Choice = Fight(TEXT("Move.TrickRoom"));
		else if (Actor == 11 || Actor == 21 || Actor == 22) Choice = Fight(TEXT("Move.QuickAttack"), Actor < 20 ? EBattleSide::Opponent : EBattleSide::Player);
		else Choice = Fight(TEXT("Move.Swift"));
		return true;
	};
	const FDriveFunction Drive = [&Turn, Provider](FBattleEngine& Engine, FRunEvidence& Evidence, FString& Error) mutable
	{
		for (Turn = 0; Turn < 2 && Engine.GetSnapshot().GetOutcome() == EBattleOutcome::InProgress; ++Turn)
		{
			if (!LockTurn(Engine, Provider, Evidence, Error)) return false;
			if (Turn == 0)
			{
				const TArray<FBattleLockedAction> Locked = Engine.GetLockedActions();
				if (Locked.Num() != 4
					|| Locked[0].Decision.GetMoveId() != MakeDefinitionId<FMoveId>(TEXT("Move.QuickAttack"))
					|| Locked[1].Decision.GetMoveId() != MakeDefinitionId<FMoveId>(TEXT("Move.QuickAttack"))
					|| Locked[2].Decision.GetMoveId() != MakeDefinitionId<FMoveId>(TEXT("Move.QuickAttack"))
					|| Locked[3].Decision.GetMoveId() != MakeDefinitionId<FMoveId>(TEXT("Move.TrickRoom")))
				{
					Error = TEXT("Priority did not place all three Quick Attacks before Trick Room.");
					return false;
				}
			}
			if (!FinishTurn(Engine, Evidence, Error)) return false;
		}
		return true;
	};
	FRunEvidence Evidence;
	const bool bTwins = RunDeterministicTwins(*this, TEXT("double order"), Fixture.Catalog, Setup, Drive, &Evidence);
	TestTrue(TEXT("All four actions are locked each live turn"), CountEvents(Evidence.Replay, EBattleEventType::ActionOrderLocked) >= 4);
	TestTrue(TEXT("Trick Room is established through its production move"), HasEvent(Evidence.Replay, EBattleEventType::FieldEffectChanged));

	FSetupSpec TieSpec = MakeDoubleSpec(11405);
	for (FBattlerSpec& Battler : TieSpec.Battlers)
	{
		Battler.SpeciesId = TEXT("Species.Charizard");
		Battler.NatureId = TEXT("Nature.Hardy");
		Battler.AbilityId = TEXT("Ability.Blaze");
		Battler.MoveIds = {TEXT("Move.SwordsDance")};
		Battler.EffortValues = FPokemonStatValues();
		Battler.OriginalHeldItemId = NAME_None;
		Battler.CurrentHeldItemId = NAME_None;
	}
	FBattleSetup TieSetup;
	FString SetupError;
	if (!TryBuildSetup(Fixture.Catalog, TieSpec, TieSetup, SetupError))
	{
		AddError(SetupError);
		return false;
	}
	const FChoiceProvider TieProvider = [](const FBattleDecisionRequest&, FChoice& Choice, FString&)
	{
		Choice = Fight(TEXT("Move.SwordsDance"));
		return true;
	};
	const FDriveFunction TieDrive = [TieProvider](FBattleEngine& Engine, FRunEvidence& TieEvidence, FString& Error)
	{
		if (!LockTurn(Engine, TieProvider, TieEvidence, Error)) return false;
		if (!HasExactActorOrder(Engine.GetLockedActions(), {12, 11, 21, 22}))
		{
			Error = TEXT("Four-way tie did not preserve player-side precedence and the two scripted same-side results.");
			return false;
		}
		return true;
	};
	const FDefinitionId TiePurpose = MakeDefinitionId<FDefinitionId>(TEXT("Rule.ActionOrder.SameSideTie"));
	FRunEvidence TieEvidence;
	const bool bTieTwins = RunStrictTwins(*this, TEXT("double cross-side and same-side ties"),
		Fixture.Catalog, TieSetup, {{0, 1, 1, TiePurpose}, {0, 1, 0, TiePurpose}}, TieDrive, &TieEvidence);
	TestTrue(TEXT("Only the two same-side tie groups consume draws; the cross-side tie remains player-first"),
		TieEvidence.Replay.GetRandomTrace().Num() == 2
		&& TieEvidence.Replay.GetRandomTrace()[0].RulePurpose == TiePurpose
		&& TieEvidence.Replay.GetRandomTrace()[1].RulePurpose == TiePurpose);

	FSetupSpec TrickSpec = MakeDoubleSpec(11406);
	TrickSpec.Battlers[0].MoveIds = {TEXT("Move.SwordsDance")};
	TrickSpec.Battlers[1].MoveIds = {TEXT("Move.TrickRoom"), TEXT("Move.SwordsDance")};
	TrickSpec.Battlers[2].MoveIds = {TEXT("Move.SwordsDance")};
	TrickSpec.Battlers[3].MoveIds = {TEXT("Move.SwordsDance")};
	TrickSpec.Battlers[1].OriginalHeldItemId = NAME_None;
	TrickSpec.Battlers[1].CurrentHeldItemId = NAME_None;
	FBattleSetup TrickSetup;
	if (!TryBuildSetup(Fixture.Catalog, TrickSpec, TrickSetup, SetupError))
	{
		AddError(SetupError);
		return false;
	}
	int32 TrickTurn = 0;
	const FChoiceProvider TrickProvider = [&TrickTurn](const FBattleDecisionRequest& Request, FChoice& Choice, FString&)
	{
		Choice = TrickTurn == 0 && Request.GetActingBattlerId() == MakeNumericId<FBattlerId>(12)
			? Fight(TEXT("Move.TrickRoom")) : Fight(TEXT("Move.SwordsDance"));
		return true;
	};
	const FDriveFunction TrickDrive = [&TrickTurn, TrickProvider](FBattleEngine& Engine, FRunEvidence& TrickEvidence, FString& Error) mutable
	{
		for (TrickTurn = 0; TrickTurn < 2; ++TrickTurn)
		{
			if (!LockTurn(Engine, TrickProvider, TrickEvidence, Error)) return false;
			if (TrickTurn == 1 && !HasExactActorOrder(Engine.GetLockedActions(), {22, 21, 11, 12}))
			{
				Error = TEXT("Trick Room did not reverse the four production Speed values on the next lock.");
				return false;
			}
			if (!FinishTurn(Engine, TrickEvidence, Error)) return false;
		}
		return true;
	};
	FRunEvidence TrickEvidence;
	const bool bTrickTwins = RunDeterministicTwins(
		*this, TEXT("double Trick Room reversal"), Fixture.Catalog, TrickSetup, TrickDrive, &TrickEvidence, 5);

	FSetupSpec StabilitySpec = MakeDoubleSpec(11407, false, 1);
	StabilitySpec.Battlers[1].OriginalHeldItemId = NAME_None;
	StabilitySpec.Battlers[1].CurrentHeldItemId = NAME_None;
	StabilitySpec.Battlers[1].MoveIds = {TEXT("Move.SwordsDance")};
	FBattleSetup StabilitySetup;
	if (!TryBuildSetup(Fixture.Catalog, StabilitySpec, StabilitySetup, SetupError))
	{
		AddError(SetupError);
		return false;
	}
	const FChoiceProvider StabilityProvider = [](const FBattleDecisionRequest& Request, FChoice& Choice, FString&)
	{
		Choice = Request.GetActingBattlerId() == MakeNumericId<FBattlerId>(11)
			? Fight(TEXT("Move.QuickAttack")) : Fight(TEXT("Move.SwordsDance"));
		return true;
	};
	const FDriveFunction StabilityDrive = [StabilityProvider](FBattleEngine& Engine, FRunEvidence& StableEvidence, FString& Error)
	{
		if (!LockTurn(Engine, StabilityProvider, StableEvidence, Error)) return false;
		const TArray<FBattleLockedAction> LockedBefore = Engine.GetLockedActions();
		const FBattleResolution Begun = Engine.BeginNextLockedAction();
		const TOptional<FBattleLockedAction> Current = Engine.GetCurrentLockedAction();
		if (!Begun.WasAccepted() || !Current.IsSet()
			|| Current->Decision.GetActingBattlerId() != MakeNumericId<FBattlerId>(11))
		{
			Error = TEXT("Locked-stability fixture did not start the priority action.");
			return false;
		}
		RecordCheckpoint(Engine, StableEvidence, TEXT("stability-action-start"));
		const FBattleResolution Committed = Engine.CommitCurrentMoveAfterPreMoveGates();
		const FBattleResolution Targeted = Committed.WasAccepted() ? Engine.ResolveCurrentMoveTargets() : FBattleResolution();
		const FBattleResolution Effects = Targeted.WasAccepted() ? Engine.ExecuteCurrentMoveEffects() : FBattleResolution();
		if (!Committed.WasAccepted() || !Targeted.WasAccepted() || !Effects.WasAccepted())
		{
			Error = TEXT("Locked-stability priority action did not reach the removal checkpoint.");
			return false;
		}
		const FBattleEvent* Checkpoint = Effects.GetEvents().FindByPredicate([](const FBattleEvent& Event)
		{
			return Event.GetType() == EBattleEventType::OpponentRemovalCheckpoint;
		});
		const FBattlePartyEntrySetup* RefreshTarget = Engine.GetSnapshot().FindBattler(MakeNumericId<FBattlerId>(12));
		if (Checkpoint == nullptr || RefreshTarget == nullptr)
		{
			Error = TEXT("Locked-stability fixture lacks its public refresh facts.");
			return false;
		}
		FBattleBetweenActionsStatRefresh Refresh;
		Refresh.StateVersion = Engine.GetSnapshot().GetStateVersion();
		Refresh.OpponentRemovalCheckpointEventOrdinal = Checkpoint->GetEventOrdinal();
		Refresh.BattlerId = RefreshTarget->BattlerId;
		Refresh.NewLevel = RefreshTarget->Level;
		Refresh.NewStats = RefreshTarget->Stats;
		Refresh.NewStats.Speed = 1;
		Refresh.NewCurrentHP = RefreshTarget->CurrentHP;
		if (!Engine.ApplyBetweenActionsStatRefresh(Refresh).WasAccepted())
		{
			Error = TEXT("The public between-actions Speed refresh was rejected.");
			return false;
		}
		RecordCheckpoint(Engine, StableEvidence, TEXT("stability-refresh"));
		const TArray<FBattleLockedAction> LockedAfter = Engine.GetLockedActions();
		if (LockedAfter.Num() != LockedBefore.Num())
		{
			Error = TEXT("The locked queue changed size after a public Speed refresh.");
			return false;
		}
		for (int32 Index = 0; Index < LockedBefore.Num(); ++Index)
		{
			if (LockedBefore[Index].ActionId != LockedAfter[Index].ActionId
				|| LockedBefore[Index].Decision.GetActingBattlerId()
					!= LockedAfter[Index].Decision.GetActingBattlerId())
			{
				Error = TEXT("The locked queue reordered after a public Speed refresh.");
				return false;
			}
		}
		return ExecuteLockedQueue(Engine, StableEvidence, Error);
	};
	FRunEvidence StabilityEvidence;
	const bool bStabilityTwins = RunDeterministicTwins(*this, TEXT("double locked-order stability"),
		Fixture.Catalog, StabilitySetup, StabilityDrive, &StabilityEvidence, 5);
	TestEqual(TEXT("Locked stability applies one external stat refresh"),
		CountEvents(StabilityEvidence.Replay, EBattleEventType::StatRefreshApplied), 1);

	const FChoiceProvider QuickClawProvider = [](const FBattleDecisionRequest& Request, FChoice& Choice, FString&)
	{
		const uint64 Actor = Request.GetActingBattlerId().GetValue();
		if (Actor == 11) Choice = Fight(TEXT("Move.Earthquake"));
		else if (Actor == 12) Choice = Fight(TEXT("Move.Swift"));
		else Choice = Fight(TEXT("Move.SwordsDance"));
		return true;
	};
	const FDriveFunction QuickClawDrive = [QuickClawProvider](FBattleEngine& Engine, FRunEvidence& QuickClawEvidence, FString& Error)
	{
		if (!LockTurn(Engine, QuickClawProvider, QuickClawEvidence, Error)) return false;
		const TArray<FBattleLockedAction> Locked = Engine.GetLockedActions();
		const FBattleLockedAction* HolderAction = Locked.FindByPredicate([](const FBattleLockedAction& Action)
		{
			return Action.Decision.GetActingBattlerId() == MakeNumericId<FBattlerId>(12);
		});
		if (HolderAction == nullptr || HolderAction->OrderKey.FractionalPriorityTenths != FBattleItemRules::GetQuickClawFractionalPriorityTenths())
		{
			Error = TEXT("A successful Quick Claw draw did not affect the holder's public locked-order key.");
			return false;
		}
		return true;
	};
	FRunEvidence QuickClawEvidence;
	const bool bQuickClawTwins = RunStrictTwins(*this, TEXT("double Quick Claw success"), Fixture.Catalog, Setup,
		{{0, FBattleItemRules::GetQuickClawRollMaxInclusive(), FBattleItemRules::GetQuickClawSuccessRawDraw(), FBattleItemRules::GetQuickClawActivationPurpose()}}, QuickClawDrive, &QuickClawEvidence);
	const FBattleEvent* QuickClawActivation = FindEvent(QuickClawEvidence.Replay, EBattleEventType::ItemActivated);
	TestTrue(TEXT("Quick Claw uses the public held-item activation path"), QuickClawActivation != nullptr
		&& QuickClawActivation->GetSource().BattlerId == MakeNumericId<FBattlerId>(12));
	return bTwins && bTieTwins && bTrickTwins && bStabilityTwins && bQuickClawTwins
		&& ValidateGlobalInvariants(*this, Fixture.Catalog, Evidence, TEXT("double order"))
		&& ValidateGlobalInvariants(*this, Fixture.Catalog, TieEvidence, TEXT("double ties"))
		&& ValidateGlobalInvariants(*this, Fixture.Catalog, TrickEvidence, TEXT("Trick Room"))
		&& ValidateGlobalInvariants(*this, Fixture.Catalog, StabilityEvidence, TEXT("locked stability"))
		&& ValidateGlobalInvariants(*this, Fixture.Catalog, QuickClawEvidence, TEXT("Quick Claw"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FC11ADoubleTargets, "PokemonSolarus.Battle.C11A.Double.Targets.SelectedRedirectedRandomAllySpreadFriendlyFireAndUnavailable", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FC11ADoubleTargets::RunTest(const FString& Parameters)
{
	FCatalogFixture Fixture; FBattleSetup Setup;
	if (!Build(*this, MakeDoubleSpec(11402), Fixture, Setup)) return false;
	const FChoiceProvider Provider = [](const FBattleDecisionRequest& Request, FChoice& Choice, FString&)
	{
		const uint64 Actor = Request.GetActingBattlerId().GetValue();
		if (Actor == 11) Choice = Fight(TEXT("Move.FollowMe"));
		else if (Actor == 12) Choice = Fight(TEXT("Move.HelpingHand"), EBattleSide::Player, EBattlePosition::Left);
		else if (Actor == 21) Choice = Fight(TEXT("Move.QuickAttack"), EBattleSide::Player, EBattlePosition::Right);
		else Choice = Fight(TEXT("Move.SwordsDance"));
		return true;
	};
	const FDriveFunction Drive = [Provider](FBattleEngine& Engine, FRunEvidence& Evidence, FString& Error)
	{
		return LockTurn(Engine, Provider, Evidence, Error) && FinishTurn(Engine, Evidence, Error);
	};
	FRunEvidence Evidence;
	const bool bTwins = RunDeterministicTwins(*this, TEXT("double targets"), Fixture.Catalog, Setup, Drive, &Evidence);
	const FBattleEvent* Redirected = FindSourceEvent(Evidence.Replay, EBattleEventType::TargetsResolved, 21);
	const FBattleEvent* Ally = FindSourceEvent(Evidence.Replay, EBattleEventType::TargetsResolved, 12);
	TestTrue(TEXT("Selected Quick Attack is redirected from player Right to the Follow Me user"),
		HasExactBattlerTargets(Redirected, {11}) && Redirected->GetTargetResolution().IsSet()
		&& Redirected->GetTargetResolution()->TargetClass == EBattleTargetClass::SelectedOpponent
		&& Redirected->GetTargetResolution()->bWasRedirected
		&& HasEvent(Evidence.Replay, EBattleEventType::TargetRedirectionRegistered));
	TestTrue(TEXT("Helping Hand retains the exact selected ally target"),
		HasExactBattlerTargets(Ally, {11}) && Ally->GetTargetResolution().IsSet()
		&& Ally->GetTargetResolution()->TargetClass == EBattleTargetClass::SelectedAlly
		&& !Ally->GetTargetResolution()->bWasRedirected
		&& HasEvent(Evidence.Replay, EBattleEventType::ActionPowerModifierRegistered));

	FSetupSpec SpreadSpec = MakeDoubleSpec(11408);
	SpreadSpec.Battlers[1].OriginalHeldItemId = NAME_None;
	SpreadSpec.Battlers[1].CurrentHeldItemId = NAME_None;
	FBattleSetup SpreadSetup;
	FString SetupError;
	if (!TryBuildSetup(Fixture.Catalog, SpreadSpec, SpreadSetup, SetupError))
	{
		AddError(SetupError);
		return false;
	}
	const FChoiceProvider SpreadProvider = [](const FBattleDecisionRequest& Request, FChoice& Choice, FString&)
	{
		const uint64 Actor = Request.GetActingBattlerId().GetValue();
		Choice = Actor == 11 ? Fight(TEXT("Move.Earthquake"))
			: Actor == 12 ? Fight(TEXT("Move.Swift")) : Fight(TEXT("Move.SwordsDance"));
		return true;
	};
	const FDriveFunction SpreadDrive = [SpreadProvider](FBattleEngine& Engine, FRunEvidence& SpreadEvidence, FString& Error)
	{
		return LockTurn(Engine, SpreadProvider, SpreadEvidence, Error)
			&& FinishTurn(Engine, SpreadEvidence, Error);
	};
	FRunEvidence SpreadEvidence;
	const bool bSpreadTwins = RunDeterministicTwins(*this, TEXT("double production spread targets"),
		Fixture.Catalog, SpreadSetup, SpreadDrive, &SpreadEvidence, 5);
	const FBattleEvent* Earthquake = FindSourceEvent(SpreadEvidence.Replay, EBattleEventType::TargetsResolved, 11);
	const FBattleEvent* Swift = FindSourceEvent(SpreadEvidence.Replay, EBattleEventType::TargetsResolved, 12);
	TestTrue(TEXT("Earthquake freezes ally friendly-fire plus both opponents in structural order"),
		HasExactBattlerTargets(Earthquake, {12, 21, 22}) && Earthquake->GetTargetResolution().IsSet()
		&& Earthquake->GetTargetResolution()->TargetClass == EBattleTargetClass::FixedSpreadSet);
	TestTrue(TEXT("Swift freezes exactly the two living opponents and excludes its ally"),
		HasExactBattlerTargets(Swift, {21, 22}) && Swift->GetTargetResolution().IsSet()
		&& Swift->GetTargetResolution()->TargetClass == EBattleTargetClass::FixedOpponentSpreadSet);

	const FActiveSlotId PlayerLeft = MakeActiveSlotId(EBattleSide::Player, EBattlePosition::Left);
	const FActiveSlotId PlayerRight = MakeActiveSlotId(EBattleSide::Player, EBattlePosition::Right);
	const FActiveSlotId OpponentLeft = MakeActiveSlotId(EBattleSide::Opponent, EBattlePosition::Left);
	const FActiveSlotId OpponentRight = MakeActiveSlotId(EBattleSide::Opponent, EBattlePosition::Right);
	auto Positions = [PlayerLeft, PlayerRight, OpponentLeft, OpponentRight](
		const EBattleTargetPositionState LeftState,
		const EBattleTargetPositionState RightState,
		const bool bLeftSemi = false)
	{
		return TArray<FBattleTargetPositionFacts>{
			{PlayerLeft, MakeNumericId<FBattlerId>(11), EBattleTargetPositionState::Living, false},
			{PlayerRight, MakeNumericId<FBattlerId>(12), EBattleTargetPositionState::Living, false},
			{OpponentLeft, MakeNumericId<FBattlerId>(21), LeftState, bLeftSemi},
			{OpponentRight, MakeNumericId<FBattlerId>(22), RightState, false}};
	};
	FBattleTargetResolutionSpec RandomSpec;
	RandomSpec.TargetClass = EBattleTargetClass::RandomLegalOpponent;
	RandomSpec.UserSlotId = PlayerLeft;
	RandomSpec.UserBattlerId = MakeNumericId<FBattlerId>(11);
	RandomSpec.Positions = Positions(EBattleTargetPositionState::Living, EBattleTargetPositionState::Living);
	RandomSpec.RandomContext.BattleId = MakeNumericId<FBattleId>(11402);
	RandomSpec.RandomContext.TurnId = MakeNumericId<FTurnId>(1);
	RandomSpec.RandomContext.ActionId = MakeNumericId<FActionId>(1);
	RandomSpec.RandomContext.ResolutionId = MakeNumericId<FResolutionId>(1);
	RandomSpec.RandomContext.RulePurpose = FBattleTargetResolver::GetRandomLegalOpponentRulePurpose();
	BattleTest::FStrictBattleRandom RandomA({{0, 1, 1, RandomSpec.RandomContext.RulePurpose}});
	BattleTest::FStrictBattleRandom RandomB({{0, 1, 1, RandomSpec.RandomContext.RulePurpose}});
	FBattleTargetResolutionResult RandomResultA, RandomResultB;
	EBattleTargetingError TargetError = EBattleTargetingError::None;
	const bool bRandomA = FBattleTargetResolver::TryResolve(RandomSpec, RandomA, RandomResultA, TargetError);
	const bool bRandomB = FBattleTargetResolver::TryResolve(RandomSpec, RandomB, RandomResultB, TargetError);
	TestTrue(TEXT("The synthetic RandomLegalOpponent seam consumes one exact draw and selects opponent Right in twins"),
		bRandomA && bRandomB && RandomA.IsExact() && RandomB.IsExact()
		&& RandomResultA.Outcome == EBattleTargetResolutionOutcome::Resolved
		&& RandomResultB.Outcome == EBattleTargetResolutionOutcome::Resolved
		&& RandomResultA.Targets.Num() == 1 && RandomResultB.Targets.Num() == 1
		&& RandomResultA.Targets[0].GetBattler().BattlerId == MakeNumericId<FBattlerId>(22)
		&& RandomResultA.Targets == RandomResultB.Targets);

	FBattleTargetResolutionSpec Lifecycle = RandomSpec;
	Lifecycle.TargetClass = EBattleTargetClass::SelectedOpponent;
	Lifecycle.RandomContext = FBattleRandomContext();
	Lifecycle.ExplicitTarget = {OpponentLeft, MakeNumericId<FBattlerId>(21)};
	BattleTest::FStrictBattleRandom NoDraw({});
	FBattleTargetResolutionResult LifecycleResult;
	Lifecycle.Positions = Positions(EBattleTargetPositionState::Fainted, EBattleTargetPositionState::Living);
	TestTrue(TEXT("A fainted selected opponent falls back exactly to the other living opponent"),
		FBattleTargetResolver::TryResolve(Lifecycle, NoDraw, LifecycleResult, TargetError)
		&& LifecycleResult.bUsedFaintedTargetFallback && LifecycleResult.bWasRedirected
		&& LifecycleResult.Targets.Num() == 1
		&& LifecycleResult.Targets[0].GetBattler().BattlerId == MakeNumericId<FBattlerId>(22));
	Lifecycle.Positions = Positions(EBattleTargetPositionState::Captured, EBattleTargetPositionState::Living);
	TestTrue(TEXT("A captured explicit target cancels without fallback or RNG"),
		FBattleTargetResolver::TryResolve(Lifecycle, NoDraw, LifecycleResult, TargetError)
		&& LifecycleResult.Outcome == EBattleTargetResolutionOutcome::CapturedTargetCanceled
		&& LifecycleResult.Targets.IsEmpty() && NoDraw.GetTrace().IsEmpty());
	Lifecycle.Positions = Positions(EBattleTargetPositionState::Removed, EBattleTargetPositionState::Living);
	TestTrue(TEXT("A removed explicit target has no legal target and does not retarget"),
		FBattleTargetResolver::TryResolve(Lifecycle, NoDraw, LifecycleResult, TargetError)
		&& LifecycleResult.Outcome == EBattleTargetResolutionOutcome::NoLegalTarget
		&& LifecycleResult.Targets.IsEmpty());
	Lifecycle.Positions = Positions(EBattleTargetPositionState::Living, EBattleTargetPositionState::Living, true);
	TestTrue(TEXT("A semi-invulnerable target remains structurally selected for the later hit gate"),
		FBattleTargetResolver::TryResolve(Lifecycle, NoDraw, LifecycleResult, TargetError)
		&& LifecycleResult.Outcome == EBattleTargetResolutionOutcome::Resolved
		&& LifecycleResult.Targets.Num() == 1
		&& LifecycleResult.Targets[0].GetBattler().BattlerId == MakeNumericId<FBattlerId>(21));
	FBattleTargetSelectionSpec EmptySelection;
	EmptySelection.TargetClass = EBattleTargetClass::SelectedOpponent;
	EmptySelection.UserSlotId = PlayerLeft;
	EmptySelection.UserBattlerId = MakeNumericId<FBattlerId>(11);
	EmptySelection.Positions = {
		{PlayerLeft, MakeNumericId<FBattlerId>(11), EBattleTargetPositionState::Living, false},
		{PlayerRight, MakeNumericId<FBattlerId>(12), EBattleTargetPositionState::Living, false},
		{OpponentLeft, FBattlerId(), EBattleTargetPositionState::Empty, false},
		{OpponentRight, FBattlerId(), EBattleTargetPositionState::Empty, false}};
	FBattleTargetSelectionResult EmptyResult;
	TestTrue(TEXT("Two empty opponent positions expose no selector target"),
		FBattleTargetResolver::TryBuildSelection(EmptySelection, EmptyResult, TargetError)
		&& !EmptyResult.bHasLegalTarget && EmptyResult.BattlerCandidates.IsEmpty());

	FSetupSpec SemiSpec = MakeDoubleSpec(11409);
	SemiSpec.Battlers[0].MoveIds = {TEXT("Move.SwordsDance"), TEXT("Move.QuickAttack")};
	SemiSpec.Battlers[1].MoveIds = {TEXT("Move.Protect")};
	SemiSpec.Battlers[1].OriginalHeldItemId = NAME_None;
	SemiSpec.Battlers[1].CurrentHeldItemId = NAME_None;
	SemiSpec.Battlers[2].MoveIds = {TEXT("Move.Fly")};
	SemiSpec.Battlers[3].MoveIds = {TEXT("Move.Protect")};
	FBattleSetup SemiSetup;
	if (!TryBuildSetup(Fixture.Catalog, SemiSpec, SemiSetup, SetupError))
	{
		AddError(SetupError);
		return false;
	}
	int32 SemiTurn = 0;
	const FChoiceProvider SemiProvider = [&SemiTurn](const FBattleDecisionRequest& Request, FChoice& Choice, FString&)
	{
		const uint64 Actor = Request.GetActingBattlerId().GetValue();
		if (Actor == 11) Choice = SemiTurn == 0 ? Fight(TEXT("Move.SwordsDance"))
			: Fight(TEXT("Move.QuickAttack"), EBattleSide::Opponent, EBattlePosition::Left);
		else if (Actor == 21) Choice = Fight(TEXT("Move.Fly"), EBattleSide::Player, EBattlePosition::Left);
		else Choice = Fight(TEXT("Move.Protect"));
		return true;
	};
	const FDriveFunction SemiDrive = [&SemiTurn, SemiProvider](FBattleEngine& Engine, FRunEvidence& SemiEvidence, FString& Error) mutable
	{
		for (SemiTurn = 0; SemiTurn < 2; ++SemiTurn)
			if (!LockTurn(Engine, SemiProvider, SemiEvidence, Error)
				|| !FinishTurn(Engine, SemiEvidence, Error)) return false;
		return true;
	};
	FRunEvidence SemiEvidence;
	const bool bSemiTwins = RunDeterministicTwins(*this, TEXT("double semi-invulnerable hit gate"),
		Fixture.Catalog, SemiSetup, SemiDrive, &SemiEvidence, 5);
	TestTrue(TEXT("Priority Quick Attack reaches the production Fly semi-invulnerability gate without damage"),
		CountSourceEvents(SemiEvidence.Replay, EBattleEventType::Unreachable, 11) == 1
		&& CountSourceEvents(SemiEvidence.Replay, EBattleEventType::Damage, 11) == 0);
	return bTwins && bSpreadTwins && bRandomA && bRandomB && bSemiTwins
		&& ValidateGlobalInvariants(*this, Fixture.Catalog, Evidence, TEXT("redirected and ally targets"))
		&& ValidateGlobalInvariants(*this, Fixture.Catalog, SpreadEvidence, TEXT("spread targets"))
		&& ValidateGlobalInvariants(*this, Fixture.Catalog, SemiEvidence, TEXT("semi-invulnerable target"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FC11ADoubleFaints, "PokemonSolarus.Battle.C11A.Double.Faints.SimultaneousGroupingQueuedActionsAndReplacements", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FC11ADoubleFaints::RunTest(const FString& Parameters)
{
	FCatalogFixture Fixture; FBattleSetup Setup;
	if (!Build(*this, MakeDoubleSpec(11403, true, 1), Fixture, Setup)) return false;
	const FChoiceProvider Provider = [](const FBattleDecisionRequest& Request, FChoice& Choice, FString&)
	{
		const uint64 Actor = Request.GetActingBattlerId().GetValue();
		Choice = Actor == 12 ? Fight(TEXT("Move.Swift")) : Actor == 11 ? Fight(TEXT("Move.FollowMe")) : Fight(TEXT("Move.SwordsDance"));
		return true;
	};
	const FDriveFunction Drive = [Provider](FBattleEngine& Engine, FRunEvidence& Evidence, FString& Error)
	{
		return LockTurn(Engine, Provider, Evidence, Error) && FinishTurn(Engine, Evidence, Error);
	};
	FRunEvidence Evidence;
	const bool bTwins = RunDeterministicTwins(*this, TEXT("double simultaneous faint"), Fixture.Catalog, Setup, Drive, &Evidence);
	TestEqual(TEXT("Spread resolution faints both opponents"), CountEvents(Evidence.Replay, EBattleEventType::Fainted), 2);
	TestEqual(TEXT("Two mandatory replacement facts are published"), CountEvents(Evidence.Replay, EBattleEventType::ReplacementRequired), 2);
	TArray<const FBattleEvent*> Faints;
	for (const FBattleResolution& Resolution : Evidence.Replay.GetResolutions())
		for (const FBattleEvent& Event : Resolution.GetEvents())
			if (Event.GetType() == EBattleEventType::Fainted) Faints.Add(&Event);
	TestTrue(TEXT("Both spread faints share one nonzero group and retain Left/Right target order"),
		Faints.Num() == 2 && Faints[0]->GetSimultaneousGroupId().IsSet()
		&& Faints[1]->GetSimultaneousGroupId() == Faints[0]->GetSimultaneousGroupId()
		&& HasExactBattlerTargets(Faints[0], {21}) && HasExactBattlerTargets(Faints[1], {22}));
	int32 ReplacementDecisions = 0;
	for (const FBattleDecision& Decision : Evidence.Replay.GetInputs().Decisions)
		ReplacementDecisions += Decision.GetRequestKind() == EBattleDecisionRequestKind::MandatoryReplacement ? 1 : 0;
	TestEqual(TEXT("Exactly two mandatory replacement decisions are replay inputs"), ReplacementDecisions, 2);
	TestTrue(TEXT("Both fainted queued opponents are canceled before MoveUsed or PP consumption"),
		CountSourceEvents(Evidence.Replay, EBattleEventType::ActionCanceled, 21) == 1
		&& CountSourceEvents(Evidence.Replay, EBattleEventType::ActionCanceled, 22) == 1
		&& CountSourceEvents(Evidence.Replay, EBattleEventType::MoveUsed, 21) == 0
		&& CountSourceEvents(Evidence.Replay, EBattleEventType::MoveUsed, 22) == 0
		&& CountSourceEvents(Evidence.Replay, EBattleEventType::PPConsumed, 21) == 0
		&& CountSourceEvents(Evidence.Replay, EBattleEventType::PPConsumed, 22) == 0
		&& MovePP(Evidence.Replay.GetFinalSnapshot(), 21, TEXT("Move.SwordsDance"))
			== Setup.FindBattler(MakeNumericId<FBattlerId>(21))->Moves[0].CurrentPP
		&& MovePP(Evidence.Replay.GetFinalSnapshot(), 22, TEXT("Move.SwordsDance"))
			== Setup.FindBattler(MakeNumericId<FBattlerId>(22))->Moves[3].CurrentPP);
	TestTrue(TEXT("Party-ordered distinct reserves fill opponent Left then Right"),
		ActiveBattler(Evidence.Replay.GetFinalSnapshot(), EBattleSide::Opponent, EBattlePosition::Left)
			== MakeNumericId<FBattlerId>(23)
		&& ActiveBattler(Evidence.Replay.GetFinalSnapshot(), EBattleSide::Opponent, EBattlePosition::Right)
			== MakeNumericId<FBattlerId>(24));
	return bTwins && ValidateGlobalInvariants(*this, Fixture.Catalog, Evidence, TEXT("simultaneous faint"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FC11ADoubleSwitching, "PokemonSolarus.Battle.C11A.Double.Switching.DistinctReservesAndDuplicateReservationRejection", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FC11ADoubleSwitching::RunTest(const FString& Parameters)
{
	FCatalogFixture Fixture; FBattleSetup Setup;
	if (!Build(*this, MakeDoubleSpec(11404, true), Fixture, Setup)) return false;
	const FDriveFunction DuplicateDrive = [](FBattleEngine& Engine, FRunEvidence& DuplicateEvidence, FString& Error)
	{
		FBattleRejection Rejection;
		if (!Engine.TryBeginActionDecisionSequence(Rejection))
		{
			Error = TEXT("Could not begin the duplicate-reservation probe.");
			return false;
		}
		RecordCheckpoint(Engine, DuplicateEvidence, TEXT("duplicate-selection"));
		const TArray<FBattleDecisionRequest> Requests = Engine.GetPendingDecisionRequests();
		if (Requests.Num() != 2
			|| Requests[0].GetDecisionOwnerTrainerId() != MakeNumericId<FTrainerId>(1)
			|| Requests[1].GetDecisionOwnerTrainerId() != MakeNumericId<FTrainerId>(1))
		{
			Error = TEXT("Duplicate-reservation probe did not expose the two player requests.");
			return false;
		}
		TArray<FBattleDecision> DuplicateDecisions;
		for (const FBattleDecisionRequest& Request : Requests)
		{
			FChoice Choice;
			Choice.Kind = EChoiceKind::Switch;
			Choice.PartyTarget = MakePartySlotId(2);
			FBattleDecision Decision;
			if (!TryMakeDecision(Request, Choice, Decision, Error)) return false;
			DuplicateDecisions.Add(Decision);
		}
		FBattleDecisionBatchSpec BatchSpec{
			Requests[0].GetStateVersion(), Requests[0].GetRequestKind(),
			Requests[0].GetDecisionOwnerTrainerId(), DuplicateDecisions};
		FBattleDecisionBatch Batch;
		if (!FBattleDecisionBatch::TryCreate(BatchSpec, Batch, Rejection))
		{
			Error = TEXT("The structurally valid duplicate-reserve batch was not constructible.");
			return false;
		}
		const FString Before = SnapshotSignature(Engine.GetSnapshot());
		const int32 TraceBefore = Engine.ExportRandomTrace().Num();
		const int32 RequestsBefore = Engine.GetPendingDecisionRequests().Num();
		const FBattleReplayRecord ReplayBefore = Engine.ExportReplayRecord();
		const FBattleResolution Rejected = Engine.SubmitDecisionBatch(Batch);
		const FBattleReplayRecord ReplayAfter = Engine.ExportReplayRecord();
		if (Rejected.WasAccepted()
			|| Rejected.GetRejection().Reason != EBattleRejectionReason::IllegalSwitch
			|| Rejected.GetBeforeStateVersion() != Rejected.GetAfterStateVersion()
			|| SnapshotSignature(Engine.GetSnapshot()) != Before
			|| Engine.ExportRandomTrace().Num() != TraceBefore
			|| Engine.GetPendingDecisionRequests().Num() != RequestsBefore
			|| Rejected.GetEvents().Num() != 1
			|| Rejected.GetEvents()[0].GetType() != EBattleEventType::DecisionRejected
			|| ReplayAfter.GetInputs().Decisions.Num() != ReplayBefore.GetInputs().Decisions.Num() + 2
			|| ReplayAfter.GetResolutions().Num() != ReplayBefore.GetResolutions().Num() + 1)
		{
			Error = TEXT("Duplicate-reserve rejection changed state, RNG, requests, resources, or its exact audit delta.");
			return false;
		}
		RecordCheckpoint(Engine, DuplicateEvidence, TEXT("duplicate-rejected"));
		return true;
	};
	FRunEvidence DuplicateEvidence;
	const bool bDuplicateTwins = RunDeterministicTwins(*this, TEXT("double duplicate-reserve rejection"),
		Fixture.Catalog, Setup, DuplicateDrive, &DuplicateEvidence, 5);

	const FChoiceProvider Provider = [](const FBattleDecisionRequest& Request, FChoice& Choice, FString&)
	{
		Choice.Kind = EChoiceKind::Switch;
		const uint64 Actor = Request.GetActingBattlerId().GetValue();
		Choice.PartyTarget = MakePartySlotId(Actor == 11 || Actor == 21 ? 2 : 3);
		return true;
	};
	const FDriveFunction Drive = [Provider](FBattleEngine& Engine, FRunEvidence& Evidence, FString& Error)
	{
		return LockTurn(Engine, Provider, Evidence, Error) && FinishTurn(Engine, Evidence, Error);
	};
	FRunEvidence Evidence;
	const bool bTwins = RunDeterministicTwins(*this, TEXT("double distinct reserves"), Fixture.Catalog, Setup, Drive, &Evidence);
	int32 PlayerSwitches = 0;
	for (const FBattleResolution& Resolution : Evidence.Replay.GetResolutions())
	{
		for (const FBattleEvent& Event : Resolution.GetEvents())
		{
			PlayerSwitches += Event.GetType() == EBattleEventType::Switched
				&& Event.GetSource().TrainerId == MakeNumericId<FTrainerId>(1) ? 1 : 0;
		}
	}
	TestEqual(TEXT("Both distinct player reserves switch in"), PlayerSwitches, 2);
	TestTrue(TEXT("All four distinct reserves occupy their requested Left/Right destinations"),
		ActiveBattler(Evidence.Replay.GetFinalSnapshot(), EBattleSide::Player, EBattlePosition::Left)
			== MakeNumericId<FBattlerId>(13)
		&& ActiveBattler(Evidence.Replay.GetFinalSnapshot(), EBattleSide::Player, EBattlePosition::Right)
			== MakeNumericId<FBattlerId>(14)
		&& ActiveBattler(Evidence.Replay.GetFinalSnapshot(), EBattleSide::Opponent, EBattlePosition::Left)
			== MakeNumericId<FBattlerId>(23)
		&& ActiveBattler(Evidence.Replay.GetFinalSnapshot(), EBattleSide::Opponent, EBattlePosition::Right)
			== MakeNumericId<FBattlerId>(24));
	return bDuplicateTwins && bTwins
		&& ValidateGlobalInvariants(*this, Fixture.Catalog, DuplicateEvidence, TEXT("duplicate reserve"))
		&& ValidateGlobalInvariants(*this, Fixture.Catalog, Evidence, TEXT("distinct double switches"));
}

#endif // WITH_DEV_AUTOMATION_TESTS
