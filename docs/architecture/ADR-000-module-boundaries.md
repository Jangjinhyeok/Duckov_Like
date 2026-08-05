# ADR-000: 모듈 경계 — 빌드 시스템으로 Model/View 분리를 강제한다

| 항목 | 내용 |
| --- | --- |
| 상태 | Proposed |
| 날짜 | 2026-08-05 |
| 마일스톤 | M0.5 (스캐폴딩) |

> 이 ADR은 인벤토리 도메인 결정(ADR-001~008)보다 **앞서는 기반 결정**이라 000번을 쓴다.
> 인벤토리 아키텍처 항목(A1~E3)은 여전히 전부 `Proposed`이며, 이 ADR은 그중 어느 것도 확정하지 않는다.

## 맥락

GDD의 아키텍처 원칙은 `UMG View -> ViewModel -> Model` 단방향 의존을 요구하고,
인벤토리 설계 문서의 INV-08은 "View와 ViewModel은 위치나 수량의 원본 상태를 소유하지 않는다"를
불변식으로 못박는다.

문제는 **이 원칙이 규율로만 존재하면 지켜지는지 확인할 방법이 없다**는 것이다.
단일 모듈 프로젝트에서는 Model 코드가 `#include "Blueprint/UserWidget.h"`를 하고 위젯을 직접
조작해도 아무 일도 일어나지 않는다. 빌드는 통과하고, 리뷰에서 놓치면 그대로 남는다.
포트폴리오 심사자에게 "지켰습니다"라고 말할 수는 있어도 **증명할 수는 없다.**

## 결정

게임 코드를 두 개의 Runtime 모듈로 분리하고, 의존 관계를 `Build.cs`에 고정한다.

| 모듈 | 성격 | 의존 | 금지 |
| --- | --- | --- | --- |
| `InventoryCore` | 순수 Model — 그리드, 배치 규칙, 연산 서비스, 저장 레코드 | `Core`, `CoreUObject`, `Engine` | `UMG`, `Slate`, `SlateCore`, `ModelViewViewModel` |
| `DuckovLike` | Primary game module — ViewModel, 위젯, 액터, 이후 GAS | `InventoryCore` + 표준 게임 모듈 + UI 모듈 | — |

`InventoryCore`는 `Public/`·`Private/`를 분리해 모듈 경계를 디렉터리 구조로도 드러낸다.
`DuckovLike`는 소비자가 없는 primary game module이므로 평면 구조를 유지한다.

## 근거

**의존 방향이 빌드 그래프의 사실이 된다.** `InventoryCore.Build.cs`에 UI 모듈이 없으므로
`UMG.lib`가 링크 라인에 오르지 않는다. UI 타입을 실제로 사용하는 코드는 링크에서 죽는다.

이 실패는 리뷰어의 주의력이 아니라 UBT가 만든다. 포트폴리오에서
"단방향 의존성을 지켰다"가 주장이 아니라 **재현 가능한 사실**이 되는 지점이다.

두 `Build.cs`의 대비 자체가 설명 자료다 — 한쪽에는 UI 모듈이 있고 한쪽에는 없다.
그래서 `DuckovLike`에는 M2까지 실사용이 없더라도 `UMG`·`ModelViewViewModel` 의존을 넣는다.
비대칭이 보여야 경계가 보인다.

`Engine` 의존을 `InventoryCore`에 허용한 것은 A1(`UPrimaryDataAsset`)·A2(`UObject`+`FGuid`)·
A4(Container=`UObject`)를 **가능하게 두기 위해서**다. 막지 않을 뿐 강제하지 않는다 —
나중에 순수 struct 기반으로 선회해도 이 경계는 그대로 유효하다.

## 경계가 실제로 작동하는 지점 — M0.5 실측

`Source/InventoryCore/Private/`에 임시 프로브를 두고 위반을 3단계로 시도했다.

| 위반 시도 | 결과 |
| --- | --- |
| `#include "Blueprint/UserWidget.h"` | **통과한다** |
| `UUserWidget* Ptr = nullptr;` (선언만) | **통과한다** |
| `UUserWidget::StaticClass()` (심볼 참조) | **LNK2019 → 빌드 실패** |

```
BoundaryProbe.cpp.obj : error LNK2019: unresolved external symbol
  Z_Construct_UClass_UUserWidget_NoRegister
UnrealEditor-InventoryCore.dll : fatal error LNK1120: 1 unresolved externals
Result: Failed (OtherCompilationError)
```

프로브를 제거하자 즉시 `Result: Succeeded`로 복구됐다.

**헤더가 통과하는 이유**는 에디터 타깃이 `PCHUsage = UseExplicitOrSharedPCHs`로
`SharedPCH.UnrealEd`를 사용하고, 그 PCH에 UMG 헤더가 이미 들어 있기 때문이다.
경계를 만드는 것은 include path가 아니라 **링크 대상(`UMG.lib`)의 부재**다.

즉 경계는 즉각적이지 않지만 **우회 불가능하다.** UI 타입의 이름을 적어 둘 수는 있어도,
그것을 실제로 사용하는 코드는 한 줄도 빌드를 통과하지 못한다.

> 이 구분은 설명할 때 중요하다. "컴파일 에러로 막힌다"는 흔한 서술은 정확하지 않다.

## 기각한 대안

**단일 모듈 + 폴더 컨벤션 (`Source/DuckovLike/Model/`, `View/`).**
마찰이 없고 UE 소규모 프로젝트에서 가장 흔하다. 기각한 이유는 정확히 그 마찰 없음 때문이다 —
위반해도 아무 일이 없어서 원칙이 검증되지 않는다. 이 프로젝트의 명제가 "설계대로 동작함을
검증 가능한 형태로 보인다"인 이상, 검증 불가능한 구조는 명제와 충돌한다.

**3개 모듈 (`InventoryCore` / `InventoryUI` / `DuckovLike`).**
ViewModel까지 별도 모듈로 빼면 `View -> ViewModel -> Model` 3단이 전부 빌드 시스템에 새겨진다.
더 강한 구조지만 v1에서 ViewModel의 분량(C1: Container VM / Item VM / Interaction VM)이
모듈 하나를 정당화할 만큼인지 아직 모른다. **M2에서 실제 분량을 보고 재검토한다** —
이 ADR을 superseding하는 형태로.

**Plugin으로 `InventoryCore` 분리.**
경계는 가장 강하고 재사용성 서사도 좋지만, 단일 프로젝트에서 플러그인 구조는
빌드·패키징 복잡도만 늘린다. 재사용 요구가 실재하지 않는다.

## 결과

**얻는 것**

- Model이 UI를 모른다는 사실이 UBT에 의해 강제되고, 위반은 링크 에러로 드러난다.
- 심사자에게 보여줄 근거가 산문이 아니라 두 개의 `Build.cs` 파일이다.
- M1의 Model 작업이 UI 없이 독립적으로 진행·검증 가능하다.

**감수하는 비용**

- 모듈 간 타입 노출에 `INVENTORYCORE_API` 매크로가 필요하다. 빼먹으면 링크 에러가 난다.
- `Public/` 헤더에 무엇을 둘지 매번 판단해야 한다 — 이 마찰은 의도된 것이다.
- 새 모듈 추가 시 `.uproject` Modules 배열과 `Build.cs` 양쪽을 건드려야 한다.

**연쇄되는 미결 결정**

- Automation Test 배치(모듈 내부 `WITH_DEV_AUTOMATION_TESTS` vs 별도 `InventoryCoreTests` 모듈)는
  **M1로 미뤘다.** 별도 모듈로 가면 테스트가 `InventoryCore`의 Public 계약만 보게 되어
  캡슐화가 강제된다. 이 ADR은 두 선택지 모두와 양립한다.

## 검증

1. `Build.bat DuckovLikeEditor Win64 Development` → `Result: Succeeded`.
   `UnrealEditor-InventoryCore.dll`과 `UnrealEditor-DuckovLike.dll`이 각각 생성된다.
2. `Source/InventoryCore/InventoryCore.Build.cs`의 `PublicDependencyModuleNames`에
   `UMG`/`Slate`/`SlateCore`/`ModelViewViewModel`이 없음을 확인.
3. **M0.5에서 실측 완료** — 위 "경계가 실제로 작동하는 지점" 참조.
   포트폴리오 증빙이 필요하면 이 프로브를 그대로 재현하면 된다.
