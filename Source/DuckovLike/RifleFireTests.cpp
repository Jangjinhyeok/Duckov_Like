#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "AimCrosshair.h"
#include "CombatTestSupport.h"
#include "Tests/AutomationCommon.h"
#include "Components/StaticMeshComponent.h"
#include "DuckovCharacter.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/App.h"
#include "Slate/WidgetRenderer.h"
#include "TextureResource.h"
#include "TrainingDummyActor.h"
#include "UObject/StrongObjectPtr.h"

namespace RifleFireTests
{
namespace
{
constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
using CombatTestSupport::SpawnAt;

// tracer는 높이 100 cm Cylinder를 길이만큼 늘린 것이다.
float TracerLength(const ADuckovCharacter* Shooter)
{
    return Shooter->GetTracer()->GetComponentScale().Z * 100.f;
}

// 십자선을 바닥과 비슷한 불투명 색 위에 그려 pixel을 읽는다. 커서 위젯과 같은 크기이고 가운데가 커서 위치다.
struct FCrosshairImage
{
    FIntPoint Size;
    TArray<FColor> Pixels;
    FColor At(int32 DX, int32 DY) const
    {
        const int32 Index = (Size.Y / 2 + DY) * Size.X + Size.X / 2 + DX;
        return Pixels.IsValidIndex(Index) ? Pixels[Index] : FColor::Transparent;
    }
    // 가운데 아래 문구 자리(커서 아래 22~38 px)에 빨간 pixel이 있는지다.
    bool HasRedLabel() const
    {
        for (int32 DY = 22; DY <= 38; ++DY)
        {
            for (int32 DX = -30; DX <= 30; ++DX)
            {
                const FColor C = At(DX, DY);
                if (C.R > 200 && C.G < 120 && C.B < 120) { return true; }
            }
        }
        return false;
    }
};

FCrosshairImage RenderCrosshair(const TSharedRef<SAimCrosshair>& Crosshair)
{
    FCrosshairImage Image;
    const FVector2D Size = Crosshair->ComputeDesiredSize(1.f);
    Image.Size = FIntPoint(FMath::RoundToInt(Size.X), FMath::RoundToInt(Size.Y));
    TStrongObjectPtr<UTextureRenderTarget2D> Target(FWidgetRenderer::CreateTargetFor(Size, TF_Bilinear, false));
    Target->ClearColor = FLinearColor(0.3f, 0.33f, 0.3f, 1.f);
    FWidgetRenderer Renderer(false, true);
    Renderer.DrawWidget(Target.Get(), Crosshair, Size, 0.f);
    Target->GameThread_GetRenderTargetResource()->ReadPixels(Image.Pixels);
    return Image;
}

bool IsWhite(const FColor& C) { return C.R > 200 && C.G > 200 && C.B > 200; }
bool IsAmber(const FColor& C) { return C.R > 200 && C.G > 120 && C.G < 220 && C.B < 100; }
bool IsRed(const FColor& C) { return C.R > 200 && C.G < 120 && C.B < 120; }
}

// 총 사격의 판정·간격·조준 방향·벽 막힘이다. 탄창과 장착 분기는 Duckov.Combat.RifleInput이 PIE에서 확인한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRifleFire, "Duckov.Combat.Rifle", Flags)
bool FRifleFire::RunTest(const FString& Parameters)
{
    CombatTestSupport::FPlayWorld Play;
    // 캐릭터는 +X를 바라본다. 몸 반지름 32 cm라 총알은 x 32에서 나간다. 과녁 기둥은 반지름 30 cm다.
    ADuckovCharacter* Shooter = SpawnAt<ADuckovCharacter>(Play.World, FVector(0.f, 0.f, 46.f));
    ATrainingDummyActor* Front = SpawnAt<ATrainingDummyActor>(Play.World, FVector(200.f, 0.f, 0.f));
    ATrainingDummyActor* Side = SpawnAt<ATrainingDummyActor>(Play.World, FVector(0.f, 300.f, 0.f));
    ATrainingDummyActor* Covered = SpawnAt<ATrainingDummyActor>(Play.World, FVector(0.f, -400.f, 0.f));
    // 캐릭터와 가려진 과녁 사이의 벽이다. y -210 ~ -190을 막는다.
    AStaticMeshActor* Wall = SpawnAt<AStaticMeshActor>(Play.World, FVector(0.f, -200.f, 50.f));
    if (!TestTrue(TEXT("캐릭터·과녁·벽"), Shooter && Front && Side && Covered && Wall)) { return false; }
    UStaticMeshComponent* WallMesh = Wall->GetStaticMeshComponent();
    WallMesh->SetMobility(EComponentMobility::Movable);
    WallMesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
    Wall->SetActorScale3D(FVector(2.f, 0.2f, 1.f));
    const float Full = Front->GetMaxHealth();
    TestFalse(TEXT("쏘기 전에는 tracer 숨김"), Shooter->GetTracer()->IsVisible());

    // 조준 지점이 없으면 바라보는 방향으로 쏘고, 처음 막은 과녁에 피해가 들어간다.
    TestTrue(TEXT("첫 사격"), Shooter->TryFire());
    TestEqual(TEXT("앞 과녁 피해"), Front->GetHealth(), Full - ADuckovCharacter::RifleDamage);
    TestEqual(TEXT("옆 과녁은 그대로"), Side->GetHealth(), Full);
    TestTrue(TEXT("tracer 표시"), Shooter->GetTracer()->IsVisible());
    TestTrue(TEXT("tracer는 과녁 표면(x 170)에서 끝남"), FMath::IsNearlyEqual(TracerLength(Shooter), 138.f, 1.f));

    // 간격 안의 사격은 무시되고 피해도 없다.
    TestFalse(TEXT("간격 안 사격 거부"), Shooter->TryFire());
    TestEqual(TEXT("거부된 사격은 피해 없음"), Front->GetHealth(), Full - ADuckovCharacter::RifleDamage);
    Play.Advance(ADuckovCharacter::TracerDuration + 0.01f);
    TestFalse(TEXT("tracer는 잠깐만"), Shooter->GetTracer()->IsVisible());

    // 쏘는 순간 조준 지점 쪽으로 돌아서 쏜다.
    Play.Advance(ADuckovCharacter::RifleInterval);
    Shooter->SetAimPoint(FVector(0.f, 500.f, 46.f));
    TestTrue(TEXT("옆으로 조준해 사격"), Shooter->TryFire());
    TestTrue(TEXT("조준 지점 쪽으로 돎"), FMath::IsNearlyEqual(Shooter->GetActorRotation().Yaw, 90.f, 0.1f));
    TestEqual(TEXT("옆 과녁 피해"), Side->GetHealth(), Full - ADuckovCharacter::RifleDamage);
    TestEqual(TEXT("앞 과녁은 더 맞지 않음"), Front->GetHealth(), Full - ADuckovCharacter::RifleDamage);

    // 벽 뒤 과녁은 맞지 않고 총알은 벽 면(y -190)에서 멈춘다.
    Play.Advance(ADuckovCharacter::RifleInterval);
    Shooter->SetAimPoint(FVector(0.f, -600.f, 46.f));
    TestTrue(TEXT("벽 쪽 사격"), Shooter->TryFire());
    TestEqual(TEXT("벽 뒤 과녁은 그대로"), Covered->GetHealth(), Full);
    TestTrue(TEXT("tracer는 벽 면에서 끝남"), FMath::IsNearlyEqual(TracerLength(Shooter), 158.f, 1.f));

    // 막는 것이 없으면 사거리 끝까지 날아간다.
    Play.Advance(ADuckovCharacter::RifleInterval);
    Shooter->SetAimPoint(FVector(-500.f, 0.f, 46.f));
    TestTrue(TEXT("빈 곳 사격"), Shooter->TryFire());
    TestTrue(TEXT("tracer는 사거리 끝까지"), FMath::IsNearlyEqual(TracerLength(Shooter), ADuckovCharacter::RifleRange, 1.f));
    return true;
}

// 십자선과 탄약 HUD가 함께 쓰는 탄약 상태 기준, 그리고 재장전 링이 시간에 따라 차는 비율이다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAmmoState, "Duckov.Combat.AmmoState", Flags)
bool FAmmoState::RunTest(const FString& Parameters)
{
    constexpr int32 Full = ADuckovCharacter::RifleMagazineSize;
    TestTrue(TEXT("가득 차면 Ready"), ClassifyAmmo(Full, 0, Full, false) == EAimAmmoState::Ready);
    TestTrue(TEXT("4분의 1 초과면 Ready"), ClassifyAmmo(Full / 4 + 1, 10, Full, false) == EAimAmmoState::Ready);
    TestTrue(TEXT("4분의 1 이하면 Low"), ClassifyAmmo(Full / 4, 10, Full, false) == EAimAmmoState::Low);
    TestTrue(TEXT("한 발이면 Low"), ClassifyAmmo(1, 0, Full, false) == EAimAmmoState::Low);
    TestTrue(TEXT("빈 탄창에 가방 탄약이 있으면 Empty"), ClassifyAmmo(0, 3, Full, false) == EAimAmmoState::Empty);
    TestTrue(TEXT("탄창·가방이 비면 NoAmmo"), ClassifyAmmo(0, 0, Full, false) == EAimAmmoState::NoAmmo);
    TestTrue(TEXT("재장전 중이 가장 우선"), ClassifyAmmo(0, 0, Full, true) == EAimAmmoState::Reloading);

    TSharedRef<SAimCrosshair> Crosshair = SNew(SAimCrosshair);
    const double Now = FSlateApplication::Get().GetCurrentTime();
    TestEqual(TEXT("재장전 전에는 링 없음"), Crosshair->GetReloadProgress(Now + 1.0), 0.f);
    Crosshair->SetAmmoState(EAimAmmoState::Reloading, 2.f);
    TestTrue(TEXT("시작 시점은 0"), FMath::IsNearlyEqual(Crosshair->GetReloadProgress(Now), 0.f, 1e-3f));
    TestTrue(TEXT("절반 시간이면 절반"), FMath::IsNearlyEqual(Crosshair->GetReloadProgress(Now + 1.0), 0.5f, 1e-3f));
    TestEqual(TEXT("시간이 지나도 1을 넘지 않음"), Crosshair->GetReloadProgress(Now + 5.0), 1.f);
    // 재장전 중 다른 갱신(가방 변경 등)이 다시 Reloading을 알려도 시작 시각은 그대로다.
    Crosshair->SetAmmoState(EAimAmmoState::Reloading, 2.f);
    TestTrue(TEXT("다시 알려도 진행 유지"), FMath::IsNearlyEqual(Crosshair->GetReloadProgress(Now + 1.0), 0.5f, 1e-3f));
    Crosshair->SetAmmoState(EAimAmmoState::Ready);
    TestEqual(TEXT("끝나면 링 없음"), Crosshair->GetReloadProgress(Now + 1.0), 0.f);
    return true;
}

// 실제로 그린 십자선의 색·문구·재장전 링이다. 그린 그림은 사람이 볼 수 있게 PNG로도 남긴다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAimCrosshairPaint, "Duckov.Combat.AimCrosshairPaint", Flags)
bool FAimCrosshairPaint::RunTest(const FString& Parameters)
{
    // 눈금은 가운데에서 5~13 px에 있다. 오른쪽 눈금 한가운데 pixel로 색을 본다.
    constexpr int32 Tick = 9;
    const TPair<EAimAmmoState, const TCHAR*> States[] = {
        {EAimAmmoState::None, TEXT("Crosshair_None")}, {EAimAmmoState::Low, TEXT("Crosshair_Low")},
        {EAimAmmoState::Empty, TEXT("Crosshair_Empty")}, {EAimAmmoState::NoAmmo, TEXT("Crosshair_NoAmmo")},
    };
    for (const TPair<EAimAmmoState, const TCHAR*>& State : States)
    {
        TSharedRef<SAimCrosshair> Crosshair = SNew(SAimCrosshair);
        Crosshair->SetAmmoState(State.Key);
        const FCrosshairImage Image = RenderCrosshair(Crosshair);
        CombatTestSupport::SaveEvidencePng(*this, Image.Pixels, Image.Size, State.Value);
        const FColor TickColor = Image.At(Tick, 0);
        switch (State.Key)
        {
        case EAimAmmoState::None:
            TestTrue(TEXT("기본 눈금은 흰색"), IsWhite(TickColor));
            TestFalse(TEXT("기본은 문구 없음"), Image.HasRedLabel());
            break;
        case EAimAmmoState::Low:
            TestTrue(TEXT("잔탄이 적으면 눈금 주황"), IsAmber(TickColor));
            TestFalse(TEXT("잔탄이 적으면 문구 없음"), Image.HasRedLabel());
            break;
        default:
            TestTrue(*FString::Printf(TEXT("%s: 비면 눈금 빨강"), State.Value), IsRed(TickColor));
            TestTrue(*FString::Printf(TEXT("%s: 비면 아래에 빨간 문구"), State.Value), Image.HasRedLabel());
            break;
        }
        TestFalse(*FString::Printf(TEXT("%s: 재장전 링 없음"), State.Value), IsWhite(Image.At(0, -20)));
    }
    // 재장전 링은 그릴 때의 시각으로 위쪽에서 시계 방향으로 찬다. 약 절반 지난 뒤 그린다.
    // 위젯 renderer는 커서와 다른 시계(FApp)로 그리므로 기대값도 그 시각의 진행률로 정한다.
    TSharedRef<SAimCrosshair> Reloading = SNew(SAimCrosshair);
    Reloading->SetAmmoState(EAimAmmoState::Reloading, 2.f);
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Reloading]
    {
        const float Progress = Reloading->GetReloadProgress(FApp::GetCurrentTime());
        const FCrosshairImage Image = RenderCrosshair(Reloading);
        CombatTestSupport::SaveEvidencePng(*this, Image.Pixels, Image.Size, TEXT("Crosshair_ReloadingHalf"));
        AddInfo(FString::Printf(TEXT("그린 시점의 진행률 %.2f"), Progress));
        TestTrue(TEXT("재장전 중 눈금은 흰색"), IsWhite(Image.At(Tick, 0)));
        // 원호 끝의 anti-aliasing을 피해 경계에서 5% 넘게 떨어진 지점만 판정한다.
        const TPair<FIntPoint, float> Points[] = {{FIntPoint(0, -20), 0.f}, {FIntPoint(20, 0), 0.25f}, {FIntPoint(0, 20), 0.5f}, {FIntPoint(-20, 0), 0.75f}};
        for (const TPair<FIntPoint, float>& Point : Points)
        {
            if (FMath::Abs(Progress - Point.Value) < 0.05f) { continue; }
            TestEqual(*FString::Printf(TEXT("링 %.2f 지점이 진행률대로 찼는지"), Point.Value), IsWhite(Image.At(Point.Key.X, Point.Key.Y)), Progress > Point.Value);
        }
    }, 1.f));
    return true;
}
}
#endif
