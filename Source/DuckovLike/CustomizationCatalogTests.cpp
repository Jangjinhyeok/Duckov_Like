#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "CustomizationTestSupport.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"

namespace CustomizationCatalogTests
{
using namespace CustomizationTestSupport;

FString MeshName(const UMeshComponent* Component)
{
    const auto* Skeletal = Cast<USkeletalMeshComponent>(Component);
    return Skeletal && Skeletal->GetSkeletalMeshAsset() ? Skeletal->GetSkeletalMeshAsset()->GetName() : FString();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatalogResolves, "Duckov.Customization.Catalog.ResolvesRowsBySlot", Flags)
bool FCatalogResolves::RunTest(const FString& Parameters)
{
    const FCustomizationCatalog Catalog = FCustomizationCatalog::LoadDefault();
    if (!TestTrue(TEXT("기본 카탈로그 DataTable"), Catalog.IsAvailable())) { return false; }
    int32 Total = 0;
    for (const ECustomizationSlot Slot : GetCustomizationSlots())
    {
        for (const FName PartId : Catalog.GetPartIds(Slot))
        {
            ++Total;
            const FCustomizationPartRow* Row = Catalog.Find(PartId);
            if (!TestNotNull(TEXT("목록의 파츠는 조회된다"), Row)) { continue; }
            TestTrue(TEXT("row의 슬롯이 목록 슬롯과 같다"), Row->Slot == Slot);
            TestTrue(TEXT("표시 이름이 있다"), !Row->DisplayName.IsEmpty());
            const FCustomizationPartResources Resources = UCustomizationAppearanceComponent::GetDefaultResources(PartId);
            TestTrue(TEXT("GetDefaultResources는 row 리소스를 돌려준다"), Resources.Mesh == Row->Resources.Mesh
                && Resources.Material == Row->Resources.Material && Resources.Morph == Row->Resources.Morph);
        }
    }
    TestTrue(TEXT("등록된 파츠 4개 이상"), Total >= 4);
    TestTrue(TEXT("머리 장식은 필수 슬롯이라 선택지가 있다"), Catalog.GetPartIds(ECustomizationSlot::Crest).Num() >= 2
        && IsCustomizationSlotRequired(ECustomizationSlot::Crest) && !IsCustomizationSlotRequired(ECustomizationSlot::Eyewear));
    TestTrue(TEXT("미등록 ID는 조회되지 않는다"), !Catalog.Find(TEXT("Unregistered")) && !Catalog.Find(NAME_None));
    TestEqual(TEXT("기본 Profile은 통과"), ValidateCustomizationProfile(FCustomizationProfile()), ECustomizationFailure::None);
    return true;
}

// 카탈로그에 등록된 모든 파츠를 자기 슬롯에 실제로 적용한다. row가 늘어도 이 검사는 고치지 않는다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEveryPartApplies, "Duckov.Customization.Catalog.EveryRegisteredPartApplies", Flags)
bool FEveryPartApplies::RunTest(const FString& Parameters)
{
    FRealWorld Scene;
    UCustomizationAppearanceComponent* Target = Scene.CreateTarget();
    const FCustomizationCatalog Catalog = FCustomizationCatalog::LoadDefault();
    int32 Applied = 0;
    for (const ECustomizationSlot Slot : GetCustomizationSlots())
    {
        for (const FName PartId : Catalog.GetPartIds(Slot))
        {
            FCustomizationProfile Profile;
            Profile.SetSlotPartId(Slot, PartId);
            if (!TestEqual(*FString::Printf(TEXT("%s 적용"), *PartId.ToString()), Target->TryApply(Profile), ECustomizationFailure::None)) { continue; }
            const FString Expected = Catalog.Find(PartId)->Resources.Mesh.GetAssetName();
            TestEqual(*FString::Printf(TEXT("%s는 자기 슬롯에 표시된다"), *PartId.ToString()),
                MeshName(Target->GetDisplayedSlotMesh(Slot)), Expected);
            ++Applied;
        }
    }
    TestTrue(TEXT("적용한 파츠가 4개 이상"), Applied >= 4);
    Target->ClearPreview();
    return true;
}

// 런타임 적용 준비와 카탈로그 등록 검사가 같은 규칙과 같은 failure enum을 쓰는지 본다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSharedRules, "Duckov.Customization.Catalog.RuntimeAndRegistrationShareRules", Flags)
bool FSharedRules::RunTest(const FString& Parameters)
{
    FRealWorld Scene;
    UCustomizationAppearanceComponent* Target = Scene.CreateTarget();
    struct FCase
    {
        FName PartId;
        ECustomizationSlot Slot;
        ECustomizationFailure Expected;
        TFunction<void(FCustomizationPartResources&)> Break;
    };
    const FSoftObjectPath Other(TEXT("/Game/Customization/Tests/SKEL_Unexpected.SKEL_Unexpected"));
    const FCase Cases[] = {
        {TEXT("A"), ECustomizationSlot::Crest, ECustomizationFailure::MissingMesh, [](auto& R) { R.Mesh.Reset(); }},
        {TEXT("A"), ECustomizationSlot::Crest, ECustomizationFailure::MissingSkeleton, [](auto& R) { R.ExpectedSkeleton.Reset(); }},
        {TEXT("A"), ECustomizationSlot::Crest, ECustomizationFailure::SkeletonMismatch, [Other](auto& R) { R.ExpectedSkeleton = Other; }},
        {TEXT("A"), ECustomizationSlot::Crest, ECustomizationFailure::MissingMorph, [](auto& R) { R.Morph = TEXT("NoSuchMorph"); }},
        {TEXT("A"), ECustomizationSlot::Crest, ECustomizationFailure::MissingMaterial, [](auto& R) { R.Material.Reset(); }},
        {TEXT("A"), ECustomizationSlot::Crest, ECustomizationFailure::MissingMaterial, [](auto& R) { R.MaterialSlot = 9; }},
        {TEXT("A"), ECustomizationSlot::Crest, ECustomizationFailure::MissingColorParameter, [](auto& R) { R.ColorParameter = TEXT("AbsentColor"); }},
        {TEXT("A"), ECustomizationSlot::Crest, ECustomizationFailure::MissingMask, [](auto& R) { R.bMaskRequired = true; }},
        {TEXT("A"), ECustomizationSlot::Crest, ECustomizationFailure::MissingMaskParameter, [](auto& R) { R.MaskParameter = TEXT("AbsentMask"); }},
        {TEXT("RoundGlasses"), ECustomizationSlot::Eyewear, ECustomizationFailure::MissingMesh, [](auto& R) { R.Mesh.Reset(); }},
        {TEXT("RoundGlasses"), ECustomizationSlot::Eyewear, ECustomizationFailure::SkeletonMismatch, [Other](auto& R) { R.ExpectedSkeleton = Other; }},
        {TEXT("RoundGlasses"), ECustomizationSlot::Eyewear, ECustomizationFailure::MissingMorph, [](auto& R) { R.Morph = TEXT("Shape"); }},
        {TEXT("RoundGlasses"), ECustomizationSlot::Eyewear, ECustomizationFailure::MissingMaterial, [](auto& R) { R.Material.Reset(); }},
        {TEXT("BeakRing"), ECustomizationSlot::Beak, ECustomizationFailure::MissingColorParameter, [](auto& R) { R.ColorParameter = TEXT("AbsentColor"); }},
        {TEXT("BeakRing"), ECustomizationSlot::Beak, ECustomizationFailure::MissingMaterial, [](auto& R) { R.Material.Reset(); }},
    };
    const FCustomizationCatalog Catalog = FCustomizationCatalog::LoadDefault();
    for (const FCase& Case : Cases)
    {
        FCustomizationPartResources Resources = UCustomizationAppearanceComponent::GetDefaultResources(Case.PartId);
        Case.Break(Resources);
        const FString Label = FString::Printf(TEXT("%s: %s"), *Case.PartId.ToString(), *UEnum::GetValueAsString(Case.Expected));

        FCustomizationLoadedPart Loaded;
        TestEqual(*(Label + TEXT(" — 계약 검사")), ValidateCustomizationPartContract(Resources, Case.Slot, Loaded), Case.Expected);

        Target->SetResourcesForTests(Case.PartId, Resources);
        FCustomizationProfile Profile;
        Profile.SetSlotPartId(Case.Slot, Case.PartId);
        TestEqual(*(Label + TEXT(" — 런타임 적용")), Target->TryApply(Profile), Case.Expected);
        TestTrue(*(Label + TEXT(" — 런타임이 실패 슬롯을 안다")), Target->GetLastFailure().bHasSlot && Target->GetLastFailure().Slot == Case.Slot);
        Target->SetResourcesForTests(Case.PartId, UCustomizationAppearanceComponent::GetDefaultResources(Case.PartId));

        FCustomizationPartRow Row = *Catalog.Find(Case.PartId);
        Row.Resources = Resources;
        const TPair<FName, const FCustomizationPartRow*> Entry(Case.PartId, &Row);
        const TArray<FCustomizationCatalogIssue> Issues = ValidateCustomizationCatalogRows(MakeArrayView(&Entry, 1));
        const FCustomizationCatalogIssue* Mine = Issues.FindByPredicate([&Case](const FCustomizationCatalogIssue& Issue) { return Issue.PartId == Case.PartId; });
        TestTrue(*(Label + TEXT(" — 등록 검사도 같은 사유와 슬롯")), Mine && Mine->Detail.Reason == Case.Expected
            && Mine->Detail.bHasSlot && Mine->Detail.Slot == Case.Slot);
    }
    Target->ClearPreview();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegistrationRules, "Duckov.Customization.Catalog.RegistrationRules", Flags)
bool FRegistrationRules::RunTest(const FString& Parameters)
{
    LoadCatalogAssets();
    const FCustomizationCatalog Catalog = FCustomizationCatalog::LoadDefault();
    const FCustomizationPartRow A = *Catalog.Find(TEXT("A"));
    const FCustomizationPartRow B = *Catalog.Find(TEXT("B"));
    const FCustomizationPartRow Glasses = *Catalog.Find(TEXT("RoundGlasses"));
    using FEntry = TPair<FName, const FCustomizationPartRow*>;
    const auto Validate = [](std::initializer_list<FEntry> Entries) { return ValidateCustomizationCatalogRows(MakeArrayView(Entries.begin(), static_cast<int32>(Entries.size()))); };

    TestEqual(TEXT("정상 카탈로그는 문제 없음"), Validate({{TEXT("A"), &A}, {TEXT("B"), &B}, {TEXT("RoundGlasses"), &Glasses}}).Num(), 0);

    // ID 중복(FName은 대소문자를 구분하지 않는다)과 "없음"(None) 예약어 충돌
    TArray<FCustomizationCatalogIssue> Issues = Validate({{TEXT("A"), &A}, {TEXT("a"), &B}});
    TestTrue(TEXT("대소문자만 다른 중복 ID"), Issues.Num() == 1 && Issues[0].Detail.Reason == ECustomizationFailure::DuplicatePartId);
    Issues = Validate({{TEXT("A"), &A}, {NAME_None, &B}});
    TestTrue(TEXT("None은 ID로 쓸 수 없다"), Issues.Num() == 1 && Issues[0].Detail.Reason == ECustomizationFailure::DuplicatePartId);

    // 슬롯과 mesh 종류 선언
    FCustomizationPartRow BadSlot = Glasses; BadSlot.Slot = static_cast<ECustomizationSlot>(99);
    Issues = Validate({{TEXT("A"), &A}, {TEXT("RoundGlasses"), &BadSlot}});
    TestTrue(TEXT("알 수 없는 슬롯 값"), Issues.Num() == 1 && Issues[0].Detail.Reason == ECustomizationFailure::InvalidKind && !Issues[0].Detail.bHasSlot);
    FCustomizationPartRow Static = Glasses; Static.Resources.MeshKind = ECustomizationMeshKind::StaticPlaceholder;
    Issues = Validate({{TEXT("A"), &A}, {TEXT("RoundGlasses"), &Static}});
    TestTrue(TEXT("정식 카탈로그에 static 대역 등록"), Issues.Num() == 1 && Issues[0].Detail.Reason == ECustomizationFailure::InvalidKind
        && Issues[0].Detail.bHasSlot && Issues[0].Detail.Slot == ECustomizationSlot::Eyewear);

    // 기본 Profile(초기화 대상)이 가리키는 슬롯 파츠
    Issues = Validate({{TEXT("B"), &B}, {TEXT("RoundGlasses"), &Glasses}});
    TestTrue(TEXT("기본 머리 장식 A가 없다"), Issues.Num() == 1 && Issues[0].PartId == TEXT("A")
        && Issues[0].Detail.Reason == ECustomizationFailure::UnknownPart && Issues[0].Detail.Slot == ECustomizationSlot::Crest);
    FCustomizationPartRow WrongSlot = A; WrongSlot.Slot = ECustomizationSlot::Eyewear;
    Issues = Validate({{TEXT("A"), &WrongSlot}, {TEXT("B"), &B}});
    TestTrue(TEXT("기본 머리 장식 A가 다른 슬롯에 등록"), Issues.Num() == 1 && Issues[0].Detail.Reason == ECustomizationFailure::SlotMismatch
        && Issues[0].Detail.Slot == ECustomizationSlot::Crest);

    // 전부 아니면 전무: 좋은 row가 섞여 있어도 나쁜 row 하나가 전체를 통과시키지 않는다. 문제는 그 row만 짚는다.
    FCustomizationPartRow BadMesh = B; BadMesh.Resources.Mesh.Reset();
    FCustomizationPartRow BadMaterial = Glasses; BadMaterial.Resources.Material.Reset();
    Issues = Validate({{TEXT("A"), &A}, {TEXT("B"), &BadMesh}, {TEXT("RoundGlasses"), &BadMaterial}});
    TestEqual(TEXT("나쁜 row가 둘이면 문제도 둘"), Issues.Num(), 2);
    TestTrue(TEXT("문제는 각 row의 슬롯과 사유를 짚는다"), Issues.ContainsByPredicate([](const FCustomizationCatalogIssue& Issue)
        { return Issue.PartId == TEXT("B") && Issue.Detail.Reason == ECustomizationFailure::MissingMesh && Issue.Detail.Slot == ECustomizationSlot::Crest; })
        && Issues.ContainsByPredicate([](const FCustomizationCatalogIssue& Issue)
        { return Issue.PartId == TEXT("RoundGlasses") && Issue.Detail.Reason == ECustomizationFailure::MissingMaterial && Issue.Detail.Slot == ECustomizationSlot::Eyewear; }));
    return true;
}
}
#endif
