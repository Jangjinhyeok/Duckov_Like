#include "InventoryScreenWidget.h"

#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/HorizontalBox.h"
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
#include "String/LexFromString.h"
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
    CreateSplitDialog();
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
    CloseSplitDialog();
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
    if (SplitSourceItem.IsValid())
    {
        if (InKeyEvent.GetKey() == EKeys::Escape || InKeyEvent.GetKey() == EKeys::I)
        {
            CloseSplitDialog();
            return FReply::Handled();
        }
        else if (InKeyEvent.GetKey() == EKeys::Enter)
        {
            ConfirmSplit();
            return FReply::Handled();
        }
        return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
    }
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
    if (SplitSourceItem.IsValid()) { return FReply::Handled(); }
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
    if (SplitSourceItem.IsValid()) { return FReply::Handled(); }
    if (bPointerCaptured && Interaction && Interaction->IsDragging())
    {
        UpdateItemDrag(InMouseEvent.GetScreenSpacePosition());
        return FReply::Handled();
    }
    return Super::NativeOnMouseMove(InGeometry, InMouseEvent);
}

FReply UInventoryScreenWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
    if (SplitSourceItem.IsValid()) { return FReply::Handled(); }
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
    if (!IsActivated() || SplitSourceItem.IsValid() || !Interaction || !Item || !Item->IsAvailable()) { return false; }
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

void UInventoryScreenWidget::CreateSplitDialog()
{
    if (SplitOverlay) { return; }
    UCanvasPanel* Root = Cast<UCanvasPanel>(WidgetTree->RootWidget);
    if (!Root) { return; }
    SplitOverlay = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("SplitOverlay"));
    UCanvasPanelSlot* OverlaySlot = Root->AddChildToCanvas(SplitOverlay);
    OverlaySlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
    OverlaySlot->SetOffsets(FMargin(0.f));
    OverlaySlot->SetZOrder(200);
    SplitOverlay->SetVisibility(ESlateVisibility::Collapsed);

    UBorder* Backdrop = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("SplitBackdrop"));
    Backdrop->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.75f));
    UCanvasPanelSlot* BackdropSlot = SplitOverlay->AddChildToCanvas(Backdrop);
    BackdropSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
    BackdropSlot->SetOffsets(FMargin(0.f));

    UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("SplitDialog"));
    Panel->SetBrushColor(FLinearColor(0.12f, 0.13f, 0.15f, 1.f));
    Panel->SetPadding(FMargin(20.f));
    UCanvasPanelSlot* PanelSlot = SplitOverlay->AddChildToCanvas(Panel);
    PanelSlot->SetAnchors(FAnchors(0.5f));
    PanelSlot->SetAlignment(FVector2D(0.5f));
    PanelSlot->SetSize(FVector2D(360.f, 220.f));
    PanelSlot->SetZOrder(1);

    UVerticalBox* Content = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("SplitContent"));
    Panel->AddChild(Content);
    UTextBlock* Title = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("SplitTitle"));
    Title->SetText(NSLOCTEXT("Inventory", "SplitTitle", "스택 분할"));
    Title->SetColorAndOpacity(FSlateColor(FLinearColor::White));
    Content->AddChild(Title);
    SplitRangeText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("SplitRangeText"));
    SplitRangeText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
    Content->AddChild(SplitRangeText);
    SplitQuantityInput = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("SplitQuantityInput"));
    SplitQuantityInput->SetHintText(NSLOCTEXT("Inventory", "SplitQuantityHint", "분할 수량"));
    SplitQuantityInput->OnTextChanged.AddUniqueDynamic(this, &ThisClass::OnSplitQuantityChanged);
    SplitQuantityInput->OnTextCommitted.AddUniqueDynamic(this, &ThisClass::OnSplitQuantityCommitted);
    Content->AddChild(SplitQuantityInput);
    SplitErrorText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("SplitErrorText"));
    SplitErrorText->SetColorAndOpacity(FSlateColor(FLinearColor(1.f, 0.55f, 0.45f)));
    SplitErrorText->SetAutoWrapText(true);
    Content->AddChild(SplitErrorText);

    UHorizontalBox* Actions = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("SplitActions"));
    Content->AddChild(Actions);
    SplitConfirmButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("SplitConfirmButton"));
    UTextBlock* ConfirmText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("SplitConfirmText"));
    ConfirmText->SetText(NSLOCTEXT("Inventory", "SplitConfirm", "확인"));
    SplitConfirmButton->AddChild(ConfirmText);
    SplitConfirmButton->OnClicked.AddUniqueDynamic(this, &ThisClass::ConfirmSplit);
    Actions->AddChild(SplitConfirmButton);
    SplitCancelButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("SplitCancelButton"));
    UTextBlock* CancelText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("SplitCancelText"));
    CancelText->SetText(NSLOCTEXT("Inventory", "SplitCancel", "취소"));
    SplitCancelButton->AddChild(CancelText);
    SplitCancelButton->OnClicked.AddUniqueDynamic(this, &ThisClass::CancelSplit);
    Actions->AddChild(SplitCancelButton);
}

bool UInventoryScreenWidget::OpenSplitDialog(FName Source, UItemViewModel* Item)
{
    if (!IsActivated() || !Interaction || !SplitOverlay || SplitSourceItem.IsValid()
        || Interaction->IsDragging() || !Item || !Item->IsAvailable() || Item->GetQuantity() < 2
        || (Source != TEXT("Stash") && Source != TEXT("Bag")))
    {
        return false;
    }
    SplitSourceItem = Item;
    SplitSourceContainer = Source;
    SplitSourceId = Item->GetInstanceId();
    SplitItemChangedHandle = Item->OnChanged().AddUObject(this, &ThisClass::OnSplitSourceChanged);
    SplitRangeText->SetText(FText::Format(NSLOCTEXT("Inventory", "SplitRange", "수량을 선택하세요 (1~{0})"),
        FText::AsNumber(Item->GetQuantity() - 1)));
    SplitErrorText->SetText(FText::GetEmpty());
    SplitQuantityInput->SetText(FText::FromString(FString::FromInt(Item->GetQuantity() / 2)));
    OnSplitQuantityChanged(SplitQuantityInput->GetText());
    SplitOverlay->SetVisibility(ESlateVisibility::Visible);
    LeftGrid->SetIsEnabled(false);
    RightGrid->SetIsEnabled(false);
    CloseButton->SetIsEnabled(false);
    SortLeftButton->SetIsEnabled(false);
    SortRightButton->SetIsEnabled(false);
    if (APlayerController* Owner = GetOwningPlayer()) { SplitQuantityInput->SetUserFocus(Owner); }
    return true;
}

void UInventoryScreenWidget::CloseSplitDialog()
{
    if (UItemViewModel* Item = SplitSourceItem.Get()) { Item->OnChanged().Remove(SplitItemChangedHandle); }
    SplitItemChangedHandle.Reset();
    SplitSourceItem.Reset();
    SplitSourceContainer = NAME_None;
    SplitSourceId = INDEX_NONE;
    if (!SplitOverlay) { return; }
    SplitOverlay->SetVisibility(ESlateVisibility::Collapsed);
    if (LeftGrid) { LeftGrid->SetIsEnabled(true); }
    if (RightGrid) { RightGrid->SetIsEnabled(true); }
    if (CloseButton) { CloseButton->SetIsEnabled(true); }
    if (SortLeftButton) { SortLeftButton->SetIsEnabled(true); }
    if (SortRightButton) { SortRightButton->SetIsEnabled(true); }
    if (IsActivated() && CloseButton)
    {
        if (APlayerController* Owner = GetOwningPlayer()) { CloseButton->SetUserFocus(Owner); }
    }
}

void UInventoryScreenWidget::OnSplitSourceChanged() { CloseSplitDialog(); }

bool UInventoryScreenWidget::ParseSplitQuantity(const FText& Text, int32& OutQuantity) const
{
    const UItemViewModel* Item = SplitSourceItem.Get();
    if (!Item || !Item->IsAvailable() || !SplitQuantityInput) { return false; }
    const FString Value = Text.ToString();
    if (Value.IsEmpty() || Value.Len() > 10) { return false; }
    for (TCHAR Digit : Value)
    {
        if (Digit < TEXT('0') || Digit > TEXT('9')) { return false; }
    }
    int64 Parsed = 0;
    if (!LexTryParseString(Parsed, *Value) || Parsed < 1 || Parsed > MAX_int32
        || Parsed >= Item->GetQuantity()) { return false; }
    OutQuantity = static_cast<int32>(Parsed);
    return true;
}

void UInventoryScreenWidget::OnSplitQuantityChanged(const FText& Text)
{
    int32 Quantity = 0;
    const bool bValid = ParseSplitQuantity(Text, Quantity);
    if (SplitConfirmButton) { SplitConfirmButton->SetIsEnabled(bValid); }
    if (SplitErrorText)
    {
        SplitErrorText->SetText(bValid ? FText::GetEmpty()
            : NSLOCTEXT("Inventory", "SplitInvalidQuantity", "1 이상이며 원래 수량보다 작은 정수를 입력하세요."));
    }
}

void UInventoryScreenWidget::OnSplitQuantityCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
    if (CommitMethod == ETextCommit::OnEnter) { ConfirmSplit(); }
}

void UInventoryScreenWidget::ConfirmSplit()
{
    int32 Quantity = 0;
    if (!SplitSourceItem.IsValid() || !Interaction || !SplitQuantityInput
        || !ParseSplitQuantity(SplitQuantityInput->GetText(), Quantity))
    {
        OnSplitQuantityChanged(SplitQuantityInput ? SplitQuantityInput->GetText() : FText::GetEmpty());
        return;
    }
    const EInventoryOperationFailure Result = Interaction->Split(SplitSourceContainer, SplitSourceId, Quantity);
    if (Result != EInventoryOperationFailure::None && SplitSourceItem.IsValid() && SplitErrorText)
    {
        SplitErrorText->SetText(Interaction->GetFailureText());
    }
}

void UInventoryScreenWidget::CancelSplit() { CloseSplitDialog(); }

UWidget* UInventoryScreenWidget::NativeGetDesiredFocusTarget() const
{
    if (SplitSourceItem.IsValid()) { return SplitQuantityInput.Get(); }
    return CloseButton.Get();
}
bool UInventoryScreenWidget::NativeOnHandleBackAction()
{
    if (SplitSourceItem.IsValid()) { CloseSplitDialog(); return true; }
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
void UInventoryScreenWidget::SortLeft() { if (Interaction && !SplitSourceItem.IsValid()) { Interaction->Sort(TEXT("Stash")); } }
void UInventoryScreenWidget::SortRight() { if (Interaction && !SplitSourceItem.IsValid()) { Interaction->Sort(TEXT("Bag")); } }
