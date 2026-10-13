#include "InteractionRangeComponent.h"

UInteractionRangeComponent::UInteractionRangeComponent()
{
    InitSphereRadius(150.f);
    // pawn과의 overlap만 만든다. 이동이나 trace를 막지 않고, 부모의 크기 조절과 무관하게 같은 반경을 쓴다.
    SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    SetCollisionObjectType(ECC_WorldDynamic);
    SetCollisionResponseToAllChannels(ECR_Ignore);
    SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
    SetGenerateOverlapEvents(true);
    SetUsingAbsoluteScale(true);
    SetCanEverAffectNavigation(false);
}
