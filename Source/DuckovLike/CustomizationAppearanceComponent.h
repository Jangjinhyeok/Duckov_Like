#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CustomizationCatalog.h"
#include "CustomizationTypes.h"
#include "CustomizationAppearanceComponent.generated.h"

class UAnimationAsset;
class UMaterialInstanceDynamic;
class UMeshComponent;
class USceneComponent;
class USkeletalMeshComponent;

UCLASS()
class DUCKOVLIKE_API UCustomizationAppearanceComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UCustomizationAppearanceComponent();
    // 모든 슬롯의 후보 값과 리소스를 먼저 검증·준비하고, 전부 통과했을 때만 한 번에 교체한다.
    // 실패하면 표시 중인 외형(mesh·MID·가시성)은 하나도 바뀌지 않는다. 실패 슬롯은 GetLastFailure()로 확인한다.
    ECustomizationFailure TryApply(const FCustomizationProfile& Candidate);
    void ClearPreview();
    // 표시 중인 외형이 어느 Profile로 준비됐는지다. 성공한 TryApply로만 바뀌고 ClearPreview로 비워진다.
    bool HasAppliedProfile() const { return bHasApplied; }
    const FCustomizationProfile& GetAppliedProfile() const { return AppliedProfile; }
    // 마지막 TryApply의 결과다. 성공하면 None이며 슬롯과 무관한 실패는 bHasSlot이 false다.
    const FCustomizationFailureDetail& GetLastFailure() const { return LastFailure; }
    // 첫 슬롯(눈)의 표시다. 슬롯별 표시는 GetDisplayedSlotMesh/MID로 읽는다.
    UMeshComponent* GetDisplayedMesh() const { return DisplayedMeshes[0]; }
    UMaterialInstanceDynamic* GetDisplayedMID() const { return DisplayedMIDs[0]; }
    UMeshComponent* GetDisplayedSlotMesh(ECustomizationSlot Slot) const { return DisplayedMeshes[static_cast<int32>(Slot)]; }
    UMaterialInstanceDynamic* GetDisplayedSlotMID(ECustomizationSlot Slot) const { return DisplayedMIDs[static_cast<int32>(Slot)]; }
    USkeletalMeshComponent* GetDisplayedBody() const { return DisplayedBody; }
    UMaterialInstanceDynamic* GetDisplayedBodyMID() const { return DisplayedBodyMID; }
    // 카탈로그 row의 리소스를 돌려준다. 등록되지 않은 ID는 기본 값이다.
    static FCustomizationPartResources GetDefaultResources(FName PartId);
    // 표시 mesh를 붙일 부모다. 지정하지 않으면 owner의 root에 붙는다.
    // 발밑 원점 mesh를 중심 원점 capsule에 맞출 때처럼 owner root와 기준점이 다를 때 쓴다.
    void SetAttachParent(USceneComponent* Parent) { AttachParent = Parent; }
    // 표시 body가 재생할 애니메이션이다. 부위 파츠는 body pose를 따른다(leader pose). body만 tick한다.
    // 비워 두면 tick 없이 reference pose로 고정한다(커스터마이징 미리보기). 바꾸면 다음 TryApply가 body를 새로 준비한다.
    void SetBodyAnimation(UAnimationAsset* Animation) { BodyAnimation = Animation; }
    // BlendSpace의 첫 축 입력이다. 새로 준비하는 body도 이 값으로 시작한다.
    void SetBodyAnimationInput(float Value);

    // 잘못된 리소스는 정상 UI catalog에 등록하지 않고 자동 검사의 seam에서만 교체한다.
    void SetResourcesForTests(FName PartId, const FCustomizationPartResources& Resources);
    void SetBodyMeshForTests(const FSoftObjectPath& MeshPath) { BodyMeshPath = MeshPath; }

protected:
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    // 카탈로그 대신 쓰는 시험용 리소스다. 비어 있으면 카탈로그 row를 쓴다.
    TMap<FName, FCustomizationPartResources> TestResources;
    FSoftObjectPath BodyMeshPath = GetDefaultBodyMeshPath();
    bool bPreparing = false;
    FCustomizationFailureDetail LastFailure;
    FCustomizationProfile AppliedProfile;
    bool bHasApplied = false;

    static FSoftObjectPath GetDefaultBodyMeshPath();
    FCustomizationFailureDetail Prepare(const FCustomizationProfile& Candidate);

    UPROPERTY(Transient)
    TObjectPtr<USceneComponent> AttachParent;

    UPROPERTY(Transient)
    TObjectPtr<UAnimationAsset> BodyAnimation;
    float BodyAnimationInput = 0.0f;

    // 슬롯 순서(ECustomizationSlot)로 표시 중인 파츠 mesh와 MID다.
    UPROPERTY(Transient)
    TObjectPtr<UMeshComponent> DisplayedMeshes[CustomizationSlotCount];

    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> DisplayedMIDs[CustomizationSlotCount];

    UPROPERTY(Transient)
    TObjectPtr<USkeletalMeshComponent> DisplayedBody;

    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> DisplayedBodyMID;
};
