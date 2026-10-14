#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "InteractionPromptWidget.generated.h"

class UTextBlock;

// 원작처럼 물체 위의 ◎에서 오른쪽으로 뻗는 파란 pill이다: "◎ 동작 [F]".
// 화면 고정이 아니라 screen space widget component에 담겨 상호작용 대상 위치에 붙는다. 입력을 받지 않는다.
UCLASS(meta=(DisableNativeTick))
class DUCKOVLIKE_API UInteractionPromptWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    void SetAction(const FText& ActionName);
protected:
    virtual void NativeOnInitialized() override;
private:
    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> PromptText;
};
