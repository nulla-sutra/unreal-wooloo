using UnrealBuildTool;

public class WoolooAssets : ModuleRules
{
    public WoolooAssets(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.Default;
        bLegacyParentIncludePaths = true;

        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "ToolsetRegistry" });
        PrivateDependencyModuleNames.AddRange(new[] { "UnrealEd", "AssetRegistry", "AssetTools", "CollectionManager", "Json" });
        AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenSSL");
    }
}
