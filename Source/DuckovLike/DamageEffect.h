#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "NativeGameplayTags.h"
#include "DamageEffect.generated.h"

// 피해량을 넘기는 SetByCaller 이름이다.
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_SetByCaller_Damage)

// 대상의 Damage meta attribute에 SetByCaller 피해량을 더하는 즉시 효과다.
// 공격 방식과 무관하게 모든 피해가 이 효과 하나를 쓴다.
UCLASS()
class DUCKOVLIKE_API UDamageGameplayEffect : public UGameplayEffect
{
    GENERATED_BODY()
public:
    UDamageGameplayEffect();
};

namespace DuckovDamage
{
// Target의 ability system에 Amount만큼 피해 효과를 적용한다. Instigator는 공격한 actor이며 없어도 된다.
// Target에 ability system이 없거나 Amount가 0 이하면 아무것도 하지 않고 false를 돌려준다.
DUCKOVLIKE_API bool Apply(AActor* Target, float Amount, AActor* Instigator);
}
