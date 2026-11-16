#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "CombatTestSupport.h"
#include "Components/StaticMeshComponent.h"
#include "DuckovCharacter.h"
#include "TrainingDummyActor.h"

namespace MeleeAttackTests
{
namespace
{
constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

template <typename T>
T* SpawnAt(UWorld* World, const FVector& Location)
{
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    return World->SpawnActor<T>(Location, FRotator::ZeroRotator, Params);
}
}

// 맨손 근접 공격의 판정 범위·간격·표시다. 입력 경로는 Duckov.Combat.MeleeInput이 PIE에서 확인한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMeleeAttack, "Duckov.Combat.Melee", Flags)
bool FMeleeAttack::RunTest(const FString& Parameters)
{
    CombatTestSupport::FPlayWorld Play;
    // 캐릭터는 +X를 바라본다. 과녁 기둥은 반지름 30 cm라 앞 과녁의 가까운 면은 x 60이다.
    ADuckovCharacter* Attacker = SpawnAt<ADuckovCharacter>(Play.World, FVector(0.f, 0.f, 46.f));
    ATrainingDummyActor* Front = SpawnAt<ATrainingDummyActor>(Play.World, FVector(90.f, 0.f, 0.f));
    ATrainingDummyActor* Behind = SpawnAt<ATrainingDummyActor>(Play.World, FVector(-90.f, 0.f, 0.f));
    ATrainingDummyActor* Far = SpawnAt<ATrainingDummyActor>(Play.World, FVector(300.f, 0.f, 0.f));
    if (!TestTrue(TEXT("캐릭터와 과녁"), Attacker && Front && Behind && Far)) { return false; }
    const float Full = Front->GetMaxHealth();
    TestFalse(TEXT("공격 전에는 판정 원 숨김"), Attacker->GetMeleeSwing()->IsVisible());

    TestTrue(TEXT("첫 공격"), Attacker->TryMelee());
    TestEqual(TEXT("앞 과녁만 피해"), Front->GetHealth(), Full - ADuckovCharacter::MeleeDamage);
    TestEqual(TEXT("뒤 과녁은 그대로"), Behind->GetHealth(), Full);
    TestEqual(TEXT("먼 과녁은 그대로"), Far->GetHealth(), Full);
    TestTrue(TEXT("판정 원 표시"), Attacker->GetMeleeSwing()->IsVisible());

    // 간격 안의 공격은 무시되고 아무 피해도 없다.
    TestFalse(TEXT("간격 안 공격 거부"), Attacker->TryMelee());
    TestEqual(TEXT("거부된 공격은 피해 없음"), Front->GetHealth(), Full - ADuckovCharacter::MeleeDamage);
    Play.Advance(ADuckovCharacter::MeleeSwingDuration + 0.01f);
    TestFalse(TEXT("판정 원은 잠깐만"), Attacker->GetMeleeSwing()->IsVisible());

    // 간격이 지나면 다시 공격하고, 바라보는 방향의 대상만 맞는다.
    Play.Advance(ADuckovCharacter::MeleeInterval);
    Attacker->SetActorRotation(FRotator(0.f, 180.f, 0.f));
    TestTrue(TEXT("간격 뒤 공격"), Attacker->TryMelee());
    TestEqual(TEXT("돌아서면 뒤 과녁이 맞음"), Behind->GetHealth(), Full - ADuckovCharacter::MeleeDamage);
    TestEqual(TEXT("등 뒤가 된 앞 과녁은 그대로"), Front->GetHealth(), Full - ADuckovCharacter::MeleeDamage);
    return true;
}
}
#endif
