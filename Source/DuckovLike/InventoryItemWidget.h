#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "InventoryItemWidget.generated.h"

class UItemViewModel;
class UInventoryScreenWidget;
class UTextBlock;

UCLASS(meta=(DisableNativeTick))
class DUCKOVLIKE_API UInventoryItemWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    void Bind(UItemViewModel* InItem, UInventoryScreenWidget* InScreen = nullptr, FName InContainerId = NAME_None);
    virtual void NativeDestruct() override;
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
#endif
    virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
private:
#if WITH_DEV_AUTOMATION_TESTS
    friend struct FInventoryItemWidgetTestAccess;
#endif
    UPROPERTY(meta=(BindWidget))
    TObjectPtr<UTextBlock> ItemText;
    UPROPERTY(Transient)
    TObjectPtr<UItemViewModel> Item;
    TWeakObjectPtr<UInventoryScreenWidget> Screen;
    FName ContainerId;
    FDelegateHandle ChangedHandle;
    void Refresh();
};
