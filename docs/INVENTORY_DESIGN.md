# Grid Inventory Technical Design | Proposed Baseline

> **문서 상태**: M0 설계 결정 문서 / Proposed
> 
> 
> **목표**: 구현 전 데이터 소유권, 불변식, 연산 계약, MVVM 데이터 흐름을 합의한다.
> 
> **범위**: 싱글플레이 포트폴리오의 v1 그리드 인벤토리. 중첩 컨테이너와 고급 확장은 v1 이후로 미룬다.
> 

## 1. 설계 요약

### 문제

그리드 인벤토리는 아이템의 위치, 회전, 수량, 컨테이너 간 이동, 저장, 드래그 프리뷰가 동시에 얽힌다. UI가 이 규칙을 소유하면 로직이 중복되고 테스트가 어려워진다.

### 설계 목표

1. 규칙은 UI와 독립된 Model에서 검증한다.
2. 실패한 연산은 상태를 바꾸지 않고 실패 이유를 반환한다.
3. UMG View는 표현만, ViewModel은 상태 투영과 입력 중계만 담당한다.
4. 플레이어, 상자, 시체, 스태시를 같은 컨테이너 추상으로 처리한다.

### 런타임 흐름

`UMG View -> Interaction / Container ViewModel -> Inventory Operation Service -> Inventory Model`

`Inventory Model -> ChangeSet Event -> ViewModel -> UMG View`

`Save Record Mapper <-> Inventory Model`

---

## 2. 용어와 책임

| 용어 | 책임 |
| --- | --- |
| Definition | 아이템 종류의 불변 데이터: 크기, 스택 상한, 카테고리, 아이콘, 무게 |
| Instance | 월드에 존재하는 아이템 하나의 가변 데이터: `InstanceId`, 수량, 회전, 위치, 상태 |
| Container | 아이템 Instance의 배치와 점유 규칙을 관리하는 그리드 |
| Operation Service | 이동, 분할, 병합, 퀵 이동, 정렬처럼 여러 컨테이너를 아우르는 명령의 검증/커밋 |
| Container VM | Container의 표시용 Item VM 목록과 그리드 크기를 제공 |
| Interaction VM | 드래그 중인 아이템, 프리뷰 좌표, 현재 배치 결과처럼 컨테이너 횡단 상태를 보유 |
| Save Record Mapper | 런타임 Model과 명시적 저장 레코드를 상호 변환 |

---

## 3. 반드시 지킬 불변식

| ID | 규칙 |
| --- | --- |
| INV-01 | 하나의 아이템 Instance는 동시에 둘 이상의 컨테이너에 존재할 수 없다. |
| INV-02 | 배치된 아이템의 모든 점유 셀은 그리드 경계 안에 있다. |
| INV-03 | 서로 다른 두 아이템은 점유 셀을 공유하지 않는다. |
| INV-04 | `Count`는 항상 `1..MaxStack` 범위다. |
| INV-05 | 회전 상태일 때 유효 크기는 Definition의 W/H를 교환해 계산한다. |
| INV-06 | 실패한 연산은 Model 상태를 변경하지 않는다. |
| INV-07 | 중첩 컨테이너는 순환 참조를 만들 수 없다. |
| INV-08 | View와 ViewModel은 위치나 수량의 원본 상태를 소유하지 않는다. |

> **담을 내용**: 아이템을 그리드 경계 밖으로 걸치게 끌었을 때(INV-02 위반)와 이미 다른 아이템이 있는 칸에 겹치게 끌었을 때(INV-03 위반)의 화면. 두 상황을 한 장에 나란히 또는 2장.
**여기 넣는 이유**: INV-02·INV-03은 표에서 한 줄이지만 실제로는 Model 검증 로직의 대부분이다. 심사자가 “이 불변식이 화면에서 무엇을 막는지” 즉시 알 수 있다.
> 
> 
> ![image.png](image%202.png)
> 

> **담을 내용**: 세로 2x4 아이템을 R키로 회전시켜 가로 4x2가 되고, 회전 전에는 안 들어가던 자리에 들어가는 순간.
**여기 넣는 이유**: INV-05(*회전 시 W/H 교환*)와 §4 E3(좌표·회전 규약)의 시각적 근거. “왜 회전 상태를 Instance가 들고 있어야 하는가”가 이 3초로 설명된다.
> 
> 
> ![그리드 인벤토리 아이템 회전.gif](%EA%B7%B8%EB%A6%AC%EB%93%9C_%EC%9D%B8%EB%B2%A4%ED%86%A0%EB%A6%AC_%EC%95%84%EC%9D%B4%ED%85%9C_%ED%9A%8C%EC%A0%84.gif)
> 

---

## 4. 제안된 기준 아키텍처

아래 항목은 현 단계의 **권장안**이다. 구현 시작 전 ADR 상태를 `Accepted`로 변경한다.

| ID | 제안 | 선택 이유 | 상태 |
| --- | --- | --- | --- |
| A1 | Definition은 `UPrimaryDataAsset` | 에셋 참조와 AssetManager 흐름을 설명하기 좋음 | Proposed |
| A2 | Instance는 `UObject` + `FGuid` | UI 매핑, 가변 상태, 저장 식별자에 적합 | Proposed |
| A3 | 아이템 목록을 진실로 두고 셀 맵은 재생성 가능한 캐시 | 저장 친화성과 빠른 셀 질의를 함께 확보 | Proposed |
| A4 | Container는 `UObject`, Component는 Actor 어댑터 | Actor가 아닌 스태시도 같은 방식으로 처리 | Proposed |
| B1 | Operation Service / Request-Result 계층 | 횡단 연산과 실패 이유를 한 곳에서 관리 | Proposed |
| B2 | 사전 전체 검증 후 원자적으로 커밋 | 롤백 경로 없이 실패 원자성 보장 | Proposed |
| B3 | 퀵 이동은 row-major first-fit, 정렬은 면적 내림차순 greedy | 결정론적이며 테스트 가능 | Proposed |
| C1 | Container VM + Item VM + Interaction VM | 드래그 상태의 소유 위치가 명확 | Proposed |
| C2 | 초기에는 ChangeSet 이벤트 하나로 통지 | v1의 복잡도를 낮추고 필요 시 세분화 가능 | Proposed |
| C3 | UI Subsystem/Factory가 Model-VM 매핑을 소유 | View가 생성/수명 책임을 갖지 않음 | Proposed |
| D1 | 그리드/하이라이트는 Paint, 아이템만 위젯 | 셀 수에 비례하는 위젯 생성을 억제 | Proposed |
| D2 | 수동 드래그 상태 머신을 우선 검토 | 드래그 중 회전, 프리뷰, 복수 컨테이너 처리를 제어하기 쉬움 | Proposed |
| D3 | 좌표 변환은 View, 유효성 판정은 Model Query | View에 배치 규칙을 중복하지 않음 | Proposed |
| E1 | 버전이 있는 명시적 저장 레코드 | 런타임 클래스 구조와 저장 포맷을 분리 | Proposed |
| E2 | 장비는 별도 Slot Container/Equipment Model | 그리드 규칙과 슬롯 카테고리 규칙을 명확히 분리 | Proposed |
| E3 | 좌상단 원점, X=열/Y=행, 좌상단 앵커, 0/90도 회전 | 모든 계층이 공유할 최소 좌표 규약 | Proposed |

---

## 5. 데이터와 소유권

### 최소 데이터 형태

| 데이터 | 최소 필드 |
| --- | --- |
| Item Definition | `DefinitionId`, 크기(W/H), `MaxStack`, 카테고리, 아이콘, 무게 |
| Item Instance | `InstanceId`, `DefinitionId`, `Count`, `bRotated`, `Position`, 가변 상태 |
| Container | `ContainerId`, `GridSize`, 아이템 목록, 점유 캐시 |
| Save Record | 포맷 버전, ContainerId, 아이템별 DefinitionId/Position/Rotation/Count/State |

### 소유권 규칙

- Container가 Item Instance의 현재 배치 상태를 소유한다.
- Actor에 붙는 `UInventoryComponent`는 Actor와 Container를 연결하는 어댑터다.
- 스태시는 Actor가 아닌 장기 소유자에서도 같은 Container를 사용한다.
- 셀 점유 맵은 Item 목록에서 계산할 수 있는 캐시이며, 별도의 진실이 아니다.

---

## 6. 연산 계약

### 공통 결과

모든 UI 명령은 성공 여부만이 아니라 실패 원인을 반환한다.

| 결과 | UI 사용 예 |
| --- | --- |
| `None` | 성공 처리 |
| `InvalidItem` | 존재하지 않는 아이템 입력 차단 |
| `OutOfBounds` | 배치 불가 프리뷰 |
| `Occupied` | 겹침 프리뷰 |
| `NoSpace` | 퀵 이동/정렬 실패 안내 |
| `InvalidCategory` | 장비 슬롯 제한 안내 |
| `StackMismatch`, `StackFull` | 스택 병합 불가/부분 병합 안내 |
| `WouldCreateCycle` | 중첩 컨테이너 순환 방지 |
| `ResizeOverflow` | 가방 교체 거부 안내 |

### 컨테이너 간 이동

1. Source가 Item을 소유하는지 확인한다.
2. Target에서 유효 크기, 경계, 점유, 카테고리를 검증한다.
3. 두 컨테이너 모두 성공 가능한 경우에만 Source 제거와 Target 추가를 커밋한다.
4. 하나라도 실패하면 상태를 바꾸지 않고 오류를 반환한다.
5. 성공 시 변경된 Instance ID를 포함한 ChangeSet을 발행한다.

### 스택 병합

- Definition과 스택 동질성에 영향을 주는 상태가 같아야 한다.
- 이동 수량은 `min(Source.Count, Target.MaxStack - Target.Count)`다.
- Source 수량이 0이 되면 제거한다.
- 부분 병합은 성공이며 실제 이동 수량을 결과에 포함한다.

> 🎞️ **REF-08 · 레퍼런스 GIF** — 스택 부분 병합 (2~4초)
**담을 내용**: 수량이 찬 스택에 다른 스택을 겹쳐 놓아 일부만 들어가고, 남은 수량이 원래 자리에 그대로 남는 장면. 양쪽 수량 숫자가 읽히게.
**여기 넣는 이유**: 위 `min(Source.Count, Target.MaxStack - Target.Count)` 공식이 실제로 무엇을 계산하는지, 그리고 **부분 병합이 실패가 아니라 성공**이라는 이 문서의 계약이 왜 그렇게 정의됐는지를 보여준다.
**파일**: `ref/ref-08-partial-stack.gif`
> 

### 자동 정렬과 퀵 이동

- 퀵 이동: row-major first-fit으로 빈 자리를 찾는다.
- 자동 정렬: 임시 배치 계획에서 면적 내림차순 greedy를 실행한다.
- 모든 항목이 배치될 때만 실제 상태를 교체한다.
- 실패하면 원래 배치를 유지한다.

---

## 7. UI 데이터 흐름

### 드래그 프리뷰

1. View가 마우스 위치를 대상 컨테이너의 셀 좌표로 변환한다.
2. Interaction VM이 Model Query에 배치 가능 여부를 요청한다.
3. Model이 유효성 및 실패 이유를 반환한다.
4. Interaction VM이 프리뷰 상태를 갱신한다.
5. View는 성공/실패 하이라이트만 표현한다.

> **담을 내용**: 같은 아이템을 드래그한 상태에서 ①놓을 수 있는 자리(초록 계열 하이라이트) ②놓을 수 없는 자리(빨강 계열)의 두 컷.
**여기 넣는 이유**: 이 문서에서 가장 중요한 그림. 위 5단계 흐름의 **결과물**이며, 동시에 D3 결정(*좌표 변환은 View, 유효성 판정은 Model Query*)의 근거다 — 색깔은 View가 칠하지만 판정은 Model이 한다는 분리가 이 화면 한 장에 걸려 있다.
> 
> 
> ![image.png](image%203.png)
> 
> ![image.png](image%202.png)
> 

> 🟩 **OWN-03 · 내 구현 (M2 이후 채움)** — 동일 상황의 내 구현 프리뷰
**담을 내용**: REF-09와 같은 구도로, 내 UMG 구현에서의 가능/불가 하이라이트. 가능하면 화면 옆에 Model이 반환한 실패 사유(`Occupied`, `OutOfBounds`)를 디버그 텍스트로 함께 표시.
**여기 넣는 이유**: 레퍼런스는 결과만 보여주지만, 내 구현은 **실패 사유가 Model에서 나온다는 것까지** 보여줄 수 있다. §6 연산 계약 표가 실제로 동작함을 증명하는 자리.
**파일**: `own/own-03-my-preview.png`
> 

### 드롭

1. View가 Drop 명령을 Interaction VM에 전달한다.
2. Interaction VM이 Operation Service에 Move Request를 전달한다.
3. Service가 검증 후 성공 시에만 Model을 커밋한다.
4. Model ChangeSet을 구독한 Container VM이 Item VM을 갱신한다.
5. View Binding이 화면을 갱신한다.

### 드래그 취소

- ESC, UI 닫힘, 대상 컨테이너 파괴, 레벨 전환에서 취소한다.
- 드래그 시작 시에는 Model에서 아이템을 제거하지 않는다.
- Drop 성공 때만 실제 이동을 커밋한다.

---

## 8. 예외 정책

| 상황 | v1 정책 | 이유 |
| --- | --- | --- |
| 가방 교체로 그리드 축소 | 기존 아이템이 맞지 않으면 교체를 거부 | 예측 가능하고 테스트가 단순 |
| 중첩 컨테이너 | 인터페이스와 순환 검사 계약만 준비, 기능은 P1 | M1의 핵심 규칙에 집중 |
| 장비 슬롯 | 그리드와 다른 카테고리 제한을 명시 | 1x1 그리드로 규칙을 숨기지 않음 |
| 저장 포맷 변경 | 버전 필드와 명시적 레코드 변환 사용 | 개발 중 구조 변경을 추적 가능 |

> 🖼️ **REF-10 · 레퍼런스 이미지** — 가방 교체에 따른 그리드 크기 변화 (정지컷 2장)
**담을 내용**: 큰 가방 장착 상태의 소지 그리드와, 작은 가방으로 교체했을 때의 그리드. 크기 차이가 한눈에 보이게 같은 배율로.
**여기 넣는 이유**: 위 표 첫 행(*가방 교체로 그리드 축소 → 맞지 않으면 교체 거부*)이 다루는 상황. 이 정책이 왜 필요한지는 “줄어든 그리드에 아이템이 남아 있으면 어디로 가나?”라는 질문이 보여야 이해된다.
**파일**: `ref/ref-10-bag-resize.png`
> 

---

## 9. 검증 매트릭스

| 범주 | 필수 테스트 |
| --- | --- |
| 배치 | 경계, 겹침, 회전, 1x1, 전체 점유 |
| 이동 | 동일 컨테이너, 다른 컨테이너, 실패 원자성 |
| 스택 | 완전/부분 병합, 최대치, 서로 다른 상태 |
| 정렬 | 결정론, 실패 시 원상 보존, 회전 허용 정책 |
| 저장 | 포맷 버전, Definition 참조, GUID, 위치/회전/수량 동일성 |
| 리사이즈 | 축소 거부, 확대 |
| UI | 드래그 취소, 빠른 연속 입력, 컨테이너 종료 |
| 성능 | 20x20 셀, 아이템 100개, 상시 Tick 0, 이벤트 폭주 여부 |

### v1 완료 기준

- 30개 이상의 Model Automation Test가 통과한다.
- 실패한 컨테이너 간 이동 뒤 양쪽 Container 상태가 동일함을 검증한다.
- 저장-로드 뒤 `InstanceId`, 위치, 회전, 수량이 유지된다.
- 정해진 성능 시나리오에서 측정 결과와 렌더 전략의 근거를 남긴다.

---

## 10. 구현 전 승인할 ADR

아래 항목은 의존성이 크므로 M1 전에 승인한다.

1. ADR-001: Definition 에셋 형식
2. ADR-002: Item Instance 표현과 `FGuid` 정책
3. ADR-003: Container 소유 구조와 점유 캐시 정책
4. ADR-004: Operation Service와 이동 원자성
5. ADR-005: MVVM 분할과 ChangeSet 통지
6. ADR-006: 그리드 렌더와 드래그 구현 경로
7. ADR-007: 저장 레코드와 포맷 버전
8. ADR-008: 장비 슬롯 및 가방 리사이즈 정책

### ADR 작성 형식

| 항목 | 기록 내용 |
| --- | --- |
| 상태 | Proposed / Accepted / Superseded |
| 맥락 | 해결하려는 문제 |
| 결정 | 선택한 방식 |
| 근거 | 이 프로젝트에 적합한 이유 |
| 기각한 대안 | 제외한 선택지와 이유 |
| 결과 | 얻는 장점과 감수할 비용 |
| 검증 | 테스트 또는 프로파일 방법 |

---

## 변경 이력

| 버전 | 날짜 | 내용 |
| --- | --- | --- |
| v0.1 | 2026-07-06 | 결정 축과 대안 정리 |
| v0.2 | 2026-07-24 | Notion 심사용으로 권장안, 책임, 계약, 검증 기준을 단일 흐름으로 재구성 |
| v0.3 | 2026-07-27 | 설계 근거용 레퍼런스 이미지 슬롯 배치(REF-06~10, OWN-03), 이미지 자산 규약(§11) 추가 |