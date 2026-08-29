# M1a — 배치·이동·스택 슬라이스

기간: 2026-08-18 ~ 2026-08-29 · 상태: **진행 중**

## 목표

M1a는 `InventoryCore`의 그리드 배치와 컨테이너 간 이동을 하나의 검증 가능한 슬라이스로 구현하는 것을 목표로 했다. `TryPlace`로 아이템의 점유 가능성을 검증하고, `Operation Service`가 두 컨테이너에 걸친 이동을 원자적으로 처리하도록 구조를 잡았다. 구현 결과 배치 8개와 이동 7개, 합계 15개의 Automation Test가 통과한 것으로 기록되어 있다.

이어서 스택 병합 슬라이스(설계결정기록 011·012, `TryStack`, Automation Test 9개)를
완료해 M1a 테스트 매트릭스의 배치·이동·스택 세 범주가 전부 구현됐다 — 배치 8개, 이동 7개, 스택 9개로 합계 24개가 통과했다.

상태를 여전히 **진행 중**으로 남긴 이유는 CLAUDE.md §5.6이 요구하는 마일스톤 종료
조건 — 코드를 보지 않고 구조를 설명하는 "설명 역전" — 을 아직 수행하지 않았기
때문이다.

---

## 작업 내역

### `1eb154d` — `build(inventory): InventoryCoreTests 모듈 신설`

`DuckovLike.uproject`에 Editor-only 테스트 모듈을 등록하고, `Source/InventoryCoreTests/InventoryCoreTests.Build.cs`에 모듈 정의를 추가했으며, `Source/InventoryCoreTests/Private/InventoryCoreTestsModule.cpp`에 최소 모듈 진입점을 만들었다. `InventoryCore`의 배치·이동 동작을 별도 Automation Test 모듈에서 검증할 수 있는 골격을 신설했다. 변경 파일은 `git show --stat 1eb154d`로 확인했다.

### `ef4ebdc` — `feat(inventory): 그리드 배치 핵심 타입과 TryPlace 구현`

`Source/InventoryCore/Public/ItemDefinitionRow.h`, `Source/InventoryCore/Public/ItemInstance.h`, `Source/InventoryCore/Public/InventoryContainer.h`, `Source/InventoryCore/Public/InventoryOperationTypes.h`, `Source/InventoryCore/Public/InventoryPlacement.h`에 Definition·Instance·Container·실패 사유·배치 계약을 공개하고, `Source/InventoryCore/Private/ItemInstance.cpp`, `Source/InventoryCore/Private/InventoryContainer.cpp`, `Source/InventoryCore/Private/InventoryPlacement.cpp`에 발급기·컨테이너 초기화·배치 구현을 추가했다. 그리드 좌표와 아이템 배치에 필요한 핵심 Model 타입을 `InventoryCore`에 추가하고, 배치 가능 여부를 먼저 검증한 뒤 상태를 반영하는 `TryPlace` 흐름을 구현했다. 파일별 역할과 데이터 흐름은 `docs/architecture/MODULE_MAP.md`의 해당 설명을 참고했다.

### `9b2054a` — `test(inventory): 배치 Automation Test 8개 추가`

`Source/InventoryCoreTests/Private/InventoryPlacementTests.cpp`에 그리드 경계, 겹침, 회전과 같은 배치 규칙을 확인하는 Automation Test 8개를 추가했다. 테스트가 `InventoryCore`의 공개 배치 계약을 직접 확인하도록 구성해 `TryPlace`의 성공·실패 경로를 구분했다.

검증 결과: 기록된 사실은 배치 Automation Test 8개 통과다. 이 worklog에서는 빌드나 테스트를 직접 실행하지 않았다.

### `724d71a` — `feat(inventory): Operation Service와 컨테이너 간 원자적 이동 구현`

`Source/InventoryCore/Public/InventoryOperations.h`에 `FInventoryOperations::TryMove` 공개 계약을 추가하고, `Source/InventoryCore/Private/InventoryOperations.cpp`에 상태 없는 조정자를 구현했다. `Source/InventoryCore/Public/InventoryOperationTypes.h`와 `Source/InventoryCore/Public/InventoryPlacement.h`의 공개 계약을 이동 경로에 맞게 보강하고, `Source/InventoryCore/Private/InventoryPlacement.cpp`와 `Source/InventoryCore/Private/InventoryPlacementInternal.h`를 통해 배치·이동이 공통 footprint·경계 검증을 사용하게 했다. 두 `Container`를 조정하는 `Operation Service`가 출발지 제거와 목적지 배치를 함께 검증한 뒤 반영하는 원자적 이동 흐름을 구현했다. 파일별 역할과 데이터 흐름은 `docs/architecture/MODULE_MAP.md`에 정리된 범위에서만 참고했다.

### `1c73f38` — `test(inventory): 이동 Automation Test 7개 추가`

`Source/InventoryCoreTests/Private/InventoryMoveTests.cpp`에 컨테이너 간 이동의 성공·실패와 원자성 계약을 확인하는 Automation Test 7개를 추가했다. 배치 테스트와 합쳐 배치·이동 범주에서 15개가 통과한 상태를 확인할 수 있게 했다.

검증 결과: 기록된 사실은 이동 Automation Test 7개와 배치 8개, 합계 15개 통과다. 이 worklog에서는 빌드나 테스트를 직접 실행하지 않았다.

### `a301a7c` — `docs: 스택 병합 규칙을 설계결정기록 011·012로 확정`

스택 가능 여부·동질성 판정(011)과 병합 연산 계약(012)을 Option Sweep 절차로 확정해
`docs/architecture/011-stackability-and-homogeneity.md`,
`docs/architecture/012-stack-merge-operation-contract.md`로 남겼다.

### `42c5fae` — `feat(inventory): 스택 병합 TryStack 구현`

`Source/InventoryCore/Public/ItemDefinitionRow.h`에 `bStackable`·`MaxStack`을,
`Source/InventoryCore/Public/InventoryOperationTypes.h`에 `StackMismatch`·`StackFull`을
추가하고, `Source/InventoryCore/Public/InventoryOperations.h`·
`Source/InventoryCore/Private/InventoryOperations.cpp`에
`FInventoryOperations::TryStack`을 구현했다. 같은 컨테이너로 호출되는 경우를 대비해
Target 갱신을 Source `RemoveAt` 이전에 끝내는 순서를 지켰다.

검증 결과: 빌드 `Result: Succeeded`로 확인됨.

### `e65760d` — `test(inventory): 스택 병합 Automation Test 9개 추가`

`Source/InventoryCoreTests/Private/InventoryStackTests.cpp`에 완전/부분 병합, 최대치
(StackFull), 상태 불일치(StackMismatch — 다른 Definition·`bStackable=false` 두 경로),
존재하지 않는 InstanceId, 동일/다른 컨테이너 병합을 검증하는 Automation Test 9개를
추가했다.

검증 결과: 이번 슬라이스는 이전 두 슬라이스와 달리 **실제로 UE Automation Test를
실행해 확인했다** — `Automation RunTests Duckov.InventoryCore.Stack` 실행 로그에서
9개 전부 `Test Completed. Result={Success}`, `TEST COMPLETE. EXIT CODE: 0`을
확인했다(Architect 세션이 Codex Builder의 로그를 직접 읽어 검증).

---

## AI 활용

| 내가 결정한 것 | AI가 수행한 것 | 내가 수정한 것 |
| --- | --- | --- |
| **설계결정기록 001 — Item Definition 형식**<br><br>“아이템 하나하나 애셋 파일로 만드는 방식은 해봤지만, 한눈에 보고 관리하는 게 목적이라<br>DataTable을 원함.”<br><br>**설계결정기록 003 — Container 소유 구조·점유 캐시**<br><br>“캐시-목록 동기화가 어긋날 수 있다는 리스크는 인지하고 있고, 구현을 진행하면서 그때그때<br>자문을 구하기로 함(사전에 전부 설계하지 않음).”<br><br>**설계결정기록 004 — Operation Service 원자성**<br><br>“Container끼리 서로를 알고 처리하는 것보다, 별도 조정자가 둘을 동시에 다루는 편이 더<br>낫다고 재고 후 판단함.”<br><br>**설계결정기록 009 — Item Instance 식별자**<br><br>“이 프로젝트가 증명하려는 축은 UI다. GDD의 한 줄 요약도 "UI 프로그래밍 포트폴리오<br>프로젝트"다. 식별자 선택은 포트폴리오에서 한 줄로 지나가는 항목이다. 거기에 무게를 두지<br>않는다. 앞으로 계속 코드를 만지는 동안 int가 더 다루기 편한 방향이다.”<br><br>**설계결정기록 010 — Item Instance 표현**<br><br>“GC 비용이 제일 걸리네, USTRUCT로 기울어”<br><br>“uobject로 하면, 맵에 있는 모든 아이템들을 gc로 관리해야 하니까 그게 가장 비용이 크니까<br>값 형태인 ustruct로 하는게 더 좋다라고 이해했어.” | 설계결정기록 001에서 `UPrimaryDataAsset`과 단일 `DataTable`을, 설계결정기록 003에서 계산형 점유와 `OccupancyCache` 및 Container/Actor 소유 선택지를, 설계결정기록 004에서 Container 자체 처리와 정적 `Operation Service` 선택지를 나열했다. 설계결정기록 009에서는 좌표·배열 인덱스·주소·문자열·`int32`·`FGuid`를, 설계결정기록 010에서는 `USTRUCT`와 `UObject`를 비교했다. 각 AI Review에서 참조 안정성, 소유권·수명, 캐시 동기화, 원자성, coupling·testability, GC와 값 복사 비용, 저장·스택 분할 및 테스트 결정성 축을 검토했다. 그 결과 `TryPlace`/`TryMove`, `OccupancyCache`, 정적 `Operation Service`를 구현하고 배치 8개·이동 7개의 Automation Test를 작성했다. 설계결정기록 009의 “쓰기 편해서” 근거를 “읽기 편해서”로 교정하고 카운터를 전역 단일화하는 판단을 반영했다. 스태시의 레벨 전환 생존 문제에 대해서는 “필요할 때만 Actor에 잠깐 붙여서 보여주는 그림”으로 정정된 AI Review 근거를 남겼다. | 설계결정기록 001은 단일 `DataTable`, 설계결정기록 003은 `Container` 데이터의 Actor 비종속·`OccupancyCache` 정책, 설계결정기록 004는 상태 없는 정적 `Operation Service`로 Final Decision을 반영했다. 설계결정기록 009는 식별자를 `int32` 순차 번호로 하고 “읽기 편해서”를 근거로 하며 전역 단일 카운터를 쓰도록 반영했다. 설계결정기록 010은 `USTRUCT` 표현을 선택하고, “맵에 있는 아이템”이 아니라 컨테이너 안의 모든 Instance가 GC 추적 대상이라는 정정을 반영했다. |

| **설계결정기록 011 — 스택 가능 여부·동질성**<br><br>"이 코드를 내가 아닌 다른 사람이 봤을 때도 아 이건 이런 코드구나라는 걸 확인할 수 있어야 된다고 생각해 주석없이도 그래서 b야."<br>"지금 당장은 확장성보다는 지금 있는 구조에 정합한게 더 맞는거같아."<br>"bstackable만 보는걸로해."<br><br>**설계결정기록 012 — 병합 연산 계약**<br><br>"try move에 책임이 늘어나는것보다는 책임분산이 더맞는 판단이라고 생각해."<br>"target quantity라는게 있으니까 이걸 쓰는게 맞다."(초기 근거 "구조체 필드를 늘리면 스택 메모리가 많아질거라 생각해서"는 AI Review로 부정확함이 지적돼 교정됨)<br>"occupied 를 잘 파악하고 있는것도 있지만 stack mismatch자체를 신설하는게 코드를 디버깅할 때 이해하기 편할거라 생각했고, stackfull은 성공이라고 하기에는 합쳐지지 않았으니까 실패가 더 맞다고 생각해서."<br>"코드흐름과 맞다는 힌트가 있어서." | 011에서 `MaxStack` 필드 표현(단일 필드 vs 분리)과 동질성 판정 범위(고정 비교 vs 확장 가능한 훅) 선택지를 나열했고, `bStackable`/`MaxStack` 독립 필드의 모순 가능성을 지적해 판정 기준을 명확히 하도록 이끌었다. 012에서는 API 형태·반환 방식·실패 사유·완전 병합 처리 선택지를 나열했고, "구조체 필드가 늘어나면 스택 메모리가 는다"는 근거가 부정확함을 교정했다. 그 결과 헤더 3개, `TryStack` 구현, 테스트 9개를 작성했다. | 011은 동질성 판정을 확장 가능한 훅에서 고정 비교로 바꾸고, 판정 기준을 `bStackable`만 보는 것으로 확정했다. 012는 반환 방식의 근거를 "스택 메모리"에서 "Target.Quantity 재사용"으로 교정해 유지했다. |

---

## 막혔던 것

이 슬라이스에서는 큰 장애 없이 진행되었다. 유일한 보류 사항은 ID 정책이다. `docs/architecture/009-item-instance-identifier.md`에는 “스택 분할 시 어느 쪽이 기존 번호를 유지하는가 — 미결. M1a 테스트 매트릭스(배치·이동·스택)에 "분할" 테스트가 없어 지금은 필요 없음 — 실제로 분할을 구현할 때(M1b 이후) 결정한다.”라고 되어 있다. `docs/architecture/010-item-instance-representation.md`에도 “번호 발급 시점, 스택 분할 시 번호 정책, 저장 시 카운터 복원, 테스트 카운터 초기화는 여전히 미결이며 설계결정기록 009가 열어 둔 채로 남는다.”라고 되어 있다. 이는 막혔다기보다 M1a 범위 밖으로 명시적으로 미룬 사항이다.

스택 슬라이스 구현 중, UE 빌드가 `UnauthorizedAccessException`(UBT 로그 파일 접근
경합으로 추정)으로 몇 차례 `Result: Failed (OtherCompilationError)`를 반환했다.
소스 코드 문제가 아니라 파일 접근 경합이라 재시도로 해결됐고 최종적으로
`Result: Succeeded`를 받았다. 프로젝트 설정은 건드리지 않고 재시도만으로 넘어갔다.

---

## 다음으로 넘길 것

- 스택 분할 시 ID 유지 규칙은 설계결정기록 009에서 M1b로 명시 이연되어 있다.
- 설계결정기록 000(모듈 경계)은 아직 `Proposed` 상태다.
- `CLAUDE.md` §9 표에는 설계결정기록 005(ChangeSet 형태)가 "M1a 전 필요"로 되어 있으나, 설계결정기록 005 파일 자체는 아직 없다.
- M1a 테스트 매트릭스(배치·이동·스택)가 24개로 전부 구현됐다. CLAUDE.md §5.6의 설명 역전을 수행해야 상태를 `완료`로 바꿀 수 있다.
- 설명 역전 이후 다음 마일스톤은 M1b(정렬·저장·리사이즈)다.
