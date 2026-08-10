# Codex Builder 발췌

`CLAUDE.md`가 원본이고 이 파일은 Codex Builder용 발췌다. 충돌하면 `CLAUDE.md`가 우선한다.

## 프로젝트

싱글플레이 탑다운 익스트랙션 슈터. **포트폴리오 프로젝트이며 상용 게임 완성이 목적이 아니다.**

핵심 명제: **그리드 인벤토리 시스템의 구조를 직접 설계하고, 설계대로 동작함을 검증 가능한 형태
(Automation Test)로 완성한다.**

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
| `docs` | 문서, ADR |
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

## 구현 중 Architecture Constraint

승인된 ADR은 참고 문서가 아니라 **Architecture Constraint**다. 구현 중 ADR과 충돌하는 변경이 필요해 보여도
**임의로 바꾸지 않는다.** 먼저 아래를 보고하고 구현을 멈춘다.

1. 충돌하는 ADR 또는 공개 계약
2. 필요한 변경
3. 변경이 필요한 이유
4. ADR을 유지하는 대안과 그 비용

**사용자 승인 없이 `Public/` 헤더의 시그니처와 `UPROPERTY`·`UFUNCTION` specifier를 바꾸지 않는다.**
변경이 필요하면 먼저 말한다.

## 빌드와 문서

```
& "C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\Build.bat" DuckovLikeEditor Win64 Development "-Project=$PWD\DuckovLike.uproject" -WaitMutex -NoHotReload
```

- **PowerShell 기준이다.** cmd에서는 `-Project="%CD%\DuckovLike.uproject"`로 바꾼다.
- **경로를 절대 경로로 박지 않는다.** Builder는 별도 worktree에서 움직이므로, 경로를 박으면
  자기 변경이 아닌 primary tree를 빌드하고 `Result: Succeeded`를 보고하게 된다. 상대 경로도
  쓸 수 없다 — `Build.bat`이 CWD를 엔진의 `Engine/Source`로 바꾸므로 실패한다.
- `exit 0`을 성공으로 믿지 않는다. 출력의 `Result: Succeeded`를 확인한다.
- `-ExecCmds="Quit"`은 unattended 에디터를 종료시키지 못하므로 쓰지 않거나 반드시 정리한다.
- **`GDD.md`와 `INVENTORY_DESIGN.md`는 읽기 전용이다.** 문제를 발견하면 고치지 말고 보고한다.
- **현재 마일스톤 밖의 일을 하지 않는다.** 현재 M1a에서는 위젯과 저장 로직을 만들지 않는다.
