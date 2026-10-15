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

UDataTable* MakeDefinitionTable(
    const FIntPoint Size,
    const bool bStackable = false,
    const int32 MaxStack = 1)
{
    UDataTable* Table = NewObject<UDataTable>();
    Table->RowStruct = FItemDefinitionRow::StaticStruct();

    FItemDefinitionRow Row;
    Row.Size = Size;
    Row.bStackable = bStackable;
    Row.MaxStack = MaxStack;
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
    TestStack_SameDefinition_PartialMerge_MovesMinOfSourceAndRemainingCapacity,
    "Duckov.InventoryCore.Stack.TestStack_SameDefinition_PartialMerge_MovesMinOfSourceAndRemainingCapacity",
    TestFlags)

bool TestStack_SameDefinition_PartialMerge_MovesMinOfSourceAndRemainingCapacity::RunTest(const FString& Parameters)
{
    FItemInstanceIdAllocator::ResetInstanceIdCounter_ForTests();
    UDataTable* Table = MakeDefinitionTable(FIntPoint(1, 1), true, 10);
    FInventoryContainer Source = FInventoryContainer::MakeEmpty(FIntPoint(2, 1));
    FInventoryContainer Dest = FInventoryContainer::MakeEmpty(FIntPoint(2, 1));
    const FItemInstance SourceItem = MakeItem(Table, FIntPoint(0, 0), false, 7);
    const FItemInstance TargetItem = MakeItem(Table, FIntPoint(0, 0), false, 6);
    FInventoryPlacement::TryPlace(Source, SourceItem);
    FInventoryPlacement::TryPlace(Dest, TargetItem);

    const EInventoryOperationFailure Result =
        FInventoryOperations::TryStack(Source, Dest, SourceItem.InstanceId, TargetItem.InstanceId);

    if (!TestEqual(TEXT("부분 병합은 성공한다"), Result, EInventoryOperationFailure::None))
    {
        return false;
    }
    TestEqual(TEXT("Source는 남은 수량을 유지한다"), Source.Items.Num(), 1);
    TestEqual(TEXT("Source 수량은 여유 공간만큼 감소한다"), Source.Items[0].Quantity, 3);
    TestEqual(TEXT("Target 수량은 최대치가 된다"), Dest.Items[0].Quantity, 10);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    TestStack_SameDefinition_FullMerge_RemovesSourceFromContainer,
    "Duckov.InventoryCore.Stack.TestStack_SameDefinition_FullMerge_RemovesSourceFromContainer",
    TestFlags)

bool TestStack_SameDefinition_FullMerge_RemovesSourceFromContainer::RunTest(const FString& Parameters)
{
    FItemInstanceIdAllocator::ResetInstanceIdCounter_ForTests();
    UDataTable* Table = MakeDefinitionTable(FIntPoint(1, 1), true, 10);
    FInventoryContainer Source = FInventoryContainer::MakeEmpty(FIntPoint(2, 1));
    FInventoryContainer Dest = FInventoryContainer::MakeEmpty(FIntPoint(2, 1));
    const FItemInstance SourceItem = MakeItem(Table, FIntPoint(0, 0), false, 4);
    const FItemInstance TargetItem = MakeItem(Table, FIntPoint(0, 0), false, 6);
    FInventoryPlacement::TryPlace(Source, SourceItem);
    FInventoryPlacement::TryPlace(Dest, TargetItem);

    const EInventoryOperationFailure Result =
        FInventoryOperations::TryStack(Source, Dest, SourceItem.InstanceId, TargetItem.InstanceId);

    if (!TestEqual(TEXT("전량 병합은 성공한다"), Result, EInventoryOperationFailure::None))
    {
        return false;
    }
    TestEqual(TEXT("Source에서 Item이 제거된다"), Source.Items.Num(), 0);
    TestEqual(TEXT("Target이 Source 수량을 흡수한다"), Dest.Items[0].Quantity, 10);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    TestStack_TargetAtMaxStack_ReturnsStackFull_LeavesBothContainersUnchanged,
    "Duckov.InventoryCore.Stack.TestStack_TargetAtMaxStack_ReturnsStackFull_LeavesBothContainersUnchanged",
    TestFlags)

bool TestStack_TargetAtMaxStack_ReturnsStackFull_LeavesBothContainersUnchanged::RunTest(const FString& Parameters)
{
    FItemInstanceIdAllocator::ResetInstanceIdCounter_ForTests();
    UDataTable* Table = MakeDefinitionTable(FIntPoint(1, 1), true, 10);
    FInventoryContainer Source = FInventoryContainer::MakeEmpty(FIntPoint(2, 1));
    FInventoryContainer Dest = FInventoryContainer::MakeEmpty(FIntPoint(2, 1));
    const FItemInstance SourceItem = MakeItem(Table, FIntPoint(0, 0), false, 3);
    const FItemInstance TargetItem = MakeItem(Table, FIntPoint(0, 0), false, 10);
    FInventoryPlacement::TryPlace(Source, SourceItem);
    FInventoryPlacement::TryPlace(Dest, TargetItem);
    const FInventoryContainer SourceBefore = Source;
    const FInventoryContainer DestBefore = Dest;

    const EInventoryOperationFailure Result =
        FInventoryOperations::TryStack(Source, Dest, SourceItem.InstanceId, TargetItem.InstanceId);

    TestEqual(TEXT("최대 수량 Target은 StackFull을 반환한다"), Result, EInventoryOperationFailure::StackFull);
    TestContainerUnchanged(*this, SourceBefore, Source);
    TestContainerUnchanged(*this, DestBefore, Dest);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    TestStack_DifferentDefinition_ReturnsStackMismatch_LeavesBothContainersUnchanged,
    "Duckov.InventoryCore.Stack.TestStack_DifferentDefinition_ReturnsStackMismatch_LeavesBothContainersUnchanged",
    TestFlags)

bool TestStack_DifferentDefinition_ReturnsStackMismatch_LeavesBothContainersUnchanged::RunTest(const FString& Parameters)
{
    FItemInstanceIdAllocator::ResetInstanceIdCounter_ForTests();
    UDataTable* SourceTable = MakeDefinitionTable(FIntPoint(1, 1), true, 10);
    UDataTable* TargetTable = MakeDefinitionTable(FIntPoint(1, 1), true, 10);
    FInventoryContainer Source = FInventoryContainer::MakeEmpty(FIntPoint(2, 1));
    FInventoryContainer Dest = FInventoryContainer::MakeEmpty(FIntPoint(2, 1));
    const FItemInstance SourceItem = MakeItem(SourceTable, FIntPoint(0, 0), false, 3);
    const FItemInstance TargetItem = MakeItem(TargetTable, FIntPoint(0, 0), false, 4);
    FInventoryPlacement::TryPlace(Source, SourceItem);
    FInventoryPlacement::TryPlace(Dest, TargetItem);
    const FInventoryContainer SourceBefore = Source;
    const FInventoryContainer DestBefore = Dest;

    const EInventoryOperationFailure Result =
        FInventoryOperations::TryStack(Source, Dest, SourceItem.InstanceId, TargetItem.InstanceId);

    TestEqual(TEXT("서로 다른 Definition은 StackMismatch를 반환한다"), Result, EInventoryOperationFailure::StackMismatch);
    TestContainerUnchanged(*this, SourceBefore, Source);
    TestContainerUnchanged(*this, DestBefore, Dest);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    TestStack_NonStackableDefinition_ReturnsStackMismatch_LeavesBothContainersUnchanged,
    "Duckov.InventoryCore.Stack.TestStack_NonStackableDefinition_ReturnsStackMismatch_LeavesBothContainersUnchanged",
    TestFlags)

bool TestStack_NonStackableDefinition_ReturnsStackMismatch_LeavesBothContainersUnchanged::RunTest(const FString& Parameters)
{
    FItemInstanceIdAllocator::ResetInstanceIdCounter_ForTests();
    UDataTable* Table = MakeDefinitionTable(FIntPoint(1, 1), false, 10);
    FInventoryContainer Source = FInventoryContainer::MakeEmpty(FIntPoint(2, 1));
    FInventoryContainer Dest = FInventoryContainer::MakeEmpty(FIntPoint(2, 1));
    const FItemInstance SourceItem = MakeItem(Table, FIntPoint(0, 0), false, 3);
    const FItemInstance TargetItem = MakeItem(Table, FIntPoint(0, 0), false, 4);
    FInventoryPlacement::TryPlace(Source, SourceItem);
    FInventoryPlacement::TryPlace(Dest, TargetItem);
    const FInventoryContainer SourceBefore = Source;
    const FInventoryContainer DestBefore = Dest;

    const EInventoryOperationFailure Result =
        FInventoryOperations::TryStack(Source, Dest, SourceItem.InstanceId, TargetItem.InstanceId);

    TestEqual(TEXT("Non-stackable Definition은 StackMismatch를 반환한다"), Result, EInventoryOperationFailure::StackMismatch);
    TestContainerUnchanged(*this, SourceBefore, Source);
    TestContainerUnchanged(*this, DestBefore, Dest);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    TestStack_UnknownSourceInstanceId_ReturnsItemNotFound_LeavesBothContainersUnchanged,
    "Duckov.InventoryCore.Stack.TestStack_UnknownSourceInstanceId_ReturnsItemNotFound_LeavesBothContainersUnchanged",
    TestFlags)

bool TestStack_UnknownSourceInstanceId_ReturnsItemNotFound_LeavesBothContainersUnchanged::RunTest(const FString& Parameters)
{
    FItemInstanceIdAllocator::ResetInstanceIdCounter_ForTests();
    UDataTable* Table = MakeDefinitionTable(FIntPoint(1, 1), true, 10);
    FInventoryContainer Source = FInventoryContainer::MakeEmpty(FIntPoint(2, 1));
    FInventoryContainer Dest = FInventoryContainer::MakeEmpty(FIntPoint(2, 1));
    const FItemInstance SourceItem = MakeItem(Table, FIntPoint(0, 0), false, 3);
    const FItemInstance TargetItem = MakeItem(Table, FIntPoint(0, 0), false, 4);
    FInventoryPlacement::TryPlace(Source, SourceItem);
    FInventoryPlacement::TryPlace(Dest, TargetItem);
    const FInventoryContainer SourceBefore = Source;
    const FInventoryContainer DestBefore = Dest;

    const EInventoryOperationFailure Result =
        FInventoryOperations::TryStack(Source, Dest, 999, TargetItem.InstanceId);

    TestEqual(TEXT("없는 Source InstanceId는 ItemNotFound를 반환한다"), Result, EInventoryOperationFailure::ItemNotFound);
    TestContainerUnchanged(*this, SourceBefore, Source);
    TestContainerUnchanged(*this, DestBefore, Dest);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    TestStack_UnknownTargetInstanceId_ReturnsItemNotFound_LeavesBothContainersUnchanged,
    "Duckov.InventoryCore.Stack.TestStack_UnknownTargetInstanceId_ReturnsItemNotFound_LeavesBothContainersUnchanged",
    TestFlags)

bool TestStack_UnknownTargetInstanceId_ReturnsItemNotFound_LeavesBothContainersUnchanged::RunTest(const FString& Parameters)
{
    FItemInstanceIdAllocator::ResetInstanceIdCounter_ForTests();
    UDataTable* Table = MakeDefinitionTable(FIntPoint(1, 1), true, 10);
    FInventoryContainer Source = FInventoryContainer::MakeEmpty(FIntPoint(2, 1));
    FInventoryContainer Dest = FInventoryContainer::MakeEmpty(FIntPoint(2, 1));
    const FItemInstance SourceItem = MakeItem(Table, FIntPoint(0, 0), false, 3);
    FInventoryPlacement::TryPlace(Source, SourceItem);
    const FInventoryContainer SourceBefore = Source;
    const FInventoryContainer DestBefore = Dest;

    const EInventoryOperationFailure Result =
        FInventoryOperations::TryStack(Source, Dest, SourceItem.InstanceId, 999);

    TestEqual(TEXT("없는 Target InstanceId는 ItemNotFound를 반환한다"), Result, EInventoryOperationFailure::ItemNotFound);
    TestContainerUnchanged(*this, SourceBefore, Source);
    TestContainerUnchanged(*this, DestBefore, Dest);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    TestStack_WithinSameContainer_Succeeds,
    "Duckov.InventoryCore.Stack.TestStack_WithinSameContainer_Succeeds",
    TestFlags)

bool TestStack_WithinSameContainer_Succeeds::RunTest(const FString& Parameters)
{
    FItemInstanceIdAllocator::ResetInstanceIdCounter_ForTests();
    UDataTable* Table = MakeDefinitionTable(FIntPoint(1, 1), true, 10);
    FInventoryContainer Container = FInventoryContainer::MakeEmpty(FIntPoint(3, 1));
    const FItemInstance SourceItem = MakeItem(Table, FIntPoint(0, 0), false, 4);
    const FItemInstance TargetItem = MakeItem(Table, FIntPoint(2, 0), false, 6);
    FInventoryPlacement::TryPlace(Container, SourceItem);
    FInventoryPlacement::TryPlace(Container, TargetItem);

    const EInventoryOperationFailure Result =
        FInventoryOperations::TryStack(Container, Container, SourceItem.InstanceId, TargetItem.InstanceId);

    if (!TestEqual(TEXT("같은 Container 병합은 성공한다"), Result, EInventoryOperationFailure::None))
    {
        return false;
    }
    TestEqual(TEXT("같은 Container에서 Source가 제거된다"), Container.Items.Num(), 1);
    TestEqual(TEXT("Target이 남은 Item이 된다"), Container.Items[0].InstanceId, TargetItem.InstanceId);
    TestEqual(TEXT("Target 수량이 병합된다"), Container.Items[0].Quantity, 10);
    TestEqual(TEXT("Source 위치의 cache가 비워진다"), Container.OccupancyCache[0], INDEX_NONE);
    TestEqual(TEXT("Target 위치의 cache가 유지된다"), Container.OccupancyCache[2], 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    TestStack_ToDifferentContainer_Succeeds,
    "Duckov.InventoryCore.Stack.TestStack_ToDifferentContainer_Succeeds",
    TestFlags)

bool TestStack_ToDifferentContainer_Succeeds::RunTest(const FString& Parameters)
{
    FItemInstanceIdAllocator::ResetInstanceIdCounter_ForTests();
    UDataTable* Table = MakeDefinitionTable(FIntPoint(1, 1), true, 10);
    FInventoryContainer Source = FInventoryContainer::MakeEmpty(FIntPoint(2, 1));
    FInventoryContainer Dest = FInventoryContainer::MakeEmpty(FIntPoint(2, 1));
    const FItemInstance SourceItem = MakeItem(Table, FIntPoint(0, 0), false, 4);
    const FItemInstance TargetItem = MakeItem(Table, FIntPoint(1, 0), false, 6);
    FInventoryPlacement::TryPlace(Source, SourceItem);
    FInventoryPlacement::TryPlace(Dest, TargetItem);

    const EInventoryOperationFailure Result =
        FInventoryOperations::TryStack(Source, Dest, SourceItem.InstanceId, TargetItem.InstanceId);

    if (!TestEqual(TEXT("다른 Container 병합은 성공한다"), Result, EInventoryOperationFailure::None))
    {
        return false;
    }
    TestEqual(TEXT("Source에서 Item이 제거된다"), Source.Items.Num(), 0);
    TestEqual(TEXT("Dest Target 수량이 병합된다"), Dest.Items[0].Quantity, 10);
    TestEqual(TEXT("Dest Target의 위치가 유지된다"), Dest.Items[0].AnchorCell, FIntPoint(1, 0));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    TestStack_CanStack_MatchesTryStackWithoutChange,
    "Duckov.InventoryCore.Stack.TestStack_CanStack_MatchesTryStackWithoutChange",
    TestFlags)

bool TestStack_CanStack_MatchesTryStackWithoutChange::RunTest(const FString& Parameters)
{
    FItemInstanceIdAllocator::ResetInstanceIdCounter_ForTests();
    UDataTable* Stackable = MakeDefinitionTable(FIntPoint(1, 1), true, 10);
    UDataTable* Single = MakeDefinitionTable(FIntPoint(1, 1));
    FInventoryContainer Container = FInventoryContainer::MakeEmpty(FIntPoint(4, 1));
    const FItemInstance Source = MakeItem(Stackable, FIntPoint(0, 0), false, 4);
    const FItemInstance Target = MakeItem(Stackable, FIntPoint(1, 0), false, 6);
    const FItemInstance Full = MakeItem(Stackable, FIntPoint(2, 0), false, 10);
    const FItemInstance Other = MakeItem(Single, FIntPoint(3, 0));
    for (const FItemInstance& Item : {Source, Target, Full, Other}) { FInventoryPlacement::TryPlace(Container, Item); }
    const FInventoryContainer Before = Container;

    TestEqual(TEXT("병합 가능"), FInventoryOperations::CanStack(Container, Container, Source.InstanceId, Target.InstanceId),
        EInventoryOperationFailure::None);
    TestEqual(TEXT("가득 찬 대상"), FInventoryOperations::CanStack(Container, Container, Source.InstanceId, Full.InstanceId),
        EInventoryOperationFailure::StackFull);
    TestEqual(TEXT("다른 정의"), FInventoryOperations::CanStack(Container, Container, Source.InstanceId, Other.InstanceId),
        EInventoryOperationFailure::StackMismatch);
    TestEqual(TEXT("없는 대상"), FInventoryOperations::CanStack(Container, Container, Source.InstanceId, 999),
        EInventoryOperationFailure::ItemNotFound);
    TestContainerUnchanged(*this, Before, Container);
    TestEqual(TEXT("TryStack도 같은 검증으로 실패"), FInventoryOperations::TryStack(Container, Container, Source.InstanceId, Full.InstanceId),
        EInventoryOperationFailure::StackFull);
    TestContainerUnchanged(*this, Before, Container);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    TestStack_FindSingleOverlap_ReturnsOnlyLoneOccupant,
    "Duckov.InventoryCore.Stack.TestStack_FindSingleOverlap_ReturnsOnlyLoneOccupant",
    TestFlags)

bool TestStack_FindSingleOverlap_ReturnsOnlyLoneOccupant::RunTest(const FString& Parameters)
{
    FItemInstanceIdAllocator::ResetInstanceIdCounter_ForTests();
    UDataTable* Small = MakeDefinitionTable(FIntPoint(1, 1));
    UDataTable* Wide = MakeDefinitionTable(FIntPoint(2, 1));
    FInventoryContainer Source = FInventoryContainer::MakeEmpty(FIntPoint(2, 1));
    FInventoryContainer Dest = FInventoryContainer::MakeEmpty(FIntPoint(5, 1));
    const FItemInstance Dragged = MakeItem(Wide, FIntPoint(0, 0));
    const FItemInstance First = MakeItem(Wide, FIntPoint(1, 0));
    const FItemInstance Second = MakeItem(Small, FIntPoint(3, 0));
    FInventoryPlacement::TryPlace(Source, Dragged);
    FInventoryPlacement::TryPlace(Dest, First);
    FInventoryPlacement::TryPlace(Dest, Second);
    const FInventoryContainer SourceBefore = Source;
    const FInventoryContainer DestBefore = Dest;

    TestEqual(TEXT("한 항목과만 겹침"), FInventoryOperations::FindSingleOverlap(Source, Dest, Dragged.InstanceId, FIntPoint(0, 0), false),
        First.InstanceId);
    TestEqual(TEXT("두 항목과 겹치면 없음"), FInventoryOperations::FindSingleOverlap(Source, Dest, Dragged.InstanceId, FIntPoint(2, 0), false),
        INDEX_NONE);
    TestEqual(TEXT("격자 밖이면 없음"), FInventoryOperations::FindSingleOverlap(Source, Dest, Dragged.InstanceId, FIntPoint(4, 0), false),
        INDEX_NONE);
    TestEqual(TEXT("회전 footprint 사용(1x2는 높이 1 격자 밖)"), FInventoryOperations::FindSingleOverlap(Source, Dest, Dragged.InstanceId, FIntPoint(0, 0), true),
        INDEX_NONE);

    FInventoryContainer Same = FInventoryContainer::MakeEmpty(FIntPoint(4, 1));
    const FItemInstance Moving = MakeItem(Small, FIntPoint(0, 0));
    const FItemInstance Neighbor = MakeItem(Small, FIntPoint(2, 0));
    FInventoryPlacement::TryPlace(Same, Moving);
    FInventoryPlacement::TryPlace(Same, Neighbor);
    TestEqual(TEXT("같은 컨테이너의 자기 칸은 겹침 아님"), FInventoryOperations::FindSingleOverlap(Same, Same, Moving.InstanceId, FIntPoint(0, 0), false),
        INDEX_NONE);
    TestEqual(TEXT("빈 칸은 겹침 아님"), FInventoryOperations::FindSingleOverlap(Same, Same, Moving.InstanceId, FIntPoint(1, 0), false),
        INDEX_NONE);
    TestEqual(TEXT("같은 컨테이너의 다른 항목"), FInventoryOperations::FindSingleOverlap(Same, Same, Moving.InstanceId, FIntPoint(2, 0), false),
        Neighbor.InstanceId);
    TestEqual(TEXT("없는 항목"), FInventoryOperations::FindSingleOverlap(Source, Dest, 999, FIntPoint(0, 0), false), INDEX_NONE);
    TestContainerUnchanged(*this, SourceBefore, Source);
    TestContainerUnchanged(*this, DestBefore, Dest);
    return true;
}

#endif
