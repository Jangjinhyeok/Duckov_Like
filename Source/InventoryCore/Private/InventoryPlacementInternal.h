#pragma once

#include "CoreMinimal.h"

struct FInventoryContainer;
struct FItemInstance;

namespace InventoryPlacementInternal
{
int32 GetCellIndex(FIntPoint Cell, FIntPoint GridSize);
bool TryGetFootprint(const FItemInstance& Item, FIntPoint& OutFootprint);
bool FitsInContainer(const FInventoryContainer& Container, FIntPoint AnchorCell, FIntPoint Footprint);
}
