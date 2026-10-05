#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "CustomizationAppearanceComponent.h"
#include "CustomizationModel.h"
#include "Animation/MorphTarget.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "SkinnedAssetCompiler.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCustomizationSkeletalAtomicity,
    "Duckov.Customization.Skeletal.ActualAssetsAtomicityAndColor", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCustomizationSkeletalAtomicity::RunTest(const FString& Parameters)
{
    for (const TCHAR* Name : {TEXT("SK_CustomizationBody"), TEXT("SK_CustomizationPartA"), TEXT("SK_CustomizationPartB")})
    {
        LoadObject<USkeletalMesh>(nullptr, *(FString(TEXT("/Game/Customization/Prototype/")) + Name + TEXT(".") + Name));
    }
    FSkinnedAssetCompilingManager::Get().FinishAllCompilation();
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    const auto CreateTarget = [World]
    {
        AActor* Owner = World->SpawnActor<AActor>();
        auto* Root = NewObject<USceneComponent>(Owner);
        Owner->SetRootComponent(Root);
        Root->RegisterComponent();
        auto* Result = NewObject<UCustomizationAppearanceComponent>(Owner);
        Result->RegisterComponent();
        return Result;
    };
    auto* Target = CreateTarget();
    auto* Other = CreateTarget();
    TStrongObjectPtr<UCustomizationModel> Model{NewObject<UCustomizationModel>()};
    const auto Present = [Target](const FCustomizationProfile& Value) { return Target->TryApply(Value); };
    const bool Started = TestEqual(TEXT("실제 skeletal body/part 준비"), Model->BeginEdit(Present), ECustomizationFailure::None);
    if (!Started)
    {
        World->DestroyWorld(false); if (World->IsRooted()) { World->RemoveFromRoot(); }
        return false;
    }
    FCustomizationProfile Good; Good.Hue = 0.72f; Good.Shape = 0.6f;
    TestEqual(TEXT("실제 정상 편집"), Model->TryEdit(Good, Present), ECustomizationFailure::None);
    TestTrue(TEXT("편집 확정 값 보존"), Model->GetProfile() == FCustomizationProfile() && Model->GetDraft() == Good);
    auto* Part = Cast<USkeletalMeshComponent>(Target->GetDisplayedMesh());
    auto* Body = Target->GetDisplayedBody();
    auto* PartMID = Target->GetDisplayedMID();
    auto* BodyMID = Target->GetDisplayedBodyMID();
    TestTrue(TEXT("등록된 실제 body/part·같은 Skeleton"), Part && Body && Part->IsRegistered() && Body->IsRegistered()
        && Part->GetSkeletalMeshAsset()->GetSkeleton() == Body->GetSkeletalMeshAsset()->GetSkeleton());
    TestTrue(TEXT("상시 skeletal Tick 없음"), !Part->IsComponentTickEnabled() && !Body->IsComponentTickEnabled());
    TestTrue(TEXT("skeletal scale 대체 없음"), Part->GetRelativeScale3D() == FVector::OneVector);
    TestTrue(TEXT("실제 평가된 Morph weight"), Part->MorphTargetWeights.Num() == 1 && FMath::IsNearlyEqual(Part->MorphTargetWeights[0], 0.8f));
    int32 Events = 0;
    Model->OnChanged().AddLambda([&Events] { ++Events; });
    const auto CheckFailure = [&](const FCustomizationPartResources& Resources, ECustomizationFailure Expected)
    {
        Target->SetResourcesForTests(TEXT("B"), Resources);
        auto Candidate = Good; Candidate.PartId = TEXT("B");
        TestEqual(TEXT("실제 리소스 실패 사유"), Model->TryEdit(Candidate, Present), Expected);
        TestTrue(TEXT("실패는 Profile/Draft 보존"), Model->GetProfile() == FCustomizationProfile() && Model->GetDraft() == Good);
        TestTrue(TEXT("실패는 body/part/MID/등록 보존"), Target->GetDisplayedMesh() == Part && Target->GetDisplayedBody() == Body
            && Target->GetDisplayedMID() == PartMID && Target->GetDisplayedBodyMID() == BodyMID && Part->IsRegistered() && Body->IsRegistered());
        TestEqual(TEXT("실패 이벤트 없음"), Events, 0);
    };
    auto Bad = Target->GetDefaultResources(TEXT("B")); Bad.Mesh.Reset(); CheckFailure(Bad, ECustomizationFailure::MissingMesh);
    Bad = Target->GetDefaultResources(TEXT("B")); Bad.ExpectedSkeleton = FSoftObjectPath(TEXT("/Game/Customization/Tests/SKEL_Unexpected.SKEL_Unexpected")); CheckFailure(Bad, ECustomizationFailure::SkeletonMismatch);
    Bad = Target->GetDefaultResources(TEXT("B")); Bad.Mesh = FSoftObjectPath(TEXT("/Game/Customization/Prototype/SK_CustomizationBody.SK_CustomizationBody")); CheckFailure(Bad, ECustomizationFailure::MissingMorph);
    Bad = Target->GetDefaultResources(TEXT("B")); Bad.Material.Reset(); CheckFailure(Bad, ECustomizationFailure::MissingMaterial);
    Bad = Target->GetDefaultResources(TEXT("B")); Bad.ColorParameter = TEXT("AbsentColor"); CheckFailure(Bad, ECustomizationFailure::MissingColorParameter);
    Bad = Target->GetDefaultResources(TEXT("B")); Bad.bMaskRequired = true; CheckFailure(Bad, ECustomizationFailure::MissingMask);
    Bad = Target->GetDefaultResources(TEXT("B")); Bad.MaskParameter = TEXT("ColorMask"); Bad.Mask = Bad.Mesh; CheckFailure(Bad, ECustomizationFailure::MissingMask);
    Bad = Target->GetDefaultResources(TEXT("B")); Bad.MaskParameter = TEXT("ColorMask"); Bad.Mask = FSoftObjectPath(TEXT("/Engine/EngineResources/DefaultTextureCube.DefaultTextureCube")); CheckFailure(Bad, ECustomizationFailure::MissingMask);
    Target->SetResourcesForTests(TEXT("B"), Target->GetDefaultResources(TEXT("B")));
    TestEqual(TEXT("동일 표현 재적용"), Model->TryEdit(Good, Present), ECustomizationFailure::None);
    TestTrue(TEXT("동일 body/part/MID pointer"), Target->GetDisplayedMesh() == Part && Target->GetDisplayedBody() == Body
        && Target->GetDisplayedMID() == PartMID && Target->GetDisplayedBodyMID() == BodyMID);
    TestEqual(TEXT("동일 표현 이벤트 없음"), Events, 0);
    auto B = Good; B.PartId = TEXT("B");
    TestEqual(TEXT("실패 후 정상 B 재시도"), Model->TryEdit(B, Present), ECustomizationFailure::None);
    TestEqual(TEXT("A 복귀"), Model->TryEdit(Good, Present), ECustomizationFailure::None);
    Part = CastChecked<USkeletalMeshComponent>(Target->GetDisplayedMesh());
    TestTrue(TEXT("A/B/A Morph 누적 없음"), Part->GetMorphTargetCurves().Num() == 1 && FMath::IsNearlyEqual(Part->MorphTargetWeights[0], 0.8f));
    TestEqual(TEXT("성공 변경만 이벤트"), Events, 2);
    TestEqual(TEXT("확정 적용"), Model->Apply(Present), ECustomizationFailure::None);
    TestTrue(TEXT("확정 값 일치"), Model->GetProfile() == Good && !Model->IsEditing());
    FCustomizationProfile Second; Second.Hue = 0.2f;
    TestEqual(TEXT("두 번째 실제 대상"), Other->TryApply(Second), ECustomizationFailure::None);
    auto* OtherPartMID = Other->GetDisplayedMID(); auto* OtherBodyMID = Other->GetDisplayedBodyMID();
    UMaterialInterface* Shared = OtherPartMID->Parent;
    FLinearColor BeforeShared; Shared->GetVectorParameterValue(FMaterialParameterInfo(TEXT("Color")), BeforeShared);
    Good.Hue = 0.95f; Target->TryApply(Good);
    FLinearColor PartColor, BodyColor, AfterShared;
    TestTrue(TEXT("다른 대상의 body/part 색상 독립"), Other->GetDisplayedMID() == OtherPartMID && Other->GetDisplayedBodyMID() == OtherBodyMID
        && OtherPartMID->GetVectorParameterValue(FMaterialParameterInfo(TEXT("Color")), PartColor) && PartColor == GetCustomizationColor(Second.Hue)
        && OtherBodyMID->GetVectorParameterValue(FMaterialParameterInfo(TEXT("Color")), BodyColor) && BodyColor == PartColor);
    TestTrue(TEXT("공유 material 값 보존"), Shared->GetVectorParameterValue(FMaterialParameterInfo(TEXT("Color")), AfterShared) && BeforeShared == AfterShared);
    CollectGarbage(RF_NoFlags);
    TestTrue(TEXT("GC 후 실제 body/part/MID 보존"), IsValid(Target->GetDisplayedBody()) && IsValid(Target->GetDisplayedMesh())
        && IsValid(Target->GetDisplayedMID()) && IsValid(Target->GetDisplayedBodyMID()));
    Target->ClearPreview();
    TestTrue(TEXT("body까지 정리"), !Target->GetDisplayedBody() && !Target->GetDisplayedBodyMID() && !Target->GetDisplayedMesh() && !Target->GetDisplayedMID());
    World->DestroyWorld(false); if (World->IsRooted()) { World->RemoveFromRoot(); }
    return true;
}
#endif
