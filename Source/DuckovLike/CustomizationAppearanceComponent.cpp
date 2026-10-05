#include "CustomizationAppearanceComponent.h"

#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/StrongObjectPtr.h"

UCustomizationAppearanceComponent::UCustomizationAppearanceComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    PartA = GetDefaultResources(TEXT("A"));
    PartB = GetDefaultResources(TEXT("B"));
}

FCustomizationPartResources UCustomizationAppearanceComponent::GetDefaultResources(FName PartId)
{
    FCustomizationPartResources Resources;
    Resources.Mesh = FSoftObjectPath(PartId == TEXT("B")
        ? TEXT("/Engine/BasicShapes/Sphere.Sphere") : TEXT("/Engine/BasicShapes/Cube.Cube"));
    Resources.Material = FSoftObjectPath(TEXT("/Engine/EngineDebugMaterials/M_SimpleOpaque.M_SimpleOpaque"));
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
    if (Resources.MeshKind == ECustomizationMeshKind::Skeletal)
    {
        Skeleton.Reset(Cast<USkeleton>(Resources.ExpectedSkeleton.TryLoad()));
        if (!Skeleton.IsValid()) { return ECustomizationFailure::MissingSkeleton; }
        if (SkeletalMesh->GetSkeleton() != Skeleton.Get()) { return ECustomizationFailure::SkeletonMismatch; }
        if (Resources.Morph.IsNone() || !SkeletalMesh->FindMorphTarget(Resources.Morph))
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
    FLinearColor ExistingColor;
    if (Resources.ColorParameter.IsNone()
        || !Material->GetVectorParameterValue(FMaterialParameterInfo(Resources.ColorParameter), ExistingColor))
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
        if (!Mask.IsValid()) { return ECustomizationFailure::MissingMask; }
    }

    const FVector Scale = StaticMesh ? FVector(1.0f, 1.0f, 1.0f + 0.35f * Candidate.Shape) : FVector::OneVector;
    const FTransform Transform(FQuat::Identity, FVector::ZeroVector, Scale);
    UStaticMeshComponent* CurrentStatic = Cast<UStaticMeshComponent>(DisplayedMesh);
    USkeletalMeshComponent* CurrentSkeletal = Cast<USkeletalMeshComponent>(DisplayedMesh);
    const bool bSameMesh = DisplayedMesh && DisplayedMesh->IsRegistered() && DisplayedMID
        && DisplayedMesh->GetMaterial(Resources.MaterialSlot) == DisplayedMID
        && ((StaticMesh && CurrentStatic && CurrentStatic->GetStaticMesh() == StaticMesh)
            || (SkeletalMesh && CurrentSkeletal && CurrentSkeletal->GetSkeletalMeshAsset() == SkeletalMesh));
    FLinearColor CurrentColor;
    UTexture* CurrentMask = nullptr;
    const FLinearColor Color = GetCustomizationColor(Candidate.Hue);
    const float MorphWeight = (Candidate.Shape + 1.0f) * 0.5f;
    if (bSameMesh && DisplayedMID->Parent == Material.Get()
        && DisplayedMID->VectorParameterValues.Num() == 1
        && DisplayedMID->VectorParameterValues[0].ParameterInfo.Name == Resources.ColorParameter
        && DisplayedMID->TextureParameterValues.Num() == (Mask.IsValid() ? 1 : 0)
        && DisplayedMID->GetVectorParameterValue(FMaterialParameterInfo(Resources.ColorParameter), CurrentColor)
        && CurrentColor == Color
        && DisplayedMesh->GetRelativeLocation() == FVector::ZeroVector
        && DisplayedMesh->GetRelativeScale3D() == Scale
        && DisplayedMesh->GetRelativeTransform().GetRotation() == FQuat::Identity
        && (!Mask.IsValid() || (DisplayedMID->GetTextureParameterValue(FMaterialParameterInfo(Resources.MaskParameter), CurrentMask)
            && CurrentMask == Mask.Get()))
        && (!CurrentSkeletal || (CurrentSkeletal->GetMorphTargetCurves().Num() == 1
            && CurrentSkeletal->GetMorphTargetCurves().Contains(Resources.Morph)
            && CurrentSkeletal->GetMorphTarget(Resources.Morph) == MorphWeight)))
    {
        return ECustomizationFailure::None;
    }
    // 같은 mesh의 입력은 새 MID를 먼저 준비한 뒤 실패 결과가 없는 setter만 공개한다.
    // 이전 MID의 mask/vector override를 복사하지 않는다.
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
    if (bSameMesh)
    {
        DisplayedMesh->SetMaterial(Resources.MaterialSlot, MID.Get());
        DisplayedMesh->SetRelativeTransform(Transform);
        if (CurrentSkeletal)
        {
            CurrentSkeletal->ClearMorphTargets();
            CurrentSkeletal->SetMorphTarget(Resources.Morph, MorphWeight, false);
        }
        DisplayedMID = MID.Get();
        return ECustomizationFailure::None;
    }

    UMeshComponent* Staged = nullptr;
    if (SkeletalMesh)
    {
        USkeletalMeshComponent* Skeletal = NewObject<USkeletalMeshComponent>(Owner, NAME_None, RF_Transient);
        Skeletal->SetSkeletalMesh(SkeletalMesh);
        Skeletal->SetMorphTarget(Resources.Morph, MorphWeight, false);
        Staged = Skeletal;
    }
    else
    {
        UStaticMeshComponent* Static = NewObject<UStaticMeshComponent>(Owner, NAME_None, RF_Transient);
        Static->SetStaticMesh(StaticMesh);
        Staged = Static;
    }
    TStrongObjectPtr<UMeshComponent> StagedLifetime(Staged);
    // 등록 전부터 숨긴다. 등록 후 실패해도 기존 mesh/MID를 보존한다.
    Staged->SetVisibility(false);
    Staged->SetHiddenInGame(true);
    Staged->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Staged->SetGenerateOverlapEvents(false);
    Staged->SetMobility(EComponentMobility::Movable);
    Staged->SetupAttachment(Owner->GetRootComponent());
    Staged->SetRelativeTransform(Transform);
    Staged->SetMaterial(Resources.MaterialSlot, MID.Get());
    Staged->RegisterComponent();
    const bool bReady = Staged->IsRegistered() && Staged->GetMaterial(Resources.MaterialSlot) == MID.Get()
        && (!SkeletalMesh || FMath::IsNearlyEqual(CastChecked<USkeletalMeshComponent>(Staged)->GetMorphTarget(Resources.Morph),
            MorphWeight));
    if (!bReady)
    {
        Staged->DestroyComponent();
        return ECustomizationFailure::PreparationFailed;
    }

    // 이후 호출은 실패 결과가 없는 공개/폐기뿐이다. Model은 이 성공 뒤에 값을 commit한다.
    UMeshComponent* Previous = DisplayedMesh;
    DisplayedMesh = Staged;
    DisplayedMID = MID.Get();
    Staged->SetHiddenInGame(false);
    Staged->SetVisibility(true);
    if (Previous)
    {
        Previous->SetVisibility(false);
        Previous->SetHiddenInGame(true);
        Previous->DestroyComponent();
    }
    return ECustomizationFailure::None;
}

void UCustomizationAppearanceComponent::ClearPreview()
{
    if (DisplayedMesh) { DisplayedMesh->DestroyComponent(); }
    DisplayedMesh = nullptr;
    DisplayedMID = nullptr;
}

void UCustomizationAppearanceComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    ClearPreview();
    Super::EndPlay(EndPlayReason);
}
