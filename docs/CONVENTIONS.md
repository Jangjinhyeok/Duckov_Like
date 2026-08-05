# 프로젝트 컨벤션

UE 표준을 되풀이하지 않는다. 이 프로젝트에만 해당하는 규칙만 적는다.

## 1. 모듈 경계

| 모듈 | 책임 | 의존 |
| --- | --- | --- |
| `InventoryCore` | 그리드 인벤토리 Model — 그리드 상태, 배치 규칙, 연산 서비스, 저장 레코드 | `Core`, `CoreUObject`, `Engine` |
| `DuckovLike` | 게임 모듈 — ViewModel, 위젯, 액터, 이후 GAS | 위 + `InputCore`, `EnhancedInput`, `InventoryCore`, `UMG`, `ModelViewViewModel` |

- **`InventoryCore`는 `UMG`/`Slate`/`SlateCore`/`ModelViewViewModel`에 의존하지 않는다.**
  이 금지는 `InventoryCore.Build.cs`가 강제한다. UI 타입을 실제로 사용하면 링크가 깨진다.
- `InventoryCore`는 `Public/`·`Private/`를 나눈다. **다른 모듈에 노출할 타입만 `Public/`에 둔다.**
  `DuckovLike`는 소비자가 없는 primary game module이라 평면 구조를 유지한다.
- 결정 근거와 실측 결과는 [ADR-000](architecture/ADR-000-module-boundaries.md).

## 2. Content 폴더

| 경로 | 용도 |
| --- | --- |
| `Content/Inventory/` | 아이템 Definition 에셋 |
| `Content/UI/` | 위젯 블루프린트 |
| `Content/Maps/` | 레벨 |

**폴더는 실제 에셋이 생길 때 만든다.** 빈 폴더를 미리 만들지 않는다
(git이 추적하지 못하고, 규약은 이 문서가 이미 담고 있다).

## 3. 네이밍

UE 표준 접두어를 따른다 — `U`/`A`/`F`/`E`/`I`, 에셋은 `BP_`/`WBP_`/`DA_`/`T_`/`M_`.
아래는 프로젝트 고유 규칙이다.

- 인벤토리 Model 타입은 접두어 뒤를 `Inventory` 또는 `Item`으로 시작한다.
  예: `UInventoryContainer`, `FItemInstanceId`
- ViewModel은 `UVM_` 접두어가 아니라 `...ViewModel` 접미어를 쓴다 (MVVM 플러그인 관례).
  예: `UContainerViewModel`, `UInteractionViewModel`

## 4. 언어

- 코드 주석, 커밋 메시지: **영어**
- 설계 문서(`docs/`): **한국어**

## 5. 좌표 규약

좌상단 원점, X=열 / Y=행, 좌상단 앵커, 0/90도 회전.

**이 문서에서 확정하지 않는다.** 인벤토리 설계 문서의 E3 항목이며 아직 `Proposed` 상태다.
확정은 M1 전 ADR 승인 시점에 일어난다 → [INVENTORY_DESIGN.md](INVENTORY_DESIGN.md)

## 6. 빌드

```
"C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\Build.bat" DuckovLikeEditor Win64 Development -Project="<repo>\DuckovLike.uproject" -WaitMutex -NoHotReload
```

`Binaries/`, `Intermediate/`, `Saved/`, `DerivedDataCache/`는 커밋하지 않는다(`.gitignore`).
바이너리 에셋은 Git LFS로 추적한다(`.gitattributes`).
