#include "CustomizationScreenWidget.h"

#include "CustomizationModel.h"
#include "CustomizationViewModel.h"
#include "CustomizationPreviewActor.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Input/UIActionBindingHandle.h"

UCustomizationScreenWidget::UCustomizationScreenWidget(const FObjectInitializer& Initializer)
    : Super(Initializer)
{
    bIsBackHandler = true;
    bSetVisibilityOnActivated = true;
    bSetVisibilityOnDeactivated = true;
    ActivatedVisibility = ESlateVisibility::Visible;
    DeactivatedVisibility = ESlateVisibility::Collapsed;
    SetVisibility(ESlateVisibility::Collapsed);
}

void UCustomizationScreenWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    auto* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("CustomizationRoot"));
    WidgetTree->RootWidget = Root;
    auto* Backdrop = WidgetTree->ConstructWidget<UBorder>();
    Backdrop->SetBrushColor(FLinearColor(0.005f, 0.008f, 0.015f, 0.96f));
    auto* BackgroundSlot = Root->AddChildToCanvas(Backdrop);
    BackgroundSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
    BackgroundSlot->SetOffsets(FMargin(0.f));
    auto* Panel = WidgetTree->ConstructWidget<UBorder>();
    Panel->SetBrushColor(FLinearColor(0.05f, 0.065f, 0.095f, 1.f));
    Panel->SetPadding(FMargin(24.f));
    auto* PanelSlot = Root->AddChildToCanvas(Panel);
    PanelSlot->SetAnchors(FAnchors(0.5f));
    PanelSlot->SetAlignment(FVector2D(0.5f));
    PanelSlot->SetAutoSize(true);
    auto* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
    Panel->AddChild(Row);
    auto* ControlsSize = WidgetTree->ConstructWidget<USizeBox>();
    ControlsSize->SetWidthOverride(410.f);
    Row->AddChildToHorizontalBox(ControlsSize)->SetPadding(FMargin(0.f, 0.f, 28.f, 0.f));
    auto* Controls = WidgetTree->ConstructWidget<UVerticalBox>();
    ControlsSize->AddChild(Controls);
    auto AddLabel = [&](const FText& Text, int32 FontSize)
    {
        auto* Label = WidgetTree->ConstructWidget<UTextBlock>();
        FSlateFontInfo Font = Label->GetFont();
        Font.Size = FontSize;
        Label->SetFont(Font);
        Label->SetText(Text);
        Label->SetAutoWrapText(true);
        Controls->AddChildToVerticalBox(Label)->SetPadding(FMargin(0.f, 6.f));
        return Label;
    };
    AddLabel(NSLOCTEXT("Customization", "Title", "외형 편집 · 최소 prototype"), 24);
    AddLabel(NSLOCTEXT("Customization", "Contract", "파츠 A/B는 색상과 형상 값을 유지합니다.\n적용 전 편집은 확정 값을 바꾸지 않습니다."), 16);
    auto AddButton = [&](FName Name, const FText& Text)
    {
        auto* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
        auto* Label = WidgetTree->ConstructWidget<UTextBlock>();
        FSlateFontInfo Font = Label->GetFont();
        Font.Size = 18;
        Label->SetFont(Font);
        Label->SetText(Text);
        Button->AddChild(Label);
        Controls->AddChildToVerticalBox(Button)->SetPadding(FMargin(0.f, 4.f));
        return Button;
    };
    PartA = AddButton(TEXT("PartA"), NSLOCTEXT("Customization", "A", "A · Cube"));
    PartB = AddButton(TEXT("PartB"), NSLOCTEXT("Customization", "B", "B · Sphere"));
    PartA->OnClicked.AddUniqueDynamic(this, &ThisClass::SelectA);
    PartB->OnClicked.AddUniqueDynamic(this, &ThisClass::SelectB);
    AddLabel(NSLOCTEXT("Customization", "Hue", "색상 · Hue 0~1"), 16);
    HueSlider = WidgetTree->ConstructWidget<USlider>(USlider::StaticClass(), TEXT("HueSlider"));
    Controls->AddChildToVerticalBox(HueSlider)->SetPadding(FMargin(0.f, 8.f));
    HueSlider->OnValueChanged.AddUniqueDynamic(this, &ThisClass::ChangeHue);
    AddLabel(NSLOCTEXT("Customization", "Shape", "형상 · -1~1 (임시 scale 표시)"), 16);
    ShapeSlider = WidgetTree->ConstructWidget<USlider>(USlider::StaticClass(), TEXT("ShapeSlider"));
    ShapeSlider->SetMinValue(-1.f);
    ShapeSlider->SetMaxValue(1.f);
    Controls->AddChildToVerticalBox(ShapeSlider)->SetPadding(FMargin(0.f, 8.f));
    ShapeSlider->OnValueChanged.AddUniqueDynamic(this, &ThisClass::ChangeShape);
    ValueText = AddLabel(FText::GetEmpty(), 16);
    AddButton(TEXT("ResetButton"), NSLOCTEXT("Customization", "Reset", "초기화 · 편집 값만 기본값으로"))->OnClicked.AddUniqueDynamic(this, &ThisClass::Reset);
    AddButton(TEXT("ApplyButton"), NSLOCTEXT("Customization", "Apply", "적용 · 메모리에 확정"))->OnClicked.AddUniqueDynamic(this, &ThisClass::Apply);
    AddButton(TEXT("CancelButton"), NSLOCTEXT("Customization", "Cancel", "취소 · Back"))->OnClicked.AddUniqueDynamic(this, &ThisClass::Cancel);
    AddButton(TEXT("FailureButton"), NSLOCTEXT("Customization", "FailureProbe", "개발용 실패 시험 · 미등록 ID"))->OnClicked.AddUniqueDynamic(this, &ThisClass::TestFailure);
    FailureText = AddLabel(FText::GetEmpty(), 16);
    FailureText->SetColorAndOpacity(FSlateColor(FLinearColor(1.f, 0.6f, 0.4f)));
    auto* PreviewColumn = WidgetTree->ConstructWidget<UVerticalBox>();
    Row->AddChildToHorizontalBox(PreviewColumn);
    auto* ImageSize = WidgetTree->ConstructWidget<USizeBox>();
    ImageSize->SetWidthOverride(400.f);
    ImageSize->SetHeightOverride(400.f);
    PreviewColumn->AddChildToVerticalBox(ImageSize);
    PreviewImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("PreviewImage"));
    PreviewImage->SetDesiredSizeOverride(FVector2D(400.f));
    ImageSize->AddChild(PreviewImage);
    auto* Notice = WidgetTree->ConstructWidget<UTextBlock>();
    FSlateFontInfo NoticeFont = Notice->GetFont();
    NoticeFont.Size = 16;
    Notice->SetFont(NoticeFont);
    Notice->SetText(NSLOCTEXT("Customization", "PlaceholderNotice", "엔진 임시 mesh 미리보기\nShape는 scale 대체 표시입니다.\n실제 Skeleton·Morph 검증은 C3에 남습니다."));
    PreviewColumn->AddChildToVerticalBox(Notice)->SetPadding(FMargin(0.f, 16.f));
    ViewModel = NewObject<UCustomizationViewModel>(this);
}

void UCustomizationScreenWidget::NativeOnActivated()
{
    Super::NativeOnActivated();
    if (!Model || !GetWorld()) { DeactivateWidget(); return; }
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    Preview = GetWorld()->SpawnActor<ACustomizationPreviewActor>(FVector(0.f, 0.f, -10000.f), FRotator::ZeroRotator, Params);
    if (!Preview) { FailureText->SetText(GetCustomizationFailureText(ECustomizationFailure::PreparationFailed)); return; }
    Preview->InitializeCapture();
    ViewModel->Bind(Model, Preview->GetAppearance());
    StateHandle = ViewModel->AddFieldValueChangedDelegate(UCustomizationViewModel::FFieldNotificationClassDescriptor::GetShape,
        INotifyFieldValueChanged::FFieldValueChangedDelegate::CreateUObject(this, &ThisClass::OnStateChanged));
    FailureHandle = ViewModel->AddFieldValueChangedDelegate(UCustomizationViewModel::FFieldNotificationClassDescriptor::GetFailureText,
        INotifyFieldValueChanged::FFieldValueChangedDelegate::CreateUObject(this, &ThisClass::OnFailureChanged));
    ViewModel->Open();
    Refresh();
    if (GetOwningPlayer()) { PartA->SetUserFocus(GetOwningPlayer()); }
}

void UCustomizationScreenWidget::Refresh()
{
    if (!ViewModel) { return; }
    TGuardValue<bool> Guard(bSynchronizing, true);
    const bool bEditing = Model && Model->IsEditing();
    PartA->SetIsEnabled(bEditing);
    PartB->SetIsEnabled(bEditing);
    HueSlider->SetIsEnabled(bEditing);
    ShapeSlider->SetIsEnabled(bEditing);
    PartA->SetBackgroundColor(ViewModel->GetPartId() == TEXT("A") ? FLinearColor(0.3f, 0.7f, 0.5f) : FLinearColor::White);
    PartB->SetBackgroundColor(ViewModel->GetPartId() == TEXT("B") ? FLinearColor(0.3f, 0.7f, 0.5f) : FLinearColor::White);
    HueSlider->SetValue(ViewModel->GetHue());
    ShapeSlider->SetValue(ViewModel->GetShape());
    ValueText->SetText(FText::FromString(FString::Printf(TEXT("선택 %s · Hue %.2f · Shape %.2f"),
        *ViewModel->GetPartId().ToString(), ViewModel->GetHue(), ViewModel->GetShape())));
    FailureText->SetText(ViewModel->GetFailureText());
    if (Preview)
    {
        PreviewImage->SetBrushResourceObject(Preview->GetRenderTarget());
        Preview->CapturePreview();
    }
}

void UCustomizationScreenWidget::OnStateChanged(UObject*, UE::FieldNotification::FFieldId) { Refresh(); }
void UCustomizationScreenWidget::OnFailureChanged(UObject*, UE::FieldNotification::FFieldId)
{
    FailureText->SetText(ViewModel->GetFailureText());
}
void UCustomizationScreenWidget::SelectA() { if (ViewModel) { ViewModel->SelectPart(TEXT("A")); } }
void UCustomizationScreenWidget::SelectB() { if (ViewModel) { ViewModel->SelectPart(TEXT("B")); } }
void UCustomizationScreenWidget::ChangeHue(float Value) { if (!bSynchronizing && ViewModel) { ViewModel->SetHue(Value); } }
void UCustomizationScreenWidget::ChangeShape(float Value) { if (!bSynchronizing && ViewModel) { ViewModel->SetShape(Value); } }
void UCustomizationScreenWidget::Reset() { if (ViewModel) { ViewModel->Reset(); } }
void UCustomizationScreenWidget::TestFailure() { if (ViewModel) { ViewModel->SelectPart(TEXT("Unregistered")); } }
void UCustomizationScreenWidget::Apply()
{
    if (ViewModel && ViewModel->Apply() == ECustomizationFailure::None) { DeactivateWidget(); }
}
void UCustomizationScreenWidget::Cancel()
{
    if (!Model || !Model->IsEditing()) { DeactivateWidget(); return; }
    if (ViewModel && ViewModel->Cancel() == ECustomizationFailure::None) { DeactivateWidget(); }
}
bool UCustomizationScreenWidget::NativeOnHandleBackAction() { Cancel(); return true; }
UWidget* UCustomizationScreenWidget::NativeGetDesiredFocusTarget() const { return PartA; }
TOptional<FUIInputConfig> UCustomizationScreenWidget::GetDesiredInputConfig() const
{
    return FUIInputConfig(ECommonInputMode::Menu, EMouseCaptureMode::NoCapture);
}
void UCustomizationScreenWidget::ReleasePreview()
{
    if (ViewModel)
    {
        ViewModel->RemoveFieldValueChangedDelegate(UCustomizationViewModel::FFieldNotificationClassDescriptor::GetShape, StateHandle);
        ViewModel->RemoveFieldValueChangedDelegate(UCustomizationViewModel::FFieldNotificationClassDescriptor::GetFailureText, FailureHandle);
        StateHandle.Reset();
        FailureHandle.Reset();
        ViewModel->Bind(nullptr, nullptr);
    }
    if (PreviewImage) { PreviewImage->SetBrushResourceObject(nullptr); }
    if (Preview) { Preview->Destroy(); Preview = nullptr; }
}
void UCustomizationScreenWidget::NativeOnDeactivated()
{
    if (ViewModel && Model && Model->IsEditing()) { ViewModel->Cancel(); }
    ReleasePreview();
    Super::NativeOnDeactivated();
}
void UCustomizationScreenWidget::NativeDestruct()
{
    if (IsActivated()) { DeactivateWidget(); }
    ReleasePreview();
    Super::NativeDestruct();
}
