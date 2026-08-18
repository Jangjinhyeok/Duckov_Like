#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/DataTable.h"
#include "InventoryContainer.h"
#include "InventoryOperations.h"
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

FItemInstance MakeItem(
    UDataTable* Table,
    const FIntPoint AnchorCell,
    const bool bRotated = false,
    const int32 Quantity = 1)
{
    FItemInstance Item;
    Item.InstanceId = FItemInstanceIdAllocator::AllocateNextInstanceId();
    Item.DefinitionRowName = TEXT("TestItem");
    Item.DefinitionTable = Table;
    Item.Quantity = Quantity;
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
    TestMove_WithinSameContainer_Succeeds,
    "Duckov.InventoryCore.Move.TestMove_WithinSameContainer_Succeeds",
    TestFlags)

bool TestMove_WithinSameContainer_Succeeds::RunTest(const FString& Parameters)
{
    FItemInstanceIdAllocator::ResetInstanceIdCounter_ForTests();
    UDataTable* Table = MakeDefinitionTable(FIntPoint(2, 1));
    FInventoryContainer Container = FInventoryContainer::MakeEmpty(FIntPoint(4, 4));
    const FItemInstance Item = MakeItem(Table, FIntPoint(0, 0));
    FInventoryPlacement::TryPlace(Container, Item);

    const EInventoryOperationFailure Result =
        FInventoryOperations::TryMove(Container, Container, Item.InstanceId, FIntPoint(1, 0), false);

    if (!TestEqual(TEXT("동일 Container 이동이 성공한다"), Result, EInventoryOperationFailure::None))
    {
        return false;
    }
    TestEqual(TEXT("Item 수가 유지된다"), Container.Items.Num(), 1);
    TestEqual(TEXT("AnchorCell이 변경된다"), Container.Items[0].AnchorCell, FIntPoint(1, 0));
    TestEqual(TEXT("기존 셀이 비워진다"), Container.OccupancyCache[0], INDEX_NONE);
    TestEqual(TEXT("겹치는 목적지 셀이 점유된다"), Container.OccupancyCache[1], 0);
    TestEqual(TEXT("새 목적지 셀이 점유된다"), Container.OccupancyCache[2], 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    TestMove_ToDifferentContainer_Succeeds,
    "Duckov.InventoryCore.Move.TestMove_ToDifferentContainer_Succeeds",
    TestFlags)

bool TestMove_ToDifferentContainer_Succeeds::RunTest(const FString& Parameters)
{
    FItemInstanceIdAllocator::ResetInstanceIdCounter_ForTests();
    UDataTable* Table = MakeDefinitionTable(FIntPoint(1, 1));
    FInventoryContainer Source = FInventoryContainer::MakeEmpty(FIntPoint(2, 2));
    FInventoryContainer Dest = FInventoryContainer::MakeEmpty(FIntPoint(4, 4));
    const FItemInstance Item = MakeItem(Table, FIntPoint(0, 0));
    FInventoryPlacement::TryPlace(Source, Item);

    const EInventoryOperationFailure Result =
        FInventoryOperations::TryMove(Source, Dest, Item.InstanceId, FIntPoint(2, 1), false);

    if (!TestEqual(TEXT("다른 Container 이동이 성공한다"), Result, EInventoryOperationFailure::None))
    {
        return false;
    }
    TestEqual(TEXT("Source에서 Item이 제거된다"), Source.Items.Num(), 0);
    TestEqual(TEXT("Dest에 Item이 추가된다"), Dest.Items.Num(), 1);
    TestEqual(TEXT("Source cache가 비워진다"), Source.OccupancyCache[0], INDEX_NONE);
    TestEqual(TEXT("Dest 목적지가 점유된다"), Dest.OccupancyCache[6], 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    TestMove_ToOccupiedCell_ReturnsOccupied_LeavesBothContainersUnchanged,
    "Duckov.InventoryCore.Move.TestMove_ToOccupiedCell_ReturnsOccupied_LeavesBothContainersUnchanged",
    TestFlags)

bool TestMove_ToOccupiedCell_ReturnsOccupied_LeavesBothContainersUnchanged::RunTest(const FString& Parameters)
{
    FItemInstanceIdAllocator::ResetInstanceIdCounter_ForTests();
    UDataTable* Table = MakeDefinitionTable(FIntPoint(1, 1));
    FInventoryContainer Source = FInventoryContainer::MakeEmpty(FIntPoint(2, 2));
    FInventoryContainer Dest = FInventoryContainer::MakeEmpty(FIntPoint(2, 2));
    const FItemInstance MovingItem = MakeItem(Table, FIntPoint(0, 0));
    FInventoryPlacement::TryPlace(Source, MovingItem);
    FInventoryPlacement::TryPlace(Dest, MakeItem(Table, FIntPoint(1, 0)));
    const FInventoryContainer SourceBefore = Source;
    const FInventoryContainer DestBefore = Dest;

    const EInventoryOperationFailure Result =
        FInventoryOperations::TryMove(Source, Dest, MovingItem.InstanceId, FIntPoint(1, 0), false);

    TestEqual(TEXT("점유된 목적지는 Occupied를 반환한다"), Result, EInventoryOperationFailure::Occupied);
    TestContainerUnchanged(*this, SourceBefore, Source);
    TestContainerUnchanged(*this, DestBefore, Dest);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    TestMove_OutOfBounds_ReturnsNoSpace_LeavesBothContainersUnchanged,
    "Duckov.InventoryCore.Move.TestMove_OutOfBounds_ReturnsNoSpace_LeavesBothContainersUnchanged",
    TestFlags)

bool TestMove_OutOfBounds_ReturnsNoSpace_LeavesBothContainersUnchanged::RunTest(const FString& Parameters)
{
    FItemInstanceIdAllocator::ResetInstanceIdCounter_ForTests();
    UDataTable* Table = MakeDefinitionTable(FIntPoint(2, 2));
    FInventoryContainer Source = FInventoryContainer::MakeEmpty(FIntPoint(3, 3));
    FInventoryContainer Dest = FInventoryContainer::MakeEmpty(FIntPoint(3, 3));
    const FItemInstance Item = MakeItem(Table, FIntPoint(0, 0));
    FInventoryPlacement::TryPlace(Source, Item);
    const FInventoryContainer SourceBefore = Source;
    const FInventoryContainer DestBefore = Dest;

    const EInventoryOperationFailure Result =
        FInventoryOperations::TryMove(Source, Dest, Item.InstanceId, FIntPoint(2, 2), false);

    TestEqual(TEXT("경계를 벗어난 목적지는 NoSpace를 반환한다"), Result, EInventoryOperationFailure::NoSpace);
    TestContainerUnchanged(*this, SourceBefore, Source);
    TestContainerUnchanged(*this, DestBefore, Dest);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    TestMove_UnknownInstanceId_ReturnsItemNotFound_LeavesBothContainersUnchanged,
    "Duckov.InventoryCore.Move.TestMove_UnknownInstanceId_ReturnsItemNotFound_LeavesBothContainersUnchanged",
    TestFlags)

bool TestMove_UnknownInstanceId_ReturnsItemNotFound_LeavesBothContainersUnchanged::RunTest(const FString& Parameters)
{
    FItemInstanceIdAllocator::ResetInstanceIdCounter_ForTests();
    UDataTable* Table = MakeDefinitionTable(FIntPoint(1, 1));
    FInventoryContainer Source = FInventoryContainer::MakeEmpty(FIntPoint(2, 2));
    FInventoryContainer Dest = FInventoryContainer::MakeEmpty(FIntPoint(2, 2));
    FInventoryPlacement::TryPlace(Source, MakeItem(Table, FIntPoint(0, 0)));
    const FInventoryContainer SourceBefore = Source;
    const FInventoryContainer DestBefore = Dest;

    const EInventoryOperationFailure Result =
        FInventoryOperations::TryMove(Source, Dest, 999, FIntPoint(1, 1), false);

    TestEqual(TEXT("없는 InstanceId는 ItemNotFound를 반환한다"), Result, EInventoryOperationFailure::ItemNotFound);
    TestContainerUnchanged(*this, SourceBefore, Source);
    TestContainerUnchanged(*this, DestBefore, Dest);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    TestMove_PreservesInstanceIdAndQuantity,
    "Duckov.InventoryCore.Move.TestMove_PreservesInstanceIdAndQuantity",
    TestFlags)

bool TestMove_PreservesInstanceIdAndQuantity::RunTest(const FString& Parameters)
{
    FItemInstanceIdAllocator::ResetInstanceIdCounter_ForTests();
    UDataTable* Table = MakeDefinitionTable(FIntPoint(1, 1));
    FInventoryContainer Source = FInventoryContainer::MakeEmpty(FIntPoint(2, 2));
    FInventoryContainer Dest = FInventoryContainer::MakeEmpty(FIntPoint(2, 2));
    const FItemInstance Item = MakeItem(Table, FIntPoint(0, 0), false, 7);
    FInventoryPlacement::TryPlace(Source, Item);

    const EInventoryOperationFailure Result =
        FInventoryOperations::TryMove(Source, Dest, Item.InstanceId, FIntPoint(1, 1), false);

    if (!TestEqual(TEXT("이동이 성공한다"), Result, EInventoryOperationFailure::None))
    {
        return false;
    }
    TestEqual(TEXT("InstanceId가 유지된다"), Dest.Items[0].InstanceId, Item.InstanceId);
    TestEqual(TEXT("Quantity가 유지된다"), Dest.Items[0].Quantity, 7);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    TestMove_RotatedDestination_OccupiesRotatedFootprint,
    "Duckov.InventoryCore.Move.TestMove_RotatedDestination_OccupiesRotatedFootprint",
    TestFlags)

bool TestMove_RotatedDestination_OccupiesRotatedFootprint::RunTest(const FString& Parameters)
{
    FItemInstanceIdAllocator::ResetInstanceIdCounter_ForTests();
    UDataTable* Table = MakeDefinitionTable(FIntPoint(2, 1));
    FInventoryContainer Source = FInventoryContainer::MakeEmpty(FIntPoint(3, 3));
    FInventoryContainer Dest = FInventoryContainer::MakeEmpty(FIntPoint(3, 3));
    const FItemInstance Item = MakeItem(Table, FIntPoint(0, 0));
    FInventoryPlacement::TryPlace(Source, Item);

    const EInventoryOperationFailure Result =
        FInventoryOperations::TryMove(Source, Dest, Item.InstanceId, FIntPoint(1, 0), true);

    if (!TestEqual(TEXT("회전 목적지 이동이 성공한다"), Result, EInventoryOperationFailure::None))
    {
        return false;
    }
    TestTrue(TEXT("회전 상태가 저장된다"), Dest.Items[0].bRotated);
    TestEqual(TEXT("회전 footprint 위쪽 셀이 점유된다"), Dest.OccupancyCache[1], 0);
    TestEqual(TEXT("회전 footprint 아래쪽 셀이 점유된다"), Dest.OccupancyCache[4], 0);
    TestEqual(TEXT("원래 가로 방향 셀은 비어 있다"), Dest.OccupancyCache[2], INDEX_NONE);
    return true;
}

#endif
