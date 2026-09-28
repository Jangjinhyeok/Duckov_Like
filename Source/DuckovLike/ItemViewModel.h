#pragma once

#include "CoreMinimal.h"
#include "MVVMViewModelBase.h"
#include "ItemViewModel.generated.h"

class UInventoryModel;
struct FItemInstance;

UCLASS(BlueprintType)
class DUCKOVLIKE_API UItemViewModel : public UMVVMViewModelBase
{
    GENERATED_BODY()

public:
    DECLARE_EVENT(UItemViewModel, FOnChanged);

    void Bind(UInventoryModel* InModel, FName InContainerId, int32 InInstanceId);
    void NotifyChanged();
    // FieldNotify 완료 후 native View 소비자에게 한 번 알린다.
    FOnChanged& OnChanged() { return Changed; }

    UFUNCTION(BlueprintPure, FieldNotify, Category="Inventory")
    bool IsAvailable() const;
    UFUNCTION(BlueprintPure, FieldNotify, Category="Inventory")
    int32 GetInstanceId() const { return InstanceId; }
    UFUNCTION(BlueprintPure, FieldNotify, Category="Inventory")
    FName GetDefinitionRowName() const;
    UFUNCTION(BlueprintPure, FieldNotify, Category="Inventory")
    int32 GetQuantity() const;
    UFUNCTION(BlueprintPure, FieldNotify, Category="Inventory")
    FIntPoint GetAnchorCell() const;
    UFUNCTION(BlueprintPure, FieldNotify, Category="Inventory")
    bool IsRotated() const;
    UFUNCTION(BlueprintPure, FieldNotify, Category="Inventory")
    FIntPoint GetFootprint() const;

private:
    // 표시용 복제 상태를 소유하지 않고 ID로 Model을 다시 조회한다.
    TWeakObjectPtr<UInventoryModel> Model;
    FName ContainerId;
    int32 InstanceId = INDEX_NONE;
    FOnChanged Changed;
    const FItemInstance* FindItem() const;
};
