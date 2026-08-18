#include "InventoryPlacement.h"

#include "ItemDefinitionRow.h"

namespace
{
int32 GetCellIndex(const FIntPoint Cell, const FIntPoint GridSize)
{
    return Cell.Y * GridSize.X + Cell.X;
}
}

EInventoryOperationFailure FInventoryPlacement::TryPlace(
    FInventoryContainer& Container,
    const FItemInstance& NewItem)
{
    const UDataTable* DefinitionTable = NewItem.DefinitionTable.LoadSynchronous();
    const FItemDefinitionRow* Definition = DefinitionTable
        ? DefinitionTable->FindRow<FItemDefinitionRow>(NewItem.DefinitionRowName, TEXT("InventoryPlacement"))
        : nullptr;
    if (Definition == nullptr)
    {
        return EInventoryOperationFailure::NoSpace;
    }

    FIntPoint Footprint = Definition->Size;
    if (NewItem.bRotated)
    {
        Swap(Footprint.X, Footprint.Y);
    }

    if (NewItem.AnchorCell.X < 0 || NewItem.AnchorCell.Y < 0 ||
        Footprint.X <= 0 || Footprint.Y <= 0 ||
        NewItem.AnchorCell.X + Footprint.X > Container.GridSize.X ||
        NewItem.AnchorCell.Y + Footprint.Y > Container.GridSize.Y)
    {
        return EInventoryOperationFailure::NoSpace;
    }

    for (int32 Y = NewItem.AnchorCell.Y; Y < NewItem.AnchorCell.Y + Footprint.Y; ++Y)
    {
        for (int32 X = NewItem.AnchorCell.X; X < NewItem.AnchorCell.X + Footprint.X; ++X)
        {
            if (Container.OccupancyCache[GetCellIndex(FIntPoint(X, Y), Container.GridSize)] != INDEX_NONE)
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
            Container.OccupancyCache[GetCellIndex(FIntPoint(X, Y), Container.GridSize)] = ItemIndex;
        }
    }

    return EInventoryOperationFailure::None;
}
