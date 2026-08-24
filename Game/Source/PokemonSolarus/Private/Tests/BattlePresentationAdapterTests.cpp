#if WITH_DEV_AUTOMATION_TESTS

#include "Battle/BattleBagItem.h"
#include "Battle/BattleDisplayNameResolver.h"
#include "Battle/BattleEngine.h"
#include "Battle/BattleRandom.h"
#include "BattleTestFactories.h"
#include "Misc/AutomationTest.h"
#include "UI/BattleCommandWidget.h"
#include "UI/BattleHUDDisplayState.h"
#include "UI/BattlePresentationAdapter.h"

namespace BattlePresentationAdapterTests
{
	using BattleTest::MakeActiveSlotId;
	using BattleTest::MakeDefinitionId;
	using BattleTest::MakeNumericId;
	using BattleTest::MakePartySlotId;

	constexpr uint64 PlayerTrainerValue = 8101;
	constexpr uint64 OpponentTrainerValue = 8102;
	constexpr uint64 PlayerBattlerValue = 8111;
	constexpr uint64 PlayerReserveBattlerValue = 8112;
	constexpr uint64 OpponentBattlerValue = 8121;
	const TCHAR* SpeciesName = TEXT("Species.UI.Presentation");
	const TCHAR* MoveName = TEXT("Move.UI.Presentation");
	const TCHAR* AbilityName = TEXT("Ability.UI.Presentation");

	struct FPresentationScenario
	{
		bool bHasReserve = true;
		bool bHasUsableItem = true;
	};

	class FPresentationDisplayNameResolver final
		: public IBattleDisplayNameResolver
	{
	public:
		explicit FPresentationDisplayNameResolver(const bool bInCanResolve = true)
			: bCanResolve(bInCanResolve)
		{
		}

		virtual bool TryResolveSpeciesName(
			const FSpeciesFormId SpeciesFormId,
			FText& OutDisplayName) const override
		{
			OutDisplayName = FText::GetEmpty();
			if (!bCanResolve
				|| SpeciesFormId != MakeDefinitionId<FSpeciesFormId>(SpeciesName))
			{
				return false;
			}
			OutDisplayName = FText::FromString(TEXT("Presentation Pokemon"));
			return true;
		}

	private:
		bool bCanResolve = true;
	};

	TArray<FBattleTypeChartEntry> MakePresentationTypeChart()
	{
		TArray<FBattleTypeChartEntry> Entries;
		Entries.Reserve(FBattleTypeChart::EntryCount);
		for (int32 AttackingIndex = 0;
			AttackingIndex < FBattleTypeChart::TypeCount;
			++AttackingIndex)
		{
			for (int32 DefendingIndex = 0;
				DefendingIndex < FBattleTypeChart::TypeCount;
				++DefendingIndex)
			{
				Entries.Add(
					{
						static_cast<EPokemonType>(AttackingIndex),
						static_cast<EPokemonType>(DefendingIndex),
						1,
						1
					});
			}
		}
		return Entries;
	}

	FBattleDefinitionCatalog MakePresentationCatalog()
	{
		FBattleDefinitionCatalogInput Input;
		Input.TypeChartEntries = MakePresentationTypeChart();

		FBattleSpeciesFormDefinition Species;
		Species.Id = MakeDefinitionId<FSpeciesFormId>(SpeciesName);
		Species.PrimaryType = EPokemonType::Normal;
		Species.BaseStats = {80, 80, 80, 80, 80, 80};
		Species.CatchRate = 45;
		Species.AbilityChoices.Add(MakeDefinitionId<FAbilityId>(AbilityName));
		Input.SpeciesForms.Add(Species);
		Input.Abilities.Add({MakeDefinitionId<FAbilityId>(AbilityName)});

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
		Input.Moves.Add(Move);

		Input.Items.Add(
			{
				FBattleBagItemRules::GetHyperPotionId(),
				EBattleItemKind::Battle
			});

		FBattleDefinitionCatalog Catalog;
		TArray<FBattleCatalogDiagnostic> Diagnostics;
		check(FBattleDefinitionCatalog::TryCreate(Input, Catalog, Diagnostics));
		return Catalog;
	}

	FBattlePartyEntrySetup MakePresentationPartyEntry(
		const uint64 TrainerValue,
		const uint64 BattlerValue,
		const int32 PartyIndex,
		const int32 CurrentHP)
	{
		FBattlePartyEntrySetup Entry;
		Entry.TrainerId = MakeNumericId<FTrainerId>(TrainerValue);
		Entry.BattlerId = MakeNumericId<FBattlerId>(BattlerValue);
		Entry.SourcePokemonId = MakeNumericId<FSourcePokemonId>(8200 + BattlerValue);
		Entry.PartySlotId = MakePartySlotId(PartyIndex);
		Entry.SpeciesFormId = MakeDefinitionId<FSpeciesFormId>(SpeciesName);
		Entry.Level = 50;
		Entry.Stats = {200, 100, 100, 100, 100, 100};
		Entry.CurrentHP = CurrentHP;
		Entry.AbilityId = MakeDefinitionId<FAbilityId>(AbilityName);
		Entry.Moves.Add(
			{
				0,
				MakeDefinitionId<FMoveId>(MoveName),
				10,
				20
			});
		return Entry;
	}

	FBattleSetup MakePresentationSetup(const FPresentationScenario& Scenario)
	{
		FBattleSetupInput Input;
		Input.BattleId = MakeNumericId<FBattleId>(8100);
		Input.SettingsReference =
			{MakeDefinitionId<FDefinitionId>(TEXT("Settings.UI.Presentation")), 1};
		Input.CatalogReference =
			{MakeDefinitionId<FDefinitionId>(TEXT("Catalog.UI.Presentation")), 1};
		Input.EncounterKind = EBattleEncounterKind::Trainer;
		Input.Format = EBattleFormat::Single;
		Input.CaptureCapacity = {3, 100};
		Input.Policies.bBagAllowed = true;
		Input.Policies.bRunAllowed = false;
		Input.Policies.bCaptureAllowed = false;
		Input.Policies.WildFleeMode = EBattleWildFleeMode::Disabled;

		FBattleTrainerSetup Player;
		Player.TrainerId = MakeNumericId<FTrainerId>(PlayerTrainerValue);
		Player.Side = EBattleSide::Player;
		Player.Role = EBattleTrainerRole::Player;
		Player.Controller = EBattleDecisionController::Human;
		Player.SelectorProfileId =
			MakeDefinitionId<FDefinitionId>(TEXT("Selector.UI.Player"));
		if (Scenario.bHasUsableItem)
		{
			Player.Bag.Add({FBattleBagItemRules::GetHyperPotionId(), 1});
		}
		Input.Trainers.Add(Player);

		FBattleTrainerSetup Opponent;
		Opponent.TrainerId = MakeNumericId<FTrainerId>(OpponentTrainerValue);
		Opponent.Side = EBattleSide::Opponent;
		Opponent.Role = EBattleTrainerRole::Opponent;
		Opponent.Controller = EBattleDecisionController::EnemyAI;
		Opponent.SelectorProfileId =
			MakeDefinitionId<FDefinitionId>(TEXT("Selector.UI.Opponent"));
		Input.Trainers.Add(Opponent);

		Input.PartyEntries.Add(MakePresentationPartyEntry(
			PlayerTrainerValue,
			PlayerBattlerValue,
			0,
			100));
		if (Scenario.bHasReserve)
		{
			Input.PartyEntries.Add(MakePresentationPartyEntry(
				PlayerTrainerValue,
				PlayerReserveBattlerValue,
				1,
				200));
		}
		Input.PartyEntries.Add(MakePresentationPartyEntry(
			OpponentTrainerValue,
			OpponentBattlerValue,
			0,
			200));

		const FActiveSlotId PlayerSlot = MakeActiveSlotId(
			EBattleSide::Player,
			EBattlePosition::Left);
		const FActiveSlotId OpponentSlot = MakeActiveSlotId(
			EBattleSide::Opponent,
			EBattlePosition::Left);
		Input.StartingActive.Add(
			{
				PlayerSlot,
				MakeNumericId<FTrainerId>(PlayerTrainerValue),
				MakeNumericId<FBattlerId>(PlayerBattlerValue)
			});
		Input.StartingActive.Add(
			{
				OpponentSlot,
				MakeNumericId<FTrainerId>(OpponentTrainerValue),
				MakeNumericId<FBattlerId>(OpponentBattlerValue)
			});
		Input.ObedienceInputs.Add(
			{
				MakeNumericId<FBattlerId>(PlayerBattlerValue),
				true,
				100,
				8
			});

		FBattleSetup Setup;
		EBattleSetupValidationError Error = EBattleSetupValidationError::None;
		check(FBattleSetup::TryCreate(Input, Setup, Error));
		return Setup;
	}

	TUniquePtr<FBattleEngine> MakePresentationEngine(
		const FPresentationScenario& Scenario)
	{
		TUniquePtr<FBattleEngine> Engine;
		FBattleRejection Rejection;
		check(FBattleEngine::TryCreate(
			MakePresentationSetup(Scenario),
			MakePresentationCatalog(),
			MakeUnique<FSeededBattleRandom>(8100),
			Engine,
			Rejection));
		return Engine;
	}

	FBattleSnapshot MakeSelectingObserverSnapshot(
		const FPresentationScenario& Scenario)
	{
		TUniquePtr<FBattleEngine> Engine = MakePresentationEngine(Scenario);
		FBattleRejection Rejection;
		check(Engine->TryBeginActionDecisionSequence(Rejection));
		return Engine->GetSnapshotForObserver(
			MakeNumericId<FTrainerId>(PlayerTrainerValue));
	}

	FActiveSlotId GetPlayerSlot()
	{
		return MakeActiveSlotId(EBattleSide::Player, EBattlePosition::Left);
	}
}

class FBattlePresentationAdapterTestFixture
{
public:
	static bool TryMapUnavailableReason(
		const EBattleActionKind ActionKind,
		const EBattleOptionUnavailableReason Reason,
		FText& OutText)
	{
		return FBattlePresentationAdapter::TryMapUnavailableReason(
			ActionKind,
			Reason,
			OutText);
	}

};

namespace BattlePresentationAdapterTests
{
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattlePresentationUnavailableReasonMatrixTest,
		"PokemonSolarus.UI.Battle.Presentation.Adapter.UnavailableReasonMatrix",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FBattlePresentationUnavailableReasonMatrixTest::RunTest(
		const FString& Parameters)
	{
		struct FSupportedMapping
		{
			EBattleActionKind ActionKind;
			EBattleOptionUnavailableReason Reason;
			const TCHAR* ExpectedText;
		};
		const FSupportedMapping SupportedMappings[] = {
			{EBattleActionKind::Fight, EBattleOptionUnavailableReason::NoPP,
				TEXT("No moves can be used.")},
			{EBattleActionKind::Fight, EBattleOptionUnavailableReason::NoLegalTarget,
				TEXT("There is no target for a move.")},
			{EBattleActionKind::Bag, EBattleOptionUnavailableReason::NoItemRemaining,
				TEXT("There are no usable items.")},
			{EBattleActionKind::Bag, EBattleOptionUnavailableReason::BagRestricted,
				TEXT("The Bag cannot be used right now.")},
			{EBattleActionKind::Bag, EBattleOptionUnavailableReason::CaptureRestricted,
				TEXT("Pok\u00e9 Balls cannot be used in this battle.")},
			{EBattleActionKind::Bag, EBattleOptionUnavailableReason::NoLegalTarget,
				TEXT("No item can be used right now.")},
			{EBattleActionKind::Switch, EBattleOptionUnavailableReason::Trapped,
				TEXT("This Pok\u00e9mon cannot switch out.")},
			{EBattleActionKind::Switch, EBattleOptionUnavailableReason::SwitchRestricted,
				TEXT("Pok\u00e9mon cannot be switched right now.")},
			{EBattleActionKind::Switch, EBattleOptionUnavailableReason::NoLegalTarget,
				TEXT("There is no Pok\u00e9mon available to switch.")},
			{EBattleActionKind::Run, EBattleOptionUnavailableReason::RunRestricted,
				TEXT("You cannot run from this battle.")},
		};

		for (const FSupportedMapping& Mapping : SupportedMappings)
		{
			FText MappedText = FText::FromString(TEXT("Sentinel"));
			const FString Context = FString::Printf(
				TEXT("Action %d reason %d"),
				static_cast<int32>(Mapping.ActionKind),
				static_cast<int32>(Mapping.Reason));
			TestTrue(
				*FString::Printf(TEXT("%s is a supported mapping"), *Context),
				FBattlePresentationAdapterTestFixture::TryMapUnavailableReason(
					Mapping.ActionKind,
					Mapping.Reason,
					MappedText));
			TestEqual(
				*FString::Printf(TEXT("%s maps to its exact localized text"), *Context),
				MappedText.ToString(),
				FString(Mapping.ExpectedText));
		}

		struct FUnsupportedMapping
		{
			EBattleActionKind ActionKind;
			EBattleOptionUnavailableReason Reason;
		};
		const FUnsupportedMapping UnsupportedMappings[] = {
			{EBattleActionKind::Fight, EBattleOptionUnavailableReason::BagRestricted},
			{EBattleActionKind::Bag, EBattleOptionUnavailableReason::Trapped},
			{EBattleActionKind::Switch, EBattleOptionUnavailableReason::NoPP},
			{EBattleActionKind::Run, EBattleOptionUnavailableReason::NoLegalTarget},
			{EBattleActionKind::WildFlee, EBattleOptionUnavailableReason::RunRestricted},
		};

		for (const FUnsupportedMapping& Mapping : UnsupportedMappings)
		{
			FText MappedText = FText::FromString(TEXT("Sentinel"));
			const FString Context = FString::Printf(
				TEXT("Action %d reason %d"),
				static_cast<int32>(Mapping.ActionKind),
				static_cast<int32>(Mapping.Reason));
			TestFalse(
				*FString::Printf(TEXT("%s is rejected without a generic fallback"), *Context),
				FBattlePresentationAdapterTestFixture::TryMapUnavailableReason(
					Mapping.ActionKind,
					Mapping.Reason,
					MappedText));
			TestTrue(
				*FString::Printf(TEXT("%s clears the output text"), *Context),
				MappedText.IsEmpty());
		}
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattlePresentationObserverMappingTest,
		"PokemonSolarus.UI.Battle.Presentation.Adapter.ObserverSafeMapping",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FBattlePresentationObserverMappingTest::RunTest(const FString& Parameters)
	{
		const FBattleSnapshot Snapshot = MakeSelectingObserverSnapshot(FPresentationScenario());
		FBattleCommandDisplayState DisplayState;
		FString Error;
		TestTrue(
			TEXT("The observer-filtered action request converts"),
			FBattlePresentationAdapter::TryBuildCommandDisplayState(
				Snapshot,
				GetPlayerSlot(),
				DisplayState,
				Error));
		TestTrue(TEXT("Successful conversion clears the error"), Error.IsEmpty());
		TestEqual(
			TEXT("The localized command prompt is supplied"),
			DisplayState.NormalPrompt.ToString(),
			FString(TEXT("Choose a command.")));
		TestTrue(TEXT("Fight is available"), DisplayState.Fight.bAvailable);
		TestTrue(TEXT("Bag is available"), DisplayState.Bag.bAvailable);
		TestTrue(TEXT("Switch maps to the Pokemon command"), DisplayState.Pokemon.bAvailable);
		TestFalse(TEXT("Run is unavailable in a Trainer battle"), DisplayState.Run.bAvailable);
		TestEqual(
			TEXT("Run uses its typed localized reason"),
			DisplayState.Run.UnavailableReason.ToString(),
			FString(TEXT("You cannot run from this battle.")));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattlePresentationFullHUDMappingTest,
		"PokemonSolarus.UI.Battle.Presentation.Adapter.FullHUDStateAndNameResolution",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FBattlePresentationFullHUDMappingTest::RunTest(const FString& Parameters)
	{
		const FBattleSnapshot Snapshot = MakeSelectingObserverSnapshot(FPresentationScenario());
		const FPresentationDisplayNameResolver Resolver;
		FBattleHUDDisplayState DisplayState;
		FString Error;
		TestTrue(
			TEXT("The observer projection builds one complete HUD state"),
			FBattlePresentationAdapter::TryBuildHUDDisplayState(
				Snapshot,
				GetPlayerSlot(),
				Resolver,
				DisplayState,
				Error));
		TestTrue(TEXT("A successful full-state build clears its diagnostic"), Error.IsEmpty());
		TestTrue(TEXT("The complete HUD state validates atomically"), DisplayState.IsValid());
		TestEqual(
			TEXT("The player name comes from the required resolver"),
			DisplayState.Player.PokemonName.ToString(),
			FString(TEXT("Presentation Pokemon")));
		TestEqual(TEXT("The player current HP is observer-authoritative"),
			DisplayState.Player.CurrentHP, 100);
		TestEqual(TEXT("The player maximum HP is observer-authoritative"),
			DisplayState.Player.MaxHP, 200);
		TestEqual(
			TEXT("The opponent name comes from the required resolver"),
			DisplayState.Opponent.PokemonName.ToString(),
			FString(TEXT("Presentation Pokemon")));
		TestEqual(TEXT("The opponent current HP is observer-authoritative"),
			DisplayState.Opponent.CurrentHP, 200);
		TestEqual(TEXT("The opponent maximum HP is observer-authoritative"),
			DisplayState.Opponent.MaxHP, 200);
		TestTrue(TEXT("The complete state retains Fight availability"),
			DisplayState.Command.Fight.bAvailable);
		TestFalse(TEXT("The complete state retains the Trainer Run restriction"),
			DisplayState.Command.Run.bAvailable);

		DisplayState.Player.PokemonName = FText::FromString(TEXT("Sentinel"));
		DisplayState.Player.CurrentHP = 1;
		const FPresentationDisplayNameResolver MissingResolver(false);
		TestFalse(
			TEXT("An unresolved required species name rejects the entire HUD state"),
			FBattlePresentationAdapter::TryBuildHUDDisplayState(
				Snapshot,
				GetPlayerSlot(),
				MissingResolver,
				DisplayState,
				Error));
		TestFalse(TEXT("A failed full-state build leaves no valid partial state"),
			DisplayState.IsValid());
		TestTrue(TEXT("A failed full-state build clears the prior player name"),
			DisplayState.Player.PokemonName.IsEmpty());
		TestEqual(TEXT("A failed full-state build clears prior HP"),
			DisplayState.Player.CurrentHP, 0);
		TestFalse(TEXT("A resolver failure supplies an explicit diagnostic"), Error.IsEmpty());
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattlePresentationUnavailableMappingTest,
		"PokemonSolarus.UI.Battle.Presentation.Adapter.UnavailableReasons",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FBattlePresentationUnavailableMappingTest::RunTest(const FString& Parameters)
	{
		FPresentationScenario Scenario;
		Scenario.bHasReserve = false;
		Scenario.bHasUsableItem = false;
		const FBattleSnapshot Snapshot = MakeSelectingObserverSnapshot(Scenario);
		FBattleCommandDisplayState DisplayState;
		FString Error;
		TestTrue(
			TEXT("A complete mix of legal and unavailable commands converts"),
			FBattlePresentationAdapter::TryBuildCommandDisplayState(
				Snapshot,
				GetPlayerSlot(),
				DisplayState,
				Error));
		TestTrue(TEXT("Fight remains available through the core request"), DisplayState.Fight.bAvailable);
		TestFalse(TEXT("An empty Bag is unavailable"), DisplayState.Bag.bAvailable);
		TestEqual(
			TEXT("The Bag reason comes from NoItemRemaining"),
			DisplayState.Bag.UnavailableReason.ToString(),
			FString(TEXT("There are no usable items.")));
		TestFalse(TEXT("No reserve makes Pokemon unavailable"), DisplayState.Pokemon.bAvailable);
		TestEqual(
			TEXT("The switch reason comes from NoLegalTarget"),
			DisplayState.Pokemon.UnavailableReason.ToString(),
			FString(TEXT("There is no Pokémon available to switch.")));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattlePresentationValidationTest,
		"PokemonSolarus.UI.Battle.Presentation.Adapter.ObserverAndPhaseGuards",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FBattlePresentationValidationTest::RunTest(const FString& Parameters)
	{
		TUniquePtr<FBattleEngine> Engine = MakePresentationEngine(FPresentationScenario());
		const FBattleSnapshot SetupSnapshot = Engine->GetSnapshotForObserver(
			MakeNumericId<FTrainerId>(PlayerTrainerValue));
		FBattleCommandDisplayState DisplayState;
		DisplayState.NormalPrompt = FText::FromString(TEXT("Sentinel"));
		DisplayState.Fight.bAvailable = true;
		FString Error;
		TestFalse(
			TEXT("A non-selecting observer snapshot is rejected"),
			FBattlePresentationAdapter::TryBuildCommandDisplayState(
				SetupSnapshot,
				GetPlayerSlot(),
				DisplayState,
				Error));
		TestEqual(
			TEXT("The phase rejection is explicit"),
			Error,
			FString(TEXT("Battle snapshot is not awaiting an action selection.")));
		TestTrue(
			TEXT("Failed conversion clears a prior prompt"),
			DisplayState.NormalPrompt.IsEmpty());
		TestFalse(
			TEXT("Failed conversion clears prior availability"),
			DisplayState.Fight.bAvailable);

		FBattleRejection Rejection;
		check(Engine->TryBeginActionDecisionSequence(Rejection));
		const FBattleSnapshot AuthoritySnapshot = Engine->GetSnapshot();
		TestFalse(
			TEXT("A core-authority snapshot is never accepted by presentation"),
			FBattlePresentationAdapter::TryBuildCommandDisplayState(
				AuthoritySnapshot,
				GetPlayerSlot(),
				DisplayState,
				Error));
		TestEqual(
			TEXT("The observer-safety rejection is explicit"),
			Error,
			FString(TEXT("Battle snapshot is not observer-filtered.")));

		const FBattleSnapshot ObserverSnapshot = Engine->GetSnapshotForObserver(
			MakeNumericId<FTrainerId>(PlayerTrainerValue));
		TestFalse(
			TEXT("A different active slot cannot select another request"),
			FBattlePresentationAdapter::TryBuildCommandDisplayState(
				ObserverSnapshot,
				MakeActiveSlotId(EBattleSide::Player, EBattlePosition::Right),
				DisplayState,
				Error));
		TestEqual(
			TEXT("The missing-request rejection is explicit"),
			Error,
			FString(TEXT("No pending decision request matches the acting active slot.")));
		return true;
	}

}

#endif
