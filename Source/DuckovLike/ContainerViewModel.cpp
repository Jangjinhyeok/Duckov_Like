#include "ContainerViewModel.h"

#include "InventoryModel.h"
#include "ItemViewModel.h"
#include "Misc/ScopeExit.h"

void UContainerViewModel::Unsubscribe()
{
    if (Model) { Model->OnChanged().Remove(ChangedHandle); }
    ChangedHandle.Reset();
}

void UContainerViewModel::Bind(UInventoryModel* InModel, FName InContainerId)
{
    if (Model == InModel && ContainerId == InContainerId && !bHasPendingBinding) { return; }
    // FieldNotify에서 화면이 닫혀도 현재 목록 순회를 끝낸 뒤 재바인딩한다.
    if (bRefreshing)
    {
        PendingModel = InModel;
        PendingContainerId = InContainerId;
        bHasPendingBinding = true;
        return;
    }
    ON_SCOPE_EXIT { ApplyPendingBinding(); };
    TGuardValue<bool> Guard(bRefreshing, true);
    Unsubscribe();
    for (UItemViewModel* Item : Items) { Item->Bind(nullptr, NAME_None, INDEX_NONE); }
    Items.Reset();
    Model = InModel;
    ContainerId = InContainerId;
    if (Model) { ChangedHandle = Model->OnChanged().AddUObject(this, &ThisClass::OnModelChanged); }
    Refresh(nullptr);
}

void UContainerViewModel::ApplyPendingBinding()
{
    if (bRefreshing || !bHasPendingBinding) { return; }
    UInventoryModel* NextModel = PendingModel;
    const FName NextId = PendingContainerId;
    bHasPendingBinding = false;
    Bind(NextModel, NextId);
    PendingModel = nullptr;
}

FIntPoint UContainerViewModel::GetGridSize() const
{
    const auto* Container = Model ? Model->FindContainer(ContainerId) : nullptr;
    return Container ? Container->GridSize : FIntPoint::ZeroValue;
}

bool UContainerViewModel::IsAvailable() const { return Model && Model->FindContainer(ContainerId); }

void UContainerViewModel::Refresh(const FInventoryContainerChange* Change)
{
    ON_SCOPE_EXIT { ApplyPendingBinding(); };
    TGuardValue<bool> Guard(bRefreshing, true);
    TMap<int32, UItemViewModel*> Existing;
    for (UItemViewModel* Item : Items) { Existing.Add(Item->GetInstanceId(), Item); }
    TArray<TObjectPtr<UItemViewModel>> Next;
    const auto* Container = Model ? Model->FindContainer(ContainerId) : nullptr;
    if (Container)
    {
        for (const FItemInstance& Item : Container->Items)
        {
            UItemViewModel* VM = Existing.FindRef(Item.InstanceId);
            if (VM) { Existing.Remove(Item.InstanceId); }
            else
            {
                VM = NewObject<UItemViewModel>(this);
                VM->Bind(Model, ContainerId, Item.InstanceId);
            }
            Next.Add(VM);
        }
    }
    const bool bListChanged = Items != Next;
    Items = MoveTemp(Next);
    for (const auto& Entry : Existing) { Entry.Value->Bind(nullptr, NAME_None, INDEX_NONE); }
    for (UItemViewModel* Item : Items)
    {
        if (!Change || Change->Updated.Contains(Item->GetInstanceId())) { Item->NotifyChanged(); }
    }
    // 초기 연결과 전체 reset은 빈 목록일 때에도 통지한다.
    if (bListChanged || !Change) { UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(Items); }
    if (!Change || Change->bGridSizeChanged) { UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(GetGridSize); }
    if (!Change) { UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(IsAvailable); }
}

void UContainerViewModel::OnModelChanged(const FInventoryChangeSet& Change)
{
    if (Change.bReset) { Refresh(nullptr); return; }
    const auto* Entry = Change.Containers.FindByPredicate(
        [this](const auto& Delta) { return Delta.ContainerId == ContainerId; });
    if (Entry) { Refresh(Entry); }
}

void UContainerViewModel::BeginDestroy()
{
    Unsubscribe();
    Super::BeginDestroy();
}
