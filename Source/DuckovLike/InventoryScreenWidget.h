#pragma once

#include "CoreMinimal.h"
#include "CommonActivatableWidget.h"
#include "Types/SlateEnums.h"
#include "InventoryScreenWidget.generated.h"

class UButton;
class UBagEquipmentViewModel;
class UCanvasPanel;
class UContainerViewModel;
class UEditableTextBox;
class UInteractionViewModel;
class UInventoryGridWidget;
class UInventoryModel;
class UItemViewModel;
class UPanelWidget;
class UTextBlock;

UCLASS(meta=(DisableNativeTick))
class DUCKOVLIKE_API UInventoryScreenWidget : public UCommonActivatableWidget
{
    GENERATED_BODY()
public:
    UInventoryScreenWidget(const FObjectInitializer& ObjectInitializer);
    void SetSession(UInventoryModel* InModel);
    void SetStashAccessible(bool bAccessible);
    bool IsInventoryInteractionBlocked() const;
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
    // 이름은 기존 BindWidget 계약을 유지한다. 원작 배치에 따라 LeftGrid(창고)는 오른쪽 패널, RightGrid(가방)는 왼쪽 패널에 있다.
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
    UPROPERTY(meta=(BindWidget))
    TObjectPtr<UButton> StoreAllButton;
    // 창고를 쓸 수 없으면(레이드) 창고 패널 전체를 감춘다.
    UPROPERTY(meta=(BindWidgetOptional))
    TObjectPtr<UWidget> StashPanel;
    // 장비(무기·방어구 slot 줄과 가방 슬롯) 패널이 들어갈 자리. 없으면 가방 슬롯만 화면 위쪽에 띄우고 slot 줄은 만들지 않는다.
    UPROPERTY(meta=(BindWidgetOptional))
    TObjectPtr<UPanelWidget> EquipmentSlot;
    UPROPERTY(Transient)
    TObjectPtr<UInventoryModel> Model;
    UPROPERTY(Transient)
    TObjectPtr<UContainerViewModel> LeftVM;
    UPROPERTY(Transient)
    TObjectPtr<UContainerViewModel> RightVM;
    UPROPERTY(Transient)
    TObjectPtr<UInteractionViewModel> Interaction;
    UPROPERTY(Transient)
    TObjectPtr<UBagEquipmentViewModel> BagEquipment;
    UPROPERTY(Transient)
    TObjectPtr<UWidget> BagEquipmentPanel;
    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> BagEquipmentText;
    UPROPERTY(Transient)
    TObjectPtr<UButton> EquipSmallBagButton;
    UPROPERTY(Transient)
    TObjectPtr<UButton> EquipLargeBagButton;
    // 무기·방어구 장비 slot. 처음 열 때 EquipmentSlot 안에 WBP_InventoryGrid로 만들고, 두 slot이 Model에 등록됐을 때만 보인다.
    UPROPERTY(Transient)
    TObjectPtr<UWidget> EquipmentSlotsPanel;
    UPROPERTY(Transient)
    TObjectPtr<UInventoryGridWidget> WeaponSlotGrid;
    UPROPERTY(Transient)
    TObjectPtr<UInventoryGridWidget> ArmorSlotGrid;
    UPROPERTY(Transient)
    TObjectPtr<UContainerViewModel> WeaponSlotVM;
    UPROPERTY(Transient)
    TObjectPtr<UContainerViewModel> ArmorSlotVM;
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
    bool bStashAccessible = true;
    FDelegateHandle DragStateHandle;
    FDelegateHandle BagEquipmentHandle;
    void ReleasePointerCapture();
    void RefreshDragView();
    void RefreshStashAccess();
    void CreateBagEquipmentPanel();
    void CreateEquipmentSlots();
    void BindEquipmentSlots(bool bActive);
    void RefreshBagEquipment();
    void OnBagEquipmentChanged(UObject*, UE::FieldNotification::FFieldId);
    void EquipBag(FName RowName);
    UFUNCTION()
    void EquipSmallBag();
    UFUNCTION()
    void EquipLargeBag();
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
    UFUNCTION()
    void StoreAllInStash();
};
