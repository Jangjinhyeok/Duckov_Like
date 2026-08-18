# ADR-001: 아이템 Definition 에셋 형식 — 단일 `DataTable`

| 항목 | 내용 |
| --- | --- |
| 상태 | Accepted |
| 날짜 | 2026-08-18 |
| 마일스톤 | M1a 전 |
| 관련 | `INVENTORY_DESIGN.md` A1을 대체 |

## Context / Decision Question — AI

아이템 종류마다 고정된 데이터(이름, 아이콘, 그리드 크기, 스택 최대치 등 — 개체가 아니라
"9mm 탄약"이라는 종류 자체의 스펙)를 Definition으로 관리한다. 이걸 UE 에디터에서 어떤 애셋
형식으로 만들고 관리할 것인가.

## Options — 사용자 먼저(Option Sweep) → AI 보완

사용자가 먼저 낸 두 그림: (a) 아이템 하나마다 에셋 파일을 만드는 방식(과거 해본 경험),
(b) 데이터 테이블 하나에 몰아넣고 한눈에 관리하는 방식(원하는 방식).

`INVENTORY_DESIGN.md`의 A1은 `UPrimaryDataAsset`(애셋 파일 방식)을 후보로 적어 두었으나,
이는 후보이지 결론이 아니며 사용자가 (b)를 원한다고 먼저 밝혔다.

## User Initial Decision — 사용자

> "내가 원하는 방식은 데이터 테이블에 몰아넣고 한눈에 보고 관리하는게 목적이야"

## User Reasoning / Concerns — 사용자

- 아이템 하나하나 애셋 파일로 만드는 방식은 해봤지만, 한눈에 보고 관리하는 게 목적이라 DataTable을 원함.

## AI Review — AI

최종 판단에 영향을 준 지적만 남긴다.

- **참조 방식이 달라진다.** `UPrimaryDataAsset`은 참조가 GUID 기반이라 이름을 바꿔도 안 깨지지만,
  DataTable Row는 참조가 `FName`(RowName) 기반이다. 에디터에서 행 이름을 바꾸면 그 이름을
  참조하던 Instance/저장 데이터가 끊어질 수 있다. 크리티컬하진 않지만 감수할 비용으로 남긴다.
- 아이콘·메시 같은 애셋 참조는 Row struct 안에 `TSoftObjectPtr` 필드로 넣으면 되므로 DataTable로도
  문제없다.
- `ADR-010`(Instance = `USTRUCT`)과 결이 맞는다 — DataTable Row도 `USTRUCT`(`FTableRowBase` 파생)
  기반이라 값 타입으로 통일된다.
- Asset Manager의 Primary Asset 자동 스캔·번들링은 DataTable에서 되지 않지만, 이 프로젝트 규모에서
  실익이 없어 무시 가능하다.
- 결정을 뒤집을 만한 반론은 없었다.

## Final Decision — 사용자

Definition은 **단일 `DataTable`**로 관리한다. Instance는 Definition을 **`FName`(RowName) + 참조
대상 `UDataTable`**로 가리킨다.

## Consequences / Accepted Costs — 사용자

**얻는 것**
- 모든 아이템 종류를 표 하나에서 한눈에 보고 편집한다.
- Row struct가 `USTRUCT`라 Instance 표현(`ADR-010`)과 값 타입으로 일관된다.

**감수하는 비용**
- Row 이름(`FName`)을 잘못 바꾸면 참조가 끊어진다 — 애셋 파일(GUID 기반) 대비 리네임에 약하다.
- Asset Manager의 Primary Asset 자동 스캔·번들링 이점을 포기한다(이 프로젝트 규모에서는 실익 없음).

## Revisit Conditions — 사용자

아래 중 하나가 관찰되면 이 결정을 다시 연다.

1. 아이템 종류 수가 늘어나 표 하나로 관리하기 번거로워지거나, 카테고리별로 애셋을 나눠 로드해야
   할 필요가 실제로 생길 때.
2. Row 이름 리네임으로 인한 참조 깨짐이 실제로 반복 발생할 때.

## 검증 — AI

1. M1a 헤더 작성 시 `FItemDefinitionRow : public FTableRowBase`(또는 동등한 이름)가 선언되고,
   Instance가 `FName` + `UDataTable*`(또는 `TSoftObjectPtr<UDataTable>`)로 Definition을 참조함을
   확인한다.
2. Definition 참조 관련 테스트(§9 검증 매트릭스의 "저장 — Definition 참조")에서 RowName 기반
   조회가 정상 동작함을 확인한다.
