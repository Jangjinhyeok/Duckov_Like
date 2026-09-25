#pragma once

#include "CoreMinimal.h"
#include "InventoryOperationTypes.h"
#include "MVVMViewModelBase.h"
#include "InteractionViewModel.generated.h"

class UInventoryModel;
struct FInventoryChangeSet;

UCLASS(BlueprintType)
class DUCKOVLIKE_API UInteractionViewModel : public UMVVMViewModelBase
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category="Inventory")
    void Bind(UInventoryModel* InModel);
    UFUNCTION(BlueprintCallable, Category="Inventory")
    EInventoryOperationFailure BeginDrag(FName Source, int32 InstanceId);
    UFUNCTION(BlueprintCallable, Category="Inventory")
    EInventoryOperationFailure Preview(FName Target, FIntPoint Cell);
    UFUNCTION(BlueprintCallable, Category="Inventory")
    EInventoryOperationFailure Rotate();
    UFUNCTION(BlueprintCallable, Category="Inventory")
    EInventoryOperationFailure Drop();
    UFUNCTION(BlueprintCallable, Category="Inventory")
    void Cancel();
    UFUNCTION(BlueprintCallable, Category="Inventory")
    EInventoryOperationFailure Stack(FName Source, FName Target, int32 SourceId, int32 TargetId);
    UFUNCTION(BlueprintCallable, Category="Inventory")
    EInventoryOperationFailure Sort(FName ContainerId);
    UFUNCTION(BlueprintCallable, Category="Inventory")
    EInventoryOperationFailure Resize(FName ContainerId, FIntPoint Size);

    UFUNCTION(BlueprintPure, FieldNotify, Category="Inventory")
    bool IsDragging() const { return bDragging; }
    UFUNCTION(BlueprintPure, FieldNotify, Category="Inventory")
    FIntPoint GetPreviewCell() const { return PreviewCell; }
    UFUNCTION(BlueprintPure, FieldNotify, Category="Inventory")
    bool IsPreviewRotated() const { return bPreviewRotated; }
    UFUNCTION(BlueprintPure, FieldNotify, Category="Inventory")
    FName GetTargetContainerId() const { return TargetId; }
    UFUNCTION(BlueprintPure, FieldNotify, Category="Inventory")
    EInventoryOperationFailure GetPreviewFailure() const { return PreviewFailure; }
    UFUNCTION(BlueprintPure, FieldNotify, Category="Inventory")
    FText GetFailureText() const;

    virtual void BeginDestroy() override;

private:
    UPROPERTY(Transient)
    TObjectPtr<UInventoryModel> Model;
    FDelegateHandle ChangedHandle;
    FName SourceId;
    FName TargetId;
    int32 DraggedId = INDEX_NONE;
    FIntPoint PreviewCell = FIntPoint::ZeroValue;
    bool bPreviewRotated = false;
    bool bDragging = false;
    bool bUpdating = false;
    bool bHasPendingBinding = false;
    bool bPendingCancel = false;
    UPROPERTY(Transient)
    TObjectPtr<UInventoryModel> PendingModel;
    EInventoryOperationFailure PreviewFailure = EInventoryOperationFailure::ItemNotFound;
    EInventoryOperationFailure LastFailure = EInventoryOperationFailure::None;

    void Unsubscribe();
    void ClearDrag();
    void NotifyState();
    void ApplyPendingActions();
    void OnModelChanged(const FInventoryChangeSet& Change);
    EInventoryOperationFailure Revalidate();
    EInventoryOperationFailure Command(TFunctionRef<EInventoryOperationFailure()> Action);
};
