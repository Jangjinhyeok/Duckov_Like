#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "InventoryModel.h"
#include "ItemDefinitionRow.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

struct FAddFixture
{
    int32 OriginalCounter = FItemInstanceIdAllocator::GetNextInstanceId();
    TStrongObjectPtr<UDataTable> Table{NewObject<UDataTable>()};
    TStrongObjectPtr<UInventoryModel> Model{NewObject<UInventoryModel>()};
    FInventorySaveRecord Record;

    FAddFixture(FIntPoint Size = FIntPoint(4, 3))
    {
        Table->RowStruct = FItemDefinitionRow::StaticStruct();
        FItemDefinitionRow Unit;
        Table->AddRow(TEXT("Unit"), Unit);
        FItemDefinitionRow Wide;
        Wide.Size = FIntPoint(2, 1);
        Table->AddRow(TEXT("Wide"), Wide);
        FItemDefinitionRow Tall;
        Tall.Size = FIntPoint(1, 2);
        Table->AddRow(TEXT("Tall"), Tall);
        FItemDefinitionRow Stack;
        Stack.bStackable = true;
        Stack.MaxStack = 5;
        Table->AddRow(TEXT("Stack"), Stack);
        FItemDefinitionRow Invalid;
        Invalid.Size = FIntPoint(0, 1);
        Table->AddRow(TEXT("Invalid"), Invalid);
        Record.NextInstanceId = 10;
        for (const FName Id : {FName(TEXT("Bag")), FName(TEXT("Stash"))})
        {
            FInventoryContainerSaveRecord& Container = Record.Containers.AddDefaulted_GetRef();
            Container.ContainerId = Id;
            Container.GridSize = Size;
        }
        Model->Load(Record);
    }

    ~FAddFixture() { FItemInstanceIdAllocator::ResetInstanceIdCounter_ForTests(OriginalCounter); }

    TSoftObjectPtr<UDataTable> DefinitionTable() const
    {
        return TSoftObjectPtr<UDataTable>(FSoftObjectPath(Table.Get()));
    }

    void AddSaved(FName ContainerId, int32 Id, FIntPoint Anchor)
    {
        FInventoryContainerSaveRecord* Container = Record.Containers.FindByPredicate(
            [ContainerId](const FInventoryContainerSaveRecord& Entry) { return Entry.ContainerId == ContainerId; });
        FInventoryItemSaveRecord& Item = Container->Items.AddDefaulted_GetRef();
        Item.InstanceId = Id;
        Item.DefinitionTable = FSoftObjectPath(Table.Get());
        Item.DefinitionRowName = TEXT("Unit");
        Item.AnchorCell = Anchor;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestModel_AddSuccessAndChangeSet,
    "Duckov.InventoryCore.Model.Add.SuccessAndChangeSet", TestFlags)
bool TestModel_AddSuccessAndChangeSet::RunTest(const FString& Parameters)
{
    FAddFixture F;
    const int32 ExpectedId = FItemInstanceIdAllocator::GetNextInstanceId();
    int32 Calls = 0;
    FInventoryChangeSet Last;
    F.Model->OnChanged().AddLambda([&](const FInventoryChangeSet& Change)
    {
        ++Calls;
        Last = Change;
        TestEqual(TEXT("알림 중 재진입 거부"),
            F.Model->TryAdd(TEXT("Bag"), F.DefinitionTable(), TEXT("Unit"), 1),
            EInventoryOperationFailure::OperationInProgress);
        const FInventoryContainer* Bag = F.Model->FindContainer(TEXT("Bag"));
        TestEqual(TEXT("알림 시점에 배치 완료"), Bag->Items.Num(), 1);
    });
    TestEqual(TEXT("추가 성공"), F.Model->TryAdd(TEXT("Bag"), F.DefinitionTable(), TEXT("Stack"), 3),
        EInventoryOperationFailure::None);
    const FInventoryContainer* Bag = F.Model->FindContainer(TEXT("Bag"));
    if (!TestEqual(TEXT("한 항목 추가"), Bag->Items.Num(), 1)) { return false; }
    const FItemInstance& Item = Bag->Items[0];
    TestEqual(TEXT("할당된 ID"), Item.InstanceId, ExpectedId);
    TestEqual(TEXT("수량"), Item.Quantity, 3);
    TestEqual(TEXT("첫 위치"), Item.AnchorCell, FIntPoint(0, 0));
    TestFalse(TEXT("회전하지 않음"), Item.bRotated);
    TestEqual(TEXT("ID 카운터"), FItemInstanceIdAllocator::GetNextInstanceId(), ExpectedId + 1);
    TestEqual(TEXT("알림 한 번"), Calls, 1);
    if (!TestEqual(TEXT("변경 Container 하나"), Last.Containers.Num(), 1)) { return false; }
    TestEqual(TEXT("Bag 변경"), Last.Containers[0].ContainerId, FName(TEXT("Bag")));
    TestTrue(TEXT("Added ID 하나"), Last.Containers[0].Added == TArray<int32>{ExpectedId});
    TestTrue(TEXT("Removed 없음"), Last.Containers[0].Removed.IsEmpty());
    TestTrue(TEXT("Updated 없음"), Last.Containers[0].Updated.IsEmpty());
    TestFalse(TEXT("Reset 아님"), Last.bReset);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestModel_AddFirstFit,
    "Duckov.InventoryCore.Model.Add.FirstFit", TestFlags)
bool TestModel_AddFirstFit::RunTest(const FString& Parameters)
{
    FAddFixture F(FIntPoint(3, 2));
    F.AddSaved(TEXT("Bag"), 1, FIntPoint(0, 0));
    F.AddSaved(TEXT("Bag"), 2, FIntPoint(2, 0));
    TestEqual(TEXT("초기 데이터 로드"), F.Model->Load(F.Record), EInventorySaveFailure::None);
    TestEqual(TEXT("가로 항목 추가"), F.Model->TryAdd(TEXT("Bag"), F.DefinitionTable(), TEXT("Wide"), 1),
        EInventoryOperationFailure::None);
    const FInventoryContainer* Bag = F.Model->FindContainer(TEXT("Bag"));
    if (!TestEqual(TEXT("항목 수"), Bag->Items.Num(), 3)) { return false; }
    TestEqual(TEXT("첫 행 빈 칸을 건너 다음 행 첫 위치"), Bag->Items.Last().AnchorCell, FIntPoint(0, 1));
    TestFalse(TEXT("기본 방향"), Bag->Items.Last().bRotated);
    TestEqual(TEXT("기존 순서 보존"), Bag->Items[0].InstanceId, 1);
    TestEqual(TEXT("기존 순서 보존"), Bag->Items[1].InstanceId, 2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestModel_AddFailuresPreserveState,
    "Duckov.InventoryCore.Model.Add.FailuresPreserveState", TestFlags)
bool TestModel_AddFailuresPreserveState::RunTest(const FString& Parameters)
{
    FAddFixture F(FIntPoint(2, 1));
    F.AddSaved(TEXT("Bag"), 1, FIntPoint(0, 0));
    TestEqual(TEXT("초기 데이터 로드"), F.Model->Load(F.Record), EInventorySaveFailure::None);
    const FInventoryContainer Before = *F.Model->FindContainer(TEXT("Bag"));
    const int32 InitialCounter = FItemInstanceIdAllocator::GetNextInstanceId();
    int32 Calls = 0;
    F.Model->OnChanged().AddLambda([&](const FInventoryChangeSet&) { ++Calls; });
    auto CheckPreserved = [&]()
    {
        const FInventoryContainer* Bag = F.Model->FindContainer(TEXT("Bag"));
        TestEqual(TEXT("항목 수 보존"), Bag->Items.Num(), Before.Items.Num());
        TestEqual(TEXT("기존 ID 보존"), Bag->Items[0].InstanceId, Before.Items[0].InstanceId);
        TestTrue(TEXT("점유 캐시 보존"), Bag->OccupancyCache == Before.OccupancyCache);
        TestEqual(TEXT("ID 카운터 보존"), FItemInstanceIdAllocator::GetNextInstanceId(), InitialCounter);
        TestEqual(TEXT("실패 알림 없음"), Calls, 0);
    };
    TestEqual(TEXT("회전해도 자동 배치하지 않음"),
        F.Model->TryAdd(TEXT("Bag"), F.DefinitionTable(), TEXT("Tall"), 1), EInventoryOperationFailure::NoSpace);
    CheckPreserved();
    TestEqual(TEXT("없는 행"),
        F.Model->TryAdd(TEXT("Bag"), F.DefinitionTable(), TEXT("Missing"), 1), EInventoryOperationFailure::InvalidDefinition);
    CheckPreserved();
    TestEqual(TEXT("유효하지 않은 크기"),
        F.Model->TryAdd(TEXT("Bag"), F.DefinitionTable(), TEXT("Invalid"), 1), EInventoryOperationFailure::InvalidDefinition);
    CheckPreserved();
    TestEqual(TEXT("비어 있는 테이블"),
        F.Model->TryAdd(TEXT("Bag"), {}, TEXT("Unit"), 1), EInventoryOperationFailure::InvalidDefinition);
    CheckPreserved();
    TStrongObjectPtr<UDataTable> WrongTable{NewObject<UDataTable>()};
    WrongTable->RowStruct = FTableRowBase::StaticStruct();
    TestEqual(TEXT("잘못된 RowStruct"),
        F.Model->TryAdd(TEXT("Bag"), TSoftObjectPtr<UDataTable>(FSoftObjectPath(WrongTable.Get())), TEXT("Unit"), 1),
        EInventoryOperationFailure::InvalidDefinition);
    CheckPreserved();
    for (const int32 Quantity : {0, -1, 2})
    {
        TestEqual(TEXT("비적재 수량 거부"),
            F.Model->TryAdd(TEXT("Bag"), F.DefinitionTable(), TEXT("Unit"), Quantity),
            EInventoryOperationFailure::InvalidQuantity);
        CheckPreserved();
    }
    TestEqual(TEXT("최대 적재량 초과"),
        F.Model->TryAdd(TEXT("Bag"), F.DefinitionTable(), TEXT("Stack"), 6),
        EInventoryOperationFailure::InvalidQuantity);
    CheckPreserved();
    TestEqual(TEXT("없는 Container"),
        F.Model->TryAdd(TEXT("Missing"), F.DefinitionTable(), TEXT("Unit"), 1),
        EInventoryOperationFailure::InvalidContainer);
    CheckPreserved();
    TestEqual(TEXT("점유로 인한 공간 부족"),
        F.Model->TryAdd(TEXT("Bag"), F.DefinitionTable(), TEXT("Wide"), 1), EInventoryOperationFailure::NoSpace);
    CheckPreserved();
    TestEqual(TEXT("빈 Stash에서도 자동 회전하지 않음"),
        F.Model->TryAdd(TEXT("Stash"), F.DefinitionTable(), TEXT("Tall"), 1), EInventoryOperationFailure::NoSpace);
    TestTrue(TEXT("회전하면 들어갈 빈 Stash 보존"), F.Model->FindContainer(TEXT("Stash"))->Items.IsEmpty());
    CheckPreserved();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestModel_AddUniquenessAndExhaustion,
    "Duckov.InventoryCore.Model.Add.UniquenessAndExhaustion", TestFlags)
bool TestModel_AddUniquenessAndExhaustion::RunTest(const FString& Parameters)
{
    FAddFixture F;
    F.AddSaved(TEXT("Stash"), 9, FIntPoint(0, 0));
    TestEqual(TEXT("기존 Stash 로드"), F.Model->Load(F.Record), EInventorySaveFailure::None);
    const int32 ExpectedId = FItemInstanceIdAllocator::GetNextInstanceId();
    TestEqual(TEXT("Bag 추가"), F.Model->TryAdd(TEXT("Bag"), F.DefinitionTable(), TEXT("Unit"), 1),
        EInventoryOperationFailure::None);
    TestEqual(TEXT("Stash 추가"), F.Model->TryAdd(TEXT("Stash"), F.DefinitionTable(), TEXT("Unit"), 1),
        EInventoryOperationFailure::None);
    TestEqual(TEXT("Bag ID"), F.Model->FindContainer(TEXT("Bag"))->Items[0].InstanceId, ExpectedId);
    TestEqual(TEXT("Stash ID"), F.Model->FindContainer(TEXT("Stash"))->Items[1].InstanceId, ExpectedId + 1);
    FItemInstanceIdAllocator::ResetInstanceIdCounter_ForTests(MAX_int32);
    const FInventoryContainer Before = *F.Model->FindContainer(TEXT("Bag"));
    int32 Calls = 0;
    F.Model->OnChanged().AddLambda([&](const FInventoryChangeSet&) { ++Calls; });
    TestEqual(TEXT("ID 고갈"), F.Model->TryAdd(TEXT("Bag"), F.DefinitionTable(), TEXT("Unit"), 1),
        EInventoryOperationFailure::InstanceIdExhausted);
    const FInventoryContainer* Bag = F.Model->FindContainer(TEXT("Bag"));
    TestEqual(TEXT("고갈 뒤 항목 수 보존"), Bag->Items.Num(), Before.Items.Num());
    TestTrue(TEXT("고갈 뒤 점유 보존"), Bag->OccupancyCache == Before.OccupancyCache);
    TestEqual(TEXT("고갈 뒤 카운터 보존"), FItemInstanceIdAllocator::GetNextInstanceId(), MAX_int32);
    TestEqual(TEXT("고갈 뒤 알림 없음"), Calls, 0);
    return true;
}
#endif
