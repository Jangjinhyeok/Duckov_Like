#include "CustomizationTypes.h"

ECustomizationFailure ValidateCustomizationProfile(const FCustomizationProfile& Profile)
{
    if (Profile.Kind != ECustomizationPartKind::Part) { return ECustomizationFailure::InvalidKind; }
    if (Profile.PartId.IsNone()) { return ECustomizationFailure::RequiredPartMissing; }
    if (Profile.PartId != TEXT("A") && Profile.PartId != TEXT("B")) { return ECustomizationFailure::UnknownPart; }
    if (!FMath::IsFinite(Profile.Hue) || !FMath::IsFinite(Profile.Shape) || !FMath::IsFinite(Profile.EyeSize)
        || !FMath::IsFinite(Profile.BeakLength) || !FMath::IsFinite(Profile.BodyLength)) { return ECustomizationFailure::InvalidNumber; }
    if (Profile.Hue < 0.0f || Profile.Hue > 1.0f) { return ECustomizationFailure::HueOutOfRange; }
    if (Profile.Shape < -1.0f || Profile.Shape > 1.0f) { return ECustomizationFailure::ShapeOutOfRange; }
    if (Profile.EyeSize < -1.0f || Profile.EyeSize > 1.0f) { return ECustomizationFailure::EyeSizeOutOfRange; }
    if (Profile.BeakLength < -1.0f || Profile.BeakLength > 1.0f) { return ECustomizationFailure::BeakLengthOutOfRange; }
    if (Profile.BodyLength < -1.0f || Profile.BodyLength > 1.0f) { return ECustomizationFailure::BodyLengthOutOfRange; }
    return ECustomizationFailure::None;
}

FLinearColor GetCustomizationColor(float Hue)
{
    return FLinearColor(Hue * 360.0f, 0.7f, 0.9f, 1.0f).HSVToLinearRGB();
}

FText GetCustomizationFailureText(ECustomizationFailure Failure)
{
    switch (Failure)
    {
    case ECustomizationFailure::None: return FText::GetEmpty();
    case ECustomizationFailure::NotEditing: return FText::FromString(TEXT("편집 화면을 먼저 열어 주세요."));
    case ECustomizationFailure::OperationInProgress: return FText::FromString(TEXT("외형 변경을 처리 중입니다."));
    case ECustomizationFailure::InvalidKind: return FText::FromString(TEXT("지원하지 않는 파츠 종류입니다."));
    case ECustomizationFailure::UnknownPart: return FText::FromString(TEXT("등록되지 않은 파츠입니다."));
    case ECustomizationFailure::RequiredPartMissing: return FText::FromString(TEXT("이 슬롯은 파츠가 필요합니다."));
    case ECustomizationFailure::InvalidNumber: return FText::FromString(TEXT("색상과 부위 조절 값은 유한한 수여야 합니다."));
    case ECustomizationFailure::HueOutOfRange: return FText::FromString(TEXT("색상 범위는 0부터 1까지입니다."));
    case ECustomizationFailure::ShapeOutOfRange: return FText::FromString(TEXT("형상 범위는 -1부터 1까지입니다."));
    case ECustomizationFailure::EyeSizeOutOfRange: return FText::FromString(TEXT("눈 크기 범위는 -1부터 1까지입니다."));
    case ECustomizationFailure::BeakLengthOutOfRange: return FText::FromString(TEXT("입/부리 길이 범위는 -1부터 1까지입니다."));
    case ECustomizationFailure::BodyLengthOutOfRange: return FText::FromString(TEXT("몸통 길이 범위는 -1부터 1까지입니다."));
    case ECustomizationFailure::UnsupportedBodyAdjustment: return FText::FromString(TEXT("이 시험 표시에서는 부위별 조절을 지원하지 않습니다."));
    case ECustomizationFailure::MissingMesh: return FText::FromString(TEXT("필수 mesh를 준비할 수 없습니다."));
    case ECustomizationFailure::MissingSkeleton: return FText::FromString(TEXT("공통 Skeleton을 준비할 수 없습니다."));
    case ECustomizationFailure::SkeletonMismatch: return FText::FromString(TEXT("파츠 Skeleton이 공통 Skeleton과 다릅니다."));
    case ECustomizationFailure::MissingMaterial: return FText::FromString(TEXT("필수 material 또는 슬롯이 없습니다."));
    case ECustomizationFailure::MissingColorParameter: return FText::FromString(TEXT("필수 색상 parameter가 없습니다."));
    case ECustomizationFailure::MissingMorph: return FText::FromString(TEXT("필수 Morph Target이 없습니다."));
    case ECustomizationFailure::MissingMask: return FText::FromString(TEXT("mask texture를 준비할 수 없습니다."));
    case ECustomizationFailure::MissingMaskParameter: return FText::FromString(TEXT("mask texture parameter가 없습니다."));
    case ECustomizationFailure::PreparationFailed: return FText::FromString(TEXT("표시 외형 준비에 실패했습니다. 직전 외형을 유지합니다."));
    }
    return FText::FromString(TEXT("외형 적용에 실패했습니다."));
}
