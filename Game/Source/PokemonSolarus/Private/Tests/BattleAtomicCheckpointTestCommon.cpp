#if WITH_DEV_AUTOMATION_TESTS

#include "BattleAtomicCheckpointTestCommon.h"

namespace BattleAtomicCheckpointTestCommonPrivate
{
	const uint64 PlayerTrainerValue = 1;
	const uint64 OpponentTrainerValue = 2;
	const uint64 PartnerTrainerValue = 3;
	const uint64 PlayerLeftValue = 11;
	const uint64 PlayerRightValue = 12;
	const uint64 PlayerReserveValue = 13;
	const uint64 PlayerSecondReserveValue = 14;
	const uint64 OpponentLeftValue = 21;
	const uint64 OpponentRightValue = 22;
	const uint64 OpponentReserveValue = 23;

	const TCHAR* const PlayerSpeciesName = TEXT("Species.ADR0002.3D1.Player");
	const TCHAR* const WildSpeciesName = TEXT("Species.ADR0002.3D1.Wild");
	const TCHAR* const ProbeMoveName = TEXT("Move.ADR0002.3D1.Probe");
	const TCHAR* const TargetProbeMoveName = TEXT("Move.ADR0002.3E1.TargetProbe");
	const TCHAR* const PivotProbeMoveName = TEXT("Move.ADR0002.3E3.PivotProbe");
	const TCHAR* const ThawProbeMoveName = TEXT("Move.ADR0002.3E4.ThawProbe");
	const TCHAR* const ChargeProbeMoveName = TEXT("Move.ADR0002.3E4.ChargeProbe");
	const TCHAR* const RandomTargetProbeMoveName = TEXT("Move.ADR0002.3E5.RandomTargetProbe");
	const TCHAR* const RandomExecutionProbeMoveName = TEXT("Move.ADR0002.3E6.RandomExecutionProbe");
	const TCHAR* const ForcedEntryProbeMoveName = TEXT("Move.ADR0002.3E6.ForcedEntryProbe");
	const TCHAR* const CaptureHeldItemName = TEXT("Item.ADR0002.3D3.Capture.Held");

TArray<FBattleTypeChartEntry> MakeNeutralTypeChart()
{
		TArray<FBattleTypeChartEntry> Entries;
		Entries.Reserve(FBattleTypeChart::EntryCount);
		for (int32 Attack = 0; Attack < FBattleTypeChart::TypeCount; ++Attack)
		{
			for (int32 Defense = 0; Defense < FBattleTypeChart::TypeCount; ++Defense)
			{
				Entries.Add({
					static_cast<EPokemonType>(Attack),
					static_cast<EPokemonType>(Defense),
					1,
					1});
			}
		}
		return Entries;
	}

FBattleMoveDefinition MakeProbeMove()
{
		FBattleMoveDefinition Move;
		Move.Id = MakeDefinitionId<FMoveId>(ProbeMoveName);
		Move.Type = EPokemonType::Normal;
		Move.Category = EBattleMoveCategory::Status;
		Move.bAlwaysHits = true;
		Move.BasePP = 20;
		Move.TargetClass = EBattleTargetClass::Self;
		FBattleMoveEffectDescriptor Effect;
		Effect.Kind = EBattleMoveEffectKind::ModifyStatStage;
		Effect.Target = EBattleEffectTarget::User;
		Effect.Stat = EBattleStat::Attack;
		Effect.MagnitudeNumerator = 1;
		Move.Effects.Add(Effect);
		return Move;
	}

FBattleMoveDefinition MakeTargetProbeMove()
{
		FBattleMoveDefinition Move = MakeProbeMove();
		Move.Id = MakeDefinitionId<FMoveId>(TargetProbeMoveName);
		Move.TargetClass = EBattleTargetClass::SelectedOpponent;
		return Move;
	}

FBattleMoveDefinition MakeRandomTargetProbeMove()
{
		FBattleMoveDefinition Move = MakeProbeMove();
		Move.Id = MakeDefinitionId<FMoveId>(RandomTargetProbeMoveName);
		Move.TargetClass = EBattleTargetClass::RandomLegalOpponent;
		return Move;
	}

FBattleMoveDefinition MakeRandomExecutionProbeMove()
{
		FBattleMoveDefinition Move;
		Move.Id = MakeDefinitionId<FMoveId>(RandomExecutionProbeMoveName);
		Move.Type = EPokemonType::Normal;
		Move.Category = EBattleMoveCategory::Physical;
		Move.Power = 40;
		Move.bAlwaysHits = false;
		Move.Accuracy = 100;
		Move.BasePP = 20;
		Move.TargetClass = EBattleTargetClass::SelectedOpponent;
		Move.Flags = EBattleMoveFlags::NeverCritical;
		FBattleMoveEffectDescriptor Damage;
		Damage.Order = 0;
		Damage.Kind = EBattleMoveEffectKind::Damage;
		Damage.Target = EBattleEffectTarget::ResolvedTarget;
		Move.Effects.Add(Damage);
		FBattleMoveEffectDescriptor Sleep;
		Sleep.Order = 1;
		Sleep.Kind = EBattleMoveEffectKind::ApplyCondition;
		Sleep.Target = EBattleEffectTarget::ResolvedTarget;
		Sleep.ConditionId = FBattleMajorStatusRules::GetSleepId();
		Move.Effects.Add(Sleep);
		return Move;
	}

FBattleMoveDefinition MakeForcedEntryProbeMove()
{
		FBattleMoveDefinition Move;
		Move.Id = MakeDefinitionId<FMoveId>(ForcedEntryProbeMoveName);
		Move.Type = EPokemonType::Normal;
		Move.Category = EBattleMoveCategory::Status;
		Move.bAlwaysHits = true;
		Move.BasePP = 20;
		Move.TargetClass = EBattleTargetClass::SelectedOpponent;
		FBattleMoveEffectDescriptor Switch;
		Switch.Kind = EBattleMoveEffectKind::Switch;
		Switch.Target = EBattleEffectTarget::ResolvedTarget;
		Move.Effects.Add(Switch);
		return Move;
	}

FBattleMoveDefinition MakePivotProbeMove()
{
		FBattleMoveDefinition Move;
		Move.Id = MakeDefinitionId<FMoveId>(PivotProbeMoveName);
		Move.Type = EPokemonType::Normal;
		Move.Category = EBattleMoveCategory::Status;
		Move.bAlwaysHits = true;
		Move.BasePP = 20;
		Move.TargetClass = EBattleTargetClass::SelectedOpponent;
		FBattleMoveEffectDescriptor Effect;
		Effect.Order = 0;
		Effect.Kind = EBattleMoveEffectKind::Switch;
		Effect.Target = EBattleEffectTarget::User;
		Move.Effects.Add(Effect);
		return Move;
	}

FBattleMoveDefinition MakeThawProbeMove()
{
		FBattleMoveDefinition Move = MakeProbeMove();
		Move.Id = MakeDefinitionId<FMoveId>(ThawProbeMoveName);
		Move.Flags |= EBattleMoveFlags::ThawsUser;
		return Move;
	}

FBattleMoveDefinition MakeChargeProbeMove()
{
		FBattleMoveDefinition Move;
		Move.Id = MakeDefinitionId<FMoveId>(ChargeProbeMoveName);
		Move.Type = EPokemonType::Normal;
		Move.Category = EBattleMoveCategory::Physical;
		Move.Power = 60;
		Move.bAlwaysHits = true;
		Move.bUsesPP = true;
		Move.BasePP = 20;
		Move.bAllowsPPBoosts = true;
		Move.TargetClass = EBattleTargetClass::SelectedOpponent;
		Move.Flags = EBattleMoveFlags::NeverCritical
			| EBattleMoveFlags::BlockedByProtect;
		FBattleMoveEffectDescriptor Charge;
		Charge.Order = 0;
		Charge.Kind = EBattleMoveEffectKind::Charge;
		Charge.Target = EBattleEffectTarget::User;
		Charge.ConditionId = FBattleVolatileRules::GetChargingId();
		Move.Effects.Add(Charge);
		FBattleMoveEffectDescriptor Fly;
		Fly.Order = 1;
		Fly.Kind = EBattleMoveEffectKind::SemiInvulnerability;
		Fly.Target = EBattleEffectTarget::User;
		Fly.ConditionId = FBattleVolatileRules::GetFlySemiInvulnerableId();
		Move.Effects.Add(Fly);
		FBattleMoveEffectDescriptor Damage;
		Damage.Order = 2;
		Damage.Kind = EBattleMoveEffectKind::Damage;
		Damage.Target = EBattleEffectTarget::ResolvedTarget;
		Move.Effects.Add(Damage);
		return Move;
	}

FBattleSpeciesFormDefinition MakeSpecies(
		const TCHAR* Name,
		const int32 CatchRate )
{
		FBattleSpeciesFormDefinition Species;
		Species.Id = MakeDefinitionId<FSpeciesFormId>(Name);
		Species.PrimaryType = EPokemonType::Normal;
		Species.BaseStats = {80, 80, 80, 80, 80, 80};
		Species.CatchRate = CatchRate;
		Species.AbilityChoices.Add(FBattleAbilityRules::GetBlazeId());
		Species.AbilityChoices.Add(FBattleAbilityRules::GetIntimidateId());
		Species.AbilityChoices.Add(FBattleAbilityRules::GetMagicGuardId());
		return Species;
	}

FBattleDefinitionCatalog MakeCatalog(const FAtomicWildScenario& Scenario)
{
		FBattleDefinitionCatalogInput Input;
		Input.TypeChartEntries = MakeNeutralTypeChart();
		Input.Moves.Add(MakeProbeMove());
		Input.Moves.Add(MakeTargetProbeMove());
		Input.Moves.Add(MakePivotProbeMove());
		Input.Moves.Add(MakeThawProbeMove());
		Input.Moves.Add(MakeChargeProbeMove());
		Input.Moves.Add(MakeRandomTargetProbeMove());
		Input.Moves.Add(MakeRandomExecutionProbeMove());
		Input.Moves.Add(MakeForcedEntryProbeMove());
		Input.Abilities.Add({FBattleAbilityRules::GetBlazeId()});
		Input.Abilities.Add({FBattleAbilityRules::GetIntimidateId()});
		Input.Abilities.Add({FBattleAbilityRules::GetMagicGuardId()});
		Input.Items.Add({FBattleBagItemRules::GetPokeBallId(), EBattleItemKind::Capture});
		Input.Items.Add({FBattleItemRules::GetLeftoversId(), EBattleItemKind::Held});
		Input.Items.Add({FBattleItemRules::GetSitrusBerryId(), EBattleItemKind::Held});
		Input.Items.Add({FBattleItemRules::GetAirBalloonId(), EBattleItemKind::Held});
		Input.Items.Add({FBattleItemRules::GetLumBerryId(), EBattleItemKind::Held});
		Input.Items.Add({FBattleItemRules::GetChoiceBandId(), EBattleItemKind::Held});
		Input.Items.Add({FBattleItemRules::GetHeavyDutyBootsId(), EBattleItemKind::Held});
		Input.Items.Add({
			MakeDefinitionId<FItemId>(CaptureHeldItemName),
			EBattleItemKind::Held});
		Input.Conditions.Add({
			FBattleFieldSideConditionRules::GetMagicRoomId(),
			EBattleConditionKind::Room});
		Input.Conditions.Add({
			FBattleFieldSideConditionRules::GetSpikesId(),
			EBattleConditionKind::Hazard});
		Input.Conditions.Add({
			FBattleFieldSideConditionRules::GetStealthRockId(),
			EBattleConditionKind::Hazard});
		for (const FConditionId& VolatileId : FBattleVolatileRules::GetCanonicalIds())
		{
			Input.Conditions.Add({VolatileId, EBattleConditionKind::Volatile});
		}
		for (const FConditionId& StatusId : FBattleMajorStatusRules::GetCanonicalIds())
		{
			Input.Conditions.Add({StatusId, EBattleConditionKind::MajorStatus});
		}
		Input.SpeciesForms.Add(MakeSpecies(PlayerSpeciesName));
		Input.SpeciesForms.Add(MakeSpecies(WildSpeciesName, Scenario.CatchRate));

		FBattleDefinitionCatalog Catalog;
		TArray<FBattleCatalogDiagnostic> Diagnostics;
		const bool bCreated = FBattleDefinitionCatalog::TryCreate(Input, Catalog, Diagnostics);
		check(bCreated);
		return Catalog;
	}

FBattleTrainerSetup MakeTrainer(
		const uint64 TrainerValue,
		const EBattleSide Side,
		const EBattleTrainerRole Role,
		const EBattleDecisionController Controller,
		const int32 PokeBallCount )
{
		FBattleTrainerSetup Trainer;
		Trainer.TrainerId = MakeNumericId<FTrainerId>(TrainerValue);
		Trainer.Side = Side;
		Trainer.Role = Role;
		Trainer.Controller = Controller;
		Trainer.SelectorProfileId = MakeDefinitionId<FDefinitionId>(
			Role == EBattleTrainerRole::Player
				? TEXT("Selector.ADR0002.3D1.Player")
				: TEXT("Selector.ADR0002.3D1.Opponent"));
		if (PokeBallCount >= 0)
		{
			Trainer.Bag.Add({FBattleBagItemRules::GetPokeBallId(), PokeBallCount});
		}
		return Trainer;
	}

FBattlePartyEntrySetup MakePartyEntry(
		const uint64 TrainerValue,
		const uint64 BattlerValue,
		const int32 PartyIndex,
		const TCHAR* SpeciesName,
		const int32 Speed,
		const int32 CurrentHP ,
		const FItemId OriginalHeldItemId ,
		const FItemId CurrentHeldItemId ,
		const FAbilityId AbilityId ,
		const bool bAddPivotMove ,
		const FMoveId ExtraMoveId )
{
		FBattlePartyEntrySetup Entry;
		Entry.TrainerId = MakeNumericId<FTrainerId>(TrainerValue);
		Entry.BattlerId = MakeNumericId<FBattlerId>(BattlerValue);
		Entry.SourcePokemonId = MakeNumericId<FSourcePokemonId>(3000 + BattlerValue);
		Entry.PartySlotId = MakePartySlotId(PartyIndex);
		Entry.SpeciesFormId = MakeDefinitionId<FSpeciesFormId>(SpeciesName);
		Entry.Level = 50;
		Entry.Stats = {200, 100, 100, 100, 100, Speed};
		Entry.CurrentHP = CurrentHP;
		Entry.AbilityId = AbilityId;
		Entry.OriginalHeldItemId = OriginalHeldItemId;
		Entry.CurrentHeldItemId = CurrentHeldItemId;
		Entry.CaptureClassification = EBattleCaptureSpeciesClassification::Normal;
		Entry.Moves.Add({0, MakeDefinitionId<FMoveId>(ProbeMoveName), 20, 20});
		Entry.Moves.Add({1, MakeDefinitionId<FMoveId>(TargetProbeMoveName), 20, 20});
		if (bAddPivotMove)
		{
			Entry.Moves.Add({2, MakeDefinitionId<FMoveId>(PivotProbeMoveName), 20, 20});
		}
		else if (ExtraMoveId.IsValid())
		{
			Entry.Moves.Add({2, ExtraMoveId, 20, 20});
		}
		return Entry;
	}

FBattleActiveAssignment MakeActive(
		const EBattleSide Side,
		const EBattlePosition Position,
		const uint64 TrainerValue,
		const uint64 BattlerValue)
{
		return {
			MakeActiveSlotId(Side, Position),
			MakeNumericId<FTrainerId>(TrainerValue),
			MakeNumericId<FBattlerId>(BattlerValue)};
	}

FBattleSetupInput MakeSetupInput(const FAtomicWildScenario& Scenario)
{
		FBattleSetupInput Input;
		Input.BattleId = MakeNumericId<FBattleId>(30031);
		Input.SettingsReference = {
			MakeDefinitionId<FDefinitionId>(TEXT("Settings.ADR0002.3D1")),
			1};
		Input.CatalogReference = {
			MakeDefinitionId<FDefinitionId>(TEXT("Catalog.ADR0002.3D1")),
			1};
		Input.EncounterKind = Scenario.bTrainerEncounter
			|| Scenario.Format == EBattleFormat::PartnerDouble
			? EBattleEncounterKind::Trainer
			: EBattleEncounterKind::Wild;
		Input.Format = Scenario.Format;
		if (Scenario.bCaptureFlow)
		{
			Input.CaptureCapacity = {
				Scenario.PartyCaptureCapacity,
				Scenario.StorageCaptureCapacity};
			Input.CaptureProgression = Scenario.CaptureProgression;
		}
		Input.Policies.bBagAllowed = Scenario.bCaptureFlow;
		Input.Policies.bCaptureAllowed = Scenario.bCaptureFlow;
		Input.Policies.bRunAllowed = !Scenario.bTrainerEncounter
			&& Scenario.Format != EBattleFormat::PartnerDouble;
		Input.Policies.bShiftPromptEligible = false;
		Input.Policies.WildFleeMode = Scenario.WildFleeMode;
		Input.Policies.WildFleeNumerator = Scenario.WildFleeNumerator;
		Input.Policies.WildFleeDenominator = Scenario.WildFleeDenominator;

		Input.Trainers.Add(MakeTrainer(
			PlayerTrainerValue,
			EBattleSide::Player,
			EBattleTrainerRole::Player,
			EBattleDecisionController::Human,
			Scenario.bCaptureFlow ? Scenario.PokeBallCount : -1));
		Input.Trainers.Add(MakeTrainer(
			OpponentTrainerValue,
			EBattleSide::Opponent,
			EBattleTrainerRole::Opponent,
			EBattleDecisionController::EnemyAI));
		if (Scenario.Format == EBattleFormat::PartnerDouble)
		{
			Input.Trainers.Add(MakeTrainer(
				PartnerTrainerValue,
				EBattleSide::Player,
				EBattleTrainerRole::Partner,
				EBattleDecisionController::PartnerAI));
		}

		Input.PartyEntries.Add(MakePartyEntry(
			PlayerTrainerValue,
			PlayerLeftValue,
			0,
			PlayerSpeciesName,
			Scenario.PlayerLeftSpeed,
			Scenario.PlayerCurrentHP,
			Scenario.PlayerHeldItemId.IsValid()
				? Scenario.PlayerHeldItemId
				: Scenario.bPlayerHasCanonicalHeldItem
				? FBattleItemRules::GetLeftoversId()
				: FItemId(),
			Scenario.PlayerHeldItemId.IsValid()
				? Scenario.PlayerHeldItemId
				: Scenario.bPlayerHasCanonicalHeldItem
				? FBattleItemRules::GetLeftoversId()
				: FItemId(),
			Scenario.PlayerAbilityId.IsValid()
				? Scenario.PlayerAbilityId
				: FBattleAbilityRules::GetBlazeId(),
			Scenario.bPivotSwitchFlow,
			Scenario.PlayerExtraMoveId));
		Input.PartyEntries.Add(MakePartyEntry(
			OpponentTrainerValue,
			OpponentLeftValue,
			0,
			WildSpeciesName,
			Scenario.OpponentLeftSpeed,
			Scenario.TargetCurrentHP,
			Scenario.bCaptureFlow
				? MakeDefinitionId<FItemId>(CaptureHeldItemName)
				: FItemId(),
			Scenario.bCaptureFlow
				? MakeDefinitionId<FItemId>(CaptureHeldItemName)
				: FItemId()));
		if (Scenario.bOpponentSwitchReserve)
		{
			Input.PartyEntries.Add(MakePartyEntry(
				OpponentTrainerValue,
				OpponentReserveValue,
				1,
				WildSpeciesName,
				45,
				Scenario.OpponentReserveCurrentHP,
				Scenario.OpponentReserveHeldItemId,
				Scenario.OpponentReserveHeldItemId,
				Scenario.OpponentReserveAbilityId.IsValid()
					? Scenario.OpponentReserveAbilityId
					: FBattleAbilityRules::GetBlazeId()));
		}
		if (Scenario.bVoluntarySwitchFlow)
		{
			Input.PartyEntries.Add(MakePartyEntry(
				PlayerTrainerValue,
				PlayerReserveValue,
				Scenario.Format == EBattleFormat::Single ? 1 : 2,
				PlayerSpeciesName,
				60,
				Scenario.SwitchIncomingCurrentHP,
				Scenario.SwitchIncomingHeldItemId,
				Scenario.SwitchIncomingHeldItemId,
				Scenario.SwitchIncomingAbilityId.IsValid()
					? Scenario.SwitchIncomingAbilityId
					: FBattleAbilityRules::GetBlazeId()));
			if (Scenario.bSecondSwitchReserve)
			{
				Input.PartyEntries.Add(MakePartyEntry(
					PlayerTrainerValue,
					PlayerSecondReserveValue,
					Scenario.Format == EBattleFormat::Single ? 2 : 3,
					PlayerSpeciesName,
					55));
			}
		}
		Input.StartingActive.Add(MakeActive(
			EBattleSide::Player,
			EBattlePosition::Left,
			PlayerTrainerValue,
			PlayerLeftValue));
		Input.StartingActive.Add(MakeActive(
			EBattleSide::Opponent,
			EBattlePosition::Left,
			OpponentTrainerValue,
			OpponentLeftValue));

		if (Scenario.Format == EBattleFormat::Double
			|| Scenario.Format == EBattleFormat::PartnerDouble)
		{
			Input.PartyEntries.Add(MakePartyEntry(
				Scenario.Format == EBattleFormat::PartnerDouble
					? PartnerTrainerValue
					: PlayerTrainerValue,
				PlayerRightValue,
				Scenario.Format == EBattleFormat::PartnerDouble ? 0 : 1,
				PlayerSpeciesName,
				Scenario.PlayerRightSpeed));
			Input.PartyEntries.Add(MakePartyEntry(
				OpponentTrainerValue,
				OpponentRightValue,
				1,
				WildSpeciesName,
				Scenario.OpponentRightSpeed));
			Input.StartingActive.Add(MakeActive(
				EBattleSide::Player,
				EBattlePosition::Right,
				Scenario.Format == EBattleFormat::PartnerDouble
					? PartnerTrainerValue
					: PlayerTrainerValue,
				PlayerRightValue));
			Input.StartingActive.Add(MakeActive(
				EBattleSide::Opponent,
				EBattlePosition::Right,
				OpponentTrainerValue,
				OpponentRightValue));
		}
		if (Scenario.bPlayerSubjectToObedience)
		{
			Input.ObedienceInputs.Add({
				MakeNumericId<FBattlerId>(PlayerLeftValue),
				true,
				Scenario.PlayerReferenceLevel,
				Scenario.PlayerBadgeCount});
		}
		return Input;
	}

bool TryMakeEngine(
		const FAtomicWildScenario& Scenario,
		TUniquePtr<IBattleRandom>&& Random,
		TUniquePtr<FBattleEngine>& OutEngine)
{
		FBattleSetup Setup;
		EBattleSetupValidationError SetupError = EBattleSetupValidationError::None;
		if (!FBattleSetup::TryCreate(MakeSetupInput(Scenario), Setup, SetupError))
		{
			return false;
		}
		FBattleRejection Rejection;
		return FBattleEngine::TryCreate(
			Setup,
			MakeCatalog(Scenario),
			MoveTemp(Random),
			OutEngine,
			Rejection);
	}

bool TryMakeSequenceEngine(
		const FAtomicWildScenario& Scenario,
		TArray<uint32> Results,
		TUniquePtr<FBattleEngine>& OutEngine)
{
		TUniquePtr<IBattleRandom> Random =
			MakeUnique<FSequenceBattleRandom>(MoveTemp(Results));
		return TryMakeEngine(Scenario, MoveTemp(Random), OutEngine);
	}

bool TryMakeStrictEngine(
		const FAtomicWildScenario& Scenario,
		TArray<FBattleExpectedRandomDraw> ExpectedDraws,
		TUniquePtr<FBattleEngine>& OutEngine,
		FStrictBattleRandom*& OutRandom)
{
		TUniquePtr<FStrictBattleRandom> Strict =
			MakeUnique<FStrictBattleRandom>(MoveTemp(ExpectedDraws));
		OutRandom = Strict.Get();
		TUniquePtr<IBattleRandom> Random = MoveTemp(Strict);
		return TryMakeEngine(Scenario, MoveTemp(Random), OutEngine);
	}

FAtomicWildScenario MakeAtomicCaptureScenario()
{
		FAtomicWildScenario Scenario;
		Scenario.bCaptureFlow = true;
		Scenario.CaptureProgression.bHasSnapshot = true;
		Scenario.CaptureProgression.BadgeCount = 8;
		Scenario.CaptureProgression.CaptureCoefficientQ12 = 4096;
		return Scenario;
	}

FBattleDecision MakeDecision(
		const FBattleDecisionRequest& Request,
		const EBattleActionKind ActionKind,
		const FMoveId FightMoveId )
{
		FBattleDecision Decision;
		bool bCreated = false;
		if (ActionKind == EBattleActionKind::Fight)
		{
			if (FightMoveId.IsValid())
			{
				if (Request.GetAutomaticallyTargetedMoveIds().Contains(FightMoveId))
				{
					bCreated = FBattleDecision::TryCreateAutomaticallyTargetedFight(
						Request.GetStateVersion(),
						Request.GetDecisionOwnerTrainerId(),
						Request.GetActingBattlerId(),
						FightMoveId,
						Decision);
				}
				else
				{
					const FBattleMoveTargetOption* Target =
						Request.GetLegalMoveTargets().FindByPredicate(
							[FightMoveId](const FBattleMoveTargetOption& Option)
							{
								return Option.MoveId == FightMoveId;
							});
					check(Target != nullptr);
					bCreated = FBattleDecision::TryCreateFight(
						Request.GetStateVersion(),
						Request.GetDecisionOwnerTrainerId(),
						Request.GetActingBattlerId(),
						FightMoveId,
						Target->ActiveSlotId,
						Decision);
				}
			}
			else
			{
				bCreated = FBattleDecision::TryCreateAutomaticallyTargetedFight(
					Request.GetStateVersion(),
					Request.GetDecisionOwnerTrainerId(),
					Request.GetActingBattlerId(),
					MakeDefinitionId<FMoveId>(ProbeMoveName),
					Decision);
			}
		}
		else if (ActionKind == EBattleActionKind::Bag)
		{
			const FItemId PokeBallId = FBattleBagItemRules::GetPokeBallId();
			const FBattleItemActiveTargetOption* Target =
				Request.GetLegalItemActiveTargets().FindByPredicate(
					[PokeBallId](const FBattleItemActiveTargetOption& Option)
					{
						return Option.ItemId == PokeBallId
							&& Option.ActiveSlotId == MakeActiveSlotId(
								EBattleSide::Opponent,
								EBattlePosition::Left);
					});
			check(Target != nullptr);
			bCreated = FBattleDecision::TryCreateBag(
				Request.GetStateVersion(),
				Request.GetDecisionOwnerTrainerId(),
				Request.GetActingBattlerId(),
				PokeBallId,
				FPartySlotId(),
				Target->ActiveSlotId,
				Decision);
		}
		else if (ActionKind == EBattleActionKind::Run
			|| ActionKind == EBattleActionKind::WildFlee)
		{
			bCreated = FBattleDecision::TryCreateSimpleAction(
				Request.GetStateVersion(),
				Request.GetRequestKind(),
				Request.GetDecisionOwnerTrainerId(),
				Request.GetActingBattlerId(),
				ActionKind,
				Decision);
		}
		check(bCreated);
		return Decision;
	}

FBattleDecisionBatch MakeBatch(
		const TArray<FBattleDecisionRequest>& Requests,
		TArray<FBattleDecision> Decisions)
{
		check(!Requests.IsEmpty());
		FBattleDecisionBatchSpec Spec;
		Spec.StateVersion = Requests[0].GetStateVersion();
		Spec.RequestKind = Requests[0].GetRequestKind();
		Spec.DecisionOwnerTrainerId = Requests[0].GetDecisionOwnerTrainerId();
		Spec.Decisions = MoveTemp(Decisions);
		FBattleDecisionBatch Batch;
		FBattleRejection Rejection;
		const bool bCreated = FBattleDecisionBatch::TryCreate(Spec, Batch, Rejection);
		check(bCreated);
		return Batch;
	}

bool LockTurn(
		FBattleEngine& Engine,
		const uint64 SpecialBattlerValue,
		const EBattleActionKind SpecialAction,
		const FMoveId SpecialFightMoveId )
{
		if (Engine.GetSnapshot().GetPhase() == EBattlePhase::Setup)
		{
			FBattleRejection Rejection;
			if (!Engine.TryBeginActionDecisionSequence(Rejection))
			{
				return false;
			}
		}

		int32 Guard = 0;
		while (!Engine.GetPendingDecisionRequests().IsEmpty() && Guard++ < 4)
		{
			const TArray<FBattleDecisionRequest> Requests = Engine.GetPendingDecisionRequests();
			TArray<FBattleDecision> Decisions;
			for (const FBattleDecisionRequest& Request : Requests)
			{
				const EBattleActionKind Choice = Request.GetActingBattlerId()
					== MakeNumericId<FBattlerId>(SpecialBattlerValue)
					? SpecialAction
					: EBattleActionKind::Fight;
				Decisions.Add(MakeDecision(
					Request,
					Choice,
					Request.GetActingBattlerId()
						== MakeNumericId<FBattlerId>(SpecialBattlerValue)
						? SpecialFightMoveId
						: FMoveId()));
			}
			if (!Engine.SubmitDecisionBatch(
					MakeBatch(Requests, MoveTemp(Decisions))).WasAccepted())
			{
				return false;
			}
		}
		return Guard < 4 && Engine.GetSnapshot().GetPhase() == EBattlePhase::Locked;
	}

bool BeginExpectedWildAction(
		FBattleEngine& Engine,
		const uint64 BattlerValue,
		const EBattleActionKind ActionKind)
{
		if (!Engine.BeginNextLockedAction().WasAccepted())
		{
			return false;
		}
		const TOptional<FBattleLockedAction> Current = Engine.GetCurrentLockedAction();
		return Current.IsSet()
			&& Current->Decision.GetActingBattlerId()
				== MakeNumericId<FBattlerId>(BattlerValue)
			&& Current->Decision.GetActionKind() == ActionKind;
	}

bool HasEvent(const FBattleResolution& Resolution, const EBattleEventType Type)
{
		return Resolution.GetEvents().ContainsByPredicate(
			[Type](const FBattleEvent& Event)
			{
				return Event.GetType() == Type;
			});
	}

bool HasExactEventOrder(
		const FBattleResolution& Resolution,
		const TArray<EBattleEventType>& Expected)
{
		if (Resolution.GetEvents().Num() != Expected.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < Expected.Num(); ++Index)
		{
			if (Resolution.GetEvents()[Index].GetType() != Expected[Index])
			{
				return false;
			}
		}
		return true;
	}

bool AreEventSourcesIdentical(
		const FBattleEventSource& Left,
		const FBattleEventSource& Right)
{
		return Left.TrainerId == Right.TrainerId
			&& Left.BattlerId == Right.BattlerId
			&& Left.ActiveSlotId == Right.ActiveSlotId
			&& Left.DefinitionId == Right.DefinitionId;
	}

bool AreEventTargetsIdentical(
		const TConstArrayView<FBattleEventTarget> Left,
		const TConstArrayView<FBattleEventTarget> Right)
{
		if (Left.Num() != Right.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < Left.Num(); ++Index)
		{
			if (Left[Index].TrainerId != Right[Index].TrainerId
				|| Left[Index].BattlerId != Right[Index].BattlerId
				|| Left[Index].ActiveSlotId != Right[Index].ActiveSlotId
				|| Left[Index].Side != Right[Index].Side
				|| Left[Index].bHasSide != Right[Index].bHasSide
				|| Left[Index].bField != Right[Index].bField)
			{
				return false;
			}
		}
		return true;
	}

bool AreEventsIdentical(const FBattleEvent& Left, const FBattleEvent& Right)
{
		if (Left.IsValid() != Right.IsValid()
			|| Left.GetEventOrdinal() != Right.GetEventOrdinal()
			|| Left.GetBattleId() != Right.GetBattleId()
			|| Left.GetTurnId() != Right.GetTurnId()
			|| Left.GetActionId() != Right.GetActionId()
			|| Left.GetResolutionId() != Right.GetResolutionId()
			|| Left.GetType() != Right.GetType()
			|| Left.GetCause() != Right.GetCause()
			|| Left.GetCauseActionKind() != Right.GetCauseActionKind()
			|| Left.GetOutcomeCause() != Right.GetOutcomeCause()
			|| !AreEventSourcesIdentical(Left.GetSource(), Right.GetSource())
			|| !AreEventTargetsIdentical(Left.GetTargets(), Right.GetTargets())
			|| Left.GetNumericBefore() != Right.GetNumericBefore()
			|| Left.GetNumericAfter() != Right.GetNumericAfter()
			|| Left.GetNumericDelta() != Right.GetNumericDelta()
			|| Left.GetSimultaneousGroupId() != Right.GetSimultaneousGroupId()
			|| Left.GetHitIndex() != Right.GetHitIndex()
			|| Left.GetHitCount() != Right.GetHitCount()
			|| Left.GetActionOrder().IsSet() != Right.GetActionOrder().IsSet()
			|| Left.GetTargetResolution().IsSet()
				!= Right.GetTargetResolution().IsSet()
			|| Left.GetCapture().IsSet() != Right.GetCapture().IsSet())
		{
			return false;
		}

		if (Left.GetActionOrder().IsSet())
		{
			const FBattleActionOrderMetadata& L = Left.GetActionOrder().GetValue();
			const FBattleActionOrderMetadata& R = Right.GetActionOrder().GetValue();
			if (L.QueueOrdinal != R.QueueOrdinal
				|| L.OrderKey.CommandBand != R.OrderKey.CommandBand
				|| L.OrderKey.MovePriority != R.OrderKey.MovePriority
				|| L.OrderKey.FractionalPriorityTenths
					!= R.OrderKey.FractionalPriorityTenths
				|| L.OrderKey.EffectiveSpeed != R.OrderKey.EffectiveSpeed
				|| L.OrderKey.ActingSlotId != R.OrderKey.ActingSlotId
				|| L.bReverseSpeed != R.bReverseSpeed)
			{
				return false;
			}
		}
		if (Left.GetTargetResolution().IsSet())
		{
			const FBattleTargetResolutionMetadata& L =
				Left.GetTargetResolution().GetValue();
			const FBattleTargetResolutionMetadata& R =
				Right.GetTargetResolution().GetValue();
			if (L.TargetClass != R.TargetClass
				|| L.bWasRedirected != R.bWasRedirected
				|| L.bUsedFaintedTargetFallback != R.bUsedFaintedTargetFallback)
			{
				return false;
			}
		}
		if (Left.GetCapture().IsSet()
			&& !(Left.GetCapture().GetValue() == Right.GetCapture().GetValue()))
		{
			return false;
		}

		const FBattleEventVisibility& LVisibility = Left.GetVisibility();
		const FBattleEventVisibility& RVisibility = Right.GetVisibility();
		return LVisibility.Level == RVisibility.Level
			&& LVisibility.OwningTrainerId == RVisibility.OwningTrainerId
			&& LVisibility.OwningSide == RVisibility.OwningSide
			&& LVisibility.bHasOwningSide == RVisibility.bHasOwningSide
			&& LVisibility.bRevealSourceDefinition
				== RVisibility.bRevealSourceDefinition;
	}

bool AreActionStartHeldItemsIdentical(
		const FBattleHeldItemState& Left,
		const FBattleHeldItemState& Right)
{
		return Left.InstanceId == Right.InstanceId
			&& Left.OriginalItemId == Right.OriginalItemId
			&& Left.CurrentItemId == Right.CurrentItemId
			&& Left.bConsumed == Right.bConsumed
			&& Left.bSuppressed == Right.bSuppressed
			&& Left.bRevealed == Right.bRevealed
			&& Left.bTemporarilyRemoved == Right.bTemporarilyRemoved
			&& Left.ChoiceLockedMoveId == Right.ChoiceLockedMoveId;
	}

bool IsReturnedResolutionAppendedExactlyOnce(
		const FBattleEngine& Engine,
		const FBattleResolution& Returned)
{
		const FBattleEngineState& State =
			FBattleC09BWildFlowEngineFixture::GetState(Engine);
		if (State.Resolutions.IsEmpty())
		{
			return false;
		}
		const FBattleResolution& Appended = State.Resolutions.Last();
		const FBattleRejection& AppendedRejection = Appended.GetRejection();
		const FBattleRejection& ReturnedRejection = Returned.GetRejection();
		if (Appended.IsValid() != Returned.IsValid()
			|| Appended.GetResolutionId() != Returned.GetResolutionId()
			|| Appended.WasAccepted() != Returned.WasAccepted()
			|| Appended.GetBeforeStateVersion() != Returned.GetBeforeStateVersion()
			|| Appended.GetAfterStateVersion() != Returned.GetAfterStateVersion()
			|| AppendedRejection.Reason != ReturnedRejection.Reason
			|| AppendedRejection.TrainerId != ReturnedRejection.TrainerId
			|| AppendedRejection.BattlerId != ReturnedRejection.BattlerId
			|| AppendedRejection.ActionId != ReturnedRejection.ActionId
			|| AppendedRejection.MoveId != ReturnedRejection.MoveId
			|| AppendedRejection.ItemId != ReturnedRejection.ItemId
			|| AppendedRejection.PartySlotId != ReturnedRejection.PartySlotId
			|| AppendedRejection.ActiveSlotId != ReturnedRejection.ActiveSlotId
			|| Appended.GetEvents().Num() != Returned.GetEvents().Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < Appended.GetEvents().Num(); ++Index)
		{
			if (!AreEventsIdentical(
					Appended.GetEvents()[Index],
					Returned.GetEvents()[Index]))
			{
				return false;
			}
		}

		int32 MatchingResolutionCount = 0;
		for (const FBattleResolution& Candidate : State.Resolutions)
		{
			MatchingResolutionCount += Candidate.GetResolutionId()
				== Returned.GetResolutionId();
		}
		if (MatchingResolutionCount != 1)
		{
			return false;
		}
		for (const FBattleEvent& ReturnedEvent : Returned.GetEvents())
		{
			int32 MatchingEventCount = 0;
			for (const FBattleEvent& Candidate : State.OrderedEvents)
			{
				MatchingEventCount += Candidate.GetEventOrdinal()
					== ReturnedEvent.GetEventOrdinal();
			}
			if (MatchingEventCount != 1)
			{
				return false;
			}
		}
		return true;
	}

FCheckpointObservation ObserveCheckpoint(
		const FBattleEngine& Engine,
		const FBattlerId ObservedBattlerId)
{
		const FBattleEngineState& State =
			FBattleC09BWildFlowEngineFixture::GetState(Engine);
		FCheckpointObservation Observation;
		Observation.StateVersion = State.StateVersion;
		Observation.NextResolutionId = State.NextResolutionId;
		Observation.NextEventOrdinal = State.NextEventOrdinal;
		Observation.NextTriggerToken = State.NextTriggerReentrancyToken;
		Observation.EscapeAttemptCount = State.EscapeAttemptCount;
		Observation.LockedActionIndex = State.CurrentLockedActionIndex;
		Observation.ResolutionCount = State.Resolutions.Num();
		Observation.EventCount = State.OrderedEvents.Num();
		Observation.RandomTraceCount = State.Random->GetTrace().Num();
		Observation.PendingCaptureCount = State.PendingCaptures.Num();
		Observation.Phase = State.Phase;
		Observation.Outcome = State.Outcome;
		Observation.OutcomeCause = State.OutcomeCause;
		if (State.LockedActions.IsValidIndex(State.CurrentLockedActionIndex))
		{
			Observation.bActionStarted =
				State.LockedActions[State.CurrentLockedActionIndex].bStarted;
			Observation.bActionFinished =
				State.LockedActions[State.CurrentLockedActionIndex].bFinished;
		}
		const FBattleTrainerState* PlayerTrainer = State.FindTrainer(
			MakeNumericId<FTrainerId>(PlayerTrainerValue));
		if (PlayerTrainer != nullptr)
		{
			Observation.bBagActionAvailable =
				PlayerTrainer->ActionAllowance.bBagActionAvailable;
			const FItemId PokeBallId = FBattleBagItemRules::GetPokeBallId();
			const FBattleBagItemCount* PokeBall = PlayerTrainer->Bag.FindByPredicate(
				[PokeBallId](const FBattleBagItemCount& Candidate)
				{
					return Candidate.ItemId == PokeBallId;
				});
			Observation.PokeBallCount = PokeBall != nullptr ? PokeBall->Count : INDEX_NONE;
		}
		Observation.bObservedBattlerActive =
			FBattleC09BWildFlowEngineFixture::IsActive(Engine, ObservedBattlerId);
		const FBattleBattlerState* Observed = State.FindBattler(ObservedBattlerId);
		Observation.bObservedBattlerCaptured =
			Observed != nullptr && Observed->bCaptured;
		Observation.bObservedBattlerRemoved = Observed != nullptr && Observed->bRemoved;
		return Observation;
	}

bool VerifyRejectedCheckpoint(
		FAutomationTestBase& Test,
		const FBattleEngine& Engine,
		const FBattlerId ObservedBattlerId,
		const FCheckpointObservation& Before,
		const uint64 ExpectedStateVersion,
		const EBattleRejectionReason ExpectedReason,
		const FBattleResolution& Returned)
{
		const FBattleEngineState& State =
			FBattleC09BWildFlowEngineFixture::GetState(Engine);
		bool bValid = true;
		bValid &= Test.TestFalse(TEXT("Checkpoint failure is rejected"), Returned.WasAccepted());
		bValid &= Test.TestEqual(
			TEXT("Checkpoint failure has the expected typed reason"),
			Returned.GetRejection().Reason,
			ExpectedReason);
		bValid &= Test.TestTrue(
			TEXT("Returned rejection is the exact appended resolution"),
			IsReturnedResolutionAppendedExactlyOnce(Engine, Returned));
		bValid &= Test.TestEqual(
			TEXT("Exactly one rejection resolution is appended"),
			State.Resolutions.Num(),
			Before.ResolutionCount + 1);
		bValid &= Test.TestEqual(
			TEXT("Exactly one rejection event is appended"),
			State.OrderedEvents.Num(),
			Before.EventCount + 1);
		bValid &= Test.TestEqual(
			TEXT("The rejection consumes one invocation resolution identity"),
			State.NextResolutionId,
			Before.NextResolutionId + 1);
		bValid &= Test.TestEqual(
			TEXT("The rejection consumes one event ordinal"),
			State.NextEventOrdinal,
			Before.NextEventOrdinal + 1);
		bValid &= Test.TestEqual(
			TEXT("Rejection does not advance checkpoint state version"),
			State.StateVersion,
			ExpectedStateVersion);
		bValid &= Test.TestEqual(
			TEXT("Rejection does not advance the action cursor"),
			State.CurrentLockedActionIndex,
			Before.LockedActionIndex);
		bValid &= Test.TestEqual(
			TEXT("Rejection does not change the escape counter"),
			State.EscapeAttemptCount,
			Before.EscapeAttemptCount);
		bValid &= Test.TestEqual(TEXT("Rejection preserves phase"), State.Phase, Before.Phase);
		bValid &= Test.TestEqual(TEXT("Rejection preserves outcome"), State.Outcome, Before.Outcome);
		bValid &= Test.TestEqual(
			TEXT("Rejection preserves outcome cause"),
			State.OutcomeCause,
			Before.OutcomeCause);
		bValid &= Test.TestEqual(
			TEXT("Rejection leaves the parent RNG trace unchanged"),
			State.Random->GetTrace().Num(),
			Before.RandomTraceCount);
		const FBattleTrainerState* PlayerTrainer = State.FindTrainer(
			MakeNumericId<FTrainerId>(PlayerTrainerValue));
		const FItemId PokeBallId = FBattleBagItemRules::GetPokeBallId();
		const FBattleBagItemCount* PokeBall = PlayerTrainer != nullptr
			? PlayerTrainer->Bag.FindByPredicate(
				[PokeBallId](const FBattleBagItemCount& Candidate)
				{
					return Candidate.ItemId == PokeBallId;
				})
			: nullptr;
		bValid &= Test.TestEqual(
			TEXT("Rejection preserves the Poke Ball count"),
			PokeBall != nullptr ? PokeBall->Count : INDEX_NONE,
			Before.PokeBallCount);
		bValid &= Test.TestEqual(
			TEXT("Rejection preserves Trainer Bag quota"),
			PlayerTrainer != nullptr
				? PlayerTrainer->ActionAllowance.bBagActionAvailable
				: false,
			Before.bBagActionAvailable);
		bValid &= Test.TestEqual(
			TEXT("Rejection preserves pending captures"),
			State.PendingCaptures.Num(),
			Before.PendingCaptureCount);
		bValid &= Test.TestEqual(
			TEXT("Rejection preserves trigger-token state"),
			State.NextTriggerReentrancyToken,
			Before.NextTriggerToken);
		bValid &= Test.TestEqual(
			TEXT("Observed battler active state is unchanged"),
			FBattleC09BWildFlowEngineFixture::IsActive(Engine, ObservedBattlerId),
			Before.bObservedBattlerActive);
		bValid &= Test.TestEqual(
			TEXT("Observed battler removal state is unchanged"),
			FBattleC09BWildFlowEngineFixture::IsRemoved(Engine, ObservedBattlerId),
			Before.bObservedBattlerRemoved);
		const FBattleBattlerState* Observed = State.FindBattler(ObservedBattlerId);
		bValid &= Test.TestEqual(
			TEXT("Observed battler capture state is unchanged"),
			Observed != nullptr && Observed->bCaptured,
			Before.bObservedBattlerCaptured);
		bValid &= Test.TestTrue(
			TEXT("Started action remains current after checkpoint rejection"),
			State.LockedActions.IsValidIndex(State.CurrentLockedActionIndex)
				&& State.LockedActions[State.CurrentLockedActionIndex].bStarted
				&& !State.LockedActions[State.CurrentLockedActionIndex].bFinished);
		bValid &= Test.TestTrue(
			TEXT("Rejection publishes exactly ActionCanceled"),
			Returned.GetEvents().Num() == 1
				&& Returned.GetEvents()[0].GetType() == EBattleEventType::ActionCanceled
				&& Returned.GetEvents()[0].GetActionId().IsValid());
		bValid &= Test.TestFalse(
			TEXT("Rejection publishes no RunAttempted fact"),
			HasEvent(Returned, EBattleEventType::RunAttempted));
		bValid &= Test.TestFalse(
			TEXT("Rejection publishes no ActionCompleted fact"),
			HasEvent(Returned, EBattleEventType::ActionCompleted));

		const FBattleReplayRecord Replay = Engine.ExportReplayRecord();
		bValid &= Test.TestEqual(TEXT("Replay schema remains 6"), Replay.GetSchemaVersion(), 6U);
		bValid &= Test.TestTrue(
			TEXT("Replay contains the same rejected resolution"),
			!Replay.GetResolutions().IsEmpty()
				&& Replay.GetResolutions().Last().GetResolutionId()
					== Returned.GetResolutionId()
				&& Replay.GetResolutions().Last().GetRejection().Reason == ExpectedReason);
		return bValid;
	}
}

#endif
