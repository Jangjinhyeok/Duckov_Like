#pragma once

#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Components/Widget.h"
#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"

// 화면 가운데를 실제 Slate 마우스 입력으로 누르고 뗀다. 맨 위 widget을 hit test로 찾으므로
// 도착 화면이 HUD·게임 위에서 클릭을 받는지까지 확인한다. 누름을 어떤 widget이 처리했는지 돌려준다.
inline bool ClickScreenCenter(const UWidget* Widget)
{
    if (!Widget) { return false; }
    FSlateApplication& Slate = FSlateApplication::Get();
    const FVector2D Position = Widget->GetCachedGeometry().GetAbsolutePositionAtCoordinates(FVector2D(0.5f, 0.5f));
    TSet<FKey> Pressed;
    Pressed.Add(EKeys::LeftMouseButton);
    const FPointerEvent Down(Slate.GetUserIndexForMouse(), FSlateApplication::CursorPointerIndex, Position, Position,
        Pressed, EKeys::LeftMouseButton, 0.f, FModifierKeysState());
    const bool bHandled = Slate.ProcessMouseButtonDownEvent(nullptr, Down);
    const FPointerEvent Up(Slate.GetUserIndexForMouse(), FSlateApplication::CursorPointerIndex, Position, Position,
        TSet<FKey>(), EKeys::LeftMouseButton, 0.f, FModifierKeysState());
    Slate.ProcessMouseButtonUpEvent(Up);
    return bHandled;
}
#endif
