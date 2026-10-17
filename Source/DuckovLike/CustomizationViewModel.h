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
    ECustomizationFailure SelectSlotPart(ECustomizationSlot Slot, FName PartId);
    ECustomizationFailure SetSkinColor(int32 PaletteIndex);
    ECustomizationFailure SetSlotColor(ECustomizationSlot Slot, int32 PaletteIndex);
    ECustomizationFailure SetHeadShape(float Value);
    ECustomizationFailure Apply();
    ECustomizationFailure Reset();
    ECustomizationFailure Cancel();
    // Draft의 슬롯별 파츠 ID(ECustomizationSlot 순서)다.
    UFUNCTION(BlueprintPure, FieldNotify, Category="Customization")
    TArray<FName> GetPartIds() const;
    // Draft의 색 index다. 0은 피부색, 1부터 슬롯 순서이며 색이 없는 슬롯은 INDEX_NONE이다.
    UFUNCTION(BlueprintPure, FieldNotify, Category="Customization")
    TArray<int32> GetColorIndices() const;
    UFUNCTION(BlueprintPure, FieldNotify, Category="Customization")
    float GetHeadShape() const;
    UFUNCTION(BlueprintPure, FieldNotify, Category="Customization")
    FText GetFailureText() const;
    FName GetSlotPartId(ECustomizationSlot Slot) const;
    int32 GetSlotColor(ECustomizationSlot Slot) const;
    int32 GetSkinColor() const;
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
    FCustomizationFailureDetail LastFailure;
    FDelegateHandle ChangedHandle;
    ECustomizationFailure Present(const FCustomizationProfile& Candidate);
    ECustomizationFailure Report(ECustomizationFailure Result);
    ECustomizationFailure Edit(TFunctionRef<void(FCustomizationProfile&)> Change);
    void OnModelChanged();
};
