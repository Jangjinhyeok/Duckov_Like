#pragma once

#include "CoreMinimal.h"
#include "ItemInstance.h"
#include "InventoryContainer.generated.h"

USTRUCT()
struct INVENTORYCORE_API FInventoryContainer
{
    GENERATED_BODY()

    UPROPERTY()
    FIntPoint GridSize = FIntPoint(1, 1);

    UPROPERTY()
    TArray<FItemInstance> Items;

    UPROPERTY(Transient)
    TArray<int32> OccupancyCache;

    static FInventoryContainer MakeEmpty(FIntPoint InGridSize);
};
