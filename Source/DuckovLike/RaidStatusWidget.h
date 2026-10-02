#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "RaidStatusWidget.generated.h"

class UTextBlock;

// 세션 상태와 조작 안내만 표시하며 입력과 인벤토리 상태를 소유하지 않는다.
UCLASS(meta=(DisableNativeTick))
class DUCKOVLIKE_API URaidStatusWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    void SetStatus(const FText& Text);
protected:
    virtual void NativeOnInitialized() override;
private:
    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> StatusText;
};
