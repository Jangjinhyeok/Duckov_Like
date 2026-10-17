# 설계결정기록 033 — 원작 구성의 커스터마이징: 고정 몸통, 부위별 유형, 팔레트 색

| 항목 | 내용 |
| --- | --- |
| 상태 | Accepted |
| 날짜 | 2026-10-17 |
| 작업 범위 | M3 / 커스터마이징 추가 범위 (원작 구성 맞춤) |
| Risk | LOW — DuckovLike 안의 로컬 변경. 외형 Profile은 메모리 값이고 디스크 저장 포맷이 없다. 기존 슬롯·수치 필드를 바꾸지만 호출부는 커스터마이징 모듈과 테스트뿐이다 |
| 판단 주체 | 방향은 사용자 지시("몸통 모양은 고정이고 머리 모양이 따로 있고, 눈·헤어·부리·날개·꼬리·발의 유형과 색을 고른다"). 에셋 규모(부위당 2~3종)와 색 방식(고정 팔레트)은 사용자 선택. 구조·수치·테스트는 AI(위임 범위) |
| 사용자 검토 | 사용자 PIE 확인(2026-10-17, "부위 유형·색·머리 모양 문제없어"). 사용자 보고이며 AI 직접 관찰 아님 |
| 관련 | [028 카탈로그](028-customization-part-catalog.md)·[029 슬롯 일괄 교체](029-customization-slot-batch-replace.md)·[030 등록 검증](030-customization-catalog-editor-validation.md)의 장치를 그대로 쓰고 슬롯 구성을 바꾼다. [031 Morph 추종](031-customization-accessory-morph-follow.md)의 장치를 머리 모양에 쓴다 |

## Context / Decision Question

이전 구성은 머리·몸통·날개·부리·발·눈이 한 덩어리인 body에 Morph 여섯 개(눈 크기·부리 길이·몸통 길이)를 두고,
머리 장식(깃 A/B/C)·안경·부리 고리를 장식 슬롯으로 붙였다. 원작을 다시 관찰하니(`docs/DUCKOV_UI_REFERENCE.md`의 "커스터마이징 부위별 상세")
몸통 조절은 없고, 머리 탭은 피부색과 머리 크기·길이·모양 슬라이더이며, 나머지 여섯 부위가 각각 "유형 + 색상"이다.
원작에 가깝게 바꾸면서 028~031의 데이터 등록·일괄 교체·실패 보존 계약을 유지해야 한다.

## Decision / Rationale

- **고정 몸통**: body는 몸통+머리만 남긴 `SK_CustomizationBaseBody`다. Morph는 머리 모양 하나(`HeadFlat`, 머리 윗부분을 누른다)다.
  Profile의 `HeadShape`는 원작처럼 1이 기본(둥근 머리), 0이 가장 납작한 머리이고 weight는 `1 - HeadShape`다.
- **여섯 부위 슬롯**: `ECustomizationSlot`은 눈·헤어·부리·날개·꼬리·발이다. 헤어만 비울 수 있고(원작 헤어 0번이 없음), 나머지는 필수다.
  모든 부위는 같은 Skeleton(5 bones)에 skin된 별도 skeletal mesh이고 기존 카탈로그 row로 등록한다. 헤어는 이전 머리 장식 깃 A/B/C를 그대로 쓴다.
- **고정 팔레트 색**: 원작처럼 색상 칸을 누르면 swatch 격자가 펼쳐진다. 16색이고 Profile은 팔레트 index(`int32`)를 저장한다.
  피부색(몸통·머리)과 발 외의 다섯 부위가 각자 색을 가진다. 발은 원작처럼 유형마다 색이 정해진 고정색 material이다(row의 `ColorParameter`가 None).
- **body material은 body 자신의 슬롯 0**에서 온다. 이전에는 머리 장식(필수 슬롯) material을 body가 함께 썼는데, 헤어가 비어 있을 수 있어 연결을 끊었다.
- **머리 파츠의 머리 모양 추종**: 헤어·눈은 031의 `FollowBodyMorphs`로 `HeadFlat`을 따른다. 이동량은 테스트가 측정한 접촉 이동이다
  (헤어 −7.55~−7.78 cm, 눈 −2.17~−3.46 cm, Z). 부리·날개·꼬리·발에는 머리 모양이 닿지 않는다(접촉 이동 0).
- **색 슬롯 계약**: 색을 고르는 슬롯의 row는 색 parameter가 있어야 하고(`MissingColorParameter`), 발 row는 없어야 한다(`InvalidKind`).
  런타임 준비와 에디터 등록 검사가 같은 규칙(030)을 쓴다. 어긋나면 팔레트 선택이 성공으로 보이는데 표시가 바뀌지 않기 때문이다(독립 검토 지적).
- **슬롯 불일치·필수 누락·팔레트 범위**는 데이터 단계(`ValidateCustomizationProfileDetail`)에서 슬롯과 함께 거부한다. 표시 준비는 029와 같이 모든 슬롯을 검증·준비한 뒤 한 번에 공개한다.
- **화면**: 탭은 원작과 같은 일곱 개(머리·눈·헤어·부리·날개·꼬리·발)다. 유형 선택은 원작의 `< 숫자 >` 화살표 대신 기존 세로 목록을 유지했다
  (2026-10-16 사용자 지시 "모든 파츠에 리스트뷰"). 머리 크기·길이와 부위별 크기·위치·회전 슬라이더는 넣지 않았다(사용자 지시 범위 밖).

## Alternatives / Trade-offs

- **머리 모양을 유형 선택으로**: 사용자 설명은 "머리 모양이 따로 있다"였지만 원작 관찰상 연속 슬라이더(0.35~1)라 슬라이더로 했다.
- **기존 통짜 body를 유지하고 Morph로 부위 유형을 흉내**: 유형마다 Morph가 늘고 꼬리·장갑 손처럼 형태가 다른 유형을 만들 수 없다. 기각.
- **Hue 슬라이더 유지**: 구현은 단순하지만 원작과 다르고 검정·흰색 같은 무채색을 고를 수 없다. 사용자가 팔레트를 골랐다.
- **body가 첫 부위 material을 공유**: 이전 방식. 헤어 "없음"이나 발의 고정색 material과 충돌해 기각.
- **에셋 증량(부위당 4~5종)**: 원작 느낌은 더 가깝지만 제작·import·검사 비용이 부위 수만큼 커진다. 사용자가 2~3종을 골랐다.

## Consequences / Accepted Costs

```text
JSON row(슬롯, mesh, 색 parameter, 머리 모양 추종) -> DataTable -> TryApply: 6슬롯 검증·준비 -> 몸통(피부색, HeadFlat) + 부위(팔레트 색, 추종 위치) 한 번에 공개
화면: 탭 7개 -> 유형 목록 / 색상 칸(펼침 팔레트) / 머리 모양 슬라이더 -> ViewModel -> Model.TryEdit
```

- 눈·부리는 머리에 박힌 별도 mesh라, component를 다시 만드는 순서에 따라 교차선의 pixel 몇 개가 달라진다(실측 106개, 0.04%).
  실제 pixel 비교 테스트 하나(mask 해제 복원)를 "300개 미만"으로 완화했고, 같은 상태끼리의 비교는 정확 일치를 유지한다.
- 이전 Profile 필드(`PartId`/`EyewearId`/`BeakWearId`/`Hue`/`Shape`/`EyeSize`/`BeakLength`/`BodyLength`)와 ViewModel 함수는 없어졌다. 저장 포맷이 없어 이전 데이터 변환은 없다.
- 통짜 body·안경·부리 고리 uasset과 FBX는 지웠다. Blender 원본(`CustomizationPrototype.blend`·`CustomizationSlots.blend`)은 이력으로 남는다.
- 정면 미리보기에서 짧은 꼬리는 몸통 뒤에 가려 거의 보이지 않는다. 부채 꼬리만 머리 옆으로 보인다.
- 원작의 눈썹, 머리 크기·길이, 부위별 크기·위치·회전, 프리셋, 클립보드 복사는 범위 밖이다.

## Revisit Conditions

외형을 디스크에 저장하게 될 때(팔레트 index가 저장 계약이 되므로 HIGH), 부위별 크기·위치 조절을 넣을 때, 머리 모양 Morph를 다시 만들어 추종 이동량이 바뀔 때,
유형 수가 늘어 세로 목록보다 원작의 화살표 방식이 나아질 때.

## Verification

- 에셋: `Art/CustomizationPrototype/author_parts.py`(Blender 5.2.2) `create`/`render` PASS, `import_parts.py`(disposable pilot UE 5.7) 14개 import PASS, 공통 Skeleton hash 불변.
- 테스트(새 구성으로 옮김): `Model.*` 3, `Appearance.*` 3, `Catalog.*` 4, `Slots.BatchReplaceAtomicity`·`FailureNamesTheSlot`, `Slots.HeadPartsFollowHeadShape`(031 테스트 대체),
  `Skeletal.ActualAssetsAtomicityAndColor`, `Comparison.StateFirstSetterVsBatchReplace`, `UI.PIELifecycle`·`UI.SlotSelection`, `Catalog.EditorValidatorRejectsBadRegistration`,
  `CustomizationAssets.Verify`, `CustomizationCatalogAssets.Verify`, 타이틀·베이스먼트 흐름 테스트의 커스터마이징 부분.
- 결과와 독립 검토는 `docs/worklog/M3.md`의 2026-10-17 커스터마이징 절에 기록한다.
