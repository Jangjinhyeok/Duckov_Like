#pragma once

#include "CoreMinimal.h"
#include "InventoryContainer.h"
#include "InventorySaveRecord.generated.h"

USTRUCT()
struct INVENTORYCORE_API FInventoryItemSaveRecord
{
    GENERATED_BODY()

    UPROPERTY()
    int32 InstanceId = INDEX_NONE;

    UPROPERTY()
    FSoftObjectPath DefinitionTable;

    UPROPERTY()
    FName DefinitionRowName = NAME_None;

    UPROPERTY()
    int32 Quantity = 1;

    UPROPERTY()
    FIntPoint AnchorCell = FIntPoint::ZeroValue;

    UPROPERTY()
    bool bRotated = false;
};

USTRUCT()
struct INVENTORYCORE_API FInventoryContainerSaveRecord
{
    GENERATED_BODY()

    UPROPERTY()
    FName ContainerId = NAME_None;

    UPROPERTY()
    FIntPoint GridSize = FIntPoint(1, 1);

    UPROPERTY()
    TArray<FInventoryItemSaveRecord> Items;
};

USTRUCT()
struct INVENTORYCORE_API FInventorySaveRecord
{
    GENERATED_BODY()

    static constexpr int32 CurrentFormatVersion = 1;

    UPROPERTY()
    int32 FormatVersion = CurrentFormatVersion;

    UPROPERTY()
    int32 NextInstanceId = 0;

    UPROPERTY()
    TArray<FInventoryContainerSaveRecord> Containers;
};

// 전체 세션을 저장·복원할 때 호출자가 부여한 이름과 Model을 묶는다.
USTRUCT()
struct INVENTORYCORE_API FInventoryNamedContainer
{
    GENERATED_BODY()

    UPROPERTY()
    FName ContainerId = NAME_None;

    UPROPERTY()
    FInventoryContainer Container;
};

enum class EInventorySaveFailure : uint8
{
    None,
    UnsupportedVersion,
    InvalidContainerId,
    InvalidGridSize,
    InvalidInstanceId,
    DuplicateInstanceId,
    InvalidCounter,
    InvalidDefinition,
    InvalidQuantity,
    InvalidPlacement,
    OperationInProgress
};

struct INVENTORYCORE_API FInventorySaveMapper
{
    // game thread 동기 연산. 실패하면 출력과 ID 카운터를 보존한다.
    static EInventorySaveFailure TrySave(
        const TArray<FInventoryNamedContainer>& Containers, FInventorySaveRecord& OutRecord);

    // 부분 병합이 아닌 전체 세션 교체용. 이전 집합을 동시에 활성 상태로 유지하지 않는다.
    static EInventorySaveFailure TryLoad(
        const FInventorySaveRecord& Record, TArray<FInventoryNamedContainer>& OutContainers);
};
