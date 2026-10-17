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
                && Resources.Material == Row->Resources.Material && Resources.FollowBodyMorphs.Num() == Row->Resources.FollowBodyMorphs.Num());
        }
    }
    TestTrue(TEXT("부위 여섯 개 각각 2개 이상, 모두 12개 이상"), Total >= 12);
    for (const ECustomizationSlot Slot : GetCustomizationSlots())
    {
        TestTrue(*FString::Printf(TEXT("%s 유형 선택지 2개 이상"), *GetCustomizationSlotText(Slot).ToString()), Catalog.GetPartIds(Slot).Num() >= 2);
        // 원작처럼 헤어만 비울 수 있고, 발만 색을 고르지 않는다.
        TestEqual(TEXT("필수 슬롯 규칙"), IsCustomizationSlotRequired(Slot), Slot != ECustomizationSlot::Hair);
        TestEqual(TEXT("색 슬롯 규칙"), IsCustomizationSlotColorable(Slot), Slot != ECustomizationSlot::Foot);
    }
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
    TestTrue(TEXT("적용한 파츠가 12개 이상"), Applied >= 12);
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
    const auto BadFollow = [](FCustomizationPartResources& R)
    {
        FCustomizationMorphFollow Follow;
        Follow.BodyMorph = TEXT("NoSuchMorph");
        R.FollowBodyMorphs = {Follow};
    };
    const FCase Cases[] = {
        {TEXT("HairA"), ECustomizationSlot::Hair, ECustomizationFailure::MissingMesh, [](auto& R) { R.Mesh.Reset(); }},
        {TEXT("HairA"), ECustomizationSlot::Hair, ECustomizationFailure::MissingSkeleton, [](auto& R) { R.ExpectedSkeleton.Reset(); }},
        {TEXT("HairA"), ECustomizationSlot::Hair, ECustomizationFailure::SkeletonMismatch, [Other](auto& R) { R.ExpectedSkeleton = Other; }},
        {TEXT("HairA"), ECustomizationSlot::Hair, ECustomizationFailure::MissingMorph, BadFollow},
        {TEXT("HairA"), ECustomizationSlot::Hair, ECustomizationFailure::MissingMaterial, [](auto& R) { R.Material.Reset(); }},
        {TEXT("HairA"), ECustomizationSlot::Hair, ECustomizationFailure::MissingMaterial, [](auto& R) { R.MaterialSlot = 9; }},
        {TEXT("HairA"), ECustomizationSlot::Hair, ECustomizationFailure::MissingColorParameter, [](auto& R) { R.ColorParameter = TEXT("AbsentColor"); }},
        {TEXT("HairA"), ECustomizationSlot::Hair, ECustomizationFailure::MissingMask, [](auto& R) { R.bMaskRequired = true; }},
        {TEXT("HairA"), ECustomizationSlot::Hair, ECustomizationFailure::MissingMaskParameter, [](auto& R) { R.MaskParameter = TEXT("AbsentMask"); }},
        {TEXT("EyeRound"), ECustomizationSlot::Eye, ECustomizationFailure::MissingMesh, [](auto& R) { R.Mesh.Reset(); }},
        {TEXT("EyeRound"), ECustomizationSlot::Eye, ECustomizationFailure::SkeletonMismatch, [Other](auto& R) { R.ExpectedSkeleton = Other; }},
        {TEXT("EyeRound"), ECustomizationSlot::Eye, ECustomizationFailure::MissingMaterial, [](auto& R) { R.Material.Reset(); }},
        {TEXT("BeakDuck"), ECustomizationSlot::Beak, ECustomizationFailure::MissingColorParameter, [](auto& R) { R.ColorParameter = TEXT("AbsentColor"); }},
        // 색 슬롯 규칙: 색을 고르는 슬롯은 색 parameter가 필요하고, 발은 고정색이라 없어야 한다.
        {TEXT("EyeRound"), ECustomizationSlot::Eye, ECustomizationFailure::MissingColorParameter, [](auto& R) { R.ColorParameter = NAME_None; }},
        {TEXT("FootBoot"), ECustomizationSlot::Foot, ECustomizationFailure::InvalidKind, [](auto& R) { R.ColorParameter = TEXT("Color"); }},
        {TEXT("WingMitten"), ECustomizationSlot::Wing, ECustomizationFailure::MissingMaterial, [](auto& R) { R.Material.Reset(); }},
        {TEXT("TailShort"), ECustomizationSlot::Tail, ECustomizationFailure::MissingMesh, [](auto& R) { R.Mesh.Reset(); }},
        {TEXT("FootBoot"), ECustomizationSlot::Foot, ECustomizationFailure::SkeletonMismatch, [Other](auto& R) { R.ExpectedSkeleton = Other; }},
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
    using FEntry = TPair<FName, const FCustomizationPartRow*>;
    // 기본 Profile의 여섯 파츠와 헤어 B로 된 작은 정상 카탈로그다. Replace로 row를 바꾸고 Remove로 빼며 Extra를 덧붙인다.
    const FName Base[] = {TEXT("EyeRound"), TEXT("HairA"), TEXT("HairB"), TEXT("BeakDuck"), TEXT("WingRound"), TEXT("TailFan"), TEXT("FootWebbed")};
    const auto Validate = [&](const TMap<FName, const FCustomizationPartRow*>& Replace, TArray<FName> Remove, TArray<FEntry> Extra)
    {
        TArray<FEntry> Entries;
        for (const FName PartId : Base)
        {
            if (Remove.Contains(PartId)) { continue; }
            const FCustomizationPartRow* const* Replaced = Replace.Find(PartId);
            Entries.Emplace(PartId, Replaced ? *Replaced : Catalog.Find(PartId));
        }
        Entries.Append(Extra);
        return ValidateCustomizationCatalogRows(Entries);
    };
    const FCustomizationPartRow HairB = *Catalog.Find(TEXT("HairB"));

    TestEqual(TEXT("정상 카탈로그는 문제 없음"), Validate({}, {}, {}).Num(), 0);

    // ID 중복(FName은 대소문자를 구분하지 않는다)과 "없음"(None) 예약어 충돌
    TArray<FCustomizationCatalogIssue> Issues = Validate({}, {}, {{TEXT("hairb"), &HairB}});
    TestTrue(TEXT("대소문자만 다른 중복 ID"), Issues.Num() == 1 && Issues[0].Detail.Reason == ECustomizationFailure::DuplicatePartId);
    Issues = Validate({}, {}, {{NAME_None, &HairB}});
    TestTrue(TEXT("None은 ID로 쓸 수 없다"), Issues.Num() == 1 && Issues[0].Detail.Reason == ECustomizationFailure::DuplicatePartId);

    // 슬롯과 mesh 종류 선언
    FCustomizationPartRow BadSlot = HairB; BadSlot.Slot = static_cast<ECustomizationSlot>(99);
    Issues = Validate({{TEXT("HairB"), &BadSlot}}, {}, {});
    TestTrue(TEXT("알 수 없는 슬롯 값"), Issues.Num() == 1 && Issues[0].Detail.Reason == ECustomizationFailure::InvalidKind && !Issues[0].Detail.bHasSlot);
    FCustomizationPartRow Static = HairB; Static.Resources.MeshKind = ECustomizationMeshKind::StaticPlaceholder;
    Issues = Validate({{TEXT("HairB"), &Static}}, {}, {});
    TestTrue(TEXT("정식 카탈로그에 static 대역 등록"), Issues.Num() == 1 && Issues[0].Detail.Reason == ECustomizationFailure::InvalidKind
        && Issues[0].Detail.bHasSlot && Issues[0].Detail.Slot == ECustomizationSlot::Hair);

    // 기본 Profile(초기화 대상)이 가리키는 슬롯 파츠
    Issues = Validate({}, {TEXT("EyeRound")}, {});
    TestTrue(TEXT("기본 눈 EyeRound가 없다"), Issues.Num() == 1 && Issues[0].PartId == TEXT("EyeRound")
        && Issues[0].Detail.Reason == ECustomizationFailure::UnknownPart && Issues[0].Detail.Slot == ECustomizationSlot::Eye);
    FCustomizationPartRow WrongSlot = *Catalog.Find(TEXT("EyeRound")); WrongSlot.Slot = ECustomizationSlot::Beak;
    Issues = Validate({{TEXT("EyeRound"), &WrongSlot}}, {}, {});
    TestTrue(TEXT("기본 눈 EyeRound가 다른 슬롯에 등록"), Issues.Num() == 1 && Issues[0].Detail.Reason == ECustomizationFailure::SlotMismatch
        && Issues[0].Detail.Slot == ECustomizationSlot::Eye);

    // 전부 아니면 전무: 좋은 row가 섞여 있어도 나쁜 row 하나가 전체를 통과시키지 않는다. 문제는 그 row만 짚는다.
    FCustomizationPartRow BadMesh = HairB; BadMesh.Resources.Mesh.Reset();
    FCustomizationPartRow BadMaterial = *Catalog.Find(TEXT("TailFan")); BadMaterial.Resources.Material.Reset();
    Issues = Validate({{TEXT("HairB"), &BadMesh}, {TEXT("TailFan"), &BadMaterial}}, {}, {});
    TestEqual(TEXT("나쁜 row가 둘이면 문제도 둘"), Issues.Num(), 2);
    TestTrue(TEXT("문제는 각 row의 슬롯과 사유를 짚는다"), Issues.ContainsByPredicate([](const FCustomizationCatalogIssue& Issue)
        { return Issue.PartId == TEXT("HairB") && Issue.Detail.Reason == ECustomizationFailure::MissingMesh && Issue.Detail.Slot == ECustomizationSlot::Hair; })
        && Issues.ContainsByPredicate([](const FCustomizationCatalogIssue& Issue)
        { return Issue.PartId == TEXT("TailFan") && Issue.Detail.Reason == ECustomizationFailure::MissingMaterial && Issue.Detail.Slot == ECustomizationSlot::Tail; }));
    return true;
}
}
#endif
