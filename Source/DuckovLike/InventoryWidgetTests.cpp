#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "InventoryGridWidget.h"
#include "InventoryItemWidget.h"
#include "InventoryScreenWidget.h"
#include "InteractionViewModel.h"
#include "InventoryModel.h"
#include "ItemViewModel.h"
#include "ItemDefinitionRow.h"
#include "UObject/StrongObjectPtr.h"
#if WITH_EDITOR
#include "InventoryPerformanceProbe.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#endif

namespace
{
constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
}

struct FInventoryScreenWidgetTestAccess
{
    static void Attach(UInventoryScreenWidget* Screen, UInventoryModel* Model, UItemViewModel* Item)
    {
        Screen->Interaction = NewObject<UInteractionViewModel>(Screen);
        Screen->Interaction->Bind(Model);
        Screen->DraggedItem = Item;
        const auto Callback = INotifyFieldValueChanged::FFieldValueChangedDelegate::CreateUObject(
            Screen, &UInventoryScreenWidget::OnDragStateChanged);
        Screen->DragStateHandle = Screen->Interaction->AddFieldValueChangedDelegate(
            UInteractionViewModel::FFieldNotificationClassDescriptor::IsDragging, Callback);
    }
    static void Capture(UInventoryScreenWidget* Screen) { Screen->bPointerCaptured = true; }
    static bool Captured(const UInventoryScreenWidget* Screen) { return Screen->bPointerCaptured; }
    static UInteractionViewModel* Interaction(UInventoryScreenWidget* Screen) { return Screen->Interaction; }
};

#if WITH_EDITOR
struct FInventoryItemWidgetTestAccess
{
    static void SetText(UInventoryItemWidget* Widget, UTextBlock* Text) { Widget->ItemText = Text; }
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestUI_ItemNotificationAndRebind, "Duckov.UI.ItemNotificationAndRebind", TestFlags)
bool TestUI_ItemNotificationAndRebind::RunTest(const FString& Parameters)
{
    const int32 OriginalCounter = FItemInstanceIdAllocator::GetNextInstanceId();
    TStrongObjectPtr<UDataTable> Table(NewObject<UDataTable>());
    Table->RowStruct = FItemDefinitionRow::StaticStruct();
    FItemDefinitionRow Definition;
    Definition.Size = FIntPoint(1, 2);
    Definition.bStackable = true;
    Definition.MaxStack = 10;
    Table->AddRow(TEXT("TestItem"), Definition);
    FInventorySaveRecord Record;
    Record.NextInstanceId = 3;
    auto& Container = Record.Containers.AddDefaulted_GetRef();
    Container.ContainerId = TEXT("A");
    Container.GridSize = FIntPoint(6, 6);
    for (int32 Index = 0; Index < 2; ++Index)
    {
        auto& Saved = Container.Items.AddDefaulted_GetRef();
        Saved.InstanceId = Index + 1;
        Saved.DefinitionTable = FSoftObjectPath(Table.Get());
        Saved.DefinitionRowName = TEXT("TestItem");
        Saved.AnchorCell = FIntPoint(Index * 2, 0);
        Saved.Quantity = Index == 0 ? 8 : 9;
    }
    TStrongObjectPtr<UInventoryModel> Model(NewObject<UInventoryModel>());
    const bool bLoaded = TestEqual(TEXT("Item notification fixture Load"), Model->Load(Record), EInventorySaveFailure::None);
    if (!bLoaded)
    {
        FItemInstanceIdAllocator::ResetInstanceIdCounter_ForTests(OriginalCounter);
        return false;
    }
    TStrongObjectPtr<UItemViewModel> SourceVM(NewObject<UItemViewModel>());
    TStrongObjectPtr<UItemViewModel> TargetVM(NewObject<UItemViewModel>());
    SourceVM->Bind(Model.Get(), TEXT("A"), 1);
    TargetVM->Bind(Model.Get(), TEXT("A"), 2);
    TStrongObjectPtr<UCanvasPanel> Canvas(NewObject<UCanvasPanel>());
    TStrongObjectPtr<UInventoryItemWidget> Widget(NewObject<UInventoryItemWidget>());
    TStrongObjectPtr<UTextBlock> Label(NewObject<UTextBlock>());
    FInventoryItemWidgetTestAccess::SetText(Widget.Get(), Label.Get());
    UCanvasPanelSlot* Slot = Canvas->AddChildToCanvas(Widget.Get());
    FInventoryPerformanceProbe Probe;
    Probe.TrackedWidgets.Add(Widget.Get());
    FInventoryPerformanceProbe* PreviousProbe = GInventoryPerformanceProbe;
    GInventoryPerformanceProbe = &Probe;
    Widget->Bind(SourceVM.Get());

    const TArray<UE::FieldNotification::FFieldId> Fields = {
        UItemViewModel::FFieldNotificationClassDescriptor::IsAvailable,
        UItemViewModel::FFieldNotificationClassDescriptor::GetInstanceId,
        UItemViewModel::FFieldNotificationClassDescriptor::GetDefinitionRowName,
        UItemViewModel::FFieldNotificationClassDescriptor::GetQuantity,
        UItemViewModel::FFieldNotificationClassDescriptor::GetAnchorCell,
        UItemViewModel::FFieldNotificationClassDescriptor::IsRotated,
        UItemViewModel::FFieldNotificationClassDescriptor::GetFootprint
    };
    TMap<FName, int32> FieldCounts;
    TArray<FDelegateHandle> Handles;
    for (const auto Field : Fields)
    {
        Handles.Add(SourceVM->AddFieldValueChangedDelegate(Field,
            INotifyFieldValueChanged::FFieldValueChangedDelegate::CreateLambda(
                [&FieldCounts](UObject*, UE::FieldNotification::FFieldId Id) { ++FieldCounts.FindOrAdd(Id.GetName()); })));
    }
    Probe.Reset();
    TestEqual(TEXT("position move 성공"), Model->TryMove(TEXT("A"), TEXT("A"), 1, FIntPoint(0, 2), false),
        EInventoryOperationFailure::None);
    SourceVM->NotifyChanged();
    TestEqual(TEXT("position Widget Refresh 1"), Probe.ItemRefresh, 1);
    TestTrue(TEXT("position Widget 위치"), Slot && Slot->GetPosition() == FVector2D(0.f, 104.f));
    TestTrue(TEXT("position Widget 크기 유지"), Slot && Slot->GetSize() == FVector2D(48.f, 100.f));
    TestTrue(TEXT("position quantity text 유지"), Label->GetText().ToString().Contains(TEXT("x8")));
    TestTrue(TEXT("position tooltip 크기/수량 유지"), Widget->GetToolTipText().ToString().Contains(TEXT("1 × 2"))
        && Widget->GetToolTipText().ToString().Contains(TEXT("수량 8")));
    for (const auto Field : Fields)
    {
        TestEqual(FString::Printf(TEXT("독립 FieldNotify %s"), *Field.GetName().ToString()),
            FieldCounts.FindRef(Field.GetName()), 1);
    }
    for (int32 Index = 0; Index < Fields.Num(); ++Index)
    {
        SourceVM->RemoveFieldValueChangedDelegate(Fields[Index], Handles[Index]);
    }

    Probe.Reset();
    TestEqual(TEXT("rotation move 성공"), Model->TryMove(TEXT("A"), TEXT("A"), 1, FIntPoint(1, 2), true),
        EInventoryOperationFailure::None);
    SourceVM->NotifyChanged();
    TestEqual(TEXT("rotation Widget Refresh 1"), Probe.ItemRefresh, 1);
    TestTrue(TEXT("rotation Widget 위치/크기"), Slot && Slot->GetPosition() == FVector2D(52.f, 104.f)
        && Slot->GetSize() == FVector2D(100.f, 48.f));
    TestTrue(TEXT("rotation tooltip 크기"), Widget->GetToolTipText().ToString().Contains(TEXT("2 × 1")));

    Probe.Reset();
    TestEqual(TEXT("partial stack 성공"), Model->TryStack(TEXT("A"), TEXT("A"), 1, 2),
        EInventoryOperationFailure::None);
    SourceVM->NotifyChanged();
    TestEqual(TEXT("quantity Widget Refresh 1"), Probe.ItemRefresh, 1);
    TestTrue(TEXT("quantity Widget text"), Label->GetText().ToString().Contains(TEXT("x7")));
    TestTrue(TEXT("quantity Widget tooltip"), Widget->GetToolTipText().ToString().Contains(TEXT("수량 7")));

    Widget->Bind(TargetVM.Get());
    Widget->Bind(TargetVM.Get());
    TestFalse(TEXT("rebind 이전 VM event 해제"), SourceVM->OnChanged().IsBound());
    Probe.Reset();
    SourceVM->NotifyChanged();
    TestEqual(TEXT("stale VM Widget Refresh 0"), Probe.ItemRefresh, 0);
    TargetVM->NotifyChanged();
    TestEqual(TEXT("rebind 중복 구독 없음"), Probe.ItemRefresh, 1);
    TestTrue(TEXT("rebind 새 VM quantity"), Label->GetText().ToString().Contains(TEXT("x10")));

    FInventorySaveRecord Empty = Record;
    Empty.Containers[0].Items.Reset();
    TestEqual(TEXT("removal fixture Load"), Model->Load(Empty), EInventorySaveFailure::None);
    Probe.Reset();
    TargetVM->NotifyChanged();
    TestEqual(TEXT("removal Widget Refresh 1"), Probe.ItemRefresh, 1);
    TestFalse(TEXT("removal VM unavailable"), TargetVM->IsAvailable());
    TestTrue(TEXT("removal Widget text 비움"), Label->GetText().IsEmpty());
    TestTrue(TEXT("removal Widget tooltip 비움"), Widget->GetToolTipText().IsEmpty());
    Widget->Bind(nullptr);
    TestFalse(TEXT("unbind VM event 해제"), TargetVM->OnChanged().IsBound());
    GInventoryPerformanceProbe = PreviousProbe;
    FItemInstanceIdAllocator::ResetInstanceIdCounter_ForTests(OriginalCounter);
    return bLoaded;
}
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestUI_GridPixelBoundaries, "Duckov.UI.GridPixelBoundaries", TestFlags)
bool TestUI_GridPixelBoundaries::RunTest(const FString& Parameters)
{
    FIntPoint Cell(-1, -1);
    const FIntPoint Grid(4, 3);
    TestTrue(TEXT("첫 셀 시작"), UInventoryGridWidget::PixelToCell(FVector2D::ZeroVector, Grid, Cell));
    TestEqual(TEXT("첫 셀"), Cell, FIntPoint::ZeroValue);
    TestTrue(TEXT("셀 경계는 다음 셀"), UInventoryGridWidget::PixelToCell(FVector2D(52.f, 51.99f), Grid, Cell));
    TestEqual(TEXT("두 번째 열"), Cell, FIntPoint(1, 0));
    TestTrue(TEXT("마지막 픽셀"), UInventoryGridWidget::PixelToCell(FVector2D(207.99f, 155.99f), Grid, Cell));
    TestEqual(TEXT("마지막 셀"), Cell, FIntPoint(3, 2));
    TestFalse(TEXT("오른쪽 경계 제외"), UInventoryGridWidget::PixelToCell(FVector2D(208.f, 0.f), Grid, Cell));
    TestFalse(TEXT("아래 경계 제외"), UInventoryGridWidget::PixelToCell(FVector2D(0.f, 156.f), Grid, Cell));
    TestFalse(TEXT("음수 좌표 제외"), UInventoryGridWidget::PixelToCell(FVector2D(-0.01f, 0.f), Grid, Cell));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestUI_ItemTooltipLifetime, "Duckov.UI.ItemTooltipLifetime", TestFlags)
bool TestUI_ItemTooltipLifetime::RunTest(const FString& Parameters)
{
    const int32 OriginalCounter = FItemInstanceIdAllocator::GetNextInstanceId();
    TStrongObjectPtr<UDataTable> Table(NewObject<UDataTable>());
    Table->RowStruct = FItemDefinitionRow::StaticStruct();
    FItemDefinitionRow Definition;
    Definition.Size = FIntPoint(1, 2);
    Definition.bStackable = true;
    Definition.MaxStack = 5;
    Table->AddRow(TEXT("TestItem"), Definition);
    FInventorySaveRecord Record;
    Record.NextInstanceId = 2;
    auto& Container = Record.Containers.AddDefaulted_GetRef();
    Container.ContainerId = TEXT("A");
    Container.GridSize = FIntPoint(4, 4);
    auto& SavedItem = Container.Items.AddDefaulted_GetRef();
    SavedItem.InstanceId = 1;
    SavedItem.DefinitionTable = FSoftObjectPath(Table.Get());
    SavedItem.DefinitionRowName = TEXT("TestItem");
    SavedItem.Quantity = 3;
    TStrongObjectPtr<UInventoryModel> Model(NewObject<UInventoryModel>());
    if (!TestEqual(TEXT("초기 Model"), Model->Load(Record), EInventorySaveFailure::None))
    {
        FItemInstanceIdAllocator::ResetInstanceIdCounter_ForTests(OriginalCounter);
        return false;
    }
    TStrongObjectPtr<UItemViewModel> Item(NewObject<UItemViewModel>());
    Item->Bind(Model.Get(), TEXT("A"), 1);
    TStrongObjectPtr<UInventoryItemWidget> Widget(NewObject<UInventoryItemWidget>());
    Widget->Bind(Item.Get());
    TestTrue(TEXT("Definition 이름 표시"), Widget->GetToolTipText().ToString().Contains(TEXT("TestItem")));
    TestTrue(TEXT("크기와 수량 표시"), Widget->GetToolTipText().ToString().Contains(TEXT("1 × 2"))
        && Widget->GetToolTipText().ToString().Contains(TEXT("3")));
    TestEqual(TEXT("회전 이동"), Model->TryMove(TEXT("A"), TEXT("A"), 1, FIntPoint(1, 1), true),
        EInventoryOperationFailure::None);
    Item->NotifyChanged();
    TestTrue(TEXT("회전 후 tooltip 갱신"), Widget->GetToolTipText().ToString().Contains(TEXT("2 × 1")));
    Widget->Bind(nullptr);
    TestTrue(TEXT("위젯 해제 후 tooltip 비움"), Widget->GetToolTipText().IsEmpty());
    FItemInstanceIdAllocator::ResetInstanceIdCounter_ForTests(OriginalCounter);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestUI_ScreenDragState, "Duckov.UI.ScreenDragState", TestFlags)
bool TestUI_ScreenDragState::RunTest(const FString& Parameters)
{
    const int32 OriginalCounter = FItemInstanceIdAllocator::GetNextInstanceId();
    TStrongObjectPtr<UDataTable> Table(NewObject<UDataTable>());
    Table->RowStruct = FItemDefinitionRow::StaticStruct();
    FItemDefinitionRow Definition;
    Definition.Size = FIntPoint(1, 2);
    Definition.bStackable = true;
    Definition.MaxStack = 5;
    Table->AddRow(TEXT("TestItem"), Definition);
    FInventorySaveRecord Record;
    Record.NextInstanceId = 3;
    for (int32 Index = 0; Index < 2; ++Index)
    {
        auto& Container = Record.Containers.AddDefaulted_GetRef();
        Container.ContainerId = Index == 0 ? TEXT("A") : TEXT("B");
        Container.GridSize = FIntPoint(4, 4);
        auto& SavedItem = Container.Items.AddDefaulted_GetRef();
        SavedItem.InstanceId = Index + 1;
        SavedItem.DefinitionTable = FSoftObjectPath(Table.Get());
        SavedItem.DefinitionRowName = TEXT("TestItem");
        SavedItem.Quantity = Index == 0 ? 2 : 3;
    }
    TStrongObjectPtr<UInventoryModel> Model(NewObject<UInventoryModel>());
    if (!TestEqual(TEXT("초기 Model"), Model->Load(Record), EInventorySaveFailure::None))
    {
        FItemInstanceIdAllocator::ResetInstanceIdCounter_ForTests(OriginalCounter);
        return false;
    }
    TStrongObjectPtr<UItemViewModel> Item(NewObject<UItemViewModel>());
    Item->Bind(Model.Get(), TEXT("A"), 1);
    TStrongObjectPtr<UInventoryScreenWidget> Screen(NewObject<UInventoryScreenWidget>());
    FInventoryScreenWidgetTestAccess::Attach(Screen.Get(), Model.Get(), Item.Get());
    UInteractionViewModel* Interaction = FInventoryScreenWidgetTestAccess::Interaction(Screen.Get());
    TestEqual(TEXT("drag 시작"), Interaction->BeginDrag(TEXT("A"), 1), EInventoryOperationFailure::None);
    FInventoryScreenWidgetTestAccess::Capture(Screen.Get());
    TestEqual(TEXT("점유된 대상 preview"), Interaction->Preview(TEXT("B"), FIntPoint::ZeroValue), EInventoryOperationFailure::Occupied);
    TestEqual(TEXT("실패 Drop"), Interaction->Drop(), EInventoryOperationFailure::Occupied);
    TestTrue(TEXT("실패 뒤 화면 capture 상태 유지"), FInventoryScreenWidgetTestAccess::Captured(Screen.Get()));
    TestTrue(TEXT("실패 뒤 원본 유지"), Model->FindItem(TEXT("A"), 1) != nullptr);
    TestEqual(TEXT("회전"), Interaction->Rotate(), EInventoryOperationFailure::Occupied);
    TestEqual(TEXT("다음 위치 preview"), Interaction->Preview(TEXT("B"), FIntPoint(1, 2)), EInventoryOperationFailure::None);
    TestEqual(TEXT("교차 Container Drop"), Interaction->Drop(), EInventoryOperationFailure::None);
    TestFalse(TEXT("성공 뒤 화면 capture 해제"), FInventoryScreenWidgetTestAccess::Captured(Screen.Get()));
    TestTrue(TEXT("대상에 회전한 아이템"), Model->FindItem(TEXT("B"), 1) && Model->FindItem(TEXT("B"), 1)->bRotated);

    Model->Load(Record);
    Item->Bind(Model.Get(), TEXT("A"), 1);
    Interaction->BeginDrag(TEXT("A"), 1);
    FInventoryScreenWidgetTestAccess::Capture(Screen.Get());
    Model->Load(Record);
    TestFalse(TEXT("외부 reset은 capture 정리"), FInventoryScreenWidgetTestAccess::Captured(Screen.Get()));
    TestFalse(TEXT("외부 reset은 drag 취소"), Interaction->IsDragging());

    Interaction->BeginDrag(TEXT("A"), 1);
    FInventoryScreenWidgetTestAccess::Capture(Screen.Get());
    Model->TryStack(TEXT("A"), TEXT("B"), 1, 2);
    TestFalse(TEXT("원본 제거는 capture 정리"), FInventoryScreenWidgetTestAccess::Captured(Screen.Get()));
    TestFalse(TEXT("원본 제거는 drag 취소"), Interaction->IsDragging());

    Model->Load(Record);
    Interaction->BeginDrag(TEXT("A"), 1);
    FInventoryScreenWidgetTestAccess::Capture(Screen.Get());
    Screen->CancelItemDrag();
    TestFalse(TEXT("ESC 경로 취소는 capture 정리"), FInventoryScreenWidgetTestAccess::Captured(Screen.Get()));
    TestFalse(TEXT("ESC 경로 drag 취소"), Interaction->IsDragging());
    FItemInstanceIdAllocator::ResetInstanceIdCounter_ForTests(OriginalCounter);
    return true;
}
#endif
