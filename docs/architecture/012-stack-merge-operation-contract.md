# 설계결정기록 012 — 스택 병합 연산 계약

| 항목 | 내용 |
| --- | --- |
| 상태 | Accepted |
| 날짜 | 2026-08-29 |
| 마일스톤 | M1a |
| 관련 | 설계결정기록 011(스택 가능 여부·동질성)을 전제로 함 · 설계결정기록 004(Operation Service 원자성) · 설계결정기록 009(Instance 식별자) · `INVENTORY_DESIGN.md` 스택 병합 절(148~157행, 후보) |

## Context / Decision Question — AI

M1a 검증 매트릭스의 스택 범주(완전/부분 병합·최대치·상태 불일치)를 구현하려면, 병합을
어떤 API로 노출하고, 부분 병합·완전 병합·병합 불가를 각각 어떻게 표현하며, 완전 병합으로
사라지는 쪽의 Instance를 어떻게 처리할지가 정해져야 한다.

현재 `FInventoryOperations`에는 `TryMove` 하나만 있고, 반환 타입은 `EInventoryOperationFailure`
단일 enum이라 "실제로 몇 개 이동했는지"를 돌려줄 채널이 없다. 실패 사유도
`None/NoSpace/Occupied/ItemNotFound`뿐이다.

## Options — 사용자 먼저(Option Sweep) → AI 보완

AI가 제시한 후보:

| 축 | 후보 A | 후보 B |
| --- | --- | --- |
| API 형태 | `TryMove`를 확장해 겹쳐진 이동을 자동으로 병합 처리 | `FInventoryOperations::TryStack(...)`을 별도 신설 |
| 반환 방식 | 반환 타입을 구조체로 확장(`{Reason; MovedQuantity;}`) | 기존처럼 enum만 유지, 호출자가 `Target.Quantity` 재조회 |
| 실패 사유 | `StackMismatch`를 신설하지 않고 `Occupied` 재사용 | `StackMismatch`를 신설 |
| `StackFull`(이동량 0) | 성공(이동량 0인 성공)으로 처리 | 실패로 처리 |
| 완전 병합 시 Source Instance | 폐기 — 카운터는 계속 전진, 번호 재사용 안 함 | (다른 처리 — 근거 없어 후보에서 사실상 배제) |

## User Initial Decision — 사용자

> "3. try move에 책임이 늘어나는것보다는 책임분산이 더맞는 판단이라고 생각해"
>
> "4. 구조체 자체 필드를 늘리면 아이템이 많아질 수록 사용되는 stack 메모리가 많아질거라
> 생각해서"
>
> "5. occupied 를 잘 파악하고 있는것도 있지만 stack mismatch자체를 신설하는게 코드를
> 디버깅할 때 이해하기 편할거라 생각했고, stackfull은 성공이라고 하기에는 합쳐지지
> 않았으니까 실패가 더 맞다고 생각해서"
>
> "6. 코드흐름과 맞다는 힌트가 있어서"

## User Reasoning / Concerns — 사용자

- API 분리: "try move에 책임이 늘어나는것보다는 책임분산이 더맞는 판단이라고 생각해."
- 반환 방식(초기 근거, AI Review로 교정됨): "구조체 자체 필드를 늘리면 아이템이 많아질
  수록 사용되는 stack 메모리가 많아질거라 생각해서."
  → AI Review 반영 후 교정된 근거: "target quantity라는게 있으니까 이걸 쓰는게 맞다."
- `StackMismatch` 신설: "occupied 를 잘 파악하고 있는것도 있지만 stack mismatch자체를
  신설하는게 코드를 디버깅할 때 이해하기 편할거라 생각했고."
- `StackFull` = 실패: "stackfull은 성공이라고 하기에는 합쳐지지 않았으니까 실패가 더
  맞다고 생각해서."
- 완전 병합 시 Source 폐기: "코드흐름과 맞다는 힌트가 있어서."

## AI Review — AI

최종 판단에 실제로 영향을 준 지적만 남긴다.

- **API 분리의 결과.** `TryMove`와 `TryStack`을 분리하면 "지금 겹쳐 놓는 행위가 이동인지
  병합인지"를 판정하는 책임이 호출자(M2의 ViewModel/UI)로 넘어간다. 지금 결정으로 그 판단
  자체가 없어지는 게 아니라 위치만 옮겨가는 것이다 — M1a 범위에서는 문제가 되지 않으므로
  결정을 막지는 않되, M2에서 이 판정 로직이 어디에 놓일지는 별도로 다뤄야 한다.
- **근거 교정.** "구조체 필드를 늘리면 아이템이 많아질수록 스택 메모리가 늘어난다"는 전제는
  부정확하다. 반환값은 함수 호출 1회당 생기는 임시 값이지 인벤토리 아이템 개수에 비례해
  커지지 않는다(`enum` + `int32` 몇 바이트 수준). → 사용자가 근거를 "호출자가 이미 두
  `FItemInstance`를 쥐고 있으므로 새 타입을 만들기보다 기존 `Target.Quantity` 필드를
  재사용하는 게 낫다"로 교정해 B(enum 유지)를 그대로 유지.
- **`StackFull`=실패의 정합성.** 이동량 0은 실제로 아무 상태도 바꾸지 않으므로, CLAUDE.md
  §4 "실패한 연산은 상태를 바꾸지 않는다"와 정확히 맞아떨어진다. 반론 없음.
- **부분 병합 범위 확인.** 이번 결정에서 명시적으로 포함하기로 함 — 이동량이 1 이상이지만
  `Source`가 완전히 비지 않는 경우는 성공(`None`)이다.

## Final Decision — 사용자

1. `FInventoryOperations::TryStack(...)`을 `TryMove`와 별도의 정적 함수로 신설한다.
2. 반환 타입은 기존과 같은 `EInventoryOperationFailure` 단일 enum을 유지한다. 병합 후 실제
   이동 수량은 호출자가 `Target.Quantity`를 다시 읽어 확인한다.
3. 실패 사유에 `StackMismatch`를 신설한다 — Definition/Table이 다르거나, 설계결정기록 011의
   `bStackable=false`인 조합에 병합을 시도하는 경우 모두 이 사유를 반환한다.
4. 이동량이 0인 경우(`Target`이 이미 `MaxStack`에 도달)는 `StackFull` 실패로 처리한다.
5. 이동량이 1 이상이지만 `Source`가 완전히 비워지지 않는 경우(부분 병합)는 성공(`None`)이다.
6. 완전 병합으로 `Source.Quantity`가 0이 되면 `Source` Instance를 컨테이너에서 제거하고
   폐기한다. Instance 번호(설계결정기록 009)는 재사용하지 않는다 — 전역 카운터는 계속
   전진한다.

## Consequences / Accepted Costs — 사용자

**얻는 것**
- `TryMove`의 책임이 늘어나지 않는다.
- 실패 사유가 세분화돼 UI가 "다른 아이템이라 안 됨"과 "꽉 차서 안 됨"을 구분해 안내할 수
  있다.

**감수하는 비용**
- 호출자가 "지금 겹쳐 놓는 게 이동인지 병합인지"를 미리 판정해야 한다 — 이 판정 로직의
  위치는 M2로 이연된다.
- 호출자가 실제 이동 수량을 알려면 호출 전 `Target.Quantity`를 스냅샷해 호출 후 값과
  비교해야 한다 — 반환값만으로는 알 수 없다.
- 완전 병합 후 `Source` 번호는 사라지고 다시 나오지 않는다 — 로그를 좇다 보면 번호가
  갑자기 끊긴 것처럼 보인다(의도된 동작).

## Revisit Conditions — 사용자

1. M2에서 "이동인지 병합인지" 판정 로직이 여러 호출부에 중복되기 시작하면 `TryMove`/
   `TryStack` 경계를 다시 연다.
2. 반환값에 이동 수량이 없어서 호출자 쪽 스냅샷·비교 코드가 반복적으로 번거로워지면
   구조체 반환으로의 전환을 다시 연다.

## 검증 — AI

1. `DefinitionRowName` 또는 `DefinitionTable`이 다른 두 Instance에 `TryStack` 시도 →
   `StackMismatch`, 양쪽 상태 불변.
2. `bStackable=false`인 Definition의 두 Instance에 `TryStack` 시도 → `StackMismatch`,
   양쪽 상태 불변.
3. `Target`이 이미 `MaxStack`에 도달한 상태에서 `TryStack` 시도 → `StackFull`, 양쪽 상태
   불변.
4. 부분 병합 → `None` 반환, `Source.Quantity`가 이동량만큼 줄고 `Target.Quantity`가 그만큼
   늚, `Source`는 컨테이너에 남음.
5. 완전 병합(Source 전량 이동) → `None` 반환, `Source`가 컨테이너에서 제거됨,
   `Target.Quantity`가 이동량만큼 늚.

테스트 이름은 M1a 스택 슬라이스 시작 시 사용자가 확정한다(§5.4).
