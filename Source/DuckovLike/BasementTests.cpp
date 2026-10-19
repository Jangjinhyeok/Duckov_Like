#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "BasementGameMode.h"
#include "CustomizationAppearanceComponent.h"
#include "CustomizationModel.h"
#include "DuckovCharacter.h"
#include "GameSessionSubsystem.h"
#include "DuckovPlayerController.h"
#include "InteractionPointActor.h"
#include "InteractionRangeComponent.h"
#include "InventoryScreenWidget.h"
#include "WorldLootActor.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Engine/StaticMeshActor.h"
#include "Components/SkyLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Input/CommonUIActionRouterBase.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/PackageName.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "UObject/UnrealType.h"
#include "Widgets/SViewport.h"
#include "Engine/GameViewportClient.h"

namespace BasementTests
{
namespace
{
constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
constexpr const TCHAR* BasementMap = TEXT("/Game/Maps/L_Basement");
constexpr const TCHAR* RaidMap = TEXT("/Game/Maps/L_Raid");

template <typename T>
T* Field(UObject* Owner, FName Name)
{
    const FObjectProperty* Property = Owner ? FindFProperty<FObjectProperty>(Owner->GetClass(), Name) : nullptr;
    return Property ? Cast<T>(Property->GetObjectPropertyValue_InContainer(Owner)) : nullptr;
}

class FWaitFor final : public IAutomationLatentCommand
{
public:
    FWaitFor(FAutomationTestBase& InTest, FString InDescription, TFunction<bool()> InCondition)
        : Test(InTest), Description(MoveTemp(InDescription)), Condition(MoveTemp(InCondition)) {}
    virtual bool Update() override
    {
        if (Started == 0.0) { Started = FPlatformTime::Seconds(); }
        if (Condition()) { return true; }
        if (FPlatformTime::Seconds() - Started < 20.0) { return false; }
        Test.AddError(FString::Printf(TEXT("PIE timeout: %s"), *Description));
        return true;
    }
private:
    FAutomationTestBase& Test;
    FString Description;
    TFunction<bool()> Condition;
    double Started = 0.0;
};

struct FBasementScene
{
    TWeakObjectPtr<ADuckovPlayerController> Controller;
    TWeakObjectPtr<ADuckovCharacter> Character;
    FVector Start = FVector::ZeroVector;
    // 자동 실행 중 프레임이 멈추면 벽시계 시간만으로는 이동이 시뮬레이션되지 않는다. 월드 시간과 프레임 수로 기다린다.
    double HoldStarted = 0.0;
    uint64 HoldStartFrame = 0;
    float PeakSpeed = 0.f;
    bool bOriginalInactiveInput = false;
};

AStaticMeshActor* SpawnBlock(UWorld* World, const TCHAR* Label, const FVector& Location, const FVector& Scale)
{
    UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    AStaticMeshActor* Block = World->SpawnActor<AStaticMeshActor>(Location, FRotator::ZeroRotator);
    Block->GetStaticMeshComponent()->SetStaticMesh(Cube);
    Block->SetActorScale3D(Scale);
    Block->SetActorLabel(Label);
    return Block;
}

void SpawnLighting(UWorld* World)
{
    ADirectionalLight* Sun = World->SpawnActor<ADirectionalLight>(FVector(0.f, 0.f, 800.f), FRotator(-50.f, 30.f, 0.f));
    Sun->SetActorLabel(TEXT("KeyLight"));
    ASkyAtmosphere* Sky = World->SpawnActor<ASkyAtmosphere>(FVector::ZeroVector, FRotator::ZeroRotator);
    Sky->SetActorLabel(TEXT("SkyAtmosphere"));
    ASkyLight* Ambient = World->SpawnActor<ASkyLight>(FVector(0.f, 0.f, 500.f), FRotator::ZeroRotator);
    Ambient->SetActorLabel(TEXT("AmbientLight"));
    Ambient->GetLightComponent()->SetMobility(EComponentMobility::Movable);
    Ambient->GetLightComponent()->bRealTimeCapture = true;
}

// 상호작용 지점은 mesh 모양으로 구분한다. 창고는 부딪히는 상자, 출격 지점은 밟고 지나가는 원판이다.
AInteractionPointActor* SpawnPoint(UWorld* World, const TCHAR* Label, EInteractionKind Kind, const FVector& Location,
    const TCHAR* MeshPath, const FVector& Scale, bool bBlocks)
{
    AInteractionPointActor* Point = World->SpawnActor<AInteractionPointActor>(Location, FRotator::ZeroRotator);
    Point->GetRange()->Kind = Kind;
    UStaticMeshComponent* Mesh = Point->GetMesh();
    Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, MeshPath));
    Mesh->SetMaterial(0, LoadObject<UMaterialInterface>(nullptr,
        TEXT("/Engine/EngineDebugMaterials/DebugMeshMaterialFakeLight.DebugMeshMaterialFakeLight")));
    Mesh->SetRelativeScale3D(Scale);
    if (!bBlocks) { Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision); }
    Point->SetActorLabel(Label);
    return Point;
}

void SendKey(FKey Key, bool bDown)
{
    FSlateApplication& Slate = FSlateApplication::Get();
    const FKeyEvent Event(Key, FModifierKeysState(), Slate.GetUserIndexForKeyboard(), false, 0, 0);
    if (bDown) { Slate.ProcessKeyDownEvent(Event); }
    else { Slate.ProcessKeyUpEvent(Event); }
}

bool IsViewportFocused()
{
    return GEngine->GameViewport && GEngine->GameViewport->GetGameViewportWidget().IsValid()
        && GEngine->GameViewport->GetGameViewportWidget()->HasAnyUserFocus().IsSet();
}

ECommonInputMode GetInputMode(ADuckovPlayerController* Controller)
{
    UCommonUIActionRouterBase* Router = Controller && Controller->GetLocalPlayer()
        ? Controller->GetLocalPlayer()->GetSubsystem<UCommonUIActionRouterBase>() : nullptr;
    return Router ? Router->GetActiveInputMode() : ECommonInputMode::All;
}

// 앞 단계가 timeout으로 실패해도 다음 단계가 null을 건드리지 않고 실패로 끝나게 한다.
bool IsSceneValid(const FBasementScene& Scene)
{
    return Scene.Controller.IsValid() && Scene.Character.IsValid();
}

void BeginHold(FBasementScene& Scene)
{
    Scene.Start = Scene.Character->GetActorLocation();
    Scene.HoldStarted = Scene.Character->GetWorld()->GetTimeSeconds();
    Scene.HoldStartFrame = GFrameCounter;
}

bool IsHoldDone(const FBasementScene& Scene, double Seconds)
{
    return !IsSceneValid(Scene) || (Scene.Character->GetWorld()->GetTimeSeconds() - Scene.HoldStarted > Seconds
        && GFrameCounter - Scene.HoldStartFrame >= 10);
}

FVector MovedSinceHold(const FBasementScene& Scene)
{
    return (Scene.Character->GetActorLocation() - Scene.Start) * FVector(1.f, 1.f, 0.f);
}

// 카메라가 고정 yaw이므로 화면 위는 카메라 yaw의 지면 방향이다.
FVector ScreenUpOnGround(const ADuckovCharacter* Character)
{
    return FRotator(0.f, Character->GetCameraBoom()->GetComponentRotation().Yaw, 0.f).Vector();
}
}

// 명시적으로 실행하는 에셋 생성 경로다. 기존 맵을 덮어쓰지 않으며 회귀 검사에서는 실행하지 않는다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCreateBasementAssets, "BasementAssets.Create", Flags)
bool FCreateBasementAssets::RunTest(const FString& Parameters)
{
    if (FPackageName::DoesPackageExist(BasementMap))
    {
        AddError(FString::Printf(TEXT("기존 에셋을 덮어쓰지 않습니다: %s"), BasementMap));
        return false;
    }
    UWorld* World = UEditorLoadingAndSavingUtils::NewBlankMap(false);
    if (!World) { AddError(TEXT("베이스먼트 map 생성 실패")); return false; }
    World->GetWorldSettings()->DefaultGameMode = ABasementGameMode::StaticClass();
    // 20 m 정사각형 방. 바닥 윗면이 z=0이다. 카메라 반대쪽 두 벽만 세워 원작처럼 방 안이 보이게 한다.
    SpawnBlock(World, TEXT("Floor"), FVector(0.f, 0.f, -10.f), FVector(20.f, 20.f, 0.2f));
    SpawnBlock(World, TEXT("WallFar"), FVector(1010.f, 0.f, 125.f), FVector(0.2f, 20.4f, 2.5f));
    SpawnBlock(World, TEXT("WallSide"), FVector(0.f, -1010.f, 125.f), FVector(20.4f, 0.2f, 2.5f));
    SpawnBlock(World, TEXT("Crate_A"), FVector(400.f, 300.f, 50.f), FVector(1.f));
    SpawnBlock(World, TEXT("Crate_B"), FVector(-300.f, -450.f, 50.f), FVector(1.5f, 1.f, 1.f));
    SpawnBlock(World, TEXT("Crate_C"), FVector(650.f, -600.f, 75.f), FVector(1.f, 1.f, 1.5f));
    // 창고는 옆 벽에 붙이고, 출격 지점은 먼 벽 앞에 둔다. 시작 위치에서 둘 다 범위 밖이다.
    SpawnPoint(World, TEXT("Stash"), EInteractionKind::Stash, FVector(-250.f, -900.f, 50.f),
        TEXT("/Engine/BasicShapes/Cube.Cube"), FVector(1.6f, 0.8f, 1.f), true);
    SpawnPoint(World, TEXT("SortiePoint"), EInteractionKind::Sortie, FVector(850.f, 350.f, 2.f),
        TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), FVector(2.f, 2.f, 0.04f), false);
    SpawnLighting(World);
    APlayerStart* Start = World->SpawnActor<APlayerStart>(FVector(0.f, 0.f, 100.f), FRotator::ZeroRotator);
    Start->SetActorLabel(TEXT("PlayerStart"));
    if (!UEditorLoadingAndSavingUtils::SaveMap(World, BasementMap)) { AddError(TEXT("베이스먼트 map 저장 실패")); return false; }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBasementAssets, "Duckov.Basement.Assets", Flags)
bool FBasementAssets::RunTest(const FString& Parameters)
{
    UWorld* World = LoadObject<UWorld>(nullptr, TEXT("/Game/Maps/L_Basement.L_Basement"));
    if (!TestNotNull(TEXT("베이스먼트 map"), World)) { return false; }
    TestTrue(TEXT("베이스먼트 GameMode override"), World->GetWorldSettings()->DefaultGameMode == ABasementGameMode::StaticClass());
    const ABasementGameMode* GameMode = GetDefault<ABasementGameMode>();
    TestTrue(TEXT("베이스먼트 pawn은 지상 캐릭터"), GameMode->DefaultPawnClass == ADuckovCharacter::StaticClass());
    TestTrue(TEXT("베이스먼트 controller는 지상 흐름 controller"), GameMode->PlayerControllerClass == ADuckovPlayerController::StaticClass());
    int32 PlayerStarts = 0;
    bool bFloorCollides = false;
    TMap<EInteractionKind, int32> Points;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        if (It->IsA<APlayerStart>()) { ++PlayerStarts; }
        if (const AInteractionPointActor* Point = Cast<AInteractionPointActor>(*It)) { ++Points.FindOrAdd(Point->GetRange()->Kind); }
        if (const AStaticMeshActor* Mesh = Cast<AStaticMeshActor>(*It); Mesh && Mesh->GetActorLabel() == TEXT("Floor"))
        {
            bFloorCollides = Mesh->GetStaticMeshComponent()->IsCollisionEnabled();
        }
    }
    TestEqual(TEXT("PlayerStart 하나"), PlayerStarts, 1);
    TestTrue(TEXT("바닥 충돌"), bFloorCollides);
    TestEqual(TEXT("창고 하나"), Points.FindRef(EInteractionKind::Stash), 1);
    TestEqual(TEXT("출격 지점 하나"), Points.FindRef(EInteractionKind::Sortie), 1);
    TestEqual(TEXT("베이스먼트에는 탈출 지점 없음"), Points.FindRef(EInteractionKind::Extraction), 0);
    return true;
}

// 명시적으로 실행하는 레이드 맵 생성 경로다. 베이스먼트와 같은 GameMode·조명을 쓰고 loot와 탈출 지점만 다르다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCreateRaidAssets, "RaidAssets.Create", Flags)
bool FCreateRaidAssets::RunTest(const FString& Parameters)
{
    if (FPackageName::DoesPackageExist(RaidMap))
    {
        AddError(FString::Printf(TEXT("기존 에셋을 덮어쓰지 않습니다: %s"), RaidMap));
        return false;
    }
    UWorld* World = UEditorLoadingAndSavingUtils::NewBlankMap(false);
    if (!World) { AddError(TEXT("레이드 map 생성 실패")); return false; }
    World->GetWorldSettings()->DefaultGameMode = ABasementGameMode::StaticClass();
    // 30 m 정사각형 바깥 구역. 시작은 한쪽 모서리, 탈출 지점은 대각선 반대 모서리다.
    SpawnBlock(World, TEXT("Floor"), FVector(0.f, 0.f, -10.f), FVector(30.f, 30.f, 0.2f));
    SpawnBlock(World, TEXT("WallFar"), FVector(1510.f, 0.f, 125.f), FVector(0.2f, 30.4f, 2.5f));
    SpawnBlock(World, TEXT("WallSide"), FVector(0.f, -1510.f, 125.f), FVector(30.4f, 0.2f, 2.5f));
    SpawnBlock(World, TEXT("Cover_A"), FVector(-600.f, -200.f, 75.f), FVector(3.f, 0.6f, 1.5f));
    SpawnBlock(World, TEXT("Cover_B"), FVector(300.f, 700.f, 75.f), FVector(0.6f, 3.f, 1.5f));
    SpawnBlock(World, TEXT("Cover_C"), FVector(800.f, -500.f, 50.f), FVector(1.5f));
    const TSoftObjectPtr<UDataTable> Definitions(FSoftObjectPath(TEXT("/Game/Inventory/DT_ItemDefinitions.DT_ItemDefinitions")));
    auto SpawnLoot = [World, &Definitions](const TCHAR* Label, FName Row, int32 Quantity, const FVector& Location)
    {
        AWorldLootActor* Loot = World->SpawnActor<AWorldLootActor>(Location, FRotator::ZeroRotator);
        Loot->DefinitionTable = Definitions;
        Loot->DefinitionRowName = Row;
        Loot->Quantity = Quantity;
        Loot->SetActorLabel(Label);
    };
    // 시작 가방(소형 4x4, 구급상자 2개 차지)에 모두 들어가는 양이다.
    SpawnLoot(TEXT("Loot_Medkit"), TEXT("Medkit"), 1, FVector(-700.f, -800.f, 15.f));
    SpawnLoot(TEXT("Loot_Ammo"), TEXT("Ammo"), 12, FVector(0.f, -300.f, 15.f));
    SpawnLoot(TEXT("Loot_Rifle"), TEXT("Rifle"), 1, FVector(500.f, 300.f, 15.f));
    SpawnPoint(World, TEXT("ExtractionPoint"), EInteractionKind::Extraction, FVector(1150.f, 1150.f, 2.f),
        TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), FVector(3.f, 3.f, 0.04f), false);
    SpawnLighting(World);
    APlayerStart* Start = World->SpawnActor<APlayerStart>(FVector(-1200.f, -1200.f, 100.f), FRotator::ZeroRotator);
    Start->SetActorLabel(TEXT("PlayerStart"));
    if (!UEditorLoadingAndSavingUtils::SaveMap(World, RaidMap)) { AddError(TEXT("레이드 map 저장 실패")); return false; }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRaidAssets, "Duckov.Raid.Assets", Flags)
bool FRaidAssets::RunTest(const FString& Parameters)
{
    UWorld* World = LoadObject<UWorld>(nullptr, TEXT("/Game/Maps/L_Raid.L_Raid"));
    if (!TestNotNull(TEXT("레이드 map"), World)) { return false; }
    TestTrue(TEXT("레이드도 지상 GameMode"), World->GetWorldSettings()->DefaultGameMode == ABasementGameMode::StaticClass());
    int32 PlayerStarts = 0;
    int32 Loot = 0;
    TMap<EInteractionKind, int32> Points;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        if (It->IsA<APlayerStart>()) { ++PlayerStarts; }
        if (const AWorldLootActor* Item = Cast<AWorldLootActor>(*It); Item && !Item->DefinitionRowName.IsNone()) { ++Loot; }
        if (const AInteractionPointActor* Point = Cast<AInteractionPointActor>(*It)) { ++Points.FindOrAdd(Point->GetRange()->Kind); }
    }
    TestEqual(TEXT("PlayerStart 하나"), PlayerStarts, 1);
    TestEqual(TEXT("loot 셋"), Loot, 3);
    TestEqual(TEXT("탈출 지점 하나"), Points.FindRef(EInteractionKind::Extraction), 1);
    TestEqual(TEXT("레이드에는 창고 없음"), Points.FindRef(EInteractionKind::Stash), 0);
    TestEqual(TEXT("레이드에는 출격 지점 없음"), Points.FindRef(EInteractionKind::Sortie), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBasementCharacterMovement, "Duckov.Basement.CharacterMovement", Flags)
bool FBasementCharacterMovement::RunTest(const FString& Parameters)
{
    if (!TestTrue(TEXT("Slate initialized"), FSlateApplication::IsInitialized())) { return false; }
    if (!TestTrue(TEXT("베이스먼트 map load"), AutomationOpenMap(BasementMap))) { return false; }
    TSharedRef<FBasementScene> Scene = MakeShared<FBasementScene>();
    Scene->bOriginalInactiveInput = FSlateApplication::Get().GetHandleDeviceInputWhenApplicationNotActive();
    FSlateApplication::Get().SetHandleDeviceInputWhenApplicationNotActive(true);
    ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(*this, TEXT("지상 캐릭터 착지"), [Scene]
    {
        UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
        auto* Controller = World ? Cast<ADuckovPlayerController>(UGameplayStatics::GetPlayerController(World, 0)) : nullptr;
        auto* Character = Controller ? Cast<ADuckovCharacter>(Controller->GetPawn()) : nullptr;
        Scene->Controller = Controller;
        Scene->Character = Character;
        return Character && Character->GetCharacterMovement()->IsMovingOnGround();
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        if (!IsSceneValid(*Scene)) { AddError(TEXT("캐릭터 unavailable")); return; }
        ADuckovCharacter* Character = Scene->Character.Get();
        UCustomizationAppearanceComponent* Appearance = Character->GetAppearance();
        USkeletalMeshComponent* Body = Appearance->GetDisplayedBody();
        TestTrue(TEXT("세션 Profile로 외형 표시"), Body && Body->IsVisible() && Appearance->GetDisplayedMesh());
        const UAnimSingleNodeInstance* Locomotion = Body ? Body->GetSingleNodeInstance() : nullptr;
        TestTrue(TEXT("body가 tick하며 걷기 BlendSpace 재생"), Locomotion && Body->IsComponentTickEnabled() && Locomotion->GetAnimationAsset()
            && Locomotion->GetAnimationAsset()->GetPathName() == ADuckovCharacter::LocomotionAnimationPath);
        const auto* Hair = Cast<USkeletalMeshComponent>(Appearance->GetDisplayedSlotMesh(ECustomizationSlot::Hair));
        TestTrue(TEXT("부위 파츠가 body pose를 따름"), Hair && Hair->LeaderPoseComponent.Get() == Body);
        TestTrue(TEXT("외형은 capsule 바닥 기준점에 붙음"), Body && Body->GetAttachParent() != Character->GetRootComponent()
            && FMath::IsNearlyEqual(Body->GetComponentLocation().Z,
                Character->GetActorLocation().Z - Character->GetSimpleCollisionHalfHeight(), 1.f));
        TestTrue(TEXT("카메라 고정 각도"), Character->GetCameraBoom()->GetComponentRotation().Equals(FRotator(-55.f, -40.f, 0.f), 0.1f));
        // 게임 중 커서는 Slate가 그리는 십자선이고, 커서 위치를 읽도록 보이게 둔다.
        ADuckovPlayerController* Controller = Scene->Controller.Get();
        UGameViewportClient* ViewportClient = Character->GetWorld()->GetGameViewport();
        TestTrue(TEXT("게임 커서는 보이는 십자선"), Controller->ShouldShowMouseCursor()
            && Controller->CurrentMouseCursor == EMouseCursor::Crosshairs && ViewportClient && ViewportClient->HasSoftwareCursor(EMouseCursor::Crosshairs));
        // 화면 좌표를 캐릭터 높이의 수평면에 투영하고, 그 지점을 다시 투영하면 같은 화면 좌표다.
        int32 SizeX = 0, SizeY = 0;
        Controller->GetViewportSize(SizeX, SizeY);
        const FVector2D ScreenRight(SizeX * 0.8f, SizeY * 0.5f);
        FVector AimPoint;
        FVector2D Projected;
        TestTrue(TEXT("커서 화면 좌표가 캐릭터 높이의 조준 지점"), Controller->ScreenToAimPoint(ScreenRight, AimPoint)
            && FMath::IsNearlyEqual(AimPoint.Z, Character->GetActorLocation().Z, 0.1f)
            && Controller->ProjectWorldLocationToScreen(AimPoint, Projected) && Projected.Equals(ScreenRight, 1.f));
        // 실제 마우스가 viewport 밖이어도 아래 회전 검사가 의미를 갖도록 조준 지점을 하나 둔다. 마우스가 안에 있으면 그 값으로 바뀐다.
        Character->SetAimPoint(AimPoint);
        // 확정 Profile이 바뀌면 같은 캐릭터가 바로 다시 그린다.
        UCustomizationModel* Model = Character->GetGameInstance()->GetSubsystem<UGameSessionSubsystem>()->GetCustomizationModel();
        auto Present = [](const FCustomizationProfile&) { return ECustomizationFailure::None; };
        FCustomizationProfile Candidate = Model->GetProfile();
        Candidate.SkinColor = 10;
        TestEqual(TEXT("편집 시작"), Model->BeginEdit(Present), ECustomizationFailure::None);
        TestEqual(TEXT("편집"), Model->TryEdit(Candidate, Present), ECustomizationFailure::None);
        TestEqual(TEXT("확정"), Model->Apply(Present), ECustomizationFailure::None);
        FLinearColor Shown = FLinearColor::Black;
        UMaterialInstanceDynamic* BodyMID = Appearance->GetDisplayedBodyMID();
        TestTrue(TEXT("확정 색상이 캐릭터에 반영"), BodyMID && BodyMID->GetVectorParameterValue(FMaterialParameterInfo(TEXT("Color")), Shown)
            && Shown.Equals(GetCustomizationColor(10), 0.001f));
        FSlateApplication::Get().SetAllUserFocusToGameViewport();
    }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(*this, TEXT("viewport focus"), [] { return IsViewportFocused(); }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        if (!IsSceneValid(*Scene)) { AddError(TEXT("베이스먼트 scene unavailable")); return; }
        TestFalse(TEXT("레이저 사이트는 기본 꺼짐"), Scene->Character->IsLaserSightEnabled());
        // 테스트용 L키로 레이저 사이트를 켠 채 걷는다.
        SendKey(EKeys::L, true);
        SendKey(EKeys::L, false);
        BeginHold(*Scene);
        SendKey(EKeys::W, true);
    }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(*this, TEXT("W 유지"), [Scene] { return IsHoldDone(*Scene, 0.6); }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        SendKey(EKeys::W, false);
        if (!IsSceneValid(*Scene)) { AddError(TEXT("베이스먼트 scene unavailable")); return; }
        ADuckovCharacter* Character = Scene->Character.Get();
        const FVector Delta = MovedSinceHold(*Scene);
        UE_LOG(LogTemp, Display, TEXT("Basement: W 이동 %.1f cm, 방향 일치 %.3f"), Delta.Size(),
            FVector::DotProduct(Delta.GetSafeNormal(), ScreenUpOnGround(Character)));
        TestTrue(TEXT("W 키로 실제 이동"), Delta.Size() > 80.f);
        TestTrue(TEXT("W는 화면 위 방향"), FVector::DotProduct(Delta.GetSafeNormal(), ScreenUpOnGround(Character)) > 0.95f);
        // 회전은 이동 방향이 아니라 조준 지점(마우스 커서)을 따른다.
        // 실제 마우스가 몸 안쪽을 가리키면 돌지 않는 것이 의도라 그때는 방향을 보지 않는다.
        const FVector ToAim = Character->GetAimPoint() - Character->GetActorLocation();
        const bool bAimOutsideBody = ToAim.Size2D() > Character->GetSimpleCollisionRadius();
        TestTrue(TEXT("이동 중에도 조준 지점을 바라봄"), Character->HasAimPoint()
            && (!bAimOutsideBody || FVector::DotProduct(Character->GetActorForwardVector(), ToAim.GetSafeNormal2D()) > 0.99f));
        // 외형 component의 회전 보정이 다시 들어오는 회귀를 막는다. 부리가 +X를 향한다는 사실 자체는 독립 실행 화면으로 확인했다.
        const USkeletalMeshComponent* Body = Character->GetAppearance()->GetDisplayedBody();
        TestTrue(TEXT("외형 정면이 캐릭터 정면"), Body
            && FVector::DotProduct(Body->GetForwardVector(), Character->GetActorForwardVector()) > 0.99f);
        // component 축만으로는 재생 중 pose가 돌아간 것을 못 잡는다. 실제 왼쪽 날개 bone이 캐릭터 왼쪽에 있어야 한다.
        TestTrue(TEXT("재생 중 왼쪽 날개가 캐릭터 왼쪽"), Body && FVector::DotProduct(
            Body->GetBoneLocation(TEXT("wing_l")) - Character->GetActorLocation(), Character->GetActorRightVector()) < -15.f);
        TestTrue(TEXT("캐릭터가 돌아도 카메라는 고정"), Character->GetCameraBoom()->GetComponentRotation().Equals(FRotator(-55.f, -40.f, 0.f), 0.1f));
        // 조준 지점이 몸 안쪽이면 선을 숨기는 것이 의도라 그때는 표시를 보지 않는다.
        TestTrue(TEXT("L키로 레이저 사이트 켜짐"), Character->IsLaserSightEnabled()
            && (!bAimOutsideBody || Character->GetAimDot()->IsVisible()));
        // 키 입력은 다음 frame에 처리되므로 꺼짐은 다음 단계에서 본다.
        SendKey(EKeys::L, true);
        SendKey(EKeys::L, false);
        BeginHold(*Scene);
        SendKey(EKeys::A, true);
    }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(*this, TEXT("A 유지"), [Scene] { return IsHoldDone(*Scene, 0.4); }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        SendKey(EKeys::A, false);
        if (!IsSceneValid(*Scene)) { AddError(TEXT("베이스먼트 scene unavailable")); return; }
        TestTrue(TEXT("L키를 다시 누르면 꺼짐"), !Scene->Character->IsLaserSightEnabled()
            && !Scene->Character->GetAimLine()->IsVisible() && !Scene->Character->GetAimDot()->IsVisible());
        const FVector ScreenLeft = -FVector::CrossProduct(FVector::UpVector, ScreenUpOnGround(Scene->Character.Get()));
        const FVector Delta = MovedSinceHold(*Scene);
        // 키를 바꾼 직후에는 이전 방향의 관성이 남으므로 hold 끝의 속도 방향으로 판정한다.
        TestTrue(TEXT("A는 화면 왼쪽 방향"), Delta.Size() > 40.f
            && FVector::DotProduct(Scene->Character->GetVelocity().GetSafeNormal2D(), ScreenLeft) > 0.95f);
        BeginHold(*Scene);
        SendKey(EKeys::S, true);
    }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(*this, TEXT("S 유지"), [Scene] { return IsHoldDone(*Scene, 0.4); }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        SendKey(EKeys::S, false);
        if (!IsSceneValid(*Scene)) { AddError(TEXT("베이스먼트 scene unavailable")); return; }
        const FVector Delta = MovedSinceHold(*Scene);
        TestTrue(TEXT("S는 화면 아래 방향"), Delta.Size() > 40.f
            && FVector::DotProduct(Scene->Character->GetVelocity().GetSafeNormal2D(), -ScreenUpOnGround(Scene->Character.Get())) > 0.95f);
        Scene->PeakSpeed = 0.f;
        BeginHold(*Scene);
        SendKey(EKeys::LeftShift, true);
        SendKey(EKeys::D, true);
    }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(*this, TEXT("Shift+D 유지"), [Scene]
    {
        if (Scene->Character.IsValid()) { Scene->PeakSpeed = FMath::Max(Scene->PeakSpeed, Scene->Character->GetVelocity().Size2D()); }
        return IsHoldDone(*Scene, 0.6);
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        SendKey(EKeys::D, false);
        SendKey(EKeys::LeftShift, false);
        if (!IsSceneValid(*Scene)) { AddError(TEXT("베이스먼트 scene unavailable")); return; }
        const FVector ScreenRight = FVector::CrossProduct(FVector::UpVector, ScreenUpOnGround(Scene->Character.Get()));
        TestTrue(TEXT("D는 화면 오른쪽 방향"), FVector::DotProduct(Scene->Character->GetVelocity().GetSafeNormal2D(), ScreenRight) > 0.95f);
        TestTrue(TEXT("Shift로 걷기보다 빠르게 달림"), Scene->PeakSpeed > ADuckovCharacter::WalkSpeed + 100.f);
        FVector Input = FVector::ZeroVector, Filtered = FVector::ZeroVector;
        const USkeletalMeshComponent* Shown = Scene->Character->GetAppearance()->GetDisplayedBody();
        UAnimSingleNodeInstance* Locomotion = Shown ? Shown->GetSingleNodeInstance() : nullptr;
        if (Locomotion) { Locomotion->GetBlendSpaceState(Input, Filtered); }
        TestTrue(TEXT("달리는 속도가 걷기 애니메이션 입력"), Locomotion && Input.X > ADuckovCharacter::WalkSpeed + 100.f);
        Scene->Controller->ToggleInventory();
    }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(*this, TEXT("인벤토리 Menu 입력과 정지"), [Scene]
    {
        return !IsSceneValid(*Scene)
            || (GetInputMode(Scene->Controller.Get()) == ECommonInputMode::Menu && Scene->Character->GetVelocity().Size2D() < 1.f);
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        if (!IsSceneValid(*Scene)) { AddError(TEXT("베이스먼트 scene unavailable")); return; }
        BeginHold(*Scene);
        SendKey(EKeys::W, true);
    }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(*this, TEXT("메뉴 중 W 유지"), [Scene] { return IsHoldDone(*Scene, 0.5); }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        SendKey(EKeys::W, false);
        if (!IsSceneValid(*Scene)) { AddError(TEXT("베이스먼트 scene unavailable")); return; }
        TestTrue(TEXT("인벤토리가 열린 동안 이동 입력 차단"), MovedSinceHold(*Scene).Size() < 5.f);
        FVector Input = FVector::ZeroVector, Filtered = FVector::ZeroVector;
        const USkeletalMeshComponent* Shown = Scene->Character->GetAppearance()->GetDisplayedBody();
        UAnimSingleNodeInstance* Locomotion = Shown ? Shown->GetSingleNodeInstance() : nullptr;
        if (Locomotion) { Locomotion->GetBlendSpaceState(Input, Filtered); }
        TestTrue(TEXT("멈추면 Idle 입력"), Locomotion && Input.X < 1.f);
        UInventoryScreenWidget* Screen = Field<UInventoryScreenWidget>(Scene->Controller.Get(), TEXT("Screen"));
        TestTrue(TEXT("베이스먼트에서 인벤토리 열림"), Screen && Screen->IsActivated());
        Scene->Controller->ToggleInventory();
    }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(*this, TEXT("인벤토리 닫은 뒤 Game 입력"), [Scene]
    {
        return !IsSceneValid(*Scene) || (GetInputMode(Scene->Controller.Get()) == ECommonInputMode::Game && IsViewportFocused());
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(*this, TEXT("PIE 종료"), [] { return !GEditor || !GEditor->PlayWorld; }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([Scene]
    {
        FSlateApplication::Get().SetHandleDeviceInputWhenApplicationNotActive(Scene->bOriginalInactiveInput);
    }, 0.f));
    return true;
}
}
#endif
