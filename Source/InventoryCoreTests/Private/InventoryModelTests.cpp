#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "InventoryModel.h"
#include "InventoryOperations.h"
#include "ItemDefinitionRow.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
struct FModelFixture
{
    int32 OriginalCounter = FItemInstanceIdAllocator::GetNextInstanceId();
    TStrongObjectPtr<UDataTable> Table{NewObject<UDataTable>()};
    TStrongObjectPtr<UInventoryModel> Model{NewObject<UInventoryModel>()};
    FInventorySaveRecord Record;
    FModelFixture()
    {
        Table->RowStruct = FItemDefinitionRow::StaticStruct();
        FItemDefinitionRow Definition;
        Definition.Size = FIntPoint(1, 2);
        Definition.bStackable = true;
        Definition.MaxStack = 10;
        Table->AddRow(TEXT("Bar"), Definition);
        Record.NextInstanceId = 10;
        for (int32 Index = 0; Index < 2; ++Index)
        {
            auto& Container = Record.Containers.AddDefaulted_GetRef();
            Container.ContainerId = Index == 0 ? TEXT("A") : TEXT("B");
            Container.GridSize = FIntPoint(4, 4);
            auto& Item = Container.Items.AddDefaulted_GetRef();
            Item.InstanceId = Index + 1;
            Item.DefinitionTable = FSoftObjectPath(Table.Get());
            Item.DefinitionRowName = TEXT("Bar");
            Item.Quantity = 3;
        }
        Model->Load(Record);
    }
    ~FModelFixture() { FItemInstanceIdAllocator::ResetInstanceIdCounter_ForTests(OriginalCounter); }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestModel_QueryMatchesMove, "Duckov.InventoryCore.Query.MatchesMove", TestFlags)
bool TestModel_QueryMatchesMove::RunTest(const FString& Parameters)
{
    FModelFixture F;
    const FInventoryContainer A = *F.Model->FindContainer(TEXT("A"));
    const FInventoryContainer B = *F.Model->FindContainer(TEXT("B"));
    for (const bool bSame : {false, true})
    {
        for (const bool bRotated : {false, true})
        {
            for (const FIntPoint Cell : {FIntPoint(0, 0), FIntPoint(1, 1), FIntPoint(3, 3), FIntPoint(-1, 0), FIntPoint(MAX_int32, 0)})
            {
                FInventoryContainer From = A;
                FInventoryContainer To = B;
                FInventoryContainer& Target = bSame ? From : To;
                const auto Result = FInventoryOperations::CanMove(From, Target, 1, Cell, bRotated);
                TestTrue(TEXT("조회는 source cache 불변"), From.OccupancyCache == A.OccupancyCache);
                TestTrue(TEXT("조회는 target cache 불변"), To.OccupancyCache == B.OccupancyCache);
                TestEqual(TEXT("조회는 원본 좌표 불변"), From.Items[0].AnchorCell, FIntPoint::ZeroValue);
                TestEqual(TEXT("query와 commit 판정 일치"), FInventoryOperations::TryMove(From, Target, 1, Cell, bRotated), Result);
            }
        }
    }
    TestEqual(TEXT("자기 점유 영역 허용"), FInventoryOperations::CanMove(A, A, 1, FIntPoint(0, 0), true), EInventoryOperationFailure::None);
    TestEqual(TEXT("없는 ID"), FInventoryOperations::CanMove(A, B, 999, FIntPoint(1, 1), false), EInventoryOperationFailure::ItemNotFound);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestModel_ChangeSetAndReentry, "Duckov.InventoryCore.Model.ChangeSetAndReentry", TestFlags)
bool TestModel_ChangeSetAndReentry::RunTest(const FString& Parameters)
{
    FModelFixture F;
    int32 Calls = 0;
    FInventoryChangeSet Last;
    F.Model->OnChanged().AddLambda([&](const FInventoryChangeSet& Change)
    {
        ++Calls;
        Last = Change;
        TestNull(TEXT("양쪽 commit 후 source 제거"), F.Model->FindItem(TEXT("A"), 1));
        TestNotNull(TEXT("양쪽 commit 후 target 추가"), F.Model->FindItem(TEXT("B"), 1));
        TestEqual(TEXT("재진입 mutation 거부"), F.Model->TrySort(TEXT("B")), EInventoryOperationFailure::OperationInProgress);
        TestEqual(TEXT("재진입 load 거부"), F.Model->Load(F.Record), EInventorySaveFailure::OperationInProgress);
    });
    TestEqual(TEXT("실패 이동"), F.Model->TryMove(TEXT("A"), TEXT("B"), 1, FIntPoint(0, 0), false), EInventoryOperationFailure::Occupied);
    TestEqual(TEXT("실패 통지 없음"), Calls, 0);
    TestEqual(TEXT("성공 이동"), F.Model->TryMove(TEXT("A"), TEXT("B"), 1, FIntPoint(2, 1), true), EInventoryOperationFailure::None);
    TestEqual(TEXT("한 번 통지"), Calls, 1);
    if (!TestEqual(TEXT("두 Container delta"), Last.Containers.Num(), 2)) { return false; }
    TestTrue(TEXT("제거 ID"), Last.Containers[0].Removed == TArray<int32>{1});
    TestTrue(TEXT("추가 ID"), Last.Containers[1].Added == TArray<int32>{1});
    TestFalse(TEXT("부분 변경"), Last.bReset);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    TestModel_NonTailMoveChangeSetAndSortOrder,
    "Duckov.InventoryCore.Model.NonTailMoveChangeSetAndSortOrder",
    TestFlags)
bool TestModel_NonTailMoveChangeSetAndSortOrder::RunTest(const FString& Parameters)
{
    FModelFixture F;
    F.Record.Containers[0].Items[0].InstanceId = 4;
    FInventoryItemSaveRecord Second = F.Record.Containers[0].Items[0];
    Second.InstanceId = 3;
    Second.AnchorCell = FIntPoint(1, 0);
    F.Record.Containers[0].Items.Add(Second);
    FInventoryItemSaveRecord Third = Second;
    Third.InstanceId = 1;
    Third.AnchorCell = FIntPoint(2, 0);
    F.Record.Containers[0].Items.Add(Third);
    if (!TestEqual(TEXT("세 Item fixture load 성공"), F.Model->Load(F.Record), EInventorySaveFailure::None))
    {
        return false;
    }

    int32 Calls = 0;
    FInventoryChangeSet Last;
    F.Model->OnChanged().AddLambda([&](const FInventoryChangeSet& Change) { ++Calls; Last = Change; });
    const EInventoryOperationFailure MoveResult = F.Model->TryMove(
        TEXT("A"), TEXT("A"), 4, FIntPoint(1, 2), true);
    if (!TestEqual(TEXT("첫 Item의 동일 Container 이동 성공"), MoveResult, EInventoryOperationFailure::None))
    {
        return false;
    }
    const FInventoryContainer* Container = F.Model->FindContainer(TEXT("A"));
    if (!TestNotNull(TEXT("이동 후 Container 존재"), Container)) { return false; }
    if (!TestEqual(TEXT("이동 후 Item 수"), Container->Items.Num(), 3)) { return false; }
    TestEqual(TEXT("이동 후 알림 한 번"), Calls, 1);
    TestTrue(TEXT("이동 후 배열 순서 유지"), TArray<int32>{
        Container->Items[0].InstanceId, Container->Items[1].InstanceId, Container->Items[2].InstanceId
    } == TArray<int32>{4, 3, 1});
    TestEqual(TEXT("이동 후 위치"), Container->Items[0].AnchorCell, FIntPoint(1, 2));
    TestTrue(TEXT("이동 후 회전"), Container->Items[0].bRotated);
    if (!TestEqual(TEXT("이동 ChangeSet은 Container 하나"), Last.Containers.Num(), 1)) { return false; }
    const FInventoryContainerChange& Move = Last.Containers[0];
    TestTrue(TEXT("이동 Item만 Updated"), Move.Updated == TArray<int32>{4});
    TestTrue(TEXT("이동 시 Added 없음"), Move.Added.IsEmpty());
    TestTrue(TEXT("이동 시 Removed 없음"), Move.Removed.IsEmpty());
    TestFalse(TEXT("이동 시 순서 변경 없음"), Move.bOrderChanged);
    TestFalse(TEXT("이동 시 GridSize 변경 없음"), Move.bGridSizeChanged);
    TestFalse(TEXT("이동 시 Reset 없음"), Last.bReset);

    TestEqual(TEXT("명시적 Sort 성공"), F.Model->TrySort(TEXT("A")), EInventoryOperationFailure::None);
    Container = F.Model->FindContainer(TEXT("A"));
    if (!TestNotNull(TEXT("Sort 후 Container 존재"), Container)) { return false; }
    if (!TestEqual(TEXT("Sort 후 Item 수"), Container->Items.Num(), 3)) { return false; }
    TestEqual(TEXT("Sort 알림 추가"), Calls, 2);
    TestTrue(TEXT("Sort가 ID 순서 변경"), TArray<int32>{
        Container->Items[0].InstanceId, Container->Items[1].InstanceId, Container->Items[2].InstanceId
    } == TArray<int32>{1, 3, 4});
    if (!TestEqual(TEXT("Sort ChangeSet은 Container 하나"), Last.Containers.Num(), 1)) { return false; }
    TestTrue(TEXT("Sort 순서 변경 알림"), Last.Containers[0].bOrderChanged);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestModel_SortResizeStackReset, "Duckov.InventoryCore.Model.SortResizeStackReset", TestFlags)
bool TestModel_SortResizeStackReset::RunTest(const FString& Parameters)
{
    FModelFixture F;
    int32 Calls = 0;
    FInventoryChangeSet Last;
    F.Model->OnChanged().AddLambda([&](const auto& Change) { ++Calls; Last = Change; });
    TestEqual(TEXT("동일 크기"), F.Model->TryResize(TEXT("A"), FIntPoint(4, 4)), EInventoryOperationFailure::None);
    TestEqual(TEXT("동일 정렬"), F.Model->TrySort(TEXT("A")), EInventoryOperationFailure::None);
    TestEqual(TEXT("no-op 통지 없음"), Calls, 0);
    F.Model->TryResize(TEXT("A"), FIntPoint(5, 5));
    TestTrue(TEXT("resize delta"), Calls == 1 && Last.Containers[0].bGridSizeChanged);
    F.Model->TryStack(TEXT("A"), TEXT("B"), 1, 2);
    TestTrue(TEXT("병합 원본 제거"), Last.Containers[0].Removed == TArray<int32>{1});
    TestTrue(TEXT("병합 대상 갱신"), Last.Containers[1].Updated == TArray<int32>{2});
    TestEqual(TEXT("전체 load"), F.Model->Load(F.Record), EInventorySaveFailure::None);
    TestTrue(TEXT("reset 통지와 구독 유지"), Calls == 3 && Last.bReset);
    auto Invalid = F.Record;
    Invalid.FormatVersion = 99;
    TestEqual(TEXT("load 실패"), F.Model->Load(Invalid), EInventorySaveFailure::UnsupportedVersion);
    TestEqual(TEXT("실패 load 통지 없음"), Calls, 3);
    TestEqual(TEXT("없는 Container"), F.Model->TrySort(TEXT("Missing")), EInventoryOperationFailure::InvalidContainer);
    return true;
}
#endif
