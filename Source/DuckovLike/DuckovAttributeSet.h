#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "AttributeSet.h"
#include "DuckovAttributeSet.generated.h"

#define DUCKOV_ATTRIBUTE_ACCESSORS(ClassName, PropertyName) \
    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(ClassName, PropertyName) \
    GAMEPLAYATTRIBUTE_VALUE_GETTER(PropertyName) \
    GAMEPLAYATTRIBUTE_VALUE_SETTER(PropertyName) \
    GAMEPLAYATTRIBUTE_VALUE_INITTER(PropertyName)

// 체력을 가진 대상이 함께 쓰는 attribute다. 싱글플레이라 replication 설정은 두지 않는다.
// Damage는 남지 않는 meta attribute다. 피해 GE가 여기에 더하면 PostGameplayEffectExecute가 Health에서 빼고 0으로 되돌린다.
// 공격 방식(사격·근접)과 이후의 방어력 감산이 모두 이 한 지점을 지나게 하려는 것이다.
UCLASS()
class DUCKOVLIKE_API UDuckovAttributeSet : public UAttributeSet
{
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadOnly, Category="Attributes")
    FGameplayAttributeData Health;
    DUCKOV_ATTRIBUTE_ACCESSORS(UDuckovAttributeSet, Health)

    UPROPERTY(BlueprintReadOnly, Category="Attributes")
    FGameplayAttributeData MaxHealth;
    DUCKOV_ATTRIBUTE_ACCESSORS(UDuckovAttributeSet, MaxHealth)

    UPROPERTY(BlueprintReadOnly, Category="Attributes")
    FGameplayAttributeData Damage;
    DUCKOV_ATTRIBUTE_ACCESSORS(UDuckovAttributeSet, Damage)

protected:
    virtual void PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const override;
    virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;
    virtual void PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data) override;

private:
    void ClampHealth(const FGameplayAttribute& Attribute, float& NewValue) const;
};
