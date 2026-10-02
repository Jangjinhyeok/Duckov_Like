#include "RaidStatusWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"

void URaidStatusWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RaidStatusRoot"));
    WidgetTree->RootWidget = Root;
    UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("RaidStatusPanel"));
    Panel->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.8f));
    Panel->SetPadding(FMargin(12.f));
    UCanvasPanelSlot* PanelSlot = Root->AddChildToCanvas(Panel);
    PanelSlot->SetAnchors(FAnchors(0.5f, 0.f));
    PanelSlot->SetAlignment(FVector2D(0.5f, 0.f));
    PanelSlot->SetPosition(FVector2D(0.f, 12.f));
    PanelSlot->SetAutoSize(true);
    StatusText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("RaidStatusText"));
    StatusText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
    Panel->AddChild(StatusText);
    SetVisibility(ESlateVisibility::HitTestInvisible);
}

void URaidStatusWidget::SetStatus(const FText& Text)
{
    if (StatusText) { StatusText->SetText(Text); }
}
