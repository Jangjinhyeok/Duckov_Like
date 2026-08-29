# 모듈 지도 — 배치·이동 슬라이스 (M1a)

> 설계결정기록이 아니다. 결정 기록이 아니라 **이미 구현된 구조를 나중에 README/포트폴리오 글로 옮겨 쓸 수 있게
> 정리한 참고 문서**다. 범위가 늘어날 때마다(스택 슬라이스, M1b) 이어서 갱신한다.

**범위**: 커밋 `1eb154d`~`1c73f38` (5개) — `InventoryCoreTests` 모듈 신설, 배치(Placement) 핵심 타입과
`TryPlace`, Operation Service와 `TryMove`, Automation Test 15개.

## 구역별 파일과 역할

| 구역 | 파일 | 역할 한 줄 |
| --- | --- | --- |
| **모듈 골격** | `Source/InventoryCoreTests/InventoryCoreTests.Build.cs` | `InventoryCore`만 보는 Editor 전용 테스트 모듈 정의 |
| | `Source/InventoryCoreTests/Private/InventoryCoreTestsModule.cpp` | 위 모듈의 최소 진입점 |
| | `DuckovLike.uproject` | `InventoryCoreTests`를 Editor-only 모듈로 등록 |
| **배치 핵심 타입** | `Source/InventoryCore/Public/ItemDefinitionRow.h` | 아이템 종류(고정 스펙) — `FIntPoint Size` |
| | `Source/InventoryCore/Public/ItemInstance.h` | 아이템 개체 상태 — `InstanceId`, `DefinitionRowName`, `DefinitionTable`, `Quantity`, `AnchorCell`, `bRotated` + 전역 순차 ID 발급기(`FItemInstanceIdAllocator`) |
| | `Source/InventoryCore/Private/ItemInstance.cpp` | 위 발급기 구현 |
| | `Source/InventoryCore/Public/InventoryContainer.h` | 그리드 — `GridSize`, `Items`(진실), `OccupancyCache`(캐시) |
| | `Source/InventoryCore/Private/InventoryContainer.cpp` | `MakeEmpty` 구현 |
| | `Source/InventoryCore/Public/InventoryOperationTypes.h` | 실패 사유 enum — `None`/`NoSpace`/`Occupied`/`ItemNotFound` |
| **배치·이동 로직** | `Source/InventoryCore/Public/InventoryPlacement.h` | `TryPlace`(단일 컨테이너 배치), `RebuildOccupancyCache` 공개 시그니처 |
| | `Source/InventoryCore/Private/InventoryPlacement.cpp` | 위 두 함수의 구현 |
| | `Source/InventoryCore/Private/InventoryPlacementInternal.h` | `TryPlace`/`TryMove`가 공유하는 비공개 헬퍼 — `GetCellIndex`, `TryGetFootprint`, `FitsInContainer` |
| | `Source/InventoryCore/Public/InventoryOperations.h` | `FInventoryOperations::TryMove` 공개 시그니처 |
| | `Source/InventoryCore/Private/InventoryOperations.cpp` | 컨테이너 간(또는 동일 컨테이너 내) 원자적 이동 구현 |
| **테스트** | `Source/InventoryCoreTests/Private/InventoryPlacementTests.cpp` | 배치 Automation Test 8개 |
| | `Source/InventoryCoreTests/Private/InventoryMoveTests.cpp` | 이동 Automation Test 7개 |

## 데이터 흐름

```
FItemDefinitionRow (DataTable, 고정 스펙)
        |  Row 조회 (TryGetFootprint)
        v
FItemInstance (개체 상태) --------------------+
        |                                     |
        v                                     |  같은 footprint 조회 로직 공유
TryPlace(Container, Item)                     |  (InventoryPlacementInternal)
        |                                     |
        v                                     |
FInventoryContainer                           |
  Items        <-- 진실(source of truth) -----+
  OccupancyCache <-- 캐시(RebuildOccupancyCache로 재생성)
        ^
        |
TryMove(Source, Dest, InstanceId, DestAnchorCell, bDestRotated)
  1. Source.Items에서 InstanceId로 검색 (실패 시 ItemNotFound)
  2. 이동 후 좌표로 footprint 재계산, 목적지 경계 확인 (실패 시 NoSpace)
  3. 목적지 점유 검사 — 동일 컨테이너 이동 시 "자기 자신이 있던 칸"만 예외 처리
  4. 전부 통과해야 커밋: Source에서 RemoveAt -> Dest에 Add -> 양쪽 OccupancyCache 재생성
```

## 설계 의도 — 왜 이렇게 나눴는가

- **`InventoryPlacementInternal`은 `Public/`이 아니다.** `TryPlace`와 `TryMove`가 같은 footprint/경계
  계산을 공유해야 했지만, 이 계산은 소비자(`DuckovLike`)가 알 필요 없는 내부 구현이다.
  `Private/`에 두어 공개 계약을 좁게 유지했다.
- **`OccupancyCache`는 `Items`의 파생물이지 별도 진실이 아니다.** 이동처럼 두 컨테이너를 동시에
  건드리는 연산은 캐시를 부분 갱신하는 대신 통째로 `RebuildOccupancyCache`로 재생성한다 — 부분
  갱신 로직의 엣지케이스(부분 실패, 겹친 갱신 순서)를 원천적으로 없앤다.
- **실패는 상태를 바꾸지 않는다.** `TryMove`는 모든 검증을 통과한 뒤에야 `RemoveAt`/`Add`를 실행한다
  (CLAUDE.md §4 "실패한 연산은 상태를 바꾸지 않는다"의 실제 구현).

## 알려진 미결 지점 (의도적으로 남김)

- `TryPlace`를 `MakeEmpty()`로 초기화하지 않은 `FInventoryContainer`(기본 생성자)에 바로 호출하면
  `OccupancyCache`가 비어 있어 범위를 벗어난 접근이 날 수 있다. M1a 테스트 매트릭스에는 없는
  경로라 지금은 고치지 않음 — 저장/역직렬화를 다루는 M1b에서 다시 볼 것.
- 스택 분할 시 ID 유지 규칙, 저장/로드 시 ID 카운터 복원 규칙은 설계결정기록 009에 M1b로 명시 이연됨.
