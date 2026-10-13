#include "InteractionPointActor.h"

#include "Components/StaticMeshComponent.h"
#include "InteractionRangeComponent.h"

AInteractionPointActor::AInteractionPointActor()
{
    PrimaryActorTick.bCanEverTick = false;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
    Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
    Mesh->SetupAttachment(RootComponent);
    Range = CreateDefaultSubobject<UInteractionRangeComponent>(TEXT("Range"));
    Range->SetupAttachment(RootComponent);
}
