#include "BasementGameMode.h"

#include "DuckovCharacter.h"
#include "InventoryDemoPlayerController.h"

ABasementGameMode::ABasementGameMode()
{
    PlayerControllerClass = AInventoryDemoPlayerController::StaticClass();
    DefaultPawnClass = ADuckovCharacter::StaticClass();
}
