#include "ItemInstance.h"

namespace
{
int32 NextInstanceId = 0;
}

int32 FItemInstanceIdAllocator::AllocateNextInstanceId()
{
    return NextInstanceId++;
}

void FItemInstanceIdAllocator::ResetInstanceIdCounter_ForTests(const int32 StartValue)
{
    NextInstanceId = StartValue;
}
