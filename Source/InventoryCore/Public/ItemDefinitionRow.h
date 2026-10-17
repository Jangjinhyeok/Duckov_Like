#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "ItemDefinitionRow.generated.h"

// 장비 slot이 받는 아이템 종류. 가방은 BagGridSize로 따로 정한다.
UENUM()
enum class EItemEquipmentType : uint8
{
    None,
    Weapon,
    Armor,
};

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

    // None이 아닌 non-stackable 항목만 같은 종류의 장비 slot에 장착할 수 있다.
    UPROPERTY(EditAnywhere)
    EItemEquipmentType EquipmentType = EItemEquipmentType::None;
};
