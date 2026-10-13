#include "BasementGameMode.h"

#include "DuckovCharacter.h"
#include "DuckovPlayerController.h"

ABasementGameMode::ABasementGameMode()
{
    PlayerControllerClass = ADuckovPlayerController::StaticClass();
    DefaultPawnClass = ADuckovCharacter::StaticClass();
}
