#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "CombatTestSupport.h"
#include "Components/StaticMeshComponent.h"
#include "DuckovCharacter.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "TrainingDummyActor.h"

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
}
#endif
