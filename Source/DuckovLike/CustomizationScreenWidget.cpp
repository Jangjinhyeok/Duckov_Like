#include "CustomizationScreenWidget.h"

#include "CustomizationModel.h"
#include "CustomizationViewModel.h"
#include "CustomizationPreviewActor.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
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
#include "Components/WidgetSwitcher.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Input/UIActionBindingHandle.h"

namespace
{
const FLinearColor TabColor(0.09f, 0.14f, 0.22f, 0.95f);
const FLinearColor SelectedTabColor(0.93f, 0.5f, 0.12f);
const FLinearColor SelectedPartColor(0.2f, 0.62f, 0.38f);
const FLinearColor ConfirmColor(0.22f, 0.66f, 0.3f);
const FLinearColor GiveUpColor(0.78f, 0.2f, 0.18f);

UTextBlock* MakeText(UWidgetTree& Tree, FName Name, const FText& Text, int32 Size, const FLinearColor& Color = FLinearColor::White)
{
    auto* Label = Tree.ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
    FSlateFontInfo Font = Label->GetFont();
    Font.Size = Size;
    Label->SetFont(Font);
    Label->SetText(Text);
    Label->SetColorAndOpacity(FSlateColor(Color));
    return Label;
}

// 흰 둥근 brush에 SetBackgroundColor로 색을 입힌다. 선택 표시를 색만 바꿔 보이기 위해서다.
UButton* MakeButton(UWidgetTree& Tree, FName Name, const FText& Text, const FLinearColor& Color, int32 FontSize, const FMargin& Padding)
{
    auto* Button = Tree.ConstructWidget<UButton>(UButton::StaticClass(), Name);
    const float Radius = FontSize * 1.2f;
    FButtonStyle Style = Button->GetStyle();
    Style.SetNormal(FSlateRoundedBoxBrush(FLinearColor::White, Radius));
    Style.SetHovered(FSlateRoundedBoxBrush(FLinearColor::White, Radius, FLinearColor::White, 2.f));
    Style.SetPressed(FSlateRoundedBoxBrush(FLinearColor(0.8f, 0.8f, 0.8f), Radius));
    Style.SetDisabled(FSlateRoundedBoxBrush(FLinearColor(0.5f, 0.5f, 0.5f, 0.6f), Radius));
    Style.SetNormalPadding(Padding);
    Style.SetPressedPadding(Padding);
    Button->SetStyle(Style);
    Button->SetBackgroundColor(Color);
    auto* Label = MakeText(Tree, NAME_None, Text, FontSize);
    Label->SetJustification(ETextJustify::Center);
    Button->AddChild(Label);
    return Button;
}

UBorder* MakePanel(UWidgetTree& Tree, FName Name, float Alpha)
{
    auto* Panel = Tree.ConstructWidget<UBorder>(UBorder::StaticClass(), Name);
    Panel->SetBrush(FSlateRoundedBoxBrush(FLinearColor(0.05f, 0.09f, 0.15f, Alpha), 12.f, FLinearColor(0.25f, 0.35f, 0.45f, 0.6f), 1.f));
    return Panel;
}
}

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

// 원작 배치: 왼쪽 부위 탭, 그 옆 조절 열, 가운데 거울 미리보기, 오른쪽 아래 포기·확인, 왼쪽 아래 보조 버튼.
// 탭은 실제 조절이 있는 부위만 만든다.
void UCustomizationScreenWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    UWidgetTree& Tree = *WidgetTree;
    auto* Root = Tree.ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("CustomizationRoot"));
    Tree.RootWidget = Root;
    auto* Backdrop = Tree.ConstructWidget<UBorder>();
    Backdrop->SetBrushColor(FLinearColor(0.005f, 0.008f, 0.015f, 0.85f));
    auto* BackgroundSlot = Root->AddChildToCanvas(Backdrop);
    BackgroundSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
    BackgroundSlot->SetOffsets(FMargin(0.f));

    auto* Tabs = Tree.ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("TabColumn"));
    auto* TabsSlot = Root->AddChildToCanvas(Tabs);
    TabsSlot->SetAnchors(FAnchors(0.f, 0.5f));
    TabsSlot->SetAlignment(FVector2D(0.f, 0.5f));
    TabsSlot->SetPosition(FVector2D(48.f, -40.f));
    TabsSlot->SetAutoSize(true);

    auto* ControlsPanel = MakePanel(Tree, TEXT("ControlsPanel"), 0.9f);
    ControlsPanel->SetPadding(FMargin(20.f, 16.f));
    auto* ControlsSlot = Root->AddChildToCanvas(ControlsPanel);
    ControlsSlot->SetAnchors(FAnchors(0.f, 0.5f));
    ControlsSlot->SetAlignment(FVector2D(0.f, 0.5f));
    ControlsSlot->SetPosition(FVector2D(196.f, -40.f));
    ControlsSlot->SetAutoSize(true);
    auto* ControlsSize = Tree.ConstructWidget<USizeBox>();
    ControlsSize->SetWidthOverride(360.f);
    ControlsPanel->SetContent(ControlsSize);
    auto* ControlsColumn = Tree.ConstructWidget<UVerticalBox>();
    ControlsSize->AddChild(ControlsColumn);
    TabTitle = MakeText(Tree, TEXT("TabTitle"), FText::GetEmpty(), 22);
    ControlsColumn->AddChildToVerticalBox(TabTitle)->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));
    TabSwitcher = Tree.ConstructWidget<UWidgetSwitcher>(UWidgetSwitcher::StaticClass(), TEXT("TabSwitcher"));
    ControlsColumn->AddChildToVerticalBox(TabSwitcher);
    UTextBlock* Hint = MakeText(Tree, NAME_None,
        NSLOCTEXT("Customization", "RegionsHint", "0이 원래 외형입니다. 유형을 바꿔도 조절 값은 유지됩니다."), 13,
        FLinearColor(0.7f, 0.75f, 0.82f));
    Hint->SetAutoWrapText(true);
    ControlsColumn->AddChildToVerticalBox(Hint)->SetPadding(FMargin(0.f, 14.f, 0.f, 0.f));

    UVerticalBox* Page = nullptr;
    auto Navigate = [this](FName Function)
    {
        FCustomWidgetNavigationDelegate Delegate;
        Delegate.BindUFunction(this, Function);
        return Delegate;
    };
    auto AddTab = [&](FName Name, const FText& Text)
    {
        UButton* Tab = MakeButton(Tree, Name, Text, TabColor, 17, FMargin(12.f, 8.f));
        auto* TabSize = Tree.ConstructWidget<USizeBox>();
        TabSize->SetWidthOverride(124.f);
        TabSize->AddChild(Tab);
        Tabs->AddChildToVerticalBox(TabSize)->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));
        Tab->SetNavigationRuleCustom(EUINavigation::Right, Navigate(GET_FUNCTION_NAME_CHECKED(ThisClass, NavigateToControls)));
        TabButtons.Add(Tab);
        TabTitles.Add(Text);
        Page = Tree.ConstructWidget<UVerticalBox>();
        TabSwitcher->AddChild(Page);
        TabFirstControls.AddDefaulted();
        return Tab;
    };
    auto AddFirstControl = [&](UWidget* Control)
    {
        if (TabFirstControls.Last()) { return; }
        TabFirstControls.Last() = Control;
        Control->SetNavigationRuleCustom(EUINavigation::Up, Navigate(GET_FUNCTION_NAME_CHECKED(ThisClass, NavigateToTab)));
    };
    auto AddSlider = [&](FName Name, const FText& Text, float Min, float Max)
    {
        auto* Header = Tree.ConstructWidget<UHorizontalBox>();
        Header->AddChildToHorizontalBox(MakeText(Tree, NAME_None, Text, 16))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        UTextBlock* Value = MakeText(Tree, NAME_None, FText::GetEmpty(), 16, FLinearColor(0.95f, 0.75f, 0.45f));
        Header->AddChildToHorizontalBox(Value);
        Page->AddChildToVerticalBox(Header)->SetPadding(FMargin(0.f, 10.f, 0.f, 2.f));
        auto* Slider = Tree.ConstructWidget<USlider>(USlider::StaticClass(), Name);
        Slider->SetMinValue(Min);
        Slider->SetMaxValue(Max);
        Page->AddChildToVerticalBox(Slider)->SetPadding(FMargin(0.f, 4.f));
        AddFirstControl(Slider);
        ValueSliders.Add(Slider);
        ValueTexts.Add(Value);
        return Slider;
    };

    AddTab(TEXT("HeadTab"), NSLOCTEXT("Customization", "HeadTab", "머리"))->OnClicked.AddUniqueDynamic(this, &ThisClass::ShowHeadTab);
    Page->AddChildToVerticalBox(MakeText(Tree, NAME_None, NSLOCTEXT("Customization", "CrestType", "깃 유형"), 16));
    auto* PartRow = Tree.ConstructWidget<UHorizontalBox>();
    Page->AddChildToVerticalBox(PartRow)->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f));
    PartA = MakeButton(Tree, TEXT("PartA"), NSLOCTEXT("Customization", "A", "A · 세 갈래 깃"), TabColor, 16, FMargin(12.f, 8.f));
    PartB = MakeButton(Tree, TEXT("PartB"), NSLOCTEXT("Customization", "B", "B · 두 갈래 깃"), TabColor, 16, FMargin(12.f, 8.f));
    PartRow->AddChildToHorizontalBox(PartA)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    PartRow->AddChildToHorizontalBox(PartB)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    Cast<UHorizontalBoxSlot>(PartA->Slot)->SetPadding(FMargin(0.f, 0.f, 6.f, 0.f));
    AddFirstControl(PartA);
    PartA->SetNavigationRuleCustom(EUINavigation::Left, Navigate(GET_FUNCTION_NAME_CHECKED(ThisClass, NavigateToTab)));
    PartA->OnClicked.AddUniqueDynamic(this, &ThisClass::SelectA);
    PartB->OnClicked.AddUniqueDynamic(this, &ThisClass::SelectB);
    ShapeSlider = AddSlider(TEXT("ShapeSlider"), NSLOCTEXT("Customization", "CrestShape", "깃 모양"), -1.f, 1.f);
    ShapeSlider->OnValueChanged.AddUniqueDynamic(this, &ThisClass::ChangeShape);
    HueSlider = AddSlider(TEXT("HueSlider"), NSLOCTEXT("Customization", "Hue", "깃털 색"), 0.f, 1.f);
    HueSlider->OnValueChanged.AddUniqueDynamic(this, &ThisClass::ChangeHue);

    AddTab(TEXT("EyeTab"), NSLOCTEXT("Customization", "EyeTab", "눈"))->OnClicked.AddUniqueDynamic(this, &ThisClass::ShowEyeTab);
    EyeSizeSlider = AddSlider(TEXT("EyeSizeSlider"), NSLOCTEXT("Customization", "EyeSize", "크기 · 작게 ↔ 크게"), -1.f, 1.f);
    EyeSizeSlider->OnValueChanged.AddUniqueDynamic(this, &ThisClass::ChangeEyeSize);
    AddTab(TEXT("BeakTab"), NSLOCTEXT("Customization", "BeakTab", "부리"))->OnClicked.AddUniqueDynamic(this, &ThisClass::ShowBeakTab);
    BeakLengthSlider = AddSlider(TEXT("BeakLengthSlider"), NSLOCTEXT("Customization", "BeakLength", "길이 · 짧게 ↔ 길게"), -1.f, 1.f);
    BeakLengthSlider->OnValueChanged.AddUniqueDynamic(this, &ThisClass::ChangeBeakLength);
    AddTab(TEXT("BodyTab"), NSLOCTEXT("Customization", "BodyTab", "몸통"))->OnClicked.AddUniqueDynamic(this, &ThisClass::ShowBodyTab);
    BodyLengthSlider = AddSlider(TEXT("BodyLengthSlider"), NSLOCTEXT("Customization", "BodyLength", "앞뒤 길이 · 짧게 ↔ 길게"), -1.f, 1.f);
    BodyLengthSlider->OnValueChanged.AddUniqueDynamic(this, &ThisClass::ChangeBodyLength);

    // 가운데 거울: 둥근 테두리 안에 미리보기, 그 아래 실패 사유.
    auto* PreviewColumn = Tree.ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("PreviewColumn"));
    auto* PreviewSlot = Root->AddChildToCanvas(PreviewColumn);
    PreviewSlot->SetAnchors(FAnchors(0.5f));
    PreviewSlot->SetAlignment(FVector2D(0.5f));
    PreviewSlot->SetPosition(FVector2D(80.f, -30.f));
    PreviewSlot->SetAutoSize(true);
    auto* Mirror = Tree.ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("MirrorFrame"));
    Mirror->SetBrush(FSlateRoundedBoxBrush(FLinearColor(0.02f, 0.03f, 0.05f, 1.f), 18.f, FLinearColor(0.75f, 0.8f, 0.88f, 0.9f), 4.f));
    Mirror->SetPadding(FMargin(10.f));
    PreviewColumn->AddChildToVerticalBox(Mirror);
    auto* ImageSize = Tree.ConstructWidget<USizeBox>();
    ImageSize->SetWidthOverride(512.f);
    ImageSize->SetHeightOverride(512.f);
    Mirror->SetContent(ImageSize);
    PreviewImage = Tree.ConstructWidget<UImage>(UImage::StaticClass(), TEXT("PreviewImage"));
    PreviewImage->SetDesiredSizeOverride(FVector2D(512.f));
    ImageSize->AddChild(PreviewImage);
    FailureText = MakeText(Tree, TEXT("FailureText"), FText::GetEmpty(), 16, FLinearColor(1.f, 0.6f, 0.4f));
    FailureText->SetAutoWrapText(true);
    FailureText->SetWrapTextAt(532.f);
    FailureText->SetJustification(ETextJustify::Center);
    FailureText->SetShadowOffset(FVector2D(1.f, 1.f));
    PreviewColumn->AddChildToVerticalBox(FailureText)->SetPadding(FMargin(0.f, 10.f, 0.f, 0.f));

    // 원작의 프리셋·클립보드 자리. 이 프로젝트의 보조 동작인 초기화와 개발용 실패 시험을 둔다.
    auto* Utility = Tree.ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("UtilityRow"));
    auto* UtilitySlot = Root->AddChildToCanvas(Utility);
    UtilitySlot->SetAnchors(FAnchors(0.f, 1.f));
    UtilitySlot->SetAlignment(FVector2D(0.f, 1.f));
    UtilitySlot->SetPosition(FVector2D(48.f, -48.f));
    UtilitySlot->SetAutoSize(true);
    UButton* ResetButton = MakeButton(Tree, TEXT("ResetButton"), NSLOCTEXT("Customization", "Reset", "초기화"), TabColor, 16, FMargin(20.f, 9.f));
    ResetButton->OnClicked.AddUniqueDynamic(this, &ThisClass::Reset);
    Utility->AddChildToHorizontalBox(ResetButton)->SetPadding(FMargin(0.f, 0.f, 10.f, 0.f));
    UButton* FailureButton = MakeButton(Tree, TEXT("FailureButton"), NSLOCTEXT("Customization", "FailureProbe", "개발용 실패 시험"),
        FLinearColor(0.2f, 0.22f, 0.26f, 0.95f), 14, FMargin(16.f, 9.f));
    FailureButton->OnClicked.AddUniqueDynamic(this, &ThisClass::TestFailure);
    Utility->AddChildToHorizontalBox(FailureButton)->SetVerticalAlignment(VAlign_Center);

    // 원작처럼 오른쪽 아래에 포기(빨강)와 확인(초록, 큼).
    auto* Actions = Tree.ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("ActionRow"));
    auto* ActionsSlot = Root->AddChildToCanvas(Actions);
    ActionsSlot->SetAnchors(FAnchors(1.f, 1.f));
    ActionsSlot->SetAlignment(FVector2D(1.f, 1.f));
    ActionsSlot->SetPosition(FVector2D(-48.f, -48.f));
    ActionsSlot->SetAutoSize(true);
    UButton* CancelButton = MakeButton(Tree, TEXT("CancelButton"), NSLOCTEXT("Customization", "Cancel", "포기"), GiveUpColor, 18, FMargin(28.f, 10.f));
    CancelButton->OnClicked.AddUniqueDynamic(this, &ThisClass::Cancel);
    Actions->AddChildToHorizontalBox(CancelButton)->SetVerticalAlignment(VAlign_Center);
    Cast<UHorizontalBoxSlot>(CancelButton->Slot)->SetPadding(FMargin(0.f, 0.f, 14.f, 0.f));
    UButton* ApplyButton = MakeButton(Tree, TEXT("ApplyButton"), NSLOCTEXT("Customization", "Apply", "확인"), ConfirmColor, 24, FMargin(52.f, 14.f));
    ApplyButton->OnClicked.AddUniqueDynamic(this, &ThisClass::Apply);
    Actions->AddChildToHorizontalBox(ApplyButton);
    ViewModel = NewObject<UCustomizationViewModel>(this);
}

void UCustomizationScreenWidget::NativeOnActivated()
{
    Super::NativeOnActivated();
    if (!Model || !GetWorld()) { DeactivateWidget(); return; }
    SelectTab(0);
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

void UCustomizationScreenWidget::SelectTab(int32 Index)
{
    if (!TabButtons.IsValidIndex(Index)) { return; }
    TabSwitcher->SetActiveWidgetIndex(Index);
    for (int32 Tab = 0; Tab < TabButtons.Num(); ++Tab)
    {
        TabButtons[Tab]->SetBackgroundColor(Tab == Index ? SelectedTabColor : TabColor);
    }
    TabTitle->SetText(TabTitles[Index]);
}
void UCustomizationScreenWidget::ShowHeadTab() { SelectTab(0); }
void UCustomizationScreenWidget::ShowEyeTab() { SelectTab(1); }
void UCustomizationScreenWidget::ShowBeakTab() { SelectTab(2); }
void UCustomizationScreenWidget::ShowBodyTab() { SelectTab(3); }
// 감춘 탭의 입력으로는 갈 수 없으므로 현재 탭 기준으로 대상을 고른다.
UWidget* UCustomizationScreenWidget::NavigateToTab(EUINavigation) { return TabButtons[TabSwitcher->GetActiveWidgetIndex()]; }
UWidget* UCustomizationScreenWidget::NavigateToControls(EUINavigation) { return TabFirstControls[TabSwitcher->GetActiveWidgetIndex()]; }

void UCustomizationScreenWidget::Refresh()
{
    if (!ViewModel) { return; }
    TGuardValue<bool> Guard(bSynchronizing, true);
    const bool bEditing = Model && Model->IsEditing();
    PartA->SetIsEnabled(bEditing);
    PartB->SetIsEnabled(bEditing);
    HueSlider->SetIsEnabled(bEditing);
    ShapeSlider->SetIsEnabled(bEditing);
    EyeSizeSlider->SetIsEnabled(bEditing);
    BeakLengthSlider->SetIsEnabled(bEditing);
    BodyLengthSlider->SetIsEnabled(bEditing);
    PartA->SetBackgroundColor(ViewModel->GetPartId() == TEXT("A") ? SelectedPartColor : TabColor);
    PartB->SetBackgroundColor(ViewModel->GetPartId() == TEXT("B") ? SelectedPartColor : TabColor);
    HueSlider->SetValue(ViewModel->GetHue());
    ShapeSlider->SetValue(ViewModel->GetShape());
    EyeSizeSlider->SetValue(ViewModel->GetEyeSize());
    BeakLengthSlider->SetValue(ViewModel->GetBeakLength());
    BodyLengthSlider->SetValue(ViewModel->GetBodyLength());
    for (int32 Index = 0; Index < ValueSliders.Num(); ++Index)
    {
        ValueTexts[Index]->SetText(FText::FromString(FString::Printf(TEXT("%.2f"), ValueSliders[Index]->GetValue())));
    }
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
// Slider는 delegate 전에 자체 값을 바꾼다. 같은 실패가 반복돼도 Model의 정상 값으로 되돌린다.
void UCustomizationScreenWidget::ChangeHue(float Value)
{
    if (!bSynchronizing && ViewModel && ViewModel->SetHue(Value) != ECustomizationFailure::None) { Refresh(); }
}
void UCustomizationScreenWidget::ChangeShape(float Value)
{
    if (!bSynchronizing && ViewModel && ViewModel->SetShape(Value) != ECustomizationFailure::None) { Refresh(); }
}
void UCustomizationScreenWidget::ChangeEyeSize(float Value)
{
    if (!bSynchronizing && ViewModel && ViewModel->SetEyeSize(Value) != ECustomizationFailure::None) { Refresh(); }
}
void UCustomizationScreenWidget::ChangeBeakLength(float Value)
{
    if (!bSynchronizing && ViewModel && ViewModel->SetBeakLength(Value) != ECustomizationFailure::None) { Refresh(); }
}
void UCustomizationScreenWidget::ChangeBodyLength(float Value)
{
    if (!bSynchronizing && ViewModel && ViewModel->SetBodyLength(Value) != ECustomizationFailure::None) { Refresh(); }
}
void UCustomizationScreenWidget::Reset() { if (ViewModel) { ViewModel->Reset(); } }
void UCustomizationScreenWidget::TestFailure() { if (ViewModel) { ViewModel->SelectPart(TEXT("Unregistered")); } }
void UCustomizationScreenWidget::Apply()
{
    if (ViewModel && ViewModel->Apply() == ECustomizationFailure::None)
    {
        DeactivateWidget();
        Closed.Broadcast(true);
    }
}
void UCustomizationScreenWidget::Cancel()
{
    if (!Model || !Model->IsEditing())
    {
        DeactivateWidget();
        Closed.Broadcast(false);
        return;
    }
    if (ViewModel && ViewModel->Cancel() == ECustomizationFailure::None)
    {
        DeactivateWidget();
        Closed.Broadcast(false);
    }
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
