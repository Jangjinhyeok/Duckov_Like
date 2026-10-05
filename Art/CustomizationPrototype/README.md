# C3 최소 시험 에셋

2026-10-04, 사용자 요청의 2번 **제작 단계** 결과다. 정식 캐릭터 제작이나 UE preview 교체를 포함하지 않는다.
구와 타원체를 조합한 자체 제작 오리 시험 모델이며 외부·회사 mesh/material/texture를 복사하지 않았다.
사용자가 다리 없는 형태를 허용했으므로 발은 몸체에서 떨어져 있다. 연결 다리가 있는 시험본은
`Saved/Automation/CustomizationAssets/Source_with_legs.blend`에 보존하고 최종 결과에서는 제외했다.

## 원본과 계약

- 편집 원본: `CustomizationPrototype.blend`. mesh·UV·material node·공통 armature·vertex weight·Basis/Shape key를 보존한다.
  저장된 기본 표시는 A, Shape weight 0.5다. B는 viewport/render에서 숨겨져 있으며 Outliner에서 켤 수 있다.
- `Exports/SK_CustomizationBody.fbx`: body 1개, 1,998 vertices. material 슬롯 0=공통 Color, 1=부리/발, 2=눈.
- `Exports/SK_CustomizationPartA.fbx`: 중앙 세 갈래 머리 깃, 888 vertices. 슬롯 0만 사용한다.
- `Exports/SK_CustomizationPartB.fbx`: 양쪽 두 갈래 머리 깃, 666 vertices. 슬롯 0만 사용한다.
- 공통 rig: `root -> body -> head`, `body -> wing_l/wing_r`, 총 5 bones. 세 FBX 모두 전체 hierarchy와 같은 bind pose를 포함한다.
  A/B의 모든 정점은 `head`에 weight 1이다. body는 부위별 bone에 weight 1이며 부드러운 관절 deformation용 정식 weight가 아니다.
  애니메이션·LOD·collision을 제작하지 않았다.
- 단위: 1 Blender unit = 1 cm, metric scale 0.01. body 높이 91 cm, 폭 약 63 cm.
  source forward -Y / up +Z, 발밑 원점 (0,0,0), object translation/rotation=0, scale=1.
  engine의 변환 후 방향·bounds는 다음 import 단계에서 확인한다.
- A/B 모두 실제 `Shape` key를 가진다. Basis는 weight 0의 짧은 깃, weight 1은 긴 깃이다.
  UI의 Shape [-1,1]은 weight `(Shape + 1) / 2`로 매핑하므로 기본 Shape=0은 weight 0.5다.
  A 807 / B 585 vertices가 변하며 최대 정점 이동은 약 13.22 / 13.92 cm다. object scale 대체가 아니다.
- 공통 Blender material `M_CustomizationColor`는 RGB node **Color**가 Principled Base Color에 연결되어 있다.
  기본 linear RGBA는 Hue=0.12, S=0.7, V=0.9, A=1이다. body의 보조 material 2개는 고정색이다.
  **Blender node 이름은 UE의 vector parameter를 생성하지 않는다.** 다음 단계에서 UE surface material의
  슬롯 0용 vector parameter **Color**, skeletal usage와 대상별 MID를 실제로 만들어 검사해야 한다.
- 최소 제작물에는 mask가 없다. 다음 단계에서 mask를 시험한다면 UE texture parameter 이름을 descriptor와
  일치시키고 흰색이 neutral이 되도록 제작한다. required mask는 유효 참조 필수, optional 미지정은 WhiteSquareTexture다.
  mask 적용 성공과 교체 잔류 검사는 미실행이다.

## 재사용 명령과 export 설정

기존 portable **Blender 5.2.2 LTS, d13f752e3b9c**를 새 background 프로세스로 실행했다.
설치·add-on·기존 Blender 프로세스·preferences를 바꾸지 않았다. MCP 연결은 사용하지 않았다.
현재 머신의 executable은 `C:\Users\zero9\Documents\AssetLabs\DuckovMedkit\tools\blender-5.2.2-windows-x64\blender.exe`다.
그 AssetLab의 코드나 scene을 실행하지 않고 executable만 사용했다. 다른 머신에서는 해당 실행 파일 경로를 지정한다.

PowerShell에서 repo root 기준으로 실행한다. 기존 원본이 있으면 **create는 거부**되며 export/render는 저장된 원본을 읽는다.
사용자 편집을 재생성으로 덮지 말고 Blender에서 저장한 뒤 export한다.

```powershell
$blenderExe = '<설치된 Blender 5.2 실행 파일의 절대 경로>'
$assetScript = Join-Path $PWD 'Art/CustomizationPrototype/author_assets.py'
& $blenderExe --background --factory-startup --offline-mode --python-exit-code 1 --python $assetScript -- verify
& $blenderExe --background --factory-startup --offline-mode --python-exit-code 1 --python $assetScript -- export
& $blenderExe --background --factory-startup --offline-mode --python-exit-code 1 --python $assetScript -- roundtrip
& $blenderExe --background --factory-startup --offline-mode --python-exit-code 1 --python $assetScript -- render
& $blenderExe --background --factory-startup --offline-mode --python-exit-code 1 --python $assetScript -- edit-probe
```

export는 선택한 mesh 1개 + 공통 rig만 포함한다. camera/light는 제외한다.
FBX 7.4 binary, forward -Y/up Z, unit 적용, FBX_SCALE_UNITS, global scale 1,
Apply Modifiers=false(Shape key 보존), leaf bones=false, full bones, primary Y/secondary X,
face smoothing, animation=false, experimental bake transform=false다. source object/mesh/modifier는 변형하거나 저장하지 않는다.

`verify`는 최소 계약을 검사한다. 정식 제작에서 vertex 수·bones·material 슬롯·parameter 계약을 바꾸면
검사와 다음 단계 소비 코드를 함께 검토해야 한다. `edit-probe`는 별도 복사본에서 A의 Shape 정점 하나를
2 cm 움직여 저장·재열기·재export하고 FBX를 재읽어 같은 높이가 유지되는지 검사한다. production 원본과 exports는 보존한다.

## 실제 검사와 다음 import 단계

`Saved/Automation/CustomizationAssets/`의 최종 `*-final.log`, `verify.json`, `export.json`, `roundtrip.json`,
`edit-probe.json`, `render.json`에 증거가 있다. 원본 재열기·계약 검사·FBX 3개 왕복·편집 복사본 왕복은 PASS이며
각 Blender process exit 0과 실제 `C3 <mode>: PASS`를 확인했다. A/B × weight 0/0.5/1의 720×720 렌더 6개를 main이 화상 검사했다.
원본 파일과 최종 FBX/스크립트/preview hash는 `final-manifest.json`에 기록한다.

main self-review와 fresh-context 독립 `code-reviewer`는 **PASS — 리뷰 완료, 이슈 없음**이다.
reviewer는 제작 9개 파일과 baseline/hash·최신 preview를 직접 확인했으며 Blender/UE를 재실행하지 않았다.
기존 C++·Content 90개 hash와 문서 기존 내용은 보존됐다.
2026-10-04 사용자가 "에셋 자체를 풍성하게 하고 싶은 생각은 없어 그냥 커스터마이징을 구현했다는 사실 자체가 중요한거라 그냥 이대로 가자"고 답해
현재 최소 제작물의 형태를 수용했다. 이번 목표는 커스터마이징 동작·상태 소유·실패 보존의 구현 증명이며
에셋 증량이나 시각 고도화를 추가하지 않는다. UE 연결 후의 runtime 검증·사용자 PIE 수용은 별도다.

UE build/Automation 재실행, 실제 UE import/reimport·Morph deformation·공통 Skeleton asset·material/mask·두 대상 색상 독립은
이번 제작 단계에서 **not_run**이다. C0~C2의 과거 Build/92개 Automation PASS를 새 에셋의 검증으로 사용하지 않는다.

다음 단계는 disposable UE import 경로에서 body부터 Skeleton을 만든 뒤 A/B가 **동일 Skeleton asset 참조**를 사용하도록
import하고 Morph Targets를 켜는 것이다. FBX 왕복의 같은 bone 목록만으로 UE의 동일 Skeleton을 확인했다고 하지 않는다.
UE 5.7 importer의 실제 설정을 기록하고 치수·축·slot·Shape 범위·저장 후 fresh-process 재로드·**같은 asset reimport**를 검증한다.
이후 `Color` material을 연결하고 preview에 body/파츠를 연결해 실패 보존·A→B→A·두 대상 MID·capture 갱신을 검증한다.
현재 개발용 화면은 계속 engine Cube/Sphere를 사용한다. 애니메이션/LOD/clipping·mask 및 사용자 PIE 수용도 다음 단계 조건이다.

## UE 연결 후의 현재 규약 — 2026-10-04

위 제작 단계 기록은 보존한다. 후속 3번에서 실제 UE 에셋을 연결했다. 개발용 `OpenCustomizationPrototype`은
`Content/Customization/Prototype`의 body와 A/B를 사용한다. 원본 .blend·FBX·Blender preview는 변경하지 않았다.
body로 만든 `SK_CustomizationBody_Skeleton`을 A/B에 **동일 asset 참조**로 지정했다. 공통 bone은
root/body/head/wing_l/wing_r, body 높이는 UE에서도 91 cm이며 pivot은 발밑, component transform은 identity다.
이번 적용 경로는 reference pose/LOD0만 지원하며 animation·추가 LOD·게임 캐릭터 연결은 범위 밖이다.

UE 5.7의 disposable Python-enabled pilot에서 명시적 FbxFactory로 import했다. 실제 프로젝트의 plugin/config는
변경하지 않았다. 저장한 7개 신규 prototype package만 repo로 옮겼고 source metadata는 현재 Art/Exports로 정규화했다.
FBX 옵션은 Skeletal Mesh, Morph Targets=true, animation/material/texture/physics asset=false,
Convert Scene/Convert Scene Unit/Force Front X Axis=true, uniform scale=1, Import Normals,
Update Skeleton Reference Pose/Use T0 As Ref Pose=false다. body부터 import하고 A/B는 같은 Skeleton을 선택한다.

`M_CustomizationColor`는 surface material이며 Skeletal Mesh·Morph Targets usage를 사전 설정한다.
Vector **Color** RGB와 TextureSampleParameter2D **ColorMask** RGB를 곱해 Base Color로 연결하고 roughness=0.68이다.
body 슬롯 0과 A/B 슬롯 0에 공통 material을 연결한다. body 슬롯 1/2는 부리·발과 눈의 고정색 material이다.
runtime은 각 body/part에 별도 MID를 만들고 공유 asset의 parameter/usage를 변경하지 않는다.
첫 shader 준비도 성공 외형을 공개하기 전에 동기로 완료한다. 최초 개방에서 준비 시간이 들 수 있다.

A/B 모두 실제 **Shape** Morph, weight `(Shape + 1) / 2`, UI [-1,1], 기본 weight=0.5를 사용한다.
scale=1을 유지하며 tick 없이 reference pose와 Morph render weight를 변경 시 평가한다.
mask 계약은 **Texture2D**이며 흰색이 neutral이다. 정상 catalog는 mask 미지정, material 기본 WhiteSquareTexture를 사용한다.
필수 mask 없음·Cube 등 잘못된 종류는 실패한다. 선택 mask 미지정은 WhiteSquareTexture로 복원한다.
`Content/Customization/Tests`의 다른 Skeleton asset과 8×8 mask는 실패/잔류 검사 대역이며 정상 선택지에 노출하지 않는다.

같은 asset 재import는 별도 `CustomizationAssets.Reimport` 검사로 실행했다. `author_assets.py -- edit-probe`가 만든
Saved의 A 편집 복사 FBX를 적용해 최대 Morph Z가 13.222221 → 15.222221 cm로 변하고 원본 재import로 복원됐다.
Skeleton/material 참조와 UObject identity를 유지했고 production 원본 FBX는 변경하지 않았다.
전체 회귀는 `Duckov.+InventoryAssets.Verify+CustomizationAssets.Verify`를 사용하며 재import 쓰기 검사는 포함하지 않는다.
새 프로세스의 실제 D3D12 UI에서 Shape 세 값, mask/neutral, A/B/A 복원, 실패 전후 pixels, 다른 대상 색상 독립을 검사했다.
최종 Build/Automation·독립 C++ 검토 결과와 사람 확인 상태는 `docs/CUSTOMIZATION_PLAN.md`의 후속 UE 연결 기록이 기준이다.
Blender 제작물 형태는 사용자 수용됐고, 새 UE 화면의 사람 PIE 확인은 별도 대기다.
