# Duckov_Like — UE 5.7 탑다운 루팅·탈출 (인벤토리·캐릭터 커스터마이징)

**그리드 인벤토리와 캐릭터 커스터마이징을 Model · ViewModel · CommonUI 화면으로 나누고, 동작을 Automation Test와 실측 기록으로 검증하는 싱글플레이 탑다운 루팅·탈출 프로젝트**

[🎥 시연 영상](https://youtu.be/vTejYMb2Zfg) · [기술 문서 목록 (설계결정기록)](docs/architecture/) · [개발 기록 (worklog)](docs/worklog/)

`UE 5.7` · `C++` · `CommonUI` · `UMG` · `MVVM (FieldNotify)` · `Automation Test` · `Windows` · `GAS: 플러그인만 활성, 미구현`

> ### 🤖 이 프로젝트는 AI를 활용한 프로젝트입니다.
>
> **AI를 활용해 개발합니다.** 사용자는 목표·범위와 결과 수용을 맡고,
> AI는 기능 단위로 설계·구현·검증을 수행합니다.
> AI가 내린 기술 판단과 사용자가 실제 검토·수정한 내용을 구분해 기록합니다.
> 각 설계결정기록(ADR) 머리의 `판단 주체`·`사용자 검토` 칸에서 결정마다 확인할 수 있습니다.
>
> 판단 주체는 시기에 따라 다릅니다. M1a까지의 Model 결정(설계결정기록 001·003·004·009~012)은 AI가 선택지를 나열하고
> 사용자가 최종 결정했습니다. 그 이후의 구조·수치·테스트는 대부분 AI가 위임 범위 안에서 정했고,
> 사용자가 정한 목표·선택과 사용자 PIE 확인 여부를 각 기록에 적었습니다. 이 README도 그 구분을 따릅니다.
>
> → [작업 분담 규약](CLAUDE.md) · [설계결정기록](docs/architecture/) · [작업 기록](docs/worklog/)

---

## 프로젝트 탐색 가이드

| 순서 | 문서 | 담는 것 |
| --- | --- | --- |
| 1 | 포트폴리오 (외부 문서) | 프로젝트 요약과 맡은 역할 |
| 2 | **README (이 문서)** | What — 개요, 화면, 핵심 기능, 한계 |
| 3 | [설계결정기록](docs/architecture/) · [작업 기록](docs/worklog/) | How & Why — 결정 근거, 기각한 대안, 판단 주체, 검증 로그 경로 |

---

## 개요

| 항목 | 내용 |
| --- | --- |
| 기간 | 2026.08 ~ 진행 중 |
| 인원 | 1인 개발 (AI 활용) |
| 플랫폼 | PC (Windows), 싱글플레이 |
| 엔진 | Unreal Engine 5.7 (`.uproject`의 `EngineAssociation` `5.7`) |
| 모듈 | `InventoryCore` (Runtime) · `DuckovLike` (Runtime) · `InventoryCoreTests` (Editor) · `DuckovEditorTools` (Editor, [설계결정기록 030](docs/architecture/030-customization-catalog-editor-validation.md)) |
| 레퍼런스 | *Escape From Duckov* (Team Soda) — 분석 대상이지 클론 대상이 아니다 |

구현된 흐름은 타이틀 → 커스터마이징 → 베이스먼트 → 레이드(루팅) → 탈출 → 베이스먼트 창고다.
상용 게임의 완성이 아니라, 인벤토리와 커스터마이징 시스템을 설계대로 동작함을 검증 가능한 형태로 만드는 것이 목적이다.
네트워크/Replication, 베이스 건설, 펫·동료, 스킬 트리, 다수의 맵·적·무기는 의도적으로 범위에서 뺐다.

---

## 화면

![오리 캐릭터 걷기 루프](docs/images/readme/walk.gif)

걷기 애니메이션 — Blender에서 만든 Idle·Walk를 BlendSpace로 재생하고, 부위 mesh가 body pose를 따른다.

| 인벤토리 | 커스터마이징 |
| --- | --- |
| ![인벤토리 화면](docs/images/readme/inventory.png) | ![커스터마이징 화면](docs/images/readme/customization.png) |
| 장비(가방 slot)·가방·창고 그리드. 드래그로 옮기고, 같은 종류는 스택으로 합친다 | 부위 탭 7개, 16색 팔레트, 머리 모양 슬라이더, 실시간 미리보기 |

화면의 캐릭터·UI는 이 프로젝트에서 만든 것이며 원작 화면이나 에셋이 아니다.

---

## 핵심 기능

### 1. 실패하면 아무것도 바뀌지 않는 인벤토리 Model

- 이동·병합·정렬 같은 연산은 상태 없는 정적 조정자 `FInventoryOperations`가 관련 Container 전체를 먼저 검증하고 한 번에 커밋한다.
  실패하면 양쪽 Container, 점유 cache, ID 발급기가 그대로다. 실패는 `bool`이 아니라 `EInventoryOperationFailure`(`StackMismatch`, `StackFull`, `InvalidCategory` …)로 돌려준다.
- 이 조정자 구조와 Instance의 `USTRUCT` 값 타입 표현은 사용자가 최종 결정했다(ADR 004·010의 `Final Decision`).
  조정자는 "Container끼리 서로를 알고 처리하는 것보다 별도 조정자가 둘을 동시에 다루는 편이 낫다"는 판단이었고, Instance가 값이라 별도 롤백 경로 없이 원자성이 나온다.
- 무기·방어구 slot은 같은 이동 검사에 종류 규칙을 붙여 "놓을 수 있나"를 Model 한 곳에서 판정한다. 찬 slot에는 교체하지 않고 `Occupied`로 거부한다.
  작업 선택은 사용자, 규칙과 UI 연결은 AI가 정했고, 2026-10-17 사용자가 PIE로 장착·해제를 확인했다.

→ [ADR 004](docs/architecture/004-operation-service-atomicity.md) · [010](docs/architecture/010-item-instance-representation.md) · [012](docs/architecture/012-stack-merge-operation-contract.md) · [032](docs/architecture/032-weapon-armor-equipment-slots.md) · [M1a worklog](docs/worklog/M1a.md)

### 2. 이벤트 기반 UI 갱신 — 아이템 100개 실측

화면은 `Model → ChangeSet → ViewModel(FieldNotify) → Widget` 순서의 변경 통지로만 갱신한다(구조 판단 주체는 AI, ADR 015).
20×20 스태시에 아이템 100개를 넣고 Editor PIE에서 계측했다(2026-09-28 기록).

| 발견한 문제 | 조치 | 전 → 후 |
| --- | --- | --- |
| 같은 컨테이너 안 이동이 배열 순서를 바꿔 Item 위젯 100개를 지우고 다시 만듦 | "같은 컨테이너 안의 위치·회전 변경은 순서를 바꾸지 않는다"는 계약(사용자 결정)으로 기존 slot을 갱신 | 위젯 생성/제거 100/100 → 0/0, median 11.183 → 0.120 ms |
| 위젯이 FieldNotify 3개를 따로 구독해 한 번 바뀔 때 `Refresh()` 3회 | ViewModel이 통지 끝에 native event를 한 번 보내고 위젯은 그것만 구독 | Refresh 3 → 1회 |
| 화면이 가만히 있어도 Screen·Grid·Item의 `NativeTick`이 300 frame 동안 위젯마다 300회 | native 부모 클래스에 `meta=(DisableNativeTick)`, Tick 빈도는 `Auto` 유지(`Never`는 animation·latent action의 일시적 Tick까지 막음) | Tick이 돈 위젯 103 → 0개 |

- 측정 범위는 Editor PIE의 인벤토리 위젯 계층과 개발 PC 한 대다. 엔진·CommonUI 전역 Tick, packaged 빌드, 다른 기기는 측정하지 않았다.
- **남은 한계**: Sort는 여전히 Item 위젯 100개를 전부 지우고 다시 만든다. 중복 Refresh 제거로 median이 18.581 → 13.576 ms가 됐을 뿐, Sort 자체는 최적화하지 않았다.
- 이 성능 후속 작업들의 사용자 결과 검토는 아직이다.

→ [ADR 015](docs/architecture/015-inventory-viewmodel-flow.md) · [M2 worklog](docs/worklog/M2.md)

### 3. 캐릭터 커스터마이징 — 검증 후 일괄 교체

- 파츠는 DataTable 카탈로그의 row이고, `TryApply`가 모든 슬롯을 검증·준비한 뒤 한 번에 공개한다. 하나라도 실패하면 이전 외형이 그대로 남고 실패 사유를 알린다.
  등록 규칙은 런타임과 Editor validator(`DuckovEditorTools`)가 같은 코드를 공유한다. 이 구조는 AI가 정했고(ADR 028~030), 구조에 대한 사용자 검토는 아직이다.
- 현재 구성은 고정 몸통 + 머리 모양 슬라이더, 6개 부위 슬롯(눈·헤어·부리·날개·꼬리·발), 16색 고정 팔레트다(ADR 033).
  부위 구성과 고정 팔레트 방식은 사용자 지시·선택, 팔레트 색 수와 구조·테스트는 AI 판단이며, 2026-10-17 사용자 PIE로 확인했다.
- **비교 테스트 (2026-10-16 기록, 3슬롯 구성 시점)**: 상태를 먼저 바꾸는 테스트 전용 대조군과 같은 실패를 주입했다.
  실패 4가지(미등록 파츠 ID, 다른 Skeleton, 리소스 로드 실패, 세 슬롯 중 하나만 실패)에서 확정 값·Draft 어긋남, 표시 부분 변경, 실패인데 발행된 이벤트의 합계가
  대조군 3/3/3/4, 일괄 교체 0이었다.
  대조군은 "상태를 먼저 바꾼다"는 순서만 재현한 것이며 특정 제품의 코드를 재현한 것이 아니다.
- **새 파츠 추가 측정 (2026-10-16 기록)**: 같은 슬롯의 파츠 1개를 파일 5개(Blender 원본, FBX, uasset, 카탈로그 JSON·DataTable) 수정·추가로 등록했고 C++/UI 코드 수정은 0이었다.
  새 슬롯이나 새 Morph가 필요한 파츠는 코드 변경이 필요하다.

→ [ADR 028](docs/architecture/028-customization-part-catalog.md) · [029](docs/architecture/029-customization-slot-batch-replace.md) · [030](docs/architecture/030-customization-catalog-editor-validation.md) · [033](docs/architecture/033-customization-fixed-body-part-types.md) · [M3 worklog](docs/worklog/M3.md)

### 4. CommonUI 입력 수명과 화면 전환

- **Back과 입력 복귀 (2026-09-25)**: drag 중의 Back은 drag만 취소하고, 그 밖의 Back은 화면을 닫는다. 닫은 다음 tick에 Game 입력과 viewport focus를 돌려준다.
  사용자가 키보드·마우스로 수동 PIE를 확인하고 수용했다. 게임패드와 packaged 빌드는 not_run이다.
- **root layout과 Menu stack (ADR 021, 판단 주체 AI, 사용자 PIE 확인)**: 처음 시도한 Lyra식 "항상 활성인 HUD가 Game 입력을 갖는" 구조는
  PIE에서 메뉴를 연 다음 프레임에 Router가 HUD를 다시 active root로 골라 Game 입력으로 돌아갔고, lifecycle 테스트 2개가 실패해 폐기했다(정확한 원인은 미확정).
  대신 **Menu stack이 비는 순간 한 곳**에서 입력을 복구하고, 폐기한 설계의 실패 지점은 `Duckov.UI.LayerStack` 회귀 테스트로 남겼다.
- **화면 전환 (ADR 036·037)**: Menu stack에는 CommonUI 내장 `FadeOnly` 전환(0.3초)을 켰다. AI가 두 방식을 비교해 제시했고 사용자가 골랐다.
  맵 이동에는 원형 전환(circle wipe)과 도착 후 클릭 대기를 넣었다. 범위는 사용자가 정했고 구조·시간표는 AI가 정했다. 둘 다 2026-10-19 사용자가 PIE로 확인했다.
- 전환을 켜며 생긴 결함(전환 중 시작을 두 번 누르면 커스터마이징 화면이 두 개 쌓임)은 독립 검토에서 발견됐다. 수정 전 코드에서 실패하는 검사를 먼저 추가한 뒤 고쳤다.

→ [ADR 021](docs/architecture/021-commonui-layer-stack.md) · [036](docs/architecture/036-menu-stack-transition.md) · [037](docs/architecture/037-map-travel-circle-wipe.md) · [CommonUI 적용 구조](docs/COMMONUI_STRUCTURE.md) · [M2 worklog](docs/worklog/M2.md) · [M3 worklog](docs/worklog/M3.md)

### 5. 캐릭터 이동과 마우스 조준

- **걷기 (ADR 034)**: 공통 rig에 발 bone 2개를 더하고 Idle·Walk를 BlendSpace1D로 재생한다. AnimBlueprint 없이 single node로 재생하고, 부위 mesh는 body를 leader pose로 따른다.
  목표·동작은 사용자가 정했고 bone 구성·재생 구조·테스트는 AI가 정했다. 2026-10-18 사용자 PIE 확인.
- **마우스 조준 (ADR 035)**: controller가 커서를 pawn 높이의 수평면에 투영해 조준 지점을 정하고, 캐릭터가 회전과 카메라 앞당김을 맡는다.
  십자선은 Slate software cursor라 위치 갱신 Tick이 없다. 레이저 사이트 선은 기본 꺼짐이고 테스트용 L키로만 켠다. 목표는 사용자, 구조·수치는 AI. 2026-10-18 사용자 PIE 확인.
- import한 애니메이션 root가 reference pose보다 yaw +90° 돌아 있던 결함은 사용자 PIE 보고로 찾았다. 수정 뒤 이전 에셋으로 되돌려 방향 검사 세 개가 실패하는 것을 확인했다.

→ [ADR 034](docs/architecture/034-player-locomotion-animation.md) · [035](docs/architecture/035-mouse-aim-rotation.md) · [M3 worklog](docs/worklog/M3.md)

---

## Engineering Highlights

| 기술 | 실제 적용 |
| --- | --- |
| Model/UI 모듈 경계 | `InventoryCore`는 `Core`·`CoreUObject`·`Engine`에만 의존하고 UI 모듈이 없다. M0.5 실측에서 `InventoryCore`가 `UUserWidget::StaticClass()`를 쓰면 `LNK2019`로 빌드가 실패했다(헤더 include는 통과). [ADR 000](docs/architecture/000-module-boundaries.md)(문서 상태 Proposed) |
| MVVM · CommonUI | ViewModel은 ID로 Model을 조회하고 원본 상태 사본을 갖지 않는다. HUD·인벤토리·커스터마이징·타이틀이 root layout의 layer와 Menu stack 위에서 열린다 |
| 이벤트 기반 갱신 | ChangeSet → FieldNotify → 위젯. 인벤토리 위젯 계층의 상시 `NativeTick` 0을 300 frame 계측으로 확인(Editor PIE 범위) |
| 실패 원자성 | 컨테이너 간 이동, 정렬, 저장 레코드 복원, 레이드 탈출의 일괄 이전, 커스터마이징 적용이 모두 "전체 검증 → 한 번에 커밋" |
| 데이터 기반 카탈로그 + Editor 검증 | 파츠는 `DT_CustomizationParts` row. 등록 규칙은 런타임과 `DuckovEditorTools`의 `UCustomizationCatalogValidator`가 공유 |
| Automation Test | Model 단위 테스트와 실제 PIE·위젯을 쓰는 UI 테스트. 최신 전체 실행 **133 succeeded / 0 failed** (2026-10-19 기록, 이 README 작성 때 재실행하지 않음) |
| AI 활용 워크플로 | ADR마다 판단 주체·사용자 검토 칸, worklog마다 검증 로그 경로를 남긴다. 예: ADR 000 초안의 "컴파일 에러로 막힌다"는 AI 서술을 실측해 링크 단계 `LNK2019`로 교정했다 |

불변식 8개(`INV-01`~`INV-08`)와 연산 계약은 [인벤토리 설계 문서](docs/INVENTORY_DESIGN.md)에 있다.

---

## 한계와 다음 단계

- **GAS·장비 효과 미착수**: GAS는 `.uproject`에 플러그인만 활성화돼 있고 게임 모듈 의존과 구현이 없다. 방어구 효과도 GAS 범위로 미뤘다.
- **무기 동작 없음**: RMB 조준·사격은 범위 밖이고, 레이저 사이트는 장비 대신 테스트 키로 켠다. 장비 slot은 교체(swap)를 지원하지 않는다.
- **Sort full rebuild**: 정렬은 Item 위젯 100개를 전부 다시 만든다. 최적화하지 않았다.
- **저장**: 인벤토리 저장 레코드는 Model 계층까지다. 디스크 저장·슬롯과 커스터마이징 외형 저장은 구현하지 않았다.
- **not_run**: 게임패드 입력, packaged 빌드. 카탈로그 DataTable과 애니메이션 asset이 cook에 포함되는지는 확인하지 않았다.
- **측정 범위**: 성능 수치는 Editor PIE와 개발 PC 한 대의 기록이며 절대 시간 기준이 아니다.
- **사용자 구조 검토 대기**: ADR 013 이후 결정은 모두 사용자 PIE 확인이나 결과 수용을 거쳤다. 커스터마이징 구조(ADR 028~030)는 사용자 구조 검토가 남아 있다.
- **다음 단계**: [GDD](docs/GDD.md)의 M4 — 성능 점검과 문서·영상 정리.

---

## Source & Build

**요구사항**: UE 5.7 · Visual Studio 2022 (C++ 데스크톱 개발) · Git LFS

```bash
git clone https://github.com/Jangjinhyeok/Duckov_Like.git
cd Duckov_Like
git lfs install
```

| 모듈 | 타입 | 책임 | 의존 (Build.cs) |
| --- | --- | --- | --- |
| `InventoryCore` | Runtime | 그리드, 배치 규칙, 연산 서비스, ChangeSet, 저장 레코드 | `Core`, `CoreUObject`, `Engine` |
| `DuckovLike` | Runtime | ViewModel, CommonUI 화면, 캐릭터·상호작용, 타이틀·베이스먼트·레이드 흐름, 커스터마이징 | 위 + `InputCore`, `EnhancedInput`, `InventoryCore`, `UMG`, `ModelViewViewModel`, `CommonUI` (Private: `Slate`, `SlateCore`, `CommonInput`) |
| `InventoryCoreTests` | Editor | `InventoryCore` Public 계약의 Automation Test | `Core`, `CoreUObject`, `Engine`, `InventoryCore` |
| `DuckovEditorTools` | Editor | 커스터마이징 카탈로그 등록 검증 | `Core`, `CoreUObject`, `Engine`, `UnrealEd`, `DataValidation`, `DuckovLike` |

프로젝트 루트에서 PowerShell로 Editor target을 빌드한다. 출력의 `Result: Succeeded`로 성공을 확인한다.

```powershell
& "C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\Build.bat" DuckovLikeEditor Win64 Development "-Project=$PWD\DuckovLike.uproject" -WaitMutex -NoHotReload
```

Automation은 새 Editor process에서 실행한다. 아래는 worklog에 기록된 실행 형태에 현재 전체 회귀 필터를 넣은 것이다.
skeletal mesh 테스트가 `-NullRHI`에서 실패하므로 실제 RHI로 돌리고, 테스트 중 CEF assertion을 피하려고 `-nocefaccelpaint`를 붙인다.

```powershell
& "C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor.exe" "$PWD\DuckovLike.uproject" -unattended -nop4 -nosplash -NoSound -nocefaccelpaint -ExecCmds="Automation RunTests Duckov.+InventoryAssets.Verify+CustomizationAssets.Verify+CustomizationCatalogAssets.Verify+CharacterAnimationAssets.Verify" -TestExit="Automation Test Queue Empty" "-ReportExportPath=$PWD\Saved\Automation\Full" "-abslog=$PWD\Saved\Logs\Full.log"
```

에디터 시작 맵과 게임 기본 맵은 `L_Title`이다. 타이틀에서 게임 시작을 고르면 커스터마이징 → 베이스먼트로 이어진다.

---

## 문서

| 문서 | 내용 |
| --- | --- |
| [GDD](docs/GDD.md) | 목표·성공 기준·범위·기술 방침·검증 계획 |
| [인벤토리 설계](docs/INVENTORY_DESIGN.md) | 불변식, 연산 계약, MVVM 데이터 흐름 |
| [CommonUI 적용 구조](docs/COMMONUI_STRUCTURE.md) | root layout·Menu stack·layer 구성 |
| [커스터마이징 계획](docs/CUSTOMIZATION_PLAN.md) | 커스터마이징의 계약과 구현 기록 |
| [원작 UI 관찰 기록](docs/DUCKOV_UI_REFERENCE.md) | 레퍼런스 화면 구성 관찰(텍스트 기록) |
| [컨벤션](docs/CONVENTIONS.md) | 모듈 경계, 네이밍, Content 폴더 규약 |
| [설계결정기록](docs/architecture/) | 결정·근거·기각한 대안·판단 주체·사용자 검토 상태 |
| [작업 기록](docs/worklog/) | 마일스톤별 변경·검증·판단 출처 |

---

## 레퍼런스 표기와 크레딧

- 레퍼런스: **Escape From Duckov**(개발: Team Soda). 원작 화면은 설계 분석 목적으로 관찰해
  [텍스트로만 기록](docs/DUCKOV_UI_REFERENCE.md)했고, 원작의 스크린샷·에셋은 이 저장소에 넣지 않았다.
  저작권은 개발사에 있으며 본 프로젝트의 구현물이 아니다.
- 커스터마이징 에셋(`Art/CustomizationPrototype/`)은 AI를 활용해 이 프로젝트용으로 직접 제작했다. 원작 에셋을 추출하거나 사용하지 않았다.
