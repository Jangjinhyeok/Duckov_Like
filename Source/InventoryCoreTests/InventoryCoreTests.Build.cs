using UnrealBuildTool;

public class InventoryCoreTests : ModuleRules
{
    public InventoryCoreTests(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PrivateDependencyModuleNames.AddRange(new[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "InventoryCore"
        });
    }
}
