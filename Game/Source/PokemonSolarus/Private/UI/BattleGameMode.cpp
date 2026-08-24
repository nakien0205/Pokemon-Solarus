#include "UI/BattleGameMode.h"

#include "Battle/BattleDataTableRuntimeSource.h"
#include "Battle/BattleEngine.h"
#include "Battle/BattleRuntimeSource.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "UI/BattleHUDDisplayState.h"
#include "UI/BattlePlayerController.h"
#include "UI/BattlePresentationAdapter.h"

DEFINE_LOG_CATEGORY_STATIC(LogBattleGameMode, Log, All);

namespace
{
	const TCHAR* DefaultBattleRuntimeScenarioPath =
		TEXT("/Game/Data/Battle/Initial/DT_BattleRuntimeScenario.DT_BattleRuntimeScenario");
}

ABattleGameMode::ABattleGameMode()
{
	PlayerControllerClass = ABattlePlayerController::StaticClass();
	DefaultPawnClass = nullptr;
	SpectatorClass = nullptr;
	bStartPlayersAsSpectators = true;
	BattleRuntimeScenarioTable = TSoftObjectPtr<UDataTable>(
		FSoftObjectPath(DefaultBattleRuntimeScenarioPath));
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
		RefreshBattleHUDPresentation();
	}
}

void ABattleGameMode::DetachFromBattlePlayerController()
{
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

	ResetPresentedRequest();
	RefreshBattleHUDPresentation();
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
	FString& OutError) const
{
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
