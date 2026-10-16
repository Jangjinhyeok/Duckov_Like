#include "CustomizationCatalogValidator.h"

#include "CustomizationPartContract.h"
#include "Misc/DataValidation.h"
#include "SkinnedAssetCompiler.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(CustomizationCatalogValidator)

bool UCustomizationCatalogValidator::CanValidateAsset_Implementation(
    const FAssetData& InAssetData, UObject* InObject, FDataValidationContext& InContext) const
{
    const UDataTable* Table = Cast<UDataTable>(InObject);
    return Table && Table->GetRowStruct() == FCustomizationPartRow::StaticStruct();
}

EDataValidationResult UCustomizationCatalogValidator::ValidateLoadedAsset_Implementation(
    const FAssetData& InAssetData, UObject* InAsset, FDataValidationContext& InContext)
{
    // 방금 import한 mesh가 비동기로 컴파일 중이면 검사가 거짓 실패를 내지 않도록 끝까지 기다린다.
    FSkinnedAssetCompilingManager::Get().FinishAllCompilation();
    const TArray<FCustomizationCatalogIssue> Issues = ValidateCustomizationTable(*CastChecked<UDataTable>(InAsset));
    if (Issues.IsEmpty())
    {
        AssetPasses(InAsset);
        return EDataValidationResult::Valid;
    }
    for (const FCustomizationCatalogIssue& Issue : Issues)
    {
        AssetFails(InAsset, FText::Format(FText::FromString(TEXT("파츠 '{0}': {1}")),
            FText::FromName(Issue.PartId), GetCustomizationFailureText(Issue.Detail)));
    }
    return EDataValidationResult::Invalid;
}
