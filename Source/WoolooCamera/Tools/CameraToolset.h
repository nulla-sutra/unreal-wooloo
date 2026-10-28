#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"
#include "CameraToolset.generated.h"

UCLASS(BlueprintType, Hidden)
class WOOLOOCAMERA_API UWoolooCameraToolset : public UToolsetDefinition
{
    GENERATED_BODY()

public:
    /** Replace one registered leaf node in a native Camera Rig, with Undo support.
     * AssetPath is a package name; NodePath is the exact Rig-relative UObject path.
     * Properties contains top-level editable fields in native ImportText syntax.
     * Preserves incoming execution slots, enabled state and graph position; uses NodeComment.
     * Refuses bindings and unsupported references. Requires stopped PIE and a closed Rig editor.
     * Keep that editor closed during this operation's Undo/Redo, then reopen to inspect the graph.
     * Returns JSON with success, asset_path, old_node_path, new_node_path, dirty and message.
     * Leaves the Rig dirty and needing a build. Does not build, save or use source control. */
    UFUNCTION(BlueprintCallable, meta=(AICallable), Category="Camera")
    static FString ReplaceCameraRigNode(const FString& AssetPath, const FString& AllowedPackageRoot,
        const FString& NodePath, const FString& NewNodeClassPath,
        const TMap<FString, FString>& Properties, const FString& NodeComment);
};
