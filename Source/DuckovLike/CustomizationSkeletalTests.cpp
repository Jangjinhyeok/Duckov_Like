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
    const auto CheckBody = [&](const USkeletalMeshComponent* Component, const FCustomizationProfile& Profile)
    {
        const FName Names[] = {TEXT("EyeSmall"), TEXT("EyeLarge"), TEXT("BeakShort"),
            TEXT("BeakLong"), TEXT("BodyShort"), TEXT("BodyLong")};
        const float Weights[] = {FMath::Max(-Profile.EyeSize, 0.f), FMath::Max(Profile.EyeSize, 0.f),
            FMath::Max(-Profile.BeakLength, 0.f), FMath::Max(Profile.BeakLength, 0.f),
            FMath::Max(-Profile.BodyLength, 0.f), FMath::Max(Profile.BodyLength, 0.f)};
        if (!TestNotNull(TEXT("실제 부위별 body"), Component)) { return; }
        TestTrue(TEXT("body transform scale 대체 없음"), Component->GetRelativeTransform().Equals(FTransform::Identity));
        TestEqual(TEXT("body Morph curve 여섯 개 명시"), Component->GetMorphTargetCurves().Num(), 6);
        int32 Active = 0;
        for (int32 Index = 0; Index < UE_ARRAY_COUNT(Names); ++Index)
        {
            int32 MorphIndex = INDEX_NONE;
            const auto* Morph = Component->GetSkeletalMeshAsset()->FindMorphTargetAndIndex(Names[Index], MorphIndex);
            if (!TestTrue(TEXT("실제 body Morph와 LOD0 data"), Morph && Morph->HasDataForLOD(0)
                && Component->MorphTargetWeights.IsValidIndex(MorphIndex))) { continue; }
            TestEqual(TEXT("부위별 curve weight"), Component->GetMorphTarget(Names[Index]), Weights[Index]);
            TestEqual(TEXT("실제 평가된 body render weight"), Component->MorphTargetWeights[MorphIndex], Weights[Index]);
            TestEqual(TEXT("반대 방향의 active Morph 잔류 없음"), Component->ActiveMorphTargets.Contains(Morph), Weights[Index] > UE_SMALL_NUMBER);
            if (Weights[Index] > UE_SMALL_NUMBER) { ++Active; }
        }
        TestEqual(TEXT("body active Morph 수"), Component->ActiveMorphTargets.Num(), Active);
    };
    CheckBody(Target->GetDisplayedBody(), FCustomizationProfile());
    FCustomizationProfile Good; Good.Hue = 0.72f; Good.Shape = 0.6f;
    Good.EyeSize = 0.4f; Good.BeakLength = -0.3f; Good.BodyLength = 0.5f;
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
    CheckBody(Body, Good);
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
    Target->SetBodyMeshForTests(FSoftObjectPath(TEXT("/Game/Customization/Prototype/SK_CustomizationPartA.SK_CustomizationPartA")));
    auto NextBody = Good; NextBody.EyeSize = -0.8f;
    TestEqual(TEXT("body 필수 Morph 누락 후보 실패"), Model->TryEdit(NextBody, Present), ECustomizationFailure::MissingMorph);
    TestEqual(TEXT("body Morph 누락 최종 적용 실패"), Model->Apply(Present), ECustomizationFailure::MissingMorph);
    TestEqual(TEXT("body Morph 누락 초기화 실패"), Model->Reset(Present), ECustomizationFailure::MissingMorph);
    TestEqual(TEXT("body Morph 누락 취소 실패"), Model->Cancel(Present), ECustomizationFailure::MissingMorph);
    TestTrue(TEXT("body Morph 실패의 확정/Draft/등록/전체 MID 보존"), Model->GetProfile() == FCustomizationProfile()
        && Model->GetDraft() == Good && Model->IsEditing() && Target->GetDisplayedBody() == Body
        && Target->GetDisplayedMesh() == Part && Target->GetDisplayedBodyMID() == BodyMID
        && Target->GetDisplayedMID() == PartMID && Body->IsRegistered() && Part->IsRegistered());
    TestEqual(TEXT("body Morph 실패 통지 없음"), Events, 0);
    CheckBody(Body, Good);
    Target->SetBodyMeshForTests(FSoftObjectPath(TEXT("/Game/Customization/Prototype/SK_CustomizationBody.SK_CustomizationBody")));
    TestEqual(TEXT("동일 표현 재적용"), Model->TryEdit(Good, Present), ECustomizationFailure::None);
    TestTrue(TEXT("동일 body/part/MID pointer"), Target->GetDisplayedMesh() == Part && Target->GetDisplayedBody() == Body
        && Target->GetDisplayedMID() == PartMID && Target->GetDisplayedBodyMID() == BodyMID);
    TestEqual(TEXT("동일 표현 이벤트 없음"), Events, 0);
    auto B = Good; B.PartId = TEXT("B");
    TestEqual(TEXT("실패 후 정상 B 재시도"), Model->TryEdit(B, Present), ECustomizationFailure::None);
    TestEqual(TEXT("A 복귀"), Model->TryEdit(Good, Present), ECustomizationFailure::None);
    Part = CastChecked<USkeletalMeshComponent>(Target->GetDisplayedMesh());
    TestTrue(TEXT("A/B/A Morph 누적 없음"), Part->GetMorphTargetCurves().Num() == 1 && FMath::IsNearlyEqual(Part->MorphTargetWeights[0], 0.8f));
    CheckBody(Target->GetDisplayedBody(), Good);
    TestEqual(TEXT("성공 변경만 이벤트"), Events, 2);
    TestEqual(TEXT("확정 적용"), Model->Apply(Present), ECustomizationFailure::None);
    TestTrue(TEXT("확정 값 일치"), Model->GetProfile() == Good && !Model->IsEditing());
    TestEqual(TEXT("부위 확정 후 재개방"), Model->BeginEdit(Present), ECustomizationFailure::None);
    TestTrue(TEXT("재개방 확정 부위 값 복원"), Model->GetDraft() == Good);
    for (float Value : {-1.f, 0.f, 1.f})
    {
        auto Candidate = Good; Candidate.EyeSize = Value; Candidate.BeakLength = Value; Candidate.BodyLength = Value;
        TestEqual(TEXT("부위 양쪽 경계와 중립 편집"), Model->TryEdit(Candidate, Present), ECustomizationFailure::None);
        CheckBody(Target->GetDisplayedBody(), Candidate);
        TestTrue(TEXT("부위 편집은 확정 값 보존"), Model->GetProfile() == Good);
        const int32 BeforeEvents = Events;
        auto* BeforeBody = Target->GetDisplayedBody();
        TestEqual(TEXT("부위 동일 값 재적용"), Model->TryEdit(Candidate, Present), ECustomizationFailure::None);
        TestTrue(TEXT("부위 동일 값 body 재생성/통지 없음"), Target->GetDisplayedBody() == BeforeBody && Events == BeforeEvents);
    }
    TestEqual(TEXT("실제 초기화 성공"), Model->Reset(Present), ECustomizationFailure::None);
    CheckBody(Target->GetDisplayedBody(), FCustomizationProfile());
    TestTrue(TEXT("실제 초기화 확정 값 보존"), Model->GetProfile() == Good && Model->GetDraft() == FCustomizationProfile());
    TestEqual(TEXT("실제 취소 확정 외형 복원"), Model->Cancel(Present), ECustomizationFailure::None);
    CheckBody(Target->GetDisplayedBody(), Good);
    FCustomizationProfile Second; Second.Hue = 0.2f;
    TestEqual(TEXT("두 번째 실제 대상"), Other->TryApply(Second), ECustomizationFailure::None);
    auto* OtherPartMID = Other->GetDisplayedMID(); auto* OtherBodyMID = Other->GetDisplayedBodyMID();
    auto* OtherBody = Other->GetDisplayedBody();
    UMaterialInterface* Shared = OtherPartMID->Parent;
    FLinearColor BeforeShared; Shared->GetVectorParameterValue(FMaterialParameterInfo(TEXT("Color")), BeforeShared);
    Good.Hue = 0.95f; Good.EyeSize = -0.7f; Good.BeakLength = 0.8f; Good.BodyLength = -0.6f;
    TestEqual(TEXT("한 대상의 색상/부위 재편집"), Target->TryApply(Good), ECustomizationFailure::None);
    FLinearColor PartColor, BodyColor, AfterShared;
    TestTrue(TEXT("다른 대상의 body/part 색상 독립"), Other->GetDisplayedMID() == OtherPartMID && Other->GetDisplayedBodyMID() == OtherBodyMID
        && OtherPartMID->GetVectorParameterValue(FMaterialParameterInfo(TEXT("Color")), PartColor) && PartColor == GetCustomizationColor(Second.Hue)
        && OtherBodyMID->GetVectorParameterValue(FMaterialParameterInfo(TEXT("Color")), BodyColor) && BodyColor == PartColor);
    TestTrue(TEXT("공유 material 값 보존"), Shared->GetVectorParameterValue(FMaterialParameterInfo(TEXT("Color")), AfterShared) && BeforeShared == AfterShared);
    TestTrue(TEXT("다른 대상의 body component 독립"), Other->GetDisplayedBody() == OtherBody);
    CheckBody(OtherBody, Second);
    CollectGarbage(RF_NoFlags);
    TestTrue(TEXT("GC 후 실제 body/part/MID 보존"), IsValid(Target->GetDisplayedBody()) && IsValid(Target->GetDisplayedMesh())
        && IsValid(Target->GetDisplayedMID()) && IsValid(Target->GetDisplayedBodyMID()));
    Target->ClearPreview();
    TestTrue(TEXT("body까지 정리"), !Target->GetDisplayedBody() && !Target->GetDisplayedBodyMID() && !Target->GetDisplayedMesh() && !Target->GetDisplayedMID());
    World->DestroyWorld(false); if (World->IsRooted()) { World->RemoveFromRoot(); }
    return true;
}
#endif
