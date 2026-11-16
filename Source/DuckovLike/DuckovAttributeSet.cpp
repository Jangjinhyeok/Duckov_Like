#include "DuckovAttributeSet.h"

#include "GameplayEffectExtension.h"

void UDuckovAttributeSet::ClampHealth(const FGameplayAttribute& Attribute, float& NewValue) const
{
    if (Attribute == GetHealthAttribute()) { NewValue = FMath::Clamp(NewValue, 0.f, GetMaxHealth()); }
}

void UDuckovAttributeSet::PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const
{
    ClampHealth(Attribute, NewValue);
}

void UDuckovAttributeSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
    ClampHealth(Attribute, NewValue);
}

void UDuckovAttributeSet::PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data)
{
    if (Data.EvaluatedData.Attribute != GetDamageAttribute()) { return; }
    const float Amount = GetDamage();
    SetDamage(0.f);
    // 음수 피해로 회복하지 않는다. 회복은 별도 효과의 일이다.
    if (Amount > 0.f) { SetHealth(GetHealth() - Amount); }
}
