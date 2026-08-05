using UnrealBuildTool;

// InventoryCore는 그리드 인벤토리의 순수 Model 계층이다.
// 그리드 상태, 배치 규칙, 연산 서비스, 저장 레코드가 여기 산다.
//
// 이 모듈은 UI 모듈(UMG, Slate, SlateCore, ModelViewViewModel)에 절대 의존하지 않는다.
// View -> ViewModel -> Model 단방향 의존은 규율이 아니라 이 파일이 강제한다 —
// 여기 없는 모듈의 .lib는 링크 라인에 오르지 않으므로, UI 타입을 실제로 사용하는
// 코드는 LNK2019로 빌드가 깨진다.
//
// 상세는 docs/architecture/ADR-000-module-boundaries.md 참조.
public class InventoryCore : ModuleRules
{
	public InventoryCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });

		PrivateDependencyModuleNames.AddRange(new string[] {  });
	}
}
