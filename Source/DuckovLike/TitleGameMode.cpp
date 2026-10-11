#include "TitleGameMode.h"

#include "TitlePlayerController.h"

ATitleGameMode::ATitleGameMode()
{
    PlayerControllerClass = ATitlePlayerController::StaticClass();
    DefaultPawnClass = nullptr;
}
