#pragma once

#include "CoreMinimal.h"
#include "Components/SphereComponent.h"
#include "InteractionRangeComponent.generated.h"

UENUM()
enum class EInteractionKind : uint8
{
    Stash,
    Sortie,
    Extraction,
    Loot,
};

// 플레이어 pawn이 이 범위에 들어오면 controller가 상호작용 대상으로 삼는다.
// 범위와 종류만 가진다. 무엇을 할지는 Kind를 보고 controller가 정한다.
UCLASS(ClassGroup=(Duckov), meta=(BlueprintSpawnableComponent))
class DUCKOVLIKE_API UInteractionRangeComponent : public USphereComponent
{
    GENERATED_BODY()
public:
    UInteractionRangeComponent();
    UPROPERTY(EditAnywhere, Category="Interaction")
    EInteractionKind Kind = EInteractionKind::Loot;
};
