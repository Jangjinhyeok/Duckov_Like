#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Animation/MorphTarget.h"
#include "Animation/Skeleton.h"
#include "EditorFramework/AssetImportData.h"
#include "EditorReimportHandler.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/Material.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "SkinnedAssetCompiler.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace CustomizationEditorAssetsTests
{
constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
USkeletalMesh* Mesh(const TCHAR* Name)
{
    return LoadObject<USkeletalMesh>(nullptr, *(FString(TEXT("/Game/Customization/Prototype/")) + Name + TEXT(".") + Name));
}
float MaxMorphZ(const USkeletalMesh* Mesh)
{
    const UMorphTarget* Morph = Mesh ? Mesh->FindMorphTarget(TEXT("Shape")) : nullptr;
    float Result = 0.f;
    if (Morph)
    {
        for (const FMorphTargetDelta& Delta : Morph->GetMorphTargetDeltas(0))
        {
            Result = FMath::Max(Result, Delta.PositionDelta.Z);
        }
    }
    return Result;
}
bool SaveAsset(UObject* Asset)
{
    UPackage* Package = Asset->GetOutermost();
    Package->MarkPackageDirty();
    const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
    FSavePackageArgs Args;
    Args.TopLevelFlags = RF_Public | RF_Standalone;
    return UPackage::SavePackage(Package, Asset, *Filename, Args);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCustomizationImportedAssets, "CustomizationAssets.Verify", Flags)
bool FCustomizationImportedAssets::RunTest(const FString& Parameters)
{
    USkeletalMesh* Body = Mesh(TEXT("SK_CustomizationBody"));
    USkeletalMesh* A = Mesh(TEXT("SK_CustomizationPartA"));
    USkeletalMesh* B = Mesh(TEXT("SK_CustomizationPartB"));
    if (!TestNotNull(TEXT("실제 import body"), Body) || !TestNotNull(TEXT("실제 import A"), A)
        || !TestNotNull(TEXT("실제 import B"), B)) { return false; }
    FSkinnedAssetCompilingManager::Get().FinishAllCompilation();
    TestEqual(TEXT("body 슬롯 수"), Body->GetMaterials().Num(), 3);
    TestEqual(TEXT("A 슬롯 수"), A->GetMaterials().Num(), 1);
    TestEqual(TEXT("B 슬롯 수"), B->GetMaterials().Num(), 1);
    TestTrue(TEXT("동일 Skeleton asset"), Body->GetSkeleton() && Body->GetSkeleton() == A->GetSkeleton() && A->GetSkeleton() == B->GetSkeleton());
    if (Body->GetSkeleton())
    {
        const FReferenceSkeleton& BodyBones = Body->GetRefSkeleton();
        const FReferenceSkeleton& SharedBones = Body->GetSkeleton()->GetReferenceSkeleton();
        TestEqual(TEXT("body와 보존된 Skeleton의 bone 수 일치"), BodyBones.GetNum(), SharedBones.GetNum());
        for (int32 Index = 0; Index < FMath::Min(BodyBones.GetNum(), SharedBones.GetNum()); ++Index)
        {
            TestEqual(TEXT("body와 Skeleton bone 이름 일치"), BodyBones.GetBoneName(Index), SharedBones.GetBoneName(Index));
            TestEqual(TEXT("body와 Skeleton bone parent 일치"), BodyBones.GetParentIndex(Index), SharedBones.GetParentIndex(Index));
            TestTrue(TEXT("body와 Skeleton 기준 자세 일치"), BodyBones.GetRefBonePose()[Index].Equals(SharedBones.GetRefBonePose()[Index], 0.0001f));
        }
    }
    TestTrue(TEXT("body 높이 91 cm"), FMath::IsNearlyEqual(Body->GetImportedBounds().BoxExtent.Z * 2.f, 91.f, 0.05f));
    const FName Expected[] = {TEXT("root"), TEXT("body"), TEXT("head"), TEXT("wing_l"), TEXT("wing_r")};
    for (USkeletalMesh* Item : {Body, A, B})
    {
        TestEqual(TEXT("공통 bone 5개"), Item->GetRefSkeleton().GetNum(), 5);
        for (int32 Index = 0; Index < 5 && Index < Item->GetRefSkeleton().GetNum(); ++Index)
        {
            TestEqual(TEXT("공통 bone 이름"), Item->GetRefSkeleton().GetBoneName(Index), Expected[Index]);
        }
        const auto* RenderData = Item->GetResourceForRendering();
        TestTrue(TEXT("실제 LOD0 vertex"), RenderData && RenderData->LODRenderData.Num() == 1 && RenderData->LODRenderData[0].GetNumVertices() > 0);
        TestTrue(TEXT("충돌 에셋 없음"), Item->GetPhysicsAsset() == nullptr);
    }
    UMaterialInterface* Material = A->GetMaterials()[0].MaterialInterface;
    TestTrue(TEXT("슬롯 0의 공통 material"), Material && Material == B->GetMaterials()[0].MaterialInterface && Material == Body->GetMaterials()[0].MaterialInterface);
    FLinearColor Color;
    UTexture* Mask = nullptr;
    TestTrue(TEXT("Color vector parameter"), Material && Material->GetVectorParameterValue(FMaterialParameterInfo(TEXT("Color")), Color));
    TestTrue(TEXT("선택 mask의 흰색 기본 참조"), Material && Material->GetTextureParameterValue(FMaterialParameterInfo(TEXT("ColorMask")), Mask) && Mask);
    TestTrue(TEXT("skeletal/Morph usage"), Material && Material->GetMaterial()->GetUsageByFlag(MATUSAGE_SkeletalMesh) && Material->GetMaterial()->GetUsageByFlag(MATUSAGE_MorphTargets));
    for (int32 Slot = 1; Slot < Body->GetMaterials().Num(); ++Slot)
    {
        UMaterialInterface* Auxiliary = Body->GetMaterials()[Slot].MaterialInterface;
        TestTrue(TEXT("눈/입 material도 skeletal/Morph usage 지원"), Auxiliary && Auxiliary->GetMaterial()
            && Auxiliary->GetMaterial()->GetUsageByFlag(MATUSAGE_SkeletalMesh)
            && Auxiliary->GetMaterial()->GetUsageByFlag(MATUSAGE_MorphTargets));
    }
    const FName RequiredBodyMorphs[] = {TEXT("EyeSmall"), TEXT("EyeLarge"), TEXT("BeakShort"),
        TEXT("BeakLong"), TEXT("BodyShort"), TEXT("BodyLong")};
    TestEqual(TEXT("body 필수 Morph 여섯 개"), Body->GetMorphTargets().Num(), 6);
    const auto* BodyRender = Body->GetResourceForRendering();
    for (FName Name : RequiredBodyMorphs)
    {
        int32 Index = INDEX_NONE;
        const UMorphTarget* Morph = Body->FindMorphTargetAndIndex(Name, Index);
        TestTrue(TEXT("body Morph 실제 LOD0 정점 변화"), Morph && Morph->HasDataForLOD(0)
            && Morph->GetMorphTargetDeltas(0).ContainsByPredicate([](const FMorphTargetDelta& Delta)
                { return !Delta.PositionDelta.IsNearlyZero(); }));
        TestTrue(TEXT("body Morph 실제 GPU render batches"), BodyRender && BodyRender->LODRenderData.IsValidIndex(0)
            && BodyRender->LODRenderData[0].MorphTargetVertexInfoBuffers.IsMorphResourcesInitialized() && Index >= 0
            && static_cast<uint32>(Index) < BodyRender->LODRenderData[0].MorphTargetVertexInfoBuffers.GetNumMorphs()
            && BodyRender->LODRenderData[0].MorphTargetVertexInfoBuffers.GetNumBatches(Index) > 0);
    }
    TestTrue(TEXT("A 실제 Morph 정점 변화"), A->FindMorphTarget(TEXT("Shape")) && MaxMorphZ(A) > 13.f);
    TestTrue(TEXT("B 실제 Morph 정점 변화"), B->FindMorphTarget(TEXT("Shape")) && MaxMorphZ(B) > 13.f);
    return true;
}

// 생성 명령과 분리한다. 전체 Duckov 회귀가 매번 에셋을 쓰지 않는다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCustomizationSameAssetReimport, "CustomizationAssets.Reimport", Flags)
bool FCustomizationSameAssetReimport::RunTest(const FString& Parameters)
{
    USkeletalMesh* A = Mesh(TEXT("SK_CustomizationPartA"));
    if (!TestNotNull(TEXT("재import 대상"), A)) { return false; }
    FSkinnedAssetCompilingManager::Get().FinishAllCompilation();
    const float Before = MaxMorphZ(A);
    USkeleton* Skeleton = A->GetSkeleton();
    UMaterialInterface* Material = A->GetMaterials()[0].MaterialInterface;
    const FString Edited = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation/CustomizationAssets/EditProbeExports/SK_CustomizationPartA.fbx"));
    const FString Original = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Art/CustomizationPrototype/Exports/SK_CustomizationPartA.fbx"));
    if (!TestTrue(TEXT("편집한 실제 FBX 존재"), FPaths::FileExists(Edited))) { return false; }
    FReimportManager::Instance()->UpdateReimportPaths(A, {Edited});
    const bool Changed = FReimportManager::Instance()->Reimport(A, false, false, Edited, nullptr, INDEX_NONE, false, true);
    FSkinnedAssetCompilingManager::Get().FinishAllCompilation();
    const float After = MaxMorphZ(A);
    TestTrue(TEXT("같은 asset reimport 성공"), Changed && Mesh(TEXT("SK_CustomizationPartA")) == A);
    TestTrue(TEXT("source Shape 정점 +2 cm 반영"), FMath::IsNearlyEqual(After - Before, 2.f, 0.02f));
    TestTrue(TEXT("Skeleton/material 참조 보존"), A->GetSkeleton() == Skeleton && A->GetMaterials()[0].MaterialInterface == Material);
    // 시험 변경을 원본으로 재import하고 metadata도 현재 project의 source로 정규화한다.
    FReimportManager::Instance()->UpdateReimportPaths(A, {Original});
    TestTrue(TEXT("원본으로 같은 asset 재import"), FReimportManager::Instance()->Reimport(A, false, false, Original, nullptr, INDEX_NONE, false, true));
    FSkinnedAssetCompilingManager::Get().FinishAllCompilation();
    TestTrue(TEXT("원본 Morph 복원"), FMath::IsNearlyEqual(MaxMorphZ(A), Before, 0.02f));
    for (const TCHAR* Name : {TEXT("SK_CustomizationBody"), TEXT("SK_CustomizationPartA"), TEXT("SK_CustomizationPartB")})
    {
        USkeletalMesh* Item = Mesh(Name);
        const FString Source = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Art/CustomizationPrototype/Exports") / (FString(Name) + TEXT(".fbx")));
        Item->GetAssetImportData()->Update(Source);
        TestTrue(TEXT("최종 package 저장"), SaveAsset(Item));
    }
    const FString Record = FString::Printf(TEXT("{\"same_asset\":true,\"before_z\":%.6f,\"edited_z\":%.6f,\"restored_z\":%.6f}"), Before, After, MaxMorphZ(A));
    FFileHelper::SaveStringToFile(Record, *(FPaths::ProjectSavedDir() / TEXT("Automation/CustomizationSkeletal/reimport.json")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCustomizationBodyRegionsReimport, "CustomizationAssets.ReimportBodyRegions", Flags)
bool FCustomizationBodyRegionsReimport::RunTest(const FString& Parameters)
{
    USkeletalMesh* Body = Mesh(TEXT("SK_CustomizationBody"));
    if (!TestNotNull(TEXT("부위별 재import 대상 body"), Body)) { return false; }
    FSkinnedAssetCompilingManager::Get().FinishAllCompilation();
    if (!TestEqual(TEXT("재import 전 body 슬롯 3개"), Body->GetMaterials().Num(), 3)) { return false; }
    const auto MaxEyeMorphMagnitude = [](const USkeletalMesh* Item)
    {
        float Result = 0.f;
        const UMorphTarget* Morph = Item->FindMorphTarget(TEXT("EyeLarge"));
        if (Morph)
        {
            for (const FMorphTargetDelta& Delta : Morph->GetMorphTargetDeltas(0))
            {
                Result = FMath::Max(Result, Delta.PositionDelta.Size());
            }
        }
        return Result;
    };
    const float Before = MaxEyeMorphMagnitude(Body);
    USkeleton* Skeleton = Body->GetSkeleton();
    UMaterialInterface* Materials[] = {Body->GetMaterials()[0].MaterialInterface,
        Body->GetMaterials()[1].MaterialInterface, Body->GetMaterials()[2].MaterialInterface};
    const FString Edited = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()
        / TEXT("Automation/CustomizationRegions/EditProbeExports/SK_CustomizationBody.fbx"));
    const FString Original = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()
        / TEXT("Art/CustomizationPrototype/Exports/SK_CustomizationBody.fbx"));
    if (!TestTrue(TEXT("편집 body FBX와 복원 원본 존재"), FPaths::FileExists(Edited) && FPaths::FileExists(Original))) { return false; }
    const auto CheckReferences = [&]
    {
        TestTrue(TEXT("동일 body UObject/Skeleton 보존"), Mesh(TEXT("SK_CustomizationBody")) == Body && Body->GetSkeleton() == Skeleton);
        TestEqual(TEXT("body Morph 여섯 개 보존"), Body->GetMorphTargets().Num(), 6);
        TestEqual(TEXT("body material 슬롯 3개 보존"), Body->GetMaterials().Num(), 3);
        for (int32 Slot = 0; Slot < 3 && Slot < Body->GetMaterials().Num(); ++Slot)
        {
            TestTrue(TEXT("body 각 material 참조 보존"), Body->GetMaterials()[Slot].MaterialInterface == Materials[Slot]);
        }
    };
    FReimportManager::Instance()->UpdateReimportPaths(Body, {Edited});
    const bool Changed = FReimportManager::Instance()->Reimport(Body, false, false, Edited, nullptr, INDEX_NONE, false, true);
    FSkinnedAssetCompilingManager::Get().FinishAllCompilation();
    const float After = MaxEyeMorphMagnitude(Body);
    TestTrue(TEXT("편집 body 같은 asset 재import 성공"), Changed);
    TestTrue(TEXT("EyeLarge source delta 길이 +2 cm 반영"), FMath::IsNearlyEqual(After - Before, 2.f, 0.02f));
    CheckReferences();
    // 편집 import나 검사 실패 후에도 production 원본 복원은 수행한다.
    FReimportManager::Instance()->UpdateReimportPaths(Body, {Original});
    const bool Restored = FReimportManager::Instance()->Reimport(Body, false, false, Original, nullptr, INDEX_NONE, false, true);
    FSkinnedAssetCompilingManager::Get().FinishAllCompilation();
    const float Final = MaxEyeMorphMagnitude(Body);
    TestTrue(TEXT("production body 원본 재import 복원"), Restored);
    TestTrue(TEXT("EyeLarge 원본 delta 길이 복원"), FMath::IsNearlyEqual(Final, Before, 0.02f));
    CheckReferences();
    Body->GetAssetImportData()->Update(Original);
    const bool Saved = Restored && SaveAsset(Body);
    TestTrue(TEXT("최종 body package만 저장"), Saved);
    const FString Record = FString::Printf(TEXT("{\"same_asset\":%s,\"edited_reimport\":%s,\"restored_reimport\":%s,\"saved\":%s,\"before_magnitude\":%.6f,\"edited_magnitude\":%.6f,\"restored_magnitude\":%.6f}"),
        Mesh(TEXT("SK_CustomizationBody")) == Body ? TEXT("true") : TEXT("false"), Changed ? TEXT("true") : TEXT("false"),
        Restored ? TEXT("true") : TEXT("false"), Saved ? TEXT("true") : TEXT("false"), Before, After, Final);
    TestTrue(TEXT("body reimport 증거 저장"), FFileHelper::SaveStringToFile(Record,
        *(FPaths::ProjectSavedDir() / TEXT("Automation/CustomizationRegions/reimport-body.json"))));
    return true;
}
}
#endif
