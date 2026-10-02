#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "WorldLootActor.h"
#include "InventoryDemoPlayerController.h"
#include "InventoryGridWidget.h"
#include "InventoryItemWidget.h"
#include "InventoryModel.h"
#include "InventoryScreenWidget.h"
#include "ContainerViewModel.h"
#include "ItemViewModel.h"
#include "Components/CanvasPanel.h"
#include "Editor.h"
#include "Engine/DataTable.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "UObject/UnrealType.h"

namespace WorldLootTests
{
namespace
{
constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
constexpr const TCHAR* DemoMap = TEXT("/Game/Maps/L_InventoryDemo");
constexpr const TCHAR* DefinitionPath = TEXT("/Game/Inventory/DT_ItemDefinitions.DT_ItemDefinitions");
constexpr int32 DemoLootCount = 4;

template <typename T>
T* ReflectedObject(UObject* Owner, FName PropertyName)
{
    const FObjectProperty* Property = Owner ? FindFProperty<FObjectProperty>(Owner->GetClass(), PropertyName) : nullptr;
    return Property ? Cast<T>(Property->GetObjectPropertyValue_InContainer(Owner)) : nullptr;
}

FName DemoLootName(int32 Index)
{
    return FName(*FString::Printf(TEXT("M3_MedkitLoot_%d"), Index));
}

UCanvasPanel* BagCanvas(UInventoryScreenWidget* Screen)
{
    UInventoryGridWidget* Grid = Screen
        ? Cast<UInventoryGridWidget>(Screen->GetWidgetFromName(TEXT("RightGrid"))) : nullptr;
    return Grid ? Cast<UCanvasPanel>(Grid->GetWidgetFromName(TEXT("GridCanvas"))) : nullptr;
}

bool HasItemWidget(UCanvasPanel* Canvas, int32 InstanceId)
{
    for (int32 Index = 0; Canvas && Index < Canvas->GetChildrenCount(); ++Index)
    {
        UInventoryItemWidget* Widget = Cast<UInventoryItemWidget>(Canvas->GetChildAt(Index));
        UItemViewModel* Item = ReflectedObject<UItemViewModel>(Widget, TEXT("Item"));
        if (Item && Item->GetInstanceId() == InstanceId) { return true; }
    }
    return false;
}

class FWaitForController final : public IAutomationLatentCommand
{
public:
    explicit FWaitForController(FAutomationTestBase& InTest) : Test(InTest) {}
    virtual bool Update() override
    {
        if (GEditor && GEditor->PlayWorld &&
            Cast<AInventoryDemoPlayerController>(UGameplayStatics::GetPlayerController(GEditor->PlayWorld, 0)))
        {
            return true;
        }
        if (Started == 0.0) { Started = FPlatformTime::Seconds(); }
        if (FPlatformTime::Seconds() - Started < 15.0) { return false; }
        Test.AddError(TEXT("PIE Controller 시작 시간 초과"));
        return true;
    }
private:
    FAutomationTestBase& Test;
    double Started = 0.0;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldLootPickup, "Duckov.Gameplay.WorldLootPickup", Flags)
bool FWorldLootPickup::RunTest(const FString& Parameters)
{
    if (!TestTrue(TEXT("데모 맵 PIE 시작"), AutomationOpenMap(DemoMap))) { return false; }
    ADD_LATENT_AUTOMATION_COMMAND(FWaitForController(*this));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this]
    {
        UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
        AInventoryDemoPlayerController* Controller = World
            ? Cast<AInventoryDemoPlayerController>(UGameplayStatics::GetPlayerController(World, 0)) : nullptr;
        UInventoryModel* Model = ReflectedObject<UInventoryModel>(Controller, TEXT("Model"));
        UDataTable* Table = LoadObject<UDataTable>(nullptr, DefinitionPath);
        if (!TestNotNull(TEXT("PIE Controller"), Controller) || !TestNotNull(TEXT("Controller Model"), Model)
            || !TestNotNull(TEXT("실제 DefinitionTable"), Table)) { return; }

        // 테스트 Actor만 남겨 nearest 선택을 결정적으로 만든다.
        for (TActorIterator<AWorldLootActor> It(World); It; ++It) { It->Destroy(); }
        Controller->ToggleInventory();
        UInventoryScreenWidget* Screen = ReflectedObject<UInventoryScreenWidget>(Controller, TEXT("Screen"));
        UContainerViewModel* RightVM = ReflectedObject<UContainerViewModel>(Screen, TEXT("RightVM"));
        UInventoryGridWidget* RightGrid = Screen
            ? Cast<UInventoryGridWidget>(Screen->GetWidgetFromName(TEXT("RightGrid"))) : nullptr;
        UCanvasPanel* Canvas = BagCanvas(Screen);
        if (!TestNotNull(TEXT("활성 WBP 화면"), Screen) || !TestTrue(TEXT("화면 활성"), Screen->IsActivated())
            || !TestNotNull(TEXT("Bag ViewModel"), RightVM) || !TestNotNull(TEXT("Bag Grid"), RightGrid)
            || !TestNotNull(TEXT("Bag Canvas"), Canvas)) { return; }

        const int32 BeforeCount = Model->FindContainer(TEXT("Bag"))->Items.Num();
        const int32 BeforeId = FItemInstanceIdAllocator::GetNextInstanceId();
        AWorldLootActor* Loot = World->SpawnActor<AWorldLootActor>();
        if (!TestNotNull(TEXT("loot Actor 생성"), Loot)) { return; }
        Loot->DefinitionTable = TSoftObjectPtr<UDataTable>(FSoftObjectPath(Table));
        Loot->DefinitionRowName = TEXT("Medkit");
        Loot->Quantity = 1;
        int32 Calls = 0;
        FInventoryChangeSet Last;
        EInventoryOperationFailure Reentry = EInventoryOperationFailure::None;
        bool bAliveDuringCommit = false;
        const FDelegateHandle Handle = Model->OnChanged().AddLambda([&](const FInventoryChangeSet& Change)
        {
            ++Calls;
            Last = Change;
            bAliveDuringCommit = IsValid(Loot) && !Loot->IsActorBeingDestroyed();
            Reentry = Controller->TryPickupLoot(Loot);
        });
        TestEqual(TEXT("상호작용 성공"), Controller->TryPickupLoot(Loot), EInventoryOperationFailure::None);
        Model->OnChanged().Remove(Handle);
        const FInventoryContainer* Bag = Model->FindContainer(TEXT("Bag"));
        TestEqual(TEXT("Bag에 정확히 한 항목 추가"), Bag->Items.Num(), BeforeCount + 1);
        TestEqual(TEXT("기존 allocator ID"), Bag->Items.Last().InstanceId, BeforeId);
        TestEqual(TEXT("ID 카운터 한 번 증가"), FItemInstanceIdAllocator::GetNextInstanceId(), BeforeId + 1);
        TestEqual(TEXT("Added 알림 한 번"), Calls, 1);
        TestTrue(TEXT("commit 알림 당시 loot 생존"), bAliveDuringCommit);
        TestEqual(TEXT("callback 중 중복 pickup 거부"), Reentry, EInventoryOperationFailure::OperationInProgress);
        TestTrue(TEXT("성공 뒤 loot 제거 요청"), Loot->IsActorBeingDestroyed());
        TestEqual(TEXT("제거 뒤 중복 pickup 거부"), Controller->TryPickupLoot(Loot), EInventoryOperationFailure::ItemNotFound);
        if (TestEqual(TEXT("Bag ChangeSet 하나"), Last.Containers.Num(), 1))
        {
            TestEqual(TEXT("Bag 변경"), Last.Containers[0].ContainerId, FName(TEXT("Bag")));
            TestTrue(TEXT("Added ID 정확히 하나"), Last.Containers[0].Added == TArray<int32>{BeforeId});
        }
        TestTrue(TEXT("화면 재생성 없음"), ReflectedObject<UInventoryScreenWidget>(Controller, TEXT("Screen")) == Screen
            && Screen->IsActivated());
        TestTrue(TEXT("Grid/ViewModel 재생성 없음"),
            Screen->GetWidgetFromName(TEXT("RightGrid")) == RightGrid
            && ReflectedObject<UContainerViewModel>(Screen, TEXT("RightVM")) == RightVM);
        TestTrue(TEXT("열린 화면의 새 Item Widget"), HasItemWidget(Canvas, BeforeId));

        // 남은 세 칸을 Model command로 채워 no-space 실패를 검증한다.
        for (int32 Index = 0; Index < 2; ++Index)
        {
            TestEqual(TEXT("Bag 채우기"), Model->TryAdd(TEXT("Bag"),
                TSoftObjectPtr<UDataTable>(FSoftObjectPath(Table)), TEXT("Medkit"), 1),
                EInventoryOperationFailure::None);
        }
        const FInventoryContainer Full = *Model->FindContainer(TEXT("Bag"));
        const int32 FullCounter = FItemInstanceIdAllocator::GetNextInstanceId();
        const int32 FullChildren = Canvas->GetChildrenCount();
        AWorldLootActor* Blocked = World->SpawnActor<AWorldLootActor>();
        if (!TestNotNull(TEXT("공간 부족 loot 생성"), Blocked)) { return; }
        Blocked->DefinitionTable = TSoftObjectPtr<UDataTable>(FSoftObjectPath(Table));
        Blocked->DefinitionRowName = TEXT("Medkit");
        Blocked->Quantity = 1;
        Calls = 0;
        const FDelegateHandle FailureHandle = Model->OnChanged().AddLambda([&](const FInventoryChangeSet&) { ++Calls; });
        TestEqual(TEXT("가득 찬 Bag pickup 실패"), Controller->TryPickupLoot(Blocked), EInventoryOperationFailure::NoSpace);
        TestTrue(TEXT("실패 loot 유지"), IsValid(Blocked) && !Blocked->IsActorBeingDestroyed());
        TestEqual(TEXT("loot payload 유지"), Blocked->DefinitionRowName, FName(TEXT("Medkit")));
        TestEqual(TEXT("Bag 항목 유지"), Model->FindContainer(TEXT("Bag"))->Items.Num(), Full.Items.Num());
        TestTrue(TEXT("Bag 점유 유지"), Model->FindContainer(TEXT("Bag"))->OccupancyCache == Full.OccupancyCache);
        TestEqual(TEXT("ID 카운터 유지"), FItemInstanceIdAllocator::GetNextInstanceId(), FullCounter);
        TestEqual(TEXT("실패 ChangeSet 없음"), Calls, 0);
        TestEqual(TEXT("실패 후 widget 수 유지"), Canvas->GetChildrenCount(), FullChildren);
        AWorldLootActor* Invalid = World->SpawnActor<AWorldLootActor>();
        if (!TestNotNull(TEXT("잘못된 payload loot 생성"), Invalid))
        {
            Model->OnChanged().Remove(FailureHandle);
            return;
        }
        Invalid->DefinitionTable = Blocked->DefinitionTable;
        Invalid->DefinitionRowName = TEXT("Missing");
        Invalid->Quantity = 1;
        TestEqual(TEXT("잘못된 payload 거부"), Controller->TryPickupLoot(Invalid), EInventoryOperationFailure::InvalidDefinition);
        TestTrue(TEXT("잘못된 payload의 loot 유지"), IsValid(Invalid) && !Invalid->IsActorBeingDestroyed());
        TestEqual(TEXT("잘못된 payload 알림 없음"), Calls, 0);
        Model->OnChanged().Remove(FailureHandle);

        APawn* Pawn = Controller->GetPawnOrSpectator();
        if (TestNotNull(TEXT("상호작용 Pawn"), Pawn))
        {
            Blocked->Destroy();
            Invalid->Destroy();
            TestEqual(TEXT("Bag 확장"), Model->TryResize(TEXT("Bag"), FIntPoint(6, 4)), EInventoryOperationFailure::None);
            AWorldLootActor* Far = World->SpawnActor<AWorldLootActor>();
            if (!TestNotNull(TEXT("범위 밖 loot 생성"), Far)) { return; }
            Far->DefinitionTable = TSoftObjectPtr<UDataTable>(FSoftObjectPath(Table));
            Far->DefinitionRowName = TEXT("Medkit");
            Far->Quantity = 1;
            Far->SetActorLocation(Pawn->GetActorLocation() + FVector(300.f, 0.f, 0.f));
            Controller->PickupNearestLoot();
            TestTrue(TEXT("250 밖 loot는 선택되지 않음"), IsValid(Far) && !Far->IsActorBeingDestroyed());
            TestEqual(TEXT("범위 밖 상호작용 후 Bag 유지"), Model->FindContainer(TEXT("Bag"))->Items.Num(), Full.Items.Num());
            AWorldLootActor* Near = World->SpawnActor<AWorldLootActor>();
            if (!TestNotNull(TEXT("범위 안 loot 생성"), Near)) { return; }
            Near->DefinitionTable = TSoftObjectPtr<UDataTable>(FSoftObjectPath(Table));
            Near->DefinitionRowName = TEXT("Medkit");
            Near->Quantity = 1;
            Near->SetActorLocation(Pawn->GetActorLocation() + FVector(100.f, 0.f, 0.f));
            const int32 BeforeNearest = Model->FindContainer(TEXT("Bag"))->Items.Num();
            Controller->PickupNearestLoot();
            TestTrue(TEXT("250 안의 가장 가까운 loot 제거"), Near->IsActorBeingDestroyed());
            TestTrue(TEXT("멀리 있는 loot 유지"), IsValid(Far) && !Far->IsActorBeingDestroyed());
            TestEqual(TEXT("nearest가 Bag 한 항목 추가"), Model->FindContainer(TEXT("Bag"))->Items.Num(), BeforeNearest + 1);
            Far->Destroy();
        }
        Controller->ToggleInventory();
    }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}

// 명시적으로 실행하는 데모 맵 유지보수 작업이다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaceDemoWorldLoot, "InventoryLootAssets.PlaceDemo", Flags)
bool FPlaceDemoWorldLoot::RunTest(const FString& Parameters)
{
    const FString Filename = FPackageName::LongPackageNameToFilename(DemoMap, FPackageName::GetMapPackageExtension());
    UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(Filename);
    UDataTable* Table = LoadObject<UDataTable>(nullptr, DefinitionPath);
    if (!TestNotNull(TEXT("편집용 데모 맵"), World) || !TestNotNull(TEXT("DefinitionTable"), Table)) { return false; }
    int32 Existing = 0;
    for (AActor* Actor : World->PersistentLevel->Actors)
    {
        if (AWorldLootActor* Loot = Cast<AWorldLootActor>(Actor))
        {
            ++Existing;
            if (!TestTrue(TEXT("기존 loot 이름 확인"), Loot->GetFName().ToString().StartsWith(TEXT("M3_MedkitLoot_"))))
            {
                return false;
            }
        }
    }
    if (Existing == DemoLootCount) { return true; }
    if (!TestEqual(TEXT("부분 배치 거부"), Existing, 0)) { return false; }
    for (int32 Index = 0; Index < DemoLootCount; ++Index)
    {
        FActorSpawnParameters Spawn;
        Spawn.Name = DemoLootName(Index);
        AWorldLootActor* Loot = World->SpawnActor<AWorldLootActor>(
            FVector(130.f, -60.f + 40.f * Index, 0.f), FRotator::ZeroRotator, Spawn);
        if (!TestNotNull(TEXT("맵 loot 배치"), Loot)) { return false; }
        Loot->DefinitionTable = TSoftObjectPtr<UDataTable>(FSoftObjectPath(Table));
        Loot->DefinitionRowName = TEXT("Medkit");
        Loot->Quantity = 1;
    }
    TestTrue(TEXT("기존 맵 저장"), UEditorLoadingAndSavingUtils::SaveMap(World, DemoMap));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVerifyWorldLootMap, "Duckov.Gameplay.WorldLootMap", Flags)
bool FVerifyWorldLootMap::RunTest(const FString& Parameters)
{
    UWorld* World = LoadObject<UWorld>(nullptr, TEXT("/Game/Maps/L_InventoryDemo.L_InventoryDemo"));
    UDataTable* Table = LoadObject<UDataTable>(nullptr, DefinitionPath);
    if (!TestNotNull(TEXT("데모 맵"), World) || !TestNotNull(TEXT("DefinitionTable"), Table)) { return false; }
    int32 Found = 0;
    for (AActor* Actor : World->PersistentLevel->Actors)
    {
        AWorldLootActor* Loot = Cast<AWorldLootActor>(Actor);
        if (!Loot) { continue; }
        const FString Name = Loot->GetFName().ToString();
        if (!TestTrue(TEXT("예상 loot 이름"), Name.StartsWith(TEXT("M3_MedkitLoot_")))) { return false; }
        TestTrue(TEXT("정의 테이블"), Loot->DefinitionTable.LoadSynchronous() == Table);
        TestEqual(TEXT("정의 행"), Loot->DefinitionRowName, FName(TEXT("Medkit")));
        TestEqual(TEXT("수량"), Loot->Quantity, 1);
        const int32 Index = FCString::Atoi(*Name.RightChop(14));
        TestTrue(TEXT("loot 위치"), Index >= 0 && Index < DemoLootCount
            && Loot->GetActorLocation().Equals(FVector(130.f, -60.f + 40.f * Index, 0.f)));
        ++Found;
    }
    TestEqual(TEXT("배치된 loot 4개"), Found, DemoLootCount);
    return !HasAnyErrors();
}
}
#endif
