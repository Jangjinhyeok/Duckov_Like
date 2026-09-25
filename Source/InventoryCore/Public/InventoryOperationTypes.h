#pragma once

#include "CoreMinimal.h"
#include "InventoryOperationTypes.generated.h"

UENUM(BlueprintType)
enum class EInventoryOperationFailure : uint8
{
    None,
    NoSpace,
    Occupied,
    ItemNotFound,
    StackMismatch,
    StackFull,
    ResizeOverflow,
    InvalidContainer,
    OperationInProgress,
};
