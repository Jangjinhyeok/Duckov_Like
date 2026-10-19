#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "LoadingScreenWidget.generated.h"

class SIrisMask;
class UTextBlock;
class UWidget;

enum class ELoadingScreenPhase : uint8
{
    // 이전 화면 위에서 원이 오므라드는 중이다.
    Departing,
    // 검은 화면에 "로딩 중..."이다. 이 사이에 맵을 불러온다.
    Loading,
    // 도착한 맵에서 "화면을 클릭하여 계속하기"로 클릭을 기다린다.
    AwaitingClick,
    // 원이 커지며 게임 화면을 드러내는 중이다. 끝나면 스스로 화면에서 빠진다.
    Revealing,
};

// 맵 이동 화면이다. 원작처럼 출발은 원이 오므라들며 덮고, 도착은 클릭을 기다린 뒤 원이 커지며 드러낸다(설계결정기록 037).
// root layout의 Notice layer에 붙어 HUD와 메뉴까지 함께 덮는다. 애니메이션은 iris가 움직이는 동안만 active timer로 돈다.
UCLASS(meta=(DisableNativeTick))
class DUCKOVLIKE_API ULoadingScreenWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    DECLARE_EVENT(ULoadingScreenWidget, FLoadingScreenEvent);
    // 지금 화면 위에서 원을 오므려 덮는다. 다 덮이면 "로딩 중..."이 되고 OnCovered가 한 번 온다.
    void BeginDeparture();
    // 검은 화면에서 클릭을 기다린다.
    void BeginArrival();
    // 클릭을 기다리는 중이면 원을 키워 드러내기 시작하고 OnContinued가 한 번 온다. 마우스 클릭이 이 함수를 부른다.
    void Continue();
    ELoadingScreenPhase GetPhase() const { return Phase; }
    // 게임 화면이 완전히 가려진 단계다. 이때는 HUD를 그릴 필요가 없다.
    bool IsCoveringGame() const { return Phase == ELoadingScreenPhase::Loading || Phase == ELoadingScreenPhase::AwaitingClick; }
    FLoadingScreenEvent& OnCovered() { return CoveredEvent; }
    FLoadingScreenEvent& OnContinued() { return ContinuedEvent; }
protected:
    virtual void NativeOnInitialized() override;
    virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
private:
    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> LoadingText;
    UPROPERTY(Transient)
    TObjectPtr<UWidget> ContinuePrompt;
    UPROPERTY(Transient)
    TObjectPtr<UWidget> ContinueRing;
    TSharedPtr<SIrisMask> Iris;
    ELoadingScreenPhase Phase = ELoadingScreenPhase::Loading;
    double IrisStartTime = 0.0;
    FLoadingScreenEvent CoveredEvent;
    FLoadingScreenEvent ContinuedEvent;
    void SetPhase(ELoadingScreenPhase NewPhase);
    void StartIris();
    EActiveTimerReturnType UpdateIris(double CurrentTime, float DeltaTime);
    EActiveTimerReturnType BlinkRing(double CurrentTime, float DeltaTime);
};
