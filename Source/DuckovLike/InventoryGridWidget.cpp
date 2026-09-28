#include "InventoryGridWidget.h"

#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Components/SizeBox.h"
#include "Components/Border.h"
#include "ContainerViewModel.h"
#include "InventoryItemWidget.h"
#include "InventoryScreenWidget.h"
#include "ItemViewModel.h"
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "InventoryPerformanceProbe.h"
#endif

void UInventoryGridWidget::SetTitle(const FText& InTitle)
{
    Title = InTitle;
    Refresh();
}

void UInventoryGridWidget::Bind(UContainerViewModel* InContainer, UInventoryScreenWidget* InScreen, FName InContainerId)
{
    if (Container)
    {
        Container->RemoveFieldValueChangedDelegate(UContainerViewModel::FFieldNotificationClassDescriptor::Items, ItemsHandle);
        Container->RemoveFieldValueChangedDelegate(UContainerViewModel::FFieldNotificationClassDescriptor::GetGridSize, SizeHandle);
    }
    Container = InContainer;
    Screen = InScreen;
    ContainerId = InContainerId;
    HidePreview();
    if (Container)
    {
        const auto Callback = INotifyFieldValueChanged::FFieldValueChangedDelegate::CreateUObject(this, &ThisClass::OnFieldChanged);
        ItemsHandle = Container->AddFieldValueChangedDelegate(UContainerViewModel::FFieldNotificationClassDescriptor::Items, Callback);
        SizeHandle = Container->AddFieldValueChangedDelegate(UContainerViewModel::FFieldNotificationClassDescriptor::GetGridSize, Callback);
    }
    Refresh();
}

void UInventoryGridWidget::OnFieldChanged(UObject*, UE::FieldNotification::FFieldId) { Refresh(); }

void UInventoryGridWidget::Refresh()
{
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
    FInventoryPerformanceProbe* Probe = GInventoryPerformanceProbe;
    const bool bMeasuredGrid = Probe && GetFName() == TEXT("LeftGrid");
    if (bMeasuredGrid)
    {
        Probe->Canvas = GridCanvas;
        ++Probe->GridRefresh;
        Probe->ItemRemoved += ItemWidgets.Num();
    }
#endif
    if (HeaderText) { HeaderText->SetText(Title); }
    if (!GridCanvas) { return; }
    for (UInventoryItemWidget* Widget : ItemWidgets) { Widget->Bind(nullptr); Widget->RemoveFromParent(); }
    ItemWidgets.Reset();
    if (!Container || !Container->IsAvailable()) { return; }
    if (GridSize)
    {
        const FIntPoint Size = Container->GetGridSize();
        GridSize->SetWidthOverride(Size.X * 52.f);
        GridSize->SetHeightOverride(Size.Y * 52.f);
    }
    for (UItemViewModel* Item : Container->GetItems())
    {
        if (!Item || !Item->IsAvailable()) { continue; }
        UClass* ItemClass = LoadClass<UInventoryItemWidget>(nullptr, TEXT("/Game/UI/WBP_InventoryItem.WBP_InventoryItem_C"));
        if (!ItemClass) { UE_LOG(LogTemp, Error, TEXT("인벤토리 Item 위젯 에셋을 찾지 못했습니다")); return; }
        UInventoryItemWidget* Widget = CreateWidget<UInventoryItemWidget>(GetOwningPlayer(), ItemClass);
        GridCanvas->AddChildToCanvas(Widget);
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
        if (bMeasuredGrid) { Probe->TrackedWidgets.Add(Widget); }
#endif
        Widget->Bind(Item, Screen.Get(), ContainerId);
        ItemWidgets.Add(Widget);
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
        if (bMeasuredGrid) { ++Probe->ItemCreated; }
#endif
    }
}

bool UInventoryGridWidget::PixelToCell(FVector2D LocalPosition, FIntPoint GridDimensions, FIntPoint& OutCell)
{
    if (!FMath::IsFinite(LocalPosition.X) || !FMath::IsFinite(LocalPosition.Y)
        || LocalPosition.X < 0.f || LocalPosition.Y < 0.f
        || LocalPosition.X >= GridDimensions.X * 52.f || LocalPosition.Y >= GridDimensions.Y * 52.f)
    {
        return false;
    }
    OutCell = FIntPoint(FMath::FloorToInt(LocalPosition.X / 52.f), FMath::FloorToInt(LocalPosition.Y / 52.f));
    return true;
}

bool UInventoryGridWidget::TryGetCell(FVector2D AbsolutePosition, FIntPoint& OutCell) const
{
    return GridCanvas && Container && Container->IsAvailable()
        && PixelToCell(GridCanvas->GetCachedGeometry().AbsoluteToLocal(AbsolutePosition), Container->GetGridSize(), OutCell);
}

void UInventoryGridWidget::ShowPreview(FIntPoint Cell, FIntPoint Footprint, bool bValid, const FText& Status)
{
    if (!GridCanvas) { return; }
    if (!PreviewBorder)
    {
        PreviewBorder = NewObject<UBorder>(this);
        PreviewBorder->SetPadding(FMargin(4.f));
        PreviewBorder->SetVisibility(ESlateVisibility::HitTestInvisible);
        PreviewText = NewObject<UTextBlock>(this);
        PreviewBorder->AddChild(PreviewText);
        UCanvasPanelSlot* CanvasSlot = GridCanvas->AddChildToCanvas(PreviewBorder);
        CanvasSlot->SetZOrder(100);
    }
    PreviewBorder->SetBrushColor(bValid ? FLinearColor(0.05f, 0.55f, 0.25f, 0.75f)
        : FLinearColor(0.75f, 0.12f, 0.10f, 0.85f));
    PreviewText->SetText(Status);
    UCanvasPanelSlot* CanvasSlot = CastChecked<UCanvasPanelSlot>(PreviewBorder->Slot);
    CanvasSlot->SetPosition(FVector2D(Cell.X * 52.f, Cell.Y * 52.f));
    CanvasSlot->SetSize(FVector2D(Footprint.X * 52.f - 4.f, Footprint.Y * 52.f - 4.f));
    PreviewBorder->SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UInventoryGridWidget::HidePreview()
{
    if (PreviewBorder) { PreviewBorder->SetVisibility(ESlateVisibility::Collapsed); }
}

void UInventoryGridWidget::NativeDestruct()
{
    Bind(nullptr);
    Super::NativeDestruct();
}
