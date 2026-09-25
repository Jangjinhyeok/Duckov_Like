#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"
#include "InventoryDemoPlayerController.generated.h"

class UInventoryModel;
class UInventoryScreenWidget;

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
private:
    UPROPERTY(Transient)
    TObjectPtr<UInventoryModel> Model;
    UPROPERTY(Transient)
    TObjectPtr<UInventoryScreenWidget> Screen;
    FTimerHandle RestoreInputHandle;
    void OnScreenDeactivated();
    void RestoreGameInput();
};
