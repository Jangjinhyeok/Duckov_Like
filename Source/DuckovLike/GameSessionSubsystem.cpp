#include "GameSessionSubsystem.h"

#include "CustomizationModel.h"
#include "Engine/DataTable.h"
#include "InventoryModel.h"

UCustomizationModel* UGameSessionSubsystem::GetCustomizationModel()
{
    if (!CustomizationModel) { CustomizationModel = NewObject<UCustomizationModel>(this); }
    return CustomizationModel;
}

UInventoryModel* UGameSessionSubsystem::GetInventoryModel()
{
    if (InventoryModel) { return InventoryModel; }
    UDataTable* Definitions = LoadObject<UDataTable>(nullptr, TEXT("/Game/Inventory/DT_ItemDefinitions.DT_ItemDefinitions"));
    if (!ensureMsgf(Definitions, TEXT("인벤토리 Definition DataTable이 없습니다"))) { return nullptr; }
    // 데모 맵의 가방 장착 준비가 끝난 상태와 같다: 소형 가방을 장착하고 대형 가방은 보관함에 있다.
    FInventorySaveRecord Record;
    Record.NextInstanceId = 6;
    const FSoftObjectPath DefinitionPath(Definitions);
    auto AddContainer = [&Record](FName Id, FIntPoint Size) -> FInventoryContainerSaveRecord&
    {
        auto& Container = Record.Containers.AddDefaulted_GetRef();
        Container.ContainerId = Id;
        Container.GridSize = Size;
        return Container;
    };
    auto AddItem = [&DefinitionPath](FInventoryContainerSaveRecord& Container, int32 Id, FName Row, FIntPoint Cell, int32 Quantity)
    {
        auto& Item = Container.Items.AddDefaulted_GetRef();
        Item.InstanceId = Id;
        Item.DefinitionTable = DefinitionPath;
        Item.DefinitionRowName = Row;
        Item.AnchorCell = Cell;
        Item.Quantity = Quantity;
    };
    Record.Containers.Reserve(3);
    auto& Stash = AddContainer(TEXT("Stash"), FIntPoint(6, 4));
    AddItem(Stash, 1, TEXT("Rifle"), FIntPoint(4, 1), 1);
    AddItem(Stash, 2, TEXT("Ammo"), FIntPoint(0, 3), 18);
    AddItem(Stash, 5, TEXT("LargeBag"), FIntPoint(0, 0), 1);
    auto& Bag = AddContainer(TEXT("Bag"), FIntPoint(4, 4));
    AddItem(Bag, 3, TEXT("Medkit"), FIntPoint(2, 2), 2);
    auto& Slot = AddContainer(TEXT("BagSlot"), FIntPoint(1, 1));
    AddItem(Slot, 4, TEXT("SmallBag"), FIntPoint(0, 0), 1);
    UInventoryModel* Model = NewObject<UInventoryModel>(this);
    if (Model->Load(Record) != EInventorySaveFailure::None
        || Model->BindBagSlot(TEXT("BagSlot"), TEXT("Bag"), TEXT("Stash")) != EInventoryOperationFailure::None)
    {
        UE_LOG(LogTemp, Error, TEXT("세션 시작 인벤토리를 만들지 못했습니다"));
        return nullptr;
    }
    InventoryModel = Model;
    return InventoryModel;
}

TOptional<EInventoryOperationFailure> UGameSessionSubsystem::ConsumeExtractionResult()
{
    TOptional<EInventoryOperationFailure> Result = PendingExtractionResult;
    PendingExtractionResult.Reset();
    return Result;
}
