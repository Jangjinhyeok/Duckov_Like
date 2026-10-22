#include "CircleWipeMask.h"

#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

void SCircleWipeMask::SetRadius(float InRadius)
{
    if (Radius == InRadius) { return; }
    Radius = InRadius;
    Invalidate(EInvalidateWidgetReason::Paint);
}

int32 SCircleWipeMask::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
    FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
    const FVector2f Size = FVector2f(AllottedGeometry.GetLocalSize());
    const float HalfDiagonal = Size.Size() * 0.5f;
    if (Radius >= 1.f || HalfDiagonal <= 0.f) { return LayerId; }
    const float Inner = Radius * HalfDiagonal;
    if (Inner < 1.f)
    {
        FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(),
            FCoreStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None, FLinearColor::Black);
        return LayerId + 1;
    }
    // 원 바깥만 검게 덮는다. 화면을 다 덮는 큰 원 모양 rounded box를 채우지 않고 outline만 두껍게 그리면
    // outline 안쪽 경계가 반지름 Inner의 원이 된다. 원 하나를 그리는 요소 하나로 끝나고 텍스처가 필요 없다.
    // MakeBox의 tint가 채움 색이므로 투명을 넘기고, outline 색은 brush의 검정을 쓴다.
    const float Band = HalfDiagonal + 2.f;
    const float Outer = Inner + Band;
    RingBrush.OutlineSettings.CornerRadii = FVector4(Outer, Outer, Outer, Outer);
    RingBrush.OutlineSettings.Width = Band;
    const FVector2f Center = Size * 0.5f;
    FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
        AllottedGeometry.ToPaintGeometry(FVector2f(Outer * 2.f), FSlateLayoutTransform(Center - FVector2f(Outer))),
        &RingBrush, ESlateDrawEffect::None, FLinearColor::Transparent);
    return LayerId + 1;
}

float CircleWipe::Evaluate(float Elapsed, bool bOpening)
{
    const float Start = bOpening ? 0.f : 1.f;
    const float End = bOpening ? 1.f : 0.f;
    if (Elapsed <= 0.f) { return Start; }
    if (Elapsed < SnapTime) { return FMath::InterpEaseOut(Start, HoldRadius, Elapsed / SnapTime, 2.f); }
    if (Elapsed < HoldEndTime) { return HoldRadius; }
    if (Elapsed < Duration) { return FMath::InterpEaseIn(HoldRadius, End, (Elapsed - HoldEndTime) / (Duration - HoldEndTime), 2.f); }
    return End;
}
