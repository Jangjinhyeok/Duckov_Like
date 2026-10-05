#pragma once

#include "CoreMinimal.h"
#include "CustomizationTypes.generated.h"

UENUM()
enum class ECustomizationPartKind : uint8
{
    Part
};

UENUM()
enum class ECustomizationFailure : uint8
{
    None,
    NotEditing,
    OperationInProgress,
    InvalidKind,
    UnknownPart,
    RequiredPartMissing,
    InvalidNumber,
    HueOutOfRange,
    ShapeOutOfRange,
    MissingMesh,
    MissingSkeleton,
    SkeletonMismatch,
    MissingMaterial,
    MissingColorParameter,
    MissingMorph,
    MissingMask,
    MissingMaskParameter,
    PreparationFailed
};

// 리소스 참조 없이 Model만 소유하는 메모리 외형 값이다.
USTRUCT()
struct DUCKOVLIKE_API FCustomizationProfile
{
    GENERATED_BODY()

    UPROPERTY()
    ECustomizationPartKind Kind = ECustomizationPartKind::Part;

    // None은 파츠 없음이며 현재 필수 슬롯에서는 거부한다.
    UPROPERTY()
    FName PartId = TEXT("A");

    UPROPERTY()
    float Hue = 0.12f;

    UPROPERTY()
    float Shape = 0.0f;

    bool operator==(const FCustomizationProfile& Other) const
    {
        return Kind == Other.Kind && PartId == Other.PartId && Hue == Other.Hue && Shape == Other.Shape;
    }
    bool operator!=(const FCustomizationProfile& Other) const { return !(*this == Other); }
};

DUCKOVLIKE_API ECustomizationFailure ValidateCustomizationProfile(const FCustomizationProfile& Profile);
DUCKOVLIKE_API FText GetCustomizationFailureText(ECustomizationFailure Failure);
DUCKOVLIKE_API FLinearColor GetCustomizationColor(float Hue);
