#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "ItemDefinitionRow.generated.h"

USTRUCT()
struct FItemDefinitionRow : public FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY()
    FIntPoint Size = FIntPoint(1, 1);
};
