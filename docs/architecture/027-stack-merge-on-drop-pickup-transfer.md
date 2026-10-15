# 설계결정기록 027 — 드롭·줍기·전부 보관의 같은 종류 스택 병합

| 항목 | 내용 |
| --- | --- |
| 상태 | Accepted |
| 날짜 | 2026-10-15 |
| 작업 범위 | M3 / 인벤토리 스택 병합(사용자 보고 "메디킷이 겹쳐져야 되는데") |
| Risk | LOW — 저장 형식·그리드 규칙 변경 없음. InventoryCore Public은 조회 함수 추가뿐. 기존 Model 명령(`TryAdd`·`TryTransferAll`)의 결과가 바뀌는 계약 변경이며, Accepted 017과 충돌하는 부분은 사용자 요청으로 결정됐다 |
| 판단 주체 | 사용자: 줍기·전부 보관은 "원작처럼 기존 스택을 먼저 채우고 남은 양만 새 칸에". 드롭 병합 판정 규칙·채우는 순서·원자성 유지는 AI(위임 범위) |
| 사용자 검토 | 2026-10-15 사용자 보고 "pie 확인 다했고"로 PIE 확인. AI의 사람 조작 직접 관찰과는 구분 |
| 관련 | 스택 병합 연산 계약(012)의 "이동/병합 판정은 호출자 책임"을 구현, 월드 loot 획득(017)의 "자동 분할·merge를 하지 않는다"를 대체, 인벤토리 화면(026)의 "전부 창고에 보관"과 최소 Raid 탈출(019)의 가방 → 창고 이전에 적용 |

## Context / Decision Question

Model에는 `TryStack`(012)이 있었지만 화면 드롭은 항상 `TryMove`만 불러, 같은 종류 위에 놓으면 Occupied로 실패했다.
줍기(`TryAdd`)는 017에 따라 항상 새 항목을 만들고, 전부 보관(`TryTransferAll`)도 각 항목을 새 칸에 배치했다.
원작은 같은 종류를 겹쳐 놓으면 합쳐지고, 주운 아이템도 기존 스택에 들어간다.

## Decision / Rationale

- **드롭**: `UInteractionViewModel::Revalidate`에서 `CanMove`가 Occupied이고, 놓을 footprint가 옮기는 항목이 아닌
  다른 항목 **하나와만** 겹치며 `CanStack`이 성공이면 그 항목을 병합 대상으로 잡는다. `Drop`은 대상이 있으면 `TryStack`, 없으면 `TryMove`다.
  가득 찼으면 StackFull을 보이고, 다른 종류면 기존 Occupied를 유지한다. 판정은 ViewModel 한 곳에만 있다(012의 재검토 조건을 넘지 않음).
  InventoryCore에는 판정 재료만 조회로 추가했다(`FInventoryOperations::CanStack`·`FindSingleOverlap`과 Model 래퍼). `TryStack`은 `CanStack` 검증을 먼저 부른다.
- **줍기·전부 보관**: 같은 정의(행 이름과 DataTable이 모두 같음)의 스택 가능한 기존 스택을 **위쪽 행, 왼쪽 칸** 순으로 채우고,
  남은 수량만 기존 row-major first-fit으로 새 칸에 둔다. 전부 보관에서는 앞에서 옮긴 항목도 뒤 항목의 채울 대상이 된다.
- **원자성 유지**: 둘 다 복사본(Planned)에서 계획하고, 남은 수량을 둘 칸이 없으면 채운 스택까지 포함해 아무것도 바꾸지 않는다.
  기존 스택이 다 받으면 새 ID를 발급하지 않고 Updated만 알린다. 그래서 가방이 가득 차도 같은 종류 스택에 자리가 있으면 주울 수 있다.
- 탈출도 `TryTransferAll`을 쓰므로 탈출 때 가방 → 창고 이전에도 같은 규칙이 적용된다.

## Alternatives / Trade-offs

- **드롭 대상을 커서 아래 칸으로 판정**: 원작에 더 가깝다. BlueprintCallable `Preview(Target, Cell)` 시그니처와 호출부·테스트를 바꿔야 해서 footprint 겹침으로 정했다.
- **`TryMove`에 병합을 넣기**: 012에서 이미 버린 안이다. 이동과 병합의 실패 사유와 ChangeSet이 섞인다.
- **줍기에서 들어가는 만큼만 줍고 나머지는 월드에 남기기**: 원작에 더 가까울 수 있다. loot payload 수량을 바꾸는 새 경로가 필요하고,
  "실패한 연산은 상태를 바꾸지 않는다" 규칙과 충돌해 전부 아니면 전무를 유지했다.

## Consequences / Accepted Costs

- 전부 보관·탈출에서 완전히 합쳐진 원본 항목의 InstanceId는 사라진다(012의 완전 병합과 같은 결과).
- 두 항목에 걸쳐 놓으면 병합하지 않는다. footprint가 격자 밖으로 나가면 같은 종류 위라도 NoSpace다.
- 외부 Model 변경만으로 병합/이동 판정이 바뀌면 프리뷰 글자 갱신은 기존 실패 표시와 같은 경로라 늦을 수 있다.
- 기존 테스트 중 "같은 종류 위 = Occupied", "줍기는 항상 새 ID"를 전제로 한 6개를 목적은 두고 실패 원인이나 기대 수량만 바꿨다(worklog에 목록).

## Revisit Conditions

- 원작처럼 들어가는 만큼만 줍는 동작이 필요할 때.
- 드롭 대상을 커서 기준으로 바꿔야 할 때(두 항목에 걸친 드롭의 병합 등).

## Verification

2026-10-15 실행. 증거는 `Saved/Automation/StackDrop/`, `Saved/Automation/FillStacks/`.

- 드롭 병합: Editor·Game·Editor `-DisableAdaptiveUnity` 빌드 `Result: Succeeded`, 전체 Automation 105 succeeded + 1 warning / 0 failed(106개).
- 줍기·전부 보관: 같은 세 빌드 `Result: Succeeded`, 전체 Automation **109 succeeded + 2 succeededWithWarnings / 0 failed / 0 notRun**(111개, `FillStacks/Final2`).
  경고는 이동 시뮬레이션의 프레임 지연 로그다. 첫 실행(`FillStacks/Final`)은 옛 줍기 계약을 전제로 한 게임 테스트 3개가 실패해 기대값을 바꿨다.
- 새 테스트: `Duckov.InventoryCore.Stack.TestStack_CanStack_MatchesTryStackWithoutChange`, `…FindSingleOverlap_ReturnsOnlyLoneOccupant`,
  `Duckov.ViewModel.DropOntoSameKindStacks`, `Duckov.InventoryCore.Model.Add.FillsExistingStacksFirst`·`FillOnFullGrid`,
  `Duckov.InventoryCore.Model.TransferAll.FillsTargetStacksFirst`·`FillThenNoSpaceIsAtomic`·`MergesSourceStacksTogether`.
  `Duckov.Gameplay.WorldLootPickup`에 실제 loot Actor의 병합 획득(새 ID 없음, Updated, loot 제거) 검사를 더했다.
- 독립 검토: 두 변경 각각 별도 문맥의 cpp-reviewer PASS. 줍기·전부 보관 쪽 LOW 1건(채우는 순서 미검증)을 테스트 수정으로 반영했고 InventoryCore 69개 재실행 실패 0. 반영분 재검토는 not_run.
- 미검증: 사람의 직접 드래그·줍기 조작, 게임패드.
