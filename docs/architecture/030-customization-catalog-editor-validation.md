# 설계결정기록 030 — 카탈로그 등록 검증을 에디터 validator로, 규칙은 런타임과 공유

| 항목 | 내용 |
| --- | --- |
| 상태 | Accepted |
| 날짜 | 2026-10-16 |
| 작업 범위 | M3 / 커스터마이징 재설계 (3 에디터 등록 검증) |
| Risk | LOW — DuckovLike 안의 로컬 변경이고 저장 포맷·네트워크·외부 API 변경이 없다 |
| 판단 주체 | AI(위임 범위). 사용자는 목표와 범위(1~4)를 지시했고 구조 선택은 AI가 했다 |
| 사용자 검토 | 미검토. 사용자 직접 PIE 확인과 구조 검토는 대기 |
| 관련 | UE DataValidation 플러그인(UEditorValidatorBase), ADR 000(모듈 경계), ADR 029의 계약 검사 |

## Context / Decision Question

잘못 만든 파츠(다른 Skeleton, mesh/material 누락, Morph·mask·색상 parameter 누락, 슬롯 불일치, ID 문제)가 등록되면 런타임에서 사용자가 고르는 순간에야 실패한다.
저장·검증 시점에 잡고 싶고, 같은 규칙을 두 곳에 쓰지 않아야 한다.

## Decision / Rationale

- 규칙은 `CustomizationPartContract`(DuckovLike, 런타임 모듈)에 한 벌이다. `ValidateCustomizationPartContract`·`ValidateCustomizationBodyContract`를
  런타임 `TryApply`와 등록 검사가 함께 부르고 같은 `ECustomizationFailure`를 돌려준다. 렌더 자원·shader 준비처럼 로드 뒤 런타임 상태만 `TryApply`에 남겼다.
- `ValidateCustomizationTable`은 모든 row를 검사해 문제를 전부 모은다. 하나라도 있으면 통과가 아니다. row 규칙(None 예약어·ID 중복, 슬롯 값, static 대역 금지), 파츠 계약, body와 같은 Skeleton,
  기본 Profile이 가리키는 슬롯 파츠의 존재(슬롯 불일치·기본 파츠 삭제)를 본다.
- `UCustomizationCatalogValidator : UEditorValidatorBase`는 새 Editor 전용 모듈 `DuckovEditorTools`에 있다(`.uproject` Type=Editor, `DataValidation` 플러그인 Editor 한정).
  결과를 검증 UI로 옮길 뿐 규칙을 갖지 않는다. 저장 시 검증(`bValidateOnSave`)과 수동 검증에서 호출된다. Game target에는 모듈 자체가 없어 `WITH_EDITOR` 분기가 필요 없다.
- 모듈 이름은 `DuckovLike`로 시작하면 안 된다. UBT/UHT가 `Source/DuckovLike`를 접두사로 보고 생성 include 경로를 깨뜨렸다(`DuckovLikeEditor`로 시도했다가 변경).
  DuckovLike는 Public/Private 구분이 없는 평면 모듈이라 이 모듈이 모듈 루트를 include path로 직접 추가한다.

## Alternatives / Trade-offs

- **`UObject::IsDataValid` 오버라이드**: DataTable은 엔진 클래스라 불가능(자체 asset 클래스로 바꾸면 ADR 028과 어긋남).
- **DuckovLike 안의 `#if WITH_EDITOR` validator**: UHT가 조건부 UCLASS를 안정적으로 다루지 못하고 런타임 모듈에 에디터 코드가 섞인다. 모듈 분리를 택했다.
- 한계: DataTable은 RowName 중복을 구조적으로 허용하지 않아 validator 경로에서는 `DuplicatePartId`가 None 예약어 충돌로만 나타난다. 중복 규칙 자체는 row 목록 검사에서 직접 시험한다.
- 한계: 검증은 현재 로드된 mesh·material로 한다. 에디터에서 asset을 수정한 뒤 DataTable을 다시 저장하지 않으면 그 변화는 자동으로 재검증되지 않는다.

## Revisit Conditions

validator가 느려져 저장이 불편해질 때, 카탈로그 row가 아닌 다른 에셋(mesh 자체)도 등록 시점에 검증해야 할 때.

## Verification

`Duckov.Customization.Catalog.EditorValidatorRejectsBadRegistration`(11가지 잘못된 등록, 두 개 동시 불량·하나만 고쳐도 불통과·모두 고치면 통과, subsystem 등록 확인),
`RuntimeAndRegistrationShareRules`(런타임·계약 검사·등록 검사가 같은 enum과 슬롯), `RegistrationRules`.
