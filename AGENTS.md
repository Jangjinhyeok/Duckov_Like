# Codex 프로젝트 지침

`CLAUDE.md`가 원본이고 이 파일은 일반 Codex 세션과 명시적 Builder dispatch용 발췌다.
충돌하면 `CLAUDE.md`가 우선한다.

## 프로젝트

싱글플레이 탑다운 익스트랙션 슈터. **포트폴리오 프로젝트이며 상용 게임 완성이 목적이 아니다.**

핵심 명제: **CommonUI·UMG 기반 UI와 MVVM·인벤토리 Model의 연결을 동작·검증·구조 설명으로 보여준다.**

## 모듈 경계

| 모듈 | 책임 | 의존 |
| --- | --- | --- |
| `InventoryCore` | 순수 Model — 그리드, 배치 규칙, 연산 서비스, 저장 레코드 | `Core`, `CoreUObject`, `Engine` |
| `DuckovLike` | 게임 모듈 — ViewModel, 위젯, 액터, GAS | 위 + `InputCore`, `EnhancedInput`, `InventoryCore`, `UMG`, `ModelViewViewModel` |

- **`InventoryCore`의 `Build.cs`에 `UMG`/`Slate`/`SlateCore`/`ModelViewViewModel`을 절대 넣지 않는다.**
- 의존 방향은 `View -> ViewModel -> Model` 단방향. 역방향은 이벤트(ChangeSet)로만.
- `InventoryCore`는 `Public/`·`Private/` 분리. **다른 모듈에 노출할 타입만 `Public/`.**
  `DuckovLike`는 소비자가 없으므로 평면 구조.

## 커밋 규칙

- **논리 단위로 쪼갠다.** 한 커밋은 한 가지 일만 한다. "겸사겸사" 변경을 섞지 않는다.
- **각 커밋은 독립적으로 빌드되어야 한다.** 커밋 전에 실제로 빌드를 돌려 확인한다.
- **빌드가 깨진 상태로 커밋하지 않는다.**

```
<type>(<scope>): <제목: 한글, 50자 내외, 마침표 없음>

<본문: 한글. 무엇이 아니라 "왜". 버린 대안과 판단 근거를 남긴다.
 diff를 읽으면 아는 내용을 반복하지 않는다.>

검증: <검증 명령> -> <결과>

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
```

- **제목과 본문은 한글로 쓴다.** `type`/`scope`와 `Co-Authored-By` trailer는 영어 그대로 둔다.
- 제목은 "~ 추가 / ~ 수정 / ~ 제거"처럼 끝낸다. `scope`는 애매하면 생략한다.
- `검증:` 줄은 빌드·테스트를 돌린 커밋에 넣는다. 돌리지 않았으면 넣지 않는다 — 거짓 신호 금지.

| type | 용도 |
| --- | --- |
| `feat` | 새 기능 |
| `fix` | 버그 수정 |
| `refactor` | 동작이 바뀌지 않는 구조 변경 |
| `test` | Automation Test 추가·수정 |
| `build` | 모듈 구성, `Build.cs`, 플러그인, `.uproject`, 빌드 설정 |
| `docs` | 문서, 설계결정기록 |
| `perf` | 성능 개선 (측정 결과를 본문에 남긴다) |
| `chore` | 그 외 — 설정 파일, 정리 |

## 구현 규칙

- **한글로 쓰는 것**: 주석, 커밋 메시지, 문서, 로그 메시지 등 사람이 읽는 모든 텍스트.
  **영어로 쓰는 것**: 식별자(변수·함수·클래스·모듈·파일명)와 기술 용어.
- **최소 코드.** 추측에 기반한 유연성·확장성·에러 처리를 넣지 않는다.
- **실패는 명시적으로.** 인벤토리 연산은 bool이 아니라 실패 사유를 반환한다.
- **실패한 연산은 상태를 바꾸지 않는다.** 전체 검증 후 한 번에 커밋한다. 롤백 경로를 만들지 않는다.
- **Model이 진실이다.** 위치·회전·수량은 `InventoryCore`만 소유한다. View/ViewModel은 원본 상태를 갖지 않는다.
- **인벤토리 UI는 상시 Tick에 의존하지 않는다.** 이벤트 기반으로 갱신한다.

## 작업 흐름 — CLAUDE.md의 작업 분담을 따른다

- 일반 Codex 세션도 기능 단위로 목표·범위·완료 기준·risk tier를 짧게 밝힌 뒤 LOW 작업을 자율 실행한다.
  설계 권장안과 주요 trade-off, Public header·API·enum·UPROPERTY/UFUNCTION specifier,
  테스트 이름·내용, 구현·검증·필요한 문서 요약까지 AI가 맡는다.
  되돌릴 수 있는 구현 세부는 가정을 밝히고 진행하며, 이미 받은 권한이나 "진행할까요?"를 반복해서 묻지 않는다.
- 문서마다 역질문하거나 사용자 선답변·Option Sweep·설명 역전을 기본 gate로 요구하지 않는다.
- 기존 Accepted ADR·API/Blueprint 계약을 깨거나 저장 포맷·migration·데이터 삭제·보안·replication에
  영향을 주는 HIGH 변경은 영향·대안을 함께 제시하고 영향받는 구현 전에 사용자 확인을 받는다.
  요청 범위·마일스톤 확대와 주요 UX 변경도 먼저 확인한다.
- Public/ 파일이나 specifier라는 이유만으로 매번 승인받지 않는다. 실제 호환성·수명·소유권 영향으로 판단한다.
- 완료 시 동작 변화, 책임·흐름, 주요 선택과 대안 하나, 검증 결과·미검증 항목, 핵심 파일 최대 3개를 보고한다.
  기술적 완료와 사용자 수용·이해 상태는 별도로 기록한다. 학습용 질문은 요청받을 때만 한다.
- ADR는 중요한 구조 결정에만 작성하고 관련 결정을 묶는다. 위임 범위의 LOW 결정은
  판단 주체를 AI로 명시해 Accepted로 기록할 수 있다. 사람 승인이 필요한 결정은 승인 전까지 Proposed다.
  worklog는 기능 완료 시 해당 마일스톤 문서에 한 번 요약한다.
  과거 사용자 판단·발언·검증 기록을 보존하고, 하지 않은 승인을 만들어내지 않는다.
- 일반 작업에서는 HANDOFF/RESULT를 만들지 않는다. 명시적 Builder dispatch에서는 받은 HANDOFF를
  읽기 전용 명세로 따르며 protocol과 scope를 유지한다. 기존 파일이 있다는 이유로 Builder 모드에 진입하지 않는다.
- 현재 branch에서 작업하며 commit·push는 명시 권한이 있을 때만 한다.

## 빌드와 문서

```
& "C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\Build.bat" DuckovLikeEditor Win64 Development "-Project=$PWD\DuckovLike.uproject" -WaitMutex -NoHotReload
```

- **PowerShell 기준이다.** cmd에서는 `-Project="%CD%\DuckovLike.uproject"`로 바꾼다.
- **특정 checkout 경로를 하드코딩하지 않는다.** 실제 작업 repo의 CWD에서 `$PWD`로 절대 경로를 전개한다.
  상대 경로는 `Build.bat`이 CWD를 `Engine/Source`로 바꾸므로 쓰지 않는다.
  별도 worktree 생성이나 branch 전환을 기본 동작으로 가정하지 않는다.
- `exit 0`을 성공으로 믿지 않는다. 출력의 `Result: Succeeded`를 확인한다.
- `-ExecCmds="Quit"`은 unattended 에디터를 종료시키지 못하므로 쓰지 않거나 반드시 정리한다.
- **GDD·INVENTORY_DESIGN은 읽기 전용이다.** 문제를 발견하면 수정하지 않고 보고한다.
  과거 문서의 사용자 선답변·설명 역전 요구보다 CLAUDE.md의 현재 작업 분담이 우선한다.
- **현재 요청과 마일스톤 밖의 일을 하지 않는다.** 실제 Source와 최신 worklog로 현재 상태를 확인한다.
  CommonUI는 포트폴리오 목표이며 아직 활성화되지 않았다. 지침 변경만으로 UI 구현이나 다음 마일스톤을 시작하지 않는다.
