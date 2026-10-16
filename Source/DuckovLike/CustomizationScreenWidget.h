#pragma once

#include "CoreMinimal.h"
#include "CommonActivatableWidget.h"
#include "Components/Button.h"
#include "CustomizationTypes.h"
#include "CustomizationScreenWidget.generated.h"

class UCustomizationModel;
class UCustomizationViewModel;
class ACustomizationPreviewActor;
class USlider;
class UImage;
class UTextBlock;
class UWidget;
class UWidgetSwitcher;

// 카탈로그 row 하나를 고르는 버튼이다. 어느 슬롯의 어느 파츠인지 스스로 알고 클릭을 알린다.
UCLASS()
class DUCKOVLIKE_API UCustomizationPartButton : public UButton
{
    GENERATED_BODY()
public:
    DECLARE_EVENT_OneParam(UCustomizationPartButton, FOnPicked, UCustomizationPartButton&);
    FOnPicked& OnPicked() { return Picked; }
    // 슬롯과 파츠 ID를 정하고 클릭 delegate를 연결한다. PartId가 None이면 "없음" 선택이다.
    void Setup(ECustomizationSlot InSlot, FName InPartId);
    ECustomizationSlot GetPartSlot() const { return PartSlot; }
    FName GetPartId() const { return PartId; }
private:
    ECustomizationSlot PartSlot = ECustomizationSlot::Crest;
    FName PartId;
    FOnPicked Picked;
    UFUNCTION()
    void Forward();
};

UCLASS(meta=(DisableNativeTick))
class DUCKOVLIKE_API UCustomizationScreenWidget : public UCommonActivatableWidget
{
    GENERATED_BODY()
public:
    // 확인(true) 또는 취소·Back(false)으로 화면이 닫힌 뒤 알린다. 화면을 연 쪽이 다음 흐름을 정한다.
    DECLARE_EVENT_OneParam(UCustomizationScreenWidget, FOnClosed, bool /*bApplied*/);
    FOnClosed& OnClosed() { return Closed; }
    UCustomizationScreenWidget(const FObjectInitializer& Initializer);
    void SetSession(UCustomizationModel* InModel) { Model = InModel; }
    UCustomizationViewModel* GetViewModel() const { return ViewModel; }
    ACustomizationPreviewActor* GetPreview() const { return Preview; }
    virtual void NativeOnActivated() override;
    virtual void NativeOnDeactivated() override;
    virtual void NativeDestruct() override;
protected:
    virtual void NativeOnInitialized() override;
    virtual bool NativeOnHandleBackAction() override;
    virtual UWidget* NativeGetDesiredFocusTarget() const override;
    virtual TOptional<FUIInputConfig> GetDesiredInputConfig() const override;
private:
    UPROPERTY(Transient)
    TObjectPtr<UCustomizationModel> Model;
    UPROPERTY(Transient)
    TObjectPtr<UCustomizationViewModel> ViewModel;
    UPROPERTY(Transient)
    TObjectPtr<ACustomizationPreviewActor> Preview;
    // 카탈로그 row마다 만든 파츠 선택 버튼이다. 선택 슬롯에는 "없음" 버튼이 하나 더 있다.
    UPROPERTY(Transient)
    TArray<TObjectPtr<UCustomizationPartButton>> PartButtons;
    UPROPERTY(Transient)
    TObjectPtr<UButton> FirstCrestButton;
    UPROPERTY(Transient)
    TObjectPtr<USlider> HueSlider;
    UPROPERTY(Transient)
    TObjectPtr<USlider> ShapeSlider;
    UPROPERTY(Transient)
    TObjectPtr<USlider> EyeSizeSlider;
    UPROPERTY(Transient)
    TObjectPtr<USlider> BeakLengthSlider;
    UPROPERTY(Transient)
    TObjectPtr<USlider> BodyLengthSlider;
    // 원작처럼 왼쪽 부위 탭이 옆 조절 열의 내용을 바꾼다. 탭 순서 = Switcher 순서.
    UPROPERTY(Transient)
    TObjectPtr<UWidgetSwitcher> TabSwitcher;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UButton>> TabButtons;
    // 탭마다 조절 열의 첫 입력. 탭 ↔ 조절 열 keyboard 이동에 쓴다.
    UPROPERTY(Transient)
    TArray<TObjectPtr<UWidget>> TabFirstControls;
    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> TabTitle;
    TArray<FText> TabTitles;
    // 슬라이더 옆 숫자. 같은 index의 슬라이더 값을 보인다.
    UPROPERTY(Transient)
    TArray<TObjectPtr<USlider>> ValueSliders;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UTextBlock>> ValueTexts;
    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> FailureText;
    UPROPERTY(Transient)
    TObjectPtr<UImage> PreviewImage;
    FDelegateHandle StateHandle;
    FDelegateHandle FailureHandle;
    FOnClosed Closed;
    bool bSynchronizing = false;
    void Refresh();
    void OnStateChanged(UObject*, UE::FieldNotification::FFieldId);
    void OnFailureChanged(UObject*, UE::FieldNotification::FFieldId);
    void ReleasePreview();
    void SelectTab(int32 Index);
    UFUNCTION()
    void ShowHeadTab();
    UFUNCTION()
    void ShowEyeTab();
    UFUNCTION()
    void ShowBeakTab();
    UFUNCTION()
    void ShowBodyTab();
    UFUNCTION()
    UWidget* NavigateToTab(EUINavigation Direction);
    UFUNCTION()
    UWidget* NavigateToControls(EUINavigation Direction);
    void HandlePartPicked(UCustomizationPartButton& Button);
    UFUNCTION()
    void ChangeHue(float Value);
    UFUNCTION()
    void ChangeShape(float Value);
    UFUNCTION()
    void ChangeEyeSize(float Value);
    UFUNCTION()
    void ChangeBeakLength(float Value);
    UFUNCTION()
    void ChangeBodyLength(float Value);
    UFUNCTION()
    void Apply();
    UFUNCTION()
    void Reset();
    UFUNCTION()
    void Cancel();
    UFUNCTION()
    void TestFailure();
};
