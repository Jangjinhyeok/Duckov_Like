#include "InventoryContainer.h"

FInventoryContainer FInventoryContainer::MakeEmpty(const FIntPoint InGridSize)
{
    FInventoryContainer Container;
    Container.GridSize = InGridSize;
    Container.OccupancyCache.Init(INDEX_NONE, InGridSize.X * InGridSize.Y);
    return Container;
}
