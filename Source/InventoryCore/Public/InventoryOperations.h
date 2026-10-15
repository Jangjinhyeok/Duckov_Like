#pragma once

#include "CoreMinimal.h"
#include "InventoryOperationTypes.h"

struct FInventoryContainer;

struct INVENTORYCORE_API FInventoryOperations
{
    // Model을 변경하지 않는 프리뷰 조회. TryMove도 동일 검증을 사용한다.
    static EInventoryOperationFailure CanMove(
        const FInventoryContainer& SourceContainer,
        const FInventoryContainer& DestContainer,
        int32 InstanceId, FIntPoint DestAnchorCell, bool bDestRotated);

    // Model을 변경하지 않는 병합 검증. TryStack도 동일 검증을 사용한다.
    static EInventoryOperationFailure CanStack(
        const FInventoryContainer& SourceContainer,
        const FInventoryContainer& DestContainer,
        int32 SourceInstanceId, int32 TargetInstanceId);

    // 배치 예정 칸이 옮기는 항목이 아닌 다른 항목 하나와만 겹치면 그 InstanceId, 아니면 INDEX_NONE.
    // 병합할지는 호출자가 정한다(설계결정기록 012).
    static int32 FindSingleOverlap(
        const FInventoryContainer& SourceContainer,
        const FInventoryContainer& DestContainer,
        int32 InstanceId, FIntPoint DestAnchorCell, bool bDestRotated);

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
