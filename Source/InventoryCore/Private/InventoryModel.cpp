#include "InventoryModel.h"

#include "InventoryOperations.h"
#include "InventoryPlacement.h"
#include "InventoryPlacementInternal.h"
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

namespace InventoryBagEquipmentInternal
{
EInventoryOperationFailure GetBagDefinition(const FItemInstance& Item, const FItemDefinitionRow*& OutDefinition)
{
    const UDataTable* Table = Item.DefinitionTable.LoadSynchronous();
    if (!Table || !Table->GetRowStruct() ||
        !Table->GetRowStruct()->IsChildOf(FItemDefinitionRow::StaticStruct()))
    {
        return EInventoryOperationFailure::InvalidDefinition;
    }
    OutDefinition = Table->FindRow<FItemDefinitionRow>(Item.DefinitionRowName, TEXT("InventoryBagEquipment"), false);
    if (!OutDefinition || OutDefinition->Size.X <= 0 || OutDefinition->Size.Y <= 0)
    {
        return EInventoryOperationFailure::InvalidDefinition;
    }
    const FIntPoint Size = OutDefinition->BagGridSize;
    if (Size.X <= 0 || Size.Y <= 0 || static_cast<int64>(Size.X) * Size.Y > MAX_int32 ||
        OutDefinition->bStackable || OutDefinition->MaxStack != 1 || Item.Quantity != 1)
    {
        return EInventoryOperationFailure::InvalidCategory;
    }
    return EInventoryOperationFailure::None;
}

bool FitsBagSlot(const FInventoryContainer& Slot, const FItemInstance& Item, const FItemDefinitionRow& Definition)
{
    const FIntPoint Footprint = Item.bRotated ? FIntPoint(Definition.Size.Y, Definition.Size.X) : Definition.Size;
    return InventoryPlacementInternal::FitsInContainer(Slot, FIntPoint::ZeroValue, Footprint);
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
        if (BagBindingState != EBagBindingState::Unbound)
        {
            Change.bBagBindingChanged = BagBindingState == EBagBindingState::Bound;
            // 이름은 유지해 일반 연산 우회를 막고, callback 전에 재검증이 필요한 상태로 바꾼다.
            BagBindingState = EBagBindingState::NeedsValidation;
        }
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
    if (IsProtectedBagSlot(Source) || IsProtectedBagSlot(Target))
    {
        return EInventoryOperationFailure::InvalidContainer;
    }
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
    if (IsProtectedBagSlot(Source) || IsProtectedBagSlot(Target))
    {
        return EInventoryOperationFailure::InvalidContainer;
    }
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

EInventoryOperationFailure UInventoryModel::TryTransferAll(FName Source, FName Target)
{
    return Apply(Source, Target, [](FInventoryContainer& From, FInventoryContainer& To)
    {
        if (&From == &To) { return EInventoryOperationFailure::InvalidContainer; }
        if (From.Items.IsEmpty()) { return EInventoryOperationFailure::None; }

        FInventoryContainer PlannedTarget = To;
        for (FItemInstance Item : From.Items)
        {
            FIntPoint Footprint;
            if (!InventoryPlacementInternal::TryGetFootprint(Item, Footprint) ||
                Footprint.X <= 0 || Footprint.Y <= 0)
            {
                return EInventoryOperationFailure::NoSpace;
            }
            bool bPlaced = false;
            for (int32 Y = 0; Y <= To.GridSize.Y - Footprint.Y && !bPlaced; ++Y)
            {
                for (int32 X = 0; X <= To.GridSize.X - Footprint.X; ++X)
                {
                    Item.AnchorCell = FIntPoint(X, Y);
                    if (FInventoryPlacement::TryPlace(PlannedTarget, Item) == EInventoryOperationFailure::None)
                    {
                        bPlaced = true;
                        break;
                    }
                }
            }
            if (!bPlaced) { return EInventoryOperationFailure::NoSpace; }
        }

        FInventoryContainer PlannedSource = FInventoryContainer::MakeEmpty(From.GridSize);
        From = MoveTemp(PlannedSource);
        To = MoveTemp(PlannedTarget);
        return EInventoryOperationFailure::None;
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
    check(IsInGameThread());
    if (bApplying) { return EInventoryOperationFailure::OperationInProgress; }
    if (BagBindingState != EBagBindingState::Unbound && ContainerId == BagContentsContainerId)
    {
        return EInventoryOperationFailure::InvalidContainer;
    }
    return Apply(ContainerId, ContainerId, [Size](auto& From, auto&) { return FInventoryOperations::TryResize(From, Size); });
}

bool UInventoryModel::IsProtectedBagSlot(FName ContainerId) const
{
    return BagBindingState != EBagBindingState::Unbound && ContainerId == BagSlotContainerId;
}

bool UInventoryModel::IsBagSlotBound() const
{
    return BagBindingState == EBagBindingState::Bound;
}

const FItemInstance* UInventoryModel::GetEquippedBag() const
{
    if (!IsBagSlotBound()) { return nullptr; }
    const FInventoryContainer* Slot = FindContainer(BagSlotContainerId);
    return Slot && Slot->Items.Num() == 1 ? &Slot->Items[0] : nullptr;
}

EInventoryOperationFailure UInventoryModel::BindBagSlot(FName SlotId, FName BagId, FName ExchangeContainerId)
{
    check(IsInGameThread());
    if (bApplying) { return EInventoryOperationFailure::OperationInProgress; }
    if (SlotId.IsNone() || BagId.IsNone() || ExchangeContainerId.IsNone() ||
        SlotId == BagId || SlotId == ExchangeContainerId || BagId == ExchangeContainerId)
    {
        return EInventoryOperationFailure::InvalidContainer;
    }
    const FInventoryContainer* Slot = FindContainer(SlotId);
    const FInventoryContainer* Bag = FindContainer(BagId);
    if (!Slot || !Bag || !FindContainer(ExchangeContainerId) || Slot->Items.Num() != 1)
    {
        return EInventoryOperationFailure::InvalidContainer;
    }
    TGuardValue<bool> Guard(bApplying, true);
    const FItemDefinitionRow* Definition = nullptr;
    const EInventoryOperationFailure Failure = InventoryBagEquipmentInternal::GetBagDefinition(Slot->Items[0], Definition);
    if (Failure != EInventoryOperationFailure::None) { return Failure; }
    if (!InventoryBagEquipmentInternal::FitsBagSlot(*Slot, Slot->Items[0], *Definition))
    {
        return EInventoryOperationFailure::NoSpace;
    }
    if (Bag->GridSize != Definition->BagGridSize) { return EInventoryOperationFailure::InvalidContainer; }
    if (IsBagSlotBound() && BagSlotContainerId == SlotId && BagContentsContainerId == BagId &&
        BagExchangeContainerId == ExchangeContainerId)
    {
        return EInventoryOperationFailure::None;
    }

    BagSlotContainerId = SlotId;
    BagContentsContainerId = BagId;
    BagExchangeContainerId = ExchangeContainerId;
    BagBindingState = EBagBindingState::Bound;
    FInventoryChangeSet Change;
    Change.bBagBindingChanged = true;
    Changed.Broadcast(Change);
    return EInventoryOperationFailure::None;
}

EInventoryOperationFailure UInventoryModel::TryEquipBag(int32 InstanceId)
{
    check(IsInGameThread());
    if (bApplying) { return EInventoryOperationFailure::OperationInProgress; }
    if (!IsBagSlotBound()) { return EInventoryOperationFailure::InvalidContainer; }
    auto* Exchange = Containers.FindByPredicate([this](const auto& Entry)
        { return Entry.ContainerId == BagExchangeContainerId; });
    auto* Slot = Containers.FindByPredicate([this](const auto& Entry)
        { return Entry.ContainerId == BagSlotContainerId; });
    auto* Bag = Containers.FindByPredicate([this](const auto& Entry)
        { return Entry.ContainerId == BagContentsContainerId; });
    const int32 CandidateIndex = Exchange->Container.Items.IndexOfByPredicate(
        [InstanceId](const FItemInstance& Item) { return Item.InstanceId == InstanceId; });
    if (CandidateIndex == INDEX_NONE) { return EInventoryOperationFailure::ItemNotFound; }
    TGuardValue<bool> Guard(bApplying, true);
    FItemInstance Candidate = Exchange->Container.Items[CandidateIndex];
    const FItemDefinitionRow* Definition = nullptr;
    const EInventoryOperationFailure Failure = InventoryBagEquipmentInternal::GetBagDefinition(Candidate, Definition);
    if (Failure != EInventoryOperationFailure::None) { return Failure; }
    if (!InventoryBagEquipmentInternal::FitsBagSlot(Slot->Container, Candidate, *Definition))
    {
        return EInventoryOperationFailure::NoSpace;
    }

    FInventoryContainer PlannedExchange = Exchange->Container;
    FInventoryContainer PlannedSlot = FInventoryContainer::MakeEmpty(Slot->Container.GridSize);
    FInventoryContainer PlannedBag = Bag->Container;
    PlannedExchange.Items.RemoveAt(CandidateIndex);
    FInventoryPlacement::RebuildOccupancyCache(PlannedExchange);
    FItemInstance Previous = Slot->Container.Items[0];
    FIntPoint PreviousFootprint;
    if (!InventoryPlacementInternal::TryGetFootprint(Previous, PreviousFootprint) ||
        PreviousFootprint.X <= 0 || PreviousFootprint.Y <= 0)
    {
        return EInventoryOperationFailure::InvalidDefinition;
    }
    bool bReturned = false;
    for (int32 Y = 0; Y <= PlannedExchange.GridSize.Y - PreviousFootprint.Y && !bReturned; ++Y)
    {
        for (int32 X = 0; X <= PlannedExchange.GridSize.X - PreviousFootprint.X; ++X)
        {
            Previous.AnchorCell = FIntPoint(X, Y);
            if (FInventoryPlacement::TryPlace(PlannedExchange, Previous) == EInventoryOperationFailure::None)
            {
                bReturned = true;
                break;
            }
        }
    }
    if (!bReturned) { return EInventoryOperationFailure::NoSpace; }
    // slot 안의 위치만 원점으로 정규화한다. ID·정의·수량·회전은 동일 항목의 값이다.
    Candidate.AnchorCell = FIntPoint::ZeroValue;
    const EInventoryOperationFailure SlotFailure = FInventoryPlacement::TryPlace(PlannedSlot, Candidate);
    if (SlotFailure != EInventoryOperationFailure::None) { return SlotFailure; }
    const EInventoryOperationFailure ResizeFailure = FInventoryOperations::TryResize(PlannedBag, Definition->BagGridSize);
    if (ResizeFailure != EInventoryOperationFailure::None) { return ResizeFailure; }

    FInventoryChangeSet Change;
    Change.Containers.Add(Compare(BagExchangeContainerId, Exchange->Container, PlannedExchange));
    Change.Containers.Add(Compare(BagSlotContainerId, Slot->Container, PlannedSlot));
    FInventoryContainerChange BagChange = Compare(BagContentsContainerId, Bag->Container, PlannedBag);
    if (BagChange.bGridSizeChanged) { Change.Containers.Add(MoveTemp(BagChange)); }
    Exchange->Container = MoveTemp(PlannedExchange);
    Slot->Container = MoveTemp(PlannedSlot);
    Bag->Container = MoveTemp(PlannedBag);
    Changed.Broadcast(Change);
    return EInventoryOperationFailure::None;
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
