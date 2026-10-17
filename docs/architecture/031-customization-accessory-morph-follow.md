# 설계결정기록 031 — 장식이 body Morph를 따라가는 데이터 기반 위치 보정

> 2026-10-17 후속: [033](033-customization-fixed-body-part-types.md)에서 안경·부리 고리와 body Morph 여섯 개를 없앴다.
> 이 문서의 장치(`FollowBodyMorphs`, 접촉 이동 측정)는 그대로 남아 헤어·눈이 머리 모양 Morph를 따르는 데 쓴다. 아래 내용은 당시 기록이다.

| 항목 | 내용 |
| --- | --- |
| 상태 | Accepted |
| 날짜 | 2026-10-16 |
| 작업 범위 | M3 / 커스터마이징 재설계 후속 (장식 × body Morph) |
| Risk | LOW — DuckovLike 안의 로컬 변경. 카탈로그 row에 필드 하나를 더하지만 Profile·저장 포맷·네트워크는 그대로이고 JSON/DataTable 재import로 되돌릴 수 있다 |
| 판단 주체 | AI(위임 범위). 증상(부리 길이를 바꾸면 고리가 따라가지 않음)은 사용자 PIE 보고이고, 방향(Morph 극값의 부착 위치를 기록해 보간)은 다른 세션이 권장안으로 전달했다. 측정 기준·안경 제외는 AI가 정했다 |
| 사용자 검토 | 사용자 PIE 확인(2026-10-16). 부리 고리 추종과 눈 크기 최대의 안경 겹침 두 항목에 "PIE 확인했어"로 답함. 이후 눈 크기 최대에서 안경이 눈과 겹쳐 보이는 것을 확인했고, 의도된 표현으로 수용해 보정하지 않기로 함(사용자 보고) |
| 관련 | [029 슬롯 일괄 교체](029-customization-slot-batch-replace.md)의 비용 항목 "장식은 body Morph를 따라가지 않는다"를 해소. [028 카탈로그](028-customization-part-catalog.md)의 row 계약 확장, [030 등록 검증](030-customization-catalog-editor-validation.md)의 공유 규칙 확장 |

## Context / Decision Question

장식(안경·부리 고리)은 body와 같은 Skeleton의 `head` bone에 skin된 별도 skeletal mesh이고 owner root의 원점에 붙는다.
Morph Target은 body 정점만 움직이고 bone은 움직이지 않으므로, 부리 길이 Morph를 바꾸면 부리는 늘어나는데 고리는 원래 자리에 남는다(사용자 PIE 확인).
장식 위치를 body Morph에 맞추되, 데이터 기반 등록과 "검증 후 한 번에 교체" 계약을 깨지 않아야 한다.

## Decision / Rationale

- 카탈로그 row의 `FCustomizationPartResources`에 `FollowBodyMorphs`(`BodyMorph`, `Offset`) 목록을 둔다. `Offset`은 그 body Morph가 weight 1일 때 장식을 옮길 거리(cm, component 공간)다.
- 적용 위치는 `Σ 현재 body Morph weight × Offset`이다. body Morph가 정점을 weight에 선형으로 옮기므로, 극값(weight 1)의 위치만 기록하면 중간 값은 근사가 아니라 같은 선형 보간이다.
  음수/양수 쪽이 서로 다른 Morph(`BeakShort`/`BeakLong`)라 최소·최대 지점을 각각 기록하는 것과 같다.
- 위치 계산은 `TryApply`의 3단계(재사용 판정)에서 하고, 새 위치의 component는 기존처럼 숨긴 채 준비한 뒤 6단계에서 한 번에 공개한다. 다른 슬롯이 실패하면 위치도 바뀌지 않는다.
  재사용 판정은 원점 대신 계산한 위치와 비교한다.
- 공유 규칙(`ValidateCustomizationPartContract`)은 없는 body Morph 이름(`MissingMorph`)과 유한하지 않은 Offset(`InvalidNumber`)을 거부한다. 에디터 validator도 같은 규칙을 쓴다.
- **어느 Morph를 따를지는 데이터(사람의 의도)가 정하고, 이동량은 측정값과 대조한다.** 측정은 "장식 정점마다 그 Morph가 움직이는 가장 가까운 body 정점(3 cm 이내)의 이동"의 평균(접촉 이동)이다.
  부리 고리 × 부리 길이: 최대 +7.27 cm, 최소 −5.09 cm(X, 앞쪽)를 등록했다.
- **안경은 이번에 보정하지 않았다(잠정).** 눈은 중심에서 커지므로 안경 근처 눈 정점의 평균 이동은 +0.20 / −0.14 cm로 허용 오차(0.3 cm) 안이다.
  다만 평균이라 상쇄가 있고 가장 앞 눈 정점은 최대 1.0 cm 나오며 정점 최소 간격은 1.84 → 0.15 cm로 준다. 해석적으로는 고리 구멍과 눈 사이에 약 0.4 cm가 남지만
  **사용자 PIE에서 눈 크기 최대일 때 안경이 눈과 겹쳐 보였고, 사용자가 이를 의도된 표현으로 수용했다.** 따라서 안경 무보정을 유지한다. 크기 보정이 필요하다고 다시 판단되면 이 결정을 재검토한다.
  안경 근처 부리 윗면은 4 cm 남짓 움직이지만 안경 아래를 앞뒤로 미끄러지는 이동이라 따라갈 이동이 아니다. 몸통 길이는 모든 장식의 접촉 이동이 0이다.

## Alternatives / Trade-offs

- **B. 부리 길이를 bone 기반으로 바꾼다**: socket·skin이 자연히 따라가지만 rig·body 에셋·기존 Morph 계약과 테스트를 모두 바꿔야 한다. 범위가 커서 기각.
- **C. 장식에 같은 이름의 Morph를 넣는다**: 고리 같은 단단한 물체가 늘어나 버리고, 장식마다 body Morph 6개를 다시 제작해야 한다. 기각.
- **실행 중 body Morph delta에서 위치를 계산**: 데이터 입력이 필요 없지만 적용할 때마다 정점을 훑고, "어느 Morph를 따를지"의 의도를 형상만으로 가를 수 없다(부리 고리는 따라가야 하고, 안경 아래 부리 윗면은 따라가면 안 된다). 측정은 테스트가 하고 런타임은 숫자만 쓰는 쪽을 택했다.
- **안경을 눈 앞면 이동(최대 +1.0 cm)만큼 옮기기**: 측정 기준(접촉 이동)과 맞지 않는다. 해석적으로 계산하면 눈 크기 최대에서도 고리 구멍 안쪽과 눈 사이에 약 0.4 cm가 남아(정점 최소 거리 측정은 0.15 cm) 보정하지 않았다.
- **정점 최소 간격을 판정 기준으로 쓰기**: 시험했지만 쓰지 않았다. 보정하지 않은 부리 고리는 해석적으로 계산하면 부리 최대에서 부리에 파고드는데도(Blender 원본 수치로 고리 구멍의 반폭·반높이 약 8.7·3.4 cm, 그 자리에 오는 부리 단면 약 12·4 cm) 정점 간격은 0.47 → 0.62 cm로 커 보였다. 정점 사이로 면이 지나가는 관통을 잡지 못한다. 간격 값은 근거로 기록만 한다.

## Consequences / Accepted Costs

```text
JSON row(FollowBodyMorphs) -> DataTable -> TryApply 3단계: 위치 = Σ weight × Offset -> 4~5단계 숨긴 준비 -> 6단계 한 번에 공개
테스트: body Morph LOD0 delta에서 접촉 이동 측정 <-> row의 Offset 대조
```

- 보정은 translation뿐이다. 회전·크기가 필요한 장식(눈과 함께 커져야 하는 안경 등)은 표현하지 못한다.
- body를 다시 제작해 Morph가 바뀌면 Offset도 다시 적어야 한다. 테스트가 측정값과 어긋나면 실패하고 측정값을 메시지로 알려 준다.
- 새 장식을 등록할 때 따를 Morph를 사람이 정해야 한다. 같은 슬롯·같은 계약의 파츠는 여전히 JSON row만으로 추가된다(028의 측정 조건 유지).
- 부리 고리는 부리 단면 모양이 늘어나면서 고리 두께 범위에서 조금 더 가까워진다(정점 간격 0.47 → 0.28 cm). translation 보정의 한계다.

## Revisit Conditions

눈 크기 최대의 안경 겹침(현재 사용자가 의도로 수용)을 더 이상 허용하지 않기로 할 때, 장식이 회전·크기 보정이 필요해질 때, body Morph가 선형 보간이 아닌 방식(보정 Morph, bone 기반)으로 바뀔 때, 장식 수가 늘어 Offset을 손으로 적는 비용이 커질 때.

## Verification

- 새 테스트 `Duckov.Customization.Slots.AccessoriesFollowBodyMorphs`:
  ① 모든 등록 파츠 × body Morph 6개의 접촉 이동·앞면 이동·간격 기록, 따르는 Morph는 Offset이 접촉 이동과 0.3 cm 안에서 일치,
  몸통 길이는 모든 장식에 닿지 않음, 부리 고리는 부리 길이 최대에서 3 cm 넘게 옮겨짐, 안경 근처 눈 정점의 평균 이동이 허용 오차 안(겹침 판정 아님)
  ② 눈·부리 −1·−0.5·0·0.5·1 × 몸통 −1·0·1 조합에서 장식 위치 = Σ weight × Offset
  ③ 같은 극값 재적용 시 component 재사용 ④ 다른 슬롯이 리소스 계약 단계에서 실패하면 부리 고리 component·위치 보존, 복구 뒤 위치 변경.
- 에디터 validator 테스트에 "추종 body Morph 이름 오류"·"추종 이동량 무한대" 사례 추가.
- 미검증: 같은 body Morph의 중복 항목은 거부하지 않고 런타임이 합산한다(테스트도 합산으로 맞춤). 준비 후반 실패에서의 위치 보존은 기존 실패 보존 테스트의 범위다.
- 결과는 `docs/worklog/M3.md`의 해당 절에 기록한다. 사용자 PIE 극값 확인은 완료(사용자 보고, AI 직접 관찰 아님).
