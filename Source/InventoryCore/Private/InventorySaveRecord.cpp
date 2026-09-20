#include "InventorySaveRecord.h"

#include "InventoryPlacement.h"
#include "ItemDefinitionRow.h"

namespace
{
EInventorySaveFailure BuildContainers(
    const FInventorySaveRecord& Record, TArray<FInventoryNamedContainer>& Planned)
{
    if (Record.FormatVersion != FInventorySaveRecord::CurrentFormatVersion)
    {
        return EInventorySaveFailure::UnsupportedVersion;
    }
    if (Record.NextInstanceId < 0)
    {
        return EInventorySaveFailure::InvalidCounter;
    }

    TSet<FName> ContainerIds;
    TSet<int32> InstanceIds;
    for (const FInventoryContainerSaveRecord& SavedContainer : Record.Containers)
    {
        if (SavedContainer.ContainerId.IsNone() || ContainerIds.Contains(SavedContainer.ContainerId))
        {
            return EInventorySaveFailure::InvalidContainerId;
        }
        ContainerIds.Add(SavedContainer.ContainerId);
        const FIntPoint Size = SavedContainer.GridSize;
        if (Size.X <= 0 || Size.Y <= 0 || static_cast<int64>(Size.X) * Size.Y > MAX_int32)
        {
            return EInventorySaveFailure::InvalidGridSize;
        }

        FInventoryNamedContainer& Named = Planned.AddDefaulted_GetRef();
        Named.ContainerId = SavedContainer.ContainerId;
        Named.Container = FInventoryContainer::MakeEmpty(Size);
        for (const FInventoryItemSaveRecord& SavedItem : SavedContainer.Items)
        {
            if (SavedItem.InstanceId < 0 || SavedItem.InstanceId == MAX_int32)
            {
                return EInventorySaveFailure::InvalidInstanceId;
            }
            if (InstanceIds.Contains(SavedItem.InstanceId))
            {
                return EInventorySaveFailure::DuplicateInstanceId;
            }
            InstanceIds.Add(SavedItem.InstanceId);
            if (SavedItem.InstanceId >= Record.NextInstanceId)
            {
                return EInventorySaveFailure::InvalidCounter;
            }

            FItemInstance Item;
            Item.InstanceId = SavedItem.InstanceId;
            Item.DefinitionTable = TSoftObjectPtr<UDataTable>(SavedItem.DefinitionTable);
            Item.DefinitionRowName = SavedItem.DefinitionRowName;
            Item.Quantity = SavedItem.Quantity;
            Item.AnchorCell = SavedItem.AnchorCell;
            Item.bRotated = SavedItem.bRotated;

            const UDataTable* Table = Item.DefinitionTable.LoadSynchronous();
            if (!Table || !Table->GetRowStruct() ||
                !Table->GetRowStruct()->IsChildOf(FItemDefinitionRow::StaticStruct()))
            {
                return EInventorySaveFailure::InvalidDefinition;
            }
            const FItemDefinitionRow* Definition = Table->FindRow<FItemDefinitionRow>(
                Item.DefinitionRowName, TEXT("InventorySave"), false);
            if (!Definition)
            {
                return EInventorySaveFailure::InvalidDefinition;
            }
            const int32 MaxQuantity = Definition->bStackable ? Definition->MaxStack : 1;
            if (Item.Quantity <= 0 || Item.Quantity > MaxQuantity)
            {
                return EInventorySaveFailure::InvalidQuantity;
            }

            FIntPoint Footprint = Definition->Size;
            if (Item.bRotated) { Swap(Footprint.X, Footprint.Y); }
            // 기존 배치 helper의 int32 덧셈 전에 overflow와 경계를 확인한다.
            if (Footprint.X <= 0 || Footprint.Y <= 0 || Item.AnchorCell.X < 0 || Item.AnchorCell.Y < 0 ||
                static_cast<int64>(Item.AnchorCell.X) + Footprint.X > Size.X ||
                static_cast<int64>(Item.AnchorCell.Y) + Footprint.Y > Size.Y)
            {
                return EInventorySaveFailure::InvalidPlacement;
            }
            if (FInventoryPlacement::TryPlace(Named.Container, Item) != EInventoryOperationFailure::None)
            {
                return EInventorySaveFailure::InvalidPlacement;
            }
        }
    }
    return EInventorySaveFailure::None;
}
}

EInventorySaveFailure FInventorySaveMapper::TrySave(
    const TArray<FInventoryNamedContainer>& Containers, FInventorySaveRecord& OutRecord)
{
    check(IsInGameThread());
    FInventorySaveRecord PlannedRecord;
    PlannedRecord.NextInstanceId = FItemInstanceIdAllocator::GetNextInstanceId();
    for (const FInventoryNamedContainer& Named : Containers)
    {
        FInventoryContainerSaveRecord& SavedContainer = PlannedRecord.Containers.AddDefaulted_GetRef();
        SavedContainer.ContainerId = Named.ContainerId;
        SavedContainer.GridSize = Named.Container.GridSize;
        for (const FItemInstance& Item : Named.Container.Items)
        {
            FInventoryItemSaveRecord& SavedItem = SavedContainer.Items.AddDefaulted_GetRef();
            SavedItem.InstanceId = Item.InstanceId;
            SavedItem.DefinitionTable = Item.DefinitionTable.ToSoftObjectPath();
            SavedItem.DefinitionRowName = Item.DefinitionRowName;
            SavedItem.Quantity = Item.Quantity;
            SavedItem.AnchorCell = Item.AnchorCell;
            SavedItem.bRotated = Item.bRotated;
        }
    }

    TArray<FInventoryNamedContainer> Validated;
    const EInventorySaveFailure Failure = BuildContainers(PlannedRecord, Validated);
    if (Failure != EInventorySaveFailure::None) { return Failure; }
    OutRecord = MoveTemp(PlannedRecord);
    return EInventorySaveFailure::None;
}

EInventorySaveFailure FInventorySaveMapper::TryLoad(
    const FInventorySaveRecord& Record, TArray<FInventoryNamedContainer>& OutContainers)
{
    check(IsInGameThread());
    TArray<FInventoryNamedContainer> Planned;
    const EInventorySaveFailure Failure = BuildContainers(Record, Planned);
    if (Failure != EInventorySaveFailure::None) { return Failure; }
    OutContainers = MoveTemp(Planned);
    FItemInstanceIdAllocator::AdvanceInstanceIdCounter(Record.NextInstanceId);
    return EInventorySaveFailure::None;
}
