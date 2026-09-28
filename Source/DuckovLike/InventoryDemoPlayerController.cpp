#include "InventoryDemoPlayerController.h"

#include "Engine/DataTable.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "InputCoreTypes.h"
#include "Input/CommonUIActionRouterBase.h"
#include "Input/UIActionBindingHandle.h"
#include "InventoryModel.h"
#include "InventoryScreenWidget.h"
#include "WorldLootActor.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"

void AInventoryDemoPlayerController::BeginPlay()
{
    Super::BeginPlay();
    UDataTable* Definitions = LoadObject<UDataTable>(nullptr, TEXT("/Game/Inventory/DT_ItemDefinitions.DT_ItemDefinitions"));
    if (!ensureMsgf(Definitions, TEXT("인벤토리 Definition DataTable이 없습니다"))) { return; }
    FInventorySaveRecord Record;
    Record.NextInstanceId = 4;
    Record.Containers.Reserve(2);
    auto& Stash = Record.Containers.AddDefaulted_GetRef();
    Stash.ContainerId = TEXT("Stash");
    Stash.GridSize = FIntPoint(6, 4);
    auto& Bag = Record.Containers.AddDefaulted_GetRef();
    Bag.ContainerId = TEXT("Bag");
    Bag.GridSize = FIntPoint(4, 4);
    const FSoftObjectPath DefinitionPath(Definitions);
    auto AddItem = [&DefinitionPath](FInventoryContainerSaveRecord& Container, int32 Id, FName Row, FIntPoint Cell, int32 Quantity)
    {
        auto& Item = Container.Items.AddDefaulted_GetRef();
        Item.InstanceId = Id;
        Item.DefinitionTable = DefinitionPath;
        Item.DefinitionRowName = Row;
        Item.AnchorCell = Cell;
        Item.Quantity = Quantity;
    };
    AddItem(Stash, 1, TEXT("Rifle"), FIntPoint(4, 1), 1);
    AddItem(Stash, 2, TEXT("Ammo"), FIntPoint(0, 3), 18);
    AddItem(Bag, 3, TEXT("Medkit"), FIntPoint(2, 2), 2);
    Model = NewObject<UInventoryModel>(this);
    if (Model->Load(Record) != EInventorySaveFailure::None)
    {
        UE_LOG(LogTemp, Error, TEXT("인벤토리 데모 초기 데이터를 불러오지 못했습니다"));
        Model = nullptr;
        return;
    }
    FInputModeGameOnly GameMode;
    SetInputMode(GameMode);
    bShowMouseCursor = false;
}

void AInventoryDemoPlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();
    InputComponent->BindKey(EKeys::I, IE_Pressed, this, &ThisClass::ToggleInventory);
    InputComponent->BindKey(EKeys::E, IE_Pressed, this, &ThisClass::PickupNearestLoot);
}

EInventoryOperationFailure AInventoryDemoPlayerController::TryPickupLoot(AWorldLootActor* Loot)
{
    if (!IsValid(Loot) || Loot->GetWorld() != GetWorld()) { return EInventoryOperationFailure::ItemNotFound; }
    return Loot->TryPickup(Model, TEXT("Bag"));
}

void AInventoryDemoPlayerController::PickupNearestLoot()
{
    const APawn* ControlledPawn = GetPawnOrSpectator();
    if (!ControlledPawn) { return; }
    AWorldLootActor* Nearest = nullptr;
    double NearestDistanceSquared = FMath::Square(250.0);
    for (TActorIterator<AWorldLootActor> It(GetWorld()); It; ++It)
    {
        if (It->bConsumed || It->IsActorBeingDestroyed()) { continue; }
        const double DistanceSquared = FVector::DistSquared(ControlledPawn->GetActorLocation(), It->GetActorLocation());
        if (DistanceSquared <= NearestDistanceSquared)
        {
            Nearest = *It;
            NearestDistanceSquared = DistanceSquared;
        }
    }
    if (!Nearest) { return; }
    const EInventoryOperationFailure Result = TryPickupLoot(Nearest);
    UE_LOG(LogTemp, Display, TEXT("월드 loot 획득 결과: %s"), *UEnum::GetValueAsString(Result));
}

void AInventoryDemoPlayerController::ToggleInventory()
{
    if (!Model) { return; }
    if (Screen && Screen->IsActivated()) { Screen->DeactivateWidget(); return; }
    if (!Screen)
    {
        UClass* ScreenClass = LoadClass<UInventoryScreenWidget>(nullptr, TEXT("/Game/UI/WBP_InventoryScreen.WBP_InventoryScreen_C"));
        if (!ensureMsgf(ScreenClass, TEXT("인벤토리 화면 Widget Blueprint가 없습니다"))) { return; }
        Screen = CreateWidget<UInventoryScreenWidget>(this, ScreenClass);
        Screen->SetSession(Model);
        Screen->OnDeactivated().AddUObject(this, &ThisClass::OnScreenDeactivated);
        Screen->SetVisibility(ESlateVisibility::Collapsed);
        Screen->AddToViewport(10);
    }
    bShowMouseCursor = true;
    Screen->ActivateWidget();
}

void AInventoryDemoPlayerController::OnScreenDeactivated()
{
    bShowMouseCursor = false;
    RestoreInputHandle = GetWorldTimerManager().SetTimerForNextTick(this, &ThisClass::RestoreGameInput);
}

void AInventoryDemoPlayerController::RestoreGameInput()
{
    if (Screen && Screen->IsActivated()) { return; }
    if (ULocalPlayer* LocalPlayer = GetLocalPlayer())
    {
        if (UCommonUIActionRouterBase* Router = LocalPlayer->GetSubsystem<UCommonUIActionRouterBase>())
        {
            Router->SetActiveUIInputConfig(FUIInputConfig(ECommonInputMode::Game, EMouseCaptureMode::CapturePermanently));
        }
    }
    UWidgetBlueprintLibrary::SetFocusToGameViewport();
}

void AInventoryDemoPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (Screen)
    {
        Screen->OnDeactivated().RemoveAll(this);
        GetWorldTimerManager().ClearTimer(RestoreInputHandle);
        if (Screen->IsActivated()) { Screen->DeactivateWidget(); }
        Screen->RemoveFromParent();
        Screen = nullptr;
    }
    Model = nullptr;
    Super::EndPlay(EndPlayReason);
}
