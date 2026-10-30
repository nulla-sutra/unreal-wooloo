using UnrealBuildTool;

public class WoolooPIE : ModuleRules
{
    public WoolooPIE(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.Default;
        bLegacyParentIncludePaths = true;

        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "ToolsetRegistry" });
        PrivateDependencyModuleNames.AddRange(new[] { "UnrealEd", "EnhancedInput", "GameplayAbilities", "Json" });
    }
}
