#include "DamageEffect.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "DuckovAttributeSet.h"

UE_DEFINE_GAMEPLAY_TAG(TAG_SetByCaller_Damage, "SetByCaller.Damage")

UDamageGameplayEffect::UDamageGameplayEffect()
{
    DurationPolicy = EGameplayEffectDurationType::Instant;
    FSetByCallerFloat Amount;
    Amount.DataTag = TAG_SetByCaller_Damage;
    FGameplayModifierInfo Modifier;
    Modifier.Attribute = UDuckovAttributeSet::GetDamageAttribute();
    Modifier.ModifierOp = EGameplayModOp::Additive;
    Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(Amount);
    Modifiers.Add(Modifier);
}

bool DuckovDamage::Apply(AActor* Target, float Amount, AActor* Instigator)
{
    UAbilitySystemComponent* AbilitySystem = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target);
    if (!AbilitySystem || Amount <= 0.f) { return false; }
    FGameplayEffectContextHandle Context = AbilitySystem->MakeEffectContext();
    Context.AddInstigator(Instigator, Instigator);
    const FGameplayEffectSpecHandle Spec = AbilitySystem->MakeOutgoingSpec(UDamageGameplayEffect::StaticClass(), 1.f, Context);
    if (!Spec.IsValid()) { return false; }
    Spec.Data->SetSetByCallerMagnitude(TAG_SetByCaller_Damage, Amount);
    AbilitySystem->ApplyGameplayEffectSpecToSelf(*Spec.Data);
    return true;
}
