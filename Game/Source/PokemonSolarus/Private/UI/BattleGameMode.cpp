#include "UI/BattleGameMode.h"

#include "Battle/BattleDataTableRuntimeSource.h"
#include "Battle/BattleDecision.h"
#include "Battle/BattleEngine.h"
#include "Battle/BattleRuntimeSource.h"
#include "Battle/BattleSnapshot.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "UI/BattleHUDDisplayState.h"
#include "UI/BattleHUDWidget.h"
#include "UI/BattlePlayerController.h"
#include "UI/BattlePresentationAdapter.h"

DEFINE_LOG_CATEGORY_STATIC(LogBattleGameMode, Log, All);

namespace BattleGameModePrivate
{
	const TCHAR* DefaultBattleRuntimeScenarioPath =
		TEXT("/Game/Data/Battle/Initial/DT_BattleRuntimeScenario.DT_BattleRuntimeScenario");
	constexpr int32 MaxLockedActionSteps = 8;
}

ABattleGameMode::ABattleGameMode()
{
	PlayerControllerClass = ABattlePlayerController::StaticClass();
	DefaultPawnClass = nullptr;
	SpectatorClass = nullptr;
	bStartPlayersAsSpectators = true;
	BattleRuntimeScenarioTable = TSoftObjectPtr<UDataTable>(
		FSoftObjectPath(BattleGameModePrivate::DefaultBattleRuntimeScenarioPath));
}

ABattleGameMode::~ABattleGameMode() = default;

void ABattleGameMode::BeginPlay()
{
	Super::BeginPlay();

	if (!InitializeBattleRuntime())
	{
		if (ABattlePlayerController* PlayerController = BattlePlayerController.Get())
		{
			DisableInvalidPresentation(*PlayerController);
		}
		return;
	}

	if (UWorld* World = GetWorld())
	{
		if (ABattlePlayerController* PlayerController =
			Cast<ABattlePlayerController>(World->GetFirstPlayerController()))
		{
			AttachToBattlePlayerController(*PlayerController);
		}
	}
}

void ABattleGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	DetachFromBattlePlayerController();
	ResetBattleRuntime();
	BattleRuntimeSource.Reset();
	Super::EndPlay(EndPlayReason);
}

void ABattleGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);

	if (ABattlePlayerController* BattleController =
		Cast<ABattlePlayerController>(NewPlayer))
	{
		AttachToBattlePlayerController(*BattleController);
	}
}

void ABattleGameMode::Logout(AController* Exiting)
{
	const bool bActiveBattleControllerIsExiting =
		BattlePlayerController.Get() == Exiting;
	Super::Logout(Exiting);
	if (!bActiveBattleControllerIsExiting)
	{
		return;
	}

	DetachFromBattlePlayerController();
	if (UWorld* World = GetWorld())
	{
		for (FConstPlayerControllerIterator Iterator =
			World->GetPlayerControllerIterator(); Iterator; ++Iterator)
		{
			ABattlePlayerController* Candidate =
				Cast<ABattlePlayerController>(Iterator->Get());
			if (IsValid(Candidate) && Candidate != Exiting)
			{
				AttachToBattlePlayerController(*Candidate);
				return;
			}
		}
	}
}

void ABattleGameMode::EnsureBattleRuntimeSource()
{
	if (!BattleRuntimeSource)
	{
		BattleRuntimeSource = MakeUnique<FBattleDataTableRuntimeSource>(
			BattleRuntimeScenarioTable);
	}
}

bool ABattleGameMode::InitializeBattleRuntime()
{
	if (BattleEngine && LocalTrainerId.IsValid()
		&& BattleDisplayNames.IsValid())
	{
		return true;
	}

	ResetBattleRuntime();
	EnsureBattleRuntimeSource();
	FBattleRuntimeBundle Bundle;
	FString SetupError;
	if (!TryCreateStartedRuntimeBundle(Bundle, SetupError))
	{
		UE_LOG(LogBattleGameMode, Error,
			TEXT("The Battle runtime could not initialize: %s"), *SetupError);
		return false;
	}

	BattleEngine = MoveTemp(Bundle.Engine);
	LocalTrainerId = Bundle.LocalTrainerId;
	BattleDisplayNames = MoveTemp(Bundle.DisplayNames);
	ResetPresentedRequest();
	if (ABattlePlayerController* PlayerController = BattlePlayerController.Get())
	{
		if (PlayerController->IsBattleHUDAvailable())
		{
			BindBattleHUDCommandRequest(*PlayerController);
			RefreshBattleHUDPresentation();
		}
	}
	return true;
}

bool ABattleGameMode::TryCreateStartedRuntimeBundle(
	FBattleRuntimeBundle& OutBundle,
	FString& OutError) const
{
	OutBundle.Reset();
	OutError.Reset();
	if (!BattleRuntimeSource)
	{
		OutError = TEXT("No Battle runtime source is configured.");
		return false;
	}
	if (!BattleRuntimeSource->TryCreateInitialBattle(OutBundle, OutError))
	{
		return false;
	}
	if (!OutBundle.IsValid())
	{
		OutBundle.Reset();
		OutError = TEXT("The runtime source returned an invalid Battle bundle.");
		return false;
	}

	FBattleRejection Rejection;
	if (!OutBundle.Engine->TryBeginActionDecisionSequence(Rejection))
	{
		OutBundle.Reset();
		OutError = FString::Printf(
			TEXT("The Battle runtime could not enter command selection (rejection %d)."),
			static_cast<int32>(Rejection.Reason));
		return false;
	}
	return true;
}

void ABattleGameMode::ResetBattleRuntime()
{
	BattleEngine.Reset();
	LocalTrainerId = FTrainerId();
	BattleDisplayNames.Reset();
	DisplayedPlayerBattlerId = FBattlerId();
	DisplayedOpponentBattlerId = FBattlerId();
	bBattleTurnInProgress = false;
	bBattleRuntimeFailed = false;
	ResetPresentedRequest();
}

void ABattleGameMode::SetBattleRuntimeSourceForTesting(
	TUniquePtr<IBattleRuntimeSource>&& RuntimeSource)
{
	ResetBattleRuntime();
	BattleRuntimeSource = MoveTemp(RuntimeSource);
}

void ABattleGameMode::AttachToBattlePlayerController(
	ABattlePlayerController& PlayerController)
{
	if (BattlePlayerController.Get() != &PlayerController)
	{
		DetachFromBattlePlayerController();
		BattlePlayerController = &PlayerController;
		ResetPresentedRequest();
		BattleHUDAvailableHandle =
			PlayerController.GetBattleHUDAvailableNativeDelegate().AddUObject(
				this, &ABattleGameMode::HandleBattleHUDAvailable);
	}

	if (BattleEngine && BattleDisplayNames.IsValid()
		&& PlayerController.IsBattleHUDAvailable())
	{
		BindBattleHUDCommandRequest(PlayerController);
		RefreshBattleHUDPresentation();
	}
}

void ABattleGameMode::DetachFromBattlePlayerController()
{
	UnbindBattleHUDCommandRequest();
	if (ABattlePlayerController* PlayerController = BattlePlayerController.Get())
	{
		PlayerController->DisableBattleHUDInputPreservingPresentation();
		if (BattleHUDAvailableHandle.IsValid())
		{
			PlayerController->GetBattleHUDAvailableNativeDelegate().Remove(
				BattleHUDAvailableHandle);
		}
	}
	BattleHUDAvailableHandle.Reset();
	BattlePlayerController.Reset();
	ResetPresentedRequest();
}

void ABattleGameMode::HandleBattleHUDAvailable(
	ABattlePlayerController& PlayerController)
{
	if (BattlePlayerController.Get() != &PlayerController)
	{
		return;
	}

	BindBattleHUDCommandRequest(PlayerController);
	ResetPresentedRequest();
	RefreshBattleHUDPresentation();
}

void ABattleGameMode::BindBattleHUDCommandRequest(
	ABattlePlayerController& PlayerController)
{
	UBattleHUDWidget* HUD = PlayerController.GetBattleHUDWidget();
	if (!PlayerController.IsBattleHUDAvailable() || !IsValid(HUD))
	{
		UnbindBattleHUDCommandRequest();
		return;
	}
	if (BoundBattleHUD.Get() == HUD)
	{
		return;
	}

	UnbindBattleHUDCommandRequest();
	BoundBattleHUD = HUD;
	HUD->OnCommandRequested.AddUniqueDynamic(
		this,
		&ABattleGameMode::HandleBattleCommandRequested);
}

void ABattleGameMode::UnbindBattleHUDCommandRequest()
{
	if (UBattleHUDWidget* HUD = BoundBattleHUD.Get())
	{
		HUD->OnCommandRequested.RemoveDynamic(
			this,
			&ABattleGameMode::HandleBattleCommandRequested);
	}
	BoundBattleHUD.Reset();
}

void ABattleGameMode::HandleBattleCommandRequested(
	const EBattleUICommand RequestedCommand)
{
	if (bBattleRuntimeFailed)
	{
		return;
	}
	if (bBattleTurnInProgress)
	{
		UE_LOG(LogBattleGameMode, Warning,
			TEXT("Ignored a duplicate Battle command while a turn was resolving."));
		return;
	}

	ABattlePlayerController* PlayerController = BattlePlayerController.Get();
	if (PlayerController)
	{
		PlayerController->DisableBattleHUDInputPreservingPresentation();
	}

	if (RequestedCommand != EBattleUICommand::Fight)
	{
		FailBattleRuntime(TEXT("The playable prototype received a command other than Fight."));
		return;
	}

	TGuardValue<bool> TurnGuard(bBattleTurnInProgress, true);
	FString Error;
	if (!TryResolveRequestedTurn(Error))
	{
		FailBattleRuntime(Error);
	}
}

bool ABattleGameMode::TryResolveRequestedTurn(FString& OutError)
{
	OutError.Reset();
	ABattlePlayerController* PlayerController = BattlePlayerController.Get();
	if (!BattleEngine || !PlayerController
		|| !PlayerController->IsBattleHUDAvailable()
		|| BoundBattleHUD.Get() != PlayerController->GetBattleHUDWidget())
	{
		OutError = TEXT("The Battle runtime or bound HUD is unavailable for Fight.");
		return false;
	}

	FBattleSnapshot Snapshot;
	FBattleDecisionRequest Request;
	if (!TryGetCurrentPresentationContext(Snapshot, Request, OutError))
	{
		return false;
	}
	if (!IsSamePresentedRequest(
		Request, PlayerController->GetBattleHUDPresentationGeneration()))
	{
		OutError = TEXT("The Fight request does not match the currently presented Battle request.");
		return false;
	}

	FBattleDecision PlayerDecision;
	if (!TryCreateSoleFightDecision(Request, PlayerDecision, OutError))
	{
		return false;
	}
	const FBattleResolution PlayerSubmission =
		BattleEngine->SubmitDecision(PlayerDecision);
	if (!PlayerSubmission.WasAccepted())
	{
		OutError = FString::Printf(
			TEXT("The player Fight decision was rejected (reason %d)."),
			static_cast<int32>(PlayerSubmission.GetRejection().Reason));
		return false;
	}
	UE_LOG(LogBattleGameMode, Log,
		TEXT("Accepted the sole legal player Fight move %s."),
		*PlayerDecision.GetMoveId().GetDefinitionId().GetName().ToString());

	ResetPresentedRequest();
	return TrySubmitSoleOpponentFightDecision(OutError)
		&& TryAdvanceLockedFightTurn(OutError)
		&& TryPresentPostTurnState(OutError);
}

bool ABattleGameMode::TryCreateSoleFightDecision(
	const FBattleDecisionRequest& Request,
	FBattleDecision& OutDecision,
	FString& OutError) const
{
	OutDecision = FBattleDecision();
	OutError.Reset();
	if (!Request.IsValid()
		|| Request.GetRequestKind() != EBattleDecisionRequestKind::Action)
	{
		OutError = TEXT("The one-move policy received an invalid non-action request.");
		return false;
	}
	if (Request.GetLegalActionKinds().Num() != 1
		|| Request.GetLegalActionKinds()[0] != EBattleActionKind::Fight)
	{
		OutError = TEXT("The one-move policy requires Fight to be the only legal action.");
		return false;
	}
	if (Request.GetLegalMoveIds().Num() != 1)
	{
		OutError = FString::Printf(
			TEXT("The one-move policy requires exactly one legal move, but received %d."),
			Request.GetLegalMoveIds().Num());
		return false;
	}

	const FMoveId MoveId = Request.GetLegalMoveIds()[0];
	FActiveSlotId Target;
	int32 MatchingTargetCount = 0;
	for (const FBattleMoveTargetOption& Option : Request.GetLegalMoveTargets())
	{
		if (Option.MoveId == MoveId)
		{
			Target = Option.ActiveSlotId;
			++MatchingTargetCount;
		}
	}
	if (MatchingTargetCount != 1 || !Target.IsValid())
	{
		OutError = FString::Printf(
			TEXT("The one-move policy requires exactly one legal target, but received %d."),
			MatchingTargetCount);
		return false;
	}

	if (!FBattleDecision::TryCreateFight(
		Request.GetStateVersion(),
		Request.GetDecisionOwnerTrainerId(),
		Request.GetActingBattlerId(),
		MoveId,
		Target,
		OutDecision))
	{
		OutError = TEXT("The sole legal move and target did not create a valid Fight decision.");
		return false;
	}
	return true;
}

bool ABattleGameMode::TrySubmitSoleOpponentFightDecision(FString& OutError)
{
	OutError.Reset();
	if (!BattleEngine)
	{
		OutError = TEXT("The Battle engine is unavailable for opponent selection.");
		return false;
	}

	const TArray<FBattleDecisionRequest> Requests =
		BattleEngine->GetPendingDecisionRequests();
	if (Requests.Num() != 1)
	{
		OutError = FString::Printf(
			TEXT("The opponent policy requires exactly one pending request, but received %d."),
			Requests.Num());
		return false;
	}
	const FBattleDecisionRequest& Request = Requests[0];
	if (Request.GetDecisionOwnerTrainerId() == LocalTrainerId)
	{
		OutError = TEXT("The opponent policy received another local Trainer request.");
		return false;
	}

	FBattleDecision OpponentDecision;
	if (!TryCreateSoleFightDecision(Request, OpponentDecision, OutError))
	{
		return false;
	}
	const FBattleResolution OpponentSubmission =
		BattleEngine->SubmitDecision(OpponentDecision);
	if (!OpponentSubmission.WasAccepted())
	{
		OutError = FString::Printf(
			TEXT("The opponent Fight decision was rejected (reason %d)."),
			static_cast<int32>(OpponentSubmission.GetRejection().Reason));
		return false;
	}
	UE_LOG(LogBattleGameMode, Log,
		TEXT("Accepted the sole legal opponent Fight move %s."),
		*OpponentDecision.GetMoveId().GetDefinitionId().GetName().ToString());

	if (!BattleEngine->GetPendingDecisionRequests().IsEmpty()
		|| BattleEngine->GetSnapshot().GetPhase() != EBattlePhase::Locked)
	{
		OutError = TEXT("The two Fight decisions did not lock exactly one Battle turn.");
		return false;
	}
	return true;
}

bool ABattleGameMode::TryAdvanceLockedFightTurn(FString& OutError)
{
	OutError.Reset();
	if (!BattleEngine)
	{
		OutError = TEXT("The Battle engine is unavailable for turn advancement.");
		return false;
	}

	int32 StepCount = 0;
	while (BattleEngine->GetSnapshot().GetPhase() == EBattlePhase::Locked
		|| BattleEngine->GetSnapshot().GetPhase() == EBattlePhase::Resolving)
	{
		if (StepCount++ >= BattleGameModePrivate::MaxLockedActionSteps)
		{
			OutError = TEXT("The Battle turn exceeded the guarded locked-action limit.");
			return false;
		}
		if (!BattleEngine->GetPendingDecisionRequests().IsEmpty())
		{
			OutError = TEXT("An unexpected decision request interrupted locked-action resolution.");
			return false;
		}

		const FBattleResolution ActionStart = BattleEngine->BeginNextLockedAction();
		if (!ActionStart.WasAccepted())
		{
			OutError = FString::Printf(
				TEXT("BeginNextLockedAction was rejected (reason %d)."),
				static_cast<int32>(ActionStart.GetRejection().Reason));
			return false;
		}
		const TOptional<FBattleLockedAction> CurrentAction =
			BattleEngine->GetCurrentLockedAction();
		if (!CurrentAction.IsSet())
		{
			continue;
		}
		if (CurrentAction->Decision.GetActionKind() != EBattleActionKind::Fight)
		{
			OutError = TEXT("The playable Fight turn contained a non-Fight locked action.");
			return false;
		}

		const FBattleResolution MoveCommit =
			BattleEngine->CommitCurrentMoveAfterPreMoveGates();
		if (!MoveCommit.WasAccepted())
		{
			OutError = FString::Printf(
				TEXT("CommitCurrentMoveAfterPreMoveGates was rejected (reason %d)."),
				static_cast<int32>(MoveCommit.GetRejection().Reason));
			return false;
		}
		if (!BattleEngine->GetCurrentLockedAction().IsSet())
		{
			continue;
		}

		const FBattleResolution TargetResolution =
			BattleEngine->ResolveCurrentMoveTargets();
		if (!TargetResolution.WasAccepted())
		{
			OutError = FString::Printf(
				TEXT("ResolveCurrentMoveTargets was rejected (reason %d)."),
				static_cast<int32>(TargetResolution.GetRejection().Reason));
			return false;
		}
		if (!BattleEngine->GetCurrentLockedAction().IsSet())
		{
			continue;
		}

		const FBattleResolution EffectResolution =
			BattleEngine->ExecuteCurrentMoveEffects();
		if (!EffectResolution.WasAccepted())
		{
			OutError = FString::Printf(
				TEXT("ExecuteCurrentMoveEffects was rejected (reason %d)."),
				static_cast<int32>(EffectResolution.GetRejection().Reason));
			return false;
		}
	}

	const EBattlePhase BoundaryPhase = BattleEngine->GetSnapshot().GetPhase();
	if (BoundaryPhase == EBattlePhase::Terminal)
	{
		return true;
	}
	if (BoundaryPhase != EBattlePhase::EndOfTurn)
	{
		OutError = TEXT("The locked actions reached neither an end-turn nor terminal boundary.");
		return false;
	}
	const FBattleResolution EndTurnResolution = BattleEngine->ResolveEndTurn();
	if (!EndTurnResolution.WasAccepted())
	{
		OutError = FString::Printf(
			TEXT("ResolveEndTurn was rejected (reason %d)."),
			static_cast<int32>(EndTurnResolution.GetRejection().Reason));
		return false;
	}
	return true;
}

bool ABattleGameMode::TryPresentPostTurnState(FString& OutError)
{
	OutError.Reset();
	if (!BattleEngine || !LocalTrainerId.IsValid())
	{
		OutError = TEXT("The Battle runtime is unavailable after turn resolution.");
		return false;
	}

	const FBattleSnapshot Snapshot =
		BattleEngine->GetSnapshotForObserver(LocalTrainerId);
	if (!Snapshot.IsValid())
	{
		OutError = TEXT("The Battle engine returned an invalid post-turn snapshot.");
		return false;
	}
	if (Snapshot.GetOutcome() == EBattleOutcome::InProgress)
	{
		if (Snapshot.GetPhase() != EBattlePhase::Selecting)
		{
			OutError = TEXT("An active Battle did not return to command selection.");
			return false;
		}
		ResetPresentedRequest();
		if (!RefreshBattleHUDPresentation())
		{
			OutError = TEXT("The next Battle command request could not be presented.");
			return false;
		}
		UE_LOG(LogBattleGameMode, Log,
			TEXT("Completed one Battle turn and presented the next Fight request."));
		return true;
	}

	if (Snapshot.GetPhase() != EBattlePhase::Terminal)
	{
		OutError = TEXT("A completed Battle outcome was published outside Terminal phase.");
		return false;
	}
	return TryPresentTerminalState(Snapshot, OutError);
}

bool ABattleGameMode::TryPresentTerminalState(
	const FBattleSnapshot& Snapshot,
	FString& OutError)
{
	OutError.Reset();
	if (!DisplayedPlayerBattlerId.IsValid()
		|| !DisplayedOpponentBattlerId.IsValid())
	{
		OutError = TEXT("The terminal presentation has no remembered displayed battlers.");
		return false;
	}
	ABattlePlayerController* PlayerController = BattlePlayerController.Get();
	UBattleHUDWidget* HUD = PlayerController
		? PlayerController->GetBattleHUDWidget()
		: nullptr;
	if (!PlayerController || !PlayerController->IsBattleHUDAvailable()
		|| !IsValid(HUD) || !HUD->IsPresentationVisible())
	{
		OutError = TEXT("The Battle HUD is unavailable for the terminal presentation.");
		return false;
	}
	FBattleHUDDisplayState LastDisplayState;
	if (!HUD->TryGetLastValidatedDisplayState(LastDisplayState))
	{
		OutError = TEXT("The Battle HUD has no validated state to preserve at terminal outcome.");
		return false;
	}

	const FBattleObservedBattler* PlayerBattler =
		Snapshot.FindObservedBattler(DisplayedPlayerBattlerId);
	const FBattleObservedBattler* OpponentBattler =
		Snapshot.FindObservedBattler(DisplayedOpponentBattlerId);
	FBattleSnapshot CompleteSnapshot;
	if (!PlayerBattler || !OpponentBattler)
	{
		// Observer projections omit removed opponents. Read only the already-displayed
		// battlers' final health from the matching complete public snapshot.
		CompleteSnapshot = BattleEngine->GetSnapshot();
		if (!CompleteSnapshot.IsValid()
			|| CompleteSnapshot.GetStateVersion() != Snapshot.GetStateVersion()
			|| CompleteSnapshot.GetBattleId() != Snapshot.GetBattleId())
		{
			OutError = TEXT("The terminal Battle state is unavailable for displayed health.");
			return false;
		}
		PlayerBattler = PlayerBattler
			? PlayerBattler
			: CompleteSnapshot.FindObservedBattler(DisplayedPlayerBattlerId);
		OpponentBattler = OpponentBattler
			? OpponentBattler
			: CompleteSnapshot.FindObservedBattler(DisplayedOpponentBattlerId);
	}
	if (!PlayerBattler || !OpponentBattler
		|| PlayerBattler->MaxHP <= 0 || OpponentBattler->MaxHP <= 0
		|| PlayerBattler->CurrentHP < 0
		|| PlayerBattler->CurrentHP > PlayerBattler->MaxHP
		|| OpponentBattler->CurrentHP < 0
		|| OpponentBattler->CurrentHP > OpponentBattler->MaxHP
		|| PlayerBattler->bFainted != (PlayerBattler->CurrentHP == 0)
		|| OpponentBattler->bFainted != (OpponentBattler->CurrentHP == 0))
	{
		OutError = TEXT("The terminal Battle state has invalid displayed battler health.");
		return false;
	}
	if (!HUD->InitializeHealthPanels(
		LastDisplayState.Player.PokemonName,
		PlayerBattler->CurrentHP,
		PlayerBattler->MaxHP,
		LastDisplayState.Opponent.PokemonName,
		OpponentBattler->CurrentHP,
		OpponentBattler->MaxHP))
	{
		OutError = TEXT("The Battle HUD rejected authoritative terminal health.");
		return false;
	}

	FText ResultText;
	if (Snapshot.GetOutcome() == EBattleOutcome::Victory)
	{
		ResultText = NSLOCTEXT("BattleGameMode", "PlayerVictory", "You won.");
	}
	else if (Snapshot.GetOutcome() == EBattleOutcome::Defeat)
	{
		ResultText = NSLOCTEXT("BattleGameMode", "PlayerDefeat", "You lost.");
	}
	else
	{
		OutError = FString::Printf(
			TEXT("The playable prototype reached unsupported terminal outcome %d."),
			static_cast<int32>(Snapshot.GetOutcome()));
		return false;
	}
	if (!HUD->PresentBattleStatusText(ResultText))
	{
		OutError = TEXT("The Battle HUD rejected the terminal result text.");
		return false;
	}

	ResetPresentedRequest();
	UE_LOG(LogBattleGameMode, Log,
		TEXT("Completed the playable Battle with outcome %d."),
		static_cast<int32>(Snapshot.GetOutcome()));
	return true;
}

void ABattleGameMode::FailBattleRuntime(const FString& Error)
{
	bBattleRuntimeFailed = true;
	ResetPresentedRequest();
	UE_LOG(LogBattleGameMode, Error,
		TEXT("Battle runtime advancement stopped: %s"), *Error);

	ABattlePlayerController* PlayerController = BattlePlayerController.Get();
	if (!PlayerController)
	{
		return;
	}
	PlayerController->DisableBattleHUDInputPreservingPresentation();
	if (UBattleHUDWidget* HUD = PlayerController->GetBattleHUDWidget();
		IsValid(HUD) && HUD->IsPresentationVisible()
		&& !HUD->PresentBattleStatusText(
			NSLOCTEXT("BattleGameMode", "BattleError", "Battle error.")))
	{
		UE_LOG(LogBattleGameMode, Error,
			TEXT("The Battle HUD could not present the runtime failure text."));
	}
}

bool ABattleGameMode::TryFindLocalActionRequest(
	const FBattleSnapshot& Snapshot,
	const FBattleDecisionRequest*& OutRequest,
	FString& OutError) const
{
	OutRequest = nullptr;
	for (const FBattleDecisionRequest& Request : Snapshot.GetPendingDecisionRequests())
	{
		if (Request.GetRequestKind() != EBattleDecisionRequestKind::Action
			|| Request.GetDecisionOwnerTrainerId() != LocalTrainerId)
		{
			continue;
		}
		if (OutRequest != nullptr)
		{
			OutError = TEXT("The local Trainer has multiple pending action requests.");
			return false;
		}
		OutRequest = &Request;
	}
	if (OutRequest == nullptr)
	{
		OutError = TEXT("The local Trainer has no pending action request.");
		return false;
	}
	return true;
}

bool ABattleGameMode::TryGetCurrentPresentationContext(
	FBattleSnapshot& OutSnapshot,
	FBattleDecisionRequest& OutRequest,
	FString& OutError) const
{
	OutSnapshot = FBattleSnapshot();
	OutRequest = FBattleDecisionRequest();
	OutError.Reset();
	if (!BattleEngine || !LocalTrainerId.IsValid()
		|| !BattleDisplayNames.IsValid())
	{
		OutError = TEXT("The Battle runtime presentation dependencies are invalid.");
		return false;
	}

	FBattleSnapshot Snapshot = BattleEngine->GetSnapshotForObserver(LocalTrainerId);
	if (!Snapshot.IsValid()
		|| Snapshot.GetPhase() != EBattlePhase::Selecting
		|| Snapshot.GetOutcome() != EBattleOutcome::InProgress)
	{
		OutError = TEXT("The Battle runtime is not awaiting a local action selection.");
		return false;
	}

	const FBattleDecisionRequest* Request = nullptr;
	if (!TryFindLocalActionRequest(Snapshot, Request, OutError))
	{
		return false;
	}
	OutRequest = *Request;
	OutSnapshot = MoveTemp(Snapshot);
	return true;
}

void ABattleGameMode::DisableInvalidPresentation(
	ABattlePlayerController& PlayerController)
{
	PlayerController.DisableBattleHUDInputPreservingPresentation();
	ResetPresentedRequest();
}

bool ABattleGameMode::RejectPresentationUpdate(
	ABattlePlayerController& PlayerController,
	const FString& Error)
{
	UE_LOG(LogBattleGameMode, Warning,
		TEXT("The Battle HUD update was rejected: %s"), *Error);
	DisableInvalidPresentation(PlayerController);
	return false;
}

bool ABattleGameMode::TryApplyHUDDisplayState(
	ABattlePlayerController& PlayerController,
	const FBattleSnapshot& Snapshot,
	const FBattleDecisionRequest& Request,
	FString& OutError)
{
	FBattlerId PlayerBattlerId;
	FBattlerId OpponentBattlerId;
	if (!TryResolveDisplayedBattlers(
		Snapshot,
		Request,
		PlayerBattlerId,
		OpponentBattlerId,
		OutError))
	{
		return false;
	}

	FBattleHUDDisplayState DisplayState;
	if (!FBattlePresentationAdapter::TryBuildHUDDisplayState(
		Snapshot, Request.GetActingSlotId(), *BattleDisplayNames,
		DisplayState, OutError))
	{
		return false;
	}
	if (!PlayerController.ApplyBattleHUDDisplayState(DisplayState))
	{
		OutError = TEXT("The Battle controller rejected a complete HUD display state.");
		return false;
	}
	DisplayedPlayerBattlerId = PlayerBattlerId;
	DisplayedOpponentBattlerId = OpponentBattlerId;
	return true;
}

bool ABattleGameMode::TryResolveDisplayedBattlers(
	const FBattleSnapshot& Snapshot,
	const FBattleDecisionRequest& Request,
	FBattlerId& OutPlayerBattlerId,
	FBattlerId& OutOpponentBattlerId,
	FString& OutError) const
{
	OutPlayerBattlerId = FBattlerId();
	OutOpponentBattlerId = FBattlerId();
	OutError.Reset();
	if (Snapshot.GetFormat() != EBattleFormat::Single
		|| Request.GetDecisionOwnerTrainerId() != LocalTrainerId
		|| !Request.GetActingBattlerId().IsValid()
		|| !Request.GetActingSlotId().IsValid())
	{
		OutError = TEXT("The playable HUD requires one valid local Single Battle request.");
		return false;
	}

	const EBattleSide PlayerSide = Request.GetActingSlotId().GetSide();
	int32 OpponentSlotCount = 0;
	for (const FBattleObservedActiveSlot& Slot : Snapshot.GetObservedActiveSlots())
	{
		if (!Slot.bAvailable || Slot.ActiveSlotId.GetSide() == PlayerSide)
		{
			continue;
		}
		OutOpponentBattlerId = Slot.BattlerId;
		++OpponentSlotCount;
	}
	if (OpponentSlotCount != 1 || !OutOpponentBattlerId.IsValid())
	{
		OutError = FString::Printf(
			TEXT("The playable HUD requires exactly one displayed opponent, but found %d."),
			OpponentSlotCount);
		return false;
	}

	OutPlayerBattlerId = Request.GetActingBattlerId();
	return true;
}

bool ABattleGameMode::RefreshBattleHUDPresentation()
{
	ABattlePlayerController* PlayerController = BattlePlayerController.Get();
	if (PlayerController == nullptr || !PlayerController->IsBattleHUDAvailable())
	{
		return false;
	}

	FBattleSnapshot Snapshot;
	FBattleDecisionRequest Request;
	FString PresentationError;
	if (!TryGetCurrentPresentationContext(Snapshot, Request, PresentationError))
	{
		return RejectPresentationUpdate(*PlayerController, PresentationError);
	}

	const uint64 HUDGeneration =
		PlayerController->GetBattleHUDPresentationGeneration();
	if (HUDGeneration == 0)
	{
		return RejectPresentationUpdate(
			*PlayerController, TEXT("The available Battle HUD has no generation."));
	}
	if (IsSamePresentedRequest(Request, HUDGeneration))
	{
		return true;
	}

	if (!TryApplyHUDDisplayState(
		*PlayerController, Snapshot, Request, PresentationError))
	{
		return RejectPresentationUpdate(*PlayerController, PresentationError);
	}

	RememberPresentedRequest(Request, HUDGeneration);
	return true;
}

bool ABattleGameMode::RefreshCommandSelectionPresentation()
{
	return RefreshBattleHUDPresentation();
}

bool ABattleGameMode::IsSamePresentedRequest(
	const FBattleDecisionRequest& Request,
	const uint64 HUDGeneration) const
{
	return bHasPresentedRequest
		&& PresentedHUDGeneration == HUDGeneration
		&& PresentedStateVersion == Request.GetStateVersion()
		&& PresentedTrainerId == Request.GetDecisionOwnerTrainerId()
		&& PresentedBattlerId == Request.GetActingBattlerId()
		&& PresentedActiveSlotId == Request.GetActingSlotId();
}

void ABattleGameMode::RememberPresentedRequest(
	const FBattleDecisionRequest& Request,
	const uint64 HUDGeneration)
{
	bHasPresentedRequest = true;
	PresentedHUDGeneration = HUDGeneration;
	PresentedStateVersion = Request.GetStateVersion();
	PresentedTrainerId = Request.GetDecisionOwnerTrainerId();
	PresentedBattlerId = Request.GetActingBattlerId();
	PresentedActiveSlotId = Request.GetActingSlotId();
}

void ABattleGameMode::ResetPresentedRequest()
{
	bHasPresentedRequest = false;
	PresentedHUDGeneration = 0;
	PresentedStateVersion = 0;
	PresentedTrainerId = FTrainerId();
	PresentedBattlerId = FBattlerId();
	PresentedActiveSlotId = FActiveSlotId();
}
