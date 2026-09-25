#include "InteractionViewModel.h"

#include "InventoryModel.h"
#include "Misc/ScopeExit.h"

void UInteractionViewModel::Unsubscribe()
{
    if (Model) { Model->OnChanged().Remove(ChangedHandle); }
    ChangedHandle.Reset();
}

void UInteractionViewModel::Bind(UInventoryModel* InModel)
{
    if (Model == InModel && !bDragging && !bHasPendingBinding) { return; }
    if (bUpdating)
    {
        PendingModel = InModel;
        bHasPendingBinding = true;
        return;
    }
    ON_SCOPE_EXIT { ApplyPendingActions(); };
    TGuardValue<bool> Guard(bUpdating, true);
    Unsubscribe();
    ClearDrag();
    Model = InModel;
    LastFailure = EInventoryOperationFailure::None;
    if (Model) { ChangedHandle = Model->OnChanged().AddUObject(this, &ThisClass::OnModelChanged); }
    NotifyState();
}

void UInteractionViewModel::ApplyPendingActions()
{
    if (bUpdating) { return; }
    if (bHasPendingBinding)
    {
        bHasPendingBinding = false;
        Bind(PendingModel);
        PendingModel = nullptr;
    }
    if (bPendingCancel)
    {
        bPendingCancel = false;
        Cancel();
    }
}

void UInteractionViewModel::ClearDrag()
{
    bDragging = false;
    DraggedId = INDEX_NONE;
    SourceId = TargetId = NAME_None;
    PreviewCell = FIntPoint::ZeroValue;
    bPreviewRotated = false;
    PreviewFailure = EInventoryOperationFailure::ItemNotFound;
}

EInventoryOperationFailure UInteractionViewModel::Revalidate()
{
    if (!Model || !bDragging || !Model->FindItem(SourceId, DraggedId))
    {
        ClearDrag();
        return EInventoryOperationFailure::ItemNotFound;
    }
    PreviewFailure = Model->CanMove(SourceId, TargetId, DraggedId, PreviewCell, bPreviewRotated);
    return PreviewFailure;
}

EInventoryOperationFailure UInteractionViewModel::BeginDrag(FName Source, int32 InstanceId)
{
    if (bUpdating) { return EInventoryOperationFailure::OperationInProgress; }
    ON_SCOPE_EXIT { ApplyPendingActions(); };
    TGuardValue<bool> Guard(bUpdating, true);
    ClearDrag();
    const FItemInstance* Item = Model ? Model->FindItem(Source, InstanceId) : nullptr;
    if (!Item) { LastFailure = EInventoryOperationFailure::ItemNotFound; NotifyState(); return LastFailure; }
    SourceId = TargetId = Source;
    DraggedId = InstanceId;
    PreviewCell = Item->AnchorCell;
    bPreviewRotated = Item->bRotated;
    bDragging = true;
    LastFailure = Revalidate();
    NotifyState();
    return LastFailure;
}

EInventoryOperationFailure UInteractionViewModel::Preview(FName Target, FIntPoint Cell)
{
    if (bUpdating) { return EInventoryOperationFailure::OperationInProgress; }
    ON_SCOPE_EXIT { ApplyPendingActions(); };
    TGuardValue<bool> Guard(bUpdating, true);
    TargetId = Target;
    PreviewCell = Cell;
    LastFailure = Revalidate();
    NotifyState();
    return LastFailure;
}

EInventoryOperationFailure UInteractionViewModel::Rotate()
{
    if (bUpdating) { return EInventoryOperationFailure::OperationInProgress; }
    ON_SCOPE_EXIT { ApplyPendingActions(); };
    TGuardValue<bool> Guard(bUpdating, true);
    bPreviewRotated = !bPreviewRotated;
    LastFailure = Revalidate();
    NotifyState();
    return LastFailure;
}

EInventoryOperationFailure UInteractionViewModel::Drop()
{
    if (bUpdating) { return EInventoryOperationFailure::OperationInProgress; }
    ON_SCOPE_EXIT { ApplyPendingActions(); };
    TGuardValue<bool> Guard(bUpdating, true);
    LastFailure = Revalidate();
    if (LastFailure == EInventoryOperationFailure::None)
    {
        LastFailure = Model->TryMove(SourceId, TargetId, DraggedId, PreviewCell, bPreviewRotated);
        if (LastFailure == EInventoryOperationFailure::None) { ClearDrag(); }
    }
    NotifyState();
    return LastFailure;
}

void UInteractionViewModel::Cancel()
{
    if (!bDragging && LastFailure == EInventoryOperationFailure::None) { return; }
    if (bUpdating) { bPendingCancel = true; return; }
    ON_SCOPE_EXIT { ApplyPendingActions(); };
    TGuardValue<bool> Guard(bUpdating, true);
    ClearDrag();
    LastFailure = EInventoryOperationFailure::None;
    NotifyState();
}

EInventoryOperationFailure UInteractionViewModel::Command(TFunctionRef<EInventoryOperationFailure()> Action)
{
    if (bUpdating) { return EInventoryOperationFailure::OperationInProgress; }
    ON_SCOPE_EXIT { ApplyPendingActions(); };
    TGuardValue<bool> Guard(bUpdating, true);
    const EInventoryOperationFailure Result = Model ? Action() : EInventoryOperationFailure::InvalidContainer;
    LastFailure = Result;
    if (bDragging)
    {
        const EInventoryOperationFailure PreviewResult = Revalidate();
        if (Result == EInventoryOperationFailure::None) { LastFailure = PreviewResult; }
    }
    NotifyState();
    return Result;
}

EInventoryOperationFailure UInteractionViewModel::Stack(FName Source, FName Target, int32 SourceInstanceId, int32 TargetInstanceId)
{
    return Command([&]() { return Model->TryStack(Source, Target, SourceInstanceId, TargetInstanceId); });
}
EInventoryOperationFailure UInteractionViewModel::Sort(FName ContainerId)
{
    return Command([&]() { return Model->TrySort(ContainerId); });
}
EInventoryOperationFailure UInteractionViewModel::Resize(FName ContainerId, FIntPoint Size)
{
    return Command([&]() { return Model->TryResize(ContainerId, Size); });
}

void UInteractionViewModel::OnModelChanged(const FInventoryChangeSet& Change)
{
    if (bUpdating || !bDragging) { return; }
    ON_SCOPE_EXIT { ApplyPendingActions(); };
    TGuardValue<bool> Guard(bUpdating, true);
    if (Change.bReset) { ClearDrag(); LastFailure = EInventoryOperationFailure::None; }
    else { LastFailure = Revalidate(); }
    NotifyState();
}

FText UInteractionViewModel::GetFailureText() const
{
    switch (LastFailure)
    {
    case EInventoryOperationFailure::None: return FText::GetEmpty();
    case EInventoryOperationFailure::NoSpace: return NSLOCTEXT("Inventory", "NoSpace", "배치할 공간이 없습니다.");
    case EInventoryOperationFailure::Occupied: return NSLOCTEXT("Inventory", "Occupied", "다른 아이템이 차지한 위치입니다.");
    case EInventoryOperationFailure::ItemNotFound: return NSLOCTEXT("Inventory", "ItemNotFound", "아이템을 찾을 수 없습니다.");
    case EInventoryOperationFailure::StackMismatch: return NSLOCTEXT("Inventory", "StackMismatch", "함께 쌓을 수 없는 아이템입니다.");
    case EInventoryOperationFailure::StackFull: return NSLOCTEXT("Inventory", "StackFull", "더 쌓을 수 없습니다.");
    case EInventoryOperationFailure::ResizeOverflow: return NSLOCTEXT("Inventory", "ResizeOverflow", "현재 배치를 유지할 수 없는 크기입니다.");
    case EInventoryOperationFailure::InvalidContainer: return NSLOCTEXT("Inventory", "InvalidContainer", "인벤토리를 찾을 수 없습니다.");
    case EInventoryOperationFailure::OperationInProgress: return NSLOCTEXT("Inventory", "OperationInProgress", "변경 처리 중입니다.");
    }
    return FText::GetEmpty();
}

void UInteractionViewModel::NotifyState()
{
    UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(IsDragging);
    UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(GetPreviewCell);
    UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(IsPreviewRotated);
    UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(GetTargetContainerId);
    UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(GetPreviewFailure);
    UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(GetFailureText);
}

void UInteractionViewModel::BeginDestroy()
{
    Unsubscribe();
    Super::BeginDestroy();
}
