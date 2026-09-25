# 설계결정기록 016 — Editor 인벤토리 화면의 소유와 입력 연결

| 항목 | 내용 |
| --- | --- |
| 상태 | Proposed — 로컬 구현 진행, 결과 수용 대기 |
| 날짜 | 2026-09-23 |
| 작업 범위 | M2 / 실제 데이터·화면 표시·열기와 닫기 |
| Risk | HIGH — CommonUI 활성화, 게임 모듈 의존과 viewport 입력 경로 변경 |
| 판단 주체 | AI 설계·구현, 사용자 결과 수용 별도 |
| 사용자 검토 | “에디터 작업 risk 검증해서 진행해보자”로 로컬 구현·검증 진행 요청. 결과 수용은 미확인 |
| 관련 | 이벤트 기반 Model과 ViewModel 연결(015), 모듈 경계(000) |

## Context / Decision Question

Model·ViewModel의 자동 검증 이후 실제 Editor 데이터와 화면을 연결한다. 첫 작업 단위는 두 컨테이너 표시, 정렬 갱신, 열기·닫기와 focus 복귀다. 드래그·회전·프리뷰 입력 연결은 다음 작업 단위이며 이 화면의 완성 범위로 주장하지 않는다.

## Decision / Rationale

- 전용 테스트 맵 `L_InventoryDemo`에서만 demo GameMode와 PlayerController를 사용한다. 프로젝트의 기본 게임 맵은 바꾸지 않는다.
- PlayerController가 세션 Model을 소유한다. 화면은 Container/Item/Interaction ViewModel을 연결하고 닫힐 때 취소·구독 해제한다. 화면을 다시 열어도 Model은 유지된다.
- CommonUI의 activation과 입력 설정을 사용한다. 실제 viewport 연결, 닫힘 후 입력·focus 복귀를 runtime에서 검증한다. 플러그인 활성화만으로 입력이 완성됐다고 판단하지 않는다.
- Widget Blueprint가 화면 구성을 소유하고 native widget이 기존 FieldNotify를 구독한다. 위치·회전·수량의 원본은 여전히 InventoryCore 하나이며 polling/Tick binding은 추가하지 않는다.
- 실제 DataTable·Widget Blueprint·map 패키지를 생성하고, 별도 프로세스에서 재로드·컴파일·참조를 검증한다. 기존 에셋을 자동으로 덮어쓰는 생성 경로는 두지 않는다.
- InventoryCore의 UI 비의존성과 기존 저장 형식은 유지한다. DataTable 크기의 Editor 편집 노출은 기존 필드의 표시 권한만 추가한다.

## Alternatives / Trade-offs

모든 화면을 C++에서 조립하면 파일만으로 재현하기 쉽지만 Editor에서 레이아웃을 확인·수정하는 목적에 맞지 않는다. 반대로 모든 구독과 수명 처리를 Blueprint graph에 두면 자동 검증이 어려워진다. 화면 구성은 WBP, 기존 VM 연결과 수명은 native 코드로 분리한다.

## Consequences / Accepted Costs

CommonUI 설정은 프로젝트 viewport에 적용되므로 전용 맵 밖의 기본 Editor 진입도 확인한다. demo 초기 데이터는 UI 검증용이며 게임 루프·슬롯 저장을 도입하지 않는다. 바이너리 에셋은 기존 Git LFS 규칙의 적용 대상이다.

## Revisit Conditions

드래그·회전 연결, 다른 화면과의 중첩, gamepad navigation, 실제 게임 controller 통합, 측정으로 확인한 item 표시 비용이 다음 검토 지점이다.

## Verification

- Editor/Game Win64 Development 최종 build 각각 `Result: Succeeded`, exit 0.
- 별도 프로세스에서 기존 51개와 에셋 Verify를 실행해 52개 성공, 테스트 경고·실패·미실행 0개(2026.09.23-11.10.31 UTC). 실제 패키지 로드, WBP 재컴파일과 BindWidget 타입, DataTable row, map GameMode를 확인했다.
- main이 실제 DataTable 편집 필드와 PIE 화면을 확인했다. 두 grid 표시·정렬 이동·닫기 버튼/ESC·I 재열기·정렬 상태 보존·Enter 기본 focus와 활성 화면의 PIE 종료를 검증했다. 작은 셀의 label 겹침은 수정·재생성·재검증했다.
- 생성기 초기 빈 WBP 컴파일 경고는 생성 실행에 남고, 저장된 최종 에셋 재로드/재컴파일에는 없다. 최초 잘못된 SaveMap 경로와 CommonUI 입력 복귀 순서도 수정했다. 실패 기록과 증거 경로는 M2 worklog에 보존한다.
- 독립 `code-reviewer`가 수정 코드와 최신 build/test/runtime 로그를 재검토해 APPROVE. 리뷰 완료, 해당 slice 미해결 이슈 없음. reviewer의 직접 visual 검증은 not_run이며 main의 GUI 검증과 구분한다.
- 기술 구현·검증과 HIGH 사람 수용은 별개다. 이 결정의 최종 사람 수용은 아직 확인하지 않아 Proposed를 유지한다. 드래그/회전·게임패드·패키징·성능 측정·다른 레벨로 travel은 이번 완료 범위 밖이다.
