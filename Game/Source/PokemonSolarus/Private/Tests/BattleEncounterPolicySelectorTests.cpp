#if WITH_DEV_AUTOMATION_TESTS

#include "Battle/BattleActionSelector.h"
#include "Battle/BattleBagItem.h"
#include "Battle/BattleEncounterPolicy.h"
#include "Battle/BattleEngine.h"
#include "BattleScriptedActionSelector.h"
#include "BattleTestFactories.h"
#include "Misc/AutomationTest.h"

namespace BattleEncounterPolicySelectorTests
{
	using BattleTest::MakeActiveSlotId;
	using BattleTest::MakeDefinitionId;
	using BattleTest::MakeNumericId;
	using BattleTest::MakePartySlotId;

	constexpr uint64 PlayerTrainerValue = 1;
	constexpr uint64 OpponentTrainerValue = 2;
	constexpr uint64 PartnerTrainerValue = 3;
	constexpr uint64 PlayerLeftBattlerValue = 11;
	constexpr uint64 PlayerRightBattlerValue = 12;
	constexpr uint64 OpponentLeftBattlerValue = 21;
	constexpr uint64 OpponentRightBattlerValue = 22;
	constexpr uint64 OpponentReserveBattlerValue = 23;
	constexpr uint64 PartnerBattlerValue = 31;

	const TCHAR* SpeciesName = TEXT("Species.C09A.Proof");
	const TCHAR* AbilityName = TEXT("Ability.C09A.Proof");
	const TCHAR* MoveName = TEXT("Move.C09A.Proof");

	FBattleSnapshotReference MakeReference(const TCHAR* Name)
	{
		return {MakeDefinitionId<FDefinitionId>(Name), 1};
	}

	FBattleTrainerSetup MakeTrainer(
		const uint64 TrainerValue,
		const EBattleSide Side,
		const EBattleTrainerRole Role,
		const EBattleDecisionController Controller,
		const bool bAddRevive = false)
	{
		FBattleTrainerSetup Trainer;
		Trainer.TrainerId = MakeNumericId<FTrainerId>(TrainerValue);
		Trainer.Side = Side;
		Trainer.Role = Role;
		Trainer.Controller = Controller;
		Trainer.SelectorProfileId = MakeDefinitionId<FDefinitionId>(
			Role == EBattleTrainerRole::Player ? TEXT("Selector.C09A.Player")
			: (Role == EBattleTrainerRole::Partner
				? TEXT("Selector.C09A.Partner")
				: TEXT("Selector.C09A.Opponent")));
		if (bAddRevive)
		{
			Trainer.Bag.Add({FBattleBagItemRules::GetReviveId(), 1});
		}
		return Trainer;
	}

	FBattlePartyEntrySetup MakePartyEntry(
		const uint64 TrainerValue,
		const uint64 BattlerValue,
		const int32 PartyIndex,
		const bool bFainted = false)
	{
		FBattlePartyEntrySetup Entry;
		Entry.TrainerId = MakeNumericId<FTrainerId>(TrainerValue);
		Entry.BattlerId = MakeNumericId<FBattlerId>(BattlerValue);
		Entry.SourcePokemonId = MakeNumericId<FSourcePokemonId>(1000 + BattlerValue);
		Entry.PartySlotId = MakePartySlotId(PartyIndex);
		Entry.SpeciesFormId = MakeDefinitionId<FSpeciesFormId>(SpeciesName);
		Entry.Level = 50;
		Entry.Stats = {200, 100, 100, 100, 100, 100};
		Entry.CurrentHP = bFainted ? 0 : Entry.Stats.MaxHP;
		Entry.AbilityId = MakeDefinitionId<FAbilityId>(AbilityName);
		Entry.Moves.Add({0, MakeDefinitionId<FMoveId>(MoveName), 20, 20});
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
			MakeNumericId<FBattlerId>(BattlerValue)
		};
	}

	FBattleSetupInput MakeSetupInput(
		const EBattleEncounterKind EncounterKind,
		const EBattleFormat Format,
		const bool bOpponentHasRevive = false,
		const bool bAddFaintedOpponentReserve = false)
	{
		FBattleSetupInput Input;
		Input.BattleId = MakeNumericId<FBattleId>(909);
		Input.SettingsReference = MakeReference(TEXT("Settings.C09A"));
		Input.CatalogReference = MakeReference(TEXT("Catalog.C09A"));
		Input.EncounterKind = EncounterKind;
		Input.Format = Format;
		Input.CaptureCapacity = {4, 100};
		Input.Policies.bRunAllowed = EncounterKind == EBattleEncounterKind::Wild;
		Input.Policies.bCaptureAllowed = EncounterKind == EBattleEncounterKind::Wild;
		Input.CaptureProgression.bHasSnapshot = Input.Policies.bCaptureAllowed;
		Input.Policies.bBagAllowed = true;
		Input.Policies.bShiftPromptEligible = true;
		Input.Policies.WildFleeMode = EBattleWildFleeMode::Disabled;

		Input.Trainers.Add(MakeTrainer(
			PlayerTrainerValue,
			EBattleSide::Player,
			EBattleTrainerRole::Player,
			EBattleDecisionController::Human));
		Input.Trainers.Add(MakeTrainer(
			OpponentTrainerValue,
			EBattleSide::Opponent,
			EBattleTrainerRole::Opponent,
			EBattleDecisionController::EnemyAI,
			bOpponentHasRevive));

		Input.PartyEntries.Add(MakePartyEntry(PlayerTrainerValue, PlayerLeftBattlerValue, 0));
		Input.PartyEntries.Add(MakePartyEntry(OpponentTrainerValue, OpponentLeftBattlerValue, 0));
		Input.StartingActive.Add(MakeActive(
			EBattleSide::Player,
			EBattlePosition::Left,
			PlayerTrainerValue,
			PlayerLeftBattlerValue));
		Input.StartingActive.Add(MakeActive(
			EBattleSide::Opponent,
			EBattlePosition::Left,
			OpponentTrainerValue,
			OpponentLeftBattlerValue));

		if (bAddFaintedOpponentReserve)
		{
			Input.PartyEntries.Add(MakePartyEntry(
				OpponentTrainerValue,
				OpponentReserveBattlerValue,
				1,
				true));
		}

		if (Format == EBattleFormat::Double)
		{
			Input.PartyEntries.Add(MakePartyEntry(PlayerTrainerValue, PlayerRightBattlerValue, 1));
			Input.PartyEntries.Add(MakePartyEntry(OpponentTrainerValue, OpponentRightBattlerValue, 1));
			Input.StartingActive.Add(MakeActive(
				EBattleSide::Player,
				EBattlePosition::Right,
				PlayerTrainerValue,
				PlayerRightBattlerValue));
			Input.StartingActive.Add(MakeActive(
				EBattleSide::Opponent,
				EBattlePosition::Right,
				OpponentTrainerValue,
				OpponentRightBattlerValue));
		}
		else if (Format == EBattleFormat::PartnerDouble)
		{
			Input.Trainers.Add(MakeTrainer(
				PartnerTrainerValue,
				EBattleSide::Player,
				EBattleTrainerRole::Partner,
				EBattleDecisionController::PartnerAI));
			Input.PartyEntries.Add(MakePartyEntry(PartnerTrainerValue, PartnerBattlerValue, 0));
			Input.PartyEntries.Add(MakePartyEntry(OpponentTrainerValue, OpponentRightBattlerValue, 1));
			Input.StartingActive.Add(MakeActive(
				EBattleSide::Player,
				EBattlePosition::Right,
				PartnerTrainerValue,
				PartnerBattlerValue));
			Input.StartingActive.Add(MakeActive(
				EBattleSide::Opponent,
				EBattlePosition::Right,
				OpponentTrainerValue,
				OpponentRightBattlerValue));
		}

		return Input;
	}

	bool TryMakeSetup(
		const EBattleEncounterKind EncounterKind,
		const EBattleFormat Format,
		FBattleSetup& OutSetup,
		const bool bOpponentHasRevive = false,
		const bool bAddFaintedOpponentReserve = false)
	{
		EBattleSetupValidationError Error = EBattleSetupValidationError::None;
		return FBattleSetup::TryCreate(
			MakeSetupInput(
				EncounterKind,
				Format,
				bOpponentHasRevive,
				bAddFaintedOpponentReserve),
			OutSetup,
			Error);
	}

	TArray<FBattleTypeChartEntry> MakeTypeChart()
	{
		TArray<FBattleTypeChartEntry> Entries;
		Entries.Reserve(FBattleTypeChart::EntryCount);
		for (int32 Attacking = 0; Attacking < FBattleTypeChart::TypeCount; ++Attacking)
		{
			for (int32 Defending = 0; Defending < FBattleTypeChart::TypeCount; ++Defending)
			{
				Entries.Add(
					{
						static_cast<EPokemonType>(Attacking),
						static_cast<EPokemonType>(Defending),
						1,
						1
					});
			}
		}
		return Entries;
	}

	FBattleDefinitionCatalog MakeCatalog(const bool bIncludeRevive)
	{
		FBattleMoveDefinition Move;
		Move.Id = MakeDefinitionId<FMoveId>(MoveName);
		Move.Type = EPokemonType::Normal;
		Move.Category = EBattleMoveCategory::Physical;
		Move.Power = 40;
		Move.Accuracy = 100;
		Move.BasePP = 20;
		Move.TargetClass = EBattleTargetClass::SelectedOpponent;
		FBattleMoveEffectDescriptor Damage;
		Damage.Kind = EBattleMoveEffectKind::Damage;
		Damage.Target = EBattleEffectTarget::ResolvedTarget;
		Move.Effects.Add(Damage);

		FBattleSpeciesFormDefinition Species;
		Species.Id = MakeDefinitionId<FSpeciesFormId>(SpeciesName);
		Species.PrimaryType = EPokemonType::Normal;
		Species.BaseStats = {80, 80, 80, 80, 80, 80};
		Species.CatchRate = 45;
		Species.AbilityChoices.Add(MakeDefinitionId<FAbilityId>(AbilityName));

		FBattleDefinitionCatalogInput Input;
		Input.TypeChartEntries = MakeTypeChart();
		Input.Moves.Add(Move);
		Input.Abilities.Add({MakeDefinitionId<FAbilityId>(AbilityName)});
		Input.SpeciesForms.Add(Species);
		if (bIncludeRevive)
		{
			Input.Items.Add({FBattleBagItemRules::GetReviveId(), EBattleItemKind::Battle});
		}

		FBattleDefinitionCatalog Catalog;
		TArray<FBattleCatalogDiagnostic> Diagnostics;
		const bool bCreated = FBattleDefinitionCatalog::TryCreate(Input, Catalog, Diagnostics);
		check(bCreated);
		return Catalog;
	}

	TUniquePtr<FBattleEngine> MakeEngine(
		const EBattleEncounterKind EncounterKind,
		const EBattleFormat Format,
		const bool bOpponentHasRevive = false,
		const bool bAddFaintedOpponentReserve = false)
	{
		FBattleSetup Setup;
		const bool bSetupCreated = TryMakeSetup(
			EncounterKind,
			Format,
			Setup,
			bOpponentHasRevive,
			bAddFaintedOpponentReserve);
		check(bSetupCreated);

		TUniquePtr<FBattleEngine> Engine;
		FBattleRejection Rejection;
		const bool bEngineCreated = FBattleEngine::TryCreate(
			Setup,
			MakeCatalog(bOpponentHasRevive),
			MakeUnique<FSeededBattleRandom>(909),
			Engine,
			Rejection);
		check(bEngineCreated);
		return Engine;
	}

	FBattleDecision MakeFightDecision(const FBattleDecisionRequest& Request)
	{
		check(!Request.GetLegalMoveIds().IsEmpty());
		const FMoveId MoveId = Request.GetLegalMoveIds()[0];
		const FBattleMoveTargetOption* Pair = Request.GetLegalMoveTargets().FindByPredicate(
			[MoveId](const FBattleMoveTargetOption& Candidate)
			{
				return Candidate.MoveId == MoveId;
			});
		check(Pair != nullptr);

		FBattleDecision Decision;
		const bool bCreated = FBattleDecision::TryCreateFight(
			Request.GetStateVersion(),
			Request.GetDecisionOwnerTrainerId(),
			Request.GetActingBattlerId(),
			MoveId,
			Pair->ActiveSlotId,
			Decision);
		check(bCreated);
		return Decision;
	}

	EBattleSelectorProfileTag GetExpectedOpponentProfile(const EBattleEncounterKind Kind)
	{
		switch (Kind)
		{
		case EBattleEncounterKind::Wild:
			return EBattleSelectorProfileTag::Wild;
		case EBattleEncounterKind::Trainer:
			return EBattleSelectorProfileTag::Basic;
		case EBattleEncounterKind::Rival:
			return EBattleSelectorProfileTag::Skilled;
		case EBattleEncounterKind::BossGym:
			return EBattleSelectorProfileTag::Boss;
		case EBattleEncounterKind::TutorialScripted:
			return EBattleSelectorProfileTag::Tutorial;
		default:
			return EBattleSelectorProfileTag::None;
		}
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC09APolicyMatrixTest,
		"PokemonSolarus.Battle.C09A.Policy.Matrix",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

	bool FBattleC09APolicyMatrixTest::RunTest(const FString& Parameters)
	{
		const TArray<EBattleEncounterKind> EncounterKinds = {
			EBattleEncounterKind::Wild,
			EBattleEncounterKind::Trainer,
			EBattleEncounterKind::Rival,
			EBattleEncounterKind::BossGym,
			EBattleEncounterKind::TutorialScripted
		};
		const TArray<EBattleFormat> Formats = {
			EBattleFormat::Single,
			EBattleFormat::Double,
			EBattleFormat::PartnerDouble
		};

		for (const EBattleEncounterKind Kind : EncounterKinds)
		{
			for (const EBattleFormat Format : Formats)
			{
				const FString Label = FString::Printf(
					TEXT("kind %d format %d"),
					static_cast<int32>(Kind),
					static_cast<int32>(Format));
				FBattleSetup Setup;
				TestTrue(*FString::Printf(TEXT("%s setup is supported"), *Label), TryMakeSetup(Kind, Format, Setup));

				FBattleCompiledEncounterPolicies Policies;
				EBattleEncounterPolicyError Error = EBattleEncounterPolicyError::None;
				TestTrue(
					*FString::Printf(TEXT("%s policies compile"), *Label),
					FBattleEncounterPolicyCompiler::TryCompile(Setup, Policies, Error));
				TestTrue(*FString::Printf(TEXT("%s result is valid"), *Label), Policies.IsValid());
				TestEqual(
					*FString::Printf(TEXT("%s active limit"), *Label),
					Policies.GetMaximumActiveBattlersPerSide(),
					Format == EBattleFormat::Single ? 1 : 2);
				TestEqual(*FString::Printf(TEXT("%s party limit"), *Label), Policies.GetMaximumPartySize(), 6);
				TestEqual(
					*FString::Printf(TEXT("%s Run policy"), *Label),
					Policies.IsRunAllowed(),
					Kind == EBattleEncounterKind::Wild);
				TestEqual(
					*FString::Printf(TEXT("%s capture policy"), *Label),
					Policies.IsCaptureAllowed(),
					Kind == EBattleEncounterKind::Wild);
				TestTrue(*FString::Printf(TEXT("%s player-side Bag policy"), *Label), Policies.IsBagAllowed());
				TestEqual(
					*FString::Printf(TEXT("%s style policy"), *Label),
					Policies.GetBattleStyle(),
					Kind != EBattleEncounterKind::Wild && Format == EBattleFormat::Single
						? EBattleStylePolicy::Shift
						: EBattleStylePolicy::Set);
				TestEqual(
					*FString::Printf(TEXT("%s reinforcement policy"), *Label),
					Policies.GetReinforcementPolicy(),
					Kind == EBattleEncounterKind::Wild && Format != EBattleFormat::Single
						? EBattleReinforcementPolicy::OneWildRightSlot
						: EBattleReinforcementPolicy::Disabled);
				TestEqual(
					*FString::Printf(TEXT("%s scripted ending policy"), *Label),
					Policies.IsScriptedEndingAllowed(),
					Kind == EBattleEncounterKind::TutorialScripted);
				TestEqual(
					*FString::Printf(TEXT("%s partner ownership"), *Label),
					Policies.HasSeparatePartnerOwnership(),
					Format == EBattleFormat::PartnerDouble);

				const FBattleTrainerEncounterPolicy* Player = Policies.FindTrainerPolicy(
					MakeNumericId<FTrainerId>(PlayerTrainerValue));
				TestNotNull(*FString::Printf(TEXT("%s player policy exists"), *Label), Player);
				if (Player != nullptr)
				{
					TestEqual(*FString::Printf(TEXT("%s player controller"), *Label), Player->Controller, EBattleDecisionController::Human);
					TestTrue(
						*FString::Printf(TEXT("%s player profile ID"), *Label),
						Player->SelectorProfileId == MakeDefinitionId<FDefinitionId>(TEXT("Selector.C09A.Player")));
					TestEqual(*FString::Printf(TEXT("%s player profile tag"), *Label), Player->SelectorProfileTag, EBattleSelectorProfileTag::None);
					TestTrue(*FString::Printf(TEXT("%s player Bag permission"), *Label), Player->bMayUseBag);
					TestTrue(*FString::Printf(TEXT("%s player Revive permission"), *Label), Player->bMayUseRevive);
					TestEqual(*FString::Printf(TEXT("%s player Run permission"), *Label), Player->bMayRun, Kind == EBattleEncounterKind::Wild);
					TestEqual(*FString::Printf(TEXT("%s player capture permission"), *Label), Player->bMayCapture, Kind == EBattleEncounterKind::Wild);
					TestFalse(*FString::Printf(TEXT("%s player is not partner ownership"), *Label), Player->bPartnerOwnsSeparatePartyAndBag);
				}

				const FBattleTrainerEncounterPolicy* Opponent = Policies.FindTrainerPolicy(
					MakeNumericId<FTrainerId>(OpponentTrainerValue));
				TestNotNull(*FString::Printf(TEXT("%s opponent policy exists"), *Label), Opponent);
				if (Opponent != nullptr)
				{
					TestEqual(*FString::Printf(TEXT("%s opponent controller"), *Label), Opponent->Controller, EBattleDecisionController::EnemyAI);
					TestTrue(
						*FString::Printf(TEXT("%s opponent profile ID"), *Label),
						Opponent->SelectorProfileId == MakeDefinitionId<FDefinitionId>(TEXT("Selector.C09A.Opponent")));
					TestEqual(
						*FString::Printf(TEXT("%s opponent profile"), *Label),
						Opponent->SelectorProfileTag,
						GetExpectedOpponentProfile(Kind));
					TestEqual(
						*FString::Printf(TEXT("%s opponent Bag permission"), *Label),
						Opponent->bMayUseBag,
						Kind != EBattleEncounterKind::Wild);
					TestFalse(*FString::Printf(TEXT("%s opponent has no default Revive permission"), *Label), Opponent->bMayUseRevive);
					TestFalse(*FString::Printf(TEXT("%s opponent cannot Run"), *Label), Opponent->bMayRun);
					TestFalse(*FString::Printf(TEXT("%s opponent cannot capture"), *Label), Opponent->bMayCapture);
					TestFalse(*FString::Printf(TEXT("%s opponent is not partner ownership"), *Label), Opponent->bPartnerOwnsSeparatePartyAndBag);
				}

				if (Format == EBattleFormat::PartnerDouble)
				{
					const FBattleTrainerEncounterPolicy* Partner = Policies.FindTrainerPolicy(
						MakeNumericId<FTrainerId>(PartnerTrainerValue));
					TestNotNull(*FString::Printf(TEXT("%s partner policy exists"), *Label), Partner);
					if (Partner != nullptr)
					{
						TestEqual(*FString::Printf(TEXT("%s partner controller"), *Label), Partner->Controller, EBattleDecisionController::PartnerAI);
						TestTrue(
							*FString::Printf(TEXT("%s partner profile ID"), *Label),
							Partner->SelectorProfileId == MakeDefinitionId<FDefinitionId>(TEXT("Selector.C09A.Partner")));
						TestEqual(
							*FString::Printf(TEXT("%s partner profile"), *Label),
							Partner->SelectorProfileTag,
							EBattleSelectorProfileTag::Partner);
						TestTrue(*FString::Printf(TEXT("%s partner Bag permission"), *Label), Partner->bMayUseBag);
						TestTrue(*FString::Printf(TEXT("%s partner Revive permission"), *Label), Partner->bMayUseRevive);
						TestFalse(*FString::Printf(TEXT("%s partner cannot Run"), *Label), Partner->bMayRun);
						TestFalse(*FString::Printf(TEXT("%s partner cannot capture"), *Label), Partner->bMayCapture);
						TestTrue(
							*FString::Printf(TEXT("%s partner keeps separate ownership"), *Label),
							Partner->bPartnerOwnsSeparatePartyAndBag);
					}
				}
				else
				{
					TestNull(
						*FString::Printf(TEXT("%s has no partner policy"), *Label),
						Policies.FindTrainerPolicy(MakeNumericId<FTrainerId>(PartnerTrainerValue)));
				}
			}
		}
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC09AInvalidPolicyTest,
		"PokemonSolarus.Battle.C09A.Policy.InvalidAndUnsupported",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

	bool FBattleC09AInvalidPolicyTest::RunTest(const FString& Parameters)
	{
		FBattleSetup Setup;
		EBattleSetupValidationError Error = EBattleSetupValidationError::None;

		FBattleSetupInput Invalid = MakeSetupInput(EBattleEncounterKind::Trainer, EBattleFormat::Single);
		Invalid.EncounterKind = static_cast<EBattleEncounterKind>(255);
		TestFalse(TEXT("An unsupported encounter kind is rejected"), FBattleSetup::TryCreate(Invalid, Setup, Error));
		TestEqual(TEXT("Unsupported encounter kind is typed"), Error, EBattleSetupValidationError::InvalidEnum);

		Invalid = MakeSetupInput(EBattleEncounterKind::Trainer, EBattleFormat::Single);
		Invalid.Format = static_cast<EBattleFormat>(255);
		TestFalse(TEXT("An unsupported format is rejected"), FBattleSetup::TryCreate(Invalid, Setup, Error));
		TestEqual(TEXT("Unsupported format is typed"), Error, EBattleSetupValidationError::InvalidEnum);

		Invalid = MakeSetupInput(EBattleEncounterKind::Trainer, EBattleFormat::Single);
		Invalid.Policies.bRunAllowed = true;
		TestFalse(TEXT("Trainer Run permission is rejected"), FBattleSetup::TryCreate(Invalid, Setup, Error));
		TestEqual(TEXT("Trainer Run reports encounter policy"), Error, EBattleSetupValidationError::InvalidEncounterPolicy);

		Invalid = MakeSetupInput(EBattleEncounterKind::Rival, EBattleFormat::Single);
		Invalid.Policies.bCaptureAllowed = true;
		Invalid.CaptureProgression.bHasSnapshot = true;
		TestFalse(TEXT("Rival capture permission is rejected"), FBattleSetup::TryCreate(Invalid, Setup, Error));
		TestEqual(TEXT("Rival capture reports encounter policy"), Error, EBattleSetupValidationError::InvalidEncounterPolicy);

		Invalid = MakeSetupInput(EBattleEncounterKind::BossGym, EBattleFormat::Single);
		Invalid.Policies.WildFleeMode = EBattleWildFleeMode::Always;
		TestFalse(TEXT("Trainer-family wild fleeing is rejected"), FBattleSetup::TryCreate(Invalid, Setup, Error));
		TestEqual(TEXT("Wild fleeing reports encounter policy"), Error, EBattleSetupValidationError::InvalidEncounterPolicy);

		Invalid = MakeSetupInput(EBattleEncounterKind::Trainer, EBattleFormat::PartnerDouble);
		Invalid.Trainers.Pop();
		TestFalse(TEXT("Partner Double without a partner Trainer is rejected"), FBattleSetup::TryCreate(Invalid, Setup, Error));
		TestEqual(TEXT("Missing partner reports Trainer shape"), Error, EBattleSetupValidationError::TrainerShape);

		Invalid = MakeSetupInput(EBattleEncounterKind::Wild, EBattleFormat::Single);
		Invalid.Trainers[1].Bag.Add({FBattleBagItemRules::GetHyperPotionId(), 1});
		TestFalse(TEXT("A wild opponent cannot own a Trainer Bag"), FBattleSetup::TryCreate(Invalid, Setup, Error));
		TestEqual(TEXT("Wild opponent Bag reports encounter policy"), Error, EBattleSetupValidationError::InvalidEncounterPolicy);

		Invalid = MakeSetupInput(EBattleEncounterKind::Trainer, EBattleFormat::Single);
		Invalid.Trainers[1].Side = EBattleSide::Player;
		TestFalse(TEXT("A Trainer on the wrong side is rejected"), FBattleSetup::TryCreate(Invalid, Setup, Error));
		TestEqual(TEXT("Wrong side reports Trainer ownership"), Error, EBattleSetupValidationError::TrainerOwnership);

		Invalid = MakeSetupInput(EBattleEncounterKind::Trainer, EBattleFormat::Single);
		Invalid.Trainers[1].Role = EBattleTrainerRole::Partner;
		TestFalse(TEXT("A Single battle cannot replace its opponent with a partner role"), FBattleSetup::TryCreate(Invalid, Setup, Error));
		TestEqual(TEXT("Wrong role reports Trainer ownership"), Error, EBattleSetupValidationError::TrainerOwnership);

		Invalid = MakeSetupInput(EBattleEncounterKind::Trainer, EBattleFormat::Single);
		Invalid.PartyEntries.Add(MakePartyEntry(PlayerTrainerValue, 13, 1));
		Invalid.StartingActive.Add(MakeActive(EBattleSide::Player, EBattlePosition::Right, PlayerTrainerValue, 13));
		TestFalse(TEXT("A Single battle cannot add a third active assignment"), FBattleSetup::TryCreate(Invalid, Setup, Error));
		TestEqual(TEXT("Overfilled active shape is typed"), Error, EBattleSetupValidationError::ActiveSlotShape);

		Invalid = MakeSetupInput(EBattleEncounterKind::Trainer, EBattleFormat::Single);
		for (int32 PartyIndex = 1; PartyIndex <= 5; ++PartyIndex)
		{
			Invalid.PartyEntries.Add(MakePartyEntry(
				PlayerTrainerValue,
				100 + static_cast<uint64>(PartyIndex),
				PartyIndex));
		}
		FBattlePartyEntrySetup SeventhPartyEntry = MakePartyEntry(PlayerTrainerValue, 106, 5);
		SeventhPartyEntry.PartySlotId = FPartySlotId();
		Invalid.PartyEntries.Add(SeventhPartyEntry);
		TestFalse(TEXT("A Trainer party cannot exceed six"), FBattleSetup::TryCreate(Invalid, Setup, Error));
		TestEqual(TEXT("Overfilled party shape is typed"), Error, EBattleSetupValidationError::PartyShape);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC09AWildFleePolicyTest,
		"PokemonSolarus.Battle.C09A.Policy.WildFlee",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

	bool FBattleC09AWildFleePolicyTest::RunTest(const FString& Parameters)
	{
		FBattleSetupInput Input = MakeSetupInput(EBattleEncounterKind::Wild, EBattleFormat::Single);
		FBattleSetup Setup;
		EBattleSetupValidationError SetupError = EBattleSetupValidationError::None;
		TestTrue(TEXT("Default-disabled wild flee setup is valid"), FBattleSetup::TryCreate(Input, Setup, SetupError));

		FBattleCompiledEncounterPolicies Policies;
		EBattleEncounterPolicyError PolicyError = EBattleEncounterPolicyError::None;
		TestTrue(TEXT("Default-disabled policy compiles"), FBattleEncounterPolicyCompiler::TryCompile(Setup, Policies, PolicyError));
		TestFalse(TEXT("Default-disabled is not authored flee configuration"), Policies.IsWildFleeConfigured());

		Input.Policies.WildFleeMode = EBattleWildFleeMode::Never;
		TestTrue(TEXT("Explicit Never wild flee is valid"), FBattleSetup::TryCreate(Input, Setup, SetupError));
		TestTrue(TEXT("Explicit Never compiles"), FBattleEncounterPolicyCompiler::TryCompile(Setup, Policies, PolicyError));
		TestTrue(TEXT("Never remains explicit configuration"), Policies.IsWildFleeConfigured());
		TestEqual(TEXT("Never mode is frozen"), Policies.GetWildFleeMode(), EBattleWildFleeMode::Never);

		Input.Policies.WildFleeMode = EBattleWildFleeMode::Always;
		TestTrue(TEXT("Explicit Always wild flee is valid"), FBattleSetup::TryCreate(Input, Setup, SetupError));
		TestTrue(TEXT("Explicit Always compiles"), FBattleEncounterPolicyCompiler::TryCompile(Setup, Policies, PolicyError));
		TestTrue(TEXT("Always remains explicit configuration"), Policies.IsWildFleeConfigured());
		TestEqual(TEXT("Always mode is frozen"), Policies.GetWildFleeMode(), EBattleWildFleeMode::Always);

		Input.Policies.WildFleeMode = EBattleWildFleeMode::Chance;
		Input.Policies.WildFleeNumerator = 1;
		Input.Policies.WildFleeDenominator = 4;
		TestTrue(TEXT("Valid authored Chance is accepted"), FBattleSetup::TryCreate(Input, Setup, SetupError));
		TestTrue(TEXT("Chance compiles"), FBattleEncounterPolicyCompiler::TryCompile(Setup, Policies, PolicyError));
		TestEqual(TEXT("Chance numerator is frozen"), Policies.GetWildFleeNumerator(), 1U);
		TestEqual(TEXT("Chance denominator is frozen"), Policies.GetWildFleeDenominator(), 4U);

		Input.Policies.WildFleeNumerator = 4;
		TestFalse(TEXT("Chance numerator equal to denominator is rejected"), FBattleSetup::TryCreate(Input, Setup, SetupError));
		TestEqual(TEXT("Invalid Chance reports encounter policy"), SetupError, EBattleSetupValidationError::InvalidEncounterPolicy);

		Input.Policies.WildFleeNumerator = 0;
		Input.Policies.WildFleeDenominator = 0;
		TestFalse(TEXT("Chance requires a non-zero proper fraction"), FBattleSetup::TryCreate(Input, Setup, SetupError));
		TestEqual(TEXT("Zero Chance fraction reports encounter policy"), SetupError, EBattleSetupValidationError::InvalidEncounterPolicy);

		Input.Policies.WildFleeMode = EBattleWildFleeMode::Always;
		Input.Policies.WildFleeNumerator = 1;
		Input.Policies.WildFleeDenominator = 4;
		TestFalse(TEXT("A non-Chance mode cannot carry a probability fraction"), FBattleSetup::TryCreate(Input, Setup, SetupError));
		TestEqual(TEXT("Unexpected flee fraction reports encounter policy"), SetupError, EBattleSetupValidationError::InvalidEncounterPolicy);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC09ARevivePolicyTest,
		"PokemonSolarus.Battle.C09A.Policy.ReviveAndProfiles",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

	bool FBattleC09ARevivePolicyTest::RunTest(const FString& Parameters)
	{
		const TArray<EBattleEncounterKind> Kinds = {
			EBattleEncounterKind::Trainer,
			EBattleEncounterKind::Rival,
			EBattleEncounterKind::BossGym,
			EBattleEncounterKind::TutorialScripted
		};
		for (const EBattleEncounterKind Kind : Kinds)
		{
			FBattleSetup Setup;
			TestTrue(TEXT("Explicit opponent Bag setup is accepted"), TryMakeSetup(Kind, EBattleFormat::Single, Setup, true));
			FBattleCompiledEncounterPolicies Policies;
			EBattleEncounterPolicyError Error = EBattleEncounterPolicyError::None;
			TestTrue(TEXT("Explicit opponent Bag policy compiles"), FBattleEncounterPolicyCompiler::TryCompile(Setup, Policies, Error));

			const FBattleTrainerEncounterPolicy* Opponent = Policies.FindTrainerPolicy(
				MakeNumericId<FTrainerId>(OpponentTrainerValue));
			TestNotNull(TEXT("Opponent policy exists"), Opponent);
			if (Opponent != nullptr)
			{
				TestEqual(
					TEXT("Only Boss/Gym admits an explicitly configured opponent Revive"),
					Opponent->bMayUseRevive,
					Kind == EBattleEncounterKind::BossGym);
			}

			FBattleBagItemUseFacts Facts;
			Facts.ItemId = FBattleBagItemRules::GetReviveId();
			Facts.DefinitionKind = EBattleItemKind::Battle;
			Facts.TargetKind = EBattleBagItemTargetKind::Party;
			Facts.bActingTrainerMayUseBag = Opponent != nullptr
				&& Opponent->bMayUseBag;
			Facts.bActingTrainerMayUseRevive = Opponent != nullptr
				&& Opponent->bMayUseRevive;
			Facts.bTargetOwnedByActingTrainer = true;
			Facts.bTargetFainted = true;
			Facts.CurrentHP = 0;
			Facts.MaximumHP = 200;
			FBattleBagItemUseResult Result;
			TestTrue(TEXT("Opponent Revive facts are valid"), FBattleBagItemRules::TryEvaluateUse(Facts, Result));
			TestEqual(
				TEXT("Pure item rule matches the compiled boss permission"),
				Result.bLegal,
				Kind == EBattleEncounterKind::BossGym);
		}

		FBattleSetupInput BagDisabled = MakeSetupInput(EBattleEncounterKind::BossGym, EBattleFormat::Single, true);
		BagDisabled.Policies.bBagAllowed = false;
		FBattleSetup Setup;
		EBattleSetupValidationError SetupError = EBattleSetupValidationError::None;
		TestTrue(TEXT("A globally disabled Bag setup is valid"), FBattleSetup::TryCreate(BagDisabled, Setup, SetupError));
		FBattleCompiledEncounterPolicies Policies;
		EBattleEncounterPolicyError PolicyError = EBattleEncounterPolicyError::None;
		TestTrue(TEXT("A globally disabled Bag policy compiles"), FBattleEncounterPolicyCompiler::TryCompile(Setup, Policies, PolicyError));
		TestFalse(TEXT("The compiled encounter Bag is disabled"), Policies.IsBagAllowed());
		for (const FBattleTrainerEncounterPolicy& TrainerPolicy : Policies.GetTrainerPolicies())
		{
			TestFalse(TEXT("A disabled encounter grants no Trainer Bag permission"), TrainerPolicy.bMayUseBag);
			TestFalse(TEXT("A disabled encounter grants no Revive permission"), TrainerPolicy.bMayUseRevive);
		}
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC09ASelectorBoundaryTest,
		"PokemonSolarus.Battle.C09A.Selector.VisibilityLegalityAndRevalidation",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

	bool FBattleC09ASelectorBoundaryTest::RunTest(const FString& Parameters)
	{
		TUniquePtr<FBattleEngine> Engine = MakeEngine(
			EBattleEncounterKind::Trainer,
			EBattleFormat::PartnerDouble);
		FBattleRejection Rejection;
		TestTrue(TEXT("Decision sequence starts"), Engine->TryBeginActionDecisionSequence(Rejection));

		FBattleActionSelectorInput InvalidInput;
		TestFalse(
			TEXT("A core-authority snapshot cannot cross the selector boundary"),
			FBattleActionSelectorInput::TryCreate(Engine->GetSnapshot(), 0, InvalidInput, Rejection));
		TestEqual(TEXT("Unfiltered input reports invalid setup"), Rejection.Reason, EBattleRejectionReason::InvalidSetup);

		const FTrainerId PlayerTrainerId = MakeNumericId<FTrainerId>(PlayerTrainerValue);
		const FTrainerId PartnerTrainerId = MakeNumericId<FTrainerId>(PartnerTrainerValue);
		const FTrainerId OpponentTrainerId = MakeNumericId<FTrainerId>(OpponentTrainerValue);
		FBattleActionSelectorInput PlayerInput;
		TestTrue(
			TEXT("Player receives its filtered legal actions"),
			FBattleActionSelectorInput::TryCreate(
				Engine->GetSnapshotForObserver(PlayerTrainerId),
				0,
				PlayerInput,
				Rejection));

		const FBattleDecision PlayerDecision = MakeFightDecision(PlayerInput.GetLegalActions());
		FScriptedBattleActionSelector PlayerSelector({PlayerDecision});
		FBattleDecision SelectedPlayerDecision;
		TestTrue(
			TEXT("The legal-only boundary accepts a scripted player action"),
			FBattleActionSelectorBoundary::TrySelectLegalAction(
				PlayerSelector,
				PlayerInput,
				SelectedPlayerDecision,
				Rejection));
		TestTrue(TEXT("The engine accepts the revalidated player action"), Engine->SubmitDecision(SelectedPlayerDecision).WasAccepted());

		const FBattleSnapshot EarlyEnemyObservation = Engine->GetSnapshotForObserver(OpponentTrainerId);
		TestTrue(TEXT("Enemy observation is valid"), EarlyEnemyObservation.IsValid());
		TestEqual(TEXT("Enemy cannot see the player's unexecuted selection"), EarlyEnemyObservation.GetVisibleSelections().Num(), 0);
		TestEqual(TEXT("Enemy cannot see another owner's current request"), EarlyEnemyObservation.GetPendingDecisionRequests().Num(), 0);

		const FBattleSnapshot PartnerObservation = Engine->GetSnapshotForObserver(PartnerTrainerId);
		TestEqual(TEXT("Partner sees the player's accepted selection"), PartnerObservation.GetVisibleSelections().Num(), 1);
		TestEqual(
			TEXT("The visible selection belongs to the player"),
			PartnerObservation.GetVisibleSelections()[0].GetDecisionOwnerTrainerId(),
			PlayerTrainerId);

		FBattleActionSelectorInput PartnerInput;
		TestTrue(
			TEXT("Partner receives its filtered legal actions"),
			FBattleActionSelectorInput::TryCreate(
				PartnerObservation,
				0,
				PartnerInput,
				Rejection));
		const FBattleDecision PartnerDecision = MakeFightDecision(PartnerInput.GetLegalActions());

		FScriptedBattleActionSelector FirstSelector({PartnerDecision});
		FScriptedBattleActionSelector SecondSelector({PartnerDecision});
		FBattleDecision FirstOutput;
		FBattleDecision SecondOutput;
		TestTrue(
			TEXT("First identical script selects legally"),
			FBattleActionSelectorBoundary::TrySelectLegalAction(
				FirstSelector,
				PartnerInput,
				FirstOutput,
				Rejection));
		TestTrue(
			TEXT("Second identical script selects legally"),
			FBattleActionSelectorBoundary::TrySelectLegalAction(
				SecondSelector,
				PartnerInput,
				SecondOutput,
				Rejection));
		TestTrue(TEXT("Identical scripts choose the same move"), FirstOutput.GetMoveId() == SecondOutput.GetMoveId());
		TestTrue(TEXT("Identical scripts choose the same target"), FirstOutput.GetActiveTargetId() == SecondOutput.GetActiveTargetId());

		FBattleDecision IllegalRun;
		TestTrue(
			TEXT("A structurally valid Run payload can be built for rejection proof"),
			FBattleDecision::TryCreateSimpleAction(
				PartnerInput.GetLegalActions().GetStateVersion(),
				EBattleDecisionRequestKind::Action,
				PartnerInput.GetLegalActions().GetDecisionOwnerTrainerId(),
				PartnerInput.GetLegalActions().GetActingBattlerId(),
				EBattleActionKind::Run,
				IllegalRun));
		FScriptedBattleActionSelector IllegalSelector({IllegalRun});
		FBattleDecision RejectedOutput;
		TestFalse(
			TEXT("The boundary rejects a selector output outside generated legal actions"),
			FBattleActionSelectorBoundary::TrySelectLegalAction(
				IllegalSelector,
				PartnerInput,
				RejectedOutput,
				Rejection));
		TestEqual(TEXT("Illegal selector output reports illegal action"), Rejection.Reason, EBattleRejectionReason::IllegalAction);

		TestTrue(TEXT("The partner action is accepted once"), Engine->SubmitDecision(FirstOutput).WasAccepted());
		const FBattleResolution StaleResolution = Engine->SubmitDecision(FirstOutput);
		TestFalse(TEXT("The same selector payload is stale after state advances"), StaleResolution.WasAccepted());
		TestEqual(
			TEXT("Engine revalidation reports stale state"),
			StaleResolution.GetRejection().Reason,
			EBattleRejectionReason::StaleStateVersion);

		const FBattleSnapshot EnemyObservation = Engine->GetSnapshotForObserver(OpponentTrainerId);
		TestEqual(TEXT("Enemy still sees no allied unexecuted selections"), EnemyObservation.GetVisibleSelections().Num(), 0);
		FBattleActionSelectorInput EnemyInput;
		TestTrue(
			TEXT("Enemy receives only its own generated legal actions"),
			FBattleActionSelectorInput::TryCreate(EnemyObservation, 0, EnemyInput, Rejection));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC09ABossReviveRequestTest,
		"PokemonSolarus.Battle.C09A.Policy.BossReviveGeneratedAction",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

	bool FBattleC09ABossReviveRequestTest::RunTest(const FString& Parameters)
	{
		TUniquePtr<FBattleEngine> Engine = MakeEngine(
			EBattleEncounterKind::BossGym,
			EBattleFormat::Single,
			true,
			true);
		FBattleRejection Rejection;
		TestTrue(TEXT("Boss decision sequence starts"), Engine->TryBeginActionDecisionSequence(Rejection));
		const FBattleDecisionRequest PlayerRequest = Engine->GetPendingDecision().GetValue();
		TestTrue(TEXT("Player action is accepted before boss selection"), Engine->SubmitDecision(MakeFightDecision(PlayerRequest)).WasAccepted());

		const FBattleDecisionRequest BossRequest = Engine->GetPendingDecision().GetValue();
		const FItemId ReviveId = FBattleBagItemRules::GetReviveId();
		const FPartySlotId ReserveSlot = MakePartySlotId(1);
		TestTrue(TEXT("Explicit boss Revive is a legal item"), BossRequest.GetLegalItemIds().Contains(ReviveId));
		TestTrue(
			TEXT("Boss Revive is paired only through the owned fainted reserve"),
			BossRequest.GetLegalItemPartyTargets().ContainsByPredicate(
				[ReviveId, ReserveSlot](const FBattleItemPartyTargetOption& Option)
				{
					return Option.ItemId == ReviveId && Option.PartySlotId == ReserveSlot;
				}));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleADR00023B2SelectorOwnershipTest,
		"PokemonSolarus.Battle.ADR0002.3B2.RuntimeAuthority.Selector.FifoDeepCopyAndCrossOwnerRejection",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

	bool FBattleADR00023B2SelectorOwnershipTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		TUniquePtr<FBattleEngine> Engine = MakeEngine(
			EBattleEncounterKind::Trainer,
			EBattleFormat::Double);
		FBattleRejection Rejection;
		TestTrue(TEXT("Double decision sequence starts"),
			Engine->TryBeginActionDecisionSequence(Rejection));

		FBattleSnapshot SourceObservation = Engine->GetSnapshotForObserver(
			MakeNumericId<FTrainerId>(PlayerTrainerValue));
		FBattleActionSelectorInput Input;
		TestTrue(TEXT("The filtered selector input is created"),
			FBattleActionSelectorInput::TryCreate(
				SourceObservation,
				0,
				Input,
				Rejection));
		SourceObservation = FBattleSnapshot();
		TestTrue(TEXT("Selector input owns a deep snapshot copy"),
			Input.GetObservation().IsValid());

		const FBattleDecisionRequest& Request = Input.GetLegalActions();
		const FBattleDecision FirstDecision = MakeFightDecision(Request);
		const FBattleMoveTargetOption* SecondTarget =
			Request.GetLegalMoveTargets().FindByPredicate(
				[&FirstDecision](const FBattleMoveTargetOption& Option)
				{
					return Option.MoveId == FirstDecision.GetMoveId()
						&& Option.ActiveSlotId != FirstDecision.GetActiveTargetId();
				});
		TestNotNull(TEXT("Double targeting supplies a distinct FIFO payload"), SecondTarget);
		check(SecondTarget != nullptr);
		FBattleDecision SecondDecision;
		TestTrue(TEXT("The second FIFO payload is valid"),
			FBattleDecision::TryCreateFight(
				Request.GetStateVersion(),
				Request.GetDecisionOwnerTrainerId(),
				Request.GetActingBattlerId(),
				FirstDecision.GetMoveId(),
				SecondTarget->ActiveSlotId,
				SecondDecision));

		FScriptedBattleActionSelector Selector({FirstDecision, SecondDecision});
		FBattleDecision FirstOutput;
		FBattleDecision SecondOutput;
		TestTrue(TEXT("FIFO selector returns its first payload"),
			FBattleActionSelectorBoundary::TrySelectLegalAction(
				Selector, Input, FirstOutput, Rejection));
		TestTrue(TEXT("FIFO selector returns its second payload"),
			FBattleActionSelectorBoundary::TrySelectLegalAction(
				Selector, Input, SecondOutput, Rejection));
		TestTrue(TEXT("FIFO preserves first target order"),
			FirstOutput.GetActiveTargetId() == FirstDecision.GetActiveTargetId());
		TestTrue(TEXT("FIFO preserves second target order"),
			SecondOutput.GetActiveTargetId() == SecondDecision.GetActiveTargetId());
		TestEqual(TEXT("FIFO consumes both deep-copied payloads"),
			Selector.GetRemainingDecisionCount(), 0);

		FBattleDecision ForgedCrossOwner;
		TestTrue(TEXT("A structurally valid cross-owner payload can be forged"),
			FBattleDecision::TryCreateFight(
				Request.GetStateVersion(),
				MakeNumericId<FTrainerId>(OpponentTrainerValue),
				Request.GetActingBattlerId(),
				FirstDecision.GetMoveId(),
				FirstDecision.GetActiveTargetId(),
				ForgedCrossOwner));
		const uint64 BeforeVersion = Engine->GetSnapshot().GetStateVersion();
		const FBattleResolution ForgedResult = Engine->SubmitDecision(ForgedCrossOwner);
		TestFalse(TEXT("The engine rejects the forged cross-owner payload"),
			ForgedResult.WasAccepted());
		TestEqual(TEXT("Cross-owner rejection is typed"),
			ForgedResult.GetRejection().Reason,
			EBattleRejectionReason::WrongDecisionOwner);
		TestEqual(TEXT("Cross-owner rejection leaves state unchanged"),
			Engine->GetSnapshot().GetStateVersion(), BeforeVersion);
		TestEqual(TEXT("Cross-owner rejection consumes no RNG"),
			Engine->ExportRandomTrace().Num(), 0);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleADR00023B1CompiledTransferTest,
		"PokemonSolarus.Battle.ADR0002.3B1.SetupPolicy.CompiledValueTransfer",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

	bool FBattleADR00023B1CompiledTransferTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		FBattleSetup Setup;
		TestTrue(
			TEXT("A Trainer Single setup is created"),
			TryMakeSetup(EBattleEncounterKind::Trainer, EBattleFormat::Single, Setup));
		const FBattleCompiledEncounterPolicies& Stored =
			Setup.GetCompiledEncounterPolicies();
		TestTrue(TEXT("The stored compiled value is valid"), Stored.IsValid());
		TestEqual(TEXT("The stored encounter kind is compiled"), Stored.GetEncounterKind(), EBattleEncounterKind::Trainer);
		TestEqual(TEXT("The stored format is compiled"), Stored.GetFormat(), EBattleFormat::Single);
		TestEqual(TEXT("The stored style is compiled"), Stored.GetBattleStyle(), EBattleStylePolicy::Shift);

		FBattleCompiledEncounterPolicies Recompiled;
		EBattleEncounterPolicyError Error = EBattleEncounterPolicyError::None;
		TestTrue(
			TEXT("The immutable setup recompiles deterministically"),
			FBattleEncounterPolicyCompiler::TryCompile(Setup, Recompiled, Error));
		TestEqual(
			TEXT("Stored and recompiled Trainer policy counts match"),
			Stored.GetTrainerPolicies().Num(),
			Recompiled.GetTrainerPolicies().Num());

		const FBattleTrainerEncounterPolicy* Player = Stored.FindTrainerPolicy(
			MakeNumericId<FTrainerId>(PlayerTrainerValue));
		const FBattleTrainerEncounterPolicy* Opponent = Stored.FindTrainerPolicy(
			MakeNumericId<FTrainerId>(OpponentTrainerValue));
		TestNotNull(TEXT("The stored player policy exists"), Player);
		TestNotNull(TEXT("The stored opponent policy exists"), Opponent);
		if (Player != nullptr)
		{
			TestEqual(TEXT("The player side is compiled"), Player->Side, EBattleSide::Player);
			TestTrue(TEXT("The player may voluntarily switch"), Player->bMayVoluntarilySwitch);
		}
		if (Opponent != nullptr)
		{
			TestEqual(TEXT("The opponent side is compiled"), Opponent->Side, EBattleSide::Opponent);
			TestTrue(TEXT("A Trainer opponent may voluntarily switch"), Opponent->bMayVoluntarilySwitch);
		}

		FBattleSetup WildSetup;
		TestTrue(
			TEXT("A Wild Single setup is created"),
			TryMakeSetup(EBattleEncounterKind::Wild, EBattleFormat::Single, WildSetup));
		const FBattleTrainerEncounterPolicy* WildOpponent =
			WildSetup.GetCompiledEncounterPolicies().FindTrainerPolicy(
				MakeNumericId<FTrainerId>(OpponentTrainerValue));
		TestNotNull(TEXT("The stored Wild opponent policy exists"), WildOpponent);
		if (WildOpponent != nullptr)
		{
			TestFalse(
				TEXT("An ordinary Wild opponent may not voluntarily switch"),
				WildOpponent->bMayVoluntarilySwitch);
		}
		return true;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
