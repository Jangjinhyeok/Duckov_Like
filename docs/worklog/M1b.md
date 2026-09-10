# M1b — 정렬·저장·리사이즈

기간: 2026-09-10 ~ · 상태: **진행 중 — 정렬 기술적 완료, 저장·리사이즈 미착수**

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
