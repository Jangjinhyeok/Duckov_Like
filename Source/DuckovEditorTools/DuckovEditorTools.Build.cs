using System.IO;
using UnrealBuildTool;

// 에디터 전용 도구 모듈이다. Game target에는 포함되지 않으며 DuckovLike(런타임)가 이 모듈을 알지 못한다.
public class DuckovEditorTools : ModuleRules
{
    public DuckovEditorTools(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        // DuckovLike는 Public/Private 구분 없는 평면 모듈이라 이 모듈이 쓰는 헤더는 모듈 루트에서 직접 포함한다.
        PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "..", "DuckovLike"));

        PrivateDependencyModuleNames.AddRange(new[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "UnrealEd",
            "DataValidation",
            "DuckovLike"
        });
    }
}
