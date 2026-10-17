#pragma once

#include "CoreMinimal.h"
#include "CustomizationTypes.generated.h"

// 원작처럼 몸통은 고정이고 부위마다 유형을 고른다. 헤어만 비어 있을 수 있다.
// enum 순서가 슬롯 배열의 index이자 화면 탭 순서다.
UENUM()
enum class ECustomizationSlot : uint8
{
    Eye,
    Hair,
    Beak,
    Wing,
    Tail,
    Foot
};

constexpr int32 CustomizationSlotCount = 6;

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
    // 팔레트에 없는 색 index다.
    ColorOutOfRange,
    HeadShapeOutOfRange,
    // body가 없는 시험 표시에서 머리 모양을 바꾸려 했다.
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
    ECustomizationSlot Slot = ECustomizationSlot::Eye;
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

// 리소스 참조 없이 Model만 소유하는 메모리 외형 값이다. 색은 고정 팔레트의 index다.
USTRUCT()
struct DUCKOVLIKE_API FCustomizationProfile
{
    GENERATED_BODY()

    // 슬롯별 파츠 ID다. None은 파츠 없음이며 헤어 외의 슬롯에서는 거부한다.
    UPROPERTY()
    FName EyeId = TEXT("EyeRound");

    UPROPERTY()
    FName HairId = TEXT("HairA");

    UPROPERTY()
    FName BeakId = TEXT("BeakDuck");

    UPROPERTY()
    FName WingId = TEXT("WingRound");

    UPROPERTY()
    FName TailId = TEXT("TailFan");

    UPROPERTY()
    FName FootId = TEXT("FootWebbed");

    // 머리와 몸통의 색이다.
    UPROPERTY()
    int32 SkinColor = 0;

    // 부위 색이다. 발은 유형마다 색이 정해져 있어 색 값이 없다.
    UPROPERTY()
    int32 EyeColor = 3;

    UPROPERTY()
    int32 HairColor = 4;

    UPROPERTY()
    int32 BeakColor = 5;

    UPROPERTY()
    int32 WingColor = 0;

    UPROPERTY()
    int32 TailColor = 0;

    // 머리 모양이다. 1이 기본(둥근 머리), 0이 가장 납작한 머리다.
    UPROPERTY()
    float HeadShape = 1.0f;

    FName GetSlotPartId(ECustomizationSlot Slot) const
    {
        switch (Slot)
        {
        case ECustomizationSlot::Hair: return HairId;
        case ECustomizationSlot::Beak: return BeakId;
        case ECustomizationSlot::Wing: return WingId;
        case ECustomizationSlot::Tail: return TailId;
        case ECustomizationSlot::Foot: return FootId;
        default: return EyeId;
        }
    }
    void SetSlotPartId(ECustomizationSlot Slot, FName Id)
    {
        switch (Slot)
        {
        case ECustomizationSlot::Hair: HairId = Id; break;
        case ECustomizationSlot::Beak: BeakId = Id; break;
        case ECustomizationSlot::Wing: WingId = Id; break;
        case ECustomizationSlot::Tail: TailId = Id; break;
        case ECustomizationSlot::Foot: FootId = Id; break;
        default: EyeId = Id; break;
        }
    }
    // 색이 없는 발은 INDEX_NONE이다.
    int32 GetSlotColor(ECustomizationSlot Slot) const
    {
        switch (Slot)
        {
        case ECustomizationSlot::Eye: return EyeColor;
        case ECustomizationSlot::Hair: return HairColor;
        case ECustomizationSlot::Beak: return BeakColor;
        case ECustomizationSlot::Wing: return WingColor;
        case ECustomizationSlot::Tail: return TailColor;
        default: return INDEX_NONE;
        }
    }
    void SetSlotColor(ECustomizationSlot Slot, int32 Index)
    {
        switch (Slot)
        {
        case ECustomizationSlot::Eye: EyeColor = Index; break;
        case ECustomizationSlot::Hair: HairColor = Index; break;
        case ECustomizationSlot::Beak: BeakColor = Index; break;
        case ECustomizationSlot::Wing: WingColor = Index; break;
        case ECustomizationSlot::Tail: TailColor = Index; break;
        default: break;
        }
    }

    bool operator==(const FCustomizationProfile& Other) const
    {
        return EyeId == Other.EyeId && HairId == Other.HairId && BeakId == Other.BeakId && WingId == Other.WingId
            && TailId == Other.TailId && FootId == Other.FootId && SkinColor == Other.SkinColor
            && EyeColor == Other.EyeColor && HairColor == Other.HairColor && BeakColor == Other.BeakColor
            && WingColor == Other.WingColor && TailColor == Other.TailColor && HeadShape == Other.HeadShape;
    }
    bool operator!=(const FCustomizationProfile& Other) const { return !(*this == Other); }
};

DUCKOVLIKE_API TConstArrayView<ECustomizationSlot> GetCustomizationSlots();
DUCKOVLIKE_API bool IsCustomizationSlotRequired(ECustomizationSlot Slot);
// 발 외의 슬롯은 팔레트 색을 고른다.
DUCKOVLIKE_API bool IsCustomizationSlotColorable(ECustomizationSlot Slot);
DUCKOVLIKE_API FText GetCustomizationSlotText(ECustomizationSlot Slot);

// 고정 색 팔레트다. Profile의 색 값은 이 배열의 index다.
DUCKOVLIKE_API TConstArrayView<FLinearColor> GetCustomizationPalette();
DUCKOVLIKE_API FLinearColor GetCustomizationColor(int32 PaletteIndex);

// 데이터 검사다. 슬롯별 파츠 ID는 카탈로그에 등록된 같은 슬롯의 파츠여야 한다.
// 어느 슬롯에서 실패했는지는 Detail로, 호환용 ECustomizationFailure 반환은 Reason만 돌려준다.
DUCKOVLIKE_API FCustomizationFailureDetail ValidateCustomizationProfileDetail(const FCustomizationProfile& Profile);
DUCKOVLIKE_API ECustomizationFailure ValidateCustomizationProfile(const FCustomizationProfile& Profile);
DUCKOVLIKE_API FText GetCustomizationFailureText(ECustomizationFailure Failure);
// 슬롯 이름을 앞에 붙인 안내 문구다.
DUCKOVLIKE_API FText GetCustomizationFailureText(const FCustomizationFailureDetail& Detail);
