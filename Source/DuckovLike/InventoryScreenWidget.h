#pragma once

#include "CoreMinimal.h"
#include "CommonActivatableWidget.h"
#include "Types/SlateEnums.h"
#include "InventoryScreenWidget.generated.h"

class UButton;
class UCanvasPanel;
class UContainerViewModel;
class UEditableTextBox;
class UInteractionViewModel;
class UInventoryGridWidget;
class UInventoryModel;
class UItemViewModel;
class UTextBlock;

UCLASS(meta=(DisableNativeTick))
class DUCKOVLIKE_API UInventoryScreenWidget : public UCommonActivatableWidget
{
    GENERATED_BODY()
public:
    UInventoryScreenWidget(const FObjectInitializer& ObjectInitializer);
    void SetSession(UInventoryModel* InModel);
    bool BeginItemDrag(FName Source, UItemViewModel* Item, const FGeometry& ItemGeometry, FVector2D AbsolutePosition);
    bool OpenSplitDialog(FName Source, UItemViewModel* Item);
    void UpdateItemDrag(FVector2D AbsolutePosition);
    void EndItemDrag(FVector2D AbsolutePosition);
    void CancelItemDrag();
    virtual void NativeOnActivated() override;
    virtual void NativeOnDeactivated() override;
    virtual void NativeDestruct() override;
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
#endif
    virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
    virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
    virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
    virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
    virtual void NativeOnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;
protected:
    virtual bool NativeOnHandleBackAction() override;
    virtual UWidget* NativeGetDesiredFocusTarget() const override;
    virtual TOptional<FUIInputConfig> GetDesiredInputConfig() const override;
private:
    friend struct FInventoryScreenWidgetTestAccess;
    UPROPERTY(meta=(BindWidget))
    TObjectPtr<UInventoryGridWidget> LeftGrid;
    UPROPERTY(meta=(BindWidget))
    TObjectPtr<UInventoryGridWidget> RightGrid;
    UPROPERTY(meta=(BindWidget))
    TObjectPtr<UButton> CloseButton;
    UPROPERTY(meta=(BindWidget))
    TObjectPtr<UButton> SortLeftButton;
    UPROPERTY(meta=(BindWidget))
    TObjectPtr<UButton> SortRightButton;
    UPROPERTY(Transient)
    TObjectPtr<UInventoryModel> Model;
    UPROPERTY(Transient)
    TObjectPtr<UContainerViewModel> LeftVM;
    UPROPERTY(Transient)
    TObjectPtr<UContainerViewModel> RightVM;
    UPROPERTY(Transient)
    TObjectPtr<UInteractionViewModel> Interaction;
    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> FailureText;
    UPROPERTY(Transient)
    TObjectPtr<UCanvasPanel> SplitOverlay;
    UPROPERTY(Transient)
    TObjectPtr<UEditableTextBox> SplitQuantityInput;
    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> SplitRangeText;
    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> SplitErrorText;
    UPROPERTY(Transient)
    TObjectPtr<UButton> SplitConfirmButton;
    UPROPERTY(Transient)
    TObjectPtr<UButton> SplitCancelButton;
    TWeakObjectPtr<UItemViewModel> SplitSourceItem;
    FName SplitSourceContainer;
    int32 SplitSourceId = INDEX_NONE;
    FDelegateHandle SplitItemChangedHandle;
    TWeakObjectPtr<UItemViewModel> DraggedItem;
    FIntPoint PointerOffset = FIntPoint::ZeroValue;
    FVector2D LastPointerPosition = FVector2D::ZeroVector;
    bool bPointerCaptured = false;
    FDelegateHandle DragStateHandle;
    void ReleasePointerCapture();
    void RefreshDragView();
    void OnDragStateChanged(UObject*, UE::FieldNotification::FFieldId);
    void CreateSplitDialog();
    void CloseSplitDialog();
    void OnSplitSourceChanged();
    bool ParseSplitQuantity(const FText& Text, int32& OutQuantity) const;
    UFUNCTION()
    void OnSplitQuantityChanged(const FText& Text);
    UFUNCTION()
    void OnSplitQuantityCommitted(const FText& Text, ETextCommit::Type CommitMethod);
    UFUNCTION()
    void ConfirmSplit();
    UFUNCTION()
    void CancelSplit();
    UFUNCTION()
    void Close();
    UFUNCTION()
    void SortLeft();
    UFUNCTION()
    void SortRight();
};
