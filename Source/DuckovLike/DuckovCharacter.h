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
struct FInputActionValue;

// 지상 플레이 캐릭터다. 화면 기준 WASD 이동과 달리기, 고정 각도 탑다운 카메라를 갖는다.
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
};
