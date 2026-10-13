#pragma once

#include "CoreMinimal.h"
#include "InventoryOperationTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GameSessionSubsystem.generated.h"

class UCustomizationModel;
class UInventoryModel;

// 레벨 이동(타이틀 → 베이스먼트 → 레이드)을 넘어 유지해야 하는 플레이 세션 상태를 소유한다.
// 디스크 저장은 하지 않으며 게임 인스턴스가 끝나면 함께 사라진다.
UCLASS()
class DUCKOVLIKE_API UGameSessionSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    UCustomizationModel* GetCustomizationModel();
    // 처음 요청할 때 시작 인벤토리(보관함, 소형 가방 장착)로 만든다. 시작 데이터를 불러오지 못하면 null이다.
    UInventoryModel* GetInventoryModel();
    // 레이드에서 탈출할 때 남기고, 돌아온 베이스먼트가 한 번 꺼내 결과를 알린다.
    void SetExtractionResult(EInventoryOperationFailure Result) { PendingExtractionResult = Result; }
    TOptional<EInventoryOperationFailure> ConsumeExtractionResult();
private:
    UPROPERTY(Transient)
    TObjectPtr<UCustomizationModel> CustomizationModel;
    UPROPERTY(Transient)
    TObjectPtr<UInventoryModel> InventoryModel;
    TOptional<EInventoryOperationFailure> PendingExtractionResult;
};
