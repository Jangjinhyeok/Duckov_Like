#include "TitlePlayerController.h"

#include "CustomizationScreenWidget.h"
#include "Engine/GameInstance.h"
#include "GameRootLayoutWidget.h"
#include "GameSessionSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "LoadingScreenWidget.h"
#include "TitleScreenWidget.h"
#include "Widgets/CommonActivatableWidgetContainer.h"

namespace TitleFlow
{
const TCHAR* const BasementMap = TEXT("/Game/Maps/L_Basement");
}

void ATitlePlayerController::BeginPlay()
{
    Super::BeginPlay();
    if (!IsLocalController()) { return; }
    RootLayout = CreateWidget<UGameRootLayoutWidget>(this, UGameRootLayoutWidget::StaticClass());
    RootLayout->AddToViewport();
    TitleScreen = RootLayout->GetMenuStack()->AddWidget<UTitleScreenWidget>(UTitleScreenWidget::StaticClass(),
        [this](UTitleScreenWidget& Screen)
        {
            Screen.OnStartRequested().AddUObject(this, &ThisClass::StartGame);
            Screen.OnQuitRequested().AddUObject(this, &ThisClass::QuitToDesktop);
        });
}

void ATitlePlayerController::StartGame()
{
    // 화면 전환 중에는 타이틀이 이미 비활성인데 커스터마이징은 아직 활성 전이다. 타이틀이 활성일 때만 받아 중복 push를 막는다.
    if (!RootLayout || LoadingScreen || !TitleScreen || !TitleScreen->IsActivated()) { return; }
    UCustomizationModel* Model = GetGameInstance()->GetSubsystem<UGameSessionSubsystem>()->GetCustomizationModel();
    // 커스터마이징은 타이틀 위에 쌓인다. 닫히면 stack이 아래의 타이틀을 다시 활성화한다.
    CustomizationScreen = RootLayout->GetMenuStack()->AddWidget<UCustomizationScreenWidget>(UCustomizationScreenWidget::StaticClass(),
        [this, Model](UCustomizationScreenWidget& Screen)
        {
            Screen.SetSession(Model);
            // 원작처럼 확인한 화면이 원 안에 남은 채 덮인다. 맵을 떠날 때 EndPlay가 닫는다.
            Screen.SetKeepOpenOnApply(true);
            // pool에서 같은 인스턴스를 다시 받으므로 이전 연결을 지우고 한 번만 묶는다.
            Screen.OnClosed().RemoveAll(this);
            Screen.OnClosed().AddUObject(this, &ThisClass::OnCustomizationClosed);
        });
}

void ATitlePlayerController::OnCustomizationClosed(bool bApplied)
{
    if (!bApplied || !RootLayout || LoadingScreen) { return; }
    LoadingScreen = CreateWidget<ULoadingScreenWidget>(this, ULoadingScreenWidget::StaticClass());
    RootLayout->AddNoticeElement(LoadingScreen);
    LoadingScreen->OnCovered().AddUObject(this, &ThisClass::OnDepartureCovered);
    LoadingScreen->BeginDeparture();
}

void ATitlePlayerController::OnDepartureCovered()
{
    // "로딩 중..."이 한 번 그려진 뒤 맵을 불러오도록 다음 tick에 이동한다. 불러오는 동안 이 화면이 남는다.
    TravelHandle = GetWorldTimerManager().SetTimerForNextTick(this, &ThisClass::TravelToBasement);
}

void ATitlePlayerController::TravelToBasement()
{
    GetGameInstance()->GetSubsystem<UGameSessionSubsystem>()->SetArrivalPending();
    UGameplayStatics::OpenLevel(this, FName(TitleFlow::BasementMap));
}

void ATitlePlayerController::QuitToDesktop()
{
    UKismetSystemLibrary::QuitGame(this, this, EQuitPreference::Quit, false);
}

void ATitlePlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    GetWorldTimerManager().ClearTimer(TravelHandle);
    if (CustomizationScreen)
    {
        CustomizationScreen->OnClosed().RemoveAll(this);
        if (CustomizationScreen->IsActivated()) { CustomizationScreen->DeactivateWidget(); }
    }
    if (TitleScreen)
    {
        TitleScreen->OnStartRequested().RemoveAll(this);
        TitleScreen->OnQuitRequested().RemoveAll(this);
        if (TitleScreen->IsActivated()) { TitleScreen->DeactivateWidget(); }
    }
    if (RootLayout) { RootLayout->RemoveFromParent(); }
    RootLayout = nullptr;
    TitleScreen = nullptr;
    CustomizationScreen = nullptr;
    LoadingScreen = nullptr;
    Super::EndPlay(EndPlayReason);
}
