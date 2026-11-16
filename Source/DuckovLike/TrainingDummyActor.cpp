#include "TrainingDummyActor.h"

#include "AbilitySystemComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DuckovAttributeSet.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace TrainingDummySetup
{
// 지름 60 cm, 키 160 cm 기둥이다. actor 원점이 바닥이다.
const FVector BodyScale(0.6f, 0.6f, 1.6f);
const FVector BodyLocation(0.f, 0.f, 80.f);
// 쓰러지면 옆으로 눕는다. 누운 기둥의 중심은 반지름 높이다.
const FRotator DownRotation(0.f, 0.f, 90.f);
const FVector DownLocation(0.f, 0.f, 30.f);
// 체력 막대는 머리 위에 눕힌 얇은 판이다. 지상 카메라 yaw가 -40°라 화면 오른쪽은 yaw 50°다.
constexpr float BarYaw = 50.f;
constexpr float BarHeight = 185.f;
constexpr float BarWidth = 80.f;
constexpr float BarDepth = 10.f;
const FLinearColor BodyColor(0.75f, 0.55f, 0.3f);
const FLinearColor FlashColor(1.f, 1.f, 1.f);
const FLinearColor BarBackColor(0.05f, 0.05f, 0.05f);
const FLinearColor BarFillColor(0.85f, 0.1f, 0.1f);

UStaticMeshComponent* CreateBarMesh(AActor* Owner, const TCHAR* Name, USceneComponent* Parent, UStaticMesh* Mesh)
{
    UStaticMeshComponent* Result = Owner->CreateDefaultSubobject<UStaticMeshComponent>(Name);
    Result->SetupAttachment(Parent);
    Result->SetStaticMesh(Mesh);
    Result->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Result->SetGenerateOverlapEvents(false);
    Result->SetCastShadow(false);
    return Result;
}

void SetColor(UStaticMeshComponent* Mesh, UMaterialInterface* Material, const FLinearColor& Color)
{
    UMaterialInstanceDynamic* Instance = Mesh->CreateDynamicMaterialInstance(0, Material);
    if (Instance) { Instance->SetVectorParameterValue(TEXT("Color"), Color); }
}
}

ATrainingDummyActor::ATrainingDummyActor()
{
    PrimaryActorTick.bCanEverTick = false;
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
    Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
    Body->SetupAttachment(RootComponent);
    Body->SetStaticMesh(Cylinder.Object);
    Body->SetRelativeLocation(TrainingDummySetup::BodyLocation);
    Body->SetRelativeScale3D(TrainingDummySetup::BodyScale);

    HealthBar = CreateDefaultSubobject<USceneComponent>(TEXT("HealthBar"));
    HealthBar->SetupAttachment(RootComponent);
    HealthBar->SetRelativeLocation(FVector(0.f, 0.f, TrainingDummySetup::BarHeight));
    // 과녁이 어느 방향으로 놓여도 막대는 화면 가로로 보이게 한다.
    HealthBar->SetUsingAbsoluteRotation(true);
    HealthBar->SetWorldRotation(FRotator(0.f, TrainingDummySetup::BarYaw, 0.f));
    HealthBarBack = TrainingDummySetup::CreateBarMesh(this, TEXT("HealthBarBack"), HealthBar, Cube.Object);
    HealthBarBack->SetRelativeScale3D(FVector(TrainingDummySetup::BarWidth, TrainingDummySetup::BarDepth, 1.f) / 100.f);
    HealthBarFill = TrainingDummySetup::CreateBarMesh(this, TEXT("HealthBarFill"), HealthBar, Cube.Object);

    AbilitySystem = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystem"));
    // owner의 subobject인 attribute set은 ability system이 초기화할 때 찾아 등록한다.
    Attributes = CreateDefaultSubobject<UDuckovAttributeSet>(TEXT("Attributes"));
}

void ATrainingDummyActor::BeginPlay()
{
    Super::BeginPlay();
    UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    BodyMaterial = Body->CreateDynamicMaterialInstance(0, Material);
    SetBodyColor(TrainingDummySetup::BodyColor);
    TrainingDummySetup::SetColor(HealthBarBack, Material, TrainingDummySetup::BarBackColor);
    TrainingDummySetup::SetColor(HealthBarFill, Material, TrainingDummySetup::BarFillColor);
    // 최대 체력을 먼저 정해야 체력이 0으로 잘리지 않는다.
    AbilitySystem->SetNumericAttributeBase(UDuckovAttributeSet::GetMaxHealthAttribute(), StartingMaxHealth);
    AbilitySystem->SetNumericAttributeBase(UDuckovAttributeSet::GetHealthAttribute(), StartingMaxHealth);
    AbilitySystem->GetGameplayAttributeValueChangeDelegate(UDuckovAttributeSet::GetHealthAttribute())
        .AddUObject(this, &ThisClass::HandleHealthChanged);
    UpdateHealthBar(GetHealth());
}

void ATrainingDummyActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (UWorld* World = GetWorld()) { World->GetTimerManager().ClearAllTimersForObject(this); }
    Super::EndPlay(EndPlayReason);
}

float ATrainingDummyActor::GetHealth() const
{
    return AbilitySystem->GetNumericAttribute(UDuckovAttributeSet::GetHealthAttribute());
}

float ATrainingDummyActor::GetMaxHealth() const
{
    return AbilitySystem->GetNumericAttribute(UDuckovAttributeSet::GetMaxHealthAttribute());
}

bool ATrainingDummyActor::IsFlashing() const
{
    const UWorld* World = GetWorld();
    return World && World->GetTimerManager().IsTimerActive(FlashTimer);
}

void ATrainingDummyActor::HandleHealthChanged(const FOnAttributeChangeData& Change)
{
    UpdateHealthBar(Change.NewValue);
    if (Change.NewValue >= Change.OldValue) { return; }
    SetBodyColor(TrainingDummySetup::FlashColor);
    GetWorldTimerManager().SetTimer(FlashTimer, this, &ThisClass::EndFlash, FlashDuration, false);
    if (Change.NewValue <= 0.f && !bDown) { FallDown(); }
}

void ATrainingDummyActor::UpdateHealthBar(float CurrentHealth)
{
    const float MaxValue = GetMaxHealth();
    const float Ratio = MaxValue > 0.f ? FMath::Clamp(CurrentHealth / MaxValue, 0.f, 1.f) : 0.f;
    // 왼쪽 끝을 고정하고 오른쪽으로 줄어든다. 배경보다 1 cm 위에 그려 겹쳐 깜박이지 않게 한다.
    const float Width = TrainingDummySetup::BarWidth * Ratio;
    HealthBarFill->SetRelativeLocation(FVector((Width - TrainingDummySetup::BarWidth) * 0.5f, 0.f, 1.f));
    HealthBarFill->SetRelativeScale3D(FVector(Width, TrainingDummySetup::BarDepth, 1.f) / 100.f);
    HealthBarFill->SetVisibility(Ratio > 0.f);
}

void ATrainingDummyActor::SetBodyColor(const FLinearColor& Color)
{
    if (BodyMaterial) { BodyMaterial->SetVectorParameterValue(TEXT("Color"), Color); }
}

void ATrainingDummyActor::EndFlash()
{
    SetBodyColor(TrainingDummySetup::BodyColor);
}

void ATrainingDummyActor::FallDown()
{
    bDown = true;
    // 누운 동안에는 공격이 지나가고 체력 막대를 감춘다. 다시 설 때는 배치된 충돌 설정으로 되돌린다.
    StandingCollision = Body->GetCollisionEnabled();
    Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Body->SetRelativeLocationAndRotation(TrainingDummySetup::DownLocation, TrainingDummySetup::DownRotation);
    HealthBar->SetVisibility(false, true);
    GetWorldTimerManager().SetTimer(RecoverTimer, this, &ThisClass::Recover, FMath::Max(RecoverDelay, 0.1f), false);
}

void ATrainingDummyActor::Recover()
{
    bDown = false;
    Body->SetCollisionEnabled(StandingCollision);
    Body->SetRelativeLocationAndRotation(TrainingDummySetup::BodyLocation, FRotator::ZeroRotator);
    HealthBar->SetVisibility(true, true);
    AbilitySystem->SetNumericAttributeBase(UDuckovAttributeSet::GetHealthAttribute(), GetMaxHealth());
}
