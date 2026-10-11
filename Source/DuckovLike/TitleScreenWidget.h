#pragma once

#include "CoreMinimal.h"
#include "CommonActivatableWidget.h"
#include "TitleScreenWidget.generated.h"

class UButton;
class UWidget;

// 첫 화면이다. 스플래시("클릭하여 계속하기")에서 입력을 받으면 같은 배경 위에 왼쪽 세로 메뉴를 보인다.
// 메뉴의 결과는 이벤트로만 알리고, 맵 이동과 다음 화면은 controller가 정한다.
UCLASS(meta=(DisableNativeTick))
class DUCKOVLIKE_API UTitleScreenWidget : public UCommonActivatableWidget
{
    GENERATED_BODY()
public:
    UTitleScreenWidget(const FObjectInitializer& Initializer);
    DECLARE_EVENT(UTitleScreenWidget, FOnTitleAction);
    FOnTitleAction& OnStartRequested() { return StartRequested; }
    FOnTitleAction& OnQuitRequested() { return QuitRequested; }
    bool IsMenuVisible() const { return bMenuVisible; }
    void ShowMenu();
protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeOnActivated() override;
    virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
    virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
    virtual bool NativeOnHandleBackAction() override;
    virtual UWidget* NativeGetDesiredFocusTarget() const override;
    virtual TOptional<FUIInputConfig> GetDesiredInputConfig() const override;
private:
    UPROPERTY(Transient)
    TObjectPtr<UWidget> SplashPanel;
    UPROPERTY(Transient)
    TObjectPtr<UWidget> MenuPanel;
    UPROPERTY(Transient)
    TObjectPtr<UButton> StartButton;
    UPROPERTY(Transient)
    TObjectPtr<UButton> QuitButton;
    FOnTitleAction StartRequested;
    FOnTitleAction QuitRequested;
    bool bMenuVisible = false;
    void FocusCurrentTarget();
    UFUNCTION()
    void RequestStart();
    UFUNCTION()
    void RequestQuit();
};
