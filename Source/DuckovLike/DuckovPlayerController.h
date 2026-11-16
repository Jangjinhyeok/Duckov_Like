#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"
#include "DuckovPlayerController.generated.h"

class UCommonActivatableWidget;
class UCustomizationModel;
class UCustomizationScreenWidget;
class UGameHudWidget;
class UGameRootLayoutWidget;
class UInteractionRangeComponent;
class UInventoryModel;
class UInventoryScreenWidget;
class ULoadingScreenWidget;
class UWidgetComponent;
struct FInventoryChangeSet;

// 베이스먼트와 레이드 맵의 controller다. 세션 인벤토리를 화면과 월드 오브젝트에 잇는다.
// 지상 캐릭터가 상호작용 범위에 들어오면 F로 창고 열기·출격·loot 획득·탈출을 한다. O는 조작 설명을 펼친다.
// I는 원작처럼 베이스먼트에서는 창고까지, 레이드에서는 가방만 연다.
// 게임 입력 중 마우스 커서는 십자선이며, 커서가 가리키는 지점을 캐릭터의 조준 지점으로 넘긴다.
// 인벤토리 상태는 세션 Model이 소유한다. 레이드 여부는 맵에서 정하며 이 controller는 단계를 바꾸지 않는다.
UCLASS()
class DUCKOVLIKE_API ADuckovPlayerController : public APlayerController
{
    GENERATED_BODY()
public:
    virtual void BeginPlay() override;
    virtual void SetupInputComponent() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void PlayerTick(float DeltaTime) override;
    // 화면 좌표를 빙의한 pawn 높이의 수평면 위 지점으로 바꾼다. 그 면과 만나지 않으면 false다.
    bool ScreenToAimPoint(const FVector2D& ScreenPosition, FVector& OutPoint) const;
    UFUNCTION(Exec)
    void ToggleInventory();
    UFUNCTION(Exec)
    void Interact();
    UFUNCTION(Exec)
    void OpenCustomization();
    UFUNCTION(Exec)
    void ToggleControls();
    // 테스트용: L로 빙의한 캐릭터의 레이저 사이트 선을 켜고 끈다. 레이저 사이트 장비가 생기면 장착 상태로 대체한다.
    UFUNCTION(Exec)
    void ToggleLaserSight();
    // LMB: 무기 slot에 총이 있으면 탄창에서 한 발을 쏘고, 비어 있으면 맨손 근접 공격을 한다.
    // 잔탄이 없거나 재장전 중이면 쏘지 않는다. 메뉴가 열려 있거나 맵 이동·도착 대기 중이면 무시한다.
    UFUNCTION(Exec)
    void Attack();
    // R: 장착한 총을 재장전한다. ReloadDuration이 지나는 순간 가방의 Ammo를 한 번에 소모해 탄창을 채운다.
    // 그 사이 무기를 해제하거나 바꾸면 취소되고 아무것도 소모하지 않는다. 창고의 탄약은 쓰지 않는다.
    UFUNCTION(Exec)
    void Reload();
    bool IsReloading() const { return ReloadingWeaponId != INDEX_NONE; }
    static constexpr float ReloadDuration = 1.5f;
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
    TObjectPtr<UGameHudWidget> Hud;
    // 상호작용 대상 위치에 붙는 프롬프트 하나. 대상이 있을 때만 보이고 위치를 갱신한다.
    // controller는 숨은 actor라 screen space widget이 그려지지 않으므로 빙의한 pawn이 소유한다.
    UPROPERTY(Transient)
    TObjectPtr<UWidgetComponent> PromptComponent;
    UPROPERTY(Transient)
    TObjectPtr<ULoadingScreenWidget> LoadingScreen;
    // 범위에 들어온 순서다.
    TArray<TWeakObjectPtr<UInteractionRangeComponent>> Targets;
    FTimerHandle RestoreInputHandle;
    // 출발 화면이 다 덮인 뒤 이동할 맵이다.
    FName TravelMap;
    FTimerHandle TravelHandle;
    bool bInRaid = false;
    // 재장전 중인 무기의 InstanceId다. 재장전 중이 아니면 INDEX_NONE이다.
    int32 ReloadingWeaponId = INDEX_NONE;
    FTimerHandle ReloadTimer;
    FDelegateHandle InventoryChangedHandle;
    UFUNCTION()
    void OnPawnBeginOverlap(AActor* OverlappedActor, AActor* OtherActor);
    UFUNCTION()
    void OnPawnEndOverlap(AActor* OverlappedActor, AActor* OtherActor);
    void OpenInventory(bool bStashAccessible);
    void PickupLoot(UInteractionRangeComponent* Target);
    void Extract();
    void StartTravel(FName Map);
    void OnDepartureCovered();
    void TravelAfterDeparture();
    void OnArrivalContinued();
    bool IsMenuOpen() const;
    // 무기 slot에 장착한 항목의 InstanceId다. 비어 있으면 INDEX_NONE이다.
    int32 GetEquippedWeaponId() const;
    void RefreshAimPoint();
    int32 CountBagAmmo() const;
    void FinishReload();
    void CancelReload();
    void OnInventoryChanged(const FInventoryChangeSet& Change);
    void RefreshAmmoHud();
    void RefreshHud();
    void DestroyPromptComponent();
    void OnMenuChanged(UCommonActivatableWidget* DisplayedMenu);
    void RestoreGameInput();
};
