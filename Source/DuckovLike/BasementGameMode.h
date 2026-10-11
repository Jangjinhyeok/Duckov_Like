#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "BasementGameMode.generated.h"

// 베이스먼트 맵 전용이다. 지상 캐릭터로 걷고, 인벤토리·커스터마이징은 기존 controller가 연다.
UCLASS()
class DUCKOVLIKE_API ABasementGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    ABasementGameMode();
};
