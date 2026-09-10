# M1b — 정렬·저장·리사이즈

기간: 2026-09-10 ~ · 상태: **진행 중 — 정렬·리사이즈 기술적 완료, 저장 미착수**

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
