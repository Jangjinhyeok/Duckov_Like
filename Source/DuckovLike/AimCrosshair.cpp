#include "AimCrosshair.h"

#include "Rendering/DrawElements.h"

namespace AimCrosshair
{
constexpr float Size = 28.f;
// 가운데를 비우고 눈금 네 개만 그린다. 커서가 가리키는 지점이 가려지지 않는다.
constexpr float Gap = 5.f;
}

FVector2D SAimCrosshair::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
    return FVector2D(AimCrosshair::Size);
}

int32 SAimCrosshair::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
    FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
    const FVector2D Center = AllottedGeometry.GetLocalSize() * 0.5f;
    const float Reach = AimCrosshair::Size * 0.5f - 1.f;
    const FVector2D Directions[] = {FVector2D(1.f, 0.f), FVector2D(-1.f, 0.f), FVector2D(0.f, 1.f), FVector2D(0.f, -1.f)};
    // 밝은 바닥에서도 보이도록 어두운 테두리를 먼저 그린다.
    const TPair<FLinearColor, float> Passes[] = {{FLinearColor(0.f, 0.f, 0.f, 0.6f), 4.f}, {FLinearColor::White, 2.f}};
    for (int32 Pass = 0; Pass < UE_ARRAY_COUNT(Passes); ++Pass)
    {
        for (const FVector2D& Direction : Directions)
        {
            // 그리기 요소가 점 배열을 보관하므로 요소마다 넘긴다. FVector2f로 바로 넘겨 변환 복사를 피한다.
            FSlateDrawElement::MakeLines(OutDrawElements, LayerId + Pass, AllottedGeometry.ToPaintGeometry(),
                TArray<FVector2f>{FVector2f(Center + Direction * AimCrosshair::Gap), FVector2f(Center + Direction * Reach)},
                ESlateDrawEffect::None, Passes[Pass].Key, true, Passes[Pass].Value);
        }
    }
    return LayerId + UE_ARRAY_COUNT(Passes);
}
