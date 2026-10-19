#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"
#include "TitlePlayerController.generated.h"

class UCustomizationScreenWidget;
class UGameRootLayoutWidget;
class ULoadingScreenWidget;
class UTitleScreenWidget;

// 타이틀 맵의 흐름을 맡는다: 타이틀 → 게임 시작 → 커스터마이징 → 확인 시 베이스먼트로 이동.
// 커스터마이징을 취소하면 Menu stack에서 내려가 아래에 있던 타이틀 메뉴로 돌아온다.
// 확인하면 원이 오므라들며 화면을 덮은 뒤 이동하고, 도착한 베이스먼트가 클릭 대기 화면을 이어서 띄운다.
UCLASS()
class DUCKOVLIKE_API ATitlePlayerController : public APlayerController
{
    GENERATED_BODY()
public:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    void StartGame();
    void QuitToDesktop();
private:
    UPROPERTY(Transient)
    TObjectPtr<UGameRootLayoutWidget> RootLayout;
    UPROPERTY(Transient)
    TObjectPtr<UTitleScreenWidget> TitleScreen;
    UPROPERTY(Transient)
    TObjectPtr<UCustomizationScreenWidget> CustomizationScreen;
    UPROPERTY(Transient)
    TObjectPtr<ULoadingScreenWidget> LoadingScreen;
    FTimerHandle TravelHandle;
    void OnCustomizationClosed(bool bApplied);
    void OnDepartureCovered();
    void TravelToBasement();
};
