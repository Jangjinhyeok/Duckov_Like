# 설계결정기록 011 — 스택 가능 여부와 동질성 판정

| 항목 | 내용 |
| --- | --- |
| 상태 | Accepted |
| 날짜 | 2026-08-29 |
| 마일스톤 | M1a |
| 관련 | 설계결정기록 001(Item Definition 형식) · `INVENTORY_DESIGN.md` 스택 병합 절(148~157행, 후보) · 설계결정기록 012(병합 연산 계약)의 전제 |

## Context / Decision Question — AI

M1a 스택 병합 슬라이스를 구현하려면 두 가지가 먼저 정해져야 한다.

1. 아이템이 "몇 개까지 겹쳐 쌓일 수 있는지"를 어디에, 어떤 형태로 둘 것인가. 현재
   `Public/ItemDefinitionRow.h`에는 `Size`만 있고 스택 관련 필드가 없다.
2. 두 `FItemInstance`가 "같은 스택으로 합칠 수 있는 사이"인지 판정하는 기준이 무엇인가.
   현재 `FItemInstance`에는 `Quantity`·`AnchorCell`·`bRotated`뿐이고, 동질성 비교에 쓸 가변
   상태 필드가 없다.

## Options — 사용자 먼저(Option Sweep) → AI 보완

AI가 제시한 후보:

| 축 | 후보 A | 후보 B |
| --- | --- | --- |
| 스택 상한 표현 | `int32 MaxStack` 하나만 (`1`=비스택) | `bool bStackable` + `int32 MaxStack` 분리 |
| 동질성 판정 범위 | `DefinitionRowName`+`DefinitionTable`만 비교 | 지금부터 확장 가능한 비교 훅을 만들어 둠 |

## User Initial Decision — 사용자

> "1. 이 코드를 내가 아닌 다른 사람이 봤을 때도 아 이건 이런 코드구나라는 걸 확인할 수 있어야
> 된다고 생각해 주석없이도 그래서 b야"
>
> "2. 이건 a가 맞겠다. 지금 당장은 확장성 보다는 지금 있는 구조에 정합한게 더 맞는거같아."

(2번은 처음 B로 답했다가, 이유를 적는 과정에서 A로 바꿈.)

## User Reasoning / Concerns — 사용자

- 필드 분리(B) 이유: "이 코드를 내가 아닌 다른 사람이 봤을 때도 아 이건 이런 코드구나라는 걸
  확인할 수 있어야 된다고 생각해 주석없이도 그래서 b야" — 주석 없이도 코드 자체로 의도가
  드러나야 한다는 것.
- 동질성 판정 범위(A) 이유: "지금 당장은 확장성보다는 지금 있는 구조에 정합한게 더 맞는거같아."

## AI Review — AI

최종 판단에 실제로 영향을 준 지적만 남긴다.

- **모순 상태 위험.** `bStackable`과 `MaxStack`을 독립 필드로 두면 `bStackable=false`인데
  `MaxStack=5`, 또는 그 반대인 데이터가 만들어질 수 있다. 어느 필드가 실제 판정의 기준인지
  정해두지 않으면 구현 단계에서 임의로 정해지게 된다.
  → 사용자가 **`bStackable`만 판정 기준으로 쓰고 `MaxStack`은 상한값 전용**으로 확정해 해소.
- 동질성 판정을 `DefinitionRowName`+`DefinitionTable` 비교로 한정하는 것은 simplicity-first
  원칙과 맞고, 지금 `FItemInstance`에 비교할 가변 상태 필드가 아예 없으므로 사실상 이것이
  유일하게 구현 가능한 범위이기도 하다. 반론 없음.

## Final Decision — 사용자

1. `Public/ItemDefinitionRow.h`에 `bool bStackable`과 `int32 MaxStack`을 각각 추가한다.
2. **스택 가능 여부 판정은 `bStackable`만 본다.** `MaxStack`은 `bStackable=true`일 때 병합
   상한값으로만 쓰인다 — `bStackable=false`면 `MaxStack` 값과 무관하게 비스택으로 취급한다.
3. 동질성(같은 스택으로 합칠 수 있는가) 판정은 `DefinitionRowName`+`DefinitionTable` 비교만
   한다. 확장 가능한 비교 훅은 지금 만들지 않는다.

## Consequences / Accepted Costs — 사용자

**얻는 것**
- 필드 이름만 보고도 의도가 드러난다(`bStackable`=가능 여부, `MaxStack`=상한) — 주석 없이도
  읽힌다.
- 지금 구조에 필요한 만큼만 구현한다 — 쓰지 않는 확장 지점을 미리 만들지 않는다.

**감수하는 비용**
- `bStackable=false`이면서 `MaxStack`에 의미 있는 값이 들어있는 모순 데이터를 코드가 걸러주지
  않는다 — 에디터 데이터 검증은 이번 범위 밖이다.
- 나중에 내구도·인챈트 등 가변 상태가 Instance에 추가되면, 동질성 판정 로직을 그때 다시
  손봐야 한다(지금은 의도적으로 미룸).

## Revisit Conditions — 사용자

1. `FItemInstance`에 내구도·인챈트 등 동질성에 영향을 주는 가변 상태가 실제로 추가되면
   동질성 판정 범위를 다시 연다.
2. `bStackable=false`인데 `MaxStack`이 잘못 설정된 데이터가 실제 버그를 유발하면, 데이터
   검증(예: 에디터 `IsDataValid` 오버라이드)을 다시 고려한다.

## 검증 — AI

1. `bStackable=false`인 두 Instance는 같은 Definition이어도 병합이 실패한다.
2. `bStackable=true`인 동일 Definition 두 Instance는 병합 가능하며, 합친 수량이 `MaxStack`을
   넘지 않는다.
3. `DefinitionRowName` 또는 `DefinitionTable`이 다른 두 Instance는 `bStackable` 값과 무관하게
   병합되지 않는다.

테스트 이름은 M1a 스택 슬라이스 시작 시 사용자가 확정한다(§5.4).
