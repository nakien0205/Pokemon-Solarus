#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
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

class FBattleHUDProductionLifecycleTestFixture final
{
public:
	static bool TryAttachStructurallyInvalidHUD(
		ABattlePlayerController& PlayerController,
		UBattleHUDWidget& CandidateHUD)
	{
		PlayerController.DiscardBattleHUD();
		CandidateHUD.HealthPanel_Player = nullptr;
		const bool bAttached =
			PlayerController.TryAttachAndValidateBattleHUD(CandidateHUD);
		if (!bAttached)
		{
			PlayerController.RemoveBattleHUDFromScreen(CandidateHUD);
		}
		return bAttached;
	}

	static bool TryCreateBattleHUDWithTemporaryClass(
		ABattlePlayerController& PlayerController,
		const TSoftClassPtr<UBattleHUDWidget>& TemporaryHUDClass)
	{
		const TSoftClassPtr<UBattleHUDWidget> ConfiguredHUDClass =
			PlayerController.BattleHUDWidgetClass;
		PlayerController.BattleHUDWidgetClass = TemporaryHUDClass;
		const bool bCreated = PlayerController.TryCreateBattleHUD();
		PlayerController.BattleHUDWidgetClass = ConfiguredHUDClass;
		return bCreated;
	}

	static bool TryCreateConfiguredBattleHUD(
		ABattlePlayerController& PlayerController)
	{
		return PlayerController.TryCreateBattleHUD();
	}
};

namespace BattleHUDProductionLifecycleTests
{
	constexpr double ProductionLifecycleTimeoutSeconds = 15.0;
	const TCHAR* FoundationMapPath = TEXT("/Game/Maps/FoundationMap");
	const TCHAR* BattleHUDClassPath =
		TEXT("/Game/UI/Battle/WBP_BattleHUD.WBP_BattleHUD_C");

	class FObserveProductionBattleHUDCommand final : public IAutomationLatentCommand
	{
	public:
		explicit FObserveProductionBattleHUDCommand(FAutomationTestBase& InTest)
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
				return WaitOrFail(TEXT("the FoundationMap PIE world to begin play"));
			}

			ABattleGameMode* GameMode = Cast<ABattleGameMode>(World->GetAuthGameMode());
			if (!IsValid(GameMode))
			{
				return WaitOrFail(TEXT("the production ABattleGameMode"));
			}

			ABattlePlayerController* PlayerController =
				Cast<ABattlePlayerController>(World->GetFirstPlayerController());
			if (!IsValid(PlayerController))
			{
				return WaitOrFail(TEXT("the production ABattlePlayerController"));
			}

			UBattleHUDWidget* HUD = PlayerController->GetBattleHUDWidget();
			if (!IsValid(HUD))
			{
				return WaitOrFail(TEXT("the production Battle HUD instance"));
			}

			FBattleHUDDisplayState DisplayState;
			const bool bHasCompleteDisplayState =
				HUD->TryGetLastValidatedDisplayState(DisplayState)
				&& DisplayState.IsValid();
			const bool bProductionPresentationReady =
				PlayerController->IsBattleHUDAvailable()
				&& PlayerController->IsBattleHUDVisible()
				&& PlayerController->IsBattleCommandInputReady()
				&& HUD->IsPresentationVisible()
				&& HUD->IsCommandInputEnabled()
				&& bHasCompleteDisplayState;

			if (!bProductionPresentationReady)
			{
				const bool bRootIsCollapsed =
					HUD->GetVisibility() == ESlateVisibility::Collapsed;
				if (!bHasCompleteDisplayState && !bRootIsCollapsed)
				{
					Test.AddError(
						TEXT("The production Battle HUD became visible before a complete validated display state existed."));
					return true;
				}

				return WaitOrFail(TEXT("the complete fail-closed Battle HUD presentation"));
			}

			ValidateProductionLifecycle(
				*World,
				*GameMode,
				*PlayerController,
				*HUD,
				DisplayState);
			return true;
		}

	private:
		bool WaitOrFail(const TCHAR* WaitingFor)
		{
			LastWaitingFor = WaitingFor;
			if (FPlatformTime::Seconds() - FirstUpdateTimeSeconds
				< ProductionLifecycleTimeoutSeconds)
			{
				return false;
			}

			Test.AddError(FString::Printf(
				TEXT("Timed out after %.0f seconds waiting for %s."),
				ProductionLifecycleTimeoutSeconds,
				*LastWaitingFor));
			return true;
		}

		void ValidateProductionLifecycle(
			UWorld& World,
			ABattleGameMode& GameMode,
			ABattlePlayerController& PlayerController,
			UBattleHUDWidget& HUD,
			const FBattleHUDDisplayState& DisplayState)
		{
			Test.TestTrue(
				TEXT("AutomationOpenMap created a PIE game world"),
				World.WorldType == EWorldType::PIE);
			Test.TestFalse(
				TEXT("The production GameMode is not a class-default object"),
				GameMode.HasAnyFlags(RF_ClassDefaultObject));
			Test.TestFalse(
				TEXT("The production PlayerController is not a class-default object"),
				PlayerController.HasAnyFlags(RF_ClassDefaultObject));

			UClass* HUDClass = HUD.GetClass();
			Test.TestNotNull(TEXT("The production HUD has a generated class"), HUDClass);
			if (HUDClass)
			{
				Test.TestEqual(
					TEXT("The controller soft-loaded the production WBP_BattleHUD class"),
					HUDClass->GetPathName(),
					FString(BattleHUDClassPath));
				Test.TestTrue(
					TEXT("The production HUD class was compiled from Blueprint"),
					HUDClass->HasAnyClassFlags(CLASS_CompiledFromBlueprint));
				Test.TestTrue(
					TEXT("The production HUD is a non-CDO widget instance"),
					&HUD != HUDClass->GetDefaultObject());
			}
			Test.TestFalse(
				TEXT("The production HUD has no class-default flag"),
				HUD.HasAnyFlags(RF_ClassDefaultObject));
			Test.TestTrue(
				TEXT("The production HUD belongs to the real Battle PlayerController"),
				HUD.GetOwningPlayer() == &PlayerController);
			Test.TestTrue(
				TEXT("The production HUD belongs to the FoundationMap PIE world"),
				HUD.GetWorld() == &World);

			Test.TestTrue(
				TEXT("AddToPlayerScreen attached the production HUD to the viewport"),
				HUD.IsInViewport());
			Test.TestTrue(
				TEXT("The production HUD has a constructed Slate widget"),
				HUD.IsConstructed() && HUD.GetCachedWidget().IsValid());
			Test.TestTrue(
				TEXT("NativeConstruct validated every required HUD binding"),
				HUD.IsStructurallyReady());
			Test.TestTrue(
				TEXT("The controller reports the constructed HUD as available"),
				PlayerController.IsBattleHUDAvailable());
			Test.TestTrue(
				TEXT("The compatibility readiness query matches structural availability"),
				PlayerController.IsBattleHUDReady());
			Test.TestTrue(
				TEXT("The attached HUD owns a non-zero presentation generation"),
				PlayerController.GetBattleHUDPresentationGeneration() > 0);

			Test.TestTrue(
				TEXT("The visible root has one complete validated HUD state"),
				DisplayState.IsValid());
			Test.TestTrue(
				TEXT("The root becomes visible only after full state application"),
				HUD.GetVisibility() == ESlateVisibility::Visible
					&& HUD.IsPresentationVisible()
					&& PlayerController.IsBattleHUDVisible());

			Test.TestEqual(
				TEXT("The local authoritative active Pokemon is Charizard"),
				DisplayState.Player.PokemonName.ToString(),
				FString(TEXT("Charizard")));
			Test.TestEqual(
				TEXT("Charizard starts at authoritative current HP"),
				DisplayState.Player.CurrentHP,
				153);
			Test.TestEqual(
				TEXT("Charizard has authoritative maximum HP"),
				DisplayState.Player.MaxHP,
				153);
			Test.TestEqual(
				TEXT("The opponent authoritative active Pokemon is Venusaur"),
				DisplayState.Opponent.PokemonName.ToString(),
				FString(TEXT("Venusaur")));
			Test.TestEqual(
				TEXT("Venusaur starts at authoritative current HP"),
				DisplayState.Opponent.CurrentHP,
				155);
			Test.TestEqual(
				TEXT("Venusaur has authoritative maximum HP"),
				DisplayState.Opponent.MaxHP,
				155);

			Test.TestEqual(
				TEXT("The production command prompt is display-ready"),
				DisplayState.Command.NormalPrompt.ToString(),
				FString(TEXT("Choose a command.")));
			Test.TestTrue(
				TEXT("Fight is available for the approved matchup"),
				DisplayState.Command.Fight.bAvailable);
			Test.TestFalse(
				TEXT("The empty Bag is unavailable"),
				DisplayState.Command.Bag.bAvailable);
			Test.TestEqual(
				TEXT("The empty Bag has its authoritative unavailable reason"),
				DisplayState.Command.Bag.UnavailableReason.ToString(),
				FString(TEXT("There are no usable items.")));
			Test.TestFalse(
				TEXT("Pokemon is unavailable without a reserve"),
				DisplayState.Command.Pokemon.bAvailable);
			Test.TestEqual(
				TEXT("The unavailable Pokemon command has its typed reason"),
				DisplayState.Command.Pokemon.UnavailableReason.ToString(),
				FString(TEXT("There is no Pokémon available to switch.")));
			Test.TestFalse(
				TEXT("Run is unavailable in the Trainer battle"),
				DisplayState.Command.Run.bAvailable);
			Test.TestEqual(
				TEXT("The unavailable Run command has its typed reason"),
				DisplayState.Command.Run.UnavailableReason.ToString(),
				FString(TEXT("You cannot run from this battle.")));
			Test.TestTrue(
				TEXT("The production HUD enables local command input"),
				HUD.IsCommandInputEnabled()
					&& HUD.IsCommandMenuActive()
					&& PlayerController.IsBattleCommandInputReady());

			UBattleCommandWidget* CommandUI = Cast<UBattleCommandWidget>(
				HUD.GetWidgetFromName(TEXT("CommandUI")));
			UBattlePokemonHealthPanel* PlayerHealthPanel =
				Cast<UBattlePokemonHealthPanel>(
					HUD.GetWidgetFromName(TEXT("HealthPanel_Player")));
			UBattlePokemonHealthPanel* OpponentHealthPanel =
				Cast<UBattlePokemonHealthPanel>(
					HUD.GetWidgetFromName(TEXT("HealthPanel_Opponent")));
			Test.TestNotNull(
				TEXT("The real WBP_BattleHUD generated the command child"),
				CommandUI);
			Test.TestTrue(
				TEXT("The real player health panel completed NativeConstruct"),
				PlayerHealthPanel && PlayerHealthPanel->IsStructurallyReady());
			Test.TestTrue(
				TEXT("The real opponent health panel completed NativeConstruct"),
				OpponentHealthPanel && OpponentHealthPanel->IsStructurallyReady());
			if (!CommandUI)
			{
				return;
			}

			Test.TestTrue(
				TEXT("NativeConstruct bound command focus to the HUD facade"),
				CommandUI->OnCommandFocusChanged.Contains(
					&HUD,
					FName(TEXT("HandleCommandFocusChanged"))));
			Test.TestTrue(
				TEXT("NativeConstruct bound Battle text to the HUD facade"),
				CommandUI->OnBattleTextChanged.Contains(
					&HUD,
					FName(TEXT("HandleCommandBattleTextChanged"))));
			Test.TestTrue(
				TEXT("NativeConstruct bound command presses to the HUD facade"),
				CommandUI->OnCommandPressed.Contains(
					&HUD,
					FName(TEXT("HandleCommandPressed"))));
			Test.TestTrue(
				TEXT("NativeConstruct bound command requests to the HUD facade"),
				CommandUI->OnCommandRequested.Contains(
					&HUD,
					FName(TEXT("HandleCommandRequested"))));

			EBattleUICommand FocusedCommand = EBattleUICommand::Run;
			FText BattleText;
			Test.TestTrue(
				TEXT("The facade reads the initial Fight focus"),
				HUD.TryGetFocusedCommand(FocusedCommand));
			Test.TestEqual(
				TEXT("The production command menu initially focuses Fight"),
				FocusedCommand,
				EBattleUICommand::Fight);
			Test.TestTrue(
				TEXT("The facade reads the initial Battle text"),
				HUD.TryGetCurrentBattleText(BattleText));
			Test.TestEqual(
				TEXT("The initial Battle text is the normal prompt"),
				BattleText.ToString(),
				FString(TEXT("Choose a command.")));

			int32 FocusSignalCount = 0;
			int32 TextSignalCount = 0;
			int32 PressSignalCount = 0;
			int32 RequestSignalCount = 0;
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
			const FDelegateHandle PressHandle =
				CommandUI->GetCommandPressedNativeDelegate().AddLambda(
					[&PressSignalCount](const EBattleUICommand)
					{
						++PressSignalCount;
					});
			const FDelegateHandle RequestHandle =
				CommandUI->GetCommandRequestedNativeDelegate().AddLambda(
					[&RequestSignalCount](const EBattleUICommand)
					{
						++RequestSignalCount;
					});

			const bool bNavigatedToBag = HUD.NavigateCommandMenu(FVector2D(1.0, 0.0));
			const bool bReadBagFocus = HUD.TryGetFocusedCommand(FocusedCommand);
			const bool bReadBagText = HUD.TryGetCurrentBattleText(BattleText);
			Test.TestTrue(
				TEXT("The HUD facade forwards right navigation"),
				bNavigatedToBag && bReadBagFocus && bReadBagText);
			Test.TestEqual(
				TEXT("Facade navigation focuses Bag"),
				FocusedCommand,
				EBattleUICommand::Bag);
			Test.TestEqual(
				TEXT("Facade navigation forwards the unavailable Bag text"),
				BattleText.ToString(),
				FString(TEXT("There are no usable items.")));

			const bool bNavigatedToFight = HUD.NavigateCommandMenu(FVector2D(-1.0, 0.0));
			const bool bReadFightFocus = HUD.TryGetFocusedCommand(FocusedCommand);
			const bool bReadFightText = HUD.TryGetCurrentBattleText(BattleText);
			Test.TestTrue(
				TEXT("The HUD facade forwards left navigation"),
				bNavigatedToFight && bReadFightFocus && bReadFightText);
			Test.TestEqual(
				TEXT("Facade navigation returns focus to Fight"),
				FocusedCommand,
				EBattleUICommand::Fight);
			Test.TestEqual(
				TEXT("Facade navigation restores the normal prompt"),
				BattleText.ToString(),
				FString(TEXT("Choose a command.")));

			const bool bConfirmedFight = HUD.ConfirmCommandMenu();
			CommandUI->GetCommandFocusChangedNativeDelegate().Remove(FocusHandle);
			CommandUI->GetBattleTextChangedNativeDelegate().Remove(TextHandle);
			CommandUI->GetCommandPressedNativeDelegate().Remove(PressHandle);
			CommandUI->GetCommandRequestedNativeDelegate().Remove(RequestHandle);

			Test.TestTrue(
				TEXT("The HUD facade forwards an available Fight confirmation"),
				bConfirmedFight);
			Test.TestEqual(
				TEXT("Two real focus changes were emitted through command navigation"),
				FocusSignalCount,
				2);
			Test.TestEqual(
				TEXT("Two matching Battle-text changes were emitted"),
				TextSignalCount,
				2);
			Test.TestEqual(
				TEXT("Available confirmation emits one visual press signal"),
				PressSignalCount,
				1);
			Test.TestEqual(
				TEXT("Available confirmation emits one local request"),
				RequestSignalCount,
				1);
			Test.TestTrue(
				TEXT("Facade exercise does not submit or disable the top-level command request"),
				HUD.IsCommandInputEnabled() && HUD.IsCommandMenuActive());
		}

		FAutomationTestBase& Test;
		double FirstUpdateTimeSeconds = 0.0;
		FString LastWaitingFor;
	};

	bool AreHealthStatesEquivalent(
		const FBattleHUDHealthDisplayState& Left,
		const FBattleHUDHealthDisplayState& Right)
	{
		return Left.PokemonName.ToString() == Right.PokemonName.ToString()
			&& Left.CurrentHP == Right.CurrentHP
			&& Left.MaxHP == Right.MaxHP;
	}

	bool AreCommandAvailabilitiesEquivalent(
		const FBattleCommandAvailability& Left,
		const FBattleCommandAvailability& Right)
	{
		return Left.bAvailable == Right.bAvailable
			&& Left.UnavailableReason.ToString()
				== Right.UnavailableReason.ToString();
	}

	bool AreHUDStatesEquivalent(
		const FBattleHUDDisplayState& Left,
		const FBattleHUDDisplayState& Right)
	{
		return AreHealthStatesEquivalent(Left.Player, Right.Player)
			&& AreHealthStatesEquivalent(Left.Opponent, Right.Opponent)
			&& Left.Command.NormalPrompt.ToString()
				== Right.Command.NormalPrompt.ToString()
			&& AreCommandAvailabilitiesEquivalent(
				Left.Command.Fight, Right.Command.Fight)
			&& AreCommandAvailabilitiesEquivalent(
				Left.Command.Bag, Right.Command.Bag)
			&& AreCommandAvailabilitiesEquivalent(
				Left.Command.Pokemon, Right.Command.Pokemon)
			&& AreCommandAvailabilitiesEquivalent(
				Left.Command.Run, Right.Command.Run);
	}

	template <typename DynamicDelegateType>
	int32 CountBindingsForObject(
		const DynamicDelegateType& Delegate,
		const UObject& BoundObject)
	{
		int32 Count = 0;
		for (const UObject* Object : Delegate.GetAllObjects())
		{
			if (Object == &BoundObject)
			{
				++Count;
			}
		}
		return Count;
	}

	struct FBattleHUDLifecycleObservations
	{
		int32 NativeConstructCount = 0;
		int32 NativeDestructCount = 0;
	};

	class FObserveProductionBattleHUDReconstructionCommand final
		: public IAutomationLatentCommand
	{
	public:
		explicit FObserveProductionBattleHUDReconstructionCommand(
			FAutomationTestBase& InTest)
			: Test(InTest)
			, Observations(MakeShared<FBattleHUDLifecycleObservations>())
		{
		}

		virtual bool Update() override
		{
			if (PhaseStartTimeSeconds <= 0.0)
			{
				PhaseStartTimeSeconds = FPlatformTime::Seconds();
			}

			UWorld* World = AutomationCommon::GetAnyGameWorld();
			if (!IsValid(World) || !World->HasBegunPlay())
			{
				return WaitOrFail(TEXT("the reconstruction test PIE world"));
			}

			return bReattachmentStarted
				? ObserveRedelivery(*World)
				: BeginReattachment(*World);
		}

	private:
		bool BeginReattachment(UWorld& World)
		{
			ABattlePlayerController* PlayerController =
				Cast<ABattlePlayerController>(World.GetFirstPlayerController());
			if (!IsValid(PlayerController))
			{
				return WaitOrFail(TEXT("the production Battle controller"));
			}

			UBattleHUDWidget* HUD = PlayerController->GetBattleHUDWidget();
			FBattleHUDDisplayState DisplayState;
			if (!IsValid(HUD)
				|| !PlayerController->IsBattleHUDAvailable()
				|| !PlayerController->IsBattleHUDVisible()
				|| !PlayerController->IsBattleCommandInputReady()
				|| !HUD->TryGetLastValidatedDisplayState(DisplayState)
				|| !DisplayState.IsValid())
			{
				return WaitOrFail(TEXT("the initial complete Battle HUD presentation"));
			}

			UBattleCommandWidget* CommandUI = Cast<UBattleCommandWidget>(
				HUD->GetWidgetFromName(TEXT("CommandUI")));
			if (!IsValid(CommandUI))
			{
				Test.AddError(TEXT("The production HUD has no real CommandUI child."));
				return true;
			}

			PlayerControllerUnderTest = PlayerController;
			HUDUnderTest = HUD;
			CommandUIUnderTest = CommandUI;
			OriginalDisplayState = DisplayState;
			OriginalPresentationGeneration =
				PlayerController->GetBattleHUDPresentationGeneration();
			OriginalConstructionSerial = HUD->GetNativeConstructionSerial();

			const TWeakObjectPtr<UBattleHUDWidget> WeakHUD(HUD);
			ConstructedHandle = HUD->GetConstructedNativeDelegate().AddLambda(
				[WeakHUD, Observations = Observations](UBattleHUDWidget& ConstructedHUD)
				{
					if (WeakHUD.Get() == &ConstructedHUD)
					{
						++Observations->NativeConstructCount;
					}
				});
			DestructHandle = HUD->OnNativeDestruct.AddLambda(
				[WeakHUD, Observations = Observations](UUserWidget* DestructedHUD)
				{
					if (WeakHUD.Get() == DestructedHUD)
					{
						++Observations->NativeDestructCount;
					}
				});

			Test.TestEqual(
				TEXT("The initial focus facade has exactly one HUD binding"),
				CountBindingsForObject(CommandUI->OnCommandFocusChanged, *HUD),
				1);
			Test.TestEqual(
				TEXT("The initial text facade has exactly one HUD binding"),
				CountBindingsForObject(CommandUI->OnBattleTextChanged, *HUD),
				1);
			Test.TestEqual(
				TEXT("The initial press facade has exactly one HUD binding"),
				CountBindingsForObject(CommandUI->OnCommandPressed, *HUD),
				1);
			Test.TestEqual(
				TEXT("The initial request facade has exactly one HUD binding"),
				CountBindingsForObject(CommandUI->OnCommandRequested, *HUD),
				1);

			HUD->RemoveFromParent();
			Test.TestEqual(
				TEXT("Removing the real HUD executes NativeDestruct once"),
				Observations->NativeDestructCount,
				1);
			Test.TestEqual(
				TEXT("NativeDestruct does not advance the construction serial"),
				HUD->GetNativeConstructionSerial(),
				OriginalConstructionSerial);
			Test.TestTrue(
				TEXT("NativeDestruct leaves the same HUD detached and structurally unavailable"),
				PlayerController->GetBattleHUDWidget() == HUD
					&& !HUD->IsInViewport()
					&& !HUD->IsStructurallyReady());
			Test.TestTrue(
				TEXT("NativeDestruct collapses the root and disables every input path"),
				HUD->GetVisibility() == ESlateVisibility::Collapsed
					&& !HUD->IsPresentationVisible()
					&& !HUD->IsCommandInputEnabled()
					&& !PlayerController->IsBattleHUDVisible()
					&& !PlayerController->IsBattleCommandInputReady());
			Test.TestEqual(
				TEXT("NativeDestruct unbinds the focus facade"),
				CountBindingsForObject(CommandUI->OnCommandFocusChanged, *HUD),
				0);
			Test.TestEqual(
				TEXT("NativeDestruct leaves the accepted presentation generation unchanged"),
				PlayerController->GetBattleHUDPresentationGeneration(),
				OriginalPresentationGeneration);

			const bool bAdded = HUD->AddToPlayerScreen();
			Test.TestTrue(
				TEXT("The same real WBP_BattleHUD instance was re-added through UMG"),
				bAdded
					&& PlayerController->GetBattleHUDWidget() == HUD
					&& HUD->IsInViewport());
			Test.TestEqual(
				TEXT("Re-adding the same instance executes one new NativeConstruct pass"),
				Observations->NativeConstructCount,
				1);
			Test.TestEqual(
				TEXT("The repeated NativeConstruct advances its construction serial once"),
				HUD->GetNativeConstructionSerial(),
				OriginalConstructionSerial + 1);
			Test.TestTrue(
				TEXT("The reconstructed root remains fail-closed until redelivery"),
				HUD->IsStructurallyReady()
					&& HUD->GetVisibility() == ESlateVisibility::Collapsed
					&& !HUD->IsPresentationVisible()
					&& !HUD->IsCommandInputEnabled()
					&& !PlayerController->IsBattleCommandInputReady());
			Test.TestEqual(
				TEXT("Reconstruction does not advance generation before controller acceptance"),
				PlayerController->GetBattleHUDPresentationGeneration(),
				OriginalPresentationGeneration);

			bReattachmentStarted = true;
			PhaseStartTimeSeconds = FPlatformTime::Seconds();
			return false;
		}

		bool ObserveRedelivery(UWorld& World)
		{
			ABattlePlayerController* PlayerController = PlayerControllerUnderTest.Get();
			UBattleHUDWidget* HUD = HUDUnderTest.Get();
			UBattleCommandWidget* CommandUI = CommandUIUnderTest.Get();
			if (!IsValid(PlayerController) || !IsValid(HUD) || !IsValid(CommandUI)
				|| PlayerController->GetWorld() != &World)
			{
				CleanupObservers();
				Test.AddError(TEXT("A same-instance lifecycle object became invalid."));
				return true;
			}

			FBattleHUDDisplayState RedeliveredState;
			const bool bRedelivered =
				PlayerController->GetBattleHUDWidget() == HUD
				&& PlayerController->GetBattleHUDPresentationGeneration()
					> OriginalPresentationGeneration
				&& PlayerController->IsBattleHUDAvailable()
				&& PlayerController->IsBattleHUDVisible()
				&& PlayerController->IsBattleCommandInputReady()
				&& HUD->TryGetLastValidatedDisplayState(RedeliveredState)
				&& RedeliveredState.IsValid();
			if (!bRedelivered)
			{
				if (HUD->GetVisibility() != ESlateVisibility::Collapsed
					&& !HUD->IsPresentationVisible())
				{
					CleanupObservers();
					Test.AddError(TEXT("The reconstructed HUD root escaped fail-closed visibility before redelivery."));
					return true;
				}
				return WaitOrFail(TEXT("unchanged-request redelivery to the reconstructed HUD"));
			}

			Test.TestEqual(
				TEXT("Controller acceptance advances the presentation generation exactly once"),
				PlayerController->GetBattleHUDPresentationGeneration(),
				OriginalPresentationGeneration + 1);
			Test.TestTrue(
				TEXT("The unchanged authoritative request reappears on the same HUD instance"),
				AreHUDStatesEquivalent(OriginalDisplayState, RedeliveredState)
					&& HUD->GetVisibility() == ESlateVisibility::Visible
					&& HUD->IsPresentationVisible()
					&& HUD->IsCommandInputEnabled());
			Test.TestEqual(
				TEXT("The reconstructed focus facade has exactly one HUD binding"),
				CountBindingsForObject(CommandUI->OnCommandFocusChanged, *HUD),
				1);
			Test.TestEqual(
				TEXT("The reconstructed text facade has exactly one HUD binding"),
				CountBindingsForObject(CommandUI->OnBattleTextChanged, *HUD),
				1);
			Test.TestEqual(
				TEXT("The reconstructed press facade has exactly one HUD binding"),
				CountBindingsForObject(CommandUI->OnCommandPressed, *HUD),
				1);
			Test.TestEqual(
				TEXT("The reconstructed request facade has exactly one HUD binding"),
				CountBindingsForObject(CommandUI->OnCommandRequested, *HUD),
				1);
			Test.TestEqual(
				TEXT("The lifecycle emitted exactly one repeated NativeConstruct"),
				Observations->NativeConstructCount,
				1);
			Test.TestEqual(
				TEXT("The lifecycle emitted exactly one NativeDestruct"),
				Observations->NativeDestructCount,
				1);
			CleanupObservers();
			return true;
		}

		bool WaitOrFail(const TCHAR* WaitingFor)
		{
			if (FPlatformTime::Seconds() - PhaseStartTimeSeconds
				< ProductionLifecycleTimeoutSeconds)
			{
				return false;
			}

			CleanupObservers();
			Test.AddError(FString::Printf(
				TEXT("Timed out after %.0f seconds waiting for %s."),
				ProductionLifecycleTimeoutSeconds,
				WaitingFor));
			return true;
		}

		void CleanupObservers()
		{
			if (UBattleHUDWidget* HUD = HUDUnderTest.Get())
			{
				if (ConstructedHandle.IsValid())
				{
					HUD->GetConstructedNativeDelegate().Remove(ConstructedHandle);
					ConstructedHandle.Reset();
				}
				if (DestructHandle.IsValid())
				{
					HUD->OnNativeDestruct.Remove(DestructHandle);
					DestructHandle.Reset();
				}
			}
		}

		FAutomationTestBase& Test;
		TSharedRef<FBattleHUDLifecycleObservations> Observations;
		TWeakObjectPtr<ABattlePlayerController> PlayerControllerUnderTest;
		TWeakObjectPtr<UBattleHUDWidget> HUDUnderTest;
		TWeakObjectPtr<UBattleCommandWidget> CommandUIUnderTest;
		FBattleHUDDisplayState OriginalDisplayState;
		FDelegateHandle ConstructedHandle;
		FDelegateHandle DestructHandle;
		double PhaseStartTimeSeconds = 0.0;
		uint64 OriginalPresentationGeneration = 0;
		uint64 OriginalConstructionSerial = 0;
		bool bReattachmentStarted = false;
	};

	class FObserveProductionBattleHUDFailClosedCommand final
		: public IAutomationLatentCommand
	{
	public:
		explicit FObserveProductionBattleHUDFailClosedCommand(
			FAutomationTestBase& InTest)
			: Test(InTest)
		{
		}

		virtual bool Update() override
		{
			if (PhaseStartTimeSeconds <= 0.0)
			{
				PhaseStartTimeSeconds = FPlatformTime::Seconds();
			}

			UWorld* World = AutomationCommon::GetAnyGameWorld();
			if (!IsValid(World) || !World->HasBegunPlay())
			{
				return WaitOrFail(TEXT("the fail-closed test PIE world"));
			}

			switch (Phase)
			{
			case EPhase::InitialPresentation:
				return ExerciseStructuralFailure(*World);
			case EPhase::StructuralRestore:
				return ExerciseInvalidSoftClass(*World);
			case EPhase::InvalidClassRestore:
				return ObserveFinalRestore(*World);
			default:
				Test.AddError(TEXT("The fail-closed lifecycle entered an invalid phase."));
				return true;
			}
		}

	private:
		enum class EPhase : uint8
		{
			InitialPresentation,
			StructuralRestore,
			InvalidClassRestore
		};

		bool ExerciseStructuralFailure(UWorld& World)
		{
			ABattlePlayerController* PlayerController =
				Cast<ABattlePlayerController>(World.GetFirstPlayerController());
			if (!IsValid(PlayerController))
			{
				return WaitOrFail(TEXT("the production Battle controller"));
			}

			UBattleHUDWidget* InitialHUD = PlayerController->GetBattleHUDWidget();
			FBattleHUDDisplayState InitialState;
			if (!IsCompletePresentation(*PlayerController, InitialHUD, InitialState))
			{
				return WaitOrFail(TEXT("the initial complete Battle HUD presentation"));
			}

			UBattleHUDWidget* InvalidCandidate = CreateWidget<UBattleHUDWidget>(
				PlayerController,
				InitialHUD->GetClass());
			if (!IsValid(InvalidCandidate))
			{
				Test.AddError(TEXT("CreateWidget could not create the real WBP structural-failure candidate."));
				return true;
			}

			PlayerControllerUnderTest = PlayerController;
			OriginalHUD = InitialHUD;
			OriginalDisplayState = InitialState;
			OriginalPresentationGeneration =
				PlayerController->GetBattleHUDPresentationGeneration();

			Test.AddExpectedError(
				TEXT("The battle HUD failed construction, viewport attachment, or required binding validation."),
				EAutomationExpectedErrorFlags::Contains,
				1);
			const bool bAttached =
				FBattleHUDProductionLifecycleTestFixture::
					TryAttachStructurallyInvalidHUD(
						*PlayerController,
						*InvalidCandidate);
			FBattleHUDDisplayState CandidateState;
			Test.TestFalse(
				TEXT("The controller rejects a real WBP candidate with a missing required binding"),
				bAttached);
			Test.TestTrue(
				TEXT("Structural failure leaves no owned, visible, or interactive Battle HUD"),
				PlayerController->GetBattleHUDWidget() == nullptr
					&& !PlayerController->IsBattleHUDAvailable()
					&& !PlayerController->IsBattleHUDVisible()
					&& !PlayerController->IsBattleCommandInputReady());
			Test.TestTrue(
				TEXT("The rejected WBP candidate is collapsed, detached, and has no cached success"),
				!InvalidCandidate->IsInViewport()
					&& InvalidCandidate->GetVisibility() == ESlateVisibility::Collapsed
					&& !InvalidCandidate->IsStructurallyReady()
					&& !InvalidCandidate->IsPresentationVisible()
					&& !InvalidCandidate->IsCommandInputEnabled()
					&& !InvalidCandidate->TryGetLastValidatedDisplayState(CandidateState));
			Test.TestTrue(
				TEXT("The discarded production HUD also remains detached and non-interactive"),
				!InitialHUD->IsInViewport()
					&& InitialHUD->GetVisibility() == ESlateVisibility::Collapsed
					&& !InitialHUD->IsPresentationVisible()
					&& !InitialHUD->IsCommandInputEnabled());
			Test.TestEqual(
				TEXT("Structural attachment failure does not advance presentation generation"),
				PlayerController->GetBattleHUDPresentationGeneration(),
				OriginalPresentationGeneration);

			if (!FBattleHUDProductionLifecycleTestFixture::
				TryCreateConfiguredBattleHUD(*PlayerController))
			{
				Test.AddError(TEXT("The configured production HUD could not be restored after structural rejection."));
				return true;
			}

			Phase = EPhase::StructuralRestore;
			PhaseStartTimeSeconds = FPlatformTime::Seconds();
			return false;
		}

		bool ExerciseInvalidSoftClass(UWorld& World)
		{
			ABattlePlayerController* PlayerController = PlayerControllerUnderTest.Get();
			if (!IsValid(PlayerController) || PlayerController->GetWorld() != &World)
			{
				Test.AddError(TEXT("The fail-closed production controller became invalid."));
				return true;
			}

			UBattleHUDWidget* RestoredHUD = PlayerController->GetBattleHUDWidget();
			FBattleHUDDisplayState RestoredState;
			if (!IsCompletePresentation(*PlayerController, RestoredHUD, RestoredState)
				|| PlayerController->GetBattleHUDPresentationGeneration()
					<= OriginalPresentationGeneration)
			{
				return WaitOrFail(TEXT("normal redelivery after structural rejection"));
			}

			Test.TestTrue(
				TEXT("The configured WBP restores the unchanged request after structural rejection"),
				RestoredHUD != OriginalHUD.Get()
					&& RestoredHUD->GetClass()->GetPathName()
						== FString(BattleHUDClassPath)
					&& AreHUDStatesEquivalent(OriginalDisplayState, RestoredState));
			Test.TestEqual(
				TEXT("Only the successful structural-failure restore advances generation"),
				PlayerController->GetBattleHUDPresentationGeneration(),
				OriginalPresentationGeneration + 1);

			HUDDiscardedByInvalidClass = RestoredHUD;
			GenerationBeforeInvalidClass =
				PlayerController->GetBattleHUDPresentationGeneration();
			Test.AddExpectedError(
				TEXT("The battle HUD Widget Blueprint could not be loaded."),
				EAutomationExpectedErrorFlags::Contains,
				1);
			const TSoftClassPtr<UBattleHUDWidget> MissingHUDClass;
			const bool bCreated =
				FBattleHUDProductionLifecycleTestFixture::
					TryCreateBattleHUDWithTemporaryClass(
						*PlayerController,
						MissingHUDClass);
			Test.TestFalse(
				TEXT("The real controller rejects an invalid Battle HUD soft class"),
				bCreated);
			Test.TestTrue(
				TEXT("Invalid soft-class initialization leaves no visible or interactive HUD"),
				PlayerController->GetBattleHUDWidget() == nullptr
					&& !PlayerController->IsBattleHUDAvailable()
					&& !PlayerController->IsBattleHUDVisible()
					&& !PlayerController->IsBattleCommandInputReady()
					&& !RestoredHUD->IsInViewport()
					&& RestoredHUD->GetVisibility() == ESlateVisibility::Collapsed
					&& !RestoredHUD->IsPresentationVisible()
					&& !RestoredHUD->IsCommandInputEnabled());
			Test.TestEqual(
				TEXT("Invalid soft-class initialization does not cache a successful generation"),
				PlayerController->GetBattleHUDPresentationGeneration(),
				GenerationBeforeInvalidClass);

			if (!FBattleHUDProductionLifecycleTestFixture::
				TryCreateConfiguredBattleHUD(*PlayerController))
			{
				Test.AddError(TEXT("The configured production HUD could not be restored after invalid soft-class rejection."));
				return true;
			}

			Phase = EPhase::InvalidClassRestore;
			PhaseStartTimeSeconds = FPlatformTime::Seconds();
			return false;
		}

		bool ObserveFinalRestore(UWorld& World)
		{
			ABattlePlayerController* PlayerController = PlayerControllerUnderTest.Get();
			if (!IsValid(PlayerController) || PlayerController->GetWorld() != &World)
			{
				Test.AddError(TEXT("The final-restore production controller became invalid."));
				return true;
			}

			UBattleHUDWidget* FinalHUD = PlayerController->GetBattleHUDWidget();
			FBattleHUDDisplayState FinalState;
			if (!IsCompletePresentation(*PlayerController, FinalHUD, FinalState)
				|| PlayerController->GetBattleHUDPresentationGeneration()
					<= GenerationBeforeInvalidClass)
			{
				return WaitOrFail(TEXT("normal redelivery after invalid soft-class rejection"));
			}

			Test.TestTrue(
				TEXT("The saved production class restores a fresh real WBP HUD"),
				FinalHUD != HUDDiscardedByInvalidClass.Get()
					&& FinalHUD->GetClass()->GetPathName()
						== FString(BattleHUDClassPath));
			Test.TestTrue(
				TEXT("The unchanged authoritative request is redelivered after invalid initialization"),
				AreHUDStatesEquivalent(OriginalDisplayState, FinalState)
					&& FinalHUD->IsPresentationVisible()
					&& FinalHUD->IsCommandInputEnabled());
			Test.TestEqual(
				TEXT("Only the successful invalid-class restore advances generation"),
				PlayerController->GetBattleHUDPresentationGeneration(),
				GenerationBeforeInvalidClass + 1);
			return true;
		}

		static bool IsCompletePresentation(
			ABattlePlayerController& PlayerController,
			UBattleHUDWidget* HUD,
			FBattleHUDDisplayState& OutState)
		{
			return IsValid(HUD)
				&& PlayerController.IsBattleHUDAvailable()
				&& PlayerController.IsBattleHUDVisible()
				&& PlayerController.IsBattleCommandInputReady()
				&& HUD->TryGetLastValidatedDisplayState(OutState)
				&& OutState.IsValid();
		}

		bool WaitOrFail(const TCHAR* WaitingFor)
		{
			if (FPlatformTime::Seconds() - PhaseStartTimeSeconds
				< ProductionLifecycleTimeoutSeconds)
			{
				return false;
			}

			Test.AddError(FString::Printf(
				TEXT("Timed out after %.0f seconds waiting for %s."),
				ProductionLifecycleTimeoutSeconds,
				WaitingFor));
			return true;
		}

		FAutomationTestBase& Test;
		TWeakObjectPtr<ABattlePlayerController> PlayerControllerUnderTest;
		TWeakObjectPtr<UBattleHUDWidget> OriginalHUD;
		TWeakObjectPtr<UBattleHUDWidget> HUDDiscardedByInvalidClass;
		FBattleHUDDisplayState OriginalDisplayState;
		double PhaseStartTimeSeconds = 0.0;
		uint64 OriginalPresentationGeneration = 0;
		uint64 GenerationBeforeInvalidClass = 0;
		EPhase Phase = EPhase::InitialPresentation;
	};

	class FObserveProductionBattleHUDReplacementCommand final
		: public IAutomationLatentCommand
	{
	public:
		explicit FObserveProductionBattleHUDReplacementCommand(
			FAutomationTestBase& InTest)
			: Test(InTest)
		{
		}

		virtual bool Update() override
		{
			if (PhaseStartTimeSeconds <= 0.0)
			{
				PhaseStartTimeSeconds = FPlatformTime::Seconds();
			}

			UWorld* World = AutomationCommon::GetAnyGameWorld();
			if (!IsValid(World) || !World->HasBegunPlay())
			{
				return WaitOrFail(TEXT("the replacement test PIE world"));
			}

			UGameInstance* GameInstance = World->GetGameInstance();
			if (!IsValid(GameInstance))
			{
				return WaitOrFail(TEXT("the replacement test GameInstance"));
			}

			return bReplacementPlayerCreated
				? ObserveReplacement(*World, *GameInstance)
				: CreateReplacement(*World, *GameInstance);
		}

	private:
		bool CreateReplacement(UWorld& World, UGameInstance& GameInstance)
		{
			ABattleGameMode* GameMode = Cast<ABattleGameMode>(World.GetAuthGameMode());
			ABattlePlayerController* InitialController =
				Cast<ABattlePlayerController>(World.GetFirstPlayerController());
			if (!IsValid(GameMode) || !IsValid(InitialController))
			{
				return WaitOrFail(
					TEXT("the initial production GameMode and Battle controller"));
			}

			UBattleHUDWidget* InitialHUD = InitialController->GetBattleHUDWidget();
			FBattleHUDDisplayState InitialState;
			if (!IsValid(InitialHUD)
				|| !InitialController->IsBattleHUDAvailable()
				|| !InitialController->IsBattleHUDVisible()
				|| !InitialController->IsBattleCommandInputReady()
				|| !InitialHUD->TryGetLastValidatedDisplayState(InitialState)
				|| !InitialState.IsValid())
			{
				return WaitOrFail(TEXT("the initial complete Battle HUD presentation"));
			}

			InitialLocalPlayerCount = GameInstance.GetNumLocalPlayers();
			if (InitialLocalPlayerCount < 1)
			{
				Test.AddError(TEXT("The PIE GameInstance has no initial local player."));
				return true;
			}

			OriginalController = InitialController;
			OriginalHUD = InitialHUD;
			OriginalDisplayState = InitialState;

			FString CreateError;
			ULocalPlayer* NewLocalPlayer = GameInstance.CreateLocalPlayer(
				-1,
				CreateError,
				true);
			if (!IsValid(NewLocalPlayer))
			{
				Test.AddError(FString::Printf(
					TEXT("CreateLocalPlayer could not create a replacement: %s"),
					CreateError.IsEmpty() ? TEXT("no engine diagnostic") : *CreateError));
				return true;
			}

			ReplacementLocalPlayer = NewLocalPlayer;
			bReplacementPlayerCreated = true;
			PhaseStartTimeSeconds = FPlatformTime::Seconds();
			return false;
		}

		bool ObserveReplacement(UWorld& World, UGameInstance& GameInstance)
		{
			ULocalPlayer* NewLocalPlayer = ReplacementLocalPlayer.Get();
			ABattlePlayerController* InitialController = OriginalController.Get();
			UBattleHUDWidget* InitialHUD = OriginalHUD.Get();
			if (!IsValid(NewLocalPlayer)
				|| !IsValid(InitialController)
				|| !IsValid(InitialHUD))
			{
				return FailAndCleanup(
					GameInstance,
					TEXT("A production replacement lifecycle object became invalid."));
			}

			APlayerController* CreatedController = NewLocalPlayer->GetPlayerController(&World);
			if (IsValid(CreatedController)
				&& !CreatedController->IsA<ABattlePlayerController>())
			{
				return FailAndCleanup(
					GameInstance,
					TEXT("CreateLocalPlayer spawned a non-Battle PlayerController."));
			}

			ABattlePlayerController* ReplacementController =
				Cast<ABattlePlayerController>(CreatedController);
			if (!IsValid(ReplacementController))
			{
				return WaitOrFail(TEXT("the replacement Battle PlayerController"));
			}

			UBattleHUDWidget* ReplacementHUD =
				ReplacementController->GetBattleHUDWidget();
			FBattleHUDDisplayState ReplacementState;
			const bool bReplacementReady = IsValid(ReplacementHUD)
				&& ReplacementController->IsBattleHUDAvailable()
				&& ReplacementController->IsBattleHUDVisible()
				&& ReplacementController->IsBattleCommandInputReady()
				&& ReplacementHUD->TryGetLastValidatedDisplayState(ReplacementState)
				&& ReplacementState.IsValid();
			if (!bReplacementReady)
			{
				if (IsValid(ReplacementHUD)
					&& ReplacementHUD->GetVisibility() != ESlateVisibility::Collapsed
					&& !ReplacementHUD->TryGetLastValidatedDisplayState(ReplacementState))
				{
					return FailAndCleanup(
						GameInstance,
						TEXT("The replacement HUD became visible before full state redelivery."));
				}
				return WaitOrFail(TEXT("unchanged-request redelivery to the replacement HUD"));
			}

			Test.TestTrue(
				TEXT("CreateLocalPlayer used a distinct production Battle controller"),
				ReplacementController != InitialController
					&& ReplacementController->GetLocalPlayer() == NewLocalPlayer);
			Test.TestEqual(
				TEXT("The normal engine path registered one additional local player"),
				GameInstance.GetNumLocalPlayers(),
				InitialLocalPlayerCount + 1);
			Test.TestTrue(
				TEXT("The replacement owns a distinct real WBP_BattleHUD instance"),
				ReplacementHUD != InitialHUD
					&& ReplacementHUD->GetClass()->GetPathName()
						== FString(BattleHUDClassPath)
					&& !ReplacementHUD->HasAnyFlags(RF_ClassDefaultObject));
			Test.TestTrue(
				TEXT("The replacement HUD completed construction and viewport attachment"),
				ReplacementHUD->IsConstructed()
					&& ReplacementHUD->IsInViewport()
					&& ReplacementHUD->IsStructurallyReady());
			Test.TestTrue(
				TEXT("The replacement HUD has a valid presentation generation"),
				ReplacementController->GetBattleHUDPresentationGeneration() > 0);
			Test.TestTrue(
				TEXT("The unchanged request was fully redelivered to the replacement HUD"),
				AreHUDStatesEquivalent(OriginalDisplayState, ReplacementState)
					&& ReplacementHUD->IsPresentationVisible()
					&& ReplacementHUD->IsCommandInputEnabled());

			Test.TestTrue(
				TEXT("Detaching the old controller preserves its last visible presentation"),
				InitialController->IsBattleHUDVisible());
			Test.TestFalse(
				TEXT("The old controller no longer accepts Battle command input"),
				InitialController->IsBattleCommandInputReady());
			Test.TestFalse(
				TEXT("The old HUD rejects navigation after controller replacement"),
				InitialHUD->NavigateCommandMenu(FVector2D(1.0, 0.0)));
			Test.TestFalse(
				TEXT("The old HUD rejects confirmation after controller replacement"),
				InitialHUD->ConfirmCommandMenu());

			const bool bRemoved = GameInstance.RemoveLocalPlayer(NewLocalPlayer);
			Test.TestTrue(
				TEXT("The replacement local player was removed through the engine lifecycle"),
				bRemoved);
			if (bRemoved)
			{
				ReplacementLocalPlayer.Reset();
				Test.TestEqual(
					TEXT("Replacement cleanup restored the original local-player count"),
					GameInstance.GetNumLocalPlayers(),
					InitialLocalPlayerCount);
			}
			return true;
		}

		bool WaitOrFail(const TCHAR* WaitingFor)
		{
			if (FPlatformTime::Seconds() - PhaseStartTimeSeconds
				< ProductionLifecycleTimeoutSeconds)
			{
				return false;
			}

			if (UGameInstance* GameInstance = GameInstanceForCleanup())
			{
				CleanupReplacement(*GameInstance);
			}
			Test.AddError(FString::Printf(
				TEXT("Timed out after %.0f seconds waiting for %s."),
				ProductionLifecycleTimeoutSeconds,
				WaitingFor));
			return true;
		}

		bool FailAndCleanup(UGameInstance& GameInstance, const TCHAR* Error)
		{
			CleanupReplacement(GameInstance);
			Test.AddError(Error);
			return true;
		}

		UGameInstance* GameInstanceForCleanup() const
		{
			if (UWorld* World = AutomationCommon::GetAnyGameWorld())
			{
				return World->GetGameInstance();
			}
			return nullptr;
		}

		void CleanupReplacement(UGameInstance& GameInstance)
		{
			if (ULocalPlayer* LocalPlayer = ReplacementLocalPlayer.Get())
			{
				GameInstance.RemoveLocalPlayer(LocalPlayer);
				ReplacementLocalPlayer.Reset();
			}
		}

		FAutomationTestBase& Test;
		TWeakObjectPtr<ABattlePlayerController> OriginalController;
		TWeakObjectPtr<UBattleHUDWidget> OriginalHUD;
		TWeakObjectPtr<ULocalPlayer> ReplacementLocalPlayer;
		FBattleHUDDisplayState OriginalDisplayState;
		double PhaseStartTimeSeconds = 0.0;
		int32 InitialLocalPlayerCount = 0;
		bool bReplacementPlayerCreated = false;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleHUDProductionLifecycleTest,
	"PokemonSolarus.UI.Battle.Production.FoundationMapLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleHUDProductionLifecycleTest::RunTest(const FString& Parameters)
{
	if (!AutomationOpenMap(BattleHUDProductionLifecycleTests::FoundationMapPath))
	{
		AddError(TEXT("AutomationOpenMap could not open /Game/Maps/FoundationMap."));
		return false;
	}

	ADD_LATENT_AUTOMATION_COMMAND(
		BattleHUDProductionLifecycleTests::FObserveProductionBattleHUDCommand(*this));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleHUDProductionReplacementTest,
	"PokemonSolarus.UI.Battle.Production.ReplacementControllerRedelivery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleHUDProductionReplacementTest::RunTest(const FString& Parameters)
{
	if (!AutomationOpenMap(BattleHUDProductionLifecycleTests::FoundationMapPath))
	{
		AddError(TEXT("AutomationOpenMap could not open /Game/Maps/FoundationMap."));
		return false;
	}

	ADD_LATENT_AUTOMATION_COMMAND(
		BattleHUDProductionLifecycleTests::FObserveProductionBattleHUDReplacementCommand(
			*this));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleHUDProductionReconstructionTest,
	"PokemonSolarus.UI.Battle.Production.SameWidgetReconstructionRedelivery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleHUDProductionReconstructionTest::RunTest(const FString& Parameters)
{
	if (!AutomationOpenMap(BattleHUDProductionLifecycleTests::FoundationMapPath))
	{
		AddError(TEXT("AutomationOpenMap could not open /Game/Maps/FoundationMap."));
		return false;
	}

	ADD_LATENT_AUTOMATION_COMMAND(
		BattleHUDProductionLifecycleTests::
			FObserveProductionBattleHUDReconstructionCommand(*this));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleHUDProductionInitializationFailClosedTest,
	"PokemonSolarus.UI.Battle.Production.InitializationFailClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleHUDProductionInitializationFailClosedTest::RunTest(
	const FString& Parameters)
{
	if (!AutomationOpenMap(BattleHUDProductionLifecycleTests::FoundationMapPath))
	{
		AddError(TEXT("AutomationOpenMap could not open /Game/Maps/FoundationMap."));
		return false;
	}

	ADD_LATENT_AUTOMATION_COMMAND(
		BattleHUDProductionLifecycleTests::
			FObserveProductionBattleHUDFailClosedCommand(*this));
	return true;
}

#endif
