#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "InventoryModel.h"
#include "ItemDefinitionRow.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

struct FSplitFixture
{
    int32 OriginalCounter = FItemInstanceIdAllocator::GetNextInstanceId();
    TStrongObjectPtr<UDataTable> Table{NewObject<UDataTable>()};
    TStrongObjectPtr<UInventoryModel> Model{NewObject<UInventoryModel>()};
    FInventorySaveRecord Record;

    explicit FSplitFixture(FIntPoint Size)
    {
        Table->RowStruct = FItemDefinitionRow::StaticStruct();
        FItemDefinitionRow Stack;
        Stack.bStackable = true;
        Stack.MaxStack = 5;
        Stack.Size = FIntPoint(2, 1);
        Table->AddRow(TEXT("Stack"), Stack);
        FItemDefinitionRow Unit;
        Table->AddRow(TEXT("Unit"), Unit);
        Record.NextInstanceId = 20;
        FInventoryContainerSaveRecord& Bag = Record.Containers.AddDefaulted_GetRef();
        Bag.ContainerId = TEXT("Bag");
        Bag.GridSize = Size;
    }

    ~FSplitFixture() { FItemInstanceIdAllocator::ResetInstanceIdCounter_ForTests(OriginalCounter); }

    void AddSaved(int32 Id, FName Row, int32 Quantity, FIntPoint Anchor, bool bRotated = false)
    {
        FInventoryItemSaveRecord& Item = Record.Containers[0].Items.AddDefaulted_GetRef();
        Item.InstanceId = Id;
        Item.DefinitionTable = FSoftObjectPath(Table.Get());
        Item.DefinitionRowName = Row;
        Item.Quantity = Quantity;
        Item.AnchorCell = Anchor;
        Item.bRotated = bRotated;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestModel_SplitRotatedFirstFitAndChangeSet,
    "Duckov.InventoryCore.Model.Split.RotatedFirstFitAndChangeSet", TestFlags)
bool TestModel_SplitRotatedFirstFitAndChangeSet::RunTest(const FString& Parameters)
{
    FSplitFixture F(FIntPoint(3, 3));
    F.AddSaved(1, TEXT("Stack"), 5, FIntPoint(0, 0), true);
    F.AddSaved(2, TEXT("Unit"), 1, FIntPoint(1, 0));
    F.AddSaved(3, TEXT("Unit"), 1, FIntPoint(1, 1));
    if (!TestEqual(TEXT("초기 데이터 로드"), F.Model->Load(F.Record), EInventorySaveFailure::None)) { return false; }
    const int32 NewId = FItemInstanceIdAllocator::GetNextInstanceId();
    int32 Calls = 0;
    FInventoryChangeSet Last;
    F.Model->OnChanged().AddLambda([&](const FInventoryChangeSet& Change)
    {
        ++Calls;
        Last = Change;
        TestEqual(TEXT("알림 중 재진입 거부"), F.Model->TrySplit(TEXT("Bag"), 1, 1),
            EInventoryOperationFailure::OperationInProgress);
        const FItemInstance* Source = F.Model->FindItem(TEXT("Bag"), 1);
        TestEqual(TEXT("알림 시점에 원본 수량 확정"), Source->Quantity, 3);
    });
    TestEqual(TEXT("분할 성공"), F.Model->TrySplit(TEXT("Bag"), 1, 2), EInventoryOperationFailure::None);
    const FInventoryContainer* Bag = F.Model->FindContainer(TEXT("Bag"));
    if (!TestEqual(TEXT("항목 수"), Bag->Items.Num(), 4)) { return false; }
    const FItemInstance& Source = Bag->Items[0];
    const FItemInstance& Split = Bag->Items.Last();
    TestEqual(TEXT("원본 ID 보존"), Source.InstanceId, 1);
    TestEqual(TEXT("원본 수량"), Source.Quantity, 3);
    TestEqual(TEXT("원본 위치"), Source.AnchorCell, FIntPoint(0, 0));
    TestTrue(TEXT("원본 회전 보존"), Source.bRotated);
    TestEqual(TEXT("새 ID"), Split.InstanceId, NewId);
    TestEqual(TEXT("분할 수량"), Split.Quantity, 2);
    TestEqual(TEXT("분할 전후 총수량 보존"), Source.Quantity + Split.Quantity, 5);
    TestEqual(TEXT("회전 footprint 기준 첫 빈 위치"), Split.AnchorCell, FIntPoint(2, 0));
    TestTrue(TEXT("새 항목 회전 보존"), Split.bRotated);
    TestTrue(TEXT("Definition 보존"), Split.DefinitionTable == Source.DefinitionTable &&
        Split.DefinitionRowName == Source.DefinitionRowName);
    TestEqual(TEXT("새 항목 첫 점유 칸"), Bag->OccupancyCache[2], 3);
    TestEqual(TEXT("새 항목 둘째 점유 칸"), Bag->OccupancyCache[5], 3);
    TestEqual(TEXT("ID 카운터 한 번 증가"), FItemInstanceIdAllocator::GetNextInstanceId(), NewId + 1);
    TestEqual(TEXT("알림 한 번"), Calls, 1);
    if (!TestEqual(TEXT("변경 Container 하나"), Last.Containers.Num(), 1)) { return false; }
    TestEqual(TEXT("Bag 변경"), Last.Containers[0].ContainerId, FName(TEXT("Bag")));
    TestTrue(TEXT("Added 새 ID"), Last.Containers[0].Added == TArray<int32>{NewId});
    TestTrue(TEXT("Updated 원본 ID"), Last.Containers[0].Updated == TArray<int32>{1});
    TestTrue(TEXT("Removed 없음"), Last.Containers[0].Removed.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestModel_SplitFailuresPreserveState,
    "Duckov.InventoryCore.Model.Split.FailuresPreserveState", TestFlags)
bool TestModel_SplitFailuresPreserveState::RunTest(const FString& Parameters)
{
    FSplitFixture F(FIntPoint(3, 1));
    F.AddSaved(1, TEXT("Stack"), 3, FIntPoint(0, 0));
    F.AddSaved(2, TEXT("Unit"), 1, FIntPoint(2, 0));
    if (!TestEqual(TEXT("초기 데이터 로드"), F.Model->Load(F.Record), EInventorySaveFailure::None)) { return false; }
    const FInventoryContainer Before = *F.Model->FindContainer(TEXT("Bag"));
    const int32 Counter = FItemInstanceIdAllocator::GetNextInstanceId();
    int32 Calls = 0;
    F.Model->OnChanged().AddLambda([&](const FInventoryChangeSet&) { ++Calls; });
    auto CheckPreserved = [&]()
    {
        const FInventoryContainer* Bag = F.Model->FindContainer(TEXT("Bag"));
        TestEqual(TEXT("항목 수 보존"), Bag->Items.Num(), Before.Items.Num());
        TestEqual(TEXT("원본 수량 보존"), Bag->Items[0].Quantity, Before.Items[0].Quantity);
        TestEqual(TEXT("원본 ID 보존"), Bag->Items[0].InstanceId, Before.Items[0].InstanceId);
        TestTrue(TEXT("점유 cache 보존"), Bag->OccupancyCache == Before.OccupancyCache);
        TestEqual(TEXT("ID 카운터 보존"), FItemInstanceIdAllocator::GetNextInstanceId(), Counter);
        TestEqual(TEXT("실패 알림 없음"), Calls, 0);
    };
    for (const int32 Quantity : {0, -1, 3, 4})
    {
        TestEqual(TEXT("분할 수량 범위 거부"), F.Model->TrySplit(TEXT("Bag"), 1, Quantity),
            EInventoryOperationFailure::InvalidQuantity);
        CheckPreserved();
    }
    TestEqual(TEXT("비적재 아이템 거부"), F.Model->TrySplit(TEXT("Bag"), 2, 1),
        EInventoryOperationFailure::StackMismatch);
    CheckPreserved();
    TestEqual(TEXT("없는 아이템"), F.Model->TrySplit(TEXT("Bag"), 9, 1),
        EInventoryOperationFailure::ItemNotFound);
    CheckPreserved();
    TestEqual(TEXT("없는 Container"), F.Model->TrySplit(TEXT("Missing"), 1, 1),
        EInventoryOperationFailure::InvalidContainer);
    CheckPreserved();
    TestEqual(TEXT("빈 공간 없음"), F.Model->TrySplit(TEXT("Bag"), 1, 1),
        EInventoryOperationFailure::NoSpace);
    CheckPreserved();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestModel_SplitExhaustedIdPreservesState,
    "Duckov.InventoryCore.Model.Split.ExhaustedIdPreservesState", TestFlags)
bool TestModel_SplitExhaustedIdPreservesState::RunTest(const FString& Parameters)
{
    FSplitFixture F(FIntPoint(4, 1));
    F.AddSaved(1, TEXT("Stack"), 3, FIntPoint(0, 0));
    if (!TestEqual(TEXT("초기 데이터 로드"), F.Model->Load(F.Record), EInventorySaveFailure::None)) { return false; }
    const FInventoryContainer Before = *F.Model->FindContainer(TEXT("Bag"));
    FItemInstanceIdAllocator::ResetInstanceIdCounter_ForTests(MAX_int32);
    int32 Calls = 0;
    F.Model->OnChanged().AddLambda([&](const FInventoryChangeSet&) { ++Calls; });
    TestEqual(TEXT("ID 고갈"), F.Model->TrySplit(TEXT("Bag"), 1, 1),
        EInventoryOperationFailure::InstanceIdExhausted);
    const FInventoryContainer* Bag = F.Model->FindContainer(TEXT("Bag"));
    if (!TestEqual(TEXT("항목 수 보존"), Bag->Items.Num(), Before.Items.Num())) { return false; }
    TestEqual(TEXT("원본 수량 보존"), Bag->Items[0].Quantity, Before.Items[0].Quantity);
    TestEqual(TEXT("원본 ID 보존"), Bag->Items[0].InstanceId, Before.Items[0].InstanceId);
    TestTrue(TEXT("점유 cache 보존"), Bag->OccupancyCache == Before.OccupancyCache);
    TestEqual(TEXT("ID 카운터 보존"), FItemInstanceIdAllocator::GetNextInstanceId(), MAX_int32);
    TestEqual(TEXT("실패 알림 없음"), Calls, 0);
    return true;
}
#endif
