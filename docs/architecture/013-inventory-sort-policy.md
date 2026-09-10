# 설계결정기록 013 — 정렬 순서와 원자적 배치 계획

| 항목 | 내용 |
| --- | --- |
| 상태 | Accepted |
| 날짜 | 2026-09-10 |
| 작업 범위 | M1b / 자동 정렬 |
| Risk | LOW — 기존 API와 저장 형식을 변경하지 않는 Model 연산 추가 |
| 판단 주체 | AI(위임 범위) |
| 사용자 검토 | 미검토 |
| 관련 | Operation Service 원자성(004), Instance 식별자(009), INVENTORY_DESIGN 자동 정렬 초안 |

## Context / Decision Question

기존 배치 판정을 재사용해 정렬하며, 같은 항목 집합에 같은 결과를 내고 실패 시 원본을 보존해야 한다.
면적이 같은 항목의 순서와 자동 회전 여부는 기존 계약에 정해져 있지 않다.

## Decision / Rationale

`FInventoryOperations::TrySort`가 면적 내림차순, 동률이면 `InstanceId` 오름차순으로 처리한다.
각 항목은 현재 회전을 유지한 채 row-major first-fit으로 배치한다. 배열의 기존 순서는 결과에 영향을 주지 않는다.
고유 InstanceId와 유효한 Container라는 기존 Model 전제를 따른다.

빈 임시 Container에 `TryPlace`로 배치하며 전체 성공 후 한 번에 원본을 교체한다.
성공 시 `None`, Definition 해석 또는 배치 실패 시 기존 `NoSpace`를 반환한다.
위치와 Items 배열 순서 및 그에 따른 cache 인덱스만 바뀌며 ID·Definition·수량·회전은 유지한다.

## Alternatives / Trade-offs

자동 회전이나 backtracking은 더 많은 배치를 성공시킬 수 있지만 탐색 비용과 정책 복잡도가 늘어난다.
이번에는 현재 방향을 보존하는 greedy로 한정한다. 유효한 기존 배치가 있어도 정렬은 실패할 수 있으며,
이때 원본을 유지한다. `NoSpace`는 이 정렬 정책에서 배치하지 못했다는 뜻이지 가능한 배치가 없다는 증명이 아니다.

## Consequences / Accepted Costs

Model의 정적 Operation Service만 상태를 변경한다. UI 의존이나 상시 Tick은 추가하지 않는다.
항목 수 N, 셀 수 G, 최대 footprint 면적 A에 대해 정렬 O(N log N), 탐색 최악 O(NGA),
임시 메모리 O(N + G)다. 사용자 명령 시 호출하는 연산이며 frame hot path에 넣지 않는다.
기존 `TryPlace` 재사용으로 후보마다 Definition 조회가 반복되고 soft reference의 동기 로드 비용이 발생할 수 있다.
동작을 중복 구현하는 최적화는 측정 결과가 필요할 때 검토한다.

## Revisit Conditions

자동 회전을 포함하는 UX가 요구되거나 실제 인벤토리 규모에서 정렬 지연이 관찰될 때 정책과 탐색을 재검토한다.

## Verification

Automation Test로 입력 순서 독립성·반복 정렬·row-major 좌표·cache·항목 데이터 보존,
유효 배치에서의 greedy 실패 원자성, 회전 보존, 빈 Container, Definition 부재를 검증한다.
실제 실행 결과는 M1b worklog에 기록한다. 사용자 수용·이해 상태는 미확인이다.
