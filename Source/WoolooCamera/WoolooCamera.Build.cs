using UnrealBuildTool;

public class WoolooCamera : ModuleRules
{
    public WoolooCamera(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.Default;
        bLegacyParentIncludePaths = true;

        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "ToolsetRegistry" });
        PrivateDependencyModuleNames.AddRange(new[] { "GameplayCameras", "UnrealEd", "AssetRegistry", "Json" });
    }
}
