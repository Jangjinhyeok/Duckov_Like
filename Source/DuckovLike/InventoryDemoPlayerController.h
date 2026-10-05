#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "InventoryOperationTypes.h"
#include "TimerManager.h"
#include "InventoryDemoPlayerController.generated.h"

class UInventoryModel;
class UInventoryScreenWidget;
class AWorldLootActor;
class AStaticMeshActor;
class URaidStatusWidget;
class UCustomizationModel;
class UCustomizationScreenWidget;

UENUM(BlueprintType)
enum class ERaidDemoPhase : uint8
{
    Disabled,
    Preparation,
    InRaid,
};

UENUM(BlueprintType)
enum class ERaidDemoFailure : uint8
{
    None,
    InvalidPhase,
    OperationInProgress,
    WorldUnavailable,
    OutsideExtraction,
    InventoryBlocked,
};

UCLASS()
class DUCKOVLIKE_API AInventoryDemoPlayerController : public APlayerController
{
    GENERATED_BODY()
public:
    virtual void BeginPlay() override;
    virtual void SetupInputComponent() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    UFUNCTION(BlueprintCallable, Category="Inventory Demo")
    void ToggleInventory();
    UFUNCTION(BlueprintCallable, Category="Inventory Demo")
    EInventoryOperationFailure TryPickupLoot(AWorldLootActor* Loot);
    UFUNCTION(Exec)
    void PickupNearestLoot();
    UFUNCTION(Exec)
    void StartRaidDemo();
    UFUNCTION(Exec)
    void StartBagEquipmentDemo();
    UFUNCTION(Exec)
    void OpenCustomizationPrototype();
    UFUNCTION(Exec)
    void EnterRaid();
    UFUNCTION(Exec)
    void ExtractRaid();
    ERaidDemoFailure TryStartRaidDemo();
    EInventoryOperationFailure TryStartBagEquipmentDemo();
    ERaidDemoFailure TryEnterRaid();
    ERaidDemoFailure TryExtractRaid();
    ERaidDemoPhase GetRaidPhase() const { return RaidPhase; }
    int32 GetCompletedRaidCount() const { return CompletedRaidCount; }
private:
    UPROPERTY(Transient)
    TObjectPtr<UInventoryModel> Model;
    UPROPERTY(Transient)
    TObjectPtr<UInventoryScreenWidget> Screen;
    UPROPERTY(Transient)
    TObjectPtr<URaidStatusWidget> RaidStatus;
    UPROPERTY(Transient)
    TObjectPtr<UCustomizationModel> CustomizationModel;
    UPROPERTY(Transient)
    TObjectPtr<UCustomizationScreenWidget> CustomizationScreen;
    UPROPERTY(Transient)
    TObjectPtr<AStaticMeshActor> ExtractionPoint;
    UPROPERTY(Transient)
    TArray<TObjectPtr<AWorldLootActor>> RaidLootTemplates;
    UPROPERTY(Transient)
    TArray<TObjectPtr<AWorldLootActor>> RaidLoot;
    ERaidDemoPhase RaidPhase = ERaidDemoPhase::Disabled;
    EInventoryOperationFailure LastExtractionFailure = EInventoryOperationFailure::None;
    int32 CompletedRaidCount = 0;
    bool bRaidTransitionInProgress = false;
    FTimerHandle RestoreInputHandle;
    void DestroyRaidLoot();
    void UpdateRaidStatus(const FText& Notice = FText::GetEmpty());
    void ShowRaidResult(ERaidDemoFailure Result);
    void OnScreenDeactivated();
    void RestoreGameInput();
};
