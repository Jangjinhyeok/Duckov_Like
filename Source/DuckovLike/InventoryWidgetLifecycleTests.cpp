#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "InventoryDemoPlayerController.h"
#include "InventoryGridWidget.h"
#include "InventoryItemWidget.h"
#include "InventoryModel.h"
#include "InventoryScreenWidget.h"
#include "InteractionViewModel.h"
#include "ItemViewModel.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/Border.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "UObject/UnrealType.h"
#include "Widgets/SWidget.h"

namespace
{
constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
constexpr const TCHAR* DemoMap = TEXT("/Game/Maps/L_InventoryDemo");

template <typename T>
T* ReflectedObject(UObject* Owner, FName PropertyName)
{
    const FObjectProperty* Property = Owner ? FindFProperty<FObjectProperty>(Owner->GetClass(), PropertyName) : nullptr;
    return Property ? Cast<T>(Property->GetObjectPropertyValue_InContainer(Owner)) : nullptr;
}

UInventoryItemWidget* FindAmmoWidget(UInventoryScreenWidget* Screen)
{
    UInventoryGridWidget* Grid = Cast<UInventoryGridWidget>(Screen->GetWidgetFromName(TEXT("LeftGrid")));
    UCanvasPanel* Canvas = Grid ? Cast<UCanvasPanel>(Grid->GetWidgetFromName(TEXT("GridCanvas"))) : nullptr;
    if (!Canvas) { return nullptr; }
    for (int32 Index = 0; Index < Canvas->GetChildrenCount(); ++Index)
    {
        UInventoryItemWidget* ItemWidget = Cast<UInventoryItemWidget>(Canvas->GetChildAt(Index));
        UItemViewModel* Item = ReflectedObject<UItemViewModel>(ItemWidget, TEXT("Item"));
        if (Item && Item->GetInstanceId() == 2) { return ItemWidget; }
    }
    return nullptr;
}

bool StartCapturedDrag(FAutomationTestBase& Test, UInventoryScreenWidget* Screen)
{
    UInventoryItemWidget* ItemWidget = FindAmmoWidget(Screen);
    if (!Test.TestNotNull(TEXT("실제 WBP Ammo child"), ItemWidget)) { return false; }
    FSlateApplication& Slate = FSlateApplication::Get();
    FWidgetPath Path;
    if (!Test.TestTrue(TEXT("PIE Slate item path"), Slate.FindPathToWidget(ItemWidget->TakeWidget(), Path))) { return false; }
    if (!Test.TestTrue(TEXT("PIE Slate path 유효"), Path.IsValid())) { return false; }
    // 위젯 탐색 경로에 없는 pointer 슬롯을 routing용 생성자로 채운다.
    TArray<FWidgetAndPointer> WidgetsAndPointers;
    for (int32 Index = 0; Index < Path.Widgets.Num(); ++Index)
    {
        WidgetsAndPointers.Emplace(Path.Widgets[Index], TOptional<FVirtualPointerPosition>());
    }
    Path = FWidgetPath(MakeArrayView(WidgetsAndPointers));
    const FVector2D Position = ItemWidget->GetCachedGeometry().GetAbsolutePosition() + FVector2D(12.f, 12.f);
    TSet<FKey> Buttons;
    Buttons.Add(EKeys::LeftMouseButton);
    const FPointerEvent MouseDown(Slate.GetUserIndexForMouse(), FSlateApplication::CursorPointerIndex, Position, Position, Buttons,
        EKeys::LeftMouseButton, 0.f, FModifierKeysState());
    const FReply Reply = Slate.RoutePointerDownEvent(Path, MouseDown);
    Test.TestTrue(TEXT("Slate mouse down handled"), Reply.IsEventHandled());
    Test.TestTrue(TEXT("화면이 실제 Slate mouse capture 소유"), Screen->HasMouseCaptureByUser(Slate.GetUserIndexForMouse()));
    UInteractionViewModel* Interaction = ReflectedObject<UInteractionViewModel>(Screen, TEXT("Interaction"));
    Test.TestTrue(TEXT("WBP drag 활성"), Interaction && Interaction->IsDragging());
    UInventoryGridWidget* Left = Cast<UInventoryGridWidget>(Screen->GetWidgetFromName(TEXT("LeftGrid")));
    UBorder* Preview = ReflectedObject<UBorder>(Left, TEXT("PreviewBorder"));
    Test.TestTrue(TEXT("실제 WBP preview 표시"), Preview && Preview->GetVisibility() == ESlateVisibility::HitTestInvisible);
    return Reply.IsEventHandled() && Screen->HasMouseCaptureByUser(Slate.GetUserIndexForMouse())
        && Interaction && Interaction->IsDragging() && Preview
        && Preview->GetVisibility() == ESlateVisibility::HitTestInvisible;
}

bool CheckCleared(FAutomationTestBase& Test, UInventoryScreenWidget* Screen)
{
    UInteractionViewModel* Interaction = ReflectedObject<UInteractionViewModel>(Screen, TEXT("Interaction"));
    const int32 UserIndex = FSlateApplication::Get().GetUserIndexForMouse();
    Test.TestFalse(TEXT("실제 Slate capture 해제"), Screen->HasMouseCaptureByUser(UserIndex));
    Test.TestTrue(TEXT("Interaction drag 취소"), Interaction && !Interaction->IsDragging());
    Test.TestTrue(TEXT("Interaction preview 대상 비움"), Interaction && Interaction->GetTargetContainerId().IsNone());
    for (const FName GridName : {FName(TEXT("LeftGrid")), FName(TEXT("RightGrid"))})
    {
        UInventoryGridWidget* Grid = Cast<UInventoryGridWidget>(Screen->GetWidgetFromName(GridName));
        if (!Test.TestNotNull(TEXT("preview grid"), Grid)) { continue; }
        if (UBorder* Preview = ReflectedObject<UBorder>(Grid, TEXT("PreviewBorder")))
        {
            Test.TestEqual(TEXT("preview 숨김"), Preview->GetVisibility(), ESlateVisibility::Collapsed);
        }
    }
    return Interaction && !Interaction->IsDragging() && Interaction->GetTargetContainerId().IsNone()
        && !Screen->HasMouseCaptureByUser(UserIndex);
}

class FWaitForCondition final : public IAutomationLatentCommand
{
public:
    FWaitForCondition(FAutomationTestBase& InTest, FString InDescription, TFunction<bool()> InCondition)
        : Test(InTest), Description(MoveTemp(InDescription)), Condition(MoveTemp(InCondition)) {}
    virtual bool Update() override
    {
        if (Started == 0.0) { Started = FPlatformTime::Seconds(); }
        if (Condition()) { return true; }
        if (FPlatformTime::Seconds() - Started < 15.0) { return false; }
        Test.AddError(FString::Printf(TEXT("PIE timeout: %s"), *Description));
        return true;
    }
private:
    FAutomationTestBase& Test;
    FString Description;
    TFunction<bool()> Condition;
    double Started = 0.0;
};

struct FInventoryLifecycleScene
{
    TWeakObjectPtr<UInventoryModel> OldModel;
    TWeakObjectPtr<UInventoryScreenWidget> OldScreen;
    TWeakObjectPtr<UInventoryModel> ReleasedModel;
    TWeakObjectPtr<UInventoryScreenWidget> ReleasedScreen;
    TWeakObjectPtr<UInventoryScreenWidget> NewScreen;
    FDelegateHandle CleanupHandle;
    bool bCleanupObserved = false;
    TWeakObjectPtr<UWorld> OldWorld;
    TWeakObjectPtr<AInventoryDemoPlayerController> OldController;
    const UWorld* OldWorldAddress = nullptr;
    AInventoryDemoPlayerController* OldControllerAddress = nullptr;
    FInventorySaveRecord OriginalRecord;
    bool bReadyForTravel = false;
    bool bOriginalInactiveInput = false;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInventoryWidgetLifecycle, "Duckov.UI.PIELifecycle", Flags)
bool FInventoryWidgetLifecycle::RunTest(const FString& Parameters)
{
    if (!TestTrue(TEXT("Slate initialized"), FSlateApplication::IsInitialized())) { return false; }
    if (!TestTrue(TEXT("데모 map load"), AutomationOpenMap(DemoMap))) { return false; }
    TSharedRef<FInventoryLifecycleScene> Scene = MakeShared<FInventoryLifecycleScene>();
    Scene->bOriginalInactiveInput = FSlateApplication::Get().GetHandleDeviceInputWhenApplicationNotActive();
    FSlateApplication::Get().SetHandleDeviceInputWhenApplicationNotActive(true);
    // AutomationOpenMap이 Editor에서 PIE까지 시작하므로 중복 시작하지 않는다.
    ADD_LATENT_AUTOMATION_COMMAND(FWaitForCondition(*this, TEXT("데모 PIE controller"), []
    {
        return GEditor && GEditor->PlayWorld
            && Cast<AInventoryDemoPlayerController>(UGameplayStatics::GetPlayerController(GEditor->PlayWorld, 0));
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        UWorld* World = GEditor->PlayWorld;
        AInventoryDemoPlayerController* Controller = Cast<AInventoryDemoPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
        Scene->OldWorld = World;
        Scene->OldController = Controller;
        Scene->OldWorldAddress = World;
        Scene->OldControllerAddress = Controller;
        UInventoryModel* Model = ReflectedObject<UInventoryModel>(Controller, TEXT("Model"));
        if (!TestNotNull(TEXT("실제 PIE Model"), Model)) { return; }
        Scene->OldModel = Model;
        Controller->ToggleInventory();
        UInventoryScreenWidget* Screen = ReflectedObject<UInventoryScreenWidget>(Controller, TEXT("Screen"));
        if (!TestNotNull(TEXT("실제 WBP Screen"), Screen)) { return; }
        Scene->OldScreen = Screen;
        TestTrue(TEXT("Screen 활성 및 viewport 등록"), Screen->IsActivated() && Screen->IsInViewport());
        UE_LOG(LogTemp, Display, TEXT("PIELifecycle: 초기 World=%s Controller=%s Screen=%s"),
            *GetNameSafe(World), *GetNameSafe(Controller), *GetNameSafe(Screen));
    }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitForCondition(*this, TEXT("WBP Slate geometry"), [Scene]
    {
        UInventoryItemWidget* Item = Scene->OldScreen.IsValid() ? FindAmmoWidget(Scene->OldScreen.Get()) : nullptr;
        return Item && Item->GetCachedGeometry().GetLocalSize().X > 0.f;
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        UInventoryModel* Model = Scene->OldModel.Get();
        UInventoryScreenWidget* Screen = Scene->OldScreen.Get();
        if (!Model || !Screen || !StartCapturedDrag(*this, Screen)) { return; }
        FInventorySaveRecord Record;
        if (!TestEqual(TEXT("Model snapshot"), Model->Save(Record), EInventorySaveFailure::None)) { return; }
        Scene->OriginalRecord = Record;
        TestEqual(TEXT("외부 Model reset"), Model->Load(Record), EInventorySaveFailure::None);
        CheckCleared(*this, Screen);

        // 원본 스택 전체를 흡수하는 Ammo 대상을 구성한다.
        FInventoryContainerSaveRecord* Stash = Record.Containers.FindByPredicate([](const auto& C) { return C.ContainerId == TEXT("Stash"); });
        FInventoryContainerSaveRecord* Bag = Record.Containers.FindByPredicate([](const auto& C) { return C.ContainerId == TEXT("Bag"); });
        if (!TestTrue(TEXT("Stash/Bag records"), Stash && Bag)) { return; }
        const FInventoryItemSaveRecord* Ammo = Stash->Items.FindByPredicate([](const auto& I) { return I.InstanceId == 2; });
        if (!TestNotNull(TEXT("Ammo record"), Ammo)) { return; }
        auto& Target = Bag->Items.Add_GetRef(*Ammo);
        Target.InstanceId = 4;
        Target.Quantity = 12;
        Target.AnchorCell = FIntPoint::ZeroValue;
        Record.NextInstanceId = 5;
        TestEqual(TEXT("stack target 구성"), Model->Load(Record), EInventorySaveFailure::None);
    }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitForCondition(*this, TEXT("reset 후 WBP 재구성"), [Scene]
    {
        UInventoryItemWidget* Item = Scene->OldScreen.IsValid() ? FindAmmoWidget(Scene->OldScreen.Get()) : nullptr;
        return Item && Item->GetCachedGeometry().GetLocalSize().X > 0.f;
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        UInventoryModel* Model = Scene->OldModel.Get();
        UInventoryScreenWidget* Screen = Scene->OldScreen.Get();
        if (!Model || !Screen || !StartCapturedDrag(*this, Screen)) { return; }
        TestEqual(TEXT("외부 stack으로 source 제거"), Model->TryStack(TEXT("Stash"), TEXT("Bag"), 2, 4),
            EInventoryOperationFailure::None);
        TestNull(TEXT("source 실제 제거"), Model->FindItem(TEXT("Stash"), 2));
        CheckCleared(*this, Screen);
        TestEqual(TEXT("travel 전 demo state 복구"), Model->Load(Scene->OriginalRecord), EInventorySaveFailure::None);
    }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitForCondition(*this, TEXT("travel 전 WBP 재구성"), [Scene]
    {
        UInventoryItemWidget* Item = Scene->OldScreen.IsValid() ? FindAmmoWidget(Scene->OldScreen.Get()) : nullptr;
        return Item && Item->GetCachedGeometry().GetLocalSize().X > 0.f;
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        UInventoryScreenWidget* Screen = Scene->OldScreen.Get();
        UWorld* World = Scene->OldWorld.Get();
        if (!Screen || !World || !StartCapturedDrag(*this, Screen)) { return; }
        TestTrue(TEXT("travel 전 Model 변경 delegate 연결"), Scene->OldModel->OnChanged().IsBound());
        TestTrue(TEXT("travel 전 Screen deactivation delegate 연결"), Screen->OnDeactivated().IsBound());
        Scene->bReadyForTravel = true;
        Scene->CleanupHandle = FWorldDelegates::OnWorldCleanup.AddLambda(
            [this, Scene, OldScreen = Screen, OldModel = Scene->OldModel.Get()](UWorld* CleaningWorld, bool, bool)
        {
            if (CleaningWorld != Scene->OldWorldAddress || Scene->bCleanupObserved) { return; }
            Scene->bCleanupObserved = true;


            TestFalse(TEXT("이전 Screen 비활성"), OldScreen->IsActivated());
            TestFalse(TEXT("이전 Screen viewport 제거"), OldScreen->IsInViewport());
            CheckCleared(*this, OldScreen);
            TestFalse(TEXT("이전 Model 변경 delegate 제거"), OldModel->OnChanged().IsBound());
            TestFalse(TEXT("이전 Screen deactivation delegate 제거"), OldScreen->OnDeactivated().IsBound());
            // EndPlay 이후이며 LoadMap의 GC 전이므로 기존 객체 필드를 검사할 수 있다.
            TestNull(TEXT("이전 controller Screen 참조 제거"), ReflectedObject<UInventoryScreenWidget>(Scene->OldControllerAddress, TEXT("Screen")));
            TestNull(TEXT("이전 controller Model 참조 제거"), ReflectedObject<UInventoryModel>(Scene->OldControllerAddress, TEXT("Model")));
            Scene->ReleasedScreen = OldScreen;
            Scene->ReleasedModel = OldModel;
            Scene->OldScreen.Reset();
            Scene->OldModel.Reset();
            UE_LOG(LogTemp, Display, TEXT("PIELifecycle: 이전 화면 capture/delegate 정리 및 테스트 약참조 해제"));
        });
        UE_LOG(LogTemp, Display, TEXT("PIELifecycle: capture before travel World=%s Controller=%s"),
            *GetNameSafe(World), *GetNameSafe(Scene->OldController.Get()));
        UGameplayStatics::OpenLevel(World, FName(DemoMap));
    }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitForCondition(*this, TEXT("OpenLevel 새 PIE world/controller"), [Scene]
    {
        if (!Scene->bReadyForTravel || !GEditor || !GEditor->PlayWorld || GEditor->PlayWorld == Scene->OldWorldAddress)
        {
            return false;
        }
        return Cast<AInventoryDemoPlayerController>(UGameplayStatics::GetPlayerController(GEditor->PlayWorld, 0)) != nullptr;
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        UWorld* NewWorld = GEditor ? GEditor->PlayWorld : nullptr;
        AInventoryDemoPlayerController* NewController = NewWorld
            ? Cast<AInventoryDemoPlayerController>(UGameplayStatics::GetPlayerController(NewWorld, 0)) : nullptr;
        if (!TestTrue(TEXT("실제 World 교체"), NewWorld && NewWorld != Scene->OldWorldAddress)
            || !TestNotNull(TEXT("새 demo controller"), NewController)
            || !TestTrue(TEXT("실제 Controller 교체"), NewController != Scene->OldControllerAddress)) { return; }
        UE_LOG(LogTemp, Display, TEXT("PIELifecycle: travel 후 World=%s Controller=%s OldControllerValid=%d"),
            *GetNameSafe(NewWorld), *GetNameSafe(NewController), Scene->OldController.IsValid());
        TestTrue(TEXT("이전 world cleanup 관찰"), Scene->bCleanupObserved);
        TestFalse(TEXT("이전 World 유효 참조 해제"), Scene->OldWorld.IsValid());
        TestFalse(TEXT("이전 Controller 유효 참조 해제"), Scene->OldController.IsValid());
        TestFalse(TEXT("이전 Screen 유효 참조 해제"), Scene->ReleasedScreen.IsValid());
        TestFalse(TEXT("이전 Model 유효 참조 해제"), Scene->ReleasedModel.IsValid());
        NewController->ToggleInventory();
        UInventoryScreenWidget* NewScreen = ReflectedObject<UInventoryScreenWidget>(NewController, TEXT("Screen"));
        Scene->NewScreen = NewScreen;
        TestTrue(TEXT("새 controller에서 Screen 재개방"), NewScreen && NewScreen->IsActivated() && NewScreen->IsInViewport());
    }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitForCondition(*this, TEXT("travel 후 WBP geometry"), [Scene]
    {
        UInventoryItemWidget* Item = Scene->NewScreen.IsValid() ? FindAmmoWidget(Scene->NewScreen.Get()) : nullptr;
        return Item && Item->GetCachedGeometry().GetLocalSize().X > 0.f;
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        if (UInventoryScreenWidget* Screen = Scene->NewScreen.Get())
        {
            if (StartCapturedDrag(*this, Screen))
            {
                Screen->CancelItemDrag();
                CheckCleared(*this, Screen);
                UE_LOG(LogTemp, Display, TEXT("PIELifecycle: travel 후 새 화면 실제 capture 재획득과 취소 확인"));
            }
        }
        FWorldDelegates::OnWorldCleanup.Remove(Scene->CleanupHandle);
        Scene->OldScreen.Reset();
        Scene->OldModel.Reset();
    }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    ADD_LATENT_AUTOMATION_COMMAND(FWaitForCondition(*this, TEXT("PIE 종료"), []
    {
        return !GEditor || !GEditor->PlayWorld;
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([Scene]
    {
        FSlateApplication::Get().SetHandleDeviceInputWhenApplicationNotActive(Scene->bOriginalInactiveInput);
    }, 0.f));
    return true;
}
#endif
