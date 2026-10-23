using UnrealBuildTool;

public class WoolooStateTree : ModuleRules
{
    public WoolooStateTree(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.Default;

        // Resolve module-qualified headers through Unreal's parent include paths.
        bLegacyParentIncludePaths = true;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "ToolsetRegistry" });
        PrivateDependencyModuleNames.AddRange(new[]
        {
            "UnrealEd", "StateTreeModule", "StateTreeEditorModule", "PropertyBindingUtils",
            "AssetRegistry", "GameplayTags", "Json"
        });
    }
}
