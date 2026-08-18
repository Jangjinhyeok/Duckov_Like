#include "InventoryOperations.h"

#include "InventoryContainer.h"
#include "InventoryPlacement.h"
#include "InventoryPlacementInternal.h"

EInventoryOperationFailure FInventoryOperations::TryMove(
    FInventoryContainer& SourceContainer,
    FInventoryContainer& DestContainer,
    const int32 InstanceId,
    const FIntPoint DestAnchorCell,
    const bool bDestRotated)
{
    const int32 SourceIndex = SourceContainer.Items.IndexOfByPredicate(
        [InstanceId](const FItemInstance& Item)
        {
            return Item.InstanceId == InstanceId;
        });
    if (SourceIndex == INDEX_NONE)
    {
        return EInventoryOperationFailure::ItemNotFound;
    }

    FItemInstance MovedItem = SourceContainer.Items[SourceIndex];
    FIntPoint SourceFootprint;
    if (!InventoryPlacementInternal::TryGetFootprint(MovedItem, SourceFootprint))
    {
        return EInventoryOperationFailure::NoSpace;
    }

    MovedItem.AnchorCell = DestAnchorCell;
    MovedItem.bRotated = bDestRotated;

    FIntPoint Footprint;
    if (!InventoryPlacementInternal::TryGetFootprint(MovedItem, Footprint) ||
        !InventoryPlacementInternal::FitsInContainer(DestContainer, DestAnchorCell, Footprint))
    {
        return EInventoryOperationFailure::NoSpace;
    }

    const bool bSameContainer = &SourceContainer == &DestContainer;
    for (int32 Y = DestAnchorCell.Y; Y < DestAnchorCell.Y + Footprint.Y; ++Y)
    {
        for (int32 X = DestAnchorCell.X; X < DestAnchorCell.X + Footprint.X; ++X)
        {
            const int32 OccupantIndex = DestContainer.OccupancyCache[
                InventoryPlacementInternal::GetCellIndex(FIntPoint(X, Y), DestContainer.GridSize)];
            const bool bIsCurrentItemCell = bSameContainer &&
                OccupantIndex == SourceIndex &&
                X >= SourceContainer.Items[SourceIndex].AnchorCell.X &&
                X < SourceContainer.Items[SourceIndex].AnchorCell.X + SourceFootprint.X &&
                Y >= SourceContainer.Items[SourceIndex].AnchorCell.Y &&
                Y < SourceContainer.Items[SourceIndex].AnchorCell.Y + SourceFootprint.Y;
            if (OccupantIndex != INDEX_NONE && !bIsCurrentItemCell)
            {
                return EInventoryOperationFailure::Occupied;
            }
        }
    }

    SourceContainer.Items.RemoveAt(SourceIndex);
    DestContainer.Items.Add(MovedItem);
    FInventoryPlacement::RebuildOccupancyCache(SourceContainer);
    FInventoryPlacement::RebuildOccupancyCache(DestContainer);

    return EInventoryOperationFailure::None;
}
