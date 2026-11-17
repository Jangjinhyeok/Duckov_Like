#include "AimCrosshair.h"

#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"

namespace AimCrosshair
{
constexpr float Size = 28.f;
// 가운데를 비우고 눈금 네 개만 그린다. 커서가 가리키는 지점이 가려지지 않는다.
constexpr float Gap = 5.f;
// 커서 위젯은 커서 위치에 가운데를 맞춰 그려진다. 아래 안내 문구가 들어가도록 십자선보다 넓게 잡는다.
const FVector2D Area(140.f, 100.f);
// 재장전 링은 십자선 끝 바로 바깥을 돈다.
constexpr float RingRadius = 20.f;
constexpr int32 RingSegments = 48;
constexpr float LabelOffset = 30.f;
const FLinearColor Shadow(0.f, 0.f, 0.f, 0.6f);
const FLinearColor Low(1.f, 0.7f, 0.15f);
const FLinearColor Empty(1.f, 0.25f, 0.2f);

FLinearColor TickColor(EAimAmmoState State)
{
    switch (State)
    {
    case EAimAmmoState::Low: return Low;
    case EAimAmmoState::Empty:
    case EAimAmmoState::NoAmmo: return Empty;
    default: return FLinearColor::White;
    }
}

// 위쪽에서 시계 방향으로 Fraction만큼의 원호 점들이다. 화면 좌표는 y가 아래로 커진다.
TArray<FVector2f> Arc(const FVector2D& Center, float Fraction)
{
    const int32 Count = FMath::Max(1, FMath::CeilToInt(RingSegments * Fraction));
    TArray<FVector2f> Points;
    Points.Reserve(Count + 1);
    for (int32 Index = 0; Index <= Count; ++Index)
    {
        const float Angle = 2.f * PI * Fraction * Index / Count;
        Points.Add(FVector2f(Center + FVector2D(FMath::Sin(Angle), -FMath::Cos(Angle)) * RingRadius));
    }
    return Points;
}
}

EAimAmmoState ClassifyAmmo(int32 Rounds, int32 Carried, int32 MagazineSize, bool bReloading)
{
    if (bReloading) { return EAimAmmoState::Reloading; }
    if (Rounds <= 0) { return Carried > 0 ? EAimAmmoState::Empty : EAimAmmoState::NoAmmo; }
    return Rounds * 4 <= MagazineSize ? EAimAmmoState::Low : EAimAmmoState::Ready;
}

void SAimCrosshair::SetAmmoState(EAimAmmoState InState, float ReloadDuration)
{
    if (InState == EAimAmmoState::Reloading && State != EAimAmmoState::Reloading)
    {
        ReloadStartTime = FSlateApplication::IsInitialized() ? FSlateApplication::Get().GetCurrentTime() : 0.0;
        ReloadSeconds = ReloadDuration;
    }
    State = InState;
}

float SAimCrosshair::GetReloadProgress(double CurrentTime) const
{
    if (State != EAimAmmoState::Reloading || ReloadSeconds <= 0.f) { return 0.f; }
    return FMath::Clamp(static_cast<float>((CurrentTime - ReloadStartTime) / ReloadSeconds), 0.f, 1.f);
}

FVector2D SAimCrosshair::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
    return AimCrosshair::Area;
}

int32 SAimCrosshair::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
    FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
    const FVector2D Center = AllottedGeometry.GetLocalSize() * 0.5f;
    const FPaintGeometry Geometry = AllottedGeometry.ToPaintGeometry();
    const float Reach = AimCrosshair::Size * 0.5f - 1.f;
    const FVector2D Directions[] = {FVector2D(1.f, 0.f), FVector2D(-1.f, 0.f), FVector2D(0.f, 1.f), FVector2D(0.f, -1.f)};
    // 밝은 바닥에서도 보이도록 어두운 테두리를 먼저 그린다.
    const TPair<FLinearColor, float> Passes[] = {{AimCrosshair::Shadow, 4.f}, {AimCrosshair::TickColor(State), 2.f}};
    for (int32 Pass = 0; Pass < UE_ARRAY_COUNT(Passes); ++Pass)
    {
        for (const FVector2D& Direction : Directions)
        {
            // 그리기 요소가 점 배열을 보관하므로 요소마다 넘긴다. FVector2f로 바로 넘겨 변환 복사를 피한다.
            FSlateDrawElement::MakeLines(OutDrawElements, LayerId + Pass, Geometry,
                TArray<FVector2f>{FVector2f(Center + Direction * AimCrosshair::Gap), FVector2f(Center + Direction * Reach)},
                ESlateDrawEffect::None, Passes[Pass].Key, true, Passes[Pass].Value);
        }
    }
    int32 TopLayer = LayerId + UE_ARRAY_COUNT(Passes);
    if (State == EAimAmmoState::Reloading)
    {
        // 어두운 전체 원 위에 지금까지의 진행을 흰 원호로 채운다. 끝나면 controller가 상태를 바꿔 링이 사라진다.
        FSlateDrawElement::MakeLines(OutDrawElements, TopLayer, Geometry, AimCrosshair::Arc(Center, 1.f),
            ESlateDrawEffect::None, AimCrosshair::Shadow, true, 5.f);
        const float Progress = GetReloadProgress(Args.GetCurrentTime());
        if (Progress > 0.f)
        {
            FSlateDrawElement::MakeLines(OutDrawElements, TopLayer + 1, Geometry, AimCrosshair::Arc(Center, Progress),
                ESlateDrawEffect::None, FLinearColor::White, true, 3.f);
        }
        TopLayer += 2;
    }
    if (State == EAimAmmoState::Empty || State == EAimAmmoState::NoAmmo)
    {
        const FText Label = State == EAimAmmoState::Empty
            ? NSLOCTEXT("Hud", "AimReloadHint", "R 재장전") : NSLOCTEXT("Hud", "AimNoAmmo", "탄약 없음");
        const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Bold", 11);
        const FVector2D TextSize = FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Label, Font);
        const FVector2D TextOrigin = Center + FVector2D(-TextSize.X * 0.5f, AimCrosshair::LabelOffset - TextSize.Y * 0.5f);
        FSlateDrawElement::MakeText(OutDrawElements, TopLayer, AllottedGeometry.ToPaintGeometry(TextSize, FSlateLayoutTransform(TextOrigin + FVector2D(1.f, 1.f))),
            Label, Font, ESlateDrawEffect::None, AimCrosshair::Shadow);
        FSlateDrawElement::MakeText(OutDrawElements, TopLayer + 1, AllottedGeometry.ToPaintGeometry(TextSize, FSlateLayoutTransform(TextOrigin)),
            Label, Font, ESlateDrawEffect::None, AimCrosshair::Empty);
        TopLayer += 2;
    }
    return TopLayer;
}
