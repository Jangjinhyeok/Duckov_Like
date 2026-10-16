#pragma once

#include "CoreMinimal.h"
#include "CustomizationTypes.generated.h"

// 외형 슬롯이다. Crest(머리 장식)는 필수이고 Eyewear(눈)·Beak(부리)는 비어 있을 수 있다.
UENUM()
enum class ECustomizationSlot : uint8
{
    Crest,
    Eyewear,
    Beak
};

UENUM()
enum class ECustomizationFailure : uint8
{
    None,
    NotEditing,
    OperationInProgress,
    // 카탈로그 row가 지원하지 않는 슬롯·mesh 종류를 선언했다.
    InvalidKind,
    UnknownPart,
    RequiredPartMissing,
    InvalidNumber,
    HueOutOfRange,
    ShapeOutOfRange,
    EyeSizeOutOfRange,
    BeakLengthOutOfRange,
    BodyLengthOutOfRange,
    UnsupportedBodyAdjustment,
    MissingMesh,
    MissingSkeleton,
    SkeletonMismatch,
    MissingMaterial,
    MissingColorParameter,
    MissingMorph,
    MissingMask,
    MissingMaskParameter,
    PreparationFailed,
    // 파츠가 등록돼 있지만 다른 슬롯의 파츠다.
    SlotMismatch,
    // 카탈로그에서 파츠 ID가 겹치거나 "없음"(None)과 충돌한다.
    DuplicatePartId,
    CatalogUnavailable
};

// 실패 사유와, 슬롯에서 생긴 실패라면 그 슬롯이다. 슬롯과 무관한 실패는 bHasSlot이 false다.
struct DUCKOVLIKE_API FCustomizationFailureDetail
{
    ECustomizationFailure Reason = ECustomizationFailure::None;
    ECustomizationSlot Slot = ECustomizationSlot::Crest;
    bool bHasSlot = false;

    static FCustomizationFailureDetail Of(ECustomizationFailure InReason)
    {
        FCustomizationFailureDetail Result;
        Result.Reason = InReason;
        return Result;
    }
    static FCustomizationFailureDetail Of(ECustomizationFailure InReason, ECustomizationSlot InSlot)
    {
        FCustomizationFailureDetail Result;
        Result.Reason = InReason;
        Result.Slot = InSlot;
        Result.bHasSlot = true;
        return Result;
    }
    bool operator==(const FCustomizationFailureDetail& Other) const
    {
        return Reason == Other.Reason && bHasSlot == Other.bHasSlot && (!bHasSlot || Slot == Other.Slot);
    }
    bool operator!=(const FCustomizationFailureDetail& Other) const { return !(*this == Other); }
};

// 리소스 참조 없이 Model만 소유하는 메모리 외형 값이다.
USTRUCT()
struct DUCKOVLIKE_API FCustomizationProfile
{
    GENERATED_BODY()

    // Crest 슬롯의 파츠 ID다. None은 파츠 없음이며 필수 슬롯에서는 거부한다.
    UPROPERTY()
    FName PartId = TEXT("A");

    // 선택 슬롯이다. None은 파츠 없음이다.
    UPROPERTY()
    FName EyewearId = NAME_None;

    UPROPERTY()
    FName BeakWearId = NAME_None;

    UPROPERTY()
    float Hue = 0.12f;

    UPROPERTY()
    float Shape = 0.0f;

    UPROPERTY()
    float EyeSize = 0.0f;

    UPROPERTY()
    float BeakLength = 0.0f;

    UPROPERTY()
    float BodyLength = 0.0f;

    FName GetSlotPartId(ECustomizationSlot Slot) const
    {
        switch (Slot)
        {
        case ECustomizationSlot::Eyewear: return EyewearId;
        case ECustomizationSlot::Beak: return BeakWearId;
        default: return PartId;
        }
    }
    void SetSlotPartId(ECustomizationSlot Slot, FName Id)
    {
        switch (Slot)
        {
        case ECustomizationSlot::Eyewear: EyewearId = Id; break;
        case ECustomizationSlot::Beak: BeakWearId = Id; break;
        default: PartId = Id; break;
        }
    }

    bool operator==(const FCustomizationProfile& Other) const
    {
        return PartId == Other.PartId && EyewearId == Other.EyewearId && BeakWearId == Other.BeakWearId
            && Hue == Other.Hue && Shape == Other.Shape && EyeSize == Other.EyeSize
            && BeakLength == Other.BeakLength && BodyLength == Other.BodyLength;
    }
    bool operator!=(const FCustomizationProfile& Other) const { return !(*this == Other); }
};

DUCKOVLIKE_API TConstArrayView<ECustomizationSlot> GetCustomizationSlots();
DUCKOVLIKE_API bool IsCustomizationSlotRequired(ECustomizationSlot Slot);
DUCKOVLIKE_API FText GetCustomizationSlotText(ECustomizationSlot Slot);

// 데이터 검사다. 슬롯별 파츠 ID는 카탈로그에 등록된 같은 슬롯의 파츠여야 한다.
// 어느 슬롯에서 실패했는지는 Detail로, 호환용 ECustomizationFailure 반환은 Reason만 돌려준다.
DUCKOVLIKE_API FCustomizationFailureDetail ValidateCustomizationProfileDetail(const FCustomizationProfile& Profile);
DUCKOVLIKE_API ECustomizationFailure ValidateCustomizationProfile(const FCustomizationProfile& Profile);
DUCKOVLIKE_API FText GetCustomizationFailureText(ECustomizationFailure Failure);
// 슬롯 이름을 앞에 붙인 안내 문구다.
DUCKOVLIKE_API FText GetCustomizationFailureText(const FCustomizationFailureDetail& Detail);
DUCKOVLIKE_API FLinearColor GetCustomizationColor(float Hue);
