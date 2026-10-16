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
    FName ColorParameter, FName Morph)
{
    FCustomizationPartRow Row;
    Row.Slot = Slot;
    Row.DisplayName = FText::FromString(Display);
    Row.Resources.Mesh = FSoftObjectPath(Mesh);
    Row.Resources.ExpectedSkeleton = FSoftObjectPath(TEXT("/Game/Customization/Prototype/SK_CustomizationBody_Skeleton.SK_CustomizationBody_Skeleton"));
    Row.Resources.Material = FSoftObjectPath(Material);
    Row.Resources.ColorParameter = ColorParameter;
    Row.Resources.Morph = Morph;
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
    const TCHAR* Color = TEXT("/Game/Customization/Prototype/M_CustomizationColor.M_CustomizationColor");
    Table->AddRow(TEXT("A"), MakeRow(ECustomizationSlot::Crest, TEXT("A · 세 갈래 깃"),
        TEXT("/Game/Customization/Prototype/SK_CustomizationPartA.SK_CustomizationPartA"), Color, TEXT("Color"), TEXT("Shape")));
    Table->AddRow(TEXT("B"), MakeRow(ECustomizationSlot::Crest, TEXT("B · 두 갈래 깃"),
        TEXT("/Game/Customization/Prototype/SK_CustomizationPartB.SK_CustomizationPartB"), Color, TEXT("Color"), TEXT("Shape")));
    Table->AddRow(TEXT("RoundGlasses"), MakeRow(ECustomizationSlot::Eyewear, TEXT("둥근 안경"),
        TEXT("/Game/Customization/Prototype/SK_CustomizationRoundGlasses.SK_CustomizationRoundGlasses"),
        TEXT("/Game/Customization/Prototype/M_PrototypeEye.M_PrototypeEye"), NAME_None, NAME_None));
    Table->AddRow(TEXT("BeakRing"), MakeRow(ECustomizationSlot::Beak, TEXT("부리 고리"),
        TEXT("/Game/Customization/Prototype/SK_CustomizationBeakRing.SK_CustomizationBeakRing"), Color, TEXT("Color"), NAME_None));
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
    const TArray<FName> Crests = Catalog.GetPartIds(ECustomizationSlot::Crest);
    TestTrue(TEXT("머리 장식 선택지의 앞 두 개는 A, B"), Crests.Num() >= 2 && Crests[0] == TEXT("A") && Crests[1] == TEXT("B"));
    TestTrue(TEXT("눈 슬롯 선택지"), Catalog.GetPartIds(ECustomizationSlot::Eyewear) == TArray<FName>({TEXT("RoundGlasses")}));
    TestTrue(TEXT("부리 슬롯 선택지"), Catalog.GetPartIds(ECustomizationSlot::Beak) == TArray<FName>({TEXT("BeakRing")}));
    const FCustomizationPartRow* A = Catalog.Find(TEXT("A"));
    if (TestNotNull(TEXT("A row"), A))
    {
        TestTrue(TEXT("A는 skeletal Shape 파츠"), A->Resources.MeshKind == ECustomizationMeshKind::Skeletal
            && A->Resources.Morph == TEXT("Shape") && A->Resources.ColorParameter == TEXT("Color"));
    }
    const FCustomizationPartRow* Glasses = Catalog.Find(TEXT("RoundGlasses"));
    if (TestNotNull(TEXT("안경 row"), Glasses))
    {
        TestTrue(TEXT("안경은 고정색·Morph 없음"), Glasses->Resources.ColorParameter.IsNone() && Glasses->Resources.Morph.IsNone());
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
