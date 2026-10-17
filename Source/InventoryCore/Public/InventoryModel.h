#pragma once

#include "CoreMinimal.h"
#include "InventoryOperationTypes.h"
#include "InventorySaveRecord.h"
#include "ItemDefinitionRow.h"
#include "InventoryModel.generated.h"

struct FInventoryContainerChange
{
    FName ContainerId;
    TArray<int32> Added;
    TArray<int32> Removed;
    TArray<int32> Updated;
    bool bGridSizeChanged = false;
    bool bOrderChanged = false;
};

struct FInventoryChangeSet
{
    bool bReset = false;
    bool bBagBindingChanged = false;
    TArray<FInventoryContainerChange> Containers;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FOnInventoryChanged, const FInventoryChangeSet&);

// Actor와 독립적인 세션 Model 소유자. 스태시/레이드의 게임 정책은 여기서 정하지 않는다.
UCLASS(BlueprintType)
class INVENTORYCORE_API UInventoryModel : public UObject
{
    GENERATED_BODY()

public:
    EInventorySaveFailure Load(const FInventorySaveRecord& Record);
    EInventorySaveFailure Save(FInventorySaveRecord& OutRecord) const;
    const FInventoryContainer* FindContainer(FName ContainerId) const;
    const FItemInstance* FindItem(FName ContainerId, int32 InstanceId) const;
    FOnInventoryChanged& OnChanged() { return Changed; }

    EInventoryOperationFailure CanMove(FName Source, FName Target, int32 InstanceId,
        FIntPoint Anchor, bool bRotated) const;
    EInventoryOperationFailure TryMove(FName Source, FName Target, int32 InstanceId,
        FIntPoint Anchor, bool bRotated);
    // 현재 회전으로 전체 항목을 원자적으로 이전한다. 같은 컨테이너는 InvalidContainer다.
    EInventoryOperationFailure TryTransferAll(FName Source, FName Target);
    EInventoryOperationFailure CanStack(FName Source, FName Target, int32 SourceId, int32 TargetId) const;
    // 드롭 위치가 다른 항목 하나와만 겹치면 그 InstanceId, 아니면 INDEX_NONE. 병합 여부는 호출자가 정한다.
    int32 FindSingleOverlap(FName Source, FName Target, int32 InstanceId, FIntPoint Anchor, bool bRotated) const;
    EInventoryOperationFailure TryStack(FName Source, FName Target, int32 SourceId, int32 TargetId);
    EInventoryOperationFailure TrySplit(FName ContainerId, int32 InstanceId, int32 Quantity);
    EInventoryOperationFailure TrySort(FName ContainerId);
    EInventoryOperationFailure TryResize(FName ContainerId, FIntPoint Size);
    EInventoryOperationFailure TryAdd(FName ContainerId, TSoftObjectPtr<UDataTable> DefinitionTable,
        FName DefinitionRowName, int32 Quantity);

    // 저장된 slot 항목과 contents 크기를 검증한다. binding은 save v1에 추가하지 않는다.
    EInventoryOperationFailure BindBagSlot(FName SlotId, FName BagId, FName ExchangeContainerId);
    EInventoryOperationFailure TryEquipBag(int32 InstanceId);
    const FItemInstance* GetEquippedBag() const;
    bool IsBagSlotBound() const;
    FName GetBagSlotContainerId() const { return BagSlotContainerId; }
    FName GetBagExchangeContainerId() const { return BagExchangeContainerId; }
    FName GetBagContentsContainerId() const { return BagContentsContainerId; }

    // 장비 slot을 등록한다. 비어 있거나 그 종류 항목 하나만 원점·무회전으로 있어야 한다.
    // 등록 뒤 slot에는 이동만 허용하며 binding은 save v1에 추가하지 않는다. Load 뒤에는 다시 호출해야 한다.
    EInventoryOperationFailure BindEquipmentSlot(FName SlotId, EItemEquipmentType Type);
    // 등록된 slot이면 Load 뒤 재검증 전이라도 true다.
    bool IsEquipmentSlot(FName ContainerId) const { return FindEquipmentSlot(ContainerId) != nullptr; }
    bool IsEquipmentSlotBound(FName ContainerId) const;

private:
    // 조회 포인터는 호출 중에만 유효하다. 변경 이후 보관하거나 mutable로 노출하지 않는다.
    UPROPERTY()
    TArray<FInventoryNamedContainer> Containers;

    FOnInventoryChanged Changed;
    bool bApplying = false;

    enum class EBagBindingState : uint8 { Unbound, Bound, NeedsValidation };
    EBagBindingState BagBindingState = EBagBindingState::Unbound;
    FName BagSlotContainerId = NAME_None;
    FName BagExchangeContainerId = NAME_None;
    FName BagContentsContainerId = NAME_None;

    struct FEquipmentSlotBinding
    {
        FName SlotId;
        EItemEquipmentType Type = EItemEquipmentType::None;
        bool bValidated = false;
    };
    TArray<FEquipmentSlotBinding> EquipmentSlots;

    bool IsProtectedBagSlot(FName ContainerId) const;
    const FEquipmentSlotBinding* FindEquipmentSlot(FName ContainerId) const;
    // slot이 낀 이동을 검사하고, 대상이 slot이면 놓을 위치를 원점·무회전으로 바꾼다.
    EInventoryOperationFailure CheckEquipmentMove(FName Source, FName Target, int32 InstanceId,
        FIntPoint& InOutAnchor, bool& bInOutRotated) const;

    // bEquipmentMove가 아니면 장비 slot이 낀 연산을 거부한다.
    EInventoryOperationFailure Apply(FName Source, FName Target,
        TFunctionRef<EInventoryOperationFailure(FInventoryContainer&, FInventoryContainer&)> Operation,
        bool bEquipmentMove = false);
};
