#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameHudWidget.generated.h"

class UTextBlock;
class UVerticalBox;

// 베이스먼트·레이드의 HUD다. 원작 배치를 따르되 이 게임에서 동작하는 요소만 둔다:
// 왼쪽 아래 장소, 오른쪽 위 조작 설명(O로 펼침), 아래 중앙 마지막 결과 문구.
// 입력을 받지 않으며 controller가 값이 바뀔 때만 갱신한다.
UCLASS(meta=(DisableNativeTick))
class DUCKOVLIKE_API UGameHudWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    void SetLocation(const FText& Text);
    void SetMessage(const FText& Text);
    void ToggleControls();
    bool AreControlsExpanded() const;
protected:
    virtual void NativeOnInitialized() override;
private:
    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> LocationText;
    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> MessageText;
    UPROPERTY(Transient)
    TObjectPtr<UVerticalBox> ControlsList;
};
