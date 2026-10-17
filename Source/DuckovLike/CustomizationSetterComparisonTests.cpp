#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "CustomizationModel.h"
#include "CustomizationTestSupport.h"
#include "HAL/PlatformMisc.h"
#include "Misc/DateTime.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/App.h"
#include "UObject/StrongObjectPtr.h"

namespace CustomizationSetterComparisonTests
{
using namespace CustomizationTestSupport;

// 대조군이다. 제품 코드가 아니며 "상태를 먼저 바꾸는 단순 setter"의 순서를 일부러 그대로 재현한다.
//  1) 확정 값과 Draft를 먼저 바꾸고 변경 이벤트를 낸다.
//  2) 그다음 슬롯을 하나씩 표시에 반영한다. 앞 슬롯의 성공은 되돌리지 않고, 실패해도 이미 바뀐 상태는 그대로다.
// 표시는 실제 UCustomizationAppearanceComponent를 쓰므로 같은 리소스·같은 실패 주입이 두 방식에 똑같이 적용된다.
struct FNaiveSetter
{
    FCustomizationProfile Profile;
    FCustomizationProfile Draft;
    int32 Events = 0;

    void Set(UCustomizationAppearanceComponent& Appearance, const FCustomizationProfile& Candidate)
    {
        Profile = Candidate;
        Draft = Candidate;
        ++Events;
        FCustomizationProfile Shown = Appearance.GetAppliedProfile();
        for (const ECustomizationSlot Slot : GetCustomizationSlots())
        {
            Shown.SetSlotPartId(Slot, Candidate.GetSlotPartId(Slot));
            Appearance.TryApply(Shown);
            // 실패한 슬롯은 표시에 반영되지 않았으므로 다음 슬롯은 마지막으로 성공한 표시에서 이어 간다.
            Shown = Appearance.GetAppliedProfile();
        }
        // 마지막으로 색과 머리 모양을 반영한다. 파츠는 위에서 성공한 것만 남는다.
        FCustomizationProfile Final = Candidate;
        for (const ECustomizationSlot Slot : GetCustomizationSlots()) { Final.SetSlotPartId(Slot, Shown.GetSlotPartId(Slot)); }
        Appearance.TryApply(Final);
    }
};

struct FOutcome
{
    int32 ProfileMismatch = 0;
    int32 DraftMismatch = 0;
    int32 DisplayLeak = 0;
    int32 SpuriousEvents = 0;
    int32 Total() const { return ProfileMismatch + DraftMismatch + DisplayLeak + SpuriousEvents; }
};

struct FScenario
{
    const TCHAR* Name;
    bool bExpectFailure;
    FCustomizationProfile Candidate;
    // 실제 에셋 리소스를 깨뜨린다. 두 방식에 같은 주입을 쓴다.
    TFunction<void(UCustomizationAppearanceComponent&)> Inject;
    TFunction<void(UCustomizationAppearanceComponent&)> Restore;
};

FString Mark(int32 Count) { return Count == 0 ? FString(TEXT("0")) : FString::Printf(TEXT("**%d**"), Count); }

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSetterComparison, "Duckov.Customization.Comparison.StateFirstSetterVsBatchReplace", Flags)
bool FSetterComparison::RunTest(const FString& Parameters)
{
    FCustomizationProfile Valid;
    Valid.EyeId = TEXT("EyeLarge"); Valid.HairId = TEXT("HairB"); Valid.BeakId = TEXT("BeakLong");
    Valid.WingId = TEXT("WingMitten"); Valid.TailId = TEXT("TailShort"); Valid.FootId = TEXT("FootBoot"); Valid.SkinColor = 6;
    FCustomizationProfile Unregistered; Unregistered.HairId = TEXT("Unregistered");
    FCustomizationProfile PickB; PickB.HairId = TEXT("HairB");
    FCustomizationProfile OneOfSix = Valid;  // 다른 부위는 정상이고 꼬리 슬롯만 깨진다.
    const auto Break = [](FName PartId, TFunction<void(FCustomizationPartResources&)> Edit)
    {
        return [PartId, Edit](UCustomizationAppearanceComponent& Target)
        {
            FCustomizationPartResources Resources = Target.GetDefaultResources(PartId);
            Edit(Resources);
            Target.SetResourcesForTests(PartId, Resources);
        };
    };
    const auto Fix = [](FName PartId)
    {
        return [PartId](UCustomizationAppearanceComponent& Target) { Target.SetResourcesForTests(PartId, Target.GetDefaultResources(PartId)); };
    };
    const TArray<FScenario> Scenarios = {
        {TEXT("대조: 정상 입력"), false, Valid, nullptr, nullptr},
        {TEXT("미등록 파츠 ID"), true, Unregistered, nullptr, nullptr},
        {TEXT("다른 Skeleton"), true, PickB,
            Break(TEXT("HairB"), [](FCustomizationPartResources& R) { R.ExpectedSkeleton = FSoftObjectPath(TEXT("/Game/Customization/Tests/SKEL_Unexpected.SKEL_Unexpected")); }), Fix(TEXT("HairB"))},
        {TEXT("리소스 로드 실패"), true, PickB,
            Break(TEXT("HairB"), [](FCustomizationPartResources& R) { R.Mesh = R.Material; /* mesh가 아닌 asset을 가리켜 경고 없이 로드 실패를 만든다 */ }), Fix(TEXT("HairB"))},
        {TEXT("여섯 슬롯 중 하나만 실패"), true, OneOfSix,
            Break(TEXT("TailShort"), [](FCustomizationPartResources& R) { R.Mesh.Reset(); }), Fix(TEXT("TailShort"))},
    };

    FRealWorld Scene;
    UCustomizationAppearanceComponent* NaiveTarget = Scene.CreateTarget();
    UCustomizationAppearanceComponent* RealTarget = Scene.CreateTarget();
    FString Table = TEXT("| 실패 상황 | 방식 | 확정 값 어긋남 | Draft 어긋남 | 표시 부분 변경 | 실패인데 발행된 이벤트 | 합계 |\n| --- | --- | ---: | ---: | ---: | ---: | ---: |\n");
    int32 NaiveFailureTotal = 0;
    for (const FScenario& Scenario : Scenarios)
    {
        const FCustomizationProfile Original;
        for (UCustomizationAppearanceComponent* Target : {NaiveTarget, RealTarget})
        {
            Target->ClearPreview();
            TestEqual(TEXT("시나리오 시작 표시"), Target->TryApply(Original), ECustomizationFailure::None);
        }
        if (Scenario.Inject) { Scenario.Inject(*NaiveTarget); Scenario.Inject(*RealTarget); }

        // 단순 setter
        FNaiveSetter Naive;
        Naive.Profile = Naive.Draft = Original;
        Naive.Set(*NaiveTarget, Scenario.Candidate);
        const FCustomizationProfile NaiveShown = NaiveTarget->GetAppliedProfile();
        FOutcome NaiveOutcome;
        const bool bNaiveSucceeded = NaiveShown == Scenario.Candidate;
        NaiveOutcome.ProfileMismatch = Naive.Profile != NaiveShown;
        NaiveOutcome.DraftMismatch = Naive.Draft != NaiveShown;
        NaiveOutcome.DisplayLeak = !bNaiveSucceeded && NaiveShown != Original;
        NaiveOutcome.SpuriousEvents = bNaiveSucceeded ? 0 : Naive.Events;

        // 검증 후 일괄 교체(제품 경로)
        TStrongObjectPtr<UCustomizationModel> Model{NewObject<UCustomizationModel>()};
        int32 Events = 0;
        Model->OnChanged().AddLambda([&Events] { ++Events; });
        const auto Present = [RealTarget](const FCustomizationProfile& Value) { return RealTarget->TryApply(Value); };
        Model->BeginEdit(Present);
        const int32 BaseEvents = Events;
        const ECustomizationFailure Result = Model->TryEdit(Scenario.Candidate, Present);
        const FCustomizationProfile RealShown = RealTarget->GetAppliedProfile();
        const bool bRealSucceeded = Result == ECustomizationFailure::None;
        FOutcome RealOutcome;
        // 성공하면 표시와 Draft가 같아야 하고, 확정 값은 Apply 전이라 원래 값이어야 한다. 실패하면 셋 다 원래 값이어야 한다.
        RealOutcome.ProfileMismatch = Model->GetProfile() != Original;
        RealOutcome.DraftMismatch = Model->GetDraft() != RealShown;
        RealOutcome.DisplayLeak = !bRealSucceeded && RealShown != Original;
        RealOutcome.SpuriousEvents = bRealSucceeded ? 0 : Events - BaseEvents;
        if (Scenario.Restore) { Scenario.Restore(*NaiveTarget); Scenario.Restore(*RealTarget); }

        TestEqual(*FString::Printf(TEXT("%s: 제품 경로의 성공 여부가 기대와 같다"), Scenario.Name),
            bRealSucceeded, !Scenario.bExpectFailure);
        if (Scenario.bExpectFailure)
        {
            TestEqual(*FString::Printf(TEXT("%s: 일괄 교체는 어긋남 0"), Scenario.Name), RealOutcome.Total(), 0);
            TestTrue(*FString::Printf(TEXT("%s: 단순 setter는 실제로 어긋난다"), Scenario.Name), NaiveOutcome.Total() > 0);
            NaiveFailureTotal += NaiveOutcome.Total();
        }
        else
        {
            TestTrue(TEXT("대조: 정상 입력은 두 방식 모두 표시가 후보와 같다"), bNaiveSucceeded && bRealSucceeded);
            TestEqual(TEXT("대조: 정상 입력에서 단순 setter 어긋남 0"), NaiveOutcome.Total() - NaiveOutcome.SpuriousEvents, 0);
        }
        Table += FString::Printf(TEXT("| %s | 단순 setter | %s | %s | %s | %s | %d |\n"), Scenario.Name, *Mark(NaiveOutcome.ProfileMismatch),
            *Mark(NaiveOutcome.DraftMismatch), *Mark(NaiveOutcome.DisplayLeak), *Mark(NaiveOutcome.SpuriousEvents), NaiveOutcome.Total());
        Table += FString::Printf(TEXT("| | 검증 후 일괄 교체 | %s | %s | %s | %s | %d |\n"), *Mark(RealOutcome.ProfileMismatch),
            *Mark(RealOutcome.DraftMismatch), *Mark(RealOutcome.DisplayLeak), *Mark(RealOutcome.SpuriousEvents), RealOutcome.Total());
    }
    NaiveTarget->ClearPreview();
    RealTarget->ClearPreview();
    TestTrue(TEXT("단순 setter의 실패 상황 어긋남 합계가 0보다 크다"), NaiveFailureTotal > 0);

    const FString Header = FString::Printf(TEXT("측정 시각: %s (로컬) / UE %s / %s / RHI %s / CPU %s\n\n"),
        *FDateTime::Now().ToString(TEXT("%Y-%m-%d %H:%M:%S")), *FEngineVersion::Current().ToString(),
        FPlatformMisc::GetOSVersion().IsEmpty() ? TEXT("OS 미상") : *FPlatformMisc::GetOSVersion(), *FApp::GetGraphicsRHI(),
        *FPlatformMisc::GetCPUBrand());
    AddInfo(Header + Table);
    FFileHelper::SaveStringToFile(Header + Table, *(FPaths::ProjectSavedDir() / TEXT("Automation/CustomizationCatalog/before-after.md")),
        FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    return !HasAnyErrors();
}
}
#endif
