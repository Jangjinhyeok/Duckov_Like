#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "CustomizationAppearanceComponent.h"
#include "CustomizationModel.h"
#include "CustomizationTestSupport.h"
#include "Animation/MorphTarget.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCustomizationSkeletalAtomicity,
    "Duckov.Customization.Skeletal.ActualAssetsAtomicityAndColor", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCustomizationSkeletalAtomicity::RunTest(const FString& Parameters)
{
    CustomizationTestSupport::FRealWorld Scene;
    auto* Target = Scene.CreateTarget();
    auto* Other = Scene.CreateTarget();
    TStrongObjectPtr<UCustomizationModel> Model{NewObject<UCustomizationModel>()};
    const auto Present = [Target](const FCustomizationProfile& Value) { return Target->TryApply(Value); };
    if (!TestEqual(TEXT("실제 skeletal body/part 준비"), Model->BeginEdit(Present), ECustomizationFailure::None)) { return false; }
    const FName HeadMorph(TEXT("HeadFlat"));
    // 고정 몸통의 Morph는 머리 모양 하나다. weight는 1 - HeadShape이고 반대 방향 Morph가 없다.
    const auto CheckBody = [&](const USkeletalMeshComponent* Component, const FCustomizationProfile& Profile)
    {
        if (!TestNotNull(TEXT("실제 고정 몸통 body"), Component)) { return; }
        const float Weight = 1.f - Profile.HeadShape;
        TestTrue(TEXT("body transform scale 대체 없음"), Component->GetRelativeTransform().Equals(FTransform::Identity));
        TestEqual(TEXT("body Morph curve 하나 명시"), Component->GetMorphTargetCurves().Num(), 1);
        int32 MorphIndex = INDEX_NONE;
        const auto* Morph = Component->GetSkeletalMeshAsset()->FindMorphTargetAndIndex(HeadMorph, MorphIndex);
        if (!TestTrue(TEXT("실제 머리 모양 Morph와 LOD0 data"), Morph && Morph->HasDataForLOD(0)
            && Component->MorphTargetWeights.IsValidIndex(MorphIndex))) { return; }
        TestEqual(TEXT("머리 모양 curve weight"), Component->GetMorphTarget(HeadMorph), Weight);
        TestEqual(TEXT("실제 평가된 body render weight"), Component->MorphTargetWeights[MorphIndex], Weight);
        TestEqual(TEXT("active Morph는 납작할 때만"), Component->ActiveMorphTargets.Num(), Weight > UE_SMALL_NUMBER ? 1 : 0);
    };
    CheckBody(Target->GetDisplayedBody(), FCustomizationProfile());
    FCustomizationProfile Good; Good.SkinColor = 7; Good.HairColor = 9; Good.EyeColor = 1; Good.HeadShape = 0.4f;
    TestEqual(TEXT("실제 정상 편집"), Model->TryEdit(Good, Present), ECustomizationFailure::None);
    TestTrue(TEXT("편집 확정 값 보존"), Model->GetProfile() == FCustomizationProfile() && Model->GetDraft() == Good);
    auto* Part = Cast<USkeletalMeshComponent>(Target->GetDisplayedSlotMesh(ECustomizationSlot::Hair));
    auto* Body = Target->GetDisplayedBody();
    auto* PartMID = Target->GetDisplayedSlotMID(ECustomizationSlot::Hair);
    auto* BodyMID = Target->GetDisplayedBodyMID();
    TestTrue(TEXT("등록된 실제 body/헤어·같은 Skeleton"), Part && Body && Part->IsRegistered() && Body->IsRegistered()
        && Part->GetSkeletalMeshAsset()->GetSkeleton() == Body->GetSkeletalMeshAsset()->GetSkeleton());
    TestTrue(TEXT("상시 skeletal Tick 없음"), !Part->IsComponentTickEnabled() && !Body->IsComponentTickEnabled());
    TestTrue(TEXT("skeletal scale 대체 없음"), Part->GetRelativeScale3D() == FVector::OneVector);
    TestTrue(TEXT("헤어 mesh에 남은 Shape Morph는 쓰지 않는다"), Part->GetMorphTargetCurves().Num() == 0 && Part->ActiveMorphTargets.Num() == 0);
    CheckBody(Body, Good);
    int32 Events = 0;
    Model->OnChanged().AddLambda([&Events] { ++Events; });
    const auto CheckFailure = [&](const FCustomizationPartResources& Resources, ECustomizationFailure Expected)
    {
        Target->SetResourcesForTests(TEXT("HairB"), Resources);
        auto Candidate = Good; Candidate.HairId = TEXT("HairB");
        TestEqual(TEXT("실제 리소스 실패 사유"), Model->TryEdit(Candidate, Present), Expected);
        TestTrue(TEXT("실패는 Profile/Draft 보존"), Model->GetProfile() == FCustomizationProfile() && Model->GetDraft() == Good);
        TestTrue(TEXT("실패는 body/part/MID/등록 보존"), Target->GetDisplayedSlotMesh(ECustomizationSlot::Hair) == Part && Target->GetDisplayedBody() == Body
            && Target->GetDisplayedSlotMID(ECustomizationSlot::Hair) == PartMID && Target->GetDisplayedBodyMID() == BodyMID
            && Part->IsRegistered() && Body->IsRegistered());
        TestEqual(TEXT("실패 이벤트 없음"), Events, 0);
    };
    FCustomizationMorphFollow BadFollow; BadFollow.BodyMorph = TEXT("NoSuchMorph");
    auto Bad = Target->GetDefaultResources(TEXT("HairB")); Bad.Mesh.Reset(); CheckFailure(Bad, ECustomizationFailure::MissingMesh);
    Bad = Target->GetDefaultResources(TEXT("HairB")); Bad.ExpectedSkeleton = FSoftObjectPath(TEXT("/Game/Customization/Tests/SKEL_Unexpected.SKEL_Unexpected")); CheckFailure(Bad, ECustomizationFailure::SkeletonMismatch);
    Bad = Target->GetDefaultResources(TEXT("HairB")); Bad.FollowBodyMorphs = {BadFollow}; CheckFailure(Bad, ECustomizationFailure::MissingMorph);
    Bad = Target->GetDefaultResources(TEXT("HairB")); Bad.Material.Reset(); CheckFailure(Bad, ECustomizationFailure::MissingMaterial);
    Bad = Target->GetDefaultResources(TEXT("HairB")); Bad.ColorParameter = TEXT("AbsentColor"); CheckFailure(Bad, ECustomizationFailure::MissingColorParameter);
    Bad = Target->GetDefaultResources(TEXT("HairB")); Bad.bMaskRequired = true; CheckFailure(Bad, ECustomizationFailure::MissingMask);
    Bad = Target->GetDefaultResources(TEXT("HairB")); Bad.MaskParameter = TEXT("ColorMask"); Bad.Mask = Bad.Mesh; CheckFailure(Bad, ECustomizationFailure::MissingMask);
    Bad = Target->GetDefaultResources(TEXT("HairB")); Bad.MaskParameter = TEXT("ColorMask"); Bad.Mask = FSoftObjectPath(TEXT("/Engine/EngineResources/DefaultTextureCube.DefaultTextureCube")); CheckFailure(Bad, ECustomizationFailure::MissingMask);
    Target->SetResourcesForTests(TEXT("HairB"), Target->GetDefaultResources(TEXT("HairB")));
    // 머리 모양 Morph가 없는 mesh를 body로 쓰면 body 계약에서 실패한다.
    Target->SetBodyMeshForTests(FSoftObjectPath(TEXT("/Game/Customization/Prototype/SK_CustomizationPartA.SK_CustomizationPartA")));
    auto NextBody = Good; NextBody.HeadShape = 0.1f;
    TestEqual(TEXT("body 필수 Morph 누락 후보 실패"), Model->TryEdit(NextBody, Present), ECustomizationFailure::MissingMorph);
    TestEqual(TEXT("body Morph 누락 최종 적용 실패"), Model->Apply(Present), ECustomizationFailure::MissingMorph);
    TestEqual(TEXT("body Morph 누락 초기화 실패"), Model->Reset(Present), ECustomizationFailure::MissingMorph);
    TestEqual(TEXT("body Morph 누락 취소 실패"), Model->Cancel(Present), ECustomizationFailure::MissingMorph);
    TestTrue(TEXT("body Morph 실패의 확정/Draft/등록/전체 MID 보존"), Model->GetProfile() == FCustomizationProfile()
        && Model->GetDraft() == Good && Model->IsEditing() && Target->GetDisplayedBody() == Body
        && Target->GetDisplayedSlotMesh(ECustomizationSlot::Hair) == Part && Target->GetDisplayedBodyMID() == BodyMID
        && Target->GetDisplayedSlotMID(ECustomizationSlot::Hair) == PartMID && Body->IsRegistered() && Part->IsRegistered());
    TestEqual(TEXT("body Morph 실패 통지 없음"), Events, 0);
    CheckBody(Body, Good);
    Target->SetBodyMeshForTests(GetDefaultCustomizationBodyMesh());
    TestEqual(TEXT("동일 표현 재적용"), Model->TryEdit(Good, Present), ECustomizationFailure::None);
    TestTrue(TEXT("동일 body/part/MID pointer"), Target->GetDisplayedSlotMesh(ECustomizationSlot::Hair) == Part && Target->GetDisplayedBody() == Body
        && Target->GetDisplayedSlotMID(ECustomizationSlot::Hair) == PartMID && Target->GetDisplayedBodyMID() == BodyMID);
    TestEqual(TEXT("동일 표현 이벤트 없음"), Events, 0);
    auto B = Good; B.HairId = TEXT("HairB");
    TestEqual(TEXT("실패 후 정상 B 재시도"), Model->TryEdit(B, Present), ECustomizationFailure::None);
    TestEqual(TEXT("A 복귀"), Model->TryEdit(Good, Present), ECustomizationFailure::None);
    Part = CastChecked<USkeletalMeshComponent>(Target->GetDisplayedSlotMesh(ECustomizationSlot::Hair));
    FLinearColor PartColor;
    TestTrue(TEXT("A/B/A 색·Morph 잔류 없음"), Part->GetMorphTargetCurves().Num() == 0
        && Target->GetDisplayedSlotMID(ECustomizationSlot::Hair)->GetVectorParameterValue(FMaterialParameterInfo(TEXT("Color")), PartColor)
        && PartColor == GetCustomizationColor(Good.HairColor));
    CheckBody(Target->GetDisplayedBody(), Good);
    TestEqual(TEXT("성공 변경만 이벤트"), Events, 2);
    TestEqual(TEXT("확정 적용"), Model->Apply(Present), ECustomizationFailure::None);
    TestTrue(TEXT("확정 값 일치"), Model->GetProfile() == Good && !Model->IsEditing());
    TestEqual(TEXT("확정 후 재개방"), Model->BeginEdit(Present), ECustomizationFailure::None);
    TestTrue(TEXT("재개방 확정 값 복원"), Model->GetDraft() == Good);
    for (float Value : {0.f, 0.5f, 1.f})
    {
        auto Candidate = Good; Candidate.HeadShape = Value;
        TestEqual(TEXT("머리 모양 양쪽 경계와 중간 편집"), Model->TryEdit(Candidate, Present), ECustomizationFailure::None);
        CheckBody(Target->GetDisplayedBody(), Candidate);
        TestTrue(TEXT("머리 모양 편집은 확정 값 보존"), Model->GetProfile() == Good);
        const int32 BeforeEvents = Events;
        auto* BeforeBody = Target->GetDisplayedBody();
        TestEqual(TEXT("머리 모양 동일 값 재적용"), Model->TryEdit(Candidate, Present), ECustomizationFailure::None);
        TestTrue(TEXT("동일 값 body 재생성/통지 없음"), Target->GetDisplayedBody() == BeforeBody && Events == BeforeEvents);
    }
    TestEqual(TEXT("실제 초기화 성공"), Model->Reset(Present), ECustomizationFailure::None);
    CheckBody(Target->GetDisplayedBody(), FCustomizationProfile());
    TestTrue(TEXT("실제 초기화 확정 값 보존"), Model->GetProfile() == Good && Model->GetDraft() == FCustomizationProfile());
    TestEqual(TEXT("실제 취소 확정 외형 복원"), Model->Cancel(Present), ECustomizationFailure::None);
    CheckBody(Target->GetDisplayedBody(), Good);
    FCustomizationProfile Second; Second.SkinColor = 10;
    TestEqual(TEXT("두 번째 실제 대상"), Other->TryApply(Second), ECustomizationFailure::None);
    auto* OtherPartMID = Other->GetDisplayedSlotMID(ECustomizationSlot::Hair); auto* OtherBodyMID = Other->GetDisplayedBodyMID();
    auto* OtherBody = Other->GetDisplayedBody();
    UMaterialInterface* Shared = OtherPartMID->Parent;
    FLinearColor BeforeShared; Shared->GetVectorParameterValue(FMaterialParameterInfo(TEXT("Color")), BeforeShared);
    Good.SkinColor = 14; Good.HairColor = 5; Good.HeadShape = 0.2f;
    TestEqual(TEXT("한 대상의 색상/머리 모양 재편집"), Target->TryApply(Good), ECustomizationFailure::None);
    FLinearColor BodyColor, AfterShared;
    TestTrue(TEXT("다른 대상의 body/헤어 색상 독립"), Other->GetDisplayedSlotMID(ECustomizationSlot::Hair) == OtherPartMID && Other->GetDisplayedBodyMID() == OtherBodyMID
        && OtherPartMID->GetVectorParameterValue(FMaterialParameterInfo(TEXT("Color")), PartColor) && PartColor == GetCustomizationColor(Second.HairColor)
        && OtherBodyMID->GetVectorParameterValue(FMaterialParameterInfo(TEXT("Color")), BodyColor) && BodyColor == GetCustomizationColor(Second.SkinColor));
    TestTrue(TEXT("공유 material 값 보존"), Shared->GetVectorParameterValue(FMaterialParameterInfo(TEXT("Color")), AfterShared) && BeforeShared == AfterShared);
    TestTrue(TEXT("다른 대상의 body component 독립"), Other->GetDisplayedBody() == OtherBody);
    CheckBody(OtherBody, Second);
    CollectGarbage(RF_NoFlags);
    TestTrue(TEXT("GC 후 실제 body/part/MID 보존"), IsValid(Target->GetDisplayedBody()) && IsValid(Target->GetDisplayedSlotMesh(ECustomizationSlot::Hair))
        && IsValid(Target->GetDisplayedSlotMID(ECustomizationSlot::Hair)) && IsValid(Target->GetDisplayedBodyMID()));
    Target->ClearPreview();
    TestTrue(TEXT("body까지 정리"), !Target->GetDisplayedBody() && !Target->GetDisplayedBodyMID()
        && !Target->GetDisplayedSlotMesh(ECustomizationSlot::Hair) && !Target->GetDisplayedSlotMID(ECustomizationSlot::Hair));
    return true;
}
#endif
