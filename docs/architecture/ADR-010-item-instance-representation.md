# ADR-010: 아이템 Instance 표현 — `USTRUCT` (값 타입)

| 항목 | 내용 |
| --- | --- |
| 상태 | Accepted |
| 날짜 | 2026-08-18 |
| 마일스톤 | M1a 전 |
| 관련 | [ADR-009](ADR-009-item-instance-identifier.md)가 열어 둔 미결 항목 중 하나를 해소 · `INVENTORY_DESIGN.md` A2를 대체(표현 방식 부분) |

> **범위 주의.** 이 ADR은 **Instance가 값 타입인가 참조 타입인가**만 다룬다. 번호 발급 시점,
> 스택 분할 시 번호 정책, 저장 시 카운터 복원, 테스트 카운터 초기화는 여전히 미결이며
> ADR-009가 열어 둔 채로 남는다.

## Context / Decision Question — AI

`ADR-009`가 식별자(전역 카운터 `int32`)를 정하면서 함께 열어 둔 질문이다: 아이템 Instance를
`USTRUCT`(값 타입 — 컨테이너의 배열에 인라인으로 저장, 복사됨, GC 무관)로 둘 것인가,
`UObject`(참조 타입 — 포인터로 다루고 GC 대상)로 둘 것인가.

`INVENTORY_DESIGN.md`의 A2는 `UObject + FGuid`를 후보로 적어 두었으나, 식별자 부분은 이미
ADR-009로 무효화됐고 표현 방식(`UObject` 여부)은 사용자 판단을 거친 적이 없어 미결로 남아 있었다.

## Options — 사용자 먼저(Option Sweep) → AI 보완

두 후보는 ADR-009가 이미 이름 붙여 둔 상태였다(`USTRUCT` / `UObject`). 사용자가 먼저 든 근거는
"아이템은 유저가 들고 있을 수도, 맵에 자연 스폰됐을 수도 있는데 값 타입으로 고정하면 원하는
동작이 안 될 것 같다"였다. 확인 결과 이 근거는 **월드 픽업 액터(별도 존재, 인벤토리 진입 시
Instance로 변환)와 그리드 내부 표현(오늘 결정)을 혼동한 것**이었고, 오늘 결정 범위 밖이라
사용자가 직접 제외했다 — 해당 이유는 M3 픽업 액터 설계로 이관한다.

AI가 보완한 비교 축: 소유권/수명, GC 비용, Automation Test와의 상호작용, 컨테이너 저장 형태,
동일성 비교, 스택 분할, 저장(직렬화).

## User Initial Decision — 사용자

축을 검토한 뒤 사용자가 낸 판단:

> "GC 비용이 제일 걸리네, USTRUCT로 기울어"

## User Reasoning / Concerns — 사용자

> "uobject로 하면, 맵에 있는 모든 아이템들을 gc로 관리해야 하니까 그게 가장 비용이 크니까 값
> 형태인 ustruct로 하는게 더 좋다라고 이해했어."

(정정: "맵에 있는 아이템"이 아니라 **컨테이너(스태시+레이드) 안에 있는 모든 Instance**가 GC
추적 대상이 된다는 뜻으로 확인 — 월드 표현은 오늘 결정 범위 밖이므로. 정정 반영해도 결론은
동일하게 성립.)

우려·모르는 것은 명시적으로 없다고 답함.

## AI Review — AI

최종 판단에 실제로 영향을 준 지적만 남긴다.

- **포인터 무효화 위험.** `USTRUCT`를 택하면 Instance는 컨테이너의 `TArray<FItemInstance>`에
  인라인으로 산다. 배열이 재할당되거나 원소가 제거되면 그 원소를 가리키던 raw pointer/reference는
  무효화된다. 즉 특정 Instance를 계속 참조해야 하는 코드는 포인터를 캐싱하면 안 되고 Instance
  번호(ADR-009)로 매번 다시 조회해야 한다.
- **기존 원칙과의 정합.** 이 제약은 오히려 CLAUDE.md §4가 이미 정한 두 원칙과 맞아떨어진다 —
  "View/ViewModel은 원본 상태를 갖지 않는다"(포인터 캐싱 금지가 이를 구조적으로 강제)와
  "실패한 연산은 상태를 바꾸지 않는다 — 전체 검증 후 한 번에 커밋, 롤백 경로 없음"(값 타입은
  임시 복사본을 검증한 뒤 그대로 대입하는 것만으로 원자성을 얻는다. `UObject`였으면 이 원자성을
  위해 deep-copy나 수동 undo가 별도로 필요했을 것).
- **반대(`UObject`) 선택이 유리해지는 조건.** A2의 원래 근거였던 "UI 매핑"은 `InventoryCore`가
  UMG를 모르는 모듈이라 지금 구조에서 실현되지 않는다. ViewModel이 직접 참조 편의가 필요해지면
  ViewModel 쪽에서 별도 wrapper를 만들면 되고, Model의 표현 방식과는 분리된다.
- 결정을 뒤집을 만한 반론은 찾지 못했다.

## Final Decision — 사용자

아이템 Instance는 **`USTRUCT`(값 타입)**로 한다. 채택 근거는 **GC 비용** — 인벤토리에 존재하는
모든 Instance가 UObject GC 추적 대상이 되는 것을 피한다.

## Consequences / Accepted Costs — 사용자

**얻는 것**
- 인벤토리 슬롯 수만큼 쌓이는 UObject당 GC 오버헤드(reflection 메타데이터, mark-sweep 대상)가
  없다.
- 값 복사 기반이라 CLAUDE.md §4의 "전체 검증 후 원자적 커밋" 원칙이 별도 롤백 로직 없이
  자연스럽게 구현된다.
- Automation Test에서 생성·소멸이 결정론적이다 — GC 타이밍에 좌우되지 않는다.

**감수하는 비용**
- **포인터·참조를 캐싱할 수 없다.** 어디서든 특정 Instance를 계속 붙잡고 있으려면 Instance
  번호로 매번 재조회해야 한다.
- **UObject 기반 UI 바인딩 편의(A2 원 후보의 근거)가 필요해지면 ViewModel 쪽에 별도 wrapper를
  만들어야 한다.** Model 표현과 UI 편의가 분리된 채로 간다.

## Revisit Conditions — 사용자

아래 중 하나가 관찰되면 이 결정을 다시 연다.

1. **M2에서 ViewModel/MVVM 바인딩에 UObject 참조가 실질적으로 필요해지고, wrapper 비용이
   감당 안 될 정도로 반복될 때.**
2. **포인터 무효화 문제가 실제 구현에서 반복적으로 버그를 유발할 때** — 번호 재조회 규율이
   지켜지지 않고 dangling 참조 버그가 M1a~M2에서 여러 번 나오면 재검토한다.
3. **Instance 수가 예상보다 훨씬 커져 값 복사 비용이 GC 비용보다 커지는 것이 프로파일링으로
   실측될 때.**

## 검증 — AI

1. M1a 헤더 작성 시 `FItemInstance`가 `USTRUCT()`로 선언되고 컨테이너가 `TArray<FItemInstance>`
   형태로 저장함을 확인한다.
2. 코드 리뷰에서 Instance에 대한 장기 캐싱 포인터/참조가 없고, 대신 Instance 번호로 조회하는
   패턴을 따르는지 확인한다.
