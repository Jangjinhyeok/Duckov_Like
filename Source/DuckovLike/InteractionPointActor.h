#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InteractionPointActor.generated.h"

class UInteractionRangeComponent;
class UStaticMeshComponent;

// 창고·출격 지점·탈출 지점처럼 맵에 놓이는 상호작용 대상이다. 상태를 갖지 않는다.
// mesh 크기를 바꿔도 상호작용 반경은 그대로다.
UCLASS()
class DUCKOVLIKE_API AInteractionPointActor : public AActor
{
    GENERATED_BODY()
public:
    AInteractionPointActor();
    UStaticMeshComponent* GetMesh() const { return Mesh; }
    UInteractionRangeComponent* GetRange() const { return Range; }
private:
    UPROPERTY(VisibleAnywhere, Category="Interaction")
    TObjectPtr<UStaticMeshComponent> Mesh;
    UPROPERTY(VisibleAnywhere, Category="Interaction")
    TObjectPtr<UInteractionRangeComponent> Range;
};
