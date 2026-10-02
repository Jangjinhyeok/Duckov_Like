#pragma once

#include "CoreMinimal.h"
#include "MVVMViewModelBase.h"
#include "BagEquipmentViewModel.generated.h"

class UInventoryModel;
struct FInventoryChangeSet;

UCLASS(BlueprintType)
class DUCKOVLIKE_API UBagEquipmentViewModel : public UMVVMViewModelBase
{
    GENERATED_BODY()
public:
    void Bind(UInventoryModel* InModel);
    UFUNCTION(BlueprintPure, FieldNotify, Category="Inventory")
    bool HasBinding() const;
    UFUNCTION(BlueprintPure, FieldNotify, Category="Inventory")
    FText GetEquippedText() const;
    UFUNCTION(BlueprintPure, FieldNotify, Category="Inventory")
    int32 GetSmallBagInstanceId() const;
    UFUNCTION(BlueprintPure, FieldNotify, Category="Inventory")
    int32 GetLargeBagInstanceId() const;
    virtual void BeginDestroy() override;
private:
    UPROPERTY(Transient)
    TObjectPtr<UInventoryModel> Model;
    UPROPERTY(Transient)
    TObjectPtr<UInventoryModel> PendingModel;
    FDelegateHandle ChangedHandle;
    bool bNotifying = false;
    bool bHasPendingBinding = false;
    int32 FindSourceBag(FName RowName) const;
    void OnModelChanged(const FInventoryChangeSet& Change);
    void NotifyState();
    void Unsubscribe();
    void ApplyPendingBinding();
};
