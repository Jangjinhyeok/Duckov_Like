# 설계결정기록 019 — 데모 Raid의 성공 탈출과 원자적 Stash 반영

| 항목 | 내용 |
| --- | --- |
| 상태 | Accepted |
| 날짜 | 2026-10-02 |
| 작업 범위 | M3 — Raid 1회 루프의 최소 데모 |
| Risk | LOW — opt-in 로컬 기능, 기존 API·저장 형식·asset 보존 |
| 판단 주체 | AI(위임 범위) |
| 사용자 검토 | 2026-10-02 직접 PIE 성공 보고 및 commit·push 승인 |
| 관련 | World loot ingestion, ViewModel 흐름, 저장 레코드, 스택 분할 |

## Context / Decision Question

사용자는 장비 슬롯보다 먼저 Raid 1회 루프를 간단히 구현하도록 요청했다.
기존 World Loot → Bag과 인벤토리 화면을 사용해 준비·진입·루팅·탈출·Stash 반영을 연결한다.
기존 데모와 미커밋 스택 분할은 보존한다. 사망 손실·전투·장비·디스크 저장·map 전환은 이번 범위 밖이다.

## Decision / Rationale

- 기존 Controller의 `StartRaidDemo` console command로만 데모를 활성화한다.
  기본 상태는 Disabled이며 기존 인벤토리 데모와 테스트의 동작은 유지한다.
- Controller가 Preparation/InRaid와 완료 횟수·loot/탈출 지점 수명을 소유한다.
  준비 중에는 기존 Stash/Bag 화면을 사용한다. F5/`EnterRaid`로 진입한다.
- 원본 map loot는 비표시 템플릿으로 유지하고 진입 때 payload를 복사한 Actor를 생성한다.
  Raid 중 해당 Actor만 획득할 수 있다. 재진입 시 loot는 다시 생성된다.
- Raid 중 Stash VM 구독과 View의 이동·분할·정렬 접근을 차단한다. Bag 조작은 유지한다.
  단계 전환 성공은 열린 화면·drag·split 입력을 기존 비활성화 경로로 정리한다.
- 원기둥 marker를 탈출 지점으로 사용한다. F6/`ExtractRaid`는 Pawn의 XY 거리가 180 units 이내일 때 실행된다.
  현재 데모 Pawn이 비행할 수 있으므로 높이는 탈출 판정에 사용하지 않는다.
- `UInventoryModel::TryTransferAll(Bag, Stash)`만 실제 아이템 이전을 소유한다.
  source 배열 순서와 현재 회전의 footprint로 row-major first-fit을 찾는다.
  ID·정의·수량·회전을 보존하고 자동 병합·회전·정렬·ID 발급을 하지 않는다.
- 전체 후보 배치 성공 후 양쪽 컨테이너를 반영하고 기존 Apply의 단일 ChangeSet을 보낸다.
  실패는 양쪽 items/cache/allocator/통지를 보존한다. 같은 컨테이너 이전은 InvalidContainer다.
- 반영 성공 뒤에만 남은 Raid loot를 제거하고 Preparation으로 복귀한다.
  공간 부족이면 InRaid·Bag·Stash·남은 loot를 유지한다. 세션 전환 중 재진입과 추가 획득을 거부한다.
- native 상태 위젯은 Controller의 단계 변경/명령 결과 때만 갱신한다. 상시 Tick을 추가하지 않는다.

## Alternatives / Trade-offs

별도 map·영속 세션·SaveGame 저장을 먼저 도입하면 이동과 데이터 복구 계약까지 넓어진다.
이번에는 같은 PIE world에서 성공 탈출만 연결한다. 종료하면 데모 초기 데이터로 돌아가며,
장비/사망/영속 저장을 포함한 M3 전체 완료를 뜻하지 않는다.

전체 이전은 기존 단건 이동을 반복 호출하는 방식보다 실패 원자성을 명확히 보장한다.
대신 first-fit은 가능한 모든 배치를 탐색하지 않으므로 총면적에 여유가 있어도 실패할 수 있다.
Stash가 차면 플레이어가 Bag 스택을 합쳐 재시도할 수 있지만, 공간을 보장하는 복귀·초과 보관 정책은 없다.
퀵 이동 입력은 추가하지 않는다. 이 명령은 성공 탈출의 batch 반영에만 연결한다.

## Consequences / Accepted Costs

InventoryCore는 UI·Actor·Raid 정책에 의존하지 않는다. Model이 item 상태를 소유하고
Controller가 세션 정책, View/VM이 기존 입력·표시를 맡는다. 저장 레코드와 map/WBP/Config는 변경하지 않는다.
배치 탐색은 최악 O(Nbag × Gstash × footprint)이며, destination 후보와 기존 Apply의 비교용 사본이 필요하다.
이 비용은 탈출 명령 때만 발생한다. 원본 loot Actor를 비표시 상태로 유지하는 비용도 데모 규모에서 감수한다.

## Revisit Conditions

실제 캐릭터·map travel·사망 손실·Stash 초과 보관·장비·영속 저장이 요구되면 세션 정책을 재검토한다.
저장 형식·migration·데이터 손실을 바꾸는 확장은 HIGH로 별도 영향 및 수용 조건을 정한다.

## Verification

Model 전체 성공·부분 실패·현재 회전·빈 원본·동일/없는 컨테이너·ChangeSet/재진입과
실제 WBP PIE의 준비/진입/획득/탈출/복귀·Stash 차단·분할 취소·중복·실패/재시도를 검사한다.
실제 Build/Automation·독립 리뷰와 직접 PIE의 실행 여부는 [M3 작업 기록](../worklog/M3.md)에 구분한다.
