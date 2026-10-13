#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "BasementGameMode.generated.h"

// 베이스먼트와 레이드 맵이 함께 쓴다. 지상 캐릭터로 걷고, 맵에 놓인 창고·출격·탈출 지점과 loot로 루프를 잇는다.
// 두 맵의 차이는 놓인 오브젝트뿐이라 GameMode를 나누지 않는다.
UCLASS()
class DUCKOVLIKE_API ABasementGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    ABasementGameMode();
};
