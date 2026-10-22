#pragma once

#include "CoreMinimal.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Widgets/SLeafWidget.h"

// 맵 이동 때 화면 가운데 원 바깥을 검게 덮는 원형 전환(circle wipe) 마스크다(원작의 출발·도착 연출, 설계결정기록 037).
// 반지름은 화면 대각선 절반에 대한 비율이다. 0이면 화면 전체를 덮고, 1 이상이면 아무것도 그리지 않는다.
class SCircleWipeMask : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SCircleWipeMask) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs) {}
    void SetRadius(float InRadius);
    float GetRadius() const { return Radius; }
    virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override { return FVector2D::ZeroVector; }
    virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
        FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
private:
    float Radius = 1.f;
    // 그리기 요소가 brush를 참조하므로 paint 동안 살아 있도록 멤버로 둔다. 크기는 paint마다 화면에 맞춘다.
    mutable FSlateRoundedBoxBrush RingBrush = FSlateRoundedBoxBrush(FLinearColor::Transparent, 0.f, FLinearColor::Black, 0.f);
};

// 원작 관찰: 중간 크기까지 빠르게 → 잠깐 멈춤 → 끝까지 빠르게. 닫힘과 열림은 같은 시간표를 반대로 쓴다.
namespace CircleWipe
{
constexpr float SnapTime = 0.07f;
constexpr float HoldEndTime = 0.32f;
constexpr float Duration = 0.44f;
constexpr float HoldRadius = 0.25f;
// 경과 시간의 반지름이다. 닫힘은 1 → 0, 열림은 0 → 1로 간다.
float Evaluate(float Elapsed, bool bOpening);
}
