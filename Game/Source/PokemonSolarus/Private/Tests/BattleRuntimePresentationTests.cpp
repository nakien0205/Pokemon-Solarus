#if WITH_DEV_AUTOMATION_TESTS

#include "Battle/BattleDataTableRuntimeSource.h"
#include "Battle/BattleDecision.h"
#include "Battle/BattleDisplayNameResolver.h"
#include "Battle/BattleEngine.h"
#include "Battle/BattleRuntimeSource.h"
#include "BattleTestFactories.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "UI/BattleCommandWidget.h"
#include "UI/BattleGameMode.h"
#include "UI/BattleHUDDisplayState.h"
#include "UI/BattleHUDWidget.h"
#include "UI/BattlePlayerController.h"
#include "UI/BattlePokemonHealthPanel.h"
#include "UObject/UObjectGlobals.h"

class FBattleRuntimePresentationTestFixture
{
public:
	static void Attach(
		ABattleGameMode& GameMode,
		ABattlePlayerController& PlayerController)
	{
		GameMode.AttachToBattlePlayerController(PlayerController);
	}

	static bool HasPresentedRequest(const ABattleGameMode& GameMode)
	{
		return GameMode.bHasPresentedRequest;
	}

	static uint64 GetPresentedStateVersion(const ABattleGameMode& GameMode)
	{
		return GameMode.PresentedStateVersion;
	}

	static uint64 GetPresentedHUDGeneration(const ABattleGameMode& GameMode)
	{
		return GameMode.PresentedHUDGeneration;
	}

	static bool IsSamePresentedRequest(
		const ABattleGameMode& GameMode,
		const FBattleDecisionRequest& Request,
		const uint64 HUDGeneration)
	{
		return GameMode.IsSamePresentedRequest(Request, HUDGeneration);
	}

	static void RememberPresentedRequest(
		ABattleGameMode& GameMode,
		const FBattleDecisionRequest& Request,
		const uint64 HUDGeneration)
	{
		GameMode.RememberPresentedRequest(Request, HUDGeneration);
	}

	static bool Refresh(ABattleGameMode& GameMode)
	{
		return GameMode.RefreshBattleHUDPresentation();
	}

	static void SetRuntimeSource(
		ABattleGameMode& GameMode,
		TUniquePtr<IBattleRuntimeSource>&& RuntimeSource)
	{
		GameMode.SetBattleRuntimeSourceForTesting(MoveTemp(RuntimeSource));
	}

	static bool InitializeRuntime(ABattleGameMode& GameMode)
	{
		return GameMode.InitializeBattleRuntime();
	}

	static FBattleEngine* GetEngine(ABattleGameMode& GameMode)
	{
		return GameMode.BattleEngine.Get();
	}

	static TSharedPtr<const IBattleDisplayNameResolver> GetDisplayNames(
		const ABattleGameMode& GameMode)
	{
		return GameMode.BattleDisplayNames;
	}

	static void SetDisplayNames(
		ABattleGameMode& GameMode,
		TSharedPtr<const IBattleDisplayNameResolver> DisplayNames)
	{
		GameMode.BattleDisplayNames = MoveTemp(DisplayNames);
	}

	static void PrepareHealthPanel(
		UBattlePokemonHealthPanel& Panel,
		const bool bStructurallyReady = true)
	{
		Panel.Text_PokemonName = bStructurallyReady
			? NewObject<UTextBlock>(&Panel)
			: nullptr;
		Panel.ProgressBar_HP = bStructurallyReady
			? NewObject<UProgressBar>(&Panel)
			: nullptr;
		Panel.Text_HPValue = bStructurallyReady
			? NewObject<UTextBlock>(&Panel)
			: nullptr;
		Panel.bNativeConstructed = bStructurallyReady;
	}

	static void PrepareHUD(
		UBattleHUDWidget& HUD,
		UBattleCommandWidget* CommandUI,
		UBattlePokemonHealthPanel* PlayerHealthPanel,
		UBattlePokemonHealthPanel* OpponentHealthPanel)
	{
		HUD.CommandUI = CommandUI;
		HUD.HealthPanel_Player = PlayerHealthPanel;
		HUD.HealthPanel_Opponent = OpponentHealthPanel;
		HUD.LastValidatedDisplayState = FBattleHUDDisplayState();
		HUD.bNativeConstructed = true;
		HUD.bHasValidatedDisplayState = false;
		HUD.bPresentationVisible = false;
		HUD.bCommandInputEnabled = false;
		HUD.SetVisibility(ESlateVisibility::Collapsed);
	}

	static void RemoveRequiredBinding(
		UBattleHUDWidget& HUD,
		const int32 BindingIndex)
	{
		switch (BindingIndex)
		{
		case 0:
			HUD.CommandUI = nullptr;
			break;
		case 1:
			HUD.HealthPanel_Player = nullptr;
			break;
		case 2:
			HUD.HealthPanel_Opponent = nullptr;
			break;
		default:
			HUD.bNativeConstructed = false;
			break;
		}
	}
};

namespace BattleRuntimePresentationTests
{
	using BattleTest::MakeActiveSlotId;
	using BattleTest::MakeNumericId;

	FBattleDecisionRequest MakeActionRequest(const uint64 StateVersion)
	{
		FBattleDecisionRequestSpec Spec;
		Spec.StateVersion = StateVersion;
		Spec.RequestKind = EBattleDecisionRequestKind::Action;
		Spec.DecisionOwnerTrainerId = MakeNumericId<FTrainerId>(1);
		Spec.ActingBattlerId = MakeNumericId<FBattlerId>(11);
		Spec.ActingSlotId = MakeActiveSlotId(
			EBattleSide::Player,
			EBattlePosition::Left);
		Spec.LegalActionKinds.Add(EBattleActionKind::ScriptedEnd);

		FBattleDecisionRequest Request;
		FBattleRejection Rejection;
		check(FBattleDecisionRequest::TryCreate(Spec, Request, Rejection));
		return Request;
	}

	class FScopedNativeTestWorld
	{
	public:
		bool Initialize(FAutomationTestBase& Test)
		{
			if (!WorldWrapper.CreateTestWorld(EWorldType::Game))
			{
				WorldWrapper.ForwardErrorMessages(&Test);
				return false;
			}
			return WorldWrapper.GetTestWorld() != nullptr;
		}

		template <typename ActorType>
		ActorType* SpawnActor()
		{
			UWorld* World = WorldWrapper.GetTestWorld();
			return World ? World->SpawnActor<ActorType>() : nullptr;
		}

	private:
		FTestWorldWrapper WorldWrapper;
	};

	FBattleCommandDisplayState MakeActiveDisplayState()
	{
		FBattleCommandDisplayState State;
		State.NormalPrompt = FText::FromString(TEXT("Choose a command."));
		State.Fight.bAvailable = true;
		State.Bag.bAvailable = true;
		State.Pokemon.bAvailable = true;
		State.Run.bAvailable = true;
		return State;
	}

	FBattleHUDDisplayState MakeValidHUDDisplayState()
	{
		FBattleHUDDisplayState State;
		State.Player.PokemonName = FText::FromString(TEXT("Charizard"));
		State.Player.CurrentHP = 153;
		State.Player.MaxHP = 153;
		State.Opponent.PokemonName = FText::FromString(TEXT("Venusaur"));
		State.Opponent.CurrentHP = 155;
		State.Opponent.MaxHP = 155;
		State.Command = MakeActiveDisplayState();
		return State;
	}

	class FNativeHUDHarness
	{
	public:
		bool Initialize()
		{
			UClass* HUDClass = LoadClass<UBattleHUDWidget>(
				nullptr,
				TEXT("/Game/UI/Battle/WBP_BattleHUD.WBP_BattleHUD_C"));
			UClass* HealthPanelClass = LoadClass<UBattlePokemonHealthPanel>(
				nullptr,
				TEXT("/Game/UI/Battle/WBP_BattlePokemonHealthPanel.WBP_BattlePokemonHealthPanel_C"));
			if (!HUDClass || !HealthPanelClass)
			{
				return false;
			}

			HUD = NewObject<UBattleHUDWidget>(GetTransientPackage(), HUDClass);
			CommandUI = NewObject<UBattleCommandWidget>(HUD);
			PlayerHealthPanel = NewObject<UBattlePokemonHealthPanel>(HUD, HealthPanelClass);
			OpponentHealthPanel = NewObject<UBattlePokemonHealthPanel>(HUD, HealthPanelClass);
			if (!HUD || !CommandUI || !PlayerHealthPanel || !OpponentHealthPanel)
			{
				return false;
			}

			FBattleRuntimePresentationTestFixture::PrepareHealthPanel(*PlayerHealthPanel);
			FBattleRuntimePresentationTestFixture::PrepareHealthPanel(*OpponentHealthPanel);
			FBattleRuntimePresentationTestFixture::PrepareHUD(
				*HUD,
				CommandUI,
				PlayerHealthPanel,
				OpponentHealthPanel);
			return true;
		}

		UBattleHUDWidget* HUD = nullptr;
		UBattleCommandWidget* CommandUI = nullptr;
		UBattlePokemonHealthPanel* PlayerHealthPanel = nullptr;
		UBattlePokemonHealthPanel* OpponentHealthPanel = nullptr;
	};

	constexpr double ProductionRefreshTimeoutSeconds = 15.0;
	const TCHAR* FoundationMapPath = TEXT("/Game/Maps/FoundationMap");
	const TCHAR* RuntimeScenarioPath =
		TEXT("/Game/Data/Battle/Initial/DT_BattleRuntimeScenario.DT_BattleRuntimeScenario");

	bool TryInstallDeterministicRuntime(ABattleGameMode& GameMode)
	{
		TUniquePtr<IBattleRuntimeSource> RuntimeSource =
			MakeUnique<FBattleDataTableRuntimeSource>(
				TSoftObjectPtr<UDataTable>(FSoftObjectPath(RuntimeScenarioPath)));
		FBattleRuntimePresentationTestFixture::SetRuntimeSource(
			GameMode,
			MoveTemp(RuntimeSource));
		return FBattleRuntimePresentationTestFixture::InitializeRuntime(GameMode);
	}

	bool TryMakeFirstFightDecision(
		const FBattleDecisionRequest& Request,
		FBattleDecision& OutDecision,
		FString& OutError)
	{
		OutDecision = FBattleDecision();
		OutError.Reset();
		if (!Request.GetLegalActionKinds().Contains(EBattleActionKind::Fight)
			|| Request.GetLegalMoveIds().IsEmpty())
		{
			OutError = TEXT("The deterministic runtime did not expose a legal Fight choice.");
			return false;
		}

		const FMoveId MoveId = Request.GetLegalMoveIds()[0];
		if (Request.GetAutomaticallyTargetedMoveIds().Contains(MoveId))
		{
			if (FBattleDecision::TryCreateAutomaticallyTargetedFight(
				Request.GetStateVersion(),
				Request.GetDecisionOwnerTrainerId(),
				Request.GetActingBattlerId(),
				MoveId,
				OutDecision))
			{
				return true;
			}
			OutError = TEXT("The automatically targeted Fight decision was invalid.");
			return false;
		}

		for (const FBattleMoveTargetOption& Target : Request.GetLegalMoveTargets())
		{
			if (Target.MoveId == MoveId
				&& FBattleDecision::TryCreateFight(
					Request.GetStateVersion(),
					Request.GetDecisionOwnerTrainerId(),
					Request.GetActingBattlerId(),
					MoveId,
					Target.ActiveSlotId,
					OutDecision))
			{
				return true;
			}
		}

		OutError = TEXT("The deterministic runtime exposed no legal target for its first move.");
		return false;
	}

	bool TryAdvanceToNextActionRequest(
		FBattleEngine& Engine,
		FString& OutError)
	{
		OutError.Reset();
		int32 DecisionGuard = 0;
		while (Engine.GetPendingDecision().IsSet() && DecisionGuard++ < 4)
		{
			const FBattleDecisionRequest Request =
				Engine.GetPendingDecision().GetValue();
			FBattleDecision Decision;
			if (!TryMakeFirstFightDecision(Request, Decision, OutError))
			{
				return false;
			}
			if (!Engine.SubmitDecision(Decision).WasAccepted())
			{
				OutError = TEXT("The deterministic Fight decision was rejected.");
				return false;
			}
		}
		if (DecisionGuard >= 4 || Engine.GetSnapshot().GetPhase() != EBattlePhase::Locked)
		{
			OutError = TEXT("The deterministic runtime did not lock its first turn.");
			return false;
		}

		int32 ResolutionGuard = 0;
		while ((Engine.GetSnapshot().GetPhase() == EBattlePhase::Locked
				|| Engine.GetSnapshot().GetPhase() == EBattlePhase::Resolving)
			&& !Engine.GetPendingDecision().IsSet()
			&& ResolutionGuard++ < 8)
		{
			if (!Engine.BeginNextLockedAction().WasAccepted())
			{
				OutError = TEXT("The next locked Fight action could not start.");
				return false;
			}
			const TOptional<FBattleLockedAction> CurrentAction =
				Engine.GetCurrentLockedAction();
			if (!CurrentAction.IsSet())
			{
				continue;
			}
			if (CurrentAction->Decision.GetActionKind() != EBattleActionKind::Fight)
			{
				OutError = TEXT("The deterministic turn produced a non-Fight locked action.");
				return false;
			}
			if (!Engine.CommitCurrentMoveAfterPreMoveGates().WasAccepted())
			{
				OutError = TEXT("The current Fight action could not commit.");
				return false;
			}
			if (!Engine.GetCurrentLockedAction().IsSet())
			{
				continue;
			}
			if (!Engine.ResolveCurrentMoveTargets().WasAccepted())
			{
				OutError = TEXT("The current Fight action could not resolve its targets.");
				return false;
			}
			if (!Engine.GetCurrentLockedAction().IsSet())
			{
				continue;
			}
			if (!Engine.ExecuteCurrentMoveEffects().WasAccepted())
			{
				OutError = TEXT("The current Fight action could not execute its effects.");
				return false;
			}
		}
		if (ResolutionGuard >= 8
			|| Engine.GetSnapshot().GetPhase() != EBattlePhase::EndOfTurn)
		{
			OutError = TEXT("The deterministic runtime did not reach EndOfTurn.");
			return false;
		}
		if (!Engine.ResolveEndTurn().WasAccepted())
		{
			OutError = TEXT("The deterministic end-turn boundary was rejected.");
			return false;
		}
		if (Engine.GetSnapshot().GetPhase() != EBattlePhase::Selecting
			|| !Engine.GetPendingDecision().IsSet())
		{
			OutError = TEXT("The deterministic runtime did not create the next action request.");
			return false;
		}
		return true;
	}

	class FFaultInjectingDisplayNameResolver final
		: public IBattleDisplayNameResolver
	{
	public:
		FFaultInjectingDisplayNameResolver(
			TSharedPtr<const IBattleDisplayNameResolver> InInner,
			TFunction<void()>&& InFault)
			: Inner(MoveTemp(InInner))
			, Fault(MoveTemp(InFault))
		{
		}

		virtual bool TryResolveSpeciesName(
			const FSpeciesFormId SpeciesFormId,
			FText& OutDisplayName) const override
		{
			if (!Inner.IsValid()
				|| !Inner->TryResolveSpeciesName(SpeciesFormId, OutDisplayName))
			{
				return false;
			}
			if (!bFaultInjected)
			{
				bFaultInjected = true;
				Fault();
			}
			return true;
		}

		bool WasTriggered() const
		{
			return bFaultInjected;
		}

	private:
		TSharedPtr<const IBattleDisplayNameResolver> Inner;
		TFunction<void()> Fault;
		mutable bool bFaultInjected = false;
	};

	class FProductionRefreshCommandBase : public IAutomationLatentCommand
	{
	public:
		explicit FProductionRefreshCommandBase(FAutomationTestBase& InTest)
			: Test(InTest)
		{
		}

		virtual bool Update() override
		{
			if (FirstUpdateTimeSeconds <= 0.0)
			{
				FirstUpdateTimeSeconds = FPlatformTime::Seconds();
			}

			UWorld* World = AutomationCommon::GetAnyGameWorld();
			if (!IsValid(World) || !World->HasBegunPlay())
			{
				return WaitOrFail(TEXT("the FoundationMap PIE world"));
			}
			ABattleGameMode* GameMode = Cast<ABattleGameMode>(World->GetAuthGameMode());
			ABattlePlayerController* PlayerController =
				Cast<ABattlePlayerController>(World->GetFirstPlayerController());
			UBattleHUDWidget* HUD = PlayerController
				? PlayerController->GetBattleHUDWidget()
				: nullptr;
			if (!IsValid(GameMode)
				|| !IsValid(PlayerController)
				|| !IsValid(HUD)
				|| !PlayerController->IsBattleHUDAvailable()
				|| !HUD->IsPresentationVisible()
				|| !FBattleRuntimePresentationTestFixture::HasPresentedRequest(*GameMode))
			{
				return WaitOrFail(TEXT("the complete production Battle HUD presentation"));
			}

			RunScenario(*GameMode, *PlayerController, *HUD);
			return true;
		}

	protected:
		virtual void RunScenario(
			ABattleGameMode& GameMode,
			ABattlePlayerController& PlayerController,
			UBattleHUDWidget& HUD) = 0;

		FAutomationTestBase& Test;

	private:
		bool WaitOrFail(const TCHAR* WaitingFor)
		{
			if (FPlatformTime::Seconds() - FirstUpdateTimeSeconds
				< ProductionRefreshTimeoutSeconds)
			{
				return false;
			}
			Test.AddError(FString::Printf(
				TEXT("Timed out waiting for %s."),
				WaitingFor));
			return true;
		}

		double FirstUpdateTimeSeconds = 0.0;
	};

	class FChangedRequestRefreshCommand final : public FProductionRefreshCommandBase
	{
	public:
		using FProductionRefreshCommandBase::FProductionRefreshCommandBase;

	private:
		virtual void RunScenario(
			ABattleGameMode& GameMode,
			ABattlePlayerController& PlayerController,
			UBattleHUDWidget& HUD) override
		{
			if (!Test.TestTrue(
				TEXT("The injected data-driven runtime initializes"),
				TryInstallDeterministicRuntime(GameMode)))
			{
				return;
			}
			UBattleCommandWidget* CommandUI = Cast<UBattleCommandWidget>(
				HUD.GetWidgetFromName(TEXT("CommandUI")));
			if (!Test.TestNotNull(TEXT("The real HUD exposes CommandUI"), CommandUI))
			{
				return;
			}

			FBattleHUDDisplayState BeforeState;
			if (!Test.TestTrue(
				TEXT("The injected runtime produced a complete initial HUD state"),
				HUD.TryGetLastValidatedDisplayState(BeforeState)))
			{
				return;
			}
			const uint64 BeforeVersion =
				FBattleRuntimePresentationTestFixture::GetPresentedStateVersion(GameMode);
			int32 FocusSignalCount = 0;
			int32 TextSignalCount = 0;
			const FDelegateHandle FocusHandle =
				CommandUI->GetCommandFocusChangedNativeDelegate().AddLambda(
					[&FocusSignalCount](const EBattleUICommand)
					{
						++FocusSignalCount;
					});
			const FDelegateHandle TextHandle =
				CommandUI->GetBattleTextChangedNativeDelegate().AddLambda(
					[&TextSignalCount](const FText&)
					{
						++TextSignalCount;
					});

			Test.TestTrue(
				TEXT("Refreshing the same request succeeds through GameMode"),
				FBattleRuntimePresentationTestFixture::Refresh(GameMode));
			Test.TestEqual(
				TEXT("The same request is not delivered twice"),
				FocusSignalCount,
				0);

			FBattleEngine* Engine =
				FBattleRuntimePresentationTestFixture::GetEngine(GameMode);
			FString AdvanceError;
			if (!Test.TestTrue(
				TEXT("The injected runtime advances to its next action request"),
				Engine && TryAdvanceToNextActionRequest(*Engine, AdvanceError)))
			{
				if (!AdvanceError.IsEmpty())
				{
					Test.AddError(AdvanceError);
				}
				CommandUI->GetCommandFocusChangedNativeDelegate().Remove(FocusHandle);
				CommandUI->GetBattleTextChangedNativeDelegate().Remove(TextHandle);
				return;
			}
			const TOptional<FBattleDecisionRequest> ChangedRequest =
				Engine->GetPendingDecision();
			Test.TestTrue(
				TEXT("The next request has a new state version"),
				ChangedRequest.IsSet()
					&& ChangedRequest->GetStateVersion() != BeforeVersion);

			Test.TestTrue(
				TEXT("GameMode delivers the changed request through the real controller and HUD"),
				FBattleRuntimePresentationTestFixture::Refresh(GameMode));
			CommandUI->GetCommandFocusChangedNativeDelegate().Remove(FocusHandle);
			CommandUI->GetBattleTextChangedNativeDelegate().Remove(TextHandle);
			Test.TestEqual(
				TEXT("Only the changed request emits a new focus notification"),
				FocusSignalCount,
				1);
			Test.TestEqual(
				TEXT("Only the changed request emits new Battle text"),
				TextSignalCount,
				1);
			if (ChangedRequest.IsSet())
			{
				Test.TestEqual(
					TEXT("The cache commits the changed request only after delivery"),
					FBattleRuntimePresentationTestFixture::GetPresentedStateVersion(GameMode),
					ChangedRequest->GetStateVersion());
			}

			FBattleHUDDisplayState AfterState;
			Test.TestTrue(
				TEXT("The real HUD caches the changed full presentation"),
				HUD.TryGetLastValidatedDisplayState(AfterState));
			Test.TestTrue(
				TEXT("The changed full presentation contains authoritative turn damage"),
				AfterState.Player.CurrentHP != BeforeState.Player.CurrentHP
					|| AfterState.Opponent.CurrentHP != BeforeState.Opponent.CurrentHP);
			Test.TestTrue(
				TEXT("The real controller remains ready after changed-request delivery"),
				PlayerController.IsBattleCommandInputReady());
		}
	};

	class FFailedApplyRefreshCommand final : public FProductionRefreshCommandBase
	{
	public:
		using FProductionRefreshCommandBase::FProductionRefreshCommandBase;

	private:
		virtual void RunScenario(
			ABattleGameMode& GameMode,
			ABattlePlayerController& PlayerController,
			UBattleHUDWidget& HUD) override
		{
			if (!Test.TestTrue(
				TEXT("The injected data-driven runtime initializes"),
				TryInstallDeterministicRuntime(GameMode)))
			{
				return;
			}
			FBattleEngine* Engine =
				FBattleRuntimePresentationTestFixture::GetEngine(GameMode);
			FString AdvanceError;
			if (!Test.TestTrue(
				TEXT("The injected runtime advances before fault injection"),
				Engine && TryAdvanceToNextActionRequest(*Engine, AdvanceError)))
			{
				if (!AdvanceError.IsEmpty())
				{
					Test.AddError(AdvanceError);
				}
				return;
			}

			TSharedPtr<const IBattleDisplayNameResolver> InnerResolver =
				FBattleRuntimePresentationTestFixture::GetDisplayNames(GameMode);
			TSharedPtr<FFaultInjectingDisplayNameResolver> FaultResolver =
				MakeShared<FFaultInjectingDisplayNameResolver>(
					InnerResolver,
					[&HUD]()
					{
						FBattleRuntimePresentationTestFixture::RemoveRequiredBinding(
							HUD,
							2);
					});
			FBattleRuntimePresentationTestFixture::SetDisplayNames(
				GameMode,
				FaultResolver);
			Test.AddExpectedError(
				TEXT("Cannot apply a Battle HUD display state because the HUD is structurally unavailable."),
				EAutomationExpectedErrorFlags::Contains,
				1);
			Test.AddExpectedError(
				TEXT("The Battle HUD update was rejected: The Battle controller rejected a complete HUD display state."),
				EAutomationExpectedErrorFlags::Contains,
				1);
			Test.TestFalse(
				TEXT("GameMode reports the injected full-state application failure"),
				FBattleRuntimePresentationTestFixture::Refresh(GameMode));
			Test.TestTrue(
				TEXT("The fault was injected during GameMode full-state construction"),
				FaultResolver->WasTriggered());
			Test.TestFalse(
				TEXT("A failed controller/HUD application is not cached"),
				FBattleRuntimePresentationTestFixture::HasPresentedRequest(GameMode));
			Test.TestEqual(
				TEXT("A failed application clears the cached HUD generation"),
				FBattleRuntimePresentationTestFixture::GetPresentedHUDGeneration(GameMode),
				static_cast<uint64>(0));
			Test.TestNull(
				TEXT("The controller discards the structurally invalid HUD"),
				PlayerController.GetBattleHUDWidget());
			Test.TestEqual(
				TEXT("The discarded HUD root is fail-closed"),
				HUD.GetVisibility(),
				ESlateVisibility::Collapsed);
			Test.TestFalse(
				TEXT("The discarded HUD cannot accept command input"),
				HUD.IsCommandInputEnabled());
		}
	};

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleHUDInitialFailClosedTest,
		"PokemonSolarus.UI.Battle.Runtime.HUD.InitialFailuresRemainCollapsed",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FBattleHUDInitialFailClosedTest::RunTest(const FString& Parameters)
	{
		for (int32 MissingBindingIndex = 0; MissingBindingIndex < 4; ++MissingBindingIndex)
		{
			FNativeHUDHarness Harness;
			if (!TestTrue(
				TEXT("A concrete HUD fixture loads for structural validation"),
				Harness.Initialize()))
			{
				return false;
			}
			FBattleRuntimePresentationTestFixture::RemoveRequiredBinding(
				*Harness.HUD,
				MissingBindingIndex);
			AddExpectedError(
				TEXT("Rejected a full Battle HUD display state because required widget bindings are unavailable."),
				EAutomationExpectedErrorFlags::Contains,
				1);
			TestFalse(
				*FString::Printf(
					TEXT("Missing structural requirement %d rejects initial presentation"),
					MissingBindingIndex),
				Harness.HUD->ApplyHUDDisplayState(MakeValidHUDDisplayState()));
			TestEqual(
				TEXT("A structural failure keeps the root collapsed"),
				Harness.HUD->GetVisibility(),
				ESlateVisibility::Collapsed);
			TestFalse(
				TEXT("A structural failure never marks presentation visible"),
				Harness.HUD->IsPresentationVisible());
			TestFalse(
				TEXT("A structural failure keeps command input disabled"),
				Harness.HUD->IsCommandInputEnabled());
			FBattleHUDDisplayState UnsetState;
			TestFalse(
				TEXT("A structural failure caches no full display state"),
				Harness.HUD->TryGetLastValidatedDisplayState(UnsetState));
		}

		TArray<FBattleHUDDisplayState> InvalidStates;
		FBattleHUDDisplayState EmptyPlayerName = MakeValidHUDDisplayState();
		EmptyPlayerName.Player.PokemonName = FText::GetEmpty();
		InvalidStates.Add(EmptyPlayerName);
		FBattleHUDDisplayState PlayerHPAboveMaximum = MakeValidHUDDisplayState();
		PlayerHPAboveMaximum.Player.CurrentHP = PlayerHPAboveMaximum.Player.MaxHP + 1;
		InvalidStates.Add(PlayerHPAboveMaximum);
		FBattleHUDDisplayState EmptyOpponentName = MakeValidHUDDisplayState();
		EmptyOpponentName.Opponent.PokemonName = FText::FromString(TEXT("   "));
		InvalidStates.Add(EmptyOpponentName);
		FBattleHUDDisplayState InvalidOpponentMaximum = MakeValidHUDDisplayState();
		InvalidOpponentMaximum.Opponent.MaxHP = 0;
		InvalidStates.Add(InvalidOpponentMaximum);
		FBattleHUDDisplayState EmptyPrompt = MakeValidHUDDisplayState();
		EmptyPrompt.Command.NormalPrompt = FText::GetEmpty();
		InvalidStates.Add(EmptyPrompt);
		FBattleHUDDisplayState MissingUnavailableReason = MakeValidHUDDisplayState();
		MissingUnavailableReason.Command.Bag.bAvailable = false;
		MissingUnavailableReason.Command.Bag.UnavailableReason = FText::GetEmpty();
		InvalidStates.Add(MissingUnavailableReason);

		for (int32 InvalidIndex = 0; InvalidIndex < InvalidStates.Num(); ++InvalidIndex)
		{
			FNativeHUDHarness Harness;
			if (!TestTrue(
				TEXT("A concrete HUD fixture loads for full-state validation"),
				Harness.Initialize()))
			{
				return false;
			}
			AddExpectedError(
				TEXT("Rejected an incomplete or invalid full Battle HUD display state."),
				EAutomationExpectedErrorFlags::Contains,
				1);
			TestFalse(
				*FString::Printf(
					TEXT("Invalid initial full state %d is rejected atomically"),
					InvalidIndex),
				Harness.HUD->ApplyHUDDisplayState(InvalidStates[InvalidIndex]));
			TestEqual(
				TEXT("Invalid initial data keeps the root collapsed"),
				Harness.HUD->GetVisibility(),
				ESlateVisibility::Collapsed);
			TestFalse(
				TEXT("Invalid initial data keeps command input disabled"),
				Harness.HUD->IsCommandInputEnabled());
			FBattleHUDDisplayState UnsetState;
			TestFalse(
				TEXT("Invalid initial data is never cached as presented"),
				Harness.HUD->TryGetLastValidatedDisplayState(UnsetState));
		}
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleHUDLaterInvalidUpdateTest,
		"PokemonSolarus.UI.Battle.Runtime.HUD.LaterInvalidUpdatePreservesVisualsAndDisablesInput",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FBattleHUDLaterInvalidUpdateTest::RunTest(const FString& Parameters)
	{
		FNativeHUDHarness Harness;
		if (!TestTrue(
			TEXT("A concrete structurally ready HUD fixture loads"),
			Harness.Initialize()))
		{
			return false;
		}

		const FBattleHUDDisplayState InitialState = MakeValidHUDDisplayState();
		TestTrue(
			TEXT("A complete initial state reveals the HUD"),
			Harness.HUD->ApplyHUDDisplayState(InitialState));
		TestTrue(
			TEXT("The successful initial presentation is visible"),
			Harness.HUD->IsPresentationVisible());
		TestTrue(
			TEXT("The successful initial presentation enables command input"),
			Harness.HUD->IsCommandInputEnabled());

		FBattleHUDDisplayState InvalidUpdate = InitialState;
		InvalidUpdate.Player.CurrentHP = InvalidUpdate.Player.MaxHP + 1;
		AddExpectedError(
			TEXT("Rejected an incomplete or invalid full Battle HUD display state."),
			EAutomationExpectedErrorFlags::Contains,
			1);
		TestFalse(
			TEXT("A later invalid full-state update is rejected"),
			Harness.HUD->ApplyHUDDisplayState(InvalidUpdate));
		TestEqual(
			TEXT("A later invalid data update preserves the visible root"),
			Harness.HUD->GetVisibility(),
			ESlateVisibility::Visible);
		TestTrue(
			TEXT("The last valid full presentation remains visible"),
			Harness.HUD->IsPresentationVisible());
		TestFalse(
			TEXT("The invalid update disables command input"),
			Harness.HUD->IsCommandInputEnabled());
		TestFalse(
			TEXT("Disabled input blocks navigation"),
			Harness.HUD->NavigateCommandMenu(FVector2D(1.0, 0.0)));
		TestFalse(
			TEXT("Disabled input blocks confirmation"),
			Harness.HUD->ConfirmCommandMenu());

		FBattleHUDDisplayState PreservedState;
		TestTrue(
			TEXT("The last valid full state remains cached"),
			Harness.HUD->TryGetLastValidatedDisplayState(PreservedState));
		TestEqual(
			TEXT("The rejected HP did not replace the visible HP"),
			PreservedState.Player.CurrentHP,
			InitialState.Player.CurrentHP);
		TestEqual(
			TEXT("The rejected update did not replace the visible name"),
			PreservedState.Player.PokemonName.ToString(),
			InitialState.Player.PokemonName.ToString());
		FBattleCommandDisplayState PreservedCommand;
		TestTrue(
			TEXT("The command child keeps its last valid visual state"),
			Harness.CommandUI->TryGetDisplayState(PreservedCommand));
		TestEqual(
			TEXT("The visible command prompt remains unchanged"),
			PreservedCommand.NormalPrompt.ToString(),
			InitialState.Command.NormalPrompt.ToString());

		FBattleHUDDisplayState RecoveryState = InitialState;
		RecoveryState.Player.CurrentHP = 100;
		TestTrue(
			TEXT("A later valid state recovers presentation"),
			Harness.HUD->ApplyHUDDisplayState(RecoveryState));
		TestTrue(
			TEXT("Recovery re-enables command input"),
			Harness.HUD->IsCommandInputEnabled());
		TestTrue(
			TEXT("Recovery replaces the cached state atomically"),
			Harness.HUD->TryGetLastValidatedDisplayState(PreservedState));
		TestEqual(
			TEXT("The recovered HP becomes current"),
			PreservedState.Player.CurrentHP,
			100);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleRuntimeFailedApplyCacheTest,
		"PokemonSolarus.UI.Battle.Runtime.Cache.FailedFullStateApplyIsNotRemembered",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FBattleRuntimeFailedApplyCacheTest::RunTest(const FString& Parameters)
	{
		if (!AutomationOpenMap(FoundationMapPath))
		{
			AddError(TEXT("AutomationOpenMap could not open FoundationMap."));
			return false;
		}
		ADD_LATENT_AUTOMATION_COMMAND(FFailedApplyRefreshCommand(*this));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleRuntimeSameRequestCacheTest,
		"PokemonSolarus.UI.Battle.Runtime.Cache.SameRequestSameGenerationDeduplicates",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FBattleRuntimeSameRequestCacheTest::RunTest(const FString& Parameters)
	{
		FScopedNativeTestWorld TestWorld;
		if (!TestTrue(TEXT("A native Game world is created"), TestWorld.Initialize(*this)))
		{
			return false;
		}
		ABattleGameMode* GameMode = TestWorld.SpawnActor<ABattleGameMode>();
		if (!TestNotNull(TEXT("A real GameMode actor is spawned"), GameMode))
		{
			return false;
		}

		const FBattleDecisionRequest Request = MakeActionRequest(80);
		constexpr uint64 HUDGeneration = 41;
		TestFalse(
			TEXT("A request is deliverable before any successful presentation"),
			FBattleRuntimePresentationTestFixture::IsSamePresentedRequest(
				*GameMode,
				Request,
				HUDGeneration));
		FBattleRuntimePresentationTestFixture::RememberPresentedRequest(
			*GameMode,
			Request,
			HUDGeneration);
		TestTrue(
			TEXT("The same request and same HUD generation are deduplicated"),
			FBattleRuntimePresentationTestFixture::IsSamePresentedRequest(
				*GameMode,
				Request,
				HUDGeneration));
		TestEqual(
			TEXT("The cache records the exact HUD generation"),
			FBattleRuntimePresentationTestFixture::GetPresentedHUDGeneration(*GameMode),
			HUDGeneration);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleRuntimeChangedRequestCacheTest,
		"PokemonSolarus.UI.Battle.Runtime.Cache.ChangedRequestRemainsDeliverable",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FBattleRuntimeChangedRequestCacheTest::RunTest(const FString& Parameters)
	{
		if (!AutomationOpenMap(FoundationMapPath))
		{
			AddError(TEXT("AutomationOpenMap could not open FoundationMap."));
			return false;
		}
		ADD_LATENT_AUTOMATION_COMMAND(FChangedRequestRefreshCommand(*this));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleRuntimeReplacementCacheTest,
		"PokemonSolarus.UI.Battle.Runtime.Cache.ControllerOrHUDReplacementRemainsDeliverable",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FBattleRuntimeReplacementCacheTest::RunTest(const FString& Parameters)
	{
		FScopedNativeTestWorld TestWorld;
		if (!TestTrue(TEXT("A native Game world is created"), TestWorld.Initialize(*this)))
		{
			return false;
		}
		ABattleGameMode* GameMode = TestWorld.SpawnActor<ABattleGameMode>();
		ABattlePlayerController* FirstController =
			TestWorld.SpawnActor<ABattlePlayerController>();
		ABattlePlayerController* ReplacementController =
			TestWorld.SpawnActor<ABattlePlayerController>();
		if (!TestNotNull(TEXT("A real GameMode actor is spawned"), GameMode)
			|| !TestNotNull(TEXT("The first real controller actor is spawned"), FirstController)
			|| !TestNotNull(
				TEXT("The replacement real controller actor is spawned"),
				ReplacementController))
		{
			return false;
		}

		const FBattleDecisionRequest Request = MakeActionRequest(100);
		FBattleRuntimePresentationTestFixture::Attach(*GameMode, *FirstController);
		FBattleRuntimePresentationTestFixture::RememberPresentedRequest(
			*GameMode,
			Request,
			51);
		FBattleRuntimePresentationTestFixture::Attach(*GameMode, *FirstController);
		TestTrue(
			TEXT("Reattaching the same controller preserves the same-view cache"),
			FBattleRuntimePresentationTestFixture::HasPresentedRequest(*GameMode));

		FBattleRuntimePresentationTestFixture::Attach(*GameMode, *ReplacementController);
		TestFalse(
			TEXT("Attaching a replacement controller clears the prior view cache"),
			FBattleRuntimePresentationTestFixture::HasPresentedRequest(*GameMode));

		FBattleRuntimePresentationTestFixture::RememberPresentedRequest(
			*GameMode,
			Request,
			51);
		TestFalse(
			TEXT("The same request on a replacement HUD generation remains deliverable"),
			FBattleRuntimePresentationTestFixture::IsSamePresentedRequest(
				*GameMode,
				Request,
				52));
		return true;
	}
}

#endif
