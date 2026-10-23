using UnrealBuildTool;
using System.IO;

public class WoolooMutable : ModuleRules
{
    public WoolooMutable(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.Default;

        // Resolve module-qualified headers through Unreal's parent include paths.
        bLegacyParentIncludePaths = true;


        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core", "CoreUObject", "Engine", "CustomizableObject", "ToolsetRegistry"
        });

        PrivateDependencyModuleNames.AddRange(new[]
        {
            "AssetRegistry", "AssetTools", "CustomizableObjectEditor", "UnrealEd"
        });

        // UE 5.8 keeps its native hierarchy/compilation interface in Internal, not Public.
        // Expose only that module's headers to this editor bridge; engine files remain unchanged.
        PrivateIncludePaths.Add(Path.Combine(GetModuleDirectory("CustomizableObject"), "Internal"));
    }
}
