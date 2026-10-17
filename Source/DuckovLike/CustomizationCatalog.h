#pragma once

#include "CoreMinimal.h"
#include "CustomizationTypes.h"
#include "Engine/DataTable.h"
#include "UObject/StrongObjectPtr.h"
#include "CustomizationCatalog.generated.h"

UENUM()
enum class ECustomizationMeshKind : uint8
{
    // 엔진 기본 도형을 쓰는 자동 검사 전용 표시다. 정식 카탈로그에는 등록하지 않는다.
    StaticPlaceholder,
    Skeletal
};

// body Morph가 weight 1일 때 파츠를 옮길 거리(cm, component 공간)다. Morph는 정점만 움직이고 bone은 그대로라
// 같은 Skeleton에 붙은 파츠(머리 위 헤어 등)는 따라가지 않는다. 적용 위치는 현재 weight × Offset의 합이다(Morph와 같은 선형 보간).
USTRUCT()
struct DUCKOVLIKE_API FCustomizationMorphFollow
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere)
    FName BodyMorph;

    UPROPERTY(EditAnywhere)
    FVector Offset = FVector::ZeroVector;
};

// Profile과 별개인 적용 리소스 계약이다. 카탈로그 row와 자동 검사 대역이 같은 구조를 쓴다.
USTRUCT()
struct DUCKOVLIKE_API FCustomizationPartResources
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere)
    ECustomizationMeshKind MeshKind = ECustomizationMeshKind::StaticPlaceholder;

    UPROPERTY(EditAnywhere, meta=(AllowedClasses="/Script/Engine.SkeletalMesh,/Script/Engine.StaticMesh"))
    FSoftObjectPath Mesh;

    // 파츠 mesh가 가져야 하는 Skeleton이다. body와 같은 공통 Skeleton이어야 한다.
    UPROPERTY(EditAnywhere, meta=(AllowedClasses="/Script/Engine.Skeleton"))
    FSoftObjectPath ExpectedSkeleton;

    UPROPERTY(EditAnywhere, meta=(AllowedClasses="/Script/Engine.MaterialInterface"))
    FSoftObjectPath Material;

    UPROPERTY(EditAnywhere)
    int32 MaterialSlot = 0;

    // None이면 팔레트 색을 받지 않는 고정색 파츠다(발).
    UPROPERTY(EditAnywhere)
    FName ColorParameter = TEXT("Color");

    UPROPERTY(EditAnywhere)
    FName MaskParameter = NAME_None;

    UPROPERTY(EditAnywhere, meta=(AllowedClasses="/Script/Engine.Texture"))
    FSoftObjectPath Mask;

    UPROPERTY(EditAnywhere)
    bool bMaskRequired = false;

    // 이 파츠가 따라갈 body Morph다. 비어 있으면 body Morph와 무관하게 원점에 둔다.
    UPROPERTY(EditAnywhere)
    TArray<FCustomizationMorphFollow> FollowBodyMorphs;
};

// 파츠 카탈로그의 한 행이다. RowName이 파츠 ID(Profile이 참조하는 값)다.
USTRUCT()
struct DUCKOVLIKE_API FCustomizationPartRow : public FTableRowBase
{
    GENERATED_BODY()

    FCustomizationPartRow() { Resources.MeshKind = ECustomizationMeshKind::Skeletal; }

    UPROPERTY(EditAnywhere)
    ECustomizationSlot Slot = ECustomizationSlot::Eye;

    UPROPERTY(EditAnywhere)
    FText DisplayName;

    UPROPERTY(EditAnywhere)
    FCustomizationPartResources Resources;
};

// 카탈로그 DataTable의 읽기 전용 조회 창구다. 조회하는 동안 table을 살려 둔다.
class DUCKOVLIKE_API FCustomizationCatalog
{
public:
    static const TCHAR* GetDefaultTablePath();
    // 기본 DataTable을 불러온다. 없거나 row 구조가 다르면 IsAvailable()이 false다.
    static FCustomizationCatalog LoadDefault();

    FCustomizationCatalog() = default;
    explicit FCustomizationCatalog(UDataTable* InTable);

    bool IsAvailable() const { return Table.IsValid(); }
    const FCustomizationPartRow* Find(FName PartId) const;
    // 슬롯의 파츠 ID를 table의 row 순서대로 돌려준다. UI 선택지 순서다.
    TArray<FName> GetPartIds(ECustomizationSlot Slot) const;

private:
    TStrongObjectPtr<UDataTable> Table;
};
