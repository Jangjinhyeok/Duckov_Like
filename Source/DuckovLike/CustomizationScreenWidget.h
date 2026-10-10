#pragma once

#include "CoreMinimal.h"
#include "CommonActivatableWidget.h"
#include "CustomizationScreenWidget.generated.h"

class UCustomizationModel;
class UCustomizationViewModel;
class ACustomizationPreviewActor;
class UButton;
class USlider;
class UImage;
class UTextBlock;
class UScrollBox;

UCLASS(meta=(DisableNativeTick))
class DUCKOVLIKE_API UCustomizationScreenWidget : public UCommonActivatableWidget
{
    GENERATED_BODY()
public:
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
    UPROPERTY(Transient)
    TObjectPtr<UButton> PartA;
    UPROPERTY(Transient)
    TObjectPtr<UButton> PartB;
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
    UPROPERTY(Transient)
    TObjectPtr<UScrollBox> ControlsScroll;
    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> ValueText;
    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> FailureText;
    UPROPERTY(Transient)
    TObjectPtr<UImage> PreviewImage;
    FDelegateHandle StateHandle;
    FDelegateHandle FailureHandle;
    bool bSynchronizing = false;
    void Refresh();
    void OnStateChanged(UObject*, UE::FieldNotification::FFieldId);
    void OnFailureChanged(UObject*, UE::FieldNotification::FFieldId);
    void ReleasePreview();
    UFUNCTION()
    void SelectA();
    UFUNCTION()
    void SelectB();
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
