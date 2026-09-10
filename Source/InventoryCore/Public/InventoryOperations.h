#pragma once

#include "CoreMinimal.h"
#include "InventoryOperationTypes.h"

struct FInventoryContainer;

struct INVENTORYCORE_API FInventoryOperations
{
	// 면적 내림차순, InstanceId 오름차순으로 재배치하며 현재 회전을 유지한다.
	// 전체 배치가 불가능하면 NoSpace를 반환하고 원본을 보존한다.
	static EInventoryOperationFailure TrySort(FInventoryContainer& Container);

	// 현재 배치와 배열 순서를 유지한 채 크기를 변경한다.
	// 크기가 유효하지 않거나 모든 항목을 유지할 수 없으면 ResizeOverflow와 함께 원본을 보존한다.
	static EInventoryOperationFailure TryResize(FInventoryContainer& Container, FIntPoint NewGridSize);

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
