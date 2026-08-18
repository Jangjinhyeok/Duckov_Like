#include "InventoryPlacement.h"

#include "InventoryPlacementInternal.h"
#include "ItemDefinitionRow.h"

namespace InventoryPlacementInternal
{
int32 GetCellIndex(const FIntPoint Cell, const FIntPoint GridSize)
{
    return Cell.Y * GridSize.X + Cell.X;
}

bool TryGetFootprint(const FItemInstance& Item, FIntPoint& OutFootprint)
{
    const UDataTable* DefinitionTable = Item.DefinitionTable.LoadSynchronous();
    const FItemDefinitionRow* Definition = DefinitionTable
        ? DefinitionTable->FindRow<FItemDefinitionRow>(Item.DefinitionRowName, TEXT("InventoryPlacement"))
        : nullptr;
    if (Definition == nullptr)
    {
        return false;
    }

    OutFootprint = Definition->Size;
    if (Item.bRotated)
    {
        Swap(OutFootprint.X, OutFootprint.Y);
    }
    return true;
}

bool FitsInContainer(
    const FInventoryContainer& Container,
    const FIntPoint AnchorCell,
    const FIntPoint Footprint)
{
    return AnchorCell.X >= 0 && AnchorCell.Y >= 0 &&
        Footprint.X > 0 && Footprint.Y > 0 &&
        AnchorCell.X + Footprint.X <= Container.GridSize.X &&
        AnchorCell.Y + Footprint.Y <= Container.GridSize.Y;
}
}

EInventoryOperationFailure FInventoryPlacement::TryPlace(
    FInventoryContainer& Container,
    const FItemInstance& NewItem)
{
    FIntPoint Footprint;
    if (!InventoryPlacementInternal::TryGetFootprint(NewItem, Footprint))
    {
        return EInventoryOperationFailure::NoSpace;
    }

    if (!InventoryPlacementInternal::FitsInContainer(Container, NewItem.AnchorCell, Footprint))
    {
        return EInventoryOperationFailure::NoSpace;
    }

    for (int32 Y = NewItem.AnchorCell.Y; Y < NewItem.AnchorCell.Y + Footprint.Y; ++Y)
    {
        for (int32 X = NewItem.AnchorCell.X; X < NewItem.AnchorCell.X + Footprint.X; ++X)
        {
            if (Container.OccupancyCache[
                    InventoryPlacementInternal::GetCellIndex(FIntPoint(X, Y), Container.GridSize)] != INDEX_NONE)
            {
                return EInventoryOperationFailure::Occupied;
            }
        }
    }

    const int32 ItemIndex = Container.Items.Add(NewItem);
    for (int32 Y = NewItem.AnchorCell.Y; Y < NewItem.AnchorCell.Y + Footprint.Y; ++Y)
    {
        for (int32 X = NewItem.AnchorCell.X; X < NewItem.AnchorCell.X + Footprint.X; ++X)
        {
            Container.OccupancyCache[
                InventoryPlacementInternal::GetCellIndex(FIntPoint(X, Y), Container.GridSize)] = ItemIndex;
        }
    }

    return EInventoryOperationFailure::None;
}

void FInventoryPlacement::RebuildOccupancyCache(FInventoryContainer& Container)
{
    Container.OccupancyCache.Init(INDEX_NONE, Container.GridSize.X * Container.GridSize.Y);

    for (int32 ItemIndex = 0; ItemIndex < Container.Items.Num(); ++ItemIndex)
    {
        const FItemInstance& Item = Container.Items[ItemIndex];
        FIntPoint Footprint;
        if (!InventoryPlacementInternal::TryGetFootprint(Item, Footprint) ||
            !InventoryPlacementInternal::FitsInContainer(Container, Item.AnchorCell, Footprint))
        {
            continue;
        }

        for (int32 Y = Item.AnchorCell.Y; Y < Item.AnchorCell.Y + Footprint.Y; ++Y)
        {
            for (int32 X = Item.AnchorCell.X; X < Item.AnchorCell.X + Footprint.X; ++X)
            {
                Container.OccupancyCache[
                    InventoryPlacementInternal::GetCellIndex(FIntPoint(X, Y), Container.GridSize)] = ItemIndex;
            }
        }
    }
}
