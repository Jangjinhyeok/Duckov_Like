# CLAUDE.md — Duckov_Like

이 문서는 **이 프로젝트 고유의 규칙**만 담는다. 응답 스타일·작업 원칙 같은 메타 규칙은
user-level `~/.claude/CLAUDE.md`에 있고 자동으로 함께 로드된다. 여기에 반복하지 않는다.

> **user-level 규칙을 덮는 항목**: user-level CLAUDE.md는 "코드 주석과 커밋 메시지는 영어를
> 기본으로 한다"고 정하지만, **이 프로젝트에서는 둘 다 한글로 쓴다**(3절·4절).
> 충돌 시 프로젝트 규칙이 우선한다.

---

## 1. 프로젝트

싱글플레이 탑다운 익스트랙션 슈터. **포트폴리오 프로젝트이며 상용 게임 완성이 목적이 아니다.**

핵심 명제: **그리드 인벤토리 시스템의 구조를 직접 설계하고, 설계대로 동작함을 검증 가능한 형태
(Automation Test)로 완성한다.**

- 엔진: **UE 5.7** (`C:\Program Files\Epic Games\UE_5.7`) · 플랫폼: Windows(Desktop)
- 레퍼런스: Escape From Duckov (Team Soda) — 설계 분석 대상이지 클론 대상이 아니다
- 기술 증명 축 우선순위: **①그리드 인벤토리 ②MVVM ③GAS**. 리소스가 부족하면 ③부터 줄인다

판단이 갈릴 때의 기준: **인벤토리의 구조적 완성도나 검증 가능성을 높이지 않으면 v1에서 뺀다.**

## 2. 모듈 경계 — 이 프로젝트에서 가장 중요한 규칙

| 모듈 | 책임 | 의존 |
| --- | --- | --- |
| `InventoryCore` | 순수 Model — 그리드, 배치 규칙, 연산 서비스, 저장 레코드 | `Core`, `CoreUObject`, `Engine` |
| `DuckovLike` | 게임 모듈 — ViewModel, 위젯, 액터, GAS | 위 + `InputCore`, `EnhancedInput`, `InventoryCore`, `UMG`, `ModelViewViewModel` |

- **`InventoryCore`의 `Build.cs`에 `UMG`/`Slate`/`SlateCore`/`ModelViewViewModel`을 절대 넣지 않는다.**
  이걸 넣는 순간 이 프로젝트의 핵심 논증이 무너진다. 필요해 보이면 설계가 잘못된 것이다.
- 의존 방향은 `View -> ViewModel -> Model` 단방향. 역방향은 이벤트(ChangeSet)로만.
- `InventoryCore`는 `Public/`·`Private/` 분리. **다른 모듈에 노출할 타입만 `Public/`.**
  `DuckovLike`는 소비자가 없으므로 평면 구조.

경계가 실제로 작동하는 지점(M0.5 실측): UMG 헤더 `#include`와 타입 선언은 **통과한다**
(에디터 타깃의 `SharedPCH.UnrealEd`가 이미 담고 있다). `UUserWidget::StaticClass()`처럼
**심볼을 참조하면 `LNK2019`로 링크가 깨진다.** 즉시성은 없지만 우회는 불가능하다.

## 3. 커밋 규칙

- **논리 단위로 쪼갠다.** 한 커밋은 한 가지 일만 한다. "겸사겸사" 변경을 섞지 않는다.
- **각 커밋은 독립적으로 빌드되어야 한다.** 커밋 전에 실제로 빌드를 돌려 확인한다.
- **빌드가 깨진 상태로 커밋하지 않는다.**

형식:

```
<type>(<scope>): <제목: 한글, 50자 내외, 마침표 없음>

<본문: 한글. 무엇이 아니라 "왜". 버린 대안과 판단 근거를 남긴다.
 diff를 읽으면 아는 내용을 반복하지 않는다.>

검증: <검증 명령> -> <결과>

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
```

- **제목과 본문은 한글로 쓴다.** `type`/`scope`와 `Co-Authored-By` trailer는 영어 그대로 둔다.
- 기술 용어는 번역하지 않는다 — "그리드 인벤토리의 widget hierarchy 최적화" 같은 형태.
- 제목은 "~ 추가 / ~ 수정 / ~ 제거"처럼 끝낸다. 영어의 명령형을 억지로 옮기지 않는다.

**type** (필수):

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

**scope** (선택): 모듈이나 영역. `inventory`, `ui`, `gas`, `lfs` 등.
애매하면 생략한다 — 억지로 붙이지 않는다.

예시:

```
build: InventoryCore 모듈 추가 — UI 비의존 경계를 빌드 시스템으로 강제
feat(inventory): row-major first-fit 퀵 이동 구현
test(inventory): 컨테이너 간 이동의 실패 원자성 테스트 추가
docs: 모듈 경계 결정을 ADR-000에 기록
```

- `검증:` 줄은 빌드·테스트를 돌린 커밋에 넣는다. 돌리지 않았으면 넣지 않는다 — 거짓 신호 금지.
- **push는 사용자가 결정한다.** 지시 없이 push하지 않는다.
- 기본 브랜치는 `main`. 스캐폴딩·단일 마일스톤 작업은 `main`에 직접 커밋해도 된다.

## 4. 코딩 규칙

UE 표준 코딩 규약을 따른다. 아래는 이 프로젝트에서 추가로 지킬 것.

- **한글로 쓰는 것**: 주석, 커밋 메시지, 문서, 로그 메시지 등 사람이 읽는 모든 텍스트.
  **영어로 쓰는 것**: 식별자(변수·함수·클래스·모듈·파일명)와 기술 용어.
  → "`InventoryContainer`의 점유 cache를 재생성한다" 처럼 섞어 쓴다. 기술 용어를 번역하지 않는다.
- **최소 코드.** 추측에 기반한 유연성·확장성·에러 처리를 넣지 않는다.
  빈 클래스 스텁을 미리 만들어 두지 않는다.
- **실패는 명시적으로.** 인벤토리 연산은 bool이 아니라 실패 사유를 반환한다
  (`NoSpace`, `Occupied`, `InvalidCategory`, `StackFull` …). UI가 이유를 표시할 수 있어야 한다.
- **실패한 연산은 상태를 바꾸지 않는다.** 전체 검증 후 한 번에 커밋한다. 롤백 경로를 만들지 않는다.
- **Model이 진실이다.** 위치·회전·수량은 `InventoryCore`만 소유한다.
  View/ViewModel은 원본 상태를 갖지 않는다.
- **인벤토리 UI는 상시 Tick에 의존하지 않는다.** 이벤트 기반으로 갱신한다.
- 네이밍 세부는 `docs/CONVENTIONS.md`.

## 5. 빌드와 검증

```
"C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\Build.bat" DuckovLikeEditor Win64 Development -Project="C:\Users\zero9\Documents\Github\Duckov_Like\DuckovLike.uproject" -WaitMutex -NoHotReload
```

- 첫 빌드는 2분 이상 걸린다. 증분은 10초 내외.
- **`exit 0`을 성공으로 믿지 않는다.** 출력의 `Result: Succeeded`를 확인한다.
- headless 에디터 부팅으로 모듈 로드를 확인할 수 있다. 단 `-ExecCmds="Quit"`은
  **unattended 에디터를 종료시키지 못한다** — 프로세스가 무한히 남으니 쓰지 않거나 반드시 정리한다.
- Model 규칙은 Automation Test로 검증한다. **v1 완료 기준은 30개 이상 통과.**

## 6. 문서

| 문서 | 성격 |
| --- | --- |
| `docs/GDD.md` | 목표·범위·마일스톤·검증 계획 |
| `docs/INVENTORY_DESIGN.md` | 불변식(INV-01~08), 제안 아키텍처(A1~E3), 연산 계약, MVVM 흐름 |
| `docs/CONVENTIONS.md` | 네이밍, Content 폴더 규약 |
| `docs/architecture/ADR-*.md` | 구조 결정 기록 |
| `docs/worklog/M*.md` | 마일스톤별 작업 기록 — 무엇을 했고 AI를 어떻게 썼는가 |

- **`GDD.md`와 `INVENTORY_DESIGN.md`는 읽기 전용이다.** 문제를 발견하면 고치지 말고 보고한다.
- 문서를 인용할 때 **번호가 아니라 제목으로** 가리킨다 (`§6` ❌ → "GDD의 기술 방침" ✅).
- 이미지 링크는 깨져 있다. **의도된 상태다** — 이미지 자산은 별도로 처리한다.
- **새 시스템·모듈 경계·데이터 흐름·패턴 선택이 걸린 결정은 ADR로 남긴다.**
  세션은 휘발되지만 ADR은 누적된다.
- `INVENTORY_DESIGN.md`의 A1~E3와 ADR-001~008은 **전부 `Proposed`다.** 임의로 확정하지 않는다.

### 작업 기록 — 각 작업이 끝나면 문서로 남긴다

**작업 하나가 완료될 때마다(보통 커밋 단위) `docs/worklog/M<마일스톤>-<이름>.md`에 추가 기록한다.**
마일스톤당 문서 하나이며, 진행 중에는 계속 덧붙이고 마일스톤이 끝나면 `상태`를 `완료`로 바꿔 닫는다.

담을 것:

| 절 | 내용 |
| --- | --- |
| 목표 | 이 마일스톤이 무엇을 성립시키는가 |
| 작업 내역 | 커밋 해시 + 한 일 + **검증 명령과 실제 결과** |
| AI 활용 | **내가 결정한 것 / AI가 수행한 것 / 내가 수정한 것** |
| 막혔던 것 | 실패한 시도와 원인, 우회 방법 |
| 다음으로 넘길 것 | 미결 사항, 발견한 문제 |

- **`AI 활용` 절이 이 문서의 핵심이다.** 산출물이 아니라 *판단의 출처*를 남긴다 —
  어떤 선택을 사람이 했고, AI가 무엇을 제안했으며, 무엇을 반려·수정했는지.
  포트폴리오에서 "AI로 만들었다"와 "AI를 부려 만들었다"를 가르는 것이 이 기록이다.
- **실패를 지우지 않는다.** 막혔던 경로와 그 원인은 성공한 경로만큼 기술적 근거가 된다.
- 검증 결과는 **실제 출력을 인용한다.** "빌드 성공"이 아니라 `Result: Succeeded (175초)`.
- 작업 기록은 사실의 누적이다. ADR과 역할이 다르다 —
  **ADR은 "왜 이 구조인가"(결정), 작업 기록은 "무엇을 어떻게 했는가"(경위).**

## 7. 마일스톤과 스코프

| 단계 | 내용 | 상태 |
| --- | --- | --- |
| M0 | 설계 (ADR 승인) | 문서 완료, ADR 미승인 |
| **M0.5** | **UE 프로젝트 스캐폴딩, 모듈 분리** | **진행 중** |
| M1 | Model 구현 — 배치·이동·스택·정렬·저장 | |
| M2 | ViewModel + UMG | |
| M3 | 게임 루프 — 루팅·장비·탈출·스태시, GAS | |
| M4 | 성능 점검, 문서·영상 정리 | |

**현재 마일스톤 밖의 일을 하지 않는다.** M1에서 위젯을 만들지 않고, M0.5에서 인벤토리 로직을
쓰지 않는다. 필요해 보이면 먼저 말한다.

의도적으로 제외된 것: **네트워크/Replication**, 베이스 건설, 펫/동료, 스킬 트리, 다수의 맵·적·무기.

## 8. 미결 사항

- **Automation Test 배치** — `InventoryCore` 모듈 내부(`WITH_DEV_AUTOMATION_TESTS`) vs
  별도 `InventoryCoreTests` 모듈. M1 시작 시 결정한다.
  별도 모듈로 가면 테스트가 Public 계약만 보게 되어 캡슐화가 강제된다.
- **ADR-001~008 승인** — M1 전에 `Proposed` → `Accepted`.
