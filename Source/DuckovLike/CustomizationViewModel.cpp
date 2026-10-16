#include "CustomizationViewModel.h"

#include "CustomizationModel.h"
#include "CustomizationAppearanceComponent.h"
#include "Misc/ScopeExit.h"

void UCustomizationViewModel::Bind(UCustomizationModel* InModel, UCustomizationAppearanceComponent* InAppearance)
{
    if (bNotifying)
    {
        PendingModel = InModel;
        PendingAppearance = InAppearance;
        bPendingBind = true;
        return;
    }
    if (Model == InModel && Appearance == InAppearance) { return; }
    if (Model) { Model->OnChanged().Remove(ChangedHandle); }
    ChangedHandle.Reset();
    Model = InModel;
    Appearance = InAppearance;
    LastFailure = FCustomizationFailureDetail();
    if (Model) { ChangedHandle = Model->OnChanged().AddUObject(this, &ThisClass::OnModelChanged); }
}

ECustomizationFailure UCustomizationViewModel::Present(const FCustomizationProfile& Candidate)
{
    return IsValid(Appearance) ? Appearance->TryApply(Candidate) : ECustomizationFailure::PreparationFailed;
}

ECustomizationFailure UCustomizationViewModel::Report(ECustomizationFailure Result)
{
    // 실패한 슬롯은 데이터 검사(Model)나 표시 준비(Appearance)가 알고 있다. 같은 사유일 때만 가져온다.
    FCustomizationFailureDetail Detail = FCustomizationFailureDetail::Of(Result);
    if (Result != ECustomizationFailure::None)
    {
        if (Model && Model->GetLastFailure().Reason == Result) { Detail = Model->GetLastFailure(); }
        if (!Detail.bHasSlot && IsValid(Appearance) && Appearance->GetLastFailure().Reason == Result) { Detail = Appearance->GetLastFailure(); }
    }
    if (LastFailure != Detail)
    {
        LastFailure = Detail;
        UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(GetFailureText);
    }
    return Result;
}

ECustomizationFailure UCustomizationViewModel::Open()
{
    if (!Model) { return Report(ECustomizationFailure::PreparationFailed); }
    // 강제 화면 해제 뒤 남은 편집도 확정 외형을 준비한 후 버린다.
    if (Model->IsEditing())
    {
        const ECustomizationFailure Result = Model->Cancel([this](const auto& Value) { return Present(Value); });
        if (Result != ECustomizationFailure::None) { return Report(Result); }
    }
    if (!Model) { return Report(ECustomizationFailure::PreparationFailed); }
    return Report(Model->BeginEdit([this](const auto& Value) { return Present(Value); }));
}

ECustomizationFailure UCustomizationViewModel::SelectPart(FName PartId)
{
    return SelectSlotPart(ECustomizationSlot::Crest, PartId);
}

ECustomizationFailure UCustomizationViewModel::SelectSlotPart(ECustomizationSlot Slot, FName PartId)
{
    if (!Model) { return Report(ECustomizationFailure::NotEditing); }
    FCustomizationProfile Candidate = Model->GetDraft();
    Candidate.SetSlotPartId(Slot, PartId);
    return Report(Model->TryEdit(Candidate, [this](const auto& Value) { return Present(Value); }));
}

ECustomizationFailure UCustomizationViewModel::SetHue(float Value)
{
    if (!Model) { return Report(ECustomizationFailure::NotEditing); }
    FCustomizationProfile Candidate = Model->GetDraft();
    Candidate.Hue = Value;
    return Report(Model->TryEdit(Candidate, [this](const auto& Profile) { return Present(Profile); }));
}

ECustomizationFailure UCustomizationViewModel::SetShape(float Value)
{
    if (!Model) { return Report(ECustomizationFailure::NotEditing); }
    FCustomizationProfile Candidate = Model->GetDraft();
    Candidate.Shape = Value;
    return Report(Model->TryEdit(Candidate, [this](const auto& Profile) { return Present(Profile); }));
}

ECustomizationFailure UCustomizationViewModel::SetEyeSize(float Value)
{
    if (!Model) { return Report(ECustomizationFailure::NotEditing); }
    FCustomizationProfile Candidate = Model->GetDraft();
    Candidate.EyeSize = Value;
    return Report(Model->TryEdit(Candidate, [this](const auto& Profile) { return Present(Profile); }));
}

ECustomizationFailure UCustomizationViewModel::SetBeakLength(float Value)
{
    if (!Model) { return Report(ECustomizationFailure::NotEditing); }
    FCustomizationProfile Candidate = Model->GetDraft();
    Candidate.BeakLength = Value;
    return Report(Model->TryEdit(Candidate, [this](const auto& Profile) { return Present(Profile); }));
}

ECustomizationFailure UCustomizationViewModel::SetBodyLength(float Value)
{
    if (!Model) { return Report(ECustomizationFailure::NotEditing); }
    FCustomizationProfile Candidate = Model->GetDraft();
    Candidate.BodyLength = Value;
    return Report(Model->TryEdit(Candidate, [this](const auto& Profile) { return Present(Profile); }));
}

ECustomizationFailure UCustomizationViewModel::Apply()
{
    return Report(Model ? Model->Apply([this](const auto& Value) { return Present(Value); }) : ECustomizationFailure::NotEditing);
}

ECustomizationFailure UCustomizationViewModel::Reset()
{
    return Report(Model ? Model->Reset([this](const auto& Value) { return Present(Value); }) : ECustomizationFailure::NotEditing);
}

ECustomizationFailure UCustomizationViewModel::Cancel()
{
    return Report(Model ? Model->Cancel([this](const auto& Value) { return Present(Value); }) : ECustomizationFailure::NotEditing);
}

FName UCustomizationViewModel::GetPartId() const { return Model ? Model->GetDraft().PartId : NAME_None; }
FName UCustomizationViewModel::GetEyewearId() const { return Model ? Model->GetDraft().EyewearId : NAME_None; }
FName UCustomizationViewModel::GetBeakWearId() const { return Model ? Model->GetDraft().BeakWearId : NAME_None; }
FName UCustomizationViewModel::GetSlotPartId(ECustomizationSlot Slot) const { return Model ? Model->GetDraft().GetSlotPartId(Slot) : NAME_None; }
float UCustomizationViewModel::GetHue() const { return Model ? Model->GetDraft().Hue : 0.12f; }
float UCustomizationViewModel::GetShape() const { return Model ? Model->GetDraft().Shape : 0.f; }
float UCustomizationViewModel::GetEyeSize() const { return Model ? Model->GetDraft().EyeSize : 0.f; }
float UCustomizationViewModel::GetBeakLength() const { return Model ? Model->GetDraft().BeakLength : 0.f; }
float UCustomizationViewModel::GetBodyLength() const { return Model ? Model->GetDraft().BodyLength : 0.f; }
FText UCustomizationViewModel::GetFailureText() const { return GetCustomizationFailureText(LastFailure); }

void UCustomizationViewModel::OnModelChanged()
{
    ON_SCOPE_EXIT
    {
        if (bPendingBind)
        {
            UCustomizationModel* NextModel = PendingModel;
            UCustomizationAppearanceComponent* NextAppearance = PendingAppearance;
            bPendingBind = false;
            PendingModel = nullptr;
            PendingAppearance = nullptr;
            Bind(NextModel, NextAppearance);
        }
    };
    TGuardValue<bool> Guard(bNotifying, true);
    UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(GetPartId);
    UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(GetEyewearId);
    UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(GetBeakWearId);
    UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(GetHue);
    UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(GetShape);
    UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(GetEyeSize);
    UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(GetBeakLength);
    UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(GetBodyLength);
}

void UCustomizationViewModel::BeginDestroy()
{
    if (Model) { Model->OnChanged().Remove(ChangedHandle); }
    Super::BeginDestroy();
}
