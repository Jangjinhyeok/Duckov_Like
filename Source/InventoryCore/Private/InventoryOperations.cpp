#include "InventoryOperations.h"

#include "InventoryContainer.h"
#include "InventoryPlacement.h"
#include "InventoryPlacementInternal.h"
#include "ItemDefinitionRow.h"

namespace
{
const FItemDefinitionRow* TryGetDefinition(const FItemInstance& Item)
{
    const UDataTable* DefinitionTable = Item.DefinitionTable.LoadSynchronous();
    return DefinitionTable
        ? DefinitionTable->FindRow<FItemDefinitionRow>(Item.DefinitionRowName, TEXT("InventoryOperations"))
        : nullptr;
}
}

EInventoryOperationFailure FInventoryOperations::TrySort(FInventoryContainer& Container)
{
    struct FSortEntry
    {
        int32 ItemIndex;
        FIntPoint Footprint;
        int64 Area;
    };

    TArray<FSortEntry> Entries;
    Entries.Reserve(Container.Items.Num());
    for (int32 Index = 0; Index < Container.Items.Num(); ++Index)
    {
        FIntPoint Footprint;
        if (!InventoryPlacementInternal::TryGetFootprint(Container.Items[Index], Footprint) ||
            Footprint.X <= 0 || Footprint.Y <= 0)
        {
            return EInventoryOperationFailure::NoSpace;
        }
        Entries.Add({Index, Footprint, static_cast<int64>(Footprint.X) * Footprint.Y});
    }
    Entries.Sort([&Container](const FSortEntry& Left, const FSortEntry& Right)
    {
        return Left.Area != Right.Area ? Left.Area > Right.Area :
            Container.Items[Left.ItemIndex].InstanceId < Container.Items[Right.ItemIndex].InstanceId;
    });

    FInventoryContainer Planned = FInventoryContainer::MakeEmpty(Container.GridSize);
    for (const FSortEntry& Entry : Entries)
    {
        FItemInstance Item = Container.Items[Entry.ItemIndex];
        bool bPlaced = false;
        for (int32 Y = 0; Y <= Container.GridSize.Y - Entry.Footprint.Y && !bPlaced; ++Y)
        {
            for (int32 X = 0; X <= Container.GridSize.X - Entry.Footprint.X; ++X)
            {
                Item.AnchorCell = FIntPoint(X, Y);
                if (FInventoryPlacement::TryPlace(Planned, Item) == EInventoryOperationFailure::None)
                {
                    bPlaced = true;
                    break;
                }
            }
        }
        if (!bPlaced)
        {
            return EInventoryOperationFailure::NoSpace;
        }
    }

    Container = MoveTemp(Planned);
    return EInventoryOperationFailure::None;
}

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

EInventoryOperationFailure FInventoryOperations::TryStack(
    FInventoryContainer& SourceContainer,
    FInventoryContainer& DestContainer,
    const int32 SourceInstanceId,
    const int32 TargetInstanceId)
{
    const int32 SourceIndex = SourceContainer.Items.IndexOfByPredicate(
        [SourceInstanceId](const FItemInstance& Item)
        {
            return Item.InstanceId == SourceInstanceId;
        });
    if (SourceIndex == INDEX_NONE)
    {
        return EInventoryOperationFailure::ItemNotFound;
    }

    const int32 TargetIndex = DestContainer.Items.IndexOfByPredicate(
        [TargetInstanceId](const FItemInstance& Item)
        {
            return Item.InstanceId == TargetInstanceId;
        });
    if (TargetIndex == INDEX_NONE)
    {
        return EInventoryOperationFailure::ItemNotFound;
    }

    FItemInstance& Source = SourceContainer.Items[SourceIndex];
    FItemInstance& Target = DestContainer.Items[TargetIndex];
    if (Source.DefinitionRowName != Target.DefinitionRowName ||
        Source.DefinitionTable != Target.DefinitionTable)
    {
        return EInventoryOperationFailure::StackMismatch;
    }

    const FItemDefinitionRow* Definition = TryGetDefinition(Target);
    if (Definition == nullptr || !Definition->bStackable)
    {
        return EInventoryOperationFailure::StackMismatch;
    }

    const int32 AvailableCapacity = Definition->MaxStack - Target.Quantity;
    if (AvailableCapacity <= 0)
    {
        return EInventoryOperationFailure::StackFull;
    }

    const int32 MoveQuantity = FMath::Min(Source.Quantity, AvailableCapacity);
    Target.Quantity += MoveQuantity;
    Source.Quantity -= MoveQuantity;

    if (Source.Quantity == 0)
    {
        SourceContainer.Items.RemoveAt(SourceIndex);
        FInventoryPlacement::RebuildOccupancyCache(SourceContainer);
    }

    return EInventoryOperationFailure::None;
}
