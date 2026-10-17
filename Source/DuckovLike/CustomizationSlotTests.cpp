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

bool HasColor(const UMaterialInstanceDynamic* MID, int32 PaletteIndex)
{
    FLinearColor Color;
    return MID && MID->GetVectorParameterValue(FMaterialParameterInfo(TEXT("Color")), Color)
        && Color.Equals(GetCustomizationColor(PaletteIndex));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlotBatchReplace, "Duckov.Customization.Slots.BatchReplaceAtomicity", Flags)
bool FSlotBatchReplace::RunTest(const FString& Parameters)
{
    FRealWorld Scene;
    UCustomizationAppearanceComponent* Target = Scene.CreateTarget();
    TStrongObjectPtr<UCustomizationModel> Model{NewObject<UCustomizationModel>()};
    const auto Present = [Target](const FCustomizationProfile& Value) { return Target->TryApply(Value); };
    if (!TestEqual(TEXT("기본 Profile 표시"), Model->BeginEdit(Present), ECustomizationFailure::None)) { return false; }
    bool bAllSlots = Target->GetDisplayedBody() != nullptr;
    for (const ECustomizationSlot Slot : GetCustomizationSlots()) { bAllSlots &= Target->GetDisplayedSlotMesh(Slot) != nullptr; }
    TestTrue(TEXT("기본 표시는 고정 몸통과 여섯 부위"), bAllSlots);
    int32 Events = 0;
    Model->OnChanged().AddLambda([&Events] { ++Events; });

    // 여섯 부위와 색을 한 번에 교체한다.
    FCustomizationProfile Full;
    Full.EyeId = TEXT("EyeLarge"); Full.HairId = TEXT("HairB"); Full.BeakId = TEXT("BeakLong");
    Full.WingId = TEXT("WingMitten"); Full.TailId = TEXT("TailShort"); Full.FootId = TEXT("FootBoot");
    Full.SkinColor = 10; Full.EyeColor = 1; Full.HairColor = 13; Full.BeakColor = 6; Full.WingColor = 8; Full.TailColor = 12;
    if (!TestEqual(TEXT("여섯 슬롯 일괄 교체"), Model->TryEdit(Full, Present), ECustomizationFailure::None)) { return false; }
    TestEqual(TEXT("일괄 교체는 통지 한 번"), Events, 1);
    for (const ECustomizationSlot Slot : GetCustomizationSlots())
    {
        const FCustomizationPartResources Resources = Target->GetDefaultResources(Full.GetSlotPartId(Slot));
        TestEqual(*FString::Printf(TEXT("%s mesh"), *GetCustomizationSlotText(Slot).ToString()),
            MeshName(Target->GetDisplayedSlotMesh(Slot)), Resources.Mesh.GetAssetName());
        UMaterialInstanceDynamic* MID = Target->GetDisplayedSlotMID(Slot);
        if (IsCustomizationSlotColorable(Slot))
        {
            TestTrue(*FString::Printf(TEXT("%s는 고른 팔레트 색"), *GetCustomizationSlotText(Slot).ToString()), HasColor(MID, Full.GetSlotColor(Slot)));
        }
        else
        {
            TestTrue(TEXT("발은 유형의 고정색이라 override가 없다"), MID && MID->VectorParameterValues.Num() == 0);
        }
    }
    TestTrue(TEXT("몸통은 피부색"), HasColor(Target->GetDisplayedBodyMID(), Full.SkinColor));
    TestTrue(TEXT("표시 기준 Profile"), Target->GetAppliedProfile() == Full);

    // 한 슬롯만 깨뜨리고 모든 슬롯이 바뀔 후보(기본 Profile)를 적용한다. 실패하면 표시·Draft·확정 값 어느 것도 바뀌지 않는다.
    const FDisplaySnapshot Before(Target);
    const FCustomizationProfile Confirmed = Model->GetProfile();
    struct FInjection { FName PartId; ECustomizationSlot Slot; ECustomizationFailure Expected;
        TFunction<void(FCustomizationPartResources&)> Break; };
    FCustomizationProfile Candidate; Candidate.SkinColor = Full.SkinColor;
    const FInjection Injections[] = {
        {TEXT("HairA"), ECustomizationSlot::Hair, ECustomizationFailure::SkeletonMismatch,
            [](FCustomizationPartResources& R) { R.ExpectedSkeleton = FSoftObjectPath(TEXT("/Game/Customization/Tests/SKEL_Unexpected.SKEL_Unexpected")); }},
        {TEXT("EyeRound"), ECustomizationSlot::Eye, ECustomizationFailure::MissingMesh, [](FCustomizationPartResources& R) { R.Mesh.Reset(); }},
        {TEXT("FootWebbed"), ECustomizationSlot::Foot, ECustomizationFailure::MissingMaterial, [](FCustomizationPartResources& R) { R.Material.Reset(); }},
    };
    for (const FInjection& Injection : Injections)
    {
        FCustomizationPartResources Broken = Target->GetDefaultResources(Injection.PartId);
        Injection.Break(Broken);
        Target->SetResourcesForTests(Injection.PartId, Broken);
        TestEqual(TEXT("일괄 교체 중 한 슬롯 실패 사유"), Model->TryEdit(Candidate, Present), Injection.Expected);
        TestTrue(TEXT("실패한 슬롯이 어디인지 알 수 있다"), Target->GetLastFailure().bHasSlot && Target->GetLastFailure().Slot == Injection.Slot
            && Target->GetLastFailure().Reason == Injection.Expected);
        TestTrue(TEXT("모든 슬롯의 표시가 그대로다"), FDisplaySnapshot(Target) == Before);
        TestTrue(TEXT("Draft와 확정 값도 그대로다"), Model->GetDraft() == Full && Model->GetProfile() == Confirmed && Model->IsEditing());
        TestEqual(TEXT("실패는 통지하지 않는다"), Events, 1);
        Target->SetResourcesForTests(Injection.PartId, Target->GetDefaultResources(Injection.PartId));
    }

    // 데이터 단계 실패도 슬롯을 알려 준다.
    FCustomizationProfile Unknown = Full; Unknown.TailId = TEXT("Unregistered");
    TestEqual(TEXT("미등록 꼬리"), Model->TryEdit(Unknown, Present), ECustomizationFailure::UnknownPart);
    TestTrue(TEXT("Model이 슬롯을 알려 준다"), Model->GetLastFailure().bHasSlot && Model->GetLastFailure().Slot == ECustomizationSlot::Tail);
    FCustomizationProfile Mismatch = Full; Mismatch.BeakId = TEXT("HairA");
    TestEqual(TEXT("다른 슬롯의 파츠를 부리 슬롯에 지정"), Model->TryEdit(Mismatch, Present), ECustomizationFailure::SlotMismatch);
    TestTrue(TEXT("슬롯 불일치의 슬롯"), Model->GetLastFailure().bHasSlot && Model->GetLastFailure().Slot == ECustomizationSlot::Beak);
    FCustomizationProfile BadColor = Full; BadColor.WingColor = GetCustomizationPalette().Num();
    TestEqual(TEXT("팔레트 밖 날개 색"), Model->TryEdit(BadColor, Present), ECustomizationFailure::ColorOutOfRange);
    TestTrue(TEXT("색 실패도 슬롯을 알려 준다"), Model->GetLastFailure().bHasSlot && Model->GetLastFailure().Slot == ECustomizationSlot::Wing);
    TestTrue(TEXT("데이터 실패 뒤에도 표시 보존"), FDisplaySnapshot(Target) == Before && Events == 1);

    // 헤어 색만 바꾸면 헤어만 다시 준비하고 다른 부위와 몸통은 그대로 둔다.
    FCustomizationProfile Recolored = Full; Recolored.HairColor = 2;
    TestEqual(TEXT("헤어 색 편집"), Model->TryEdit(Recolored, Present), ECustomizationFailure::None);
    const int32 HairIndex = static_cast<int32>(ECustomizationSlot::Hair);
    bool bOthersKept = Target->GetDisplayedBody() == Before.Body && Target->GetDisplayedBodyMID() == Before.BodyMID;
    for (int32 Index = 0; Index < CustomizationSlotCount; ++Index)
    {
        if (Index == HairIndex) { continue; }
        bOthersKept &= Target->GetDisplayedSlotMesh(static_cast<ECustomizationSlot>(Index)) == Before.Meshes[Index]
            && Target->GetDisplayedSlotMID(static_cast<ECustomizationSlot>(Index)) == Before.MIDs[Index];
    }
    TestTrue(TEXT("다른 부위·몸통 component 유지"), bOthersKept);
    TestTrue(TEXT("헤어는 새 색으로 새로 준비"), Target->GetDisplayedSlotMesh(ECustomizationSlot::Hair) != Before.Meshes[HairIndex]
        && HasColor(Target->GetDisplayedSlotMID(ECustomizationSlot::Hair), Recolored.HairColor));

    // 피부색만 바꾸면 몸통만 다시 준비한다.
    UMeshComponent* EyeBefore = Target->GetDisplayedSlotMesh(ECustomizationSlot::Eye);
    USkeletalMeshComponent* BodyBefore = Target->GetDisplayedBody();
    FCustomizationProfile Reskinned = Recolored; Reskinned.SkinColor = 3;
    TestEqual(TEXT("피부색 편집"), Model->TryEdit(Reskinned, Present), ECustomizationFailure::None);
    TestTrue(TEXT("피부색은 몸통만 교체"), Target->GetDisplayedBody() != BodyBefore && HasColor(Target->GetDisplayedBodyMID(), 3)
        && Target->GetDisplayedSlotMesh(ECustomizationSlot::Eye) == EyeBefore);

    // 헤어를 비우면 헤어 component만 정리된다.
    UMeshComponent* RetiredHair = Target->GetDisplayedSlotMesh(ECustomizationSlot::Hair);
    FCustomizationProfile NoHair = Reskinned; NoHair.HairId = NAME_None;
    TestEqual(TEXT("헤어 제거"), Model->TryEdit(NoHair, Present), ECustomizationFailure::None);
    TestTrue(TEXT("제거한 슬롯은 비고 이전 component는 해제"), !Target->GetDisplayedSlotMesh(ECustomizationSlot::Hair)
        && !Target->GetDisplayedSlotMID(ECustomizationSlot::Hair) && (!IsValid(RetiredHair) || !RetiredHair->IsRegistered())
        && Target->GetDisplayedSlotMesh(ECustomizationSlot::Eye) == EyeBefore);
    TestEqual(TEXT("확정 적용"), Model->Apply(Present), ECustomizationFailure::None);
    TestTrue(TEXT("확정 값 일치"), Model->GetProfile() == NoHair && Target->GetAppliedProfile() == NoHair);
    TestEqual(TEXT("재개방"), Model->BeginEdit(Present), ECustomizationFailure::None);
    TestEqual(TEXT("초기화는 기본 부위로"), Model->Reset(Present), ECustomizationFailure::None);
    TestTrue(TEXT("초기화 표시"), Target->GetAppliedProfile() == FCustomizationProfile()
        && MeshName(Target->GetDisplayedSlotMesh(ECustomizationSlot::Hair)) == TEXT("SK_CustomizationPartA"));
    TestEqual(TEXT("취소로 확정 외형 복귀"), Model->Cancel(Present), ECustomizationFailure::None);
    TestTrue(TEXT("복귀 표시"), Target->GetAppliedProfile() == NoHair && !Target->GetDisplayedSlotMesh(ECustomizationSlot::Hair));
    CollectGarbage(RF_NoFlags);
    TestTrue(TEXT("GC 후 슬롯별 표시 보존"), IsValid(Target->GetDisplayedSlotMesh(ECustomizationSlot::Eye))
        && IsValid(Target->GetDisplayedSlotMesh(ECustomizationSlot::Foot)) && IsValid(Target->GetDisplayedSlotMID(ECustomizationSlot::Tail))
        && IsValid(Target->GetDisplayedBody()));
    Target->ClearPreview();
    TestTrue(TEXT("preview 종료 시 모든 슬롯 정리"), !Target->GetDisplayedSlotMesh(ECustomizationSlot::Eye)
        && !Target->GetDisplayedSlotMesh(ECustomizationSlot::Foot) && !Target->GetDisplayedBody() && !Target->HasAppliedProfile());
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
    TestEqual(TEXT("정상 선택"), VM->SelectSlotPart(ECustomizationSlot::Wing, TEXT("WingSmall")), ECustomizationFailure::None);
    TestEqual(TEXT("선택 getter"), VM->GetSlotPartId(ECustomizationSlot::Wing), FName(TEXT("WingSmall")));
    TestEqual(TEXT("슬롯 순서 목록 getter"), VM->GetPartIds()[static_cast<int32>(ECustomizationSlot::Wing)], FName(TEXT("WingSmall")));
    TestTrue(TEXT("성공이면 안내 없음"), VM->GetFailureText().IsEmpty());
    const FString Unknown = [&]
    {
        VM->SelectSlotPart(ECustomizationSlot::Beak, TEXT("Unregistered"));
        return VM->GetFailureText().ToString();
    }();
    TestTrue(TEXT("데이터 실패 안내에 슬롯 이름"), Unknown.Contains(GetCustomizationSlotText(ECustomizationSlot::Beak).ToString()));
    FCustomizationPartResources Broken = Target->GetDefaultResources(TEXT("WingSmall"));
    Broken.Mesh.Reset();
    Target->SetResourcesForTests(TEXT("WingSmall"), Broken);
    TestEqual(TEXT("준비 실패"), VM->SetSkinColor(4), ECustomizationFailure::MissingMesh);
    TestTrue(TEXT("준비 실패 안내에 실패한 슬롯 이름"),
        VM->GetFailureText().ToString().Contains(GetCustomizationSlotText(ECustomizationSlot::Wing).ToString()));
    TestTrue(TEXT("실패 Draft 보존"), VM->GetSkinColor() == FCustomizationProfile().SkinColor && VM->GetSlotPartId(ECustomizationSlot::Wing) == TEXT("WingSmall"));
    Target->SetResourcesForTests(TEXT("WingSmall"), Target->GetDefaultResources(TEXT("WingSmall")));
    TestEqual(TEXT("복구 뒤 성공"), VM->SetSkinColor(4), ECustomizationFailure::None);
    TestTrue(TEXT("성공하면 안내가 지워진다"), VM->GetFailureText().IsEmpty() && VM->GetColorIndices()[0] == 4);
    TestEqual(TEXT("발 색 지정은 값이 없어 무변경 성공"), VM->SetSlotColor(ECustomizationSlot::Foot, 7), ECustomizationFailure::None);
    TestEqual(TEXT("발 색은 없음"), VM->GetSlotColor(ECustomizationSlot::Foot), INDEX_NONE);
    VM->Bind(nullptr, nullptr);
    Target->ClearPreview();
    return true;
}
}
#endif
