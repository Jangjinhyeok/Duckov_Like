#include "CustomizationScreenWidget.h"

#include "CustomizationCatalog.h"
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
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
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
UButton* MakeButton(UWidgetTree& Tree, FName Name, const FText& Text, const FLinearColor& Color, int32 FontSize, const FMargin& Padding,
    TSubclassOf<UButton> ButtonClass = UButton::StaticClass())
{
    auto* Button = Tree.ConstructWidget<UButton>(ButtonClass, Name);
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

FString SlotName(ECustomizationSlot Slot)
{
    return StaticEnum<ECustomizationSlot>()->GetNameStringByValue(static_cast<int64>(Slot));
}

// 색 대상 이름이다. 0은 피부, 1부터 슬롯이다.
FString ColorTargetName(int32 Target)
{
    return Target == 0 ? FString(TEXT("Skin")) : SlotName(static_cast<ECustomizationSlot>(Target - 1));
}

// 밝은 swatch 위에서는 검정, 어두운 swatch 위에서는 흰 선택 표시를 쓴다.
FLinearColor MarkColor(const FLinearColor& Swatch)
{
    return Swatch.GetLuminance() > 0.35f ? FLinearColor::Black : FLinearColor::White;
}

UBorder* MakePanel(UWidgetTree& Tree, FName Name, float Alpha)
{
    auto* Panel = Tree.ConstructWidget<UBorder>(UBorder::StaticClass(), Name);
    Panel->SetBrush(FSlateRoundedBoxBrush(FLinearColor(0.05f, 0.09f, 0.15f, Alpha), 12.f, FLinearColor(0.25f, 0.35f, 0.45f, 0.6f), 1.f));
    return Panel;
}
}

void UCustomizationPartButton::Setup(ECustomizationSlot InSlot, FName InPartId)
{
    PartSlot = InSlot;
    PartId = InPartId;
    OnClicked.AddUniqueDynamic(this, &UCustomizationPartButton::Forward);
}

void UCustomizationPartButton::Forward() { Picked.Broadcast(*this); }

void UCustomizationColorButton::Setup(int32 InTarget, int32 InPaletteIndex)
{
    Target = InTarget;
    PaletteIndex = InPaletteIndex;
    OnClicked.AddUniqueDynamic(this, &UCustomizationColorButton::Forward);
}

void UCustomizationColorButton::Forward() { Picked.Broadcast(*this); }

void UCustomizationTabButton::Setup(int32 InIndex)
{
    Index = InIndex;
    OnClicked.AddUniqueDynamic(this, &UCustomizationTabButton::Forward);
}

void UCustomizationTabButton::Forward() { Picked.Broadcast(Index); }

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
// 탭은 원작과 같은 일곱 부위다. 머리는 피부색·머리 모양, 나머지는 유형과 색상(발은 유형만)이다.
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
        NSLOCTEXT("Customization", "PartsHint", "몸통은 모두 같습니다. 유형을 바꿔도 고른 색은 유지됩니다."), 13,
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
        auto* Tab = CastChecked<UCustomizationTabButton>(MakeButton(Tree, Name, Text, TabColor, 17, FMargin(12.f, 6.f),
            UCustomizationTabButton::StaticClass()));
        Tab->Setup(TabButtons.Num());
        Tab->OnPicked().AddUObject(this, &ThisClass::SelectTab);
        auto* TabSize = Tree.ConstructWidget<USizeBox>();
        TabSize->SetWidthOverride(124.f);
        TabSize->AddChild(Tab);
        Tabs->AddChildToVerticalBox(TabSize)->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
        Tab->SetNavigationRuleCustom(EUINavigation::Right, Navigate(GET_FUNCTION_NAME_CHECKED(ThisClass, NavigateToControls)));
        TabButtons.Add(Tab);
        TabTitles.Add(Text);
        Page = Tree.ConstructWidget<UVerticalBox>();
        TabSwitcher->AddChild(Page);
        TabFirstControls.AddDefaulted();
    };
    auto AddFirstControl = [&](UWidget* Control)
    {
        if (TabFirstControls.Last()) { return; }
        TabFirstControls.Last() = Control;
        Control->SetNavigationRuleCustom(EUINavigation::Up, Navigate(GET_FUNCTION_NAME_CHECKED(ThisClass, NavigateToTab)));
    };
    auto AddHeader = [&](const FText& Text)
    {
        Page->AddChildToVerticalBox(MakeText(Tree, NAME_None, Text, 16))->SetPadding(FMargin(0.f, 10.f, 0.f, 0.f));
    };

    // 슬롯의 유형을 카탈로그 row 순서대로 세로 목록에 만든다. 코드에는 파츠 ID나 이름이 없다.
    // 헤어는 맨 위에 "없음"을 둔다. 모든 항목은 왼쪽 키로 탭에 돌아간다.
    const FCustomizationCatalog Catalog = FCustomizationCatalog::LoadDefault();
    auto AddPartList = [&](ECustomizationSlot SlotKind)
    {
        AddHeader(FText::Format(NSLOCTEXT("Customization", "PartType", "{0} 유형"), GetCustomizationSlotText(SlotKind)));
        // 선택지가 늘어도 같은 높이 안에서 스크롤되는 세로 목록이다.
        auto* ListSize = Tree.ConstructWidget<USizeBox>();
        ListSize->SetMaxDesiredHeight(176.f);
        Page->AddChildToVerticalBox(ListSize)->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f));
        auto* List = Tree.ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), FName(*(SlotName(SlotKind) + TEXT("List"))));
        ListSize->AddChild(List);
        auto AddChoice = [&](FName PartId, const FText& Label)
        {
            const FName Name(*(TEXT("Part") + (PartId.IsNone() ? SlotName(SlotKind) + TEXT("None") : PartId.ToString())));
            auto* Button = CastChecked<UCustomizationPartButton>(MakeButton(Tree, Name, Label, TabColor, 16, FMargin(12.f, 8.f),
                UCustomizationPartButton::StaticClass()));
            Button->Setup(SlotKind, PartId);
            Button->OnPicked().AddUObject(this, &ThisClass::HandlePartPicked);
            Cast<UScrollBoxSlot>(List->AddChild(Button))->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
            Button->SetNavigationRuleCustom(EUINavigation::Left, Navigate(GET_FUNCTION_NAME_CHECKED(ThisClass, NavigateToTab)));
            AddFirstControl(Button);
            PartButtons.Add(Button);
        };
        if (!IsCustomizationSlotRequired(SlotKind)) { AddChoice(NAME_None, NSLOCTEXT("Customization", "NoPart", "없음")); }
        for (const FName PartId : Catalog.GetPartIds(SlotKind))
        {
            AddChoice(PartId, Catalog.Find(PartId)->DisplayName);
        }
    };
    // 원작처럼 색상 칸을 누르면 아래로 swatch 격자가 펼쳐진다. 색을 고르면 접힌다.
    auto AddPalette = [&](int32 Target, const FText& Title)
    {
        AddHeader(Title);
        const FString Prefix = ColorTargetName(Target);
        auto* Toggle = CastChecked<UCustomizationColorButton>(MakeButton(Tree, FName(*(Prefix + TEXT("Color"))), FText::GetEmpty(),
            FLinearColor::White, 12, FMargin(8.f, 6.f), UCustomizationColorButton::StaticClass()));
        Toggle->Setup(Target, INDEX_NONE);
        Toggle->OnPicked().AddUObject(this, &ThisClass::HandleColorPicked);
        Toggle->SetNavigationRuleCustom(EUINavigation::Left, Navigate(GET_FUNCTION_NAME_CHECKED(ThisClass, NavigateToTab)));
        Page->AddChildToVerticalBox(Toggle)->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f));
        AddFirstControl(Toggle);
        ColorButtons.Add(Toggle);
        auto* Grid = Tree.ConstructWidget<UUniformGridPanel>(UUniformGridPanel::StaticClass(), FName(*(Prefix + TEXT("Palette"))));
        Grid->SetSlotPadding(FMargin(2.f));
        Grid->SetVisibility(ESlateVisibility::Collapsed);
        Page->AddChildToVerticalBox(Grid)->SetPadding(FMargin(0.f, 4.f, 0.f, 0.f));
        const TConstArrayView<FLinearColor> Palette = GetCustomizationPalette();
        for (int32 Index = 0; Index < Palette.Num(); ++Index)
        {
            auto* Swatch = CastChecked<UCustomizationColorButton>(MakeButton(Tree, FName(*FString::Printf(TEXT("%sSwatch%d"), *Prefix, Index)),
                FText::GetEmpty(), Palette[Index], 12, FMargin(4.f, 4.f), UCustomizationColorButton::StaticClass()));
            Swatch->Setup(Target, Index);
            Swatch->OnPicked().AddUObject(this, &ThisClass::HandleColorPicked);
            auto* SwatchSize = Tree.ConstructWidget<USizeBox>();
            SwatchSize->SetWidthOverride(38.f);
            SwatchSize->SetHeightOverride(30.f);
            SwatchSize->AddChild(Swatch);
            Grid->AddChildToUniformGrid(SwatchSize, Index / 8, Index % 8);
            ColorButtons.Add(Swatch);
        }
        if (PaletteGrids.Num() <= Target) { PaletteGrids.SetNum(Target + 1); }
        PaletteGrids[Target] = Grid;
    };

    AddTab(TEXT("HeadTab"), NSLOCTEXT("Customization", "HeadTab", "머리"));
    AddPalette(0, NSLOCTEXT("Customization", "SkinColor", "피부색"));
    DefaultFocus = TabFirstControls.Last();
    {
        auto* Header = Tree.ConstructWidget<UHorizontalBox>();
        Header->AddChildToHorizontalBox(MakeText(Tree, NAME_None, NSLOCTEXT("Customization", "HeadShape", "머리 모양 · 납작 ↔ 둥글게"), 16))
            ->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        HeadShapeText = MakeText(Tree, TEXT("HeadShapeValue"), FText::GetEmpty(), 16, FLinearColor(0.95f, 0.75f, 0.45f));
        Header->AddChildToHorizontalBox(HeadShapeText);
        Page->AddChildToVerticalBox(Header)->SetPadding(FMargin(0.f, 14.f, 0.f, 2.f));
        HeadShapeSlider = Tree.ConstructWidget<USlider>(USlider::StaticClass(), TEXT("HeadShapeSlider"));
        HeadShapeSlider->SetMinValue(0.f);
        HeadShapeSlider->SetMaxValue(1.f);
        HeadShapeSlider->OnValueChanged.AddUniqueDynamic(this, &ThisClass::ChangeHeadShape);
        Page->AddChildToVerticalBox(HeadShapeSlider)->SetPadding(FMargin(0.f, 4.f));
    }
    for (const ECustomizationSlot SlotKind : GetCustomizationSlots())
    {
        AddTab(FName(*(SlotName(SlotKind) + TEXT("Tab"))), GetCustomizationSlotText(SlotKind));
        AddPartList(SlotKind);
        if (IsCustomizationSlotColorable(SlotKind))
        {
            AddPalette(static_cast<int32>(SlotKind) + 1,
                FText::Format(NSLOCTEXT("Customization", "PartColor", "{0} 색상"), GetCustomizationSlotText(SlotKind)));
        }
    }

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
    StateHandle = ViewModel->AddFieldValueChangedDelegate(UCustomizationViewModel::FFieldNotificationClassDescriptor::GetPartIds,
        INotifyFieldValueChanged::FFieldValueChangedDelegate::CreateUObject(this, &ThisClass::OnStateChanged));
    FailureHandle = ViewModel->AddFieldValueChangedDelegate(UCustomizationViewModel::FFieldNotificationClassDescriptor::GetFailureText,
        INotifyFieldValueChanged::FFieldValueChangedDelegate::CreateUObject(this, &ThisClass::OnFailureChanged));
    ViewModel->Open();
    Refresh();
    if (GetOwningPlayer() && DefaultFocus) { DefaultFocus->SetUserFocus(GetOwningPlayer()); }
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
    // 탭을 바꾸거나 화면을 다시 열면(첫 탭 선택) 펼쳐 둔 팔레트를 접는다.
    for (UWidget* Grid : PaletteGrids)
    {
        if (Grid) { Grid->SetVisibility(ESlateVisibility::Collapsed); }
    }
}
// 감춘 탭의 입력으로는 갈 수 없으므로 현재 탭 기준으로 대상을 고른다.
UWidget* UCustomizationScreenWidget::NavigateToTab(EUINavigation) { return TabButtons[TabSwitcher->GetActiveWidgetIndex()]; }
UWidget* UCustomizationScreenWidget::NavigateToControls(EUINavigation) { return TabFirstControls[TabSwitcher->GetActiveWidgetIndex()]; }

void UCustomizationScreenWidget::Refresh()
{
    if (!ViewModel) { return; }
    TGuardValue<bool> Guard(bSynchronizing, true);
    const bool bEditing = Model && Model->IsEditing();
    for (UCustomizationPartButton* Button : PartButtons)
    {
        Button->SetIsEnabled(bEditing);
        Button->SetBackgroundColor(ViewModel->GetSlotPartId(Button->GetPartSlot()) == Button->GetPartId() ? SelectedPartColor : TabColor);
    }
    // 색상 칸은 현재 색으로 칠하고, 팔레트에서는 현재 색 swatch에만 표시를 붙인다.
    const TArray<int32> Colors = ViewModel->GetColorIndices();
    for (UCustomizationColorButton* Button : ColorButtons)
    {
        Button->SetIsEnabled(bEditing);
        const int32 Current = Colors.IsValidIndex(Button->GetTarget()) ? Colors[Button->GetTarget()] : INDEX_NONE;
        if (Button->GetPaletteIndex() == INDEX_NONE)
        {
            Button->SetBackgroundColor(GetCustomizationColor(Current));
            continue;
        }
        if (UTextBlock* Mark = Cast<UTextBlock>(Button->GetChildAt(0)))
        {
            Mark->SetText(Button->GetPaletteIndex() == Current ? FText::FromString(TEXT("●")) : FText::GetEmpty());
            Mark->SetColorAndOpacity(FSlateColor(MarkColor(GetCustomizationColor(Button->GetPaletteIndex()))));
        }
    }
    HeadShapeSlider->SetIsEnabled(bEditing);
    HeadShapeSlider->SetValue(ViewModel->GetHeadShape());
    HeadShapeText->SetText(FText::FromString(FString::Printf(TEXT("%.2f"), ViewModel->GetHeadShape())));
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
void UCustomizationScreenWidget::HandlePartPicked(UCustomizationPartButton& Button)
{
    if (ViewModel) { ViewModel->SelectSlotPart(Button.GetPartSlot(), Button.GetPartId()); }
}
// 색상 칸은 팔레트를 펼치고 접는다. swatch는 색을 고르고, 성공하면 팔레트를 접는다.
void UCustomizationScreenWidget::HandleColorPicked(UCustomizationColorButton& Button)
{
    UWidget* Grid = PaletteGrids.IsValidIndex(Button.GetTarget()) ? PaletteGrids[Button.GetTarget()].Get() : nullptr;
    if (!ViewModel || !Grid) { return; }
    if (Button.GetPaletteIndex() == INDEX_NONE)
    {
        Grid->SetVisibility(Grid->IsVisible() ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
        return;
    }
    const ECustomizationFailure Result = Button.GetTarget() == 0 ? ViewModel->SetSkinColor(Button.GetPaletteIndex())
        : ViewModel->SetSlotColor(static_cast<ECustomizationSlot>(Button.GetTarget() - 1), Button.GetPaletteIndex());
    if (Result == ECustomizationFailure::None) { Grid->SetVisibility(ESlateVisibility::Collapsed); }
}
// Slider는 delegate 전에 자체 값을 바꾼다. 같은 실패가 반복돼도 Model의 정상 값으로 되돌린다.
void UCustomizationScreenWidget::ChangeHeadShape(float Value)
{
    if (!bSynchronizing && ViewModel && ViewModel->SetHeadShape(Value) != ECustomizationFailure::None) { Refresh(); }
}
void UCustomizationScreenWidget::Reset() { if (ViewModel) { ViewModel->Reset(); } }
void UCustomizationScreenWidget::TestFailure() { if (ViewModel) { ViewModel->SelectSlotPart(ECustomizationSlot::Hair, TEXT("Unregistered")); } }
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
UWidget* UCustomizationScreenWidget::NativeGetDesiredFocusTarget() const { return DefaultFocus; }
TOptional<FUIInputConfig> UCustomizationScreenWidget::GetDesiredInputConfig() const
{
    return FUIInputConfig(ECommonInputMode::Menu, EMouseCaptureMode::NoCapture);
}
void UCustomizationScreenWidget::ReleasePreview()
{
    if (ViewModel)
    {
        ViewModel->RemoveFieldValueChangedDelegate(UCustomizationViewModel::FFieldNotificationClassDescriptor::GetPartIds, StateHandle);
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
