#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "InventoryContainer.h"
#include "InventoryOperations.h"
#include "InventoryPlacement.h"
#include "ItemDefinitionRow.h"

namespace
{
constexpr EAutomationTestFlags TestFlags =
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

UDataTable* MakeTable()
{
    UDataTable* Table = NewObject<UDataTable>();
    Table->RowStruct = FItemDefinitionRow::StaticStruct();
    FItemDefinitionRow Row;
    Row.Size = FIntPoint(2, 2);
    Table->AddRow(TEXT("Square"), Row);
    Row.Size = FIntPoint(1, 3);
    Table->AddRow(TEXT("Bar"), Row);
    Row.Size = FIntPoint(1, 1);
    Table->AddRow(TEXT("Small"), Row);
    return Table;
}

FItemInstance MakeItem(UDataTable* Table, FName Row, int32 Id, FIntPoint Anchor, bool bRotated = false)
{
    FItemInstance Item;
    Item.DefinitionTable = Table;
    Item.DefinitionRowName = Row;
    Item.InstanceId = Id;
    Item.AnchorCell = Anchor;
    Item.bRotated = bRotated;
    Item.Quantity = 7;
    return Item;
}

bool SameState(const FInventoryContainer& Left, const FInventoryContainer& Right)
{
    if (Left.GridSize != Right.GridSize || Left.OccupancyCache != Right.OccupancyCache ||
        Left.Items.Num() != Right.Items.Num())
    {
        return false;
    }
    for (int32 Index = 0; Index < Left.Items.Num(); ++Index)
    {
        const FItemInstance& A = Left.Items[Index];
        const FItemInstance& B = Right.Items[Index];
        if (A.InstanceId != B.InstanceId || A.DefinitionTable != B.DefinitionTable ||
            A.DefinitionRowName != B.DefinitionRowName || A.Quantity != B.Quantity ||
            A.AnchorCell != B.AnchorCell || A.bRotated != B.bRotated)
        {
            return false;
        }
    }
    return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestSort_DeterministicAndIdempotent,
    "Duckov.InventoryCore.Sort.TestSort_DeterministicAndIdempotent", TestFlags)

bool TestSort_DeterministicAndIdempotent::RunTest(const FString& Parameters)
{
    UDataTable* Table = MakeTable();
    FInventoryContainer A = FInventoryContainer::MakeEmpty(FIntPoint(4, 4));
    const FItemInstance Small = MakeItem(Table, TEXT("Small"), 1, FIntPoint(3, 3));
    const FItemInstance High = MakeItem(Table, TEXT("Square"), 20, FIntPoint(0, 2));
    const FItemInstance Low = MakeItem(Table, TEXT("Square"), 10, FIntPoint(0, 0));
    for (const FItemInstance& Item : {Small, High, Low})
    {
        TestEqual(TEXT("초기 배치 성공"), FInventoryPlacement::TryPlace(A, Item), EInventoryOperationFailure::None);
    }
    FInventoryContainer B = FInventoryContainer::MakeEmpty(A.GridSize);
    for (const FItemInstance& Item : {Low, Small, High})
    {
        TestEqual(TEXT("순서를 바꾼 초기 배치 성공"), FInventoryPlacement::TryPlace(B, Item), EInventoryOperationFailure::None);
    }
    if (!TestEqual(TEXT("정렬 성공"), FInventoryOperations::TrySort(A), EInventoryOperationFailure::None) ||
        !TestEqual(TEXT("순서를 바꿔도 정렬 성공"), FInventoryOperations::TrySort(B), EInventoryOperationFailure::None))
    {
        return false;
    }
    TestTrue(TEXT("입력 배열 순서와 무관하게 결과가 동일하다"), SameState(A, B));
    TestEqual(TEXT("항목 수 보존"), A.Items.Num(), 3);
    if (A.Items.Num() != 3) { return false; }
    TestEqual(TEXT("동일 면적은 작은 ID 먼저"), A.Items[0].InstanceId, 10);
    TestEqual(TEXT("두 번째 큰 항목"), A.Items[1].InstanceId, 20);
    TestEqual(TEXT("작은 항목은 마지막"), A.Items[2].InstanceId, 1);
    TestEqual(TEXT("첫 좌표"), A.Items[0].AnchorCell, FIntPoint(0, 0));
    TestEqual(TEXT("row-major 다음 좌표"), A.Items[1].AnchorCell, FIntPoint(2, 0));
    TestEqual(TEXT("작은 항목 좌표"), A.Items[2].AnchorCell, FIntPoint(0, 2));
    const TArray<int32> ExpectedCache = {0, 0, 1, 1, 0, 0, 1, 1, 2, -1, -1, -1, -1, -1, -1, -1};
    TestTrue(TEXT("cache가 정렬 후 배열 인덱스를 가리킨다"), A.OccupancyCache == ExpectedCache);
    for (const FItemInstance& Item : A.Items)
    {
        TestEqual(TEXT("수량 보존"), Item.Quantity, 7);
        TestTrue(TEXT("Definition Table 보존"), Item.DefinitionTable.Get() == Table);
        TestEqual(TEXT("Definition Row 보존"), Item.DefinitionRowName, Item.InstanceId == 1 ? FName(TEXT("Small")) : FName(TEXT("Square")));
    }
    const FInventoryContainer Before = A;
    TestEqual(TEXT("반복 정렬 성공"), FInventoryOperations::TrySort(A), EInventoryOperationFailure::None);
    TestTrue(TEXT("반복 정렬은 상태를 바꾸지 않는다"), SameState(A, Before));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestSort_GreedyFailurePreservesValidLayout,
    "Duckov.InventoryCore.Sort.TestSort_GreedyFailurePreservesValidLayout", TestFlags)

bool TestSort_GreedyFailurePreservesValidLayout::RunTest(const FString& Parameters)
{
    UDataTable* Table = MakeTable();
    FInventoryContainer Container = FInventoryContainer::MakeEmpty(FIntPoint(4, 4));
    for (const FItemInstance& Item : {
        MakeItem(Table, TEXT("Square"), 10, FIntPoint(0, 0)),
        MakeItem(Table, TEXT("Square"), 20, FIntPoint(0, 2)),
        MakeItem(Table, TEXT("Bar"), 30, FIntPoint(2, 0))})
    {
        TestEqual(TEXT("유효한 초기 배치"), FInventoryPlacement::TryPlace(Container, Item), EInventoryOperationFailure::None);
    }
    const FInventoryContainer Before = Container;
    TestEqual(TEXT("greedy 배치 실패"), FInventoryOperations::TrySort(Container), EInventoryOperationFailure::NoSpace);
    TestTrue(TEXT("자동 회전 없이 원래의 모든 상태 보존"), SameState(Container, Before));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestSort_PreservesRotation,
    "Duckov.InventoryCore.Sort.TestSort_PreservesRotation", TestFlags)

bool TestSort_PreservesRotation::RunTest(const FString& Parameters)
{
    UDataTable* Table = MakeTable();
    FInventoryContainer Container = FInventoryContainer::MakeEmpty(FIntPoint(3, 2));
    TestEqual(TEXT("회전된 초기 배치"), FInventoryPlacement::TryPlace(Container,
        MakeItem(Table, TEXT("Bar"), 5, FIntPoint(0, 1), true)), EInventoryOperationFailure::None);
    if (!TestEqual(TEXT("회전 상태 그대로 정렬 성공"), FInventoryOperations::TrySort(Container), EInventoryOperationFailure::None) || Container.Items.Num() != 1)
    {
        return false;
    }
    TestTrue(TEXT("회전 유지"), Container.Items[0].bRotated);
    TestEqual(TEXT("원점에 배치"), Container.Items[0].AnchorCell, FIntPoint(0, 0));
    const TArray<int32> ExpectedCache = {0, 0, 0, -1, -1, -1};
    TestTrue(TEXT("회전 footprint 점유"), Container.OccupancyCache == ExpectedCache);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestSort_EmptyContainer,
    "Duckov.InventoryCore.Sort.TestSort_EmptyContainer", TestFlags)

bool TestSort_EmptyContainer::RunTest(const FString& Parameters)
{
    FInventoryContainer Container = FInventoryContainer::MakeEmpty(FIntPoint(3, 2));
    const FInventoryContainer Before = Container;
    TestEqual(TEXT("빈 Container 정렬 성공"), FInventoryOperations::TrySort(Container), EInventoryOperationFailure::None);
    TestTrue(TEXT("빈 상태와 크기 보존"), SameState(Container, Before));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestSort_MissingDefinitionPreservesState,
    "Duckov.InventoryCore.Sort.TestSort_MissingDefinitionPreservesState", TestFlags)

bool TestSort_MissingDefinitionPreservesState::RunTest(const FString& Parameters)
{
    FInventoryContainer Container = FInventoryContainer::MakeEmpty(FIntPoint(2, 2));
    Container.Items.Add(MakeItem(nullptr, NAME_None, 1, FIntPoint(1, 1)));
    Container.OccupancyCache[3] = 0;
    const FInventoryContainer Before = Container;
    TestEqual(TEXT("Definition 부재는 NoSpace"), FInventoryOperations::TrySort(Container), EInventoryOperationFailure::NoSpace);
    TestTrue(TEXT("실패 상태 보존"), SameState(Container, Before));
    return true;
}

#endif
