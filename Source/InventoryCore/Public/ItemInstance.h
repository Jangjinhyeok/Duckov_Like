#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "ItemInstance.generated.h"

USTRUCT()
struct INVENTORYCORE_API FItemInstance
{
    GENERATED_BODY()

    UPROPERTY()
    int32 InstanceId = INDEX_NONE;

    UPROPERTY()
    FName DefinitionRowName = NAME_None;

    UPROPERTY()
    TSoftObjectPtr<UDataTable> DefinitionTable;

    UPROPERTY()
    int32 Quantity = 1;

    UPROPERTY()
    FIntPoint AnchorCell = FIntPoint::ZeroValue;

    UPROPERTY()
    bool bRotated = false;
};

struct INVENTORYCORE_API FItemInstanceIdAllocator
{
    static int32 AllocateNextInstanceId();
    static void ResetInstanceIdCounter_ForTests(int32 StartValue = 0);
};
