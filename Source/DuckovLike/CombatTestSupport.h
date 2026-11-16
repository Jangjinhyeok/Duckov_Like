#pragma once

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "TimerManager.h"

namespace CombatTestSupport
{
// BeginPlay와 timer가 도는 transient world다. GameMode 없이 world settings로 시작을 알린다.
struct FPlayWorld
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    FPlayWorld()
    {
        World->InitializeActorsForPlay(FURL());
        World->GetWorldSettings()->NotifyBeginPlay();
        // 첫 tick 전에 건 timer는 pending으로 있다가 다음 tick 끝에야 시작한다. 한 번 돌려 두면 이후 timer는 건 시점부터 센다.
        Advance(0.f);
    }
    ~FPlayWorld()
    {
        World->EndPlay(EEndPlayReason::EndPlayInEditor);
        World->DestroyWorld(false);
        if (World->IsRooted()) { World->RemoveFromRoot(); }
    }
    // world 시간과 timer를 함께 넘긴다. FTimerManager::Tick은 frame마다 한 번만 돌므로 frame 번호를 먼저 올린다.
    // 엔진 전역 값을 바꾸지만 증가만 하고, 이 값을 읽는 다른 테스트는 자기 실행 구간의 차이만 본다.
    void Advance(float Seconds)
    {
        ++GFrameCounter;
        World->TimeSeconds += Seconds;
        World->GetTimerManager().Tick(Seconds);
    }
};

}
#endif
