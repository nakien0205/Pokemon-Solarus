#if WITH_DEV_AUTOMATION_TESTS

#include "BattleCanonicalIntegrationTestSupport.h"

#include "Misc/AutomationTest.h"

namespace BattleCanonicalSingleEffectsPrivate
{
	using namespace BattleCanonicalIntegrationTestSupport;

	struct FSpeciesAbility
	{
		const TCHAR* Species;
		const TCHAR* Ability;
	};

	const FSpeciesAbility SpeciesAbilities[] = {
		{TEXT("Species.Charizard"), TEXT("Ability.Blaze")},
		{TEXT("Species.Venusaur"), TEXT("Ability.Overgrow")},
		{TEXT("Species.Gyarados"), TEXT("Ability.Intimidate")},
		{TEXT("Species.Rotom"), TEXT("Ability.Levitate")},
		{TEXT("Species.Pelipper"), TEXT("Ability.Drizzle")},
		{TEXT("Species.Espathra"), TEXT("Ability.SpeedBoost")},
		{TEXT("Species.Clefable"), TEXT("Ability.MagicGuard")},
		{TEXT("Species.Excadrill"), TEXT("Ability.MoldBreaker")}};

	const TCHAR* HeldItems[] = {
		TEXT("Item.AirBalloon"), TEXT("Item.ChoiceBand"), TEXT("Item.FocusSash"),
		TEXT("Item.HeavyDutyBoots"), TEXT("Item.Leftovers"), TEXT("Item.LifeOrb"),
		TEXT("Item.LumBerry"), TEXT("Item.QuickClaw"), TEXT("Item.SitrusBerry")};

	FChoice FightChoice(const FName MoveId, const FActiveSlotId Target = FActiveSlotId())
	{
		FChoice Choice;
		Choice.Kind = EChoiceKind::Fight;
		Choice.DefinitionId = MoveId;
		Choice.ActiveTarget = Target;
		return Choice;
	}

	const FBattleEvent* FindMoveUsed(const FBattleReplayRecord& Record, const FName MoveId)
	{
		for (const FBattleResolution& Resolution : Record.GetResolutions())
		{
			for (const FBattleEvent& Event : Resolution.GetEvents())
			{
				if (Event.GetType() == EBattleEventType::MoveUsed
					&& Event.GetSource().DefinitionId == MakeDefinitionId<FDefinitionId>(*MoveId.ToString())) return &Event;
			}
		}
		return nullptr;
	}

	const FBattleEvent* FindSourceActionEvent(
		const FBattleReplayRecord& Record,
		const EBattleEventType Type,
		const uint64 BattlerValue,
		const TCHAR* Definition)
	{
		const FBattlerId BattlerId = MakeNumericId<FBattlerId>(BattlerValue);
		const FDefinitionId DefinitionId = MakeDefinitionId<FDefinitionId>(Definition);
		for (const FBattleResolution& Resolution : Record.GetResolutions())
			for (const FBattleEvent& Event : Resolution.GetEvents())
				if (Event.GetType() == Type
					&& Event.GetSource().BattlerId == BattlerId
					&& Event.GetSource().DefinitionId == DefinitionId) return &Event;
		return nullptr;
	}

	int32 CountActionEvents(const FBattleReplayRecord& Record, const FActionId ActionId, const EBattleEventType Type)
	{
		int32 Count = 0;
		for (const FBattleResolution& Resolution : Record.GetResolutions())
			for (const FBattleEvent& Event : Resolution.GetEvents())
				Count += Event.GetActionId() == ActionId && Event.GetType() == Type ? 1 : 0;
		return Count;
	}

	bool HasReachedEffectPath(const FBattleReplayRecord& Record, const FActionId ActionId)
	{
		for (const FBattleResolution& Resolution : Record.GetResolutions())
		{
			for (const FBattleEvent& Event : Resolution.GetEvents())
			{
				if (Event.GetActionId() != ActionId) continue;
				switch (Event.GetType())
				{
				case EBattleEventType::Missed: case EBattleEventType::Immunity: case EBattleEventType::Protected:
				case EBattleEventType::Unreachable: case EBattleEventType::Damage: case EBattleEventType::Healing:
				case EBattleEventType::StatusChanged: case EBattleEventType::StatStageChanged:
				case EBattleEventType::FieldEffectChanged: case EBattleEventType::EffectBlocked:
				case EBattleEventType::EffectFailed: case EBattleEventType::EffectCapped:
				case EBattleEventType::EffectPrevented: case EBattleEventType::EffectDeferred:
				case EBattleEventType::TargetRedirectionRegistered: case EBattleEventType::ActionPowerModifierRegistered:
				case EBattleEventType::ItemRemoved: case EBattleEventType::ItemRestored: case EBattleEventType::ItemTransferred:
				case EBattleEventType::Switched: case EBattleEventType::ActionCanceled: return true;
				default: break;
				}
			}
		}
		return false;
	}

	FSetupSpec MoveSpec(const FBattleMoveDefinition& Move, const int32 Index)
	{
		const bool bDouble = Move.TargetClass == EBattleTargetClass::SelectedAlly
			|| Move.TargetClass == EBattleTargetClass::FixedSpreadSet
			|| Move.TargetClass == EBattleTargetClass::FixedOpponentSpreadSet;
		FSetupSpec Spec;
		Spec.BattleValue = 11100 + Index;
		Spec.EncounterKind = EBattleEncounterKind::Trainer;
		Spec.Format = bDouble ? EBattleFormat::Double : EBattleFormat::Single;
		Spec.Policies.bBagAllowed = true;
		Spec.Policies.bShiftPromptEligible = false;
		Spec.Trainers = {
			{1, EBattleSide::Player, EBattleTrainerRole::Player, EBattleDecisionController::Human, {}},
			{2, EBattleSide::Opponent, EBattleTrainerRole::Opponent, EBattleDecisionController::EnemyAI, {}}};
		const FSpeciesAbility& Pair = SpeciesAbilities[Index % UE_ARRAY_COUNT(SpeciesAbilities)];
		FBattlerSpec Actor;
		Actor.TrainerValue = 1;
		Actor.BattlerValue = 11;
		Actor.SpeciesId = FName(Pair.Species);
		Actor.NatureId = FName(TEXT("Nature.Hardy"));
		Actor.AbilityId = FName(Pair.Ability);
		Actor.MoveIds = {Move.Id.GetDefinitionId().GetName()};
		Actor.OriginalHeldItemId = FName(HeldItems[Index % UE_ARRAY_COUNT(HeldItems)]);
		Actor.CurrentHeldItemId = Actor.OriginalHeldItemId;
		Spec.Battlers.Add(Actor);
		FBattlerSpec Target;
		Target.TrainerValue = 2;
		Target.BattlerValue = 21;
		Target.SpeciesId = FName(TEXT("Species.Clefable"));
		Target.AbilityId = FName(TEXT("Ability.MagicGuard"));
		Target.MoveIds = {FName(TEXT("Move.SwordsDance"))};
		Target.OriginalHeldItemId = FName(TEXT("Item.Leftovers"));
		Target.CurrentHeldItemId = Target.OriginalHeldItemId;
		Spec.Battlers.Add(Target);
		Spec.Active = {
			{EBattleSide::Player, EBattlePosition::Left, 1, 11},
			{EBattleSide::Opponent, EBattlePosition::Left, 2, 21}};
		if (bDouble)
		{
			FBattlerSpec Ally;
			Ally.TrainerValue = 1;
			Ally.BattlerValue = 12;
			Ally.PartyIndex = 1;
			Ally.SpeciesId = FName(TEXT("Species.Venusaur"));
			Ally.AbilityId = FName(TEXT("Ability.Overgrow"));
			Ally.MoveIds = {FName(TEXT("Move.SwordsDance"))};
			Spec.Battlers.Add(Ally);
			FBattlerSpec Other = Target;
			Other.BattlerValue = 22;
			Other.PartyIndex = 1;
			Other.SpeciesId = FName(TEXT("Species.Rotom"));
			Other.AbilityId = FName(TEXT("Ability.Levitate"));
			Other.OriginalHeldItemId = NAME_None;
			Other.CurrentHeldItemId = NAME_None;
			Spec.Battlers.Add(Other);
			Spec.Active.Add({EBattleSide::Player, EBattlePosition::Right, 1, 12});
			Spec.Active.Add({EBattleSide::Opponent, EBattlePosition::Right, 2, 22});
		}
		return Spec;
	}

	bool RunMoveSweep(FAutomationTestBase& Test, const FCatalogFixture& Fixture)
	{
		TSet<FName> Exercised;
		for (int32 Index = 0; Index < Fixture.Catalog.GetMoves().Num(); ++Index)
		{
			const FBattleMoveDefinition& Move = Fixture.Catalog.GetMoves()[Index];
			FBattleSetup Setup;
			FString Error;
			if (!TryBuildSetup(Fixture.Catalog, MoveSpec(Move, Index), Setup, Error))
			{
				Test.AddError(Error);
				return false;
			}
			const FName MoveName = Move.Id.GetDefinitionId().GetName();
			const FActiveSlotId MoveTarget = Move.TargetClass == EBattleTargetClass::SelectedAlly
				? MakeActiveSlotId(EBattleSide::Player, EBattlePosition::Right)
				: MakeActiveSlotId(EBattleSide::Opponent, EBattlePosition::Left);
			const FChoiceProvider Provider = [MoveName, MoveTarget](
				const FBattleDecisionRequest& Request, FChoice& Out, FString&)
			{
				if (Request.GetActingBattlerId() == MakeNumericId<FBattlerId>(11))
				{
					Out = FightChoice(MoveName, MoveTarget);
				}
				else
				{
					Out = FightChoice(FName(TEXT("Move.SwordsDance")));
				}
				return true;
			};
			const FDriveFunction Drive = [Provider](
				FBattleEngine& Engine, FRunEvidence& Evidence, FString& DriveError)
			{
				return LockTurn(Engine, Provider, Evidence, DriveError)
					&& ExecuteLockedQueue(Engine, Evidence, DriveError);
			};
			FRunEvidence Evidence;
			if (!RunDeterministicTwins(
				Test, FString::Printf(TEXT("catalog move %s"), *MoveName.ToString()),
				Fixture.Catalog, Setup, Drive, &Evidence))
			{
				return false;
			}
			const FBattleEvent* MoveUsed = FindMoveUsed(Evidence.Replay, MoveName);
			if (!Test.TestNotNull(FString::Printf(TEXT("%s reaches MoveUsed"), *MoveName.ToString()), MoveUsed))
			{
				return false;
			}
			if (!Test.TestTrue(FString::Printf(TEXT("%s reaches an accepted, blocked, no-effect, or target-loss path"), *MoveName.ToString()),
				MoveUsed->GetActionId().IsValid() && HasReachedEffectPath(Evidence.Replay, MoveUsed->GetActionId()))) return false;
			if (!Test.TestEqual(FString::Printf(TEXT("%s action completes at most once"), *MoveName.ToString()),
				CountActionEvents(Evidence.Replay, MoveUsed->GetActionId(), EBattleEventType::ActionCompleted), 1)) return false;
			Exercised.Add(MoveName);
			if (!ValidateGlobalInvariants(Test, Fixture.Catalog, Evidence, MoveName.ToString()))
			{
				return false;
			}
		}
		return Test.TestEqual(TEXT("All 62 production moves have an integration path"), Exercised.Num(), 62);
	}

	bool RunMajorStatusMatrix(FAutomationTestBase& Test, const FCatalogFixture& Fixture)
	{
		struct FCase { const TCHAR* Move; const TCHAR* Status; uint64 Seed; };
		const FCase Cases[] = {{TEXT("Move.WillOWisp"), TEXT("Condition.Burn"), 5}, {TEXT("Move.IceBeam"), TEXT("Condition.Freeze"), 5},
			{TEXT("Move.ThunderWave"), TEXT("Condition.Paralysis"), 5}, {TEXT("Move.PoisonPowder"), TEXT("Condition.Poison"), 5},
			{TEXT("Move.SleepPowder"), TEXT("Condition.Sleep"), 5}, {TEXT("Move.Toxic"), TEXT("Condition.Toxic"), 5}};
		bool bValid = true;
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Cases); ++Index)
		{
			const FBattleMoveDefinition* Move = Fixture.Catalog.FindMove(MakeDefinitionId<FMoveId>(Cases[Index].Move));
			if (!Test.TestNotNull(FString(Cases[Index].Move) + TEXT(" is catalog-backed"), Move)) return false;
			FBattleSetup Setup; FString Error;
			if (!TryBuildSetup(Fixture.Catalog, MoveSpec(*Move, 200 + Index), Setup, Error)) { Test.AddError(Error); return false; }
			const FName MoveName(Cases[Index].Move);
			const FChoiceProvider Provider = [MoveName](const FBattleDecisionRequest& Request, FChoice& Out, FString&)
			{
				Out = Request.GetActingBattlerId() == MakeNumericId<FBattlerId>(11)
					? FightChoice(MoveName, MakeActiveSlotId(EBattleSide::Opponent, EBattlePosition::Left))
					: FightChoice(TEXT("Move.SwordsDance"));
				return true;
			};
			const FDriveFunction Drive = [Provider](FBattleEngine& Engine, FRunEvidence& Evidence, FString& DriveError)
			{
				return LockTurn(Engine, Provider, Evidence, DriveError) && ExecuteLockedQueue(Engine, Evidence, DriveError);
			};
			FRunEvidence Evidence;
			bValid &= RunDeterministicTwins(Test, FString(Cases[Index].Status), Fixture.Catalog, Setup, Drive, &Evidence, Cases[Index].Seed);
			const FBattleObservedBattler* Target = Evidence.Replay.GetFinalSnapshot().FindObservedBattler(MakeNumericId<FBattlerId>(21));
			bValid &= Test.TestTrue(FString(Cases[Index].Status) + TEXT(" is reached through its canonical move"), Target != nullptr
				&& Target->MajorStatusId == MakeDefinitionId<FConditionId>(Cases[Index].Status)
				&& HasEvent(Evidence.Replay, EBattleEventType::StatusChanged));
		}
		return bValid;
	}

	bool RunVolatileCrossMatrix(FAutomationTestBase& Test, const FCatalogFixture& Fixture)
	{
		struct FCase { const TCHAR* Move; const TCHAR* Condition; };
		const FCase Cases[] = {{TEXT("Move.ConfuseRay"), TEXT("Condition.Confusion")}, {TEXT("Move.Protect"), TEXT("Condition.Protect")},
			{TEXT("Move.Substitute"), TEXT("Condition.Substitute")}, {TEXT("Move.Taunt"), TEXT("Condition.Taunt")},
			{TEXT("Move.SolarBeam"), TEXT("Condition.Charging")}, {TEXT("Move.HyperBeam"), TEXT("Condition.Recharge")},
			{TEXT("Move.Fly"), TEXT("Condition.FlySemiInvulnerable")}, {TEXT("Move.MeanLook"), TEXT("Condition.Trap")},
			{TEXT("Move.Wrap"), TEXT("Condition.PartialTrap")}, {TEXT("Move.LeechSeed"), TEXT("Condition.LeechSeed")}};
		bool bValid = true;
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Cases); ++Index)
		{
			const FBattleMoveDefinition* Move = Fixture.Catalog.FindMove(MakeDefinitionId<FMoveId>(Cases[Index].Move));
			if (Move == nullptr) return false;
			const bool bDescriptorMatches = Move->Effects.ContainsByPredicate([&Cases, Index](const FBattleMoveEffectDescriptor& Effect)
			{
				return Effect.ConditionId == MakeDefinitionId<FConditionId>(Cases[Index].Condition);
			});
			bValid &= Test.TestTrue(FString(Cases[Index].Move) + TEXT(" uses the expected production condition descriptor"), bDescriptorMatches);
			FBattleSetup Setup; FString Error;
			if (!TryBuildSetup(Fixture.Catalog, MoveSpec(*Move, 300 + Index), Setup, Error)) { Test.AddError(Error); return false; }
			const FName MoveName(Cases[Index].Move);
			const FChoiceProvider Provider = [MoveName](const FBattleDecisionRequest& Request, FChoice& Out, FString&)
			{
				Out = Request.GetActingBattlerId() == MakeNumericId<FBattlerId>(11)
					? FightChoice(MoveName, MakeActiveSlotId(EBattleSide::Opponent, EBattlePosition::Left))
					: FightChoice(TEXT("Move.SwordsDance")); return true;
			};
			const FDriveFunction Drive = [Provider](FBattleEngine& Engine, FRunEvidence& Evidence, FString& DriveError)
			{
				return LockTurn(Engine, Provider, Evidence, DriveError) && ExecuteLockedQueue(Engine, Evidence, DriveError);
			};
			FRunEvidence Evidence;
			bValid &= RunDeterministicTwins(Test, FString(Cases[Index].Condition), Fixture.Catalog, Setup, Drive, &Evidence, 5);
			const FBattleEvent* Used = FindMoveUsed(Evidence.Replay, MoveName);
			bValid &= Test.TestTrue(FString(Cases[Index].Condition) + TEXT(" publishes a mutation/deferred path"), Used != nullptr
				&& (CountActionEvents(Evidence.Replay, Used->GetActionId(), EBattleEventType::StatusChanged) > 0
					|| CountActionEvents(Evidence.Replay, Used->GetActionId(), EBattleEventType::EffectDeferred) > 0));
		}

		const FBattleMoveDefinition* Protect = Fixture.Catalog.FindMove(MakeDefinitionId<FMoveId>(TEXT("Move.Protect")));
		FSetupSpec ProtectSpec = MoveSpec(*Protect, 399);
		ProtectSpec.Battlers[1].MoveIds = {TEXT("Move.QuickAttack")};
		FBattleSetup ProtectSetup; FString Error;
		if (!TryBuildSetup(Fixture.Catalog, ProtectSpec, ProtectSetup, Error)) { Test.AddError(Error); return false; }
		const FChoiceProvider ProtectProvider = [](const FBattleDecisionRequest& Request, FChoice& Out, FString&)
		{
			Out = Request.GetActingBattlerId() == MakeNumericId<FBattlerId>(11) ? FightChoice(TEXT("Move.Protect"))
				: FightChoice(TEXT("Move.QuickAttack"), MakeActiveSlotId(EBattleSide::Player, EBattlePosition::Left)); return true;
		};
		const FDriveFunction ProtectDrive = [ProtectProvider](FBattleEngine& Engine, FRunEvidence& Evidence, FString& DriveError)
		{
			return LockTurn(Engine, ProtectProvider, Evidence, DriveError) && ExecuteLockedQueue(Engine, Evidence, DriveError);
		};
		FRunEvidence Protected;
		bValid &= RunDeterministicTwins(Test, TEXT("Protect blocks damage"), Fixture.Catalog, ProtectSetup, ProtectDrive, &Protected, 5);
		const FBattlePartyEntrySetup* InitialPlayer = ProtectSetup.FindBattler(MakeNumericId<FBattlerId>(11));
		const FBattlePartyEntrySetup* FinalPlayer = Protected.Replay.GetFinalSnapshot().FindBattler(MakeNumericId<FBattlerId>(11));
		bValid &= Test.TestTrue(TEXT("Protect publishes Protected and preserves HP"), HasEvent(Protected.Replay, EBattleEventType::Protected)
			&& InitialPlayer != nullptr && FinalPlayer != nullptr && InitialPlayer->CurrentHP == FinalPlayer->CurrentHP);
		return bValid;
	}

	FSetupSpec BagSpec(const uint64 BattleValue, const TCHAR* Item, const int32 CurrentHP)
	{
		FSetupSpec Spec;
		Spec.BattleValue = BattleValue;
		Spec.EncounterKind = EBattleEncounterKind::Trainer;
		Spec.Format = EBattleFormat::Single;
		Spec.Policies.bBagAllowed = true;
		Spec.Policies.bShiftPromptEligible = false;
		Spec.Trainers = {
			{1, EBattleSide::Player, EBattleTrainerRole::Player, EBattleDecisionController::Human,
				{{FName(Item), 2}}},
			{2, EBattleSide::Opponent, EBattleTrainerRole::Opponent, EBattleDecisionController::EnemyAI, {}}};
		FBattlerSpec Player;
		Player.TrainerValue = 1;
		Player.BattlerValue = 11;
		Player.SpeciesId = FName(TEXT("Species.Charizard"));
		Player.AbilityId = FName(TEXT("Ability.Blaze"));
		if (FName(Item) == FName(TEXT("Item.FullHeal")))
		{
			Player.SpeciesId = FName(TEXT("Species.Clefable"));
			Player.AbilityId = FName(TEXT("Ability.MagicGuard"));
		}
		Player.MoveIds = {FName(TEXT("Move.SwordsDance"))};
		Player.CurrentHP = CurrentHP;
		Spec.Battlers.Add(Player);
		FBattlerSpec Reserve;
		Reserve.TrainerValue = 1;
		Reserve.BattlerValue = 12;
		Reserve.PartyIndex = 1;
		Reserve.SpeciesId = FName(TEXT("Species.Venusaur"));
		Reserve.AbilityId = FName(TEXT("Ability.Overgrow"));
		Reserve.MoveIds = {FName(TEXT("Move.VineWhip"))};
		Reserve.CurrentHP = 0;
		Spec.Battlers.Add(Reserve);
		FBattlerSpec Opponent;
		Opponent.TrainerValue = 2;
		Opponent.BattlerValue = 21;
		Opponent.SpeciesId = FName(TEXT("Species.Clefable"));
		Opponent.AbilityId = FName(TEXT("Ability.MagicGuard"));
		Opponent.MoveIds = FName(Item) == FName(TEXT("Item.FullHeal"))
			? TArray<FName>{FName(TEXT("Move.WillOWisp")), FName(TEXT("Move.SwordsDance"))}
			: TArray<FName>{FName(TEXT("Move.SwordsDance"))};
		Spec.Battlers.Add(Opponent);
		Spec.Active = {
			{EBattleSide::Player, EBattlePosition::Left, 1, 11},
			{EBattleSide::Opponent, EBattlePosition::Left, 2, 21}};
		return Spec;
	}

	bool HasSourceDefinition(const FBattleReplayRecord& Record, const EBattleEventType Type, const TCHAR* Id)
	{
		const FDefinitionId DefinitionId = MakeDefinitionId<FDefinitionId>(Id);
		for (const FBattleResolution& Resolution : Record.GetResolutions())
			for (const FBattleEvent& Event : Resolution.GetEvents())
				if (Event.GetType() == Type && Event.GetSource().DefinitionId == DefinitionId) return true;
		return false;
	}

	FSetupSpec InteractionSpec(const uint64 BattleValue, const TArray<FName>& PlayerMoves, const TArray<FName>& OpponentMoves,
		const FName PlayerAbility = TEXT("Ability.Blaze"), const FName OpponentAbility = TEXT("Ability.MagicGuard"),
		const FName PlayerItem = NAME_None, const FName OpponentItem = NAME_None)
	{
		FSetupSpec Spec;
		Spec.BattleValue = BattleValue; Spec.Format = EBattleFormat::Single; Spec.Policies.bShiftPromptEligible = false;
		Spec.Trainers = {{1, EBattleSide::Player, EBattleTrainerRole::Player, EBattleDecisionController::Human, {}},
			{2, EBattleSide::Opponent, EBattleTrainerRole::Opponent, EBattleDecisionController::EnemyAI, {}}};
		FBattlerSpec Player{1, 11, 0, TEXT("Species.Charizard"), TEXT("Nature.Hardy"), PlayerAbility, PlayerMoves};
		Player.OriginalHeldItemId = PlayerItem; Player.CurrentHeldItemId = PlayerItem; Player.EffortValues.Speed = 252;
		FBattlerSpec Opponent{2, 21, 0, TEXT("Species.Clefable"), TEXT("Nature.Hardy"), OpponentAbility, OpponentMoves};
		Opponent.OriginalHeldItemId = OpponentItem; Opponent.CurrentHeldItemId = OpponentItem;
		Spec.Battlers = {Player, Opponent};
		Spec.Active = {{EBattleSide::Player, EBattlePosition::Left, 1, 11}, {EBattleSide::Opponent, EBattlePosition::Left, 2, 21}};
		return Spec;
	}

	bool RunTurn(FBattleEngine& Engine, FRunEvidence& Evidence, FString& Error, const FName PlayerMove, const FName OpponentMove, const bool bEndTurn)
	{
		const FChoiceProvider Provider = [PlayerMove, OpponentMove](const FBattleDecisionRequest& Request, FChoice& Out, FString&)
		{
			const bool bPlayer = Request.GetDecisionOwnerTrainerId() == MakeNumericId<FTrainerId>(1);
			Out = FightChoice(bPlayer ? PlayerMove : OpponentMove,
				MakeActiveSlotId(bPlayer ? EBattleSide::Opponent : EBattleSide::Player, EBattlePosition::Left)); return true;
		};
		if (!LockTurn(Engine, Provider, Evidence, Error) || !ExecuteLockedQueue(Engine, Evidence, Error)) return false;
		return !bEndTurn || Engine.GetSnapshot().GetPhase() != EBattlePhase::EndOfTurn || ResolveEndTurn(Engine, Evidence, Error);
	}

	bool HasUnavailableMove(
		const FBattleDecisionRequest& Request,
		const TCHAR* Move,
		const EBattleOptionUnavailableReason Reason)
	{
		const FMoveId MoveId = MakeDefinitionId<FMoveId>(Move);
		return Request.GetUnavailableOptions().ContainsByPredicate(
			[MoveId, Reason](const FBattleUnavailableDecisionOption& Option)
			{
				return Option.Kind == EBattleDecisionOptionKind::Move
					&& Option.MoveId == MoveId
					&& Option.Reason == Reason;
			});
	}

	bool RunRemainingVolatileBehaviorMatrix(FAutomationTestBase& Test, const FCatalogFixture& Fixture)
	{
		struct FLockCase
		{
			const TCHAR* Move;
			const TCHAR* Condition;
			EBattleOptionUnavailableReason Reason;
			bool bQuickAttackLegal;
			uint64 BattleValue;
		};
		const FLockCase LockCases[] = {
			{TEXT("Move.Encore"), TEXT("Condition.Encore"), EBattleOptionUnavailableReason::Encored, true, 11290},
			{TEXT("Move.Disable"), TEXT("Condition.Disable"), EBattleOptionUnavailableReason::Disabled, false, 11291}};
		bool bValid = true;
		FString Error;
		for (const FLockCase& LockCase : LockCases)
		{
			const FBattleMoveDefinition* Move = Fixture.Catalog.FindMove(
				MakeDefinitionId<FMoveId>(LockCase.Move));
			if (!Test.TestNotNull(FString(LockCase.Move) + TEXT(" is catalog-backed"), Move)) return false;
			bValid &= Test.TestTrue(FString(LockCase.Move) + TEXT(" carries its exact volatile descriptor"),
				Move->Effects.ContainsByPredicate([&LockCase](const FBattleMoveEffectDescriptor& Effect)
				{
					return Effect.ConditionId == MakeDefinitionId<FConditionId>(LockCase.Condition);
				}));
			FSetupSpec Spec = InteractionSpec(
				LockCase.BattleValue,
				{FName(LockCase.Move), TEXT("Move.Protect")},
				{TEXT("Move.QuickAttack"), TEXT("Move.SwordsDance")});
			FBattleSetup Setup;
			if (!TryBuildSetup(Fixture.Catalog, Spec, Setup, Error))
			{
				Test.AddError(Error);
				return false;
			}
			const FName EffectMove(LockCase.Move);
			const EBattleOptionUnavailableReason ExpectedReason = LockCase.Reason;
			const bool bQuickAttackLegal = LockCase.bQuickAttackLegal;
			const FDriveFunction Drive = [EffectMove, ExpectedReason, bQuickAttackLegal](
				FBattleEngine& Engine, FRunEvidence& Evidence, FString& DriveError)
			{
				if (!RunTurn(Engine, Evidence, DriveError,
						TEXT("Move.Protect"), TEXT("Move.QuickAttack"), true)
					|| !RunTurn(Engine, Evidence, DriveError,
						EffectMove, TEXT("Move.SwordsDance"), true)) return false;
				if (Engine.GetPendingDecisionRequests().IsEmpty())
				{
					FBattleRejection Rejection;
					if (!Engine.TryBeginActionDecisionSequence(Rejection))
					{
						DriveError = TEXT("The post-condition selection sequence did not begin.");
						return false;
					}
				}
				TArray<FBattleDecisionRequest> Requests = Engine.GetPendingDecisionRequests();
				if (Requests.Num() != 1
					|| Requests[0].GetDecisionOwnerTrainerId() != MakeNumericId<FTrainerId>(1))
				{
					DriveError = TEXT("The player request was not first after the condition turn.");
					return false;
				}
				FBattleDecision PlayerDecision;
				if (!TryMakeDecision(Requests[0], FightChoice(TEXT("Move.Protect")), PlayerDecision, DriveError)
					|| !Engine.SubmitDecision(PlayerDecision).WasAccepted())
				{
					DriveError = TEXT("The player could not advance the condition selection probe.");
					return false;
				}
				RecordCheckpoint(Engine, Evidence, TEXT("condition-selection-player"));
				Requests = Engine.GetPendingDecisionRequests();
				if (Requests.Num() != 1
					|| Requests[0].GetDecisionOwnerTrainerId() != MakeNumericId<FTrainerId>(2))
				{
					DriveError = TEXT("The affected opponent request was not exposed.");
					return false;
				}
				const FMoveId QuickAttack = MakeDefinitionId<FMoveId>(TEXT("Move.QuickAttack"));
				const FMoveId SwordsDance = MakeDefinitionId<FMoveId>(TEXT("Move.SwordsDance"));
				const FMoveId ExpectedLegal = bQuickAttackLegal ? QuickAttack : SwordsDance;
				const TCHAR* ExpectedUnavailable = bQuickAttackLegal
					? TEXT("Move.SwordsDance") : TEXT("Move.QuickAttack");
				if (Requests[0].GetLegalMoveIds().Num() != 1
					|| !Requests[0].GetLegalMoveIds().Contains(ExpectedLegal)
					|| !HasUnavailableMove(Requests[0], ExpectedUnavailable, ExpectedReason))
				{
					DriveError = TEXT("The exact Encore or Disable legal-option contract was not exposed.");
					return false;
				}
				RecordCheckpoint(Engine, Evidence, TEXT("condition-selection-affected"));
				return true;
			};
			FRunEvidence Evidence;
			bValid &= RunDeterministicTwins(
				Test, FString(LockCase.Condition) + TEXT(" selection behavior"),
				Fixture.Catalog, Setup, Drive, &Evidence, 5);
			const FBattleEvent* Used = FindMoveUsed(Evidence.Replay, EffectMove);
			bValid &= Test.TestTrue(FString(LockCase.Condition) + TEXT(" mutates status before changing the next public request"),
				Used != nullptr
				&& CountActionEvents(Evidence.Replay, Used->GetActionId(), EBattleEventType::StatusChanged) > 0);
			bValid &= ValidateGlobalInvariants(
				Test, Fixture.Catalog, Evidence, FString(LockCase.Condition) + TEXT(" behavior"));
		}

		const FBattleMoveDefinition* Bite = Fixture.Catalog.FindMove(
			MakeDefinitionId<FMoveId>(TEXT("Move.Bite")));
		if (!Test.TestNotNull(TEXT("Move.Bite is catalog-backed for Flinch"), Bite)) return false;
		bValid &= Test.TestTrue(TEXT("Bite carries the exact Flinch descriptor"),
			Bite->Effects.ContainsByPredicate([](const FBattleMoveEffectDescriptor& Effect)
			{
				return Effect.ConditionId == MakeDefinitionId<FConditionId>(TEXT("Condition.Flinch"))
					&& Effect.ChanceNumerator == 30 && Effect.ChanceDenominator == 100;
			}));
		FSetupSpec FlinchSpec = InteractionSpec(
			11292, {TEXT("Move.Bite")}, {TEXT("Move.SwordsDance")});
		FBattleSetup FlinchSetup;
		if (!TryBuildSetup(Fixture.Catalog, FlinchSpec, FlinchSetup, Error))
		{
			Test.AddError(Error);
			return false;
		}
		const FDriveFunction FlinchDrive = [](FBattleEngine& Engine, FRunEvidence& Evidence, FString& DriveError)
		{
			return RunTurn(Engine, Evidence, DriveError,
				TEXT("Move.Bite"), TEXT("Move.SwordsDance"), false);
		};
		FRunEvidence FlinchEvidence;
		bValid &= RunDeterministicTwins(
			Test, TEXT("Condition.Flinch action denial"), Fixture.Catalog,
			FlinchSetup, FlinchDrive, &FlinchEvidence, 5);
		const FBattleEvent* BiteUsed = FindMoveUsed(FlinchEvidence.Replay, TEXT("Move.Bite"));
		const FBattleEvent* CanceledSwordsDance = FindSourceActionEvent(
			FlinchEvidence.Replay, EBattleEventType::ActionCanceled, 21, TEXT("Move.SwordsDance"));
		const FBattleRandomDraw* SecondaryChance = FlinchEvidence.Replay.GetRandomTrace().FindByPredicate(
			[](const FBattleRandomDraw& Draw)
			{
				return Draw.RulePurpose
					== MakeDefinitionId<FDefinitionId>(TEXT("Rule.C05B.SecondaryChance"));
			});
		const FBattleObservedBattler* Flinched = FlinchEvidence.Replay.GetFinalSnapshot().FindObservedBattler(
			MakeNumericId<FBattlerId>(21));
		int32 AttackStage = 0;
		bValid &= Test.TestTrue(TEXT("Bite's passing secondary draw applies Flinch to the slower target"),
			SecondaryChance != nullptr && SecondaryChance->Result < 30
			&& BiteUsed != nullptr
			&& CountActionEvents(FlinchEvidence.Replay, BiteUsed->GetActionId(), EBattleEventType::StatusChanged) > 0);
		bValid &= Test.TestTrue(TEXT("Flinch cancels exactly the target action and prevents Swords Dance"),
			CanceledSwordsDance != nullptr
			&& CountActionEvents(FlinchEvidence.Replay, CanceledSwordsDance->GetActionId(), EBattleEventType::EffectPrevented) == 1
			&& CountActionEvents(FlinchEvidence.Replay, CanceledSwordsDance->GetActionId(), EBattleEventType::ActionCanceled) == 1
			&& FindMoveUsed(FlinchEvidence.Replay, TEXT("Move.SwordsDance")) == nullptr
			&& Flinched != nullptr && Flinched->StatStages.TryGetStage(EBattleStat::Attack, AttackStage)
			&& AttackStage == 0);
		return bValid && ValidateGlobalInvariants(
			Test, Fixture.Catalog, FlinchEvidence, TEXT("Condition.Flinch behavior"));
	}

	bool RunAbilityItemBehaviorMatrix(FAutomationTestBase& Test, const FCatalogFixture& Fixture)
	{
		bool bValid = true; FString Error;
		FSetupSpec Entry = InteractionSpec(11250, {TEXT("Move.SwordsDance")}, {TEXT("Move.SwordsDance")});
		Entry.Battlers.Add({1, 12, 1, TEXT("Species.Gyarados"), TEXT("Nature.Adamant"), TEXT("Ability.Intimidate"), {TEXT("Move.Bite")}});
		Entry.Battlers.Add({2, 22, 1, TEXT("Species.Pelipper"), TEXT("Nature.Modest"), TEXT("Ability.Drizzle"), {TEXT("Move.RainDance")}});
		FBattleSetup EntrySetup;
		if (!TryBuildSetup(Fixture.Catalog, Entry, EntrySetup, Error)) { Test.AddError(Error); return false; }
		const FChoiceProvider Switches = [](const FBattleDecisionRequest& Request, FChoice& Out, FString&)
		{
			Out.Kind = EChoiceKind::Switch; Out.PartyTarget = MakePartySlotId(1); Out.ActiveTarget = Request.GetActingSlotId(); return true;
		};
		const FDriveFunction EntryDrive = [Switches](FBattleEngine& Engine, FRunEvidence& Evidence, FString& DriveError)
		{
			return LockTurn(Engine, Switches, Evidence, DriveError) && ExecuteLockedQueue(Engine, Evidence, DriveError);
		};
		FRunEvidence EntryEvidence;
		bValid &= RunDeterministicTwins(Test, TEXT("Intimidate and Drizzle entry"), Fixture.Catalog, EntrySetup, EntryDrive, &EntryEvidence);
		bValid &= Test.TestTrue(TEXT("Intimidate activates through a public switch"), HasSourceDefinition(EntryEvidence.Replay, EBattleEventType::AbilityActivated, TEXT("Ability.Intimidate"))
			&& HasEvent(EntryEvidence.Replay, EBattleEventType::StatStageChanged));
		bValid &= Test.TestTrue(TEXT("Drizzle activates through a public switch"), HasSourceDefinition(EntryEvidence.Replay, EBattleEventType::AbilityActivated, TEXT("Ability.Drizzle"))
			&& EntryEvidence.Replay.GetFinalSnapshot().GetWeather().IsSet()
			&& EntryEvidence.Replay.GetFinalSnapshot().GetWeather()->ConditionId == MakeDefinitionId<FConditionId>(TEXT("Condition.Rain")));

		FSetupSpec Residual = InteractionSpec(11251, {TEXT("Move.SwordsDance")}, {TEXT("Move.Sandstorm")}, TEXT("Ability.SpeedBoost"), TEXT("Ability.MagicGuard"));
		Residual.Battlers[0].SpeciesId = TEXT("Species.Espathra"); Residual.Battlers[1].SpeciesId = TEXT("Species.Clefable");
		FBattleSetup ResidualSetup;
		if (!TryBuildSetup(Fixture.Catalog, Residual, ResidualSetup, Error)) { Test.AddError(Error); return false; }
		const FDriveFunction ResidualDrive = [](FBattleEngine& Engine, FRunEvidence& Evidence, FString& DriveError)
		{
			return RunTurn(Engine, Evidence, DriveError, TEXT("Move.SwordsDance"), TEXT("Move.Sandstorm"), true);
		};
		FRunEvidence ResidualEvidence;
		bValid &= RunDeterministicTwins(Test, TEXT("Speed Boost and Magic Guard residual"), Fixture.Catalog, ResidualSetup, ResidualDrive, &ResidualEvidence);
		const FBattleObservedBattler* SpeedUser = ResidualEvidence.Replay.GetFinalSnapshot().FindObservedBattler(MakeNumericId<FBattlerId>(11));
		int32 SpeedStage = 0;
		bValid &= Test.TestTrue(TEXT("Speed Boost raises Speed after one active turn"), SpeedUser != nullptr && SpeedUser->StatStages.TryGetStage(EBattleStat::Speed, SpeedStage)
			&& SpeedStage == 1 && HasSourceDefinition(ResidualEvidence.Replay, EBattleEventType::AbilityActivated, TEXT("Ability.SpeedBoost")));
		const FBattlePartyEntrySetup* MagicBefore = ResidualSetup.FindBattler(MakeNumericId<FBattlerId>(21));
		const FBattlePartyEntrySetup* MagicAfter = ResidualEvidence.Replay.GetFinalSnapshot().FindBattler(MakeNumericId<FBattlerId>(21));
		bValid &= Test.TestTrue(TEXT("Magic Guard prevents Sandstorm HP loss"), MagicBefore != nullptr && MagicAfter != nullptr
			&& MagicBefore->CurrentHP == MagicAfter->CurrentHP && HasSourceDefinition(ResidualEvidence.Replay, EBattleEventType::AbilityActivated, TEXT("Ability.MagicGuard")));

		struct FItemCase { const TCHAR* Label; const TCHAR* Item; const TCHAR* PlayerMove; const TCHAR* OpponentMove; int32 PlayerHP; int32 PlayerLevel; int32 OpponentLevel; EBattleEventType Fact; };
		const FItemCase Items[] = {
			{TEXT("Leftovers"), TEXT("Item.Leftovers"), TEXT("Move.SwordsDance"), TEXT("Move.SwordsDance"), 40, 50, 50, EBattleEventType::Healing},
			{TEXT("Sitrus Berry"), TEXT("Item.SitrusBerry"), TEXT("Move.SwordsDance"), TEXT("Move.QuickAttack"), 50, 50, 50, EBattleEventType::ItemConsumed},
			{TEXT("Lum Berry"), TEXT("Item.LumBerry"), TEXT("Move.SwordsDance"), TEXT("Move.WillOWisp"), INDEX_NONE, 50, 50, EBattleEventType::ItemConsumed},
			{TEXT("Focus Sash"), TEXT("Item.FocusSash"), TEXT("Move.SwordsDance"), TEXT("Move.Flamethrower"), INDEX_NONE, 1, 100, EBattleEventType::ItemConsumed},
			{TEXT("Life Orb"), TEXT("Item.LifeOrb"), TEXT("Move.Flamethrower"), TEXT("Move.SwordsDance"), INDEX_NONE, 50, 50, EBattleEventType::ItemActivated},
			{TEXT("Choice Band"), TEXT("Item.ChoiceBand"), TEXT("Move.Bite"), TEXT("Move.SwordsDance"), INDEX_NONE, 50, 50, EBattleEventType::ItemActivated}};
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Items); ++Index)
		{
			const FItemCase& Item = Items[Index];
			TArray<FName> PlayerMoves{FName(Item.PlayerMove)};
			if (PlayerMoves[0] != FName(TEXT("Move.SwordsDance"))) PlayerMoves.Add(TEXT("Move.SwordsDance"));
			FSetupSpec Spec = InteractionSpec(11260 + Index, PlayerMoves, {FName(Item.OpponentMove)}, TEXT("Ability.Blaze"), TEXT("Ability.MagicGuard"), FName(Item.Item));
			Spec.Battlers[0].CurrentHP = Item.PlayerHP; Spec.Battlers[0].Level = Item.PlayerLevel; Spec.Battlers[1].Level = Item.OpponentLevel;
			if (FName(Item.Item) == FName(TEXT("Item.LumBerry"))) { Spec.Battlers[0].SpeciesId = TEXT("Species.Clefable"); Spec.Battlers[0].AbilityId = TEXT("Ability.MagicGuard"); }
			if (FName(Item.Item) == FName(TEXT("Item.FocusSash"))) { Spec.Battlers[0].SpeciesId = TEXT("Species.Venusaur"); Spec.Battlers[0].AbilityId = TEXT("Ability.Overgrow"); Spec.Battlers[1].SpeciesId = TEXT("Species.Charizard"); Spec.Battlers[1].AbilityId = TEXT("Ability.Blaze"); }
			FBattleSetup Setup;
			if (!TryBuildSetup(Fixture.Catalog, Spec, Setup, Error)) { Test.AddError(Error); return false; }
			const FName PlayerMove(Item.PlayerMove), OpponentMove(Item.OpponentMove);
			const bool bChoiceBand = FName(Item.Item) == FName(TEXT("Item.ChoiceBand"));
			const bool bFocusSash = FName(Item.Item) == FName(TEXT("Item.FocusSash"));
			const FDriveFunction Drive = [PlayerMove, OpponentMove, bChoiceBand, bFocusSash](FBattleEngine& Engine, FRunEvidence& Evidence, FString& DriveError)
			{
				if (!RunTurn(Engine, Evidence, DriveError, PlayerMove, OpponentMove, !bFocusSash)) return false;
				if (!bChoiceBand) return true;
				if (Engine.GetPendingDecisionRequests().IsEmpty())
				{
					FBattleRejection Rejection;
					if (!Engine.TryBeginActionDecisionSequence(Rejection)) { DriveError = TEXT("Choice Band next selection did not begin."); return false; }
				}
				const TArray<FBattleDecisionRequest> Requests = Engine.GetPendingDecisionRequests();
				const FBattleDecisionRequest* PlayerRequest = Requests.FindByPredicate([](const FBattleDecisionRequest& Request)
				{
					return Request.GetActingBattlerId() == MakeNumericId<FBattlerId>(11);
				});
				FMoveId ExpectedMove;
				if (!FMoveId::TryCreate(PlayerMove, ExpectedMove)) { DriveError = TEXT("Choice Band expected move ID is invalid."); return false; }
				if (PlayerRequest == nullptr || !PlayerRequest->GetLegalMoveIds().Contains(ExpectedMove)
					|| PlayerRequest->GetLegalMoveIds().Contains(MakeDefinitionId<FMoveId>(TEXT("Move.SwordsDance"))))
				{
					const FString ActualMove = PlayerRequest == nullptr || PlayerRequest->GetLegalMoveIds().IsEmpty() ? TEXT("none")
						: PlayerRequest->GetLegalMoveIds()[0].GetDefinitionId().GetName().ToString();
					DriveError = FString::Printf(TEXT("Choice Band next-selection mismatch: request=%d legal=%d expected=%s actual=%s swords=%d."), PlayerRequest != nullptr,
						PlayerRequest == nullptr ? -1 : PlayerRequest->GetLegalMoveIds().Num(), *PlayerMove.ToString(), *ActualMove,
						PlayerRequest != nullptr && PlayerRequest->GetLegalMoveIds().Contains(MakeDefinitionId<FMoveId>(TEXT("Move.SwordsDance")))); return false;
				}
				RecordCheckpoint(Engine, Evidence, TEXT("choice-band-selection")); return true;
			};
			FRunEvidence Evidence;
			bValid &= RunDeterministicTwins(Test, Item.Label, Fixture.Catalog, Setup, Drive, &Evidence, 5);
			const FBattlePartyEntrySetup* Final = Evidence.Replay.GetFinalSnapshot().FindBattler(MakeNumericId<FBattlerId>(11));
			if (bChoiceBand) bValid &= Test.TestTrue(TEXT("Choice Band remains visible in the final public snapshot without a synthetic activation event"),
				Final != nullptr && Final->CurrentHeldItemId == MakeDefinitionId<FItemId>(TEXT("Item.ChoiceBand"))
				&& !HasSourceDefinition(Evidence.Replay, EBattleEventType::ItemActivated, TEXT("Item.ChoiceBand")));
			else bValid &= Test.TestTrue(FString(Item.Label) + TEXT(" publishes its canonical public item fact"), HasEvent(Evidence.Replay, Item.Fact));
			if (FName(Item.Item) == FName(TEXT("Item.LumBerry")))
			{
				const FBattleObservedBattler* Observation = Evidence.Replay.GetFinalSnapshot().FindObservedBattler(
					MakeNumericId<FBattlerId>(11));
				bValid &= Test.TestTrue(TEXT("Lum Berry cures the applied Burn"),
					Final != nullptr && Observation != nullptr && !Observation->MajorStatusId.IsValid());
			}
			if (FName(Item.Item) == FName(TEXT("Item.FocusSash")))
			{
				bValid &= Test.TestNotNull(TEXT("Focus Sash holder remains in the public snapshot"), Final);
				if (Final != nullptr) bValid &= Test.TestEqual(TEXT("Focus Sash leaves its full-HP holder at exactly one HP"), Final->CurrentHP, 1);
			}
		}
		return bValid;
	}

	bool RunHeldItemMoveMatrix(FAutomationTestBase& Test, const FCatalogFixture& Fixture)
	{
		struct FCase
		{
			const TCHAR* Move;
			FName PlayerItem;
			FName OpponentItem;
			EBattleEventType Event;
		};
		const FCase Cases[] = {
			{TEXT("Move.KnockOff"), NAME_None, TEXT("Item.Leftovers"), EBattleEventType::ItemRemoved},
			{TEXT("Move.Trick"), TEXT("Item.ChoiceBand"), TEXT("Item.Leftovers"), EBattleEventType::ItemTransferred},
			{TEXT("Move.Thief"), NAME_None, TEXT("Item.Leftovers"), EBattleEventType::ItemTransferred}};
		bool bValid = true;
		FString Error;
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Cases); ++Index)
		{
			const FCase& ItemCase = Cases[Index];
			FSetupSpec Spec = InteractionSpec(
				11280 + Index, {FName(ItemCase.Move)}, {TEXT("Move.SwordsDance")},
				TEXT("Ability.Blaze"), TEXT("Ability.MagicGuard"),
				ItemCase.PlayerItem, ItemCase.OpponentItem);
			FBattleSetup Setup;
			if (!TryBuildSetup(Fixture.Catalog, Spec, Setup, Error))
			{
				Test.AddError(Error);
				return false;
			}
			const FName Move(ItemCase.Move);
			const FDriveFunction Drive = [Move](
				FBattleEngine& Engine, FRunEvidence& Evidence, FString& DriveError)
			{
				return RunTurn(Engine, Evidence, DriveError, Move, TEXT("Move.SwordsDance"), false);
			};
			FRunEvidence Evidence;
			bValid &= RunDeterministicTwins(
				Test, FString(ItemCase.Move) + TEXT(" production item intent"),
				Fixture.Catalog, Setup, Drive, &Evidence, 5);
			const FBattlePartyEntrySetup* Player = Evidence.Replay.GetFinalSnapshot().FindBattler(
				MakeNumericId<FBattlerId>(11));
			const FBattlePartyEntrySetup* Opponent = Evidence.Replay.GetFinalSnapshot().FindBattler(
				MakeNumericId<FBattlerId>(21));
			const int32 ExpectedMutationEvents = Move == FName(TEXT("Move.Trick")) ? 2 : 1;
			bValid &= Test.TestEqual(FString(ItemCase.Move) + TEXT(" publishes the exact typed item mutation count"),
				CountEvents(Evidence.Replay, ItemCase.Event), ExpectedMutationEvents);
			if (Move == FName(TEXT("Move.Trick")))
			{
				bValid &= Test.TestTrue(TEXT("Trick swaps both production held items atomically"),
					Player != nullptr && Opponent != nullptr
					&& Player->CurrentHeldItemId == MakeDefinitionId<FItemId>(TEXT("Item.Leftovers"))
					&& Opponent->CurrentHeldItemId == MakeDefinitionId<FItemId>(TEXT("Item.ChoiceBand")));
			}
			else
			{
				bValid &= Test.TestTrue(FString(ItemCase.Move) + TEXT(" leaves the exact public ledger ownership"),
					Player != nullptr && Opponent != nullptr && !Opponent->CurrentHeldItemId.IsValid()
					&& (Move == FName(TEXT("Move.KnockOff"))
						? !Player->CurrentHeldItemId.IsValid()
						: Player->CurrentHeldItemId == MakeDefinitionId<FItemId>(TEXT("Item.Leftovers"))));
			}
			bValid &= ValidateGlobalInvariants(Test, Fixture.Catalog, Evidence, ItemCase.Move);
		}

		FSetupSpec RecycleSpec = InteractionSpec(
			11283, {TEXT("Move.SwordsDance"), TEXT("Move.Recycle")},
			{TEXT("Move.WillOWisp"), TEXT("Move.SwordsDance")},
			TEXT("Ability.MagicGuard"), TEXT("Ability.MagicGuard"), TEXT("Item.LumBerry"));
		RecycleSpec.Battlers[0].SpeciesId = TEXT("Species.Clefable");
		FBattleSetup RecycleSetup;
		if (!TryBuildSetup(Fixture.Catalog, RecycleSpec, RecycleSetup, Error))
		{
			Test.AddError(Error);
			return false;
		}
		const FDriveFunction RecycleDrive = [](FBattleEngine& Engine, FRunEvidence& Evidence, FString& DriveError)
		{
			return RunTurn(Engine, Evidence, DriveError,
				TEXT("Move.SwordsDance"), TEXT("Move.WillOWisp"), true)
				&& RunTurn(Engine, Evidence, DriveError,
					TEXT("Move.Recycle"), TEXT("Move.SwordsDance"), false);
		};
		FRunEvidence RecycleEvidence;
		bValid &= RunDeterministicTwins(
			Test, TEXT("Move.Recycle production item intent"), Fixture.Catalog,
			RecycleSetup, RecycleDrive, &RecycleEvidence, 5);
		const FBattlePartyEntrySetup* Recycled = RecycleEvidence.Replay.GetFinalSnapshot().FindBattler(
			MakeNumericId<FBattlerId>(11));
		const FBattleObservedBattler* RecycledObservation =
			RecycleEvidence.Replay.GetFinalSnapshot().FindObservedBattler(MakeNumericId<FBattlerId>(11));
		bValid &= Test.TestTrue(TEXT("Lum Berry is consumed before Recycle and Recycle restores that exact item"),
			CountEvents(RecycleEvidence.Replay, EBattleEventType::ItemConsumed) == 1
			&& CountEvents(RecycleEvidence.Replay, EBattleEventType::ItemRestored) == 1
			&& Recycled != nullptr
			&& Recycled->CurrentHeldItemId == MakeDefinitionId<FItemId>(TEXT("Item.LumBerry"))
			&& RecycledObservation != nullptr && !RecycledObservation->MajorStatusId.IsValid());
		bValid &= ValidateGlobalInvariants(Test, Fixture.Catalog, RecycleEvidence, TEXT("Move.Recycle"));
		return bValid;
	}
}

using namespace BattleCanonicalSingleEffectsPrivate;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FC11AStatusVolatileCrossInteractions,
	"PokemonSolarus.Battle.C11A.Single.Status.AllMajorVolatileAndCrossInteractions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FC11AStatusVolatileCrossInteractions::RunTest(const FString& Parameters)
{
	FCatalogFixture Fixture;
	FString Error;
	if (!TryLoadProductionFixture(*this, Fixture, Error))
	{
		AddError(Error);
		return false;
	}
	TestTrue(TEXT("The catalog manifest is exact before the move sweep"),
		ValidateCoverageManifest(*this, Fixture.Catalog));
	return RunMoveSweep(*this, Fixture)
		&& RunMajorStatusMatrix(*this, Fixture)
		&& RunVolatileCrossMatrix(*this, Fixture)
		&& RunRemainingVolatileBehaviorMatrix(*this, Fixture);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FC11ABagAbilityItemIntegration,
	"PokemonSolarus.Battle.C11A.Single.BagAbilityItem.ActionsTriggersOwnershipCleanupAndBlockedPaths",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FC11ABagAbilityItemIntegration::RunTest(const FString& Parameters)
{
	FCatalogFixture Fixture;
	FString Error;
	if (!TryLoadProductionFixture(*this, Fixture, Error))
	{
		AddError(Error);
		return false;
	}
	struct FBagCase
	{
		const TCHAR* Item;
		int32 CurrentHP;
		bool bReserveTarget;
		EBattleEventType ExpectedEffect;
	};
	const FBagCase Cases[] = {
		{TEXT("Item.HyperPotion"), 1, false, EBattleEventType::Healing},
		{TEXT("Item.Revive"), INDEX_NONE, true, EBattleEventType::Healing},
		{TEXT("Item.FullHeal"), INDEX_NONE, false, EBattleEventType::StatusChanged},
		{TEXT("Item.XAttack"), INDEX_NONE, false, EBattleEventType::StatStageChanged}};
	bool bValid = true;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Cases); ++Index)
	{
		FBattleSetup Setup;
		if (!TryBuildSetup(Fixture.Catalog,
			BagSpec(11200 + Index, Cases[Index].Item, Cases[Index].CurrentHP), Setup, Error))
		{
			AddError(Error);
			return false;
		}
		const FName ItemName(Cases[Index].Item);
		const bool bReserve = Cases[Index].bReserveTarget;
		const FChoiceProvider Provider = [ItemName, bReserve](
			const FBattleDecisionRequest& Request, FChoice& Out, FString&)
		{
			if (Request.GetDecisionOwnerTrainerId() == MakeNumericId<FTrainerId>(1))
			{
				Out.Kind = EChoiceKind::Bag;
				Out.DefinitionId = ItemName;
				if (ItemName == FName(TEXT("Item.XAttack")))
				{
					Out.ActiveTarget = MakeActiveSlotId(EBattleSide::Player, EBattlePosition::Left);
				}
				else
				{
					Out.PartyTarget = MakePartySlotId(bReserve ? 1 : 0);
				}
			}
			else Out = FightChoice(FName(TEXT("Move.SwordsDance")));
			return true;
		};
		const bool bFullHeal = ItemName == FName(TEXT("Item.FullHeal"));
		const FDriveFunction Drive = [Provider, bFullHeal](
			FBattleEngine& Engine, FRunEvidence& Evidence, FString& DriveError)
		{
			if (bFullHeal)
			{
				const FChoiceProvider Inflict = [](const FBattleDecisionRequest& Request, FChoice& Out, FString&)
				{
					Out = Request.GetDecisionOwnerTrainerId() == MakeNumericId<FTrainerId>(1)
						? FightChoice(TEXT("Move.SwordsDance"))
						: FightChoice(TEXT("Move.WillOWisp"), MakeActiveSlotId(EBattleSide::Player, EBattlePosition::Left));
					return true;
				};
				if (!LockTurn(Engine, Inflict, Evidence, DriveError) || !ExecuteLockedQueue(Engine, Evidence, DriveError)
					|| !ResolveEndTurn(Engine, Evidence, DriveError)) return false;
				const FBattleObservedBattler* Burned = Engine.GetSnapshot().FindObservedBattler(MakeNumericId<FBattlerId>(11));
				if (Burned == nullptr || Burned->MajorStatusId != MakeDefinitionId<FConditionId>(TEXT("Condition.Burn")))
				{
					DriveError = TEXT("Full Heal precondition did not reach Burn through Will-O-Wisp."); return false;
				}
			}
			return LockTurn(Engine, Provider, Evidence, DriveError)
				&& ExecuteLockedQueue(Engine, Evidence, DriveError);
		};
		FRunEvidence Evidence;
		const FString Label(Cases[Index].Item);
		bValid &= RunDeterministicTwins(*this, Label, Fixture.Catalog, Setup, Drive, &Evidence);
		bValid &= TestTrue(Label + TEXT(" emits ItemUsed"), HasEvent(Evidence.Replay, EBattleEventType::ItemUsed));
		bValid &= TestTrue(Label + TEXT(" consumes exactly one Bag item"),
			CountEvents(Evidence.Replay, EBattleEventType::ItemConsumed) == 1);
		bValid &= TestTrue(Label + TEXT(" publishes its typed effect"),
			HasEvent(Evidence.Replay, Cases[Index].ExpectedEffect));
		const FBattleTrainerSetup* FinalTrainer = Evidence.Replay.GetFinalSnapshot().GetTrainers().FindByPredicate(
			[](const FBattleTrainerSetup& Trainer) { return Trainer.TrainerId == MakeNumericId<FTrainerId>(1); });
		const FBattleBagItemCount* FinalItem = FinalTrainer == nullptr ? nullptr : FinalTrainer->Bag.FindByPredicate(
			[ItemName](const FBattleBagItemCount& Item) { return Item.ItemId == MakeDefinitionId<FItemId>(*ItemName.ToString()); });
		bValid &= TestTrue(Label + TEXT(" decrements only its public Bag count"), FinalItem != nullptr && FinalItem->Count == 1);
		if (bFullHeal)
		{
			const FBattleObservedBattler* Cured = Evidence.Replay.GetFinalSnapshot().FindObservedBattler(MakeNumericId<FBattlerId>(11));
			bValid &= TestTrue(TEXT("Full Heal cures the reached major status"), Cured != nullptr && !Cured->MajorStatusId.IsValid());
		}
		bValid &= ValidateGlobalInvariants(*this, Fixture.Catalog, Evidence, Label);
	}
	return bValid
		&& RunAbilityItemBehaviorMatrix(*this, Fixture)
		&& RunHeldItemMoveMatrix(*this, Fixture);
}

#endif // WITH_DEV_AUTOMATION_TESTS
