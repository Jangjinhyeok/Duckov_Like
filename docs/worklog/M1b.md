# M1b — 정렬·저장·리사이즈

기간: 2026-09-10 ~ 2026-09-20 · 상태: **Model 범위 완료 — 정렬·리사이즈·저장 레코드 검증 완료, 저장 HIGH 결과 수용 확인. 디스크 저장은 후속 통합 범위**

## 2026-09-10 — 정렬

- 사용자 지시: 진행 상황 확인 후 “다음 진행하자”. AI가 다음 단계의 첫 기능을 정렬로 한정했다.
- Risk: LOW. 기존 연산 계약·저장 포맷·UI에 변경 없이 정적 Model 연산을 추가했다.
- 변경: `FInventoryOperations::TrySort`가 면적 내림차순·InstanceId 오름차순으로 현재 회전을 유지하며 row-major first-fit을 수행한다.
  임시 Container에서 `TryPlace`를 재사용하고, 전부 성공해야 원본을 교체한다. 실패는 `NoSpace`로 반환하며 원본의 모든 상태를 보존한다.
- AI 판단: 자동 회전·backtracking은 도입하지 않았다. 유효한 기존 배치라도 greedy 정렬은 실패할 수 있다.
  순서·회전·원자성·비용의 근거는 `docs/architecture/013-inventory-sort-policy.md`에 AI 위임 범위 Accepted로 기록했다.
- 테스트: 입력 배열 순서 독립성·반복 호출·정렬 좌표·cache·데이터 보존, 유효한 배치에서의 실패 원자성,
  기존 회전 유지, 빈 Container, Definition 부재를 검증하는 Automation Test 5개를 추가했다.

### 실제 검증

```powershell
& "C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\Build.bat" DuckovLikeEditor Win64 Development "-Project=$PWD\DuckovLike.uproject" -WaitMutex -NoHotReload
& "C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$PWD\DuckovLike.uproject" -unattended -nop4 -nosplash -NullRHI -ExecCmds="Automation RunTests Duckov.InventoryCore" -TestExit="Automation Test Queue Empty" "-ReportExportPath=$PWD\Saved\Automation\M1bSort" "-abslog=$PWD\Saved\Logs\M1bSort-verified.log"
git diff --check
```

- 최초 build는 sandbox 밖 UBT 로그 접근에서 `UnauthorizedAccessException`, `Result: Failed (OtherCompilationError)`로 실패했다.
  접근 승인 후 재실행한 build는 **`Result: Succeeded`**다.
- 최초 테스트 실행은 sandbox에서 Zen data path 접근 문제로 초기화가 진행되지 않아 중단했다. 테스트 성공으로 계산하지 않는다.
  승인 후 재실행한 UE Automation은 **29개 성공, 0개 실패, 0개 미실행**, 프로세스 exit code 0이다.
  기존 배치 8개·이동 7개·스택 9개에 정렬 5개를 더한 전체 회귀 결과다.
- 증거: `Saved/Automation/M1bSort/index.json`, `Saved/Logs/M1bSort-verified.log` (로컬 생성물).
- `git diff --check` 통과. 시작 branch `main`, 시작 staged/unstaged/untracked 변경 없음.
- Self-review: 정렬의 순서·회전·실패 원자성·cache와 테스트 및 문서 범위를 확인했다. 리뷰 완료, 이슈 없음.
  독립 reviewer는 사용하지 않았다. 변경은 정렬 구현·테스트·설계 기록·이 worklog로 한정했다.

### 남은 상태

- 정렬 구현·자동 검증은 완료. 사용자 결과 수용·이해는 미확인이다. commit/push는 수행하지 않았다.
- UI 연동·실제 화면 조작·성능 측정은 미실행이며 이번 Model 범위 밖이다.
- M1b의 저장·리사이즈는 미착수다. 저장 포맷과 카운터 복원 등 HIGH 계약은 영향받는 구현 전에 별도로 확인한다.
- M1a worklog의 설명 역전 대기는 과거 기록이다. 현재 CLAUDE.md에 따라 후속 기술 작업의 차단 조건으로 적용하지 않았다.

## 2026-09-10 — 커밋 전 재검증

- 사용자 지시: 진행 상황을 확인하고 현재 미커밋 내용을 커밋한다. 현재 `main`에서 정렬 구현·테스트·설계 기록·worklog 5개 파일을 하나의 기능 단위로 묶는다.
- 위 build 명령을 다시 실행했다. sandbox의 UBT 로그 접근 거부로 최초 실행은 실패했고, 확장 권한 재실행에서 `Result: Succeeded`를 확인했다.
- 위 전체 Model Automation 명령을 출력 경로 `Saved/Automation/M1bSortCommit`, 로그 `Saved/Logs/M1bSort-commit.log`로 재실행했다. 29개 성공, 경고·실패·미실행 0개, 프로세스 exit code 0이다.
- Self-review: 변경 5개 파일의 정렬 계약·실패 원자성·cache·회귀 테스트·문서 일치를 검토했다. 리뷰 완료, 이슈 없음. 독립 reviewer는 사용하지 않았다.
- 기존 구현 내용은 보존했다. UI 조작·성능 측정과 사용자 구조 이해 확인은 이번에도 수행하지 않았다. push는 요청 범위에 없다.

## 2026-09-10 — 리사이즈

- 사용자 지시: “다음 작업 진행하자”. AI가 같은 M1b 안에서 저장과 독립적인 리사이즈를 다음 기능으로 선택했다.
- 시작 상태: `main`, `e6b2c2d`, staged/unstaged/untracked 변경 없음. Risk LOW: 기존 API·enum 값·저장 계약을 보존하는 Model 연산 추가다.
- 변경: `FInventoryOperations::TryResize(Container, NewGridSize)`가 새 크기의 임시 Container에 기존 항목을 `TryPlace`로 검증·배치하고, 전부 성공해야 원본을 교체한다.
  ID·Definition·위치·회전·수량·Items 순서는 유지하며 GridSize와 OccupancyCache만 새 크기에 맞춘다.
- AI 판단: 자동 재배치 없이 기존 좌표가 새 경계에 들어가는 경우에만 축소를 허용한다. 자동 정렬을 함께 수행하는 대안은 책임과 결과 예측을 복잡하게 하므로 제외했다.
  0·음수 크기, int32 셀 수 범위 초과, Definition 해석 또는 배치 실패는 새 `ResizeOverflow`로 반환하고 원본 전체를 보존한다.
  기존 enum 값은 유지하도록 마지막에 추가했다. 같은 크기는 유효한 기존 Container에서 동일 상태로 성공한다.
- 책임·비용: 기존 정적 Operation Service와 배치 검증을 재사용한다. 별도 UObject·UI 의존·Tick은 없다.
  새 셀 수 G와 전체 항목 footprint 합 S, 항목 수 N에 대해 O(G + N + S), 임시 메모리 O(G + N)이다.
  Definition soft reference 동기 로드 비용은 기존 `TryPlace`를 따른다. 사용자 명령 연산이며 frame hot path에 넣지 않는다.
- 기존 구조를 확장한 기능이므로 별도 ADR는 만들지 않고 정책을 여기에 기록했다. 저장·장비 교체 UI는 구현하지 않았다.

### 실제 검증

```powershell
& "C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\Build.bat" DuckovLikeEditor Win64 Development "-Project=$PWD\DuckovLike.uproject" -WaitMutex -NoHotReload
& "C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$PWD\DuckovLike.uproject" -unattended -nop4 -nosplash -NullRHI -ExecCmds="Automation RunTests Duckov.InventoryCore" -TestExit="Automation Test Queue Empty" "-ReportExportPath=$PWD\Saved\Automation\M1bResize" "-abslog=$PWD\Saved\Logs\M1bResize-verified.log"
git diff --check
```

- 기존 세션에서 확인한 sandbox 밖 UBT·UE runtime 접근 필요에 따라 확장 권한으로 실행했다. build `Result: Succeeded`, exit code 0.
- UE Automation **35개 성공, 경고·실패·미실행 0개**, 프로세스 exit code 0. 기존 29개와 신규 리사이즈 6개다.
  신규 검증은 확대·cache 인덱스 재계산, 회전 항목의 경계에 맞춘 축소, 너비/높이 초과 시 부분 계획 폐기와 원본 보존,
  빈 Container·같은 크기, 잘못된 크기, Definition 부재를 포함한다.
- 증거: `Saved/Automation/M1bResize/index.json`, `Saved/Logs/M1bResize-verified.log` (로컬 생성물).
- Self-review: 구현·공개 계약·enum·신규 테스트·worklog 5개 파일과 기존 `TryPlace` 연결을 검토했다. 리뷰 완료, 이슈 없음.
  독립 reviewer는 사용하지 않았다. `git diff --check` 통과. 시작 baseline 대비 요청 밖 변경 없음.
- 리사이즈 기술적 완료. 사용자 결과 수용·이해는 미확인. UI 조작·성능 측정은 미실행이다.
  현재 변경은 미커밋이며 commit/push는 수행하지 않았다. M1b의 저장은 미착수이며 HIGH 계약 확인이 필요하다.

## 2026-09-20 — 저장 레코드와 원자적 복원

- 사용자 지시: “다음 예정된 작업 진행하자.”에 따라 다음 작업을 저장으로 확인하고, HIGH 계약과 Model ↔ 레코드 범위를 제시했다. 독립 계획 검토 PASS 후 사용자가 “진행해”로 계약과 로컬 구현을 승인했다. 구현 결과 수용이나 Git delivery 승인은 별개다.
- 시작 상태: main, 기존 변경은 앞선 계획 단계에서 작성한 미추적 ADR 014 하나뿐. 기존 Source 변경 없음.
- Risk HIGH: 저장 포맷과 전역 ID 복원 계약. ADR 014는 승인한 계약으로 Accepted 처리하되 결과 수용 대기를 명시했다.
- 변경: InventoryCore에 버전 1 저장 레코드, 이름 있는 Container 집합, FInventorySaveMapper::TrySave/TryLoad를 추가했다. ID·Definition soft path/RowName·수량·위치·회전·배열 순서를 보존하며 cache는 복원 시 재생성한다.
- 저장과 로드는 공통 검증을 거친다. 임시 전체 집합을 구성한 후에만 출력을 반영하고, 실패 시 기존 출력과 카운터를 보존한다. 카운터는 max(현재, 저장값)로 복원한다. MAX_int32에서 발급은 INDEX_NONE이며 overflow가 없다.
- 책임·흐름: 호출자가 이름을 붙인 세션 전체 Model 집합을 Mapper에 제공한다. Mapper는 Definition·ID·수량·배치를 확인한다. UI 역의존이나 새 모듈은 없다. 직접 Model 직렬화 대신 명시적 레코드를 선택해 내부 cache와 저장 계약을 분리했다.
- 비용: N개 항목·G개 셀·footprint 합 S에서 평균 O(N + G + S), 임시 메모리 O(N + G). 저장도 공통 검증을 위해 cache를 구성한다. Definition 동기 로드가 가능하므로 game thread 명령 경로이며 frame hot path가 아니다.
- allocator의 현재 호출부는 테스트뿐이다. 새 고갈 동작은 저장 테스트로 검증했고 향후 게임 생성 경로는 INDEX_NONE을 처리해야 한다.

### 실제 검증

```powershell
& "C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\Build.bat" DuckovLikeEditor Win64 Development "-Project=$PWD\DuckovLike.uproject" -WaitMutex -NoHotReload
& "C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$PWD\DuckovLike.uproject" -unattended -nop4 -nosplash -NullRHI -ExecCmds="Automation RunTests Duckov.InventoryCore" -TestExit="Automation Test Queue Empty" "-ReportExportPath=$PWD\Saved\Automation\M1bSave" "-abslog=$PWD\Saved\Logs\M1bSave-verified.log"
git diff --check
```

- UE는 sandbox 밖 UBT 로그·캐시 접근이 필요하여 확장 권한으로 실행했다. 최초 build 성공 후 테스트 수정의 최신 산출물을 보장하기 위해 증분 build를 수행했고 둘 다 Result: Succeeded, exit 0이다.
- Automation: 기존 35개 + 신규 저장 5개 = 40개 성공, 테스트 경고·실패·미실행 0개, exit 0. 보고 시각 2026.09.20-04.20.46 UTC.
- 신규 테스트는 다중 Container roundtrip·cache 재생성, 24개 잘못된 레코드 사례의 실패 원자성, 잘못된 Model 저장 시 출력 보존, 카운터 단조 증가·고갈, 빈 집합·빈 Container를 검증한다.
- 증거: Saved/Automation/M1bSave/index.json, Saved/Logs/M1bSave-verified.log. 에디터 시작 시 Compile 메뉴 section 관련 LogToolMenus 경고 1건은 있었으며 테스트 경고는 0개다.
- Self-review: 저장/복원 원자성, 기존 allocator 호출부, int64 경계 선검증, cache 제외, 모듈 경계와 변경 범위를 검토했다. 리뷰 완료, 이슈 없음.
- 독립 구현 검토: PASS. 별도 code-reviewer가 저장 Mapper·allocator·테스트·ADR·worklog 7개 파일, 기존 배치·연산 호출부와 실제 Automation 보고서를 확인했다. build 성공은 메인 실행 증거로 확인했다. 리뷰 완료, 이슈 없음. 계획 검토 PASS와 별도의 구현 검토다.
- 최종 scope: staged 변경 없음, 기존 파일 3개 수정·신규 파일 4개. tracked/untracked 공백 검사 통과, 변경 파일 비밀정보 표식 검사 0건. 전용 secret scanner는 repo에 없어 제한된 표식 검사만 수행했다.
- 미검증·범위 밖: 디스크 직렬화와 저장 슬롯, 실제 asset cold load, UI·수동 PIE·성능 측정. Definition 참조 테스트는 메모리에 생성한 UDataTable을 사용했다.
- 당시 사용자 결과 수용 대기였으며 commit/push는 수행하지 않았다.
- 후속 사용자 지시(2026-09-20): 결과 보고 후 “커밋, 푸쉬 진행하고 다음 구현도 진행”으로 저장 결과 수용·delivery 및 다음 마일스톤 코드 구현을 승인했다. Editor 작업 전 중단·전체 요약·시각화를 요청했다.
- 커밋 전 최신 build 재실행: Result: Succeeded, exit 0 (Target is up to date). 소스는 독립 검토·Automation 통과본과 동일하며 수용 상태 문서만 갱신했다. Git delivery 결과는 실행 후 응답으로 보고한다.
