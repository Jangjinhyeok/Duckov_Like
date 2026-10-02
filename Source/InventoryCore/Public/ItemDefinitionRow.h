#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "ItemDefinitionRow.generated.h"

USTRUCT()
struct FItemDefinitionRow : public FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere)
    FIntPoint Size = FIntPoint(1, 1);

    UPROPERTY(EditAnywhere)
    bool bStackable = false;

    UPROPERTY(EditAnywhere)
    int32 MaxStack = 1;

    // 양수 크기를 가진 non-stackable 항목만 가방 slot에 장착할 수 있다.
    UPROPERTY(EditAnywhere)
    FIntPoint BagGridSize = FIntPoint::ZeroValue;
};
