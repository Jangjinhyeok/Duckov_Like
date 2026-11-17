#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "AimCrosshair.h"
#include "Algo/AllOf.h"
#include "BasementGameMode.h"
#include "CombatTestSupport.h"
#include "CustomizationAppearanceComponent.h"
#include "CustomizationModel.h"
#include "DuckovCharacter.h"
#include "GameHudWidget.h"
#include "GameSessionSubsystem.h"
#include "DuckovPlayerController.h"
#include "InteractionPointActor.h"
#include "InteractionRangeComponent.h"
#include "InventoryModel.h"
#include "InventoryScreenWidget.h"
#include "TrainingDummyActor.h"
#include "WorldLootActor.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/Border.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextBlock.h"
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

// 과녁은 시작 위치에서 보이되 창고·출격·탈출 지점과 loot로 가는 길에서 벗어난 곳에 둔다.
const FVector BasementDummyLocation(-600.f, 450.f, 0.f);
const FVector RaidDummyLocation(-400.f, -1100.f, 0.f);

void SpawnTrainingDummy(UWorld* World, const FVector& Location)
{
    ATrainingDummyActor* Dummy = World->SpawnActor<ATrainingDummyActor>(Location, FRotator::ZeroRotator);
    Dummy->SetActorLabel(TEXT("TrainingDummy"));
}

int32 CountTrainingDummies(UWorld* World)
{
    int32 Count = 0;
    for (TActorIterator<ATrainingDummyActor> It(World); It; ++It) { ++Count; }
    return Count;
}

void SendKey(FKey Key, bool bDown)
{
    FSlateApplication& Slate = FSlateApplication::Get();
    const FKeyEvent Event(Key, FModifierKeysState(), Slate.GetUserIndexForKeyboard(), false, 0, 0);
    if (bDown) { Slate.ProcessKeyDownEvent(Event); }
    else { Slate.ProcessKeyUpEvent(Event); }
}

// 마우스 버튼은 key 이벤트가 아니라 pointer 이벤트로 viewport에 들어간다. game viewport 경로로 눌렀다 뗀다.
// 경로를 직접 지정해 Slate hit-test는 거치지 않는다. viewport → InputKey → controller binding 구간만 증명한다.
bool ClickGameViewport(FKey Button)
{
    FSlateApplication& Slate = FSlateApplication::Get();
    const TSharedPtr<SViewport> Viewport = GEngine->GameViewport ? GEngine->GameViewport->GetGameViewportWidget() : nullptr;
    FWidgetPath Path;
    if (!Viewport.IsValid() || !Slate.FindPathToWidget(Viewport.ToSharedRef(), Path)) { return false; }
    // 위젯 탐색 경로에 없는 pointer 슬롯을 routing용 생성자로 채운다.
    TArray<FWidgetAndPointer> WidgetsAndPointers;
    for (int32 Index = 0; Index < Path.Widgets.Num(); ++Index)
    {
        WidgetsAndPointers.Emplace(Path.Widgets[Index], TOptional<FVirtualPointerPosition>());
    }
    Path = FWidgetPath(MakeArrayView(WidgetsAndPointers));
    const FGeometry& Geometry = Path.Widgets.Last().Geometry;
    const FVector2D Position = Geometry.GetAbsolutePosition() + Geometry.GetAbsoluteSize() * 0.5f;
    TSet<FKey> Pressed;
    Pressed.Add(Button);
    const int32 UserIndex = Slate.GetUserIndexForMouse();
    Slate.RoutePointerDownEvent(Path, FPointerEvent(UserIndex, FSlateApplication::CursorPointerIndex, Position, Position,
        Pressed, Button, 0.f, FModifierKeysState()));
    Slate.RoutePointerUpEvent(Path, FPointerEvent(UserIndex, FSlateApplication::CursorPointerIndex, Position, Position,
        TSet<FKey>(), Button, 0.f, FModifierKeysState()));
    return true;
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

UTextBlock* GetHudTextBlock(ADuckovPlayerController* Controller, FName Name)
{
    return Field<UTextBlock>(Field<UGameHudWidget>(Controller, TEXT("Hud")), Name);
}

// 오른쪽 아래 탄약 HUD를 "상태  잔탄 / 가방" 한 줄로 읽는다. 상태 문구가 없으면 "잔탄 / 가방"이고 보이지 않으면 빈 문자열이다.
FString GetAmmoHud(ADuckovPlayerController* Controller)
{
    UBorder* Panel = Field<UBorder>(Field<UGameHudWidget>(Controller, TEXT("Hud")), TEXT("AmmoPanel"));
    UTextBlock* Status = GetHudTextBlock(Controller, TEXT("AmmoStatusText"));
    UTextBlock* Rounds = GetHudTextBlock(Controller, TEXT("AmmoRoundsText"));
    UTextBlock* Carried = GetHudTextBlock(Controller, TEXT("AmmoCarriedText"));
    if (!Panel || !Status || !Rounds || !Carried || Panel->GetVisibility() == ESlateVisibility::Collapsed) { return FString(); }
    const FString Numbers = Rounds->GetText().ToString() + TEXT(" ") + Carried->GetText().ToString();
    return Status->GetVisibility() == ESlateVisibility::Collapsed ? Numbers : Status->GetText().ToString() + TEXT("  ") + Numbers;
}

// PIE game viewport를 사람이 볼 검증용 PNG로 남긴다(HUD 포함, 커서 위젯 제외).
void CaptureGameViewport(FAutomationTestBase& Test, ADuckovPlayerController* Controller, const TCHAR* Name)
{
    UGameViewportClient* Client = Controller->GetWorld()->GetGameViewport();
    TSharedPtr<SViewport> Widget = Client ? Client->GetGameViewportWidget() : nullptr;
    TArray<FColor> Pixels;
    FIntVector Size = FIntVector::ZeroValue;
    if (Widget && FSlateApplication::Get().TakeScreenshot(Widget.ToSharedRef(), Pixels, Size))
    {
        CombatTestSupport::SaveEvidencePng(Test, Pixels, FIntPoint(Size.X, Size.Y), Name);
    }
}

EAimAmmoState GetAimState(ADuckovPlayerController* Controller)
{
    const TSharedPtr<SAimCrosshair> Crosshair = Controller->GetAimCrosshair();
    return Crosshair ? Crosshair->GetAmmoState() : EAimAmmoState::None;
}

// 손에 든 모형이 body에 붙어 있고 조각이 모두 bVisible대로 그려지는지다.
// 화면에 그려지는 것은 조각들이므로 조각마다 본다. 부모만 숨겨진 채 조각이 보이던 회귀를 막는다.
bool IsModelShown(const USceneComponent* Model, const USkeletalMeshComponent* Body, int32 PieceCount, bool bVisible)
{
    const TArray<TObjectPtr<USceneComponent>>& Pieces = Model->GetAttachChildren();
    return Model->GetAttachParent() == Body && Model->IsVisible() == bVisible && Pieces.Num() == PieceCount
        && Algo::AllOf(Pieces, [bVisible](const USceneComponent* Piece)
        {
            const UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Piece);
            return Primitive && Primitive->ShouldRender() == bVisible;
        });
}

// body가 재생 중인 동작 asset의 경로다.
FString GetBodyAnimationPath(ADuckovCharacter* Holder)
{
    USkeletalMeshComponent* Body = Holder->GetAppearance()->GetDisplayedBody();
    const UAnimSingleNodeInstance* Instance = Body ? Body->GetSingleNodeInstance() : nullptr;
    return Instance && Instance->IsPlaying() && Instance->GetAnimationAsset() ? Instance->GetAnimationAsset()->GetPathName() : FString();
}

// 캐릭터가 총 모형을 보이고 총을 쥔 동작을 재생하는지다. 해제 상태면 모형을 숨기고 기본 동작이다.
bool IsHoldingRifle(ADuckovCharacter* Holder, bool bExpected)
{
    const FString Expected = bExpected ? ADuckovCharacter::RifleLocomotionAnimationPath : ADuckovCharacter::LocomotionAnimationPath;
    return (Holder->GetEquippedWeapon() == EDuckovWeaponKind::Rifle) == bExpected
        && IsModelShown(Holder->GetWeaponModel(), Holder->GetAppearance()->GetDisplayedBody(), 4, bExpected)
        && GetBodyAnimationPath(Holder) == Expected;
}

// 캐릭터가 방망이 모형을 보이는지다. 근접 무기는 기본 이동 동작을 그대로 쓴다.
bool IsHoldingMelee(ADuckovCharacter* Holder, bool bExpected)
{
    return (Holder->GetEquippedWeapon() == EDuckovWeaponKind::Melee) == bExpected
        && IsModelShown(Holder->GetMeleeModel(), Holder->GetAppearance()->GetDisplayedBody(), 2, bExpected);
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
    SpawnTrainingDummy(World, BasementDummyLocation);
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
    TestEqual(TEXT("과녁 하나"), CountTrainingDummies(World), 1);
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
    SpawnTrainingDummy(World, RaidDummyLocation);
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
    TestEqual(TEXT("과녁 하나"), CountTrainingDummies(World), 1);
    return true;
}

// 실제 베이스먼트에서 맨손 LMB는 공격하지 않고, 방망이를 장착하면 LMB가 휘두르기와 근접 공격으로 이어지며,
// 메뉴가 열린 동안은 막히고 맵에 놓인 과녁이 맞는지 본다.
// 실제 마우스 위치가 매 frame 바라보는 방향을 정하므로, 과녁을 맞히는 단계는 방향을 정한 같은 frame에 공격한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMeleeInput, "Duckov.Combat.MeleeInput", Flags)
bool FMeleeInput::RunTest(const FString& Parameters)
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
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([] { FSlateApplication::Get().SetAllUserFocusToGameViewport(); }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(*this, TEXT("viewport focus"), [] { return IsViewportFocused(); }));
    // 시작 인벤토리의 방망이(7)는 창고 (4, 3)에 있고 무기 slot은 비어 있다.
    constexpr int32 BatId = 7;
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        if (!IsSceneValid(*Scene)) { AddError(TEXT("베이스먼트 scene unavailable")); return; }
        TestFalse(TEXT("공격 전에는 판정 원 숨김"), Scene->Character->GetMeleeSwing()->IsVisible());
        TestTrue(TEXT("맨손이면 방망이 모형 없음"), IsHoldingMelee(Scene->Character.Get(), false));
        TestTrue(TEXT("맨손으로 game viewport 클릭"), ClickGameViewport(EKeys::LeftMouseButton));
        BeginHold(*Scene);
    }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(*this, TEXT("맨손 클릭 뒤 대기"), [Scene]
    {
        return !IsSceneValid(*Scene) || IsHoldDone(*Scene, 0.3);
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        if (!IsSceneValid(*Scene)) { AddError(TEXT("베이스먼트 scene unavailable")); return; }
        ADuckovCharacter* Character = Scene->Character.Get();
        TestFalse(TEXT("맨손 LMB는 공격하지 않음"), Character->GetMeleeSwing()->IsVisible());
        TestFalse(TEXT("맨손 LMB는 휘두르지 않음"), Character->IsSwingPlaying());
        // 판정 원은 0.12초만 보이므로 클릭 뒤 확인만으로는 약하다. LMB가 부르는 함수를 직접 불러 같은 frame에 확인한다.
        Scene->Controller->Attack();
        TestFalse(TEXT("맨손 Attack은 판정 원을 띄우지 않음"), Character->GetMeleeSwing()->IsVisible());
        TestFalse(TEXT("맨손 Attack은 휘두르지 않음"), Character->IsSwingPlaying());
        UInventoryModel* Model = Scene->Controller->GetGameInstance()->GetSubsystem<UGameSessionSubsystem>()->GetInventoryModel();
        if (!TestNotNull(TEXT("세션 인벤토리"), Model)) { return; }
        if (!TestTrue(TEXT("방망이를 무기 slot에 장착"),
            Model->TryMove(TEXT("Stash"), TEXT("WeaponSlot"), BatId, FIntPoint::ZeroValue, false) == EInventoryOperationFailure::None)) { return; }
        TestTrue(TEXT("장착하면 방망이 모형"), IsHoldingMelee(Character, true));
        TestTrue(TEXT("방망이는 총 모형 없이 기본 동작"), IsHoldingRifle(Character, false));
        TestEqual(TEXT("근접 무기는 탄약 HUD 없음"), GetAmmoHud(Scene->Controller.Get()), FString());
        TestTrue(TEXT("방망이로 game viewport 클릭"), ClickGameViewport(EKeys::LeftMouseButton));
    }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(*this, TEXT("LMB로 근접 공격"), [Scene]
    {
        return !IsSceneValid(*Scene) || Scene->Character->GetMeleeSwing()->IsVisible();
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        if (!IsSceneValid(*Scene)) { AddError(TEXT("베이스먼트 scene unavailable")); return; }
        TestTrue(TEXT("근접 공격과 함께 휘두르기 재생"), Scene->Character->IsSwingPlaying());
        TestEqual(TEXT("body가 휘두르기를 재생"), GetBodyAnimationPath(Scene->Character.Get()), FString(ADuckovCharacter::MeleeSwingAnimationPath));
    }, 0.f));
    // 휘두르는 도중의 화면을 사람이 볼 수 있게 남긴다. 판정은 위 상태 검사가 맡는다.
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        if (IsSceneValid(*Scene)) { CaptureGameViewport(*this, Scene->Controller.Get(), TEXT("MeleeInput_Swing")); }
    }, 0.12f));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        if (!IsSceneValid(*Scene)) { AddError(TEXT("베이스먼트 scene unavailable")); return; }
        // 아래 대기는 메뉴 입력으로 바뀐 순간부터 센다. 맨손 클릭 단계의 시작 시각을 지운다.
        Scene->HoldStarted = 0.0;
        Scene->Controller->ToggleInventory();
    }, 0.f));
    // 메뉴 입력으로 바뀌고 앞 공격의 판정 원과 간격이 모두 끝날 때까지 기다린다.
    ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(*this, TEXT("인벤토리 Menu 입력과 공격 간격"), [Scene]
    {
        if (!IsSceneValid(*Scene)) { return true; }
        if (Scene->HoldStarted == 0.0 && GetInputMode(Scene->Controller.Get()) == ECommonInputMode::Menu) { BeginHold(*Scene); }
        return Scene->HoldStarted != 0.0 && IsHoldDone(*Scene, ADuckovCharacter::MeleeInterval + 0.1);
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        if (!IsSceneValid(*Scene)) { AddError(TEXT("베이스먼트 scene unavailable")); return; }
        TestFalse(TEXT("판정 원이 다시 숨음"), Scene->Character->GetMeleeSwing()->IsVisible());
        TestFalse(TEXT("휘두르기가 끝남"), Scene->Character->IsSwingPlaying());
        TestEqual(TEXT("휘두른 뒤 기본 동작으로 돌아감"), GetBodyAnimationPath(Scene->Character.Get()), FString(ADuckovCharacter::LocomotionAnimationPath));
        // 메뉴가 keyboard focus를 가지므로 키 대신 LMB가 부르는 함수를 직접 불러 controller의 차단을 본다.
        Scene->Controller->Attack();
        TestFalse(TEXT("메뉴가 열린 동안 공격 차단"), Scene->Character->GetMeleeSwing()->IsVisible());
        Scene->Controller->ToggleInventory();
    }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(*this, TEXT("인벤토리 닫은 뒤 Game 입력"), [Scene]
    {
        return !IsSceneValid(*Scene) || GetInputMode(Scene->Controller.Get()) == ECommonInputMode::Game;
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        if (!IsSceneValid(*Scene)) { AddError(TEXT("베이스먼트 scene unavailable")); return; }
        ADuckovCharacter* Character = Scene->Character.Get();
        TActorIterator<ATrainingDummyActor> It(Character->GetWorld());
        ATrainingDummyActor* Dummy = It ? *It : nullptr;
        if (!TestNotNull(TEXT("맵에 놓인 과녁"), Dummy)) { return; }
        // 과녁 100 cm 앞(-X)에 세우고 과녁을 바라보게 한 뒤 같은 frame에 공격한다.
        const FVector Target = Dummy->GetActorLocation();
        Character->SetActorLocation(FVector(Target.X - 100.f, Target.Y, Character->GetActorLocation().Z), false, nullptr, ETeleportType::TeleportPhysics);
        Character->SetActorRotation(FRotator::ZeroRotator);
        const float Before = Dummy->GetHealth();
        TestTrue(TEXT("과녁 앞에서 공격"), Character->TryMelee());
        TestEqual(TEXT("맵의 과녁이 피해를 받음"), Dummy->GetHealth(), Before - ADuckovCharacter::MeleeDamage);
    }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(*this, TEXT("PIE 종료"), [] { return !GEditor || !GEditor->PlayWorld; }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([Scene]
    {
        FSlateApplication::Get().SetHandleDeviceInputWhenApplicationNotActive(Scene->bOriginalInactiveInput);
    }, 0.f));
    return true;
}

// 무기 slot에 총을 장착하면 LMB가 근접 대신 사격이 되고 탄창에서 한 발씩 빠진다. 잔탄이 없으면 쏘지 않고 알린다.
// 실제 마우스 위치가 쏘는 방향을 정하므로 과녁 적중은 Duckov.Combat.Rifle이 맡고, 여기서는 controller의 분기와 탄창을 본다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRifleInput, "Duckov.Combat.RifleInput", Flags)
bool FRifleInput::RunTest(const FString& Parameters)
{
    if (!TestTrue(TEXT("베이스먼트 map load"), AutomationOpenMap(BasementMap))) { return false; }
    TSharedRef<FBasementScene> Scene = MakeShared<FBasementScene>();
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
        if (!IsSceneValid(*Scene)) { AddError(TEXT("베이스먼트 scene unavailable")); return; }
        ADuckovPlayerController* Controller = Scene->Controller.Get();
        ADuckovCharacter* Shooter = Scene->Character.Get();
        UGameSessionSubsystem* Session = Controller->GetGameInstance()->GetSubsystem<UGameSessionSubsystem>();
        UInventoryModel* Model = Session->GetInventoryModel();
        constexpr int32 RifleId = 1;
        constexpr int32 Full = ADuckovCharacter::RifleMagazineSize;
        // 시작 인벤토리의 소총은 창고 (4, 1)에 있다.
        TestTrue(TEXT("맨손이면 총 모형 없이 기본 동작"), IsHoldingRifle(Shooter, false));
        if (!TestTrue(TEXT("소총을 무기 slot에 장착"),
            Model->TryMove(TEXT("Stash"), TEXT("WeaponSlot"), RifleId, FIntPoint::ZeroValue, false) == EInventoryOperationFailure::None)) { return; }
        TestEqual(TEXT("처음 장착한 총은 가득 찬 탄창"), Session->GetMagazineRounds(RifleId, Full), Full);
        TestTrue(TEXT("장착하면 총 모형과 총을 쥔 동작"), IsHoldingRifle(Shooter, true));

        Controller->Attack();
        TestTrue(TEXT("장착하면 사격"), Shooter->GetTracer()->IsVisible());
        TestFalse(TEXT("근접 공격은 나가지 않음"), Shooter->GetMeleeSwing()->IsVisible());
        TestEqual(TEXT("한 발 소모"), Session->GetMagazineRounds(RifleId, Full), Full - 1);

        // 탄창이 비면 쏘지 않고 이유를 알린다. 이 분기는 사격 간격과 무관하게 먼저 걸린다.
        Session->SetMagazineRounds(RifleId, 0);
        Controller->Attack();
        TestEqual(TEXT("빈 탄창은 그대로"), Session->GetMagazineRounds(RifleId, Full), 0);
        UTextBlock* Message = Field<UTextBlock>(Field<UGameHudWidget>(Controller, TEXT("Hud")), TEXT("MessageText"));
        TestTrue(TEXT("탄약 없음 알림"), Message && Message->GetText().ToString() == TEXT("탄약이 없습니다"));

        // 해제하면 맨손이라 공격하지 않는다.
        if (!TestTrue(TEXT("소총 해제"),
            Model->TryMove(TEXT("WeaponSlot"), TEXT("Stash"), RifleId, FIntPoint(4, 1), false) == EInventoryOperationFailure::None)) { return; }
        Controller->Attack();
        TestFalse(TEXT("해제하면 공격하지 않음"), Shooter->GetMeleeSwing()->IsVisible());
        TestTrue(TEXT("해제하면 총 모형을 숨기고 기본 동작"), IsHoldingRifle(Shooter, false));
        // 잔탄은 무기 InstanceId에 붙어 있어 다시 장착해도 그대로다.
        TestTrue(TEXT("소총 재장착"),
            Model->TryMove(TEXT("Stash"), TEXT("WeaponSlot"), RifleId, FIntPoint::ZeroValue, false) == EInventoryOperationFailure::None);
        TestEqual(TEXT("재장착해도 빈 탄창 유지"), Session->GetMagazineRounds(RifleId, Full), 0);
    }, 0.f));
    // 총을 쥔 동작으로 바뀐 몇 frame 뒤의 화면을 남긴다. 판정은 위 상태 검사가 맡는다.
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        if (IsSceneValid(*Scene)) { CaptureGameViewport(*this, Scene->Controller.Get(), TEXT("RifleInput_Holding")); }
    }, 0.5f));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(*this, TEXT("PIE 종료"), [] { return !GEditor || !GEditor->PlayWorld; }));
    return true;
}

// R 재장전은 시간이 끝나는 순간 가방 Ammo를 한 번에 소모해 탄창을 채운다. 그동안 사격을 막고, 무기를 해제하면 취소한다.
// 끝나는 순간 가방 탄약이 없으면 아무것도 바꾸지 않는다. 탄약 HUD는 사격·재장전·인벤토리 변경 때 갱신된다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReloadFlow, "Duckov.Combat.Reload", Flags)
bool FReloadFlow::RunTest(const FString& Parameters)
{
    if (!TestTrue(TEXT("베이스먼트 map load"), AutomationOpenMap(BasementMap))) { return false; }
    TSharedRef<FBasementScene> Scene = MakeShared<FBasementScene>();
    ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(*this, TEXT("지상 캐릭터 착지"), [Scene]
    {
        UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
        auto* Controller = World ? Cast<ADuckovPlayerController>(UGameplayStatics::GetPlayerController(World, 0)) : nullptr;
        auto* Character = Controller ? Cast<ADuckovCharacter>(Controller->GetPawn()) : nullptr;
        Scene->Controller = Controller;
        Scene->Character = Character;
        return Character && Character->GetCharacterMovement()->IsMovingOnGround();
    }));
    // 시작 인벤토리: 소총(1)은 창고 (4, 1), 탄약(2) 18발은 창고 (0, 3)에 있다. 가방(4x4)에는 구급상자가 (2, 2)에 있다.
    constexpr int32 RifleId = 1;
    constexpr int32 AmmoId = 2;
    constexpr int32 Full = ADuckovCharacter::RifleMagazineSize;
    auto Session = [Scene] { return Scene->Controller->GetGameInstance()->GetSubsystem<UGameSessionSubsystem>(); };
    auto Rounds = [Session] { return Session()->GetMagazineRounds(RifleId, Full); };
    auto Ammo = [Session](FName ContainerId)
    {
        const FItemInstance* Item = Session()->GetInventoryModel()->FindItem(ContainerId, AmmoId);
        return Item ? Item->Quantity : 0;
    };
    auto WaitReload = [this, Scene](const TCHAR* Description)
    {
        ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(*this, Description, [Scene]
        {
            return IsHoldDone(*Scene, ADuckovPlayerController::ReloadDuration + 0.1);
        }));
    };
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene, Session, Rounds]
    {
        if (!IsSceneValid(*Scene)) { AddError(TEXT("베이스먼트 scene unavailable")); return; }
        ADuckovPlayerController* Controller = Scene->Controller.Get();
        UInventoryModel* Model = Session()->GetInventoryModel();
        TestEqual(TEXT("무기가 없으면 탄약 HUD 없음"), GetAmmoHud(Controller), FString());
        TestTrue(TEXT("무기가 없으면 십자선에 탄약 상태 없음"), GetAimState(Controller) == EAimAmmoState::None);
        TestTrue(TEXT("소총 장착"), Model->TryMove(TEXT("Stash"), TEXT("WeaponSlot"), RifleId, FIntPoint::ZeroValue, false) == EInventoryOperationFailure::None);
        TestEqual(TEXT("장착하면 잔탄 / 가방 탄약"), GetAmmoHud(Controller), FString(TEXT("20 / 0")));
        TestTrue(TEXT("가득 찬 탄창은 십자선 기본"), GetAimState(Controller) == EAimAmmoState::Ready);
        TestTrue(TEXT("탄약을 가방으로"), Model->TryMove(TEXT("Stash"), TEXT("Bag"), AmmoId, FIntPoint::ZeroValue, false) == EInventoryOperationFailure::None);
        TestEqual(TEXT("가방 변경이 HUD에 반영"), GetAmmoHud(Controller), FString(TEXT("20 / 18")));
        Controller->Reload();
        TestFalse(TEXT("가득 찬 탄창은 재장전하지 않음"), Controller->IsReloading());

        Session()->SetMagazineRounds(RifleId, 5);
        UGameHudWidget* Hud = Field<UGameHudWidget>(Controller, TEXT("Hud"));
        if (Hud) { Hud->SetMessage(FText::FromString(TEXT("탈출 성공"))); }
        Controller->Reload();
        TestTrue(TEXT("재장전 시작"), Controller->IsReloading());
        UTextBlock* Message = GetHudTextBlock(Controller, TEXT("MessageText"));
        TestTrue(TEXT("탄약과 무관한 문구는 남김"), Message && Message->GetText().ToString() == TEXT("탈출 성공"));
        TestEqual(TEXT("재장전 중 표시"), GetAmmoHud(Controller), FString(TEXT("재장전 중  5 / 18")));
        TestTrue(TEXT("재장전 중 십자선 링"), GetAimState(Controller) == EAimAmmoState::Reloading);
        Controller->Attack();
        TestEqual(TEXT("재장전 중에는 쏘지 않음"), Rounds(), 5);
        TestFalse(TEXT("재장전 중 tracer 없음"), Scene->Character->GetTracer()->IsVisible());
        BeginHold(*Scene);
    }, 0.f));
    WaitReload(TEXT("첫 재장전 시간"));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene, Session, Rounds, Ammo]
    {
        if (!IsSceneValid(*Scene)) { AddError(TEXT("베이스먼트 scene unavailable")); return; }
        ADuckovPlayerController* Controller = Scene->Controller.Get();
        UInventoryModel* Model = Session()->GetInventoryModel();
        TestFalse(TEXT("재장전 끝"), Controller->IsReloading());
        TestEqual(TEXT("모자란 15발을 채움"), Rounds(), Full);
        TestEqual(TEXT("가방 탄약 15발 소모"), Ammo(TEXT("Bag")), 3);
        TestEqual(TEXT("끝난 뒤 HUD"), GetAmmoHud(Controller), FString(TEXT("20 / 3")));
        TestTrue(TEXT("끝나면 십자선 링 없음"), GetAimState(Controller) == EAimAmmoState::Ready);

        // 재장전 중 무기를 해제하면 취소되고 아무것도 소모하지 않는다.
        Session()->SetMagazineRounds(RifleId, 0);
        Controller->Reload();
        TestTrue(TEXT("두 번째 재장전 시작"), Controller->IsReloading());
        TestTrue(TEXT("재장전 중 소총 해제"), Model->TryMove(TEXT("WeaponSlot"), TEXT("Stash"), RifleId, FIntPoint(4, 1), false) == EInventoryOperationFailure::None);
        TestFalse(TEXT("해제하면 재장전 취소"), Controller->IsReloading());
        TestEqual(TEXT("해제하면 탄약 HUD 감춤"), GetAmmoHud(Controller), FString());
        TestTrue(TEXT("해제하면 십자선 링 없음"), GetAimState(Controller) == EAimAmmoState::None);
        BeginHold(*Scene);
    }, 0.f));
    WaitReload(TEXT("취소된 재장전 시간"));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene, Session, Rounds, Ammo]
    {
        if (!IsSceneValid(*Scene)) { AddError(TEXT("베이스먼트 scene unavailable")); return; }
        ADuckovPlayerController* Controller = Scene->Controller.Get();
        UInventoryModel* Model = Session()->GetInventoryModel();
        TestEqual(TEXT("취소된 재장전은 소모 없음"), Ammo(TEXT("Bag")), 3);
        TestEqual(TEXT("취소된 재장전은 탄창 그대로"), Rounds(), 0);

        // 재장전 중 가방 탄약이 모두 빠지면 끝나는 순간 아무것도 바꾸지 않는다.
        TestTrue(TEXT("소총 재장착"), Model->TryMove(TEXT("Stash"), TEXT("WeaponSlot"), RifleId, FIntPoint::ZeroValue, false) == EInventoryOperationFailure::None);
        Controller->Reload();
        TestTrue(TEXT("세 번째 재장전 시작"), Controller->IsReloading());
        TestTrue(TEXT("재장전 중 탄약을 창고로"), Model->TryMove(TEXT("Bag"), TEXT("Stash"), AmmoId, FIntPoint(0, 3), false) == EInventoryOperationFailure::None);
        TestTrue(TEXT("탄약이 빠져도 재장전은 계속"), Controller->IsReloading());
        BeginHold(*Scene);
    }, 0.f));
    WaitReload(TEXT("가방 탄약이 빠진 재장전 시간"));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene, Session, Rounds, Ammo]
    {
        if (!IsSceneValid(*Scene)) { AddError(TEXT("베이스먼트 scene unavailable")); return; }
        ADuckovPlayerController* Controller = Scene->Controller.Get();
        UInventoryModel* Model = Session()->GetInventoryModel();
        TestFalse(TEXT("재장전 끝"), Controller->IsReloading());
        TestEqual(TEXT("탄창 그대로"), Rounds(), 0);
        TestEqual(TEXT("창고 탄약은 쓰지 않음"), Ammo(TEXT("Stash")), 3);
        UTextBlock* Message = GetHudTextBlock(Controller, TEXT("MessageText"));
        TestTrue(TEXT("가방 탄약 없음 알림"), Message && Message->GetText().ToString() == TEXT("가방에 탄약이 없습니다"));
        TestEqual(TEXT("탄창·가방이 비면 HUD 탄약 없음"), GetAmmoHud(Controller), FString(TEXT("탄약 없음  0 / 0")));
        TestTrue(TEXT("탄창·가방이 비면 십자선 탄약 없음"), GetAimState(Controller) == EAimAmmoState::NoAmmo);

        // 가방 탄약이 모자라면 있는 만큼만 채우고 빈 스택은 없어진다.
        TestTrue(TEXT("탄약을 다시 가방으로"), Model->TryMove(TEXT("Stash"), TEXT("Bag"), AmmoId, FIntPoint::ZeroValue, false) == EInventoryOperationFailure::None);
        TestEqual(TEXT("빈 탄창에 가방 탄약이 있으면 R 안내"), GetAmmoHud(Controller), FString(TEXT("R 재장전  0 / 3")));
        TestTrue(TEXT("빈 탄창에 가방 탄약이 있으면 십자선 R 안내"), GetAimState(Controller) == EAimAmmoState::Empty);
        Controller->Reload();
        TestTrue(TEXT("네 번째 재장전 시작"), Controller->IsReloading());
        TestTrue(TEXT("재장전 시작이 탄약 없음 문구를 지움"), Message && Message->GetText().IsEmpty());
        BeginHold(*Scene);
    }, 0.f));
    WaitReload(TEXT("모자란 재장전 시간"));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene, Session, Rounds]
    {
        if (!IsSceneValid(*Scene)) { AddError(TEXT("베이스먼트 scene unavailable")); return; }
        ADuckovPlayerController* Controller = Scene->Controller.Get();
        TestEqual(TEXT("있는 3발만 채움"), Rounds(), 3);
        TestNull(TEXT("다 쓴 탄약 스택은 없어짐"), Session()->GetInventoryModel()->FindItem(TEXT("Bag"), AmmoId));
        TestEqual(TEXT("모자란 재장전 뒤 HUD"), GetAmmoHud(Controller), FString(TEXT("탄약 부족  3 / 0")));
        TestTrue(TEXT("적은 잔탄은 십자선 주황"), GetAimState(Controller) == EAimAmmoState::Low);
        Controller->Reload();
        TestFalse(TEXT("가방 탄약이 없으면 시작하지 않음"), Controller->IsReloading());
        UTextBlock* Message = GetHudTextBlock(Controller, TEXT("MessageText"));
        TestTrue(TEXT("시작 거부 알림"), Message && Message->GetText().ToString() == TEXT("가방에 탄약이 없습니다"));
    }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(*this, TEXT("PIE 종료"), [] { return !GEditor || !GEditor->PlayWorld; }));
    return true;
}

// 이미 만든 베이스먼트·레이드 맵에 과녁을 더하는 명시 실행 경로다. 과녁이 있으면 저장하지 않아 다시 실행해도 같다.
// 새로 만드는 맵은 위 생성 경로가 같은 위치에 과녁을 둔다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAddTrainingDummies, "TrainingDummyAssets.AddToMaps", Flags)
bool FAddTrainingDummies::RunTest(const FString& Parameters)
{
    const TPair<const TCHAR*, FVector> Targets[] = { { BasementMap, BasementDummyLocation }, { RaidMap, RaidDummyLocation } };
    for (const TPair<const TCHAR*, FVector>& Target : Targets)
    {
        UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(Target.Key);
        if (!World) { AddError(FString::Printf(TEXT("map load 실패: %s"), Target.Key)); return false; }
        if (CountTrainingDummies(World) > 0)
        {
            AddInfo(FString::Printf(TEXT("이미 과녁이 있어 저장하지 않습니다: %s"), Target.Key));
            continue;
        }
        SpawnTrainingDummy(World, Target.Value);
        if (!UEditorLoadingAndSavingUtils::SaveMap(World, Target.Key)) { AddError(FString::Printf(TEXT("map 저장 실패: %s"), Target.Key)); return false; }
    }
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
