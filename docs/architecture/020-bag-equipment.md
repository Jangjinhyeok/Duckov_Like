# 설계결정기록 020 — 가방 슬롯과 원자적 가방 교체

| 항목 | 내용 |
| --- | --- |
| 상태 | Accepted — AI의 위임 범위 안에서 결정 |
| 날짜 | 2026-10-02 |
| 작업 범위 | M3 — 가방 슬롯 하나와 두 가방의 교체 |
| Risk | LOW — 기존 API·저장 레코드 v1·Model 소유권을 보존하는 추가 기능 |
| 판단 주체 | AI(사용자가 승인한 로컬 구현 범위) |
| 사용자 검토 | 구현 요청은 “진행하자”, 결과 수용과 직접 PIE는 별도 |
| 관련 | 단일 DataTable, Container 소유 구조, 저장 레코드와 원자적 복원, ViewModel 흐름 |

## Context / Decision Question

최소 Raid 성공 탈출 루프 이후 가방 장착으로 Bag 크기가 바뀌는 UI 흐름을 연결한다.
장착된 가방도 실제 ItemInstance여야 하며, 교체 실패 때 가방과 소지품이 유실되거나
장착 정보와 grid 크기가 달라져서는 안 된다. 장비 영속 저장이나 저장 포맷 변경은 이번 범위 밖이다.

## Decision / Rationale

- 기존 단일 `DT_ItemDefinitions`에 `SmallBag`(4×4)와 `LargeBag`(6×4)를 추가한다.
  두 가방의 물리적 footprint는 1×1이며 기존 세 행은 보존한다.
  `FItemDefinitionRow::BagGridSize`의 기본값은 0×0이다. 유효한 양의 크기·수량 1·비 스택·MaxStack 1을
  명시적인 가방 capability로 검증한다.
- `UInventoryModel::BindBagSlot`이 Slot·Bag·Exchange의 관계를 검증한다. 슬롯은 정확히 가방 하나를
  가지며 Bag 크기는 그 정의와 일치해야 한다. `BagSlot`은 기존 named container를 backing storage로
  사용하지만 일반 grid 연산으로 장비 규칙을 대신하지 않는다. 일반 이동·추가·분할·정렬 등은
  bound slot에 접근할 수 없고, Bag 직접 리사이즈도 장착 API를 우회할 수 없다.
- `TryEquipBag`은 bound Exchange(데모에서는 Stash)의 실제 아이템을 장착하고 이전 가방을
  Exchange의 row-major 첫 빈자리에 반환한다. Exchange·Slot·Bag 후보 상태를 모두 검증한 뒤
  한 번 commit하고 단일 ChangeSet을 보낸다. ID 발급·자동 회전·정렬·병합은 없다.
  장착 위치만 slot 원점으로 정규화하며 ID·정의·수량·회전은 보존한다.
- Bag 축소는 기존 배치·순서·수량·회전을 보존한다. 새 경계에 맞지 않으면 `ResizeOverflow`,
  이전 가방을 반환할 공간이 없으면 `NoSpace`다. 실패는 세 컨테이너·cache·allocator·binding·알림을 보존한다.
- 성공한 기존 `Load`는 reset 통지 전에 binding을 `NeedsValidation`으로 바꾼다.
  이때 장착 조회는 null이며 slot 보호와 Bag 직접 리사이즈 차단은 유지한다.
  명시적 재바인딩에 성공해야 장착 상태를 다시 표시한다. 실패 Load는 기존 binding도 보존한다.
- `FInventorySaveRecord`와 Mapper는 변경하지 않는다. 실제 장착 아이템은 기존 container/item 레코드에
  포함되지만 runtime binding 설정은 저장하지 않는다. 새 Model에서 Load한 호출자는 interaction을
  재개하기 전에 `BindBagSlot`을 호출해야 한다. 이것은 장비의 영속 저장 완료를 뜻하지 않는다.
- `StartBagEquipmentDemo`는 기존 세션을 Save한 후보에 소형 가방 슬롯·대형 가방만 추가한다.
  기존 항목 전체를 보존하고 정의·충돌·공간·counter를 사전 검증한 뒤 Load와 Bind를 실행한다.
  최초 준비는 4×4 Bag에 한정하고 중복 준비·분할/drag 중 준비·Raid 중 준비를 거부한다.
- View는 장착 버튼·임시 입력을, `UBagEquipmentViewModel`은 Model 조회와 ChangeSet/FieldNotify를,
  `UInteractionViewModel`은 명령과 실패 문구를 담당한다. 화면 활성 수명에 구독하며
  split·drag·Raid 중 교체를 button과 handler 양쪽에서 차단한다. 상시 Tick은 추가하지 않는다.

## Alternatives / Trade-offs

별도 equipment 저장 레코드는 슬롯 의미를 직접 저장할 수 있지만 version·migration 계약을
확대하므로 채택하지 않았다. 기존 레코드를 재사용하는 비용으로 runtime binding의 명시적 재검증이 필요하다.
가방 교체 후 자동 재배치도 추가하지 않았다. 플레이어가 경계를 벗어난 아이템을 옮긴 뒤 다시 시도한다.

## Consequences / Accepted Costs

Model이 아이템과 슬롯 관계를 소유하고 Controller는 데모 준비·Raid 정책을 맡는다.
InventoryCore는 UI/Actor에 의존하지 않는다. map·WBP·Config·모듈 의존은 변경하지 않는다.
교체 명령은 세 후보 container 복사와 반환 first-fit 탐색·resize cache 검증을 수행한다.
Exchange 셀 수 G, 이전 가방 footprint S, Bag 전체 셀/항목 footprint 합 B에 대해
대략 O(G×S + B) 비용이며 명령 실행 때만 발생한다. 프레임마다 검사하지 않는다.

## Revisit Conditions

가방 해제·Bag에서 직접 장착·다른 장비 슬롯·장비 영속 저장·map travel이 필요하면 재검토한다.
저장 형식·migration·데이터 손실 계약의 변경은 HIGH로 별도 영향·수용 조건을 정한다.

## Verification

Model 성공/실패 원자성·generic 우회 차단·재진입·v1 round-trip과 재바인딩,
VM 구독 수명과 실제 WBP의 확대/축소/오류/입력 차단을 Automation으로 확인한다.
실제 Build/Automation·독립 리뷰·자동 화면 캡처 및 사용자 직접 PIE의 결과는
[M3 작업 기록](../worklog/M3.md)에 실행 출처를 나누어 기록한다.
