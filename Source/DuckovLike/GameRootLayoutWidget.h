#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameRootLayoutWidget.generated.h"

class UCommonActivatableWidgetStack;
class UOverlay;

// viewport에 하나만 붙는 화면 골격이다. 화면 순서는 Z값 대신 layer 순서로 정한다.
// Menu stack만 CommonUI 입력 전환에 참여하고, 그 위의 Notice layer는 입력을 갖지 않는 안내 표시 전용이다.
UCLASS(meta=(DisableNativeTick))
class DUCKOVLIKE_API UGameRootLayoutWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    UCommonActivatableWidgetStack* GetMenuStack() const { return MenuStack; }
    void AddNoticeElement(UWidget* Element);
protected:
    virtual void NativeOnInitialized() override;
private:
    UPROPERTY(Transient)
    TObjectPtr<UCommonActivatableWidgetStack> MenuStack;
    UPROPERTY(Transient)
    TObjectPtr<UOverlay> NoticeLayer;
};
