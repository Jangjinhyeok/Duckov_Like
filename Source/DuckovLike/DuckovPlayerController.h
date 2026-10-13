#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"
#include "DuckovPlayerController.generated.h"

class UCommonActivatableWidget;
class UCustomizationModel;
class UCustomizationScreenWidget;
class UGameRootLayoutWidget;
class UInteractionPromptWidget;
class UInteractionRangeComponent;
class UInventoryModel;
class UInventoryScreenWidget;
class ULoadingScreenWidget;

// 베이스먼트와 레이드 맵의 controller다. 세션 인벤토리를 화면과 월드 오브젝트에 잇는다.
// 지상 캐릭터가 상호작용 범위에 들어오면 F로 창고 열기·출격·loot 획득·탈출을 한다. I는 가방만 연다.
// 인벤토리 상태는 세션 Model이, 레이드 여부는 맵에 놓인 오브젝트가 정하므로 이 controller는 단계를 저장하지 않는다.
UCLASS()
class DUCKOVLIKE_API ADuckovPlayerController : public APlayerController
{
    GENERATED_BODY()
public:
    virtual void BeginPlay() override;
    virtual void SetupInputComponent() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    UFUNCTION(Exec)
    void ToggleInventory();
    UFUNCTION(Exec)
    void Interact();
    UFUNCTION(Exec)
    void OpenCustomization();
    // 범위 안의 대상 중 가장 최근에 들어온 것이다. 없으면 null이다.
    UInteractionRangeComponent* GetInteractionTarget() const;
protected:
    virtual void OnPossess(APawn* InPawn) override;
    virtual void OnUnPossess() override;
private:
    UPROPERTY(Transient)
    TObjectPtr<UInventoryModel> Model;
    UPROPERTY(Transient)
    TObjectPtr<UGameRootLayoutWidget> RootLayout;
    UPROPERTY(Transient)
    TObjectPtr<UInventoryScreenWidget> Screen;
    UPROPERTY(Transient)
    TObjectPtr<UCustomizationModel> CustomizationModel;
    UPROPERTY(Transient)
    TObjectPtr<UCustomizationScreenWidget> CustomizationScreen;
    UPROPERTY(Transient)
    TObjectPtr<UInteractionPromptWidget> Prompt;
    UPROPERTY(Transient)
    TObjectPtr<ULoadingScreenWidget> LoadingScreen;
    // 범위에 들어온 순서다.
    TArray<TWeakObjectPtr<UInteractionRangeComponent>> Targets;
    FTimerHandle RestoreInputHandle;
    UFUNCTION()
    void OnPawnBeginOverlap(AActor* OverlappedActor, AActor* OtherActor);
    UFUNCTION()
    void OnPawnEndOverlap(AActor* OverlappedActor, AActor* OtherActor);
    void OpenInventory(bool bStashAccessible);
    void PickupLoot(UInteractionRangeComponent* Target);
    void Extract();
    void StartTravel(FName Map);
    bool IsMenuOpen() const;
    void RefreshPrompt();
    void OnMenuChanged(UCommonActivatableWidget* DisplayedMenu);
    void RestoreGameInput();
};
