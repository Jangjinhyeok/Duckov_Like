#pragma once

#include "CoreMinimal.h"
#include "InventoryOperationTypes.h"

struct FInventoryContainer;

struct INVENTORYCORE_API FInventoryOperations
{
	static EInventoryOperationFailure TryMove(
		FInventoryContainer& SourceContainer,
		FInventoryContainer& DestContainer,
		int32 InstanceId,
		FIntPoint DestAnchorCell,
		bool bDestRotated);
};
