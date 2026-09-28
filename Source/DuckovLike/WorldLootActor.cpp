#include "WorldLootActor.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "InventoryModel.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

AWorldLootActor::AWorldLootActor()
{
    PrimaryActorTick.bCanEverTick = false;
    Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LootMesh"));
    SetRootComponent(Mesh);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Mesh->SetRelativeScale3D(FVector(0.3f));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(
        TEXT("/Engine/EngineDebugMaterials/DebugMeshMaterialFakeLight.DebugMeshMaterialFakeLight"));
    if (Cube.Succeeded()) { Mesh->SetStaticMesh(Cube.Object); }
    if (Material.Succeeded()) { Mesh->SetMaterial(0, Material.Object); }
}

EInventoryOperationFailure AWorldLootActor::TryPickup(UInventoryModel* Model, FName ContainerId)
{
    if (bConsumed || IsActorBeingDestroyed()) { return EInventoryOperationFailure::ItemNotFound; }
    if (bPickupInProgress) { return EInventoryOperationFailure::OperationInProgress; }
    if (!IsValid(Model)) { return EInventoryOperationFailure::InvalidContainer; }
    TGuardValue<bool> Guard(bPickupInProgress, true);
    const EInventoryOperationFailure Result = Model->TryAdd(ContainerId, DefinitionTable, DefinitionRowName, Quantity);
    if (Result != EInventoryOperationFailure::None) { return Result; }

    // ChangeSet callback 이후에도 같은 loot를 다시 획득할 수 없게 한다.
    bConsumed = true;
    SetActorHiddenInGame(true);
    SetActorEnableCollision(false);
    Destroy();
    return EInventoryOperationFailure::None;
}
