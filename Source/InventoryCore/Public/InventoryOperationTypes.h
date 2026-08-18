#pragma once

#include "CoreMinimal.h"
#include "InventoryOperationTypes.generated.h"

UENUM()
enum class EInventoryOperationFailure : uint8
{
    None,
    NoSpace,
    Occupied,
    ItemNotFound,
};
