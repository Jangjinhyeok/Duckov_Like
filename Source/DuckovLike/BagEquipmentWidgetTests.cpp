#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "BagEquipmentViewModel.h"
#include "ContainerViewModel.h"
#include "InteractionViewModel.h"
#include "InventoryDemoPlayerController.h"
#include "InventoryGridWidget.h"
#include "InventoryModel.h"
#include "InventoryScreenWidget.h"
#include "ItemDefinitionRow.h"
#include "ItemViewModel.h"
#include "Components/Button.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace BagEquipmentWidgetTests
{
namespace
{
constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

template<typename T>
T* Field(UObject* Owner, FName Name)
{
    const FObjectProperty* Property = Owner ? FindFProperty<FObjectProperty>(Owner->GetClass(), Name) : nullptr;
    return Property ? Cast<T>(Property->GetObjectPropertyValue_InContainer(Owner)) : nullptr;
}

bool SameState(UInventoryModel* Model, const FInventorySaveRecord& Before)
{
    FInventorySaveRecord After;
    return Model && Model->Save(After) == EInventorySaveFailure::None
        && FInventorySaveRecord::StaticStruct()->CompareScriptStruct(&Before, &After, 0);
}

UItemViewModel* FindItem(UContainerViewModel* VM, int32 Id)
{
    if (!VM) { return nullptr; }
    const auto* Found = VM->GetItems().FindByPredicate([Id](const auto& Item) { return Item->GetInstanceId() == Id; });
    return Found ? Found->Get() : nullptr;
}

void Capture(FAutomationTestBase& Test, UInventoryScreenWidget* Screen, const TCHAR* Name)
{
    const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation/BagEquipment"));
    IFileManager::Get().MakeDirectory(*Directory, true);
    const FString Path = FPaths::Combine(Directory, FString(Name) + TEXT(".png"));
    TArray<FColor> Pixels;
    FIntVector Size = FIntVector::ZeroValue;
    bool bSaved = Screen && FSlateApplication::Get().TakeScreenshot(Screen->TakeWidget(), Pixels, Size)
        && Size.X > 0 && Size.Y > 0 && Pixels.Num() == static_cast<int64>(Size.X) * Size.Y;
    if (bSaved)
    {
        TArray64<uint8> Compressed;
        FImageUtils::PNGCompressImageArray(Size.X, Size.Y, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), Compressed);
        bSaved = !Compressed.IsEmpty() && FFileHelper::SaveArrayToFile(Compressed, *Path);
    }
    if (!bSaved) { IFileManager::Get().Delete(*Path, false, true); }
    const FString Status = FString::Printf(TEXT("%s %s %s"), bSaved ? TEXT("generated") : TEXT("not_run"),
        *FDateTime::UtcNow().ToIso8601(), *Path);
    FFileHelper::SaveStringToFile(Status, *(Path + TEXT(".status.txt")));
    Test.AddInfo(Status);
}

class FWaitForBagCondition final : public IAutomationLatentCommand
{
public:
    FWaitForBagCondition(FAutomationTestBase& InTest, TFunction<bool()> InCondition)
        : Test(InTest), Condition(MoveTemp(InCondition)) {}
    virtual bool Update() override
    {
        if (Condition()) { return true; }
        if (Started == 0.0) { Started = FPlatformTime::Seconds(); }
        if (FPlatformTime::Seconds() - Started < 15.0) { return false; }
        Test.AddError(TEXT("가방 장착 PIE 준비 시간 초과"));
        return true;
    }
private:
    FAutomationTestBase& Test;
    TFunction<bool()> Condition;
    double Started = 0.0;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBagEquipmentVMLifecycle, "Duckov.ViewModel.BagEquipmentLifecycle", Flags)
bool FBagEquipmentVMLifecycle::RunTest(const FString& Parameters)
{
    const int32 OriginalCounter = FItemInstanceIdAllocator::GetNextInstanceId();
    ON_SCOPE_EXIT { FItemInstanceIdAllocator::ResetInstanceIdCounter_ForTests(OriginalCounter); };
    TStrongObjectPtr<UDataTable> Table(NewObject<UDataTable>());
    TStrongObjectPtr<UInventoryModel> Model(NewObject<UInventoryModel>());
    TStrongObjectPtr<UBagEquipmentViewModel> VM(NewObject<UBagEquipmentViewModel>());
    Table->RowStruct = FItemDefinitionRow::StaticStruct();
    FItemDefinitionRow Small;
    Small.BagGridSize = FIntPoint(4, 4);
    FItemDefinitionRow Large = Small;
    Large.BagGridSize = FIntPoint(6, 4);
    Table->AddRow(TEXT("SmallBag"), Small);
    Table->AddRow(TEXT("LargeBag"), Large);
    FInventorySaveRecord Record;
    Record.NextInstanceId = 2;
    for (const FName Name : {FName(TEXT("Stash")), FName(TEXT("BagSlot")), FName(TEXT("Bag"))})
    {
        auto& Container = Record.Containers.AddDefaulted_GetRef();
        Container.ContainerId = Name;
        Container.GridSize = Name == TEXT("Bag") ? FIntPoint(4, 4) : FIntPoint(1, 1);
        if (Name == TEXT("Bag")) { continue; }
        auto& Item = Container.Items.AddDefaulted_GetRef();
        Item.InstanceId = Name == TEXT("BagSlot") ? 0 : 1;
        Item.DefinitionTable = FSoftObjectPath(Table.Get());
        Item.DefinitionRowName = Name == TEXT("BagSlot") ? TEXT("SmallBag") : TEXT("LargeBag");
    }
    TestEqual(TEXT("VM fixture load"), Model->Load(Record), EInventorySaveFailure::None);
    VM->Bind(Model.Get());
    TestFalse(TEXT("미바인딩 상태"), VM->HasBinding());
    int32 Notifications = 0;
    const FDelegateHandle Handle = VM->AddFieldValueChangedDelegate(
        UBagEquipmentViewModel::FFieldNotificationClassDescriptor::GetEquippedText,
        INotifyFieldValueChanged::FFieldValueChangedDelegate::CreateLambda([&](UObject*, UE::FieldNotification::FFieldId)
            { ++Notifications; }));
    TestEqual(TEXT("binding 이벤트"), Model->BindBagSlot(TEXT("BagSlot"), TEXT("Bag"), TEXT("Stash")), EInventoryOperationFailure::None);
    TestEqual(TEXT("binding FieldNotify 1회"), Notifications, 1);
    TestEqual(TEXT("source large query"), VM->GetLargeBagInstanceId(), 1);
    VM->Bind(Model.Get());
    TestEqual(TEXT("같은 Model bind 통지 중복 없음"), Notifications, 1);
    TestEqual(TEXT("Model equip"), Model->TryEquipBag(1), EInventoryOperationFailure::None);
    TestEqual(TEXT("equip FieldNotify 1회"), Notifications, 2);
    TestTrue(TEXT("현재 capacity 조회"), VM->GetEquippedText().ToString().Contains(TEXT("6x4")));
    TestEqual(TEXT("반환된 source small query"), VM->GetSmallBagInstanceId(), 0);
    FInventorySaveRecord Bound;
    Model->Save(Bound);
    TestEqual(TEXT("Load 무효화"), Model->Load(Bound), EInventorySaveFailure::None);
    TestFalse(TEXT("Load 후 binding 숨김"), VM->HasBinding());
    TestTrue(TEXT("Load 후 text 없음"), VM->GetEquippedText().IsEmpty());
    TestEqual(TEXT("명시 재검증"), Model->BindBagSlot(TEXT("BagSlot"), TEXT("Bag"), TEXT("Stash")), EInventoryOperationFailure::None);
    const FDelegateHandle CloseHandle = VM->AddFieldValueChangedDelegate(
        UBagEquipmentViewModel::FFieldNotificationClassDescriptor::HasBinding,
        INotifyFieldValueChanged::FFieldValueChangedDelegate::CreateLambda([&](UObject*, UE::FieldNotification::FFieldId)
            { if (VM->HasBinding()) { VM->Bind(nullptr); } }));
    TestEqual(TEXT("통지 중 reentrant 해제"), Model->TryEquipBag(0), EInventoryOperationFailure::None);
    TestFalse(TEXT("지연 해제 반영"), VM->HasBinding());
    const int32 DetachedNotifications = Notifications;
    Model->TryEquipBag(1);
    TestEqual(TEXT("해제 뒤 Model 구독 0"), Notifications, DetachedNotifications);
    VM->RemoveFieldValueChangedDelegate(UBagEquipmentViewModel::FFieldNotificationClassDescriptor::HasBinding, CloseHandle);
    VM->RemoveFieldValueChangedDelegate(UBagEquipmentViewModel::FFieldNotificationClassDescriptor::GetEquippedText, Handle);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBagEquipmentPIE, "Duckov.UI.PIEBagEquipment", Flags)
bool FBagEquipmentPIE::RunTest(const FString& Parameters)
{
    if (!TestTrue(TEXT("Slate 준비"), FSlateApplication::IsInitialized())) { return false; }
    struct FScene
    {
        TWeakObjectPtr<AInventoryDemoPlayerController> Controller;
        TWeakObjectPtr<UInventoryScreenWidget> Screen;
        TWeakObjectPtr<UInventoryModel> Model;
        FInventorySaveRecord Baseline;
        int32 OriginalCounter = FItemInstanceIdAllocator::GetNextInstanceId();
        int32 Notifications = 0;
        FDelegateHandle VMHandle;
        bool bPrepared = false;
    };
    const TSharedRef<FScene> Scene = MakeShared<FScene>();
    if (!TestTrue(TEXT("기존 데모 map PIE"), AutomationOpenMap(TEXT("/Game/Maps/L_InventoryDemo")))) { return false; }
    ADD_LATENT_AUTOMATION_COMMAND(FWaitForBagCondition(*this, []
    {
        return GEditor && GEditor->PlayWorld
            && Cast<AInventoryDemoPlayerController>(UGameplayStatics::GetPlayerController(GEditor->PlayWorld, 0));
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
        auto* Controller = World ? Cast<AInventoryDemoPlayerController>(UGameplayStatics::GetPlayerController(World, 0)) : nullptr;
        UInventoryModel* Model = Field<UInventoryModel>(Controller, TEXT("Model"));
        if (!TestTrue(TEXT("기본 demo session"), Controller && Model)) { return; }
        Scene->Controller = Controller;
        Scene->Model = Model;
        TestEqual(TEXT("초기 snapshot"), Model->Save(Scene->Baseline), EInventorySaveFailure::None);
        Controller->ToggleInventory();
        UInventoryScreenWidget* Screen = Field<UInventoryScreenWidget>(Controller, TEXT("Screen"));
        Scene->Screen = Screen;
        UBagEquipmentViewModel* VM = Field<UBagEquipmentViewModel>(Screen, TEXT("BagEquipment"));
        if (!TestTrue(TEXT("실제 WBP와 gear VM"), Screen && Screen->IsActivated() && VM)) { return; }
        UWidget* Panel = Screen->GetWidgetFromName(TEXT("BagEquipmentPanel"));
        if (!TestNotNull(TEXT("native gear panel"), Panel)) { return; }
        TestEqual(TEXT("opt-in 전 panel collapsed"), Panel->GetVisibility(), ESlateVisibility::Collapsed);
        auto CheckFailure = [&](EInventoryOperationFailure Expected)
        {
            FInventorySaveRecord Before;
            Model->Save(Before);
            const TArray<int32> Cache = Model->FindContainer(TEXT("Stash"))->OccupancyCache;
            int32 Changes = 0;
            const FDelegateHandle Handle = Model->OnChanged().AddLambda([&](const auto&) { ++Changes; });
            TestEqual(TEXT("준비 실패 이유"), Controller->TryStartBagEquipmentDemo(), Expected);
            Model->OnChanged().Remove(Handle);
            TestTrue(TEXT("준비 실패 전체 상태와 counter 보존"), SameState(Model, Before));
            TestTrue(TEXT("준비 실패 occupancy 보존"), Model->FindContainer(TEXT("Stash"))->OccupancyCache == Cache);
            TestEqual(TEXT("준비 실패 통지 0"), Changes, 0);
        };
        UDataTable* Table = LoadObject<UDataTable>(nullptr, TEXT("/Game/Inventory/DT_ItemDefinitions.DT_ItemDefinitions"));
        const auto* Row = Table ? Table->FindRow<FItemDefinitionRow>(TEXT("LargeBag"), TEXT("BagUITest"), false) : nullptr;
        if (!TestNotNull(TEXT("저장된 가방 정의"), Row)) { return; }
        const FItemDefinitionRow Original = *Row;
        const bool bDirty = Table->GetOutermost()->IsDirty();
        {
            ON_SCOPE_EXIT { Table->AddRow(TEXT("LargeBag"), Original); Table->GetOutermost()->SetDirtyFlag(bDirty); };
            FItemDefinitionRow Invalid = Original;
            Invalid.BagGridSize = FIntPoint::ZeroValue;
            Table->AddRow(TEXT("LargeBag"), Invalid);
            CheckFailure(EInventoryOperationFailure::InvalidDefinition);
        }
        TestTrue(TEXT("준비 전 modal 열기"), Screen->OpenSplitDialog(TEXT("Stash"), FindItem(Field<UContainerViewModel>(Screen, TEXT("LeftVM")), 2)));
        CheckFailure(EInventoryOperationFailure::OperationInProgress);
        Screen->NativeOnKeyDown(Screen->GetCachedGeometry(), FKeyEvent(EKeys::Escape, FModifierKeysState(), 0, false, 0, 0));
        UInteractionViewModel* InitialInteraction = Field<UInteractionViewModel>(Screen, TEXT("Interaction"));
        TestEqual(TEXT("준비 전 drag 시작"), InitialInteraction->BeginDrag(TEXT("Stash"), 2), EInventoryOperationFailure::None);
        CheckFailure(EInventoryOperationFailure::OperationInProgress);
        Screen->CancelItemDrag();
        FInventorySaveRecord Full = Scene->Baseline;
        auto* Stash = Full.Containers.FindByPredicate([](const auto& C) { return C.ContainerId == TEXT("Stash"); });
        Stash->GridSize = FIntPoint(1, 1);
        Stash->Items.RemoveAll([](const auto& I) { return I.InstanceId != 2; });
        Stash->Items[0].AnchorCell = FIntPoint::ZeroValue;
        TestEqual(TEXT("full Stash fixture"), Model->Load(Full), EInventorySaveFailure::None);
        CheckFailure(EInventoryOperationFailure::NoSpace);
        TestEqual(TEXT("원본 복원"), Model->Load(Scene->Baseline), EInventorySaveFailure::None);
        FItemInstanceIdAllocator::ResetInstanceIdCounter_ForTests(MAX_int32 - 1);
        CheckFailure(EInventoryOperationFailure::InstanceIdExhausted);
        FItemInstanceIdAllocator::ResetInstanceIdCounter_ForTests(Scene->Baseline.NextInstanceId);
        FInventorySaveRecord Collision = Scene->Baseline;
        Collision.Containers.AddDefaulted_GetRef().ContainerId = TEXT("BagSlot");
        TestEqual(TEXT("slot 충돌 fixture"), Model->Load(Collision), EInventorySaveFailure::None);
        CheckFailure(EInventoryOperationFailure::InvalidContainer);
        TestEqual(TEXT("원본 재복원"), Model->Load(Scene->Baseline), EInventorySaveFailure::None);
        UContainerViewModel* RightVM = Field<UContainerViewModel>(Screen, TEXT("RightVM"));
        UItemViewModel* Medkit = FindItem(RightVM, 3);
        EInventoryOperationFailure Reentry = EInventoryOperationFailure::None;
        const FDelegateHandle ReentryHandle = Model->OnChanged().AddLambda([&](const FInventoryChangeSet& Change)
            { if (Change.bReset) { Reentry = Controller->TryStartBagEquipmentDemo(); } });
        TestEqual(TEXT("활성 화면에서 opt-in"), Controller->TryStartBagEquipmentDemo(), EInventoryOperationFailure::None);
        Model->OnChanged().Remove(ReentryHandle);
        TestEqual(TEXT("reset callback 재진입 차단"), Reentry, EInventoryOperationFailure::OperationInProgress);
        TestTrue(TEXT("원래 Bag Item VM identity"), FindItem(RightVM, 3) == Medkit);
        FInventorySaveRecord After;
        Model->Save(After);
        TestEqual(TEXT("신규 ID 2개만 사용"), After.NextInstanceId, Scene->Baseline.NextInstanceId + 2);
        After.NextInstanceId = Scene->Baseline.NextInstanceId;
        After.Containers.RemoveAll([](const auto& C) { return C.ContainerId == TEXT("BagSlot"); });
        After.Containers.FindByPredicate([](const auto& C) { return C.ContainerId == TEXT("Stash"); })->Items.RemoveAll(
            [](const auto& I) { return I.DefinitionRowName == TEXT("LargeBag"); });
        TestTrue(TEXT("기존 모든 container와 item field 보존"), FInventorySaveRecord::StaticStruct()->CompareScriptStruct(&After, &Scene->Baseline, 0));
        CheckFailure(EInventoryOperationFailure::InvalidContainer);
        TestTrue(TEXT("binding 후 panel 표시"), Panel->GetVisibility() == ESlateVisibility::Visible && VM->HasBinding());
        Scene->VMHandle = VM->AddFieldValueChangedDelegate(UBagEquipmentViewModel::FFieldNotificationClassDescriptor::GetEquippedText,
            INotifyFieldValueChanged::FFieldValueChangedDelegate::CreateLambda([Scene](UObject*, UE::FieldNotification::FFieldId) { ++Scene->Notifications; }));
        Scene->bPrepared = true;
    }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitForBagCondition(*this, [Scene]
    {
        return !Scene->bPrepared || (Scene->Screen.IsValid() && Scene->Screen->GetCachedGeometry().GetLocalSize().X > 0.f);
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        if (!Scene->bPrepared) { return; }
        UInventoryScreenWidget* Screen = Scene->Screen.Get();
        Capture(*this, Screen, TEXT("SmallBag"));
        UButton* Small = Cast<UButton>(Screen->GetWidgetFromName(TEXT("EquipSmallBagButton")));
        UButton* Large = Cast<UButton>(Screen->GetWidgetFromName(TEXT("EquipLargeBagButton")));
        UContainerViewModel* Right = Field<UContainerViewModel>(Screen, TEXT("RightVM"));
        UInventoryGridWidget* Grid = Cast<UInventoryGridWidget>(Screen->GetWidgetFromName(TEXT("RightGrid")));
        UItemViewModel* Medkit = FindItem(Right, 3);
        if (!TestTrue(TEXT("장착 controls와 Bag grid"), Small && Large && Grid && Right && Medkit)) { return; }
        TestFalse(TEXT("source에 없는 small 비활성"), Small->GetIsEnabled());
        TestTrue(TEXT("source large 활성"), Large->GetIsEnabled());
        const int32 Before = Scene->Notifications;
        Large->OnClicked.Broadcast();
        TestEqual(TEXT("grow FieldNotify 1회"), Scene->Notifications, Before + 1);
        TestEqual(TEXT("Bag VM grow"), Right->GetGridSize(), FIntPoint(6, 4));
        TestTrue(TEXT("같은 grid와 contents VM 유지"), Screen->GetWidgetFromName(TEXT("RightGrid")) == Grid && FindItem(Right, 3) == Medkit);
        USizeBox* Size = Cast<USizeBox>(Grid->GetWidgetFromName(TEXT("GridSize")));
        TestTrue(TEXT("grid 크기 binding"), Size && Size->GetWidthOverride() == 312.f && Size->GetHeightOverride() == 208.f);
        TestTrue(TEXT("old bag Stash 반환"), Scene->Model->FindItem(TEXT("Stash"), Scene->Baseline.NextInstanceId) != nullptr);
        TestTrue(TEXT("소형 재장착 가능"), Small->GetIsEnabled());
    }, 0.1f));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        if (!Scene->bPrepared) { return; }
        UInventoryScreenWidget* Screen = Scene->Screen.Get();
        UInventoryModel* Model = Scene->Model.Get();
        Capture(*this, Screen, TEXT("LargeBag"));
        UButton* Small = Cast<UButton>(Screen->GetWidgetFromName(TEXT("EquipSmallBagButton")));
        TestEqual(TEXT("Medkit 우측 경계로 이동"), Model->TryMove(TEXT("Bag"), TEXT("Bag"), 3, FIntPoint(4, 2), false), EInventoryOperationFailure::None);
        FInventorySaveRecord Before;
        Model->Save(Before);
        const TArray<int32> SourceCache = Model->FindContainer(TEXT("Stash"))->OccupancyCache;
        const TArray<int32> SlotCache = Model->FindContainer(TEXT("BagSlot"))->OccupancyCache;
        const TArray<int32> BagCache = Model->FindContainer(TEXT("Bag"))->OccupancyCache;
        const int32 BeforeNotifications = Scene->Notifications;
        Small->OnClicked.Broadcast();
        TestTrue(TEXT("축소 실패 slot/source/contents/counter 보존"), SameState(Model, Before));
        TestTrue(TEXT("축소 실패 모든 cache 보존"), Model->FindContainer(TEXT("Stash"))->OccupancyCache == SourceCache
            && Model->FindContainer(TEXT("BagSlot"))->OccupancyCache == SlotCache && Model->FindContainer(TEXT("Bag"))->OccupancyCache == BagCache);
        TestEqual(TEXT("실패 Model 통지 0"), Scene->Notifications, BeforeNotifications);
        UTextBlock* Failure = Field<UTextBlock>(Screen, TEXT("FailureText"));
        TestTrue(TEXT("기존 실패 label 표시"), Failure && Failure->GetText().ToString().Contains(TEXT("배치를 유지")));
    }, 0.1f));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        if (!Scene->bPrepared) { return; }
        UInventoryScreenWidget* Screen = Scene->Screen.Get();
        UInventoryModel* Model = Scene->Model.Get();
        Capture(*this, Screen, TEXT("ShrinkFailure"));
        UButton* Small = Cast<UButton>(Screen->GetWidgetFromName(TEXT("EquipSmallBagButton")));
        UButton* Large = Cast<UButton>(Screen->GetWidgetFromName(TEXT("EquipLargeBagButton")));
        UContainerViewModel* Left = Field<UContainerViewModel>(Screen, TEXT("LeftVM"));
        UContainerViewModel* Right = Field<UContainerViewModel>(Screen, TEXT("RightVM"));
        UInteractionViewModel* Interaction = Field<UInteractionViewModel>(Screen, TEXT("Interaction"));
        FInventorySaveRecord Before;
        Model->Save(Before);
        TestTrue(TEXT("분할 modal 열기"), Screen->OpenSplitDialog(TEXT("Stash"), FindItem(Left, 2)));
        TestFalse(TEXT("modal gear 비활성"), Small->GetIsEnabled());
        Small->OnClicked.Broadcast();
        TestTrue(TEXT("modal delegate guard"), SameState(Model, Before));
        Screen->NativeOnKeyDown(Screen->GetCachedGeometry(), FKeyEvent(EKeys::Escape, FModifierKeysState(), 0, false, 0, 0));
        TestTrue(TEXT("modal 취소 후 gear 복귀"), Small->GetIsEnabled());
        TestEqual(TEXT("drag 시작"), Interaction->BeginDrag(TEXT("Stash"), 2), EInventoryOperationFailure::None);
        TestFalse(TEXT("drag gear 비활성"), Small->GetIsEnabled());
        Small->OnClicked.Broadcast();
        TestEqual(TEXT("drag EquipBag 명령 guard"), Interaction->EquipBag(Scene->Baseline.NextInstanceId), EInventoryOperationFailure::OperationInProgress);
        TestTrue(TEXT("drag 중 상태 보존"), SameState(Model, Before));
        Screen->CancelItemDrag();
        TestTrue(TEXT("drag 취소 후 gear 복귀"), Small->GetIsEnabled());
        TestEqual(TEXT("Medkit 원래 범위 복귀"), Model->TryMove(TEXT("Bag"), TEXT("Bag"), 3, FIntPoint(2, 2), false), EInventoryOperationFailure::None);
        Small->OnClicked.Broadcast();
        TestEqual(TEXT("재시도 shrink 성공"), Right->GetGridSize(), FIntPoint(4, 4));
        TestTrue(TEXT("성공 뒤 failure label 해제"), Field<UTextBlock>(Screen, TEXT("FailureText"))->GetText().IsEmpty());
        const int32 LargeId = Scene->Baseline.NextInstanceId + 1;
        TestEqual(TEXT("source large를 Bag에 이전"), Model->TryMove(TEXT("Stash"), TEXT("Bag"), LargeId, FIntPoint::ZeroValue, false), EInventoryOperationFailure::None);
        TestFalse(TEXT("Stash에 없는 large 비활성"), Large->GetIsEnabled());
        Model->Save(Before);
        Large->OnClicked.Broadcast();
        TestTrue(TEXT("Bag source 장착 우회 없음"), SameState(Model, Before));
        TestEqual(TEXT("large Stash 복귀"), Model->TryMove(TEXT("Bag"), TEXT("Stash"), LargeId, FIntPoint::ZeroValue, false), EInventoryOperationFailure::None);
        for (int32 Index = 0; Index < 2; ++Index)
        {
            Scene->Controller->ToggleInventory();
            const int32 Detached = Scene->Notifications;
            Model->TrySort(TEXT("Stash"));
            TestEqual(TEXT("close 뒤 gear 구독 0"), Scene->Notifications, Detached);
            Scene->Controller->ToggleInventory();
            const int32 Active = Scene->Notifications;
            (Index == 0 ? Large : Small)->OnClicked.Broadcast();
            TestEqual(TEXT("reopen 뒤 장착 통지 1회"), Scene->Notifications, Active + 1);
        }
        Model->Save(Before);
        TestEqual(TEXT("활성 화면 reset"), Model->Load(Before), EInventorySaveFailure::None);
        TestFalse(TEXT("reset 시 gear binding 없음"), Field<UBagEquipmentViewModel>(Screen, TEXT("BagEquipment"))->HasBinding());
        TestEqual(TEXT("reset panel collapsed"), Screen->GetWidgetFromName(TEXT("BagEquipmentPanel"))->GetVisibility(), ESlateVisibility::Collapsed);
        TestEqual(TEXT("NeedsValidation opt-in 재실행 거부"), Scene->Controller->TryStartBagEquipmentDemo(), EInventoryOperationFailure::InvalidContainer);
        TestTrue(TEXT("NeedsValidation 실패 상태 보존"), SameState(Model, Before));
        TestEqual(TEXT("explicit binding 복원"), Model->BindBagSlot(TEXT("BagSlot"), TEXT("Bag"), TEXT("Stash")), EInventoryOperationFailure::None);
        TestEqual(TEXT("Raid 준비"), Scene->Controller->TryStartRaidDemo(), ERaidDemoFailure::None);
        TestEqual(TEXT("Raid 진입"), Scene->Controller->TryEnterRaid(), ERaidDemoFailure::None);
        Scene->Controller->ToggleInventory();
        TestFalse(TEXT("Raid small 비활성"), Small->GetIsEnabled());
        TestFalse(TEXT("Raid large 비활성"), Large->GetIsEnabled());
        Model->Save(Before);
        Small->OnClicked.Broadcast();
        Large->OnClicked.Broadcast();
        TestEqual(TEXT("Raid opt-in 거부"), Scene->Controller->TryStartBagEquipmentDemo(), EInventoryOperationFailure::InvalidContainer);
        TestTrue(TEXT("Raid native 명령 guard 상태 보존"), SameState(Model, Before));
    }, 0.1f));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([Scene]
    {
        UBagEquipmentViewModel* VM = Field<UBagEquipmentViewModel>(Scene->Screen.Get(), TEXT("BagEquipment"));
        if (VM) { VM->RemoveFieldValueChangedDelegate(UBagEquipmentViewModel::FFieldNotificationClassDescriptor::GetEquippedText, Scene->VMHandle); }
    }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    ADD_LATENT_AUTOMATION_COMMAND(FWaitForBagCondition(*this, [] { return !GEditor || !GEditor->PlayWorld; }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([Scene]
    {
        FItemInstanceIdAllocator::ResetInstanceIdCounter_ForTests(Scene->OriginalCounter);
    }, 0.f));
    return true;
}
}
#endif
