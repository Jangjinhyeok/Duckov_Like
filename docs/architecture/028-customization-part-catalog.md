# 설계결정기록 028 — 커스터마이징 파츠 카탈로그를 DataTable로

| 항목 | 내용 |
| --- | --- |
| 상태 | Accepted |
| 날짜 | 2026-10-16 |
| 작업 범위 | M3 / 커스터마이징 재설계 (1 카탈로그) |
| Risk | LOW — DuckovLike 안의 로컬 변경이고 저장 포맷·네트워크·외부 API 변경이 없다 |
| 판단 주체 | AI(위임 범위). 사용자는 목표와 범위(1~4)를 지시했고 구조 선택은 AI가 했다 |
| 사용자 검토 | 미검토. 사용자 직접 PIE 확인과 구조 검토는 대기 |
| 관련 | ADR 001(Definition = 단일 DataTable)과 같은 관례. `docs/CUSTOMIZATION_PLAN.md`의 CustomizationCatalog 책임을 구현 |

## Context / Decision Question

파츠 리소스가 `UCustomizationAppearanceComponent::GetDefaultResources`에 하드코딩돼 있었고 선택도 `PartId == "A" ? PartA : PartB`로 분기했다.
파츠를 하나 늘리려면 C++와 UI를 고쳐야 했다. 파츠 정보를 어디에 어떤 형식으로 둘 것인가.

## Decision / Rationale

- `FCustomizationPartRow : FTableRowBase` 한 행이 파츠 하나다. RowName이 파츠 ID, 필드는 슬롯·표시 이름·`FCustomizationPartResources`
  (mesh, 기대 Skeleton, material과 슬롯, 색상 parameter, Morph, mask와 parameter)다. 기존 리소스 구조체를 USTRUCT로 올려 재사용했고 자동 검사의 seam도 같은 타입을 쓴다.
- 에셋은 `/Game/Customization/DT_CustomizationParts` 하나다. 편집 원본은 같은 폴더의 `DT_CustomizationParts.json`이고 asset은 그 import 결과다
  (`CustomizationCatalogAssets.ImportJson`, Content Browser의 Reimport와 같은 일). 사람이 읽고 diff할 수 있고 에디터 UI 없이도 등록할 수 있다.
  `CustomizationCatalogAssets.Verify`가 asset과 JSON의 일치를 검사한다.
- 읽기는 `FCustomizationCatalog`(table을 붙든 조회 창구)가 맡고, 슬롯별 선택지는 row 순서 그대로 UI가 만든다. 코드에 파츠 이름이 없다.
- `ColorParameter`가 None이면 Hue로 색이 바뀌지 않는 고정색 파츠, `Morph`가 None이면 형상 조절이 없는 파츠다(눈·부리 장식이 쓴다).

## Alternatives / Trade-offs

- **PrimaryDataAsset 목록(파츠마다 에셋)**: 리네임에 강하고 Asset Manager와 맞지만, 파츠 수십 개를 한눈에 보고 고치기 어렵고 ADR 001과 다른 관례가 생긴다. 규모에서 이점이 없어 기각.
- **JSON 없이 DataTable asset만**: 등록은 에디터 UI로만 가능하고 변경 이력이 이진 파일에 묻힌다. 사람이 쓰는 원본을 남기려고 JSON을 두었다.
- 비용: RowName 리네임에 약하다(001과 같은 비용). 중첩 struct가 JSON에서 export 문자열로 나와 손으로 쓰기 번거롭다. FText의 localization key는 import마다 달라 일치 검사는 표시 문자열로 한다.
- 비용: DataTable은 cook에서 자동으로 따라오지 않는다. 패키징은 이번 범위 밖이며 `LoadObject` 경로 참조는 인벤토리 DataTable과 같다.

## Revisit Conditions

파츠가 수십 개가 되어 카테고리별 부분 로드가 필요하거나, RowName 리네임으로 Profile 참조가 실제로 끊길 때.

## Verification

`Duckov.Customization.Catalog.*`, `CustomizationCatalogAssets.Verify`. 새 파츠 1개(머리 장식 C)를 JSON row와 에셋 추가만으로 등록했고 C++·UI 수정은 0이다(측정은 `docs/worklog/M3.md`).
