#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "InventoryGridWidget.generated.h"

class UCanvasPanel;
class UContainerViewModel;
class UInventoryItemWidget;
class UTextBlock;
class USizeBox;
class UBorder;
class UInventoryScreenWidget;

UCLASS(meta=(DisableNativeTick))
class DUCKOVLIKE_API UInventoryGridWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    void Bind(UContainerViewModel* InContainer, UInventoryScreenWidget* InScreen = nullptr, FName InContainerId = NAME_None);
    void SetTitle(const FText& InTitle);
    static bool PixelToCell(FVector2D LocalPosition, FIntPoint GridDimensions, FIntPoint& OutCell);
    bool TryGetCell(FVector2D AbsolutePosition, FIntPoint& OutCell) const;
    void ShowPreview(FIntPoint Cell, FIntPoint Footprint, bool bValid, const FText& Status);
    void HidePreview();
    virtual void NativeDestruct() override;
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
#endif
private:
    UPROPERTY(meta=(BindWidget))
    TObjectPtr<UCanvasPanel> GridCanvas;
    UPROPERTY(meta=(BindWidget))
    TObjectPtr<UTextBlock> HeaderText;
    UPROPERTY(meta=(BindWidget))
    TObjectPtr<USizeBox> GridSize;
    UPROPERTY(Transient)
    TObjectPtr<UContainerViewModel> Container;
    TWeakObjectPtr<UInventoryScreenWidget> Screen;
    FName ContainerId;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UInventoryItemWidget>> ItemWidgets;
    UPROPERTY(Transient)
    TObjectPtr<UBorder> PreviewBorder;
    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> PreviewText;
    FDelegateHandle ItemsHandle;
    FDelegateHandle SizeHandle;
    FText Title;
    void Refresh();
    void OnFieldChanged(UObject*, UE::FieldNotification::FFieldId);
};
