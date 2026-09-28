#include "InventoryItemWidget.h"

#include "Components/TextBlock.h"
#include "Components/CanvasPanelSlot.h"
#include "ItemViewModel.h"
#include "InventoryScreenWidget.h"
#include "InputCoreTypes.h"
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "InventoryPerformanceProbe.h"
#endif

void UInventoryItemWidget::Bind(UItemViewModel* InItem, UInventoryScreenWidget* InScreen, FName InContainerId)
{
    if (Item)
    {
        Item->RemoveFieldValueChangedDelegate(UItemViewModel::FFieldNotificationClassDescriptor::GetQuantity, QuantityHandle);
        Item->RemoveFieldValueChangedDelegate(UItemViewModel::FFieldNotificationClassDescriptor::GetAnchorCell, AnchorHandle);
        Item->RemoveFieldValueChangedDelegate(UItemViewModel::FFieldNotificationClassDescriptor::IsRotated, RotationHandle);
    }
    Item = InItem;
    Screen = InScreen;
    ContainerId = InContainerId;
    if (Item)
    {
        const auto Callback = INotifyFieldValueChanged::FFieldValueChangedDelegate::CreateUObject(this, &ThisClass::OnFieldChanged);
        QuantityHandle = Item->AddFieldValueChangedDelegate(UItemViewModel::FFieldNotificationClassDescriptor::GetQuantity, Callback);
        AnchorHandle = Item->AddFieldValueChangedDelegate(UItemViewModel::FFieldNotificationClassDescriptor::GetAnchorCell, Callback);
        RotationHandle = Item->AddFieldValueChangedDelegate(UItemViewModel::FFieldNotificationClassDescriptor::IsRotated, Callback);
    }
    Refresh();
}

void UInventoryItemWidget::OnFieldChanged(UObject*, UE::FieldNotification::FFieldId) { Refresh(); }

void UInventoryItemWidget::Refresh()
{
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
    if (GInventoryPerformanceProbe && GInventoryPerformanceProbe->TrackedWidgets.Contains(this))
    {
        ++GInventoryPerformanceProbe->ItemRefresh;
        ++(Item ? GInventoryPerformanceProbe->BoundItemRefresh : GInventoryPerformanceProbe->UnboundItemRefresh);
        ++GInventoryPerformanceProbe->RefreshByWidget.FindOrAdd(this);
    }
#endif
    if (Item && Item->IsAvailable())
    {
        if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Slot))
        {
            const FIntPoint Anchor = Item->GetAnchorCell();
            const FIntPoint Size = Item->GetFootprint();
            CanvasSlot->SetPosition(FVector2D(Anchor.X * 52.f, Anchor.Y * 52.f));
            CanvasSlot->SetSize(FVector2D(Size.X * 52.f - 4.f, Size.Y * 52.f - 4.f));
        }
    }
    if (ItemText)
    {
        ItemText->SetText(Item && Item->IsAvailable()
            ? FText::Format(NSLOCTEXT("Inventory", "ItemLabel", "{0}\nx{1}"),
                FText::FromName(Item->GetDefinitionRowName()), FText::AsNumber(Item->GetQuantity()))
            : FText::GetEmpty());
    }
    if (Item && Item->IsAvailable())
    {
        const FIntPoint Size = Item->GetFootprint();
        SetToolTipText(FText::Format(NSLOCTEXT("Inventory", "ItemTooltip", "{0}\n크기 {1} × {2}\n수량 {3}"),
            FText::FromName(Item->GetDefinitionRowName()), FText::AsNumber(Size.X),
            FText::AsNumber(Size.Y), FText::AsNumber(Item->GetQuantity())));
    }
    else { SetToolTipText(FText::GetEmpty()); }
}

FReply UInventoryItemWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
    if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && Screen.IsValid() && Item && Item->IsAvailable()
        && Screen->BeginItemDrag(ContainerId, Item, InGeometry, InMouseEvent.GetScreenSpacePosition()))
    {
        return FReply::Handled().CaptureMouse(Screen->TakeWidget());
    }
    return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

void UInventoryItemWidget::NativeDestruct()
{
    Bind(nullptr);
    Super::NativeDestruct();
}
