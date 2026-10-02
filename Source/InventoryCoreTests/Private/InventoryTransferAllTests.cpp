#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "InventoryModel.h"
#include "ItemDefinitionRow.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

struct FTransferAllFixture
{
    int32 OriginalCounter = FItemInstanceIdAllocator::GetNextInstanceId();
    TStrongObjectPtr<UDataTable> Table{NewObject<UDataTable>()};
    TStrongObjectPtr<UInventoryModel> Model{NewObject<UInventoryModel>()};
    FInventorySaveRecord Record;

    FTransferAllFixture(FIntPoint SourceSize, FIntPoint TargetSize)
    {
        Table->RowStruct = FItemDefinitionRow::StaticStruct();
        FItemDefinitionRow Unit;
        Table->AddRow(TEXT("Unit"), Unit);
        FItemDefinitionRow Wide;
        Wide.Size = FIntPoint(2, 1);
        Wide.bStackable = true;
        Wide.MaxStack = 5;
        Table->AddRow(TEXT("Wide"), Wide);
        Record.NextInstanceId = 20;
        FInventoryContainerSaveRecord& Bag = Record.Containers.AddDefaulted_GetRef();
        Bag.ContainerId = TEXT("Bag");
        Bag.GridSize = SourceSize;
        FInventoryContainerSaveRecord& Stash = Record.Containers.AddDefaulted_GetRef();
        Stash.ContainerId = TEXT("Stash");
        Stash.GridSize = TargetSize;
    }

    ~FTransferAllFixture() { FItemInstanceIdAllocator::ResetInstanceIdCounter_ForTests(OriginalCounter); }

    void AddSaved(FName ContainerId, int32 Id, FName Row, int32 Quantity, FIntPoint Anchor, bool bRotated = false)
    {
        FInventoryContainerSaveRecord* Container = Record.Containers.FindByPredicate(
            [ContainerId](const FInventoryContainerSaveRecord& Entry) { return Entry.ContainerId == ContainerId; });
        FInventoryItemSaveRecord& Item = Container->Items.AddDefaulted_GetRef();
        Item.InstanceId = Id;
        Item.DefinitionTable = FSoftObjectPath(Table.Get());
        Item.DefinitionRowName = Row;
        Item.Quantity = Quantity;
        Item.AnchorCell = Anchor;
        Item.bRotated = bRotated;
    }
};

bool SameItem(const FItemInstance& Before, const FItemInstance& After)
{
    return Before.InstanceId == After.InstanceId && Before.DefinitionTable == After.DefinitionTable &&
        Before.DefinitionRowName == After.DefinitionRowName && Before.Quantity == After.Quantity &&
        Before.AnchorCell == After.AnchorCell && Before.bRotated == After.bRotated;
}

bool SameContainer(const FInventoryContainer& Before, const FInventoryContainer& After)
{
    if (Before.GridSize != After.GridSize || Before.Items.Num() != After.Items.Num() ||
        Before.OccupancyCache != After.OccupancyCache)
    {
        return false;
    }
    for (int32 Index = 0; Index < Before.Items.Num(); ++Index)
    {
        if (!SameItem(Before.Items[Index], After.Items[Index])) { return false; }
    }
    return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestModel_TransferAllSuccessAndChangeSet,
    "Duckov.InventoryCore.Model.TransferAll.SuccessAndChangeSet", TestFlags)
bool TestModel_TransferAllSuccessAndChangeSet::RunTest(const FString& Parameters)
{
    FTransferAllFixture F(FIntPoint(4, 3), FIntPoint(4, 3));
    F.AddSaved(TEXT("Bag"), 4, TEXT("Wide"), 3, FIntPoint(3, 1), true);
    F.AddSaved(TEXT("Bag"), 3, TEXT("Wide"), 2, FIntPoint(0, 0));
    F.AddSaved(TEXT("Stash"), 9, TEXT("Unit"), 1, FIntPoint(0, 0));
    F.AddSaved(TEXT("Stash"), 8, TEXT("Unit"), 1, FIntPoint(2, 0));
    if (!TestEqual(TEXT("초기 데이터 로드"), F.Model->Load(F.Record), EInventorySaveFailure::None)) { return false; }
    const FInventoryContainer BeforeSource = *F.Model->FindContainer(TEXT("Bag"));
    const FInventoryContainer BeforeTarget = *F.Model->FindContainer(TEXT("Stash"));
    FItemInstanceIdAllocator::ResetInstanceIdCounter_ForTests(MAX_int32);
    int32 Calls = 0;
    FInventoryChangeSet Last;
    F.Model->OnChanged().AddLambda([&](const FInventoryChangeSet& Change)
    {
        ++Calls;
        Last = Change;
        TestTrue(TEXT("알림 시점에 원본 전체 제거"), F.Model->FindContainer(TEXT("Bag"))->Items.IsEmpty());
        TestEqual(TEXT("알림 시점에 대상 전체 확정"), F.Model->FindContainer(TEXT("Stash"))->Items.Num(), 4);
        TestEqual(TEXT("전체 이전 재진입 거부"), F.Model->TryTransferAll(TEXT("Stash"), TEXT("Bag")),
            EInventoryOperationFailure::OperationInProgress);
        TestEqual(TEXT("같은 컨테이너도 재진입 guard 우선"), F.Model->TryTransferAll(TEXT("Bag"), TEXT("Bag")),
            EInventoryOperationFailure::OperationInProgress);
        TestEqual(TEXT("Load 재진입 거부"), F.Model->Load(F.Record), EInventorySaveFailure::OperationInProgress);
    });
    TestEqual(TEXT("ID 고갈 상태에서도 전체 이전 성공"), F.Model->TryTransferAll(TEXT("Bag"), TEXT("Stash")),
        EInventoryOperationFailure::None);
    const FInventoryContainer* Bag = F.Model->FindContainer(TEXT("Bag"));
    const FInventoryContainer* Stash = F.Model->FindContainer(TEXT("Stash"));
    TestTrue(TEXT("원본 비움"), Bag->Items.IsEmpty());
    TestEqual(TEXT("원본 grid 보존"), Bag->GridSize, BeforeSource.GridSize);
    TestTrue(TEXT("원본 점유 비움"), Bag->OccupancyCache == TArray<int32>{
        INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE,
        INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE});
    if (!TestEqual(TEXT("대상 항목 수"), Stash->Items.Num(), 4)) { return false; }
    TestTrue(TEXT("기존 대상 항목과 순서 보존"), SameItem(BeforeTarget.Items[0], Stash->Items[0]) &&
        SameItem(BeforeTarget.Items[1], Stash->Items[1]));
    for (int32 Index = 0; Index < BeforeSource.Items.Num(); ++Index)
    {
        FItemInstance Expected = BeforeSource.Items[Index];
        Expected.AnchorCell = Index == 0 ? FIntPoint(1, 0) : FIntPoint(2, 1);
        TestTrue(TEXT("원본 배열 순서와 ID·정의·수량·회전 보존 및 first-fit"), SameItem(Expected, Stash->Items[Index + 2]));
    }
    TestTrue(TEXT("회전 footprint와 기존 대상의 점유 보존"), Stash->OccupancyCache == TArray<int32>{
        0, 2, 1, INDEX_NONE, INDEX_NONE, 2, 3, 3, INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE});
    TestEqual(TEXT("대상 grid 보존"), Stash->GridSize, BeforeTarget.GridSize);
    TestEqual(TEXT("ID 카운터 보존"), FItemInstanceIdAllocator::GetNextInstanceId(), MAX_int32);
    TestEqual(TEXT("알림 한 번"), Calls, 1);
    if (!TestEqual(TEXT("두 Container 변경"), Last.Containers.Num(), 2)) { return false; }
    const FInventoryContainerChange& SourceChange = Last.Containers[0];
    const FInventoryContainerChange& TargetChange = Last.Containers[1];
    TestEqual(TEXT("원본 Container"), SourceChange.ContainerId, FName(TEXT("Bag")));
    TestEqual(TEXT("대상 Container"), TargetChange.ContainerId, FName(TEXT("Stash")));
    TestTrue(TEXT("원본 Removed 순서"), SourceChange.Removed == TArray<int32>{4, 3});
    TestTrue(TEXT("대상 Added 순서"), TargetChange.Added == TArray<int32>{4, 3});
    TestTrue(TEXT("불필요한 item delta 없음"), SourceChange.Added.IsEmpty() && SourceChange.Updated.IsEmpty() &&
        TargetChange.Removed.IsEmpty() && TargetChange.Updated.IsEmpty());
    TestTrue(TEXT("양쪽 배열 순서 변경 알림"), SourceChange.bOrderChanged && TargetChange.bOrderChanged);
    TestFalse(TEXT("grid 변경 없음"), SourceChange.bGridSizeChanged || TargetChange.bGridSizeChanged);
    TestFalse(TEXT("Reset 아님"), Last.bReset);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestModel_TransferAllPartialFitFailureIsAtomic,
    "Duckov.InventoryCore.Model.TransferAll.PartialFitFailureIsAtomic", TestFlags)
bool TestModel_TransferAllPartialFitFailureIsAtomic::RunTest(const FString& Parameters)
{
    FTransferAllFixture F(FIntPoint(3, 1), FIntPoint(3, 1));
    F.AddSaved(TEXT("Bag"), 4, TEXT("Unit"), 1, FIntPoint(0, 0));
    F.AddSaved(TEXT("Bag"), 3, TEXT("Wide"), 3, FIntPoint(1, 0));
    F.AddSaved(TEXT("Stash"), 9, TEXT("Unit"), 1, FIntPoint(0, 0));
    if (!TestEqual(TEXT("초기 데이터 로드"), F.Model->Load(F.Record), EInventorySaveFailure::None)) { return false; }
    const FInventoryContainer BeforeSource = *F.Model->FindContainer(TEXT("Bag"));
    const FInventoryContainer BeforeTarget = *F.Model->FindContainer(TEXT("Stash"));
    const int32 Counter = FItemInstanceIdAllocator::GetNextInstanceId();
    int32 Calls = 0;
    F.Model->OnChanged().AddLambda([&](const FInventoryChangeSet&) { ++Calls; });
    TestEqual(TEXT("첫 항목은 대상에 들어가는 조건"),
        F.Model->CanMove(TEXT("Bag"), TEXT("Stash"), 4, FIntPoint(1, 0), false), EInventoryOperationFailure::None);
    TestEqual(TEXT("둘째 항목의 공간 부족"), F.Model->TryTransferAll(TEXT("Bag"), TEXT("Stash")),
        EInventoryOperationFailure::NoSpace);
    TestTrue(TEXT("원본 항목·순서·cache 보존"), SameContainer(BeforeSource, *F.Model->FindContainer(TEXT("Bag"))));
    TestTrue(TEXT("대상 항목·순서·cache 보존"), SameContainer(BeforeTarget, *F.Model->FindContainer(TEXT("Stash"))));
    TestEqual(TEXT("ID 카운터 보존"), FItemInstanceIdAllocator::GetNextInstanceId(), Counter);
    TestEqual(TEXT("부분 이전 알림 없음"), Calls, 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestModel_TransferAllDoesNotAutoRotate,
    "Duckov.InventoryCore.Model.TransferAll.DoesNotAutoRotate", TestFlags)
bool TestModel_TransferAllDoesNotAutoRotate::RunTest(const FString& Parameters)
{
    FTransferAllFixture F(FIntPoint(2, 1), FIntPoint(1, 2));
    F.AddSaved(TEXT("Bag"), 4, TEXT("Wide"), 3, FIntPoint(0, 0));
    if (!TestEqual(TEXT("초기 데이터 로드"), F.Model->Load(F.Record), EInventorySaveFailure::None)) { return false; }
    const FInventoryContainer BeforeSource = *F.Model->FindContainer(TEXT("Bag"));
    const FInventoryContainer BeforeTarget = *F.Model->FindContainer(TEXT("Stash"));
    const int32 Counter = FItemInstanceIdAllocator::GetNextInstanceId();
    int32 Calls = 0;
    F.Model->OnChanged().AddLambda([&](const FInventoryChangeSet&) { ++Calls; });
    TestEqual(TEXT("회전하면 들어가는 조건"),
        F.Model->CanMove(TEXT("Bag"), TEXT("Stash"), 4, FIntPoint(0, 0), true), EInventoryOperationFailure::None);
    TestEqual(TEXT("현재 회전으로만 이전"), F.Model->TryTransferAll(TEXT("Bag"), TEXT("Stash")),
        EInventoryOperationFailure::NoSpace);
    TestTrue(TEXT("원본 보존"), SameContainer(BeforeSource, *F.Model->FindContainer(TEXT("Bag"))));
    TestTrue(TEXT("대상 보존"), SameContainer(BeforeTarget, *F.Model->FindContainer(TEXT("Stash"))));
    TestEqual(TEXT("ID 카운터 보존"), FItemInstanceIdAllocator::GetNextInstanceId(), Counter);
    TestEqual(TEXT("실패 알림 없음"), Calls, 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestModel_TransferAllEmptyAndInvalidContainers,
    "Duckov.InventoryCore.Model.TransferAll.EmptyAndInvalidContainers", TestFlags)
bool TestModel_TransferAllEmptyAndInvalidContainers::RunTest(const FString& Parameters)
{
    FTransferAllFixture F(FIntPoint(2, 2), FIntPoint(2, 2));
    F.AddSaved(TEXT("Stash"), 9, TEXT("Unit"), 1, FIntPoint(1, 1));
    if (!TestEqual(TEXT("초기 데이터 로드"), F.Model->Load(F.Record), EInventorySaveFailure::None)) { return false; }
    const FInventoryContainer BeforeSource = *F.Model->FindContainer(TEXT("Bag"));
    const FInventoryContainer BeforeTarget = *F.Model->FindContainer(TEXT("Stash"));
    const int32 Counter = FItemInstanceIdAllocator::GetNextInstanceId();
    int32 Calls = 0;
    F.Model->OnChanged().AddLambda([&](const FInventoryChangeSet&) { ++Calls; });
    TestEqual(TEXT("빈 원본은 성공 no-op"), F.Model->TryTransferAll(TEXT("Bag"), TEXT("Stash")),
        EInventoryOperationFailure::None);
    TestEqual(TEXT("없는 원본 거부"), F.Model->TryTransferAll(TEXT("Missing"), TEXT("Stash")),
        EInventoryOperationFailure::InvalidContainer);
    TestEqual(TEXT("빈 원본이어도 없는 대상 거부"), F.Model->TryTransferAll(TEXT("Bag"), TEXT("Missing")),
        EInventoryOperationFailure::InvalidContainer);
    TestEqual(TEXT("None 원본 거부"), F.Model->TryTransferAll(NAME_None, TEXT("Stash")),
        EInventoryOperationFailure::InvalidContainer);
    TestEqual(TEXT("같은 비어 있는 컨테이너 거부"), F.Model->TryTransferAll(TEXT("Bag"), TEXT("Bag")),
        EInventoryOperationFailure::InvalidContainer);
    TestEqual(TEXT("같은 채워진 컨테이너 거부"), F.Model->TryTransferAll(TEXT("Stash"), TEXT("Stash")),
        EInventoryOperationFailure::InvalidContainer);
    TestTrue(TEXT("원본 보존"), SameContainer(BeforeSource, *F.Model->FindContainer(TEXT("Bag"))));
    TestTrue(TEXT("대상 보존"), SameContainer(BeforeTarget, *F.Model->FindContainer(TEXT("Stash"))));
    TestEqual(TEXT("ID 카운터 보존"), FItemInstanceIdAllocator::GetNextInstanceId(), Counter);
    TestEqual(TEXT("no-op과 실패 알림 없음"), Calls, 0);
    return true;
}
#endif
