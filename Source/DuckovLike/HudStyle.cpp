#include "HudStyle.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"

namespace DuckovHudStyle
{
UBorder* MakeKeyBadge(UWidgetTree& Tree, const FText& Key)
{
    UBorder* Badge = Tree.ConstructWidget<UBorder>(UBorder::StaticClass());
    Badge->SetBrush(FSlateRoundedBoxBrush(FLinearColor(0.92f, 0.92f, 0.92f), 4.f));
    Badge->SetPadding(FMargin(6.f, 1.f));
    UTextBlock* Text = Tree.ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
    Text->SetText(Key);
    Text->SetColorAndOpacity(FSlateColor(FLinearColor(0.08f, 0.08f, 0.1f)));
    FSlateFontInfo Font = Text->GetFont();
    Font.Size = 12;
    Text->SetFont(Font);
    Badge->SetContent(Text);
    return Badge;
}

UTextBlock* MakeShadowText(UWidgetTree& Tree, FName Name, const FText& Text, int32 Size)
{
    UTextBlock* Block = Tree.ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
    Block->SetText(Text);
    Block->SetColorAndOpacity(FSlateColor(FLinearColor::White));
    Block->SetShadowOffset(FVector2D(1.f, 1.f));
    Block->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.85f));
    FSlateFontInfo Font = Block->GetFont();
    Font.Size = Size;
    Block->SetFont(Font);
    return Block;
}
}
