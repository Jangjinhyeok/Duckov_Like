#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/DataTable.h"
#include "InventoryContainer.h"
#include "InventoryPlacement.h"
#include "ItemDefinitionRow.h"
#include "ItemInstance.h"

namespace
{
constexpr EAutomationTestFlags TestFlags =
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

UDataTable* MakeDefinitionTable(const FIntPoint Size)
{
    UDataTable* Table = NewObject<UDataTable>();
    Table->RowStruct = FItemDefinitionRow::StaticStruct();

    FItemDefinitionRow Row;
    Row.Size = Size;
    Table->AddRow(TEXT("TestItem"), Row);
    return Table;
}

FItemInstance MakeItem(UDataTable* Table, const FIntPoint AnchorCell, const bool bRotated = false)
{
    FItemInstance Item;
    Item.InstanceId = FItemInstanceIdAllocator::AllocateNextInstanceId();
    Item.DefinitionRowName = TEXT("TestItem");
    Item.DefinitionTable = Table;
    Item.AnchorCell = AnchorCell;
    Item.bRotated = bRotated;
    return Item;
}

bool AreItemsEqual(const TArray<FItemInstance>& Left, const TArray<FItemInstance>& Right)
{
    if (Left.Num() != Right.Num())
    {
        return false;
    }

    for (int32 Index = 0; Index < Left.Num(); ++Index)
    {
        const FItemInstance& LeftItem = Left[Index];
        const FItemInstance& RightItem = Right[Index];
        if (LeftItem.InstanceId != RightItem.InstanceId ||
            LeftItem.DefinitionRowName != RightItem.DefinitionRowName ||
            LeftItem.DefinitionTable != RightItem.DefinitionTable ||
            LeftItem.Quantity != RightItem.Quantity ||
            LeftItem.AnchorCell != RightItem.AnchorCell ||
            LeftItem.bRotated != RightItem.bRotated)
        {
            return false;
        }
    }

    return true;
}

void TestContainerUnchanged(
    FAutomationTestBase& Test,
    const FInventoryContainer& Before,
    const FInventoryContainer& After)
{
    Test.TestTrue(TEXT("Items가 호출 전과 동일하다"), AreItemsEqual(Before.Items, After.Items));
    Test.TestTrue(TEXT("OccupancyCache가 호출 전과 동일하다"), After.OccupancyCache == Before.OccupancyCache);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    TestPlace_WithinBounds_Succeeds,
    "Duckov.InventoryCore.Placement.TestPlace_WithinBounds_Succeeds",
    TestFlags)

bool TestPlace_WithinBounds_Succeeds::RunTest(const FString& Parameters)
{
    FItemInstanceIdAllocator::ResetInstanceIdCounter_ForTests();
    UDataTable* Table = MakeDefinitionTable(FIntPoint(2, 2));
    FInventoryContainer Container = FInventoryContainer::MakeEmpty(FIntPoint(4, 4));

    const EInventoryOperationFailure Result =
        FInventoryPlacement::TryPlace(Container, MakeItem(Table, FIntPoint(1, 1)));

    TestEqual(TEXT("배치가 성공한다"), Result, EInventoryOperationFailure::None);
    TestEqual(TEXT("Item이 하나 추가된다"), Container.Items.Num(), 1);
    TestEqual(TEXT("footprint 첫 셀이 점유된다"), Container.OccupancyCache[5], 0);
    TestEqual(TEXT("footprint 마지막 셀이 점유된다"), Container.OccupancyCache[10], 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    TestPlace_OutOfBounds_ReturnsNoSpace_ContainerUnchanged,
    "Duckov.InventoryCore.Placement.TestPlace_OutOfBounds_ReturnsNoSpace_ContainerUnchanged",
    TestFlags)

bool TestPlace_OutOfBounds_ReturnsNoSpace_ContainerUnchanged::RunTest(const FString& Parameters)
{
    FItemInstanceIdAllocator::ResetInstanceIdCounter_ForTests();
    UDataTable* Table = MakeDefinitionTable(FIntPoint(2, 2));
    FInventoryContainer Container = FInventoryContainer::MakeEmpty(FIntPoint(3, 3));
    const FInventoryContainer Before = Container;

    const EInventoryOperationFailure Result =
        FInventoryPlacement::TryPlace(Container, MakeItem(Table, FIntPoint(2, 2)));

    TestEqual(TEXT("경계를 벗어나면 NoSpace를 반환한다"), Result, EInventoryOperationFailure::NoSpace);
    TestContainerUnchanged(*this, Before, Container);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    TestPlace_OverlapsOccupiedCell_ReturnsOccupied_ContainerUnchanged,
    "Duckov.InventoryCore.Placement.TestPlace_OverlapsOccupiedCell_ReturnsOccupied_ContainerUnchanged",
    TestFlags)

bool TestPlace_OverlapsOccupiedCell_ReturnsOccupied_ContainerUnchanged::RunTest(const FString& Parameters)
{
    FItemInstanceIdAllocator::ResetInstanceIdCounter_ForTests();
    UDataTable* Table = MakeDefinitionTable(FIntPoint(2, 2));
    FInventoryContainer Container = FInventoryContainer::MakeEmpty(FIntPoint(4, 4));
    FInventoryPlacement::TryPlace(Container, MakeItem(Table, FIntPoint(0, 0)));
    const FInventoryContainer Before = Container;

    const EInventoryOperationFailure Result =
        FInventoryPlacement::TryPlace(Container, MakeItem(Table, FIntPoint(1, 1)));

    TestEqual(TEXT("점유 셀과 겹치면 Occupied를 반환한다"), Result, EInventoryOperationFailure::Occupied);
    TestContainerUnchanged(*this, Before, Container);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    TestPlace_Rotated_OccupiesRotatedFootprint,
    "Duckov.InventoryCore.Placement.TestPlace_Rotated_OccupiesRotatedFootprint",
    TestFlags)

bool TestPlace_Rotated_OccupiesRotatedFootprint::RunTest(const FString& Parameters)
{
    FItemInstanceIdAllocator::ResetInstanceIdCounter_ForTests();
    UDataTable* Table = MakeDefinitionTable(FIntPoint(2, 1));
    FInventoryContainer Container = FInventoryContainer::MakeEmpty(FIntPoint(3, 3));

    const EInventoryOperationFailure Result =
        FInventoryPlacement::TryPlace(Container, MakeItem(Table, FIntPoint(1, 0), true));

    TestEqual(TEXT("회전 배치가 성공한다"), Result, EInventoryOperationFailure::None);
    TestEqual(TEXT("회전 footprint 위쪽 셀이 점유된다"), Container.OccupancyCache[1], 0);
    TestEqual(TEXT("회전 footprint 아래쪽 셀이 점유된다"), Container.OccupancyCache[4], 0);
    TestEqual(TEXT("회전 전 가로 방향 셀은 비어 있다"), Container.OccupancyCache[2], INDEX_NONE);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    TestPlace_RotatedOutOfBounds_ReturnsNoSpace_ContainerUnchanged,
    "Duckov.InventoryCore.Placement.TestPlace_RotatedOutOfBounds_ReturnsNoSpace_ContainerUnchanged",
    TestFlags)

bool TestPlace_RotatedOutOfBounds_ReturnsNoSpace_ContainerUnchanged::RunTest(const FString& Parameters)
{
    FItemInstanceIdAllocator::ResetInstanceIdCounter_ForTests();
    UDataTable* Table = MakeDefinitionTable(FIntPoint(2, 1));
    FInventoryContainer Container = FInventoryContainer::MakeEmpty(FIntPoint(3, 3));
    const FInventoryContainer Before = Container;

    const EInventoryOperationFailure Result =
        FInventoryPlacement::TryPlace(Container, MakeItem(Table, FIntPoint(1, 2), true));

    TestEqual(TEXT("회전 footprint가 경계를 벗어나면 NoSpace를 반환한다"), Result, EInventoryOperationFailure::NoSpace);
    TestContainerUnchanged(*this, Before, Container);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    TestPlace_1x1Item_OccupiesSingleCell,
    "Duckov.InventoryCore.Placement.TestPlace_1x1Item_OccupiesSingleCell",
    TestFlags)

bool TestPlace_1x1Item_OccupiesSingleCell::RunTest(const FString& Parameters)
{
    FItemInstanceIdAllocator::ResetInstanceIdCounter_ForTests();
    UDataTable* Table = MakeDefinitionTable(FIntPoint(1, 1));
    FInventoryContainer Container = FInventoryContainer::MakeEmpty(FIntPoint(2, 2));

    const EInventoryOperationFailure Result =
        FInventoryPlacement::TryPlace(Container, MakeItem(Table, FIntPoint(1, 1)));

    TestEqual(TEXT("1x1 Item 배치가 성공한다"), Result, EInventoryOperationFailure::None);
    TestEqual(TEXT("지정한 한 셀만 점유된다"), Container.OccupancyCache[3], 0);
    TestEqual(TEXT("나머지 셀은 비어 있다"), Container.OccupancyCache[0], INDEX_NONE);
    TestEqual(TEXT("나머지 셀은 비어 있다"), Container.OccupancyCache[1], INDEX_NONE);
    TestEqual(TEXT("나머지 셀은 비어 있다"), Container.OccupancyCache[2], INDEX_NONE);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    TestPlace_FillsEntireContainer_Succeeds,
    "Duckov.InventoryCore.Placement.TestPlace_FillsEntireContainer_Succeeds",
    TestFlags)

bool TestPlace_FillsEntireContainer_Succeeds::RunTest(const FString& Parameters)
{
    FItemInstanceIdAllocator::ResetInstanceIdCounter_ForTests();
    UDataTable* Table = MakeDefinitionTable(FIntPoint(2, 2));
    FInventoryContainer Container = FInventoryContainer::MakeEmpty(FIntPoint(2, 2));

    const EInventoryOperationFailure Result =
        FInventoryPlacement::TryPlace(Container, MakeItem(Table, FIntPoint::ZeroValue));

    TestEqual(TEXT("Container 전체를 채우는 배치가 성공한다"), Result, EInventoryOperationFailure::None);
    for (const int32 OccupantIndex : Container.OccupancyCache)
    {
        TestEqual(TEXT("모든 셀이 배치된 Item을 가리킨다"), OccupantIndex, 0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    TestPlace_ExceedsRemainingSpace_ReturnsNoSpace_ContainerUnchanged,
    "Duckov.InventoryCore.Placement.TestPlace_ExceedsRemainingSpace_ReturnsNoSpace_ContainerUnchanged",
    TestFlags)

bool TestPlace_ExceedsRemainingSpace_ReturnsNoSpace_ContainerUnchanged::RunTest(const FString& Parameters)
{
    FItemInstanceIdAllocator::ResetInstanceIdCounter_ForTests();
    UDataTable* OneByOneTable = MakeDefinitionTable(FIntPoint(1, 1));
    UDataTable* TwoByTwoTable = MakeDefinitionTable(FIntPoint(2, 2));
    FInventoryContainer Container = FInventoryContainer::MakeEmpty(FIntPoint(3, 3));
    FInventoryPlacement::TryPlace(Container, MakeItem(OneByOneTable, FIntPoint(0, 0)));
    const FInventoryContainer Before = Container;

    const EInventoryOperationFailure Result =
        FInventoryPlacement::TryPlace(Container, MakeItem(TwoByTwoTable, FIntPoint(2, 1)));

    TestEqual(TEXT("남은 경계 공간을 넘으면 NoSpace를 반환한다"), Result, EInventoryOperationFailure::NoSpace);
    TestContainerUnchanged(*this, Before, Container);
    return true;
}

#endif
