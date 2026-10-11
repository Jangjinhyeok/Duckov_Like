#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "LoadingScreenWidget.generated.h"

// 맵을 불러오는 동안 마지막으로 그려지는 화면이다. 입력을 받지 않으며 root layout의 Notice layer에 붙는다.
UCLASS(meta=(DisableNativeTick))
class DUCKOVLIKE_API ULoadingScreenWidget : public UUserWidget
{
    GENERATED_BODY()
protected:
    virtual void NativeOnInitialized() override;
};
