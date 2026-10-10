# 캐릭터 커스터마이징 구현 계획

2026-10-04 기준 Proposed 계획이다. 지상 플레이 캐릭터에 앞서 커스터마이징의 데이터·편집·미리보기 기능을 구현하고, 필요한 에셋을 직접 제작한 뒤 통합한다. 첫 과제는 **파츠 적용 실패 시 기존 외형을 보존하는 편집·적용 구조**다. 최종 목적은 회사 프로젝트의 커스터마이징 개선에 활용할 설계와 검증 근거를 만드는 것이다. 최소 prototype의 상태 보존을 먼저 검증하고 원작의 부위별 기능으로 확장한다.

이번 요청은 원작 레퍼런스 조사와 계획 작성이며, 사용자가 전달한 회사 프로젝트 조사 결과를 반영한다. 프로젝트 구현·에셋 제작·새 저장 형식 도입을 승인하거나 완료한 기록은 아니다. 기존 지상 캐릭터 계획의 순서에 이 작업을 먼저 넣으며, GDD와 INVENTORY_DESIGN 원문은 수정하지 않는다.

## 현재 상태와 조사 출처

- 현재 branch는 `main`, HEAD는 `c8e1e5c`이고 시작 시 staged/unstaged/untracked 변경은 없었다.
- 기존 World Loot·스택 분할·최소 Raid 성공 탈출·가방 교체는 Source와 M3 worklog에 존재한다.
- Source/Content에는 전용 커스터마이징 시스템과 캐릭터 에셋이 없다. 기존 CommonUI·MVVM과 인벤토리의 Model 소유권을 보존한다.
- 조사 대상은 사용자가 열어둔 Escape From Duckov의 커스터마이징 화면이다. Computer Use로 `Duckov.exe`의 창을 선택하고 활성화한 후 2560×1440 화면을 직접 확인했다. 최초 비활성 창 캡처는 게임 화면이 아니어서 조사 근거에서 제외했다.
- 원작 버전 번호와 내부 코드는 확인하지 않았다. 다음 관찰은 이번 화면의 UI와 조작 결과에 한정하며, 원작의 mesh 구성·shader·저장 형식을 추정해 사실로 쓰지 않는다.
- 회사 시스템에 관한 근거는 사용자가 전달한 읽기 전용 조사 보고서다. 해당 프로젝트의 working tree를 정적으로 조사한 결과이며, 여기서 회사 코드·에셋을 직접 열거나 실패 상황을 실행 재현하지 않았다. 보고서의 독립 검토 결과도 이 계획이나 향후 구현에 대한 독립 검토로 간주하지 않는다. 회사 내부 경로·식별자·코드·에셋은 이 문서에 복사하지 않는다.

## 원작에서 확인한 기능

| 분류 | 화면에서 확인한 항목 | 확인 수준 |
| --- | --- | --- |
| 머리 | 피부색, 머리 크기·길이·모양 | UI 관찰 |
| 눈 | 눈 유형·색상, 간격·높이·크기·회전 | UI 관찰 및 유형 6→7→6 변경과 미리보기 반영 확인 |
| 눈썹 | 눈 탭의 별도 열에 유형·색상, 간격·높이·크기·회전 | UI 관찰 |
| 헤어 | 유형·색상, 현재 유형 0 | UI 관찰 |
| 부리 | 유형·색상, 크기·높이·위치·회전 | UI 관찰 |
| 날개 | 유형·색상·크기 | UI 관찰 |
| 꼬리 | 유형·색상·크기 | UI 관찰 |
| 발 | 유형·크기 | UI 관찰 및 크기 1→1.36→1 변경과 미리보기 반영 확인 |
| 색상 | 색상 영역을 누르면 여러 색의 swatch 팔레트 펼침 | 헤어 색상 버튼을 열어 확인. 실제 색상 선택은 미실행 |
| 미리보기 | 우측 거울 형태의 3D 캐릭터, 편집 즉시 외형 변화 | 눈 유형·발 크기에서 실제 관찰 |
| 종료 | 확인·포기 버튼 | 버튼 존재만 확인. 적용·취소 계약과 재개방 결과는 미검증 |
| 프리셋 | 옐로우 덕 버튼 | 버튼 존재만 확인. 동작 미검증 |
| 데이터 이동 | 커스터마이징 데이터를 클립보드로 복사하기, 클립보드 데이터 적용 | 버튼 존재만 확인. 형식·오류 처리·왕복 결과는 미검증 |

좌측에는 머리·눈·헤어·부리·날개·꼬리·발의 7개 탭이 있고, 눈썹은 눈 탭 안에 있다. 현재 유형 선택은 숫자와 이전/다음 화살표로 표현된다. 발 탭에는 색상 조절이 보이지 않았다.

미리보기 영역의 가로 drag에서는 명확한 사용자 회전을 확인하지 못했다. 회전·zoom 지원 여부는 미확인이며, 우리 시스템의 회전/zoom은 개선 제안으로 다룬다. 시험한 눈 유형과 발 크기는 조사 전 표시값으로 복원했고 머리 탭으로 돌아왔다. 확인·포기·프리셋·클립보드 버튼은 누르지 않았으며 게임 저장 상태의 변경 여부를 별도로 검사하지 않았다.

## 개선 목표와 범위

전달된 보고서는 회사 시스템이 UE 5.6.1을 사용하며, 파츠 setter가 lookup·로드·적용 성공 전에 상태를 갱신하는 순서를 확인했다고 설명한다. 실제 실패 결과와 UE 5.7 호환성은 실행 검증하지 않았다. 이 정적 근거를 바탕으로 다음 우선순위를 제안한다.

| 조사 보고서의 개선 후보 | 이 프로젝트에서의 처리 | 검증 경계 |
| --- | --- | --- |
| 적용 성공 전 파츠 상태 갱신 | 첫 prototype 과제로 선정. 후보 값과 필요한 리소스를 먼저 검증하고 성공한 결과만 공개 | 자체 제작 에셋으로 실패 시 확정 값·Draft·표시 외형 보존을 실행 검사. 회사의 실제 장애 재현이나 수정 효과로 표현하지 않음 |
| 색상 codec 규약 불일치 | 후속 과제. 첫 prototype은 메모리의 색상 값만 사용 | 서버 규약과 왕복 결과 확인이 선행. wire 계약 변경은 HIGH |
| 개발용 저장 실패에도 완료 처리 | C6의 저장 통합 과제에 반영 | 저장 실패 시 편집 값 유지·재시도·재실행 복원 검사. 회사 온라인 저장의 문제로 일반화하지 않음 |

보고서에서 등록 누락·의상 Morph 불능·성능 저하는 확정되지 않았다. 이 항목을 현재 결함으로 다루거나, mesh·MID가 매 호출마다 생성된다고 가정하지 않는다. 다음 초기 개선 가정은 첫 prototype 이후의 추가 평가 대상으로 유지한다.

| 초기 개선 가정 | 제안 | 효과를 확인할 방법 |
| --- | --- | --- |
| 파츠 추가 때 코드와 UI 수정이 많을 수 있다 | 동일 분류의 새 파츠는 catalog 데이터와 에셋 등록으로 추가 | 새 파츠 1개 등록 시 수정한 파일·수작업 단계·걸린 시간 기록 |
| 외형 값의 관리·적용 경로가 분산될 수 있다 | 하나의 Profile과 동일한 외형 적용 Component 사용 | 같은 Profile을 두 preview에 적용해 동일한 선택·수치·색상 확인 |
| 조합 오류가 제작 이후 발견될 수 있다 | Skeleton·socket·material·변형 규약과 등록 검증 | 의도적으로 잘못 만든 파츠를 등록 단계에서 검출 |
| 편집 결과를 비교하거나 되돌리기 어려울 수 있다 | 편집 전 상태와 Draft 분리, 초기화와 적용/취소 제공 | 취소·재개방·반복 적용에서 상태 보존 확인 |

**첫 prototype 범위는 파츠 슬롯 1개·선택지 A/B·색상 1개·연속 형상 값 1개·즉시 미리보기·적용/취소/초기화/재개방이다.** 적용에 실패하면 마지막 정상 상태를 보존해야 한다. 초기화는 편집 값만 기본값으로 바꾸며 적용 전에는 확정 값을 바꾸지 않는다. A/B는 같은 색상·형상 파라미터를 지원하고 교체 시 해당 값을 유지한다.

이 상태 시험을 통과한 뒤 원작의 7개 탭과 눈썹 하위 항목, 색상 팔레트, 부위별 수치, 기본 프리셋으로 확장한다. 유형에는 이름을 표시하고 정식 에셋 제작 후 thumbnail을 추가한다. 자유 위치·회전·크기 조절과 preview 회전/zoom·카메라 초기화는 후속 범위다. 비동기 로딩도 최초 상태 시험의 필수 기능에 넣지 않는다.

다수 종족·무제한 체형·의상 시스템·장비 능력치·네트워크·범용 캐릭터 제작 도구는 현재 계획에 포함하지 않는다. 회사 개선 목표가 이런 기능에 있다면 그 목표에 맞춰 범위를 다시 정한다. 원작의 전체 유형 개수는 조사하지 않았으며 복제 목표로 삼지 않는다.

## 데이터와 책임 제안

이 절은 우리 프로젝트의 설계 제안이며 원작 내부 구조의 설명이 아니다. 새 모듈 없이 `DuckovLike` 안에 시작하고 커스터마이징 타입을 `InventoryCore`에 넣지 않는다.

| 구성 | 책임 |
| --- | --- |
| CustomizationCatalog | 고정된 분류와 파츠의 안정적인 ID, 표시 이름, 에셋 참조, 지원 파라미터·범위·기본값, 부착·material 규약 |
| CustomizationProfile | 선택한 파츠 ID, 색상, 변형 수치. Widget·Actor·동적 material instance 참조를 원본 외형 데이터에 넣지 않음 |
| CustomizationModel | 확정 Profile과 편집 Draft 소유, 후보 값의 데이터 검증·초기화·적용·취소. 외형 준비 성공 뒤 Draft를 갱신하고 실패 사유와 변경 이벤트 전달 |
| CustomizationViewModel | Model 명령 전달, 표시값·선택 목록·실패 안내. 외형의 별도 원본 상태를 보유하지 않음 |
| CustomizationAppearanceComponent | 후보 Profile의 리소스·조합 검증과 외형 준비, 성공한 결과를 mesh·material·변형으로 표현. preview와 이후 플레이 캐릭터가 같은 경로 사용 |
| CustomizationScreen과 PreviewActor | CommonUI 입력/focus·화면 배치, 미리보기 카메라·임시 회전/zoom. gameplay 상태를 소유하지 않음 |

화면 입력 → ViewModel 명령 → 후보 Profile의 데이터 검증 → AppearanceComponent의 리소스 검증·준비 → 성공한 외형과 Draft 공개 → 변경 이벤트·UI 갱신 순서로 연결한다. 후보 값은 성공 전까지 현재 Draft를 덮어쓰지 않는다. Model은 mesh·Widget 참조를 소유하지 않으며, 적용 흐름이 Component의 준비 결과를 받아 상태 갱신을 결정한다. UI는 매 frame 외형을 다시 읽거나 전체 파츠를 재생성하지 않는다. 연속 슬라이더 입력에서도 바뀐 파라미터만 반영한다. preview capture·회전·애니메이션의 비용은 인벤토리 Widget Tick과 따로 측정한다.

화면을 열면 확정 Profile에서 Draft를 시작한다. preview는 마지막으로 성공한 Draft를 표시하고 실제 플레이 캐릭터에는 적용 성공 시 확정 Profile을 전달한다. 취소·Back은 Draft를 버리고 확정 상태를 보존한다. 초기화는 Draft에만 기본값을 반영한다. 재개방하면 마지막 확정 Profile과 UI 선택 표시가 일치해야 한다. 파츠 교체 시 값 유지 또는 기본값 재설정 정책은 지원 파라미터별로 고정하고 UI에 드러낸다.

파츠 ID·종류·수치 범위와 NaN/Infinity, mesh 로드, Skeleton, 필수 material·mask·Morph 규약을 현재 상태를 바꾸기 전에 검사한다. 필수/선택 리소스와 파츠 없음의 ID 의미를 C0에서 정하고, 선택 mask의 부재를 일괄 오류로 처리하지 않는다. 실패는 사유를 반환하고 확정 Profile·직전 정상 Draft·현재 표시 외형을 모두 보존한다. 색상·Morph·mask의 준비 실패도 같은 계약을 따른다.

Profile 데이터 검사 통과만으로 외형 적용 성공을 선언하지 않는다. 최종 외형 공개 단계에 실패 가능성이 남으면 현재 외형을 건드리지 않는 준비·교체 방식으로 보완한 뒤 수용한다. 전체 snapshot rollback을 기본 해법으로 먼저 만들지 않는다. 첫 범위에서는 동기 로드로 계약을 검증하고, 비동기화를 추가할 때 대상 수명·취소·늦은 완료의 별도 시험을 마련한다.

같은 Profile을 반복 적용해도 scale·offset이 누적되지 않아야 한다. 파츠의 기준 transform에서 수치를 계산한다. 공유 material asset 자체를 수정하지 않고 대상별 외형 값을 적용한다. 커스터마이징은 시각 표현에 한정하고 이후 캐릭터의 capsule·이동속도·게임플레이 판정 크기를 자동 변경하지 않는다.

## 에셋 제작 전에 검증할 기술 선택

권장 시작점은 공통 Skeleton을 가진 기본 SkeletalMesh와 필요한 교체 파츠다. 독립 변형이 필요 없는 rigid 파츠는 socket 부착을 먼저 시험하고, 머리 길이·모양처럼 연속 형상 변화가 필요한 항목은 Morph Target 후보로 검증한다. 눈/눈썹의 표현과 얼굴 곡면 추종은 대표 시험 에셋에서 확인한 뒤 mesh·material 방식 중 하나로 확정한다.

이동 애니메이션을 따라 변형해야 하는 날개·발을 단순 socket 파츠로 무조건 확정하지 않는다. 대표 rig에서 움직임·파츠 교체·크기 조절을 함께 시험하고, 필요하면 같은 bone 구조의 skeletal 파츠와 Leader Pose를 사용한다. 별도 child animation이나 물리가 실제 필요할 때만 Copy Pose를 검토한다. Leader Pose는 child mesh의 render 비용을 없애지 않으므로 최적화 완료로 표현하지 않는다. [UE 5.7 Modular Characters](https://dev.epicgames.com/documentation/en-us/unreal-engine/working-with-modular-characters-in-unreal-engine?application_version=5.7)

socket은 Skeleton의 bone을 기준으로 부착점을 제공한다. Morph Target은 vertex 위치의 변형을 blend하는 방식이다. **Morph만 바꿔서는 socket이 자동으로 그 표면을 따라가지 않으므로**, 머리 변형과 눈·부리·헤어의 부착 위치를 함께 보정하는 계약을 시험한다. 위치 조절이 표면에서 뜨거나 파고들지 않도록 허용 범위를 제한한다. [UE 5.7 Sockets](https://dev.epicgames.com/documentation/en-us/unreal-engine/skeletal-mesh-sockets-in-unreal-engine?application_version=5.7), [UE 5.7 Morph Target Pipeline](https://dev.epicgames.com/documentation/en-us/unreal-engine/fbx-morph-target-pipeline-in-unreal-engine?application_version=5.7)

대안인 runtime Skeletal Mesh Merge는 초기 제작 규약과 결합 비용을 늘리고 Morph Target 전달을 기본 지원하지 않는다. 작은 단일 캐릭터 prototype에서 먼저 도입하지 않고, 필요한 외형을 완성한 뒤 측정된 render 병목이 있을 때 재검토한다. [UE 5.7 Modular Characters](https://dev.epicgames.com/documentation/en-us/unreal-engine/working-with-modular-characters-in-unreal-engine?application_version=5.7)

## 구현과 직접 제작 순서

| 단계 | 수행할 작업 | 다음 단계로 넘어갈 조건 |
| --- | --- | --- |
| C0 최소 계약 | 슬롯 1개·A/B·색상·Morph의 범위와 기본값, ID sentinel, 필수/선택 리소스, Skeleton·material·mask 규약 결정 | 실패 보존 대상과 정상/실패 시험 명세가 정리됨 |
| C1 데이터와 편집 기능 | Catalog·Profile·Model, 후보 검증·성공 결과 공개, Draft·초기화·적용·취소를 임시 데이터로 구현 | 실패 시 확정 값과 Draft 보존, 초기화·취소·재개방의 자동 검사 |
| C2 UI와 임시 미리보기 | CommonUI·MVVM으로 최소 선택·색상·형상 입력, 동기 리소스 준비·적용과 실패 안내 구현 | 정상 변경 반영과 실패 시 직전 외형 보존, 입력/focus/Back·재개방 확인 |
| C3 대표 시험 에셋과 적용 검증 | 몸체·공통 Skeleton의 A/B·material·Morph 1개 직접 제작. 다른 Skeleton·누락 참조·리소스 로드 실패, 두 캐릭터 시험 | UE 5.7 import/reimport와 최소 prototype의 전체 수용 기준 통과. 실패 보존·반복 교체·색상 독립성의 실행 증거 확보 |
| C4 정식 에셋 직접 제작 | C3 통과 후 규약에 맞춰 최종 캐릭터·부위별 파츠·색상 material·thumbnail 제작 | 각 파츠와 수치 최소/중간/최대, animation·LOD 조합의 부착·silhouette·clipping 확인 |
| C5 부위별 확장과 통합 | 정식 에셋으로 7개 탭·눈썹·프리셋 확장. 제한된 Transform·preview 회전/zoom을 필요한 순서로 검증 | 실제 화면의 편집·적용·재개방·취소가 일관되고 확장 부위에도 실패 보존 기준 적용 |
| C6 데이터 보관과 공유 | 필요 범위를 정한 뒤 외형 저장/로드와 Profile 복사/적용, 버전·불명 ID·불완전 입력·실패 보존 계약 설계 | HIGH 독립 검토와 사람 수용, 재시작/왕복/잘못된 입력의 실제 검사 |
| C7 지상 플레이 연결 | 이후 지상 캐릭터가 동일 AppearanceComponent와 확정 Profile 사용 | 걷기·조준 자세에서 외형 유지, 인벤토리 입력 복귀와 기존 Raid 루프 확인 |

사용자가 요청한 **기능 선행 → 필요한 에셋 직접 제작 → 시스템 통합** 순서를 유지한다. C1·C2의 placeholder는 기능과 상태 흐름을 검증하기 위한 것으로, rig/Morph의 검증을 대신하지 않는다. C3은 정식 에셋을 여러 개 만들기 전의 작은 제작 시험이며 최종 기능 지원 여부를 가르는 필수 단계다. 아직 없는 Morph나 socket을 placeholder 결과만으로 지원 완료라 하지 않는다.

C6은 새 serialization·영속성 계약이므로 HIGH이며 현재 조사/LOW 구현 단계와 분리한다. 기존 `FInventorySaveRecord` v1에 외형 필드를 섞지 않는 것을 권장한다. 저장 위치·Profile 버전·구버전 처리와 정확한 범위를 먼저 제시하고, 영향받는 구현 전 확인을 받는다. UI의 적용은 메모리의 확정 Profile 변경이라는 뜻이며 디스크 저장과 구분한다.

## 직접 제작할 초기 에셋 세트

첫 세트는 **기본 몸체 1개·공통 Skeleton의 교체 파츠 A/B·공통 material·연속 형상 값에 대응하는 Morph 1개**다. 같은 원본 material을 쓰는 두 preview 대상에서 색상 독립성을 검사한다. 시험용으로 다른 Skeleton의 파츠와 필수 참조 누락 데이터를 준비하고, 리소스 준비 실패도 재현 가능한 방식으로 검사한다. 이 잘못된 데이터는 사용자에게 선택 가능한 정상 catalog와 분리한다.

전체 부위 확장은 C3 통과 후 별도로 제작한다. 후보 세트는 눈 2종·눈썹 2종, 헤어 2종과 없음, 부리 2종, 날개 2종, 꼬리 2종, 발 2종이며 실제 제작량은 시험 결과에 맞춰 확정한다. 아래 규약은 최소 세트부터 적용하되 모든 부위의 제작을 먼저 요구하지 않는다.

- 기본 캐릭터: 공통 Skeleton·neutral pose, 변형에 필요한 topology·weight, 최소 idle/간단한 팔다리 움직임 시험.
- 연속 변형: 머리 크기·길이·모양과 부위별 수치의 표현 방식을 시험 결과에 맞춰 제작. 임의 scale만으로 모든 형상을 대체하지 않음.
- 파츠: pivot·bone/socket 이름·기준 transform 통일. 좌우 눈/눈썹·날개·발의 대칭 적용과 회전 방향 검증.
- material: 피부·눈·눈썹·헤어·부리·날개·꼬리의 색상 영역 구분. 외형 하나의 색상 변경이 다른 대상에 전파되지 않아야 함.
- 편집 원본: `.blend`에 mesh·rig·weight·shape key·material을 보존. 2D 편집이 필요하면 layered 원본도 보존. export용 변경이 원본 편집 구조를 파괴하지 않도록 관리.
- thumbnail: 최종 파츠를 같은 카메라·조명 규약으로 직접 render. 파츠를 바꿨을 때 thumbnail도 갱신되는 경로 확인.

최종 캐릭터는 원작 화면을 기능·스타일 레퍼런스로 삼아 직접 제작한다. 원작 게임 파일에서 mesh·texture·rig를 추출해 프로젝트 에셋으로 사용하지 않는다. 정식 source/export/Content 경로와 LFS 대상은 C3 시작 때 정하고 실제 파일이 생길 때 폴더를 만든다. 새로운 제작 도구 설치나 유료 생성은 이 계획 작성의 권한에 포함하지 않는다.

## 검증과 완료 기준

첫 완료 장면은 **화면 열기 → A/B 교체·색상·형상 편집 → 잘못된 파츠 적용 시도 → 직전 정상 외형 보존 → 정상 적용 → 재개방 유지 → 추가 편집·초기화 후 취소 → 확정 외형 유지**다. C5에서는 부위별 수치와 여러 각도 확인을 추가하고, C6에서는 저장·재시작·Profile 복사/적용의 왕복 장면을 추가한다.

| 첫 prototype 수용 기준 | 판정 |
| --- | --- |
| 미등록 ID·잘못된 종류·다른 Skeleton·필수 참조 누락·리소스 준비 실패 | 실행한 각 실패 사례에서 확정 값·직전 정상 Draft·표시 외형 변경 0건 |
| 취소·초기화·Back·재개방 | 확정 값 오염과 값/선택 표시 불일치 0건. 초기화 후 적용하면 기본값이 확정됨 |
| A→B→A와 같은 Profile 반복 적용 | mesh·색상·Morph·mask 잔류 또는 누적 0건 |
| 같은 원본 material을 쓰는 두 대상 | 한 대상의 편집에 의한 다른 대상의 색상 변경 0건 |
| 입력 반응과 비용 | 입력→미리보기 시간, frame time, component/material 생성 개수를 같은 조건에서 기록. 성능 개선 여부는 측정 후 판단 |

0건은 명시한 입력과 실행한 시험의 수용 기준이며 모든 가능한 조합의 무결함 보증이 아니다. C6에서는 저장 실패의 성공 안내 0회와 편집 값 유지·재시도·재실행 복원을 추가한다. 저장을 구현하지 않은 첫 prototype에 저장 시험 PASS를 부여하지 않는다.

적용 순서의 개선 전후 비교가 필요하면 회사 코드 대신 동일 UE 5.7 환경·자체 제작 에셋을 사용하는 격리된 시험 fixture에서 상태 선변경과 사전 검증의 차이를 재현한다. 실패하는 비교 fixture는 제품 동작에 넣지 않는다. 이 비교로 회사 UE 5.6.1의 runtime 결과나 성능 개선율을 입증했다고 표현하지 않는다.

| 검증 | 확인할 내용 |
| --- | --- |
| Model 자동 검사 | 유효/불명 ID, 파츠와 분류 불일치, 수치 경계·NaN/Infinity, 초기화, 적용/취소, 실패 보존, 이벤트 중복 |
| 적용 Component | 실패 시 기존 외형 보존, 동일 Profile의 결정적 외형, transform 누적 없음, 교체 전 값 잔류 없음, 필수 socket/material/mask/Morph 검출, 대상 간 색상 독립 |
| UI/PIE | 변경 즉시 미리보기, 탭/선택/슬라이더 일치, modal·Back·focus 복귀, 취소/재개방, 반복 activation 구독 정리 |
| 에셋 | 원본 재개방/편집·export, UE import·fresh-process save/reload, 작은 원본 수정 후 같은 asset reimport, neutral/극단 조합·간단한 애니메이션의 clipping |
| 성능 | 초기 열기, idle, 슬라이더 연속 편집, 파츠 교체를 나눠 측정. Widget 갱신·capture·game/render thread·component/material 수를 구분 |
| 회귀 | 변경 영향에 맞는 Editor/Game Development build와 Automation. 기존 인벤토리 입력 수명·원자성·100-item/상시 Tick 증거 보존 |
| 개선 근거 | 동일 분류에 새 파츠 1개를 등록하고 코드/UI 수정 여부·작업 단계·시간 기록. 회사 시스템과 실제 비교하지 않은 개선율은 보고하지 않음 |

프레임 시간·메모리 개선을 목표로 잡는 경우 최적화 전 baseline을 먼저 측정한다. 단일 캐릭터의 작은 목록에 선제적 mesh merge·pooling·virtualization·복잡한 cache를 추가하지 않는다. 파츠 수 증가나 로딩 지연이 실제 문제가 된 뒤 필요한 범위를 정한다. preview를 닫으면 capture·입력·임시 대상의 수명을 정리하고, 인벤토리에 상시 polling을 추가하지 않는다.

구현 시 표준 Build.bat 명령의 실제 `Result: Succeeded`와 대상 Automation의 succeeded/failed/notRun을 확인한다. UE interactive 검증은 해당 단계에서 사용자 직접 테스트 또는 Computer Use의 선택을 확인한다. 이번 원작 조사에 대한 Computer Use 요청을 향후 UE PIE 전체의 자동 승인으로 해석하지 않는다.

## 실행 경로와 미결 사항

이번 조사는 main이 직접 수행한다. 화면 관찰과 범위 판단이 연속된 단일 계획 작업이어서 별도 구현 agent를 만들지 않았다. 구현에 들어갈 때 active agent-routing 원문을 읽고, Model·UI·에셋 제작을 분리할 실익과 파일 소유권을 판단한다. 동일 tree의 동시 writer는 두지 않으며 통합·검증 책임은 main에 남는다.

첫 개선 과제는 전달된 조사 결과에 따라 적용 실패 시 상태 보존으로 좁혔다. 미결 사항은 실제 실패·성능 baseline, 최종 캐릭터 스타일, 수치 허용 범위, skeletal 파츠/Morph 조합의 시험 결과, 영속 저장·공유 범위다. 회사에 적용하기 전에는 전체 Blueprint 취소 경로, 서버 응답과 편집의 충돌, ID sentinel·성별/종류 매핑, 저장 시점, mask 선택성, cook·LOD·animation 결과를 해당 프로젝트에서 확인해야 한다. 이 prototype의 결과만으로 회사 시스템 적용 적합성이나 개선 효과를 선언하지 않는다.

이번 결과는 원작 7개 탭의 직접 관찰과 일부 유형/수치 조작, 전달된 회사 조사 결과를 반영한 계획 작성, main self-review까지다. 회사 보고서의 정적 검토 결과와 이 프로젝트에서 실행한 검증을 구분한다. 프로젝트 구현·build·Automation·에셋 제작/연결·독립 검토는 `not_run`이다. 계획은 사용자 검토 대기이며 Accepted ADR가 아니다. 향후 HIGH 구현의 사전 설계 검토는 구현 후 독립 검토와 사람 수용을 대신하지 않는다. commit/push는 수행하지 않는다.

## C0~C2 로컬 구현 기록 — 2026-10-04

위 조사·계획 작성 기록을 보존하고 이번 구현 결과를 별도로 덧붙인다. 사용자가 C0~C2의 로컬 구현·검증을 명시 요청했다.
Risk LOW: DuckovLike 내부의 메모리 prototype이며 기존 save/InventoryCore/Raid 계약·에셋을 보존한다.
시작은 main, 기존 변경은 이 계획 파일만 untracked였다. 원문은
Saved/Automation/CustomizationGate/baseline/CUSTOMIZATION_PLAN.md에 복사했고 원문 prefix 보존을 검사했다.
branch 변경·commit·push는 수행하지 않았다.

### 구현 계약과 책임

| 단계 | 실제 구현 상태 |
| --- | --- |
| C0 최소 계약 | 구현 완료. 필수 슬롯 Part 1개, ID A/B. NAME_None은 파츠 없음이며 필수 슬롯에서 거부 |
| C1 데이터와 편집 | 구현 완료. Model이 확정 Profile·Draft·편집 상태를 단독 소유. 유한 수·종류·ID·범위 검사와 성공 후 공개 |
| C2 UI와 임시 미리보기 | 구현 완료. native CommonActivatableWidget, MVVM FieldNotify, A/B·슬라이더·적용·취소·초기화·Back·재개방·실패 안내 |
| C3 자체 에셋 시험 | not_run. 공통 Skeleton·실제 Morph·mask의 정상 시각 적용과 애니메이션/LOD/import 시험 필요 |

- Hue 범위 [0, 1], 기본 0.12. HSV의 S=0.7, V=0.9 고정이며 두 파츠에 같은 색상 규약을 적용한다.
  Shape 범위 [-1, 1], 기본 0. A/B 교체 시 두 값을 유지한다. NaN/Infinity와 범위 초과는 거부한다.
- Model에는 Profile 값만 있으며 mesh·Widget·MID 참조가 없다. ViewModel은 Model getter·명령·실패 안내만 맡는다.
  AppearanceComponent에는 표시 mesh/MID와 파츠별 resource descriptor가 있고 별도 원본 Profile은 없다.
  controller가 메모리 Model을 보존하며 preview Actor는 화면마다 만들고 닫을 때 폐기한다.
- 입력 → 후보 Profile → 데이터 검사 → 동기 리소스 준비/검사 → 외형 공개 → Draft/Profile 공개 → 이벤트 순서다.
  실패한 입력·최종 적용·초기화·취소는 기존 Profile·Draft·표시 외형과 편집 상태를 보존하고 Model 이벤트를 발행하지 않는다.
  실패 안내의 FieldNotify는 원본 상태 이벤트와 구분한다.
- 같은 mesh의 parameter 편집은 새 MID를 준비·검사한 뒤 material/절대 transform/Morph setter를 실행한다.
  mesh 교체는 숨긴 후보 component의 등록·참조 검사를 끝내고 공개하며 이전 component를 폐기한다.
  동일 표현은 기존 mesh/MID를 유지한다. 공유 material asset을 직접 변경하지 않는다.
- 화면 열기는 확정 Profile에서 시작한다. 편집·초기화는 Draft만 바꾼다.
  적용은 외형 준비가 성공한 Draft를 메모리 Profile로 확정하고 화면을 닫는다.
  취소·Back은 확정 외형 복원 성공 후 닫으며, 복원 실패 시 화면을 유지한다.
  owner/world teardown은 preview를 폐기하고, 남은 편집이 있으면 다음 Open에서 확정 외형 준비 후 정리한다.
- CommonUI Menu 입력·초기 focus·Back·Game 입력 복귀를 기존 관례로 연결했다.
  화면 종료 시 FieldNotify/Model 구독·render target·capture·임시 mesh/MID·Actor를 해제한다.
  native 화면 Tick을 끄고 capture는 성공 이벤트에 따라 요청한다. 인벤토리에 polling을 추가하지 않았다.
- AI 선택: 숨긴 후보 준비와 같은 mesh의 parameter 교체를 사용했다.
  전체 외형 snapshot을 되돌리는 범용 rollback 대안은 복구 경로와 상태 복사 비용 때문에 채택하지 않았다.
  parameter 편집은 MID를 새로 준비하는 비용이 있으며 frame time/메모리 개선을 주장하지 않는다.

### 실제 에셋과 테스트 대역의 경계

기존 Content는 인벤토리 DataTable·WBP 3개·데모 map이며 skeletal 캐릭터/Morph 시험 에셋은 없었다.
엔진 Cube/Sphere와 M_SimpleOpaque의 실제 Color vector parameter를 runtime에서 확인해 사용했다.
Static placeholder의 Shape는 Z scale = 1 + 0.35 × Shape라는 **대체 표시**이며 실제 Morph 지원 증거가 아니다.
정상 placeholder는 mask가 필요 없다.

skeletal 경로는 기대 Skeleton asset의 동일성, 필수 Morph 이름, material 슬롯/색상 parameter를 검사하고
Morph curve를 절대값으로 설정한다. 유효한 skeletal/Morph 에셋으로 deformation·capture 결과를 시험하지 않았다.
Skeleton 누락·불일치와 Morph 누락의 자동 진단에는 transient UObject 대역을 사용했다.
필수 mask 누락·없는 texture parameter는 실제 engine material로 실패 검사했지만,
정상 mask의 시각 효과는 not_run이다. descriptor에 mask parameter를 선언했다면
선택 mask 없음은 /Engine/EngineResources/WhiteSquareTexture로 준비하며 제공된 경로의 로드 실패는 거부한다.

### 실행·검토 증거

main이 계약·UI·통합·검증을 맡고 native gameplay-programmer가 core 7파일을 구현한 뒤 writer 소유권을 반환했다.
동일 tree의 동시 writer는 없었다. 호출 역할의 설정은 builder_normal, GPT-6.1 Sol xhigh이며
실제 실행 model/effort metadata는 확인하지 못했다. main과 review에는 builder Compute를 붙이지 않는다.
독립 cpp-reviewer는 fresh context로 구현 후 검토한다.

| 실행한 검사 | 결과와 근거 |
| --- | --- |
| 최종 DuckovLikeEditor Win64 Development Build | PASS, 실제 Result: Succeeded, exit 0. Saved/Automation/CustomizationGate/EditorBuild-final.log |
| 최종 DuckovLike Win64 Development Build | PASS, 실제 Result: Succeeded, exit 0. Saved/Automation/CustomizationGate/GameBuild-final.log |
| 최종 Duckov.+InventoryAssets.Verify | PASS, 92 succeeded / succeededWithWarnings·failed·notRun·inProcess 모두 0, Editor exit 0. Saved/Automation/CustomizationFinal/index.json, Saved/Logs/CustomizationFinal.log |
| 기존 회귀 보존 | 이전 BagEquipmentFinal의 85 test path 누락 0. InventoryCore·저장·Raid·기존 UI/입력·100-item/Tick 검사 포함 |
| Model 대역 3개 | 데이터 실패·리소스 callback 실패·최종 적용/취소/초기화 실패의 세 값 보존, 재시도, lifecycle, 중복 이벤트, 공개 순서·재진입 PASS |
| 실제 engine resource 3개 | Cube/Sphere 등록·MID·실패 보존·같은 Profile pointer 유지·A→B→A 색상/scale 잔류·대상 간 색상 독립·GC PASS. skeletal 진단만 transient 대역 |
| 자동 PIE UI 1개 | 실제 native 화면과 delegate 입력, CommonUI Back routing/focus/Game 복귀, 적용 실패 후 화면 유지·재시도, 초기화·취소·재개방·구독 수명 PASS |
| 실제 표시 외형 보존 | RHI render target의 실패 전후 FColor 배열 동일. Saved/Automation/CustomizationGate/Visual/EnginePreviewB.png, EnginePreviewB-AfterFailure.png |
| 화면 화상 검사 | main이 최신 EditedB-FailureNotice.png·ResetDraft.png와 preview PNG를 직접 확인. 임시 표시·선택/슬라이더/실패 안내 확인 |
| scope·secret·baseline | PASS. 설치된 secret_scan 규칙의 read-only 검사 0건, 예상 밖 변경 0, staged 0, 테스트한 Source/Content hash 불일치 0, 계획 원문 prefix 보존. Saved/Automation/CustomizationGate/delta-check.json |
| self-review | 리뷰 완료, 이슈 없음. 원본 상태 소유·실패 순서·구독/preview 해제·실제 delta 확인 |
| 독립 cpp-reviewer | PASS — 리뷰 완료, 이슈 없음. fresh context에서 신규 C++ 14개·controller 2개·관련 기존 입력/UI·계획 계약·Build/Automation·90개 Source/Content hash와 PNG 동일성을 직접 확인. reviewer의 Build/PIE 재실행은 not_run |
| 사용자 직접 PIE·결과 수용 | 대기. 자동 PIE/delegate 검사와 실제 사람의 키·마우스 확인을 구분 |

처음 sandbox Build는 UBA ProgramData 접근 실패로 중단했다(EditorBuild-first.log).
필요한 권한으로 재실행한 Build의 FlushRenderingCommands linker 실패는
Engine ReadPixels가 내부에서 이미 동기화하는 사실을 확인해 불필요한 직접 호출을 제거했다(EditorBuild-elevated.log).
첫 Automation launcher는 인자 quoting 때문에 대상 시험 없이 종료했고 PASS로 쓰지 않았다(CustomizationTargeted.log).
quoting 수정 후 targeted 결과는 6 성공/1 실패였으며, 동일 색상 비교의 tolerance 0으로 MID가 재생성되는 결함을 수정했다
(CustomizationTargetedFixed/index.json). 최종 92개는 이 수정과 pixel 보존 검사를 포함한다.
Game Build의 새 UI deprecated API 경고도 지원 API로 고쳐 최종 Build를 재실행했다.

### 다음 C3 에셋 규약과 사람 확인

- 기본 body 1개와 파츠 A/B는 **동일 Skeleton asset**을 사용한다. bone 구조/neutral pose·weight·pivot을 통일하고
  이번 교체 파츠의 상대 translation/rotation은 0, skeletal scale은 1로 둔다. 자동 retarget/socket 부착은 구현하지 않았다.
- 슬롯 0의 공통 surface material이 필수이며 Color vector parameter와 skeletal mesh 사용을 지원해야 한다.
  Hue에서 변환한 linear RGB를 대상별 MID에 적용한다. 다른 material 슬롯의 색상은 이번 범위에 포함하지 않는다.
- A/B 모두 Shape Morph Target을 가져야 한다. 현재 계약은 weight = (Shape + 1) / 2로 [0, 1]에 매핑한다.
  기본 Shape=0은 weight=0.5이므로 이 지점을 기본 외형으로 제작한다.
  실제 vertex deformation·neutral/양 끝값·교체·idle/움직임·LOD·clipping과 capture 갱신 시점을 C3에서 검증한다.
- mask를 쓰면 descriptor에 정확한 texture parameter 이름을 선언한다.
  필수 mask는 유효한 texture 참조가 필요하고 선택 mask 없음은 흰색 neutral로 정의한다.
  material은 흰색이 neutral mask로 동작하도록 제작한다.
  유효 mask/선택 없음/교체의 pixel 잔류 시험과 두 대상 색상 독립 시험을 수행한다.
- .blend의 mesh·공통 rig·weight·shape key·material을 보존하고 export/import·fresh-process 재로드·reimport를 검사한다.
  제작/툴 설치·새 저장 형식·네트워크·7개 탭·지상 플레이 연결은 이번에 실행하지 않았다.

사람 확인: L_InventoryDemo에서 PIE → console OpenCustomizationPrototype → console 닫기.
A/B·두 슬라이더 즉시 반영 → 개발용 실패 버튼에서 직전 선택/외형 보존 → 정상 편집 후 적용 →
같은 명령으로 재개방해 값/선택 확인 → 추가 편집·초기화 뒤 취소 또는 Escape →
재개방해 확정 값 확인 → 초기화·적용·재개방해 기본값 확인.
닫힌 뒤 I의 기존 인벤토리 입력과 StartRaidDemo·F5/E/F6 루프도 확인한다.
동시에 다른 화면 또는 Raid가 열려 있으면 개발용 커스터마이징 진입을 허용하지 않는다.
기술적 C0~C2 구현·자동 검증과 C3 실제 에셋 수용·사용자 PIE 수용은 별도 상태다.

최종 인계 시 검증용 UnrealEditor/UnrealEditor-Cmd 프로세스는 0개였다. 구현·자동 검증·self-review·독립 C++ 검토는
C0~C2 범위에서 완료했다. C3 에셋 시험과 사용자 직접 PIE·결과 수용은 대기다.
독립 reviewer 역할/model 요청은 cpp-reviewer / GPT-6.1 Sol / xhigh이며 실제 실행 metadata는 확인 불가다.

## 사용자 확인과 C3 최소 제작 기록 — 2026-10-04

- 사용자 **“적용 완료했고 다음 단계 알려줘”**, 이어서 **“1번은 확인됐어 2번으로 넘어가자”**를 보고했다.
  직전 안내의 1번은 재개방·초기화 후 취소·Back의 직접 PIE 확인이었다. C0~C2 사용자 확인을
  **사용자 보고 기준 PASS/수용**으로 갱신한다. AI가 사람의 키·마우스 조작을 직접 관찰한 결과는 아니다.
- 이번 2번은 body 1개·공통 rig의 파츠 A/B·공통 Color material·실제 Shape key의 **최소 시험 에셋 제작**이다.
  Risk LOW / main 직접 수행: 한 세트의 형태·원본·export 규약이 결합돼 있어 제작과 검증을 같은 경로에서 수행했다.
  기존 portable Blender 5.2.2 LTS를 새 background 프로세스로 사용했다. 설치·MCP 연결·다른 AssetLab 장면 변경은 없다.
- 경로는 `Art/CustomizationPrototype/`이다. 편집 가능한 `CustomizationPrototype.blend`, FBX 3개,
  비교 `Preview.png`, 제작/export/검사 script와 README를 추가했다. .blend는 이 폴더의 LFS 속성을 선언했고
  FBX/PNG는 기존 root LFS 규칙을 사용한다. 기존 untracked 원문과 C0~C2 Source·Content는 보존한다.
- 형태는 구/타원체 조합의 자체 제작 오리와 중앙 깃 A/양쪽 깃 B다. 원작·회사 에셋을 복사하지 않았다.
  사용자 **“duckov도 다리가 몸체사이에 떠있어 다리가 없어도 괜찮아”**에 따라 연결 다리 없이 발을 분리했다.
  연결 다리 시험본은 Saved에 보존하고 최종 원본/export에서는 제외했다. 처음 원본의 hash도 보존했다.
- 공통 bone hierarchy는 root/body/head/wing_l/wing_r, bind pose와 발밑 pivot을 통일했다.
  세 FBX에 모든 bone을 포함한다. A/B는 head weight=1, object transform identity, 슬롯 0 공통 material,
  Shape key weight [0,1]·기본 0.5다. 약 13.22/13.92 cm의 **실제 vertex deformation**이며 scale 대체가 아니다.
  Blender RGB node Color는 연결돼 있으나 UE vector parameter material은 아직 제작/import하지 않았다.
  mask·animation·LOD·collision은 이번 최소 제작물에 없다.
- 선택: 공통 rig 전체와 개별 mesh를 FBX 3개로 export했다. 하나의 통합 FBX 대안은 개별 파츠 import/reimport와
  동일 Skeleton 선택을 명시적으로 검사하기 어려워 사용하지 않았다. body의 눈·부리/발 보조 material은 고정색이며
  A/B의 변경 계약은 슬롯 0의 공통 Color다. source 단위/축·FBX 옵션·다음 importer 절차는 README에 기록했다.

| C3 제작 단계 검사 | 현재 결과 |
| --- | --- |
| 저장 원본 fresh-process 재열기·rig/weight/UV/Shape 검사 | PASS, verify-final.log / verify.json |
| body/A/B FBX export | PASS, export-final.log / export.json, camera/light 제외 |
| FBX 3개를 새 Blender scene으로 각각 재읽기 | PASS, roundtrip-final.log / roundtrip.json. 5 bones·bind heads·cm bounds·slots·A/B Shape와 weight 0.5 보존 |
| 복사본의 A Shape 정점 +2 cm, 저장·재열기·재export·FBX 재읽기 | PASS, edit-probe-final.log / edit-probe.json. 최종 원본/export는 건드리지 않음 |
| A/B × Shape -1/0/1 렌더 6개 | PASS, render-final.log / render.json. main이 최신 Preview.png를 직접 화상 확인 |
| process 완료 | 최종 verify/export/roundtrip/edit-probe/render 모두 exit 0과 실제 C3 mode: PASS 확인 |
| UE Build/Automation 재실행 | not_run. 이번 delta는 제작 파일/문서뿐이며 C++·Content 변경 없음. 이전 92개 PASS를 새 skeletal 에셋 증거로 사용하지 않음 |
| 실제 UE import/동일 Skeleton asset/Color MID/Morph/mask·실패 보존 | not_run, 다음 3번 연결·검증 단계. Blender FBX 왕복은 UE import 성공 증거가 아님 |
| self-review | 리뷰 완료, 이슈 없음. 최종 원본·script·export·최신 preview와 이번 baseline delta를 확인 |
| scope/secret·기존 변경 보존 | PASS. 제작 파일 9개, 기존 변경은 두 문서 append만, Source/Content 90개 hash 불일치 0, 원본/문서 prefix 보존, 예상 밖·secret 규칙 일치·staged 0 |
| 독립 code-reviewer | PASS — 리뷰 완료, 이슈 없음. fresh context에서 제작 9개·script·문서 append·최종 증거·preview를 직접 검토. 원본/manifest/Source·Content hash와 prefix/HEAD를 직접 대조. Blender/UE 재실행은 not_run |
| 새 형태의 사용자 결과 수용 | 수용. 2026-10-04 최종 A/B·Shape 비교 제시 후 아래 사용자 답변으로 최소 제작물 형태 수용. UE runtime 수용은 별도 |

최종 로그·JSON·manifest는 `Saved/Automation/CustomizationAssets/`에 있다. 초기 sandbox Blender 실행은
Windows 접근 거부(-1073741790)였으며 권한을 갖춘 새 process로 실제 작업을 실행했다. 설치나 보호 규칙을 바꾸지 않았다.
처음 source 이동은 파일 잠금으로 실패했고 원본을 보존한 채 별도 버전으로 시험했다. 사용자 정정 뒤 production 원본은
최초 hash 그대로이며 다리 추가본은 Saved에만 보존했다. 초기 제작의 Blender 6.0 예정 API deprecation 경고는
node 활성화 setter를 제거해 최종 실행 경로에 남지 않았다. 최신 검사에는 예외·실패가 없다.

**다음 3번**: disposable UE 5.7 import → body로 Skeleton 생성 → A/B import에 **같은 Skeleton asset** 선택 및
Morph Targets 활성화 → 슬롯 0 UE Color vector material/skeletal usage → fresh-process 재로드/같은 asset reimport →
body/파츠 preview 연결 → 실제 Morph/색상·A→B→A·반복 적용·실패 전후 pixels·대상별 MID 검사 → 사용자 PIE 확인.
mask 정상 효과·idle/움직임·LOD/clipping 시험은 실제 준비 범위에 따라 C3의 미검증 조건을 유지한다.
현재 OpenCustomizationPrototype은 계속 Cube/Sphere를 표시하며 제작만으로 실제 skeletal 적용 완료를 선언하지 않는다.
InventoryCore·Raid·저장 형식·GDD·INVENTORY_DESIGN은 이번 단계에서도 수정하지 않는다. branch 변경·commit·push는 없다.

독립 검토 요청은 code-reviewer / GPT-6.1 Sol / xhigh이며 실제 실행 model metadata는 확인 불가다.
reviewer는 process exit 0을 parent의 실제 도구 실행 기록으로 구분했고 로그의 PASS·빈 stderr는 직접 확인했다.
제작 단계의 기술적 완료와 UE 연결·runtime 검증, 새 제작물의 사용자 시각 수용은 별도 상태다.

### 최소 제작물 시각 수용 — 2026-10-04

최종 A/B × Shape -1/0/+1 비교 이미지를 제시한 뒤 사용자가
**"에셋 자체를 풍성하게 하고 싶은 생각은 없어 그냥 커스터마이징을 구현했다는 사실 자체가 중요한거라 그냥 이대로 가자"**고 답했다.
현재 몸체·파츠 A/B·Shape 변화·분리된 발의 **최소 시험 에셋 형태를 사용자 수용**으로 기록한다.
이번 목표는 에셋의 양이나 시각 고도화가 아니라 커스터마이징 동작·Model 책임·실패 보존을 실제로 증명하는 것이다.
이 목표를 위해 에셋 증량을 추가하지 않는다. 3번 UE 연결·실제 Morph/Color·실패 보존 검증과 사용자 PIE 수용은
제작물의 시각 수용과 구분하며 아직 not_run이다. 이번 응답 기록에서 source/export/code는 변경하지 않았다.

## C3 최소 UE 연결·실제 적용 검사 — 2026-10-04

사용자 “진행하자”로 후속 3번 UE 연결·검증을 승인했다. 이전 단계의 not_run 기록은 당시 상태로 보존한다.
현재 목표는 수용한 최소 에셋으로 커스터마이징의 상태 책임과 실패 보존을 증명하는 것이다.
에셋 증량·정식 캐릭터 제작·새 slot·animation·LOD 제작·저장/네트워크·지상 플레이 연결을 추가하지 않았다.

- **Risk LOW**: 가역적인 신규 로컬 시험 에셋과 DuckovLike 적용 경로만 변경한다. main이 import·UI·검증·통합을,
  native gameplay-programmer가 AppearanceComponent 두 파일의 skeletal 준비/교체를 순차 작성했다.
  파일 소유 인계 뒤 main이 public render API와 shader 준비·surface/Texture2D 계약을 보완했다. 같은 tree 병렬 writer는 없었다.
  별도 독립 cpp-reviewer가 최종 변경과 실제 증거를 읽기 전용으로 검토한다. 요청 role은 GPT-6.1 Sol/xhigh,
  실제 실행 model metadata는 확인 불가다. 선택 이유는 적용 수명/공개 순서를 분리 검토하고 통합 검증 책임은 main에 유지하기 위해서다.
- C0·C1은 기존 계약 그대로 완료다. **Model만 Profile/Draft/편집 상태를 소유**하며 mesh·Widget·MID를 갖지 않는다.
  C2의 개발용 화면은 이제 Cube/Sphere 대신 실제 body와 A/B를 표시한다. CommonUI·MVVM·focus/Back·구독/preview 수명을 재사용한다.
- 적용 Component는 Profile 복사본을 보관하지 않는다. 후보의 ID/종류/유한 수/범위와 mesh·동일 Skeleton·LOD0 Morph/render buffers·
  surface material/usage/Color·Texture2D mask를 검사한다. shader 준비까지 마친 숨긴 body/part와 각각의 MID를 동기 평가한다.
  두 후보가 등록·reference pose·실제 Morph weight 검사를 통과한 뒤 표시를 교체하고, Model은 성공 뒤 Draft/Profile과 이벤트를 공개한다.
  실패는 이전 body/part/MID 및 확정 값/직전 Draft를 보존한다. 동일 표현은 기존 pointer를 유지하고 중복 상태 이벤트를 내지 않는다.
- 선택: 변경마다 숨긴 skeletal body/part 쌍을 준비해 공개한다. 기존 component를 직접 수정하는 대안은 최종 준비 실패 시
  표시 외형 복원을 요구하므로 사용하지 않았다. 작은 prototype에서 추가 할당/첫 shader 동기 대기를 허용하며 성능 개선을 주장하지 않는다.
  skeletal scale=1, reference pose/LOD0만 사용하고 상시 Tick/capture polling을 추가하지 않았다.

실제 프로젝트의 plugin/config는 변경하지 않았다. disposable Python-enabled UE 5.7 pilot에 FbxFactory로 import한
7개 prototype package와 2개 검사용 package만 Content/Customization에 추가했다. import의 초기 material 슬롯 struct 쓰기 실패와
비공개 Python property 접근 실패를 수정하고 PilotFinalizeFixed/PilotFixtures의 PASS·exit 0을 확인했다.
시험용 다른 Skeleton/8×8 mask는 정상 catalog에 노출하지 않는다. 원본 .blend·FBX·preview의 byte hash는 보존했다.

| 검사와 실제 대상 | 상태·증거 |
| --- | --- |
| Editor / Game Win64 Development Build | 각각 exit 0, **Result: Succeeded**, EditorBuild-final.log / GameBuild-final.log / BuildProcess.json |
| 같은 실제 asset reimport·복원 + fresh reload Verify | **2 succeeded / 0 failed / 0 notRun**, ReimportReport/index.json. A Shape 최대 Z 13.222221 → 15.222221 → 13.222221 cm, 같은 UObject/Skeleton/material 유지 |
| 새 process에서 UI만 실행 | **1 succeeded / 0 failed / 0 notRun**, FreshUIFixed/index.json, 실제 D3D12 SM6. shader 준비 보완 후·최종 mask 종류 보완 전의 UI 단독 증거이며 아래 최종 회귀로 갱신 검사 |
| 최종 Duckov.+InventoryAssets.Verify+CustomizationAssets.Verify | **94 succeeded / 0 failed / 0 notRun / 0 inProcess / 경고 0**, FinalVerifiedReport/index.json·FinalVerifiedProcess.json exit 0. 기존 92개 경로 누락 0, 기존 inventory/Raid/asset 85개 포함 |
| 실제 skeletal 적용 계약 | body/A/B의 동일 Skeleton 참조·실제 Morph delta/weight·scale=1, 실패 사유와 세 값/body/part/MID 보존·재시도·GC·대상별 MID 검사 |
| 실제 RHI 표시 검사 | 정상 material의 A Shape 최소/중간/최대, B, mask 효과/neutral·A/B/A 정확 복원, 실패 전후 pixel 동일, 두 대상 색상 pixel 독립. 최신 Visual PNG를 main이 직접 화상 확인 |
| 테스트 대역 | 기존 Model callback 실패 검사는 대역, engine Cube/Sphere 검사는 명시한 static fixture다. 실제 Morph 지원 증거는 새 skeletal/RHI 검사다. 다른 Skeleton·mask는 실제 UE 검사용 asset |
| 기존 변경·범위 | scope/secret PASS: 예상 밖 delta·규칙 일치·hash mismatch 0, 기존 문서 byte prefix 보존. 기존 89개 runtime 파일·원본/exports 보존, Source/Content 101개 최신 hash 기록. GDD/INVENTORY_DESIGN 미수정. branch main/HEAD 유지, staged 0, commit/push 없음 |
| self-review | 리뷰 완료, 이슈 없음. main이 상태·공개 순서·수명·실제 delta/PNG 확인 후 surface/Texture2D 규약을 보완하고 최종 Build/94개 검사로 재검증 |
| 독립 cpp-reviewer | **PASS — 리뷰 완료, 이슈 없음.** C++ delta 9개·Model/VM/controller 호출부·문서 3개·최신 Build/94개 report·PNG 12개·101개 hash/89개 기존 runtime 보호를 직접 확인. UI 증거 시점 문장 수정 후 재확인. reviewer의 Build/runtime 재실행은 not_run |

최초 Build의 private render API 오류와 deprecated compiler manager는 public GetResourceForRendering/SkinnedAssetCompiler로 고쳤다.
UI 검사 확장의 함수명 오기는 기존 SelectPart API로 고쳐 Build를 재실행했다. 첫 캡처의 fallback 체크무늬를 확인한 뒤
material EnsureIsComplete를 준비 경로에 넣었다. UI 단독 첫 실행은 UE CEF의 BUseSupportedRHIRenderer assertion(exit 3)으로
중단돼 PASS로 사용하지 않는다. 브라우저를 쓰지 않는 자동 검사 process에 공식 **-nocef**를 지정해 재실행했다.
프로젝트 설정·보호 규칙을 바꾸지 않았고 실제 RHI rendering은 유지한다. 최종 로그·report·hash는
Saved/Automation/CustomizationSkeletal, 엔진 runtime 로그는 Saved/Logs/CustomizationSkeletal*.log에 있다.

### 현재 에셋 규약·남은 사람 확인

공통 Skeleton은 SK_CustomizationBody_Skeleton, 5 bones·동일 bind pose·발밑 pivot/cm 단위다.
body/A/B component translation/rotation=0, scale=1이다. 슬롯 0 surface material의 **Color** vector와 skeletal/Morph usage가 필수다.
Hue [0,1] 기본 0.12, S=0.7/V=0.9의 linear RGB를 body/part별 MID에 적용한다. body의 보조 슬롯 1/2는 고정색이다.
A/B는 **Shape** Morph가 필수며 Shape [-1,1] 기본 0, weight=(Shape+1)/2다. 파츠 ID A/B만 지원하고 NAME_None은
파츠 없음으로서 필수 슬롯에서 거부한다. mask는 Texture2D **ColorMask**, 흰색 neutral이며 정상 catalog는 선택 mask 미지정이다.
필수 mask 없음/잘못된 종류는 실패하고 선택 mask 없음은 흰색으로 복원한다. 상세 import/제작 규약은 Art README에 있다.

reference pose·LOD0의 기술 검사와 Blender 최소 형태 수용은 구분한다. animation/움직이는 skeleton·추가 LOD·게임 캐릭터
부착/clipping·packaged build는 **not_run**이며 이번 최소 prototype 범위 밖이다. 새 UE 화면의 사용자 직접 PIE 수용은 **대기**다.
도구의 자동 PIE/delegate 입력과 main의 PNG 관찰을 사람의 키·마우스 수용으로 기록하지 않는다.

사용자 PIE: L_InventoryDemo → console OpenCustomizationPrototype → console 닫기 → A/B 교체와 색상/형상 슬라이더 확인 →
개발용 실패 버튼에서 선택/값/외형 보존과 사유 확인 → 정상 편집·적용 → 같은 명령 재개방해 확정 값 확인 →
초기화 뒤 취소 또는 Escape → 재개방해 확정 값 복원 확인 → 초기화·적용·재개방해 기본값 확인.
마지막으로 닫은 뒤 I 인벤토리와 StartRaidDemo/F5/E/F6 기존 루프를 확인한다. 검증용 Editor는 종료한 상태로 인계한다.

최종 상태: C0~C2 구현과 최소 C3의 **reference pose/LOD0 실제 에셋 연결·자동 검증·self-review·독립 C++ 검토 완료**.
최소 제작물의 형태 수용은 기존 사용자 보고로 유지한다. 새 UE 화면의 사람 PIE·결과 수용은 대기이며,
animation/추가 LOD/packaged runtime과 정식 에셋 증량은 완료 범위에 포함하지 않는다. 인계 시 검증용 Editor/Cmd process는 0개였다.

### 새 UE 화면 직접 PIE 확인 준비 — 2026-10-05

사용자가 다음 단계로 **“아직 — 직접 PIE 확인 진행”**을 선택했다. Risk LOW/main 직접 경로이며
Source/Content 101개를 기존 최종 manifest와 대조해 hash 불일치 0을 확인했다. 2026-10-04의 Editor/Game Build 성공과
Automation 94 succeeded/0 failed/0 notRun 기록을 확인했으며 이번 준비에서 재실행한 검사로 표시하지 않는다.
확인용 Editor를 L_InventoryDemo map과 process 옵션 -nocef로 열고 **PIE 미시작** 상태로 인계한다.
프로젝트 설정·코드·에셋은 변경하지 않는다. 새 UI의 직접 입력/결과 수용은 아직 대기다.
확인 순서는 A/B·색상/Shape·실패 보존 → 적용/재개방 → 초기화/취소·Back/재개방이며,
초기화 후 적용하면 기본값이 확정되는 것까지 확인한다. 사용자의 실제 결과 보고 뒤 수용 상태를 갱신한다.

### 새 UE 화면 사용자 직접 PIE 수용 — 2026-10-05

바로 앞 직접 PIE 안내에 대해 사용자가 **“검증 완료했어.”**라고 보고했다. 안내한 다음 항목을
**사용자 보고 기준 PASS/수용**으로 기록한다. AI가 사용자의 키·마우스 조작을 직접 관찰한 결과는 아니다.

- 같은 PIE 세션의 L_InventoryDemo에서 OpenCustomizationPrototype 진입.
- 실제 A/B·색상·Shape 변경 및 개발용 실패 버튼의 직전 선택/값/표시 외형 보존과 실패 안내.
- 정상 편집·적용 후 재개방 시 확정 값/외형 일치.
- 초기화 후 취소 또는 Escape·재개방 시 마지막 확정 값 복원.
- 초기화·적용·재개방 시 기본값 확정.

현재 완료 범위는 **C0~C2와 최소 C3 reference pose/LOD0의 구현·자동 검증·독립 C++ 검토·사용자 직접 PIE 수용**이다.
인벤토리/Raid의 자동 회귀 기록과 이번 다섯 항목의 사람 수용을 구분하며, 별도 인벤토리/Raid 수동 확인을 추정하지 않는다.
animation·추가 LOD·packaged runtime·정식 에셋 증량·지상 플레이 연결은 기존 범위 밖/not_run 상태를 유지한다.
이번 갱신은 계획/worklog의 수용 기록만 변경한다. Build/Automation·독립 리뷰를 다시 실행한 것으로 표시하지 않으며,
코드·에셋·설정·기존 변경을 보존하고 branch 변경·commit·push는 수행하지 않는다.


### 커밋·push 전달 승인 및 재검증 — 2026-10-05

사용자 **“커밋 푸쉬 하자”**로 수용된 커스터마이징 결과의 commit/push를 명시 승인했다.
Risk LOW/main 직접 경로다. 현재 main에서 index·LFS·원본 보존을 순차 확인하는 작은 전달 작업이므로
새 구현 위임을 하지 않았다. branch 변경·history rewrite 없이 기존 상태/UI, 실제 에셋 연결, 문서를 세 논리 단위로 나눈다.
전달 전 변경 38개를 Saved/Automation/CustomizationDelivery/final에 byte hash로 검증해 보존했다.

- C0~C2 commit **4e7ea27**: 검토된 기존 16개 Source 버전을 복원해 Editor/Game 각각 **Result: Succeeded**,
  Duckov.+InventoryAssets.Verify **92 succeeded / 0 failed / 0 notRun / 경고 0**을 실제 재실행했다.
- 최소 C3 commit **a9dfd8a**: 최종 38개 hash를 모두 복원한 뒤 Editor/Game 각각 **Result: Succeeded**,
  Duckov.+InventoryAssets.Verify+CustomizationAssets.Verify **94 succeeded / 0 failed / 0 notRun / 경고 0**을 실제 재실행했다.
  이전 최종 94개 test path가 모두 일치하며 실패 후 상태/표시 보존·재시도, 실제 Morph/RHI·색상 독립과 기존 회귀를 포함한다.
- self-review: 리뷰 완료, 이슈 없음. 전달 delta는 수용한 파일만 포함하며 InventoryCore·Raid·Config·저장 형식·읽기 전용 문서는 보존한다.
  secret 규칙 일치 0, git lfs fsck OK이며 Blender/FBX/PNG/uasset 14개가 LFS pointer로 등록됐다.
- 독립 C++ 검토는 앞선 실제 PASS를 유지한다. 검토된 Source/Content **101개 hash 불일치 0**으로
  새 코드 변경이 없음을 확인했으며, 전달 중 독립 리뷰를 재실행한 것으로 표시하지 않는다.
- 사용자 직접 PIE 수용은 바로 앞 2026-10-05 보고를 유지한다. animation·추가 LOD·packaged runtime은 기존 범위 밖/not_run이다.

최신 전달용 Build·Automation 증거는 Saved/Automation/CustomizationDelivery의 Stage1/Stage2 로그·report에 있다.
문서 커밋과 원격 push의 실제 SHA·동기화·clean 상태는 완료 보고에서 확인한다. 과거 commit/push 없음 기록은 당시 상태로 보존한다.


## 부위별 최소 조절 확장 — 2026-10-07

사용자의 “눈, 입, 몸통 길이 같은 것도 조절” 요청을 기존 최소 prototype에 연결했다.
AI가 첫 범위를 눈 크기·입/부리 길이·몸통 앞뒤 길이의 연속 슬라이더 3개로 선택했다.
이는 7개 탭/정식 에셋 제작 단계 전체의 완료를 뜻하지 않는다. 기존 머리 A/B·Hue·Shape는 유지한다.

- **Risk LOW / 경로**: 가역적인 로컬 데이터·UI·기존 body Morph 확장이다. active autonomy-policy/agent-routing,
  asset-authoring/verification-loop를 적용했다. main이 계약·통합·검증을 책임지고 gameplay-programmer의 C++ 12개,
  tools-programmer의 Art 5개·Content 3개를 소유권 인계하며 순차 작성했다. 동일 tree 병렬 writer는 없었다.
  main은 실패 slider 표시 복원·Skeleton 기준 자세 검사·하단 focus 검사를 보완했다. 독립 cpp-reviewer를 별도 호출했다.
- **최소 계약**: EyeSize/BeakLength/BodyLength 모두 [-1,1], 기본 0이다. 0은 이전 body Basis 그대로다.
  각 음수/양수 Morph weight는 max(-value,0)/max(value,0)이다. ID A/B·필수 슬롯·Hue/Shape·mask 규약은 기존과 같다.

| 값 | 실제 body Morph | 제작 기준 |
| --- | --- | --- |
| EyeSize | EyeSmall / EyeLarge | 양 눈 고정 중심의 크기 0.65~1.5배 |
| BeakLength | BeakShort / BeakLong | 입/부리 뒤 anchor에서 앞뒤 길이 0.65~1.5배 |
| BodyLength | BodyShort / BodyLong | 몸통·날개 앞뒤 길이 0.75~1.35배 |

**상태·적용 책임**: Model만 Profile/Draft/편집 상태를 소유한다. UI/VM은 입력 후보를 만들며
기존 메모리 적용·초기화·취소·Back·재개방 경로에 세 값을 함께 전달한다. A/B 교체도 모든 값을 유지한다.
입력의 NaN/Infinity/범위와 body 필수 Morph 6개·CPU/GPU 데이터·공통 Skeleton·material usage/shader를 검사하고,
숨긴 body/part에서 모든 weight와 기준 자세를 준비한 뒤 외형, Draft, 이벤트 순서로 공개한다.
동일 Profile fast path도 body weight 전체를 비교한다. 실패한 편집/최종 적용/초기화/취소는 확정 값·직전 Draft·표시를 보존한다.
Slider 자체 Value가 delegate 전에 변경되는 경우에는 UI만 Model의 정상 값으로 즉시 되돌린다.
실패·동일 상태의 중복 Model 이벤트는 없으며, 공유 material의 runtime 변경이나 원본 상태 복사본을 추가하지 않았다.

**선택과 대안**: 기존 body에 독립된 양방향 Morph pair를 사용했다. bone/object scale 대안은 머리·다른 부위까지
영향을 줄 수 있어 채택하지 않았다. component scale은 1이고 실제 정점 delta/렌더 변화로 검증한다.
조절 항목은 520 높이 ScrollBox로 묶고 포커스 이동 시 해당 항목을 표시한다. 실패 안내는 preview 아래에 고정한다.
상시 UI polling은 없으며, 동기 shader 준비/후보 교체 비용을 허용하는 prototype이다. 성능 개선을 주장하지 않는다.

### 실제 실행·검토 증거

아래 최신 증거는 Saved/Automation/CustomizationRegions, 엔진 로그는 Saved/Logs/CustomizationRegions*.log에 있다.
원본 .blend의 Basis/UV/rig/bone weight/A/B signature, A/B FBX, 공통 Skeleton과 다른 Content는 보존했다.

| 검사 | 실제 결과 |
| --- | --- |
| Blender 저장·재개방·원본 보존·비대상 정점·대칭·FBX 왕복 | PASS, verify/export/roundtrip/edit-probe/render JSON 및 checked 로그, 실제 비교 렌더 9장 |
| 같은 production body asset 수정 재import·원본 복원 + Verify | 2 succeeded / 0 failed / 0 notRun / 경고 0, ReimportReport/index.json, process exit 0 |
| 위 실제 수정 반영 | EyeLarge 최대 delta 길이 1.75 → 3.75 → 1.75 cm, 동일 UObject/Skeleton/material 참조 보존, reimport-body.json |
| Editor/Game Win64 Development | 각각 **Result: Succeeded**, exit 0, EditorBuild-scroll.log / GameBuild-scroll.log / BuildScrollProcess.json |
| 최종 Duckov.+InventoryAssets.Verify+CustomizationAssets.Verify | **94 succeeded / 0 failed / 0 notRun / 0 inProcess / 경고 0**, VerifiedReport/index.json·VerifiedProcess.json exit 0 |
| 기존 회귀 | 이전 94개 test path 누락 0, 인벤토리/Raid/asset 85개 포함, test-path-comparison.json |
| 실제 에셋·D3D12 SM6 | body Morph 6개 delta/weight/active render, body와 공통 Skeleton의 bone 이름·parent·ref transform 일치, 대상별 색상/MID/형상 독립 PASS |
| 상태·실패 보존 | 정상 Draft 편집의 확정 불변, 입력/필수 Morph/리소스/최종 적용 실패 후 세 상태·component/MID 보존, 복구 후 정상 적용 PASS |
| 반복·UI 수명 | -1/0/+1·복합값·A/B/A·반복 적용 잔류/중복 이벤트 없음, 적용/초기화/취소/Back/재개방·preview 폐기·GC PASS |
| 실제 화면 | 각 부위 최소/중립/최대 pixels 변화, 실패 전후 pixels 동일, 슬라이더 실패 복원, 하단 focus 시 동작 버튼 4개 표시 PASS. main이 Visual PNG를 직접 확인 |
| self-review | **리뷰 완료, 이슈 없음** — 상태/표시 공개·UObject/MID·UI 실패 복원·범위·실제 렌더 증거 확인 |
| 독립 cpp-reviewer | **PASS — 리뷰 완료, 이슈 없음.** fresh context로 C++ 12개·호출부·Art script/README·문서 append·Build/report·PNG를 확인. runtime/보호 파일 각 101개 hash 불일치 0을 독립 대조. reviewer의 build/runtime 재실행은 not_run |
| 이번 확장의 사용자 PIE 수용 | **대기/not_run**. 이전 A/B·Hue·Shape의 사용자 수용을 새 3개 조절의 수용으로 간주하지 않는다 |

Model callback 실패는 테스트 대역이며 static fixture는 새 부위 조절을 UnsupportedBodyAdjustment로 거부한다.
실제 Morph 지원 증거는 위 skeletal/RHI 검사다. Morph 누락 시험은 같은 Skeleton의 A mesh를 body 준비에 주입한다.
처음 Blender sandbox 실행은 접근 거부였고 권한을 갖춘 별도 process로 실행했다. Basis value/대칭 rounding 검사와
pilot Unreal.Array JSON 변환 오류는 수정·재실행하여 PASS를 확보했다. 최초 실패 로그는 남겨 두었다.
pilot body 재import에는 기존 Shape FBX에서도 보였던 BindPose warning 1건이 있었다.
fresh pilot 재로드는 0 errors/0 warnings, production Automation은 경고 0이며 ref bone pose 동등도 직접 검증했다.

범위/secret 검사에서 예상 밖 변경·규칙 일치·보호 대상 101개 hash 불일치 0, 기존 계획/worklog/Art README byte prefix 보존이다.
최종 Source/Content 101개 hash를 tested-source-content.json으로 기록했다. InventoryCore·Raid·저장 형식·Config·GDD·
INVENTORY_DESIGN은 미수정이다. 현재 main/HEAD 45f23a5/staged 0을 유지하며 이번 확장의 commit/push는 수행하지 않는다.

### 제작 계약과 인계

공통 SK_CustomizationBody_Skeleton 5 bones/동일 ref pose·cm 단위·원래 pivot을 유지한다.
body의 위 Morph 6개와 A/B Shape는 LOD0 필수다. body 슬롯 0의 Color/ColorMask, 슬롯 1/2의 고정색은 유지하되
눈·부리 material에도 Skeletal Mesh/Morph Targets usage가 필요하다. 선택 mask 미지정은 흰색 neutral로 복원한다.
직접 편집 가능한 .blend, body-only export 스크립트 author_regions.py, 비교표 RegionsPreview.png와 상세 규약은 Art README에 있다.

기술적 구현·실제 자동 검증과 새 화면의 사람 수용은 구분한다. reference pose/LOD0만 검증했고 animation/추가 LOD/
packaged runtime·정식 에셋 증량·지상 플레이 연결·저장/네트워크는 not_run/범위 밖이다.
검증용 Editor는 종료한 상태로 인계한다. 사용자 직접 확인 순서는 L_InventoryDemo PIE → OpenCustomizationPrototype →
눈·입·몸통 슬라이더를 각각/함께 조절 → A/B 교체 시 값 유지 → 아래로 스크롤해 적용 → 재개방 값 일치 →
초기화 후 취소 또는 Escape → 재개방 확정 값 복원 → 개발용 실패 시험에서 값/외형 보존과 사유 확인이다.
초기화 후 적용·재개방하면 새 세 값은 0으로 확정되어야 한다. 이번 변경은 로컬 구현 완료 범위이며 사람 수용은 별도로 받는다.


### 부위별 조절 사용자 수용·Git 전달 — 2026-10-10

사용자가 **“다 확인됐어. 커밋 푸쉬 진행하고 진행상황 업데이트 하자”**고 보고했다.
새 눈 크기·입/부리 길이·몸통 앞뒤 길이의 직접 PIE 확인을 **사용자 보고 PASS/수용**으로 기록한다.
안내한 미리보기·A/B 값 유지·적용/재개방·초기화/취소·Back·실패 보존의 수용이며, AI의 사람 입력 직접 관찰과 구분한다.

**현재 진행상황**: C0~C2와 최소 C3에 더해 부위별 3개 조절의 구현·실제 에셋 자동 검증·사용자 수용을 완료했다.
기존 기능의 독립 C++ PASS를 유지하며, 전달 중 발견한 테스트 컴파일 수정도 별도 재검토 PASS를 받았다.
7개 탭 전체·정식 에셋 증량·animation/추가 LOD/packaged·지상 플레이 연결·저장/네트워크는 이번 완료 범위가 아니다.

- **Risk LOW / 선택 경로**: main이 보존 검사·문서·Git 전달을 담당한다. 실제 compile 실패 후 cpp-build-resolver에
  테스트 2개 파일만 순차 인계했고 main이 다시 통합 검증한다. fresh-context cpp-reviewer가 해당 수정과 증거를 재검토한다.
  active autonomy-policy/agent-routing 및 verification-loop를 적용하며 동일 tree 병렬 writer는 없다.
- 전달 전 main/HEAD 45f23a5와 origin/main 일치, staged 0, 수용 범위 22개를 확인했다.
  변경 22개를 Saved/Automation/CustomizationRegionsDelivery/baseline에 hash 검증 사본으로 보존했다.
- 기능 commit **227a99f**: 눈·부리·몸통 조절 코드와 필수 Morph/원본 에셋 20개를 묶었다.
  코드와 body 리소스를 분리하면 필수 Morph 계약을 만족하지 못하므로 한 기능 단위로 전달했다.
  해당 커밋 전 Editor/Game Build는 Target is up to date 및 **Result: Succeeded**, exit 0이었다.

**전달 중 발견·수정한 빌드 실패**: 문서 커밋 전 재빌드는 열린 Editor의 Live Coding으로 차단됐다.
사용자가 저장 후 Editor 종료를 확인했고, 이후 Unreal의 Unity Build에서 테스트 Flags 모호성(C2872)과
BodyMorphs 이름 숨김(C4459)이 드러났다. Unity Build는 UnrealBuildTool이 여러 C++ 파일을 묶는 컴파일 방식이다.
수정 파일이 개별 컴파일되던 앞선 검증으로는 이 경로를 잡지 못했다. 실패 로그는 DocsEditorBuild.log와
DocsEditorBuild-retry.log에 보존한다. 성공으로 덮어쓰지 않는다.

CustomizationTests.cpp와 CustomizationEditorAssetsTests.cpp의 테스트를 각 고유 namespace 안에 유지하고,
후자의 지역 배열만 RequiredBodyMorphs로 바꿨다. 테스트 등록 9개와 검사 본문은 동일하다.
게임 동작·에셋·설정은 변경하지 않았다. 검토된 runtime 101개 중 **99개 hash 동일**, 2개 차이는 이 테스트 수정뿐이다.

| 최종 전달 검증 | 실제 결과 |
| --- | --- |
| Editor 전체 묶음 컴파일, -DisableAdaptiveUnity | **Result: Succeeded**, exit 0, 실제 Module.DuckovLike.2.cpp compile, UnityEditorBuild.log |
| 표준 Editor/Game Win64 Development | 각각 **Result: Succeeded**, exit 0, FixedEditorBuild.log / FixedGameBuild.log / FixedBuildProcess.json |
| 수정 후 새 process 전체 Automation | **94 succeeded / 0 failed / 0 notRun / 0 inProcess / 테스트 경고 0**, FinalReport/index.json·FinalProcess.json exit 0 |
| 기존 테스트·범위 보존 | 이전 94개 path 누락 0, 기존 인벤토리/Raid/asset 85개 포함. 테스트 2개 외 수용 코드·에셋 불변 |
| body 같은 asset 재import | 기존 2026-10-07의 2/0/0 및 1.75→3.75→1.75 cm 증거 유지. 이번 전달에서 mutating reimport는 재실행하지 않음 |
| self-review | **리뷰 완료, 이슈 없음** — 최소 이름 범위 수정·실제 diff·재실행 검사·LFS/secret·원본/문서 보존 |
| 수정분 독립 cpp-reviewer | **PASS — 리뷰 완료, 이슈 없음.** fresh context로 테스트 2개·등록 9개·UE macro 수명·3 Build·새 94개 Automation·hash/문서 증거 직접 확인. reviewer의 runtime 재실행은 not_run |

테스트 경고 0은 Automation report의 카운터다. 엔진 시작 시 기존 Slate/VisionOS 이미지·EditorPerf 경고와 구분한다.
최신 Source/Content 101개 hash는 fixed-runtime.json, 보존/secret 증거는 verify-*.json에 있다.
InventoryCore·Raid·저장 형식·Config·GDD·INVENTORY_DESIGN과 보호 대상 101개 파일을 보존한다.
사용자 수용은 게임 동작을 바꾸지 않는 테스트 컴파일 수정 뒤에도 유지하며 새 수용을 만들어내지 않는다.

테스트 compile 수정은 **29801ea**로 커밋했고, 계획/worklog 기록은 별도 문서 커밋으로 전달한다.
현재 main에서 commit/push하며 branch 변경·history rewrite는 하지 않는다. 각 실제 SHA와 원격 push·동기화·clean 상태는
완료 보고에서 확인한다. 검증용 process는 테스트 종료 후 닫으며, 사용자 Editor는 사용자가 저장하고 종료했다.
