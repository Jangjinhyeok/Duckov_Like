#include "CustomizationModel.h"

ECustomizationFailure UCustomizationModel::Change(const FCustomizationProfile& Candidate, bool bNextEditing,
    bool bCommit, TFunctionRef<ECustomizationFailure(const FCustomizationProfile&)> Present)
{
    if (bChanging) { return ECustomizationFailure::OperationInProgress; }
    const ECustomizationFailure Validation = ValidateCustomizationProfile(Candidate);
    if (Validation != ECustomizationFailure::None) { return Validation; }
    // callback이 Draft 조회 참조를 받더라도 공개 전 값이 흔들리지 않도록 값 후보를 고정한다.
    const FCustomizationProfile Prepared = Candidate;
    TGuardValue<bool> Guard(bChanging, true);
    const ECustomizationFailure Presentation = Present(Prepared);
    if (Presentation != ECustomizationFailure::None) { return Presentation; }
    const bool bChanged = Draft != Prepared || bEditing != bNextEditing || (bCommit && Profile != Prepared);
    Draft = Prepared;
    if (bCommit) { Profile = Prepared; }
    bEditing = bNextEditing;
    if (bChanged) { Changed.Broadcast(); }
    return ECustomizationFailure::None;
}

ECustomizationFailure UCustomizationModel::BeginEdit(
    TFunctionRef<ECustomizationFailure(const FCustomizationProfile&)> Present)
{
    if (bChanging) { return ECustomizationFailure::OperationInProgress; }
    if (bEditing) { return ECustomizationFailure::None; }
    return Change(Profile, true, false, Present);
}

ECustomizationFailure UCustomizationModel::TryEdit(const FCustomizationProfile& Candidate,
    TFunctionRef<ECustomizationFailure(const FCustomizationProfile&)> Present)
{
    if (bChanging) { return ECustomizationFailure::OperationInProgress; }
    if (!bEditing) { return ECustomizationFailure::NotEditing; }
    return Change(Candidate, true, false, Present);
}

ECustomizationFailure UCustomizationModel::Apply(
    TFunctionRef<ECustomizationFailure(const FCustomizationProfile&)> Present)
{
    if (bChanging) { return ECustomizationFailure::OperationInProgress; }
    if (!bEditing) { return ECustomizationFailure::NotEditing; }
    return Change(Draft, false, true, Present);
}

ECustomizationFailure UCustomizationModel::Reset(
    TFunctionRef<ECustomizationFailure(const FCustomizationProfile&)> Present)
{
    if (bChanging) { return ECustomizationFailure::OperationInProgress; }
    if (!bEditing) { return ECustomizationFailure::NotEditing; }
    return Change(FCustomizationProfile(), true, false, Present);
}

ECustomizationFailure UCustomizationModel::Cancel(
    TFunctionRef<ECustomizationFailure(const FCustomizationProfile&)> Present)
{
    if (bChanging) { return ECustomizationFailure::OperationInProgress; }
    if (!bEditing) { return ECustomizationFailure::NotEditing; }
    return Change(Profile, false, false, Present);
}
