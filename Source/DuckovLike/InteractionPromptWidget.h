#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "InteractionPromptWidget.generated.h"

class UBorder;
class UTextBlock;

// 지금 상호작용할 수 있는 대상(pill)과 마지막 상호작용 결과 한 줄을 화면 아래쪽에 보인다.
// 입력을 받지 않고 root layout의 Notice layer에 붙는다. controller가 대상이나 결과가 바뀔 때만 갱신한다.
UCLASS(meta=(DisableNativeTick))
class DUCKOVLIKE_API UInteractionPromptWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    // 빈 text면 pill을 숨긴다.
    void SetPrompt(const FText& Text);
    void SetMessage(const FText& Text);
protected:
    virtual void NativeOnInitialized() override;
private:
    UPROPERTY(Transient)
    TObjectPtr<UBorder> PromptPill;
    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> PromptText;
    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> MessageText;
};
