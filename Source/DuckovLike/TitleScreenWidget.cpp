#include "TitleScreenWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "GameFramework/PlayerController.h"

namespace TitleScreenStyle
{
// 레퍼런스 화면에서 고른 sRGB 값이다. Slate는 linear 색을 쓰므로 변환해 둔다.
const FLinearColor BackdropColor = FLinearColor::FromSRGBColor(FColor(14, 30, 52));
const FLinearColor TitleColor = FLinearColor::FromSRGBColor(FColor(240, 166, 64));
const FLinearColor TextColor = FLinearColor::FromSRGBColor(FColor(236, 230, 214));
const FLinearColor PrimaryColor = FLinearColor::FromSRGBColor(FColor(108, 194, 74));
const FLinearColor SecondaryColor = FLinearColor::FromSRGBColor(FColor(30, 156, 200));

UTextBlock* MakeText(UWidgetTree* Tree, const FText& Text, int32 Size, const FLinearColor& Color)
{
    auto* Label = Tree->ConstructWidget<UTextBlock>();
    FSlateFontInfo Font = Label->GetFont();
    Font.Size = Size;
    Font.TypefaceFontName = TEXT("Bold");
    Label->SetFont(Font);
    Label->SetText(Text);
    Label->SetColorAndOpacity(FSlateColor(Color));
    return Label;
}

// 원작 메뉴처럼 둥근 pill 버튼을 만든다. 주 동작은 크기와 색으로 구분한다.
UButton* MakePillButton(UWidgetTree* Tree, FName Name, const FText& Text, const FLinearColor& Color, int32 FontSize)
{
    auto* Button = Tree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
    const float Radius = FontSize * 1.2f;
    FButtonStyle Style = Button->GetStyle();
    Style.SetNormal(FSlateRoundedBoxBrush(Color, Radius));
    Style.SetHovered(FSlateRoundedBoxBrush(Color * 1.25f, Radius, FLinearColor::White, 2.f));
    Style.SetPressed(FSlateRoundedBoxBrush(Color * 0.8f, Radius));
    Style.SetNormalPadding(FMargin(28.f, 10.f));
    Style.SetPressedPadding(FMargin(28.f, 12.f, 28.f, 8.f));
    Button->SetStyle(Style);
    Button->AddChild(MakeText(Tree, Text, FontSize, FLinearColor::White));
    return Button;
}
}

UTitleScreenWidget::UTitleScreenWidget(const FObjectInitializer& Initializer)
    : Super(Initializer)
{
    bIsBackHandler = true;
    // 스플래시에서는 화면 자체가 입력을 받아야 하므로 focus를 받을 수 있게 둔다.
    SetIsFocusable(true);
}

void UTitleScreenWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    auto* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("TitleRoot"));
    WidgetTree->RootWidget = Root;
    auto* Backdrop = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Backdrop"));
    Backdrop->SetBrushColor(TitleScreenStyle::BackdropColor);
    auto* BackdropSlot = Root->AddChildToCanvas(Backdrop);
    BackdropSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
    BackdropSlot->SetOffsets(FMargin(0.f));

    auto* Splash = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("SplashPanel"));
    auto* SplashSlot = Root->AddChildToCanvas(Splash);
    SplashSlot->SetAnchors(FAnchors(0.5f, 0.38f));
    SplashSlot->SetAlignment(FVector2D(0.5f));
    SplashSlot->SetAutoSize(true);
    Splash->AddChildToVerticalBox(TitleScreenStyle::MakeText(WidgetTree, NSLOCTEXT("Title", "GameTitle", "DUCKOV LIKE"), 96, TitleScreenStyle::TitleColor))
        ->SetHorizontalAlignment(HAlign_Center);
    Splash->AddChildToVerticalBox(TitleScreenStyle::MakeText(WidgetTree, NSLOCTEXT("Title", "Subtitle", "탑다운 익스트랙션 · UI 포트폴리오"), 24, TitleScreenStyle::TextColor))
        ->SetHorizontalAlignment(HAlign_Center);
    auto* Prompt = TitleScreenStyle::MakeText(WidgetTree, NSLOCTEXT("Title", "PressToContinue", "화면을 클릭하여 계속하기"), 30, TitleScreenStyle::TextColor);
    UVerticalBoxSlot* PromptSlot = Splash->AddChildToVerticalBox(Prompt);
    PromptSlot->SetHorizontalAlignment(HAlign_Center);
    PromptSlot->SetPadding(FMargin(0.f, 220.f, 0.f, 0.f));
    SplashPanel = Splash;

    auto* Menu = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("MenuPanel"));
    auto* MenuSlot = Root->AddChildToCanvas(Menu);
    MenuSlot->SetAnchors(FAnchors(0.06f, 0.2f));
    MenuSlot->SetAutoSize(true);
    StartButton = TitleScreenStyle::MakePillButton(WidgetTree, TEXT("StartButton"), NSLOCTEXT("Title", "Start", "▶  게임 시작"), TitleScreenStyle::PrimaryColor, 34);
    QuitButton = TitleScreenStyle::MakePillButton(WidgetTree, TEXT("QuitButton"), NSLOCTEXT("Title", "Quit", "게임 종료"), TitleScreenStyle::SecondaryColor, 24);
    Menu->AddChildToVerticalBox(StartButton)->SetHorizontalAlignment(HAlign_Left);
    UVerticalBoxSlot* QuitSlot = Menu->AddChildToVerticalBox(QuitButton);
    QuitSlot->SetHorizontalAlignment(HAlign_Left);
    QuitSlot->SetPadding(FMargin(0.f, 14.f, 0.f, 0.f));
    StartButton->OnClicked.AddUniqueDynamic(this, &ThisClass::RequestStart);
    QuitButton->OnClicked.AddUniqueDynamic(this, &ThisClass::RequestQuit);
    MenuPanel = Menu;
    MenuPanel->SetVisibility(ESlateVisibility::Collapsed);
}

void UTitleScreenWidget::NativeOnActivated()
{
    Super::NativeOnActivated();
    // 게임 시작 직후에는 viewport가 아직 focus 경로에 없어 Router가 focus를 주지 않을 수 있다.
    // 다른 화면과 같이 활성화 시 직접 focus를 준다.
    FocusCurrentTarget();
}

void UTitleScreenWidget::ShowMenu()
{
    if (bMenuVisible) { return; }
    bMenuVisible = true;
    SplashPanel->SetVisibility(ESlateVisibility::Collapsed);
    MenuPanel->SetVisibility(ESlateVisibility::Visible);
    // focus 대상이 화면 자체에서 시작 버튼으로 바뀌었음을 Router에 알린다.
    RequestRefreshFocus();
    FocusCurrentTarget();
}

void UTitleScreenWidget::FocusCurrentTarget()
{
    if (APlayerController* Owner = GetOwningPlayer()) { NativeGetDesiredFocusTarget()->SetUserFocus(Owner); }
}

FReply UTitleScreenWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
    if (!bMenuVisible) { ShowMenu(); return FReply::Handled(); }
    return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

FReply UTitleScreenWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
    if (!bMenuVisible) { ShowMenu(); return FReply::Handled(); }
    return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

// 첫 화면은 닫히지 않는다. Back은 스플래시에서 메뉴로 넘기는 것 외에는 소비만 한다.
bool UTitleScreenWidget::NativeOnHandleBackAction()
{
    ShowMenu();
    return true;
}

UWidget* UTitleScreenWidget::NativeGetDesiredFocusTarget() const
{
    return bMenuVisible ? static_cast<UWidget*>(StartButton.Get()) : const_cast<UTitleScreenWidget*>(this);
}

TOptional<FUIInputConfig> UTitleScreenWidget::GetDesiredInputConfig() const
{
    return FUIInputConfig(ECommonInputMode::Menu, EMouseCaptureMode::NoCapture);
}

void UTitleScreenWidget::RequestStart() { StartRequested.Broadcast(); }
void UTitleScreenWidget::RequestQuit() { QuitRequested.Broadcast(); }
