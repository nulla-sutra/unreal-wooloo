#pragma once

#include "ToolsetRegistry/ToolsetDefinition.h"
#include "WoolooBlueprint/Tools/BlueprintToolTypes.h"
#include "BlueprintToolset.generated.h"

UCLASS(BlueprintType, Hidden)
class WOOLOOBLUEPRINT_API UWoolooBlueprintToolset : public UToolsetDefinition
{
    GENERATED_BODY()
public:
    /** Read graphs including native interface implementations, node GUIDs, pins, links, layout and comments. */
    UFUNCTION(BlueprintCallable, meta=(AICallable), Category="Blueprint")
    static FWoolooBlueprintResult DescribeBlueprint(const FString& AssetPath);
    /** Apply one targeted edit. Does not compile, save, or run the Blueprint. */
    UFUNCTION(BlueprintCallable, meta=(AICallable), Category="Blueprint")
    static FWoolooBlueprintResult EditBlueprint(const FString& AssetPath, const FString& AllowedPackageRoot,
        const FWoolooBlueprintEdit& Edit);
    /** Explicit editor Blueprint compilation, including diagnostic messages. Does not compile C++. */
    UFUNCTION(BlueprintCallable, meta=(AICallable), Category="Blueprint")
    static FWoolooBlueprintResult CompileBlueprint(const FString& AssetPath, const FString& AllowedPackageRoot);
    /** Save only the requested Blueprint package; no dependency saves or version-control changes. */
    UFUNCTION(BlueprintCallable, meta=(AICallable), Category="Blueprint")
    static FWoolooBlueprintResult SaveBlueprint(const FString& AssetPath, const FString& AllowedPackageRoot);
};
