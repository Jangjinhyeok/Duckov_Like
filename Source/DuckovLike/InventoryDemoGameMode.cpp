#include "InventoryDemoGameMode.h"

#include "InventoryDemoPlayerController.h"

AInventoryDemoGameMode::AInventoryDemoGameMode()
{
    PlayerControllerClass = AInventoryDemoPlayerController::StaticClass();
}
