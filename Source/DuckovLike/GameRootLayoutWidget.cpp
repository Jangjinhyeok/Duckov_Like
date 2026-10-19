#include "GameRootLayoutWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Widgets/CommonActivatableWidgetContainer.h"

namespace
{
void FillOverlaySlot(UOverlaySlot* OverlaySlot)
{
    OverlaySlot->SetHorizontalAlignment(HAlign_Fill);
    OverlaySlot->SetVerticalAlignment(VAlign_Fill);
}
}

void UGameRootLayoutWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("RootLayout"));
    WidgetTree->RootWidget = Root;
    // HUD는 열린 메뉴 아래에 깔린다. Notice layer처럼 activatable root로 두지 않는다.
    HudLayer = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("HudLayer"));
    HudLayer->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    FillOverlaySlot(Root->AddChildToOverlay(HudLayer));
    MenuStack = WidgetTree->ConstructWidget<UCommonActivatableWidgetStack>(UCommonActivatableWidgetStack::StaticClass(), TEXT("MenuStack"));
    // CommonUI 기본 전환(FadeOnly·Linear)을 쓴다. 이전 화면이 절반 시간 동안 사라진 뒤 새 화면이 절반 시간 동안 나타난다.
    // 원작 인게임 메뉴의 닫기 fade(약 0.15초)에 한 단계를 맞춘다. 전환 중에는 container가 마우스 입력을 막고,
    // 메뉴를 닫은 뒤의 Game 입력 복구도 사라지는 fade가 끝난 뒤에 일어난다(설계결정기록 036).
    MenuStack->SetTransitionDuration(0.3f);
    FillOverlaySlot(Root->AddChildToOverlay(MenuStack));
    // 안내는 열린 메뉴 위에도 보여야 하므로 Menu stack 위에 둔다. activatable root로 두지 않는다.
    // 항상 활성인 root가 있으면 막 열린 메뉴가 첫 paint 전에 Router의 paint layer 비교에서 밀린다(설계결정기록 021).
    NoticeLayer = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("NoticeLayer"));
    NoticeLayer->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    FillOverlaySlot(Root->AddChildToOverlay(NoticeLayer));
}

void UGameRootLayoutWidget::AddHudElement(UWidget* Element)
{
    if (!HudLayer || !Element) { return; }
    FillOverlaySlot(HudLayer->AddChildToOverlay(Element));
}

void UGameRootLayoutWidget::AddNoticeElement(UWidget* Element)
{
    if (!NoticeLayer || !Element) { return; }
    FillOverlaySlot(NoticeLayer->AddChildToOverlay(Element));
}
