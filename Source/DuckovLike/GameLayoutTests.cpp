#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "CustomizationScreenWidget.h"
#include "GameRootLayoutWidget.h"
#include "InventoryDemoPlayerController.h"
#include "InventoryScreenWidget.h"
#include "RaidStatusWidget.h"
#include "Editor.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/CommonUIActionRouterBase.h"
#include "Kismet/GameplayStatics.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "UObject/UnrealType.h"
#include "Components/Overlay.h"
#include "Widgets/CommonActivatableWidgetContainer.h"
#include "Widgets/SViewport.h"

namespace GameLayoutTests
{
namespace
{
constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
constexpr const TCHAR* DemoMap = TEXT("/Game/Maps/L_InventoryDemo");

template <typename T>
T* Field(UObject* Owner, FName Name)
{
    const FObjectProperty* Property = Owner ? FindFProperty<FObjectProperty>(Owner->GetClass(), Name) : nullptr;
    return Property ? Cast<T>(Property->GetObjectPropertyValue_InContainer(Owner)) : nullptr;
}

class FWaitFor final : public IAutomationLatentCommand
{
public:
    FWaitFor(FAutomationTestBase& InTest, FString InDescription, TFunction<bool()> InCondition)
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

struct FLayoutScene
{
    TWeakObjectPtr<AInventoryDemoPlayerController> Controller;
    TWeakObjectPtr<UGameRootLayoutWidget> Layout;
    TWeakObjectPtr<UInventoryScreenWidget> FirstScreen;
    bool bOriginalInactiveInput = false;
};

UCommonUIActionRouterBase* GetRouter(const FLayoutScene& Scene)
{
    AInventoryDemoPlayerController* Controller = Scene.Controller.Get();
    return Controller && Controller->GetLocalPlayer() ? Controller->GetLocalPlayer()->GetSubsystem<UCommonUIActionRouterBase>() : nullptr;
}

// Menu stack이 비면 Game 입력으로 돌아오고 viewport가 focus를 받는다.
bool IsGameInputRestored(const FLayoutScene& Scene)
{
    UCommonUIActionRouterBase* Router = GetRouter(Scene);
    return Router && !Router->IsPendingTreeChange() && Router->GetActiveInputMode() == ECommonInputMode::Game
        && GEngine->GameViewport && GEngine->GameViewport->GetGameViewportWidget().IsValid()
        && GEngine->GameViewport->GetGameViewportWidget()->HasAnyUserFocus().IsSet();
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameLayerStack, "Duckov.UI.LayerStack", Flags)
bool FGameLayerStack::RunTest(const FString& Parameters)
{
    if (!TestTrue(TEXT("Slate initialized"), FSlateApplication::IsInitialized())) { return false; }
    if (!TestTrue(TEXT("데모 map load"), AutomationOpenMap(DemoMap))) { return false; }
    TSharedRef<FLayoutScene> Scene = MakeShared<FLayoutScene>();
    Scene->bOriginalInactiveInput = FSlateApplication::Get().GetHandleDeviceInputWhenApplicationNotActive();
    FSlateApplication::Get().SetHandleDeviceInputWhenApplicationNotActive(true);
    ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(*this, TEXT("데모 PIE controller"), []
    {
        return GEditor && GEditor->PlayWorld
            && Cast<AInventoryDemoPlayerController>(UGameplayStatics::GetPlayerController(GEditor->PlayWorld, 0));
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        AInventoryDemoPlayerController* Controller = Cast<AInventoryDemoPlayerController>(
            UGameplayStatics::GetPlayerController(GEditor->PlayWorld, 0));
        Scene->Controller = Controller;
        UGameRootLayoutWidget* Layout = Field<UGameRootLayoutWidget>(Controller, TEXT("RootLayout"));
        Scene->Layout = Layout;
        if (!TestNotNull(TEXT("root layout 생성"), Layout)) { return; }
        TestTrue(TEXT("viewport에는 root layout 하나만 붙는다"), Layout->IsInViewport());
        TestNull(TEXT("시작 시 Menu stack 비어 있음"), Layout->GetMenuStack()->GetActiveWidget());
    }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        AInventoryDemoPlayerController* Controller = Scene->Controller.Get();
        UGameRootLayoutWidget* Layout = Scene->Layout.Get();
        if (!Controller || !Layout) { AddError(TEXT("layout scene unavailable")); return; }
        Controller->ToggleInventory();
        UInventoryScreenWidget* Screen = Field<UInventoryScreenWidget>(Controller, TEXT("Screen"));
        Scene->FirstScreen = Screen;
        if (!TestNotNull(TEXT("인벤토리 화면 생성"), Screen)) { return; }
        TestTrue(TEXT("인벤토리는 Menu stack에 표시"), Layout->GetMenuStack()->GetActiveWidget() == Screen);
        TestTrue(TEXT("인벤토리는 viewport에 직접 붙지 않음"), !Screen->IsInViewport());
    }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(*this, TEXT("인벤토리의 Menu 입력"), [Scene]
    {
        UCommonUIActionRouterBase* Router = GetRouter(*Scene);
        return Router && !Router->IsPendingTreeChange() && Router->GetActiveInputMode() == ECommonInputMode::Menu
            && Router->IsWidgetInActiveRoot(Scene->FirstScreen.Get());
    }));
    // 메뉴가 열린 직후 다른 root에 밀려 Game 입력으로 되돌아가지 않는지 여러 프레임 확인한다.
    ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(*this, TEXT("인벤토리 Menu 입력 유지"), [this, Scene, Frames = 0]() mutable
    {
        UCommonUIActionRouterBase* Router = GetRouter(*Scene);
        if (!Router || Router->GetActiveInputMode() != ECommonInputMode::Menu || !Router->IsWidgetInActiveRoot(Scene->FirstScreen.Get()))
        {
            AddError(FString::Printf(TEXT("인벤토리를 연 뒤 %d 프레임째에 Menu 입력을 잃음"), Frames));
            return true;
        }
        return ++Frames >= 10;
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        AInventoryDemoPlayerController* Controller = Scene->Controller.Get();
        UCommonActivatableWidgetStack* Stack = Scene->Layout->GetMenuStack();
        TestEqual(TEXT("Menu stack 전환 시간"), Stack->GetTransitionDuration(), 0.3f);
        Controller->ToggleInventory();
        TestFalse(TEXT("닫으면 인벤토리 비활성"), Scene->FirstScreen.IsValid() && Scene->FirstScreen->IsActivated());
        // 설계결정기록 036: 사라지는 전환 동안 Menu stack은 닫힌 화면을 계속 표시하고 다시 열기를 받지 않는다.
        TestTrue(TEXT("닫는 전환 동안 화면 표시 유지"), Stack->GetActiveWidget() == Scene->FirstScreen.Get());
        Controller->ToggleInventory();
        TestFalse(TEXT("닫는 전환 중 재개방 무시"), Scene->FirstScreen.IsValid() && Scene->FirstScreen->IsActivated());
        TestTrue(TEXT("닫는 전환 중 새 인스턴스 없음"), Field<UInventoryScreenWidget>(Controller, TEXT("Screen")) == Scene->FirstScreen.Get());
    }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(*this, TEXT("닫는 전환 뒤 Menu stack 비움"), [Scene]
    {
        return Scene->Layout.IsValid() && !Scene->Layout->GetMenuStack()->GetActiveWidget();
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(*this, TEXT("인벤토리 닫은 뒤 Game 입력"), [Scene] { return IsGameInputRestored(*Scene); }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        AInventoryDemoPlayerController* Controller = Scene->Controller.Get();
        Controller->ToggleInventory();
        UInventoryScreenWidget* Reopened = Field<UInventoryScreenWidget>(Controller, TEXT("Screen"));
        TestTrue(TEXT("재개방은 pool의 같은 인스턴스"), Reopened && Reopened == Scene->FirstScreen.Get() && Reopened->IsActivated());
        Controller->ToggleInventory();
    }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(*this, TEXT("재개방 후 닫기 Game 입력"), [Scene] { return IsGameInputRestored(*Scene); }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        AInventoryDemoPlayerController* Controller = Scene->Controller.Get();
        Controller->OpenCustomizationPrototype();
        UCustomizationScreenWidget* Customization = Field<UCustomizationScreenWidget>(Controller, TEXT("CustomizationScreen"));
        if (!TestNotNull(TEXT("커스터마이징 화면 생성"), Customization)) { return; }
        TestTrue(TEXT("커스터마이징도 Menu stack에 표시"), Scene->Layout->GetMenuStack()->GetActiveWidget() == Customization);
        Customization->DeactivateWidget();
    }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(*this, TEXT("커스터마이징 닫는 전환 뒤 Menu stack 비움"), [Scene]
    {
        return Scene->Layout.IsValid() && !Scene->Layout->GetMenuStack()->GetActiveWidget();
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(*this, TEXT("커스터마이징 닫은 뒤 Game 입력"), [Scene] { return IsGameInputRestored(*Scene); }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        AInventoryDemoPlayerController* Controller = Scene->Controller.Get();
        TestEqual(TEXT("Raid 데모 시작"), Controller->TryStartRaidDemo(), ERaidDemoFailure::None);
        URaidStatusWidget* RaidStatus = Field<URaidStatusWidget>(Controller, TEXT("RaidStatus"));
        UGameRootLayoutWidget* Layout = Scene->Layout.Get();
        TestTrue(TEXT("Raid 상태는 Notice layer에 표시"), RaidStatus && Layout && RaidStatus->GetParent() == Layout->GetWidgetFromName(TEXT("NoticeLayer")));
        TestTrue(TEXT("Raid 상태는 viewport에 직접 붙지 않음"), RaidStatus && !RaidStatus->IsInViewport());
        Controller->ToggleInventory();
        TestTrue(TEXT("Raid 준비 중 인벤토리 열기"), Scene->FirstScreen.IsValid() && Scene->FirstScreen->IsActivated());
    }, 0.f));
    // 실패 안내가 열린 인벤토리의 불투명 배경에 가려지지 않도록 Raid 상태가 화면보다 위 layer에 그려지는지 확인한다.
    ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(*this, TEXT("Raid 상태가 열린 인벤토리 위에 그려짐"), [Scene]
    {
        auto PaintedLayer = [](UWidget* Widget)
        {
            TSharedPtr<SWidget> Slate = Widget ? Widget->GetCachedWidget() : nullptr;
            return Slate ? Slate->GetPersistentState().LayerId : INDEX_NONE;
        };
        AInventoryDemoPlayerController* Controller = Scene->Controller.Get();
        const int32 ScreenLayer = PaintedLayer(Scene->FirstScreen.Get());
        const int32 StatusLayer = PaintedLayer(Field<URaidStatusWidget>(Controller, TEXT("RaidStatus")));
        return ScreenLayer > 0 && StatusLayer > ScreenLayer;
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([Scene] { Scene->Controller->ToggleInventory(); }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(*this, TEXT("PIE 종료"), [] { return !GEditor || !GEditor->PlayWorld; }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([Scene]
    {
        FSlateApplication::Get().SetHandleDeviceInputWhenApplicationNotActive(Scene->bOriginalInactiveInput);
    }, 0.f));
    return true;
}
}
#endif
