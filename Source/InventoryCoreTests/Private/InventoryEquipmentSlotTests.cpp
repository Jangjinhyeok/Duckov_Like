#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "InventoryModel.h"
#include "InventoryPlacement.h"
#include "ItemDefinitionRow.h"
#include "UObject/StrongObjectPtr.h"

namespace InventoryEquipmentSlotTests
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
        FItemDefinitionRow Rifle;
        Rifle.Size = FIntPoint(2, 1);
        Rifle.EquipmentType = EItemEquipmentType::Weapon;
        Table->AddRow(TEXT("Rifle"), Rifle);
        FItemDefinitionRow Armor;
        Armor.Size = FIntPoint(2, 2);
        Armor.EquipmentType = EItemEquipmentType::Armor;
        Table->AddRow(TEXT("Armor"), Armor);
        FItemDefinitionRow Unit;
        Table->AddRow(TEXT("Unit"), Unit);
        // 장비 종류가 있어도 쌓을 수 있으면 장비가 아니다.
        FItemDefinitionRow StackWeapon = Rifle;
        StackWeapon.Size = FIntPoint(1, 1);
        StackWeapon.bStackable = true;
        StackWeapon.MaxStack = 5;
        Table->AddRow(TEXT("StackWeapon"), StackWeapon);
        Record.NextInstanceId = 30;
        AddContainer(TEXT("Bag"), FIntPoint(4, 3));
        AddContainer(TEXT("Stash"), FIntPoint(4, 3));
        AddContainer(TEXT("WeaponSlot"), FIntPoint(2, 1));
        AddContainer(TEXT("ArmorSlot"), FIntPoint(2, 2));
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

    bool LoadAndBind(FAutomationTestBase& Test)
    {
        return Test.TestEqual(TEXT("초기 로드"), Model->Load(Record), EInventorySaveFailure::None)
            && Test.TestEqual(TEXT("무기 slot Bind"), Model->BindEquipmentSlot(TEXT("WeaponSlot"), EItemEquipmentType::Weapon),
                EInventoryOperationFailure::None)
            && Test.TestEqual(TEXT("방어구 slot Bind"), Model->BindEquipmentSlot(TEXT("ArmorSlot"), EItemEquipmentType::Armor),
                EInventoryOperationFailure::None);
    }
};

bool SameContainer(const FInventoryContainer& Before, const FInventoryContainer& After)
{
    if (Before.GridSize != After.GridSize || Before.Items.Num() != After.Items.Num() ||
        Before.OccupancyCache != After.OccupancyCache) { return false; }
    for (int32 Index = 0; Index < Before.Items.Num(); ++Index)
    {
        const FItemInstance& L = Before.Items[Index];
        const FItemInstance& R = After.Items[Index];
        if (L.InstanceId != R.InstanceId || L.DefinitionRowName != R.DefinitionRowName || L.Quantity != R.Quantity ||
            L.AnchorCell != R.AnchorCell || L.bRotated != R.bRotated) { return false; }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestModel_EquipmentSlotBindingValidation,
    "Duckov.InventoryCore.Model.EquipmentSlot.BindingValidation", TestFlags)
bool TestModel_EquipmentSlotBindingValidation::RunTest(const FString& Parameters)
{
    {
        FFixture F;
        F.AddSaved(TEXT("ArmorSlot"), 1, TEXT("Armor"), FIntPoint::ZeroValue);
        if (!TestEqual(TEXT("초기 로드"), F.Model->Load(F.Record), EInventorySaveFailure::None)) { return false; }
        TestFalse(TEXT("Bind 전에는 일반 컨테이너"), F.Model->IsEquipmentSlot(TEXT("WeaponSlot")));
        TestEqual(TEXT("빈 slot Bind"), F.Model->BindEquipmentSlot(TEXT("WeaponSlot"), EItemEquipmentType::Weapon),
            EInventoryOperationFailure::None);
        TestTrue(TEXT("빈 slot 등록"), F.Model->IsEquipmentSlotBound(TEXT("WeaponSlot")));
        TestEqual(TEXT("장착된 slot Bind"), F.Model->BindEquipmentSlot(TEXT("ArmorSlot"), EItemEquipmentType::Armor),
            EInventoryOperationFailure::None);
        TestEqual(TEXT("종류 None 거부"), F.Model->BindEquipmentSlot(TEXT("Stash"), EItemEquipmentType::None),
            EInventoryOperationFailure::InvalidCategory);
        TestEqual(TEXT("없는 컨테이너"), F.Model->BindEquipmentSlot(TEXT("Missing"), EItemEquipmentType::Weapon),
            EInventoryOperationFailure::InvalidContainer);
        TestFalse(TEXT("실패한 Bind는 등록하지 않음"), F.Model->IsEquipmentSlot(TEXT("Stash")));
        TestEqual(TEXT("가방 slot 이름과 겹침 거부"), F.Model->BindBagSlot(TEXT("WeaponSlot"), TEXT("Bag"), TEXT("Stash")),
            EInventoryOperationFailure::InvalidContainer);
    }
    {
        FFixture F;
        F.AddSaved(TEXT("WeaponSlot"), 1, TEXT("Armor"), FIntPoint::ZeroValue);
        F.Saved(TEXT("WeaponSlot")).GridSize = FIntPoint(2, 2);
        TestEqual(TEXT("다른 종류 fixture 로드"), F.Model->Load(F.Record), EInventorySaveFailure::None);
        TestEqual(TEXT("다른 종류가 든 slot"), F.Model->BindEquipmentSlot(TEXT("WeaponSlot"), EItemEquipmentType::Weapon),
            EInventoryOperationFailure::InvalidCategory);
        TestFalse(TEXT("종류 불일치는 등록하지 않음"), F.Model->IsEquipmentSlot(TEXT("WeaponSlot")));
    }
    {
        FFixture F;
        F.AddSaved(TEXT("WeaponSlot"), 1, TEXT("StackWeapon"), FIntPoint::ZeroValue);
        TestEqual(TEXT("쌓이는 정의 fixture 로드"), F.Model->Load(F.Record), EInventorySaveFailure::None);
        TestEqual(TEXT("쌓이는 정의 거부"), F.Model->BindEquipmentSlot(TEXT("WeaponSlot"), EItemEquipmentType::Weapon),
            EInventoryOperationFailure::InvalidCategory);
    }
    {
        FFixture F;
        F.Saved(TEXT("WeaponSlot")).GridSize = FIntPoint(4, 1);
        F.AddSaved(TEXT("WeaponSlot"), 1, TEXT("Rifle"), FIntPoint(2, 0));
        TestEqual(TEXT("원점 밖 fixture 로드"), F.Model->Load(F.Record), EInventorySaveFailure::None);
        TestEqual(TEXT("원점 밖 항목 거부"), F.Model->BindEquipmentSlot(TEXT("WeaponSlot"), EItemEquipmentType::Weapon),
            EInventoryOperationFailure::InvalidContainer);
    }
    {
        FFixture F;
        F.Saved(TEXT("WeaponSlot")).GridSize = FIntPoint(4, 1);
        F.AddSaved(TEXT("WeaponSlot"), 1, TEXT("Rifle"), FIntPoint::ZeroValue);
        F.AddSaved(TEXT("WeaponSlot"), 2, TEXT("Rifle"), FIntPoint(2, 0));
        TestEqual(TEXT("두 항목 fixture 로드"), F.Model->Load(F.Record), EInventorySaveFailure::None);
        TestEqual(TEXT("두 항목 거부"), F.Model->BindEquipmentSlot(TEXT("WeaponSlot"), EItemEquipmentType::Weapon),
            EInventoryOperationFailure::InvalidContainer);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestModel_EquipmentSlotEquipAndUnequip,
    "Duckov.InventoryCore.Model.EquipmentSlot.EquipAndUnequip", TestFlags)
bool TestModel_EquipmentSlotEquipAndUnequip::RunTest(const FString& Parameters)
{
    FFixture F;
    F.AddSaved(TEXT("Bag"), 5, TEXT("Rifle"), FIntPoint(3, 1), true);
    F.AddSaved(TEXT("ArmorSlot"), 6, TEXT("Armor"), FIntPoint::ZeroValue);
    if (!F.LoadAndBind(*this)) { return false; }
    TArray<FInventoryChangeSet> Changes;
    F.Model->OnChanged().AddLambda([&Changes](const FInventoryChangeSet& Change) { Changes.Add(Change); });

    // 드래그가 slot의 어느 칸을 가리키든, 회전 중이든 Model이 원점·무회전으로 정한다.
    TestEqual(TEXT("미리보기: 회전·칸과 무관하게 장착 가능"),
        F.Model->CanMove(TEXT("Bag"), TEXT("WeaponSlot"), 5, FIntPoint(1, 0), true), EInventoryOperationFailure::None);
    TestEqual(TEXT("Bag에서 무기 장착"), F.Model->TryMove(TEXT("Bag"), TEXT("WeaponSlot"), 5, FIntPoint(1, 0), true),
        EInventoryOperationFailure::None);
    const FItemInstance* Equipped = F.Model->FindItem(TEXT("WeaponSlot"), 5);
    if (!TestNotNull(TEXT("같은 ID가 slot에"), Equipped)) { return false; }
    TestEqual(TEXT("slot 원점"), Equipped->AnchorCell, FIntPoint::ZeroValue);
    TestFalse(TEXT("무회전"), Equipped->bRotated);
    TestNull(TEXT("Bag에서 제거"), F.Model->FindItem(TEXT("Bag"), 5));
    if (TestEqual(TEXT("장착 알림 한 번"), Changes.Num(), 1))
    {
        TestEqual(TEXT("두 컨테이너 변경"), Changes[0].Containers.Num(), 2);
    }

    TestEqual(TEXT("방어구를 Bag의 정한 칸에 해제"), F.Model->TryMove(TEXT("ArmorSlot"), TEXT("Bag"), 6, FIntPoint(1, 1), false),
        EInventoryOperationFailure::None);
    const FItemInstance* Unequipped = F.Model->FindItem(TEXT("Bag"), 6);
    if (!TestNotNull(TEXT("해제한 방어구가 Bag에"), Unequipped)) { return false; }
    TestEqual(TEXT("해제 위치는 놓은 칸"), Unequipped->AnchorCell, FIntPoint(1, 1));
    TestTrue(TEXT("방어구 slot 비움"), F.Model->FindContainer(TEXT("ArmorSlot"))->Items.IsEmpty());
    TestTrue(TEXT("해제 뒤에도 slot 등록 유지"), F.Model->IsEquipmentSlotBound(TEXT("ArmorSlot")));
    TestEqual(TEXT("Stash에서도 장착"), F.Model->TryMove(TEXT("Bag"), TEXT("Stash"), 6, FIntPoint::ZeroValue, false),
        EInventoryOperationFailure::None);
    TestEqual(TEXT("Stash → 방어구 slot"), F.Model->TryMove(TEXT("Stash"), TEXT("ArmorSlot"), 6, FIntPoint(1, 1), false),
        EInventoryOperationFailure::None);
    TestNotNull(TEXT("방어구 재장착"), F.Model->FindItem(TEXT("ArmorSlot"), 6));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestModel_EquipmentSlotFailuresAreAtomic,
    "Duckov.InventoryCore.Model.EquipmentSlot.FailuresAreAtomic", TestFlags)
bool TestModel_EquipmentSlotFailuresAreAtomic::RunTest(const FString& Parameters)
{
    FFixture F;
    F.AddSaved(TEXT("WeaponSlot"), 1, TEXT("Rifle"), FIntPoint::ZeroValue);
    F.AddSaved(TEXT("Bag"), 2, TEXT("Rifle"), FIntPoint::ZeroValue);
    F.AddSaved(TEXT("Bag"), 3, TEXT("Armor"), FIntPoint(0, 1));
    F.AddSaved(TEXT("Bag"), 4, TEXT("Unit"), FIntPoint(3, 0));
    F.AddSaved(TEXT("Bag"), 5, TEXT("StackWeapon"), FIntPoint(3, 1), false, 3);
    F.Saved(TEXT("Stash")).GridSize = FIntPoint(1, 1);
    F.AddSaved(TEXT("Stash"), 7, TEXT("Unit"), FIntPoint::ZeroValue);
    if (!F.LoadAndBind(*this)) { return false; }
    const FInventoryContainer BeforeBag = *F.Model->FindContainer(TEXT("Bag"));
    const FInventoryContainer BeforeStash = *F.Model->FindContainer(TEXT("Stash"));
    const FInventoryContainer BeforeWeapon = *F.Model->FindContainer(TEXT("WeaponSlot"));
    const FInventoryContainer BeforeArmor = *F.Model->FindContainer(TEXT("ArmorSlot"));
    int32 Calls = 0;
    F.Model->OnChanged().AddLambda([&Calls](const FInventoryChangeSet&) { ++Calls; });
    auto Check = [&](const TCHAR* What, EInventoryOperationFailure Actual, EInventoryOperationFailure Expected)
    {
        TestEqual(What, Actual, Expected);
        TestTrue(TEXT("네 컨테이너 보존"), SameContainer(BeforeBag, *F.Model->FindContainer(TEXT("Bag")))
            && SameContainer(BeforeStash, *F.Model->FindContainer(TEXT("Stash")))
            && SameContainer(BeforeWeapon, *F.Model->FindContainer(TEXT("WeaponSlot")))
            && SameContainer(BeforeArmor, *F.Model->FindContainer(TEXT("ArmorSlot"))));
        TestEqual(TEXT("실패 알림 없음"), Calls, 0);
    };
    Check(TEXT("무기 slot에 방어구"), F.Model->TryMove(TEXT("Bag"), TEXT("WeaponSlot"), 3, FIntPoint::ZeroValue, false),
        EInventoryOperationFailure::InvalidCategory);
    Check(TEXT("미리보기도 같은 사유"), F.Model->CanMove(TEXT("Bag"), TEXT("WeaponSlot"), 3, FIntPoint::ZeroValue, false),
        EInventoryOperationFailure::InvalidCategory);
    Check(TEXT("방어구 slot에 일반 아이템"), F.Model->TryMove(TEXT("Bag"), TEXT("ArmorSlot"), 4, FIntPoint::ZeroValue, false),
        EInventoryOperationFailure::InvalidCategory);
    Check(TEXT("쌓이는 무기 정의"), F.Model->TryMove(TEXT("Bag"), TEXT("WeaponSlot"), 5, FIntPoint::ZeroValue, false),
        EInventoryOperationFailure::InvalidCategory);
    // 교체(swap)는 하지 않는다. 먼저 해제해야 한다.
    Check(TEXT("이미 찬 무기 slot"), F.Model->TryMove(TEXT("Bag"), TEXT("WeaponSlot"), 2, FIntPoint::ZeroValue, false),
        EInventoryOperationFailure::Occupied);
    Check(TEXT("해제할 칸이 막힘"), F.Model->TryMove(TEXT("WeaponSlot"), TEXT("Bag"), 1, FIntPoint(2, 0), false),
        EInventoryOperationFailure::Occupied);
    Check(TEXT("해제할 공간 부족"), F.Model->TryMove(TEXT("WeaponSlot"), TEXT("Stash"), 1, FIntPoint::ZeroValue, false),
        EInventoryOperationFailure::NoSpace);
    Check(TEXT("없는 항목"), F.Model->TryMove(TEXT("Bag"), TEXT("ArmorSlot"), 99, FIntPoint::ZeroValue, false),
        EInventoryOperationFailure::ItemNotFound);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestModel_EquipmentSlotGenericMutationProtection,
    "Duckov.InventoryCore.Model.EquipmentSlot.GenericMutationProtection", TestFlags)
bool TestModel_EquipmentSlotGenericMutationProtection::RunTest(const FString& Parameters)
{
    FFixture F;
    F.AddSaved(TEXT("WeaponSlot"), 1, TEXT("Rifle"), FIntPoint::ZeroValue);
    F.AddSaved(TEXT("Bag"), 2, TEXT("StackWeapon"), FIntPoint::ZeroValue, false, 2);
    if (!F.LoadAndBind(*this)) { return false; }
    const FInventoryContainer BeforeWeapon = *F.Model->FindContainer(TEXT("WeaponSlot"));
    const FInventoryContainer BeforeArmor = *F.Model->FindContainer(TEXT("ArmorSlot"));
    int32 Calls = 0;
    F.Model->OnChanged().AddLambda([&Calls](const FInventoryChangeSet&) { ++Calls; });
    constexpr EInventoryOperationFailure Invalid = EInventoryOperationFailure::InvalidContainer;
    TestEqual(TEXT("Stack 원본"), F.Model->TryStack(TEXT("WeaponSlot"), TEXT("Bag"), 1, 2), Invalid);
    TestEqual(TEXT("Stack 대상"), F.Model->TryStack(TEXT("Bag"), TEXT("WeaponSlot"), 2, 1), Invalid);
    TestEqual(TEXT("CanStack"), F.Model->CanStack(TEXT("Bag"), TEXT("WeaponSlot"), 2, 1), Invalid);
    TestEqual(TEXT("겹침 조회"), F.Model->FindSingleOverlap(TEXT("Bag"), TEXT("WeaponSlot"), 2, FIntPoint::ZeroValue, false),
        INDEX_NONE);
    TestEqual(TEXT("Split"), F.Model->TrySplit(TEXT("WeaponSlot"), 1, 1), Invalid);
    TestEqual(TEXT("Sort"), F.Model->TrySort(TEXT("WeaponSlot")), Invalid);
    TestEqual(TEXT("Resize"), F.Model->TryResize(TEXT("ArmorSlot"), FIntPoint(4, 4)), Invalid);
    TestEqual(TEXT("Add"), F.Model->TryAdd(TEXT("ArmorSlot"), F.Table.Get(), TEXT("Armor"), 1), Invalid);
    TestEqual(TEXT("TransferAll 원본"), F.Model->TryTransferAll(TEXT("WeaponSlot"), TEXT("Bag")), Invalid);
    TestEqual(TEXT("TransferAll 대상"), F.Model->TryTransferAll(TEXT("Bag"), TEXT("ArmorSlot")), Invalid);
    TestTrue(TEXT("slot 보존"), SameContainer(BeforeWeapon, *F.Model->FindContainer(TEXT("WeaponSlot")))
        && SameContainer(BeforeArmor, *F.Model->FindContainer(TEXT("ArmorSlot"))));
    TestEqual(TEXT("거부 알림 없음"), Calls, 0);
    TestEqual(TEXT("일반 컨테이너 연산은 그대로"), F.Model->TrySplit(TEXT("Bag"), 2, 1), EInventoryOperationFailure::None);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestModel_EquipmentSlotSaveLoadRevalidation,
    "Duckov.InventoryCore.Model.EquipmentSlot.SaveLoadRevalidation", TestFlags)
bool TestModel_EquipmentSlotSaveLoadRevalidation::RunTest(const FString& Parameters)
{
    FFixture F;
    F.AddSaved(TEXT("Bag"), 5, TEXT("Rifle"), FIntPoint::ZeroValue);
    F.AddSaved(TEXT("Bag"), 6, TEXT("Armor"), FIntPoint(0, 1));
    if (!F.LoadAndBind(*this) ||
        !TestEqual(TEXT("무기 장착"), F.Model->TryMove(TEXT("Bag"), TEXT("WeaponSlot"), 5, FIntPoint::ZeroValue, false),
            EInventoryOperationFailure::None)) { return false; }
    FInventorySaveRecord Saved;
    if (!TestEqual(TEXT("v1 저장"), F.Model->Save(Saved), EInventorySaveFailure::None)) { return false; }
    TestEqual(TEXT("format v1 유지"), Saved.FormatVersion, 1);

    TestEqual(TEXT("같은 Model roundtrip Load"), F.Model->Load(Saved), EInventorySaveFailure::None);
    TestTrue(TEXT("Load 뒤에도 slot 이름 보호"), F.Model->IsEquipmentSlot(TEXT("WeaponSlot")));
    TestFalse(TEXT("Load 뒤 재검증 필요"), F.Model->IsEquipmentSlotBound(TEXT("WeaponSlot")));
    TestEqual(TEXT("재검증 전 해제 거부"), F.Model->TryMove(TEXT("WeaponSlot"), TEXT("Bag"), 5, FIntPoint(2, 0), false),
        EInventoryOperationFailure::InvalidContainer);
    TestEqual(TEXT("재검증 전 장착 거부"), F.Model->CanMove(TEXT("Bag"), TEXT("ArmorSlot"), 6, FIntPoint::ZeroValue, false),
        EInventoryOperationFailure::InvalidContainer);
    TestEqual(TEXT("재검증 전 일반 연산도 거부"), F.Model->TryAdd(TEXT("ArmorSlot"), F.Table.Get(), TEXT("Armor"), 1),
        EInventoryOperationFailure::InvalidContainer);
    TestEqual(TEXT("무기 slot 재Bind"), F.Model->BindEquipmentSlot(TEXT("WeaponSlot"), EItemEquipmentType::Weapon),
        EInventoryOperationFailure::None);
    TestNotNull(TEXT("장착 ID 보존"), F.Model->FindItem(TEXT("WeaponSlot"), 5));
    TestEqual(TEXT("재Bind한 slot만 다시 동작"), F.Model->TryMove(TEXT("WeaponSlot"), TEXT("Bag"), 5, FIntPoint(2, 0), false),
        EInventoryOperationFailure::None);
    TestEqual(TEXT("재Bind 안 한 slot은 계속 거부"), F.Model->TryMove(TEXT("Bag"), TEXT("ArmorSlot"), 6, FIntPoint::ZeroValue, false),
        EInventoryOperationFailure::InvalidContainer);

    TStrongObjectPtr<UInventoryModel> Fresh{NewObject<UInventoryModel>()};
    TestEqual(TEXT("fresh Model v1 Load"), Fresh->Load(Saved), EInventorySaveFailure::None);
    TestFalse(TEXT("fresh Load는 slot을 자동 등록하지 않음"), Fresh->IsEquipmentSlot(TEXT("WeaponSlot")));
    TestEqual(TEXT("fresh 명시적 Bind"), Fresh->BindEquipmentSlot(TEXT("WeaponSlot"), EItemEquipmentType::Weapon),
        EInventoryOperationFailure::None);
    TestNotNull(TEXT("fresh 장착 ID"), Fresh->FindItem(TEXT("WeaponSlot"), 5));
    return true;
}
}
#endif
