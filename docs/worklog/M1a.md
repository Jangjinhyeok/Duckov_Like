# M1a — 배치·이동 슬라이스

기간: 2026-08-18 · 상태: **진행 중**

## 목표

M1a는 `InventoryCore`의 그리드 배치와 컨테이너 간 이동을 하나의 검증 가능한 슬라이스로 구현하는 것을 목표로 했다. `TryPlace`로 아이템의 점유 가능성을 검증하고, `Operation Service`가 두 컨테이너에 걸친 이동을 원자적으로 처리하도록 구조를 잡았다. 구현 결과 배치 8개와 이동 7개, 합계 15개의 Automation Test가 통과한 것으로 기록되어 있다.

상태를 **진행 중**으로 남긴 이유는 M1a 테스트 매트릭스가 배치·이동·스택 세 범주 약 18개를 대상으로 하지만, 스택 병합 로직과 테스트가 아직 없어 현재 15개까지만 통과했기 때문이다.

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

---

## AI 활용

| 내가 결정한 것 | AI가 수행한 것 | 내가 수정한 것 |
| --- | --- | --- |
| **설계결정기록 001 — Item Definition 형식**<br><br>“아이템 하나하나 애셋 파일로 만드는 방식은 해봤지만, 한눈에 보고 관리하는 게 목적이라<br>DataTable을 원함.”<br><br>**설계결정기록 003 — Container 소유 구조·점유 캐시**<br><br>“캐시-목록 동기화가 어긋날 수 있다는 리스크는 인지하고 있고, 구현을 진행하면서 그때그때<br>자문을 구하기로 함(사전에 전부 설계하지 않음).”<br><br>**설계결정기록 004 — Operation Service 원자성**<br><br>“Container끼리 서로를 알고 처리하는 것보다, 별도 조정자가 둘을 동시에 다루는 편이 더<br>낫다고 재고 후 판단함.”<br><br>**설계결정기록 009 — Item Instance 식별자**<br><br>“이 프로젝트가 증명하려는 축은 UI다. GDD의 한 줄 요약도 "UI 프로그래밍 포트폴리오<br>프로젝트"다. 식별자 선택은 포트폴리오에서 한 줄로 지나가는 항목이다. 거기에 무게를 두지<br>않는다. 앞으로 계속 코드를 만지는 동안 int가 더 다루기 편한 방향이다.”<br><br>**설계결정기록 010 — Item Instance 표현**<br><br>“GC 비용이 제일 걸리네, USTRUCT로 기울어”<br><br>“uobject로 하면, 맵에 있는 모든 아이템들을 gc로 관리해야 하니까 그게 가장 비용이 크니까<br>값 형태인 ustruct로 하는게 더 좋다라고 이해했어.” | 설계결정기록 001에서 `UPrimaryDataAsset`과 단일 `DataTable`을, 설계결정기록 003에서 계산형 점유와 `OccupancyCache` 및 Container/Actor 소유 선택지를, 설계결정기록 004에서 Container 자체 처리와 정적 `Operation Service` 선택지를 나열했다. 설계결정기록 009에서는 좌표·배열 인덱스·주소·문자열·`int32`·`FGuid`를, 설계결정기록 010에서는 `USTRUCT`와 `UObject`를 비교했다. 각 AI Review에서 참조 안정성, 소유권·수명, 캐시 동기화, 원자성, coupling·testability, GC와 값 복사 비용, 저장·스택 분할 및 테스트 결정성 축을 검토했다. 그 결과 `TryPlace`/`TryMove`, `OccupancyCache`, 정적 `Operation Service`를 구현하고 배치 8개·이동 7개의 Automation Test를 작성했다. 설계결정기록 009의 “쓰기 편해서” 근거를 “읽기 편해서”로 교정하고 카운터를 전역 단일화하는 판단을 반영했다. 스태시의 레벨 전환 생존 문제에 대해서는 “필요할 때만 Actor에 잠깐 붙여서 보여주는 그림”으로 정정된 AI Review 근거를 남겼다. | 설계결정기록 001은 단일 `DataTable`, 설계결정기록 003은 `Container` 데이터의 Actor 비종속·`OccupancyCache` 정책, 설계결정기록 004는 상태 없는 정적 `Operation Service`로 Final Decision을 반영했다. 설계결정기록 009는 식별자를 `int32` 순차 번호로 하고 “읽기 편해서”를 근거로 하며 전역 단일 카운터를 쓰도록 반영했다. 설계결정기록 010은 `USTRUCT` 표현을 선택하고, “맵에 있는 아이템”이 아니라 컨테이너 안의 모든 Instance가 GC 추적 대상이라는 정정을 반영했다. |

---

## 막혔던 것

이 슬라이스에서는 큰 장애 없이 진행되었다. 유일한 보류 사항은 ID 정책이다. `docs/architecture/009-item-instance-identifier.md`에는 “스택 분할 시 어느 쪽이 기존 번호를 유지하는가 — 미결. M1a 테스트 매트릭스(배치·이동·스택)에 "분할" 테스트가 없어 지금은 필요 없음 — 실제로 분할을 구현할 때(M1b 이후) 결정한다.”라고 되어 있다. `docs/architecture/010-item-instance-representation.md`에도 “번호 발급 시점, 스택 분할 시 번호 정책, 저장 시 카운터 복원, 테스트 카운터 초기화는 여전히 미결이며 설계결정기록 009가 열어 둔 채로 남는다.”라고 되어 있다. 이는 막혔다기보다 M1a 범위 밖으로 명시적으로 미룬 사항이다.

---

## 다음으로 넘길 것

- 스택 병합(완전 병합·부분 병합·최대치·상태 불일치)은 아직 착수하지 않았다. `InventoryOperationTypes.h`에 `StackFull` 등 관련 실패 사유도 아직 없다.
- 스택 분할 시 ID 유지 규칙은 설계결정기록 009에서 M1b로 명시 이연되어 있다.
- 설계결정기록 000(모듈 경계)은 아직 `Proposed` 상태다.
- `CLAUDE.md` §9 표에는 설계결정기록 005(ChangeSet 형태)가 “M1a 전 필요”로 되어 있으나, 설계결정기록 005 파일 자체는 아직 없다. 이 worklog에서는 임의로 만들지 않는다.
- `docs/architecture/MODULE_MAP.md`는 아직 git에 커밋되지 않은 `untracked` 상태다.
