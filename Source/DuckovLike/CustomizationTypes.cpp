#include "CustomizationTypes.h"

#include "CustomizationCatalog.h"

TConstArrayView<ECustomizationSlot> GetCustomizationSlots()
{
    static const ECustomizationSlot Slots[] = {ECustomizationSlot::Eye, ECustomizationSlot::Hair, ECustomizationSlot::Beak,
        ECustomizationSlot::Wing, ECustomizationSlot::Tail, ECustomizationSlot::Foot};
    return Slots;
}

bool IsCustomizationSlotRequired(ECustomizationSlot Slot)
{
    return Slot != ECustomizationSlot::Hair;
}

bool IsCustomizationSlotColorable(ECustomizationSlot Slot)
{
    return Slot != ECustomizationSlot::Foot;
}

FText GetCustomizationSlotText(ECustomizationSlot Slot)
{
    switch (Slot)
    {
    case ECustomizationSlot::Eye: return FText::FromString(TEXT("눈"));
    case ECustomizationSlot::Hair: return FText::FromString(TEXT("헤어"));
    case ECustomizationSlot::Beak: return FText::FromString(TEXT("부리"));
    case ECustomizationSlot::Wing: return FText::FromString(TEXT("날개"));
    case ECustomizationSlot::Tail: return FText::FromString(TEXT("꼬리"));
    case ECustomizationSlot::Foot: return FText::FromString(TEXT("발"));
    }
    return FText::GetEmpty();
}

TConstArrayView<FLinearColor> GetCustomizationPalette()
{
    // 원작의 swatch 격자를 줄인 16색이다. 0은 이전 기본 깃털과 비슷한 크림색이다. sRGB로 정의한다.
    static const FLinearColor Palette[] = {
        FLinearColor(FColor(243, 221, 140)), FLinearColor(FColor(250, 250, 250)), FLinearColor(FColor(160, 160, 166)),
        FLinearColor(FColor(36, 36, 42)), FLinearColor(FColor(214, 52, 44)), FLinearColor(FColor(242, 128, 36)),
        FLinearColor(FColor(250, 208, 48)), FLinearColor(FColor(140, 200, 70)), FLinearColor(FColor(52, 146, 78)),
        FLinearColor(FColor(46, 168, 168)), FLinearColor(FColor(92, 172, 232)), FLinearColor(FColor(48, 78, 188)),
        FLinearColor(FColor(132, 82, 192)), FLinearColor(FColor(240, 132, 172)), FLinearColor(FColor(128, 84, 48)),
        FLinearColor(FColor(84, 84, 92))};
    return Palette;
}

FLinearColor GetCustomizationColor(int32 PaletteIndex)
{
    const TConstArrayView<FLinearColor> Palette = GetCustomizationPalette();
    return Palette.IsValidIndex(PaletteIndex) ? Palette[PaletteIndex] : FLinearColor::Black;
}

namespace
{
    FCustomizationFailureDetail ValidateSlotPart(const FCustomizationCatalog& Catalog, ECustomizationSlot Slot, FName PartId)
    {
        if (PartId.IsNone())
        {
            return IsCustomizationSlotRequired(Slot)
                ? FCustomizationFailureDetail::Of(ECustomizationFailure::RequiredPartMissing, Slot) : FCustomizationFailureDetail();
        }
        const FCustomizationPartRow* Row = Catalog.Find(PartId);
        if (!Row) { return FCustomizationFailureDetail::Of(ECustomizationFailure::UnknownPart, Slot); }
        if (Row->Slot != Slot) { return FCustomizationFailureDetail::Of(ECustomizationFailure::SlotMismatch, Slot); }
        return FCustomizationFailureDetail();
    }
}

FCustomizationFailureDetail ValidateCustomizationProfileDetail(const FCustomizationProfile& Profile)
{
    const FCustomizationCatalog Catalog = FCustomizationCatalog::LoadDefault();
    if (!Catalog.IsAvailable()) { return FCustomizationFailureDetail::Of(ECustomizationFailure::CatalogUnavailable); }
    for (const ECustomizationSlot Slot : GetCustomizationSlots())
    {
        const FCustomizationFailureDetail Detail = ValidateSlotPart(Catalog, Slot, Profile.GetSlotPartId(Slot));
        if (Detail.Reason != ECustomizationFailure::None) { return Detail; }
    }
    const int32 PaletteCount = GetCustomizationPalette().Num();
    if (Profile.SkinColor < 0 || Profile.SkinColor >= PaletteCount)
    {
        return FCustomizationFailureDetail::Of(ECustomizationFailure::ColorOutOfRange);
    }
    for (const ECustomizationSlot Slot : GetCustomizationSlots())
    {
        const int32 Color = Profile.GetSlotColor(Slot);
        if (IsCustomizationSlotColorable(Slot) && (Color < 0 || Color >= PaletteCount))
        {
            return FCustomizationFailureDetail::Of(ECustomizationFailure::ColorOutOfRange, Slot);
        }
    }
    if (!FMath::IsFinite(Profile.HeadShape)) { return FCustomizationFailureDetail::Of(ECustomizationFailure::InvalidNumber); }
    if (Profile.HeadShape < 0.0f || Profile.HeadShape > 1.0f) { return FCustomizationFailureDetail::Of(ECustomizationFailure::HeadShapeOutOfRange); }
    return FCustomizationFailureDetail();
}

ECustomizationFailure ValidateCustomizationProfile(const FCustomizationProfile& Profile)
{
    return ValidateCustomizationProfileDetail(Profile).Reason;
}

FText GetCustomizationFailureText(ECustomizationFailure Failure)
{
    switch (Failure)
    {
    case ECustomizationFailure::None: return FText::GetEmpty();
    case ECustomizationFailure::NotEditing: return FText::FromString(TEXT("편집 화면을 먼저 열어 주세요."));
    case ECustomizationFailure::OperationInProgress: return FText::FromString(TEXT("외형 변경을 처리 중입니다."));
    case ECustomizationFailure::InvalidKind: return FText::FromString(TEXT("지원하지 않는 슬롯 또는 파츠 종류입니다."));
    case ECustomizationFailure::UnknownPart: return FText::FromString(TEXT("등록되지 않은 파츠입니다."));
    case ECustomizationFailure::RequiredPartMissing: return FText::FromString(TEXT("이 슬롯은 파츠가 필요합니다."));
    case ECustomizationFailure::InvalidNumber: return FText::FromString(TEXT("머리 모양 값은 유한한 수여야 합니다."));
    case ECustomizationFailure::ColorOutOfRange: return FText::FromString(TEXT("팔레트에 없는 색입니다."));
    case ECustomizationFailure::HeadShapeOutOfRange: return FText::FromString(TEXT("머리 모양 범위는 0부터 1까지입니다."));
    case ECustomizationFailure::UnsupportedBodyAdjustment: return FText::FromString(TEXT("이 시험 표시에서는 머리 모양을 지원하지 않습니다."));
    case ECustomizationFailure::MissingMesh: return FText::FromString(TEXT("필수 mesh를 준비할 수 없습니다."));
    case ECustomizationFailure::MissingSkeleton: return FText::FromString(TEXT("공통 Skeleton을 준비할 수 없습니다."));
    case ECustomizationFailure::SkeletonMismatch: return FText::FromString(TEXT("파츠 Skeleton이 공통 Skeleton과 다릅니다."));
    case ECustomizationFailure::MissingMaterial: return FText::FromString(TEXT("필수 material 또는 슬롯이 없습니다."));
    case ECustomizationFailure::MissingColorParameter: return FText::FromString(TEXT("필수 색상 parameter가 없습니다."));
    case ECustomizationFailure::MissingMorph: return FText::FromString(TEXT("필수 Morph Target이 없습니다."));
    case ECustomizationFailure::MissingMask: return FText::FromString(TEXT("mask texture를 준비할 수 없습니다."));
    case ECustomizationFailure::MissingMaskParameter: return FText::FromString(TEXT("mask texture parameter가 없습니다."));
    case ECustomizationFailure::PreparationFailed: return FText::FromString(TEXT("표시 외형 준비에 실패했습니다. 직전 외형을 유지합니다."));
    case ECustomizationFailure::SlotMismatch: return FText::FromString(TEXT("다른 슬롯의 파츠입니다."));
    case ECustomizationFailure::DuplicatePartId: return FText::FromString(TEXT("파츠 ID가 겹치거나 예약어(None)와 충돌합니다."));
    case ECustomizationFailure::CatalogUnavailable: return FText::FromString(TEXT("파츠 카탈로그를 불러올 수 없습니다."));
    }
    return FText::FromString(TEXT("외형 적용에 실패했습니다."));
}

FText GetCustomizationFailureText(const FCustomizationFailureDetail& Detail)
{
    const FText Reason = GetCustomizationFailureText(Detail.Reason);
    if (!Detail.bHasSlot || Detail.Reason == ECustomizationFailure::None) { return Reason; }
    return FText::Format(FText::FromString(TEXT("{0} 슬롯: {1}")), GetCustomizationSlotText(Detail.Slot), Reason);
}
