#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "CustomizationCatalogValidator.h"
#include "CustomizationTestSupport.h"
#include "DataValidationSettings.h"
#include "Editor.h"
#include "EditorValidatorSubsystem.h"
#include "Misc/DataValidation.h"
#include "UObject/Package.h"

namespace CustomizationCatalogValidatorTests
{
constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

using FRowEdit = TFunction<void(FCustomizationPartRow&)>;

// 실제 카탈로그의 row를 복사한 transient DataTable이다. 저장하지 않으므로 production 카탈로그는 바뀌지 않는다.
UDataTable* MakeTable(const TMap<FName, FRowEdit>& Edits, TArray<FName> Drop = {})
{
    const FCustomizationCatalog Catalog = FCustomizationCatalog::LoadDefault();
    UDataTable* Table = NewObject<UDataTable>(GetTransientPackage());
    Table->RowStruct = FCustomizationPartRow::StaticStruct();
    for (const ECustomizationSlot Slot : GetCustomizationSlots())
    {
        for (const FName PartId : Catalog.GetPartIds(Slot))
        {
            if (Drop.Contains(PartId)) { continue; }
            FCustomizationPartRow Row = *Catalog.Find(PartId);
            if (const FRowEdit* Edit = Edits.Find(PartId)) { (*Edit)(Row); }
            Table->AddRow(PartId, Row);
        }
    }
    return Table;
}

struct FRun
{
    EDataValidationResult Result = EDataValidationResult::NotValidated;
    TArray<FString> Errors;
};

FRun Validate(UDataTable* Table)
{
    TStrongObjectPtr<UCustomizationCatalogValidator> Validator{NewObject<UCustomizationCatalogValidator>(GetTransientPackage())};
    FDataValidationContext Context(false, EDataValidationUsecase::Script, {});
    FRun Run;
    Run.Result = Validator->ValidateLoadedAsset(FAssetData(Table), Table, Context);
    for (const FDataValidationContext::FIssue& Issue : Context.GetIssues())
    {
        if (Issue.Severity == EMessageSeverity::Error) { Run.Errors.Add(Issue.Message.ToString()); }
    }
    // 마지막 단계에서 모은 오류가 Context에 옮겨졌는지(검증 UI가 보는 경로)도 같이 확인한다.
    return Run;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatalogValidator, "Duckov.Customization.Catalog.EditorValidatorRejectsBadRegistration", Flags)
bool FCatalogValidator::RunTest(const FString& Parameters)
{
    CustomizationTestSupport::LoadCatalogAssets();
    UDataTable* Real = LoadObject<UDataTable>(nullptr, FCustomizationCatalog::GetDefaultTablePath());
    if (!TestNotNull(TEXT("카탈로그 asset"), Real)) { return false; }

    const FRun Good = Validate(Real);
    TestTrue(TEXT("정상 카탈로그 asset은 통과"), Good.Result == EDataValidationResult::Valid && Good.Errors.IsEmpty());
    const FRun Copy = Validate(MakeTable({}));
    TestTrue(TEXT("정상 row의 복사본도 통과"), Copy.Result == EDataValidationResult::Valid && Copy.Errors.IsEmpty());

    UDataTable* Unrelated = NewObject<UDataTable>(GetTransientPackage());
    Unrelated->RowStruct = FTableRowBase::StaticStruct();
    TestTrue(TEXT("카탈로그가 아닌 DataTable은 검사 대상이 아니다"), Validate(Unrelated).Result == EDataValidationResult::NotValidated);

    const FSoftObjectPath OtherSkeleton(TEXT("/Game/Customization/Tests/SKEL_Unexpected.SKEL_Unexpected"));
    struct FCase
    {
        const TCHAR* Name;
        FName PartId;
        FRowEdit Edit;
        TArray<FName> Drop;
        ECustomizationFailure Expected;
        ECustomizationSlot Slot;
        // 슬롯 값 자체가 틀린 row는 어느 슬롯인지 알 수 없어 사유만 나온다.
        bool bSlotKnown = true;
    };
    const FCase Cases[] = {
        {TEXT("다른 Skeleton"), TEXT("HairA"), [OtherSkeleton](auto& R) { R.Resources.ExpectedSkeleton = OtherSkeleton; }, {}, ECustomizationFailure::SkeletonMismatch, ECustomizationSlot::Hair},
        {TEXT("mesh 누락"), TEXT("HairB"), [](auto& R) { R.Resources.Mesh.Reset(); }, {}, ECustomizationFailure::MissingMesh, ECustomizationSlot::Hair},
        {TEXT("material 누락"), TEXT("EyeLarge"), [](auto& R) { R.Resources.Material.Reset(); }, {}, ECustomizationFailure::MissingMaterial, ECustomizationSlot::Eye},
        {TEXT("필요한 mask 누락"), TEXT("HairB"), [](auto& R) { R.Resources.bMaskRequired = true; }, {}, ECustomizationFailure::MissingMask, ECustomizationSlot::Hair},
        {TEXT("mask parameter 누락"), TEXT("HairB"), [](auto& R) { R.Resources.MaskParameter = TEXT("AbsentMask"); }, {}, ECustomizationFailure::MissingMaskParameter, ECustomizationSlot::Hair},
        {TEXT("추종 body Morph 이름 오류"), TEXT("BeakLong"), [](auto& R)
            {
                FCustomizationMorphFollow Follow;
                Follow.BodyMorph = TEXT("NoSuchBodyMorph");
                R.Resources.FollowBodyMorphs.Add(Follow);
            }, {}, ECustomizationFailure::MissingMorph, ECustomizationSlot::Beak},
        {TEXT("추종 이동량 무한대"), TEXT("HairA"), [](auto& R) { R.Resources.FollowBodyMorphs[0].Offset.Z = std::numeric_limits<double>::infinity(); },
            {}, ECustomizationFailure::InvalidNumber, ECustomizationSlot::Hair},
        {TEXT("색상 parameter 누락"), TEXT("BeakLong"), [](auto& R) { R.Resources.ColorParameter = TEXT("AbsentColor"); }, {}, ECustomizationFailure::MissingColorParameter, ECustomizationSlot::Beak},
        {TEXT("색 슬롯인데 색상 parameter 없음"), TEXT("TailShort"), [](auto& R) { R.Resources.ColorParameter = NAME_None; }, {}, ECustomizationFailure::MissingColorParameter, ECustomizationSlot::Tail},
        {TEXT("발에 색상 parameter 등록"), TEXT("FootBoot"), [](auto& R) { R.Resources.ColorParameter = TEXT("Color"); }, {}, ECustomizationFailure::InvalidKind, ECustomizationSlot::Foot},
        {TEXT("슬롯 불일치(기본 눈이 다른 슬롯)"), TEXT("EyeRound"), [](auto& R) { R.Slot = ECustomizationSlot::Beak; }, {}, ECustomizationFailure::SlotMismatch, ECustomizationSlot::Eye},
        {TEXT("슬롯 값 오류"), TEXT("EyeLarge"), [](auto& R) { R.Slot = static_cast<ECustomizationSlot>(99); }, {}, ECustomizationFailure::InvalidKind, ECustomizationSlot::Eye, false},
        {TEXT("static 대역 등록"), TEXT("EyeLarge"), [](auto& R) { R.Resources.MeshKind = ECustomizationMeshKind::StaticPlaceholder; }, {}, ECustomizationFailure::InvalidKind, ECustomizationSlot::Eye},
        {TEXT("기본 파츠 삭제"), NAME_None, nullptr, {TEXT("EyeRound")}, ECustomizationFailure::UnknownPart, ECustomizationSlot::Eye},
    };
    for (const FCase& Case : Cases)
    {
        TMap<FName, FRowEdit> Edits;
        if (Case.Edit) { Edits.Add(Case.PartId, Case.Edit); }
        const FRun Bad = Validate(MakeTable(Edits, Case.Drop));
        TestTrue(*FString::Printf(TEXT("%s: 통과로 표시하지 않는다"), Case.Name), Bad.Result == EDataValidationResult::Invalid);
        const FCustomizationFailureDetail Detail = Case.bSlotKnown ? FCustomizationFailureDetail::Of(Case.Expected, Case.Slot)
            : FCustomizationFailureDetail::Of(Case.Expected);
        const FString Reason = GetCustomizationFailureText(Detail).ToString();
        TestEqual(*FString::Printf(TEXT("%s: 문제는 한 건"), Case.Name), Bad.Errors.Num(), 1);
        TestTrue(*FString::Printf(TEXT("%s: 런타임과 같은 사유 문구 (%s)"), Case.Name, *Reason),
            Bad.Errors.Num() == 1 && Bad.Errors[0].Contains(Reason));
    }

    // 전부 아니면 전무: 나쁜 row가 둘이면 둘 다 보고하고 하나라도 있으면 통과하지 못한다. 고치면 통과한다.
    TMap<FName, FRowEdit> Two;
    Two.Add(TEXT("HairB"), [](auto& R) { R.Resources.Mesh.Reset(); });
    Two.Add(TEXT("BeakLong"), [](auto& R) { R.Resources.Material.Reset(); });
    const FRun Both = Validate(MakeTable(Two));
    TestTrue(TEXT("나쁜 row 둘: 통과하지 못한다"), Both.Result == EDataValidationResult::Invalid);
    TestTrue(TEXT("나쁜 row 둘: 둘 다 보고"), Both.Errors.Num() == 2 && Both.Errors.ContainsByPredicate([](const FString& E) { return E.Contains(TEXT("'HairB'")); })
        && Both.Errors.ContainsByPredicate([](const FString& E) { return E.Contains(TEXT("'BeakLong'")); }));
    Two.Remove(TEXT("HairB"));
    TestTrue(TEXT("하나만 고쳐도 통과하지 못한다"), Validate(MakeTable(Two)).Result == EDataValidationResult::Invalid);
    Two.Reset();
    TestTrue(TEXT("모두 고치면 통과"), Validate(MakeTable(Two)).Result == EDataValidationResult::Valid);

    // 에디터 검증 subsystem에 등록돼 저장·검증 때 자동으로 호출되는지 본다.
    TestTrue(TEXT("저장 시 검증 설정이 켜져 있다"), GetDefault<UDataValidationSettings>()->bValidateOnSave);
    UEditorValidatorSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UEditorValidatorSubsystem>() : nullptr;
    if (TestNotNull(TEXT("에디터 검증 subsystem"), Subsystem))
    {
        bool bRegistered = false;
        Subsystem->ForEachEnabledValidator([&bRegistered](UEditorValidatorBase* Validator)
        {
            bRegistered |= Validator->IsA<UCustomizationCatalogValidator>();
            return !bRegistered;
        });
        TestTrue(TEXT("카탈로그 validator가 subsystem에 등록됨"), bRegistered);
        TMap<FName, FRowEdit> Broken;
        Broken.Add(TEXT("HairA"), [](auto& R) { R.Resources.Mesh.Reset(); });
        FDataValidationContext BadContext(false, EDataValidationUsecase::Save, {});
        TestEqual(TEXT("subsystem 경로: 잘못된 카탈로그"), Subsystem->IsObjectValidWithContext(MakeTable(Broken), BadContext), EDataValidationResult::Invalid);
        TestTrue(TEXT("subsystem 경로: 오류가 Context에 담긴다"), BadContext.GetNumErrors() >= 1);
        FDataValidationContext GoodContext(false, EDataValidationUsecase::Save, {});
        TestEqual(TEXT("subsystem 경로: 정상 카탈로그"), Subsystem->IsObjectValidWithContext(Real, GoodContext), EDataValidationResult::Valid);
        TestEqual(TEXT("subsystem 경로: 정상이면 오류 없음"), GoodContext.GetNumErrors(), 0u);
    }
    return !HasAnyErrors();
}
}
#endif
