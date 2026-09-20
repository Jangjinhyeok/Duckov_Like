#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "InventorySaveRecord.h"
#include "ItemDefinitionRow.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
constexpr EAutomationTestFlags TestFlags =
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

struct FSaveFixture
{
    const int32 OriginalCounter = FItemInstanceIdAllocator::GetNextInstanceId();
    TStrongObjectPtr<UDataTable> Table{NewObject<UDataTable>()};

    FSaveFixture()
    {
        Table->RowStruct = FItemDefinitionRow::StaticStruct();
        FItemDefinitionRow Row;
        Row.Size = FIntPoint(1, 2);
        Row.bStackable = true;
        Row.MaxStack = 10;
        Table->AddRow(TEXT("Bar"), Row);
        FItemInstanceIdAllocator::ResetInstanceIdCounter_ForTests();
    }

    ~FSaveFixture()
    {
        FItemInstanceIdAllocator::ResetInstanceIdCounter_ForTests(OriginalCounter);
    }

    FInventorySaveRecord MakeRecord() const
    {
        FInventorySaveRecord Record;
        Record.NextInstanceId = 25;
        for (int32 Index = 0; Index < 2; ++Index)
        {
            FInventoryContainerSaveRecord& Container = Record.Containers.AddDefaulted_GetRef();
            Container.ContainerId = Index == 0 ? TEXT("Stash") : TEXT("Raid");
            Container.GridSize = FIntPoint(3, 3);
            FInventoryItemSaveRecord& Item = Container.Items.AddDefaulted_GetRef();
            Item.InstanceId = Index == 0 ? 9 : 2;
            Item.DefinitionTable = FSoftObjectPath(Table.Get());
            Item.DefinitionRowName = TEXT("Bar");
            Item.Quantity = 3 + Index;
            Item.AnchorCell = FIntPoint(1, 1);
            Item.bRotated = Index == 1;
        }
        FInventoryItemSaveRecord Extra = Record.Containers[0].Items[0];
        Extra.InstanceId = 0;
        Extra.AnchorCell = FIntPoint(0, 0);
        Record.Containers[0].Items.Add(Extra);
        return Record;
    }
};

bool SameRecord(const FInventorySaveRecord& A, const FInventorySaveRecord& B)
{
    if (A.FormatVersion != B.FormatVersion || A.NextInstanceId != B.NextInstanceId ||
        A.Containers.Num() != B.Containers.Num()) { return false; }
    for (int32 C = 0; C < A.Containers.Num(); ++C)
    {
        const auto& Left = A.Containers[C];
        const auto& Right = B.Containers[C];
        if (Left.ContainerId != Right.ContainerId || Left.GridSize != Right.GridSize ||
            Left.Items.Num() != Right.Items.Num()) { return false; }
        for (int32 I = 0; I < Left.Items.Num(); ++I)
        {
            const auto& X = Left.Items[I];
            const auto& Y = Right.Items[I];
            if (X.InstanceId != Y.InstanceId || X.DefinitionTable != Y.DefinitionTable ||
                X.DefinitionRowName != Y.DefinitionRowName || X.Quantity != Y.Quantity ||
                X.AnchorCell != Y.AnchorCell || X.bRotated != Y.bRotated) { return false; }
        }
    }
    return true;
}

bool SameModels(const TArray<FInventoryNamedContainer>& A, const TArray<FInventoryNamedContainer>& B)
{
    if (A.Num() != B.Num()) { return false; }
    for (int32 C = 0; C < A.Num(); ++C)
    {
        const auto& X = A[C].Container;
        const auto& Y = B[C].Container;
        if (A[C].ContainerId != B[C].ContainerId || X.GridSize != Y.GridSize ||
            X.OccupancyCache != Y.OccupancyCache || X.Items.Num() != Y.Items.Num()) { return false; }
        for (int32 I = 0; I < X.Items.Num(); ++I)
        {
            const auto& L = X.Items[I];
            const auto& R = Y.Items[I];
            if (L.InstanceId != R.InstanceId || L.DefinitionTable != R.DefinitionTable ||
                L.DefinitionRowName != R.DefinitionRowName || L.Quantity != R.Quantity ||
                L.AnchorCell != R.AnchorCell || L.bRotated != R.bRotated) { return false; }
        }
    }
    return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestSave_RoundTrip,
    "Duckov.InventoryCore.Save.RoundTrip", TestFlags)

bool TestSave_RoundTrip::RunTest(const FString& Parameters)
{
    FSaveFixture Fixture;
    const FInventorySaveRecord Record = Fixture.MakeRecord();
    TArray<FInventoryNamedContainer> Models;
    if (!TestEqual(TEXT("복원 성공"), FInventorySaveMapper::TryLoad(Record, Models), EInventorySaveFailure::None)) { return false; }
    TestEqual(TEXT("Container 수"), Models.Num(), 2);
    TestEqual(TEXT("ID와 배열 순서"), Models[0].Container.Items[0].InstanceId, 9);
    TestEqual(TEXT("Definition 참조"), Models[1].Container.Items[0].DefinitionTable.Get(), Fixture.Table.Get());
    const TArray<int32> StashCache = {1, -1, -1, 1, 0, -1, -1, 0, -1};
    const TArray<int32> RaidCache = {-1, -1, -1, -1, 0, 0, -1, -1, -1};
    TestTrue(TEXT("일반 배치 cache"), Models[0].Container.OccupancyCache == StashCache);
    TestTrue(TEXT("회전 배치 cache"), Models[1].Container.OccupancyCache == RaidCache);
    Models[0].Container.OccupancyCache = {123};
    const auto BeforeSave = Models;
    FInventorySaveRecord Saved;
    TestEqual(TEXT("cache와 독립적인 저장"), FInventorySaveMapper::TrySave(Models, Saved), EInventorySaveFailure::None);
    TestTrue(TEXT("모든 레코드 필드와 순서 보존"), SameRecord(Record, Saved));
    TestTrue(TEXT("저장 입력 불변"), SameModels(Models, BeforeSave));
    TestEqual(TEXT("다시 복원"), FInventorySaveMapper::TryLoad(Saved, Models), EInventorySaveFailure::None);
    TestTrue(TEXT("cache 재생성"), Models[0].Container.OccupancyCache == StashCache);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestSave_InvalidRecordsAreAtomic,
    "Duckov.InventoryCore.Save.InvalidRecordsAreAtomic", TestFlags)

bool TestSave_InvalidRecordsAreAtomic::RunTest(const FString& Parameters)
{
    FSaveFixture Fixture;
    const FInventorySaveRecord Valid = Fixture.MakeRecord();
    TArray<FInventoryNamedContainer> Models;
    if (!TestEqual(TEXT("기준 상태"), FInventorySaveMapper::TryLoad(Valid, Models), EInventorySaveFailure::None)) { return false; }
    const auto Before = Models;
    auto CheckFailure = [&](const TCHAR* Label, EInventorySaveFailure Expected,
        TFunctionRef<void(FInventorySaveRecord&)> Mutate)
    {
        FInventorySaveRecord Bad = Valid;
        Bad.NextInstanceId = 100;
        Mutate(Bad);
        TestEqual(Label, FInventorySaveMapper::TryLoad(Bad, Models), Expected);
        TestTrue(TEXT("실패 시 출력 전체 보존"), SameModels(Models, Before));
        TestEqual(TEXT("실패 시 카운터 보존"), FItemInstanceIdAllocator::GetNextInstanceId(), 25);
    };
    CheckFailure(TEXT("버전"), EInventorySaveFailure::UnsupportedVersion, [](auto& R) { R.FormatVersion = 2; });
    CheckFailure(TEXT("이름 없음"), EInventorySaveFailure::InvalidContainerId, [](auto& R) { R.Containers[1].ContainerId = NAME_None; });
    CheckFailure(TEXT("이름 중복"), EInventorySaveFailure::InvalidContainerId, [](auto& R) { R.Containers[1].ContainerId = R.Containers[0].ContainerId; });
    CheckFailure(TEXT("크기 0"), EInventorySaveFailure::InvalidGridSize, [](auto& R) { R.Containers[1].GridSize.X = 0; });
    CheckFailure(TEXT("음수 크기"), EInventorySaveFailure::InvalidGridSize, [](auto& R) { R.Containers[1].GridSize.Y = -1; });
    CheckFailure(TEXT("셀 수 overflow"), EInventorySaveFailure::InvalidGridSize, [](auto& R) { R.Containers[1].GridSize = FIntPoint(MAX_int32, 2); });
    CheckFailure(TEXT("음수 ID"), EInventorySaveFailure::InvalidInstanceId, [](auto& R) { R.Containers[1].Items[0].InstanceId = -1; });
    CheckFailure(TEXT("고갈 sentinel ID"), EInventorySaveFailure::InvalidInstanceId, [](auto& R) { R.Containers[1].Items[0].InstanceId = MAX_int32; });
    CheckFailure(TEXT("Container 간 중복 ID"), EInventorySaveFailure::DuplicateInstanceId, [](auto& R) { R.Containers[1].Items[0].InstanceId = 9; });
    CheckFailure(TEXT("Container 내부 중복 ID"), EInventorySaveFailure::DuplicateInstanceId, [](auto& R)
    {
        const auto Duplicate = R.Containers[1].Items[0];
        R.Containers[1].Items.Add(Duplicate);
    });
    CheckFailure(TEXT("음수 카운터"), EInventorySaveFailure::InvalidCounter, [](auto& R) { R.NextInstanceId = -1; });
    CheckFailure(TEXT("ID 이하 카운터"), EInventorySaveFailure::InvalidCounter, [](auto& R) { R.Containers[1].Items[0].InstanceId = 100; });
    CheckFailure(TEXT("Definition 없음"), EInventorySaveFailure::InvalidDefinition, [](auto& R) { R.Containers[1].Items[0].DefinitionTable.Reset(); });
    CheckFailure(TEXT("Row 없음"), EInventorySaveFailure::InvalidDefinition, [](auto& R) { R.Containers[1].Items[0].DefinitionRowName = TEXT("Missing"); });
    TStrongObjectPtr<UDataTable> WrongTable(NewObject<UDataTable>());
    WrongTable->RowStruct = FTableRowBase::StaticStruct();
    CheckFailure(TEXT("다른 row struct"), EInventorySaveFailure::InvalidDefinition, [&](auto& R) { R.Containers[1].Items[0].DefinitionTable = FSoftObjectPath(WrongTable.Get()); });
    CheckFailure(TEXT("수량 0"), EInventorySaveFailure::InvalidQuantity, [](auto& R) { R.Containers[1].Items[0].Quantity = 0; });
    CheckFailure(TEXT("스택 초과"), EInventorySaveFailure::InvalidQuantity, [](auto& R) { R.Containers[1].Items[0].Quantity = 11; });
    CheckFailure(TEXT("음수 좌표"), EInventorySaveFailure::InvalidPlacement, [](auto& R) { R.Containers[1].Items[0].AnchorCell.X = -1; });
    CheckFailure(TEXT("좌표 overflow"), EInventorySaveFailure::InvalidPlacement, [](auto& R) { R.Containers[1].Items[0].AnchorCell.Y = MAX_int32; });
    CheckFailure(TEXT("회전 경계 초과"), EInventorySaveFailure::InvalidPlacement, [](auto& R) { R.Containers[1].Items[0].AnchorCell.X = 2; });
    CheckFailure(TEXT("겹침"), EInventorySaveFailure::InvalidPlacement, [](auto& R)
    {
        auto Extra = R.Containers[1].Items[0];
        Extra.InstanceId = 50;
        R.Containers[1].Items.Add(Extra);
    });
    FItemDefinitionRow BadFootprint;
    BadFootprint.Size = FIntPoint(MAX_int32, 2);
    BadFootprint.bStackable = true;
    BadFootprint.MaxStack = 10;
    Fixture.Table->AddRow(TEXT("Bad"), BadFootprint);
    CheckFailure(TEXT("footprint overflow"), EInventorySaveFailure::InvalidPlacement, [](auto& R) { R.Containers[1].Items[0].DefinitionRowName = TEXT("Bad"); });
    BadFootprint.Size = FIntPoint(0, 1);
    Fixture.Table->AddRow(TEXT("Bad"), BadFootprint);
    CheckFailure(TEXT("잘못된 footprint"), EInventorySaveFailure::InvalidPlacement, [](auto& R) { R.Containers[1].Items[0].DefinitionRowName = TEXT("Bad"); });
    BadFootprint.Size = FIntPoint(1, 1);
    BadFootprint.bStackable = false;
    Fixture.Table->AddRow(TEXT("Bad"), BadFootprint);
    CheckFailure(TEXT("비 스택 항목 수량"), EInventorySaveFailure::InvalidQuantity, [](auto& R) { R.Containers[1].Items[0].DefinitionRowName = TEXT("Bad"); });
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestSave_InvalidModelsPreserveOutput,
    "Duckov.InventoryCore.Save.InvalidModelsPreserveOutput", TestFlags)

bool TestSave_InvalidModelsPreserveOutput::RunTest(const FString& Parameters)
{
    FSaveFixture Fixture;
    const auto Record = Fixture.MakeRecord();
    TArray<FInventoryNamedContainer> Models;
    if (!TestEqual(TEXT("기준 복원"), FInventorySaveMapper::TryLoad(Record, Models), EInventorySaveFailure::None)) { return false; }
    FInventorySaveRecord Output = Record;
    Output.FormatVersion = 71;
    Output.Containers[0].ContainerId = TEXT("Keep");
    const auto Before = Output;
    const auto ValidModels = Models;
    auto CheckFailure = [&](EInventorySaveFailure Expected)
    {
        const auto InputBefore = Models;
        TestEqual(TEXT("잘못된 Model 저장 거부"), FInventorySaveMapper::TrySave(Models, Output), Expected);
        TestTrue(TEXT("기존 레코드 전체 보존"), SameRecord(Output, Before));
        TestTrue(TEXT("입력 Model 불변"), SameModels(Models, InputBefore));
        TestEqual(TEXT("카운터 불변"), FItemInstanceIdAllocator::GetNextInstanceId(), 25);
        Models = ValidModels;
    };
    Models[1].Container.Items[0].Quantity = 0;
    CheckFailure(EInventorySaveFailure::InvalidQuantity);
    Models[1].Container.Items[0].InstanceId = 9;
    CheckFailure(EInventorySaveFailure::DuplicateInstanceId);
    Models[1].Container.Items[0].InstanceId = 25;
    CheckFailure(EInventorySaveFailure::InvalidCounter);
    Models[1].ContainerId = NAME_None;
    CheckFailure(EInventorySaveFailure::InvalidContainerId);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestSave_CounterAndExhaustion,
    "Duckov.InventoryCore.Save.CounterAndExhaustion", TestFlags)

bool TestSave_CounterAndExhaustion::RunTest(const FString& Parameters)
{
    FSaveFixture Fixture;
    auto Record = Fixture.MakeRecord();
    TArray<FInventoryNamedContainer> Models;
    TestEqual(TEXT("초기 복원"), FInventorySaveMapper::TryLoad(Record, Models), EInventorySaveFailure::None);
    TestEqual(TEXT("삭제된 ID 뒤 번호 보존"), FItemInstanceIdAllocator::AllocateNextInstanceId(), 25);
    FItemInstanceIdAllocator::ResetInstanceIdCounter_ForTests(80);
    TestEqual(TEXT("오래된 저장 복원"), FInventorySaveMapper::TryLoad(Record, Models), EInventorySaveFailure::None);
    TestEqual(TEXT("카운터 되감기 금지"), FItemInstanceIdAllocator::AllocateNextInstanceId(), 80);
    Record.NextInstanceId = MAX_int32 - 1;
    TestEqual(TEXT("고갈 직전 복원"), FInventorySaveMapper::TryLoad(Record, Models), EInventorySaveFailure::None);
    TestEqual(TEXT("마지막 유효 번호"), FItemInstanceIdAllocator::AllocateNextInstanceId(), MAX_int32 - 1);
    for (int32 I = 0; I < 2; ++I)
    {
        TestEqual(TEXT("고갈 명시 실패"), FItemInstanceIdAllocator::AllocateNextInstanceId(), INDEX_NONE);
        TestEqual(TEXT("wraparound 없음"), FItemInstanceIdAllocator::GetNextInstanceId(), MAX_int32);
    }
    FInventorySaveRecord Exhausted;
    TestEqual(TEXT("고갈 상태 저장"), FInventorySaveMapper::TrySave(Models, Exhausted), EInventorySaveFailure::None);
    TestEqual(TEXT("sentinel 보존"), Exhausted.NextInstanceId, MAX_int32);
    FItemInstanceIdAllocator::ResetInstanceIdCounter_ForTests();
    TestEqual(TEXT("고갈 상태 복원"), FInventorySaveMapper::TryLoad(Exhausted, Models), EInventorySaveFailure::None);
    TestEqual(TEXT("복원 뒤에도 고갈"), FItemInstanceIdAllocator::AllocateNextInstanceId(), INDEX_NONE);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestSave_EmptyState,
    "Duckov.InventoryCore.Save.EmptyState", TestFlags)

bool TestSave_EmptyState::RunTest(const FString& Parameters)
{
    FSaveFixture Fixture;
    TArray<FInventoryNamedContainer> Models;
    FInventorySaveRecord Record;
    TestEqual(TEXT("빈 집합 저장"), FInventorySaveMapper::TrySave(Models, Record), EInventorySaveFailure::None);
    FInventoryNamedContainer Named;
    Named.ContainerId = TEXT("Empty");
    Named.Container = FInventoryContainer::MakeEmpty(FIntPoint(2, 2));
    Models.Add(Named);
    TestEqual(TEXT("빈 집합으로 교체"), FInventorySaveMapper::TryLoad(Record, Models), EInventorySaveFailure::None);
    TestEqual(TEXT("기존 출력 제거"), Models.Num(), 0);
    TestEqual(TEXT("빈 초기 카운터"), FItemInstanceIdAllocator::GetNextInstanceId(), 0);
    Models.Add(Named);
    TestEqual(TEXT("빈 Container 저장"), FInventorySaveMapper::TrySave(Models, Record), EInventorySaveFailure::None);
    Models.Reset();
    TestEqual(TEXT("빈 Container 복원"), FInventorySaveMapper::TryLoad(Record, Models), EInventorySaveFailure::None);
    TestTrue(TEXT("빈 cache 복원"), SameModels(Models, TArray<FInventoryNamedContainer>{Named}));
    return true;
}

#endif
