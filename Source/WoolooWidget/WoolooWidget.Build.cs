using UnrealBuildTool;

public class WoolooWidget : ModuleRules
{
    public WoolooWidget(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.Default;
        bLegacyParentIncludePaths = true;

        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "ToolsetRegistry" });
        PrivateDependencyModuleNames.AddRange(new[] {
            "UMG", "UMGEditor", "UnrealEd", "BlueprintGraph", "Json", "MovieScene", "SlateCore"
        });
    }
}
