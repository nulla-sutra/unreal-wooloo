#pragma once

#include "ToolsetRegistry/ToolsetDefinition.h"
#include "WoolooStateTree/Tools/StateTreeToolTypes.h"
#include "StateTreeToolset.generated.h"

UCLASS(BlueprintType, Hidden)
class WOOLOOSTATETREE_API UWoolooStateTreeToolset : public UToolsetDefinition
{
    GENERATED_BODY()
public:
    /** Read hierarchy, IDs, instance properties, parameters, property bindings and each node's accessible binding sources.
     * Does not repair the asset. */
    UFUNCTION(BlueprintCallable, meta=(AICallable), Category="StateTree")
    static FWoolooStateTreeResult DescribeStateTree(const FString& AssetPath);

    /** Create an unsaved tree using a native schema class. Existing assets are never overwritten. */
    UFUNCTION(BlueprintCallable, meta=(AICallable), Category="StateTree")
    static FWoolooStateTreeResult CreateStateTree(const FString& AssetPath, const FString& AllowedPackageRoot,
        const FString& SchemaClassPath);

    /** Apply one targeted edit within AllowedPackageRoot. Inspect FWoolooStateTreeEdit for operation fields. */
    UFUNCTION(BlueprintCallable, meta=(AICallable), Category="StateTree")
    static FWoolooStateTreeResult EditStateTree(const FString& AssetPath, const FString& AllowedPackageRoot,
        const FWoolooStateTreeEdit& Edit);

    /** Explicit native StateTree compilation. Returns compiler diagnostics; does not save. */
    UFUNCTION(BlueprintCallable, meta=(AICallable), Category="StateTree")
    static FWoolooStateTreeResult CompileStateTree(const FString& AssetPath, const FString& AllowedPackageRoot);

    /** Save only this package; no dependency saves or version-control operations. */
    UFUNCTION(BlueprintCallable, meta=(AICallable), Category="StateTree")
    static FWoolooStateTreeResult SaveStateTree(const FString& AssetPath, const FString& AllowedPackageRoot);
};
