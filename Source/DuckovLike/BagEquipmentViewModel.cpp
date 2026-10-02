#include "BagEquipmentViewModel.h"

#include "InventoryModel.h"
#include "ItemDefinitionRow.h"
#include "Misc/ScopeExit.h"

void UBagEquipmentViewModel::Unsubscribe()
{
    if (Model) { Model->OnChanged().Remove(ChangedHandle); }
    ChangedHandle.Reset();
}

void UBagEquipmentViewModel::Bind(UInventoryModel* InModel)
{
    if (Model == InModel && !bHasPendingBinding) { return; }
    // FieldNotify 중 화면이 닫혀도 현재 통지를 끝낸 뒤 구독을 정리한다.
    if (bNotifying) { PendingModel = InModel; bHasPendingBinding = true; return; }
    Unsubscribe();
    Model = InModel;
    if (Model) { ChangedHandle = Model->OnChanged().AddUObject(this, &ThisClass::OnModelChanged); }
    NotifyState();
}

void UBagEquipmentViewModel::ApplyPendingBinding()
{
    if (bNotifying || !bHasPendingBinding) { return; }
    UInventoryModel* Next = PendingModel;
    bHasPendingBinding = false;
    PendingModel = nullptr;
    Bind(Next);
}

bool UBagEquipmentViewModel::HasBinding() const { return Model && Model->IsBagSlotBound(); }

FText UBagEquipmentViewModel::GetEquippedText() const
{
    const FItemInstance* Item = Model ? Model->GetEquippedBag() : nullptr;
    const FInventoryContainer* Bag = HasBinding() ? Model->FindContainer(Model->GetBagContentsContainerId()) : nullptr;
    if (!Item || !Bag) { return FText::GetEmpty(); }
    const FText Name = Item->DefinitionRowName == TEXT("SmallBag")
        ? NSLOCTEXT("Inventory", "SmallBag", "소형")
        : Item->DefinitionRowName == TEXT("LargeBag") ? NSLOCTEXT("Inventory", "LargeBag", "대형")
        : NSLOCTEXT("Inventory", "EquippedBag", "장착 가방");
    return FText::Format(NSLOCTEXT("Inventory", "BagSlot", "가방 슬롯: {0} {1}x{2}"),
        Name, FText::AsNumber(Bag->GridSize.X), FText::AsNumber(Bag->GridSize.Y));
}

int32 UBagEquipmentViewModel::FindSourceBag(FName RowName) const
{
    if (!HasBinding() || Model->GetBagExchangeContainerId() != TEXT("Stash")) { return INDEX_NONE; }
    const FInventoryContainer* Source = Model->FindContainer(TEXT("Stash"));
    const FItemInstance* Item = Source ? Source->Items.FindByPredicate(
        [RowName](const FItemInstance& Candidate) { return Candidate.DefinitionRowName == RowName; }) : nullptr;
    return Item ? Item->InstanceId : INDEX_NONE;
}

int32 UBagEquipmentViewModel::GetSmallBagInstanceId() const { return FindSourceBag(TEXT("SmallBag")); }
int32 UBagEquipmentViewModel::GetLargeBagInstanceId() const { return FindSourceBag(TEXT("LargeBag")); }

void UBagEquipmentViewModel::OnModelChanged(const FInventoryChangeSet& Change)
{
    if (Change.bReset || Change.bBagBindingChanged || Change.Containers.ContainsByPredicate([this](const auto& Entry)
        { return Entry.ContainerId == Model->GetBagSlotContainerId()
            || Entry.ContainerId == Model->GetBagExchangeContainerId()
            || Entry.ContainerId == Model->GetBagContentsContainerId(); }))
    {
        NotifyState();
    }
}

void UBagEquipmentViewModel::NotifyState()
{
    ON_SCOPE_EXIT { ApplyPendingBinding(); };
    TGuardValue<bool> Guard(bNotifying, true);
    UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(HasBinding);
    UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(GetSmallBagInstanceId);
    UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(GetLargeBagInstanceId);
    UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(GetEquippedText);
}

void UBagEquipmentViewModel::BeginDestroy()
{
    Unsubscribe();
    Super::BeginDestroy();
}
