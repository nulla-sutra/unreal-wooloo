using UnrealBuildTool;

public class WoolooBlueprint : ModuleRules
{
    public WoolooBlueprint(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.Default;

        // Resolve module-qualified headers through Unreal's parent include paths.
        bLegacyParentIncludePaths = true;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "ToolsetRegistry" });
        PrivateDependencyModuleNames.AddRange(new[] {
            "UnrealEd", "BlueprintGraph", "KismetCompiler", "Json", "EnhancedInput", "InputBlueprintNodes"
        });
    }
}
