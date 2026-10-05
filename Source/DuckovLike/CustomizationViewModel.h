#pragma once

#include "CoreMinimal.h"
#include "MVVMViewModelBase.h"
#include "CustomizationTypes.h"
#include "CustomizationViewModel.generated.h"

class UCustomizationModel;
class UCustomizationAppearanceComponent;

UCLASS()
class DUCKOVLIKE_API UCustomizationViewModel : public UMVVMViewModelBase
{
    GENERATED_BODY()
public:
    void Bind(UCustomizationModel* InModel, UCustomizationAppearanceComponent* InAppearance);
    ECustomizationFailure Open();
    ECustomizationFailure SelectPart(FName PartId);
    ECustomizationFailure SetHue(float Value);
    ECustomizationFailure SetShape(float Value);
    ECustomizationFailure Apply();
    ECustomizationFailure Reset();
    ECustomizationFailure Cancel();
    UFUNCTION(BlueprintPure, FieldNotify, Category="Customization")
    FName GetPartId() const;
    UFUNCTION(BlueprintPure, FieldNotify, Category="Customization")
    float GetHue() const;
    UFUNCTION(BlueprintPure, FieldNotify, Category="Customization")
    float GetShape() const;
    UFUNCTION(BlueprintPure, FieldNotify, Category="Customization")
    FText GetFailureText() const;
    virtual void BeginDestroy() override;
private:
    UPROPERTY(Transient)
    TObjectPtr<UCustomizationModel> Model;
    UPROPERTY(Transient)
    TObjectPtr<UCustomizationAppearanceComponent> Appearance;
    UPROPERTY(Transient)
    TObjectPtr<UCustomizationModel> PendingModel;
    UPROPERTY(Transient)
    TObjectPtr<UCustomizationAppearanceComponent> PendingAppearance;
    bool bNotifying = false;
    bool bPendingBind = false;
    ECustomizationFailure LastFailure = ECustomizationFailure::None;
    FDelegateHandle ChangedHandle;
    ECustomizationFailure Present(const FCustomizationProfile& Candidate);
    ECustomizationFailure Report(ECustomizationFailure Result);
    void OnModelChanged();
};
