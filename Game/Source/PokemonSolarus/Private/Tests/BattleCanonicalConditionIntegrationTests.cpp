#if WITH_DEV_AUTOMATION_TESTS

#include "BattleCanonicalIntegrationTestSupport.h"

#include "Misc/AutomationTest.h"

namespace BattleCanonicalConditionPrivate
{
	using namespace BattleCanonicalIntegrationTestSupport;

	FSetupSpec ConditionSpec(
		const uint64 BattleValue,
		const TArray<FName>& PlayerMoves,
		const TArray<FName>& OpponentMoves,
		const bool bOpponentReserve = false,
		const FName ReserveItem = NAME_None)
	{
		FSetupSpec Spec;
		Spec.BattleValue = BattleValue;
		Spec.EncounterKind = EBattleEncounterKind::Trainer;
		Spec.Format = EBattleFormat::Single;
		Spec.Policies.bBagAllowed = false;
		Spec.Policies.bShiftPromptEligible = false;
		Spec.Trainers = {
			{1, EBattleSide::Player, EBattleTrainerRole::Player, EBattleDecisionController::Human, {}},
			{2, EBattleSide::Opponent, EBattleTrainerRole::Opponent, EBattleDecisionController::EnemyAI, {}}};
		FBattlerSpec Player;
		Player.TrainerValue = 1;
		Player.BattlerValue = 11;
		Player.SpeciesId = FName(TEXT("Species.Charizard"));
		Player.AbilityId = FName(TEXT("Ability.Blaze"));
		Player.MoveIds = PlayerMoves;
		Player.EffortValues.Speed = 252;
		Spec.Battlers.Add(Player);
		FBattlerSpec Opponent;
		Opponent.TrainerValue = 2;
		Opponent.BattlerValue = 21;
		Opponent.SpeciesId = FName(TEXT("Species.Venusaur"));
		Opponent.AbilityId = FName(TEXT("Ability.Overgrow"));
		Opponent.MoveIds = OpponentMoves;
		Opponent.OriginalHeldItemId = FName(TEXT("Item.Leftovers"));
		Opponent.CurrentHeldItemId = Opponent.OriginalHeldItemId;
		Spec.Battlers.Add(Opponent);
		Spec.Active = {
			{EBattleSide::Player, EBattlePosition::Left, 1, 11},
			{EBattleSide::Opponent, EBattlePosition::Left, 2, 21}};
		if (bOpponentReserve)
		{
			FBattlerSpec Reserve;
			Reserve.TrainerValue = 2;
			Reserve.BattlerValue = 22;
			Reserve.PartyIndex = 1;
			Reserve.SpeciesId = FName(TEXT("Species.Rotom"));
			Reserve.AbilityId = FName(TEXT("Ability.Levitate"));
			Reserve.MoveIds = {FName(TEXT("Move.SwordsDance"))};
			Reserve.OriginalHeldItemId = ReserveItem;
			Reserve.CurrentHeldItemId = ReserveItem;
			Spec.Battlers.Add(Reserve);
		}
		return Spec;
	}

	FChoice Fight(const FName Move, const EBattleSide TargetSide)
	{
		FChoice Choice;
		Choice.Kind = EChoiceKind::Fight;
		Choice.DefinitionId = Move;
		Choice.ActiveTarget = MakeActiveSlotId(TargetSide, EBattlePosition::Left);
		return Choice;
	}

	bool RunTurns(
		FBattleEngine& Engine,
		FRunEvidence& Evidence,
		FString& Error,
		const TArray<TPair<FName, FName>>& Turns,
		const bool bResolveLastEndTurn = true)
	{
		for (int32 Turn = 0; Turn < Turns.Num(); ++Turn)
		{
			const FName PlayerMove = Turns[Turn].Key;
			const FName OpponentMove = Turns[Turn].Value;
			const FChoiceProvider Provider = [PlayerMove, OpponentMove](
				const FBattleDecisionRequest& Request, FChoice& Out, FString&)
			{
				const bool bPlayer = Request.GetDecisionOwnerTrainerId() == MakeNumericId<FTrainerId>(1);
				Out = Fight(bPlayer ? PlayerMove : OpponentMove,
					bPlayer ? EBattleSide::Opponent : EBattleSide::Player);
				return true;
			};
			if (!LockTurn(Engine, Provider, Evidence, Error)
				|| !ExecuteLockedQueue(Engine, Evidence, Error)) return false;
			if (Engine.GetSnapshot().GetPhase() == EBattlePhase::EndOfTurn
				&& (bResolveLastEndTurn || Turn + 1 < Turns.Num())
				&& !ResolveEndTurn(Engine, Evidence, Error)) return false;
			if (Engine.GetSnapshot().GetPhase() == EBattlePhase::Terminal) break;
		}
		return true;
	}

	bool Build(
		FAutomationTestBase& Test,
		const FSetupSpec& Spec,
		FCatalogFixture& Fixture,
		FBattleSetup& Setup)
	{
		FString Error;
		if (!TryLoadProductionFixture(Test, Fixture, Error)
			|| !TryBuildSetup(Fixture.Catalog, Spec, Setup, Error))
		{
			Test.AddError(Error);
			return false;
		}
		return true;
	}

	bool SnapshotContainsCondition(const FBattleSnapshot& Snapshot, const TCHAR* Id)
	{
		const FConditionId ConditionId = MakeDefinitionId<FConditionId>(Id);
		if (Snapshot.GetWeather().IsSet() && Snapshot.GetWeather()->ConditionId == ConditionId) return true;
		if (Snapshot.GetTerrain().IsSet() && Snapshot.GetTerrain()->ConditionId == ConditionId) return true;
		if (Snapshot.GetRooms().ContainsByPredicate([ConditionId](const FBattleObservedCondition& C) { return C.ConditionId == ConditionId; })) return true;
		for (const FBattleObservedSide& Side : Snapshot.GetObservedSides())
		{
			if (Side.Conditions.ContainsByPredicate([ConditionId](const FBattleObservedCondition& C) { return C.ConditionId == ConditionId; })
				|| Side.Hazards.ContainsByPredicate([ConditionId](const FBattleObservedCondition& C) { return C.ConditionId == ConditionId; })) return true;
		}
		return false;
	}

	const FBattleObservedCondition* FindSnapshotCondition(const FBattleSnapshot& Snapshot, const TCHAR* Id)
	{
		const FConditionId ConditionId = MakeDefinitionId<FConditionId>(Id);
		if (Snapshot.GetWeather().IsSet() && Snapshot.GetWeather()->ConditionId == ConditionId) return &Snapshot.GetWeather().GetValue();
		if (Snapshot.GetTerrain().IsSet() && Snapshot.GetTerrain()->ConditionId == ConditionId) return &Snapshot.GetTerrain().GetValue();
		if (const FBattleObservedCondition* Room = Snapshot.GetRooms().FindByPredicate([ConditionId](const FBattleObservedCondition& C) { return C.ConditionId == ConditionId; })) return Room;
		for (const FBattleObservedSide& Side : Snapshot.GetObservedSides())
		{
			if (const FBattleObservedCondition* Condition = Side.Conditions.FindByPredicate([ConditionId](const FBattleObservedCondition& C) { return C.ConditionId == ConditionId; })) return Condition;
			if (const FBattleObservedCondition* Hazard = Side.Hazards.FindByPredicate([ConditionId](const FBattleObservedCondition& C) { return C.ConditionId == ConditionId; })) return Hazard;
		}
		return nullptr;
	}

	uint64 FindResidualOrdinal(const FBattleReplayRecord& Record, const TCHAR* SourceId, const EBattleEventType Type)
	{
		const FDefinitionId Id = MakeDefinitionId<FDefinitionId>(SourceId);
		uint64 Found = 0;
		for (const FBattleResolution& Resolution : Record.GetResolutions())
			for (const FBattleEvent& Event : Resolution.GetEvents())
				if (Event.GetCauseActionKind() == EBattleActionKind::Residual && Event.GetType() == Type && Event.GetSource().DefinitionId == Id) Found = Event.GetEventOrdinal();
		return Found;
	}

	int32 CurrentHP(const FBattleSnapshot& Snapshot, const uint64 BattlerValue)
	{
		const FBattlePartyEntrySetup* Battler = Snapshot.FindBattler(
			MakeNumericId<FBattlerId>(BattlerValue));
		return Battler == nullptr ? INDEX_NONE : Battler->CurrentHP;
	}

	int32 StartingHP(const FBattleSetup& Setup, const uint64 BattlerValue)
	{
		const FBattlePartyEntrySetup* Battler = Setup.FindBattler(
			MakeNumericId<FBattlerId>(BattlerValue));
		return Battler == nullptr ? INDEX_NONE : Battler->CurrentHP;
	}

	int32 DamageTaken(
		const FBattleSetup& Setup,
		const FRunEvidence& Evidence,
		const uint64 BattlerValue)
	{
		return StartingHP(Setup, BattlerValue)
			- CurrentHP(Evidence.Replay.GetFinalSnapshot(), BattlerValue);
	}

	bool RunScenario(
		FAutomationTestBase& Test,
		const FCatalogFixture& Fixture,
		const FString& Label,
		const FSetupSpec& Spec,
		const TArray<TPair<FName, FName>>& Turns,
		FBattleSetup& OutSetup,
		FRunEvidence& OutEvidence,
		const bool bResolveLastEndTurn = true)
	{
		FString Error;
		if (!TryBuildSetup(Fixture.Catalog, Spec, OutSetup, Error))
		{
			Test.AddError(Label + TEXT(" setup: ") + Error);
			return false;
		}
		const FDriveFunction Drive = [Turns, bResolveLastEndTurn](
			FBattleEngine& Engine,
			FRunEvidence& Evidence,
			FString& DriveError)
		{
			return RunTurns(
				Engine,
				Evidence,
				DriveError,
				Turns,
				bResolveLastEndTurn);
		};
		return RunDeterministicTwins(
				Test,
				Label,
				Fixture.Catalog,
				OutSetup,
				Drive,
				&OutEvidence)
			&& ValidateGlobalInvariants(
				Test,
				Fixture.Catalog,
				OutEvidence,
				Label);
	}

	void SetPlayerSpecies(
		FSetupSpec& Spec,
		const FName SpeciesId,
		const FName AbilityId)
	{
		Spec.Battlers[0].SpeciesId = SpeciesId;
		Spec.Battlers[0].AbilityId = AbilityId;
	}

	bool ComparePlayerDamage(
		FAutomationTestBase& Test,
		const FCatalogFixture& Fixture,
		const FString& Label,
		const FSetupSpec& ActiveSpec,
		const TArray<TPair<FName, FName>>& ActiveTurns,
		const FSetupSpec& ControlSpec,
		const TArray<TPair<FName, FName>>& ControlTurns,
		const bool bActiveDamageLower)
	{
		FBattleSetup ActiveSetup;
		FBattleSetup ControlSetup;
		FRunEvidence ActiveEvidence;
		FRunEvidence ControlEvidence;
		if (!RunScenario(
				Test,
				Fixture,
				Label + TEXT(" active"),
				ActiveSpec,
				ActiveTurns,
				ActiveSetup,
				ActiveEvidence)
			|| !RunScenario(
				Test,
				Fixture,
				Label + TEXT(" control"),
				ControlSpec,
				ControlTurns,
				ControlSetup,
				ControlEvidence))
		{
			return false;
		}
		const int32 ActiveDamage = DamageTaken(ActiveSetup, ActiveEvidence, 11);
		const int32 ControlDamage = DamageTaken(ControlSetup, ControlEvidence, 11);
		return Test.TestTrue(
			FString::Printf(
				TEXT("%s has distinguishable engine HP deltas (%d active, %d control)"),
				*Label,
				ActiveDamage,
				ControlDamage),
			ActiveDamage > 0
				&& ControlDamage > 0
				&& (bActiveDamageLower
					? ActiveDamage < ControlDamage
					: ActiveDamage > ControlDamage));
	}

	bool RunStickyWebMistCase(
		FAutomationTestBase& Test,
		const FCatalogFixture& Fixture,
		const bool bMistActive,
		int32& OutSpeedStage)
	{
		FSetupSpec Spec = ConditionSpec(
			bMistActive ? 11458 : 11459,
			{TEXT("Move.StickyWeb"), TEXT("Move.SwordsDance")},
			{bMistActive ? FName(TEXT("Move.Mist")) : FName(TEXT("Move.SwordsDance"))},
			true);
		Spec.Battlers[2].SpeciesId = TEXT("Species.Espathra");
		Spec.Battlers[2].AbilityId = TEXT("Ability.SpeedBoost");
		Spec.Battlers[2].CurrentHeldItemId = NAME_None;
		Spec.Battlers[2].OriginalHeldItemId = NAME_None;
		FBattleSetup Setup;
		FString Error;
		if (!TryBuildSetup(Fixture.Catalog, Spec, Setup, Error))
		{
			Test.AddError(Error);
			return false;
		}
		const FName OpponentSetupMove = bMistActive
			? FName(TEXT("Move.Mist"))
			: FName(TEXT("Move.SwordsDance"));
		const FDriveFunction Drive = [OpponentSetupMove, &OutSpeedStage](
			FBattleEngine& Engine,
			FRunEvidence& Evidence,
			FString& DriveError)
		{
			if (!RunTurns(
					Engine,
					Evidence,
					DriveError,
					{{TEXT("Move.StickyWeb"), OpponentSetupMove}}))
			{
				return false;
			}
			const FChoiceProvider SwitchProvider = [](
				const FBattleDecisionRequest& Request,
				FChoice& Out,
				FString&)
			{
				if (Request.GetDecisionOwnerTrainerId()
					== MakeNumericId<FTrainerId>(2))
				{
					Out.Kind = EChoiceKind::Switch;
					Out.PartyTarget = MakePartySlotId(1);
					Out.ActiveTarget = Request.GetActingSlotId();
				}
				else
				{
					Out = Fight(TEXT("Move.SwordsDance"), EBattleSide::Opponent);
				}
				return true;
			};
			if (!LockTurn(Engine, SwitchProvider, Evidence, DriveError)
				|| !ExecuteLockedQueue(Engine, Evidence, DriveError))
			{
				return false;
			}
			const FBattleObservedBattler* Reserve = Engine.GetSnapshot().FindObservedBattler(
				MakeNumericId<FBattlerId>(22));
			return Reserve != nullptr
				&& Reserve->StatStages.TryGetStage(
					EBattleStat::Speed,
					OutSpeedStage);
		};
		FRunEvidence Evidence;
		const FString Label = bMistActive
			? TEXT("Mist blocks opposing Sticky Web")
			: TEXT("Sticky Web without Mist control");
		return RunDeterministicTwins(
				Test,
				Label,
				Fixture.Catalog,
				Setup,
				Drive,
				&Evidence)
			&& ValidateGlobalInvariants(
				Test,
				Fixture.Catalog,
				Evidence,
				Label);
	}

	bool RunTailwindOrderCase(
		FAutomationTestBase& Test,
		const FCatalogFixture& Fixture,
		const bool bTailwindActive,
		uint64& OutFirstActor)
	{
		const TArray<FName> OpponentMoves = bTailwindActive
			? TArray<FName>{TEXT("Move.Tailwind"), TEXT("Move.SwordsDance")}
			: TArray<FName>{TEXT("Move.SwordsDance")};
		FSetupSpec Spec = ConditionSpec(
			bTailwindActive ? 11460 : 11461,
			{TEXT("Move.SwordsDance")},
			OpponentMoves);
		FBattleSetup Setup;
		FString Error;
		if (!TryBuildSetup(Fixture.Catalog, Spec, Setup, Error))
		{
			Test.AddError(Error);
			return false;
		}
		const FName OpponentSetupMove = bTailwindActive
			? FName(TEXT("Move.Tailwind"))
			: FName(TEXT("Move.SwordsDance"));
		const FDriveFunction Drive = [OpponentSetupMove, &OutFirstActor](
			FBattleEngine& Engine,
			FRunEvidence& Evidence,
			FString& DriveError)
		{
			if (!RunTurns(
					Engine,
					Evidence,
					DriveError,
					{{TEXT("Move.SwordsDance"), OpponentSetupMove}}))
			{
				return false;
			}
			const FChoiceProvider Provider = [](
				const FBattleDecisionRequest& Request,
				FChoice& Out,
				FString&)
			{
				Out = Fight(
					TEXT("Move.SwordsDance"),
					Request.GetDecisionOwnerTrainerId()
						== MakeNumericId<FTrainerId>(1)
						? EBattleSide::Opponent
						: EBattleSide::Player);
				return true;
			};
			if (!LockTurn(Engine, Provider, Evidence, DriveError)
				|| Engine.GetLockedActions().IsEmpty())
			{
				return false;
			}
			OutFirstActor = Engine.GetLockedActions()[0].Decision
				.GetActingBattlerId().GetValue();
			return ExecuteLockedQueue(Engine, Evidence, DriveError)
				&& ResolveEndTurn(Engine, Evidence, DriveError);
		};
		FRunEvidence Evidence;
		const FString Label = bTailwindActive
			? TEXT("Tailwind doubled-Speed order")
			: TEXT("Tailwind order control");
		return RunDeterministicTwins(
				Test,
				Label,
				Fixture.Catalog,
				Setup,
				Drive,
				&Evidence)
			&& ValidateGlobalInvariants(
				Test,
				Fixture.Catalog,
				Evidence,
				Label);
	}

	bool RunActiveEffectSubcases(
		FAutomationTestBase& Test,
		const FCatalogFixture& Fixture)
	{
		bool bValid = true;
		const auto GroundPlayer = [](FSetupSpec& Spec)
		{
			SetPlayerSpecies(
				Spec,
				TEXT("Species.Clefable"),
				TEXT("Ability.MagicGuard"));
		};

		FSetupSpec Sun = ConditionSpec(11430, {TEXT("Move.SunnyDay")}, {TEXT("Move.Flamethrower")});
		FSetupSpec SunControl = ConditionSpec(11431, {TEXT("Move.SwordsDance")}, {TEXT("Move.Flamethrower")});
		GroundPlayer(Sun); GroundPlayer(SunControl);
		bValid &= ComparePlayerDamage(Test, Fixture, TEXT("Sun Fire boost"), Sun,
			{{TEXT("Move.SunnyDay"), TEXT("Move.Flamethrower")}}, SunControl,
			{{TEXT("Move.SwordsDance"), TEXT("Move.Flamethrower")}}, false);

		FSetupSpec Rain = ConditionSpec(11432, {TEXT("Move.RainDance")}, {TEXT("Move.Flamethrower")});
		FSetupSpec RainControl = ConditionSpec(11433, {TEXT("Move.SwordsDance")}, {TEXT("Move.Flamethrower")});
		GroundPlayer(Rain); GroundPlayer(RainControl);
		bValid &= ComparePlayerDamage(Test, Fixture, TEXT("Rain Fire reduction"), Rain,
			{{TEXT("Move.RainDance"), TEXT("Move.Flamethrower")}}, RainControl,
			{{TEXT("Move.SwordsDance"), TEXT("Move.Flamethrower")}}, true);

		FSetupSpec Sand = ConditionSpec(11434, {TEXT("Move.Sandstorm")}, {TEXT("Move.SwordsDance")});
		FSetupSpec SandImmune = ConditionSpec(11435, {TEXT("Move.Sandstorm")}, {TEXT("Move.SwordsDance")});
		SetPlayerSpecies(Sand, TEXT("Species.Espathra"), TEXT("Ability.SpeedBoost"));
		SetPlayerSpecies(SandImmune, TEXT("Species.Excadrill"), TEXT("Ability.MoldBreaker"));
		FBattleSetup SandSetup; FBattleSetup SandImmuneSetup;
		FRunEvidence SandEvidence; FRunEvidence SandImmuneEvidence;
		if (RunScenario(Test, Fixture, TEXT("Sandstorm residual"), Sand,
				{{TEXT("Move.Sandstorm"), TEXT("Move.SwordsDance")}}, SandSetup, SandEvidence)
			&& RunScenario(Test, Fixture, TEXT("Sandstorm immune control"), SandImmune,
				{{TEXT("Move.Sandstorm"), TEXT("Move.SwordsDance")}}, SandImmuneSetup, SandImmuneEvidence))
		{
			bValid &= Test.TestTrue(TEXT("Sandstorm damages a non-immune battler and preserves Ground/Steel immunity"),
				DamageTaken(SandSetup, SandEvidence, 11) > 0
				&& DamageTaken(SandImmuneSetup, SandImmuneEvidence, 11) == 0);
		}
		else bValid = false;

		FSetupSpec Snow = ConditionSpec(11436,
			{TEXT("Move.Snowscape"), TEXT("Move.SwordsDance")}, {TEXT("Move.SolarBeam")});
		FSetupSpec SnowControl = ConditionSpec(11437,
			{TEXT("Move.SwordsDance")}, {TEXT("Move.SolarBeam")});
		GroundPlayer(Snow); GroundPlayer(SnowControl);
		bValid &= ComparePlayerDamage(Test, Fixture, TEXT("Snow Solar Beam half power"), Snow,
			{{TEXT("Move.Snowscape"), TEXT("Move.SolarBeam")}, {TEXT("Move.SwordsDance"), TEXT("Move.SolarBeam")}},
			SnowControl,
			{{TEXT("Move.SwordsDance"), TEXT("Move.SolarBeam")}, {TEXT("Move.SwordsDance"), TEXT("Move.SolarBeam")}}, true);

		FSetupSpec Electric = ConditionSpec(11438, {TEXT("Move.ElectricTerrain")}, {TEXT("Move.SleepPowder")});
		FSetupSpec ElectricControl = ConditionSpec(11439, {TEXT("Move.SwordsDance")}, {TEXT("Move.SleepPowder")});
		GroundPlayer(Electric); GroundPlayer(ElectricControl);
		FBattleSetup ElectricSetup; FBattleSetup ElectricControlSetup;
		FRunEvidence ElectricEvidence; FRunEvidence ElectricControlEvidence;
		if (RunScenario(Test, Fixture, TEXT("Electric Terrain Sleep prevention"), Electric,
				{{TEXT("Move.ElectricTerrain"), TEXT("Move.SleepPowder")}}, ElectricSetup, ElectricEvidence)
			&& RunScenario(Test, Fixture, TEXT("Electric Terrain Sleep control"), ElectricControl,
				{{TEXT("Move.SwordsDance"), TEXT("Move.SleepPowder")}}, ElectricControlSetup, ElectricControlEvidence))
		{
			const FBattleObservedBattler* Protected = ElectricEvidence.Replay.GetFinalSnapshot().FindObservedBattler(MakeNumericId<FBattlerId>(11));
			const FBattleObservedBattler* Control = ElectricControlEvidence.Replay.GetFinalSnapshot().FindObservedBattler(MakeNumericId<FBattlerId>(11));
			bValid &= Test.TestTrue(TEXT("Electric Terrain prevents grounded Sleep while the no-terrain control sleeps"),
				Protected != nullptr && !Protected->MajorStatusId.IsValid()
				&& Control != nullptr && Control->MajorStatusId == MakeDefinitionId<FConditionId>(TEXT("Condition.Sleep")));
		}
		else bValid = false;

		FSetupSpec Grassy = ConditionSpec(11440, {TEXT("Move.GrassyTerrain")}, {TEXT("Move.SwordsDance")});
		FSetupSpec GrassyControl = ConditionSpec(11441, {TEXT("Move.SwordsDance")}, {TEXT("Move.SwordsDance")});
		GroundPlayer(Grassy); GroundPlayer(GrassyControl);
		Grassy.Battlers[0].CurrentHP = 1; GrassyControl.Battlers[0].CurrentHP = 1;
		FBattleSetup GrassySetup; FBattleSetup GrassyControlSetup;
		FRunEvidence GrassyEvidence; FRunEvidence GrassyControlEvidence;
		if (RunScenario(Test, Fixture, TEXT("Grassy Terrain healing"), Grassy,
				{{TEXT("Move.GrassyTerrain"), TEXT("Move.SwordsDance")}}, GrassySetup, GrassyEvidence)
			&& RunScenario(Test, Fixture, TEXT("Grassy Terrain healing control"), GrassyControl,
				{{TEXT("Move.SwordsDance"), TEXT("Move.SwordsDance")}}, GrassyControlSetup, GrassyControlEvidence))
		{
			bValid &= Test.TestTrue(TEXT("Grassy Terrain heals a damaged grounded battler while the control remains unchanged"),
				CurrentHP(GrassyEvidence.Replay.GetFinalSnapshot(), 11) > StartingHP(GrassySetup, 11)
				&& CurrentHP(GrassyControlEvidence.Replay.GetFinalSnapshot(), 11) == StartingHP(GrassyControlSetup, 11));
		}
		else bValid = false;

		FSetupSpec Misty = ConditionSpec(11442, {TEXT("Move.MistyTerrain")}, {TEXT("Move.Toxic")});
		FSetupSpec MistyControl = ConditionSpec(11443, {TEXT("Move.SwordsDance")}, {TEXT("Move.Toxic")});
		GroundPlayer(Misty); GroundPlayer(MistyControl);
		FBattleSetup MistySetup; FBattleSetup MistyControlSetup;
		FRunEvidence MistyEvidence; FRunEvidence MistyControlEvidence;
		if (RunScenario(Test, Fixture, TEXT("Misty Terrain status prevention"), Misty,
				{{TEXT("Move.MistyTerrain"), TEXT("Move.Toxic")}}, MistySetup, MistyEvidence)
			&& RunScenario(Test, Fixture, TEXT("Misty Terrain status control"), MistyControl,
				{{TEXT("Move.SwordsDance"), TEXT("Move.Toxic")}}, MistyControlSetup, MistyControlEvidence))
		{
			const FBattleObservedBattler* Protected = MistyEvidence.Replay.GetFinalSnapshot().FindObservedBattler(MakeNumericId<FBattlerId>(11));
			const FBattleObservedBattler* Control = MistyControlEvidence.Replay.GetFinalSnapshot().FindObservedBattler(MakeNumericId<FBattlerId>(11));
			bValid &= Test.TestTrue(TEXT("Misty Terrain prevents a grounded major status while the control receives Toxic"),
				Protected != nullptr && !Protected->MajorStatusId.IsValid()
				&& Control != nullptr && Control->MajorStatusId == MakeDefinitionId<FConditionId>(TEXT("Condition.Toxic")));
		}
		else bValid = false;

		FSetupSpec Psychic = ConditionSpec(11444,
			{TEXT("Move.PsychicTerrain"), TEXT("Move.SwordsDance")},
			{TEXT("Move.SwordsDance"), TEXT("Move.QuickAttack")});
		FSetupSpec PsychicControl = ConditionSpec(11445,
			{TEXT("Move.SwordsDance")},
			{TEXT("Move.SwordsDance"), TEXT("Move.QuickAttack")});
		GroundPlayer(Psychic); GroundPlayer(PsychicControl);
		FBattleSetup PsychicSetup; FBattleSetup PsychicControlSetup;
		FRunEvidence PsychicEvidence; FRunEvidence PsychicControlEvidence;
		if (RunScenario(Test, Fixture, TEXT("Psychic Terrain priority block"), Psychic,
				{{TEXT("Move.PsychicTerrain"), TEXT("Move.SwordsDance")}, {TEXT("Move.SwordsDance"), TEXT("Move.QuickAttack")}}, PsychicSetup, PsychicEvidence)
			&& RunScenario(Test, Fixture, TEXT("Psychic Terrain priority control"), PsychicControl,
				{{TEXT("Move.SwordsDance"), TEXT("Move.SwordsDance")}, {TEXT("Move.SwordsDance"), TEXT("Move.QuickAttack")}}, PsychicControlSetup, PsychicControlEvidence))
		{
			bValid &= Test.TestTrue(TEXT("Psychic Terrain blocks opposing Quick Attack against a grounded target while the control takes damage"),
				DamageTaken(PsychicSetup, PsychicEvidence, 11) == 0
				&& DamageTaken(PsychicControlSetup, PsychicControlEvidence, 11) > 0);
		}
		else bValid = false;

		FSetupSpec Reflect = ConditionSpec(11446, {TEXT("Move.Reflect")}, {TEXT("Move.Bite")});
		FSetupSpec ReflectControl = ConditionSpec(11447, {TEXT("Move.Safeguard")}, {TEXT("Move.Bite")});
		GroundPlayer(Reflect); GroundPlayer(ReflectControl);
		bValid &= ComparePlayerDamage(Test, Fixture, TEXT("Reflect physical reduction"), Reflect,
			{{TEXT("Move.Reflect"), TEXT("Move.Bite")}}, ReflectControl,
			{{TEXT("Move.Safeguard"), TEXT("Move.Bite")}}, true);

		FSetupSpec Light = ConditionSpec(11448, {TEXT("Move.LightScreen")}, {TEXT("Move.Flamethrower")});
		FSetupSpec LightControl = ConditionSpec(11449, {TEXT("Move.Safeguard")}, {TEXT("Move.Flamethrower")});
		GroundPlayer(Light); GroundPlayer(LightControl);
		bValid &= ComparePlayerDamage(Test, Fixture, TEXT("Light Screen special reduction"), Light,
			{{TEXT("Move.LightScreen"), TEXT("Move.Flamethrower")}}, LightControl,
			{{TEXT("Move.Safeguard"), TEXT("Move.Flamethrower")}}, true);

		FSetupSpec Veil = ConditionSpec(11450,
			{TEXT("Move.Snowscape"), TEXT("Move.AuroraVeil")},
			{TEXT("Move.SwordsDance"), TEXT("Move.Flamethrower")});
		FSetupSpec VeilControl = ConditionSpec(11451,
			{TEXT("Move.Snowscape"), TEXT("Move.Safeguard")},
			{TEXT("Move.SwordsDance"), TEXT("Move.Flamethrower")});
		GroundPlayer(Veil); GroundPlayer(VeilControl);
		bValid &= ComparePlayerDamage(Test, Fixture, TEXT("Aurora Veil special reduction under Snow"), Veil,
			{{TEXT("Move.Snowscape"), TEXT("Move.SwordsDance")}, {TEXT("Move.AuroraVeil"), TEXT("Move.Flamethrower")}},
			VeilControl,
			{{TEXT("Move.Snowscape"), TEXT("Move.SwordsDance")}, {TEXT("Move.Safeguard"), TEXT("Move.Flamethrower")}}, true);

		FSetupSpec Magic = ConditionSpec(11452, {TEXT("Move.MagicRoom")}, {TEXT("Move.Bite")});
		GroundPlayer(Magic);
		Magic.Battlers[1].OriginalHeldItemId = TEXT("Item.ChoiceBand");
		Magic.Battlers[1].CurrentHeldItemId = TEXT("Item.ChoiceBand");
		FBattleSetup MagicSetup;
		FString MagicError;
		if (!TryBuildSetup(Fixture.Catalog, Magic, MagicSetup, MagicError))
		{
			Test.AddError(MagicError);
			bValid = false;
		}
		else
		{
			int32 SuppressedDamage = INDEX_NONE;
			int32 RestoredDamage = INDEX_NONE;
			const FDriveFunction MagicDrive = [&SuppressedDamage, &RestoredDamage](
				FBattleEngine& Engine,
				FRunEvidence& Evidence,
				FString& DriveError)
			{
				const int32 Before = CurrentHP(Engine.GetSnapshot(), 11);
				if (!RunTurns(Engine, Evidence, DriveError,
						{{TEXT("Move.MagicRoom"), TEXT("Move.Bite")}})) return false;
				const int32 During = CurrentHP(Engine.GetSnapshot(), 11);
				if (!RunTurns(Engine, Evidence, DriveError,
						{{TEXT("Move.MagicRoom"), TEXT("Move.Bite")}})) return false;
				const int32 After = CurrentHP(Engine.GetSnapshot(), 11);
				SuppressedDamage = Before - During;
				RestoredDamage = During - After;
				return true;
			};
			FRunEvidence MagicEvidence;
			if (RunDeterministicTwins(Test, TEXT("Magic Room Choice Band suppression and restoration"),
					Fixture.Catalog, MagicSetup, MagicDrive, &MagicEvidence)
				&& ValidateGlobalInvariants(Test, Fixture.Catalog, MagicEvidence, TEXT("Magic Room")))
			{
				bValid &= Test.TestTrue(TEXT("Magic Room suppresses Choice Band and toggling it off restores the larger damage"),
					SuppressedDamage > 0 && RestoredDamage > SuppressedDamage);
			}
			else bValid = false;
		}

		FSetupSpec Wonder = ConditionSpec(11453, {TEXT("Move.WonderRoom")}, {TEXT("Move.Bite")});
		FSetupSpec WonderControl = ConditionSpec(11454, {TEXT("Move.Safeguard")}, {TEXT("Move.Bite")});
		SetPlayerSpecies(Wonder, TEXT("Species.Pelipper"), TEXT("Ability.Drizzle"));
		SetPlayerSpecies(WonderControl, TEXT("Species.Pelipper"), TEXT("Ability.Drizzle"));
		bValid &= ComparePlayerDamage(Test, Fixture, TEXT("Wonder Room Defense swap"), Wonder,
			{{TEXT("Move.WonderRoom"), TEXT("Move.Bite")}}, WonderControl,
			{{TEXT("Move.Safeguard"), TEXT("Move.Bite")}}, false);

		FSetupSpec Safeguard = ConditionSpec(11455, {TEXT("Move.Safeguard")}, {TEXT("Move.Toxic")});
		FSetupSpec SafeguardControl = ConditionSpec(11456, {TEXT("Move.SwordsDance")}, {TEXT("Move.Toxic")});
		GroundPlayer(Safeguard); GroundPlayer(SafeguardControl);
		FBattleSetup SafeguardSetup; FBattleSetup SafeguardControlSetup;
		FRunEvidence SafeguardEvidence; FRunEvidence SafeguardControlEvidence;
		if (RunScenario(Test, Fixture, TEXT("Safeguard status prevention"), Safeguard,
				{{TEXT("Move.Safeguard"), TEXT("Move.Toxic")}}, SafeguardSetup, SafeguardEvidence)
			&& RunScenario(Test, Fixture, TEXT("Safeguard status control"), SafeguardControl,
				{{TEXT("Move.SwordsDance"), TEXT("Move.Toxic")}}, SafeguardControlSetup, SafeguardControlEvidence))
		{
			const FBattleObservedBattler* Protected = SafeguardEvidence.Replay.GetFinalSnapshot().FindObservedBattler(MakeNumericId<FBattlerId>(11));
			const FBattleObservedBattler* Control = SafeguardControlEvidence.Replay.GetFinalSnapshot().FindObservedBattler(MakeNumericId<FBattlerId>(11));
			bValid &= Test.TestTrue(TEXT("Safeguard prevents opponent-applied Toxic while the no-Safeguard control receives it"),
				Protected != nullptr && !Protected->MajorStatusId.IsValid()
				&& Control != nullptr && Control->MajorStatusId == MakeDefinitionId<FConditionId>(TEXT("Condition.Toxic")));
		}
		else bValid = false;

		int32 MistStage = INDEX_NONE;
		int32 MistControlStage = INDEX_NONE;
		bValid &= RunStickyWebMistCase(Test, Fixture, true, MistStage);
		bValid &= RunStickyWebMistCase(Test, Fixture, false, MistControlStage);
		bValid &= Test.TestTrue(TEXT("Mist prevents the opposing Sticky Web Speed drop while the control reaches minus one"),
			MistStage == 0 && MistControlStage == -1);

		uint64 TailwindFirst = 0;
		uint64 TailwindControlFirst = 0;
		bValid &= RunTailwindOrderCase(Test, Fixture, true, TailwindFirst);
		bValid &= RunTailwindOrderCase(Test, Fixture, false, TailwindControlFirst);
		bValid &= Test.TestTrue(TEXT("Tailwind changes the next locked-action order through doubled Speed"),
			TailwindFirst == 21 && TailwindControlFirst == 11);
		return bValid;
	}

	bool RunFieldInventorySubcases(FAutomationTestBase& Test, const FCatalogFixture& Fixture)
	{
		bool bValid = true; FString Error;
		const FSetupSpec ScreenSpec = ConditionSpec(11402,
			{TEXT("Move.Snowscape"), TEXT("Move.AuroraVeil"), TEXT("Move.Safeguard"), TEXT("Move.Mist")},
			{TEXT("Move.Reflect"), TEXT("Move.LightScreen"), TEXT("Move.Tailwind"), TEXT("Move.SwordsDance")});
		FBattleSetup ScreenSetup;
		if (!TryBuildSetup(Fixture.Catalog, ScreenSpec, ScreenSetup, Error)) { Test.AddError(Error); return false; }
		const TArray<TPair<FName, FName>> ScreenTurns = {{TEXT("Move.Snowscape"), TEXT("Move.Reflect")}, {TEXT("Move.AuroraVeil"), TEXT("Move.LightScreen")},
			{TEXT("Move.Safeguard"), TEXT("Move.Tailwind")}, {TEXT("Move.Mist"), TEXT("Move.SwordsDance")}};
		const FDriveFunction ScreenDrive = [ScreenTurns](FBattleEngine& Engine, FRunEvidence& Evidence, FString& DriveError)
		{
			if (!RunTurns(Engine, Evidence, DriveError, ScreenTurns)) return false;
			for (const TCHAR* Id : {TEXT("Condition.Snow"), TEXT("Condition.AuroraVeil"), TEXT("Condition.Reflect"), TEXT("Condition.LightScreen"), TEXT("Condition.Safeguard"), TEXT("Condition.Mist"), TEXT("Condition.Tailwind")})
			{
				if (!SnapshotContainsCondition(Engine.GetSnapshot(), Id)) { DriveError = FString(TEXT("Missing public condition: ")) + Id; return false; }
			}
			return true;
		};
		FRunEvidence ScreenEvidence;
		bValid &= RunDeterministicTwins(Test, TEXT("screens and side-condition coexistence"), Fixture.Catalog, ScreenSetup, ScreenDrive, &ScreenEvidence);

		const FSetupSpec RoomSpec = ConditionSpec(11403, {TEXT("Move.TrickRoom"), TEXT("Move.MagicRoom"), TEXT("Move.WonderRoom")}, {TEXT("Move.SwordsDance")});
		FBattleSetup RoomSetup;
		if (!TryBuildSetup(Fixture.Catalog, RoomSpec, RoomSetup, Error)) { Test.AddError(Error); return false; }
		const TArray<TPair<FName, FName>> RoomTurns = {{TEXT("Move.TrickRoom"), TEXT("Move.SwordsDance")}, {TEXT("Move.MagicRoom"), TEXT("Move.SwordsDance")}, {TEXT("Move.WonderRoom"), TEXT("Move.SwordsDance")}};
		const FDriveFunction RoomDrive = [RoomTurns](FBattleEngine& Engine, FRunEvidence& Evidence, FString& DriveError)
		{
			if (!RunTurns(Engine, Evidence, DriveError, RoomTurns)) return false;
			return SnapshotContainsCondition(Engine.GetSnapshot(), TEXT("Condition.TrickRoom"))
				&& SnapshotContainsCondition(Engine.GetSnapshot(), TEXT("Condition.MagicRoom"))
				&& SnapshotContainsCondition(Engine.GetSnapshot(), TEXT("Condition.WonderRoom"));
		};
		FRunEvidence RoomEvidence;
		bValid &= RunDeterministicTwins(Test, TEXT("three rooms coexist"), Fixture.Catalog, RoomSetup, RoomDrive, &RoomEvidence);

		const FSetupSpec MissingSpec = ConditionSpec(11404, {TEXT("Move.Sandstorm"), TEXT("Move.MistyTerrain")}, {TEXT("Move.Snowscape"), TEXT("Move.PsychicTerrain")});
		FBattleSetup MissingSetup;
		if (!TryBuildSetup(Fixture.Catalog, MissingSpec, MissingSetup, Error)) { Test.AddError(Error); return false; }
		const TArray<TPair<FName, FName>> MissingTurns = {{TEXT("Move.Sandstorm"), TEXT("Move.Snowscape")}, {TEXT("Move.MistyTerrain"), TEXT("Move.PsychicTerrain")}};
		const FDriveFunction MissingDrive = [MissingTurns](FBattleEngine& Engine, FRunEvidence& Evidence, FString& DriveError)
		{
			return RunTurns(Engine, Evidence, DriveError, MissingTurns, false)
				&& SnapshotContainsCondition(Engine.GetSnapshot(), TEXT("Condition.Snow"))
				&& SnapshotContainsCondition(Engine.GetSnapshot(), TEXT("Condition.PsychicTerrain"));
		};
		FRunEvidence MissingEvidence;
		bValid &= RunDeterministicTwins(Test, TEXT("remaining weather and terrains"), Fixture.Catalog, MissingSetup, MissingDrive, &MissingEvidence);
		bValid &= Test.TestTrue(TEXT("Sandstorm, Snow, Misty Terrain, and Psychic Terrain all publish lifecycle facts"),
			CountEvents(MissingEvidence.Replay, EBattleEventType::FieldEffectChanged) >= 4);

		const FSetupSpec RemovalSpec = ConditionSpec(11405, {TEXT("Move.Defog"), TEXT("Move.SwordsDance")},
			{TEXT("Move.Reflect"), TEXT("Move.GrassyTerrain"), TEXT("Move.Spikes"), TEXT("Move.SwordsDance")});
		FBattleSetup RemovalSetup;
		if (!TryBuildSetup(Fixture.Catalog, RemovalSpec, RemovalSetup, Error)) { Test.AddError(Error); return false; }
		const TArray<TPair<FName, FName>> RemovalTurns = {{TEXT("Move.SwordsDance"), TEXT("Move.Reflect")}, {TEXT("Move.SwordsDance"), TEXT("Move.GrassyTerrain")},
			{TEXT("Move.SwordsDance"), TEXT("Move.Spikes")}, {TEXT("Move.Defog"), TEXT("Move.SwordsDance")}};
		const FDriveFunction RemovalDrive = [RemovalTurns](FBattleEngine& Engine, FRunEvidence& Evidence, FString& DriveError)
		{
			return RunTurns(Engine, Evidence, DriveError, RemovalTurns, false)
				&& !SnapshotContainsCondition(Engine.GetSnapshot(), TEXT("Condition.Reflect"))
				&& !SnapshotContainsCondition(Engine.GetSnapshot(), TEXT("Condition.GrassyTerrain"))
				&& !SnapshotContainsCondition(Engine.GetSnapshot(), TEXT("Condition.Spikes"));
		};
		FRunEvidence RemovalEvidence;
		bValid &= RunDeterministicTwins(Test, TEXT("Defog public removal"), Fixture.Catalog, RemovalSetup, RemovalDrive, &RemovalEvidence);
		return bValid;
	}
}

using namespace BattleCanonicalConditionPrivate;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FC11AFieldSideLifecycle,
	"PokemonSolarus.Battle.C11A.Single.Conditions.FieldSideLifecycleReplacementRemovalExpiryVisibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FC11AFieldSideLifecycle::RunTest(const FString& Parameters)
{
	FCatalogFixture Fixture;
	FBattleSetup Setup;
	const FSetupSpec Spec = ConditionSpec(11401,
		{FName(TEXT("Move.SunnyDay")), FName(TEXT("Move.ElectricTerrain")),
			FName(TEXT("Move.Reflect")), FName(TEXT("Move.SwordsDance"))},
		{FName(TEXT("Move.RainDance")), FName(TEXT("Move.GrassyTerrain")),
			FName(TEXT("Move.LightScreen")), FName(TEXT("Move.SwordsDance"))});
	if (!Build(*this, Spec, Fixture, Setup)) return false;
	TArray<TPair<FName, FName>> Turns = {
		{FName(TEXT("Move.SunnyDay")), FName(TEXT("Move.RainDance"))},
		{FName(TEXT("Move.ElectricTerrain")), FName(TEXT("Move.GrassyTerrain"))},
		{FName(TEXT("Move.Reflect")), FName(TEXT("Move.LightScreen"))}};
	for (int32 Index = 0; Index < 6; ++Index)
	{
		Turns.Add({FName(TEXT("Move.SwordsDance")), FName(TEXT("Move.SwordsDance"))});
	}
	const FDriveFunction Drive = [Turns](FBattleEngine& Engine, FRunEvidence& Evidence, FString& Error)
	{
		return RunTurns(Engine, Evidence, Error, Turns);
	};
	FRunEvidence Evidence;
	if (!RunDeterministicTwins(*this, TEXT("field and side lifecycle"),
		Fixture.Catalog, Setup, Drive, &Evidence)) return false;
	TestTrue(TEXT("Weather replacement publishes both starts/removals"),
		CountEvents(Evidence.Replay, EBattleEventType::FieldEffectChanged) >= 6);
	TestFalse(TEXT("Sun expires from the public snapshot"),
		SnapshotContainsCondition(Evidence.Replay.GetFinalSnapshot(), TEXT("Condition.Sun")));
	TestFalse(TEXT("Rain expires from the public snapshot"),
		SnapshotContainsCondition(Evidence.Replay.GetFinalSnapshot(), TEXT("Condition.Rain")));
	TestFalse(TEXT("Electric Terrain expires from the public snapshot"),
		SnapshotContainsCondition(Evidence.Replay.GetFinalSnapshot(), TEXT("Condition.ElectricTerrain")));
	TestFalse(TEXT("Grassy Terrain expires from the public snapshot"),
		SnapshotContainsCondition(Evidence.Replay.GetFinalSnapshot(), TEXT("Condition.GrassyTerrain")));
	TestFalse(TEXT("Reflect expires from the public snapshot"),
		SnapshotContainsCondition(Evidence.Replay.GetFinalSnapshot(), TEXT("Condition.Reflect")));
	TestFalse(TEXT("Light Screen expires from the public snapshot"),
		SnapshotContainsCondition(Evidence.Replay.GetFinalSnapshot(), TEXT("Condition.LightScreen")));
	return ValidateGlobalInvariants(*this, Fixture.Catalog, Evidence, TEXT("field lifecycle"))
		&& RunFieldInventorySubcases(*this, Fixture)
		&& RunActiveEffectSubcases(*this, Fixture);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FC11AHazardGroundingSwitch,
	"PokemonSolarus.Battle.C11A.Single.Conditions.HazardsGroundingItemsSwitchAndFaint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FC11AHazardGroundingSwitch::RunTest(const FString& Parameters)
{
	struct FCase { const TCHAR* Label; FName Item; FName Species; FName Ability; int32 HP; };
	const FCase Cases[] = {
		{TEXT("Heavy-Duty Boots"), TEXT("Item.HeavyDutyBoots"), TEXT("Species.Venusaur"), TEXT("Ability.Overgrow"), INDEX_NONE},
		{TEXT("Air Balloon"), TEXT("Item.AirBalloon"), TEXT("Species.Venusaur"), TEXT("Ability.Overgrow"), INDEX_NONE},
		{TEXT("Levitate"), NAME_None, TEXT("Species.Rotom"), TEXT("Ability.Levitate"), INDEX_NONE},
		{TEXT("ordinary grounded"), NAME_None, TEXT("Species.Espathra"), TEXT("Ability.SpeedBoost"), INDEX_NONE},
		{TEXT("switch-in faint"), NAME_None, TEXT("Species.Espathra"), TEXT("Ability.SpeedBoost"), 1}};
	bool bValid = true;
	for (int32 CaseIndex = 0; CaseIndex < UE_ARRAY_COUNT(Cases); ++CaseIndex)
	{
		FCatalogFixture Fixture;
		FBattleSetup Setup;
		FSetupSpec Spec = ConditionSpec(11410 + CaseIndex,
			{FName(TEXT("Move.Spikes")), FName(TEXT("Move.ToxicSpikes")),
				FName(TEXT("Move.StealthRock")), FName(TEXT("Move.StickyWeb"))},
			{FName(TEXT("Move.SwordsDance"))}, true, Cases[CaseIndex].Item);
		Spec.Battlers[2].SpeciesId = Cases[CaseIndex].Species;
		Spec.Battlers[2].AbilityId = Cases[CaseIndex].Ability;
		Spec.Battlers[2].CurrentHP = Cases[CaseIndex].HP;
		if (!Build(*this, Spec, Fixture, Setup)) return false;
		const FDriveFunction Drive = [](
			FBattleEngine& Engine, FRunEvidence& Evidence, FString& Error)
		{
			const FName Hazards[] = {TEXT("Move.Spikes"), TEXT("Move.Spikes"), TEXT("Move.Spikes"), TEXT("Move.ToxicSpikes"),
				TEXT("Move.ToxicSpikes"), TEXT("Move.StealthRock"), TEXT("Move.StickyWeb")};
			for (const FName Hazard : Hazards)
			{
				const FChoiceProvider Provider = [Hazard](const FBattleDecisionRequest& Request, FChoice& Out, FString&)
				{
					Out = Fight(Request.GetDecisionOwnerTrainerId() == MakeNumericId<FTrainerId>(1)
						? Hazard : FName(TEXT("Move.SwordsDance")),
						Request.GetDecisionOwnerTrainerId() == MakeNumericId<FTrainerId>(1)
							? EBattleSide::Opponent : EBattleSide::Player);
					return true;
				};
				if (!LockTurn(Engine, Provider, Evidence, Error)
					|| !ExecuteLockedQueue(Engine, Evidence, Error)
					|| !ResolveEndTurn(Engine, Evidence, Error)) return false;
			}
			const FChoiceProvider SwitchProvider = [](const FBattleDecisionRequest& Request, FChoice& Out, FString&)
			{
				if (Request.GetDecisionOwnerTrainerId() == MakeNumericId<FTrainerId>(2))
				{
					Out.Kind = EChoiceKind::Switch;
					Out.PartyTarget = MakePartySlotId(1);
					Out.ActiveTarget = Request.GetActingSlotId();
				}
				else Out = Fight(FName(TEXT("Move.Spikes")), EBattleSide::Opponent);
				return true;
			};
			return LockTurn(Engine, SwitchProvider, Evidence, Error)
				&& ExecuteLockedQueue(Engine, Evidence, Error);
		};
		FRunEvidence Evidence;
		const FString Label(Cases[CaseIndex].Label);
		bValid &= RunDeterministicTwins(*this, Label, Fixture.Catalog, Setup, Drive, &Evidence);
		bValid &= TestTrue(Label + TEXT(" switch emits Switched"),
			HasEvent(Evidence.Replay, EBattleEventType::Switched));
		bValid &= TestTrue(Label + TEXT(" hazards remain publicly visible"),
			SnapshotContainsCondition(Evidence.Replay.GetFinalSnapshot(), TEXT("Condition.Spikes"))
			&& SnapshotContainsCondition(Evidence.Replay.GetFinalSnapshot(), TEXT("Condition.StealthRock")));
		const FBattleObservedCondition* Spikes = FindSnapshotCondition(Evidence.Replay.GetFinalSnapshot(), TEXT("Condition.Spikes"));
		const FBattleObservedCondition* ToxicSpikes = FindSnapshotCondition(Evidence.Replay.GetFinalSnapshot(), TEXT("Condition.ToxicSpikes"));
		bValid &= TestTrue(Label + TEXT(" preserves exact hazard layers"), Spikes != nullptr && Spikes->LayerCount == 3
			&& ToxicSpikes != nullptr && ToxicSpikes->LayerCount == 2);
		const FBattlePartyEntrySetup* BeforeReserve = Setup.FindBattler(MakeNumericId<FBattlerId>(22));
		const FBattlePartyEntrySetup* AfterReserve = Evidence.Replay.GetFinalSnapshot().FindBattler(MakeNumericId<FBattlerId>(22));
		const FBattleObservedBattler* ObservedReserve = Evidence.Replay.GetFinalSnapshot().FindObservedBattler(MakeNumericId<FBattlerId>(22));
		int32 SpeedStage = 0;
		if (Label == TEXT("Heavy-Duty Boots")) bValid &= TestTrue(TEXT("Boots prevent every switch-in hazard effect"), BeforeReserve != nullptr && AfterReserve != nullptr
			&& BeforeReserve->CurrentHP == AfterReserve->CurrentHP && ObservedReserve != nullptr && !ObservedReserve->MajorStatusId.IsValid()
			&& ObservedReserve->StatStages.TryGetStage(EBattleStat::Speed, SpeedStage) && SpeedStage == 0);
		if (Label == TEXT("Air Balloon") || Label == TEXT("Levitate")) bValid &= TestTrue(Label + TEXT(" remains airborne for grounded hazards"), ObservedReserve != nullptr
			&& !ObservedReserve->MajorStatusId.IsValid() && ObservedReserve->StatStages.TryGetStage(EBattleStat::Speed, SpeedStage) && SpeedStage == 0);
		if (Label == TEXT("ordinary grounded")) bValid &= TestTrue(TEXT("Grounded reserve receives damage, Toxic Spikes, and Sticky Web"), BeforeReserve != nullptr && AfterReserve != nullptr
			&& AfterReserve->CurrentHP < BeforeReserve->CurrentHP && ObservedReserve != nullptr && ObservedReserve->MajorStatusId.IsValid()
			&& ObservedReserve->StatStages.TryGetStage(EBattleStat::Speed, SpeedStage) && SpeedStage == -1);
		if (Label == TEXT("switch-in faint")) bValid &= TestTrue(TEXT("Hazards can faint on switch-in and request replacement"), HasEvent(Evidence.Replay, EBattleEventType::Fainted)
			&& HasEvent(Evidence.Replay, EBattleEventType::ReplacementRequired));
		bValid &= ValidateGlobalInvariants(*this, Fixture.Catalog, Evidence, Label);
	}
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FC11AEndTurnOrdering,
	"PokemonSolarus.Battle.C11A.Single.Conditions.EndTurnOrderNoRecursiveOrDoubleTrigger",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FC11AEndTurnOrdering::RunTest(const FString& Parameters)
{
	FCatalogFixture Fixture;
	FBattleSetup Setup;
	FSetupSpec Spec = ConditionSpec(11420,
		{FName(TEXT("Move.Toxic")), FName(TEXT("Move.LeechSeed")),
			FName(TEXT("Move.Sandstorm")), FName(TEXT("Move.SwordsDance"))},
		{FName(TEXT("Move.SwordsDance"))});
	Spec.Battlers[1].SpeciesId = TEXT("Species.Espathra");
	Spec.Battlers[1].AbilityId = TEXT("Ability.SpeedBoost");
	if (!Build(*this, Spec, Fixture, Setup)) return false;
	const TArray<TPair<FName, FName>> Turns = {
		{FName(TEXT("Move.Toxic")), FName(TEXT("Move.SwordsDance"))},
		{FName(TEXT("Move.LeechSeed")), FName(TEXT("Move.SwordsDance"))},
		{FName(TEXT("Move.Sandstorm")), FName(TEXT("Move.SwordsDance"))},
		{FName(TEXT("Move.SwordsDance")), FName(TEXT("Move.SwordsDance"))}};
	const FDriveFunction Drive = [Turns](FBattleEngine& Engine, FRunEvidence& Evidence, FString& Error)
	{
		return RunTurns(Engine, Evidence, Error, Turns);
	};
	FRunEvidence Evidence;
	if (!RunDeterministicTwins(*this, TEXT("end-turn ordering"),
		Fixture.Catalog, Setup, Drive, &Evidence)) return false;
	int32 ResidualFacts = 0;
	for (const FBattleResolution& Resolution : Evidence.Replay.GetResolutions())
	{
		TSet<FString> SeenResidualFacts;
		for (const FBattleEvent& Event : Resolution.GetEvents())
		{
			if (Event.GetCauseActionKind() != EBattleActionKind::Residual) continue;
			const FBattlerId Target = Event.GetTargets().IsEmpty() ? FBattlerId() : Event.GetTargets()[0].BattlerId;
			const FString Key = FString::Printf(TEXT("%u/%llu/%s/%llu"), static_cast<uint8>(Event.GetType()),
				Target.IsValid() ? Target.GetValue() : 0, *Event.GetSource().DefinitionId.GetName().ToString(),
				Event.GetSource().BattlerId.IsValid() ? Event.GetSource().BattlerId.GetValue() : 0);
			TestFalse(TEXT("One residual fact is never published twice in one resolution"), SeenResidualFacts.Contains(Key));
			SeenResidualFacts.Add(Key);
			++ResidualFacts;
		}
	}
	TestTrue(TEXT("The combined status/field/item setup reaches residual work"), ResidualFacts > 0);
	TestTrue(TEXT("End-turn damage is published before the matching healing pass"),
		HasEvent(Evidence.Replay, EBattleEventType::Damage)
		&& HasEvent(Evidence.Replay, EBattleEventType::Healing));
	const uint64 Sandstorm = FindResidualOrdinal(Evidence.Replay, TEXT("Condition.Sandstorm"), EBattleEventType::Damage);
	const uint64 Leftovers = FindResidualOrdinal(Evidence.Replay, TEXT("Item.Leftovers"), EBattleEventType::Healing);
	const uint64 LeechSeed = FindResidualOrdinal(Evidence.Replay, TEXT("Condition.LeechSeed"), EBattleEventType::Damage);
	const uint64 Toxic = FindResidualOrdinal(Evidence.Replay, TEXT("Condition.Toxic"), EBattleEventType::Damage);
	TestTrue(FString::Printf(TEXT("Residual event ordinals follow weather, item, Leech Seed, then Toxic (%llu/%llu/%llu/%llu)"),
		Sandstorm, Leftovers, LeechSeed, Toxic), Sandstorm > 0 && Leftovers > Sandstorm && LeechSeed > Leftovers && Toxic > LeechSeed);
	return ValidateGlobalInvariants(*this, Fixture.Catalog, Evidence, TEXT("end-turn ordering"));
}

#endif // WITH_DEV_AUTOMATION_TESTS
