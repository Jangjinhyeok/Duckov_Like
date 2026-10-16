# 설계결정기록 029 — 슬롯 3개와 전체 슬롯 일괄 교체

| 항목 | 내용 |
| --- | --- |
| 상태 | Accepted |
| 날짜 | 2026-10-16 |
| 작업 범위 | M3 / 커스터마이징 재설계 (2 슬롯) |
| Risk | LOW — DuckovLike 안의 로컬 변경이고 저장 포맷·네트워크·외부 API 변경이 없다 |
| 판단 주체 | AI(위임 범위). 사용자는 목표와 범위(1~4)를 지시했고 구조 선택은 AI가 했다 |
| 사용자 검토 | 미검토. 사용자 직접 PIE 확인과 구조 검토는 대기 |
| 관련 | ADR 004(전체 검증 후 일괄 커밋)와 같은 원칙. 기존 실패 보존 계약 유지. `FCustomizationProfile`에서 `Kind`를 제거(내부 계약 변경) |

## Context / Decision Question

슬롯이 하나뿐이었다. 슬롯을 늘리면서도 "표시 준비가 성공하기 전에 상태가 먼저 바뀌지 않는다"는 기존 보증을 슬롯 전체로 넓혀야 한다.

## Decision / Rationale

- 슬롯은 `Crest`(머리 장식, 필수 — 기존 A/B), `Eyewear`(눈 장식, 선택), `Beak`(부리 장식, 선택)이다. 기존 body의 눈·부리 Morph 조절은 그대로이고
  장식은 같은 공통 Skeleton의 `head`에 skin된 별도 skeletal mesh다. 에셋 구조에서 무리가 없는 쪽(공통 Skeleton, 새 Skeleton·socket 불필요)을 골랐다.
- `FCustomizationProfile`은 `PartId`(Crest, 기존 이름 유지), `EyewearId`, `BeakWearId`를 갖는다. None은 선택 슬롯에서 "없음"이다.
- `TryApply`는 (1) 슬롯별 리소스 계약 검사 → (2) 로드 뒤 렌더 준비 상태 → (3) 이미 같은 외형인지 → (4) 새 MID → (5) 숨긴 채 등록·pose 준비 → (6) 한 번에 공개한다.
  1~5 어디서든 실패하면 표시 중인 component·MID·가시성은 하나도 바뀌지 않는다. 같은 static mesh의 MID 교체도 6단계에서만 한다. Model은 성공 결과 뒤에 Profile/Draft를 공개한다(기존 흐름).
- 실패는 `FCustomizationFailureDetail{Reason, Slot, bHasSlot}`으로 어느 슬롯인지 알린다. `ECustomizationFailure`에 `SlotMismatch`·`DuplicatePartId`·`CatalogUnavailable`을 더했다. 슬롯과 무관한 body 문제는 슬롯 없이 돌려준다.
  호환을 위해 `TryApply`·Model API의 반환은 enum 그대로이고 Detail은 `GetLastFailure()`로 읽는다. 화면은 "눈 슬롯: …"처럼 슬롯 이름을 붙여 보인다.
- 슬롯별로 바뀌지 않은 파츠(예: 고정색 안경)는 component를 그대로 둔다.

## Alternatives / Trade-offs

- **슬롯별 순차 적용 + 실패 시 롤백**: 롤백 경로가 새 실패 지점이 되고 중간 상태가 잠깐 보인다. 기각(프로젝트 규칙: 롤백 경로를 만들지 않는다).
- **Model 콜백이 Detail을 반환**: 모든 호출부와 테스트 람다가 바뀐다. enum 반환 + 마지막 실패 조회로 호환을 지켰다.
- **`TMap<Slot, FName>` Profile**: 일반적이지만 슬롯이 코드로 고정된 UI 탭과 맞물려 이점이 없다. 필드 세 개가 단순하다.
- 비용: 한 슬롯이 바뀌어도 변경된 슬롯 외에 색을 따르는 슬롯은 다시 준비한다(미리보기 한정). 장식은 body Morph를 따라가지 않는다(눈을 키워도 안경 크기는 그대로).

## Revisit Conditions

슬롯이 더 늘거나 장식이 body Morph를 따라야 할 때, 슬라이더 드래그 비용이 측정으로 문제가 될 때.

## Verification

`Duckov.Customization.Slots.*`(세 슬롯 일괄 교체, 슬롯별 한 곳만 실패, 재사용·제거·GC), `Duckov.Customization.UI.SlotSelection`(실제 렌더 pixel), 기존 Model/Skeletal/UI 테스트 유지.
