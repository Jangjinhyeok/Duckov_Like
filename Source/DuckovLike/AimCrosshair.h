#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

// 총을 장착했을 때 십자선이 알리는 탄약 상태다. 총이 없으면 None이다.
enum class EAimAmmoState : uint8
{
    None,
    Ready,
    // 탄창이 얼마 남지 않았다.
    Low,
    // 탄창이 비었고 가방에 탄약이 있다(R로 재장전).
    Empty,
    // 탄창도 가방도 비었다.
    NoAmmo,
    Reloading,
};

// 장착한 총의 탄약 상태다. 탄창의 4분의 1 이하면 Low다. 십자선과 오른쪽 아래 탄약 HUD가 같은 기준을 쓴다.
EAimAmmoState ClassifyAmmo(int32 Rounds, int32 Carried, int32 MagazineSize, bool bReloading);

// 게임 입력 중 마우스 커서 자리에 그리는 4방향 눈금 십자선이다(원작과 같은 모양).
// viewport의 software cursor로 등록하면 Slate가 매 frame 커서 위치 중앙에 그리므로 위치 갱신용 tick이 없다.
// 시선이 머무는 곳이라 탄약 상태도 여기서 알린다: 적으면 주황, 비면 빨강과 안내 문구, 재장전 중이면 둘레에 진행 링.
// 재장전 진행은 그릴 때의 Slate 시각으로 계산하므로 상태가 바뀔 때만 controller가 값을 넣는다.
class SAimCrosshair : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SAimCrosshair) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs) {}
    // Reloading으로 바뀌는 순간부터 Duration초 동안 링을 채운다. 이미 Reloading이면 시작 시각을 유지한다.
    void SetAmmoState(EAimAmmoState InState, float ReloadDuration = 0.f);
    EAimAmmoState GetAmmoState() const { return State; }
    // 재장전 링이 찬 비율(0~1)이다. 재장전 중이 아니면 0이다.
    float GetReloadProgress(double CurrentTime) const;
    virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;
    virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
        FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
    EAimAmmoState State = EAimAmmoState::None;
    double ReloadStartTime = 0.0;
    float ReloadSeconds = 0.f;
};
