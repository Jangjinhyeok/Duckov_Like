#pragma once

#include "CoreMinimal.h"
#include "Animation/Skeleton.h"
#include "CustomizationCatalog.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Materials/MaterialInterface.h"
#include "UObject/StrongObjectPtr.h"

// 파츠 리소스 계약 검사다. 런타임 적용 준비(UCustomizationAppearanceComponent)와
// 에디터 등록 검증(카탈로그 validator)이 같은 규칙과 같은 ECustomizationFailure를 쓴다.
// 렌더 자원 초기화·shader 준비처럼 로드한 뒤의 런타임 상태는 여기서 검사하지 않는다.

// 검사를 통과한 리소스다. 적용 준비가 끝날 때까지 강한 참조로 붙든다.
struct DUCKOVLIKE_API FCustomizationLoadedPart
{
    TStrongObjectPtr<UObject> Mesh;
    UStaticMesh* StaticMesh = nullptr;
    USkeletalMesh* SkeletalMesh = nullptr;
    TStrongObjectPtr<USkeleton> Skeleton;
    TStrongObjectPtr<UMaterialInterface> Material;
    // mask를 쓰지 않는 파츠는 비어 있다.
    TStrongObjectPtr<UTexture> Mask;
};

struct DUCKOVLIKE_API FCustomizationLoadedBody
{
    TStrongObjectPtr<USkeletalMesh> Mesh;
    // body 슬롯 0의 material이다. 피부색 MID의 부모다.
    TStrongObjectPtr<UMaterialInterface> Material;
};

// 고정 몸통(몸통+머리) body의 Morph다. 지금은 머리 모양 하나(HeadFlat, weight 1이 가장 납작)다.
DUCKOVLIKE_API TConstArrayView<FName> GetCustomizationBodyMorphs();
DUCKOVLIKE_API const FSoftObjectPath& GetDefaultCustomizationBodyMesh();
// body 피부색 parameter 이름이다.
DUCKOVLIKE_API FName GetCustomizationBodyColorParameter();

// mesh·Skeleton·material·색상 parameter·mask가 계약을 지키는지 검사한다.
DUCKOVLIKE_API ECustomizationFailure ValidateCustomizationPartContract(
    const FCustomizationPartResources& Resources, ECustomizationSlot Slot, FCustomizationLoadedPart& Out);

// 공통 body mesh가 파츠와 같은 Skeleton이고 머리 모양 Morph·피부색 material을 갖췄는지 검사한다.
DUCKOVLIKE_API ECustomizationFailure ValidateCustomizationBodyContract(
    const FSoftObjectPath& BodyMesh, const USkeleton* ExpectedSkeleton, FCustomizationLoadedBody& Out);

// 카탈로그 등록 검사의 결과 한 건이다.
struct DUCKOVLIKE_API FCustomizationCatalogIssue
{
    FName PartId;
    FCustomizationFailureDetail Detail;
};

// 카탈로그의 모든 row를 검사하고 문제를 전부 모아 돌려준다. 비어 있어야 통과다(전부 아니면 전무).
// row 규칙, 파츠 리소스 계약, body와 같은 Skeleton, 기본 Profile이 가리키는 슬롯 파츠의 존재를 본다.
DUCKOVLIKE_API TArray<FCustomizationCatalogIssue> ValidateCustomizationCatalogRows(
    TConstArrayView<TPair<FName, const FCustomizationPartRow*>> Rows);
DUCKOVLIKE_API TArray<FCustomizationCatalogIssue> ValidateCustomizationTable(const UDataTable& Table);
