#include "InteractionPromptWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "HudStyle.h"

void UInteractionPromptWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("PromptRow"));
    WidgetTree->RootWidget = Row;
    // 물체의 기준점을 가리키는 흰 고리. widget component의 pivot이 이 고리 중심에 오도록 왼쪽 끝에 둔다.
    USizeBox* RingSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("PromptRingSize"));
    RingSize->SetWidthOverride(22.f);
    RingSize->SetHeightOverride(22.f);
    UBorder* Ring = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("PromptRing"));
    Ring->SetBrush(FSlateRoundedBoxBrush(FLinearColor(0.f, 0.f, 0.f, 0.35f), 11.f, FLinearColor::White, 3.f));
    RingSize->SetContent(Ring);
    Row->AddChildToHorizontalBox(RingSize)->SetVerticalAlignment(VAlign_Center);
    UBorder* Pill = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("PromptPill"));
    Pill->SetBrush(FSlateRoundedBoxBrush(FLinearColor(0.24f, 0.5f, 0.78f, 0.95f), 14.f));
    Pill->SetPadding(FMargin(14.f, 5.f, 7.f, 5.f));
    UHorizontalBoxSlot* PillSlot = Row->AddChildToHorizontalBox(Pill);
    PillSlot->SetVerticalAlignment(VAlign_Center);
    PillSlot->SetPadding(FMargin(4.f, 0.f, 0.f, 0.f));
    UHorizontalBox* PillRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("PromptPillRow"));
    Pill->SetContent(PillRow);
    PromptText = DuckovHudStyle::MakeShadowText(*WidgetTree, TEXT("PromptText"), FText::GetEmpty(), 17);
    PillRow->AddChildToHorizontalBox(PromptText)->SetVerticalAlignment(VAlign_Center);
    UHorizontalBoxSlot* KeySlot = PillRow->AddChildToHorizontalBox(DuckovHudStyle::MakeKeyBadge(*WidgetTree, FText::FromString(TEXT("F"))));
    KeySlot->SetVerticalAlignment(VAlign_Center);
    KeySlot->SetPadding(FMargin(10.f, 0.f, 0.f, 0.f));
    SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UInteractionPromptWidget::SetAction(const FText& ActionName)
{
    if (PromptText) { PromptText->SetText(ActionName); }
}
