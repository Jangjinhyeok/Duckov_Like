#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "TitleGameMode.generated.h"

// 타이틀 맵 전용이다. 조작할 pawn 없이 화면만 띄운다.
UCLASS()
class DUCKOVLIKE_API ATitleGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    ATitleGameMode();
};
