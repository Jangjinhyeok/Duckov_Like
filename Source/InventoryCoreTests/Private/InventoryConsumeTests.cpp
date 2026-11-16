#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "InventoryModel.h"
#include "ItemDefinitionRow.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

// Bag(4x3)과 Stash(4x3). Stack은 최대 5개, Unit은 쌓이지 않는다.
struct FConsumeFixture
{
    // Load가 전역 ID 카운터를 앞당기므로 테스트 뒤 되돌린다.
    int32 OriginalCounter = FItemInstanceIdAllocator::GetNextInstanceId();
    TStrongObjectPtr<UDataTable> Table{NewObject<UDataTable>()};
    TStrongObjectPtr<UInventoryModel> Model{NewObject<UInventoryModel>()};
    FInventorySaveRecord Record;

    FConsumeFixture()
    {
        Table->RowStruct = FItemDefinitionRow::StaticStruct();
        Table->AddRow(TEXT("Unit"), FItemDefinitionRow());
        FItemDefinitionRow Stack;
        Stack.bStackable = true;
        Stack.MaxStack = 5;
        Table->AddRow(TEXT("Stack"), Stack);
        FItemDefinitionRow Weapon;
        Weapon.EquipmentType = EItemEquipmentType::Weapon;
        Table->AddRow(TEXT("Weapon"), Weapon);
        Record.NextInstanceId = 10;
        for (const FName Id : {FName(TEXT("Bag")), FName(TEXT("Stash"))})
        {
            FInventoryContainerSaveRecord& Container = Record.Containers.AddDefaulted_GetRef();
            Container.ContainerId = Id;
            Container.GridSize = FIntPoint(4, 3);
        }
    }

    ~FConsumeFixture() { FItemInstanceIdAllocator::ResetInstanceIdCounter_ForTests(OriginalCounter); }

    TSoftObjectPtr<UDataTable> DefinitionTable() const
    {
        return TSoftObjectPtr<UDataTable>(FSoftObjectPath(Table.Get()));
    }

    void AddSaved(FName ContainerId, int32 Id, FName Row, FIntPoint Anchor, int32 Quantity)
    {
        FInventoryContainerSaveRecord* Container = Record.Containers.FindByPredicate(
            [ContainerId](const FInventoryContainerSaveRecord& Entry) { return Entry.ContainerId == ContainerId; });
        FInventoryItemSaveRecord& Item = Container->Items.AddDefaulted_GetRef();
        Item.InstanceId = Id;
        Item.DefinitionTable = FSoftObjectPath(Table.Get());
        Item.DefinitionRowName = Row;
        Item.AnchorCell = Anchor;
        Item.Quantity = Quantity;
    }

    // Bag: Stack 5 (0,0), Stack 5 (1,0), Stack 2 (0,1), Unit (3,2). Stash: Stack 5 (0,0).
    bool LoadStacks(FAutomationTestBase& Test)
    {
        AddSaved(TEXT("Bag"), 1, TEXT("Stack"), FIntPoint(0, 0), 5);
        AddSaved(TEXT("Bag"), 2, TEXT("Stack"), FIntPoint(1, 0), 5);
        AddSaved(TEXT("Bag"), 3, TEXT("Stack"), FIntPoint(0, 1), 2);
        AddSaved(TEXT("Bag"), 4, TEXT("Unit"), FIntPoint(3, 2), 1);
        AddSaved(TEXT("Stash"), 5, TEXT("Stack"), FIntPoint(0, 0), 5);
        return Test.TestEqual(TEXT("초기 데이터 로드"), Model->Load(Record), EInventorySaveFailure::None);
    }

    int32 Quantity(FName ContainerId, int32 Id) const
    {
        const FItemInstance* Item = Model->FindItem(ContainerId, Id);
        return Item ? Item->Quantity : 0;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestModel_ConsumeAcrossStacks,
    "Duckov.InventoryCore.Model.Consume.AcrossStacks", TestFlags)
bool TestModel_ConsumeAcrossStacks::RunTest(const FString& Parameters)
{
    FConsumeFixture F;
    if (!F.LoadStacks(*this)) { return false; }
    TestEqual(TEXT("가방 Stack 합계"), F.Model->CountItems(TEXT("Bag"), F.DefinitionTable(), TEXT("Stack")), 12);
    TestEqual(TEXT("다른 정의는 따로 셈"), F.Model->CountItems(TEXT("Bag"), F.DefinitionTable(), TEXT("Unit")), 1);
    TestEqual(TEXT("없는 컨테이너는 0"), F.Model->CountItems(TEXT("Missing"), F.DefinitionTable(), TEXT("Stack")), 0);

    int32 Calls = 0;
    FInventoryChangeSet Last;
    F.Model->OnChanged().AddLambda([&](const FInventoryChangeSet& Change) { ++Calls; Last = Change; });
    // 아래 행의 2개를 먼저 다 쓰고, 위 행에서는 오른쪽 스택에서 나머지 2개를 뺀다.
    TestEqual(TEXT("4개 소모"), F.Model->TryConsume(TEXT("Bag"), F.DefinitionTable(), TEXT("Stack"), 4),
        EInventoryOperationFailure::None);
    TestEqual(TEXT("가방 Stack 합계 감소"), F.Model->CountItems(TEXT("Bag"), F.DefinitionTable(), TEXT("Stack")), 8);
    TestNull(TEXT("0이 된 아래 행 스택은 없어짐"), F.Model->FindItem(TEXT("Bag"), 3));
    TestEqual(TEXT("오른쪽 스택에서 나머지"), F.Quantity(TEXT("Bag"), 2), 3);
    TestEqual(TEXT("왼쪽 위 스택은 가득 찬 채"), F.Quantity(TEXT("Bag"), 1), 5);
    TestEqual(TEXT("다른 정의는 그대로"), F.Quantity(TEXT("Bag"), 4), 1);
    TestEqual(TEXT("다른 컨테이너는 그대로"), F.Quantity(TEXT("Stash"), 5), 5);

    TestEqual(TEXT("알림 한 번"), Calls, 1);
    if (TestEqual(TEXT("가방만 변경"), Last.Containers.Num(), 1))
    {
        const FInventoryContainerChange& Change = Last.Containers[0];
        TestEqual(TEXT("변경 컨테이너"), Change.ContainerId, FName(TEXT("Bag")));
        TestTrue(TEXT("Removed는 빈 스택"), Change.Removed == TArray<int32>{3});
        TestTrue(TEXT("Updated는 줄어든 스택"), Change.Updated == TArray<int32>{2});
        TestTrue(TEXT("Added 없음"), Change.Added.IsEmpty());
    }
    // 없어진 스택의 칸은 다시 비어 있다.
    TestEqual(TEXT("빈 칸에 다른 항목 배치"), F.Model->CanMove(TEXT("Stash"), TEXT("Bag"), 5, FIntPoint(0, 1), false),
        EInventoryOperationFailure::None);

    // 합계와 같은 수량은 같은 정의 항목을 모두 없앤다.
    TestEqual(TEXT("남은 전부 소모"), F.Model->TryConsume(TEXT("Bag"), F.DefinitionTable(), TEXT("Stack"), 8),
        EInventoryOperationFailure::None);
    const FInventoryContainer* Bag = F.Model->FindContainer(TEXT("Bag"));
    TestTrue(TEXT("Unit만 남음"), Bag && Bag->Items.Num() == 1 && Bag->Items[0].InstanceId == 4);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestModel_ConsumeFailureIsAtomic,
    "Duckov.InventoryCore.Model.Consume.FailureIsAtomic", TestFlags)
bool TestModel_ConsumeFailureIsAtomic::RunTest(const FString& Parameters)
{
    FConsumeFixture F;
    FInventoryContainerSaveRecord& WeaponSlot = F.Record.Containers.AddDefaulted_GetRef();
    WeaponSlot.ContainerId = TEXT("WeaponSlot");
    WeaponSlot.GridSize = FIntPoint(1, 1);
    if (!F.LoadStacks(*this)) { return false; }
    TestEqual(TEXT("장비 slot 등록"), F.Model->BindEquipmentSlot(TEXT("WeaponSlot"), EItemEquipmentType::Weapon),
        EInventoryOperationFailure::None);
    int32 Calls = 0;
    F.Model->OnChanged().AddLambda([&Calls](const FInventoryChangeSet&) { ++Calls; });

    TestEqual(TEXT("합계보다 많으면 거부"), F.Model->TryConsume(TEXT("Bag"), F.DefinitionTable(), TEXT("Stack"), 13),
        EInventoryOperationFailure::InvalidQuantity);
    TestEqual(TEXT("0개 거부"), F.Model->TryConsume(TEXT("Bag"), F.DefinitionTable(), TEXT("Stack"), 0),
        EInventoryOperationFailure::InvalidQuantity);
    TestEqual(TEXT("음수 거부"), F.Model->TryConsume(TEXT("Bag"), F.DefinitionTable(), TEXT("Stack"), -1),
        EInventoryOperationFailure::InvalidQuantity);
    TestEqual(TEXT("없는 정의 거부"), F.Model->TryConsume(TEXT("Bag"), F.DefinitionTable(), TEXT("Missing"), 1),
        EInventoryOperationFailure::InvalidQuantity);
    TestEqual(TEXT("없는 컨테이너 거부"), F.Model->TryConsume(TEXT("Missing"), F.DefinitionTable(), TEXT("Stack"), 1),
        EInventoryOperationFailure::InvalidContainer);
    TestEqual(TEXT("장비 slot 거부"), F.Model->TryConsume(TEXT("WeaponSlot"), F.DefinitionTable(), TEXT("Stack"), 1),
        EInventoryOperationFailure::InvalidContainer);

    TestEqual(TEXT("실패는 알리지 않음"), Calls, 0);
    TestEqual(TEXT("스택 1 그대로"), F.Quantity(TEXT("Bag"), 1), 5);
    TestEqual(TEXT("스택 2 그대로"), F.Quantity(TEXT("Bag"), 2), 5);
    TestEqual(TEXT("스택 3 그대로"), F.Quantity(TEXT("Bag"), 3), 2);
    TestEqual(TEXT("합계 그대로"), F.Model->CountItems(TEXT("Bag"), F.DefinitionTable(), TEXT("Stack")), 12);
    return true;
}
#endif
