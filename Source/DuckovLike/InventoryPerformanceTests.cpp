#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "InventoryPerformanceProbe.h"
#include "InventoryDemoPlayerController.h"
#include "InventoryGridWidget.h"
#include "InventoryItemWidget.h"
#include "InventoryModel.h"
#include "InventoryScreenWidget.h"
#include "ContainerViewModel.h"
#include "ItemViewModel.h"
#include "ItemDefinitionRow.h"
#include "Components/CanvasPanel.h"
#include "Editor.h"
#include "Engine/DataTable.h"
#include "Framework/Application/SlateApplication.h"
#include "Kismet/GameplayStatics.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

FInventoryPerformanceProbe* GInventoryPerformanceProbe = nullptr;

namespace
{
constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
constexpr int32 ItemCount = 100;
constexpr int32 SampleCount = 20;
constexpr int32 WarmupCount = 3;

template <typename T>
T* ReflectedObject(UObject* Owner, FName Name)
{
    const FObjectProperty* Property = Owner ? FindFProperty<FObjectProperty>(Owner->GetClass(), Name) : nullptr;
    return Property ? Cast<T>(Property->GetObjectPropertyValue_InContainer(Owner)) : nullptr;
}

struct FInventoryPerformanceScene
{
    TStrongObjectPtr<UDataTable> Table{NewObject<UDataTable>()};
    FInventorySaveRecord Record;
    FInventoryPerformanceProbe Probe;
    TWeakObjectPtr<AInventoryDemoPlayerController> Controller;
    TWeakObjectPtr<UInventoryModel> Model;
    TWeakObjectPtr<UInventoryScreenWidget> Screen;
    TWeakObjectPtr<UInventoryGridWidget> Grid;
    TWeakObjectPtr<UContainerViewModel> Container;
    int32 OriginalCounter = FItemInstanceIdAllocator::GetNextInstanceId();
    TArray<double> OpenMs;
    double ColdOpenMs = 0.0;
    TArray<double> MoveMs;
    TArray<double> NonTailMoveMs;
    TArray<double> SortMs;
    int32 ItemsNotifications = 0;
    FDelegateHandle ItemsHandle;
    uint64 IdleStartFrame = 0;
    double IdleStartTime = 0.0;
    int32 IdleGridRefresh = 0;
    int32 IdleItemRefresh = 0;

    ~FInventoryPerformanceScene()
    {
        GInventoryPerformanceProbe = nullptr;
        if (UContainerViewModel* VM = Container.Get())
        {
            VM->RemoveFieldValueChangedDelegate(UContainerViewModel::FFieldNotificationClassDescriptor::Items, ItemsHandle);
        }
        FItemInstanceIdAllocator::ResetInstanceIdCounter_ForTests(OriginalCounter);
    }
};

class FWaitForCondition final : public IAutomationLatentCommand
{
public:
    FWaitForCondition(FAutomationTestBase& InTest, FString InName, TFunction<bool()> InCondition)
        : Test(InTest), Name(MoveTemp(InName)), Condition(MoveTemp(InCondition)) {}
    virtual bool Update() override
    {
        if (Start == 0.0) { Start = FPlatformTime::Seconds(); }
        if (Condition()) { return true; }
        if (FPlatformTime::Seconds() - Start < 60.0) { return false; }
        Test.AddError(FString::Printf(TEXT("PIE timeout: %s"), *Name));
        return true;
    }
private:
    FAutomationTestBase& Test;
    FString Name;
    TFunction<bool()> Condition;
    double Start = 0.0;
};

void LogTimes(const TCHAR* Name, TArray<double> Samples)
{
    Samples.Sort();
    if (Samples.IsEmpty()) { return; }
    const int32 Count = Samples.Num();
    const double Median = Count % 2 ? Samples[Count / 2] : (Samples[Count / 2 - 1] + Samples[Count / 2]) * 0.5;
    const double P95 = Samples[FMath::CeilToInt(Count * 0.95) - 1];
    UE_LOG(LogTemp, Display, TEXT("Inventory100Items %s: n=%d median=%.3f ms p95=%.3f ms min=%.3f ms max=%.3f ms"),
        Name, Count, Median, P95, Samples[0], Samples.Last());
}

FInventorySaveRecord MakeRecord(UDataTable* Table, bool bReverse)
{
    FInventorySaveRecord Record;
    Record.NextInstanceId = ItemCount + 2;
    auto& Stash = Record.Containers.AddDefaulted_GetRef();
    Stash.ContainerId = TEXT("Stash");
    Stash.GridSize = FIntPoint(20, 20);
    const FSoftObjectPath Path(Table);
    for (int32 Index = 0; Index < ItemCount; ++Index)
    {
        auto& Item = Stash.Items.AddDefaulted_GetRef();
        Item.InstanceId = bReverse ? ItemCount - Index : Index + 1;
        Item.DefinitionTable = Path;
        Item.DefinitionRowName = TEXT("OneCell");
        Item.AnchorCell = FIntPoint(Index % 20, Index / 20);
    }
    auto& Bag = Record.Containers.AddDefaulted_GetRef();
    Bag.ContainerId = TEXT("Bag");
    Bag.GridSize = FIntPoint(4, 4);
    return Record;
}

int32 CountItemWidgets(const UCanvasPanel* Canvas)
{
    int32 Count = 0;
    for (int32 Index = 0; Canvas && Index < Canvas->GetChildrenCount(); ++Index)
    {
        Count += Cast<UInventoryItemWidget>(Canvas->GetChildAt(Index)) != nullptr;
    }
    return Count;
}

UInventoryScreenWidget* OpenScreen(AInventoryDemoPlayerController* Controller, UInventoryModel* Model)
{
    UClass* ScreenClass = LoadClass<UInventoryScreenWidget>(nullptr,
        TEXT("/Game/UI/WBP_InventoryScreen.WBP_InventoryScreen_C"));
    if (!ScreenClass) { return nullptr; }
    UInventoryScreenWidget* Screen = CreateWidget<UInventoryScreenWidget>(Controller, ScreenClass);
    if (!Screen) { return nullptr; }
    Screen->SetSession(Model);
    Screen->SetVisibility(ESlateVisibility::Collapsed);
    Screen->AddToViewport(10);
    Screen->ActivateWidget();
    return Screen;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInventory100ItemsPerformance, "Duckov.Performance.Inventory100Items", Flags)
bool FInventory100ItemsPerformance::RunTest(const FString& Parameters)
{
    if (!TestTrue(TEXT("Slate initialized"), FSlateApplication::IsInitialized())
        || !TestTrue(TEXT("데모 map load"), AutomationOpenMap(TEXT("/Game/Maps/L_InventoryDemo")))) { return false; }
    TSharedRef<FInventoryPerformanceScene> Scene = MakeShared<FInventoryPerformanceScene>();
    Scene->Table->RowStruct = FItemDefinitionRow::StaticStruct();
    FItemDefinitionRow Definition;
    Definition.Size = FIntPoint(1, 1);
    Scene->Table->AddRow(TEXT("OneCell"), Definition);
    Scene->Record = MakeRecord(Scene->Table.Get(), false);

    ADD_LATENT_AUTOMATION_COMMAND(FWaitForCondition(*this, TEXT("데모 PIE controller"), []
    {
        return GEditor && GEditor->PlayWorld
            && Cast<AInventoryDemoPlayerController>(UGameplayStatics::GetPlayerController(GEditor->PlayWorld, 0));
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        if (!TestTrue(TEXT("PIE world 준비"), GEditor && GEditor->PlayWorld)) { return; }
        AInventoryDemoPlayerController* Controller = Cast<AInventoryDemoPlayerController>(
            UGameplayStatics::GetPlayerController(GEditor->PlayWorld, 0));
        UInventoryModel* Model = ReflectedObject<UInventoryModel>(Controller, TEXT("Model"));
        if (!TestNotNull(TEXT("PIE Model"), Model)) { return; }
        Scene->Controller = Controller;
        Scene->Model = Model;
        if (!TestEqual(TEXT("100개 fixture Load"), Model->Load(Scene->Record), EInventorySaveFailure::None)) { return; }
        const FInventoryContainer* Stash = Model->FindContainer(TEXT("Stash"));
        if (!TestNotNull(TEXT("20x20 Stash"), Stash)) { return; }
        TestEqual(TEXT("GridSize"), Stash->GridSize, FIntPoint(20, 20));
        TestEqual(TEXT("Model Item count"), Stash->Items.Num(), ItemCount);
        TSet<FIntPoint> Cells;
        for (const FItemInstance& Item : Stash->Items) { Cells.Add(Item.AnchorCell); }
        TestEqual(TEXT("서로 다른 cell 100개"), Cells.Num(), ItemCount);
        GInventoryPerformanceProbe = &Scene->Probe;
        const double Start = FPlatformTime::Seconds();
        UInventoryScreenWidget* Screen = OpenScreen(Controller, Model);
        Scene->ColdOpenMs = (FPlatformTime::Seconds() - Start) * 1000.0;
        Scene->Screen = Screen;
        Scene->Grid = Screen ? Cast<UInventoryGridWidget>(Screen->GetWidgetFromName(TEXT("LeftGrid"))) : nullptr;
        Scene->Container = ReflectedObject<UContainerViewModel>(Screen, TEXT("LeftVM"));
        TestTrue(TEXT("실제 WBP 활성"), Screen && Screen->IsActivated() && Screen->IsInViewport());
        TestTrue(TEXT("초기 Stash grid Refresh"), Scene->Probe.GridRefresh > 0);
        TestEqual(TEXT("초기 Item Widget 생성"), Scene->Probe.ItemCreated, ItemCount);
    }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitForCondition(*this, TEXT("100 Item Slate geometry"), [Scene]
    {
        UInventoryGridWidget* Grid = Scene->Grid.Get();
        UCanvasPanel* Canvas = Grid ? Cast<UCanvasPanel>(Grid->GetWidgetFromName(TEXT("GridCanvas"))) : nullptr;
        return Canvas && CountItemWidgets(Canvas) == ItemCount && Canvas->GetCachedGeometry().GetLocalSize().X > 0.f;
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        UInventoryGridWidget* Grid = Scene->Grid.Get();
        UCanvasPanel* Canvas = Grid ? Cast<UCanvasPanel>(Grid->GetWidgetFromName(TEXT("GridCanvas"))) : nullptr;
        UContainerViewModel* VM = Scene->Container.Get();
        if (!TestNotNull(TEXT("실제 GridCanvas"), Canvas) || !TestNotNull(TEXT("Stash VM"), VM)) { return; }
        TestEqual(TEXT("실제 Item Widget 100개"), CountItemWidgets(Canvas), ItemCount);
        TestTrue(TEXT("400개 cell Widget 없음"), Canvas->GetChildrenCount() < 400);
        TestEqual(TEXT("VM Item count"), VM->GetItems().Num(), ItemCount);
        UE_LOG(LogTemp, Display, TEXT("Inventory100Items initial: gridRefresh=%d itemRefresh=%d created=%d removed=%d canvasChildren=%d itemWidgets=%d nonItemChildren=%d"),
            Scene->Probe.GridRefresh, Scene->Probe.ItemRefresh, Scene->Probe.ItemCreated,
            Scene->Probe.ItemRemoved, Canvas->GetChildrenCount(), CountItemWidgets(Canvas),
            Canvas->GetChildrenCount() - CountItemWidgets(Canvas));
        UE_LOG(LogTemp, Display, TEXT("Inventory100Items first open synchronous: %.3f ms"), Scene->ColdOpenMs);
        // 에셋과 Slate 경로를 warm-up한 뒤 새 WBP 생성부터 활성화까지 측정한다.
        for (int32 Index = 0; Index < WarmupCount + SampleCount; ++Index)
        {
            if (UInventoryScreenWidget* Previous = Scene->Screen.Get())
            {
                Previous->DeactivateWidget();
                Previous->RemoveFromParent();
            }
            Scene->Probe.Reset();
            const double Start = FPlatformTime::Seconds();
            UInventoryScreenWidget* Current = OpenScreen(Scene->Controller.Get(), Scene->Model.Get());
            const double Ms = (FPlatformTime::Seconds() - Start) * 1000.0;
            if (!TestNotNull(TEXT("새 WBP Screen"), Current)) { return; }
            Scene->Screen = Current;
            Scene->Grid = Cast<UInventoryGridWidget>(Current->GetWidgetFromName(TEXT("LeftGrid")));
            Scene->Container = ReflectedObject<UContainerViewModel>(Current, TEXT("LeftVM"));
            Grid = Scene->Grid.Get();
            Canvas = Grid ? Cast<UCanvasPanel>(Grid->GetWidgetFromName(TEXT("GridCanvas"))) : nullptr;
            if (Index >= WarmupCount) { Scene->OpenMs.Add(Ms); }
            TestEqual(TEXT("새 WBP Item Widget 100개"), CountItemWidgets(Canvas), ItemCount);
            TestEqual(TEXT("재개방 Item Widget 생성"), Scene->Probe.ItemCreated, ItemCount);
        }
        LogTimes(TEXT("new WBP open synchronous"), Scene->OpenMs);
        UE_LOG(LogTemp, Display, TEXT("Inventory100Items open last: gridRefresh=%d itemRefresh=%d created=%d removed=%d"),
            Scene->Probe.GridRefresh, Scene->Probe.ItemRefresh, Scene->Probe.ItemCreated, Scene->Probe.ItemRemoved);
        VM = Scene->Container.Get();
        const auto ActiveCallback = INotifyFieldValueChanged::FFieldValueChangedDelegate::CreateLambda(
            [Scene](UObject*, UE::FieldNotification::FFieldId) { ++Scene->ItemsNotifications; });
        Scene->ItemsHandle = VM->AddFieldValueChangedDelegate(UContainerViewModel::FFieldNotificationClassDescriptor::Items, ActiveCallback);
        Scene->ItemsNotifications = 0;
    }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitForCondition(*this, TEXT("최종 open Slate geometry"), [Scene]
    {
        return Scene->Grid.IsValid() && Scene->Grid->GetCachedGeometry().GetLocalSize().X > 0.f;
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        UInventoryGridWidget* Grid = Scene->Grid.Get();
        UCanvasPanel* Canvas = Grid ? Cast<UCanvasPanel>(Grid->GetWidgetFromName(TEXT("GridCanvas"))) : nullptr;
        if (!TestNotNull(TEXT("idle Canvas"), Canvas)) { return; }
        int32 ItemTickEnabled = 0;
        for (int32 Child = 0; Child < Canvas->GetChildrenCount(); ++Child)
        {
            if (UInventoryItemWidget* Candidate = Cast<UInventoryItemWidget>(Canvas->GetChildAt(Child)))
            {
                ItemTickEnabled += Candidate->TakeWidget()->GetCanTick();
            }
        }
        UE_LOG(LogTemp, Display, TEXT("Inventory100Items Slate tick capability: screen=%d grid=%d items=%d/%d"),
            Scene->Screen->TakeWidget()->GetCanTick(), Grid->TakeWidget()->GetCanTick(), ItemTickEnabled, CountItemWidgets(Canvas));
        Scene->IdleGridRefresh = Scene->Probe.GridRefresh;
        Scene->IdleItemRefresh = Scene->Probe.ItemRefresh;
        Scene->IdleStartFrame = GFrameCounter;
        Scene->IdleStartTime = FPlatformTime::Seconds();
    }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitForCondition(*this, TEXT("idle 300 engine frames"), [Scene]
    {
        return GFrameCounter - Scene->IdleStartFrame >= 300;
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        TestTrue(TEXT("idle 최소 300 frame"), GFrameCounter - Scene->IdleStartFrame >= 300);
        TestTrue(TEXT("최종 WBP Slate geometry"), Scene->Grid.IsValid()
            && Scene->Grid->GetCachedGeometry().GetLocalSize().X > 0.f);
        TestEqual(TEXT("idle Grid Refresh 추가 0"), Scene->Probe.GridRefresh - Scene->IdleGridRefresh, 0);
        TestEqual(TEXT("idle Item Refresh 추가 0"), Scene->Probe.ItemRefresh - Scene->IdleItemRefresh, 0);
        UE_LOG(LogTemp, Display, TEXT("Inventory100Items idle: frames=%llu gridRefreshDelta=%d itemRefreshDelta=%d elapsed=%.3f s (engine wait, not UI CPU time)"),
            GFrameCounter - Scene->IdleStartFrame, Scene->Probe.GridRefresh - Scene->IdleGridRefresh,
            Scene->Probe.ItemRefresh - Scene->IdleItemRefresh, FPlatformTime::Seconds() - Scene->IdleStartTime);

        UInventoryModel* Model = Scene->Model.Get();
        UContainerViewModel* VM = Scene->Container.Get();
        UCanvasPanel* Canvas = Scene->Probe.Canvas;
        if (!TestNotNull(TEXT("move Model"), Model) || !TestNotNull(TEXT("move VM"), VM)
            || !TestNotNull(TEXT("move Canvas"), Canvas)) { return; }
        auto FindItemVM = [VM]() -> UItemViewModel*
        {
            const auto* Found = VM->GetItems().FindByPredicate([](const auto& Item) { return Item->GetInstanceId() == 1; });
            return Found ? Found->Get() : nullptr;
        };
        // 매번 fixture를 복원하는 non-tail 이동과 같은 Item의 연속 이동을 분리한다.
        for (const bool bNonTail : {true, false})
        {
            for (int32 Index = 0; Index < WarmupCount + SampleCount; ++Index)
            {
                if (bNonTail && !TestEqual(TEXT("non-tail move 준비 Load"), Model->Load(Scene->Record), EInventorySaveFailure::None)) { return; }
                UItemViewModel* ItemVM = FindItemVM();
                if (!TestNotNull(TEXT("move 전 ID 1 VM"), ItemVM)) { return; }
                const auto PreviousItems = VM->GetItems();
                Scene->Probe.Reset();
                Scene->ItemsNotifications = 0;
                const FIntPoint Target = bNonTail || Index % 2 ? FIntPoint(10, 10) : FIntPoint(0, 0);
                const double Start = FPlatformTime::Seconds();
                const EInventoryOperationFailure Result = Model->TryMove(TEXT("Stash"), TEXT("Stash"), 1, Target, false);
                const double Ms = (FPlatformTime::Seconds() - Start) * 1000.0;
                TestEqual(TEXT("single-item move 성공"), Result, EInventoryOperationFailure::None);
                TestEqual(TEXT("move ID 기준 VM identity"), FindItemVM(), ItemVM);
                const FItemInstance* Moved = Model->FindItem(TEXT("Stash"), 1);
                if (!TestNotNull(TEXT("move 이후 ID 1 Model item"), Moved)) { return; }
                TestEqual(TEXT("move Model anchor"), Moved->AnchorCell, Target);
                // same-container 이동은 배열 순서와 기존 Widget을 유지해야 한다.
                TestEqual(TEXT("hard gate: same-container move Grid rebuild 0"), Scene->Probe.GridRefresh, 0);
                TestEqual(TEXT("hard gate: move Item 생성 0"), Scene->Probe.ItemCreated, 0);
                TestEqual(TEXT("hard gate: move Item 제거 0"), Scene->Probe.ItemRemoved, 0);
                TestTrue(TEXT("localized move Items 목록 불변"), PreviousItems == VM->GetItems());
                TestEqual(TEXT("localized move 갱신 Widget 1개"), Scene->Probe.RefreshByWidget.Num(), 1);
                TestEqual(TEXT("localized move Items notification 0"), Scene->ItemsNotifications, 0);
                if (Index >= WarmupCount) { (bNonTail ? Scene->NonTailMoveMs : Scene->MoveMs).Add(Ms); }
            }
            LogTimes(bNonTail ? TEXT("non-tail move operation") : TEXT("list-stable move operation"),
                bNonTail ? Scene->NonTailMoveMs : Scene->MoveMs);
            UE_LOG(LogTemp, Display, TEXT("Inventory100Items move last: fixtureReset=%d itemsNotifications=%d gridRefresh=%d itemRefresh=%d distinctItems=%d created=%d removed=%d boundRefresh=%d unboundRefresh=%d"),
                bNonTail, Scene->ItemsNotifications, Scene->Probe.GridRefresh, Scene->Probe.ItemRefresh,
                Scene->Probe.RefreshByWidget.Num(), Scene->Probe.ItemCreated, Scene->Probe.ItemRemoved,
                Scene->Probe.BoundItemRefresh, Scene->Probe.UnboundItemRefresh);
        }

        const FInventorySaveRecord Reverse = MakeRecord(Scene->Table.Get(), true);
        for (int32 Index = 0; Index < WarmupCount + SampleCount; ++Index)
        {
            if (!TestEqual(TEXT("sort 준비 Load"), Model->Load(Reverse), EInventorySaveFailure::None)) { return; }
            TestEqual(TEXT("sort 준비 reverse 첫 ID"), Model->FindContainer(TEXT("Stash"))->Items[0].InstanceId, ItemCount);
            Scene->Probe.Reset();
            Scene->ItemsNotifications = 0;
            const double Start = FPlatformTime::Seconds();
            const EInventoryOperationFailure Result = Model->TrySort(TEXT("Stash"));
            const double Ms = (FPlatformTime::Seconds() - Start) * 1000.0;
            TestEqual(TEXT("sort 성공"), Result, EInventoryOperationFailure::None);
            TestEqual(TEXT("sort 첫 ID"), Model->FindContainer(TEXT("Stash"))->Items[0].InstanceId, 1);
            TestEqual(TEXT("sort Items notification"), Scene->ItemsNotifications, 1);
            TestEqual(TEXT("sort Grid Refresh"), Scene->Probe.GridRefresh, 1);
            TestEqual(TEXT("sort Widget 제거"), Scene->Probe.ItemRemoved, ItemCount);
            TestEqual(TEXT("sort Widget 생성"), Scene->Probe.ItemCreated, ItemCount);
            TestEqual(TEXT("sort 이후 Widget count"), CountItemWidgets(Canvas), ItemCount);
            if (Index >= WarmupCount) { Scene->SortMs.Add(Ms); }
        }
        LogTimes(TEXT("sort operation and UI refresh"), Scene->SortMs);
        UE_LOG(LogTemp, Display, TEXT("Inventory100Items sort last: itemsNotifications=%d gridRefresh=%d itemRefresh=%d created=%d removed=%d boundRefresh=%d unboundRefresh=%d"),
            Scene->ItemsNotifications, Scene->Probe.GridRefresh, Scene->Probe.ItemRefresh,
            Scene->Probe.ItemCreated, Scene->Probe.ItemRemoved, Scene->Probe.BoundItemRefresh, Scene->Probe.UnboundItemRefresh);
    }, 0.f));
    // 앞선 assertion이나 준비 단계가 실패해도 화면과 delegate를 정리한다.
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([Scene]
    {
        if (UContainerViewModel* VM = Scene->Container.Get())
        {
            VM->RemoveFieldValueChangedDelegate(UContainerViewModel::FFieldNotificationClassDescriptor::Items, Scene->ItemsHandle);
        }
        Scene->ItemsHandle.Reset();
        GInventoryPerformanceProbe = nullptr;
        if (UInventoryScreenWidget* Screen = Scene->Screen.Get())
        {
            Screen->DeactivateWidget();
            Screen->RemoveFromParent();
        }
        Scene->Controller.Reset();
        Scene->Model.Reset();
        Scene->Screen.Reset();
        Scene->Grid.Reset();
        Scene->Container.Reset();
    }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    ADD_LATENT_AUTOMATION_COMMAND(FWaitForCondition(*this, TEXT("PIE 종료"), [] { return !GEditor || !GEditor->PlayWorld; }));
    return true;
}
#endif
