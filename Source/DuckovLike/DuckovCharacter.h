#pragma once

#include "CoreMinimal.h"
#include "Engine/TimerHandle.h"
#include "GameFramework/Character.h"
#include "DuckovCharacter.generated.h"

class UAnimationAsset;
class UCameraComponent;
class UCustomizationAppearanceComponent;
class UCustomizationModel;
class UInputAction;
class UInputMappingContext;
class USpringArmComponent;
class UStaticMeshComponent;
struct FInputActionValue;

// 지상 플레이 캐릭터다. 화면 기준 WASD 이동과 달리기, 고정 각도 탑다운 카메라를 갖는다.
// 원작처럼 이동 방향과 무관하게 조준 지점(마우스 커서)을 바라보고 카메라를 그쪽으로 앞당긴다.
// 조준 지점까지의 선은 레이저 사이트를 켰을 때만 보인다(원작은 총기에 레이저 사이트를 달아야 보인다).
// 맨손 근접 공격과 총 사격은 바라보는 방향으로 나간다. 피해는 대상의 ability system이 처리한다.
// 총을 장착하면 가슴 앞에 총 모형이 보이고 날개로 총을 쥔 Idle·Walk를 재생한다. 장착 여부는 controller가 알려 준다.
// 외형은 세션의 커스터마이징 Model에서 확정된 Profile을 따른다. 위치·이동·공격 간격 외의 게임 상태는 소유하지 않는다(탄창 잔탄도 세션이 갖는다).
UCLASS()
class DUCKOVLIKE_API ADuckovCharacter : public ACharacter
{
    GENERATED_BODY()
public:
    ADuckovCharacter();
    UCustomizationAppearanceComponent* GetAppearance() const { return Appearance; }
    USpringArmComponent* GetCameraBoom() const { return CameraBoom; }
    static constexpr float WalkSpeed = 350.f;
    static constexpr float SprintSpeed = 600.f;
    // 수평 속도(cm/s)를 입력으로 받는 Idle·Walk BlendSpace다(CharacterAnimationAssets.Create).
    static constexpr const TCHAR* LocomotionAnimationPath = TEXT("/Game/Character/BS_DuckLocomotion.BS_DuckLocomotion");
    // 같은 축의 총을 쥔 Idle·Walk BlendSpace다. 날개만 다르고 몸·발 동작은 같다.
    static constexpr const TCHAR* RifleLocomotionAnimationPath = TEXT("/Game/Character/BS_DuckLocomotionRifle.BS_DuckLocomotionRifle");
    // 총을 장착하면 총 모형을 보이고 총을 쥔 동작으로 바꾼다. 해제하면 되돌린다. 같은 값이면 아무것도 하지 않는다.
    void SetWeaponEquipped(bool bEquipped);
    bool IsWeaponEquipped() const { return bWeaponEquipped; }
    USceneComponent* GetWeaponModel() const { return WeaponModel; }
    virtual void Tick(float DeltaSeconds) override;
    // 바라보고 조준할 월드 지점이다. controller가 마우스 커서에서 정한다. 높이는 쓰지 않는다.
    void SetAimPoint(const FVector& WorldPoint);
    bool HasAimPoint() const { return bHasAimPoint; }
    const FVector& GetAimPoint() const { return AimPoint; }
    // 레이저 사이트 선을 켜고 끈다. 기본은 꺼져 있다. 아직 이를 켜는 장비가 없다.
    void SetLaserSightEnabled(bool bEnabled);
    bool IsLaserSightEnabled() const { return bLaserSightEnabled; }
    UStaticMeshComponent* GetAimLine() const { return AimLine; }
    UStaticMeshComponent* GetAimDot() const { return AimDot; }
    // 조준 지점 쪽으로 카메라를 앞당기는 비율과 최대 거리(cm)다. 카메라가 움직이면 커서의 월드 지점도 따라 밀려서
    // 실제 앞당김은 r/(1-r)배로 수렴한다. 0.15면 커서를 화면 끝에 둘 때 캐릭터가 창 너비의 약 7% 밀린다(원작 관찰).
    static constexpr float LookAheadRatio = 0.15f;
    static constexpr float MaxLookAhead = 300.f;
    // 맨손 근접 공격이다. 바라보는 방향 앞의 원 안에 있는 ability system 대상마다 피해를 한 번 준다.
    // 공격 간격이 지나지 않았으면 아무것도 하지 않고 false다. 벽 너머도 원 안이면 맞는다.
    bool TryMelee();
    UStaticMeshComponent* GetMeleeSwing() const { return MeleeSwing; }
    static constexpr float MeleeDamage = 15.f;
    static constexpr float MeleeInterval = 0.4f;
    // 판정 원의 중심은 캐릭터 중심에서 앞으로 MeleeReach, 반지름은 MeleeRadius(cm)다. 몸 가장자리 앞 약 90 cm까지 닿는다.
    static constexpr float MeleeReach = 70.f;
    static constexpr float MeleeRadius = 50.f;
    static constexpr float MeleeSwingDuration = 0.12f;
    // 장착한 총의 사격이다. 조준 지점 쪽으로 몸 가장자리에서 hitscan을 쏴 처음 막는 것에 피해를 준다. 벽에 막힌다.
    // 잔탄을 확인하고 소모하는 것은 호출자다. 사격 간격이 지나지 않았으면 아무것도 하지 않고 false다.
    bool TryFire();
    UStaticMeshComponent* GetTracer() const { return Tracer; }
    static constexpr float RifleDamage = 20.f;
    static constexpr float RifleInterval = 0.15f;
    static constexpr float RifleRange = 3000.f;
    static constexpr int32 RifleMagazineSize = 20;
    static constexpr float TracerDuration = 0.05f;
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void NotifyControllerChanged() override;
    virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
private:
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<USpringArmComponent> CameraBoom;
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UCameraComponent> Camera;
    // 발밑 원점 외형 mesh를 capsule 바닥에 맞추는 기준점이다.
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<USceneComponent> VisualRoot;
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UCustomizationAppearanceComponent> Appearance;
    // 레이저 사이트의 선과 끝 점이다. 몸 가장자리에서 조준 지점까지 긋고, 막히면 막힌 지점에서 끝난다.
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UStaticMeshComponent> AimLine;
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UStaticMeshComponent> AimDot;
    // 근접 공격 판정 원을 발밑에 잠깐 보여 준다. 공격 애니메이션이 없어 휘두른 범위를 이것으로 알린다.
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UStaticMeshComponent> MeleeSwing;
    FTimerHandle MeleeSwingTimer;
    double NextMeleeTime = 0.0;
    // 총알이 날아간 선이다. 몸 가장자리에서 맞은 지점(없으면 사거리 끝)까지 잠깐 보인다.
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UStaticMeshComponent> Tracer;
    FTimerHandle TracerTimer;
    double NextFireTime = 0.0;
    // 장착한 총의 모형이다. 기본 도형 조각들을 자식으로 갖고 body의 body bone을 따라 움직인다.
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<USceneComponent> WeaponModel;
    bool bWeaponEquipped = false;
    UPROPERTY(Transient)
    TObjectPtr<UAnimationAsset> Locomotion;
    UPROPERTY(Transient)
    TObjectPtr<UAnimationAsset> RifleLocomotion;
    FVector AimPoint = FVector::ZeroVector;
    bool bHasAimPoint = false;
    bool bLaserSightEnabled = false;
    UPROPERTY(Transient)
    TObjectPtr<UInputMappingContext> InputMapping;
    UPROPERTY(Transient)
    TObjectPtr<UInputAction> MoveAction;
    UPROPERTY(Transient)
    TObjectPtr<UInputAction> SprintAction;
    UPROPERTY(Transient)
    TObjectPtr<UCustomizationModel> CustomizationModel;
    FDelegateHandle CustomizationHandle;
    void CreateInput();
    void ApplyConfirmedAppearance();
    void AttachWeaponModel();
    void Move(const FInputActionValue& Value);
    void StartSprint();
    void StopSprint();
    void UpdateAim();
    void HideMeleeSwing();
    void HideTracer();
};
