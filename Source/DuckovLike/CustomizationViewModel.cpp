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

ECustomizationFailure UCustomizationViewModel::Edit(TFunctionRef<void(FCustomizationProfile&)> Change)
{
    if (!Model) { return Report(ECustomizationFailure::NotEditing); }
    FCustomizationProfile Candidate = Model->GetDraft();
    Change(Candidate);
    return Report(Model->TryEdit(Candidate, [this](const auto& Value) { return Present(Value); }));
}

ECustomizationFailure UCustomizationViewModel::SelectSlotPart(ECustomizationSlot Slot, FName PartId)
{
    return Edit([Slot, PartId](FCustomizationProfile& Candidate) { Candidate.SetSlotPartId(Slot, PartId); });
}

ECustomizationFailure UCustomizationViewModel::SetSkinColor(int32 PaletteIndex)
{
    return Edit([PaletteIndex](FCustomizationProfile& Candidate) { Candidate.SkinColor = PaletteIndex; });
}

ECustomizationFailure UCustomizationViewModel::SetSlotColor(ECustomizationSlot Slot, int32 PaletteIndex)
{
    // 색이 없는 슬롯(발)의 색 변경은 값이 바뀌지 않으므로 성공·무변경이다.
    return Edit([Slot, PaletteIndex](FCustomizationProfile& Candidate) { Candidate.SetSlotColor(Slot, PaletteIndex); });
}

ECustomizationFailure UCustomizationViewModel::SetHeadShape(float Value)
{
    return Edit([Value](FCustomizationProfile& Candidate) { Candidate.HeadShape = Value; });
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

TArray<FName> UCustomizationViewModel::GetPartIds() const
{
    TArray<FName> Result;
    for (const ECustomizationSlot Slot : GetCustomizationSlots()) { Result.Add(GetSlotPartId(Slot)); }
    return Result;
}
TArray<int32> UCustomizationViewModel::GetColorIndices() const
{
    TArray<int32> Result{GetSkinColor()};
    for (const ECustomizationSlot Slot : GetCustomizationSlots()) { Result.Add(GetSlotColor(Slot)); }
    return Result;
}
FName UCustomizationViewModel::GetSlotPartId(ECustomizationSlot Slot) const
{
    return (Model ? Model->GetDraft() : FCustomizationProfile()).GetSlotPartId(Slot);
}
int32 UCustomizationViewModel::GetSlotColor(ECustomizationSlot Slot) const
{
    return (Model ? Model->GetDraft() : FCustomizationProfile()).GetSlotColor(Slot);
}
int32 UCustomizationViewModel::GetSkinColor() const { return (Model ? Model->GetDraft() : FCustomizationProfile()).SkinColor; }
float UCustomizationViewModel::GetHeadShape() const { return (Model ? Model->GetDraft() : FCustomizationProfile()).HeadShape; }
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
    UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(GetPartIds);
    UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(GetColorIndices);
    UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(GetHeadShape);
}

void UCustomizationViewModel::BeginDestroy()
{
    if (Model) { Model->OnChanged().Remove(ChangedHandle); }
    Super::BeginDestroy();
}
