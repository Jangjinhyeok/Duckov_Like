#pragma once

#include "CoreMinimal.h"
#include "EditorValidatorBase.h"
#include "CustomizationCatalogValidator.generated.h"

// 파츠 카탈로그 DataTable을 저장·검증할 때 잘못된 등록을 잡는다.
// 규칙은 런타임 적용 준비와 같은 ValidateCustomizationTable이며 이 클래스는 결과를 에디터 검증 UI로 옮기기만 한다.
// 하나라도 문제가 있으면 Invalid를 돌려주고 통과로 표시하지 않는다.
UCLASS()
class UCustomizationCatalogValidator : public UEditorValidatorBase
{
    GENERATED_BODY()

protected:
    virtual bool CanValidateAsset_Implementation(const FAssetData& InAssetData, UObject* InObject, FDataValidationContext& InContext) const override;
    virtual EDataValidationResult ValidateLoadedAsset_Implementation(const FAssetData& InAssetData, UObject* InAsset, FDataValidationContext& InContext) override;
};
