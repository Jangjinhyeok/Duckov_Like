#pragma once

#include "CoreMinimal.h"
#include "InventoryOperationTypes.h"

struct FInventoryContainer;

struct INVENTORYCORE_API FInventoryOperations
{
	// 면적 내림차순, InstanceId 오름차순으로 재배치하며 현재 회전을 유지한다.
	// 전체 배치가 불가능하면 NoSpace를 반환하고 원본을 보존한다.
	static EInventoryOperationFailure TrySort(FInventoryContainer& Container);

	static EInventoryOperationFailure TryMove(
		FInventoryContainer& SourceContainer,
		FInventoryContainer& DestContainer,
		int32 InstanceId,
		FIntPoint DestAnchorCell,
		bool bDestRotated);

	static EInventoryOperationFailure TryStack(
		FInventoryContainer& SourceContainer,
		FInventoryContainer& DestContainer,
		int32 SourceInstanceId,
		int32 TargetInstanceId);
};
