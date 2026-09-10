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

FItemInstance MakeItem(int32 Id, FIntPoint Anchor, bool bRotated = false)
{
    UDataTable* Table = NewObject<UDataTable>();
    Table->RowStruct = FItemDefinitionRow::StaticStruct();
    FItemDefinitionRow Row;
    Row.Size = FIntPoint(1, 2);
    Table->AddRow(TEXT("Bar"), Row);

    FItemInstance Item;
    Item.DefinitionTable = Table;
    Item.DefinitionRowName = TEXT("Bar");
    Item.InstanceId = Id;
    Item.AnchorCell = Anchor;
    Item.bRotated = bRotated;
    Item.Quantity = 3;
    return Item;
}

bool SameItems(const FInventoryContainer& Left, const FInventoryContainer& Right)
{
    if (Left.Items.Num() != Right.Items.Num()) { return false; }
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

bool SameState(const FInventoryContainer& Left, const FInventoryContainer& Right)
{
    return Left.GridSize == Right.GridSize && Left.OccupancyCache == Right.OccupancyCache &&
        SameItems(Left, Right);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestResize_GrowPreservesItemsAndReindexesCache,
    "Duckov.InventoryCore.Resize.TestResize_GrowPreservesItemsAndReindexesCache", TestFlags)

bool TestResize_GrowPreservesItemsAndReindexesCache::RunTest(const FString& Parameters)
{
    FInventoryContainer Container = FInventoryContainer::MakeEmpty(FIntPoint(2, 2));
    TestEqual(TEXT("첫 항목 배치"), FInventoryPlacement::TryPlace(Container, MakeItem(20, FIntPoint(1, 0))), EInventoryOperationFailure::None);
    TestEqual(TEXT("두 번째 항목 배치"), FInventoryPlacement::TryPlace(Container, MakeItem(10, FIntPoint(0, 0))), EInventoryOperationFailure::None);
    const FInventoryContainer Before = Container;

    TestEqual(TEXT("확대 성공"), FInventoryOperations::TryResize(Container, FIntPoint(3, 3)), EInventoryOperationFailure::None);
    TestEqual(TEXT("새 크기"), Container.GridSize, FIntPoint(3, 3));
    TestTrue(TEXT("배열 순서와 모든 항목 필드 보존"), SameItems(Container, Before));
    const TArray<int32> ExpectedCache = {1, 0, -1, 1, 0, -1, -1, -1, -1};
    TestTrue(TEXT("새 행 너비에 맞게 cache 재생성"), Container.OccupancyCache == ExpectedCache);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestResize_ShrinkPreservesRotatedPlacement,
    "Duckov.InventoryCore.Resize.TestResize_ShrinkPreservesRotatedPlacement", TestFlags)

bool TestResize_ShrinkPreservesRotatedPlacement::RunTest(const FString& Parameters)
{
    FInventoryContainer Container = FInventoryContainer::MakeEmpty(FIntPoint(4, 3));
    TestEqual(TEXT("회전 항목 배치"), FInventoryPlacement::TryPlace(Container, MakeItem(5, FIntPoint(1, 1), true)), EInventoryOperationFailure::None);
    const FInventoryContainer Before = Container;

    TestEqual(TEXT("경계에 정확히 맞는 축소 성공"), FInventoryOperations::TryResize(Container, FIntPoint(3, 2)), EInventoryOperationFailure::None);
    TestEqual(TEXT("축소된 크기"), Container.GridSize, FIntPoint(3, 2));
    TestTrue(TEXT("위치와 회전을 포함한 항목 보존"), SameItems(Container, Before));
    const TArray<int32> ExpectedCache = {-1, -1, -1, -1, 0, 0};
    TestTrue(TEXT("축소 후 회전 footprint cache"), Container.OccupancyCache == ExpectedCache);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestResize_OverflowPreservesStateWithoutRepacking,
    "Duckov.InventoryCore.Resize.TestResize_OverflowPreservesStateWithoutRepacking", TestFlags)

bool TestResize_OverflowPreservesStateWithoutRepacking::RunTest(const FString& Parameters)
{
    FInventoryContainer Container = FInventoryContainer::MakeEmpty(FIntPoint(4, 4));
    TestEqual(TEXT("임시 계획에 먼저 들어갈 항목"), FInventoryPlacement::TryPlace(Container, MakeItem(1, FIntPoint(0, 0))), EInventoryOperationFailure::None);
    TestEqual(TEXT("오른쪽과 아래 경계의 회전 항목"), FInventoryPlacement::TryPlace(Container, MakeItem(2, FIntPoint(2, 3), true)), EInventoryOperationFailure::None);
    const FInventoryContainer Before = Container;

    for (const FIntPoint Size : {FIntPoint(3, 4), FIntPoint(4, 3), FIntPoint(5, 3)})
    {
        TestEqual(TEXT("한 축이라도 기존 배치를 넘으면 거부"), FInventoryOperations::TryResize(Container, Size), EInventoryOperationFailure::ResizeOverflow);
        TestTrue(TEXT("앞선 항목 배치 후 실패해도 원본 전체 보존"), SameState(Container, Before));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestResize_EmptyAndSameSize,
    "Duckov.InventoryCore.Resize.TestResize_EmptyAndSameSize", TestFlags)

bool TestResize_EmptyAndSameSize::RunTest(const FString& Parameters)
{
    FInventoryContainer Container = FInventoryContainer::MakeEmpty(FIntPoint(4, 4));
    TestEqual(TEXT("빈 Container 축소"), FInventoryOperations::TryResize(Container, FIntPoint(1, 1)), EInventoryOperationFailure::None);
    TestTrue(TEXT("축소 후 빈 cache와 크기"), SameState(Container, FInventoryContainer::MakeEmpty(FIntPoint(1, 1))));
    TestEqual(TEXT("빈 Container 확대"), FInventoryOperations::TryResize(Container, FIntPoint(2, 2)), EInventoryOperationFailure::None);
    TestEqual(TEXT("항목 배치"), FInventoryPlacement::TryPlace(Container, MakeItem(1, FIntPoint(0, 0))), EInventoryOperationFailure::None);
    const FInventoryContainer Before = Container;
    TestEqual(TEXT("같은 크기는 성공"), FInventoryOperations::TryResize(Container, Container.GridSize), EInventoryOperationFailure::None);
    TestTrue(TEXT("같은 크기의 전체 상태 보존"), SameState(Container, Before));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestResize_InvalidSizePreservesState,
    "Duckov.InventoryCore.Resize.TestResize_InvalidSizePreservesState", TestFlags)

bool TestResize_InvalidSizePreservesState::RunTest(const FString& Parameters)
{
    FInventoryContainer Container = FInventoryContainer::MakeEmpty(FIntPoint(2, 2));
    TestEqual(TEXT("초기 배치"), FInventoryPlacement::TryPlace(Container, MakeItem(1, FIntPoint(0, 0))), EInventoryOperationFailure::None);
    const FInventoryContainer Before = Container;
    for (const FIntPoint Size : {FIntPoint(0, 2), FIntPoint(2, 0), FIntPoint(-1, 2),
        FIntPoint(2, -1), FIntPoint(65536, 65536)})
    {
        TestEqual(TEXT("0·음수·셀 수 overflow 거부"), FInventoryOperations::TryResize(Container, Size), EInventoryOperationFailure::ResizeOverflow);
        TestTrue(TEXT("잘못된 크기의 원본 전체 보존"), SameState(Container, Before));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestResize_MissingDefinitionPreservesState,
    "Duckov.InventoryCore.Resize.TestResize_MissingDefinitionPreservesState", TestFlags)

bool TestResize_MissingDefinitionPreservesState::RunTest(const FString& Parameters)
{
    FInventoryContainer Container = FInventoryContainer::MakeEmpty(FIntPoint(2, 2));
    Container.Items.AddDefaulted();
    Container.OccupancyCache[0] = 0;
    const FInventoryContainer Before = Container;
    TestEqual(TEXT("배치를 검증할 수 없으면 거부"), FInventoryOperations::TryResize(Container, FIntPoint(3, 3)), EInventoryOperationFailure::ResizeOverflow);
    TestTrue(TEXT("Definition 부재 시 원본 전체 보존"), SameState(Container, Before));
    return true;
}

#endif
