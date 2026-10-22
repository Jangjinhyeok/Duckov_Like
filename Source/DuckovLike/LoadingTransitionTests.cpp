#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "CircleWipeMask.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Slate/WidgetRenderer.h"
#include "TextureResource.h"
#include "UObject/StrongObjectPtr.h"

namespace LoadingTransitionTests
{
namespace
{
constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
const FIntPoint MaskSize(320, 180);

// 마스크를 투명 render target에 그려 pixel을 읽는다. 덮인 곳은 alpha가 차고 비운 곳은 0이다.
TArray<FColor> RenderMask(float Radius)
{
    TSharedRef<SCircleWipeMask> Mask = SNew(SCircleWipeMask);
    Mask->SetRadius(Radius);
    TStrongObjectPtr<UTextureRenderTarget2D> Target(FWidgetRenderer::CreateTargetFor(FVector2D(MaskSize), TF_Bilinear, false));
    Target->ClearColor = FLinearColor::Transparent;
    FWidgetRenderer Renderer(false, true);
    Renderer.DrawWidget(Target.Get(), Mask, FVector2D(MaskSize), 0.f);
    // ReadPixels가 render thread 명령을 기다린 뒤 읽는다.
    TArray<FColor> Pixels;
    Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels);
    return Pixels;
}

uint8 AlphaAt(const TArray<FColor>& Pixels, int32 X, int32 Y)
{
    return Pixels.IsValidIndex(Y * MaskSize.X + X) ? Pixels[Y * MaskSize.X + X].A : 0;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCircleWipeCurve, "Duckov.UI.LoadingCircleWipe.Curve", Flags)
bool FCircleWipeCurve::RunTest(const FString& Parameters)
{
    using namespace CircleWipe;
    TestEqual(TEXT("닫힘 시작은 열린 화면"), Evaluate(0.f, false), 1.f);
    TestEqual(TEXT("닫힘은 빠르게 중간 크기"), Evaluate(SnapTime, false), HoldRadius);
    TestEqual(TEXT("닫힘 중간에 멈춤"), Evaluate((SnapTime + HoldEndTime) * 0.5f, false), HoldRadius);
    TestEqual(TEXT("닫힘 끝은 다 덮음"), Evaluate(Duration, false), 0.f);
    TestEqual(TEXT("열림 시작은 다 덮음"), Evaluate(0.f, true), 0.f);
    TestEqual(TEXT("열림은 빠르게 중간 크기"), Evaluate(SnapTime, true), HoldRadius);
    TestEqual(TEXT("열림 끝은 다 드러냄"), Evaluate(Duration, true), 1.f);
    bool bClosingMonotonic = true;
    bool bOpeningMonotonic = true;
    for (float Time = 0.01f; Time <= Duration + 0.05f; Time += 0.01f)
    {
        bClosingMonotonic &= Evaluate(Time, false) <= Evaluate(Time - 0.01f, false);
        bOpeningMonotonic &= Evaluate(Time, true) >= Evaluate(Time - 0.01f, true);
    }
    TestTrue(TEXT("닫힘은 다시 커지지 않음"), bClosingMonotonic);
    TestTrue(TEXT("열림은 다시 작아지지 않음"), bOpeningMonotonic);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCircleWipeMaskPixels, "Duckov.UI.LoadingCircleWipe.Mask", Flags)
bool FCircleWipeMaskPixels::RunTest(const FString& Parameters)
{
    const FIntPoint Center = MaskSize / 2;
    // 320×180에서 대각선 절반은 약 183.6 px이고, 반지름 0.25는 약 45.9 px이다. 가장자리 anti-aliasing은 피해서 본다.
    const TArray<FColor> Hold = RenderMask(CircleWipe::HoldRadius);
    if (!TestEqual(TEXT("render pixel 수"), Hold.Num(), MaskSize.X * MaskSize.Y)) { return false; }
    TestEqual(TEXT("원 가운데는 비움"), AlphaAt(Hold, Center.X, Center.Y), uint8(0));
    TestEqual(TEXT("원 안쪽 가장자리 근처도 비움"), AlphaAt(Hold, Center.X + 38, Center.Y), uint8(0));
    TestEqual(TEXT("원 위쪽 안쪽도 비움"), AlphaAt(Hold, Center.X, Center.Y - 38), uint8(0));
    TestEqual(TEXT("원 바깥은 덮음"), AlphaAt(Hold, Center.X + 55, Center.Y), uint8(255));
    TestEqual(TEXT("원 대각선 바깥도 덮음"), AlphaAt(Hold, Center.X + 40, Center.Y + 40), uint8(255));
    TestEqual(TEXT("화면 모서리까지 덮음"), AlphaAt(Hold, 1, 1), uint8(255));
    TestEqual(TEXT("반대쪽 모서리까지 덮음"), AlphaAt(Hold, MaskSize.X - 2, MaskSize.Y - 2), uint8(255));
    const FColor Outside = Hold[(Center.Y) * MaskSize.X + Center.X + 55];
    TestTrue(TEXT("덮는 색은 검정"), Outside.R < 5 && Outside.G < 5 && Outside.B < 5);

    const TArray<FColor> Closed = RenderMask(0.f);
    TestEqual(TEXT("반지름 0은 가운데까지 덮음"), AlphaAt(Closed, Center.X, Center.Y), uint8(255));
    const TArray<FColor> Open = RenderMask(1.f);
    TestEqual(TEXT("반지름 1은 모서리도 비움"), AlphaAt(Open, 1, 1), uint8(0));
    TestEqual(TEXT("반지름 1은 가운데도 비움"), AlphaAt(Open, Center.X, Center.Y), uint8(0));
    return true;
}
}
#endif
