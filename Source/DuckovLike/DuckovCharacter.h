#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "DuckovCharacter.generated.h"

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
// 외형은 세션의 커스터마이징 Model에서 확정된 Profile을 따른다. 위치·이동 외의 게임 상태는 소유하지 않는다.
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
    void Move(const FInputActionValue& Value);
    void StartSprint();
    void StopSprint();
    void UpdateAim();
};
