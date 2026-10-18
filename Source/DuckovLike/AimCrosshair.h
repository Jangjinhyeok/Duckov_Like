#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

// 게임 입력 중 마우스 커서 자리에 그리는 4방향 눈금 십자선이다(원작과 같은 모양).
// viewport의 software cursor로 등록하면 Slate가 커서 위치 중앙에 그리므로 위치 갱신용 tick이 없다.
class SAimCrosshair : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SAimCrosshair) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs) {}
    virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;
    virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
        FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
};
