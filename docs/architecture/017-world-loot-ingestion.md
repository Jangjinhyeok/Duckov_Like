# 설계결정기록 017 — World loot의 Model ingestion

| 항목 | 내용 |
| --- | --- |
| 상태 | Accepted |
| 날짜 | 2026-09-28 |
| 작업 범위 | M3 첫 slice: World Loot → Bag → 기존 Inventory UI |
| Risk | LOW — 기존 API·저장·소유권 계약을 보존하는 로컬 기능 추가 |
| 판단 주체 | AI(사용자가 지정한 구현 범위) |
| 사용자 검토 | 2026-09-28 사용자 실검증 완료 보고 및 commit/push 승인 |
| 관련 | 식별자(009), 이벤트 기반 Model/ViewModel(015) |

## Context / Decision Question

기존 Model에는 외부 아이템을 생성하는 public command가 없다. 월드 payload를 inventory item으로
전환하되 실패 시 loot와 Model 양쪽을 보존하고, 열린 UI에 기존 ChangeSet으로 반영해야 한다.

## Decision / Rationale

- `AWorldLootActor`는 DefinitionTable·DefinitionRowName·Quantity만 저장한다. InstanceId·위치·회전은
  월드 payload에 없다. StaticMesh는 데모 표시용이며 Actor Tick은 사용하지 않는다.
- `AInventoryDemoPlayerController`가 기존 Model lifetime을 유지하고 `TryPickupLoot`로 Bag을 선택한다.
  `E`/`PickupNearestLoot`는 Pawn으로부터 250 Unreal units 안의 가장 가까운 loot를 선택한다.
  BlueprintCallable `TryPickupLoot`는 같은 World의 명시적 대상을 받는 gameplay command다.
- `UInventoryModel::TryAdd(ContainerId, DefinitionTable, DefinitionRowName, Quantity)`는 Actor를 모른다.
  정의 타입·행·양수 크기와 수량을 검증하고, 회전 없이 row-major first-fit 배치를 후보 container에서
  검증한다. 성공 후보가 있을 때만 기존 전역 allocator로 ID를 발급하고 한 번 commit한다.
- quantity는 단일 ItemInstance의 한도다. non-stackable은 1, stackable은 1..MaxStack이다.
  자동 분할·merge·rotation·sort를 하지 않는다. 실패 시 container·cache·allocator와 통지가 보존된다.
- 기존 `Apply`가 commit 이후 Added ChangeSet을 발행한다. callback 재진입은 기존 Model guard와
  Actor pickup guard로 거부한다. 성공 반환 이후 Actor를 소비 처리·숨김·Destroy한다.
  UE의 [Destroy는 지연 제거](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/AActor/Destroy)이므로
  소비 flag로 중복 획득도 막는다. UE 5.7 설치 소스의 Actor.h에서도 계약을 확인했다.
- Container/Item ViewModel과 Widget의 기존 구독 경로를 그대로 사용한다. 화면을 다시 만들거나
  loot 전용 UI mutation·polling·Tick을 추가하지 않는다.

## Alternatives / Trade-offs

PlayerController에서 container를 직접 배치하거나 Load로 전체 상태를 교체하면 mutation 경계와
부분 ChangeSet 의미가 흐려진다. Model의 작은 public command를 추가해 기존 Apply를 재사용한다.
새 session subsystem은 현재 소유권 문제를 해결하지 않으므로 도입하지 않는다.

## Consequences / Accepted Costs

first-fit은 후보 셀마다 footprint 점유를 검사한다. 후보 container는 한 번 복사하며, 기존 Apply의
변경 전 snapshot/diff 비용도 유지한다. pickup 입력 때만 실행하고 프레임마다 탐색하지 않는다.
Definition soft reference는 동기 로드한다. 데모 proximity interaction에는 시야·장애물 검사가 없다.
Inventory 화면은 기존 Menu input mode를 유지하므로 `E`는 화면을 닫은 gameplay 상태에서 사용한다.
열린 화면의 외부 pickup은 Blueprint/gameplay command 또는 console `PickupNearestLoot`로 발생시킬 수 있다.

## Revisit Conditions

extraction/stash commit을 구현할 때 session ownership과 gameplay policy를 재검토한다.
대규모 loot나 스트리밍 요구가 생기면 탐색 및 Definition 로드 비용을 측정한다.

## Verification

Model Automation은 first-fit·실패 보존·ID·Added/reentry를 검사한다. gameplay Automation은 실제 PIE
Controller/Model/WBP에서 commit 순서·실패 보존·즉시 Widget 추가·거리 선택을 검사한다.
전체 Duckov 회귀와 기존 100 item performance/NativeTick 검사를 실행한다.
이번 실행의 결과와 interactive 검증 상태는 `docs/worklog/M3.md`에 기록한다.
