#pragma once

#include "CoreMinimal.h"
#include "InventoryOperationTypes.h"
#include "InventorySaveRecord.h"
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
    EInventoryOperationFailure TryStack(FName Source, FName Target, int32 SourceId, int32 TargetId);
    EInventoryOperationFailure TrySplit(FName ContainerId, int32 InstanceId, int32 Quantity);
    EInventoryOperationFailure TrySort(FName ContainerId);
    EInventoryOperationFailure TryResize(FName ContainerId, FIntPoint Size);
    EInventoryOperationFailure TryAdd(FName ContainerId, TSoftObjectPtr<UDataTable> DefinitionTable,
        FName DefinitionRowName, int32 Quantity);

private:
    // 조회 포인터는 호출 중에만 유효하다. 변경 이후 보관하거나 mutable로 노출하지 않는다.
    UPROPERTY()
    TArray<FInventoryNamedContainer> Containers;

    FOnInventoryChanged Changed;
    bool bApplying = false;

    EInventoryOperationFailure Apply(FName Source, FName Target,
        TFunctionRef<EInventoryOperationFailure(FInventoryContainer&, FInventoryContainer&)> Operation);
};
