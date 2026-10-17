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
    USkeletalMesh* Body = Mesh(TEXT("SK_CustomizationBaseBody"));
    if (!TestNotNull(TEXT("실제 import 고정 몸통"), Body)) { return false; }
    // 고정 몸통과 카탈로그의 모든 부위 mesh다.
    TArray<USkeletalMesh*> Parts;
    for (const TCHAR* Name : {TEXT("SK_CustomizationPartA"), TEXT("SK_CustomizationPartB"), TEXT("SK_CustomizationPartC"),
        TEXT("SK_CustomizationEyeRound"), TEXT("SK_CustomizationEyeLarge"), TEXT("SK_CustomizationEyeSleepy"),
        TEXT("SK_CustomizationBeakDuck"), TEXT("SK_CustomizationBeakSmall"), TEXT("SK_CustomizationBeakLong"),
        TEXT("SK_CustomizationWingRound"), TEXT("SK_CustomizationWingSmall"), TEXT("SK_CustomizationWingMitten"),
        TEXT("SK_CustomizationTailShort"), TEXT("SK_CustomizationTailFan"),
        TEXT("SK_CustomizationFootWebbed"), TEXT("SK_CustomizationFootBoot")})
    {
        USkeletalMesh* Item = Mesh(Name);
        if (TestNotNull(*FString::Printf(TEXT("실제 import %s"), Name), Item)) { Parts.Add(Item); }
    }
    FSkinnedAssetCompilingManager::Get().FinishAllCompilation();
    TestEqual(TEXT("body 슬롯은 피부색 하나"), Body->GetMaterials().Num(), 1);
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
    // 몸통+머리만 남았다. 발밑(z 0)부터 발이 아니라 몸통 아래(z 11)부터 머리 꼭대기(z 91)까지다.
    TestTrue(TEXT("고정 몸통 높이 80 cm"), FMath::IsNearlyEqual(Body->GetImportedBounds().BoxExtent.Z * 2.f, 80.f, 0.05f));
    const FName Expected[] = {TEXT("root"), TEXT("body"), TEXT("head"), TEXT("wing_l"), TEXT("wing_r")};
    for (USkeletalMesh* Item : Parts)
    {
        TestTrue(TEXT("동일 Skeleton asset"), Item->GetSkeleton() && Item->GetSkeleton() == Body->GetSkeleton());
        TestEqual(TEXT("부위 슬롯 수"), Item->GetMaterials().Num(), 1);
    }
    Parts.Add(Body);
    for (USkeletalMesh* Item : Parts)
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
    UMaterialInterface* Material = Body->GetMaterials()[0].MaterialInterface;
    TestTrue(TEXT("헤어·몸통 슬롯 0의 공통 material"), Material && Material == Mesh(TEXT("SK_CustomizationPartA"))->GetMaterials()[0].MaterialInterface);
    FLinearColor Color;
    UTexture* Mask = nullptr;
    TestTrue(TEXT("Color vector parameter"), Material && Material->GetVectorParameterValue(FMaterialParameterInfo(TEXT("Color")), Color));
    TestTrue(TEXT("선택 mask의 흰색 기본 참조"), Material && Material->GetTextureParameterValue(FMaterialParameterInfo(TEXT("ColorMask")), Mask) && Mask);
    TestTrue(TEXT("skeletal/Morph usage"), Material && Material->GetMaterial()->GetUsageByFlag(MATUSAGE_SkeletalMesh) && Material->GetMaterial()->GetUsageByFlag(MATUSAGE_MorphTargets));
    for (const TCHAR* Name : {TEXT("SK_CustomizationFootWebbed"), TEXT("SK_CustomizationFootBoot")})
    {
        UMaterialInterface* Fixed = Mesh(Name)->GetMaterials()[0].MaterialInterface;
        TestTrue(TEXT("발은 색상 parameter가 없는 고정색 material"), Fixed && Fixed != Material && Fixed->GetMaterial()
            && Fixed->GetMaterial()->GetUsageByFlag(MATUSAGE_SkeletalMesh));
    }
    TestEqual(TEXT("body Morph는 머리 모양 하나"), Body->GetMorphTargets().Num(), 1);
    const auto* BodyRender = Body->GetResourceForRendering();
    int32 Index = INDEX_NONE;
    const UMorphTarget* Morph = Body->FindMorphTargetAndIndex(TEXT("HeadFlat"), Index);
    float MaxDown = 0.f;
    bool bHasMorph = Morph != nullptr;
    if (Morph)
    {
        for (const FMorphTargetDelta& Delta : Morph->GetMorphTargetDeltas(0))
        {
            MaxDown = FMath::Min(MaxDown, Delta.PositionDelta.Z);
        }
    }
    // 머리 꼭대기(z 91)가 0.4 × 21 cm 내려간다. author_parts.py의 HEAD_FLATTEN과 같은 값이다.
    TestTrue(TEXT("머리 모양 Morph 실제 LOD0 정점 변화"), bHasMorph && Morph->HasDataForLOD(0) && FMath::IsNearlyEqual(MaxDown, -8.4f, 0.05f));
    TestTrue(TEXT("머리 모양 Morph 실제 GPU render batches"), BodyRender && BodyRender->LODRenderData.IsValidIndex(0)
        && BodyRender->LODRenderData[0].MorphTargetVertexInfoBuffers.IsMorphResourcesInitialized() && Index >= 0
        && static_cast<uint32>(Index) < BodyRender->LODRenderData[0].MorphTargetVertexInfoBuffers.GetNumMorphs()
        && BodyRender->LODRenderData[0].MorphTargetVertexInfoBuffers.GetNumBatches(Index) > 0);
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
    for (const TCHAR* Name : {TEXT("SK_CustomizationPartA")})
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

}
#endif
