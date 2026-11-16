#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "AbilitySystemComponent.h"
#include "CombatTestSupport.h"
#include "Components/StaticMeshComponent.h"
#include "DamageEffect.h"
#include "DuckovAttributeSet.h"
#include "TrainingDummyActor.h"

namespace TrainingDummyTests
{
namespace
{
constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
using CombatTestSupport::FPlayWorld;

float BarRatio(const ATrainingDummyActor* Dummy)
{
    return Dummy->GetHealthBarFill()->GetRelativeScale3D().X / Dummy->GetHealthBarBack()->GetRelativeScale3D().X;
}

float PendingDamage(const ATrainingDummyActor* Dummy)
{
    return Dummy->GetAbilitySystemComponent()->GetNumericAttribute(UDuckovAttributeSet::GetDamageAttribute());
}
}

// 피해 GE가 체력을 줄이고, 0에서 쓰러졌다가 시간이 지나면 다시 서는 과녁의 계약이다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrainingDummyDamage, "Duckov.Combat.TrainingDummy", Flags)
bool FTrainingDummyDamage::RunTest(const FString& Parameters)
{
    FPlayWorld Play;
    ATrainingDummyActor* Dummy = Play.World->SpawnActor<ATrainingDummyActor>(FVector::ZeroVector, FRotator::ZeroRotator);
    AActor* Attacker = Play.World->SpawnActor<AActor>();
    if (!TestNotNull(TEXT("과녁"), Dummy) || !TestNotNull(TEXT("공격자"), Attacker)) { return false; }
    TestTrue(TEXT("BeginPlay"), Dummy->HasActorBegunPlay());
    TestEqual(TEXT("시작 체력은 최대 체력"), Dummy->GetHealth(), Dummy->StartingMaxHealth);
    TestEqual(TEXT("최대 체력"), Dummy->GetMaxHealth(), Dummy->StartingMaxHealth);
    TestTrue(TEXT("체력 막대 가득"), FMath::IsNearlyEqual(BarRatio(Dummy), 1.f, 1e-4f) && Dummy->GetHealthBarFill()->IsVisible());
    TestTrue(TEXT("서 있고 충돌함"), !Dummy->IsDown() && Dummy->GetBody()->IsCollisionEnabled());

    // 피해량은 SetByCaller로 넘어가 Damage meta attribute를 거쳐 체력에서 빠진다.
    TestTrue(TEXT("피해 적용"), DuckovDamage::Apply(Dummy, 30.f, Attacker));
    TestEqual(TEXT("체력 70"), Dummy->GetHealth(), 70.f);
    TestEqual(TEXT("Damage는 적용 뒤 0"), PendingDamage(Dummy), 0.f);
    TestTrue(TEXT("체력 막대 70%"), FMath::IsNearlyEqual(BarRatio(Dummy), 0.7f, 1e-4f));
    TestTrue(TEXT("맞으면 깜빡임"), Dummy->IsFlashing());
    Play.Advance(ATrainingDummyActor::FlashDuration + 0.01f);
    TestFalse(TEXT("깜빡임은 잠깐"), Dummy->IsFlashing());

    // 0 이하 피해와 ability system이 없는 대상은 아무것도 바꾸지 않는다.
    TestFalse(TEXT("0 피해 거부"), DuckovDamage::Apply(Dummy, 0.f, Attacker));
    TestFalse(TEXT("음수 피해 거부"), DuckovDamage::Apply(Dummy, -10.f, Attacker));
    TestFalse(TEXT("ability system 없는 대상 거부"), DuckovDamage::Apply(Attacker, 10.f, Dummy));
    TestEqual(TEXT("거부된 피해는 체력 유지"), Dummy->GetHealth(), 70.f);
    TestFalse(TEXT("거부된 피해는 깜빡이지 않음"), Dummy->IsFlashing());

    // 남은 체력보다 큰 피해는 0에서 멈추고 과녁이 쓰러진다.
    TestTrue(TEXT("큰 피해 적용"), DuckovDamage::Apply(Dummy, 500.f, nullptr));
    TestEqual(TEXT("체력은 0 아래로 내려가지 않음"), Dummy->GetHealth(), 0.f);
    TestTrue(TEXT("쓰러짐"), Dummy->IsDown());
    TestFalse(TEXT("쓰러지면 공격이 지나감"), Dummy->GetBody()->IsCollisionEnabled());
    TestFalse(TEXT("쓰러지면 체력 막대 감춤"), Dummy->GetHealthBarFill()->IsVisible() || Dummy->GetHealthBarBack()->IsVisible());

    // 쓰러진 동안 받은 피해는 체력을 바꾸지 않고 다시 서는 시간도 늦추지 않는다.
    Play.Advance(Dummy->RecoverDelay * 0.5f);
    TestTrue(TEXT("쓰러진 뒤 피해"), DuckovDamage::Apply(Dummy, 10.f, Attacker));
    TestEqual(TEXT("쓰러진 동안 체력 0"), Dummy->GetHealth(), 0.f);
    Play.Advance(Dummy->RecoverDelay * 0.5f + 0.01f);
    TestFalse(TEXT("시간이 지나면 다시 섬"), Dummy->IsDown());
    TestEqual(TEXT("다시 서면 최대 체력"), Dummy->GetHealth(), Dummy->GetMaxHealth());
    TestTrue(TEXT("다시 서면 충돌·막대 복구"), Dummy->GetBody()->IsCollisionEnabled()
        && Dummy->GetHealthBarFill()->IsVisible() && FMath::IsNearlyEqual(BarRatio(Dummy), 1.f, 1e-4f));
    TestFalse(TEXT("회복은 깜빡이지 않음"), Dummy->IsFlashing());

    // 다시 선 과녁도 같은 경로로 피해를 받는다.
    TestTrue(TEXT("복구 뒤 피해"), DuckovDamage::Apply(Dummy, 25.f, Attacker));
    TestEqual(TEXT("복구 뒤 체력 75"), Dummy->GetHealth(), 75.f);
    return true;
}
}
#endif
