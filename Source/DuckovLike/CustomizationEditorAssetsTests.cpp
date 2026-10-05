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
}
using namespace CustomizationEditorAssetsTests;

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
#endif
