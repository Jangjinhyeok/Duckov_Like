#pragma once

#include "CoreMinimal.h"
#include "MVVMViewModelBase.h"
#include "ContainerViewModel.generated.h"

class UInventoryModel;
class UItemViewModel;
struct FInventoryChangeSet;
struct FInventoryContainerChange;

UCLASS(BlueprintType)
class DUCKOVLIKE_API UContainerViewModel : public UMVVMViewModelBase
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category="Inventory")
    void Bind(UInventoryModel* InModel, FName InContainerId);
    UFUNCTION(BlueprintPure, FieldNotify, Category="Inventory")
    FIntPoint GetGridSize() const;
    UFUNCTION(BlueprintPure, FieldNotify, Category="Inventory")
    bool IsAvailable() const;

    const TArray<TObjectPtr<UItemViewModel>>& GetItems() const { return Items; }
    virtual void BeginDestroy() override;

private:
    // 바인딩 수명 동안 Model을 보유한다. 화면 소유자가 닫힐 때 Bind(nullptr, NAME_None)로 해제한다.
    UPROPERTY(Transient)
    TObjectPtr<UInventoryModel> Model;
    UPROPERTY(Transient, BlueprintReadOnly, FieldNotify, Category="Inventory", meta=(AllowPrivateAccess="true"))
    TArray<TObjectPtr<UItemViewModel>> Items;
    FName ContainerId;
    FDelegateHandle ChangedHandle;
    bool bRefreshing = false;
    bool bHasPendingBinding = false;
    UPROPERTY(Transient)
    TObjectPtr<UInventoryModel> PendingModel;
    FName PendingContainerId;

    void OnModelChanged(const FInventoryChangeSet& Change);
    void Refresh(const FInventoryContainerChange* Change);
    void Unsubscribe();
    void ApplyPendingBinding();
};
