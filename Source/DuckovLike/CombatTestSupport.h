#pragma once

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "TimerManager.h"

namespace CombatTestSupport
{
// 사람이 눈으로 볼 검증용 그림을 Saved/Automation/RifleHold에 PNG로 남긴다. 저장 결과는 info로만 알리고 판정에 쓰지 않는다.
inline void SaveEvidencePng(FAutomationTestBase& Test, const TArray<FColor>& Pixels, const FIntPoint& Size, const TCHAR* Name)
{
    const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation/RifleHold"), FString(Name) + TEXT(".png"));
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
    TArray64<uint8> Compressed;
    if (Size.X > 0 && Size.Y > 0 && Pixels.Num() == static_cast<int64>(Size.X) * Size.Y)
    {
        FImageUtils::PNGCompressImageArray(Size.X, Size.Y, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), Compressed);
    }
    const bool bSaved = !Compressed.IsEmpty() && FFileHelper::SaveArrayToFile(Compressed, *Path);
    Test.AddInfo(FString::Printf(TEXT("%s %s"), bSaved ? TEXT("generated") : TEXT("not_run"), *Path));
}

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

// 겹쳐도 그 자리에 놓는다. 판정 거리를 정확히 맞추려고 위치를 밀어내지 않는다.
template <typename T>
T* SpawnAt(UWorld* World, const FVector& Location)
{
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    return World->SpawnActor<T>(Location, FRotator::ZeroRotator, Params);
}
}
#endif
