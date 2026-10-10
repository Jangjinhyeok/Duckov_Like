#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CustomizationTypes.h"
#include "CustomizationAppearanceComponent.generated.h"

class UMaterialInstanceDynamic;
class UMeshComponent;
class USkeletalMeshComponent;

enum class ECustomizationMeshKind : uint8 { StaticPlaceholder, Skeletal };

// Profile과 별개인 적용 리소스 계약이다. 정상 catalog는 A/B 둘뿐이다.
struct DUCKOVLIKE_API FCustomizationPartResources
{
    ECustomizationMeshKind MeshKind = ECustomizationMeshKind::StaticPlaceholder;
    FSoftObjectPath Mesh;
    FSoftObjectPath ExpectedSkeleton;
    FSoftObjectPath Material;
    int32 MaterialSlot = 0;
    FName ColorParameter = TEXT("Color");
    FName Morph = TEXT("Shape");
    FName MaskParameter = NAME_None;
    FSoftObjectPath Mask;
    bool bMaskRequired = false;
};

UCLASS()
class DUCKOVLIKE_API UCustomizationAppearanceComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UCustomizationAppearanceComponent();
    ECustomizationFailure TryApply(const FCustomizationProfile& Candidate);
    void ClearPreview();
    UMeshComponent* GetDisplayedMesh() const { return DisplayedMesh; }
    UMaterialInstanceDynamic* GetDisplayedMID() const { return DisplayedMID; }
    USkeletalMeshComponent* GetDisplayedBody() const { return DisplayedBody; }
    UMaterialInstanceDynamic* GetDisplayedBodyMID() const { return DisplayedBodyMID; }
    static FCustomizationPartResources GetDefaultResources(FName PartId);

    // 잘못된 리소스는 정상 UI catalog에 등록하지 않고 자동 검사의 seam에서만 교체한다.
    void SetResourcesForTests(FName PartId, const FCustomizationPartResources& Resources);
    void SetBodyMeshForTests(const FSoftObjectPath& MeshPath) { BodyMeshPath = MeshPath; }

protected:
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    FCustomizationPartResources PartA;
    FCustomizationPartResources PartB;
    FSoftObjectPath BodyMeshPath = FSoftObjectPath(TEXT("/Game/Customization/Prototype/SK_CustomizationBody.SK_CustomizationBody"));
    bool bPreparing = false;

    UPROPERTY(Transient)
    TObjectPtr<UMeshComponent> DisplayedMesh;

    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> DisplayedMID;

    UPROPERTY(Transient)
    TObjectPtr<USkeletalMeshComponent> DisplayedBody;

    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> DisplayedBodyMID;
};
