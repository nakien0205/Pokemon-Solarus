#include "UI/BattleGameMode.h"

#include "UI/BattlePlayerController.h"

ABattleGameMode::ABattleGameMode()
{
	PlayerControllerClass = ABattlePlayerController::StaticClass();
	DefaultPawnClass = nullptr;
	SpectatorClass = nullptr;
	bStartPlayersAsSpectators = true;
}
