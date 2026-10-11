#include "LoadingScreenWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"

void ULoadingScreenWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    auto* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("LoadingRoot"));
    WidgetTree->RootWidget = Root;
    auto* Backdrop = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("LoadingBackdrop"));
    Backdrop->SetBrushColor(FLinearColor::Black);
    auto* BackdropSlot = Root->AddChildToCanvas(Backdrop);
    BackdropSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
    BackdropSlot->SetOffsets(FMargin(0.f));
    auto* Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("LoadingText"));
    FSlateFontInfo Font = Label->GetFont();
    Font.Size = 32;
    Label->SetFont(Font);
    Label->SetText(NSLOCTEXT("Loading", "Loading", "로딩 중..."));
    auto* LabelSlot = Root->AddChildToCanvas(Label);
    LabelSlot->SetAnchors(FAnchors(0.5f, 0.8f));
    LabelSlot->SetAlignment(FVector2D(0.5f));
    LabelSlot->SetAutoSize(true);
    SetVisibility(ESlateVisibility::HitTestInvisible);
}
