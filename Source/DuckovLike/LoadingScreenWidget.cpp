#include "LoadingScreenWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/NativeWidgetHost.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Containers/Ticker.h"
#include "Framework/Application/SlateApplication.h"
#include "CircleWipeMask.h"

namespace LoadingScreen
{
constexpr float TextSize = 32.f;
constexpr float RingSize = 36.f;
// 원작의 클릭 대기 원은 켜졌다 꺼졌다 한다.
constexpr float BlinkInterval = 0.5f;

UTextBlock* MakeText(UWidgetTree& Tree, FName Name, const FText& Text)
{
    auto* Label = Tree.ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
    FSlateFontInfo Font = Label->GetFont();
    Font.Size = TextSize;
    Label->SetFont(Font);
    Label->SetText(Text);
    return Label;
}

// 원작처럼 문구는 화면 아래쪽 가운데에 둔다.
void PlaceBottomCenter(UCanvasPanel& Root, UWidget* Widget)
{
    auto* WidgetSlot = Root.AddChildToCanvas(Widget);
    WidgetSlot->SetAnchors(FAnchors(0.5f, 0.8f));
    WidgetSlot->SetAlignment(FVector2D(0.5f));
    WidgetSlot->SetAutoSize(true);
}
}

void ULoadingScreenWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    auto* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("LoadingRoot"));
    WidgetTree->RootWidget = Root;
    // 검은 화면도 원형 마스크가 그린다. 반지름 0이면 화면 전체를 덮는다.
    Wipe = SNew(SCircleWipeMask);
    auto* WipeHost = WidgetTree->ConstructWidget<UNativeWidgetHost>(UNativeWidgetHost::StaticClass(), TEXT("WipeHost"));
    WipeHost->SetContent(Wipe.ToSharedRef());
    WipeHost->SetVisibility(ESlateVisibility::HitTestInvisible);
    auto* WipeSlot = Root->AddChildToCanvas(WipeHost);
    WipeSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
    WipeSlot->SetOffsets(FMargin(0.f));

    LoadingText = LoadingScreen::MakeText(*WidgetTree, TEXT("LoadingText"), NSLOCTEXT("Loading", "Loading", "로딩 중..."));
    LoadingScreen::PlaceBottomCenter(*Root, LoadingText);

    auto* Prompt = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("ContinuePrompt"));
    auto* RingBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("ContinueRingBox"));
    RingBox->SetWidthOverride(LoadingScreen::RingSize);
    RingBox->SetHeightOverride(LoadingScreen::RingSize);
    auto* Ring = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ContinueRing"));
    Ring->SetBrush(FSlateRoundedBoxBrush(FLinearColor::Transparent, LoadingScreen::RingSize * 0.5f, FLinearColor::White, 2.f));
    RingBox->AddChild(Ring);
    auto* RingSlot = Prompt->AddChildToHorizontalBox(RingBox);
    RingSlot->SetVerticalAlignment(VAlign_Center);
    RingSlot->SetPadding(FMargin(0.f, 0.f, 10.f, 0.f));
    auto* PromptText = LoadingScreen::MakeText(*WidgetTree, TEXT("ContinueText"),
        NSLOCTEXT("Loading", "ClickToContinue", "화면을 클릭하여 계속하기"));
    Prompt->AddChildToHorizontalBox(PromptText)->SetVerticalAlignment(VAlign_Center);
    LoadingScreen::PlaceBottomCenter(*Root, Prompt);
    ContinuePrompt = Prompt;
    ContinueRing = Ring;

    // 게임 중 커서는 십자선이지만 로딩 화면 위에서는 기본 화살표를 보인다.
    SetCursor(EMouseCursor::Default);
    Wipe->SetRadius(0.f);
    SetPhase(ELoadingScreenPhase::Loading);
}

void ULoadingScreenWidget::BeginDeparture()
{
    Wipe->SetRadius(1.f);
    SetPhase(ELoadingScreenPhase::Departing);
    StartWipe();
}

void ULoadingScreenWidget::BeginArrival()
{
    Wipe->SetRadius(0.f);
    SetPhase(ELoadingScreenPhase::AwaitingClick);
    Wipe->RegisterActiveTimer(LoadingScreen::BlinkInterval, FWidgetActiveTimerDelegate::CreateUObject(this, &ThisClass::BlinkRing));
}

void ULoadingScreenWidget::Continue()
{
    if (Phase != ELoadingScreenPhase::AwaitingClick) { return; }
    SetPhase(ELoadingScreenPhase::Revealing);
    StartWipe();
    ContinuedEvent.Broadcast();
}

FReply ULoadingScreenWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
    Continue();
    // 덮고 있는 동안에는 아래의 메뉴·게임으로 클릭을 넘기지 않는다.
    return FReply::Handled();
}

void ULoadingScreenWidget::SetPhase(ELoadingScreenPhase NewPhase)
{
    Phase = NewPhase;
    LoadingText->SetVisibility(Phase == ELoadingScreenPhase::Loading ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
    ContinuePrompt->SetVisibility(Phase == ELoadingScreenPhase::AwaitingClick ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
    ContinueRing->SetVisibility(ESlateVisibility::HitTestInvisible);
    // 드러내는 동안에는 클릭이 게임으로 지나간다.
    SetVisibility(Phase == ELoadingScreenPhase::Revealing ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Visible);
}

void ULoadingScreenWidget::StartWipe()
{
    WipeStartTime = FSlateApplication::Get().GetCurrentTime();
    Wipe->RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateUObject(this, &ThisClass::UpdateWipe));
}

EActiveTimerReturnType ULoadingScreenWidget::UpdateWipe(double CurrentTime, float DeltaTime)
{
    const float Elapsed = float(CurrentTime - WipeStartTime);
    const bool bOpening = Phase == ELoadingScreenPhase::Revealing;
    Wipe->SetRadius(CircleWipe::Evaluate(Elapsed, bOpening));
    if (Elapsed < CircleWipe::Duration) { return EActiveTimerReturnType::Continue; }
    if (bOpening)
    {
        // 이 widget의 active timer가 실행되는 중에는 떼지 않는다. 다음 frame에 화면에서 뺀다.
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this](float)
        {
            RemoveFromParent();
            return false;
        }));
    }
    else
    {
        SetPhase(ELoadingScreenPhase::Loading);
        CoveredEvent.Broadcast();
    }
    return EActiveTimerReturnType::Stop;
}

EActiveTimerReturnType ULoadingScreenWidget::BlinkRing(double CurrentTime, float DeltaTime)
{
    if (Phase != ELoadingScreenPhase::AwaitingClick) { return EActiveTimerReturnType::Stop; }
    ContinueRing->SetVisibility(ContinueRing->IsVisible() ? ESlateVisibility::Hidden : ESlateVisibility::HitTestInvisible);
    return EActiveTimerReturnType::Continue;
}
