#include "ItemViewModel.h"

#include "InventoryModel.h"
#include "ItemDefinitionRow.h"

void UItemViewModel::Bind(UInventoryModel* InModel, FName InContainerId, int32 InInstanceId)
{
    Model = InModel;
    ContainerId = InContainerId;
    InstanceId = InInstanceId;
    NotifyChanged();
}

const FItemInstance* UItemViewModel::FindItem() const
{
    return Model.IsValid() ? Model->FindItem(ContainerId, InstanceId) : nullptr;
}

bool UItemViewModel::IsAvailable() const { return FindItem() != nullptr; }
FName UItemViewModel::GetDefinitionRowName() const
{
    const auto* Item = FindItem();
    return Item ? Item->DefinitionRowName : NAME_None;
}
int32 UItemViewModel::GetQuantity() const
{
    const auto* Item = FindItem();
    return Item ? Item->Quantity : 0;
}
FIntPoint UItemViewModel::GetAnchorCell() const
{
    const auto* Item = FindItem();
    return Item ? Item->AnchorCell : FIntPoint::ZeroValue;
}
bool UItemViewModel::IsRotated() const
{
    const auto* Item = FindItem();
    return Item && Item->bRotated;
}
FIntPoint UItemViewModel::GetFootprint() const
{
    const auto* Item = FindItem();
    if (!Item) { return FIntPoint::ZeroValue; }
    const UDataTable* Table = Item->DefinitionTable.LoadSynchronous();
    const FItemDefinitionRow* Definition = Table ? Table->FindRow<FItemDefinitionRow>(
        Item->DefinitionRowName, TEXT("ItemViewModel"), false) : nullptr;
    if (!Definition) { return FIntPoint::ZeroValue; }
    FIntPoint Size = Definition->Size;
    if (Item->bRotated) { Swap(Size.X, Size.Y); }
    return Size;
}

void UItemViewModel::NotifyChanged()
{
    UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(IsAvailable);
    UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(GetInstanceId);
    UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(GetDefinitionRowName);
    UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(GetQuantity);
    UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(GetAnchorCell);
    UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(IsRotated);
    UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(GetFootprint);
}
