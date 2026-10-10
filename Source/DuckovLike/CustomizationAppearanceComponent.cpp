#include "CustomizationAppearanceComponent.h"

#include "Animation/MorphTarget.h"
#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
    const FName BodyColorParameter(TEXT("Color"));
    const FName BodyMorphs[] = {TEXT("EyeSmall"), TEXT("EyeLarge"), TEXT("BeakShort"),
        TEXT("BeakLong"), TEXT("BodyShort"), TEXT("BodyLong")};

    bool HasSkeletalRenderData(const USkeletalMesh* Mesh)
    {
        if (!Mesh || Mesh->IsCompiling() || Mesh->GetRefSkeleton().GetNum() == 0) { return false; }
        const FSkeletalMeshRenderData* RenderData = Mesh->GetResourceForRendering();
        return RenderData && RenderData->IsInitialized() && RenderData->LODRenderData.IsValidIndex(0)
            && RenderData->LODRenderData[0].GetNumVertices() > 0;
    }

    bool HasSkeletalUsage(const UMaterialInterface* Material, bool bMorphRequired)
    {
        const UMaterial* Base = Material ? Material->GetMaterial() : nullptr;
        // runtime에서는 공유 material의 usage flag나 dirty 상태를 바꾸지 않는다.
        return Base && Base->MaterialDomain == MD_Surface && (Base->bUsedAsSpecialEngineMaterial
            || (Base->GetUsageByFlag(MATUSAGE_SkeletalMesh)
                && (!bMorphRequired || Base->GetUsageByFlag(MATUSAGE_MorphTargets))));
    }

    bool MatchesMaterial(const UMaterialInstanceDynamic* MID, const UMaterialInterface* Parent,
        FName ColorParameter, const FLinearColor& Color, FName MaskParameter = NAME_None, UTexture* Mask = nullptr)
    {
        FLinearColor CurrentColor;
        UTexture* CurrentMask = nullptr;
        return MID && MID->Parent == Parent && MID->VectorParameterValues.Num() == 1
            && MID->VectorParameterValues[0].ParameterInfo.Name == ColorParameter
            && MID->GetVectorParameterValue(FMaterialParameterInfo(ColorParameter), CurrentColor) && CurrentColor == Color
            && MID->TextureParameterValues.Num() == (Mask ? 1 : 0)
            && (!Mask || (MID->TextureParameterValues[0].ParameterInfo.Name == MaskParameter
                && MID->GetTextureParameterValue(FMaterialParameterInfo(MaskParameter), CurrentMask) && CurrentMask == Mask));
    }

    bool HasMorphData(const USkeletalMesh* Mesh, TConstArrayView<FName> Morphs)
    {
        for (FName Name : Morphs)
        {
            int32 Index = INDEX_NONE;
            const UMorphTarget* Morph = Name.IsNone() ? nullptr : Mesh->FindMorphTargetAndIndex(Name, Index);
            if (!Morph || Index < 0 || !Morph->HasDataForLOD(0)) { return false; }
        }
        return true;
    }

    bool HasMorphBatches(const USkeletalMesh* Mesh, TConstArrayView<FName> Morphs)
    {
        const FMorphTargetVertexInfoBuffers& Buffers = Mesh->GetResourceForRendering()->LODRenderData[0].MorphTargetVertexInfoBuffers;
        for (FName Name : Morphs)
        {
            int32 Index = INDEX_NONE;
            Mesh->FindMorphTargetAndIndex(Name, Index);
            if (Index < 0 || static_cast<uint32>(Index) >= Buffers.GetNumMorphs() || Buffers.GetNumBatches(Index) == 0)
            {
                return false;
            }
        }
        return true;
    }

    bool HasPreparedPose(const USkeletalMeshComponent* Component, TConstArrayView<FName> Morphs,
        TConstArrayView<float> Weights)
    {
        const USkeletalMesh* Mesh = Component ? Component->GetSkeletalMeshAsset() : nullptr;
        if (!Mesh || Component->GetComponentSpaceTransforms().Num() != Mesh->GetRefSkeleton().GetNum()
            || Component->MorphTargetWeights.Num() != Mesh->GetMorphTargets().Num()
            || Component->GetMorphTargetCurves().Num() != Morphs.Num()) { return false; }
        for (const FTransform& Bone : Component->GetComponentSpaceTransforms())
        {
            if (Bone.ContainsNaN()) { return false; }
        }
        int32 ActiveCount = 0;
        for (int32 Index = 0; Index < Morphs.Num(); ++Index)
        {
            int32 MorphIndex = INDEX_NONE;
            const UMorphTarget* Target = Mesh->FindMorphTargetAndIndex(Morphs[Index], MorphIndex);
            if (!Target || !Component->GetMorphTargetCurves().Contains(Morphs[Index])
                || Component->GetMorphTarget(Morphs[Index]) != Weights[Index]) { return false; }
            // UE 5.7은 UE_SMALL_NUMBER 이하 weight를 active render set에서 제외한다.
            if (Weights[Index] > UE_SMALL_NUMBER)
            {
                const int32* ActiveIndex = Component->ActiveMorphTargets.Find(Target);
                if (!ActiveIndex || *ActiveIndex != MorphIndex) { return false; }
                ++ActiveCount;
            }
        }
        if (Component->ActiveMorphTargets.Num() != ActiveCount) { return false; }
        for (int32 Index = 0; Index < Component->MorphTargetWeights.Num(); ++Index)
        {
            float Expected = 0.0f;
            for (int32 MorphIndex = 0; MorphIndex < Morphs.Num(); ++MorphIndex)
            {
                if (Mesh->GetMorphTargets()[Index]->GetFName() == Morphs[MorphIndex]
                    && Weights[MorphIndex] > UE_SMALL_NUMBER) { Expected = Weights[MorphIndex]; }
            }
            if (Component->MorphTargetWeights[Index] != Expected) { return false; }
        }
        return true;
    }

    void ConfigureSkeletal(USkeletalMeshComponent* Component, USkeletalMesh* Mesh)
    {
        Component->PrimaryComponentTick.bCanEverTick = false;
        Component->PrimaryComponentTick.bStartWithTickEnabled = false;
        Component->bEnableUpdateRateOptimizations = false;
        Component->SetDisablePostProcessBlueprint(true);
        Component->SetForceRefPose(true);
        Component->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
        Component->SetSkeletalMesh(Mesh);
        // C3 시험 에셋의 LOD0와 reference pose만 사용한다.
        Component->SetForcedLOD(1);
    }

    void RegisterHidden(UMeshComponent* Component, AActor* Owner, const FTransform& Transform)
    {
        Component->SetVisibility(false);
        Component->SetHiddenInGame(true);
        Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Component->SetGenerateOverlapEvents(false);
        Component->SetMobility(EComponentMobility::Movable);
        Component->SetupAttachment(Owner->GetRootComponent());
        Component->SetRelativeTransform(Transform);
        Component->RegisterComponent();
    }

    void RefreshSkeletal(USkeletalMeshComponent* Component)
    {
        // null TickFunction으로 동기 평가하고 실제 Morph render weight를 준비한다.
        Component->RefreshBoneTransforms();
        Component->UpdateBounds();
        Component->MarkRenderTransformDirty();
        Component->MarkRenderDynamicDataDirty();
    }

    void DestroyPreview(UMeshComponent* Component)
    {
        if (!Component) { return; }
        Component->SetVisibility(false);
        Component->SetHiddenInGame(true);
        Component->DestroyComponent();
    }
}

UCustomizationAppearanceComponent::UCustomizationAppearanceComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    PartA = GetDefaultResources(TEXT("A"));
    PartB = GetDefaultResources(TEXT("B"));
}

FCustomizationPartResources UCustomizationAppearanceComponent::GetDefaultResources(FName PartId)
{
    FCustomizationPartResources Resources;
    Resources.MeshKind = ECustomizationMeshKind::Skeletal;
    Resources.Mesh = FSoftObjectPath(PartId == TEXT("B")
        ? TEXT("/Game/Customization/Prototype/SK_CustomizationPartB.SK_CustomizationPartB")
        : TEXT("/Game/Customization/Prototype/SK_CustomizationPartA.SK_CustomizationPartA"));
    Resources.ExpectedSkeleton = FSoftObjectPath(TEXT("/Game/Customization/Prototype/SK_CustomizationBody_Skeleton.SK_CustomizationBody_Skeleton"));
    Resources.Material = FSoftObjectPath(TEXT("/Game/Customization/Prototype/M_CustomizationColor.M_CustomizationColor"));
    return Resources;
}

void UCustomizationAppearanceComponent::SetResourcesForTests(FName PartId,
    const FCustomizationPartResources& Resources)
{
    if (PartId == TEXT("A")) { PartA = Resources; }
    if (PartId == TEXT("B")) { PartB = Resources; }
}

ECustomizationFailure UCustomizationAppearanceComponent::TryApply(const FCustomizationProfile& Candidate)
{
    if (bPreparing) { return ECustomizationFailure::OperationInProgress; }
    const ECustomizationFailure Validation = ValidateCustomizationProfile(Candidate);
    if (Validation != ECustomizationFailure::None) { return Validation; }
    AActor* Owner = GetOwner();
    if (!Owner || !Owner->GetWorld() || !Owner->GetRootComponent() || Owner->IsActorBeingDestroyed())
    {
        return ECustomizationFailure::PreparationFailed;
    }
    TGuardValue<bool> Guard(bPreparing, true);
    const FCustomizationPartResources& Resources = Candidate.PartId == TEXT("A") ? PartA : PartB;
    if (Resources.MeshKind != ECustomizationMeshKind::StaticPlaceholder
        && Resources.MeshKind != ECustomizationMeshKind::Skeletal)
    {
        return ECustomizationFailure::PreparationFailed;
    }
    if (Resources.MeshKind == ECustomizationMeshKind::StaticPlaceholder
        && (Candidate.EyeSize != 0.f || Candidate.BeakLength != 0.f || Candidate.BodyLength != 0.f))
    {
        return ECustomizationFailure::UnsupportedBodyAdjustment;
    }
    const FName PartMorphs[] = {Resources.Morph};
    const float PartWeights[] = {(Candidate.Shape + 1.0f) * 0.5f};
    const float BodyWeights[] = {FMath::Max(-Candidate.EyeSize, 0.f), FMath::Max(Candidate.EyeSize, 0.f),
        FMath::Max(-Candidate.BeakLength, 0.f), FMath::Max(Candidate.BeakLength, 0.f),
        FMath::Max(-Candidate.BodyLength, 0.f), FMath::Max(Candidate.BodyLength, 0.f)};
    // 동기 로드는 후보에 필요한 리소스를 강하게 보관하며 표시 component에는 아직 손대지 않는다.
    TStrongObjectPtr<UObject> LoadedMesh(Resources.Mesh.TryLoad());
    UStaticMesh* StaticMesh = Cast<UStaticMesh>(LoadedMesh.Get());
    USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(LoadedMesh.Get());
    if ((Resources.MeshKind == ECustomizationMeshKind::StaticPlaceholder && !StaticMesh)
        || (Resources.MeshKind == ECustomizationMeshKind::Skeletal && !SkeletalMesh))
    {
        return ECustomizationFailure::MissingMesh;
    }
    TStrongObjectPtr<USkeleton> Skeleton;
    TStrongObjectPtr<USkeletalMesh> BodyMesh;
    if (Resources.MeshKind == ECustomizationMeshKind::Skeletal)
    {
        Skeleton.Reset(Cast<USkeleton>(Resources.ExpectedSkeleton.TryLoad()));
        if (!Skeleton.IsValid()) { return ECustomizationFailure::MissingSkeleton; }
        if (SkeletalMesh->GetSkeleton() != Skeleton.Get()) { return ECustomizationFailure::SkeletonMismatch; }
        if (SkeletalMesh->IsCompiling()) { return ECustomizationFailure::PreparationFailed; }
        if (!HasMorphData(SkeletalMesh, PartMorphs))
        {
            return ECustomizationFailure::MissingMorph;
        }
        BodyMesh.Reset(Cast<USkeletalMesh>(BodyMeshPath.TryLoad()));
        if (!BodyMesh.IsValid()) { return ECustomizationFailure::MissingMesh; }
        if (BodyMesh->GetSkeleton() != Skeleton.Get()) { return ECustomizationFailure::SkeletonMismatch; }
        if (BodyMesh->IsCompiling()) { return ECustomizationFailure::PreparationFailed; }
        if (!HasMorphData(BodyMesh.Get(), BodyMorphs)) { return ECustomizationFailure::MissingMorph; }
        if (!HasSkeletalRenderData(SkeletalMesh) || !HasSkeletalRenderData(BodyMesh.Get()))
        {
            return ECustomizationFailure::PreparationFailed;
        }
        if (!SkeletalMesh->GetResourceForRendering()->LODRenderData[0].MorphTargetVertexInfoBuffers.IsMorphResourcesInitialized()
            || !BodyMesh->GetResourceForRendering()->LODRenderData[0].MorphTargetVertexInfoBuffers.IsMorphResourcesInitialized())
        {
            return ECustomizationFailure::PreparationFailed;
        }
        if (!HasMorphBatches(SkeletalMesh, PartMorphs) || !HasMorphBatches(BodyMesh.Get(), BodyMorphs))
        {
            return ECustomizationFailure::MissingMorph;
        }
    }
    const int32 SlotCount = StaticMesh ? StaticMesh->GetStaticMaterials().Num() : SkeletalMesh->GetMaterials().Num();
    if (Resources.MaterialSlot < 0 || Resources.MaterialSlot >= SlotCount)
    {
        return ECustomizationFailure::MissingMaterial;
    }
    TStrongObjectPtr<UMaterialInterface> Material(Cast<UMaterialInterface>(Resources.Material.TryLoad()));
    if (!Material.IsValid()) { return ECustomizationFailure::MissingMaterial; }
    if (SkeletalMesh)
    {
        if (!HasSkeletalUsage(Material.Get(), true) || BodyMesh->GetMaterials().Num() == 0)
        {
            return ECustomizationFailure::MissingMaterial;
        }
        for (int32 Slot = 1; Slot < BodyMesh->GetMaterials().Num(); ++Slot)
        {
            if (!HasSkeletalUsage(BodyMesh->GetMaterials()[Slot].MaterialInterface, true))
            {
                return ECustomizationFailure::MissingMaterial;
            }
        }
    }
    FLinearColor ExistingColor;
    if (Resources.ColorParameter.IsNone()
        || !Material->GetVectorParameterValue(FMaterialParameterInfo(Resources.ColorParameter), ExistingColor))
    {
        return ECustomizationFailure::MissingColorParameter;
    }
    if (BodyMesh.IsValid()
        && !Material->GetVectorParameterValue(FMaterialParameterInfo(BodyColorParameter), ExistingColor))
    {
        return ECustomizationFailure::MissingColorParameter;
    }
    TStrongObjectPtr<UTexture> Mask;
    if (Resources.bMaskRequired && Resources.Mask.IsNull()) { return ECustomizationFailure::MissingMask; }
    if (!Resources.Mask.IsNull() || !Resources.MaskParameter.IsNone() || Resources.bMaskRequired)
    {
        UTexture* ExistingMask = nullptr;
        if (Resources.MaskParameter.IsNone()
            || !Material->GetTextureParameterValue(FMaterialParameterInfo(Resources.MaskParameter), ExistingMask))
        {
            return ECustomizationFailure::MissingMaskParameter;
        }
        // 선택 mask 없음은 흰색 neutral texture이다. 이전 파츠의 mask를 재사용하지 않는다.
        const FSoftObjectPath MaskPath = Resources.Mask.IsNull()
            ? FSoftObjectPath(TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture")) : Resources.Mask;
        Mask.Reset(Cast<UTexture>(MaskPath.TryLoad()));
        if (!Cast<UTexture2D>(Mask.Get())) { return ECustomizationFailure::MissingMask; }
    }

    if (SkeletalMesh)
    {
        // 첫 draw의 fallback material을 성공 외형으로 공개하지 않는다. 동기 prototype 준비 경로다.
        Material->EnsureIsComplete();
        if (!Material->IsComplete() || !Material->GetMaterial()->IsComplete())
        {
            return ECustomizationFailure::PreparationFailed;
        }
        for (int32 Slot = 1; Slot < BodyMesh->GetMaterials().Num(); ++Slot)
        {
            UMaterialInterface* Auxiliary = BodyMesh->GetMaterials()[Slot].MaterialInterface;
            Auxiliary->EnsureIsComplete();
            if (!Auxiliary->IsComplete() || !Auxiliary->GetMaterial()->IsComplete())
            {
                return ECustomizationFailure::PreparationFailed;
            }
        }
    }

    const FVector Scale = StaticMesh ? FVector(1.0f, 1.0f, 1.0f + 0.35f * Candidate.Shape) : FVector::OneVector;
    const FTransform Transform(FQuat::Identity, FVector::ZeroVector, Scale);
    UStaticMeshComponent* CurrentStatic = Cast<UStaticMeshComponent>(DisplayedMesh);
    USkeletalMeshComponent* CurrentSkeletal = Cast<USkeletalMeshComponent>(DisplayedMesh);
    const bool bSameMesh = DisplayedMesh && DisplayedMesh->IsRegistered() && DisplayedMID
        && DisplayedMesh->GetMaterial(Resources.MaterialSlot) == DisplayedMID
        && ((StaticMesh && CurrentStatic && CurrentStatic->GetStaticMesh() == StaticMesh)
            || (SkeletalMesh && CurrentSkeletal && CurrentSkeletal->GetSkeletalMeshAsset() == SkeletalMesh));
    const FLinearColor Color = GetCustomizationColor(Candidate.Hue);
    const bool bSameBody = BodyMesh.IsValid()
        ? DisplayedBody && DisplayedBody->IsRegistered() && DisplayedBody->GetSkeletalMeshAsset() == BodyMesh.Get()
            && DisplayedBody->GetMaterial(0) == DisplayedBodyMID
            && MatchesMaterial(DisplayedBodyMID, Material.Get(), BodyColorParameter, Color)
            && DisplayedBody->GetRelativeTransform().Equals(FTransform::Identity)
            && HasPreparedPose(DisplayedBody, BodyMorphs, BodyWeights)
        : !DisplayedBody && !DisplayedBodyMID;
    if (bSameMesh && bSameBody
        && MatchesMaterial(DisplayedMID, Material.Get(), Resources.ColorParameter, Color, Resources.MaskParameter, Mask.Get())
        && DisplayedMesh->GetRelativeLocation() == FVector::ZeroVector
        && DisplayedMesh->GetRelativeScale3D() == Scale
        && DisplayedMesh->GetRelativeTransform().GetRotation() == FQuat::Identity
        && (!CurrentSkeletal || HasPreparedPose(CurrentSkeletal, PartMorphs, PartWeights)))
    {
        return ECustomizationFailure::None;
    }
    // 새 override는 이전 파츠의 Morph와 mask를 상속하지 않는다.
    TStrongObjectPtr<UMaterialInstanceDynamic> MID(UMaterialInstanceDynamic::Create(Material.Get(), Owner));
    if (!MID.IsValid()) { return ECustomizationFailure::PreparationFailed; }
    MID->SetVectorParameterValue(Resources.ColorParameter, Color);
    if (Mask.IsValid()) { MID->SetTextureParameterValue(Resources.MaskParameter, Mask.Get()); }
    FLinearColor AppliedColor;
    UTexture* AppliedMask = nullptr;
    if (!MID->GetVectorParameterValue(FMaterialParameterInfo(Resources.ColorParameter), AppliedColor)
        || !AppliedColor.Equals(Color)
        || (Mask.IsValid() && (!MID->GetTextureParameterValue(FMaterialParameterInfo(Resources.MaskParameter), AppliedMask)
            || AppliedMask != Mask.Get())))
    {
        return ECustomizationFailure::PreparationFailed;
    }
    TStrongObjectPtr<UMaterialInstanceDynamic> BodyMID;
    if (BodyMesh.IsValid())
    {
        BodyMID.Reset(UMaterialInstanceDynamic::Create(Material.Get(), Owner));
        if (!BodyMID.IsValid()) { return ECustomizationFailure::PreparationFailed; }
        BodyMID->SetVectorParameterValue(BodyColorParameter, Color);
        if (!MatchesMaterial(BodyMID.Get(), Material.Get(), BodyColorParameter, Color))
        {
            return ECustomizationFailure::PreparationFailed;
        }
    }
    if (bSameMesh && StaticMesh && !DisplayedBody)
    {
        DisplayedMesh->SetMaterial(Resources.MaterialSlot, MID.Get());
        DisplayedMesh->SetRelativeTransform(Transform);
        DisplayedMID = MID.Get();
        return ECustomizationFailure::None;
    }

    UMeshComponent* Staged = nullptr;
    if (SkeletalMesh)
    {
        USkeletalMeshComponent* Skeletal = NewObject<USkeletalMeshComponent>(Owner, NAME_None, RF_Transient);
        ConfigureSkeletal(Skeletal, SkeletalMesh);
        Skeletal->ClearMorphTargets();
        Skeletal->SetMorphTarget(Resources.Morph, PartWeights[0], false);
        Staged = Skeletal;
    }
    else
    {
        UStaticMeshComponent* Static = NewObject<UStaticMeshComponent>(Owner, NAME_None, RF_Transient);
        Static->SetStaticMesh(StaticMesh);
        Staged = Static;
    }
    TStrongObjectPtr<UMeshComponent> StagedLifetime(Staged);
    Staged->SetMaterial(Resources.MaterialSlot, MID.Get());
    RegisterHidden(Staged, Owner, Transform);
    TStrongObjectPtr<USkeletalMeshComponent> StagedBody;
    if (BodyMesh.IsValid())
    {
        StagedBody.Reset(NewObject<USkeletalMeshComponent>(Owner, NAME_None, RF_Transient));
        ConfigureSkeletal(StagedBody.Get(), BodyMesh.Get());
        StagedBody->ClearMorphTargets();
        for (int32 Index = 0; Index < UE_ARRAY_COUNT(BodyMorphs); ++Index)
        {
            StagedBody->SetMorphTarget(BodyMorphs[Index], BodyWeights[Index], false);
        }
        StagedBody->SetMaterial(0, BodyMID.Get());
        RegisterHidden(StagedBody.Get(), Owner, FTransform::Identity);
        if (Staged->IsRegistered()) { RefreshSkeletal(CastChecked<USkeletalMeshComponent>(Staged)); }
        if (StagedBody->IsRegistered()) { RefreshSkeletal(StagedBody.Get()); }
    }
    const bool bReady = Staged->IsRegistered() && Staged->GetMaterial(Resources.MaterialSlot) == MID.Get()
        && (!SkeletalMesh || (HasPreparedPose(CastChecked<USkeletalMeshComponent>(Staged), PartMorphs, PartWeights)
            && StagedBody->IsRegistered() && StagedBody->GetMaterial(0) == BodyMID.Get()
            && HasPreparedPose(StagedBody.Get(), BodyMorphs, BodyWeights)));
    if (!bReady)
    {
        DestroyPreview(Staged);
        DestroyPreview(StagedBody.Get());
        return ECustomizationFailure::PreparationFailed;
    }

    // 두 숨긴 후보의 pose와 render weight 검증 후에만 공개한다.
    // Model은 이 성공 결과 뒤에 Profile/Draft를 공개한다.
    UMeshComponent* Previous = DisplayedMesh;
    USkeletalMeshComponent* PreviousBody = DisplayedBody;
    DisplayedMesh = Staged;
    DisplayedMID = MID.Get();
    DisplayedBody = StagedBody.Get();
    DisplayedBodyMID = BodyMID.Get();
    Staged->SetHiddenInGame(false);
    Staged->SetVisibility(true);
    if (DisplayedBody)
    {
        DisplayedBody->SetHiddenInGame(false);
        DisplayedBody->SetVisibility(true);
    }
    DestroyPreview(Previous);
    DestroyPreview(PreviousBody);
    return ECustomizationFailure::None;
}

void UCustomizationAppearanceComponent::ClearPreview()
{
    DestroyPreview(DisplayedMesh);
    DestroyPreview(DisplayedBody);
    DisplayedMesh = nullptr;
    DisplayedMID = nullptr;
    DisplayedBody = nullptr;
    DisplayedBodyMID = nullptr;
}

void UCustomizationAppearanceComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    ClearPreview();
    Super::EndPlay(EndPlayReason);
}
