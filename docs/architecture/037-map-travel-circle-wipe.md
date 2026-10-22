# 설계결정기록 037 — 맵 이동 전환: 원형 전환(circle wipe)과 도착 클릭 대기

| 항목 | 내용 |
| --- | --- |
| 상태 | Accepted |
| 날짜 | 2026-10-19 |
| 작업 범위 | M3 / UI 디테일 — 게임 진입·레이드 출입 연출 |
| Risk | LOW — 로딩 화면 widget, 맵 이동 controller 두 곳, 세션의 일시 플래그(저장 안 함)가 바뀐다. 저장 포맷·네트워크·외부 API가 없다 |
| 판단 주체 | 목표와 범위는 사용자 지시(원작 GIF "인게임 진입 ui 연출"을 보고 차이 파악, "레이드 출입도 똑같아", "화면을 클릭하여 계속하기도 포함", Menu stack fade는 "그대로 두자"). 원작 수치는 AI가 GIF와 직접 캡처에서 측정. 구조·수치·테스트는 AI(위임 범위) |
| 사용자 검토 | 사용자 PIE 확인(2026-10-19, "PIE에서 맵 이동 연출 확인했어"). 사용자 보고이며 AI가 사람 조작을 직접 관찰한 결과는 아니다 |
| 관련 | [022 타이틀 흐름](022-title-flow-and-session-state.md)의 로딩 화면과 세션 상태, [025 HUD layer](025-hud-layer-and-anchored-prompt.md)의 Notice layer, [036 Menu stack 전환](036-menu-stack-transition.md)은 그대로 둔다 |

## Context / Decision Question

원작은 게임에 들어가고 나올 때 화면을 원형으로 닫고 연다(`docs/DUCKOV_UI_REFERENCE.md`의 "UI 애니메이션", 사용자 GIF 122프레임).

- 출발: 누르면 약 70ms 안에 화면 가운데 원(화면 높이의 약 50%) 밖이 검게 덮이고, 약 0.3초 머문 뒤 약 0.1초에 오므라든다. 이어서 검은 "로딩 중...".
- 도착: 로딩이 끝나면 "화면을 클릭하여 계속하기"와 깜박이는 원이 클릭을 기다린다. 클릭하면 플레이어가 있는 화면 가운데 원 안에서 게임이 먼저 보이고,
  약 0.25초 뒤 화면 밖까지 커진다. HUD도 원 안에서만 보인다.

이전에는 확인 즉시 검은 로딩 화면이 덮고, 맵이 뜨면 바로 게임이 보였다.

## Decision / Rationale

- **원형 전환은 Slate leaf `SCircleWipeMask` 하나가 그린다.** 화면을 다 덮는 큰 원 모양 rounded box를 채우지 않고 outline만 두껍게 그리면 outline 안쪽 경계가 원이 된다.
  draw element 하나, 텍스처·머티리얼 에셋 없이 끝난다. 반지름은 화면 대각선 절반에 대한 비율이라 화면 비율과 무관하게 1이면 다 열린다.
- **시간표는 `CircleWipe::Evaluate` 순수 함수**: 0.07초에 0.25까지(ease out) → 0.32초까지 정지 → 0.44초에 끝까지(ease in). 닫힘·열림이 같은 시간표를 반대로 쓴다.
- **`ULoadingScreenWidget`이 단계를 소유한다**: Departing → Loading(출발 쪽 맵), AwaitingClick → Revealing(도착 쪽 맵). Notice layer에 붙어 HUD·메뉴까지 덮는다.
  원이 움직이는 동안과 클릭 대기 원이 깜박이는 동안만 active timer가 돈다(상시 Tick 없음). 다 열리면 다음 frame에 스스로 화면에서 빠진다.
- **맵을 넘는 신호는 세션의 일시 플래그**: 이동을 시작한 controller가 `OpenLevel` 직전에 `SetArrivalPending`을 남기고,
  도착한 `ADuckovPlayerController::BeginPlay`가 한 번 꺼내 클릭 대기 화면을 띄운다. 맵에서 바로 시작한 PIE와 테스트에는 대기 화면이 없다.
- **다 덮인 뒤 한 tick 미뤄 이동한다**: 다 덮이는 순간은 Slate paint 도중이라 "로딩 중..."은 다음 frame에 그려진다.
  standalone에서는 다음 frame의 engine tick이 paint보다 먼저 맵을 내리므로, 바로 `OpenLevel`하면 문구가 한 번도 보이지 않는다. 타이틀·게임 controller 모두 다음 tick에 이동한다.
- **입력**: 출발·도착 대기 동안 이동 입력을 무시하고(`SetIgnoreMoveInput`), 로딩 화면이 있으면 상호작용·메뉴·조준 갱신을 막는다.
  클릭하면 조작과 HUD를 곧바로 돌려주고 원은 그 위에서 열린다. HUD는 원이 움직이는 동안 보이고, 게임을 다 덮은 단계에서만 숨긴다.
- **타이틀 출발은 확인한 화면을 원 안에 남긴다**: 커스터마이징의 확인은 원래 화면을 닫았다. 그대로면 Menu stack이 커스터마이징을 닫고 타이틀을 다시 띄우는 동안
  원 안에 타이틀 배경이 보인다. 타이틀 controller만 `SetKeepOpenOnApply(true)`로 열고, 맵을 떠날 때 기존 `EndPlay`가 닫는다.

## Alternatives / Trade-offs

- **UI 머티리얼(원형 마스크 파라미터)**: 가장자리 부드럽게 하기 등 표현 폭은 넓지만 에셋 생성·검증 경로가 필요하다. 원작 가장자리가 또렷해 Slate 그리기로 충분했다.
- **도착 화면을 이전 맵의 로딩 화면이 이어서 맡기**: `OpenLevel`은 이전 맵의 layout을 함께 내리므로 widget이 맵을 넘지 못한다. 세션 플래그 하나로 도착 쪽이 새로 띄운다.
- **"필드 준비 중..." 단계**: 원작은 클릭 뒤 실제 배치 작업 문구를 잠깐 보인다. 이 프로젝트에는 그 작업이 없어 넣지 않았다.
- **이름 iris**: 영화 편집 용어로는 iris wipe지만 UE의 Iris replication system과 헷갈려(사용자 지적, 2026-10-22) circle wipe로 이름 지었다.
- **키보드로 계속하기**: 원작 문구가 클릭이라 마우스 클릭만 받는다. 게임패드는 범위 밖이다.

## Consequences / Accepted Costs

| 흐름 | 바뀐 점 |
| --- | --- |
| 타이틀 커스터마이징 확인 → 베이스먼트 | 0.44초 원 닫힘(커스터마이징이 원 안에 남음) → "로딩 중..." → 맵 → 클릭 대기 → 0.44초 원 열림 |
| 출격·탈출(레이드 출입) | 같은 흐름. 원이 닫히는 동안 캐릭터는 멈추고 HUD는 원 안에 남는다 |
| 맵에서 바로 시작한 PIE | 대기 화면 없이 바로 조작 |

- 맵에 도착한 뒤 클릭하기 전까지 조작할 수 없다. 원작과 같은 의도된 대기다.
- 원이 닫히는 0.44초 동안 맵 이동이 늦어진다.
- 커스터마이징 확인 뒤 닫는 시점이 owner마다 다를 수 있게 됐다. 기본값은 기존처럼 확인 즉시 닫는다.

## Revisit Conditions

- 실제 로딩 시간이 길어져 "로딩 중..." 문구 외에 진행 표시가 필요할 때.
- 원 가장자리를 부드럽게 하거나 원 중심을 플레이어 화면 위치로 옮겨야 할 때(지금은 카메라가 플레이어를 가운데 두어 화면 가운데와 같다).
- 키보드·게임패드로 계속하기가 필요할 때.

## Verification

로그 파일 이름의 `Iris`는 2026-10-22 이름 변경 전에 붙인 이름이다. 당시 클래스·테스트 이름은 `SIrisMask`, `IrisTransition`, `Duckov.UI.LoadingIris.*`였다.

- `Duckov.UI.LoadingCircleWipe.Curve`: 시간표의 시작·정지·끝 값과 단조성.
- `Duckov.UI.LoadingCircleWipe.Mask`: `FWidgetRenderer`로 그린 pixel. 반지름 0.25에서 원 안은 투명, 원 밖·모서리는 불투명 검정, 0은 전부 덮고 1은 전부 비움.
  처음 구현은 이 검사에서 원 안이 흰색으로 채워져 실패했고(MakeBox의 tint가 채움 색), tint를 투명으로 바꿔 고쳤다. PIE 흐름 테스트는 그 상태에서도 통과했으므로 이 검사가 그리기 결과의 유일한 자동 검증이다.
- `Duckov.Raid.LoopFromBasement`: 맵에서 바로 시작하면 대기 화면 없음, 출격 시 Departing·HUD 유지·이동 무시, 레이드 도착 시 AwaitingClick·HUD 숨김·이동 무시,
  실제 Slate 마우스 클릭(hit test)으로 계속하기 → 조작·HUD 즉시 복귀와 Revealing, 원이 다 열리면 widget이 빠짐, 탈출 뒤 베이스먼트도 클릭 대기.
- `Duckov.Title.FlowToBasement`: 확인 뒤 Departing, 커스터마이징이 원 안에 남음(활성·preview 유지), 도착 대기 중 커스터마이징 열기 차단, 실제 클릭으로 계속하기.
- 독립 검토(code-reviewer): REQUEST CHANGES — 레이드 출입은 다 덮인 순간 바로 `OpenLevel`해 standalone에서 "로딩 중..."이 그려지지 않는다(엔진 tick 순서 추론).
  AI가 standalone 출격을 캡처해 재현(원 닫힘 → 문구 없는 검은 화면 → 클릭 대기)했고, 한 tick 미룬 뒤 같은 캡처에서 "로딩 중..."과 레이드 도착 원 열림을 확인했다.
  `Raid.LoopFromBasement`의 "맵 이동 전 로딩 문구 단계" 검사는 PIE에서 재현 빌드도 통과해(`IrisLoadingTextRepro.log`) 이 timing을 가려내지 못한다. 단계 상태 검사로만 둔다.
- Editor·Game 빌드 `Result: Succeeded`(`Saved/Automation/UiTransition/IrisBuild7.log`, `IrisGameBuild2.log`), 전체 Automation **133 succeeded / 0 failed**(`IrisRunFull2.log`, 이전 131 + 새 2).
- 화면(standalone 게임, AI가 약 15fps 연속 캡처로 확인): 타이틀 확인 → 커스터마이징이 원 안에 남은 채 오므라듦 → "로딩 중..." → 베이스먼트 클릭 대기(원 깜박임) → 캐릭터 중심 원이 열리며 HUD도 원 안에 보임.
  첫 캡처에서 원 안에 타이틀 배경이 보이는 문제를 발견해 `SetKeepOpenOnApply`를 더했다.
- 미검증: 레이드에서 탈출해 베이스먼트로 돌아오는 실제 화면(같은 경로, 자동 검사만), 사용자 PIE 체감.
- 남은 차이: 원이 닫히는 동안과 로딩 중에는 게임 십자선 커서가 그대로 보인다(원작은 화살표). 마우스를 움직이면 로딩 화면의 기본 커서로 바뀐다.
