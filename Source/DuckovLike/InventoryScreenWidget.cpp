#include "InventoryScreenWidget.h"

#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Blueprint/WidgetTree.h"
#include "ContainerViewModel.h"
#include "InputCoreTypes.h"
#include "InteractionViewModel.h"
#include "InventoryGridWidget.h"
#include "InventoryModel.h"
#include "Input/UIActionBindingHandle.h"
#include "ItemViewModel.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWidget.h"
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "InventoryTickProbe.h"
#endif

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
void UInventoryScreenWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    if (GInventoryTickProbe && GInventoryTickProbe->Screen == this)
    {
        ++GInventoryTickProbe->ScreenCalls;
    }
    Super::NativeTick(MyGeometry, InDeltaTime);
}
#endif

UInventoryScreenWidget::UInventoryScreenWidget(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    bSetVisibilityOnActivated = true;
    bIsBackHandler = true;
    ActivatedVisibility = ESlateVisibility::Visible;
    bSetVisibilityOnDeactivated = true;
    DeactivatedVisibility = ESlateVisibility::Collapsed;
    SetVisibility(ESlateVisibility::Collapsed);
}

void UInventoryScreenWidget::SetSession(UInventoryModel* InModel) { Model = InModel; }

void UInventoryScreenWidget::NativeOnActivated()
{
    Super::NativeOnActivated();
    if (!ensure(Model && LeftGrid && RightGrid && CloseButton && SortLeftButton && SortRightButton)) { DeactivateWidget(); return; }
    if (!LeftVM) { LeftVM = NewObject<UContainerViewModel>(this); }
    if (!RightVM) { RightVM = NewObject<UContainerViewModel>(this); }
    if (!Interaction) { Interaction = NewObject<UInteractionViewModel>(this); }
    LeftVM->Bind(Model, TEXT("Stash"));
    RightVM->Bind(Model, TEXT("Bag"));
    Interaction->Bind(Model);
    if (!FailureText)
    {
        if (UCanvasPanel* Root = Cast<UCanvasPanel>(WidgetTree->RootWidget))
        {
            FailureText = NewObject<UTextBlock>(this);
            FailureText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
            FailureText->SetVisibility(ESlateVisibility::HitTestInvisible);
            UCanvasPanelSlot* CanvasSlot = Root->AddChildToCanvas(FailureText);
            CanvasSlot->SetAnchors(FAnchors(0.5f));
            CanvasSlot->SetAlignment(FVector2D(0.5f, 0.f));
            CanvasSlot->SetPosition(FVector2D(0.f, 190.f));
            CanvasSlot->SetAutoSize(true);
            CanvasSlot->SetZOrder(100);
        }
    }
    LeftGrid->SetTitle(NSLOCTEXT("Inventory", "Stash", "보관함"));
    RightGrid->SetTitle(NSLOCTEXT("Inventory", "Bag", "가방"));
    LeftGrid->Bind(LeftVM, this, TEXT("Stash"));
    RightGrid->Bind(RightVM, this, TEXT("Bag"));
    const auto Callback = INotifyFieldValueChanged::FFieldValueChangedDelegate::CreateUObject(this, &ThisClass::OnDragStateChanged);
    DragStateHandle = Interaction->AddFieldValueChangedDelegate(
        UInteractionViewModel::FFieldNotificationClassDescriptor::IsDragging, Callback);
    RefreshDragView();
    CloseButton->OnClicked.AddUniqueDynamic(this, &ThisClass::Close);
    SortLeftButton->OnClicked.AddUniqueDynamic(this, &ThisClass::SortLeft);
    SortRightButton->OnClicked.AddUniqueDynamic(this, &ThisClass::SortRight);
    if (APlayerController* Owner = GetOwningPlayer()) { CloseButton->SetUserFocus(Owner); }
}

void UInventoryScreenWidget::NativeOnDeactivated()
{
    ReleasePointerCapture();
    if (Interaction && DragStateHandle.IsValid())
    {
        Interaction->RemoveFieldValueChangedDelegate(
            UInteractionViewModel::FFieldNotificationClassDescriptor::IsDragging, DragStateHandle);
        DragStateHandle.Reset();
    }
    DraggedItem.Reset();
    if (LeftGrid) { LeftGrid->HidePreview(); }
    if (RightGrid) { RightGrid->HidePreview(); }
    if (FailureText) { FailureText->SetText(FText::GetEmpty()); }
    if (LeftGrid) { LeftGrid->Bind(nullptr); }
    if (RightGrid) { RightGrid->Bind(nullptr); }
    if (LeftVM) { LeftVM->Bind(nullptr, NAME_None); }
    if (RightVM) { RightVM->Bind(nullptr, NAME_None); }
    if (Interaction) { Interaction->Cancel(); Interaction->Bind(nullptr); }
    if (CloseButton) { CloseButton->OnClicked.RemoveDynamic(this, &ThisClass::Close); }
    if (SortLeftButton) { SortLeftButton->OnClicked.RemoveDynamic(this, &ThisClass::SortLeft); }
    if (SortRightButton) { SortRightButton->OnClicked.RemoveDynamic(this, &ThisClass::SortRight); }
    Super::NativeOnDeactivated();
}

void UInventoryScreenWidget::NativeDestruct()
{
    if (IsActivated()) { DeactivateWidget(); }
    Super::NativeDestruct();
}

FReply UInventoryScreenWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
    if (Interaction && Interaction->IsDragging() && InKeyEvent.GetKey() == EKeys::R)
    {
        Interaction->Rotate();
        if (UItemViewModel* Item = DraggedItem.Get())
        {
            FIntPoint Footprint = Item->GetFootprint();
            if (Item->IsRotated() != Interaction->IsPreviewRotated()) { Swap(Footprint.X, Footprint.Y); }
            PointerOffset.X = FMath::Clamp(PointerOffset.X, 0, FMath::Max(0, Footprint.X - 1));
            PointerOffset.Y = FMath::Clamp(PointerOffset.Y, 0, FMath::Max(0, Footprint.Y - 1));
            UpdateItemDrag(LastPointerPosition);
        }
        return FReply::Handled();
    }
    if (InKeyEvent.GetKey() == EKeys::I)
    {
        Close();
        return FReply::Handled();
    }
    return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

FReply UInventoryScreenWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
    if (bPointerCaptured && Interaction && Interaction->IsDragging()
        && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
    {
        UpdateItemDrag(InMouseEvent.GetScreenSpacePosition());
        return FReply::Handled();
    }
    return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

FReply UInventoryScreenWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
    if (bPointerCaptured && Interaction && Interaction->IsDragging())
    {
        UpdateItemDrag(InMouseEvent.GetScreenSpacePosition());
        return FReply::Handled();
    }
    return Super::NativeOnMouseMove(InGeometry, InMouseEvent);
}

FReply UInventoryScreenWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
    if (bPointerCaptured && Interaction && Interaction->IsDragging()
        && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
    {
        if (CloseButton && CloseButton->GetCachedGeometry().IsUnderLocation(InMouseEvent.GetScreenSpacePosition()))
        {
            Close();
            return FReply::Handled().ReleaseMouseCapture();
        }
        EndItemDrag(InMouseEvent.GetScreenSpacePosition());
        return Interaction->IsDragging() ? FReply::Handled() : FReply::Handled().ReleaseMouseCapture();
    }
    return Super::NativeOnMouseButtonUp(InGeometry, InMouseEvent);
}

void UInventoryScreenWidget::NativeOnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
    if (bPointerCaptured)
    {
        bPointerCaptured = false;
        CancelItemDrag();
    }
    Super::NativeOnMouseCaptureLost(CaptureLostEvent);
}

bool UInventoryScreenWidget::BeginItemDrag(FName Source, UItemViewModel* Item,
    const FGeometry& ItemGeometry, FVector2D AbsolutePosition)
{
    if (!IsActivated() || !Interaction || !Item || !Item->IsAvailable()) { return false; }
    const FVector2D Local = ItemGeometry.AbsoluteToLocal(AbsolutePosition);
    const FIntPoint Size = Item->GetFootprint();
    PointerOffset = FIntPoint(FMath::Clamp(FMath::FloorToInt(Local.X / 52.f), 0, FMath::Max(0, Size.X - 1)),
        FMath::Clamp(FMath::FloorToInt(Local.Y / 52.f), 0, FMath::Max(0, Size.Y - 1)));
    DraggedItem = Item;
    LastPointerPosition = AbsolutePosition;
    if (Interaction->BeginDrag(Source, Item->GetInstanceId()) != EInventoryOperationFailure::None
        || !Interaction->IsDragging())
    {
        DraggedItem.Reset();
        return false;
    }
    UpdateItemDrag(AbsolutePosition);
    if (CloseButton)
    {
        if (APlayerController* Owner = GetOwningPlayer()) { CloseButton->SetUserFocus(Owner); }
    }
    bPointerCaptured = true;
    return true;
}

void UInventoryScreenWidget::UpdateItemDrag(FVector2D AbsolutePosition)
{
    if (!Interaction || !Interaction->IsDragging()) { return; }
    LastPointerPosition = AbsolutePosition;
    FIntPoint Cell;
    if (LeftGrid && LeftGrid->TryGetCell(AbsolutePosition, Cell))
    {
        Interaction->Preview(TEXT("Stash"), Cell - PointerOffset);
    }
    else if (RightGrid && RightGrid->TryGetCell(AbsolutePosition, Cell))
    {
        Interaction->Preview(TEXT("Bag"), Cell - PointerOffset);
    }
    else { Interaction->Preview(NAME_None, FIntPoint::ZeroValue); }
}

void UInventoryScreenWidget::EndItemDrag(FVector2D AbsolutePosition)
{
    if (!Interaction || !Interaction->IsDragging()) { return; }
    UpdateItemDrag(AbsolutePosition);
    Interaction->Drop();
    RefreshDragView();
}

void UInventoryScreenWidget::CancelItemDrag()
{
    if (Interaction) { Interaction->Cancel(); }
    DraggedItem.Reset();
    RefreshDragView();
}

void UInventoryScreenWidget::ReleasePointerCapture()
{
    if (!bPointerCaptured) { return; }
    bPointerCaptured = false;
    if (FSlateApplication::IsInitialized())
    {
        FSlateApplication& Slate = FSlateApplication::Get();
        const TSharedPtr<SWidget> Cached = GetCachedWidget();
        if (Cached.IsValid() && Cached->HasMouseCaptureByUser(Slate.GetUserIndexForMouse()))
        {
            Slate.ReleaseAllPointerCapture(Slate.GetUserIndexForMouse());
        }
    }
}

void UInventoryScreenWidget::OnDragStateChanged(UObject*, UE::FieldNotification::FFieldId)
{
    RefreshDragView();
}

void UInventoryScreenWidget::RefreshDragView()
{
    if (LeftGrid) { LeftGrid->HidePreview(); }
    if (RightGrid) { RightGrid->HidePreview(); }
    if (!Interaction) { return; }
    if (FailureText) { FailureText->SetText(Interaction->GetFailureText()); }
    if (!Interaction->IsDragging())
    {
        DraggedItem.Reset();
        ReleasePointerCapture();
        return;
    }
    UInventoryGridWidget* Target = Interaction->GetTargetContainerId() == TEXT("Stash") ? LeftGrid.Get()
        : Interaction->GetTargetContainerId() == TEXT("Bag") ? RightGrid.Get() : nullptr;
    UItemViewModel* Item = DraggedItem.Get();
    if (!Target || !Item || !Item->IsAvailable()) { return; }
    FIntPoint Footprint = Item->GetFootprint();
    if (Item->IsRotated() != Interaction->IsPreviewRotated()) { Swap(Footprint.X, Footprint.Y); }
    const bool bValid = Interaction->GetPreviewFailure() == EInventoryOperationFailure::None;
    Target->ShowPreview(Interaction->GetPreviewCell(), Footprint, bValid,
        bValid ? NSLOCTEXT("Inventory", "PreviewValid", "배치 가능") : Interaction->GetFailureText());
}

UWidget* UInventoryScreenWidget::NativeGetDesiredFocusTarget() const { return CloseButton; }
bool UInventoryScreenWidget::NativeOnHandleBackAction()
{
    if (Interaction && Interaction->IsDragging())
    {
        CancelItemDrag();
        return true;
    }
    return Super::NativeOnHandleBackAction();
}
TOptional<FUIInputConfig> UInventoryScreenWidget::GetDesiredInputConfig() const
{
    return FUIInputConfig(ECommonInputMode::Menu, EMouseCaptureMode::NoCapture);
}
void UInventoryScreenWidget::Close() { DeactivateWidget(); }
void UInventoryScreenWidget::SortLeft() { if (Interaction) { Interaction->Sort(TEXT("Stash")); } }
void UInventoryScreenWidget::SortRight() { if (Interaction) { Interaction->Sort(TEXT("Bag")); } }
