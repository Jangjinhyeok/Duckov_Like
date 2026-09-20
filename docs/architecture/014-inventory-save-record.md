# 설계결정기록 014 — 저장 레코드와 원자적 복원

| 항목 | 내용 |
| --- | --- |
| 상태 | Accepted — 계약·구현 결과 수용 |
| 날짜 | 2026-09-20 |
| 작업 범위 | M1b / Model 저장 레코드와 변환 |
| Risk | HIGH — 저장 포맷과 전역 ID 카운터 복원 계약 |
| 판단 주체 | AI 제안, 사용자 승인 |
| 사용자 검토 | 2026-09-20 “진행해”로 구현 승인 후, 검증·독립 검토 결과 보고에 “커밋, 푸쉬 진행하고 다음 구현도 진행”으로 결과 수용·delivery 승인 |
| 관련 | 식별자 결정 009, 값 타입 결정 010 |

## Context / Decision Question

정렬과 리사이즈는 구현되어 있지만 저장 레코드는 없다. 전역 int32 카운터는 발급과 테스트 초기화만 지원한다.
Model의 상태를 보존하고 잘못된 레코드가 기존 상태나 카운터를 변경하지 않게 해야 한다.
기존 문서가 예고한 저장 ADR 007은 실제 파일이 없으므로 여기서 계약을 제안한다.

## Decision / Rationale

아래 계약은 2026-09-20 사용자가 로컬 구현을 승인했다. 구현 검증·결과 수용 상태는 Verification에 별도로 기록한다.

- InventoryCore에 명시적 USTRUCT 레코드와 정적 Mapper를 둔다. FormatVersion은 1이며 다른 버전은 거부한다.
- 최상위 레코드는 FormatVersion, NextInstanceId, Container 배열을 가진다. Container는 호출자가 제공한 고유하고 비어 있지 않은 FName ContainerId, GridSize, 아이템 배열을 가진다. 런타임 Container에 ID 소유권을 추가하지 않는다.
- 아이템은 InstanceId, DefinitionTable의 soft object path, DefinitionRowName, Quantity, AnchorCell, bRotated와 배열 순서를 보존한다. OccupancyCache는 저장하지 않고 복원 시 재생성한다.
- 저장·로드는 세션 전체 인벤토리 집합을 단위로 한다. 부분 병합은 지원하지 않는다. 호출자는 이전 집합과 복원 집합을 동시에 활성 상태로 유지하지 않아야 한다.
- 저장은 입력을 검증한 뒤 레코드를 출력한다. 로드는 모든 Container를 임시 상태로 검증·구성한 뒤 출력 전체와 카운터를 반영한다. 실패하면 기존 출력과 카운터를 보존한다. Definition 로드에 따른 엔진 asset cache 부수 효과는 원자성 대상이 아니다.
- ID는 0 이상이고 전체 집합에서 중복되지 않아야 한다. 저장 NextInstanceId는 음수가 아니고 모든 저장 ID보다 커야 한다. 위반은 자동 보정 없이 실패한다.
- 복원 카운터는 max(현재 카운터, 저장 NextInstanceId)다. 삭제된 최대 ID 뒤 번호도 저장하며, 같은 프로세스에서 이미 발급한 번호를 다시 쓰지 않는다.
- MAX_int32는 카운터 고갈 sentinel로 예약한다. 마지막 유효 ID는 MAX_int32 - 1이며 이후 발급은 INDEX_NONE을 반환하고 증가하지 않는다. 기존 int32 반환형을 유지하며 기존 호출부의 실패 처리도 확인한다.
- 실패 사유는 버전, Container 식별자·크기, ID 범위·중복, 카운터, Definition, 수량, 배치 오류를 구분한다.
- DefinitionTable·Row와 row struct를 확인하고 수량은 스택 가능 항목에서 1..MaxStack, 비 스택 항목에서 1이어야 한다. 크기·footprint·좌표 산술은 overflow 없이 검사하고 겹침·경계 초과를 거부한다. 기존 배치 helper는 안전한 입력임을 확인한 뒤 재사용한다.
- game thread 동기 연산이다. 이번 범위는 Model ↔ 레코드 변환이며 파일 I/O, USaveGame 슬롯, migration, UI, 새 모듈은 후속 통합 범위다.

## Alternatives / Trade-offs

런타임 Container 직접 저장은 짧지만 내부 구조와 저장 포맷이 결합된다. 명시적 레코드는 변환 코드 비용으로 cache 제외와 버전 검증을 분리한다.
개별 Container 복원은 전역 ID 검증 책임이 추가되므로 전체 집합 복원을 선택했다.
카운터를 저장값으로 되감으면 이미 발급한 번호를 재사용하므로 채택하지 않는다.

## Consequences / Accepted Costs

호출자가 이름 있는 전체 Model 집합을 Mapper에 전달한다. Mapper는 검증된 레코드 또는 새 Model 집합을 출력한다. UI 역의존은 없다.
Definition soft path와 RowName은 유지하되 asset 이동·이름 변경 migration은 제공하지 않는다.
전체 임시 상태 메모리와 Definition 동기 로드 비용이 발생하므로 frame hot path에 넣지 않는다.
항목 수 N, 전체 셀 수 G, footprint 합 S에 대해 검증·복원은 평균 O(N + G + S), 임시 메모리는 O(N + G)다. 저장도 같은 검증 경로를 사용하므로 같은 차수의 임시 cache 비용을 낸다. 별도 최적화보다 저장·로드의 검증 일치를 우선했다.
현재 allocator 호출부는 Automation Test뿐이며 게임 생성 경로는 아직 없다. 향후 생성 호출자는 INDEX_NONE을 명시적으로 처리해야 한다.
외부 파일 parser와 입력 메모리 제한은 파일 저장 통합 시 별도로 정한다.

## Revisit Conditions

디스크 저장, 부분 병합, Definition migration, 비동기 로드, 여러 세션 동시 유지가 필요하면 재검토한다.

## Verification

- 계획 및 사용자 계약·로컬 구현 승인 완료. 구현 결과 수용은 별도다.
- 독립 계획 검토: PASS (2026-09-20). 별도 reviewer가 이 제안, Accepted ADR 009·010, 당시 allocator·Container·Definition·배치 구현과 allocator 호출부를 검토했다. 리뷰 완료, 이슈 없음.
- 구현 완료. UE 5.7 DuckovLikeEditor Win64 Development build Result: Succeeded, 전체 Model Automation 40개 성공·테스트 경고/실패/미실행 0개 (2026-09-20). 근거: Saved/Automation/M1bSave/index.json, Saved/Logs/M1bSave-verified.log.
- 실제 파일 직렬화·디스크 roundtrip·UI·수동 PIE: not_run, 이번 범위 밖. 테스트의 Definition은 메모리에 생성한 UDataTable로 검증했으며 실제 asset의 cold load는 검증하지 않았다.
- 구현 후 UE 5.7 DuckovLikeEditor build의 Result: Succeeded와 전체 Duckov.InventoryCore Automation 결과를 확인한다.
- roundtrip: 여러 Container·빈 집합, 회전·수량·순서·Definition·ID·cache를 검증한다.
- 실패 원자성: 버전, 식별자, ID, 카운터, Definition, 수량, 크기·좌표 overflow, 겹침 오류를 집합 후반부에 주어 출력과 카운터 보존을 확인한다.
- 카운터: 삭제된 ID 뒤 번호 보존, 현재 카운터가 더 큰 경우, 복원 후 충돌 방지, 고갈 시 wraparound 방지를 검증한다.
- 독립 구현 검토: PASS (2026-09-20, 별도 code-reviewer). 저장 Mapper·allocator·신규 테스트·ADR·worklog와 기존 호출부, 실제 Automation 보고서를 검토했다. build 성공은 메인 세션의 실행 증거를 확인했으며 reviewer가 재실행한 것은 아니다. 리뷰 완료, 이슈 없음.
- 계획·구현·자동 검증·독립 검토 완료. 2026-09-20 결과 보고 후 사용자 commit·push 및 후속 구현 지시로 결과 수용을 확인했다. 구조 이해 확인은 별도로 수행하지 않았다.
