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
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/CommonUIActionRouterBase.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "UObject/UnrealType.h"

namespace InventorySplitWidgetTests
{
namespace
{
constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

template <typename T>
T* Field(UObject* Owner, FName Name)
{
    const FObjectProperty* Property = Owner ? FindFProperty<FObjectProperty>(Owner->GetClass(), Name) : nullptr;
    return Property ? Cast<T>(Property->GetObjectPropertyValue_InContainer(Owner)) : nullptr;
}

UInventoryItemWidget* FindItemWidget(UInventoryScreenWidget* Screen, int32 InstanceId)
{
    UInventoryGridWidget* Grid = Screen ? Cast<UInventoryGridWidget>(Screen->GetWidgetFromName(TEXT("LeftGrid"))) : nullptr;
    UCanvasPanel* Canvas = Grid ? Cast<UCanvasPanel>(Grid->GetWidgetFromName(TEXT("GridCanvas"))) : nullptr;
    if (!Canvas) { return nullptr; }
    for (int32 Index = 0; Index < Canvas->GetChildrenCount(); ++Index)
    {
        UInventoryItemWidget* Widget = Cast<UInventoryItemWidget>(Canvas->GetChildAt(Index));
        UItemViewModel* Item = Field<UItemViewModel>(Widget, TEXT("Item"));
        if (Item && Item->GetInstanceId() == InstanceId) { return Widget; }
    }
    return nullptr;
}

bool ShiftClick(UInventoryScreenWidget* Screen)
{
    UInventoryItemWidget* Widget = FindItemWidget(Screen, 2);
    if (!Widget) { return false; }
    FSlateApplication& Slate = FSlateApplication::Get();
    FWidgetPath Path;
    if (!Slate.FindPathToWidget(Widget->TakeWidget(), Path) || !Path.IsValid()) { return false; }
    TArray<FWidgetAndPointer> Widgets;
    for (int32 Index = 0; Index < Path.Widgets.Num(); ++Index)
    {
        Widgets.Emplace(Path.Widgets[Index], TOptional<FVirtualPointerPosition>());
    }
    Path = FWidgetPath(MakeArrayView(Widgets));
    const FVector2D Position = Widget->GetCachedGeometry().GetAbsolutePosition() + FVector2D(12.f, 12.f);
    TSet<FKey> Buttons;
    Buttons.Add(EKeys::LeftMouseButton);
    const FModifierKeysState Modifiers(true, false, false, false, false, false, false, false, false);
    const FPointerEvent Event(Slate.GetUserIndexForMouse(), FSlateApplication::CursorPointerIndex,
        Position, Position, Buttons, EKeys::LeftMouseButton, 0.f, Modifiers);
    return Slate.RoutePointerDownEvent(Path, Event).IsEventHandled();
}

bool SameState(UInventoryModel* Model, const FInventorySaveRecord& Before)
{
    FInventorySaveRecord After;
    return Model && Model->Save(After) == EInventorySaveFailure::None
        && FInventorySaveRecord::StaticStruct()->CompareScriptStruct(&Before, &After, 0);
}

// SetText는 사용자 입력 이벤트를 발생시키지 않으므로 실제 Slate 문자 입력을 사용한다.
bool TypeQuantity(UEditableTextBox* Input, APlayerController* Controller, const FString& Value)
{
    Input->SetUserFocus(Controller);
    FSlateApplication& Slate = FSlateApplication::Get();
    const uint32 User = Slate.GetUserIndexForMouse();
    const FModifierKeysState Control(false, false, true, false, false, false, false, false, false);
    const FKeyEvent SelectAll(EKeys::A, Control, User, false, 0, 0);
    Slate.ProcessKeyDownEvent(SelectAll);
    Slate.ProcessKeyUpEvent(SelectAll);
    for (TCHAR Character : Value)
    {
        Slate.ProcessKeyCharEvent(FCharacterEvent(Character, FModifierKeysState(), User, false));
    }
    return Input->GetText().ToString() == Value;
}

class FWaitForSplitCondition final : public IAutomationLatentCommand
{
public:
    FWaitForSplitCondition(FAutomationTestBase& InTest, FString InLabel, TFunction<bool()> InCondition)
        : Test(InTest), Label(MoveTemp(InLabel)), Condition(MoveTemp(InCondition)) {}
    virtual bool Update() override
    {
        if (Started == 0.0) { Started = FPlatformTime::Seconds(); }
        if (Condition()) { return true; }
        if (FPlatformTime::Seconds() - Started < 15.0) { return false; }
        Test.AddError(FString::Printf(TEXT("PIE timeout: %s"), *Label));
        return true;
    }
private:
    FAutomationTestBase& Test;
    FString Label;
    TFunction<bool()> Condition;
    double Started = 0.0;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInventorySplitWidgetPIE, "Duckov.UI.PIESplit", Flags)
bool FInventorySplitWidgetPIE::RunTest(const FString& Parameters)
{
    if (!TestTrue(TEXT("Slate initialized"), FSlateApplication::IsInitialized())) { return false; }
    if (!TestTrue(TEXT("데모 map load"), AutomationOpenMap(TEXT("/Game/Maps/L_InventoryDemo")))) { return false; }
    struct FScene
    {
        TWeakObjectPtr<AInventoryDemoPlayerController> Controller;
        TWeakObjectPtr<UInventoryScreenWidget> Screen;
        TWeakObjectPtr<UInventoryModel> Model;
        bool bOriginalInactiveInput = false;
    };
    TSharedRef<FScene> Scene = MakeShared<FScene>();
    Scene->bOriginalInactiveInput = FSlateApplication::Get().GetHandleDeviceInputWhenApplicationNotActive();
    FSlateApplication::Get().SetHandleDeviceInputWhenApplicationNotActive(true);
    ADD_LATENT_AUTOMATION_COMMAND(FWaitForSplitCondition(*this, TEXT("PIE controller"), []
    {
        return GEditor && GEditor->PlayWorld
            && Cast<AInventoryDemoPlayerController>(UGameplayStatics::GetPlayerController(GEditor->PlayWorld, 0));
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        AInventoryDemoPlayerController* Controller = Cast<AInventoryDemoPlayerController>(
            UGameplayStatics::GetPlayerController(GEditor->PlayWorld, 0));
        if (!TestNotNull(TEXT("PIE Controller 준비"), Controller)) { return; }
        Scene->Controller = Controller;
        Controller->ToggleInventory();
        Scene->Screen = Field<UInventoryScreenWidget>(Controller, TEXT("Screen"));
        Scene->Model = Field<UInventoryModel>(Controller, TEXT("Model"));
        TestTrue(TEXT("실제 WBP screen 활성"), Scene->Screen.IsValid() && Scene->Screen->IsActivated());
    }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitForSplitCondition(*this, TEXT("Ammo widget geometry"), [Scene]
    {
        UInventoryItemWidget* Widget = FindItemWidget(Scene->Screen.Get(), 2);
        return Widget && Widget->GetCachedGeometry().GetLocalSize().X > 0.f;
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        UInventoryScreenWidget* Screen = Scene->Screen.Get();
        UInventoryModel* Model = Scene->Model.Get();
        if (!Screen || !Model) { AddError(TEXT("PIE screen/model unavailable")); return; }
        UEditableTextBox* Input = Cast<UEditableTextBox>(Screen->GetWidgetFromName(TEXT("SplitQuantityInput")));
        UButton* Confirm = Cast<UButton>(Screen->GetWidgetFromName(TEXT("SplitConfirmButton")));
        UButton* Sort = Cast<UButton>(Screen->GetWidgetFromName(TEXT("SortLeftButton")));
        UCanvasPanel* Overlay = Cast<UCanvasPanel>(Screen->GetWidgetFromName(TEXT("SplitOverlay")));
        UInteractionViewModel* Interaction = Field<UInteractionViewModel>(Screen, TEXT("Interaction"));
        if (!TestTrue(TEXT("native modal controls"), Input && Confirm && Sort && Overlay && Interaction)) { return; }
        TestTrue(TEXT("실제 Shift+click routed"), ShiftClick(Screen));
        TestEqual(TEXT("dialog visible"), Overlay->GetVisibility(), ESlateVisibility::Visible);
        TestEqual(TEXT("default half"), Input->GetText().ToString(), FString(TEXT("9")));
        TestFalse(TEXT("Shift click starts no drag"), Interaction->IsDragging());
        TestFalse(TEXT("background sort disabled"), Sort->GetIsEnabled());
        TestFalse(TEXT("pointer capture absent"), Screen->HasMouseCaptureByUser(FSlateApplication::Get().GetUserIndexForMouse()));
        FInventorySaveRecord Before;
        TestEqual(TEXT("snapshot before invalid input"), Model->Save(Before), EInventorySaveFailure::None);
        for (const TCHAR* Invalid : {TEXT("0"), TEXT("18"), TEXT("1.5"), TEXT("-1"), TEXT("4294967297")})
        {
            TestTrue(TEXT("수량 문자 입력"), TypeQuantity(Input, Scene->Controller.Get(), Invalid));
            TestFalse(FString::Printf(TEXT("invalid quantity %s disabled"), Invalid), Confirm->GetIsEnabled());
            Confirm->OnClicked.Broadcast();
            TestTrue(TEXT("invalid input preserves Model"), SameState(Model, Before));
        }
        TestTrue(TEXT("유효 수량 문자 입력"), TypeQuantity(Input, Scene->Controller.Get(), TEXT("9")));
        TestTrue(TEXT("valid quantity enabled"), Confirm->GetIsEnabled());
        const int32 NewId = FItemInstanceIdAllocator::GetNextInstanceId();
        FSlateApplication& Slate = FSlateApplication::Get();
        const FKeyEvent Enter(EKeys::Enter, FModifierKeysState(), Slate.GetUserIndexForMouse(), false, 0, 0);
        TestTrue(TEXT("실제 Enter로 분할 확정"), Slate.ProcessKeyDownEvent(Enter));
        Slate.ProcessKeyUpEvent(Enter);
        const FItemInstance* Original = Model->FindItem(TEXT("Stash"), 2);
        const FItemInstance* Added = Model->FindItem(TEXT("Stash"), NewId);
        TestTrue(TEXT("Model source and allocated item"), Original && Added);
        if (Original && Added)
        {
            TestEqual(TEXT("original quantity"), Original->Quantity, 9);
            TestEqual(TEXT("new quantity"), Added->Quantity, 9);
            TestTrue(TEXT("new ID distinct"), Added->InstanceId != Original->InstanceId);
        }
        TestEqual(TEXT("success closes modal"), Overlay->GetVisibility(), ESlateVisibility::Collapsed);
        TestTrue(TEXT("new WBP child"), FindItemWidget(Screen, NewId) != nullptr);
        for (int32 Id : {2, NewId})
        {
            UTextBlock* Label = Field<UTextBlock>(FindItemWidget(Screen, Id), TEXT("ItemText"));
            TestTrue(TEXT("원본과 새 Widget 수량 갱신"), Label && Label->GetText().ToString().Contains(TEXT("x9")));
        }
        TestTrue(TEXT("background sort restored"), Sort->GetIsEnabled());
    }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        UInventoryScreenWidget* Screen = Scene->Screen.Get();
        UInventoryModel* Model = Scene->Model.Get();
        AInventoryDemoPlayerController* Controller = Scene->Controller.Get();
        if (!Screen || !Model || !Controller) { AddError(TEXT("PIE scene unavailable")); return; }
        UCanvasPanel* Overlay = Cast<UCanvasPanel>(Screen->GetWidgetFromName(TEXT("SplitOverlay")));
        UButton* Close = Cast<UButton>(Screen->GetWidgetFromName(TEXT("CloseButton")));
        UButton* Sort = Cast<UButton>(Screen->GetWidgetFromName(TEXT("SortLeftButton")));
        UEditableTextBox* Input = Cast<UEditableTextBox>(Screen->GetWidgetFromName(TEXT("SplitQuantityInput")));
        UTextBlock* Error = Cast<UTextBlock>(Screen->GetWidgetFromName(TEXT("SplitErrorText")));
        UCommonUIActionRouterBase* Router = Controller->GetLocalPlayer()->GetSubsystem<UCommonUIActionRouterBase>();
        if (!Overlay || !Close || !Sort || !Input || !Error || !Router) { AddError(TEXT("split controls/router unavailable")); return; }
        FInventorySaveRecord Before;
        Model->Save(Before);
        TestTrue(TEXT("reopen by Shift+click"), ShiftClick(Screen));
        TestEqual(TEXT("CommonUI Back handled"), Router->ProcessInput(EKeys::Escape, IE_Pressed), ERouteUIInputResult::Handled);
        TestTrue(TEXT("Back keeps screen open"), Screen->IsActivated());
        TestEqual(TEXT("Back closes dialog"), Overlay->GetVisibility(), ESlateVisibility::Collapsed);
        TestTrue(TEXT("Back preserves Model"), SameState(Model, Before));
        TestTrue(TEXT("focus restored"), Close->HasUserFocus(Controller));
        TestTrue(TEXT("reopen before screen close"), ShiftClick(Screen));
        Controller->ToggleInventory();
        TestEqual(TEXT("screen close cancels dialog"), Overlay->GetVisibility(), ESlateVisibility::Collapsed);
        TestTrue(TEXT("screen close preserves Model"), SameState(Model, Before));
        Controller->ToggleInventory();
        TestTrue(TEXT("screen reopen active"), Screen->IsActivated());
        TestTrue(TEXT("dialog can reopen after screen activation"), ShiftClick(Screen));
        TestEqual(TEXT("source reset succeeds"), Model->Load(Before), EInventorySaveFailure::None);
        TestEqual(TEXT("source reset closes stale dialog"), Overlay->GetVisibility(), ESlateVisibility::Collapsed);
        TestTrue(TEXT("source reset preserves record"), SameState(Model, Before));

        FInventorySaveRecord Full = Before;
        FInventoryContainerSaveRecord* Stash = Full.Containers.FindByPredicate([](const auto& C)
        {
            return C.ContainerId == TEXT("Stash");
        });
        if (!TestNotNull(TEXT("Stash record"), Stash)) { return; }
        Stash->GridSize = FIntPoint(1, 1);
        Stash->Items.RemoveAll([](const auto& I) { return I.InstanceId != 2; });
        Stash->Items[0].AnchorCell = FIntPoint::ZeroValue;
        TestEqual(TEXT("full grid fixture load"), Model->Load(Full), EInventorySaveFailure::None);
        TestTrue(TEXT("full grid Shift+click"), ShiftClick(Screen));
        FInventorySaveRecord FullBefore;
        Model->Save(FullBefore);
        Sort->OnClicked.Broadcast();
        TestTrue(TEXT("modal prevents sort"), SameState(Model, FullBefore));
        TestTrue(TEXT("공간 부족 수량 입력"), TypeQuantity(Input, Controller, TEXT("3")));
        Cast<UButton>(Screen->GetWidgetFromName(TEXT("SplitConfirmButton")))->OnClicked.Broadcast();
        TestEqual(TEXT("no space keeps dialog"), Overlay->GetVisibility(), ESlateVisibility::Visible);
        TestFalse(TEXT("no space has message"), Error->GetText().IsEmpty());
        TestTrue(TEXT("no space preserves Model"), SameState(Model, FullBefore));
        Cast<UButton>(Screen->GetWidgetFromName(TEXT("SplitCancelButton")))->OnClicked.Broadcast();
        TestEqual(TEXT("cancel closes no-space dialog"), Overlay->GetVisibility(), ESlateVisibility::Collapsed);
        TestTrue(TEXT("cancel preserves Model"), SameState(Model, FullBefore));
        TestTrue(TEXT("공간 부족 후 재시도 창 열기"), ShiftClick(Screen));
        TestTrue(TEXT("재시도 수량 입력"), TypeQuantity(Input, Controller, TEXT("3")));
        TestEqual(TEXT("원본 이동 없이 공간 확보"), Model->TryResize(TEXT("Stash"), FIntPoint(2, 1)), EInventoryOperationFailure::None);
        TestEqual(TEXT("공간 확보 후 분할 선택 유지"), Overlay->GetVisibility(), ESlateVisibility::Visible);
        const int32 RetryId = FItemInstanceIdAllocator::GetNextInstanceId();
        Cast<UButton>(Screen->GetWidgetFromName(TEXT("SplitConfirmButton")))->OnClicked.Broadcast();
        TestEqual(TEXT("공간 확보 후 성공하면 창 닫힘"), Overlay->GetVisibility(), ESlateVisibility::Collapsed);
        const FItemInstance* RetryItem = Model->FindItem(TEXT("Stash"), RetryId);
        TestTrue(TEXT("재시도 분할 수량"), RetryItem && RetryItem->Quantity == 3);
    }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    ADD_LATENT_AUTOMATION_COMMAND(FWaitForSplitCondition(*this, TEXT("PIE exit"), [] { return !GEditor || !GEditor->PlayWorld; }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([Scene]
    {
        FSlateApplication::Get().SetHandleDeviceInputWhenApplicationNotActive(Scene->bOriginalInactiveInput);
    }, 0.f));
    return true;
}
}
#endif
