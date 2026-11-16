#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemInterface.h"
#include "GameFramework/Actor.h"
#include "TrainingDummyActor.generated.h"

class UAbilitySystemComponent;
class UDuckovAttributeSet;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;
struct FOnAttributeChangeData;

// 공격을 시험하는 고정 과녁이다. 체력은 GAS attribute이고 피해는 피해 GE로만 받는다.
// 맞으면 잠깐 밝아지고, 체력이 0이 되면 쓰러졌다가 RecoverDelay 뒤 최대 체력으로 다시 선다.
// Tick 없이 attribute 변경 delegate와 timer로만 갱신한다.
UCLASS()
class DUCKOVLIKE_API ATrainingDummyActor : public AActor, public IAbilitySystemInterface
{
    GENERATED_BODY()
public:
    ATrainingDummyActor();
    virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override { return AbilitySystem; }

    float GetHealth() const;
    float GetMaxHealth() const;
    bool IsDown() const { return bDown; }
    bool IsFlashing() const;
    UStaticMeshComponent* GetBody() const { return Body; }
    UStaticMeshComponent* GetHealthBarBack() const { return HealthBarBack; }
    UStaticMeshComponent* GetHealthBarFill() const { return HealthBarFill; }

    UPROPERTY(EditAnywhere, Category="TrainingDummy", meta=(ClampMin="1"))
    float StartingMaxHealth = 100.f;
    // timer는 0 이하 간격을 등록하지 않으므로 0보다 커야 다시 선다.
    UPROPERTY(EditAnywhere, Category="TrainingDummy", meta=(ClampMin="0.1"))
    float RecoverDelay = 3.f;
    static constexpr float FlashDuration = 0.1f;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    void HandleHealthChanged(const FOnAttributeChangeData& Change);
    void UpdateHealthBar(float CurrentHealth);
    void SetBodyColor(const FLinearColor& Color);
    void EndFlash();
    void FallDown();
    void Recover();

    UPROPERTY(VisibleAnywhere, Category="TrainingDummy")
    TObjectPtr<UStaticMeshComponent> Body;
    UPROPERTY(VisibleAnywhere, Category="TrainingDummy")
    TObjectPtr<USceneComponent> HealthBar;
    UPROPERTY(VisibleAnywhere, Category="TrainingDummy")
    TObjectPtr<UStaticMeshComponent> HealthBarBack;
    UPROPERTY(VisibleAnywhere, Category="TrainingDummy")
    TObjectPtr<UStaticMeshComponent> HealthBarFill;
    UPROPERTY(VisibleAnywhere, Category="TrainingDummy")
    TObjectPtr<UAbilitySystemComponent> AbilitySystem;
    UPROPERTY()
    TObjectPtr<UDuckovAttributeSet> Attributes;
    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> BodyMaterial;

    FTimerHandle FlashTimer;
    FTimerHandle RecoverTimer;
    TEnumAsByte<ECollisionEnabled::Type> StandingCollision = ECollisionEnabled::QueryAndPhysics;
    bool bDown = false;
};
