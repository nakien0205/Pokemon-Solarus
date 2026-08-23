#include "Misc/AutomationTest.h"

#include "UI/BattleCommandWidget.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FBattleCommandDisplayState MakeAvailableCommandState()
	{
		FBattleCommandDisplayState State;
		State.NormalPrompt = FText::FromString(TEXT("Choose a command."));
		State.Fight.bAvailable = true;
		State.Bag.bAvailable = true;
		State.Pokemon.bAvailable = true;
		State.Run.bAvailable = true;
		return State;
	}

	EBattleUICommand GetFocusedCommand(
		FAutomationTestBase& Test,
		UBattleCommandWidget* Widget)
	{
		EBattleUICommand FocusedCommand = EBattleUICommand::Run;
		Test.TestTrue(
			TEXT("An active command menu exposes a focus"),
			Widget->TryGetFocusedCommand(FocusedCommand));
		return FocusedCommand;
	}

	FString GetCurrentBattleText(
		FAutomationTestBase& Test,
		UBattleCommandWidget* Widget)
	{
		FText BattleText;
		Test.TestTrue(
			TEXT("An active command menu exposes current Battle text"),
			Widget->TryGetCurrentBattleText(BattleText));
		return BattleText.ToString();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleCommandNavigationAdjacencyTest,
	"PokemonSolarus.UI.Battle.Command.Navigation.DefaultFocusAndAdjacency",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleCommandNavigationAdjacencyTest::RunTest(const FString& Parameters)
{
	UBattleCommandWidget* Widget = NewObject<UBattleCommandWidget>();
	TestNotNull(TEXT("The native command widget can be created without frontend assets"), Widget);
	if (!Widget)
	{
		return false;
	}

	const FBattleCommandDisplayState State = MakeAvailableCommandState();
	TestTrue(TEXT("Valid display state activates the menu"), Widget->ApplyDisplayState(State));
	TestEqual(TEXT("A new phase defaults to Fight"),
		GetFocusedCommand(*this, Widget), EBattleUICommand::Fight);

	TestTrue(TEXT("Fight Right moves to Bag"), Widget->Navigate(FVector2D(1.0, 0.0)));
	TestEqual(TEXT("Right from Fight focuses Bag"),
		GetFocusedCommand(*this, Widget), EBattleUICommand::Bag);
	TestTrue(TEXT("Bag Down moves to Run"), Widget->Navigate(FVector2D(0.0, -1.0)));
	TestEqual(TEXT("Down from Bag focuses Run"),
		GetFocusedCommand(*this, Widget), EBattleUICommand::Run);
	TestTrue(TEXT("Run Left moves to Pokemon"), Widget->Navigate(FVector2D(-1.0, 0.0)));
	TestEqual(TEXT("Left from Run focuses Pokemon"),
		GetFocusedCommand(*this, Widget), EBattleUICommand::Pokemon);
	TestTrue(TEXT("Pokemon Up moves to Fight"), Widget->Navigate(FVector2D(0.0, 1.0)));
	TestEqual(TEXT("Up from Pokemon focuses Fight"),
		GetFocusedCommand(*this, Widget), EBattleUICommand::Fight);

	TestTrue(TEXT("Fight Down moves to Pokemon"), Widget->Navigate(FVector2D(0.0, -1.0)));
	TestTrue(TEXT("Pokemon Right moves to Run"), Widget->Navigate(FVector2D(1.0, 0.0)));
	TestTrue(TEXT("Run Up moves to Bag"), Widget->Navigate(FVector2D(0.0, 1.0)));
	TestTrue(TEXT("Bag Left moves to Fight"), Widget->Navigate(FVector2D(-1.0, 0.0)));
	TestEqual(TEXT("The second adjacency cycle returns to Fight"),
		GetFocusedCommand(*this, Widget), EBattleUICommand::Fight);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleCommandNavigationClampTest,
	"PokemonSolarus.UI.Battle.Command.Navigation.ClampDiagonalAndZero",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleCommandNavigationClampTest::RunTest(const FString& Parameters)
{
	UBattleCommandWidget* Widget = NewObject<UBattleCommandWidget>();
	if (!TestNotNull(TEXT("The native command widget exists"), Widget))
	{
		return false;
	}

	const FBattleCommandDisplayState State = MakeAvailableCommandState();
	TestTrue(TEXT("The clamp test starts from valid state"), Widget->ApplyDisplayState(State));
	int32 FocusSignalCount = 0;
	Widget->GetCommandFocusChangedNativeDelegate().AddLambda(
		[&FocusSignalCount](const EBattleUICommand)
		{
			++FocusSignalCount;
		});

	TestFalse(TEXT("Fight clamps on Up"), Widget->Navigate(FVector2D(0.0, 1.0)));
	TestFalse(TEXT("Fight clamps on Left"), Widget->Navigate(FVector2D(-1.0, 0.0)));
	TestFalse(TEXT("Zero input is ignored"), Widget->Navigate(FVector2D::ZeroVector));
	TestFalse(TEXT("Diagonal input is ignored"), Widget->Navigate(FVector2D(1.0, 1.0)));
	TestEqual(TEXT("Ignored input emits no focus signal"), FocusSignalCount, 0);
	TestEqual(TEXT("Ignored input leaves Fight focused"),
		GetFocusedCommand(*this, Widget), EBattleUICommand::Fight);

	TestTrue(TEXT("Navigate to Bag"), Widget->Navigate(FVector2D(1.0, 0.0)));
	TestFalse(TEXT("Bag clamps on Up"), Widget->Navigate(FVector2D(0.0, 1.0)));
	TestFalse(TEXT("Bag clamps on Right"), Widget->Navigate(FVector2D(1.0, 0.0)));

	TestTrue(TEXT("Resetting valid state restores Fight"), Widget->ApplyDisplayState(State));
	TestTrue(TEXT("Navigate to Pokemon"), Widget->Navigate(FVector2D(0.0, -1.0)));
	TestFalse(TEXT("Pokemon clamps on Down"), Widget->Navigate(FVector2D(0.0, -1.0)));
	TestFalse(TEXT("Pokemon clamps on Left"), Widget->Navigate(FVector2D(-1.0, 0.0)));
	TestTrue(TEXT("Navigate to Run"), Widget->Navigate(FVector2D(1.0, 0.0)));
	TestFalse(TEXT("Run clamps on Down"), Widget->Navigate(FVector2D(0.0, -1.0)));
	TestFalse(TEXT("Run clamps on Right"), Widget->Navigate(FVector2D(1.0, 0.0)));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleCommandStateValidationTest,
	"PokemonSolarus.UI.Battle.Command.State.ValidAndInvalidAtomicApplication",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleCommandStateValidationTest::RunTest(const FString& Parameters)
{
	UBattleCommandWidget* Widget = NewObject<UBattleCommandWidget>();
	if (!TestNotNull(TEXT("The native command widget exists"), Widget))
	{
		return false;
	}

	FBattleCommandDisplayState InvalidInitialState = MakeAvailableCommandState();
	InvalidInitialState.Bag.bAvailable = false;
	AddExpectedError(
		TEXT("Rejected invalid Battle command display state: Bag is unavailable but has no display reason."),
		EAutomationExpectedErrorFlags::Contains,
		1);
	TestFalse(TEXT("An unavailable command requires a display reason"),
		Widget->ApplyDisplayState(InvalidInitialState));
	TestFalse(TEXT("Invalid initial state remains inactive"), Widget->IsCommandMenuActive());
	TestEqual(TEXT("Invalid initial state remains hidden"),
		Widget->GetVisibility(), ESlateVisibility::Collapsed);
	FBattleCommandDisplayState UnsetState;
	TestFalse(TEXT("Invalid initial state never becomes the cached state"),
		Widget->TryGetDisplayState(UnsetState));

	FBattleCommandDisplayState ValidState = MakeAvailableCommandState();
	ValidState.Fight.bAvailable = false;
	ValidState.Fight.UnavailableReason = FText::FromString(TEXT("Fight is unavailable."));
	TestTrue(TEXT("A complete state applies atomically"), Widget->ApplyDisplayState(ValidState));
	TestTrue(TEXT("Valid state activates the menu"), Widget->IsCommandMenuActive());
	TestEqual(TEXT("Valid state is visible"),
		Widget->GetVisibility(), ESlateVisibility::Visible);
	TestEqual(TEXT("Fight remains the default focus even when unavailable"),
		GetFocusedCommand(*this, Widget), EBattleUICommand::Fight);
	TestEqual(TEXT("Unavailable default focus exposes its supplied reason"),
		GetCurrentBattleText(*this, Widget), FString(TEXT("Fight is unavailable.")));

	FBattleCommandDisplayState InvalidUpdate = MakeAvailableCommandState();
	InvalidUpdate.NormalPrompt = FText::GetEmpty();
	AddExpectedError(
		TEXT("Rejected invalid Battle command display state: NormalPrompt is empty."),
		EAutomationExpectedErrorFlags::Contains,
		1);
	TestFalse(TEXT("An empty normal prompt rejects the full update"),
		Widget->ApplyDisplayState(InvalidUpdate));
	TestFalse(TEXT("A rejected update fails closed"), Widget->IsCommandMenuActive());
	TestEqual(TEXT("A rejected update hides command input"),
		Widget->GetVisibility(), ESlateVisibility::Collapsed);

	FBattleCommandDisplayState CachedState;
	TestTrue(TEXT("The last fully valid state remains inspectable"),
		Widget->TryGetDisplayState(CachedState));
	TestEqual(TEXT("The rejected prompt was not partially copied"),
		CachedState.NormalPrompt.ToString(), ValidState.NormalPrompt.ToString());
	TestFalse(TEXT("The rejected availability was not partially copied"),
		CachedState.Fight.bAvailable);
	TestEqual(TEXT("The last valid reason remains intact"),
		CachedState.Fight.UnavailableReason.ToString(),
		ValidState.Fight.UnavailableReason.ToString());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleCommandAvailableActivationTest,
	"PokemonSolarus.UI.Battle.Command.Activation.AvailableExactOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleCommandAvailableActivationTest::RunTest(const FString& Parameters)
{
	UBattleCommandWidget* Widget = NewObject<UBattleCommandWidget>();
	if (!TestNotNull(TEXT("The native command widget exists"), Widget))
	{
		return false;
	}

	int32 PressedCount = 0;
	int32 RequestCount = 0;
	EBattleUICommand LastRequested = EBattleUICommand::Fight;
	Widget->GetCommandPressedNativeDelegate().AddLambda(
		[&PressedCount](const EBattleUICommand)
		{
			++PressedCount;
		});
	Widget->GetCommandRequestedNativeDelegate().AddLambda(
		[&RequestCount, &LastRequested](const EBattleUICommand Command)
		{
			++RequestCount;
			LastRequested = Command;
		});

	const FBattleCommandDisplayState State = MakeAvailableCommandState();
	TestTrue(TEXT("The activation test starts from valid state"), Widget->ApplyDisplayState(State));
	TestTrue(TEXT("Available Fight confirms"), Widget->ConfirmFocusedCommand());
	TestEqual(TEXT("Fight emits one pressed signal"), PressedCount, 1);
	TestEqual(TEXT("Fight emits one local request"), RequestCount, 1);
	TestEqual(TEXT("The Fight request is typed"), LastRequested, EBattleUICommand::Fight);

	Widget->Navigate(FVector2D(1.0, 0.0));
	TestTrue(TEXT("Available Bag confirms"), Widget->ConfirmFocusedCommand());
	TestEqual(TEXT("Bag adds exactly one pressed signal"), PressedCount, 2);
	TestEqual(TEXT("Bag adds exactly one local request"), RequestCount, 2);
	TestEqual(TEXT("The Bag request is typed"), LastRequested, EBattleUICommand::Bag);

	Widget->ApplyDisplayState(State);
	Widget->Navigate(FVector2D(0.0, -1.0));
	TestTrue(TEXT("Available Pokemon confirms"), Widget->ConfirmFocusedCommand());
	TestEqual(TEXT("Pokemon adds exactly one pressed signal"), PressedCount, 3);
	TestEqual(TEXT("Pokemon adds exactly one local request"), RequestCount, 3);
	TestEqual(TEXT("The Pokemon request is typed"), LastRequested, EBattleUICommand::Pokemon);

	Widget->Navigate(FVector2D(1.0, 0.0));
	TestTrue(TEXT("Available Run confirms locally"), Widget->ConfirmFocusedCommand());
	TestEqual(TEXT("Run adds exactly one pressed signal"), PressedCount, 4);
	TestEqual(TEXT("Run adds exactly one local request"), RequestCount, 4);
	TestEqual(TEXT("The Run request remains UI-local and typed"),
		LastRequested, EBattleUICommand::Run);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleCommandReentrantDelegateTest,
	"PokemonSolarus.UI.Battle.Command.Delegates.ReentrantObserversPreservePayloads",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleCommandReentrantDelegateTest::RunTest(const FString& Parameters)
{
	UBattleCommandWidget* ConfirmWidget = NewObject<UBattleCommandWidget>();
	if (!TestNotNull(TEXT("The re-entrant Confirm widget exists"), ConfirmWidget))
	{
		return false;
	}

	const FBattleCommandDisplayState AvailableState = MakeAvailableCommandState();
	TestTrue(TEXT("The re-entrant Confirm test starts from valid state"),
		ConfirmWidget->ApplyDisplayState(AvailableState));
	EBattleUICommand RequestedCommand = EBattleUICommand::Run;
	int32 RequestCount = 0;
	ConfirmWidget->GetCommandPressedNativeDelegate().AddLambda(
		[ConfirmWidget](const EBattleUICommand)
		{
			ConfirmWidget->Navigate(FVector2D(1.0, 0.0));
		});
	ConfirmWidget->GetCommandRequestedNativeDelegate().AddLambda(
		[&RequestedCommand, &RequestCount](const EBattleUICommand Command)
		{
			RequestedCommand = Command;
			++RequestCount;
		});

	TestTrue(TEXT("Fight remains an accepted Confirm before listener re-entry"),
		ConfirmWidget->ConfirmFocusedCommand());
	TestEqual(TEXT("The pressed listener changed live focus to Bag"),
		GetFocusedCommand(*this, ConfirmWidget), EBattleUICommand::Bag);
	TestEqual(TEXT("Re-entry still emits exactly one request"), RequestCount, 1);
	TestEqual(TEXT("The request preserves the originally confirmed Fight payload"),
		RequestedCommand, EBattleUICommand::Fight);

	UBattleCommandWidget* FocusWidget = NewObject<UBattleCommandWidget>();
	if (!TestNotNull(TEXT("The re-entrant focus widget exists"), FocusWidget))
	{
		return false;
	}

	FBattleCommandDisplayState FocusState = MakeAvailableCommandState();
	FocusState.Bag.bAvailable = false;
	FocusState.Bag.UnavailableReason = FText::FromString(TEXT("Bag reason."));
	FocusState.Run.bAvailable = false;
	FocusState.Run.UnavailableReason = FText::FromString(TEXT("Run reason."));
	TestTrue(TEXT("The re-entrant focus test starts from valid state"),
		FocusWidget->ApplyDisplayState(FocusState));

	TArray<EBattleUICommand> FocusPayloads;
	TArray<FString> TextPayloads;
	bool bDidReenter = false;
	FocusWidget->GetCommandFocusChangedNativeDelegate().AddLambda(
		[FocusWidget, &FocusPayloads, &bDidReenter](const EBattleUICommand Command)
		{
			FocusPayloads.Add(Command);
			if (!bDidReenter && Command == EBattleUICommand::Bag)
			{
				bDidReenter = true;
				FocusWidget->Navigate(FVector2D(0.0, -1.0));
			}
		});
	FocusWidget->GetBattleTextChangedNativeDelegate().AddLambda(
		[&TextPayloads](const FText& BattleText)
		{
			TextPayloads.Add(BattleText.ToString());
		});

	TestTrue(TEXT("Fight Right begins the outer focus change to Bag"),
		FocusWidget->Navigate(FVector2D(1.0, 0.0)));
	TestEqual(TEXT("Both outer and re-entrant focus notifications are observed"),
		FocusPayloads.Num(), 2);
	if (FocusPayloads.Num() == 2)
	{
		TestEqual(TEXT("The outer focus payload remains Bag"),
			FocusPayloads[0], EBattleUICommand::Bag);
		TestEqual(TEXT("The re-entrant focus payload is Run"),
			FocusPayloads[1], EBattleUICommand::Run);
	}
	TestEqual(TEXT("Both supplied reasons are observed"), TextPayloads.Num(), 2);
	if (TextPayloads.Num() == 2)
	{
		TestEqual(TEXT("The outer focus and text publish as one coherent pair"),
			TextPayloads[0], FString(TEXT("Bag reason.")));
		TestEqual(TEXT("The deferred re-entrant pair publishes final Run text last"),
			TextPayloads[1], FString(TEXT("Run reason.")));
	}
	TestEqual(TEXT("Live focus reflects the completed re-entrant navigation"),
		GetFocusedCommand(*this, FocusWidget), EBattleUICommand::Run);
	TestEqual(TEXT("The final emitted text matches final live focus"),
		GetCurrentBattleText(*this, FocusWidget), FString(TEXT("Run reason.")));

	UBattleCommandWidget* TextNavigateWidget = NewObject<UBattleCommandWidget>();
	if (!TestNotNull(TEXT("The text-listener navigation widget exists"), TextNavigateWidget))
	{
		return false;
	}

	FBattleCommandDisplayState TextNavigateState = MakeAvailableCommandState();
	TextNavigateState.Bag.bAvailable = false;
	TextNavigateState.Bag.UnavailableReason = FText::FromString(TEXT("Text Bag reason."));
	TextNavigateState.Run.bAvailable = false;
	TextNavigateState.Run.UnavailableReason = FText::FromString(TEXT("Text Run reason."));
	TestTrue(TEXT("The text-listener navigation test starts from valid state"),
		TextNavigateWidget->ApplyDisplayState(TextNavigateState));
	TestTrue(TEXT("The text-listener navigation test focuses unavailable Bag"),
		TextNavigateWidget->Navigate(FVector2D(1.0, 0.0)));

	TArray<FString> TextNavigateNotificationOrder;
	bool bTextListenerNavigated = false;
	TextNavigateWidget->GetCommandFocusChangedNativeDelegate().AddLambda(
		[&TextNavigateNotificationOrder](const EBattleUICommand Command)
		{
			TextNavigateNotificationOrder.Add(FString::Printf(
				TEXT("Focus:%d"),
				static_cast<int32>(Command)));
		});
	TextNavigateWidget->GetBattleTextChangedNativeDelegate().AddLambda(
		[TextNavigateWidget,
			&TextNavigateNotificationOrder,
			&bTextListenerNavigated](const FText& BattleText)
		{
			const FString TextValue = BattleText.ToString();
			TextNavigateNotificationOrder.Add(FString::Printf(
				TEXT("Text:%s"),
				*TextValue));
			if (!bTextListenerNavigated
				&& TextValue == TEXT("Text Bag reason."))
			{
				bTextListenerNavigated = true;
				TextNavigateWidget->Navigate(FVector2D(0.0, -1.0));
			}
		});

	TestFalse(TEXT("Unavailable Confirm remains text-only before listener re-entry"),
		TextNavigateWidget->ConfirmFocusedCommand());
	TestEqual(TEXT("The initial text and deferred final pair emit in order"),
		TextNavigateNotificationOrder.Num(), 3);
	if (TextNavigateNotificationOrder.Num() == 3)
	{
		TestEqual(TEXT("Unavailable Confirm first reaffirms the current Bag reason"),
			TextNavigateNotificationOrder[0], FString(TEXT("Text:Text Bag reason.")));
		TestEqual(TEXT("Listener navigation then publishes final Run focus"),
			TextNavigateNotificationOrder[1], FString::Printf(
				TEXT("Focus:%d"),
				static_cast<int32>(EBattleUICommand::Run)));
		TestEqual(TEXT("Listener navigation publishes matching Run text last"),
			TextNavigateNotificationOrder[2], FString(TEXT("Text:Text Run reason.")));
	}
	TestEqual(TEXT("Text-listener navigation leaves final focus on Run"),
		GetFocusedCommand(*this, TextNavigateWidget), EBattleUICommand::Run);
	TestEqual(TEXT("Text-listener navigation leaves final text consistent with Run"),
		GetCurrentBattleText(*this, TextNavigateWidget), FString(TEXT("Text Run reason.")));

	UBattleCommandWidget* LoopWidget = NewObject<UBattleCommandWidget>();
	if (!TestNotNull(TEXT("The bounded re-entry widget exists"), LoopWidget))
	{
		return false;
	}

	TestTrue(TEXT("The bounded re-entry test starts from valid state"),
		LoopWidget->ApplyDisplayState(AvailableState));
	int32 LoopFocusSignalCount = 0;
	LoopWidget->GetCommandFocusChangedNativeDelegate().AddLambda(
		[LoopWidget, &LoopFocusSignalCount](const EBattleUICommand Command)
		{
			++LoopFocusSignalCount;
			if (Command == EBattleUICommand::Bag)
			{
				LoopWidget->Navigate(FVector2D(0.0, -1.0));
			}
			else if (Command == EBattleUICommand::Run)
			{
				LoopWidget->Navigate(FVector2D(0.0, 1.0));
			}
		});
	AddExpectedError(
		TEXT("Battle command listeners exceeded the bounded re-entrant notification limit; command input was disabled."),
		EAutomationExpectedErrorFlags::Contains,
		1);
	TestTrue(TEXT("The loop starts with a valid Fight-to-Bag navigation"),
		LoopWidget->Navigate(FVector2D(1.0, 0.0)));
	TestTrue(TEXT("The loop emitted at least its initial focus signal"),
		LoopFocusSignalCount > 0);
	TestTrue(TEXT("The loop stopped at the defensive pass bound"),
		LoopFocusSignalCount <= 8);
	TestFalse(TEXT("Continuous listener mutation fails command input closed"),
		LoopWidget->IsCommandMenuActive());
	TestEqual(TEXT("Continuous listener mutation hides the menu"),
		LoopWidget->GetVisibility(), ESlateVisibility::Collapsed);

	UBattleCommandWidget* TextLoopWidget = NewObject<UBattleCommandWidget>();
	if (!TestNotNull(TEXT("The bounded text-only re-entry widget exists"), TextLoopWidget))
	{
		return false;
	}

	FBattleCommandDisplayState TextLoopState = MakeAvailableCommandState();
	TextLoopState.Fight.bAvailable = false;
	TextLoopState.Fight.UnavailableReason = FText::FromString(TEXT("Text loop reason."));
	TestTrue(TEXT("The bounded text-only re-entry test starts from valid state"),
		TextLoopWidget->ApplyDisplayState(TextLoopState));
	int32 TextLoopSignalCount = 0;
	int32 TextLoopFocusSignalCount = 0;
	TextLoopWidget->GetCommandFocusChangedNativeDelegate().AddLambda(
		[&TextLoopFocusSignalCount](const EBattleUICommand)
		{
			++TextLoopFocusSignalCount;
		});
	TextLoopWidget->GetBattleTextChangedNativeDelegate().AddLambda(
		[TextLoopWidget, &TextLoopSignalCount](const FText&)
		{
			++TextLoopSignalCount;
			TextLoopWidget->ConfirmFocusedCommand();
		});
	AddExpectedError(
		TEXT("Battle command listeners exceeded the bounded re-entrant notification limit; command input was disabled."),
		EAutomationExpectedErrorFlags::Contains,
		1);
	TestFalse(TEXT("Unavailable Confirm stays non-activating during text-only re-entry"),
		TextLoopWidget->ConfirmFocusedCommand());
	TestTrue(TEXT("Text-only re-entry emitted at least its initial text signal"),
		TextLoopSignalCount > 0);
	TestTrue(TEXT("Text-only re-entry stopped at the defensive pass bound"),
		TextLoopSignalCount <= 8);
	TestEqual(TEXT("Text-only re-entry emitted no focus signal"),
		TextLoopFocusSignalCount, 0);
	TestFalse(TEXT("Continuous text-only re-entry fails command input closed"),
		TextLoopWidget->IsCommandMenuActive());
	TestEqual(TEXT("Continuous text-only re-entry hides the menu"),
		TextLoopWidget->GetVisibility(), ESlateVisibility::Collapsed);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleCommandUnavailableActivationTest,
	"PokemonSolarus.UI.Battle.Command.Activation.UnavailableReasonAndNoActivation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleCommandUnavailableActivationTest::RunTest(const FString& Parameters)
{
	UBattleCommandWidget* Widget = NewObject<UBattleCommandWidget>();
	if (!TestNotNull(TEXT("The native command widget exists"), Widget))
	{
		return false;
	}

	int32 PressedCount = 0;
	int32 RequestCount = 0;
	int32 TextSignalCount = 0;
	Widget->GetCommandPressedNativeDelegate().AddLambda(
		[&PressedCount](const EBattleUICommand)
		{
			++PressedCount;
		});
	Widget->GetCommandRequestedNativeDelegate().AddLambda(
		[&RequestCount](const EBattleUICommand)
		{
			++RequestCount;
		});
	Widget->GetBattleTextChangedNativeDelegate().AddLambda(
		[&TextSignalCount](const FText&)
		{
			++TextSignalCount;
		});

	FBattleCommandDisplayState State = MakeAvailableCommandState();
	State.Bag.bAvailable = false;
	State.Bag.UnavailableReason = FText::FromString(TEXT("There are no usable items."));
	TestTrue(TEXT("A supplied unavailable reason validates"), Widget->ApplyDisplayState(State));
	const int32 SignalsAfterApply = TextSignalCount;

	TestTrue(TEXT("Unavailable Bag remains focusable"), Widget->Navigate(FVector2D(1.0, 0.0)));
	TestEqual(TEXT("Bag is focused"),
		GetFocusedCommand(*this, Widget), EBattleUICommand::Bag);
	TestEqual(TEXT("Focusing Bag exposes its supplied reason"),
		GetCurrentBattleText(*this, Widget), FString(TEXT("There are no usable items.")));
	TestEqual(TEXT("Focus publishes the unavailable reason once"),
		TextSignalCount, SignalsAfterApply + 1);

	TestFalse(TEXT("Unavailable Confirm does not activate"), Widget->ConfirmFocusedCommand());
	TestEqual(TEXT("Unavailable Confirm emits no pressed signal"), PressedCount, 0);
	TestEqual(TEXT("Unavailable Confirm emits no command request"), RequestCount, 0);
	TestEqual(TEXT("Unavailable Confirm reaffirms the supplied reason"),
		TextSignalCount, SignalsAfterApply + 2);
	TestEqual(TEXT("The reaffirmed text remains the supplied reason"),
		GetCurrentBattleText(*this, Widget), FString(TEXT("There are no usable items.")));

	TestTrue(TEXT("Returning focus to Fight succeeds"), Widget->Navigate(FVector2D(-1.0, 0.0)));
	TestEqual(TEXT("Available focus restores the normal prompt"),
		GetCurrentBattleText(*this, Widget), State.NormalPrompt.ToString());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleCommandTopLevelCancelTest,
	"PokemonSolarus.UI.Battle.Command.Cancel.TopLevelNoOp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleCommandTopLevelCancelTest::RunTest(const FString& Parameters)
{
	UBattleCommandWidget* Widget = NewObject<UBattleCommandWidget>();
	if (!TestNotNull(TEXT("The native command widget exists"), Widget))
	{
		return false;
	}

	int32 FocusSignalCount = 0;
	int32 TextSignalCount = 0;
	int32 PressedCount = 0;
	int32 RequestCount = 0;
	Widget->GetCommandFocusChangedNativeDelegate().AddLambda(
		[&FocusSignalCount](const EBattleUICommand)
		{
			++FocusSignalCount;
		});
	Widget->GetBattleTextChangedNativeDelegate().AddLambda(
		[&TextSignalCount](const FText&)
		{
			++TextSignalCount;
		});
	Widget->GetCommandPressedNativeDelegate().AddLambda(
		[&PressedCount](const EBattleUICommand)
		{
			++PressedCount;
		});
	Widget->GetCommandRequestedNativeDelegate().AddLambda(
		[&RequestCount](const EBattleUICommand)
		{
			++RequestCount;
		});

	const FBattleCommandDisplayState State = MakeAvailableCommandState();
	TestTrue(TEXT("The Cancel test starts from valid state"), Widget->ApplyDisplayState(State));
	Widget->Navigate(FVector2D(1.0, 0.0));
	Widget->Navigate(FVector2D(0.0, -1.0));
	const EBattleUICommand FocusBeforeCancel = GetFocusedCommand(*this, Widget);
	const FString TextBeforeCancel = GetCurrentBattleText(*this, Widget);
	const int32 FocusSignalsBeforeCancel = FocusSignalCount;
	const int32 TextSignalsBeforeCancel = TextSignalCount;

	Widget->HandleTopLevelCancel();
	TestEqual(TEXT("Top-level Cancel preserves focus"),
		GetFocusedCommand(*this, Widget), FocusBeforeCancel);
	TestEqual(TEXT("Top-level Cancel preserves text"),
		GetCurrentBattleText(*this, Widget), TextBeforeCancel);
	TestEqual(TEXT("Top-level Cancel emits no focus signal"),
		FocusSignalCount, FocusSignalsBeforeCancel);
	TestEqual(TEXT("Top-level Cancel emits no text signal"),
		TextSignalCount, TextSignalsBeforeCancel);
	TestEqual(TEXT("Top-level Cancel emits no pressed signal"), PressedCount, 0);
	TestEqual(TEXT("Top-level Cancel emits no command request"), RequestCount, 0);

	Widget->DeactivateCommandMenu();
	TestFalse(TEXT("Inactive navigation is ignored"), Widget->Navigate(FVector2D(1.0, 0.0)));
	TestFalse(TEXT("Inactive Confirm is ignored"), Widget->ConfirmFocusedCommand());
	Widget->HandleTopLevelCancel();
	TestFalse(TEXT("Inactive top-level Cancel remains a no-op"), Widget->IsCommandMenuActive());

	return true;
}

#endif
