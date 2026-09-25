#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "ContainerViewModel.h"
#include "InteractionViewModel.h"
#include "InventoryModel.h"
#include "ItemDefinitionRow.h"
#include "ItemViewModel.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
struct FViewModelFixture
{
    int32 OriginalCounter = FItemInstanceIdAllocator::GetNextInstanceId();
    TStrongObjectPtr<UDataTable> Table{NewObject<UDataTable>()};
    TStrongObjectPtr<UInventoryModel> Model{NewObject<UInventoryModel>()};
    TStrongObjectPtr<UContainerViewModel> A{NewObject<UContainerViewModel>()};
    TStrongObjectPtr<UContainerViewModel> B{NewObject<UContainerViewModel>()};
    TStrongObjectPtr<UInteractionViewModel> Interaction{NewObject<UInteractionViewModel>()};
    FInventorySaveRecord Record;
    FViewModelFixture()
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
        A->Bind(Model.Get(), TEXT("A"));
        B->Bind(Model.Get(), TEXT("B"));
        Interaction->Bind(Model.Get());
    }
    ~FViewModelFixture() { FItemInstanceIdAllocator::ResetInstanceIdCounter_ForTests(OriginalCounter); }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestVM_ProjectionAndNotifications, "Duckov.ViewModel.ProjectionAndNotifications", TestFlags)
bool TestVM_ProjectionAndNotifications::RunTest(const FString& Parameters)
{
    FViewModelFixture F;
    TestEqual(TEXT("초기 grid"), F.A->GetGridSize(), FIntPoint(4, 4));
    if (!TestEqual(TEXT("초기 Item VM"), F.A->GetItems().Num(), 1)) { return false; }
    UItemViewModel* Item = F.A->GetItems()[0];
    int32 ItemNotifications = 0;
    int32 OtherNotifications = 0;
    Item->AddFieldValueChangedDelegate(UItemViewModel::FFieldNotificationClassDescriptor::GetAnchorCell,
        INotifyFieldValueChanged::FFieldValueChangedDelegate::CreateLambda([&](UObject*, UE::FieldNotification::FFieldId) { ++ItemNotifications; }));
    F.B->GetItems()[0]->AddFieldValueChangedDelegate(UItemViewModel::FFieldNotificationClassDescriptor::GetAnchorCell,
        INotifyFieldValueChanged::FFieldValueChangedDelegate::CreateLambda([&](UObject*, UE::FieldNotification::FFieldId) { ++OtherNotifications; }));
    F.Model->TryMove(TEXT("A"), TEXT("A"), 1, FIntPoint(2, 1), true);
    TestEqual(TEXT("같은 ID의 VM 재사용"), F.A->GetItems()[0].Get(), Item);
    TestEqual(TEXT("Model 위치 조회"), Item->GetAnchorCell(), FIntPoint(2, 1));
    TestEqual(TEXT("회전 footprint"), Item->GetFootprint(), FIntPoint(2, 1));
    TestEqual(TEXT("변경 항목 통지"), ItemNotifications, 1);
    TestEqual(TEXT("다른 Container 항목 통지 없음"), OtherNotifications, 0);
    F.Model->TryMove(TEXT("A"), TEXT("A"), 1, FIntPoint(-1, 0), false);
    TestEqual(TEXT("실패 항목 통지 없음"), ItemNotifications, 1);
    F.Model->TrySort(TEXT("A"));
    TestEqual(TEXT("sort 뒤 동일 VM"), F.A->GetItems()[0].Get(), Item);
    F.Model->TryResize(TEXT("A"), FIntPoint(5, 5));
    TestEqual(TEXT("resize 투영"), F.A->GetGridSize(), FIntPoint(5, 5));
    TestEqual(TEXT("표시 Definition"), Item->GetDefinitionRowName(), FName(TEXT("Bar")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestVM_RebindAndRemoval, "Duckov.ViewModel.RebindAndRemoval", TestFlags)
bool TestVM_RebindAndRemoval::RunTest(const FString& Parameters)
{
    FViewModelFixture F;
    TStrongObjectPtr<UItemViewModel> OldItem(F.A->GetItems()[0]);
    F.A->Bind(F.Model.Get(), TEXT("B"));
    F.A->Bind(F.Model.Get(), TEXT("B"));
    TestFalse(TEXT("이전 Item VM 무효화"), OldItem->IsAvailable());
    int32 Notifications = 0;
    F.A->GetItems()[0]->AddFieldValueChangedDelegate(UItemViewModel::FFieldNotificationClassDescriptor::GetQuantity,
        INotifyFieldValueChanged::FFieldValueChangedDelegate::CreateLambda([&](UObject*, UE::FieldNotification::FFieldId) { ++Notifications; }));
    F.Model->TryStack(TEXT("A"), TEXT("B"), 1, 2);
    TestEqual(TEXT("재바인딩 중복 구독 없음"), Notifications, 1);
    TestEqual(TEXT("수량 투영"), F.A->GetItems()[0]->GetQuantity(), 6);
    F.Model->Load(F.Record);
    TStrongObjectPtr<UItemViewModel> RemovedItem(F.B->GetItems()[0]);
    F.Model->TryStack(TEXT("B"), TEXT("A"), 2, 1);
    TestEqual(TEXT("삭제된 목록 제거"), F.B->GetItems().Num(), 0);
    TestFalse(TEXT("삭제 Item의 외부 참조 안전"), RemovedItem->IsAvailable());
    F.A->Bind(nullptr, NAME_None);
    TestFalse(TEXT("화면 해제"), F.A->IsAvailable());
    TestTrue(TEXT("화면 목록 해제"), F.A->GetItems().IsEmpty());
    F.Model->Load(F.Record);
    TestTrue(TEXT("해제 후 통지로 재생성 안 됨"), F.A->GetItems().IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestVM_DragPreviewDrop, "Duckov.ViewModel.DragPreviewDrop", TestFlags)
bool TestVM_DragPreviewDrop::RunTest(const FString& Parameters)
{
    FViewModelFixture F;
    TestEqual(TEXT("드래그 시작"), F.Interaction->BeginDrag(TEXT("A"), 1), EInventoryOperationFailure::None);
    TestEqual(TEXT("회전"), F.Interaction->Rotate(), EInventoryOperationFailure::None);
    TestFalse(TEXT("프리뷰 회전은 Model 불변"), F.Model->FindItem(TEXT("A"), 1)->bRotated);
    TestEqual(TEXT("겹침 프리뷰"), F.Interaction->Preview(TEXT("B"), FIntPoint(0, 0)), EInventoryOperationFailure::Occupied);
    TestEqual(TEXT("실패 drop"), F.Interaction->Drop(), EInventoryOperationFailure::Occupied);
    TestTrue(TEXT("실패 drop은 drag 유지"), F.Interaction->IsDragging());
    TestFalse(TEXT("사용자 실패 이유"), F.Interaction->GetFailureText().IsEmpty());
    TestEqual(TEXT("유효 프리뷰"), F.Interaction->Preview(TEXT("B"), FIntPoint(1, 2)), EInventoryOperationFailure::None);
    TestNotNull(TEXT("drop 전 source 유지"), F.Model->FindItem(TEXT("A"), 1));
    TestEqual(TEXT("drop 성공"), F.Interaction->Drop(), EInventoryOperationFailure::None);
    TestFalse(TEXT("drag 종료"), F.Interaction->IsDragging());
    TestTrue(TEXT("회전 commit"), F.Model->FindItem(TEXT("B"), 1)->bRotated);
    TestTrue(TEXT("source VM 제거"), F.A->GetItems().IsEmpty());
    TestEqual(TEXT("target VM 추가"), F.B->GetItems().Num(), 2);
    TestTrue(TEXT("오류 해제"), F.Interaction->GetFailureText().IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestVM_CancelAndStalePreview, "Duckov.ViewModel.CancelAndStalePreview", TestFlags)
bool TestVM_CancelAndStalePreview::RunTest(const FString& Parameters)
{
    FViewModelFixture F;
    F.Interaction->BeginDrag(TEXT("A"), 1);
    F.Interaction->Preview(TEXT("B"), FIntPoint(1, 1));
    F.Model->TryMove(TEXT("B"), TEXT("B"), 2, FIntPoint(1, 1), false);
    TestEqual(TEXT("외부 변경 후 프리뷰 재검증"), F.Interaction->GetPreviewFailure(), EInventoryOperationFailure::Occupied);
    TestEqual(TEXT("stale 프리뷰 drop 거부"), F.Interaction->Drop(), EInventoryOperationFailure::Occupied);
    F.Interaction->Cancel();
    TestFalse(TEXT("ESC/닫힘 취소 API"), F.Interaction->IsDragging());
    TestEqual(TEXT("취소 Model 불변"), F.Model->FindItem(TEXT("A"), 1)->AnchorCell, FIntPoint::ZeroValue);
    F.Interaction->BeginDrag(TEXT("A"), 1);
    F.Model->TryStack(TEXT("A"), TEXT("B"), 1, 2);
    TestFalse(TEXT("드래그 원본 제거 시 취소"), F.Interaction->IsDragging());
    F.Model->Load(F.Record);
    F.Interaction->BeginDrag(TEXT("A"), 1);
    F.Model->Load(F.Record);
    TestFalse(TEXT("전체 reset 시 취소"), F.Interaction->IsDragging());
    F.Interaction->BeginDrag(TEXT("A"), 1);
    F.Interaction->Bind(nullptr);
    TestFalse(TEXT("owner 해제 시 취소"), F.Interaction->IsDragging());
    TestEqual(TEXT("owner 없는 명령"), F.Interaction->Sort(TEXT("A")), EInventoryOperationFailure::InvalidContainer);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestVM_CommandAndLifetime, "Duckov.ViewModel.CommandAndLifetime", TestFlags)
bool TestVM_CommandAndLifetime::RunTest(const FString& Parameters)
{
    FViewModelFixture F;
    TestEqual(TEXT("정렬 명령"), F.Interaction->Sort(TEXT("A")), EInventoryOperationFailure::None);
    TestEqual(TEXT("크기 명령"), F.Interaction->Resize(TEXT("A"), FIntPoint(5, 5)), EInventoryOperationFailure::None);
    TestEqual(TEXT("병합 명령"), F.Interaction->Stack(TEXT("A"), TEXT("B"), 1, 2), EInventoryOperationFailure::None);
    TWeakObjectPtr<UInventoryModel> WeakModel = F.Model.Get();
    F.Model.Reset();
    CollectGarbage(RF_NoFlags);
    TestTrue(TEXT("바인딩 동안 owner 유지"), WeakModel.IsValid());
    TestEqual(TEXT("GC 후 투영"), F.B->GetItems()[0]->GetQuantity(), 6);
    F.A->Bind(nullptr, NAME_None);
    F.B->Bind(nullptr, NAME_None);
    F.Interaction->Bind(nullptr);
    CollectGarbage(RF_NoFlags);
    TestFalse(TEXT("해제 후 owner 수거"), WeakModel.IsValid());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestVM_UnbindDuringNotification, "Duckov.ViewModel.UnbindDuringNotification", TestFlags)
bool TestVM_UnbindDuringNotification::RunTest(const FString& Parameters)
{
    FViewModelFixture F;
    TStrongObjectPtr<UItemViewModel> Item(F.A->GetItems()[0]);
    Item->AddFieldValueChangedDelegate(UItemViewModel::FFieldNotificationClassDescriptor::GetAnchorCell,
        INotifyFieldValueChanged::FFieldValueChangedDelegate::CreateLambda([&](UObject*, UE::FieldNotification::FFieldId)
        {
            F.A->Bind(nullptr, NAME_None);
        }));
    TestEqual(TEXT("통지 도중 화면 해제"), F.Model->TryMove(TEXT("A"), TEXT("A"), 1, FIntPoint(1, 1), false), EInventoryOperationFailure::None);
    TestFalse(TEXT("콜백 종료 후 해제 반영"), F.A->IsAvailable());
    TestTrue(TEXT("목록 안전하게 비움"), F.A->GetItems().IsEmpty());
    TestFalse(TEXT("외부 Item VM 무효화"), Item->IsAvailable());
    TestEqual(TEXT("Model 이동은 유지"), F.Model->FindItem(TEXT("A"), 1)->AnchorCell, FIntPoint(1, 1));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestVM_InteractionCloseDuringNotification, "Duckov.ViewModel.InteractionCloseDuringNotification", TestFlags)
bool TestVM_InteractionCloseDuringNotification::RunTest(const FString& Parameters)
{
    FViewModelFixture F;
    F.A->Bind(nullptr, NAME_None);
    F.B->Bind(nullptr, NAME_None);
    const auto Handle = F.Interaction->AddFieldValueChangedDelegate(UInteractionViewModel::FFieldNotificationClassDescriptor::IsDragging,
        INotifyFieldValueChanged::FFieldValueChangedDelegate::CreateLambda([&](UObject*, UE::FieldNotification::FFieldId)
        {
            F.Interaction->Bind(nullptr);
        }));
    F.Interaction->BeginDrag(TEXT("A"), 1);
    TestFalse(TEXT("통지 중 화면 닫힘 요청 보존"), F.Interaction->IsDragging());
    TestEqual(TEXT("닫힘 뒤 owner 해제"), F.Interaction->Sort(TEXT("A")), EInventoryOperationFailure::InvalidContainer);
    F.Interaction->RemoveFieldValueChangedDelegate(UInteractionViewModel::FFieldNotificationClassDescriptor::IsDragging, Handle);
    F.Interaction->Bind(F.Model.Get());
    F.Interaction->AddFieldValueChangedDelegate(UInteractionViewModel::FFieldNotificationClassDescriptor::IsDragging,
        INotifyFieldValueChanged::FFieldValueChangedDelegate::CreateLambda([&](UObject*, UE::FieldNotification::FFieldId)
        {
            F.Interaction->Cancel();
        }));
    F.Interaction->BeginDrag(TEXT("A"), 1);
    TestFalse(TEXT("통지 중 취소 요청 보존"), F.Interaction->IsDragging());
    TWeakObjectPtr<UInventoryModel> WeakModel = F.Model.Get();
    F.Model.Reset();
    F.Interaction->Bind(nullptr);
    CollectGarbage(RF_NoFlags);
    TestFalse(TEXT("닫힘 후 Model 참조 잔류 없음"), WeakModel.IsValid());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestVM_CommandRevalidatesFailureText, "Duckov.ViewModel.CommandRevalidatesFailureText", TestFlags)
bool TestVM_CommandRevalidatesFailureText::RunTest(const FString& Parameters)
{
    FViewModelFixture F;
    F.Interaction->BeginDrag(TEXT("A"), 1);
    TestEqual(TEXT("크기 변경 전 유효 preview"), F.Interaction->Preview(TEXT("B"), FIntPoint(2, 2)), EInventoryOperationFailure::None);
    TestEqual(TEXT("명령 반환은 resize 성공"), F.Interaction->Resize(TEXT("B"), FIntPoint(1, 2)), EInventoryOperationFailure::None);
    TestEqual(TEXT("resize 후 preview 경계 초과"), F.Interaction->GetPreviewFailure(), EInventoryOperationFailure::NoSpace);
    TestFalse(TEXT("preview 실패 문구 일치"), F.Interaction->GetFailureText().IsEmpty());
    TestTrue(TEXT("실패 preview는 drag 유지"), F.Interaction->IsDragging());
    return true;
}
#endif
