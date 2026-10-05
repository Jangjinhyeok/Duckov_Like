#pragma once

#include "CoreMinimal.h"
#include "CustomizationTypes.h"
#include "CustomizationModel.generated.h"

DECLARE_MULTICAST_DELEGATE(FOnCustomizationChanged);

UCLASS()
class DUCKOVLIKE_API UCustomizationModel : public UObject
{
    GENERATED_BODY()

public:
    const FCustomizationProfile& GetProfile() const { return Profile; }
    const FCustomizationProfile& GetDraft() const { return Draft; }
    bool IsEditing() const { return bEditing; }
    FOnCustomizationChanged& OnChanged() { return Changed; }

    // callback은 표시 준비와 교체를 완료한 경우에만 None을 반환한다.
    ECustomizationFailure BeginEdit(TFunctionRef<ECustomizationFailure(const FCustomizationProfile&)> Present);
    ECustomizationFailure TryEdit(const FCustomizationProfile& Candidate,
        TFunctionRef<ECustomizationFailure(const FCustomizationProfile&)> Present);
    ECustomizationFailure Apply(TFunctionRef<ECustomizationFailure(const FCustomizationProfile&)> Present);
    ECustomizationFailure Reset(TFunctionRef<ECustomizationFailure(const FCustomizationProfile&)> Present);
    ECustomizationFailure Cancel(TFunctionRef<ECustomizationFailure(const FCustomizationProfile&)> Present);

private:
    UPROPERTY()
    FCustomizationProfile Profile;

    UPROPERTY()
    FCustomizationProfile Draft;

    bool bEditing = false;
    bool bChanging = false;
    FOnCustomizationChanged Changed;

    ECustomizationFailure Change(const FCustomizationProfile& Candidate, bool bNextEditing, bool bCommit,
        TFunctionRef<ECustomizationFailure(const FCustomizationProfile&)> Present);
};
