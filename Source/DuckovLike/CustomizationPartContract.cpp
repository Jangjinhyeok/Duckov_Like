#include "CustomizationPartContract.h"

#include "Animation/MorphTarget.h"
#include "Engine/Texture2D.h"
#include "Materials/Material.h"

namespace
{
    const FName BodyColorParameter(TEXT("Color"));
    const FName BodyMorphs[] = {TEXT("EyeSmall"), TEXT("EyeLarge"), TEXT("BeakShort"),
        TEXT("BeakLong"), TEXT("BodyShort"), TEXT("BodyLong")};

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

    bool HasSkeletalUsage(const UMaterialInterface* Material, bool bMorphRequired)
    {
        const UMaterial* Base = Material ? Material->GetMaterial() : nullptr;
        // runtime에서는 공유 material의 usage flag나 dirty 상태를 바꾸지 않는다.
        return Base && Base->MaterialDomain == MD_Surface && (Base->bUsedAsSpecialEngineMaterial
            || (Base->GetUsageByFlag(MATUSAGE_SkeletalMesh)
                && (!bMorphRequired || Base->GetUsageByFlag(MATUSAGE_MorphTargets))));
    }
}

TConstArrayView<FName> GetCustomizationBodyMorphs() { return BodyMorphs; }

const FSoftObjectPath& GetDefaultCustomizationBodyMesh()
{
    static const FSoftObjectPath Path(TEXT("/Game/Customization/Prototype/SK_CustomizationBody.SK_CustomizationBody"));
    return Path;
}

FName GetCustomizationBodyColorParameter() { return BodyColorParameter; }

ECustomizationFailure ValidateCustomizationPartContract(
    const FCustomizationPartResources& Resources, ECustomizationSlot Slot, FCustomizationLoadedPart& Out)
{
    Out = FCustomizationLoadedPart();
    Out.Mesh.Reset(Resources.Mesh.TryLoad());
    Out.StaticMesh = Cast<UStaticMesh>(Out.Mesh.Get());
    Out.SkeletalMesh = Cast<USkeletalMesh>(Out.Mesh.Get());
    if ((Resources.MeshKind == ECustomizationMeshKind::StaticPlaceholder && !Out.StaticMesh)
        || (Resources.MeshKind == ECustomizationMeshKind::Skeletal && !Out.SkeletalMesh)
        || (!Out.StaticMesh && !Out.SkeletalMesh))
    {
        return ECustomizationFailure::MissingMesh;
    }
    for (const FCustomizationMorphFollow& Follow : Resources.FollowBodyMorphs)
    {
        if (!TConstArrayView<FName>(BodyMorphs).Contains(Follow.BodyMorph)) { return ECustomizationFailure::MissingMorph; }
        if (Follow.Offset.ContainsNaN()) { return ECustomizationFailure::InvalidNumber; }
    }
    // 형상 Morph가 없는 파츠는 Morph를 요구하지 않는다.
    const FName PartMorphs[] = {Resources.Morph};
    const TConstArrayView<FName> RequiredMorphs = Resources.Morph.IsNone() ? TConstArrayView<FName>() : TConstArrayView<FName>(PartMorphs);
    if (Out.SkeletalMesh)
    {
        Out.Skeleton.Reset(Cast<USkeleton>(Resources.ExpectedSkeleton.TryLoad()));
        if (!Out.Skeleton.IsValid()) { return ECustomizationFailure::MissingSkeleton; }
        if (Out.SkeletalMesh->GetSkeleton() != Out.Skeleton.Get()) { return ECustomizationFailure::SkeletonMismatch; }
        if (Out.SkeletalMesh->IsCompiling()) { return ECustomizationFailure::PreparationFailed; }
        if (!HasMorphData(Out.SkeletalMesh, RequiredMorphs)) { return ECustomizationFailure::MissingMorph; }
    }
    const int32 SlotCount = Out.StaticMesh ? Out.StaticMesh->GetStaticMaterials().Num() : Out.SkeletalMesh->GetMaterials().Num();
    if (Resources.MaterialSlot < 0 || Resources.MaterialSlot >= SlotCount) { return ECustomizationFailure::MissingMaterial; }
    Out.Material.Reset(Cast<UMaterialInterface>(Resources.Material.TryLoad()));
    if (!Out.Material.IsValid()) { return ECustomizationFailure::MissingMaterial; }
    const bool bBodyShares = Slot == ECustomizationSlot::Crest && Out.SkeletalMesh;
    if (Out.SkeletalMesh && !HasSkeletalUsage(Out.Material.Get(), bBodyShares || !RequiredMorphs.IsEmpty()))
    {
        return ECustomizationFailure::MissingMaterial;
    }
    FLinearColor ExistingColor;
    if (!Resources.ColorParameter.IsNone()
        && !Out.Material->GetVectorParameterValue(FMaterialParameterInfo(Resources.ColorParameter), ExistingColor))
    {
        return ECustomizationFailure::MissingColorParameter;
    }
    if (bBodyShares && !Out.Material->GetVectorParameterValue(FMaterialParameterInfo(BodyColorParameter), ExistingColor))
    {
        return ECustomizationFailure::MissingColorParameter;
    }
    if (Resources.bMaskRequired && Resources.Mask.IsNull()) { return ECustomizationFailure::MissingMask; }
    if (!Resources.Mask.IsNull() || !Resources.MaskParameter.IsNone() || Resources.bMaskRequired)
    {
        UTexture* ExistingMask = nullptr;
        if (Resources.MaskParameter.IsNone()
            || !Out.Material->GetTextureParameterValue(FMaterialParameterInfo(Resources.MaskParameter), ExistingMask))
        {
            return ECustomizationFailure::MissingMaskParameter;
        }
        // 선택 mask 없음은 흰색 neutral texture이다. 이전 파츠의 mask를 재사용하지 않는다.
        const FSoftObjectPath MaskPath = Resources.Mask.IsNull()
            ? FSoftObjectPath(TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture")) : Resources.Mask;
        Out.Mask.Reset(Cast<UTexture>(MaskPath.TryLoad()));
        if (!Cast<UTexture2D>(Out.Mask.Get())) { return ECustomizationFailure::MissingMask; }
    }
    return ECustomizationFailure::None;
}

ECustomizationFailure ValidateCustomizationBodyContract(
    const FSoftObjectPath& BodyMesh, const USkeleton* ExpectedSkeleton, FCustomizationLoadedBody& Out)
{
    Out.Mesh.Reset(Cast<USkeletalMesh>(BodyMesh.TryLoad()));
    if (!Out.Mesh.IsValid()) { return ECustomizationFailure::MissingMesh; }
    if (Out.Mesh->GetSkeleton() != ExpectedSkeleton) { return ECustomizationFailure::SkeletonMismatch; }
    if (Out.Mesh->IsCompiling()) { return ECustomizationFailure::PreparationFailed; }
    if (!HasMorphData(Out.Mesh.Get(), BodyMorphs)) { return ECustomizationFailure::MissingMorph; }
    if (Out.Mesh->GetMaterials().Num() == 0) { return ECustomizationFailure::MissingMaterial; }
    for (int32 Slot = 1; Slot < Out.Mesh->GetMaterials().Num(); ++Slot)
    {
        if (!HasSkeletalUsage(Out.Mesh->GetMaterials()[Slot].MaterialInterface, true)) { return ECustomizationFailure::MissingMaterial; }
    }
    return ECustomizationFailure::None;
}

TArray<FCustomizationCatalogIssue> ValidateCustomizationCatalogRows(
    TConstArrayView<TPair<FName, const FCustomizationPartRow*>> Rows)
{
    TArray<FCustomizationCatalogIssue> Issues;
    const auto AddIssue = [&Issues](FName PartId, const FCustomizationFailureDetail& Detail)
    {
        Issues.Add({PartId, Detail});
    };
    TSet<FName> Seen;
    TMap<FName, const FCustomizationPartRow*> ById;
    for (const TPair<FName, const FCustomizationPartRow*>& Entry : Rows)
    {
        const FName PartId = Entry.Key;
        const FCustomizationPartRow* Row = Entry.Value;
        if (!Row) { AddIssue(PartId, FCustomizationFailureDetail::Of(ECustomizationFailure::UnknownPart)); continue; }
        // None은 "파츠 없음"을 뜻하므로 ID로 쓸 수 없다. 같은 ID는 FName 비교(대소문자 무시)로 겹친다.
        if (PartId.IsNone() || Seen.Contains(PartId))
        {
            AddIssue(PartId, FCustomizationFailureDetail::Of(ECustomizationFailure::DuplicatePartId));
            continue;
        }
        Seen.Add(PartId);
        ById.Add(PartId, Row);
        const ECustomizationSlot Slot = Row->Slot;
        const bool bKnownSlot = GetCustomizationSlots().Contains(Slot);
        if (!bKnownSlot) { AddIssue(PartId, FCustomizationFailureDetail::Of(ECustomizationFailure::InvalidKind)); continue; }
        // 정식 카탈로그는 skeletal 파츠만 둔다. static 대역은 자동 검사 전용이다.
        if (Row->Resources.MeshKind != ECustomizationMeshKind::Skeletal)
        {
            AddIssue(PartId, FCustomizationFailureDetail::Of(ECustomizationFailure::InvalidKind, Slot));
            continue;
        }
        FCustomizationLoadedPart Part;
        ECustomizationFailure Failure = ValidateCustomizationPartContract(Row->Resources, Slot, Part);
        if (Failure == ECustomizationFailure::None)
        {
            FCustomizationLoadedBody Body;
            Failure = ValidateCustomizationBodyContract(GetDefaultCustomizationBodyMesh(), Part.Skeleton.Get(), Body);
        }
        if (Failure != ECustomizationFailure::None) { AddIssue(PartId, FCustomizationFailureDetail::Of(Failure, Slot)); }
    }
    // 기본 Profile(초기화 대상)이 가리키는 파츠가 같은 슬롯에 있어야 한다.
    const FCustomizationProfile Defaults;
    for (const ECustomizationSlot Slot : GetCustomizationSlots())
    {
        const FName DefaultId = Defaults.GetSlotPartId(Slot);
        if (DefaultId.IsNone()) { continue; }
        const FCustomizationPartRow* const* Found = ById.Find(DefaultId);
        if (!Found) { AddIssue(DefaultId, FCustomizationFailureDetail::Of(ECustomizationFailure::UnknownPart, Slot)); }
        else if ((*Found)->Slot != Slot) { AddIssue(DefaultId, FCustomizationFailureDetail::Of(ECustomizationFailure::SlotMismatch, Slot)); }
    }
    return Issues;
}

TArray<FCustomizationCatalogIssue> ValidateCustomizationTable(const UDataTable& Table)
{
    TArray<TPair<FName, const FCustomizationPartRow*>> Rows;
    if (Table.GetRowStruct() != FCustomizationPartRow::StaticStruct())
    {
        return {{NAME_None, FCustomizationFailureDetail::Of(ECustomizationFailure::CatalogUnavailable)}};
    }
    for (const FName RowName : Table.GetRowNames())
    {
        Rows.Emplace(RowName, Table.FindRow<FCustomizationPartRow>(RowName, TEXT("CustomizationCatalog"), false));
    }
    return ValidateCustomizationCatalogRows(Rows);
}
