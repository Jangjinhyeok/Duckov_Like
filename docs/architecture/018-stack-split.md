# 설계결정기록 018 — 스택 분할의 식별자와 원자적 반영

| 항목 | 내용 |
| --- | --- |
| 상태 | Accepted |
| 날짜 | 2026-09-29 |
| 작업 범위 | 인벤토리 기본 조작 — 스택 분할 |
| Risk | LOW — 기존 API·저장 형식·소유권을 보존하는 추가 명령 |
| 판단 주체 | AI(위임 범위) |
| 사용자 검토 | 구현 요청 확인, 세부 동작의 결과 수용은 미확인 |
| 관련 | 식별자 결정 009의 분할 ID 정책 미결 사항, ViewModel 흐름 015 |

## Context / Decision Question

사용자는 퀵 이동을 구현 범위에서 제외하고 스택 분할 구현을 요청했다.
분할 시 기존 아이템의 동일성과 총수량을 보존하고, 실패했을 때 부분 변경이 없어야 한다.

## Decision / Rationale

- `UInventoryModel::TrySplit(ContainerId, InstanceId, Quantity)`가 분할을 소유한다.
- 원본은 ID·위치·회전을 유지하며 수량만 줄어든다. 새 스택은 원본의 정의·회전을 유지하고 새 ID를 받는다.
- 분할 수량은 1 이상, 원본 수량 미만이다. 같은 컨테이너에서 현재 회전의 footprint로 row-major 첫 빈자리를 찾는다.
- 사본의 배치 검증을 끝낸 뒤 전역 allocator에서 ID를 발급하고 한 번에 반영한다.
  실패하면 아이템·점유 cache·allocator·변경 알림을 보존한다.
- 성공은 기존 `Apply`의 한 ChangeSet으로 원본 `Updated`와 새 스택 `Added`를 전달한다.
- View는 Shift+클릭 수량창과 임시 선택만 소유한다. `InteractionViewModel.Split`을 통해 Model을 호출한다.
  취소·비활성화·원본 ItemViewModel 변경은 창과 구독을 정리한다. 상시 Tick을 추가하지 않는다.

## Alternatives / Trade-offs

분할한 수량을 커서에 들고 목적지를 선택하는 방식은 별도의 드래그 수량·취소 계약이 필요하다.
이번에는 같은 컨테이너에 분할한 뒤 기존 드래그를 재사용한다. 반대쪽에 공간이 있더라도
원본 컨테이너가 가득 차면 분할은 실패한다. 자동 병합·회전은 수행하지 않는다.

## Consequences / Accepted Costs

`InventoryCore`는 UI 비의존을 유지하고 저장 레코드와 기존 ID 의미는 바뀌지 않는다.
원본 VM을 유지할 수 있으며 새 스택은 기존 목록 갱신 경로로 표시된다.
명령 시 사본 및 기존 ChangeSet 비교 비용에 더해 빈자리 탐색 비용이 발생한다.
아이템 수 N, 셀 수 G, footprint 면적 A에 대해 추가 임시 공간은 O(N+G), 탐색은 최악 O(G×A)다.
이 비용은 분할 명령 때만 발생한다.

## Revisit Conditions

분할 직후 목적지 선택, 컨테이너 간 직접 분할, 아이템별 추가 가변 상태 또는
분할 명령의 측정된 성능 문제가 요구될 때 재검토한다.

## Verification

수량 보존·ID 고유성·회전 배치·실패 원자성·재진입과 실제 WBP의 분할·취소·오류·수명을 검증한다.
이번 실행 결과와 미검증 항목은 [M3 작업 기록](../worklog/M3.md)의 스택 분할 항목에 기록한다.
