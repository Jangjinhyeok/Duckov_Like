#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameHudWidget.generated.h"

class UTextBlock;
class UVerticalBox;

// 베이스먼트·레이드의 HUD다. 원작 배치를 따르되 이 게임에서 동작하는 요소만 둔다:
// 왼쪽 아래 장소, 오른쪽 위 조작 설명(O로 펼침), 아래 중앙 마지막 결과 문구, 오른쪽 아래 탄약(총을 장착했을 때만).
// 입력을 받지 않으며 controller가 값이 바뀔 때만 갱신한다.
UCLASS(meta=(DisableNativeTick))
class DUCKOVLIKE_API UGameHudWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    void SetLocation(const FText& Text);
    void SetMessage(const FText& Text);
    // 지금 문구가 Text와 같을 때만 지운다. 다른 결과 문구는 남긴다.
    void ClearMessageIf(const FText& Text);
    // 탄창 잔탄과 가방 탄약 수다. 재장전 중이면 그 상태를 함께 보인다.
    void SetAmmo(int32 Rounds, int32 Carried, bool bReloading);
    void HideAmmo();
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
    TObjectPtr<UTextBlock> AmmoText;
    UPROPERTY(Transient)
    TObjectPtr<UVerticalBox> ControlsList;
};
