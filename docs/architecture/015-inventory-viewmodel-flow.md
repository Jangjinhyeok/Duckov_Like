# 설계결정기록 015 — 이벤트 기반 Model과 ViewModel 연결

| 항목 | 내용 |
| --- | --- |
| 상태 | Accepted |
| 날짜 | 2026-09-20 |
| 작업 범위 | M2 / Editor 화면 조립 전 코드 기반 |
| Risk | LOW — 기존 연산·저장 형식·식별자 계약을 보존하는 연결 계층 추가 |
| 판단 주체 | AI(후속 구현 위임 범위) |
| 사용자 검토 | Editor 작업 전 요약 요청. 구현 결과 보고 예정 |
| 관련 | 정적 연산 004, 값 타입 010, 저장 레코드 014, 설계 문서의 C1/C2/C3 |

## Context / Decision Question

Model의 배치·이동·스택·정렬·리사이즈·저장 레코드가 있으나 UI가 상태를 관찰하거나 드래그를 미리 검증할 연결은 없었다.
사용자는 저장 변경 commit/push 후 다음 구현을 계속하고 Editor 작업 전에 멈추도록 요청했다.
현재 범위는 Model Query·이벤트·ViewModel·Interaction이다. M2 화면 전체 완료를 의미하지 않는다.

## Decision / Rationale

- InventoryCore의 UInventoryModel이 이름 있는 Container 집합을 UPROPERTY로 소유한다. Actor와 독립적이며 위치·회전·수량의 원본은 InventoryCore에만 있다. 스태시와 레이드의 게임 정책/타입을 하나로 합치는 결정이 아니다.
- FInventoryOperations는 상태 없는 정적 서비스로 유지한다. UInventoryModel은 명령을 위임하고 commit 후 ChangeSet을 발행하는 수명·관찰 경계다. mutable Container는 외부에 노출하지 않는다.
- FInventoryContainer 안에 delegate를 넣지 않는다. 정렬·리사이즈의 값 대입으로 구독이 사라지는 문제를 피한다.
- ChangeSet은 Container별 Added/Removed/Updated InstanceId와 grid/순서 변경을 담는다. Load 성공은 전체 reset이다. 실패·실질적 no-op은 부분 변경 이벤트를 내지 않는다. callback 중 재진입 mutation은 OperationInProgress로 거부한다.
- CanMove와 TryMove는 같은 검증을 사용한다. 마우스의 임의 셀 좌표 입력을 위해 기존 footprint 경계 덧셈을 int64로 검사한다. 기존 NoSpace/Occupied/ItemNotFound 의미와 enum 번호는 유지한다.
- DuckovLike의 UContainerViewModel은 Model을 구독하고 Item VM 목록을 유지한다. 같은 Container 안에서 같은 ID의 VM은 재사용하며 제거된 Item VM은 무효화한다. UItemViewModel은 Model+ContainerId+InstanceId로 매번 조회하며 Item 포인터·위치·수량 사본을 장기 보관하지 않는다.
- VM은 UMVVMViewModelBase와 FieldNotify를 사용한다. 변경된 항목과 목록·grid만 통지하며 상시 Tick·binding 함수 polling은 필요하지 않다. getter는 읽기 전용이다.
- Container/Interaction VM의 바인딩은 Model을 GC 참조로 보유한다. 화면 소유자는 닫힘·레벨 전환에서 Interaction.Cancel과 Bind(nullptr), Container.Bind(nullptr, NAME_None)를 호출해야 한다. 재바인딩·BeginDestroy는 이전 delegate를 해제한다. Item VM은 약한 Model 참조만 가진다.
- FieldNotify 도중 발생한 재바인딩·닫힘·취소는 현재 통지를 마친 뒤 적용한다. 목록 순회 중 무효화와 닫힘 요청 유실을 막는다. 명령 성공 후 preview가 무효해지면 preview 실패 문구를 갱신하되 명령 반환값은 실제 명령 결과를 유지한다.
- UInteractionViewModel은 드래그 ID·대상·셀·프리뷰 회전이라는 임시 상호작용 상태만 소유한다. 시작·프리뷰·회전·취소는 Model을 변경하지 않는다. Drop은 최신 상태로 재검증하고 성공 때만 이동한다. 실패 Drop은 이유를 표시하고 drag를 유지한다. 외부 변경은 프리뷰를 재검증하며 원본 제거·전체 reset·해제는 취소한다.
- View는 픽셀→셀 좌표 변환과 화면 표현을 맡고 BlueprintCallable 명령을 호출한다. 스택·정렬·리사이즈 명령도 같은 Model 경로를 사용한다. 실패 메시지는 NSLOCTEXT로 제공한다.
- Model 테스트는 InventoryCoreTests에 유지한다. ViewModel 테스트는 DuckovLike 모듈의 WITH_DEV_AUTOMATION_TESTS 안에 두어 Model 테스트 모듈에 UI 의존을 넣지 않는다. 테스트만을 위한 새 모듈·build 설정은 만들지 않는다.

## Alternatives / Trade-offs

VM에 FItemInstance 사본을 저장하면 getter는 빠르지만 상태 복제와 동기화 책임이 늘어난다. ID 조회 방식은 조회 비용을 내는 대신 원본 소유권을 명확히 한다.
Container struct의 delegate 대신 별도 Model owner를 사용하여 값 복사와 observer 수명을 분리했다.
전체 화면을 매번 재생성하는 방식 대신 변경 ID와 기존 Item VM을 재사용한다. 픽셀 렌더링·위젯 pool 최적화는 실제 화면 측정 전 도입하지 않는다.

## Consequences / Accepted Costs

Model 명령은 대상 Container의 이전 상태를 임시 복사하고 ID map으로 diff한다. 항목 수 N·셀 수 G에서 평균 O(N+G)의 추가 시간·메모리이며 정적 연산 자체의 비용은 별도다. 이동 query는 ID 탐색과 footprint 셀 검사이며 전체 상태 복사를 하지 않는다.
Item getter의 ID 조회는 O(N)이고 Definition footprint 조회에서 동기 로드가 가능하다. 이벤트 갱신 경로이며 Tick에 두지 않는다. 100개 아이템 성능 목표는 Editor 화면 구현 뒤 실측해야 한다.
FieldNotify는 데이터 갱신 신호이지 CommonUI focus/input routing 설정을 대체하지 않는다.
공식 MVVM API와 로컬 UE 5.7 MVVMViewModelBase.h를 확인했다: https://dev.epicgames.com/documentation/en-us/unreal-engine/umg-viewmodel-for-unreal-engine?application_version=5.7

## Revisit Conditions

실제 UI의 getter 비용, 다수 아이템의 변경 통지 비용, stash/raid 게임 정책, async Definition loading, slot persistence 통합이 필요하면 재검토한다.

## Verification

- 최신 UE 5.7 build Result: Succeeded. 전체 Duckov Automation 51개 성공, 테스트 경고·실패·미실행 0개 (2026.09.20-04.47.41 UTC). Saved/Automation/M2ViewModelReviewed/index.json과 Saved/Logs/M2ViewModel-reviewed.log를 확인했다.
- 기존 40개 + Model Query/ChangeSet/재진입/정렬·리사이즈·병합·reset 3개 + ViewModel 8개다.
- ViewModel 검증은 FieldNotify·ID VM 재사용·재바인딩·제거·drop·stale preview·취소·GC 수명을 포함한다.
- 독립 검토 지적: Interaction 통지 중 닫힘·취소 요청 유실, 명령 후 무효 preview의 오류 문구 불일치. 수정과 regression 추가·전체 재검증 후 별도 code-reviewer가 실제 코드와 최신 51/51 보고서를 재검토하여 PASS/APPROVE로 판정했다. 최종 리뷰 완료, 미해결 이슈 없음.
- 코드 기반의 계획·구현·자동 검증·독립 검토 완료. 사람이 실제 화면을 수용한 기록은 없으며 Editor 작업 직전 중단한다.
- Widget Blueprint, CommonUI 활성화·input/focus, DataTable asset, 실제 드래그 화면·툴팁·PIE와 성능 측정은 not_run. Editor 작업 전 의도적으로 중단한다.
