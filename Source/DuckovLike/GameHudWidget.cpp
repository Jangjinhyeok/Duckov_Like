#include "GameHudWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "HudStyle.h"

namespace
{
UHorizontalBox* MakeKeyRow(UWidgetTree& Tree, const FText& Label, const FText& Key, int32 Size)
{
    UHorizontalBox* Row = Tree.ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
    Row->AddChildToHorizontalBox(DuckovHudStyle::MakeShadowText(Tree, NAME_None, Label, Size))->SetVerticalAlignment(VAlign_Center);
    UHorizontalBoxSlot* KeySlot = Row->AddChildToHorizontalBox(DuckovHudStyle::MakeKeyBadge(Tree, Key));
    KeySlot->SetVerticalAlignment(VAlign_Center);
    KeySlot->SetPadding(FMargin(8.f, 0.f, 0.f, 0.f));
    return Row;
}

UCanvasPanelSlot* Place(UCanvasPanel* Root, UWidget* Widget, FAnchors Anchors, FVector2D Alignment, FVector2D Position)
{
    UCanvasPanelSlot* CanvasSlot = Root->AddChildToCanvas(Widget);
    CanvasSlot->SetAnchors(Anchors);
    CanvasSlot->SetAlignment(Alignment);
    CanvasSlot->SetPosition(Position);
    CanvasSlot->SetAutoSize(true);
    return CanvasSlot;
}
}

void UGameHudWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("HudRoot"));
    WidgetTree->RootWidget = Root;

    // 오른쪽 위: 원작처럼 "조작 설명 [O]"만 보이고 O를 누르면 아래로 키 목록이 펼쳐진다.
    UVerticalBox* Controls = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ControlsColumn"));
    Controls->AddChildToVerticalBox(MakeKeyRow(*WidgetTree, NSLOCTEXT("Hud", "Controls", "조작 설명"), FText::FromString(TEXT("O")), 16))
        ->SetHorizontalAlignment(HAlign_Right);
    ControlsList = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ControlsList"));
    const TPair<FText, FText> Keys[] = {
        {NSLOCTEXT("Hud", "Move", "이동"), FText::FromString(TEXT("W A S D"))},
        {NSLOCTEXT("Hud", "Sprint", "달리기"), FText::FromString(TEXT("Shift"))},
        {NSLOCTEXT("Hud", "Interact", "상호작용"), FText::FromString(TEXT("F"))},
        {NSLOCTEXT("Hud", "Inventory", "인벤토리"), FText::FromString(TEXT("I"))},
        {NSLOCTEXT("Hud", "LaserSight", "레이저 사이트(테스트)"), FText::FromString(TEXT("L"))},
        {NSLOCTEXT("Hud", "CloseMenu", "메뉴 닫기"), FText::FromString(TEXT("Esc"))},
    };
    for (const TPair<FText, FText>& Key : Keys)
    {
        UVerticalBoxSlot* RowSlot = ControlsList->AddChildToVerticalBox(MakeKeyRow(*WidgetTree, Key.Key, Key.Value, 13));
        RowSlot->SetHorizontalAlignment(HAlign_Right);
        RowSlot->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f));
    }
    ControlsList->SetVisibility(ESlateVisibility::Collapsed);
    Controls->AddChildToVerticalBox(ControlsList)->SetHorizontalAlignment(HAlign_Right);
    Place(Root, Controls, FAnchors(1.f, 0.f), FVector2D(1.f, 0.f), FVector2D(-28.f, 28.f));

    // 왼쪽 아래: 어두운 작은 pill에 현재 장소.
    UBorder* LocationPill = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("LocationPill"));
    LocationPill->SetBrush(FSlateRoundedBoxBrush(FLinearColor(0.f, 0.f, 0.f, 0.55f), 12.f));
    LocationPill->SetPadding(FMargin(14.f, 4.f));
    LocationText = DuckovHudStyle::MakeShadowText(*WidgetTree, TEXT("LocationText"), FText::GetEmpty(), 14);
    LocationPill->SetContent(LocationText);
    Place(Root, LocationPill, FAnchors(0.f, 1.f), FVector2D(0.f, 1.f), FVector2D(28.f, -28.f));

    // 아래 중앙: 마지막 상호작용 결과. 비어 있으면 아무것도 보이지 않는다.
    MessageText = DuckovHudStyle::MakeShadowText(*WidgetTree, TEXT("MessageText"), FText::GetEmpty(), 15);
    MessageText->SetColorAndOpacity(FSlateColor(FLinearColor(1.f, 0.85f, 0.4f)));
    Place(Root, MessageText, FAnchors(0.5f, 0.8f), FVector2D(0.5f, 0.f), FVector2D::ZeroVector);

    SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UGameHudWidget::SetLocation(const FText& Text)
{
    if (LocationText) { LocationText->SetText(Text); }
}

void UGameHudWidget::SetMessage(const FText& Text)
{
    if (MessageText) { MessageText->SetText(Text); }
}

void UGameHudWidget::ToggleControls()
{
    if (!ControlsList) { return; }
    ControlsList->SetVisibility(AreControlsExpanded() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
}

bool UGameHudWidget::AreControlsExpanded() const
{
    return ControlsList && ControlsList->GetVisibility() != ESlateVisibility::Collapsed;
}
