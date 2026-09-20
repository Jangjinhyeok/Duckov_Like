#include "ItemInstance.h"

namespace
{
int32 NextInstanceId = 0;
}

int32 FItemInstanceIdAllocator::AllocateNextInstanceId()
{
    check(IsInGameThread());
    if (NextInstanceId < 0 || NextInstanceId == MAX_int32)
    {
        return INDEX_NONE;
    }
    return NextInstanceId++;
}

int32 FItemInstanceIdAllocator::GetNextInstanceId()
{
    check(IsInGameThread());
    return NextInstanceId;
}

void FItemInstanceIdAllocator::AdvanceInstanceIdCounter(const int32 SavedNextInstanceId)
{
    check(IsInGameThread());
    NextInstanceId = FMath::Max(NextInstanceId, SavedNextInstanceId);
}

void FItemInstanceIdAllocator::ResetInstanceIdCounter_ForTests(const int32 StartValue)
{
    NextInstanceId = StartValue;
}
