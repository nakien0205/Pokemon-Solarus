#if WITH_DEV_AUTOMATION_TESTS

#include "Battle/BattleAbility.h"
#include "Battle/BattleBagItem.h"
#include "Battle/BattleCapture.h"
#include "Battle/BattleEngine.h"
#include "Battle/BattleFieldSideConditions.h"
#include "Battle/BattleItem.h"
#include "Battle/BattleMajorStatus.h"
#include "Battle/BattleReplay.h"
#include "Battle/BattleState.h"
#include "Battle/BattleVolatile.h"
#include "Battle/BattleWildFlow.h"
#include "Battle/BattleFaintOutcomeResolver.h"
#include "Battle/BattlePartnerFlow.h"
#include "BattleAtomicCheckpointTestHarness.h"
#include "BattleTestFactories.h"
#include "BattleTestRandom.h"
#include "Math/NumericLimits.h"
#include "Misc/AutomationTest.h"

namespace
{
	using BattleTest::FScriptedBattleRandomBase;
	using BattleTest::FSequenceBattleRandom;
	using BattleTest::FBattleExpectedRandomDraw;
	using BattleTest::FStrictBattleRandom;
	using BattleTest::MakeActiveSlotId;
	using BattleTest::MakeDefinitionId;
	using BattleTest::MakeNumericId;
	using BattleTest::MakePartySlotId;

	constexpr uint64 PlayerTrainerValue = 1;
	constexpr uint64 OpponentTrainerValue = 2;
	constexpr uint64 PartnerTrainerValue = 3;
	constexpr uint64 PlayerLeftValue = 11;
	constexpr uint64 PlayerRightValue = 12;
	constexpr uint64 PlayerReserveValue = 13;
	constexpr uint64 PlayerSecondReserveValue = 14;
	constexpr uint64 OpponentLeftValue = 21;
	constexpr uint64 OpponentRightValue = 22;

	const TCHAR* PlayerSpeciesName = TEXT("Species.ADR0002.3D1.Player");
	const TCHAR* WildSpeciesName = TEXT("Species.ADR0002.3D1.Wild");
	const TCHAR* ProbeMoveName = TEXT("Move.ADR0002.3D1.Probe");
	const TCHAR* TargetProbeMoveName = TEXT("Move.ADR0002.3E1.TargetProbe");
	const TCHAR* PivotProbeMoveName = TEXT("Move.ADR0002.3E3.PivotProbe");
	const TCHAR* ThawProbeMoveName = TEXT("Move.ADR0002.3E4.ThawProbe");
	const TCHAR* ChargeProbeMoveName = TEXT("Move.ADR0002.3E4.ChargeProbe");
	const TCHAR* RandomTargetProbeMoveName = TEXT("Move.ADR0002.3E5.RandomTargetProbe");
	const TCHAR* CaptureHeldItemName = TEXT("Item.ADR0002.3D3.Capture.Held");

	struct FAtomicWildScenario
	{
		EBattleFormat Format = EBattleFormat::Single;
		int32 PlayerLeftSpeed = 50;
		int32 PlayerRightSpeed = 73;
		int32 OpponentLeftSpeed = 100;
		int32 OpponentRightSpeed = 4;
		EBattleWildFleeMode WildFleeMode = EBattleWildFleeMode::Disabled;
		uint32 WildFleeNumerator = 0;
		uint32 WildFleeDenominator = 0;
		bool bCaptureFlow = false;
		int32 CatchRate = 45;
		int32 TargetCurrentHP = 200;
		int32 PlayerCurrentHP = 200;
		int32 PokeBallCount = 3;
		int32 PartyCaptureCapacity = 1;
		int32 StorageCaptureCapacity = 2;
		FBattleCaptureProgressionSnapshot CaptureProgression;
		bool bPlayerHasCanonicalHeldItem = false;
		bool bPlayerSubjectToObedience = false;
		uint8 PlayerReferenceLevel = 20;
		uint8 PlayerBadgeCount = 0;
		bool bVoluntarySwitchFlow = false;
		bool bPivotSwitchFlow = false;
		FAbilityId PlayerAbilityId;
		FItemId PlayerHeldItemId;
		FMoveId PlayerExtraMoveId;
		bool bSecondSwitchReserve = false;
		int32 SwitchIncomingCurrentHP = 200;
		FAbilityId SwitchIncomingAbilityId;
		FItemId SwitchIncomingHeldItemId;
	};

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
		const int32 CatchRate = 45)
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
		Input.Abilities.Add({FBattleAbilityRules::GetBlazeId()});
		Input.Abilities.Add({FBattleAbilityRules::GetIntimidateId()});
		Input.Abilities.Add({FBattleAbilityRules::GetMagicGuardId()});
		Input.Items.Add({FBattleBagItemRules::GetPokeBallId(), EBattleItemKind::Capture});
		Input.Items.Add({FBattleItemRules::GetLeftoversId(), EBattleItemKind::Held});
		Input.Items.Add({FBattleItemRules::GetSitrusBerryId(), EBattleItemKind::Held});
		Input.Items.Add({FBattleItemRules::GetAirBalloonId(), EBattleItemKind::Held});
		Input.Items.Add({FBattleItemRules::GetLumBerryId(), EBattleItemKind::Held});
		Input.Items.Add({FBattleItemRules::GetChoiceBandId(), EBattleItemKind::Held});
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
		const int32 PokeBallCount = -1)
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
		const int32 CurrentHP = 200,
		const FItemId OriginalHeldItemId = FItemId(),
		const FItemId CurrentHeldItemId = FItemId(),
		const FAbilityId AbilityId = FBattleAbilityRules::GetBlazeId(),
		const bool bAddPivotMove = false,
		const FMoveId ExtraMoveId = FMoveId())
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
		Input.EncounterKind = Scenario.Format == EBattleFormat::PartnerDouble
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
		Input.Policies.bRunAllowed = Scenario.Format != EBattleFormat::PartnerDouble;
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
		const FMoveId FightMoveId = FMoveId())
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
		const FMoveId SpecialFightMoveId = FMoveId())
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

	bool LockCaptureThenTargetTurn(FBattleEngine& Engine)
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
			const TArray<FBattleDecisionRequest> Requests =
				Engine.GetPendingDecisionRequests();
			TArray<FBattleDecision> Decisions;
			for (const FBattleDecisionRequest& Request : Requests)
			{
				if (Request.GetActingBattlerId()
					== MakeNumericId<FBattlerId>(PlayerLeftValue))
				{
					Decisions.Add(MakeDecision(Request, EBattleActionKind::Bag));
				}
				else if (Request.GetActingBattlerId()
					== MakeNumericId<FBattlerId>(PlayerRightValue))
				{
					Decisions.Add(MakeDecision(
						Request,
						EBattleActionKind::Fight,
						MakeDefinitionId<FMoveId>(TargetProbeMoveName)));
				}
				else
				{
					Decisions.Add(MakeDecision(Request, EBattleActionKind::Fight));
				}
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

	bool ExecuteRemainingQueue(FBattleEngine& Engine)
	{
		int32 Guard = 0;
		while (Engine.GetSnapshot().GetPhase() == EBattlePhase::Resolving
			&& Guard++ < 12)
		{
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
			if (Engine.GetCurrentLockedAction().IsSet()
				&& !Engine.ResolveCurrentMoveTargets().WasAccepted())
			{
				return false;
			}
			if (Engine.GetCurrentLockedAction().IsSet()
				&& !Engine.ExecuteCurrentMoveEffects().WasAccepted())
			{
				return false;
			}
		}
		return Guard < 12 && Engine.GetSnapshot().GetPhase() == EBattlePhase::EndOfTurn;
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

	struct FCheckpointObservation
	{
		uint64 StateVersion = 0;
		uint64 NextResolutionId = 0;
		uint64 NextEventOrdinal = 0;
		uint64 NextTriggerToken = 0;
		uint32 EscapeAttemptCount = 0;
		int32 LockedActionIndex = INDEX_NONE;
		int32 ResolutionCount = 0;
		int32 EventCount = 0;
		int32 RandomTraceCount = 0;
		int32 PokeBallCount = INDEX_NONE;
		int32 PendingCaptureCount = 0;
		EBattlePhase Phase = EBattlePhase::Setup;
		EBattleOutcome Outcome = EBattleOutcome::InProgress;
		EBattleOutcomeCause OutcomeCause = EBattleOutcomeCause::None;
		bool bActionStarted = false;
		bool bActionFinished = false;
		bool bBagActionAvailable = false;
		bool bObservedBattlerActive = false;
		bool bObservedBattlerCaptured = false;
		bool bObservedBattlerRemoved = false;
	};

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

	enum class EFaultRandomMode : uint8
	{
		PassThrough,
		CreateTransaction,
		Draw,
		StaleAfterDraw,
		Commit
	};

	struct FFaultRandomCounters
	{
		int32 TransactionCreateAttempts = 0;
		int32 DrawAttempts = 0;
		int32 SuccessfulDraws = 0;
		int32 CommitAttempts = 0;
	};

	class FFaultBattleRandomTransaction final : public IBattleRandomTransaction
	{
	public:
		FFaultBattleRandomTransaction(
			TUniquePtr<IBattleRandomTransaction>&& InInner,
			const EFaultRandomMode InMode,
			const int32 InSuccessfulDrawsBeforeFailure,
			TFunction<void()>* InAfterDraw,
			FFaultRandomCounters* InCounters)
			: Inner(MoveTemp(InInner))
			, Mode(InMode)
			, SuccessfulDrawsBeforeFailure(InSuccessfulDrawsBeforeFailure)
			, AfterDraw(InAfterDraw)
			, Counters(InCounters)
		{
		}

		virtual bool TryDrawUniform(
			const uint32 InclusiveMinimum,
			const uint32 InclusiveMaximum,
			const FBattleRandomContext& Context,
			FBattleRandomDraw& OutDraw) override
		{
			OutDraw = FBattleRandomDraw();
			if (Counters != nullptr)
			{
				++Counters->DrawAttempts;
			}
			if (bFinalized
				|| (Mode == EFaultRandomMode::Draw
					&& SuccessfulDrawCount >= SuccessfulDrawsBeforeFailure))
			{
				return false;
			}
			if (!Inner->TryDrawUniform(InclusiveMinimum, InclusiveMaximum, Context, OutDraw))
			{
				return false;
			}
			++SuccessfulDrawCount;
			if (Counters != nullptr)
			{
				++Counters->SuccessfulDraws;
			}
			if (Mode == EFaultRandomMode::StaleAfterDraw
				&& !bAfterDrawCalled
				&& AfterDraw != nullptr
				&& static_cast<bool>(*AfterDraw))
			{
				bAfterDrawCalled = true;
				(*AfterDraw)();
			}
			return true;
		}

		virtual TConstArrayView<FBattleRandomDraw> GetTrace() const override
		{
			return Inner->GetTrace();
		}

		virtual bool TryCommit(
			IBattleRandom& Parent,
			const FResolutionId ResolutionId,
			const FActionId OwningActionId,
			EBattleRandomTransactionCommitError& OutError) override
		{
			OutError = EBattleRandomTransactionCommitError::None;
			if (Counters != nullptr)
			{
				++Counters->CommitAttempts;
			}
			if (bFinalized)
			{
				OutError = EBattleRandomTransactionCommitError::AlreadyFinalized;
				return false;
			}
			bFinalized = true;
			if (Mode == EFaultRandomMode::Commit)
			{
				Inner->Rollback();
				OutError = EBattleRandomTransactionCommitError::ParentPositionMismatch;
				return false;
			}
			return Inner->TryCommit(Parent, ResolutionId, OwningActionId, OutError);
		}

		virtual void Rollback() override
		{
			bFinalized = true;
			Inner->Rollback();
		}

	private:
		TUniquePtr<IBattleRandomTransaction> Inner;
		EFaultRandomMode Mode;
		int32 SuccessfulDrawsBeforeFailure = 0;
		TFunction<void()>* AfterDraw = nullptr;
		FFaultRandomCounters* Counters = nullptr;
		int32 SuccessfulDrawCount = 0;
		bool bAfterDrawCalled = false;
		bool bFinalized = false;
	};

	class FFaultBattleRandom final : public FScriptedBattleRandomBase
	{
	public:
		FFaultBattleRandom(
			TArray<uint32> Results,
			const EFaultRandomMode InMode,
			const int32 InSuccessfulDrawsBeforeFailure = 0)
			: FScriptedBattleRandomBase(MoveTemp(Results))
			, Mode(InMode)
			, SuccessfulDrawsBeforeFailure(InSuccessfulDrawsBeforeFailure)
		{
		}

		FFaultBattleRandom(
			TArray<FBattleExpectedRandomDraw> ExpectedDraws,
			const EFaultRandomMode InMode,
			const int32 InSuccessfulDrawsBeforeFailure = 0)
			: FScriptedBattleRandomBase(MoveTemp(ExpectedDraws))
			, Mode(InMode)
			, SuccessfulDrawsBeforeFailure(InSuccessfulDrawsBeforeFailure)
		{
		}

		void SetAfterDraw(TFunction<void()>&& InAfterDraw)
		{
			AfterDraw = MoveTemp(InAfterDraw);
		}

		const FFaultRandomCounters& GetCounters() const
		{
			return Counters;
		}

		virtual bool TryCreateTransaction(
			const FResolutionId ResolutionId,
			const FActionId OwningActionId,
			TUniquePtr<IBattleRandomTransaction>& OutTransaction) override
		{
			OutTransaction.Reset();
			++Counters.TransactionCreateAttempts;
			if (Mode == EFaultRandomMode::CreateTransaction)
			{
				return false;
			}

			TUniquePtr<IBattleRandomTransaction> Inner;
			if (!FScriptedBattleRandomBase::TryCreateTransaction(
					ResolutionId,
					OwningActionId,
					Inner))
			{
				return false;
			}
			OutTransaction = MakeUnique<FFaultBattleRandomTransaction>(
				MoveTemp(Inner),
				Mode,
				SuccessfulDrawsBeforeFailure,
				&AfterDraw,
				&Counters);
			return true;
		}

	private:
		EFaultRandomMode Mode;
		int32 SuccessfulDrawsBeforeFailure = 0;
		TFunction<void()> AfterDraw;
		FFaultRandomCounters Counters;
	};

	bool TryMakeFaultEngine(
		const FAtomicWildScenario& Scenario,
		TArray<uint32> Results,
		const EFaultRandomMode Mode,
		TUniquePtr<FBattleEngine>& OutEngine,
		FFaultBattleRandom*& OutRandom,
		const int32 SuccessfulDrawsBeforeFailure = 0)
	{
		TUniquePtr<FFaultBattleRandom> Fault =
			MakeUnique<FFaultBattleRandom>(
				MoveTemp(Results),
				Mode,
				SuccessfulDrawsBeforeFailure);
		OutRandom = Fault.Get();
		TUniquePtr<IBattleRandom> Random = MoveTemp(Fault);
		return TryMakeEngine(Scenario, MoveTemp(Random), OutEngine);
	}

	bool TryMakeStrictFaultEngine(
		const FAtomicWildScenario& Scenario,
		TArray<FBattleExpectedRandomDraw> ExpectedDraws,
		const EFaultRandomMode Mode,
		TUniquePtr<FBattleEngine>& OutEngine,
		FFaultBattleRandom*& OutRandom,
		const int32 SuccessfulDrawsBeforeFailure = 0)
	{
		TUniquePtr<FFaultBattleRandom> Fault =
			MakeUnique<FFaultBattleRandom>(
				MoveTemp(ExpectedDraws),
				Mode,
				SuccessfulDrawsBeforeFailure);
		OutRandom = Fault.Get();
		TUniquePtr<IBattleRandom> Random = MoveTemp(Fault);
		return TryMakeEngine(Scenario, MoveTemp(Random), OutEngine);
	}

	bool TrySeedActionStartVolatile(
		FBattleEngine& Engine,
		const FBattlerId BattlerId,
		const FConditionId VolatileId,
		const FDefinitionId PayloadId = FDefinitionId(),
		const TOptional<int32> RemainingTurns = TOptional<int32>())
	{
		FBattleEngineState& State =
			FBattleC09BWildFlowEngineFixture::GetMutableState(Engine);
		FBattleBattlerState* Battler = State.FindMutableBattler(BattlerId);
		if (Battler == nullptr
			|| Battler->Volatiles.ContainsByPredicate(
				[VolatileId](const FBattleConditionState& Condition)
				{
					return Condition.ConditionId == VolatileId;
				}))
		{
			return false;
		}

		FBattleTriggerSubject Owner;
		if (!FBattleTriggerSubject::TryCreateBattler(BattlerId, Owner))
		{
			return false;
		}
		FBattleVolatileTriggerRegistrationFacts Facts;
		Facts.VolatileId = VolatileId;
		Facts.PayloadId = PayloadId.IsValid()
			? PayloadId
			: VolatileId.GetDefinitionId();
		Facts.Owner = Owner;
		Facts.Source = Owner;
		Facts.Targets.Add(Owner);
		Facts.Layers = 1;
		Facts.RemainingTurns = RemainingTurns;
		EBattleTriggerError TriggerError = EBattleTriggerError::None;
		if (!FBattleVolatileRules::TryRegisterTriggers(
				State.TriggerFramework,
				Facts,
				TriggerError))
		{
			return false;
		}

		FBattleConditionState Condition;
		Condition.ConditionId = VolatileId;
		Condition.LayerCount = 1;
		Condition.RemainingTurns = RemainingTurns;
		Condition.CreationOrdinal = State.NextConditionCreationOrdinal++;
		Condition.SourceBattlerId = BattlerId;
		Battler->Volatiles.Add(MoveTemp(Condition));
		TArray<FBattleTriggerLifecycleFact> IgnoredFacts;
		State.TriggerFramework.DrainLifecycleFacts(IgnoredFacts);
		return true;
	}

	bool TrySeedPreMoveMajorStatus(
		FBattleEngine& Engine,
		const FBattlerId BattlerId,
		const FConditionId StatusId,
		const TOptional<int32> SleepTurns = TOptional<int32>())
	{
		FBattleEngineState& State =
			FBattleC09BWildFlowEngineFixture::GetMutableState(Engine);
		FBattleBattlerState* Battler = State.FindMutableBattler(BattlerId);
		FBattleTriggerSubject Owner;
		EBattleTriggerError TriggerError = EBattleTriggerError::None;
		if (Battler == nullptr
			|| Battler->MajorStatusId.IsValid()
			|| !FBattleTriggerSubject::TryCreateBattler(BattlerId, Owner)
			|| !FBattleMajorStatusRules::TryRegisterTriggers(
				State.TriggerFramework,
				StatusId,
				Owner,
				SleepTurns,
				TriggerError))
		{
			return false;
		}
		Battler->MajorStatusId = StatusId;
		TArray<FBattleTriggerLifecycleFact> IgnoredFacts;
		State.TriggerFramework.DrainLifecycleFacts(IgnoredFacts);
		return true;
	}

	bool TrySeedActionStartMagicRoom(
		FBattleEngine& Engine,
		const FBattlerId SourceBattlerId)
	{
		FBattleEngineState& State =
			FBattleC09BWildFlowEngineFixture::GetMutableState(Engine);
		const FConditionId MagicRoomId =
			FBattleFieldSideConditionRules::GetMagicRoomId();
		if (State.Field.Rooms.ContainsByPredicate(
			[MagicRoomId](const FBattleConditionState& Condition)
			{
				return Condition.ConditionId == MagicRoomId;
			}))
		{
			return false;
		}

		FBattleTriggerSubject Source;
		if (!FBattleTriggerSubject::TryCreateBattler(SourceBattlerId, Source))
		{
			return false;
		}
		FBattleFieldSideTriggerRegistrationFacts Facts;
		Facts.ConditionId = MagicRoomId;
		Facts.PayloadId = MagicRoomId.GetDefinitionId();
		Facts.Owner = FBattleTriggerSubject::CreateField();
		Facts.Source = Source;
		Facts.Targets.Add(Facts.Owner);
		Facts.RemainingTurns = 5;
		Facts.Layers = 1;
		EBattleTriggerError TriggerError = EBattleTriggerError::None;
		if (!FBattleFieldSideConditionRules::TryRegisterTriggers(
				State.TriggerFramework,
				Facts,
				TriggerError))
		{
			return false;
		}

		FBattleConditionState MagicRoom;
		MagicRoom.ConditionId = MagicRoomId;
		MagicRoom.RemainingTurns = Facts.RemainingTurns;
		MagicRoom.LayerCount = Facts.Layers;
		MagicRoom.CreationOrdinal = State.NextConditionCreationOrdinal++;
		MagicRoom.SourceBattlerId = SourceBattlerId;
		State.Field.Rooms.Add(MoveTemp(MagicRoom));
		TArray<FBattleTriggerLifecycleFact> IgnoredFacts;
		State.TriggerFramework.DrainLifecycleFacts(IgnoredFacts);
		return true;
	}

	bool HasActionStartVolatile(
		const FBattleEngineState& State,
		const FBattlerId BattlerId,
		const FConditionId VolatileId)
	{
		const FBattleBattlerState* Battler = State.FindBattler(BattlerId);
		return Battler != nullptr
			&& Battler->Volatiles.ContainsByPredicate(
				[VolatileId](const FBattleConditionState& Condition)
				{
					return Condition.ConditionId == VolatileId;
				});
	}

	int32 CountActionStartTriggerRegistrations(
		const FBattleEngineState& State,
		const FDefinitionId DefinitionId)
	{
		int32 Count = 0;
		for (const FBattleTriggerRegistrationState& Registration :
			State.TriggerFramework.GetActiveRegistrations())
		{
			const FBattleTriggerSourceDefinition& Source =
				Registration.Spec.SourceDefinition;
			bool bMatches = false;
			switch (Source.Kind)
			{
			case EBattleTriggerSourceDefinitionKind::Condition:
				bMatches = Source.ConditionId.GetDefinitionId() == DefinitionId;
				break;
			case EBattleTriggerSourceDefinitionKind::Ability:
				bMatches = Source.AbilityId.GetDefinitionId() == DefinitionId;
				break;
			case EBattleTriggerSourceDefinitionKind::Item:
				bMatches = Source.ItemId.GetDefinitionId() == DefinitionId;
				break;
			default:
				break;
			}
			Count += bMatches ? 1 : 0;
		}
		return Count;
	}

	bool TryPrepareLastLockedAction(
		FBattleEngine& Engine,
		const FBattlerId ExpectedActorId)
	{
		FBattleEngineState& State =
			FBattleC09BWildFlowEngineFixture::GetMutableState(Engine);
		if (State.LockedActions.Num() < 2)
		{
			return false;
		}
		const int32 LastIndex = State.LockedActions.Num() - 1;
		if (State.LockedActions[LastIndex].Decision.GetActingBattlerId()
			!= ExpectedActorId)
		{
			return false;
		}
		for (int32 Index = 0; Index < LastIndex; ++Index)
		{
			State.LockedActions[Index].bStarted = true;
			State.LockedActions[Index].bFinished = true;
		}
		State.CurrentLockedActionIndex = LastIndex;
		State.Phase = EBattlePhase::Resolving;
		EBattleStateValidationError StateError = EBattleStateValidationError::None;
		return State.ValidateInvariants(StateError);
	}

	bool TryClearActionStartActiveSlot(
		FBattleEngine& Engine,
		const FActiveSlotId ActiveSlotId)
	{
		FBattleEngineState& State =
			FBattleC09BWildFlowEngineFixture::GetMutableState(Engine);
		FBattleActivePositionState* Active = State.FindMutableActivePosition(ActiveSlotId);
		if (Active == nullptr)
		{
			return false;
		}
		Active->TrainerId = FTrainerId();
		Active->BattlerId = FBattlerId();
		return true;
	}

	class FActionStartStaleRandom final : public FScriptedBattleRandomBase
	{
	public:
		explicit FActionStartStaleRandom(TArray<uint32> Results)
			: FScriptedBattleRandomBase(MoveTemp(Results))
		{
		}

		void ArmAfterTraceRead(
			const int32 TraceReadOrdinal,
			TFunction<void()>&& InCallback)
		{
			ReadsSinceArm = 0;
			InjectionReadOrdinal = TraceReadOrdinal;
			Callback = MoveTemp(InCallback);
			bInjected = false;
		}

		void Disarm()
		{
			InjectionReadOrdinal = INDEX_NONE;
			Callback = TFunction<void()>();
		}

		int32 GetReadsSinceArm() const { return ReadsSinceArm; }
		bool WasInjected() const { return bInjected; }

		virtual TConstArrayView<FBattleRandomDraw> GetTrace() const override
		{
			const TConstArrayView<FBattleRandomDraw> Trace =
				FScriptedBattleRandomBase::GetTrace();
			if (InjectionReadOrdinal != INDEX_NONE)
			{
				++ReadsSinceArm;
				if (!bInjected
					&& ReadsSinceArm == InjectionReadOrdinal
					&& static_cast<bool>(Callback))
				{
					bInjected = true;
					Callback();
				}
			}
			return Trace;
		}

	private:
		mutable int32 ReadsSinceArm = 0;
		mutable int32 InjectionReadOrdinal = INDEX_NONE;
		mutable TFunction<void()> Callback;
		mutable bool bInjected = false;
	};

	bool TryMakeActionStartStaleEngine(
		const FAtomicWildScenario& Scenario,
		TUniquePtr<FBattleEngine>& OutEngine,
		FActionStartStaleRandom*& OutRandom)
	{
		TUniquePtr<FActionStartStaleRandom> Random =
			MakeUnique<FActionStartStaleRandom>(TArray<uint32>());
		OutRandom = Random.Get();
		TUniquePtr<IBattleRandom> Base = MoveTemp(Random);
		return TryMakeEngine(Scenario, MoveTemp(Base), OutEngine);
	}

	struct FActionStartCheckpointObservation
	{
		uint64 StateVersion = 0;
		uint64 NextResolutionId = 0;
		uint64 NextEventOrdinal = 0;
		uint64 NextTriggerToken = 0;
		uint64 NextConditionCreationOrdinal = 0;
		int32 ActionIndex = INDEX_NONE;
		int32 ResolutionCount = 0;
		int32 EventCount = 0;
		int32 RandomTraceCount = 0;
		int32 TotalMovePP = 0;
		int32 MaximumActions = INDEX_NONE;
		int32 RemainingActions = INDEX_NONE;
		bool bBagActionAvailable = false;
		EBattlePhase Phase = EBattlePhase::Setup;
		EBattleOutcome Outcome = EBattleOutcome::InProgress;
		EBattleOutcomeCause OutcomeCause = EBattleOutcomeCause::None;
		bool bActionStarted = false;
		bool bMoveCommitted = false;
		bool bTargetResolutionSet = false;
		EBattleLockedEffectExecutionState EffectExecutionState =
			EBattleLockedEffectExecutionState::Pending;
		bool bActionFinished = false;
		bool bPendingDecisionSet = false;
		int32 PendingDecisionRequestCount = 0;
		int32 PendingReplacementCount = 0;
		int32 RoomCount = 0;
		bool bActorActive = false;
		bool bHasHeldItem = false;
		FBattleHeldItemState HeldItem;
		TArray<FBattleHeldItemInstanceState> LedgerStates;
		TArray<FConditionId> ActorVolatileIds;
		TArray<FBattleTriggerRegistrationId> TriggerRegistrationIds;
		TArray<uint64> TriggerCreationOrdinals;
		TArray<FBattleTriggerSourceDefinition> TriggerSources;
		TArray<uint8> TriggerSuppression;
		int32 PendingTriggerDispatchCount = 0;
		int32 PendingTriggerEffectCount = 0;
		int32 PendingTriggerLifecycleCount = 0;
	};

	FActionStartCheckpointObservation ObserveActionStartCheckpoint(
		const FBattleEngine& Engine,
		const FBattlerId ActorId,
		const FTrainerId TrainerId)
	{
		const FBattleEngineState& State =
			FBattleC09BWildFlowEngineFixture::GetState(Engine);
		FActionStartCheckpointObservation Observation;
		Observation.StateVersion = State.StateVersion;
		Observation.NextResolutionId = State.NextResolutionId;
		Observation.NextEventOrdinal = State.NextEventOrdinal;
		Observation.NextTriggerToken = State.NextTriggerReentrancyToken;
		Observation.NextConditionCreationOrdinal = State.NextConditionCreationOrdinal;
		Observation.ActionIndex = State.CurrentLockedActionIndex;
		Observation.ResolutionCount = State.Resolutions.Num();
		Observation.EventCount = State.OrderedEvents.Num();
		Observation.RandomTraceCount = State.Random->GetTrace().Num();
		Observation.Phase = State.Phase;
		Observation.Outcome = State.Outcome;
		Observation.OutcomeCause = State.OutcomeCause;
		Observation.bPendingDecisionSet = State.PendingDecision.IsSet();
		Observation.PendingDecisionRequestCount = State.PendingDecisionRequests.Num();
		Observation.PendingReplacementCount = State.PendingReplacements.Num();
		Observation.RoomCount = State.Field.Rooms.Num();
		if (State.LockedActions.IsValidIndex(Observation.ActionIndex))
		{
			const FBattleLockedActionState& Action =
				State.LockedActions[Observation.ActionIndex];
			Observation.bActionStarted = Action.bStarted;
			Observation.bMoveCommitted = Action.bMoveCommitted;
			Observation.bTargetResolutionSet = Action.TargetResolution.IsSet();
			Observation.EffectExecutionState = Action.EffectExecutionState;
			Observation.bActionFinished = Action.bFinished;
		}
		const FBattleTrainerState* Trainer = State.FindTrainer(TrainerId);
		if (Trainer != nullptr)
		{
			Observation.MaximumActions = Trainer->ActionAllowance.MaximumActions;
			Observation.RemainingActions = Trainer->ActionAllowance.RemainingActions;
			Observation.bBagActionAvailable =
				Trainer->ActionAllowance.bBagActionAvailable;
		}
		const FBattleBattlerState* Actor = State.FindBattler(ActorId);
		if (Actor != nullptr)
		{
			for (const FBattleMoveSlotState& Move : Actor->Moves)
			{
				Observation.TotalMovePP += Move.CurrentPP;
			}
			Observation.bHasHeldItem = Actor->HeldItem.InstanceId.IsValid();
			Observation.HeldItem = Actor->HeldItem;
			for (const FBattleConditionState& Condition : Actor->Volatiles)
			{
				Observation.ActorVolatileIds.Add(Condition.ConditionId);
			}
		}
		Observation.bActorActive = State.ActivePositions.ContainsByPredicate(
			[ActorId](const FBattleActivePositionState& Active)
			{
				return Active.BattlerId == ActorId;
			});
		Observation.LedgerStates.Append(State.HeldItemLedger.GetStates());
		for (const FBattleTriggerRegistrationState& Registration :
			State.TriggerFramework.GetActiveRegistrations())
		{
			Observation.TriggerRegistrationIds.Add(Registration.RegistrationId);
			Observation.TriggerCreationOrdinals.Add(Registration.CreationOrdinal);
			Observation.TriggerSources.Add(Registration.Spec.SourceDefinition);
			Observation.TriggerSuppression.Add(Registration.bSuppressed ? 1 : 0);
		}
		Observation.PendingTriggerDispatchCount =
			State.TriggerFramework.GetPendingDispatchCount();
		Observation.PendingTriggerEffectCount =
			State.TriggerFramework.GetPendingEffectRequestCount();
		Observation.PendingTriggerLifecycleCount =
			State.TriggerFramework.GetPendingLifecycleFactCount();
		return Observation;
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

	bool VerifyRejectedActionStartCheckpoint(
		FAutomationTestBase& Test,
		const FBattleEngine& Engine,
		const FBattlerId ActorId,
		const FTrainerId TrainerId,
		const FActionStartCheckpointObservation& Before,
		const uint64 ExpectedStateVersion,
		const EBattleRejectionReason ExpectedReason,
		const FBattleResolution& Returned)
	{
		const FActionStartCheckpointObservation After =
			ObserveActionStartCheckpoint(Engine, ActorId, TrainerId);
		bool bValid = true;
		bValid &= Test.TestFalse(TEXT("Action-start checkpoint failure is rejected"),
			Returned.WasAccepted());
		bValid &= Test.TestEqual(TEXT("Action-start rejection reason is typed"),
			Returned.GetRejection().Reason, ExpectedReason);
		bValid &= Test.TestTrue(TEXT("Action-start rejection is appended exactly once"),
			IsReturnedResolutionAppendedExactlyOnce(Engine, Returned));
		bValid &= Test.TestTrue(TEXT("Action-start rejection publishes one cancellation only"),
			Returned.GetEvents().Num() == 1
				&& Returned.GetEvents()[0].GetType()
					== EBattleEventType::ActionCanceled
				&& Returned.GetEvents()[0].GetCause() == EBattleEventCause::Rule
				&& Returned.GetEvents()[0].GetEventOrdinal()
					== Before.NextEventOrdinal
				&& Returned.GetEvents()[0].GetVisibility().Level
					== EBattleVisibilityLevel::Public);
		bValid &= Test.TestEqual(TEXT("Rejection appends one resolution"),
			After.ResolutionCount, Before.ResolutionCount + 1);
		bValid &= Test.TestEqual(TEXT("Rejection appends one event"),
			After.EventCount, Before.EventCount + 1);
		bValid &= Test.TestEqual(TEXT("Rejection consumes one resolution identity"),
			After.NextResolutionId, Before.NextResolutionId + 1);
		bValid &= Test.TestEqual(TEXT("Rejection consumes one event ordinal"),
			After.NextEventOrdinal, Before.NextEventOrdinal + 1);
		bValid &= Test.TestEqual(TEXT("Rejection publishes no accepted version delta"),
			After.StateVersion, ExpectedStateVersion);
		bValid &= Test.TestEqual(TEXT("Rejection preserves the action cursor"),
			After.ActionIndex, Before.ActionIndex);
		bValid &= Test.TestEqual(TEXT("Rejection preserves phase"),
			After.Phase, Before.Phase);
		bValid &= Test.TestEqual(TEXT("Rejection preserves outcome"),
			After.Outcome, Before.Outcome);
		bValid &= Test.TestEqual(TEXT("Rejection preserves outcome cause"),
			After.OutcomeCause, Before.OutcomeCause);
		bValid &= Test.TestEqual(TEXT("Rejection preserves Trainer maximum allowance"),
			After.MaximumActions, Before.MaximumActions);
		bValid &= Test.TestEqual(TEXT("Rejection preserves Trainer remaining allowance"),
			After.RemainingActions, Before.RemainingActions);
		bValid &= Test.TestEqual(TEXT("Rejection preserves Trainer Bag allowance"),
			After.bBagActionAvailable, Before.bBagActionAvailable);
		bValid &= Test.TestEqual(TEXT("Rejection preserves bStarted"),
			After.bActionStarted, Before.bActionStarted);
		bValid &= Test.TestEqual(TEXT("Rejection preserves bMoveCommitted"),
			After.bMoveCommitted, Before.bMoveCommitted);
		bValid &= Test.TestEqual(TEXT("Rejection preserves target-resolution state"),
			After.bTargetResolutionSet, Before.bTargetResolutionSet);
		bValid &= Test.TestEqual(TEXT("Rejection preserves effect-execution state"),
			After.EffectExecutionState, Before.EffectExecutionState);
		bValid &= Test.TestEqual(TEXT("Rejection preserves bFinished"),
			After.bActionFinished, Before.bActionFinished);
		bValid &= Test.TestEqual(TEXT("Rejection preserves move PP"),
			After.TotalMovePP, Before.TotalMovePP);
		bValid &= Test.TestEqual(TEXT("Rejection consumes no gameplay RNG"),
			After.RandomTraceCount, Before.RandomTraceCount);
		bValid &= Test.TestEqual(TEXT("Rejection preserves trigger token"),
			After.NextTriggerToken, Before.NextTriggerToken);
		bValid &= Test.TestEqual(TEXT("Rejection preserves condition ordinal"),
			After.NextConditionCreationOrdinal, Before.NextConditionCreationOrdinal);
		bValid &= Test.TestEqual(TEXT("Rejection preserves room state"),
			After.RoomCount, Before.RoomCount);
		bValid &= Test.TestEqual(TEXT("Rejection preserves actor active state"),
			After.bActorActive, Before.bActorActive);
		bValid &= Test.TestEqual(TEXT("Rejection preserves pending-decision presence"),
			After.bPendingDecisionSet, Before.bPendingDecisionSet);
		bValid &= Test.TestEqual(TEXT("Rejection preserves pending decision requests"),
			After.PendingDecisionRequestCount, Before.PendingDecisionRequestCount);
		bValid &= Test.TestEqual(TEXT("Rejection preserves pending replacements"),
			After.PendingReplacementCount, Before.PendingReplacementCount);
		bValid &= Test.TestEqual(TEXT("Rejection preserves held-item presence"),
			After.bHasHeldItem, Before.bHasHeldItem);
		bValid &= Test.TestTrue(TEXT("Rejection preserves held-item battler facts"),
			AreActionStartHeldItemsIdentical(After.HeldItem, Before.HeldItem));
		bValid &= Test.TestTrue(TEXT("Rejection preserves the held-item ledger"),
			After.LedgerStates == Before.LedgerStates);
		bValid &= Test.TestTrue(TEXT("Rejection preserves actor volatiles"),
			After.ActorVolatileIds == Before.ActorVolatileIds);
		bValid &= Test.TestTrue(TEXT("Rejection preserves trigger registration ids"),
			After.TriggerRegistrationIds == Before.TriggerRegistrationIds);
		bValid &= Test.TestTrue(TEXT("Rejection preserves trigger creation order"),
			After.TriggerCreationOrdinals == Before.TriggerCreationOrdinals);
		bValid &= Test.TestTrue(TEXT("Rejection preserves trigger sources"),
			After.TriggerSources == Before.TriggerSources);
		bValid &= Test.TestTrue(TEXT("Rejection preserves trigger suppression"),
			After.TriggerSuppression == Before.TriggerSuppression);
		bValid &= Test.TestEqual(TEXT("Rejection preserves pending trigger dispatches"),
			After.PendingTriggerDispatchCount, Before.PendingTriggerDispatchCount);
		bValid &= Test.TestEqual(TEXT("Rejection preserves pending trigger effects"),
			After.PendingTriggerEffectCount, Before.PendingTriggerEffectCount);
		bValid &= Test.TestEqual(TEXT("Rejection preserves pending trigger lifecycle facts"),
			After.PendingTriggerLifecycleCount, Before.PendingTriggerLifecycleCount);
		const FBattleReplayRecord Replay = Engine.ExportReplayRecord();
		bValid &= Test.TestEqual(TEXT("Rejection replay keeps the canonical schema"),
			Replay.GetSchemaVersion(), FBattleReplayRecord::CurrentSchemaVersion);
		bValid &= Test.TestTrue(TEXT("Rejection replay contains the same resolution"),
			!Replay.GetResolutions().IsEmpty()
				&& Replay.GetResolutions().Last().GetResolutionId()
					== Returned.GetResolutionId()
				&& Replay.GetResolutions().Last().GetRejection().Reason
					== ExpectedReason);
		return bValid;
	}

	FAtomicWildScenario MakeAtomicVoluntarySwitchScenario(
		const FItemId IncomingItemId = FItemId(),
		const FAbilityId IncomingAbilityId = FBattleAbilityRules::GetBlazeId(),
		const int32 IncomingCurrentHP = 200)
	{
		FAtomicWildScenario Scenario;
		Scenario.bVoluntarySwitchFlow = true;
		Scenario.SwitchIncomingHeldItemId = IncomingItemId;
		Scenario.SwitchIncomingAbilityId = IncomingAbilityId;
		Scenario.SwitchIncomingCurrentHP = IncomingCurrentHP;
		return Scenario;
	}

	bool TryPrepareAtomicVoluntarySwitch(FBattleEngine& Engine)
	{
		FBattleEngineState& State =
			FBattleC09BWildFlowEngineFixture::GetMutableState(Engine);
		const FBattlerId OutgoingBattlerId =
			MakeNumericId<FBattlerId>(PlayerLeftValue);
		const FBattleBattlerState* Outgoing = State.FindBattler(OutgoingBattlerId);
		const FBattleActivePositionState* Active = State.ActivePositions.FindByPredicate(
			[OutgoingBattlerId](const FBattleActivePositionState& Candidate)
			{
				return Candidate.BattlerId == OutgoingBattlerId;
			});
		FBattleTrainerState* Trainer = Outgoing != nullptr
			? State.FindMutableTrainer(Outgoing->TrainerId)
			: nullptr;
		if (Outgoing == nullptr || Active == nullptr || Trainer == nullptr)
		{
			return false;
		}

		FBattleDecision Decision;
		if (!FBattleDecision::TryCreateSwitch(
				State.StateVersion,
				EBattleDecisionRequestKind::Action,
				Outgoing->TrainerId,
				OutgoingBattlerId,
				MakePartySlotId(1),
				Active->ActiveSlotId,
				Decision))
		{
			return false;
		}
		FBattleLockedActionState Action;
		Action.ActionId = MakeNumericId<FActionId>(3003201);
		Action.QueueOrdinal = 1;
		Action.Decision = MoveTemp(Decision);
		Action.OrderKey.CommandBand = EBattleActionCommandBand::VoluntarySwitch;
		Action.OrderKey.EffectiveSpeed = Outgoing->PermanentStats.Speed;
		Action.OrderKey.ActingSlotId = Active->ActiveSlotId;
		State.LockedActions = {MoveTemp(Action)};
		State.CurrentLockedActionIndex = 0;
		State.PendingDecision.Reset();
		State.PendingDecisionRequests.Reset();
		State.PendingReplacements.Reset();
		State.DecisionOwnerSequence.Reset();
		State.AcceptedSelections.Reset();
		State.Phase = EBattlePhase::Locked;
		Trainer->ActionAllowance.MaximumActions = 1;
		Trainer->ActionAllowance.RemainingActions = 1;
		EBattleStateValidationError Validation = EBattleStateValidationError::None;
		return State.ValidateInvariants(Validation);
	}

	bool TryBeginAtomicVoluntarySwitch(FBattleEngine& Engine)
	{
		if (!Engine.BeginNextLockedAction().WasAccepted())
		{
			return false;
		}
		const TOptional<FBattleLockedAction> Current = Engine.GetCurrentLockedAction();
		return Current.IsSet()
			&& Current->Decision.GetActionKind() == EBattleActionKind::Switch
			&& Current->Decision.GetActingBattlerId()
				== MakeNumericId<FBattlerId>(PlayerLeftValue)
			&& Current->Decision.GetSwitchPartySlotId() == MakePartySlotId(1);
	}

	FAtomicWildScenario MakeAtomicPivotSwitchScenario(
		const FItemId IncomingItemId = FItemId(),
		const FAbilityId IncomingAbilityId = FBattleAbilityRules::GetBlazeId(),
		const int32 IncomingCurrentHP = 200,
		const bool bSecondReserve = false)
	{
		FAtomicWildScenario Scenario = MakeAtomicVoluntarySwitchScenario(
			IncomingItemId,
			IncomingAbilityId,
			IncomingCurrentHP);
		Scenario.bPivotSwitchFlow = true;
		Scenario.bSecondSwitchReserve = bSecondReserve;
		Scenario.PlayerLeftSpeed = 150;
		Scenario.OpponentLeftSpeed = 50;
		return Scenario;
	}

	bool TryMakePivotSwitchDecision(
		const FBattleDecisionRequest& Request,
		const FPartySlotId PartySlotId,
		FBattleDecision& OutDecision)
	{
		return FBattleDecision::TryCreateSwitch(
			Request.GetStateVersion(),
			EBattleDecisionRequestKind::PivotSwitch,
			Request.GetDecisionOwnerTrainerId(),
			Request.GetActingBattlerId(),
			PartySlotId,
			Request.GetActingSlotId(),
			OutDecision);
	}

	bool TryPrepareAtomicPivotSwitch(
		FBattleEngine& Engine,
		FBattleDecisionRequest& OutRequest)
	{
		if (!LockTurn(
				Engine,
				PlayerLeftValue,
				EBattleActionKind::Fight,
				MakeDefinitionId<FMoveId>(PivotProbeMoveName)))
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
				&& Current->Decision.GetMoveId()
					== MakeDefinitionId<FMoveId>(PivotProbeMoveName))
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
		const TOptional<FBattleLockedAction> PivotAction = Engine.GetCurrentLockedAction();
		if (Guard > 4
			|| !PivotAction.IsSet()
			|| PivotAction->Decision.GetMoveId()
				!= MakeDefinitionId<FMoveId>(PivotProbeMoveName)
			|| !Engine.CommitCurrentMoveAfterPreMoveGates().WasAccepted()
			|| !Engine.ResolveCurrentMoveTargets().WasAccepted())
		{
			return false;
		}
		const FBattleResolution Effects = Engine.ExecuteCurrentMoveEffects();
		const FBattleEngineState& State =
			FBattleC09BWildFlowEngineFixture::GetState(Engine);
		if (!Effects.WasAccepted()
			|| Effects.GetEvents().ContainsByPredicate(
				[](const FBattleEvent& Event)
				{
					return Event.GetType() == EBattleEventType::ActionCompleted;
				})
			|| State.Phase != EBattlePhase::Resolving
			|| !State.LockedActions.IsValidIndex(State.CurrentLockedActionIndex)
			|| State.PendingDecisionRequests.Num() != 1
			|| !State.PendingDecision.IsSet())
		{
			return false;
		}
		const FBattleLockedActionState& Action =
			State.LockedActions[State.CurrentLockedActionIndex];
		const FBattleDecisionRequest& Request = State.PendingDecisionRequests[0];
		if (Action.Decision.GetActionKind() != EBattleActionKind::Fight
			|| Action.Decision.GetMoveId() != MakeDefinitionId<FMoveId>(PivotProbeMoveName)
			|| !Action.bStarted
			|| !Action.bMoveCommitted
			|| !Action.TargetResolution.IsSet()
			|| Action.EffectExecutionState
				!= EBattleLockedEffectExecutionState::AwaitingPivot
			|| Action.bFinished
			|| Request.GetRequestKind() != EBattleDecisionRequestKind::PivotSwitch
			|| Request.GetActingBattlerId()
				!= MakeNumericId<FBattlerId>(PlayerLeftValue)
			|| Request.GetActingSlotId()
				!= MakeActiveSlotId(EBattleSide::Player, EBattlePosition::Left))
		{
			return false;
		}
		OutRequest = Request;
		return true;
	}

	bool TrySeedAtomicSwitchHazard(
		FBattleEngine& Engine,
		const FConditionId HazardId,
		const int32 Layers = 1)
	{
		FBattleEngineState& State =
			FBattleC09BWildFlowEngineFixture::GetMutableState(Engine);
		FBattleTriggerSubject Owner;
		FBattleTriggerSubject Source;
		const FBattlerId SourceBattlerId =
			MakeNumericId<FBattlerId>(OpponentLeftValue);
		if (Layers <= 0
			|| FBattleFieldSideConditionRules::GetConditionFamily(HazardId)
				!= EBattleConditionKind::Hazard
			|| !FBattleTriggerSubject::TryCreateSide(EBattleSide::Player, Owner)
			|| !FBattleTriggerSubject::TryCreateBattler(SourceBattlerId, Source))
		{
			return false;
		}
		FBattleSideState* Side = State.Sides.FindByPredicate(
			[](const FBattleSideState& Candidate)
			{
				return Candidate.Side == EBattleSide::Player;
			});
		if (Side == nullptr
			|| Side->Hazards.ContainsByPredicate(
				[HazardId](const FBattleConditionState& Candidate)
				{
					return Candidate.ConditionId == HazardId;
				}))
		{
			return false;
		}

		FBattleFieldSideTriggerRegistrationFacts Facts;
		Facts.ConditionId = HazardId;
		Facts.PayloadId = HazardId.GetDefinitionId();
		Facts.Owner = Owner;
		Facts.Source = Source;
		Facts.Targets.Add(Owner);
		Facts.Layers = Layers;
		EBattleTriggerError TriggerError = EBattleTriggerError::None;
		if (!FBattleFieldSideConditionRules::TryRegisterTriggers(
				State.TriggerFramework,
				Facts,
				TriggerError))
		{
			return false;
		}
		FBattleConditionState Condition;
		Condition.ConditionId = HazardId;
		Condition.LayerCount = Layers;
		Condition.CreationOrdinal = State.NextConditionCreationOrdinal++;
		Condition.SourceBattlerId = SourceBattlerId;
		Side->Hazards.Add(MoveTemp(Condition));
		TArray<FBattleTriggerEffectRequest> IgnoredRequests;
		TArray<FBattleTriggerLifecycleFact> IgnoredLifecycle;
		State.TriggerFramework.DrainEffectRequests(IgnoredRequests);
		State.TriggerFramework.DrainLifecycleFacts(IgnoredLifecycle);
		return true;
	}

	bool TrySeedAtomicSwitchOutgoingTransients(FBattleEngine& Engine)
	{
		const FBattlerId OutgoingId = MakeNumericId<FBattlerId>(PlayerLeftValue);
		FBattleEngineState& State =
			FBattleC09BWildFlowEngineFixture::GetMutableState(Engine);
		FBattleBattlerState* Outgoing = State.FindMutableBattler(OutgoingId);
		if (Outgoing == nullptr)
		{
			return false;
		}
		const FBattleStatStageChangeResult Change =
			Outgoing->Stages.ApplyChange(EBattleStat::Attack, 2);
		Outgoing->LastMoveId = MakeDefinitionId<FMoveId>(ProbeMoveName);
		return Change.Outcome == EBattleStatStageChangeOutcome::Applied;
	}

	struct FAtomicSwitchConditionObservation
	{
		FConditionId ConditionId;
		int32 RemainingTurns = 0;
		int32 LayerCount = 0;
		uint64 CreationOrdinal = 0;
		FBattlerId SourceBattlerId;
		bool bHasRemainingTurns = false;
	};

	FAtomicSwitchConditionObservation ObserveAtomicSwitchCondition(
		const FBattleConditionState& Condition)
	{
		FAtomicSwitchConditionObservation Observation;
		Observation.ConditionId = Condition.ConditionId;
		Observation.bHasRemainingTurns = Condition.RemainingTurns.IsSet();
		Observation.RemainingTurns = Condition.RemainingTurns.Get(0);
		Observation.LayerCount = Condition.LayerCount;
		Observation.CreationOrdinal = Condition.CreationOrdinal;
		Observation.SourceBattlerId = Condition.SourceBattlerId;
		return Observation;
	}

	bool AreAtomicSwitchConditionsIdentical(
		const TArray<FAtomicSwitchConditionObservation>& Left,
		const TArray<FAtomicSwitchConditionObservation>& Right)
	{
		if (Left.Num() != Right.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < Left.Num(); ++Index)
		{
			const FAtomicSwitchConditionObservation& L = Left[Index];
			const FAtomicSwitchConditionObservation& R = Right[Index];
			if (L.ConditionId != R.ConditionId
				|| L.bHasRemainingTurns != R.bHasRemainingTurns
				|| L.RemainingTurns != R.RemainingTurns
				|| L.LayerCount != R.LayerCount
				|| L.CreationOrdinal != R.CreationOrdinal
				|| L.SourceBattlerId != R.SourceBattlerId)
			{
				return false;
			}
		}
		return true;
	}

	struct FAtomicSwitchBattlerObservation
	{
		FTrainerId TrainerId;
		FBattlerId BattlerId;
		FPartySlotId PartySlotId;
		int32 CurrentHP = 0;
		TArray<int32> Stages;
		TArray<FAtomicSwitchConditionObservation> Volatiles;
		FConditionId MajorStatusId;
		FAbilityId AbilityId;
		FBattleHeldItemState HeldItem;
		FMoveId LastMoveId;
		FTurnId EnteredActiveOnTurnId;
		bool bFainted = false;
		bool bCaptured = false;
		bool bRemoved = false;
		bool bFaintTransitionPending = false;
		bool bAbilitySuppressed = false;
	};

	FAtomicSwitchBattlerObservation ObserveAtomicSwitchBattler(
		const FBattleEngineState& State,
		const FBattlerId BattlerId)
	{
		FAtomicSwitchBattlerObservation Observation;
		const FBattleBattlerState* Battler = State.FindBattler(BattlerId);
		if (Battler == nullptr)
		{
			return Observation;
		}
		Observation.TrainerId = Battler->TrainerId;
		Observation.BattlerId = Battler->BattlerId;
		Observation.PartySlotId = Battler->PartySlotId;
		Observation.CurrentHP = Battler->CurrentHP;
		for (uint8 StatValue = static_cast<uint8>(EBattleStat::Attack);
			StatValue <= static_cast<uint8>(EBattleStat::Evasion);
			++StatValue)
		{
			int32 Stage = 0;
			const bool bRead = Battler->Stages.TryGetStage(
				static_cast<EBattleStat>(StatValue),
				Stage);
			check(bRead);
			Observation.Stages.Add(Stage);
		}
		for (const FBattleConditionState& Condition : Battler->Volatiles)
		{
			Observation.Volatiles.Add(ObserveAtomicSwitchCondition(Condition));
		}
		Observation.MajorStatusId = Battler->MajorStatusId;
		Observation.AbilityId = Battler->AbilityId;
		Observation.HeldItem = Battler->HeldItem;
		Observation.LastMoveId = Battler->LastMoveId;
		Observation.EnteredActiveOnTurnId = Battler->EnteredActiveOnTurnId;
		Observation.bFainted = Battler->bFainted;
		Observation.bCaptured = Battler->bCaptured;
		Observation.bRemoved = Battler->bRemoved;
		Observation.bFaintTransitionPending = Battler->bFaintTransitionPending;
		Observation.bAbilitySuppressed = Battler->bAbilitySuppressed;
		return Observation;
	}

	bool AreAtomicSwitchBattlersIdentical(
		const FAtomicSwitchBattlerObservation& Left,
		const FAtomicSwitchBattlerObservation& Right)
	{
		return Left.TrainerId == Right.TrainerId
			&& Left.BattlerId == Right.BattlerId
			&& Left.PartySlotId == Right.PartySlotId
			&& Left.CurrentHP == Right.CurrentHP
			&& Left.Stages == Right.Stages
			&& AreAtomicSwitchConditionsIdentical(Left.Volatiles, Right.Volatiles)
			&& Left.MajorStatusId == Right.MajorStatusId
			&& Left.AbilityId == Right.AbilityId
			&& AreActionStartHeldItemsIdentical(Left.HeldItem, Right.HeldItem)
			&& Left.LastMoveId == Right.LastMoveId
			&& Left.EnteredActiveOnTurnId == Right.EnteredActiveOnTurnId
			&& Left.bFainted == Right.bFainted
			&& Left.bCaptured == Right.bCaptured
			&& Left.bRemoved == Right.bRemoved
			&& Left.bFaintTransitionPending == Right.bFaintTransitionPending
			&& Left.bAbilitySuppressed == Right.bAbilitySuppressed;
	}

	bool IsAtomicSwitchDefinitionRevealed(
		const FBattleEngineState& State,
		const FBattlerId BattlerId,
		const bool bAbility)
	{
		const FBattleBattlerState* Battler = State.FindBattler(BattlerId);
		FBattleTriggerSubject Owner;
		FBattleTriggerSourceDefinition Source;
		if (Battler == nullptr
			|| !FBattleTriggerSubject::TryCreateBattler(BattlerId, Owner)
			|| !(bAbility
				? FBattleTriggerSourceDefinition::TryCreateAbility(Battler->AbilityId, Source)
				: FBattleTriggerSourceDefinition::TryCreateItem(
					Battler->HeldItem.CurrentItemId,
					Source)))
		{
			return false;
		}
		return State.AbilityItemRevealTracker.HasBeenRevealed(Source, Owner);
	}

	struct FAtomicSwitchCheckpointObservation
	{
		uint64 StateVersion = 0;
		uint64 NextResolutionId = 0;
		uint64 NextEventOrdinal = 0;
		uint64 NextConditionCreationOrdinal = 0;
		uint64 NextTriggerToken = 0;
		int32 ActionIndex = INDEX_NONE;
		int32 ResolutionCount = 0;
		int32 EventCount = 0;
		int32 RandomTraceCount = 0;
		int32 PendingDecisionRequestCount = 0;
		int32 PendingReplacementCount = 0;
		int32 OpponentRemovalCheckpointCount = 0;
		int32 PendingTriggerDispatchCount = 0;
		int32 PendingTriggerEffectCount = 0;
		int32 PendingTriggerLifecycleCount = 0;
		EBattlePhase Phase = EBattlePhase::Setup;
		EBattleOutcome Outcome = EBattleOutcome::InProgress;
		EBattleOutcomeCause OutcomeCause = EBattleOutcomeCause::None;
		bool bPendingDecisionSet = false;
		bool bActionStarted = false;
		bool bActionFinished = false;
		bool bIncomingAbilityRevealed = false;
		bool bIncomingItemRevealed = false;
		FAtomicSwitchBattlerObservation Outgoing;
		FAtomicSwitchBattlerObservation Incoming;
		FAtomicSwitchBattlerObservation Opponent;
		TArray<FActiveSlotId> ActiveSlotIds;
		TArray<FTrainerId> ActiveTrainerIds;
		TArray<FBattlerId> ActiveBattlerIds;
		TArray<uint8> ActiveAvailability;
		TArray<FAtomicSwitchConditionObservation> PlayerHazards;
		TArray<FBattleHeldItemInstanceState> LedgerStates;
		TArray<FBattleTriggerRegistrationId> TriggerRegistrationIds;
		TArray<uint64> TriggerCreationOrdinals;
		TArray<FBattleTriggerSourceDefinition> TriggerSources;
		TArray<uint8> TriggerSuppression;
	};

	FAtomicSwitchCheckpointObservation ObserveAtomicSwitchCheckpoint(
		const FBattleEngine& Engine)
	{
		const FBattleEngineState& State =
			FBattleC09BWildFlowEngineFixture::GetState(Engine);
		FAtomicSwitchCheckpointObservation Observation;
		Observation.StateVersion = State.StateVersion;
		Observation.NextResolutionId = State.NextResolutionId;
		Observation.NextEventOrdinal = State.NextEventOrdinal;
		Observation.NextConditionCreationOrdinal = State.NextConditionCreationOrdinal;
		Observation.NextTriggerToken = State.NextTriggerReentrancyToken;
		Observation.ActionIndex = State.CurrentLockedActionIndex;
		Observation.ResolutionCount = State.Resolutions.Num();
		Observation.EventCount = State.OrderedEvents.Num();
		Observation.RandomTraceCount = State.Random->GetTrace().Num();
		Observation.PendingDecisionRequestCount = State.PendingDecisionRequests.Num();
		Observation.PendingReplacementCount = State.PendingReplacements.Num();
		Observation.OpponentRemovalCheckpointCount =
			State.AvailableOpponentRemovalCheckpoints.Num();
		Observation.PendingTriggerDispatchCount =
			State.TriggerFramework.GetPendingDispatchCount();
		Observation.PendingTriggerEffectCount =
			State.TriggerFramework.GetPendingEffectRequestCount();
		Observation.PendingTriggerLifecycleCount =
			State.TriggerFramework.GetPendingLifecycleFactCount();
		Observation.Phase = State.Phase;
		Observation.Outcome = State.Outcome;
		Observation.OutcomeCause = State.OutcomeCause;
		Observation.bPendingDecisionSet = State.PendingDecision.IsSet();
		if (State.LockedActions.IsValidIndex(State.CurrentLockedActionIndex))
		{
			const FBattleLockedActionState& Action =
				State.LockedActions[State.CurrentLockedActionIndex];
			Observation.bActionStarted = Action.bStarted;
			Observation.bActionFinished = Action.bFinished;
		}
		const FBattlerId OutgoingId = MakeNumericId<FBattlerId>(PlayerLeftValue);
		const FBattlerId IncomingId = MakeNumericId<FBattlerId>(PlayerReserveValue);
		Observation.Outgoing = ObserveAtomicSwitchBattler(State, OutgoingId);
		Observation.Incoming = ObserveAtomicSwitchBattler(State, IncomingId);
		Observation.Opponent = ObserveAtomicSwitchBattler(
			State,
			MakeNumericId<FBattlerId>(OpponentLeftValue));
		Observation.bIncomingAbilityRevealed =
			IsAtomicSwitchDefinitionRevealed(State, IncomingId, true);
		Observation.bIncomingItemRevealed =
			IsAtomicSwitchDefinitionRevealed(State, IncomingId, false);
		for (const FBattleActivePositionState& Active : State.ActivePositions)
		{
			Observation.ActiveSlotIds.Add(Active.ActiveSlotId);
			Observation.ActiveTrainerIds.Add(Active.TrainerId);
			Observation.ActiveBattlerIds.Add(Active.BattlerId);
			Observation.ActiveAvailability.Add(Active.bAvailable ? 1 : 0);
		}
		const FBattleSideState* PlayerSide = State.Sides.FindByPredicate(
			[](const FBattleSideState& Candidate)
			{
				return Candidate.Side == EBattleSide::Player;
			});
		if (PlayerSide != nullptr)
		{
			for (const FBattleConditionState& Hazard : PlayerSide->Hazards)
			{
				Observation.PlayerHazards.Add(ObserveAtomicSwitchCondition(Hazard));
			}
		}
		Observation.LedgerStates.Append(State.HeldItemLedger.GetStates());
		for (const FBattleTriggerRegistrationState& Registration :
			State.TriggerFramework.GetActiveRegistrations())
		{
			Observation.TriggerRegistrationIds.Add(Registration.RegistrationId);
			Observation.TriggerCreationOrdinals.Add(Registration.CreationOrdinal);
			Observation.TriggerSources.Add(Registration.Spec.SourceDefinition);
			Observation.TriggerSuppression.Add(Registration.bSuppressed ? 1 : 0);
		}
		return Observation;
	}

	bool AreAtomicSwitchMechanicsIdentical(
		const FAtomicSwitchCheckpointObservation& Left,
		const FAtomicSwitchCheckpointObservation& Right)
	{
		return Left.NextConditionCreationOrdinal == Right.NextConditionCreationOrdinal
			&& Left.NextTriggerToken == Right.NextTriggerToken
			&& Left.ActionIndex == Right.ActionIndex
			&& Left.RandomTraceCount == Right.RandomTraceCount
			&& Left.PendingDecisionRequestCount == Right.PendingDecisionRequestCount
			&& Left.PendingReplacementCount == Right.PendingReplacementCount
			&& Left.OpponentRemovalCheckpointCount == Right.OpponentRemovalCheckpointCount
			&& Left.PendingTriggerDispatchCount == Right.PendingTriggerDispatchCount
			&& Left.PendingTriggerEffectCount == Right.PendingTriggerEffectCount
			&& Left.PendingTriggerLifecycleCount == Right.PendingTriggerLifecycleCount
			&& Left.Phase == Right.Phase
			&& Left.Outcome == Right.Outcome
			&& Left.OutcomeCause == Right.OutcomeCause
			&& Left.bPendingDecisionSet == Right.bPendingDecisionSet
			&& Left.bActionStarted == Right.bActionStarted
			&& Left.bActionFinished == Right.bActionFinished
			&& Left.bIncomingAbilityRevealed == Right.bIncomingAbilityRevealed
			&& Left.bIncomingItemRevealed == Right.bIncomingItemRevealed
			&& AreAtomicSwitchBattlersIdentical(Left.Outgoing, Right.Outgoing)
			&& AreAtomicSwitchBattlersIdentical(Left.Incoming, Right.Incoming)
			&& AreAtomicSwitchBattlersIdentical(Left.Opponent, Right.Opponent)
			&& Left.ActiveSlotIds == Right.ActiveSlotIds
			&& Left.ActiveTrainerIds == Right.ActiveTrainerIds
			&& Left.ActiveBattlerIds == Right.ActiveBattlerIds
			&& Left.ActiveAvailability == Right.ActiveAvailability
			&& AreAtomicSwitchConditionsIdentical(Left.PlayerHazards, Right.PlayerHazards)
			&& Left.LedgerStates == Right.LedgerStates
			&& Left.TriggerRegistrationIds == Right.TriggerRegistrationIds
			&& Left.TriggerCreationOrdinals == Right.TriggerCreationOrdinals
			&& Left.TriggerSources == Right.TriggerSources
			&& Left.TriggerSuppression == Right.TriggerSuppression;
	}

	template <typename ElementType, typename EqualType>
	bool AreOrderedPivotTestValuesEqual(
		const TConstArrayView<ElementType> Left,
		const TConstArrayView<ElementType> Right,
		EqualType Equal)
	{
		if (Left.Num() != Right.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < Left.Num(); ++Index)
		{
			if (!Equal(Left[Index], Right[Index]))
			{
				return false;
			}
		}
		return true;
	}

	bool ArePivotTestDecisionsIdentical(
		const FBattleDecision& Left,
		const FBattleDecision& Right)
	{
		return Left.IsValid() == Right.IsValid()
			&& Left.GetStateVersion() == Right.GetStateVersion()
			&& Left.GetRequestKind() == Right.GetRequestKind()
			&& Left.GetDecisionOwnerTrainerId() == Right.GetDecisionOwnerTrainerId()
			&& Left.GetActingBattlerId() == Right.GetActingBattlerId()
			&& Left.GetActionKind() == Right.GetActionKind()
			&& Left.GetMoveId() == Right.GetMoveId()
			&& Left.GetSwitchPartySlotId() == Right.GetSwitchPartySlotId()
			&& Left.GetItemId() == Right.GetItemId()
			&& Left.GetItemPartyTargetId() == Right.GetItemPartyTargetId()
			&& Left.GetActiveTargetId() == Right.GetActiveTargetId();
	}

	bool ArePivotTestRequestsIdentical(
		const FBattleDecisionRequest& Left,
		const FBattleDecisionRequest& Right)
	{
		return Left.IsValid() == Right.IsValid()
			&& Left.GetStateVersion() == Right.GetStateVersion()
			&& Left.GetRequestKind() == Right.GetRequestKind()
			&& Left.GetDecisionOwnerTrainerId() == Right.GetDecisionOwnerTrainerId()
			&& Left.GetActingBattlerId() == Right.GetActingBattlerId()
			&& Left.GetActingSlotId() == Right.GetActingSlotId()
			&& AreOrderedPivotTestValuesEqual(
				Left.GetLegalActionKinds(),
				Right.GetLegalActionKinds(),
				[](const EBattleActionKind L, const EBattleActionKind R)
				{
					return L == R;
				})
			&& AreOrderedPivotTestValuesEqual(
				Left.GetLegalMoveIds(),
				Right.GetLegalMoveIds(),
				[](const FMoveId& L, const FMoveId& R)
				{
					return L == R;
				})
			&& AreOrderedPivotTestValuesEqual(
				Left.GetAutomaticallyTargetedMoveIds(),
				Right.GetAutomaticallyTargetedMoveIds(),
				[](const FMoveId& L, const FMoveId& R)
				{
					return L == R;
				})
			&& AreOrderedPivotTestValuesEqual(
				Left.GetLegalSwitchPartySlots(),
				Right.GetLegalSwitchPartySlots(),
				[](const FPartySlotId L, const FPartySlotId R)
				{
					return L == R;
				})
			&& AreOrderedPivotTestValuesEqual(
				Left.GetLegalItemIds(),
				Right.GetLegalItemIds(),
				[](const FItemId& L, const FItemId& R)
				{
					return L == R;
				})
			&& AreOrderedPivotTestValuesEqual(
				Left.GetLegalActiveTargets(),
				Right.GetLegalActiveTargets(),
				[](const FActiveSlotId L, const FActiveSlotId R)
				{
					return L == R;
				})
			&& AreOrderedPivotTestValuesEqual(
				Left.GetLegalPartyTargets(),
				Right.GetLegalPartyTargets(),
				[](const FPartySlotId L, const FPartySlotId R)
				{
					return L == R;
				})
			&& AreOrderedPivotTestValuesEqual(
				Left.GetLegalMoveTargets(),
				Right.GetLegalMoveTargets(),
				[](const FBattleMoveTargetOption& L, const FBattleMoveTargetOption& R)
				{
					return L.MoveId == R.MoveId && L.ActiveSlotId == R.ActiveSlotId;
				})
			&& AreOrderedPivotTestValuesEqual(
				Left.GetLegalItemPartyTargets(),
				Right.GetLegalItemPartyTargets(),
				[](const FBattleItemPartyTargetOption& L,
					const FBattleItemPartyTargetOption& R)
				{
					return L.ItemId == R.ItemId && L.PartySlotId == R.PartySlotId;
				})
			&& AreOrderedPivotTestValuesEqual(
				Left.GetLegalItemActiveTargets(),
				Right.GetLegalItemActiveTargets(),
				[](const FBattleItemActiveTargetOption& L,
					const FBattleItemActiveTargetOption& R)
				{
					return L.ItemId == R.ItemId && L.ActiveSlotId == R.ActiveSlotId;
				})
			&& AreOrderedPivotTestValuesEqual(
				Left.GetUnavailableOptions(),
				Right.GetUnavailableOptions(),
				[](const FBattleUnavailableDecisionOption& L,
					const FBattleUnavailableDecisionOption& R)
				{
					return L.Kind == R.Kind
						&& L.Reason == R.Reason
						&& L.ActionKind == R.ActionKind
						&& L.MoveId == R.MoveId
						&& L.PartySlotId == R.PartySlotId
						&& L.ItemId == R.ItemId
						&& L.ActiveSlotId == R.ActiveSlotId;
				});
	}

	bool ArePivotTestTargetResolutionsIdentical(
		const TOptional<FBattleTargetResolutionResult>& Left,
		const TOptional<FBattleTargetResolutionResult>& Right)
	{
		if (Left.IsSet() != Right.IsSet())
		{
			return false;
		}
		if (!Left.IsSet())
		{
			return true;
		}
		const FBattleTargetResolutionResult& L = Left.GetValue();
		const FBattleTargetResolutionResult& R = Right.GetValue();
		return L.TargetClass == R.TargetClass
			&& L.Outcome == R.Outcome
			&& L.bWasRedirected == R.bWasRedirected
			&& L.bUsedFaintedTargetFallback == R.bUsedFaintedTargetFallback
			&& AreOrderedPivotTestValuesEqual(
				TConstArrayView<FBattleResolvedTarget>(L.Targets),
				TConstArrayView<FBattleResolvedTarget>(R.Targets),
				[](const FBattleResolvedTarget& LTarget,
					const FBattleResolvedTarget& RTarget)
				{
					return LTarget == RTarget;
				});
	}

	bool ArePivotTestLockedActionsIdentical(
		const FBattleLockedActionState& Left,
		const FBattleLockedActionState& Right)
	{
		return Left.ActionId == Right.ActionId
			&& Left.QueueOrdinal == Right.QueueOrdinal
			&& ArePivotTestDecisionsIdentical(Left.Decision, Right.Decision)
			&& Left.OrderKey.CommandBand == Right.OrderKey.CommandBand
			&& Left.OrderKey.MovePriority == Right.OrderKey.MovePriority
			&& Left.OrderKey.FractionalPriorityTenths
				== Right.OrderKey.FractionalPriorityTenths
			&& Left.OrderKey.EffectiveSpeed == Right.OrderKey.EffectiveSpeed
			&& Left.OrderKey.ActingSlotId == Right.OrderKey.ActingSlotId
			&& Left.TargetClass == Right.TargetClass
			&& Left.SelectedTargetBattlerId == Right.SelectedTargetBattlerId
			&& Left.bStarted == Right.bStarted
			&& Left.bMoveCommitted == Right.bMoveCommitted
			&& ArePivotTestTargetResolutionsIdentical(
				Left.TargetResolution,
				Right.TargetResolution)
			&& Left.EffectExecutionState == Right.EffectExecutionState
			&& Left.bFinished == Right.bFinished;
	}

	bool TryCopyPivotRequestWithSwitchSlot(
		const FBattleDecisionRequest& Source,
		const FPartySlotId PartySlotId,
		FBattleDecisionRequest& OutRequest)
	{
		FBattleDecisionRequestSpec Spec;
		Spec.StateVersion = Source.GetStateVersion();
		Spec.RequestKind = Source.GetRequestKind();
		Spec.DecisionOwnerTrainerId = Source.GetDecisionOwnerTrainerId();
		Spec.ActingBattlerId = Source.GetActingBattlerId();
		Spec.ActingSlotId = Source.GetActingSlotId();
		Spec.LegalActionKinds.Append(Source.GetLegalActionKinds());
		Spec.LegalMoveIds.Append(Source.GetLegalMoveIds());
		Spec.AutomaticallyTargetedMoveIds.Append(Source.GetAutomaticallyTargetedMoveIds());
		Spec.LegalSwitchPartySlots.Add(PartySlotId);
		Spec.LegalItemIds.Append(Source.GetLegalItemIds());
		Spec.LegalActiveTargets.Append(Source.GetLegalActiveTargets());
		Spec.LegalPartyTargets.Append(Source.GetLegalPartyTargets());
		Spec.LegalMoveTargets.Append(Source.GetLegalMoveTargets());
		Spec.LegalItemPartyTargets.Append(Source.GetLegalItemPartyTargets());
		Spec.LegalItemActiveTargets.Append(Source.GetLegalItemActiveTargets());
		Spec.UnavailableOptions.Append(Source.GetUnavailableOptions());
		FBattleRejection Rejection;
		return FBattleDecisionRequest::TryCreate(Spec, OutRequest, Rejection);
	}

	struct FAtomicPivotSwitchObservation
	{
		FAtomicSwitchCheckpointObservation Mechanics;
		int32 LockedActionCount = 0;
		int32 SubmittedDecisionCount = 0;
		int32 RemainingActions = INDEX_NONE;
		int32 ActionStartedEventCount = 0;
		bool bHasCurrentAction = false;
		bool bHasPendingDecision = false;
		bool bHasPendingRequest = false;
		bool bHasLastSubmittedDecision = false;
		FBattleLockedActionState CurrentAction;
		FBattleDecisionRequest PendingDecision;
		FBattleDecisionRequest PendingRequest;
		FBattleDecision LastSubmittedDecision;
	};

	FAtomicPivotSwitchObservation ObserveAtomicPivotSwitchCheckpoint(
		const FBattleEngine& Engine)
	{
		const FBattleEngineState& State =
			FBattleC09BWildFlowEngineFixture::GetState(Engine);
		FAtomicPivotSwitchObservation Observation;
		Observation.Mechanics = ObserveAtomicSwitchCheckpoint(Engine);
		Observation.LockedActionCount = State.LockedActions.Num();
		Observation.SubmittedDecisionCount = State.SubmittedDecisions.Num();
		if (State.LockedActions.IsValidIndex(State.CurrentLockedActionIndex))
		{
			Observation.bHasCurrentAction = true;
			Observation.CurrentAction = State.LockedActions[State.CurrentLockedActionIndex];
		}
		if (State.PendingDecision.IsSet())
		{
			Observation.bHasPendingDecision = true;
			Observation.PendingDecision = State.PendingDecision.GetValue();
		}
		if (!State.PendingDecisionRequests.IsEmpty())
		{
			Observation.bHasPendingRequest = true;
			Observation.PendingRequest = State.PendingDecisionRequests[0];
		}
		if (!State.SubmittedDecisions.IsEmpty())
		{
			Observation.bHasLastSubmittedDecision = true;
			Observation.LastSubmittedDecision = State.SubmittedDecisions.Last();
		}
		const FBattleTrainerState* Trainer = State.FindTrainer(
			MakeNumericId<FTrainerId>(PlayerTrainerValue));
		if (Trainer != nullptr)
		{
			Observation.RemainingActions = Trainer->ActionAllowance.RemainingActions;
		}
		for (const FBattleEvent& Event : State.OrderedEvents)
		{
			Observation.ActionStartedEventCount +=
				Event.GetType() == EBattleEventType::ActionStarted ? 1 : 0;
		}
		return Observation;
	}

	bool AreAtomicPivotSwitchGameplayFactsIdentical(
		const FAtomicPivotSwitchObservation& Left,
		const FAtomicPivotSwitchObservation& Right)
	{
		return AreAtomicSwitchMechanicsIdentical(Left.Mechanics, Right.Mechanics)
			&& Left.LockedActionCount == Right.LockedActionCount
			&& Left.RemainingActions == Right.RemainingActions
			&& Left.ActionStartedEventCount == Right.ActionStartedEventCount
			&& Left.bHasCurrentAction == Right.bHasCurrentAction
			&& (!Left.bHasCurrentAction
				|| ArePivotTestLockedActionsIdentical(
					Left.CurrentAction,
					Right.CurrentAction))
			&& Left.bHasPendingDecision == Right.bHasPendingDecision
			&& (!Left.bHasPendingDecision
				|| ArePivotTestRequestsIdentical(
					Left.PendingDecision,
					Right.PendingDecision))
			&& Left.bHasPendingRequest == Right.bHasPendingRequest
			&& (!Left.bHasPendingRequest
				|| ArePivotTestRequestsIdentical(
					Left.PendingRequest,
					Right.PendingRequest));
	}

	bool VerifyRejectedAtomicVoluntarySwitch(
		FAutomationTestBase& Test,
		const FBattleEngine& Engine,
		const FAtomicSwitchCheckpointObservation& Before,
		const uint64 ExpectedStateVersion,
		const EBattleRejectionReason ExpectedReason,
		const FBattleResolution& Returned)
	{
		const FAtomicSwitchCheckpointObservation After =
			ObserveAtomicSwitchCheckpoint(Engine);
		bool bValid = true;
		bValid &= Test.TestFalse(TEXT("Voluntary Switch checkpoint failure is rejected"),
			Returned.WasAccepted());
		bValid &= Test.TestEqual(TEXT("Voluntary Switch rejection is typed"),
			Returned.GetRejection().Reason,
			ExpectedReason);
		bValid &= Test.TestTrue(TEXT("Rejected Switch is appended exactly once"),
			IsReturnedResolutionAppendedExactlyOnce(Engine, Returned));
		bValid &= Test.TestTrue(TEXT("Rejected Switch publishes cancellation only"),
			Returned.GetEvents().Num() == 1
				&& Returned.GetEvents()[0].GetType() == EBattleEventType::ActionCanceled
				&& Returned.GetEvents()[0].GetCause() == EBattleEventCause::Rule);
		bValid &= Test.TestEqual(TEXT("Rejection appends one resolution"),
			After.ResolutionCount, Before.ResolutionCount + 1);
		bValid &= Test.TestEqual(TEXT("Rejection appends one event"),
			After.EventCount, Before.EventCount + 1);
		bValid &= Test.TestEqual(TEXT("Rejection consumes one resolution id"),
			After.NextResolutionId, Before.NextResolutionId + 1);
		bValid &= Test.TestEqual(TEXT("Rejection consumes one event ordinal"),
			After.NextEventOrdinal, Before.NextEventOrdinal + 1);
		bValid &= Test.TestEqual(TEXT("Rejection has no accepted version delta"),
			After.StateVersion, ExpectedStateVersion);
		bValid &= Test.TestTrue(TEXT("Rejection preserves every staged gameplay domain"),
			AreAtomicSwitchMechanicsIdentical(After, Before));
		const FBattleReplayRecord Replay = Engine.ExportReplayRecord();
		bValid &= Test.TestEqual(TEXT("Rejected Switch replay uses schema 6"),
			Replay.GetSchemaVersion(), FBattleReplayRecord::CurrentSchemaVersion);
		bValid &= Test.TestTrue(TEXT("Rejected Switch replay contains the same fact"),
			!Replay.GetResolutions().IsEmpty()
				&& Replay.GetResolutions().Last().GetResolutionId()
					== Returned.GetResolutionId()
				&& Replay.GetResolutions().Last().GetRejection().Reason
					== Returned.GetRejection().Reason);
		return bValid;
	}

	enum class EAtomicSwitchFailureFamily : uint8
	{
		EntryItemReveal,
		EntryHazard,
		ImmediateHeldItem,
		EntryAbility
	};

	bool RunAtomicVoluntarySwitchFailureFamily(
		FAutomationTestBase& Test,
		const EAtomicSwitchFailureFamily Family)
	{
		FItemId IncomingItemId;
		FAbilityId IncomingAbilityId = FBattleAbilityRules::GetBlazeId();
		if (Family == EAtomicSwitchFailureFamily::EntryItemReveal)
		{
			IncomingItemId = FBattleItemRules::GetAirBalloonId();
		}
		else if (Family == EAtomicSwitchFailureFamily::ImmediateHeldItem)
		{
			IncomingItemId = FBattleItemRules::GetSitrusBerryId();
		}
		else if (Family == EAtomicSwitchFailureFamily::EntryAbility)
		{
			IncomingAbilityId = FBattleAbilityRules::GetIntimidateId();
		}
		TUniquePtr<FBattleEngine> Engine;
		if (!Test.TestTrue(TEXT("Failure-family Switch engine is created"),
				TryMakeSequenceEngine(
					MakeAtomicVoluntarySwitchScenario(
						IncomingItemId,
						IncomingAbilityId,
						Family == EAtomicSwitchFailureFamily::ImmediateHeldItem ? 90 : 200),
					{},
					Engine))
			|| !Test.TestTrue(TEXT("Failure-family Switch is locked"),
				TryPrepareAtomicVoluntarySwitch(*Engine))
			|| !Test.TestTrue(TEXT("Outgoing transient state is seeded"),
				TrySeedAtomicSwitchOutgoingTransients(*Engine))
			|| !Test.TestTrue(TEXT("Failure-family Switch is started"),
				TryBeginAtomicVoluntarySwitch(*Engine)))
		{
			return false;
		}
		if (Family == EAtomicSwitchFailureFamily::EntryHazard
			&& !Test.TestTrue(TEXT("Entry hazard is seeded"),
				TrySeedAtomicSwitchHazard(
					*Engine,
					FBattleFieldSideConditionRules::GetSpikesId())))
		{
			return false;
		}
		FBattleEngineState& MutableState =
			FBattleC09BWildFlowEngineFixture::GetMutableState(*Engine);
		MutableState.NextTriggerReentrancyToken =
			TNumericLimits<uint64>::Max() - 1;
		const FAtomicSwitchCheckpointObservation Before =
			ObserveAtomicSwitchCheckpoint(*Engine);
		const FBattleResolution Rejected = Engine->ExecuteCurrentSwitch();
		return VerifyRejectedAtomicVoluntarySwitch(
			Test,
			*Engine,
			Before,
			Before.StateVersion,
			EBattleRejectionReason::CheckpointPreparationFailed,
			Rejected);
	}

	bool VerifyRejectedAtomicPivotSwitch(
		FAutomationTestBase& Test,
		const FBattleEngine& Engine,
		const FAtomicPivotSwitchObservation& Before,
		const FAtomicPivotSwitchObservation& ExpectedGameplay,
		const FBattleDecision& ExpectedSubmittedResponse,
		const EBattleRejectionReason ExpectedReason,
		const FBattleResolution& Returned)
	{
		const FAtomicPivotSwitchObservation After =
			ObserveAtomicPivotSwitchCheckpoint(Engine);
		bool bValid = true;
		bValid &= Test.TestFalse(TEXT("Pivot checkpoint failure is rejected"),
			Returned.WasAccepted());
		bValid &= Test.TestEqual(TEXT("Pivot rejection is typed"),
			Returned.GetRejection().Reason,
			ExpectedReason);
		bValid &= Test.TestTrue(TEXT("Rejected Pivot is returned and appended exactly once"),
			IsReturnedResolutionAppendedExactlyOnce(Engine, Returned));
		bValid &= Test.TestTrue(TEXT("Rejected Pivot publishes one Fight cancellation only"),
			Returned.GetEvents().Num() == 1
				&& Returned.GetEvents()[0].GetType() == EBattleEventType::ActionCanceled
				&& Returned.GetEvents()[0].GetCause() == EBattleEventCause::Rule
				&& Returned.GetEvents()[0].GetCauseActionKind()
					== EBattleActionKind::Fight);
		bValid &= Test.TestEqual(TEXT("Rejected Pivot appends one resolution"),
			After.Mechanics.ResolutionCount,
			Before.Mechanics.ResolutionCount + 1);
		bValid &= Test.TestEqual(TEXT("Rejected Pivot appends one event"),
			After.Mechanics.EventCount,
			Before.Mechanics.EventCount + 1);
		bValid &= Test.TestEqual(TEXT("Rejected Pivot consumes one resolution id"),
			After.Mechanics.NextResolutionId,
			Before.Mechanics.NextResolutionId + 1);
		bValid &= Test.TestEqual(TEXT("Rejected Pivot consumes one event ordinal"),
			After.Mechanics.NextEventOrdinal,
			Before.Mechanics.NextEventOrdinal + 1);
		bValid &= Test.TestEqual(TEXT("Rejected Pivot has no accepted version delta"),
			After.Mechanics.StateVersion,
			ExpectedGameplay.Mechanics.StateVersion);
		bValid &= Test.TestTrue(TEXT("Rejected Pivot preserves every expected gameplay fact"),
			AreAtomicPivotSwitchGameplayFactsIdentical(After, ExpectedGameplay));
		bValid &= Test.TestTrue(TEXT("Rejected Pivot preserves AwaitingPivot Fight continuation"),
			After.bHasCurrentAction
				&& After.CurrentAction.Decision.GetActionKind() == EBattleActionKind::Fight
				&& After.CurrentAction.EffectExecutionState
					== EBattleLockedEffectExecutionState::AwaitingPivot
				&& !After.CurrentAction.bFinished
				&& After.Mechanics.ActionIndex == ExpectedGameplay.Mechanics.ActionIndex
				&& After.bHasPendingDecision
				&& After.bHasPendingRequest
				&& ArePivotTestRequestsIdentical(
					After.PendingDecision,
					ExpectedGameplay.PendingDecision)
				&& ArePivotTestRequestsIdentical(
					After.PendingRequest,
					ExpectedGameplay.PendingRequest));
		bValid &= Test.TestEqual(TEXT("Submitted Pivot response remains replay input"),
			After.SubmittedDecisionCount,
			Before.SubmittedDecisionCount + 1);
		bValid &= Test.TestTrue(TEXT("Submitted Pivot response remains exact"),
			After.bHasLastSubmittedDecision
				&& ArePivotTestDecisionsIdentical(
					After.LastSubmittedDecision,
					ExpectedSubmittedResponse));
		bValid &= Test.TestEqual(TEXT("Rejected Pivot consumes no gameplay RNG"),
			After.Mechanics.RandomTraceCount,
			ExpectedGameplay.Mechanics.RandomTraceCount);
		const FBattleReplayRecord Replay = Engine.ExportReplayRecord();
		bValid &= Test.TestEqual(TEXT("Rejected Pivot replay schema is exactly 6"),
			Replay.GetSchemaVersion(),
			static_cast<uint32>(6));
		bValid &= Test.TestTrue(TEXT("Rejected Pivot replay keeps the submitted response input"),
			Replay.GetInputs().Decisions.Num() == After.SubmittedDecisionCount
				&& !Replay.GetInputs().Decisions.IsEmpty()
				&& ArePivotTestDecisionsIdentical(
					Replay.GetInputs().Decisions.Last(),
					ExpectedSubmittedResponse));
		bValid &= Test.TestTrue(TEXT("Rejected Pivot replay contains no checkpoint success fact"),
			!Replay.GetResolutions().IsEmpty()
				&& Replay.GetResolutions().Last().GetResolutionId()
					== Returned.GetResolutionId()
				&& !Replay.GetResolutions().Last().WasAccepted()
				&& Replay.GetResolutions().Last().GetRejection().Reason == ExpectedReason
				&& Replay.GetResolutions().Last().GetEvents().Num() == 1
				&& Replay.GetResolutions().Last().GetEvents()[0].GetType()
					== EBattleEventType::ActionCanceled);
		return bValid;
	}

	bool RunAtomicPivotSwitchFailureFamily(
		FAutomationTestBase& Test,
		const EAtomicSwitchFailureFamily Family)
	{
		FItemId IncomingItemId;
		FAbilityId IncomingAbilityId = FBattleAbilityRules::GetBlazeId();
		if (Family == EAtomicSwitchFailureFamily::EntryItemReveal)
		{
			IncomingItemId = FBattleItemRules::GetAirBalloonId();
		}
		else if (Family == EAtomicSwitchFailureFamily::ImmediateHeldItem)
		{
			IncomingItemId = FBattleItemRules::GetSitrusBerryId();
		}
		else if (Family == EAtomicSwitchFailureFamily::EntryAbility)
		{
			IncomingAbilityId = FBattleAbilityRules::GetIntimidateId();
		}
		TUniquePtr<FBattleEngine> Engine;
		if (!Test.TestTrue(TEXT("Failure-family Pivot engine is created"),
				TryMakeSequenceEngine(
					MakeAtomicPivotSwitchScenario(
						IncomingItemId,
						IncomingAbilityId,
						Family == EAtomicSwitchFailureFamily::ImmediateHeldItem ? 90 : 200),
					{},
					Engine))
			|| !Test.TestTrue(TEXT("Failure-family Pivot outgoing transients are seeded"),
				TrySeedAtomicSwitchOutgoingTransients(*Engine)))
		{
			return false;
		}
		if (Family == EAtomicSwitchFailureFamily::EntryHazard
			&& !Test.TestTrue(TEXT("Failure-family Pivot hazard is seeded"),
				TrySeedAtomicSwitchHazard(
					*Engine,
					FBattleFieldSideConditionRules::GetSpikesId())))
		{
			return false;
		}
		FBattleDecisionRequest Request;
		FBattleDecision Response;
		if (!Test.TestTrue(TEXT("Failure-family Pivot reaches AwaitingPivot"),
				TryPrepareAtomicPivotSwitch(*Engine, Request))
			|| !Test.TestTrue(TEXT("Failure-family Pivot response is created"),
				TryMakePivotSwitchDecision(Request, MakePartySlotId(1), Response)))
		{
			return false;
		}
		FBattleEngineState& MutableState =
			FBattleC09BWildFlowEngineFixture::GetMutableState(*Engine);
		MutableState.NextTriggerReentrancyToken = TNumericLimits<uint64>::Max() - 1;
		const FAtomicPivotSwitchObservation Before =
			ObserveAtomicPivotSwitchCheckpoint(*Engine);
		const FBattleResolution Rejected = Engine->SubmitDecision(Response);
		return VerifyRejectedAtomicPivotSwitch(
			Test,
			*Engine,
			Before,
			Before,
			Response,
			EBattleRejectionReason::CheckpointPreparationFailed,
			Rejected);
	}

	FAtomicWildScenario MakePreMoveScenario(
		const FMoveId ExtraMoveId = FMoveId(),
		const int32 PlayerCurrentHP = 200,
		const FAbilityId AbilityId = FAbilityId(),
		const FItemId HeldItemId = FItemId())
	{
		FAtomicWildScenario Scenario;
		Scenario.PlayerLeftSpeed = 300;
		Scenario.OpponentLeftSpeed = 50;
		Scenario.PlayerCurrentHP = PlayerCurrentHP;
		Scenario.PlayerAbilityId = AbilityId;
		Scenario.PlayerHeldItemId = HeldItemId;
		Scenario.PlayerExtraMoveId = ExtraMoveId;
		return Scenario;
	}

	bool TryLockAndBeginPreMove(
		FBattleEngine& Engine,
		const FMoveId MoveId = FMoveId())
	{
		return LockTurn(
				Engine,
				PlayerLeftValue,
				EBattleActionKind::Fight,
				MoveId)
			&& BeginExpectedWildAction(
				Engine,
				PlayerLeftValue,
				EBattleActionKind::Fight);
	}

	int32 GetPreMovePP(
		const FBattleEngine& Engine,
		const FBattlerId BattlerId,
		const FMoveId MoveId)
	{
		const FBattleBattlerState* Battler =
			FBattleC09BWildFlowEngineFixture::GetState(Engine).FindBattler(BattlerId);
		const FBattleMoveSlotState* Move = Battler != nullptr
			? Battler->Moves.FindByPredicate(
				[MoveId](const FBattleMoveSlotState& Candidate)
				{
					return Candidate.MoveId == MoveId;
				})
			: nullptr;
		return Move != nullptr ? Move->CurrentPP : INDEX_NONE;
	}

	const FBattleConditionState* FindPreMoveVolatile(
		const FBattleEngine& Engine,
		const FBattlerId BattlerId,
		const FConditionId VolatileId)
	{
		const FBattleBattlerState* Battler =
			FBattleC09BWildFlowEngineFixture::GetState(Engine).FindBattler(BattlerId);
		return Battler != nullptr
			? Battler->Volatiles.FindByPredicate(
				[VolatileId](const FBattleConditionState& Candidate)
				{
					return Candidate.ConditionId == VolatileId;
				})
			: nullptr;
	}

	FBattleExpectedRandomDraw MakeTargetExpectedDraw(
		const uint32 Maximum,
		const uint32 Result)
	{
		return {
			0,
			Maximum,
			Result,
			FBattleTargetResolver::GetRandomLegalOpponentRulePurpose()};
	}

	bool TryPrepareTargetCheckpoint(
		FBattleEngine& Engine,
		const FMoveId MoveId)
	{
		if (!TryLockAndBeginPreMove(Engine, MoveId))
		{
			return false;
		}
		const FBattleResolution Commit = Engine.CommitCurrentMoveAfterPreMoveGates();
		const FBattleEngineState& State =
			FBattleC09BWildFlowEngineFixture::GetState(Engine);
		const FBattleLockedActionState* Current =
			State.LockedActions.IsValidIndex(State.CurrentLockedActionIndex)
				? &State.LockedActions[State.CurrentLockedActionIndex]
				: nullptr;
		return Commit.WasAccepted()
			&& Current != nullptr
			&& Current->bStarted
			&& Current->bMoveCommitted
			&& !Current->TargetResolution.IsSet()
			&& !Current->bFinished;
	}

	bool TryPrepareLastTargetCheckpoint(
		FBattleEngine& Engine,
		const FMoveId MoveId)
	{
		const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
		return LockTurn(
				Engine,
				PlayerLeftValue,
				EBattleActionKind::Fight,
				MoveId)
			&& TryPrepareLastLockedAction(Engine, ActorId)
			&& BeginExpectedWildAction(
				Engine,
				PlayerLeftValue,
				EBattleActionKind::Fight)
			&& Engine.CommitCurrentMoveAfterPreMoveGates().WasAccepted();
	}

	bool TryMarkTargetFainted(
		FBattleEngine& Engine,
		const FBattlerId BattlerId)
	{
		FBattleBattlerState* Battler =
			FBattleC09BWildFlowEngineFixture::GetMutableState(Engine)
				.FindMutableBattler(BattlerId);
		if (Battler == nullptr)
		{
			return false;
		}
		Battler->CurrentHP = 0;
		Battler->bFainted = true;
		Battler->bFaintTransitionPending = false;
		return true;
	}

	bool TryClearTargetActivePosition(
		FBattleEngine& Engine,
		const FActiveSlotId ActiveSlotId)
	{
		FBattleActivePositionState* Position =
			FBattleC09BWildFlowEngineFixture::GetMutableState(Engine)
				.ActivePositions.FindByPredicate(
					[ActiveSlotId](const FBattleActivePositionState& Candidate)
					{
						return Candidate.ActiveSlotId == ActiveSlotId;
					});
		if (Position == nullptr)
		{
			return false;
		}
		Position->TrainerId = FTrainerId();
		Position->BattlerId = FBattlerId();
		return true;
	}

	bool TrySeedChargedReleaseTargetCheckpoint(
		FBattleEngine& Engine,
		const FMoveId ChargeMoveId)
	{
		const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
		FBattleBattlerState* Actor =
			FBattleC09BWildFlowEngineFixture::GetMutableState(Engine)
				.FindMutableBattler(ActorId);
		FBattleMoveSlotState* Slot = Actor != nullptr
			? Actor->Moves.FindByPredicate(
				[ChargeMoveId](const FBattleMoveSlotState& Candidate)
				{
					return Candidate.MoveId == ChargeMoveId;
				})
			: nullptr;
		if (Actor == nullptr || Slot == nullptr)
		{
			return false;
		}
		Slot->CurrentPP = 19;
		Actor->LastMoveId = ChargeMoveId;
		return TrySeedActionStartVolatile(
				Engine,
				ActorId,
				FBattleVolatileRules::GetChargingId(),
				ChargeMoveId.GetDefinitionId())
			&& TrySeedActionStartVolatile(
				Engine,
				ActorId,
				FBattleVolatileRules::GetFlySemiInvulnerableId())
			&& TryPrepareTargetCheckpoint(Engine, ChargeMoveId);
	}

	bool IsTargetCheckpointSuccessEvent(const FBattleEvent& Event)
	{
		switch (Event.GetType())
		{
		case EBattleEventType::TargetsResolved:
		case EBattleEventType::ActionCompleted:
		case EBattleEventType::ReplacementRequired:
		case EBattleEventType::BattleEnded:
			return true;
		case EBattleEventType::ActionCanceled:
			return Event.GetCause() == EBattleEventCause::Targeting;
		default:
			return false;
		}
	}

	bool HasExactTargetEventOrder(
		const FBattleResolution& Resolution,
		const TConstArrayView<EBattleEventType> Expected)
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

	struct FTargetCheckpointBattlerObservation
	{
		FAtomicSwitchBattlerObservation Facts;
		bool bEgg = false;
		TArray<FMoveId> MoveIds;
		TArray<int32> CurrentPP;
		TArray<int32> MaximumPP;
	};

	FTargetCheckpointBattlerObservation ObserveTargetCheckpointBattler(
		const FBattleEngineState& State,
		const FBattleBattlerState& Battler)
	{
		FTargetCheckpointBattlerObservation Observation;
		Observation.Facts = ObserveAtomicSwitchBattler(State, Battler.BattlerId);
		Observation.bEgg = Battler.bEgg;
		for (const FBattleMoveSlotState& Move : Battler.Moves)
		{
			Observation.MoveIds.Add(Move.MoveId);
			Observation.CurrentPP.Add(Move.CurrentPP);
			Observation.MaximumPP.Add(Move.MaxPP);
		}
		return Observation;
	}

	bool AreTargetCheckpointBattlersIdentical(
		const FTargetCheckpointBattlerObservation& Left,
		const FTargetCheckpointBattlerObservation& Right)
	{
		return AreAtomicSwitchBattlersIdentical(Left.Facts, Right.Facts)
			&& Left.bEgg == Right.bEgg
			&& Left.MoveIds == Right.MoveIds
			&& Left.CurrentPP == Right.CurrentPP
			&& Left.MaximumPP == Right.MaximumPP;
	}

	struct FTargetCheckpointObservation
	{
		FActionStartCheckpointObservation Action;
		FAtomicSwitchCheckpointObservation Mechanics;
		bool bHasCurrentAction = false;
		FBattleLockedActionState CurrentAction;
		TArray<FTargetCheckpointBattlerObservation> Battlers;
		TArray<FBattleRandomDraw> RandomTrace;
		TOptional<FBattleDecisionRequest> PendingDecision;
		TArray<FBattleDecisionRequest> PendingRequests;
		TArray<FBattlePendingReplacementState> PendingReplacements;
		int32 TargetSuccessEventCount = 0;
	};

	FTargetCheckpointObservation ObserveTargetCheckpoint(
		const FBattleEngine& Engine)
	{
		const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
		const FTrainerId TrainerId = MakeNumericId<FTrainerId>(PlayerTrainerValue);
		const FBattleEngineState& State =
			FBattleC09BWildFlowEngineFixture::GetState(Engine);
		FTargetCheckpointObservation Observation;
		Observation.Action = ObserveActionStartCheckpoint(Engine, ActorId, TrainerId);
		Observation.Mechanics = ObserveAtomicSwitchCheckpoint(Engine);
		if (State.LockedActions.IsValidIndex(State.CurrentLockedActionIndex))
		{
			Observation.bHasCurrentAction = true;
			Observation.CurrentAction = State.LockedActions[State.CurrentLockedActionIndex];
		}
		for (const FBattleBattlerState& Battler : State.Battlers)
		{
			Observation.Battlers.Add(ObserveTargetCheckpointBattler(State, Battler));
		}
		for (const FBattleRandomDraw& Draw : State.Random->GetTrace())
		{
			Observation.RandomTrace.Add(Draw);
		}
		Observation.PendingDecision = State.PendingDecision;
		Observation.PendingRequests = State.PendingDecisionRequests;
		Observation.PendingReplacements = State.PendingReplacements;
		for (const FBattleEvent& Event : State.OrderedEvents)
		{
			Observation.TargetSuccessEventCount +=
				IsTargetCheckpointSuccessEvent(Event) ? 1 : 0;
		}
		return Observation;
	}

	bool AreTargetPendingRequestsIdentical(
		const TConstArrayView<FBattleDecisionRequest> Left,
		const TConstArrayView<FBattleDecisionRequest> Right)
	{
		if (Left.Num() != Right.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < Left.Num(); ++Index)
		{
			if (!ArePivotTestRequestsIdentical(Left[Index], Right[Index]))
			{
				return false;
			}
		}
		return true;
	}

	bool AreTargetPendingReplacementsIdentical(
		const TConstArrayView<FBattlePendingReplacementState> Left,
		const TConstArrayView<FBattlePendingReplacementState> Right)
	{
		if (Left.Num() != Right.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < Left.Num(); ++Index)
		{
			if (Left[Index].TrainerId != Right[Index].TrainerId
				|| Left[Index].ActiveSlotId != Right[Index].ActiveSlotId)
			{
				return false;
			}
		}
		return true;
	}

	bool AreTargetCheckpointGameplayFactsIdentical(
		const FTargetCheckpointObservation& Left,
		const FTargetCheckpointObservation& Right)
	{
		if (!AreAtomicSwitchMechanicsIdentical(Left.Mechanics, Right.Mechanics)
			|| Left.bHasCurrentAction != Right.bHasCurrentAction
			|| (Left.bHasCurrentAction
				&& !ArePivotTestLockedActionsIdentical(
					Left.CurrentAction,
					Right.CurrentAction))
			|| Left.Battlers.Num() != Right.Battlers.Num()
			|| Left.RandomTrace != Right.RandomTrace
			|| Left.PendingDecision.IsSet() != Right.PendingDecision.IsSet()
			|| (Left.PendingDecision.IsSet()
				&& !ArePivotTestRequestsIdentical(
					Left.PendingDecision.GetValue(),
					Right.PendingDecision.GetValue()))
			|| !AreTargetPendingRequestsIdentical(
				Left.PendingRequests,
				Right.PendingRequests)
			|| !AreTargetPendingReplacementsIdentical(
				Left.PendingReplacements,
				Right.PendingReplacements)
			|| Left.TargetSuccessEventCount != Right.TargetSuccessEventCount)
		{
			return false;
		}
		for (int32 Index = 0; Index < Left.Battlers.Num(); ++Index)
		{
			if (!AreTargetCheckpointBattlersIdentical(
					Left.Battlers[Index],
					Right.Battlers[Index]))
			{
				return false;
			}
		}
		return true;
	}

	bool VerifyRejectedTargetEnvelope(
		FAutomationTestBase& Test,
		const FBattleEngine& Engine,
		const FTargetCheckpointObservation& Before,
		const EBattleRejectionReason ExpectedReason,
		const FBattleResolution& Returned)
	{
		const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
		const FTrainerId TrainerId = MakeNumericId<FTrainerId>(PlayerTrainerValue);
		bool bValid = VerifyRejectedActionStartCheckpoint(
			Test,
			Engine,
			ActorId,
			TrainerId,
			Before.Action,
			Before.Action.StateVersion,
			ExpectedReason,
			Returned);
		const FTargetCheckpointObservation After = ObserveTargetCheckpoint(Engine);
		bValid &= Test.TestTrue(
			TEXT("Rejected target checkpoint publishes one rule cancellation only"),
			Returned.GetEvents().Num() == 1
				&& Returned.GetEvents()[0].GetType()
					== EBattleEventType::ActionCanceled
				&& Returned.GetEvents()[0].GetCause() == EBattleEventCause::Rule);
		bValid &= Test.TestFalse(
			TEXT("Rejected target checkpoint publishes no 3E5 success fact"),
			Returned.GetEvents().ContainsByPredicate(
				[](const FBattleEvent& Event)
				{
					return IsTargetCheckpointSuccessEvent(Event);
				}));
		bValid &= Test.TestEqual(
			TEXT("Rejected target checkpoint preserves prior success-event history"),
			After.TargetSuccessEventCount,
			Before.TargetSuccessEventCount);
		const FBattleReplayRecord Replay = Engine.ExportReplayRecord();
		bValid &= Test.TestEqual(
			TEXT("Rejected target checkpoint replay remains schema 6"),
			Replay.GetSchemaVersion(),
			static_cast<uint32>(6));
		bValid &= Test.TestTrue(
			TEXT("Rejected target checkpoint replay appends the same rejection once"),
			!Replay.GetResolutions().IsEmpty()
				&& Replay.GetResolutions().Last().GetResolutionId()
					== Returned.GetResolutionId()
				&& Replay.GetResolutions().Last().GetEvents().Num() == 1
				&& !IsTargetCheckpointSuccessEvent(
					Replay.GetResolutions().Last().GetEvents()[0]));
		return bValid;
	}

	bool VerifyRejectedTargetCheckpoint(
		FAutomationTestBase& Test,
		const FBattleEngine& Engine,
		const FTargetCheckpointObservation& Before,
		const EBattleRejectionReason ExpectedReason,
		const FBattleResolution& Returned)
	{
		bool bValid = VerifyRejectedTargetEnvelope(
			Test,
			Engine,
			Before,
			ExpectedReason,
			Returned);
		bValid &= Test.TestTrue(
			TEXT("Rejected target checkpoint preserves action, PP, battlers, positions, charge, triggers, cursor, pending facts and parent RNG"),
			AreTargetCheckpointGameplayFactsIdentical(
				ObserveTargetCheckpoint(Engine),
				Before));
		return bValid;
	}

	bool IsPreMoveSuccessEvent(const EBattleEventType Type)
	{
		switch (Type)
		{
		case EBattleEventType::PPConsumed:
		case EBattleEventType::MoveUsed:
		case EBattleEventType::RandomCheck:
		case EBattleEventType::StatusChanged:
		case EBattleEventType::Damage:
		case EBattleEventType::HPChanged:
		case EBattleEventType::Fainted:
		case EBattleEventType::LeftActiveSlot:
		case EBattleEventType::Removed:
		case EBattleEventType::AbilityActivated:
		case EBattleEventType::ItemActivated:
		case EBattleEventType::ItemConsumed:
		case EBattleEventType::ActionCompleted:
		case EBattleEventType::BattleEnded:
		case EBattleEventType::ReplacementRequired:
			return true;
		default:
			return false;
		}
	}

	bool VerifyRejectedPreMoveCheckpoint(
		FAutomationTestBase& Test,
		const FBattleEngine& Engine,
		const FActionStartCheckpointObservation& BeforeAction,
		const FAtomicSwitchCheckpointObservation& BeforeMechanics,
		const EBattleRejectionReason ExpectedReason,
		const FBattleResolution& Returned)
	{
		const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
		const FTrainerId TrainerId = MakeNumericId<FTrainerId>(PlayerTrainerValue);
		bool bValid = VerifyRejectedActionStartCheckpoint(
			Test,
			Engine,
			ActorId,
			TrainerId,
			BeforeAction,
			BeforeAction.StateVersion,
			ExpectedReason,
			Returned);
		const FAtomicSwitchCheckpointObservation AfterMechanics =
			ObserveAtomicSwitchCheckpoint(Engine);
		bValid &= Test.TestTrue(
			TEXT("Rejected pre-move checkpoint preserves actor, target, conditions, triggers, item and cursor facts"),
			AreAtomicSwitchMechanicsIdentical(
				AfterMechanics,
				BeforeMechanics));
		bValid &= Test.TestFalse(
			TEXT("Rejected pre-move checkpoint publishes no checkpoint success fact"),
			Returned.GetEvents().ContainsByPredicate(
				[](const FBattleEvent& Event)
				{
					return IsPreMoveSuccessEvent(Event.GetType());
				}));
		const FBattleReplayRecord Replay = Engine.ExportReplayRecord();
		bValid &= Test.TestEqual(
			TEXT("Rejected pre-move replay schema remains 6"),
			Replay.GetSchemaVersion(),
			static_cast<uint32>(6));
		bValid &= Test.TestTrue(
			TEXT("Rejected pre-move replay contains the same rejection exactly once"),
			!Replay.GetResolutions().IsEmpty()
				&& Replay.GetResolutions().Last().GetResolutionId()
					== Returned.GetResolutionId()
				&& Replay.GetResolutions().Last().GetRejection().Reason
					== ExpectedReason
				&& Replay.GetResolutions().Last().GetEvents().Num() == 1
				&& !IsPreMoveSuccessEvent(
					Replay.GetResolutions().Last().GetEvents()[0].GetType()));
		return bValid;
	}

	bool TryReplaceAtomicPivotPendingRequest(
		FBattleEngine& Engine,
		const FPartySlotId PartySlotId)
	{
		FBattleEngineState& State =
			FBattleC09BWildFlowEngineFixture::GetMutableState(Engine);
		if (!State.PendingDecision.IsSet() || State.PendingDecisionRequests.Num() != 1)
		{
			return false;
		}
		FBattleDecisionRequest Replacement;
		if (!TryCopyPivotRequestWithSwitchSlot(
				State.PendingDecision.GetValue(),
				PartySlotId,
				Replacement))
		{
			return false;
		}
		State.PendingDecision = Replacement;
		State.PendingDecisionRequests[0] = MoveTemp(Replacement);
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023D1RunAtomicCommitTest,
	"PokemonSolarus.Battle.ADR0002.3D1.WildActions.Run.SuccessFailureAndNoDraw",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023D1RunAtomicCommitTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FAtomicWildScenario Scenario;

	TUniquePtr<FBattleEngine> Failed;
	if (!TestTrue(TEXT("Failed-Run engine is created"),
		TryMakeSequenceEngine(Scenario, {200}, Failed))
		|| !TestTrue(TEXT("Failed Run turn locks"),
			LockTurn(*Failed, PlayerLeftValue, EBattleActionKind::Run))
		|| !TestTrue(TEXT("Failed Run starts"),
			BeginExpectedWildAction(*Failed, PlayerLeftValue, EBattleActionKind::Run)))
	{
		return false;
	}
	const int32 FailedResolutionCountBefore =
		FBattleC09BWildFlowEngineFixture::GetState(*Failed).Resolutions.Num();
	const FBattleResolution FailedResolution = Failed->ExecuteCurrentWildAction();
	TestTrue(TEXT("Legal failed Run is accepted"), FailedResolution.WasAccepted());
	TestEqual(TEXT("Failed Run increments the escape counter"),
		Failed->GetSnapshot().GetEscapeAttemptCount(), 2U);
	TestEqual(TEXT("Failed Run commits one parent draw"),
		FBattleC09BWildFlowEngineFixture::GetState(*Failed).Random->GetTrace().Num(), 1);
	TestTrue(TEXT("Failed Run preserves exact events"),
		HasExactEventOrder(FailedResolution, {
			EBattleEventType::RunAttempted,
			EBattleEventType::ActionCompleted}));
	TestEqual(TEXT("Failed Run appends one resolution"),
		FBattleC09BWildFlowEngineFixture::GetState(*Failed).Resolutions.Num(),
		FailedResolutionCountBefore + 1);
	TestTrue(TEXT("Failed Run returns the appended resolution"),
		IsReturnedResolutionAppendedExactlyOnce(*Failed, FailedResolution));

	TUniquePtr<FBattleEngine> Succeeded;
	if (!TestTrue(TEXT("Successful-Run engine is created"),
		TryMakeSequenceEngine(Scenario, {0}, Succeeded))
		|| !TestTrue(TEXT("Successful Run turn locks"),
			LockTurn(*Succeeded, PlayerLeftValue, EBattleActionKind::Run))
		|| !TestTrue(TEXT("Successful Run starts"),
			BeginExpectedWildAction(*Succeeded, PlayerLeftValue, EBattleActionKind::Run)))
	{
		return false;
	}
	const FBattleResolution SuccessResolution = Succeeded->ExecuteCurrentWildAction();
	TestTrue(TEXT("Legal successful Run is accepted"), SuccessResolution.WasAccepted());
	TestEqual(TEXT("Successful Run reaches terminal Escape"),
		Succeeded->GetSnapshot().GetOutcome(), EBattleOutcome::Escape);
	TestEqual(TEXT("Successful Run keeps ordinary cause"),
		Succeeded->GetSnapshot().GetOutcomeCause(), EBattleOutcomeCause::Ordinary);
	TestTrue(TEXT("Successful Run preserves exact events"),
		HasExactEventOrder(SuccessResolution, {
			EBattleEventType::RunAttempted,
			EBattleEventType::Escaped,
			EBattleEventType::ActionCompleted,
			EBattleEventType::BattleEnded}));
	TestTrue(TEXT("Successful Run returns the appended resolution"),
		IsReturnedResolutionAppendedExactlyOnce(*Succeeded, SuccessResolution));

	FAtomicWildScenario GuaranteedScenario;
	GuaranteedScenario.PlayerLeftSpeed = 100;
	GuaranteedScenario.OpponentLeftSpeed = 4;
	TUniquePtr<FBattleEngine> Guaranteed;
	if (!TestTrue(TEXT("Guaranteed-Run engine is created"),
		TryMakeSequenceEngine(GuaranteedScenario, {}, Guaranteed))
		|| !TestTrue(TEXT("Guaranteed Run turn locks"),
			LockTurn(*Guaranteed, PlayerLeftValue, EBattleActionKind::Run))
		|| !TestTrue(TEXT("Guaranteed Run starts"),
			BeginExpectedWildAction(*Guaranteed, PlayerLeftValue, EBattleActionKind::Run)))
	{
		return false;
	}
	const FBattleResolution GuaranteedResolution = Guaranteed->ExecuteCurrentWildAction();
	TestTrue(TEXT("F greater than 255 succeeds"), GuaranteedResolution.WasAccepted());
	TestEqual(TEXT("Guaranteed Run publishes no parent draw"),
		FBattleC09BWildFlowEngineFixture::GetState(*Guaranteed).Random->GetTrace().Num(), 0);
	TestEqual(TEXT("Guaranteed Run remains ordinary Escape"),
		Guaranteed->GetSnapshot().GetOutcomeCause(), EBattleOutcomeCause::Ordinary);
	TestEqual(TEXT("Run replay schema remains 6"),
		Guaranteed->ExportReplayRecord().GetSchemaVersion(), 6U);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023D1WildFleeModesAtomicCommitTest,
	"PokemonSolarus.Battle.ADR0002.3D1.WildActions.WildFlee.DeterministicAndChance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023D1WildFleeModesAtomicCommitTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	for (const EBattleWildFleeMode Mode : {
		EBattleWildFleeMode::Never,
		EBattleWildFleeMode::Always})
	{
		FAtomicWildScenario Scenario;
		Scenario.WildFleeMode = Mode;
		TUniquePtr<FBattleEngine> Engine;
		if (!TestTrue(TEXT("Deterministic WildFlee engine is created"),
			TryMakeSequenceEngine(Scenario, {}, Engine))
			|| !TestTrue(TEXT("Deterministic WildFlee turn locks"),
				LockTurn(*Engine, OpponentLeftValue, EBattleActionKind::WildFlee))
			|| !TestTrue(TEXT("Deterministic WildFlee starts"),
				BeginExpectedWildAction(*Engine, OpponentLeftValue, EBattleActionKind::WildFlee)))
		{
			return false;
		}
		const FBattleResolution Resolution = Engine->ExecuteCurrentWildAction();
		TestTrue(TEXT("Deterministic WildFlee checkpoint is accepted"), Resolution.WasAccepted());
		TestEqual(TEXT("Deterministic WildFlee publishes no parent draw"),
			FBattleC09BWildFlowEngineFixture::GetState(*Engine).Random->GetTrace().Num(), 0);
		TestEqual(TEXT("Never leaves the actor active; Always removes it"),
			FBattleC09BWildFlowEngineFixture::IsRemoved(
				*Engine,
				MakeNumericId<FBattlerId>(OpponentLeftValue)),
			Mode == EBattleWildFleeMode::Always);
		TestTrue(TEXT("Deterministic result returns the appended resolution"),
			IsReturnedResolutionAppendedExactlyOnce(*Engine, Resolution));
	}

	for (const uint32 Draw : {1U, 0U})
	{
		FAtomicWildScenario Scenario;
		Scenario.WildFleeMode = EBattleWildFleeMode::Chance;
		Scenario.WildFleeNumerator = 1;
		Scenario.WildFleeDenominator = 2;
		TUniquePtr<FBattleEngine> Engine;
		if (!TestTrue(TEXT("Chance WildFlee engine is created"),
			TryMakeSequenceEngine(Scenario, {Draw}, Engine))
			|| !TestTrue(TEXT("Chance WildFlee turn locks"),
				LockTurn(*Engine, OpponentLeftValue, EBattleActionKind::WildFlee))
			|| !TestTrue(TEXT("Chance WildFlee starts"),
				BeginExpectedWildAction(*Engine, OpponentLeftValue, EBattleActionKind::WildFlee)))
		{
			return false;
		}
		const FBattleResolution Resolution = Engine->ExecuteCurrentWildAction();
		TestTrue(TEXT("Chance WildFlee checkpoint is accepted"), Resolution.WasAccepted());
		TestEqual(TEXT("Chance WildFlee commits exactly one parent draw"),
			FBattleC09BWildFlowEngineFixture::GetState(*Engine).Random->GetTrace().Num(), 1);
		TestEqual(TEXT("Chance outcome follows draw below numerator"),
			FBattleC09BWildFlowEngineFixture::IsRemoved(
				*Engine,
				MakeNumericId<FBattlerId>(OpponentLeftValue)),
			Draw == 0);
		TestTrue(TEXT("Chance result returns the appended resolution"),
			IsReturnedResolutionAppendedExactlyOnce(*Engine, Resolution));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023D1MultiWildQueueContinuationTest,
	"PokemonSolarus.Battle.ADR0002.3D1.WildActions.WildFlee.MultiActiveQueueContinuation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023D1MultiWildQueueContinuationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FAtomicWildScenario Scenario;
	Scenario.Format = EBattleFormat::Double;
	Scenario.WildFleeMode = EBattleWildFleeMode::Always;
	TUniquePtr<FBattleEngine> Engine;
	if (!TestTrue(TEXT("Multi-Wild engine is created"),
		TryMakeSequenceEngine(Scenario, {}, Engine))
		|| !TestTrue(TEXT("Multi-Wild turn locks"),
			LockTurn(*Engine, OpponentLeftValue, EBattleActionKind::WildFlee))
		|| !TestTrue(TEXT("WildFlee is the first locked action"),
			BeginExpectedWildAction(*Engine, OpponentLeftValue, EBattleActionKind::WildFlee)))
	{
		return false;
	}

	const FBattleResolution Resolution = Engine->ExecuteCurrentWildAction();
	TestTrue(TEXT("One active Wild may flee"), Resolution.WasAccepted());
	TestTrue(TEXT("Only the fleeing Wild is removed"),
		FBattleC09BWildFlowEngineFixture::IsRemoved(
			*Engine,
			MakeNumericId<FBattlerId>(OpponentLeftValue)));
	TestFalse(TEXT("The remaining active Wild is not removed"),
		FBattleC09BWildFlowEngineFixture::IsRemoved(
			*Engine,
			MakeNumericId<FBattlerId>(OpponentRightValue)));
	TestTrue(TEXT("The remaining Wild stays active"),
		FBattleC09BWildFlowEngineFixture::IsActive(
			*Engine,
			MakeNumericId<FBattlerId>(OpponentRightValue)));
	TestEqual(TEXT("Battle remains in progress"),
		Engine->GetSnapshot().GetOutcome(), EBattleOutcome::InProgress);
	TestTrue(TEXT("Multi-Wild flee preserves exact event order"),
		HasExactEventOrder(Resolution, {
			EBattleEventType::RunAttempted,
			EBattleEventType::Escaped,
			EBattleEventType::LeftActiveSlot,
			EBattleEventType::Removed,
			EBattleEventType::OpponentRemovalCheckpoint,
			EBattleEventType::ActionCompleted}));
	TestTrue(TEXT("Remaining locked actions execute to queue boundary"),
		ExecuteRemainingQueue(*Engine));
	TestEqual(TEXT("Queue reaches EndOfTurn without Wild replacement"),
		Engine->GetSnapshot().GetPhase(), EBattlePhase::EndOfTurn);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023D1PreparationFailureTest,
	"PokemonSolarus.Battle.ADR0002.3D1.WildActions.Failure.Preparation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023D1PreparationFailureTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FAtomicWildScenario RunScenario;
	RunScenario.PlayerLeftSpeed = 100;
	RunScenario.OpponentLeftSpeed = 4;
	TUniquePtr<FBattleEngine> RunEngine;
	if (!TestTrue(TEXT("Run cleanup-failure engine is created"),
		TryMakeSequenceEngine(RunScenario, {}, RunEngine))
		|| !TestTrue(TEXT("Run cleanup-failure turn locks"),
			LockTurn(*RunEngine, PlayerLeftValue, EBattleActionKind::Run))
		|| !TestTrue(TEXT("Run cleanup-failure action starts"),
			BeginExpectedWildAction(*RunEngine, PlayerLeftValue, EBattleActionKind::Run)))
	{
		return false;
	}
	FBattleC09BWildFlowEngineFixture::SetNextTriggerReentrancyToken(
		*RunEngine,
		TNumericLimits<uint64>::Max());
	const FCheckpointObservation RunBefore = ObserveCheckpoint(
		*RunEngine,
		MakeNumericId<FBattlerId>(PlayerLeftValue));
	const FBattleResolution RunRejected = RunEngine->ExecuteCurrentWildAction();
	VerifyRejectedCheckpoint(
		*this,
		*RunEngine,
		MakeNumericId<FBattlerId>(PlayerLeftValue),
		RunBefore,
		RunBefore.StateVersion,
		EBattleRejectionReason::CheckpointPreparationFailed,
		RunRejected);

	FAtomicWildScenario FleeScenario;
	FleeScenario.WildFleeMode = EBattleWildFleeMode::Always;
	TUniquePtr<FBattleEngine> FleeEngine;
	if (!TestTrue(TEXT("WildFlee cleanup-failure engine is created"),
		TryMakeSequenceEngine(FleeScenario, {}, FleeEngine))
		|| !TestTrue(TEXT("WildFlee cleanup-failure turn locks"),
			LockTurn(*FleeEngine, OpponentLeftValue, EBattleActionKind::WildFlee))
		|| !TestTrue(TEXT("WildFlee cleanup-failure action starts"),
			BeginExpectedWildAction(*FleeEngine, OpponentLeftValue, EBattleActionKind::WildFlee)))
	{
		return false;
	}
	FBattleC09BWildFlowEngineFixture::SetNextTriggerReentrancyToken(
		*FleeEngine,
		TNumericLimits<uint64>::Max());
	const FCheckpointObservation FleeBefore = ObserveCheckpoint(
		*FleeEngine,
		MakeNumericId<FBattlerId>(OpponentLeftValue));
	const FBattleResolution FleeRejected = FleeEngine->ExecuteCurrentWildAction();
	VerifyRejectedCheckpoint(
		*this,
		*FleeEngine,
		MakeNumericId<FBattlerId>(OpponentLeftValue),
		FleeBefore,
		FleeBefore.StateVersion,
		EBattleRejectionReason::CheckpointPreparationFailed,
		FleeRejected);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023D1RandomStageFailureTest,
	"PokemonSolarus.Battle.ADR0002.3D1.WildActions.Failure.RandomStage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023D1RandomStageFailureTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FAtomicWildScenario RunScenario;
	TUniquePtr<FBattleEngine> CreateEngine;
	FFaultBattleRandom* CreateRandom = nullptr;
	if (!TestTrue(TEXT("Transaction-create failure engine is created"),
		TryMakeFaultEngine(
			RunScenario,
			{0},
			EFaultRandomMode::CreateTransaction,
			CreateEngine,
			CreateRandom))
		|| !TestTrue(TEXT("Transaction-create failure turn locks"),
			LockTurn(*CreateEngine, PlayerLeftValue, EBattleActionKind::Run))
		|| !TestTrue(TEXT("Transaction-create failure action starts"),
			BeginExpectedWildAction(*CreateEngine, PlayerLeftValue, EBattleActionKind::Run)))
	{
		return false;
	}
	check(CreateRandom != nullptr);
	const FCheckpointObservation CreateBefore = ObserveCheckpoint(
		*CreateEngine,
		MakeNumericId<FBattlerId>(PlayerLeftValue));
	const FBattleResolution CreateRejected = CreateEngine->ExecuteCurrentWildAction();
	VerifyRejectedCheckpoint(
		*this,
		*CreateEngine,
		MakeNumericId<FBattlerId>(PlayerLeftValue),
		CreateBefore,
		CreateBefore.StateVersion,
		EBattleRejectionReason::CheckpointRandomStageFailed,
		CreateRejected);

	FAtomicWildScenario ChanceScenario;
	ChanceScenario.WildFleeMode = EBattleWildFleeMode::Chance;
	ChanceScenario.WildFleeNumerator = 1;
	ChanceScenario.WildFleeDenominator = 2;
	TUniquePtr<FBattleEngine> DrawEngine;
	FFaultBattleRandom* DrawRandom = nullptr;
	if (!TestTrue(TEXT("Staged-draw failure engine is created"),
		TryMakeFaultEngine(
			ChanceScenario,
			{0},
			EFaultRandomMode::Draw,
			DrawEngine,
			DrawRandom))
		|| !TestTrue(TEXT("Staged-draw failure turn locks"),
			LockTurn(*DrawEngine, OpponentLeftValue, EBattleActionKind::WildFlee))
		|| !TestTrue(TEXT("Staged-draw failure action starts"),
			BeginExpectedWildAction(*DrawEngine, OpponentLeftValue, EBattleActionKind::WildFlee)))
	{
		return false;
	}
	check(DrawRandom != nullptr);
	const FCheckpointObservation DrawBefore = ObserveCheckpoint(
		*DrawEngine,
		MakeNumericId<FBattlerId>(OpponentLeftValue));
	const FBattleResolution DrawRejected = DrawEngine->ExecuteCurrentWildAction();
	VerifyRejectedCheckpoint(
		*this,
		*DrawEngine,
		MakeNumericId<FBattlerId>(OpponentLeftValue),
		DrawBefore,
		DrawBefore.StateVersion,
		EBattleRejectionReason::CheckpointRandomStageFailed,
		DrawRejected);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023D1StaleIdentityFailureTest,
	"PokemonSolarus.Battle.ADR0002.3D1.WildActions.Failure.StaleIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023D1StaleIdentityFailureTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FAtomicWildScenario Scenario;
	TUniquePtr<FBattleEngine> Engine;
	FFaultBattleRandom* Random = nullptr;
	if (!TestTrue(TEXT("Stale-identity engine is created"),
		TryMakeFaultEngine(
			Scenario,
			{0},
			EFaultRandomMode::StaleAfterDraw,
			Engine,
			Random))
		|| !TestTrue(TEXT("Stale-identity turn locks"),
			LockTurn(*Engine, PlayerLeftValue, EBattleActionKind::Run))
		|| !TestTrue(TEXT("Stale-identity action starts"),
			BeginExpectedWildAction(*Engine, PlayerLeftValue, EBattleActionKind::Run)))
	{
		return false;
	}
	check(Random != nullptr);
	Random->SetAfterDraw([EnginePtr = Engine.Get()]()
	{
		FBattleC09BWildFlowEngineFixture::AdvanceStateVersion(*EnginePtr);
	});
	const FCheckpointObservation Before = ObserveCheckpoint(
		*Engine,
		MakeNumericId<FBattlerId>(PlayerLeftValue));
	const FBattleResolution Rejected = Engine->ExecuteCurrentWildAction();
	VerifyRejectedCheckpoint(
		*this,
		*Engine,
		MakeNumericId<FBattlerId>(PlayerLeftValue),
		Before,
		Before.StateVersion + 1,
		EBattleRejectionReason::StaleCheckpointIdentity,
		Rejected);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023D1RandomCommitFailureTest,
	"PokemonSolarus.Battle.ADR0002.3D1.WildActions.Failure.RandomCommit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023D1RandomCommitFailureTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FAtomicWildScenario Scenario;
	TUniquePtr<FBattleEngine> Engine;
	FFaultBattleRandom* Random = nullptr;
	if (!TestTrue(TEXT("Random-commit failure engine is created"),
		TryMakeFaultEngine(
			Scenario,
			{0},
			EFaultRandomMode::Commit,
			Engine,
			Random))
		|| !TestTrue(TEXT("Random-commit failure turn locks"),
			LockTurn(*Engine, PlayerLeftValue, EBattleActionKind::Run))
		|| !TestTrue(TEXT("Random-commit failure action starts"),
			BeginExpectedWildAction(*Engine, PlayerLeftValue, EBattleActionKind::Run)))
	{
		return false;
	}
	check(Random != nullptr);
	const FCheckpointObservation Before = ObserveCheckpoint(
		*Engine,
		MakeNumericId<FBattlerId>(PlayerLeftValue));
	const FBattleResolution Rejected = Engine->ExecuteCurrentWildAction();
	VerifyRejectedCheckpoint(
		*this,
		*Engine,
		MakeNumericId<FBattlerId>(PlayerLeftValue),
		Before,
		Before.StateVersion,
		EBattleRejectionReason::RandomTransactionCommitFailed,
		Rejected);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023D3CaptureZeroIndicatorTest,
	"PokemonSolarus.Battle.ADR0002.3D3.Capture.Preparation.ZeroIndicatorBeforeTransaction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023D3CaptureZeroIndicatorTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FAtomicWildScenario Scenario = MakeAtomicCaptureScenario();
	Scenario.CatchRate = 1;
	Scenario.CaptureProgression.CaptureCoefficientQ12 = 1;
	TUniquePtr<FBattleEngine> Engine;
	FFaultBattleRandom* Random = nullptr;
	if (!TestTrue(TEXT("Zero-indicator engine is created"),
		TryMakeFaultEngine(
			Scenario,
			{},
			EFaultRandomMode::Commit,
			Engine,
			Random))
		|| !TestTrue(TEXT("Zero-indicator Capture turn locks"),
			LockTurn(*Engine, PlayerLeftValue, EBattleActionKind::Bag))
		|| !TestTrue(TEXT("Zero-indicator Capture starts"),
			BeginExpectedWildAction(*Engine, PlayerLeftValue, EBattleActionKind::Bag)))
	{
		return false;
	}

	check(Random != nullptr);
	const FBattlerId TargetId = MakeNumericId<FBattlerId>(OpponentLeftValue);
	const FCheckpointObservation Before = ObserveCheckpoint(*Engine, TargetId);
	const FBattleResolution Rejected = Engine->ExecuteCurrentBagItem();
	VerifyRejectedCheckpoint(
		*this,
		*Engine,
		TargetId,
		Before,
		Before.StateVersion,
		EBattleRejectionReason::CheckpointPreparationFailed,
		Rejected);
	TestEqual(TEXT("Zero indicator creates no RNG transaction"),
		Random->GetCounters().TransactionCreateAttempts, 0);
	TestEqual(TEXT("Zero indicator attempts no RNG draw"),
		Random->GetCounters().DrawAttempts, 0);
	TestEqual(TEXT("Zero indicator attempts no RNG commit"),
		Random->GetCounters().CommitAttempts, 0);
	TestFalse(TEXT("Zero-indicator rejection publishes no item success fact"),
		HasEvent(Rejected, EBattleEventType::ItemUsed));
	TestFalse(TEXT("Zero-indicator rejection publishes no CaptureAttempted fact"),
		HasEvent(Rejected, EBattleEventType::CaptureAttempted));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023D3CaptureRandomRollbackTest,
	"PokemonSolarus.Battle.ADR0002.3D3.Capture.Failure.RandomCreationCriticalShakeRollback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023D3CaptureRandomRollbackTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FBattlerId TargetId = MakeNumericId<FBattlerId>(OpponentLeftValue);

	FAtomicWildScenario CreateScenario = MakeAtomicCaptureScenario();
	TUniquePtr<FBattleEngine> CreateEngine;
	FFaultBattleRandom* CreateRandom = nullptr;
	if (!TestTrue(TEXT("Capture transaction-create failure engine is created"),
		TryMakeFaultEngine(
			CreateScenario,
			{0, 0, 0, 0},
			EFaultRandomMode::CreateTransaction,
			CreateEngine,
			CreateRandom))
		|| !TestTrue(TEXT("Capture transaction-create failure turn locks"),
			LockTurn(*CreateEngine, PlayerLeftValue, EBattleActionKind::Bag))
		|| !TestTrue(TEXT("Capture transaction-create failure starts"),
			BeginExpectedWildAction(
				*CreateEngine,
				PlayerLeftValue,
				EBattleActionKind::Bag)))
	{
		return false;
	}
	check(CreateRandom != nullptr);
	const FCheckpointObservation CreateBefore = ObserveCheckpoint(*CreateEngine, TargetId);
	const FBattleResolution CreateRejected = CreateEngine->ExecuteCurrentBagItem();
	VerifyRejectedCheckpoint(
		*this,
		*CreateEngine,
		TargetId,
		CreateBefore,
		CreateBefore.StateVersion,
		EBattleRejectionReason::CheckpointRandomStageFailed,
		CreateRejected);
	TestEqual(TEXT("Capture transaction creation is attempted once"),
		CreateRandom->GetCounters().TransactionCreateAttempts, 1);
	TestEqual(TEXT("Failed transaction creation performs no draw"),
		CreateRandom->GetCounters().DrawAttempts, 0);

	FAtomicWildScenario DrawScenario = MakeAtomicCaptureScenario();
	DrawScenario.CatchRate = 120;
	DrawScenario.TargetCurrentHP = 10;
	DrawScenario.CaptureProgression.CaughtSpeciesCount = 451;
	DrawScenario.CaptureProgression.bCriticalCaptureEnabled = true;
	DrawScenario.CaptureProgression.bCatchingCharm = true;
	DrawScenario.CaptureProgression.bUseCaughtCountHPComponentModifier = true;
	TUniquePtr<FBattleEngine> DrawEngine;
	FFaultBattleRandom* DrawRandom = nullptr;
	if (!TestTrue(TEXT("Critical/shake rollback engine is created"),
		TryMakeFaultEngine(
			DrawScenario,
			{255, 0, 0, 0},
			EFaultRandomMode::Draw,
			DrawEngine,
			DrawRandom,
			2))
		|| !TestTrue(TEXT("Critical/shake rollback turn locks"),
			LockTurn(*DrawEngine, PlayerLeftValue, EBattleActionKind::Bag))
		|| !TestTrue(TEXT("Critical/shake rollback Capture starts"),
			BeginExpectedWildAction(
				*DrawEngine,
				PlayerLeftValue,
				EBattleActionKind::Bag)))
	{
		return false;
	}
	check(DrawRandom != nullptr);
	const FCheckpointObservation DrawBefore = ObserveCheckpoint(*DrawEngine, TargetId);
	const FBattleResolution DrawRejected = DrawEngine->ExecuteCurrentBagItem();
	VerifyRejectedCheckpoint(
		*this,
		*DrawEngine,
		TargetId,
		DrawBefore,
		DrawBefore.StateVersion,
		EBattleRejectionReason::CheckpointRandomStageFailed,
		DrawRejected);
	TestEqual(TEXT("Rollback staged one critical and one shake draw"),
		DrawRandom->GetCounters().SuccessfulDraws, 2);
	TestEqual(TEXT("A later shake failure is the third draw attempt"),
		DrawRandom->GetCounters().DrawAttempts, 3);
	TestEqual(TEXT("Random-stage rollback never attempts parent commit"),
		DrawRandom->GetCounters().CommitAttempts, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023D3CaptureIdentityCommitFailureTest,
	"PokemonSolarus.Battle.ADR0002.3D3.Capture.Failure.StaleIdentityAndRandomCommit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023D3CaptureIdentityCommitFailureTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FBattlerId TargetId = MakeNumericId<FBattlerId>(OpponentLeftValue);
	FAtomicWildScenario Scenario = MakeAtomicCaptureScenario();

	TUniquePtr<FBattleEngine> StaleEngine;
	FFaultBattleRandom* StaleRandom = nullptr;
	if (!TestTrue(TEXT("Stale Capture engine is created"),
		TryMakeFaultEngine(
			Scenario,
			{0, 0, 0, 0},
			EFaultRandomMode::StaleAfterDraw,
			StaleEngine,
			StaleRandom))
		|| !TestTrue(TEXT("Stale Capture turn locks"),
			LockTurn(*StaleEngine, PlayerLeftValue, EBattleActionKind::Bag))
		|| !TestTrue(TEXT("Stale Capture starts"),
			BeginExpectedWildAction(
				*StaleEngine,
				PlayerLeftValue,
				EBattleActionKind::Bag)))
	{
		return false;
	}
	check(StaleRandom != nullptr);
	StaleRandom->SetAfterDraw([EnginePtr = StaleEngine.Get()]()
	{
		FBattleC09BWildFlowEngineFixture::AdvanceStateVersion(*EnginePtr);
	});
	const FCheckpointObservation StaleBefore = ObserveCheckpoint(*StaleEngine, TargetId);
	const FBattleResolution StaleRejected = StaleEngine->ExecuteCurrentBagItem();
	VerifyRejectedCheckpoint(
		*this,
		*StaleEngine,
		TargetId,
		StaleBefore,
		StaleBefore.StateVersion + 1,
		EBattleRejectionReason::StaleCheckpointIdentity,
		StaleRejected);
	TestTrue(TEXT("Stale identity is injected after staged Capture draws"),
		StaleRandom->GetCounters().SuccessfulDraws > 0);
	TestEqual(TEXT("Stale identity prevents parent RNG commit"),
		StaleRandom->GetCounters().CommitAttempts, 0);

	TUniquePtr<FBattleEngine> CommitEngine;
	FFaultBattleRandom* CommitRandom = nullptr;
	if (!TestTrue(TEXT("Capture commit-failure engine is created"),
		TryMakeFaultEngine(
			Scenario,
			{0, 0, 0, 0},
			EFaultRandomMode::Commit,
			CommitEngine,
			CommitRandom))
		|| !TestTrue(TEXT("Capture commit-failure turn locks"),
			LockTurn(*CommitEngine, PlayerLeftValue, EBattleActionKind::Bag))
		|| !TestTrue(TEXT("Capture commit-failure starts"),
			BeginExpectedWildAction(
				*CommitEngine,
				PlayerLeftValue,
				EBattleActionKind::Bag)))
	{
		return false;
	}
	check(CommitRandom != nullptr);
	const FCheckpointObservation CommitBefore = ObserveCheckpoint(*CommitEngine, TargetId);
	const FBattleResolution CommitRejected = CommitEngine->ExecuteCurrentBagItem();
	VerifyRejectedCheckpoint(
		*this,
		*CommitEngine,
		TargetId,
		CommitBefore,
		CommitBefore.StateVersion,
		EBattleRejectionReason::RandomTransactionCommitFailed,
		CommitRejected);
	TestEqual(TEXT("Capture RNG commit is attempted exactly once"),
		CommitRandom->GetCounters().CommitAttempts, 1);
	TestTrue(TEXT("Commit failure occurs after all four staged shakes"),
		CommitRandom->GetCounters().SuccessfulDraws == 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023D3CaptureStaleExecutionTest,
	"PokemonSolarus.Battle.ADR0002.3D3.Capture.Execution.StalePreUseCancellation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023D3CaptureStaleExecutionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FAtomicWildScenario Scenario = MakeAtomicCaptureScenario();
	Scenario.CaptureProgression.bMustCapture = true;
	TUniquePtr<FBattleEngine> Engine;
	if (!TestTrue(TEXT("Pre-use stale Capture engine is created"),
		TryMakeSequenceEngine(Scenario, {}, Engine))
		|| !TestTrue(TEXT("Pre-use stale Capture turn locks"),
			LockTurn(*Engine, PlayerLeftValue, EBattleActionKind::Bag))
		|| !TestTrue(TEXT("Pre-use stale Capture starts"),
			BeginExpectedWildAction(*Engine, PlayerLeftValue, EBattleActionKind::Bag)))
	{
		return false;
	}

	FBattleEngineState& MutableState =
		FBattleC09BWildFlowEngineFixture::GetMutableState(*Engine);
	FBattleBattlerState* Target = MutableState.FindMutableBattler(
		MakeNumericId<FBattlerId>(OpponentLeftValue));
	check(Target != nullptr);
	Target->CaptureClassification = EBattleCaptureSpeciesClassification::UltraBeast;
	const FCheckpointObservation Before = ObserveCheckpoint(*Engine, Target->BattlerId);
	const FBattleResolution Canceled = Engine->ExecuteCurrentBagItem();
	const FBattleEngineState& State =
		FBattleC09BWildFlowEngineFixture::GetState(*Engine);
	const FBattleTrainerState* PlayerTrainer = State.FindTrainer(
		MakeNumericId<FTrainerId>(PlayerTrainerValue));
	TestTrue(TEXT("A stale pre-use Capture is an accepted queue cancellation"),
		Canceled.WasAccepted());
	TestTrue(TEXT("Stale pre-use Capture preserves exact event order"),
		HasExactEventOrder(Canceled, {
			EBattleEventType::ActionCanceled,
			EBattleEventType::ActionCompleted}));
	TestEqual(TEXT("Stale pre-use Capture preserves Poke Balls"),
		PlayerTrainer != nullptr ? PlayerTrainer->Bag[0].Count : INDEX_NONE,
		Before.PokeBallCount);
	TestEqual(TEXT("Stale pre-use Capture preserves Bag quota"),
		PlayerTrainer != nullptr
			? PlayerTrainer->ActionAllowance.bBagActionAvailable
			: false,
		Before.bBagActionAvailable);
	TestEqual(TEXT("Stale pre-use Capture consumes no gameplay RNG"),
		State.Random->GetTrace().Num(), Before.RandomTraceCount);
	TestEqual(TEXT("Stale pre-use Capture creates no pending record"),
		State.PendingCaptures.Num(), Before.PendingCaptureCount);
	TestTrue(TEXT("Stale pre-use Capture leaves the target active"),
		FBattleC09BWildFlowEngineFixture::IsActive(*Engine, Target->BattlerId));
	TestEqual(TEXT("Stale pre-use cancellation advances only its action cursor"),
		State.CurrentLockedActionIndex, Before.LockedActionIndex + 1);
	TestTrue(TEXT("Stale pre-use resolution returns the exact single append"),
		IsReturnedResolutionAppendedExactlyOnce(*Engine, Canceled));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023D3CaptureAtomicPublicationTest,
	"PokemonSolarus.Battle.ADR0002.3D3.Capture.Execution.LegalFailureAndAtomicSuccess",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023D3CaptureAtomicPublicationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FBattlerId TargetId = MakeNumericId<FBattlerId>(OpponentLeftValue);
	const FTrainerId PlayerTrainerId = MakeNumericId<FTrainerId>(PlayerTrainerValue);
	FAtomicWildScenario Scenario = MakeAtomicCaptureScenario();

	TUniquePtr<FBattleEngine> FailureEngine;
	if (!TestTrue(TEXT("Legal failed-Capture engine is created"),
		TryMakeSequenceEngine(Scenario, {65535}, FailureEngine))
		|| !TestTrue(TEXT("Legal failed-Capture turn locks"),
			LockTurn(*FailureEngine, PlayerLeftValue, EBattleActionKind::Bag))
		|| !TestTrue(TEXT("Legal failed-Capture starts"),
			BeginExpectedWildAction(
				*FailureEngine,
				PlayerLeftValue,
				EBattleActionKind::Bag)))
	{
		return false;
	}
	const FCheckpointObservation FailureBefore = ObserveCheckpoint(*FailureEngine, TargetId);
	const FBattleResolution Failed = FailureEngine->ExecuteCurrentBagItem();
	const FBattleEngineState& FailureState =
		FBattleC09BWildFlowEngineFixture::GetState(*FailureEngine);
	const FBattleTrainerState* FailureTrainer = FailureState.FindTrainer(PlayerTrainerId);
	TestTrue(TEXT("Legal unsuccessful Capture is accepted"), Failed.WasAccepted());
	TestTrue(TEXT("Legal unsuccessful Capture preserves exact event order"),
		HasExactEventOrder(Failed, {
			EBattleEventType::ItemUsed,
			EBattleEventType::ItemConsumed,
			EBattleEventType::CaptureAttempted,
			EBattleEventType::ActionCompleted}));
	TestEqual(TEXT("Legal failure consumes exactly one Ball"),
		FailureTrainer != nullptr ? FailureTrainer->Bag[0].Count : INDEX_NONE,
		FailureBefore.PokeBallCount - 1);
	TestFalse(TEXT("Legal failure consumes exactly one Trainer Bag action"),
		FailureTrainer != nullptr
			&& FailureTrainer->ActionAllowance.bBagActionAvailable);
	TestEqual(TEXT("Legal failure commits one early-stopping draw"),
		FailureState.Random->GetTrace().Num(), FailureBefore.RandomTraceCount + 1);
	TestTrue(TEXT("Legal failure draw uses the Capture shake purpose"),
		!FailureState.Random->GetTrace().IsEmpty()
			&& FailureState.Random->GetTrace().Last().RulePurpose
				== FBattleCaptureCalculator::GetShakeCheckPurpose());
	TestTrue(TEXT("Legal failure leaves its target active"),
		FBattleC09BWildFlowEngineFixture::IsActive(*FailureEngine, TargetId));
	TestEqual(TEXT("Legal failure creates no pending record"),
		FailureState.PendingCaptures.Num(), 0);
	TestTrue(TEXT("Legal failure returns the exact single append"),
		IsReturnedResolutionAppendedExactlyOnce(*FailureEngine, Failed));

	TUniquePtr<FBattleEngine> SuccessEngine;
	if (!TestTrue(TEXT("Atomic successful-Capture engine is created"),
		TryMakeSequenceEngine(Scenario, {0, 0, 0, 0}, SuccessEngine))
		|| !TestTrue(TEXT("Atomic successful-Capture turn locks"),
			LockTurn(*SuccessEngine, PlayerLeftValue, EBattleActionKind::Bag))
		|| !TestTrue(TEXT("Atomic successful-Capture starts"),
			BeginExpectedWildAction(
				*SuccessEngine,
				PlayerLeftValue,
				EBattleActionKind::Bag)))
	{
		return false;
	}
	const FCheckpointObservation SuccessBefore = ObserveCheckpoint(*SuccessEngine, TargetId);
	const FBattleResolution Succeeded = SuccessEngine->ExecuteCurrentBagItem();
	const FBattleEngineState& SuccessState =
		FBattleC09BWildFlowEngineFixture::GetState(*SuccessEngine);
	const FBattleTrainerState* SuccessTrainer = SuccessState.FindTrainer(PlayerTrainerId);
	const FBattleBattlerState* CapturedBattler = SuccessState.FindBattler(TargetId);
	TestTrue(TEXT("Successful Capture is accepted"), Succeeded.WasAccepted());
	TestTrue(TEXT("Terminal successful Capture preserves exact event order"),
		HasExactEventOrder(Succeeded, {
			EBattleEventType::ItemUsed,
			EBattleEventType::ItemConsumed,
			EBattleEventType::CaptureAttempted,
			EBattleEventType::Captured,
			EBattleEventType::LeftActiveSlot,
			EBattleEventType::Removed,
			EBattleEventType::OpponentRemovalCheckpoint,
			EBattleEventType::ActionCompleted,
			EBattleEventType::BattleEnded}));
	TestEqual(TEXT("Successful Capture consumes exactly one Ball"),
		SuccessTrainer != nullptr ? SuccessTrainer->Bag[0].Count : INDEX_NONE,
		SuccessBefore.PokeBallCount - 1);
	TestFalse(TEXT("Successful Capture consumes Trainer Bag quota"),
		SuccessTrainer != nullptr
			&& SuccessTrainer->ActionAllowance.bBagActionAvailable);
	TestEqual(TEXT("Successful Capture commits exactly four shake draws"),
		SuccessState.Random->GetTrace().Num(), SuccessBefore.RandomTraceCount + 4);
	TestTrue(TEXT("Successful Capture atomically marks and removes the target"),
		CapturedBattler != nullptr
			&& CapturedBattler->bCaptured
			&& CapturedBattler->bRemoved
			&& !FBattleC09BWildFlowEngineFixture::IsActive(*SuccessEngine, TargetId));
	TestEqual(TEXT("Successful Capture appends one pending record"),
		SuccessState.PendingCaptures.Num(), SuccessBefore.PendingCaptureCount + 1);
	if (!SuccessState.PendingCaptures.IsEmpty())
	{
		const FBattlePendingCaptureRecord& Pending = SuccessState.PendingCaptures.Last();
		const FItemId HeldItemId = MakeDefinitionId<FItemId>(CaptureHeldItemName);
		TestEqual(TEXT("Pending Capture preserves current HP"),
			Pending.CurrentHP, Scenario.TargetCurrentHP);
		TestEqual(TEXT("Pending Capture preserves Party-first destination"),
			Pending.Destination, EBattlePendingCaptureDestination::Party);
		TestTrue(TEXT("Pending Capture retains original and current held item"),
			Pending.HeldItem.OriginalItemId == HeldItemId
				&& Pending.HeldItem.CurrentItemId == HeldItemId);
	}
	TestEqual(TEXT("Last-target Capture enters Victory"),
		SuccessState.Outcome, EBattleOutcome::Victory);
	TestEqual(TEXT("Last-target Capture retains Capture outcome cause"),
		SuccessState.OutcomeCause, EBattleOutcomeCause::Capture);
	TestEqual(TEXT("Last-target Capture is terminal"),
		SuccessState.Phase, EBattlePhase::Terminal);
	const int32 RemovalEventIndex = Succeeded.GetEvents().IndexOfByPredicate(
		[](const FBattleEvent& Event)
		{
			return Event.GetType() == EBattleEventType::OpponentRemovalCheckpoint;
		});
	TestTrue(TEXT("Removal checkpoint is staged into authoritative availability"),
		RemovalEventIndex != INDEX_NONE
			&& SuccessState.AvailableOpponentRemovalCheckpoints.Contains(
				Succeeded.GetEvents()[RemovalEventIndex].GetEventOrdinal()));
	TestTrue(TEXT("Successful Capture returns the exact single append"),
		IsReturnedResolutionAppendedExactlyOnce(*SuccessEngine, Succeeded));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E1ActionStartProceedTest,
	"PokemonSolarus.Battle.ADR0002.3E1.ActionStart.Success.ProceedSuppressionAndObedience",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E1ActionStartProceedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FAtomicWildScenario Scenario;
	Scenario.PlayerLeftSpeed = 150;
	Scenario.bPlayerHasCanonicalHeldItem = true;
	Scenario.bPlayerSubjectToObedience = true;
	Scenario.PlayerReferenceLevel = 20;
	Scenario.PlayerBadgeCount = 0;
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	const FTrainerId TrainerId = MakeNumericId<FTrainerId>(PlayerTrainerValue);

	TUniquePtr<FBattleEngine> Engine;
	if (!TestTrue(TEXT("Proceed engine is created"),
		TryMakeSequenceEngine(Scenario, {}, Engine))
		|| !TestTrue(TEXT("Proceed action is locked first"),
			LockTurn(*Engine, PlayerLeftValue, EBattleActionKind::Fight))
		|| !TestTrue(TEXT("Magic Room is seeded before action start"),
			TrySeedActionStartMagicRoom(*Engine, ActorId)))
	{
		return false;
	}
	const FActionStartCheckpointObservation Before =
		ObserveActionStartCheckpoint(*Engine, ActorId, TrainerId);
	const FBattleResolution Resolution = Engine->BeginNextLockedAction();
	const FBattleEngineState& State =
		FBattleC09BWildFlowEngineFixture::GetState(*Engine);
	const FBattleTrainerState* Trainer = State.FindTrainer(TrainerId);
	const FBattleBattlerState* Actor = State.FindBattler(ActorId);
	const FBattleHeldItemInstanceState* LedgerItem = Actor != nullptr
		? State.HeldItemLedger.FindState(Actor->HeldItem.InstanceId)
		: nullptr;

	TestTrue(TEXT("Proceed action start is accepted"), Resolution.WasAccepted());
	TestTrue(TEXT("Proceed keeps exact action-start event order"),
		HasExactEventOrder(Resolution, {
			EBattleEventType::ActionStarted,
			EBattleEventType::ObedienceConfirmed}));
	TestTrue(TEXT("Proceed obedience fact keeps canonical numeric metadata"),
		Resolution.GetEvents().Num() == 2
			&& Resolution.GetEvents()[1].GetNumericBefore()
				== TOptional<int64>(20)
			&& Resolution.GetEvents()[1].GetNumericAfter()
				== TOptional<int64>(20)
			&& Resolution.GetEvents()[1].GetNumericDelta()
				== TOptional<int64>(0));
	TestTrue(TEXT("Obedience confirmation remains CoreOnly"),
		Resolution.GetEvents().Num() == 2
			&& Resolution.GetEvents()[0].GetVisibility().Level
				== EBattleVisibilityLevel::Public
			&& Resolution.GetEvents()[1].GetVisibility().Level
				== EBattleVisibilityLevel::CoreOnly);
	TestTrue(TEXT("Proceed returns the exact single append"),
		IsReturnedResolutionAppendedExactlyOnce(*Engine, Resolution));
	TestEqual(TEXT("Proceed increments state version once"),
		State.StateVersion, Before.StateVersion + 1);
	TestEqual(TEXT("Proceed resolution reports the exact version pair"),
		Resolution.GetBeforeStateVersion(), Before.StateVersion);
	TestEqual(TEXT("Proceed resolution reports one after-version"),
		Resolution.GetAfterStateVersion(), Before.StateVersion + 1);
	TestEqual(TEXT("Proceed keeps the current action cursor"),
		State.CurrentLockedActionIndex, Before.ActionIndex);
	TestEqual(TEXT("Proceed enters Resolving"), State.Phase, EBattlePhase::Resolving);
	TestTrue(TEXT("Proceed starts without finishing or committing the action"),
		State.LockedActions.IsValidIndex(Before.ActionIndex)
			&& State.LockedActions[Before.ActionIndex].bStarted
			&& !State.LockedActions[Before.ActionIndex].bFinished
			&& !State.LockedActions[Before.ActionIndex].bMoveCommitted
			&& !State.LockedActions[Before.ActionIndex].TargetResolution.IsSet()
			&& State.LockedActions[Before.ActionIndex].EffectExecutionState
				== EBattleLockedEffectExecutionState::Pending);
	TestEqual(TEXT("Proceed consumes one Trainer action"),
		Trainer != nullptr ? Trainer->ActionAllowance.RemainingActions : INDEX_NONE,
		Before.RemainingActions - 1);
	TestTrue(TEXT("Proceed suppresses both battler and ledger held-item facts"),
		Actor != nullptr
			&& Actor->HeldItem.CurrentItemId == FBattleItemRules::GetLeftoversId()
			&& Actor->HeldItem.bSuppressed
			&& !Actor->HeldItem.bConsumed
			&& LedgerItem != nullptr
			&& LedgerItem->bSuppressed
			&& !LedgerItem->bConsumed);
	TestTrue(TEXT("Proceed preserves Magic Room and re-registers suppressed item hooks"),
		CountActionStartTriggerRegistrations(
			State,
			FBattleFieldSideConditionRules::GetMagicRoomId().GetDefinitionId()) > 0
			&& CountActionStartTriggerRegistrations(
				State,
				FBattleItemRules::GetLeftoversId().GetDefinitionId()) > 0
			&& State.TriggerFramework.GetActiveRegistrations().ContainsByPredicate(
				[](const FBattleTriggerRegistrationState& Registration)
				{
					return Registration.Spec.SourceDefinition.Kind
							== EBattleTriggerSourceDefinitionKind::Item
						&& Registration.Spec.SourceDefinition.ItemId
							== FBattleItemRules::GetLeftoversId()
						&& Registration.bSuppressed;
				}));
	TestEqual(TEXT("Proceed commits the staged Magic Room and item-cleanup tokens"),
		State.NextTriggerReentrancyToken, Before.NextTriggerToken + 2);
	TestEqual(TEXT("Proceed consumes no PP"),
		ObserveActionStartCheckpoint(*Engine, ActorId, TrainerId).TotalMovePP,
		Before.TotalMovePP);
	TestEqual(TEXT("Proceed consumes no RNG"),
		State.Random->GetTrace().Num(), Before.RandomTraceCount);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E1ActionStartRechargeTest,
	"PokemonSolarus.Battle.ADR0002.3E1.ActionStart.Success.RechargeDenial",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E1ActionStartRechargeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FAtomicWildScenario Scenario;
	Scenario.PlayerLeftSpeed = 150;
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	const FTrainerId TrainerId = MakeNumericId<FTrainerId>(PlayerTrainerValue);
	const FMoveId MoveId = MakeDefinitionId<FMoveId>(ProbeMoveName);
	TUniquePtr<FBattleEngine> Engine;
	if (!TestTrue(TEXT("Recharge engine is created"),
		TryMakeSequenceEngine(Scenario, {}, Engine))
		|| !TestTrue(TEXT("Recharge action is locked first"),
			LockTurn(*Engine, PlayerLeftValue, EBattleActionKind::Fight))
		|| !TestTrue(TEXT("Charging is seeded"),
			TrySeedActionStartVolatile(
				*Engine,
				ActorId,
				FBattleVolatileRules::GetChargingId(),
				MoveId.GetDefinitionId()))
		|| !TestTrue(TEXT("Fly semi-invulnerability is seeded"),
			TrySeedActionStartVolatile(
				*Engine,
				ActorId,
				FBattleVolatileRules::GetFlySemiInvulnerableId()))
		|| !TestTrue(TEXT("Recharge is seeded"),
			TrySeedActionStartVolatile(
				*Engine,
				ActorId,
				FBattleVolatileRules::GetRechargeId())))
	{
		return false;
	}
	const FActionStartCheckpointObservation Before =
		ObserveActionStartCheckpoint(*Engine, ActorId, TrainerId);
	const FBattleResolution Resolution = Engine->BeginNextLockedAction();
	const FBattleEngineState& State =
		FBattleC09BWildFlowEngineFixture::GetState(*Engine);
	const FBattleTrainerState* Trainer = State.FindTrainer(TrainerId);

	TestTrue(TEXT("Recharge denial is an accepted consumed action"),
		Resolution.WasAccepted());
	TestTrue(TEXT("Recharge denial keeps exact event order"),
		HasExactEventOrder(Resolution, {
			EBattleEventType::ActionStarted,
			EBattleEventType::StatusChanged,
			EBattleEventType::EffectPrevented,
			EBattleEventType::ActionCanceled,
			EBattleEventType::ActionCompleted}));
	TestTrue(TEXT("Recharge status removal keeps exact numeric metadata"),
		Resolution.GetEvents().Num() == 5
			&& Resolution.GetEvents()[1].GetNumericBefore()
				== TOptional<int64>(1)
			&& Resolution.GetEvents()[1].GetNumericAfter()
				== TOptional<int64>(0)
			&& Resolution.GetEvents()[1].GetNumericDelta()
				== TOptional<int64>(-1));
	TestTrue(TEXT("Recharge denial returns the exact single append"),
		IsReturnedResolutionAppendedExactlyOnce(*Engine, Resolution));
	TestEqual(TEXT("Recharge denial increments state version once"),
		State.StateVersion, Before.StateVersion + 1);
	TestEqual(TEXT("Recharge denial advances exactly one queue action"),
		State.CurrentLockedActionIndex, Before.ActionIndex + 1);
	TestEqual(TEXT("Recharge denial remains Resolving while another action waits"),
		State.Phase, EBattlePhase::Resolving);
	TestTrue(TEXT("Recharge denial marks the consumed action started and finished"),
		State.LockedActions.IsValidIndex(Before.ActionIndex)
			&& State.LockedActions[Before.ActionIndex].bStarted
			&& State.LockedActions[Before.ActionIndex].bFinished
			&& !State.LockedActions[Before.ActionIndex].bMoveCommitted);
	TestEqual(TEXT("Recharge denial consumes one Trainer action"),
		Trainer != nullptr ? Trainer->ActionAllowance.RemainingActions : INDEX_NONE,
		Before.RemainingActions - 1);
	TestFalse(TEXT("Recharge denial removes Recharge"),
		HasActionStartVolatile(State, ActorId, FBattleVolatileRules::GetRechargeId()));
	TestFalse(TEXT("Recharge denial clears Charging"),
		HasActionStartVolatile(State, ActorId, FBattleVolatileRules::GetChargingId()));
	TestFalse(TEXT("Recharge denial clears Fly semi-invulnerability"),
		HasActionStartVolatile(
			State,
			ActorId,
			FBattleVolatileRules::GetFlySemiInvulnerableId()));
	TestTrue(TEXT("Recharge and charge trigger registrations are cleaned"),
		CountActionStartTriggerRegistrations(
			State,
			FBattleVolatileRules::GetRechargeId().GetDefinitionId()) == 0
			&& CountActionStartTriggerRegistrations(
				State,
				FBattleVolatileRules::GetChargingId().GetDefinitionId()) == 0
			&& CountActionStartTriggerRegistrations(
				State,
				FBattleVolatileRules::GetFlySemiInvulnerableId().GetDefinitionId()) == 0);
	TestEqual(TEXT("Recharge denial commits dispatch plus three cleanup tokens"),
		State.NextTriggerReentrancyToken, Before.NextTriggerToken + 4);
	TestEqual(TEXT("Recharge denial consumes no PP"),
		ObserveActionStartCheckpoint(*Engine, ActorId, TrainerId).TotalMovePP,
		Before.TotalMovePP);
	TestEqual(TEXT("Recharge denial consumes no RNG"),
		State.Random->GetTrace().Num(), Before.RandomTraceCount);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E1ActionStartObedienceRefusalTest,
	"PokemonSolarus.Battle.ADR0002.3E1.ActionStart.Success.ObedienceRefusalAndChargeCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E1ActionStartObedienceRefusalTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	FAtomicWildScenario Scenario;
	Scenario.PlayerLeftSpeed = 150;
	Scenario.bPlayerSubjectToObedience = true;
	Scenario.PlayerReferenceLevel = 21;
	Scenario.PlayerBadgeCount = 0;
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	const FTrainerId TrainerId = MakeNumericId<FTrainerId>(PlayerTrainerValue);
	const FMoveId MoveId = MakeDefinitionId<FMoveId>(ProbeMoveName);
	TUniquePtr<FBattleEngine> Engine;
	if (!TestTrue(TEXT("Obedience-refusal engine is created"),
		TryMakeSequenceEngine(Scenario, {}, Engine))
		|| !TestTrue(TEXT("Obedience-refusal action is locked first"),
			LockTurn(*Engine, PlayerLeftValue, EBattleActionKind::Fight))
		|| !TestTrue(TEXT("Refused action Charging is seeded"),
			TrySeedActionStartVolatile(
				*Engine,
				ActorId,
				FBattleVolatileRules::GetChargingId(),
				MoveId.GetDefinitionId()))
		|| !TestTrue(TEXT("Refused action Fly state is seeded"),
			TrySeedActionStartVolatile(
				*Engine,
				ActorId,
				FBattleVolatileRules::GetFlySemiInvulnerableId())))
	{
		return false;
	}
	const FActionStartCheckpointObservation Before =
		ObserveActionStartCheckpoint(*Engine, ActorId, TrainerId);
	const FBattleResolution Resolution = Engine->BeginNextLockedAction();
	const FBattleEngineState& State =
		FBattleC09BWildFlowEngineFixture::GetState(*Engine);
	const FBattleTrainerState* Trainer = State.FindTrainer(TrainerId);

	TestTrue(TEXT("Obedience refusal is an accepted consumed action"),
		Resolution.WasAccepted());
	TestTrue(TEXT("Obedience refusal keeps exact event order"),
		HasExactEventOrder(Resolution, {
			EBattleEventType::ActionStarted,
			EBattleEventType::ObedienceRefused,
			EBattleEventType::ActionCompleted}));
	TestTrue(TEXT("Obedience refusal keeps exact public numeric metadata"),
		Resolution.GetEvents().Num() == 3
			&& Resolution.GetEvents()[1].GetVisibility().Level
				== EBattleVisibilityLevel::Public
			&& Resolution.GetEvents()[1].GetNumericBefore()
				== TOptional<int64>(21)
			&& Resolution.GetEvents()[1].GetNumericAfter()
				== TOptional<int64>(20)
			&& Resolution.GetEvents()[1].GetNumericDelta()
				== TOptional<int64>(1));
	TestTrue(TEXT("Obedience refusal returns the exact single append"),
		IsReturnedResolutionAppendedExactlyOnce(*Engine, Resolution));
	TestEqual(TEXT("Obedience refusal increments state version once"),
		State.StateVersion, Before.StateVersion + 1);
	TestEqual(TEXT("Obedience refusal advances one queue action"),
		State.CurrentLockedActionIndex, Before.ActionIndex + 1);
	TestTrue(TEXT("Obedience refusal starts and finishes the action"),
		State.LockedActions.IsValidIndex(Before.ActionIndex)
			&& State.LockedActions[Before.ActionIndex].bStarted
			&& State.LockedActions[Before.ActionIndex].bFinished
			&& !State.LockedActions[Before.ActionIndex].bMoveCommitted);
	TestEqual(TEXT("Obedience refusal consumes one Trainer action"),
		Trainer != nullptr ? Trainer->ActionAllowance.RemainingActions : INDEX_NONE,
		Before.RemainingActions - 1);
	TestFalse(TEXT("Obedience refusal clears Charging"),
		HasActionStartVolatile(State, ActorId, FBattleVolatileRules::GetChargingId()));
	TestFalse(TEXT("Obedience refusal clears Fly semi-invulnerability"),
		HasActionStartVolatile(
			State,
			ActorId,
			FBattleVolatileRules::GetFlySemiInvulnerableId()));
	TestEqual(TEXT("Obedience refusal commits both charge-cleanup tokens"),
		State.NextTriggerReentrancyToken, Before.NextTriggerToken + 2);
	TestEqual(TEXT("Obedience refusal consumes no PP"),
		ObserveActionStartCheckpoint(*Engine, ActorId, TrainerId).TotalMovePP,
		Before.TotalMovePP);
	TestEqual(TEXT("Obedience refusal consumes no RNG"),
		State.Random->GetTrace().Num(), Before.RandomTraceCount);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E1ActionStartActorInvalidationTest,
	"PokemonSolarus.Battle.ADR0002.3E1.ActionStart.Success.ActorInvalidationReplacementBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E1ActionStartActorInvalidationTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	FAtomicWildScenario Scenario;
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	const FTrainerId TrainerId = MakeNumericId<FTrainerId>(PlayerTrainerValue);
	const FActiveSlotId ActorSlot = MakeActiveSlotId(
		EBattleSide::Player,
		EBattlePosition::Left);
	TUniquePtr<FBattleEngine> Engine;
	if (!TestTrue(TEXT("Actor-invalidation engine is created"),
		TryMakeSequenceEngine(Scenario, {}, Engine))
		|| !TestTrue(TEXT("Actor-invalidation turn is locked"),
			LockTurn(*Engine, PlayerLeftValue, EBattleActionKind::Fight))
		|| !TestTrue(TEXT("Player action is selected as the last queued start"),
			TryPrepareLastLockedAction(*Engine, ActorId))
		|| !TestTrue(TEXT("The last action actor is invalidated from its active slot"),
			TryClearActionStartActiveSlot(*Engine, ActorSlot)))
	{
		return false;
	}
	const FActionStartCheckpointObservation Before =
		ObserveActionStartCheckpoint(*Engine, ActorId, TrainerId);
	const FBattleResolution Resolution = Engine->BeginNextLockedAction();
	const FBattleEngineState& State =
		FBattleC09BWildFlowEngineFixture::GetState(*Engine);
	const FBattleTrainerState* Trainer = State.FindTrainer(TrainerId);

	TestTrue(TEXT("Actor invalidation is an accepted cancellation"),
		Resolution.WasAccepted());
	TestTrue(TEXT("Actor invalidation and replacement keep exact event order"),
		HasExactEventOrder(Resolution, {
			EBattleEventType::ActionCanceled,
			EBattleEventType::ActionCompleted,
			EBattleEventType::ReplacementRequired}));
	TestTrue(TEXT("Actor invalidation cancellation stays public and action-caused"),
		Resolution.GetEvents().Num() == 3
			&& Resolution.GetEvents()[0].GetCause()
				== EBattleEventCause::Action
			&& Resolution.GetEvents()[0].GetVisibility().Level
				== EBattleVisibilityLevel::Public);
	TestTrue(TEXT("Actor invalidation returns the exact single append"),
		IsReturnedResolutionAppendedExactlyOnce(*Engine, Resolution));
	TestEqual(TEXT("Actor invalidation increments state version once"),
		State.StateVersion, Before.StateVersion + 1);
	TestEqual(TEXT("Actor invalidation exhausts the queue"),
		State.CurrentLockedActionIndex, State.LockedActions.Num());
	TestTrue(TEXT("Actor invalidation finishes without starting the action"),
		State.LockedActions.IsValidIndex(Before.ActionIndex)
			&& !State.LockedActions[Before.ActionIndex].bStarted
			&& State.LockedActions[Before.ActionIndex].bFinished
			&& !State.LockedActions[Before.ActionIndex].bMoveCommitted);
	TestEqual(TEXT("Actor invalidation consumes one Trainer action"),
		Trainer != nullptr ? Trainer->ActionAllowance.RemainingActions : INDEX_NONE,
		Before.RemainingActions - 1);
	TestEqual(TEXT("Actor invalidation stages MandatoryReplacement"),
		State.Phase, EBattlePhase::MandatoryReplacement);
	TestTrue(TEXT("Actor invalidation stages one canonical replacement request"),
		State.PendingReplacements.Num() == 1
			&& State.PendingReplacements[0].TrainerId == TrainerId
			&& State.PendingReplacements[0].ActiveSlotId == ActorSlot
			&& State.PendingDecision.IsSet()
			&& State.PendingDecisionRequests.Num() == 1
			&& State.PendingDecisionRequests[0].GetRequestKind()
				== EBattleDecisionRequestKind::MandatoryReplacement
			&& State.PendingDecisionRequests[0].GetStateVersion()
				== State.StateVersion);
	TestTrue(TEXT("ReplacementRequired targets the exact empty slot"),
		Resolution.GetEvents().Num() == 3
			&& Resolution.GetEvents()[2].GetTargets().Num() == 1
			&& Resolution.GetEvents()[2].GetTargets()[0].TrainerId == TrainerId
			&& Resolution.GetEvents()[2].GetTargets()[0].ActiveSlotId == ActorSlot);
	TestEqual(TEXT("Actor invalidation consumes no PP"),
		ObserveActionStartCheckpoint(*Engine, ActorId, TrainerId).TotalMovePP,
		Before.TotalMovePP);
	TestEqual(TEXT("Actor invalidation consumes no RNG"),
		State.Random->GetTrace().Num(), Before.RandomTraceCount);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E1ActionStartCapturedTargetTest,
	"PokemonSolarus.Battle.ADR0002.3E1.ActionStart.Success.CapturedTargetCancellation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E1ActionStartCapturedTargetTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	FAtomicWildScenario Scenario = MakeAtomicCaptureScenario();
	Scenario.Format = EBattleFormat::Double;
	Scenario.PlayerLeftSpeed = 160;
	Scenario.PlayerRightSpeed = 150;
	Scenario.OpponentLeftSpeed = 100;
	Scenario.OpponentRightSpeed = 4;
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerRightValue);
	const FTrainerId TrainerId = MakeNumericId<FTrainerId>(PlayerTrainerValue);
	const FBattlerId TargetId = MakeNumericId<FBattlerId>(OpponentLeftValue);
	TUniquePtr<FBattleEngine> Engine;
	if (!TestTrue(TEXT("Captured-target engine is created"),
		TryMakeSequenceEngine(Scenario, {0, 0, 0, 0}, Engine))
		|| !TestTrue(TEXT("Capture-before-selected-target turn is locked"),
			LockCaptureThenTargetTurn(*Engine))
		|| !TestTrue(TEXT("The faster Capture action starts first"),
			BeginExpectedWildAction(
				*Engine,
				PlayerLeftValue,
				EBattleActionKind::Bag)))
	{
		return false;
	}
	const FBattleResolution Capture = Engine->ExecuteCurrentBagItem();
	const FBattleEngineState& CapturedState =
		FBattleC09BWildFlowEngineFixture::GetState(*Engine);
	const FBattleBattlerState* CapturedBeforeStart = CapturedState.FindBattler(TargetId);
	if (!TestTrue(TEXT("The first action successfully captures its target"),
		Capture.WasAccepted()
			&& HasEvent(Capture, EBattleEventType::Captured)
			&& CapturedBeforeStart != nullptr
			&& CapturedBeforeStart->bCaptured
			&& CapturedBeforeStart->bRemoved
			&& !FBattleC09BWildFlowEngineFixture::IsActive(*Engine, TargetId))
		|| !TestTrue(TEXT("The next locked action retains the captured selected target"),
			CapturedState.LockedActions.IsValidIndex(
				CapturedState.CurrentLockedActionIndex)
				&& CapturedState.LockedActions[CapturedState.CurrentLockedActionIndex]
					.Decision.GetActingBattlerId() == ActorId
				&& CapturedState.LockedActions[CapturedState.CurrentLockedActionIndex]
					.SelectedTargetBattlerId == TargetId))
	{
		return false;
	}
	const FActionStartCheckpointObservation Before =
		ObserveActionStartCheckpoint(*Engine, ActorId, TrainerId);
	const FBattleResolution Resolution = Engine->BeginNextLockedAction();
	const FBattleEngineState& State =
		FBattleC09BWildFlowEngineFixture::GetState(*Engine);
	const FBattleTrainerState* Trainer = State.FindTrainer(TrainerId);
	const FBattleBattlerState* CapturedTarget = State.FindBattler(TargetId);

	TestTrue(TEXT("Captured target produces an accepted cancellation"),
		Resolution.WasAccepted());
	TestTrue(TEXT("Captured-target cancellation keeps exact event order"),
		HasExactEventOrder(Resolution, {
			EBattleEventType::ActionCanceled,
			EBattleEventType::ActionCompleted}));
	TestTrue(TEXT("Captured-target cancellation returns the exact single append"),
		IsReturnedResolutionAppendedExactlyOnce(*Engine, Resolution));
	TestEqual(TEXT("Captured-target cancellation increments state version once"),
		State.StateVersion, Before.StateVersion + 1);
	TestEqual(TEXT("Captured-target cancellation advances one queue action"),
		State.CurrentLockedActionIndex, Before.ActionIndex + 1);
	TestEqual(TEXT("Captured-target cancellation remains Resolving"),
		State.Phase, EBattlePhase::Resolving);
	TestTrue(TEXT("Captured-target cancellation finishes without starting"),
		State.LockedActions.IsValidIndex(Before.ActionIndex)
			&& !State.LockedActions[Before.ActionIndex].bStarted
			&& State.LockedActions[Before.ActionIndex].bFinished
			&& !State.LockedActions[Before.ActionIndex].bMoveCommitted);
	TestEqual(TEXT("Captured-target cancellation consumes one Trainer action"),
		Trainer != nullptr ? Trainer->ActionAllowance.RemainingActions : INDEX_NONE,
		Before.RemainingActions - 1);
	TestTrue(TEXT("Captured-target facts remain captured, removed, and inactive"),
		CapturedTarget != nullptr
			&& CapturedTarget->bCaptured
			&& CapturedTarget->bRemoved
			&& !FBattleC09BWildFlowEngineFixture::IsActive(*Engine, TargetId));
	TestEqual(TEXT("Captured-target cancellation consumes no PP"),
		ObserveActionStartCheckpoint(*Engine, ActorId, TrainerId).TotalMovePP,
		Before.TotalMovePP);
	TestEqual(TEXT("Captured-target cancellation consumes no RNG"),
		State.Random->GetTrace().Num(), Before.RandomTraceCount);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E1ActionStartPreparationFailureTest,
	"PokemonSolarus.Battle.ADR0002.3E1.ActionStart.Failure.PreparationBeforePublication",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E1ActionStartPreparationFailureTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	FAtomicWildScenario Scenario;
	Scenario.PlayerLeftSpeed = 150;
	Scenario.bPlayerHasCanonicalHeldItem = true;
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	const FTrainerId TrainerId = MakeNumericId<FTrainerId>(PlayerTrainerValue);
	TUniquePtr<FBattleEngine> Engine;
	if (!TestTrue(TEXT("Preparation-failure engine is created"),
		TryMakeSequenceEngine(Scenario, {}, Engine))
		|| !TestTrue(TEXT("Preparation-failure action is locked first"),
			LockTurn(*Engine, PlayerLeftValue, EBattleActionKind::Fight))
		|| !TestTrue(TEXT("Preparation-failure Magic Room is seeded"),
			TrySeedActionStartMagicRoom(*Engine, ActorId)))
	{
		return false;
	}
	FBattleEngineState& MutableState =
		FBattleC09BWildFlowEngineFixture::GetMutableState(*Engine);
	MutableState.NextTriggerReentrancyToken = TNumericLimits<uint64>::Max() - 1;
	const FActionStartCheckpointObservation Before =
		ObserveActionStartCheckpoint(*Engine, ActorId, TrainerId);
	const FBattleResolution Rejected = Engine->BeginNextLockedAction();
	const bool bRejectedWithoutDelta = VerifyRejectedActionStartCheckpoint(
		*this,
		*Engine,
		ActorId,
		TrainerId,
		Before,
		Before.StateVersion,
		EBattleRejectionReason::CheckpointPreparationFailed,
		Rejected);
	const FBattleEngineState& State =
		FBattleC09BWildFlowEngineFixture::GetState(*Engine);
	const FBattleBattlerState* Actor = State.FindBattler(ActorId);
	TestTrue(TEXT("Preparation failure occurs after staged Magic Room dispatch"),
		Before.NextTriggerToken == TNumericLimits<uint64>::Max() - 1
			&& CountActionStartTriggerRegistrations(
				State,
				FBattleFieldSideConditionRules::GetMagicRoomId().GetDefinitionId()) > 0);
	TestTrue(TEXT("Preparation failure publishes no held-item suppression"),
		Actor != nullptr
			&& Actor->HeldItem.CurrentItemId == FBattleItemRules::GetLeftoversId()
			&& !Actor->HeldItem.bSuppressed
			&& !Actor->HeldItem.bConsumed);
	return bRejectedWithoutDelta;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E1ActionStartStaleIdentityTest,
	"PokemonSolarus.Battle.ADR0002.3E1.ActionStart.Failure.StaleIdentityAfterPreparation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E1ActionStartStaleIdentityTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	FAtomicWildScenario Scenario;
	Scenario.PlayerLeftSpeed = 150;
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	const FTrainerId TrainerId = MakeNumericId<FTrainerId>(PlayerTrainerValue);
	TUniquePtr<FBattleEngine> Engine;
	FActionStartStaleRandom* Random = nullptr;
	if (!TestTrue(TEXT("Stale-identity engine is created"),
		TryMakeActionStartStaleEngine(Scenario, Engine, Random))
		|| !TestNotNull(TEXT("Stale-identity random seam is retained"), Random)
		|| !TestTrue(TEXT("Stale-identity action is locked first"),
			LockTurn(*Engine, PlayerLeftValue, EBattleActionKind::Fight)))
	{
		return false;
	}
	const FActionStartCheckpointObservation Before =
		ObserveActionStartCheckpoint(*Engine, ActorId, TrainerId);
	Random->ArmAfterTraceRead(
		7,
		[EnginePtr = Engine.Get()]()
		{
			FBattleC09BWildFlowEngineFixture::AdvanceStateVersion(*EnginePtr);
		});
	const FBattleResolution Rejected = Engine->BeginNextLockedAction();
	const int32 TraceReads = Random->GetReadsSinceArm();
	const bool bInjected = Random->WasInjected();
	Random->Disarm();
	TestTrue(TEXT("Stale identity is injected at the final post-plan recheck"),
		bInjected && TraceReads == 7);
	return VerifyRejectedActionStartCheckpoint(
		*this,
		*Engine,
		ActorId,
		TrainerId,
		Before,
		Before.StateVersion + 1,
		EBattleRejectionReason::StaleCheckpointIdentity,
		Rejected);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E1ActionStartPlanFailureTest,
	"PokemonSolarus.Battle.ADR0002.3E1.ActionStart.Failure.ResolutionPlanStaging",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E1ActionStartPlanFailureTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	FAtomicWildScenario Scenario;
	Scenario.PlayerLeftSpeed = 150;
	Scenario.bPlayerSubjectToObedience = true;
	Scenario.PlayerReferenceLevel = 21;
	Scenario.PlayerBadgeCount = 0;
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	const FTrainerId TrainerId = MakeNumericId<FTrainerId>(PlayerTrainerValue);
	TUniquePtr<FBattleEngine> Engine;
	if (!TestTrue(TEXT("Plan-failure engine is created"),
		TryMakeSequenceEngine(Scenario, {}, Engine))
		|| !TestTrue(TEXT("Plan-failure refusal action is locked first"),
			LockTurn(*Engine, PlayerLeftValue, EBattleActionKind::Fight)))
	{
		return false;
	}
	FBattleEngineState& MutableState =
		FBattleC09BWildFlowEngineFixture::GetMutableState(*Engine);
	MutableState.NextEventOrdinal = TNumericLimits<uint64>::Max() - 2;
	const FActionStartCheckpointObservation Before =
		ObserveActionStartCheckpoint(*Engine, ActorId, TrainerId);
	const FBattleResolution Rejected = Engine->BeginNextLockedAction();
	TestEqual(TEXT("Plan failure starts at the bounded near-overflow ordinal"),
		Before.NextEventOrdinal, TNumericLimits<uint64>::Max() - 2);
	return VerifyRejectedActionStartCheckpoint(
		*this,
		*Engine,
		ActorId,
		TrainerId,
		Before,
		Before.StateVersion,
		EBattleRejectionReason::CheckpointPreparationFailed,
		Rejected);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E2VoluntarySwitchFullEntryChainTest,
	"PokemonSolarus.Battle.ADR0002.3E2.VoluntarySwitch.Success.FullEntryChain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E2VoluntarySwitchFullEntryChainTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	TUniquePtr<FBattleEngine> Engine;
	if (!TestTrue(TEXT("Full-chain Switch engine is created"),
			TryMakeSequenceEngine(
				MakeAtomicVoluntarySwitchScenario(
					FBattleItemRules::GetSitrusBerryId(),
					FBattleAbilityRules::GetIntimidateId(),
					110),
				{},
				Engine))
		|| !TestTrue(TEXT("Full-chain Switch is locked"),
			TryPrepareAtomicVoluntarySwitch(*Engine))
		|| !TestTrue(TEXT("Full-chain outgoing transients are seeded"),
			TrySeedAtomicSwitchOutgoingTransients(*Engine))
		|| !TestTrue(TEXT("Full-chain Switch is started"),
			TryBeginAtomicVoluntarySwitch(*Engine))
		|| !TestTrue(TEXT("Full-chain Stealth Rock is seeded"),
			TrySeedAtomicSwitchHazard(
				*Engine,
				FBattleFieldSideConditionRules::GetStealthRockId())))
	{
		return false;
	}

	const FAtomicSwitchCheckpointObservation Before =
		ObserveAtomicSwitchCheckpoint(*Engine);
	const FBattleResolution Resolution = Engine->ExecuteCurrentSwitch();
	const FBattleEngineState& State =
		FBattleC09BWildFlowEngineFixture::GetState(*Engine);
	const FBattleBattlerState* Outgoing = State.FindBattler(
		MakeNumericId<FBattlerId>(PlayerLeftValue));
	const FBattleBattlerState* Incoming = State.FindBattler(
		MakeNumericId<FBattlerId>(PlayerReserveValue));
	const FBattleBattlerState* Opponent = State.FindBattler(
		MakeNumericId<FBattlerId>(OpponentLeftValue));
	const FBattleActivePositionState* PlayerActive = State.FindActivePosition(
		MakeActiveSlotId(EBattleSide::Player, EBattlePosition::Left));
	int32 OpponentAttackStage = 0;
	const bool bOpponentStageRead = Opponent != nullptr
		&& Opponent->Stages.TryGetStage(EBattleStat::Attack, OpponentAttackStage);

	TestTrue(TEXT("Full entry chain is accepted"), Resolution.WasAccepted());
	TestTrue(TEXT("Full entry chain has exact event order"),
		HasExactEventOrder(
			Resolution,
			{
				EBattleEventType::LeftActiveSlot,
				EBattleEventType::SwitchTransientStateCleared,
				EBattleEventType::EnteredActiveSlot,
				EBattleEventType::Switched,
				EBattleEventType::Damage,
				EBattleEventType::HPChanged,
				EBattleEventType::ItemActivated,
				EBattleEventType::ItemConsumed,
				EBattleEventType::Healing,
				EBattleEventType::HPChanged,
				EBattleEventType::AbilityActivated,
				EBattleEventType::StatStageChanged,
				EBattleEventType::ActionCompleted
			}));
	TestTrue(TEXT("Full entry chain is appended exactly once"),
		IsReturnedResolutionAppendedExactlyOnce(*Engine, Resolution));
	TestEqual(TEXT("Full entry chain advances one version"),
		State.StateVersion, Before.StateVersion + 1);
	TestEqual(TEXT("Full entry chain consumes no gameplay RNG"),
		State.Random->GetTrace().Num(), Before.RandomTraceCount);
	TestTrue(TEXT("Outgoing battler is inactive and fully cleaned"),
		Outgoing != nullptr
			&& PlayerActive != nullptr
			&& PlayerActive->BattlerId == MakeNumericId<FBattlerId>(PlayerReserveValue)
			&& Outgoing->Volatiles.IsEmpty()
			&& !Outgoing->LastMoveId.IsValid()
			&& !Outgoing->bAbilitySuppressed
			&& !Outgoing->EnteredActiveOnTurnId.IsValid());
	int32 OutgoingAttackStage = 1;
	TestTrue(TEXT("Outgoing stages reset at commit"),
		Outgoing != nullptr
			&& Outgoing->Stages.TryGetStage(EBattleStat::Attack, OutgoingAttackStage)
			&& OutgoingAttackStage == 0);
	TestTrue(TEXT("Incoming hazard, item, and entry Ability all commit"),
		Incoming != nullptr
			&& Incoming->CurrentHP == 135
			&& Incoming->HeldItem.bConsumed
			&& !Incoming->HeldItem.CurrentItemId.IsValid()
			&& Incoming->EnteredActiveOnTurnId == State.TurnId
			&& bOpponentStageRead
			&& OpponentAttackStage == -1);
	TestTrue(TEXT("Incoming Sitrus and Intimidate reveal facts commit"),
		IsAtomicSwitchDefinitionRevealed(
			State,
			MakeNumericId<FBattlerId>(PlayerReserveValue),
			true)
			&& Incoming != nullptr
			&& Incoming->HeldItem.bRevealed);
	TestEqual(TEXT("Full entry chain reaches EndOfTurn"),
		State.Phase, EBattlePhase::EndOfTurn);
	TestEqual(TEXT("Full entry chain advances the action cursor"),
		State.CurrentLockedActionIndex, 1);
	TestTrue(TEXT("Full entry chain marks its exact action finished"),
		State.LockedActions.Num() == 1 && State.LockedActions[0].bFinished);
	const FBattleReplayRecord Replay = Engine->ExportReplayRecord();
	TestEqual(TEXT("Full entry chain replay keeps schema 6"),
		Replay.GetSchemaVersion(), FBattleReplayRecord::CurrentSchemaVersion);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E2VoluntarySwitchAirBalloonTest,
	"PokemonSolarus.Battle.ADR0002.3E2.VoluntarySwitch.Success.AirBalloonReveal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E2VoluntarySwitchAirBalloonTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	TUniquePtr<FBattleEngine> Engine;
	if (!TestTrue(TEXT("Air Balloon Switch engine is created"),
			TryMakeSequenceEngine(
				MakeAtomicVoluntarySwitchScenario(FBattleItemRules::GetAirBalloonId()),
				{},
				Engine))
		|| !TestTrue(TEXT("Air Balloon Switch is locked"),
			TryPrepareAtomicVoluntarySwitch(*Engine))
		|| !TestTrue(TEXT("Air Balloon Switch is started"),
			TryBeginAtomicVoluntarySwitch(*Engine)))
	{
		return false;
	}
	const FAtomicSwitchCheckpointObservation Before =
		ObserveAtomicSwitchCheckpoint(*Engine);
	const FBattleResolution Resolution = Engine->ExecuteCurrentSwitch();
	const FBattleEngineState& State =
		FBattleC09BWildFlowEngineFixture::GetState(*Engine);
	const FBattleBattlerState* Incoming = State.FindBattler(
		MakeNumericId<FBattlerId>(PlayerReserveValue));
	TestTrue(TEXT("Air Balloon Switch is accepted"), Resolution.WasAccepted());
	TestTrue(TEXT("Air Balloon Switch has exact event order"),
		HasExactEventOrder(
			Resolution,
			{
				EBattleEventType::LeftActiveSlot,
				EBattleEventType::SwitchTransientStateCleared,
				EBattleEventType::EnteredActiveSlot,
				EBattleEventType::Switched,
				EBattleEventType::ItemActivated,
				EBattleEventType::ActionCompleted
			}));
	TestTrue(TEXT("Air Balloon Switch is appended exactly once"),
		IsReturnedResolutionAppendedExactlyOnce(*Engine, Resolution));
	TestTrue(TEXT("Air Balloon reveal commits to mirror and tracker"),
		Incoming != nullptr
			&& Incoming->HeldItem.CurrentItemId == FBattleItemRules::GetAirBalloonId()
			&& Incoming->HeldItem.bRevealed
			&& IsAtomicSwitchDefinitionRevealed(
				State,
				Incoming->BattlerId,
				false));
	TestEqual(TEXT("Air Balloon Switch advances one version"),
		State.StateVersion, Before.StateVersion + 1);
	TestEqual(TEXT("Air Balloon Switch consumes no RNG"),
		State.Random->GetTrace().Num(), Before.RandomTraceCount);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E2VoluntarySwitchLethalHazardTest,
	"PokemonSolarus.Battle.ADR0002.3E2.VoluntarySwitch.Success.LethalHazardReplacementBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E2VoluntarySwitchLethalHazardTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	TUniquePtr<FBattleEngine> Engine;
	if (!TestTrue(TEXT("Lethal-hazard Switch engine is created"),
			TryMakeSequenceEngine(MakeAtomicVoluntarySwitchScenario(FItemId(),
				FBattleAbilityRules::GetBlazeId(), 10), {}, Engine))
		|| !TestTrue(TEXT("Lethal-hazard Switch is locked"),
			TryPrepareAtomicVoluntarySwitch(*Engine))
		|| !TestTrue(TEXT("Lethal-hazard Switch is started"),
			TryBeginAtomicVoluntarySwitch(*Engine))
		|| !TestTrue(TEXT("Lethal Stealth Rock is seeded"),
			TrySeedAtomicSwitchHazard(
				*Engine,
				FBattleFieldSideConditionRules::GetStealthRockId())))
	{
		return false;
	}
	const FAtomicSwitchCheckpointObservation Before =
		ObserveAtomicSwitchCheckpoint(*Engine);
	const FBattleResolution Resolution = Engine->ExecuteCurrentSwitch();
	const FBattleEngineState& State =
		FBattleC09BWildFlowEngineFixture::GetState(*Engine);
	const FBattleBattlerState* Incoming = State.FindBattler(
		MakeNumericId<FBattlerId>(PlayerReserveValue));
	const FBattleActivePositionState* PlayerActive = State.FindActivePosition(
		MakeActiveSlotId(EBattleSide::Player, EBattlePosition::Left));
	TestTrue(TEXT("Lethal-hazard Switch is accepted"), Resolution.WasAccepted());
	TestTrue(TEXT("Lethal-hazard Switch has exact event and boundary order"),
		HasExactEventOrder(
			Resolution,
			{
				EBattleEventType::LeftActiveSlot,
				EBattleEventType::SwitchTransientStateCleared,
				EBattleEventType::EnteredActiveSlot,
				EBattleEventType::Switched,
				EBattleEventType::Damage,
				EBattleEventType::HPChanged,
				EBattleEventType::Fainted,
				EBattleEventType::LeftActiveSlot,
				EBattleEventType::Removed,
				EBattleEventType::ActionCompleted,
				EBattleEventType::ReplacementRequired
			}));
	TestTrue(TEXT("Lethal-hazard Switch is appended exactly once"),
		IsReturnedResolutionAppendedExactlyOnce(*Engine, Resolution));
	TestTrue(TEXT("Lethal incoming battler fully faints and leaves its slot"),
		Incoming != nullptr
			&& Incoming->CurrentHP == 0
			&& Incoming->bFainted
			&& Incoming->bRemoved
			&& PlayerActive != nullptr
			&& !PlayerActive->BattlerId.IsValid()
			&& !PlayerActive->TrainerId.IsValid());
	TestEqual(TEXT("Lethal hazard reaches mandatory replacement"),
		State.Phase, EBattlePhase::MandatoryReplacement);
	TestTrue(TEXT("Lethal hazard prepares one complete replacement checkpoint"),
		State.PendingReplacements.Num() == 1
			&& State.PendingDecisionRequests.Num() == 1
			&& State.PendingDecision.IsSet()
			&& State.PendingReplacements[0].TrainerId
				== MakeNumericId<FTrainerId>(PlayerTrainerValue));
	TestEqual(TEXT("Lethal hazard advances one version"),
		State.StateVersion, Before.StateVersion + 1);
	TestEqual(TEXT("Lethal hazard consumes no RNG"),
		State.Random->GetTrace().Num(), Before.RandomTraceCount);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E2VoluntarySwitchEntryItemFailureTest,
	"PokemonSolarus.Battle.ADR0002.3E2.VoluntarySwitch.Failure.EntryItemReveal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E2VoluntarySwitchEntryItemFailureTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	return RunAtomicVoluntarySwitchFailureFamily(
		*this,
		EAtomicSwitchFailureFamily::EntryItemReveal);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E2VoluntarySwitchEntryHazardFailureTest,
	"PokemonSolarus.Battle.ADR0002.3E2.VoluntarySwitch.Failure.EntryHazard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E2VoluntarySwitchEntryHazardFailureTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	return RunAtomicVoluntarySwitchFailureFamily(
		*this,
		EAtomicSwitchFailureFamily::EntryHazard);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E2VoluntarySwitchImmediateItemFailureTest,
	"PokemonSolarus.Battle.ADR0002.3E2.VoluntarySwitch.Failure.ImmediateHeldItem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E2VoluntarySwitchImmediateItemFailureTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	return RunAtomicVoluntarySwitchFailureFamily(
		*this,
		EAtomicSwitchFailureFamily::ImmediateHeldItem);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E2VoluntarySwitchEntryAbilityFailureTest,
	"PokemonSolarus.Battle.ADR0002.3E2.VoluntarySwitch.Failure.EntryAbility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E2VoluntarySwitchEntryAbilityFailureTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	return RunAtomicVoluntarySwitchFailureFamily(
		*this,
		EAtomicSwitchFailureFamily::EntryAbility);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E2VoluntarySwitchStaleIdentityTest,
	"PokemonSolarus.Battle.ADR0002.3E2.VoluntarySwitch.Failure.StaleIdentityBeforeCommit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E2VoluntarySwitchStaleIdentityTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	TUniquePtr<FBattleEngine> Engine;
	FActionStartStaleRandom* Random = nullptr;
	if (!TestTrue(TEXT("Stale Switch engine is created"),
			TryMakeActionStartStaleEngine(
				MakeAtomicVoluntarySwitchScenario(),
				Engine,
				Random))
		|| !TestNotNull(TEXT("Stale Switch random seam is retained"), Random)
		|| !TestTrue(TEXT("Stale Switch is locked"),
			TryPrepareAtomicVoluntarySwitch(*Engine))
		|| !TestTrue(TEXT("Stale Switch outgoing transients are seeded"),
			TrySeedAtomicSwitchOutgoingTransients(*Engine))
		|| !TestTrue(TEXT("Stale Switch is started"),
			TryBeginAtomicVoluntarySwitch(*Engine)))
	{
		return false;
	}
	const FAtomicSwitchCheckpointObservation Before =
		ObserveAtomicSwitchCheckpoint(*Engine);
	check(Random != nullptr);
	Random->ArmAfterTraceRead(
		2,
		[EnginePtr = Engine.Get()]()
		{
			FBattleC09BWildFlowEngineFixture::AdvanceStateVersion(*EnginePtr);
		});
	const FBattleResolution Rejected = Engine->ExecuteCurrentSwitch();
	const int32 TraceReads = Random->GetReadsSinceArm();
	const bool bInjected = Random->WasInjected();
	Random->Disarm();
	TestTrue(TEXT("Stale identity is injected only at the final recheck"),
		bInjected && TraceReads == 2);
	return VerifyRejectedAtomicVoluntarySwitch(
		*this,
		*Engine,
		Before,
		Before.StateVersion + 1,
		EBattleRejectionReason::StaleCheckpointIdentity,
		Rejected);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E3PivotSwitchFullEntryChainTest,
	"PokemonSolarus.Battle.ADR0002.3E3.PivotSwitch.Success.FullEntryChain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E3PivotSwitchFullEntryChainTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	TUniquePtr<FBattleEngine> Engine;
	if (!TestTrue(TEXT("Full-chain Pivot engine is created"),
			TryMakeSequenceEngine(
				MakeAtomicPivotSwitchScenario(
					FBattleItemRules::GetSitrusBerryId(),
					FBattleAbilityRules::GetIntimidateId(),
					110),
				{},
				Engine))
		|| !TestTrue(TEXT("Full-chain Pivot outgoing transients are seeded"),
			TrySeedAtomicSwitchOutgoingTransients(*Engine))
		|| !TestTrue(TEXT("Full-chain Pivot Stealth Rock is seeded"),
			TrySeedAtomicSwitchHazard(
				*Engine,
				FBattleFieldSideConditionRules::GetStealthRockId())))
	{
		return false;
	}
	FBattleDecisionRequest Request;
	FBattleDecision Response;
	if (!TestTrue(TEXT("Full-chain Fight reaches AwaitingPivot"),
			TryPrepareAtomicPivotSwitch(*Engine, Request))
		|| !TestTrue(TEXT("Full-chain Pivot response is created"),
			TryMakePivotSwitchDecision(Request, MakePartySlotId(1), Response)))
	{
		return false;
	}
	const FAtomicPivotSwitchObservation Before =
		ObserveAtomicPivotSwitchCheckpoint(*Engine);
	const FBattleResolution Resolution = Engine->SubmitDecision(Response);
	const FBattleEngineState& State =
		FBattleC09BWildFlowEngineFixture::GetState(*Engine);
	const FAtomicPivotSwitchObservation After =
		ObserveAtomicPivotSwitchCheckpoint(*Engine);
	const FBattleBattlerState* Outgoing = State.FindBattler(
		MakeNumericId<FBattlerId>(PlayerLeftValue));
	const FBattleBattlerState* Incoming = State.FindBattler(
		MakeNumericId<FBattlerId>(PlayerReserveValue));
	const FBattleBattlerState* Opponent = State.FindBattler(
		MakeNumericId<FBattlerId>(OpponentLeftValue));
	const FBattleActivePositionState* PlayerActive = State.FindActivePosition(
		MakeActiveSlotId(EBattleSide::Player, EBattlePosition::Left));
	int32 OpponentAttackStage = 0;
	const bool bOpponentStageRead = Opponent != nullptr
		&& Opponent->Stages.TryGetStage(EBattleStat::Attack, OpponentAttackStage);
	int32 OutgoingAttackStage = 1;

	TestTrue(TEXT("Full entry-chain Pivot is accepted"), Resolution.WasAccepted());
	TestTrue(TEXT("Full entry-chain Pivot keeps exact event order"),
		HasExactEventOrder(
			Resolution,
			{
				EBattleEventType::LeftActiveSlot,
				EBattleEventType::SwitchTransientStateCleared,
				EBattleEventType::EnteredActiveSlot,
				EBattleEventType::Switched,
				EBattleEventType::Damage,
				EBattleEventType::HPChanged,
				EBattleEventType::ItemActivated,
				EBattleEventType::ItemConsumed,
				EBattleEventType::Healing,
				EBattleEventType::HPChanged,
				EBattleEventType::AbilityActivated,
				EBattleEventType::StatStageChanged,
				EBattleEventType::ActionCompleted
			}));
	TestTrue(TEXT("Full entry-chain Pivot is appended exactly once"),
		IsReturnedResolutionAppendedExactlyOnce(*Engine, Resolution));
	TestEqual(TEXT("Full entry-chain Pivot advances one version"),
		State.StateVersion, Before.Mechanics.StateVersion + 1);
	TestEqual(TEXT("Full entry-chain Pivot consumes no RNG"),
		State.Random->GetTrace().Num(), Before.Mechanics.RandomTraceCount);
	TestTrue(TEXT("Full entry-chain Pivot cleans and deactivates the outgoing battler"),
		Outgoing != nullptr
			&& PlayerActive != nullptr
			&& PlayerActive->BattlerId == MakeNumericId<FBattlerId>(PlayerReserveValue)
			&& Outgoing->Volatiles.IsEmpty()
			&& !Outgoing->LastMoveId.IsValid()
			&& !Outgoing->bAbilitySuppressed
			&& !Outgoing->EnteredActiveOnTurnId.IsValid()
			&& Outgoing->Stages.TryGetStage(EBattleStat::Attack, OutgoingAttackStage)
			&& OutgoingAttackStage == 0);
	TestTrue(TEXT("Full entry-chain Pivot commits hazard, item, and Ability effects"),
		Incoming != nullptr
			&& Incoming->CurrentHP == 135
			&& Incoming->HeldItem.bConsumed
			&& !Incoming->HeldItem.CurrentItemId.IsValid()
			&& Incoming->HeldItem.bRevealed
			&& Incoming->EnteredActiveOnTurnId == State.TurnId
			&& bOpponentStageRead
			&& OpponentAttackStage == -1
			&& IsAtomicSwitchDefinitionRevealed(
				State,
				MakeNumericId<FBattlerId>(PlayerReserveValue),
				true));
	TestTrue(TEXT("Pivot completes the same Fight action without another start or cost"),
		Before.bHasCurrentAction
			&& State.LockedActions.IsValidIndex(Before.Mechanics.ActionIndex)
			&& State.LockedActions[Before.Mechanics.ActionIndex].ActionId
				== Before.CurrentAction.ActionId
			&& State.LockedActions[Before.Mechanics.ActionIndex].Decision.GetActionKind()
				== EBattleActionKind::Fight
			&& State.LockedActions[Before.Mechanics.ActionIndex].EffectExecutionState
				== EBattleLockedEffectExecutionState::Completed
			&& State.LockedActions[Before.Mechanics.ActionIndex].bFinished
			&& After.ActionStartedEventCount == Before.ActionStartedEventCount
			&& After.RemainingActions == Before.RemainingActions);
	TestTrue(TEXT("Full entry-chain Pivot clears its request and advances its cursor"),
		State.Phase == EBattlePhase::Resolving
			&& State.CurrentLockedActionIndex == Before.Mechanics.ActionIndex + 1
			&& !State.PendingDecision.IsSet()
			&& State.PendingDecisionRequests.IsEmpty());
	TestTrue(TEXT("Successful Pivot response is retained as exact replay input"),
		After.SubmittedDecisionCount == Before.SubmittedDecisionCount + 1
			&& After.bHasLastSubmittedDecision
			&& ArePivotTestDecisionsIdentical(After.LastSubmittedDecision, Response));
	const FBattleReplayRecord Replay = Engine->ExportReplayRecord();
	TestTrue(TEXT("Successful Pivot replay keeps schema 6 and exact response"),
		Replay.GetSchemaVersion() == 6
			&& Replay.GetInputs().Decisions.Num() == After.SubmittedDecisionCount
			&& !Replay.GetInputs().Decisions.IsEmpty()
			&& ArePivotTestDecisionsIdentical(
				Replay.GetInputs().Decisions.Last(),
				Response));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E3PivotSwitchAirBalloonTest,
	"PokemonSolarus.Battle.ADR0002.3E3.PivotSwitch.Success.AirBalloonReveal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E3PivotSwitchAirBalloonTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	TUniquePtr<FBattleEngine> Engine;
	if (!TestTrue(TEXT("Air Balloon Pivot engine is created"),
			TryMakeSequenceEngine(
				MakeAtomicPivotSwitchScenario(FBattleItemRules::GetAirBalloonId()),
				{},
				Engine)))
	{
		return false;
	}
	FBattleDecisionRequest Request;
	FBattleDecision Response;
	if (!TestTrue(TEXT("Air Balloon Fight reaches AwaitingPivot"),
			TryPrepareAtomicPivotSwitch(*Engine, Request))
		|| !TestTrue(TEXT("Air Balloon Pivot response is created"),
			TryMakePivotSwitchDecision(Request, MakePartySlotId(1), Response)))
	{
		return false;
	}
	const FAtomicPivotSwitchObservation Before =
		ObserveAtomicPivotSwitchCheckpoint(*Engine);
	const FBattleResolution Resolution = Engine->SubmitDecision(Response);
	const FBattleEngineState& State =
		FBattleC09BWildFlowEngineFixture::GetState(*Engine);
	const FAtomicPivotSwitchObservation After =
		ObserveAtomicPivotSwitchCheckpoint(*Engine);
	const FBattleBattlerState* Incoming = State.FindBattler(
		MakeNumericId<FBattlerId>(PlayerReserveValue));
	TestTrue(TEXT("Air Balloon Pivot is accepted"), Resolution.WasAccepted());
	TestTrue(TEXT("Air Balloon Pivot keeps exact event order"),
		HasExactEventOrder(
			Resolution,
			{
				EBattleEventType::LeftActiveSlot,
				EBattleEventType::SwitchTransientStateCleared,
				EBattleEventType::EnteredActiveSlot,
				EBattleEventType::Switched,
				EBattleEventType::ItemActivated,
				EBattleEventType::ActionCompleted
			}));
	TestTrue(TEXT("Air Balloon Pivot is appended exactly once"),
		IsReturnedResolutionAppendedExactlyOnce(*Engine, Resolution));
	TestTrue(TEXT("Air Balloon Pivot commits mirror and reveal tracker"),
		Incoming != nullptr
			&& Incoming->HeldItem.CurrentItemId == FBattleItemRules::GetAirBalloonId()
			&& Incoming->HeldItem.bRevealed
			&& IsAtomicSwitchDefinitionRevealed(State, Incoming->BattlerId, false));
	TestEqual(TEXT("Air Balloon Pivot advances one version"),
		State.StateVersion, Before.Mechanics.StateVersion + 1);
	TestEqual(TEXT("Air Balloon Pivot consumes no RNG"),
		State.Random->GetTrace().Num(), Before.Mechanics.RandomTraceCount);
	TestTrue(TEXT("Air Balloon Pivot resumes and completes the same Fight action"),
		State.LockedActions.IsValidIndex(Before.Mechanics.ActionIndex)
			&& State.LockedActions[Before.Mechanics.ActionIndex].ActionId
				== Before.CurrentAction.ActionId
			&& State.LockedActions[Before.Mechanics.ActionIndex].bFinished
			&& State.LockedActions[Before.Mechanics.ActionIndex].EffectExecutionState
				== EBattleLockedEffectExecutionState::Completed
			&& After.ActionStartedEventCount == Before.ActionStartedEventCount
			&& After.RemainingActions == Before.RemainingActions
			&& After.SubmittedDecisionCount == Before.SubmittedDecisionCount + 1
			&& ArePivotTestDecisionsIdentical(After.LastSubmittedDecision, Response));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E3PivotSwitchLethalHazardTest,
	"PokemonSolarus.Battle.ADR0002.3E3.PivotSwitch.Success.LethalHazardReplacementBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E3PivotSwitchLethalHazardTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	FAtomicWildScenario Scenario = MakeAtomicPivotSwitchScenario(
		FItemId(),
		FBattleAbilityRules::GetBlazeId(),
		10);
	Scenario.PlayerLeftSpeed = 50;
	Scenario.OpponentLeftSpeed = 150;
	TUniquePtr<FBattleEngine> Engine;
	if (!TestTrue(TEXT("Lethal-hazard Pivot engine is created"),
			TryMakeSequenceEngine(
				Scenario,
				{},
				Engine))
		|| !TestTrue(TEXT("Lethal-hazard Pivot Stealth Rock is seeded"),
			TrySeedAtomicSwitchHazard(
				*Engine,
				FBattleFieldSideConditionRules::GetStealthRockId())))
	{
		return false;
	}
	FBattleDecisionRequest Request;
	FBattleDecision Response;
	if (!TestTrue(TEXT("Lethal-hazard Fight reaches AwaitingPivot"),
			TryPrepareAtomicPivotSwitch(*Engine, Request))
		|| !TestTrue(TEXT("Lethal-hazard Pivot response is created"),
			TryMakePivotSwitchDecision(Request, MakePartySlotId(1), Response)))
	{
		return false;
	}
	const FAtomicPivotSwitchObservation Before =
		ObserveAtomicPivotSwitchCheckpoint(*Engine);
	const FBattleResolution Resolution = Engine->SubmitDecision(Response);
	const FBattleEngineState& State =
		FBattleC09BWildFlowEngineFixture::GetState(*Engine);
	const FAtomicPivotSwitchObservation After =
		ObserveAtomicPivotSwitchCheckpoint(*Engine);
	const FBattleBattlerState* Incoming = State.FindBattler(
		MakeNumericId<FBattlerId>(PlayerReserveValue));
	const FBattleActivePositionState* PlayerActive = State.FindActivePosition(
		MakeActiveSlotId(EBattleSide::Player, EBattlePosition::Left));
	TestTrue(TEXT("Lethal-hazard Pivot is accepted"), Resolution.WasAccepted());
	TestTrue(TEXT("Lethal-hazard Pivot keeps exact event and boundary order"),
		HasExactEventOrder(
			Resolution,
			{
				EBattleEventType::LeftActiveSlot,
				EBattleEventType::SwitchTransientStateCleared,
				EBattleEventType::EnteredActiveSlot,
				EBattleEventType::Switched,
				EBattleEventType::Damage,
				EBattleEventType::HPChanged,
				EBattleEventType::Fainted,
				EBattleEventType::LeftActiveSlot,
				EBattleEventType::Removed,
				EBattleEventType::ActionCompleted,
				EBattleEventType::ReplacementRequired
			}));
	TestTrue(TEXT("Lethal-hazard Pivot is appended exactly once"),
		IsReturnedResolutionAppendedExactlyOnce(*Engine, Resolution));
	TestTrue(TEXT("Lethal-hazard Pivot commits faint and empty-slot facts"),
		Incoming != nullptr
			&& Incoming->CurrentHP == 0
			&& Incoming->bFainted
			&& Incoming->bRemoved
			&& PlayerActive != nullptr
			&& !PlayerActive->BattlerId.IsValid()
			&& !PlayerActive->TrainerId.IsValid());
	TestTrue(TEXT("Lethal-hazard Pivot stages one complete replacement boundary"),
		State.Phase == EBattlePhase::MandatoryReplacement
			&& State.PendingReplacements.Num() == 1
			&& State.PendingDecisionRequests.Num() == 1
			&& State.PendingDecision.IsSet()
			&& State.PendingReplacements[0].TrainerId
				== MakeNumericId<FTrainerId>(PlayerTrainerValue));
	TestTrue(TEXT("Lethal-hazard Pivot completes the same Fight action once"),
		State.LockedActions.IsValidIndex(Before.Mechanics.ActionIndex)
			&& State.LockedActions[Before.Mechanics.ActionIndex].ActionId
				== Before.CurrentAction.ActionId
			&& State.LockedActions[Before.Mechanics.ActionIndex].bFinished
			&& State.LockedActions[Before.Mechanics.ActionIndex].EffectExecutionState
				== EBattleLockedEffectExecutionState::Completed
			&& State.CurrentLockedActionIndex == Before.Mechanics.ActionIndex + 1
			&& After.ActionStartedEventCount == Before.ActionStartedEventCount
			&& After.RemainingActions == Before.RemainingActions);
	TestEqual(TEXT("Lethal-hazard Pivot advances one version"),
		State.StateVersion, Before.Mechanics.StateVersion + 1);
	TestEqual(TEXT("Lethal-hazard Pivot consumes no RNG"),
		State.Random->GetTrace().Num(), Before.Mechanics.RandomTraceCount);
	TestTrue(TEXT("Lethal-hazard Pivot retains its exact response input"),
		After.SubmittedDecisionCount == Before.SubmittedDecisionCount + 1
			&& ArePivotTestDecisionsIdentical(After.LastSubmittedDecision, Response));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E3PivotSwitchEntryItemFailureTest,
	"PokemonSolarus.Battle.ADR0002.3E3.PivotSwitch.Failure.EntryItemReveal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E3PivotSwitchEntryItemFailureTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	return RunAtomicPivotSwitchFailureFamily(
		*this,
		EAtomicSwitchFailureFamily::EntryItemReveal);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E3PivotSwitchEntryHazardFailureTest,
	"PokemonSolarus.Battle.ADR0002.3E3.PivotSwitch.Failure.EntryHazard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E3PivotSwitchEntryHazardFailureTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	return RunAtomicPivotSwitchFailureFamily(
		*this,
		EAtomicSwitchFailureFamily::EntryHazard);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E3PivotSwitchImmediateItemFailureTest,
	"PokemonSolarus.Battle.ADR0002.3E3.PivotSwitch.Failure.ImmediateHeldItem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E3PivotSwitchImmediateItemFailureTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	return RunAtomicPivotSwitchFailureFamily(
		*this,
		EAtomicSwitchFailureFamily::ImmediateHeldItem);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E3PivotSwitchEntryAbilityFailureTest,
	"PokemonSolarus.Battle.ADR0002.3E3.PivotSwitch.Failure.EntryAbility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E3PivotSwitchEntryAbilityFailureTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	return RunAtomicPivotSwitchFailureFamily(
		*this,
		EAtomicSwitchFailureFamily::EntryAbility);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E3PivotSwitchStaleFightActionTest,
	"PokemonSolarus.Battle.ADR0002.3E3.PivotSwitch.Failure.StaleFightActionIdentityBeforeCommit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E3PivotSwitchStaleFightActionTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	TUniquePtr<FBattleEngine> Engine;
	FActionStartStaleRandom* Random = nullptr;
	if (!TestTrue(TEXT("Stale-Fight Pivot engine is created"),
			TryMakeActionStartStaleEngine(
				MakeAtomicPivotSwitchScenario(),
				Engine,
				Random))
		|| !TestNotNull(TEXT("Stale-Fight Pivot random seam is retained"), Random))
	{
		return false;
	}
	FBattleDecisionRequest Request;
	FBattleDecision Response;
	if (!TestTrue(TEXT("Stale-Fight test reaches AwaitingPivot"),
			TryPrepareAtomicPivotSwitch(*Engine, Request))
		|| !TestTrue(TEXT("Stale-Fight response is created"),
			TryMakePivotSwitchDecision(Request, MakePartySlotId(1), Response)))
	{
		return false;
	}
	const FAtomicPivotSwitchObservation Before =
		ObserveAtomicPivotSwitchCheckpoint(*Engine);
	FAtomicPivotSwitchObservation Expected = Before;
	++Expected.CurrentAction.QueueOrdinal;
	bool bMutationSucceeded = false;
	check(Random != nullptr);
	Random->ArmAfterTraceRead(
		2,
		[EnginePtr = Engine.Get(), &bMutationSucceeded]()
		{
			FBattleEngineState& State =
				FBattleC09BWildFlowEngineFixture::GetMutableState(*EnginePtr);
			if (State.LockedActions.IsValidIndex(State.CurrentLockedActionIndex))
			{
				++State.LockedActions[State.CurrentLockedActionIndex].QueueOrdinal;
				bMutationSucceeded = true;
			}
		});
	const FBattleResolution Rejected = Engine->SubmitDecision(Response);
	const int32 TraceReads = Random->GetReadsSinceArm();
	const bool bInjected = Random->WasInjected();
	Random->Disarm();
	TestTrue(TEXT("Exact Fight identity changes only at final Pivot recheck"),
		bInjected && bMutationSucceeded && TraceReads == 2);
	return VerifyRejectedAtomicPivotSwitch(
		*this,
		*Engine,
		Before,
		Expected,
		Response,
		EBattleRejectionReason::StaleCheckpointIdentity,
		Rejected);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E3PivotSwitchStaleRequestTest,
	"PokemonSolarus.Battle.ADR0002.3E3.PivotSwitch.Failure.StalePendingRequestIdentityBeforeCommit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E3PivotSwitchStaleRequestTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	TUniquePtr<FBattleEngine> Engine;
	FActionStartStaleRandom* Random = nullptr;
	if (!TestTrue(TEXT("Stale-request Pivot engine is created"),
			TryMakeActionStartStaleEngine(
				MakeAtomicPivotSwitchScenario(
					FItemId(),
					FBattleAbilityRules::GetBlazeId(),
					200,
					true),
				Engine,
				Random))
		|| !TestNotNull(TEXT("Stale-request Pivot random seam is retained"), Random))
	{
		return false;
	}
	FBattleDecisionRequest Request;
	FBattleDecision Response;
	if (!TestTrue(TEXT("Stale-request test reaches AwaitingPivot"),
			TryPrepareAtomicPivotSwitch(*Engine, Request))
		|| !TestTrue(TEXT("Stale-request response is created"),
			TryMakePivotSwitchDecision(Request, MakePartySlotId(1), Response)))
	{
		return false;
	}
	const FAtomicPivotSwitchObservation Before =
		ObserveAtomicPivotSwitchCheckpoint(*Engine);
	FAtomicPivotSwitchObservation Expected = Before;
	FBattleDecisionRequest Replacement;
	if (!TestTrue(TEXT("Different canonical Pivot request is created"),
			TryCopyPivotRequestWithSwitchSlot(
				Before.PendingRequest,
				MakePartySlotId(2),
				Replacement)))
	{
		return false;
	}
	Expected.PendingDecision = Replacement;
	Expected.PendingRequest = Replacement;
	bool bMutationSucceeded = false;
	check(Random != nullptr);
	Random->ArmAfterTraceRead(
		2,
		[EnginePtr = Engine.Get(), &bMutationSucceeded]()
		{
			bMutationSucceeded = TryReplaceAtomicPivotPendingRequest(
				*EnginePtr,
				MakePartySlotId(2));
		});
	const FBattleResolution Rejected = Engine->SubmitDecision(Response);
	const int32 TraceReads = Random->GetReadsSinceArm();
	const bool bInjected = Random->WasInjected();
	Random->Disarm();
	TestTrue(TEXT("Exact pending Pivot request changes only at final recheck"),
		bInjected && bMutationSucceeded && TraceReads == 2);
	return VerifyRejectedAtomicPivotSwitch(
		*this,
		*Engine,
		Before,
		Expected,
		Response,
		EBattleRejectionReason::StaleCheckpointIdentity,
		Rejected);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E3PivotSwitchStaleSubmittedResponseTest,
	"PokemonSolarus.Battle.ADR0002.3E3.PivotSwitch.Failure.StaleSubmittedResponseIdentityBeforeCommit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E3PivotSwitchStaleSubmittedResponseTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	TUniquePtr<FBattleEngine> Engine;
	FActionStartStaleRandom* Random = nullptr;
	if (!TestTrue(TEXT("Stale-response Pivot engine is created"),
			TryMakeActionStartStaleEngine(
				MakeAtomicPivotSwitchScenario(
					FItemId(),
					FBattleAbilityRules::GetBlazeId(),
					200,
					true),
				Engine,
				Random))
		|| !TestNotNull(TEXT("Stale-response Pivot random seam is retained"), Random))
	{
		return false;
	}
	FBattleDecisionRequest Request;
	FBattleDecision Response;
	FBattleDecision ReplacementResponse;
	if (!TestTrue(TEXT("Stale-response test reaches AwaitingPivot"),
			TryPrepareAtomicPivotSwitch(*Engine, Request))
		|| !TestTrue(TEXT("Original Pivot response is created"),
			TryMakePivotSwitchDecision(Request, MakePartySlotId(1), Response))
		|| !TestTrue(TEXT("Different Pivot response is created"),
			TryMakePivotSwitchDecision(
				Request,
				MakePartySlotId(2),
				ReplacementResponse)))
	{
		return false;
	}
	const FAtomicPivotSwitchObservation Before =
		ObserveAtomicPivotSwitchCheckpoint(*Engine);
	bool bMutationSucceeded = false;
	check(Random != nullptr);
	Random->ArmAfterTraceRead(
		2,
		[EnginePtr = Engine.Get(), ReplacementResponse, &bMutationSucceeded]()
		{
			FBattleEngineState& State =
				FBattleC09BWildFlowEngineFixture::GetMutableState(*EnginePtr);
			if (!State.SubmittedDecisions.IsEmpty())
			{
				State.SubmittedDecisions.Last() = ReplacementResponse;
				bMutationSucceeded = true;
			}
		});
	const FBattleResolution Rejected = Engine->SubmitDecision(Response);
	const int32 TraceReads = Random->GetReadsSinceArm();
	const bool bInjected = Random->WasInjected();
	Random->Disarm();
	TestTrue(TEXT("Exact submitted Pivot response changes only at final recheck"),
		bInjected && bMutationSucceeded && TraceReads == 2);
	return VerifyRejectedAtomicPivotSwitch(
		*this,
		*Engine,
		Before,
		Before,
		ReplacementResponse,
		EBattleRejectionReason::StaleCheckpointIdentity,
		Rejected);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E4OrdinaryMoveTest,
	"PokemonSolarus.Battle.ADR0002.3E4.PreMoveGates.Success.OrdinaryMoveCommitsOnePP",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E4OrdinaryMoveTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	const FMoveId MoveId = MakeDefinitionId<FMoveId>(ProbeMoveName);
	TUniquePtr<FBattleEngine> Engine;
	if (!TestTrue(TEXT("Ordinary pre-move engine is created"),
			TryMakeSequenceEngine(MakePreMoveScenario(), {}, Engine))
		|| !TestTrue(TEXT("Ordinary Fight is locked and started"),
			TryLockAndBeginPreMove(*Engine)))
	{
		return false;
	}
	const FBattleEngineState& BeforeState =
		FBattleC09BWildFlowEngineFixture::GetState(*Engine);
	const uint64 VersionBefore = BeforeState.StateVersion;
	const int32 CursorBefore = BeforeState.CurrentLockedActionIndex;
	const int32 PPBefore = GetPreMovePP(*Engine, ActorId, MoveId);
	const FBattleResolution Resolution =
		Engine->CommitCurrentMoveAfterPreMoveGates();
	const FBattleEngineState& State =
		FBattleC09BWildFlowEngineFixture::GetState(*Engine);
	const FBattleBattlerState* Actor = State.FindBattler(ActorId);
	TestTrue(TEXT("Ordinary gate commits successfully"), Resolution.WasAccepted());
	TestTrue(TEXT("Ordinary gate publishes PPConsumed before MoveUsed"),
		HasExactEventOrder(
			Resolution,
			{EBattleEventType::PPConsumed, EBattleEventType::MoveUsed}));
	TestEqual(TEXT("Ordinary gate consumes exactly one PP"),
		GetPreMovePP(*Engine, ActorId, MoveId), PPBefore - 1);
	TestTrue(TEXT("Allowed move remains ready for target resolution"),
		State.CurrentLockedActionIndex == CursorBefore
			&& State.LockedActions.IsValidIndex(CursorBefore)
			&& State.LockedActions[CursorBefore].bStarted
			&& State.LockedActions[CursorBefore].bMoveCommitted
			&& !State.LockedActions[CursorBefore].TargetResolution.IsSet()
			&& !State.LockedActions[CursorBefore].bFinished);
	TestTrue(TEXT("Allowed move stores LastMoveId"),
		Actor != nullptr && Actor->LastMoveId == MoveId);
	TestEqual(TEXT("Accepted gate advances state version once"),
		State.StateVersion, VersionBefore + 1);
	TestTrue(TEXT("Accepted gate is returned and appended exactly once"),
		IsReturnedResolutionAppendedExactlyOnce(*Engine, Resolution));
	TestEqual(TEXT("Accepted gate replay schema remains 6"),
		Engine->ExportReplayRecord().GetSchemaVersion(), static_cast<uint32>(6));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E4SleepTest,
	"PokemonSolarus.Battle.ADR0002.3E4.PreMoveGates.Status.SleepDenialWakeAndExpiration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E4SleepTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	const FMoveId MoveId = MakeDefinitionId<FMoveId>(ProbeMoveName);
	TUniquePtr<FBattleEngine> Denied;
	if (!TestTrue(TEXT("Sleep-denial engine is created"),
			TryMakeSequenceEngine(MakePreMoveScenario(), {}, Denied))
		|| !TestTrue(TEXT("Two-turn Sleep is seeded"),
			TrySeedPreMoveMajorStatus(
				*Denied,
				ActorId,
				FBattleMajorStatusRules::GetSleepId(),
				2))
		|| !TestTrue(TEXT("Sleeping Fight is locked and started"),
			TryLockAndBeginPreMove(*Denied)))
	{
		return false;
	}
	const int32 DeniedPP = GetPreMovePP(*Denied, ActorId, MoveId);
	const FBattleResolution Denial = Denied->CommitCurrentMoveAfterPreMoveGates();
	const FBattleEngineState& DeniedState =
		FBattleC09BWildFlowEngineFixture::GetState(*Denied);
	const TArray<FBattleTriggerRegistrationState> SleepRegistrations =
		DeniedState.TriggerFramework.GetActiveRegistrations().FilterByPredicate(
			[](const FBattleTriggerRegistrationState& Registration)
			{
				return Registration.Spec.SourceDefinition.ConditionId
					== FBattleMajorStatusRules::GetSleepId();
			});
	TestTrue(TEXT("Sleep denial is an accepted rules outcome"), Denial.WasAccepted());
	TestEqual(TEXT("Sleep denial consumes no PP"),
		GetPreMovePP(*Denied, ActorId, MoveId), DeniedPP);
	TestEqual(TEXT("Sleep denial consumes no RNG"), Denied->ExportRandomTrace().Num(), 0);
	TestTrue(TEXT("Sleep duration decrements only in committed staged state"),
		SleepRegistrations.Num() == 1
			&& SleepRegistrations[0].RemainingTurns.IsSet()
			&& SleepRegistrations[0].RemainingTurns.GetValue() == 1);
	TestTrue(TEXT("Sleep denial completes and advances the action"),
		HasEvent(Denial, EBattleEventType::ActionCompleted)
			&& DeniedState.CurrentLockedActionIndex == 1);

	if (!TestTrue(TEXT("Opponent action begins after Sleep denial"),
			Denied->BeginNextLockedAction().WasAccepted())
		|| !TestTrue(TEXT("Opponent move passes its gate"),
			Denied->CommitCurrentMoveAfterPreMoveGates().WasAccepted())
		|| !TestTrue(TEXT("Opponent targets resolve"),
			Denied->ResolveCurrentMoveTargets().WasAccepted())
		|| !TestTrue(TEXT("Opponent effects complete turn one"),
			Denied->ExecuteCurrentMoveEffects().WasAccepted())
		|| !TestTrue(TEXT("Turn-one end phase resolves"),
			Denied->ResolveEndTurn().WasAccepted())
		|| !TestTrue(TEXT("Turn-two actions lock"),
			LockTurn(
				*Denied,
				PlayerLeftValue,
				EBattleActionKind::Fight))
		|| !TestTrue(TEXT("Expiring Sleep Fight starts"),
			BeginExpectedWildAction(
				*Denied,
				PlayerLeftValue,
				EBattleActionKind::Fight)))
	{
		return false;
	}
	const int32 WakePP = GetPreMovePP(*Denied, ActorId, MoveId);
	const int32 TraceBeforeWake = Denied->ExportRandomTrace().Num();
	const FBattleResolution Wake = Denied->CommitCurrentMoveAfterPreMoveGates();
	const FBattleBattlerState* Woken =
		FBattleC09BWildFlowEngineFixture::GetState(*Denied).FindBattler(ActorId);
	TestTrue(TEXT("Expired Sleep permits the move"), Wake.WasAccepted());
	TestTrue(TEXT("Expired Sleep cleanup precedes PP and MoveUsed"),
		HasExactEventOrder(
			Wake,
			{EBattleEventType::StatusChanged,
			 EBattleEventType::PPConsumed,
			 EBattleEventType::MoveUsed}));
	TestTrue(TEXT("Expired Sleep clears status and its trigger"),
		Woken != nullptr
			&& !Woken->MajorStatusId.IsValid()
			&& CountActionStartTriggerRegistrations(
				FBattleC09BWildFlowEngineFixture::GetState(*Denied),
				FBattleMajorStatusRules::GetSleepId().GetDefinitionId()) == 0);
	TestEqual(TEXT("Wake consumes one PP"),
		GetPreMovePP(*Denied, ActorId, MoveId), WakePP - 1);
	TestEqual(TEXT("Sleep expiration consumes no RNG"),
		Denied->ExportRandomTrace().Num(), TraceBeforeWake);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E4FreezeTest,
	"PokemonSolarus.Battle.ADR0002.3E4.PreMoveGates.Status.FreezeDeniedNaturalAndForcedThaw",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E4FreezeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	const FMoveId ProbeId = MakeDefinitionId<FMoveId>(ProbeMoveName);
	auto MakeFrozen = [&](TArray<uint32> Draws,
		const FAtomicWildScenario& Scenario,
		const FMoveId MoveId,
		TUniquePtr<FBattleEngine>& Out) -> bool
	{
		return TryMakeSequenceEngine(Scenario, MoveTemp(Draws), Out)
			&& TrySeedPreMoveMajorStatus(
				*Out,
				ActorId,
				FBattleMajorStatusRules::GetFreezeId())
			&& TryLockAndBeginPreMove(*Out, MoveId);
	};

	TUniquePtr<FBattleEngine> Denied;
	if (!TestTrue(TEXT("Failed-thaw Fight is prepared"),
			MakeFrozen({4}, MakePreMoveScenario(), FMoveId(), Denied)))
	{
		return false;
	}
	const int32 DeniedPP = GetPreMovePP(*Denied, ActorId, ProbeId);
	const FBattleResolution Denial = Denied->CommitCurrentMoveAfterPreMoveGates();
	TestTrue(TEXT("Failed natural thaw is accepted denial"), Denial.WasAccepted());
	TestEqual(TEXT("Failed thaw consumes no PP"),
		GetPreMovePP(*Denied, ActorId, ProbeId), DeniedPP);
	TestEqual(TEXT("Failed thaw commits exactly one transactional draw"),
		Denied->ExportRandomTrace().Num(), 1);

	TUniquePtr<FBattleEngine> Natural;
	if (!TestTrue(TEXT("Natural-thaw Fight is prepared"),
			MakeFrozen({0}, MakePreMoveScenario(), FMoveId(), Natural)))
	{
		return false;
	}
	const int32 NaturalPP = GetPreMovePP(*Natural, ActorId, ProbeId);
	const FBattleResolution Thaw = Natural->CommitCurrentMoveAfterPreMoveGates();
	const FBattleBattlerState* NaturallyThawed =
		FBattleC09BWildFlowEngineFixture::GetState(*Natural).FindBattler(ActorId);
	TestTrue(TEXT("Natural thaw permits the move"), Thaw.WasAccepted());
	TestTrue(TEXT("Natural thaw draw and cleanup precede PP"),
		HasExactEventOrder(
			Thaw,
			{EBattleEventType::RandomCheck,
			 EBattleEventType::StatusChanged,
			 EBattleEventType::PPConsumed,
			 EBattleEventType::MoveUsed}));
	TestTrue(TEXT("Natural thaw clears Freeze"),
		NaturallyThawed != nullptr && !NaturallyThawed->MajorStatusId.IsValid());
	TestEqual(TEXT("Natural thaw consumes one PP"),
		GetPreMovePP(*Natural, ActorId, ProbeId), NaturalPP - 1);
	TestEqual(TEXT("Natural thaw commits one draw"),
		Natural->ExportRandomTrace().Num(), 1);

	const FMoveId ThawMoveId = MakeDefinitionId<FMoveId>(ThawProbeMoveName);
	TUniquePtr<FBattleEngine> Forced;
	if (!TestTrue(TEXT("Forced-user-thaw Fight is prepared"),
			MakeFrozen(
				{},
				MakePreMoveScenario(ThawMoveId),
				ThawMoveId,
				Forced)))
	{
		return false;
	}
	const int32 ForcedPP = GetPreMovePP(*Forced, ActorId, ThawMoveId);
	const FBattleResolution ForcedThaw =
		Forced->CommitCurrentMoveAfterPreMoveGates();
	const FBattleBattlerState* ForceThawed =
		FBattleC09BWildFlowEngineFixture::GetState(*Forced).FindBattler(ActorId);
	TestTrue(TEXT("Forced user thaw permits the move"), ForcedThaw.WasAccepted());
	TestTrue(TEXT("Forced user thaw clears Freeze"),
		ForceThawed != nullptr && !ForceThawed->MajorStatusId.IsValid());
	TestEqual(TEXT("Forced user thaw consumes one PP"),
		GetPreMovePP(*Forced, ActorId, ThawMoveId), ForcedPP - 1);
	TestEqual(TEXT("Forced user thaw is a no-draw path"),
		Forced->ExportRandomTrace().Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E4ParalysisTest,
	"PokemonSolarus.Battle.ADR0002.3E4.PreMoveGates.Status.ParalysisDeniedAndAllowed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E4ParalysisTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	const FMoveId MoveId = MakeDefinitionId<FMoveId>(ProbeMoveName);
	auto Prepare = [&](const uint32 Draw, TUniquePtr<FBattleEngine>& Out) -> bool
	{
		return TryMakeSequenceEngine(MakePreMoveScenario(), {Draw}, Out)
			&& TrySeedPreMoveMajorStatus(
				*Out,
				ActorId,
				FBattleMajorStatusRules::GetParalysisId())
			&& TryLockAndBeginPreMove(*Out);
	};
	TUniquePtr<FBattleEngine> Denied;
	if (!TestTrue(TEXT("Full-Paralysis Fight is prepared"), Prepare(0, Denied)))
	{
		return false;
	}
	const int32 DeniedPP = GetPreMovePP(*Denied, ActorId, MoveId);
	const FBattleResolution Denial = Denied->CommitCurrentMoveAfterPreMoveGates();
	TestTrue(TEXT("Full Paralysis is accepted denial"), Denial.WasAccepted());
	TestEqual(TEXT("Full Paralysis consumes no PP"),
		GetPreMovePP(*Denied, ActorId, MoveId), DeniedPP);
	TestEqual(TEXT("Full Paralysis commits one transactional draw"),
		Denied->ExportRandomTrace().Num(), 1);
	TestTrue(TEXT("Full Paralysis completes the action"),
		HasEvent(Denial, EBattleEventType::ActionCompleted));

	TUniquePtr<FBattleEngine> Allowed;
	if (!TestTrue(TEXT("Allowed-Paralysis Fight is prepared"), Prepare(3, Allowed)))
	{
		return false;
	}
	const int32 AllowedPP = GetPreMovePP(*Allowed, ActorId, MoveId);
	const FBattleResolution Proceed = Allowed->CommitCurrentMoveAfterPreMoveGates();
	TestTrue(TEXT("Nonzero Paralysis roll permits the move"), Proceed.WasAccepted());
	TestTrue(TEXT("Allowed Paralysis draw precedes PP and MoveUsed"),
		HasExactEventOrder(
			Proceed,
			{EBattleEventType::RandomCheck,
			 EBattleEventType::PPConsumed,
			 EBattleEventType::MoveUsed}));
	TestEqual(TEXT("Allowed Paralysis consumes one PP"),
		GetPreMovePP(*Allowed, ActorId, MoveId), AllowedPP - 1);
	TestEqual(TEXT("Allowed Paralysis commits one transactional draw"),
		Allowed->ExportRandomTrace().Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E4ConfusionAllowedTest,
	"PokemonSolarus.Battle.ADR0002.3E4.PreMoveGates.Volatile.ConfusionAllowedAndDuration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E4ConfusionAllowedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	const FMoveId MoveId = MakeDefinitionId<FMoveId>(ProbeMoveName);
	TUniquePtr<FBattleEngine> Engine;
	if (!TestTrue(TEXT("Allowed-Confusion engine is created"),
			TryMakeSequenceEngine(MakePreMoveScenario(), {33}, Engine))
		|| !TestTrue(TEXT("Three-turn Confusion is seeded"),
			TrySeedActionStartVolatile(
				*Engine,
				ActorId,
				FBattleVolatileRules::GetConfusionId(),
				FDefinitionId(),
				3))
		|| !TestTrue(TEXT("Confused Fight is locked and started"),
			TryLockAndBeginPreMove(*Engine)))
	{
		return false;
	}
	const int32 PPBefore = GetPreMovePP(*Engine, ActorId, MoveId);
	const FBattleResolution Resolution =
		Engine->CommitCurrentMoveAfterPreMoveGates();
	const FBattleConditionState* Confusion = FindPreMoveVolatile(
		*Engine,
		ActorId,
		FBattleVolatileRules::GetConfusionId());
	TestTrue(TEXT("Allowed Confusion commits"), Resolution.WasAccepted());
	TestTrue(TEXT("Confusion gate draw precedes PP and MoveUsed"),
		HasExactEventOrder(
			Resolution,
			{EBattleEventType::RandomCheck,
			 EBattleEventType::PPConsumed,
			 EBattleEventType::MoveUsed}));
	TestTrue(TEXT("Confusion duration decrements atomically"),
		Confusion != nullptr
			&& Confusion->RemainingTurns.IsSet()
			&& Confusion->RemainingTurns.GetValue() == 2);
	TestEqual(TEXT("Allowed Confusion consumes one PP"),
		GetPreMovePP(*Engine, ActorId, MoveId), PPBefore - 1);
	TestEqual(TEXT("Allowed Confusion commits one draw"),
		Engine->ExportRandomTrace().Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E4ConfusionNonlethalTest,
	"PokemonSolarus.Battle.ADR0002.3E4.PreMoveGates.Volatile.ConfusionNonlethalSelfHit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E4ConfusionNonlethalTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	const FBattlerId TargetId = MakeNumericId<FBattlerId>(OpponentLeftValue);
	const FMoveId MoveId = MakeDefinitionId<FMoveId>(ProbeMoveName);
	TUniquePtr<FBattleEngine> Engine;
	if (!TestTrue(TEXT("Nonlethal self-hit engine is created"),
			TryMakeSequenceEngine(MakePreMoveScenario(), {0, 0}, Engine))
		|| !TestTrue(TEXT("Confusion is seeded for nonlethal self-hit"),
			TrySeedActionStartVolatile(
				*Engine,
				ActorId,
				FBattleVolatileRules::GetConfusionId(),
				FDefinitionId(),
				3))
		|| !TestTrue(TEXT("Nonlethal self-hit Fight is started"),
			TryLockAndBeginPreMove(*Engine)))
	{
		return false;
	}
	const FBattleEngineState& Before =
		FBattleC09BWildFlowEngineFixture::GetState(*Engine);
	const int32 HPBefore = Before.FindBattler(ActorId)->CurrentHP;
	const int32 TargetHPBefore = Before.FindBattler(TargetId)->CurrentHP;
	const int32 PPBefore = GetPreMovePP(*Engine, ActorId, MoveId);
	const int32 CursorBefore = Before.CurrentLockedActionIndex;
	const FBattleResolution Resolution =
		Engine->CommitCurrentMoveAfterPreMoveGates();
	const FBattleEngineState& State =
		FBattleC09BWildFlowEngineFixture::GetState(*Engine);
	const FBattleBattlerState* Actor = State.FindBattler(ActorId);
	const FBattleBattlerState* Target = State.FindBattler(TargetId);
	TestTrue(TEXT("Nonlethal self-hit is accepted denial"), Resolution.WasAccepted());
	TestTrue(TEXT("Self-hit commits gate draw, damage draw, damage and HP facts"),
		Resolution.GetEvents().Num() >= 7
			&& Resolution.GetEvents()[0].GetType() == EBattleEventType::RandomCheck
			&& Resolution.GetEvents()[1].GetType() == EBattleEventType::RandomCheck
			&& Resolution.GetEvents()[2].GetType() == EBattleEventType::Damage
			&& Resolution.GetEvents()[3].GetType() == EBattleEventType::HPChanged
			&& HasEvent(Resolution, EBattleEventType::ActionCompleted));
	TestTrue(TEXT("Nonlethal self-hit HP and completion commit together"),
		Actor != nullptr
			&& Actor->CurrentHP > 0
			&& Actor->CurrentHP < HPBefore
			&& State.CurrentLockedActionIndex == CursorBefore + 1);
	TestEqual(TEXT("Self-hit consumes no move PP"),
		GetPreMovePP(*Engine, ActorId, MoveId), PPBefore);
	TestTrue(TEXT("Self-hit leaves the target untouched"),
		Target != nullptr && Target->CurrentHP == TargetHPBefore);
	TestEqual(TEXT("Self-hit commits gate and damage draws"),
		Engine->ExportRandomTrace().Num(), 2);
	TestTrue(TEXT("Self-hit resolution is appended exactly once"),
		IsReturnedResolutionAppendedExactlyOnce(*Engine, Resolution));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E4MagicGuardConfusionTest,
	"PokemonSolarus.Battle.ADR0002.3E4.PreMoveGates.Volatile.MagicGuardPreventsConfusionDamage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E4MagicGuardConfusionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	const FMoveId MoveId = MakeDefinitionId<FMoveId>(ProbeMoveName);
	TUniquePtr<FBattleEngine> Engine;
	if (!TestTrue(TEXT("Magic Guard self-hit engine is created"),
			TryMakeSequenceEngine(
				MakePreMoveScenario(
					FMoveId(),
					200,
					FBattleAbilityRules::GetMagicGuardId()),
				{0},
				Engine))
		|| !TestTrue(TEXT("Confusion is seeded on Magic Guard actor"),
			TrySeedActionStartVolatile(
				*Engine,
				ActorId,
				FBattleVolatileRules::GetConfusionId(),
				FDefinitionId(),
				3))
		|| !TestTrue(TEXT("Magic Guard self-hit Fight is started"),
			TryLockAndBeginPreMove(*Engine)))
	{
		return false;
	}
	const FBattleEngineState& Before =
		FBattleC09BWildFlowEngineFixture::GetState(*Engine);
	const int32 HPBefore = Before.FindBattler(ActorId)->CurrentHP;
	const int32 PPBefore = GetPreMovePP(*Engine, ActorId, MoveId);
	const FBattleResolution Resolution =
		Engine->CommitCurrentMoveAfterPreMoveGates();
	const FBattleBattlerState* Actor =
		FBattleC09BWildFlowEngineFixture::GetState(*Engine).FindBattler(ActorId);
	TestTrue(TEXT("Magic Guard prevention is accepted denial"), Resolution.WasAccepted());
	TestTrue(TEXT("Magic Guard activation is published"),
		HasEvent(Resolution, EBattleEventType::AbilityActivated));
	TestFalse(TEXT("Magic Guard publishes no confusion damage"),
		HasEvent(Resolution, EBattleEventType::Damage)
			|| HasEvent(Resolution, EBattleEventType::HPChanged));
	TestTrue(TEXT("Magic Guard preserves HP"),
		Actor != nullptr && Actor->CurrentHP == HPBefore);
	TestEqual(TEXT("Magic Guard self-hit denial consumes no PP"),
		GetPreMovePP(*Engine, ActorId, MoveId), PPBefore);
	TestEqual(TEXT("Magic Guard commits only the gate draw and skips damage draw"),
		Engine->ExportRandomTrace().Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E4ConfusionLethalTest,
	"PokemonSolarus.Battle.ADR0002.3E4.PreMoveGates.Volatile.ConfusionLethalOutcomes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E4ConfusionLethalTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	const FMoveId MoveId = MakeDefinitionId<FMoveId>(ProbeMoveName);
	auto PrepareLethal = [&](const bool bReserve, TUniquePtr<FBattleEngine>& Out) -> bool
	{
		FAtomicWildScenario Scenario = MakePreMoveScenario(FMoveId(), 1);
		Scenario.PlayerLeftSpeed = 10;
		Scenario.bVoluntarySwitchFlow = bReserve;
		return TryMakeSequenceEngine(Scenario, {0, 0}, Out)
			&& TrySeedActionStartVolatile(
				*Out,
				ActorId,
				FBattleVolatileRules::GetConfusionId(),
				FDefinitionId(),
				3)
			&& LockTurn(
				*Out,
				PlayerLeftValue,
				EBattleActionKind::Fight)
			&& TryPrepareLastLockedAction(*Out, ActorId)
			&& BeginExpectedWildAction(
				*Out,
				PlayerLeftValue,
				EBattleActionKind::Fight);
	};

	TUniquePtr<FBattleEngine> Terminal;
	if (!TestTrue(TEXT("Terminal lethal self-hit is prepared"),
			PrepareLethal(false, Terminal)))
	{
		return false;
	}
	const int32 TerminalPP = GetPreMovePP(*Terminal, ActorId, MoveId);
	const FBattleResolution TerminalResult =
		Terminal->CommitCurrentMoveAfterPreMoveGates();
	const FBattleEngineState& TerminalState =
		FBattleC09BWildFlowEngineFixture::GetState(*Terminal);
	const FBattleBattlerState* Fainted = TerminalState.FindBattler(ActorId);
	TestTrue(TEXT("Lethal self-hit is accepted"), TerminalResult.WasAccepted());
	TestTrue(TEXT("Lethal self-hit publishes damage, faint cleanup and battle end"),
		HasEvent(TerminalResult, EBattleEventType::Damage)
			&& HasEvent(TerminalResult, EBattleEventType::Fainted)
			&& HasEvent(TerminalResult, EBattleEventType::LeftActiveSlot)
			&& HasEvent(TerminalResult, EBattleEventType::Removed)
			&& HasEvent(TerminalResult, EBattleEventType::ActionCompleted)
			&& HasEvent(TerminalResult, EBattleEventType::BattleEnded));
	TestTrue(TEXT("Terminal lethal self-hit commits faint and outcome together"),
		Fainted != nullptr
			&& Fainted->CurrentHP == 0
			&& Fainted->bFainted
			&& Fainted->bRemoved
			&& !Fainted->bFaintTransitionPending
			&& TerminalState.Phase == EBattlePhase::Terminal
			&& TerminalState.Outcome == EBattleOutcome::Defeat);
	TestEqual(TEXT("Terminal self-hit consumes no PP"),
		GetPreMovePP(*Terminal, ActorId, MoveId), TerminalPP);

	TUniquePtr<FBattleEngine> Replacement;
	if (!TestTrue(TEXT("Replacement lethal self-hit is prepared"),
			PrepareLethal(true, Replacement)))
	{
		return false;
	}
	const int32 ReplacementPP = GetPreMovePP(*Replacement, ActorId, MoveId);
	const FBattleResolution ReplacementResult =
		Replacement->CommitCurrentMoveAfterPreMoveGates();
	const FBattleEngineState& ReplacementState =
		FBattleC09BWildFlowEngineFixture::GetState(*Replacement);
	TestTrue(TEXT("Reserve-backed lethal self-hit is accepted"),
		ReplacementResult.WasAccepted());
	TestTrue(TEXT("Queue boundary requests mandatory replacement"),
		ReplacementState.Phase == EBattlePhase::MandatoryReplacement
			&& HasEvent(ReplacementResult, EBattleEventType::ReplacementRequired)
			&& !HasEvent(ReplacementResult, EBattleEventType::BattleEnded));
	TestEqual(TEXT("Replacement lethal self-hit consumes no PP"),
		GetPreMovePP(*Replacement, ActorId, MoveId), ReplacementPP);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E4PartnerRecoveryPlanTest,
	"PokemonSolarus.Battle.ADR0002.3E4.PreMoveGates.Projection.PartnerTeamVictoryRecoveryPlan",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E4PartnerRecoveryPlanTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FAtomicWildScenario Scenario = MakePreMoveScenario();
	Scenario.Format = EBattleFormat::PartnerDouble;
	TUniquePtr<FBattleEngine> Engine;
	if (!TestTrue(TEXT("Partner projection engine is created"),
			TryMakeSequenceEngine(Scenario, {}, Engine)))
	{
		return false;
	}
	FBattleEngineState& State =
		FBattleC09BWildFlowEngineFixture::GetMutableState(*Engine);
	FBattleBattlerState* Player = State.FindMutableBattler(
		MakeNumericId<FBattlerId>(PlayerLeftValue));
	if (!TestNotNull(TEXT("Partner projection finds player battler"), Player))
	{
		return false;
	}
	Player->CurrentHP = 0;
	Player->bFainted = true;
	Player->bRemoved = true;
	Player->MajorStatusId = FBattleMajorStatusRules::GetParalysisId();
	const int32 HPBeforePlan = Player->CurrentHP;
	const bool bFaintedBeforePlan = Player->bFainted;
	const FConditionId StatusBeforePlan = Player->MajorStatusId;
	FBattlePartnerTeamVictoryRecoveryPlan Plan;
	TestTrue(TEXT("Partner recovery plan is produced"),
		FBattlePartnerFlow::TryApplyTeamVictoryRecovery(
			static_cast<const FBattleEngineState&>(State),
			Plan));
	TestTrue(TEXT("Partner recovery preparation does not mutate supplied state"),
		Player->CurrentHP == HPBeforePlan
			&& Player->bFainted == bFaintedBeforePlan
			&& Player->MajorStatusId == StatusBeforePlan);
	TestTrue(TEXT("Partner recovery plan owns exact target and recovery facts"),
		Plan.Recovery.Target.BattlerId == Player->BattlerId
			&& Plan.Recovery.PreviousHP == 0
			&& Plan.Recovery.NewHP == 1
			&& Plan.Recovery.bMajorStatusCured);
	TestTrue(TEXT("Partner recovery plan applies to caller-owned staged state"),
		FBattlePartnerFlow::TryApplyTeamVictoryRecoveryPlan(State, Plan));
	TestTrue(TEXT("Applied partner plan restores exactly one HP and cures status"),
		Player->CurrentHP == 1
			&& !Player->bFainted
			&& !Player->bRemoved
			&& !Player->MajorStatusId.IsValid());

	TUniquePtr<FBattleEngine> InvalidEngine;
	if (!TestTrue(TEXT("Invalid partner-plan engine is created"),
			TryMakeSequenceEngine(Scenario, {}, InvalidEngine)))
	{
		return false;
	}
	const FBattleEngineState& InvalidState =
		FBattleC09BWildFlowEngineFixture::GetState(*InvalidEngine);
	const int32 LivingHP = InvalidState.FindBattler(
		MakeNumericId<FBattlerId>(PlayerLeftValue))->CurrentHP;
	FBattlePartnerTeamVictoryRecoveryPlan InvalidPlan;
	TestFalse(TEXT("Living player rejects partner recovery preparation"),
		FBattlePartnerFlow::TryApplyTeamVictoryRecovery(
			InvalidState,
			InvalidPlan));
	TestEqual(TEXT("Rejected partner preparation leaves player unchanged"),
		InvalidState.FindBattler(MakeNumericId<FBattlerId>(PlayerLeftValue))->CurrentHP,
		LivingHP);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E4SimpleVolatileCleanupTest,
	"PokemonSolarus.Battle.ADR0002.3E4.PreMoveGates.Volatile.SimpleAndRestrictionCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E4SimpleVolatileCleanupTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	const FMoveId MoveId = MakeDefinitionId<FMoveId>(ProbeMoveName);
	for (const FConditionId SimpleId : {
		FBattleVolatileRules::GetFlinchId(),
		FBattleVolatileRules::GetRechargeId()})
	{
		TUniquePtr<FBattleEngine> Engine;
		if (!TestTrue(TEXT("Simple-denial engine is created"),
				TryMakeSequenceEngine(MakePreMoveScenario(), {}, Engine))
			|| !TestTrue(TEXT("Simple-denial Fight starts"),
				TryLockAndBeginPreMove(*Engine))
			|| !TestTrue(TEXT("Simple volatile is seeded at the pre-move checkpoint"),
				TrySeedActionStartVolatile(*Engine, ActorId, SimpleId)))
		{
			return false;
		}
		const int32 PPBefore = GetPreMovePP(*Engine, ActorId, MoveId);
		const FBattleResolution Resolution =
			Engine->CommitCurrentMoveAfterPreMoveGates();
		TestTrue(TEXT("Simple volatile denial is accepted"), Resolution.WasAccepted());
		TestFalse(TEXT("Simple volatile is cleaned"),
			FindPreMoveVolatile(*Engine, ActorId, SimpleId) != nullptr);
		TestEqual(TEXT("Simple volatile denial consumes no PP"),
			GetPreMovePP(*Engine, ActorId, MoveId), PPBefore);
		TestTrue(TEXT("Simple volatile denial completes the action"),
			HasEvent(Resolution, EBattleEventType::ActionCompleted));
	}

	TUniquePtr<FBattleEngine> Taunted;
	if (!TestTrue(TEXT("Taunt engine is created"),
			TryMakeSequenceEngine(MakePreMoveScenario(), {}, Taunted))
		|| !TestTrue(TEXT("Taunt Fight starts"), TryLockAndBeginPreMove(*Taunted))
		|| !TestTrue(TEXT("Taunt is seeded"),
			TrySeedActionStartVolatile(
				*Taunted,
				ActorId,
				FBattleVolatileRules::GetTauntId(),
				FDefinitionId(),
				3)))
	{
		return false;
	}
	const int32 TauntPP = GetPreMovePP(*Taunted, ActorId, MoveId);
	const FBattleResolution TauntResult =
		Taunted->CommitCurrentMoveAfterPreMoveGates();
	TestTrue(TEXT("Taunt denial is accepted"), TauntResult.WasAccepted());
	TestEqual(TEXT("Taunt denial consumes no PP"),
		GetPreMovePP(*Taunted, ActorId, MoveId), TauntPP);
	TestTrue(TEXT("Taunt remains active after denying a status move"),
		FindPreMoveVolatile(
			*Taunted,
			ActorId,
			FBattleVolatileRules::GetTauntId()) != nullptr);

	TUniquePtr<FBattleEngine> ExpiredRestrictions;
	if (!TestTrue(TEXT("Restriction-cleanup engine is created"),
			TryMakeSequenceEngine(MakePreMoveScenario(), {}, ExpiredRestrictions))
		|| !TestTrue(TEXT("Restriction-cleanup Fight starts"),
			TryLockAndBeginPreMove(*ExpiredRestrictions))
		|| !TestTrue(TEXT("Invalid Encore payload is seeded"),
			TrySeedActionStartVolatile(
				*ExpiredRestrictions,
				ActorId,
				FBattleVolatileRules::GetEncoreId(),
				MakeDefinitionId<FDefinitionId>(TEXT("Move.ADR0002.3E4.MissingEncore")),
				3))
		|| !TestTrue(TEXT("Invalid Disable payload is seeded"),
			TrySeedActionStartVolatile(
				*ExpiredRestrictions,
				ActorId,
				FBattleVolatileRules::GetDisableId(),
				MakeDefinitionId<FDefinitionId>(TEXT("Move.ADR0002.3E4.MissingDisable")),
				5)))
	{
		return false;
	}
	const int32 RestrictionPP = GetPreMovePP(*ExpiredRestrictions, ActorId, MoveId);
	const FBattleResolution RestrictionResult =
		ExpiredRestrictions->CommitCurrentMoveAfterPreMoveGates();
	TestTrue(TEXT("Stale restrictions clean up and allow the move"),
		RestrictionResult.WasAccepted());
	TestFalse(TEXT("Stale Encore is removed"),
		FindPreMoveVolatile(
			*ExpiredRestrictions,
			ActorId,
			FBattleVolatileRules::GetEncoreId()) != nullptr);
	TestFalse(TEXT("Stale Disable is removed"),
		FindPreMoveVolatile(
			*ExpiredRestrictions,
			ActorId,
			FBattleVolatileRules::GetDisableId()) != nullptr);
	TestEqual(TEXT("Allowed restriction cleanup consumes one PP"),
		GetPreMovePP(*ExpiredRestrictions, ActorId, MoveId), RestrictionPP - 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E4StruggleChoiceTest,
	"PokemonSolarus.Battle.ADR0002.3E4.PreMoveGates.PP.StruggleAndChoiceLock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E4StruggleChoiceTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	TUniquePtr<FBattleEngine> StruggleEngine;
	if (!TestTrue(TEXT("Struggle engine is created"),
			TryMakeSequenceEngine(MakePreMoveScenario(), {}, StruggleEngine)))
	{
		return false;
	}
	FBattleBattlerState* Struggler =
		FBattleC09BWildFlowEngineFixture::GetMutableState(*StruggleEngine)
			.FindMutableBattler(ActorId);
	if (!TestNotNull(TEXT("Struggle actor exists"), Struggler))
	{
		return false;
	}
	for (FBattleMoveSlotState& Slot : Struggler->Moves)
	{
		Slot.CurrentPP = 0;
	}
	if (!TestTrue(TEXT("Struggle is locked and started"),
			TryLockAndBeginPreMove(
				*StruggleEngine,
				FBattleBuiltInMoveDefinitions::GetStruggleMoveId())))
	{
		return false;
	}
	const FBattleResolution Struggle =
		StruggleEngine->CommitCurrentMoveAfterPreMoveGates();
	TestTrue(TEXT("Struggle passes the checkpoint"), Struggle.WasAccepted());
	TestTrue(TEXT("Struggle publishes MoveUsed without PPConsumed"),
		HasExactEventOrder(Struggle, {EBattleEventType::MoveUsed}));
	TestTrue(TEXT("Struggle leaves every ordinary PP slot at zero"),
		Struggler->Moves.ContainsByPredicate(
			[](const FBattleMoveSlotState& Slot)
			{
				return Slot.CurrentPP == 0;
			})
			&& !Struggler->Moves.ContainsByPredicate(
				[](const FBattleMoveSlotState& Slot)
				{
					return Slot.CurrentPP != 0;
				}));

	TUniquePtr<FBattleEngine> ChoiceEngine;
	if (!TestTrue(TEXT("Choice Band engine is created"),
			TryMakeSequenceEngine(
				MakePreMoveScenario(
					FMoveId(),
					200,
					FAbilityId(),
					FBattleItemRules::GetChoiceBandId()),
				{},
				ChoiceEngine))
		|| !TestTrue(TEXT("Choice Band Fight starts"),
			TryLockAndBeginPreMove(*ChoiceEngine)))
	{
		return false;
	}
	const FMoveId ProbeId = MakeDefinitionId<FMoveId>(ProbeMoveName);
	const int32 ChoicePP = GetPreMovePP(*ChoiceEngine, ActorId, ProbeId);
	const FBattleResolution Choice =
		ChoiceEngine->CommitCurrentMoveAfterPreMoveGates();
	const FBattleBattlerState* ChoiceActor =
		FBattleC09BWildFlowEngineFixture::GetState(*ChoiceEngine).FindBattler(ActorId);
	TestTrue(TEXT("Choice Band ordinary move commits"), Choice.WasAccepted());
	TestTrue(TEXT("Choice Band establishes the exact move lock"),
		ChoiceActor != nullptr
			&& ChoiceActor->HeldItem.ChoiceLockedMoveId == ProbeId);
	TestEqual(TEXT("Choice Band first use consumes exactly one PP"),
		GetPreMovePP(*ChoiceEngine, ActorId, ProbeId), ChoicePP - 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E4ChargePPTest,
	"PokemonSolarus.Battle.ADR0002.3E4.PreMoveGates.PP.ChargedFirstTurnReleaseAndDeniedRelease",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E4ChargePPTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	const FMoveId ChargeId = MakeDefinitionId<FMoveId>(ChargeProbeMoveName);
	TUniquePtr<FBattleEngine> FirstTurn;
	if (!TestTrue(TEXT("Charge first-turn engine is created"),
			TryMakeSequenceEngine(MakePreMoveScenario(ChargeId), {}, FirstTurn))
		|| !TestTrue(TEXT("Charge first-turn Fight starts"),
			TryLockAndBeginPreMove(*FirstTurn, ChargeId)))
	{
		return false;
	}
	const int32 FirstPP = GetPreMovePP(*FirstTurn, ActorId, ChargeId);
	const FBattleResolution FirstCommit =
		FirstTurn->CommitCurrentMoveAfterPreMoveGates();
	TestTrue(TEXT("Charge first turn passes the checkpoint"),
		FirstCommit.WasAccepted());
	TestEqual(TEXT("Charge first turn consumes exactly one PP"),
		GetPreMovePP(*FirstTurn, ActorId, ChargeId), FirstPP - 1);

	auto PrepareRelease = [&](TUniquePtr<FBattleEngine>& Out) -> bool
	{
		if (!TryMakeSequenceEngine(MakePreMoveScenario(ChargeId), {}, Out))
		{
			return false;
		}
		FBattleBattlerState* Actor =
			FBattleC09BWildFlowEngineFixture::GetMutableState(*Out)
				.FindMutableBattler(ActorId);
		if (Actor == nullptr)
		{
			return false;
		}
		FBattleMoveSlotState* Slot = Actor->Moves.FindByPredicate(
			[ChargeId](const FBattleMoveSlotState& Candidate)
			{
				return Candidate.MoveId == ChargeId;
			});
		if (Slot == nullptr)
		{
			return false;
		}
		Slot->CurrentPP = 19;
		Actor->LastMoveId = ChargeId;
		return TrySeedActionStartVolatile(
				*Out,
				ActorId,
				FBattleVolatileRules::GetChargingId(),
				ChargeId.GetDefinitionId())
			&& TrySeedActionStartVolatile(
				*Out,
				ActorId,
				FBattleVolatileRules::GetFlySemiInvulnerableId())
			&& TryLockAndBeginPreMove(*Out, ChargeId);
	};

	TUniquePtr<FBattleEngine> Release;
	if (!TestTrue(TEXT("Allowed charged release is prepared"), PrepareRelease(Release)))
	{
		return false;
	}
	const int32 ReleasePP = GetPreMovePP(*Release, ActorId, ChargeId);
	const FBattleResolution ReleaseCommit =
		Release->CommitCurrentMoveAfterPreMoveGates();
	TestTrue(TEXT("Charged release passes the checkpoint"),
		ReleaseCommit.WasAccepted());
	TestEqual(TEXT("Charged release has no second PP cost"),
		GetPreMovePP(*Release, ActorId, ChargeId), ReleasePP);
	TestFalse(TEXT("Charged release publishes no PPConsumed"),
		HasEvent(ReleaseCommit, EBattleEventType::PPConsumed));

	TUniquePtr<FBattleEngine> DeniedRelease;
	if (!TestTrue(TEXT("Denied charged release is prepared"),
			PrepareRelease(DeniedRelease))
		|| !TestTrue(TEXT("Flinch is seeded at denied release gate"),
			TrySeedActionStartVolatile(
				*DeniedRelease,
				ActorId,
				FBattleVolatileRules::GetFlinchId())))
	{
		return false;
	}
	const int32 DeniedPP = GetPreMovePP(*DeniedRelease, ActorId, ChargeId);
	const FBattleResolution Denied =
		DeniedRelease->CommitCurrentMoveAfterPreMoveGates();
	TestTrue(TEXT("Denied charged release is accepted denial"), Denied.WasAccepted());
	TestEqual(TEXT("Denied charged release has no second PP cost"),
		GetPreMovePP(*DeniedRelease, ActorId, ChargeId), DeniedPP);
	TestFalse(TEXT("Denied release clears Charging atomically"),
		FindPreMoveVolatile(
			*DeniedRelease,
			ActorId,
			FBattleVolatileRules::GetChargingId()) != nullptr);
	TestFalse(TEXT("Denied release clears semi-invulnerability atomically"),
		FindPreMoveVolatile(
			*DeniedRelease,
			ActorId,
			FBattleVolatileRules::GetFlySemiInvulnerableId()) != nullptr);
	TestTrue(TEXT("Denied release completes the action"),
		HasEvent(Denied, EBattleEventType::ActionCompleted));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E4StatusFailureTest,
	"PokemonSolarus.Battle.ADR0002.3E4.PreMoveGates.Failure.StatusDispatchAndCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E4StatusFailureTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	const FTrainerId TrainerId = MakeNumericId<FTrainerId>(PlayerTrainerValue);

	TUniquePtr<FBattleEngine> Dispatch;
	if (!TestTrue(TEXT("Status-dispatch failure engine is created"),
			TryMakeSequenceEngine(MakePreMoveScenario(), {}, Dispatch))
		|| !TestTrue(TEXT("Status-dispatch Fight starts"),
			TryLockAndBeginPreMove(*Dispatch)))
	{
		return false;
	}
	FBattleBattlerState* DispatchActor =
		FBattleC09BWildFlowEngineFixture::GetMutableState(*Dispatch)
			.FindMutableBattler(ActorId);
	if (!TestNotNull(TEXT("Status-dispatch actor exists"), DispatchActor))
	{
		return false;
	}
	DispatchActor->MajorStatusId = FBattleMajorStatusRules::GetSleepId();
	const FActionStartCheckpointObservation DispatchBeforeAction =
		ObserveActionStartCheckpoint(*Dispatch, ActorId, TrainerId);
	const FAtomicSwitchCheckpointObservation DispatchBeforeMechanics =
		ObserveAtomicSwitchCheckpoint(*Dispatch);
	const FBattleResolution DispatchRejected =
		Dispatch->CommitCurrentMoveAfterPreMoveGates();
	bool bValid = VerifyRejectedPreMoveCheckpoint(
		*this,
		*Dispatch,
		DispatchBeforeAction,
		DispatchBeforeMechanics,
		EBattleRejectionReason::CheckpointPreparationFailed,
		DispatchRejected);

	TUniquePtr<FBattleEngine> Cleanup;
	if (!TestTrue(TEXT("Status-cleanup failure engine is created"),
			TryMakeSequenceEngine(MakePreMoveScenario(), {0}, Cleanup))
		|| !TestTrue(TEXT("Status-cleanup Fight starts"),
			TryLockAndBeginPreMove(*Cleanup))
		|| !TestTrue(TEXT("Freeze is seeded for cleanup failure"),
			TrySeedPreMoveMajorStatus(
				*Cleanup,
				ActorId,
				FBattleMajorStatusRules::GetFreezeId())))
	{
		return false;
	}
	FBattleC09BWildFlowEngineFixture::GetMutableState(*Cleanup)
		.NextTriggerReentrancyToken = TNumericLimits<uint64>::Max() - 1;
	const FActionStartCheckpointObservation CleanupBeforeAction =
		ObserveActionStartCheckpoint(*Cleanup, ActorId, TrainerId);
	const FAtomicSwitchCheckpointObservation CleanupBeforeMechanics =
		ObserveAtomicSwitchCheckpoint(*Cleanup);
	const FBattleResolution CleanupRejected =
		Cleanup->CommitCurrentMoveAfterPreMoveGates();
	bValid &= VerifyRejectedPreMoveCheckpoint(
		*this,
		*Cleanup,
		CleanupBeforeAction,
		CleanupBeforeMechanics,
		EBattleRejectionReason::CheckpointPreparationFailed,
		CleanupRejected);
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E4VolatileFailureTest,
	"PokemonSolarus.Battle.ADR0002.3E4.PreMoveGates.Failure.VolatileDispatchAndCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E4VolatileFailureTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	const FTrainerId TrainerId = MakeNumericId<FTrainerId>(PlayerTrainerValue);

	TUniquePtr<FBattleEngine> Dispatch;
	if (!TestTrue(TEXT("Volatile-dispatch failure engine is created"),
			TryMakeSequenceEngine(MakePreMoveScenario(), {}, Dispatch))
		|| !TestTrue(TEXT("Volatile-dispatch Fight starts"),
			TryLockAndBeginPreMove(*Dispatch)))
	{
		return false;
	}
	FBattleEngineState& DispatchState =
		FBattleC09BWildFlowEngineFixture::GetMutableState(*Dispatch);
	FBattleBattlerState* DispatchActor = DispatchState.FindMutableBattler(ActorId);
	if (!TestNotNull(TEXT("Volatile-dispatch actor exists"), DispatchActor))
	{
		return false;
	}
	FBattleConditionState MissingRegistration;
	MissingRegistration.ConditionId = FBattleVolatileRules::GetConfusionId();
	MissingRegistration.RemainingTurns = 3;
	MissingRegistration.LayerCount = 1;
	MissingRegistration.CreationOrdinal = DispatchState.NextConditionCreationOrdinal++;
	MissingRegistration.SourceBattlerId = ActorId;
	DispatchActor->Volatiles.Add(MissingRegistration);
	const FActionStartCheckpointObservation DispatchBeforeAction =
		ObserveActionStartCheckpoint(*Dispatch, ActorId, TrainerId);
	const FAtomicSwitchCheckpointObservation DispatchBeforeMechanics =
		ObserveAtomicSwitchCheckpoint(*Dispatch);
	const FBattleResolution DispatchRejected =
		Dispatch->CommitCurrentMoveAfterPreMoveGates();
	bool bValid = VerifyRejectedPreMoveCheckpoint(
		*this,
		*Dispatch,
		DispatchBeforeAction,
		DispatchBeforeMechanics,
		EBattleRejectionReason::CheckpointPreparationFailed,
		DispatchRejected);

	TUniquePtr<FBattleEngine> Cleanup;
	if (!TestTrue(TEXT("Volatile-cleanup failure engine is created"),
			TryMakeSequenceEngine(MakePreMoveScenario(), {}, Cleanup))
		|| !TestTrue(TEXT("Volatile-cleanup Fight starts"),
			TryLockAndBeginPreMove(*Cleanup))
		|| !TestTrue(TEXT("Flinch is seeded for cleanup failure"),
			TrySeedActionStartVolatile(
				*Cleanup,
				ActorId,
				FBattleVolatileRules::GetFlinchId())))
	{
		return false;
	}
	FBattleC09BWildFlowEngineFixture::GetMutableState(*Cleanup)
		.NextTriggerReentrancyToken = TNumericLimits<uint64>::Max() - 1;
	const FActionStartCheckpointObservation CleanupBeforeAction =
		ObserveActionStartCheckpoint(*Cleanup, ActorId, TrainerId);
	const FAtomicSwitchCheckpointObservation CleanupBeforeMechanics =
		ObserveAtomicSwitchCheckpoint(*Cleanup);
	const FBattleResolution CleanupRejected =
		Cleanup->CommitCurrentMoveAfterPreMoveGates();
	bValid &= VerifyRejectedPreMoveCheckpoint(
		*this,
		*Cleanup,
		CleanupBeforeAction,
		CleanupBeforeMechanics,
		EBattleRejectionReason::CheckpointPreparationFailed,
		CleanupRejected);
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E4ConfusionPreparationFailureTest,
	"PokemonSolarus.Battle.ADR0002.3E4.PreMoveGates.Failure.ConfusionDamageAndImmediateHeldItem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E4ConfusionPreparationFailureTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	const FTrainerId TrainerId = MakeNumericId<FTrainerId>(PlayerTrainerValue);

	TUniquePtr<FBattleEngine> Damage;
	FFaultBattleRandom* DamageRandom = nullptr;
	if (!TestTrue(TEXT("Confusion-damage failure engine is created"),
			TryMakeFaultEngine(
				MakePreMoveScenario(),
				{0, 0},
				EFaultRandomMode::Draw,
				Damage,
				DamageRandom,
				1))
		|| !TestNotNull(TEXT("Confusion-damage fault source is retained"), DamageRandom)
		|| !TestTrue(TEXT("Confusion-damage Fight starts"),
			TryLockAndBeginPreMove(*Damage))
		|| !TestTrue(TEXT("Confusion is seeded for damage failure"),
			TrySeedActionStartVolatile(
				*Damage,
				ActorId,
				FBattleVolatileRules::GetConfusionId(),
				FDefinitionId(),
				3)))
	{
		return false;
	}
	const FActionStartCheckpointObservation DamageBeforeAction =
		ObserveActionStartCheckpoint(*Damage, ActorId, TrainerId);
	const FAtomicSwitchCheckpointObservation DamageBeforeMechanics =
		ObserveAtomicSwitchCheckpoint(*Damage);
	const FBattleResolution DamageRejected =
		Damage->CommitCurrentMoveAfterPreMoveGates();
	bool bValid = VerifyRejectedPreMoveCheckpoint(
		*this,
		*Damage,
		DamageBeforeAction,
		DamageBeforeMechanics,
		EBattleRejectionReason::CheckpointRandomStageFailed,
		DamageRejected);
	bValid &= TestEqual(TEXT("Damage failure occurs after one staged gate draw"),
		DamageRandom->GetCounters().SuccessfulDraws, 1);

	TUniquePtr<FBattleEngine> Item;
	if (!TestTrue(TEXT("Immediate-item failure engine is created"),
			TryMakeSequenceEngine(
				MakePreMoveScenario(
					FMoveId(),
					200,
					FAbilityId(),
					FBattleItemRules::GetLumBerryId()),
				{0, 0},
				Item))
		|| !TestTrue(TEXT("Immediate-item Fight starts"),
			TryLockAndBeginPreMove(*Item))
		|| !TestTrue(TEXT("Confusion is seeded for immediate-item failure"),
			TrySeedActionStartVolatile(
				*Item,
				ActorId,
				FBattleVolatileRules::GetConfusionId(),
				FDefinitionId(),
				3)))
	{
		return false;
	}
	FBattleC09BWildFlowEngineFixture::GetMutableState(*Item)
		.NextTriggerReentrancyToken = TNumericLimits<uint64>::Max() - 1;
	const FActionStartCheckpointObservation ItemBeforeAction =
		ObserveActionStartCheckpoint(*Item, ActorId, TrainerId);
	const FAtomicSwitchCheckpointObservation ItemBeforeMechanics =
		ObserveAtomicSwitchCheckpoint(*Item);
	const FBattleResolution ItemRejected =
		Item->CommitCurrentMoveAfterPreMoveGates();
	bValid &= VerifyRejectedPreMoveCheckpoint(
		*this,
		*Item,
		ItemBeforeAction,
		ItemBeforeMechanics,
		EBattleRejectionReason::CheckpointPreparationFailed,
		ItemRejected);
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E4FaintRecoveryFailureTest,
	"PokemonSolarus.Battle.ADR0002.3E4.PreMoveGates.Failure.FaintOutcomeAndPartnerRecovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E4FaintRecoveryFailureTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	const FBattlerId TargetId = MakeNumericId<FBattlerId>(OpponentLeftValue);
	const FTrainerId TrainerId = MakeNumericId<FTrainerId>(PlayerTrainerValue);
	TUniquePtr<FBattleEngine> Engine;
	if (!TestTrue(TEXT("Faint-outcome failure engine is created"),
			TryMakeSequenceEngine(MakePreMoveScenario(FMoveId(), 1), {0, 0}, Engine))
		|| !TestTrue(TEXT("Faint-outcome Fight starts"),
			TryLockAndBeginPreMove(*Engine))
		|| !TestTrue(TEXT("Confusion is seeded for lethal projection"),
			TrySeedActionStartVolatile(
				*Engine,
				ActorId,
				FBattleVolatileRules::GetConfusionId(),
				FDefinitionId(),
				3)))
	{
		return false;
	}
	FBattleBattlerState* Target =
		FBattleC09BWildFlowEngineFixture::GetMutableState(*Engine)
			.FindMutableBattler(TargetId);
	if (!TestNotNull(TEXT("Faint-outcome target exists"), Target))
	{
		return false;
	}
	Target->bFaintTransitionPending = true;
	const FActionStartCheckpointObservation BeforeAction =
		ObserveActionStartCheckpoint(*Engine, ActorId, TrainerId);
	const FAtomicSwitchCheckpointObservation BeforeMechanics =
		ObserveAtomicSwitchCheckpoint(*Engine);
	const FBattleResolution Rejected =
		Engine->CommitCurrentMoveAfterPreMoveGates();
	bool bValid = VerifyRejectedPreMoveCheckpoint(
		*this,
		*Engine,
		BeforeAction,
		BeforeMechanics,
		EBattleRejectionReason::CheckpointPreparationFailed,
		Rejected);

	FAtomicWildScenario PartnerScenario = MakePreMoveScenario();
	PartnerScenario.Format = EBattleFormat::PartnerDouble;
	TUniquePtr<FBattleEngine> Partner;
	if (!TestTrue(TEXT("Partner-recovery failure engine is created"),
			TryMakeSequenceEngine(PartnerScenario, {}, Partner)))
	{
		return false;
	}
	const FBattleEngineState& PartnerState =
		FBattleC09BWildFlowEngineFixture::GetState(*Partner);
	const FBattleBattlerState* LivingPlayer = PartnerState.FindBattler(ActorId);
	const int32 LivingHP = LivingPlayer != nullptr ? LivingPlayer->CurrentHP : INDEX_NONE;
	FBattlePartnerTeamVictoryRecoveryPlan InvalidPlan;
	bValid &= TestFalse(TEXT("Invalid partner recovery preparation is recoverable"),
		FBattlePartnerFlow::TryApplyTeamVictoryRecovery(
			PartnerState,
			InvalidPlan));
	bValid &= TestTrue(TEXT("Partner recovery preparation failure is non-mutating"),
		LivingPlayer != nullptr
			&& LivingPlayer->CurrentHP == LivingHP
			&& !LivingPlayer->bFainted);
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E4RandomFailureTest,
	"PokemonSolarus.Battle.ADR0002.3E4.PreMoveGates.Failure.RandomTransactionCreateDrawCommit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E4RandomFailureTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	const FTrainerId TrainerId = MakeNumericId<FTrainerId>(PlayerTrainerValue);
	auto RunFault = [&](const EFaultRandomMode Mode,
		const uint32 Draw,
		const EBattleRejectionReason ExpectedReason) -> bool
	{
		TUniquePtr<FBattleEngine> Engine;
		FFaultBattleRandom* Random = nullptr;
		if (!TestTrue(TEXT("RNG-fault engine is created"),
				TryMakeFaultEngine(
					MakePreMoveScenario(),
					{Draw},
					Mode,
					Engine,
					Random))
			|| !TestNotNull(TEXT("RNG-fault source is retained"), Random)
			|| !TestTrue(TEXT("RNG-fault Fight starts"),
				TryLockAndBeginPreMove(*Engine))
			|| !TestTrue(TEXT("Paralysis is seeded for RNG fault"),
				TrySeedPreMoveMajorStatus(
					*Engine,
					ActorId,
					FBattleMajorStatusRules::GetParalysisId())))
		{
			return false;
		}
		const FActionStartCheckpointObservation BeforeAction =
			ObserveActionStartCheckpoint(*Engine, ActorId, TrainerId);
		const FAtomicSwitchCheckpointObservation BeforeMechanics =
			ObserveAtomicSwitchCheckpoint(*Engine);
		const FBattleResolution Rejected =
			Engine->CommitCurrentMoveAfterPreMoveGates();
		return VerifyRejectedPreMoveCheckpoint(
			*this,
			*Engine,
			BeforeAction,
			BeforeMechanics,
			ExpectedReason,
			Rejected);
	};
	bool bValid = RunFault(
		EFaultRandomMode::CreateTransaction,
		0,
		EBattleRejectionReason::CheckpointRandomStageFailed);
	bValid &= RunFault(
		EFaultRandomMode::Draw,
		0,
		EBattleRejectionReason::CheckpointRandomStageFailed);
	bValid &= RunFault(
		EFaultRandomMode::Commit,
		3,
		EBattleRejectionReason::RandomTransactionCommitFailed);
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E4PlanStaleFailureTest,
	"PokemonSolarus.Battle.ADR0002.3E4.PreMoveGates.Failure.PlanStagingAndStaleExactFightIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E4PlanStaleFailureTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	const FTrainerId TrainerId = MakeNumericId<FTrainerId>(PlayerTrainerValue);

	TUniquePtr<FBattleEngine> Plan;
	if (!TestTrue(TEXT("Plan-staging failure engine is created"),
			TryMakeSequenceEngine(MakePreMoveScenario(), {}, Plan))
		|| !TestTrue(TEXT("Plan-staging Fight starts"),
			TryLockAndBeginPreMove(*Plan)))
	{
		return false;
	}
	FBattleC09BWildFlowEngineFixture::GetMutableState(*Plan).NextEventOrdinal =
		TNumericLimits<uint64>::Max() - 1;
	const FActionStartCheckpointObservation PlanBeforeAction =
		ObserveActionStartCheckpoint(*Plan, ActorId, TrainerId);
	const FAtomicSwitchCheckpointObservation PlanBeforeMechanics =
		ObserveAtomicSwitchCheckpoint(*Plan);
	const FBattleResolution PlanRejected =
		Plan->CommitCurrentMoveAfterPreMoveGates();
	bool bValid = VerifyRejectedPreMoveCheckpoint(
		*this,
		*Plan,
		PlanBeforeAction,
		PlanBeforeMechanics,
		EBattleRejectionReason::CheckpointPreparationFailed,
		PlanRejected);

	TUniquePtr<FBattleEngine> Stale;
	FActionStartStaleRandom* StaleRandom = nullptr;
	if (!TestTrue(TEXT("Stale-Fight engine is created"),
			TryMakeActionStartStaleEngine(
				MakePreMoveScenario(),
				Stale,
				StaleRandom))
		|| !TestNotNull(TEXT("Stale-Fight random seam is retained"), StaleRandom)
		|| !TestTrue(TEXT("Stale-Fight action starts"),
			TryLockAndBeginPreMove(*Stale)))
	{
		return false;
	}
	const FActionStartCheckpointObservation StaleBefore =
		ObserveActionStartCheckpoint(*Stale, ActorId, TrainerId);
	const FAtomicSwitchCheckpointObservation StaleMechanicsBefore =
		ObserveAtomicSwitchCheckpoint(*Stale);
	FBattleEngineState& MutableStale =
		FBattleC09BWildFlowEngineFixture::GetMutableState(*Stale);
	const int32 ActionIndex = MutableStale.CurrentLockedActionIndex;
	const uint64 QueueOrdinalBefore =
		MutableStale.LockedActions[ActionIndex].QueueOrdinal;
	bool bInjectedMutation = false;
	StaleRandom->ArmAfterTraceRead(
		2,
		[EnginePtr = Stale.Get(), ActionIndex, &bInjectedMutation]()
		{
			FBattleEngineState& State =
				FBattleC09BWildFlowEngineFixture::GetMutableState(*EnginePtr);
			if (State.LockedActions.IsValidIndex(ActionIndex))
			{
				++State.LockedActions[ActionIndex].QueueOrdinal;
				bInjectedMutation = true;
			}
		});
	const FBattleResolution StaleRejected =
		Stale->CommitCurrentMoveAfterPreMoveGates();
	const int32 TraceReads = StaleRandom->GetReadsSinceArm();
	const bool bInjected = StaleRandom->WasInjected();
	StaleRandom->Disarm();
	const FBattleEngineState& StaleState =
		FBattleC09BWildFlowEngineFixture::GetState(*Stale);
	const FActionStartCheckpointObservation StaleAfter =
		ObserveActionStartCheckpoint(*Stale, ActorId, TrainerId);
	bValid &= TestTrue(TEXT("Exact Fight identity changes only at final recheck"),
		bInjected && bInjectedMutation && TraceReads == 2);
	bValid &= TestFalse(TEXT("Stale exact Fight identity rejects"),
		StaleRejected.WasAccepted());
	bValid &= TestEqual(TEXT("Stale Fight rejection is typed"),
		StaleRejected.GetRejection().Reason,
		EBattleRejectionReason::StaleCheckpointIdentity);
	bValid &= TestTrue(TEXT("Concurrent exact action change remains authoritative"),
		StaleState.LockedActions[ActionIndex].QueueOrdinal == QueueOrdinalBefore + 1);
	bValid &= TestEqual(TEXT("Stale Fight rejection preserves PP"),
		StaleAfter.TotalMovePP, StaleBefore.TotalMovePP);
	bValid &= TestEqual(TEXT("Stale Fight rejection preserves cursor"),
		StaleAfter.ActionIndex, StaleBefore.ActionIndex);
	bValid &= TestEqual(TEXT("Stale Fight rejection preserves gameplay RNG"),
		StaleAfter.RandomTraceCount, StaleBefore.RandomTraceCount);
	bValid &= TestTrue(TEXT("Stale Fight rejection preserves non-concurrent mechanics"),
		AreAtomicSwitchMechanicsIdentical(
			ObserveAtomicSwitchCheckpoint(*Stale),
			StaleMechanicsBefore));
	bValid &= TestTrue(TEXT("Stale Fight rejection is appended exactly once"),
		IsReturnedResolutionAppendedExactlyOnce(*Stale, StaleRejected));
	bValid &= TestFalse(TEXT("Stale Fight rejection publishes no success fact"),
		StaleRejected.GetEvents().ContainsByPredicate(
			[](const FBattleEvent& Event)
			{
				return IsPreMoveSuccessEvent(Event.GetType());
			}));
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E4RejectionPreservationTest,
	"PokemonSolarus.Battle.ADR0002.3E4.PreMoveGates.Failure.RejectionExactOncePreservesStateAndReplayFacts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E4RejectionPreservationTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	const FBattlerId TargetId = MakeNumericId<FBattlerId>(OpponentLeftValue);
	const FTrainerId TrainerId = MakeNumericId<FTrainerId>(PlayerTrainerValue);
	FAtomicWildScenario Scenario = MakePreMoveScenario(
		FMoveId(),
		150,
		FAbilityId(),
		FBattleItemRules::GetChoiceBandId());
	Scenario.TargetCurrentHP = 137;
	TUniquePtr<FBattleEngine> Engine;
	if (!TestTrue(TEXT("Preservation engine is created"),
			TryMakeSequenceEngine(Scenario, {}, Engine))
		|| !TestTrue(TEXT("Preservation Fight starts"),
			TryLockAndBeginPreMove(*Engine))
		|| !TestTrue(TEXT("Preserved actor volatile is seeded"),
			TrySeedActionStartVolatile(
				*Engine,
				ActorId,
				FBattleVolatileRules::GetFlinchId()))
		|| !TestTrue(TEXT("Preserved target status is seeded"),
			TrySeedPreMoveMajorStatus(
				*Engine,
				TargetId,
				FBattleMajorStatusRules::GetParalysisId())))
	{
		return false;
	}
	FBattleBattlerState* Actor =
		FBattleC09BWildFlowEngineFixture::GetMutableState(*Engine)
			.FindMutableBattler(ActorId);
	if (!TestNotNull(TEXT("Preservation actor exists"), Actor))
	{
		return false;
	}
	Actor->MajorStatusId = FBattleMajorStatusRules::GetSleepId();
	const FActionStartCheckpointObservation BeforeAction =
		ObserveActionStartCheckpoint(*Engine, ActorId, TrainerId);
	const FAtomicSwitchCheckpointObservation BeforeMechanics =
		ObserveAtomicSwitchCheckpoint(*Engine);
	const FBattleResolution Rejected =
		Engine->CommitCurrentMoveAfterPreMoveGates();
	return VerifyRejectedPreMoveCheckpoint(
		*this,
		*Engine,
		BeforeAction,
		BeforeMechanics,
		EBattleRejectionReason::CheckpointPreparationFailed,
		Rejected);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E5DeterministicResolvedAtomicTest,
	"PokemonSolarus.Battle.ADR0002.3E5.Targets.Success.DeterministicResolvedAtomic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E5DeterministicResolvedAtomicTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	const FMoveId MoveId = MakeDefinitionId<FMoveId>(TargetProbeMoveName);
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	TUniquePtr<FBattleEngine> Engine;
	FStrictBattleRandom* Random = nullptr;
	if (!TestTrue(TEXT("Deterministic target engine is created"),
			TryMakeStrictEngine(MakePreMoveScenario(), {}, Engine, Random))
		|| !TestNotNull(TEXT("Deterministic strict RNG is retained"), Random)
		|| !TestTrue(TEXT("Deterministic target checkpoint follows committed PP"),
			TryPrepareTargetCheckpoint(*Engine, MoveId)))
	{
		return false;
	}

	const FTargetCheckpointObservation Before = ObserveTargetCheckpoint(*Engine);
	const int32 PPBefore = GetPreMovePP(*Engine, ActorId, MoveId);
	const FBattleResolution Resolved = Engine->ResolveCurrentMoveTargets();
	const FBattleEngineState& State =
		FBattleC09BWildFlowEngineFixture::GetState(*Engine);
	const FBattleLockedActionState& Action =
		State.LockedActions[Before.Action.ActionIndex];
	bool bValid = TestTrue(TEXT("Deterministic target checkpoint is accepted"),
		Resolved.WasAccepted());
	bValid &= TestTrue(TEXT("Deterministic target checkpoint uses no RNG"),
		Random->IsExact() && Random->GetTrace().IsEmpty());
	bValid &= TestEqual(TEXT("3E4 PP remains spent after target resolution"),
		GetPreMovePP(*Engine, ActorId, MoveId), PPBefore);
	bValid &= TestTrue(TEXT("Target and TargetsResolved event commit together"),
		Action.TargetResolution.IsSet()
			&& Action.TargetResolution.GetValue().Outcome
				== EBattleTargetResolutionOutcome::Resolved
			&& Action.TargetResolution.GetValue().Targets.Num() == 1
			&& Resolved.GetEvents().Num() == 1
			&& Resolved.GetEvents()[0].GetType()
				== EBattleEventType::TargetsResolved
			&& Resolved.GetEvents()[0].GetTargets().Num() == 1);
	bValid &= TestTrue(TEXT("Resolved action remains current and ready for effects"),
		State.CurrentLockedActionIndex == Before.Action.ActionIndex
			&& !Action.bFinished
			&& Action.EffectExecutionState
				== EBattleLockedEffectExecutionState::Pending);
	bValid &= TestEqual(TEXT("Accepted target checkpoint advances version once"),
		State.StateVersion, Before.Action.StateVersion + 1);
	bValid &= TestTrue(TEXT("Accepted target resolution is published exactly once"),
		IsReturnedResolutionAppendedExactlyOnce(*Engine, Resolved));
	bValid &= TestEqual(TEXT("Accepted target replay remains schema 6"),
		Engine->ExportReplayRecord().GetSchemaVersion(), static_cast<uint32>(6));
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E5RandomSuccessTest,
	"PokemonSolarus.Battle.ADR0002.3E5.Targets.Success.RandomTwoAndOneCandidateTransactional",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E5RandomSuccessTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FMoveId MoveId = MakeDefinitionId<FMoveId>(RandomTargetProbeMoveName);
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	auto RunCase = [&](const EBattleFormat Format,
		const uint32 Maximum,
		const uint32 Result,
		const FBattlerId ExpectedTarget) -> bool
	{
		FAtomicWildScenario Scenario = MakePreMoveScenario(MoveId);
		Scenario.Format = Format;
		TUniquePtr<FBattleEngine> Engine;
		FStrictBattleRandom* Random = nullptr;
		if (!TestTrue(TEXT("Random target engine is created"),
				TryMakeStrictEngine(
					Scenario,
					{MakeTargetExpectedDraw(Maximum, Result)},
					Engine,
					Random))
			|| !TestNotNull(TEXT("Random strict RNG is retained"), Random)
			|| !TestTrue(TEXT("Random target checkpoint follows committed PP"),
				TryPrepareTargetCheckpoint(*Engine, MoveId)))
		{
			return false;
		}
		const int32 PPBefore = GetPreMovePP(*Engine, ActorId, MoveId);
		const FBattleResolution Resolution = Engine->ResolveCurrentMoveTargets();
		const TOptional<FBattleLockedAction> Current = Engine->GetCurrentLockedAction();
		bool bCase = TestTrue(TEXT("Random target checkpoint is accepted"),
			Resolution.WasAccepted());
		bCase &= TestTrue(TEXT("Random target draw contract is exact"),
			Random->IsExact()
				&& Random->GetTrace().Num() == 1
				&& Random->GetTrace()[0].InclusiveMinimum == 0
				&& Random->GetTrace()[0].InclusiveMaximum == Maximum
				&& Random->GetTrace()[0].RulePurpose
					== FBattleTargetResolver::GetRandomLegalOpponentRulePurpose());
		bCase &= TestTrue(TEXT("Committed random trace and frozen target agree"),
			Current.IsSet()
				&& Current->TargetResolution.IsSet()
				&& Current->TargetResolution.GetValue().Targets.Num() == 1
				&& Current->TargetResolution.GetValue().Targets[0].GetBattler().BattlerId
					== ExpectedTarget);
		bCase &= TestEqual(TEXT("Random targeting does not charge PP again"),
			GetPreMovePP(*Engine, ActorId, MoveId), PPBefore);
		bCase &= TestTrue(TEXT("Random target resolution publishes exactly once"),
			IsReturnedResolutionAppendedExactlyOnce(*Engine, Resolution));
		return bCase;
	};

	bool bValid = RunCase(
		EBattleFormat::Double,
		1,
		1,
		MakeNumericId<FBattlerId>(OpponentRightValue));
	bValid &= RunCase(
		EBattleFormat::Single,
		0,
		0,
		MakeNumericId<FBattlerId>(OpponentLeftValue));
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E5EmptyRandomTest,
	"PokemonSolarus.Battle.ADR0002.3E5.Targets.Success.EmptyRandomNoDrawNoTargetExactOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E5EmptyRandomTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FMoveId MoveId = MakeDefinitionId<FMoveId>(RandomTargetProbeMoveName);
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	const FBattlerId TargetId = MakeNumericId<FBattlerId>(OpponentLeftValue);
	TUniquePtr<FBattleEngine> Engine;
	FFaultBattleRandom* Random = nullptr;
	if (!TestTrue(TEXT("Empty-random engine is created"),
			TryMakeStrictFaultEngine(
				MakePreMoveScenario(MoveId),
				{},
				EFaultRandomMode::PassThrough,
				Engine,
				Random))
		|| !TestNotNull(TEXT("Empty-random transaction source is retained"), Random)
		|| !TestTrue(TEXT("Empty-random target checkpoint follows committed PP"),
			TryPrepareTargetCheckpoint(*Engine, MoveId))
		|| !TestTrue(TEXT("Empty-random legal set is removed"),
			TryMarkTargetFainted(*Engine, TargetId)))
	{
		return false;
	}

	const FTargetCheckpointObservation Before = ObserveTargetCheckpoint(*Engine);
	const int32 PPBefore = GetPreMovePP(*Engine, ActorId, MoveId);
	const FBattleResolution NoTarget = Engine->ResolveCurrentMoveTargets();
	const FBattleEngineState& State =
		FBattleC09BWildFlowEngineFixture::GetState(*Engine);
	const FBattleLockedActionState& Action =
		State.LockedActions[Before.Action.ActionIndex];
	const TArray<EBattleEventType> ExpectedOrder = {
		EBattleEventType::TargetsResolved,
		EBattleEventType::ActionCanceled,
		EBattleEventType::ActionCompleted};
	bool bValid = TestTrue(TEXT("Empty random set is accepted as no target"),
		NoTarget.WasAccepted());
	bValid &= TestTrue(TEXT("Empty random set creates and commits an empty transaction"),
		Random->IsExact()
			&& Random->GetCounters().TransactionCreateAttempts == 1
			&& Random->GetCounters().DrawAttempts == 0
			&& Random->GetCounters().CommitAttempts == 1
			&& Random->GetTrace().IsEmpty());
	bValid &= TestTrue(TEXT("No-target action completes and advances exactly once"),
		Action.TargetResolution.IsSet()
			&& Action.TargetResolution.GetValue().Outcome
				== EBattleTargetResolutionOutcome::NoLegalTarget
			&& Action.bFinished
			&& State.CurrentLockedActionIndex == Before.Action.ActionIndex + 1);
	bValid &= TestTrue(TEXT("No-target event order is exact"),
		HasExactTargetEventOrder(NoTarget, ExpectedOrder));
	bValid &= TestEqual(TEXT("Empty random no-target retains committed PP"),
		GetPreMovePP(*Engine, ActorId, MoveId), PPBefore);
	bValid &= TestTrue(TEXT("Empty random no-target publishes exactly once"),
		IsReturnedResolutionAppendedExactlyOnce(*Engine, NoTarget));
	bValid &= TestEqual(TEXT("Empty random replay remains schema 6"),
		Engine->ExportReplayRecord().GetSchemaVersion(), static_cast<uint32>(6));
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E5FaintedFallbackTest,
	"PokemonSolarus.Battle.ADR0002.3E5.Targets.Success.FaintedFallbackNoDraw",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E5FaintedFallbackTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FMoveId MoveId = MakeDefinitionId<FMoveId>(TargetProbeMoveName);
	TUniquePtr<FBattleEngine> Engine;
	FStrictBattleRandom* Random = nullptr;
	FAtomicWildScenario Scenario = MakePreMoveScenario();
	Scenario.Format = EBattleFormat::Double;
	if (!TestTrue(TEXT("Fainted-fallback engine is created"),
			TryMakeStrictEngine(Scenario, {}, Engine, Random))
		|| !TestNotNull(TEXT("Fainted-fallback strict RNG is retained"), Random)
		|| !TestTrue(TEXT("Fainted-fallback target checkpoint is prepared"),
			TryPrepareTargetCheckpoint(*Engine, MoveId))
		|| !TestTrue(TEXT("Originally selected opponent is fainted"),
			TryMarkTargetFainted(
				*Engine,
				MakeNumericId<FBattlerId>(OpponentLeftValue))))
	{
		return false;
	}

	const FBattleResolution Resolution = Engine->ResolveCurrentMoveTargets();
	const TOptional<FBattleLockedAction> Current = Engine->GetCurrentLockedAction();
	bool bValid = TestTrue(TEXT("Fainted-target fallback is accepted"),
		Resolution.WasAccepted());
	bValid &= TestTrue(TEXT("Fainted-target fallback consumes no RNG"),
		Random->IsExact() && Random->GetTrace().IsEmpty());
	bValid &= TestTrue(TEXT("Fainted-target fallback freezes the other living opponent"),
		Current.IsSet()
			&& Current->TargetResolution.IsSet()
			&& Current->TargetResolution.GetValue().Outcome
				== EBattleTargetResolutionOutcome::Resolved
			&& Current->TargetResolution.GetValue().bWasRedirected
			&& Current->TargetResolution.GetValue().bUsedFaintedTargetFallback
			&& Current->TargetResolution.GetValue().Targets.Num() == 1
			&& Current->TargetResolution.GetValue().Targets[0].GetBattler().BattlerId
				== MakeNumericId<FBattlerId>(OpponentRightValue));
	bValid &= TestTrue(TEXT("Fainted fallback publishes target and event exactly once"),
		Resolution.GetEvents().Num() == 1
			&& Resolution.GetEvents()[0].GetType()
				== EBattleEventType::TargetsResolved
			&& IsReturnedResolutionAppendedExactlyOnce(*Engine, Resolution));
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E5ChargedNoTargetTest,
	"PokemonSolarus.Battle.ADR0002.3E5.Targets.Success.ChargedReleaseNoTargetCleanupAndPP",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E5ChargedNoTargetTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FMoveId MoveId = MakeDefinitionId<FMoveId>(ChargeProbeMoveName);
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	TUniquePtr<FBattleEngine> Engine;
	FStrictBattleRandom* Random = nullptr;
	if (!TestTrue(TEXT("Charged no-target engine is created"),
			TryMakeStrictEngine(MakePreMoveScenario(MoveId), {}, Engine, Random))
		|| !TestNotNull(TEXT("Charged no-target strict RNG is retained"), Random)
		|| !TestTrue(TEXT("Charged release reaches target checkpoint"),
			TrySeedChargedReleaseTargetCheckpoint(*Engine, MoveId))
		|| !TestTrue(TEXT("Charged release loses its legal target"),
			TryMarkTargetFainted(
				*Engine,
				MakeNumericId<FBattlerId>(OpponentLeftValue))))
	{
		return false;
	}

	const int32 PPBefore = GetPreMovePP(*Engine, ActorId, MoveId);
	const FBattleResolution NoTarget = Engine->ResolveCurrentMoveTargets();
	const FBattleEngineState& State =
		FBattleC09BWildFlowEngineFixture::GetState(*Engine);
	bool bValid = TestTrue(TEXT("Charged release no-target is accepted"),
		NoTarget.WasAccepted());
	bValid &= TestEqual(TEXT("Charged release no-target has no second PP cost"),
		GetPreMovePP(*Engine, ActorId, MoveId), PPBefore);
	bValid &= TestFalse(TEXT("Charged release no-target emits no PP event"),
		HasEvent(NoTarget, EBattleEventType::PPConsumed));
	bValid &= TestFalse(TEXT("Charged release no-target clears Charging"),
		FindPreMoveVolatile(
			*Engine,
			ActorId,
			FBattleVolatileRules::GetChargingId()) != nullptr);
	bValid &= TestFalse(TEXT("Charged release no-target clears semi-invulnerability"),
		FindPreMoveVolatile(
			*Engine,
			ActorId,
			FBattleVolatileRules::GetFlySemiInvulnerableId()) != nullptr);
	bValid &= TestEqual(TEXT("Charging trigger registrations are cleaned"),
		CountActionStartTriggerRegistrations(
			State,
			FBattleVolatileRules::GetChargingId().GetDefinitionId()), 0);
	bValid &= TestEqual(TEXT("Semi-invulnerability trigger registrations are cleaned"),
		CountActionStartTriggerRegistrations(
			State,
			FBattleVolatileRules::GetFlySemiInvulnerableId().GetDefinitionId()), 0);
	bValid &= TestTrue(TEXT("Charged no-target consumes no targeting RNG"),
		Random->IsExact() && Random->GetTrace().IsEmpty());
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E5NoTargetBoundaryTest,
	"PokemonSolarus.Battle.ADR0002.3E5.Targets.Success.NoTargetBoundaryCursorEventOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E5NoTargetBoundaryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FMoveId MoveId = MakeDefinitionId<FMoveId>(TargetProbeMoveName);
	const FBattlerId OpponentLeft = MakeNumericId<FBattlerId>(OpponentLeftValue);

	FAtomicWildScenario EndScenario = MakePreMoveScenario();
	EndScenario.PlayerLeftSpeed = 1;
	TUniquePtr<FBattleEngine> EndEngine;
	FStrictBattleRandom* EndRandom = nullptr;
	if (!TestTrue(TEXT("End-of-turn boundary engine is created"),
			TryMakeStrictEngine(EndScenario, {}, EndEngine, EndRandom))
		|| !TestTrue(TEXT("Last action reaches target checkpoint"),
			TryPrepareLastTargetCheckpoint(*EndEngine, MoveId))
		|| !TestTrue(TEXT("Last action loses its legal target"),
			TryMarkTargetFainted(*EndEngine, OpponentLeft)))
	{
		return false;
	}
	const FTargetCheckpointObservation EndBefore = ObserveTargetCheckpoint(*EndEngine);
	const FBattleResolution EndResolution = EndEngine->ResolveCurrentMoveTargets();
	const FBattleEngineState& EndState =
		FBattleC09BWildFlowEngineFixture::GetState(*EndEngine);
	const TArray<EBattleEventType> EndOrder = {
		EBattleEventType::TargetsResolved,
		EBattleEventType::ActionCanceled,
		EBattleEventType::ActionCompleted};
	bool bValid = TestTrue(TEXT("Last no-target action is accepted"),
		EndResolution.WasAccepted());
	bValid &= TestTrue(TEXT("Last no-target action enters EndOfTurn exactly once"),
		EndState.Phase == EBattlePhase::EndOfTurn
			&& EndState.CurrentLockedActionIndex == EndState.LockedActions.Num()
			&& EndState.CurrentLockedActionIndex == EndBefore.Action.ActionIndex + 1);
	bValid &= TestTrue(TEXT("End-of-turn no-target event order is exact"),
		HasExactTargetEventOrder(EndResolution, EndOrder));

	FAtomicWildScenario ReplacementScenario = MakePreMoveScenario();
	ReplacementScenario.Format = EBattleFormat::Double;
	ReplacementScenario.bVoluntarySwitchFlow = true;
	ReplacementScenario.PlayerLeftSpeed = 1;
	TUniquePtr<FBattleEngine> ReplacementEngine;
	FStrictBattleRandom* ReplacementRandom = nullptr;
	if (!TestTrue(TEXT("Replacement-boundary engine is created"),
			TryMakeStrictEngine(
				ReplacementScenario,
				{},
				ReplacementEngine,
				ReplacementRandom))
		|| !TestTrue(TEXT("Replacement last action reaches target checkpoint"),
			TryPrepareLastTargetCheckpoint(*ReplacementEngine, MoveId))
		|| !TestTrue(TEXT("Replacement case faints opponent Left"),
			TryMarkTargetFainted(*ReplacementEngine, OpponentLeft))
		|| !TestTrue(TEXT("Replacement case faints opponent Right"),
			TryMarkTargetFainted(
				*ReplacementEngine,
				MakeNumericId<FBattlerId>(OpponentRightValue)))
		|| !TestTrue(TEXT("Replacement case faints player Right"),
			TryMarkTargetFainted(
				*ReplacementEngine,
				MakeNumericId<FBattlerId>(PlayerRightValue)))
		|| !TestTrue(TEXT("Replacement case opens player Right slot"),
			TryClearTargetActivePosition(
				*ReplacementEngine,
				MakeActiveSlotId(EBattleSide::Player, EBattlePosition::Right))))
	{
		return false;
	}
	const FBattleResolution Replacement =
		ReplacementEngine->ResolveCurrentMoveTargets();
	const FBattleEngineState& ReplacementState =
		FBattleC09BWildFlowEngineFixture::GetState(*ReplacementEngine);
	const TArray<EBattleEventType> ReplacementOrder = {
		EBattleEventType::TargetsResolved,
		EBattleEventType::ActionCanceled,
		EBattleEventType::ActionCompleted,
		EBattleEventType::ReplacementRequired};
	bValid &= TestTrue(TEXT("No-target replacement boundary is accepted"),
		Replacement.WasAccepted());
	bValid &= TestTrue(TEXT("Replacement boundary stages pending request facts"),
		ReplacementState.Phase == EBattlePhase::MandatoryReplacement
			&& ReplacementState.PendingReplacements.Num() == 1
			&& ReplacementState.PendingDecisionRequests.Num() == 1
			&& ReplacementState.PendingDecision.IsSet());
	bValid &= TestTrue(TEXT("Boundary events follow target cancellation and completion"),
		HasExactTargetEventOrder(Replacement, ReplacementOrder));
	bValid &= TestTrue(TEXT("Both deterministic boundary cases consume no RNG"),
		EndRandom != nullptr
			&& ReplacementRandom != nullptr
			&& EndRandom->IsExact()
			&& ReplacementRandom->IsExact());
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E5TargetPreparationFailureTest,
	"PokemonSolarus.Battle.ADR0002.3E5.Targets.Failure.TargetSpecAndResolverPreparation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E5TargetPreparationFailureTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	const FMoveId MoveId = MakeDefinitionId<FMoveId>(TargetProbeMoveName);
	TUniquePtr<FBattleEngine> Engine;
	FStrictBattleRandom* Random = nullptr;
	if (!TestTrue(TEXT("Target-preparation failure engine is created"),
			TryMakeStrictEngine(MakePreMoveScenario(), {}, Engine, Random))
		|| !TestTrue(TEXT("Target-preparation failure follows committed PP"),
			TryPrepareTargetCheckpoint(*Engine, MoveId)))
	{
		return false;
	}
	FBattleEngineState& MutableState =
		FBattleC09BWildFlowEngineFixture::GetMutableState(*Engine);
	if (!MutableState.LockedActions.IsValidIndex(MutableState.CurrentLockedActionIndex))
	{
		return false;
	}
	MutableState.LockedActions[MutableState.CurrentLockedActionIndex].TargetClass =
		static_cast<EBattleTargetClass>(255);
	const FTargetCheckpointObservation Before = ObserveTargetCheckpoint(*Engine);
	const FBattleResolution Rejected = Engine->ResolveCurrentMoveTargets();
	bool bValid = VerifyRejectedTargetCheckpoint(
		*this,
		*Engine,
		Before,
		EBattleRejectionReason::CheckpointPreparationFailed,
		Rejected);
	bValid &= TestTrue(TEXT("Rejected invalid target spec consumes no RNG"),
		Random != nullptr && Random->IsExact() && Random->GetTrace().IsEmpty());
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E5ChargeCleanupFailureTest,
	"PokemonSolarus.Battle.ADR0002.3E5.Targets.Failure.ChargeAndTriggerCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E5ChargeCleanupFailureTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	const FMoveId MoveId = MakeDefinitionId<FMoveId>(ChargeProbeMoveName);
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	TUniquePtr<FBattleEngine> Engine;
	FStrictBattleRandom* Random = nullptr;
	if (!TestTrue(TEXT("Charge-cleanup failure engine is created"),
			TryMakeStrictEngine(MakePreMoveScenario(MoveId), {}, Engine, Random))
		|| !TestTrue(TEXT("Charge-cleanup failure reaches target checkpoint"),
			TrySeedChargedReleaseTargetCheckpoint(*Engine, MoveId))
		|| !TestTrue(TEXT("Charge-cleanup failure has no legal target"),
			TryMarkTargetFainted(
				*Engine,
				MakeNumericId<FBattlerId>(OpponentLeftValue))))
	{
		return false;
	}
	FBattleC09BWildFlowEngineFixture::GetMutableState(*Engine)
		.NextTriggerReentrancyToken = TNumericLimits<uint64>::Max();
	const FTargetCheckpointObservation Before = ObserveTargetCheckpoint(*Engine);
	const int32 PPBefore = GetPreMovePP(*Engine, ActorId, MoveId);
	const FBattleResolution Rejected = Engine->ResolveCurrentMoveTargets();
	bool bValid = VerifyRejectedTargetCheckpoint(
		*this,
		*Engine,
		Before,
		EBattleRejectionReason::CheckpointPreparationFailed,
		Rejected);
	bValid &= TestEqual(TEXT("Failed charge cleanup retains committed PP"),
		GetPreMovePP(*Engine, ActorId, MoveId), PPBefore);
	bValid &= TestTrue(TEXT("Failed charge cleanup retains Charging"),
		FindPreMoveVolatile(
			*Engine,
			ActorId,
			FBattleVolatileRules::GetChargingId()) != nullptr);
	bValid &= TestTrue(TEXT("Failed charge cleanup retains semi-invulnerability"),
		FindPreMoveVolatile(
			*Engine,
			ActorId,
			FBattleVolatileRules::GetFlySemiInvulnerableId()) != nullptr);
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E5BoundaryPlanFailureTest,
	"PokemonSolarus.Battle.ADR0002.3E5.Targets.Failure.BoundaryAndPlanStaging",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E5BoundaryPlanFailureTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	const FMoveId MoveId = MakeDefinitionId<FMoveId>(TargetProbeMoveName);

	TUniquePtr<FBattleEngine> PlanEngine;
	FStrictBattleRandom* PlanRandom = nullptr;
	if (!TestTrue(TEXT("Plan-staging failure engine is created"),
			TryMakeStrictEngine(MakePreMoveScenario(), {}, PlanEngine, PlanRandom))
		|| !TestTrue(TEXT("Plan-staging failure follows committed PP"),
			TryPrepareTargetCheckpoint(*PlanEngine, MoveId)))
	{
		return false;
	}
	FBattleC09BWildFlowEngineFixture::GetMutableState(*PlanEngine).StateVersion =
		TNumericLimits<uint64>::Max();
	const FTargetCheckpointObservation PlanBefore =
		ObserveTargetCheckpoint(*PlanEngine);
	const FBattleResolution PlanRejected =
		PlanEngine->ResolveCurrentMoveTargets();
	bool bValid = VerifyRejectedTargetCheckpoint(
		*this,
		*PlanEngine,
		PlanBefore,
		EBattleRejectionReason::CheckpointPreparationFailed,
		PlanRejected);

	FAtomicWildScenario BoundaryScenario = MakePreMoveScenario();
	BoundaryScenario.Format = EBattleFormat::Double;
	BoundaryScenario.bVoluntarySwitchFlow = true;
	BoundaryScenario.PlayerLeftSpeed = 1;
	TUniquePtr<FBattleEngine> BoundaryEngine;
	FStrictBattleRandom* BoundaryRandom = nullptr;
	if (!TestTrue(TEXT("Boundary-preparation failure engine is created"),
			TryMakeStrictEngine(
				BoundaryScenario,
				{},
				BoundaryEngine,
				BoundaryRandom))
		|| !TestTrue(TEXT("Boundary-preparation failure reaches last target checkpoint"),
			TryPrepareLastTargetCheckpoint(*BoundaryEngine, MoveId))
		|| !TestTrue(TEXT("Boundary failure faints opponent Left"),
			TryMarkTargetFainted(
				*BoundaryEngine,
				MakeNumericId<FBattlerId>(OpponentLeftValue)))
		|| !TestTrue(TEXT("Boundary failure faints opponent Right"),
			TryMarkTargetFainted(
				*BoundaryEngine,
				MakeNumericId<FBattlerId>(OpponentRightValue)))
		|| !TestTrue(TEXT("Boundary failure faints player Right"),
			TryMarkTargetFainted(
				*BoundaryEngine,
				MakeNumericId<FBattlerId>(PlayerRightValue)))
		|| !TestTrue(TEXT("Boundary failure opens replacement slot"),
			TryClearTargetActivePosition(
				*BoundaryEngine,
				MakeActiveSlotId(EBattleSide::Player, EBattlePosition::Right))))
	{
		return false;
	}
	FBattleC09BWildFlowEngineFixture::GetMutableState(*BoundaryEngine).StateVersion =
		TNumericLimits<uint64>::Max();
	const FTargetCheckpointObservation BoundaryBefore =
		ObserveTargetCheckpoint(*BoundaryEngine);
	const FBattleResolution BoundaryRejected =
		BoundaryEngine->ResolveCurrentMoveTargets();
	bValid &= VerifyRejectedTargetCheckpoint(
		*this,
		*BoundaryEngine,
		BoundaryBefore,
		EBattleRejectionReason::CheckpointPreparationFailed,
		BoundaryRejected);
	bValid &= TestTrue(TEXT("Both plan and boundary failures consume no RNG"),
		PlanRandom != nullptr
			&& BoundaryRandom != nullptr
			&& PlanRandom->IsExact()
			&& BoundaryRandom->IsExact());
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E5RandomFailureTest,
	"PokemonSolarus.Battle.ADR0002.3E5.Targets.Failure.RandomTransactionCreateDrawCommit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E5RandomFailureTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FMoveId MoveId = MakeDefinitionId<FMoveId>(RandomTargetProbeMoveName);
	auto RunFault = [&](const EFaultRandomMode Mode,
		const EBattleRejectionReason ExpectedReason) -> bool
	{
		TUniquePtr<FBattleEngine> Engine;
		FFaultBattleRandom* Random = nullptr;
		if (!TestTrue(TEXT("Target RNG-fault engine is created"),
				TryMakeStrictFaultEngine(
					MakePreMoveScenario(MoveId),
					{MakeTargetExpectedDraw(0, 0)},
					Mode,
					Engine,
					Random))
			|| !TestNotNull(TEXT("Target RNG-fault source is retained"), Random)
			|| !TestTrue(TEXT("Target RNG-fault follows committed PP"),
				TryPrepareTargetCheckpoint(*Engine, MoveId)))
		{
			return false;
		}
		const FTargetCheckpointObservation Before = ObserveTargetCheckpoint(*Engine);
		const FBattleResolution Rejected = Engine->ResolveCurrentMoveTargets();
		bool bCase = VerifyRejectedTargetCheckpoint(
			*this,
			*Engine,
			Before,
			ExpectedReason,
			Rejected);
		const FFaultRandomCounters& Counters = Random->GetCounters();
		bCase &= TestEqual(TEXT("Target RNG failure creates at most one transaction"),
			Counters.TransactionCreateAttempts, 1);
		if (Mode == EFaultRandomMode::CreateTransaction)
		{
			bCase &= TestTrue(TEXT("Creation failure performs no draw or commit"),
				Counters.DrawAttempts == 0 && Counters.CommitAttempts == 0);
		}
		else if (Mode == EFaultRandomMode::Draw)
		{
			bCase &= TestTrue(TEXT("Staged-draw failure never reaches commit"),
				Counters.DrawAttempts == 1
					&& Counters.SuccessfulDraws == 0
					&& Counters.CommitAttempts == 0);
		}
		else
		{
			bCase &= TestTrue(TEXT("Commit failure follows one successful staged draw"),
				Counters.DrawAttempts == 1
					&& Counters.SuccessfulDraws == 1
					&& Counters.CommitAttempts == 1);
		}
		return bCase;
	};

	bool bValid = RunFault(
		EFaultRandomMode::CreateTransaction,
		EBattleRejectionReason::CheckpointRandomStageFailed);
	bValid &= RunFault(
		EFaultRandomMode::Draw,
		EBattleRejectionReason::CheckpointRandomStageFailed);
	bValid &= RunFault(
		EFaultRandomMode::Commit,
		EBattleRejectionReason::RandomTransactionCommitFailed);
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E5StaleIdentityTest,
	"PokemonSolarus.Battle.ADR0002.3E5.Targets.Failure.StaleExactActionActorAndTargetPosition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E5StaleIdentityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FMoveId MoveId = MakeDefinitionId<FMoveId>(RandomTargetProbeMoveName);
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	const FActiveSlotId TargetSlot =
		MakeActiveSlotId(EBattleSide::Opponent, EBattlePosition::Right);
	enum class EStaleFact : uint8
	{
		Action,
		Actor,
		TargetPosition
	};
	auto RunStale = [&](const EStaleFact Fact) -> bool
	{
		FAtomicWildScenario Scenario = MakePreMoveScenario(MoveId);
		Scenario.Format = EBattleFormat::Double;
		TUniquePtr<FBattleEngine> Engine;
		FFaultBattleRandom* Random = nullptr;
		if (!TestTrue(TEXT("Stale target engine is created"),
				TryMakeStrictFaultEngine(
					Scenario,
					{MakeTargetExpectedDraw(1, 0)},
					EFaultRandomMode::StaleAfterDraw,
					Engine,
					Random))
			|| !TestNotNull(TEXT("Stale target RNG seam is retained"), Random)
			|| !TestTrue(TEXT("Stale target case follows committed PP"),
				TryPrepareTargetCheckpoint(*Engine, MoveId)))
		{
			return false;
		}
		const FTargetCheckpointObservation Before = ObserveTargetCheckpoint(*Engine);
		FTargetCheckpointObservation ExpectedAfter = Before;
		Random->SetAfterDraw([EnginePtr = Engine.Get(), Fact, ActorId, TargetSlot]()
		{
			FBattleEngineState& State =
				FBattleC09BWildFlowEngineFixture::GetMutableState(*EnginePtr);
			switch (Fact)
			{
			case EStaleFact::Action:
				if (State.LockedActions.IsValidIndex(State.CurrentLockedActionIndex))
				{
					++State.LockedActions[State.CurrentLockedActionIndex].QueueOrdinal;
				}
				break;
			case EStaleFact::Actor:
				if (FBattleBattlerState* Actor = State.FindMutableBattler(ActorId))
				{
					--Actor->CurrentHP;
				}
				break;
			case EStaleFact::TargetPosition:
				if (FBattleActivePositionState* Position =
					State.ActivePositions.FindByPredicate(
						[TargetSlot](const FBattleActivePositionState& Candidate)
						{
							return Candidate.ActiveSlotId == TargetSlot;
						}))
				{
					Position->bAvailable = false;
				}
				break;
			}
		});

		if (Fact == EStaleFact::Action)
		{
			++ExpectedAfter.CurrentAction.QueueOrdinal;
		}
		else if (Fact == EStaleFact::Actor)
		{
			for (FTargetCheckpointBattlerObservation& Battler : ExpectedAfter.Battlers)
			{
				if (Battler.Facts.BattlerId == ActorId)
				{
					--Battler.Facts.CurrentHP;
				}
			}
			--ExpectedAfter.Mechanics.Outgoing.CurrentHP;
		}
		else
		{
			const int32 PositionIndex = ExpectedAfter.Mechanics.ActiveSlotIds.Find(TargetSlot);
			if (ExpectedAfter.Mechanics.ActiveAvailability.IsValidIndex(PositionIndex))
			{
				ExpectedAfter.Mechanics.ActiveAvailability[PositionIndex] = 0;
			}
		}

		const FBattleResolution Rejected = Engine->ResolveCurrentMoveTargets();
		bool bCase = VerifyRejectedTargetEnvelope(
			*this,
			*Engine,
			Before,
			EBattleRejectionReason::StaleCheckpointIdentity,
			Rejected);
		bCase &= TestTrue(TEXT("Stale identity follows one staged draw without parent commit"),
			Random->GetCounters().SuccessfulDraws == 1
				&& Random->GetCounters().CommitAttempts == 0
				&& Random->GetTrace().IsEmpty());
		bCase &= TestTrue(TEXT("Only the injected concurrent stale fact survives rejection"),
			AreTargetCheckpointGameplayFactsIdentical(
				ObserveTargetCheckpoint(*Engine),
				ExpectedAfter));
		return bCase;
	};

	bool bValid = RunStale(EStaleFact::Action);
	bValid &= RunStale(EStaleFact::Actor);
	bValid &= RunStale(EStaleFact::TargetPosition);
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023E5RejectionPreservationTest,
	"PokemonSolarus.Battle.ADR0002.3E5.Targets.Failure.RejectionExactOncePreservesStateAndReplayFacts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleADR00023E5RejectionPreservationTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	const FMoveId MoveId = MakeDefinitionId<FMoveId>(TargetProbeMoveName);
	const FBattlerId ActorId = MakeNumericId<FBattlerId>(PlayerLeftValue);
	const FBattlerId TargetId = MakeNumericId<FBattlerId>(OpponentLeftValue);
	TUniquePtr<FBattleEngine> Engine;
	FStrictBattleRandom* Random = nullptr;
	if (!TestTrue(TEXT("Rich rejection-preservation engine is created"),
			TryMakeStrictEngine(MakePreMoveScenario(), {}, Engine, Random))
		|| !TestTrue(TEXT("Rich rejection follows committed PP"),
			TryPrepareTargetCheckpoint(*Engine, MoveId))
		|| !TestTrue(TEXT("Rich rejection seeds Charging"),
			TrySeedActionStartVolatile(
				*Engine,
				ActorId,
				FBattleVolatileRules::GetChargingId(),
				MoveId.GetDefinitionId()))
		|| !TestTrue(TEXT("Rich rejection seeds semi-invulnerability"),
			TrySeedActionStartVolatile(
				*Engine,
				ActorId,
				FBattleVolatileRules::GetFlySemiInvulnerableId()))
		|| !TestTrue(TEXT("Rich rejection seeds target status"),
			TrySeedPreMoveMajorStatus(
				*Engine,
				TargetId,
				FBattleMajorStatusRules::GetParalysisId())))
	{
		return false;
	}
	FBattleEngineState& MutableState =
		FBattleC09BWildFlowEngineFixture::GetMutableState(*Engine);
	FBattleBattlerState* Target = MutableState.FindMutableBattler(TargetId);
	if (!TestNotNull(TEXT("Rich rejection target exists"), Target)
		|| !MutableState.LockedActions.IsValidIndex(MutableState.CurrentLockedActionIndex))
	{
		return false;
	}
	Target->CurrentHP = 137;
	MutableState.LockedActions[MutableState.CurrentLockedActionIndex].TargetClass =
		static_cast<EBattleTargetClass>(255);
	const int32 PPBefore = GetPreMovePP(*Engine, ActorId, MoveId);
	const FTargetCheckpointObservation Before = ObserveTargetCheckpoint(*Engine);
	const FBattleResolution Rejected = Engine->ResolveCurrentMoveTargets();
	bool bValid = VerifyRejectedTargetCheckpoint(
		*this,
		*Engine,
		Before,
		EBattleRejectionReason::CheckpointPreparationFailed,
		Rejected);
	bValid &= TestEqual(TEXT("Rich rejection retains PP already committed by 3E4"),
		GetPreMovePP(*Engine, ActorId, MoveId), PPBefore);
	bValid &= TestTrue(TEXT("Rich rejection retains charged-release state"),
		FindPreMoveVolatile(
			*Engine,
			ActorId,
			FBattleVolatileRules::GetChargingId()) != nullptr
			&& FindPreMoveVolatile(
				*Engine,
				ActorId,
				FBattleVolatileRules::GetFlySemiInvulnerableId()) != nullptr);
	bValid &= TestTrue(TEXT("Rich rejection consumes no targeting RNG"),
		Random != nullptr && Random->IsExact() && Random->GetTrace().IsEmpty());
	return bValid;
}

#endif
