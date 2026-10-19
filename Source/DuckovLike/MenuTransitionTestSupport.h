#pragma once

#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "GameRootLayoutWidget.h"
#include "UObject/UnrealType.h"
#include "Widgets/CommonActivatableWidgetContainer.h"

// 화면 전환이 아니라 Model·ViewModel 흐름을 보는 PIE 테스트는 같은 프레임에서 화면을 닫고 다시 연다.
// 그런 테스트의 layout에서만 Menu stack 전환(설계결정기록 036)을 꺼 즉시 전환으로 확인한다.
// 전환 자체는 Duckov.UI.LayerStack과 Duckov.Title.FlowToBasement가 검증한다.
inline bool DisableMenuTransitionForTest(UObject* Controller)
{
    const FObjectProperty* Property = Controller ? FindFProperty<FObjectProperty>(Controller->GetClass(), TEXT("RootLayout")) : nullptr;
    UGameRootLayoutWidget* Layout = Property ? Cast<UGameRootLayoutWidget>(Property->GetObjectPropertyValue_InContainer(Controller)) : nullptr;
    if (!Layout || !Layout->GetMenuStack()) { return false; }
    Layout->GetMenuStack()->SetTransitionDuration(0.f);
    return true;
}
#endif
