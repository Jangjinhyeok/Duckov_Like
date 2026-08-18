#pragma once

#include "InventoryContainer.h"
#include "InventoryOperationTypes.h"

struct INVENTORYCORE_API FInventoryPlacement
{
    static EInventoryOperationFailure TryPlace(FInventoryContainer& Container, const FItemInstance& NewItem);
};
