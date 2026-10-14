#pragma once

#include "CoreMinimal.h"

class UBorder;
class UTextBlock;
class UWidgetTree;

// 원작 HUD에서 반복되는 작은 요소를 같은 모양으로 만든다.
namespace DuckovHudStyle
{
// 흰 둥근 사각 위에 어두운 글자로 쓴 키 배지([F], [O] 등).
UBorder* MakeKeyBadge(UWidgetTree& Tree, const FText& Key);
// 배경 없이 그림자만 있는 HUD 글자.
UTextBlock* MakeShadowText(UWidgetTree& Tree, FName Name, const FText& Text, int32 Size);
}
