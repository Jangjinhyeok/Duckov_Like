# 설계결정기록 036 — Menu stack 화면 전환: CommonUI 내장 FadeOnly 전환

| 항목 | 내용 |
| --- | --- |
| 상태 | Accepted |
| 날짜 | 2026-10-19 |
| 작업 범위 | M3 / UI 디테일 — 화면 전환 애니메이션 |
| Risk | LOW — root layout의 Menu stack 설정 한 곳, 전환 중 재개방을 막는 데모·타이틀 controller 가드, 테스트 대기 조건이 바뀐다. 저장·네트워크·외부 API가 없고 메뉴 입력 흐름은 기존 경로(Menu stack이 비면 다음 tick에 Game 복구)를 그대로 쓴다 |
| 판단 주체 | 목표는 사용자 지시("ui 애니메이션을 추가 … 화면 전환 애니메이션"). 방식은 AI가 두 안(CommonUI 내장 / 원작형 비대칭)을 비교해 제시하고 사용자가 선택("연출자체가 목적이니까 A가 맞는거같아"). 시간·테스트 수정은 AI(위임 범위) |
| 사용자 검토 | 사용자 PIE 확인(2026-10-19) 뒤 맵 이동 연출 변경을 요청했고([037](037-map-travel-iris-transition.md)), 이 Menu stack fade는 "그대로 두자"로 유지. 사용자 보고이며 AI 직접 관찰 아님 |
| 관련 | [021 CommonUI layer](021-commonui-layer-stack.md)의 Decision 중 "전환 시간은 0으로 둔다" 한 항목을 대체한다. 나머지 021 결정(layer 구성, Menu stack이 비는 순간의 입력 복구)은 그대로다. AI가 방식 설명 때 이 결정을 뒤집는다고 알렸고 사용자가 A를 골랐다 |

## Context / Decision Question

Menu stack(`UCommonActivatableWidgetStack`)은 설계결정기록 021에 따라 `SetTransitionDuration(0.f)`로 즉시 전환했다.
"전환 중 입력 차단이 기존 열기·닫기 반응과 테스트 가정을 바꾸기 때문"이었다. 이번에는 화면 전환 연출 자체를 넣으므로 그 비용을 받아들인다.

원작 관찰(`docs/DUCKOV_UI_REFERENCE.md`의 "UI 애니메이션", 약 19fps 연속 캡처):

- 메인 메뉴 ↔ 설정: 약 0.15~0.2초 crossfade(두 화면이 겹쳐 보임).
- 인게임 Tab 인벤토리·Esc 일시정지: 열기는 즉시, 닫기는 약 0.15~0.2초 fade와 HUD 복귀가 동시에 진행.
- 메인 메뉴 → 로딩: 원형 iris.

엔진 코드(`SCommonAnimatedSwitcher`)를 읽은 결과 CommonUI 내장 전환은 crossfade가 아니라 순차 fade다.
이전 화면이 duration의 절반 동안 사라진 뒤 활성 화면이 바뀌고, 새 화면이 나머지 절반 동안 나타난다.
빈 자리(0번 slot)로 들어가거나 빈 자리에서 나올 때는 그 쪽 절반을 생략한다.

## Decision / Rationale

- **CommonUI 내장 전환을 켠다.** `UGameRootLayoutWidget`이 Menu stack에 `SetTransitionDuration(0.3f)`를 준다.
  전환 종류와 곡선은 엔진 기본값(`FadeOnly`, `Linear`)이다. 한 단계 0.15초가 원작 인게임 닫기 fade 길이와 같다.
- 사용자가 연출 자체를 목적으로 내장 기능 쪽을 골랐다. 구현이 설정 한 줄이고, 전환·입력 차단·중간 반전·활성화 시점을 엔진이 맡는다.
- 입력 복구 흐름은 바꾸지 않는다. 메뉴를 닫으면 사라지는 fade가 끝난 뒤 `OnDisplayedWidgetChanged(nullptr)`가 오고,
  기존 코드가 그때 HUD를 다시 보이고 다음 tick에 Game 입력을 복구한다.
- 전환 중 다시 열기는 막는다. 게임 controller는 기존 `IsMenuOpen()`으로 막고 있었다. 데모 controller는 Menu stack이 화면을 표시 중이면 열지 않고,
  타이틀 controller는 타이틀 화면이 활성일 때만 시작을 받는다(push 전환 중에는 타이틀이 이미 비활성인데 커스터마이징은 아직 활성 전이다).

## Alternatives / Trade-offs

- **원작형 비대칭(열기는 즉시, 닫기만 fade + HUD 동시 복귀)**: 인게임 체감은 원작과 같다.
  다만 stack이 비활성화된 화면을 곧바로 pool로 돌려보내므로, 각 화면이 닫기 요청을 받아 직접 fade한 뒤 `DeactivateWidget`을 불러야 한다.
  그러려면 Back·닫기 버튼·I 토글·커스터마이징 확인/취소를 한 경로로 모으고, "닫는 중" 상태와 중단 처리를 더해야 한다. 사용자가 A를 골라 채택하지 않았다.
- **진짜 crossfade(container 직접 구현)**: 메뉴 ↔ 메뉴까지 원작과 같아지지만 M2에서 수용된 입력 lifecycle을 다시 검증해야 해 범위가 크다.
- **전환 시간 0.2초**: 한 단계 0.1초라 인게임 열기·닫기에서 연출이 거의 보이지 않아 0.3초를 골랐다.

## Consequences / Accepted Costs

| 전환 | 동작 |
| --- | --- |
| 게임 → 메뉴(I, 레이드 커스터마이징) | 즉시 활성화(입력 config·focus 적용), 0.15초 fade-in |
| 메뉴 → 게임(닫기) | 0.15초 fade-out, 끝난 뒤 HUD 표시, 다음 tick에 Game 입력 복구 |
| 메뉴 → 메뉴(타이틀 → 커스터마이징, 취소로 복귀) | 이전 화면 0.15초 fade-out → 새 화면 활성화 → 0.15초 fade-in. 중간에 메뉴 뒤가 잠깐 비친다 |

- 전환 중에는 container의 input guard가 마우스를 막는다(최대 0.3초).
- 닫는 fade 동안에는 Menu stack이 아직 닫히는 화면을 표시 중이므로 `IsMenuOpen()`이 true다. 그 사이 I를 다시 눌러도 열리지 않고,
  캐릭터 이동·조준도 fade가 끝난 뒤 돌아온다. 원작은 닫기 fade와 HUD 복귀가 동시이고 열기가 즉시라는 점이 다르다.
- 메뉴 → 메뉴에서는 새 화면의 활성화(focus·입력 config)가 0.15초 늦다.
- 애니메이션은 엔진 switcher의 active timer로만 돈다. 상시 Tick 0 기준은 그대로다.
- 닫는 fade 동안 stack은 닫힌 화면을 아직 표시하므로, 그 사이 같은 클래스를 `AddWidget`하면 pool이 새 인스턴스를 만든다.
  게임 controller는 `IsMenuOpen()`으로 이미 막고 있었고, 데모 controller(`AInventoryDemoPlayerController`)에도 같은 가드를 더했다.
- 즉시 전환을 가정하던 테스트는 두 갈래로 바꿨다. 전환을 검증하는 `Duckov.UI.LayerStack`·`Duckov.Title.FlowToBasement`는 전환 중 상태와 완료를 확인한다.
  Model·ViewModel 흐름을 보며 같은 프레임에 닫고 다시 여는 PIE 테스트 5개는 `DisableMenuTransitionForTest`로 그 layout의 전환만 끈다.

## Revisit Conditions

- PIE에서 닫은 뒤 이동 복귀 지연이나 메뉴 → 메뉴의 중간 비침이 거슬린다는 피드백이 있을 때: 원작형 비대칭이나 crossfade를 다시 검토한다.
- 전환 곡선을 바꾸고 싶을 때: `TransitionCurveType`은 protected라 Menu stack subclass가 필요하다.
- 메인 메뉴 → 로딩의 iris처럼 Menu stack 밖(Notice layer) 전환을 넣을 때는 별도로 다룬다.

## Verification

- 완료 기준: 메뉴를 열고 닫을 때와 메뉴끼리 바뀔 때 fade가 보이고, 기존 입력 lifecycle(Menu 입력 적용, 닫은 뒤 Game 복구·viewport focus)이 유지된다.
- Editor·Game 빌드 `Result: Succeeded`(`Saved/Automation/UiTransition/Build2.log`, `GameBuild1.log`).
- 전환을 켠 직후 전체 Automation은 124 succeeded / 7 failed(`RunFull1.log`). 모두 즉시 전환을 가정한 검사였다.
  테스트 수정 뒤 7개 재실행 7 succeeded(`Targeted1.log`), 전체 Automation(실제 D3D12 RHI, `-nocefaccelpaint`) 131 succeeded / 0 failed(`RunFull2.log`).
- 독립 검토에서 타이틀 push 전환 중 시작 중복 결함이 나와 타이틀 가드를 고쳤다. 새 검사가 수정 전 코드에서 실패함을 확인했고(`TitleRepro.log`),
  수정 뒤 Editor·Game 빌드 `Result: Succeeded`(`Build4.log`, `GameBuild2.log`), 전체 Automation **131 succeeded / 0 failed**(`RunFull3.log`).
- 새 검사: 전환 시간 0.3초, 닫는 전환 동안 닫힌 화면 표시 유지, 그 사이 재개방 무시·새 인스턴스 없음, 전환 뒤 stack 비움과 Game 입력 복구,
  타이틀 → 커스터마이징 push에서 타이틀이 사라지는 동안 커스터마이징이 아직 비활성이고 전환 뒤 활성화, 그 사이 시작을 다시 눌러도 중복 push 없음, Back 전환 뒤 타이틀 복귀.
- 미검증: 실제 화면의 fade 모습과 체감(사용자 PIE 확인 대기), 레이드 맵 이동(`StartTravel`) 도중의 전환 모습.
- 첫 전체 실행에서 "닫으면 Menu stack 비어 있음"(인벤토리)이 기대와 달리 통과했다. 같은 상황을 반대로 검사한 새 검사는 이후 세 번 모두 통과했고,
  첫 실행의 통과 이유는 로그로 확정하지 못했다.
