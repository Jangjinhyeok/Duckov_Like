#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GameSessionSubsystem.generated.h"

class UCustomizationModel;

// 레벨 이동(타이틀 → 베이스먼트 → 레이드)을 넘어 유지해야 하는 플레이 세션 상태를 소유한다.
// 디스크 저장은 하지 않으며 게임 인스턴스가 끝나면 함께 사라진다.
UCLASS()
class DUCKOVLIKE_API UGameSessionSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    UCustomizationModel* GetCustomizationModel();
private:
    UPROPERTY(Transient)
    TObjectPtr<UCustomizationModel> CustomizationModel;
};
