#include "InteractionPromptWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

void UInteractionPromptWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("PromptRoot"));
    WidgetTree->RootWidget = Root;
    UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("PromptColumn"));
    UCanvasPanelSlot* ColumnSlot = Root->AddChildToCanvas(Column);
    // 화면 아래 중앙, 캐릭터와 겹치지 않는 높이다.
    ColumnSlot->SetAnchors(FAnchors(0.5f, 0.72f));
    ColumnSlot->SetAlignment(FVector2D(0.5f, 0.f));
    ColumnSlot->SetAutoSize(true);
    // 원작의 상호작용 프롬프트처럼 둥근 pill에 동작 이름과 키를 함께 쓴다.
    PromptPill = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("PromptPill"));
    PromptPill->SetBrush(FSlateRoundedBoxBrush(FLinearColor(0.f, 0.f, 0.f, 0.75f), 16.f));
    PromptPill->SetPadding(FMargin(18.f, 6.f));
    PromptPill->SetVisibility(ESlateVisibility::Collapsed);
    Column->AddChildToVerticalBox(PromptPill)->SetHorizontalAlignment(HAlign_Center);
    PromptText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("PromptText"));
    PromptText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
    PromptPill->SetContent(PromptText);
    MessageText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("MessageText"));
    MessageText->SetColorAndOpacity(FSlateColor(FLinearColor(1.f, 0.85f, 0.4f)));
    MessageText->SetShadowOffset(FVector2D(1.f, 1.f));
    MessageText->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.8f));
    UVerticalBoxSlot* MessageSlot = Column->AddChildToVerticalBox(MessageText);
    MessageSlot->SetHorizontalAlignment(HAlign_Center);
    MessageSlot->SetPadding(FMargin(0.f, 8.f, 0.f, 0.f));
    SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UInteractionPromptWidget::SetPrompt(const FText& Text)
{
    if (!PromptPill) { return; }
    PromptText->SetText(Text);
    PromptPill->SetVisibility(Text.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
}

void UInteractionPromptWidget::SetMessage(const FText& Text)
{
    if (MessageText) { MessageText->SetText(Text); }
}
