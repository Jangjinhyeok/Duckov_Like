#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "InventoryModel.h"
#include "InventoryPlacement.h"
#include "ItemDefinitionRow.h"
#include "UObject/StrongObjectPtr.h"

namespace InventoryBagEquipmentTests
{
constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

struct FFixture
{
    int32 OriginalCounter = FItemInstanceIdAllocator::GetNextInstanceId();
    TStrongObjectPtr<UDataTable> Table{NewObject<UDataTable>()};
    TStrongObjectPtr<UInventoryModel> Model{NewObject<UInventoryModel>()};
    FInventorySaveRecord Record;

    FFixture()
    {
        Table->RowStruct = FItemDefinitionRow::StaticStruct();
        FItemDefinitionRow Small;
        Small.BagGridSize = FIntPoint(2, 2);
        Table->AddRow(TEXT("Small"), Small);
        FItemDefinitionRow Large;
        Large.BagGridSize = FIntPoint(4, 3);
        Table->AddRow(TEXT("Large"), Large);
        FItemDefinitionRow Wide = Small;
        Wide.Size = FIntPoint(2, 1);
        Table->AddRow(TEXT("Wide"), Wide);
        FItemDefinitionRow Unit;
        Table->AddRow(TEXT("Unit"), Unit);
        FItemDefinitionRow Stack;
        Stack.Size = FIntPoint(1, 2);
        Stack.bStackable = true;
        Stack.MaxStack = 10;
        Table->AddRow(TEXT("Stack"), Stack);
        Record.NextInstanceId = 30;
        AddContainer(TEXT("Slot"), FIntPoint(1, 1));
        AddContainer(TEXT("Bag"), FIntPoint(2, 2));
        AddContainer(TEXT("Stash"), FIntPoint(4, 3));
        AddContainer(TEXT("Other"), FIntPoint(4, 3));
        AddSaved(TEXT("Slot"), 1, TEXT("Small"), FIntPoint::ZeroValue);
    }

    ~FFixture() { FItemInstanceIdAllocator::ResetInstanceIdCounter_ForTests(OriginalCounter); }

    void AddContainer(FName Id, FIntPoint Size)
    {
        auto& Container = Record.Containers.AddDefaulted_GetRef();
        Container.ContainerId = Id;
        Container.GridSize = Size;
    }

    FInventoryContainerSaveRecord& Saved(FName Id)
    {
        return *Record.Containers.FindByPredicate([Id](const auto& Entry) { return Entry.ContainerId == Id; });
    }

    void AddSaved(FName ContainerId, int32 Id, FName Row, FIntPoint Anchor, bool bRotated = false, int32 Quantity = 1)
    {
        auto& Item = Saved(ContainerId).Items.AddDefaulted_GetRef();
        Item.InstanceId = Id;
        Item.DefinitionTable = FSoftObjectPath(Table.Get());
        Item.DefinitionRowName = Row;
        Item.AnchorCell = Anchor;
        Item.bRotated = bRotated;
        Item.Quantity = Quantity;
    }

    EInventoryOperationFailure Bind()
    {
        return Model->BindBagSlot(TEXT("Slot"), TEXT("Bag"), TEXT("Stash"));
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
        Before.OccupancyCache != After.OccupancyCache) { return false; }
    for (int32 Index = 0; Index < Before.Items.Num(); ++Index)
    {
        if (!SameItem(Before.Items[Index], After.Items[Index])) { return false; }
    }
    return true;
}

bool HasRebuiltCache(const FInventoryContainer& Container)
{
    FInventoryContainer Rebuilt = Container;
    FInventoryPlacement::RebuildOccupancyCache(Rebuilt);
    return Rebuilt.OccupancyCache == Container.OccupancyCache;
}
}

namespace InventoryBagEquipmentTests
{

IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestModel_BagBindingValidation,
    "Duckov.InventoryCore.Model.BagEquipment.BindingValidation", InventoryBagEquipmentTests::TestFlags)
bool TestModel_BagBindingValidation::RunTest(const FString& Parameters)
{
    FFixture F;
    if (!TestEqual(TEXT("초기 로드"), F.Model->Load(F.Record), EInventorySaveFailure::None)) { return false; }
    TestFalse(TEXT("명시적 Bind 전 비활성"), F.Model->IsBagSlotBound());
    TestNull(TEXT("미장착 조회"), F.Model->GetEquippedBag());
    FFixture Multiple;
    Multiple.Saved(TEXT("Slot")).GridSize = FIntPoint(2, 1);
    Multiple.AddSaved(TEXT("Slot"), 2, TEXT("Small"), FIntPoint(1, 0));
    TestEqual(TEXT("복수 slot fixture 로드"), Multiple.Model->Load(Multiple.Record), EInventorySaveFailure::None);
    TestEqual(TEXT("slot 단일 항목 필수"), Multiple.Bind(), EInventoryOperationFailure::InvalidContainer);
    FFixture Quantity;
    FItemDefinitionRow* QuantityRow = Quantity.Table->FindRow<FItemDefinitionRow>(TEXT("Small"), TEXT("BagTest"), false);
    QuantityRow->bStackable = true;
    QuantityRow->MaxStack = 2;
    Quantity.Saved(TEXT("Slot")).Items[0].Quantity = 2;
    TestEqual(TEXT("수량 검증 fixture 로드"), Quantity.Model->Load(Quantity.Record), EInventorySaveFailure::None);
    QuantityRow->bStackable = false;
    QuantityRow->MaxStack = 1;
    TestEqual(TEXT("가방 수량 1 필수"), Quantity.Bind(), EInventoryOperationFailure::InvalidCategory);
    int32 Calls = 0;
    F.Model->OnChanged().AddLambda([&](const FInventoryChangeSet& Change)
    {
        ++Calls;
        TestTrue(TEXT("binding 변경 알림"), Change.bBagBindingChanged);
        TestFalse(TEXT("binding은 reset이 아님"), Change.bReset);
        TestTrue(TEXT("binding은 item delta가 없음"), Change.Containers.IsEmpty());
        TestTrue(TEXT("알림 전에 binding 확정"), F.Model->IsBagSlotBound());
        TestEqual(TEXT("Bind callback 재진입 차단"), F.Bind(), EInventoryOperationFailure::OperationInProgress);
        TestEqual(TEXT("Bind callback Load 차단"), F.Model->Load(F.Record), EInventorySaveFailure::OperationInProgress);
    });
    TestEqual(TEXT("없는 slot"), F.Model->BindBagSlot(TEXT("Missing"), TEXT("Bag"), TEXT("Stash")),
        EInventoryOperationFailure::InvalidContainer);
    TestEqual(TEXT("같은 이름"), F.Model->BindBagSlot(TEXT("Slot"), TEXT("Slot"), TEXT("Stash")),
        EInventoryOperationFailure::InvalidContainer);
    TestEqual(TEXT("None 이름"), F.Model->BindBagSlot(NAME_None, TEXT("Bag"), TEXT("Stash")),
        EInventoryOperationFailure::InvalidContainer);
    TestEqual(TEXT("빈 slot"), F.Model->BindBagSlot(TEXT("Other"), TEXT("Bag"), TEXT("Stash")),
        EInventoryOperationFailure::InvalidContainer);
    FItemDefinitionRow* Small = F.Table->FindRow<FItemDefinitionRow>(TEXT("Small"), TEXT("BagTest"), false);
    const FItemDefinitionRow ValidSmall = *Small;
    for (const FIntPoint Size : {FIntPoint::ZeroValue, FIntPoint(-1, 2), FIntPoint(2, 0), FIntPoint(MAX_int32, 2)})
    {
        Small->BagGridSize = Size;
        TestEqual(TEXT("invalid 가방 capability"), F.Bind(), EInventoryOperationFailure::InvalidCategory);
    }
    *Small = ValidSmall;
    Small->bStackable = true;
    TestEqual(TEXT("stackable 가방 거부"), F.Bind(), EInventoryOperationFailure::InvalidCategory);
    *Small = ValidSmall;
    Small->MaxStack = 2;
    TestEqual(TEXT("MaxStack 1 필수"), F.Bind(), EInventoryOperationFailure::InvalidCategory);
    *Small = ValidSmall;
    Small->Size = FIntPoint(2, 1);
    TestEqual(TEXT("slot footprint 명시 검증"), F.Bind(), EInventoryOperationFailure::NoSpace);
    *Small = ValidSmall;
    Small->BagGridSize = FIntPoint(3, 2);
    TestEqual(TEXT("contents와 capability 불일치"), F.Bind(), EInventoryOperationFailure::InvalidContainer);
    *Small = ValidSmall;
    TestEqual(TEXT("검증 실패 알림 없음"), Calls, 0);
    if (!TestEqual(TEXT("초기 Bind"), F.Bind(), EInventoryOperationFailure::None) ||
        !TestNotNull(TEXT("장착 조회 유효"), F.Model->GetEquippedBag())) { return false; }
    TestEqual(TEXT("동일 binding no-op"), F.Bind(), EInventoryOperationFailure::None);
    TestEqual(TEXT("binding 알림 한 번"), Calls, 1);
    TestEqual(TEXT("slot 이름"), F.Model->GetBagSlotContainerId(), FName(TEXT("Slot")));
    TestEqual(TEXT("교환 이름"), F.Model->GetBagExchangeContainerId(), FName(TEXT("Stash")));
    TestEqual(TEXT("contents 이름"), F.Model->GetBagContentsContainerId(), FName(TEXT("Bag")));
    TestEqual(TEXT("실제 slot ID 조회"), F.Model->GetEquippedBag()->InstanceId, 1);
    TestEqual(TEXT("실패한 재binding"), F.Model->BindBagSlot(TEXT("Missing"), TEXT("Bag"), TEXT("Stash")),
        EInventoryOperationFailure::InvalidContainer);
    TestTrue(TEXT("이전 binding 유지"), F.Model->IsBagSlotBound());
    TestEqual(TEXT("이전 slot 유지"), F.Model->GetBagSlotContainerId(), FName(TEXT("Slot")));
    TestEqual(TEXT("재binding 실패 알림 없음"), Calls, 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestModel_BagEquipGrowAndShrink,
    "Duckov.InventoryCore.Model.BagEquipment.GrowAndShrink", InventoryBagEquipmentTests::TestFlags)
bool TestModel_BagEquipGrowAndShrink::RunTest(const FString& Parameters)
{
    FFixture F;
    F.AddSaved(TEXT("Stash"), 2, TEXT("Large"), FIntPoint(3, 1), true);
    F.AddSaved(TEXT("Stash"), 3, TEXT("Unit"), FIntPoint::ZeroValue);
    F.AddSaved(TEXT("Bag"), 10, TEXT("Stack"), FIntPoint::ZeroValue, true, 4);
    F.AddSaved(TEXT("Bag"), 11, TEXT("Unit"), FIntPoint(1, 1));
    if (!TestEqual(TEXT("초기 로드"), F.Model->Load(F.Record), EInventorySaveFailure::None) ||
        !TestEqual(TEXT("초기 Bind"), F.Bind(), EInventoryOperationFailure::None)) { return false; }
    const FInventoryContainer BeforeBag = *F.Model->FindContainer(TEXT("Bag"));
    const FItemInstance BeforeCandidate = *F.Model->FindItem(TEXT("Stash"), 2);
    const FItemInstance BeforeUnit = *F.Model->FindItem(TEXT("Stash"), 3);
    FItemInstanceIdAllocator::ResetInstanceIdCounter_ForTests(MAX_int32);
    int32 Calls = 0;
    FInventoryChangeSet Last;
    F.Model->OnChanged().AddLambda([&](const FInventoryChangeSet& Change) { ++Calls; Last = Change; });
    if (!TestEqual(TEXT("ID 고갈이어도 큰 가방 장착"), F.Model->TryEquipBag(2), EInventoryOperationFailure::None) ||
        !TestNotNull(TEXT("큰 가방 조회 유효"), F.Model->GetEquippedBag()) ||
        !TestNotNull(TEXT("이전 가방 반환 유효"), F.Model->FindItem(TEXT("Stash"), 1))) { return false; }
    FItemInstance Expected = BeforeCandidate;
    Expected.AnchorCell = FIntPoint::ZeroValue;
    TestTrue(TEXT("장착 항목 identity 보존과 slot 원점"), SameItem(Expected, *F.Model->GetEquippedBag()));
    const FInventoryContainer* Stash = F.Model->FindContainer(TEXT("Stash"));
    if (!TestEqual(TEXT("교체 후 Stash 항목 수"), Stash->Items.Num(), 2)) { return false; }
    TestTrue(TEXT("기존 Stash 항목 보존"), SameItem(BeforeUnit, Stash->Items[0]));
    TestEqual(TEXT("기존 가방 반환 first-fit"), F.Model->FindItem(TEXT("Stash"), 1)->AnchorCell, FIntPoint(1, 0));
    TestTrue(TEXT("Stash 순서"), Stash->Items[0].InstanceId == 3 && Stash->Items[1].InstanceId == 1);
    const FInventoryContainer* Bag = F.Model->FindContainer(TEXT("Bag"));
    if (!TestEqual(TEXT("contents 항목 수 보존"), Bag->Items.Num(), BeforeBag.Items.Num())) { return false; }
    TestEqual(TEXT("큰 contents grid"), Bag->GridSize, FIntPoint(4, 3));
    for (int32 Index = 0; Index < BeforeBag.Items.Num(); ++Index)
    {
        TestTrue(TEXT("contents 항목·순서·수량·회전·위치 보존"), SameItem(BeforeBag.Items[Index], Bag->Items[Index]));
    }
    TestEqual(TEXT("교체 알림 한 번"), Calls, 1);
    if (!TestEqual(TEXT("세 container delta"), Last.Containers.Num(), 3)) { return false; }
    TestTrue(TEXT("Stash Added/Removed"), Last.Containers[0].Added == TArray<int32>{1} &&
        Last.Containers[0].Removed == TArray<int32>{2} && Last.Containers[0].Updated.IsEmpty());
    TestTrue(TEXT("Slot Added/Removed"), Last.Containers[1].Added == TArray<int32>{2} &&
        Last.Containers[1].Removed == TArray<int32>{1} && Last.Containers[1].Updated.IsEmpty());
    TestTrue(TEXT("Bag는 grid delta만"), Last.Containers[2].bGridSizeChanged &&
        Last.Containers[2].Added.IsEmpty() && Last.Containers[2].Removed.IsEmpty() &&
        Last.Containers[2].Updated.IsEmpty() && !Last.Containers[2].bOrderChanged);
    TestFalse(TEXT("교체는 binding 변경 아님"), Last.bBagBindingChanged || Last.bReset);
    TestTrue(TEXT("세 cache 재생성 일치"), HasRebuiltCache(*Stash) && HasRebuiltCache(*Bag) &&
        HasRebuiltCache(*F.Model->FindContainer(TEXT("Slot"))));
    if (!TestEqual(TEXT("작은 가방으로 축소"), F.Model->TryEquipBag(1), EInventoryOperationFailure::None) ||
        !TestNotNull(TEXT("작은 가방 조회 유효"), F.Model->GetEquippedBag()) ||
        !TestNotNull(TEXT("큰 가방 반환 유효"), F.Model->FindItem(TEXT("Stash"), 2))) { return false; }
    TestEqual(TEXT("작은 가방 실제 ID"), F.Model->GetEquippedBag()->InstanceId, 1);
    TestTrue(TEXT("축소 후 contents 전체 보존"), SameContainer(BeforeBag, *F.Model->FindContainer(TEXT("Bag"))));
    TestEqual(TEXT("반환된 큰 가방 first-fit"), F.Model->FindItem(TEXT("Stash"), 2)->AnchorCell, FIntPoint(1, 0));
    TestTrue(TEXT("반환된 회전 보존"), F.Model->FindItem(TEXT("Stash"), 2)->bRotated);
    TestEqual(TEXT("두 성공만 알림"), Calls, 2);
    TestEqual(TEXT("allocator 미사용"), FItemInstanceIdAllocator::GetNextInstanceId(), MAX_int32);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestModel_BagEquipFailuresAreAtomic,
    "Duckov.InventoryCore.Model.BagEquipment.FailuresAreAtomic", InventoryBagEquipmentTests::TestFlags)
bool TestModel_BagEquipFailuresAreAtomic::RunTest(const FString& Parameters)
{
    FFixture F;
    F.Saved(TEXT("Slot")).Items[0].DefinitionRowName = TEXT("Large");
    F.Saved(TEXT("Bag")).GridSize = FIntPoint(4, 3);
    F.AddSaved(TEXT("Bag"), 10, TEXT("Unit"), FIntPoint(3, 2));
    F.AddSaved(TEXT("Stash"), 2, TEXT("Small"), FIntPoint(1, 0));
    F.AddSaved(TEXT("Stash"), 3, TEXT("Unit"), FIntPoint::ZeroValue);
    F.AddSaved(TEXT("Stash"), 4, TEXT("Wide"), FIntPoint(2, 1));
    F.AddSaved(TEXT("Other"), 5, TEXT("Large"), FIntPoint::ZeroValue);
    if (!TestEqual(TEXT("초기 로드"), F.Model->Load(F.Record), EInventorySaveFailure::None) ||
        !TestEqual(TEXT("초기 Bind"), F.Bind(), EInventoryOperationFailure::None)) { return false; }
    const FInventoryContainer BeforeStash = *F.Model->FindContainer(TEXT("Stash"));
    const FInventoryContainer BeforeSlot = *F.Model->FindContainer(TEXT("Slot"));
    const FInventoryContainer BeforeBag = *F.Model->FindContainer(TEXT("Bag"));
    const int32 Counter = FItemInstanceIdAllocator::GetNextInstanceId();
    int32 Calls = 0;
    F.Model->OnChanged().AddLambda([&](const FInventoryChangeSet&) { ++Calls; });
    auto CheckPreserved = [&]()
    {
        TestTrue(TEXT("Stash 항목·순서·cache 보존"), SameContainer(BeforeStash, *F.Model->FindContainer(TEXT("Stash"))));
        TestTrue(TEXT("Slot 항목·cache 보존"), SameContainer(BeforeSlot, *F.Model->FindContainer(TEXT("Slot"))));
        TestTrue(TEXT("Bag 항목·grid·cache 보존"), SameContainer(BeforeBag, *F.Model->FindContainer(TEXT("Bag"))));
        TestEqual(TEXT("allocator 보존"), FItemInstanceIdAllocator::GetNextInstanceId(), Counter);
        TestTrue(TEXT("binding 보존"), F.Model->IsBagSlotBound());
        TestEqual(TEXT("실패 알림 없음"), Calls, 0);
    };
    TestEqual(TEXT("없는 ID"), F.Model->TryEquipBag(99), EInventoryOperationFailure::ItemNotFound);
    CheckPreserved();
    TestEqual(TEXT("bound Stash 외 후보 거부"), F.Model->TryEquipBag(5), EInventoryOperationFailure::ItemNotFound);
    CheckPreserved();
    TestEqual(TEXT("일반 아이템 category 거부"), F.Model->TryEquipBag(3), EInventoryOperationFailure::InvalidCategory);
    CheckPreserved();
    TestEqual(TEXT("큰 physical footprint 거부"), F.Model->TryEquipBag(4), EInventoryOperationFailure::NoSpace);
    CheckPreserved();
    TestEqual(TEXT("기존 contents 좌표의 축소 overflow"), F.Model->TryEquipBag(2), EInventoryOperationFailure::ResizeOverflow);
    CheckPreserved();

    // 후보가 반환 공간을 비워도 기존 가방 footprint가 더 크면 돌아갈 수 없다.
    FFixture Full;
    Full.Saved(TEXT("Slot")).GridSize = FIntPoint(2, 1);
    Full.Saved(TEXT("Slot")).Items[0].DefinitionRowName = TEXT("Wide");
    Full.Saved(TEXT("Stash")).GridSize = FIntPoint(1, 1);
    Full.AddSaved(TEXT("Stash"), 2, TEXT("Small"), FIntPoint::ZeroValue);
    if (!TestEqual(TEXT("반환 실패 fixture 로드"), Full.Model->Load(Full.Record), EInventorySaveFailure::None) ||
        !TestEqual(TEXT("큰 physical 가방 Bind"), Full.Bind(), EInventoryOperationFailure::None)) { return false; }
    const FInventoryContainer FullBeforeStash = *Full.Model->FindContainer(TEXT("Stash"));
    const FInventoryContainer FullBeforeSlot = *Full.Model->FindContainer(TEXT("Slot"));
    const FInventoryContainer FullBeforeBag = *Full.Model->FindContainer(TEXT("Bag"));
    int32 FullCalls = 0;
    Full.Model->OnChanged().AddLambda([&](const FInventoryChangeSet&) { ++FullCalls; });
    TestEqual(TEXT("기존 가방 반환 공간 없음"), Full.Model->TryEquipBag(2), EInventoryOperationFailure::NoSpace);
    TestTrue(TEXT("반환 실패 전체 보존"), SameContainer(FullBeforeStash, *Full.Model->FindContainer(TEXT("Stash"))) &&
        SameContainer(FullBeforeSlot, *Full.Model->FindContainer(TEXT("Slot"))) &&
        SameContainer(FullBeforeBag, *Full.Model->FindContainer(TEXT("Bag"))));
    TestEqual(TEXT("반환 실패 allocator 보존"), FItemInstanceIdAllocator::GetNextInstanceId(), Counter);
    TestEqual(TEXT("반환 실패 알림 없음"), FullCalls, 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestModel_BagSlotGenericMutationProtection,
    "Duckov.InventoryCore.Model.BagEquipment.GenericMutationProtection", InventoryBagEquipmentTests::TestFlags)
bool TestModel_BagSlotGenericMutationProtection::RunTest(const FString& Parameters)
{
    FFixture F;
    F.AddSaved(TEXT("Stash"), 2, TEXT("Large"), FIntPoint::ZeroValue);
    F.AddSaved(TEXT("Bag"), 10, TEXT("Stack"), FIntPoint::ZeroValue, true, 4);
    F.AddSaved(TEXT("Bag"), 11, TEXT("Stack"), FIntPoint(0, 1), true, 2);
    if (!TestEqual(TEXT("초기 로드"), F.Model->Load(F.Record), EInventorySaveFailure::None) ||
        !TestEqual(TEXT("초기 Bind"), F.Bind(), EInventoryOperationFailure::None)) { return false; }
    const FInventoryContainer BeforeSlot = *F.Model->FindContainer(TEXT("Slot"));
    int32 Calls = 0;
    F.Model->OnChanged().AddLambda([&](const FInventoryChangeSet&) { ++Calls; });
    auto CheckProtection = [&]()
    {
        TestEqual(TEXT("CanMove slot 원본"), F.Model->CanMove(TEXT("Slot"), TEXT("Stash"), 1, FIntPoint(1, 0), false),
            EInventoryOperationFailure::InvalidContainer);
        TestEqual(TEXT("CanMove slot 대상"), F.Model->CanMove(TEXT("Stash"), TEXT("Slot"), 2, FIntPoint::ZeroValue, false),
            EInventoryOperationFailure::InvalidContainer);
        TestEqual(TEXT("Move slot 원본"), F.Model->TryMove(TEXT("Slot"), TEXT("Stash"), 1, FIntPoint(1, 0), false),
            EInventoryOperationFailure::InvalidContainer);
        TestEqual(TEXT("Move slot 대상"), F.Model->TryMove(TEXT("Stash"), TEXT("Slot"), 2, FIntPoint::ZeroValue, false),
            EInventoryOperationFailure::InvalidContainer);
        TestEqual(TEXT("Move slot 내부"), F.Model->TryMove(TEXT("Slot"), TEXT("Slot"), 1, FIntPoint::ZeroValue, true),
            EInventoryOperationFailure::InvalidContainer);
        TestEqual(TEXT("Transfer slot 원본"), F.Model->TryTransferAll(TEXT("Slot"), TEXT("Stash")),
            EInventoryOperationFailure::InvalidContainer);
        TestEqual(TEXT("Transfer slot 대상"), F.Model->TryTransferAll(TEXT("Stash"), TEXT("Slot")),
            EInventoryOperationFailure::InvalidContainer);
        TestEqual(TEXT("Stack slot 원본"), F.Model->TryStack(TEXT("Slot"), TEXT("Stash"), 1, 2),
            EInventoryOperationFailure::InvalidContainer);
        TestEqual(TEXT("Stack slot 대상"), F.Model->TryStack(TEXT("Stash"), TEXT("Slot"), 2, 1),
            EInventoryOperationFailure::InvalidContainer);
        TestEqual(TEXT("Split slot"), F.Model->TrySplit(TEXT("Slot"), 1, 1), EInventoryOperationFailure::InvalidContainer);
        TestEqual(TEXT("Sort slot"), F.Model->TrySort(TEXT("Slot")), EInventoryOperationFailure::InvalidContainer);
        TestEqual(TEXT("Resize slot"), F.Model->TryResize(TEXT("Slot"), FIntPoint(2, 1)), EInventoryOperationFailure::InvalidContainer);
        TestEqual(TEXT("Add slot"), F.Model->TryAdd(TEXT("Slot"), F.Table.Get(), TEXT("Unit"), 1),
            EInventoryOperationFailure::InvalidContainer);
        TestEqual(TEXT("contents 직접 resize"), F.Model->TryResize(TEXT("Bag"), FIntPoint(4, 3)),
            EInventoryOperationFailure::InvalidContainer);
    };
    CheckProtection();
    TestTrue(TEXT("보호 거부 후 slot 보존"), SameContainer(BeforeSlot, *F.Model->FindContainer(TEXT("Slot"))));
    TestEqual(TEXT("보호 거부 알림 없음"), Calls, 0);
    TestEqual(TEXT("contents Stack 허용"), F.Model->TryStack(TEXT("Bag"), TEXT("Bag"), 11, 10), EInventoryOperationFailure::None);
    TestEqual(TEXT("contents Split 허용"), F.Model->TrySplit(TEXT("Bag"), 10, 2), EInventoryOperationFailure::None);
    TestEqual(TEXT("contents Sort 허용"), F.Model->TrySort(TEXT("Bag")), EInventoryOperationFailure::None);
    TestEqual(TEXT("contents 이동 허용"), F.Model->TryMove(TEXT("Bag"), TEXT("Stash"), 10, FIntPoint(1, 0), true),
        EInventoryOperationFailure::None);
    TestEqual(TEXT("contents 전체 이전 허용"), F.Model->TryTransferAll(TEXT("Bag"), TEXT("Stash")), EInventoryOperationFailure::None);
    TestEqual(TEXT("contents loot 추가 허용"), F.Model->TryAdd(TEXT("Bag"), F.Table.Get(), TEXT("Unit"), 1),
        EInventoryOperationFailure::None);
    TestEqual(TEXT("Load로 binding 무효화"), F.Model->Load(F.Record), EInventorySaveFailure::None);
    const int32 BeforeRejectedCalls = Calls;
    CheckProtection();
    TestEqual(TEXT("NeedsValidation Equip 거부"), F.Model->TryEquipBag(2), EInventoryOperationFailure::InvalidContainer);
    TestEqual(TEXT("재검증 전 거부 알림 없음"), Calls, BeforeRejectedCalls);
    TestTrue(TEXT("NeedsValidation slot 보존"), SameContainer(BeforeSlot, *F.Model->FindContainer(TEXT("Slot"))));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestModel_BagEquipCallbackAndReentry,
    "Duckov.InventoryCore.Model.BagEquipment.CallbackAndReentry", InventoryBagEquipmentTests::TestFlags)
bool TestModel_BagEquipCallbackAndReentry::RunTest(const FString& Parameters)
{
    FFixture F;
    F.AddSaved(TEXT("Stash"), 2, TEXT("Large"), FIntPoint::ZeroValue);
    if (!TestEqual(TEXT("초기 로드"), F.Model->Load(F.Record), EInventorySaveFailure::None) ||
        !TestEqual(TEXT("초기 Bind"), F.Bind(), EInventoryOperationFailure::None)) { return false; }
    int32 Calls = 0;
    F.Model->OnChanged().AddLambda([&](const FInventoryChangeSet& Change)
    {
        ++Calls;
        if (!TestNotNull(TEXT("callback 장착 조회 유효"), F.Model->GetEquippedBag())) { return; }
        TestEqual(TEXT("callback slot 최종 ID"), F.Model->GetEquippedBag()->InstanceId, 2);
        TestEqual(TEXT("callback contents 최종 크기"), F.Model->FindContainer(TEXT("Bag"))->GridSize, FIntPoint(4, 3));
        TestNotNull(TEXT("callback 이전 가방 반환 완료"), F.Model->FindItem(TEXT("Stash"), 1));
        TestNull(TEXT("callback 후보 원본 제거 완료"), F.Model->FindItem(TEXT("Stash"), 2));
        TestEqual(TEXT("Equip 재진입"), F.Model->TryEquipBag(1), EInventoryOperationFailure::OperationInProgress);
        TestEqual(TEXT("Bind 재진입"), F.Bind(), EInventoryOperationFailure::OperationInProgress);
        TestEqual(TEXT("Load 재진입"), F.Model->Load(F.Record), EInventorySaveFailure::OperationInProgress);
        TestEqual(TEXT("Move 재진입"), F.Model->TryMove(TEXT("Slot"), TEXT("Stash"), 2, FIntPoint(1, 0), false),
            EInventoryOperationFailure::OperationInProgress);
        TestEqual(TEXT("Add 재진입"), F.Model->TryAdd(TEXT("Bag"), F.Table.Get(), TEXT("Unit"), 1),
            EInventoryOperationFailure::OperationInProgress);
        TestEqual(TEXT("Resize 재진입 우선"), F.Model->TryResize(TEXT("Bag"), FIntPoint(2, 2)),
            EInventoryOperationFailure::OperationInProgress);
        TestEqual(TEXT("한 ChangeSet 안의 세 상태"), Change.Containers.Num(), 3);
    });
    TestEqual(TEXT("교체 성공"), F.Model->TryEquipBag(2), EInventoryOperationFailure::None);
    TestEqual(TEXT("재진입 부가 알림 없음"), Calls, 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestModel_BagEquipmentSaveLoadRevalidation,
    "Duckov.InventoryCore.Model.BagEquipment.SaveLoadRevalidation", InventoryBagEquipmentTests::TestFlags)
bool TestModel_BagEquipmentSaveLoadRevalidation::RunTest(const FString& Parameters)
{
    FFixture F;
    F.AddSaved(TEXT("Stash"), 2, TEXT("Large"), FIntPoint(2, 1), true);
    F.AddSaved(TEXT("Bag"), 10, TEXT("Stack"), FIntPoint::ZeroValue, true, 4);
    if (!TestEqual(TEXT("초기 로드"), F.Model->Load(F.Record), EInventorySaveFailure::None) ||
        !TestEqual(TEXT("초기 Bind"), F.Bind(), EInventoryOperationFailure::None) ||
        !TestEqual(TEXT("가방 교체"), F.Model->TryEquipBag(2), EInventoryOperationFailure::None)) { return false; }
    FInventorySaveRecord Saved;
    if (!TestEqual(TEXT("v1 저장"), F.Model->Save(Saved), EInventorySaveFailure::None)) { return false; }
    TestEqual(TEXT("format v1 유지"), Saved.FormatVersion, 1);
    const FInventoryContainer BeforeSlot = *F.Model->FindContainer(TEXT("Slot"));
    const FInventoryContainer BeforeBag = *F.Model->FindContainer(TEXT("Bag"));
    const FInventoryContainer BeforeStash = *F.Model->FindContainer(TEXT("Stash"));
    const int32 Counter = FItemInstanceIdAllocator::GetNextInstanceId();
    int32 Calls = 0;
    const FDelegateHandle Handle = F.Model->OnChanged().AddLambda([&](const FInventoryChangeSet& Change)
    {
        ++Calls;
        TestTrue(TEXT("Load reset"), Change.bReset);
        TestTrue(TEXT("Load active binding 무효화 알림"), Change.bBagBindingChanged);
        TestFalse(TEXT("callback 전에 NeedsValidation"), F.Model->IsBagSlotBound());
        TestNull(TEXT("callback Equipped 조회 null"), F.Model->GetEquippedBag());
        TestEqual(TEXT("callback에도 보호 이름 유지"), F.Model->GetBagSlotContainerId(), FName(TEXT("Slot")));
        TestEqual(TEXT("callback CanMove slot 보호"), F.Model->CanMove(TEXT("Slot"), TEXT("Stash"), 2,
            FIntPoint(1, 0), false), EInventoryOperationFailure::InvalidContainer);
        TestEqual(TEXT("callback slot mutation 차단"), F.Model->TryMove(TEXT("Slot"), TEXT("Stash"), 2,
            FIntPoint(1, 0), false), EInventoryOperationFailure::OperationInProgress);
        TestEqual(TEXT("callback contents resize 차단"), F.Model->TryResize(TEXT("Bag"), FIntPoint(2, 2)),
            EInventoryOperationFailure::OperationInProgress);
        TestEqual(TEXT("Load callback Bind 재진입 거부"), F.Bind(), EInventoryOperationFailure::OperationInProgress);
        TestEqual(TEXT("Load callback Equip 재진입 거부"), F.Model->TryEquipBag(1), EInventoryOperationFailure::OperationInProgress);
    });
    FInventorySaveRecord Invalid = Saved;
    Invalid.FormatVersion = 2;
    TestEqual(TEXT("Load 실패"), F.Model->Load(Invalid), EInventorySaveFailure::UnsupportedVersion);
    TestTrue(TEXT("Load 실패 active binding 보존"), F.Model->IsBagSlotBound());
    if (!TestNotNull(TEXT("Load 실패 장착 조회 유효"), F.Model->GetEquippedBag())) { return false; }
    TestEqual(TEXT("Load 실패 equipped ID 보존"), F.Model->GetEquippedBag()->InstanceId, 2);
    TestTrue(TEXT("Load 실패 전체 상태 보존"), SameContainer(BeforeSlot, *F.Model->FindContainer(TEXT("Slot"))) &&
        SameContainer(BeforeBag, *F.Model->FindContainer(TEXT("Bag"))) &&
        SameContainer(BeforeStash, *F.Model->FindContainer(TEXT("Stash"))));
    TestEqual(TEXT("Load 실패 allocator 보존"), FItemInstanceIdAllocator::GetNextInstanceId(), Counter);
    TestEqual(TEXT("Load 실패 알림 없음"), Calls, 0);
    TestEqual(TEXT("기존 Model roundtrip Load"), F.Model->Load(Saved), EInventorySaveFailure::None);
    TestEqual(TEXT("reset 한 번"), Calls, 1);
    F.Model->OnChanged().Remove(Handle);
    TestTrue(TEXT("roundtrip 실제 상태 보존"), SameContainer(BeforeSlot, *F.Model->FindContainer(TEXT("Slot"))) &&
        SameContainer(BeforeBag, *F.Model->FindContainer(TEXT("Bag"))) &&
        SameContainer(BeforeStash, *F.Model->FindContainer(TEXT("Stash"))));
    TestEqual(TEXT("실패한 rebinding은 NeedsValidation 유지"), F.Model->BindBagSlot(TEXT("Missing"), TEXT("Bag"), TEXT("Stash")),
        EInventoryOperationFailure::InvalidContainer);
    TestFalse(TEXT("재검증 실패 후 비활성"), F.Model->IsBagSlotBound());
    TestEqual(TEXT("NeedsValidation 보호 유지"), F.Model->TryResize(TEXT("Bag"), FIntPoint(2, 2)),
        EInventoryOperationFailure::InvalidContainer);
    TestEqual(TEXT("실패한 rebinding 뒤 slot 보호"), F.Model->TryTransferAll(TEXT("Slot"), TEXT("Stash")),
        EInventoryOperationFailure::InvalidContainer);
    if (!TestEqual(TEXT("명시적 재binding"), F.Bind(), EInventoryOperationFailure::None) ||
        !TestNotNull(TEXT("재binding 장착 조회 유효"), F.Model->GetEquippedBag())) { return false; }
    TestEqual(TEXT("실제 장착 ID 복원"), F.Model->GetEquippedBag()->InstanceId, 2);

    TStrongObjectPtr<UInventoryModel> Fresh{NewObject<UInventoryModel>()};
    TestEqual(TEXT("fresh Model v1 Load"), Fresh->Load(Saved), EInventorySaveFailure::None);
    TestFalse(TEXT("fresh Load는 binding 자동 복원하지 않음"), Fresh->IsBagSlotBound());
    TestNull(TEXT("fresh equipped null"), Fresh->GetEquippedBag());
    if (!TestEqual(TEXT("fresh 명시적 Bind"), Fresh->BindBagSlot(TEXT("Slot"), TEXT("Bag"), TEXT("Stash")),
        EInventoryOperationFailure::None) || !TestNotNull(TEXT("fresh 장착 조회 유효"), Fresh->GetEquippedBag())) { return false; }
    TestEqual(TEXT("fresh 실제 장착 ID"), Fresh->GetEquippedBag()->InstanceId, 2);
    TestTrue(TEXT("fresh cache 재생성"), HasRebuiltCache(*Fresh->FindContainer(TEXT("Slot"))) &&
        HasRebuiltCache(*Fresh->FindContainer(TEXT("Bag"))) && HasRebuiltCache(*Fresh->FindContainer(TEXT("Stash"))));
    return true;
}
}
#endif
