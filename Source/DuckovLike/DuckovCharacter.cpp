#include "DuckovCharacter.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "CustomizationAppearanceComponent.h"
#include "CustomizationModel.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameSessionSubsystem.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"

namespace DuckovCharacterSetup
{
// 오리 외형은 키 91 cm, 폭 약 63 cm다(Art/CustomizationPrototype). 판정 크기는 외형 조절과 무관하게 고정한다.
constexpr float CapsuleRadius = 32.f;
constexpr float CapsuleHalfHeight = 46.f;
// 원작처럼 높은 각도에서 비스듬히 내려다보는 고정 시점이다. 캐릭터가 돌아도 카메라는 돌지 않는다.
const FRotator CameraRotation(-55.f, -40.f, 0.f);
constexpr float CameraDistance = 1500.f;
constexpr float CameraFieldOfView = 45.f;
}

ADuckovCharacter::ADuckovCharacter()
{
    PrimaryActorTick.bCanEverTick = false;
    GetCapsuleComponent()->InitCapsuleSize(DuckovCharacterSetup::CapsuleRadius, DuckovCharacterSetup::CapsuleHalfHeight);
    bUseControllerRotationYaw = false;
    UCharacterMovementComponent* Movement = GetCharacterMovement();
    Movement->bOrientRotationToMovement = true;
    Movement->RotationRate = FRotator(0.f, 720.f, 0.f);
    Movement->MaxWalkSpeed = WalkSpeed;

    CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
    CameraBoom->SetupAttachment(RootComponent);
    CameraBoom->SetUsingAbsoluteRotation(true);
    CameraBoom->SetWorldRotation(DuckovCharacterSetup::CameraRotation);
    CameraBoom->TargetArmLength = DuckovCharacterSetup::CameraDistance;
    CameraBoom->bDoCollisionTest = false;
    CameraBoom->bEnableCameraLag = true;
    CameraBoom->CameraLagSpeed = 8.f;
    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    Camera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
    Camera->SetFieldOfView(DuckovCharacterSetup::CameraFieldOfView);
    Camera->bUsePawnControlRotation = false;

    VisualRoot = CreateDefaultSubobject<USceneComponent>(TEXT("VisualRoot"));
    VisualRoot->SetupAttachment(RootComponent);
    // 원본(Blender)은 forward -Y지만 FBX import 뒤 UE에서는 +X가 정면이라 회전 보정 없이 actor forward와 맞는다.
    VisualRoot->SetRelativeLocation(FVector(0.f, 0.f, -DuckovCharacterSetup::CapsuleHalfHeight));
    GetMesh()->SetVisibility(false);
    Appearance = CreateDefaultSubobject<UCustomizationAppearanceComponent>(TEXT("Appearance"));
    CreateInput();
}

void ADuckovCharacter::CreateInput()
{
    MoveAction = CreateDefaultSubobject<UInputAction>(TEXT("MoveAction"));
    MoveAction->ValueType = EInputActionValueType::Axis2D;
    SprintAction = CreateDefaultSubobject<UInputAction>(TEXT("SprintAction"));
    SprintAction->ValueType = EInputActionValueType::Boolean;
    InputMapping = CreateDefaultSubobject<UInputMappingContext>(TEXT("InputMapping"));
    // Move의 X는 화면 위, Y는 화면 오른쪽이다. 키 하나는 X 축 값만 내므로 modifier로 축과 부호를 바꾼다.
    auto MapMoveKey = [this](const FKey& Key, bool bSwizzle, bool bNegate)
    {
        FEnhancedActionKeyMapping& Mapping = InputMapping->MapKey(MoveAction, Key);
        if (bSwizzle) { Mapping.Modifiers.Add(CreateDefaultSubobject<UInputModifierSwizzleAxis>(*FString::Printf(TEXT("Swizzle_%s"), *Key.ToString()))); }
        if (bNegate) { Mapping.Modifiers.Add(CreateDefaultSubobject<UInputModifierNegate>(*FString::Printf(TEXT("Negate_%s"), *Key.ToString()))); }
    };
    MapMoveKey(EKeys::W, false, false);
    MapMoveKey(EKeys::S, false, true);
    MapMoveKey(EKeys::D, true, false);
    MapMoveKey(EKeys::A, true, true);
    InputMapping->MapKey(SprintAction, EKeys::LeftShift);
}

void ADuckovCharacter::BeginPlay()
{
    Super::BeginPlay();
    Appearance->SetAttachParent(VisualRoot);
    UGameInstance* GameInstance = GetGameInstance();
    UGameSessionSubsystem* Session = GameInstance ? GameInstance->GetSubsystem<UGameSessionSubsystem>() : nullptr;
    if (!Session) { return; }
    CustomizationModel = Session->GetCustomizationModel();
    // 편집 중 Draft가 아니라 확정된 Profile만 반영한다. 변경 통지마다 같은 값이면 적용 비용만 든다.
    CustomizationHandle = CustomizationModel->OnChanged().AddUObject(this, &ThisClass::ApplyConfirmedAppearance);
    ApplyConfirmedAppearance();
}

void ADuckovCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (CustomizationModel) { CustomizationModel->OnChanged().Remove(CustomizationHandle); }
    CustomizationModel = nullptr;
    Super::EndPlay(EndPlayReason);
}

void ADuckovCharacter::ApplyConfirmedAppearance()
{
    if (!CustomizationModel) { return; }
    const ECustomizationFailure Result = Appearance->TryApply(CustomizationModel->GetProfile());
    if (Result != ECustomizationFailure::None)
    {
        UE_LOG(LogTemp, Warning, TEXT("캐릭터 외형 적용 실패: %s. 이전 외형을 유지합니다"), *UEnum::GetValueAsString(Result));
    }
}

namespace DuckovCharacterSetup
{
UEnhancedInputLocalPlayerSubsystem* GetInputSubsystem(const AController* Controller)
{
    const APlayerController* PlayerController = Cast<APlayerController>(Controller);
    ULocalPlayer* LocalPlayer = PlayerController ? PlayerController->GetLocalPlayer() : nullptr;
    return LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
}
}

void ADuckovCharacter::NotifyControllerChanged()
{
    // mapping context는 캐릭터마다 따로 있다. 빙의가 바뀌면 이전 controller에서 빼야 다른 pawn의 키를 가로채지 않는다.
    if (auto* Previous = DuckovCharacterSetup::GetInputSubsystem(PreviousController)) { Previous->RemoveMappingContext(InputMapping); }
    Super::NotifyControllerChanged();
    if (auto* Current = DuckovCharacterSetup::GetInputSubsystem(GetController())) { Current->AddMappingContext(InputMapping, 0); }
}

void ADuckovCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);
    if (auto* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent))
    {
        Input->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ThisClass::Move);
        Input->BindAction(SprintAction, ETriggerEvent::Started, this, &ThisClass::StartSprint);
        Input->BindAction(SprintAction, ETriggerEvent::Completed, this, &ThisClass::StopSprint);
    }
}

void ADuckovCharacter::Move(const FInputActionValue& Value)
{
    const FVector2D Axis = Value.Get<FVector2D>();
    // 카메라는 고정 yaw이므로 화면 위·오른쪽을 지면 방향으로 옮겨 움직인다.
    const FRotator ScreenYaw(0.f, CameraBoom->GetComponentRotation().Yaw, 0.f);
    AddMovementInput(ScreenYaw.RotateVector(FVector::ForwardVector), Axis.X);
    AddMovementInput(ScreenYaw.RotateVector(FVector::RightVector), Axis.Y);
}

void ADuckovCharacter::StartSprint() { GetCharacterMovement()->MaxWalkSpeed = SprintSpeed; }
void ADuckovCharacter::StopSprint() { GetCharacterMovement()->MaxWalkSpeed = WalkSpeed; }
