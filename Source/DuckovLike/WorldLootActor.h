#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InventoryOperationTypes.h"
#include "WorldLootActor.generated.h"

class UDataTable;
class UInventoryModel;
class UStaticMeshComponent;

UCLASS()
class DUCKOVLIKE_API AWorldLootActor : public AActor
{
    GENERATED_BODY()
public:
    AWorldLootActor();

    UPROPERTY(EditAnywhere, Category="Loot")
    TSoftObjectPtr<UDataTable> DefinitionTable;
    UPROPERTY(EditAnywhere, Category="Loot")
    FName DefinitionRowName = NAME_None;
    UPROPERTY(EditAnywhere, Category="Loot", meta=(ClampMin="1"))
    int32 Quantity = 1;

private:
    friend class AInventoryDemoPlayerController;
    UPROPERTY(VisibleAnywhere, Category="Loot")
    TObjectPtr<UStaticMeshComponent> Mesh;
    bool bPickupInProgress = false;
    bool bConsumed = false;

    EInventoryOperationFailure TryPickup(UInventoryModel* Model, FName ContainerId);
};
