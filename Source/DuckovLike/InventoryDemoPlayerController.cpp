#include "InventoryDemoPlayerController.h"

#include "Engine/DataTable.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "InputCoreTypes.h"
#include "Input/CommonUIActionRouterBase.h"
#include "Input/UIActionBindingHandle.h"
#include "InventoryModel.h"
#include "InventoryPlacement.h"
#include "ItemDefinitionRow.h"
#include "InventoryScreenWidget.h"
#include "WorldLootActor.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "RaidStatusWidget.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "CustomizationModel.h"
#include "CustomizationScreenWidget.h"

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
    InputComponent->BindKey(EKeys::F5, IE_Pressed, this, &ThisClass::EnterRaid);
    InputComponent->BindKey(EKeys::F6, IE_Pressed, this, &ThisClass::ExtractRaid);
}

EInventoryOperationFailure AInventoryDemoPlayerController::TryPickupLoot(AWorldLootActor* Loot)
{
    if (bRaidTransitionInProgress) { return EInventoryOperationFailure::OperationInProgress; }
    if (RaidPhase != ERaidDemoPhase::Disabled &&
        (RaidPhase != ERaidDemoPhase::InRaid || !RaidLoot.Contains(Loot)))
    {
        return EInventoryOperationFailure::InvalidContainer;
    }
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
        if (It->bConsumed || It->IsActorBeingDestroyed() || It->IsHidden()) { continue; }
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
    if (RaidPhase == ERaidDemoPhase::InRaid)
    {
        UpdateRaidStatus(Result == EInventoryOperationFailure::None
            ? NSLOCTEXT("RaidDemo", "LootAdded", "loot를 가방에 넣었습니다")
            : NSLOCTEXT("RaidDemo", "LootFailed", "loot를 넣을 수 없습니다. 가방 공간을 확인하세요"));
    }
}

void AInventoryDemoPlayerController::ToggleInventory()
{
    if (CustomizationScreen && CustomizationScreen->IsActivated()) { return; }
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
    Screen->SetStashAccessible(RaidPhase != ERaidDemoPhase::InRaid);
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
    if (CustomizationScreen && CustomizationScreen->IsActivated()) { return; }
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
    if (CustomizationScreen)
    {
        CustomizationScreen->OnDeactivated().RemoveAll(this);
        if (CustomizationScreen->IsActivated()) { CustomizationScreen->DeactivateWidget(); }
        CustomizationScreen->RemoveFromParent();
        CustomizationScreen = nullptr;
    }
    CustomizationModel = nullptr;
    DestroyRaidLoot();
    if (RaidStatus) { RaidStatus->RemoveFromParent(); RaidStatus = nullptr; }
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

void AInventoryDemoPlayerController::OpenCustomizationPrototype()
{
    if (!GetWorld() || !Model || bRaidTransitionInProgress || RaidPhase != ERaidDemoPhase::Disabled
        || (Screen && Screen->IsActivated())) { return; }
    if (CustomizationScreen && CustomizationScreen->IsActivated()) { return; }
    if (!CustomizationModel) { CustomizationModel = NewObject<UCustomizationModel>(this); }
    if (!CustomizationScreen)
    {
        CustomizationScreen = CreateWidget<UCustomizationScreenWidget>(this, UCustomizationScreenWidget::StaticClass());
        if (!CustomizationScreen) { return; }
        CustomizationScreen->SetSession(CustomizationModel);
        CustomizationScreen->OnDeactivated().AddUObject(this, &ThisClass::OnScreenDeactivated);
        CustomizationScreen->AddToViewport(30);
    }
    bShowMouseCursor = true;
    CustomizationScreen->ActivateWidget();
}

void AInventoryDemoPlayerController::StartRaidDemo() { ShowRaidResult(TryStartRaidDemo()); }
void AInventoryDemoPlayerController::StartBagEquipmentDemo()
{
    const EInventoryOperationFailure Result = TryStartBagEquipmentDemo();
    UE_LOG(LogTemp, Display, TEXT("가방 장착 데모 준비 결과: %s"), *UEnum::GetValueAsString(Result));
}

EInventoryOperationFailure AInventoryDemoPlayerController::TryStartBagEquipmentDemo()
{
    if (bRaidTransitionInProgress) { return EInventoryOperationFailure::OperationInProgress; }
    if (!Model || !GetWorld() || RaidPhase == ERaidDemoPhase::InRaid || !Model->GetBagSlotContainerId().IsNone())
    {
        return EInventoryOperationFailure::InvalidContainer;
    }
    if (Screen && Screen->IsInventoryInteractionBlocked()) { return EInventoryOperationFailure::OperationInProgress; }
    TGuardValue<bool> Guard(bRaidTransitionInProgress, true);
    UDataTable* Definitions = LoadObject<UDataTable>(nullptr, TEXT("/Game/Inventory/DT_ItemDefinitions.DT_ItemDefinitions"));
    if (!Definitions || Definitions->GetRowStruct() != FItemDefinitionRow::StaticStruct())
    {
        return EInventoryOperationFailure::InvalidDefinition;
    }
    auto HasDefinition = [&](FName Name, FIntPoint Capacity)
    {
        const FItemDefinitionRow* Row = Definitions->FindRow<FItemDefinitionRow>(Name, TEXT("BagEquipmentDemo"), false);
        return Row && Row->Size == FIntPoint(1, 1) && !Row->bStackable && Row->MaxStack == 1 && Row->BagGridSize == Capacity;
    };
    if (!HasDefinition(TEXT("SmallBag"), FIntPoint(4, 4)) || !HasDefinition(TEXT("LargeBag"), FIntPoint(6, 4)))
    {
        return EInventoryOperationFailure::InvalidDefinition;
    }
    const FInventoryContainer* Stash = Model->FindContainer(TEXT("Stash"));
    const FInventoryContainer* Bag = Model->FindContainer(TEXT("Bag"));
    if (!Stash || !Bag || Bag->GridSize != FIntPoint(4, 4) || Model->FindContainer(TEXT("BagSlot")))
    {
        return EInventoryOperationFailure::InvalidContainer;
    }
    FInventorySaveRecord Candidate;
    if (Model->Save(Candidate) != EInventorySaveFailure::None) { return EInventoryOperationFailure::InvalidDefinition; }
    for (const auto& Container : Candidate.Containers)
    {
        if (Container.Items.ContainsByPredicate([](const auto& Item)
            { return Item.DefinitionRowName == TEXT("SmallBag") || Item.DefinitionRowName == TEXT("LargeBag"); }))
        {
            return EInventoryOperationFailure::InvalidContainer;
        }
    }
    if (Candidate.NextInstanceId > MAX_int32 - 2) { return EInventoryOperationFailure::InstanceIdExhausted; }
    FItemInstance Large;
    Large.InstanceId = Candidate.NextInstanceId + 1;
    Large.DefinitionTable = Definitions;
    Large.DefinitionRowName = TEXT("LargeBag");
    FInventoryContainer PlannedStash = *Stash;
    bool bPlaced = false;
    for (int32 Y = 0; Y < Stash->GridSize.Y && !bPlaced; ++Y)
    {
        for (int32 X = 0; X < Stash->GridSize.X; ++X)
        {
            Large.AnchorCell = FIntPoint(X, Y);
            if (FInventoryPlacement::TryPlace(PlannedStash, Large) == EInventoryOperationFailure::None)
            {
                bPlaced = true;
                break;
            }
        }
    }
    if (!bPlaced) { return EInventoryOperationFailure::NoSpace; }
    // 유효한 원본 Save와 검증된 두 추가 항목만 사용한다. 사전 검사에서 allocator를 변경하지 않는다.
    FInventoryItemSaveRecord Small;
    Small.InstanceId = Candidate.NextInstanceId;
    Small.DefinitionTable = FSoftObjectPath(Definitions);
    Small.DefinitionRowName = TEXT("SmallBag");
    FInventoryItemSaveRecord LargeSaved = Small;
    LargeSaved.InstanceId = Large.InstanceId;
    LargeSaved.DefinitionRowName = TEXT("LargeBag");
    LargeSaved.AnchorCell = Large.AnchorCell;
    FInventoryContainerSaveRecord* SavedStash = Candidate.Containers.FindByPredicate(
        [](const auto& Container) { return Container.ContainerId == TEXT("Stash"); });
    SavedStash->Items.Add(LargeSaved);
    auto& Slot = Candidate.Containers.AddDefaulted_GetRef();
    Slot.ContainerId = TEXT("BagSlot");
    Slot.Items.Add(Small);
    Candidate.NextInstanceId += 2;
    const EInventorySaveFailure LoadResult = Model->Load(Candidate);
    if (LoadResult != EInventorySaveFailure::None)
    {
        return LoadResult == EInventorySaveFailure::OperationInProgress
            ? EInventoryOperationFailure::OperationInProgress : EInventoryOperationFailure::InvalidDefinition;
    }
    return Model->BindBagSlot(TEXT("BagSlot"), TEXT("Bag"), TEXT("Stash"));
}

void AInventoryDemoPlayerController::EnterRaid() { ShowRaidResult(TryEnterRaid()); }
void AInventoryDemoPlayerController::ExtractRaid() { ShowRaidResult(TryExtractRaid()); }

ERaidDemoFailure AInventoryDemoPlayerController::TryStartRaidDemo()
{
    if (CustomizationScreen && CustomizationScreen->IsActivated()) { return ERaidDemoFailure::OperationInProgress; }
    if (bRaidTransitionInProgress) { return ERaidDemoFailure::OperationInProgress; }
    if (RaidPhase != ERaidDemoPhase::Disabled) { return ERaidDemoFailure::InvalidPhase; }
    if (!Model || !GetWorld()) { return ERaidDemoFailure::WorldUnavailable; }
    TGuardValue<bool> Guard(bRaidTransitionInProgress, true);
    UStaticMesh* Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr,
        TEXT("/Engine/EngineDebugMaterials/DebugMeshMaterialFakeLight.DebugMeshMaterialFakeLight"));
    if (!Cylinder || !Material) { return ERaidDemoFailure::WorldUnavailable; }
    FActorSpawnParameters Spawn;
    Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    ExtractionPoint = GetWorld()->SpawnActor<AStaticMeshActor>(FVector(430.f, 0.f, 20.f), FRotator::ZeroRotator, Spawn);
    if (!ExtractionPoint) { return ERaidDemoFailure::WorldUnavailable; }
    UStaticMeshComponent* Mesh = ExtractionPoint->GetStaticMeshComponent();
    Mesh->SetMobility(EComponentMobility::Movable);
    Mesh->SetStaticMesh(Cylinder);
    Mesh->SetMaterial(0, Material);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    ExtractionPoint->SetActorScale3D(FVector(2.f, 2.f, 0.2f));
    ExtractionPoint->SetActorHiddenInGame(true);
    RaidStatus = CreateWidget<URaidStatusWidget>(this, URaidStatusWidget::StaticClass());
    if (!RaidStatus)
    {
        ExtractionPoint->Destroy();
        ExtractionPoint = nullptr;
        return ERaidDemoFailure::WorldUnavailable;
    }
    RaidStatus->AddToViewport(20);
    // 원본 map Actor는 비표시 템플릿으로 유지하고 저장된 asset은 변경하지 않는다.
    for (TActorIterator<AWorldLootActor> It(GetWorld()); It; ++It)
    {
        if (It->bConsumed || It->IsActorBeingDestroyed()) { continue; }
        RaidLootTemplates.Add(*It);
        It->SetActorHiddenInGame(true);
    }
    if (Screen && Screen->IsActivated()) { Screen->DeactivateWidget(); }
    RaidPhase = ERaidDemoPhase::Preparation;
    UpdateRaidStatus();
    return ERaidDemoFailure::None;
}

ERaidDemoFailure AInventoryDemoPlayerController::TryEnterRaid()
{
    if (bRaidTransitionInProgress) { return ERaidDemoFailure::OperationInProgress; }
    if (RaidPhase != ERaidDemoPhase::Preparation) { return ERaidDemoFailure::InvalidPhase; }
    if (!Model || !IsValid(ExtractionPoint)) { return ERaidDemoFailure::WorldUnavailable; }
    TGuardValue<bool> Guard(bRaidTransitionInProgress, true);
    FActorSpawnParameters Spawn;
    Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    for (AWorldLootActor* Template : RaidLootTemplates)
    {
        if (!IsValid(Template)) { continue; }
        AWorldLootActor* Loot = GetWorld()->SpawnActor<AWorldLootActor>(
            Template->GetActorLocation(), Template->GetActorRotation(), Spawn);
        if (!Loot)
        {
            DestroyRaidLoot();
            return ERaidDemoFailure::WorldUnavailable;
        }
        Loot->DefinitionTable = Template->DefinitionTable;
        Loot->DefinitionRowName = Template->DefinitionRowName;
        Loot->Quantity = Template->Quantity;
        RaidLoot.Add(Loot);
    }
    if (Screen && Screen->IsActivated()) { Screen->DeactivateWidget(); }
    RaidPhase = ERaidDemoPhase::InRaid;
    if (Screen) { Screen->SetStashAccessible(false); }
    ExtractionPoint->SetActorHiddenInGame(false);
    UpdateRaidStatus();
    return ERaidDemoFailure::None;
}

ERaidDemoFailure AInventoryDemoPlayerController::TryExtractRaid()
{
    if (bRaidTransitionInProgress) { return ERaidDemoFailure::OperationInProgress; }
    if (RaidPhase != ERaidDemoPhase::InRaid) { return ERaidDemoFailure::InvalidPhase; }
    const APawn* ControlledPawn = GetPawnOrSpectator();
    if (!Model || !ControlledPawn || !IsValid(ExtractionPoint)) { return ERaidDemoFailure::WorldUnavailable; }
    if (FVector::DistSquared2D(ControlledPawn->GetActorLocation(), ExtractionPoint->GetActorLocation()) > FMath::Square(180.0))
    {
        return ERaidDemoFailure::OutsideExtraction;
    }
    TGuardValue<bool> Guard(bRaidTransitionInProgress, true);
    LastExtractionFailure = Model->TryTransferAll(TEXT("Bag"), TEXT("Stash"));
    if (LastExtractionFailure != EInventoryOperationFailure::None) { return ERaidDemoFailure::InventoryBlocked; }
    if (Screen && Screen->IsActivated()) { Screen->DeactivateWidget(); }
    DestroyRaidLoot();
    RaidPhase = ERaidDemoPhase::Preparation;
    ++CompletedRaidCount;
    if (Screen) { Screen->SetStashAccessible(true); }
    ExtractionPoint->SetActorHiddenInGame(true);
    UpdateRaidStatus(NSLOCTEXT("RaidDemo", "ExtractionSucceeded", "탈출 성공: 가방의 아이템을 보관함에 반영했습니다"));
    return ERaidDemoFailure::None;
}

void AInventoryDemoPlayerController::DestroyRaidLoot()
{
    for (AWorldLootActor* Loot : RaidLoot)
    {
        if (IsValid(Loot) && !Loot->IsActorBeingDestroyed()) { Loot->Destroy(); }
    }
    RaidLoot.Reset();
}

void AInventoryDemoPlayerController::UpdateRaidStatus(const FText& Notice)
{
    if (!RaidStatus) { return; }
    const FText Instructions = RaidPhase == ERaidDemoPhase::InRaid
        ? NSLOCTEXT("RaidDemo", "RaidInstructions", "Raid | E: loot 획득 · I: 가방\n큰 원기둥 주변에서 F6: 탈출")
        : NSLOCTEXT("RaidDemo", "PreparationInstructions", "준비 | I: 보관함과 가방 정리 · F5: Raid 진입");
    RaidStatus->SetStatus(FText::Format(NSLOCTEXT("RaidDemo", "Status", "{0}\n완료한 Raid: {1}  {2}"),
        Instructions, FText::AsNumber(CompletedRaidCount), Notice));
}

void AInventoryDemoPlayerController::ShowRaidResult(ERaidDemoFailure Result)
{
    FText Notice;
    switch (Result)
    {
    case ERaidDemoFailure::None: return;
    case ERaidDemoFailure::InvalidPhase:
        Notice = NSLOCTEXT("RaidDemo", "InvalidPhase", "현재 상태에서는 실행할 수 없습니다"); break;
    case ERaidDemoFailure::OperationInProgress:
        Notice = NSLOCTEXT("RaidDemo", "InProgress", "다른 Raid 작업이 진행 중입니다"); break;
    case ERaidDemoFailure::WorldUnavailable:
        Notice = NSLOCTEXT("RaidDemo", "WorldUnavailable", "Raid 데모에 필요한 대상을 찾을 수 없습니다"); break;
    case ERaidDemoFailure::OutsideExtraction:
        Notice = NSLOCTEXT("RaidDemo", "OutsideExtraction", "탈출 지점의 큰 원기둥에 더 가까이 이동하세요"); break;
    case ERaidDemoFailure::InventoryBlocked:
        Notice = LastExtractionFailure == EInventoryOperationFailure::NoSpace
            ? NSLOCTEXT("RaidDemo", "StashFull", "보관함 공간이 부족합니다. 가방과 Raid 상태를 유지합니다")
            : NSLOCTEXT("RaidDemo", "InventoryBlocked", "보관함 반영에 실패했습니다. 아이템을 유지합니다"); break;
    }
    UpdateRaidStatus(Notice);
    UE_LOG(LogTemp, Display, TEXT("Raid 데모: %s"), *Notice.ToString());
}
