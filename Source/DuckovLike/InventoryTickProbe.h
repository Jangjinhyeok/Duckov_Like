#pragma once

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"

class UInventoryScreenWidget;
class UInventoryGridWidget;
class UInventoryItemWidget;

// PIE 자동 검사 중 실제 Inventory UserWidget::Tick 진입만 기록한다.
struct FInventoryTickProbe
{
    const UInventoryScreenWidget* Screen = nullptr;
    TSet<const UInventoryGridWidget*> Grids;
    TSet<const UInventoryItemWidget*> Items;
    int32 ScreenCalls = 0;
    int32 GridCalls = 0;
    int32 ItemCalls = 0;
    TMap<const UInventoryGridWidget*, int32> GridCallsByWidget;
    TMap<const UInventoryItemWidget*, int32> ItemCallsByWidget;

    void Reset()
    {
        ScreenCalls = GridCalls = ItemCalls = 0;
        GridCallsByWidget.Reset();
        ItemCallsByWidget.Reset();
    }
};

extern FInventoryTickProbe* GInventoryTickProbe;

#endif
