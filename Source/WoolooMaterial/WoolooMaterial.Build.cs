using UnrealBuildTool;

public class WoolooMaterial : ModuleRules
{
    public WoolooMaterial(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.Default;
        bLegacyParentIncludePaths = true;

        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "ToolsetRegistry" });
        PrivateDependencyModuleNames.AddRange(new[] { "UnrealEd", "AssetRegistry", "AssetTools", "Json", "MaterialEditor" });
    }
}
