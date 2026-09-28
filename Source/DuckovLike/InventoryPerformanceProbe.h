#pragma once

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"

class UCanvasPanel;
class UInventoryGridWidget;
class UInventoryItemWidget;

// 에디터 자동 검사 중 Stash grid의 실제 위젯 호출만 기록한다.
struct FInventoryPerformanceProbe
{
    UCanvasPanel* Canvas = nullptr;
    TSet<const UInventoryItemWidget*> TrackedWidgets;
    int32 GridRefresh = 0;
    int32 ItemRefresh = 0;
    int32 BoundItemRefresh = 0;
    int32 UnboundItemRefresh = 0;
    int32 ItemCreated = 0;
    int32 ItemRemoved = 0;
    TMap<const UInventoryItemWidget*, int32> RefreshByWidget;

    void Reset()
    {
        GridRefresh = ItemRefresh = ItemCreated = ItemRemoved = 0;
        BoundItemRefresh = UnboundItemRefresh = 0;
        RefreshByWidget.Reset();
    }
};

extern FInventoryPerformanceProbe* GInventoryPerformanceProbe;

#endif
