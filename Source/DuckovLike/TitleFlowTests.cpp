#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "CustomizationModel.h"
#include "CustomizationScreenWidget.h"
#include "GameRootLayoutWidget.h"
#include "GameSessionSubsystem.h"
#include "InventoryDemoPlayerController.h"
#include "LoadingScreenWidget.h"
#include "TitleGameMode.h"
#include "TitlePlayerController.h"
#include "TitleScreenWidget.h"
#include "Components/Button.h"
#include "Components/Slider.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"
#include "Input/CommonUIActionRouterBase.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "UObject/UnrealType.h"
#include "Widgets/CommonActivatableWidgetContainer.h"

namespace TitleFlowTests
{
namespace
{
constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
constexpr const TCHAR* TitleMap = TEXT("/Game/Maps/L_Title");

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
        if (FPlatformTime::Seconds() - Started < 20.0) { return false; }
        Test.AddError(FString::Printf(TEXT("PIE timeout: %s"), *Description));
        return true;
    }
private:
    FAutomationTestBase& Test;
    FString Description;
    TFunction<bool()> Condition;
    double Started = 0.0;
};

struct FTitleScene
{
    TWeakObjectPtr<ATitlePlayerController> Controller;
    TWeakObjectPtr<UGameRootLayoutWidget> Layout;
    TWeakObjectPtr<UTitleScreenWidget> Title;
    TWeakObjectPtr<UCustomizationScreenWidget> Customization;
    TWeakObjectPtr<UWorld> TitleWorld;
    TWeakObjectPtr<UGameInstance> GameInstance;
    TWeakObjectPtr<UCustomizationModel> SessionModel;
    bool bOriginalInactiveInput = false;
};

UCommonUIActionRouterBase* GetRouter(APlayerController* Controller)
{
    return Controller && Controller->GetLocalPlayer() ? Controller->GetLocalPlayer()->GetSubsystem<UCommonUIActionRouterBase>() : nullptr;
}

bool IsMenuActive(APlayerController* Controller, UWidget* Screen)
{
    UCommonUIActionRouterBase* Router = GetRouter(Controller);
    return Router && !Router->IsPendingTreeChange() && Router->GetActiveInputMode() == ECommonInputMode::Menu
        && Router->IsWidgetInActiveRoot(Cast<UCommonActivatableWidget>(Screen));
}

void ClickButton(UUserWidget* Screen, FName Name)
{
    if (UButton* Button = Screen ? Cast<UButton>(Screen->GetWidgetFromName(Name)) : nullptr) { Button->OnClicked.Broadcast(); }
}

bool IsTitleSceneValid(const FTitleScene& Scene)
{
    return Scene.Controller.IsValid() && Scene.Layout.IsValid() && Scene.Title.IsValid();
}

bool RouteFocusedKey(FKey Key)
{
    FSlateApplication& Slate = FSlateApplication::Get();
    const FKeyEvent Event(Key, FModifierKeysState(), Slate.GetUserIndexForMouse(), false, 0, 0);
    const bool bDown = Slate.ProcessKeyDownEvent(Event);
    Slate.ProcessKeyUpEvent(Event);
    return bDown;
}
}

// 명시적으로 실행하는 에셋 생성 경로다. 기존 맵을 덮어쓰지 않으며 회귀 검사에서는 실행하지 않는다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCreateTitleAssets, "TitleAssets.Create", Flags)
bool FCreateTitleAssets::RunTest(const FString& Parameters)
{
    if (FPackageName::DoesPackageExist(TitleMap))
    {
        AddError(FString::Printf(TEXT("기존 에셋을 덮어쓰지 않습니다: %s"), TitleMap));
        return false;
    }
    UWorld* World = UEditorLoadingAndSavingUtils::NewBlankMap(false);
    if (!World) { AddError(TEXT("타이틀 map 생성 실패")); return false; }
    World->GetWorldSettings()->DefaultGameMode = ATitleGameMode::StaticClass();
    if (!UEditorLoadingAndSavingUtils::SaveMap(World, TitleMap)) { AddError(TEXT("타이틀 map 저장 실패")); return false; }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTitleAssets, "Duckov.Title.Assets", Flags)
bool FTitleAssets::RunTest(const FString& Parameters)
{
    FString DefaultMap;
    GConfig->GetString(TEXT("/Script/EngineSettings.GameMapsSettings"), TEXT("GameDefaultMap"), DefaultMap, GEngineIni);
    TestEqual(TEXT("게임 시작 맵은 타이틀"), DefaultMap, FString(TEXT("/Game/Maps/L_Title.L_Title")));
    UWorld* World = LoadObject<UWorld>(nullptr, TEXT("/Game/Maps/L_Title.L_Title"));
    if (!TestNotNull(TEXT("타이틀 map"), World)) { return false; }
    const AWorldSettings* Settings = World->GetWorldSettings();
    TestTrue(TEXT("타이틀 map GameMode override"), Settings && Settings->DefaultGameMode == ATitleGameMode::StaticClass());
    const ATitleGameMode* GameMode = GetDefault<ATitleGameMode>();
    TestTrue(TEXT("타이틀은 pawn 없이 시작"), GameMode->DefaultPawnClass == nullptr);
    TestTrue(TEXT("타이틀 controller"), GameMode->PlayerControllerClass == ATitlePlayerController::StaticClass());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTitleFlowToBasement, "Duckov.Title.FlowToBasement", Flags)
bool FTitleFlowToBasement::RunTest(const FString& Parameters)
{
    if (!TestTrue(TEXT("Slate initialized"), FSlateApplication::IsInitialized())) { return false; }
    if (!TestTrue(TEXT("타이틀 map load"), AutomationOpenMap(TitleMap))) { return false; }
    TSharedRef<FTitleScene> Scene = MakeShared<FTitleScene>();
    Scene->bOriginalInactiveInput = FSlateApplication::Get().GetHandleDeviceInputWhenApplicationNotActive();
    FSlateApplication::Get().SetHandleDeviceInputWhenApplicationNotActive(true);
    ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(*this, TEXT("타이틀 PIE controller"), []
    {
        return GEditor && GEditor->PlayWorld
            && Cast<ATitlePlayerController>(UGameplayStatics::GetPlayerController(GEditor->PlayWorld, 0));
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
        ATitlePlayerController* Controller = World ? Cast<ATitlePlayerController>(UGameplayStatics::GetPlayerController(World, 0)) : nullptr;
        if (!Controller) { AddError(TEXT("타이틀 controller unavailable")); return; }
        Scene->Controller = Controller;
        Scene->TitleWorld = World;
        Scene->GameInstance = World->GetGameInstance();
        Scene->Layout = Field<UGameRootLayoutWidget>(Controller, TEXT("RootLayout"));
        Scene->Title = Field<UTitleScreenWidget>(Controller, TEXT("TitleScreen"));
        if (!TestTrue(TEXT("타이틀 layout과 화면"), Scene->Layout.IsValid() && Scene->Title.IsValid())) { return; }
        TestTrue(TEXT("타이틀은 Menu stack에 표시"), Scene->Layout->GetMenuStack()->GetActiveWidget() == Scene->Title.Get());
        TestFalse(TEXT("처음에는 스플래시"), Scene->Title->IsMenuVisible());
        TestNull(TEXT("타이틀에는 조작 pawn 없음"), Controller->GetPawn());
    }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(*this, TEXT("스플래시가 Menu 입력과 focus를 가짐"), [Scene]
    {
        return IsTitleSceneValid(*Scene) && IsMenuActive(Scene->Controller.Get(), Scene->Title.Get())
            && Scene->Title->HasUserFocus(Scene->Controller.Get());
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        if (!IsTitleSceneValid(*Scene)) { AddError(TEXT("타이틀 scene unavailable")); return; }
        TestTrue(TEXT("스플래시에서 아무 키 입력 처리"), RouteFocusedKey(EKeys::SpaceBar));
        TestTrue(TEXT("입력 후 메뉴 표시"), Scene->Title->IsMenuVisible());
    }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(*this, TEXT("메뉴의 시작 버튼 focus"), [Scene]
    {
        UButton* Start = Scene->Title.IsValid() ? Cast<UButton>(Scene->Title->GetWidgetFromName(TEXT("StartButton"))) : nullptr;
        return Start && Start->HasUserFocus(Scene->Controller.Get());
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        if (!IsTitleSceneValid(*Scene)) { AddError(TEXT("타이틀 scene unavailable")); return; }
        TestTrue(TEXT("시작 버튼 keyboard activation"), RouteFocusedKey(EKeys::Enter));
        Scene->Customization = Field<UCustomizationScreenWidget>(Scene->Controller.Get(), TEXT("CustomizationScreen"));
        if (!TestTrue(TEXT("게임 시작으로 커스터마이징 열림"), Scene->Customization.IsValid() && Scene->Customization->IsActivated())) { return; }
        TestTrue(TEXT("커스터마이징은 타이틀 위에 쌓임"), Scene->Layout->GetMenuStack()->GetActiveWidget() == Scene->Customization.Get());
        TestFalse(TEXT("커스터마이징 중 타이틀 비활성"), Scene->Title->IsActivated());
    }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(*this, TEXT("커스터마이징의 Menu 입력"), [Scene]
    {
        return Scene->Customization.IsValid() && IsMenuActive(Scene->Controller.Get(), Scene->Customization.Get());
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        if (!IsTitleSceneValid(*Scene) || !Scene->Customization.IsValid()) { AddError(TEXT("커스터마이징 scene unavailable")); return; }
        UCommonUIActionRouterBase* Router = GetRouter(Scene->Controller.Get());
        if (!Router) { AddError(TEXT("Router unavailable")); return; }
        TestEqual(TEXT("커스터마이징 Back routed"), Router->ProcessInput(EKeys::Escape, IE_Pressed), ERouteUIInputResult::Handled);
        TestFalse(TEXT("Back으로 커스터마이징 닫힘"), Scene->Customization->IsActivated());
        TestTrue(TEXT("Back 뒤 타이틀 메뉴 복귀"), Scene->Title->IsActivated() && Scene->Title->IsMenuVisible()
            && Scene->Layout->GetMenuStack()->GetActiveWidget() == Scene->Title.Get());
        TestTrue(TEXT("취소로는 맵 이동 없음"), Field<ULoadingScreenWidget>(Scene->Controller.Get(), TEXT("LoadingScreen")) == nullptr);
    }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(*this, TEXT("Back 뒤 타이틀의 Menu 입력"), [Scene]
    {
        return IsTitleSceneValid(*Scene) && IsMenuActive(Scene->Controller.Get(), Scene->Title.Get());
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        if (!IsTitleSceneValid(*Scene) || !Scene->GameInstance.IsValid()) { AddError(TEXT("타이틀 scene unavailable")); return; }
        ClickButton(Scene->Title.Get(), TEXT("StartButton"));
        UCustomizationScreenWidget* Customization = Field<UCustomizationScreenWidget>(Scene->Controller.Get(), TEXT("CustomizationScreen"));
        if (!TestTrue(TEXT("다시 게임 시작"), Customization && Customization->IsActivated())) { return; }
        TestTrue(TEXT("재개방은 pool의 같은 인스턴스"), Customization == Scene->Customization.Get());
        USlider* EyeSize = Cast<USlider>(Customization->GetWidgetFromName(TEXT("EyeSizeSlider")));
        if (!TestNotNull(TEXT("눈 크기 slider"), EyeSize)) { return; }
        EyeSize->SetValue(0.5f);
        EyeSize->OnValueChanged.Broadcast(0.5f);
        Scene->SessionModel = Scene->GameInstance->GetSubsystem<UGameSessionSubsystem>()->GetCustomizationModel();
        ClickButton(Customization, TEXT("ApplyButton"));
        TestFalse(TEXT("확인으로 커스터마이징 닫힘"), Customization->IsActivated());
        TestTrue(TEXT("확인 값이 세션 Model에 확정"), Scene->SessionModel.IsValid()
            && FMath::IsNearlyEqual(Scene->SessionModel->GetProfile().EyeSize, 0.5f));
        ULoadingScreenWidget* Loading = Field<ULoadingScreenWidget>(Scene->Controller.Get(), TEXT("LoadingScreen"));
        TestTrue(TEXT("확인 뒤 로딩 화면이 Notice layer에 표시"),
            Loading && Loading->GetParent() == Scene->Layout->GetWidgetFromName(TEXT("NoticeLayer")));
    }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(*this, TEXT("베이스먼트 맵 이동"), [Scene]
    {
        UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
        return World && !Scene->TitleWorld.HasSameIndexAndSerialNumber(TWeakObjectPtr<UWorld>(World))
            && Cast<AInventoryDemoPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        UWorld* World = GEditor->PlayWorld;
        auto* Controller = World ? Cast<AInventoryDemoPlayerController>(UGameplayStatics::GetPlayerController(World, 0)) : nullptr;
        if (!Controller) { AddError(TEXT("베이스먼트 controller unavailable")); return; }
        TestTrue(TEXT("같은 게임 인스턴스 유지"), Scene->GameInstance.Get() == World->GetGameInstance());
        TestTrue(TEXT("베이스먼트에는 조작 pawn 있음"), Controller->GetPawn() != nullptr);
        UCustomizationModel* Model = World->GetGameInstance()->GetSubsystem<UGameSessionSubsystem>()->GetCustomizationModel();
        TestTrue(TEXT("레벨 이동 뒤에도 같은 세션 Model"), Model == Scene->SessionModel.Get());
        TestTrue(TEXT("타이틀에서 확정한 외형 유지"), Model && FMath::IsNearlyEqual(Model->GetProfile().EyeSize, 0.5f));
        Controller->OpenCustomizationPrototype();
        TestTrue(TEXT("베이스먼트 커스터마이징도 세션 Model 사용"),
            Field<UCustomizationModel>(Controller, TEXT("CustomizationModel")) == Model);
        if (UCustomizationScreenWidget* Screen = Field<UCustomizationScreenWidget>(Controller, TEXT("CustomizationScreen")))
        {
            Screen->DeactivateWidget();
        }
    }, 0.f));
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
