#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "CustomizationCatalog.h"
#include "CustomizationPartContract.h"
#include "EditorFramework/AssetImportData.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace CustomizationCatalogAssetsTests
{
constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
const TCHAR* TablePackage = TEXT("/Game/Customization/DT_CustomizationParts");

FString SourcePath() { return FPaths::ProjectContentDir() / TEXT("Customization/DT_CustomizationParts.json"); }

bool SaveAsset(UObject* Asset)
{
    UPackage* Package = Asset->GetOutermost();
    Package->MarkPackageDirty();
    const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
    FSavePackageArgs Args;
    Args.TopLevelFlags = RF_Public | RF_Standalone;
    return UPackage::SavePackage(Package, Asset, *Filename, Args);
}

FCustomizationPartRow MakeRow(ECustomizationSlot Slot, const TCHAR* Display, const TCHAR* Mesh, const TCHAR* Material,
    FName ColorParameter)
{
    FCustomizationPartRow Row;
    Row.Slot = Slot;
    Row.DisplayName = FText::FromString(Display);
    Row.Resources.Mesh = FSoftObjectPath(Mesh);
    Row.Resources.ExpectedSkeleton = FSoftObjectPath(TEXT("/Game/Customization/Prototype/SK_CustomizationBody_Skeleton.SK_CustomizationBody_Skeleton"));
    Row.Resources.Material = FSoftObjectPath(Material);
    Row.Resources.ColorParameter = ColorParameter;
    return Row;
}

// DataTable asset과 JSON 원본이 같은 내용인지 본다. 편집 원본은 JSON이고 asset은 그 import 결과다.
// FText의 localization key는 import마다 달라지므로 표시 문자열로 비교한다.
bool MatchesSource(UDataTable* Table, FString& Problem)
{
    FString Json;
    if (!FFileHelper::LoadFileToString(Json, *SourcePath())) { Problem = TEXT("JSON 원본을 읽을 수 없다"); return false; }
    UDataTable* Parsed = NewObject<UDataTable>(GetTransientPackage());
    Parsed->RowStruct = FCustomizationPartRow::StaticStruct();
    const TArray<FString> Problems = Parsed->CreateTableFromJSONString(Json);
    if (!Problems.IsEmpty()) { Problem = FString::Join(Problems, TEXT(" / ")); return false; }
    if (Parsed->GetRowNames() != Table->GetRowNames()) { Problem = TEXT("row 이름이나 순서가 JSON 원본과 다르다"); return false; }
    for (const FName RowName : Table->GetRowNames())
    {
        const auto* Source = Parsed->FindRow<FCustomizationPartRow>(RowName, TEXT("JSON"), false);
        const auto* Asset = Table->FindRow<FCustomizationPartRow>(RowName, TEXT("asset"), false);
        if (!Source || !Asset || Source->Slot != Asset->Slot || Source->DisplayName.ToString() != Asset->DisplayName.ToString()
            || !FCustomizationPartResources::StaticStruct()->CompareScriptStruct(&Source->Resources, &Asset->Resources, PPF_None))
        {
            Problem = FString::Printf(TEXT("%s row가 JSON 원본과 다르다"), *RowName.ToString());
            return false;
        }
    }
    return true;
}

// 명시적으로 한 번 실행하는 생성 경로다. 이미 있으면 덮어쓰지 않는다. JSON 원본도 같이 만든다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCreateCustomizationCatalog, "CustomizationCatalogAssets.Create", Flags)
bool FCreateCustomizationCatalog::RunTest(const FString& Parameters)
{
    if (FPackageName::DoesPackageExist(TablePackage) || FPaths::FileExists(SourcePath()))
    {
        AddError(TEXT("기존 카탈로그를 덮어쓰지 않습니다."));
        return false;
    }
    UPackage* Package = CreatePackage(TablePackage);
    UDataTable* Table = NewObject<UDataTable>(Package, TEXT("DT_CustomizationParts"), RF_Public | RF_Standalone);
    Table->RowStruct = FCustomizationPartRow::StaticStruct();
    // 기본 Profile이 가리키는 부위 여섯 개만 만든다. 나머지 유형은 JSON에 row를 적고 ImportJson으로 등록한다.
    const TCHAR* Color = TEXT("/Game/Customization/Prototype/M_CustomizationColor.M_CustomizationColor");
    const auto Path = [](const TCHAR* Name) { return FString::Printf(TEXT("/Game/Customization/Prototype/%s.%s"), Name, Name); };
    Table->AddRow(TEXT("EyeRound"), MakeRow(ECustomizationSlot::Eye, TEXT("둥근 눈"), *Path(TEXT("SK_CustomizationEyeRound")), Color, TEXT("Color")));
    Table->AddRow(TEXT("HairA"), MakeRow(ECustomizationSlot::Hair, TEXT("세 갈래 깃"), *Path(TEXT("SK_CustomizationPartA")), Color, TEXT("Color")));
    Table->AddRow(TEXT("BeakDuck"), MakeRow(ECustomizationSlot::Beak, TEXT("오리 부리"), *Path(TEXT("SK_CustomizationBeakDuck")), Color, TEXT("Color")));
    Table->AddRow(TEXT("WingRound"), MakeRow(ECustomizationSlot::Wing, TEXT("둥근 날개"), *Path(TEXT("SK_CustomizationWingRound")), Color, TEXT("Color")));
    Table->AddRow(TEXT("TailFan"), MakeRow(ECustomizationSlot::Tail, TEXT("부채 꼬리"), *Path(TEXT("SK_CustomizationTailFan")), Color, TEXT("Color")));
    Table->AddRow(TEXT("FootWebbed"), MakeRow(ECustomizationSlot::Foot, TEXT("오리발"), *Path(TEXT("SK_CustomizationFootWebbed")),
        *Path(TEXT("M_PrototypeBeak")), NAME_None));
    if (!FFileHelper::SaveStringToFile(Table->GetTableAsJSON(), *SourcePath(), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        AddError(TEXT("JSON 원본 저장 실패"));
        return false;
    }
    if (Table->AssetImportData) { Table->AssetImportData->Update(SourcePath()); }
    if (!SaveAsset(Table)) { AddError(TEXT("DataTable 저장 실패")); return false; }
    return true;
}

// 명시적으로 실행하는 등록 경로다. JSON 원본을 DataTable asset에 다시 import한다(Content Browser의 Reimport와 같은 일).
// 파츠를 추가할 때 사람이 하는 일은 에셋을 만들고 JSON에 row를 적는 것이고 코드는 고치지 않는다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FImportCustomizationCatalog, "CustomizationCatalogAssets.ImportJson", Flags)
bool FImportCustomizationCatalog::RunTest(const FString& Parameters)
{
    UDataTable* Table = LoadObject<UDataTable>(nullptr, FCustomizationCatalog::GetDefaultTablePath());
    FString Json;
    if (!TestNotNull(TEXT("기존 카탈로그"), Table) || !TestTrue(TEXT("JSON 원본"), FFileHelper::LoadFileToString(Json, *SourcePath()))) { return false; }
    // 운영 table을 건드리기 전에 transient 복사본에서 import와 검증을 먼저 끝낸다.
    UDataTable* Trial = NewObject<UDataTable>(GetTransientPackage());
    Trial->RowStruct = FCustomizationPartRow::StaticStruct();
    const TArray<FString> Problems = Trial->CreateTableFromJSONString(Json);
    for (const FString& Problem : Problems) { AddError(Problem); }
    if (!Problems.IsEmpty()) { return false; }
    const TArray<FCustomizationCatalogIssue> Issues = ValidateCustomizationTable(*Trial);
    if (!TestEqual(TEXT("import할 카탈로그 검증 문제 수"), Issues.Num(), 0)) { return false; }
    Table->CreateTableFromJSONString(Json);
    return TestTrue(TEXT("카탈로그 저장"), SaveAsset(Table));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVerifyCustomizationCatalog, "CustomizationCatalogAssets.Verify", Flags)
bool FVerifyCustomizationCatalog::RunTest(const FString& Parameters)
{
    UDataTable* Table = LoadObject<UDataTable>(nullptr, FCustomizationCatalog::GetDefaultTablePath());
    if (!TestNotNull(TEXT("카탈로그 DataTable"), Table)) { return false; }
    TestTrue(TEXT("row struct"), Table->GetRowStruct() == FCustomizationPartRow::StaticStruct());
    const FCustomizationCatalog Catalog(Table);
    // 원작 구성: 부위 여섯 개에 각각 2~3개 유형. 헤어는 이전 머리 장식 깃 A/B/C를 쓴다.
    TestTrue(TEXT("눈 유형"), Catalog.GetPartIds(ECustomizationSlot::Eye) == TArray<FName>({TEXT("EyeRound"), TEXT("EyeLarge"), TEXT("EyeSleepy")}));
    TestTrue(TEXT("헤어 유형"), Catalog.GetPartIds(ECustomizationSlot::Hair) == TArray<FName>({TEXT("HairA"), TEXT("HairB"), TEXT("HairC")}));
    TestTrue(TEXT("부리 유형"), Catalog.GetPartIds(ECustomizationSlot::Beak) == TArray<FName>({TEXT("BeakDuck"), TEXT("BeakSmall"), TEXT("BeakLong")}));
    TestTrue(TEXT("날개 유형"), Catalog.GetPartIds(ECustomizationSlot::Wing) == TArray<FName>({TEXT("WingRound"), TEXT("WingSmall"), TEXT("WingMitten")}));
    TestTrue(TEXT("꼬리 유형"), Catalog.GetPartIds(ECustomizationSlot::Tail) == TArray<FName>({TEXT("TailShort"), TEXT("TailFan")}));
    TestTrue(TEXT("발 유형"), Catalog.GetPartIds(ECustomizationSlot::Foot) == TArray<FName>({TEXT("FootWebbed"), TEXT("FootBoot")}));
    for (const ECustomizationSlot Slot : GetCustomizationSlots())
    {
        for (const FName PartId : Catalog.GetPartIds(Slot))
        {
            const FCustomizationPartRow* Row = Catalog.Find(PartId);
            TestTrue(*FString::Printf(TEXT("%s: skeletal, 색 슬롯만 Color parameter"), *PartId.ToString()), Row
                && Row->Resources.MeshKind == ECustomizationMeshKind::Skeletal
                && Row->Resources.ColorParameter.IsNone() == !IsCustomizationSlotColorable(Slot));
            // 머리에 붙은 헤어·눈만 머리 모양 Morph를 따른다.
            const bool bOnHead = Slot == ECustomizationSlot::Hair || Slot == ECustomizationSlot::Eye;
            TestTrue(*FString::Printf(TEXT("%s: 머리 모양 추종은 머리 파츠만"), *PartId.ToString()), Row
                && (Row->Resources.FollowBodyMorphs.Num() == 1) == bOnHead
                && (!bOnHead || Row->Resources.FollowBodyMorphs[0].BodyMorph == TEXT("HeadFlat")));
        }
    }
    const TArray<FCustomizationCatalogIssue> Issues = ValidateCustomizationTable(*Table);
    for (const FCustomizationCatalogIssue& Issue : Issues)
    {
        AddError(FString::Printf(TEXT("카탈로그 검증 실패 %s: %s"), *Issue.PartId.ToString(), *GetCustomizationFailureText(Issue.Detail).ToString()));
    }
    FString Problem;
    const bool bMatches = MatchesSource(Table, Problem);
    TestTrue(*FString::Printf(TEXT("DataTable asset과 JSON 원본 일치 (%s)"), *Problem), bMatches);
    return !HasAnyErrors();
}
}
#endif
