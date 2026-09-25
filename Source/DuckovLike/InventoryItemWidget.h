#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "InventoryItemWidget.generated.h"

class UItemViewModel;
class UInventoryScreenWidget;
class UTextBlock;

UCLASS()
class DUCKOVLIKE_API UInventoryItemWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    void Bind(UItemViewModel* InItem, UInventoryScreenWidget* InScreen = nullptr, FName InContainerId = NAME_None);
    virtual void NativeDestruct() override;
    virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
private:
    UPROPERTY(meta=(BindWidget))
    TObjectPtr<UTextBlock> ItemText;
    UPROPERTY(Transient)
    TObjectPtr<UItemViewModel> Item;
    TWeakObjectPtr<UInventoryScreenWidget> Screen;
    FName ContainerId;
    FDelegateHandle QuantityHandle;
    FDelegateHandle AnchorHandle;
    FDelegateHandle RotationHandle;
    void Refresh();
    void OnFieldChanged(UObject*, UE::FieldNotification::FFieldId);
};
