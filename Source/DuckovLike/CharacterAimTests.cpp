#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Components/StaticMeshComponent.h"
#include "DuckovCharacter.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/SpringArmComponent.h"

// 마우스 조준의 기하다. controller가 정한 조준 지점으로 회전·카메라 앞당김·레이저 사이트 선이 결정되는지 본다.
// 실제 마우스·화면 좌표 변환은 Duckov.Basement.CharacterMovement가 PIE에서 확인한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCharacterMouseAim, "Duckov.Character.MouseAim",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCharacterMouseAim::RunTest(const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    ON_SCOPE_EXIT
    {
        World->DestroyWorld(false);
        if (World->IsRooted()) { World->RemoveFromRoot(); }
    };
    const FVector Start(0.f, 0.f, 46.f);
    ADuckovCharacter* Character = World->SpawnActor<ADuckovCharacter>(Start, FRotator::ZeroRotator);
    if (!TestNotNull(TEXT("캐릭터"), Character)) { return false; }
    UStaticMeshComponent* Line = Character->GetAimLine();
    UStaticMeshComponent* Dot = Character->GetAimDot();
    TestTrue(TEXT("조준 전에는 조준선 없음"), !Character->HasAimPoint() && !Line->IsVisible() && !Dot->IsVisible());

    // 오른쪽(+Y) 500 cm. 기본 상태는 레이저 사이트가 없어 회전과 카메라만 바뀌고 선은 없다(원작과 같다).
    Character->SetAimPoint(FVector(0.f, 500.f, 0.f));
    Character->Tick(1.f / 60.f);
    TestTrue(TEXT("조준 지점을 바라봄"), Character->GetActorForwardVector().Equals(FVector(0.f, 1.f, 0.f), 1e-3f));
    TestTrue(TEXT("기본은 레이저 사이트 선 없음"), !Character->IsLaserSightEnabled() && !Line->IsVisible() && !Dot->IsVisible());
    // 레이저 사이트를 켜면 조준 지점의 높이는 쓰지 않고 캐릭터 높이에서 선을 긋는다.
    Character->SetLaserSightEnabled(true);
    Character->Tick(1.f / 60.f);
    TestTrue(TEXT("막히지 않으면 조준 지점까지"), Line->IsVisible() && Dot->IsVisible()
        && Dot->GetComponentLocation().Equals(FVector(0.f, 500.f, 46.f), 0.1f));
    TestTrue(TEXT("선은 몸 가장자리에서 시작"), FMath::IsNearlyEqual(Line->GetComponentScale().Z * 100.f, 500.f - 32.f, 0.1f));
    TestTrue(TEXT("카메라가 조준 쪽으로 앞당겨짐"),
        Character->GetCameraBoom()->TargetOffset.Equals(FVector(0.f, 500.f * ADuckovCharacter::LookAheadRatio, 0.f), 0.1f));

    // 사이에 벽이 있으면 벽 앞면에서 끊긴다. Cube는 100 cm라 y 250 중심, 두께 20 cm면 앞면이 y 240이다.
    AStaticMeshActor* Wall = World->SpawnActor<AStaticMeshActor>(FVector(0.f, 250.f, 46.f), FRotator::ZeroRotator);
    Wall->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
    Wall->SetActorScale3D(FVector(2.f, 0.2f, 2.f));
    Character->Tick(1.f / 60.f);
    TestTrue(TEXT("벽에서 조준선이 끊김"), FMath::IsNearlyEqual(Dot->GetComponentLocation().Y, 240.f, 0.5f)
        && FMath::IsNearlyEqual(Line->GetComponentScale().Z * 100.f, 240.f - 32.f, 0.5f));
    Wall->Destroy();

    // 멀리 있는 커서는 카메라를 최대 거리까지만 앞당긴다.
    Character->SetAimPoint(FVector(3000.f, 0.f, 0.f));
    Character->Tick(1.f / 60.f);
    TestTrue(TEXT("앞(+X)으로 회전"), Character->GetActorForwardVector().Equals(FVector(1.f, 0.f, 0.f), 1e-3f));
    TestTrue(TEXT("앞당김 최대 거리"), Character->GetCameraBoom()->TargetOffset.Equals(FVector(ADuckovCharacter::MaxLookAhead, 0.f, 0.f), 0.1f));

    // 커서가 몸 안쪽이면 방향이 흔들리므로 돌지 않고 선을 숨긴다.
    Character->SetAimPoint(FVector(-10.f, 5.f, 0.f));
    Character->Tick(1.f / 60.f);
    TestTrue(TEXT("몸 안쪽 커서는 회전 유지·조준선 숨김"), Character->GetActorForwardVector().Equals(FVector(1.f, 0.f, 0.f), 1e-3f)
        && !Line->IsVisible() && !Dot->IsVisible());

    // 레이저 사이트를 끄면 바로 선이 사라지고 회전은 계속 조준을 따른다.
    Character->SetAimPoint(FVector(0.f, -500.f, 0.f));
    Character->Tick(1.f / 60.f);
    Character->SetLaserSightEnabled(false);
    TestTrue(TEXT("레이저 사이트를 끄면 바로 숨김"), !Line->IsVisible() && !Dot->IsVisible());
    Character->SetAimPoint(FVector(500.f, 0.f, 0.f));
    Character->Tick(1.f / 60.f);
    TestTrue(TEXT("끈 뒤에도 회전은 조준을 따름·선 없음"), Character->GetActorForwardVector().Equals(FVector(1.f, 0.f, 0.f), 1e-3f)
        && !Line->IsVisible() && !Dot->IsVisible());
    return true;
}
#endif
