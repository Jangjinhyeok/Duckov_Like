#include "InventoryModel.h"

#include "InventoryOperations.h"
#include "InventoryPlacement.h"
#include "ItemDefinitionRow.h"

namespace
{
FInventoryContainerChange Compare(FName Id, const FInventoryContainer& Before, const FInventoryContainer& After)
{
    FInventoryContainerChange Change;
    Change.ContainerId = Id;
    Change.bGridSizeChanged = Before.GridSize != After.GridSize;
    TMap<int32, const FItemInstance*> OldItems;
    TSet<int32> CurrentIds;
    for (const FItemInstance& Item : Before.Items) { OldItems.Add(Item.InstanceId, &Item); }
    for (int32 Index = 0; Index < After.Items.Num(); ++Index)
    {
        const FItemInstance& Item = After.Items[Index];
        CurrentIds.Add(Item.InstanceId);
        const FItemInstance* const* Found = OldItems.Find(Item.InstanceId);
        if (!Found) { Change.Added.Add(Item.InstanceId); }
        else
        {
            const FItemInstance& Old = **Found;
            if (Old.AnchorCell != Item.AnchorCell || Old.bRotated != Item.bRotated ||
                Old.Quantity != Item.Quantity || Old.DefinitionTable != Item.DefinitionTable ||
                Old.DefinitionRowName != Item.DefinitionRowName)
            {
                Change.Updated.Add(Item.InstanceId);
            }
        }
        if (!Before.Items.IsValidIndex(Index) || Before.Items[Index].InstanceId != Item.InstanceId)
        {
            Change.bOrderChanged = true;
        }
    }
    for (const FItemInstance& Item : Before.Items)
    {
        if (!CurrentIds.Contains(Item.InstanceId)) { Change.Removed.Add(Item.InstanceId); }
    }
    Change.bOrderChanged |= Before.Items.Num() != After.Items.Num();
    return Change;
}
}

EInventorySaveFailure UInventoryModel::Load(const FInventorySaveRecord& Record)
{
    check(IsInGameThread());
    if (bApplying) { return EInventorySaveFailure::OperationInProgress; }
    TGuardValue<bool> Guard(bApplying, true);
    const EInventorySaveFailure Result = FInventorySaveMapper::TryLoad(Record, Containers);
    if (Result == EInventorySaveFailure::None)
    {
        FInventoryChangeSet Change;
        Change.bReset = true;
        Changed.Broadcast(Change);
    }
    return Result;
}

EInventorySaveFailure UInventoryModel::Save(FInventorySaveRecord& OutRecord) const
{
    return FInventorySaveMapper::TrySave(Containers, OutRecord);
}

const FInventoryContainer* UInventoryModel::FindContainer(FName ContainerId) const
{
    const auto* Named = Containers.FindByPredicate(
        [ContainerId](const auto& Entry) { return Entry.ContainerId == ContainerId; });
    return Named ? &Named->Container : nullptr;
}

const FItemInstance* UInventoryModel::FindItem(FName ContainerId, int32 InstanceId) const
{
    const FInventoryContainer* Container = FindContainer(ContainerId);
    return Container ? Container->Items.FindByPredicate(
        [InstanceId](const auto& Item) { return Item.InstanceId == InstanceId; }) : nullptr;
}

EInventoryOperationFailure UInventoryModel::CanMove(FName Source, FName Target, int32 InstanceId,
    FIntPoint Anchor, bool bRotated) const
{
    const auto* From = FindContainer(Source);
    const auto* To = FindContainer(Target);
    if (!From || !To) { return EInventoryOperationFailure::InvalidContainer; }
    return FInventoryOperations::CanMove(*From, *To, InstanceId, Anchor, bRotated);
}

EInventoryOperationFailure UInventoryModel::Apply(FName Source, FName Target,
    TFunctionRef<EInventoryOperationFailure(FInventoryContainer&, FInventoryContainer&)> Operation)
{
    check(IsInGameThread());
    if (bApplying) { return EInventoryOperationFailure::OperationInProgress; }
    auto* From = Containers.FindByPredicate([Source](const auto& Entry) { return Entry.ContainerId == Source; });
    auto* To = Containers.FindByPredicate([Target](const auto& Entry) { return Entry.ContainerId == Target; });
    if (!From || !To) { return EInventoryOperationFailure::InvalidContainer; }
    TGuardValue<bool> Guard(bApplying, true);
    const FInventoryContainer BeforeSource = From->Container;
    const FInventoryContainer BeforeTarget = From == To ? FInventoryContainer{} : To->Container;
    const EInventoryOperationFailure Result = Operation(From->Container, To->Container);
    if (Result != EInventoryOperationFailure::None) { return Result; }

    FInventoryChangeSet Change;
    auto AddChange = [&Change](FInventoryContainerChange Entry)
    {
        if (Entry.bGridSizeChanged || Entry.bOrderChanged || !Entry.Added.IsEmpty() ||
            !Entry.Removed.IsEmpty() || !Entry.Updated.IsEmpty())
        {
            Change.Containers.Add(MoveTemp(Entry));
        }
    };
    AddChange(Compare(Source, BeforeSource, From->Container));
    if (From != To) { AddChange(Compare(Target, BeforeTarget, To->Container)); }
    if (!Change.Containers.IsEmpty()) { Changed.Broadcast(Change); }
    return Result;
}

EInventoryOperationFailure UInventoryModel::TryMove(FName Source, FName Target, int32 InstanceId,
    FIntPoint Anchor, bool bRotated)
{
    return Apply(Source, Target, [=](auto& From, auto& To)
    {
        return FInventoryOperations::TryMove(From, To, InstanceId, Anchor, bRotated);
    });
}

EInventoryOperationFailure UInventoryModel::TryStack(FName Source, FName Target, int32 SourceId, int32 TargetId)
{
    return Apply(Source, Target, [=](auto& From, auto& To)
    {
        return FInventoryOperations::TryStack(From, To, SourceId, TargetId);
    });
}

EInventoryOperationFailure UInventoryModel::TrySplit(FName ContainerId, int32 InstanceId, int32 Quantity)
{
    return Apply(ContainerId, ContainerId, [=](FInventoryContainer& Container, FInventoryContainer&)
    {
        const int32 SourceIndex = Container.Items.IndexOfByPredicate(
            [InstanceId](const FItemInstance& Item) { return Item.InstanceId == InstanceId; });
        if (SourceIndex == INDEX_NONE) { return EInventoryOperationFailure::ItemNotFound; }

        const FItemInstance& Source = Container.Items[SourceIndex];
        const UDataTable* Table = Source.DefinitionTable.LoadSynchronous();
        if (!Table || !Table->GetRowStruct() ||
            !Table->GetRowStruct()->IsChildOf(FItemDefinitionRow::StaticStruct()))
        {
            return EInventoryOperationFailure::InvalidDefinition;
        }
        const FItemDefinitionRow* Definition = Table->FindRow<FItemDefinitionRow>(
            Source.DefinitionRowName, TEXT("InventorySplit"), false);
        if (!Definition || Definition->Size.X <= 0 || Definition->Size.Y <= 0 ||
            Definition->MaxStack <= 0)
        {
            return EInventoryOperationFailure::InvalidDefinition;
        }
        if (!Definition->bStackable) { return EInventoryOperationFailure::StackMismatch; }
        if (Source.Quantity <= 0 || Source.Quantity > Definition->MaxStack ||
            Quantity <= 0 || Quantity >= Source.Quantity)
        {
            return EInventoryOperationFailure::InvalidQuantity;
        }

        FItemInstance Split = Source;
        Split.Quantity = Quantity;
        const FIntPoint Footprint = Source.bRotated
            ? FIntPoint(Definition->Size.Y, Definition->Size.X) : Definition->Size;
        FInventoryContainer Planned = Container;
        for (int32 Y = 0; Y <= Container.GridSize.Y - Footprint.Y; ++Y)
        {
            for (int32 X = 0; X <= Container.GridSize.X - Footprint.X; ++X)
            {
                Split.AnchorCell = FIntPoint(X, Y);
                if (FInventoryPlacement::TryPlace(Planned, Split) != EInventoryOperationFailure::None)
                {
                    continue;
                }
                const int32 NewId = FItemInstanceIdAllocator::AllocateNextInstanceId();
                if (NewId == INDEX_NONE)
                {
                    return EInventoryOperationFailure::InstanceIdExhausted;
                }
                Planned.Items[SourceIndex].Quantity -= Quantity;
                Planned.Items.Last().InstanceId = NewId;
                Container = MoveTemp(Planned);
                return EInventoryOperationFailure::None;
            }
        }
        return EInventoryOperationFailure::NoSpace;
    });
}

EInventoryOperationFailure UInventoryModel::TrySort(FName ContainerId)
{
    return Apply(ContainerId, ContainerId, [](auto& From, auto&) { return FInventoryOperations::TrySort(From); });
}

EInventoryOperationFailure UInventoryModel::TryResize(FName ContainerId, FIntPoint Size)
{
    return Apply(ContainerId, ContainerId, [Size](auto& From, auto&) { return FInventoryOperations::TryResize(From, Size); });
}

EInventoryOperationFailure UInventoryModel::TryAdd(FName ContainerId, TSoftObjectPtr<UDataTable> DefinitionTable,
    FName DefinitionRowName, int32 Quantity)
{
    return Apply(ContainerId, ContainerId, [=](FInventoryContainer& Container, FInventoryContainer&)
    {
        const UDataTable* Table = DefinitionTable.LoadSynchronous();
        if (!Table || !Table->GetRowStruct() ||
            !Table->GetRowStruct()->IsChildOf(FItemDefinitionRow::StaticStruct()))
        {
            return EInventoryOperationFailure::InvalidDefinition;
        }
        const FItemDefinitionRow* Definition = Table->FindRow<FItemDefinitionRow>(
            DefinitionRowName, TEXT("InventoryAdd"), false);
        if (!Definition || Definition->Size.X <= 0 || Definition->Size.Y <= 0)
        {
            return EInventoryOperationFailure::InvalidDefinition;
        }
        const int32 MaxQuantity = Definition->bStackable ? Definition->MaxStack : 1;
        if (Quantity <= 0 || Quantity > MaxQuantity)
        {
            return EInventoryOperationFailure::InvalidQuantity;
        }

        FItemInstance Item;
        Item.DefinitionTable = DefinitionTable;
        Item.DefinitionRowName = DefinitionRowName;
        Item.Quantity = Quantity;
        FInventoryContainer Planned = Container;
        for (int32 Y = 0; Y <= Container.GridSize.Y - Definition->Size.Y; ++Y)
        {
            for (int32 X = 0; X <= Container.GridSize.X - Definition->Size.X; ++X)
            {
                Item.AnchorCell = FIntPoint(X, Y);
                if (FInventoryPlacement::TryPlace(Planned, Item) != EInventoryOperationFailure::None)
                {
                    continue;
                }
                const int32 NewId = FItemInstanceIdAllocator::AllocateNextInstanceId();
                if (NewId == INDEX_NONE)
                {
                    return EInventoryOperationFailure::InstanceIdExhausted;
                }
                Planned.Items.Last().InstanceId = NewId;
                Container = MoveTemp(Planned);
                return EInventoryOperationFailure::None;
            }
        }
        return EInventoryOperationFailure::NoSpace;
    });
}
