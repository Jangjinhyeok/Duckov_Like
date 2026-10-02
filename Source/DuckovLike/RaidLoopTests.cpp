#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "InventoryDemoPlayerController.h"
#include "InventoryScreenWidget.h"
#include "InventoryGridWidget.h"
#include "InventoryModel.h"
#include "ContainerViewModel.h"
#include "ItemViewModel.h"
#include "RaidStatusWidget.h"
#include "WorldLootActor.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Editor.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "UObject/UnrealType.h"

namespace RaidLoopTests
{
namespace
{
template <typename T>
T* Field(UObject* Owner, FName Name)
{
    const FObjectProperty* Property = Owner ? FindFProperty<FObjectProperty>(Owner->GetClass(), Name) : nullptr;
    return Property ? Cast<T>(Property->GetObjectPropertyValue_InContainer(Owner)) : nullptr;
}

TArray<AWorldLootActor*> ActiveLoot(UWorld* World)
{
    TArray<AWorldLootActor*> Result;
    for (TActorIterator<AWorldLootActor> It(World); It; ++It)
    {
        if (!It->IsHidden() && !It->IsActorBeingDestroyed()) { Result.Add(*It); }
    }
    return Result;
}

bool SameState(UInventoryModel* Model, const FInventorySaveRecord& Before)
{
    FInventorySaveRecord After;
    return Model->Save(After) == EInventorySaveFailure::None
        && FInventorySaveRecord::StaticStruct()->CompareScriptStruct(&Before, &After, 0);
}

class FWaitForRaidController final : public IAutomationLatentCommand
{
public:
    explicit FWaitForRaidController(FAutomationTestBase& InTest) : Test(InTest) {}
    virtual bool Update() override
    {
        if (GEditor && GEditor->PlayWorld &&
            Cast<AInventoryDemoPlayerController>(UGameplayStatics::GetPlayerController(GEditor->PlayWorld, 0))) { return true; }
        if (Started == 0.0) { Started = FPlatformTime::Seconds(); }
        if (FPlatformTime::Seconds() - Started < 15.0) { return false; }
        Test.AddError(TEXT("Raid PIE Controller 시작 시간 초과"));
        return true;
    }
private:
    FAutomationTestBase& Test;
    double Started = 0.0;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRaidLoopPIE, "Duckov.Gameplay.RaidLoop",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRaidLoopPIE::RunTest(const FString& Parameters)
{
    if (!TestTrue(TEXT("기존 데모 맵 PIE 시작"), AutomationOpenMap(TEXT("/Game/Maps/L_InventoryDemo")))) { return false; }
    ADD_LATENT_AUTOMATION_COMMAND(FWaitForRaidController(*this));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this]
    {
        UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
        AInventoryDemoPlayerController* Controller = World
            ? Cast<AInventoryDemoPlayerController>(UGameplayStatics::GetPlayerController(World, 0)) : nullptr;
        UInventoryModel* Model = Field<UInventoryModel>(Controller, TEXT("Model"));
        APawn* ControlledPawn = Controller ? Controller->GetPawnOrSpectator() : nullptr;
        if (!TestNotNull(TEXT("실제 Controller"), Controller) || !TestNotNull(TEXT("실제 Model"), Model)
            || !TestNotNull(TEXT("실제 이동 Pawn"), ControlledPawn)) { return; }
        TestEqual(TEXT("기존 데모는 기본 동작 유지"), Controller->GetRaidPhase(), ERaidDemoPhase::Disabled);
        TestEqual(TEXT("시작 전 진입 거부"), Controller->TryEnterRaid(), ERaidDemoFailure::InvalidPhase);
        TestEqual(TEXT("시작 전 탈출 거부"), Controller->TryExtractRaid(), ERaidDemoFailure::InvalidPhase);
        FInventorySaveRecord Initial;
        Model->Save(Initial);
        Controller->ConsoleCommand(TEXT("StartRaidDemo"), false);
        TestEqual(TEXT("console command로 준비 시작"), Controller->GetRaidPhase(), ERaidDemoPhase::Preparation);
        TestTrue(TEXT("데모 시작은 아이템 보존"), SameState(Model, Initial));
        TestEqual(TEXT("중복 시작 거부"), Controller->TryStartRaidDemo(), ERaidDemoFailure::InvalidPhase);
        TestEqual(TEXT("준비 중 loot 비표시"), ActiveLoot(World).Num(), 0);
        AWorldLootActor* Template = nullptr;
        for (TActorIterator<AWorldLootActor> It(World); It; ++It) { Template = *It; break; }
        if (!TestNotNull(TEXT("비표시 원본 loot"), Template)) { return; }
        TestEqual(TEXT("준비 중 원본 획득 차단"), Controller->TryPickupLoot(Template), EInventoryOperationFailure::InvalidContainer);
        URaidStatusWidget* Status = Field<URaidStatusWidget>(Controller, TEXT("RaidStatus"));
        UTextBlock* StatusText = Status ? Cast<UTextBlock>(Status->GetWidgetFromName(TEXT("RaidStatusText"))) : nullptr;
        AStaticMeshActor* Point = Field<AStaticMeshActor>(Controller, TEXT("ExtractionPoint"));
        if (!TestNotNull(TEXT("native 상태 표시"), StatusText) || !TestNotNull(TEXT("탈출 지점"), Point)) { return; }
        TestTrue(TEXT("준비 안내 표시"), StatusText->GetText().ToString().Contains(TEXT("준비")));
        Controller->ToggleInventory();
        UInventoryScreenWidget* Screen = Field<UInventoryScreenWidget>(Controller, TEXT("Screen"));
        UContainerViewModel* LeftVM = Field<UContainerViewModel>(Screen, TEXT("LeftVM"));
        if (!TestNotNull(TEXT("저장된 실제 WBP"), Screen) || !TestNotNull(TEXT("Stash VM"), LeftVM)) { return; }
        TestTrue(TEXT("준비 중 Stash 접근"), LeftVM->IsAvailable());
        Controller->EnterRaid();
        TestEqual(TEXT("Raid 진입"), Controller->GetRaidPhase(), ERaidDemoPhase::InRaid);
        TestFalse(TEXT("진입은 기존 화면 정리"), Screen->IsActivated());
        TestEqual(TEXT("원본으로부터 새 loot 4개"), ActiveLoot(World).Num(), 4);
        TestFalse(TEXT("Raid 중 탈출 marker 표시"), Point->IsHidden());
        TestEqual(TEXT("중복 진입 거부"), Controller->TryEnterRaid(), ERaidDemoFailure::InvalidPhase);
        TestEqual(TEXT("비표시 템플릿 직접 획득 차단"), Controller->TryPickupLoot(Template), EInventoryOperationFailure::InvalidContainer);
        Controller->ToggleInventory();
        TestFalse(TEXT("Raid에서 Stash VM 구독 해제"), LeftVM->IsAvailable());
        UButton* SortLeft = Cast<UButton>(Screen->GetWidgetFromName(TEXT("SortLeftButton")));
        UInventoryGridWidget* LeftGrid = Cast<UInventoryGridWidget>(Screen->GetWidgetFromName(TEXT("LeftGrid")));
        UContainerViewModel* RightVM = Field<UContainerViewModel>(Screen, TEXT("RightVM"));
        if (!TestNotNull(TEXT("Stash 정렬 button"), SortLeft) || !TestNotNull(TEXT("Stash grid"), LeftGrid)
            || !TestNotNull(TEXT("Bag VM"), RightVM)) { return; }
        TestFalse(TEXT("Raid Stash grid 비활성"), LeftGrid->GetIsEnabled());
        TestFalse(TEXT("Raid Stash 정렬 비활성"), SortLeft->GetIsEnabled());
        UItemViewModel* BagItem = RightVM->GetItems().IsEmpty() ? nullptr : RightVM->GetItems()[0].Get();
        if (!TestNotNull(TEXT("Bag item"), BagItem)) { return; }
        TestTrue(TEXT("Raid에서도 Bag 분할 입력 가능"), Screen->OpenSplitDialog(TEXT("Bag"), BagItem));
        Screen->NativeOnKeyDown(Screen->GetCachedGeometry(), FKeyEvent(EKeys::Escape, FModifierKeysState(), 0, false, 0, 0));
        TestFalse(TEXT("분할 취소 후에도 Stash grid 차단"), LeftGrid->GetIsEnabled());
        TestFalse(TEXT("분할 취소 후에도 Stash 정렬 차단"), SortLeft->GetIsEnabled());
        FInventorySaveRecord BeforeSort;
        Model->Save(BeforeSort);
        SortLeft->OnClicked.Broadcast();
        TestTrue(TEXT("비활성 정렬 delegate도 Stash 불변"), SameState(Model, BeforeSort));
        TArray<AWorldLootActor*> Loot = ActiveLoot(World);
        if (!TestEqual(TEXT("획득할 loot 수"), Loot.Num(), 4)) { return; }
        const int32 AcquiredId = FItemInstanceIdAllocator::GetNextInstanceId();
        const int32 InitialStashCount = Model->FindContainer(TEXT("Stash"))->Items.Num();
        TestEqual(TEXT("Raid loot Bag 획득"), Controller->TryPickupLoot(Loot[0]), EInventoryOperationFailure::None);
        TestNotNull(TEXT("획득 ID는 Bag에 존재"), Model->FindItem(TEXT("Bag"), AcquiredId));
        TestNull(TEXT("탈출 전 Stash에는 없음"), Model->FindItem(TEXT("Stash"), AcquiredId));
        TestEqual(TEXT("탈출 전 Stash 개수 유지"), Model->FindContainer(TEXT("Stash"))->Items.Num(), InitialStashCount);
        FInventorySaveRecord BeforeExtraction;
        Model->Save(BeforeExtraction);
        ControlledPawn->SetActorLocation(Point->GetActorLocation() + FVector(181.f, 0.f, 0.f));
        TestEqual(TEXT("탈출 범위 바로 밖 거부"), Controller->TryExtractRaid(), ERaidDemoFailure::OutsideExtraction);
        TestTrue(TEXT("범위 밖은 inventory 전체 보존"), SameState(Model, BeforeExtraction));
        int32 Notifications = 0;
        const FDelegateHandle Handle = Model->OnChanged().AddLambda([&](const FInventoryChangeSet&)
        {
            ++Notifications;
            TestEqual(TEXT("탈출 알림 중 재진입 거부"), Controller->TryExtractRaid(), ERaidDemoFailure::OperationInProgress);
            TestEqual(TEXT("알림 중 Raid 진입 거부"), Controller->TryEnterRaid(), ERaidDemoFailure::OperationInProgress);
            TestEqual(TEXT("알림 중 추가 loot 차단"), Controller->TryPickupLoot(Loot[1]), EInventoryOperationFailure::OperationInProgress);
            TestFalse(TEXT("commit 알림 전 남은 loot 제거 금지"), Loot[1]->IsActorBeingDestroyed());
        });
        ControlledPawn->SetActorLocation(Point->GetActorLocation() + FVector(180.f, 0.f, 0.f));
        TestEqual(TEXT("경계에서 탈출 성공"), Controller->TryExtractRaid(), ERaidDemoFailure::None);
        Model->OnChanged().Remove(Handle);
        TestEqual(TEXT("탈출은 단일 ChangeSet"), Notifications, 1);
        TestEqual(TEXT("탈출 후 준비 복귀"), Controller->GetRaidPhase(), ERaidDemoPhase::Preparation);
        TestEqual(TEXT("Raid 1회 완료"), Controller->GetCompletedRaidCount(), 1);
        TestTrue(TEXT("성공 후 Bag 비움"), Model->FindContainer(TEXT("Bag"))->Items.IsEmpty());
        TestNotNull(TEXT("획득 ID를 Stash로 보존"), Model->FindItem(TEXT("Stash"), AcquiredId));
        TestEqual(TEXT("기존 Bag과 획득 item 모두 반영"), Model->FindContainer(TEXT("Stash"))->Items.Num(), InitialStashCount + 2);
        TestTrue(TEXT("성공 뒤 남은 loot 제거"), Loot[1]->IsActorBeingDestroyed());
        TestTrue(TEXT("탈출 성공 안내"), StatusText->GetText().ToString().Contains(TEXT("탈출 성공")));
        TestEqual(TEXT("중복 탈출 거부"), Controller->TryExtractRaid(), ERaidDemoFailure::InvalidPhase);
        Controller->ToggleInventory();
        TestTrue(TEXT("준비 복귀 후 Stash VM 연결"), LeftVM->IsAvailable());
        TestTrue(TEXT("준비 복귀 후 Stash 정렬 복구"), SortLeft->GetIsEnabled());
        const int32 StashCountAfterFirstRaid = Model->FindContainer(TEXT("Stash"))->Items.Num();
        TestEqual(TEXT("다음 Raid 재진입"), Controller->TryEnterRaid(), ERaidDemoFailure::None);
        TestEqual(TEXT("다음 Raid loot 재생성"), ActiveLoot(World).Num(), 4);
        TestEqual(TEXT("빈 Bag 탈출"), Controller->TryExtractRaid(), ERaidDemoFailure::None);
        TestEqual(TEXT("빈 Bag은 Stash 복제하지 않음"), Model->FindContainer(TEXT("Stash"))->Items.Num(), StashCountAfterFirstRaid);

        // 첫 항목만 들어가고 둘째는 실패하는 실제 세션을 만들어 부분 반영을 검증한다.
        FInventorySaveRecord Small;
        Small.NextInstanceId = FMath::Max(FItemInstanceIdAllocator::GetNextInstanceId(), 203);
        Small.Containers.SetNum(2);
        Small.Containers[0].ContainerId = TEXT("Stash");
        Small.Containers[0].GridSize = FIntPoint(2, 2);
        Small.Containers[1].ContainerId = TEXT("Bag");
        Small.Containers[1].GridSize = FIntPoint(3, 2);
        auto Add = [&](int32 Container, int32 Id, FName Row, FIntPoint Anchor)
        {
            FInventoryItemSaveRecord& Item = Small.Containers[Container].Items.AddDefaulted_GetRef();
            Item.InstanceId = Id;
            Item.DefinitionTable = Initial.Containers[0].Items[0].DefinitionTable;
            Item.DefinitionRowName = Row;
            Item.Quantity = 1;
            Item.AnchorCell = Anchor;
        };
        Add(0, 202, TEXT("Ammo"), FIntPoint(0, 0));
        Add(1, 200, TEXT("Ammo"), FIntPoint(0, 0));
        Add(1, 201, TEXT("Medkit"), FIntPoint(1, 0));
        if (!TestEqual(TEXT("공간 부족 fixture 로드"), Model->Load(Small), EInventorySaveFailure::None)) { return; }
        Controller->TryEnterRaid();
        Controller->ToggleInventory();
        FInventorySaveRecord Full;
        Model->Save(Full);
        int32 FailedNotifications = 0;
        const FDelegateHandle FailedHandle = Model->OnChanged().AddLambda([&](const FInventoryChangeSet&) { ++FailedNotifications; });
        Controller->ExtractRaid();
        Model->OnChanged().Remove(FailedHandle);
        TestTrue(TEXT("부분 공간 부족은 items·allocator 전체 보존"), SameState(Model, Full));
        TestEqual(TEXT("실패는 ChangeSet 없음"), FailedNotifications, 0);
        TestEqual(TEXT("실패는 Raid 유지"), Controller->GetRaidPhase(), ERaidDemoPhase::InRaid);
        TestEqual(TEXT("실패는 완료 횟수 유지"), Controller->GetCompletedRaidCount(), 2);
        TestEqual(TEXT("실패는 남은 loot 유지"), ActiveLoot(World).Num(), 4);
        TestTrue(TEXT("실패는 열린 Bag 화면 유지"), Screen->IsActivated());
        TestTrue(TEXT("공간 부족 안내 표시"), StatusText->GetText().ToString().Contains(TEXT("공간이 부족")));
        TestEqual(TEXT("공간 확보"), Model->TryResize(TEXT("Stash"), FIntPoint(3, 3)), EInventoryOperationFailure::None);
        TestEqual(TEXT("실패 후 재시도 성공"), Controller->TryExtractRaid(), ERaidDemoFailure::None);
        TestEqual(TEXT("재시도는 한 번 완료"), Controller->GetCompletedRaidCount(), 3);
        TestTrue(TEXT("재시도 후 Bag 비움"), Model->FindContainer(TEXT("Bag"))->Items.IsEmpty());
        TestNotNull(TEXT("재시도 후 첫 item ID 보존"), Model->FindItem(TEXT("Stash"), 200));
        TestNotNull(TEXT("재시도 후 둘째 item ID 보존"), Model->FindItem(TEXT("Stash"), 201));
    }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}
}
#endif
