# 설계결정기록 032 — 무기·방어구 장비 slot의 종류 규칙과 드래그 장착

| 항목 | 내용 |
| --- | --- |
| 상태 | Accepted |
| 날짜 | 2026-10-17 |
| 작업 범위 | M3 / 가방 외 장비 — 무기 slot 하나, 방어구 slot 하나 |
| Risk | LOW — 기존 API·저장 레코드 v1·Model 소유권을 보존하는 추가 기능. 정의 row에 필드 하나와 DataTable 행 하나를 더하고, 기존 Rifle 행은 장비 종류만 바뀐다 |
| 판단 주체 | 사용자: 다음 작업으로 "무기·방어구 장비 슬롯 작업 진행하자". Model 규칙·UI 연결·시작 상태·교체(swap) 제외는 AI(위임 범위) |
| 사용자 검토 | 사용자 PIE 확인(2026-10-17): 장착·해제와 화면 배치 "문제없어"(사용자 보고, AI 직접 관찰 아님) |
| 관련 | [020 가방 슬롯](020-bag-equipment.md)의 slot 보호·Load 재검증 방식을 따른다. [026 인벤토리 화면](026-inventory-screen-duckov-layout.md)의 `EquipmentSlot` 자리를 채운다. INVENTORY_DESIGN의 "장비 슬롯은 그리드와 다른 카테고리 제한을 명시" |

## Context / Decision Question

가방 slot(020)만 있고 무기·방어구 slot이 없었다. GDD P1은 무기/방어구/가방 slot을 요구하고, 원작은 캐릭터 패널의 장비 칸에 아이템을 끌어 장착한다.
장비 slot은 그리드와 다른 종류 규칙을 가져야 하지만, 장착된 아이템도 실제 ItemInstance로 Model이 소유해야 하고 저장 포맷은 바꾸지 않아야 한다.
방어구의 효과(방어력 등)는 GAS 작업의 범위라 여기서는 slot과 장착·해제만 다룬다.

## Decision / Rationale

- **종류는 정의 데이터**: `FItemDefinitionRow::EquipmentType`(`None`·`Weapon`·`Armor`). 기본값 `None`이라 기존 행은 장비가 아니다.
  장비 slot에는 종류가 같고 수량 1의 non-stackable 항목만 들어간다. 가방은 크기 변화라는 다른 규칙이 있어 기존 `BagGridSize` 그대로 둔다.
- **slot 규칙은 이동 검사에 붙인다**: `BindEquipmentSlot(SlotId, Type)`으로 named container를 slot으로 등록하면,
  `CanMove`/`TryMove`가 slot에 들어가는 항목의 종류를 검사해 `InvalidCategory`를 돌려준다. 이동 외 연산(병합·분할·정렬·리사이즈·추가·전부 이전)은 slot에서 `InvalidContainer`다.
  덕분에 ViewModel과 드래그 상태 머신은 그대로이고 "놓을 수 있나"를 Model 한 곳에서 판정한다(Model Query, D3).
- **위치는 Model이 정한다**: slot에 놓을 때는 가리킨 칸·드래그 중 회전과 상관없이 slot 원점·무회전으로 세운다(020의 원점 정규화와 같음).
  View는 미리보기를 같은 모양(원점, 원래 방향)으로 그린다. slot container의 grid 크기는 그 종류 아이템을 담는 저장 공간일 뿐이고 종류 규칙은 Model이 명시적으로 검사한다.
- **교체(swap)는 하지 않는다**: 찬 slot에 놓으면 `Occupied`. 먼저 해제하고 장착한다. 가장 단순하고 실패 원자성이 분명하다.
- **binding은 저장하지 않는다(020과 같음)**: 장착 아이템은 기존 container/item 레코드로 저장된다. 성공한 `Load` 뒤 slot은 등록(보호)은 유지하되 미검증 상태가 되어 이동도 막히고,
  호출자가 `BindEquipmentSlot`을 다시 불러야 한다. 가방 slot·가방 contents·교환 container와 같은 이름은 서로 등록을 거부한다.
- **UI는 기존 grid 위젯 재사용**: 화면이 처음 열릴 때 `EquipmentSlot` 안에 `WBP_InventoryGrid`(LeftGrid와 같은 class) 두 개를 만들어 "무기"(2×1)·"방어구"(2×2) 칸으로 쓴다.
  아이템 표시·드래그 시작·미리보기를 그대로 재사용하고, 화면은 두 slot이 Model에 등록됐을 때만 slot 줄을 보인다(데모 map은 감춤). 분할창이 열리면 slot 줄도 막는다. 상시 Tick은 없다.
- **시작 상태**: 세션은 방어구를 장착한 채 시작하고 소총은 창고에 있다(무기 slot 비어 있음). 장착과 해제를 모두 바로 해볼 수 있다.
- **데이터 변경은 명령으로**: `InventoryEquipmentAssets.AddDefinitions`가 Rifle을 `Weapon`으로 표시하고 `Armor`(2×2, `Armor`) 행을 더한다. 다시 실행해도 같은 결과다.

## Alternatives / Trade-offs

- **가방처럼 전용 `TryEquip` 명령과 버튼**: 슬롯별 명령이 명확하지만 드래그 경로와 따로 놀고, ViewModel·화면에 명령·버튼이 slot 수만큼 는다. 원작의 끌어 장착과도 다르다.
- **별도 Equipment Model/저장 레코드**: slot 의미를 직접 저장할 수 있지만 저장 version·migration 계약(HIGH)을 바꿔야 한다. 020과 같은 이유로 버렸다.
- **찬 slot에 놓으면 교체**: 원작에 가깝다. 이전 장비를 어디로 보낼지(끌어온 자리, 첫 빈칸) 정책과 반환 실패 처리가 더 필요해 이번 범위에서 뺐다.

## Consequences / Accepted Costs

- Model 공개 API: `BindEquipmentSlot`, `IsEquipmentSlot`, `IsEquipmentSlotBound`. 기존 API의 동작은 slot이 등록되지 않으면 그대로다.
- `CanMove`는 드래그 중 마우스 이동마다 불린다. 대상이 slot일 때만 DataTable row를 한 번 찾는다(TMap 조회). 대상이 일반 grid면 추가 비용은 slot 목록(2개) 선형 검색뿐이다.
- 정의 row 필드가 늘어 DataTable 에셋이 다시 저장됐다. 이전 `InventoryBagAssets.AddDefinitions`는 장비 이전 상태의 DT를 전제하므로 현재 DT에서는 실패하는 일회성 명령이 됐다.
- `InvalidCategory`의 문구는 "이 슬롯에 장착할 수 없는 아이템입니다."로 일반화했다(가방 slot도 같은 문구).
- 장비는 아직 게임플레이에 효과가 없다. 탈출해도 장착 상태는 유지된다(탈출은 가방 내용만 창고로 옮긴다).

## Revisit Conditions

- 화면이 열린 채 Load하는 경로가 생길 때: binding 변경 알림(ChangeSet flag)을 더하고 화면이 slot 줄을 다시 바인딩하게 한다.
- 장비 효과(GAS)·사망 시 장비 손실이 들어올 때: slot 조회 API(장착 항목 조회)와 변경 알림 필요 여부를 다시 정한다.
- 찬 slot 교체, slot 여러 개(무기 2개 등), 무기 크기가 slot보다 큰 정의가 생길 때: 교체 정책과 slot 크기 규칙을 다시 정한다.
- 장비 영속 저장(binding 저장)이 필요할 때: 저장 포맷 변경이므로 HIGH로 별도 결정한다.

## Verification

2026-10-17 실행 결과. 증거는 `Saved/Automation/Equipment/`에 있다.

- Editor·Game Win64 Development 빌드: 각각 `Result: Succeeded`(`EditorBuild2.log`, `GameBuild.log`).
- `InventoryEquipmentAssets.AddDefinitions`: 원본 DT를 `DT_ItemDefinitions-before.uasset`으로 복사한 뒤 실행해 성공, 재실행은 저장 없이 성공(`AddDefinitions.log`, `Targeted1.log`).
- 새 Model 테스트 5개(`Duckov.InventoryCore.Model.EquipmentSlot.*`): 등록 검증, 장착·해제와 원점 정규화, 종류·찬 slot·공간 실패의 원자성, 일반 연산 보호, Save/Load 뒤 재검증.
- 새 PIE 테스트 `Duckov.UI.EquipmentSlots`(베이스먼트 세션, 화면의 실제 grid 좌표로 드래그): slot 줄 위치·표시, 창고 소총을 무기 slot에 장착(원점 미리보기), 회전 중에도 무회전 미리보기,
  방어구를 무기 slot에 놓으면 이유 표시·상태 유지, 끌던 방어구를 가방에 해제, 분할창 중 slot 줄 비활성, 닫으면 감춤. `Duckov.UI.InventoryLayout`에 데모 map의 slot 줄 감춤을 더했다.
- 전체 회귀(`Duckov.+InventoryAssets.Verify+CustomizationAssets.Verify+CustomizationCatalogAssets.Verify`, 실제 D3D12 RHI, `-nocefaccelpaint`): **128개 / 실패 0**(125 succeeded + 3 succeededWithWarnings, `Full1`).
  경고 3건은 모두 기존의 캐릭터 이동 시뮬레이션 단계 로그다.
- 독립 검토: cpp-reviewer Approve, LOW 3건(이전 가방 정의 명령 주석 반영, binding 변경 알림과 slot grid Tick 추적은 미반영). 반영분 재검토는 not_run.
- 사용자 PIE 확인: 장착·해제와 화면 배치 문제없음(사용자 보고, 2026-10-17).
- 미검증: 해상도별 배치, 게임패드, 패키징.
