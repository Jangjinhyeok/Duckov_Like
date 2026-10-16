#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "CustomizationModel.h"
#include "CustomizationTestSupport.h"
#include "CustomizationViewModel.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/StrongObjectPtr.h"

namespace CustomizationSlotTests
{
using namespace CustomizationTestSupport;

FString MeshName(const UMeshComponent* Component)
{
    const auto* Skeletal = Cast<USkeletalMeshComponent>(Component);
    return Skeletal && Skeletal->GetSkeletalMeshAsset() ? Skeletal->GetSkeletalMeshAsset()->GetName() : FString();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlotBatchReplace, "Duckov.Customization.Slots.BatchReplaceAtomicity", Flags)
bool FSlotBatchReplace::RunTest(const FString& Parameters)
{
    FRealWorld Scene;
    UCustomizationAppearanceComponent* Target = Scene.CreateTarget();
    TStrongObjectPtr<UCustomizationModel> Model{NewObject<UCustomizationModel>()};
    const auto Present = [Target](const FCustomizationProfile& Value) { return Target->TryApply(Value); };
    if (!TestEqual(TEXT("기본 Profile(머리 장식만) 표시"), Model->BeginEdit(Present), ECustomizationFailure::None)) { return false; }
    TestTrue(TEXT("기본 표시는 머리 장식과 body뿐"), Target->GetDisplayedSlotMesh(ECustomizationSlot::Crest)
        && !Target->GetDisplayedSlotMesh(ECustomizationSlot::Eyewear) && !Target->GetDisplayedSlotMesh(ECustomizationSlot::Beak)
        && Target->GetDisplayedBody());
    int32 Events = 0;
    Model->OnChanged().AddLambda([&Events] { ++Events; });

    // 세 슬롯을 한 번에 교체한다.
    FCustomizationProfile Full;
    Full.PartId = TEXT("B"); Full.EyewearId = TEXT("RoundGlasses"); Full.BeakWearId = TEXT("BeakRing"); Full.Hue = 0.7f;
    if (!TestEqual(TEXT("세 슬롯 일괄 교체"), Model->TryEdit(Full, Present), ECustomizationFailure::None)) { return false; }
    TestEqual(TEXT("일괄 교체는 통지 한 번"), Events, 1);
    TestEqual(TEXT("머리 장식 mesh"), MeshName(Target->GetDisplayedSlotMesh(ECustomizationSlot::Crest)), FString(TEXT("SK_CustomizationPartB")));
    TestEqual(TEXT("눈 장식 mesh"), MeshName(Target->GetDisplayedSlotMesh(ECustomizationSlot::Eyewear)), FString(TEXT("SK_CustomizationRoundGlasses")));
    TestEqual(TEXT("부리 장식 mesh"), MeshName(Target->GetDisplayedSlotMesh(ECustomizationSlot::Beak)), FString(TEXT("SK_CustomizationBeakRing")));
    TestTrue(TEXT("표시 기준 Profile"), Target->GetAppliedProfile() == Full);
    FLinearColor Color;
    const FLinearColor Expected = GetCustomizationColor(Full.Hue);
    UMaterialInstanceDynamic* GlassesMID = Target->GetDisplayedSlotMID(ECustomizationSlot::Eyewear);
    TestTrue(TEXT("색상 슬롯(머리 장식·부리 고리)은 Hue를 따른다"),
        Target->GetDisplayedSlotMID(ECustomizationSlot::Crest)->GetVectorParameterValue(FMaterialParameterInfo(TEXT("Color")), Color) && Color.Equals(Expected)
        && Target->GetDisplayedSlotMID(ECustomizationSlot::Beak)->GetVectorParameterValue(FMaterialParameterInfo(TEXT("Color")), Color) && Color.Equals(Expected));
    TestTrue(TEXT("안경은 고정색이라 override가 없다"), GlassesMID && GlassesMID->VectorParameterValues.Num() == 0);
    TestTrue(TEXT("선택 슬롯 mesh는 모두 등록·표시"), Target->GetDisplayedSlotMesh(ECustomizationSlot::Eyewear)->IsRegistered()
        && Target->GetDisplayedSlotMesh(ECustomizationSlot::Eyewear)->IsVisible() && Target->GetDisplayedSlotMesh(ECustomizationSlot::Beak)->IsVisible());

    // 한 슬롯만 깨뜨리고 세 슬롯이 모두 바뀔 후보를 적용한다. 실패하면 표시·Draft·확정 값 어느 것도 바뀌지 않는다.
    const FDisplaySnapshot Before(Target);
    const FCustomizationProfile Confirmed = Model->GetProfile();
    struct FInjection { FName PartId; ECustomizationSlot Slot; ECustomizationFailure Expected; FCustomizationProfile Candidate;
        TFunction<void(FCustomizationPartResources&)> Break; };
    FCustomizationProfile CrestBroken; CrestBroken.PartId = TEXT("A"); CrestBroken.Hue = Full.Hue;
    FCustomizationProfile EyewearBroken = CrestBroken; EyewearBroken.EyewearId = TEXT("RoundGlasses");
    FCustomizationProfile BeakBroken = CrestBroken; BeakBroken.BeakWearId = TEXT("BeakRing");
    const FInjection Injections[] = {
        {TEXT("A"), ECustomizationSlot::Crest, ECustomizationFailure::SkeletonMismatch, CrestBroken,
            [](FCustomizationPartResources& R) { R.ExpectedSkeleton = FSoftObjectPath(TEXT("/Game/Customization/Tests/SKEL_Unexpected.SKEL_Unexpected")); }},
        {TEXT("RoundGlasses"), ECustomizationSlot::Eyewear, ECustomizationFailure::MissingMesh, EyewearBroken,
            [](FCustomizationPartResources& R) { R.Mesh.Reset(); }},
        {TEXT("BeakRing"), ECustomizationSlot::Beak, ECustomizationFailure::MissingMaterial, BeakBroken,
            [](FCustomizationPartResources& R) { R.Material.Reset(); }},
    };
    for (const FInjection& Injection : Injections)
    {
        FCustomizationPartResources Broken = Target->GetDefaultResources(Injection.PartId);
        Injection.Break(Broken);
        Target->SetResourcesForTests(Injection.PartId, Broken);
        TestEqual(TEXT("일괄 교체 중 한 슬롯 실패 사유"), Model->TryEdit(Injection.Candidate, Present), Injection.Expected);
        TestTrue(TEXT("실패한 슬롯이 어디인지 알 수 있다"), Target->GetLastFailure().bHasSlot && Target->GetLastFailure().Slot == Injection.Slot
            && Target->GetLastFailure().Reason == Injection.Expected);
        TestTrue(TEXT("모든 슬롯의 표시가 그대로다"), FDisplaySnapshot(Target) == Before);
        TestTrue(TEXT("Draft와 확정 값도 그대로다"), Model->GetDraft() == Full && Model->GetProfile() == Confirmed && Model->IsEditing());
        TestEqual(TEXT("실패는 통지하지 않는다"), Events, 1);
        Target->SetResourcesForTests(Injection.PartId, Target->GetDefaultResources(Injection.PartId));
    }

    // 데이터 단계 실패도 슬롯을 알려 준다.
    FCustomizationProfile Unknown = Full; Unknown.EyewearId = TEXT("Unregistered");
    TestEqual(TEXT("미등록 눈 장식"), Model->TryEdit(Unknown, Present), ECustomizationFailure::UnknownPart);
    TestTrue(TEXT("Model이 슬롯을 알려 준다"), Model->GetLastFailure().bHasSlot && Model->GetLastFailure().Slot == ECustomizationSlot::Eyewear);
    FCustomizationProfile Mismatch = Full; Mismatch.BeakWearId = TEXT("A");
    TestEqual(TEXT("다른 슬롯의 파츠를 부리 슬롯에 지정"), Model->TryEdit(Mismatch, Present), ECustomizationFailure::SlotMismatch);
    TestTrue(TEXT("슬롯 불일치의 슬롯"), Model->GetLastFailure().bHasSlot && Model->GetLastFailure().Slot == ECustomizationSlot::Beak);
    TestTrue(TEXT("데이터 실패 뒤에도 표시 보존"), FDisplaySnapshot(Target) == Before && Events == 1);

    // 색상만 바꾸면 고정색 안경은 그대로 두고 색을 따르는 슬롯만 다시 준비한다.
    FCustomizationProfile Recolored = Full; Recolored.Hue = 0.2f;
    TestEqual(TEXT("색상 편집"), Model->TryEdit(Recolored, Present), ECustomizationFailure::None);
    TestTrue(TEXT("고정색 안경 component 유지"), Target->GetDisplayedSlotMesh(ECustomizationSlot::Eyewear) == Before.Meshes[1]
        && Target->GetDisplayedSlotMID(ECustomizationSlot::Eyewear) == GlassesMID);
    TestTrue(TEXT("색을 따르는 슬롯은 새로 준비"), Target->GetDisplayedSlotMesh(ECustomizationSlot::Crest) != Before.Meshes[0]
        && Target->GetDisplayedSlotMesh(ECustomizationSlot::Beak) != Before.Meshes[2]);

    // 선택 슬롯을 비우면 해당 component만 정리된다.
    UMeshComponent* RetiredGlasses = Target->GetDisplayedSlotMesh(ECustomizationSlot::Eyewear);
    FCustomizationProfile NoGlasses = Recolored; NoGlasses.EyewearId = NAME_None;
    TestEqual(TEXT("눈 장식 제거"), Model->TryEdit(NoGlasses, Present), ECustomizationFailure::None);
    TestTrue(TEXT("제거한 슬롯은 비고 이전 component는 해제"), !Target->GetDisplayedSlotMesh(ECustomizationSlot::Eyewear)
        && !Target->GetDisplayedSlotMID(ECustomizationSlot::Eyewear) && (!IsValid(RetiredGlasses) || !RetiredGlasses->IsRegistered())
        && Target->GetDisplayedSlotMesh(ECustomizationSlot::Beak));
    TestEqual(TEXT("확정 적용"), Model->Apply(Present), ECustomizationFailure::None);
    TestTrue(TEXT("확정 값 일치"), Model->GetProfile() == NoGlasses && Target->GetAppliedProfile() == NoGlasses);
    TestEqual(TEXT("재개방"), Model->BeginEdit(Present), ECustomizationFailure::None);
    TestEqual(TEXT("초기화는 선택 슬롯을 모두 비운다"), Model->Reset(Present), ECustomizationFailure::None);
    TestTrue(TEXT("초기화 표시"), Target->GetAppliedProfile() == FCustomizationProfile()
        && !Target->GetDisplayedSlotMesh(ECustomizationSlot::Beak) && !Target->GetDisplayedSlotMesh(ECustomizationSlot::Eyewear));
    TestEqual(TEXT("취소로 확정 외형 복귀"), Model->Cancel(Present), ECustomizationFailure::None);
    TestTrue(TEXT("복귀 표시"), Target->GetAppliedProfile() == NoGlasses && Target->GetDisplayedSlotMesh(ECustomizationSlot::Beak));
    CollectGarbage(RF_NoFlags);
    TestTrue(TEXT("GC 후 슬롯별 표시 보존"), IsValid(Target->GetDisplayedSlotMesh(ECustomizationSlot::Crest))
        && IsValid(Target->GetDisplayedSlotMesh(ECustomizationSlot::Beak)) && IsValid(Target->GetDisplayedSlotMID(ECustomizationSlot::Beak))
        && IsValid(Target->GetDisplayedBody()));
    Target->ClearPreview();
    TestTrue(TEXT("preview 종료 시 모든 슬롯 정리"), !Target->GetDisplayedSlotMesh(ECustomizationSlot::Crest)
        && !Target->GetDisplayedSlotMesh(ECustomizationSlot::Beak) && !Target->GetDisplayedBody() && !Target->HasAppliedProfile());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlotFailureText, "Duckov.Customization.Slots.FailureNamesTheSlot", Flags)
bool FSlotFailureText::RunTest(const FString& Parameters)
{
    FRealWorld Scene;
    UCustomizationAppearanceComponent* Target = Scene.CreateTarget();
    TStrongObjectPtr<UCustomizationModel> Model{NewObject<UCustomizationModel>()};
    TStrongObjectPtr<UCustomizationViewModel> VM{NewObject<UCustomizationViewModel>()};
    VM->Bind(Model.Get(), Target);
    if (!TestEqual(TEXT("열기"), VM->Open(), ECustomizationFailure::None)) { return false; }
    TestEqual(TEXT("정상 선택"), VM->SelectSlotPart(ECustomizationSlot::Eyewear, TEXT("RoundGlasses")), ECustomizationFailure::None);
    TestEqual(TEXT("선택 getter"), VM->GetEyewearId(), FName(TEXT("RoundGlasses")));
    TestTrue(TEXT("성공이면 안내 없음"), VM->GetFailureText().IsEmpty());
    const FString Unknown = [&]
    {
        VM->SelectSlotPart(ECustomizationSlot::Beak, TEXT("Unregistered"));
        return VM->GetFailureText().ToString();
    }();
    TestTrue(TEXT("데이터 실패 안내에 슬롯 이름"), Unknown.Contains(GetCustomizationSlotText(ECustomizationSlot::Beak).ToString()));
    FCustomizationPartResources Broken = Target->GetDefaultResources(TEXT("RoundGlasses"));
    Broken.Mesh.Reset();
    Target->SetResourcesForTests(TEXT("RoundGlasses"), Broken);
    TestEqual(TEXT("준비 실패"), VM->SetHue(0.4f), ECustomizationFailure::MissingMesh);
    TestTrue(TEXT("준비 실패 안내에 실패한 슬롯 이름"),
        VM->GetFailureText().ToString().Contains(GetCustomizationSlotText(ECustomizationSlot::Eyewear).ToString()));
    TestTrue(TEXT("실패 Draft 보존"), Model->GetDraft().Hue == FCustomizationProfile().Hue && VM->GetEyewearId() == TEXT("RoundGlasses"));
    Target->SetResourcesForTests(TEXT("RoundGlasses"), Target->GetDefaultResources(TEXT("RoundGlasses")));
    TestEqual(TEXT("복구 뒤 성공"), VM->SetHue(0.4f), ECustomizationFailure::None);
    TestTrue(TEXT("성공하면 안내가 지워진다"), VM->GetFailureText().IsEmpty());
    VM->Bind(nullptr, nullptr);
    Target->ClearPreview();
    return true;
}
}
#endif
