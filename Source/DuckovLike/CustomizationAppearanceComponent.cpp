#include "CustomizationAppearanceComponent.h"

#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimationAsset.h"
#include "Animation/MorphTarget.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "CustomizationPartContract.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/Material.h"
#include "Rendering/SkeletalMeshRenderData.h"

namespace
{
    bool HasSkeletalRenderData(const USkeletalMesh* Mesh)
    {
        if (!Mesh || Mesh->IsCompiling() || Mesh->GetRefSkeleton().GetNum() == 0) { return false; }
        const FSkeletalMeshRenderData* RenderData = Mesh->GetResourceForRendering();
        return RenderData && RenderData->IsInitialized() && RenderData->LODRenderData.IsValidIndex(0)
            && RenderData->LODRenderData[0].GetNumVertices() > 0;
    }

    // ColorParameter가 None인 고정색 파츠는 override 없는 MID를 기대한다.
    bool MatchesMaterial(const UMaterialInstanceDynamic* MID, const UMaterialInterface* Parent,
        FName ColorParameter, const FLinearColor& Color, FName MaskParameter = NAME_None, UTexture* Mask = nullptr)
    {
        FLinearColor CurrentColor;
        UTexture* CurrentMask = nullptr;
        const bool bTinted = !ColorParameter.IsNone();
        return MID && MID->Parent == Parent && MID->VectorParameterValues.Num() == (bTinted ? 1 : 0)
            && (!bTinted || (MID->VectorParameterValues[0].ParameterInfo.Name == ColorParameter
                && MID->GetVectorParameterValue(FMaterialParameterInfo(ColorParameter), CurrentColor) && CurrentColor == Color))
            && MID->TextureParameterValues.Num() == (Mask ? 1 : 0)
            && (!Mask || (MID->TextureParameterValues[0].ParameterInfo.Name == MaskParameter
                && MID->GetTextureParameterValue(FMaterialParameterInfo(MaskParameter), CurrentMask) && CurrentMask == Mask));
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
        if (!Mesh) { return false; }
        // Morph를 쓰지 않는 파츠는 weight 배열이 비어 있을 수 있다(헤어 mesh에 남은 Shape Morph 등). 값은 아래에서 모두 0인지 본다.
        const int32 WeightCount = Component->MorphTargetWeights.Num();
        if (Component->GetComponentSpaceTransforms().Num() != Mesh->GetRefSkeleton().GetNum()
            || (WeightCount != Mesh->GetMorphTargets().Num() && !(Morphs.IsEmpty() && WeightCount == 0))
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
        for (int32 Index = 0; Index < WeightCount; ++Index)
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

    // 애니메이션이 없으면 tick 없이 reference pose만 평가한다. 있으면 그 component만 tick하며 재생한다.
    void ConfigureSkeletal(USkeletalMeshComponent* Component, USkeletalMesh* Mesh, UAnimationAsset* Animation = nullptr)
    {
        const bool bAnimated = Animation != nullptr;
        Component->PrimaryComponentTick.bCanEverTick = bAnimated;
        Component->PrimaryComponentTick.bStartWithTickEnabled = bAnimated;
        Component->bEnableUpdateRateOptimizations = false;
        Component->SetDisablePostProcessBlueprint(true);
        Component->SetForceRefPose(!bAnimated);
        Component->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
        Component->SetSkeletalMesh(Mesh);
        // C3 시험 에셋의 LOD0만 사용한다.
        Component->SetForcedLOD(1);
        if (bAnimated)
        {
            // 등록할 때 single node instance가 만들어진다. 재생할 asset은 등록 뒤 PlayAnimation으로 준다.
            Component->SetAnimationMode(EAnimationMode::AnimationSingleNode);
        }
    }

    // body가 기대한 재생 상태인지다. 애니메이션이 없으면 tick 없는 reference pose 고정이다.
    // tick 등록 자체는 owner의 BeginPlay를 따르므로(game world) 여기서는 tick 가능 설정만 본다.
    bool HasBodyAnimation(const USkeletalMeshComponent* Component, const UAnimationAsset* Animation)
    {
        const bool bCanTick = Component->PrimaryComponentTick.bCanEverTick;
        if (!Animation) { return Component->bForceRefpose && !bCanTick && !Component->GetAnimInstance(); }
        const UAnimSingleNodeInstance* Instance = Component->GetSingleNodeInstance();
        return !Component->bForceRefpose && bCanTick && Instance && Instance->GetAnimationAsset() == Animation && Instance->IsPlaying();
    }

    // 애니메이션 body를 따르는 파츠는 자기 pose 배열이 없다(leader가 계산한다). 따르는 body는 공개할 때 다시 맞춘다.
    bool HasPreparedPart(const USkeletalMeshComponent* Part, bool bFollowsBody)
    {
        return bFollowsBody ? Part->LeaderPoseComponent.IsValid() : !Part->LeaderPoseComponent.IsValid() && HasPreparedPose(Part, {}, {});
    }

    void RegisterHidden(UMeshComponent* Component, USceneComponent* Parent, const FTransform& Transform)
    {
        Component->SetVisibility(false);
        Component->SetHiddenInGame(true);
        Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Component->SetGenerateOverlapEvents(false);
        Component->SetMobility(EComponentMobility::Movable);
        Component->SetupAttachment(Parent);
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

    void Show(UMeshComponent* Component)
    {
        Component->SetHiddenInGame(false);
        Component->SetVisibility(true);
    }

    // 한 슬롯의 후보다. 검증과 준비를 마친 뒤에만 표시에 반영한다.
    struct FSlotPlan
    {
        ECustomizationSlot Slot = ECustomizationSlot::Eye;
        int32 Index = 0;
        FCustomizationPartResources Resources;
        FCustomizationLoadedPart Loaded;
        FTransform Transform;
        // 팔레트 색을 받을 parameter다. 색이 없는 슬롯(발)은 고정색이라 None이다.
        FName ColorParameter;
        FLinearColor Color = FLinearColor::White;
        TStrongObjectPtr<UMaterialInstanceDynamic> MID;
        TStrongObjectPtr<UMeshComponent> Staged;
        // 이미 같은 외형이 표시 중이라 그대로 둔다.
        bool bReuse = false;
        // 같은 static mesh라 component를 그대로 두고 material/transform만 바꾼다.
        bool bInPlace = false;
    };
}

UCustomizationAppearanceComponent::UCustomizationAppearanceComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

FSoftObjectPath UCustomizationAppearanceComponent::GetDefaultBodyMeshPath()
{
    return GetDefaultCustomizationBodyMesh();
}

FCustomizationPartResources UCustomizationAppearanceComponent::GetDefaultResources(FName PartId)
{
    const FCustomizationCatalog Catalog = FCustomizationCatalog::LoadDefault();
    const FCustomizationPartRow* Row = Catalog.Find(PartId);
    return Row ? Row->Resources : FCustomizationPartResources();
}

void UCustomizationAppearanceComponent::SetResourcesForTests(FName PartId,
    const FCustomizationPartResources& Resources)
{
    TestResources.Add(PartId, Resources);
}

ECustomizationFailure UCustomizationAppearanceComponent::TryApply(const FCustomizationProfile& Candidate)
{
    LastFailure = Prepare(Candidate);
    if (LastFailure.Reason == ECustomizationFailure::None)
    {
        AppliedProfile = Candidate;
        bHasApplied = true;
    }
    return LastFailure.Reason;
}

FCustomizationFailureDetail UCustomizationAppearanceComponent::Prepare(const FCustomizationProfile& Candidate)
{
    using FDetail = FCustomizationFailureDetail;
    if (bPreparing) { return FDetail::Of(ECustomizationFailure::OperationInProgress); }
    const FDetail Validation = ValidateCustomizationProfileDetail(Candidate);
    if (Validation.Reason != ECustomizationFailure::None) { return Validation; }
    AActor* Owner = GetOwner();
    if (!Owner || !Owner->GetWorld() || !Owner->GetRootComponent() || Owner->IsActorBeingDestroyed())
    {
        return FDetail::Of(ECustomizationFailure::PreparationFailed);
    }
    TGuardValue<bool> Guard(bPreparing, true);

    // 1) 슬롯별 후보와 리소스 계약. 표시 component에는 아직 손대지 않는다.
    const FCustomizationCatalog Catalog = FCustomizationCatalog::LoadDefault();
    TArray<FSlotPlan, TInlineAllocator<CustomizationSlotCount>> Plans;
    for (const ECustomizationSlot Slot : GetCustomizationSlots())
    {
        const FName PartId = Candidate.GetSlotPartId(Slot);
        if (PartId.IsNone()) { continue; }
        FSlotPlan& Plan = Plans.AddDefaulted_GetRef();
        Plan.Slot = Slot;
        Plan.Index = static_cast<int32>(Slot);
        if (const FCustomizationPartResources* Override = TestResources.Find(PartId)) { Plan.Resources = *Override; }
        else if (const FCustomizationPartRow* Row = Catalog.Find(PartId)) { Plan.Resources = Row->Resources; }
        else { return FDetail::Of(ECustomizationFailure::UnknownPart, Slot); }
        const ECustomizationMeshKind Kind = Plan.Resources.MeshKind;
        if (Kind != ECustomizationMeshKind::StaticPlaceholder && Kind != ECustomizationMeshKind::Skeletal)
        {
            return FDetail::Of(ECustomizationFailure::PreparationFailed, Slot);
        }
        const bool bColorable = IsCustomizationSlotColorable(Slot);
        Plan.ColorParameter = bColorable ? Plan.Resources.ColorParameter : NAME_None;
        Plan.Color = GetCustomizationColor(bColorable ? Candidate.GetSlotColor(Slot) : INDEX_NONE);
    }
    // 고정 몸통은 첫 skeletal 파츠의 Skeleton으로 검사하고, 나머지 skeletal 파츠도 같은 Skeleton이어야 한다.
    // static 대역만 있는 자동 검사 표시에는 body가 없다.
    FCustomizationLoadedBody Body;
    for (FSlotPlan& Plan : Plans)
    {
        ECustomizationFailure Failure = ValidateCustomizationPartContract(Plan.Resources, Plan.Slot, Plan.Loaded);
        if (Failure != ECustomizationFailure::None) { return FDetail::Of(Failure, Plan.Slot); }
        if (!Plan.Loaded.SkeletalMesh) { continue; }
        if (!Body.Mesh.IsValid())
        {
            // body 문제는 슬롯의 문제가 아니므로 슬롯 없이 돌려준다.
            Failure = ValidateCustomizationBodyContract(BodyMeshPath, Plan.Loaded.Skeleton.Get(), Body);
            if (Failure != ECustomizationFailure::None) { return FDetail::Of(Failure); }
        }
        else if (Plan.Loaded.Skeleton.Get() != Body.Mesh->GetSkeleton())
        {
            return FDetail::Of(ECustomizationFailure::SkeletonMismatch, Plan.Slot);
        }
    }
    if (!Body.Mesh.IsValid() && Candidate.HeadShape != 1.0f)
    {
        return FDetail::Of(ECustomizationFailure::UnsupportedBodyAdjustment);
    }
    // 다른 Skeleton의 애니메이션은 재생되지 않고 reference pose로 남는다. 성공으로 공개하지 않는다.
    if (BodyAnimation && Body.Mesh.IsValid() && BodyAnimation->GetSkeleton() != Body.Mesh->GetSkeleton())
    {
        return FDetail::Of(ECustomizationFailure::SkeletonMismatch);
    }

    // 2) 로드한 뒤의 런타임 준비 상태. render 자원과 Morph batch가 실제로 만들어졌는지 본다.
    const TConstArrayView<FName> BodyMorphs = GetCustomizationBodyMorphs();
    const auto CheckRender = [](USkeletalMesh* Mesh, TConstArrayView<FName> Morphs) -> ECustomizationFailure
    {
        if (!HasSkeletalRenderData(Mesh)) { return ECustomizationFailure::PreparationFailed; }
        if (Morphs.IsEmpty()) { return ECustomizationFailure::None; }
        if (!Mesh->GetResourceForRendering()->LODRenderData[0].MorphTargetVertexInfoBuffers.IsMorphResourcesInitialized())
        {
            return ECustomizationFailure::PreparationFailed;
        }
        return HasMorphBatches(Mesh, Morphs) ? ECustomizationFailure::None : ECustomizationFailure::MissingMorph;
    };
    for (FSlotPlan& Plan : Plans)
    {
        if (!Plan.Loaded.SkeletalMesh) { continue; }
        const ECustomizationFailure Failure = CheckRender(Plan.Loaded.SkeletalMesh, {});
        if (Failure != ECustomizationFailure::None) { return FDetail::Of(Failure, Plan.Slot); }
    }
    if (Body.Mesh.IsValid())
    {
        const ECustomizationFailure Failure = CheckRender(Body.Mesh.Get(), BodyMorphs);
        if (Failure != ECustomizationFailure::None) { return FDetail::Of(Failure); }
    }
    // 첫 draw의 fallback material을 성공 외형으로 공개하지 않는다. 동기 prototype 준비 경로다.
    const auto IsComplete = [](UMaterialInterface* Material)
    {
        Material->EnsureIsComplete();
        return Material->IsComplete() && Material->GetMaterial()->IsComplete();
    };
    for (FSlotPlan& Plan : Plans)
    {
        if (Plan.Loaded.SkeletalMesh && !IsComplete(Plan.Loaded.Material.Get()))
        {
            return FDetail::Of(ECustomizationFailure::PreparationFailed, Plan.Slot);
        }
    }
    if (Body.Mesh.IsValid() && !IsComplete(Body.Material.Get())) { return FDetail::Of(ECustomizationFailure::PreparationFailed); }

    // 3) 이미 같은 외형이면 아무것도 바꾸지 않는다.
    // 머리 모양 1(기본)이 HeadFlat weight 0이다.
    const float BodyWeights[] = {1.0f - Candidate.HeadShape};
    const FLinearColor SkinColor = GetCustomizationColor(Candidate.SkinColor);
    const FName BodyColorParameter = GetCustomizationBodyColorParameter();
    bool bAllReused = true;
    for (FSlotPlan& Plan : Plans)
    {
        const FCustomizationPartResources& Resources = Plan.Resources;
        // body Morph를 따라가는 파츠는 같은 weight로 위치를 옮긴다. 이 위치도 준비 후 한 번에 공개된다.
        FVector Location = FVector::ZeroVector;
        for (const FCustomizationMorphFollow& Follow : Resources.FollowBodyMorphs)
        {
            const int32 MorphIndex = BodyMorphs.Find(Follow.BodyMorph);
            if (MorphIndex != INDEX_NONE) { Location += Follow.Offset * BodyWeights[MorphIndex]; }
        }
        Plan.Transform = FTransform(FQuat::Identity, Location);
        UMeshComponent* Current = DisplayedMeshes[Plan.Index];
        UMaterialInstanceDynamic* CurrentMID = DisplayedMIDs[Plan.Index];
        const UStaticMeshComponent* CurrentStatic = Cast<UStaticMeshComponent>(Current);
        const USkeletalMeshComponent* CurrentSkeletal = Cast<USkeletalMeshComponent>(Current);
        const bool bSameMesh = Current && Current->IsRegistered() && CurrentMID
            && Current->GetMaterial(Resources.MaterialSlot) == CurrentMID
            && ((Plan.Loaded.StaticMesh && CurrentStatic && CurrentStatic->GetStaticMesh() == Plan.Loaded.StaticMesh)
                || (Plan.Loaded.SkeletalMesh && CurrentSkeletal && CurrentSkeletal->GetSkeletalMeshAsset() == Plan.Loaded.SkeletalMesh));
        Plan.bReuse = bSameMesh
            && MatchesMaterial(CurrentMID, Plan.Loaded.Material.Get(), Plan.ColorParameter, Plan.Color,
                Resources.MaskParameter, Plan.Loaded.Mask.Get())
            && Current->GetRelativeLocation() == Plan.Transform.GetLocation()
            && Current->GetRelativeScale3D() == FVector::OneVector
            && Current->GetRelativeTransform().GetRotation() == FQuat::Identity
            && (!CurrentSkeletal || HasPreparedPart(CurrentSkeletal, BodyAnimation != nullptr));
        Plan.bInPlace = !Plan.bReuse && bSameMesh && Plan.Loaded.StaticMesh;
        bAllReused &= Plan.bReuse;
    }
    const bool bSameBody = Body.Mesh.IsValid()
        ? DisplayedBody && DisplayedBody->IsRegistered() && DisplayedBody->GetSkeletalMeshAsset() == Body.Mesh.Get()
            && DisplayedBody->GetMaterial(0) == DisplayedBodyMID
            && MatchesMaterial(DisplayedBodyMID, Body.Material.Get(), BodyColorParameter, SkinColor)
            && DisplayedBody->GetRelativeTransform().Equals(FTransform::Identity)
            && HasPreparedPose(DisplayedBody, BodyMorphs, BodyWeights) && HasBodyAnimation(DisplayedBody, BodyAnimation)
        : !DisplayedBody && !DisplayedBodyMID;
    bool bRemovesSlot = false;
    for (int32 Index = 0; Index < CustomizationSlotCount; ++Index)
    {
        bRemovesSlot |= DisplayedMeshes[Index] && !Plans.ContainsByPredicate([Index](const FSlotPlan& Plan) { return Plan.Index == Index; });
    }
    if (bAllReused && bSameBody && !bRemovesSlot) { return FDetail(); }

    // 4) 바뀌는 파츠의 새 MID. 새 override는 이전 파츠의 색과 mask를 상속하지 않는다.
    for (FSlotPlan& Plan : Plans)
    {
        if (Plan.bReuse) { continue; }
        const FCustomizationPartResources& Resources = Plan.Resources;
        Plan.MID.Reset(UMaterialInstanceDynamic::Create(Plan.Loaded.Material.Get(), Owner));
        if (!Plan.MID.IsValid()) { return FDetail::Of(ECustomizationFailure::PreparationFailed, Plan.Slot); }
        if (!Plan.ColorParameter.IsNone()) { Plan.MID->SetVectorParameterValue(Plan.ColorParameter, Plan.Color); }
        if (Plan.Loaded.Mask.IsValid()) { Plan.MID->SetTextureParameterValue(Resources.MaskParameter, Plan.Loaded.Mask.Get()); }
        if (!MatchesMaterial(Plan.MID.Get(), Plan.Loaded.Material.Get(), Plan.ColorParameter, Plan.Color,
            Resources.MaskParameter, Plan.Loaded.Mask.Get()))
        {
            return FDetail::Of(ECustomizationFailure::PreparationFailed, Plan.Slot);
        }
    }
    TStrongObjectPtr<UMaterialInstanceDynamic> BodyMID;
    if (Body.Mesh.IsValid() && !bSameBody)
    {
        BodyMID.Reset(UMaterialInstanceDynamic::Create(Body.Material.Get(), Owner));
        if (!BodyMID.IsValid()) { return FDetail::Of(ECustomizationFailure::PreparationFailed); }
        BodyMID->SetVectorParameterValue(BodyColorParameter, SkinColor);
        if (!MatchesMaterial(BodyMID.Get(), Body.Material.Get(), BodyColorParameter, SkinColor))
        {
            return FDetail::Of(ECustomizationFailure::PreparationFailed);
        }
    }

    // 5) 바뀌는 파츠를 숨긴 채 등록하고 pose·Morph render weight까지 준비한다.
    USceneComponent* Parent = AttachParent ? AttachParent.Get() : Owner->GetRootComponent();
    for (FSlotPlan& Plan : Plans)
    {
        if (Plan.bReuse || Plan.bInPlace) { continue; }
        UMeshComponent* Staged = nullptr;
        if (Plan.Loaded.SkeletalMesh)
        {
            USkeletalMeshComponent* Skeletal = NewObject<USkeletalMeshComponent>(Owner, NAME_None, RF_Transient);
            ConfigureSkeletal(Skeletal, Plan.Loaded.SkeletalMesh);
            Skeletal->ClearMorphTargets();
            Staged = Skeletal;
        }
        else
        {
            UStaticMeshComponent* Static = NewObject<UStaticMeshComponent>(Owner, NAME_None, RF_Transient);
            Static->SetStaticMesh(Plan.Loaded.StaticMesh);
            Staged = Static;
        }
        Plan.Staged.Reset(Staged);
        Staged->SetMaterial(Plan.Resources.MaterialSlot, Plan.MID.Get());
        RegisterHidden(Staged, Parent, Plan.Transform);
    }
    TStrongObjectPtr<USkeletalMeshComponent> StagedBody;
    if (Body.Mesh.IsValid() && !bSameBody)
    {
        StagedBody.Reset(NewObject<USkeletalMeshComponent>(Owner, NAME_None, RF_Transient));
        ConfigureSkeletal(StagedBody.Get(), Body.Mesh.Get(), BodyAnimation);
        StagedBody->ClearMorphTargets();
        for (int32 Index = 0; Index < BodyMorphs.Num(); ++Index)
        {
            StagedBody->SetMorphTarget(BodyMorphs[Index], BodyWeights[Index], false);
        }
        StagedBody->SetMaterial(0, BodyMID.Get());
        RegisterHidden(StagedBody.Get(), Parent, FTransform::Identity);
        if (BodyAnimation && StagedBody->IsRegistered())
        {
            StagedBody->PlayAnimation(BodyAnimation, true);
            if (UAnimSingleNodeInstance* Instance = StagedBody->GetSingleNodeInstance())
            {
                Instance->SetBlendSpacePosition(FVector(BodyAnimationInput, 0.0f, 0.0f));
            }
        }
    }
    const auto DiscardStaged = [&Plans, &StagedBody]
    {
        for (FSlotPlan& Plan : Plans) { DestroyPreview(Plan.Staged.Get()); }
        DestroyPreview(StagedBody.Get());
    };
    for (FSlotPlan& Plan : Plans)
    {
        if (!Plan.Staged.IsValid()) { continue; }
        if (Plan.Staged->IsRegistered() && Plan.Loaded.SkeletalMesh) { RefreshSkeletal(CastChecked<USkeletalMeshComponent>(Plan.Staged.Get())); }
    }
    if (StagedBody.IsValid() && StagedBody->IsRegistered()) { RefreshSkeletal(StagedBody.Get()); }
    for (FSlotPlan& Plan : Plans)
    {
        if (!Plan.Staged.IsValid()) { continue; }
        const bool bReady = Plan.Staged->IsRegistered() && Plan.Staged->GetMaterial(Plan.Resources.MaterialSlot) == Plan.MID.Get()
            && (!Plan.Loaded.SkeletalMesh || HasPreparedPose(CastChecked<USkeletalMeshComponent>(Plan.Staged.Get()), {}, {}));
        if (!bReady)
        {
            DiscardStaged();
            return FDetail::Of(ECustomizationFailure::PreparationFailed, Plan.Slot);
        }
    }
    if (StagedBody.IsValid()
        && !(StagedBody->IsRegistered() && StagedBody->GetMaterial(0) == BodyMID.Get()
            && HasPreparedPose(StagedBody.Get(), BodyMorphs, BodyWeights) && HasBodyAnimation(StagedBody.Get(), BodyAnimation)))
    {
        DiscardStaged();
        return FDetail::Of(ECustomizationFailure::PreparationFailed);
    }

    // 6) 모든 슬롯의 후보 pose와 render weight 검증을 마친 뒤에만 한 번에 공개한다.
    //    Model은 이 성공 결과 뒤에 Profile/Draft를 공개한다.
    TArray<UMeshComponent*, TInlineAllocator<CustomizationSlotCount + 1>> Retired;
    for (int32 Index = 0; Index < CustomizationSlotCount; ++Index)
    {
        FSlotPlan* Plan = Plans.FindByPredicate([Index](const FSlotPlan& Item) { return Item.Index == Index; });
        if (Plan && Plan->bReuse) { continue; }
        if (Plan && Plan->bInPlace)
        {
            DisplayedMeshes[Index]->SetMaterial(Plan->Resources.MaterialSlot, Plan->MID.Get());
            DisplayedMeshes[Index]->SetRelativeTransform(Plan->Transform);
            DisplayedMIDs[Index] = Plan->MID.Get();
            continue;
        }
        if (DisplayedMeshes[Index]) { Retired.Add(DisplayedMeshes[Index]); }
        DisplayedMeshes[Index] = Plan ? Plan->Staged.Get() : nullptr;
        DisplayedMIDs[Index] = Plan ? Plan->MID.Get() : nullptr;
        if (Plan) { Show(Plan->Staged.Get()); }
    }
    if (!bSameBody)
    {
        if (DisplayedBody) { Retired.Add(DisplayedBody); }
        DisplayedBody = StagedBody.Get();
        DisplayedBodyMID = BodyMID.Get();
        if (DisplayedBody) { Show(DisplayedBody); }
    }
    // 재사용 파츠도 새 body를 따르게 한 뒤 이전 body를 없앤다. 같은 body면 SetLeaderPoseComponent는 아무것도 하지 않는다.
    if (BodyAnimation && DisplayedBody)
    {
        for (UMeshComponent* Mesh : DisplayedMeshes)
        {
            if (auto* Part = Cast<USkeletalMeshComponent>(Mesh)) { Part->SetLeaderPoseComponent(DisplayedBody); }
        }
    }
    for (UMeshComponent* Previous : Retired) { DestroyPreview(Previous); }
    return FDetail();
}

void UCustomizationAppearanceComponent::SetBodyAnimationInput(float Value)
{
    BodyAnimationInput = Value;
    if (UAnimSingleNodeInstance* Instance = BodyAnimation && DisplayedBody ? DisplayedBody->GetSingleNodeInstance() : nullptr)
    {
        Instance->SetBlendSpacePosition(FVector(Value, 0.0f, 0.0f));
    }
}

void UCustomizationAppearanceComponent::ClearPreview()
{
    for (int32 Index = 0; Index < CustomizationSlotCount; ++Index)
    {
        DestroyPreview(DisplayedMeshes[Index]);
        DisplayedMeshes[Index] = nullptr;
        DisplayedMIDs[Index] = nullptr;
    }
    DestroyPreview(DisplayedBody);
    DisplayedBody = nullptr;
    DisplayedBodyMID = nullptr;
    AppliedProfile = FCustomizationProfile();
    bHasApplied = false;
}

void UCustomizationAppearanceComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    ClearPreview();
    Super::EndPlay(EndPlayReason);
}
